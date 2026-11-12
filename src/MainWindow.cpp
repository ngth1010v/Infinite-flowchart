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
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QProxyStyle>
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
#include <QTabBar>
#include <QToolButton>
#include <QUndoGroup>
#include <QUndoStack>
#include <QUuid>

namespace {
constexpr int kPaletteOpen = 230, kPaletteClosed = 72, kPanelOpen = 300;
const QString kFilter = QObject::tr("Flowchart (*.flowx)");

// Hide selection/hover decorations while rendering to an image or file.
struct OverlaysHidden {
    ChartScene* s;
    explicit OverlaysHidden(ChartScene* scene) : s(scene) { s->overlaysHidden = true; }
    ~OverlaysHidden() { s->overlaysHidden = false; }
};

// Tabs with less room around the name and close button than the style's default.
class CompactTabStyle : public QProxyStyle {
public:
    CompactTabStyle() : QProxyStyle(QApplication::style()->name()) {}  // same look as the rest (Fusion)
    int pixelMetric(PixelMetric m, const QStyleOption* o, const QWidget* w) const override {
        const int v = QProxyStyle::pixelMetric(m, o, w);
        return m == PM_TabBarTabHSpace || m == PM_TabBarTabVSpace ? qRound(v * 0.6) : v;
    }
};

// Its default minimum width keeps room for the scroll arrows, wider than a single tab, and layouts
// never go below it: that left a gap before '+'. Now the bar is as wide as its tabs.
class TabBar : public QTabBar {
public:
    QSize minimumSizeHint() const override {
        const QSize s = QTabBar::minimumSizeHint();
        return {qMin(s.width(), sizeHint().width()), s.height()};
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

    for (auto* a : {&m_leftAnim, &m_rightAnim}) {
        a->setDuration(180);
        a->setEasingCurve(QEasingCurve::OutCubic);
    }
    connect(&m_leftAnim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& v) { m_paletteBox->setFixedWidth(v.toInt()); });
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

    // Tab bar across the top, like a browser: one tab per open chart, '+' opens more.
    m_tabs = new TabBar;
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    m_tabs->setExpanding(false);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->setElideMode(Qt::ElideRight);
    m_tabs->setFocusPolicy(Qt::NoFocus);
    auto* tabStyle = new CompactTabStyle;
    tabStyle->setParent(m_tabs);
    m_tabs->setStyle(tabStyle);
    auto* plus = new QToolButton;
    plus->setText(QStringLiteral("+"));
    plus->setFixedSize(22, 22);  // shorter than the tabs, so the row stays as tall as the tab bar
    plus->setToolTip(tr("Open charts in new tabs (Ctrl+O)"));
    QFont pf = plus->font();
    pf.setPointSizeF(pf.pointSizeF() * 1.3);
    plus->setFont(pf);
    connect(plus, &QToolButton::clicked, this, &MainWindow::openDialog);
    auto* tabRow = new QHBoxLayout;
    tabRow->setContentsMargins(0, 0, 0, 0);
    tabRow->setSpacing(3);
    tabRow->addWidget(m_tabs, 0, Qt::AlignTop);
    tabRow->addWidget(plus, 0, Qt::AlignVCenter);
    tabRow->addStretch(1);

    auto* h = new QHBoxLayout;
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    h->addWidget(m_paletteBox);
    h->addWidget(m_views, 1);
    h->addWidget(m_panelScroll);
    h->addWidget(panelStrip);
    m_chartPage = new QWidget;
    auto* v = new QVBoxLayout(m_chartPage);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addLayout(tabRow);
    v->addLayout(h, 1);

    m_home = new HomeScreen;
    m_stack = new QStackedWidget;
    m_stack->addWidget(m_home);
    m_stack->addWidget(m_chartPage);
    setCentralWidget(m_stack);

    m_zoomLabel = new QLabel;
    statusBar()->addWidget(new QLabel(tr("Wheel: zoom   ·   Right-drag / Space-drag / Shift+wheel: pan   ·   Ctrl+1: fit chart   ·   "
                                         "Alt while dragging: no snap   ·   Hover a shape and drag an arrow to connect")));
    statusBar()->addPermanentWidget(m_zoomLabel);

    buildMenus();

    connect(m_home, &HomeScreen::newRequested, this, &MainWindow::newChart);
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
    connect(m_tabs, &QTabBar::currentChanged, this, &MainWindow::currentTabChanged);
    connect(m_tabs, &QTabBar::tabCloseRequested, this, &MainWindow::closeTab);

