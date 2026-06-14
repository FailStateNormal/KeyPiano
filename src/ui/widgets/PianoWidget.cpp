#include "PianoWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QTimer>

namespace keypiano::ui {

// ── Black-key pitch-class lookup ──────────────────────────────────────────────

static constexpr bool PC_IS_BLACK[12] = {
    false, true,  false, true,  false,
    false, true,  false, true,  false, true,  false
};

// ── Constructor ───────────────────────────────────────────────────────────────

PianoWidget::PianoWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumSize(minimumSizeHint());
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // Dark background shows between keys if rounding leaves tiny gaps.
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0x33, 0x33, 0x33));
    setPalette(pal);

    anim_timer_ = new QTimer(this);
    anim_timer_->setInterval(16);
    connect(anim_timer_, &QTimer::timeout, this, &PianoWidget::tick);

    buildGeometry();
    rects_.resize(88);
}

QSize PianoWidget::minimumSizeHint() const { return {52 * 10, 80}; }
QSize PianoWidget::sizeHint()        const { return {52 * 20, 160}; }

QRect PianoWidget::keyRect(int midi_note) const {
    if (midi_note < 21 || midi_note > 108) return {};
    return rects_[midi_note - 21];
}

// ── Geometry ──────────────────────────────────────────────────────────────────

void PianoWidget::buildGeometry() {
    int white_count = 0;
    for (int i = 0; i < 88; i++) {
        int  pc       = (21 + i) % 12;
        bool is_black = PC_IS_BLACK[pc];
        geom_[i].is_black  = is_black;
        geom_[i].white_idx = is_black ? white_count - 1 : white_count++;
        // white_count - 1 is the left-adjacent white key for a black key.
    }
    // Postcondition: white_count == 52.
}

void PianoWidget::updateRects() {
    const int W  = width();
    const int H  = height();
    const double ww = W / 52.0;
    const int    wh = H;
    const int    bw = static_cast<int>(ww * 0.55 + 0.5);
    const int    bh = static_cast<int>(H  * 0.60 + 0.5);

    for (int i = 0; i < 88; i++) {
        if (!geom_[i].is_black) {
            int x0 = static_cast<int>(geom_[i].white_idx * ww + 0.5);
            int x1 = static_cast<int>((geom_[i].white_idx + 1) * ww + 0.5);
            rects_[i] = QRect(x0, 0, x1 - x0, wh);
        } else {
            // Centre the black key at the boundary between its two neighbours.
            int cx = static_cast<int>((geom_[i].white_idx + 1) * ww + 0.5);
            rects_[i] = QRect(cx - bw / 2, 0, bw, bh);
        }
    }
}

// ── Hit test ─────────────────────────────────────────────────────────────────

int PianoWidget::hitTest(QPoint pos) const {
    // Black keys are on top — check them first.
    for (int i = 0; i < 88; i++) {
        if (geom_[i].is_black && rects_[i].contains(pos))
            return 21 + i;
    }
    for (int i = 0; i < 88; i++) {
        if (!geom_[i].is_black && rects_[i].contains(pos))
            return 21 + i;
    }
    return -1;
}

// ── Color blending ────────────────────────────────────────────────────────────

