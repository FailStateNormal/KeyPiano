#ifndef KEYPIANO_UI_WIDGETS_PIANOWIDGET_H_
#define KEYPIANO_UI_WIDGETS_PIANOWIDGET_H_

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace keypiano::ui {

// QPainter-drawn 88-key piano (MIDI 21-108).
//
// Visual model:
//   - 52 white keys, each ww = widget_width/52 wide, full height
//   - 36 black keys, width 55% ww, height 60% of widget
//   - Pressed: white → #6BB5FF, black → #2255AA
//   - Released: 0.1-0.25 s fade-out (alpha -= 17 per 16 ms tick)
//   - Optional per-key label (key name or note name) from KeyMap
//
// Threading: all methods must be called from the UI thread.
class PianoWidget : public QWidget {
    Q_OBJECT

public:
    explicit PianoWidget(QWidget* parent = nullptr);

    // Set/clear the overlay label for a key (shown in the key body).
    void setKeyLabel(int midi_note, const QString& label);
    void clearLabels();

    // Soft-release all held notes (call before engine shuts down).
    void releaseAll();

    QSize minimumSizeHint() const override;
    QSize sizeHint()        const override;

    // Returns key bounding rect in widget-local coordinates, or null QRect if
    // midi_note is outside [21, 108].  Used by KeyboardOverlayWidget.
    QRect keyRect(int midi_note) const;

    // Rebind mode: a left-click selects a key for rebinding (emitting
    // keyClickedForRebind) instead of playing a note.
    void setRebindMode(bool on);

    // Highlight the key currently selected for rebinding (-1 clears).
    void setSelectedKey(int midi_note);

signals:
    // Emitted on mouse press/release so MainWindow can forward to AudioEngine.
    void mouseNoteOn(int midi_note, int velocity);
    void mouseNoteOff(int midi_note);

    // Emitted when a key is clicked while in rebind mode.
    void keyClickedForRebind(int midi_note);

public slots:
    void onNoteActivated(int midi_note);
    void onNoteReleased(int midi_note);   // midi_note == -1 means AllNotesOff

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private slots:
    void tick();

private:
    struct KeyGeom {
        bool is_black;
        int  white_idx; // white keys: absolute [0,51]; black keys: left neighbour
    };

    void   buildGeometry();
    void   updateRects();
    int    hitTest(QPoint pos) const;    // returns MIDI note 21-108 or -1
    QColor keyColor(int midi) const;

    KeyGeom        geom_[88];
    QVector<QRect> rects_;             // one rect per note, index = midi-21

    QSet<int>    pressed_;             // MIDI notes currently held (full highlight)
    QHash<int,int> fading_;            // MIDI note → fade alpha [1,255]

    QHash<int, QString> labels_;       // MIDI note → overlay label

    QTimer* anim_timer_;
    int     mouse_note_ = -1;          // MIDI note held by mouse, or -1

    bool rebind_mode_   = false;       // clicks select-for-rebind instead of play
    int  selected_note_ = -1;          // key highlighted for rebinding, or -1
};

} // namespace keypiano::ui

#endif // KEYPIANO_UI_WIDGETS_PIANOWIDGET_H_
