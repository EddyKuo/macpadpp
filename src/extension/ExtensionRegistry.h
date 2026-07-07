#pragma once

// ExtensionRegistry — 擴充註冊表（FR-035）
// v1：僅管理內建（in-process）擴充的生命週期；不做動態載入（v2）。

#include <memory>
#include <vector>

#include <QVector>

#include "extension/IExtension.h"

namespace macpad::extension {

class IHostServices;

class ExtensionRegistry {
public:
    explicit ExtensionRegistry(IHostServices *host);
    ~ExtensionRegistry();

    // 註冊並載入一個擴充（接管所有權，立即呼叫 onLoad）
    void load(std::unique_ptr<IExtension> ext);

    // 卸載全部（呼叫 onUnload）
    void unloadAll();

    std::size_t count() const { return m_extensions.size(); }

    // 已載入擴充的能力宣告清單（供 Plugin Manager，FR-037）
    QVector<ExtensionCapabilities> capabilitiesList() const;

private:
    IHostServices *m_host;
    std::vector<std::unique_ptr<IExtension>> m_extensions;
};

}  // namespace macpad::extension
