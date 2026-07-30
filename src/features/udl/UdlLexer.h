#pragma once

// UdlLexer — 依 UdlDefinition 著色的自訂 lexer（FR-032, FR-059）
// 樣式：Default / Keyword(群組0~7) / Comment / String / Number / Operator / Delimiter。
// 另依 folderTokens 產生 fold points。

#include <Qsci/qscilexercustom.h>

#include "features/udl/UdlDefinition.h"

namespace macpad::features {

class UdlLexer : public QsciLexerCustom {
    Q_OBJECT
public:
    enum Style {
        Default = 0,
        Keyword = 1,    // 關鍵字群組 0（沿用既有樣式編號，向後相容）
        Comment = 2,
        String = 3,
        Number = 4,
        Keyword2 = 5,   // 關鍵字群組 1
        Keyword3 = 6,   // 關鍵字群組 2
        Keyword4 = 7,   // 關鍵字群組 3
        Keyword5 = 8,   // 關鍵字群組 4
        Keyword6 = 9,   // 關鍵字群組 5
        Keyword7 = 10,  // 關鍵字群組 6
        Keyword8 = 11,  // 關鍵字群組 7
        Operator = 12,
        Delimiter = 13
    };

    UdlLexer(const UdlDefinition &def, QObject *parent = nullptr);

    const char *language() const override { return m_langName.constData(); }
    QString description(int style) const override;
    void styleText(int start, int end) override;
    QColor defaultColor(int style) const override;

private:
    // 掃描一次 styleText 內用得到的、預先轉成 UTF-8 的樣板（避免在迴圈中重複轉碼）
    struct ScanTables {
        QByteArray lineComment;
        QByteArray blockOpen;
        QByteArray blockClose;
        struct Delim { QByteArray open, escape, close; int nesting = 0; };
        QVector<Delim> delims;
        QList<QByteArray> operators;   // 依長度遞減，確保最長匹配優先
    };

    // 巢狀遞迴深度上限。UDL 允許區塊巢狀包含自己（例如可巢狀的區塊註解），
    // 若不設限，一份含大量未閉合開頭標記的文件會讓每個開頭標記各佔一層 C++ 堆疊，
    // 且 styleText() 每次按鍵都從頭重掃 —— 正常編輯就足以爆堆疊。
    // 超過上限即退回逐位元組上色（畫面仍正確，只是不再往下辨識巢狀內容）。
    static constexpr int kMaxNestDepth = 64;
    // 可被 nesting 遮罩指涉的分隔符組數上限（上游格式為 8 組）。超出者不參與巢狀判定。
    static constexpr int kMaxNestableDelimiters = 8;

    // 在 utf8[i] 處嘗試辨識一個 token 並上色，回傳消耗的位元組數（至少 1）。
    // mask 為 UdlNest 位元遮罩，決定此處允許辨識哪些類別——巢狀區塊內部即以較窄的
    // mask 遞迴呼叫本函式，未獲允許的類別會退回以 fallbackStyle 逐位元組上色。
    int scanToken(const QByteArray &utf8, int i, int end, const ScanTables &t,
                  int mask, int fallbackStyle, int depth);
    // 掃描一段有明確結束標記的區塊（註解／分隔符／字串）：開頭與結尾標記以 bodyStyle 上色，
    // 中間內容依 nesting 遞迴。回傳消耗的位元組數。
    int scanRegion(const QByteArray &utf8, int i, int end, const ScanTables &t,
                   const QByteArray &openTok, const QByteArray &closeTok,
                   const QByteArray &escapeTok, int nesting, int bodyStyle, int depth);

    // 第 groupIdx（0-based，最多 8）組關鍵字對應的樣式編號
    static int styleForKeywordGroup(int groupIdx);
    // 依 folderTokens 掃描整份文件、設定 fold points
    void applyFolding(const QString &text);
    // 依 UdlDefinition::styles 套用使用者自訂顏色/粗體/斜體/底線（③a UDL Styler）；
    // 未設定的樣式維持 defaultColor() 內建預設（向後相容）。
    void applyUserStyles();

    UdlDefinition m_def;
    QByteArray m_langName;
};

}  // namespace macpad::features
