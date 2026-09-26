#include "pch.h"
#include "Core/Profiler.h"
#include "Core/Log.h"
#include "RHI/Device.h"
#include "RHI/CommandList.h"
#include "RHI/GraphicsConfig.h"

namespace
{
	struct OpenCpu
	{
		const char* name;
		LARGE_INTEGER start;
		size_t index;   // 결과 목록에서의 위치 (구간이 닫힐 때 시간을 채움)
	};

	struct GpuRange
	{
		const char* name;
		uint32_t beginSlot;
		uint32_t endSlot;
		uint32_t depth;
	};

	struct State
	{
		bool enabled = true;
		LARGE_INTEGER frequency = {};

		std::vector<Profiler::CpuSample> cpuCurrent;
		std::vector<Profiler::CpuSample> cpuLast;
		std::vector<OpenCpu> cpuOpen;

		// GPU: 프레임마다 기록한 구간 목록을 프레임 번호와 함께 몇 개 보관하다가, Device 가 그 프레임의 결과를 주면 짝지음.
		struct GpuFrame { uint64_t frameNumber; std::vector<GpuRange> ranges; };
		std::vector<GpuFrame> gpuPending;
		std::vector<GpuRange> gpuCurrent;
		uint32_t gpuNextSlot = 0;
		uint32_t gpuDepth = 0;
		std::vector<uint32_t> gpuOpen;   // gpuCurrent 인덱스 스택
		uint64_t frameCounter = 0;       // Device 의 프레임 번호와 같은 기준 (BeginFrame 마다 +1)

		std::vector<Profiler::GpuSample> gpuLast;
		uint64_t gpuLastFrame = 0;
		float cpuFrameMs = 0.0f;
		float gpuFrameMs = 0.0f;
	};

	State& S()
	{
		static State state;
		if (state.frequency.QuadPart == 0) QueryPerformanceFrequency(&state.frequency);
		return state;
	}
}

namespace Profiler
{
	void BeginFrame(RHI::Device* device)
	{
		State& s = S();
		// ---- CPU: 직전 프레임 확정 ----
		while (!s.cpuOpen.empty()) EndCpu();   // 짝이 안 맞는 Begin 은 여기서 닫음
		s.cpuLast.swap(s.cpuCurrent);
		s.cpuCurrent.clear();
		s.cpuFrameMs = 0.0f;
		for (const CpuSample& sample : s.cpuLast) if (sample.depth == 0) s.cpuFrameMs += sample.milliseconds;

		// ---- GPU: 직전 프레임의 구간 목록을 보관하고, 완료된 결과와 짝지음 ----
		if (!s.gpuCurrent.empty())
		{
			// gpuCurrent 는 직전 프레임(번호 frameCounter − 1)의 구간 목록임.
			if (s.frameCounter > 0) s.gpuPending.push_back(State::GpuFrame{ s.frameCounter - 1, s.gpuCurrent });
			if (s.gpuPending.size() > 8) s.gpuPending.erase(s.gpuPending.begin());
		}
		s.gpuCurrent.clear();
		s.gpuOpen.clear();
		s.gpuNextSlot = 0;
		s.gpuDepth = 0;

		uint64_t ticks[kMaxTimestamps] = {};
		uint64_t frequency = 0;
		uint64_t frameNumber = 0;
		if (device != nullptr && device->GetTimestampResults(ticks, kMaxTimestamps, frequency, frameNumber) && frequency != 0)
		{
			for (size_t i = 0; i < s.gpuPending.size(); ++i)
			{
				if (s.gpuPending[i].frameNumber != frameNumber) continue;
				if (frameNumber != s.gpuLastFrame)
				{
					s.gpuLast.clear();
					s.gpuFrameMs = 0.0f;
					for (const GpuRange& range : s.gpuPending[i].ranges)
					{
						const uint64_t begin = ticks[range.beginSlot];
						const uint64_t end = ticks[range.endSlot];
						const float ms = (end >= begin) ? static_cast<float>(static_cast<double>(end - begin) * 1000.0 / static_cast<double>(frequency)) : 0.0f;
						s.gpuLast.push_back(GpuSample{ range.name, ms, range.depth });
						if (range.depth == 0) s.gpuFrameMs += ms;
					}
					s.gpuLastFrame = frameNumber;
				}
				s.gpuPending.erase(s.gpuPending.begin(), s.gpuPending.begin() + i + 1);
				break;
			}
		}
		++s.frameCounter;
	}

	void BeginCpu(const char* name)
	{
		State& s = S();
		if (!s.enabled) return;
		OpenCpu open;
		open.name = name;
		QueryPerformanceCounter(&open.start);
		open.index = s.cpuCurrent.size();
		s.cpuCurrent.push_back(CpuSample{ name, 0.0f, static_cast<uint32_t>(s.cpuOpen.size()) });
		s.cpuOpen.push_back(open);
	}

	void EndCpu()
	{
		State& s = S();
		if (s.cpuOpen.empty()) return;
		const OpenCpu open = s.cpuOpen.back();
		s.cpuOpen.pop_back();
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		if (open.index < s.cpuCurrent.size())
		{
			s.cpuCurrent[open.index].milliseconds = static_cast<float>(static_cast<double>(now.QuadPart - open.start.QuadPart) * 1000.0 / static_cast<double>(s.frequency.QuadPart));
		}
	}

	void BeginGpu(RHI::CommandList& cmd, const char* name)
	{
		State& s = S();
		if (!s.enabled) return;
		if (s.gpuNextSlot + 2 > kMaxTimestamps)
		{
			static bool warned = false;
			if (!warned) { warned = true; Log::Warn("Profiler : GPU 타임스탬프 슬롯(%u) 이 부족해 '%s' 구간을 건너뜀.", kMaxTimestamps, name); }
			s.gpuOpen.push_back(UINT32_MAX);
			return;
		}
		GpuRange range;
		range.name = name;
		range.beginSlot = s.gpuNextSlot++;
		range.endSlot = UINT32_MAX;
		range.depth = s.gpuDepth++;
		cmd.WriteTimestamp(range.beginSlot);
		s.gpuOpen.push_back(static_cast<uint32_t>(s.gpuCurrent.size()));
		s.gpuCurrent.push_back(range);
	}

	void EndGpu(RHI::CommandList& cmd)
	{
		State& s = S();
		if (s.gpuOpen.empty()) return;
		const uint32_t index = s.gpuOpen.back();
		s.gpuOpen.pop_back();
		if (index == UINT32_MAX) return;   // 슬롯 부족으로 건너뛴 구간
		if (s.gpuDepth > 0) --s.gpuDepth;
		GpuRange& range = s.gpuCurrent[index];
		range.endSlot = s.gpuNextSlot++;
		cmd.WriteTimestamp(range.endSlot);
	}

	const std::vector<CpuSample>& GetCpuSamples() { return S().cpuLast; }
	const std::vector<GpuSample>& GetGpuSamples() { return S().gpuLast; }
	uint64_t GetGpuFrameNumber() { return S().gpuLastFrame; }
	float GetCpuFrameMs() { return S().cpuFrameMs; }
	float GetGpuFrameMs() { return S().gpuFrameMs; }
	bool IsEnabled() { return S().enabled; }
	void SetEnabled(bool enabled) { S().enabled = enabled; }
}
