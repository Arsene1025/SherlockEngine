#pragma once
#include "Scene/Transform.h"

class GameObject
{
public:
	Transform& GetTransform() { return transform; }
	const Transform& GetTransform() const { return transform; }

private:
	Transform transform;
};
