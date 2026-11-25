# 게임 UI 캔버스 테스트 씬 생성 (docs/plan/ui-canvas-plan.html).
#
#   python make_ui_test_scenes.py                       → Assets/Scenes/UiShowcase.json 하나 (최종 쇼케이스, 프로젝트에 남는 씬)
#   python make_ui_test_scenes.py --verify-dir=<폴더>   → 단계별 검증 씬을 그 폴더에 (저장소에 넣지 않음, 실행은 --scene=<절대 경로>)
#
# 모두 DamagedHelmet.json 을 바탕으로 함 (기존 씬은 고치지 않음). 검증 씬의 배치는 에디터 씬 뷰 732×445 (1280×720 창) 기준.
# 캔버스 단위가 픽셀과 같도록 대부분 Constant Pixel Size ×1 — 씬 뷰 픽셀 좌표 (x, y) = (캔버스 x, 445 − 캔버스 y).
#
# 검증 씬 (--verify-dir):
#   1~2단계  UiAnchors UiStress UiText UiTextAdded UiTextAtlas UiTextSaturate UiFonts   (check_ui_rects / check_ui_text / check_ui_fonts)
#   3단계    UiClick      버튼 클릭·겹침·꺼진 버튼·가림·바깥 클릭, 저장된 연결(UiClickCounter)
#   4단계    UiWidgets    9-slice 두 크기, Filled, Slider, Toggle 그룹
#   5단계    UiLayout     세로·가로·격자 레이아웃, ContentSizeFitter, RectMask, ScrollRect(휠·드래그)
#            UiPerf       1000 요소 (UI Build 시간)
#   6단계    UiNav        방향키·패드 내비게이션, Submit, Slider 키 조작, Cancel
#   7단계    UiRectTool   Rect 툴 드래그·회전 요소·재부모화
#   8단계    UiWorld      기둥 뒤 World 캔버스, 이름표(Billboard), 기울어진 패널의 버튼 / UiWorldNoUI (같은 씬, 캔버스 끔)
import argparse
import copy
import json
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
SCENES = os.path.normpath(os.path.join(HERE, "..", "..", "Assets", "Scenes"))
OUT = SCENES


def load(name):
    with open(os.path.join(SCENES, name), encoding="utf-8-sig") as f:
        return json.load(f)


def save(name, scene):
    text = json.dumps(scene, indent=2, ensure_ascii=False)
    with open(os.path.join(OUT, name), "w", encoding="utf-8", newline="\r\n") as f:
        f.write(text + "\n")
    print("wrote", os.path.join(OUT, name))


# ---------------------------------------------------------------- 컴포넌트

def canvas(sort=0, mode=0, scale=1.0, ref=(1920.0, 1080.0), match=0.5, world=False, ppu=1.0):
    return [{"type": "Canvas", "enabled": True, "renderMode": 1 if world else 0, "sortOrder": sort, "pixelsPerUnit": ppu},
            {"type": "CanvasScaler", "enabled": True, "mode": mode, "scaleFactor": scale, "referenceResolution": list(ref), "match": match}]


def rect(anchor_min, anchor_max, pivot, pos, size):
    return {"type": "RectTransform", "enabled": True, "anchorMin": list(anchor_min), "anchorMax": list(anchor_max),
            "pivot": list(pivot), "anchoredPosition": list(pos), "sizeDelta": list(size)}


def stretch(left=0, right=0, top=0, bottom=0):
    # 부모를 채우고 가장자리에서 안쪽으로 (유니티 Left/Right/Top/Bottom)
    return rect((0, 0), (1, 1), (0.5, 0.5), ((left - right) / 2.0, (bottom - top) / 2.0), (-(left + right), -(top + bottom)))


def image(color, texture="", raycast=True, **extra):
    c = {"type": "UIImage", "enabled": True, "color": list(color), "raycastTarget": raycast, "texture": texture}
    c.update(extra)
    return c


def panel(color, raycast=True, border=12.0, **extra):
    # 기본 둥근 사각형 9-slice
    return image(color, "builtin:ui-panel", raycast, imageType=1, borderLeft=border, borderRight=border, borderTop=border, borderBottom=border, **extra)


def text(value, size, color, align=0, overflow=0, wrap=True, spacing=1.0, font="", raycast=False):
    return {"type": "UIText", "enabled": True, "color": list(color), "raycastTarget": raycast, "text": value, "font": font,
            "fontSize": size, "alignment": align, "lineSpacing": spacing, "wrap": wrap, "verticalOverflow": overflow}


def call(target, method, **arg):
    # arg: dynamic=False 와 float=/int=/bool=/string= 하나 (고정 인자)
    c = {"target": target, "method": method, "dynamic": arg.pop("dynamic", True)}
    c.update(arg)
    return c


def button(calls=(), interactable=True, target=".", nav=0, **extra):
    c = {"type": "UIButton", "enabled": True, "interactable": interactable, "targetGraphic": target, "navigation": nav, "onClick": list(calls)}
    c.update(extra)
    return c


def toggle(is_on, graphic, group="", calls=(), target="Background"):
    return {"type": "UIToggle", "enabled": True, "isOn": is_on, "graphic": graphic, "group": group, "targetGraphic": target, "onValueChanged": list(calls)}


def slider(value, lo=0.0, hi=1.0, whole=False, calls=(), direction=0):
    return {"type": "UISlider", "enabled": True, "minValue": lo, "maxValue": hi, "value": value, "wholeNumbers": whole, "direction": direction,
            "fillRect": "Fill Area/Fill", "handleRect": "Handle Slide Area/Handle", "targetGraphic": "Handle Slide Area/Handle",
            "onValueChanged": list(calls)}


def counter(label, prefix="Clicks", count=0, write_on_start=True):
    return {"type": "UiClickCounter", "enabled": True, "label": label, "prefix": prefix, "count": count, "writeOnStart": write_on_start}


def vlayout(pad=(0, 0, 0, 0), spacing=0.0, align=0, control=(True, True), force=(True, True)):
    l, r, t, b = pad
    return {"type": "UIVerticalLayout", "enabled": True, "paddingLeft": l, "paddingRight": r, "paddingTop": t, "paddingBottom": b,
            "childAlignment": align, "spacing": spacing, "controlChildWidth": control[0], "controlChildHeight": control[1],
            "childForceExpandWidth": force[0], "childForceExpandHeight": force[1]}


def hlayout(pad=(0, 0, 0, 0), spacing=0.0, align=0, control=(True, True), force=(True, True)):
    c = vlayout(pad, spacing, align, control, force)
    c["type"] = "UIHorizontalLayout"
    return c


