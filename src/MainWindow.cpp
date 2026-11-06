#include "MainWindow.h"
#include "ChartScene.h"
#include "ChartView.h"
#include "HomeScreen.h"
#include "Panels.h"
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QStyleHints>
#include <QToolButton>
#include <QUndoStack>

namespace {
constexpr int kPaletteOpen = 230, kPaletteClosed = 72, kPanelOpen = 300;
const QString kFilter = QObject::tr("Flowchart (*.flowx)");

// Hide selection/hover decorations while rendering to an image or file.
struct OverlaysHidden {
    ChartScene* s;
    explicit OverlaysHidden(ChartScene* scene) : s(scene) { s->overlaysHidden = true; }
    ~OverlaysHidden() { s->overlaysHidden = false; }
};

}  // namespace

MainWindow::MainWindow() {
    m_scene = new ChartScene(this);
    m_view = new ChartView(m_scene);

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

    m_panel = new StylePanel(m_scene);
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

    m_chartPage = new QWidget;
    auto* h = new QHBoxLayout(m_chartPage);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    h->addWidget(m_paletteBox);
    h->addWidget(m_view, 1);
    h->addWidget(m_panelScroll);
    h->addWidget(panelStrip);

    m_home = new HomeScreen;
    m_stack = new QStackedWidget;
    m_stack->addWidget(m_home);
    m_stack->addWidget(m_chartPage);
    setCentralWidget(m_stack);

    m_zoomLabel = new QLabel;
    statusBar()->addWidget(new QLabel(tr("Wheel: zoom   ·   Right-drag / Space-drag / Shift+wheel: pan   ·   Ctrl+R: fit   ·   "
                                         "Alt while dragging: no snap   ·   Hover a shape and drag an arrow to connect")));
    statusBar()->addPermanentWidget(m_zoomLabel);

    buildMenus();

    connect(m_home, &HomeScreen::newRequested, this, &MainWindow::newChart);
    connect(m_home, &HomeScreen::openRequested, this, [this](const QString& p) {
        if (p.isEmpty()) openDialog();
        else openFile(p);
    });
    connect(m_palette, &ShapePalette::shapeClicked, this, [this](FlowShape s) {
        m_scene->addNode(s, m_view->visibleCenter());
        m_view->setFocus();
    });
    connect(m_panelToggle, &QToolButton::clicked, m_panelAction, &QAction::toggle);
    connect(m_scene->undoStack(), &QUndoStack::cleanChanged, this, &MainWindow::updateTitle);
    connect(m_view, &ChartView::zoomChanged, this,
            [this](double z) { m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(z * 100))); });

    m_autosave.setInterval(30'000);
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
    makeAction(file, tr("&Save"), {QKeySequence::Save}, [this] { save(); });
    makeAction(file, tr("Save &as…"), {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S)}, [this] { saveAs(); });
    makeAction(file, tr("&Export as PNG / SVG / PDF…"), {QKeySequence(Qt::CTRL | Qt::Key_E)}, [this] { exportChart(); });
    file->addSeparator();
    makeAction(file, tr("&Close chart (go home)"), {QKeySequence(Qt::CTRL | Qt::Key_W)}, [this] { goHome(); });
    makeAction(file, tr("E&xit"), {QKeySequence(Qt::ALT | Qt::Key_F4)}, [this] { close(); }, false);

    auto* edit = menuBar()->addMenu(tr("&Edit"));
    auto* undo = m_scene->undoStack()->createUndoAction(this, tr("&Undo"));
    undo->setShortcuts(QKeySequence::Undo);
    auto* redo = m_scene->undoStack()->createRedoAction(this, tr("&Redo"));
    redo->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Y), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
    edit->addAction(undo);
    edit->addAction(redo);
    edit->addSeparator();
    makeAction(edit, tr("Cu&t"), {QKeySequence::Cut}, [this] {
        copy();
        m_scene->deleteSelected();
    });
    makeAction(edit, tr("&Copy"), {QKeySequence::Copy}, [this] { copy(); });
    makeAction(edit, tr("&Paste"), {QKeySequence::Paste}, [this] { paste(); });
    makeAction(edit, tr("&Duplicate"), {QKeySequence(Qt::CTRL | Qt::Key_D)},
               [this] { m_scene->paste(m_scene->copySelection(), {20, 20}); });
    makeAction(edit, tr("De&lete"), {QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)},
               [this] { m_scene->deleteSelected(); });
    edit->addSeparator();
    makeAction(edit, tr("Select &all"), {QKeySequence::SelectAll}, [this] { m_scene->selectAll(); });
    makeAction(edit, tr("&Reset link path"), {}, [this] {
        m_scene->editSelected(QString(), [](ChartItem* c) {
            if (auto* l = qgraphicsitem_cast<LinkItem*>(c)) l->path.clear();
        });
    });

    auto* view = menuBar()->addMenu(tr("&View"));
    makeAction(view, tr("&Fit whole chart"), {QKeySequence(Qt::CTRL | Qt::Key_R)}, [this] { m_view->fitAll(); });
    makeAction(view, tr("Actual size (100%)"), {QKeySequence(Qt::CTRL | Qt::Key_0)}, [this] { m_view->resetZoom(); });
    makeAction(view, tr("Zoom &in"), {QKeySequence(Qt::CTRL | Qt::Key_Equal), QKeySequence(Qt::CTRL | Qt::Key_Plus)},
               [this] { m_view->zoomBy(1.25, false); });
    makeAction(view, tr("Zoom &out"), {QKeySequence(Qt::CTRL | Qt::Key_Minus)}, [this] { m_view->zoomBy(0.8, false); });
    view->addSeparator();
    auto* snap = makeAction(view, tr("&Snap to grid && guides"), {}, [] {});
    snap->setCheckable(true);
    snap->setChecked(QSettings().value("snap", true).toBool());
    m_scene->snapEnabled = snap->isChecked();
    connect(snap, &QAction::toggled, this, [this](bool on) {
        m_scene->snapEnabled = on;
        QSettings().setValue("snap", on);
    });
    auto* minimap = makeAction(view, tr("&Minimap"), {QKeySequence(Qt::CTRL | Qt::Key_M)}, [] {});
    minimap->setCheckable(true);
    minimap->setChecked(QSettings().value("minimap", true).toBool());
    m_view->setMinimapVisible(minimap->isChecked());
    connect(minimap, &QAction::toggled, this, [this](bool on) {
        m_view->setMinimapVisible(on);
        QSettings().setValue("minimap", on);
    });
    makeAction(view, tr("Shape panel: icons only"), {QKeySequence(Qt::CTRL | Qt::Key_B)},
               [this] { setPaletteCollapsed(!m_paletteCollapsed); });
    // Style panel starts closed every launch and opens only on request, not on selection.
    m_panelAction = makeAction(view, tr("S&tyle panel"), {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B)}, [] {});
    m_panelAction->setCheckable(true);
    connect(m_panelAction, &QAction::toggled, this, [this](bool on) {
        m_panelToggle->setText(on ? QStringLiteral("»") : QStringLiteral("«"));
        updatePanelVisibility();
    });
    view->addSeparator();
    auto* dark = makeAction(view, tr("&Dark theme"), {}, [] {}, false);
    dark->setCheckable(true);
    dark->setChecked(QSettings().value("dark", false).toBool());
    connect(dark, &QAction::toggled, this, &MainWindow::applyTheme);
}

