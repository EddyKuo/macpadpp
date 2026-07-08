#pragma once

// CliArgs — 命令列參數解析（FR-033, IR-005, FR-051）
// 解析 `path`、`path:line` 或旗標（-n<line> -c<col> -ro -r -nosession -multiInst
// -p<pos> -l<lang> -alwaysOnTop -title:<text> -titleAdd:<text> -quickPrint -openSession
// -openFoldersAsWorkspace -x -y -monitor -notabbar -fullReadOnly -settingsDir -L<langCode>
// -udl=<name> -z -notepadStyleCmdline）；純邏輯、可單元測試。
// 不可辨識的 "-" 開頭旗標一律忽略，不視為檔案路徑。
// 部分 Windows-only / 非適用旗標（-systemtray -noPlugin -pluginMessage -loadingTime
// -qn -qt -qf -qSpeed 等 ghost-typing 除錯旗標）僅辨識並吞噬，不在 macOS 上有實際作用。

#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace macpad::features {

struct FileArg {
    QString path;
    int line = 0;    // 0 = 未指定
    int column = 0;  // 0 = 未指定（FR-051 新增）
};

// FR-051：完整命令列解析結果
struct ParsedArgs {
    QVector<FileArg> files;
    bool readOnly = false;
    bool noSession = false;
    bool multiInstance = false;
    int gotoLine = 0;
    int gotoColumn = 0;
    int gotoPos = 0;             // -p<pos>：以原始字元位置定位（0 = 未指定）
    QString forceLanguage;       // -l<lang>：強制指定開啟檔案的語言
    bool alwaysOnTop = false;    // -alwaysOnTop
    QString titleAdd;            // -title:<text> / -titleAdd:<text>：附加視窗標題文字
    bool quickPrint = false;     // -quickPrint

    // ---- Notepad++ 相容旗標（macOS 適用子集）----
    QString openSessionPath;              // -openSession <file>：啟動時開啟指定 session 檔
    QStringList openFoldersAsWorkspace;   // -openFoldersAsWorkspace <folder>：作為工作區資料夾開啟（可重複出現）
    std::optional<int> windowX;           // -x <n>：視窗初始 X 座標
    std::optional<int> windowY;           // -y <n>：視窗初始 Y 座標
    bool monitorMode = false;             // -monitor：監控模式（外部檔案異動自動重載）
    bool hideTabBar = false;              // -notabbar：啟動時隱藏分頁列
    bool fullReadOnly = false;            // -fullReadOnly：完全唯讀（較 -ro 更嚴格，含選單鎖定）
    QString settingsDir;                  // -settingsDir <dir>：自訂設定檔存放目錄
    QString uiLangCode;                   // -L<langCode>：介面語言代碼（大寫 L，區分 -l<lang> 語法高亮）
    QString udlName;                      // -udl=<name>：套用自訂使用者定義語言（UDL）
    bool notepadStyleCmdline = false;     // -notepadStyleCmdline：Notepad 風格命令列相容模式

    // ---- Windows-only / 非適用旗標：僅辨識並吞噬，避免誤判為檔案路徑 ----
    bool systemTrayIgnored = false;       // -systemtray（macOS 無系統匣，無作用）
    bool noPluginIgnored = false;         // -noPlugin（macOS 外掛機制不同，無作用）
    bool pluginMessageIgnored = false;    // -pluginMessage <msg>（外掛間通訊，無對應機制）
    bool loadingTimeIgnored = false;      // -loadingTime <ms>（啟動計時除錯旗標，無作用）
    bool ghostTypingIgnored = false;      // -qn/-qt/-qf/-qSpeed 等 ghost-typing 除錯旗標，無作用
};

class CliArgs {
public:
    // 解析單一檔案參數；尾端 ":<digits>" 視為行號（避免誤判 Windows 磁碟代號）
    static FileArg parseFileArg(const QString &arg);

    // FR-051：解析完整命令列引數清單，辨識旗標並將其餘視為檔案路徑
    static ParsedArgs parse(const QStringList &args);
};

}  // namespace macpad::features
