# Cutefish Framework

Cutefish Framework provides shared system APIs and backend integrations for
Cutefish desktop components and applications.

It provides common access to application metadata, networking, Bluetooth,
audio, display, appearance, accounts, media controls, and other desktop
services.

## Components

The framework provides these CMake targets and C++ aliases:

- `cutefish-framework-application` / `Cutefish::Application`
- `cutefish-framework-accounts` / `Cutefish::Accounts`
- `cutefish-framework-audio` / `Cutefish::Audio`
- `cutefish-framework-appearance` / `Cutefish::Appearance`
- `cutefish-framework-bluetooth` / `Cutefish::Bluetooth`
- `cutefish-framework-capture` / `Cutefish::Capture`
- `cutefish-framework-display` / `Cutefish::Display`
- `cutefish-framework-media` / `Cutefish::Media`
- `cutefish-framework-network` / `Cutefish::Network`
- `cutefish-framework-protocols` / `Cutefish::Protocols`
- `cutefish-framework-screen` / `Cutefish::Screen`

`Cutefish::Protocols` carries the client bindings for the compositor's private
Wayland protocols (`protocols/*.xml`). That XML is kept byte-identical to
`cutefish-compositor/protocols`; other projects link the target instead of
keeping a copy.

`Cutefish::Capture` captures outputs and windows through the compositor's
`ext-image-copy-capture-v1`, into shared memory or DMA-BUFs. The compositor
offers it only to the trusted shell and allowlisted programs.

`Cutefish::Display` is the C++ client for the compositor's
`com.cutefish.DisplayConfig1` D-Bus interface. Use it rather than `QScreen` for
facts about the hardware, such as internal and external panels, mirroring or
power. `QScreen` still decides where a window goes.

The QML modules are `Cutefish.Application`, `Cutefish.Accounts`, `Cutefish.Appearance`,
`Cutefish.Audio`, `Cutefish.Bluetooth`, `Cutefish.Media`,
`Cutefish.Network`, and `Cutefish.Screen`.

The audio module uses native PipeWire with WirePlumber session policy.
See [audio integration and tests](audio/README.md) for details.

## Dependencies

The framework uses Qt 6 and a C++20 compiler. The network module talks to
NetworkManager over D-Bus, and the Bluetooth module to BlueZ.

### Debian/Ubuntu

```shell
sudo apt install cmake pkg-config build-essential \
    qt6-base-dev qt6-declarative-dev qt6-tools-dev qt6-tools-dev-tools \
    network-manager bluez \
    libpipewire-0.3-dev libspa-0.2-dev pipewire wireplumber
```

### Arch Linux

```shell
sudo pacman -S --needed base-devel cmake pkgconf \
    qt6-base qt6-declarative networkmanager bluez \
    pipewire wireplumber
```

## Build and Install

```
mkdir build
cd build
cmake ..
make
sudo make install
```

## License

This project has been licensed by GPLv3.

## Window presentation

Set `WindowHints.skipTaskbar` to exclude a window from the dock, status bar
application identity, and window switcher:

```qml
import Cutefish.Application

Window {
    WindowHints.skipTaskbar: true
}
```
