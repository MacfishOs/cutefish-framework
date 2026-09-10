#pragma once
#include <QUrl>

namespace Cutefish::IO {
class FileSystemUrl
{
public:
    explicit FileSystemUrl(QUrl url);

    const QUrl &url() const { return m_url; }

    QString localPath() const;
    static QString trashRoot();

private:
    QUrl m_url;
};

QUrl upUrl(const QUrl &url);
} // namespace Cutefish::IO
