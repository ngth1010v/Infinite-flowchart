#include "MainWindow.h"
#include "ChartScene.h"
#include "ChartView.h"
#include "HomeScreen.h"
#include "Panels.h"
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QCloseEvent>
#include <QCursor>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QStyleHints>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QHeaderView>
#include <QTreeView>
#include <QTreeWidget>
#include <QUndoGroup>
#include <QUndoStack>
#include <QUrl>
#include <QUuid>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
constexpr int kPaletteOpen = 230, kPaletteClosed = 72, kPanelOpen = 300, kFolderOpen = 240, kFolderClosed = 30;
const QString kFilter = QObject::tr("Flowchart (*.flowx)");

// Hide selection/hover decorations while rendering to an image or file.
struct OverlaysHidden {
    ChartScene* s;
    bool dark;  // also back to the chart's real colors (light thumbnails)
    explicit OverlaysHidden(ChartScene* scene) : s(scene), dark(scene->darkCanvas) {
        s->overlaysHidden = true;
        s->darkCanvas = false;
    }
    ~OverlaysHidden() {
        s->overlaysHidden = false;
        s->darkCanvas = dark;
    }
};

bool samePath(const QString& a, const QString& b) { return !a.isEmpty() && !b.isEmpty() && QFileInfo(a) == QFileInfo(b); }

// The chart JSON in a .flowx file. Returns an error message (empty on success).
QString readChart(const QString& path, QJsonObject* out) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QObject::tr("Cannot open %1:\n%2").arg(QDir::toNativeSeparators(path), f.errorString());
    QByteArray data = f.readAll();
    if (data.startsWith("\xEF\xBB\xBF")) data.remove(0, 3);  // UTF-8 BOM some editors add
    QJsonParseError err{};
    // A blank file (e.g. an empty .txt renamed to .flowx) opens as an empty chart.
    const QJsonObject o = data.trimmed().isEmpty() ? QJsonObject{{"format", "flowx"}, {"version", 1}}
                                                   : QJsonDocument::fromJson(data, &err).object();
    if (err.error != QJsonParseError::NoError || o["format"].toString() != QLatin1String("flowx"))
        return QObject::tr("%1 is not a valid flowchart file.").arg(QDir::toNativeSeparators(path));
    if (o["version"].toInt() > 1) return QObject::tr("This file was made by a newer version of Flowchart.");
    *out = o;
    return {};
}

// What counts as a change on disk: shapes and links, not the thumbnail, view or chart id.
QJsonObject chartContent(QJsonObject o) {
    o.remove("thumb");
    o.remove("view");
    o.remove("chartId");
    return o;
}

// An LLM export names the chart by its id, so the file on disk must hold that id too, or the
// next load would make a new one. Writes only the id: unsaved edits stay unsaved.
void stampChartId(const QString& path, ChartScene* s) {
    if (path.isEmpty() || s->chartIdSaved) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    QByteArray data = f.readAll();
    f.close();
    if (data.startsWith("\xEF\xBB\xBF")) data.remove(0, 3);
    QJsonObject o = QJsonDocument::fromJson(data).object();
    if (data.trimmed().isEmpty()) o = {{"format", "flowx"}, {"version", 1}};
    if (o["format"].toString() != QLatin1String("flowx")) return;
    o["chartId"] = s->chartId;
    QSaveFile out(path);
    if (out.open(QIODevice::WriteOnly) && out.write(QJsonDocument(o).toJson()) >= 0 && out.commit())
        s->chartIdSaved = true;
}

// LLM text of the chart at `path`, from `open` if given (unsaved edits included), else from disk.
// Null on error (*error says why). With stamp, writes the chart id into the file so a later update
// of the export matches it (only for exports written to a file: a printed one cannot be updated).
QString llmText(const QString& path, ChartScene* open, const QString& title, const QString& previous, QString* error,
                bool stamp = true) {
    ChartScene loaded;
    ChartScene* s = open ? open : &loaded;
    if (!open) {
        QJsonObject o;
        if (*error = readChart(path, &o); !error->isEmpty()) return {};
        loaded.fromJson(o);
    }
    const QString text = s->toLlmText(title, previous, error);
    if (!text.isNull() && stamp) stampChartId(path, s);
    return text;
}

QStringList chartsUnder(const QString& dir) {
    QStringList files;
    for (QDirIterator it(dir, {"*.flowx"}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) files << it.next();
    files.sort(Qt::CaseInsensitive);
    return files;
}

// Every .flowx under `dir` as an LLM .md into `out`; see MainWindow::exportFolderForLlm.
// `open` gives the loaded scene of a chart that is open in the app (null: read the file).
int exportFolder(const QString& dir, const QString& out, bool update, QStringList* skipped,
                 const std::function<ChartScene*(const QString&)>& open) {
    int done = 0;
    for (const QString& path : chartsUnder(dir)) {
        const QString rel = QDir(dir).relativeFilePath(path);
        const QString md = QDir(out).filePath(rel.chopped(QFileInfo(rel).suffix().size()) + "md");
        QString previous, error;  // null: no earlier export, fresh one
        if (QFile f(md); update && f.open(QIODevice::ReadOnly)) previous = QString::fromUtf8(f.readAll());
        const QString text = llmText(path, open ? open(path) : nullptr, QFileInfo(md).completeBaseName(), previous, &error);
        if (text.isNull()) {
            *skipped << rel + ": " + error;
            continue;
        }
        QDir().mkpath(QFileInfo(md).absolutePath());
        QSaveFile f(md);
        if (!f.open(QIODevice::WriteOnly) || f.write(text.toUtf8()) < 0 || !f.commit()) {
            *skipped << rel + ": " + QObject::tr("cannot write %1").arg(QDir::toNativeSeparators(md));
            continue;
        }
        ++done;
    }
    return done;
}

// The exe is a GUI app with no console of its own: write to the handle the caller redirected
// (pipe or file), else to the console of the shell that started it.
void writeStd(bool err, const QString& s) {
#ifdef _WIN32
    HANDLE h = GetStdHandle(err ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    if (!h || h == INVALID_HANDLE_VALUE) {
        static const HANDLE con = AttachConsole(ATTACH_PARENT_PROCESS)
            ? CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr)
            : INVALID_HANDLE_VALUE;
        h = con;
    }
    DWORD mode, n;
    if (GetConsoleMode(h, &mode)) {
        WriteConsoleW(h, reinterpret_cast<const wchar_t*>(s.utf16()), DWORD(s.size()), &n, nullptr);
    } else {
        const QByteArray b = s.toUtf8();
        WriteFile(h, b.constData(), DWORD(b.size()), &n, nullptr);
    }
#else
    std::fputs(s.toUtf8().constData(), err ? stderr : stdout);
#endif
}

// Files and folders under the open folder. Dropping a row onto a folder (or the empty area: the
// root) moves it there on disk; renamed files keep their .flowx extension.
class FolderModel : public QFileSystemModel {
public:
    std::function<void(const QString& from, const QString& to)> moved;
    bool dropMimeData(const QMimeData* data, Qt::DropAction, int, int, const QModelIndex& parent) override {
        QModelIndex dirIndex = parent;
        if (dirIndex.isValid() && !isDir(dirIndex)) dirIndex = dirIndex.parent();  // dropped on a file: its folder
        const QString dir = dirIndex.isValid() ? filePath(dirIndex) : rootPath();
        bool any = false;
        for (const QUrl& u : data->urls()) {
            const QString from = QFileInfo(u.toLocalFile()).absoluteFilePath();
            const QString to = QDir(dir).filePath(QFileInfo(from).fileName());
            // Same place, into itself, or a name already taken there: leave it.
            if (samePath(QFileInfo(from).absolutePath(), dir) || (dir + '/').startsWith(from + '/', Qt::CaseInsensitive) ||
                QFileInfo::exists(to) || !QDir().rename(from, to))
                continue;
            moved(from, to);
            any = true;
        }
        return any;
    }
    bool setData(const QModelIndex& index, const QVariant& value, int role) override {
        QString name = value.toString().trimmed();
        if (role == Qt::EditRole && !isDir(index) && !name.endsWith(".flowx", Qt::CaseInsensitive)) name += ".flowx";
        return QFileSystemModel::setData(index, name, role);
    }
};

// No branch arrows on the left: folders show a caret on the right of their row instead.
class FolderTree : public QTreeView {
protected:
    void drawBranches(QPainter*, const QRect&, const QModelIndex&) const override {}
};

// Right edge of each row: a caret on folders, a red dot on files with unsaved changes, and left of
// that a green diamond on files changed since the row picked in the history pane.
class FolderDelegate : public QStyledItemDelegate {
public:
    std::function<bool(const QString& path)> unsaved, changed;
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& index) const override {
        QStyle* style = opt.widget->style();
        QStyleOptionViewItem bg = opt;  // selection/hover background across the whole row
        initStyleOption(&bg, index);
        bg.text.clear();
        bg.icon = QIcon();
        style->drawControl(QStyle::CE_ItemViewItem, &bg, p, opt.widget);
        const auto* model = static_cast<const QFileSystemModel*>(index.model());
        const bool isChanged = !model->isDir(index) && changed(model->filePath(index));
        QStyleOptionViewItem left = opt;
        left.rect.setRight(opt.rect.right() - (isChanged ? 34 : 18));
        QStyledItemDelegate::paint(p, left, index);
        const QRect mark(opt.rect.right() - 16, opt.rect.top(), 14, opt.rect.height());
        if (isChanged) {
            const QPointF c = QPointF(mark.center()) + QPointF(-15.5, 0.5);
            p->save();
            p->setRenderHint(QPainter::Antialiasing);
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(22, 163, 74));
            p->drawPolygon(QPolygonF{c + QPointF(0, -4.5), c + QPointF(4.5, 0), c + QPointF(0, 4.5), c + QPointF(-4.5, 0)});
            p->restore();
        }
        if (model->isDir(index)) {
            QStyleOption a;
            a.rect = mark;
            a.palette = opt.palette;
            a.state = opt.state;
            const bool open = static_cast<const QTreeView*>(opt.widget)->isExpanded(index);
            style->drawPrimitive(open ? QStyle::PE_IndicatorArrowDown : QStyle::PE_IndicatorArrowRight, &a, p, opt.widget);
        } else if (unsaved(model->filePath(index))) {
            p->save();
            p->setRenderHint(QPainter::Antialiasing);
            p->setPen(Qt::NoPen);
            p->setBrush(opt.state & QStyle::State_Selected ? opt.palette.highlightedText() : opt.palette.text());  // black; white in dark theme
            p->drawEllipse(QPointF(mark.center()) + QPointF(0.5, 0.5), 3.2, 3.2);
            p->restore();
        }
    }
};

}  // namespace