def grid(cell, spacing=(0, 0), constraint=0, count=2, pad=(0, 0, 0, 0), align=0, corner=0, axis=0):
    l, r, t, b = pad
    return {"type": "UIGridLayout", "enabled": True, "paddingLeft": l, "paddingRight": r, "paddingTop": t, "paddingBottom": b,
            "childAlignment": align, "cellSize": list(cell), "spacing": list(spacing), "startCorner": corner, "startAxis": axis,
            "constraint": constraint, "constraintCount": count}


def element(min_w=-1, min_h=-1, pref_w=-1, pref_h=-1, flex_w=-1, flex_h=-1, ignore=False):
    return {"type": "UILayoutElement", "enabled": True, "ignoreLayout": ignore, "minWidth": min_w, "minHeight": min_h,
            "preferredWidth": pref_w, "preferredHeight": pref_h, "flexibleWidth": flex_w, "flexibleHeight": flex_h}


def fitter(h=0, v=2):
    return {"type": "UIContentSizeFitter", "enabled": True, "horizontalFit": h, "verticalFit": v}


def event_system(first="", wasd=False, cancel=()):
    return {"type": "UIEventSystem", "enabled": True, "firstSelected": first, "repeatDelay": 0.4, "repeatRate": 0.1, "wasdNavigation": wasd,
            "onCancel": list(cancel)}


WHITE = (1, 1, 1, 1)
DARK = (0.196, 0.196, 0.196, 1)


class Builder:
    def __init__(self, scene):
        self.scene = scene
        self.next_id = max(o.get("id", 0) for o in scene["objects"]) + 1
        self.sibling = {}

    def add(self, name, components, parent=None, rotation=(0.0, 0.0, 0.0), position=(0.0, 0.0, 0.0), scale=(1.0, 1.0, 1.0), mesh=-1, material=-1):
        if not isinstance(rotation, (tuple, list)):
            rotation = (0.0, 0.0, rotation)
        obj = {"name": name, "id": self.next_id, "mesh": mesh, "material": material,
               "position": list(position), "rotation": list(rotation), "scale": list(scale),
               "components": components}
        self.next_id += 1
        if parent is not None:
            obj["parent"] = parent
            obj["sibling"] = self.sibling.get(parent, 0)
            self.sibling[parent] = obj["sibling"] + 1
        self.scene["objects"].append(obj)
        return len(self.scene["objects"]) - 1

    def id_of(self, index):
        return self.scene["objects"][index]["id"]

    # ---- 위젯 묶음 (에디터 "+ UI" 메뉴의 AddUIWidget 과 같은 구조) ----
    def button(self, name, parent, r, label, calls=(), interactable=True, color=WHITE, font_size=18, **extra):
        root = self.add(name, [r, panel(color), button(calls, interactable, **extra)], parent)
        self.add("Text", [stretch(), text(label, font_size, DARK, align=4)], root)
        return root

    def toggle(self, name, parent, r, label, is_on, group="", calls=(), label_color=WHITE):
        root = self.add(name, [r, toggle(is_on, "Background/Checkmark", group, calls)], parent)
        bg = self.add("Background", [rect((0, 1), (0, 1), (0.5, 0.5), (10, -10), (20, 20)), panel(WHITE)], root)
        self.add("Checkmark", [stretch(4, 4, 4, 4), panel((0.2, 0.2, 0.2, 1), raycast=False, border=6.0)], bg)
        self.add("Label", [rect((0, 0), (1, 1), (0.5, 0.5), (14, 0), (-28, 0)), text(label, 16, label_color, align=3)], root)
        return root

    def slider(self, name, parent, r, value, lo=0.0, hi=1.0, whole=False, calls=()):
        root = self.add(name, [r, slider(value, lo, hi, whole, calls)], parent)
        self.add("Background", [rect((0, 0.25), (1, 0.75), (0.5, 0.5), (0, 0), (0, 0)), panel((0.8, 0.8, 0.8, 1), border=5.0)], root)
        area = self.add("Fill Area", [rect((0, 0.25), (1, 0.75), (0.5, 0.5), (0, 0), (-20, 0))], root)
        self.add("Fill", [rect((0, 0), (1, 1), (0.5, 0.5), (0, 0), (10, 0)), panel((0.35, 0.6, 1, 1), raycast=False, border=5.0)], area)
        handles = self.add("Handle Slide Area", [rect((0, 0), (1, 1), (0.5, 0.5), (0, 0), (-20, 0))], root)
        self.add("Handle", [rect((0, 0), (1, 1), (0.5, 0.5), (0, 0), (20, 0)), panel(WHITE, border=10.0)], handles)
        return root

    def scroll_view(self, name, parent, r, bar_value=1.0, bar_size=0.5, content_layout=True, movement=1):
        root = self.add(name, [r, panel((1, 1, 1, 0.39)),
                               {"type": "UIScrollRect", "enabled": True, "content": "Viewport/Content", "viewport": "Viewport",
                                "horizontal": False, "vertical": True, "movementType": movement, "elasticity": 0.1, "inertia": True,
                                "decelerationRate": 0.135, "scrollSensitivity": 20.0, "verticalScrollbar": "Scrollbar Vertical"}], parent)
        viewport = self.add("Viewport", [rect((0, 0), (1, 1), (0, 1), (0, 0), (-17, 0)), {"type": "UIRectMask", "enabled": True, "padding": [0, 0]}], root)
        comps = [rect((0, 1), (1, 1), (0, 1), (0, 0), (0, 300))]
        if content_layout:
            comps += [vlayout((4, 4, 4, 4), 4.0, control=(True, False), force=(True, False)), fitter(0, 2)]
        content = self.add("Content", comps, viewport)
        bar = self.add("Scrollbar Vertical", [rect((1, 0), (1, 1), (1, 1), (0, 0), (17, 0)), panel((0.25, 0.25, 0.25, 1), border=6.0),
                                              {"type": "UIScrollbar", "enabled": True, "direction": 2, "value": bar_value, "size": bar_size,
                                               "handleRect": "Sliding Area/Handle", "targetGraphic": "Sliding Area/Handle"}], root)
        area = self.add("Sliding Area", [stretch(3, 3, 3, 3)], bar)
        self.add("Handle", [stretch(), panel((0.85, 0.85, 0.85, 1), border=5.0)], area)
        return root, content


def with_cube(scene):
    # 1 m 정육면체 메시 + 회색 재질 (World 캔버스를 가리는 기둥)
    scene["meshes"].append({"a": 0, "b": 0, "color": [1.0, 1.0, 1.0, 1.0], "params": [1.0, 0.0, 0.0, 0.0], "type": "cube"})
    mat = copy.deepcopy(scene["materials"][-1])
    mat.update({"name": "Pillar", "albedoTexture": "", "baseColor": [0.55, 0.55, 0.6, 1.0], "emissive": [0.0, 0.0, 0.0], "emissiveTexture": "",
                "metallic": 0.0, "metallicRoughnessTexture": "", "normalTexture": "", "occlusionTexture": "", "roughness": 0.7})
    scene["materials"].append(mat)
    return len(scene["meshes"]) - 1, len(scene["materials"]) - 1


