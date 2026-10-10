# Nexus

Nexus is a personal, non-destructive RAW photo editor for Windows 11 built with Qt.

## Current feature: photo canvas

The first implemented slice is the center editing canvas. It is deliberately separate from decoding and future edit state: the canvas receives a display-ready `QImage` preview and never changes source pixels.

Current interactions:

- **Open photo:** `Ctrl+O` or drag a local image onto the canvas.
- **Fit to view:** `F`.
- **100% / actual pixels:** `1`. On high-DPI Windows displays, 100% means one image pixel per physical display pixel.
- **Zoom:** mouse wheel under the pointer, `Ctrl++`, or `Ctrl+-`.
- **Pan:** hold `Space` and drag with the left mouse button, or drag with the middle mouse button.
- **Toggle Fit / 100%:** double-click the image.

The temporary bootstrap loader uses Qt's supported raster formats (typically JPEG, PNG, TIFF, and others provided by the installed Qt image plugins). RAW decoding is intentionally not faked here; it belongs in the render pipeline that will later feed the canvas.

## Build on Windows 11

Install Qt 6.8 or newer with a matching compiler kit (MSVC 2022 is recommended), then configure with Qt Creator or CMake.

Example with a Qt MSVC kit and Ninja:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64"
cmake --build build
```

Run `build/Nexus.exe` (the exact output location can vary by generator/configuration).

## Source layout

```text
src/
├── app/                  # application shell
├── imaging/              # image decoding/loading boundaries
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