MainWindow::MainWindow() {
    m_undoGroup = new QUndoGroup(this);
    m_views = new QStackedWidget;

    m_palette = new ShapePalette;
    m_paletteBox = new QWidget;
    m_paletteBox->setFixedWidth(kPaletteOpen);
    auto* pl = new QVBoxLayout(m_paletteBox);
    pl->setContentsMargins(6, 4, 2, 4);
    pl->setSpacing(2);
    m_paletteToggle = new QToolButton;
    m_paletteToggle->setAutoRaise(true);
    m_paletteToggle->setToolTip(tr("Show/hide shape names (Ctrl+B)"));
    connect(m_paletteToggle, &QToolButton::clicked, this, [this] { setPaletteCollapsed(!m_paletteCollapsed); });
    pl->addWidget(m_paletteToggle, 0, Qt::AlignRight);
    pl->addWidget(m_palette, 1);
    m_shortcutsButton = new QToolButton;
    m_shortcutsButton->setAutoRaise(true);
    m_shortcutsButton->setToolTip(tr("Keyboard shortcuts (F1)"));
    m_shortcutsButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_shortcutsButton, &QToolButton::clicked, this, &MainWindow::showShortcuts);
    pl->addWidget(m_shortcutsButton);

    m_panel = new StylePanel(nullptr);
    m_find = new FindPanel(nullptr, this);
    m_panelScroll = new QScrollArea;
    m_panelScroll->setWidget(m_panel);
    m_panelScroll->setWidgetResizable(true);
    m_panelScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_panelScroll->setFrameShape(QFrame::NoFrame);
    m_panel->setMinimumWidth(kPanelOpen - 16);  // content keeps its width and is clipped while sliding
    m_panelScroll->setFixedWidth(0);
    m_panelScroll->hide();

    // CSS "ease": cubic-bezier(0.25, 0.1, 0.25, 1).
    QEasingCurve ease(QEasingCurve::BezierSpline);
    ease.addCubicBezierSegment(QPointF(0.25, 0.1), QPointF(0.25, 1), QPointF(1, 1));
    for (auto* a : {&m_leftAnim, &m_rightAnim, &m_folderAnim}) {
        a->setDuration(220);
        a->setEasingCurve(ease);
    }
    connect(&m_folderAnim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& v) { m_folderBox->setFixedWidth(v.toInt()); });
    connect(&m_folderAnim, &QVariantAnimation::finished, this, [this] {
        m_folderBody->setVisible(!m_folderCollapsed);  // the tree only goes once the pane is narrow
        m_folderName->setVisible(!m_folderCollapsed);
    });
    connect(&m_leftAnim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& v) { setPaletteWidth(v.toInt()); });
    connect(&m_leftAnim, &QVariantAnimation::finished, this, [this] { m_palette->setCompact(m_paletteCollapsed); });
    connect(&m_rightAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_panelScroll->setFixedWidth(v.toInt());
        m_panelScroll->setVisible(v.toInt() > 0);
    });
    // Thin strip on the far right holding the style panel's open/close button.
    auto* panelStrip = new QWidget;
    auto* sl = new QVBoxLayout(panelStrip);
    sl->setContentsMargins(2, 4, 2, 4);
    m_panelToggle = new QToolButton;
    m_panelToggle->setAutoRaise(true);
    m_panelToggle->setText(QStringLiteral("«"));
    m_panelToggle->setToolTip(tr("Show/hide style panel (Ctrl+Shift+B)"));
    sl->addWidget(m_panelToggle);
    sl->addStretch();

    // Folder pane on the far left, shown while a folder is open: its .flowx files as a tree.
    m_fsModel = new FolderModel;
    m_fsModel->setParent(this);
    m_fsModel->setReadOnly(false);  // inline rename (F2) and drag/drop moves
    m_fsModel->setNameFilters({"*.flowx"});
    m_fsModel->setNameFilterDisables(false);  // hide other files instead of greying them out
    static_cast<FolderModel*>(m_fsModel)->moved = [this](const QString& from, const QString& to) { pathMoved(from, to); };
    connect(m_fsModel, &QFileSystemModel::fileRenamed, this,
            [this](const QString& dir, const QString& from, const QString& to) { pathMoved(dir + '/' + from, dir + '/' + to); });
    m_tree = new FolderTree;
    m_tree->setModel(m_fsModel);
    for (int c = 1; c < m_fsModel->columnCount(); ++c) m_tree->hideColumn(c);  // name only
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(false);
    m_tree->setExpandsOnDoubleClick(false);  // a single click toggles a folder
    m_tree->setEditTriggers(QAbstractItemView::EditKeyPressed);
    m_tree->setDragDropMode(QAbstractItemView::DragDrop);
    m_tree->setDefaultDropAction(Qt::MoveAction);
    m_tree->setFrameShape(QFrame::NoFrame);
    auto* delegate = new FolderDelegate(m_tree);
    delegate->unsaved = [this](const QString& path) {
        const OpenChart* t = findChart(path);
        return t && !t->scene->undoStack()->isClean();
    };
    delegate->changed = [this](const QString& path) {
        const OpenChart* t = findChart(path);
        return t && t->historyChanged;
    };
    m_tree->setItemDelegate(delegate);
    connect(m_tree, &QTreeView::clicked, this, [this](const QModelIndex& i) {
        if (m_fsModel->isDir(i)) m_tree->setExpanded(i, !m_tree->isExpanded(i));
        else openFile(m_fsModel->filePath(i));
    });
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        showFolderMenu(m_tree->indexAt(pos), m_tree->viewport()->mapToGlobal(pos));
    });
    m_folderName = new QLabel;
    QFont fnf = m_folderName->font();
    fnf.setBold(true);
    m_folderName->setFont(fnf);
    m_folderName->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);  // long names never push » out while sliding
    m_folderToggle = new QToolButton;
    m_folderToggle->setAutoRaise(true);
    m_folderToggle->setToolTip(tr("Show/hide folder pane (Ctrl+Alt+B)"));
    connect(m_folderToggle, &QToolButton::clicked, this, [this] { setFolderCollapsed(!m_folderCollapsed); });
    auto* newFile = new QToolButton;
    newFile->setText(tr("+  New file"));
    newFile->setToolTip(tr("New chart in the folder root (Ctrl+N)"));
    newFile->setAutoRaise(true);
    newFile->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(newFile, &QToolButton::clicked, this, [this] { newFileInFolder(); });
    m_folderBody = new QWidget;
    auto* fbl = new QVBoxLayout(m_folderBody);
    fbl->setContentsMargins(0, 0, 0, 0);
    fbl->addWidget(m_tree, 1);
    fbl->addWidget(newFile);
    m_folderBody->setMinimumWidth(kFolderOpen - 6);  // keeps its width and is clipped while sliding
    m_folderBox = new QWidget;
    auto* fl = new QVBoxLayout(m_folderBox);
    fl->setContentsMargins(4, 4, 2, 4);
    fl->setSpacing(2);
    auto* fhead = new QHBoxLayout;
    fhead->addWidget(m_folderName, 1);
    fhead->addWidget(m_folderToggle, 0, Qt::AlignRight);
    fl->addLayout(fhead);
    fl->addWidget(m_folderBody, 1);
    fl->addStretch();  // collapsed: keeps the » button at the top instead of floating mid-height
    m_folderBox->hide();
    m_folderGap = new QWidget;  // window-colored strip between the folder pane and the shape pane
    m_folderGap->setFixedWidth(6);
    m_folderGap->hide();
    // Both side panes (header, buttons and all) use the list color, not the window color.
    for (QWidget* box : {m_folderBox, m_paletteBox}) {
        box->setAutoFillBackground(true);
        box->setBackgroundRole(QPalette::Base);
    }

    m_historyList = new QTreeWidget;
    m_historyList->setColumnCount(2);
    m_historyList->setHeaderHidden(true);
    m_historyList->setRootIsDecorated(false);
    m_historyList->setFrameShape(QFrame::NoFrame);
    m_historyList->header()->setStretchLastSection(true);
    m_historyList->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    connect(m_historyList, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        const int row = item->data(0, Qt::UserRole).toInt();
        if (row != m_historyPick) return pickHistory(row);
        const int files = int(std::count_if(m_charts.begin(), m_charts.end(), [](OpenChart* t) { return t->historyChanged; }));
        if (files) QGuiApplication::clipboard()->setText(m_historyPrompt);
        setHistoryOpen(false);
        toast(files ? tr("Copied changes of %n chart(s) as an LLM prompt", nullptr, files) : tr("Nothing to copy: no shape, text or link changed"));
    });
    auto* historyHint = new QLabel(tr("<b>History</b><br>Click a change to outline everything changed since. Click it again to copy the changes as an LLM prompt."));
    historyHint->setWordWrap(true);
    historyHint->setContentsMargins(6, 6, 6, 4);
    m_historyBox = new QWidget;
    m_historyBox->setFixedWidth(kPanelOpen);
    m_historyBox->setAutoFillBackground(true);
    m_historyBox->setBackgroundRole(QPalette::Base);
    auto* hbl = new QVBoxLayout(m_historyBox);
    hbl->setContentsMargins(0, 0, 0, 0);
    hbl->setSpacing(0);
    hbl->addWidget(historyHint);
    hbl->addWidget(m_historyList, 1);
    m_historyBox->hide();

    auto* noChart = new QLabel(tr("Pick a chart on the left, or create one with + New file."));
    noChart->setAlignment(Qt::AlignCenter);
    noChart->setEnabled(false);
    m_noChart = noChart;
    m_views->addWidget(m_noChart);

    auto* h = new QHBoxLayout;
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    h->addWidget(m_folderBox);
    h->addWidget(m_folderGap);
    h->addWidget(m_paletteBox);
    h->addWidget(m_views, 1);
    h->addWidget(m_historyBox);
    h->addWidget(m_panelScroll);
    h->addWidget(panelStrip);
    m_chartPage = new QWidget;
    m_chartPage->setLayout(h);

    m_home = new HomeScreen;
    m_stack = new QStackedWidget;
    m_stack->addWidget(m_home);
    m_stack->addWidget(m_chartPage);
    setCentralWidget(m_stack);

    m_zoomLabel = new QLabel;
    statusBar()->addWidget(new QLabel(tr("Wheel: zoom   ·   Right-drag / Space-drag / Shift+wheel: pan   ·   Ctrl+1: fit chart   ·   "
                                         "Alt while dragging: no snap   ·   Hover a shape and drag an arrow to connect")));
    m_toast = new QLabel;
    statusBar()->addPermanentWidget(m_toast);
    statusBar()->addPermanentWidget(m_zoomLabel);
    m_toastTimer.setSingleShot(true);
    m_toastTimer.setInterval(4000);
    connect(&m_toastTimer, &QTimer::timeout, m_toast, &QLabel::clear);

    buildMenus();

    connect(m_home, &HomeScreen::newRequested, this, &MainWindow::newChart);
    connect(m_home, &HomeScreen::openFolderRequested, this, [this](const QString& p) {
        if (p.isEmpty()) openFolderDialog();
        else openFolder(p);
    });
    connect(m_home, &HomeScreen::openRequested, this, [this](const QString& p) {
        if (p.isEmpty()) openDialog();
        else openFile(p);
    });
    connect(m_palette, &ShapePalette::shapeClicked, this, [this](FlowShape s) {
        if (!cur()) return;
        scene()->addNode(s, view()->visibleCenter());
        view()->setFocus();
    });
    connect(m_panelToggle, &QToolButton::clicked, m_panelAction, &QAction::toggle);

    m_autosave.setInterval(QSettings().value("autosaveMinutes", 1).toInt() * 60'000);
    connect(&m_autosave, &QTimer::timeout, this, &MainWindow::autosave);
    m_autosave.start();
    // Like VS Code: a file changed outside the app reloads if its chart has no unsaved edits;
    // otherwise saving asks before overwriting it. Watch events can be lost (an editor that deletes
    // and recreates the file), so coming back to the app checks every chart too.
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
        if (OpenChart* t = findChart(path)) syncFromDisk(t);
        if (QFileInfo::exists(path) && !m_watcher.files().contains(path)) m_watcher.addPath(path);  // replaced by rename: watch it again
    });
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState st) {
        if (st != Qt::ApplicationActive) return;
        for (auto* t : QList<OpenChart*>(m_charts)) syncFromDisk(t);
        watchCharts();
    });

    applyTheme(QSettings().value("dark", false).toBool());
    setPaletteCollapsed(QSettings().value("paletteCollapsed", false).toBool(), false);
    setFolderCollapsed(QSettings().value("folderCollapsed", false).toBool(), false);
    showHome();
}

