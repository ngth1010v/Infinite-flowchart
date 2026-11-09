#pragma once
#include "Model.h"
#include <QListWidget>
#include <QPointer>
#include <functional>
#include <optional>

class ChartScene;
class ChartItem;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLabel;
class QPlainTextEdit;
class QSpinBox;
class QToolButton;

// Left side: every flowchart shape. Drag onto the canvas, or click to drop at the view center.
class ShapePalette : public QListWidget {
    Q_OBJECT
public:
    explicit ShapePalette(QWidget* parent = nullptr);
    void rebuildIcons(const QColor& line);
    void setCompact(bool compact);  // icons only

signals:
    void shapeClicked(FlowShape s);

protected:
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;

private:
    bool m_compact = false;
};

// Right side: style of the selected nodes/links. Fields where the selection differs show blank.
class StylePanel : public QWidget {
    Q_OBJECT
public:
    explicit StylePanel(ChartScene* scene, QWidget* parent = nullptr);
    void refresh();

private:
    ChartScene* m_scene;
    bool m_updating = false;  // filling widgets from the selection
    bool m_applying = false;  // pushing a widget change to the selection

    QLabel* m_title;
    QPlainTextEdit* m_text;
    QWidget *m_nodeBox, *m_containerBox, *m_linkBox;
    QComboBox *m_shape, *m_dash, *m_align;
    QSpinBox *m_w, *m_h, *m_fontSize, *m_padding;
    QDoubleSpinBox* m_strokeW;
    QToolButton *m_fill, *m_stroke, *m_textColor;
    QFontComboBox* m_font;
    QCheckBox *m_bold, *m_italic, *m_arrowStart, *m_arrowEnd;

    void edit(const QString& key, const std::function<void(ChartItem*)>& fn);
    QToolButton* colorButton(const QString& key, QColor Style::*field);
    QCheckBox* checkBox(const QString& text, const QString& key, bool Style::*field);
    static void showColor(QToolButton* b, const std::optional<QColor>& c);
};

class ChartView;
class QLineEdit;

// Find & replace (Ctrl+F): a small floating window the user can move off the shapes they look at.
// Matches highlight on the canvas while typing. Enter / Shift+Enter: next / previous. Esc: close.
class FindPanel : public QWidget {
    Q_OBJECT
public:
    FindPanel(ChartView* view, QWidget* parent);
    void open();  // show (first time: top-right of the canvas) and focus the search box
    void refresh();  // recount matches after the chart changed

protected:
    void hideEvent(QHideEvent* e) override;

private:
    QPointer<ChartView> m_view;
    QLineEdit *m_find, *m_replace;
    QLabel* m_count;
    QToolButton* m_case;
    bool m_placed = false;
    void step(int dir);  // +1 next, -1 previous, 0 stay on the current match if it still matches
    void replace(bool all);
};
