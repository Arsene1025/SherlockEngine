#include "pch.h"
#include "Graphics/Renderer.h"
#include "RHI/Device.h"
#include "RHI/CommandList.h"
#include "RHI/ResourceDesc.h"
#include "RHI/BindingTypes.h"
#include "Graphics/ShaderConstants.h"
#include "Scene/GameObject.h"
#include "Scene/ParticleEmitter.h"
#include "Scene/Camera.h"
#include "Core/Log.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;   // 이 파일 안에서만

//   BuildDrawList → CollectEmitter        이미터 컴포넌트를 찾아 월드 위치·방향과 함께 모음
//   UpdateParticleEmitters (업로드 구간)   버퍼·셋을 만들거나 크기를 맞추고, 원하는 생성 수를 정해 b8 (b9, b10) 을 올림
//   RenderScenePass (메인 뷰, 패스 1 뒤)  SimulateParticles: [Reset] → Sim (간접 디스패치 + 깊이 충돌) → Args (스레드 1개) → Emit
//   RenderScenePass (뷰마다, 패스 2 전)   SortParticles: 알파 이미터만 바이토닉 정렬
//   패스 2 (읽기 전용 깊이) → DrawParticles   DrawInstancedIndirect — 인스턴스 수 = GPU 가 센 생존 수
//
// CPU 는 생성률만 앎. 살아 있는 파티클 수와 위치는 GPU 의 생존·사망 목록과 개수 버퍼에만 있음 (읽어 오지 않음).
// 생존 목록은 두 개를 번갈아 씀: 시뮬레이션이 지난 프레임 목록(입력)을 읽어 이번 프레임 목록(출력)에 씀.

namespace
{
	constexpr uint32_t kParticleGroupSize = 64;   // 파티클 CS 의 [numthreads(64, 1, 1)] (Args 만 1)
	constexpr float kMaxParticleStep = 0.1f;      // 실제 시간 모드에서 한 프레임에 진행하는 최대 시간 (멈췄다 돌아올 때 튀지 않게)
	constexpr uint64_t kEmittersPerObject = 16;   // 키 = 오브젝트 ID × 16 + 순번
	constexpr float kCollisionThickness = 0.3f;   // 깊이 표면 뒤 이 거리(m) 안이면 충돌 (얇은 물체 뒤로 지나가는 파티클은 통과)
}

void Renderer::CollectEmitter(const GameObject& object, const XMMATRIX& world)
{
	uint64_t index = 0;
	for (const auto& behaviour : object.GetBehaviours())
	{
		const ParticleEmitter* emitter = dynamic_cast<const ParticleEmitter*>(behaviour.get());
		if (emitter == nullptr) continue;
		const uint64_t key = object.GetId() * kEmittersPerObject + (std::min)(index++, kEmittersPerObject - 1);
		if (!emitter->enabled) continue;   // 꺼진 이미터는 GPU 상태도 지움 (다시 켜면 빈 버퍼에서 시작)

		FrameEmitter frame;
		frame.key = key;
		frame.emitter = emitter;
		XMStoreFloat3(&frame.position, world.r[3]);
		// 로컬 +Y 를 월드로. 스케일은 길이만 바꾸므로 정규화함. 스케일 0 이면 위쪽.
		const XMVECTOR up = XMVector3TransformNormal(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), world);
		const float length = XMVectorGetX(XMVector3Length(up));
		XMStoreFloat3(&frame.direction, length > 1e-6f ? up / length : XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
		m_frameEmitters.push_back(frame);
	}
}

