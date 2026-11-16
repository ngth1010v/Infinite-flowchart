#pragma once
#include <QMainWindow>
#include <QPointer>
#include <QTimer>
#include <QVariantAnimation>
#include <functional>

class ChartScene;
class ChartView;
class HomeScreen;
class QLabel;
class QModelIndex;
class QScrollArea;
class QFileSystemModel;
class QStackedWidget;
class QToolButton;
class QTreeView;
class QUndoGroup;
class FindPanel;
class ShapePalette;
class StylePanel;

// One open chart: its own scene (so its own undo history) and view (zoom, scroll). Only the current
// one is shown. Without a folder there is one; with a folder, every chart opened from it stays
// loaded, so switching files keeps unsaved changes (red dot in the folder pane).
struct OpenChart {
    ChartScene* scene;
    ChartView* view;
    QString path;      // empty: Untitled
    QString recovery;  // autosave copy of unsaved work for crash recovery; empty until written
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    void start(const QString& fileArg);  // crash recovery, then open a file given on the command line
    bool openFile(const QString& path);  // replaces the current chart; with a folder open, switches to it
    void openFolder(const QString& dir);  // shows the folder pane with the folder's .flowx files
    // Untitled.flowx (or Untitled 2…) in `dir` (default: the folder root), opened, name being edited
    bool newFileInFolder(const QString& dir = {});
    bool newSubfolder(const QString& dir);  // "New folder" (or New folder 2…) in `dir`, name being edited
    // Every .flowx under `dir` as an LLM .md into `out`, same subfolder layout. With update, an
    // existing .md is rewritten with changes marked (see ChartScene::toLlmText); charts without one
    // get a fresh export. Charts that could not be read or updated go into *skipped. Returns how many were written.
    int exportFolderForLlm(const QString& dir, const QString& out, bool update, QStringList* skipped);
    void saveAll();
    int chartCount() const { return m_charts.size(); }
    QString currentPath() const;

protected:
    void closeEvent(QCloseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    HomeScreen* m_home;
    QList<OpenChart*> m_charts;
    OpenChart* m_cur = nullptr;
    QStackedWidget* m_views;  // one ChartView per chart, plus a placeholder when none is current
    QWidget* m_noChart;
    QUndoGroup* m_undoGroup;  // Undo/Redo act on the current chart
    bool m_dark = false, m_minimap = true, m_snap = true;  // applied to every chart
    QString m_folder;  // open folder; empty: single-file mode
    QFileSystemModel* m_fsModel;
    QTreeView* m_tree;
    QWidget* m_folderBox;
    QWidget* m_folderGap;
    QWidget* m_folderBody;  // everything but the collapse button
    QLabel* m_folderName;
    QToolButton* m_folderToggle;
    QStackedWidget* m_stack;
    QWidget* m_chartPage;
    ShapePalette* m_palette;
    StylePanel* m_panel;
    FindPanel* m_find;
    QScrollArea* m_panelScroll;
    QWidget* m_paletteBox;
    QToolButton* m_paletteToggle;
    QToolButton* m_shortcutsButton;
    QPointer<QWidget> m_shortcutsPanel;  // open shortcut list, if any
    QVariantAnimation m_leftAnim, m_rightAnim, m_folderAnim;  // slide the side panels open/closed
    bool m_folderCollapsed = false;
    QToolButton* m_panelToggle;
    QAction* m_panelAction;
    bool m_paletteCollapsed = false;
    QLabel* m_zoomLabel;
    QList<QAction*> m_chartActions;
    QTimer m_autosave;
    int m_pasteCount = 0;

    QAction* makeAction(QMenu* menu, const QString& text, const QList<QKeySequence>& keys,
                       const std::function<void()>& fn, bool chartOnly = true);
    void buildMenus();
    void showShortcuts();  // full-window list of every keyboard shortcut
    bool onChart() const;
    void showHome();
    void showChart();
    OpenChart* cur() const { return m_cur; }  // null on the home screen or before a folder file is picked
    ChartScene* scene() const;
    ChartView* view() const;
    OpenChart* findChart(const QString& path) const;
    OpenChart* addChart();  // new empty Untitled chart, made current
    void setCurrent(OpenChart* t);
    void chartStateChanged(OpenChart* t);  // saved/unsaved or path changed
    void removeChart(OpenChart* t);  // drops it without asking
    void pathMoved(const QString& from, const QString& to);  // a file or folder was moved/renamed on disk
    void setFolderCollapsed(bool collapsed, bool animate = true);
    void showFolderMenu(const QModelIndex& index, const QPoint& globalPos);  // invalid index: empty area (root)
    void deletePath(const QString& path);  // asks, then to the Recycle Bin; open charts there are dropped
    void exportFolderDialog(const QString& dir, bool update);
    void newChart();
    void openDialog();
    void openFolderDialog();
    bool maybeSave(OpenChart* t);
    bool save(OpenChart* t);
    bool saveAs(OpenChart* t);
    bool writeFile(OpenChart* t, const QString& path);
    void exportChart(bool forLlm = false);  // forLlm: text outline (.md/.txt) instead of an image
    void exportLlmUpdate();                 // rewrite an earlier LLM export of this chart, marking the changes
    void stampChartId(const QString& path, ChartScene* s);
    void copy();
    void paste();
    void applyTheme(bool dark);
    void setPaletteCollapsed(bool collapsed, bool animate = true);
    void setPaletteWidth(int w);
    void updatePanelVisibility();
    void updateTitle();
    void autosave();
    QString autosaveDir() const;
    void removeRecovery(OpenChart* t);
    QByteArray thumbnailPng(ChartScene* s);
};
