#ifndef KEYPIANO_UI_DIALOGS_SOUNDFONTDIALOG_H_
#define KEYPIANO_UI_DIALOGS_SOUNDFONTDIALOG_H_

#include <QDialog>
#include <QString>

QT_BEGIN_NAMESPACE
class QDialogButtonBox;
class QLabel;
class QListWidget;
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

    // Returns the path chosen by the user (non-empty only after Accepted).
    QString selectedPath() const { return selected_path_; }

private slots:
    void browse();
    void onRecentActivated();
    void updateOkButton();

private:
    void loadRecents();
    void saveRecents();
    void applyPath(const QString& path);

    QListWidget*     recents_list_  = nullptr;
    QLabel*          path_label_    = nullptr;
    QDialogButtonBox* buttons_      = nullptr;

    QString selected_path_;
    QStringList recents_;
};

} // namespace keypiano::ui

#endif // KEYPIANO_UI_DIALOGS_SOUNDFONTDIALOG_H_
