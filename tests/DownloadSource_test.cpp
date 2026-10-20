// SPDX-License-Identifier: GPL-3.0-only
#include <QCryptographicHash>
#include <QEvent>
#include <QFile>
#include <QNetworkAccessManager>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

#include "net/ChecksumValidator.h"
#include "net/Download.h"
#include "net/DownloadSource.h"
#include "net/HeaderProxy.h"

class TestAuthorizationHeader : public Net::HeaderProxy {
   public:
    QList<Net::HeaderPair> headers(const QNetworkRequest&) const override { return { { "Authorization", "test-secret" } }; }
};

class DownloadSourceTest : public QObject {
    Q_OBJECT

   private slots:
    void mapping()
    {
        using namespace Net::DownloadSource;
        const auto base = baseUrl(Type::BMCLAPI, {});
        QCOMPARE(base, QUrl("https://bmclapi2.bangbang93.com"));
        QVERIFY(baseUrl(Type::Official, {}).isEmpty());
        QVERIFY(
            mirrorUrl(QUrl("https://libraries.minecraft.net/com/mojang/blocklist/1.0.10/blocklist-1.0.10.jar"), baseUrl(Type::Official, {}))
                .isEmpty());
        const auto prefixed = baseUrl(Type::Custom, "https://example.org/mc/proxy/");
        QCOMPARE(mirrorUrl(QUrl("https://resources.download.minecraft.net/9d/9dd32387135eefa7ab95996d52a5ca4cec8a3b30"), prefixed),
                 QUrl("https://example.org/mc/proxy/assets/9d/9dd32387135eefa7ab95996d52a5ca4cec8a3b30"));
        QCOMPARE(mirrorUrl(QUrl("https://libraries.minecraft.net/com/mojang/blocklist/1.0.10/blocklist-1.0.10.jar"), base),
                 QUrl("https://bmclapi2.bangbang93.com/maven/com/mojang/blocklist/1.0.10/blocklist-1.0.10.jar"));
        QCOMPARE(mirrorUrl(QUrl("https://piston-data.mojang.com/v1/objects/30c73b1c5da787909b2f73340419fdf13b9def88/client.jar"), base),
                 QUrl("https://bmclapi2.bangbang93.com/v1/objects/30c73b1c5da787909b2f73340419fdf13b9def88/client.jar"));
        QCOMPARE(mirrorUrl(QUrl("https://piston-meta.mojang.com/v1/packages/15200809663f45954c2c9a33ce88849e61aaaecc/17.json"), base),
                 QUrl("https://bmclapi2.bangbang93.com/v1/packages/15200809663f45954c2c9a33ce88849e61aaaecc/17.json"));
        QCOMPARE(
            mirrorUrl(QUrl("https://launchermeta.mojang.com/mc/assets/legacy/c0fd82e8ce9fbc93119e40d96d5a4e62cfa3f729/legacy.json"), base),
            QUrl("https://bmclapi2.bangbang93.com/mc/assets/legacy/c0fd82e8ce9fbc93119e40d96d5a4e62cfa3f729/legacy.json"));
    }

    void exclusionAndValidation()
    {
        using namespace Net::DownloadSource;
        QVERIFY(validCustomBase("https://mirror.example.com"));
        QVERIFY(validCustomBase("https://mirror.example.com/prefix"));
        for (const QString& bad :
             { "", "http://mirror.example.com", "https://user:pass@mirror.example.com", "https://mirror.example.com/?token=secret",
               "https://mirror.example.com/#fragment", "https://mirror.example.com/../escape" }) {
            QVERIFY2(!validCustomBase(bad), qPrintable(bad));
        }
        const auto base = baseUrl(Type::BMCLAPI, {});
        for (const QString& excluded :
             { "https://api.minecraftservices.com/minecraft/profile", "https://login.microsoftonline.com/consumers/oauth2/v2.0/token",
               "https://evil-libraries.minecraft.net/a/b.jar", "https://libraries.minecraft.net.evil.org/a/b.jar",
               "https://files.minecraftforge.net/maven/a/b.jar", "https://libraries.minecraft.net/a/b.jar?token=secret",
               "http://libraries.minecraft.net/a/b.jar", "https://libraries.minecraft.net/a/%2e%2e/b.jar" }) {
            QVERIFY2(mirrorUrl(QUrl(excluded), base).isEmpty(), qPrintable(excluded));
        }
        QVERIFY(
            mirrorUrl(QUrl("https://resources.download.minecraft.net/9d/9dd32387135eefa7ab95996d52a5ca4cec8a3b30"), base, true).isEmpty());
    }

