# 컬링 카메라 고정 테스트 씬 생성 (docs/plan/freeze-culling-camera-plan.html).
#
#   FreezeCulling.json  CascadeBigScene.json 복사본에서 씬 카메라만 바꿈. 기둥 길 안쪽 낮은 자리(0, 3, 20)에서 오른쪽 앞
#                       (25, 2, 80)을 봄 — 왼쪽·뒤의 기둥이 절두체 밖이라 기본 시점에서 컬링되는 오브젝트가 생김
#                       (원본은 전체를 내려다보는 시점이라 culled = 0). 검증은 이 시점으로 --freeze-culling=N 을 건 뒤
#                       --cam-look 으로 원본 카메라 자리(내려다보는 시점)로 옮겨 빠진 기둥을 봄.
#
# 실행: python make_freeze_culling_test_scene.py  (이 파일 기준으로 ../../Assets/Scenes 에 씀, 기존 씬은 고치지 않음)
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


def main():
    scene = load("CascadeBigScene.json")
    position = [0.0, 3.0, 20.0]
    yaw, pitch = look(position, [25.0, 2.0, 80.0])
    scene["camera"]["position"] = position
    scene["camera"]["yaw"] = yaw
    scene["camera"]["pitch"] = pitch
    save("FreezeCulling.json", scene)


if __name__ == "__main__":
    main()
