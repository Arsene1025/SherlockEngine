#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct Model;

// 에셋 관리자 (10단계): 경로 → CPU 리소스 캐시.
//
// 경로는 Assets/ 기준 상대 경로("Models\\DamagedHelmet\\DamagedHelmet.glb")이고 Paths::GetAssetPath 가
// exe 옆 또는 소스 트리로 푼다. 같은 경로를 두 번 요청하면 두 번째는 캐시에서 온다 — 씬을 전환할 때
// Sponza 를 다시 파싱하지 않는다. GPU 자원은 여기 없다: 그것은 Renderer 캐시(메시·재질·텍스처)의 몫이고,
// 이 계층은 "파일 → 메모리" 만 맡는다. 텍스처 파일은 Renderer 가 재질 색공간을 알아야 디코딩할 수 있어
// 아직 여기로 오지 않았다 (Scene 이미지 바이트 경로, 9단계).
class AssetManager
{
public:
	// unique_ptr<Model> 의 소멸에 완전한 타입이 필요하므로 소멸자는 .cpp 에 (헤더는 Model 을 전방 선언만 한다).
	AssetManager();
	~AssetManager();

	struct Stats
	{
		uint32_t modelLoads = 0;      // 실제 파싱 횟수
		uint32_t modelHits = 0;       // 캐시 적중
		uint32_t fileLoads = 0;
		uint32_t fileHits = 0;
		double totalLoadMs = 0.0;
		size_t cachedBytes = 0;       // 모델 정점·인덱스·이미지 + 파일 바이트
	};

	// 실패하면 nullptr (로그에 이유). 반환된 포인터는 Unload/Clear 전까지 유효하다.
	const Model* GetModel(const std::wstring& relativePath);
	// 임의 파일의 바이트. 실패하면 nullptr.
	const std::vector<uint8_t>* GetFile(const std::wstring& relativePath);

	bool IsModelCached(const std::wstring& relativePath) const;
	void UnloadModel(const std::wstring& relativePath);
	void Clear();

	const Stats& GetStats() const { return m_stats; }
	size_t GetModelCount() const { return m_models.size(); }
	size_t GetFileCount() const { return m_files.size(); }
	std::vector<std::wstring> GetModelPaths() const;

private:
	static std::wstring Normalize(const std::wstring& path);   // 소문자 + 구분자 통일 (캐시 키)
	static size_t EstimateBytes(const Model& model);

	std::unordered_map<std::wstring, std::unique_ptr<Model>> m_models;
	std::unordered_map<std::wstring, std::vector<uint8_t>> m_files;
	Stats m_stats;
};
