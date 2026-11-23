// Copyright (c) 2026 Peter Kokosharov <peter.kokosharov@gmail.com>
// LLM: Claude Sonnet 5.5 (claude-sonnet-5-5) - chat pane with Markdown, images and diagrams
// Licensed under MIT License.
#include "chatview.h"

#include <QCryptographicHash>
#include <QRegularExpression>
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

ChatView::ChatView(QWidget *parent) : QTextBrowser(parent), diagrams_(new DiagramRenderer(this)) {
    setReadOnly(true);
    setOpenExternalLinks(true);     // http(s) and mailto links go to the system browser
    setOpenLinks(false);            // everything else must not replace the chat with a page
    connect(diagrams_, &DiagramRenderer::done, this, &ChatView::onDiagramDone);
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
}

void ChatView::clearChat() {
    streaming_ = false;
    entries_.clear();
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
    scratch.setMarkdown(prepareMarkdown(text));
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
    const int maxWidth = qMax(120, viewport()->width() - 40);
    const QImage fitted = image.width() > maxWidth
                              ? image.scaledToWidth(maxWidth, Qt::SmoothTransformation)
                              : image;
    document()->addResource(QTextDocument::ImageResource, QUrl(name), QVariant(fitted));
}

// Local image files named in ![alt](path) become resources; other images are left alone.
QString ChatView::rewriteImages(const QString &text) {
    static const QRegularExpression re(QStringLiteral(R"re(!\[([^\]]*)\]\(\s*<?([^)\s>]+)>?(\s+"[^"]*")?\s*\))re"));
    QString out;
    qsizetype last = 0;
    QRegularExpressionMatchIterator it = re.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const std::string path = m.captured(2).toStdString();
        if (!isLocalImagePath(path)) continue;
        const QString file = QString::fromStdString(resolveImagePath(workspace_.toStdString(), path));
        if (!files_.contains(file)) files_.insert(file, QImage(file));
        const QImage &img = files_[file];
        if (img.isNull()) continue;              // missing file: leave the Markdown as it is
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
        if (lang != QLatin1String("mermaid") && lang != QLatin1String("plantuml")) { out += raw; continue; }

        const QString body = QString::fromStdString(s.body);
        const QString key = QString::fromLatin1(
            QCryptographicHash::hash((lang + QLatin1Char('\0') + body).toUtf8(), QCryptographicHash::Sha1).toHex());
        const auto img = diagramImages_.constFind(key);
        if (img != diagramImages_.constEnd()) {
            const QString name = QStringLiteral("sisi://diagram/") + key;
            addImageResource(name, img.value());
            out += QStringLiteral("\n![") + lang + QStringLiteral("](") + name + QStringLiteral(")\n\n");
        } else {
            requestDiagram(key, lang, body);
            out += raw;
        }
    }
    return out;
}

void ChatView::requestDiagram(const QString &key, const QString &lang, const QString &body) {
    if (requested_.contains(key)) return;        // asked before: rendering, or failed and shown as code
    requested_.insert(key);
    QString why;
    if (!diagrams_->available(lang, &why)) {
        if (!why.isEmpty())                      // later: we are in the middle of drawing the chat
            QTimer::singleShot(0, this, [this, lang, why] { addStatus(tr("[%1] %2").arg(lang, why)); });
        return;
    }
    diagrams_->render(key, lang, body);
}

void ChatView::onDiagramDone(const QString &key, const QImage &image, const QString &error) {
    if (image.isNull()) {                        // the code block stays; say why once
        addStatus(tr("[diagram] could not render: %1").arg(error));
        return;
    }
    diagramImages_.insert(key, image);
    rebuild();
}
