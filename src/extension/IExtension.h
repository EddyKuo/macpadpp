#pragma once

// 擴充協定（extension protocol）—— FR-035 / IR-004 / CON-007
// v1 僅定義介面並凍結為向前相容基準（v2 據此載入外部外掛）。v1 不載入外部程式碼，
// 僅供內建功能 dogfood 掛載，驗證核心可擴充性。
//
// 相容性：本介面為 CON-007 凍結基準。任何破壞性變更須升 Major 並提供遷移。

#include <functional>

#include <QString>

class QWidget;

namespace macpad::core { class EditorWidget; }

namespace macpad::extension {

// 能力宣告（外掛自我描述）
struct ExtensionCapabilities {
    QString id;       // 唯一識別，如 "builtin.wordcount"
    QString name;     // 顯示名稱
    QString version;  // SemVer
};

// 宿主服務：外掛對編輯核心的「受限」存取（不暴露完整內部狀態）
class IHostServices {
public:
    virtual ~IHostServices() = default;

    // 目前作用中的編輯器（可能為 nullptr）
    virtual macpad::core::EditorWidget *activeEditor() = 0;
    // 在指定選單新增一個動作
    virtual void addMenuAction(const QString &menuTitle, const QString &text,
                               std::function<void()> callback) = 0;
    // 狀態列訊息
    virtual void showStatusMessage(const QString &message, int timeoutMs = 3000) = 0;
    // 宿主主視窗（QMainWindow）——供進階外掛掛載自己的 UI（如停靠面板）。
    // additive（Minor）：既有僅用選單/狀態列的外掛不受影響。
    virtual QWidget *hostWindow() = 0;
};

// 外掛介面（純虛擬 + 生命週期 hook）
class IExtension {
public:
    virtual ~IExtension() = default;

    virtual ExtensionCapabilities capabilities() const = 0;

    // 生命週期：載入時取得宿主服務；卸載時釋放資源
    virtual void onLoad(IHostServices *host) = 0;
    virtual void onUnload() = 0;
};

}  // namespace macpad::extension
