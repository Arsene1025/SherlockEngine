#pragma once
#include <cstdint>
#include "RHI/Handle.h"
#include "RHI/ResourceDesc.h"   // ShaderStageMask

// 바인딩 레이아웃과 리소스 셋. D3D 헤더 없음.
//
// BindingLayout = "이 파이프라인은 어떤 슬롯을 쓰는가"의 선언.
//   D3D12의 루트 시그니처에 해당함. D3D11에서는 슬롯 번호표일 뿐임.
// ResourceSet   = 그 선언의 각 슬롯에 실제로 꽂는 핸들 목록.
//   D3D12의 디스크립터 테이블에 해당함. D3D11에서는 SetResourceSet이
//   슬롯마다 *SSetConstantBuffers / *SSetShaderResources 호출로 풀림.
//
// 갱신 빈도별로 셋을 나눔 (프레임 / 오브젝트 / 재질). D3D12에서 디스크립터
// 테이블을 빈도별로 분리하는 관례와 같고, 드로우 정렬(PSO → Material → Mesh)의
// 단위이기도 함.

enum class BindingType : uint8_t
{
	ConstantBuffer,   // b#  (CBV)
	ShaderResource,   // t#  (SRV) — 텍스처
	Sampler,          // s#
	BufferResource,   // t#  (SRV) — 구조체 버퍼 (HLSL StructuredBuffer<T>). ResourceBinding::buffer 에 BufferBind_ShaderResource 버퍼를 꽂음
};

struct BindingSlot
{
	BindingType type = BindingType::ConstantBuffer;
	uint8_t stageMask = 0;   // ShaderStageMask_* 조합
	uint8_t reg = 0;         // 레지스터 번호 (b0의 0)
};

constexpr uint32_t kMaxBindingSlots = 24;   // 레이아웃 하나의 슬롯 수

// 레지스터 상한. CBV 와 샘플러는 D3D11 한도(14, 16) 그대로, SRV 는 D3D11 한도(128)보다 좁게 잡음 —
// D3D11 커맨드 리스트가 스테이지마다 이 크기의 추적 배열을 가짐.
//
// 레지스터 번호는 "한 PSO 가 쓰는 레이아웃들 안에서만" 겹치지 않으면 됨. 서로 다른 PSO(패스)는 같은 번호를
// 다른 용도로 써도 됨 (예: G-buffer 를 읽는 라이팅 패스의 t0~t3 과 재질의 t0~t4). 그래서 패스가 늘어도 이
// 상한을 올릴 일은 드묾.
//
// D3D12 루트 시그니처는 64 DWORD 예산이 있음: CBV 슬롯 하나 = 루트 CBV 2 DWORD, 셋마다 SRV 테이블·샘플러
// 테이블이 1 DWORD 씩. 텍스처는 몇 장이든 테이블 하나라 예산을 쓰지 않고, 제약은 "한 PSO 의 CBV 수"에 걸림
// (예산 초과는 GetOrCreateRootLayout 이 에러로 알림).
constexpr uint8_t kMaxCbvRegister = 14;
constexpr uint8_t kMaxSrvRegister = 32;
constexpr uint8_t kMaxSamplerRegister = 16;

struct BindingLayoutDesc
{
	BindingSlot slots[kMaxBindingSlots];
	uint8_t slotCount = 0;
	const char* debugName = nullptr;

	BindingLayoutDesc& Add(BindingType type, uint8_t stageMask, uint8_t reg)
	{
		if (slotCount < kMaxBindingSlots)
		{
			slots[slotCount++] = BindingSlot{ type, stageMask, reg };
		}
		return *this;
	}
};

// 슬롯 하나에 꽂는 리소스. 슬롯 타입에 따라 buffer / texture / sampler 중 하나만 사용함
// (ConstantBuffer·BufferResource → buffer, ShaderResource → texture, Sampler → sampler).
struct ResourceBinding
{
	BufferHandle buffer;
	TextureHandle texture;
	SamplerHandle sampler;
};

struct ResourceSetDesc
{
	BindingLayoutHandle layout;
	ResourceBinding bindings[kMaxBindingSlots];   // layout.slots[i] ↔ bindings[i]
	const char* debugName = nullptr;
};
