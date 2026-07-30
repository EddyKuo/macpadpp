#pragma once

// ApiFileStore — 讀取 Notepad++ 相容的 API 定義檔（plugins/APIs/<lang>.xml 格式）
//
// Notepad++ 的函式參數提示（call tip）與自動完成清單來自可由使用者自行擴充的 API 檔；
// 本專案先前只有內建的 ApiDatabase 硬編表 + 當前文件掃描，故跨檔案的函式簽名叫不出來，
// 使用者也無從擴充。此模組補上外部 API 檔，達成與上游對等。
//
// 檔案位置：設定目錄下的 apis/<langKey>.xml（langKey 與 core/LexerFactory 一致）。
// 格式（與 Notepad++ 相同，可直接沿用其既有 API 檔）：
//   <NotepadPlus>
//     <AutoComplete language="C++">
//       <Environment ignoreCase="no" startFunc="(" stopFunc=")" paramSeparator="," terminal=";"/>
//       <KeyWord name="fopen" func="yes">
//         <Overload retVal="FILE *" descr="開啟檔案">
//           <Param name="const char *filename" />
//           <Param name="const char *mode" />
//         </Overload>
//       </KeyWord>
//     </AutoComplete>
//   </NotepadPlus>
//
// 純解析邏輯，無 GUI 相依，可完整單元測試。

#include <QString>
#include <QStringList>
#include <QVector>

namespace macpad::features {

// 單一關鍵字/函式的 API 條目
struct ApiEntry {
    QString name;
    bool isFunction = false;
    // 每個多載一行的簽名文字（已組好，可直接送 call tip）；非函式時為空
    QStringList overloads;
};

class ApiFileStore {
public:
    // 解析指定 XML 檔；失敗或格式不符回傳空 vector（不拋例外、不臆測）
    static QVector<ApiEntry> parseFile(const QString &path);
    // 解析 XML 內容（供單元測試免落地檔案）
    static QVector<ApiEntry> parseXml(const QByteArray &xml);

    // 取得某語言的 API 條目；結果會快取，同一語言只解析一次。
    // 檔案不存在時回傳空 vector——此為正常情況（使用者未提供 API 檔）。
    static const QVector<ApiEntry> &entriesFor(const QString &langKey);

    // 某語言的 API 檔預期路徑（設定目錄下 apis/<langKey>.xml）
    static QString filePathFor(const QString &langKey);

    // 清除快取（供測試，或使用者更換 API 檔後重新載入）
    static void clearCache();
};

}  // namespace macpad::features