def hud_scene(bg=True):
    scene = load("DamagedHelmet.json")
    b = Builder(scene)
    hud = b.add("HUD", canvas())
    if bg:
        b.add("Bg", [stretch(), image((0.08, 0.08, 0.1, 1), raycast=False)], hud)
    return scene, b, hud


# ---------------------------------------------------------------- 1~2단계 (그대로)

def stage1_2():
    # 1~2단계 씬은 처음 만든 그대로 (세로 넘침 기본값 Truncate) — check_ui_text 등의 기대값이 이것을 기준으로 함
    def text(*args, **kwargs):
        kwargs.setdefault("overflow", 1)
        return globals()["text"](*args, **kwargs)

    scene = load("DamagedHelmet.json")
    b = Builder(scene)
    hud = b.add("HUD", canvas())
    b.add("BL", [rect((0, 0), (0, 0), (0, 0), (10, 10), (100, 50)), image((1, 0, 0, 1))], hud)
    b.add("TR", [rect((1, 1), (1, 1), (1, 1), (-10, -10), (80, 40)), image((0, 1, 0, 1))], hud)
    b.add("Bar", [rect((0.25, 0.5), (0.75, 0.5), (0.5, 0.5), (0, 0), (0, 20)), image((0, 0, 1, 1))], hud)
    back = b.add("Back", [rect((0.5, 0.5), (0.5, 0.5), (0.5, 0.5), (0, -80), (300, 150)), image((0, 0, 0, 1))], hud)
    half = b.add("Half", [rect((0.5, 0.5), (0.5, 0.5), (0.5, 0.5), (0, 0), (200, 100)), image((1, 1, 1, 0.5))], back)
    b.add("Child", [rect((0, 0), (0, 0), (0, 0), (0, 0), (20, 20)), image((1, 1, 0, 1))], half)
    b.add("Tex", [rect((1, 0), (1, 0), (1, 0), (-10, 10), (64, 64)), image((1, 1, 1, 1), "asset:Textures\\NormalBumpTest.png")], hud)
    b.add("Rot", [rect((0, 1), (0, 1), (0.5, 0.5), (60, -200), (60, 20)), image((0.3, 0.8, 1, 1))], hud, rotation=math.radians(45.0))
    scaled = b.add("Scaled", canvas(sort=-1, mode=1))
    b.add("Corner", [rect((0, 1), (0, 1), (0, 1), (0, 0), (192, 108)), image((1, 0, 1, 1))], scaled)
    save("UiAnchors.json", scene)

    scene = load("DamagedHelmet.json")
    b = Builder(scene)
    c = b.add("Canvas", canvas())
    g = b.add("Grid", [rect((0, 0), (0, 0), (0, 0), (8, 8), (96, 90))], c)
    for y in range(15):
        for x in range(16):
            b.add(f"Cell {x},{y}", [rect((0, 0), (0, 0), (0, 0), (x * 6, y * 6), (4, 4)), image(((x + 1) / 16.0, (y + 1) / 15.0, 0.5, 1.0))], g)
    b.add("Albedo", [rect((1, 1), (1, 1), (1, 1), (-8, -8), (64, 64)), image(WHITE, "DamagedHelmet/0")], c)
    b.add("Bogus", [rect((0, 1), (0, 1), (0, 1), (8, -8), (64, 64)), image(WHITE, "no_such_ui_texture.png")], c)
    save("UiStress.json", scene)

    def text_scene():
        scene = load("DamagedHelmet.json")
        b = Builder(scene)
        hud = b.add("HUD", canvas())
        b.add("Bg", [stretch(), image((0, 0, 0, 1))], hud)
        p = b.add("BlendPanel", [rect((0, 1), (0, 1), (0, 1), (20, -20), (200, 60)), image((0, 0, 1, 1))], hud)
        b.add("Blend", [stretch(), text("Blend 혼합", 32, (1, 0, 0, 1), align=4, raycast=True)], p)
        b.add("White", [rect((0, 1), (0, 1), (0, 1), (20, -100), (200, 50)), text("Hi 안녕", 32, WHITE, raycast=True)], hud)
        for i in range(9):
            r, col = divmod(i, 3)
            b.add(f"Align {i}", [rect((1, 1), (1, 1), (1, 1), (-20 - (2 - col) * 160, -20 - r * 60), (150, 50)), text("Ag", 20, WHITE, align=i, raycast=True)], hud)
        b.add("Wrap", [rect((0, 0), (0, 0), (0, 0), (20, 20), (220, 90)),
                       text("줄바꿈 테스트입니다. The quick brown fox jumps over the lazy dog. 마지막 줄은 말줄임표로 끝나야 합니다.", 17, WHITE, overflow=2, raycast=True)], hud)
        b.add("Word", [rect((0, 0), (0, 0), (0, 0), (260, 70), (100, 80)), text("가나다라마바사아자차카타파하", 20, WHITE, raycast=True)], hud)
        b.add("Missing", [rect((0, 0), (0, 0), (0, 0), (380, 10), (200, 50)), text("X 😀 Y", 28, WHITE, raycast=True)], hud)
        b.add("Kern", [rect((0, 0), (0, 0), (0, 0), (380, 70), (200, 50)), text("AVAWAY", 28, WHITE, raycast=True)], hud)
        b.add("Rotated", [rect((0, 0), (0, 0), (0.5, 0.5), (660, 60), (120, 40)), text("회전 Rot", 22, (1, 1, 0, 1), raycast=True)], hud, rotation=math.radians(15.0))
        s = b.add("Scaled", canvas(sort=1, mode=1))
        b.add("ScaledText", [rect((0, 1), (0, 1), (0, 1), (60, -380), (600, 60)), text("Scaled 캔버스 40", 40, (0.5, 1, 0.5, 1), raycast=True)], s)
        return scene, b, hud

    scene, b, hud = text_scene()
    save("UiText.json", scene)
    scene, b, hud = text_scene()
    b.add("Text", [rect((0.5, 0.5), (0.5, 0.5), (0.5, 0.5), (0, 0), (160, 30)), text("New Text", 18, DARK, raycast=True)], hud)
    save("UiTextAdded.json", scene)

    for name, size, count in (("UiTextAtlas.json", 64, 400), ("UiTextSaturate.json", 96, 1200)):
        scene = load("DamagedHelmet.json")
        b = Builder(scene)
        c = b.add("Canvas", canvas())
        b.add("Bg", [stretch(), image((0, 0, 0, 1))], c)
        chars = "".join(chr(0xAC00 + i * 7) for i in range(count))
        b.add("Many", [stretch(), text(chars, size, WHITE, overflow=0, raycast=True)], c)
        save(name, scene)

    scene = load("DamagedHelmet.json")
    b = Builder(scene)
    hud = b.add("HUD", canvas())
    b.add("Bg", [stretch(), image((0, 0, 0, 1))], hud)
    for row, (name, font) in enumerate((("FontKarla", "asset:UiTestFonts\\Karla-Regular.ttf"), ("FontMissing", "asset:UiTestFonts\\NoSuchFont.ttf"), ("FontDefault", ""))):
        b.add(name, [rect((0, 1), (0, 1), (0, 1), (20, -20 - row * 60), (300, 50)), text("Font AVTo 123", 32, WHITE, font=font, raycast=True)], hud)
    b.add("Spacing1", [rect((0, 1), (0, 1), (0, 1), (360, -20), (100, 200)), text("A\nA", 24, WHITE, spacing=1.0, raycast=True)], hud)
    b.add("Spacing2", [rect((0, 1), (0, 1), (0, 1), (480, -20), (100, 200)), text("A\nA", 24, WHITE, spacing=2.0, raycast=True)], hud)
    b.add("NoWrap", [rect((0, 0), (0, 0), (0, 0), (20, 40), (80, 40)), text("wrap off: one long line", 20, WHITE, wrap=False, raycast=True)], hud)
    b.add("WrapEdge", [rect((0, 0), (0, 0), (0, 0), (300, 20), (100, 100)), text("i mmmmamm", 20, WHITE, raycast=True)], hud)
    b.add("Huge", [rect((0, 0), (0, 0), (0, 0), (700, 0), (4000, 4000)), text("A", 3000, (0.3, 0.3, 0.3, 1), overflow=0, raycast=True)], hud)
    save("UiFonts.json", scene)


