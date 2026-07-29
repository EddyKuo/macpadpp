"""合成「多份」類圖示：把同一個圖形位移疊放兩份，並在交界挖出實體空隙。

用途：圖示庫（Phosphor）不會為每個動作都附「All」變體。與其自己畫一個風格
不同的圖示，不如用庫裡既有的圖形合成——線寬、圓角、視覺重心自然一致。

為什麼一定要挖空隙：app 以 CompositionMode_SourceIn 把整張圖換成單一顏色，
只有 alpha 通道有意義。單純重疊會融成一塊實心色，交界完全消失。

為什麼把減法烘進 path 而不用 <mask>：實測 QtSvg 支援 <mask>，但那是版本相依的，
且 <clipPath> 不但不支援、還會把 clipPath 的內容當成圖形畫出來（多畫一塊）。
烘進 path 後只用到最基礎的 SVG 語法，跨 Qt 版本與平台都安全。

依賴（僅產生圖示時需要，不影響建置）：
    pip install skia-pathops fonttools

實際使用的參數（resources/icons/saveall.svg 即由此產生，可重現）：
    python3 scripts/icons/compose_stack.py \\
        resources/icons/save.svg resources/icons/saveall.svg 0.72 4 64 24

gap 的下限由最小工具列尺寸決定：圖示 viewBox 為 256 單位、工具列最小 16px，
即 16 單位 = 1 像素，故 gap 需 >= 16 才看得見；24 是留餘裕後的取值。
改動參數後請跑 test_icons，其中的連通元件檢查會驗證空隙是否仍然存在。
"""
import re, sys, pathops
from fontTools.svgLib.path.parser import parse_path
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

def read_d(path):
    src = open(path).read()
    return " ".join(re.findall(r'\sd="([^"]+)"', src))

def to_path(d, xform):
    p = pathops.Path()
    parse_path(d, TransformPen(p.getPen(), xform))
    return p

def dilate(src, amount):
    """向外擴張 amount：取 width=2*amount 的描邊外框，再與原形聯集。"""
    ring = pathops.Path()
    src.draw(ring.getPen())
    ring.stroke(amount * 2, pathops.LineCap.ROUND_CAP,
                pathops.LineJoin.ROUND_JOIN, 4.0)
    # skia 的圓角 join/cap 會產生 CONIC 節點，SVG path 沒有這種指令，先轉成二次曲線
    ring.convertConicsToQuads()
    out = pathops.Path()
    pathops.union([src, ring], out.getPen())
    return out

def to_d(p):
    pen = SVGPathPen(None)
    p.draw(pen)
    return pen.getCommands()

def stack(src_svg, scale, back_off, front_off, gap):
    d = read_d(src_svg)
    front = to_path(d, (scale, 0, 0, scale, front_off, front_off))
    back  = to_path(d, (scale, 0, 0, scale, back_off,  back_off))
    visible_back = pathops.Path()
    pathops.difference([back], [dilate(front, gap)], visible_back.getPen())
    out = pathops.Path()
    pathops.union([visible_back, front], out.getPen())
    return to_d(out)

def write(path, d):
    open(path, "w").write(
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" '
        'fill="#000000"><path d="%s"/></svg>\n' % d)

if __name__ == "__main__":
    src, out, scale, back, front, gap = sys.argv[1:7]
    write(out, stack(src, float(scale), float(back), float(front), float(gap)))
    print("wrote", out)
