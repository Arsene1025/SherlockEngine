#pragma once

// 여기에는 "거의 모든 파일이 쓰는 것"만 둔다.
// 특정 기능에만 필요한 헤더는 그것을 쓰는 .cpp에서 직접 include할 것.

//기본
#include <windows.h>

//DX라이브러리
#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")

#include <dxgi.h>

#include <DirectXMath.h> //XNA 수학 라이브러리
// using namespace DirectX 는 두지 않는다.
// 헤더에서는 DirectX:: 를 붙이고, .cpp에서만 파일 지역 using을 쓴다.

//D3DCompiler
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

//스마트 포인터
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

//표준 라이브러리
#include <iostream>
#include <string>
#include <vector>
#include <memory>
