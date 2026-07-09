cmake_minimum_required(VERSION 3.21)

project(macpadpp
    VERSION 0.3.0
    DESCRIPTION "Notepad++-equivalent native macOS editor (Qt6 + QScintilla)"
    LANGUAGES CXX)

# --- 全域設定 -------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 依 profile §4.2 / rd-build-cmake：out-of-source 強制
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "禁止 in-source build。請用: cmake -S . -B build")
endif()

# Qt 自動化
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 警告視為 error（CI 門檻 NFR-009；本機開發可用 -DSTRICT_WARNINGS=OFF 關閉）
option(STRICT_WARNINGS "Treat warnings as errors" ON)

# --- 相依：Qt6 -----------------------------------------------------------
# QScintilla 依賴 PrintSupport；Widgets 為主體；Test 供單元測試（NFR-008）
# Core5Compat 提供 QTextCodec（傳統/區域編碼：Big5/GBK/Shift-JIS…，複刻 NP++ Character sets）
# WebEngineWidgets 供 Markdown 預覽外掛渲染（marked.js + mermaid.js 流程圖）
find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Core Gui Test Concurrent Network Core5Compat WebEngineWidgets Svg)

# --- 相依：QScintilla (Homebrew 無 CMake config，手動定位) ---------------
# 允許使用者以 -DQSCINTILLA_ROOT=... 覆寫；預設探測 Homebrew 前綴
if(NOT DEFINED QSCINTILLA_ROOT)
    execute_process(COMMAND brew --prefix qscintilla2
        OUTPUT_VARIABLE QSCINTILLA_ROOT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()

find_path(QSCINTILLA_INCLUDE_DIR
    NAMES Qsci/qsciscintilla.h
    HINTS "${QSCINTILLA_ROOT}/include" /opt/homebrew/opt/qscintilla2/include /usr/local/opt/qscintilla2/include)

find_library(QSCINTILLA_LIBRARY
    NAMES qscintilla2_qt6 libqscintilla2_qt6
    HINTS "${QSCINTILLA_ROOT}/lib" /opt/homebrew/opt/qscintilla2/lib /usr/local/opt/qscintilla2/lib)

if(NOT QSCINTILLA_INCLUDE_DIR OR NOT QSCINTILLA_LIBRARY)
    message(FATAL_ERROR
        "找不到 QScintilla。請先安裝: brew install qscintilla2\n"
        "  include: ${QSCINTILLA_INCLUDE_DIR}\n  lib: ${QSCINTILLA_LIBRARY}")
endif()
message(STATUS "QScintilla include: ${QSCINTILLA_INCLUDE_DIR}")
message(STATUS "QScintilla library: ${QSCINTILLA_LIBRARY}")

# --- 子目錄（分層 lib 目標，見 profile §4.1）-----------------------------
add_subdirectory(src)

# --- 測試 ---------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
