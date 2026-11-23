// Copyright (c) 2026 Peter Kokosharov <peter.kokosharov@gmail.com>
// LLM: Claude Sonnet 5.5 (claude-sonnet-5-5) - main window
// Licensed under MIT License.
#ifndef SISI_MAINWINDOW_H
#define SISI_MAINWINDOW_H

#include <QMainWindow>
#include <QString>

class QColor;
class QComboBox;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QPlainTextEdit;
class QPushButton;
class QTextEdit;
class QTimer;
class QToolButton;
class QWidget;
class RexBridge;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void sendPrompt();
    void stopTurn();
    void stopTimedOut();
    void resetConversation();
    void pickFolder();
    void refreshModels();
    void toggleBashPane();
    void onStream(const QString &textSoFar);
    void onReply(const QString &text);
    void onTurnDone();
    void onExited();
    void addToolLog(const QString &text);
    void addLine(const QString &text);

private:
    void onModelsReply(QNetworkReply *reply);
    void appendChat(const QString &label, const QString &text, const QColor &color);
    void setBusy(bool busy);
    void scrollChatDown();

    RexBridge *bridge_;
    QNetworkAccessManager *nam_;
    QNetworkReply *modelsReply_ = nullptr;

    QLineEdit *endpoint_;
    QComboBox *model_;
    QLineEdit *workspace_;
    QTextEdit *chat_;
    QPlainTextEdit *prompt_;
    QPushButton *sendBtn_;
    QPushButton *stopBtn_;
    QToolButton *plusBtn_;
    QWidget *bashPane_;
    QPlainTextEdit *bash_;
    QLabel *status_;
    QTimer *stopTimer_;

    bool busy_ = false;
    bool streaming_ = false;     // the last chat block is a SISI answer that is still growing
    int streamStart_ = 0;        // document position where that block starts
    QString curEndpoint_, curModel_, curWorkspace_;   // what the running rex was started with
};

#endif
