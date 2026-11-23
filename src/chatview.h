// Copyright (c) 2026 Peter Kokosharov <peter.kokosharov@gmail.com>
// LLM: Claude Sonnet 5.5 (claude-sonnet-5-5) - chat pane with Markdown, images and diagrams
// Licensed under MIT License.
//
// The chat pane is a plain QTextBrowser (no WebEngine). While SISI is answering the text is
// streamed in as it comes; when the answer is finished it is replaced by its Markdown
// rendering: headings, lists, code blocks, links, local images and diagrams.
#ifndef SISI_CHATVIEW_H
#define SISI_CHATVIEW_H

#include <QHash>
#include <QImage>
#include <QList>
#include <QSet>
#include <QString>
#include <QTextBrowser>

class DiagramRenderer;

class ChatView : public QTextBrowser {
    Q_OBJECT
public:
    explicit ChatView(QWidget *parent = nullptr);

    // Relative image paths in answers are taken from this folder.
    void setWorkspace(const QString &dir) { workspace_ = dir; }

    void addUser(const QString &text);
    void addStatus(const QString &text);        // a grey info or error line
    void addToken(const QString &delta);        // streaming: extends the current SISI block
    void addAnswer(const QString &text);        // a finished answer that was not streamed
    void finishAnswers();                       // the turn ended: render streamed answers as Markdown
    void clearChat();

private slots:
    void onDiagramDone(const QString &key, const QImage &image, const QString &error);

private:
    enum class Kind { User, Sisi, Status };
    struct Entry {
        Kind kind;
        QString text;
        bool done;          // SISI only: false while the text is still streaming (shown plain)
    };

    void insertPlain(Kind kind, const QString &text);
    void insertMarkdown(const QString &text);
    QString prepareMarkdown(const QString &md);
    QString rewriteImages(const QString &text);
    void requestDiagram(const QString &key, const QString &lang, const QString &body);
    void addImageResource(const QString &name, const QImage &image);
    void rebuild();
    void scrollDown();

    QList<Entry> entries_;
    bool streaming_ = false;       // the last entry is a SISI answer that tokens can extend
    QString workspace_;
    DiagramRenderer *diagrams_;
    QHash<QString, QImage> files_;           // local image files already read (null = unreadable)
    QHash<QString, QImage> diagramImages_;   // rendered diagrams by source hash
    QSet<QString> requested_;                // diagram hashes already sent to the renderer
};

#endif
