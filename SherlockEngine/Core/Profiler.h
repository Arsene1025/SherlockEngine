#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace RHI { class Device; class CommandList; }

// 프로파일러 (10단계). CPU 구간과 GPU 타임스탬프 구간.
//
// CPU: QueryPerformanceCounter 로 잰 중첩 구간. ProfileScope RAII 로 감쌈. 프레임마다 목록을 만들고
//      BeginFrame 에서 "직전 프레임" 결과로 넘김(패널은 항상 완성된 프레임을 봄).
// GPU: RHI CommandList::WriteTimestamp 로 슬롯 두 개(시작·끝)를 기록하고, 결과는 Device 가 몇 프레임 뒤에
//      돌려줌 (D3D11: 쿼리 GetData, D3D12: 펜스 뒤 리드백). 그래서 GPU 결과는 CPU 결과보다 2~3 프레임 늦고,
//      어느 프레임의 결과인지는 frameNumber 로 알 수 있음. 슬롯 번호는 프레임 안에서 0 부터 순서대로 배정함.
//
// Log 처럼 namespace 자유 함수 + 파일 지역 상태. 스레드는 하나(렌더 스레드)라고 가정함.
namespace Profiler
{
	struct CpuSample
	{
		const char* name;
		float milliseconds;
		uint32_t depth;   // 중첩 깊이 (패널 들여쓰기)
	};

	struct GpuSample
	{
		const char* name;
		float milliseconds;
		uint32_t depth;
	};

	// 프레임 시작. 직전 프레임의 CPU 목록을 확정하고, Device 에서 완료된 GPU 결과를 읽음.
	void BeginFrame(RHI::Device* device);

	// CPU 구간. name 은 리터럴이어야 함 (포인터만 저장).
	void BeginCpu(const char* name);
	void EndCpu();

	// GPU 구간. 시작·끝 슬롯을 하나씩 소비함 (kMaxTimestamps / 2 구간까지).
	void BeginGpu(RHI::CommandList& cmd, const char* name);
	void EndGpu(RHI::CommandList& cmd);

	const std::vector<CpuSample>& GetCpuSamples();   // 직전 프레임
	const std::vector<GpuSample>& GetGpuSamples();   // 가장 최근에 완료된 프레임
	uint64_t GetGpuFrameNumber();                    // 그 프레임 번호
	float GetCpuFrameMs();                           // 직전 프레임의 최상위 구간 합
	float GetGpuFrameMs();                           // GPU 최상위 구간 합
	bool IsEnabled();
	void SetEnabled(bool enabled);
}

// RAII 구간.
class ProfileScope
{
public:
	explicit ProfileScope(const char* name) { Profiler::BeginCpu(name); }
	~ProfileScope() { Profiler::EndCpu(); }
	ProfileScope(const ProfileScope&) = delete;
	ProfileScope& operator=(const ProfileScope&) = delete;
};

class GpuProfileScope
{
public:
	GpuProfileScope(RHI::CommandList& cmd, const char* name) : m_cmd(cmd) { Profiler::BeginGpu(cmd, name); }
	~GpuProfileScope() { Profiler::EndGpu(m_cmd); }
	GpuProfileScope(const GpuProfileScope&) = delete;
	GpuProfileScope& operator=(const GpuProfileScope&) = delete;
private:
	RHI::CommandList& m_cmd;
};
