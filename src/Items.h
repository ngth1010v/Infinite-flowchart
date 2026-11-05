#pragma once
#include "Model.h"
#include <QGraphicsTextItem>
#include <functional>

class ChartScene;
class LinkItem;

// Inline editor placed over a node while its text is edited.
// Enter commits, Shift+Enter adds a line, Esc cancels, clicking away commits.
class TextEditor : public QGraphicsTextItem {
public:
    using QGraphicsTextItem::QGraphicsTextItem;
    std::function<void(bool commit)> done;

protected:
    void focusOutEvent(QFocusEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    void finish(bool commit);
};

// Shared base of nodes and links: what the undo system and the style panel work with.
class ChartItem : public QGraphicsItem {
public:
    int id = 0;
    Style style;
    QString text;

    ChartScene* chart() const;
    virtual QJsonObject toJson() const = 0;
    virtual void refresh() = 0;  // re-derive geometry after changing the fields above
};

class NodeItem : public ChartItem {
public:
    enum { Type = UserType + 1 };
    NodeItem(FlowShape shape, QSizeF size);
    ~NodeItem() override;
    int type() const override { return Type; }

    FlowShape shapeType;
    QSizeF size;
    QList<LinkItem*> links;

    QRectF rect() const { return {-size.width() / 2, -size.height() / 2, size.width(), size.height()}; }
    QPointF portScenePos(Port p) const { return mapToScene(m_ports[int(p)]); }
    Port nearestPort(QPointF scenePos) const;
    bool hitBody(QPointF scenePos) const { return m_outline.contains(mapFromScene(scenePos)); }

    void setFromJson(const QJsonObject& j);
    QJsonObject toJson() const override;
    void refresh() override;
    void startEdit();
    void cancelEdit();

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* p, const QStyleOptionGraphicsItem* opt, QWidget* w) override;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* e) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* e) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* e) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* e) override;

private:
    enum class Mode { None, Resize, Link } m_mode = Mode::None;
    bool m_hover = false;
    QPainterPath m_outline, m_deco;
    QPointF m_ports[4];
    QPointF m_fixedCorner;
    TextEditor* m_editor = nullptr;

    double uiUnit() const;  // scene units per screen pixel, so handles keep a steady on-screen size
    bool showHandles() const;
    bool showArrows() const;
    QRectF handleRect(int corner) const;
    QPointF arrowPos(int port) const;
    int hitHandle(QPointF pos) const;
    int hitArrow(QPointF pos) const;
};

// Orthogonal connector between two node ports. Lives at scene origin; points are scene coords.
class LinkItem : public ChartItem {
public:
    enum { Type = UserType + 2 };
    LinkItem();
    ~LinkItem() override;
    int type() const override { return Type; }

    NodeItem* from = nullptr;
    NodeItem* to = nullptr;
    Port fromPort = Port::Bottom;
    Port toPort = Port::Top;

    void setEnds(NodeItem* f, Port fp, NodeItem* t, Port tp);
    QJsonObject toJson() const override;
    void refresh() override;
    const QPolygonF& points() const { return m_pts; }

    QRectF boundingRect() const override { return m_bounds; }
    QPainterPath shape() const override { return m_shape; }
    void paint(QPainter* p, const QStyleOptionGraphicsItem* opt, QWidget* w) override;

    static QPolygonF route(QPointF a, Port pa, QPointF b, Port pb);

protected:
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* e) override;

private:
    QPolygonF m_pts;
    QPainterPath m_shape;
    QRectF m_bounds, m_label;
    double arrowSize() const { return 6 + 2.5 * style.strokeWidth; }
};
