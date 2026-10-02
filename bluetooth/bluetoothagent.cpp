#include "bluetoothagent.h"
#include "bluetoothmanager.h"
#include <QDBusError>

BluetoothAgent::BluetoothAgent(BluetoothManager *manager)
    : QDBusVirtualObject(manager)
    , m_manager(manager)
{
}

QString BluetoothAgent::introspect(const QString &) const
{
    return QStringLiteral(
            "<interface name=\"org.bluez.Agent1\">"
            "<method name=\"Release\"/>"
            "<method name=\"Cancel\"/>"
            "<method name=\"RequestPinCode\"><arg type=\"o\" direction=\"in\"/><arg type=\"s\" "
            "direction=\"out\"/></method>"
            "<method name=\"RequestPasskey\"><arg type=\"o\" direction=\"in\"/><arg type=\"u\" "
            "direction=\"out\"/></method>"
            "<method name=\"DisplayPinCode\"><arg type=\"o\" direction=\"in\"/><arg type=\"s\" "
            "direction=\"in\"/></method>"
            "<method name=\"DisplayPasskey\"><arg type=\"o\" direction=\"in\"/><arg type=\"u\" "
            "direction=\"in\"/><arg type=\"q\" direction=\"in\"/></method>"
            "<method name=\"RequestConfirmation\"><arg type=\"o\" direction=\"in\"/><arg "
            "type=\"u\" direction=\"in\"/></method>"
            "<method name=\"RequestAuthorization\"><arg type=\"o\" direction=\"in\"/></method>"
            "<method name=\"AuthorizeService\"><arg type=\"o\" direction=\"in\"/><arg "
            "type=\"s\" direction=\"in\"/></method>"
            "</interface>");
}

bool BluetoothAgent::handleMessage(const QDBusMessage &message, const QDBusConnection &connection)
{
    if (message.interface() != QStringLiteral("org.bluez.Agent1"))
        return false;
    if (message.service() != m_manager->m_owner || !m_manager->m_agentEnabled) {
        connection.send(message.createErrorReply(QStringLiteral("org.bluez.Error.Rejected"),
                                                 QStringLiteral("Agent unavailable")));
        return true;
    }
    const QMap<QString, QString> signatures {{"Release", ""},
                                             {"Cancel", ""},
                                             {"RequestPinCode", "o"},
                                             {"RequestPasskey", "o"},
                                             {"DisplayPinCode", "os"},
                                             {"DisplayPasskey", "ouq"},
                                             {"RequestConfirmation", "ou"},
                                             {"RequestAuthorization", "o"},
                                             {"AuthorizeService", "os"}};
    if (!signatures.contains(message.member()))
        return false;
    if (message.signature() != signatures.value(message.member())) {
        connection.send(message.createErrorReply(QDBusError::InvalidArgs,
                                                 QStringLiteral("Invalid agent arguments")));
        return true;
    }
    if (message.member() == QStringLiteral("Cancel")
        || message.member() == QStringLiteral("Release")) {
        m_manager->finishPrompt();
        if (message.member() == QStringLiteral("Release")) {
            m_manager->m_agentReady = false;
            emit m_manager->changed();
        }
        connection.send(message.createReply());
    } else {
        m_manager->prompt(message);
    }
    return true;
}