# ---------------------------------------------------------------- 3단계: 클릭

def stage3():
    # 픽셀 좌표 (씬 뷰 732×445, y 아래 +):
    #   A      캔버스 (20,20)~(180,60)    → 픽셀 x 20..180, y 385..425       클릭 (60, 405) → Counter A +1
    #   B      캔버스 (100,30)~(260,70)   → 픽셀 x 100..260, y 375..415      겹친 곳 (140, 400) → B 만 +1 (나중에 그린 것)
    #   Off    캔버스 (20,100)~(180,140)  꺼진 버튼                           (60, 325) → 없음
    #   Hidden 캔버스 (20,160)~(180,200)  위를 Blocker(raycastTarget) 가 덮음  (60, 265) → 없음
    #   Top    sortOrder 1 캔버스, 캔버스 (300,20)~(460,60), 그 아래 HUD 의 Under (300,20)~(460,60)  (380, 405) → Top 만
    #   바깥 (600, 100) → 없음.  Label: 'Clicks: N' 글자
    scene, b, hud = hud_scene()
    ca = b.add("Counter A", [counter("../Label A", "A")], hud)
    cb = b.add("Counter B", [counter("../Label B", "B")], hud)
    b.add("Label A", [rect((0, 1), (0, 1), (0, 1), (20, -20), (300, 30)), text("A: 0", 24, WHITE)], hud)
    b.add("Label B", [rect((0, 1), (0, 1), (0, 1), (20, -60), (300, 30)), text("B: 0", 24, WHITE)], hud)
    b.button("A", hud, rect((0, 0), (0, 0), (0, 0), (20, 20), (160, 40)), "A", [call("../Counter A", "UiClickCounter.Increment")])
    b.button("B", hud, rect((0, 0), (0, 0), (0, 0), (100, 30), (160, 40)), "B", [call("../Counter B", "UiClickCounter.Increment")], color=(0.8, 0.9, 1, 1))
    b.button("Off", hud, rect((0, 0), (0, 0), (0, 0), (20, 100), (160, 40)), "Off", [call("../Counter A", "UiClickCounter.Increment")], interactable=False)
    b.button("Hidden", hud, rect((0, 0), (0, 0), (0, 0), (20, 160), (160, 40)), "Hidden", [call("../Counter A", "UiClickCounter.Increment")])
    b.add("Blocker", [rect((0, 0), (0, 0), (0, 0), (10, 150), (180, 60)), image((0.6, 0.2, 0.2, 0.5))], hud)
    b.button("Under", hud, rect((0, 0), (0, 0), (0, 0), (300, 20), (160, 40)), "Under", [call("../Counter A", "UiClickCounter.Increment")])
    top = b.add("Top Canvas", canvas(sort=1))
    b.button("Top", top, rect((0, 0), (0, 0), (0, 0), (300, 20), (160, 40)), "Top", [call("../../HUD/Counter B", "UiClickCounter.Increment")], color=(1, 0.9, 0.6, 1))
    b.add("EventSystem", [event_system()])
    save("UiClick.json", scene)


# ---------------------------------------------------------------- 4단계: 위젯