// ---------------------------------------------------------------- menus

QAction* MainWindow::makeAction(QMenu* menu, const QString& text, const QList<QKeySequence>& keys,
                                const std::function<void()>& fn, bool chartOnly) {
    auto* a = menu->addAction(text);
    a->setShortcuts(keys);
    connect(a, &QAction::triggered, this, fn);
    if (chartOnly) m_chartActions << a;
    return a;
}

void MainWindow::buildMenus() {
    auto* file = menuBar()->addMenu(tr("&File"));
    makeAction(file, tr("&New chart"), {QKeySequence::New}, [this] { newChart(); }, false);
    makeAction(file, tr("&Open…"), {QKeySequence::Open}, [this] { openDialog(); }, false);
    makeAction(file, tr("Open &folder…"), {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O)}, [this] { openFolderDialog(); }, false);
    file->addSeparator();
    makeAction(file, tr("&Save"), {QKeySequence::Save}, [this] { save(cur()); });
    makeAction(file, tr("Save a&ll"), {QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S)}, [this] { saveAll(); }, false);
    makeAction(file, tr("Save &as…"), {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S)}, [this] { saveAs(cur()); });
    makeAction(file, tr("&Export as PNG / SVG / PDF…"), {QKeySequence(Qt::CTRL | Qt::Key_E)}, [this] { exportChart(); });
    makeAction(file, tr("Export for &LLM (text)…"), {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E)},
               [this] { exportChart(true); });
    makeAction(file, tr("Export as up&date for LLM…"), {}, [this] { exportLlmUpdate(); });
    makeAction(file, tr("A&uto-save interval…"), {}, [this] {
        bool ok = false;
        const int min = QInputDialog::getInt(this, tr("Auto-save"), tr("Save every (minutes):"),
                                             m_autosave.interval() / 60'000, 1, 120, 1, &ok);
        if (!ok) return;
        QSettings().setValue("autosaveMinutes", min);
        m_autosave.start(min * 60'000);
    }, false);
    file->addSeparator();
    makeAction(file, tr("E&xit"), {QKeySequence(Qt::ALT | Qt::Key_F4)}, [this] { close(); }, false);

    auto* edit = menuBar()->addMenu(tr("&Edit"));
    auto* undo = m_undoGroup->createUndoAction(this, tr("&Undo"));
    undo->setShortcuts(QKeySequence::Undo);
    auto* redo = m_undoGroup->createRedoAction(this, tr("&Redo"));
    redo->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Y), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
    edit->addAction(undo);
    edit->addAction(redo);
    edit->addSeparator();
    makeAction(edit, tr("Cu&t"), {QKeySequence::Cut}, [this] {
        copy();
        scene()->deleteSelected();
    });
    makeAction(edit, tr("&Copy"), {QKeySequence::Copy}, [this] { copy(); });
    makeAction(edit, tr("&Paste"), {QKeySequence::Paste}, [this] { paste(); });
    makeAction(edit, tr("&Duplicate"), {QKeySequence(Qt::CTRL | Qt::Key_D)},
               [this] { scene()->paste(scene()->copySelection(), {20, 20}); });
    makeAction(edit, tr("De&lete"), {QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)},
               [this] { scene()->deleteSelected(); });
    edit->addSeparator();
    makeAction(edit, tr("&Find && replace"), {QKeySequence::Find}, [this] { m_find->open(); });
    makeAction(edit, tr("Select &all"), {QKeySequence::SelectAll}, [this] { scene()->selectAll(); });
    makeAction(edit, tr("C&hange history"), {QKeySequence(Qt::ALT | Qt::Key_C)},
               [this] { setHistoryOpen(m_historyBox->isHidden()); });
    // Shapes first, so a selected container then wraps them at their fitted size.
    makeAction(edit, tr("&Fit to content"), {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F)}, [this] {
        scene()->fitSelectedShapes();
        scene()->fitSelectedContainers();
    });
    makeAction(edit, tr("&Reset link path"), {}, [this] {
        scene()->editSelected(QString(), [](ChartItem* c) {
            if (auto* l = qgraphicsitem_cast<LinkItem*>(c)) l->path.clear();
        });
    });

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    makeAction(viewMenu, tr("&Fit whole chart"), {QKeySequence(Qt::CTRL | Qt::Key_1)}, [this] { view()->fitAll(); });
    makeAction(viewMenu, tr("Actual size (100%)"), {QKeySequence(Qt::CTRL | Qt::Key_0)}, [this] { view()->resetZoom(); });
    makeAction(viewMenu, tr("Zoom &in"), {QKeySequence(Qt::CTRL | Qt::Key_Equal), QKeySequence(Qt::CTRL | Qt::Key_Plus)},
               [this] { view()->zoomBy(1.25, false); });
    makeAction(viewMenu, tr("Zoom &out"), {QKeySequence(Qt::CTRL | Qt::Key_Minus)}, [this] { view()->zoomBy(0.8, false); });
    viewMenu->addSeparator();
    auto* snap = makeAction(viewMenu, tr("&Snap to grid && guides"), {}, [] {});
    snap->setCheckable(true);
    snap->setChecked(QSettings().value("snap", true).toBool());
    m_snap = snap->isChecked();
    connect(snap, &QAction::toggled, this, [this](bool on) {
        m_snap = on;
        for (auto* t : std::as_const(m_charts)) t->scene->snapEnabled = on;
        QSettings().setValue("snap", on);
    });
    auto* minimap = makeAction(viewMenu, tr("&Minimap"), {}, [] {});
    minimap->setCheckable(true);
    minimap->setChecked(QSettings().value("minimap", true).toBool());
    m_minimap = minimap->isChecked();
    connect(minimap, &QAction::toggled, this, [this](bool on) {
        m_minimap = on;
        for (auto* t : std::as_const(m_charts)) t->view->setMinimapVisible(on);
        QSettings().setValue("minimap", on);
    });
    minimap->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
    makeAction(edit, tr("&Bundle selected links"), {QKeySequence(Qt::CTRL | Qt::Key_L)}, [this] { scene()->bundleSelectedLinks(); });
    makeAction(viewMenu, tr("Shape panel: icons only"), {QKeySequence(Qt::CTRL | Qt::Key_B)},
               [this] { setPaletteCollapsed(!m_paletteCollapsed); });
    // Style panel starts closed every launch and opens only on request, not on selection.
    m_panelAction = makeAction(viewMenu, tr("S&tyle panel"), {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B)}, [] {});
    m_panelAction->setCheckable(true);
    connect(m_panelAction, &QAction::toggled, this, [this](bool on) {
        m_panelToggle->setText(on ? QStringLiteral("»") : QStringLiteral("«"));
        updatePanelVisibility();
    });
    makeAction(viewMenu, tr("F&older pane"), {QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_B)},
               [this] { setFolderCollapsed(!m_folderCollapsed); }, false);
    viewMenu->addSeparator();
    auto* dark =makeAction(viewMenu, tr("&Dark theme"), {}, [] {}, false);
    dark->setCheckable(true);
    dark->setChecked(QSettings().value("dark", false).toBool());
    connect(dark, &QAction::toggled, this, &MainWindow::applyTheme);

    auto* help = menuBar()->addMenu(tr("&Help"));
    makeAction(help, tr("&Keyboard shortcuts"), {QKeySequence(Qt::Key_F1)}, [this] { showShortcuts(); }, false);
}

