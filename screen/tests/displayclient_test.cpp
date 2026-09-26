#include <memory>
#include <optional>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include "displayclient.h"

using cutefish::display::ipc::ErrorCode;

namespace {
const QString serviceName = QStringLiteral("com.cutefish.DisplayConfig.ClientTest");
const QString objectPath = QStringLiteral("/com/cutefish/DisplayConfig1");

class FakeDisplayConfig : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.cutefish.DisplayConfig1")

public:
    enum class Answer {
        BeforeReply,
        AfterReply,
        Never,
    };

    Answer answer = Answer::AfterReply;
    // For SetDockedLidClosed; SetDisplayPower always succeeds.
    bool succeeded = true;
    ErrorCode error = ErrorCode::None;
    QList<bool> dockedRequests;
    QList<uint> powerRequests;

public slots:
    QString SetDockedLidClosed(bool closed)
    {
        dockedRequests.append(closed);
        return finish(succeeded, error);
    }

    QString SetDisplayPower(const QStringList &, uint state)
    {
        powerRequests.append(state);
        return finish(true, ErrorCode::None);
    }

signals:
    void OperationFinished(const QString &token, bool succeeded, bool degraded, uint errorCode,
                           const QString &message, const QStringList &affectedDisplayIds,
                           qulonglong topologyGeneration, qulonglong appliedRevision);
    void IdentifyRequested(const QStringList &displayIds);

private:
    QString finish(bool succeeded, ErrorCode error)
    {
        const QString token = QStringLiteral("token-%1").arg(++m_serial);
        const auto report = [this, token, succeeded, error] {
            emit OperationFinished(token, succeeded, false, uint(error), {}, {}, 1, 1);
        };
        if (answer == Answer::BeforeReply)
            report();
        else if (answer == Answer::AfterReply)
            QTimer::singleShot(0, this, report);
        return token;
    }

    int m_serial = 0;
};

// The compositor side, on a connection of its own as it would be in another process.
class Service
{
public:
    Service()
        : m_bus(QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                              QStringLiteral("fake-display-config")))
    {
    }

    ~Service()
    {
        stop();
        QDBusConnection::disconnectFromBus(m_bus.name());
    }

    bool start()
    {
        return m_bus.registerObject(objectPath, &fake,
                                    QDBusConnection::ExportAllSlots
                                            | QDBusConnection::ExportAllSignals)
                && m_bus.registerService(serviceName);
    }

    void stop()
    {
        m_bus.unregisterService(serviceName);
        m_bus.unregisterObject(objectPath);
    }

    FakeDisplayConfig fake;

private:
    QDBusConnection m_bus;
};

struct Result {
    bool succeeded = false;
    ErrorCode error = ErrorCode::None;
};
} // namespace

class DisplayClientTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qputenv("CUTEFISH_DISPLAY_CONFIG_SERVICE", serviceName.toUtf8());
    }

    void requestsNeedTheService()
    {
        DisplayClient client;
        QVERIFY(!client.setDockedLidClosed(true, [](bool, ErrorCode) {
            QFAIL("a request that was never sent must not complete");
        }));
        QVERIFY(!client.setDisplayPower(false));
    }

    void completesWhenTheOperationFinishesAfterTheReply()
    {
        Service service;
        QVERIFY(service.start());
        DisplayClient client;
        std::optional<Result> result;
        QVERIFY(client.setDockedLidClosed(true, [&](bool succeeded, ErrorCode error) {
            result = Result {succeeded, error};
        }));
        QTRY_VERIFY(result.has_value());
        QVERIFY(result->succeeded);
        QCOMPARE(result->error, ErrorCode::None);
        QCOMPARE(service.fake.dockedRequests, QList<bool> {true});
    }

    void completesWhenARejectionArrivesBeforeTheReply()
    {
        Service service;
        service.fake.answer = FakeDisplayConfig::Answer::BeforeReply;
        service.fake.succeeded = false;
        service.fake.error = ErrorCode::Busy;
        QVERIFY(service.start());
        DisplayClient client;
        std::optional<Result> result;
        QVERIFY(client.setDockedLidClosed(true, [&](bool succeeded, ErrorCode error) {
            result = Result {succeeded, error};
        }));
        QTRY_VERIFY(result.has_value());
        QVERIFY(!result->succeeded);
        QCOMPARE(result->error, ErrorCode::Busy);
    }

    void eachRequestGetsItsOwnResult()
    {
        Service service;
        service.fake.answer = FakeDisplayConfig::Answer::BeforeReply;
        QVERIFY(service.start());
        DisplayClient client;
        std::optional<Result> power;
        std::optional<Result> docked;
        QVERIFY(client.setDisplayPower(false, [&](bool succeeded, ErrorCode error) {
            power = Result {succeeded, error};
        }));
        service.fake.succeeded = false;
        service.fake.error = ErrorCode::NoEnabledOutput;
        QVERIFY(client.setDockedLidClosed(true, [&](bool succeeded, ErrorCode error) {
            docked = Result {succeeded, error};
        }));
        QTRY_VERIFY(power.has_value() && docked.has_value());
        QVERIFY(power->succeeded);
        QVERIFY(!docked->succeeded);
        QCOMPARE(docked->error, ErrorCode::NoEnabledOutput);
        QCOMPARE(service.fake.powerRequests, QList<uint> {1});
    }

    void losingTheServiceFailsWhatIsPending()
    {
        Service service;
        service.fake.answer = FakeDisplayConfig::Answer::Never;
        QVERIFY(service.start());
        DisplayClient client;
        std::optional<Result> result;
        QVERIFY(client.setDockedLidClosed(true, [&](bool succeeded, ErrorCode error) {
            result = Result {succeeded, error};
        }));
        QTRY_COMPARE(service.fake.dockedRequests.size(), 1);
        // Let the reply arrive, so the request waits on its OperationFinished.
        QTest::qWait(100);
        QVERIFY(!result.has_value());
        service.stop();
        QTRY_VERIFY(result.has_value());
        QVERIFY(!result->succeeded);
        QCOMPARE(result->error, ErrorCode::Unavailable);
    }

    void forwardsIdentifyRequests()
    {
        Service service;
        QVERIFY(service.start());
        DisplayClient client;
        QSignalSpy identify(&client, &DisplayClient::identifyRequested);
        emit service.fake.IdentifyRequested({QStringLiteral("display-1")});
        QTRY_COMPARE(identify.count(), 1);
        QCOMPARE(identify.at(0).at(0).toStringList(), QStringList {QStringLiteral("display-1")});
    }
};

QTEST_GUILESS_MAIN(DisplayClientTest)

#include "displayclient_test.moc"