void Renderer::UpdateParticleEmitters(float totalTime, const Camera& camera)
{
	//m_activeEmitterKeys->이번 프레임에 실제로 시뮬레이션 할 Emitter들의 키 목록
	m_activeEmitterKeys.clear();

	//이번 프레임 시뮬레이션 시간.
	float dt = 0.0f;
	//고정 된 값이 매 프레임 dt가 됨. 0.015를 주면 실제 시간과 관계없이 매 프레임 0.015씩 진행
	//테스트할 때 매번 같은 화면이 나오게 하려는 의도.
	if (m_settings.particleFixedStep > 0.0f) dt = m_settings.particleFixedStep;
	//std::clamp(x, lo, hi)는 [lo, hi] 범위 안으로 잘라 줌.
	//totalTime - m_lastParticleTime <- 지난 프레임부터 흐른 시간
	//지난 프레임으로 부터 흐른 시간을 0~0.1사이로 클램프해서 dt로 사용
	else if (m_lastParticleTime >= 0.0f) dt = std::clamp(totalTime - m_lastParticleTime, 0.0f, kMaxParticleStep);
	//totalTime -> 엔진 시계의 경과 시간
	//deltaTime을 받아도 되지만 Render함수인자에 delta가 없음 
	//Render의 인자를 늘리기 보다는 직접 계산이 맞을 듯
	//다음 프레임의 dt계산을 위해 totalTime저장
	m_lastParticleTime = totalTime;

	//FrameEmitter는 입자를 나타내는 것이 아님
	//ParticleEmitter 컴포넌트 하나.
	//한 오브젝트에 이미터가 여러개 붙어있으면 FrameEmitter도 여러개 생김
	/*
			       ParticleEmitter                      FrameEmitter
	소유		  씬(Gameobject에 붙은 컴포넌트)       렌더러(m_frameEmitters)
	수명		  오브젝트가 존재하는 한 계속           한 프레임(메 프레임 비우고 채움)
	내용       사용자가 정하는 설정 값              렌더러가 이번 프레임의 쓸 값(키, ParticleEmitter포인터, 위치)
	*/
	for (const FrameEmitter& frame : m_frameEmitters)
	{
		const ParticleEmitter& e = *frame.emitter;

		const uint32_t capacity = static_cast<uint32_t>(std::clamp(e.maxParticles, 1, ParticleEmitter::kMaxParticlesLimit));
		//GpuEmitter는 이미터 하나가 GPU에 가진 리소스, 상태.
		//프레임을 넘어 유지됨
		GpuEmitter& gpu = m_gpuEmitters[frame.key];
		if (gpu.lastSeenFrame == m_frameNumber) continue;   // 같은 키가 두 번 (오브젝트 ID 중복) — 첫 것만
		gpu.lastSeenFrame = m_frameNumber;

		// 처음이거나 크기가 바뀌었으면 버퍼·셋을 새로 만듦.
		// 옛 것은 D3D12 에서 지연 해제됨 (이전 프레임이 읽는 중일 수 있음).
		if (gpu.capacity != capacity || !gpu.particles.IsValid())
		{
			//gpu파괴
			DestroyGpuEmitter(gpu, false);
			//상수 버퍼가 없으면 만들어줌
			if (!gpu.constants.IsValid())
			{
				gpu.constants = m_device->CreateBuffer(BufferDesc{ .size = sizeof(ParticleEmitterConstants), .usage = BufferUsage::Dynamic,
					.bindFlags = BufferBind_Constant, .debugName = "CB_ParticleEmitter" });
			}
			//실제 입자 데이터는 particles 버퍼에 존재함.
			//capacity만큼의 칸에 Particle(위치, 나이, 속도, 수명)하나씩 들어감
			gpu.particles = m_device->CreateBuffer(BufferDesc{ .size = capacity * kParticleStride, .usage = BufferUsage::Default,
				.bindFlags = BufferBind_ShaderResource | BufferBind_UnorderedAccess, .stride = kParticleStride, .debugName = "SB_Particles" });
			//particles에서 지금 비어 있는 칸의 번호를 모아놓음
			//Sim이 칸 번호를 넣고, Emit은 번호를 꺼냄
			//스택이 비면 더 태어나지 않음
			gpu.deadList = m_device->CreateBuffer(BufferDesc{ .size = capacity * 4, .usage = BufferUsage::Default,
				.bindFlags = BufferBind_UnorderedAccess, .stride = 4, .debugName = "SB_ParticleDead" });
			//살아있는 파티클 데이터
			//2개인 이유 -> GPU 쓰레드 읽기, 쓰기 동시접근 때문에.
			//한 프레임에서 읽기용, 쓰기용을 나눈 다음에 프레임이 바뀔 때 둘의 역할을 바꿈
			for (uint32_t i = 0; i < 2; ++i)
			{
				gpu.aliveLists[i] = m_device->CreateBuffer(BufferDesc{ .size = capacity * 4, .usage = BufferUsage::Default,
					.bindFlags = BufferBind_ShaderResource | BufferBind_UnorderedAccess, .stride = 4, .debugName = i == 0 ? "SB_ParticleAlive0" : "SB_ParticleAlive1" });
			}
			//셰이더끼리 개수를 주고받는 버퍼.
			//생존 수, 사망 목록 길이, 다음 Sim이 훑을 수, 이번 생성 수, 생성이 쓸 구간의 시작 위치가 들어감
			//Sim은 원자 연산으로 개수를 늘리고, Args가 그 값을 정리하고, Emit은 정리된 값을 읽어 자기 자리를 찾음
			//칸 번호는  kCounter*
			gpu.counters = m_device->CreateBuffer(BufferDesc{ .size = kParticleCounterCount * 4, .usage = BufferUsage::Default,
				.bindFlags = BufferBind_ShaderResource | BufferBind_UnorderedAccess, .stride = 4, .debugName = "SB_ParticleCounters" });   // SRV: 정렬이 생존 수를 읽음
			// 간접 인자 버퍼는 구조체 버퍼일 수 없음 (D3D11 의 DRAWINDIRECT_ARGS 는 BUFFER_STRUCTURED 와 함께 못 씀) → raw.
			//간접 호출에 넘기는 인자 버퍼 Args커널이 두 가지를 씀
			//{6, 생존 수, 0, 0}: DrawInstancedIndirect가 생존 수만큼 쿼드를 그림
			//{그룹 수, 1, 1}: 다음 Sim의 DispatchIndirect
			//생존 수는 GPU에서 정해져서 CPU 개입 없이 그리기와 디스패치 크기를 GPU가 정할 수 있게
			gpu.args = m_device->CreateBuffer(BufferDesc{ .size = kParticleArgsSize, .usage = BufferUsage::Default,
				.bindFlags = BufferBind_Raw | BufferBind_UnorderedAccess | BufferBind_IndirectArgs, .debugName = "RB_ParticleArgs" });
			//셰이더에 바인딩 할 버퍼 묶음(리소스 셋) 세 종류를 parity 0/1용으로 하나씩
			// parity는 어느 쪽이 입력인지 나타내는 0,1값
			//모두 6개 만들기
			for (uint32_t p = 0; p < 2; ++p)
			{
				//b8, u0~u5(args)까지
				//Reset·Args·Emit 커널
				//여기서 커널 <- 스레드들이 동시에 실행하는 컴퓨트 셰이더 함수 하나
				// [numthreads(64,1,1)]같은 속성이 붙은 함수가 커널
				//Dispatch(그룹 수)로 실행하면 그룹 수 × 64개 스레드가 같은 함수를 각자 다른 인덱스로 돌림
				gpu.simSets[p] = m_device->CreateResourceSet(ResourceSetDesc{
					.layout = m_layouts[Layout_ParticleSim],
					.bindings = { { .buffer = gpu.constants }, { .buffer = gpu.particles }, { .buffer = gpu.deadList },   // b8, u0, u1
						{ .buffer = gpu.aliveLists[p] }, { .buffer = gpu.aliveLists[1 - p] },                              // u2 입력, u3 출력
						{ .buffer = gpu.counters }, { .buffer = gpu.args } },                                              // u4, u5
					.debugName = p == 0 ? "Set_ParticleSim0" : "Set_ParticleSim1" });
				// b8, u0~u4 (args 없음)
				//Sim에서 사용
				gpu.stepSets[p] = m_device->CreateResourceSet(ResourceSetDesc{
					.layout = m_layouts[Layout_ParticleStep],
					.bindings = { { .buffer = gpu.constants }, { .buffer = gpu.particles }, { .buffer = gpu.deadList },
						{ .buffer = gpu.aliveLists[p] }, { .buffer = gpu.aliveLists[1 - p] }, { .buffer = gpu.counters } },   // b8, u0~u4
					.debugName = p == 0 ? "Set_ParticleStep0" : "Set_ParticleStep1" });
				//b8, t21(particles), t22(출력 생존 목록)
				//파티클 VS·PS 그리기에서 사용
				gpu.drawSets[p] = m_device->CreateResourceSet(ResourceSetDesc{
					.layout = m_layouts[Layout_ParticleDraw],
					.bindings = { { .buffer = gpu.constants }, { .buffer = gpu.particles }, { .buffer = gpu.aliveLists[1 - p] } },   // b8, t21, t22
					.debugName = p == 0 ? "Set_ParticleDraw0" : "Set_ParticleDraw1" });
			}
			//지금 만든 버퍼의 칸 수
			gpu.capacity = capacity;
			gpu.parity = 0;
			//생성 수의 소수 부분을 다음 프레임으로 넘기는 값
			//30개 × 0.016초 = 0.48개라면 이번 프레임엔 0개를 만들고 0.48을 넘김
			//낮은 생성률에서도 평균을 맞추기 위함.
			gpu.spawnCarry = 0.0f;
			//이번 프레임에 Reset으로 버퍼를 비워야 한다는 표시
			//모든 칸을 죽은 상태로 만들고 사망 리스트에 번호를 다 넣어.
			gpu.needsReset = true;
			bool ok = gpu.constants.IsValid() && gpu.particles.IsValid() && gpu.deadList.IsValid() && gpu.counters.IsValid() && gpu.args.IsValid();
			for (uint32_t i = 0; i < 2; ++i)
			{
				ok = ok && gpu.aliveLists[i].IsValid() && gpu.simSets[i].IsValid() && gpu.stepSets[i].IsValid() && gpu.drawSets[i].IsValid();
			}
			if (!ok)
			{
				static bool warned = false;
				if (!warned) Log::Error("파티클 이미터 GPU 리소스를 만들지 못함 (capacity %u). 이 이미터는 건너뜀.", capacity);
				warned = true;
				gpu.capacity = 0;   // 다음 프레임에 다시 시도
				continue;
			}
		}
		else if (!gpu.needsReset)
		{
			gpu.parity ^= 1u;   // 지난 프레임의 출력 목록이 이번 프레임의 입력
		}


		//알파 이미터면 정렬을 해야함
		//반투명 블렌드
		gpu.sorted = false;
		if (e.blendMode == 1 && m_settings.particleSort)
		{
			//정렬 길이 -> capacity 이상인 가장 작은 2의 거듭제곱 (최소 64)
			//바이토닉 정렬은 길이가 2의 거듭제곱이어야 해서
			const uint32_t sortCount = ParticleSortCount(capacity);
			//정렬 리소스는 처음 알파가 될 때 한 번만 만듦
			//가산으로 돌아가도 지우지 않고 남겨 둠
			if (!gpu.sortKeys[0].IsValid())
			{
				//뷰마다 따로 만듦 (0 메인, 1 카메라 미리보기)
				//카메라가 다르면 먼 것부터의 순서도 달라서
				//카메라가 많을때 카메라마다 정렬하면 비용이 커서 뷰는 2개로 제한
				for (uint32_t v = 0; v < kMaxParticleViews; ++v)
				{
					//m_particleSortViewCB -> 정렬할 때 기준이 되는 카메라의 view행렬
					if (!m_particleSortViewCB[v].IsValid())
					{
						m_particleSortViewCB[v] = m_device->CreateBuffer(BufferDesc{ .size = sizeof(ParticleSortViewConstants), .usage = BufferUsage::Dynamic,
							.bindFlags = BufferBind_Constant, .debugName = "CB_ParticleSortView" });
					}
					//정렬 키 -> 입자의 뷰 공간 z (float)
					//SortInit이 채우고 SortStep이 이 값을 비교함
					gpu.sortKeys[v] = m_device->CreateBuffer(BufferDesc{ .size = sortCount * 4, .usage = BufferUsage::Default,
						.bindFlags = BufferBind_UnorderedAccess, .stride = 4, .debugName = "SB_ParticleSortKeys" });
					//정렬된 파티클 번호 -> 키와 같이 자리를 바꿈
					//정렬이 끝나면 VS가 t22로 읽어서 먼 것부터 그림 (그래서 SRV도 붙음)
					gpu.sortIndices[v] = m_device->CreateBuffer(BufferDesc{ .size = sortCount * 4, .usage = BufferUsage::Default,
						.bindFlags = BufferBind_ShaderResource | BufferBind_UnorderedAccess, .stride = 4, .debugName = "SB_ParticleSortIndices" });
					//정렬 커널(SortInit, SortStep)에 바인딩 할 셋. [뷰][parity]
					//b8, b10(view행렬), t21(particles), t22(그 프레임 출력 생존 목록), t24(counters), u0(키), u1(번호)
					//parity마다 출력 생존 목록이 달라서 2개
					for (uint32_t p = 0; p < 2; ++p)
					{
						gpu.sortSets[v][p] = m_device->CreateResourceSet(ResourceSetDesc{
							.layout = m_layouts[Layout_ParticleSort],
							.bindings = { { .buffer = gpu.constants }, { .buffer = m_particleSortViewCB[v] }, { .buffer = gpu.particles },   // b8, b10, t21
								{ .buffer = gpu.aliveLists[1 - p] }, { .buffer = gpu.counters },                                             // t22 (그 프레임 출력), t24
								{ .buffer = gpu.sortKeys[v] }, { .buffer = gpu.sortIndices[v] } },                                          // u0, u1
							.debugName = "Set_ParticleSort" });
					}
					//정렬된 순서로 그릴 때 쓰는 그리기 셋
					//drawSets와 같은 레이아웃인데 t22에 생존 목록 대신 sortIndices를 꽂음
					gpu.drawSortedSets[v] = m_device->CreateResourceSet(ResourceSetDesc{
						.layout = m_layouts[Layout_ParticleDraw],
						.bindings = { { .buffer = gpu.constants }, { .buffer = gpu.particles }, { .buffer = gpu.sortIndices[v] } },   // b8, t21, t22 = 정렬된 번호
						.debugName = "Set_ParticleDrawSorted" });
				}
			}
			//바이토닉 정렬 단계마다 쓰는 (k, j) 상수 버퍼(b11)와 셋을 sortCount 길이까지 준비
			//모든 이미터가 같이 씀 (이미터마다 만들지 않음)
			bool ok = EnsureParticleSortSteps(sortCount);
			//정렬 리소스가 다 만들어졌는지 확인
			for (uint32_t v = 0; v < kMaxParticleViews; ++v)
			{
				ok = ok && gpu.sortKeys[v].IsValid() && gpu.sortIndices[v].IsValid() && gpu.drawSortedSets[v].IsValid()
					&& gpu.sortSets[v][0].IsValid() && gpu.sortSets[v][1].IsValid();
			}
			//다 있으면 이번 프레임에 정렬해서 그림
			gpu.sorted = ok;
			//실패하면 정렬 없이 생존 목록 순서로 그림
			if (!ok)
			{
				static bool warned = false;
				if (!warned) Log::Error("알파 파티클 정렬 리소스를 만들지 못함 (capacity %u). 정렬하지 않고 그림.", capacity);
				warned = true;
			}
		}

		// 원하는 생성 수: 초당 spawnRate × dt. 소수 부분은 다음 프레임으로 넘겨 낮은 생성률도 평균이 맞게. 새 버퍼를
		// 지우는 프레임에는 생성하지 않음 (1단계와 같은 순서 — 고정 스텝에서 1단계와 같은 파티클이 나오게).
		uint32_t spawnCount = 0;
		if (!gpu.needsReset)
		{
			//원하는 수 = 초당 생성 수 × dt + 지난 프레임에 남은 소수 부분
			const float wanted = (std::max)(e.spawnRate, 0.0f) * dt + gpu.spawnCarry;
			//정수 부분만 이번 프레임에 만듦
			const float whole = std::floor(wanted);
			//남은 소수 부분은 다음 프레임으로 넘김
			gpu.spawnCarry = wanted - whole;
			//한 프레임에 capacity보다 많이 만들 수는 없음
			spawnCount = static_cast<uint32_t>((std::min)(whole, static_cast<float>(capacity)));
		}
		//Emit 디스패치 크기로 씀 (0이면 Emit을 안 돌림)
		gpu.spawnCount = spawnCount;

		//b8 상수 버퍼에 올릴 값. 컴포넌트 설정값을 셰이더가 쓸 수 있는 범위로 잘라서 넣음
		ParticleEmitterConstants c = {};
		//FrameEmitter에서 계산한 이미터 월드 위치, 방출 방향
		c.position = frame.position;
		c.deltaTime = dt;
		c.direction = frame.direction;
		//퍼짐 각도는 코사인으로 넘김 (0도면 1 -> 한 방향, 180도면 -1 -> 모든 방향)
		c.spreadCos = std::cos(XMConvertToRadians(std::clamp(e.spreadAngle, 0.0f, 180.0f)));
		//색에 밝기를 미리 곱함. 1을 넘으면 HDR이라 블룸에 걸림
		const float intensity = (std::max)(e.intensity, 0.0f);
		c.color = XMFLOAT3((std::max)(e.color.x, 0.0f) * intensity, (std::max)(e.color.y, 0.0f) * intensity, (std::max)(e.color.z, 0.0f) * intensity);
		//수명이 0이면 나이/수명 계산이 깨지니까 최소 0.01초
		c.lifetime = (std::max)(e.lifetime, 0.01f);
		c.gravity = e.gravity;
		c.emitRadius = (std::max)(e.emitRadius, 0.0f);
		c.startSpeed = e.startSpeed;
		c.speedVariation = std::clamp(e.speedVariation, 0.0f, 1.0f);
		c.startSize = (std::max)(e.startSize, 0.0f);
		c.endSize = (std::max)(e.endSize, 0.0f);
		c.capacity = capacity;
		c.spawnCount = spawnCount;
		//난수 씨앗. 프레임마다 1씩 늘려서 매 프레임 다른 난수가 나오게
		c.seed = gpu.seed++;
		c.collide = e.collide ? 1u : 0u;
		c.softDistance = (std::max)(e.softness, 0.0f);
		c.bounce = std::clamp(e.bounce, 0.0f, 1.0f);
		c.opacity = std::clamp(e.opacity, 0.0f, 1.0f);
		c.blendMode = e.blendMode == 1 ? 1u : 0u;
		c.endTint = XMFLOAT3((std::max)(e.endTint.x, 0.0f), (std::max)(e.endTint.y, 0.0f), (std::max)(e.endTint.z, 0.0f));
		//GPU로 올림
		m_device->UpdateBuffer(gpu.constants, &c, sizeof(c));   // 프레임에 한 번 (D3D12 Dynamic CB 규칙)

		//여기까지 왔으면 이번 프레임에 시뮬레이션·그리기 할 이미터
		m_activeEmitterKeys.push_back(frame.key);
		//통계 (이미터 수, 전체 칸 수, 생성 수)
		++m_stats.particleEmitters;
		m_stats.particleCapacity += capacity;
		m_stats.particlesSpawned += spawnCount;   // 원하는 수. 사망 목록이 모자라 실제로 덜 태어났는지는 CPU 가 모름
	}

	bool anySorted = false;
	for (const uint64_t key : m_activeEmitterKeys) anySorted = anySorted || m_gpuEmitters[key].sorted;
	if (anySorted)
	{
		const Camera* views[kMaxParticleViews] = { &camera, (m_previewCamera != nullptr && m_previewView.IsValid() && m_previewView.color.IsValid()) ? m_previewCamera : nullptr };
		for (uint32_t v = 0; v < kMaxParticleViews; ++v)
		{
			if (views[v] == nullptr || !m_particleSortViewCB[v].IsValid()) continue;
			ParticleSortViewConstants sv = {};
			XMStoreFloat4x4(&sv.view, XMMatrixTranspose(views[v]->GetViewMatrix()));
			m_device->UpdateBuffer(m_particleSortViewCB[v], &sv, sizeof(sv));
		}
	}

	// 충돌 상수 (b9): 메인 카메라. 적분 패스가 충돌 셋을 늘 바인딩하므로 이미터가 있는 프레임에는 올림 (D3D12 Dynamic
	// 버퍼는 그 프레임에 쓴 것만 바인딩됨). 시뮬레이션은 메인 씬 패스 사이에서 메인 뷰의 깊이를 읽음.
	if (!m_activeEmitterKeys.empty() && m_particleCollisionCB.IsValid())
	{
		const XMMATRIX viewProj = camera.GetViewMatrix() * camera.GetProjectionMatrix();
		XMFLOAT4X4 proj;
		XMStoreFloat4x4(&proj, camera.GetProjectionMatrix());
		ParticleCollisionConstants cc = {};
		XMStoreFloat4x4(&cc.viewProj, XMMatrixTranspose(viewProj));
		XMStoreFloat4x4(&cc.invViewProj, XMMatrixTranspose(XMMatrixInverse(nullptr, viewProj)));
		cc.depthA = proj._33;
		cc.depthB = proj._43;
		cc.perspective = proj._34 != 0.0f ? 1.0f : 0.0f;
		cc.thickness = kCollisionThickness;
		cc.width = m_mainView.width;
		cc.height = m_mainView.height;
		m_device->UpdateBuffer(m_particleCollisionCB, &cc, sizeof(cc));
	}

	// 이번 프레임에 보이지 않은 이미터 (오브젝트·컴포넌트 삭제, 꺼짐) 의 GPU 리소스를 지움.
	for (auto it = m_gpuEmitters.begin(); it != m_gpuEmitters.end();)
	{
		if (it->second.lastSeenFrame == m_frameNumber) { ++it; continue; }
		DestroyGpuEmitter(it->second, true);
		it = m_gpuEmitters.erase(it);
	}
}

