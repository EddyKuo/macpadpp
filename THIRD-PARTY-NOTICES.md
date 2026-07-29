# 第三方資源授權（Third-Party Notices）

本專案自身授權見 [LICENSE](LICENSE)。以下為隨程式一同散布的第三方資源，
依其授權條款保留原始聲明。

---

## Phosphor Icons — 工具列圖示

`resources/icons/*.svg`（不含 `app-icon.svg`，該檔為本專案自製）

- 來源：<https://github.com/phosphor-icons/core>
- 授權：MIT
- 修改說明：僅將 `fill="currentColor"` 取代為明確色值 `#000000`。
  Qt 的 `QSvgRenderer` 不解析 `currentColor`，未處理會算繪不出圖形；
  實際顯示顏色由程式在執行期依主題以 SourceIn 重新填色，故此色值不影響外觀。
  圖形本身未經修改。

```
MIT License

Copyright (c) 2023 Phosphor Icons

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
