#ifndef PROXYSETTINGS_H
#define PROXYSETTINGS_H

#include <QMap>
#include <QProcessEnvironment>
#include <QStringList>

// The session proxy, stored where GNOME keeps it: org.gnome.system.proxy in the GSettings
// keyfile. Third-party proxy clients write it with gsettings, so every other consumer derives
// from it.
struct ProxySettings
{
    enum Mode { None, Auto, Manual };

    struct Server
    {
        QString host;
        int port = 0;

        bool isValid() const;
        bool operator==(const Server &other) const;
        bool operator!=(const Server &other) const { return !(*this == other); }
    };

    Mode mode = None;
    QString autoconfigUrl;
    QStringList ignoreHosts = {QStringLiteral("localhost"), QStringLiteral("127.0.0.0/8"),
                               QStringLiteral("::1")};
    Server http {QString(), 8080};
    Server https;
    Server ftp;
    Server socks;
    bool httpUseAuthentication = false;
    QString httpUser;
    QString httpPassword;

    static ProxySettings load();

    // Writes the mode, the PAC URL and the servers. Ignore hosts and credentials are left as
    // they are, since nothing in Cutefish edits them.
    void save() const;

    // GIO's reading of the schema: HTTPS falls back to the HTTP proxy, other schemes to SOCKS.
    Server effectiveHttps() const;

    bool operator==(const ProxySettings &other) const;
    bool operator!=(const ProxySettings &other) const { return !(*this == other); }
};

namespace ProxyEnvironment
{
// Every variable the proxy owns; a launch drops the ones the current mode does not set.
QStringList names();

QMap<QString, QString> variables(const ProxySettings &settings);

void apply(QProcessEnvironment &environment, const ProxySettings &settings);
void apply(QProcessEnvironment &environment);
}

#endif
