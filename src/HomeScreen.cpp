#include "HomeScreen.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace {
constexpr int kMaxRecent = 16;
const QSize kThumb(220, 138);

// ponytail: parses the whole file just for its thumbnail. Fine for normal charts; cache
// thumbnails next to the settings if huge files make the home screen slow.
QPixmap thumbnail(const QString& path, const QPalette& pal) {
    QImage img;
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        img.loadFromData(QByteArray::fromBase64(o["thumb"].toString().toLatin1()), "PNG");
    }
    QPixmap pm(kThumb);
    pm.fill(pal.color(QPalette::Base));
    QPainter p(&pm);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    if (!img.isNull()) {
        const QImage scaled = img.scaled(kThumb, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawImage((kThumb.width() - scaled.width()) / 2, (kThumb.height() - scaled.height()) / 2, scaled);
    } else {
        p.setPen(pal.color(QPalette::PlaceholderText));
        p.drawText(pm.rect(), Qt::AlignCenter, QObject::tr("No preview"));
    }
    p.setPen(pal.color(QPalette::Mid));
    p.drawRect(pm.rect().adjusted(0, 0, -1, -1));
    return pm;
}
}  // namespace

QStringList HomeScreen::recentFiles() { return QSettings().value("recent").toStringList(); }

void HomeScreen::addRecent(const QString& path) {
    QStringList l = recentFiles();
    l.removeAll(path);
    l.prepend(path);
    while (l.size() > kMaxRecent) l.removeLast();
    QSettings().setValue("recent", l);
}

void HomeScreen::removeRecent(const QString& path) {
    QStringList l = recentFiles();
    l.removeAll(path);
    QSettings().setValue("recent", l);
}

HomeScreen::HomeScreen(QWidget* parent) : QWidget(parent) {
    auto* title = new QLabel(tr("Flowchart"));
    QFont tf = title->font();
    tf.setPointSize(26);
    tf.setBold(true);
    title->setFont(tf);
    auto* sub = new QLabel(tr("Create a new chart or continue where you left off."));
    sub->setEnabled(false);

    auto* newBtn = new QPushButton(tr("New chart"));
    auto* openBtn = new QPushButton(tr("Open file…"));
    for (auto* b : {newBtn, openBtn}) b->setMinimumSize(140, 38);
    newBtn->setDefault(true);
    connect(newBtn, &QPushButton::clicked, this, &HomeScreen::newRequested);
    connect(openBtn, &QPushButton::clicked, this, [this] { emit openRequested(QString()); });
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(newBtn);
    buttons->addWidget(openBtn);
    buttons->addStretch();

    auto* recentLabel = new QLabel(tr("Recent charts"));
    QFont rf = recentLabel->font();
    rf.setBold(true);
    rf.setPointSizeF(rf.pointSizeF() * 1.15);
    recentLabel->setFont(rf);

    m_recent = new QListWidget;
    m_recent->setViewMode(QListView::IconMode);
    m_recent->setIconSize(kThumb);
    m_recent->setGridSize(QSize(kThumb.width() + 24, kThumb.height() + 56));
    m_recent->setResizeMode(QListView::Adjust);
    m_recent->setMovement(QListView::Static);
    m_recent->setWordWrap(true);
    m_recent->setUniformItemSizes(true);
    m_recent->setFrameShape(QFrame::NoFrame);
    m_recent->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_recent, &QListWidget::itemActivated, this,
            [this](QListWidgetItem* it) { emit openRequested(it->data(Qt::UserRole).toString()); });
    connect(m_recent, &QListWidget::customContextMenuRequested, this, [this](QPoint pos) {
        auto* it = m_recent->itemAt(pos);
        if (!it) return;
        QMenu menu;
        menu.addAction(tr("Open"), this, [this, it] { emit openRequested(it->data(Qt::UserRole).toString()); });
        menu.addAction(tr("Remove from list"), this, [this, it] {
            removeRecent(it->data(Qt::UserRole).toString());
            refresh();
        });
        menu.exec(m_recent->viewport()->mapToGlobal(pos));
    });

    m_empty = new QLabel(tr("No recent charts yet. Create one to get started."));
    m_empty->setEnabled(false);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(56, 48, 56, 32);
    lay->setSpacing(10);
    lay->addWidget(title);
    lay->addWidget(sub);
    lay->addSpacing(14);
    lay->addLayout(buttons);
    lay->addSpacing(26);
    lay->addWidget(recentLabel);
    lay->addWidget(m_empty);
    lay->addWidget(m_recent, 1);
}

void HomeScreen::refresh() {
    m_recent->clear();
    // Files that were moved or deleted drop out of the list for good.
    const QStringList all = recentFiles();
    QStringList existing;
    for (const QString& path : all)
        if (QFileInfo::exists(path)) existing << path;
    if (existing.size() != all.size()) QSettings().setValue("recent", existing);
    for (const QString& path : existing) {
        const QFileInfo fi(path);
        const QString when = fi.lastModified().toString("yyyy-MM-dd hh:mm");
        auto* it = new QListWidgetItem(QIcon(thumbnail(path, palette())), fi.completeBaseName() + "\n" + when);
        it->setData(Qt::UserRole, path);
        it->setToolTip(QDir::toNativeSeparators(path));
        m_recent->addItem(it);
    }
    m_empty->setVisible(m_recent->count() == 0);
}
