// Copyright (c) 2026 Peter Kokosharov <peter.kokosharov@gmail.com>
// LLM: Claude Sonnet 5.5 (claude-sonnet-5-5) - main window
// Licensed under MIT License.
#include "mainwindow.h"

#include <QAction>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QStatusBar>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "config.h"
#include "rexbridge.h"

namespace {
const int kStopGraceMs = 5000;      // how long rex gets to react to "stop" before it is restarted
const int kBashMaxLines = 2000;
}

MainWindow::MainWindow()
    : bridge_(new RexBridge(this)), nam_(new QNetworkAccessManager(this)),
      stopTimer_(new QTimer(this)) {
    setWindowTitle(QStringLiteral("Sisi Valkodav"));
    resize(900, 640);

    // --- connection settings -------------------------------------------------
    endpoint_ = new QLineEdit(QString::fromStdString(g_cfg.endpoint));
    model_ = new QComboBox;
    model_->setEditable(true);
    model_->setInsertPolicy(QComboBox::NoInsert);
    model_->addItem(QString::fromStdString(g_cfg.model));
    workspace_ = new QLineEdit(QString::fromStdString(g_cfg.workspace));
    auto *browse = new QPushButton(tr("Folder..."));

    auto *settings = new QGridLayout;
    settings->addWidget(new QLabel(tr("Endpoint")), 0, 0);
    settings->addWidget(endpoint_, 0, 1, 1, 2);
    settings->addWidget(new QLabel(tr("Model")), 1, 0);
    settings->addWidget(model_, 1, 1, 1, 2);
    settings->addWidget(new QLabel(tr("Workspace")), 2, 0);
    settings->addWidget(workspace_, 2, 1);
    settings->addWidget(browse, 2, 2);
    settings->setColumnStretch(1, 1);

    // --- chat and bash pane --------------------------------------------------
    chat_ = new QTextEdit;
    chat_->setReadOnly(true);

    bash_ = new QPlainTextEdit;
    bash_->setReadOnly(true);
    bash_->setLineWrapMode(QPlainTextEdit::NoWrap);
    bash_->setMaximumBlockCount(kBashMaxLines);
    bash_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    bashPane_ = new QWidget;
    auto *bashLayout = new QVBoxLayout(bashPane_);
    bashLayout->setContentsMargins(0, 0, 0, 0);
    bashLayout->addWidget(new QLabel(tr("bash")));
    bashLayout->addWidget(bash_);
    bashPane_->hide();

    plusBtn_ = new QToolButton;
    plusBtn_->setText(QStringLiteral("+"));
    plusBtn_->setToolTip(tr("Show the bash pane (rex tool calls)"));

    auto *panes = new QHBoxLayout;
    panes->addWidget(chat_, 3);
    panes->addWidget(bashPane_, 2);
    panes->addWidget(plusBtn_, 0, Qt::AlignTop);

    // --- prompt and buttons --------------------------------------------------
    prompt_ = new QPlainTextEdit;
    prompt_->setPlaceholderText(tr("Ask Sisi... (Ctrl+Enter sends)"));
    prompt_->setFixedHeight(80);
    prompt_->installEventFilter(this);

    sendBtn_ = new QPushButton(tr("Send"));
    stopBtn_ = new QPushButton(tr("Stop"));
    stopBtn_->setEnabled(false);
    auto *quitBtn = new QPushButton(tr("Quit"));
    auto *buttons = new QVBoxLayout;
    buttons->addWidget(sendBtn_);
    buttons->addWidget(stopBtn_);
    buttons->addWidget(quitBtn);

    auto *bottom = new QHBoxLayout;
    bottom->addWidget(prompt_, 1);
    bottom->addLayout(buttons);

    auto *central = new QWidget;
    auto *root = new QVBoxLayout(central);
    root->addLayout(settings);
    root->addLayout(panes, 1);
    root->addLayout(bottom);
    setCentralWidget(central);

    // --- menu and status line ------------------------------------------------
    QAction *reset = menuBar()->addMenu(tr("&Sisi"))->addAction(tr("&Reset conversation"));
    status_ = new QLabel;
    statusBar()->addWidget(status_);
    setBusy(false);

    // --- wiring ----------------------------------------------------------------
    connect(sendBtn_, &QPushButton::clicked, this, &MainWindow::sendPrompt);
    connect(stopBtn_, &QPushButton::clicked, this, &MainWindow::stopTurn);
    connect(quitBtn, &QPushButton::clicked, this, &QWidget::close);
    connect(browse, &QPushButton::clicked, this, &MainWindow::pickFolder);
    connect(plusBtn_, &QToolButton::clicked, this, &MainWindow::toggleBashPane);
    connect(reset, &QAction::triggered, this, &MainWindow::resetConversation);
    connect(endpoint_, &QLineEdit::editingFinished, this, &MainWindow::refreshModels);

    stopTimer_->setSingleShot(true);
    connect(stopTimer_, &QTimer::timeout, this, &MainWindow::stopTimedOut);

    connect(bridge_, &RexBridge::info, this, &MainWindow::addLine);
    connect(bridge_, &RexBridge::stream, this, &MainWindow::onStream);
    connect(bridge_, &RexBridge::reply, this, &MainWindow::onReply);
    connect(bridge_, &RexBridge::toolLog, this, &MainWindow::addToolLog);
    connect(bridge_, &RexBridge::turnDone, this, &MainWindow::onTurnDone);
    connect(bridge_, &RexBridge::exited, this, &MainWindow::onExited);

    refreshModels();
    prompt_->setFocus();
}

