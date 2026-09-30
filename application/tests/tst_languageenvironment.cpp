#include "languageenvironment.h"
#include "preferences.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>
#include <cstring>
#include <libintl.h>
#include <locale.h>
#include <langinfo.h>

class LanguageEnvironmentTest : public QObject
{
    Q_OBJECT

private slots:
    void freshPreferencesForNewApplications()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray previous = qgetenv("XDG_CONFIG_HOME");
        qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
        QSettings settings(Preferences::file(QStringLiteral("services")), QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("Language"));
        settings.setValue("language", "en_US");
        auto environment = QProcessEnvironment::systemEnvironment();
        LanguageEnvironment::apply(environment);
        QCOMPARE(environment.value("LANGUAGE"), "en_US");
        settings.setValue("language", "zh_Hant_TW");
        settings.setValue("preferredLanguages", QStringList{"zh_Hant_TW", "ja_JP", "en_US"});
        settings.sync();
        LanguageEnvironment::apply(environment);
        QCOMPARE(environment.value("LANGUAGE"), "zh_TW:ja_JP:en_US");
        settings.setValue("language", "en_US");
        settings.setValue("preferredLanguages", QStringList{"en_US", "ja_JP"});
        settings.sync();
        LanguageEnvironment::apply(environment);
        QCOMPARE(environment.value("LANGUAGE"), "en_US");
        qputenv("XDG_CONFIG_HOME", previous);
    }

    void localeNames()
    {
        QCOMPARE(LanguageEnvironment::translationLanguages({"zh_Hans_CN", "zh_Hant_TW", "sr_Latn_RS", "de"}),
                 QStringList({"zh_CN", "zh_TW", "sr_RS@latin", "de_DE"}));
    }

    void precedenceAndAvailability()
    {
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("LC_ALL", "C");
        environment.insert("LC_MESSAGES", "C");
        environment.insert("LC_TIME", "en_US.UTF-8");
        LanguageEnvironment::apply(environment, {"zh_Hans_CN", "de_DE"});
        QVERIFY(!environment.contains("LC_ALL"));
        QCOMPARE(environment.value("LANGUAGE"), "zh_CN:de_DE");
        QCOMPARE(environment.value("LC_TIME"), "en_US.UTF-8");
        locale_t locale = newlocale(LC_ALL_MASK, environment.value("LC_MESSAGES").toUtf8().constData(), nullptr);
        QVERIFY(locale);
        freelocale(locale);
    }

    void repairsInheritedCategories()
    {
        for (const QString &ctype : {QStringLiteral("zh_Hans_CN.UTF-8"), QStringLiteral("C")}) {
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert("LC_CTYPE", ctype);
            environment.insert("LC_TIME", "zh_Hans_CN.UTF-8");
            environment.insert("LC_MONETARY", "zh_Hans_CN.UTF-8");
            environment.insert("LC_NUMERIC", "C");
            LanguageEnvironment::apply(environment, {"zh_Hans_CN", "en_US"});
            QCOMPARE(environment.value("LC_NUMERIC"), "C");
            QVERIFY(environment.value("LC_CTYPE") != ctype);
            QProcess process;
            process.setProcessEnvironment(environment);
            process.start(QCoreApplication::applicationFilePath(), {"--locale-probe"});
            QVERIFY(process.waitForFinished());
            QCOMPARE(process.exitCode(), 0);
            QCOMPARE(process.readAllStandardOutput(), QByteArray("UTF-8"));
            QVERIFY(process.readAllStandardError().isEmpty());

            const QString fontconfig = QStandardPaths::findExecutable("fc-match");
            if (!fontconfig.isEmpty()) {
                process.start(fontconfig, {"sans-serif"});
                QVERIFY(process.waitForFinished());
                QCOMPARE(process.exitCode(), 0);
                QVERIFY(!process.readAllStandardError().contains("not a valid region tag"));
            }
        }
    }

    void gettextUsesPreferredLanguage()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QDir().mkpath(directory.path() + "/zh_CN/LC_MESSAGES"));
        QFile source(directory.path() + "/test.po");
        QVERIFY(source.open(QIODevice::WriteOnly));
        source.write("msgid \"\"\nmsgstr \"\"\n\"Content-Type: text/plain; charset=UTF-8\\n\"\n\n"
                     "msgid \"Language test\"\nmsgstr \"语言测试\"\n");
        source.close();
        QCOMPARE(QProcess::execute("msgfmt", {source.fileName(), "-o",
                 directory.path() + "/zh_CN/LC_MESSAGES/cutefish-language-test.mo"}), 0);

        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("LC_ALL", "C");
        environment.insert("LC_CTYPE", "zh_Hans_CN.UTF-8");
        environment.insert("LC_TIME", "zh_Hans_CN.UTF-8");
        LanguageEnvironment::apply(environment, {"zh_Hans_CN", "en_US"});
        QProcess process;
        process.setProcessEnvironment(environment);
        process.start(QCoreApplication::applicationFilePath(), {"--gettext-probe", directory.path()});
        QVERIFY(process.waitForFinished());
        QCOMPARE(process.exitCode(), 0);
        QCOMPARE(QString::fromUtf8(process.readAllStandardOutput()), QStringLiteral("语言测试"));

        LanguageEnvironment::apply(environment, {"en_US", "zh_Hans_CN"});
        process.setProcessEnvironment(environment);
        process.start(QCoreApplication::applicationFilePath(), {"--gettext-probe", directory.path()});
        QVERIFY(process.waitForFinished());
        QCOMPARE(process.exitCode(), 0);
        QCOMPARE(QString::fromUtf8(process.readAllStandardOutput()), QStringLiteral("Language test"));
    }
};

int main(int argc, char **argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--locale-probe") == 0) {
        if (!setlocale(LC_ALL, ""))
            return 1;
        std::fputs(nl_langinfo(CODESET), stdout);
        return 0;
    }
    if (argc == 3 && std::strcmp(argv[1], "--gettext-probe") == 0) {
        if (!setlocale(LC_ALL, ""))
            return 1;
        bindtextdomain("cutefish-language-test", argv[2]);
        bind_textdomain_codeset("cutefish-language-test", "UTF-8");
        std::fputs(dgettext("cutefish-language-test", "Language test"), stdout);
        return 0;
    }
    QCoreApplication app(argc, argv);
    LanguageEnvironmentTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_languageenvironment.moc"