void MainWindow::showShortcuts() {
    if (m_shortcutsPanel) {  // F1 again closes it
        m_shortcutsPanel->close();
        return;
    }
    // Menu shortcuts are read from the menus, so they never go stale; canvas keys live in ChartView.
    auto group = [](const QString& title, const QList<std::pair<QString, QString>>& rows) {
        QString h = QStringLiteral("<h3>%1</h3><table cellspacing=0 cellpadding=4>").arg(title.toHtmlEscaped());
        for (const auto& [keys, what] : rows)
            h += QStringLiteral("<tr><td style='padding-right:24px'><b>%1</b></td><td>%2</td></tr>")
                     .arg(keys.toHtmlEscaped(), what.toHtmlEscaped());
        return h + QStringLiteral("</table>");
    };
    QStringList groups;
    for (auto* m : menuBar()->actions()) {
        if (!m->menu()) continue;
        QList<std::pair<QString, QString>> rows;
        for (auto* a : m->menu()->actions()) {
            if (a->shortcuts().isEmpty()) continue;
            QStringList keys;
            for (const auto& k : a->shortcuts()) keys << k.toString(QKeySequence::NativeText);
            QString text = a->text();
            text.remove(QRegularExpression(QStringLiteral("&(?!&)")));  // mnemonic marks; "&&" becomes "&"
            rows.append({keys.join(QLatin1String(" / ")), text.remove(QStringLiteral("…"))});
        }
        if (!rows.isEmpty()) groups << group(QString(m->text()).remove('&'), rows);
    }
    groups << group(tr("Canvas"), {{tr("Ctrl+Arrow"), tr("Align selected shapes to that side")},
                                   {tr("Ctrl+Shift+Arrow"), tr("Align and space evenly")},
                                   {tr("Wheel"), tr("Zoom around the cursor")},
                                   {tr("Shift+Wheel"), tr("Pan sideways")},
                                   {tr("Space+drag / Right-drag / Middle-drag"), tr("Pan")},
                                   {tr("Alt while dragging"), tr("Place freely (no snap)")},
                                   {tr("Esc"), tr("Cancel link drag, clear selection")}})
           << group(tr("Text"), {{tr("Double-click"), tr("Edit a shape's text or a link's label")},
                                 {tr("Enter"), tr("Done")},
                                 {tr("Shift+Enter"), tr("New line")},
                                 {tr("Esc"), tr("Cancel")}});
    QString html = QStringLiteral("<table cellspacing=24><tr>");
    for (int i = 0; i < groups.size(); ++i) {
        if (i && i % 3 == 0) html += QStringLiteral("</tr><tr>");
        html += QStringLiteral("<td valign=top>") + groups[i] + QStringLiteral("</td>");
    }
    html += QStringLiteral("</tr></table>");

    // An overlay inside the window, not a modal dialog: a modal one blocked the window's own close button.
    auto* dlg = new QWidget(this);
    m_shortcutsPanel = dlg;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setAutoFillBackground(true);
    dlg->setGeometry(rect());  // covers the whole window; Esc, F1 or Close dismisses it
    new QShortcut(QKeySequence(Qt::Key_Escape), dlg, dlg, &QWidget::close, Qt::WidgetWithChildrenShortcut);
    auto* v = new QVBoxLayout(dlg);
    auto* top = new QHBoxLayout;
    auto* title = new QLabel(QStringLiteral("<h2>%1</h2>").arg(tr("Keyboard shortcuts")));
    auto* close = new QToolButton;
    close->setText(tr("Close (Esc)"));
    connect(close, &QToolButton::clicked, dlg, &QWidget::close);
    top->addWidget(title, 1);
    top->addWidget(close);
    v->addLayout(top);
    auto* body = new QLabel(html);
    body->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    auto* scroll = new QScrollArea;
    scroll->setWidget(body);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    v->addWidget(scroll, 1);
    dlg->show();
    close->setFocus();  // so Esc reaches the overlay
}

void MainWindow::resizeEvent(QResizeEvent* e) {
    QMainWindow::resizeEvent(e);
    if (m_shortcutsPanel) m_shortcutsPanel->setGeometry(rect());
}

// ---------------------------------------------------------------- screens & charts

bool MainWindow::onChart() const { return m_stack->currentWidget() == m_chartPage; }

void MainWindow::showHome() {
    m_find->close();
    setHistoryOpen(false);
    m_stack->setCurrentWidget(m_home);
    for (auto* a : std::as_const(m_chartActions)) a->setEnabled(false);
    m_rightAnim.stop();
    m_panelScroll->setFixedWidth(0);
    m_panelScroll->hide();
    m_zoomLabel->clear();
    statusBar()->hide();
    m_home->refresh();
    updateTitle();
}

void MainWindow::showChart() {
    if (onChart()) return;
    m_stack->setCurrentWidget(m_chartPage);
    statusBar()->show();
    setCurrent(m_cur);  // enables the chart actions if there is a chart
    updatePanelVisibility();  // reopen the style panel if it was open
}

