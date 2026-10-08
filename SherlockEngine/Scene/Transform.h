#pragma once
#include "Core/EngineApi.h"   // 2026-10-08: SHERLOCK_API (엔진 DLL 내보내기)
#include <DirectXMath.h>

// 오브젝트 하나의 로컬 자세: 위치·오일러 회전·스케일 (+ 모델 노드의 고정 행렬).
//
// 11-F단계: 이 값은 언제나 "부모 기준"이다. 부모가 없으면 로컬 = 월드. 월드 행렬은 GameObject::GetWorldMatrix 가
// 부모를 따라 올라가며 곱해 만들고, Transform 은 부모를 모른다 (유니티의 localPosition/localRotation/localScale 에 해당).
// 이전에는 GetWorldMatrix 라는 이름으로 이 로컬 행렬을 돌려줬는데, 계층이 생기면서 이름이 거짓이 되어 GetLocalMatrix 로 바꿨다.
class SHERLOCK_API Transform
{
public:
	Transform();

	void SetPosition(const DirectX::XMFLOAT3& value);
	void SetPosition(float x, float y, float z);
	void SetRotation(const DirectX::XMFLOAT3& value); 
	void SetRotation(float x, float y, float z);
	void SetScale(const DirectX::XMFLOAT3& value);
	void SetScale(float x, float y, float z);

	const DirectX::XMFLOAT3& GetPosition() const { return position; }
	const DirectX::XMFLOAT3& GetRotation() const { return rotation; }
	const DirectX::XMFLOAT3& GetScale() const { return scale; }

	// 9단계: S·R·T 앞에 곱하는 고정 행렬. 모델 노드의 월드 변환(임의 행렬)을 오일러 각으로 분해하지 않고
	// 그대로 보관함. local = pre × S × R × T. 인스턴스 전체를 옮기려면 position/rotation/scale 을 쓰면 됨.
	void SetPreTransform(const DirectX::XMMATRIX& value);
	DirectX::XMMATRIX GetPreTransform() const { return DirectX::XMLoadFloat4x4(&preTransform); }
	void ClearPreTransform() { hasPreTransform = false; }
	bool HasPreTransform() const { return hasPreTransform; }

	// 부모 기준 행렬 = pre × S × R × T. 월드 행렬이 필요하면 GameObject::GetWorldMatrix 를 쓸 것.
	DirectX::XMMATRIX GetLocalMatrix() const;
	// 회전만 (오일러 → 행렬). GetForward/Right/Up 과 GameObject 의 월드 회전 계산이 씀.
	DirectX::XMMATRIX GetRotationMatrix() const;
	// 역방향: 로컬 행렬(pre 포함)을 S·R·T 로 분해해 저장함. 기즈모와 재부모화가 씀.
	// pre 가 있으면 pre⁻¹ 을 먼저 곱함. 분해가 불가능하면(스케일 0 등) false 를 돌려주고 값을 바꾸지 않음.
	bool SetLocalMatrix(const DirectX::XMMATRIX& local);

	// 회전 행렬(순수 회전, 행벡터 관례) → RollPitchYaw 오일러 각. XMMatrixRotationRollPitchYaw(x, y, z) 의 역함수.
	// pitch 가 ±90° 근처(짐벌락)이면 yaw 를 0 으로 두고 roll 에 몰아줌. 11-F단계에 Editor 에서 옮겨 옴 (재부모화·FollowTarget 도 씀).
	static DirectX::XMFLOAT3 EulerFromRotationMatrix(const DirectX::XMFLOAT4X4& m);

	// ---- 11-C단계: 스크립트가 Update 에서 호출하는 조작 함수 (유니티 Transform 의 Translate/Rotate/LookAt) ----
	// 모두 로컬(부모 기준) 값이다. 부모가 있는 오브젝트를 월드 기준으로 옮기려면 GameObject::SetWorldPosition 등을 쓴다.
	void Translate(float x, float y, float z);                       // 부모 축 기준 이동
	void Translate(const DirectX::XMFLOAT3& delta);
	void TranslateLocal(float forward, float right, float up);       // 자기 회전 기준 이동 (앞/오른쪽/위)
	void Rotate(float pitch, float yaw, float roll);                 // 오일러 각 누적 (라디안)
	void LookAt(const DirectX::XMFLOAT3& target);                    // target(부모 기준 좌표) 을 향하도록 yaw/pitch 를 설정하고 roll 은 0 (Camera::SetLookAt 과 같은 규약)
	DirectX::XMVECTOR GetForward() const;                            // 회전만 적용한 부모 기준 방향 (+Z 가 앞)
	DirectX::XMVECTOR GetRight() const;
	DirectX::XMVECTOR GetUp() const;

private:
	DirectX::XMFLOAT3 position;
	DirectX::XMFLOAT3 rotation;
	DirectX::XMFLOAT3 scale;
	DirectX::XMFLOAT4X4 preTransform;
	bool hasPreTransform = false;
};
