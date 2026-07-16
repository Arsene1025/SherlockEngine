#pragma once


//Vertex구조체
struct VERTEX
{
	float x, y, z; 			//좌표(Position)
	float r, g, b, a;		//색상(Diffuse Color)
	float nx, ny, nz;		//노멀(Normal)
};

struct GameTime
{
    float deltaTime = 0.0f;
    float totalTime = 0.0f;

    float GetDeltaTimeMS() const
    {
        return deltaTime * 1000.0f;
    }
};

struct ConstBuffer
{
	XMMATRIX mWorld;
	XMMATRIX mWorldInverseTranspose;
	XMMATRIX mView;
	XMMATRIX mProj;
	XMMATRIX mWVP;
};



//16바이트 정렬
struct alignas(16) LightData
{
	XMFLOAT3 position = XMFLOAT3(0.0f, 10.0f, -10.0f);
	float intensity = 1.0f;

	// 빛이 진행하는 방향. 셰이더의 표면 방향 벡터 L은 반대 방향을 사용
	XMFLOAT3 direction = XMFLOAT3(0.0f, -0.4472136f, 0.8944272f);
	float range = 50.0f;

	XMFLOAT3 color = XMFLOAT3(1.0f, 1.0f, 1.0f);
	UINT type = static_cast<UINT>(LightType::Directional);

	// constant, linear, quadratic attenuation
	XMFLOAT3 attenuation = XMFLOAT3(1.0f, 0.045f, 0.0075f);
	float innerConeCos = 0.9238795f; // cos(22.5 degrees)

	float outerConeCos = 0.8660254f; // cos(30 degrees)
	XMFLOAT3 padding = XMFLOAT3(0.0f, 0.0f, 0.0f);
};

struct alignas(16) LightBuffer
{
	LightData lights[MAX_LIGHTS];

	XMFLOAT3 cameraPosition = XMFLOAT3(0.0f, 0.0f, 0.0f);
	UINT lightCount = 0;

	XMFLOAT3 ambientColor = XMFLOAT3(0.12f, 0.12f, 0.12f);
	float shininess = 32.0f;

	XMFLOAT3 specularColor = XMFLOAT3(1.0f, 1.0f, 1.0f);
	float padding = 0.0f;
};

static_assert(sizeof(LightData) % 16 == 0, "[경고] LightData must be 16-byte aligned for HLSL.");
static_assert(sizeof(LightBuffer) % 16 == 0, "[경고] LightBuffer must be 16-byte aligned for HLSL.");
