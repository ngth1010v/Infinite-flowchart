#include "ChartView.h"
#include "ChartScene.h"
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QScrollBar>
#include <QStyleOptionGraphicsItem>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <utility>

// Overview of the whole chart in the corner. Click or drag to jump there.
// The view paints it (drawForeground) instead of it being a child widget: any widget over the
// viewport stops Qt from scrolling the canvas by moving its pixels, so every pan step would
// repaint the whole view.
class Minimap {
public:
    static constexpr int kW = 220, kH = 150, kMargin = 12;
    bool visible = true;

    explicit Minimap(ChartView* view) : m_view(view) {
        m_timer.setSingleShot(true);
        m_timer.setInterval(300);  // re-render at most ~3x/s while the chart changes
        QObject::connect(&m_timer, &QTimer::timeout, view, [this] { rerender(); });
    }
    QRect rect() const { return {m_view->viewport()->width() - kW - kMargin, kMargin, kW, kH}; }  // viewport coords
    bool contains(QPoint vpPos) const { return visible && rect().contains(vpPos); }
    void scheduleRender() {
        if (visible) m_timer.start();
    }
    void update() {
        if (visible) m_view->viewport()->update(rect());
    }
    // After the canvas scrolled by (dx, dy): its pixels, minimap included, moved along. Repaint
    // both the stale copy and the minimap's own spot.
    void scrolled(int dx, int dy) {
        if (!visible) return;
        m_view->viewport()->update(rect().translated(dx, dy));
        update();
    }
    void rerender() {
        if (!visible) return;
        auto* s = m_view->chart();
        QRectF world = s->contentRect();
        if (world.isNull()) world = QRectF(-500, -350, 1000, 700);
        world.adjust(-40, -40, 40, 40);
        // Grow the world rect to the minimap's aspect so one scale factor maps both axes.
        const double k = std::min(kW / world.width(), kH / world.height());
        const QSizeF need(kW / k, kH / k);
        m_world = QRectF(world.center() - QPointF(need.width() / 2, need.height() / 2), need);

        const qreal dpr = m_view->viewport()->devicePixelRatioF();
        m_img = QImage(QSize(kW, kH) * dpr, QImage::Format_ARGB32_Premultiplied);
        m_img.setDevicePixelRatio(dpr);
        m_img.fill(Qt::transparent);
        QPainter p(&m_img);
        p.setRenderHint(QPainter::Antialiasing);
        s->overlaysHidden = true;
        s->render(&p, QRectF(0, 0, kW, kH), m_world);
        s->overlaysHidden = false;
        update();
    }
    void paint(QPainter* p) {  // p is in viewport coordinates
        if (m_img.isNull()) m_timer.start(0);  // first show: render soon, not inside this paint
        const QRect r = rect();
        const bool dark = m_view->isDark();
        p->fillRect(r, dark ? QColor(38, 40, 46, 235) : QColor(255, 255, 255, 235));
        p->drawImage(r.topLeft(), m_img);
        if (!m_world.isEmpty()) {
            const double k = kW / m_world.width();
            const QRectF vr = m_view->mapToScene(m_view->viewport()->rect()).boundingRect();
            const QRectF mapped(r.topLeft() + (vr.topLeft() - m_world.topLeft()) * k, vr.size() * k);
            p->save();
            p->setClipRect(r);
            p->setPen(QPen(QColor(37, 99, 235), 1.5));
            p->setBrush(QColor(37, 99, 235, 30));
            p->drawRect(mapped);
            p->restore();
        }
        p->setPen(QPen(dark ? QColor(70, 72, 80) : QColor(203, 213, 225), 1));
        p->setBrush(Qt::NoBrush);
        p->drawRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5));
    }
    void jump(QPoint vpPos) {
        if (m_world.isEmpty()) return;
        m_view->centerOn(m_world.topLeft() + QPointF(vpPos - rect().topLeft()) / (kW / m_world.width()));
        update();
    }

