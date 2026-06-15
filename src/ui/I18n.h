#ifndef KEYPIANO_UI_I18N_H_
#define KEYPIANO_UI_I18N_H_

#include <QHash>
#include <QString>
#include <QTranslator>

namespace keypiano::ui {

// UI language. English is the source language (what the code's tr() literals are
// written in); Chinese is provided by the embedded table in Translator.
enum class Lang { English, Chinese };

// A self-contained QTranslator backed by a compiled-in English->Simplified
// Chinese string table. We use this instead of a .qm file because the vcpkg Qt
// build ships no lrelease/lupdate, so there is no way to produce a .qm at build
// time. Installing this translator makes every tr() call return Chinese; removing
// it falls back to the English source string. Because the lookup ignores the tr()
// context and keys purely on the source text, one entry covers a string wherever
// it appears (MainWindow, dialogs, ...).
class Translator : public QTranslator {
public:
    explicit Translator(QObject* parent = nullptr);

    // Force Qt to consult translate() — the default returns true when no .qm is
    // loaded, which would make Qt skip this translator entirely.
    bool isEmpty() const override { return false; }

    QString translate(const char* context, const char* sourceText,
                      const char* disambiguation = nullptr,
                      int n = -1) const override;

private:
    QHash<QString, QString> map_;
};

}  // namespace keypiano::ui

#endif  // KEYPIANO_UI_I18N_H_
