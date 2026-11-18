# 디퍼드·클러스터드·SSAO 검증 씬 생성기 (docs/plan/deferred-clustered-ssao-record.html 의 "테스트 씬" 절).
#
# 실행: python Scripts/Tests/make_deferred_test_scenes.py   (SherlockTestScenes 폴더에서, 또는 어디서든 — 경로는 이 파일 기준)
# 결과 (Assets/Scenes):
#   ManyLights64.json / ManyLights.json (128) / ManyLights256.json
#       기존 Sponza.json 을 복사하고 방향광·IBL 을 어둡게 한 뒤 포인트·스팟 조명을 격자로 깔음. 조명 위치·색은 시드가
#       고정된 난수라 다시 돌려도 같은 파일이 나옴. 일부 조명만 그림자를 드리움 (아틀라스 16 타일보다 훨씬 많이 — 3a 확인).
#       Intel Sponza 가 아니라 이미 있는 Crytek Sponza(Models/Sponza) 를 씀 (Intel 판은 내려받지 않았음).
#   DeferredMaterials.json
#       디퍼드로 가는 재질과 포워드로 남는 재질(unlit·블렌딩·투과·코팅 노멀 맵)을 한 화면에 섞음 — 두 경로가 한 프레임에서
#       깊이·순서가 맞게 합쳐지는지, --deferred=0 과 차이가 작은지 확인.
#   SponzaSsao.json
#       Sponza.json 에 environment.ssao 를 켠 것 (기둥 밑동·아치 안쪽의 접촉 그림자 확인).
#   SsaoBalls.json
#       SSAO 를 보기 쉬운 씬: 구를 피라미드로 쌓은 더미 두 개(하나는 벽 모서리) + 벽에 붙은 구 줄 + 상자. IBL 강하게, 방향광 약하게.
#   LightOrder.json
#       MultiShadow.json 의 조명 순서만 바꿔 방향광을 맨 뒤에 둔 것. Renderer::BuildGpuLights 가 방향광을 앞으로 모으므로
#       화면이 MultiShadow.json 과 같아야 함 (다른 것은 에디터가 씬 번호로 붙이는 조명 이름표 "L번호" 뿐).
#   OrthoLights.json
#       DeferredMaterials.json 을 직교 카메라로 — 클러스터 깊이 분할의 직교 분기(균등 분할)와 직교 위치 복원 확인.
import copy
import json
import math
import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))
SCENES = os.path.normpath(os.path.join(HERE, '..', '..', 'Assets', 'Scenes'))


def load(name):
    with open(os.path.join(SCENES, name), encoding='utf-8-sig') as f:
        return json.load(f)


def save(name, scene):
    path = os.path.join(SCENES, name)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(scene, f, indent=4, sort_keys=True, ensure_ascii=False)
        f.write('\n')
    print('wrote', path, '-', len(scene.get('lights', [])), 'lights')


def hsv(h, s, v):
    i = int(h * 6.0) % 6
    f = h * 6.0 - math.floor(h * 6.0)
    p, q, t = v * (1 - s), v * (1 - f * s), v * (1 - (1 - f) * s)
    return [(v, t, p), (q, v, p), (p, v, t), (p, q, v), (t, p, v), (v, p, q)][i]


def light(kind, position, color, intensity, rng, direction=(0.0, -1.0, 0.0), inner=0.9238795, outer=0.8660254, shadows=False):
    # 씬 파일의 조명 키 (SceneSerializer). type: 0 방향, 1 포인트, 2 스팟. attenuation 은 range 안에서의 감쇠 곡선.
    return {
        'type': kind, 'position': list(position), 'direction': list(direction), 'color': list(color),
        'intensity': intensity, 'range': rng, 'attenuation': [1.0, 0.7, 1.8],
        'innerConeCos': inner, 'outerConeCos': outer, 'castShadows': shadows,
    }