private:
    ChartView* m_view;
    QTimer m_timer;
    QImage m_img;
    QRectF m_world;
};

// ---------------------------------------------------------------- ChartView

ChartView::ChartView(ChartScene* scene, QWidget* parent) : QGraphicsView(scene, parent) {
    m_minimap = std::make_unique<Minimap>(this);  // first: the setters below already scroll, which updates it
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    setDragMode(RubberBandDrag);
    setRubberBandSelectionMode(Qt::IntersectsItemShape);
    setTransformationAnchor(AnchorUnderMouse);
    setResizeAnchor(NoAnchor);  // see keepContentInPlace()
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setAcceptDrops(true);
    // A fixed, very large canvas: effectively borderless, and fits comfortably in float/int ranges.
    setSceneRect(-500000, -500000, 1000000, 1000000);
    connect(scene, &QGraphicsScene::changed, this, [this] { m_minimap->scheduleRender(); });
    setDark(false);
}

ChartView::~ChartView() = default;

ChartScene* ChartView::chart() const { return static_cast<ChartScene*>(scene()); }

void ChartView::viewChanged() {
    m_minimap->update();
    emit zoomChanged(zoom());
}

void ChartView::zoomBy(double factor, bool atMouse) {
    const double z = std::clamp(zoom() * factor, kMinZoom, kMaxZoom);
    if (std::abs(z - zoom()) < 1e-9) return;
    setTransformationAnchor(atMouse ? AnchorUnderMouse : AnchorViewCenter);
    scale(z / zoom(), z / zoom());
    setTransformationAnchor(AnchorUnderMouse);
    viewChanged();
}

void ChartView::zoomAt(double factor, const QPoint& viewportPos) {
    const double z = std::clamp(zoom() * factor, kMinZoom, kMaxZoom);
    if (std::abs(z - zoom()) < 1e-9) return;
    const QPointF under = mapToScene(viewportPos);
    setTransformationAnchor(NoAnchor);
    scale(z / zoom(), z / zoom());
    setTransformationAnchor(AnchorUnderMouse);
    // Scroll so the point that was under viewportPos is back under it.
    const QPoint drift = mapFromScene(under) - viewportPos;
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + drift.x());
    verticalScrollBar()->setValue(verticalScrollBar()->value() + drift.y());
    viewChanged();
}

void ChartView::fitAll() {
    const QRectF r = chart()->contentRect();
    if (r.isNull()) return resetView();
    fitInView(r, Qt::KeepAspectRatio);
    // Small charts are shown at 100% instead of blown up.
    if (zoom() > 1 || zoom() < kMinZoom) {
        const double z = std::clamp(zoom(), kMinZoom, 1.0);
        setTransform(QTransform::fromScale(z, z));
        centerOn(r.center());
    }
    viewChanged();
}

void ChartView::resetZoom() {
    const QPointF c = visibleCenter();
    setTransform(QTransform());
    centerOn(c);
    viewChanged();
}

void ChartView::resetView() {
    setTransform(QTransform());
    centerOn(0, 0);
    viewChanged();
}

QPointF ChartView::visibleCenter() const { return mapToScene(viewport()->rect().center()); }

void ChartView::setDark(bool dark) {
    m_dark = dark;
    m_bg = dark ? QColor(30, 31, 36) : QColor(248, 249, 251);
    m_gridMinor = dark ? QColor(255, 255, 255, 14) : QColor(15, 23, 42, 16);
    m_gridMajor = dark ? QColor(255, 255, 255, 28) : QColor(15, 23, 42, 32);
    chart()->canvasColor = m_bg;
    m_bgCache = QImage();  // holds the old colors
    viewport()->update();
}

void ChartView::setMinimapVisible(bool on) {
    if (on == m_minimap->visible) return;
    m_minimap->visible = on;
    if (on) m_minimap->rerender();
    else viewport()->update(m_minimap->rect());
}