void Renderer::SimulateParticles(RHI::CommandList& cmd, const ViewTarget& view)
{
	const ComputePipelineHandle resetPipeline = GetComputePipeline(ComputePass_ParticleReset);
	const ComputePipelineHandle simPipeline = GetComputePipeline(ComputePass_ParticleSim);
	const ComputePipelineHandle argsPipeline = GetComputePipeline(ComputePass_ParticleArgs);
	const ComputePipelineHandle emitPipeline = GetComputePipeline(ComputePass_ParticleEmit);
	if (!resetPipeline.IsValid() || !simPipeline.IsValid() || !argsPipeline.IsValid() || !emitPipeline.IsValid() || !view.particleCollideSet.IsValid())
	{
		// 컴퓨트를 못 쓰면 그리지도 않음 (버퍼 내용이 정해져 있지 않음).
		static bool warned = false;
		if (!warned) Log::Error("파티클 컴퓨트 PSO 가 없어 파티클을 그리지 않음.");
		warned = true;
		m_activeEmitterKeys.clear();
		return;
	}

	for (const uint64_t key : m_activeEmitterKeys)
	{
		GpuEmitter& gpu = m_gpuEmitters[key];
		const BufferHandle aliveIn = gpu.aliveLists[gpu.parity];
		const BufferHandle aliveOut = gpu.aliveLists[1 - gpu.parity];
		const ResourceSetHandle simSet = gpu.simSets[gpu.parity];
		const BufferHandle written[] = { gpu.particles, gpu.deadList, aliveIn, aliveOut, gpu.counters, gpu.args };
		// 앞 디스패치가 쓴 것을 다음 디스패치가 읽거나 다시 씀 → 그 사이마다 UAV 배리어 (D3D12. D3D11 은 런타임이 알아서 함).
		auto uavBarrierAll = [&]() { for (const BufferHandle& b : written) cmd.UavBarrier(b); };
		auto run = [&](ComputePipelineHandle pipeline, ResourceSetHandle set)
		{
			cmd.SetComputePipeline(pipeline);
			cmd.SetComputeResourceSet(kSetParticleSim, set);
		};

		// 쓰기 전 UnorderedAccess. 버퍼 상태는 프레임 끝에 Common 으로 돌아가므로 매 프레임 필요. D3D11 에서는 이
		// 전이가 지난 프레임에 VS t21 / t22 에 남은 SRV 바인딩을 풀어 줌.
		for (const BufferHandle& b : written) cmd.Barrier(b, ResourceState::UnorderedAccess);

		if (gpu.needsReset)
		{
			run(resetPipeline, simSet);
			cmd.Dispatch((gpu.capacity + kParticleGroupSize - 1) / kParticleGroupSize, 1, 1);
			uavBarrierAll();
			gpu.needsReset = false;
		}

		// 지난 프레임 생존 목록 적분. 그룹 수는 GPU 가 앎 (인자 버퍼 바이트 16) → 간접 디스패치. 인자로 읽는 동안
		// 인자 버퍼는 IndirectArgument 상태여야 하고 UAV 로 바인딩할 수도 없음 → u5 가 없는 셋 (개수는 u4 에 셈).
		cmd.Barrier(gpu.args, ResourceState::IndirectArgument);
		run(simPipeline, gpu.stepSets[gpu.parity]);
		cmd.SetComputeResourceSet(kSetParticleCollide, view.particleCollideSet);   // b9 + t23 (깊이는 DepthRead 상태)
		cmd.DispatchIndirect(gpu.args, kParticleArgsDispatchOffset);
		cmd.Barrier(gpu.args, ResourceState::UnorderedAccess);
		uavBarrierAll();

		run(argsPipeline, simSet);   // 생성 수 확정 + 그리기 인자 + 다음 프레임 디스패치 인자
		cmd.Dispatch(1, 1, 1);
		uavBarrierAll();

		if (gpu.spawnCount > 0)
		{
			run(emitPipeline, simSet);
			cmd.Dispatch((gpu.spawnCount + kParticleGroupSize - 1) / kParticleGroupSize, 1, 1);
		}

		// 메인·미리보기 패스의 VS 가 t21 (파티클) · t22 (출력 생존 목록) 로 읽고, 그리기가 인자 버퍼를 읽음. 정렬은
		// 파티클·생존 목록·개수 (t24) 를 SRV 로 읽음.
		cmd.Barrier(gpu.particles, ResourceState::ShaderResource);
		cmd.Barrier(aliveOut, ResourceState::ShaderResource);
		cmd.Barrier(gpu.counters, ResourceState::ShaderResource);
		cmd.Barrier(gpu.args, ResourceState::IndirectArgument);
	}
}

