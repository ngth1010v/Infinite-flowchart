#include "Panels.h"
#include "ChartScene.h"
#include "ChartView.h"
#include <QGridLayout>
#include <QLineEdit>
#include <QShortcut>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScopedValueRollback>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>
#include <type_traits>

// ---------------------------------------------------------------- ShapePalette

namespace {
// Collapsed palette: icon on top of the (empty) text, which centers it in the row.
class PaletteDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    bool compact = false;

    QSize sizeHint(const QStyleOptionViewItem& o, const QModelIndex& i) const override {
        const QScopedValueRollback keep(m_sizing, true);  // rows keep their expanded height
        return QStyledItemDelegate::sizeHint(o, i);
    }

protected:
    void initStyleOption(QStyleOptionViewItem* o, const QModelIndex& i) const override {
        QStyledItemDelegate::initStyleOption(o, i);
        if (compact && !m_sizing) o->decorationPosition = QStyleOptionViewItem::Top;
    }

private:
    mutable bool m_sizing = false;
};
}  // namespace

ShapePalette::ShapePalette(QWidget* parent) : QListWidget(parent) {
    setItemDelegate(new PaletteDelegate(this));
    setIconSize(QSize(44, 30));
    setDragEnabled(true);
    setDragDropMode(DragOnly);
    setSelectionMode(SingleSelection);
    setFrameShape(QFrame::NoFrame);
    setSpacing(1);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(this, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        if (!it->data(Qt::UserRole).isValid()) return;  // section header
        emit shapeClicked(FlowShape(it->data(Qt::UserRole).toInt()));
        clearSelection();
    });
}

void ShapePalette::rebuildIcons(const QColor& line) {
    clear();
    const qreal dpr = devicePixelRatioF();
    for (int i = 0; i < int(FlowShape::Count); ++i) {
        const FlowShape s = FlowShape(i);
        if (i == 0 || (isContainer(s) && !isContainer(FlowShape(i - 1)))) {  // section header
            const QString title = isContainer(s) ? tr("Containers") : tr("Shapes");
            auto* h = new QListWidgetItem(m_compact ? QString() : title);
            h->setData(Qt::UserRole + 1, title);
            h->setFlags(Qt::NoItemFlags);
            QFont f = font();
            f.setBold(true);
            h->setFont(f);
            if (i > 0) {  // later sections get a gap above, which stays as a spacer when collapsed
                h->setTextAlignment(Qt::AlignLeft | Qt::AlignBottom);
                h->setSizeHint(QSize(0, QFontMetrics(f).height() + 28));
            }
            h->setHidden(m_compact && i == 0);
            addItem(h);
        }
        QPixmap pm(QSize(44, 30) * dpr);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const QSizeF ds = shapeDefaultSize(s);
        const double k = std::min(40 / ds.width(), 26 / ds.height());
        QRectF r(0, 0, ds.width() * k, ds.height() * k);
        r.moveCenter(QPointF(22, 15));
        p.setPen(QPen(line, 1.3));
        p.setBrush(Qt::NoBrush);
        p.drawPath(shapeOutline(s, r));
        p.drawPath(shapeDecoration(s, r));
        p.end();
        auto* it = new QListWidgetItem(QIcon(pm), m_compact ? QString() : shapeName(s));
        it->setData(Qt::UserRole, i);
        it->setToolTip(shapeName(s) + '\n' + tr("Drag onto the canvas, or click to add at the center"));
        addItem(it);
    }
}

void ShapePalette::setCompact(bool compact) {
    m_compact = compact;
    static_cast<PaletteDelegate*>(itemDelegate())->compact = compact;
    for (int i = 0; i < count(); ++i) {
        QListWidgetItem* it = item(i);
        if (!it->data(Qt::UserRole).isValid()) {  // header: no title when collapsed, but keep its gap
            it->setText(compact ? QString() : it->data(Qt::UserRole + 1).toString());
            it->setHidden(compact && i == 0);
        } else it->setText(compact ? QString() : shapeName(FlowShape(it->data(Qt::UserRole).toInt())));
    }
    doItemsLayout();
}

QStringList ShapePalette::mimeTypes() const { return {QString::fromLatin1(kShapeMime)}; }

