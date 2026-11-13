#include "pch.h"
#include "Scene/Behaviour.h"
#include "Scene/GameObject.h"
#include "Scene/Scene.h"
#include "Core/AssetManager.h"
#include "Core/Log.h"
#include "Graphics/Model.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

// 애니메이션 재생: 소유 오브젝트의 모델(glTF)이 가진 AnimationClip 을 샘플링해 노드 트랜스폼에 적용함.
//
// 두 가지 모드가 있음.
//
//   (1) 스킨 모드: 소유 오브젝트의 메시가 skin 을 가진 노드에서 왔을 때. 클립이 노드 여러 개(본)를 움직여도 됨.
//       Scene::AddModel 은 노드를 월드 행렬이 굳은 인스턴스로 펴서 GameObject 를 만들기 때문에 본에는 GameObject 가 없음.
//       그래서 이 Animator 가 모델의 노드 표(Model::nodes)에서 직접 계층을 계산함: 노드마다 기본 TRS 를 채널로 덮어
//       쓰고, 부모 → 자식 순으로 곱해 조인트 월드를 구한 뒤, 조인트 행렬 팔레트를 GameObject::SetSkinPalette 로 올림.
//       Renderer 가 그 팔레트로 정점 셰이더에서 스키닝함. 오브젝트의 배치(Transform)는 그대로 모델 전체에 적용됨.
//   (2) 단일 노드 모드: 스킨이 없는 모델. 클립의 모든 채널이 같은 노드 하나를 가리키고 그 노드에 부모가 없을 때만 재생하고,
//       아니면 경고 로그를 남기고 멈춤 (노드 계층을 GameObject 로 펴지 않았으므로). 샘플링 함수는 두 모드가 같이 씀.
//
// 적용 방법: 샘플링한 S·R·T 를 Transform 의 pre 행렬(노드 고정 행렬)에 넣음. 에디터에서 정한 위치·회전·스케일(배치)은
// 그대로 pre 뒤에 곱해지므로 배치와 애니메이션이 충돌하지 않음. 모델을 불러올 때 쓴 루트 스케일(options.scale)은
// 기본값 1 이라고 가정함 (스킨 모드도 같음).
//
// 클립 데이터는 Model(AssetManager 캐시)에 있고, 씬 파일에는 clip 이름·loop·speed 만 저장됨 (Reflect).
class Animator : public Behaviour
{
public:
	const char* GetTypeName() const override;

	void Reflect(PropertyVisitor& v) override
	{
		v.String("clip", clip);
		v.Bool("loop", loop);
		v.Float("speed", speed, 0.05f);
	}

