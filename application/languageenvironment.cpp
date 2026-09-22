#include "languageenvironment.h"

#include <QLocale>
#include <QProcess>
#include <QSettings>

#include <locale.h>
#include <langinfo.h>

namespace
{
QString posixLanguage(const QString &code)
{
    const QLocale locale(code);
    if (locale.language() == QLocale::C)
        return code;

    QString name = locale.name();
    const QLocale regionalLocale(name);
    if (locale.script() != regionalLocale.script()) {
        const QString script = QLocale::scriptToString(locale.script()).toLower();
        if (script == "latin" || script == "cyrillic" || script == "devanagari")
            name += '@' + script;
    }
    return name;
}

QString utf8Locale(QString name)
{
    const int modifier = name.indexOf('@');
    name.insert(modifier < 0 ? name.size() : modifier, ".UTF-8");
    return name;
}

bool isAvailable(const QString &name)
{
    locale_t locale = newlocale(LC_ALL_MASK, name.toUtf8().constData(), nullptr);
    if (!locale)
        return false;
    freelocale(locale);
    return true;
}

bool isUtf8Locale(const QString &name)
{
    locale_t locale = newlocale(LC_ALL_MASK, name.toUtf8().constData(), nullptr);
    if (!locale)
        return false;
    const QByteArray encoding = QByteArray(nl_langinfo_l(CODESET, locale)).toUpper();
    freelocale(locale);
    return encoding == "UTF-8" || encoding == "UTF8";
}

QString fallbackLocale()
{
    if (isAvailable("en_US.UTF-8"))
        return QStringLiteral("en_US.UTF-8");

    static const QString installedLocale = []() {
        QProcess process;
        process.start("locale", {"-a"});
        if (process.waitForFinished(1000)) {
            for (const QByteArray &line : process.readAllStandardOutput().split('\n')) {
                const QString name = QString::fromLocal8Bit(line.trimmed());
                const QString normalized = QString(name).remove('-').toLower();
                if (!normalized.startsWith("c.") && normalized.contains("utf8") && isAvailable(name))
                    return name;
            }
        }
        return QStringLiteral("C.UTF-8");
    }();
    return installedLocale;
}
}

QStringList LanguageEnvironment::preferredLanguages()
{
    QSettings settings("cutefish", "language");
    const QString primary = settings.value("language").toString();
    if (primary.isEmpty())
        return {};
    QStringList codes = settings.value("preferredLanguages").toStringList();
    codes.removeAll(QString());
    codes.removeAll(primary);
    codes.prepend(primary);
    codes.removeDuplicates();
    return codes;
}

QStringList LanguageEnvironment::translationLanguages(const QStringList &codes)
{
    QStringList languages;
    for (const QString &code : codes) {
        if (code.isEmpty())
            continue;
        languages.append(posixLanguage(code));
    }
    languages.removeDuplicates();
    return languages;
}

void LanguageEnvironment::apply(QProcessEnvironment &environment, const QStringList &codes)
{
    const QStringList languages = translationLanguages(codes);
    if (languages.isEmpty())
        return;

    QString localeName = utf8Locale(languages.first());
    if (!isAvailable(localeName)) {
        // Gettext needs a valid non-C message locale even when its language packs are installed.
        localeName = fallbackLocale();
    }
    environment.remove("LC_ALL");
    for (const QString &name : environment.keys()) {
        if (name.startsWith("LC_") && !environment.value(name).isEmpty()
            && !isAvailable(environment.value(name))) {
            environment.insert(name, localeName);
        }
    }
    // LC_CTYPE overrides LANG for Qt and Fontconfig, including valid but non-UTF-8 values like C.
    if (!environment.value("LC_CTYPE").isEmpty() && !isUtf8Locale(environment.value("LC_CTYPE")))
        environment.insert("LC_CTYPE", localeName);
    environment.insert("LANG", localeName);
    environment.insert("LC_MESSAGES", localeName);

    // Gettext has no English catalogs to stop at, so languages after English would replace the source strings.
    QStringList messageLanguages;
    for (const QString &language : languages) {
        messageLanguages.append(language);
        if (language == "en" || language.startsWith("en_") || language.startsWith("en@"))
            break;
    }
    environment.insert("LANGUAGE", messageLanguages.join(':'));
}

void LanguageEnvironment::apply(QProcessEnvironment &environment)
{
    apply(environment, preferredLanguages());
}
