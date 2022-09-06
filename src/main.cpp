/*
 * This is a HTTP/HTTPS forward proxy server implementation
 * capable for running inside shared libraries (.dll/.so)
 * Copyright (C) 2022 Iman Ahmadvand
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * It is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
*/

#include <QtCore>
#include <QtNetwork>
#include <httpparser/request.h>
#include <httpparser/httprequestparser.h>

// key registry
static constexpr auto kAddress = "Address";
static constexpr auto kPort    = "Port";
static constexpr auto kConnect = "CONNECT";
static constexpr auto kGet     = "GET";
static constexpr auto kPut     = "PUT";
static constexpr auto kPost    = "POST";
static constexpr auto kHead    = "HEAD";
static constexpr auto kDelete  = "DELETE";

#ifdef QT_NO_DEBUG
void qMessageHandler(QtMsgType, const QMessageLogContext&, const QString&) {
    // bypass output in release-mode
}

#endif // QT_NO_DEBUG

///
/// \brief The Settings class
///
class Settings final : protected QSettings {
  public:
    explicit Settings(const QString& file);
    QVariant read(const QString& key, const QVariant& defaultValue);
};

///
/// \brief The ProxyConnection class
///
class ProxyConnection final : public QObject {
    Q_OBJECT

  public:
    ProxyConnection(const QSharedPointer<QTcpSocket>& downStream, int id);
    ~ProxyConnection() override;

  private Q_SLOTS:
    void terminate();
    void downStreamReadyRead();
    void upStreamReadyRead();

  private:
    int _id = 0;
    QSharedPointer<QTcpSocket> _downStream;
    QSharedPointer<QTcpSocket> _upStream;

  Q_SIGNALS:
    void terminated(int id, QPrivateSignal);
};

///
/// \brief The ProxyServer class
///
class ProxyServer final : public QTcpServer {
    Q_OBJECT

  public:
    explicit ProxyServer(QObject* parent = nullptr);

  protected:
    void        incomingConnection(qintptr handle) override;
    Q_SLOT void onConnectionTerminate(int id);

  private:
    QMap<int, QSharedPointer<ProxyConnection>> _connections;
};

