#ifndef PREFERENCES_H
#define PREFERENCES_H

#include <QString>

// Each process keeps its preferences in ~/.config/cutefish/<process>.conf, named without the
// "cutefish-" prefix, and is the only one that writes it. Others read it and ask the owner over
// D-Bus to change it.
namespace Preferences
{
QString file(const QString &process);

// The calling process's own file, after QCoreApplication::applicationName().
QString ownFile();
}

#endif
