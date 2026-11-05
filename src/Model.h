#pragma once
#include <QColor>
#include <QFont>
#include <QJsonObject>
#include <QPainterPath>
#include <QSizeF>
#include <QString>

inline constexpr char kShapeMime[] = "application/x-flowx-shape";
inline constexpr char kClipMime[] = "application/x-flowx";

enum class FlowShape {
    Terminator, Process, Decision, InputOutput, PredefinedProcess, Document, MultiDocument,
    ManualInput, ManualOperation, Preparation, Connector, OffPageConnector, Database, StoredData,
    Delay, Display, LoopLimit, HContainer, VContainer,
    Count
};

enum class Port { Top, Right, Bottom, Left };

QString shapeName(FlowShape s);
QString shapeKey(FlowShape s);
FlowShape shapeFromKey(const QString& key);  // unknown keys fall back to Process
QSizeF shapeDefaultSize(FlowShape s);
QPainterPath shapeOutline(FlowShape s, const QRectF& r);     // closed: filled, stroked, hit-tested
QPainterPath shapeDecoration(FlowShape s, const QRectF& r);  // extra inner lines, stroked only
QPointF shapePort(FlowShape s, const QRectF& r, Port p);     // where a link attaches, on the outline

// Containers (swimlanes) hold the shapes whose center lies inside them.
inline constexpr double kContainerHeader = 40;
bool isContainer(FlowShape s);
QRectF containerHeader(FlowShape s, const QRectF& r);  // title band: left for HContainer, top for VContainer

QPointF portDir(Port p);
QString portKey(Port p);
Port portFromKey(const QString& key);

struct Style {
    QColor fill{Qt::white};
    QColor stroke{0x47, 0x55, 0x69};
    QColor text{0x1f, 0x24, 0x30};
    double strokeWidth = 1.5;
    Qt::PenStyle dash = Qt::SolidLine;
    QString font = QStringLiteral("Segoe UI");
    int fontSize = 14;  // pixels in chart units, so it scales with zoom and export
    bool bold = false;
    bool italic = false;
    Qt::Alignment align = Qt::AlignHCenter;
    bool arrowStart = false;
    bool arrowEnd = true;
};

Style defaultNodeStyle(FlowShape s);
Style defaultLinkStyle();
QFont styleFont(const Style& s);
QJsonObject styleToJson(const Style& s);
Style styleFromJson(const QJsonObject& o);
