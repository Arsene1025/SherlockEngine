#include "pch.h"
#include "Shader.h"
#include "Device.h"
#include "AppBase.h"

bool Shader::Initalize(Device* device, AppBase* app)
{
	graphicsDevice = device;
	appBase = app;
	//ShaderCreate();
	return true;
}

void Shader::ShaderCreate()
{
	ShaderLoad();
	graphicsDevice->GetContext()->VSSetShader(pVS, nullptr, 0);
	graphicsDevice->GetContext()->PSSetShader(pPS, nullptr, 0);

	//상수버퍼 생성
	graphicsDevice->CreateConstBuffer(sizeof(ConstBuffer), &pCB);
}

void Shader::ShaderUpdate()
{
	//장치에 셰이더 설정
	graphicsDevice->GetContext()->VSSetShader(pVS, nullptr, 0);
	graphicsDevice->GetContext()->PSSetShader(pPS, nullptr, 0);
	//셰이더 상수 버퍼 갱신

	//셰이더 상수 버퍼 설정
	graphicsDevice->GetContext()->VSSetConstantBuffers(0, 1, &pCB);
}

void Shader::ShaderRelease()
{
	SafeRelease(pVS);
	SafeRelease(pPS);
	SafeRelease(pVSCode);
	SafeRelease(pCB);

}

bool Shader::ShaderLoad()
{
	ShaderLoad(L"BasicVertexShader.hlsl", "VS_Main", "vs_5_0", &pVS, &pVSCode);
	ShaderLoad(L"BasicPixelShader.hlsl", "PS_Main", "ps_5_0", &pPS);
	return true;
}

HRESULT Shader::ShaderLoad(const TCHAR* fxname, const CHAR* entry, const CHAR* target, ID3D11VertexShader** ppVS, ID3DBlob** ppCode)
{
	// 셰이더 컴파일.
	ID3DBlob* pCode = nullptr;
	HRESULT hr = ShaderCompile(fxname, entry, target, &pCode);
	if (FAILED(hr))
	{
		std::cout << "셰이더 컴파일 실패 : " << "파일 경로 : " << fxname << " 진입점 : " << entry << " 모델 : " << target << std::endl ;
		return hr;
	}

	//정점 셰이더 객체 생성
	ID3D11VertexShader* pVs = nullptr;
	hr = graphicsDevice->GetDevice()->CreateVertexShader(pCode->GetBufferPointer(), pCode->GetBufferSize(), nullptr, &pVS);
	if (FAILED(hr))
	{
		pCode->Release();
		pCode = nullptr;
		std::cout << "셰이더 객체 생성 실패 : " << "파일 경로 : " << fxname << " 진입점 : " << entry << " 모델 : " << target << std::endl;
		return hr;
	}
	//완료 후 생성된 셰이더와 객체 반환
	*ppVS = pVS;
	*ppCode = pCode;

	return hr;
}

HRESULT Shader::ShaderLoad(const TCHAR* fxname, const CHAR* entry, const CHAR* target, ID3D11PixelShader** ppPS)
{
	//셰이더 컴파일
	ID3DBlob* pCode = nullptr;
	HRESULT hr = ShaderCompile(fxname, entry, target, &pCode);
	if (FAILED(hr))
	{
		std::cout << "셰이더 컴파일 실패 : " << "파일 경로 : " << fxname << " 진입점 : " << entry << " 모델 : " << target << std::endl;
		return hr;
	}

	ID3D11PixelShader* pPS = nullptr;
	hr = graphicsDevice->GetDevice()->CreatePixelShader(pCode->GetBufferPointer(), pCode->GetBufferSize(), nullptr, &pPS);
	if (FAILED(hr))
	{
		pCode->Release();
		pCode = nullptr;
		std::cout << "셰이더 객체 생성 실패 : " << "파일 경로 : " << fxname << " 진입점 : " << entry << " 모델 : " << target << std::endl;
		return hr;
	}

	pCode->Release();
	pCode = nullptr;
	if (FAILED(hr))	return hr;

	*ppPS = pPS;
	return hr;
}

HRESULT Shader::ShaderCompile(const TCHAR* FileName, const CHAR* EntryPoint, const CHAR* ShaderModel, ID3DBlob** ppCode)
{
	// 셰이더 컴파일 방법 공식 MS문서
	// https://docs.microsoft.com/en-us/windows/win32/direct3d11/how-to--compile-a-shader

	ID3DBlob* pError = nullptr;
	HRESULT hr;
	
	/*HRESULT D3DCompileFromFile
	(
		LPCWSTR pFileName,                   //컴파일할 셰이더 경로
		const D3D_SHADER_MACRO * pDefines,	 //컴파일 시 사용할 매크로 정의
		ID3DInclude * pInclude,				 //HLSL 파일 안에서 #include를 사용할 때 include 파일을 어떻게 찾을지
		LPCSTR pEntrypoint,					 //셰이더 코드 시작 함수 이름
		LPCSTR pTarget,						 //셰이터 타입과 버전
		UINT Flags1,						 //컴파일 옵션 https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/d3dcompile-constants
		UINT Flags2,					     //Effect관련 옵션 https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/d3dcompile-effect-constants
		ID3DBlob * *ppCode,					 //컴파일 성공시 저장될 바이트 코드 변수
		ID3DBlob * *ppErrorMsgs				 //실패시 에러 메시지 수신하는 Blob
	);*/

	hr = D3DCompileFromFile(FileName, 0, 0, EntryPoint, ShaderModel, 0, 0, ppCode, &pError);
	if (FAILED(hr))
	{
		std::cout << "셰이더 컴파일 실패 : " << "파일 경로 : " << FileName << " 진입점 : " << EntryPoint << " 모델 : " << ShaderModel << std::endl;
	}

	if (pError)
	{
		pError->Release();
	}
	pError = nullptr;

	return hr;
}
