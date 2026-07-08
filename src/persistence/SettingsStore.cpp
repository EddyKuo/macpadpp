#include "persistence/SettingsStore.h"

#include "persistence/AppPaths.h"
#include "persistence/JsonFile.h"

#include <QJsonObject>

namespace macpad::persistence {

static QString settingsPath() { return AppPaths::filePath(QStringLiteral("settings.json")); }

static ThemeMode themeFromString(const QString &s)
{
    if (s == QLatin1String("light")) return ThemeMode::Light;
    if (s == QLatin1String("dark"))  return ThemeMode::Dark;
    return ThemeMode::System;
}

static QString themeToString(ThemeMode m)
{
    switch (m) {
    case ThemeMode::Light: return QStringLiteral("light");
    case ThemeMode::Dark:  return QStringLiteral("dark");
    case ThemeMode::System:return QStringLiteral("system");
    }
    return QStringLiteral("system");
}

static BackupMode backupModeFromString(const QString &s)
{
    if (s == QLatin1String("simple"))  return BackupMode::Simple;
    if (s == QLatin1String("verbose")) return BackupMode::Verbose;
    return BackupMode::None;
}

static QString backupModeToString(BackupMode m)
{
    switch (m) {
    case BackupMode::Simple:  return QStringLiteral("simple");
    case BackupMode::Verbose: return QStringLiteral("verbose");
    case BackupMode::None:    return QStringLiteral("none");
    }
    return QStringLiteral("none");
}

static macpad::core::Eol eolFromString(const QString &s)
{
    if (s == QLatin1String("crlf")) return macpad::core::Eol::CrLf;
    if (s == QLatin1String("cr"))   return macpad::core::Eol::Cr;
    return macpad::core::Eol::Lf;
}

static QString eolToString(macpad::core::Eol e)
{
    switch (e) {
    case macpad::core::Eol::CrLf: return QStringLiteral("crlf");
    case macpad::core::Eol::Cr:   return QStringLiteral("cr");
    case macpad::core::Eol::Lf:   return QStringLiteral("lf");
    }
    return QStringLiteral("lf");
}

static macpad::core::Encoding encodingFromString(const QString &s)
{
    if (s == QLatin1String("utf8bom")) return macpad::core::Encoding::Utf8Bom;
    if (s == QLatin1String("utf16le")) return macpad::core::Encoding::Utf16LE;
    if (s == QLatin1String("utf16be")) return macpad::core::Encoding::Utf16BE;
    if (s == QLatin1String("latin1"))  return macpad::core::Encoding::Latin1;
    return macpad::core::Encoding::Utf8;
}

static QString encodingToString(macpad::core::Encoding e)
{
    switch (e) {
    case macpad::core::Encoding::Utf8Bom: return QStringLiteral("utf8bom");
    case macpad::core::Encoding::Utf16LE: return QStringLiteral("utf16le");
    case macpad::core::Encoding::Utf16BE: return QStringLiteral("utf16be");
    case macpad::core::Encoding::Latin1:  return QStringLiteral("latin1");
    case macpad::core::Encoding::Utf8:    return QStringLiteral("utf8");
    }
    return QStringLiteral("utf8");
}

Settings SettingsStore::load()
{
    Settings s;  // 預設值
    const QJsonObject o = JsonFile::load(settingsPath());
    if (o.isEmpty())
        return s;  // 首次或損毀 → 全預設（缺欄位回填）

    s.restoreOnLaunch = o.value(QStringLiteral("restore_on_launch")).toBool(s.restoreOnLaunch);
    s.theme = themeFromString(o.value(QStringLiteral("theme")).toString(themeToString(s.theme)));
    s.autosaveEnabled = o.value(QStringLiteral("autosave_enabled")).toBool(s.autosaveEnabled);
    s.autosaveIntervalSec = o.value(QStringLiteral("autosave_interval_sec")).toInt(s.autosaveIntervalSec);
    s.tabWidth = o.value(QStringLiteral("tab_width")).toInt(s.tabWidth);
    s.singleInstance = o.value(QStringLiteral("single_instance")).toBool(s.singleInstance);
    s.language = o.value(QStringLiteral("language")).toString(s.language);

    // Editing
    s.showLineNumbers = o.value(QStringLiteral("show_line_numbers")).toBool(s.showLineNumbers);
    s.showIndentGuides = o.value(QStringLiteral("show_indent_guides")).toBool(s.showIndentGuides);
    s.wordWrap = o.value(QStringLiteral("word_wrap")).toBool(s.wordWrap);
    s.showWhitespace = o.value(QStringLiteral("show_whitespace")).toBool(s.showWhitespace);
    s.caretWidth = o.value(QStringLiteral("caret_width")).toInt(s.caretWidth);
    s.currentLineHighlight =
        o.value(QStringLiteral("current_line_highlight")).toBool(s.currentLineHighlight);
    s.enableVirtualSpace = o.value(QStringLiteral("enable_virtual_space")).toBool(s.enableVirtualSpace);
    s.copyLineWithoutSelection =
        o.value(QStringLiteral("copy_line_without_selection")).toBool(s.copyLineWithoutSelection);

    // New Document
    s.defaultEol = eolFromString(o.value(QStringLiteral("default_eol")).toString(eolToString(s.defaultEol)));
    s.defaultEncoding = encodingFromString(
        o.value(QStringLiteral("default_encoding")).toString(encodingToString(s.defaultEncoding)));
    s.autoDetectFileStatus =
        o.value(QStringLiteral("auto_detect_file_status")).toBool(s.autoDetectFileStatus);
    s.sessionFileExt = o.value(QStringLiteral("session_file_ext")).toString(s.sessionFileExt);

    // Backup
    s.backupMode = backupModeFromString(
        o.value(QStringLiteral("backup_mode")).toString(backupModeToString(s.backupMode)));
    s.backupDir = o.value(QStringLiteral("backup_dir")).toString(s.backupDir);
    s.autosaveOnFocusLoss = o.value(QStringLiteral("autosave_on_focus_loss")).toBool(s.autosaveOnFocusLoss);
    s.enableSessionSnapshot =
        o.value(QStringLiteral("enable_session_snapshot")).toBool(s.enableSessionSnapshot);
    s.snapshotIntervalSec = o.value(QStringLiteral("snapshot_interval_sec")).toInt(s.snapshotIntervalSec);

    // Auto-Completion
    s.autoInsertPairs = o.value(QStringLiteral("auto_insert_pairs")).toBool(s.autoInsertPairs);
    s.wordAutoComplete = o.value(QStringLiteral("word_auto_complete")).toBool(s.wordAutoComplete);
    s.acThreshold = o.value(QStringLiteral("ac_threshold")).toInt(s.acThreshold);
    s.showCallTips = o.value(QStringLiteral("show_call_tips")).toBool(s.showCallTips);

    // Performance
    s.largeFileMB = o.value(QStringLiteral("large_file_mb")).toInt(s.largeFileMB);
    s.disableAutoCompleteOverMB =
        o.value(QStringLiteral("disable_autocomplete_over_mb")).toInt(s.disableAutoCompleteOverMB);

    // Search
    s.searchEngineUrl = o.value(QStringLiteral("search_engine_url")).toString(s.searchEngineUrl);
    s.keepFindDialogOpen = o.value(QStringLiteral("keep_find_dialog_open")).toBool(s.keepFindDialogOpen);
    s.confirmReplaceAll = o.value(QStringLiteral("confirm_replace_all")).toBool(s.confirmReplaceAll);
    s.findInSelectionThreshold =
        o.value(QStringLiteral("find_in_selection_threshold")).toInt(s.findInSelectionThreshold);

    // Highlighting
    s.showWrapSymbol = o.value(QStringLiteral("show_wrap_symbol")).toBool(s.showWrapSymbol);
    s.showEol = o.value(QStringLiteral("show_eol")).toBool(s.showEol);
    s.smartHighlight = o.value(QStringLiteral("smart_highlight")).toBool(s.smartHighlight);
    s.highlightMatchingTags =
        o.value(QStringLiteral("highlight_matching_tags")).toBool(s.highlightMatchingTags);
    s.edgeColumn = o.value(QStringLiteral("edge_column")).toInt(s.edgeColumn);
    s.multiEdgeEnabled = o.value(QStringLiteral("multi_edge_enabled")).toBool(s.multiEdgeEnabled);

    // Dark Mode / Appearance
    s.showToolbar = o.value(QStringLiteral("show_toolbar")).toBool(s.showToolbar);
    s.showStatusBar = o.value(QStringLiteral("show_status_bar")).toBool(s.showStatusBar);
    s.showTabBar = o.value(QStringLiteral("show_tab_bar")).toBool(s.showTabBar);
    s.caretBlinkRate = o.value(QStringLiteral("caret_blink_rate")).toInt(s.caretBlinkRate);

    return s;
}

bool SettingsStore::save(const Settings &s)
{
    QJsonObject o;
    o.insert(QStringLiteral("schema_version"), s.schemaVersion);
    o.insert(QStringLiteral("restore_on_launch"), s.restoreOnLaunch);
    o.insert(QStringLiteral("theme"), themeToString(s.theme));
    o.insert(QStringLiteral("autosave_enabled"), s.autosaveEnabled);
    o.insert(QStringLiteral("autosave_interval_sec"), s.autosaveIntervalSec);
    o.insert(QStringLiteral("tab_width"), s.tabWidth);
    o.insert(QStringLiteral("single_instance"), s.singleInstance);
    o.insert(QStringLiteral("language"), s.language);

    // Editing
    o.insert(QStringLiteral("show_line_numbers"), s.showLineNumbers);
    o.insert(QStringLiteral("show_indent_guides"), s.showIndentGuides);
    o.insert(QStringLiteral("word_wrap"), s.wordWrap);
    o.insert(QStringLiteral("show_whitespace"), s.showWhitespace);
    o.insert(QStringLiteral("caret_width"), s.caretWidth);
    o.insert(QStringLiteral("current_line_highlight"), s.currentLineHighlight);
    o.insert(QStringLiteral("enable_virtual_space"), s.enableVirtualSpace);
    o.insert(QStringLiteral("copy_line_without_selection"), s.copyLineWithoutSelection);

    // New Document
    o.insert(QStringLiteral("default_eol"), eolToString(s.defaultEol));
    o.insert(QStringLiteral("default_encoding"), encodingToString(s.defaultEncoding));
    o.insert(QStringLiteral("auto_detect_file_status"), s.autoDetectFileStatus);
    o.insert(QStringLiteral("session_file_ext"), s.sessionFileExt);

    // Backup
    o.insert(QStringLiteral("backup_mode"), backupModeToString(s.backupMode));
    o.insert(QStringLiteral("backup_dir"), s.backupDir);
    o.insert(QStringLiteral("autosave_on_focus_loss"), s.autosaveOnFocusLoss);
    o.insert(QStringLiteral("enable_session_snapshot"), s.enableSessionSnapshot);
    o.insert(QStringLiteral("snapshot_interval_sec"), s.snapshotIntervalSec);

    // Auto-Completion
    o.insert(QStringLiteral("auto_insert_pairs"), s.autoInsertPairs);
    o.insert(QStringLiteral("word_auto_complete"), s.wordAutoComplete);
    o.insert(QStringLiteral("ac_threshold"), s.acThreshold);
    o.insert(QStringLiteral("show_call_tips"), s.showCallTips);

    // Performance
    o.insert(QStringLiteral("large_file_mb"), s.largeFileMB);
    o.insert(QStringLiteral("disable_autocomplete_over_mb"), s.disableAutoCompleteOverMB);

    // Search
    o.insert(QStringLiteral("search_engine_url"), s.searchEngineUrl);
    o.insert(QStringLiteral("keep_find_dialog_open"), s.keepFindDialogOpen);
    o.insert(QStringLiteral("confirm_replace_all"), s.confirmReplaceAll);
    o.insert(QStringLiteral("find_in_selection_threshold"), s.findInSelectionThreshold);

    // Highlighting
    o.insert(QStringLiteral("show_wrap_symbol"), s.showWrapSymbol);
    o.insert(QStringLiteral("show_eol"), s.showEol);
    o.insert(QStringLiteral("smart_highlight"), s.smartHighlight);
    o.insert(QStringLiteral("highlight_matching_tags"), s.highlightMatchingTags);
    o.insert(QStringLiteral("edge_column"), s.edgeColumn);
    o.insert(QStringLiteral("multi_edge_enabled"), s.multiEdgeEnabled);

    // Dark Mode / Appearance
    o.insert(QStringLiteral("show_toolbar"), s.showToolbar);
    o.insert(QStringLiteral("show_status_bar"), s.showStatusBar);
    o.insert(QStringLiteral("show_tab_bar"), s.showTabBar);
    o.insert(QStringLiteral("caret_blink_rate"), s.caretBlinkRate);

    return JsonFile::save(settingsPath(), o);
}

}  // namespace macpad::persistence