QString MainWindow::currentPath() const { return m_cur ? m_cur->path : QString(); }
ChartScene* MainWindow::scene() const { return m_cur ? m_cur->scene : nullptr; }
ChartView* MainWindow::view() const { return m_cur ? m_cur->view : nullptr; }

OpenChart* MainWindow::findChart(const QString& path) const {
    for (auto* t : m_charts)
        if (samePath(t->path, path)) return t;
    return nullptr;
}

OpenChart* MainWindow::addChart() {
    auto* t = new OpenChart;
    t->scene = new ChartScene(this);
    t->scene->snapEnabled = m_snap;
    t->view = new ChartView(t->scene);
    t->view->setDark(m_dark);
    t->view->setMinimapVisible(m_minimap);
    m_views->addWidget(t->view);
    m_undoGroup->addStack(t->scene->undoStack());
    connect(t->scene->undoStack(), &QUndoStack::cleanChanged, this, [this, t] { chartStateChanged(t); });
    // Any edit, undo, redo or reload closes the history pane (its outlines would be stale).
    connect(t->scene->undoStack(), &QUndoStack::indexChanged, this, [this] { setHistoryOpen(false); });
    connect(t->scene, &ChartScene::edited, this, [this, t](const QString& text, const QString& before, bool merged) {
        if (merged)  // same step continued: the row keeps its "before", takes the new time
            for (int i = int(m_history.size()) - 1; i >= 0; --i)
                if (m_history[i].chart == t) {
                    m_history[i].time = QDateTime::currentDateTime();
                    return;
                }
        m_history.append({QDateTime::currentDateTime(), t, text, before});
    });
    connect(t->view, &ChartView::zoomChanged, this, [this, t](double z) {
        if (t == m_cur) m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(z * 100)));
    });
    m_charts << t;
    showChart();
    setCurrent(t);
    return t;
}

void MainWindow::setCurrent(OpenChart* t) {
    m_cur = t;
    for (auto* a : std::as_const(m_chartActions)) a->setEnabled(t && onChart());
    m_panel->setScene(t ? t->scene : nullptr);
    m_find->setView(t ? t->view : nullptr);
    if (!t) {
        m_find->close();
        m_views->setCurrentWidget(m_noChart);
        m_zoomLabel->clear();
        m_tree->clearSelection();
        updateTitle();
        return;
    }
    m_views->setCurrentWidget(t->view);
    m_undoGroup->setActiveStack(t->scene->undoStack());
    m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(t->view->zoom() * 100)));
    t->view->setFocus();
    m_pasteCount = 0;
    if (!m_folder.isEmpty()) {  // highlight its row (if it lives in the folder)
        const QModelIndex i = t->path.isEmpty() ? QModelIndex() : m_fsModel->index(t->path);
        if (i.isValid()) m_tree->setCurrentIndex(i);
        else m_tree->clearSelection();
    }
    updateTitle();
}

void MainWindow::chartStateChanged(OpenChart* t) {
    m_tree->viewport()->update();  // red dots
    if (t == m_cur) updateTitle();
}

void MainWindow::removeChart(OpenChart* t) {
    setHistoryOpen(false);
    m_history.removeIf([t](const HistoryRow& r) { return r.chart == t; });
    m_charts.removeOne(t);
    if (t == m_cur) setCurrent(nullptr);
    m_undoGroup->removeStack(t->scene->undoStack());
    delete t->view;
    delete t->scene;
    delete t;
    watchCharts();
}

void MainWindow::pathMoved(const QString& from, const QString& to) {
    for (auto* t : std::as_const(m_charts)) {
        if (samePath(t->path, from)) t->path = QFileInfo(to).absoluteFilePath();
        else if (t->path.startsWith(from + '/', Qt::CaseInsensitive))  // inside a moved folder
            t->path = QFileInfo(to + t->path.mid(from.size())).absoluteFilePath();
        else continue;
        chartStateChanged(t);
    }
    watchCharts();
}

void MainWindow::newChart() {
    if (!m_folder.isEmpty()) {
        newFileInFolder();
        return;
    }
    if (!maybeSave(m_cur)) return;
    OpenChart* old = m_cur;
    if (old) removeRecovery(old);
    OpenChart* t = addChart();
    if (old) removeChart(old);
    t->view->resetView();
}

bool MainWindow::newFileInFolder(const QString& dir) {
    if (m_folder.isEmpty()) return false;
    const QDir base(dir.isEmpty() ? m_folder : dir);
    QString path = base.filePath(tr("Untitled") + ".flowx");
    for (int n = 2; QFileInfo::exists(path); ++n) path = base.filePath(tr("Untitled %1").arg(n) + ".flowx");
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(QJsonObject{{"format", "flowx"}, {"version", 1}}).toJson()) < 0 ||
        !f.commit()) {
        QMessageBox::warning(this, tr("New file"), tr("Cannot create %1:\n%2").arg(QDir::toNativeSeparators(path), f.errorString()));
        return false;
    }
    if (!openFile(path)) return false;
    const QModelIndex i = m_fsModel->index(path);
    if (i.isValid()) {
        m_tree->expand(i.parent());  // a subfolder: the new row must be on screen to edit it
        m_tree->setCurrentIndex(i);
        m_tree->edit(i);  // name it right away; Esc keeps "Untitled"
    }
    return true;
}

bool MainWindow::newSubfolder(const QString& dir) {
    QString name = tr("New folder");
    for (int n = 2; QFileInfo::exists(QDir(dir).filePath(name)); ++n) name = tr("New folder %1").arg(n);
    const QModelIndex i = m_fsModel->mkdir(m_fsModel->index(dir), name);
    if (!i.isValid()) {
        QMessageBox::warning(this, tr("New folder"), tr("Cannot create a folder in %1.").arg(QDir::toNativeSeparators(dir)));
        return false;
    }
    m_tree->expand(i.parent());
    m_tree->setCurrentIndex(i);
    m_tree->edit(i);
    return true;
}

void MainWindow::showFolderMenu(const QModelIndex& index, const QPoint& globalPos) {
    if (m_folder.isEmpty()) return;
    const QString path = index.isValid() ? m_fsModel->filePath(index) : m_folder;
    const bool isDir = !index.isValid() || m_fsModel->isDir(index);
    const QPersistentModelIndex row(index);  // the file watcher may change the model while the menu is open
    QMenu menu(this);
    if (index.isValid()) {
        menu.addAction(tr("Rename"), this, [this, row] { if (row.isValid()) m_tree->edit(row); });
        menu.addAction(tr("Delete"), this, [this, path] { deletePath(path); });
    }
    if (isDir) {
        menu.addAction(tr("Export this folder for LLM…"), this, [this, path] { exportFolderDialog(path, false); });
        menu.addAction(tr("Export this folder as update for LLM…"), this, [this, path] { exportFolderDialog(path, true); });
    } else {  // the File menu's exports, on this chart (shown first, so it is clear which one)
        menu.addAction(tr("Export as PNG / SVG / PDF…"), this, [this, path] { if (openFile(path)) exportChart(); });
        menu.addAction(tr("Export for LLM (text)…"), this, [this, path] { if (openFile(path)) exportChart(true); });
        menu.addAction(tr("Export as update for LLM…"), this, [this, path] { if (openFile(path)) exportLlmUpdate(); });
    }
    menu.addSeparator();
    const QString dir = isDir ? path : QFileInfo(path).absolutePath();
    menu.addAction(tr("Create new file"), this, [this, dir] { newFileInFolder(dir); });
    menu.addAction(tr("Create new folder"), this, [this, dir] { newSubfolder(dir); });
    menu.exec(globalPos);
}

void MainWindow::deletePath(const QString& path) {
    const QFileInfo fi(path);
    QList<OpenChart*> inside;
    bool unsaved = false;
    for (auto* t : std::as_const(m_charts))
        if (samePath(t->path, path) || t->path.startsWith(fi.absoluteFilePath() + '/', Qt::CaseInsensitive)) {
            inside << t;
            unsaved |= !t->scene->undoStack()->isClean();
        }
    QString q = (fi.isDir() ? tr("Delete the folder \"%1\" and everything in it?") : tr("Delete \"%1\"?")).arg(fi.fileName());
    q += '\n' + tr("It goes to the Recycle Bin.");
    if (unsaved) q += '\n' + tr("Unsaved changes to the open chart(s) there are lost.");
    if (QMessageBox::question(this, tr("Delete"), q) != QMessageBox::Yes) return;
    if (!QFile::moveToTrash(path)) {
        QMessageBox::warning(this, tr("Delete"), tr("Cannot delete %1.").arg(QDir::toNativeSeparators(path)));
        return;
    }
    for (auto* t : std::as_const(inside)) {
        removeRecovery(t);
        removeChart(t);
    }
}

void MainWindow::openDialog() {
    const QString dir = m_cur && !m_cur->path.isEmpty() ? QFileInfo(m_cur->path).absolutePath()
                                                        : QSettings().value("lastDir").toString();
    const QString path = QFileDialog::getOpenFileName(this, tr("Open chart"), dir, kFilter + ";;" + tr("All files (*)"));
    if (!path.isEmpty()) openFile(path);
}

