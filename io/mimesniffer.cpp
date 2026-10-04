// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mimesniffer_p.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimeZone>
#include <QXmlStreamReader>
#include <QtEndian>

#include <string.h>

#include <span>
#include <vector>

namespace Cutefish::IO::Internal {

namespace {

// Qt reads this much of a file for content matching (QIODEVICE_BUFFERSIZE).
constexpr qint64 SniffLength = 16384;

// Qt's own directory for a built-in database, searched after the XDG ones.
const QString InternalDirectory = QStringLiteral(":/qt-project.org/qmime");

const QString DefaultType = QStringLiteral("application/octet-stream");

QStringList mimeDirectories()
{
    return QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("mime"),
                                     QStandardPaths::LocateDirectory)
            + QStringList {InternalDirectory};
}

// What a reload has to notice: the directories, and each cache's replacement.
QString databaseVersion(const QStringList &directories)
{
    QString version;
    for (const QString &directory : directories) {
        const QFileInfo cache(directory + QStringLiteral("/mime.cache"));
        version += directory + u' '
                + QString::number(cache.lastModified(QTimeZone::UTC).toMSecsSinceEpoch()) + u'\n';
    }

    return version;
}

// QMimeGlobMatchResult: the highest weight wins, then the longest pattern.
struct NameMatches
{
    void add(const QString &type, int weight, qsizetype patternLength)
    {
        if (all.contains(type))
            return;

        if (weight < m_weight) {
            all.append(type);
            return;
        }

        bool replace = weight > m_weight;
        if (!replace) {
            if (patternLength < m_patternLength)
                return;
            replace = patternLength > m_patternLength;
        }

        if (replace) {
            matching.clear();
            m_patternLength = patternLength;
            m_weight = weight;
        }

        if (!matching.contains(type)) {
            matching.append(type);
            if (replace)
                all.prepend(type);
            else
                all.append(type);
        }
    }

    QStringList matching;
    QStringList all;

private:
    int m_weight = 0;
    qsizetype m_patternLength = 0;
};

// A literal or complex glob, classified as QMimeGlobPattern does.
struct Glob
{
    enum Kind {
        Suffix,
        Prefix,
        Literal,
        Vdr,
        Anim,
        Other
    };

    Glob(const QString &source, const char *mimeType, int globWeight, bool sensitive)
        : pattern(sensitive ? source : source.toLower())
        , type(QString::fromLatin1(mimeType))
        , weight(globWeight)
        , caseSensitive(sensitive)
        , kind(classify(pattern))
    {
    }

    static Kind classify(QStringView pattern)
    {
        if (pattern.isEmpty())
            return Other;

        const qsizetype stars = pattern.count(u'*');
        if (!pattern.contains(u'[') && !pattern.contains(u'?')) {
            if (stars == 1) {
                if (pattern.front() == u'*')
                    return Suffix;
                if (pattern.back() == u'*')
                    return Prefix;
            } else if (stars == 0) {
                return Literal;
            }
        }

        if (pattern == QLatin1StringView("[0-9][0-9][0-9].vdr"))
            return Vdr;
        if (pattern == QLatin1StringView("*.anim[1-9j]"))
            return Anim;

        return Other;
    }