    m_autosave.setInterval(QSettings().value("autosaveMinutes", 1).toInt() * 60'000);
    connect(&m_autosave, &QTimer::timeout, this, &MainWindow::autosave);
    m_autosave.start();

    applyTheme(QSettings().value("dark", false).toBool());
    setPaletteCollapsed(QSettings().value("paletteCollapsed", false).toBool(), false);
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
    file->addSeparator();
    makeAction(file, tr("&Save"), {QKeySequence::Save}, [this] { save(cur()); });
    makeAction(file, tr("Save &as…"), {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S)}, [this] { saveAs(cur()); });
    makeAction(file, tr("&Export as PNG / SVG / PDF…"), {QKeySequence(Qt::CTRL | Qt::Key_E)}, [this] { exportChart(); });
    makeAction(file, tr("Export for &LLM (text)…"), {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E)},
               [this] { exportChart(true); });
    makeAction(file, tr("A&uto-save interval…"), {}, [this] {
        bool ok = false;
        const int min = QInputDialog::getInt(this, tr("Auto-save"), tr("Save every (minutes):"),
                                             m_autosave.interval() / 60'000, 1, 120, 1, &ok);
        if (!ok) return;
        QSettings().setValue("autosaveMinutes", min);
        m_autosave.start(min * 60'000);
    }, false);
    file->addSeparator();
    makeAction(file, tr("&Close tab"), {QKeySequence(Qt::CTRL | Qt::Key_W)}, [this] { closeTab(m_tabs->currentIndex()); });
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
        for (int i = 0; i < m_tabs->count(); ++i) tab(i)->scene->snapEnabled = on;
        QSettings().setValue("snap", on);
    });
    auto* minimap = makeAction(viewMenu, tr("&Minimap"), {}, [] {});
    minimap->setCheckable(true);
    minimap->setChecked(QSettings().value("minimap", true).toBool());
    m_minimap = minimap->isChecked();
    connect(minimap, &QAction::toggled, this, [this](bool on) {
        m_minimap = on;
        for (int i = 0; i < m_tabs->count(); ++i) tab(i)->view->setMinimapVisible(on);
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
    viewMenu->addSeparator();
    makeAction(viewMenu, tr("&Next tab"), {QKeySequence(Qt::CTRL | Qt::Key_Tab)},
               [this] { m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % m_tabs->count()); });
    makeAction(viewMenu, tr("&Previous tab"), {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab)},
               [this] { m_tabs->setCurrentIndex((m_tabs->currentIndex() + m_tabs->count() - 1) % m_tabs->count()); });
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

// ---------------------------------------------------------------- screens & tabs

bool MainWindow::onChart() const { return m_stack->currentWidget() == m_chartPage; }

void MainWindow::showHome() {
    m_find->close();
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
    for (auto* a : std::as_const(m_chartActions)) a->setEnabled(true);
    statusBar()->show();
    updatePanelVisibility();  // reopen the style panel if it was open
}

int MainWindow::tabCount() const { return m_tabs->count(); }

ChartTab* MainWindow::tab(int index) const {
    return static_cast<ChartTab*>(m_tabs->tabData(index).value<void*>());
}

ChartTab* MainWindow::cur() const { return m_tabs->count() ? tab(m_tabs->currentIndex()) : nullptr; }
ChartScene* MainWindow::scene() const { return cur() ? cur()->scene : nullptr; }
ChartView* MainWindow::view() const { return cur() ? cur()->view : nullptr; }

int MainWindow::indexOf(const ChartTab* t) const {
    for (int i = 0; i < m_tabs->count(); ++i)
        if (tab(i) == t) return i;
    return -1;
}

// New empty Untitled tab, made current.
ChartTab* MainWindow::addTab() {
    auto* t = new ChartTab;
    t->scene = new ChartScene(this);
    t->scene->snapEnabled = m_snap;
    t->view = new ChartView(t->scene);
    t->view->setDark(m_dark);
    t->view->setMinimapVisible(m_minimap);
    m_views->addWidget(t->view);
    m_undoGroup->addStack(t->scene->undoStack());
    connect(t->scene->undoStack(), &QUndoStack::cleanChanged, this, [this, t] {
        updateTabText(t);
        if (t == cur()) updateTitle();
    });
    connect(t->view, &ChartView::zoomChanged, this, [this, t](double z) {
        if (t == cur()) m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(z * 100)));
    });
    const int i = m_tabs->addTab(QString());
    m_tabs->setTabData(i, QVariant::fromValue<void*>(t));
    updateTabText(t);
    showChart();
    m_tabs->setCurrentIndex(i);
    currentTabChanged();  // also for the first tab, which QTabBar made current before its data was set
    return t;
}

