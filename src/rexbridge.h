// Copyright (c) 2026 Peter Kokosharov <peter.kokosharov@gmail.com>
// LLM: Claude Sonnet 5.5 (claude-sonnet-5-5) - rex child process and JSONL protocol
// Licensed under MIT License.
//
// Runs  <python> -m runtime --model <model> --jsonl --local-tools  as a child
// process and speaks JSON lines with it (same protocol as Layka):
//   to rex:   {"type":"chat","text":...}  {"type":"reset"}  {"type":"stop"}
//   from rex: ready, token, tool_start, tool_end, final, error, reset_ok, model_ok, bye
// (a "tool_output" event is shown too if a newer rex sends it).
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
    // Terminates rex (kills it if it does not leave in time). No signals are emitted.
    void stop();
    bool alive() const;
    bool send(const QJsonObject &obj);

signals:
    void info(const QString &line);          // one status/error line for the chat pane
    void stream(const QString &textSoFar);   // the answer so far (streaming tokens)
    void reply(const QString &text);         // the final answer
    void toolLog(const QString &text);       // tool calls and their output, for the bash pane
    void turnDone();                         // the current request ended (final, error, bye)
    void exited();                           // the rex process is gone

private slots:
    void onReadyRead();
    void onFinished(int code, QProcess::ExitStatus status);
    void onError(QProcess::ProcessError err);

private:
    void handleLine(const QString &line);

    QProcess *proc_ = nullptr;
    QByteArray buf_;
    QString assistant_;
};

#endif
