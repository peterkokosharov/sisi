// Copyright (c) 2026 Peter Kokosharov <peter.kokosharov@gmail.com>
// LLM: Claude Sonnet 5.5 (claude-sonnet-5-5) - rex child process and JSONL protocol
// Licensed under MIT License.
#include "rexbridge.h"

#include <QDir>
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
    // A key from the config, else one already in the environment, else a dummy
    // (a local LiteLLM usually needs none).
    if (!g_cfg.apiKey.empty())
        env.insert(QStringLiteral("RUNTIME_LITELLM__API_KEY"), QString::fromStdString(g_cfg.apiKey));
    else if (!env.contains(QStringLiteral("RUNTIME_LITELLM__API_KEY")))
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

    lastStderr_.clear();
    errBuf_.clear();
    proc_ = new QProcess(this);
    proc_->setProcessEnvironment(env);
    if (!workspace.isEmpty()) proc_->setWorkingDirectory(workspace);
    proc_->setProcessChannelMode(QProcess::SeparateChannels);   // stdout = JSON lines, stderr = noise
    connect(proc_, &QProcess::readyReadStandardOutput, this, &RexBridge::onReadyRead);
    connect(proc_, &QProcess::readyReadStandardError, this, &RexBridge::onStderr);
    connect(proc_, &QProcess::finished, this, &RexBridge::onFinished);
    connect(proc_, &QProcess::errorOccurred, this, &RexBridge::onError);
    proc_->start(python, args);
    // Waiting here makes a bad python path or workspace fail right away
    // (onError reports it) and means a chat line can be written at once.
    return proc_->waitForStarted(5000);
}

// Blocks the GUI for up to about 3 seconds in the worst case; that is acceptable
// for a quit or a restart, and it guarantees no orphaned rex is left behind.
void RexBridge::stop() {
    if (!proc_) return;
    QProcess *p = proc_;
    proc_ = nullptr;
    p->disconnect(this);                     // no more signals from the old process
    if (p->state() != QProcess::NotRunning) {
        p->closeWriteChannel();              // rex ends by itself when its stdin closes
        if (!p->waitForFinished(1500)) {
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
    }
    delete p;
    buf_.clear();
}

bool RexBridge::send(const QJsonObject &obj) {
    if (!alive()) return false;
    const QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n";
    return proc_->write(data) == data.size();
}

// ---------------------------------------------------------------------------
// Reading from the process
// ---------------------------------------------------------------------------

void RexBridge::onReadyRead() {
    if (!proc_) return;
    buf_ += proc_->readAllStandardOutput();
    drainStdout(false);
}

// Hands every complete line in buf_ to handleLine; at the end also a last line without newline.
void RexBridge::drainStdout(bool atEnd) {
    qsizetype nl;
    while ((nl = buf_.indexOf('\n')) >= 0) {
        QByteArray raw = buf_.left(nl);
        buf_.remove(0, nl + 1);
        if (raw.endsWith('\r')) raw.chop(1);
        if (!raw.isEmpty()) handleLine(QString::fromUtf8(raw));
    }
    if (atEnd && !buf_.isEmpty()) {
        const QByteArray raw = buf_.trimmed();
        buf_.clear();
        if (!raw.isEmpty()) handleLine(QString::fromUtf8(raw));
    }
}

void RexBridge::onStderr() {
    if (!proc_) return;
    errBuf_ += proc_->readAllStandardError();
    drainStderr(false);
}

// Like drainStdout: only complete lines, the unfinished rest waits for more data (or the exit).
void RexBridge::drainStderr(bool atEnd) {
    qsizetype nl;
    while ((nl = errBuf_.indexOf('\n')) >= 0 || (atEnd && !errBuf_.isEmpty())) {
        const QByteArray raw = (nl >= 0) ? errBuf_.left(nl) : errBuf_;
        errBuf_.remove(0, (nl >= 0) ? nl + 1 : errBuf_.size());
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty()) continue;
        logLine("rex stderr> " + line.toStdString());
        lastStderr_ = line;
        emit toolLog(QStringLiteral("[rex stderr] ") + line);
    }
}

void RexBridge::onFinished(int code, QProcess::ExitStatus status) {
    if (proc_) errBuf_ += proc_->readAllStandardError();
    drainStderr(true);
    if (proc_) buf_ += proc_->readAllStandardOutput();
    drainStdout(true);
    const bool ok = (status == QProcess::NormalExit && code == 0);
    QString m = (status == QProcess::NormalExit) ? QStringLiteral("[rex exited, code %1]").arg(code)
                                                 : QStringLiteral("[rex crashed]");
    logLine(m.toStdString());
    if (!ok) {
        if (!lastStderr_.isEmpty()) m += QStringLiteral(" ") + lastStderr_;
        emit info(m);
    }
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

// ---------------------------------------------------------------------------
// One JSON line from rex
// ---------------------------------------------------------------------------

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

    if (type == QLatin1String("token")) {
        emit token(o.value(QStringLiteral("text")).toString());
    } else if (type == QLatin1String("tool_start")) {
        const QString name = o.value(QStringLiteral("name")).toString();
        const QJsonObject args = o.value(QStringLiteral("args")).toObject();
        QString cmd = firstString(args, {QStringLiteral("command"), QStringLiteral("cmd"),
                                         QStringLiteral("script")});
        if (cmd.isEmpty())
            cmd = QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact));
        emit toolLog(QStringLiteral("$ [") + name + QStringLiteral("] ") + cmd);
    } else if (type == QLatin1String("tool_end")) {
        // A failing tool is only logged in the bash pane; the turn goes on.
        const QJsonValue e = o.value(QStringLiteral("error"));
        const bool failed = !o.value(QStringLiteral("ok")).toBool(true) || !(e.isNull() || e.isUndefined());
        QString why = e.isString() ? e.toString() : QString();
        emit toolLog(failed ? QStringLiteral("  (failed: ") + why + QLatin1Char(')')
                            : QStringLiteral("  (done)"));
    } else if (type == QLatin1String("final")) {
        emit reply(o.value(QStringLiteral("text")).toString());
        emit turnDone();
    } else if (type == QLatin1String("error")) {
        QString msg = o.value(QStringLiteral("message")).toString();
        if (msg.isEmpty()) msg = line;
        emit info(QStringLiteral("[rex error] ") + msg);
        emit turnDone();
    } else if (type == QLatin1String("reset_ok")) {
        emit info(QStringLiteral("[context reset]"));
    } else if (type == QLatin1String("bye")) {
        emit turnDone();
    }
    // ready and model_ok need no reaction; every line is in the log anyway.
}
