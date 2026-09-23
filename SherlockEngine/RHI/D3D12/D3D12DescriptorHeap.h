#pragma once
#include "RHI/D3D12/D3D12Common.h"
#include <cstdint>
#include <string>
#include <vector>

// 디스크립터 힙 하나 + 연속 범위 할당기.
//
// D3D11 은 뷰 객체(ID3D11*View)를 만들면 끝이지만, D3D12 의 뷰는 "디스크립터 힙 안의 슬롯"이다.
// CPU 전용 힙(RTV·DSV·스테이징 SRV·스테이징 샘플러)에 뷰를 만들어 두고, 셰이더가 읽을 것은
// 셰이더 가시 힙(CBV/SRV/UAV 하나, 샘플러 하나)에 CopyDescriptorsSimple 로 복사한다.
// ResourceSet 은 셰이더 가시 힙에서 연속 범위(디스크립터 테이블)를 영구 할당받는다.
//
// 할당기는 bump + free list(first-fit) 이다. 셋과 텍스처 수가 수백 개 수준이라 이걸로 충분하다.
class D3D12DescriptorHeap
{
public:
	bool Init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity, bool shaderVisible, const wchar_t* name)
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = type;
		desc.NumDescriptors = capacity;
		desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		desc.NodeMask = 0;
		if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(m_heap.ReleaseAndGetAddressOf())))) return false;
		m_heap->SetName(name);
		m_type = type;
		m_capacity = capacity;
		m_shaderVisible = shaderVisible;
		m_increment = device->GetDescriptorHandleIncrementSize(type);
		m_cpuStart = m_heap->GetCPUDescriptorHandleForHeapStart();
		m_gpuStart = shaderVisible ? m_heap->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE{};
		m_next = 0;
		m_free.clear();
		return true;
	}

	void Release()
	{
		m_heap.Reset();
		m_free.clear();
		m_next = 0;
	}

	static constexpr uint32_t kInvalid = UINT32_MAX;

	// 연속 count 개. 실패하면 kInvalid.
	uint32_t Allocate(uint32_t count = 1)
	{
		for (size_t i = 0; i < m_free.size(); ++i)
		{
			if (m_free[i].count >= count)
			{
				const uint32_t start = m_free[i].start;
				m_free[i].start += count;
				m_free[i].count -= count;
				if (m_free[i].count == 0) m_free.erase(m_free.begin() + i);
				return start;
			}
		}
		if (m_next + count > m_capacity) return kInvalid;
		const uint32_t start = m_next;
		m_next += count;
		return start;
	}

	void Free(uint32_t start, uint32_t count = 1)
	{
		if (start == kInvalid || count == 0) return;
		m_free.push_back(Range{ start, count });
	}

	D3D12_CPU_DESCRIPTOR_HANDLE Cpu(uint32_t index) const
	{
		D3D12_CPU_DESCRIPTOR_HANDLE h = m_cpuStart;
		h.ptr += static_cast<SIZE_T>(index) * m_increment;
		return h;
	}

	D3D12_GPU_DESCRIPTOR_HANDLE Gpu(uint32_t index) const
	{
		D3D12_GPU_DESCRIPTOR_HANDLE h = m_gpuStart;
		h.ptr += static_cast<UINT64>(index) * m_increment;
		return h;
	}

	ID3D12DescriptorHeap* Get() const { return m_heap.Get(); }
	D3D12_DESCRIPTOR_HEAP_TYPE GetType() const { return m_type; }
	uint32_t GetIncrement() const { return m_increment; }
	uint32_t GetUsed() const { return m_next; }
	uint32_t GetCapacity() const { return m_capacity; }

private:
	struct Range { uint32_t start; uint32_t count; };

	ComPtr<ID3D12DescriptorHeap> m_heap;
	D3D12_DESCRIPTOR_HEAP_TYPE m_type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	uint32_t m_capacity = 0;
	uint32_t m_increment = 0;
	uint32_t m_next = 0;
	bool m_shaderVisible = false;
	D3D12_CPU_DESCRIPTOR_HANDLE m_cpuStart = {};
	D3D12_GPU_DESCRIPTOR_HANDLE m_gpuStart = {};
	std::vector<Range> m_free;
};
