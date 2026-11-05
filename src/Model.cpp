#include "Model.h"
#include <QPolygonF>
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
    {"annotation", "Comment / Annotation", 150, 60},
    {"merge", "Merge", 80, 60},
    {"extract", "Extract", 80, 60},
    {"sort", "Sort", 80, 90},
    {"collate", "Collate", 70, 90},
    {"summing", "Summing Junction", 50, 50},
    {"or", "Or", 50, 50},
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

QPainterPath shapeOutline(FlowShape s, const QRectF& r) {
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
    case FlowShape::Annotation:
        p.addRect(r);
        return p;
    case FlowShape::Decision:
    case FlowShape::Sort:
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
    case FlowShape::SummingJunction:
    case FlowShape::Or:
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
    case FlowShape::Merge:
        return poly({{l, t}, {rt, t}, {cx, b}});
    case FlowShape::Extract:
        return poly({{cx, t}, {rt, b}, {l, b}});
    case FlowShape::Collate:
        return poly({{l, t}, {rt, t}, {l, b}, {rt, b}});
    case FlowShape::Count:
        break;
    }
    p.addRect(r);
    return p;
}

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
        break;
    }
    case FlowShape::Database: {
        const QRectF top(l, t, w, 2 * h * 0.12);
        p.arcMoveTo(top, 180);
        p.arcTo(top, 180, 180);
        break;
    }
    case FlowShape::Annotation: {
        const double k = std::min(w * 0.1, 14.0);
        p.moveTo(l + k, t);
        p.lineTo(l, t);
        p.lineTo(l, b);
        p.lineTo(l + k, b);
        break;
    }
    case FlowShape::Sort:
        p.moveTo(l, cy);
        p.lineTo(rt, cy);
        break;
    case FlowShape::SummingJunction: {
        const double dx = w / 2 * M_SQRT1_2, dy = h / 2 * M_SQRT1_2;
        p.moveTo(cx - dx, cy - dy);
        p.lineTo(cx + dx, cy + dy);
        p.moveTo(cx + dx, cy - dy);
        p.lineTo(cx - dx, cy + dy);
        break;
    }
    case FlowShape::Or:
        p.moveTo(cx, t);
        p.lineTo(cx, b);
        p.moveTo(l, cy);
        p.lineTo(rt, cy);
        break;
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

QPointF shapePort(FlowShape s, const QRectF& r, Port p) {
    const QPointF c = r.center(), d = portDir(p);
    const QPointF mid = c + QPointF(d.x() * r.width() / 2, d.y() * r.height() / 2);
    if (s == FlowShape::Annotation) return mid;
    const QPainterPath path = shapeOutline(s, r);
    if (!path.contains(c + d)) return mid;  // e.g. Collate's waist: no inside along that axis
    // Binary search along the ray from the center for the outline crossing.
    double lo = 0, hi = std::max(r.width(), r.height());
    for (int i = 0; i < 24; ++i) {
        const double m = (lo + hi) / 2;
        (path.contains(c + d * m) ? lo : hi) = m;
    }
    return c + d * lo;
}

Style defaultNodeStyle(FlowShape s) {
    Style st;
    if (s == FlowShape::Annotation) {
        st.fill = Qt::transparent;
        st.align = Qt::AlignLeft;
    }
    return st;
}

Style defaultLinkStyle() {
    Style st;
    st.stroke = QColor(0x64, 0x74, 0x8b);
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
