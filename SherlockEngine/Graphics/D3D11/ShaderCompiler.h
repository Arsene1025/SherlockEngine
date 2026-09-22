#pragma once
#include <cstdint>
#include <string>
#include <vector>

// HLSL → 바이트코드. Device는 바이트코드만 받는다 (RHI는 컴파일러를 모른다).
//
// 4단계에서 fxc/dxc 사전 컴파일(.cso)과 핫리로드가 여기에 붙는다.
namespace ShaderCompiler
{
	// D3DCompileFromFile. Debug 빌드는 D3DCOMPILE_DEBUG | SKIP_OPTIMIZATION,
	// 그 외는 OPTIMIZATION_LEVEL3. 컴파일러 메시지(줄 번호 포함)를 Log로 보낸다.
	// path는 절대 경로여야 셰이더 안의 #include가 그 파일 위치 기준으로 풀린다.
	bool CompileFromFile(const std::wstring& path, const char* entryPoint, const char* target,
		std::vector<uint8_t>& outBytecode);

	// 리플렉션으로 cbuffer 크기를 C++ 구조체 크기와 대조한다. 패킹이 어긋나면
	// 화면이 조용히 깨지는 대신 여기서 잡힌다. 셰이더가 그 cbuffer를 쓰지 않아
	// 리플렉션에 없으면 true(검사할 것이 없음)를 돌려주고 Info 로그를 남긴다.
	bool ValidateConstantBufferSize(const std::vector<uint8_t>& bytecode, const char* cbufferName, uint32_t expectedSize);
}
