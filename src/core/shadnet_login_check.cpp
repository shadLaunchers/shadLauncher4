// SPDX-FileCopyrightText: Copyright 2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QTcpSocket>
#include <QTimer>

#include "core/shadnet_login_check.h"

namespace ShadNet {

namespace {

constexpr quint32 HeaderSize = 15;
constexpr quint32 MaxPacketSize = 0x800000; // 8 MiB
constexpr quint32 ProtocolVersion = 1;

enum class PacketType : quint8 {
    Request = 0,
    Reply = 1,
    Notification = 2,
    ServerInfo = 3,
};

enum class CommandType : quint16 {
    Login = 0,
    Terminate = 1,
};

// Subset of shadNet's ErrorType that a Login reply can carry.
enum class ErrorType : quint8 {
    NoError = 0,
    Malformed = 1,
    LoginError = 5,
    LoginAlreadyLoggedIn = 6,
    LoginInvalidUsername = 7,
    LoginInvalidPassword = 8,
    LoginInvalidToken = 9,
};

constexpr quint64 LoginPacketId = 1;

void AppendU16LE(QByteArray& out, quint16 v) {
    out.append(static_cast<char>(v & 0xFF));
    out.append(static_cast<char>((v >> 8) & 0xFF));
}

void AppendU32LE(QByteArray& out, quint32 v) {
    for (int i = 0; i < 4; ++i) {
        out.append(static_cast<char>((v >> (8 * i)) & 0xFF));
    }
}

void AppendU64LE(QByteArray& out, quint64 v) {
    for (int i = 0; i < 8; ++i) {
        out.append(static_cast<char>((v >> (8 * i)) & 0xFF));
    }
}

quint16 ReadU16LE(const char* p) {
    return static_cast<quint16>(static_cast<quint8>(p[0])) |
           (static_cast<quint16>(static_cast<quint8>(p[1])) << 8);
}

quint32 ReadU32LE(const char* p) {
    quint32 v = 0;
    for (int i = 0; i < 4; ++i) {
        v |= static_cast<quint32>(static_cast<quint8>(p[i])) << (8 * i);
    }
    return v;
}

quint64 ReadU64LE(const char* p) {
    quint64 v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<quint64>(static_cast<quint8>(p[i])) << (8 * i);
    }
    return v;
}

void PbAppendVarint(QByteArray& out, quint64 v) {
    do {
        quint8 byte = v & 0x7F;
        v >>= 7;
        if (v != 0) {
            byte |= 0x80;
        }
        out.append(static_cast<char>(byte));
    } while (v != 0);
}

void PbAppendString(QByteArray& out, quint32 field, const QString& value) {
    if (value.isEmpty()) {
        return;
    }
    const QByteArray utf8 = value.toUtf8();
    PbAppendVarint(out, (static_cast<quint64>(field) << 3) | 2);
    PbAppendVarint(out, static_cast<quint64>(utf8.size()));
    out.append(utf8);
}

bool PbReadVarint(const QByteArray& data, int& pos, quint64& out) {
    out = 0;
    int shift = 0;
    while (pos < data.size()) {
        const quint8 byte = static_cast<quint8>(data.at(pos++));
        if (shift < 64) {
            out |= static_cast<quint64>(byte & 0x7F) << shift;
        }
        if ((byte & 0x80) == 0) {
            return true;
        }
        shift += 7;
        if (shift > 70) {
            return false;
        }
    }
    return false;
}

quint64 PbExtractUserId(const QByteArray& msg) {
    int pos = 0;
    while (pos < msg.size()) {
        quint64 key = 0;
        if (!PbReadVarint(msg, pos, key)) {
            return 0;
        }
        const quint32 field = static_cast<quint32>(key >> 3);
        const quint32 wire = static_cast<quint32>(key & 0x07);
        switch (wire) {
        case 0: { // varint
            quint64 value = 0;
            if (!PbReadVarint(msg, pos, value)) {
                return 0;
            }
            if (field == 2) {
                return value;
            }
            break;
        }
        case 1: // 64-bit
            pos += 8;
            break;
        case 2: { // length-delimited
            quint64 len = 0;
            if (!PbReadVarint(msg, pos, len) || len > static_cast<quint64>(msg.size() - pos)) {
                return 0;
            }
            pos += static_cast<int>(len);
            break;
        }
        case 5: // 32-bit
            pos += 4;
            break;
        default: // groups and anything else: give up
            return 0;
        }
    }
    return 0;
}

QByteArray BuildPacket(CommandType command, quint64 packet_id, const QByteArray& payload) {
    QByteArray out;
    out.reserve(static_cast<int>(HeaderSize) + payload.size());
    out.append(static_cast<char>(static_cast<quint8>(PacketType::Request)));
    AppendU16LE(out, static_cast<quint16>(command));
    AppendU32LE(out, static_cast<quint32>(HeaderSize + payload.size()));
    AppendU64LE(out, packet_id);
    out.append(payload);
    return out;
}

// Splits "host:port" / "[v6]:port" / bare host. Returns false if unusable.
bool SplitServerAddress(const QString& server, QString& host, quint16& port) {
    QString value = server.trimmed();
    if (value.isEmpty()) {
        return false;
    }
    // Tolerate a scheme prefix in case someone pasted a URL.
    const int scheme = value.indexOf(QStringLiteral("://"));
    if (scheme >= 0) {
        value = value.mid(scheme + 3);
    }
    value = value.split(QLatin1Char('/')).first();
    if (value.isEmpty()) {
        return false;
    }

    port = LoginChecker::DefaultPort;
    if (value.startsWith(QLatin1Char('['))) { // [::1]:31313
        const int close = value.indexOf(QLatin1Char(']'));
        if (close < 0) {
            return false;
        }
        host = value.mid(1, close - 1);
        const QString rest = value.mid(close + 1);
        if (rest.startsWith(QLatin1Char(':'))) {
            bool ok = false;
            const uint parsed = rest.mid(1).toUInt(&ok);
            if (!ok || parsed == 0 || parsed > 65535) {
                return false;
            }
            port = static_cast<quint16>(parsed);
        }
    } else {
        const int colon = value.lastIndexOf(QLatin1Char(':'));
        // More than one colon and no brackets: treat it as a bare IPv6 literal.
        if (colon >= 0 && value.count(QLatin1Char(':')) == 1) {
            host = value.left(colon);
            bool ok = false;
            const uint parsed = value.mid(colon + 1).toUInt(&ok);
            if (!ok || parsed == 0 || parsed > 65535) {
                return false;
            }
            port = static_cast<quint16>(parsed);
        } else {
            host = value;
        }
    }
    return !host.isEmpty();
}

} // namespace

