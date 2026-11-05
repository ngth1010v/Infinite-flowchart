#include "Items.h"
#include "ChartScene.h"
#include <QCursor>
#include <QFontMetricsF>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QTextCursor>
#include <QTextDocument>
#include <cmath>
#include <utility>

namespace {
const QColor kAccent(37, 99, 235);
constexpr double kMargin = 48;  // room around a node for handles and quick-connect arrows
constexpr double kMinSize = 20;
}  // namespace

ChartScene* ChartItem::chart() const { return static_cast<ChartScene*>(scene()); }

double ChartItem::uiUnit() const {
    const double s = chart() ? chart()->viewScale() : 1.0;
    return 1.0 / std::max(s, 0.5);
}

// ---------------------------------------------------------------- TextEditor

void TextEditor::focusOutEvent(QFocusEvent* e) {
    QGraphicsTextItem::focusOutEvent(e);
    finish(true);
}

void TextEditor::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) return finish(false);
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && !(e->modifiers() & Qt::ShiftModifier))
        return finish(true);
    QGraphicsTextItem::keyPressEvent(e);
}

void TextEditor::finish(bool commit) {
    if (auto f = std::exchange(done, nullptr)) f(commit);
}

// ---------------------------------------------------------------- NodeItem

NodeItem::NodeItem(FlowShape shape, QSizeF sz) : shapeType(shape), size(sz) {
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    refresh();
}

NodeItem::~NodeItem() {
    if (m_editor) m_editor->done = nullptr;
    for (auto* l : std::as_const(links)) {
        if (l->from == this) l->from = nullptr;
        if (l->to == this) l->to = nullptr;
    }
}

Port NodeItem::nearestPort(QPointF scenePos) const {
    Port best = Port::Top;
    double bestD = 1e300;
    for (int i = 0; i < 4; ++i) {
        const double d = QLineF(portScenePos(Port(i)), scenePos).length();
        if (d < bestD) bestD = d, best = Port(i);
    }
    return best;
}

int NodeItem::hitAnchor(QPointF scenePos) const {
    const QPointF p = mapFromScene(scenePos);
    if (QLineF(p, QPointF()).length() <= 14 * uiUnit()) return 4;
    for (int i = 0; i < 4; ++i)
        if (QLineF(p, m_ports[i]).length() <= 12 * uiUnit()) return i;
    return -1;
}

void NodeItem::setFromJson(const QJsonObject& j) {
    shapeType = shapeFromKey(j["shape"].toString());
    size = QSizeF(std::clamp(j["w"].toDouble(140), kMinSize, 20000.0), std::clamp(j["h"].toDouble(70), kMinSize, 20000.0));
    setPos(std::clamp(j["x"].toDouble(), -4e5, 4e5), std::clamp(j["y"].toDouble(), -4e5, 4e5));
    text = j["text"].toString();
    style = styleFromJson(j["style"].toObject());
    refresh();
}

QJsonObject NodeItem::toJson() const {
    return {{"kind", "node"},          {"id", id},       {"shape", shapeKey(shapeType)},
            {"x", pos().x()},          {"y", pos().y()}, {"w", size.width()},
            {"h", size.height()},      {"text", text},   {"style", styleToJson(style)}};
}

void NodeItem::refresh() {
    prepareGeometryChange();
    const QRectF r = rect();
    m_outline = shapeOutline(shapeType, r);
    m_deco = shapeDecoration(shapeType, r);
    for (int i = 0; i < 4; ++i) m_ports[i] = shapePort(shapeType, r, Port(i));
    // Containers sit under links (-1) and shapes (0); bigger ones lower, so nested ones stay visible.
    setZValue(isContainer(shapeType) ? -2 - size.width() * size.height() * 1e-9 : 0);
    update();
    for (auto* l : std::as_const(links)) l->refresh();
}

