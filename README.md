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
| Zoom | Mouse wheel (around the cursor), Ctrl+= / Ctrl+-, Ctrl+0 = 100%, Ctrl+1 = fit whole chart (2%–5000%) |
| Pan | Right-drag, middle-drag, Space+drag, Shift+wheel (sideways) |
| Snap | Grid + alignment guides while dragging; hold Alt to place freely; toggle in View menu |
| Edit | Ctrl+Z / Ctrl+Y, Ctrl+C / X / V, Ctrl+D duplicate, Delete, Ctrl+A, Ctrl+Shift+F fit to content (selected shapes fit their text, 2+ shapes then all get the largest width and height; selected containers fit their contents, padding set in the style panel), Ctrl+L with only 2+ links selected: bundle them (middle segments moved onto shared lines, as little as possible), Ctrl+Arrow align selected shapes' left/right/top/bottom edges (containers and links untouched), Ctrl+Shift+Arrow align and space them evenly |
| Find | Ctrl+F opens a small find & replace window (drag it anywhere). Matches highlight on the canvas as you type; Aa (Alt+C) toggles match case. The current match is orange and centered on screen. Enter / Shift+Enter or ↑ ↓: next / previous. Replace: changes the current shape or link label and moves on; Replace all: every match, one undo step. Esc closes |
| Tabs | Each open chart is a tab on the bar at the top (• = unsaved). + or Ctrl+O opens one or more charts in new tabs (a chart already open just switches to its tab); Ctrl+N adds an Untitled tab. Ctrl+Tab / Ctrl+Shift+Tab switch tabs, drag a tab to reorder. Closing a tab (✕ or Ctrl+W) asks to save unsaved changes; closing the last tab closes the app. The home screen shows only at startup: clicking a recent chart opens that one chart |
| File | Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Shift+S, Ctrl+E export PNG/SVG/PDF, Ctrl+Shift+E export for LLM (.md/.txt outline: container nesting + links, no positions), Ctrl+W close tab |
| View | F1 or the ⌨ Shortcuts button (bottom of the shape panel) lists every shortcut, Ctrl+B shape panel icons-only, Ctrl+M minimap, View > Dark theme |

Unsaved work is autosaved (every minute by default): saved charts write their own file, Untitled
ones get a recovery copy `%APPDATA%\Flowchart\Flowchart\autosave-*.flowx` (one per tab), offered
for recovery after a crash.

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
| `src/HomeScreen.*`, `src/MainWindow.*` | Home screen, tabs (one scene + view per tab), menus, open/save, autosave, theme |

## Credits

App icon: Arrow infinity Icon by IcoLabs on <a href="https://icon-icons.com/authors/1038-icolabs">Icon-Icons.com</a>