# ---- Sponza 실내 범위 (Models/Sponza 의 glTF 정점 범위 × 0.008): x −15.4..14.4, y −1.0..11.4, z −9.5..8.8 ----
# 1층 회랑 높이 0.4~2.4 m, 2층 회랑 5.2~6.4 m. 가운데 마당 z −4..4, 양옆 회랑 z ±6.
def many_lights(count, seed):
    scene = load('Sponza.json')
    rnd = random.Random(seed)
    sun = scene['lights'][0]
    sun['intensity'] = 0.25            # 방향광은 남겨 둠 (CSM + 클러스터드의 "방향광 전부 돌기" 경로 확인)
    env = scene['environment']
    env['iblIntensity'] = 0.12         # 지역광이 눈에 띄도록 환경광을 낮춤
    lights = [sun]
    shadow_budget = {'point': 6, 'spot': 14}   # 6 × 6 + 14 = 50 타일 요청 > 아틀라스 16 타일 → 3a 우선순위가 의미 있음
    for i in range(count):
        x = rnd.uniform(-13.0, 13.0)
        upper = rnd.random() < 0.3
        y = rnd.uniform(5.2, 6.4) if upper else rnd.uniform(0.4, 2.4)
        z = rnd.choice([rnd.uniform(-4.0, 4.0), rnd.uniform(-6.8, -5.0), rnd.uniform(5.0, 6.8)])
        color = hsv(rnd.random(), rnd.uniform(0.35, 0.85), 1.0)
        rng = rnd.uniform(3.5, 7.0)         # 범위·강도를 키움 (2026-11-18 요청: 2~4.5 → 3.5~7, 1.5~3.5 → 3~7)
        intensity = rnd.uniform(3.0, 7.0)
        if i % 5 == 4:   # 다섯에 하나는 아래를 비추는 스팟
            angle = math.radians(rnd.uniform(25.0, 55.0))
            d = (rnd.uniform(-0.3, 0.3), -1.0, rnd.uniform(-0.3, 0.3))
            n = math.sqrt(sum(c * c for c in d))
            shadows = shadow_budget['spot'] > 0
            shadow_budget['spot'] -= shadows
            lights.append(light(2, (x, y + 1.5, z), color, intensity * 2.0, rng + 2.0, tuple(c / n for c in d),
                                math.cos(angle * 0.8), math.cos(angle), shadows))
        else:
            shadows = shadow_budget['point'] > 0 and rnd.random() < 0.2
            shadow_budget['point'] -= shadows
            lights.append(light(1, (x, y, z), color, intensity, rng, shadows=shadows))
    scene['lights'] = lights
    return scene


def deferred_materials():
    base = load('MultiShadow.json')
    scene = {k: copy.deepcopy(base[k]) for k in ('ambientColor', 'camera', 'clearColor', 'environment', 'version')}
    scene['environment']['ssao'] = {'enabled': True, 'radius': 0.5, 'bias': 0.025, 'intensity': 1.0, 'power': 1.5, 'sampleCount': 16, 'multiply': False}
    scene['meshes'] = [
        {'type': 'plane', 'a': 21, 'b': 21, 'color': [1, 1, 1, 1], 'params': [16.0, 16.0, 0.0, 0.0]},
        {'type': 'sphere', 'a': 48, 'b': 24, 'color': [1, 1, 1, 1], 'params': [0.7, 0.0, 0.0, 0.0]},
        {'type': 'cube', 'a': 0, 'b': 0, 'color': [1, 1, 1, 1], 'params': [1.0, 0.0, 0.0, 0.0]},
    ]
    floor = base['materials'][0]
    proto = copy.deepcopy(floor)
    proto.update({'normalTexture': '', 'uvScale': [1.0, 1.0], 'baseColor': [0.8, 0.8, 0.8, 1.0], 'roughness': 0.5, 'metallic': 0.0})

    def mat(name, **kw):
        m = copy.deepcopy(proto)
        m['name'] = name
        m.update(kw)
        return m

    bump = 'asset:Textures\\NormalBumpTest.png'
    materials = [
        copy.deepcopy(floor),                                                                   # 0 바닥 (노말 맵) — 디퍼드
        mat('D_Dielectric', baseColor=[0.8, 0.2, 0.15, 1.0], roughness=0.35),                  # 1 디퍼드
        mat('D_Metal', baseColor=[0.95, 0.75, 0.4, 1.0], metallic=1.0, roughness=0.25),        # 2 디퍼드
        mat('D_Clearcoat', baseColor=[0.1, 0.3, 0.8, 1.0], roughness=0.6, clearcoatFactor=1.0, clearcoatRoughness=0.05),   # 3 디퍼드 (코팅 노멀 = 기하 노멀)
        mat('D_Cutout', baseColor=[0.3, 0.8, 0.3, 1.0], alphaCutoff=0.5, normalTexture=bump),  # 4 디퍼드 (알파 컷아웃은 불투명)
        mat('F_ClearcoatNormal', baseColor=[0.1, 0.3, 0.8, 1.0], roughness=0.6, clearcoatFactor=1.0, clearcoatRoughness=0.05,
            clearcoatNormalTexture=bump),                                                      # 5 포워드 (코팅 노멀 맵)
        mat('F_Transmission', baseColor=[0.9, 0.95, 1.0, 1.0], roughness=0.05, transmissionFactor=1.0),   # 6 포워드 (투과)
        mat('F_Unlit', baseColor=[1.0, 0.85, 0.2, 1.0], unlit=True),                           # 7 포워드 (unlit)
        mat('F_Blend', baseColor=[0.2, 0.9, 0.9, 0.45], alphaBlend=True),                      # 8 포워드 (블렌딩)
    ]
    scene['materials'] = materials
    objects = [{'id': 1, 'name': 'Floor', 'mesh': 0, 'material': 0, 'position': [0.0, 0.0, 0.0], 'rotation': [0.0, 0.0, 0.0], 'scale': [1.0, 1.0, 1.0]}]
    names = ['D_Dielectric', 'D_Metal', 'D_Clearcoat', 'D_Cutout', 'F_ClearcoatNormal', 'F_Transmission', 'F_Unlit', 'F_Blend']
    for i, name in enumerate(names):
        row, col = divmod(i, 4)
        objects.append({'id': len(objects) + 1, 'name': name, 'mesh': 1 if i % 2 == 0 else 2, 'material': i + 1,
                        'position': [-3.3 + col * 2.2, 0.7, -1.0 + row * 2.4], 'rotation': [0.0, 0.5 * i, 0.0], 'scale': [1.0, 1.0, 1.0]})
    # 블렌딩 상자 뒤에 디퍼드 구를 하나 더 — 블렌딩이 G-buffer 로 그린 물체 위에 합쳐지는지
    objects.append({'id': len(objects) + 1, 'name': 'D_BehindBlend', 'mesh': 1, 'material': 2, 'position': [3.3, 0.7, 3.4],
                    'rotation': [0.0, 0.0, 0.0], 'scale': [0.8, 0.8, 0.8]})
    scene['objects'] = objects
    scene['camera'] = {'position': [0.0, 4.5, -9.5], 'yaw': 0.0, 'pitch': 0.38}
    scene['lights'] = [
        light(0, (0.0, 10.0, -10.0), (1.0, 0.97, 0.9), 1.2, 50.0, (0.35, -0.85, 0.4), shadows=True),
        light(1, (-2.0, 2.5, -2.5), (1.0, 0.5, 0.3), 4.0, 6.0, shadows=True),
        light(1, (2.5, 2.0, 0.5), (0.3, 0.6, 1.0), 4.0, 6.0),
        light(2, (0.0, 5.0, 1.5), (1.0, 1.0, 1.0), 6.0, 10.0, (0.0, -1.0, 0.0), 0.94, 0.87, True),
    ]
    return scene