bool Renderer::EnsureParticleSortSteps(uint32_t count)
{
	while (m_particleSortStepCount < count)
	{
		// 길이를 두 배로: k = 2N 의 단계 (j = N … 1) 를 뒤에 붙임. 짧은 길이의 단계 목록은 긴 목록의 앞부분 그대로.
		const uint32_t k = m_particleSortStepCount == 0 ? 2 : m_particleSortStepCount * 2;
		for (uint32_t j = k / 2; j > 0; j /= 2)
		{
			ParticleSortStepConstants sc = {};
			sc.k = k;
			sc.j = j;
			ParticleSortStep step;
			step.constants = m_device->CreateBuffer(BufferDesc{ .size = sizeof(sc), .usage = BufferUsage::Default,
				.bindFlags = BufferBind_Constant, .debugName = "CB_ParticleSortStep" }, &sc);
			step.set = m_device->CreateResourceSet(ResourceSetDesc{
				.layout = m_layouts[Layout_ParticleSortStep], .bindings = { { .buffer = step.constants } }, .debugName = "Set_ParticleSortStep" });
			if (!step.constants.IsValid() || !step.set.IsValid())
			{
				m_device->DestroyResourceSet(step.set);
				m_device->DestroyBuffer(step.constants);
				return false;
			}
			m_particleSortSteps.push_back(step);
		}
		m_particleSortStepCount = k;
	}
	return true;
}

