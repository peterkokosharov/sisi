// Copyright (c) 2026 Peter Kokosharov <peter.kokosharov@gmail.com>
// LLM: Claude Sonnet 5.5 (claude-sonnet-5-5) - rex child process and JSONL protocol
// Licensed under MIT License.
//
// Runs  <python> -m runtime --model <model> --jsonl --local-tools  as a child
// process and speaks JSON lines with it (same protocol as Layka):
//   to rex:   {"type":"chat","text":...}  {"type":"reset"}  {"type":"stop"}
//   from rex: ready, token, tool_start, tool_end, final, error, reset_ok, model_ok, bye
// stdout carries the JSON lines; stderr is diagnostics (log + bash pane).
#ifndef SISI_REXBRIDGE_H
#define SISI_REXBRIDGE_H

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>

class RexBridge : public QObject {
    Q_OBJECT
public:
    explicit RexBridge(QObject *parent = nullptr);
    ~RexBridge() override;

    // Stops a running rex first. Returns false if the process could not be started
    // (the reason is reported through info()).
    bool start(const QString &endpoint, const QString &model, const QString &workspace);
    // Ends rex (asks politely, then kills). No signals are emitted.
    void stop();
    bool alive() const;
    bool send(const QJsonObject &obj);

signals:
    void info(const QString &line);          // one status/error line for the chat pane
    void token(const QString &delta);        // the next piece of the answer
    void reply(const QString &text);         // the final answer (may be empty)
    void toolLog(const QString &text);       // tool calls and rex diagnostics, for the bash pane
    void turnDone();                         // the current request ended (final, error, bye)
    void exited();                           // the rex process is gone

private slots:
    void onReadyRead();
    void onStderr();
    void onFinished(int code, QProcess::ExitStatus status);
    void onError(QProcess::ProcessError err);

private:
    void handleLine(const QString &line);
    void drainStdout(bool atEnd);

    QProcess *proc_ = nullptr;
    QByteArray buf_;          // stdout bytes not yet forming a full line
    QString lastStderr_;      // last non-empty stderr line, shown if rex exits with an error
};

#endif
