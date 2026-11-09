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
class QToolButton;
class ShapePalette;
class StylePanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    void start(const QString& fileArg);  // crash recovery, then open a file given on the command line
    bool openFile(const QString& path);

protected:
    void closeEvent(QCloseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    ChartScene* m_scene;
    ChartView* m_view;
    HomeScreen* m_home;
    QStackedWidget* m_stack;
    QWidget* m_chartPage;
    ShapePalette* m_palette;
    StylePanel* m_panel;
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
    QString m_path;
    QTimer m_autosave;
    int m_pasteCount = 0;

    QAction* makeAction(QMenu* menu, const QString& text, const QList<QKeySequence>& keys,
                       const std::function<void()>& fn, bool chartOnly = true);
    void buildMenus();
    void showShortcuts();  // full-window list of every keyboard shortcut
    bool onChart() const;
    void showHome();
    void showChart();
    void newChart();
    void openDialog();
    void goHome();
    bool maybeSave();
    bool save();
    bool saveAs();
    bool writeFile(const QString& path);
    void exportChart(bool forLlm = false);  // forLlm: text outline (.md/.txt) instead of an image
    void copy();
    void paste();
    void applyTheme(bool dark);
    void setPaletteCollapsed(bool collapsed, bool animate = true);
    void updatePanelVisibility();
    void updateTitle();
    void autosave();
    QString autosavePath() const;
    void removeAutosave();
    QByteArray thumbnailPng();
};
