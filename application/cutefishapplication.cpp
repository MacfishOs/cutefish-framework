#include "cutefishapplication.h"

#include <QCoreApplication>

namespace Cutefish::Application {

void prepare()
{
    QCoreApplication::setOrganizationName(QStringLiteral("cutefish"));
}

} // namespace Cutefish::Application
