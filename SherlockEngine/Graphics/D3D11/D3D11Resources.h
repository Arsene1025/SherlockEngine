#pragma once
#include "Graphics/D3D11/D3D11Common.h"
#include "Graphics/D3D11/D3D11Convert.h"
#include "Graphics/ResourceDesc.h"
#include "Graphics/GraphicsConfig.h"
#include "Core/Log.h"
#include <string>
#include <vector>

// 풀에 들어가는 D3D11 리소스 항목. Device만 만지고, 폴더 밖에서는 핸들만 본다.

struct D3D11Buffer
{
	BufferDesc desc;               // debugName은 아래 name으로 복사되고 여기서는 nullptr
	std::string name;
	// Dynamic은 프레임 수만큼 복제(GraphicsConfig.h 참고). 나머지는 [0]만 쓴다.
	ComPtr<ID3D11Buffer> buffers[kFrameCount];

	ID3D11Buffer* Get(uint32_t frameIndex) const
	{
		const uint32_t slot = (desc.usage == BufferUsage::Dynamic) ? (frameIndex % kFrameCount) : 0;
		return buffers[slot].Get();
	}
};

struct D3D11Texture
{
	TextureDesc desc;
	std::string name;
	ComPtr<ID3D11Texture2D> texture;

	// 뷰는 처음 요청될 때 만든다. bindFlags에 없는 뷰를 요청하면 nullptr.
	ComPtr<ID3D11RenderTargetView> rtv;
	ComPtr<ID3D11DepthStencilView> dsv;
	ComPtr<ID3D11ShaderResourceView> srv;

	ID3D11RenderTargetView* GetRTV(ID3D11Device* device)
	{
		if (!rtv && texture && (desc.bindFlags & TextureBind_RenderTarget))
		{
			const HRESULT hr = device->CreateRenderTargetView(texture.Get(), nullptr, rtv.GetAddressOf());
			if (FAILED(hr)) Log::Error("RTV 생성 실패 (%s). %s", name.c_str(), Log::HrToString(hr).c_str());
		}
		return rtv.Get();
	}

	ID3D11DepthStencilView* GetDSV(ID3D11Device* device)
	{
		if (!dsv && texture && (desc.bindFlags & TextureBind_DepthStencil))
		{
			const HRESULT hr = device->CreateDepthStencilView(texture.Get(), nullptr, dsv.GetAddressOf());
			if (FAILED(hr)) Log::Error("DSV 생성 실패 (%s). %s", name.c_str(), Log::HrToString(hr).c_str());
		}
		return dsv.Get();
	}

	ID3D11ShaderResourceView* GetSRV(ID3D11Device* device)
	{
		if (!srv && texture && (desc.bindFlags & TextureBind_ShaderResource))
		{
			const HRESULT hr = device->CreateShaderResourceView(texture.Get(), nullptr, srv.GetAddressOf());
			if (FAILED(hr)) Log::Error("SRV 생성 실패 (%s). %s", name.c_str(), Log::HrToString(hr).c_str());
		}
		return srv.Get();
	}
};

struct D3D11Shader
{
	ShaderStage stage = ShaderStage::Vertex;
	std::string name;
	ComPtr<ID3D11VertexShader> vs;
	ComPtr<ID3D11PixelShader> ps;
	std::vector<uint8_t> bytecode;   // 입력 레이아웃 생성(VS)과 리플렉션에 필요
};
