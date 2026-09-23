#pragma once

// D3D12 백엔드의 유일한 D3D12/DXGI include 지점. RHI/D3D12/ 밖에서는 include 하지 않는다.
// d3dx12.h(DirectX-Headers) 는 쓰지 않는다 — 필요한 헬퍼는 D3D12Convert 에 직접 적었다.
#include <d3d12.h>
#include <d3d12sdklayers.h>   // ID3D12Debug1(GBV), ID3D12InfoQueue, ID3D12DebugDevice
#include <dxgi1_6.h>
#include <dxgidebug.h>        // IDXGIDebug1::ReportLiveObjects
#include <wrl/client.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

using Microsoft::WRL::ComPtr;
