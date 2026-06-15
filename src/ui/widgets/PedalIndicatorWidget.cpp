#include "widgets/PedalIndicatorWidget.h"

#include <QFont>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QSizePolicy>

namespace keypiano::ui {

constexpr int PedalIndicatorWidget::kCc[3];

PedalIndicatorWidget::PedalIndicatorWidget(QWidget* parent) : QWidget(parent) {
    // A fixed-height strip across the bottom: stretch horizontally, never get
    // squeezed vertically by the Expanding piano above it.
    setFixedHeight(58);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAutoFillBackground(true);
    setToolTip(tr("Pedals — light up while held. In Rebind mode, click a pedal "
                  "then press a key to assign it."));
}

QSize PedalIndicatorWidget::sizeHint() const { return QSize(400, 58); }

int PedalIndicatorWidget::indexForCc(int cc) {
    for (int i = 0; i < 3; ++i)
        if (kCc[i] == cc) return i;
    return -1;
}

void PedalIndicatorWidget::setPedalState(int cc, bool on) {
    int i = indexForCc(cc);
    if (i < 0 || on_[i] == on) return;
    on_[i] = on;
    update();
}

void PedalIndicatorWidget::setPedalKey(int cc, const QString& key) {
    int i = indexForCc(cc);
    if (i < 0 || key_[i] == key) return;
    key_[i] = key;
    update();
}

void PedalIndicatorWidget::setRebindMode(bool on) {
    rebind_mode_ = on;
    update();
}

void PedalIndicatorWidget::setSelectedPedal(int cc) {
    int i = (cc < 0) ? -1 : indexForCc(cc);
    if (selected_ == i) return;
    selected_ = i;
    update();
}

void PedalIndicatorWidget::paintEvent(QPaintEvent*) {
    const QString names[3] = {tr("Sustain"), tr("Sostenuto"), tr("Soft")};

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(0x33, 0x33, 0x33));  // match the piano's dark frame

    const int w = width() / 3;
    const int h = height();
    for (int i = 0; i < 3; ++i) {
        const QRect cell(i * w, 0, (i == 2 ? width() - 2 * w : w), h);
        const QRect inner = cell.adjusted(6, 6, -6, -6);

        // Cell background + (orange) highlight when armed for rebinding.
        p.setPen(selected_ == i ? QPen(QColor(0xFF, 0x8C, 0x00), 2)
                                : QPen(QColor(0x55, 0x55, 0x55), 1));
        p.setBrush(QColor(0x2B, 0x2B, 0x2B));
        p.drawRoundedRect(inner, 5, 5);

        // Lamp.
        const int d = 14;
        const QRect lamp(inner.center().x() - d / 2, inner.top() + 6, d, d);
        p.setPen(Qt::NoPen);
        p.setBrush(on_[i] ? QColor(0x6B, 0xD3, 0x7A)   // lit green
                          : QColor(0x44, 0x44, 0x44));  // dim
        p.drawEllipse(lamp);

        // Pedal name.
        p.setPen(QColor(0xDD, 0xDD, 0xDD));
        QFont f = p.font();
        f.setPointSizeF(f.pointSizeF() - 0.5);
        p.setFont(f);
        const QRect name_rect(inner.left(), lamp.bottom() + 1,
                              inner.width(), 14);
        p.drawText(name_rect, Qt::AlignHCenter | Qt::AlignVCenter, names[i]);

        // Bound key (or "(none)").
        p.setPen(key_[i].isEmpty() ? QColor(0x99, 0x66, 0x66)
                                   : QColor(0x99, 0x99, 0x99));
        const QString key = key_[i].isEmpty() ? tr("(none)") : key_[i];
        const QRect key_rect(inner.left(), name_rect.bottom(),
                             inner.width(), inner.bottom() - name_rect.bottom());
        p.drawText(key_rect, Qt::AlignHCenter | Qt::AlignVCenter, key);
    }
}

void PedalIndicatorWidget::mousePressEvent(QMouseEvent* event) {
    if (!rebind_mode_) { QWidget::mousePressEvent(event); return; }
    const int w = width() / 3;
    const int x = static_cast<int>(event->position().x());
    int i = (w > 0) ? (x / w) : 0;
    if (i < 0) i = 0;
    if (i > 2) i = 2;
    emit pedalClickedForRebind(kCc[i]);
}

}  // namespace keypiano::ui
