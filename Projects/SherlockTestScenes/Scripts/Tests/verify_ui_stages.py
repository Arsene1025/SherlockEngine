# 게임 UI 3~8단계 자동 검증 (docs/plan/ui-canvas-plan.html 10장). 에디터를 자동화 인자로 띄우고 로그·스크린샷을 기대값과 비교함.
#
#   python verify_ui_stages.py [--config=Debug|Release] [--backend=d3d11|d3d12] [--dir=<검증 폴더>] [--only=click,nav,...]
#
# 검증 씬은 make_ui_test_scenes.py --verify-dir 로 그 폴더에 만듦 (저장소에 넣지 않음). 쇼케이스는 Assets/Scenes/UiShowcase.json.
# 좌표는 1280×720 에디터 창의 씬 뷰 (732×445) 픽셀. 1~2단계 검사기(check_ui_rects/text/fonts)도 같이 돌림 —
# check_ui_fonts 는 Assets/UiTestFonts/Karla-Regular.ttf 가 있을 때만 (시험 때만 imgui misc/fonts 에서 복사).
import argparse
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.normpath(os.path.join(HERE, "..", ".."))
ROOT = os.path.normpath(os.path.join(PROJECT, "..", ".."))
SHERLOCK = os.path.join(PROJECT, "SherlockTestScenes.sherlock")

results = []


def report(name, ok, detail=""):
    results.append((name, ok))
    print(("PASS " if ok else "FAIL ") + name + (f" — {detail}" if detail else ""), flush=True)


class Runner:
    def __init__(self, config, backend, folder):
        self.exe = os.path.join(ROOT, "x64", config, "SherlockEditor.exe")
        self.log = os.path.join(ROOT, "x64", config, "Logs", "SherlockEngine.log")
        self.backend = backend
        self.dir = folder

    def scene(self, name):
        return os.path.join(self.dir, name + ".json")

    def run(self, scene, *args, exit_after=60):
        cmd = [self.exe, f"--project={SHERLOCK}", f"--scene={scene}", f"--backend={self.backend}", f"--exit-after={exit_after}"] + list(args)
        code = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=300).returncode
        if code != 0:
            report(f"종료 코드 0 ({os.path.basename(scene)} {' '.join(args)[:60]})", False, f"종료 코드 {code}")
        with open(self.log, encoding="utf-8", errors="replace") as f:
            lines = [re.sub(r"^\[\s*[\d.]+\] \[[^\]]+\] ", "", l.rstrip("\n")) for l in f]
        bad = [l for l in lines if "D3D12 ERROR" in l or "CORRUPTION" in l or "씬 로드 실패" in l]
        if bad:
            report(f"로그 오류 없음 ({os.path.basename(scene)})", False, bad[0][:120])
        return lines


def says(lines, prefix="UiClickCounter"):
    return [l for l in lines if l.startswith(prefix)]


def rects(lines):
    out = []
    for l in lines:
        m = re.search(r"rect (.+?) '(.+?)' pos \(([-\d.]+), ([-\d.]+)\) size \(([-\d.]+), ([-\d.]+)\).*?screen\s+\(([-\d.]+), ([-\d.]+)\) \(([-\d.]+), ([-\d.]+)\) \(([-\d.]+), ([-\d.]+)\) \(([-\d.]+), ([-\d.]+)\)", l)
        if m:
            v = [float(x) for x in m.groups()[2:]]
            out.append({"why": m.group(1), "name": m.group(2), "pos": v[0:2], "size": v[2:4], "quad": [v[4:6], v[6:8], v[8:10], v[10:12]]})
    return out


def near(a, b, eps=0.02):
    return all(abs(x - y) <= eps for x, y in zip(a, b))


def expect_seq(name, got, want):
    report(name, got == want, "" if got == want else f"기대 {want} / 실제 {got}")


# ---------------------------------------------------------------- 단계별

def t_click(r):
    clicks = "--ui-click=60,405@20;140,400@25;60,325@30;60,265@35;380,405@40;600,100@45"
    want = ["UiClickCounter 'Counter A': count 1", "UiClickCounter 'Counter B': count 1", "UiClickCounter 'Counter B': count 2"]
    expect_seq("3 클릭·겹침·꺼짐·가림·정렬 순서·바깥", says(r.run(r.scene("UiClick"), "--play=1", clicks)), want)
    saved = r.scene("UiClickSaved")
    r.run(r.scene("UiClick"), f"--save={saved}", exit_after=20)
    expect_seq("3 저장·다시 열기 뒤 연결", says(r.run(saved, "--play=1", clicks)), want)
    lines = r.run(r.scene("UiClick"), "--reload-scripts=1", "--play=1", clicks)
    reloaded = [l for l in lines if "reload-scripts →" in l]
    report("3 핫리로드가 실제로 일어남", reloaded == ["자동 검증: reload-scripts → 성공"], str(reloaded))
    expect_seq("3 스크립트 핫리로드 뒤 연결", says(lines), want)