void Renderer::SortParticles(RHI::CommandList& cmd, uint32_t viewIndex)
{
	if (viewIndex >= kMaxParticleViews) return;
	const ComputePipelineHandle initPipeline = GetComputePipeline(ComputePass_ParticleSortInit);
	const ComputePipelineHandle stepPipeline = GetComputePipeline(ComputePass_ParticleSortStep);
	for (const uint64_t key : m_activeEmitterKeys)
	{
		GpuEmitter& gpu = m_gpuEmitters[key];
		if (!gpu.sorted) continue;
		if (!initPipeline.IsValid() || !stepPipeline.IsValid() || m_particleSortSteps.empty())
		{
			gpu.sorted = false;   // 정렬 없이 생존 목록 순서로 그림
			continue;
		}
		const uint32_t count = ParticleSortCount(gpu.capacity);
		const uint32_t groups = count / kParticleGroupSize;
		const BufferHandle keys = gpu.sortKeys[viewIndex];
		const BufferHandle indices = gpu.sortIndices[viewIndex];
		const ResourceSetHandle sortSet = gpu.sortSets[viewIndex][gpu.parity];

		cmd.Barrier(keys, ResourceState::UnorderedAccess);
		cmd.Barrier(indices, ResourceState::UnorderedAccess);   // D3D11: 지난 뷰·프레임에 VS t22 에 남은 SRV 를 풂
		cmd.SetComputePipeline(initPipeline);
		cmd.SetComputeResourceSet(kSetParticleSim, sortSet);
		cmd.SetComputeResourceSet(kSetParticleSortStep, m_particleSortSteps[0].set);   // 준비 단계는 b11 을 안 읽지만 레이아웃이 같아 꽂아 둠
		cmd.Dispatch(groups, 1, 1);

		// N = 2^m 이면 단계 m(m+1)/2 개. 단계마다 앞 단계 결과를 읽으므로 사이마다 UAV 배리어.
		uint32_t m = 0;
		while ((1u << m) < count) ++m;
		const uint32_t steps = m * (m + 1) / 2;
		cmd.SetComputePipeline(stepPipeline);
		cmd.SetComputeResourceSet(kSetParticleSim, sortSet);
		for (uint32_t s = 0; s < steps; ++s)
		{
			cmd.UavBarrier(keys);
			cmd.UavBarrier(indices);
			cmd.SetComputeResourceSet(kSetParticleSortStep, m_particleSortSteps[s].set);
			cmd.Dispatch(groups, 1, 1);
		}
		cmd.Barrier(indices, ResourceState::ShaderResource);   // 파티클 패스의 VS 가 t22 로 읽음
	}
}

