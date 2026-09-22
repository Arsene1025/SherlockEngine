#pragma once
#include <memory>
#include <vector>
#include <DirectXMath.h>
#include "Scene/GameObject.h"
#include "Scene/Light.h"

class Mesh;

// 씬 = 그릴 것들의 CPU 쪽 데이터.
//
// 오브젝트 목록, 조명 목록, 환경(주변광·클리어 색). GPU 자원은 없다.
// Renderer는 Render(const Scene&, const Camera&)로 이것을 입력으로 받아 그리기만 한다.
// 예전에는 Renderer가 구 하나와 조명 배열과 ImGui 패널을 직접 들고 있었다(D3).
//
// 메시는 unique_ptr로 소유해 포인터가 흔들리지 않게 한다. GameObject가 그 포인터를
// 들고, Renderer의 GPU 캐시도 그 포인터를 키로 쓴다.
class Scene
{
public:
	Scene();
	~Scene();

	Mesh* AddMesh(Mesh&& mesh);
	GameObject& AddObject(const Mesh* mesh, const char* name);

	std::vector<GameObject>& GetObjects() { return m_objects; }
	const std::vector<GameObject>& GetObjects() const { return m_objects; }

	// 최대 MAX_LIGHTS개. 넘치는 것은 Renderer가 잘라낸다.
	std::vector<LightData>& GetLights() { return m_lights; }
	const std::vector<LightData>& GetLights() const { return m_lights; }

	DirectX::XMFLOAT3 ambientColor = DirectX::XMFLOAT3(0.12f, 0.12f, 0.12f);
	DirectX::XMFLOAT3 specularColor = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);   // 4단계에서 Material로 이동
	float shininess = 32.0f;                                                   // 4단계에서 Material로 이동
	float clearColor[4] = { 0.1f, 0.1f, 0.3f, 1.0f };

private:
	std::vector<std::unique_ptr<Mesh>> m_meshes;
	std::vector<GameObject> m_objects;
	std::vector<LightData> m_lights;
};