def light_order():
    scene = load('MultiShadow.json')
    lights = scene['lights']
    directional = [l for l in lights if l['type'] == 0]
    scene['lights'] = [l for l in lights if l['type'] != 0] + directional
    return scene


def ortho_lights():
    scene = deferred_materials()
    # 에디터 카메라는 씬 파일의 camera 를 쓰고, 직교 여부는 카메라 오브젝트(CameraComponent)가 정함 → 직교 카메라 오브젝트를 넣음.
    # 편집기에서는 --select="Ortho Camera" 로 미리보기 창에, 재생(--play=1)·게임에서는 화면 전체가 직교. 회전은 라디안.
    scene['objects'].append({'id': len(scene['objects']) + 1, 'name': 'Ortho Camera', 'mesh': -1, 'material': -1,
                             'position': list(scene['camera']['position']), 'rotation': [0.38, 0.0, 0.0], 'scale': [1.0, 1.0, 1.0],
                             'components': [{'type': 'CameraComponent', 'enabled': True, 'fovY': 45.0, 'nearZ': 0.1, 'farZ': 100.0,
                                             'orthographic': True, 'orthoSize': 9.0, 'priority': 0, 'blendTime': 0.5}]})
    return scene


def sponza_ssao():
    scene = load('Sponza.json')
    scene['environment']['ssao'] = {'enabled': True, 'radius': 0.5, 'bias': 0.025, 'intensity': 1.2, 'power': 1.5, 'sampleCount': 16, 'multiply': False}
    return scene