// ---------------------------------------------------------------- screens

bool MainWindow::onChart() const { return m_stack->currentWidget() == m_chartPage; }

void MainWindow::showHome() {
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
    m_stack->setCurrentWidget(m_chartPage);
    for (auto* a : std::as_const(m_chartActions)) a->setEnabled(true);
    statusBar()->show();
    updatePanelVisibility();  // back from home: reopen the style panel if it was open
    m_view->setFocus();
}

void MainWindow::newChart() {
    if (!maybeSave()) return;
    m_scene->fromJson({});
    m_path.clear();
    removeAutosave();
    showChart();
    m_view->resetView();
    updateTitle();
}

void MainWindow::openDialog() {
    const QString dir = m_path.isEmpty() ? QSettings().value("lastDir").toString() : QFileInfo(m_path).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, tr("Open chart"), dir, kFilter + ";;" + tr("All files (*)"));
    if (!path.isEmpty()) openFile(path);
}

bool MainWindow::openFile(const QString& path) {
    if (!maybeSave()) return false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Cannot open chart"), tr("Cannot open %1:\n%2").arg(QDir::toNativeSeparators(path), f.errorString()));
        HomeScreen::removeRecent(path);
        if (!onChart()) m_home->refresh();
        return false;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    const QJsonObject o = doc.object();
    if (err.error != QJsonParseError::NoError || o["format"].toString() != QLatin1String("flowx")) {
        QMessageBox::warning(this, tr("Cannot open chart"), tr("%1 is not a valid flowchart file.").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    if (o["version"].toInt() > 1) {
        QMessageBox::warning(this, tr("Cannot open chart"), tr("This file was made by a newer version of Flowchart."));
        return false;
    }
    showChart();
    m_scene->fromJson(o);
    m_path = QFileInfo(path).absoluteFilePath();
    QSettings().setValue("lastDir", QFileInfo(m_path).absolutePath());
    removeAutosave();
    HomeScreen::addRecent(m_path);
    if (o.contains("view")) m_view->setViewState(o["view"].toObject());
    else QTimer::singleShot(0, m_view, &ChartView::fitAll);  // after layout gives the view its size
    updateTitle();
    return true;
}

void MainWindow::goHome() {
    if (!maybeSave()) return;
    m_scene->fromJson({});
    m_path.clear();
    removeAutosave();
    showHome();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!maybeSave()) return e->ignore();
    removeAutosave();
    e->accept();
}

void MainWindow::updateTitle() {
    if (!onChart()) {
        setWindowTitle(tr("Flowchart"));
        setWindowModified(false);
        return;
    }
    const QString name = m_path.isEmpty() ? tr("Untitled") : QFileInfo(m_path).completeBaseName();
    setWindowTitle(tr("%1[*] — Flowchart").arg(name));
    setWindowModified(!m_scene->undoStack()->isClean());
}

// ---------------------------------------------------------------- saving

bool MainWindow::maybeSave() {
    if (!onChart() || m_scene->undoStack()->isClean()) return true;
    const QString name = m_path.isEmpty() ? tr("Untitled") : QFileInfo(m_path).fileName();
    const auto r = QMessageBox::warning(this, tr("Unsaved changes"), tr("Save changes to \"%1\"?").arg(name),
                                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (r == QMessageBox::Save) return save();
    return r == QMessageBox::Discard;
}

bool MainWindow::save() { return m_path.isEmpty() ? saveAs() : writeFile(m_path); }

bool MainWindow::saveAs() {
    const QString start = m_path.isEmpty()
                              ? QDir(QSettings().value("lastDir", QDir::homePath()).toString()).filePath(tr("Untitled.flowx"))
                              : m_path;
    QString path = QFileDialog::getSaveFileName(this, tr("Save chart"), start, kFilter);
    if (path.isEmpty()) return false;
    if (!path.endsWith(".flowx", Qt::CaseInsensitive)) path += ".flowx";
    return writeFile(path);
}

bool MainWindow::writeFile(const QString& path) {
    QJsonObject o = m_scene->toJson();
    o["view"] = m_view->viewState();
    o["thumb"] = QString::fromLatin1(thumbnailPng().toBase64());
    QSaveFile f(path);  // writes to a temp file and renames, so a crash never leaves half a file
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(o).toJson()) < 0 || !f.commit()) {
        QMessageBox::warning(this, tr("Save failed"), tr("Cannot save %1:\n%2").arg(QDir::toNativeSeparators(path), f.errorString()));
        return false;
    }
    m_path = QFileInfo(path).absoluteFilePath();
    QSettings().setValue("lastDir", QFileInfo(m_path).absolutePath());
    m_scene->undoStack()->setClean();
    removeAutosave();
    HomeScreen::addRecent(m_path);
    updateTitle();
    return true;
}

