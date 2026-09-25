#include "keyfile.h"
#include "proxysettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class ProxySettingsTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_directory.reset(new QTemporaryDir);
        QVERIFY(m_directory->isValid());
        qputenv("XDG_CONFIG_HOME", m_directory->path().toUtf8());
    }

    void defaultsWithoutKeyfile()
    {
        const ProxySettings settings = ProxySettings::load();
        QCOMPARE(settings.mode, ProxySettings::None);
        QCOMPARE(settings.http.port, 8080);
        QCOMPARE(settings.ignoreHosts, QStringList({"localhost", "127.0.0.0/8", "::1"}));
        QVERIFY(ProxyEnvironment::variables(settings).isEmpty());
    }

    void readsWhatGLibWrites()
    {
        writeKeyfile("[org/gnome/desktop/interface]\n"
                     "color-scheme='prefer-dark'\n"
                     "\n"
                     "[system/proxy]\n"
                     "mode='manual'\n"
                     "ignore-hosts=['localhost', '*.local']\n"
                     "autoconfig-url=\"http://x/it's.pac\"\n"
                     "\n"
                     "[system/proxy/http]\n"
                     "host='127.0.0.1'\n"
                     "port=7890\n"
                     "authentication-password='p@ss:w\"d'\n"
                     "\n"
                     "[system/proxy/socks]\n"
                     "host='127.0.0.1'\n"
                     "port=uint32 7891\n");

        const ProxySettings settings = ProxySettings::load();
        QCOMPARE(settings.mode, ProxySettings::Manual);
        QCOMPARE(settings.ignoreHosts, QStringList({"localhost", "*.local"}));
        QCOMPARE(settings.autoconfigUrl, "http://x/it's.pac");
        QCOMPARE(settings.http, ProxySettings::Server({"127.0.0.1", 7890}));
        QCOMPARE(settings.httpPassword, "p@ss:w\"d");
        QCOMPARE(settings.socks, ProxySettings::Server({"127.0.0.1", 7891}));
        QCOMPARE(settings.https, ProxySettings::Server());
    }

    void stringListForms()
    {
        const QStringList fallback {"fallback"};
        QCOMPARE(GSettingsKeyfile::toStringList("[]", fallback), QStringList());
        QCOMPARE(GSettingsKeyfile::toStringList("@as []", fallback), QStringList());
        QCOMPARE(GSettingsKeyfile::toStringList("['a\\'b', \"c,d\"]", fallback),
                 QStringList({"a'b", "c,d"}));
        QCOMPARE(GSettingsKeyfile::toStringList("['a'", fallback), fallback);
        QCOMPARE(GSettingsKeyfile::toString("'\\u00e9\\n'"), QString::fromUtf8("é\n"));
    }

    void environmentForAProxyClient()
    {
        ProxySettings settings;
        settings.mode = ProxySettings::Manual;
        settings.http = settings.https = settings.ftp = settings.socks = {"127.0.0.1", 7890};
        settings.ignoreHosts = {"localhost", "*.local"};

        const QMap<QString, QString> variables = ProxyEnvironment::variables(settings);
        QCOMPARE(variables.value("http_proxy"), "http://127.0.0.1:7890");
        QCOMPARE(variables.value("HTTPS_PROXY"), "http://127.0.0.1:7890");
        QCOMPARE(variables.value("ftp_proxy"), "http://127.0.0.1:7890");
        QCOMPARE(variables.value("all_proxy"), "socks5://127.0.0.1:7890");
        QCOMPARE(variables.value("no_proxy"), "localhost,.local");
        QCOMPARE(variables.size(), ProxyEnvironment::names().size());
    }

    void environmentFollowsGio()
    {
        ProxySettings settings;
        settings.mode = ProxySettings::Manual;
        settings.http = {"::1", 3128};
        settings.httpUseAuthentication = true;
        settings.httpUser = "me@home";
        settings.httpPassword = "a:b";

        const QMap<QString, QString> variables = ProxyEnvironment::variables(settings);
        QCOMPARE(variables.value("http_proxy"), "http://me%40home:a%3Ab@[::1]:3128");
        // An unset HTTPS proxy falls back to the HTTP one, credentials included.
        QCOMPARE(variables.value("https_proxy"), variables.value("http_proxy"));
        QVERIFY(!variables.contains("ftp_proxy"));
        QVERIFY(!variables.contains("all_proxy"));

        settings.mode = ProxySettings::Auto;
        QVERIFY(ProxyEnvironment::variables(settings).isEmpty());
    }

    void applyDropsStaleVariables()
    {
        QProcessEnvironment environment;
        environment.insert("HTTP_PROXY", "http://stale:1");
        environment.insert("no_proxy", "stale");
        environment.insert("PATH", "/usr/bin");

        ProxySettings settings;
        ProxyEnvironment::apply(environment, settings);
        QVERIFY(!environment.contains("HTTP_PROXY"));
        QVERIFY(!environment.contains("no_proxy"));
        QCOMPARE(environment.value("PATH"), "/usr/bin");
    }

    void writePreservesOtherLines()
    {
        writeKeyfile("# comment\n"
                     "[org/gnome/desktop/interface]\n"
                     "color-scheme='prefer-dark'\n"
                     "\n"
                     "[system/proxy]\n"
                     "mode='none'\n"
                     "use-same-proxy=false\n");

        ProxySettings settings = ProxySettings::load();
        settings.mode = ProxySettings::Manual;
        settings.http = {"127.0.0.1", 7890};
        settings.save();

        const QString text = readKeyfile();
        QVERIFY(text.startsWith("# comment\n[org/gnome/desktop/interface]\n"
                                "color-scheme='prefer-dark'\n\n[system/proxy]\n"
                                "mode='manual'\nuse-same-proxy=false\n"));
        QCOMPARE(ProxySettings::load(), settings);

        // Saving the same values again leaves the file alone.
        QVERIFY(!GSettingsKeyfile::writeGroup("system/proxy", {{"mode", QStringLiteral("manual")}}));
        QVERIFY(GSettingsKeyfile::writeGroup("system/proxy", {{"mode", QStringLiteral("none")}}));
    }

    void roundTripsThroughGSettings()
    {
        const QString gsettings = QStandardPaths::findExecutable("gsettings");
        if (gsettings.isEmpty())
            QSKIP("gsettings is not installed");

        const auto run = [&](const QStringList &arguments) {
            QProcess process;
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert("GSETTINGS_BACKEND", "keyfile");
            process.setProcessEnvironment(environment);
            process.start(gsettings, arguments);
            process.waitForFinished();
            return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        };
        if (run({"list-schemas"}).split('\n').indexOf("org.gnome.system.proxy") < 0)
            QSKIP("org.gnome.system.proxy is not installed");

        // What a proxy client such as Throne does.
        run({"set", "org.gnome.system.proxy.http", "host", "127.0.0.1"});
        run({"set", "org.gnome.system.proxy.http", "port", "7890"});
        run({"set", "org.gnome.system.proxy", "ignore-hosts", "[\"it's\", 'b']"});
        run({"set", "org.gnome.system.proxy", "mode", "manual"});

        ProxySettings settings = ProxySettings::load();
        QCOMPARE(settings.mode, ProxySettings::Manual);
        QCOMPARE(settings.http, ProxySettings::Server({"127.0.0.1", 7890}));
        QCOMPARE(settings.ignoreHosts, QStringList({"it's", "b"}));

        settings.mode = ProxySettings::Auto;
        settings.autoconfigUrl = "file:///it's here.pac";
        settings.socks = {"proxy.lan", 1080};
        settings.save();
        QCOMPARE(run({"get", "org.gnome.system.proxy", "mode"}), "'auto'");
        QCOMPARE(run({"get", "org.gnome.system.proxy", "autoconfig-url"}),
                 "\"file:///it's here.pac\"");
        QCOMPARE(run({"get", "org.gnome.system.proxy.socks", "port"}), "1080");
    }

private:
    void writeKeyfile(const QByteArray &contents)
    {
        const QString path = GSettingsKeyfile::path();
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }

    QString readKeyfile()
    {
        QFile file(GSettingsKeyfile::path());
        if (!file.open(QIODevice::ReadOnly))
            return QString();
        return QString::fromUtf8(file.readAll());
    }

    QScopedPointer<QTemporaryDir> m_directory;
};

QTEST_GUILESS_MAIN(ProxySettingsTest)

#include "tst_proxysettings.moc"
