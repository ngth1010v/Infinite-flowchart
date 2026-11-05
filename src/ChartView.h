#pragma once
#include <QGraphicsView>
#include <QJsonObject>

class ChartScene;
class Minimap;

class ChartView : public QGraphicsView {
    Q_OBJECT
public:
    explicit ChartView(ChartScene* scene, QWidget* parent = nullptr);
    ChartScene* chart() const;

    static constexpr double kMinZoom = 0.02, kMaxZoom = 50.0;
    double zoom() const { return transform().m11(); }
    void zoomBy(double factor, bool atMouse = true);
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
    void drawBackground(QPainter* p, const QRectF& r) override;
    void drawForeground(QPainter* p, const QRectF& r) override;
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    Minimap* m_minimap;
    bool m_dark = false;
    bool m_panning = false;
    QPoint m_lastPan;
    QColor m_bg, m_gridMinor, m_gridMajor;

    void endSpacePan();
    void viewChanged();
};
