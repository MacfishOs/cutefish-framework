#include "cutefishapplication.h"

#include <QCoreApplication>
#include <QQuickStyle>

namespace Cutefish::Application {

void prepare()
{
    QCoreApplication::setOrganizationName(QStringLiteral("cutefish"));
    QQuickStyle::setStyle(QStringLiteral("Fish"));
}

} // namespace Cutefish::Application
