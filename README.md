# Nexus

Nexus is a personal, non-destructive RAW photo editor for Windows 11 built with Qt.

## Current features

### Photo canvas

The center editing canvas receives a display-ready `QImage` preview and never changes source pixels.

Current interactions:

- **Open photo:** `Ctrl+O` or drag a local image onto the canvas.
- **Fit to view:** `F`.
- **100% / actual pixels:** `1`. On high-DPI Windows displays, 100% means one image pixel per physical display pixel.
- **Zoom:** mouse wheel under the pointer, `Ctrl++`, or `Ctrl+-`.
- **Pan:** hold `Space` and drag with the left mouse button, or drag with the middle mouse button.
- **Toggle Fit / 100%:** double-click the image.

### Sony ARW import

`.arw` files are decoded with LibRaw and rendered to an sRGB display image for the canvas. Nexus uses the camera white balance and keeps LibRaw automatic brightness disabled. The source ARW file is never modified.

Other raster image formats continue to use Qt's installed image plugins.

## Build on Windows 11

Nexus requires Qt 6.8 or newer and LibRaw. The repository includes a `vcpkg.json` manifest for LibRaw.

Install and bootstrap vcpkg once, then configure Nexus with the vcpkg toolchain and your Qt MSVC kit. For example:

```powershell
git clone https://github.com/microsoft/vcpkg C:/dev/vcpkg
C:/dev/vcpkg/bootstrap-vcpkg.bat

cmake -S . -B build -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64"

cmake --build build
```

The vcpkg manifest installs LibRaw automatically during configuration. Run `build/Nexus.exe` after the build completes (the exact output location can vary by generator/configuration).

## Source layout

```text
src/
├── app/                  # application shell
├── imaging/              # Qt raster loading and Sony ARW decoding
└── ui/
    └── canvas/           # center photo viewer
```

The intended long-term flow is:

```text
Original RAW -> decoder/render pipeline -> display-ready preview -> PhotoCanvas
                         ^
                         |
                    non-destructive
                      edit state
```