void Renderer::DrawParticles(RHI::CommandList& cmd, const ViewTarget& view, uint32_t viewIndex)
{
	const PipelineHandle pipeline = GetParticlePipeline();
	if (!pipeline.IsValid()) return;
	cmd.SetPipelineState(pipeline);
	++m_stats.pipelineSwitches;
	cmd.SetResourceSet(kSetFrame, m_frameSet);   // b0 = 지금 뷰의 카메라 (미리보기 패스는 b0 를 다시 올렸음)
	cmd.SetResourceSet(kSetParticleDepth, view.particleDepthSet);   // t23 = 이 뷰의 깊이 (소프트 파티클)
	// 알파 이미터 먼저 (이미터 안은 정렬됨, 이미터끼리는 정렬하지 않음), 가산 이미터는 그 위에 — 가산은 순서와 상관없음.
	for (uint32_t pass = 0; pass < 2; ++pass)
	{
		for (const uint64_t key : m_activeEmitterKeys)
		{
			const GpuEmitter& gpu = m_gpuEmitters[key];
			if (gpu.sorted != (pass == 0)) continue;
			cmd.SetResourceSet(kSetParticle, gpu.sorted ? gpu.drawSortedSets[(std::min)(viewIndex, kMaxParticleViews - 1)] : gpu.drawSets[gpu.parity]);
			// 정점 6 개 (쿼드) × 인스턴스 = 생존 수. 정점 버퍼 없음 (VS 가 SV_InstanceID 로 생존 목록·정렬 목록을 읽음).
			cmd.DrawInstancedIndirect(gpu.args, kParticleArgsDrawOffset);
			++m_stats.draws;
			++m_stats.resourceSetSwitches;
		}
	}
}

