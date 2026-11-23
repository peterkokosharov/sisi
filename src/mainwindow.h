// Copyright (c) 2026 Peter Kokosharov <peter.kokosharov@gmail.com>
// LLM: Claude Sonnet 5.5 (claude-sonnet-5-5) - main window
// Licensed under MIT License.
#ifndef SISI_MAINWINDOW_H
#define SISI_MAINWINDOW_H

#include <QMainWindow>
#include <QString>

#include "core.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class QToolButton;
class QWidget;
class ChatView;
class RexBridge;
class PermissionDialog;

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
    void onToken(const QString &delta);
    void onReply(const QString &text);
    void onTurnDone();
    void onExited();
    void onPermission(const QString &id, const QString &tool, const QString &kind,
                      const QString &summary, const QString &detail);
    void addToolLog(const QString &text);
    void addLine(const QString &text);

private:
    void onModelsReply(QNetworkReply *reply);
    void setBusy(bool busy);
    void showNextPermission();
    void onPermissionDone(PermissionDialog *dlg);
    void answerPermission(PermDecision d);
    void cancelPermissions();

    RexBridge *bridge_;
    QNetworkAccessManager *nam_;
    QNetworkReply *modelsReply_ = nullptr;

    QLineEdit *endpoint_;
    QComboBox *model_;
    QLineEdit *workspace_;
    ChatView *chat_;
    QPlainTextEdit *prompt_;
    QPushButton *sendBtn_;
    QPushButton *stopBtn_;
    QToolButton *plusBtn_;
    QWidget *bashPane_;
    QPlainTextEdit *bash_;
    QLabel *status_;
    QTimer *stopTimer_;

    PermQueue perms_;                // requests waiting; the first one is on screen
    PermissionDialog *permDlg_ = nullptr;

    bool busy_ = false;
    bool streamedThisTurn_ = false;  // tokens were shown, so the final text must not be shown again
    QString curEndpoint_, curModel_, curWorkspace_;   // what the running rex was started with
};

#endif
