#include "ChartScene.h"
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QSvgGenerator>
#include <QGraphicsPathItem>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QGuiApplication>
#include <QJsonArray>
#include <QPen>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

// One command type for every edit: it swaps the JSON state of the touched items.
class StateCommand : public QUndoCommand {
public:
    StateCommand(ChartScene* s, const QString& text, States before, States after, QString key)
        : QUndoCommand(text), m_scene(s), m_before(std::move(before)), m_after(std::move(after)), m_key(std::move(key)) {
        m_time.start();
    }
    void undo() override { m_scene->apply(m_before); }
    void redo() override {
        if (std::exchange(m_first, false)) return;  // change is already in the scene when pushed
        m_scene->apply(m_after);
    }
    int id() const override { return m_key.isEmpty() ? -1 : 0x464c; }
    // Rapid edits of the same field (typing, spin box steps) collapse into one undo step.
    bool mergeWith(const QUndoCommand* other) override {
        const auto* o = static_cast<const StateCommand*>(other);
        if (o->m_key != m_key || m_time.elapsed() > 1500) return false;
        if (QSet<int>(o->m_after.keyBegin(), o->m_after.keyEnd()) != QSet<int>(m_after.keyBegin(), m_after.keyEnd()))
            return false;
        m_after = o->m_after;
        m_time.restart();
        return true;
    }

private:
    ChartScene* m_scene;
    States m_before, m_after;
    QString m_key;
    QElapsedTimer m_time;
    bool m_first = true;
};

ChartScene::ChartScene(QObject* parent) : QGraphicsScene(parent), m_undo(new QUndoStack(this)) {
    m_undo->setUndoLimit(300);
    // Resize handles show only for a single selection, so repaint selected nodes when it changes.
    connect(this, &QGraphicsScene::selectionChanged, this, [this] {
        const auto sel = selectedItems();
        m_selectionSize = int(sel.size());
        for (auto* it : sel) it->update();
    });
}

ChartScene::~ChartScene() {
    blockSignals(true);
    m_undo->blockSignals(true);
    clearAll();
}

// ---------------------------------------------------------------- document

QJsonObject ChartScene::toJson() const {
    QList<int> ids = m_items.keys();
    std::sort(ids.begin(), ids.end());
    QJsonArray nodes, links;
    for (int id : ids) {
        auto* c = m_items[id];
        (c->type() == NodeItem::Type ? nodes : links).append(c->toJson());
    }
    return {{"format", "flowx"}, {"version", 1}, {"nodes", nodes}, {"links", links}};
}

void ChartScene::fromJson(const QJsonObject& o) {
    clearAll();
    for (const auto& v : o["nodes"].toArray()) {
        QJsonObject j = v.toObject();
        j["kind"] = "node";
        createItem(j);
    }
    for (const auto& v : o["links"].toArray()) {
        QJsonObject j = v.toObject();
        j["kind"] = "link";
        createItem(j);
    }
    m_undo->clear();
}

QRectF ChartScene::contentRect() const {
    QRectF r;
    for (auto* c : m_items) {
        if (auto* n = qgraphicsitem_cast<NodeItem*>(c))
            r |= n->mapRectToScene(n->rect());
        else
            r |= c->boundingRect();
    }
    return r.isNull() ? r : r.adjusted(-20, -20, 20, 20);
}

bool ChartScene::exportTo(const QString& path) {
    const QRectF r = contentRect();
    if (r.isNull()) return false;
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "md" || ext == "txt") {
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write(toLlmText(QFileInfo(path).completeBaseName()).toUtf8());
        return f.commit();
    }
    overlaysHidden = true;
    bool ok = true;
    if (ext == "png") {
        // 2x for crisp output, capped so huge charts don't allocate gigabytes.
        const double k = std::min(2.0, 16000.0 / std::max(r.width(), r.height()));
        QImage img((r.size() * k).toSize(), QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::white);
        QPainter p(&img);
        p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
        render(&p, QRectF(QPointF(), img.size()), r);
        p.end();
        ok = img.save(path);
    } else if (ext == "svg") {
        QSvgGenerator gen;
        gen.setFileName(path);
        gen.setSize(r.size().toSize());
        gen.setViewBox(QRectF(QPointF(), r.size()));
        QPainter p;
        ok = p.begin(&gen);
        if (ok) render(&p, QRectF(QPointF(), r.size()), r);
    } else {
        QPdfWriter pdf(path);
        pdf.setResolution(72);
        pdf.setPageSize(QPageSize(r.size(), QPageSize::Point));
        pdf.setPageMargins(QMarginsF());
        QPainter p;
        ok = p.begin(&pdf);
        if (ok) render(&p, QRectF(0, 0, pdf.width(), pdf.height()), r);
    }
    overlaysHidden = false;
    return ok;
}

