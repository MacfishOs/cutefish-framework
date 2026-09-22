#ifndef CUTEFISH_APPLICATION_QMLREGISTRATION_H
#define CUTEFISH_APPLICATION_QMLREGISTRATION_H

#include <QtQml/qqmlregistration.h>

#include "applicationregistry.h"
#include "applicationruntime.h"
#include "desktopentry.h"
#include "windowhints.h"

class QQmlEngine;
class QJSEngine;

struct ApplicationRegistryForeign
{
    Q_GADGET
    QML_FOREIGN(ApplicationRegistry)
    QML_NAMED_ELEMENT(DesktopEntries)
    QML_SINGLETON

public:
    static ApplicationRegistry *create(QQmlEngine *engine, QJSEngine *);
};

struct ApplicationRuntimeForeign
{
    Q_GADGET
    QML_FOREIGN(ApplicationRuntime)
    QML_NAMED_ELEMENT(AppRuntime)
    QML_SINGLETON

public:
    static ApplicationRuntime *create(QQmlEngine *engine, QJSEngine *);
};

struct DesktopEntryForeign
{
    Q_GADGET
    QML_FOREIGN(DesktopEntry)
    QML_NAMED_ELEMENT(DesktopEntry)
    QML_UNCREATABLE("DesktopEntry objects are provided by DesktopEntries")
};

struct WindowHintsForeign
{
    Q_GADGET
    QML_FOREIGN(Cutefish::WindowHints)
    QML_NAMED_ELEMENT(WindowHints)
    QML_UNCREATABLE("WindowHints is an attached property of Window")
    QML_ATTACHED(Cutefish::WindowHints)
};

#endif
