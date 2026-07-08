#include "persistence/SettingsStore.h"

#include "persistence/AppPaths.h"
#include "persistence/JsonFile.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

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

static ToolbarIconSize toolbarIconSizeFromString(const QString &s)
{
    if (s == QLatin1String("small")) return ToolbarIconSize::Small;
    if (s == QLatin1String("large")) return ToolbarIconSize::Large;
    return ToolbarIconSize::Standard;
}

static QString toolbarIconSizeToString(ToolbarIconSize v)
{
    switch (v) {
    case ToolbarIconSize::Small:    return QStringLiteral("small");
    case ToolbarIconSize::Large:    return QStringLiteral("large");
    case ToolbarIconSize::Standard: return QStringLiteral("standard");
    }
    return QStringLiteral("standard");
}

static EdgeMode edgeModeFromString(const QString &s)
{
    if (s == QLatin1String("line"))       return EdgeMode::Line;
    if (s == QLatin1String("background")) return EdgeMode::Background;
    return EdgeMode::None;
}

static QString edgeModeToString(EdgeMode v)
{
    switch (v) {
    case EdgeMode::Line:       return QStringLiteral("line");
    case EdgeMode::Background: return QStringLiteral("background");
    case EdgeMode::None:       return QStringLiteral("none");
    }
    return QStringLiteral("none");
}

static FoldMarginStyle foldMarginStyleFromString(const QString &s)
{
    if (s == QLatin1String("none"))   return FoldMarginStyle::None;
    if (s == QLatin1String("arrow"))  return FoldMarginStyle::Arrow;
    if (s == QLatin1String("circle")) return FoldMarginStyle::Circle;
    if (s == QLatin1String("box"))    return FoldMarginStyle::Box;
    return FoldMarginStyle::Simple;
}

static QString foldMarginStyleToString(FoldMarginStyle v)
{
    switch (v) {
    case FoldMarginStyle::None:   return QStringLiteral("none");
    case FoldMarginStyle::Arrow:  return QStringLiteral("arrow");
    case FoldMarginStyle::Circle: return QStringLiteral("circle");
    case FoldMarginStyle::Box:    return QStringLiteral("box");
    case FoldMarginStyle::Simple: return QStringLiteral("simple");
    }
    return QStringLiteral("simple");
}

static DefaultDirPolicy defaultDirPolicyFromString(const QString &s)
{
    if (s == QLatin1String("remember_last")) return DefaultDirPolicy::RememberLast;
    if (s == QLatin1String("fixed_path"))    return DefaultDirPolicy::FixedPath;
    return DefaultDirPolicy::FollowCurrentDoc;
}

static QString defaultDirPolicyToString(DefaultDirPolicy v)
{
    switch (v) {
    case DefaultDirPolicy::RememberLast:     return QStringLiteral("remember_last");
    case DefaultDirPolicy::FixedPath:        return QStringLiteral("fixed_path");
    case DefaultDirPolicy::FollowCurrentDoc: return QStringLiteral("follow_current_doc");
    }
    return QStringLiteral("follow_current_doc");
}

static MultiInstanceMode multiInstanceModeFromString(const QString &s)
{
    if (s == QLatin1String("mono"))         return MultiInstanceMode::MonoInstance;
    if (s == QLatin1String("always_multi")) return MultiInstanceMode::AlwaysMulti;
    return MultiInstanceMode::MultiInstOnSession;
}

static QString multiInstanceModeToString(MultiInstanceMode v)
{
    switch (v) {
    case MultiInstanceMode::MonoInstance:        return QStringLiteral("mono");
    case MultiInstanceMode::AlwaysMulti:         return QStringLiteral("always_multi");
    case MultiInstanceMode::MultiInstOnSession:  return QStringLiteral("multi_inst_on_session");
    }
    return QStringLiteral("multi_inst_on_session");
}

static FileStatusAutoDetectMode fileStatusAutoDetectFromString(const QString &s)
{
    if (s == QLatin1String("disabled"))       return FileStatusAutoDetectMode::Disabled;
    if (s == QLatin1String("enabled_silent")) return FileStatusAutoDetectMode::EnabledSilent;
    return FileStatusAutoDetectMode::Enabled;
}