QColor PianoWidget::keyColor(int midi) const {
    bool pressed = pressed_.contains(midi);
    int  alpha   = fading_.value(midi, 0);

    if (geom_[midi - 21].is_black) {
        if (pressed) return QColor(0x22, 0x55, 0xAA);
        if (alpha > 0) {
            // Blend #2255AA → black
            return QColor(0x22 * alpha / 255,
                          0x55 * alpha / 255,
                          0xAA * alpha / 255);
        }
        return Qt::black;
    } else {
        if (pressed) return QColor(0x6B, 0xB5, 0xFF);
        if (alpha > 0) {
            // Blend #6BB5FF → white (linear interpolation per channel)
            return QColor(255 + alpha * (0x6B - 255) / 255,
                          255 + alpha * (0xB5 - 255) / 255,
                          255); // blue channel is already 0xFF at both ends
        }
        return Qt::white;
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void PianoWidget::setKeyLabel(int midi_note, const QString& label) {
    if (midi_note < 21 || midi_note > 108) return;
    if (label.isEmpty())
        labels_.remove(midi_note);
    else
        labels_[midi_note] = label;
    update();
}

void PianoWidget::clearLabels() {
    labels_.clear();
    update();
}

void PianoWidget::releaseAll() {
    for (int m : pressed_)
        fading_[m] = 255;
    pressed_.clear();
    if (!fading_.isEmpty() && !anim_timer_->isActive())
        anim_timer_->start();
    update();
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void PianoWidget::onNoteActivated(int midi_note) {
    if (midi_note < 21 || midi_note > 108) return;
    pressed_.insert(midi_note);
    fading_.remove(midi_note);
    update();
}

void PianoWidget::onNoteReleased(int midi_note) {
    if (midi_note == -1) {
        // AllNotesOff
        for (int m : pressed_)
            fading_[m] = 255;
        pressed_.clear();
    } else {
        if (midi_note < 21 || midi_note > 108) return;
        pressed_.remove(midi_note);
        fading_[midi_note] = 255;
    }
    if (!fading_.isEmpty() && !anim_timer_->isActive())
        anim_timer_->start();
    update();
}

void PianoWidget::tick() {
    auto it = fading_.begin();
    while (it != fading_.end()) {
        it.value() -= 17;
        it = (it.value() <= 0) ? fading_.erase(it) : ++it;
    }
    if (fading_.isEmpty())
        anim_timer_->stop();
    update();
}

// ── Mouse ─────────────────────────────────────────────────────────────────────

void PianoWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    int midi = hitTest(event->pos());
    if (midi == -1) return;
    mouse_note_ = midi;
    // Immediate visual feedback without waiting for AudioBridge poll.
    pressed_.insert(midi);
    fading_.remove(midi);
    update();
    emit mouseNoteOn(midi, 80);
}

void PianoWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || mouse_note_ == -1) return;
    int midi = mouse_note_;
    mouse_note_ = -1;
    pressed_.remove(midi);
    fading_[midi] = 255;
    if (!anim_timer_->isActive())
        anim_timer_->start();
    update();
    emit mouseNoteOff(midi);
}

void PianoWidget::leaveEvent(QEvent* event) {
    if (mouse_note_ != -1) {
        int midi = mouse_note_;
        mouse_note_ = -1;
        pressed_.remove(midi);
        fading_[midi] = 255;
        if (!anim_timer_->isActive())
            anim_timer_->start();
        update();
        emit mouseNoteOff(midi);
    }
    QWidget::leaveEvent(event);
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void PianoWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const QPen border_pen(QColor(0x80, 0x80, 0x80), 1);

    // ── White keys ────────────────────────────────────────────────────────────
    for (int i = 0; i < 88; i++) {
        if (geom_[i].is_black) continue;
        const int midi = 21 + i;
        const QRect& r = rects_[i];

        p.fillRect(r, keyColor(midi));
        p.setPen(border_pen);
        p.drawRect(r.adjusted(0, 0, -1, -1));

        const auto it = labels_.constFind(midi);
        if (it != labels_.constEnd()) {
            p.setPen(Qt::darkGray);
            int fs = qMax(6, r.width() * 2 / 5);
            p.setFont(QFont("Arial", fs));
            p.drawText(
                QRect(r.left(), r.bottom() - r.height() / 4,
                      r.width(), r.height() / 4),
                Qt::AlignHCenter | Qt::AlignVCenter,
                it.value());
        }
    }

    // ── Black keys (drawn on top) ─────────────────────────────────────────────
    for (int i = 0; i < 88; i++) {
        if (!geom_[i].is_black) continue;
        const int midi = 21 + i;
        const QRect& r = rects_[i];

        p.fillRect(r, keyColor(midi));
        // Subtle highlight on top edge for depth.
        p.setPen(QColor(0x88, 0x88, 0x88));
        p.drawLine(r.left(), r.top(), r.right(), r.top());

        const auto it = labels_.constFind(midi);
        if (it != labels_.constEnd()) {
            p.setPen(Qt::white);
            int fs = qMax(5, r.width() * 2 / 3);
            p.setFont(QFont("Arial", fs));
            p.drawText(
                QRect(r.left(), r.top() + r.height() / 2,
                      r.width(), r.height() / 2),
                Qt::AlignHCenter | Qt::AlignVCenter,
                it.value());
        }
    }
}

// ── Resize ────────────────────────────────────────────────────────────────────

void PianoWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateRects();
}

} // namespace keypiano::ui
