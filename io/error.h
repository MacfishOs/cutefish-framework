#pragma once
#include <QMetaType>
#include <QString>
#include <stdexcept>

namespace Cutefish::IO {
struct Error {
    int code = 0;
    QString path;
    QString message;

    explicit operator bool() const { return code != 0; }
};

class Failure : public std::runtime_error
{
public:
    Failure(int code, const QString &path);
    Error error;
};

} // namespace Cutefish::IO
Q_DECLARE_METATYPE(Cutefish::IO::Error)
