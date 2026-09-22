#pragma once
#include <cstdint>
#include <unordered_map>
#include "Graphics/Handle.h"
#include "Graphics/PipelineTypes.h"

class Device;
class Scene;
class Camera;
class Mesh;

// ImGui에서 바꾸는 렌더 설정. 매 프레임 이 값으로 PipelineStateDesc를 만들고
// 캐시에서 PSO를 얻는다.
struct RenderSettings
{
	bool wireframe = false;
	bool cullBack = true;
};

// 씬을 그린다. 씬 데이터는 갖지 않고 입력으로 받는다 (rendering-analysis D3).
//
// 소유하는 것: 셰이더 핸들, 상수버퍼 핸들 3개, PSO Desc의 고정 부분, 그리고
// Mesh → GPU 버퍼 캐시. 전부 핸들이므로 이 헤더와 .cpp에는 D3D 타입이 없다.
class Renderer
{
public:
	Renderer();
	~Renderer();

	bool Initialize(Device* device);
	void Shutdown();

	void Render(const Scene& scene, const Camera& camera, float totalTime);

	RenderSettings& GetSettings() { return m_settings; }
	size_t GetPipelineCount() const;
	size_t GetGpuMeshCount() const { return m_gpuMeshes.size(); }

	// 메시가 파괴되거나 내용이 바뀌면 캐시 항목을 버린다.
	void InvalidateMesh(const Mesh* mesh);

private:
	struct GpuMesh
	{
		BufferHandle vertexBuffer;
		BufferHandle indexBuffer;
		uint32_t indexCount = 0;
	};

	const GpuMesh* GetOrCreateGpuMesh(const Mesh& mesh);
	PipelineHandle GetPipelineForSettings();

private:
	Device* m_device = nullptr;

	ShaderHandle m_vs;
	ShaderHandle m_ps;
	BufferHandle m_perFrameCB;
	BufferHandle m_perObjectCB;
	BufferHandle m_lightCB;

	PipelineStateDesc m_baseDesc;   // 셰이더·정점 레이아웃 등 설정과 무관한 부분
	RenderSettings m_settings;

	// Scene이 메시를 unique_ptr로 소유하므로 포인터가 안정적이다. 그래서 키로 쓴다.
	std::unordered_map<const Mesh*, GpuMesh> m_gpuMeshes;
};