LoginChecker::LoginChecker(QObject* parent) : QObject(parent) {}

LoginChecker::~LoginChecker() = default;

void LoginChecker::Start(const QString& server, const QString& npid, const QString& password,
                         const QString& token, int timeout_ms) {
    m_npid = npid;
    m_password = password;
    m_token = token;

    QString host;
    quint16 port = DefaultPort;
    if (!SplitServerAddress(server, host, port)) {
        // Queued so callers always see Finished after Start() returns.
        QTimer::singleShot(0, this, [this] { Finish(LoginCheckStatus::BadAddress); });
        return;
    }

    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, &LoginChecker::OnConnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &LoginChecker::OnReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &LoginChecker::OnErrorOccurred);

    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, &LoginChecker::OnTimeout);
    m_timeout->start(timeout_ms);

    m_socket->connectToHost(host, port);
}

void LoginChecker::Cancel() {
    m_finished = true;
    if (m_timeout) {
        m_timeout->stop();
    }
    if (m_socket) {
        m_socket->abort();
    }
    deleteLater();
}

void LoginChecker::OnConnected() {
    // Nothing to do until the server's ServerInfo handshake arrives.
}

void LoginChecker::OnErrorOccurred() {
    if (m_finished) {
        return;
    }
    Finish(LoginCheckStatus::ConnectionFailed);
}

void LoginChecker::OnTimeout() {
    if (m_finished) {
        return;
    }
    Finish(LoginCheckStatus::Timeout);
}

void LoginChecker::OnReadyRead() {
    if (m_finished) {
        return;
    }
    m_buffer.append(m_socket->readAll());
    ProcessBuffer();
}

bool LoginChecker::ProcessBuffer() {
    while (!m_finished && m_buffer.size() >= static_cast<int>(HeaderSize)) {
        const char* head = m_buffer.constData();
        const auto type = static_cast<PacketType>(static_cast<quint8>(head[0]));
        const quint16 command = ReadU16LE(head + 1);
        const quint32 size = ReadU32LE(head + 3);
        const quint64 packet_id = ReadU64LE(head + 7);

        if (size < HeaderSize || size > MaxPacketSize) {
            Finish(LoginCheckStatus::Malformed);
            return false;
        }
        if (m_buffer.size() < static_cast<int>(size)) {
            return true; // wait for the rest
        }

        const QByteArray payload =
            m_buffer.mid(static_cast<int>(HeaderSize), static_cast<int>(size - HeaderSize));
        m_buffer.remove(0, static_cast<int>(size));

        if (type == PacketType::ServerInfo) {
            HandleServerInfo(payload);
        } else if (type == PacketType::Reply &&
                   command == static_cast<quint16>(CommandType::Login) &&
                   packet_id == LoginPacketId) {
            HandleLoginReply(payload);
        }
    }
    return !m_finished;
}