def stage4():
    # Big / Small: 같은 9-slice (border 12) 를 300×120 과 60×30 으로 → 네 모서리 12×12 픽셀이 같아야 함 (check_ui_widgets.py)
    #   Big   캔버스 (20, 305)~(320, 425)  → 픽셀 x 20..320, y 20..140
    #   Small 캔버스 (360, 395)~(420, 425) → 픽셀 x 360..420, y 20..50
    #   Fill  Filled 가로 0.3: 캔버스 (20, 250)~(220, 280) → 칠해진 폭 60 px (x 20..80, y 165..195)
    #   Slider 0..1, 0.5: 캔버스 (20, 180)~(220, 200) (픽셀 y 245..265). 손잡이 영역 = x 30..210 → 0.3 은 x = 30 + 0.3·180 = 84
    #   Toggle 그룹 T1(켬)·T2·T3, allowSwitchOff 끔: 캔버스 x 20, y 120·90·60 (픽셀 y 315·345·375 가운데 = 325·355·385 − 10)
    #   Solo  그룹 없는 토글 (켬) → 누르면 끔
    scene, b, hud = hud_scene()
    b.add("Big", [rect((0, 0), (0, 0), (0, 0), (20, 305), (300, 120)), panel((1, 0.6, 0.2, 1))], hud)
    b.add("Small", [rect((0, 0), (0, 0), (0, 0), (360, 395), (60, 30)), panel((1, 0.6, 0.2, 1))], hud)
    b.add("NoCenter", [rect((0, 0), (0, 0), (0, 0), (440, 305), (120, 120)), panel((0.4, 1, 0.5, 1), fillCenter=False)], hud)
    b.add("Fill", [rect((0, 0), (0, 0), (0, 0), (20, 250), (200, 30)), image((0.3, 0.9, 0.3, 1), imageType=2, fillMethod=0, fillOrigin=0, fillAmount=0.3)], hud)
    b.add("FillV", [rect((0, 0), (0, 0), (0, 0), (240, 220), (30, 60)), image((0.9, 0.3, 0.3, 1), imageType=2, fillMethod=1, fillOrigin=0, fillAmount=0.5)], hud)
    b.add("Aspect", [rect((0, 0), (0, 0), (0, 0), (600, 305), (120, 60)), image(WHITE, "asset:Textures\\NormalBumpTest.png", preserveAspect=True)], hud)
    log = b.add("Log", [counter("../Value", "Value")], hud)
    b.add("Value", [rect((1, 1), (1, 1), (1, 1), (-20, -20), (250, 30)), text("Value", 22, WHITE, align=2)], hud)
    b.slider("Slider", hud, rect((0, 0), (0, 0), (0, 0), (20, 180), (200, 20)), 0.5, calls=[call("../Log", "UiClickCounter.ShowValue")])
    group = b.add("Group", [rect((0, 0), (0, 0), (0, 0), (20, 40), (200, 110)), {"type": "UIToggleGroup", "enabled": True, "allowSwitchOff": False}], hud)
    for i, y in enumerate((80, 50, 20)):
        b.toggle(f"T{i + 1}", group, rect((0, 0), (0, 0), (0, 0), (0, y), (160, 20)), f"Option {i + 1}", i == 0, group="..",
                 calls=[call("../../Log", "UiClickCounter.Say", dynamic=False, string=f"T{i + 1} changed")])
    b.toggle("Solo", hud, rect((0, 0), (0, 0), (0, 0), (260, 60), (160, 20)), "Solo", True, calls=[call("../Log", "UiClickCounter.ShowFlag")])
    b.add("EventSystem", [event_system()])
    save("UiWidgets.json", scene)


# ---------------------------------------------------------------- 5단계: 레이아웃·마스크·스크롤

def stage5():
    # 기대 사각형 (캔버스 ×1, 씬 뷰 445 높이 → 픽셀 y = 445 − 캔버스 y). --log-rect 의 화면 사각형과 비교 (check_ui_layout.py)
    #   V  세로 그룹 (20, 205)~(220, 425) 200×220, 여백 10, 간격 5, 너비 제어·늘이기, 높이 제어
    #      V1 preferredHeight 30 / V2 preferredHeight 40, flexibleHeight 1 / V3 minHeight 20, preferredHeight 30, flexibleHeight 2
    #      합 pref = 10+30+5+40+5+30+10 = 130, 남는 220−130 = 90 → flex 3 에 나눔 (30 / 60) → V1 30, V2 70, V3 90
    #      → V1 y 415..385 (캔버스 위에서 10), V2 380..310, V3 305..215.  x 30..210
    #   H  가로 그룹 (240, 365)~(540, 425) 300×60, 여백 0, 간격 10, 너비 제어(늘이기 끔), 높이 제어·늘이기
    #      H1 preferredWidth 50 / H2 preferredWidth 80, flexibleWidth 1 / H3 preferredWidth 50
    #      합 50+10+80+10+50 = 200, 남는 100 → H2 180 → H1 x 240..290, H2 300..480, H3 490..540, 높이 60 (y 365..425)
    #   G  격자 (240, 205)~(540, 345) 300×140, 칸 60×40, 간격 10×10, 고정 열 4, 가운데 정렬(4) → 7 칸 = 2 줄
    #      필요 폭 4·60 + 3·10 = 270 → 시작 x = 240 + (300 − 270)/2 = 255, 필요 높이 90 → 시작 y(위에서) = (140 − 90)/2 = 25
    #      G1 x 255..315, 위 y 345−25 = 320..280
    #   Fit  ContentSizeFitter 가로·세로 preferred 의 글자 상자 (가운데 피벗) — 크기 = 글자 크기 (눈으로)
    #   Masked  (580, 305)~(700, 425) 120×120 마스크, 자식 Big 200×200 빨강 → 마스크 밖은 배경색
    #   Scroll  (580, 20)~(720, 260) 140×240 (Clamped), 항목 12 개 30 px. 휠 아래 2 칸 (−2) → 내용이 40 위로
    scene, b, hud = hud_scene()
    v = b.add("V", [rect((0, 0), (0, 0), (0, 0), (20, 205), (200, 220)), image((0.2, 0.2, 0.3, 1), raycast=False),
                    vlayout((10, 10, 10, 10), 5.0, control=(True, True), force=(True, False))], hud)
    b.add("V1", [rect((0, 0), (0, 0), (0, 0), (0, 0), (10, 10)), image((1, 0, 0, 1), raycast=False), element(pref_h=30)], v)
    b.add("V2", [rect((0, 0), (0, 0), (0, 0), (0, 0), (10, 10)), image((0, 1, 0, 1), raycast=False), element(pref_h=40, flex_h=1)], v)
    b.add("V3", [rect((0, 0), (0, 0), (0, 0), (0, 0), (10, 10)), image((0, 0, 1, 1), raycast=False), element(min_h=20, pref_h=30, flex_h=2)], v)
    b.add("VIgnored", [rect((1, 1), (1, 1), (1, 1), (0, 0), (20, 20)), image((1, 1, 0, 1), raycast=False), element(ignore=True)], v)
    h = b.add("H", [rect((0, 0), (0, 0), (0, 0), (240, 365), (300, 60)), image((0.2, 0.2, 0.3, 1), raycast=False),
                    hlayout((0, 0, 0, 0), 10.0, control=(True, True), force=(False, True))], hud)
    b.add("H1", [rect((0, 0), (0, 0), (0, 0), (0, 0), (10, 10)), image((1, 0, 0, 1), raycast=False), element(pref_w=50)], h)
    b.add("H2", [rect((0, 0), (0, 0), (0, 0), (0, 0), (10, 10)), image((0, 1, 0, 1), raycast=False), element(pref_w=80, flex_w=1)], h)
    b.add("H3", [rect((0, 0), (0, 0), (0, 0), (0, 0), (10, 10)), image((0, 0, 1, 1), raycast=False), element(pref_w=50)], h)
    g = b.add("G", [rect((0, 0), (0, 0), (0, 0), (240, 205), (300, 140)), image((0.2, 0.2, 0.3, 1), raycast=False),
                    grid((60, 40), (10, 10), constraint=1, count=4, align=4)], hud)
    for i in range(7):
        b.add(f"G{i + 1}", [rect((0, 0), (0, 0), (0, 0), (0, 0), (10, 10)), image((0.9, 0.5 + i * 0.07, 0.2, 1), raycast=False)], g)
    b.add("Fit", [rect((0, 0), (0, 0), (0.5, 0.5), (130, 150), (10, 10)), text("Fit 맞춤 상자", 24, (1, 1, 0.5, 1)), fitter(2, 2)], hud)
    m = b.add("Masked", [rect((0, 0), (0, 0), (0, 0), (580, 305), (120, 120)), image((0.1, 0.4, 0.1, 1), raycast=False),
                         {"type": "UIRectMask", "enabled": True, "padding": [0, 0]}], hud)
    b.add("Big", [rect((0.5, 0.5), (0.5, 0.5), (0.5, 0.5), (0, 0), (200, 200)), image((1, 0, 0, 1), raycast=False)], m)
    root, content = b.scroll_view("Scroll", hud, rect((0, 0), (0, 0), (0, 0), (580, 20), (140, 240)), movement=2)
    for i in range(12):
        b.add(f"Item {i}", [rect((0, 1), (0, 1), (0, 1), (0, 0), (100, 30)), panel((0.9, 0.9, 0.9, 1), border=6.0)], content)
    root, content = b.scroll_view("Elastic", hud, rect((0, 0), (0, 0), (0, 0), (420, 20), (140, 160)), movement=1)
    for i in range(8):
        b.add(f"E{i}", [rect((0, 1), (0, 1), (0, 1), (0, 0), (100, 30)), panel((0.7, 0.85, 1, 1), border=6.0)], content)
    b.add("EventSystem", [event_system()])
    save("UiLayout.json", scene)

    # UiPerf: 1000 요소 (가로 그룹 10 줄 × 이미지 + 글자 50 쌍) — UI Build 시간 (Release, --avg-frames)
    scene, b, hud = hud_scene(bg=False)
    col = b.add("Rows", [stretch(), vlayout((4, 4, 4, 4), 2.0, control=(True, True), force=(True, True))], hud)
    for r in range(10):
        row = b.add(f"Row {r}", [hlayout((0, 0, 0, 0), 2.0, control=(True, True), force=(True, True))], col)
        for i in range(49):
            cell = b.add(f"C{r},{i}", [rect((0, 0), (0, 0), (0, 0), (0, 0), (10, 10)), image(((i % 7) / 7.0, (r % 5) / 5.0, 0.6, 1))], row)
            b.add("T", [stretch(), text(str(i), 10, WHITE, align=4)], cell)
    save("UiPerf.json", scene)


