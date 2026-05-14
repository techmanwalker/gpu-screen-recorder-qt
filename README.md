# GPU Screen Recorder (Qt)

A Qt6 frontend for [gpu-screen-recorder](https://git.dec05eba.com/gpu-screen-recorder).

This is a Qt rewrite of the original [GTK version](https://git.dec05eba.com/gpu-screen-recorder-gtk).

## Dependencies

- Qt 6 (Widgets, DBus modules)
- [gpu-screen-recorder](https://git.dec05eba.com/gpu-screen-recorder) (the backend)

## Build

```sh
cmake -B build
cmake --build build
```

## Install

```sh
sudo cmake --install build
```

## Usage

```sh
./build/gpu-screen-recorder-qt
```

For global hotkeys on Wayland, use KDE Plasma (the only Wayland compositor with working global shortcut portal support).
