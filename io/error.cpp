#include "error.h"
#include <cstring>

namespace Cutefish::IO {
Failure::Failure(int code, const QString &path)
    : std::runtime_error(std::strerror(code))
    , error {code, path, QString::fromLocal8Bit(std::strerror(code))}
{
}

} // namespace Cutefish::IO