QString ChartScene::toLlmText(const QString& title) const {
    QList<NodeItem*> nodes;
    for (auto* c : m_items)
        if (auto* n = qgraphicsitem_cast<NodeItem*>(c)) nodes << n;
    // Reading order: top to bottom, then left to right.
    std::sort(nodes.begin(), nodes.end(), [](NodeItem* a, NodeItem* b) {
        return std::pair(a->pos().y(), a->pos().x()) < std::pair(b->pos().y(), b->pos().x());
    });
    auto area = [](const NodeItem* n) { return n->size.width() * n->size.height(); };
    // Parent = smallest container (bigger than the shape itself) holding the shape's center,
    // the same rule contents() uses.
    QHash<const NodeItem*, QList<NodeItem*>> kids;
    for (auto* n : nodes) {
        NodeItem* parent = nullptr;
        for (auto* c : nodes) {
            if (c == n || !isContainer(c->shapeType) || !c->mapRectToScene(c->rect()).contains(n->pos())) continue;
            if (isContainer(n->shapeType) && area(c) <= area(n)) continue;
            if (!parent || area(c) < area(parent)) parent = c;
        }
        kids[parent] << n;
    }
    auto label = [](const NodeItem* n) {
        const QString t = n->text.simplified();
        return QString("[%1] %2").arg(n->id).arg(shapeName(n->shapeType)) + (t.isEmpty() ? "" : " \"" + t + "\"");
    };

    QString out = "# Flowchart: " + title + "\n\n"
                  "Shapes are listed as [id] Type \"text\". Indented shapes are inside the container above them.\n\n"
                  "## Shapes\n\n";
    std::function<void(const NodeItem*, int)> tree = [&](const NodeItem* parent, int depth) {
        for (auto* n : kids.value(parent)) {
            out += QString(depth * 2, ' ') + "- " + label(n) + "\n";
            tree(n, depth + 1);
        }
    };
    tree(nullptr, 0);

    out += "\n## Links\n\n";
    QList<int> ids = m_items.keys();
    std::sort(ids.begin(), ids.end());
    int linkCount = 0;
    for (int id : ids) {
        auto* l = qgraphicsitem_cast<LinkItem*>(m_items[id]);
        if (!l || !l->from || !l->to) continue;
        const char* arrow = l->style.arrowEnd ? (l->style.arrowStart ? "<->" : "->") : (l->style.arrowStart ? "<-" : "--");
        const QString t = l->text.simplified();
        out += "- " + label(l->from) + " " + arrow + " " + label(l->to) + (t.isEmpty() ? "" : " (label: \"" + t + "\")") + "\n";
        ++linkCount;
    }
    if (!linkCount) out += "(none)\n";
    return out;
}

// ---------------------------------------------------------------- state plumbing

States ChartScene::capture(const QList<int>& ids) const {
    States s;
    for (int id : ids) {
        auto* c = m_items.value(id);
        s[id] = c ? c->toJson() : QJsonObject();
    }
    return s;
}

void ChartScene::apply(const States& states) {
    auto isLink = [](const QJsonObject& j) { return j["kind"].toString() == QLatin1String("link"); };
    // Order matters: drop links, then create/update nodes, then links, then drop nodes.
    for (auto it = states.begin(); it != states.end(); ++it)
        if (it->isEmpty() && m_items.value(it.key()) && m_items[it.key()]->type() == LinkItem::Type) removeChartItem(it.key());
    for (auto it = states.begin(); it != states.end(); ++it) {
        if (it->isEmpty() || isLink(*it)) continue;
        if (auto* n = nodeById(it.key())) n->setFromJson(*it);
        else createItem(*it);
    }
    for (auto it = states.begin(); it != states.end(); ++it) {
        if (it->isEmpty() || !isLink(*it)) continue;
        if (auto* l = qgraphicsitem_cast<LinkItem*>(m_items.value(it.key()))) applyLinkJson(l, *it);
        else createItem(*it);
    }
    for (auto it = states.begin(); it != states.end(); ++it)
        if (it->isEmpty()) removeChartItem(it.key());
    settleLinkPaths();
}

void ChartScene::push(const QString& text, const States& before, const States& after, const QString& mergeKey) {
    if (before == after) return;
    m_undo->push(new StateCommand(this, text, before, after, mergeKey));
}

ChartItem* ChartScene::createItem(const QJsonObject& j) {
    const int id = j["id"].toInt();
    if (id <= 0 || m_items.contains(id)) return nullptr;  // bad or duplicate id in file
    ChartItem* item = nullptr;
    if (j["kind"].toString() == QLatin1String("link")) {
        if (!nodeById(j["from"].toInt()) || !nodeById(j["to"].toInt())) return nullptr;
        auto* l = new LinkItem;
        l->id = id;
        addItem(l);
        applyLinkJson(l, j);
        item = l;
    } else {
        auto* n = new NodeItem(FlowShape::Process, {140, 70});
        n->id = id;
        n->setFromJson(j);
        addItem(n);
        item = n;
    }
    m_items[id] = item;
    m_nextId = std::max(m_nextId, id + 1);
    return item;
}

