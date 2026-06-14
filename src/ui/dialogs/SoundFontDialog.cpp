#include "SoundFontDialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
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

    auto* browse_btn = new QPushButton(tr("Browse..."), this);
    connect(browse_btn, &QPushButton::clicked, this, &SoundFontDialog::browse);

    path_label_ = new QLabel(tr("(none selected)"), this);
    path_label_->setWordWrap(true);

    buttons_ = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* path_row = new QHBoxLayout;
    path_row->addWidget(path_label_, 1);
    path_row->addWidget(browse_btn);

    auto* vlay = new QVBoxLayout(this);
    vlay->addWidget(recents_label);
    vlay->addWidget(recents_list_, 1);
    vlay->addLayout(path_row);
    vlay->addWidget(buttons_);

    loadRecents();
    updateOkButton();
}

void SoundFontDialog::setInitialPath(const QString& path) {
    if (!path.isEmpty()) applyPath(path);
}

void SoundFontDialog::loadRecents() {
    QSettings cfg("keypiano", "keypiano");
    recents_ = cfg.value("recent_sf2").toStringList();
    recents_list_->clear();
    for (const auto& p : recents_)
        recents_list_->addItem(QFileInfo(p).fileName() + "  [" + p + "]");
}

void SoundFontDialog::saveRecents() {
    QSettings cfg("keypiano", "keypiano");
    cfg.setValue("recent_sf2", recents_);
}

void SoundFontDialog::applyPath(const QString& path) {
    selected_path_ = path;
    path_label_->setText(path);

    recents_.removeAll(path);
    recents_.prepend(path);
    while (recents_.size() > kMaxRecents)
        recents_.removeLast();
    saveRecents();

    // Refresh list widget
    recents_list_->clear();
    for (const auto& p : recents_)
        recents_list_->addItem(QFileInfo(p).fileName() + "  [" + p + "]");

    updateOkButton();
}

void SoundFontDialog::browse() {
    QString path = QFileDialog::getOpenFileName(
        this, tr("Open SoundFont"), selected_path_,
        tr("SoundFont 2 (*.sf2);;All files (*)"));
    if (!path.isEmpty()) applyPath(path);
}

void SoundFontDialog::onRecentActivated() {
    int row = recents_list_->currentRow();
    if (row >= 0 && row < recents_.size())
        applyPath(recents_[row]);
}

void SoundFontDialog::updateOkButton() {
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(!selected_path_.isEmpty());
}

} // namespace keypiano::ui
