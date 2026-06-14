#include "SettingsDialog.h"

#include <rtaudio/RtAudio.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace keypiano::ui {

SettingsDialog::SettingsDialog(
    const keypiano::audio::AudioEngine::Config& current,
    QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Audio Settings"));
    setMinimumWidth(380);

    device_combo_ = new QComboBox(this);
    device_combo_->addItem(tr("System Default"));
    device_names_ << QString();  // index 0 = default (empty string)

    // Enumerate WASAPI output devices.
    try {
        RtAudio tmp(RtAudio::WINDOWS_WASAPI);
        for (unsigned int id : tmp.getDeviceIds()) {
            RtAudio::DeviceInfo info = tmp.getDeviceInfo(id);
            if (info.outputChannels > 0) {
                QString name = QString::fromStdString(info.name);
                device_combo_->addItem(name);
                device_names_ << name;
            }
        }
    } catch (...) {
        // If enumeration fails we just show System Default.
    }

    // Select the current device
    {
        QString cur = QString::fromStdString(current.device_name);
        int idx = device_names_.indexOf(cur);
        device_combo_->setCurrentIndex(idx >= 0 ? idx : 0);
    }

    rate_combo_ = new QComboBox(this);
    for (const char* r : {"44100", "48000", "96000"})
        rate_combo_->addItem(r);
    rate_combo_->setCurrentText(QString::number(current.sample_rate));
    if (rate_combo_->currentIndex() < 0) rate_combo_->setCurrentIndex(0);

    frames_combo_ = new QComboBox(this);
    for (const char* f : {"128", "256", "512", "1024"})
        frames_combo_->addItem(f);
    frames_combo_->setCurrentText(QString::number(current.buffer_frames));
    if (frames_combo_->currentIndex() < 0) frames_combo_->setCurrentIndex(1);

    auto* note = new QLabel(
        tr("Changes take effect immediately (audio will briefly drop out)."),
        this);
    note->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* form = new QFormLayout;
    form->addRow(tr("Output Device:"), device_combo_);
    form->addRow(tr("Sample Rate (Hz):"), rate_combo_);
    form->addRow(tr("Buffer Frames:"), frames_combo_);

    auto* vlay = new QVBoxLayout(this);
    vlay->addLayout(form);
    vlay->addWidget(note);
    vlay->addWidget(buttons);
}

keypiano::audio::AudioEngine::Config SettingsDialog::config() const {
    keypiano::audio::AudioEngine::Config cfg;
    int idx = device_combo_->currentIndex();
    if (idx >= 0 && idx < device_names_.size())
        cfg.device_name = device_names_[idx].toStdString();
    cfg.sample_rate   = static_cast<uint32_t>(rate_combo_->currentText().toUInt());
    cfg.buffer_frames = static_cast<uint32_t>(frames_combo_->currentText().toUInt());
    return cfg;
}

} // namespace keypiano::ui
