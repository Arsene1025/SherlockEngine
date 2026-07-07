#pragma once


//
// 현재는 VertexBuffer등의 렌더링에 필요한 지원과 작업을 Renderer클래스에 몰아놓고 나중에 분리
// 지금 Renderer클래스는 D3D수업 자료를 기반으로 구성
// 
// *************예상 구조****************
//  GameObject
//├─ Transform
//└─ MeshRenderer
//	├─ Mesh 참조
//	└─ Material 참조
// 
// Mesh
//├─ VertexBuffer
//└─ IndexBuffer
// 
// Renderer
//└─ MeshRenderer들을 모아서 Draw
//**************************************


class Device;
class Shader;
class Camera;

class Renderer
{
public:
	Renderer();
	~Renderer();

	bool Initialize(Device* device, Shader* shader, Camera* camera);

	//일단 public으로 몰아넣기
	//***************D3D수업자료 기반***************
	int DataLoading();
	void DataRelease();
	void Render();
	void RenderModeUpdate();

	//추후 Gameobject의 Mesh쪽으로 이동
	int ObjLoad(); 
	void ObjRelease();
	void ObjUpdate();
	void ObjDraw();

	//RS
	void RasterStateCreate();
	void RasterStateRelease();

	//**********************************************

private:

public:

	//일단 public으로 몰아넣기
	//***************D3D수업자료 기반***************
	// 
	//정점 버퍼
	ID3D11Buffer* g_vertexBuffer = nullptr;

	//Input Layout
	ID3D11InputLayout* g_vertexBufferLayout = nullptr;

	//렌더링 상태 플래그
	BOOL g_bCullback = FALSE;
	BOOL g_bWireFrame = FALSE;
	#define g_bSolid (!g_bWireFrame)

	//배경 색상
	XMFLOAT4 g_ClearColor = XMFLOAT4(0.0f, 0.125f, 0.3f, 1.0f);

	//레스터라이져 상태 객체 배열
	ID3D11RasterizerState* g_RState[RS_MAX_] = { NULL, };

	//현재 렌더링 모드
	DWORD g_RMode = RM_DEFAULT;

	//**********************************************

private:
	Device* graphicsDevice = nullptr;
	Shader* graphicsShader = nullptr;
    Camera* mainCamera = nullptr;
};

