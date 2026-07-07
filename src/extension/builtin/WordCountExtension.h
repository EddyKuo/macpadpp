#pragma once

// WordCountExtension — 內建 dogfood 擴充（FR-035 AC2）
// 透過 extension protocol 掛載一個「字數統計」動作，證明核心可經協定擴充。

#include "extension/IExtension.h"

namespace macpad::extension {

class WordCountExtension : public IExtension {
public:
    ExtensionCapabilities capabilities() const override;
    void onLoad(IHostServices *host) override;
    void onUnload() override;

private:
    IHostServices *m_host = nullptr;
};

}  // namespace macpad::extension