QByteArray MainWindow::thumbnailPng() {
    QImage img(320, 200, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    const QRectF r = m_scene->contentRect();
    if (!r.isNull()) {
        OverlaysHidden guard(m_scene);
        QPainter p(&img);
        p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
        m_scene->render(&p, QRectF(8, 8, 304, 184), r, Qt::KeepAspectRatio);
    }
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return bytes;
}

// ponytail: one autosave slot. Two app instances editing at once share it; give each instance
// its own file plus a lock if that becomes a real workflow.
QString MainWindow::autosavePath() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/autosave.flowx";
}

void MainWindow::removeAutosave() { QFile::remove(autosavePath()); }

void MainWindow::autosave() {
    if (!onChart() || m_scene->undoStack()->isClean()) return;
    QJsonObject o = m_scene->toJson();
    o["view"] = m_view->viewState();
    o["origPath"] = m_path;
    QDir().mkpath(QFileInfo(autosavePath()).absolutePath());
    QSaveFile f(autosavePath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

void MainWindow::start(const QString& fileArg) {
    QFile f(autosavePath());
    if (f.exists()) {
        const auto answer = QMessageBox::question(
            this, tr("Recover chart"), tr("Flowchart did not close normally last time.\nRecover the unsaved chart?"));
        if (answer == QMessageBox::Yes && f.open(QIODevice::ReadOnly)) {
            const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
            f.close();
            showChart();
            m_scene->fromJson(o);
            m_path = o["origPath"].toString();
            m_view->setViewState(o["view"].toObject());
            m_scene->undoStack()->resetClean();  // recovered work counts as unsaved
            updateTitle();
            return;
        }
        removeAutosave();
    }
    if (!fileArg.isEmpty()) openFile(fileArg);
}

// ---------------------------------------------------------------- export & clipboard

void MainWindow::exportChart() {
    const QRectF r = m_scene->contentRect();
    if (r.isNull()) {
        QMessageBox::information(this, tr("Export"), tr("The chart is empty."));
        return;
    }
    const QString base = m_path.isEmpty() ? tr("Untitled") : QFileInfo(m_path).completeBaseName();
    const QString dir = QSettings().value("lastDir", QDir::homePath()).toString();
    QString filter;
    QString path = QFileDialog::getSaveFileName(this, tr("Export chart"), QDir(dir).filePath(base + ".png"),
                                                tr("PNG image (*.png);;SVG image (*.svg);;PDF document (*.pdf)"), &filter);
    if (path.isEmpty()) return;
    QString ext = QFileInfo(path).suffix().toLower();
    if (ext != "png" && ext != "svg" && ext != "pdf") {
        ext = filter.contains("svg") ? "svg" : filter.contains("pdf") ? "pdf" : "png";
        path += "." + ext;
    }

    const bool ok = m_scene->exportTo(path);
    if (!ok) QMessageBox::warning(this, tr("Export failed"), tr("Cannot write %1.").arg(QDir::toNativeSeparators(path)));
}

void MainWindow::copy() {
    const QJsonObject clip = m_scene->copySelection();
    if (clip["nodes"].toArray().isEmpty()) return;
    auto* md = new QMimeData;
    md->setData(kClipMime, QJsonDocument(clip).toJson(QJsonDocument::Compact));
    QGuiApplication::clipboard()->setMimeData(md);
    m_pasteCount = 0;
}

void MainWindow::paste() {
    const QMimeData* md = QGuiApplication::clipboard()->mimeData();
    if (!md || !md->hasFormat(kClipMime)) return;
    ++m_pasteCount;
    m_scene->paste(QJsonDocument::fromJson(md->data(kClipMime)).object(), QPointF(20, 20) * m_pasteCount);
}

// ---------------------------------------------------------------- theme

void MainWindow::applyTheme(bool dark) {
    // Override the Windows light/dark setting; Fusion then picks the matching palette.
    QGuiApplication::styleHints()->setColorScheme(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
    QApplication::setPalette(QApplication::style()->standardPalette());
    m_view->setDark(dark);
    m_palette->rebuildIcons(QApplication::palette().color(QPalette::Text));
    QSettings().setValue("dark", dark);
    if (!onChart()) m_home->refresh();
}

// ---------------------------------------------------------------- side panels

void MainWindow::setPaletteCollapsed(bool collapsed, bool animate) {
    m_paletteCollapsed = collapsed;
    QSettings().setValue("paletteCollapsed", collapsed);
    m_paletteToggle->setText(collapsed ? QStringLiteral("»") : QStringLiteral("«"));
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
