# Bluetooth

`Cutefish.Bluetooth 1.0` uses Qt DBus to communicate directly with the system
BlueZ service. The shell and settings remain separate processes, each with its
own connection and manager. No KDE Bluetooth library or QML module is required.

The `Manager` singleton exposes service availability, adapter power, hardware
blocking, discovery, adapters, devices, errors, and pairing prompts.
Its `available` property requires a running service and at least one adapter;
powering an adapter off does not make it unavailable. Settings uses this property
to add or remove its Bluetooth navigation entry as hardware or the service changes.
`DevicesModel` supplies sorted device objects, with connected and paired devices
first. Devices are identified by object path, so identical addresses on different
adapters remain distinct.

Device objects support pairing, cancellation, connecting, disconnecting, profile
connections, forgetting, aliases, trust, blocking, RSSI, UUIDs, connection state,
and optional battery percentage (`-1` means unavailable). `mediaCommand()` accepts
the standard BlueZ player commands. Adapter objects expose power, discovery,
discoverability, pairability, and aliases. Successful user-initiated pairing is
followed by setting trust and connecting; a failed pairing never grants trust.

Settings enables the `KeyboardDisplay` agent while its Bluetooth page is visible.
All interactive Agent1 methods require a user response, except display-only
requests, which receive immediate replies while their codes remain visible.
The agent checks the caller's unique bus name, validates PIN/passkey input,
rejects overlapping prompts, and cancels outstanding pairing when the page closes.
Incoming pairing is handled while this page is open; the shell does not claim
the default agent without a pairing UI.

Discovery sessions are owned per adapter and released when the page closes or
search stops. Initial ObjectManager snapshots are reconciled with concurrent
signals. Service disappearance clears objects and prompts; a new service owner
triggers discovery of objects and agent registration again. Replies from a prior
service owner cannot modify the new objects.

Software radio blocking is cleared through `/dev/rfkill` when enabling Bluetooth.
Access failures are shown to the user; a hardware block must be cleared using
the hardware switch. Power-off changes adapter power without changing other
radio types. BlueZ and the host's D-Bus/rfkill access policy must be installed.

## Source layout

- `bluetoothmanager.*`: service lifecycle, state, and asynchronous operations.
- `bluetoothobject.*`: adapter and device properties and actions.
- `bluetoothdevicesmodel.*`: the QML device list and sorting.
- `bluetoothagent.*`: the exported pairing agent and caller validation.
- `bluetoothrfkill.*`: Linux radio-block monitoring and unblocking.
- `bluetoothdbustypes.*`: D-Bus types, serialization, and protocol constants.

## Tests

Backend integration tests use a fake BlueZ service on a private bus:

```sh
cmake -S cutefish-framework/bluetooth/tests -B /tmp/cutefish-bluetooth-tests -G Ninja
cmake --build /tmp/cutefish-bluetooth-tests
ctest --test-dir /tmp/cutefish-bluetooth-tests --output-on-failure
```

Building the framework with `BUILD_TESTING=ON` also adds `bluetooth-qml` when the
sibling settings sources are available. This loads the actual QML module and
Bluetooth page with fake devices and requires an installed FishUI module.
Both test suites require permission to create a local D-Bus socket. They never
operate on the host's Bluetooth service.

Hardware acceptance still requires a BR/EDR headset, an LE device, a keyboard
using passkey entry, and adapter hotplug/rfkill testing. Check discovery, pairing,
audio/input use, reconnecting, forgetting, power cycles, and daemon restarts.
Audio routing and HID operation are provided by BlueZ and the host audio/input
stack; this module manages the Bluetooth connections.

Protocol references: [Adapter1](https://bluez.readthedocs.io/en/latest/adapter-api/),
[Device1](https://bluez.readthedocs.io/en/latest/device-api/),
[Agent1](https://github.com/bluez/bluez/blob/master/doc/org.bluez.Agent.rst).
