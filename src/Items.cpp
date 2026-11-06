#include "Items.h"
#include "ChartScene.h"
#include <QCursor>
#include <QFontMetricsF>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QJsonArray>
#include <QKeyEvent>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QTextCursor>
#include <QTextDocument>
#include <algorithm>
#include <cmath>
#include <queue>
#include <utility>

namespace {
const QColor kAccent(37, 99, 235);
constexpr double kMargin = 64;  // room around a node for handles and quick-connect arrows (at 2x uiUnit)
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
        if (QLineF(p, m_ports[i]).length() <= 16 * uiUnit()) return i;
    return -1;
}

void NodeItem::setFromJson(const QJsonObject& j) {
    shapeType = shapeFromKey(j["shape"].toString());
    size = QSizeF(std::clamp(j["w"].toDouble(140), kMinSize, 20000.0), std::clamp(j["h"].toDouble(70), kMinSize, 20000.0));
    setPos(std::clamp(j["x"].toDouble(), -4e5, 4e5), std::clamp(j["y"].toDouble(), -4e5, 4e5));
    text = j["text"].toString();
    style = styleFromJson(j["style"].toObject());
    padding = std::clamp(j["padding"].toDouble(40), 0.0, 2000.0);
    refresh();
}

QJsonObject NodeItem::toJson() const {
    QJsonObject j{{"kind", "node"},          {"id", id},       {"shape", shapeKey(shapeType)},
                  {"x", pos().x()},          {"y", pos().y()}, {"w", size.width()},
                  {"h", size.height()},      {"text", text},   {"style", styleToJson(style)}};
    if (isContainer(shapeType)) j["padding"] = padding;
    return j;
}

