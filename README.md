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
| Folder | No tabs (the home screen lists the last 6 opened folders above recent charts): one chart is shown at a time. Ctrl+O opens a chart in place of the current one (asks to save unsaved changes). Ctrl+Shift+O (or Open folder… on the home screen) opens a folder: a pane on the far left lists its .flowx files and subfolders as a tree. Click a file to show it (unsaved charts stay loaded and get a red dot), click a folder to open/close it, drag rows onto a folder (or empty space: the root) to move them on disk, F2 renames. + New file (or Ctrl+N) creates Untitled.flowx in the root and starts naming it. Ctrl+Alt+S saves all unsaved charts. Ctrl+Alt+B shows/hides the pane. Right-click a file: rename, delete (Recycle Bin), its three exports, create new file/folder next to it. Right-click a folder: rename, delete, export the folder for LLM / as update for LLM, create new file/folder inside it. Right-click empty space: the same for the root folder. Folder export asks for a target and writes `<target>/<folder name>/` with one .md per chart (same subfolders); as update, earlier .md files are rewritten with changes marked, new charts get a fresh .md, and .md files from another chart are skipped and listed |
| File | Ctrl+N, Ctrl+O, Ctrl+Shift+O open folder, Ctrl+S, Ctrl+Alt+S save all, Ctrl+Shift+S, Ctrl+E export PNG/SVG/PDF, Ctrl+Shift+E export for LLM (.md/.txt outline: container nesting + links, no positions), Export as update for LLM (rewrite an earlier export of the same chart with changes marked) |
| View | F1 or the ⌨ Shortcuts button (bottom of the shape panel) lists every shortcut, Ctrl+B shape panel icons-only, Ctrl+M minimap, View > Dark theme |

Unsaved work is autosaved (every minute by default): saved charts write their own file, Untitled
ones get a recovery copy `%APPDATA%\Flowchart\Flowchart\autosave-*.flowx` (one per unsaved chart), offered
for recovery after a crash.

Open charts follow their file on disk, like VS Code: when another program (an editor, an LLM)
changes the file, a chart with no unsaved edits reloads it at once. A chart with unsaved edits
keeps them; saving it then asks Overwrite / Cancel, and autosave writes a recovery copy instead of
the file.

## Command line export for LLM

```sh
Flowchart.exe --export chart.flowx                        # Markdown outline to stdout
Flowchart.exe --export folder                             # every chart under it, to stdout
Flowchart.exe --export chart.flowx -o chart.md            # to a file (.md or .txt)
Flowchart.exe --export folder -o out                      # out/<same subfolders>/<chart>.md
Flowchart.exe --export chart.flowx -o chart.md --update   # rewrite an earlier export, changes marked
Flowchart.exe --export folder -o out --update             # same for a folder (new charts: fresh .md)
```

Same output as the app's Export for LLM / Export as update for LLM. Exit code 0 ok, 1 a chart was
skipped (reason on stderr), 2 bad arguments. It is a GUI exe: in PowerShell pipe it
(`... | Out-String`) so the shell waits for the output.

## LLM skill

`skills/flowx/SKILL.md` teaches an AI assistant to read .flowx files (export when only the content
is needed, JSON when editing) and to write them. To install it, ask your assistant
"install the skill in this repo <repo link>": it copies `skills/flowx/` into `~/.claude/skills/flowx/`
(all projects) or `<project>/.claude/skills/flowx/` (one project). Put `Flowchart.exe` on PATH so
the skill can run the export.

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

| File | Ctrl+N, Ctrl+O, Ctrl+Shift+O open folder, Ctrl+S, Ctrl+Alt+S save all, Ctrl+Shift+S, Ctrl+E export PNG/SVG/PDF, Ctrl+Shift+E export for LLM (.md/.txt outline: container nesting + links, no positions), Export as update for LLM (rewrite an earlier export of the same chart with changes marked) |
|---|---|
| `src/Model.*` | Shape list, shape outlines and ports, `Style`, JSON helpers |
| `src/Items.*` | `NodeItem` (shape, handles, quick-connect arrows, inline text), `LinkItem` (elbow routing) |
| `src/ChartScene.*` | Items by id, undo (one JSON-snapshot command type), clipboard, snapping/guides, export |
| `src/ChartView.*` | Zoom/pan, adaptive grid, minimap, shape drop |
| `src/Panels.*` | Left shape palette, right style panel |
| `src/HomeScreen.*`, `src/MainWindow.*` | Home screen, folder pane, open charts (one scene + view each), menus, open/save, autosave, theme |

## Credits

App icon: Arrow infinity Icon by IcoLabs on <a href="https://icon-icons.com/authors/1038-icolabs">Icon-Icons.com</a>
