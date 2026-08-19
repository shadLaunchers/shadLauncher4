// SPDX-FileCopyrightText: Copyright 2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

class QTcpSocket;
class QTimer;

namespace ShadNet {

// Result of a credential check against a shadNet server.
enum class LoginCheckStatus {
    Ok,                 // credentials accepted
    AlreadyLoggedIn,    // credentials accepted, but a session is already open
    InvalidCredentials, // NPID unknown or password wrong
    InvalidToken,       // server requires e-mail validation, token missing/wrong
    Banned,             // account exists but is banned
    ServerError,        // server replied with some other error code
    BadAddress,         // shadnet_server setting could not be parsed
    ConnectionFailed,   // could not reach the server
    ProtocolMismatch,   // handshake version is not the one we speak
    Malformed,          // server sent something we could not parse
    Timeout,            // no reply within the deadline
};

struct LoginCheckResult {
    LoginCheckStatus status = LoginCheckStatus::ConnectionFailed;
    quint8 server_error = 0; // raw ErrorType byte, when the server replied
    quint64 user_id = 0;     // account id, on success
    QString message;         // human readable, already translated

    bool credentials_valid() const {
        return status == LoginCheckStatus::Ok || status == LoginCheckStatus::AlreadyLoggedIn;
    }
};

// Performs a single shadNet Login round-trip and reports whether the
// credentials are accepted, then closes the connection again.
class LoginChecker : public QObject {
    Q_OBJECT

public:
    explicit LoginChecker(QObject* parent = nullptr);
    ~LoginChecker() override;

    // `server` is the "host:port" string from the ShadNet server setting
    // the port may be omitted, in which case DefaultPort is used.
    void Start(const QString& server, const QString& npid, const QString& password,
               const QString& token = QString(), int timeout_ms = 10000);

    // Stops the check without emitting Finished and schedules deletion.
    void Cancel();

    static constexpr quint16 DefaultPort = 31313;

Q_SIGNALS:
    void Finished(const ShadNet::LoginCheckResult& result);

private:
    void OnConnected();
    void OnReadyRead();
    void OnErrorOccurred();
    void OnTimeout();

    // Consumes complete packets from m_buffer. Returns false when the
    // check is over (Finish() was called).
    bool ProcessBuffer();
    void HandleServerInfo(const QByteArray& payload);
    void HandleLoginReply(const QByteArray& payload);

    void SendLogin();
    void Finish(LoginCheckStatus status, quint8 server_error = 0, quint64 user_id = 0);

    QTcpSocket* m_socket = nullptr;
    QTimer* m_timeout = nullptr;
    QByteArray m_buffer;

    QString m_npid;
    QString m_password;
    QString m_token;

    bool m_handshake_done = false;
    bool m_finished = false;
};

} // namespace ShadNet

Q_DECLARE_METATYPE(ShadNet::LoginCheckResult)
