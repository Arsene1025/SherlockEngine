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
