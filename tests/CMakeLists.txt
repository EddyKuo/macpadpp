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
    test_udlxmlio
    test_functionlistconfig
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
