#pragma once
#include "Items.h"
#include <QGraphicsScene>
#include <QHash>
#include <QJsonObject>
#include <QSet>
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
    bool dragLayerOn = false;  // the view shows its drag layer image; baked items skip painting
    QColor canvasColor;  // the view's background; on screen, see-through container fills are blended onto it
    // Dark theme: item colors that are black, white or gray (low chroma) are drawn with their
    // lightness flipped (black -> white, white -> a gray just above the canvas); colored ones stay.
    // Containers flip to a darker floor than shapes/links, so layers lighten bottom to top.
    // Only how they are drawn on screen: the stored style, saved files and exports are untouched.
    bool darkCanvas = false;
    QColor ink(const QColor& c, bool container = false) const;
    QVector<QLineF> guides;       // smart alignment guides, drawn by the view
    NodeItem* linkTarget = nullptr;  // while a link end is dragged: the node showing its anchors
    int linkAnchor = -1;             // linkTarget's anchor under the cursor (see NodeItem::hitAnchor)
    void setLinkTarget(NodeItem* n, int anchor = -1);

    // Find (Ctrl+F): items whose text contains findText (case per findCase) get their matches highlighted;
    // findCurrent is the id of the item the find panel is on.
    QString findText;
    int findCurrent = 0;
    Qt::CaseSensitivity findCase = Qt::CaseInsensitive;
    QList<ChartItem*> findMatches() const;  // in reading order: top to bottom, then left to right
    // Undoable, one step: in each item, every occurrence of findText becomes `with`. Returns how many items changed.
    int replaceText(const QList<ChartItem*>& items, const QString& with);

    // Where a link end dragged to `pos` goes. `shown` is the nearest shape (not `exclude`), whose
    // anchors are drawn; `node` is what the end attaches to (null: empty canvas). `other` is the
    // link's opposite end, which the center anchor turns to face.
    struct LinkDrop {
        NodeItem* shown = nullptr;
        int anchor = -1;
        NodeItem* node = nullptr;
        Port port = Port::Top;
        bool autoPort = false;
    };
    LinkDrop linkDropAt(QPointF pos, QPointF other, const NodeItem* exclude) const;

    // Document
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& o);  // replaces everything and clears undo history
    QRectF contentRect() const;           // tight bounds of nodes and links (+ small padding)
    int itemCount() const { return int(m_items.size()); }
    bool exportTo(const QString& path);   // .png (2x), .svg, .pdf, or .md/.txt (toLlmText) by extension
    // Plain-text outline for LLMs: which container holds which shape, and which shape links to
    // which. No positions or styles. Carries chartId in an HTML comment.
    // With `previous` (an earlier export of this chart), lines that differ are marked and a
    // "Changes" section lists what was added, changed and removed. If `previous` is not an
    // export of this chart, returns a null string and sets *error.
    // With `previous`, *marked gets the ids of the shapes and links marked (new)/(changed), and
    // *changed whether anything changed at all (removals included).
    QString toLlmText(const QString& title, const QString& previous = {}, QString* error = nullptr,
                      QSet<int>* marked = nullptr, bool* changed = nullptr) const;
    // Identifies the chart across saves and LLM exports. fromJson takes it from the file or makes
    // a new one; chartIdSaved says whether the file on disk already holds it.
    QString chartId;
    bool chartIdSaved = false;

    // History pane (Alt+C): ids of the items changed since the picked history row, outlined on screen.
    QSet<int> historyMarks;

    // Undoable edits
    NodeItem* addNode(FlowShape s, QPointF center);
    void deleteSelected();
    void setItemText(ChartItem* item, const QString& text);
    void reconnectLink(LinkItem* link, bool sourceEnd, NodeItem* node, Port port, bool autoPort = false);
    void setLinkPath(LinkItem* link, const QPolygonF& path);
    // Shrink or grow each selected container to wrap the shapes inside it, plus its padding.
    void fitSelectedContainers();
    // Selection is only links, 2 or more: move their middle segments so they overlap where they
    // can, moving as little as possible. Returns false (nothing done) for any other selection.
    bool bundleSelectedLinks();
    // 2+ shapes selected (containers and links in the selection are left alone): line up their
    // left / right / top / bottom edges for Key_Left / Right / Up / Down. With distribute, also
    // space them evenly along the other axis. Returns false (nothing done) when fewer than 2 shapes.
    bool alignSelectedShapes(Qt::Key key, bool distribute);
    // Shapes in the selection (containers and links left alone) resize to fit their text; with 2+,
    // they then all take the largest width and height. Returns false (nothing done) when no shapes.
    bool fitSelectedShapes();
    void settleLinkPaths();  // see LinkItem::settlePath
    void editSelected(const QString& mergeKey, const std::function<void(ChartItem*)>& fn);
    QJsonObject copySelection() const;
    void paste(const QJsonObject& clip, QPointF offset);
    void selectAll();

    QList<ChartItem*> selectedChartItems() const;
    // selectedItems().size() without building the list; nodes ask on every paint and mouse move.
    int selectionSize() const { return m_selectionSize; }
    NodeItem* nodeById(int id) const;
    NodeItem* nodeAt(QPointF scenePos, const NodeItem* exclude = nullptr) const;  // hit by shape body
    QList<NodeItem*> contents(const NodeItem* container) const;  // shapes whose center is inside it
    bool isEditingText() const;
    double viewScale() const;
    QPointF snap(QPointF p, Qt::KeyboardModifiers mods = {}) const;

    // While shapes are dragged or resized: those shapes and their links (what changes each frame).
    // Empty otherwise, including while a new link is dragged out.
    QList<ChartItem*> dragMovingItems() const;

    // Used by NodeItem during mouse interaction
    QPointF adjustDrag(NodeItem* n, QPointF pos);
    void moveContents(NodeItem* container, QPointF delta);
    void beginLinkDrag(NodeItem* n, Port p);
    void moveLinkDrag(QPointF pos);
    void endLinkDrag(QPointF pos);
    void cancelLinkDrag();

signals:
    // An edit was recorded on the undo stack. `before` is toLlmText of the chart before it;
    // merged: it was folded into the previous undo step (typing, spin box steps).
    void edited(const QString& text, const QString& before, bool merged);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;

private:
    friend class StateCommand;
    QUndoStack* m_undo;
    QHash<int, ChartItem*> m_items;
    int m_nextId = 1;
    int m_selectionSize = 0;
    QString m_llm;  // toLlmText of the chart as it is, refreshed on every undo index change
    QList<int> m_pressIds;
    States m_pressState;
    QPointF m_grabPress, m_dragDelta;  // during a drag: grabbed item's start position, mouse travel
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
