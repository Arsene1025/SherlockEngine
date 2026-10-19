#include "pch.h"
#include "Scene/Script.h"
#include "Scene/Scene.h"
#include "Core/Input.h"

Input& Script::GetInput()
{
	return *GetContext().input;
}

Scene& Script::GetScene()
{
	return *GetContext().scene;
}

GameObject* Script::Find(const std::string& name)
{
	return GetScene().FindObject(name);
}

// 2026-10-15
GameObject* Script::Instantiate(const std::string& prefabPath, const DirectX::XMFLOAT3& position)
{
	return GetScene().Instantiate(prefabPath, position);
}

GameObject* Script::Instantiate(const GameObject& source)
{
	return GetScene().Instantiate(source);
}

void Script::Destroy(GameObject* object)
{
	GetScene().Destroy(object);
}

GameObject* Script::FindById(uint64_t id)
{
	return GetScene().FindById(id);
}