void NodeItem::refresh() {
    prepareGeometryChange();
    const QRectF r = rect();
    m_bounds = r.adjusted(-kMargin, -kMargin, kMargin, kMargin);
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
    const QSizeF startSize = size;
    const QPointF startPos = pos();
    auto place = [this, ed, rotated, startSize] {
        if (!rotated) {  // live: grow while typing, and wrap at the new width
            growToFit(ed->toPlainText(), startSize);
            ed->setTextWidth(std::max(10.0, textRect().width() - 16));
        }
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
    ed->done = [this, ed, startSize, startPos](bool commit) {
        const QString t = ed->toPlainText();
        m_editor = nullptr;
        ed->hide();
        ed->deleteLater();
        // Undo the live growth; setItemText grows again as one undoable step.
        size = startSize;
        setPos(startPos);
        refresh();
        if (commit && t != text && chart()) chart()->setItemText(this, t);
    };
    ed->setFocus();
    update();
}

double NodeItem::textHeight(const QString& t, double width) const {
    const QFontMetricsF fm(styleFont(style));
    const int flags = int(style.align) | Qt::TextWordWrap;
    const double h = fm.boundingRect(QRectF(0, 0, std::max(10.0, width), 1e6), flags, t.isEmpty() ? QStringLiteral(" ") : t).height();
    return h + 8 + 16;  // paint() keeps 4 px top and bottom, plus some breathing room
}

void NodeItem::growToFit(const QString& t, QSizeF minSize) {
    if (isContainer(shapeType)) return;  // container titles live in the header band
    // Widen first, up to kMaxTextWidth, so long text reads in lines, not a tall column; a single
    // longer word widens past it, since word wrap cannot break it. Then grow the height to fit.
    constexpr double kMaxTextWidth = 320;
    const QFontMetricsF fm(styleFont(style));
    double lineW = 0, wordW = 0;
    for (const QString& line : t.split('\n')) {
        lineW = std::max(lineW, fm.horizontalAdvance(line));
        for (const QString& word : line.split(' ', Qt::SkipEmptyParts)) wordW = std::max(wordW, fm.horizontalAdvance(word));
    }
    const double needW = std::max(std::min(lineW, kMaxTextWidth), wordW) + 16 + 2;  // paint() keeps 8 px each side
    // The text area is an inset of the node (shapeTextRect), so grow the node until that area fits.
    QSizeF sz = minSize;
    for (int i = 0; i < 20; ++i) {
        const QRectF area = shapeTextRect(shapeType, QRectF(QPointF(), sz));
        if (area.width() < needW - 0.5) {
            sz.rwidth() += (needW - area.width()) * sz.width() / std::max(1.0, area.width());
            continue;
        }
        const double needH = textHeight(t, area.width() - 16);
        if (area.height() < needH - 0.5) {
            sz.rheight() += (needH - area.height()) * sz.height() / std::max(1.0, area.height());
            continue;
        }
        break;
    }
    if (std::abs(sz.width() - size.width()) < 0.5 && std::abs(sz.height() - size.height()) < 0.5) return;
    setPos(pos() + QPointF(0, (sz.height() - size.height()) / 2));  // keep the top edge; width grows both ways
    size = sz;
    refresh();
}

void ChartItem::cancelEdit() {
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

QRectF NodeItem::handleRect(int handle) const {
    const QRectF r = rect();
    // 0-3: corners (resize both ways), 4-7: middle of top, right, bottom, left (resize one way).
    const QPointF pts[] = {r.topLeft(),  r.topRight(),    r.bottomRight(),  r.bottomLeft(),
                           {0, r.top()}, {r.right(), 0}, {0, r.bottom()}, {r.left(), 0}};
    const double h = 4 * uiUnit();
    return {pts[handle] - QPointF(h, h), QSizeF(2 * h, 2 * h)};
}

QPointF NodeItem::arrowPos(int port) const { return m_ports[port] + portDir(Port(port)) * (18 * uiUnit()); }

// Grab area of a handle: wider than it is drawn, so it is easy to hit.
QRectF NodeItem::handleHitRect(int handle) const {
    const double pad = 6 * uiUnit();
    return handleRect(handle).adjusted(-pad, -pad, pad, pad);
}

int NodeItem::hitHandle(QPointF pos) const {
    if (!showHandles()) return -1;
    for (int i = 0; i < kHandles; ++i)
        if (handleHitRect(i).contains(pos)) return i;
    return -1;
}

int NodeItem::hitArrow(QPointF pos) const {
    if (!showArrows()) return -1;
    for (int i = 0; i < 4; ++i)
        if (QLineF(pos, arrowPos(i)).length() <= 12 * uiUnit()) return i;
    return -1;
}

QPainterPath NodeItem::shape() const {
    // Containers are hit only on their border and title band; clicks in the body fall through to
    // the canvas (rubber-band selection) and the shapes inside.
    // Containers are built with real unions: plain addPath under WindingFill would fill the hole.
    const bool box = isContainer(shapeType);
    const double edge = 12 * uiUnit();  // grab band on each side of a container's border
    const double z = 32 * uiUnit();    // while hovered: out past the quick-connect arrows
    QPainterPath p;
    if (box) {
        QPainterPath outer, body, header;
        outer.addRect(rect().adjusted(-(showArrows() ? z : edge), -(showArrows() ? z : edge),
                                      showArrows() ? z : edge, showArrows() ? z : edge));
        body.addRect(rect().adjusted(edge, edge, -edge, -edge));
        header.addRect(textRect());
        p = outer.subtracted(body).united(header);
    } else {
        p = m_outline;
        p.setFillRule(Qt::WindingFill);
        if (showArrows()) p.addRect(rect().adjusted(-z, -z, z, z));  // keep the arrows reachable
    }
    if (showHandles()) {
        QPainterPath grips;
        for (int i = 0; i < kHandles; ++i) grips.addRect(handleHitRect(i));
        // A container's path comes from boolean ops (odd-even fill): an added handle overlapping
        // its border band would cancel out into a hole there, so unite instead.
        if (box) p = p.united(grips);
        else p.addPath(grips);
    }
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
            for (int i = 0; i < kHandles; ++i) p->drawRect(handleRect(i));
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
        setCursor(h >= 4 ? (h % 2 == 0 ? Qt::SizeVerCursor : Qt::SizeHorCursor)
                         : (h % 2 == 0 ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor));
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
            // An edge handle drags the corner at its clockwise start, with one axis held still.
            const int corner = h % 4;
            m_fixedCorner = mapToScene(handleRect((corner + 2) % 4).center());
            m_movingCorner = mapToScene(handleRect(corner).center());
            m_lockX = h == 4 || h == 6;
            m_lockY = h == 5 || h == 7;
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
        QPointF m = chart()->snap(e->scenePos(), e->modifiers());
        if (m_lockX) m.setX(m_movingCorner.x());
        if (m_lockY) m.setY(m_movingCorner.y());
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
    if (m_editor) m_editor->done = nullptr;
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

QJsonArray LinkItem::pathJson() const {
    QJsonArray a;
    for (const QPointF& p : path) a << QJsonArray{p.x(), p.y()};
    return a;
}

QJsonObject LinkItem::toJson() const {
    return {{"kind", "link"},
            {"id", id},
            {"from", from ? from->id : 0},
            {"fromPort", fromAuto ? QStringLiteral("auto") : portKey(fromPort)},
            {"to", to ? to->id : 0},
            {"toPort", toAuto ? QStringLiteral("auto") : portKey(toPort)},
            {"path", pathJson()},
            {"text", text},
            {"style", styleToJson(style)}};
}

namespace {
// Drop repeated points and merge straight runs going the same way.
QPolygonF simplify(const QPolygonF& in) {
    QPolygonF out;
    for (const QPointF& p : in) {
        // Exact repeats only: dropping a near one (a tiny jog's corner) would leave a slanted segment.
        if (!out.isEmpty() && QLineF(out.last(), p).length() < 1e-3) continue;
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

namespace {
constexpr double kStub = LinkItem::kStub;
constexpr double kPortSlack = 10;  // how far facing ends may slide off their side's center to line up

// Orthogonal polyline a -> stub -> the user's points -> stub -> b. `path` is the user's polyline from
// stub to stub; where an end shape has moved since, a corner joins its new stub to the user's
// first/last point so the user's segments stay where they were put.
QPolygonF throughPath(QPointF a, Port pa, QPointF b, Port pb, const QPolygonF& path) {
    const QPointF s = a + portDir(pa) * kStub, e = b + portDir(pb) * kStub;
    const QPolygonF mid = path.mid(1, path.size() - 2);
    auto apart = [](QPointF p, QPointF q) { return std::abs(p.x() - q.x()) > 1e-6 && std::abs(p.y() - q.y()) > 1e-6; };
    QPolygonF pts{a, s};
    const bool h0 = std::abs(path.first().y() - mid.first().y()) < 0.01;  // user's first segment is horizontal
    if (apart(s, mid.first())) pts << (h0 ? QPointF(s.x(), mid.first().y()) : QPointF(mid.first().x(), s.y()));
    pts << mid;
    const bool h1 = std::abs(mid.last().y() - path.last().y()) < 0.01;
    if (apart(mid.last(), e)) pts << (h1 ? QPointF(e.x(), mid.last().y()) : QPointF(mid.last().x(), e.y()));
    pts << e << b;
    return simplify(pts);
}
}  // namespace

QPolygonF LinkItem::route(QPointF a, Port pa, QPointF b, Port pb) {
    constexpr double stub = kStub;
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
    if (!from || !to) return setPoints({});
    QPointF a = from->portScenePos(fromPort), b = to->portScenePos(toPort);
    if (path.size() < 3) {
        // Facing ends that are nearly level: slide both along their sides to meet halfway, so the
        // link runs straight instead of making a tiny jog. Only where the outline is flat there.
        const QPointF da = portDir(fromPort), db = portDir(toPort);
        if (da == -db && QPointF::dotProduct(b - a, da) > 0) {
            const QPointF along(std::abs(da.y()), std::abs(da.x()));
            const double off = QPointF::dotProduct(b - a, along);
            const QPointF a2 = a + along * off / 2, b2 = b - along * off / 2;
            auto onEdge = [](const NodeItem* n, QPointF p, QPointF out) { return n->hitBody(p - out) && !n->hitBody(p + out); };
            if (std::abs(off) > 1e-6 && std::abs(off) <= kPortSlack && onEdge(from, a2, da) && onEdge(to, b2, db)) a = a2, b = b2;
        }
        return setPoints(route(a, fromPort, b, toPort));
    }
    // Both ends moved by the same amount (group drag, container, undo of one): the path moves along.
    const QPointF d1 = from->scenePos() - m_pathFrom, d2 = to->scenePos() - m_pathTo;
    if (QLineF(d1, d2).length() < 0.01 && !d1.isNull()) {
        path.translate(d1);
        m_pathFrom += d1, m_pathTo += d1;
    }
    setPoints(throughPath(a, fromPort, b, toPort, path));
}

QPolygonF LinkItem::pathFromPoints(const QPolygonF& pts) const {
    // The stubs are often merged into the runs next to them (a 3-line link is just 4 points), so put
    // them back: without them a middle-line edit left only the two bends, too few to keep.
    QPolygonF p = pts.mid(1, pts.size() - 2);
    const QPointF s = pts.first() + portDir(fromPort) * kStub, e = pts.last() + portDir(toPort) * kStub;
    if (p.isEmpty() || QLineF(p.first(), s).length() > 0.01) p.prepend(s);
    if (QLineF(p.last(), e).length() > 0.01) p.append(e);
    return simplify(p);
}

void LinkItem::setPath(const QPolygonF& p) {
    path = p;
    settlePath();
    refresh();
}

void LinkItem::settlePath() {
    if (from && to) m_pathFrom = from->scenePos(), m_pathTo = to->scenePos();
}

QPolygonF LinkItem::dragSegment(const QPolygonF& pts, int seg, QPointF pos) const {
    const int n = int(pts.size());
    const QPointF p = pts[seg], q = pts[seg + 1];
    const bool horiz = std::abs(p.y() - q.y()) < 0.01;
    auto moved = [&](QPointF v) { return horiz ? QPointF(v.x(), pos.y()) : QPointF(pos.x(), v.y()); };
    // Keep p and q, and join them to the moved segment with square corners. Moving p and q
    // themselves only works when the neighbor segments are at right angles to this one; a neighbor
    // on the same line (the link doubles back) would turn diagonal.
    QPolygonF out = pts.mid(0, seg);
    if (seg == 0) {  // the first run leaves the port: keep its stub, bend off after it
        const QPointF s = pts[0] + portDir(fromPort) * kStub;
        out << pts[0] << s << moved(s);
    } else {
        out << p << moved(p);
    }
    if (seg + 1 == n - 1) {  // likewise for the last run
        const QPointF e = pts[n - 1] + portDir(toPort) * kStub;
        out << moved(e) << e << pts[n - 1];
    } else {
        out << moved(q) << q << pts.mid(seg + 2);
    }
    // Fold runs that go out and back along one line: they draw over themselves and leave hidden
    // segment handles. Put the stub points in first and keep them (index 1 and n-2).
    const QPointF s = out.first() + portDir(fromPort) * kStub, e = out.last() + portDir(toPort) * kStub;
    if (QLineF(out[1], s).length() > 1e-6) out.insert(1, s);
    if (QLineF(out[out.size() - 2], e).length() > 1e-6) out.insert(out.size() - 1, e);
    for (int i = 2; i + 2 < out.size();) {
        const QPointF u = out[i] - out[i - 1], v = out[i + 1] - out[i];
        if (std::abs(u.x() * v.y() - u.y() * v.x()) < 1e-3) {
            out.remove(i);
            i = std::max(2, i - 1);
        } else {
            ++i;
        }
    }
    return simplify(out);
}

namespace {
// Polyline with lightly rounded corners (radius r, less on short segments).
QPainterPath roundedPolyline(const QPolygonF& pts, double r) {
    QPainterPath path(pts.first());
    for (int i = 1; i + 1 < pts.size(); ++i) {
        const QPointF v = pts[i];
        const QLineF in(pts[i - 1], v), out(v, pts[i + 1]);
        if (in.length() < 1e-6 || out.length() < 1e-6) {
            path.lineTo(v);
            continue;
        }
        const double d = std::min({r, in.length() / 2, out.length() / 2});
        path.lineTo(v + (pts[i - 1] - v) * (d / in.length()));
        path.quadTo(v, v + (pts[i + 1] - v) * (d / out.length()));
    }
    path.lineTo(pts.last());
    return path;
}
constexpr double kLinkCorner = 8;
}  // namespace

void LinkItem::setPoints(const QPolygonF& pts) {
    prepareGeometryChange();
    m_pts = pts;
    m_shape = QPainterPath();
    m_label = QRectF();
    m_bounds = QRectF();
    if (m_pts.size() < 2) return;
    const QPainterPath path = roundedPolyline(m_pts, kLinkCorner);
    QPainterPathStroker stroker;
    stroker.setWidth(std::max(14.0, style.strokeWidth + 10));
    m_shape = stroker.createStroke(path);
    m_shape.setFillRule(Qt::WindingFill);
    if (!text.isEmpty() || m_editor) {
        // Center of the link by length, set beside the line there so the label never covers it.
        double half = 0;
        for (int i = 0; i + 1 < m_pts.size(); ++i) half += QLineF(m_pts[i], m_pts[i + 1]).length() / 2;
        int seg = 0;
        for (; seg + 2 < m_pts.size(); ++seg) {
            const double len = QLineF(m_pts[seg], m_pts[seg + 1]).length();
            if (half <= len) break;
            half -= len;
        }
        const QLineF s(m_pts[seg], m_pts[seg + 1]);
        const QPointF mid = s.length() > 0 ? s.pointAt(std::min(1.0, half / s.length())) : s.p1();
        const QSizeF ts = QFontMetricsF(styleFont(style)).size(0, text.isEmpty() ? QStringLiteral(" ") : text) + QSizeF(10, 4);
        const double w = ts.width(), h = ts.height(), gap = 2 + style.strokeWidth / 2;
        const bool horiz = std::abs(s.dx()) >= std::abs(s.dy());
        // Above or below a horizontal stretch, right or left of a vertical one: first side clear of the line.
        const QRectF sides[2] = {
            horiz ? QRectF(mid.x() - w / 2, mid.y() - gap - h, w, h) : QRectF(mid.x() + gap, mid.y() - h / 2, w, h),
            horiz ? QRectF(mid.x() - w / 2, mid.y() + gap, w, h) : QRectF(mid.x() - gap - w, mid.y() - h / 2, w, h)};
        QPainterPathStroker thin;
        thin.setWidth(style.strokeWidth + 1);
        const QPainterPath line = thin.createStroke(path);
        m_label = line.intersects(sides[0]) && !line.intersects(sides[1]) ? sides[1] : sides[0];
        m_shape.addRect(m_label);
    }
    const double a = std::max(arrowSize() + 6, 24.0);  // also room for the end handles (12 x uiUnit, up to 2)
    m_bounds = m_shape.boundingRect().adjusted(-a, -a, a, a);
    update();
}


// ---- dragging an end onto another shape (reconnect)

bool LinkItem::showEndHandles() const {
    const auto* c = chart();
    return isSelected() && c && !c->overlaysHidden && m_pts.size() >= 2;
}

bool LinkItem::segDraggable(int seg) const { return QLineF(m_pts[seg], m_pts[seg + 1]).length() >= 16; }

int LinkItem::hitSegment(QPointF pos) const {
    if (!showEndHandles()) return -1;
    for (int i = 0; i + 1 < m_pts.size(); ++i)
        if (segDraggable(i) && QLineF(pos, (m_pts[i] + m_pts[i + 1]) / 2).length() <= 11 * uiUnit()) return i;
    return -1;
}

int LinkItem::hitEnd(QPointF pos) const {
    if (!showEndHandles()) return -1;
    const double r = 12 * uiUnit();
    if (QLineF(pos, m_pts.first()).length() <= r) return 0;
    if (QLineF(pos, m_pts.last()).length() <= r) return 1;
    return -1;
}

QPainterPath LinkItem::shape() const {
    QPainterPath p = m_shape;
    if (showEndHandles()) {
        const double r = 12 * uiUnit();
        p.addEllipse(m_pts.first(), r, r);
        p.addEllipse(m_pts.last(), r, r);
        for (int i = 0; i + 1 < m_pts.size(); ++i)
            if (segDraggable(i)) p.addEllipse((m_pts[i] + m_pts[i + 1]) / 2, 11 * uiUnit(), 11 * uiUnit());
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
    else if (const int seg = hitSegment(e->pos()); seg >= 0)
        setCursor(std::abs(m_pts[seg].y() - m_pts[seg + 1].y()) < 0.01 ? Qt::SizeVerCursor : Qt::SizeHorCursor);
    else unsetCursor();
}

void LinkItem::hoverLeaveEvent(QGraphicsSceneHoverEvent*) { unsetCursor(); }

void LinkItem::mousePressEvent(QGraphicsSceneMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        if (const int end = hitEnd(e->pos()); end >= 0) {
            m_dragEnd = end;
            return e->accept();
        }
        if (const int seg = hitSegment(e->pos()); seg >= 0) {
            m_dragSeg = seg;
            m_dragBase = m_pts;
            return e->accept();
        }
    }
    ChartItem::mousePressEvent(e);
}

void LinkItem::mouseMoveEvent(QGraphicsSceneMouseEvent* e) {
    if (m_dragSeg >= 0) return setPoints(dragSegment(m_dragBase, m_dragSeg, chart()->snap(e->scenePos(), e->modifiers())));
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
    if (m_dragSeg >= 0) {  // keep the edited path (stub to stub) as the link's own
        const QPolygonF pts = dragSegment(m_dragBase, std::exchange(m_dragSeg, -1), chart()->snap(e->scenePos(), e->modifiers()));
        return chart()->setLinkPath(this, pathFromPoints(pts));
    }
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
        p->setBrush(Qt::NoBrush);
        p->drawPath(roundedPolyline(m_pts, kLinkCorner));
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
        p->drawPath(roundedPolyline(line, kLinkCorner));
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
    if (!m_label.isEmpty() && !m_editor && lod * style.fontSize >= 4) {
        p->setBrush(style.fill);  // transparent by default
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
        p->setBrush(kAccent);  // segment handles: drag to slide that segment sideways
        const double h = 3.5 * uiUnit();
        for (int i = 0; i + 1 < m_pts.size(); ++i)
            if (segDraggable(i)) p->drawRoundedRect(QRectF((m_pts[i] + m_pts[i + 1]) / 2 - QPointF(h, h), QSizeF(2 * h, 2 * h)), h / 2, h / 2);
    }
}

void LinkItem::startEdit() {
    if (m_editor || m_pts.size() < 2) return;
    auto* ed = new TextEditor(this);
    m_editor = ed;
    ed->document()->setDocumentMargin(0);
    ed->setFont(styleFont(style));
    ed->setDefaultTextColor(style.text);
    QTextOption opt = ed->document()->defaultTextOption();
    opt.setAlignment(style.align);
    ed->document()->setDefaultTextOption(opt);
    ed->setPlainText(text);
    ed->setTextInteractionFlags(Qt::TextEditorInteraction);
    const QString startText = text;
    auto place = [this, ed] {  // live: the label box follows the text being typed
        text = ed->toPlainText();
        setPoints(m_pts);
        ed->setPos(m_label.topLeft() + QPointF(5, 2));
    };
    QObject::connect(ed->document(), &QTextDocument::contentsChanged, ed, place);
    place();
    QTextCursor c(ed->document());
    c.select(QTextCursor::Document);
    ed->setTextCursor(c);
    ed->done = [this, ed, startText](bool commit) {
        const QString t = ed->toPlainText();
        m_editor = nullptr;
        ed->hide();
        ed->deleteLater();
        text = startText;  // setItemText applies the new text as one undoable step
        setPoints(m_pts);
        if (commit && t != text && chart()) chart()->setItemText(this, t);
    };
    ed->setFocus();
    update();
}

void LinkItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* e) {
    if (e->button() == Qt::LeftButton) startEdit();
}
