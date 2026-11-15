# PROCESS

**Status:** DONE (exe link pending: app was running)
**Started:** 2026-11-15
**Last updated:** 2026-11-15

## Task
"remove the tab bar, open a folder":
- remove the tab bar
- open a folder: new pane left of the shape palette with the folder's .flowx files as a tree
- pane collapses/opens; click a file to switch to it; click a folder to expand/collapse (caret on the right)
- unsaved file: red dot on the right of its row
- drag/drop rows to move files
- "+ New file" button at the bottom (and a shortcut): new file in the root folder
- save all (user picked Ctrl+Alt+S)

## Decisions (asked the user)
- Save all: Ctrl+Alt+S
- Open a single file without a folder: replaces the current chart (asks to save)
- New file: auto-named Untitled.flowx / Untitled 2.flowx… in the root, then inline rename

Other choices: Ctrl+Shift+O opens a folder, Ctrl+Alt+B shows/hides the pane, Ctrl+N = new file
when a folder is open. Every chart opened from the folder stays loaded (keeps undo + unsaved
changes). Rename (F2) keeps the .flowx extension. Moving/renaming updates open charts' paths.

## Done
- src/MainWindow.h/.cpp (OpenChart list replaces QTabBar; FolderModel/FolderTree/FolderDelegate)
- src/HomeScreen.h/.cpp (Open folder… button), tests/guitest.cpp, README.md

## Tests / verification
| Case | How | Result |
|------|-----|--------|
| selftest | `build/selftest.exe` | pass |
| guitest (new folder test: replace, switch, red dot state, drag move, new file + rename, Ctrl+Alt+S) | `build/guitest.exe` | pass |
| Flowchart.exe link | `cmake --build build` | blocked: Flowchart.exe was running |

## Notes for resuming
- Build env: PATH=/c/msys64/ucrt64/bin. Build dir `build/`.
