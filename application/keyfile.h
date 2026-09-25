#ifndef KEYFILE_H
#define KEYFILE_H

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>

// Line-preserving access to INI-style files that several programs share, such as GLib's
// GSettings keyfile and KDE's kioslaverc. Lines this code does not touch stay byte-identical.
namespace KeyFile
{
using Group = QMap<QString, QString>;

// Raw text after '=' for every key, by group.
QMap<QString, Group> read(const QString &path);

// Replaces or appends the given keys of one group. Returns true only when the file changed.
bool writeGroup(const QString &path, const QString &group, const Group &values);
}

// The session runs GLib with GSETTINGS_BACKEND=keyfile: a schema at /a/b/ lives in group [a/b]
// and each value is GVariant text. Missing keys mean the schema default.
namespace GSettingsKeyfile
{
QString path();

QMap<QString, KeyFile::Group> read();

// Accepts bool, int, string and string list values.
bool writeGroup(const QString &group, const QMap<QString, QVariant> &values);

QString toString(const QString &text, const QString &fallback = QString());
int toInt(const QString &text, int fallback);
bool toBool(const QString &text, bool fallback);
QStringList toStringList(const QString &text, const QStringList &fallback);
}

#endif