QJsonObject ChartView::viewState() const {
    const QPointF c = visibleCenter();
    return {{"cx", c.x()}, {"cy", c.y()}, {"zoom", zoom()}};
}

void ChartView::setViewState(const QJsonObject& o) {
    const double z = std::clamp(o["zoom"].toDouble(1), kMinZoom, kMaxZoom);
    setTransform(QTransform::fromScale(z, z));
    centerOn(o["cx"].toDouble(), o["cy"].toDouble());
    viewChanged();
}

// ---------------------------------------------------------------- painting

namespace {
void drawGrid(QPainter* p, const QRectF& r, double step, QColor c, bool skipEveryFifth) {
    QVector<QLineF> lines;
    for (qint64 i = qint64(std::floor(r.left() / step)); i * step <= r.right(); ++i)
        if (!skipEveryFifth || i % 5) lines << QLineF(i * step, r.top(), i * step, r.bottom());
    for (qint64 i = qint64(std::floor(r.top() / step)); i * step <= r.bottom(); ++i)
        if (!skipEveryFifth || i % 5) lines << QLineF(r.left(), i * step, r.right(), i * step);
    p->setPen(QPen(c, 0));
    p->drawLines(lines);
}
}  // namespace

// The grid is kept as an image of the whole viewport, made for one exact view transform (zoom and
// scroll). While nothing but shapes changes (dragging, selecting, editing), paints copy from it.
// Panning paints only thin new strips, drawn directly; the image is remade on the next big paint.
// (Qt's own CacheBackground shifts its image by fractional pixels at 150% scaling, which moved
// grid lines by a pixel, so it is not used.)
void ChartView::drawBackground(QPainter* p, const QRectF& r) {
    const QTransform t = viewportTransform();
    const qreal dpr = viewport()->devicePixelRatioF();
    const QSize px = viewport()->size() * dpr;
    if (chart()->dragLayerOn) {
        if (chart()->mouseGrabberItem() && m_layerTransform == t && m_layer.size() == px && m_layer.devicePixelRatio() == dpr) {
            p->save();
            p->resetTransform();
            p->drawImage(QPointF(0, 0), m_layer);  // grid and every still item
            if (!m_group.isNull()) p->drawImage(m_groupPos + m_groupShift * zoom(), m_group);  // the rigid moving piece
            p->restore();
            return;
        }
        // The view moved or the drag ended without a release: draw normally from here on (the
        // next drag step makes a new layer). This paint already draws everything, so no repaint.
        endDragLayer(false);
    }
    if (m_bgCache.isNull() || m_bgTransform != t || m_bgCache.size() != px || m_bgCache.devicePixelRatio() != dpr) {
        // `r` is only the bounding box of what is repainted; a pan step's two edge strips span the
        // whole view, so judge by the real area (paintEvent notes it).
        // Also draw directly while the view itself is moving (zoom steps): an image made now would
        // be stale by the next paint. Make it once a paint finds the view where the last one did.
        const bool still = t == std::exchange(m_lastPaintTransform, t);
        if (m_paintedShare < 0.25 || !still) return paintGrid(p, r);
        const QRectF all = mapToScene(viewport()->rect()).boundingRect();
        m_bgCache = QImage(px, QImage::Format_RGB32);
        m_bgCache.setDevicePixelRatio(dpr);
        m_bgCache.fill(m_bg);
        QPainter ip(&m_bgCache);
        ip.setRenderHints(renderHints());
        ip.setWorldTransform(t);
        paintGrid(&ip, all);
        m_bgTransform = t;
    }
    p->save();
    p->resetTransform();  // viewport coordinates; the image is pixel-for-pixel the viewport
    p->drawImage(QPointF(0, 0), m_bgCache);
    p->restore();
}

namespace {
// A point that a rigid move shifts with the item: a node's position, a link's first point.
QPointF dragAnchor(ChartItem* c) {
    if (auto* l = qgraphicsitem_cast<LinkItem*>(c)) return l->points().isEmpty() ? QPointF() : l->points().first();
    return c->scenePos();
}
}  // namespace

