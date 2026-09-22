#include "pch.h"
#include "Graphics/D3D11/Shader.h"
#include "Core/Log.h"
#include "Core/Paths.h"
#include "Graphics/struct.h"   // ConstBuffer, LightBuffer
#include "Graphics/D3D11/Device.h"

bool Shader::Initialize(Device* device)
{
	if (device == nullptr)
	{
		Log::Error("Shader::Initialize : Device가 없음.");
		return false;
	}
	graphicsDevice = device;
	return true;
}

bool Shader::ShaderCreate()
{
	if (graphicsDevice == nullptr)
	{
		Log::Error("ShaderCreate : Device가 없음. Initialize를 먼저 호출할 것.");
		return false;
	}

	if (!ShaderLoad())
	{
		return false;
	}

	//상수버퍼 생성
	pCB = graphicsDevice->CreateConstBuffer(sizeof(ConstBuffer));
	//조명 상수버퍼 생성함
	pLightCB = graphicsDevice->CreateConstBuffer(sizeof(LightBuffer));

	if (!pCB || !pLightCB)
	{
		Log::Error("ShaderCreate : 상수버퍼 생성 실패.");
		return false;
	}

	return true;
}

void Shader::BindConstantBuffers()
{
	//셰이더 상수 버퍼 설정
	graphicsDevice->GetContext()->VSSetConstantBuffers(0, 1, pCB.GetAddressOf());
	//조명 데이터 상수버퍼
	graphicsDevice->GetContext()->PSSetConstantBuffers(1, 1, pLightCB.GetAddressOf());
}

bool Shader::ShaderLoad()
{
	// 경로는 exe 기준 절대 경로다. 절대 경로여야 HLSL 안의 #include도
	// D3D_COMPILE_STANDARD_FILE_INCLUDE가 셰이더 파일 위치 기준으로 찾는다.
	const std::wstring vsPath = Paths::GetShaderPath(L"BasicVertexShader.hlsl");
	const std::wstring psPath = Paths::GetShaderPath(L"BasicPixelShader.hlsl");

	// HRESULT를 여기서 bool로 바꿔 올린다. 실패를 삼키지 않는다.
	if (FAILED(ShaderLoad(vsPath.c_str(), "VS_Main", "vs_5_0", ShaderStage::Vertex, m_vs)))
	{
		return false;
	}
	if (FAILED(ShaderLoad(psPath.c_str(), "PS_Main", "ps_5_0", ShaderStage::Pixel, m_ps)))
	{
		return false;
	}
	return true;
}

HRESULT Shader::ShaderLoad(const TCHAR* fxname, const CHAR* entry, const CHAR* target, ShaderStage stage, ShaderHandle& outHandle)
{
	// 셰이더 컴파일.
	ComPtr<ID3DBlob> pCode;
	HRESULT hr = ShaderCompile(fxname, entry, target, pCode);
	if (FAILED(hr))
	{
		return hr;
	}

	// 셰이더 객체는 Device의 풀이 만들고 소유한다. 여기는 핸들만 받는다.
	outHandle = graphicsDevice->CreateShader(stage, pCode->GetBufferPointer(), pCode->GetBufferSize());
	if (!outHandle.IsValid())
	{
		Log::Error("셰이더 객체 생성 실패 : 파일 경로 : %s 진입점 : %s 타깃 : %s",
			Log::ToUtf8(fxname).c_str(), entry, target);
		return E_FAIL;
	}

	// pCode는 ComPtr이므로 여기서 자동 해제된다. 바이트코드 사본은 풀에 있다.
	return S_OK;
}

HRESULT Shader::ShaderCompile(const TCHAR* FileName, const CHAR* EntryPoint, const CHAR* ShaderModel, ComPtr<ID3DBlob>& outCode)
{
	// 셰이더 컴파일 방법 관련 MS문서
	// https://docs.microsoft.com/en-us/windows/win32/direct3d11/how-to--compile-a-shader

	ComPtr<ID3DBlob> pError;
	HRESULT hr;

	// 컴파일 옵션. Debug 빌드에서는 디버그 정보를 남기고 최적화를 끈다.
	// 그래야 RenderDoc·PIX에서 HLSL 원본 줄 단위로 따라갈 수 있다.
	UINT compileFlags = 0;
#if defined(_DEBUG)
	compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

	/*HRESULT D3DCompileFromFile
	(
		LPCWSTR pFileName,                   //컴파일할 셰이더 경로
		const D3D_SHADER_MACRO * pDefines,	 //컴파일 시 적용할 매크로 정의
		ID3DInclude * pInclude,				 //HLSL 파일 안에서 #include를 사용할 때 include 파일을 어디서 찾을지
		LPCSTR pEntrypoint,					 //셰이더 코드 시작 함수 이름
		LPCSTR pTarget,						 //컴파일 타입과 버전
		UINT Flags1,						 //컴파일 옵션 https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/d3dcompile-constants
		UINT Flags2,					     //Effect파일 옵션 https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/d3dcompile-effect-constants
		ID3DBlob * *ppCode,					 //컴파일 성공시 결과인 바이트 코드 저장
		ID3DBlob * *ppErrorMsgs				 //실패시 에러 메시지 저장하는 Blob
	);*/

	hr = D3DCompileFromFile(
		FileName,
		nullptr,
		D3D_COMPILE_STANDARD_FILE_INCLUDE,   // HLSL 안의 #include를 파일 기준으로 찾는다
		EntryPoint,
		ShaderModel,
		compileFlags,
		0,
		outCode.ReleaseAndGetAddressOf(),
		pError.GetAddressOf());

	// 컴파일러가 남긴 메시지. 실패하면 원인이, 성공해도 경고가 들어 있을 수 있다.
	// 여기에 "파일(줄,열): error X____: 설명" 형태로 줄 번호가 들어 있다.
	const char* compilerText = nullptr;
	if (pError && pError->GetBufferSize() > 0)
	{
		compilerText = static_cast<const char*>(pError->GetBufferPointer());
	}

	if (FAILED(hr))
	{
		Log::Error("셰이더 컴파일 실패 : 파일 경로 : %s 진입점 : %s 타깃 : %s",
			Log::ToUtf8(FileName).c_str(), EntryPoint, ShaderModel);

		if (compilerText != nullptr)
		{
			// 컴파일러 텍스트에 %가 들어 있을 수 있으므로 인자로 넘긴다.
			Log::Error("%s", compilerText);
		}
		else
		{
			// Blob이 없으면 보통 파일을 못 찾은 경우다. HRESULT를 그대로 보여준다.
			Log::Error("  (컴파일러 메시지 없음. HRESULT = %s. 셰이더 파일 경로를 확인할 것.)",
				Log::HrToString(hr).c_str());
		}
	}
	else if (compilerText != nullptr)
	{
		Log::Warn("셰이더 컴파일 경고 : %s", Log::ToUtf8(FileName).c_str());
		Log::Warn("%s", compilerText);
	}

	// pError는 ComPtr이므로 여기서 자동 해제된다.
	return hr;
}
