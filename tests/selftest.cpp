// Headless checks for the chart model: shapes, routing, save/load, undo, paste, and a
// 5000-node load/render timing. Run: selftest.exe (exit code 0 = pass).
#include "ChartScene.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <cmath>
#include <cstdio>

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            return 1;                                                            \
        }                                                                        \
    } while (0)

static bool orthogonal(const QPolygonF& pts) {
    for (int i = 0; i + 1 < pts.size(); ++i)
        if (std::abs(pts[i].x() - pts[i + 1].x()) > 1e-6 && std::abs(pts[i].y() - pts[i + 1].y()) > 1e-6) return false;
    return true;
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    // Ctrl+F: a container wraps its contents plus padding and the title band; undo, save/load.
    {
        ChartScene sc;
        auto* box = sc.addNode(FlowShape::VContainer, {0, 0});
        box->size = {600, 400};
        box->refresh();
        auto* a = sc.addNode(FlowShape::Process, {-50, 0});
        auto* b = sc.addNode(FlowShape::Process, {100, 80});
        const QRectF inner = a->mapRectToScene(a->rect()) | b->mapRectToScene(b->rect());
        CHECK(box->padding > 0);
        box->padding = 15;
        sc.clearSelection();
        box->setSelected(true);
        QApplication::processEvents();
        const QRectF oldArea = box->mapRectToScene(box->rect());
        QRectF repainted;
        QObject::connect(&sc, &QGraphicsScene::changed, [&](const QList<QRectF>& rs) {
            for (const QRectF& r : rs) repainted |= r;
        });
        sc.fitSelectedContainers();
        QApplication::processEvents();
        CHECK(repainted.contains(oldArea));  // the old border is repainted, not left behind
        const QRectF want = inner.adjusted(-15, -15 - kContainerHeader, 15, 15);
        CHECK(QLineF(box->mapRectToScene(box->rect()).topLeft(), want.topLeft()).length() < 1e-6);
        CHECK(QLineF(box->mapRectToScene(box->rect()).bottomRight(), want.bottomRight()).length() < 1e-6);
        CHECK(a->pos() == QPointF(-50, 0) && b->pos() == QPointF(100, 80));  // contents stay put
        CHECK(box->toJson()["padding"].toDouble() == 15);
        sc.undoStack()->undo();
        CHECK(box->size == QSizeF(600, 400));
        CHECK(box->toJson()["padding"].toDouble() == 15);  // padding survives undo's reload
    }

    // Ctrl+M: fan-out links from one port get one shared horizontal run; undo restores them.
    {
        ChartScene sc;
        auto* src = sc.addNode(FlowShape::Process, {0, 0});
        auto* l = sc.addNode(FlowShape::Process, {-200, 200});
        auto* r = sc.addNode(FlowShape::Process, {200, 300});
        sc.beginLinkDrag(src, Port::Bottom);
        sc.endLinkDrag(l->portScenePos(Port::Top));
        sc.beginLinkDrag(src, Port::Bottom);
        sc.endLinkDrag(r->portScenePos(Port::Top));
        LinkItem *a = src->links[0], *b = src->links[1];
        const QPolygonF oldA = a->points(), oldB = b->points();
        CHECK(oldA.size() == 4 && oldB.size() == 4 && oldA[1].y() != oldB[1].y());
        sc.clearSelection();
        CHECK(!sc.bundleSelectedLinks());  // nothing selected: minimap toggles instead
        a->setSelected(true);
        CHECK(!sc.bundleSelectedLinks());  // one link only
        b->setSelected(true);
        r->setSelected(true);
        CHECK(!sc.bundleSelectedLinks());  // a shape in the selection
        r->setSelected(false);
        CHECK(sc.bundleSelectedLinks());
        CHECK(orthogonal(a->points()) && orthogonal(b->points()));
        CHECK(a->points()[1].y() == b->points()[1].y());  // shared run
        CHECK(a->points()[1].y() == std::min(oldA[1].y(), oldB[1].y()));  // only one link moved
        CHECK(a->points() == oldA);
        sc.undoStack()->undo();
        CHECK(a->points() == oldA && b->points() == oldB);
    }

    // Random segment drags and shape moves never leave a slanted segment.
    srand(7);
    auto rnd = [](double a, double b) { return a + (b - a) * (rand() / double(RAND_MAX)); };
    for (int iter = 0; iter < 500; ++iter) {
        ChartScene fz;
        auto* n1 = fz.addNode(FlowShape::Process, {0, 0});
        auto* n2 = fz.addNode(FlowShape::Process, {rnd(-300, 300), rnd(-300, 300)});
        n1->size = QSizeF(rnd(40, 200), rnd(30, 150));
        n2->size = QSizeF(rnd(40, 200), rnd(30, 150));
        n1->refresh(), n2->refresh();
        fz.beginLinkDrag(n1, Port(rand() % 4));
        fz.endLinkDrag(n2->portScenePos(Port(rand() % 4)));
        if (n1->links.isEmpty()) continue;
        LinkItem* l = n1->links[0];
        for (int step = 0; step < 8; ++step) {
            if (rand() % 3) {
                const int sg = rand() % int(l->points().size() - 1);
                const QPointF pos(rnd(-400, 400), rnd(-400, 400));
                const QPolygonF pts = l->dragSegment(l->points(), sg, rand() % 2 ? fz.snap(pos) : pos);
                CHECK(orthogonal(pts));
                fz.setLinkPath(l, l->pathFromPoints(pts));
            } else {
                (rand() % 2 ? n1 : n2)->moveBy(rnd(-100, 100), rnd(-100, 100));
                fz.settleLinkPaths();
            }
            CHECK(orthogonal(l->points()));
        }
    }

    // Facing ends a little off level slide along their sides, so the link runs straight.
    {
        ChartScene sc;
        auto* p = sc.addNode(FlowShape::Process, {0, 0});
        auto* q = sc.addNode(FlowShape::Process, {300, 0});
        q->moveBy(0, 6);
        sc.beginLinkDrag(p, Port::Right);
        sc.endLinkDrag(q->portScenePos(Port::Left));
        const QPolygonF pts = p->links.value(0)->points();
        CHECK(pts.size() == 2 && std::abs(pts[0].y() - 3) < 1e-6 && std::abs(pts[1].y() - 3) < 1e-6);
        q->moveBy(0, 30);  // too far off: back to a jog from the side centers
        CHECK(p->links[0]->points().size() == 4 && std::abs(p->links[0]->points()[0].y()) < 1e-6);
    }

    // Every shape has an outline, round-trips its key, and has ports on its edge.
    const QRectF r(-70, -40, 140, 80);
    for (int i = 0; i < int(FlowShape::Count); ++i) {
        const FlowShape s = FlowShape(i);
        CHECK(!shapeOutline(s, r).isEmpty());
        CHECK(shapeFromKey(shapeKey(s)) == s);
    }
    const QPointF top = shapePort(FlowShape::Decision, QRectF(-50, -50, 100, 100), Port::Top);
    CHECK(std::abs(top.x()) < 0.1 && top.y() > -50.1 && top.y() < -46);  // tip, a little in for the rounded corner
    const QPointF left = shapePort(FlowShape::InputOutput, QRectF(-100, -50, 200, 100), Port::Left);
    CHECK(left.x() > -95);  // slanted side: port sits on the slant, not the bounding box

    // Routing is always orthogonal, including awkward back-facing cases.
    const Port ports[] = {Port::Top, Port::Right, Port::Bottom, Port::Left};
    for (Port pa : ports)
        for (Port pb : ports)
            for (QPointF b : {QPointF(200, 120), QPointF(-200, -120), QPointF(0, 300), QPointF(10, -5)})
                CHECK(orthogonal(LinkItem::route({0, 0}, pa, b, pb)));

    {   // Editing a link's path: slide a segment sideways; the path stays as the ends move.
        ChartScene sc;
        auto* top = sc.addNode(FlowShape::Process, {0, 0});
        auto* mid = sc.addNode(FlowShape::Process, {300, 200});
        auto* bot = sc.addNode(FlowShape::Process, {0, 400});
        sc.beginLinkDrag(top, Port::Bottom);
        sc.endLinkDrag(bot->portScenePos(Port::Top) + QPointF(0, 5));
        LinkItem* l = top->links.value(0);
        CHECK(l && l->points().size() == 2);  // straight down
        QPolygonF pts = l->dragSegment(l->points(), 0, {-120, 200});
        CHECK(orthogonal(pts));
        sc.setLinkPath(l, l->pathFromPoints(pts));
        auto hasVertical = [](const QPolygonF& q, double x) {
            for (int i = 1; i < q.size(); ++i)
                if (std::abs(q[i - 1].x() - x) < 0.01 && std::abs(q[i].x() - x) < 0.01) return true;
            return false;
        };
        CHECK(orthogonal(l->points()) && hasVertical(l->points(), -120));
        bot->setPos(60, 420);  // one end moves: still orthogonal, user's segment stays put
        CHECK(orthogonal(l->points()) && hasVertical(l->points(), -120));
        top->setPos(top->pos() + QPointF(10, 10));
        bot->setPos(bot->pos() + QPointF(-40, -10));  // ends moved by different amounts: path stays
        CHECK(hasVertical(l->points(), -120));
        sc.settleLinkPaths();  // gesture over; now both ends move together: the path moves along
        top->setPos(top->pos() + QPointF(100, 0));
        bot->setPos(bot->pos() + QPointF(100, 0));
        CHECK(orthogonal(l->points()) && hasVertical(l->points(), -20));
        const QJsonObject saved = sc.toJson();  // path survives save/load
        ChartScene sc2;
        sc2.fromJson(saved);
        CHECK(sc2.toJson() == saved);
        sc.undoStack()->undo();  // back to the automatic route
        CHECK(l->path.isEmpty());

        // A 3-line link (Z shape): sliding its middle line must stick, not snap back.
        auto* zs = sc.addNode(FlowShape::Process, {600, 0});
        auto* zt = sc.addNode(FlowShape::Process, {1000, 200});
        sc.beginLinkDrag(zs, Port::Right);
        sc.endLinkDrag(zt->portScenePos(Port::Left) + QPointF(5, 0));
        LinkItem* z = zs->links.value(0);
        CHECK(z && z->points().size() == 4);  // start, bend, bend, end
        const QPolygonF zpts = z->dragSegment(z->points(), 1, {900, 100});
        sc.setLinkPath(z, z->pathFromPoints(zpts));
        CHECK(!z->path.isEmpty() && orthogonal(z->points()) && hasVertical(z->points(), 900));

        // Typing a long text grows the node; its top edge stays put.
        const double topEdge = mid->pos().y() - mid->size.height() / 2;
        sc.setItemText(mid, QString("word ").repeated(60));
        CHECK(mid->size.height() > 70 && std::abs(mid->pos().y() - mid->size.height() / 2 - topEdge) < 0.01);
        CHECK(mid->size.width() > 300 && mid->size.width() < 345);  // widened up to the cap, then taller
        sc.undoStack()->undo();
        CHECK(std::abs(mid->size.height() - 70) < 0.01 && std::abs(mid->size.width() - 140) < 0.01);
        // One long word with no spaces cannot wrap, so the node widens to fit it.
        sc.setItemText(mid, QString("x").repeated(80));
        CHECK(mid->size.width() > QFontMetricsF(QFont()).horizontalAdvance(QString("x").repeated(40)));
        sc.undoStack()->undo();
    }

    {   // Long text grows every shape so its text area stays inside the outline (round ends, cut corners).
        ChartScene sc;
        for (int k = 0; k < int(FlowShape::HContainer); ++k) {
            auto* n = sc.addNode(FlowShape(k), {0, 0});
            sc.setItemText(n, QString("word ").repeated(40));
            const QRectF tr = n->textRect().adjusted(8, 4, -8, -4);
            const QPainterPath out = shapeOutline(n->shapeType, n->rect());
            CHECK(out.contains(tr.topLeft()) && out.contains(tr.topRight()) && out.contains(tr.bottomLeft()) &&
                  out.contains(tr.bottomRight()));
        }
    }

    // Build: two nodes, connect by dragging from A's bottom arrow onto B.
    ChartScene s;
    auto* a = s.addNode(FlowShape::Process, {0, 0});
    auto* b = s.addNode(FlowShape::Decision, {0, 200});
    s.beginLinkDrag(a, Port::Bottom);
    s.endLinkDrag({0, 200});
    CHECK(s.itemCount() == 3 && a->links.size() == 1 && b->links.size() == 1);
    CHECK(a->links[0]->toPort == Port::Top);  // dropped on B's center anchor: side facing A
    {   // Center anchor picks the side facing the source, even where the drop point alone would not.
        ChartScene sc;
        auto* p = sc.addNode(FlowShape::Process, {0, 0});
        auto* q = sc.addNode(FlowShape::Decision, {400, 50});
        sc.beginLinkDrag(p, Port::Right);
        sc.endLinkDrag(q->pos());
        CHECK(q->links.size() == 1 && q->links[0]->toPort == Port::Left && q->links[0]->toAuto);
        q->setPos(0, 300);  // moved below the source: auto end switches to the side facing it
        CHECK(q->links[0]->toPort == Port::Top);
        CHECK(q->links[0]->toJson()["toPort"] == "auto");
        q->setPos(400, 50);
        sc.beginLinkDrag(p, Port::Right);
        sc.endLinkDrag(q->pos() + QPointF(0, -40));  // off-center, near the top: top side as before
        CHECK(q->links.size() == 2 && q->links[1]->toPort == Port::Top);
        // Side anchors reach a little past the outline: a drop just outside the left corner connects.
        const auto d = sc.linkDropAt(q->portScenePos(Port::Left) - QPointF(8, 0), p->pos(), p);
        CHECK(d.shown == q && d.node == q && d.anchor == int(Port::Left) && d.port == Port::Left);
        CHECK(sc.linkDropAt({400, 300}, p->pos(), p).shown == nullptr);  // nothing in reach
    }
    // Clicking an arrow (no drag) adds a connected copy of the shape.
    s.beginLinkDrag(b, Port::Right);
    s.endLinkDrag(b->portScenePos(Port::Right));
    CHECK(s.itemCount() == 5);
    s.undoStack()->undo();
    CHECK(s.itemCount() == 3);

    // Save/load round trip is exact.
    const QJsonObject saved = s.toJson();
    ChartScene s2;
    s2.fromJson(saved);
    CHECK(s2.toJson() == saved);

    // Delete a node takes its link along; undo brings both back exactly.
    s.clearSelection();
    a->setSelected(true);
    s.deleteSelected();
    CHECK(s.itemCount() == 1);
    s.undoStack()->undo();
    CHECK(s.itemCount() == 3 && s.toJson() == saved);
    s.undoStack()->redo();
    CHECK(s.itemCount() == 1);
    s.undoStack()->undo();

    // Style edits merge into one undo step and undo cleanly.
    s.selectAll();
    s.editSelected("fontSize", [](ChartItem* c) { c->style.fontSize = 20; });
    s.editSelected("fontSize", [](ChartItem* c) { c->style.fontSize = 22; });
    s.undoStack()->undo();
    CHECK(s.toJson() == saved);

    // Paste remaps ids and keeps the link between copied nodes.
    s.selectAll();
    s.paste(s.copySelection(), {20, 20});
    CHECK(s.itemCount() == 6);
    s.undoStack()->undo();
    CHECK(s.toJson() == saved);

    // Export writes non-empty PNG, SVG and PDF files.
    QTemporaryDir tmp;
    for (const char* ext : {"png", "svg", "pdf"}) {
        const QString path = tmp.filePath(QString("chart.") + ext);
        CHECK(s.exportTo(path));
        CHECK(QFileInfo(path).size() > 500);
    }
    CHECK(!ChartScene().exportTo(tmp.filePath("empty.png")));

    // Malformed input is skipped, not trusted.
    ChartScene bad;
    bad.fromJson(QJsonDocument::fromJson(R"({"nodes":[{"id":0},{"id":1,"w":-5},{"id":1}],
        "links":[{"id":2,"from":1,"to":99}]})").object());
    CHECK(bad.itemCount() == 1 && bad.nodeById(1)->size.width() >= 20);

    // Stress: 5000 nodes + 4900 links.
    QJsonArray nodes, links;
    for (int i = 0; i < 5000; ++i)
        nodes << QJsonObject{{"id", i + 1}, {"shape", shapeKey(FlowShape(i % int(FlowShape::Count)))},
                             {"x", (i % 100) * 220}, {"y", (i / 100) * 160}, {"w", 140}, {"h", 70},
                             {"text", QString("Step %1").arg(i)}};
    for (int i = 0; i < 4900; ++i)
        links << QJsonObject{{"id", 10000 + i}, {"from", i + 1}, {"fromPort", "bottom"}, {"to", i + 101}, {"toPort", "top"}};
    ChartScene big;
    QElapsedTimer t;
    t.start();
    big.fromJson({{"nodes", nodes}, {"links", links}});
    const qint64 loadMs = t.elapsed();
    CHECK(big.itemCount() == 9900);

    QImage img(1920, 1080, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    t.restart();
    big.render(&p, img.rect(), big.contentRect(), Qt::KeepAspectRatio);  // whole chart, zoomed out
    const qint64 fitMs = t.elapsed();
    big.render(&p, img.rect(), QRectF(0, 0, 1920, 1080));  // warm-up: first text draw loads fonts
    t.restart();
    big.render(&p, img.rect(), QRectF(0, 0, 1920, 1080));  // one screen at 100%
    const qint64 screenMs = t.elapsed();
    p.end();

    std::printf("OK  load 5000 nodes: %lld ms | render all: %lld ms | render one screen: %lld ms\n",
                loadMs, fitMs, screenMs);
    return 0;
}
