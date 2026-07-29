#pragma once

// BuiltinLanguages — 內建語言定義表（對齊 Notepad++ Language 選單）
//
// QScintilla 只隨附約 30 個原生 lexer，但 Notepad++（Lexilla）支援約 160 個語言識別碼。
// 本表以既有的 UDL 引擎（UdlDefinition + UdlLexer）為通用 lexer，資料驅動地補上
// QScintilla 沒有原生 lexer 的語言：關鍵字群組、行/區塊註解、大小寫敏感、摺疊符。
//
// 與使用者自訂 UDL 的關係：使用者 UDL（UdlManager）優先，內建語言僅在使用者未定義
// 同副檔名時生效——使用者永遠可以覆寫內建行為。

#include <QString>
#include <QStringList>
#include <QVector>

#include "features/udl/UdlDefinition.h"

namespace macpad::features {

struct BuiltinLanguageEntry {
    QString key;             // 小寫語言鍵（Language 選單與 session 持久化用），如 "go"
    QString display;         // 選單顯示名，如 "Go"
    QStringList extensions;  // 不含點，全小寫
};

class BuiltinLanguages {
public:
    // 全部內建語言（依 display 排序），供 Language 選單與 Style Configurator 列舉
    static const QVector<BuiltinLanguageEntry> &entries();

    // 是否為內建語言鍵
    static bool contains(const QString &key);

    // 副檔名 → 語言鍵；查無回傳空字串
    static QString keyForSuffix(const QString &suffix);

    // 取得可餵給 UdlLexer 的定義；查無回傳 isValid()==false 的空定義
    static UdlDefinition definitionFor(const QString &key);
};

}  // namespace macpad::features