void NodeItem::startEdit() {
    if (m_editor) return;
    auto* ed = new TextEditor(this);
    m_editor = ed;
    ed->document()->setDocumentMargin(0);
    ed->setFont(styleFont(style));
    ed->setDefaultTextColor(style.text);
    // Containers edit their title in the header band; the horizontal one's title runs bottom-to-top.
    const bool rotated = shapeType == FlowShape::HContainer;
    const QRectF band = textRect();
    ed->setTextWidth(std::max(10.0, (rotated ? band.height() : band.width()) - 16));
    if (rotated) ed->setRotation(-90);
    QTextOption opt = ed->document()->defaultTextOption();
    opt.setAlignment(style.align);
    ed->document()->setDefaultTextOption(opt);
    ed->setPlainText(text);
    ed->setTextInteractionFlags(Qt::TextEditorInteraction);
    auto place = [this, ed, rotated] {
        const QRectF b = textRect();
        const double eh = ed->boundingRect().height();
        if (rotated) ed->setPos(b.center().x() - eh / 2, b.bottom() - 8);
        else ed->setPos(b.left() + 8, b.center().y() - eh / 2);
    };
    QObject::connect(ed->document(), &QTextDocument::contentsChanged, ed, place);
    place();
    QTextCursor c(ed->document());
    c.select(QTextCursor::Document);
    ed->setTextCursor(c);
    ed->done = [this, ed](bool commit) {
        const QString t = ed->toPlainText();
        m_editor = nullptr;
        ed->hide();
        ed->deleteLater();
        update();
        if (commit && t != text && chart()) chart()->setItemText(this, t);
    };
    ed->setFocus();
    update();
}

void NodeItem::cancelEdit() {
    if (!m_editor) return;
    m_editor->done = nullptr;
    delete std::exchange(m_editor, nullptr);
    update();
}


bool NodeItem::showHandles() const {
    const auto* c = chart();
    return isSelected() && c && !c->overlaysHidden && !m_editor && c->selectedItems().size() == 1;
}

bool NodeItem::showArrows() const {
    const auto* c = chart();
    return m_hover && c && !c->overlaysHidden && !m_editor && m_mode != Mode::Resize && c->viewScale() >= 0.5;
}

QRectF NodeItem::handleRect(int corner) const {
    const QRectF r = rect();
    const QPointF pts[] = {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
    const double h = 4 * uiUnit();
    return {pts[corner] - QPointF(h, h), QSizeF(2 * h, 2 * h)};
}

QPointF NodeItem::arrowPos(int port) const { return m_ports[port] + portDir(Port(port)) * (18 * uiUnit()); }

int NodeItem::hitHandle(QPointF pos) const {
    if (!showHandles()) return -1;
    const double pad = 2 * uiUnit();
    for (int i = 0; i < 4; ++i)
        if (handleRect(i).adjusted(-pad, -pad, pad, pad).contains(pos)) return i;
    return -1;
}

int NodeItem::hitArrow(QPointF pos) const {
    if (!showArrows()) return -1;
    for (int i = 0; i < 4; ++i)
        if (QLineF(pos, arrowPos(i)).length() <= 8 * uiUnit()) return i;
    return -1;
}

QRectF NodeItem::boundingRect() const { return rect().adjusted(-kMargin, -kMargin, kMargin, kMargin); }

QPainterPath NodeItem::shape() const {
    QPainterPath p = m_outline;
    p.setFillRule(Qt::WindingFill);
    if (showArrows()) {
        // While hovered, the zone out to the arrows belongs to the node so they stay reachable.
        const double z = 26 * uiUnit();
        p.addRect(rect().adjusted(-z, -z, z, z));
    }
    if (showHandles())
        for (int i = 0; i < 4; ++i) p.addRect(handleRect(i));
    return p;
}

void NodeItem::paint(QPainter* p, const QStyleOptionGraphicsItem* opt, QWidget*) {
    const double lod = opt->levelOfDetailFromTransform(p->worldTransform());
    const QPen pen = style.dash == Qt::NoPen || style.strokeWidth <= 0
                         ? QPen(Qt::NoPen)
                         : QPen(style.stroke, style.strokeWidth, style.dash, Qt::FlatCap, Qt::MiterJoin);
    p->setBrush(style.fill);
    p->setPen(pen);
    p->drawPath(m_outline);
    if (isContainer(shapeType)) {
        QColor band = style.stroke;
        band.setAlpha(28);
        p->fillRect(textRect(), band);
    }
    if (!m_deco.isEmpty()) {
        p->setBrush(Qt::NoBrush);
        p->setPen(pen);
        p->drawPath(m_deco);
    }
    // Skip text once it would be under ~4px tall: unreadable and the main cost when zoomed out.
    if (!m_editor && !text.isEmpty() && lod * style.fontSize >= 4) {
        p->setPen(style.text);
        p->setFont(styleFont(style));
        const QRectF tr = textRect();
        const int flags = int(style.align) | Qt::AlignVCenter | Qt::TextWordWrap;
        if (shapeType == FlowShape::HContainer) {  // title reads bottom-to-top in the left band
            p->save();
            p->translate(tr.center());
            p->rotate(-90);
            p->drawText(QRectF(-tr.height() / 2 + 8, -tr.width() / 2, tr.height() - 16, tr.width()), flags, text);
            p->restore();
        } else {
            p->drawText(tr.adjusted(8, 4, -8, -4), flags, text);
        }
    }

    const auto* c = chart();
    if (!c || c->overlaysHidden) return;
    const double u = uiUnit();
    if (isSelected()) {
        QPen sel(kAccent, 1.5);
        sel.setCosmetic(true);
        p->setPen(sel);
        p->setBrush(Qt::NoBrush);
        p->drawRect(rect().adjusted(-3 * u, -3 * u, 3 * u, 3 * u));
        if (showHandles()) {
            p->setBrush(Qt::white);
            for (int i = 0; i < 4; ++i) p->drawRect(handleRect(i));
        }
    }
    if (c->linkTarget == this) {  // anchors for the link end being dragged; the one under the cursor pops
        QPen ring(kAccent, 1.5);
        ring.setCosmetic(true);
        p->setPen(ring);
        for (int i = 0; i < 5; ++i) {
            const bool hot = c->linkAnchor == i;
            p->setBrush(QColor(kAccent.red(), kAccent.green(), kAccent.blue(), hot ? 230 : 70));
            const double r = (hot ? 9 : 6) * u;
            p->drawEllipse(i < 4 ? m_ports[i] : QPointF(), r, r);
        }
    }
    if (showArrows()) {
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(kAccent.red(), kAccent.green(), kAccent.blue(), 200));
        for (int i = 0; i < 4; ++i) {
            const QPointF d = portDir(Port(i)), n(-d.y(), d.x()), a = arrowPos(i);
            p->drawPolygon(QPolygonF{a + d * 5 * u, a - d * 4 * u + n * 5 * u, a - d * 4 * u - n * 5 * u});
        }
    }
}

