#ifndef KEYPIANO_UI_HELPCONTENT_H_
#define KEYPIANO_UI_HELPCONTENT_H_

#include <QString>

namespace keypiano::ui {

// The bilingual Usage Guide, returned as self-contained HTML for a QTextBrowser.
// Kept out of MainWindow so the (long) prose does not bloat the window class.
// keypiano bundles NO VST3 plug-in — the guide explains where to get one and how
// to load it.
QString usageGuideHtmlEn();
QString usageGuideHtmlZh();

}  // namespace keypiano::ui

#endif  // KEYPIANO_UI_HELPCONTENT_H_
