// Copyright (c) 2026 Peter Kokosharov <peter.kokosharov@gmail.com>
// LLM: Claude Sonnet 5.5 (claude-sonnet-5-5) - chat pane with Markdown, images and diagrams
// Licensed under MIT License.
#include "chatview.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QUrl>
#include <QVariant>

#include "diagram.h"
#include "markdown.h"

ChatView::ChatView(QWidget *parent)
    : QTextBrowser(parent), diagrams_(new DiagramRenderer(this)), resizeTimer_(new QTimer(this)) {
    setReadOnly(true);
    setOpenExternalLinks(true);     // http(s) and mailto links go to the system browser
    setOpenLinks(false);            // everything else must not replace the chat with a page
    connect(diagrams_, &DiagramRenderer::done, this, &ChatView::onDiagramDone);
    resizeTimer_->setSingleShot(true);
    connect(resizeTimer_, &QTimer::timeout, this, &ChatView::onResizeSettled);
}

// The document asks for images it does not know: we never load any by name.
// Images added with addResource (our own sisi:// names) are found before this is called.
QVariant ChatView::loadResource(int type, const QUrl &name) {
    if (type == QTextDocument::ImageResource) {
        // A non-null image stops Qt's own file/URL fallback; a missing image stays invisible.
        QImage blank(1, 1, QImage::Format_ARGB32);
        blank.fill(Qt::transparent);
        return QVariant::fromValue(blank);
    }
    return QTextBrowser::loadResource(type, name);
}

// When the window size has settled, draw again so the images fit the new width.
void ChatView::resizeEvent(QResizeEvent *event) {
    QTextBrowser::resizeEvent(event);
    resizeTimer_->start(200);
}

void ChatView::onResizeSettled() {
    if (renderedWidth_ != 0 && viewport()->width() != renderedWidth_) rebuild();
}

// ---------------------------------------------------------------------------
// Adding to the chat
// ---------------------------------------------------------------------------

void ChatView::addUser(const QString &text) {
    streaming_ = false;
    entries_.append(Entry{Kind::User, text, true});
    insertPlain(Kind::User, text);
    scrollDown();
}

void ChatView::addStatus(const QString &text) {
    streaming_ = false;              // a later token opens a new SISI block
    entries_.append(Entry{Kind::Status, text, true});
    insertPlain(Kind::Status, text);
    scrollDown();
}

void ChatView::addToken(const QString &delta) {
    if (delta.isEmpty()) return;
    if (!streaming_) {
        entries_.append(Entry{Kind::Sisi, QString(), false});
        insertPlain(Kind::Sisi, QString());      // just the "SISI: " label
        streaming_ = true;
    }
    entries_.last().text += delta;
    QTextCursor c(document());
    c.movePosition(QTextCursor::End);
    c.insertText(delta, QTextCharFormat());      // plain, not the bold label style
    scrollDown();
}

void ChatView::addAnswer(const QString &text) {
    streaming_ = false;
    entries_.append(Entry{Kind::Sisi, text, true});
    insertMarkdown(text);
    scrollDown();
}

// The streamed answers are complete now: draw them again as Markdown.
void ChatView::finishAnswers() {
    streaming_ = false;
    bool any = false;
    for (Entry &e : entries_) {
        if (e.kind == Kind::Sisi && !e.done) { e.done = true; any = true; }
    }
    if (any) rebuild();
    const QStringList notes = pendingNotes_;     // held back while the answer was streaming
    pendingNotes_.clear();
    for (const QString &n : notes) addStatus(n);
}

bool ChatView::answering() const {
    for (const Entry &e : entries_)
        if (e.kind == Kind::Sisi && !e.done) return true;
    return false;
}

// A status line that must not cut into a streaming answer waits until the answer is finished.
void ChatView::note(const QString &text) {
    if (answering()) pendingNotes_.append(text);
    else addStatus(text);
}

