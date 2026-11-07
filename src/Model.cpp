#include "Model.h"
#include <QLineF>
#include <QPolygonF>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <iterator>

namespace {

struct ShapeInfo {
    const char* key;
    const char* name;
    double w, h;
};

const ShapeInfo kShapes[] = {
    {"terminator", "Terminator (Start / End)", 140, 50},
    {"process", "Process", 140, 70},
    {"decision", "Decision", 150, 90},
    {"io", "Input / Output", 150, 70},
    {"predefined", "Predefined Process", 150, 70},
    {"document", "Document", 140, 80},
    {"multidoc", "Multiple Documents", 150, 90},
    {"manualinput", "Manual Input", 140, 70},
    {"manualop", "Manual Operation", 140, 70},
    {"preparation", "Preparation", 150, 70},
    {"connector", "Connector", 50, 50},
    {"offpage", "Off-page Connector", 60, 60},
    {"database", "Database", 100, 90},
    {"storeddata", "Stored Data", 140, 70},
    {"delay", "Delay", 120, 70},
    {"display", "Display", 150, 70},
    {"looplimit", "Loop Limit", 140, 60},
    {"hcontainer", "Horizontal Container", 640, 220},
    {"vcontainer", "Vertical Container", 260, 480},
};
static_assert(std::size(kShapes) == size_t(FlowShape::Count));

const ShapeInfo& info(FlowShape s) { return kShapes[std::clamp(int(s), 0, int(FlowShape::Count) - 1)]; }

QPainterPath poly(std::initializer_list<QPointF> pts) {
    QPainterPath p;
    p.addPolygon(QPolygonF(QList<QPointF>(pts)));
    p.closeSubpath();
    return p;
}

QPainterPath documentPath(const QRectF& r) {
    const double a = r.height() * 0.1, y0 = r.bottom() - a, w = r.width();
    QPainterPath p;
    p.moveTo(r.topLeft());
    p.lineTo(r.topRight());
    p.lineTo(r.right(), y0);
    p.cubicTo(r.left() + 0.75 * w, y0 - 2 * a, r.left() + 0.25 * w, y0 + 2 * a, r.left(), y0);
    p.closeSubpath();
    return p;
}

// Front page of the "multiple documents" stack; the two back pages are offset up-right.
QRectF multiDocFront(const QRectF& r, double& o) {
    o = std::min(r.width(), r.height()) * 0.08;
    return r.adjusted(0, 2 * o, -2 * o, 0);
}

}  // namespace

QString shapeName(FlowShape s) { return QString::fromLatin1(info(s).name); }
QString shapeKey(FlowShape s) { return QString::fromLatin1(info(s).key); }
QSizeF shapeDefaultSize(FlowShape s) { return {info(s).w, info(s).h}; }

FlowShape shapeFromKey(const QString& key) {
    for (int i = 0; i < int(FlowShape::Count); ++i)
        if (key == QLatin1String(kShapes[i].key)) return FlowShape(i);
    return FlowShape::Process;
}

// Light rounded corners: every sharp turn (> 20 degrees) in the path becomes a short curve.
// ponytail: works on the flattened path (4x finer than Qt's default), so curves turn into short
// lines; fine at normal zoom. Round line/curve joins analytically if deep zoom shows facets.
static QPainterPath roundCorners(const QPainterPath& path, double radius) {
    QPainterPath out;
    const QTransform up = QTransform::fromScale(4, 4), down = QTransform::fromScale(0.25, 0.25);
    for (QPolygonF poly : path.toSubpathPolygons(up)) {
        poly = down.map(poly);
        const bool closed = poly.size() > 2 && poly.first() == poly.last();
        if (closed) poly.removeLast();
        const int n = int(poly.size());
        if (n < 2) continue;
        QList<QPointF> a(n), b(n);  // where each corner's curve starts and ends
        QList<bool> round(n, false);
        for (int i = 0; i < n; ++i) {
            a[i] = b[i] = poly[i];
            if (!closed && (i == 0 || i == n - 1)) continue;
            const QPointF v = poly[i], pv = poly[(i + n - 1) % n], nx = poly[(i + 1) % n];
            const QLineF in(pv, v), outLine(v, nx);
            if (in.length() < 1e-9 || outLine.length() < 1e-9) continue;
            const double turn = in.angleTo(outLine);
            if (std::min(turn, 360 - turn) <= 20) continue;
            const double d = std::min({radius, in.length() / 2, outLine.length() / 2});
            a[i] = v + (pv - v) * (d / in.length());
            b[i] = v + (nx - v) * (d / outLine.length());
            round[i] = true;
        }
        out.moveTo(b[0]);
        for (int i = 1; i < n; ++i) {
            out.lineTo(a[i]);
            if (round[i]) out.quadTo(poly[i], b[i]);
        }
        if (closed) {
            out.lineTo(a[0]);
            if (round[0]) out.quadTo(poly[0], b[0]);
            out.closeSubpath();
        }
    }
    return out;
}

