// Copyright (c) 2026 Peter Kokosharov <peter.kokosharov@gmail.com>
// LLM: Claude Sonnet 5.5 (claude-sonnet-5-5) - rex child process and JSONL protocol
// Licensed under MIT License.
#include "rexbridge.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcessEnvironment>
#include <QStringList>

#include "config.h"

RexBridge::RexBridge(QObject *parent) : QObject(parent) {}

RexBridge::~RexBridge() { stop(); }

bool RexBridge::alive() const { return proc_ && proc_->state() == QProcess::Running; }

bool RexBridge::start(const QString &endpoint, const QString &model, const QString &workspace) {
    stop();
    const QString python = QString::fromStdString(g_cfg.python);
    const QString rexDir = QString::fromStdString(g_cfg.rexDir);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("RUNTIME_LITELLM__BASE_URL"), endpoint);
    env.insert(QStringLiteral("RUNTIME_LITELLM__API_KEY"), QStringLiteral("dummy"));
    if (!rexDir.isEmpty()) {
        QString pp = rexDir;
        const QString old = env.value(QStringLiteral("PYTHONPATH"));
        if (!old.isEmpty()) pp += QDir::listSeparator() + old;
        env.insert(QStringLiteral("PYTHONPATH"), pp);
    }

    QStringList args;
    args << QStringLiteral("-m") << QStringLiteral("runtime") << QStringLiteral("--model") << model
         << QStringLiteral("--jsonl") << QStringLiteral("--local-tools");
    logLine("rex start: " + python.toStdString() + " " + args.join(QLatin1Char(' ')).toStdString() +
            "  (cwd " + workspace.toStdString() + ", PYTHONPATH prefix " + rexDir.toStdString() + ")");

    proc_ = new QProcess(this);
    proc_->setProcessEnvironment(env);
    if (!workspace.isEmpty()) proc_->setWorkingDirectory(workspace);
    proc_->setProcessChannelMode(QProcess::MergedChannels);   // stderr noise arrives as text lines
    connect(proc_, &QProcess::readyRead, this, &RexBridge::onReadyRead);
    connect(proc_, &QProcess::finished, this, &RexBridge::onFinished);
    connect(proc_, &QProcess::errorOccurred, this, &RexBridge::onError);
    proc_->start(python, args);
    // Waiting here makes a bad python path or workspace fail right away
    // (onError reports it) and means a chat line can be written at once.
    return proc_->waitForStarted(5000);
}

void RexBridge::stop() {
    if (!proc_) return;
    QProcess *p = proc_;
    proc_ = nullptr;
    p->disconnect(this);                 // no more signals from the old process
    if (p->state() != QProcess::NotRunning) {
        p->closeWriteChannel();          // rex ends when its stdin closes
#ifdef Q_OS_WIN
        p->kill();                       // terminate() does nothing for a windowless console process
#else
        p->terminate();
#endif
        if (!p->waitForFinished(1500)) {
            p->kill();
            p->waitForFinished(1500);
        }
    }
    delete p;
    buf_.clear();
    assistant_.clear();
}

bool RexBridge::send(const QJsonObject &obj) {
    if (!alive()) return false;
    const QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n";
    return proc_->write(data) == data.size();
}

void RexBridge::onReadyRead() {
    if (!proc_) return;
    buf_ += proc_->readAll();
    int nl;
    while ((nl = buf_.indexOf('\n')) >= 0) {
        QByteArray raw = buf_.left(nl);
        buf_.remove(0, nl + 1);
        if (raw.endsWith('\r')) raw.chop(1);
        if (raw.isEmpty()) continue;
        handleLine(QString::fromUtf8(raw));
    }
}

void RexBridge::onFinished(int code, QProcess::ExitStatus status) {
    if (proc_) buf_ += proc_->readAll();
    if (!buf_.isEmpty()) {                       // last line without a newline
        QByteArray raw = buf_;
        buf_.clear();
        if (raw.endsWith('\r')) raw.chop(1);
        if (!raw.isEmpty()) handleLine(QString::fromUtf8(raw));
    }
    const QString m = (status == QProcess::NormalExit)
                          ? QStringLiteral("[rex exited, code %1]").arg(code)
                          : QStringLiteral("[rex crashed]");
    logLine(m.toStdString());
    assistant_.clear();
    emit info(m);
    emit exited();
}

