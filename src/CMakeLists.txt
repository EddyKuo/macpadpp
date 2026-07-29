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
    features/print/PrintFormatter.cpp
    features/print/DocumentPrinter.cpp
    features/mime/MimeTools.h
    features/udl/UdlDefinition.cpp
    features/udl/UdlDefinition.h
    features/udl/UdlLexer.cpp
    features/udl/UdlLexer.h
    features/udl/UdlManager.cpp
    features/udl/UdlManager.h
    features/langs/BuiltinLanguages.cpp
    features/langs/BuiltinLanguages.h
    features/update/UpdateChecker.cpp
    features/update/UpdateChecker.h
    ui/MultiRowTabBar.cpp
    ui/MultiRowTabBar.h
    ui/ColorPicker.cpp
    ui/ColorPicker.h
    ui/WindowsListDialog.cpp
    ui/WindowsListDialog.h
    features/export/HtmlExporter.cpp
    features/export/HtmlExporter.h
    features/export/RtfExporter.cpp
    features/export/RtfExporter.h
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
    platform/DesktopIntegration.cpp
    platform/DesktopIntegration.h
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
    persistence/PluginStore.cpp
    persistence/ProjectStore.h
    ui/ProjectPanelDock.cpp
    ui/ProjectPanelDock.h
    features/udl/UdlXmlIo.cpp
    features/udl/UdlXmlIo.h
    ${CMAKE_SOURCE_DIR}/resources/webview/webview.qrc
    ${CMAKE_SOURCE_DIR}/resources/i18n/i18n.qrc
    ${CMAKE_SOURCE_DIR}/resources/icons/icons.qrc
    ${CMAKE_SOURCE_DIR}/resources/themes/themes.qrc
)

add_library(macpad_lib STATIC ${MACPAD_LIB_SOURCES})

# 版本字串供程式內使用（About 對話框、更新檢查比對）
target_compile_definitions(macpad_lib PUBLIC MACPAD_VERSION="${PROJECT_VERSION}")

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

# Windows：QScintilla 以 DLL 形式提供，消費端須定義 QSCINTILLA_DLL，
# 使其標頭的 QSCINTILLA_EXPORT 展開為 __declspec(dllimport)，否則
# QsciScintilla::staticMetaObject 等資料符號無法從 DLL 匯入（LNK2001）。
# PUBLIC：讓連結 macpad_lib 且直接 include Qsci 標頭的 app/tests 也一併取得。
if(WIN32)
    target_compile_definitions(macpad_lib PUBLIC QSCINTILLA_DLL)
endif()

if(STRICT_WARNINGS)
    if(MSVC)
        # /W4：高警告等級；/WX：警告視為錯誤；/permissive-：嚴格標準符合
        # /wd4458：停用「區域變數遮蔽繼承而來的基底類別成員」警告。此警告僅針對
        #   Qt/QScintilla 基底類別的成員（本專案標頭未宣告 lex/data 等成員），
        #   Clang 的 -Wall -Wextra 亦不回報，停用以維持雙平台一致（macOS 亦為 -Werror）。
        target_compile_options(macpad_lib PRIVATE /W4 /WX /permissive- /wd4458)
    else()
        target_compile_options(macpad_lib PRIVATE -Wall -Wextra -Werror)
    endif()
endif()

