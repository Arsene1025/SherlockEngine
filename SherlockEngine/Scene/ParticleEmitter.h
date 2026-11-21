#pragma once
#include "Core/EngineApi.h"   // SHERLOCK_API (엔진 DLL 내보내기)
#include "Scene/Behaviour.h"

// GPU 파티클 이미터 (VFX). 이 컴포넌트는 값만 들고 있고, 시뮬레이션·그리기는 렌더러가 함
// (Renderer::UpdateParticleEmitters → SimulateParticles → DrawParticles, docs/plan/vfx-gpu-particles-record.html).
//
// 위치·방향은 소유 오브젝트의 월드 행렬에서 읽음: 원점 = 위치, 로컬 +Y = 방출 방향. 파티클은 월드 공간에서
// 움직이므로 오브젝트를 옮겨도 이미 나간 파티클은 따라오지 않음.
//
// 편집 모드에서도 시뮬레이션함 (값을 바꾸면 씬 뷰에서 바로 보임). Update 를 쓰지 않으므로 Play 와 상관없음.
// 색은 HDR: color × intensity 가 1 을 넘으면 블룸에 걸림. blendMode 0 = 가산 (순서 무관, 정렬 안 함), 1 = 알파
// (프리멀티플라이드, 뷰마다 먼 것부터 GPU 정렬). 깊이: softness > 0 이면 불투명 표면 근처에서 부드럽게 사라지고,
// collide 면 메인 뷰의 이번 프레임 깊이 버퍼에 튕김 (화면 공간 충돌 — 화면 밖·가려진 곳에서는 충돌하지 않음).
class SHERLOCK_API ParticleEmitter : public Behaviour
{
public:
	const char* GetTypeName() const override;
	void Reflect(PropertyVisitor& visitor) override;

	static constexpr int kMaxParticlesLimit = 1 << 18;   // 262,144. 렌더러가 maxParticles 를 [1, 이 값] 으로 자름

	int maxParticles = 4096;                                            // 버퍼 크기 (고정). 다 차면 빈 슬롯이 생길 때까지 생성이 멈춤
	float spawnRate = 500.0f;                                           // 초당 생성 수
	float lifetime = 2.0f;                                              // 초
	float startSpeed = 3.0f;                                            // 방출 속도 (m/s)
	float speedVariation = 0.3f;                                        // 0..1. 속도 = startSpeed × (1 − speedVariation × 난수)
	float spreadAngle = 20.0f;                                          // 방출 원뿔의 반각 (도). 0 = 한 방향, 180 = 모든 방향
	float emitRadius = 0.05f;                                           // 생성 위치가 퍼지는 구의 반지름 (m)
	DirectX::XMFLOAT3 gravity = DirectX::XMFLOAT3(0.0f, -4.0f, 0.0f);   // 가속도 (m/s², 월드)
	float startSize = 0.08f;                                            // 쿼드 한 변 (m). 수명 동안 endSize 로 보간
	float endSize = 0.02f;
	DirectX::XMFLOAT3 color = DirectX::XMFLOAT3(1.0f, 0.55f, 0.2f);    // 선형 RGB
	float intensity = 4.0f;                                            // 색 배율 (HDR)
	DirectX::XMFLOAT3 endTint = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);   // 수명 끝의 색 배율 (처음 1 → 끝 endTint 로 보간). 흰색 = 변화 없음
	int blendMode = 0;                                                 // 0 가산, 1 알파 (정렬)
	float opacity = 1.0f;                                              // 알파 모드의 불투명도 배율 (0..1)
	float softness = 0.0f;                                             // 소프트 파티클 거리 (m). 0 = 끔
	bool collide = false;                                              // 깊이 버퍼 충돌
	float bounce = 0.5f;                                               // 충돌 시 법선 방향 속도의 반발 계수 (0 = 붙음, 1 = 완전 탄성)
	// 바람·공기 저항: 속도가 매 초 drag 의 비율로 wind 쪽으로 끌려감 (v ← wind + (v − wind) × e^(−drag·dt)).
	// drag 0 이면 wind 를 넣어도 아무 일도 없음. drag 는 중력·방출 속도도 같이 줄여, 끝 속도는 대략 wind + gravity / drag.
	DirectX::XMFLOAT3 wind = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);   // 끌려가는 목표 속도 (m/s, 월드)
	float drag = 0.0f;                                              // 끌려가는 세기 (1/초). 0 = 끔, 클수록 빨리 wind 를 따라감
};