void MainWindow::openFolderDialog() {
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Open folder"),
                                                          m_folder.isEmpty() ? QSettings().value("lastDir").toString() : m_folder);
    if (!dir.isEmpty()) openFolder(dir);
}

// ponytail: charts opened earlier stay loaded when another folder is opened (dirty ones are still
// asked about on exit). Drop clean charts from outside the new folder if memory ever matters.
void MainWindow::openFolder(const QString& dir) {
    m_folder = QFileInfo(dir).absoluteFilePath();
    QSettings().setValue("lastDir", m_folder);
    HomeScreen::addRecentFolder(m_folder);
    m_tree->setRootIndex(m_fsModel->setRootPath(m_folder));
    m_folderName->setText(QDir(m_folder).dirName());
    m_folderName->setToolTip(QDir::toNativeSeparators(m_folder));
    m_folderBox->show();
    m_folderGap->show();
    showChart();
    setCurrent(m_cur);  // select its row if it is in this folder
}

void MainWindow::setFolderCollapsed(bool collapsed, bool animate) {
    m_folderCollapsed = collapsed;
    QSettings().setValue("folderCollapsed", collapsed);
    m_folderToggle->setText(collapsed ? QStringLiteral("»") : QStringLiteral("«"));
    if (!collapsed) {  // tree back before growing; hidden after shrinking
        m_folderBody->show();
        m_folderName->show();
    }
    const int to = collapsed ? kFolderClosed : kFolderOpen;
    m_folderAnim.stop();
    if (!animate || !m_folderBox->isVisible()) {
        m_folderBox->setFixedWidth(to);
        m_folderBody->setVisible(!collapsed);
        m_folderName->setVisible(!collapsed);
        return;
    }
    m_folderAnim.setStartValue(m_folderBox->width());
    m_folderAnim.setEndValue(to);
    m_folderAnim.start();
}

bool MainWindow::openFile(const QString& path) {
    const QString abs = QFileInfo(path).absoluteFilePath();
    if (OpenChart* t = findChart(abs)) {  // already loaded: show it
        showChart();
        setCurrent(t);
        return true;
    }
    QJsonObject o;
    if (const QString error = readChart(path, &o); !error.isEmpty()) {
        QMessageBox::warning(this, tr("Cannot open chart"), error);
        if (!QFileInfo(path).isReadable()) {  // gone or locked: off the recent list
            HomeScreen::removeRecent(path);
            if (!onChart()) m_home->refresh();
        }
        return false;
    }
    // Without a folder the file replaces the current chart. With one, the current chart stays
    // loaded, unless it is an empty Untitled chart nobody touched (e.g. right after Ctrl+N).
    OpenChart* old = m_cur;
    if (old && !m_folder.isEmpty() &&
        !(old->path.isEmpty() && old->scene->itemCount() == 0 && old->scene->undoStack()->count() == 0))
        old = nullptr;
    if (old && m_folder.isEmpty()) {
        if (!maybeSave(old)) return false;
        removeRecovery(old);
    }
    OpenChart* t = addChart();
    t->scene->fromJson(o);
    t->path = abs;
    t->disk = chartContent(o);
    watchCharts();
    QSettings().setValue("lastDir", QFileInfo(abs).absolutePath());
    HomeScreen::addRecent(abs);
    if (o.contains("view")) t->view->setViewState(o["view"].toObject());
    else QTimer::singleShot(0, t->view, &ChartView::fitAll);  // after layout gives the view its size
    if (old) removeChart(old);
    setCurrent(t);  // now with its path: selects its row
    return true;
}

void MainWindow::closeEvent(QCloseEvent* e) {
    for (auto* t : std::as_const(m_charts)) {
        if (t->scene->undoStack()->isClean()) continue;
        setCurrent(t);  // show the chart the save question is about
        if (!maybeSave(t)) return e->ignore();
    }
    for (auto* t : std::as_const(m_charts)) removeRecovery(t);
    e->accept();
}

void MainWindow::saveAll() {
    OpenChart* was = m_cur;
    for (auto* t : std::as_const(m_charts))
        if (!t->scene->undoStack()->isClean()) {
            if (t->path.isEmpty()) setCurrent(t);  // Save as asks for a name: show which chart
            if (!save(t)) break;
        }
    if (m_cur != was) setCurrent(was);
}

void MainWindow::updateTitle() {
    OpenChart* t = onChart() ? m_cur : nullptr;
    if (!t) {
        setWindowTitle(tr("Flowchart"));
        setWindowModified(false);
        return;
    }
    const QString name = t->path.isEmpty() ? tr("Untitled") : QFileInfo(t->path).completeBaseName();
    setWindowTitle(tr("%1[*] — Flowchart").arg(name));
    setWindowModified(!t->scene->undoStack()->isClean());
}

// ---------------------------------------------------------------- saving

bool MainWindow::maybeSave(OpenChart* t) {
    if (!t || t->scene->undoStack()->isClean()) return true;
    const QString name = t->path.isEmpty() ? tr("Untitled") : QFileInfo(t->path).fileName();
    const auto r = QMessageBox::warning(this, tr("Unsaved changes"), tr("Save changes to \"%1\"?").arg(name),
                                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (r == QMessageBox::Save) return save(t);
    return r == QMessageBox::Discard;
}

bool MainWindow::save(OpenChart* t) {
    if (!t) return false;
    return t->path.isEmpty() ? saveAs(t) : writeFile(t, t->path);
}

bool MainWindow::saveAs(OpenChart* t) {
    if (!t) return false;
    const QString start = t->path.isEmpty()
                              ? QDir(QSettings().value("lastDir", QDir::homePath()).toString()).filePath(tr("Untitled.flowx"))
                              : t->path;
    QString path = QFileDialog::getSaveFileName(this, tr("Save chart"), start, kFilter);
    if (path.isEmpty()) return false;
    if (!path.endsWith(".flowx", Qt::CaseInsensitive)) path += ".flowx";
    return writeFile(t, path);
}

bool MainWindow::writeFile(OpenChart* t, const QString& path, bool quiet) {
    if (samePath(path, t->path) && changedOnDisk(t)) {
        if (quiet) return false;
        QMessageBox box(QMessageBox::Warning, tr("File changed on disk"),
                        tr("\"%1\" was changed outside Flowchart after it was opened or last saved.\n"
                           "Overwrite it with your version?").arg(QFileInfo(path).fileName()),
                        QMessageBox::Cancel, this);
        QPushButton* overwrite = box.addButton(tr("Overwrite"), QMessageBox::DestructiveRole);
        box.setDefaultButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() != overwrite) return false;
    }
    QJsonObject o = t->scene->toJson();
    o["view"] = t->view->viewState();
    o["thumb"] = QString::fromLatin1(thumbnailPng(t->scene).toBase64());
    const QByteArray bytes = QJsonDocument(o).toJson();
    QSaveFile f(path);  // writes to a temp file and renames, so a crash never leaves half a file
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) < 0 || !f.commit()) {
        QMessageBox::warning(this, tr("Save failed"), tr("Cannot save %1:\n%2").arg(QDir::toNativeSeparators(path), f.errorString()));
        return false;
    }
    t->path = QFileInfo(path).absoluteFilePath();
    t->disk = chartContent(QJsonDocument::fromJson(bytes).object());  // as read back, so it compares equal later
    watchCharts();
    QSettings().setValue("lastDir", QFileInfo(t->path).absolutePath());
    t->scene->undoStack()->setClean();
    t->scene->chartIdSaved = true;
    removeRecovery(t);
    HomeScreen::addRecent(t->path);
    chartStateChanged(t);
    return true;
}

// The file holds other shapes/links than when it was last read or written here. A file that is
// gone does not count (saving recreates it); one that no longer parses does (someone wrote it).
bool MainWindow::changedOnDisk(OpenChart* t) const {
    if (t->path.isEmpty() || t->disk.isEmpty() || !QFileInfo::exists(t->path)) return false;
    QJsonObject o;
    return !readChart(t->path, &o).isEmpty() || chartContent(o) != t->disk;
}

