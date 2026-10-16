# Cutefish Framework

Shared system APIs for Cutefish components and applications, on the
[cutefish-toolkit](https://github.com/cutefishos/cutefish-toolkit) base
library:

- `clients/`: clients of system services (accounts, appearance, audio,
  BlueZ, display, NetworkManager, power profiles, time and date, UPower), one
  library each, in the `CutefishClients` package.
- `files/`: file listing, MIME types, operations, trash, volumes and
  thumbnails, in the `CutefishFiles` package.
- `protocols/`: client bindings for the compositor's private Wayland
  protocols, in the `CutefishFramework` package.

## Dependencies

### Debian/Ubuntu

```shell
sudo apt install cmake pkg-config build-essential \
    libwayland-dev wayland-protocols libicu-dev libpng-dev \
    libpipewire-0.3-dev libspa-0.2-dev libgtest-dev
```

### Arch Linux

```shell
sudo pacman -S --needed base-devel cmake pkgconf \
    wayland wayland-protocols icu libpng pipewire gtest
```

and [cutefish-toolkit](https://github.com/cutefishos/cutefish-toolkit).

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