void ChartView::updateDragLayer() {
    auto* s = chart();
    const QList<ChartItem*> moving = s->dragMovingItems();
    if (moving.isEmpty()) return endDragLayer();
    // Where an item may paint, in scene coords. Moving shapes get room for their selection
    // outline, handles and quick-connect arrows; still shapes just their outline.
    const double u = 1 / std::max(zoom(), 0.5);
    auto area = [u](ChartItem* c, bool live) {
        if (auto* n = qgraphicsitem_cast<NodeItem*>(c)) {
            const double m = live ? 32 * u : n->style.strokeWidth + 2;
            return n->mapRectToScene(n->rect()).adjusted(-m, -m, m, m);
        }
        return c->sceneBoundingRect();
    };
    const QRectF vis = mapToScene(viewport()->rect()).boundingRect();
    bool rebake = !s->dragLayerOn;
    if (rebake) {
        // Worth it only when what moves covers a good part of the view; small drags repaint
        // little anyway, and making the images costs about one full frame.
        QRectF r;
        for (auto* c : moving) r |= area(c, true);
        const QRectF on = r & vis;
        if (on.width() * on.height() < 0.25 * vis.width() * vis.height()) return;
        // A move (not a resize) shifts the shapes and the links between them as one piece: that
        // goes into m_group. Links out to still shapes stretch, so they are painted live.
        auto* grabber = qgraphicsitem_cast<NodeItem*>(s->mouseGrabberItem());
        const bool rigid = grabber && !grabber->isResizing();
        QSet<const NodeItem*> movingNodes;
        for (auto* c : moving)
            if (auto* n = qgraphicsitem_cast<NodeItem*>(c)) movingNodes << n;
        m_liveIds.clear();
        m_groupIds.clear();
        for (auto* c : moving) {
            auto* l = qgraphicsitem_cast<LinkItem*>(c);
            const bool inGroup = rigid && (!l || (movingNodes.contains(l->from) && movingNodes.contains(l->to)));
            (inGroup ? m_groupIds : m_liveIds) << c->id;
        }
    } else if (!m_groupIds.isEmpty()) {
        // Still one rigid piece? (Shapes snap to the grid one by one when several are selected.)
        QRectF now;
        bool first = true;
        for (auto* c : moving) {
            if (!m_groupIds.contains(c->id)) continue;
            const QPointF d = dragAnchor(c) - m_groupFrom.value(c->id);
            if (first) m_groupShift = d, first = false;
            else if (QLineF(d, m_groupShift).length() > 0.01) rebake = true;
            now |= c->sceneBoundingRect();
        }
        if (rebake) {  // not rigid: paint them all live from now on
            m_liveIds += m_groupIds;
            m_groupIds.clear();
        } else if (!m_groupCover.translated(m_groupShift).contains(now & vis)) {
            rebake = true;  // moved past what the image holds
        }
    }
    // Walk the stacking order bottom to top. Draw order is: still items (m_layer), then the group
    // (m_group), then live items. A still item over something live or grouped below it must be
    // live, and so must a group item over something live below it, to stay on top.
    QList<QRectF> drawnBelow, liveBelow;
    for (auto* it : s->items(Qt::AscendingOrder)) {
        if (it->type() != NodeItem::Type && it->type() != LinkItem::Type) continue;
        auto* c = static_cast<ChartItem*>(it);
        bool live = m_liveIds.contains(c->id), group = m_groupIds.contains(c->id);
        const QRectF a = area(c, live || group);
        auto hits = [&a](const QList<QRectF>& rects) {
            return std::any_of(rects.begin(), rects.end(), [&a](const QRectF& b) { return b.intersects(a); });
        };
        if ((group && hits(liveBelow)) || (!live && !group && hits(drawnBelow))) {
            m_groupIds.remove(c->id);
            m_liveIds << c->id;
            live = rebake = true;
            group = false;
        }
        if (live) liveBelow << a;
        if (live || group) drawnBelow << a;
    }
    if (rebake) bakeDragLayer();
}

