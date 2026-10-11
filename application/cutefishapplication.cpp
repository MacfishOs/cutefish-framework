#include "cutefishapplication.h"

#include <QCoreApplication>
#include <QGuiApplication>

namespace Cutefish::Application {

void prepare()
{
    QCoreApplication::setOrganizationName(QStringLiteral("cutefish"));
}

int run(int &argc, char *argv[], Entry entry)
{
    prepare();
    QGuiApplication app(argc, argv);
    return entry(argc, argv);
}

} // namespace Cutefish::Application