void ChartScene::applyLinkJson(LinkItem* l, const QJsonObject& j) {
    l->text = j["text"].toString();
    l->style = styleFromJson(j["style"].toObject());
    const QString fp = j["fromPort"].toString(), tp = j["toPort"].toString();
    l->path.clear();
    l->setEnds(nodeById(j["from"].toInt()), portFromKey(fp), nodeById(j["to"].toInt()), portFromKey(tp),
               fp == QLatin1String("auto"), tp == QLatin1String("auto"));
    QPolygonF path;
    for (const auto& v : j["path"].toArray())
        if (const QJsonArray pt = v.toArray(); pt.size() == 2) path << QPointF(pt[0].toDouble(), pt[1].toDouble());
    if (path.size() >= 3) l->setPath(path);
}

void ChartScene::removeChartItem(int id) {
    ChartItem* c = m_items.take(id);
    if (!c) return;
    c->cancelEdit();
    if (auto* n = qgraphicsitem_cast<NodeItem*>(c)) {
        if (m_linkSrc == n) cancelLinkDrag();
        if (linkTarget == n) linkTarget = nullptr;
        for (auto* l : QList<LinkItem*>(n->links)) removeChartItem(l->id);
    }
    delete c;  // also removes it from the scene
}

void ChartScene::clearAll() {
    for (auto* c : std::as_const(m_items))
        c->cancelEdit();
    m_linkSrc = nullptr;
    m_preview = nullptr;
    clear();  // node and link destructors unhook from each other, so order does not matter
    m_items.clear();
    m_selectionSize = 0;
    m_nextId = 1;
    guides.clear();
    m_pressIds.clear();
    m_pressState.clear();
}

// ---------------------------------------------------------------- queries

QList<ChartItem*> ChartScene::selectedChartItems() const {
    QList<ChartItem*> out;
    for (auto* it : selectedItems())
        if (it->type() == NodeItem::Type || it->type() == LinkItem::Type) out << static_cast<ChartItem*>(it);
    return out;
}

NodeItem* ChartScene::nodeById(int id) const { return qgraphicsitem_cast<NodeItem*>(m_items.value(id)); }

// ponytail: membership is geometric (center inside), so no parent ids are stored in the file.
// A container dragged over loose shapes picks them up on the next drag; store explicit
// membership if that turns out to annoy users.
QList<NodeItem*> ChartScene::contents(const NodeItem* container) const {
    QList<NodeItem*> out;
    if (!isContainer(container->shapeType)) return out;
    const QRectF box = container->mapRectToScene(container->rect());
    const double area = container->size.width() * container->size.height();
    for (auto* it : items(box, Qt::IntersectsItemBoundingRect)) {
        auto* n = qgraphicsitem_cast<NodeItem*>(it);
        if (!n || n == container || !box.contains(n->pos())) continue;
        if (isContainer(n->shapeType) && n->size.width() * n->size.height() >= area) continue;  // its parent
        out << n;
    }
    return out;
}

NodeItem* ChartScene::nodeAt(QPointF scenePos, const NodeItem* exclude) const {
    for (auto* it : items(scenePos))
        if (auto* n = qgraphicsitem_cast<NodeItem*>(it); n && n != exclude && n->hitBody(scenePos)) return n;
    return nullptr;
}

bool ChartScene::isEditingText() const { return dynamic_cast<TextEditor*>(focusItem()) != nullptr; }

double ChartScene::viewScale() const { return views().isEmpty() ? 1.0 : views().first()->transform().m11(); }

QPointF ChartScene::snap(QPointF p, Qt::KeyboardModifiers mods) const {
    if (!snapEnabled || (mods & Qt::AltModifier)) return p;
    return {std::round(p.x() / 10) * 10, std::round(p.y() / 10) * 10};
}

// ---------------------------------------------------------------- edits

NodeItem* ChartScene::addNode(FlowShape s, QPointF center) {
    center = snap(center);
    const QSizeF sz = shapeDefaultSize(s);
    const int id = m_nextId;
    auto* n = static_cast<NodeItem*>(createItem({{"kind", "node"},
                                                  {"id", id},
                                                  {"shape", shapeKey(s)},
                                                  {"x", center.x()},
                                                  {"y", center.y()},
                                                  {"w", sz.width()},
                                                  {"h", sz.height()},
                                                  {"style", styleToJson(defaultNodeStyle(s))}}));
    clearSelection();
    n->setSelected(true);
    push(tr("Add %1").arg(shapeName(s)), {{id, {}}}, capture({id}));
    return n;
}

void ChartScene::deleteSelected() {
    if (isEditingText()) return;
    QList<int> ids;
    for (auto* c : selectedChartItems()) {
        ids << c->id;
        if (auto* n = qgraphicsitem_cast<NodeItem*>(c))
            for (auto* l : std::as_const(n->links)) ids << l->id;
    }
    ids = QSet<int>(ids.begin(), ids.end()).values();
    if (ids.isEmpty()) return;
    const States before = capture(ids);
    States after;
    for (int id : ids) after[id] = {};
    apply(after);
    push(tr("Delete"), before, after);
}