QMimeData* ShapePalette::mimeData(const QList<QListWidgetItem*>& items) const {
    auto* md = new QMimeData;
    if (!items.isEmpty()) md->setData(kShapeMime, shapeKey(FlowShape(items.first()->data(Qt::UserRole).toInt())).toUtf8());
    return md;
}

// ---------------------------------------------------------------- StylePanel

namespace {

// The value every item shares, or nullopt when they differ (or there are none).
template <class F>
auto common(const QList<ChartItem*>& items, F get) {
    using T = std::decay_t<decltype(get(items.value(0)))>;
    std::optional<T> v;
    for (auto* it : items) {
        T x = get(it);
        if (!v) v = x;
        else if (!(*v == x)) return std::optional<T>{};
    }
    return v;
}

// Spin boxes reserve their minimum as the blank "mixed" value.
template <class S, class T>
void showValue(S* s, const std::optional<T>& v) {
    s->setValue(v ? static_cast<decltype(s->value())>(*v) : s->minimum());
}

void showIndex(QComboBox* c, const std::optional<int>& v) { c->setCurrentIndex(v ? c->findData(*v) : -1); }

void showCheck(QCheckBox* c, const std::optional<bool>& v) {
    c->setTristate(!v);
    c->setCheckState(!v ? Qt::PartiallyChecked : *v ? Qt::Checked : Qt::Unchecked);
}

QLabel* section(const QString& text) {
    auto* l = new QLabel(text.toUpper());
    QFont f = l->font();
    f.setPointSizeF(f.pointSizeF() * 0.85);
    f.setBold(true);
    l->setFont(f);
    l->setContentsMargins(0, 10, 0, 2);
    l->setEnabled(false);  // muted color from the palette
    return l;
}

QFormLayout* form(QWidget* host = nullptr) {
    auto* f = new QFormLayout(host);
    f->setContentsMargins(0, 0, 0, 0);
    f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    return f;
}

NodeItem* asNode(ChartItem* c) { return qgraphicsitem_cast<NodeItem*>(c); }

}  // namespace

