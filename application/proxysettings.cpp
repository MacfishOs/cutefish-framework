#include "proxysettings.h"
#include "keyfile.h"

#include <QUrl>

static const QString kProxyGroup = QStringLiteral("system/proxy");

static QString serverGroup(const QString &scheme)
{
    return kProxyGroup + QLatin1Char('/') + scheme;
}

static ProxySettings::Server loadServer(const KeyFile::Group &values, int defaultPort)
{
    ProxySettings::Server server;
    server.host = GSettingsKeyfile::toString(values.value(QStringLiteral("host")));
    server.port = GSettingsKeyfile::toInt(values.value(QStringLiteral("port")), defaultPort);
    return server;
}

static void saveServer(const QString &scheme, const ProxySettings::Server &server)
{
    GSettingsKeyfile::writeGroup(serverGroup(scheme), {
        {QStringLiteral("host"), server.host},
        {QStringLiteral("port"), server.port},
    });
}

static QString hostPort(const ProxySettings::Server &server)
{
    QString host = server.host;
    if (host.contains(QLatin1Char(':')) && !host.startsWith(QLatin1Char('[')))
        host = QLatin1Char('[') + host + QLatin1Char(']');
    return host + QLatin1Char(':') + QString::number(server.port);
}

static QString modeName(ProxySettings::Mode mode)
{
    switch (mode) {
    case ProxySettings::Auto:
        return QStringLiteral("auto");
    case ProxySettings::Manual:
        return QStringLiteral("manual");
    default:
        return QStringLiteral("none");
    }
}

static ProxySettings::Mode modeFromName(const QString &name)
{
    if (name == QLatin1String("auto"))
        return ProxySettings::Auto;
    if (name == QLatin1String("manual"))
        return ProxySettings::Manual;
    return ProxySettings::None;
}

bool ProxySettings::Server::isValid() const
{
    return !host.isEmpty() && port > 0 && port < 65536;
}

bool ProxySettings::Server::operator==(const Server &other) const
{
    return host == other.host && port == other.port;
}

ProxySettings ProxySettings::load()
{
    const QMap<QString, KeyFile::Group> groups = GSettingsKeyfile::read();
    const KeyFile::Group proxy = groups.value(kProxyGroup);
    const KeyFile::Group http = groups.value(serverGroup(QStringLiteral("http")));

    ProxySettings settings;
    settings.mode = modeFromName(GSettingsKeyfile::toString(proxy.value(QStringLiteral("mode"))));
    settings.autoconfigUrl =
            GSettingsKeyfile::toString(proxy.value(QStringLiteral("autoconfig-url")));
    settings.ignoreHosts = GSettingsKeyfile::toStringList(
            proxy.value(QStringLiteral("ignore-hosts")), settings.ignoreHosts);

    settings.http = loadServer(http, settings.http.port);
    settings.httpUseAuthentication =
            GSettingsKeyfile::toBool(http.value(QStringLiteral("use-authentication")), false);
    settings.httpUser = GSettingsKeyfile::toString(http.value(QStringLiteral("authentication-user")));
    settings.httpPassword =
            GSettingsKeyfile::toString(http.value(QStringLiteral("authentication-password")));

    settings.https = loadServer(groups.value(serverGroup(QStringLiteral("https"))), 0);
    settings.ftp = loadServer(groups.value(serverGroup(QStringLiteral("ftp"))), 0);
    settings.socks = loadServer(groups.value(serverGroup(QStringLiteral("socks"))), 0);
    return settings;
}

void ProxySettings::save() const
{
    // Servers first: watchers react to the mode, so it lands after what it points at.
    saveServer(QStringLiteral("http"), http);
    saveServer(QStringLiteral("https"), https);
    saveServer(QStringLiteral("ftp"), ftp);
    saveServer(QStringLiteral("socks"), socks);
    GSettingsKeyfile::writeGroup(kProxyGroup, {
        {QStringLiteral("mode"), modeName(mode)},
        {QStringLiteral("autoconfig-url"), autoconfigUrl},
    });
}

ProxySettings::Server ProxySettings::effectiveHttps() const
{
    return https.host.isEmpty() ? http : https;
}

bool ProxySettings::operator==(const ProxySettings &other) const
{
    return mode == other.mode && autoconfigUrl == other.autoconfigUrl
            && ignoreHosts == other.ignoreHosts && http == other.http && https == other.https
            && ftp == other.ftp && socks == other.socks
            && httpUseAuthentication == other.httpUseAuthentication
            && httpUser == other.httpUser && httpPassword == other.httpPassword;
}

QStringList ProxyEnvironment::names()
{
    static const QStringList lower = {
        QStringLiteral("http_proxy"), QStringLiteral("https_proxy"), QStringLiteral("ftp_proxy"),
        QStringLiteral("all_proxy"), QStringLiteral("no_proxy"),
    };
    QStringList result;
    for (const QString &name : lower)
        result << name << name.toUpper();
    return result;
}

QMap<QString, QString> ProxyEnvironment::variables(const ProxySettings &settings)
{
    QMap<QString, QString> lower;
    // A PAC script cannot be expressed as variables, so auto mode starts applications direct.
    if (settings.mode != ProxySettings::Manual)
        return lower;

    QString credentials;
    if (settings.httpUseAuthentication && !settings.httpUser.isEmpty()) {
        credentials = QString::fromUtf8(QUrl::toPercentEncoding(settings.httpUser));
        if (!settings.httpPassword.isEmpty())
            credentials += QLatin1Char(':')
                    + QString::fromUtf8(QUrl::toPercentEncoding(settings.httpPassword));
        credentials += QLatin1Char('@');
    }

    if (settings.http.isValid())
        lower.insert(QStringLiteral("http_proxy"),
                     QStringLiteral("http://") + credentials + hostPort(settings.http));

    const ProxySettings::Server https = settings.effectiveHttps();
    if (https.isValid()) {
        const QString httpsCredentials = https == settings.http ? credentials : QString();
        lower.insert(QStringLiteral("https_proxy"),
                     QStringLiteral("http://") + httpsCredentials + hostPort(https));
    }

    if (settings.ftp.isValid())
        lower.insert(QStringLiteral("ftp_proxy"), QStringLiteral("http://") + hostPort(settings.ftp));

    // curl reads a bare socks:// as SOCKS4; proxy clients all speak SOCKS5.
    if (settings.socks.isValid())
        lower.insert(QStringLiteral("all_proxy"), QStringLiteral("socks5://") + hostPort(settings.socks));

    if (lower.isEmpty())
        return lower;

    QStringList noProxy;
    for (const QString &host : settings.ignoreHosts) {
        // GIO's "*.example.com" is ".example.com" to curl and Go.
        noProxy << (host.startsWith(QLatin1String("*.")) ? host.mid(1) : host);
    }
    if (!noProxy.isEmpty())
        lower.insert(QStringLiteral("no_proxy"), noProxy.join(QLatin1Char(',')));

    QMap<QString, QString> result;
    for (auto it = lower.cbegin(); it != lower.cend(); ++it) {
        result.insert(it.key(), it.value());
        result.insert(it.key().toUpper(), it.value());
    }
    return result;
}

void ProxyEnvironment::apply(QProcessEnvironment &environment, const ProxySettings &settings)
{
    for (const QString &name : names())
        environment.remove(name);

    const QMap<QString, QString> values = variables(settings);
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        environment.insert(it.key(), it.value());
}

void ProxyEnvironment::apply(QProcessEnvironment &environment)
{
    apply(environment, ProxySettings::load());
}