def t_widgets(r):
    lines = r.run(r.scene("UiWidgets"), "--play=1", "--ui-drag=120,255,84,255@20:5", "--ui-click=30,345@35;30,345@40;30,375@45;270,375@50",
                  "--log-rect=Handle@60", f"--screenshot={os.path.join(r.dir, 'widgets.png')}", exit_after=70)
    s = says(lines)
    values = [l for l in s if "value" in l]
    report("4 Slider 드래그 → 0.300", bool(values) and values[-1].endswith("value 0.300"), values[-1] if values else "없음")
    toggles = [l.split(": ", 1)[1] for l in s if "say" in l or "flag" in l]
    expect_seq("4 Toggle 그룹·allowSwitchOff·단독", toggles, ["say T1 changed", "say T2 changed", "say T2 changed", "say T3 changed", "flag off"])
    h = [x for x in rects(lines) if x["name"] == "Handle"]
    report("4 Slider 손잡이 가운데 x 84", bool(h) and abs((h[0]["quad"][0][0] + h[0]["quad"][1][0]) / 2 - 84) < 0.01)
    edit = os.path.join(r.dir, "widgets_edit.png")
    r.run(r.scene("UiWidgets"), "--light-icons=selected", f"--screenshot={edit}", exit_after=20)
    report("4 9-slice·Filled 픽셀 (check_ui_widgets)", subprocess.run([sys.executable, "-I", os.path.join(HERE, "check_ui_widgets.py"), edit], stdout=subprocess.DEVNULL).returncode == 0)


def t_layout(r):
    lines = r.run(r.scene("UiLayout"), "--play=1", "--frame-dt=0.0166667",
                  "--log-rect=V1@20;V2@20;V3@20;H1@20;H2@20;H3@20;G1@20;G5@20;G7@20;Item 0@25;Item 0@40;Item 0@100;E0@25;E0@52;E0@120",
                  "--ui-wheel=640,300,-2@30;640,300,10@60", "--ui-drag=480,300,480,380@45:5", f"--screenshot={os.path.join(r.dir, 'layout.png')}", exit_after=140)
    got = {}
    for x in rects(lines):
        got.setdefault(x["name"], []).append(x["quad"])
    # (왼쪽 x, 위 y, 오른쪽 x, 아래 y) — 씬 뷰 픽셀. quad = 좌하, 우하, 우상, 좌상
    want = {"V1": (30, 30, 210, 60), "V2": (30, 65, 210, 135), "V3": (30, 140, 210, 230), "H1": (240, 20, 290, 80), "H2": (300, 20, 480, 80),
            "H3": (490, 20, 540, 80), "G1": (255, 125, 315, 165), "G5": (255, 175, 315, 215), "G7": (395, 175, 455, 215)}
    bad = [n for n, w in want.items() if n not in got or not near((got[n][0][0][0], got[n][0][3][1], got[n][0][1][0], got[n][0][0][1]), w)]
    report("5 세로·가로·격자 레이아웃 사각형 (손 계산과 0 px)", not bad, f"다름 {bad}" if bad else "")
    item = [q[3][1] for q in got.get("Item 0", [])]
    report("5 휠 −2 → 40 px 위로, +10 → 맨 위에서 멈춤 (Clamped)", len(item) == 3 and near(item, [189, 149, 189]), str(item))
    e0 = [q[3][1] for q in got.get("E0", [])]
    report("5 Elastic 끌어내림 뒤 제자리", len(e0) == 3 and e0[1] > e0[0] + 20 and abs(e0[2] - e0[0]) < 0.1, str(e0))
    a, b = os.path.join(r.dir, "LayoutA.json"), os.path.join(r.dir, "LayoutB.json")
    r.run(r.scene("UiLayout"), f"--save={a}", exit_after=20)
    r.run(a, f"--save={b}", exit_after=20)
    report("5 저장 안정성 (두 번 저장한 파일이 같음)", open(a, "rb").read() == open(b, "rb").read())


