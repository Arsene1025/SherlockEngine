# UiFonts.json 스크린샷 검사 (docs/plan/ui-canvas-plan.html 2단계). 배치는 make_ui_test_scenes.py 의 UiFonts 부분과 같음.
# 씬은 저장소에 없음 — make_ui_test_scenes.py --verify-dir=<폴더> 로 만들고 verify_ui_stages.py 가 이 검사기를 같이 돌림.
#
# 실행: python check_ui_fonts.py <screenshot.png> [뷰 왼쪽 x] [뷰 위 y] [뷰 폭] [뷰 높이]   (기본값: 에디터 씬 뷰 232, 71, 732, 445)
#   FontKarla 와 FontDefault 의 잉크가 다름 (프로젝트 .ttf 를 읽었고, 같은 픽셀 크기라도 아틀라스 키에 글꼴이 들어감)
#   FontMissing = FontDefault (60 px 아래로 옮긴 같은 그림)
#   Spacing2 의 두 'A' 위쪽 간격 = Spacing1 의 두 배 (± 1 px)
#   NoWrap 은 한 줄로 상자(오른쪽 끝 x = 100) 밖까지
#   WrapEdge (300,20)–(400,120) 의 잉크가 상자 안 (가로)
import sys

sys.stdout.reconfigure(encoding="utf-8")

import numpy as np
from PIL import Image

failures = []


def check(name, ok, detail):
    print(("OK  " if ok else "FAIL") + f" {name}: {detail}")
    if not ok:
        failures.append(name)


def main():
    args = sys.argv[1:]
    img = np.asarray(Image.open(args[0]).convert("RGB")).astype(int)
    ox, oy, w, h = (int(v) for v in (args[1:5] if len(args) >= 5 else (232, 71, 732, 445)))
    lum = img.max(axis=2)

    def crop(x0, y0, x1, y1):   # 캔버스 (y 위 +) 사각형
        return lum[oy + (h - y1):oy + (h - y0), ox + x0:ox + x1]

    rows = [crop(20, h - 20 - r * 60 - 50, 320, h - 20 - r * 60) for r in range(3)]
    karla, missing, default = rows
    check("Karla ≠ 기본", (np.abs(karla - default) > 32).sum() > 200 and (karla > 64).sum() > 200,
          f"다른 픽셀 {(np.abs(karla - default) > 32).sum()}, Karla 잉크 {(karla > 64).sum()}")
    check("없는 글꼴 = 기본", int(np.abs(missing - default).max()) == 0 and (default > 64).sum() > 200,
          f"최대 차 {int(np.abs(missing - default).max())}, 잉크 {(default > 64).sum()}")

    def tops(region):
        ink_rows = np.nonzero((region > 64).any(axis=1))[0]
        starts = [int(ink_rows[0])] + [int(b) for a, b in zip(ink_rows[:-1], ink_rows[1:]) if b - a > 1]
        return starts

    t1 = tops(crop(360, h - 220, 460, h - 20))
    t2 = tops(crop(480, h - 220, 580, h - 20))
    ok = len(t1) == 2 and len(t2) == 2 and abs((t2[1] - t2[0]) - 2 * (t1[1] - t1[0])) <= 1
    check("줄 간격", ok, f"간격 1 → {t1}, 간격 2 → {t2}")

    region = crop(20, 30, 290, 90)   # 오른쪽은 WrapEdge 상자(300~) 앞까지
    ys, xs = np.nonzero(region > 64)
    ok = len(xs) > 0 and xs.max() + 20 > 100 + 50 and ys.max() - ys.min() < 30
    check("줄바꿈 끔", ok, f"잉크 x {20 + xs.min()}..{20 + xs.max()}, 높이 {ys.max() - ys.min() + 1}" if len(xs) else "잉크 없음")

    region = crop(290, 20, 420, 120)
    ys, xs = np.nonzero(region > 64)
    ok = len(xs) > 0 and 290 + xs.min() >= 300 and 290 + xs.max() < 400
    check("줄바꿈 경계", ok, f"잉크 x {290 + xs.min()}..{290 + xs.max()} (상자 300..399)" if len(xs) else "잉크 없음")

    print("통과" if not failures else f"실패 {len(failures)}: {failures}")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