	void Start() override
	{
		m_clip = nullptr;
		m_time = 0.0f;
		m_skin = nullptr;
		m_model = nullptr;
		Scene* scene = GetContext().scene;
		if (scene == nullptr || scene->GetAssets() == nullptr) return;
		const MeshSource* source = scene->GetMeshSource(GetOwner().GetMesh());
		if (source == nullptr || source->type != MeshSource::Type::Model)
		{
			Log::Warn("Animator '%s': 모델에서 온 메시가 아니어서 재생할 수 없음", GetOwner().GetName().c_str());
			return;
		}
		const Model* model = scene->GetAssets()->GetModel(source->modelPath);
		if (model == nullptr || model->animations.empty())
		{
			Log::Warn("Animator '%s': 모델에 애니메이션이 없음", GetOwner().GetName().c_str());
			return;
		}

		const AnimationClip* found = nullptr;
		for (const AnimationClip& candidate : model->animations)
		{
			if (clip.empty() || candidate.name == clip) { found = &candidate; break; }
		}
		if (found == nullptr)
		{
			Log::Warn("Animator '%s': 클립 '%s' 을 찾을 수 없음", GetOwner().GetName().c_str(), clip.c_str());
			return;
		}

		// 스킨 모드: 이 메시를 그리는 노드가 skin 을 가지면 계층 전체를 계산함
		for (const ModelInstance& instance : model->instances)
		{
			if (instance.meshIndex != source->meshIndex || instance.skin < 0 || static_cast<size_t>(instance.skin) >= model->skins.size()) continue;
			const ModelSkin& candidate = model->skins[static_cast<size_t>(instance.skin)];
			if (candidate.joints.empty() || candidate.inverseBind.size() != candidate.joints.size()) continue;
			m_skin = &candidate;
			m_meshNode = instance.node;
			break;
		}
		if (m_skin != nullptr)
		{
			m_model = model;
			m_clip = found;
			m_rest = model->nodes;
			m_current = m_rest;
			m_global.assign(m_rest.size(), XMFLOAT4X4());
			// 메시 노드의 기본 자세 월드 (조인트 월드와 같은 좌표계, options.scale 은 반영하지 않음)
			XMStoreFloat4x4(&m_meshRest, m_meshNode >= 0 ? NodeGlobal(m_rest, m_meshNode) : XMMatrixIdentity());
			Log::Info("Animator '%s': 클립 '%s' 스킨 재생 (길이 %.2f초, 채널 %zu개, 조인트 %zu개)", GetOwner().GetName().c_str(), found->name.c_str(),
				found->duration, found->channels.size(), m_skin->joints.size());
			return;
		}

		// 단일 노드 검사
		const uint32_t node = found->channels.empty() ? 0 : found->channels.front().node;
		for (const AnimationChannel& channel : found->channels)
		{
			if (channel.node != node)
			{
				Log::Warn("Animator '%s': 클립 '%s' 이 노드 여러 개를 움직임 (아직 지원하지 않음)", GetOwner().GetName().c_str(), found->name.c_str());
				return;
			}
		}
		if (found->channels.empty() || node >= model->nodes.size() || model->nodes[node].parent >= 0)
		{
			Log::Warn("Animator '%s': 클립 '%s' 의 대상 노드가 루트가 아님 (아직 지원하지 않음)", GetOwner().GetName().c_str(), found->name.c_str());
			return;
		}
		m_clip = found;
		m_base = model->nodes[node];
		Log::Info("Animator '%s': 클립 '%s' 재생 (길이 %.2f초, 채널 %zu개)", GetOwner().GetName().c_str(), found->name.c_str(), found->duration, found->channels.size());
	}

	void Update(float dt) override
	{
		if (m_clip == nullptr) return;
		m_time += dt * speed;
		const float duration = m_clip->duration;
		float t = m_time;
		if (duration > 0.0f)
		{
			if (loop) { t = fmodf(t, duration); if (t < 0.0f) t += duration; }
			else t = (std::clamp)(t, 0.0f, duration);
		}

		if (m_skin != nullptr)
		{
			UpdateSkin(t);
			return;
		}

		XMFLOAT3 translation = m_base.translation;
		XMFLOAT4 rotation = m_base.rotation;
		XMFLOAT3 scale = m_base.scale;
		for (const AnimationChannel& channel : m_clip->channels)
		{
			float value[4];
			Sample(channel, t, value);
			switch (channel.path)
			{
			case AnimationPath::Translation: translation = XMFLOAT3(value[0], value[1], value[2]); break;
			case AnimationPath::Rotation: rotation = XMFLOAT4(value[0], value[1], value[2], value[3]); break;
			case AnimationPath::Scale: scale = XMFLOAT3(value[0], value[1], value[2]); break;
			default: break;
			}
		}

		const XMVECTOR q = XMQuaternionNormalize(XMLoadFloat4(&rotation));
		GetOwner().GetTransform().SetPreTransform(
			XMMatrixScaling(scale.x, scale.y, scale.z) * XMMatrixRotationQuaternion(q) * XMMatrixTranslation(translation.x, translation.y, translation.z));
	}