void startServer(int argc, char* argv[]) {
    new QCoreApplication(argc, argv);

#ifdef QT_NO_DEBUG
    qInstallMessageHandler(qMessageHandler);
#endif // QT_NO_DEBUG
    QCoreApplication::setApplicationName(QStringLiteral("DllProxyServer"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    Settings settings(QStringLiteral("proxy-settings.ini"));
    ProxyServer server;

    const QHostAddress host(settings.read(kAddress, QHostAddress(QHostAddress::Any).toString()).toString());
    const auto port = settings.read(kPort, 8888).toInt();

    if (!server.listen(host, port)) {
        qWarning() << server.errorString();
        QTimer::singleShot(0, Qt::PreciseTimer, QCoreApplication::instance(), &QCoreApplication::quit);
    } else {
        qInfo() << QStringLiteral("Start listening on") << QStringLiteral("%1:%2").arg(host.toString()).arg(port);
    }
    QCoreApplication::exec();
}

#ifndef BUILD_AS_SHARED_LIB
int main(int argc, char* argv[]) {
    startServer(argc, argv);
    return EXIT_SUCCESS;
}

#else // ifndef BUILD_AS_SHARED_LIB
# ifdef Q_OS_LINUX
void __attribute__((constructor)) ctor() {
    int argc    = 0;
    char** argv = nullptr;

    startServer(argc, argv);
}

# elif defined(Q_OS_WIN)
# else // ifdef Q_OS_LINUX
#  error "Platform is not supported!"
# endif // Q_OS_LINUX
extern "C" Q_DECL_EXPORT void start() {
    int argc    = 0;
    char** argv = nullptr;

    startServer(argc, argv);
}

#endif // BUILD_AS_SHARED_LIB

#include "main.moc"

ProxyServer::ProxyServer(QObject* parent) : QTcpServer(parent) {
    QObject::connect(this, &ProxyServer::acceptError, [&](QAbstractSocket::SocketError err) {
        qWarning() << errorString();
    });
}

void ProxyServer::incomingConnection(qintptr handle) {
    const auto id = static_cast<int>(handle);

    if (auto socket = QSharedPointer<QTcpSocket>(new QTcpSocket, &QObject::deleteLater)) {
        if (socket->setSocketDescriptor(handle)) {
            auto connection = QSharedPointer<ProxyConnection>(new ProxyConnection(socket, id), &QObject::deleteLater);
            _connections.insert(id, connection);
            QObject::connect(connection.get(), &ProxyConnection::terminated, this, &ProxyServer::onConnectionTerminate);
        } else {
            qWarning() <<  QStringLiteral("Failed to set socket descriptor!") << socket->errorString();
        }
    }
    qInfo() << QStringLiteral("Active Connections: ") << _connections.size();
}

void ProxyServer::onConnectionTerminate(int id) {
    if (auto connection = _connections.value(id)) {
        connection->blockSignals(true);
        _connections.remove(id);
    }

    qInfo() << QStringLiteral("Active Connections: ") << _connections.size();
}

ProxyConnection::ProxyConnection(const QSharedPointer<QTcpSocket>& downStream, int id) : _downStream{downStream}, _id{id},
    _upStream{QSharedPointer<QTcpSocket>(new QTcpSocket, &QObject::deleteLater)} {
    QObject::connect(_downStream.get(), &QTcpSocket::readyRead,     this, &ProxyConnection::downStreamReadyRead);
    QObject::connect(_downStream.get(), &QTcpSocket::disconnected,  this, &ProxyConnection::terminate);
    QObject::connect(_downStream.get(), &QTcpSocket::errorOccurred, this, [this]() {
        qWarning() << QStringLiteral("DownStream error: ") << _downStream->errorString();
    });
    QObject::connect(_upStream.get(), &QTcpSocket::disconnected,  this, &ProxyConnection::terminate);
    QObject::connect(_upStream.get(), &QTcpSocket::readyRead,     this, &ProxyConnection::upStreamReadyRead);
    QObject::connect(_upStream.get(), &QTcpSocket::errorOccurred, this, [this]() {
        qWarning() << QStringLiteral("UpStream error: ") << _upStream->errorString();
    });
}

ProxyConnection::~ProxyConnection() = default;

void ProxyConnection::terminate() {
    _downStream->blockSignals(true);
    _upStream->blockSignals(true);
    _downStream->disconnectFromHost();
    _downStream->close();
    _upStream->disconnectFromHost();
    _upStream->close();

    Q_EMIT terminated(_id, QPrivateSignal{});
}

void ProxyConnection::downStreamReadyRead() {
    using namespace httpparser;

    const auto data = _downStream->readAll();

    if (QTcpSocket::ConnectedState != _upStream->state()) {
        Request request;
        HttpRequestParser parser;
        const auto& result = parser.parse(request, data.constData(), data.constData() + data.size());

        if (result == HttpRequestParser::ParsingCompleted) {
            if ((request.method == kConnect)
                    || (request.method == kGet)
                    || (request.method == kPut)
                    || (request.method == kPost)
                    || (request.method == kHead)
                    || (request.method == kDelete)) {
                const auto regex = QRegularExpression(QStringLiteral("^(.*):(\\d+)$"));
                const auto match = regex.match(request.uri.c_str());

                if (match.hasMatch()) {
                    auto host = match.captured(1);
                    auto port = match.captured(2).toInt();
                    QHostInfo::lookupHost(host, this, [ = ](const QHostInfo & info) {
                        const auto addresses = info.addresses();

                        if (!addresses.isEmpty()) {
                            _upStream->connectToHost(addresses.first(), port);
                            QObject::connect(_upStream.get(), &QTcpSocket::connected,
                            [ = ]() {
                                if (request.method == kConnect) {
                                    const auto response = QStringLiteral("HTTP/%1.%2 200 Connection established\r\nProxy-agent: %3/%4\r\n\r\n")
                                                          .arg(request.versionMajor)
                                                          .arg(request.versionMinor)
                                                          .arg(qApp->applicationName())
                                                          .arg(qApp->applicationVersion());
                                    _downStream->write(response.toLatin1());
                                    _downStream->flush();
                                }
                            });
                        } else {
                            qWarning() << QStringLiteral("HostLookup failed!");
                            terminate();
                        }
                    });
                } else {
                    qWarning() << QStringLiteral("Invaid URI found!");
                    terminate();
                }
            }
        } else {
            qWarning() << QStringLiteral("HttpRequest parse failed!") << data.constData();
            terminate();
        }
    } else {
        _upStream->write(data);
        _upStream->flush();
    }
}

void ProxyConnection::upStreamReadyRead() {
    _downStream->write(_upStream->readAll());
    _downStream->flush();
}

Settings::Settings(const QString& file) : QSettings(file, QSettings::IniFormat) {}

QVariant Settings::read(const QString& key, const QVariant& defaultValue) {
    QVariant result = defaultValue;

    if (contains(key)) {
        result = value(key);
    } else {
        setValue(key, defaultValue);
        sync();
    }

    return result;
}
