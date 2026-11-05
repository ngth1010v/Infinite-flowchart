#pragma once
#include "Model.h"
#include <QListWidget>
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

signals:
    void shapeClicked(FlowShape s);

protected:
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;
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
    QWidget *m_nodeBox, *m_linkBox;
    QComboBox *m_shape, *m_dash, *m_align;
    QSpinBox *m_w, *m_h, *m_fontSize;
    QDoubleSpinBox* m_strokeW;
    QToolButton *m_fill, *m_stroke, *m_textColor;
    QFontComboBox* m_font;
    QCheckBox *m_bold, *m_italic, *m_arrowStart, *m_arrowEnd;

    void edit(const QString& key, const std::function<void(ChartItem*)>& fn);
    QToolButton* colorButton(const QString& key, QColor Style::*field);
    QCheckBox* checkBox(const QString& text, const QString& key, bool Style::*field);
    static void showColor(QToolButton* b, const std::optional<QColor>& c);
};
