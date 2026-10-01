#ifndef CUTEFISH_APPLICATION_QMLREGISTRATION_H
#define CUTEFISH_APPLICATION_QMLREGISTRATION_H

#include <QtQml/qqmlregistration.h>

#include "windowhints.h"

struct WindowHintsForeign
{
    Q_GADGET
    QML_FOREIGN(Cutefish::WindowHints)
    QML_NAMED_ELEMENT(WindowHints)
    QML_UNCREATABLE("WindowHints is an attached property of Window")
    QML_ATTACHED(Cutefish::WindowHints)
};

#endif
