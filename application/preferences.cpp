#include "preferences.h"

#include <QCoreApplication>
#include <QStandardPaths>

QString Preferences::file(const QString &process)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QLatin1String("/cutefish/") + process + QLatin1String(".conf");
}

QString Preferences::ownFile()
{
    QString process = QCoreApplication::applicationName();
    if (process.startsWith(QLatin1String("cutefish-")))
        process.remove(0, 9);
    return file(process);
}
