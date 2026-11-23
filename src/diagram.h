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
#include <QObject>
#include <QString>

class DiagramRenderer : public QObject {
    Q_OBJECT
public:
    explicit DiagramRenderer(QObject *parent = nullptr) : QObject(parent) {}

    // True if merman-cli can be found; if not, `why` says what to set.
    bool available(QString *why) const;

    // Starts a render job; done() follows later (never from inside this call).
    void render(const QString &key, const QString &source);

signals:
    // image is null when it failed; error then says why.
    void done(const QString &key, const QImage &image, const QString &error);
};

#endif
