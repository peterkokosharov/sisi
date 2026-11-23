// Copyright (c) 2026 Peter Kokosharov <peter.kokosharov@gmail.com>
// LLM: Claude Sonnet 5.5 (claude-sonnet-5-5) - permission dialog
// Licensed under MIT License.
#include "permissiondialog.h"

#include <QColor>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVBoxLayout>

namespace {

// Colours the lines of a unified diff: + green, - red, @@ blue.
class DiffHighlighter : public QSyntaxHighlighter {
public:
    explicit DiffHighlighter(QTextDocument *doc) : QSyntaxHighlighter(doc) {}

protected:
    void highlightBlock(const QString &text) override {
        const char c = classifyDiffLine(text.toStdString());
        QTextCharFormat f;
        if (c == '+') f.setForeground(QColor(0, 150, 0));
        else if (c == '-') f.setForeground(QColor(200, 0, 0));
        else if (c == '@') f.setForeground(QColor(30, 100, 220));
        else if (c == 'h') f.setFontWeight(QFont::Bold);
        else return;
        setFormat(0, text.size(), f);
    }
};

}  // namespace

PermissionDialog::PermissionDialog(const PermissionReq &req, QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Permission requested"));
    setModal(true);
    resize(720, 460);

    const QString tool = QString::fromStdString(req.tool);
    const QString kind = QString::fromStdString(req.kind);
    QString title = QStringLiteral("rex wants to use <b>") + tool.toHtmlEscaped() + QStringLiteral("</b>");
    if (!kind.isEmpty()) title += QStringLiteral(" (") + kind.toHtmlEscaped() + QStringLiteral(")");
    auto *head = new QLabel(title);
    head->setTextFormat(Qt::RichText);
    auto *summary = new QLabel(QString::fromStdString(req.summary));
    summary->setTextFormat(Qt::PlainText);
    summary->setWordWrap(true);

    auto *detail = new QPlainTextEdit;
    detail->setReadOnly(true);
    detail->setLineWrapMode(QPlainTextEdit::NoWrap);
    detail->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    detail->setPlainText(QString::fromStdString(req.detail));
    if (looksLikeDiff(req.detail)) new DiffHighlighter(detail->document());

    auto *once = new QPushButton(tr("Allow once"));
    auto *always = new QPushButton(tr("Allow always"));
    auto *deny = new QPushButton(tr("Deny"));
    once->setAutoDefault(false);
    always->setAutoDefault(false);
    deny->setAutoDefault(true);
    deny->setDefault(true);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(once);
    buttons->addWidget(always);
    buttons->addWidget(deny);

    auto *root = new QVBoxLayout(this);
    root->addWidget(head);
    root->addWidget(summary);
    root->addWidget(detail, 1);
    root->addLayout(buttons);

    connect(once, &QPushButton::clicked, this, [this] {
        decision_ = PermDecision::AllowOnce;
        accept();
    });
    connect(always, &QPushButton::clicked, this, [this] {
        decision_ = PermDecision::AllowAlways;
        accept();
    });
    connect(deny, &QPushButton::clicked, this, &QDialog::reject);   // Esc and the window X also reject
    deny->setFocus();
}

void PermissionDialog::forceDeny() {
    decision_ = PermDecision::Deny;
    reject();
}