def t_nav(r):
    keys = "Down@20;Right@25;Down@30;Right@35;Down@40;Enter@45;Left@50;Right@55;Right@60;Right@65;Left@70;Up@75;Esc@80"
    want = ["N1", "N2", "N3", "N4", "Far", "submit Far", "Far", "Slider", "0.100", "Slider", "0.200", "Slider", "0.300", "Slider", "0.200", "Slider", "N3", "cancel", "N3"]
    for kind, arg in (("키보드", f"--key={keys}"), ("패드", "--pad=" + keys.replace("Enter", "A").replace("Esc", "B"))):
        got = []
        for l in r.run(r.scene("UiNav"), "--play=1", "--frame-dt=0.0166667", arg, exit_after=95):
            m = re.search(r"뒤 UI 선택 '(.+)'", l)
            if m:
                got.append(m.group(1))
            elif l.startswith("UiClickCounter"):
                got.append(re.sub(r".*: (say |value )", "", l))
        expect_seq(f"6 {kind} 내비게이션·Explicit·Submit·Slider·Cancel", got, want)


def t_rect_tool(r):
    def last(lines):
        x = [y for y in rects(lines) if y["why"] != "tool-click 선택"]
        return x[-1] if x else None
    base = (r.scene("UiRectTool"),)
    cases = [
        ("7 이동 (Ctrl: 스냅 끔)", ["--tool-drag=340,230,360,220@25:5:c"], (20, 10), (100, 50)),
        ("7 오른쪽 변 크기", ["--tool-drag=340,230,360,220@25:5:c;436,212.5,466,212.5@40:5:c"], (35, 10), (130, 50)),
        ("7 Alt 가운데 기준", ["--tool-drag=340,230,360,220@25:5:c;436,212.5,466,212.5@40:5:c;466,212.5,476,212.5@55:5:ac"], (35, 10), (150, 50)),
        ("7 Shift 비율 유지", ["--tool-drag=416,197.5,446,187.5@25:5:sc"], (15, 7.5), (130, 65)),
        ("7 부모 가장자리 스냅", ["--tool-drag=340,230,243,230@25:5"], (-100, 0), (100, 50)),
    ]
    for name, extra, pos, size in cases:
        x = last(r.run(*base, "--tool=rect", "--tool-click=340,230@20", *extra, exit_after=80))
        report(name, x is not None and near(x["pos"], pos) and near(x["size"], size), f"{x['pos']} {x['size']}" if x else "없음")
    x = last(r.run(*base, "--tool=rect", "--tool-click=116,102@20", "--tool-drag=167.96,72.5,185.28,62.5@25:5:c", exit_after=50))
    report("7 30° 회전 요소 로컬 축 크기", x is not None and near(x["pos"], (-241.34, 125)) and near(x["size"], (140, 40)), f"{x['pos']} {x['size']}" if x else "없음")
    lines = r.run(*base, "--tool=rect", "--event-frame=45", "--undo=1", "--tool-click=340,230@20", "--tool-drag=340,230,360,220@25:5:c", "--log-rect=Box@50", exit_after=60)
    x = [y for y in rects(lines) if y["why"] == "log"]
    report("7 드래그 한 번 = 되돌리기 한 단계", bool(x) and near(x[-1]["pos"], (0, 0)) and sum("되돌리기 이력" in l for l in lines) == 1)
    for child in ("Box", "Rot"):
        q = [y["quad"] for y in rects(r.run(*base, f"--set-parent={child};P2", f"--log-rect={child}@3;{child}@20", exit_after=30))]
        report(f"7 부모 바꾸기 화면 사각형 그대로 ({child})", len(q) == 2 and all(near(a, b, 0.01) for a, b in zip(q[0], q[1])))


def t_world(r):
    from PIL import Image, ImageChops
    ui, noui = os.path.join(r.dir, "world.png"), os.path.join(r.dir, "world_noui.png")
    lines = r.run(r.scene("UiWorld"), "--play=1", "--ui-click=415,280@20;490,262@25;100,300@30;415,240@35", f"--screenshot={ui}", exit_after=60)
    expect_seq("8 기울어진 패널 버튼 클릭 (가운데·오른쪽 끝 / 바깥·패널 위 아님)", says(lines), ["UiClickCounter 'Counter': count 1", "UiClickCounter 'Counter': count 2"])
    r.run(r.scene("UiWorldNoUI"), "--play=1", f"--screenshot={noui}", exit_after=60)
    crop = (232, 71, 964, 516)
    d = ImageChops.difference(Image.open(ui).convert("RGB").crop(crop), Image.open(noui).convert("RGB").crop(crop)).convert("L")
    pillar = d.crop((290, 145, 360, 320)).histogram()
    report("8 기둥에 가려진 부분 = UI 끈 화면", sum(pillar[9:]) == 0, f"다른 픽셀 {sum(pillar[9:])}")
    panel = d.crop((380, 150, 540, 300)).histogram()
    report("8 보이는 패널 부분은 그려짐", sum(panel[9:]) > 20000, f"다른 픽셀 {sum(panel[9:])}")


