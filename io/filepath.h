#pragma once
#include <QByteArray>

namespace Cutefish::IO {
class FilePath
{
public:
    explicit FilePath(QByteArray bytes);

    const QByteArray &bytes() const { return m_bytes; }

    FilePath parent() const;
    FilePath child(const QByteArray &name) const;

private:
    QByteArray m_bytes;
};

} // namespace Cutefish::IO