StylePanel::StylePanel(ChartScene* scene, QWidget* parent) : QWidget(parent), m_scene(scene) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(14, 12, 14, 12);

    m_title = new QLabel;
    QFont tf = m_title->font();
    tf.setPointSizeF(tf.pointSizeF() * 1.2);
    tf.setBold(true);
    m_title->setFont(tf);
    lay->addWidget(m_title);

    lay->addWidget(section(tr("Text")));
    m_text = new QPlainTextEdit;
    m_text->setFixedHeight(64);
    lay->addWidget(m_text);
    connect(m_text, &QPlainTextEdit::textChanged, this, [this] {
        const QString t = m_text->toPlainText();
        edit("text", [&](ChartItem* c) { c->text = t; });
    });

    // Node-only: shape and size
    m_nodeBox = new QWidget;
    auto* nf = form(m_nodeBox);
    m_shape = new QComboBox;
    for (int i = 0; i < int(FlowShape::Count); ++i) m_shape->addItem(shapeName(FlowShape(i)), i);
    m_w = new QSpinBox;
    m_h = new QSpinBox;
    for (auto* s : {m_w, m_h}) {
        s->setRange(19, 20000);  // 19 = blank "mixed"
        s->setSpecialValueText(" ");
        s->setSuffix(" px");
    }
    nf->addRow(section(tr("Shape")));
    nf->addRow(tr("Type"), m_shape);
    nf->addRow(tr("Width"), m_w);
    nf->addRow(tr("Height"), m_h);
    lay->addWidget(m_nodeBox);
    connect(m_shape, &QComboBox::activated, this, [this](int i) {
        const FlowShape s = FlowShape(m_shape->itemData(i).toInt());
        edit("shape", [&](ChartItem* c) {
            if (auto* n = asNode(c)) n->shapeType = s;
        });
    });
    connect(m_w, &QSpinBox::valueChanged, this, [this](int v) {
        edit("w", [&](ChartItem* c) {
            if (auto* n = asNode(c)) n->size.setWidth(std::max(20, v));
        });
    });
    connect(m_h, &QSpinBox::valueChanged, this, [this](int v) {
        edit("h", [&](ChartItem* c) {
            if (auto* n = asNode(c)) n->size.setHeight(std::max(20, v));
        });
    });

    // Container-only: padding kept around the contents by Ctrl+Shift+F (fit to content)
    m_containerBox = new QWidget;
    auto* cf = form(m_containerBox);
    m_padding = new QSpinBox;
    m_padding->setRange(-1, 2000);  // -1 = blank "mixed"
    m_padding->setSpecialValueText(" ");
    m_padding->setSuffix(" px");
    m_padding->setToolTip(tr("Space around the contents when fitting the container (Ctrl+Shift+F)"));
    cf->addRow(tr("Padding"), m_padding);
    lay->addWidget(m_containerBox);
    connect(m_padding, &QSpinBox::valueChanged, this, [this](int v) {
        if (v < 0) return;
        edit("padding", [&](ChartItem* c) {
            if (auto* n = asNode(c); n && isContainer(n->shapeType)) n->padding = v;
        });
    });

    // Shared: fill, line, text style
    auto* sf = form();
    m_fill = colorButton("fill", &Style::fill);
    m_stroke = colorButton("stroke", &Style::stroke);
    m_strokeW = new QDoubleSpinBox;
    m_strokeW->setRange(0, 20);  // 0 = blank "mixed"; real widths start at 0.5
    m_strokeW->setSingleStep(0.5);
    m_strokeW->setDecimals(1);
    m_strokeW->setSpecialValueText(" ");
    m_dash = new QComboBox;
    m_dash->addItem(tr("Solid"), int(Qt::SolidLine));
    m_dash->addItem(tr("Dashed"), int(Qt::DashLine));
    m_dash->addItem(tr("Dotted"), int(Qt::DotLine));
    m_dash->addItem(tr("Dash-dot"), int(Qt::DashDotLine));
    m_dash->addItem(tr("None"), int(Qt::NoPen));
    sf->addRow(section(tr("Fill & line")));
    sf->addRow(tr("Fill"), m_fill);
    sf->addRow(tr("Line color"), m_stroke);
    sf->addRow(tr("Line width"), m_strokeW);
    sf->addRow(tr("Line style"), m_dash);
    connect(m_strokeW, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        edit("strokeWidth", [&](ChartItem* c) { c->style.strokeWidth = std::max(0.5, v); });
    });
    connect(m_dash, &QComboBox::activated, this, [this](int i) {
        const auto d = Qt::PenStyle(m_dash->itemData(i).toInt());
        edit("dash", [&](ChartItem* c) { c->style.dash = d; });
    });

    m_textColor = colorButton("textColor", &Style::text);
    m_font = new QFontComboBox;
    m_fontSize = new QSpinBox;
    m_fontSize->setRange(5, 200);  // 5 = blank "mixed"
    m_fontSize->setSpecialValueText(" ");
    m_fontSize->setSuffix(" px");
    m_bold = checkBox(tr("Bold"), "bold", &Style::bold);
    m_italic = checkBox(tr("Italic"), "italic", &Style::italic);
    auto* bi = new QHBoxLayout;
    bi->addWidget(m_bold);
    bi->addWidget(m_italic);
    bi->addStretch();
    m_align = new QComboBox;
    m_align->addItem(tr("Left"), int(Qt::AlignLeft));
    m_align->addItem(tr("Center"), int(Qt::AlignHCenter));
    m_align->addItem(tr("Right"), int(Qt::AlignRight));
    sf->addRow(section(tr("Font")));
    sf->addRow(tr("Color"), m_textColor);
    sf->addRow(tr("Font"), m_font);
    sf->addRow(tr("Size"), m_fontSize);
    sf->addRow(QString(), bi);
    sf->addRow(tr("Align"), m_align);
    lay->addLayout(sf);
    connect(m_font, &QComboBox::activated, this, [this] {
        const QString family = m_font->currentFont().family();
        edit("font", [&](ChartItem* c) { c->style.font = family; });
    });
    connect(m_fontSize, &QSpinBox::valueChanged, this, [this](int v) {
        edit("fontSize", [&](ChartItem* c) { c->style.fontSize = std::max(6, v); });
    });
    connect(m_align, &QComboBox::activated, this, [this](int i) {
        const auto a = Qt::Alignment(m_align->itemData(i).toInt());
        edit("align", [&](ChartItem* c) { c->style.align = a; });
    });

    // Link-only: arrow heads
    m_linkBox = new QWidget;
    auto* lf = form(m_linkBox);
    m_arrowStart = checkBox(tr("Arrow at start"), "arrowStart", &Style::arrowStart);
    m_arrowEnd = checkBox(tr("Arrow at end"), "arrowEnd", &Style::arrowEnd);
    lf->addRow(section(tr("Arrows")));
    lf->addRow(m_arrowStart);
    lf->addRow(m_arrowEnd);
    lay->addWidget(m_linkBox);
    lay->addStretch();

    m_scene = nullptr;
    setScene(scene);
}