void MainWindow::currentTabChanged() {
    ChartTab* t = cur();
    if (!t) return;
    m_views->setCurrentWidget(t->view);
    m_undoGroup->setActiveStack(t->scene->undoStack());
    m_panel->setScene(t->scene);
    m_find->setView(t->view);
    m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(t->view->zoom() * 100)));
    t->view->setFocus();
    m_pasteCount = 0;
    updateTitle();
}

void MainWindow::updateTabText(ChartTab* t) {
    const int i = indexOf(t);
    if (i < 0) return;
    const QString name = t->path.isEmpty() ? tr("Untitled") : QFileInfo(t->path).completeBaseName();
    m_tabs->setTabText(i, t->scene->undoStack()->isClean() ? name : name + QStringLiteral(" •"));
    m_tabs->setTabToolTip(i, t->path.isEmpty() ? tr("Not saved yet") : QDir::toNativeSeparators(t->path));
}

bool MainWindow::closeTab(int index) {
    ChartTab* t = tab(index);
    if (!t) return false;
    m_tabs->setCurrentIndex(index);  // show the chart the save question is about
    if (!maybeSave(t)) return false;
    removeRecovery(t);
    const bool last = m_tabs->count() == 1;
    removeTab(t);
    if (last) close();  // there is always at least one tab: closing the last one closes the app
    return true;
}

// Drops the tab without asking.
void MainWindow::removeTab(ChartTab* t) {
    if (m_tabs->count() == 1) {  // no other chart for the panels to follow
        m_panel->setScene(nullptr);
        m_find->setView(nullptr);
    }
    m_tabs->removeTab(indexOf(t));  // makes a neighbour current; currentTabChanged retargets the panels
    m_undoGroup->removeStack(t->scene->undoStack());
    delete t->view;
    delete t->scene;
    delete t;
}

void MainWindow::newChart() {
    ChartTab* t = addTab();
    t->view->resetView();
}

void MainWindow::openDialog() {
    const QString dir = cur() && !cur()->path.isEmpty() ? QFileInfo(cur()->path).absolutePath()
                                                        : QSettings().value("lastDir").toString();
    const QStringList paths =
        QFileDialog::getOpenFileNames(this, tr("Open charts"), dir, kFilter + ";;" + tr("All files (*)"));
    for (const QString& p : paths) openFile(p);
}

bool MainWindow::openFile(const QString& path) {
    const QString abs = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < m_tabs->count(); ++i)
        if (!tab(i)->path.isEmpty() && QFileInfo(tab(i)->path) == QFileInfo(abs)) {  // already open: show it
            m_tabs->setCurrentIndex(i);
            return true;
        }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Cannot open chart"), tr("Cannot open %1:\n%2").arg(QDir::toNativeSeparators(path), f.errorString()));
        HomeScreen::removeRecent(path);
        if (!onChart()) m_home->refresh();
        return false;
    }
    QByteArray data = f.readAll();
    if (data.startsWith("\xEF\xBB\xBF")) data.remove(0, 3);  // UTF-8 BOM some editors add
    QJsonParseError err{};
    // A blank file (e.g. an empty .txt renamed to .flowx) opens as an empty chart.
    const bool blankFile = data.trimmed().isEmpty();
    const QJsonObject o = blankFile ? QJsonObject{{"format", "flowx"}, {"version", 1}}
                                    : QJsonDocument::fromJson(data, &err).object();
    if (err.error != QJsonParseError::NoError || o["format"].toString() != QLatin1String("flowx")) {
        QMessageBox::warning(this, tr("Cannot open chart"), tr("%1 is not a valid flowchart file.").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    if (o["version"].toInt() > 1) {
        QMessageBox::warning(this, tr("Cannot open chart"), tr("This file was made by a newer version of Flowchart."));
        return false;
    }
    // An empty Untitled tab nobody touched (e.g. right after Ctrl+N) gives its place to the file.
    ChartTab* blank = cur();
    if (blank && !(blank->path.isEmpty() && blank->scene->itemCount() == 0 && blank->scene->undoStack()->count() == 0))
        blank = nullptr;
    ChartTab* t = addTab();
    t->scene->fromJson(o);
    t->path = abs;
    QSettings().setValue("lastDir", QFileInfo(abs).absolutePath());
    HomeScreen::addRecent(abs);
    if (o.contains("view")) t->view->setViewState(o["view"].toObject());
    else QTimer::singleShot(0, t->view, &ChartView::fitAll);  // after layout gives the view its size
    if (blank) removeTab(blank);
    updateTabText(t);
    updateTitle();
    return true;
}

void MainWindow::closeEvent(QCloseEvent* e) {
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (tab(i)->scene->undoStack()->isClean()) continue;
        m_tabs->setCurrentIndex(i);  // show the chart the save question is about
        if (!maybeSave(tab(i))) return e->ignore();
    }
    for (int i = 0; i < m_tabs->count(); ++i) removeRecovery(tab(i));
    e->accept();
}

