#include "SoundFontDialog.h"

#include <algorithm>

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace keypiano::ui {

static constexpr int kMaxRecents = 10;

SoundFontDialog::SoundFontDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Open SoundFont"));
    setMinimumWidth(480);

    auto* recents_label = new QLabel(tr("Recent files:"), this);

    recents_list_ = new QListWidget(this);
    recents_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(recents_list_, &QListWidget::itemActivated,
            this, &SoundFontDialog::onRecentActivated);
    connect(recents_list_, &QListWidget::itemSelectionChanged,
            this, &SoundFontDialog::onRecentSelectionChanged);

    remove_btn_ = new QPushButton(tr("Remove"), this);
    remove_btn_->setEnabled(false);
    remove_btn_->setToolTip(tr("Remove the selected entry from the recent list"));
    connect(remove_btn_, &QPushButton::clicked,
            this, &SoundFontDialog::removeSelectedRecent);

    clear_btn_ = new QPushButton(tr("Clear All"), this);
    connect(clear_btn_, &QPushButton::clicked,
            this, &SoundFontDialog::clearRecents);

    auto* browse_btn = new QPushButton(tr("Browse..."), this);
    connect(browse_btn, &QPushButton::clicked, this, &SoundFontDialog::browse);

    path_label_ = new QLabel(tr("(none selected)"), this);
    path_label_->setWordWrap(true);

    buttons_ = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* recents_btns = new QHBoxLayout;
    recents_btns->addStretch(1);
    recents_btns->addWidget(remove_btn_);
    recents_btns->addWidget(clear_btn_);

    auto* path_row = new QHBoxLayout;
    path_row->addWidget(path_label_, 1);
    path_row->addWidget(browse_btn);

    auto* vlay = new QVBoxLayout(this);
    vlay->addWidget(recents_label);
    vlay->addWidget(recents_list_, 1);
    vlay->addLayout(recents_btns);
    vlay->addLayout(path_row);
    vlay->addWidget(buttons_);

    loadRecents();
    updateOkButton();
}

void SoundFontDialog::setInitialPath(const QString& path) {
    if (!path.isEmpty()) applyPath(path);
}

void SoundFontDialog::setBuiltinDefault(const QString& path, const QString& label) {
    builtin_path_  = path;
    builtin_label_ = label;
    refreshList();
}

void SoundFontDialog::loadRecents() {
    QSettings cfg("keypiano", "keypiano");
    recents_ = cfg.value("recent_sf2").toStringList();

    // Drop entries that are not valid, existing files. A relative/bare name
    // (e.g. an old build that stored just "default_piano.sf2") or a deleted file
    // is what made selecting a recent fail with "Failed to load SoundFont".
    const int before = recents_.size();
    recents_.erase(std::remove_if(recents_.begin(), recents_.end(),
                                  [](const QString& p) {
                                      QFileInfo fi(p);
                                      return !fi.isAbsolute() || !fi.exists();
                                  }),
                   recents_.end());
    if (recents_.size() != before) saveRecents();

    refreshList();
}

void SoundFontDialog::saveRecents() {
    QSettings cfg("keypiano", "keypiano");
    cfg.setValue("recent_sf2", recents_);
}

void SoundFontDialog::refreshList() {
    recents_list_->clear();
    // The built-in default (if any) is pinned at the top and carries its real
    // path in UserRole, like every other row, so selection logic is uniform.
    if (!builtin_path_.isEmpty()) {
        auto* it = new QListWidgetItem(
            builtin_label_ + tr("  (built-in default)"));
        it->setData(Qt::UserRole, builtin_path_);
        recents_list_->addItem(it);
    }
    for (const auto& p : recents_) {
        auto* it = new QListWidgetItem(QFileInfo(p).fileName() + "  [" + p + "]");
        it->setData(Qt::UserRole, p);
        recents_list_->addItem(it);
    }
}

void SoundFontDialog::applyPath(const QString& path, bool remember) {
    selected_path_ = path;
    path_label_->setText(path);

    if (remember) {
        recents_.removeAll(path);
        recents_.prepend(path);
        while (recents_.size() > kMaxRecents)
            recents_.removeLast();
        saveRecents();
        refreshList();
    }
    updateOkButton();
}

void SoundFontDialog::onRecentSelectionChanged() {
    auto* it = recents_list_->currentItem();
    // The built-in default is pinned and cannot be removed.
    remove_btn_->setEnabled(
        it && it->data(Qt::UserRole).toString() != builtin_path_);
}

void SoundFontDialog::removeSelectedRecent() {
    auto* it = recents_list_->currentItem();
    if (!it) return;
    const QString removed = it->data(Qt::UserRole).toString();
    if (removed.isEmpty() || removed == builtin_path_) return;  // never the built-in

    recents_.removeAll(removed);
    saveRecents();
    refreshList();

    // If the removed entry was the current selection, clear it.
    if (selected_path_ == removed) {
        selected_path_.clear();
        path_label_->setText(tr("(none selected)"));
    }
    updateOkButton();
    onRecentSelectionChanged();
}

void SoundFontDialog::clearRecents() {
    recents_.clear();
    saveRecents();
    refreshList();
    onRecentSelectionChanged();
}

void SoundFontDialog::browse() {
    QString path = QFileDialog::getOpenFileName(
        this, tr("Open SoundFont"), selected_path_,
        tr("SoundFont 2 (*.sf2);;All files (*)"));
    if (!path.isEmpty()) applyPath(path);
}

void SoundFontDialog::onRecentActivated() {
    auto* it = recents_list_->currentItem();
    if (!it) return;
    const QString path = it->data(Qt::UserRole).toString();
    if (path.isEmpty()) return;
    // The built-in default must not be added to the recent list.
    applyPath(path, /*remember=*/path != builtin_path_);
}

void SoundFontDialog::updateOkButton() {
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(!selected_path_.isEmpty());
}

} // namespace keypiano::ui