static double cornerRadius(const QRectF& r) { return std::min(6.0, std::min(r.width(), r.height()) * 0.12); }

static QPainterPath sharpOutline(FlowShape s, const QRectF& r) {
    const double l = r.left(), t = r.top(), rt = r.right(), b = r.bottom();
    const double w = r.width(), h = r.height(), cx = r.center().x(), cy = r.center().y();
    QPainterPath p;
    switch (s) {
    case FlowShape::Terminator: {
        const double rad = std::min(w, h) / 2;
        p.addRoundedRect(r, rad, rad);
        return p;
    }
    case FlowShape::Process:
    case FlowShape::PredefinedProcess:
    case FlowShape::HContainer:
    case FlowShape::VContainer:
        p.addRect(r);
        return p;
    case FlowShape::Decision:
        return poly({{cx, t}, {rt, cy}, {cx, b}, {l, cy}});
    case FlowShape::InputOutput: {
        const double k = w * 0.15;
        return poly({{l + k, t}, {rt, t}, {rt - k, b}, {l, b}});
    }
    case FlowShape::Document:
        return documentPath(r);
    case FlowShape::MultiDocument: {
        double o;
        const QRectF f = multiDocFront(r, o);
        return documentPath(f.translated(2 * o, -2 * o))
            .united(documentPath(f.translated(o, -o)))
            .united(documentPath(f));
    }
    case FlowShape::ManualInput:
        return poly({{l, t + h * 0.3}, {rt, t}, {rt, b}, {l, b}});
    case FlowShape::ManualOperation: {
        const double k = w * 0.15;
        return poly({{l, t}, {rt, t}, {rt - k, b}, {l + k, b}});
    }
    case FlowShape::Preparation: {
        const double k = w * 0.15;
        return poly({{l + k, t}, {rt - k, t}, {rt, cy}, {rt - k, b}, {l + k, b}, {l, cy}});
    }
    case FlowShape::Connector:
        p.addEllipse(r);
        return p;
    case FlowShape::OffPageConnector:
        return poly({{l, t}, {rt, t}, {rt, t + h * 0.6}, {cx, b}, {l, t + h * 0.6}});
    case FlowShape::Database: {
        const double e = h * 0.12;
        const QRectF top(l, t, w, 2 * e), bottom(l, b - 2 * e, w, 2 * e);
        p.moveTo(l, t + e);
        p.arcTo(top, 180, -180);
        p.lineTo(rt, b - e);
        p.arcTo(bottom, 0, -180);
        p.closeSubpath();
        return p;
    }
    case FlowShape::StoredData: {
        const double e = w * 0.1;
        p.moveTo(l + e, t);
        p.lineTo(rt, t);
        p.quadTo(rt - 2 * e, cy, rt, b);
        p.lineTo(l + e, b);
        p.quadTo(l - e, cy, l + e, t);
        p.closeSubpath();
        return p;
    }
    case FlowShape::Delay: {
        const double rx = std::min(w / 2, h / 2);
        p.moveTo(l, t);
        p.lineTo(rt - rx, t);
        p.arcTo(QRectF(rt - 2 * rx, t, 2 * rx, h), 90, -180);
        p.lineTo(l, b);
        p.closeSubpath();
        return p;
    }
    case FlowShape::Display: {
        const double k = w * 0.15, rx = w * 0.12;
        p.moveTo(l, cy);
        p.lineTo(l + k, t);
        p.lineTo(rt - rx, t);
        p.arcTo(QRectF(rt - 2 * rx, t, 2 * rx, h), 90, -180);
        p.lineTo(l + k, b);
        p.closeSubpath();
        return p;
    }
    case FlowShape::LoopLimit: {
        const double k = std::min(w, h) * 0.25;
        return poly({{l + k, t}, {rt - k, t}, {rt, t + k}, {rt, b}, {l, b}, {l, t + k}});
    }
    case FlowShape::Count:
        break;
    }
    p.addRect(r);
    return p;
}