// ---------------------------------------------------------------------------
// Sending, stopping, restarting
// ---------------------------------------------------------------------------

void MainWindow::sendPrompt() {
    const QString text = prompt_->toPlainText().trimmed();
    if (busy_ || text.isEmpty()) return;

    const QString endpoint = endpoint_->text().trimmed();
    const QString model = model_->currentText().trimmed();
    const QString workspace = workspace_->text().trimmed();

    // (Re)start rex when it is not running or when its settings changed.
    const bool changed = endpoint != curEndpoint_ || model != curModel_ || workspace != curWorkspace_;
    if (!bridge_->alive() || changed) {
        if (!curModel_.isEmpty() && changed && bridge_->alive())
            addLine(tr("[settings changed, restarting rex; the conversation starts fresh]"));
        curEndpoint_ = endpoint;
        curModel_ = model;
        curWorkspace_ = workspace;
        if (!bridge_->start(endpoint, model, workspace)) {
            curModel_.clear();                 // forces a new start next time
            return;                            // the reason was reported by the bridge
        }
    }

    QJsonObject msg;
    msg[QStringLiteral("type")] = QStringLiteral("chat");
    msg[QStringLiteral("text")] = text;
    if (!bridge_->send(msg)) {
        addLine(tr("[error] could not write to rex"));
        return;
    }
    streaming_ = false;                        // a new turn starts a new SISI block
    appendChat(QStringLiteral("YOU: "), text, Qt::darkBlue);
    prompt_->clear();
    setBusy(true);
}

void MainWindow::stopTurn() {
    if (!busy_) return;
    QJsonObject msg;
    msg[QStringLiteral("type")] = QStringLiteral("stop");
    bridge_->send(msg);
    addLine(tr("[stop sent]"));
    stopBtn_->setEnabled(false);
    stopTimer_->start(kStopGraceMs);           // if rex ignores it, kill and restart
}

void MainWindow::stopTimedOut() {
    if (!busy_) return;
    addLine(tr("[rex did not react to stop, so it was killed; it restarts on the next message, "
               "the conversation starts fresh]"));
    bridge_->stop();
    curModel_.clear();
    onTurnDone();
}

void MainWindow::resetConversation() {
    if (busy_) return;
    chat_->clear();
    streaming_ = false;
    QJsonObject msg;
    msg[QStringLiteral("type")] = QStringLiteral("reset");
    if (!bridge_->send(msg)) addLine(tr("[conversation cleared]"));   // rex is not running
}

void MainWindow::pickFolder() {
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Workspace"), workspace_->text());
    if (!dir.isEmpty()) workspace_->setText(dir);
}

void MainWindow::toggleBashPane() {
    const bool show = !bashPane_->isVisible();
    bashPane_->setVisible(show);
    plusBtn_->setText(show ? QStringLiteral("-") : QStringLiteral("+"));
    plusBtn_->setToolTip(show ? tr("Hide the bash pane") : tr("Show the bash pane (rex tool calls)"));
}