void MainWindow::syncFromDisk(OpenChart* t) {
    if (t->path.isEmpty() || t->disk.isEmpty()) return;
    QJsonObject o;
    if (!readChart(t->path, &o).isEmpty()) return;  // gone, locked or half written: the next change event retries
    const QJsonObject content = chartContent(o);
    if (content == t->disk) return;  // our own write, or only the thumbnail/view/id changed
    if (!t->scene->undoStack()->isClean()) {
        statusBar()->showMessage(tr("%1 changed on disk. Saving will ask before overwriting it.")
                                     .arg(QFileInfo(t->path).fileName()), 10'000);
        return;
    }
    if (QApplication::mouseButtons() != Qt::NoButton) {  // mid-drag: reload once the mouse is released
        QTimer::singleShot(300, this, [this, t] {
            if (m_charts.contains(t)) syncFromDisk(t);
        });
        return;
    }
    t->disk = content;
    t->scene->fromJson(o);  // keeps the current zoom and scroll
    chartStateChanged(t);
}

void MainWindow::watchCharts() {
    if (!m_watcher.files().isEmpty()) m_watcher.removePaths(m_watcher.files());
    for (auto* t : std::as_const(m_charts))
        if (!t->path.isEmpty() && QFileInfo::exists(t->path)) m_watcher.addPath(t->path);
}

QByteArray MainWindow::thumbnailPng(ChartScene* s) {
    QImage img(320, 200, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    const QRectF r = s->contentRect();
    if (!r.isNull()) {
        OverlaysHidden guard(s);
        QPainter p(&img);
        p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
        s->render(&p, QRectF(8, 8, 304, 184), r, Qt::KeepAspectRatio);
    }
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return bytes;
}

// Recovery copies live here as autosave*.flowx: one per tab with unsaved work that has no file
// (or whose file could not be written). Any left at startup mean the app did not close normally.
// ponytail: two app instances running at once each see the other's live copies as left over.
// Add a lock file per instance if that becomes a real workflow.
QString MainWindow::autosaveDir() const { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation); }

void MainWindow::removeRecovery(OpenChart* t) {
    if (t->recovery.isEmpty()) return;
    QFile::remove(t->recovery);
    t->recovery.clear();
}

void MainWindow::autosave() {
    for (auto* t : std::as_const(m_charts)) {
        if (t->scene->undoStack()->isClean()) continue;
        if (!t->path.isEmpty() && writeFile(t, t->path, true)) continue;  // saved chart: write the file itself
        // Untitled, the file changed on disk meanwhile, or the write failed: keep a recovery copy instead.
        QJsonObject o = t->scene->toJson();
        o["view"] = t->view->viewState();
        o["origPath"] = t->path;
        QDir().mkpath(autosaveDir());
        if (t->recovery.isEmpty())
            t->recovery = autosaveDir() + "/autosave-" + QUuid::createUuid().toString(QUuid::Id128).left(12) + ".flowx";
        QSaveFile f(t->recovery);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
            f.commit();
        }
    }
}

void MainWindow::start(const QString& fileArg) {
    const QFileInfoList left =
        QDir(autosaveDir()).entryInfoList({"autosave*.flowx"}, QDir::Files, QDir::Time | QDir::Reversed);
    if (!left.isEmpty()) {
        const auto answer = QMessageBox::question(
            this, tr("Recover charts"),
            tr("Flowchart did not close normally last time.\nRecover the unsaved chart(s)? (%1)").arg(left.size()));
        for (const QFileInfo& fi : left) {
            QFile f(fi.absoluteFilePath());
            if (answer == QMessageBox::Yes && f.open(QIODevice::ReadOnly)) {
                const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
                f.close();
                // ponytail: without a folder only the last recovered chart is shown; the others stay
                // loaded and are asked about on exit. Add a picker if several recoveries become common.
                OpenChart* t = addChart();
                t->scene->fromJson(o);
                t->path = o["origPath"].toString();
                t->recovery = fi.absoluteFilePath();  // kept until this chart is saved or closed
                t->view->setViewState(o["view"].toObject());
                t->scene->undoStack()->resetClean();  // recovered work counts as unsaved
                chartStateChanged(t);
            } else {
                f.remove();
            }
        }
        updateTitle();
    }
    if (!fileArg.isEmpty()) openFile(fileArg);
}

// ---------------------------------------------------------------- export & clipboard

void MainWindow::exportChart(bool forLlm) {
    if (!cur()) return;
    const QRectF r = scene()->contentRect();
    if (r.isNull()) {
        QMessageBox::information(this, tr("Export"), tr("The chart is empty."));
        return;
    }
    const QString base = cur()->path.isEmpty() ? tr("Untitled") : QFileInfo(cur()->path).completeBaseName();
    // Each chart remembers where it was last exported (keyed by its file path; "" for untitled).
    QVariantMap exportDirs = QSettings().value("exportDirs").toMap();
    const QString dir = exportDirs.value(cur()->path, cur()->path.isEmpty() ? QSettings().value("lastDir", QDir::homePath()).toString()
                                                                  : QFileInfo(cur()->path).absolutePath()).toString();
    QString filter;
    const QStringList exts = forLlm ? QStringList{"md", "txt"} : QStringList{"png", "svg", "pdf"};
    const QString filters = forLlm ? tr("Markdown for LLM (*.md);;Plain text for LLM (*.txt)")
                                   : tr("PNG image (*.png);;SVG image (*.svg);;PDF document (*.pdf)");
    QString path = QFileDialog::getSaveFileName(this, forLlm ? tr("Export for LLM") : tr("Export chart"),
                                                QDir(dir).filePath(base + "." + exts[0]), filters, &filter);
    if (path.isEmpty()) return;
    if (!exts.contains(QFileInfo(path).suffix().toLower())) {
        QString ext = exts[0];
        for (const QString& e : exts)
            if (filter.contains("*." + e)) ext = e;
        path += "." + ext;
    }

    exportDirs[cur()->path] = QFileInfo(path).absolutePath();
    QSettings().setValue("exportDirs", exportDirs);
    if (forLlm) stampChartId(cur()->path, scene());
    const bool ok = scene()->exportTo(path);
    if (!ok) QMessageBox::warning(this, tr("Export failed"), tr("Cannot write %1.").arg(QDir::toNativeSeparators(path)));
}

void MainWindow::exportLlmUpdate() {
    if (!cur()) return;
    QVariantMap exportDirs = QSettings().value("exportDirs").toMap();
    const QString dir = exportDirs.value(cur()->path, cur()->path.isEmpty() ? QSettings().value("lastDir").toString()
                                                                            : QFileInfo(cur()->path).absolutePath()).toString();
    const QString path = QFileDialog::getOpenFileName(this, tr("Pick the earlier LLM export to update"), dir,
                                                      tr("LLM export (*.md *.txt)"));
    if (path.isEmpty()) return;
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Export failed"), tr("Cannot read %1:\n%2").arg(QDir::toNativeSeparators(path), in.errorString()));
        return;
    }
    QString error;
    const QString text = scene()->toLlmText(QFileInfo(path).completeBaseName(), QString::fromUtf8(in.readAll()), &error);
    in.close();
    if (text.isNull()) {
        QMessageBox::warning(this, tr("Cannot update"), error);
        return;
    }
    stampChartId(cur()->path, scene());
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(text.toUtf8()) < 0 || !out.commit()) {
        QMessageBox::warning(this, tr("Export failed"), tr("Cannot write %1.").arg(QDir::toNativeSeparators(path)));
        return;
    }
    exportDirs[cur()->path] = QFileInfo(path).absolutePath();
    QSettings().setValue("exportDirs", exportDirs);
}

void MainWindow::exportFolderDialog(const QString& dir, bool update) {
    const QString name = QDir(dir).dirName();
    const QString target = QFileDialog::getExistingDirectory(
        this, update ? tr("Pick the folder holding the earlier export \"%1\"").arg(name) : tr("Export \"%1\" for LLM into").arg(name),
        QSettings().value("folderExportDir", QFileInfo(dir).absolutePath()).toString());
    if (target.isEmpty()) return;
    QSettings().setValue("folderExportDir", target);
    // Update: picking the earlier export itself (instead of the folder holding it) works too.
    const QString out = update && QDir(target).dirName() == name && !QDir(target).exists(name) ? target : QDir(target).filePath(name);
    QStringList skipped;
    const int n = exportFolderForLlm(dir, out, update, &skipped);
    QString msg = tr("Exported %n chart(s) to %1.", nullptr, n).arg(QDir::toNativeSeparators(out));
    if (skipped.isEmpty()) return (void)QMessageBox::information(this, tr("Export for LLM"), msg);
    QMessageBox::warning(this, tr("Export for LLM"), msg + "\n\n" + tr("Skipped:") + "\n" + skipped.join("\n\n"));
}

int MainWindow::exportFolderForLlm(const QString& dir, const QString& out, bool update, QStringList* skipped) {
    // An open chart exports as shown (unsaved edits included), like File > Export; others from disk.
    return exportFolder(dir, out, update, skipped, [this](const QString& path) {
        const OpenChart* t = findChart(path);
        return t ? t->scene : nullptr;
    });
}