QVariant NodeItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    // Snap only shapes the user drags; shapes carried along inside a container keep their offsets.
    if (change == ItemPositionChange && m_mode == Mode::None && isSelected() && chart() && chart()->mouseGrabberItem())
        return chart()->adjustDrag(this, value.toPointF());
    if (change == ItemPositionHasChanged) {
        for (auto* l : std::as_const(links)) l->refresh();
        if (m_mode == Mode::None && isContainer(shapeType) && chart()) chart()->moveContents(this, pos() - m_prevPos);
        m_prevPos = pos();
    }
    return QGraphicsItem::itemChange(change, value);
}

void NodeItem::hoverEnterEvent(QGraphicsSceneHoverEvent*) {
    m_hover = true;
    update();
}

void NodeItem::hoverMoveEvent(QGraphicsSceneHoverEvent* e) {
    if (const int h = hitHandle(e->pos()); h >= 0)
        setCursor(h % 2 == 0 ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
    else if (hitArrow(e->pos()) >= 0)
        setCursor(Qt::CrossCursor);
    else
        unsetCursor();
}

void NodeItem::hoverLeaveEvent(QGraphicsSceneHoverEvent*) {
    m_hover = false;
    unsetCursor();
    update();
}

void NodeItem::mousePressEvent(QGraphicsSceneMouseEvent* e) {
    m_mode = Mode::None;
    if (e->button() == Qt::LeftButton) {
        if (const int h = hitHandle(e->pos()); h >= 0) {
            m_mode = Mode::Resize;
            m_fixedCorner = mapToScene(handleRect((h + 2) % 4).center());
            e->accept();
            return;
        }
        if (const int a = hitArrow(e->pos()); a >= 0) {
            m_mode = Mode::Link;
            chart()->beginLinkDrag(this, Port(a));
            e->accept();
            return;
        }
    }
    ChartItem::mousePressEvent(e);
}

void NodeItem::mouseMoveEvent(QGraphicsSceneMouseEvent* e) {
    if (m_mode == Mode::Resize) {
        const QPointF m = chart()->snap(e->scenePos(), e->modifiers());
        auto atLeast = [](double d) { return std::abs(d) < kMinSize ? (d < 0 ? -kMinSize : kMinSize) : d; };
        const QPointF d(atLeast(m.x() - m_fixedCorner.x()), atLeast(m.y() - m_fixedCorner.y()));
        const QRectF nr = QRectF(m_fixedCorner, m_fixedCorner + d).normalized();
        size = nr.size();
        setPos(nr.center());
        refresh();
        return;
    }
    if (m_mode == Mode::Link) return chart()->moveLinkDrag(e->scenePos());
    ChartItem::mouseMoveEvent(e);
}

void NodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* e) {
    const Mode mode = std::exchange(m_mode, Mode::None);
    if (mode == Mode::Resize) return;
    if (mode == Mode::Link) return chart()->endLinkDrag(e->scenePos());
    ChartItem::mouseReleaseEvent(e);
}

void NodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* e) {
    if (e->button() == Qt::LeftButton && hitHandle(e->pos()) < 0 && hitArrow(e->pos()) < 0) startEdit();
}

// ---------------------------------------------------------------- LinkItem

LinkItem::LinkItem() {
    setFlags(ItemIsSelectable);
    setAcceptHoverEvents(true);
    setZValue(-1);  // links stay under nodes (raised while selected, see itemChange)
}

LinkItem::~LinkItem() {
    if (from) from->links.removeOne(this);
    if (to) to->links.removeOne(this);
}

void LinkItem::setEnds(NodeItem* f, Port fp, NodeItem* t, Port tp, bool fAuto, bool tAuto) {
    if (from) from->links.removeOne(this);
    if (to) to->links.removeOne(this);
    from = f, to = t, fromPort = fp, toPort = tp, fromAuto = fAuto, toAuto = tAuto;
    if (from) from->links << this;
    if (to && to != from) to->links << this;
    refresh();
}

QJsonObject LinkItem::toJson() const {
    return {{"kind", "link"},
            {"id", id},
            {"from", from ? from->id : 0},
            {"fromPort", fromAuto ? QStringLiteral("auto") : portKey(fromPort)},
            {"to", to ? to->id : 0},
            {"toPort", toAuto ? QStringLiteral("auto") : portKey(toPort)},
            {"text", text},
            {"style", styleToJson(style)}};
}

namespace {
// Drop repeated points and merge straight runs going the same way.
QPolygonF simplify(const QPolygonF& in) {
    QPolygonF out;
    for (const QPointF& p : in) {
        if (!out.isEmpty() && QLineF(out.last(), p).length() < 0.5) continue;
        if (out.size() >= 2) {
            const QPointF u = out.last() - out[out.size() - 2], v = p - out.last();
            const double cross = u.x() * v.y() - u.y() * v.x(), dot = u.x() * v.x() + u.y() * v.y();
            if (std::abs(cross) < 1e-3 && dot > 0) {
                out.last() = p;
                continue;
            }
        }
        out << p;
    }
    return out;
}
}  // namespace

// ponytail: elbow routing with a fixed stub; links may cross other nodes. Add obstacle-avoiding
// routing (A* on a sparse grid) if users ask for it.
QPolygonF LinkItem::route(QPointF a, Port pa, QPointF b, Port pb) {
    constexpr double stub = 20;
    const QPointF da = portDir(pa), db = portDir(pb);
    const QPointF s = a + da * stub, e = b + db * stub;
    const bool ha = da.y() == 0, hb = db.y() == 0;
    auto ahead = [](double from, double to, double dir) { return (to - from) * dir >= 0; };
    QPolygonF pts{a, s};
    if (ha && hb) {
        const double mx = (s.x() + e.x()) / 2;
        if (ahead(s.x(), mx, da.x()) && ahead(e.x(), mx, db.x()))
            pts << QPointF(mx, s.y()) << QPointF(mx, e.y());
        else {
            const double my = (s.y() + e.y()) / 2;
            pts << QPointF(s.x(), my) << QPointF(e.x(), my);
        }
    } else if (!ha && !hb) {
        const double my = (s.y() + e.y()) / 2;
        if (ahead(s.y(), my, da.y()) && ahead(e.y(), my, db.y()))
            pts << QPointF(s.x(), my) << QPointF(e.x(), my);
        else {
            const double mx = (s.x() + e.x()) / 2;
            pts << QPointF(mx, s.y()) << QPointF(mx, e.y());
        }
    } else if (ha) {
        const QPointF c(e.x(), s.y());
        pts << (ahead(s.x(), c.x(), da.x()) && ahead(e.y(), c.y(), db.y()) ? c : QPointF(s.x(), e.y()));
    } else {
        const QPointF c(s.x(), e.y());
        pts << (ahead(s.y(), c.y(), da.y()) && ahead(e.x(), c.x(), db.x()) ? c : QPointF(e.x(), s.y()));
    }
    pts << e << b;
    return simplify(pts);
}

