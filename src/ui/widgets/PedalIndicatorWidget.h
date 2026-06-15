#ifndef KEYPIANO_UI_WIDGETS_PEDALINDICATORWIDGET_H_
#define KEYPIANO_UI_WIDGETS_PEDALINDICATORWIDGET_H_

#include <QString>
#include <QWidget>

namespace keypiano::ui {

// A strip of three pedal lamps (Sustain / Sostenuto / Soft) shown under the
// piano. Each lamp lights up while its pedal is held, and shows which keyboard
// key is currently bound to it ("(none)" if unbound). In rebind mode a lamp can
// be clicked to arm it; MainWindow then captures the next key press and binds it
// to that pedal — the pedal analogue of clicking a piano key to rebind a note.
class PedalIndicatorWidget : public QWidget {
    Q_OBJECT

public:
    explicit PedalIndicatorWidget(QWidget* parent = nullptr);

    QSize sizeHint() const override;

    // CC is the MIDI controller number: 64 sustain, 66 sostenuto, 67 soft.
    void setPedalState(int cc, bool on);          // lamp on/off (pedal held?)
    void setPedalKey(int cc, const QString& key); // bound key label ("" = none)

    void setRebindMode(bool on);
    void setSelectedPedal(int cc);                // armed lamp (-1 = none)

signals:
    // A lamp was clicked while in rebind mode (cc = 64/66/67).
    void pedalClickedForRebind(int cc);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    static constexpr int kCc[3] = {64, 66, 67};
    static int indexForCc(int cc);  // -1 if cc is not a pedal

    bool    on_[3]  = {false, false, false};
    QString key_[3];                 // bound key name, empty if none
    bool    rebind_mode_ = false;
    int     selected_    = -1;       // armed lamp index, -1 if none
};

}  // namespace keypiano::ui

#endif  // KEYPIANO_UI_WIDGETS_PEDALINDICATORWIDGET_H_
