#include "pch.h"
#include "Graphics/Import/ImportCommon.h"
#include "Core/Log.h"
#include <fstream>

using namespace DirectX;

namespace ModelImport
{
	std::string ToUtf8(const std::wstring& wide)
	{
		if (wide.empty()) return "";
		const int length = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
		std::string utf8(length > 0 ? length - 1 : 0, '\0');
		if (length > 1) WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &utf8[0], length, nullptr, nullptr);
		return utf8;
	}

	std::wstring ToWide(const std::string& utf8)
	{
		if (utf8.empty()) return L"";
		const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
		std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
		if (length > 1) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], length);
		return wide;
	}

	bool ReadWholeFile(const std::wstring& path, std::vector<uint8_t>& out)
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file) return false;
		const std::streamsize size = file.tellg();
		if (size <= 0) return false;
		out.resize(static_cast<size_t>(size));
		file.seekg(0);
		return static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()), size));
	}

	std::string ModelNameFromPath(const std::wstring& path)
	{
		const size_t slash = path.find_last_of(L"\\/");
		const std::wstring file = slash == std::wstring::npos ? path : path.substr(slash + 1);
		const size_t dot = file.find_last_of(L'.');
		return ToUtf8(dot == std::wstring::npos ? file : file.substr(0, dot));
	}

	std::wstring DirectoryFromPath(const std::wstring& path)
	{
		const size_t slash = path.find_last_of(L"\\/");
		return slash == std::wstring::npos ? L"" : path.substr(0, slash + 1);
	}

	void BoundsAccumulator::Add(const Bounds& local, FXMMATRIX world)
	{
		// 로컬 AABB 의 여덟 꼭짓점을 월드로 옮겨 합침.
		XMVECTOR lo = XMLoadFloat3(&m_min);
		XMVECTOR hi = XMLoadFloat3(&m_max);
		for (int corner = 0; corner < 8; ++corner)
		{
			const XMVECTOR p = XMVectorSet((corner & 1) ? local.max.x : local.min.x, (corner & 2) ? local.max.y : local.min.y, (corner & 4) ? local.max.z : local.min.z, 1.0f);
			const XMVECTOR w = XMVector3TransformCoord(p, world);
			lo = XMVectorMin(lo, w);
			hi = XMVectorMax(hi, w);
		}
		XMStoreFloat3(&m_min, lo);
		XMStoreFloat3(&m_max, hi);
		m_any = true;
	}

	void BoundsAccumulator::ApplyTo(Model& model) const
	{
		if (!m_any) return;
		model.bounds.min = m_min;
		model.bounds.max = m_max;
	}

	bool FinishModel(Model& out, std::chrono::steady_clock::time_point startTime)
	{
		out.stats.meshes = static_cast<uint32_t>(out.meshes.size());
		out.stats.instances = static_cast<uint32_t>(out.instances.size());
		out.stats.materials = static_cast<uint32_t>(out.materials.size());
		out.stats.images = static_cast<uint32_t>(out.images.size());
		out.stats.loadMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startTime).count();

		Log::Info("모델 로드: %s — 메시 %u (서브메시 %u, 인스턴스 %u), 재질 %u, 이미지 %u, 정점 %u, 삼각형 %u, 16비트 인덱스 %u, 탄젠트 생성 %u, 건너뜀 %u, %.0f ms",
			out.name.c_str(), out.stats.meshes, out.stats.submeshes, out.stats.instances, out.stats.materials, out.stats.images,
			out.stats.vertices, out.stats.triangles, out.stats.meshesWith16BitIndices, out.stats.generatedTangentMeshes, out.stats.skippedPrimitives, out.stats.loadMilliseconds);
		Log::Info("모델 바운드: (%.2f, %.2f, %.2f) ~ (%.2f, %.2f, %.2f)", out.bounds.min.x, out.bounds.min.y, out.bounds.min.z, out.bounds.max.x, out.bounds.max.y, out.bounds.max.z);
		return out.IsValid();
	}
}