void ChatView::clearChat() {
    streaming_ = false;
    entries_.clear();
    pendingNotes_.clear();
    clear();
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void ChatView::insertPlain(Kind kind, const QString &text) {
    QTextCursor c(document());
    c.movePosition(QTextCursor::End);
    if (!document()->isEmpty()) c.insertBlock();

    QTextCharFormat plain;                       // the text itself
    if (kind == Kind::Status) {
        plain.setForeground(Qt::gray);
    } else {
        QTextCharFormat label;                   // bold, coloured "YOU: " / "SISI: "
        label.setFontWeight(QFont::Bold);
        label.setForeground(kind == Kind::User ? Qt::darkBlue : Qt::darkGreen);
        c.insertText(kind == Kind::User ? QStringLiteral("YOU: ") : QStringLiteral("SISI: "), label);
    }
    c.insertText(text, plain);
}

// Renders Markdown into a scratch document and copies it to the end of the chat.
void ChatView::insertMarkdown(const QString &text) {
    QTextDocument scratch;
    scratch.setMarkdown(prepareMarkdown(text), QTextDocument::MarkdownNoHTML);
    insertPlain(Kind::Sisi, QString());          // the label line
    QTextCursor c(document());
    c.movePosition(QTextCursor::End);
    c.insertBlock();
    c.insertFragment(QTextDocumentFragment(&scratch));
}

// Redraws everything from entries_ (used when an answer finishes or a diagram arrives).
void ChatView::rebuild() {
    QScrollBar *bar = verticalScrollBar();
    const int old = bar->value();
    const bool atBottom = old >= bar->maximum() - 4;
    clear();
    for (const Entry &e : entries_) {
        if (e.kind == Kind::Sisi && e.done) insertMarkdown(e.text);
        else insertPlain(e.kind, e.text);
    }
    bar->setValue(atBottom ? bar->maximum() : old);
}

void ChatView::scrollDown() {
    QScrollBar *bar = verticalScrollBar();
    bar->setValue(bar->maximum());
}

// ---------------------------------------------------------------------------
// Images and diagrams
// ---------------------------------------------------------------------------

// Registers an image under a name the Markdown can point to, scaled down to the pane width.
void ChatView::addImageResource(const QString &name, const QImage &image) {
    renderedWidth_ = viewport()->width();
    const int maxWidth = qMax(120, renderedWidth_ - 40);
    const QImage fitted = image.width() > maxWidth
                              ? image.scaledToWidth(maxWidth, Qt::SmoothTransformation)
                              : image;
    document()->addResource(QTextDocument::ImageResource, QUrl(name), QVariant(fitted));
}

// The canonical path of `path` if it is a file inside the workspace, else an empty string.
// Symbolic links and ".." are resolved first, so they cannot lead out of the folder.
QString ChatView::insideWorkspace(const QString &path) const {
    const QString root = QFileInfo(workspace_).canonicalFilePath();
    const QString file = QFileInfo(path).canonicalFilePath();
    if (root.isEmpty() || file.isEmpty()) return QString();       // missing, or no workspace
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    const QString prefix = root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
    if (file.compare(root, cs) == 0 || file.startsWith(prefix, cs)) return file;
    return QString();
}

// Reads an image file; the result is kept until the file changes (time or size).
QImage ChatView::loadImage(const QString &file) {
    const QFileInfo info(file);
    const auto it = files_.constFind(file);
    if (it != files_.constEnd() && it->modified == info.lastModified() && it->size == info.size())
        return it->image;
    CachedImage c;
    c.image = QImage(file);
    c.modified = info.lastModified();
    c.size = info.size();
    files_.insert(file, c);
    return c.image;
}

// Images named in ![alt](path) become resources, but only files inside the workspace;
// anything else (absolute paths, "..", links out of the folder, http) is left as it is.
QString ChatView::rewriteImages(const QString &text) {
    static const QRegularExpression re(QStringLiteral(R"re(!\[([^\]]*)\]\(\s*<?([^)\s>]+)>?(\s+"[^"]*")?\s*\))re"));
    QString out;
    qsizetype last = 0;
    QRegularExpressionMatchIterator it = re.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const std::string path = m.captured(2).toStdString();
        if (!isLocalImagePath(path)) continue;
        const std::string rel = resolveImagePath(workspace_.toStdString(), path);   // lexical check
        if (rel.empty()) continue;
        const QString file = insideWorkspace(QString::fromStdString(rel));          // real path check
        if (file.isEmpty()) continue;
        const QImage img = loadImage(file);
        if (img.isNull()) continue;              // not an image: leave the Markdown as it is
        const QString name = QStringLiteral("sisi://img/") +
                             QString::fromLatin1(QCryptographicHash::hash(file.toUtf8(), QCryptographicHash::Sha1).toHex());
        addImageResource(name, img);
        out += text.mid(last, m.capturedStart() - last);
        out += QStringLiteral("![") + m.captured(1) + QStringLiteral("](") + name + QLatin1Char(')');
        last = m.capturedEnd();
    }
    return out + text.mid(last);
}

// Prepares an answer for setMarkdown: image paths become resource names and diagram
// blocks become images as soon as they are rendered (until then they stay code).
QString ChatView::prepareMarkdown(const QString &md) {
    QString out;
    const std::vector<MdSegment> segs = splitFences(md.toStdString());
    for (const MdSegment &s : segs) {
        const QString raw = QString::fromStdString(s.raw);
        if (!s.fence) { out += rewriteImages(raw); continue; }
        const QString lang = QString::fromStdString(s.lang);
        if (lang != QLatin1String("mermaid")) { out += raw; continue; }

        const QString body = QString::fromStdString(s.body);
        const QString key = QString::fromLatin1(
            QCryptographicHash::hash((lang + QLatin1Char('\0') + body).toUtf8(), QCryptographicHash::Sha1).toHex());
        const auto img = diagramImages_.constFind(key);
        if (img != diagramImages_.constEnd()) {
            const QString name = QStringLiteral("sisi://diagram/") + key;
            addImageResource(name, img.value());
            out += QStringLiteral("\n![") + lang + QStringLiteral("](") + name + QStringLiteral(")\n\n");
        } else {
            requestDiagram(key, body);
            out += raw;
        }
    }
    return out;
}

void ChatView::requestDiagram(const QString &key, const QString &body) {
    if (requested_.contains(key)) return;        // asked before: rendering, or failed and shown as code
    requested_.insert(key);
    QString why;
    if (!diagrams_->available(&why)) {           // later: we are in the middle of drawing the chat
        QTimer::singleShot(0, this, [this, why] { note(tr("[mermaid] %1").arg(why)); });
        return;
    }
    diagrams_->render(key, body);
}

void ChatView::onDiagramDone(const QString &key, const QImage &image, const QString &error) {
    if (image.isNull()) {                        // the code block stays; say why once
        note(tr("[diagram] could not render: %1").arg(error));
        return;
    }
    diagramImages_.insert(key, image);
    rebuild();
}
