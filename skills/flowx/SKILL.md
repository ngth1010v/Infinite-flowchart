---
name: flowx
description: Read, edit and create .flowx flowchart files (the Flowchart desktop app's JSON format). Use whenever a task involves a .flowx file or a folder of them, such as understanding a chart, summarizing it, building code from it, or adding, changing or removing shapes and links.
---

# .flowx flowcharts

A `.flowx` file is one flowchart saved by the Flowchart app (Windows, Qt). It is JSON. One field,
`thumb`, holds a base64 PNG preview that can be tens of KB on one line. Never read the raw file
whole just to see what the chart says.

## Pick the right way to read

| Need | Do |
|---|---|
| Only the content (what the chart says, its steps, nesting, links) | Run the export command below and read the Markdown it prints |
| A whole folder of charts | Export the folder (one command prints every chart) |
| To edit the chart | Read the JSON with `thumb` stripped (see "Read the JSON") |
| What changed since you last read it | `--update` export against your earlier export file |

### Export (content only)

```sh
Flowchart.exe --export path/to/chart.flowx               # prints Markdown to stdout
Flowchart.exe --export path/to/folder                    # every chart under it, separated by ---
Flowchart.exe --export path/to/chart.flowx -o chart.md   # writes a file instead
Flowchart.exe --export path/to/folder -o out             # out/<same subfolders>/<chart>.md
Flowchart.exe --export path/to/chart.flowx -o chart.md --update   # rewrite chart.md, changes marked
```

- Exit code 0 = ok, 1 = some chart could not be read or written (reason on stderr), 2 = bad arguments.
- `Flowchart.exe` is a GUI program. In PowerShell, pipe it, or the shell does not wait for the
  output: `& Flowchart.exe --export chart.flowx | Out-String`. Bash and cmd capture it as usual.
- Find the exe: try `Flowchart.exe` on PATH (`where Flowchart.exe` / `Get-Command Flowchart.exe`),
  then a `dist\Flowchart.exe` or `build\Flowchart.exe` in the Flowchart repo. If it is not found,
  ask the user for its path once. If there is no exe at all, fall back to reading the JSON.
- Writing an export with `-o` stores a `chartId` in the .flowx (once) so a later `--update` can
  match it. Printing to stdout never changes the .flowx.
- `--update` needs an earlier export of the same chart at `-o`. For a folder, charts without an
  earlier .md get a fresh one.

Export format (positions and styles are left out):

```
# Flowchart: <name>
<!-- flowx chart <chartId> -->
## Shapes
- [1] Terminator (Start / End) "Start"
- [10] Horizontal Container "Backend"
  - [4] Process "Validate"          <- indented = inside the container above
## Links
- [1] Terminator (Start / End) "Start" -> [4] Process "Validate" (label: "ok")
```

`[n]` is the shape's `id` in the JSON, so an export also tells you which ids to edit.
Arrows: `->` normal, `<-` reversed, `<->` both ends, `--` no arrowheads.
With `--update`, a "Changes since the previous export" section lists added, changed and removed
shapes and links, and changed lines carry `(new)` or `(changed)`.

### Read the JSON (for editing)

Load it without `thumb`, for example:

```sh
python -c "import json,sys; d=json.load(open(sys.argv[1],encoding='utf-8-sig')); d.pop('thumb',None); print(json.dumps(d,indent=1))" chart.flowx
```

Most nodes repeat a long `style` block. For a big chart, print just `id`, `shape`, `text`, `x`,
`y`, `w`, `h` for nodes and `id`, `from`, `to`, `text` for links.

## File format

```json
{
  "format": "flowx",
  "version": 1,
  "chartId": "uuid, optional, keep it as is",
  "nodes": [
    {"id": 1, "shape": "terminator", "x": 0, "y": 0, "w": 140, "h": 50, "text": "Start", "style": {...}},
    {"id": 10, "shape": "hcontainer", "x": 0, "y": 300, "w": 640, "h": 220, "text": "Backend", "padding": 40, "style": {...}}
  ],
  "links": [
    {"id": 20, "from": 1, "fromPort": "bottom", "to": 2, "toPort": "top", "path": [[0,25],[0,85]], "text": "", "style": {...}}
  ],
  "view": {...},
  "thumb": "base64 PNG"
}
```

Required: `format` = `"flowx"`, `version` = 1. Every other field is optional with defaults.
A 0-byte file is a valid empty chart.

### Nodes

- `id`: positive integer, unique across nodes AND links (one id space). An item with a missing,
  zero or duplicate id is silently dropped.
- `x`, `y`: the CENTER of the shape, in chart units (y grows downward). Not the top-left corner.
- `w`, `h`: size (min 10). Default 140 x 70.
- `text`: label; `\n` for line breaks. Lines never wrap: widen `w` for long text.
- `shape` (unknown keys become `process`), with the app's default size:

| key | shape | default w x h |
|---|---|---|
| `terminator` | Terminator (Start / End) | 140 x 50 |
| `process` | Process | 140 x 70 |
| `decision` | Decision | 150 x 90 |
| `io` | Input / Output | 150 x 70 |
| `predefined` | Predefined Process | 150 x 70 |
| `document` | Document | 140 x 80 |
| `multidoc` | Multiple Documents | 150 x 90 |
| `manualinput` | Manual Input | 140 x 70 |
| `manualop` | Manual Operation | 140 x 70 |
| `preparation` | Preparation | 150 x 70 |
| `connector` | Connector | 50 x 50 |
| `offpage` | Off-page Connector | 60 x 60 |
| `database` | Database | 100 x 90 |
| `storeddata` | Stored Data | 140 x 70 |
| `delay` | Delay | 120 x 70 |
| `display` | Display | 150 x 70 |
| `looplimit` | Loop Limit | 140 x 60 |
| `hcontainer` | Horizontal Container (swimlane, title band on the left) | 640 x 220 |
| `vcontainer` | Vertical Container (swimlane, title band on top) | 260 x 480 |

### Containers

There is no parent field. A shape belongs to the smallest container whose rectangle holds the
shape's center. To put a shape in a container, place its center inside the container's rectangle
(and outside the 40-unit title band); to take it out, move it outside. `padding` (default 40) is
only used by the app's "fit container" command. Make a container big enough for its contents.

### Links

- `from`, `to`: node ids. A link whose ends do not exist is silently dropped.
- `fromPort`, `toPort`: `top`, `right`, `bottom`, `left`, or `auto` (the app picks the side facing
  the other shape). Use `auto` when unsure.
- `path`: optional list of `[x, y]` bend points. Omit it (or give fewer than 3 points) and the app
  routes an elbow line itself. When you move shapes, delete `path` on their links so they re-route.
- `text`: optional label on the line, e.g. `"Yes"` / `"No"` out of a decision.

### Style (optional on nodes and links)

```json
{"fill": "#ffffffff", "stroke": "#ff475569", "text": "#ff1f2430", "strokeWidth": 1.5, "dash": 1,
 "font": "Segoe UI", "fontSize": 14, "bold": false, "italic": false, "align": "center",
 "arrowStart": false, "arrowEnd": true}
```

- Colors: `#rrggbb` or `#aarrggbb`.
- `dash`: 0 none, 1 solid, 2 dash, 3 dot, 4 dash-dot, 5 dash-dot-dot.
- `align`: `left`, `center`, `right`.
- `arrowStart` / `arrowEnd` matter on links only.
- A missing style or key falls back to white fill, gray stroke, solid, 14 px Segoe UI. To match the
  chart's look, copy the `style` of an existing node of the same shape (or link) in the same file.

### Other fields

- `chartId`: keep it unchanged. It ties the chart to its LLM exports. Do not copy it into a new file.
- `view`: the app's saved zoom/scroll. Keep or drop it; it is not content.
- `thumb`: keep or drop it. It goes stale after your edit and the app regenerates it on the next save.

## Write or edit a chart

1. Edit with a script (Python `json`, or PowerShell `ConvertFrom-Json` / `ConvertTo-Json -Depth 20`)
   that loads the file, changes `nodes`/`links`, and writes it back as UTF-8. Do not hand-edit around
   the `thumb` line.
2. New ids: `max(all node and link ids) + 1`, and count up.
3. Layout so the chart stays readable: main flow top to bottom, about 60 to 100 units of gap between
   shapes (for example, centers 140 apart vertically for 70-high shapes), branches to the side
   (for example, 260 to the right). Keep shapes from overlapping.
4. Keep everything you did not mean to change (ids, positions, styles, `chartId`).
5. Check: run `Flowchart.exe --export <file>` and confirm the shapes, nesting and links are what you
   meant. Dropped items (bad ids, links to missing nodes) are missing from that output.

A new chart can be just:

```json
{"format": "flowx", "version": 1,
 "nodes": [{"id": 1, "shape": "terminator", "x": 0, "y": 0, "w": 140, "h": 50, "text": "Start"},
           {"id": 2, "shape": "process", "x": 0, "y": 140, "text": "Do work"}],
 "links": [{"id": 3, "from": 1, "fromPort": "auto", "to": 2, "toPort": "auto"}]}
```

## While the app has the file open

The app watches open charts. If the chart has no unsaved edits there, it reloads your change at
once. If it has unsaved edits, they stay, and when the user saves, the app asks whether to
overwrite your version. So before a big edit, tell the user to save (Ctrl+S) in the app first,
and after editing, tell them the app will reload it.