void LoginChecker::HandleServerInfo(const QByteArray& payload) {
    if (m_handshake_done) {
        return;
    }
    if (payload.size() < 4) {
        Finish(LoginCheckStatus::Malformed);
        return;
    }
    if (ReadU32LE(payload.constData()) != ProtocolVersion) {
        Finish(LoginCheckStatus::ProtocolMismatch);
        return;
    }
    m_handshake_done = true;
    SendLogin();
}

void LoginChecker::SendLogin() {
    // LoginRequest { string npid = 1; string password = 2; string token = 3; }
    QByteArray message;
    PbAppendString(message, 1, m_npid);
    PbAppendString(message, 2, m_password);
    PbAppendString(message, 3, m_token);

    QByteArray blob;
    AppendU32LE(blob, static_cast<quint32>(message.size()));
    blob.append(message);

    m_socket->write(BuildPacket(CommandType::Login, LoginPacketId, blob));
    m_socket->flush();
}

void LoginChecker::HandleLoginReply(const QByteArray& payload) {
    if (payload.isEmpty()) {
        Finish(LoginCheckStatus::Malformed);
        return;
    }

    const quint8 error = static_cast<quint8>(payload.at(0));
    quint64 user_id = 0;
    if (static_cast<ErrorType>(error) == ErrorType::NoError && payload.size() >= 5) {
        const quint32 blob_size = ReadU32LE(payload.constData() + 1);
        if (blob_size <= static_cast<quint32>(payload.size() - 5)) {
            user_id = PbExtractUserId(payload.mid(5, static_cast<int>(blob_size)));
        }
    }

    LoginCheckStatus status;
    switch (static_cast<ErrorType>(error)) {
    case ErrorType::NoError:
        status = LoginCheckStatus::Ok;
        break;
    case ErrorType::LoginAlreadyLoggedIn:
        status = LoginCheckStatus::AlreadyLoggedIn;
        break;
    case ErrorType::LoginInvalidUsername:
    case ErrorType::LoginInvalidPassword:
        status = LoginCheckStatus::InvalidCredentials;
        break;
    case ErrorType::LoginInvalidToken:
        status = LoginCheckStatus::InvalidToken;
        break;
    case ErrorType::LoginError:
        status = LoginCheckStatus::Banned;
        break;
    case ErrorType::Malformed:
        status = LoginCheckStatus::Malformed;
        break;
    default:
        status = LoginCheckStatus::ServerError;
        break;
    }

    Finish(status, error, user_id);
}

void LoginChecker::Finish(LoginCheckStatus status, quint8 server_error, quint64 user_id) {
    if (m_finished) {
        return;
    }
    m_finished = true;
    if (m_timeout) {
        m_timeout->stop();
    }

    LoginCheckResult result;
    result.status = status;
    result.server_error = server_error;
    result.user_id = user_id;

    switch (status) {
    case LoginCheckStatus::Ok:
        result.message = tr("Login successful.");
        break;
    case LoginCheckStatus::AlreadyLoggedIn:
        result.message = tr("Credentials are valid, but this account is already signed in "
                            "somewhere else.");
        break;
    case LoginCheckStatus::InvalidCredentials:
        result.message = tr("Invalid account ID or password.");
        break;
    case LoginCheckStatus::InvalidToken:
        result.message = tr("This server requires an e-mail validation token for this account.");
        break;
    case LoginCheckStatus::Banned:
        result.message = tr("This account is banned on this server.");
        break;
    case LoginCheckStatus::BadAddress:
        result.message = tr("The ShadNet server address is not valid. Check it in Settings.");
        break;
    case LoginCheckStatus::ConnectionFailed:
        result.message =
            m_socket && !m_socket->errorString().isEmpty()
                ? tr("Could not reach the ShadNet server: %1").arg(m_socket->errorString())
                : tr("Could not reach the ShadNet server.");
        break;
    case LoginCheckStatus::ProtocolMismatch:
        result.message = tr("The server speaks a different protocol version than this launcher.");
        break;
    case LoginCheckStatus::Malformed:
        result.message = tr("The server sent an unexpected response.");
        break;
    case LoginCheckStatus::Timeout:
        result.message = tr("The ShadNet server did not respond in time.");
        break;
    case LoginCheckStatus::ServerError:
        result.message = tr("The server rejected the login (error code %1).").arg(server_error);
        break;
    }

    if (m_socket) {
        if (m_socket->state() == QAbstractSocket::ConnectedState) {
            if (status == LoginCheckStatus::Ok) {
                m_socket->write(BuildPacket(CommandType::Terminate, LoginPacketId + 1, {}));
                m_socket->flush();
            }
            m_socket->disconnectFromHost();
        } else {
            m_socket->abort();
        }
    }

    Q_EMIT Finished(result);
    deleteLater();
}

} // namespace ShadNet
