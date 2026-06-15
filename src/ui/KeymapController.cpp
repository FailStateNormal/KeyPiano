#include "KeymapController.h"

#include <fstream>
#include <sstream>
#include <vector>

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QRegularExpression>
#include <QStandardPaths>

#include "dialogs/KeyMapEditorDialog.h"
#include "keymap/KeyMapParser.h"
#include "keymap/KeyMapSerializer.h"
#include "widgets/KeyboardOverlayWidget.h"
#include "widgets/PianoWidget.h"

namespace keypiano::ui {

namespace {
// MIDI note number → human label like "C3" / "Eb4" (for status messages).
QString midiNoteLabel(int note) {
    static const char* n[] = {"C","C#","D","Eb","E","F","F#","G","Ab","A","Bb","B"};
    if (note < 0 || note > 127) return QStringLiteral("?");
    return QStringLiteral("%1%2").arg(n[note % 12]).arg(note / 12 - 1);
}
}  // namespace

KeymapController::KeymapController(QWidget* dialog_parent, QObject* parent)
    : QObject(parent), dialog_parent_(dialog_parent) {}

void KeymapController::setWidgets(PianoWidget* piano,
                                  KeyboardOverlayWidget* overlay) {
    piano_   = piano;
    overlay_ = overlay;
}

void KeymapController::setActions(QAction* edit_labels, QAction* rebind,
                                  QAction* clear, QMenu* preset_menu) {
    act_edit_    = edit_labels;
    act_rebind_  = rebind;
    act_clear_   = clear;
    preset_menu_ = preset_menu;
}

// ── Snapshot publishing ─────────────────────────────────────────────────────────

void KeymapController::publishKeymap() {
    keymap_ptr_.store(std::make_shared<const KeyMap>(keymap_),
                      std::memory_order_release);
}

void KeymapController::setActiveKeymap(KeyMap km) {
    keymap_ = std::move(km);
    publishKeymap();  // hand the input thread a fresh immutable snapshot
    if (overlay_) overlay_->updateFromKeyMap(keymap_);
    if (act_edit_)   act_edit_->setEnabled(true);
    if (act_rebind_) act_rebind_->setEnabled(true);
    if (act_clear_)  act_clear_->setEnabled(true);
}

// ── Loading ─────────────────────────────────────────────────────────────────────

void KeymapController::loadDefault() {
    QFile f(":/keymaps/default.map");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    const QByteArray map_text = f.readAll();

    auto result = KeyMapParser::parse(map_text.toStdString());
    if (!result.errors.empty()) {
        // The bundled default map should always parse cleanly; surface any
        // regression instead of silently shipping a broken default.
        QString msg;
        for (const auto& e : result.errors)
            msg += QString::fromStdString(e) + "\n";
        QMessageBox::warning(dialog_parent_, tr("Default Keymap"),
                             tr("Bundled default keymap has issues:\n%1")
                                 .arg(msg.trimmed()));
    }
    setActiveKeymap(std::move(result.map));
}

void KeymapController::loadStartup() {
    // Prefer the user's saved customisations from a previous session; fall back
    // to the bundled default if none exists or it fails to parse cleanly.
    const QString user = userKeymapPath();
    if (QFile::exists(user)) {
        std::ifstream f(user.toStdString());
        if (f) {
            std::ostringstream ss;
            ss << f.rdbuf();
            auto result = KeyMapParser::parse(ss.str());
            if (result.errors.empty()) {
                setActiveKeymap(std::move(result.map));
                return;
            }
        }
    }
    loadDefault();
}

// ── Persistence paths ───────────────────────────────────────────────────────────

QString KeymapController::userKeymapPath() const {
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir + "/user.map";
}

QString KeymapController::presetsDir() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/presets";
}

void KeymapController::saveUserKeymap() {
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    const std::string text = KeyMapSerializer::serialize(keymap_);
    QFile f(userKeymapPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Text))
        f.write(text.data(), static_cast<qint64>(text.size()));
}

// ── Open / edit ─────────────────────────────────────────────────────────────────

