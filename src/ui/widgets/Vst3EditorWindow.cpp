#include "widgets/Vst3EditorWindow.h"

#include <QCloseEvent>
#include <QVBoxLayout>

#include "synth/IPluginEditor.h"

namespace keypiano::ui {

Vst3EditorWindow::Vst3EditorWindow(IPluginEditor* editor, QWidget* parent)
    : QWidget(parent, Qt::Window), editor_(editor) {
    setWindowTitle(tr("Plugin Editor"));
    // Closing the window detaches the plug-in view (closeEvent) and deletes
    // itself; the owner tracks the lifetime via QObject::destroyed.
    setAttribute(Qt::WA_DeleteOnClose, true);

    // The plug-in attaches its own child HWND under this container. A zero-
    // margin layout keeps the container exactly the size we set.
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    container_ = new QWidget(this);
    // Force a dedicated native window so the plug-in gets a real HWND parent,
    // without turning every ancestor into a native window.
    container_->setAttribute(Qt::WA_NativeWindow);
    container_->setAttribute(Qt::WA_DontCreateNativeAncestors);
    layout->addWidget(container_);

    connect(this, &Vst3EditorWindow::pluginResizeRequested,
            this, &Vst3EditorWindow::applyPluginResize, Qt::QueuedConnection);
}

Vst3EditorWindow::~Vst3EditorWindow() {
    detach();
}

bool Vst3EditorWindow::openEditor() {
    if (!editor_) return false;

    // Realise the native window so winId() returns a valid HWND.
    const WId wid = container_->winId();

    int w = 0, h = 0;
    if (!editor_->openEditor(reinterpret_cast<void*>(wid), w, h)) {
        return false;  // plug-in has no editor / attach failed
    }
    attached_ = true;

    if (w > 0 && h > 0) {
        container_->setFixedSize(w, h);
        adjustSize();
    }

    // The plug-in may ask to resize later; marshal it onto the GUI thread.
    editor_->setEditorResizeCallback([this](int rw, int rh) {
        emit pluginResizeRequested(rw, rh);
    });
    return true;
}

void Vst3EditorWindow::applyPluginResize(int w, int h) {
    if (w <= 0 || h <= 0) return;
    container_->setFixedSize(w, h);
    adjustSize();
}

void Vst3EditorWindow::closeEvent(QCloseEvent* event) {
    detach();
    QWidget::closeEvent(event);
}

void Vst3EditorWindow::detach() {
    if (!editor_) return;
    if (attached_) {
        editor_->setEditorResizeCallback({});
        editor_->closeEditor();
        attached_ = false;
    }
}

} // namespace keypiano::ui
