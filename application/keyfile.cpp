#include "keyfile.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace
{
QStringList readLines(const QString &path)
{
    QStringList lines;
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    while (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    return lines;
}

bool isGroupHeader(const QString &line)
{
    return line.startsWith(QLatin1Char('['));
}

QString gvariantString(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    escaped.replace(QLatin1Char('\''), QLatin1String("\\'"));
    escaped.replace(QLatin1Char('\n'), QLatin1String("\\n"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

QString gvariantValue(const QVariant &value)
{
    switch (value.typeId()) {
    case QMetaType::Bool:
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QMetaType::Int:
        return QString::number(value.toInt());
    case QMetaType::QStringList: {
        QStringList items;
        for (const QString &item : value.toStringList())
            items << gvariantString(item);
        return QLatin1Char('[') + items.join(QLatin1String(", ")) + QLatin1Char(']');
    }
    default:
        return gvariantString(value.toString());
    }
}

// Parses one quoted GVariant string starting at text[pos] and moves pos past it.
bool parseString(const QString &text, int &pos, QString &result)
{
    if (pos >= text.size())
        return false;
    const QChar quote = text.at(pos);
    if (quote != QLatin1Char('\'') && quote != QLatin1Char('"'))
        return false;

    result.clear();
    for (++pos; pos < text.size(); ++pos) {
        const QChar c = text.at(pos);
        if (c == quote) {
            ++pos;
            return true;
        }
        if (c != QLatin1Char('\\')) {
            result += c;
            continue;
        }
        if (++pos >= text.size())
            return false;

        const QChar escaped = text.at(pos);
        switch (escaped.unicode()) {
        case 'n': result += QLatin1Char('\n'); break;
        case 't': result += QLatin1Char('\t'); break;
        case 'r': result += QLatin1Char('\r'); break;
        case 'b': result += QLatin1Char('\b'); break;
        case 'f': result += QLatin1Char('\f'); break;
        case 'v': result += QLatin1Char('\v'); break;
        case 'a': result += QLatin1Char('\a'); break;
        case 'u':
        case 'U': {
            const int digits = escaped == QLatin1Char('u') ? 4 : 8;
            bool ok = false;
            const char32_t code = text.mid(pos + 1, digits).toUInt(&ok, 16);
            if (!ok)
                return false;
            result += QString::fromUcs4(&code, 1);
            pos += digits;
            break;
        }
        default:
            result += escaped;
        }
    }
    return false;
}

void skipSpaces(const QString &text, int &pos)
{
    while (pos < text.size() && text.at(pos).isSpace())
        ++pos;
}
}

QMap<QString, KeyFile::Group> KeyFile::read(const QString &path)
{
    QMap<QString, Group> groups;
    Group *current = nullptr;
    for (const QString &rawLine : readLines(path)) {
        const QString line = rawLine.trimmed();
        if (isGroupHeader(line)) {
            const int end = line.indexOf(QLatin1Char(']'));
            current = end > 0 ? &groups[line.mid(1, end - 1)] : nullptr;
            continue;
        }
        if (!current || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';')))
            continue;

        const int eq = line.indexOf(QLatin1Char('='));
        if (eq > 0)
            current->insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
    }
    return groups;
}

bool KeyFile::writeGroup(const QString &path, const QString &group, const Group &values)
{
    QStringList lines = readLines(path);
    Group pending = values;
    const QString header = QLatin1Char('[') + group + QLatin1Char(']');
    bool changed = false;
    int groupEnd = -1;
    bool inGroup = false;
    bool groupFound = false;
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines.at(i).trimmed();
        if (isGroupHeader(line)) {
            if (inGroup) {
                groupEnd = i;
                break;
            }
            inGroup = line == header;
            groupFound = groupFound || inGroup;
            continue;
        }
        if (!inGroup)
            continue;

        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = line.left(eq).trimmed();
        if (!pending.contains(key))
            continue;

        const QString value = pending.take(key);
        if (line.mid(eq + 1).trimmed() != value) {
            lines[i] = key + QLatin1Char('=') + value;
            changed = true;
        }
    }
    if (inGroup && groupEnd < 0)
        groupEnd = lines.size();

    QStringList added;
    for (auto it = pending.cbegin(); it != pending.cend(); ++it)
        added << it.key() + QLatin1Char('=') + it.value();

    if (!groupFound) {
        if (!lines.isEmpty())
            lines << QString();
        lines << header;
        lines << added;
        changed = true;
    } else if (!added.isEmpty()) {
        // Keep the blank separator before the next group after the inserted keys.
        int insertAt = groupEnd;
        while (insertAt > 0 && lines.at(insertAt - 1).trimmed().isEmpty())
            --insertAt;
        for (int i = 0; i < added.size(); ++i)
            lines.insert(insertAt + i, added.at(i));
        changed = true;
    }

    if (!changed)
        return false;

    QDir().mkpath(QFileInfo(path).absolutePath());
    // GLib and KConfig replace these files atomically too; a partial write would read as empty.
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Cannot write" << path << out.errorString();
        return false;
    }
    out.write(lines.join(QLatin1Char('\n')).toUtf8());
    out.write("\n");
    return out.commit();
}

QString GSettingsKeyfile::path()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QLatin1String("/glib-2.0/settings/keyfile");
}

QMap<QString, KeyFile::Group> GSettingsKeyfile::read()
{
    return KeyFile::read(path());
}

bool GSettingsKeyfile::writeGroup(const QString &group, const QMap<QString, QVariant> &values)
{
    KeyFile::Group text;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        text.insert(it.key(), gvariantValue(it.value()));
    return KeyFile::writeGroup(path(), group, text);
}

QString GSettingsKeyfile::toString(const QString &text, const QString &fallback)
{
    int pos = 0;
    QString result;
    if (!parseString(text, pos, result))
        return fallback;
    return result;
}

int GSettingsKeyfile::toInt(const QString &text, int fallback)
{
    // Other writers may annotate the type, as in "uint32 8080".
    bool ok = false;
    const int value = text.section(QLatin1Char(' '), -1).toInt(&ok);
    return ok ? value : fallback;
}

bool GSettingsKeyfile::toBool(const QString &text, bool fallback)
{
    if (text == QLatin1String("true"))
        return true;
    if (text == QLatin1String("false"))
        return false;
    return fallback;
}

QStringList GSettingsKeyfile::toStringList(const QString &text, const QStringList &fallback)
{
    QString list = text;
    if (list.startsWith(QLatin1String("@as")))
        list = list.mid(3).trimmed();
    if (!list.startsWith(QLatin1Char('[')))
        return fallback;

    QStringList result;
    int pos = 1;
    skipSpaces(list, pos);
    if (pos < list.size() && list.at(pos) == QLatin1Char(']'))
        return result;

    while (pos < list.size()) {
        QString item;
        if (!parseString(list, pos, item))
            return fallback;
        result << item;

        skipSpaces(list, pos);
        if (pos >= list.size())
            return fallback;
        if (list.at(pos) == QLatin1Char(']'))
            return result;
        if (list.at(pos) != QLatin1Char(','))
            return fallback;
        ++pos;
        skipSpaces(list, pos);
    }
    return fallback;
}
