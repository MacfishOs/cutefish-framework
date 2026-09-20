#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

class QFileSystemWatcher;

// Per-application screen recording grants, shared between the portal that
// asks and the Settings page that revokes. Every application that has asked
// is recorded; only "always allow" sets allowed.
class ScreenRecordingPermissions : public QObject
{
    Q_OBJECT

public:
    struct Grant {
        QString appId;
        bool allowed = false;
        QStringList monitors;
    };

    // The default file is $XDG_CONFIG_HOME/cutefish/privacy.conf.
    explicit ScreenRecordingPermissions(QObject *parent = nullptr);
    ScreenRecordingPermissions(const QString &path, QObject *parent = nullptr);

    QString path() const { return m_path; }
    QList<Grant> grants() const;
    std::optional<Grant> grant(const QString &appId) const;

    void record(const QString &appId);
    void allow(const QString &appId, const QStringList &monitors);
    void setAllowed(const QString &appId, bool allowed);
    void remove(const QString &appId);

Q_SIGNALS:
    void changed();

private:
    bool watch();

    QString m_path;
    QFileSystemWatcher *m_watcher;
};