void StylePanel::setScene(ChartScene* scene) {
    if (m_scene) {
        disconnect(m_scene, nullptr, this, nullptr);
        disconnect(m_scene->undoStack(), nullptr, this, nullptr);
    }
    m_scene = scene;
    if (scene) {
        connect(scene, &QGraphicsScene::selectionChanged, this, &StylePanel::refresh);
        connect(scene->undoStack(), &QUndoStack::indexChanged, this, [this] {
            if (!m_applying) refresh();  // undo/redo or canvas edits; skip our own pushes
        });
        connect(scene, &QObject::destroyed, this, [this] { m_scene = nullptr; });
    }
    refresh();
}

void StylePanel::edit(const QString& key, const std::function<void(ChartItem*)>& fn) {
    if (m_updating || !m_scene) return;
    m_applying = true;
    m_scene->editSelected(key, fn);
    m_applying = false;
}

QToolButton* StylePanel::colorButton(const QString& key, QColor Style::*field) {
    auto* b = new QToolButton;
    b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(b, &QToolButton::clicked, this, [this, b, key, field] {
        QColor cur = b->property("color").value<QColor>();
        const QColor c = QColorDialog::getColor(cur.isValid() ? cur : Qt::white, this, tr("Choose color"),
                                                QColorDialog::ShowAlphaChannel);
        if (!c.isValid()) return;
        edit(key, [&](ChartItem* it) { it->style.*field = c; });
        showColor(b, c);
    });
    return b;
}

QCheckBox* StylePanel::checkBox(const QString& text, const QString& key, bool Style::*field) {
    auto* cb = new QCheckBox(text);
    connect(cb, &QCheckBox::clicked, this, [this, cb, key, field] {
        cb->setTristate(false);
        const bool on = cb->checkState() == Qt::Checked;
        edit(key, [&](ChartItem* it) { it->style.*field = on; });
    });
    return cb;
}

void StylePanel::showColor(QToolButton* b, const std::optional<QColor>& c) {
    b->setProperty("color", c ? *c : QColor());
    if (!c) {
        b->setIcon(QIcon());
        b->setText(QStringLiteral("—"));
        return;
    }
    QPixmap pm(18, 18);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    if (c->alpha() < 255) {  // checkerboard so transparency is visible
        p.fillRect(0, 0, 18, 18, Qt::white);
        p.fillRect(0, 0, 9, 9, Qt::lightGray);
        p.fillRect(9, 9, 9, 9, Qt::lightGray);
    }
    p.fillRect(0, 0, 18, 18, *c);
    p.setPen(Qt::gray);
    p.drawRect(0, 0, 17, 17);
    p.end();
    b->setIcon(pm);
    b->setText(c->alpha() == 0 ? tr("None") : c->alpha() < 255 ? c->name(QColor::HexArgb) : c->name());
}