int MainWindow::runCli(const QStringList& args) {
    const QString usage = tr(
        "Usage: Flowchart.exe --export <chart.flowx | folder> [-o <out>] [--update]\n"
        "  chart, no -o:   prints the LLM outline (Markdown) to stdout\n"
        "  chart, -o:      writes it to <out> (.md or .txt)\n"
        "  folder, no -o:  prints every chart under it, one after another\n"
        "  folder, -o:     writes <out>/<same subfolders>/<chart>.md\n"
        "  --update:       rewrites the earlier export at <out> with changes marked (needs -o)\n");
    QString src, out;
    bool update = false;
    for (int i = 2; i < args.size(); ++i) {
        if (args[i] == "--update") update = true;
        else if ((args[i] == "-o" || args[i] == "--out") && i + 1 < args.size()) out = args[++i];
        else if (src.isEmpty() && !args[i].startsWith('-')) src = args[i];
        else return writeStd(true, usage), 2;
    }
    if (src.isEmpty() || (update && out.isEmpty())) return writeStd(true, usage), 2;
    const QFileInfo fi(src);
    QString error;
    if (fi.isDir()) {
        if (!out.isEmpty()) {
            QStringList skipped;
            const int n = exportFolder(src, out, update, &skipped, {});
            writeStd(false, tr("Exported %n chart(s) to %1\n", nullptr, n).arg(QDir::toNativeSeparators(out)));
            if (!skipped.isEmpty()) writeStd(true, tr("Skipped:\n") + skipped.join('\n') + '\n');
            return skipped.isEmpty() ? 0 : 1;
        }
        int failed = 0;
        for (const QString& path : chartsUnder(src)) {
            const QString rel = QDir(src).relativeFilePath(path);
            const QString text = llmText(path, nullptr, rel.chopped(QFileInfo(rel).suffix().size() + 1), {}, &error, false);
            if (text.isNull()) {
                writeStd(true, rel + ": " + error + '\n');
                ++failed;
            } else {
                writeStd(false, text + "\n---\n\n");
            }
        }
        return failed ? 1 : 0;
    }
    if (!fi.isFile()) return writeStd(true, tr("No such file or folder: %1\n").arg(src)), 1;
    QString previous;  // null: fresh export
    if (update) {
        QFile f(out);
        if (!f.open(QIODevice::ReadOnly))
            return writeStd(true, tr("Cannot read the earlier export %1: %2\n").arg(out, f.errorString())), 1;
        previous = QString::fromUtf8(f.readAll());
    }
    const QString text = llmText(fi.absoluteFilePath(), nullptr, QFileInfo(out.isEmpty() ? src : out).completeBaseName(), previous, &error, !out.isEmpty());
    if (text.isNull()) return writeStd(true, error + '\n'), 1;
    if (out.isEmpty()) return writeStd(false, text), 0;
    QSaveFile f(out);
    if (!f.open(QIODevice::WriteOnly) || f.write(text.toUtf8()) < 0 || !f.commit())
        return writeStd(true, tr("Cannot write %1\n").arg(out)), 1;
    writeStd(false, tr("Wrote %1\n").arg(QDir::toNativeSeparators(out)));
    return 0;
}

void MainWindow::copy() {
    if (!cur()) return;
    const QJsonObject clip = scene()->copySelection();
    if (clip["nodes"].toArray().isEmpty()) return;
    auto* md = new QMimeData;
    md->setData(kClipMime, QJsonDocument(clip).toJson(QJsonDocument::Compact));
    QGuiApplication::clipboard()->setMimeData(md);
    m_pasteCount = 0;
}

void MainWindow::paste() {
    const QMimeData* md = QGuiApplication::clipboard()->mimeData();
    if (!md || !md->hasFormat(kClipMime) || !cur()) return;
    const QJsonObject clip = QJsonDocument::fromJson(md->data(kClipMime)).object();
    // Center the pasted shapes on the mouse when it is over the chart; else step off the source.
    const QPoint mouse = view()->viewport()->mapFromGlobal(QCursor::pos());
    if (view()->viewport()->rect().contains(mouse)) {
        QRectF box;
        for (const auto& v : clip["nodes"].toArray()) {
            const QJsonObject n = v.toObject();
            const double w = n["w"].toDouble(), h = n["h"].toDouble();
            box |= QRectF(n["x"].toDouble() - w / 2, n["y"].toDouble() - h / 2, w, h);
        }
        return scene()->paste(clip, scene()->snap(view()->mapToScene(mouse) - box.center()));
    }
    ++m_pasteCount;
    scene()->paste(clip, QPointF(20, 20) * m_pasteCount);
}

// ---------------------------------------------------------------- theme

void MainWindow::applyTheme(bool dark) {
    // Override the Windows light/dark setting; Fusion then picks the matching palette.
    QGuiApplication::styleHints()->setColorScheme(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
    QApplication::setPalette(QApplication::style()->standardPalette());
    m_dark = dark;
    for (auto* t : std::as_const(m_charts)) t->view->setDark(dark);
    m_palette->rebuildIcons(QApplication::palette().color(QPalette::Text));
    QSettings().setValue("dark", dark);
    if (!onChart()) m_home->refresh();
}

// ---------------------------------------------------------------- side panels

void MainWindow::setPaletteCollapsed(bool collapsed, bool animate) {
    m_paletteCollapsed = collapsed;
    QSettings().setValue("paletteCollapsed", collapsed);
    m_paletteToggle->setText(collapsed ? QStringLiteral("»") : QStringLiteral("«"));
    m_shortcutsButton->setText(collapsed ? QStringLiteral("⌨") : tr("⌨  Shortcuts"));
    if (!collapsed) m_palette->setCompact(false);  // names back before growing; removed after shrinking
    const int to = collapsed ? kPaletteClosed : kPaletteOpen;
    m_leftAnim.stop();
    if (!animate) {
        setPaletteWidth(to);
        m_palette->setCompact(collapsed);
        return;
    }
    m_leftAnim.setStartValue(m_paletteBox->width());
    m_leftAnim.setEndValue(to);
    m_leftAnim.start();
}

// Section titles shrink smoothly with the panel: full size open, 75% (fits "Containers") collapsed.
void MainWindow::setPaletteWidth(int w) {
    m_paletteBox->setFixedWidth(w);
    m_palette->setHeaderScale(0.75 + 0.25 * (w - kPaletteClosed) / double(kPaletteOpen - kPaletteClosed));
}

// ---------------------------------------------------------------- history pane (Alt+C)

void MainWindow::setHistoryOpen(bool open) {
    if (open == !m_historyBox->isHidden()) return;
    m_historyPick = -1;
    m_historyPrompt.clear();
    for (auto* t : std::as_const(m_charts)) {
        t->historyChanged = false;
        if (!t->scene->historyMarks.isEmpty()) {
            t->scene->historyMarks.clear();
            t->scene->update();
        }
    }
    m_tree->viewport()->update();
    m_historyList->clear();
    m_historyBox->setVisible(open);
    if (!open) {
        if (view()) view()->setFocus();
        return;
    }
    for (int i = int(m_history.size()) - 1; i >= 0; --i) {
        const HistoryRow& r = m_history[i];
        auto* item = new QTreeWidgetItem(m_historyList, {r.time.toString("HH:mm:ss"),
                                                         r.chart->path.isEmpty() ? tr("Untitled") : QFileInfo(r.chart->path).fileName()});
        item->setData(0, Qt::UserRole, i);
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setToolTip(1, r.text + "\n" + QDir::toNativeSeparators(r.chart->path));
    }
    m_historyList->setFocus();
}

void MainWindow::pickHistory(int row) {
    m_historyPick = row;
    m_historyPrompt.clear();
    for (auto* t : std::as_const(m_charts)) {
        t->historyChanged = false;
        t->scene->historyMarks.clear();
        // The chart's first step at or after `row` holds how it looked at that time.
        auto first = std::find_if(m_history.begin() + row, m_history.end(), [t](const HistoryRow& r) { return r.chart == t; });
        if (first != m_history.end()) {
            const QString title = t->path.isEmpty() ? tr("Untitled") : QFileInfo(t->path).completeBaseName();
            QString text = t->scene->toLlmText(title, first->before, nullptr, &t->scene->historyMarks, &t->historyChanged);
            if (text.isNull()) {  // chart id changed (reloaded from another file): send it whole
                text = t->scene->toLlmText(title);
                t->historyChanged = true;
            }
            // Under the "# Flowchart: <title>" line: where the chart lives, so an LLM can edit the file.
            text.insert(text.indexOf('\n') + 1, "\nFile: " + (t->path.isEmpty() ? tr("(not saved yet)") : QDir::toNativeSeparators(t->path)) + "\n");
            if (t->historyChanged) m_historyPrompt +=(m_historyPrompt.isEmpty() ? "" : "\n---\n\n") + text;
        }
        t->scene->update();
    }
    m_tree->viewport()->update();
    setCurrent(m_history[row].chart);
    m_historyList->setFocus();  // setCurrent focuses the chart
}

void MainWindow::toast(const QString& text) {
    m_toast->setText(text + "   ");
    m_toastTimer.start();
}

void MainWindow::updatePanelVisibility() {
    const int to = onChart() && m_panelAction->isChecked() ? kPanelOpen : 0;
    if (m_rightAnim.state() == QAbstractAnimation::Running ? m_rightAnim.endValue().toInt() == to
                                                            : m_panelScroll->width() == to)
        return;
    m_rightAnim.stop();
    m_rightAnim.setStartValue(m_panelScroll->width());
    m_rightAnim.setEndValue(to);
    m_rightAnim.start();
}
