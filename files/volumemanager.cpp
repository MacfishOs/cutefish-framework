// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "files/volumemanager.h"

#include <chrono>

#include "base/event_loop.h"
#include "base/locale.h"
#include "pathutils_p.h"

namespace cutefish::files {

using namespace cutefish::base;
using namespace cutefish::base::dbus;
using namespace std::chrono_literals;

static const std::string service = "org.freedesktop.UDisks2";
static const std::string root = "/org/freedesktop/UDisks2";
static const std::string blockInterface = service + ".Block";
static const std::string driveInterface = service + ".Drive";
static const std::string filesystemInterface = service + ".Filesystem";

static std::string decodePath(const std::vector<uint8_t>& bytes)
{
    std::string path(bytes.begin(), bytes.end());
    if (!path.empty() && path.back() == '\0')
        path.pop_back();

    return path;
}

template<typename T>
static T value(const VariantMap& map, const std::string& key, T fallback = T())
{
    const auto found = map.find(key);
    if (found == map.cend())
        return fallback;

    // Values arrive inside a variant.
    const Variant& variant = found->second;
    if (const auto direct = variant.get<T>())
        return *direct;
    if (const auto inner = variant.get<Variant>())
        return inner->get<T>().value_or(fallback);
    return fallback;
}

VolumeManager::VolumeManager(Connection* bus)
    : m_bus(bus)
{
    if (!m_bus)
        return;

    const auto refreshOnSignal = [this](const Message&) {
        refresh();
    };
    m_matches.push_back(m_bus->subscribe(
        {service, root, {}, "org.freedesktop.DBus.ObjectManager", "InterfacesAdded", {}}, refreshOnSignal));
    m_matches.push_back(m_bus->subscribe(
        {service, root, {}, "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved", {}}, refreshOnSignal));
    m_matches.push_back(m_bus->subscribe(
        {service, {}, {}, "org.freedesktop.DBus.Properties", "PropertiesChanged", {}}, refreshOnSignal));

    m_ownerWatch = m_bus->watchName(service, [this](const std::string& owner) {
        // The first answer is the owner as of now, which the first refresh reads.
        if (!m_ownerKnown) {
            m_ownerKnown = true;
            return;
        }

        ++m_generation;
        m_objects.clear();
        rebuild();

        if (!owner.empty())
            refresh();
    });

    refresh();
}

VolumeManager::~VolumeManager() = default;

std::vector<Volume> VolumeManager::volumes() const
{
    std::vector<Volume> result;
    result.reserve(m_volumes.size());
    for (const auto& [id, volume] : m_volumes)
        result.push_back(volume);
    return result;
}

Volume VolumeManager::volume(const std::string& id) const
{
    const auto found = m_volumes.find(id);
    return found == m_volumes.cend() ? Volume() : found->second;
}

void VolumeManager::release(uint64_t call)
{
    EventLoop::current()->post([self = m_weakFactory.getWeakPtr(), call] {
        if (VolumeManager* manager = self.get())
            manager->m_calls.erase(call);
    });
}

void VolumeManager::refresh()
{
    if (!m_bus)
        return;

    if (m_refreshing) {
        m_refreshAgain = true;
        return;
    }

    m_refreshing = true;
    const uint64_t generation = m_generation;
    const uint64_t id = m_nextCall++;
    const Proxy manager(m_bus, service, root, "org.freedesktop.DBus.ObjectManager");

    m_calls[id] = manager.callAsync("GetManagedObjects", [this, generation, id](const Reply& reply) {
        release(id);
        m_refreshing = false;

        if (generation == m_generation) {
            if (reply.isError()) {
                errorOccurred.emit(reply.error().message);
            } else {
                m_objects = reply.value<Objects>().value_or(Objects());
                rebuild();
            }
        }

        if (m_refreshAgain) {
            m_refreshAgain = false;
            refresh();
        }
    });
}

void VolumeManager::rebuild()
{
    std::map<std::string, Volume> next;

    for (const auto& [path, interfaces] : m_objects) {
        const auto blockFound = interfaces.find(blockInterface);
        if (blockFound == interfaces.cend())
            continue;

        const VariantMap& block = blockFound->second;
        if (value<bool>(block, "HintIgnore"))
            continue;

        Volume v;
        v.id = path.path;
        v.drive = value<ObjectPath>(block, "Drive").path;

        VariantMap drive;
        const auto driveObject = m_objects.find(ObjectPath{v.drive.empty() ? std::string("/") : v.drive});
        if (driveObject != m_objects.cend()) {
            if (const auto found = driveObject->second.find(driveInterface); found != driveObject->second.cend())
                drive = found->second;
        }

        v.device = decodePath(value<std::vector<uint8_t>>(block, "PreferredDevice"));
        v.size = value<uint64_t>(block, "Size");
        v.filesystem = interfaces.contains(filesystemInterface);
        v.optical = value<bool>(drive, "Optical");
        v.removable = value<bool>(drive, "Removable") || value<std::string>(drive, "ConnectionBus") == "usb";
        v.ejectable = value<bool>(drive, "Ejectable");
        v.canPowerOff = value<bool>(drive, "CanPowerOff");

        v.name = value<std::string>(block, "HintName");
        if (v.name.empty())
            v.name = value<std::string>(block, "IdLabel");
        if (v.name.empty())
            v.name = v.size ? Locale::system().formattedDataSize(int64_t(v.size)) + " Volume"
                            : Internal::fileNameOf(v.device);

        v.icon = value<std::string>(block, "HintIconName");
        if (v.icon.empty())
            v.icon = v.optical ? "drive-optical" : v.removable ? "drive-removable-media" : "drive-harddisk";

        if (const auto filesystem = interfaces.find(filesystemInterface); filesystem != interfaces.cend()) {
            for (const auto& point : value<std::vector<std::vector<uint8_t>>>(filesystem->second, "MountPoints")) {
                const std::string mountPoint = decodePath(point);
                if (!mountPoint.empty())
                    v.mountPoints.push_back(mountPoint);
            }
        }

        next.emplace(v.id, v);
    }

    const auto previous = std::exchange(m_volumes, next);

    for (const auto& [id, volume] : previous) {
        if (!next.contains(id))
            volumeRemoved.emit(id);
    }

    for (const auto& [id, volume] : next) {
        const auto old = previous.find(id);
        if (old == previous.cend())
            volumeAdded.emit(id);
        else if (!(old->second == volume))
            volumeChanged.emit(id);
    }
}

void VolumeManager::mount(const std::string& id)
{
    call(id, "mount", id, filesystemInterface, "Mount");
}

void VolumeManager::unmount(const std::string& id)
{
    call(id, "unmount", id, filesystemInterface, "Unmount");
}

void VolumeManager::eject(const std::string& id)
{
    const auto v = volume(id);
    call(id, "eject", v.drive, driveInterface, v.ejectable ? "Eject" : "PowerOff");
}

void VolumeManager::call(const std::string& id, const std::string& operation, const std::string& path,
                         const std::string& interface, const std::string& method)
{
    const auto v = volume(id);
    if (v.id.empty() || (operation == "eject" ? !(v.ejectable || v.canPowerOff) : !v.filesystem)) {
        operationFailed.emit(id, operation, "The device does not support this operation.");
        return;
    }

    bool busy = m_pending.contains(id);
    for (const auto& pending : m_pending)
        busy = busy || (v.drive != "/" && !v.drive.empty() && volume(pending).drive == v.drive);

    if (busy) {
        operationFailed.emit(id, operation, "A device operation is already in progress.");
        return;
    }

    m_pending.insert(id);

    std::vector<std::string> unmounts;
    if (operation == "eject") {
        for (const auto& [otherId, other] : m_volumes) {
            if (other.drive == v.drive && !other.mountPoints.empty())
                unmounts.push_back(other.id);
        }
    }

    perform(id, operation, path, interface, method, unmounts, m_generation);
}

void VolumeManager::perform(const std::string& id, const std::string& operation, const std::string& path,
                            const std::string& interface, const std::string& method, std::vector<std::string> unmounts,
                            uint64_t generation)
{
    const bool unmounting = !unmounts.empty();
    const std::string target = unmounting ? unmounts.front() : path;
    if (unmounting)
        unmounts.erase(unmounts.begin());

    Proxy proxy(m_bus, service, target, unmounting ? filesystemInterface : interface);
    proxy.setTimeout(120s);
    const uint64_t callId = m_nextCall++;

    m_calls[callId] = proxy.callAsync(
        unmounting ? "Unmount" : method,
        [this, callId, id, operation, path, interface, method, unmounts, unmounting, generation](const Reply& reply) {
            release(callId);

            if (generation != m_generation || (operation != "eject" && !m_volumes.contains(id))) {
                m_pending.erase(id);
                operationFailed.emit(id, operation, "The device is no longer available.");
                return;
            }

            if (reply.isError()) {
                m_pending.erase(id);
                operationFailed.emit(id, operation, reply.error().message);
            } else if (unmounting) {
                perform(id, operation, path, interface, method, unmounts, generation);
                return;
            } else {
                m_pending.erase(id);

                std::string mountPoint;
                if (operation == "mount")
                    mountPoint = reply.value<std::string>().value_or(std::string());

                operationFinished.emit(id, operation, mountPoint);
            }

            refresh();
        },
        VariantMap());
}

} // namespace cutefish::files
