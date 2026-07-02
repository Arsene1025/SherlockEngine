#pragma once

//
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
using namespace DirectX;

//DX확장 라이브러리
// DXTK
#include <directxtk/SpriteBatch.h>
#include <directxtk/SpriteFont.h>
#include <directxtk/WICTextureLoader.h>
#include <directxtk/DDSTextureLoader.h>
#include <directxtk/SimpleMath.h>
#include <directxtk/CommonStates.h>

// DirectXTex
#include <DirectXTex.h>

//스마트 포인터, 자료구조
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
using std::shared_ptr;
using std::vector;