// ---------------------------------------------------------------------------
// Models list: GET <endpoint>/v1/models
// ---------------------------------------------------------------------------

void MainWindow::refreshModels() {
    if (modelsReply_) {                        // a newer request replaces the old one
        modelsReply_->disconnect(this);
        modelsReply_->abort();
        modelsReply_->deleteLater();
        modelsReply_ = nullptr;
    }
    QString base = endpoint_->text().trimmed();
    while (base.endsWith(QLatin1Char('/'))) base.chop(1);
    const QUrl url(base + QStringLiteral("/v1/models"));
    if (base.isEmpty() || !url.isValid()) return;

    QNetworkRequest req(url);
    req.setTransferTimeout(5000);
    modelsReply_ = nam_->get(req);
    connect(modelsReply_, &QNetworkReply::finished, this,
            [this, reply = modelsReply_] { onModelsReply(reply); });
}

void MainWindow::onModelsReply(QNetworkReply *reply) {
    reply->deleteLater();
    if (reply != modelsReply_) return;         // superseded
    modelsReply_ = nullptr;
    if (reply->error() != QNetworkReply::NoError) {
        addLine(tr("[models] cannot read the list: %1").arg(reply->errorString()));
        return;
    }
    const std::vector<std::string> ids = parseModelIds(reply->readAll().toStdString());
    if (ids.empty()) {
        addLine(tr("[models] the endpoint returned no model ids"));
        return;
    }
    const QString keep = model_->currentText();   // what the user has typed or picked stays
    model_->clear();
    for (const std::string &id : ids) model_->addItem(QString::fromStdString(id));
    model_->setCurrentText(keep);
}

// ---------------------------------------------------------------------------
// Events from rex
// ---------------------------------------------------------------------------

void MainWindow::onStream(const QString &textSoFar) {
    if (!streaming_) {
        appendChat(QStringLiteral("SISI: "), QString(), Qt::darkGreen);
        streamStart_ = chat_->document()->characterCount() - 1;
        streaming_ = true;
    }
    QTextCursor c(chat_->document());
    c.setPosition(streamStart_);
    c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    c.insertText(textSoFar, QTextCharFormat());   // plain, not the bold label style
    scrollChatDown();
}

void MainWindow::onReply(const QString &text) {
    if (streaming_) onStream(text);            // replace the streamed text by the final one
    else appendChat(QStringLiteral("SISI: "), text, Qt::darkGreen);
    streaming_ = false;
}

void MainWindow::onTurnDone() {
    stopTimer_->stop();
    streaming_ = false;
    setBusy(false);
}

void MainWindow::onExited() {
    curModel_.clear();                         // next Send starts rex again
    streaming_ = false;
    setBusy(false);
}

void MainWindow::addToolLog(const QString &text) {
    bash_->appendPlainText(text);
}

// A status or error line in the chat pane.
void MainWindow::addLine(const QString &text) {
    streaming_ = false;
    appendChat(QString(), text, Qt::gray);
}

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

void MainWindow::appendChat(const QString &label, const QString &text, const QColor &color) {
    QTextCursor c(chat_->document());
    c.movePosition(QTextCursor::End);
    if (!chat_->document()->isEmpty()) c.insertBlock();
    QTextCharFormat bold;
    bold.setFontWeight(QFont::Bold);
    bold.setForeground(color);
    QTextCharFormat plain;
    if (color == Qt::gray) plain.setForeground(color);
    if (!label.isEmpty()) c.insertText(label, bold);
    c.insertText(text, plain);
    scrollChatDown();
}

void MainWindow::scrollChatDown() {
    QScrollBar *bar = chat_->verticalScrollBar();
    bar->setValue(bar->maximum());
}

void MainWindow::setBusy(bool busy) {
    busy_ = busy;
    sendBtn_->setEnabled(!busy);
    stopBtn_->setEnabled(busy);
    status_->setText(busy ? tr("Sisi is chewing...") : tr("Woof."));
}

// Ctrl+Enter in the prompt box sends.
bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    if (obj == prompt_ && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
            (key->modifiers() & Qt::ControlModifier)) {
            sendPrompt();
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    bridge_->stop();
    QMainWindow::closeEvent(event);
}
