# 비 테스트 씬 생성 (docs/plan/rain-plan.html). 씬 하나로 1~3단계를 다 봄.
#
#   Rain.json   Sponza.json 복사본 + 안뜰에 물건 둘 + Rain 컴포넌트 하나.
#               - 상자 지붕: 기둥 네 개 위 납작한 상자 (판 하나면 단면 재질의 뒷면 컬링 때문에 위에서 볼 때 빠질 수 있어 상자로 둠).
#                 지붕 아래 바닥은 마르고 빗줄기도 없어야 함. 지붕 윗면에는 튐.
#               - 잎 판: Sponza 잎 컷아웃 텍스처 (material0 과 같은 텍스처, 양면) 를 수평 판에 붙임. 판 아래 바닥은 잎 무늬대로 젖음
#                 (차폐 맵이 그림자 PSO 의 알파 테스트 변형으로 그려지는지).
#               - Sponza 회랑: 위층 바닥이 지붕 노릇 — 회랑 바닥은 마르고 안뜰 바닥은 젖고 웅덩이·물결, 기둥은 바닥보다 덜 젖음.
#               카메라는 안뜰 한쪽 끝에서 상자 지붕·잎 판·회랑이 같이 보이게 비스듬히 봄.
#
# 검증 실행 예: SherlockEditor.exe --project=<SherlockTestScenes.sherlock> --scene=Rain.json --rain-time=10 --exit-after=40 --screenshot=<png>
#              (--debug-view=18 → R 비를 맞음, G 젖음, B 웅덩이)
# 실행: python make_rain_test_scene.py  (이 파일 기준으로 ../../Assets/Scenes 에 씀. 기존 씬은 고치지 않음)
import copy
import json
import math
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


def look(position, target):
    # 엔진 카메라 관례: forward = (cos(pitch) sin(yaw), −sin(pitch), cos(pitch) cos(yaw)), pitch 양수 = 아래
    dx, dy, dz = (t - p for t, p in zip(target, position))
    yaw = math.atan2(dx, dz)
    pitch = -math.atan2(dy, math.hypot(dx, dz))
    return yaw, pitch


def add_material(scene, base_index, **values):
    material = copy.deepcopy(scene["materials"][base_index])
    material.update(values)
    scene["materials"].append(material)
    return len(scene["materials"]) - 1


def add_mesh(scene, mesh):
    scene["meshes"].append(mesh)
    return len(scene["meshes"]) - 1


def add_object(scene, name, mesh=-1, material=-1, position=(0.0, 0.0, 0.0), scale=(1.0, 1.0, 1.0), components=None):
    next_id = max(o["id"] for o in scene["objects"]) + 1
    obj = {"id": next_id, "material": material, "mesh": mesh, "name": name, "position": list(position),
           "rotation": [0.0, 0.0, 0.0], "scale": list(scale)}
    if components:
        obj["components"] = components
    scene["objects"].append(obj)
    return obj


def main():
    scene = load("Sponza.json")

    plain = dict(albedoTexture="", metallicRoughnessTexture="", normalTexture="", occlusionTexture="", emissiveTexture="",
                 metallic=0.0, roughness=0.8, alphaCutoff=0.0, doubleSided=False, uvScale=[1.0, 1.0])
    roof_material = add_material(scene, 1, name="RainTest/Roof", baseColor=[0.45, 0.32, 0.25, 1.0], **plain)
    pillar_material = add_material(scene, 1, name="RainTest/Pillar", baseColor=[0.7, 0.7, 0.68, 1.0], **plain)
    leaf_material = add_material(scene, 0, name="RainTest/Leaves", doubleSided=True, metallic=0.0, uvScale=[1.0, 1.0])

    cube = add_mesh(scene, {"a": 0, "b": 0, "color": [1.0, 1.0, 1.0, 1.0], "params": [1.0, 0.0, 0.0, 0.0], "type": "cube"})
    plate = add_mesh(scene, {"a": 1, "b": 1, "color": [1.0, 1.0, 1.0, 1.0], "params": [3.0, 3.0, 0.0, 0.0], "type": "plane"})

    # 상자 지붕 (안뜰 서쪽): 3 × 0.2 × 3 m, 윗면 높이 2.6 m. 기둥 네 개.
    roof_center = (-3.0, 0.0, 0.0)
    add_object(scene, "RainTest/Roof", cube, roof_material, (roof_center[0], 2.5, roof_center[2]), (3.0, 0.2, 3.0))
    for i, (sx, sz) in enumerate(((-1, -1), (1, -1), (-1, 1), (1, 1))):
        add_object(scene, "RainTest/Pillar%d" % i, cube, pillar_material, (roof_center[0] + sx * 1.3, 1.2, roof_center[2] + sz * 1.3), (0.15, 2.4, 0.15))

    # 잎 판 (안뜰 동쪽): 3 × 3 m 수평, 높이 1.8 m (카메라 2.8 m 에서 윗면이 보이게).
    add_object(scene, "RainTest/Leaves", plate, leaf_material, (3.0, 1.8, 0.0))

    # 비 (기본값 = Scene/Rain.h). 오브젝트 위치는 쓰지 않음.
    rain = {"type": "Rain", "enabled": True, "intensity": 1.0, "maxDrops": 20000, "areaRadius": 15.0, "areaHeight": 12.0,
            "fallSpeed": 9.0, "wind": [1.5, 0.0, 0.5], "streakLength": 0.4, "streakWidth": 0.004, "color": [0.7, 0.75, 0.8],
            "brightness": 1.0, "nearFade": 0.5, "wetness": 1.0, "wetTime": 20.0, "dryTime": 60.0, "occlusionSize": 80.0,
            "occlusionResolution": 1024, "seed": 1, "splashes": True, "ripples": True, "puddles": 0.5}
    add_object(scene, "Rain", components=[rain])

    # 카메라: 안뜰 (x −9..8 m, z −2..2 m) 동쪽 끝 2.8 m 높이에서 서쪽 (잎 판·상자 지붕·양쪽 회랑이 같이 보임)
    position = [7.5, 2.8, 0.8]
    yaw, pitch = look(position, [-4.0, 0.5, -0.3])
    scene["camera"]["position"] = position
    scene["camera"]["yaw"] = yaw
    scene["camera"]["pitch"] = pitch
    for obj in scene["objects"]:
        if any(c.get("type") == "CameraComponent" for c in obj.get("components", [])):
            obj["position"] = position
            obj["rotation"] = [pitch, yaw, 0.0]
    save("Rain.json", scene)


if __name__ == "__main__":
    main()
