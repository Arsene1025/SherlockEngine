#include "pch.h"
#include "Graphics/D3D11/PipelineState.h"
#include "Graphics/D3D11/D3D11Convert.h"
#include "Graphics/D3D11/Device.h"
#include "Core/Log.h"

bool PipelineState::Create(Device& device, const PipelineStateDesc& desc)
{
	m_desc = desc;
	ID3D11Device* d3d = device.GetDevice();
	if (d3d == nullptr)
	{
		Log::Error("PipelineState::Create : Device가 없음.");
		return false;
	}

	// 셰이더. 핸들을 풀에서 실제 객체로 바꾼다.
	const D3D11Shader* vs = device.GetShader(desc.vs);
	const D3D11Shader* ps = device.GetShader(desc.ps);
	if (vs == nullptr || vs->stage != ShaderStage::Vertex || !vs->vs)
	{
		Log::Error("PipelineState::Create : 정점 셰이더 핸들이 유효하지 않음.");
		return false;
	}
	if (ps == nullptr || ps->stage != ShaderStage::Pixel || !ps->ps)
	{
		Log::Error("PipelineState::Create : 픽셀 셰이더 핸들이 유효하지 않음.");
		return false;
	}
	m_vs = vs->vs;
	m_ps = ps->ps;

	// 입력 레이아웃. D3D11은 정점 셰이더 바이트코드로 시그니처를 검증하므로
	// 셰이더의 바이트코드 사본이 필요하다. 그래서 D3D11Shader가 바이트코드를 보관한다.
	if (desc.vertexLayout.attributeCount == 0 || desc.vertexLayout.attributeCount > kMaxVertexAttributes)
	{
		Log::Error("PipelineState::Create : 정점 속성 개수가 잘못됨 (%u).", desc.vertexLayout.attributeCount);
		return false;
	}

	D3D11_INPUT_ELEMENT_DESC elements[kMaxVertexAttributes] = {};
	for (uint32_t i = 0; i < desc.vertexLayout.attributeCount; ++i)
	{
		const VertexAttribute& a = desc.vertexLayout.attributes[i];
		elements[i].SemanticName = D3D11Convert::ToSemanticName(a.semantic);
		elements[i].SemanticIndex = a.semanticIndex;
		elements[i].Format = D3D11Convert::ToDXGI(a.format);
		elements[i].InputSlot = a.inputSlot;
		elements[i].AlignedByteOffset = a.offset;
		elements[i].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		elements[i].InstanceDataStepRate = 0;
	}

	HRESULT hr = d3d->CreateInputLayout(
		elements, desc.vertexLayout.attributeCount,
		vs->bytecode.data(), vs->bytecode.size(),
		m_inputLayout.ReleaseAndGetAddressOf());
	if (FAILED(hr))
	{
		Log::Error("PipelineState::Create : CreateInputLayout 실패. %s", Log::HrToString(hr).c_str());
		return false;
	}

	// 래스터라이저 / 깊이스텐실 / 블렌드. PSO마다 따로 만든다.
	// 같은 Desc를 공유해 D3D11 객체 수를 줄이는 하위 캐시는 PSO 수가 늘면 넣는다.
	const D3D11_RASTERIZER_DESC rd = D3D11Convert::ToD3D11(desc.rasterizer);
	hr = d3d->CreateRasterizerState(&rd, m_rasterizerState.ReleaseAndGetAddressOf());
	if (FAILED(hr))
	{
		Log::Error("PipelineState::Create : CreateRasterizerState 실패. %s", Log::HrToString(hr).c_str());
		return false;
	}

	const D3D11_DEPTH_STENCIL_DESC dd = D3D11Convert::ToD3D11(desc.depthStencil);
	hr = d3d->CreateDepthStencilState(&dd, m_depthStencilState.ReleaseAndGetAddressOf());
	if (FAILED(hr))
	{
		Log::Error("PipelineState::Create : CreateDepthStencilState 실패. %s", Log::HrToString(hr).c_str());
		return false;
	}

	const D3D11_BLEND_DESC bd = D3D11Convert::ToD3D11(desc.blend);
	hr = d3d->CreateBlendState(&bd, m_blendState.ReleaseAndGetAddressOf());
	if (FAILED(hr))
	{
		Log::Error("PipelineState::Create : CreateBlendState 실패. %s", Log::HrToString(hr).c_str());
		return false;
	}

	m_topology = D3D11Convert::ToD3D11(desc.topology);
	return true;
}

void PipelineState::Bind(ID3D11DeviceContext* context) const
{
	if (context == nullptr) return;

	// D3D12의 SetPipelineState 하나에 해당하는 일곱 호출.
	context->IASetInputLayout(m_inputLayout.Get());
	context->IASetPrimitiveTopology(m_topology);
	context->VSSetShader(m_vs.Get(), nullptr, 0);
	context->PSSetShader(m_ps.Get(), nullptr, 0);
	context->RSSetState(m_rasterizerState.Get());
	// 스텐실 참조값과 블렌드 팩터는 동적 상태다. 기본값을 준다.
	context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);
	context->OMSetBlendState(m_blendState.Get(), nullptr, 0xFFFFFFFF);
}