void RexBridge::onError(QProcess::ProcessError err) {
    if (err != QProcess::FailedToStart) return;   // a crash is reported by finished()
    const QString m = QStringLiteral("[error] cannot start %1: %2")
                          .arg(QString::fromStdString(g_cfg.python),
                               proc_ ? proc_->errorString() : QString());
    logLine(m.toStdString());
    emit info(m);
    emit exited();
}

static QString firstString(const QJsonObject &o, const QStringList &keys) {
    for (const QString &k : keys) {
        const QJsonValue v = o.value(k);
        if (v.isString() && !v.toString().isEmpty()) return v.toString();
    }
    return QString();
}

void RexBridge::handleLine(const QString &line) {
    logLine("rex> " + line.toStdString());
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        emit info(QStringLiteral("[rex output] ") + line);   // not JSON: noise, often a traceback
        return;
    }
    const QJsonObject o = doc.object();
    const QString type = o.value(QStringLiteral("type")).toString();

    // Tool events feed the bash pane. rex sends the tool name and arguments
    // (tool_start) and ok/error (tool_end); tool_output is read if a rex sends it.
    if (type == QLatin1String("tool_start")) {
        const QString name = o.value(QStringLiteral("name")).toString();
        const QJsonObject args = o.value(QStringLiteral("args")).toObject();
        QString cmd = firstString(args, {QStringLiteral("command"), QStringLiteral("cmd"),
                                         QStringLiteral("script")});
        if (cmd.isEmpty())
            cmd = QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact));
        emit toolLog(QStringLiteral("$ [") + name + QStringLiteral("] ") + cmd);
        return;
    }
    if (type == QLatin1String("tool_output")) {
        QString text = firstString(o, {QStringLiteral("text"), QStringLiteral("data"),
                                       QStringLiteral("output"), QStringLiteral("chunk")});
        while (text.endsWith(QLatin1Char('\n')) || text.endsWith(QLatin1Char('\r'))) text.chop(1);
        if (!text.isEmpty()) emit toolLog(text);
        return;
    }
    if (type == QLatin1String("tool_end")) {
        const QJsonValue e = o.value(QStringLiteral("error"));
        const bool ok = o.value(QStringLiteral("ok")).toBool(true);
        const bool noError = e.isNull() || e.isUndefined();
        emit toolLog((ok && noError) ? QStringLiteral("  (done)") : QStringLiteral("  (failed)"));
        // falls through: an "error" field is shown in the chat below, but it does NOT end the turn
    }

    // An error: type "error", or an "error" field that is not null/false/empty.
    QString errMsg;
    bool isErr = (type == QLatin1String("error"));
    const QJsonValue ev = o.value(QStringLiteral("error"));
    if (ev.isString()) {
        if (!ev.toString().isEmpty()) { errMsg = ev.toString(); isErr = true; }
    } else if (ev.isObject()) {
        errMsg = ev.toObject().value(QStringLiteral("message")).toString();
        isErr = true;
    } else if (ev.isArray() || ev.isDouble() || (ev.isBool() && ev.toBool())) {
        isErr = true;
    }
    if (type != QLatin1String("token") && isErr) {
        if (errMsg.isEmpty()) errMsg = o.value(QStringLiteral("message")).toString();
        if (errMsg.isEmpty()) errMsg = line;
        emit info(QStringLiteral("[rex error] ") + errMsg);
        // Only an error or final event ends the turn, a failing tool does not.
        if (type == QLatin1String("error") || type == QLatin1String("final")) {
            assistant_.clear();
            emit turnDone();
        }
        return;
    }

    if (type == QLatin1String("token")) {
        assistant_ += o.value(QStringLiteral("text")).toString();
        emit stream(assistant_);
    } else if (type == QLatin1String("final")) {
        QString text = o.value(QStringLiteral("text")).toString();
        if (text.isEmpty()) text = assistant_;
        assistant_.clear();
        if (text.isEmpty()) emit info(QStringLiteral("[rex returned an empty reply]"));
        else emit reply(text);
        emit turnDone();
    } else if (type == QLatin1String("ready")) {
        emit info(QStringLiteral("[rex ready] model=") + o.value(QStringLiteral("model")).toString());
    } else if (type == QLatin1String("reset_ok")) {
        emit info(QStringLiteral("[context reset]"));
    } else if (type == QLatin1String("model_ok")) {
        emit info(QStringLiteral("[model] ") + o.value(QStringLiteral("model")).toString());
    } else if (type == QLatin1String("bye")) {
        assistant_.clear();
        emit turnDone();
    }
}