	std::string clip;       // 비우면 첫 클립
	bool loop = true;
	float speed = 1.0f;

private:
	// 기본 자세 노드 표에서 노드 n 의 월드 (재생과 무관, Start 에서 한 번)
	static XMMATRIX NodeGlobal(const std::vector<ModelNode>& nodes, int n)
	{
		XMMATRIX world = XMMatrixIdentity();
		for (size_t steps = 0; n >= 0 && static_cast<size_t>(n) < nodes.size() && steps <= nodes.size(); ++steps)
		{
			const ModelNode& node = nodes[static_cast<size_t>(n)];
			world = world * XMMatrixScaling(node.scale.x, node.scale.y, node.scale.z) * XMMatrixRotationQuaternion(XMQuaternionNormalize(XMLoadFloat4(&node.rotation)))
				* XMMatrixTranslation(node.translation.x, node.translation.y, node.translation.z);
			n = node.parent;
		}
		return world;
	}

	// 스킨 모드 한 프레임: 시간 t 의 노드 TRS → 조인트 월드 → 팔레트
	void UpdateSkin(float t)
	{
		m_current = m_rest;
		for (const AnimationChannel& channel : m_clip->channels)
		{
			if (channel.node >= m_current.size()) continue;
			float value[4];
			Sample(channel, t, value);
			ModelNode& node = m_current[channel.node];
			switch (channel.path)
			{
			case AnimationPath::Translation: node.translation = XMFLOAT3(value[0], value[1], value[2]); break;
			case AnimationPath::Rotation: node.rotation = XMFLOAT4(value[0], value[1], value[2], value[3]); break;
			case AnimationPath::Scale: node.scale = XMFLOAT3(value[0], value[1], value[2]); break;
			default: break;
			}
		}

		// 노드 월드: 임포트 때 정해 둔 순서(부모가 항상 앞)로 한 번 훑음. 부모 번호가 범위 밖이면 루트로 봄 (임포터와 같은 규칙).
		for (const uint32_t n : m_skin->evalOrder)
		{
			const ModelNode& node = m_current[n];
			const XMVECTOR q = XMQuaternionNormalize(XMLoadFloat4(&node.rotation));
			XMMATRIX world = XMMatrixScaling(node.scale.x, node.scale.y, node.scale.z) * XMMatrixRotationQuaternion(q)
				* XMMatrixTranslation(node.translation.x, node.translation.y, node.translation.z);
			if (node.parent >= 0 && static_cast<size_t>(node.parent) < m_current.size()) world = world * XMLoadFloat4x4(&m_global[static_cast<size_t>(node.parent)]);
			XMStoreFloat4x4(&m_global[n], world);
		}

		// palette[j] = 역바인드[j] · 조인트 월드[j] · (메시 노드 기본 월드)⁻¹. 마지막 항은 오브젝트의 pre(메시 노드 월드)를
		// 정점 셰이더 결과에서 상쇄함 (glTF: 스킨 메시의 노드 변환은 무시).
		const XMMATRIX inverseMesh = XMMatrixInverse(nullptr, XMLoadFloat4x4(&m_meshRest));
		std::vector<XMFLOAT4X4> palette(m_skin->joints.size());
		GameObject::SkeletonPose pose;
		pose.jointToObject.resize(palette.size());
		pose.parent = m_skin->jointParent;
		for (size_t j = 0; j < palette.size(); ++j)
		{
			const XMMATRIX inverseBind = XMLoadFloat4x4(&m_skin->inverseBind[j]);
			const XMMATRIX jointWorld = XMLoadFloat4x4(&m_global[m_skin->joints[j]]);
			XMStoreFloat4x4(&palette[j], inverseBind * jointWorld * inverseMesh);
			XMStoreFloat4x4(&pose.jointToObject[j], jointWorld * inverseMesh);
		}
		GetOwner().SetSkinPalette(std::move(palette));
		GetOwner().SetSkeletonPose(std::move(pose));
	}

