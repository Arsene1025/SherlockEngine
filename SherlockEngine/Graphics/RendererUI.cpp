#include "pch.h"
#include "Graphics/Renderer.h"
#include <algorithm>
#include "RHI/Device.h"
#include "RHI/CommandList.h"
#include "Graphics/ShaderConstants.h"
#include "UI/UIDrawList.h"
#include "Core/Log.h"
#include "Core/Profiler.h"

// 게임 UI 캔버스 (Renderer 의 멤버 함수를 파일만 나눠 구현함). 설계: docs/plan/ui-canvas-plan.html.
//
// 프레임 안에서 UI 가 지나는 길:
//   (CPU) UISystem::Build   Engine::Render 앞. 메인·미리보기 두 뷰의 정점·인덱스·명령·캔버스 행렬을 한 목록에
//   Prepare 업로드 구간       UploadUI: 명령마다 텍스처를 읽어 셋을 고르고(캐시), VB·IB·b4 를 프레임에 한 번 올림
//   톤 패스 뒤               RenderCanvasPass(0): 출력 텍스처(에디터 씬 텍스처 / 게임 백버퍼)의 UNORM 뷰에 덧그림
//   미리보기 톤 패스 뒤       RenderCanvasPass(1): 미리보기 텍스처
//
// 색은 sRGB 값 그대로: 텍스처는 선형 변환 없이(Load2D srgb = false) 읽고 UNORM 뷰에 써서 감마 공간에서 섞음 (3.2).

using namespace DirectX;

static_assert(kUIMaxCanvases == kMaxUICanvases, "UI/UIDrawList.h 와 ShaderConstants.h 의 캔버스 수가 같아야 함");

bool Renderer::GetPreviewSize(uint32_t& width, uint32_t& height) const
{
	if (m_previewCamera == nullptr || !m_previewView.IsValid() || !m_previewView.color.IsValid()) return false;
	width = m_previewView.width;
	height = m_previewView.height;
	return width > 0 && height > 0;
}

uint32_t Renderer::GetMainViewWidth() const
{
	return IsOffscreen() ? m_sceneWidth : m_outputWidth;
}

uint32_t Renderer::GetMainViewHeight() const
{
	return IsOffscreen() ? m_sceneHeight : m_outputHeight;
}

bool Renderer::GetUITextureSize(const std::string& name, const Scene& scene, uint32_t& width, uint32_t& height)
{
	if (name.empty()) return false;
	if (m_textures.GetSize2D(name, false, width, height)) return true;
	m_textures.Load2D(name, false, &scene);   // UploadUI 와 같은 색공간 (선형) — 그릴 때 다시 읽지 않음
	return m_textures.GetSize2D(name, false, width, height);
}

ResourceSetHandle Renderer::GetUITextureSet(TextureHandle texture)
{
	const uint64_t key = (static_cast<uint64_t>(texture.index) << 32) | texture.generation;
	const auto found = m_uiTextureSets.find(key);
	if (found != m_uiTextureSets.end()) return found->second;
	const ResourceSetHandle set = m_device->CreateResourceSet(ResourceSetDesc{
		.layout = m_layouts[Layout_UITexture],
		.bindings = { { .texture = texture } },   // t0
		.debugName = "Set_UITexture",
	});
	m_uiTextureSets.emplace(key, set);
	return set;
}