void ChartScene::setItemText(ChartItem* item, const QString& text) {
    const States before = capture({item->id});
    item->text = text;
    if (auto* n = qgraphicsitem_cast<NodeItem*>(item)) n->growToFit(text, n->size);  // grow only
    item->refresh();
    push(tr("Edit text"), before, capture({item->id}));
}

void ChartScene::reconnectLink(LinkItem* link, bool sourceEnd, NodeItem* node, Port port, bool autoPort) {
    const States before = capture({link->id});
    link->path.clear();  // a new end: back to the automatic route
    if (sourceEnd) link->setEnds(node, port, link->to, link->toPort, autoPort, link->toAuto);
    else link->setEnds(link->from, link->fromPort, node, port, link->fromAuto, autoPort);
    push(tr("Reconnect link"), before, capture({link->id}));
}

void ChartScene::settleLinkPaths() {
    for (auto* c : std::as_const(m_items))
        if (auto* l = qgraphicsitem_cast<LinkItem*>(c); l && !l->path.isEmpty()) l->settlePath();
}

void ChartScene::fitSelectedContainers() {
    if (isEditingText()) return;
    QList<NodeItem*> boxes;
    for (auto* c : selectedChartItems())
        if (auto* n = qgraphicsitem_cast<NodeItem*>(c); n && isContainer(n->shapeType)) boxes << n;
    if (boxes.isEmpty()) return;
    // Inner containers first, so an outer one wraps them at their fitted size.
    std::sort(boxes.begin(), boxes.end(), [](NodeItem* a, NodeItem* b) {
        return a->size.width() * a->size.height() < b->size.width() * b->size.height();
    });
    QList<int> ids;
    for (auto* n : boxes) ids << n->id;
    const States before = capture(ids);
    for (auto* n : boxes) {
        QRectF box;
        for (auto* c : contents(n)) box |= c->mapRectToScene(c->rect());
        if (box.isNull()) continue;  // empty container: nothing to fit
        box.adjust(-n->padding, -n->padding, n->padding, n->padding);
        if (n->shapeType == FlowShape::HContainer) box.setLeft(box.left() - kContainerHeader);
        else box.setTop(box.top() - kContainerHeader);  // room for the title band
        n->size = box.size();
        n->setPos(box.center());
        n->refresh();
    }
    push(tr("Fit container"), before, capture(ids));
}

void ChartScene::setLinkPath(LinkItem* link, const QPolygonF& path) {
    const States before = capture({link->id});
    link->setPath(path);
    push(tr("Edit link path"), before, capture({link->id}));
}

namespace {
// One pass of bundling, over horizontal or vertical segments. Segments of different links whose
// spans meet are chained into groups, and each group goes onto one line: the median of their
// lines, which is the least total movement. Only middle segments move (their neighbors just
// stretch); a segment next to a port stub keeps that stub at least kStub long.
// ponytail: O(segments^2) grouping; fine for the handful of links a person selects.
void alignSegments(const QList<LinkItem*>& links, QList<QPolygonF>& pts, bool horiz) {
    auto along = [horiz](QPointF p) { return horiz ? p.x() : p.y(); };
    auto across = [horiz](QPointF p) { return horiz ? p.y() : p.x(); };
    struct Seg {
        int link, i;
        double at, lo, hi, min, max;
    };
    QList<Seg> segs;
    for (int k = 0; k < links.size(); ++k) {
        const QPolygonF& p = pts[k];
        const int n = int(p.size());
        for (int i = 1; i + 2 < n; ++i) {
            // This direction, with square corners on both sides (so moving it keeps the link orthogonal).
            if (std::abs(across(p[i]) - across(p[i + 1])) > 1e-6) continue;
            if (std::abs(along(p[i - 1]) - along(p[i])) > 1e-6 || std::abs(along(p[i + 1]) - along(p[i + 2])) > 1e-6) continue;
            Seg s{k, i, across(p[i]), std::min(along(p[i]), along(p[i + 1])), std::max(along(p[i]), along(p[i + 1])),
                  -1e300, 1e300};
            auto keepStub = [&](QPointF port, Port side) {
                const double d = across(portDir(side));
                if (d > 0) s.min = std::max(s.min, across(port) + LinkItem::kStub);
                if (d < 0) s.max = std::min(s.max, across(port) - LinkItem::kStub);
            };
            if (i == 1) keepStub(p[0], links[k]->fromPort);
            if (i == n - 3) keepStub(p[n - 1], links[k]->toPort);
            segs << s;
        }
    }
    // Union-find over segments of different links whose spans touch or overlap.
    QList<int> root(segs.size());
    std::iota(root.begin(), root.end(), 0);
    auto find = [&](int x) {
        while (root[x] != x) x = root[x] = root[root[x]];
        return x;
    };
    for (int a = 0; a < segs.size(); ++a)
        for (int b = a + 1; b < segs.size(); ++b)
            if (segs[a].link != segs[b].link && segs[a].lo <= segs[b].hi + 0.5 && segs[b].lo <= segs[a].hi + 0.5)
                root[find(a)] = find(b);
    QHash<int, QList<int>> groups;
    for (int a = 0; a < segs.size(); ++a) groups[find(a)] << a;
    for (const QList<int>& g : std::as_const(groups)) {
        QList<int> members;  // one segment per link: two of one link on one line would fold it back
        QSet<int> seen;
        for (int a : g)
            if (!seen.contains(segs[a].link)) seen << segs[a].link, members << a;
        if (members.size() < 2) continue;
        QList<double> at;
        for (int a : members) at << segs[a].at;
        std::sort(at.begin(), at.end());
        const double line = at[(at.size() - 1) / 2];
        for (int a : members) {
            const Seg& s = segs[a];
            if (s.min > s.max) continue;
            const double v = std::clamp(line, s.min, s.max);
            for (QPointF* q : {&pts[s.link][s.i], &pts[s.link][s.i + 1]}) (horiz ? q->ry() : q->rx()) = v;
        }
    }
}
}  // namespace

