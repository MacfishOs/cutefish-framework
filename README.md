# Cutefish Framework

Cutefish Framework provides shared system APIs and backend integrations for
Cutefish desktop components and applications.

It provides common access to application metadata, networking, Bluetooth,
audio, display, appearance, accounts, media controls, and other desktop
services.

## Components

The framework provides these CMake targets and C++ aliases:

- `cutefish-framework-applications` / `Cutefish::Applications`
- `cutefish-framework-accounts` / `Cutefish::Accounts`
- `cutefish-framework-audio` / `Cutefish::Audio`
- `cutefish-framework-appearance` / `Cutefish::Appearance`
- `cutefish-framework-bluetooth` / `Cutefish::Bluetooth`
- `cutefish-framework-media` / `Cutefish::Media`
- `cutefish-framework-network` / `Cutefish::Network`
- `cutefish-framework-screen` / `Cutefish::Screen`

The QML modules are `Cutefish.Accounts`, `Cutefish.Appearance`,
`Cutefish.Audio`, `Cutefish.Bluetooth`, `Cutefish.Media`,
`Cutefish.Network`, and `Cutefish.Screen`.

## Dependencies

The framework uses Qt 6 and a C++20 compiler. NetworkManagerQt is the only
KDE Frameworks dependency; it is used by the network module.

### Debian/Ubuntu

```shell
sudo apt install cmake pkg-config build-essential \
    qt6-base-dev qt6-declarative-dev qt6-tools-dev qt6-tools-dev-tools \
    libkf6networkmanagerqt-dev bluez \
    libcanberra-dev libpulse-dev \
    sound-theme-freedesktop
```

### Arch Linux

```shell
sudo pacman -S --needed base-devel cmake pkgconf \
    qt6-base qt6-declarative bluez networkmanager-qt \
    libcanberra libpulse sound-theme-freedesktop
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
