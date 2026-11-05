# Flowchart

Local Windows app for drawing flowcharts. C++20 + Qt 6 (Widgets, `QGraphicsView`).
Each chart is one `.flowx` file (JSON with an embedded PNG thumbnail).

## Run

`dist\Flowchart.exe` is self-contained (Qt and MinGW DLLs alongside). Copy the whole `dist` folder.
Open a chart directly: `Flowchart.exe path\to\chart.flowx`. Example: `samples\square-root.flowx`.

## Use

| Action | How |
|---|---|
| Add shape | Drag from the left list, or click it (adds at view center) |
| Connect | Hover a shape, drag one of the 4 blue arrows onto another shape |
| Reconnect link | Select the link, drag its round end handle onto another shape (drop on empty canvas cancels) |
| Containers | Horizontal / Vertical Container: shapes whose center is inside move with it (and are copied with it); double-click the title band to rename |
| Add connected shape | Click an arrow (or drag it to empty space) |
| Edit text | Double-click a shape (Enter = done, Shift+Enter = new line, Esc = cancel); double-click a link for its label |
| Resize | Select one shape, drag a corner handle |
| Style | Right panel opens when something is selected; with several selected, edits apply to all |
| Zoom | Mouse wheel (around the cursor), Ctrl+= / Ctrl+-, Ctrl+0 = 100%, Ctrl+R = fit whole chart (2%–5000%) |
| Pan | Right-drag, middle-drag, Space+drag, Shift+wheel (sideways) |
| Snap | Grid + alignment guides while dragging; hold Alt to place freely; toggle in View menu |
| Edit | Ctrl+Z / Ctrl+Y, Ctrl+C / X / V, Ctrl+D duplicate, Delete, Ctrl+A |
| File | Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Shift+S, Ctrl+E export PNG/SVG/PDF, Ctrl+W back to home |
| View | Ctrl+B shape panel icons-only, Ctrl+M minimap, View > Dark theme |

Unsaved work is autosaved every 30 s to `%APPDATA%\Flowchart\Flowchart\autosave.flowx`
and offered for recovery after a crash.

## Build (MSYS2 UCRT64)

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,qt6-base,qt6-svg,qt6-tools}
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/selftest.exe   # model, routing, save/load, undo, export, 5000-node timing
build/guitest.exe    # drives the canvas with synthetic mouse/keyboard input
```

Package: copy `build/Flowchart.exe` to `dist/`, run `windeployqt6 --release dist/Flowchart.exe`,
then copy the MinGW DLLs it depends on (`ldd dist/Flowchart.exe`) into `dist/`.

## Code map

| File | What |
|---|---|
| `src/Model.*` | Shape list, shape outlines and ports, `Style`, JSON helpers |
| `src/Items.*` | `NodeItem` (shape, handles, quick-connect arrows, inline text), `LinkItem` (elbow routing) |
| `src/ChartScene.*` | Items by id, undo (one JSON-snapshot command type), clipboard, snapping/guides, export |
| `src/ChartView.*` | Zoom/pan, adaptive grid, minimap, shape drop |
| `src/Panels.*` | Left shape palette, right style panel |
| `src/HomeScreen.*`, `src/MainWindow.*` | Home screen, menus, open/save, autosave, theme |