void LinkItem::refresh() {
    if (from && to) {  // auto ends face the other end: its port, or its center when it is auto too
        if (fromAuto) fromPort = from->nearestPort(toAuto ? to->scenePos() : to->portScenePos(toPort));
        if (toAuto) toPort = to->nearestPort(from->portScenePos(fromPort));
    }
    setPoints(from && to ? route(from->portScenePos(fromPort), fromPort, to->portScenePos(toPort), toPort)
                         : QPolygonF());
}

void LinkItem::setPoints(const QPolygonF& pts) {
    prepareGeometryChange();
    m_pts = pts;
    m_shape = QPainterPath();
    m_label = QRectF();
    m_bounds = QRectF();
    if (m_pts.size() < 2) return;
    QPainterPath path;
    path.addPolygon(m_pts);
    QPainterPathStroker stroker;
    stroker.setWidth(std::max(10.0, style.strokeWidth + 8));
    m_shape = stroker.createStroke(path);
    m_shape.setFillRule(Qt::WindingFill);
    if (!text.isEmpty()) {
        int best = 0;
        double bestLen = -1;
        for (int i = 0; i + 1 < m_pts.size(); ++i) {
            const double len = QLineF(m_pts[i], m_pts[i + 1]).length();
            if (len > bestLen) bestLen = len, best = i;
        }
        const QPointF mid = (m_pts[best] + m_pts[best + 1]) / 2;
        const QSizeF ts = QFontMetricsF(styleFont(style)).size(0, text) + QSizeF(10, 4);
        m_label = QRectF(mid - QPointF(ts.width() / 2, ts.height() / 2), ts);
        m_shape.addRect(m_label);
    }
    const double a = std::max(arrowSize() + 6, 20.0);  // also room for the end handles
    m_bounds = m_shape.boundingRect().adjusted(-a, -a, a, a);
    update();
}

// ---- dragging an end onto another shape (reconnect)

bool LinkItem::showEndHandles() const {
    const auto* c = chart();
    return isSelected() && c && !c->overlaysHidden && m_pts.size() >= 2;
}

int LinkItem::hitEnd(QPointF pos) const {
    if (!showEndHandles()) return -1;
    const double r = 9 * uiUnit();
    if (QLineF(pos, m_pts.first()).length() <= r) return 0;
    if (QLineF(pos, m_pts.last()).length() <= r) return 1;
    return -1;
}

QPainterPath LinkItem::shape() const {
    QPainterPath p = m_shape;
    if (showEndHandles()) {
        const double r = 9 * uiUnit();
        p.addEllipse(m_pts.first(), r, r);
        p.addEllipse(m_pts.last(), r, r);
    }
    return p;
}

QVariant LinkItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    // A selected link sits above nodes so its end handles (on node edges) can be grabbed.
    if (change == ItemSelectedHasChanged) setZValue(value.toBool() ? 1 : -1);
    return ChartItem::itemChange(change, value);
}

void LinkItem::hoverMoveEvent(QGraphicsSceneHoverEvent* e) {
    if (hitEnd(e->pos()) >= 0) setCursor(Qt::CrossCursor);
    else unsetCursor();
}

void LinkItem::hoverLeaveEvent(QGraphicsSceneHoverEvent*) { unsetCursor(); }

void LinkItem::mousePressEvent(QGraphicsSceneMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        if (const int end = hitEnd(e->pos()); end >= 0) {
            m_dragEnd = end;
            return e->accept();
        }
    }
    ChartItem::mousePressEvent(e);
}

