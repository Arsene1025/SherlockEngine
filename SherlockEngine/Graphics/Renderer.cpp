#include "pch.h"
#include "Graphics/Renderer.h"
#include "Graphics/D3D11/Device.h"
#include "Graphics/D3D11/ShaderCompiler.h"
#include "Graphics/ResourceDesc.h"
#include "Graphics/ShaderConstants.h"
#include "Graphics/VertexTypes.h"
#include "Graphics/Mesh.h"
#include "Scene/Scene.h"
#include "Scene/Camera.h"
#include "Core/Paths.h"
#include "Core/Log.h"

using namespace DirectX;   // 이 파일 안에서만

Renderer::Renderer()
{
}

Renderer::~Renderer()
{
	Shutdown();
}

bool Renderer::Initialize(Device* device)
{
	if (device == nullptr)
	{
		Log::Error("Renderer::Initialize : Device가 없음.");
		return false;
	}
	m_device = device;

	// ---- 셰이더: 컴파일(ShaderCompiler) → 바이트코드 → 핸들(Device) ----
	// 경로는 exe 기준 절대 경로다. 절대 경로여야 HLSL 안의 #include "Common.hlsli"가
	// 셰이더 파일 위치 기준으로 풀린다.
	std::vector<uint8_t> vsCode, psCode;
	if (!ShaderCompiler::CompileFromFile(Paths::GetShaderPath(L"BasicVertexShader.hlsl"), "VS_Main", "vs_5_0", vsCode)) return false;
	if (!ShaderCompiler::CompileFromFile(Paths::GetShaderPath(L"BasicPixelShader.hlsl"), "PS_Main", "ps_5_0", psCode)) return false;

#if defined(_DEBUG)
	// HLSL cbuffer 크기와 C++ 구조체 크기를 리플렉션으로 대조한다. 어긋나면 초기화 실패.
	bool layoutOk = true;
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(vsCode, "PerFrame", sizeof(PerFrameConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(vsCode, "PerObject", sizeof(PerObjectConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(psCode, "PerFrame", sizeof(PerFrameConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(psCode, "Lights", sizeof(LightConstants));
	if (!layoutOk)
	{
		Log::Error("Renderer::Initialize : 상수버퍼 레이아웃 불일치.");
		return false;
	}
#endif

	ShaderDesc vsDesc;
	vsDesc.stage = ShaderStage::Vertex;
	vsDesc.bytecode = vsCode.data();
	vsDesc.bytecodeSize = vsCode.size();
	vsDesc.debugName = "BasicVS";
	m_vs = m_device->CreateShader(vsDesc);

	ShaderDesc psDesc;
	psDesc.stage = ShaderStage::Pixel;
	psDesc.bytecode = psCode.data();
	psDesc.bytecodeSize = psCode.size();
	psDesc.debugName = "BasicPS";
	m_ps = m_device->CreateShader(psDesc);

	if (!m_vs.IsValid() || !m_ps.IsValid())
	{
		Log::Error("Renderer::Initialize : 셰이더 생성 실패.");
		return false;
	}

	// ---- 상수버퍼 3개. 전부 Dynamic(매 프레임/드로우마다 Map WRITE_DISCARD) ----
	auto makeCB = [this](uint32_t size, const char* name)
	{
		BufferDesc desc;
		desc.size = size;
		desc.usage = BufferUsage::Dynamic;
		desc.bindFlags = BufferBind_Constant;
		desc.debugName = name;
		return m_device->CreateBuffer(desc);
	};
	m_perFrameCB = makeCB(sizeof(PerFrameConstants), "CB_PerFrame");
	m_perObjectCB = makeCB(sizeof(PerObjectConstants), "CB_PerObject");
	m_lightCB = makeCB(sizeof(LightConstants), "CB_Lights");
	if (!m_perFrameCB.IsValid() || !m_perObjectCB.IsValid() || !m_lightCB.IsValid())
	{
		Log::Error("Renderer::Initialize : 상수버퍼 생성 실패.");
		return false;
	}

	// ---- PSO의 고정 부분. 나머지(깊이 테스트 켬, 불투명, 트라이앵글 리스트)는 Desc 기본값 ----
	m_baseDesc = PipelineStateDesc{};
	m_baseDesc.vs = m_vs;
	m_baseDesc.ps = m_ps;
	m_baseDesc.vertexLayout = GetVertexLayoutPCN();

	// 첫 프레임 전에 기본 PSO를 만들어 두어 생성 실패를 초기화 단계에서 잡는다.
	if (!GetPipelineForSettings().IsValid())
	{
		Log::Error("Renderer::Initialize : 기본 PSO 생성 실패.");
		return false;
	}

	return true;
}

void Renderer::Shutdown()
{
	if (m_device == nullptr) return;

	for (auto& entry : m_gpuMeshes)
	{
		m_device->DestroyBuffer(entry.second.vertexBuffer);
		m_device->DestroyBuffer(entry.second.indexBuffer);
	}
	m_gpuMeshes.clear();

	m_device->DestroyBuffer(m_perFrameCB);
	m_device->DestroyBuffer(m_perObjectCB);
	m_device->DestroyBuffer(m_lightCB);
	m_device->DestroyShader(m_vs);
	m_device->DestroyShader(m_ps);
	// PSO는 캐시가 소유한다. 셰이더 핸들을 지워도 PSO는 자기 참조로 셰이더 객체를 붙들고 있다.

	m_perFrameCB = m_perObjectCB = m_lightCB = BufferHandle{};
	m_vs = m_ps = ShaderHandle{};
	m_device = nullptr;
}

size_t Renderer::GetPipelineCount() const
{
	return m_device ? m_device->GetPipelineCount() : 0;
}

void Renderer::InvalidateMesh(const Mesh* mesh)
{
	auto found = m_gpuMeshes.find(mesh);
	if (found == m_gpuMeshes.end()) return;
	m_device->DestroyBuffer(found->second.vertexBuffer);
	m_device->DestroyBuffer(found->second.indexBuffer);
	m_gpuMeshes.erase(found);
}

void Renderer::Render(const Scene& scene, const Camera& camera, float totalTime)
{
	if (m_device == nullptr) return;

	// 파이프라인 상태는 매 프레임 PSO 하나로 설정한다.
	m_device->BindPipeline(GetPipelineForSettings());

	// ---- b0 PerFrame ----
	// HLSL은 column_major로 읽고 셰이더는 mul(v, M)을 쓰므로 전치해서 올린다 (ShaderConstants.h).
	const XMMATRIX view = camera.GetViewMatrix();
	const XMMATRIX proj = camera.GetProjectionMatrix();
	PerFrameConstants perFrame = {};
	XMStoreFloat4x4(&perFrame.view, XMMatrixTranspose(view));
	XMStoreFloat4x4(&perFrame.proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&perFrame.viewProj, XMMatrixTranspose(view * proj));
	perFrame.cameraPosition = camera.GetPosition();
	perFrame.time = totalTime;
	m_device->UpdateBuffer(m_perFrameCB, &perFrame, sizeof(perFrame));

	// ---- b2 Lights ----
	LightConstants lights = {};
	const std::vector<LightData>& sceneLights = scene.GetLights();
	lights.lightCount = static_cast<uint32_t>(sceneLights.size() < MAX_LIGHTS ? sceneLights.size() : MAX_LIGHTS);
	lights.ambientColor = scene.ambientColor;
	lights.specularColor = scene.specularColor;
	lights.shininess = scene.shininess;
	for (uint32_t i = 0; i < lights.lightCount; ++i)
	{
		lights.lights[i] = sceneLights[i];

		// 방향은 셰이더가 정규화된 값을 기대한다. 0벡터면 아래를 향하게.
		const XMVECTOR direction = XMLoadFloat3(&lights.lights[i].direction);
		const float lengthSquared = XMVectorGetX(XMVector3LengthSq(direction));
		if (lengthSquared > 0.000001f)
		{
			XMStoreFloat3(&lights.lights[i].direction, XMVector3Normalize(direction));
		}
		else
		{
			lights.lights[i].direction = XMFLOAT3(0.0f, -1.0f, 0.0f);
		}
	}
	m_device->UpdateBuffer(m_lightCB, &lights, sizeof(lights));

	// PS가 b0의 cameraPosition을 읽으므로 b0은 VS와 PS 둘 다에 건다.
	m_device->BindConstantBuffer(kCBSlotPerFrame, m_perFrameCB, ShaderStageMask_Vertex | ShaderStageMask_Pixel);
	m_device->BindConstantBuffer(kCBSlotLights, m_lightCB, ShaderStageMask_Pixel);

	// ---- 오브젝트 ----
	for (const GameObject& object : scene.GetObjects())
	{
		const Mesh* mesh = object.GetMesh();
		if (mesh == nullptr) continue;
		const GpuMesh* gpuMesh = GetOrCreateGpuMesh(*mesh);
		if (gpuMesh == nullptr) continue;

		// b1 PerObject. 드로우마다 Map(WRITE_DISCARD). 드라이버가 매번 새 메모리를 주므로
		// 앞 드로우가 아직 실행되지 않았어도 덮어쓰지 않는다.
		const XMMATRIX world = object.GetTransform().GetWorldMatrix();
		PerObjectConstants perObject = {};
		XMStoreFloat4x4(&perObject.world, XMMatrixTranspose(world));
		// 노멀 변환 행렬은 (W⁻¹)ᵀ. 전치해서 올려야 하므로 ((W⁻¹)ᵀ)ᵀ = W⁻¹ 를 그대로 저장한다.
		// 예전 코드의 "전치의 전치"는 이것을 우연히 맞게 계산하던 것이다.
		XMStoreFloat4x4(&perObject.worldInvTranspose, XMMatrixInverse(nullptr, world));
		m_device->UpdateBuffer(m_perObjectCB, &perObject, sizeof(perObject));
		m_device->BindConstantBuffer(kCBSlotPerObject, m_perObjectCB, ShaderStageMask_Vertex);

		m_device->BindVertexBuffer(gpuMesh->vertexBuffer);
		m_device->BindIndexBuffer(gpuMesh->indexBuffer, Format::R32_UINT);
		m_device->DrawIndexed(gpuMesh->indexCount);
	}
}

const Renderer::GpuMesh* Renderer::GetOrCreateGpuMesh(const Mesh& mesh)
{
	auto found = m_gpuMeshes.find(&mesh);
	if (found != m_gpuMeshes.end())
	{
		return &found->second;
	}
	if (!mesh.IsValid())
	{
		return nullptr;
	}

	// 정점/인덱스 버퍼는 바뀌지 않으므로 Default + 초기 데이터.
	GpuMesh gpuMesh;

	BufferDesc vbDesc;
	vbDesc.size = static_cast<uint32_t>(sizeof(VERTEX) * mesh.GetVertices().size());
	vbDesc.usage = BufferUsage::Default;
	vbDesc.bindFlags = BufferBind_Vertex;
	vbDesc.stride = sizeof(VERTEX);
	vbDesc.debugName = "VB";
	gpuMesh.vertexBuffer = m_device->CreateBuffer(vbDesc, mesh.GetVertices().data());

	BufferDesc ibDesc;
	ibDesc.size = static_cast<uint32_t>(sizeof(uint32_t) * mesh.GetIndices().size());
	ibDesc.usage = BufferUsage::Default;
	ibDesc.bindFlags = BufferBind_Index;
	ibDesc.stride = sizeof(uint32_t);
	ibDesc.debugName = "IB";
	gpuMesh.indexBuffer = m_device->CreateBuffer(ibDesc, mesh.GetIndices().data());

	gpuMesh.indexCount = mesh.GetIndexCount();

	if (!gpuMesh.vertexBuffer.IsValid() || !gpuMesh.indexBuffer.IsValid())
	{
		Log::Error("GpuMesh 생성 실패 (정점 %zu, 인덱스 %u).", mesh.GetVertices().size(), mesh.GetIndexCount());
		m_device->DestroyBuffer(gpuMesh.vertexBuffer);
		m_device->DestroyBuffer(gpuMesh.indexBuffer);
		return nullptr;
	}

	return &m_gpuMeshes.emplace(&mesh, gpuMesh).first->second;
}

PipelineHandle Renderer::GetPipelineForSettings()
{
	PipelineStateDesc desc = m_baseDesc;
	desc.rasterizer.fill = m_settings.wireframe ? FillMode::Wireframe : FillMode::Solid;
	desc.rasterizer.cull = m_settings.cullBack ? CullMode::Back : CullMode::None;
	return m_device->CreatePipeline(desc);
}
