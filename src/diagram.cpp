// Copyright (c) 2026 Peter Kokosharov <peter.kokosharov@gmail.com>
// LLM: Claude Sonnet 5.5 (claude-sonnet-5-5) - diagram rendering through external tools
// Licensed under MIT License.
#include "diagram.h"

#include <memory>

#include <QCoreApplication>
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

// merman-cli: the configured path, else next to the exe, else on PATH.
QString findMerman() {
    if (!g_cfg.merman.empty()) return QString::fromStdString(g_cfg.merman);
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("merman-cli.exe");
#else
    const QString name = QStringLiteral("merman-cli");
#endif
    const QString beside = QCoreApplication::applicationDirPath() + QLatin1Char('/') + name;
    if (QFileInfo::exists(beside)) return beside;
    return QStandardPaths::findExecutable(QStringLiteral("merman-cli"));
}

// What a running job owns; shared by the lambdas that watch the process.
struct Job {
    QString key;
    QString lang;
    QString outPng;                          // mermaid: the file merman writes
    std::unique_ptr<QTemporaryDir> dir;      // removed with the job
    bool ended = false;
};

}  // namespace

bool DiagramRenderer::available(const QString &lang, QString *why) const {
    if (why) why->clear();
    if (lang == QLatin1String("mermaid")) {
        if (!findMerman().isEmpty()) return true;
        if (why) *why = tr("merman-cli not found (see merman in sisi.ini)");
        return false;
    }
    if (lang == QLatin1String("plantuml")) {
        // Not configured is not an error: the block just stays code.
        if (g_cfg.plantumlJar.empty()) return false;
        if (QStandardPaths::findExecutable(QStringLiteral("java")).isEmpty()) {
            if (why) *why = tr("java not found, needed for plantuml_jar");
            return false;
        }
        return true;
    }
    return false;
}

void DiagramRenderer::render(const QString &key, const QString &lang, const QString &source) {
    auto job = std::make_shared<Job>();
    job->key = key;
    job->lang = lang;
    const bool mermaid = (lang == QLatin1String("mermaid"));

    // done() is always delivered from the event loop, also for immediate failures.
    auto fail = [this, key](const QString &error) {
        QMetaObject::invokeMethod(this, [this, key, error] { emit done(key, QImage(), error); },
                                  Qt::QueuedConnection);
    };

    auto *proc = new QProcess(this);
    QStringList args;
    QString program;
    if (mermaid) {
        job->dir = std::make_unique<QTemporaryDir>();
        if (!job->dir->isValid()) { proc->deleteLater(); fail(tr("cannot create a temporary folder")); return; }
        const QString in = job->dir->filePath(QStringLiteral("in.mmd"));
        job->outPng = job->dir->filePath(QStringLiteral("out.png"));
        QFile f(in);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            proc->deleteLater();
            fail(tr("cannot write the temporary file"));
            return;
        }
        f.write(source.toUtf8());
        f.close();
        program = findMerman();
        args << QStringLiteral("render") << in << QStringLiteral("-o") << job->outPng
             << QStringLiteral("-f") << QStringLiteral("png") << QStringLiteral("-q");
    } else {
        program = QStandardPaths::findExecutable(QStringLiteral("java"));
        args << QStringLiteral("-jar") << QString::fromStdString(g_cfg.plantumlJar)
             << QStringLiteral("-tpng") << QStringLiteral("-pipe");
    }

    // Ends the job exactly once: whoever comes first (finished, error, timeout) wins.
    auto end = [this, proc, job](const QImage &image, const QString &error) {
        if (job->ended) return;
        job->ended = true;
        proc->disconnect(this);
        proc->deleteLater();
        // Queued: errorOccurred can fire inside proc->start(), and done() must not.
        QMetaObject::invokeMethod(this, [this, job, image, error] { emit done(job->key, image, error); },
                                  Qt::QueuedConnection);
    };

    connect(proc, &QProcess::errorOccurred, this, [proc, end](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart) end(QImage(), QStringLiteral("cannot start: ") + proc->errorString());
    });
    connect(proc, &QProcess::finished, this,
            [proc, job, mermaid, end](int code, QProcess::ExitStatus status) {
        if (status != QProcess::NormalExit || code != 0) {
            QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed().left(160);
            end(QImage(), err.isEmpty() ? QStringLiteral("exit code %1").arg(code) : err);
            return;
        }
        QImage img = mermaid ? QImage(job->outPng)
                             : QImage::fromData(proc->readAllStandardOutput(), "PNG");
        end(img, img.isNull() ? QStringLiteral("no image produced") : QString());
    });
    if (!mermaid)       // PlantUML reads its source from stdin
        connect(proc, &QProcess::started, this, [proc, source] {
            proc->write(source.toUtf8());
            proc->closeWriteChannel();
        });

    QTimer::singleShot(kTimeoutMs, proc, [proc, end] {        // dies with proc if it finished earlier
        end(QImage(), QStringLiteral("timed out after 15 s"));
        proc->kill();
    });

    proc->start(program, args);
}