void StylePanel::refresh() {
    const QList<ChartItem*> items = m_scene ? m_scene->selectedChartItems() : QList<ChartItem*>();
    QList<ChartItem*> nodes, links;
    for (auto* c : items) (c->type() == NodeItem::Type ? nodes : links) << c;
    m_text->setEnabled(!items.isEmpty());
    if (items.isEmpty()) {  // panel can be open with nothing selected
        m_title->setText(tr("Nothing selected"));
        m_text->clear();
        m_text->setPlaceholderText(QString());
        m_nodeBox->hide();
        m_containerBox->hide();
        m_linkBox->hide();
        return;
    }

    m_updating = true;
    m_title->setText(items.size() > 1 ? tr("%1 items selected").arg(items.size())
                     : nodes.size() == 1 ? shapeName(asNode(nodes[0])->shapeType)
                                         : tr("Link"));
    m_nodeBox->setVisible(!nodes.isEmpty());
    QList<ChartItem*> boxes;
    for (auto* c : nodes)
        if (isContainer(asNode(c)->shapeType)) boxes << c;
    m_containerBox->setVisible(!boxes.isEmpty());
    showValue(m_padding, common(boxes, [](ChartItem* c) { return qRound(asNode(c)->padding); }));
    m_linkBox->setVisible(!links.isEmpty());

    if (const auto t = common(items, [](ChartItem* c) { return c->text; })) {
        if (m_text->toPlainText() != *t) m_text->setPlainText(*t);
        m_text->setPlaceholderText(QString());
    } else {
        m_text->clear();
        m_text->setPlaceholderText(tr("(mixed)"));
    }

    showIndex(m_shape, common(nodes, [](ChartItem* c) { return int(asNode(c)->shapeType); }));
    showValue(m_w, common(nodes, [](ChartItem* c) { return qRound(asNode(c)->size.width()); }));
    showValue(m_h, common(nodes, [](ChartItem* c) { return qRound(asNode(c)->size.height()); }));

    showColor(m_fill, common(items, [](ChartItem* c) { return c->style.fill; }));
    showColor(m_stroke, common(items, [](ChartItem* c) { return c->style.stroke; }));
    showColor(m_textColor, common(items, [](ChartItem* c) { return c->style.text; }));
    showValue(m_strokeW, common(items, [](ChartItem* c) { return c->style.strokeWidth; }));
    showIndex(m_dash, common(items, [](ChartItem* c) { return int(c->style.dash); }));
    showValue(m_fontSize, common(items, [](ChartItem* c) { return c->style.fontSize; }));
    showIndex(m_align, common(items, [](ChartItem* c) { return int(c->style.align); }));
    if (const auto f = common(items, [](ChartItem* c) { return c->style.font; }))
        m_font->setCurrentFont(QFont(*f));
    else
        m_font->setCurrentIndex(-1);
    showCheck(m_bold, common(items, [](ChartItem* c) { return c->style.bold; }));
    showCheck(m_italic, common(items, [](ChartItem* c) { return c->style.italic; }));
    showCheck(m_arrowStart, common(links, [](ChartItem* c) { return c->style.arrowStart; }));
    showCheck(m_arrowEnd, common(links, [](ChartItem* c) { return c->style.arrowEnd; }));
    m_updating = false;
}

// ---------------------------------------------------------------- FindPanel

FindPanel::FindPanel(ChartView* view, QWidget* parent) : QWidget(parent, Qt::Tool | Qt::CustomizeWindowHint | Qt::WindowTitleHint), m_view(view) {  // title bar to drag by, no close box: the ✕ button closes
    setWindowTitle(tr("Find & replace"));
    m_find = new QLineEdit;
    m_find->setPlaceholderText(tr("Find"));
    m_find->setClearButtonEnabled(true);
    m_replace = new QLineEdit;
    m_replace->setPlaceholderText(tr("Replace with"));
    m_count = new QLabel;
    m_count->setMinimumWidth(56);
    auto button = [this](const QString& text, const QString& tip, auto fn) {
        auto* b = new QToolButton;
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, fn);
        return b;
    };
    auto* g = new QGridLayout(this);
    g->setContentsMargins(6, 6, 6, 6);
    g->setSpacing(4);
    m_case = button(QStringLiteral("Aa"), tr("Match case (Alt+C)"), [this] {
        if (!m_view) return;
        m_view->chart()->findCase = m_case->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
        step(0);
    });
    m_case->setCheckable(true);
    m_case->setShortcut(QKeySequence(Qt::ALT | Qt::Key_C));
    g->addWidget(m_find, 0, 0);
    g->addWidget(m_case, 0, 1);
    g->addWidget(m_count, 0, 2);
    g->addWidget(button(QStringLiteral("↑"), tr("Previous match (Shift+Enter)"), [this] { step(-1); }), 0, 3);
    g->addWidget(button(QStringLiteral("↓"), tr("Next match (Enter)"), [this] { step(1); }), 0, 4);
    g->addWidget(button(QStringLiteral("✕"), tr("Close (Esc)"), [this] { close(); }), 0, 5);
    g->addWidget(m_replace, 1, 0);
    g->addWidget(button(tr("Replace"), tr("Replace in the current match, then go to the next (Enter in the replace box)"),
                        [this] { replace(false); }), 1, 1, 1, 2);
    g->addWidget(button(tr("Replace all"), tr("Replace in every match (Ctrl+Alt+Enter)"), [this] { replace(true); }), 1, 3, 1, 3);
    setFixedWidth(400);

    connect(m_find, &QLineEdit::textChanged, this, [this] { step(0); });
    connect(m_find, &QLineEdit::returnPressed, this, [this] {
        step(QGuiApplication::keyboardModifiers() & Qt::ShiftModifier ? -1 : 1);
    });
    connect(m_replace, &QLineEdit::returnPressed, this, [this] { replace(false); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Return), this, [this] { replace(true); });
    new QShortcut(QKeySequence(Qt::Key_Escape), this, [this] { close(); });
    new QShortcut(QKeySequence::Find, this, [this] { open(); });  // Ctrl+F again: back to the search box
    m_view = nullptr;
    setView(view);
}

