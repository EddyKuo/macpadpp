#pragma once

// UdlXmlIo — Notepad++ userDefineLang.xml 匯入/匯出（FR-059 擴充：NPP 相容性）
//
// 讀寫 Notepad++「Language ▸ Define your language...」匯出的 <NotepadPlus><UserLang>...
// 文件格式，並與既有 UdlDefinition 模型互轉。使用 QXmlStreamReader/Writer（純 Qt6::Core，
// 無需額外 Xml module）。對缺欄位、非標準色碼寬容處理，不因單一欄位缺失而整體失敗。
//
// NPP 檔案格式要點（依官方文件與範例檔重建，寬容解析，非逐位元組保證相容）：
//   <KeywordLists><Keywords name="Comments">00{line} 01{blockStart} 02{blockEnd}</Keywords>
//   <Keywords name="Keywords1..8">space-separated 關鍵字</Keywords>
//   <Keywords name="Operators1">space-separated 運算子</Keywords>
//   <Keywords name="Folders in code1, open|middle|close">...</Keywords>
//   <Keywords name="Delimiters">00{open0}01{escape0}02{close0}03{open1}...</Keywords>
//     （每組分隔符佔用 3 個依序遞增的兩位數代碼：open/escape/close，最多 8 組）
//   <Styles><WordsStyle name="..." styleID="N" fgColor="RRGGBB或十進位BBGGRR"
//            bgColor="..." fontStyle="bit0=bold,bit1=italic,bit2=underline" /></Styles>
//   styleID 採用與 UdlLexer::Style 相同的編號慣例（本專案自訂對應，匯出/匯入互為一致）。

#include <QString>

namespace macpad::features {

struct UdlDefinition;

class UdlXmlIo {
public:
    // 解析 Notepad++ userDefineLang.xml（單一 <UserLang> 或含多個時取第一個，
    // 若指定 langName 非空則挑選符合 name 屬性者）。解析失敗（無法開檔/XML 格式錯誤/
    // 找不到 UserLang 節點）回傳 isValid()==false 的 UdlDefinition。
    static UdlDefinition importFromXml(const QString &path, const QString &langName = QString());

    // 將 UdlDefinition 序列化為 Notepad++ userDefineLang.xml 並寫入 path。
    static bool exportToXml(const UdlDefinition &def, const QString &path);
};

}  // namespace macpad::features