    // QMimeGlobPattern::matchFileName().
    bool matches(const QString &inputFileName) const
    {
        const QString fileName = caseSensitive ? inputFileName : inputFileName.toLower();
        const qsizetype patternLength = pattern.size();
        const qsizetype fileNameLength = fileName.size();

        if (!patternLength)
            return false;

        switch (kind) {
        case Suffix:
            if (fileNameLength + 1 < patternLength)
                return false;
            for (qsizetype i = 1; i < patternLength; ++i) {
                if (pattern.at(patternLength - i) != fileName.at(fileNameLength - i))
                    return false;
            }
            return true;
        case Prefix:
            if (fileNameLength + 1 < patternLength)
                return false;
            for (qsizetype i = 0; i + 1 < patternLength; ++i) {
                if (pattern.at(i) != fileName.at(i))
                    return false;
            }
            return true;
        case Literal:
            return pattern == fileName;
        case Vdr:
            return fileNameLength == 7 && fileName.at(0).isDigit() && fileName.at(1).isDigit()
                    && fileName.at(2).isDigit()
                    && QStringView(fileName).mid(3, 4) == QLatin1StringView(".vdr");
        case Anim: {
            if (fileNameLength < 6)
                return false;
            const QChar last = fileName.at(fileNameLength - 1);
            return ((last.isDigit() && last != u'0') || last == u'j')
                    && QStringView(fileName).mid(fileNameLength - 6, 5) == QLatin1StringView(".anim");
        }
        case Other:
            break;
        }

        // Qt builds this expression for every file it matches; each thread keeps its own.
        thread_local QHash<QString, QRegularExpression> expressions;
        auto expression = expressions.constFind(pattern);
        if (expression == expressions.cend())
            expression = expressions.insert(pattern, QRegularExpression::fromWildcard(pattern));

        return expression->match(fileName).hasMatch();
    }

    QString pattern;
    QString type;
    int weight;
    bool caseSensitive;
    Kind kind;
};

// The type files of one MIME directory and which of them carry glob-deleteall, which
// drops the type's globs from the directories after it.
class TypeOverrides
{
public:
    TypeOverrides() = default;

    explicit TypeOverrides(const QString &directory)
    {
        const QDir root(directory);
        const QStringList media = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

        for (const QString &medium : media) {
            const QDir types(root.filePath(medium));
            const QStringList files = types.entryList({QStringLiteral("*.xml")}, QDir::Files);

            for (const QString &file : files) {
                const bool deletes = readsGlobDeleteAll(types.filePath(file));
                m_types.insert(medium + u'/' + file.chopped(4), deletes);
                m_deletes = m_deletes || deletes;
            }
        }
    }

    // QMimeBinaryProvider::hasGlobDeleteAll(): the lowercased type file first.
    bool deletesGlobs(const QString &type) const
    {
        if (!m_deletes)
            return false;

        auto found = m_types.constFind(type.toLower());
        if (found == m_types.cend())
            found = m_types.constFind(type);

        return found != m_types.cend() && *found;
    }

private:
    // QMimeBinaryProvider::loadMimeTypeExtra() reads the same elements.
    static bool readsGlobDeleteAll(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return false;

        const QByteArray contents = file.readAll();
        if (!contents.contains("glob-deleteall"))
            return false;

        QXmlStreamReader xml(contents);
        if (!xml.readNextStartElement() || xml.name() != QLatin1StringView("mime-type")
            || xml.attributes().value(QLatin1StringView("type")).isEmpty())
            return false;

        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1StringView("glob-deleteall"))
                return true;
            xml.skipCurrentElement();
        }

        return false;
    }

    QHash<QString, bool> m_types;
    bool m_deletes = false;
};

// QMimeProviderBase::isMimeTypeGlobsExcluded(): whether a directory before this
// one deletes the type's globs.
bool globsExcluded(std::span<const TypeOverrides> earlier, const QString &type)
{
    for (const TypeOverrides &overrides : earlier) {
        if (overrides.deletesGlobs(type))
            return true;
    }

    return false;
}

// QMimeBinaryProvider's reading of a mime.cache, which trusts its offsets too.
class CacheFile
{
public:
    explicit CacheFile(const QString &path)
        : m_file(path)
    {
        if (!m_file.open(QIODevice::ReadOnly))
            return;

        m_data = m_file.map(0, m_file.size());
        if (!m_data || m_file.size() < 4)
            return;

        const int major = uint16(0);
        const int minor = uint16(2);
        m_valid = major == 1 && minor >= 1 && minor <= 2;

        if (m_valid) {
            m_literals = globList(uint32(12));
            m_globs = globList(uint32(20));
        }
    }

    bool isValid() const
    {
        return m_valid;
    }

