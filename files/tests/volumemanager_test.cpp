// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <future>
#include <systemd/sd-bus.h>
#include <memory>
#include <thread>

#include "base/event_loop.h"
#include "files/volumemanager.h"
#include "testing.h"

using namespace cutefish;
using namespace cutefish::base;
using namespace cutefish::base::dbus;
using namespace cutefish::files;
using namespace std::chrono_literals;

namespace {

const std::string service = "org.freedesktop.UDisks2";
const std::string root = "/org/freedesktop/UDisks2";
const std::string disk = root + "/block_devices/sdb1";
const std::string drive = root + "/drives/usb";

std::vector<uint8_t> bytes(const std::string& text)
{
    return std::vector<uint8_t>(text.begin(), text.end());
}

// UDisks2 on the session bus, on a thread of its own.
class FakeUDisks
{
public:
    FakeUDisks()
    {
        objects[ObjectPath{drive}][service + ".Drive"] = {
            {"Removable", Variant(true)}, {"Ejectable", Variant(true)}, {"Optical", Variant(true)}};
        objects[ObjectPath{disk}][service + ".Block"] = {
            {"Drive", Variant(ObjectPath{drive})},
            {"PreferredDevice", Variant(bytes(std::string("/dev/sdb1\0", 10)))},
            {"IdLabel", Variant(std::string("USB"))},
            {"Size", Variant(uint64_t(4096))}};
        objects[ObjectPath{disk}][service + ".Filesystem"] = {
            {"MountPoints", Variant(std::vector<std::vector<uint8_t>>())}};

        std::promise<bool> ready;
        m_thread = std::thread([this, &ready] {
            run(ready);
        });
        m_ready = ready.get_future().get();
    }

    ~FakeUDisks()
    {
        m_handle.post([this] {
            m_loop->quit();
        });
        m_thread.join();
    }

    bool isReady() const { return m_ready; }

    template<typename F>
    auto inService(F f)
    {
        std::promise<decltype(f())> result;
        m_handle.post([&] {
            if constexpr (std::is_void_v<decltype(f())>) {
                f();
                result.set_value();
            } else {
                result.set_value(f());
            }
        });
        return result.get_future().get();
    }

    VolumeManager::Objects objects;
    std::vector<std::string> calls;
    bool fail = false;
    std::unique_ptr<Connection> connection;

private:
    void answer(MethodCall& call, const std::string& member, bool mount)
    {
        calls.push_back(member);

        if (call.message().signature() != "a{sv}")
            call.replyError("org.freedesktop.DBus.Error.InvalidArgs", "Expected options map");
        else if (fail)
            call.replyError("org.freedesktop.UDisks2.Error.NotAuthorized", "Permission denied");
        else if (mount)
            call.reply(std::string("/media/USB Café"));
        else
            call.reply();
    }

    void run(std::promise<bool>& ready)
    {
        EventLoop loop;
        m_loop = &loop;
        m_handle = loop.handle();
        connection = Connection::open(Connection::Bus::Session, &loop);
        if (!connection || !connection->requestName(service)) {
            ready.set_value(false);
            return;
        }

        // sd-bus keeps ObjectManager to itself; a callback for the path answers first.
        sd_bus_slot* managerSlot = nullptr;
        const auto getManagedObjects = [](sd_bus_message* raw, void* data, sd_bus_error*) -> int {
            const Message message(raw);
            if (message.interface() != "org.freedesktop.DBus.ObjectManager" || message.member() != "GetManagedObjects")
                return 0;
            MethodCall call(message);
            call.reply(static_cast<FakeUDisks*>(data)->objects);
            return 1;
        };
        const bool managed = sd_bus_add_object(connection->bus(), &managerSlot, root.c_str(), getManagedObjects, this) >= 0;
        Slot manager(managerSlot);

        auto filesystem = std::make_unique<Object>(connection.get(), disk, service + ".Filesystem");
        filesystem->addMethod("Mount", "a{sv}", "s", [this](MethodCall& call) {
            answer(call, "Mount", true);
        });
        filesystem->addMethod("Unmount", "a{sv}", "", [this](MethodCall& call) {
            answer(call, "Unmount", false);
        });

        auto driveObject = std::make_unique<Object>(connection.get(), drive, service + ".Drive");
        driveObject->addMethod("Eject", "a{sv}", "", [this](MethodCall& call) {
            answer(call, "Eject", false);
        });
        driveObject->addMethod("PowerOff", "a{sv}", "", [this](MethodCall& call) {
            answer(call, "PowerOff", false);
        });

        ready.set_value(managed && filesystem->publish() && driveObject->publish());
        loop.run();
        manager.reset();
        filesystem.reset();
        driveObject.reset();
        connection.reset();
    }

    std::thread m_thread;
    EventLoop* m_loop = nullptr;
    LoopHandle m_handle;
    bool m_ready = false;
};

} // namespace

