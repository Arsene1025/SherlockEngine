#pragma once
#include "Graphics/D3D11/D3D11Common.h"
#include "Graphics/Handle.h"
#include "Graphics/PipelineTypes.h"   // ShaderStage
class Device;

// 기본 셰이더 쌍(VS/PS)과 상수버퍼 두 개를 관리한다.
//
// 1단계 이후 셰이더 객체는 Device의 풀이 소유하고 여기는 핸들만 든다.
// VSSetShader/PSSetShader는 더 이상 여기서 부르지 않는다. PSO가 한다.
// 3단계에서 상수버퍼는 Renderer로, 컴파일은 ShaderCompiler로 옮겨 가고
// 이 클래스는 사라진다.
class Shader
{
public:
	bool Initialize(Device* device);

	bool ShaderCreate();
	// 상수버퍼 슬롯 바인딩만 한다. b0 = VS ConstBuffer, b1 = PS LightBuffer.
	void BindConstantBuffers();

	ShaderHandle GetVS() const { return m_vs; }
	ShaderHandle GetPS() const { return m_ps; }

	// 아래 게터는 소유하지 않는 참조를 돌려준다. 수명은 Shader가 관리한다.
	ID3D11Buffer* GetCBBuffer() { return pCB.Get(); }
	ID3D11Buffer* GetLightCBBuffer() { return pLightCB.Get(); }

private:
	bool ShaderLoad();
	HRESULT ShaderLoad(const TCHAR* fxname, const CHAR* entry, const CHAR* target, ShaderStage stage, ShaderHandle& outHandle);
	HRESULT ShaderCompile(const TCHAR* FileName, const CHAR* EntryPoint, const CHAR* ShaderModel, ComPtr<ID3DBlob>& outCode);

private:
	ShaderHandle m_vs;
	ShaderHandle m_ps;

	//상수버퍼
	ComPtr<ID3D11Buffer> pCB;
	ComPtr<ID3D11Buffer> pLightCB;

	//Device (소유하지 않는 참조)
	Device* graphicsDevice = nullptr;
};
