// Copyright (c) 2026 Peter Kokosharov <peter.kokosharov@gmail.com>
// LLM: Claude Sonnet 5.5 (claude-sonnet-5-5) - chat pane with Markdown, images and diagrams
// Licensed under MIT License.
//
// The chat pane is a plain QTextBrowser (no WebEngine). While SISI is answering the text is
// streamed in as it comes; when the answer is finished it is replaced by its Markdown
// rendering: headings, lists, code blocks, links, workspace images and diagrams.
#ifndef SISI_CHATVIEW_H
#define SISI_CHATVIEW_H

#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTextBrowser>
#include <QVariant>

class DiagramRenderer;
class QResizeEvent;
class QTimer;

class ChatView : public QTextBrowser {
    Q_OBJECT
public:
    explicit ChatView(QWidget *parent = nullptr);

    // Images in answers are only shown from inside this folder; relative paths start here.
    void setWorkspace(const QString &dir) { workspace_ = dir; }

    void addUser(const QString &text);
    void addStatus(const QString &text);        // a grey info or error line
    void addToken(const QString &delta);        // streaming: extends the current SISI block
    void addAnswer(const QString &text);        // a finished answer that was not streamed
    void finishAnswers();                       // the turn ended: render streamed answers as Markdown
    void clearChat();

protected:
    // Never fetch images by themselves (no files, no network); only our own resources show.
    QVariant loadResource(int type, const QUrl &name) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onDiagramDone(const QString &key, const QImage &image, const QString &error);
    void onResizeSettled();

private:
    enum class Kind { User, Sisi, Status };
    struct Entry {
        Kind kind;
        QString text;
        bool done;          // SISI only: false while the text is still streaming (shown plain)
    };
    struct CachedImage {    // a file read once; reread when its time or size changes
        QImage image;
        QDateTime modified;
        qint64 size = 0;
    };

    void insertPlain(Kind kind, const QString &text);
    void insertMarkdown(const QString &text);
    QString prepareMarkdown(const QString &md);
    QString rewriteImages(const QString &text);
    QString insideWorkspace(const QString &path) const;
    QImage loadImage(const QString &file);
    void requestDiagram(const QString &key, const QString &body);
    void addImageResource(const QString &name, const QImage &image);
    void note(const QString &text);
    bool answering() const;
    void rebuild();
    void scrollDown();

    QList<Entry> entries_;
    bool streaming_ = false;       // the last entry is a SISI answer that tokens can extend
    QString workspace_;
    DiagramRenderer *diagrams_;
    QTimer *resizeTimer_;
    int renderedWidth_ = 0;        // pane width the images were last scaled for
    QHash<QString, CachedImage> files_;      // workspace images by canonical path
    QHash<QString, QImage> diagramImages_;   // rendered diagrams by source hash
    QSet<QString> requested_;                // diagram hashes already sent to the renderer
    QStringList pendingNotes_;               // status lines held back while an answer streams
};

#endif
