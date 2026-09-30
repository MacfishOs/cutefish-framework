#pragma once

#include <QMetaType>
#include <QString>

namespace Cutefish::IO {

struct TaskProgress
{
    Q_GADGET
    Q_PROPERTY(bool preparing MEMBER preparing)
    Q_PROPERTY(bool totalsKnown MEMBER totalsKnown)
    Q_PROPERTY(quint64 processedBytes MEMBER processedBytes)
    Q_PROPERTY(quint64 totalBytes MEMBER totalBytes)
    Q_PROPERTY(quint64 processedItems MEMBER processedItems)
    Q_PROPERTY(quint64 totalItems MEMBER totalItems)
    Q_PROPERTY(qint64 elapsedMs MEMBER elapsedMs)
    Q_PROPERTY(QString currentPath MEMBER currentPath)

public:
    bool preparing = true;
    bool totalsKnown = false;
    quint64 processedBytes = 0;
    quint64 totalBytes = 0;
    quint64 processedItems = 0;
    quint64 totalItems = 0;
    qint64 elapsedMs = 0;
    QString currentPath;
};

} // namespace Cutefish::IO

Q_DECLARE_METATYPE(Cutefish::IO::TaskProgress)
