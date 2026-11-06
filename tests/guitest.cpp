// Drives the real canvas with synthetic mouse/keyboard input: quick-connect, drag-connect,
// resize, inline text edit, snapped move, undo, zoom. Run: guitest.exe (exit code 0 = pass).
#include "ChartScene.h"
#include "ChartView.h"
#include <QApplication>
#include <QTest>
#include <QJsonArray>
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

    // Plain wheel zooms around the cursor; the point under it stays put.
    {
        const QPoint m = vp->rect().center() + QPoint(150, 80);
        const QPointF under = view.mapToScene(m);
        QWheelEvent out(m, vp->mapToGlobal(m), {}, {0, -120}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(vp, &out);
        CHECK(view.zoom() < 0.9);
        CHECK(QLineF(view.mapToScene(m), under).length() < 2 / view.zoom());
    }
    // Wheel zoom stops at the limit; fit shows everything.
    const QPoint c = vp->rect().center();
    QWheelEvent wheel(c, vp->mapToGlobal(c), {}, {0, 120 * 40}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
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

    // Reconnect: select a link, drag its target end handle onto another shape.
    {
        ChartScene s3;
        ChartView v3(&s3);
        v3.resize(1200, 800);
        v3.show();
        CHECK(QTest::qWaitForWindowExposed(&v3));
        v3.resetView();
        QWidget* vp3 = v3.viewport();
        auto at3 = [&](QPointF s) { return v3.mapFromScene(s); };
        auto* p = s3.addNode(FlowShape::Process, {0, 0});
        auto* q = s3.addNode(FlowShape::Process, {300, 0});
        auto* r = s3.addNode(FlowShape::Process, {300, 250});
        s3.beginLinkDrag(p, Port::Right);
        s3.endLinkDrag({240, 0});  // near q's left side: straight horizontal link
        LinkItem* l = p->links.value(0);
        CHECK(l && l->to == q && l->toPort == Port::Left);

        s3.clearSelection();
        QTest::mouseClick(vp3, Qt::LeftButton, {}, at3({150, 0}));  // middle of the link
        CHECK(l->isSelected());
        const QPointF end = l->points().last();
        QTest::mousePress(vp3, Qt::LeftButton, {}, at3(end));
        QTest::mouseMove(vp3, at3({300, 200}));
        QTest::mouseMove(vp3, at3({290, 240}));
        QTest::mouseRelease(vp3, Qt::LeftButton, {}, at3({290, 240}));
        CHECK(l->to == r && l->toPort == Port::Top);
        CHECK(q->links.isEmpty() && r->links.size() == 1);
        s3.undoStack()->undo();
        CHECK(l->to == q && r->links.isEmpty());

        // Dropping the end on empty canvas changes nothing.
        const int steps = s3.undoStack()->index();
        QTest::mousePress(vp3, Qt::LeftButton, {}, at3(l->points().last()));
        QTest::mouseMove(vp3, at3({500, 500}));
        QTest::mouseRelease(vp3, Qt::LeftButton, {}, at3({600, 600}));
        CHECK(l->to == q && s3.undoStack()->index() == steps);

        // Dragging the source end works the same way.
        QTest::mousePress(vp3, Qt::LeftButton, {}, at3(l->points().first()));
        QTest::mouseMove(vp3, at3({300, 260}));
        QTest::mouseRelease(vp3, Qt::LeftButton, {}, at3({300, 260}));
        CHECK(l->from == r && l->to == q);
    }

    // Containers: dragging one carries the shapes inside it; undo restores both.
    {
        ChartScene s4;
        ChartView v4(&s4);
        v4.resize(1200, 800);
        v4.show();
        CHECK(QTest::qWaitForWindowExposed(&v4));
        v4.resetView();
        QWidget* vp4 = v4.viewport();
        auto at4 = [&](QPointF s) { return v4.mapFromScene(s); };
        auto* box = s4.addNode(FlowShape::HContainer, {0, 0});  // 640x220, title band x in [-320, -280]
        auto* inner = s4.addNode(FlowShape::Process, {100, 0});
        auto* outside = s4.addNode(FlowShape::Process, {0, 300});
        CHECK(box->zValue() < inner->zValue());  // shapes draw on top of containers
        CHECK(s4.contents(box) == QList<NodeItem*>{inner});
        s4.clearSelection();

        QTest::mousePress(vp4, Qt::LeftButton, {}, at4({-300, 0}));
        CHECK(box->isSelected());
        QTest::mouseMove(vp4, at4({-280, 20}));
        QTest::mouseMove(vp4, at4({-250, 30}));
        QTest::mouseRelease(vp4, Qt::LeftButton, {}, at4({-250, 30}));
        CHECK(box->pos() == QPointF(50, 30));
        CHECK(inner->pos() == QPointF(150, 30));
        CHECK(outside->pos() == QPointF(0, 300));
        s4.undoStack()->undo();
        CHECK(box->pos() == QPointF(0, 0) && inner->pos() == QPointF(100, 0));
        // A click in the container's body does not select it; only its border and title band do.
        s4.clearSelection();
        QTest::mouseClick(vp4, Qt::LeftButton, {}, at4({0, -80}));
        CHECK(!box->isSelected());
        QTest::mouseClick(vp4, Qt::LeftButton, {}, at4({0, -110}));  // top border
        CHECK(box->isSelected());

        // Copying a container copies what is inside it.
        s4.clearSelection();
        box->setSelected(true);
        CHECK(s4.copySelection()["nodes"].toArray().size() == 2);
    }

    // Side panels resizing/moving the view must not move the chart on screen.
    {
        QWidget page;
        auto* left = new QWidget(&page);
        auto* right = new QWidget(&page);
        ChartView v2(&scene, &page);
        auto layout = [&](int lw, int rw) {
            left->setGeometry(0, 0, lw, 600);
            v2.setGeometry(lw, 0, 1000 - lw - rw, 600);
            right->setGeometry(1000 - rw, 0, rw, 600);
        };
        page.resize(1000, 600);
        layout(230, 0);
        page.show();
        CHECK(QTest::qWaitForWindowExposed(&page));
        v2.centerOn(a->pos());
        const QPoint before = v2.viewport()->mapTo(&page, v2.mapFromScene(a->pos()));
        for (int rw : {100, 300}) layout(230, rw);  // right panel slides open
        for (int lw : {150, 72}) layout(lw, 300);   // left panel collapses
        const QPoint after = v2.viewport()->mapTo(&page, v2.mapFromScene(a->pos()));
        CHECK((after - before).manhattanLength() <= 1);
    }

    // Right-button drag pans the canvas.
    {
        const QPointF start = view.mapToScene(vp->rect().center());
        const QPoint c0 = vp->rect().center();
        QTest::mousePress(vp, Qt::RightButton, {}, c0);
        QTest::mouseMove(vp, c0 + QPoint(120, 40));
        QTest::mouseRelease(vp, Qt::RightButton, {}, c0 + QPoint(120, 40));
        const QPointF moved = view.mapToScene(vp->rect().center());
        CHECK(std::abs((start - moved).x() * view.zoom() - 120) < 2);
    }

    std::printf("OK  gui interactions\n");
    return 0;
}
