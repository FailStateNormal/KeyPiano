#ifndef KEYPIANO_UI_WIDGETS_VST3EDITORWINDOW_H_
#define KEYPIANO_UI_WIDGETS_VST3EDITORWINDOW_H_

#include <QWidget>

namespace keypiano { class IPluginEditor; }

namespace keypiano::ui {

// Top-level window that hosts a VST3 plug-in's own editor GUI.
//
// The plug-in draws into a native child window parented to `container_`'s HWND.
// We forward the plug-in's preferred size to the window and react to its resize
// requests (IPlugFrame::resizeView, marshalled onto the GUI thread via a
// queued signal).
//
// Lifetime: does NOT own the IPluginEditor (that lives in the synth backend).
// The owner MUST destroy/close this window before the backend is torn down or
// switched, otherwise the detached plug-in view would dangle.
class Vst3EditorWindow : public QWidget {
    Q_OBJECT

public:
    explicit Vst3EditorWindow(IPluginEditor* editor, QWidget* parent = nullptr);
    ~Vst3EditorWindow() override;

    // Creates + attaches the plug-in view. Returns false if the plug-in has no
    // editor (caller should then show a message and discard this window).
    bool openEditor();

signals:
    // Emitted from the plug-in's resize callback (any thread); connected queued.
    void pluginResizeRequested(int w, int h);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void applyPluginResize(int w, int h);

private:
    void detach();

    IPluginEditor* editor_    = nullptr;   // not owned
    QWidget*       container_ = nullptr;   // native host for the plug-in view
    bool           attached_  = false;
};

} // namespace keypiano::ui

#endif // KEYPIANO_UI_WIDGETS_VST3EDITORWINDOW_H_
