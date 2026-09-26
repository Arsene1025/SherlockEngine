#pragma once

// 여기에는 "거의 모든 파일이 쓰는 것"만 둠.
// 특정 기능에만 필요한 헤더는 그것을 쓰는 .cpp에서 직접 include할 것.
//
// Direct3D 헤더(d3d11 / dxgi / d3dcompiler)는 여기에 두지 않음. 3단계에서 뺐음.
// 이 헤더들을 include하는 곳은 RHI/D3D11/D3D11Common.h 하나뿐이고, 그 헤더는 같은 폴더 안의 파일만 include함.
// 폴더 밖에서 D3D 타입이 필요해지면 그곳이 곧 추상화가 새는 지점임.

//기본
#include <windows.h>

#include <DirectXMath.h> //XNA 수학 라이브러리
// using namespace DirectX 는 두지 않음.
// 헤더에서는 DirectX:: 를 붙이고, .cpp에서만 파일 지역 using을 씀.

//표준 라이브러리
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
