#pragma once
#include <QGraphicsView>
#include <QImage>
#include <QJsonObject>
#include <QHash>
#include <QSet>
#include <memory>

class ChartScene;
class Minimap;

class ChartView : public QGraphicsView {
    Q_OBJECT
public:
    explicit ChartView(ChartScene* scene, QWidget* parent = nullptr);
    ~ChartView() override;
    ChartScene* chart() const;

    static constexpr double kMinZoom = 0.02, kMaxZoom = 50.0;
    double zoom() const { return transform().m11(); }
    void zoomBy(double factor, bool atMouse = true);
    void zoomAt(double factor, const QPoint& viewportPos);  // keeps the scene point at viewportPos fixed
    void fitAll();     // Ctrl+R
    void resetZoom();  // Ctrl+0
    void resetView();  // 100% centered on origin (new chart)
    QPointF visibleCenter() const;

    void setDark(bool dark);
    bool isDark() const { return m_dark; }
    void setMinimapVisible(bool on);
    QJsonObject viewState() const;
    void setViewState(const QJsonObject& o);

signals:
    void zoomChanged(double zoom);

protected:
    void paintEvent(QPaintEvent* e) override;
    void drawBackground(QPainter* p, const QRectF& r) override;
    void drawForeground(QPainter* p, const QRectF& r) override;
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void moveEvent(QMoveEvent* e) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    std::unique_ptr<Minimap> m_minimap;
    bool m_minimapDrag = false;  // left button went down on the minimap
    bool m_overMinimap = false;  // cursor is over the minimap (it shows a pointing hand there)
    bool m_dark = false;
    bool m_panning = false;
    QPoint m_lastPan;
    QPoint m_windowPos;  // viewport top-left in window coords, to keep content still when side panels slide
    bool m_hasWindowPos = false;
    QColor m_bg, m_gridMinor, m_gridMajor;
    QImage m_bgCache;          // background + grid for the whole viewport, see drawBackground
    QTransform m_bgTransform;  // the view transform m_bgCache was drawn for
    QTransform m_lastPaintTransform;  // view transform of the last paint that drew the grid directly
    double m_paintedShare = 1;  // share of the viewport the current paint covers
    void paintGrid(QPainter* p, const QRectF& r);

    // Drag layers: while big things are dragged, everything that stays still is drawn once into
    // m_layer, and what moves as one rigid piece into m_group, which each frame just shifts.
    // Only the rest is painted live. See updateDragLayer.
    QImage m_layer;
    QTransform m_layerTransform;  // view transform both images were drawn for
    QSet<int> m_liveIds;          // items painted live
    QSet<int> m_groupIds;         // items in m_group
    QImage m_group;
    QPointF m_groupPos;           // m_group's top-left in viewport coords when drawn
    QRectF m_groupCover;          // scene area m_group holds
    QPointF m_groupShift;         // how far the group has moved since (scene units)
    QHash<int, QPointF> m_groupFrom;  // where each group item was when m_group was drawn
    void updateDragLayer();
    void bakeDragLayer();
    void endDragLayer(bool repaint = true);

    void endSpacePan();
    void viewChanged();
    void keepContentInPlace();
};
