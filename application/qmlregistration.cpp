#include "qmlregistration.h"

#include <QQmlEngine>

ApplicationRegistry *ApplicationRegistryForeign::create(QQmlEngine *engine, QJSEngine *)
{
    auto *registry = ApplicationRegistry::instance();
    Q_ASSERT(registry->thread() == engine->thread());
    QQmlEngine::setObjectOwnership(registry, QQmlEngine::CppOwnership);
    return registry;
}

ApplicationRuntime *ApplicationRuntimeForeign::create(QQmlEngine *engine, QJSEngine *)
{
    auto *runtime = ApplicationRuntime::instance();
    Q_ASSERT(runtime->thread() == engine->thread());
    QQmlEngine::setObjectOwnership(runtime, QQmlEngine::CppOwnership);
    return runtime;
}
