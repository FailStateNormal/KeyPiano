#ifndef KEYPIANO_UI_DIALOGS_SETTINGSDIALOG_H_
#define KEYPIANO_UI_DIALOGS_SETTINGSDIALOG_H_

#include <QDialog>
#include <QStringList>

#include "audio/AudioEngine.h"

QT_BEGIN_NAMESPACE
class QComboBox;
QT_END_NAMESPACE

namespace keypiano::ui {

// Modal dialog for audio output settings.
// Enumerates WASAPI output devices and lets the user change
// device, sample rate, and buffer size.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(const keypiano::audio::AudioEngine::Config& current,
                            QWidget* parent = nullptr);

    // Returns the config described by the current widget state.
    keypiano::audio::AudioEngine::Config config() const;

private:
    QComboBox* device_combo_  = nullptr;
    QComboBox* rate_combo_    = nullptr;
    QComboBox* frames_combo_  = nullptr;

    // device_names_[i] corresponds to device_combo_ item i (empty = default).
    QStringList device_names_;
};

} // namespace keypiano::ui

#endif // KEYPIANO_UI_DIALOGS_SETTINGSDIALOG_H_