void ChartView::bakeDragLayer() {
    auto* s = chart();
    s->dragLayerOn = false;  // so baked items paint into the image
    const QTransform t = viewportTransform();
    const QRectF vis = mapToScene(viewport()->rect()).boundingRect();
    const qreal dpr = viewport()->devicePixelRatioF();
    QImage img(viewport()->size() * dpr, QImage::Format_RGB32);
    img.setDevicePixelRatio(dpr);
    img.fill(m_bg);
    QPainter p(&img);
    p.setRenderHints(renderHints());
    p.setWorldTransform(t);
    paintGrid(&p, vis);
    // Paint items the way the view does: same transform, bottom to top.
    QStyleOptionGraphicsItem opt;
    auto paintItem = [&opt, this](QPainter& on, ChartItem* c, const QTransform& base) {
        on.save();
        on.setWorldTransform(c->sceneTransform() * base);
        opt.exposedRect = c->boundingRect();
        c->paint(&on, &opt, viewport());
        on.restore();
    };
    QList<ChartItem*> group;
    QRectF groupArea;
    for (auto* it : s->items(Qt::AscendingOrder)) {
        if (it->type() != NodeItem::Type && it->type() != LinkItem::Type) continue;
        auto* c = static_cast<ChartItem*>(it);
        c->baked = !m_liveIds.contains(c->id);  // still and group items: shown by the images
        if (!c->baked || !c->isVisible()) continue;
        if (m_groupIds.contains(c->id)) group << c, groupArea |= c->sceneBoundingRect();
        else if (c->sceneBoundingRect().intersects(vis)) paintItem(p, c, t);
    }
    p.end();
    m_layer = img;
    m_layerTransform = t;

    // The group, over the view and half a view around it (room to move before a redraw). Its
    // top-left sits on a whole device pixel, so it lines up with the view's own pixels.
    m_group = QImage();
    m_groupFrom.clear();
    m_groupShift = {};
    const QRectF room = vis.adjusted(-vis.width() / 2, -vis.height() / 2, vis.width() / 2, vis.height() / 2);
    m_groupCover = groupArea & room;
    if (!group.isEmpty() && !m_groupCover.isEmpty()) {
        const QRect dev = QRectF(t.mapRect(m_groupCover).topLeft() * dpr, t.mapRect(m_groupCover).size() * dpr).toAlignedRect();
        m_group = QImage(dev.size(), QImage::Format_ARGB32_Premultiplied);
        m_group.setDevicePixelRatio(dpr);
        m_group.fill(Qt::transparent);
        m_groupPos = QPointF(dev.topLeft()) / dpr;
        QPainter gp(&m_group);
        gp.setRenderHints(renderHints());
        const QTransform base = t * QTransform::fromTranslate(-m_groupPos.x(), -m_groupPos.y());
        for (auto* c : std::as_const(group)) {
            paintItem(gp, c, base);
            m_groupFrom[c->id] = dragAnchor(c);
        }
    }
    s->dragLayerOn = true;
    // What is on screen already matches (the same items, drawn the same way), so no repaint.
}

void ChartView::endDragLayer(bool repaint) {
    auto* s = chart();
    if (m_layer.isNull() && !s->dragLayerOn) return;
    const bool wasOn = s->dragLayerOn;
    s->dragLayerOn = false;
    for (auto* it : s->items())
        if (it->type() == NodeItem::Type || it->type() == LinkItem::Type) static_cast<ChartItem*>(it)->baked = false;
    m_layer = QImage();
    m_group = QImage();
    m_liveIds.clear();
    m_groupIds.clear();
    m_groupFrom.clear();
    // Anything a still item changed during the drag (an undo, say) only showed in the old image.
    if (wasOn && repaint) viewport()->update();
}