void MainWindow::updateTitle() {
    ChartTab* t = onChart() ? cur() : nullptr;
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

bool MainWindow::maybeSave(ChartTab* t) {
    if (!t || t->scene->undoStack()->isClean()) return true;
    const QString name = t->path.isEmpty() ? tr("Untitled") : QFileInfo(t->path).fileName();
    const auto r = QMessageBox::warning(this, tr("Unsaved changes"), tr("Save changes to \"%1\"?").arg(name),
                                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (r == QMessageBox::Save) return save(t);
    return r == QMessageBox::Discard;
}

bool MainWindow::save(ChartTab* t) {
    if (!t) return false;
    return t->path.isEmpty() ? saveAs(t) : writeFile(t, t->path);
}

bool MainWindow::saveAs(ChartTab* t) {
    if (!t) return false;
    const QString start = t->path.isEmpty()
                              ? QDir(QSettings().value("lastDir", QDir::homePath()).toString()).filePath(tr("Untitled.flowx"))
                              : t->path;
    QString path = QFileDialog::getSaveFileName(this, tr("Save chart"), start, kFilter);
    if (path.isEmpty()) return false;
    if (!path.endsWith(".flowx", Qt::CaseInsensitive)) path += ".flowx";
    return writeFile(t, path);
}

bool MainWindow::writeFile(ChartTab* t, const QString& path) {
    QJsonObject o = t->scene->toJson();
    o["view"] = t->view->viewState();
    o["thumb"] = QString::fromLatin1(thumbnailPng(t->scene).toBase64());
    QSaveFile f(path);  // writes to a temp file and renames, so a crash never leaves half a file
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(o).toJson()) < 0 || !f.commit()) {
        QMessageBox::warning(this, tr("Save failed"), tr("Cannot save %1:\n%2").arg(QDir::toNativeSeparators(path), f.errorString()));
        return false;
    }
    t->path = QFileInfo(path).absoluteFilePath();
    QSettings().setValue("lastDir", QFileInfo(t->path).absolutePath());
    t->scene->undoStack()->setClean();
    removeRecovery(t);
    HomeScreen::addRecent(t->path);
    updateTabText(t);
    if (t == cur()) updateTitle();
    return true;
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

void MainWindow::removeRecovery(ChartTab* t) {
    if (t->recovery.isEmpty()) return;
    QFile::remove(t->recovery);
    t->recovery.clear();
}

void MainWindow::autosave() {
    for (int i = 0; i < m_tabs->count(); ++i) {
        ChartTab* t = tab(i);
        if (t->scene->undoStack()->isClean()) continue;
        if (!t->path.isEmpty() && writeFile(t, t->path)) continue;  // saved chart: write the file itself
        // Untitled (or the write failed): keep a recovery copy instead.
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
                ChartTab* t = addTab();
                t->scene->fromJson(o);
                t->path = o["origPath"].toString();
                t->recovery = fi.absoluteFilePath();  // kept until this tab is saved or closed
                t->view->setViewState(o["view"].toObject());
                t->scene->undoStack()->resetClean();  // recovered work counts as unsaved
                updateTabText(t);
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
    const bool ok = scene()->exportTo(path);
    if (!ok) QMessageBox::warning(this, tr("Export failed"), tr("Cannot write %1.").arg(QDir::toNativeSeparators(path)));
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
    for (int i = 0; i < m_tabs->count(); ++i) tab(i)->view->setDark(dark);
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
        m_paletteBox->setFixedWidth(to);
        m_palette->setCompact(collapsed);
        return;
    }
    m_leftAnim.setStartValue(m_paletteBox->width());
    m_leftAnim.setEndValue(to);
    m_leftAnim.start();
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
