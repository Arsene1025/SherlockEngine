# UiAnchors.json 스크린샷에서 UI 사각형을 찾아 식(docs/plan/ui-canvas-plan.html 2.1·2.2)과 비교함.
# 씬은 저장소에 없음 — make_ui_test_scenes.py --verify-dir=<폴더> 로 만들고 verify_ui_stages.py 가 이 검사기를 같이 돌림.
#
# 실행: python check_ui_rects.py <screenshot.png> [뷰 왼쪽 x] [뷰 위 y] [뷰 폭] [뷰 높이] [--match=0.5] [--full]
#   --match: "Scaled" 캔버스의 match 를 바꾼 씬을 찍었을 때. --full: 게임(백버퍼) 스크린샷 — 가려지는 줄이 없음
#   기본값은 1280×720 에디터 창의 씬 뷰 (232, 71, 732, 445) — 폭·높이는 로그의 "씬 뷰 텍스처 W×H" 와 같아야 함.
#   (에디터의 창 클립 때문에 이미지의 맨 왼쪽 열·오른쪽 열·아래 행이 1 px 씩 가려져 보임 — 그 줄은 비교에서 뺌)
# 사각형은 정확한 색(빨강·초록·파랑·노랑·자홍)의 경계 상자로 찾음. 반투명 Half 는 검정 위 흰색 0.5 → 128 ± 1 인지 봄.
import math
import sys

sys.stdout.reconfigure(encoding="utf-8")

import numpy as np
from PIL import Image


def bbox(img, color, region=None):
    mask = np.all(img == np.array(color), axis=2)
    if region is not None:
        x0, y0, x1, y1 = region
        sub = np.zeros_like(mask)
        sub[y0:y1, x0:x1] = mask[y0:y1, x0:x1]
        mask = sub
    ys, xs = np.nonzero(mask)
    if len(xs) == 0:
        return None
    return int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1   # 끝은 포함하지 않음


def main():
    flags = [a for a in sys.argv[1:] if a.startswith("--")]
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    path = args[0]
    ox, oy, w, h = (int(v) for v in (args[1:5] if len(args) >= 5 else (232, 71, 732, 445)))
    match = next((float(f.split("=", 1)[1]) for f in flags if f.startswith("--match=")), 0.5)
    img = np.asarray(Image.open(path).convert("RGB")).astype(int)
    if "--full" in flags:
        view = (ox, oy, ox + w, oy + h)
    else:
        view = (ox + 1, oy, ox + w - 1, oy + h - 1)   # 에디터가 가리는 줄 제외

    def screen(x0, y0, x1, y1):
        # 캔버스(y 위 +, 원점 왼쪽 아래) → 스크린샷 픽셀 (원점 왼쪽 위)
        return ox + x0, oy + h - y1, ox + x1, oy + h - y0

    s = 2.0 ** (math.log2(w / 1920.0) + (math.log2(h / 1080.0) - math.log2(w / 1920.0)) * match)   # 2.2 CanvasScaler
    cx, cy = w / 2.0, h / 2.0
    expect = {
        "BL (빨강)": ((255, 0, 0), screen(10, 10, 110, 60)),
        "TR (초록)": ((0, 255, 0), screen(w - 90, h - 50, w - 10, h - 10)),
        "Bar (파랑)": ((0, 0, 255), screen(0.25 * w, cy - 10, 0.75 * w, cy + 10)),
        "Child (노랑)": ((255, 255, 0), screen(cx - 100, cy - 80 - 50, cx - 80, cy - 80 - 30)),
        "Corner (자홍)": ((255, 0, 255), screen(0, h - 108 * s, 192 * s, h)),
    }
    failed = 0
    for name, (color, rect) in expect.items():
        found = bbox(img, color, view)
        # 래스터화 규칙: 픽셀 중심(i + 0.5)이 [시작, 끝) 안이면 칠함 → 첫 픽셀 ceil(시작 − 0.5), 끝 ceil(끝 − 0.5). 가려진 줄로 자름.
        want = [math.ceil(v - 0.5) for v in rect]
        want = [max(want[0], view[0]), max(want[1], view[1]), min(want[2], view[2]), min(want[3], view[3])]
        exact = found is not None and list(found) == want
        failed += 0 if exact else 1
        print(f"{name:14s} 식 {tuple(round(v, 2) for v in rect)} → 픽셀 {tuple(want)} 찾음 {found} {'정확' if exact else '틀림'}")

    # 감마 공간 블렌딩: Half(흰색 α 0.5) 가 검정(Back) 위 → 0.5·255 = 127.5. Child 를 피해 가운데에서 잼.
    hx0, hy0, hx1, hy1 = screen(cx - 100 + 30, cy - 80 - 50 + 10, cx + 100 - 10, cy - 80 + 50 - 10)
    patch = img[int(hy0):int(hy1), int(hx0):int(hx1)]
    lo, hi = int(patch.min()), int(patch.max())
    blend_ok = 127 <= lo and hi <= 129
    failed += 0 if blend_ok else 1
    print(f"Half 블렌딩    값 범위 {lo}..{hi} (기대 128 ± 1) {'맞음' if blend_ok else '틀림'}  (선형 공간 블렌딩이었다면 약 188)")
    print(f"뷰 {w}×{h}, match {match}, 배율 {s:.5f}")
    print("결과:", "통과" if failed == 0 else f"실패 {failed}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
