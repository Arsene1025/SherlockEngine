# UiText.json 스크린샷 검사 (docs/plan/ui-canvas-plan.html 2단계). 배치는 make_ui_test_scenes.py 의 text_scene 과 같음.
# 씬은 저장소에 없음 — make_ui_test_scenes.py --verify-dir=<폴더> 로 만들고 verify_ui_stages.py 가 이 검사기를 같이 돌림.
#
# 실행: python check_ui_text.py <screenshot.png> [뷰 왼쪽 x] [뷰 위 y] [뷰 폭] [뷰 높이]
#   기본값은 1280×720 에디터 창의 씬 뷰 (232, 71, 732, 445). HUD 는 Constant Pixel Size ×1 이라 캔버스 단위 = 픽셀.
#
#   Blend  파란 판 위 빨간 글자: 판 안의 모든 픽셀이 G = 0, R + B = 255 ± 1 (감마 공간 혼합 · 글리프 분기 = 텍스처 R 이 알파)
#   White  검정 위 흰 글자: R = G = B
#   Align  3×3 상자의 "Ag" 잉크 상자 — 왼쪽/오른쪽 정렬은 상자 가장자리에서 0~4 px, 가운데는 중심 차 ≤ 1.5 px,
#          세로는 위·가운데·아래 줄의 위쪽 틈이 등차 (± 1 px). 같은 열·같은 행끼리 잉크 위치가 정확히 같음 (픽셀 격자 맞춤)
#   Wrap·Word  잉크가 상자 안 (말줄임·글자 단위 줄바꿈 후)
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

    def screen_box(x0, y0, x1, y1):
        # 캔버스 (y 위 +) → 스크린 픽셀 [sx0, sx1) × [sy0, sy1)
        return ox + x0, oy + (h - y1), ox + x1, oy + (h - y0)

    def ink_bbox(box, threshold, pad=0):
        sx0, sy0, sx1, sy1 = box
        sub = img[sy0 - pad:sy1 + pad, sx0 - pad:sx1 + pad]
        mask = sub.max(axis=2) > threshold
        ys, xs = np.nonzero(mask)
        if len(xs) == 0:
            return None
        return int(xs.min()) + sx0 - pad, int(ys.min()) + sy0 - pad, int(xs.max()) + 1 + sx0 - pad, int(ys.max()) + 1 + sy0 - pad

    # Blend: 판 (20, h−80)–(220, h−20)
    sx0, sy0, sx1, sy1 = screen_box(20, h - 80, 220, h - 20)
    panel = img[sy0 + 1:sy1 - 1, sx0 + 1:sx1 - 1]
    glyph = panel[:, :, 0] > 0
    edge = glyph & (panel[:, :, 0] < 255)
    err = np.abs(panel[:, :, 0] + panel[:, :, 2] - 255)
    check("Blend", int(err.max()) <= 1 and int(panel[:, :, 1].max()) <= 1 and edge.sum() > 50,
          f"글자 픽셀 {int(glyph.sum())} (경계 {int(edge.sum())}), |R+B−255| 최대 {int(err.max())}, G 최대 {int(panel[:, :, 1].max())}")

    # White: (20, h−150)–(220, h−100)
    sx0, sy0, sx1, sy1 = screen_box(20, h - 150, 220, h - 100)
    sub = img[sy0:sy1, sx0:sx1]
    spread = sub.max(axis=2) - sub.min(axis=2)
    check("White", int(spread.max()) <= 1 and (sub[:, :, 0] > 8).sum() > 50, f"잉크 {(sub[:, :, 0] > 8).sum()} 픽셀, 채널 차 최대 {int(spread.max())}")

    # Align: 상자 i = 행 r·열 c, 정렬 = i (r 세로, c 가로)
    inks = {}
    for i in range(9):
        r, c = divmod(i, 3)
        x0 = w - 20 - (2 - c) * 160 - 150
        top = h - 20 - r * 60
        box = screen_box(x0, top - 50, x0 + 150, top)
        ink = ink_bbox(box, 64, pad=4)
        inks[i] = (box, ink)
        if ink is None:
            check(f"Align {i}", False, "잉크 없음")
            continue
        if c == 0:
            d = ink[0] - box[0]
            check(f"Align {i} 가로", 0 <= d <= 4, f"왼쪽 틈 {d}")
        elif c == 2:
            d = box[2] - ink[2]
            check(f"Align {i} 가로", 0 <= d <= 4, f"오른쪽 틈 {d}")
        else:
            d = (ink[0] + ink[2]) / 2 - (box[0] + box[2]) / 2
            check(f"Align {i} 가로", abs(d) <= 1.5, f"중심 차 {d:+.1f}")
    for c in range(3):
        xs = {inks[r * 3 + c][1][0] for r in range(3) if inks[r * 3 + c][1]}
        check(f"Align 열 {c}", len(xs) == 1, f"잉크 왼쪽 x {sorted(xs)}")
    gaps = []
    for r in range(3):
        g = {inks[r * 3 + c][1][1] - inks[r * 3 + c][0][1] for c in range(3) if inks[r * 3 + c][1]}
        check(f"Align 행 {r}", len(g) == 1, f"위쪽 틈 {sorted(g)}")
        gaps.append(min(g))
    check("Align 세로", 0 <= gaps[0] <= 12 and abs((gaps[1] - gaps[0]) - (gaps[2] - gaps[1])) <= 1,
          f"위쪽 틈 위·가운데·아래 = {gaps}")

    # Wrap (20,20)–(240,110), Word (260,70)–(360,150): 잉크가 상자 안 (주변 12 px 까지 찾아봄)
    for name, (x0, y0, x1, y1) in (("Wrap", (20, 20, 240, 110)), ("Word", (260, 70, 360, 150))):
        box = screen_box(x0, y0, x1, y1)
        ink = ink_bbox(box, 64, pad=12)
        inside = ink is not None and ink[0] >= box[0] and ink[2] <= box[2] and ink[1] >= box[1] and ink[3] <= box[3]
        check(name, inside, f"상자 {box}, 잉크 {ink}")

    print("통과" if not failures else f"실패 {len(failures)}: {failures}")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
