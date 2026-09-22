#pragma once

// 여기에는 "거의 모든 파일이 쓰는 것"만 둔다.
// 특정 기능에만 필요한 헤더는 그것을 쓰는 .cpp에서 직접 include할 것.
//
// Direct3D 헤더(d3d11 / dxgi / d3dcompiler)는 여기에 없다. 3단계에서 뺐다.
// Graphics/D3D11/D3D11Common.h 가 유일한 자리이고, 그 폴더 안의 파일만 include한다.
// 폴더 밖에서 D3D 타입이 필요해지면 그것이 곧 추상화가 새는 지점이다.

//기본
#include <windows.h>

#include <DirectXMath.h> //XNA 수학 라이브러리
// using namespace DirectX 는 두지 않는다.
// 헤더에서는 DirectX:: 를 붙이고, .cpp에서만 파일 지역 using을 쓴다.

//표준 라이브러리
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
