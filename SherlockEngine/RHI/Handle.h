#pragma once
#include <cstdint>

// GPU 리소스 핸들.
//
// 리소스는 포인터가 아니라 {index, generation} 값으로 넘김. 실제 객체는
// 백엔드(RHI/D3D11/, RHI/D3D12/)의 풀 안에 있고, 호출 코드는 D3D 타입을 전혀 모름.
// 목적은 7단계에서 RHI 인터페이스를 추출할 때 호출 코드가 바뀌지 않게 하는 것임
// (rendering-analysis D21의 (b) 방식).
//
// generation은 use-after-free를 잡음. 슬롯을 해제하면 세대가 1 올라가고,
// 이전 핸들은 index가 같아도 세대가 달라 풀에서 nullptr가 돌아옴.
// generation 0은 "빈 핸들"로 예약함. 풀의 세대는 1부터 시작함.
//
// Tag 타입은 BufferHandle과 TextureHandle을 서로 다른 타입으로 만들어
// 잘못 섞어 쓰면 컴파일 에러가 나게 하는 용도일 뿐이므로 정의는 필요 없음.
template <typename Tag>
struct Handle
{
	uint32_t index = UINT32_MAX;
	uint32_t generation = 0;

	bool IsValid() const { return index != UINT32_MAX && generation != 0; }

	bool operator==(const Handle& other) const
	{
		return index == other.index && generation == other.generation;
	}
	bool operator!=(const Handle& other) const { return !(*this == other); }
};

struct ShaderTag;
struct PipelineTag;
struct BufferTag;
struct TextureTag;
struct BindingLayoutTag;
struct ResourceSetTag;
struct SamplerTag;

using ShaderHandle = Handle<ShaderTag>;
using PipelineHandle = Handle<PipelineTag>;
using BufferHandle = Handle<BufferTag>;
using TextureHandle = Handle<TextureTag>;
using SamplerHandle = Handle<SamplerTag>;               // 5단계
using BindingLayoutHandle = Handle<BindingLayoutTag>;   // 4단계: D3D12 루트 시그니처에 해당
using ResourceSetHandle = Handle<ResourceSetTag>;       // 4단계: 레이아웃에 맞는 핸들 묶음
