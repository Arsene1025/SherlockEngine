# 오브젝트별 강조(Highlight 컴포넌트) 테스트 씬 생성 (docs/plan/object-highlight-record.html).
#
#   HighlightZero.json       DeferredMaterials.json 의 모든 오브젝트에 값이 0 인 Highlight → DeferredMaterials 와 픽셀이 같아야 함
#   Highlight.json           DeferredMaterials.json 의 재질별로 효과 하나씩 (디퍼드 재질 D_*, 포워드로 빠지는 재질 F_*)
#                              D_Dielectric 색 섞기 / D_Metal 색 섞기 (금속도 색이 바뀌는지) / D_Clearcoat 번쩍임 /
#                              D_Cutout 가장자리 빛 / F_ClearcoatNormal 색 + 가장자리 / F_Transmission 번쩍임 /
#                              F_Unlit 색 + 가장자리 (unlit 경로) / F_Blend 번쩍임 (블렌딩) / D_BehindBlend·Floor 는 그대로 (대조)
#   HighlightHierarchy.json  MasterChiefWalk.json 의 마스터 치프 루트(메시 없음)에 Highlight → 자식 스킨 메시가 물려받는지.
#                              첫 자식 메시에는 값이 0 인 Highlight → 가까운 쪽이 이겨 그 부위만 원래 색
#   HighlightHitFlash.json   Highlight.json + D_Dielectric 에 HitFlash 스크립트 (재생 중 1 초마다 번쩍임, --play=1 로 확인)
#
# 실행: python make_highlight_test_scenes.py  (이 파일 기준으로 ../../Assets/Scenes 에 씀. 기존 씬은 고치지 않음)
import copy
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
SCENES = os.path.normpath(os.path.join(HERE, "..", "..", "Assets", "Scenes"))


def load(name):
    with open(os.path.join(SCENES, name), encoding="utf-8-sig") as f:
        return json.load(f)


def save(name, scene):
    text = json.dumps(scene, indent=2, ensure_ascii=False)
    with open(os.path.join(SCENES, name), "w", encoding="utf-8", newline="\r\n") as f:
        f.write(text + "\n")
    print("wrote", name)


def highlight(**values):
    h = {
        "type": "Highlight", "enabled": True,
        "tintColor": [1.0, 0.0, 0.0, 1.0], "tintAmount": 0.0,
        "flashColor": [1.0, 1.0, 1.0, 1.0], "flashIntensity": 0.0,
        "rimColor": [1.0, 0.2, 0.1, 1.0], "rimIntensity": 0.0, "rimPower": 3.0,
    }
    h.update(values)
    return h


def add_component(obj, component):
    obj.setdefault("components", []).append(component)


def by_name(scene, name):
    found = [o for o in scene["objects"] if o["name"] == name]
    assert len(found) == 1, name
    return found[0]


RED = [1.0, 0.05, 0.05, 1.0]
ORANGE = [1.0, 0.35, 0.05, 1.0]
CYAN = [0.1, 0.8, 1.0, 1.0]

EFFECTS = {
    "D_Dielectric": highlight(tintColor=RED, tintAmount=0.7),
    "D_Metal": highlight(tintColor=RED, tintAmount=0.7),
    "D_Clearcoat": highlight(flashIntensity=1.5),
    "D_Cutout": highlight(rimColor=ORANGE, rimIntensity=3.0, rimPower=3.0),
    "F_ClearcoatNormal": highlight(tintColor=RED, tintAmount=0.5, rimColor=CYAN, rimIntensity=2.0, rimPower=2.0),
    "F_Transmission": highlight(flashColor=RED, flashIntensity=1.0),
    "F_Unlit": highlight(tintColor=RED, tintAmount=0.5, rimColor=CYAN, rimIntensity=2.0, rimPower=2.0),
    "F_Blend": highlight(flashColor=RED, flashIntensity=1.0),
}


def make_zero():
    scene = load("DeferredMaterials.json")
    for obj in scene["objects"]:
        if obj.get("mesh", -1) >= 0:
            add_component(obj, highlight())
    save("HighlightZero.json", scene)


def make_effects():
    scene = load("DeferredMaterials.json")
    for name, component in EFFECTS.items():
        add_component(by_name(scene, name), copy.deepcopy(component))
    save("Highlight.json", scene)
    return scene


def make_hit_flash(effects_scene):
    scene = copy.deepcopy(effects_scene)
    add_component(by_name(scene, "D_Dielectric"),
                  {"type": "HitFlash", "enabled": True, "interval": 1.0, "duration": 0.15, "peakTint": 0.8, "peakFlash": 2.0})
    save("HighlightHitFlash.json", scene)


def make_hierarchy():
    scene = load("MasterChiefWalk.json")
    objects = scene["objects"]
    # 씬 카메라에 보이는 모델은 마스터 치프 하나 (루트 MasterChief_v5.5_walk 1, 스킨 메시 자식 4 개)
    root = next(i for i, o in enumerate(objects) if o.get("parent") is None and o["name"].startswith("MasterChief"))
    add_component(objects[root], highlight(tintColor=RED, tintAmount=0.6, rimColor=ORANGE, rimIntensity=2.0))
    children = [i for i, o in enumerate(objects) if o.get("parent") == root and o.get("mesh", -1) >= 0]
    assert children, "MasterChief 자식 메시 없음"
    child = children[0]
    add_component(objects[child], highlight())
    print("hierarchy root", objects[root]["name"], "children", [objects[i]["name"] for i in children],
          "zero child", objects[child]["name"])
    save("HighlightHierarchy.json", scene)


if __name__ == "__main__":
    make_zero()
    make_hit_flash(make_effects())
    make_hierarchy()
