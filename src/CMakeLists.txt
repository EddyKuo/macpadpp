# macpad_lib：核心/功能/持久化/平台/UI/擴充 各層編為靜態庫，供 app 與 tests 共用
# （對應 profile §4.1 分層；讓 QtTest 可連結核心邏輯）

set(MACPAD_LIB_SOURCES
    app/MainWindow.cpp
    app/MainWindow_Menus.cpp
    app/MainWindow_File.cpp
    app/MainWindow_Session.cpp
    app/MainWindow_View.cpp
    app/MainWindow_Prefs.cpp
    app/MainWindow_Actions.cpp
    app/MainWindow.h
    core/EditorWidget.cpp
    core/EditorWidget.h
    core/FileEncoding.cpp
    core/FileEncoding.h
    core/LexerFactory.cpp
    core/LexerFactory.h
    features/search/FindReplaceDialog.cpp
    features/search/FindReplaceDialog.h
    features/findinfiles/FindInFilesEngine.cpp
    features/findinfiles/FindInFilesEngine.h
    features/findinfiles/FindInFilesDock.cpp
    features/findinfiles/FindInFilesDock.h
    features/functionlist/FunctionListParser.cpp
    features/functionlist/FunctionListParser.h
    features/clipboard/ClipboardHistory.cpp
    features/clipboard/ClipboardHistory.h
    features/run/RunCommand.cpp
    features/run/RunCommand.h
    features/run/RunDock.cpp
    features/run/RunDock.h
    features/cli/CliArgs.cpp
    features/cli/CliArgs.h
    features/autocomplete/ApiDatabase.cpp
    features/autocomplete/ApiDatabase.h
    features/backup/BackupService.cpp
    features/backup/BackupService.h
    features/findall/FindAllEngine.cpp
    features/findall/FindAllEngine.h
    features/findall/FindAllDock.cpp
    features/findall/FindAllDock.h
    features/mime/MimeTools.cpp
    features/mime/MimeTools.h
    features/udl/UdlDefinition.cpp
    features/udl/UdlDefinition.h
    features/udl/UdlLexer.cpp
    features/udl/UdlLexer.h
    features/udl/UdlManager.cpp
    features/udl/UdlManager.h
    features/export/HtmlExporter.cpp
    features/export/HtmlExporter.h
    features/textops/TextOps.cpp
    features/textops/TextOps.h
    features/columneditor/ColumnEditor.cpp
    features/columneditor/ColumnEditor.h
    persistence/AppPaths.cpp
    persistence/AppPaths.h
    persistence/JsonFile.cpp
    persistence/JsonFile.h
    persistence/SettingsStore.cpp
    persistence/SettingsStore.h
    persistence/StyleStore.cpp
    persistence/StyleStore.h
    persistence/SessionStore.cpp
    persistence/SessionStore.h
    persistence/RecentFiles.cpp
    persistence/RecentFiles.h
    persistence/ThemeStore.cpp
    persistence/ThemeStore.h
    platform/ThemeManager.cpp
    platform/ThemeManager.h
    platform/SingleInstance.cpp
    platform/SingleInstance.h
    ui/EditorPane.cpp
    ui/EditorPane.h
    ui/DocumentListDock.cpp
    ui/DocumentListDock.h
    ui/Panels.cpp
    ui/Panels.h
    ui/CharacterPanel.cpp
    ui/CharacterPanel.h
    ui/StyleConfiguratorDialog.cpp
    ui/StyleConfiguratorDialog.h
    ui/ShortcutMapperDialog.cpp
    ui/ShortcutMapperDialog.h
    ui/UdlEditorDialog.cpp
    ui/UdlEditorDialog.h
    ui/ThemePickerDialog.cpp
    ui/ThemePickerDialog.h
    ui/SnapshotRecoveryDialog.cpp
    ui/SnapshotRecoveryDialog.h
    ui/WorkspaceDock.cpp
    ui/WorkspaceDock.h
    ui/PreferencesDialog.cpp
    ui/PreferencesDialog.h
    ui/ColumnEditorDialog.cpp
    ui/ColumnEditorDialog.h
    ui/MacroManagerDialog.cpp
    ui/MacroManagerDialog.h
    features/functionlist/FunctionListConfig.cpp
    features/functionlist/FunctionListConfig.h
    extension/IExtension.h
    extension/ExtensionRegistry.cpp
    extension/ExtensionRegistry.h
    extension/builtin/WordCountExtension.cpp
    extension/builtin/WordCountExtension.h
    extension/builtin/MarkdownPreviewExtension.cpp
    extension/builtin/MarkdownPreviewExtension.h
    persistence/ProjectStore.cpp
    persistence/ProjectStore.h
    ui/ProjectPanelDock.cpp
    ui/ProjectPanelDock.h
    features/udl/UdlXmlIo.cpp
    features/udl/UdlXmlIo.h
    ${CMAKE_SOURCE_DIR}/resources/webview/webview.qrc
    ${CMAKE_SOURCE_DIR}/resources/i18n/i18n.qrc
    ${CMAKE_SOURCE_DIR}/resources/icons/icons.qrc
)

add_library(macpad_lib STATIC ${MACPAD_LIB_SOURCES})

target_include_directories(macpad_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${QSCINTILLA_INCLUDE_DIR})

target_link_libraries(macpad_lib PUBLIC
    Qt6::Widgets
    Qt6::PrintSupport
    Qt6::Core
    Qt6::Gui
    Qt6::Concurrent
    Qt6::Network
    Qt6::Core5Compat
    Qt6::WebEngineWidgets
    Qt6::Svg
    ${QSCINTILLA_LIBRARY})

if(STRICT_WARNINGS)
    target_compile_options(macpad_lib PRIVATE -Wall -Wextra -Werror)
endif()

# --- 可執行檔（薄殼，只含 main）------------------------------------------
# App icon：把 .icns 放進 bundle 的 Resources/，並在 Info.plist 指定 CFBundleIconFile
set(MACPAD_ICNS "${CMAKE_SOURCE_DIR}/resources/icon/macpad.icns")

add_executable(macpad++ MACOSX_BUNDLE app/main.cpp ${MACPAD_ICNS})

# 讓 .icns 在建置時被複製到 macpad++.app/Contents/Resources/
set_source_files_properties(${MACPAD_ICNS} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources")

target_link_libraries(macpad++ PRIVATE macpad_lib)

set_target_properties(macpad++ PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "macpad++"
    MACOSX_BUNDLE_GUI_IDENTIFIER "dev.macpadpp.editor"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_ICON_FILE "macpad.icns")
