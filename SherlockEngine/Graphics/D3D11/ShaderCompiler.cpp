#include "pch.h"
#include "Graphics/D3D11/ShaderCompiler.h"
#include "Graphics/D3D11/D3D11Common.h"
#include "Core/Log.h"
#include <d3d11shader.h>   // ID3D11ShaderReflection

bool ShaderCompiler::CompileFromFile(const std::wstring& path, const char* entryPoint, const char* target,
	std::vector<uint8_t>& outBytecode)
{
	// 셰이더 컴파일 방법 관련 MS문서
	// https://learn.microsoft.com/en-us/windows/win32/direct3d11/how-to--compile-a-shader

	// 컴파일 옵션. Debug 빌드에서는 디버그 정보를 남기고 최적화를 끈다.
	// 그래야 RenderDoc·PIX에서 HLSL 원본 줄 단위로 따라갈 수 있다.
	UINT compileFlags = 0;
#if defined(_DEBUG)
	compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

	ComPtr<ID3DBlob> code;
	ComPtr<ID3DBlob> error;
	const HRESULT hr = D3DCompileFromFile(
		path.c_str(),
		nullptr,                              // 매크로 없음
		D3D_COMPILE_STANDARD_FILE_INCLUDE,    // #include를 셰이더 파일 위치 기준으로 찾는다
		entryPoint,
		target,
		compileFlags,
		0,
		code.GetAddressOf(),
		error.GetAddressOf());

	// 컴파일러가 남긴 메시지. 실패하면 원인이, 성공해도 경고가 들어 있을 수 있다.
	// "파일(줄,열): error X____: 설명" 형태로 줄 번호가 들어 있다.
	const char* compilerText = nullptr;
	if (error && error->GetBufferSize() > 0)
	{
		compilerText = static_cast<const char*>(error->GetBufferPointer());
	}

	if (FAILED(hr))
	{
		Log::Error("셰이더 컴파일 실패 : 파일 경로 : %s 진입점 : %s 타깃 : %s",
			Log::ToUtf8(path.c_str()).c_str(), entryPoint, target);
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
		return false;
	}

	if (compilerText != nullptr)
	{
		Log::Warn("셰이더 컴파일 경고 : %s", Log::ToUtf8(path.c_str()).c_str());
		Log::Warn("%s", compilerText);
	}

	const uint8_t* bytes = static_cast<const uint8_t*>(code->GetBufferPointer());
	outBytecode.assign(bytes, bytes + code->GetBufferSize());
	return true;
}

bool ShaderCompiler::ValidateConstantBufferSize(const std::vector<uint8_t>& bytecode, const char* cbufferName, uint32_t expectedSize)
{
	ComPtr<ID3D11ShaderReflection> reflection;
	HRESULT hr = D3DReflect(bytecode.data(), bytecode.size(), IID_PPV_ARGS(reflection.GetAddressOf()));
	if (FAILED(hr))
	{
		Log::Warn("D3DReflect 실패. cbuffer '%s' 크기 검사를 건너뜀. %s", cbufferName, Log::HrToString(hr).c_str());
		return true;
	}

	// GetConstantBufferByName은 없는 이름에도 더미 객체를 돌려준다. GetDesc가 실패하는지로 판단한다.
	ID3D11ShaderReflectionConstantBuffer* cbuffer = reflection->GetConstantBufferByName(cbufferName);
	D3D11_SHADER_BUFFER_DESC desc = {};
	hr = cbuffer->GetDesc(&desc);
	if (FAILED(hr))
	{
		Log::Info("cbuffer '%s' 는 이 셰이더가 쓰지 않아 리플렉션에 없음. 검사 생략.", cbufferName);
		return true;
	}

	if (desc.Size != expectedSize)
	{
		Log::Error("cbuffer '%s' 크기 불일치: HLSL %u 바이트, C++ %u 바이트. 패킹 규칙(float3 뒤 스칼라)을 확인할 것.",
			cbufferName, desc.Size, expectedSize);
		return false;
	}

	Log::Info("cbuffer '%s' 크기 일치: %u 바이트 (변수 %u개)", cbufferName, desc.Size, desc.Variables);
	return true;
}