	// 채널을 시간 t 에서 샘플링해 value 에 성분 수(3 또는 4)만큼 씀. t 는 [times.front(), times.back()] 로 자름.
	static void Sample(const AnimationChannel& channel, float t, float* value)
	{
		const size_t components = channel.path == AnimationPath::Rotation ? 4 : 3;
		const size_t count = channel.times.size();
		const bool cubic = channel.interpolation == AnimationInterpolation::CubicSpline;
		// 키 k 의 값: 보통은 values[k*c], CubicSpline 은 [입력 탄젠트, 값, 출력 탄젠트] 라 가운데
		auto valueAt = [&](size_t k) { return &channel.values[(cubic ? k * 3 + 1 : k) * components]; };
		auto copy = [&](const float* src) { for (size_t c = 0; c < components; ++c) value[c] = src[c]; };

		if (t <= channel.times.front() || count == 1) { copy(valueAt(0)); return; }
		if (t >= channel.times.back()) { copy(valueAt(count - 1)); return; }

		// times[k] <= t < times[k+1] 인 k
		const size_t next = static_cast<size_t>(std::upper_bound(channel.times.begin(), channel.times.end(), t) - channel.times.begin());
		const size_t k = next - 1;
		const float t0 = channel.times[k], t1 = channel.times[next];
		const float span = t1 - t0;
		const float u = span > 0.0f ? (t - t0) / span : 0.0f;

		if (channel.interpolation == AnimationInterpolation::Step) { copy(valueAt(k)); return; }

		if (cubic)
		{
			// glTF 3차 에르미트: 구간 길이를 곱한 탄젠트 사용
			const float* v0 = valueAt(k);
			const float* b0 = &channel.values[(k * 3 + 2) * components];   // k 의 출력 탄젠트
			const float* v1 = valueAt(next);
			const float* a1 = &channel.values[(next * 3) * components];    // next 의 입력 탄젠트
			const float u2 = u * u, u3 = u2 * u;
			for (size_t c = 0; c < components; ++c)
				value[c] = (2 * u3 - 3 * u2 + 1) * v0[c] + (u3 - 2 * u2 + u) * span * b0[c] + (-2 * u3 + 3 * u2) * v1[c] + (u3 - u2) * span * a1[c];
			if (channel.path == AnimationPath::Rotation)
			{
				XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(value), XMQuaternionNormalize(XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(value))));
			}
			return;
		}

		const float* v0 = valueAt(k);
		const float* v1 = valueAt(next);
		if (channel.path == AnimationPath::Rotation)
		{
			XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(value), XMQuaternionSlerp(XMVectorSet(v0[0], v0[1], v0[2], v0[3]), XMVectorSet(v1[0], v1[1], v1[2], v1[3]), u));
			return;
		}
		for (size_t c = 0; c < components; ++c) value[c] = v0[c] + (v1[c] - v0[c]) * u;
	}

	// 스킨 모드 상태
	const Model* m_model = nullptr;
	const ModelSkin* m_skin = nullptr;       // 비면 단일 노드 모드
	int m_meshNode = -1;                     // 스킨 메시를 단 노드
	std::vector<ModelNode> m_rest;           // 모델의 기본 노드 TRS
	std::vector<ModelNode> m_current;        // 이번 프레임에 채널이 덮은 노드 TRS
	std::vector<XMFLOAT4X4> m_global;         // 노드 월드 (UpdateSkin 이 evalOrder 의 노드만 채움)
	XMFLOAT4X4 m_meshRest;

	const AnimationClip* m_clip = nullptr;   // AssetManager 캐시의 모델이 소유함. Start 마다 다시 찾음
	ModelNode m_base;                        // 채널이 덮지 않는 성분의 기본값 (노드의 원래 TRS)
	float m_time = 0.0f;
};
SHERLOCK_BEHAVIOUR(Animator)