QPainterPath shapeOutline(FlowShape s, const QRectF& r) { return roundCorners(sharpOutline(s, r), cornerRadius(r)); }

QPainterPath shapeDecoration(FlowShape s, const QRectF& r) {
    const double l = r.left(), t = r.top(), rt = r.right(), b = r.bottom();
    const double w = r.width(), h = r.height(), cx = r.center().x(), cy = r.center().y();
    QPainterPath p;
    switch (s) {
    case FlowShape::PredefinedProcess:
        p.moveTo(l + w * 0.1, t);
        p.lineTo(l + w * 0.1, b);
        p.moveTo(rt - w * 0.1, t);
        p.lineTo(rt - w * 0.1, b);
        break;
    case FlowShape::MultiDocument: {
        double o;
        const QRectF f = multiDocFront(r, o);
        const QPainterPath front = documentPath(f);
        // Front page outline plus the visible part of the middle page.
        p = documentPath(f.translated(o, -o)).subtracted(front);
        p.addPath(front);
        p = roundCorners(p, cornerRadius(r));  // match the rounded outline
        break;
    }
    case FlowShape::Database: {
        const QRectF top(l, t, w, 2 * h * 0.12);
        p.arcMoveTo(top, 180);
        p.arcTo(top, 180, 180);
        break;
    }
    case FlowShape::HContainer:
    case FlowShape::VContainer: {
        const QRectF hd = containerHeader(s, r);  // line between title band and body
        if (s == FlowShape::HContainer) {
            p.moveTo(hd.right(), t);
            p.lineTo(hd.right(), b);
        } else {
            p.moveTo(l, hd.bottom());
            p.lineTo(rt, hd.bottom());
        }
        break;
    }
    default:
        break;
    }
    return p;
}

QPointF portDir(Port p) {
    switch (p) {
    case Port::Top: return {0, -1};
    case Port::Right: return {1, 0};
    case Port::Bottom: return {0, 1};
    case Port::Left: return {-1, 0};
    }
    return {0, 1};
}

QString portKey(Port p) {
    static const char* keys[] = {"top", "right", "bottom", "left"};
    return QString::fromLatin1(keys[int(p)]);
}

Port portFromKey(const QString& key) {
    if (key == QLatin1String("top")) return Port::Top;
    if (key == QLatin1String("right")) return Port::Right;
    if (key == QLatin1String("left")) return Port::Left;
    return Port::Bottom;
}

QPointF shapePort(FlowShape s, const QRectF& r, Port p) { return shapePort(shapeOutline(s, r), r, p); }

QPointF shapePort(const QPainterPath& path, const QRectF& r, Port p) {
    const QPointF c = r.center(), d = portDir(p);
    const QPointF mid = c + QPointF(d.x() * r.width() / 2, d.y() * r.height() / 2);
    if (!path.contains(c + d)) return mid;  // degenerate (tiny) shape
    // Binary search along the ray from the center for the outline crossing.
    double lo = 0, hi = std::max(r.width(), r.height());
    for (int i = 0; i < 24; ++i) {
        const double m = (lo + hi) / 2;
        (path.contains(c + d * m) ? lo : hi) = m;
    }
    return c + d * lo;
}

bool isContainer(FlowShape s) { return s == FlowShape::HContainer || s == FlowShape::VContainer; }

