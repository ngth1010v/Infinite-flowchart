// Drives the real canvas with synthetic mouse/keyboard input: quick-connect, drag-connect,
// resize, inline text edit, snapped move, undo, zoom. Run: guitest.exe (exit code 0 = pass).
#include "ChartScene.h"
#include "ChartView.h"
#include "MainWindow.h"
#include "Panels.h"
#include <QLineEdit>
#include <QToolButton>
#include <QUndoStack>
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
    const int aId = a->id;
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

    // Right edge handle: only the width changes; the left edge stays.
    QTest::mouseMove(vp, at({70, 0}));
    QTest::mousePress(vp, Qt::LeftButton, {}, at({70, 0}));
    QTest::mouseMove(vp, at({90, 20}));
    QTest::mouseMove(vp, at({130, 40}));
    QTest::mouseRelease(vp, Qt::LeftButton, {}, at({130, 40}));
    CHECK(std::abs(a->size.width() - 200) < 0.01 && std::abs(a->size.height() - 70) < 0.01);
    CHECK(std::abs(a->mapRectToScene(a->rect()).left() + 70) < 0.01);
    scene.undoStack()->undo();
    CHECK(std::abs(a->size.width() - 140) < 0.01);

    // Shift + corner drag: square, side = the longer drag (180 x 95 becomes 180 x 180).
    QTest::mouseMove(vp, at({70, 35}));
    QTest::mousePress(vp, Qt::LeftButton, {}, at({70, 35}));
    QTest::mouseMove(vp, at({90, 45}));
    QMouseEvent mv(QEvent::MouseMove, at({110, 64}), vp->mapToGlobal(at({110, 64})), Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
    QApplication::sendEvent(vp, &mv);
    QTest::mouseRelease(vp, Qt::LeftButton, Qt::ShiftModifier, at({110, 64}));
    CHECK(std::abs(a->size.width() - 180) < 0.01 && std::abs(a->size.height() - 180) < 0.01);
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
    a = scene.nodeById(aId);  // undo re-created the shape; the old pointer is gone

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

        // Sliding a segment: grab the handle in the middle of the selected link and drag it down.
        s3.undoStack()->undo();  // back to p -> q, straight
        CHECK(l->from == p && l->points().size() == 2);
        s3.clearSelection();
        l->setSelected(true);
        const QPointF handle = (l->points().first() + l->points().last()) / 2;
        QTest::mousePress(vp3, Qt::LeftButton, {}, at3(handle));
        QTest::mouseMove(vp3, at3(handle + QPointF(0, 60)));
        QTest::mouseMove(vp3, at3(handle + QPointF(0, 120)));
        QTest::mouseRelease(vp3, Qt::LeftButton, {}, at3(handle + QPointF(0, 120)));
        bool lowered = false;
        for (int i = 1; i < l->points().size(); ++i)
            if (std::abs(l->points()[i - 1].y() - 120) < 0.01 && std::abs(l->points()[i].y() - 120) < 0.01) lowered = true;
        CHECK(lowered && !l->path.isEmpty() && l->from == p && l->to == q);
        s3.undoStack()->undo();
        CHECK(l->path.isEmpty() && l->points().size() == 2);

        // Typing on a selected link edits its label; the label sits beside the line, not on it.
        s3.clearSelection();
        l->setSelected(true);
        vp3->setFocus();
        QTest::keyClicks(vp3, "Yes");
        CHECK(s3.isEditingText());
        QTest::keyClick(vp3, Qt::Key_Return);
        QApplication::processEvents();
        CHECK(l->text == "Yes" && !s3.isEditingText());
        CHECK(l->style.fill.alpha() == 0);
        const double lineY = l->points().first().y();
        CHECK(l->shape().contains({150, lineY - 8}));  // label above the horizontal line's middle
        s3.undoStack()->undo();
        CHECK(l->text.isEmpty());

        // 3-line link: dragging the middle line sticks where it is dropped (it used to snap back).
        s3.beginLinkDrag(p, Port::Right);
        s3.endLinkDrag(r->portScenePos(Port::Left) + QPointF(5, 0));
        LinkItem* z = p->links.last();
        CHECK(z->to == r && z->points().size() == 4);
        s3.clearSelection();
        z->setSelected(true);
        const QPointF zh = (z->points()[1] + z->points()[2]) / 2;  // middle line's handle
        QTest::mousePress(vp3, Qt::LeftButton, {}, at3(zh));
        QTest::mouseMove(vp3, at3(zh + QPointF(-20, 0)));
        QTest::mouseMove(vp3, at3(zh + QPointF(-40, 0)));
        QTest::mouseRelease(vp3, Qt::LeftButton, {}, at3(zh + QPointF(-40, 0)));
        CHECK(!z->path.isEmpty() && std::abs(z->points()[1].x() - (zh.x() - 40)) < 0.01);
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

        // A link leaving the container's right side lies over that side's edge handle (links draw
        // above containers); the handle still wins, so the drag resizes the container.
        auto* far = s4.addNode(FlowShape::Process, {600, 0});
        s4.beginLinkDrag(box, Port::Right);
        s4.endLinkDrag(far->portScenePos(Port::Left));
        CHECK(box->links.size() == 1);
        s4.clearSelection();
        box->setSelected(true);
        const double w0 = box->size.width();
        QTest::mouseMove(vp4, at4({324, 2}));
        QTest::mousePress(vp4, Qt::LeftButton, {}, at4({324, 2}));
        QTest::mouseMove(vp4, at4({350, 2}));
        QTest::mouseMove(vp4, at4({380, 2}));
        QTest::mouseRelease(vp4, Qt::LeftButton, {}, at4({380, 2}));
        CHECK(std::abs(box->size.width() - (w0 + 60)) < 0.01);
        s4.undoStack()->undo();
        CHECK(std::abs(box->size.width() - w0) < 0.01);

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

    // F1 opens the shortcut list inside the window; the window can still close while it is open.
    {
        MainWindow w;
        w.show();
        CHECK(QTest::qWaitForWindowExposed(&w));
        QTest::keyClick(&w, Qt::Key_F1);
        QWidget* panel = nullptr;
        for (auto* c : w.findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
            if (c->isVisible() && c->geometry() == w.rect()) panel = c;
        CHECK(panel);
        CHECK(w.close());
    }

    // Find & replace: matches in reading order; Replace does the current one and moves on; All is one undo step.
    {
        ChartScene s;
        ChartView v(&s);
        auto* n1 = s.addNode(FlowShape::Process, {0, 0});
        auto* n2 = s.addNode(FlowShape::Process, {0, 200});
        auto* n3 = s.addNode(FlowShape::Process, {0, 400});
        s.setItemText(n1, "Load data");
        s.setItemText(n2, "other");
        s.setItemText(n3, "Save DATA, data");
        FindPanel f(&v, nullptr);
        f.open();
        auto edits = f.findChildren<QLineEdit*>();
        edits[0]->setText("data");
        CHECK(s.findMatches() == (QList<ChartItem*>{n1, n3}) && s.findCurrent == n1->id);
        edits[1]->setText("rows");
        QTest::keyClick(edits[1], Qt::Key_Return);  // replace in n1, go to n3
        CHECK(n1->text == "Load rows" && s.findCurrent == n3->id);
        const int before = s.undoStack()->index();
        CHECK(s.replaceText(s.findMatches(), "x") == 1 && n3->text == "Save x, x");
        CHECK(s.undoStack()->index() == before + 1);
        CHECK(s.replaceText(s.findMatches(), "x") == 0 && s.undoStack()->index() == before + 1);
        // Match case button; the current match is centered in the view.
        v.resize(800, 600);
        v.show();
        CHECK(QTest::qWaitForWindowExposed(&v));
        s.findCurrent = 0;
        edits[0]->setText("load");
        CHECK(s.findMatches().size() == 1);
        for (auto* b : f.findChildren<QToolButton*>())
            if (b->text() == "Aa") b->click();
        CHECK(s.findCase == Qt::CaseSensitive && s.findMatches().isEmpty());
        edits[0]->setText("Load");
        CHECK(s.findCurrent == n1->id);
        const QPointF mid = v.mapToScene(v.viewport()->rect().center());
        CHECK(std::abs(mid.x() - n1->pos().x()) < 2 && std::abs(mid.y() - n1->pos().y()) < 2);
        f.close();
        CHECK(s.findText.isEmpty());
    }

    std::printf("OK  gui interactions\n");
    return 0;
}
