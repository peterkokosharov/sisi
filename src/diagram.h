// Copyright (c) 2026 Peter Kokosharov <peter.kokosharov@gmail.com>
// LLM: Claude Sonnet 5.5 (claude-sonnet-5-5) - mermaid rendering through merman-cli
// Licensed under MIT License.
//
// Fallback for a ```mermaid block in an answer: the source is turned into a PNG by the
// external program merman-cli, run in a QProcess. Nothing blocks the GUI: the result
// arrives through the done() signal.
#ifndef SISI_DIAGRAM_H
#define SISI_DIAGRAM_H

#include <QImage>
#include <QList>
#include <QObject>
#include <QString>

class DiagramRenderer : public QObject {
    Q_OBJECT
public:
    explicit DiagramRenderer(QObject *parent = nullptr) : QObject(parent) {}
    ~DiagramRenderer() override;       // ends running merman processes before their temp folders go

    // True if merman-cli can be found; if not, `why` says what to set.
    bool available(QString *why) const;

    // Queues a render job (at most two merman processes run at once); done() follows later,
    // never from inside this call.
    void render(const QString &key, const QString &source);

signals:
    // image is null when it failed; error then says why.
    void done(const QString &key, const QImage &image, const QString &error);

private:
    struct Pending { QString key; QString source; };
    void pump();                                   // starts queued jobs while there is room
    void startJob(const Pending &job);

    QList<Pending> queue_;
    int running_ = 0;
};

#endif
