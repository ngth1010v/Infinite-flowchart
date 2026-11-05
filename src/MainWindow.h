#pragma once
#include <QMainWindow>
#include <QTimer>
#include <functional>

class ChartScene;
class ChartView;
class HomeScreen;
class QLabel;
class QScrollArea;
class QStackedWidget;
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

private:
    ChartScene* m_scene;
    ChartView* m_view;
    HomeScreen* m_home;
    QStackedWidget* m_stack;
    QWidget* m_chartPage;
    ShapePalette* m_palette;
    StylePanel* m_panel;
    QScrollArea* m_panelScroll;
    QLabel* m_zoomLabel;
    QList<QAction*> m_chartActions;
    QString m_path;
    QTimer m_autosave;
    int m_pasteCount = 0;

    QAction* makeAction(QMenu* menu, const QString& text, const QList<QKeySequence>& keys,
                       const std::function<void()>& fn, bool chartOnly = true);
    void buildMenus();
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
    void exportChart();
    void copy();
    void paste();
    void applyTheme(bool dark);
    void updateTitle();
    void autosave();
    QString autosavePath() const;
    void removeAutosave();
    QByteArray thumbnailPng();
};