void KeymapController::openKeymap() {
    QString path = QFileDialog::getOpenFileName(
        dialog_parent_, tr("Open Keymap"), {},
        tr("Keymap files (*.map);;All files (*)"));
    if (path.isEmpty()) return;

    std::ifstream f(path.toStdString());
    if (!f) {
        QMessageBox::critical(dialog_parent_, tr("Error"),
                              tr("Cannot read keymap:\n%1").arg(path));
        return;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    auto result = KeyMapParser::parse(ss.str());
    if (!result.errors.empty()) {
        QString msg;
        for (const auto& e : result.errors)
            msg += QString::fromStdString(e) + "\n";
        QMessageBox::warning(dialog_parent_, tr("Keymap Warnings"), msg.trimmed());
    }
    setActiveKeymap(std::move(result.map));
    emit status(tr("Loaded keymap: %1").arg(QFileInfo(path).fileName()), 3000);
}

void KeymapController::editLabels() {
    KeyMapEditorDialog dlg(keymap_, dialog_parent_);
    if (dlg.exec() != QDialog::Accepted) return;
    setActiveKeymap(dlg.keyMap());
    emit status(tr("Keymap labels updated."), 2000);
}

// ── Rebind ──────────────────────────────────────────────────────────────────────

void KeymapController::toggleRebind(bool on) {
    // Rebind and Clear are mutually exclusive modes.
    if (on && act_clear_ && act_clear_->isChecked())
        act_clear_->setChecked(false);  // → toggleClear(false)

    rebind_mode_ = on;
    rebind_armed_.store(false, std::memory_order_release);
    rebind_note_ = -1;
    if (piano_) {
        piano_->setRebindMode(on);
        piano_->setSelectedKey(-1);
    }
    if (on)
        emit status(
            tr("Rebind: click a piano key, then press the keyboard key to assign."),
            0);
    else
        emit status(QString(), 0);  // clear
}

void KeymapController::toggleClear(bool on) {
    // Rebind and Clear are mutually exclusive modes.
    if (on && act_rebind_ && act_rebind_->isChecked())
        act_rebind_->setChecked(false);  // → toggleRebind(false)

    clear_mode_.store(on, std::memory_order_release);
    if (on)
        emit status(
            tr("Clear mode: press any mapped key to remove its binding."), 0);
    else
        emit status(QString(), 0);  // clear
}

void KeymapController::onRebindKeyClicked(int midi_note) {
    if (!rebind_mode_) return;
    rebind_note_ = midi_note;
    rebind_armed_.store(true, std::memory_order_release);
    if (piano_) piano_->setSelectedKey(midi_note);
    emit status(
        tr("Press a keyboard key to bind to %1...").arg(midiNoteLabel(midi_note)),
        0);
}

bool KeymapController::tryCaptureRebind(uint32_t vk_code) {
    if (!rebind_armed_.exchange(false, std::memory_order_acq_rel)) return false;
    // Marshal back to the UI thread — keymap_ must only be mutated there.
    QMetaObject::invokeMethod(
        this, [this, vk_code] { applyRebind(vk_code); }, Qt::QueuedConnection);
    return true;
}

bool KeymapController::tryCaptureClear(uint32_t vk_code) {
    if (!clear_mode_.load(std::memory_order_acquire)) return false;
    // Swallow the key (no note plays) and remove its binding on the UI thread.
    // The mode stays on so several keys can be cleared in a row.
    QMetaObject::invokeMethod(
        this, [this, vk_code] { applyClear(vk_code); }, Qt::QueuedConnection);
    return true;
}

void KeymapController::applyClear(uint32_t vk_code) {
    const QString key =
        QString::fromStdString(KeyMapSerializer::keyName(vk_code));
    if (!keymap_.find(vk_code)) {
        emit status(tr("%1 is not bound.").arg(key), 2000);
        return;
    }
    keymap_.removeBinding(vk_code);
    publishKeymap();  // republish so the hook stops sounding the cleared key
    if (overlay_) overlay_->updateFromKeyMap(keymap_);
    saveUserKeymap();
    emit status(tr("Cleared binding for %1.").arg(key), 3000);
}

void KeymapController::applyRebind(uint32_t vk_code) {
    if (rebind_note_ < 0) return;
    const uint8_t note = static_cast<uint8_t>(rebind_note_);

    // Move semantics: drop any existing channel-0 Note binding for this note so
    // it relocates to the new key instead of sounding from two keys at once.
    std::vector<uint32_t> stale;
    for (const auto& b : keymap_.bindings()) {
        if (b.action == KeyAction::Note && b.channel == 0 &&
            b.midi_note == note && b.vk_code != vk_code)
            stale.push_back(b.vk_code);
    }
    for (uint32_t vk : stale) keymap_.removeBinding(vk);

    // Bind the captured physical key to the note (overwrites its old action).
    KeyBinding kb;
    kb.vk_code   = vk_code;
    kb.action    = KeyAction::Note;
    kb.channel   = 0;
    kb.midi_note = note;
    kb.label     = KeyMapSerializer::keyName(vk_code);
    keymap_.addBinding(kb);

    publishKeymap();  // republish the snapshot so the hook sees the new binding
    if (overlay_) overlay_->updateFromKeyMap(keymap_);
    if (piano_) piano_->setSelectedKey(-1);
    rebind_note_ = -1;

    saveUserKeymap();
    emit status(
        tr("Bound %1 -> %2")
            .arg(QString::fromStdString(KeyMapSerializer::keyName(vk_code)))
            .arg(midiNoteLabel(note)),
        3000);
}

void KeymapController::resetToDefault() {
    if (QMessageBox::question(
            dialog_parent_, tr("Reset Keymap"),
            tr("Discard your custom key bindings and restore the default layout?"))
        != QMessageBox::Yes)
        return;

    if (act_rebind_ && act_rebind_->isChecked())
        act_rebind_->setChecked(false);  // exit rebind
    QFile::remove(userKeymapPath());
    loadDefault();
    emit status(tr("Keymap reset to default."), 3000);
}

// ── Presets ─────────────────────────────────────────────────────────────────────

void KeymapController::rebuildPresetMenu() {
    if (!preset_menu_) return;
    preset_menu_->clear();

    // Saved presets first (each loads itself on click), then management actions.
    QDir dir(presetsDir());
    const QStringList files = dir.entryList({"*.map"}, QDir::Files, QDir::Name);
    if (files.isEmpty()) {
        QAction* none = preset_menu_->addAction(tr("(no saved presets)"));
        none->setEnabled(false);
    } else {
        for (const QString& f : files) {
            const QString name = QFileInfo(f).completeBaseName();
            QAction* a = preset_menu_->addAction(name);
            connect(a, &QAction::triggered, this,
                    [this, name] { loadPreset(name); });
        }
    }

    preset_menu_->addSeparator();
    QAction* save = preset_menu_->addAction(tr("Save Current As..."));
    connect(save, &QAction::triggered, this, &KeymapController::savePreset);
    QAction* del = preset_menu_->addAction(tr("Delete Preset..."));
    connect(del, &QAction::triggered, this, &KeymapController::deletePreset);
}

void KeymapController::savePreset() {
    bool ok = false;
    const QString name = QInputDialog::getText(
        dialog_parent_, tr("Save Keymap Preset"), tr("Preset name:"),
        QLineEdit::Normal, {}, &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    // Keep the name filesystem-safe (it becomes <name>.map).
    QString safe = name.trimmed();
    safe.replace(QRegularExpression(R"([\\/:*?"<>|])"), "_");

    QDir().mkpath(presetsDir());
    const QString path = presetsDir() + "/" + safe + ".map";
    if (QFile::exists(path) &&
        QMessageBox::question(
            dialog_parent_, tr("Overwrite Preset"),
            tr("A preset named \"%1\" already exists. Overwrite it?").arg(safe))
            != QMessageBox::Yes)
        return;

    const std::string text = KeyMapSerializer::serialize(keymap_);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(dialog_parent_, tr("Error"),
                              tr("Could not write preset:\n%1").arg(path));
        return;
    }
    f.write(text.data(), static_cast<qint64>(text.size()));
    f.close();

    rebuildPresetMenu();
    emit status(tr("Saved preset: %1").arg(safe), 3000);
}

void KeymapController::loadPreset(const QString& name) {
    const QString path = presetsDir() + "/" + name + ".map";
    std::ifstream f(path.toStdString());
    if (!f) {
        QMessageBox::critical(dialog_parent_, tr("Error"),
                              tr("Cannot read preset:\n%1").arg(path));
        rebuildPresetMenu();  // it may have been deleted externally
        return;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    auto result = KeyMapParser::parse(ss.str());
    if (!result.errors.empty()) {
        QString msg;
        for (const auto& e : result.errors)
            msg += QString::fromStdString(e) + "\n";
        QMessageBox::warning(dialog_parent_, tr("Preset Warnings"), msg.trimmed());
    }
    setActiveKeymap(std::move(result.map));
    // Loading a preset becomes the active layout; persist it as user.map so it
    // survives a restart (matching the rebind auto-save behaviour).
    saveUserKeymap();
    emit status(tr("Loaded preset: %1").arg(name), 3000);
}

void KeymapController::deletePreset() {
    QDir dir(presetsDir());
    const QStringList files = dir.entryList({"*.map"}, QDir::Files, QDir::Name);
    if (files.isEmpty()) {
        QMessageBox::information(dialog_parent_, tr("Delete Preset"),
                                 tr("There are no saved presets."));
        return;
    }
    QStringList names;
    for (const QString& f : files) names << QFileInfo(f).completeBaseName();

    bool ok = false;
    const QString name = QInputDialog::getItem(
        dialog_parent_, tr("Delete Preset"), tr("Preset to delete:"), names, 0,
        /*editable=*/false, &ok);
    if (!ok || name.isEmpty()) return;

    if (QMessageBox::question(
            dialog_parent_, tr("Delete Preset"),
            tr("Delete preset \"%1\"? This cannot be undone.").arg(name))
        != QMessageBox::Yes)
        return;

    QFile::remove(presetsDir() + "/" + name + ".map");
    rebuildPresetMenu();
    emit status(tr("Deleted preset: %1").arg(name), 3000);
}

}  // namespace keypiano::ui
