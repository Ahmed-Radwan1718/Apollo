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

Nexus requires Qt 6.8 or newer. LibRaw does not need to be installed separately: CMake downloads the pinned LibRaw 0.22.2 source and its official companion CMake project on the first configure, then builds LibRaw statically as part of Nexus.

Because LibRaw is downloaded during the first configure, an internet connection and Git must be available at that time. Subsequent builds reuse the copy in the build directory.

### Qt Creator

Open the repository with your normal Qt desktop kit and configure it normally. Qt 6.12 MinGW 64-bit is supported by this project setup; no vcpkg toolchain or `libraw_DIR` setting is required.

If you previously configured the older vcpkg-based version, clear the old CMake configuration or delete the kit's build directory once before reconfiguring.

### Command line example

```powershell
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="C:/Qt/6.12.0/mingw_64"
cmake --build build
```

Run `build/Nexus.exe` after the build completes (the exact output location can vary by generator/configuration).

## Source layout

```text
cmake/                    # third-party build integration
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