void ChartView::paintEvent(QPaintEvent* e) {
    qint64 area = 0;
    for (const QRect& r : e->region()) area += qint64(r.width()) * r.height();
    m_paintedShare = double(area) / std::max(1, viewport()->width() * viewport()->height());
    QGraphicsView::paintEvent(e);
}

void ChartView::paintGrid(QPainter* p, const QRectF& r) {
    p->fillRect(r, m_bg);
    // Pick a grid step (10 * 5^n) that keeps minor lines 12-60px apart on screen; fade minor
    // lines in as they spread out so levels blend instead of popping.
    const double z = zoom();
    double step = 10;
    while (step * z < 12) step *= 5;
    while (step * z > 60) step /= 5;
    const double fade = std::clamp((step * z - 12) / 36.0, 0.0, 1.0);
    // Blend the grid colors onto the (solid) background here and draw the lines opaque: Qt draws
    // translucent lines about 10x slower, which made the grid most of the cost of every frame.
    auto over = [this](const QColor& c, double alpha) {
        return QColor::fromRgbF(c.redF() * alpha + m_bg.redF() * (1 - alpha), c.greenF() * alpha + m_bg.greenF() * (1 - alpha),
                                c.blueF() * alpha + m_bg.blueF() * (1 - alpha));
    };
    p->save();
    p->setRenderHint(QPainter::Antialiasing, false);
    if (fade > 0) drawGrid(p, r, step, over(m_gridMinor, m_gridMinor.alphaF() * fade), true);
    drawGrid(p, r, step * 5, over(m_gridMajor, m_gridMajor.alphaF()), false);
    p->restore();
}

void ChartView::drawForeground(QPainter* p, const QRectF&) {
    const auto& guides = chart()->guides;
    if (!guides.isEmpty()) {
        p->setPen(QPen(QColor(236, 72, 153), 0));
        p->drawLines(guides);
    }
    if (m_minimap->visible) {
        p->save();
        p->resetTransform();  // viewport coordinates
        m_minimap->paint(p);
        p->restore();
    }
}

// ---------------------------------------------------------------- input

void ChartView::wheelEvent(QWheelEvent* e) {
    // Wheel zooms around the cursor; Shift+wheel scrolls sideways.
    if (e->modifiers() & Qt::ShiftModifier) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - e->angleDelta().y());
        return e->accept();
    }
    // Use the event's own position, not the live cursor (Qt's AnchorUnderMouse reads QCursor::pos()).
    zoomAt(std::pow(1.0015, e->angleDelta().y()), e->position().toPoint());
    e->accept();
}

void ChartView::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && m_minimap->contains(e->pos())) {
        m_minimapDrag = true;
        m_minimap->jump(e->pos());
        return e->accept();
    }
    if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton) {
        m_panning = true;
        m_lastPan = e->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        return e->accept();
    }
    QGraphicsView::mousePressEvent(e);
}

void ChartView::mouseMoveEvent(QMouseEvent* e) {
    if (m_panning) {
        const QPoint d = e->pos() - m_lastPan;
        m_lastPan = e->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        return e->accept();
    }
    if (m_minimapDrag) {
        m_minimap->jump(e->pos());
        return e->accept();
    }
    // Over the minimap with no button held: it takes the pointer, the canvas under it gets no hover.
    const bool over = e->buttons() == Qt::NoButton && m_minimap->contains(e->pos());
    if (over != m_overMinimap) {
        m_overMinimap = over;
        if (over) viewport()->setCursor(Qt::PointingHandCursor);
        else viewport()->unsetCursor();
    }
    if (over) return e->accept();
    QGraphicsView::mouseMoveEvent(e);
    if (e->buttons() & Qt::LeftButton) updateDragLayer();
}

void ChartView::mouseDoubleClickEvent(QMouseEvent* e) {
    if (m_minimap->contains(e->pos())) return e->accept();  // not a double-click on the shape under it
    QGraphicsView::mouseDoubleClickEvent(e);
}

