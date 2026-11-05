#include "ChartView.h"
#include "ChartScene.h"
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include <algorithm>
#include <cmath>

// Overview of the whole chart in the corner. Click or drag to jump there.
class Minimap : public QWidget {
public:
    explicit Minimap(ChartView* view) : QWidget(view), m_view(view) {
        setFixedSize(220, 150);
        setCursor(Qt::PointingHandCursor);
        m_timer.setSingleShot(true);
        m_timer.setInterval(300);  // re-render at most ~3x/s while the chart changes
        QObject::connect(&m_timer, &QTimer::timeout, this, [this] { rerender(); });
    }
    void scheduleRender() {
        if (isVisible()) m_timer.start();
    }
    void rerender() {
        auto* s = m_view->chart();
        QRectF world = s->contentRect();
        if (world.isNull()) world = QRectF(-500, -350, 1000, 700);
        world.adjust(-40, -40, 40, 40);
        // Grow the world rect to the widget's aspect so one scale factor maps both axes.
        const double k = std::min(width() / world.width(), height() / world.height());
        const QSizeF need(width() / k, height() / k);
        m_world = QRectF(world.center() - QPointF(need.width() / 2, need.height() / 2), need);

        const qreal dpr = devicePixelRatioF();
        m_img = QImage(size() * dpr, QImage::Format_ARGB32_Premultiplied);
        m_img.setDevicePixelRatio(dpr);
        m_img.fill(Qt::transparent);
        QPainter p(&m_img);
        p.setRenderHint(QPainter::Antialiasing);
        s->overlaysHidden = true;
        s->render(&p, QRectF(QPointF(), size()), m_world);
        s->overlaysHidden = false;
        update();
    }

protected:
    void showEvent(QShowEvent*) override { rerender(); }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        const bool dark = m_view->isDark();
        p.fillRect(rect(), dark ? QColor(38, 40, 46, 235) : QColor(255, 255, 255, 235));
        p.drawImage(QPointF(0, 0), m_img);
        if (!m_world.isEmpty()) {
            const double k = width() / m_world.width();
            const QRectF vr = m_view->mapToScene(m_view->viewport()->rect()).boundingRect();
            const QRectF mapped((vr.topLeft() - m_world.topLeft()) * k, vr.size() * k);
            p.setPen(QPen(QColor(37, 99, 235), 1.5));
            p.setBrush(QColor(37, 99, 235, 30));
            p.drawRect(mapped);
        }
        p.setPen(dark ? QColor(70, 72, 80) : QColor(203, 213, 225));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    }
    void mousePressEvent(QMouseEvent* e) override { jump(e->position()); }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (e->buttons() & Qt::LeftButton) jump(e->position());
    }

private:
    ChartView* m_view;
    QTimer m_timer;
    QImage m_img;
    QRectF m_world;

    void jump(QPointF pos) {
        if (m_world.isEmpty()) return;
        m_view->centerOn(m_world.topLeft() + pos / (width() / m_world.width()));
        update();
    }
};

// ---------------------------------------------------------------- ChartView

ChartView::ChartView(ChartScene* scene, QWidget* parent) : QGraphicsView(scene, parent) {
    m_minimap = new Minimap(this);  // first: the setters below already scroll, which updates it
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
    viewport()->update();
    m_minimap->update();
}

void ChartView::setMinimapVisible(bool on) { m_minimap->setVisible(on); }

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
    if (c.alpha() == 0) return;
    QVector<QLineF> lines;
    for (qint64 i = qint64(std::floor(r.left() / step)); i * step <= r.right(); ++i)
        if (!skipEveryFifth || i % 5) lines << QLineF(i * step, r.top(), i * step, r.bottom());
    for (qint64 i = qint64(std::floor(r.top() / step)); i * step <= r.bottom(); ++i)
        if (!skipEveryFifth || i % 5) lines << QLineF(r.left(), i * step, r.right(), i * step);
    p->setPen(QPen(c, 0));
    p->drawLines(lines);
}
}  // namespace

void ChartView::drawBackground(QPainter* p, const QRectF& r) {
    p->fillRect(r, m_bg);
    // Pick a grid step (10 * 5^n) that keeps minor lines 12-60px apart on screen; fade minor
    // lines in as they spread out so levels blend instead of popping.
    const double z = zoom();
    double step = 10;
    while (step * z < 12) step *= 5;
    while (step * z > 60) step /= 5;
    const double fade = std::clamp((step * z - 12) / 36.0, 0.0, 1.0);
    QColor minor = m_gridMinor;
    minor.setAlphaF(minor.alphaF() * fade);
    p->save();
    p->setRenderHint(QPainter::Antialiasing, false);
    drawGrid(p, r, step, minor, true);
    drawGrid(p, r, step * 5, m_gridMajor, false);
    p->restore();
}

void ChartView::drawForeground(QPainter* p, const QRectF&) {
    const auto& guides = chart()->guides;
    if (guides.isEmpty()) return;
    p->setPen(QPen(QColor(236, 72, 153), 0));
    p->drawLines(guides);
}

// ---------------------------------------------------------------- input

void ChartView::wheelEvent(QWheelEvent* e) {
    // Wheel zooms around the cursor; Shift+wheel scrolls sideways.
    if (e->modifiers() & Qt::ShiftModifier) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - e->angleDelta().y());
        return e->accept();
    }
    zoomBy(std::pow(1.0015, e->angleDelta().y()));
    e->accept();
}

void ChartView::mousePressEvent(QMouseEvent* e) {
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
    QGraphicsView::mouseMoveEvent(e);
}

void ChartView::mouseReleaseEvent(QMouseEvent* e) {
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
    m_minimap->move(width() - m_minimap->width() - 12, height() - m_minimap->height() - 12);
    m_minimap->raise();
}

void ChartView::scrollContentsBy(int dx, int dy) {
    QGraphicsView::scrollContentsBy(dx, dy);
    m_minimap->update();
}
