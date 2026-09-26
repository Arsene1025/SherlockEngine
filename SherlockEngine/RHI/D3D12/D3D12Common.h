#pragma once

// D3D12 백엔드에서 D3D12/DXGI 헤더를 include 하는 유일한 지점. RHI/D3D12/ 밖에서는 include 하지 않음.
// d3dx12.h(DirectX-Headers) 는 쓰지 않음 — 필요한 헬퍼는 D3D12Convert 에 직접 작성했음.
#include <d3d12.h>
#include <d3d12sdklayers.h>   // ID3D12Debug1(GBV), ID3D12InfoQueue, ID3D12DebugDevice
#include <dxgi1_6.h>
#include <dxgidebug.h>        // IDXGIDebug1::ReportLiveObjects
#include <wrl/client.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

using Microsoft::WRL::ComPtr;