# ---------------------------------------------------------------- 6단계: 내비게이션

def stage6():
    # 버튼 2×2 + Slider + Far. EventSystem firstSelected = Grid/N1. 점수는 유니티 Selectable.FindSelectable 과 같은 식
    # (내 사각형 가장자리 점 → 후보 가운데, dot / 거리², dot ≤ 0 제외).
    #   N1 (20,365) N2 (200,365)      각 160×40 (캔버스 좌하 기준)
    #   N3 (20,305) N4 (200,305)      N2 는 Explicit: 아래 = N3 (자동이면 N4)
    #   Slider (20, 260) 200×20, 0..1 값 0 (키 한 번 = 범위의 10 %)
    #   Far   (500, 40) 160×40
    # 키 (make 뒤 --key=...): Down → N1 (선택만 보임) / Right → N2 / Down → N3 (Explicit) / Right → N4 / Down → Far
    #   (N4 아래 가장자리 (280,305) 에서 Far 0.00163 > Slider 0.00130 — 유니티도 Far) / Enter → submit Far
    #   / Left → Slider (0.00202 > N4 0.00185 > N3 0.00174) / Right ×3 → 0.3 / Left → 0.2 / Up → N3 / Esc → cancel
    scene, b, hud = hud_scene()
    b.add("Log", [counter("../Value", "Nav")], hud)
    b.add("Value", [rect((1, 1), (1, 1), (1, 1), (-20, -20), (300, 30)), text("Nav", 22, WHITE, align=2)], hud)
    g = b.add("Grid", [rect((0, 0), (0, 0), (0, 0), (0, 0), (400, 445))], hud)
    for i, (x, y) in enumerate(((20, 365), (200, 365), (20, 305), (200, 305))):
        extra = {"navigation": 4, "selectOnUp": "", "selectOnDown": "../N3", "selectOnLeft": "../N1", "selectOnRight": ""} if i == 1 else {}
        b.button(f"N{i + 1}", g, rect((0, 0), (0, 0), (0, 0), (x, y), (160, 40)), f"N{i + 1}",
                 [call("../../Log", "UiClickCounter.Say", dynamic=False, string=f"submit N{i + 1}")], **extra)
    b.slider("Slider", hud, rect((0, 0), (0, 0), (0, 0), (20, 260), (200, 20)), 0.0, calls=[call("../Log", "UiClickCounter.ShowValue")])
    b.button("Far", hud, rect((0, 0), (0, 0), (0, 0), (500, 40), (160, 40)), "Far", [call("../Log", "UiClickCounter.Say", dynamic=False, string="submit Far")])
    b.add("EventSystem", [event_system("../HUD/Grid/N1", cancel=[call("../HUD/Log", "UiClickCounter.Say", dynamic=False, string="cancel")])])
    save("UiNav.json", scene)


# ---------------------------------------------------------------- 7단계: Rect 툴

def stage7():
    # Box  P1 (가운데 300×200, 캔버스 가운데 (366, 222.5)) 의 자식, 가운데 앵커 100×50 위치 (0, 0)
    #      → 화면 (316, 197.5)~(416, 247.5). 가운데 (366, 222.5)
    # Rot  회전 30°, 가운데 앵커 120×40, 위치 (−250, 120) (캔버스 원점 = 화면 가운데) — 손잡이를 로컬 축으로
    # P2   늘이기 앵커 (0.6,0.1)~(0.95,0.5) — 재부모화 대상 (앵커가 P1 과 다름)
    scene, b, hud = hud_scene()
    p1 = b.add("P1", [rect((0.5, 0.5), (0.5, 0.5), (0.5, 0.5), (0, 0), (300, 200)), image((0.25, 0.25, 0.35, 1))], hud)
    b.add("Box", [rect((0.5, 0.5), (0.5, 0.5), (0.5, 0.5), (0, 0), (100, 50)), image((1, 0.6, 0.2, 1))], p1)
    b.add("Corner", [rect((0, 0), (0, 0), (0, 0), (10, 10), (40, 40)), image((0.3, 0.8, 1, 1))], p1)
    b.add("Rot", [rect((0.5, 0.5), (0.5, 0.5), (0.5, 0.5), (-250, 120), (120, 40)), image((0.5, 1, 0.5, 1))], hud, rotation=math.radians(30.0))
    b.add("P2", [rect((0.6, 0.1), (0.95, 0.5), (0.5, 0.5), (0, 0), (0, 0)), image((0.35, 0.25, 0.25, 1))], hud)
    save("UiRectTool.json", scene)


