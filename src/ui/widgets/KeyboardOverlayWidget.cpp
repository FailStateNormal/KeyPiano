#include "KeyboardOverlayWidget.h"
#include "PianoWidget.h"

#include <QEvent>
#include <QFont>
#include <QPainter>

namespace keypiano::ui {

static constexpr bool PC_IS_BLACK[12] = {
    false, true,  false, true,  false,
    false, true,  false, true,  false, true, false
};

KeyboardOverlayWidget::KeyboardOverlayWidget(PianoWidget* piano)
    : QWidget(piano), piano_(piano) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    piano_->installEventFilter(this);
    resize(piano_->size());
    raise();
}

void KeyboardOverlayWidget::updateFromKeyMap(const keypiano::KeyMap& km) {
    labels_.clear();
    for (const auto& b : km.bindings()) {
        if (b.action != keypiano::KeyAction::Note) continue;
        if (b.midi_note < 21 || b.midi_note > 108) continue;
        if (!b.label.empty())
            labels_[b.midi_note] = QString::fromStdString(b.label);
    }
    update();
}

void KeyboardOverlayWidget::clearLabels() {
    labels_.clear();
    update();
}

bool KeyboardOverlayWidget::eventFilter(QObject* obj, QEvent* event) {
    if (obj == piano_ && event->type() == QEvent::Resize) {
        resize(piano_->size());
        raise();
    }
    return false;
}

void KeyboardOverlayWidget::paintEvent(QPaintEvent*) {
    if (labels_.isEmpty()) return;

    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    for (auto it = labels_.constBegin(); it != labels_.constEnd(); ++it) {
        const int   midi     = it.key();
        const QRect r        = piano_->keyRect(midi);
        if (r.isNull()) continue;

        const bool is_black = PC_IS_BLACK[midi % 12];
        int   fs;
        QRect text_rect;

        if (!is_black) {
            p.setPen(QColor(0x22, 0x22, 0x22, 220));
            fs        = qMax(6, r.width() * 2 / 5);
            text_rect = QRect(r.left(),
                              r.bottom() - r.height() / 4,
                              r.width(),
                              r.height() / 4);
        } else {
            p.setPen(QColor(0xFF, 0xFF, 0xFF, 220));
            fs        = qMax(5, r.width() * 2 / 3);
            text_rect = QRect(r.left(),
                              r.top() + r.height() / 2,
                              r.width(),
                              r.height() / 2);
        }

        p.setFont(QFont("Arial", fs));
        p.drawText(text_rect, Qt::AlignHCenter | Qt::AlignVCenter, it.value());
    }
}

} // namespace keypiano::ui