void LinkItem::mouseMoveEvent(QGraphicsSceneMouseEvent* e) {
    if (m_dragEnd < 0 || !from || !to) return ChartItem::mouseMoveEvent(e);
    // Live preview: route from the fixed end to the shape under the cursor, or to the cursor.
    NodeItem* fixed = m_dragEnd == 0 ? to : from;
    const Port fixedPort = m_dragEnd == 0 ? toPort : fromPort;
    const QPointF a = fixed->portScenePos(fixedPort), p = e->scenePos();
    const auto drop = chart()->linkDropAt(p, a, fixed);
    chart()->setLinkTarget(drop.shown, drop.anchor);
    QPolygonF pts;
    if (drop.node) {
        pts = route(a, fixedPort, drop.node->portScenePos(drop.port), drop.port);
    } else {
        const QPointF d = a - p;  // loose end: approach from the fixed end's side
        const Port lp = std::abs(d.x()) > std::abs(d.y()) ? (d.x() > 0 ? Port::Right : Port::Left)
                                                          : (d.y() > 0 ? Port::Bottom : Port::Top);
        pts = route(a, fixedPort, p, lp);
    }
    if (m_dragEnd == 0) std::reverse(pts.begin(), pts.end());  // points always run source to target
    setPoints(pts);
}

void LinkItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* e) {
    if (m_dragEnd < 0) return ChartItem::mouseReleaseEvent(e);
    const int end = std::exchange(m_dragEnd, -1);
    chart()->setLinkTarget(nullptr);
    NodeItem* fixed = end == 0 ? to : from;
    const auto drop = fixed ? chart()->linkDropAt(e->scenePos(), fixed->portScenePos(end == 0 ? toPort : fromPort), fixed)
                            : ChartScene::LinkDrop{};
    if (drop.node)
        chart()->reconnectLink(this, end == 0, drop.node, drop.port, drop.autoPort);
    else
        refresh();  // dropped on empty canvas or its own other end: snap back
}

void LinkItem::paint(QPainter* p, const QStyleOptionGraphicsItem* opt, QWidget*) {
    const int n = int(m_pts.size());
    if (n < 2) return;
    const auto* c = chart();
    if (isSelected() && !(c && c->overlaysHidden)) {
        p->setPen(QPen(QColor(37, 99, 235, 90), style.strokeWidth + 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->drawPolyline(m_pts);
    }
    const double a = arrowSize();
    // Stop the line short of an arrow tip so thick lines don't poke out of it.
    QPolygonF line = m_pts;
    auto pullBack = [&](int tip, int prev) {
        const QLineF seg(line[prev], line[tip]);
        if (seg.length() > a) line[tip] = seg.pointAt(1 - a * 0.8 / seg.length());
    };
    if (style.arrowEnd) pullBack(n - 1, n - 2);
    if (style.arrowStart) pullBack(0, 1);
    if (style.dash != Qt::NoPen && style.strokeWidth > 0) {
        p->setPen(QPen(style.stroke, style.strokeWidth, style.dash, Qt::FlatCap, Qt::MiterJoin));
        p->setBrush(Qt::NoBrush);
        p->drawPolyline(line);
    }
    auto head = [&](QPointF prev, QPointF tip) {
        const QLineF seg(tip, prev);
        if (seg.length() < 0.01) return;
        const QPointF u = (prev - tip) / seg.length(), nrm(-u.y(), u.x());
        p->drawPolygon(QPolygonF{tip, tip + u * a + nrm * a * 0.45, tip + u * a - nrm * a * 0.45});
    };
    p->setPen(Qt::NoPen);
    p->setBrush(style.stroke);
    if (style.arrowEnd) head(m_pts[n - 2], m_pts[n - 1]);
    if (style.arrowStart) head(m_pts[1], m_pts[0]);
    const double lod = opt->levelOfDetailFromTransform(p->worldTransform());
    if (!m_label.isEmpty() && lod * style.fontSize >= 4) {
        p->setBrush(style.fill);
        p->drawRoundedRect(m_label, 3, 3);
        p->setPen(style.text);
        p->setFont(styleFont(style));
        p->drawText(m_label, int(style.align) | Qt::AlignVCenter, text);
    }
    if (showEndHandles()) {
        QPen hp(kAccent, 1.5);
        hp.setCosmetic(true);
        p->setPen(hp);
        p->setBrush(Qt::white);
        const double r = 5 * uiUnit();
        p->drawEllipse(m_pts.first(), r, r);
        p->drawEllipse(m_pts.last(), r, r);
    }
}

void LinkItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent*) {
    auto* c = chart();
    if (!c) return;
    bool ok = false;
    const QString t = QInputDialog::getMultiLineText(c->views().value(0), QObject::tr("Link label"),
                                                     QObject::tr("Label:"), text, &ok);
    if (ok && t != text) c->setItemText(this, t);
}