# ---------------------------------------------------------------- 8단계: World Space

def world_scene(enabled=True):
    # 헬멧(원점, 높이 ~1 m) 오른쪽 앞에 World 캔버스 Panel (0.004 m/단위, 400×250 = 1.6×1 m), y 축 −25° 기울임.
    # 그 앞을 기둥(0.3×2.4×0.3 m)이 가로질러 일부를 가림. 헬멧 위 이름표(Billboard).
    scene = load("DamagedHelmet.json")
    cube, mat = with_cube(scene)
    b = Builder(scene)
    b.add("Pillar", [], position=(0.55, 1.2, -1.3), scale=(0.3, 2.4, 0.3), mesh=cube, material=mat)
    comps = canvas(world=True, ppu=2.0)
    comps[0]["enabled"] = enabled
    w = b.add("World Panel", comps + [rect((0.5, 0.5), (0.5, 0.5), (0.5, 0.5), (0, 0), (400, 250))],
              position=(0.9, 1.2, -0.6), rotation=(0.0, math.radians(-25.0), 0.0), scale=(0.004, 0.004, 0.004))
    b.add("Back", [stretch(), panel((0.1, 0.15, 0.25, 0.92))], w)
    b.add("Title", [rect((0, 1), (1, 1), (0.5, 1), (0, -14), (-28, 50)), text("World Space 패널", 36, WHITE, align=4)], w)
    b.add("Count", [rect((0, 0), (1, 0), (0.5, 0), (0, 100), (-28, 40)), text("Clicks: 0", 30, (0.8, 0.9, 1, 1), align=4)], w)
    b.add("Counter", [counter("../Count", "Clicks")], w)
    b.button("Press", w, rect((0.5, 0), (0.5, 0), (0.5, 0), (0, 24), (240, 60)), "Press", [call("../Counter", "UiClickCounter.Increment")], font_size=30)
    comps = canvas(world=True, ppu=2.0)
    comps[0]["enabled"] = enabled
    n = b.add("Nameplate", comps + [rect((0.5, 0.5), (0.5, 0.5), (0.5, 0.5), (0, 0), (300, 60)), {"type": "Billboard", "enabled": True, "yawOnly": True}],
              position=(0.0, 1.75, 0.0), scale=(0.003, 0.003, 0.003))
    b.add("Back", [stretch(), panel((0, 0, 0, 0.6), raycast=False)], n)
    b.add("Name", [stretch(), text("Damaged Helmet", 30, (1, 0.9, 0.5, 1), align=4)], n)
    scene["camera"] = {"pitch": 0.15, "position": [1.2, 1.7, -3.6], "yaw": -0.2}
    for o in scene["objects"]:
        if o["name"] == "Main Camera":
            o["position"] = [1.2, 1.7, -3.6]
            o["rotation"] = [0.15, -0.2, 0.0]
    return scene, b


def stage8():
    scene, b = world_scene(True)
    b.add("EventSystem", [event_system()])
    save("UiWorld.json", scene)
    scene, b = world_scene(False)
    save("UiWorldNoUI.json", scene)


# ---------------------------------------------------------------- 최종 쇼케이스 (Assets/Scenes/UiShowcase.json)

