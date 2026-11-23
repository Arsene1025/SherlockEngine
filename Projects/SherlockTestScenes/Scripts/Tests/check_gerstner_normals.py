# Gerstner 파도 수식 확인 (docs/plan/ocean-gerstner-plan.html 1단계 첫 항목).
#
# Scene/Ocean.cpp 의 Ocean::BuildWaves 와 같은 방법으로 파도를 만들고, Shaders/Ocean.hlsli 의 GerstnerSample 과 같은
# 해석적 노멀·탄젠트를 유한 차분(변위된 표면 P(x, z) 의 편미분)과 비교한다. 좌표는 엔진과 같은 Y 위.
# 평평할 때 cross(dP/dz, dP/dx) = (0, 1, 0) 이 되는 순서를 확인하고, 격자 점에서 각도 차가 0.1° 미만인지 본다.
#
#   python check_gerstner_normals.py            기본값 (3.3 표)
#   python check_gerstner_normals.py --steep 1  steepness 1 (최악의 경우: 마루에서 야코비안이 0 에 가까워짐)
import argparse
import math

G = 9.81
TWO_PI = 2.0 * math.pi


def lcg(seed):
    state = (seed * 747796405 + 2891336453) & 0xFFFFFFFF
    while True:
        state = (state * 1664525 + 1013904223) & 0xFFFFFFFF
        yield (state >> 8) / float(1 << 24)


def build_waves(count=6, wind_deg=30.0, spread_deg=40.0, lmin=4.0, lmax=32.0, amplitude=0.25, steepness=0.4, speed=1.0, seed=1):
    # Ocean::BuildWaves 와 같은 순서·같은 식
    n = max(1, min(8, count))
    lmin = max(lmin, 0.1)
    lmax = max(lmax, lmin)
    s_total = max(0.0, min(1.0, steepness))
    rnd = lcg(seed)
    waves = []
    for i in range(n):
        t = i / (n - 1) if n > 1 else 0.0
        L = lmax * (lmin / lmax) ** t
        angle = math.radians(wind_deg + spread_deg * (2.0 * next(rnd) - 1.0))
        k = TWO_PI / L
        waves.append(dict(dx=math.cos(angle), dz=math.sin(angle), k=k, a=amplitude * L / lmax, s=s_total / n,
                          w=math.sqrt(G * k) * speed, phase0=TWO_PI * next(rnd)))
    return waves


def phase(w, t):
    return math.fmod(w["w"] * t + w["phase0"], TWO_PI)


def position(waves, x, z, t):
    px, py, pz = x, 0.0, z
    for w in waves:
        th = w["k"] * (w["dx"] * x + w["dz"] * z) - phase(w, t)
        c, s = math.cos(th), math.sin(th)
        px += w["s"] / w["k"] * w["dx"] * c
        pz += w["s"] / w["k"] * w["dz"] * c
        py += w["a"] * s
    return (px, py, pz)


def analytic(waves, x, z, t):
    # Ocean.hlsli GerstnerSample 과 같은 식 (계획 2장)
    dpdx = [1.0, 0.0, 0.0]
    dpdz = [0.0, 0.0, 1.0]
    for w in waves:
        th = w["k"] * (w["dx"] * x + w["dz"] * z) - phase(w, t)
        c, s = math.cos(th), math.sin(th)
        ka = w["k"] * w["a"]
        dpdx[0] -= w["s"] * w["dx"] * w["dx"] * s
        dpdx[1] += ka * w["dx"] * c
        dpdx[2] -= w["s"] * w["dx"] * w["dz"] * s
        dpdz[0] -= w["s"] * w["dx"] * w["dz"] * s
        dpdz[1] += ka * w["dz"] * c
        dpdz[2] -= w["s"] * w["dz"] * w["dz"] * s
    return dpdx, dpdz


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def normalize(v):
    l = math.sqrt(sum(c * c for c in v))
    return [c / l for c in v]


def angle_deg(a, b):
    d = max(-1.0, min(1.0, sum(x * y for x, y in zip(a, b))))
    return math.degrees(math.acos(d))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--steep", type=float, default=0.4)
    args = ap.parse_args()

    # 평평할 때의 순서 확인
    flat = normalize(cross([0.0, 0.0, 1.0], [1.0, 0.0, 0.0]))
    print(f"평평한 면: cross(dP/dz, dP/dx) = {flat}  (기대 [0, 1, 0])")

    waves = build_waves(steepness=args.steep)
    for i, w in enumerate(waves):
        print(f"  파도 {i}: L={TWO_PI / w['k']:.2f} m  dir=({w['dx']:+.3f}, {w['dz']:+.3f})  A={w['a']:.4f}  kA={w['k'] * w['a']:.3f}  s={w['s']:.3f}  omega={w['w']:.3f}")

    h = 1e-4
    worst_n = worst_t = 0.0
    min_up = 1.0
    t = 10.0
    for ix in range(-20, 21):
        for iz in range(-20, 21):
            x, z = ix * 1.37, iz * 1.11
            dpdx, dpdz = analytic(waves, x, z, t)
            n_a = normalize(cross(dpdz, dpdx))
            t_a = normalize(dpdx)
            p0x = position(waves, x - h, z, t)
            p1x = position(waves, x + h, z, t)
            p0z = position(waves, x, z - h, t)
            p1z = position(waves, x, z + h, t)
            fdx = [(b - a) / (2 * h) for a, b in zip(p0x, p1x)]
            fdz = [(b - a) / (2 * h) for a, b in zip(p0z, p1z)]
            n_f = normalize(cross(fdz, fdx))
            worst_n = max(worst_n, angle_deg(n_a, n_f))
            worst_t = max(worst_t, angle_deg(t_a, normalize(fdx)))
            min_up = min(min_up, n_a[1])
    print(f"격자 41×41, t = {t}: 노멀 각도 차 최대 {worst_n:.5f}°, 탄젠트 {worst_t:.5f}°, 노멀 y 최솟값 {min_up:.3f}")
    ok = worst_n < 0.1 and worst_t < 0.1 and min_up > 0.0
    print("통과" if ok else "실패")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
