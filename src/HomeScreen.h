#pragma once
#include <QWidget>

class QLabel;
class QListWidget;

class HomeScreen : public QWidget {
    Q_OBJECT
public:
    explicit HomeScreen(QWidget* parent = nullptr);
    void refresh();

    static QStringList recentFiles();
    static void addRecent(const QString& path);
    static void removeRecent(const QString& path);

signals:
    void newRequested();
    void openRequested(const QString& path);  // empty path = show a file dialog

private:
    QListWidget* m_recent;
    QLabel* m_empty;
};