bool ChartScene::bundleSelectedLinks() {
    if (isEditingText()) return false;
    QList<LinkItem*> links;
    for (auto* c : selectedChartItems()) {
        auto* l = qgraphicsitem_cast<LinkItem*>(c);
        if (!l) return false;  // a shape is selected too
        if (l->from && l->to) links << l;
    }
    if (links.size() < 2) return false;
    QList<int> ids;
    QList<QPolygonF> pts;
    for (auto* l : links) ids << l->id, pts << l->points();
    const States before = capture(ids);
    alignSegments(links, pts, true);
    alignSegments(links, pts, false);
    for (int k = 0; k < links.size(); ++k)
        if (pts[k] != links[k]->points()) links[k]->setPath(links[k]->pathFromPoints(pts[k]));
    push(tr("Bundle links"), before, capture(ids));
    return true;
}

bool ChartScene::alignSelectedShapes(Qt::Key key, bool distribute) {
    if (isEditingText()) return false;
    QList<NodeItem*> shapes;
    for (auto* c : selectedChartItems())
        if (auto* n = qgraphicsitem_cast<NodeItem*>(c); n && !isContainer(n->shapeType)) shapes << n;
    if (shapes.size() < 2) return false;
    QList<int> ids;
    for (auto* n : shapes) {
        ids << n->id;
        for (auto* l : std::as_const(n->links)) ids << l->id;  // settled paths change too
    }
    ids = QSet<int>(ids.begin(), ids.end()).values();
    const States before = capture(ids);
    auto box = [](NodeItem* n) { return n->mapRectToScene(n->rect()); };
    const bool horiz = key == Qt::Key_Up || key == Qt::Key_Down;  // edges line up along a horizontal line
    // Align: every shape's edge onto the outermost one.
    double edge = key == Qt::Key_Left || key == Qt::Key_Up ? 1e300 : -1e300;
    for (auto* n : shapes) {
        const QRectF r = box(n);
        switch (key) {
        case Qt::Key_Left: edge = std::min(edge, r.left()); break;
        case Qt::Key_Right: edge = std::max(edge, r.right()); break;
        case Qt::Key_Up: edge = std::min(edge, r.top()); break;
        default: edge = std::max(edge, r.bottom()); break;
        }
    }
    for (auto* n : shapes) {
        const QRectF r = box(n);
        const double d = key == Qt::Key_Left ? edge - r.left() : key == Qt::Key_Right ? edge - r.right()
                       : key == Qt::Key_Up   ? edge - r.top()  : edge - r.bottom();
        n->setPos(n->pos() + (horiz ? QPointF(0, d) : QPointF(d, 0)));
    }
    // Distribute: equal gaps along the other axis, first and last shape stay put.
    if (distribute) {
        auto lo = [&](NodeItem* n) { return horiz ? box(n).left() : box(n).top(); };
        auto len = [&](NodeItem* n) { return horiz ? box(n).width() : box(n).height(); };
        std::sort(shapes.begin(), shapes.end(), [&](NodeItem* a, NodeItem* b) { return lo(a) < lo(b); });
        double span = 0, total = 0;
        for (auto* n : shapes) span = std::max(span, lo(n) + len(n)), total += len(n);
        span -= lo(shapes.first());
        // Shapes already in a line across this axis would pile up: keep a minimum gap instead.
        const double gap = std::max(20.0, (span - total) / (shapes.size() - 1));
        double at = lo(shapes.first());
        for (auto* n : shapes) {
            const double d = at - lo(n);
            n->setPos(n->pos() + (horiz ? QPointF(d, 0) : QPointF(0, d)));
            at += len(n) + gap;
        }
    }
    settleLinkPaths();
    push(distribute ? tr("Align and distribute") : tr("Align"), before, capture(ids));
    return true;
}

