#pragma once
#include <string>
#include "Scene/Transform.h"

class Mesh;

// 씬의 물체 하나. Transform 값 하나와 그릴 메시에 대한 참조.
//
// GPU 자원은 갖지 않는다. 메시 → 정점/인덱스 버퍼 대응은 Renderer의 캐시가 관리한다.
// (rendering-analysis D3: Scene은 CPU 데이터, GPU 자원은 Renderer 측 캐시)
// 메시는 Scene이 unique_ptr로 소유하므로 포인터가 안정적이다.
class GameObject
{
public:
	GameObject() = default;
	GameObject(const Mesh* mesh, const char* name) : mesh(mesh), name(name ? name : "") {}

	Transform& GetTransform() { return transform; }
	const Transform& GetTransform() const { return transform; }

	const Mesh* GetMesh() const { return mesh; }
	void SetMesh(const Mesh* value) { mesh = value; }

	const std::string& GetName() const { return name; }

private:
	Transform transform;
	const Mesh* mesh = nullptr;
	std::string name;
};