QRectF shapeTextRect(FlowShape s, const QRectF& r) {
    const double w = r.width(), h = r.height();
    double l = 0, t = 0, rt = 0, b = 0;  // insets from each side
    switch (s) {
    case FlowShape::Terminator: l = rt = std::min(w, h) / 2 * 0.7; break;  // keep out of the round ends
    case FlowShape::PredefinedProcess: l = rt = w * 0.1; break;              // between the side bars
    case FlowShape::Decision: l = rt = w / 4; t = b = h / 4; break;         // rectangle inscribed in the diamond
    case FlowShape::InputOutput:
    case FlowShape::ManualOperation:
    case FlowShape::Preparation: l = rt = w * 0.15; break;
    case FlowShape::Document: b = h * 0.2; break;  // above the wavy edge
    case FlowShape::MultiDocument: {
        double o;
        const QRectF f = multiDocFront(r, o);
        return f.adjusted(0, 0, 0, -f.height() * 0.2);
    }
    case FlowShape::ManualInput: t = h * 0.3; break;
    case FlowShape::Connector: l = rt = w * 0.146; t = b = h * 0.146; break;  // rectangle inscribed in the ellipse
    case FlowShape::OffPageConnector: l = rt = w * 0.15; b = h * 0.3; break;
    case FlowShape::Database: t = h * 0.24; b = h * 0.12; break;
    case FlowShape::StoredData: l = rt = w * 0.1; break;
    case FlowShape::Delay: rt = std::min(w, h) / 2 * 0.7; break;
    case FlowShape::Display: l = w * 0.15; rt = w * 0.1; break;
    case FlowShape::LoopLimit: l = rt = t = std::min(w, h) * 0.125; break;  // below the cut corners
    default: break;
    }
    return r.adjusted(l, t, -rt, -b);
}

QRectF containerHeader(FlowShape s, const QRectF& r) {
    if (s == FlowShape::HContainer) return {r.left(), r.top(), std::min(kContainerHeader, r.width()), r.height()};
    return {r.left(), r.top(), r.width(), std::min(kContainerHeader, r.height())};
}

Style defaultNodeStyle(FlowShape s) {
    Style st;
    if (isContainer(s)) {
        st.fill = QColor(248, 250, 252, 200);  // light, slightly see-through so the grid still shows
        st.bold = true;
    }
    return st;
}

Style defaultLinkStyle() {
    Style st;
    st.stroke = QColor(0x64, 0x74, 0x8b);
    st.fill = Qt::transparent;  // label sits beside the line, so it needs no backing
    st.fontSize = 13;
    return st;
}

QFont styleFont(const Style& s) {
    QFont f(s.font);
    f.setPixelSize(std::max(1, s.fontSize));
    f.setBold(s.bold);
    f.setItalic(s.italic);
    return f;
}

QJsonObject styleToJson(const Style& s) {
    const char* align = s.align & Qt::AlignLeft ? "left" : s.align & Qt::AlignRight ? "right" : "center";
    return {
        {"fill", s.fill.name(QColor::HexArgb)},
        {"stroke", s.stroke.name(QColor::HexArgb)},
        {"text", s.text.name(QColor::HexArgb)},
        {"strokeWidth", s.strokeWidth},
        {"dash", int(s.dash)},
        {"font", s.font},
        {"fontSize", s.fontSize},
        {"bold", s.bold},
        {"italic", s.italic},
        {"align", QLatin1String(align)},
        {"arrowStart", s.arrowStart},
        {"arrowEnd", s.arrowEnd},
    };
}

Style styleFromJson(const QJsonObject& o) {
    Style s;
    auto color = [&](const char* key, QColor& out) {
        const QColor c = QColor::fromString(o[QLatin1String(key)].toString());
        if (c.isValid()) out = c;
    };
    color("fill", s.fill);
    color("stroke", s.stroke);
    color("text", s.text);
    s.strokeWidth = std::clamp(o["strokeWidth"].toDouble(s.strokeWidth), 0.0, 50.0);
    const int dash = o["dash"].toInt(int(s.dash));
    s.dash = dash >= int(Qt::NoPen) && dash <= int(Qt::DashDotDotLine) ? Qt::PenStyle(dash) : Qt::SolidLine;
    s.font = o["font"].toString(s.font);
    s.fontSize = std::clamp(o["fontSize"].toInt(s.fontSize), 4, 400);
    s.bold = o["bold"].toBool(s.bold);
    s.italic = o["italic"].toBool(s.italic);
    const QString align = o["align"].toString();
    s.align = align == QLatin1String("left") ? Qt::AlignLeft : align == QLatin1String("right") ? Qt::AlignRight : Qt::AlignHCenter;
    s.arrowStart = o["arrowStart"].toBool(s.arrowStart);
    s.arrowEnd = o["arrowEnd"].toBool(s.arrowEnd);
    return s;
}
