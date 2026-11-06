#pragma once
#include "Model.h"
#include <QGraphicsTextItem>
#include <QJsonArray>
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
    void cancelEdit();  // drop an open inline text editor without committing

protected:
    double uiUnit() const;  // scene units per screen pixel, so handles keep a steady on-screen size
    TextEditor* m_editor = nullptr;
};

class NodeItem : public ChartItem {
public:
    enum { Type = UserType + 1 };
    NodeItem(FlowShape shape, QSizeF size);
    ~NodeItem() override;
    int type() const override { return Type; }

    FlowShape shapeType;
    QSizeF size;
    double padding = 40;  // container only: room kept around its contents by "fit to contents"
    QList<LinkItem*> links;

    QRectF rect() const { return {-size.width() / 2, -size.height() / 2, size.width(), size.height()}; }
    QRectF textRect() const { return isContainer(shapeType) ? containerHeader(shapeType, rect()) : shapeTextRect(shapeType, rect()); }
    QPointF portScenePos(Port p) const { return mapToScene(m_ports[int(p)]); }
    Port nearestPort(QPointF scenePos) const;
    // Anchor shown while a link end is dragged near this node: 0-3 = side Port, 4 = center, -1 = none.
    int hitAnchor(QPointF scenePos) const;
    bool hitBody(QPointF scenePos) const { return m_outline.contains(mapFromScene(scenePos)); }
    // A resize handle or quick-connect arrow is under scenePos.
    bool hitGrip(QPointF scenePos) const {
        const QPointF p = mapFromScene(scenePos);
        return hitHandle(p) >= 0 || hitArrow(p) >= 0;
    }

    void setFromJson(const QJsonObject& j);
    QJsonObject toJson() const override;
    void refresh() override;
    void startEdit();
    // Height the text area needs to show `t` wrapped at `width` (padding included).
    double textHeight(const QString& t, double width) const;
    // Grow (never below minSize) so `t` fits: wider first, then taller. The top edge stays in place.
    void growToFit(const QString& t, QSizeF minSize);

    // Kept from the last refresh(), not read from `size`: callers change `size` first, and Qt must
    // still see the old bounds when refresh() announces the change, or the old area is never repainted.
    QRectF boundingRect() const override { return m_bounds; }
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
    QRectF m_bounds;
    QPointF m_ports[4];
    static constexpr int kHandles = 8;  // resize handles: 4 corners, then 4 edge middles
    QPointF m_fixedCorner, m_movingCorner;
    bool m_lockX = false, m_lockY = false;  // edge handle: that axis keeps its size
    QPointF m_prevPos;  // to know how far a container moved, so its contents follow

    bool showHandles() const;
    bool showArrows() const;
    QRectF handleRect(int handle) const;
    QRectF handleHitRect(int handle) const;
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
    // Auto ends were dropped on the center anchor: refresh() re-picks their side to face the other end.
    bool fromAuto = false;
    bool toAuto = false;

    void setEnds(NodeItem* f, Port fp, NodeItem* t, Port tp, bool fAuto = false, bool tAuto = false);
    QJsonObject toJson() const override;
    void refresh() override;
    const QPolygonF& points() const { return m_pts; }
    void startEdit();  // inline label editor, placed where the label sits

    QRectF boundingRect() const override { return m_bounds; }
    QPainterPath shape() const override;
    void paint(QPainter* p, const QStyleOptionGraphicsItem* opt, QWidget* w) override;

    static constexpr double kStub = 20;  // straight run out of a port before the first bend
    static QPolygonF route(QPointF a, Port pa, QPointF b, Port pb);  // plain elbow route
    // User-edited path: the polyline between the two port stubs, in scene coords. Empty = automatic.
    QPolygonF path;
    void setPath(const QPolygonF& p);  // also notes where the end shapes are, so the path can follow them
    // Note the end shapes' current positions as where `path` belongs. Done after each gesture/undo:
    // within one, a path moves along only when both ends moved by the same amount.
    void settlePath();
    QJsonArray pathJson() const;
    // `pts` (the link's points) after sliding segment `seg` sideways to pass through `pos`.
    QPolygonF dragSegment(const QPolygonF& pts, int seg, QPointF pos) const;
    // The `path` to store for link points `pts` (port to port): the polyline from stub to stub.
    QPolygonF pathFromPoints(const QPolygonF& pts) const;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* e) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* e) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* e) override;

private:
    int m_dragEnd = -1;  // end being dragged: 0 = source, 1 = target
    int m_dragSeg = -1;  // segment being slid sideways
    QPolygonF m_dragBase;  // the link's points when that slide started
    QPointF m_pathFrom, m_pathTo;  // end shape positions when `path` was set
    bool segDraggable(int seg) const;
    int hitSegment(QPointF pos) const;
    bool showEndHandles() const;
    int hitEnd(QPointF pos) const;
    void setPoints(const QPolygonF& pts);  // rebuild hit shape, label and bounds
    QPolygonF m_pts;
    QPainterPath m_shape;
    QRectF m_bounds, m_label;
    double arrowSize() const { return 6 + 2.5 * style.strokeWidth; }
};
