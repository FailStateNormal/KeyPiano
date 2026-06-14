#include "KeyMapEditorDialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace keypiano::ui {

// ── Helpers ───────────────────────────────────────────────────────────────────

static const char* actionName(keypiano::KeyAction a) {
    switch (a) {
        case keypiano::KeyAction::Note:            return "Note";
        case keypiano::KeyAction::OctaveInc:       return "Octave+";
        case keypiano::KeyAction::OctaveDec:       return "Octave-";
        case keypiano::KeyAction::VelocityInc:     return "Velocity+";
        case keypiano::KeyAction::VelocityDec:     return "Velocity-";
        case keypiano::KeyAction::KeySignatureInc: return "Key+";
        case keypiano::KeyAction::KeySignatureDec: return "Key-";
        case keypiano::KeyAction::SustainSet:      return "Sustain";
        case keypiano::KeyAction::VelocitySet:     return "Vel=";
        case keypiano::KeyAction::Record:          return "Record";
        default:                                   return "?";
    }
}

static QString noteName(int midi) {
    static const char* const names[] =
        {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    int oct = midi / 12 - 1;
    return QString("%1%2 (%3)").arg(names[midi % 12]).arg(oct).arg(midi);
}

static QTableWidgetItem* roItem(const QString& text) {
    auto* it = new QTableWidgetItem(text);
    it->setFlags(it->flags() & ~Qt::ItemIsEditable);
    return it;
}

// ── Dialog ────────────────────────────────────────────────────────────────────

KeyMapEditorDialog::KeyMapEditorDialog(const keypiano::KeyMap& km,
                                       QWidget* parent)
    : QDialog(parent), keymap_(km) {
    setWindowTitle(
        tr("Keymap Editor — %1").arg(km.name.empty()
                                     ? tr("(unnamed)")
                                     : QString::fromStdString(km.name)));
    setMinimumSize(640, 420);

    auto* hint = new QLabel(
        tr("Double-click the Label cell to edit the overlay text shown on the piano."),
        this);
    hint->setWordWrap(true);

    table_ = new QTableWidget(this);
    table_->setColumnCount(6);
    table_->setHorizontalHeaderLabels(
        {tr("Label"), tr("VK Code"), tr("Action"),
         tr("Channel"), tr("MIDI Note"), tr("Step")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::DoubleClicked |
                            QAbstractItemView::SelectedClicked);

    populate();

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &KeyMapEditorDialog::apply);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* vlay = new QVBoxLayout(this);
    vlay->addWidget(hint);
    vlay->addWidget(table_, 1);
    vlay->addWidget(buttons);
}

void KeyMapEditorDialog::populate() {
    const auto& bindings = keymap_.bindings();
    table_->setRowCount(static_cast<int>(bindings.size()));

    for (int row = 0; row < static_cast<int>(bindings.size()); ++row) {
        const auto& b = bindings[row];

        // Label — editable; store vk_code in UserRole for apply()
        auto* label_item = new QTableWidgetItem(
            QString::fromStdString(b.label));
        label_item->setData(Qt::UserRole, static_cast<quint32>(b.vk_code));
        table_->setItem(row, 0, label_item);

        table_->setItem(row, 1,
            roItem(QString("0x%1").arg(b.vk_code, 4, 16, QLatin1Char('0'))
                                   .toUpper()));
        table_->setItem(row, 2, roItem(actionName(b.action)));
        table_->setItem(row, 3, roItem(QString::number(b.channel)));
        table_->setItem(row, 4,
            roItem(b.action == keypiano::KeyAction::Note
                   ? noteName(b.midi_note)
                   : tr("—")));
        table_->setItem(row, 5,
            roItem(b.action == keypiano::KeyAction::Note
                   ? tr("—")
                   : QString::number(b.step)));
    }
}

void KeyMapEditorDialog::apply() {
    for (int row = 0; row < table_->rowCount(); ++row) {
        auto* it = table_->item(row, 0);
        if (!it) continue;
        quint32 vk = it->data(Qt::UserRole).toUInt();
        keymap_.setLabel(vk, it->text().toStdString());
    }
    accept();
}

} // namespace keypiano::ui
