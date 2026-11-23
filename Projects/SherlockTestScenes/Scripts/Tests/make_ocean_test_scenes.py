# Gerstner 바다 테스트 씬 생성 (docs/plan/ocean-gerstner-plan.html).
#
#   Ocean.json   1단계: 200 m × 200 m, 256 × 256 정점 평면 + Ocean 컴포넌트, 원점에 배 자리 상자 (그림자 받기 확인용),
#                갑판 높이 8 m 카메라. 하늘·IBL 은 DamagedHelmet.json 의 것을 그대로 씀 (기존 씬은 고치지 않음).
#   OceanHorizon.json  2단계: 10 km × 10 km, 512 × 512 정점, 간격 증가 g = 0.93 (원점 1.37 m, 80 m 안 1.4 m 이하,
#                가장자리 127 m), 거리 감쇠 80 → 300 m, 씬 카메라·게임 카메라 far 5000 m (갑판 8 m 에서 수평선은 약 10 km 앞).
#
# 실행: python make_ocean_test_scenes.py  (이 파일 기준으로 ../../Assets/Scenes 에 씀)
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


def material(name, base, metallic, roughness, ior=1.5):
    return {
        "albedoSrgb": True, "albedoTexture": "", "alphaCutoff": 0.0, "baseColor": base + [1.0], "doubleSided": False,
        "emissive": [0.0, 0.0, 0.0], "emissiveTexture": "", "ior": ior, "metallic": metallic, "metallicRoughnessTexture": "",
        "name": name, "normalStrength": 1.0, "normalTexture": "", "occlusionStrength": 1.0, "occlusionTexture": "",
        "roughness": roughness, "sampler": 2, "specularFactor": 1.0, "unlit": False, "uvScale": [1.0, 1.0], "wireframe": False,
    }


OCEAN_DEFAULTS = {
    "type": "Ocean", "enabled": True, "waveCount": 6, "windDirection": 30.0, "directionSpread": 40.0,
    "wavelengthMin": 4.0, "wavelengthMax": 32.0, "amplitude": 0.25, "steepness": 0.4, "speed": 1.0, "seed": 1,
    "fadeStart": 80.0, "fadeEnd": 300.0,
}


def make_ocean_scene(size, verts, camera_pos, camera_target, far_z, ocean_extra=None, growth=0.0):
    base = load("DamagedHelmet.json")
    yaw, pitch = look(camera_pos, camera_target)
    env = dict(base["environment"])
    env["shadow"] = dict(env["shadow"], distance=60.0, orthoSize=40.0)
    ocean = dict(OCEAN_DEFAULTS)
    ocean.update(ocean_extra or {})
    # 해: 카메라 앞쪽 하늘 (고도 약 20°) → 해 하이라이트가 카메라 쪽으로 이어지고 상자 그림자가 카메라 쪽으로 떨어짐
    fwd = (math.sin(yaw), math.cos(yaw))
    elev = math.radians(20.0)
    sun_dir = [-fwd[0] * math.cos(elev), -math.sin(elev), -fwd[1] * math.cos(elev)]
    return {
        "ambientColor": [0.05, 0.06, 0.07],
        "camera": dict({"pitch": pitch, "position": camera_pos, "yaw": yaw}, **({"farZ": far_z} if far_z != 1000.0 else {})),
        "clearColor": base["clearColor"],
        "environment": env,
        "lights": [{
            "attenuation": [1.0, 0.045, 0.0075], "castShadows": True, "color": [1.0, 0.95, 0.85], "direction": sun_dir,
            "innerConeCos": 0.92, "intensity": 2.5, "outerConeCos": 0.87, "position": [0.0, 50.0, 0.0], "range": 50.0, "type": 0,
        }],
        "materials": [
            material("Ocean", [0.01, 0.04, 0.05], 0.0, 0.06, ior=1.33),
            material("Hull", [0.45, 0.42, 0.38], 0.0, 0.6),
        ],
        "meshes": [
            {"a": verts, "b": verts, "color": [1.0, 1.0, 1.0, 1.0], "params": [size, size, growth, 0.0], "type": "plane"},
            {"color": [1.0, 1.0, 1.0, 1.0], "params": [1.0, 0.0, 0.0, 0.0], "type": "cube"},
        ],
        "objects": [
            {"components": [ocean], "id": 1, "material": 0, "mesh": 0, "name": "Ocean",
             "position": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]},
            # 배 자리: 6 × 3 × 16 m 상자, 수면 위로 1.5 m
            {"id": 2, "material": 1, "mesh": 1, "name": "ShipStandIn",
             "position": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0], "scale": [6.0, 3.0, 16.0]},
            {"components": [
                {"blendTime": 0.5, "enabled": True, "farZ": far_z, "fovY": 60.0, "nearZ": 0.1, "orthoSize": 25.0,
                 "orthographic": False, "priority": 0, "type": "CameraComponent"},
                {"boost": 3.0, "enabled": True, "lookSpeed": 0.0025, "speed": 4.0, "type": "FreeCamera"}],
             "id": 3, "material": -1, "mesh": -1, "name": "Main Camera", "position": camera_pos,
             "rotation": [pitch, yaw, 0.0], "scale": [1.0, 1.0, 1.0]},
        ],
        "version": base["version"],
    }


if __name__ == "__main__":
    save("Ocean.json", make_ocean_scene(200.0, 256, [-14.0, 8.0, -24.0], [0.0, 0.0, 0.0], 1000.0))
    # 수평선 쪽을 보는 카메라 (상자를 화면 아래쪽에 두고 위쪽 절반에 먼 바다·수평선)
    save("OceanHorizon.json", make_ocean_scene(10000.0, 512, [-14.0, 8.0, -24.0], [14.0, 2.0, 24.0], 5000.0, growth=0.93))
