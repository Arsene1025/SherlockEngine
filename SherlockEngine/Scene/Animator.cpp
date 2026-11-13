#include "pch.h"
#include "Scene/Behaviour.h"
#include "Scene/GameObject.h"
#include "Scene/Scene.h"
#include "Scene/SkinPose.h"
#include "Core/AssetManager.h"
#include "Core/Log.h"
#include "Graphics/Mesh.h"
#include "Graphics/Model.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

using namespace DirectX;

// 애니메이션 재생: 소유 오브젝트의 모델(glTF)이 가진 AnimationClip 을 샘플링해 노드 트랜스폼에 적용함.
//
// 두 가지 모드가 있음.
//
//   (1) 스킨 모드: 소유 오브젝트나 그 자손의 메시가 skin 을 가진 노드에서 왔을 때. 클립이 노드 여러 개(본)를 움직여도 됨.
//       Animator 는 모델 루트 오브젝트에 하나만 붙이면 됨 (유니티와 같음): 같은 모델의 스킨 메시 오브젝트(MasterChief 는 팔·가슴·헬멧·다리 4개)를
//       자손에서 찾아 노드 월드를 한 번만 계산하고 메시마다 팔레트만 만듦. 메시 오브젝트 자신에 붙여도 되고(그 오브젝트만 구동),
//       자손 중 자기 Animator 를 가진 오브젝트가 있으면 그 아래는 그 Animator 가 맡음.
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
		m_skinMode = false;
		m_model = nullptr;
		m_targets.clear();
		Scene* scene = GetContext().scene;
		if (scene == nullptr || scene->GetAssets() == nullptr) return;
		const MeshSource* source = FindModelSource(*scene, GetOwner());   // 소유 오브젝트의 메시, 없으면 자손의 메시에서 모델을 찾음
		if (source == nullptr)
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

		// 스킨 모드: 소유 오브젝트와 자손 중 이 모델의 skin 메시가 있으면 계층 전체를 계산함
		m_model = model;
		m_rest = model->nodes;
		m_current = m_rest;
		m_global.assign(m_rest.size(), XMFLOAT4X4());
		m_targetScratch.clear();
		CollectSkinTargets(GetOwner(), m_targetScratch);
		if (!m_targetScratch.empty())
		{
			m_skinMode = true;
			m_clip = found;
			Log::Info("Animator '%s': 클립 '%s' 스킨 재생 (길이 %.2f초, 채널 %zu개, 스킨 메시 오브젝트 %zu개)", GetOwner().GetName().c_str(), found->name.c_str(),
				found->duration, found->channels.size(), m_targetScratch.size());
			m_targetScratch.clear();
			return;
		}
		m_model = nullptr;
		if (scene->GetMeshSource(GetOwner().GetMesh()) != source)   // 단일 노드 모드는 소유 오브젝트 자신의 모델 메시에만 적용함 (자손의 모델 클립을 소유자에게 입히지 않음)
		{
			Log::Warn("Animator '%s': 스킨 메시가 없고 소유 오브젝트의 메시도 이 모델에서 온 것이 아니어서 재생할 수 없음", GetOwner().GetName().c_str());
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

		if (m_skinMode)
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
	// 스킨 메시 하나(Mesh 단위)의 고정 정보. 메시는 씬 수명 동안 주소가 바뀌지 않아 Mesh* 로 캐시함.
	struct SkinTarget
	{
		const ModelSkin* skin = nullptr;   // 비면 이 모델의 스킨 메시가 아님
		XMFLOAT4X4 meshRest;               // 메시 노드의 기본 자세 월드 (조인트 월드와 같은 좌표계, options.scale 은 반영하지 않음)
	};

	// 소유 오브젝트의 메시, 없으면 자손의 메시 중 모델에서 온 첫 번째 출처. 자기 Animator 를 가진 자손 아래는 보지 않음.
	static const MeshSource* FindModelSource(const Scene& scene, GameObject& object)
	{
		const MeshSource* source = scene.GetMeshSource(object.GetMesh());
		if (source != nullptr && source->type == MeshSource::Type::Model) return source;
		for (GameObject* child : object.GetChildren())
		{
			if (child->GetBehaviour<Animator>() != nullptr) continue;
			if (const MeshSource* found = FindModelSource(scene, *child)) return found;
		}
		return nullptr;
	}

	// 오브젝트가 이 Animator 의 모델에 속한 스킨 메시를 그리면 그 정보를, 아니면 nullptr. 결과는 메시별로 캐시함.
	const SkinTarget* ResolveTarget(const GameObject& object)
	{
		const Mesh* mesh = object.GetMesh();
		if (mesh == nullptr || !mesh->IsSkinned()) return nullptr;
		auto found = m_targets.find(mesh);
		if (found == m_targets.end())
		{
			SkinTarget target;
			Scene* scene = GetContext().scene;
			const MeshSource* source = scene->GetMeshSource(mesh);
			if (source != nullptr && source->type == MeshSource::Type::Model && scene->GetAssets()->GetModel(source->modelPath) == m_model)
			{
				int meshNode = -1;
				target.skin = FindMeshSkin(*m_model, source->meshIndex, meshNode);   // 편집 모드의 레스트 포즈(ApplyRestSkinPoses)와 같은 인스턴스를 고름
				if (target.skin != nullptr) XMStoreFloat4x4(&target.meshRest, meshNode >= 0 ? ComputeNodeGlobal(m_rest, meshNode) : XMMatrixIdentity());
			}
			found = m_targets.emplace(mesh, target).first;
		}
		return found->second.skin != nullptr ? &found->second : nullptr;
	}

	// 이 Animator 가 구동할 스킨 메시 오브젝트를 모음: 소유 오브젝트와 자손. 자기 Animator 를 가진 자손과 그 아래는 그 Animator 의 몫이라 건너뜀
	// (오브젝트마다 구동하는 Animator 가 정확히 하나). 오브젝트가 삭제되거나 계층이 바뀔 수 있어서 프레임마다 다시 모으고 포인터를 오래 들고 있지 않음.
	void CollectSkinTargets(GameObject& object, std::vector<GameObject*>& out)
	{
		if (ResolveTarget(object) != nullptr) out.push_back(&object);
		for (GameObject* child : object.GetChildren())
		{
			if (child->GetBehaviour<Animator>() != nullptr) continue;
			CollectSkinTargets(*child, out);
		}
	}

	// 스킨 모드 한 프레임: 시간 t 의 노드 TRS → 조인트 월드 → 팔레트
	void UpdateSkin(float t)
	{
		m_current = m_rest;
		//node의 TRS구하기
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

		// 노드 월드(evalOrder 루프)와 아래 팔레트·포즈 식은 Scene/SkinPose.cpp 의 EvaluateNodeGlobals·BuildSkinPalette 에 있음 (식은 그대로 옮김).
		// 편집 모드의 기본 자세(ApplyRestSkinPoses)도 같은 식을 써서, 재생을 시작하기 전과 후의 스킨 모양이 같은 식에서 나옴.

		// 스킨 팔레트 / 스켈레톤 포즈 계산
		// 행벡터 규약(DirectXMath): v' = v · M0 · M1 · ... 이라 왼쪽 행렬이 먼저 적용된다.
		//
		// palette[j] = 역바인드[j] · 조인트 월드[j] · (메시 노드 기본 월드)⁻¹
		//   1) inverseBind[j]  : 메시 정점(바인드 포즈 메시 공간)을 조인트 j 의 로컬 공간으로 보낸다.
		//                        "바인드 포즈에서 조인트 월드 행렬의 역행렬"이다.
		//						  joint에 영향을 받는 정점들을 조인트 로컬로 보내서 조인트의 움직임이 정점에 반영되도록 한다.
		//						  해당 과정을 거치면 정점이 조인트로부터 얼마나 떨저어졌는지 상대 좌표를 알 수 있음
		//						  이 상태에서 조인트월드를 곱하면 조인트가 움직인 만큼 정점도 움직임(조인트 로컬 ㄷ좌표가 그대로 유지되니까)
		//   2) jointWorld[j]   : 조인트 로컬 공간을 현재 애니메이션 포즈의 월드(씬) 공간으로 보낸다.
		//                        (m_global = 로컬 TRS · 부모 월드, 위 evalOrder 루프에서 계산됨)
		//                       "바인드 포즈 대비 이 조인트가 움직인 변화량" 계산.
		//                        바인드 포즈 그대로면 inverseBind · jointWorld = 단위행렬이라 정점이 제자리에 남는다.
		// 
		//   3) inverseMesh     : 현재 엔진에서는 그릴 때 pre를 곱해서 메시를 씬 공간으로 보냄.
		//						  하지만 스킨 메시는 이미 inverseBind * jointWorld를 거친 시점에 이미 씬 공간에 있음.
		//						  ->jointWorld가 씬 기준이기 때문
		//                        그런 상태에서 pre가 또 곱해지면 씬 공간으로 두 번 감.
		//                        그레서 pre의 역행렬을 넣어서 상쇄.
		//                        
		// 정점은 자기 영향 조인트 인덱스(JOINTS_0)와 가중치(WEIGHTS_0)를 갖고 있고, 정점 셰이더가 이 배열에서
		// 해당 행렬을 골라 skin = Σ 가중치 · 정점 · palette[인덱스] 로 블렌딩한다.
		// SetSkinPalette 로 오브젝트에 넘기면 렌더러가 GPU 버퍼(구조체 버퍼 등)로 올려 셰이더가 읽는다.
		// 구동할 스킨 메시 오브젝트마다 팔레트를 만듦. 스킨이 같으면 노드 월드는 한 번만 계산함 (MasterChief 는 4개 메시가 한 스킨을 씀).
		m_targetScratch.clear();
		CollectSkinTargets(GetOwner(), m_targetScratch);
		const ModelSkin* evaluated = nullptr;
		for (GameObject* object : m_targetScratch)
		{
			const SkinTarget* target = ResolveTarget(*object);
			if (target->skin != evaluated)
			{
				EvaluateNodeGlobals(m_current, *target->skin, m_global);
				evaluated = target->skin;
			}
			std::vector<XMFLOAT4X4> palette;
			GameObject::SkeletonPose pose;
			BuildSkinPalette(*target->skin, m_global, target->meshRest, palette, pose);
			object->SetSkinPalette(std::move(palette));
			object->SetSkeletonPose(std::move(pose));
		}
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
	bool m_skinMode = false;                 // false 면 단일 노드 모드
	std::unordered_map<const Mesh*, SkinTarget> m_targets;   // 메시별 스킨·메시 노드 기본 월드 캐시 (Start 에서 비움)
	std::vector<GameObject*> m_targetScratch;   // CollectSkinTargets 결과. 프레임 안에서만 씀 (오래 들고 있지 않음)
	std::vector<ModelNode> m_rest;           // 모델의 기본 노드 TRS
	std::vector<ModelNode> m_current;        // 이번 프레임에 채널이 덮은 노드 TRS
	std::vector<XMFLOAT4X4> m_global;         // 노드 월드 (UpdateSkin 이 evalOrder 의 노드만 채움)

	const AnimationClip* m_clip = nullptr;   // AssetManager 캐시의 모델이 소유함. Start 마다 다시 찾음
	ModelNode m_base;                        // 채널이 덮지 않는 성분의 기본값 (노드의 원래 TRS)
	float m_time = 0.0f;
};
SHERLOCK_BEHAVIOUR(Animator)
