# Cutefish Framework

Shared system APIs for Cutefish components and applications: applications,
accounts, audio, appearance, Bluetooth, display, media, network, power, touchpad
scrolling and more, as C++ libraries and QML modules.

## Dependencies

### Debian/Ubuntu

```shell
sudo apt install cmake pkg-config build-essential \
    qt6-base-dev qt6-declarative-dev qt6-tools-dev qt6-tools-dev-tools \
    network-manager bluez libwayland-dev wayland-protocols \
    libpipewire-0.3-dev libspa-0.2-dev pipewire wireplumber
```

### Arch Linux

```shell
sudo pacman -S --needed base-devel cmake pkgconf \
    qt6-base qt6-declarative networkmanager bluez wayland wayland-protocols \
    pipewire wireplumber
```

## Build and Install

```shell
mkdir build
cd build
cmake ..
make
sudo make install
```

## License

This project has been licensed by GPLv3.