def showcase():
    # 1280×720 기준 Scale With Screen Size (match 0.5) — 에디터 씬 뷰·게임 창 어느 크기에서도 같은 배치.
    #   왼쪽 메뉴 패널: 세로 레이아웃 (제목, 버튼 둘, 클릭 수, 품질 토글 그룹 3, 볼륨 슬라이더, 체력 표시 토글)
    #   오른쪽 인벤토리: Scroll View (항목 16, 휠·드래그·스크롤바)
    #   아래: 체력 막대 (Filled), 단축칸 격자 6
    #   World: 헬멧 위 이름표 (Billboard), 기둥 뒤로 반쯤 가려진 키오스크 패널과 버튼
    #   EventSystem: 처음 선택 = 메뉴의 Play 버튼, Esc/패드 B = 상태 줄에 "취소"
    #   카메라: FreeCamera (오른쪽 버튼 회전 — UI 위에서 누르면 회전하지 않음, WASD 이동)
    scene = load("DamagedHelmet.json")
    cube, mat = with_cube(scene)
    b = Builder(scene)
    for o in scene["objects"]:
        if o["name"] == "Main Camera":
            o["components"].append({"type": "FreeCamera", "enabled": True, "speed": 3.0, "boost": 3.0, "lookSpeed": 0.0025})
            o["position"] = [0.6, 1.35, -3.6]
            o["rotation"] = [0.05, 0.0, 0.0]
    scene["camera"] = {"pitch": 0.05, "position": [0.6, 1.35, -3.6], "yaw": 0.0}

    hud = b.add("HUD", canvas(mode=1, ref=(1280.0, 720.0), match=0.5))
    b.add("Top Bar", [rect((0, 1), (1, 1), (0.5, 1), (0, 0), (0, 84)), image((0.04, 0.05, 0.09, 0.78), raycast=False)], hud)
    b.add("Title", [rect((0.5, 1), (0.5, 1), (0.5, 1), (0, -10), (700, 40)), text("SherlockEngine 게임 UI", 30, WHITE, align=1)], hud)
    b.add("Hint", [rect((0.5, 1), (0.5, 1), (0.5, 1), (0, -50), (900, 26)),
                   text("마우스 · 방향키/Enter/Esc · 게임패드 · 오른쪽 버튼으로 시점, WASD 이동", 16, (0.8, 0.85, 0.95, 1), align=1)], hud)
    b.add("Status", [rect((1, 1), (1, 1), (1, 1), (-24, -96), (420, 30)), text("상태: 준비", 20, (1, 0.9, 0.5, 1), align=2)], hud)
    b.add("Status Log", [counter("../Status", "상태", write_on_start=False)], hud)

    # 왼쪽 메뉴
    menu = b.add("Menu", [rect((0, 0.5), (0, 0.5), (0, 0.5), (24, -20), (300, 470)), panel((0.08, 0.1, 0.16, 0.88)),
                          vlayout((18, 18, 18, 18), 10.0, control=(True, False), force=(True, False)),
                          {"type": "UIToggleGroup", "enabled": True, "allowSwitchOff": False}], hud)
    b.add("Header", [rect((0, 1), (0, 1), (0, 1), (0, 0), (100, 34)), text("메뉴", 24, WHITE, align=3)], menu)
    b.add("Click Log", [counter("../Clicks", "클릭")], menu)
    b.button("Play", menu, rect((0, 1), (0, 1), (0, 1), (0, 0), (100, 40)), "클릭", [call("../Click Log", "UiClickCounter.Increment")])
    b.button("Reset", menu, rect((0, 1), (0, 1), (0, 1), (0, 0), (100, 40)), "초기화", [call("../Click Log", "UiClickCounter.ResetCount")], color=(0.85, 0.85, 0.9, 1))
    b.add("Clicks", [rect((0, 1), (0, 1), (0, 1), (0, 0), (100, 28)), text("클릭: 0", 20, (0.7, 0.9, 1, 1), align=3)], menu)
    b.add("Quality Header", [rect((0, 1), (0, 1), (0, 1), (0, 0), (100, 24)), text("그래픽 품질", 18, (0.8, 0.8, 0.85, 1), align=3)], menu)
    for i, name in enumerate(("낮음", "보통", "높음")):
        b.toggle(f"Quality {name}", menu, rect((0, 1), (0, 1), (0, 1), (0, 0), (100, 22)), name, i == 1, group="..",
                 calls=[call("../../Status Log", "UiClickCounter.Say", dynamic=False, string=f"품질: {name}")])
    b.add("Volume Header", [rect((0, 1), (0, 1), (0, 1), (0, 0), (100, 24)), text("볼륨", 18, (0.8, 0.8, 0.85, 1), align=3)], menu)
    b.add("Volume Log", [counter("../Volume Value", "볼륨", write_on_start=False)], menu)
    b.slider("Volume", menu, rect((0, 1), (0, 1), (0, 1), (0, 0), (100, 22)), 70.0, 0.0, 100.0, True, [call("../Volume Log", "UiClickCounter.ShowValue")])
    b.add("Volume Value", [rect((0, 1), (0, 1), (0, 1), (0, 0), (100, 24)), text("볼륨: 70", 18, (0.7, 0.9, 1, 1), align=3)], menu)

    # 오른쪽 인벤토리
    b.add("Inventory Header", [rect((1, 0.5), (1, 0.5), (1, 0), (-24, 175), (220, 30)), text("인벤토리", 22, WHITE, align=3)], hud)
    inv, content = b.scroll_view("Inventory", hud, rect((1, 0.5), (1, 0.5), (1, 0.5), (-24, 10), (220, 330)), bar_value=1.0, bar_size=0.4)
    items = ["검", "방패", "활", "화살 ×20", "물약", "해독제", "지도", "열쇠", "횃불", "밧줄", "보석", "편지", "망치", "곡괭이", "빵", "물병"]
    for i, name in enumerate(items):
        b.button(f"Item {i + 1}", content, rect((0, 1), (0, 1), (0, 1), (0, 0), (100, 34)), f"{i + 1}. {name}",
                 [call("../../../../Status Log", "UiClickCounter.Say", dynamic=False, string=f"선택: {name}")], color=(0.92, 0.94, 1, 1), font_size=17)

    # 아래: 체력 막대, 단축칸
    hp = b.add("Health", [rect((0.5, 0), (0.5, 0), (0.5, 0), (0, 96), (420, 26)), panel((0, 0, 0, 0.6), border=8.0)], hud)
    b.add("Fill", [stretch(3, 3, 3, 3), image((0.85, 0.2, 0.25, 1), "builtin:ui-panel", False, imageType=2, fillMethod=0, fillOrigin=0, fillAmount=0.65)], hp)
    b.add("Label", [stretch(), text("HP 65 / 100", 16, WHITE, align=4)], hp)
    bar = b.add("Hotbar", [rect((0.5, 0), (0.5, 0), (0.5, 0), (0, 20), (420, 64)), panel((0, 0, 0, 0.5)),
                           grid((56, 56), (8, 8), constraint=2, count=1, pad=(4, 4, 4, 4), align=4)], hud)
    for i in range(6):
        b.button(f"Slot {i + 1}", bar, rect((0, 0), (0, 0), (0, 0), (0, 0), (56, 56)), str(i + 1),
                 [call("../../Status Log", "UiClickCounter.Say", dynamic=False, string=f"단축칸 {i + 1}")], color=(0.75, 0.8, 0.9, 1), font_size=22)

    b.add("EventSystem", [event_system("../HUD/Menu/Play", cancel=[call("../HUD/Status Log", "UiClickCounter.Say", dynamic=False, string="취소 (Esc / B)")])])

    # World Space
    n = b.add("Nameplate", canvas(world=True, ppu=2.0) + [rect((0.5, 0.5), (0.5, 0.5), (0.5, 0.5), (0, 0), (320, 64)),
                                                          {"type": "Billboard", "enabled": True, "yawOnly": True}],
              position=(0.0, 2.05, 0.0), scale=(0.003, 0.003, 0.003))
    b.add("Back", [stretch(), panel((0, 0, 0, 0.6), raycast=False)], n)
    b.add("Name", [stretch(), text("Damaged Helmet", 32, (1, 0.9, 0.5, 1), align=4)], n)
    # 키오스크: 헬멧 오른쪽 앞, 카메라 쪽으로 비스듬히. 기둥이 왼쪽 일부를 가림
    b.add("Pillar", [], position=(0.95, 1.2, -1.5), scale=(0.15, 2.4, 0.15), mesh=cube, material=mat)
    k = b.add("Kiosk", canvas(world=True, ppu=2.0) + [rect((0.5, 0.5), (0.5, 0.5), (0.5, 0.5), (0, 0), (360, 240))],
              position=(1.35, 1.15, -0.9), rotation=(0.0, math.radians(-35.0), 0.0), scale=(0.003, 0.003, 0.003))
    b.add("Back", [stretch(), panel((0.1, 0.15, 0.25, 0.92))], k)
    b.add("Title", [rect((0, 1), (1, 1), (0.5, 1), (0, -12), (-24, 44)), text("키오스크 (World Space)", 28, WHITE, align=4)], k)
    b.add("Count", [rect((0, 0), (1, 0), (0.5, 0), (0, 92), (-24, 40)), text("누름: 0", 28, (0.8, 0.9, 1, 1), align=4)], k)
    b.add("Counter", [counter("../Count", "누름")], k)
    b.button("Press", k, rect((0.5, 0), (0.5, 0), (0.5, 0), (0, 22), (220, 56)), "누르기", [call("../Counter", "UiClickCounter.Increment")], font_size=26)
    save("UiShowcase.json", scene)


def main():
    global OUT
    parser = argparse.ArgumentParser()
    parser.add_argument("--verify-dir", default="")
    args = parser.parse_args()
    if args.verify_dir:
        OUT = os.path.abspath(args.verify_dir)
        os.makedirs(OUT, exist_ok=True)
        stage1_2()
        stage3()
        stage4()
        stage5()
        stage6()
        stage7()
        stage8()
    else:
        showcase()


if __name__ == "__main__":
    main()
