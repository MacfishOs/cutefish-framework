#pragma once

#include <QUrl>

namespace Cutefish::IO {

// trash:/ lists every trash. trash:/<id>/<name> is an item at the top of one: id is
// "home" for the home trash, else it names a mounted volume's trash.
class FileSystemUrl
{
public:
    explicit FileSystemUrl(QUrl url);

    const QUrl &url() const
    {
        return m_url;
    }

    QString localPath() const;
    static QString trashRoot();

private:
    QUrl m_url;
};

QUrl upUrl(const QUrl &url);

} // namespace Cutefish::IO