TEST(VolumeManagerTest, LifecycleAndOperations)
{
    EventLoop loop;
    Connection* bus = Connection::session(&loop);
    if (!bus)
        GTEST_SKIP() << "No session bus; run the tests in dbus-run-session";

    FakeUDisks fake;
    ASSERT_TRUE(fake.isReady());

    VolumeManager manager(bus);
    std::vector<std::string> added, changed, removed;
    std::vector<std::tuple<std::string, std::string, std::string>> done, errors;
    const Subscription s1 = manager.volumeAdded.connect([&](const std::string& id) { added.push_back(id); });
    const Subscription s2 = manager.volumeChanged.connect([&](const std::string& id) { changed.push_back(id); });
    const Subscription s3 = manager.volumeRemoved.connect([&](const std::string& id) { removed.push_back(id); });
    const Subscription s4 = manager.operationFinished.connect(
        [&](const std::string& id, const std::string& operation, const std::string& point) {
            done.emplace_back(id, operation, point);
        });
    const Subscription s5 = manager.operationFailed.connect(
        [&](const std::string& id, const std::string& operation, const std::string& message) {
            errors.emplace_back(id, operation, message);
        });

    using files::testing::waitFor;

    ASSERT_TRUE(waitFor(loop, [&] { return added.size() == 1; }));
    EXPECT_EQ(manager.volume(disk).device, "/dev/sdb1");
    EXPECT_EQ(manager.volume(disk).name, "USB");
    EXPECT_TRUE(manager.volume(disk).optical);

    manager.mount(disk);
    ASSERT_TRUE(waitFor(loop, [&] { return done.size() == 1; }));
    EXPECT_EQ(std::get<2>(done.back()), "/media/USB Café");

    fake.inService([&] { fake.fail = true; });
    manager.unmount(disk);
    ASSERT_TRUE(waitFor(loop, [&] { return errors.size() == 1; }));
    EXPECT_EQ(done.size(), 1u);

    fake.inService([&] {
        fake.fail = false;
        auto& filesystem = fake.objects[ObjectPath{disk}][service + ".Filesystem"];
        filesystem["MountPoints"] = Variant(std::vector<std::vector<uint8_t>>{bytes(std::string("/media/USB Café") + '\0')});
        fake.connection->emitSignal(disk, "org.freedesktop.DBus.Properties", "PropertiesChanged",
                                    service + ".Filesystem", filesystem, std::vector<std::string>());
    });
    ASSERT_TRUE(waitFor(loop, [&] { return !changed.empty(); }));
    EXPECT_EQ(manager.volume(disk).mountPoints, std::vector<std::string>{"/media/USB Café"});

    fake.inService([&] { fake.calls.clear(); });
    manager.eject(disk);
    ASSERT_TRUE(waitFor(loop, [&] { return done.size() == 2; }));
    EXPECT_EQ(fake.inService([&] { return fake.calls; }), (std::vector<std::string>{"Unmount", "Eject"}));

    fake.inService([&] {
        fake.fail = true;
        fake.calls.clear();
    });
    manager.eject(disk);
    ASSERT_TRUE(waitFor(loop, [&] { return errors.size() == 2; }));
    EXPECT_EQ(fake.inService([&] { return fake.calls; }), (std::vector<std::string>{"Unmount"}));

    manager.mount("/missing");
    EXPECT_EQ(errors.size(), 3u);

    fake.inService([&] {
        fake.fail = false;
        auto& driveProperties = fake.objects[ObjectPath{drive}][service + ".Drive"];
        driveProperties["Ejectable"] = Variant(false);
        driveProperties["CanPowerOff"] = Variant(true);
        fake.connection->emitSignal(drive, "org.freedesktop.DBus.Properties", "PropertiesChanged", service + ".Drive",
                                    driveProperties, std::vector<std::string>());
    });
    ASSERT_TRUE(waitFor(loop, [&] { return manager.volume(disk).canPowerOff; }));

    fake.inService([&] { fake.calls.clear(); });
    manager.eject(disk);
    manager.mount(disk);
    EXPECT_EQ(errors.size(), 4u);
    ASSERT_TRUE(waitFor(loop, [&] { return done.size() == 3; }));
    EXPECT_EQ(fake.inService([&] { return fake.calls; }), (std::vector<std::string>{"Unmount", "PowerOff"}));

    fake.inService([&] { fake.connection->releaseName(service); });
    ASSERT_TRUE(waitFor(loop, [&] { return removed.size() == 1; }));
    EXPECT_TRUE(manager.volumes().empty());

    fake.inService([&] { fake.connection->requestName(service); });
    ASSERT_TRUE(waitFor(loop, [&] { return added.size() == 2; }));

    fake.inService([&] {
        fake.objects.erase(ObjectPath{disk});
        fake.connection->emitSignal(root, "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved", ObjectPath{disk},
                                    std::vector<std::string>{service + ".Block", service + ".Filesystem"});
    });
    ASSERT_TRUE(waitFor(loop, [&] { return removed.size() == 2; }));
    EXPECT_TRUE(manager.volumes().empty());
}