    // QMimeBinaryProvider::addFileNameMatches(), skipping types whose globs an
    // earlier directory deletes.
    void addFileNameMatches(const QString &fileName, std::span<const TypeOverrides> earlier,
                            NameMatches *result) const
    {
        bool matched = matchGlobs(m_literals, fileName, earlier, result);

        if (!matched) {
            const QString lowerFileName = fileName.toLower();
            const int treeOffset = uint32(16);
            const int numRoots = uint32(treeOffset);
            const int firstRootOffset = uint32(treeOffset + 4);

            matched = matchSuffixTree(result, earlier, numRoots, firstRootOffset, lowerFileName,
                                      lowerFileName.size() - 1, false)
                    || matchSuffixTree(result, earlier, numRoots, firstRootOffset, fileName,
                                       fileName.size() - 1, true);
        }

        if (!matched)
            matchGlobs(m_globs, fileName, earlier, result);
    }

    // QMimeBinaryProvider::findByMagic(): the first rule that matches, if it is
    // more accurate than what an earlier cache found.
    void findByMagic(const QByteArray &data, int *accuracy, const char **type) const
    {
        const int magicListOffset = uint32(24);
        const int numMatches = uint32(magicListOffset);
        const int firstMatchOffset = uint32(magicListOffset + 8);

        for (int i = 0; i < numMatches; ++i) {
            const int offset = firstMatchOffset + i * 16;

            if (matchRule(uint32(offset + 8), uint32(offset + 12), data)) {
                const int matchAccuracy = static_cast<int>(uint32(offset));

                if (matchAccuracy > *accuracy) {
                    *accuracy = matchAccuracy;
                    *type = string(uint32(offset + 4));
                    return;
                }
            }
        }
    }

private:
    quint16 uint16(int offset) const
    {
        return qFromBigEndian<quint16>(m_data + offset);
    }

    quint32 uint32(int offset) const
    {
        return qFromBigEndian<quint32>(m_data + offset);
    }

    const char *string(int offset) const
    {
        return reinterpret_cast<const char *>(m_data + offset);
    }

    std::vector<Glob> globList(int offset) const
    {
        std::vector<Glob> globs;
        const int count = uint32(offset);
        globs.reserve(count);

        for (int i = 0; i < count; ++i) {
            const int entry = offset + 4 + 12 * i;
            const int flagsAndWeight = uint32(entry + 8);
            globs.emplace_back(QString::fromLatin1(string(uint32(entry))), string(uint32(entry + 4)),
                               flagsAndWeight & 0xff, flagsAndWeight & 0x100);
        }

        return globs;
    }

    static bool matchGlobs(const std::vector<Glob> &globs, const QString &fileName,
                           std::span<const TypeOverrides> earlier, NameMatches *result)
    {
        bool matched = false;
        for (const Glob &glob : globs) {
            if (globsExcluded(earlier, glob.type))
                continue;

            if (glob.matches(fileName)) {
                result->add(glob.type, glob.weight, glob.pattern.size());
                matched = true;
            }
        }

        return matched;
    }

    // QMimeBinaryProvider::matchSuffixTree(): the file name from its last
    // character back, along a tree of reversed extensions.
    bool matchSuffixTree(NameMatches *result, std::span<const TypeOverrides> earlier,
                         int numEntries, int firstOffset, const QString &fileName,
                         qsizetype charPos, bool caseSensitiveCheck) const
    {
        const QChar fileChar = fileName.at(charPos);
        int min = 0;
        int max = numEntries - 1;

        while (min <= max) {
            const int mid = (min + max) / 2;
            const int offset = firstOffset + 12 * mid;
            const QChar ch = char16_t(uint32(offset));

            if (ch < fileChar) {
                min = mid + 1;
            } else if (ch > fileChar) {
                max = mid - 1;
            } else {
                --charPos;
                const int numChildren = uint32(offset + 4);
                const int childrenOffset = uint32(offset + 8);
                bool success = false;

                if (charPos > 0)
                    success = matchSuffixTree(result, earlier, numChildren, childrenOffset,
                                              fileName, charPos, caseSensitiveCheck);

                if (!success) {
                    for (int i = 0; i < numChildren; ++i) {
                        const int childOffset = childrenOffset + 12 * i;
                        if (uint32(childOffset) != 0)
                            break;

                        const QString type = QString::fromLatin1(string(uint32(childOffset + 4)));
                        if (globsExcluded(earlier, type))
                            continue;

                        const int flagsAndWeight = uint32(childOffset + 8);
                        const bool caseSensitive = flagsAndWeight & 0x100;

                        if (caseSensitiveCheck || !caseSensitive) {
                            // The pattern is '*' and the characters matched so far.
                            result->add(type, flagsAndWeight & 0xff, fileName.size() - charPos);
                            success = true;
                        }
                    }
                }

                return success;
            }
        }

        return false;
    }