void FindPanel::setView(ChartView* view) {
    if (m_view == view) return;
    if (m_view) {  // the old chart stops highlighting
        ChartScene* s = m_view->chart();
        disconnect(s->undoStack(), nullptr, this, nullptr);
        s->findText.clear();
        s->findCurrent = 0;
        s->update();
    }
    m_view = view;
    if (!view) return hide();
    ChartScene* s = view->chart();
    s->findCase = m_case->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    connect(s->undoStack(), &QUndoStack::indexChanged, this, [this] {
        if (isVisible()) refresh();
    });
    if (isVisible()) step(0);
}

void FindPanel::open() {
    if (!m_view) return;
    if (!m_placed) {  // top-right corner of the canvas, clear of the minimap
        m_placed = true;
        adjustSize();
        const QPoint tr = m_view->mapToGlobal(m_view->rect().topRight());
        move(tr.x() - width() - 260, tr.y() + 12);
    }
    show();
    raise();
    activateWindow();
    m_find->setFocus();
    m_find->selectAll();
    step(0);
}

void FindPanel::hideEvent(QHideEvent* e) {
    if (m_view) {  // gone when the app shuts down
        ChartScene* s = m_view->chart();
        s->findText.clear();
        s->findCurrent = 0;
        s->update();
    }
    QWidget::hideEvent(e);
}

void FindPanel::refresh() {
    if (!m_view) return;
    ChartScene* s = m_view->chart();
    const auto matches = s->findMatches();
    const int i = int(std::find_if(matches.begin(), matches.end(), [s](ChartItem* c) { return c->id == s->findCurrent; }) -
                      matches.begin());
    m_count->setText(s->findText.isEmpty() ? QString()
                     : matches.isEmpty()   ? tr("No results")
                                           : tr("%1 of %2").arg(i < matches.size() ? i + 1 : 0).arg(matches.size()));
    s->update();
}

void FindPanel::step(int dir) {
    if (!m_view) return;
    ChartScene* s = m_view->chart();
    s->findText = m_find->text();
    const auto matches = s->findMatches();
    ChartItem* cur = nullptr;
    if (!matches.isEmpty()) {
        const int n = int(matches.size());
        int i = int(std::find_if(matches.begin(), matches.end(), [s](ChartItem* c) { return c->id == s->findCurrent; }) -
                    matches.begin());
        if (i == n) i = dir < 0 ? n - 1 : 0;  // current gone (or none yet): start at an end
        else i = (i + dir + n) % n;
        cur = matches[i];
        m_view->centerOn(cur);  // match in the middle of the screen
    }
    s->findCurrent = cur ? cur->id : 0;
    refresh();
}

void FindPanel::replace(bool all) {
    if (!m_view) return;
    ChartScene* s = m_view->chart();
    if (s->findText.isEmpty()) return;
    if (all) {
        const int n = s->replaceText(s->findMatches(), m_replace->text());
        refresh();
        m_count->setText(tr("%n replaced", nullptr, n));
        return;
    }
    const auto matches = s->findMatches();
    const auto i = std::find_if(matches.begin(), matches.end(), [s](ChartItem* c) { return c->id == s->findCurrent; });
    if (i == matches.end()) return step(1);  // nothing current yet: first show the user what would change
    ChartItem* c = *i;
    const int next = matches.size() > 1 ? (i + 1 == matches.end() ? matches.first() : *(i + 1))->id : 0;
    s->replaceText({c}, m_replace->text());
    if (c->text.contains(s->findText, s->findCase)) return step(1);
    s->findCurrent = next;  // c no longer matches: move on to the match after it
    step(0);
}