def t_showcase(r):
    scene = "UiShowcase.json"
    lines = r.run(scene, "--play=1", "--frame-dt=0.0166667",
                  "--ui-click=103,143@20;103,143@24;103,173@28;31,240@32;647,131@36;647,330@40;347,414@44;511,278@48",
                  "--ui-drag=132.4,317.6,30,317.6@52:5;647,250,647,200@66:6", "--ui-wheel=647,220,-3@62", "--key=Esc@85",
                  "--log-rect=Item 1@61;Item 1@64", exit_after=100)
    s = [re.sub(r"UiClickCounter '(.+?)': ", r"\1: ", l) for l in says(lines)]
    want = ["Click Log: count 1", "Click Log: count 2", "Click Log: reset", "Status Log: say 품질: 보통", "Status Log: say 품질: 낮음",
            "Status Log: say 선택: 검", "Status Log: say 단축칸 3", "Counter: count 1"]
    expect_seq("쇼케이스 클릭 (마스크 밖 항목은 안 눌림)", s[:len(want)], want)
    vol = [l for l in s if l.startswith("Volume Log")]
    report("쇼케이스 볼륨 드래그 정수 → 0", bool(vol) and vol[-1] == "Volume Log: value 0.000" and all(l.endswith(".000") for l in vol), str(vol))
    report("쇼케이스 목록 드래그는 클릭 아님 · Esc 취소", s[len(want) + len(vol):] == ["Status Log: say 취소 (Esc / B)"], str(s[len(want) + len(vol):]))
    item = [x["quad"][3][1] for x in rects(lines)]
    report("쇼케이스 휠 −3 = 60 단위 × 캔버스 배율", len(item) == 2 and abs((item[0] - item[1]) - 60 * 0.5946) < 0.1, str(item))
    from PIL import Image, ImageChops
    shots = {}
    for n, extra in (("base", []), ("overui", ["--ui-drag=100,300,200,280@20:10:r"]), ("off", ["--ui-drag=300,250,400,230@20:10:r"])):
        shots[n] = os.path.join(r.dir, f"rmb_{n}.png")
        r.run(scene, "--play=1", "--frame-dt=0.0166667", "--mouse=300,40@15", *extra, f"--screenshot={shots[n]}", exit_after=60)
    img = {n: Image.open(p).convert("RGB").crop((232, 71, 964, 516)) for n, p in shots.items()}
    over = sum(ImageChops.difference(img["base"], img["overui"]).convert("L").histogram()[9:])
    off = sum(ImageChops.difference(img["base"], img["off"]).convert("L").histogram()[9:])
    report("3 UI 위 오른쪽 드래그는 카메라를 돌리지 않음 (밖은 돌림)", over == 0 and off > 10000, f"UI 위 {over}, 밖 {off}")


def t_regress(r):
    for scene, checker in (("UiAnchors", "check_ui_rects.py"), ("UiText", "check_ui_text.py"), ("UiFonts", "check_ui_fonts.py")):
        if scene == "UiFonts" and not os.path.exists(os.path.join(PROJECT, "Assets", "UiTestFonts", "Karla-Regular.ttf")):
            print("SKIP 1~2 회귀 UiFonts — Assets/UiTestFonts/Karla-Regular.ttf 없음")
            continue
        shot = os.path.join(r.dir, f"reg_{scene}.png")
        r.run(r.scene(scene), "--light-icons=selected", f"--screenshot={shot}", exit_after=40)
        ok = subprocess.run([sys.executable, "-I", os.path.join(HERE, checker), shot], stdout=subprocess.DEVNULL).returncode == 0
        report(f"1~2 회귀 {checker}", ok)


TESTS = {"click": t_click, "widgets": t_widgets, "layout": t_layout, "nav": t_nav, "rect": t_rect_tool, "world": t_world, "showcase": t_showcase, "regress": t_regress}


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")   # 콘솔 코드 페이지와 무관하게
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default="Debug")
    parser.add_argument("--backend", default="d3d11")
    parser.add_argument("--dir", default=os.path.join(tempfile.gettempdir(), "sherlock_ui_verify"))
    parser.add_argument("--only", default="")
    args = parser.parse_args()
    os.makedirs(args.dir, exist_ok=True)
    subprocess.run([sys.executable, "-I", os.path.join(HERE, "make_ui_test_scenes.py"), f"--verify-dir={args.dir}"], check=True, stdout=subprocess.DEVNULL)
    r = Runner(args.config, args.backend, args.dir)
    for name, test in TESTS.items():
        if not args.only or name in args.only.split(","):
            test(r)
    failed = [n for n, ok in results if not ok]
    print(f"\n{len(results) - len(failed)} / {len(results)} 통과" + (f" — 실패: {failed}" if failed else ""))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
