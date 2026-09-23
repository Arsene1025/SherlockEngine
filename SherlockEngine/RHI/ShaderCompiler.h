#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "RHI/BindingTypes.h"

// HLSL → 바이트코드. Device는 바이트코드만 받는다 (RHI는 컴파일러를 모른다).
//
// 4단계(D12): 빌드가 fxc로 만든 .cso를 우선 쓰고, Debug 빌드에서 소스 .hlsl이
// .cso보다 새로우면 런타임 컴파일로 대체한다. 핫리로드는 Renderer가 원본 파일의
// 수정 시각을 감시하다가 CompileFromFile을 다시 부르는 것으로 구현한다.
namespace ShaderCompiler
{
	// D3DCompileFromFile. Debug 빌드는 D3DCOMPILE_DEBUG | SKIP_OPTIMIZATION,
	// 그 외는 OPTIMIZATION_LEVEL3. 컴파일러 메시지(줄 번호 포함)를 Log로 보낸다.
	// path는 절대 경로여야 셰이더 안의 #include가 그 파일 위치 기준으로 풀린다.
	bool CompileFromFile(const std::wstring& path, const char* entryPoint, const char* target,
		std::vector<uint8_t>& outBytecode);

	// 파일을 통째로 읽는다(.cso 로드용).
	bool LoadFile(const std::wstring& path, std::vector<uint8_t>& outBytes);

	// .cso 우선, 필요하면 런타임 컴파일.
	//   Debug : 소스 .hlsl(또는 Common.hlsli)이 .cso보다 새로우면 소스를 컴파일한다.
	//   Release: .cso를 로드한다. 없으면 경고 후 exe 옆 .hlsl을 컴파일한다.
	// hlslName은 "BasicVertexShader.hlsl" 처럼 파일 이름만. outSource에 실제로 쓴 경로가 온다.
	bool LoadOrCompile(const wchar_t* hlslName, const char* entryPoint, const char* target,
		std::vector<uint8_t>& outBytecode, std::wstring* outSource = nullptr);

	// 리플렉션으로 cbuffer 크기를 C++ 구조체 크기와 대조한다. 패킹이 어긋나면
	// 화면이 조용히 깨지는 대신 여기서 잡힌다. 셰이더가 그 cbuffer를 쓰지 않아
	// 리플렉션에 없으면 true(검사할 것이 없음)를 돌려주고 Info 로그를 남긴다.
	bool ValidateConstantBufferSize(const std::vector<uint8_t>& bytecode, const char* cbufferName, uint32_t expectedSize);

	// 셰이더가 실제로 쓰는 바인딩(b#/t#/s#)을 리플렉션으로 읽는다.
	// Renderer가 수동 BindingLayout 선언과 대조하는 데 쓴다.
	struct ReflectedBinding
	{
		BindingType type;
		uint8_t reg;
		std::string name;
	};
	bool ReflectBindings(const std::vector<uint8_t>& bytecode, std::vector<ReflectedBinding>& out);

	// 파일의 마지막 수정 시각(FILETIME을 64비트로). 없으면 0.
	uint64_t GetLastWriteTime(const std::wstring& path);
}
