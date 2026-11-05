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

    // Every shape has an outline, round-trips its key, and has ports on its edge.
    const QRectF r(-70, -40, 140, 80);
    for (int i = 0; i < int(FlowShape::Count); ++i) {
        const FlowShape s = FlowShape(i);
        CHECK(!shapeOutline(s, r).isEmpty());
        CHECK(shapeFromKey(shapeKey(s)) == s);
    }
    const QPointF top = shapePort(FlowShape::Decision, QRectF(-50, -50, 100, 100), Port::Top);
    CHECK(std::abs(top.x()) < 0.1 && std::abs(top.y() + 50) < 0.1);
    const QPointF left = shapePort(FlowShape::InputOutput, QRectF(-100, -50, 200, 100), Port::Left);
    CHECK(left.x() > -95);  // slanted side: port sits on the slant, not the bounding box

    // Routing is always orthogonal, including awkward back-facing cases.
    const Port ports[] = {Port::Top, Port::Right, Port::Bottom, Port::Left};
    for (Port pa : ports)
        for (Port pb : ports)
            for (QPointF b : {QPointF(200, 120), QPointF(-200, -120), QPointF(0, 300), QPointF(10, -5)})
                CHECK(orthogonal(LinkItem::route({0, 0}, pa, b, pb)));

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