void ChartView::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && std::exchange(m_minimapDrag, false)) return e->accept();
    if (e->button() == Qt::LeftButton) {
        QGraphicsView::mouseReleaseEvent(e);
        return endDragLayer();
    }
    if ((e->button() == Qt::MiddleButton || e->button() == Qt::RightButton) && m_panning) {
        m_panning = false;
        viewport()->unsetCursor();
        return e->accept();
    }
    QGraphicsView::mouseReleaseEvent(e);
}

void ChartView::keyPressEvent(QKeyEvent* e) {
    if (!chart()->isEditingText()) {
        if (e->key() == Qt::Key_Space) {
            if (!e->isAutoRepeat()) {
                setInteractive(false);
                setDragMode(ScrollHandDrag);
            }
            return e->accept();
        }
        if (e->key() == Qt::Key_Escape) {
            chart()->cancelLinkDrag();
            chart()->clearSelection();
            return e->accept();
        }
        // Ctrl+arrow aligns the selected shapes to that side; Ctrl+Shift+arrow also spaces them evenly.
        const int k = e->key();
        if ((e->modifiers() & Qt::ControlModifier) &&
            (k == Qt::Key_Left || k == Qt::Key_Right || k == Qt::Key_Up || k == Qt::Key_Down)) {
            chart()->alignSelectedShapes(Qt::Key(k), e->modifiers() & Qt::ShiftModifier);
            return e->accept();
        }
        // Typing on a lone selected link starts editing its label; the key goes on into the editor.
        const auto sel = chart()->selectedItems();
        auto* link = sel.size() == 1 ? qgraphicsitem_cast<LinkItem*>(sel.first()) : nullptr;
        if (link && !e->text().isEmpty() && e->text().at(0).isPrint() &&
            !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)))
            link->startEdit();
    }
    QGraphicsView::keyPressEvent(e);
}

void ChartView::keyReleaseEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat() && !isInteractive()) {
        endSpacePan();
        return e->accept();
    }
    QGraphicsView::keyReleaseEvent(e);
}

void ChartView::focusOutEvent(QFocusEvent* e) {
    if (!isInteractive()) endSpacePan();
    QGraphicsView::focusOutEvent(e);
}

void ChartView::endSpacePan() {
    setDragMode(RubberBandDrag);
    setInteractive(true);
}

void ChartView::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasFormat(kShapeMime)) return e->acceptProposedAction();
    QGraphicsView::dragEnterEvent(e);
}

void ChartView::dragMoveEvent(QDragMoveEvent* e) {
    if (e->mimeData()->hasFormat(kShapeMime)) return e->acceptProposedAction();
    QGraphicsView::dragMoveEvent(e);
}

void ChartView::dropEvent(QDropEvent* e) {
    if (!e->mimeData()->hasFormat(kShapeMime)) return QGraphicsView::dropEvent(e);
    const FlowShape s = shapeFromKey(QString::fromUtf8(e->mimeData()->data(kShapeMime)));
    chart()->addNode(s, mapToScene(e->position().toPoint()));
    e->acceptProposedAction();
    setFocus();
}

// Side panels opening/closing resize and move the view. Shift the scroll position by however far
// the viewport moved inside the window, so the chart stays put on screen instead of jumping.
void ChartView::keepContentInPlace() {
    const QPoint pos = viewport()->mapTo(window(), QPoint(0, 0));
    if (m_hasWindowPos) {
        const QPoint d = pos - m_windowPos;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() + d.y());
    }
    m_windowPos = pos;
    m_hasWindowPos = true;
}

void ChartView::moveEvent(QMoveEvent* e) {
    QGraphicsView::moveEvent(e);
    keepContentInPlace();
}

void ChartView::resizeEvent(QResizeEvent* e) {
    QGraphicsView::resizeEvent(e);
    keepContentInPlace();
}

void ChartView::scrollContentsBy(int dx, int dy) {
    QGraphicsView::scrollContentsBy(dx, dy);
    m_minimap->scrolled(dx, dy);
}