    bool matchRule(int numMatchlets, int firstOffset, const QByteArray &data) const
    {
        for (int matchlet = 0; matchlet < numMatchlets; ++matchlet) {
            const int offset = firstOffset + matchlet * 32;
            const int rangeStart = uint32(offset);
            const int rangeLength = uint32(offset + 4);
            const int valueLength = uint32(offset + 12);
            const int maskOffset = uint32(offset + 20);

            if (!matchSubstring(data.constData(), data.size(), rangeStart, rangeLength, valueLength,
                                string(uint32(offset + 16)), maskOffset ? string(maskOffset) : nullptr))
                continue;

            const int numChildren = uint32(offset + 24);
            if (numChildren == 0 || matchRule(numChildren, uint32(offset + 28), data))
                return true;
        }

        return false;
    }

    // QMimeMagicRule::matchSubstring(): the value at any position from rangeStart
    // on, fewer than rangeLength positions later. Qt compares at every position.
    static bool matchSubstring(const char *data, qsizetype dataSize, int rangeStart, int rangeLength,
                               qsizetype valueLength, const char *value, const char *mask)
    {
        if (!mask) {
            if (rangeLength <= 0 || rangeStart + valueLength > dataSize)
                return false;

            const qsizetype end = qMin(dataSize, rangeStart + rangeLength - 1 + valueLength);
            return memmem(data + rangeStart, end - rangeStart, value, valueLength) != nullptr;
        }

        const qsizetype dataNeeded = qMin(rangeLength + valueLength - 1, dataSize - rangeStart);
        const qsizetype maxStartPos = dataNeeded - valueLength + 1;

        for (qsizetype i = 0; i < maxStartPos; ++i) {
            const char *d = data + rangeStart + i;
            qsizetype index = 0;

            while (index < valueLength && (d[index] & mask[index]) == (value[index] & mask[index]))
                ++index;

            if (index == valueLength)
                return true;
        }

        return false;
    }

    QFile m_file;
    const uchar *m_data = nullptr;
    bool m_valid = false;
    std::vector<Glob> m_literals;
    std::vector<Glob> m_globs;
};

// QMimeDatabasePrivate's isTextFile().
bool isTextFile(const QByteArray &data)
{
    if (data.startsWith("\xFE\xFF") || data.startsWith("\xFF\xFE"))
        return true;

    const char *p = data.constData();
    const char *end = p + qMin(qsizetype(128), data.size());
    for (; p < end; ++p) {
        if (static_cast<unsigned char>(*p) < 32 && *p != 9 && *p != 10 && *p != 13)
            return false;
    }

    return true;
}

} // namespace

struct MimeSniffer::Database
{
    // QMimeDatabasePrivate::findByData(): the type and its accuracy.
    std::pair<QString, int> findByData(const QByteArray &data) const
    {
        if (data.isEmpty())
            return {QStringLiteral("application/x-zerosize"), 100};

        int accuracy = 0;
        const char *type = nullptr;
        for (const auto &cache : caches)
            cache->findByMagic(data, &accuracy, &type);

        if (type)
            return {QString::fromLatin1(type), accuracy};

        if (isTextFile(data))
            return {QStringLiteral("text/plain"), 5};

        return {DefaultType, 0};
    }

