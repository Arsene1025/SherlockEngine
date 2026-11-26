# 디졸브(Dissolve 컴포넌트 + 재질 노이즈) 테스트 씬 생성 (docs/plan/object-highlight-record.html 7장).
#
#   DissolveZero.json       DamagedHelmet.json 의 헬멧에 amount 0 인 Dissolve → DamagedHelmet 과 픽셀이 같아야 함
#   Dissolve.json           헬멧 amount 0.5 (기본 노이즈 builtin:noise, 재질 칸은 비어 있음). 바닥의 그림자도 같은 무늬로 뚫려야 함
#   DissolveChecker.json    Dissolve.json + 헬멧 재질 둘의 dissolveNoiseTexture = builtin:checker → 재질별 노이즈 (격자 모양으로 사라짐)
#   DissolveGone.json       헬멧 amount 1 → 몸도 그림자도 없어야 함
#   DissolveNoHelmet.json   헬멧 오브젝트를 뺀 씬 (DissolveGone 과 픽셀이 같아야 함 — 그림자까지 사라졌다는 증거)
#   DissolveHierarchy.json  MasterChiefWalk.json 의 마스터치프 루트(메시 없음)에 amount 0.5 → 자식 스킨 메시 4 개(재질 4 개, 노이즈 칸 비어 있음)가
#                           물려받아 사라짐. 첫 자식 01.Arms 에는 amount 0 인 Dissolve → 팔만 그대로
#   DissolveFile.json       Dissolve.json + 헬멧 재질 둘의 dissolveNoiseTexture = NormalBumpTest.png (Assets/Textures 의 파일) → 패키지 빌드
#                           (--build-minimal=1)가 노이즈 파일을 싣는지 확인용
#   DissolveOut.json        DamagedHelmet.json + 헬멧에 DissolveOut 스크립트만 (Dissolve 는 재생 중 스크립트가 붙임, --play=1 로 확인)
#
# 실행: python make_dissolve_test_scenes.py  (이 파일 기준으로 ../../Assets/Scenes 에 씀. 기존 씬은 고치지 않음)
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
SCENES = os.path.normpath(os.path.join(HERE, "..", "..", "Assets", "Scenes"))
HELMET = "node_damagedHelmet_-6514"


def load(name):
    with open(os.path.join(SCENES, name), encoding="utf-8-sig") as f:
        return json.load(f)


def save(name, scene):
    text = json.dumps(scene, indent=2, ensure_ascii=False)
    with open(os.path.join(SCENES, name), "w", encoding="utf-8", newline="\r\n") as f:
        f.write(text + "\n")
    print("wrote", name)


def dissolve(**values):
    d = {"type": "Dissolve", "enabled": True, "amount": 0.0, "edgeWidth": 0.05, "edgeColor": [1.0, 0.45, 0.1, 1.0], "edgeIntensity": 4.0}
    d.update(values)
    return d


def add_component(obj, component):
    obj.setdefault("components", []).append(component)


def by_name(scene, name):
    found = [o for o in scene["objects"] if o["name"] == name]
    assert len(found) == 1, name
    return found[0]


def helmet_scene(**values):
    scene = load("DamagedHelmet.json")
    add_component(by_name(scene, HELMET), dissolve(**values))
    return scene


def remove_object(scene, name):
    # 부모 인덱스가 있는 씬이면 번호가 밀리므로, 루트이고 자식이 없는 오브젝트만 뺌
    objects = scene["objects"]
    index = next(i for i, o in enumerate(objects) if o["name"] == name)
    assert objects[index].get("parent") is None and not any(o.get("parent") == index for o in objects), name
    assert not any(o.get("parent") is not None for o in objects), "parent 가 있는 씬은 지원하지 않음"
    del objects[index]


if __name__ == "__main__":
    save("DissolveZero.json", helmet_scene(amount=0.0))
    save("Dissolve.json", helmet_scene(amount=0.5))

    checker = helmet_scene(amount=0.5)
    slots = by_name(checker, HELMET)["slotMaterials"]
    for slot in slots:
        checker["materials"][slot]["dissolveNoiseTexture"] = "builtin:checker"
    save("DissolveChecker.json", checker)

    from_file = helmet_scene(amount=0.5)
    for slot in by_name(from_file, HELMET)["slotMaterials"]:
        from_file["materials"][slot]["dissolveNoiseTexture"] = "NormalBumpTest.png"
    save("DissolveFile.json", from_file)

    save("DissolveGone.json", helmet_scene(amount=1.0))
    no_helmet = load("DamagedHelmet.json")
    remove_object(no_helmet, HELMET)
    save("DissolveNoHelmet.json", no_helmet)

    hierarchy = load("MasterChiefWalk.json")
    objects = hierarchy["objects"]
    root = next(i for i, o in enumerate(objects) if o.get("parent") is None and o["name"].startswith("MasterChief"))
    add_component(objects[root], dissolve(amount=0.5))
    children = [i for i, o in enumerate(objects) if o.get("parent") == root and o.get("mesh", -1) >= 0]
    add_component(objects[children[0]], dissolve(amount=0.0))
    print("hierarchy root", objects[root]["name"], "zero child", objects[children[0]]["name"])
    save("DissolveHierarchy.json", hierarchy)

    out = load("DamagedHelmet.json")
    add_component(by_name(out, HELMET), {"type": "DissolveOut", "enabled": True, "delay": 0.2, "duration": 1.5, "hold": 0.5})
    save("DissolveOut.json", out)
