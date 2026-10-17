// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cutefish/upower/battery_status.h"

#include <algorithm>

#include "base/event_loop.h"
#include "cutefish/upower/device.h"

using namespace cutefish::base::dbus;

namespace cutefish::upower {

static constexpr const char* kService = "com.cutefish.Services";
static constexpr const char* kPath = "/com/cutefish/Services/Battery";
static constexpr const char* kInterface = "com.cutefish.Services.Battery";

static constexpr const char* kUPowerService = "org.freedesktop.UPower";
static constexpr const char* kUPowerPath = "/org/freedesktop/UPower";

std::shared_ptr<BatteryStatus> BatteryStatus::shared()
{
    thread_local std::weak_ptr<BatteryStatus> instance;
    thread_local base::EventLoop* instanceLoop = nullptr;

    base::EventLoop* loop = base::EventLoop::current();
    std::shared_ptr<BatteryStatus> status = instance.lock();
    if (!status || instanceLoop != loop) {
        status = std::make_shared<BatteryStatus>();
        instance = status;
        instanceLoop = loop;
    }
    return status;
}

BatteryStatus::BatteryStatus(Connection* sessionBus, Connection* systemBus)
    : m_service(sessionBus, kService, kPath, kInterface)
    , m_upower(systemBus, kUPowerService, kUPowerPath, kUPowerService)
{
    // The signals only say that something moved; one GetAll brings every value up to date.
    for (const char* signal : {"chargeStateChanged", "chargePercentChanged", "capacityChanged",
                               "remainingTimeChanged", "lastChargedPercentChanged", "lastChargedSecsChanged"})
        m_matches.push_back(m_service.onSignal(signal, [this](const Message&) { fetchState(); }));

    m_matches.push_back(m_upower.onPropertiesChanged(
            [this](const VariantMap& changed, const std::vector<std::string>& invalidated) {
                if (const auto it = changed.find("OnBattery"); it != changed.end())
                    setOnBattery(it->second.value<bool>());
                else if (std::find(invalidated.begin(), invalidated.end(), "OnBattery") != invalidated.end())
                    fetchOnBattery();
            }));

    fetchState();
    fetchOnBattery();
}

BatteryStatus::~BatteryStatus() = default;

void BatteryStatus::refresh()
{
    m_service.send("refresh");
}

void BatteryStatus::fetchState()
{
    m_stateCall = m_service.getAllAsync([this](const Reply& reply) {
        if (const auto properties = reply.value<VariantMap>()) {
            applyState(*properties);
            if (!m_available) {
                m_available = true;
                availableChanged.emit();
            }
        }
        if (!m_ready) {
            m_ready = true;
            readyChanged.emit();
        }
    });
}

void BatteryStatus::applyState(const VariantMap& properties)
{
    const auto integer = [&properties](const char* name, int64_t fallback) {
        const auto it = properties.find(name);
        return it == properties.end() ? fallback : integerValue(it->second, fallback);
    };
    const auto string = [&properties](const char* name, const std::string& fallback) {
        const auto it = properties.find(name);
        return it == properties.end() ? fallback : it->second.value<std::string>(fallback);
    };

    const int chargeState = int(integer("chargeState", m_chargeState));
    const int chargePercent = int(integer("chargePercent", m_chargePercent));
    const int capacity = int(integer("capacity", m_capacity));
    const std::string statusString = string("statusString", m_statusString);
    const int lastChargedPercent = int(integer("lastChargedPercent", m_lastChargedPercent));
    const int64_t lastChargedSecs = integer("lastChargedSecs", m_lastChargedSecs);
    const std::string udi = string("udi", m_udi);

    if (m_chargeState != chargeState) {
        m_chargeState = chargeState;
        chargeStateChanged.emit();
    }
    if (m_chargePercent != chargePercent) {
        m_chargePercent = chargePercent;
        chargePercentChanged.emit();
    }
    if (m_capacity != capacity) {
        m_capacity = capacity;
        capacityChanged.emit();
    }
    if (m_statusString != statusString) {
        m_statusString = statusString;
        statusStringChanged.emit();
    }
    if (m_lastChargedPercent != lastChargedPercent || m_lastChargedSecs != lastChargedSecs) {
        m_lastChargedPercent = lastChargedPercent;
        m_lastChargedSecs = lastChargedSecs;
        lastChargedChanged.emit();
    }
    if (m_udi != udi) {
        m_udi = udi;
        udiChanged.emit();
    }
}

void BatteryStatus::fetchOnBattery()
{
    m_onBatteryCall = m_upower.getPropertyAsync("OnBattery", [this](const Reply& reply) {
        if (const auto value = reply.value<Variant>())
            setOnBattery(value->value<bool>());
    });
}

void BatteryStatus::setOnBattery(bool onBattery)
{
    if (onBattery == m_onBattery)
        return;

    m_onBattery = onBattery;
    // The service's figures depend on the power source.
    refresh();
    onBatteryChanged.emit();
}

} // namespace cutefish::upower
