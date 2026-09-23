#include "pch.h"
#include "Core/AssetManager.h"
#include "Core/Paths.h"
#include "Core/Log.h"
#include "Graphics/Model.h"
#include <algorithm>
#include <chrono>
#include <fstream>

AssetManager::AssetManager() = default;
AssetManager::~AssetManager() = default;

std::wstring AssetManager::Normalize(const std::wstring& path)
{
	std::wstring key = path;
	std::replace(key.begin(), key.end(), L'/', L'\\');
	std::transform(key.begin(), key.end(), key.begin(), ::towlower);
	return key;
}

size_t AssetManager::EstimateBytes(const Model& model)
{
	size_t bytes = 0;
	for (const Mesh& mesh : model.meshes) bytes += mesh.GetVertices().size() * sizeof(VERTEX) + mesh.GetIndices().size() * sizeof(uint32_t);
	for (const ModelImage& image : model.images) bytes += image.encoded.size();
	return bytes;
}

const Model* AssetManager::GetModel(const std::wstring& relativePath)
{
	const std::wstring key = Normalize(relativePath);
	auto found = m_models.find(key);
	if (found != m_models.end())
	{
		++m_stats.modelHits;
		return found->second.get();
	}

	const auto start = std::chrono::steady_clock::now();
	auto model = std::make_unique<Model>();
	ModelLoader::Options options;
	if (!ModelLoader::LoadGltf(Paths::GetAssetPath(relativePath.c_str()), options, *model))
	{
		Log::Error("AssetManager : 모델 로드 실패 (%s)", Log::ToUtf8(relativePath.c_str()).c_str());
		return nullptr;
	}
	model->sourcePath = relativePath;
	++m_stats.modelLoads;
	m_stats.totalLoadMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	m_stats.cachedBytes += EstimateBytes(*model);
	const Model* result = model.get();
	m_models.emplace(key, std::move(model));
	return result;
}

const std::vector<uint8_t>* AssetManager::GetFile(const std::wstring& relativePath)
{
	const std::wstring key = Normalize(relativePath);
	auto found = m_files.find(key);
	if (found != m_files.end())
	{
		++m_stats.fileHits;
		return &found->second;
	}
	const std::wstring path = Paths::GetAssetPath(relativePath.c_str());
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file)
	{
		Log::Error("AssetManager : 파일을 열 수 없음 (%s)", Log::ToUtf8(path.c_str()).c_str());
		return nullptr;
	}
	const std::streamsize size = file.tellg();
	std::vector<uint8_t> bytes(static_cast<size_t>(size > 0 ? size : 0));
	file.seekg(0);
	if (size > 0 && !file.read(reinterpret_cast<char*>(bytes.data()), size))
	{
		Log::Error("AssetManager : 파일 읽기 실패 (%s)", Log::ToUtf8(path.c_str()).c_str());
		return nullptr;
	}
	++m_stats.fileLoads;
	m_stats.cachedBytes += bytes.size();
	return &m_files.emplace(key, std::move(bytes)).first->second;
}

bool AssetManager::IsModelCached(const std::wstring& relativePath) const
{
	return m_models.find(Normalize(relativePath)) != m_models.end();
}

void AssetManager::UnloadModel(const std::wstring& relativePath)
{
	auto found = m_models.find(Normalize(relativePath));
	if (found == m_models.end()) return;
	m_stats.cachedBytes -= EstimateBytes(*found->second);
	m_models.erase(found);
}

void AssetManager::Clear()
{
	m_models.clear();
	m_files.clear();
	m_stats.cachedBytes = 0;
}

std::vector<std::wstring> AssetManager::GetModelPaths() const
{
	std::vector<std::wstring> paths;
	for (const auto& entry : m_models) paths.push_back(entry.first);
	std::sort(paths.begin(), paths.end());
	return paths;
}
