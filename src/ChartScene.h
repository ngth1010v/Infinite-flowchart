#pragma once
#include "Items.h"
#include <QGraphicsScene>
#include <QHash>
#include <QJsonObject>
#include <QUndoStack>
#include <functional>

class QGraphicsPathItem;

// id -> item JSON; an empty object means "item does not exist".
using States = QHash<int, QJsonObject>;

class ChartScene : public QGraphicsScene {
    Q_OBJECT
public:
    explicit ChartScene(QObject* parent = nullptr);
    ~ChartScene() override;

    QUndoStack* undoStack() const { return m_undo; }
    bool overlaysHidden = false;  // true while rendering exports, thumbnails and the minimap
    bool snapEnabled = true;
    QVector<QLineF> guides;       // smart alignment guides, drawn by the view
    bool linkDragging = false;    // a link end is being dragged: nodes show their center anchor
    void setLinkDragging(bool on);

    // Document
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& o);  // replaces everything and clears undo history
    QRectF contentRect() const;           // tight bounds of nodes and links (+ small padding)
    int itemCount() const { return int(m_items.size()); }
    bool exportTo(const QString& path);   // .png (2x), .svg or .pdf by extension

    // Undoable edits
    NodeItem* addNode(FlowShape s, QPointF center);
    void deleteSelected();
    void setItemText(ChartItem* item, const QString& text);
    void reconnectLink(LinkItem* link, bool sourceEnd, NodeItem* node, Port port, bool autoPort = false);
    void editSelected(const QString& mergeKey, const std::function<void(ChartItem*)>& fn);
    QJsonObject copySelection() const;
    void paste(const QJsonObject& clip, QPointF offset);
    void selectAll();

    QList<ChartItem*> selectedChartItems() const;
    NodeItem* nodeById(int id) const;
    NodeItem* nodeAt(QPointF scenePos, const NodeItem* exclude = nullptr) const;  // hit by shape body
    QList<NodeItem*> contents(const NodeItem* container) const;  // shapes whose center is inside it
    bool isEditingText() const;
    double viewScale() const;
    QPointF snap(QPointF p, Qt::KeyboardModifiers mods = {}) const;

    // Used by NodeItem during mouse interaction
    QPointF adjustDrag(NodeItem* n, QPointF pos);
    void moveContents(NodeItem* container, QPointF delta);
    void beginLinkDrag(NodeItem* n, Port p);
    void moveLinkDrag(QPointF pos);
    void endLinkDrag(QPointF pos);
    void cancelLinkDrag();

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;

private:
    friend class StateCommand;
    QUndoStack* m_undo;
    QHash<int, ChartItem*> m_items;
    int m_nextId = 1;
    QList<int> m_pressIds;
    States m_pressState;
    QHash<NodeItem*, QList<NodeItem*>> m_riders;  // during a drag: container -> shapes it carries
    NodeItem* m_linkSrc = nullptr;
    Port m_linkPort = Port::Bottom;
    QGraphicsPathItem* m_preview = nullptr;

    States capture(const QList<int>& ids) const;
    void apply(const States& states);
    // Records a change that has already been made to the scene.
    void push(const QString& text, const States& before, const States& after, const QString& mergeKey = {});
    ChartItem* createItem(const QJsonObject& j);
    void applyLinkJson(LinkItem* l, const QJsonObject& j);
    void removeChartItem(int id);
    void clearAll();
    void setGuides(const QVector<QLineF>& g);
};