void Renderer::UploadUIAtlas(UIAtlasImage& atlas)
{
	if (atlas.size == 0 || atlas.pixels.size() < static_cast<size_t>(atlas.size) * atlas.size) return;
	if (atlas.version != m_uiAtlasVersion || !m_uiAtlasTexture.IsValid())
	{
		// 새로 만들거나 크기를 바꿈(키우기·비우기): 전체를 초기 데이터로. 옛 텍스처는 GPU 가 끝낸 뒤 풀리고(지연 해제), 그 셋은 캐시에서 뺌
		if (m_uiAtlasTexture.IsValid())
		{
			const uint64_t oldKey = (static_cast<uint64_t>(m_uiAtlasTexture.index) << 32) | m_uiAtlasTexture.generation;
			if (const auto found = m_uiTextureSets.find(oldKey); found != m_uiTextureSets.end())
			{
				m_device->DestroyResourceSet(found->second);
				m_uiTextureSets.erase(found);
			}
			m_device->DestroyTexture(m_uiAtlasTexture);
		}
		const TextureSubresource initial{ .data = atlas.pixels.data(), .rowPitch = atlas.size };
		m_uiAtlasTexture = m_device->CreateTexture(TextureDesc{ .width = atlas.size, .height = atlas.size, .format = Format::R8_UNORM, .bindFlags = TextureBind_ShaderResource, .debugName = "UIGlyphAtlas" }, std::span<const TextureSubresource>(&initial, 1));
		m_uiAtlasVersion = atlas.version;
		atlas.ClearDirty();
		Log::Info("UI: 글리프 아틀라스 텍스처 %u² (버전 %llu)", atlas.size, static_cast<unsigned long long>(atlas.version));
		return;
	}
	if (!atlas.HasDirty()) return;
	// 이번 프레임에 구운 글자들을 감싼 사각형 하나만 올림
	const uint32_t x = atlas.dirtyMinX;
	const uint32_t y = atlas.dirtyMinY;
	if (m_device->UpdateTexture(m_uiAtlasTexture, x, y, atlas.dirtyMaxX - x, atlas.dirtyMaxY - y, atlas.pixels.data() + static_cast<size_t>(y) * atlas.size + x, atlas.size))
	{
		atlas.ClearDirty();
	}
}

