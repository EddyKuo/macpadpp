# 單元測試（QtTest）——連結 macpad_lib 核心邏輯（NFR-008）
# 每個測試檔一個可執行檔，註冊為 CTest。命名 test_{unit}。

set(MACPAD_TESTS
    test_fileencoding
    test_persistence
    test_lexerfactory
    test_search
    test_settings
    test_extension
    test_editor
    test_findinfiles
    test_panels
    test_runcommand
    test_cliargs
    test_udl
    test_htmlexport
    test_textops
    test_columneditor
    test_resources
    test_i18n
    test_editor_ops
    test_udllexer
    test_thememanager
    test_stylestore
    test_lexerfactory_ext
    test_wordcount
    test_udlmanager
    test_singleinstance
    test_apidb
    test_backup
    test_findall
    test_themestore
    test_recoverydialog
    test_mime
    test_projectstore
    test_pluginstore
    test_shortcutmapper
    test_printformatter
    test_icons
    test_toolbar
    test_udlxmlio
    test_functionlistconfig
    test_contextmenu
    test_session_snapshot
    test_desktopintegration
    test_builtinlangs
    test_functionlist_langs
    test_pintab
    test_multirowtabbar
    test_updatechecker
    test_apifilestore
    test_fileassociation
    # GUI 層測試：對話框、面板與 MainWindow 命令處理。
    # 這些檔案原本行覆蓋率為 0%——核心邏輯測得很紮實，缺口全在 UI 層。
    test_findreplacedialog
    test_udleditordialog
    test_styledialogs
    test_workspacedocks
    test_resultdocks
    test_miscdialogs
    test_outputpipeline
    test_mainwindow_actions
    # 第二批：MainWindow 各分部與剩餘缺口。這些檔的未覆蓋部分多半是
    # 「沒人按過那顆選單項」而非邏輯未實作，故以驅動實際命令的方式補齊。
    test_mainwindow_menus
    test_mainwindow_view
    test_mainwindow_file
    test_mainwindow_prefs
    test_mainwindow_core
    test_editorwidget_gaps
    test_panels_gaps
    test_uiwidgets_gaps
    test_smallgaps
)

foreach(t ${MACPAD_TESTS})
    add_executable(${t} unit/${t}.cpp)
    target_link_libraries(${t} PRIVATE macpad_lib Qt6::Test)
    add_test(NAME ${t} COMMAND ${t})
    # 測試需要 GUI-less 平台
    set_tests_properties(${t} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

# 效能基準（R2 大檔案、正則）——獨立可執行檔，非必跑 CTest
add_executable(bench_largefile benchmark/bench_largefile.cpp)
target_link_libraries(bench_largefile PRIVATE macpad_lib Qt6::Test)
