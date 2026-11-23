// Copyright (c) 2026 Peter Kokosharov <peter.kokosharov@gmail.com>
// LLM: Claude Sonnet 5.5 (claude-sonnet-5-5) - diagram rendering through external tools
// Licensed under MIT License.
//
// Turns the source of a ```mermaid or ```plantuml block into a PNG image by running
// an external program (merman-cli, or java -jar plantuml.jar) in a QProcess.
// Nothing blocks the GUI: the answer arrives through the done() signal.
#ifndef SISI_DIAGRAM_H
#define SISI_DIAGRAM_H

#include <QImage>
#include <QObject>
#include <QString>

class DiagramRenderer : public QObject {
    Q_OBJECT
public:
    explicit DiagramRenderer(QObject *parent = nullptr) : QObject(parent) {}

    // True if blocks of this language ("mermaid", "plantuml") can be rendered now.
    // If not, `why` says what is missing; it stays empty when the feature is simply not set up.
    bool available(const QString &lang, QString *why) const;

    // Starts a render job; done() follows later (never from inside this call).
    void render(const QString &key, const QString &lang, const QString &source);

signals:
    // image is null when it failed; error then says why.
    void done(const QString &key, const QImage &image, const QString &error);
};

#endif
