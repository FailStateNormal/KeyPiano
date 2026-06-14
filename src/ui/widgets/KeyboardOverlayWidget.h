#ifndef KEYPIANO_UI_WIDGETS_KEYBOARDOVERLAYWIDGET_H_
#define KEYPIANO_UI_WIDGETS_KEYBOARDOVERLAYWIDGET_H_

#include <QHash>
#include <QString>
#include <QWidget>

#include "keymap/KeyMap.h"

namespace keypiano::ui {
class PianoWidget;

// Transparent overlay widget that renders keyboard-binding labels on top of
// PianoWidget.  Must be constructed with a PianoWidget* as its Qt parent.
//
// All methods must be called from the UI thread.
class KeyboardOverlayWidget : public QWidget {
    Q_OBJECT

public:
    explicit KeyboardOverlayWidget(PianoWidget* piano);

    // Populate labels from KeyMap Note bindings (uses KeyBinding::label).
    void updateFromKeyMap(const keypiano::KeyMap& km);

    void clearLabels();

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    PianoWidget*        piano_;
    QHash<int, QString> labels_;  // midi note [21-108] -> overlay text
};

} // namespace keypiano::ui

#endif // KEYPIANO_UI_WIDGETS_KEYBOARDOVERLAYWIDGET_H_
