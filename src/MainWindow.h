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
class QScrollArea;
class QStackedWidget;
class QTabBar;
class QToolButton;
class QUndoGroup;
class FindPanel;
class ShapePalette;
class StylePanel;

// One open chart. Each tab has its own scene (so its own undo history) and view (zoom, scroll).
struct ChartTab {
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
    bool openFile(const QString& path);  // in a new tab, or switches to the tab already showing it
    int tabCount() const;

protected:
    void closeEvent(QCloseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    HomeScreen* m_home;
    QTabBar* m_tabs;
    QStackedWidget* m_views;  // one ChartView per tab
    QUndoGroup* m_undoGroup;  // Undo/Redo act on the current tab
    bool m_dark = false, m_minimap = true, m_snap = true;  // applied to every tab
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
    QVariantAnimation m_leftAnim, m_rightAnim;  // slide the side panels open/closed
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
    ChartTab* tab(int index) const;
    ChartTab* cur() const;  // current tab; null on the home screen
    ChartScene* scene() const;
    ChartView* view() const;
    int indexOf(const ChartTab* t) const;
    ChartTab* addTab();
    void currentTabChanged();
    void updateTabText(ChartTab* t);
    bool closeTab(int index);  // asks to save; the last tab closes the window
    void removeTab(ChartTab* t);
    void newChart();
    void openDialog();
    bool maybeSave(ChartTab* t);
    bool save(ChartTab* t);
    bool saveAs(ChartTab* t);
    bool writeFile(ChartTab* t, const QString& path);
    void exportChart(bool forLlm = false);  // forLlm: text outline (.md/.txt) instead of an image
    void copy();
    void paste();
    void applyTheme(bool dark);
    void setPaletteCollapsed(bool collapsed, bool animate = true);
    void updatePanelVisibility();
    void updateTitle();
    void autosave();
    QString autosaveDir() const;
    void removeRecovery(ChartTab* t);
    QByteArray thumbnailPng(ChartScene* s);
};