void Renderer::UploadUI(const Scene& scene)
{
	m_uiReady = false;
	if (m_uiDrawList != nullptr && m_uiDrawList->atlas != nullptr)
	{
		UploadUIAtlas(*m_uiDrawList->atlas);   // 그릴 명령이 없어도 쌓인 글자는 올려 둠
	}
	if (m_uiDrawList == nullptr || m_uiDrawList->IsEmpty() || m_uiDrawList->vertices.empty()) return;
	const UIDrawList& list = *m_uiDrawList;

	// 텍스처 캐시가 비워졌으면(씬 바꿈·텍스처 다시 읽기) 옛 핸들을 가리키는 셋을 버림
	if (m_uiTextureGeneration != m_textures.GetGeneration())
	{
		for (auto& entry : m_uiTextureSets)
		{
			m_device->DestroyResourceSet(entry.second);
		}
		m_uiTextureSets.clear();
		m_uiTextureGeneration = m_textures.GetGeneration();
	}

	// 명령마다 셋. 텍스처는 sRGB 변환 없이 읽음 (감마 공간 블렌딩). 실패·빈 이름은 흰색.
	m_uiCommandSets.resize(list.commands.size());
	for (size_t i = 0; i < list.commands.size(); ++i)
	{
		const std::string& name = list.commands[i].texture;
		if (name == kUIGlyphAtlasTexture)
		{
			m_uiCommandSets[i] = m_uiAtlasTexture.IsValid() ? GetUITextureSet(m_uiAtlasTexture) : ResourceSetHandle{};
			continue;
		}
		const TextureHandle texture = name.empty() ? m_textures.GetWhite() : m_textures.Load2D(name, false, &scene);
		m_uiCommandSets[i] = GetUITextureSet(texture.IsValid() ? texture : m_textures.GetWhite());
	}

	// 버퍼 용량: 모자라면 두 배로 다시 만듦 (Destroy 는 GPU 가 끝날 때까지 지연됨. VB·IB 는 셋에 들어 있지 않아 다시 묶기만 하면 됨)
	const uint32_t vertexCount = static_cast<uint32_t>(list.vertices.size());
	const uint32_t indexCount = static_cast<uint32_t>(list.indices.size());
	if (vertexCount > m_uiVertexCapacity || !m_uiVB.IsValid())
	{
		m_device->DestroyBuffer(m_uiVB);
		m_uiVertexCapacity = (std::max)(1024u, m_uiVertexCapacity);
		while (m_uiVertexCapacity < vertexCount)
		{
			m_uiVertexCapacity *= 2;
		}
		m_uiVB = m_device->CreateBuffer(BufferDesc{ .size = m_uiVertexCapacity * static_cast<uint32_t>(sizeof(UIVertex)), .usage = BufferUsage::Dynamic, .bindFlags = BufferBind_Vertex, .stride = sizeof(UIVertex), .debugName = "VB_UI" });
		Log::Info("UI: 정점 버퍼 %u 개 (이번 프레임 %u)", m_uiVertexCapacity, vertexCount);
	}
	if (indexCount > m_uiIndexCapacity || !m_uiIB.IsValid())
	{
		m_device->DestroyBuffer(m_uiIB);
		m_uiIndexCapacity = (std::max)(1536u, m_uiIndexCapacity);
		while (m_uiIndexCapacity < indexCount)
		{
			m_uiIndexCapacity *= 2;
		}
		m_uiIB = m_device->CreateBuffer(BufferDesc{ .size = m_uiIndexCapacity * static_cast<uint32_t>(sizeof(uint32_t)), .usage = BufferUsage::Dynamic, .bindFlags = BufferBind_Index, .stride = sizeof(uint32_t), .debugName = "IB_UI" });
		Log::Info("UI: 인덱스 버퍼 %u 개 (이번 프레임 %u)", m_uiIndexCapacity, indexCount);
	}
	if (!m_uiVB.IsValid() || !m_uiIB.IsValid())
	{
		Log::Error("UI: 정점·인덱스 버퍼 생성 실패 (정점 %u, 인덱스 %u)", vertexCount, indexCount);
		return;
	}
	m_device->UpdateBuffer(m_uiVB, list.vertices.data(), vertexCount * static_cast<uint32_t>(sizeof(UIVertex)));
	m_device->UpdateBuffer(m_uiIB, list.indices.data(), indexCount * static_cast<uint32_t>(sizeof(uint32_t)));

	// b4: 캔버스 행렬 (HLSL column_major + mul(v, M) 이라 전치해서 올림)
	UIConstants constants = {};
	const size_t canvasCount = (std::min)(list.canvasMatrices.size(), static_cast<size_t>(kMaxUICanvases));
	for (size_t i = 0; i < canvasCount; ++i)
	{
		XMStoreFloat4x4(&constants.canvasMatrices[i], XMMatrixTranspose(XMLoadFloat4x4(&list.canvasMatrices[i])));
	}
	m_device->UpdateBuffer(m_uiCB, &constants, sizeof(constants));
	m_uiReady = true;
}

PipelineHandle Renderer::GetUIPipeline()
{
	if (m_uiPipeline.IsValid()) return m_uiPipeline;
	m_uiPipeline = m_device->CreatePipeline(m_passDesc[Pass_UI]);   // 타깃 R8G8B8A8_UNORM, 깊이 없음 (BuildPassDescs)
	return m_uiPipeline;
}