bool ChartScene::fitSelectedShapes() {
    if (isEditingText()) return false;
    QList<NodeItem*> shapes;
    for (auto* c : selectedChartItems())
        if (auto* n = qgraphicsitem_cast<NodeItem*>(c); n && !isContainer(n->shapeType)) shapes << n;
    if (shapes.isEmpty()) return false;
    QList<int> ids;
    for (auto* n : shapes) {
        ids << n->id;
        for (auto* l : std::as_const(n->links)) ids << l->id;  // settled paths change too
    }
    ids = QSet<int>(ids.begin(), ids.end()).values();
    const States before = capture(ids);
    QSizeF common;
    for (auto* n : shapes) {
        n->growToFit(n->text, QSizeF(40, 30));  // from a small floor, so it shrinks as well as grows
        common = common.expandedTo(n->size);
    }
    // Several shapes: all take the widest width and tallest height, so every text still fits.
    if (shapes.size() > 1)
        for (auto* n : shapes) n->size = common, n->refresh();
    settleLinkPaths();
    push(tr("Fit to content"), before, capture(ids));
    return true;
}

void ChartScene::editSelected(const QString& mergeKey, const std::function<void(ChartItem*)>& fn) {
    QList<int> ids;
    for (auto* c : selectedChartItems()) ids << c->id;
    if (ids.isEmpty()) return;
    const States before = capture(ids);
    for (int id : ids) {
        fn(m_items[id]);
        m_items[id]->refresh();
    }
    push(tr("Edit style"), before, capture(ids), mergeKey);
}

QJsonObject ChartScene::copySelection() const {
    QSet<int> nodeIds;
    QJsonArray nodes, links;
    for (auto* c : selectedChartItems())
        if (auto* n = qgraphicsitem_cast<NodeItem*>(c)) {
            nodeIds << n->id;
            for (auto* inner : contents(n)) nodeIds << inner->id;  // a copied container brings its shapes
        }
    QList<int> sorted = nodeIds.values();
    std::sort(sorted.begin(), sorted.end());  // keep relative stacking order on paste
    for (int id : sorted) nodes << nodeById(id)->toJson();
    for (int id : nodeIds)
        for (auto* l : std::as_const(nodeById(id)->links))
            if (l->from->id == id && nodeIds.contains(l->to->id)) links << l->toJson();
    return {{"nodes", nodes}, {"links", links}};
}

void ChartScene::paste(const QJsonObject& clip, QPointF offset) {
    QHash<int, int> idMap;
    QList<int> created;
    clearSelection();
    for (const auto& v : clip["nodes"].toArray()) {
        QJsonObject j = v.toObject();
        const int id = m_nextId;
        idMap[j["id"].toInt()] = id;
        j["kind"] = "node";
        j["id"] = id;
        j["x"] = j["x"].toDouble() + offset.x();
        j["y"] = j["y"].toDouble() + offset.y();
        if (auto* c = createItem(j)) c->setSelected(true), created << id;
    }
    for (const auto& v : clip["links"].toArray()) {
        QJsonObject j = v.toObject();
        const int f = j["from"].toInt(), t = j["to"].toInt();
        if (!idMap.contains(f) || !idMap.contains(t)) continue;
        const int id = m_nextId;
        j["kind"] = "link";
        j["id"] = id;
        j["from"] = idMap[f];
        j["to"] = idMap[t];
        QJsonArray path;  // edited paths move with the pasted shapes
        for (const auto& pv : j["path"].toArray())
            path << QJsonArray{pv.toArray()[0].toDouble() + offset.x(), pv.toArray()[1].toDouble() + offset.y()};
        j["path"] = path;
        if (auto* c = createItem(j)) c->setSelected(true), created << id;
    }
    if (created.isEmpty()) return;
    States before;
    for (int id : created) before[id] = {};
    push(tr("Paste"), before, capture(created));
}

void ChartScene::selectAll() {
    for (auto* c : std::as_const(m_items)) c->setSelected(true);
}

// ---------------------------------------------------------------- dragging

void ChartScene::setGuides(const QVector<QLineF>& g) {
    if (g == guides) return;
    auto dirty = [this](const QVector<QLineF>& lines) {
        for (const auto& l : lines) update(QRectF(l.p1(), l.p2()).normalized().adjusted(-2, -2, 2, 2));
    };
    dirty(guides);
    guides = g;
    dirty(guides);
}

