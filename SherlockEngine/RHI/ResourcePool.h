#pragma once
#include <cstdint>
#include <utility>
#include <vector>

// 세대(generation) 카운터가 있는 슬롯 풀.
//
// 핸들 = {index, generation}. 슬롯을 해제하면 세대가 1 올라가므로, 해제 전에
// 발급된 핸들은 index가 같아도 Get()에서 nullptr가 돌아옴. 댕글링 포인터가
// "가끔 다른 객체를 가리키는" 대신 "항상 널"이 되는 것이 요점임.
// (Bitsquid의 ID lookup table, sokol_gfx의 pool과 같은 구조.)
//
// 빈 슬롯은 free list로 재사용함. generation 0은 빈 핸들로 예약되어 있으므로
// 세대는 1에서 시작하고, 값이 넘쳐 0이 되면 0을 건너뜀.
template <typename T, typename HandleT>
class ResourcePool
{
public:
	HandleT Add(T&& item)
	{
		uint32_t index;
		if (!m_free.empty())
		{
			index = m_free.back();
			m_free.pop_back();
		}
		else
		{
			index = static_cast<uint32_t>(m_slots.size());
			m_slots.emplace_back();
		}

		Slot& slot = m_slots[index];
		slot.item = std::move(item);
		slot.alive = true;
		return HandleT{ index, slot.generation };
	}

	T* Get(HandleT handle)
	{
		if (handle.index >= m_slots.size()) return nullptr;
		Slot& slot = m_slots[handle.index];
		if (!slot.alive || slot.generation != handle.generation) return nullptr;
		return &slot.item;
	}

	const T* Get(HandleT handle) const
	{
		return const_cast<ResourcePool*>(this)->Get(handle);
	}

	void Remove(HandleT handle)
	{
		if (Get(handle) == nullptr) return;   // 이미 해제됐거나 세대가 다름
		Slot& slot = m_slots[handle.index];
		slot.item = T{};     // 스마트 포인터 멤버가 여기서 해제됨
		slot.alive = false;
		++slot.generation;
		if (slot.generation == 0) slot.generation = 1;
		m_free.push_back(handle.index);
	}

	void Clear()
	{
		m_slots.clear();
		m_free.clear();
	}

	size_t AliveCount() const
	{
		return m_slots.size() - m_free.size();
	}

private:
	struct Slot
	{
		T item{};
		uint32_t generation = 1;
		bool alive = false;
	};

	std::vector<Slot> m_slots;
	std::vector<uint32_t> m_free;
};
