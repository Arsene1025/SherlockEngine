#pragma once

//기본
#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <iostream>

//DX라이브러리
#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")

#include <dxgi.h>
#pragma comment(lib, "dxgi.lib")

#include <DirectXMath.h> //XNA 수학 라이브러리
#include <directxtk/SimpleMath.h>
using namespace DirectX;
using namespace DirectX::SimpleMath;

//DX확장 라이브러리
// DXTK
#include <directxtk/SpriteBatch.h>
#include <directxtk/SpriteFont.h>
#include <directxtk/WICTextureLoader.h>
#include <directxtk/DDSTextureLoader.h>
#include <directxtk/CommonStates.h>

// DirectXTex
#include <DirectXTex.h>

//D3DCompiler
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

//스마트 포인터, 자료구조
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
using std::shared_ptr;
using std::vector;

//
#include "GameTimer.h"

//내가 추가한 헤더들
#include "enum.h"
#include "struct.h"
#include "helper.h"
