# PROCESS

**Status:** DONE
**Started:** 2026-11-10 19:45
**Last updated:** 2026-11-10 20:25

## Task
"open multi file as multi tab":
- tab bar on top of the app with file names, browser-like
- always at least 1 tab; closing the only tab closes the app
- '+' button on the bar opens a file selector and adds the file(s) as tabs
- closing a tab asks to save if unsaved
- home screen (recent files): clicking a file opens just that file (no multi-file workspace)

## Plan
1. [x] Panels: `StylePanel::setScene`, `FindPanel::setView` (follow the current tab; accept null)
2. [x] MainWindow: `struct ChartTab {scene, view, path, recovery}`; QTabBar + '+' above the canvas;
   QStackedWidget of views; QUndoGroup for Undo/Redo; per-tab save/maybeSave/writeFile;
   Ctrl+W closes tab (last tab -> close app); Ctrl+Tab / Ctrl+Shift+Tab
3. [x] Autosave: one recovery file per tab (`autosave-<id>.flowx`); old `autosave.flowx` still recovered
4. [x] guitest: tabs test
5. [x] README update

Decisions: one ChartScene+ChartView per tab (own undo history and zoom). Home screen only at
startup. Opening an already-open file switches to its tab. An empty untouched Untitled tab is
replaced when a file is opened.

## Done
- src/Panels.h/.cpp, src/MainWindow.h/.cpp, tests/guitest.cpp, README.md

## Doing now
- Nothing.

## Not done yet
- Copy `build/Flowchart.exe` to `dist/` (optional, not asked yet).

## Tests / verification
| Case | How | Result |
|------|-----|--------|
| flowcore + tests build | `cmake --build build --target selftest guitest` | pass |
| Flowchart.exe link | `cmake --build build` | pass (after user closed the app) |
| selftest | `build/selftest.exe` | pass |
| guitest (incl. new tabs test) | `build/guitest.exe` | pass |

## Notes for resuming
- Build env: PATH=/c/msys64/ucrt64/bin. Build dir `build/`.
