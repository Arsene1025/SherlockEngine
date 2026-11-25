# UiWidgets 검증 씬 (make_ui_test_scenes.py --verify-dir) 스크린샷의 픽셀 검사 (4단계).
#   python check_ui_widgets.py <에디터 스크린샷 PNG>
# 씬 뷰 영역 (232, 71, 732×445) 기준.
#   9-slice: Big (300×120) 과 Small (60×30) 은 같은 테두리 12 — 네 모서리 12×12 픽셀이 같아야 함 (늘어나지 않음)
#   Filled 가로 0.3: 200 폭 중 왼쪽 60 px 만 칠해짐
#   Filled 세로 0.5: 60 높이 중 아래 30 px 만 칠해짐
#   fillCenter 끔: NoCenter 가운데는 배경색
import sys
from PIL import Image

OX, OY = 232, 71


def main():
    im = Image.open(sys.argv[1]).convert("RGB")
    px = lambda x, y: im.getpixel((OX + x, OY + y))
    ok = True

    def check(name, cond):
        nonlocal ok
        print(("OK   " if cond else "FAIL ") + name)
        ok = ok and cond

    def block(x0, y0, w, h):
        return [px(x0 + i, y0 + j) for j in range(h) for i in range(w)]

    def diff(a, b):
        return max(max(abs(p - q) for p, q in zip(u, v)) for u, v in zip(a, b))

    big = (20, 20, 300, 120)
    small = (360, 20, 60, 30)
    for cname, (fx, fy) in (("좌상", (0, 0)), ("우상", (1, 0)), ("좌하", (0, 1)), ("우하", (1, 1))):
        bx = big[0] + fx * (big[2] - 12)
        by = big[1] + fy * (big[3] - 12)
        sx = small[0] + fx * (small[2] - 12)
        sy = small[1] + fy * (small[3] - 12)
        d = diff(block(bx, by, 12, 12), block(sx, sy, 12, 12))
        check(f"9-slice {cname} 모서리 12×12 같음 (최대 차 {d})", d <= 2)
    # 모서리 바깥쪽 끝은 투명(배경), 가운데는 채움색
    bg = px(5, 5)
    check("9-slice 좌상 끝 픽셀이 배경", diff([px(20, 20)], [bg]) <= 8)
    check("9-slice 가운데가 채움색", px(170, 80)[0] > 200)

    green = lambda p: p[1] > 180 and p[0] < 120
    row = [green(px(x, 180)) for x in range(15, 230)]
    filled = sum(row)
    check(f"Filled 가로 0.3: 칠한 폭 {filled} px (기대 60)", abs(filled - 60) <= 1)
    check("Filled 가로 0.3: 왼쪽부터", row[5] and not row[80])

    red = lambda p: p[0] > 180 and p[1] < 120
    col = [red(px(255, y)) for y in range(160, 230)]
    filled = sum(col)
    check(f"Filled 세로 0.5: 칠한 높이 {filled} px (기대 30)", abs(filled - 30) <= 1)
    check("Filled 세로 0.5: 아래부터", col[-8] and not col[8])

    check("fillCenter 끔: NoCenter 가운데가 배경", diff([px(500, 80)], [bg]) <= 8)
    check("fillCenter 끔: NoCenter 테두리는 칠함", px(445, 80)[1] > 200)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