    QString version;
    // Unset when Qt would match with rules these caches do not hold.
    bool usable = true;
    std::vector<std::unique_ptr<CacheFile>> caches;
    // Per cache; the last one's are never consulted.
    std::vector<TypeOverrides> overrides;
};

MimeSniffer *MimeSniffer::self()
{
    static MimeSniffer sniffer;
    return &sniffer;
}

// Qt rereads the caches at most every five seconds as well.
std::shared_ptr<const MimeSniffer::Database> MimeSniffer::database()
{
    QMutexLocker locker(&m_mutex);

    if (m_database && m_lastCheck.isValid() && m_lastCheck.elapsed() < 5000)
        return m_database;

    m_lastCheck.start();

    const QStringList directories = mimeDirectories();
    const QString version = databaseVersion(directories);
    if (m_database && m_database->version == version)
        return m_database;

    auto database = std::make_shared<Database>();
    database->version = version;

    bool freedesktop = false;
    QStringList cacheDirectories;
    for (const QString &directory : directories) {
        const QDir packages(directory + QStringLiteral("/packages"));
        freedesktop = freedesktop || packages.exists(QStringLiteral("freedesktop.org.xml"));

        auto cache = std::make_unique<CacheFile>(directory + QStringLiteral("/mime.cache"));
        if (cache->isValid()) {
            database->caches.push_back(std::move(cache));
            cacheDirectories.append(directory);
        } else if (!packages.entryList({QStringLiteral("*.xml")}, QDir::Files).isEmpty()) {
            database->usable = false;
        }
    }

    for (qsizetype i = 0; i < cacheDirectories.size(); ++i) {
        if (i + 1 < cacheDirectories.size())
            database->overrides.emplace_back(cacheDirectories.at(i));
        else
            database->overrides.emplace_back();
    }

    // Without the shared database Qt may load one built into it.
    database->usable = database->usable && freedesktop;

    m_database = database;
    return m_database;
}

// QMimeDatabasePrivate::mimeTypeForFileNameAndData().
QMimeType MimeSniffer::mimeTypeForFile(const QString &path)
{
    const QMimeDatabase mimeDatabase;
    const auto current = database();

    if (!current->usable) {
        QFile file(path);
        return mimeDatabase.mimeTypeForFileNameAndData(path, &file);
    }

    NameMatches byName;
    const QString fileName = path.mid(path.lastIndexOf(u'/') + 1);
    if (!fileName.isEmpty()) {
        for (size_t i = 0; i < current->caches.size(); ++i) {
            current->caches[i]->addFileNameMatches(
                    fileName, std::span(current->overrides).first(i), &byName);
        }
    }

    if (byName.all.size() == 1) {
        const QMimeType type = mimeDatabase.mimeTypeForName(byName.matching.constFirst());
        if (type.isValid())
            return type;

        byName = {};
    }

    // Unreadable contents leave the name alone to decide.
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const QByteArray data = file.read(SniffLength);
        file.close();

        const auto [sniffed, accuracy] = current->findByData(data);
        if (accuracy > 0) {
            QString type;

            if (byName.matching.contains(sniffed) || byName.all.isEmpty()) {
                type = sniffed;
            } else {
                for (const QString &candidate : std::as_const(byName.all)) {
                    if (mimeDatabase.mimeTypeForName(candidate).inherits(sniffed)) {
                        type = candidate;
                        break;
                    }
                }
            }

            if (!type.isEmpty()) {
                const QMimeType result = mimeDatabase.mimeTypeForName(type);
                // Qt returns a magic match unchecked; a type it does not know is its call.
                return result.isValid() ? result : mimeDatabase.mimeTypeForFileNameAndData(path, data);
            }
        }
    }

    if (byName.all.size() > 1) {
        byName.matching.sort();
        const QMimeType type = mimeDatabase.mimeTypeForName(byName.matching.constFirst());
        if (type.isValid())
            return type;
    }

    return mimeDatabase.mimeTypeForName(DefaultType);
}

} // namespace Cutefish::IO::Internal