// ponytail: compares against every node (O(n) per mouse move). Fine to ~10k nodes; use the
// scene's BSP index (items(rect)) around the dragged node if it ever shows up in a profile.
QPointF ChartScene::adjustDrag(NodeItem* n, QPointF pos) {
    if (!snapEnabled || (QGuiApplication::keyboardModifiers() & Qt::AltModifier)) {
        setGuides({});
        return pos;
    }
    pos = snap(pos);
    if (selectedItems().size() > 1 || mouseGrabberItem() != n) return pos;

    const double thr = 6 / viewScale();
    const double hw = n->size.width() / 2, hh = n->size.height() / 2;
    double bestDx = thr, bestDy = thr, gx = 0, gy = 0;
    NodeItem *ox = nullptr, *oy = nullptr;
    for (auto* c : std::as_const(m_items)) {
        auto* o = qgraphicsitem_cast<NodeItem*>(c);
        if (!o || o == n) continue;
        const QPointF op = o->pos();
        const double ow = o->size.width() / 2, oh = o->size.height() / 2;
        for (double mine : {pos.x() - hw, pos.x(), pos.x() + hw})
            for (double theirs : {op.x() - ow, op.x(), op.x() + ow})
                if (std::abs(theirs - mine) < std::abs(bestDx)) bestDx = theirs - mine, gx = theirs, ox = o;
        for (double mine : {pos.y() - hh, pos.y(), pos.y() + hh})
            for (double theirs : {op.y() - oh, op.y(), op.y() + oh})
                if (std::abs(theirs - mine) < std::abs(bestDy)) bestDy = theirs - mine, gy = theirs, oy = o;
    }
    QVector<QLineF> g;
    if (ox) {
        pos.rx() += bestDx;
        const double y1 = std::min(pos.y() - hh, ox->pos().y() - ox->size.height() / 2);
        const double y2 = std::max(pos.y() + hh, ox->pos().y() + ox->size.height() / 2);
        g << QLineF(gx, y1 - 10, gx, y2 + 10);
    }
    if (oy) {
        pos.ry() += bestDy;
        const double x1 = std::min(pos.x() - hw, oy->pos().x() - oy->size.width() / 2);
        const double x2 = std::max(pos.x() + hw, oy->pos().x() + oy->size.width() / 2);
        g << QLineF(x1 - 10, gy, x2 + 10, gy);
    }
    setGuides(g);
    return pos;
}

void ChartScene::beginLinkDrag(NodeItem* n, Port p) {
    cancelLinkDrag();
    m_linkSrc = n;
    m_linkPort = p;
    QPen pen(QColor(37, 99, 235), 1.5, Qt::DashLine);
    pen.setCosmetic(true);
    m_preview = addPath(QPainterPath(), pen);
    m_preview->setZValue(1e6);
}

void ChartScene::setLinkTarget(NodeItem* n, int anchor) {
    if (n == linkTarget && anchor == linkAnchor) return;
    if (linkTarget) linkTarget->update();
    linkTarget = n, linkAnchor = anchor;
    if (n) n->update();
}

ChartScene::LinkDrop ChartScene::linkDropAt(QPointF pos, QPointF other, const NodeItem* exclude) const {
    LinkDrop d;
    // Nearest shape by distance to its box, within reach. Containers only win when no plain shape
    // is in reach, so the shapes inside a swimlane stay easy to hit. Ties go to the topmost.
    const double reach = 60 / std::max(viewScale(), 0.5);
    double best = 2 * reach;
    const QRectF area(pos - QPointF(reach, reach), pos + QPointF(reach, reach));
    for (auto* it : items(area, Qt::IntersectsItemBoundingRect)) {
        auto* n = qgraphicsitem_cast<NodeItem*>(it);
        if (!n || n == exclude) continue;
        const QPointF p = n->mapFromScene(pos);
        const QRectF r = n->rect();
        const double dist = std::hypot(std::max({r.left() - p.x(), 0.0, p.x() - r.right()}),
                                       std::max({r.top() - p.y(), 0.0, p.y() - r.bottom()}));
        const double score = dist + (isContainer(n->shapeType) ? reach : 0);
        if (dist < reach && score < best) best = score, d.shown = n;
    }
    if (d.shown) d.anchor = d.shown->hitAnchor(pos);
    d.node = d.anchor >= 0 ? d.shown : nodeAt(pos, exclude);
    if (!d.node) return d;
    const int a = d.node == d.shown ? d.anchor : -1;
    d.autoPort = a == 4;
    d.port = a >= 0 && a < 4 ? Port(a) : d.node->nearestPort(d.autoPort ? other : pos);
    return d;
}

void ChartScene::moveLinkDrag(QPointF pos) {
    if (!m_preview || !m_linkSrc) return;
    QPainterPath path(m_linkSrc->portScenePos(m_linkPort));
    const LinkDrop d = linkDropAt(pos, path.currentPosition(), m_linkSrc);
    setLinkTarget(d.shown, d.anchor);
    path.lineTo(pos);
    m_preview->setPath(path);
}

void ChartScene::cancelLinkDrag() {
    m_linkSrc = nullptr;
    delete std::exchange(m_preview, nullptr);
    setLinkTarget(nullptr);
}

