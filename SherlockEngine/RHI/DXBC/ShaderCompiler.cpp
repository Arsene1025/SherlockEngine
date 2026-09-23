#include "pch.h"
#include "RHI/ShaderCompiler.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#pragma comment(lib, "d3dcompiler.lib")
using Microsoft::WRL::ComPtr;
#include "Core/Log.h"
#include "Core/Paths.h"
#include <d3d11shader.h>   // ID3D11ShaderReflection
#include <fstream>

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

bool ShaderCompiler::LoadFile(const std::wstring& path, std::vector<uint8_t>& outBytes)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file)
	{
		return false;
	}
	const std::streamsize size = file.tellg();
	if (size <= 0)
	{
		return false;
	}
	outBytes.resize(static_cast<size_t>(size));
	file.seekg(0);
	return static_cast<bool>(file.read(reinterpret_cast<char*>(outBytes.data()), size));
}

uint64_t ShaderCompiler::GetLastWriteTime(const std::wstring& path)
{
	WIN32_FILE_ATTRIBUTE_DATA data = {};
	if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
	{
		return 0;
	}
	return (static_cast<uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
}

bool ShaderCompiler::LoadOrCompile(const wchar_t* hlslName, const char* entryPoint, const char* target,
	std::vector<uint8_t>& outBytecode, std::wstring* outSource)
{
	// "BasicVertexShader.hlsl" → "BasicVertexShader.cso"
	std::wstring csoName = hlslName;
	const size_t dot = csoName.find_last_of(L'.');
	if (dot != std::wstring::npos) csoName.erase(dot);
	csoName += L".cso";

	const std::wstring csoPath = Paths::GetShaderBinaryPath(csoName.c_str());
	const uint64_t csoTime = GetLastWriteTime(csoPath);

#if defined(_DEBUG)
	// 소스가 .cso보다 새로우면(빌드하지 않고 셰이더만 고친 경우) 소스를 컴파일한다.
	// Common.hlsli는 include되므로 그 수정 시각도 본다.
	const std::wstring sourcePath = Paths::GetShaderSourcePath(hlslName);
	const uint64_t sourceTime = GetLastWriteTime(sourcePath);
	const uint64_t commonTime = GetLastWriteTime(Paths::GetShaderSourcePath(L"Common.hlsli"));
	const uint64_t newestSource = sourceTime > commonTime ? sourceTime : commonTime;

	if (sourceTime != 0 && (csoTime == 0 || newestSource > csoTime))
	{
		if (csoTime == 0)
			Log::Info("셰이더 %s : .cso 없음. 소스에서 컴파일.", Log::ToUtf8(hlslName).c_str());
		else
			Log::Info("셰이더 %s : 소스가 .cso보다 새로움. 소스에서 컴파일.", Log::ToUtf8(hlslName).c_str());
		if (outSource) *outSource = sourcePath;
		return CompileFromFile(sourcePath, entryPoint, target, outBytecode);
	}
#endif

	if (csoTime != 0 && LoadFile(csoPath, outBytecode))
	{
		if (outSource) *outSource = csoPath;
		return true;
	}

	// .cso가 없다. exe 옆 .hlsl(없으면 소스 트리)로 폴백.
	const std::wstring fallback = Paths::GetShaderPath(hlslName);
	Log::Warn("셰이더 %s : .cso를 찾지 못해 런타임 컴파일로 폴백 (%s).",
		Log::ToUtf8(hlslName).c_str(), Log::ToUtf8(fallback.c_str()).c_str());
	if (outSource) *outSource = fallback;
	return CompileFromFile(fallback, entryPoint, target, outBytecode);
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

bool ShaderCompiler::ReflectBindings(const std::vector<uint8_t>& bytecode, std::vector<ReflectedBinding>& out)
{
	out.clear();
	ComPtr<ID3D11ShaderReflection> reflection;
	HRESULT hr = D3DReflect(bytecode.data(), bytecode.size(), IID_PPV_ARGS(reflection.GetAddressOf()));
	if (FAILED(hr))
	{
		Log::Warn("D3DReflect 실패. 바인딩 리플렉션을 건너뜀. %s", Log::HrToString(hr).c_str());
		return false;
	}

	D3D11_SHADER_DESC shaderDesc = {};
	if (FAILED(reflection->GetDesc(&shaderDesc)))
	{
		return false;
	}

	for (UINT i = 0; i < shaderDesc.BoundResources; ++i)
	{
		D3D11_SHADER_INPUT_BIND_DESC bind = {};
		if (FAILED(reflection->GetResourceBindingDesc(i, &bind))) continue;

		ReflectedBinding entry;
		entry.reg = static_cast<uint8_t>(bind.BindPoint);
		entry.name = bind.Name ? bind.Name : "";
		switch (bind.Type)
		{
		case D3D_SIT_CBUFFER:   entry.type = BindingType::ConstantBuffer; break;
		case D3D_SIT_TEXTURE:   entry.type = BindingType::ShaderResource; break;
		case D3D_SIT_SAMPLER:   entry.type = BindingType::Sampler; break;
		default:
			// StructuredBuffer, UAV 등은 아직 레이아웃에 없다. 나오면 경고로 알린다.
			Log::Warn("리플렉션: 지원하지 않는 바인딩 타입 %d ('%s', 슬롯 %u).", static_cast<int>(bind.Type), entry.name.c_str(), bind.BindPoint);
			continue;
		}
		out.push_back(entry);
	}
	return true;
}