void Renderer::RenderCanvasPass(RHI::CommandList& cmd, uint32_t viewIndex, TextureHandle target, TextureHandle depth, ResourceState endState, uint32_t width, uint32_t height)
{
	if (!m_uiReady || m_uiDrawList == nullptr || viewIndex > 1 || !target.IsValid()) return;
	const UIDrawList& list = *m_uiDrawList;
	const UIViewRange& range = list.views[viewIndex];
	if (range.commandCount == 0) return;
	(void)width;
	(void)height;

	GpuProfileScope gpuScope(cmd, viewIndex == 0 ? "UICanvasPass" : "PreviewUICanvasPass");
	cmd.Barrier(target, ResourceState::RenderTarget);
	const auto drawRange = [&](PipelineHandle pipeline, uint32_t first, uint32_t count)
	{
		cmd.SetPipelineState(pipeline);
		cmd.SetResourceSet(kSetUICommon, m_uiCommonSet);
		cmd.SetVertexBuffer(m_uiVB);
		cmd.SetIndexBuffer(m_uiIB, Format::R32_UINT);
		++m_stats.pipelineSwitches;
		ResourceSetHandle bound;
		for (uint32_t i = first; i < first + count && i < list.commands.size(); ++i)
		{
			const UIDrawCmd& draw = list.commands[i];
			if (draw.indexCount == 0) continue;
			const ResourceSetHandle set = i < m_uiCommandSets.size() ? m_uiCommandSets[i] : ResourceSetHandle{};
			if (!set.IsValid()) continue;
			if (!(set == bound))
			{
				cmd.SetResourceSet(kSetUITexture, set);
				bound = set;
			}
			cmd.DrawIndexed(draw.indexCount, draw.indexStart, 0);
			++m_stats.draws;
			m_stats.triangles += draw.indexCount / 3;
		}
	};

	// World 캔버스 (앞쪽 명령): 뷰 깊이를 읽기 전용으로 묶어 장면에 가려지게. 톤 패스 뒤 깊이는 DepthRead (ApplyHdrEffects).
	const uint32_t worldCount = (std::min)(range.worldCommandCount, range.commandCount);
	if (worldCount > 0 && depth.IsValid())
	{
		if (!m_uiWorldPipeline.IsValid())
		{
			m_uiWorldPipeline = m_device->CreatePipeline(m_passDesc[Pass_UIWorld]);
		}
		if (m_uiWorldPipeline.IsValid())
		{
			cmd.Barrier(depth, ResourceState::DepthRead);
			const RenderPassDesc pass{
				.colors = { { .texture = target, .load = LoadOp::Load, .srgbView = false } },
				.colorCount = 1,
				.depth = { .texture = depth, .load = LoadOp::Load, .readOnly = true },
				.debugName = viewIndex == 0 ? "UIWorldCanvasPass" : "PreviewUIWorldCanvasPass",
			};
			cmd.BeginRenderPass(pass);
			drawRange(m_uiWorldPipeline, range.firstCommand, worldCount);
			cmd.EndRenderPass();
		}
	}

	// Overlay 캔버스: 깊이 없이 위에 덧그림
	if (range.commandCount > worldCount)
	{
		const PipelineHandle pipeline = GetUIPipeline();
		if (pipeline.IsValid())
		{
			const RenderPassDesc pass{
				.colors = { { .texture = target, .load = LoadOp::Load, .srgbView = false } },   // UNORM 뷰: sRGB 값을 그대로 섞음
				.colorCount = 1,
				.debugName = viewIndex == 0 ? "UICanvasPass" : "PreviewUICanvasPass",
			};
			cmd.BeginRenderPass(pass);
			drawRange(pipeline, range.firstCommand + worldCount, range.commandCount - worldCount);
			cmd.EndRenderPass();
		}
	}
	cmd.Barrier(target, endState);
}

void Renderer::DestroyUIResources()
{
	for (auto& entry : m_uiTextureSets)
	{
		m_device->DestroyResourceSet(entry.second);
	}
	m_uiTextureSets.clear();
	m_uiCommandSets.clear();
	m_device->DestroyResourceSet(m_uiCommonSet);
	m_uiCommonSet = ResourceSetHandle{};
	for (BufferHandle* buffer : { &m_uiVB, &m_uiIB, &m_uiCB })
	{
		m_device->DestroyBuffer(*buffer);
		*buffer = BufferHandle{};
	}
	m_device->DestroyTexture(m_uiAtlasTexture);
	m_uiAtlasTexture = TextureHandle{};
	m_uiAtlasVersion = 0;
	m_uiVertexCapacity = 0;
	m_uiIndexCapacity = 0;
	m_uiPipeline = PipelineHandle{};
	m_uiWorldPipeline = PipelineHandle{};
	m_uiReady = false;
	m_uiDrawList = nullptr;
}
