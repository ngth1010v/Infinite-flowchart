// Drives the real canvas with synthetic mouse/keyboard input: quick-connect, drag-connect,
// resize, inline text edit, snapped move, undo, zoom. Run: guitest.exe (exit code 0 = pass).
#include "ChartScene.h"
#include "ChartView.h"
#include <QApplication>
#include <QTest>
#include <QWheelEvent>
#include <cmath>
#include <cstdio>

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            return 1;                                                            \
        }                                                                        \
    } while (0)

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    ChartScene scene;
    ChartView view(&scene);
    view.resize(1200, 800);
    view.show();
    CHECK(QTest::qWaitForWindowExposed(&view));
    view.resetView();
    QWidget* vp = view.viewport();
    auto at = [&](QPointF s) { return view.mapFromScene(s); };

    auto* a = scene.addNode(FlowShape::Process, {0, 0});  // 140x70, right port at (70, 0)
    scene.clearSelection();

    // Hover, then click the right quick-connect arrow: adds a connected copy to the right.
    QTest::mouseMove(vp, at({0, 0}));
    QTest::mouseMove(vp, at({88, 0}));
    QTest::mouseClick(vp, Qt::LeftButton, {}, at({88, 0}));
    CHECK(scene.itemCount() == 3);
    CHECK(a->links.size() == 1);
    NodeItem* b = a->links[0]->to;
    CHECK(std::abs(b->pos().x() - 200) < 0.01 && std::abs(b->pos().y()) < 0.01);

    // Drag from A's bottom arrow onto B: connects existing nodes.
    QTest::mouseMove(vp, at({0, 0}));
    QTest::mouseMove(vp, at({0, 53}));
    QTest::mousePress(vp, Qt::LeftButton, {}, at({0, 53}));
    QTest::mouseMove(vp, at({100, 60}));
    QTest::mouseMove(vp, at({200, 10}));
    QTest::mouseRelease(vp, Qt::LeftButton, {}, at({200, 10}));
    CHECK(scene.itemCount() == 4 && a->links.size() == 2 && b->links.size() == 2);

    // Select A, drag its bottom-right handle: resize with the top-left corner fixed and snapping.
    QTest::mouseClick(vp, Qt::LeftButton, {}, at({-30, 0}));
    CHECK(a->isSelected() && scene.selectedItems().size() == 1);
    QTest::mouseMove(vp, at({70, 35}));
    QTest::mousePress(vp, Qt::LeftButton, {}, at({70, 35}));
    QTest::mouseMove(vp, at({90, 45}));
    QTest::mouseMove(vp, at({110, 64}));
    QTest::mouseRelease(vp, Qt::LeftButton, {}, at({110, 64}));
    CHECK(std::abs(a->size.width() - 180) < 0.01);
    CHECK(std::abs(a->mapRectToScene(a->rect()).left() + 70) < 0.01);

    // Undo the resize.
    scene.undoStack()->undo();
    CHECK(std::abs(a->size.width() - 140) < 0.01);

    // Double-click edits text inline; Enter commits.
    QTest::mouseDClick(vp, Qt::LeftButton, {}, at({-30, 0}));
    CHECK(scene.isEditingText());
    QTest::keyClicks(vp, "Hello");
    QTest::keyClick(vp, Qt::Key_Return);
    QApplication::processEvents();
    CHECK(a->text == "Hello");
    CHECK(!scene.isEditingText());

    // Drag the node body: lands on the 10-unit grid.
    QTest::mousePress(vp, Qt::LeftButton, {}, at({-30, 0}));
    QTest::mouseMove(vp, at({-20, 10}));
    QTest::mouseMove(vp, at({3, 107}));
    QTest::mouseRelease(vp, Qt::LeftButton, {}, at({3, 107}));
    CHECK(a->pos().y() > 50);
    CHECK(std::fmod(std::abs(a->pos().x()), 10) < 0.01 && std::fmod(std::abs(a->pos().y()), 10) < 0.01);
    scene.undoStack()->undo();
    CHECK(a->pos() == QPointF(0, 0));

    // Ctrl+wheel zooms within limits; fit shows everything.
    const QPoint c = vp->rect().center();
    QWheelEvent wheel(c, vp->mapToGlobal(c), {}, {0, 120 * 40}, Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(vp, &wheel);
    CHECK(std::abs(view.zoom() - ChartView::kMaxZoom) < 1e-6);
    view.fitAll();
    CHECK(view.zoom() <= 1.0);
    const QRectF visible = view.mapToScene(vp->rect()).boundingRect();
    CHECK(visible.contains(scene.contentRect()));

    // Delete via scene and undo restores links.
    scene.clearSelection();
    a->setSelected(true);
    scene.deleteSelected();
    CHECK(scene.itemCount() == 1);
    scene.undoStack()->undo();
    CHECK(scene.itemCount() == 4);

    std::printf("OK  gui interactions\n");
    return 0;
}
