#pragma once

// LexerFactory — 依副檔名建立對應的 QScintilla lexer（FR-003 語法高亮）
// v1 起始支援常用語言子集；未知副檔名回傳 nullptr（純文字）。
// 未來 UDL（FR-032）與完整語言對應表（DR-007）於此擴充。

#include <QString>
#include <QVector>

class QsciLexer;
class QObject;

namespace macpad::core {

struct LanguageEntry {
    QString displayName;  // 選單顯示，如 "C++"
    QString key;          // 內部鍵，如 "cpp"；空字串代表 Plain Text
};

class LexerFactory {
public:
    // 依副檔名（不含點，或含點皆可）回傳新建 lexer；呼叫端接管所有權（設 parent）。
    // 回傳 nullptr 代表純文字（無 lexer）。
    static QsciLexer *createForExtension(const QString &suffix, QObject *parent);

    // 依完整檔名/路徑推斷。
    static QsciLexer *createForFileName(const QString &fileName, QObject *parent);

    // 依語言鍵手動建立 lexer（供 Language 選單手動選擇）；空鍵回傳 nullptr（純文字）。
    static QsciLexer *createForLanguage(const QString &key, QObject *parent);

    // 支援的語言清單（供選單），依顯示名排序。
    static QVector<LanguageEntry> languages();
};

}  // namespace macpad::core