    void fallback_data()
    {
        QTest::addColumn<int>("mirrorStatus");
        QTest::addColumn<QByteArray>("mirrorBody");
        QTest::addColumn<bool>("expectOfficial");
        QTest::newRow("mirror-success") << 200 << QByteArray("correct") << false;
        QTest::newRow("mirror-404") << 404 << QByteArray("missing") << true;
        QTest::newRow("mirror-network-error") << 0 << QByteArray() << true;
        QTest::newRow("mirror-checksum-mismatch") << 200 << QByteArray("corrupt") << true;
    }

    void fallback()
    {
        QFETCH(int, mirrorStatus);
        QFETCH(QByteArray, mirrorBody);
        QFETCH(bool, expectOfficial);

        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QStringList paths;
        QList<bool> sentAuthorization;
        connect(&server, &QTcpServer::newConnection, this, [&]() {
            auto* socket = server.nextPendingConnection();
            auto buffer = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket, buffer]() {
                buffer->append(socket->readAll());
                if (!buffer->contains("\r\n\r\n")) {
                    return;
                }
                const auto request = *buffer;
                const auto path = QString::fromLatin1(request.split(' ').at(1));
                paths.append(path);
                sentAuthorization.append(request.toLower().contains("authorization: test-secret"));
                const bool mirror = path == "/mirror";
                const int status = mirror ? mirrorStatus : 200;
                const auto body = mirror ? mirrorBody : QByteArray("correct");
                if (status != 0) {
                    socket->write("HTTP/1.1 " + QByteArray::number(status) + (status == 200 ? " OK\r\n" : " Not Found\r\n") +
                                  "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                }
                socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });

        const auto root = QUrl(QString("http://127.0.0.1:%1").arg(server.serverPort()));
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto file = dir.filePath("result.jar");
        QNetworkAccessManager network;
        auto download = Net::Download::makeFile(root.resolved(QUrl("/official")), file);
        const auto cleanup = qScopeGuard([&download] {
            download.reset();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        });
        download->setNetwork(&network);
        download->useMirror(root.resolved(QUrl("/mirror")));
        download->addHeaderProxy(std::make_unique<TestAuthorizationHeader>());
        download->addValidator(
            new Net::ChecksumValidator(QCryptographicHash::Sha1, QCryptographicHash::hash("correct", QCryptographicHash::Sha1)));
        QSignalSpy succeeded(download.get(), &Net::Download::succeeded);
        QSignalSpy failed(download.get(), &Net::Download::failed);
        download->start();
        QTRY_COMPARE_WITH_TIMEOUT(succeeded.size(), 1, 5000);
        QCOMPARE(failed.size(), 0);
        // Qt may transparently repeat a GET after a peer closes without a response.
        const qsizetype mirrorAttempts = mirrorStatus == 0 ? paths.size() - 1 : 1;
        QVERIFY(mirrorAttempts >= 1);
        QStringList expectedPaths;
        QList<bool> expectedAuthorization;
        for (qsizetype i = 0; i < mirrorAttempts; ++i) {
            expectedPaths.append("/mirror");
            expectedAuthorization.append(false);
        }
        if (expectOfficial) {
            expectedPaths.append("/official");
            expectedAuthorization.append(true);
        }
        QCOMPARE(paths, expectedPaths);
        QCOMPARE(sentAuthorization, expectedAuthorization);
        QFile result(file);
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), QByteArray("correct"));
    }

    void abortDoesNotFallback()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QStringList paths;
        connect(&server, &QTcpServer::newConnection, this, [&]() {
            auto* socket = server.nextPendingConnection();
            auto buffer = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket, buffer]() {
                buffer->append(socket->readAll());
                if (!buffer->contains("\r\n\r\n")) {
                    return;
                }
                paths.append(QString::fromLatin1(buffer->split(' ').at(1)));
                // Keep the mirror request pending until the caller aborts it.
            });
        });
        const auto root = QUrl(QString("http://127.0.0.1:%1").arg(server.serverPort()));
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QNetworkAccessManager network;
        auto download = Net::Download::makeFile(root.resolved(QUrl("/official")), dir.filePath("result.jar"));
        const auto cleanup = qScopeGuard([&download] {
            download.reset();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        });
        download->setNetwork(&network);
        download->useMirror(root.resolved(QUrl("/mirror")));
        QSignalSpy aborted(download.get(), &Net::Download::aborted);
        download->start();
        QTRY_COMPARE_WITH_TIMEOUT(paths.size(), 1, 5000);
        QVERIFY(download->abort());
        QTRY_COMPARE_WITH_TIMEOUT(aborted.size(), 1, 5000);
        QCOMPARE(paths, QStringList({ "/mirror" }));
    }
};

QTEST_GUILESS_MAIN(DownloadSourceTest)
#include "DownloadSource_test.moc"