PipelineHandle Renderer::GetParticlePipeline()
{
	if (m_particlePipeline.IsValid()) return m_particlePipeline;
	m_particlePipeline = m_device->CreatePipeline(m_passDesc[Pass_Particle]);   // 타깃 포맷까지 BuildPassDescs 가 채움
	return m_particlePipeline;
}

void Renderer::DestroyGpuEmitter(GpuEmitter& gpu, bool includeConstants)
{
	for (uint32_t i = 0; i < 2; ++i)
	{
		m_device->DestroyResourceSet(gpu.simSets[i]);
		m_device->DestroyResourceSet(gpu.stepSets[i]);
		gpu.stepSets[i] = {};
		m_device->DestroyResourceSet(gpu.drawSets[i]);
		m_device->DestroyBuffer(gpu.aliveLists[i]);
		gpu.simSets[i] = {};
		gpu.drawSets[i] = {};
		gpu.aliveLists[i] = {};
	}
	for (uint32_t v = 0; v < kMaxParticleViews; ++v)
	{
		for (uint32_t p = 0; p < 2; ++p) { m_device->DestroyResourceSet(gpu.sortSets[v][p]); gpu.sortSets[v][p] = {}; }
		m_device->DestroyResourceSet(gpu.drawSortedSets[v]);
		m_device->DestroyBuffer(gpu.sortKeys[v]);
		m_device->DestroyBuffer(gpu.sortIndices[v]);
		gpu.drawSortedSets[v] = {};
		gpu.sortKeys[v] = {};
		gpu.sortIndices[v] = {};
	}
	gpu.sorted = false;
	m_device->DestroyBuffer(gpu.particles);
	m_device->DestroyBuffer(gpu.deadList);
	m_device->DestroyBuffer(gpu.counters);
	m_device->DestroyBuffer(gpu.args);
	gpu.particles = {};
	gpu.deadList = {};
	gpu.counters = {};
	gpu.args = {};
	if (includeConstants)
	{
		m_device->DestroyBuffer(gpu.constants);
		gpu.constants = {};
	}
}

void Renderer::DestroyGpuEmitters()
{
	for (auto& [key, gpu] : m_gpuEmitters) DestroyGpuEmitter(gpu, true);
	m_gpuEmitters.clear();
	m_device->DestroyBuffer(m_particleCollisionCB);   // 종료 때만 부름. 뷰의 충돌 셋은 뷰를 지울 때 같이 지워짐
	m_particleCollisionCB = {};
	for (BufferHandle& cb : m_particleSortViewCB) { m_device->DestroyBuffer(cb); cb = {}; }
	for (ParticleSortStep& step : m_particleSortSteps)
	{
		m_device->DestroyResourceSet(step.set);
		m_device->DestroyBuffer(step.constants);
	}
	m_particleSortSteps.clear();
	m_particleSortStepCount = 0;
	m_activeEmitterKeys.clear();
	m_frameEmitters.clear();
}
