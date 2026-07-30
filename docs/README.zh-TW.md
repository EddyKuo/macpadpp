# macpad++ 文件索引

[English](README.md) · **繁體中文**

所有文件皆有英文與繁體中文兩版。英文為主要版本，置於原本的檔案路徑；中文版以 `.zh-TW.md`
為後綴，兩者於頁首互相連結。

## 從這裡開始

| 文件 | 內容 | English |
|------|------|---------|
| [../README.zh-TW.md](../README.zh-TW.md) | 專案總覽、特色一覽、下載安裝 | [English](../README.md) |
| [../BUILD.zh-TW.md](../BUILD.zh-TW.md) | macOS 與 Windows 的原始碼建置、打包、疑難排解 | [English](../BUILD.md) |
| [parity.zh-TW.md](parity.zh-TW.md) | 與 Notepad++ v8.9.7 的逐項功能比對，以及未實作項目的誠實清單 | [English](parity.md) |

## 架構與開發

| 文件 | 內容 | English |
|------|------|---------|
| [design.zh-TW.md](design.zh-TW.md) | 完整設計文件：分層、元件圖與類別圖、時序圖、設計決策、逐 Sprint 沿革 | [English](design.md) |
| [plugin-development.zh-TW.md](plugin-development.zh-TW.md) | 如何撰寫擴充：協定 API、兩個實作範例、測試、常見陷阱 | [English](plugin-development.md) |
| [parity-audit.zh-TW.md](parity-audit.zh-TW.md) | 2026-07-08 的對等性稽核（204 項）、後續更正，以及雙平台重新認定 | [English](parity-audit.md) |

## Windows 移植（歷史契約）

移植已完成並合併，以下文件保留作為當初如何規格化的記錄。

| 文件 | 內容 | English |
|------|------|---------|
| [plan.zh-TW.md](plan.zh-TW.md) | 移植計畫：可移植性盤點、分階段執行、風險、工作量估計 | [English](plan.md) |
| [windows_prd.zh-TW.md](windows_prd.zh-TW.md) | 產品需求文件（PRD），含 Given/When/Then 驗收條件 | [English](windows_prd.md) |
| [windows_srs.zh-TW.md](windows_srs.zh-TW.md) | 軟體需求規格（SRS） | [English](windows_srs.md) |
| [windows_sa_sd.zh-TW.md](windows_sa_sd.zh-TW.md) | 系統架構與系統設計（SA/SD） | [English](windows_sa_sd.md) |
| [windows_design.zh-TW.md](windows_design.zh-TW.md) | 逐檔詳細設計（實作藍圖） | [English](windows_design.md) |

## 倉庫內其他文件

| 檔案 | 內容 | English |
|------|------|---------|
| [../THIRD-PARTY-NOTICES.zh-TW.md](../THIRD-PARTY-NOTICES.zh-TW.md) | 隨程式散布之第三方資源的授權聲明 | [English](../THIRD-PARTY-NOTICES.md) |
| [../LICENSE](../LICENSE) | 本專案自身授權（MIT） | — |

---

## 文件貢獻方式

修改文件時，**兩個語言版本都要更新**。兩版刻意保持同步：任一語言的讀者應獲得相同的事實，
而不是一份摘要與一份全文。

- 英文文件內的連結指向英文文件；中文文件內的連結指向 `.zh-TW.md` 對應版本。
- 語言切換列固定放在標題正下方，目前語言以粗體標示。
- 程式識別字、檔案路徑、CLI 旗標與 Mermaid 圖的結構在兩版中保持一致，僅翻譯敘述文字與圖上標籤。