void ChartScene::endLinkDrag(QPointF pos) {
    if (!m_linkSrc) return;
    NodeItem* src = m_linkSrc;
    const Port port = m_linkPort;
    cancelLinkDrag();

    const QPointF a = src->portScenePos(port);
    const bool click = QLineF(a, pos).length() < 20;
    const LinkDrop drop = linkDropAt(pos, a, src);
    NodeItem* target = drop.node;
    if (!target && !click && src->hitBody(pos)) return;  // dropped back on its own node

    QList<int> created;
    Port targetPort = drop.port;
    const bool targetAuto = drop.autoPort;
    if (!target) {
        // Quick-connect: make a copy of the source shape (empty text) where the drag ended,
        // or one step away in the arrow's direction when the arrow was just clicked.
        QPointF center = pos;
        if (click) {
            const QPointF d = portDir(port);
            center = src->pos() + QPointF(d.x() * (src->size.width() + 60), d.y() * (src->size.height() + 60));
        }
        center = snap(center);
        QJsonObject j = src->toJson();
        j["id"] = m_nextId;
        j["text"] = "";
        j["x"] = center.x();
        j["y"] = center.y();
        target = static_cast<NodeItem*>(createItem(j));
        created << target->id;
        targetPort = target->nearestPort(a);
    }
    const int linkId = m_nextId;
    auto* link = createItem({{"kind", "link"},
                             {"id", linkId},
                             {"from", src->id},
                             {"fromPort", portKey(port)},
                             {"to", target->id},
                             {"toPort", targetAuto ? QStringLiteral("auto") : portKey(targetPort)},
                             {"style", styleToJson(defaultLinkStyle())}});
    created << linkId;
    States before;
    for (int id : created) before[id] = {};
    push(created.size() > 1 ? tr("Add connected shape") : tr("Connect"), before, capture(created));
    clearSelection();
    (created.size() > 1 ? static_cast<QGraphicsItem*>(target) : link)->setSelected(true);
}

// ---------------------------------------------------------------- move / resize undo

QList<ChartItem*> ChartScene::dragMovingItems() const {
    auto* g = qgraphicsitem_cast<NodeItem*>(mouseGrabberItem());
    if (!g || g->isLinking()) return {};
    QSet<ChartItem*> out{g};
    for (int id : m_pressIds)  // selected shapes and what selected containers carry
        if (auto* n = nodeById(id)) out << n;
    for (auto* c : QSet<ChartItem*>(out))
        for (auto* l : std::as_const(static_cast<NodeItem*>(c)->links)) out << l;
    return out.values();
}

void ChartScene::moveContents(NodeItem* container, QPointF delta) {
    for (auto* n : m_riders.value(container)) n->moveBy(delta.x(), delta.y());
}

namespace {
// Containers sit under their contents and links, which can cover the container's handles and
// quick-connect arrows. Over one of those, lift the container to the top only while the event is
// routed, so it gets the click or keeps its hover. Nothing paints in between, so nothing shows.
template <class F>
void withGripOnTop(const ChartScene* scene, QPointF pos, F route) {
    NodeItem* lifted = nullptr;
    if (!scene->mouseGrabberItem())
        for (auto* it : scene->items(pos, Qt::IntersectsItemBoundingRect))
            if (auto* n = qgraphicsitem_cast<NodeItem*>(it); n && isContainer(n->shapeType) && n->hitGrip(pos)) {
                lifted = n;
                break;
            }
    const double z = lifted ? lifted->zValue() : 0;
    if (lifted) lifted->setZValue(1e9);
    route();
    if (lifted) lifted->setZValue(z);
}
}  // namespace

void ChartScene::mouseMoveEvent(QGraphicsSceneMouseEvent* e) {
    withGripOnTop(this, e->scenePos(), [&] { QGraphicsScene::mouseMoveEvent(e); });
}

void ChartScene::mousePressEvent(QGraphicsSceneMouseEvent* e) {
    withGripOnTop(this, e->scenePos(), [&] { QGraphicsScene::mousePressEvent(e); });
    m_pressIds.clear();
    m_riders.clear();
    QSet<NodeItem*> moving;
    for (auto* c : selectedChartItems())
        if (auto* n = qgraphicsitem_cast<NodeItem*>(c)) m_pressIds << n->id, moving << n;
    // Selected containers carry the unselected shapes inside them (each shape carried once).
    for (auto* n : QSet<NodeItem*>(moving))
        for (auto* r : contents(n))
            if (!moving.contains(r)) m_riders[n] << r, moving << r, m_pressIds << r->id;
    m_pressState = capture(m_pressIds);
}

void ChartScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* e) {
    QGraphicsScene::mouseReleaseEvent(e);
    setGuides({});
    settleLinkPaths();
    m_riders.clear();
    const States after = capture(std::exchange(m_pressIds, {}));
    for (const auto& j : after)
        if (j.isEmpty()) return;  // an item vanished mid-drag; nothing sensible to record
    push(tr("Move / resize"), m_pressState, after);
}
