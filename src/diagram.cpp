// Copyright (c) 2026 Peter Kokosharov <peter.kokosharov@gmail.com>
// LLM: Claude Sonnet 5.5 (claude-sonnet-5-5) - mermaid rendering through merman-cli
// Licensed under MIT License.
#include "diagram.h"

#include <memory>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>

#include "config.h"

namespace {

const int kTimeoutMs = 15000;
const int kMaxJobs = 2;          // merman processes running at the same time

// merman-cli: inside the folder MERMAN_HOME, else the path in sisi.ini, else on PATH.
QString findMerman() {
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("merman-cli.exe");
#else
    const QString name = QStringLiteral("merman-cli");
#endif
    const QString home = qEnvironmentVariable("MERMAN_HOME");
    if (!home.isEmpty()) {
        const QString p = QDir(home).filePath(name);
        if (QFileInfo::exists(p)) return p;
    }
    if (!g_cfg.merman.empty()) return QString::fromStdString(g_cfg.merman);
    return QStandardPaths::findExecutable(QStringLiteral("merman-cli"));
}

// What a running job owns; shared by the lambdas that watch the process.
struct Job {
    QString key;
    QString outPng;                          // the file merman writes
    std::unique_ptr<QTemporaryDir> dir;      // removed with the job
    bool ended = false;
};

}  // namespace

bool DiagramRenderer::available(QString *why) const {
    if (!findMerman().isEmpty()) return true;
    if (why) *why = tr("merman-cli not found (set MERMAN_HOME, or merman in sisi.ini)");
    return false;
}

DiagramRenderer::~DiagramRenderer() {
    // The running processes are children of this object. Stop them now, so no merman is
    // still writing into a temporary folder that is being removed.
    for (QProcess *p : findChildren<QProcess *>()) {
        p->disconnect(this);
        p->kill();
        p->waitForFinished(1000);
    }
}

void DiagramRenderer::render(const QString &key, const QString &source) {
    queue_.append(Pending{key, source});
    pump();
}

void DiagramRenderer::pump() {
    while (running_ < kMaxJobs && !queue_.isEmpty()) startJob(queue_.takeFirst());
}

void DiagramRenderer::startJob(const Pending &pending) {
    const QString key = pending.key;
    const QString source = pending.source;
    auto job = std::make_shared<Job>();
    job->key = key;
    ++running_;

    // done() is always delivered from the event loop, also for immediate failures;
    // then the next queued job may start.
    auto fail = [this, key](const QString &error) {
        --running_;
        QMetaObject::invokeMethod(this, [this, key, error] { emit done(key, QImage(), error); pump(); },
                                  Qt::QueuedConnection);
    };

    job->dir = std::make_unique<QTemporaryDir>();
    if (!job->dir->isValid()) { fail(tr("cannot create a temporary folder")); return; }
    const QString in = job->dir->filePath(QStringLiteral("in.mmd"));
    job->outPng = job->dir->filePath(QStringLiteral("out.png"));
    QFile f(in);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { fail(tr("cannot write the temporary file")); return; }
    f.write(source.toUtf8());
    f.close();

    auto *proc = new QProcess(this);

    // Ends the job exactly once: whoever comes first (finished, error, timeout) wins.
    auto end = [this, proc, job](const QImage &image, const QString &error) {
        if (job->ended) return;
        job->ended = true;
        --running_;
        proc->disconnect(this);
        proc->deleteLater();
        // Queued: errorOccurred can fire inside proc->start(), and done() must not.
        QMetaObject::invokeMethod(this, [this, job, image, error] { emit done(job->key, image, error); pump(); },
                                  Qt::QueuedConnection);
    };

    connect(proc, &QProcess::errorOccurred, this, [proc, end](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart) end(QImage(), QStringLiteral("cannot start: ") + proc->errorString());
    });
    connect(proc, &QProcess::finished, this, [proc, job, end](int code, QProcess::ExitStatus status) {
        if (status != QProcess::NormalExit || code != 0) {
            const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed().left(160);
            end(QImage(), err.isEmpty() ? QStringLiteral("exit code %1").arg(code) : err);
            return;
        }
        const QImage img(job->outPng);
        end(img, img.isNull() ? QStringLiteral("no image produced") : QString());
    });

    QTimer::singleShot(kTimeoutMs, proc, [proc, end] {        // dies with proc if it finished earlier
        end(QImage(), QStringLiteral("timed out after 15 s"));
        proc->kill();
    });

    proc->start(findMerman(), {QStringLiteral("render"), in, QStringLiteral("-o"), job->outPng,
                               QStringLiteral("-f"), QStringLiteral("png"), QStringLiteral("-q")});
}
