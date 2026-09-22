#include "pch.h"
#include "Scene/Scene.h"
#include "Graphics/Mesh.h"   // unique_ptr<Mesh> 소멸에 완전한 타입 필요

Scene::Scene()
{
}

Scene::~Scene()
{
}

Mesh* Scene::AddMesh(Mesh&& mesh)
{
	m_meshes.push_back(std::make_unique<Mesh>(std::move(mesh)));
	return m_meshes.back().get();
}

GameObject& Scene::AddObject(const Mesh* mesh, const char* name)
{
	m_objects.emplace_back(mesh, name);
	return m_objects.back();
}