def ssao_balls():
    # SSAO 전용 씬: 구를 피라미드로 쌓은 더미 두 개(하나는 벽 모서리에 붙임) + 바닥·벽 두 장. 구끼리, 구와 바닥·벽이 닿는
    # 틈이 많아 AO 가 또렷하게 보임. AO 는 환경광에만 곱해지므로 Cubemap.json 의 IBL 을 세게, 방향광은 약하게 둠.
    base = load('MultiShadow.json')
    ibl = load('Cubemap.json')['environment']
    scene = {k: copy.deepcopy(base[k]) for k in ('ambientColor', 'clearColor', 'environment', 'version')}
    env = scene['environment']
    for k in ('skybox', 'irradiance', 'specular', 'brdfLut'):
        env[k] = ibl[k]
    env['iblIntensity'] = 0.8
    env['ssao'] = {'enabled': True, 'radius': 0.8, 'bias': 0.025, 'intensity': 1.0, 'power': 1.5, 'sampleCount': 16, 'multiply': False}
    r = 0.5
    scene['meshes'] = [
        {'type': 'plane', 'a': 21, 'b': 21, 'color': [1, 1, 1, 1], 'params': [20.0, 20.0, 0.0, 0.0]},
        {'type': 'sphere', 'a': 48, 'b': 24, 'color': [1, 1, 1, 1], 'params': [r, 0.0, 0.0, 0.0]},
        {'type': 'cube', 'a': 0, 'b': 0, 'color': [1, 1, 1, 1], 'params': [1.0, 0.0, 0.0, 0.0]},
    ]
    proto = copy.deepcopy(base['materials'][0])
    proto.update({'normalTexture': '', 'uvScale': [1.0, 1.0], 'metallic': 0.0})

    def mat(name, color, rough):
        m = copy.deepcopy(proto)
        m.update({'name': name, 'baseColor': list(color) + [1.0], 'roughness': rough})
        return m

    scene['materials'] = [
        mat('A_Floor', (0.55, 0.55, 0.55), 0.9),     # 0
        mat('A_Wall', (0.6, 0.58, 0.55), 0.9),       # 1
        mat('A_BallWhite', (0.7, 0.7, 0.7), 0.6), # 2
        mat('A_BallWarm', (0.75, 0.45, 0.28), 0.6),  # 3
    ]
    objects = []

    def add(name, mesh, material, pos, scale=(1.0, 1.0, 1.0)):
        objects.append({'id': len(objects) + 1, 'name': name, 'mesh': mesh, 'material': material,
                        'position': [round(v, 5) for v in pos], 'rotation': [0.0, 0.0, 0.0], 'scale': list(scale)})

    add('Floor', 0, 0, (0.0, 0.0, 0.0))
    add('WallBack', 2, 1, (0.0, 2.0, 4.2), (14.0, 4.0, 0.4))      # 안쪽 면 z = 4.0
    add('WallLeft', 2, 1, (-5.2, 2.0, 0.0), (0.4, 4.0, 12.0))     # 안쪽 면 x = −5.0

    def pyramid(tag, cx, cz, layers, material):
        # 면심 입방 쌓기: n×n 층 위에 (n−1)×(n−1) 층을 구멍마다 하나씩. 층 높이 r·√2.
        for k in range(layers):
            n = layers - k
            y = r + k * r * math.sqrt(2.0)
            for i in range(n):
                for j in range(n):
                    x = cx + (i - (n - 1) / 2.0) * 2.0 * r
                    z = cz + (j - (n - 1) / 2.0) * 2.0 * r
                    add('%s_%d_%d_%d' % (tag, k, i, j), 1, material, (x, y, z))

    pyramid('Pile', 0.8, 0.6, 4, 2)                       # 가운데 더미 4·3·2·1 = 30 개
    pyramid('Corner', -5.0 + 3 * r, 4.0 - 3 * r, 3, 3)    # 모서리 더미 3·2·1 = 14 개, 벽 두 장에 닿음
    for i in range(5):                                     # 뒷벽에 붙은 줄
        add('Row_%d' % i, 1, 2, (2.0 + i * 2.0 * r, r, 4.0 - r))
    add('Box', 2, 1, (3.6, 0.5, 0.2))                      # 바닥에 놓인 상자 + 그 옆에 붙은 구
    add('BoxBall', 1, 3, (3.6 - 0.5 - r, r, 0.2))
    scene['objects'] = objects
    scene['camera'] = {'position': [0.2, 5.0, -8.0], 'yaw': -0.08, 'pitch': 0.42}
    scene['lights'] = [light(0, (0.0, 7.0, -12.0), (1.0, 0.97, 0.9), 0.35, 50.0, (0.45, -0.8, 0.4), shadows=True)]
    return scene


if __name__ == '__main__':
    save('ManyLights64.json', many_lights(64, 64))
    save('ManyLights.json', many_lights(128, 128))
    save('ManyLights256.json', many_lights(256, 256))
    save('DeferredMaterials.json', deferred_materials())
    save('SponzaSsao.json', sponza_ssao())
    save('SsaoBalls.json', ssao_balls())
    save('LightOrder.json', light_order())
    save('OrthoLights.json', ortho_lights())
