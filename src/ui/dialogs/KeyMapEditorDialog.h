#ifndef KEYPIANO_UI_DIALOGS_KEYMAPEDITORDIALOG_H_
#define KEYPIANO_UI_DIALOGS_KEYMAPEDITORDIALOG_H_

#include <QDialog>

#include "keymap/KeyMap.h"

QT_BEGIN_NAMESPACE
class QTableWidget;
QT_END_NAMESPACE

namespace keypiano::ui {

// Read/edit dialog for a KeyMap.  Displays all bindings in a table.
// The Label column is editable; all other columns are read-only.
// Call keyMap() after Accepted to get the modified map.
class KeyMapEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit KeyMapEditorDialog(const keypiano::KeyMap& km,
                                QWidget* parent = nullptr);

    const keypiano::KeyMap& keyMap() const { return keymap_; }

private slots:
    void apply();

private:
    void populate();

    keypiano::KeyMap  keymap_;
    QTableWidget*     table_ = nullptr;
};

} // namespace keypiano::ui

#endif // KEYPIANO_UI_DIALOGS_KEYMAPEDITORDIALOG_H_
