#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>
#include <DirectXMath.h>
#include "Graphics/Model.h"

// 포맷 중립 임포터 공용 도구. glTF·FBX 임포터가 함께 씀.
// 임포터의 계약: "엔진 좌표계(왼손, CW 앞면)로 변환된 Model 을 채워 돌려준다."
namespace ModelImport
{
	std::string ToUtf8(const std::wstring& wide);
	std::wstring ToWide(const std::string& utf8);

	// 파일 전체를 읽음. 비어 있거나 열 수 없으면 false.
	bool ReadWholeFile(const std::wstring& path, std::vector<uint8_t>& out);

	// 파일 이름(확장자 제외). 이미지·재질 이름의 접두사로 씀.
	std::string ModelNameFromPath(const std::wstring& path);

	// 경로의 폴더 부분 (끝의 구분자 포함). 외부 이미지·.bin 경로를 만들 때 씀.
	std::wstring DirectoryFromPath(const std::wstring& path);

	// 인스턴스 월드 변환으로 옮긴 메시 AABB 를 합쳐 Model::bounds 를 만듦.
	class BoundsAccumulator
	{
	public:
		void Add(const Bounds& local, DirectX::FXMMATRIX world);
		void ApplyTo(Model& model) const;   // 한 번도 Add 하지 않았다면 건드리지 않음
	private:
		DirectX::XMFLOAT3 m_min{ FLT_MAX, FLT_MAX, FLT_MAX };
		DirectX::XMFLOAT3 m_max{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
		bool m_any = false;
	};

	// 로드 끝에서 호출: stats 의 개수 필드를 채우고, 걸린 시간을 기록하고, 요약을 Log 로 남김.
	// meshes·instances·materials·images 는 out 의 배열 크기에서 가져옴.
	// (정점·삼각형·서브메시 수 같은 메시 단위 통계는 임포터가 메시를 만들며 out.stats 에 누적함)
	// 반환값 = out.IsValid()
	bool FinishModel(Model& out, std::chrono::steady_clock::time_point startTime);
}
