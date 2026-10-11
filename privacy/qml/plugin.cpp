// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plugin.h"

#include "deviceswitches.h"
#include "deviceusage.h"

#include <QQmlEngine>

void Plugin::registerTypes(const char *uri)
{
    using namespace CutefishPrivacy;

    qmlRegisterSingletonType<DeviceUsage>(uri, 1, 0, "DeviceUsage", [](QQmlEngine *, QJSEngine *) {
        DeviceUsage *usage = DeviceUsage::instance();
        QQmlEngine::setObjectOwnership(usage, QQmlEngine::CppOwnership);
        return usage;
    });

    qmlRegisterSingletonType<DeviceSwitches>(uri, 1, 0, "DeviceSwitches", [](QQmlEngine *, QJSEngine *) {
        DeviceSwitches *switches = DeviceSwitches::instance();
        QQmlEngine::setObjectOwnership(switches, QQmlEngine::CppOwnership);
        return switches;
    });
}