# --- 可執行檔（薄殼，只含 main）------------------------------------------
if(APPLE)
    # App icon：把 .icns 放進 bundle 的 Resources/，並在 Info.plist 指定 CFBundleIconFile
    set(MACPAD_ICNS "${CMAKE_SOURCE_DIR}/resources/icon/macpad.icns")
    # 缺圖示就直接失敗：少了它 bundle 仍會建置成功，只是在 Finder/Dock 顯示
    # 通用的空白文件圖示——是那種一路帶進 Release 才被發現的沉默故障。
    if(NOT EXISTS "${MACPAD_ICNS}")
        message(FATAL_ERROR "找不到 ${MACPAD_ICNS}；請執行 scripts/icons/build_app_icons.sh")
    endif()
    add_executable(macpad++ MACOSX_BUNDLE app/main.cpp ${MACPAD_ICNS})
    # 讓 .icns 在建置時被複製到 macpad++.app/Contents/Resources/
    set_source_files_properties(${MACPAD_ICNS} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    set_target_properties(macpad++ PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "macpad++"
        MACOSX_BUNDLE_GUI_IDENTIFIER "dev.macpadpp.editor"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "macpad.icns")
elseif(WIN32)
    # WIN32：GUI 子系統（啟動不彈 console 視窗）；.rc 嵌入應用程式圖示。
    set(MACPAD_RC "${CMAKE_SOURCE_DIR}/resources/icon/macpad.rc")
    set(MACPAD_ICO "${CMAKE_SOURCE_DIR}/resources/icon/macpad.ico")
    # 原本缺 .rc 時會靜靜地建置出「沒有圖示」的 exe。改為直接失敗——
    # 檔案總管與工作列顯示通用圖示，通常要到使用者手上才會有人發現。
    foreach(f "${MACPAD_RC}" "${MACPAD_ICO}")
        if(NOT EXISTS "${f}")
            message(FATAL_ERROR "找不到 ${f}；請執行 scripts/icons/build_app_icons.sh")
        endif()
    endforeach()
    add_executable(macpad++ WIN32 app/main.cpp ${MACPAD_RC})
else()
    add_executable(macpad++ app/main.cpp)
endif()

target_link_libraries(macpad++ PRIVATE macpad_lib)

# --- Windows：build 後就地同梱 Qt/WebEngine/QScintilla 執行期檔案 ----------
# 讓 build/src/macpad++.exe 可直接執行，免手動把 Qt bin 加進 PATH。
# 以 windeployqt 複製所需 Qt DLL、platforms\qwindows.dll（必要）、WebEngine 執行元件等，
# 再補上第三方的 qscintilla2_qt6.dll（windeployqt 不處理非 Qt 函式庫）。
# 預設開啟；快速迭代可用 -DMACPAD_WINDEPLOY=OFF 關閉（首次部署 WebEngine 較耗時，
# 之後 windeployqt 會略過已存在檔案）。
option(MACPAD_WINDEPLOY "Windows：build 後以 windeployqt 就地同梱 Qt 執行期檔案" ON)
if(WIN32 AND MACPAD_WINDEPLOY)
    find_program(WINDEPLOYQT_EXE windeployqt HINTS "${_qt_prefix}/bin")
    get_filename_component(_qsci_libdir "${QSCINTILLA_LIBRARY}" DIRECTORY)
    find_file(QSCINTILLA_DLL_FILE
        NAMES qscintilla2_qt6.dll
        HINTS "${_qsci_libdir}" "${_qt_prefix}/bin" "${_qt_prefix}/lib")

    if(WINDEPLOYQT_EXE)
        add_custom_command(TARGET macpad++ POST_BUILD
            COMMAND "${WINDEPLOYQT_EXE}" --no-translations
                    "$<IF:$<CONFIG:Debug>,--debug,--release>"
                    "$<TARGET_FILE:macpad++>"
            COMMENT "windeployqt：就地同梱 Qt 執行期 DLL 至 build 輸出"
            VERBATIM)
    else()
        message(WARNING "找不到 windeployqt，跳過就地同梱；執行 macpad++.exe 前需自行將 Qt bin 加入 PATH")
    endif()

    if(QSCINTILLA_DLL_FILE)
        add_custom_command(TARGET macpad++ POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${QSCINTILLA_DLL_FILE}" "$<TARGET_FILE_DIR:macpad++>"
            COMMENT "複製 qscintilla2_qt6.dll 至 build 輸出"
            VERBATIM)
    else()
        message(WARNING "找不到 qscintilla2_qt6.dll，執行時可能缺少此 DLL")
    endif()
endif()