static QString fileStatusAutoDetectToString(FileStatusAutoDetectMode v)
{
    switch (v) {
    case FileStatusAutoDetectMode::Disabled:      return QStringLiteral("disabled");
    case FileStatusAutoDetectMode::EnabledSilent: return QStringLiteral("enabled_silent");
    case FileStatusAutoDetectMode::Enabled:       return QStringLiteral("enabled");
    }
    return QStringLiteral("enabled");
}

static QJsonArray stringListToJsonArray(const QStringList &list)
{
    QJsonArray arr;
    for (const QString &s : list) arr.append(s);
    return arr;
}

static QStringList jsonArrayToStringList(const QJsonValue &v)
{
    QStringList out;
    if (!v.isArray()) return out;
    const QJsonArray arr = v.toArray();
    for (const QJsonValue &item : arr) out << item.toString();
    return out;
}

static QJsonObject intMapToJsonObject(const QMap<QString, int> &map)
{
    QJsonObject o;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) o.insert(it.key(), it.value());
    return o;
}

static QMap<QString, int> jsonObjectToIntMap(const QJsonValue &v)
{
    QMap<QString, int> out;
    if (!v.isObject()) return out;
    const QJsonObject o = v.toObject();
    for (auto it = o.constBegin(); it != o.constEnd(); ++it) out.insert(it.key(), it.value().toInt());
    return out;
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
    s.columnSelectionToMultiEdit =
        o.value(QStringLiteral("column_selection_to_multi_edit")).toBool(s.columnSelectionToMultiEdit);

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

    // Toolbar
    s.toolbarIconSize = toolbarIconSizeFromString(
        o.value(QStringLiteral("toolbar_icon_size")).toString(toolbarIconSizeToString(s.toolbarIconSize)));

    // Tab Bar
    s.tabBarMultiLine = o.value(QStringLiteral("tab_bar_multiline")).toBool(s.tabBarMultiLine);
    s.tabBarVertical = o.value(QStringLiteral("tab_bar_vertical")).toBool(s.tabBarVertical);
    s.tabBarShowCloseButton =
        o.value(QStringLiteral("tab_bar_show_close_button")).toBool(s.tabBarShowCloseButton);
    s.tabBarDoubleClickCloses =
        o.value(QStringLiteral("tab_bar_double_click_closes")).toBool(s.tabBarDoubleClickCloses);

    // Margins / Border / Edge
    s.edgeMode = edgeModeFromString(o.value(QStringLiteral("edge_mode")).toString(edgeModeToString(s.edgeMode)));
    s.foldMarginStyle = foldMarginStyleFromString(
        o.value(QStringLiteral("fold_margin_style")).toString(foldMarginStyleToString(s.foldMarginStyle)));
    s.lineNumberMargin = o.value(QStringLiteral("line_number_margin")).toBool(s.lineNumberMargin);

    // Default Directory
    s.defaultDirPolicy = defaultDirPolicyFromString(
        o.value(QStringLiteral("default_dir_policy")).toString(defaultDirPolicyToString(s.defaultDirPolicy)));
    s.defaultDirFixedPath =
        o.value(QStringLiteral("default_dir_fixed_path")).toString(s.defaultDirFixedPath);

    // Recent Files History
    s.recentFilesMaxEntries =
        o.value(QStringLiteral("recent_files_max_entries")).toInt(s.recentFilesMaxEntries);
    s.recentFilesShowFullPath =
        o.value(QStringLiteral("recent_files_show_full_path")).toBool(s.recentFilesShowFullPath);
    s.recentFilesInSubmenu =
        o.value(QStringLiteral("recent_files_in_submenu")).toBool(s.recentFilesInSubmenu);

    // Language menu / per-language indentation
    if (o.contains(QStringLiteral("disabled_languages")))
        s.disabledLanguages = jsonArrayToStringList(o.value(QStringLiteral("disabled_languages")));
    if (o.contains(QStringLiteral("per_lang_tab_width")))
        s.perLangTabWidth = jsonObjectToIntMap(o.value(QStringLiteral("per_lang_tab_width")));

    // Multi-Instance & Date
    s.multiInstanceMode = multiInstanceModeFromString(o.value(QStringLiteral("multi_instance_mode"))
                                                            .toString(multiInstanceModeToString(s.multiInstanceMode)));
    s.dateFormat = o.value(QStringLiteral("date_format")).toString(s.dateFormat);
    s.customDateFormat = o.value(QStringLiteral("custom_date_format")).toString(s.customDateFormat);

    // Delimiter
    s.delimiterChars = o.value(QStringLiteral("delimiter_chars")).toString(s.delimiterChars);
    s.ctrlDoubleClickWholeWord =
        o.value(QStringLiteral("ctrl_double_click_whole_word")).toBool(s.ctrlDoubleClickWholeWord);

    // MISC
    s.docSwitcherEnabled = o.value(QStringLiteral("doc_switcher_enabled")).toBool(s.docSwitcherEnabled);
    s.docPeekerEnabled = o.value(QStringLiteral("doc_peeker_enabled")).toBool(s.docPeekerEnabled);
    s.fileStatusAutoDetect = fileStatusAutoDetectFromString(
        o.value(QStringLiteral("file_status_auto_detect"))
            .toString(fileStatusAutoDetectToString(s.fileStatusAutoDetect)));
    s.autoUpdater = o.value(QStringLiteral("auto_updater")).toBool(s.autoUpdater);
    s.enableSound = o.value(QStringLiteral("enable_sound")).toBool(s.enableSound);

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
    o.insert(QStringLiteral("column_selection_to_multi_edit"), s.columnSelectionToMultiEdit);

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

    // Toolbar
    o.insert(QStringLiteral("toolbar_icon_size"), toolbarIconSizeToString(s.toolbarIconSize));

    // Tab Bar
    o.insert(QStringLiteral("tab_bar_multiline"), s.tabBarMultiLine);
    o.insert(QStringLiteral("tab_bar_vertical"), s.tabBarVertical);
    o.insert(QStringLiteral("tab_bar_show_close_button"), s.tabBarShowCloseButton);
    o.insert(QStringLiteral("tab_bar_double_click_closes"), s.tabBarDoubleClickCloses);

    // Margins / Border / Edge
    o.insert(QStringLiteral("edge_mode"), edgeModeToString(s.edgeMode));
    o.insert(QStringLiteral("fold_margin_style"), foldMarginStyleToString(s.foldMarginStyle));
    o.insert(QStringLiteral("line_number_margin"), s.lineNumberMargin);

    // Default Directory
    o.insert(QStringLiteral("default_dir_policy"), defaultDirPolicyToString(s.defaultDirPolicy));
    o.insert(QStringLiteral("default_dir_fixed_path"), s.defaultDirFixedPath);

    // Recent Files History
    o.insert(QStringLiteral("recent_files_max_entries"), s.recentFilesMaxEntries);
    o.insert(QStringLiteral("recent_files_show_full_path"), s.recentFilesShowFullPath);
    o.insert(QStringLiteral("recent_files_in_submenu"), s.recentFilesInSubmenu);

    // Language menu / per-language indentation
    o.insert(QStringLiteral("disabled_languages"), stringListToJsonArray(s.disabledLanguages));
    o.insert(QStringLiteral("per_lang_tab_width"), intMapToJsonObject(s.perLangTabWidth));

    // Multi-Instance & Date
    o.insert(QStringLiteral("multi_instance_mode"), multiInstanceModeToString(s.multiInstanceMode));
    o.insert(QStringLiteral("date_format"), s.dateFormat);
    o.insert(QStringLiteral("custom_date_format"), s.customDateFormat);

    // Delimiter
    o.insert(QStringLiteral("delimiter_chars"), s.delimiterChars);
    o.insert(QStringLiteral("ctrl_double_click_whole_word"), s.ctrlDoubleClickWholeWord);

    // MISC
    o.insert(QStringLiteral("doc_switcher_enabled"), s.docSwitcherEnabled);
    o.insert(QStringLiteral("doc_peeker_enabled"), s.docPeekerEnabled);
    o.insert(QStringLiteral("file_status_auto_detect"), fileStatusAutoDetectToString(s.fileStatusAutoDetect));
    o.insert(QStringLiteral("auto_updater"), s.autoUpdater);
    o.insert(QStringLiteral("enable_sound"), s.enableSound);

    return JsonFile::save(settingsPath(), o);
}

}  // namespace macpad::persistence
