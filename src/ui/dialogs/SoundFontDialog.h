#ifndef KEYPIANO_UI_DIALOGS_SOUNDFONTDIALOG_H_
#define KEYPIANO_UI_DIALOGS_SOUNDFONTDIALOG_H_

#include <QDialog>
#include <QString>

QT_BEGIN_NAMESPACE
class QDialogButtonBox;
class QLabel;
class QListWidget;
class QPushButton;
QT_END_NAMESPACE

namespace keypiano::ui {

// Modal dialog for selecting a .sf2 SoundFont file.
// Shows up to 10 recently used paths (persisted via QSettings).
// Double-clicking a recent entry or clicking Browse selects a path.
class SoundFontDialog : public QDialog {
    Q_OBJECT

public:
    explicit SoundFontDialog(QWidget* parent = nullptr);

    // Pre-populate the selected path (e.g., the currently loaded SF2).
    void setInitialPath(const QString& path);

    // Pin a non-removable "built-in default" entry at the top of the list, so the
    // user can always get back to the bundled sound after switching away. `label`
    // is the display name; `path` is the real file selecting it returns.
    void setBuiltinDefault(const QString& path, const QString& label);

    // Returns the path chosen by the user (non-empty only after Accepted).
    QString selectedPath() const { return selected_path_; }

private slots:
    void browse();
    void onRecentActivated();
    void onRecentSelectionChanged();
    void removeSelectedRecent();
    void clearRecents();
    void updateOkButton();

private:
    void loadRecents();
    void saveRecents();
    void refreshList();          // rebuild the list widget from recents_
    // `remember == false` selects the path without adding it to the recent list
    // (used for the built-in default, which is pinned separately).
    void applyPath(const QString& path, bool remember = true);

    QListWidget*     recents_list_  = nullptr;
    QLabel*          path_label_    = nullptr;
    QPushButton*     remove_btn_    = nullptr;
    QPushButton*     clear_btn_     = nullptr;
    QDialogButtonBox* buttons_      = nullptr;

    QString selected_path_;
    QStringList recents_;
    QString builtin_path_;   // bundled default, pinned + non-removable (may be empty)
    QString builtin_label_;
};

} // namespace keypiano::ui

#endif // KEYPIANO_UI_DIALOGS_SOUNDFONTDIALOG_H_
