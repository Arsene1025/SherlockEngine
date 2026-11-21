#include "pch.h"
#include "Editor/GameBuilder.h"
#include "Editor/ProjectGenerator.h"
#include "Core/ScriptModule.h"
#include "Core/Paths.h"
#include "Core/Log.h"
#include "Graphics/TextureImporter.h"
#include "Graphics/Model.h"
#include <nlohmann/json.hpp>
#include <shellapi.h>
#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <chrono>
#include <map>
#include <memory>
#include <set>

namespace fs = std::filesystem;

namespace
{
	// 경로 비교용 키: 정규화 + '\' + 소문자 (Windows 경로는 대소문자를 가리지 않음).
	std::wstring PathKey(const fs::path& path)
	{
		std::wstring key = path.lexically_normal().wstring();
		std::replace(key.begin(), key.end(), L'/', L'\\');
		std::transform(key.begin(), key.end(), key.begin(), ::towlower);
		return key;
	}

	// 복사하지 않을 출력 파일 (PathKey). 빌드 때 DDS 로 변환되어 원본이 필요 없는 텍스처. Build 가 채우고 끝날 때 비움.
	// 이전 빌드가 같은 폴더에 남긴 사본은 지움 — 다시 빌드해도 원본이 남지 않게.
	std::set<std::wstring> g_excludedOutputs;
	std::set<std::wstring> g_skippedOutputs;   // 실제로 건너뛴 파일 (보고용)

	std::string Utf8(const std::wstring& s)
	{
		return Log::ToUtf8(s.c_str());
	}
	std::wstring Wide(const std::string& utf8)
	{
		if (utf8.empty()) return L"";
		const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
		std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
		if (length > 1)
		{
			MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], length);
		}
		return wide;
	}

	bool CopyOne(const fs::path& from, const fs::path& to, int& count, std::string& error)
	{
		std::error_code ec;
		if (!g_excludedOutputs.empty())
		{
			const std::wstring key = PathKey(to);
			if (g_excludedOutputs.count(key) != 0)
			{
				fs::remove(to, ec);
				g_skippedOutputs.insert(key);
				return true;
			}
		}
		fs::create_directories(to.parent_path(), ec);
		fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
		if (ec)
		{
			error = "copy failed: " + Utf8(from.wstring()) + " (" + ec.message() + ")";
			return false;
		}
		++count;
		return true;
	}
	bool CopyTree(const fs::path& from, const fs::path& to, int& count, std::string& error, bool required = true)
	{
		std::error_code ec;
		if (!fs::is_directory(from, ec))
		{
			if (required)
			{
				error = "missing folder: " + Utf8(from.wstring());
			}
			return !required;
		}
		for (const fs::directory_entry& entry : fs::recursive_directory_iterator(from, ec))
		{
			if (entry.is_directory(ec)) continue;
			const fs::path relative = fs::relative(entry.path(), from, ec);
			if (!CopyOne(entry.path(), to / relative, count, error)) return false;
		}
		return true;
	}

	// 시작 씬이 참조하는 에셋을 수집함: 모델 경로의 앞 두 단계(Models\Sponza)와 재질의 텍스처 이름.
	void CollectReferences(const fs::path& scenePath, std::set<std::wstring>& modelFolders, std::set<std::wstring>& textures)
	{
		std::ifstream file(scenePath, std::ios::binary);
		if (!file) return;
		nlohmann::json root;
		try
		{
			root = nlohmann::json::parse(file);
		}
		catch (...)
		{
			return;
		}
		for (const auto& mesh : root.value("meshes", nlohmann::json::array()))
		{
			const std::string path = mesh.value("path", "");
			if (path.empty()) continue;
			std::wstring wide = Wide(path);
			for (wchar_t& c : wide)
			{
				if (c == L'/')
				{
					c = L'\\';
				}
			}
			const size_t first = wide.find(L'\\');
			const size_t second = first == std::wstring::npos ? std::wstring::npos : wide.find(L'\\', first + 1);
			modelFolders.insert(second == std::wstring::npos ? wide : wide.substr(0, second));
		}
		for (const auto& material : root.value("materials", nlohmann::json::array()))
		{
			for (const char* key : { "albedoTexture", "normalTexture", "metallicRoughnessTexture", "occlusionTexture", "emissiveTexture", "specularTexture", "specularColorTexture", "clearcoatTexture", "clearcoatRoughnessTexture", "clearcoatNormalTexture", "transmissionTexture" })
			{
				const std::string name = material.value(key, "");
				if (name.empty() || name.rfind("builtin:", 0) == 0) continue;
				if (name.rfind("asset:", 0) == 0)
				{
					textures.insert(Wide(name.substr(6)));
				}
				else if (name.find('/') == std::string::npos)
				{
					textures.insert(L"Textures\\" + Wide(name));
				}
			}
		}
		// 스카이박스와 IBL 맵. 재질 텍스처와 같은 이름 규칙. 큐브 DDS·LUT 파일이거나 면 이미지 6장이 든
		// 폴더임 (복사 루프가 둘 다 처리함).
		const nlohmann::json env = root.value("environment", nlohmann::json());
		if (env.is_object())
		{
			for (const char* key : { "skybox", "irradiance", "specular", "brdfLut" })
			{
				const std::string name = env.value(key, "");
				if (name.empty() || name.rfind("builtin:", 0) == 0) continue;
				if (name.rfind("asset:", 0) == 0)
				{
					textures.insert(Wide(name.substr(6)));
				}
				else
				{
					textures.insert(L"Textures\\" + Wide(name));
				}
			}
		}
	}

	// 상대 경로의 에셋 폴더를 프로젝트 → 엔진 순으로 찾아 복사함 (겹치면 프로젝트 것이 우선함).
	bool CopyAssetTree(const std::wstring& relative, const fs::path& outAssets, int& count, std::string& error)
	{
		bool any = false;
		const std::wstring engine = Paths::GetEngineAssetRoot();
		std::error_code ec;
		if (fs::is_directory(fs::path(engine) / relative, ec))
		{
			if (!CopyTree(fs::path(engine) / relative, outAssets / relative, count, error)) return false;
			any = true;
		}
		if (Paths::HasProject())
		{
			const fs::path project = fs::path(Paths::GetAssetRoot()) / relative;
			if (fs::is_directory(project, ec))
			{
				if (!CopyTree(project, outAssets / relative, count, error)) return false;
				any = true;
			}
		}
		if (!any)
		{
			error = "missing folder: " + Utf8(relative);
		}
		return any;
	}

	// ---- 텍스처 변환(쿡): 패키지가 쓰는 텍스처를 DDS 로 만들고, 원본을 뺄 수 있는 파일을 고름 ----

	// 재질의 텍스처 칸과 색공간. Renderer::GetOrCreateGpuMaterial 의 Load2D 호출과 같아야 함 (srgb -1 = albedoSrgb 를 따름).
	struct TextureSlot
	{
		const char* key;
		std::string Material::* member;
		int srgb;
	};
	constexpr TextureSlot kTextureSlots[] = {
		{ "albedoTexture", &Material::albedoTexture, -1 },
		{ "normalTexture", &Material::normalTexture, 0 },
		{ "metallicRoughnessTexture", &Material::metallicRoughnessTexture, 0 },
		{ "occlusionTexture", &Material::occlusionTexture, 0 },
		{ "emissiveTexture", &Material::emissiveTexture, 1 },
		{ "specularTexture", &Material::specularTexture, 0 },
		{ "specularColorTexture", &Material::specularColorTexture, 1 },
		{ "clearcoatTexture", &Material::clearcoatTexture, 0 },
		{ "clearcoatRoughnessTexture", &Material::clearcoatRoughnessTexture, 0 },
		{ "clearcoatNormalTexture", &Material::clearcoatNormalTexture, 0 },
		{ "transmissionTexture", &Material::transmissionTexture, 0 },
	};

	struct TextureCookInput
	{
		std::set<std::pair<std::string, bool>> uses;   // (텍스처 이름, sRGB)
		std::set<std::wstring> models;                 // Assets 기준 모델 경로
		std::set<std::wstring> keep;                   // 원본을 반드시 실을 파일 (Assets 기준 PathKey) — 환경 맵(큐브는 변환 캐시를 거치지 않음)
	};

	void AddUse(TextureCookInput& input, const std::string& name, bool srgb)
	{
		if (name.empty() || name.rfind("builtin:", 0) == 0) return;
		input.uses.emplace(name, srgb);
	}

	// 재질이 쓰는 텍스처 이름 → Assets 기준 파일 경로. 모델 내장 이미지 이름("<모델>/<번호> …")이면 빈 문자열.
	std::wstring LooseTexturePath(const std::string& name)
	{
		if (name.rfind("asset:", 0) == 0) return Wide(name.substr(6));
		if (name.find('/') == std::string::npos) return L"Textures\\" + Wide(name);
		return L"";
	}

	// 씬·프리팹 JSON 의 재질 텍스처(색공간 포함), 모델 경로, 환경 맵.
	void CollectTextureUses(const fs::path& jsonPath, TextureCookInput& input)
	{
		std::ifstream file(jsonPath, std::ios::binary);
		if (!file) return;
		nlohmann::json root;
		try
		{
			root = nlohmann::json::parse(file);
		}
		catch (...)
		{
			return;
		}
		if (!root.is_object()) return;
		for (const auto& mesh : root.value("meshes", nlohmann::json::array()))
		{
			const std::string path = mesh.is_object() ? mesh.value("path", "") : "";
			if (!path.empty())
			{
				std::wstring wide = Wide(path);
				std::replace(wide.begin(), wide.end(), L'/', L'\\');
				input.models.insert(wide);
			}
		}
		for (const auto& material : root.value("materials", nlohmann::json::array()))
		{
			if (!material.is_object()) continue;
			const bool albedoSrgb = material.value("albedoSrgb", true);
			for (const TextureSlot& slot : kTextureSlots)
			{
				AddUse(input, material.value(slot.key, ""), slot.srgb < 0 ? albedoSrgb : slot.srgb != 0);
			}
		}
		const nlohmann::json env = root.value("environment", nlohmann::json());
		if (env.is_object())
		{
			for (const char* key : { "skybox", "irradiance", "specular", "brdfLut" })
			{
				const std::string name = env.value(key, "");
				if (name.empty() || name.rfind("builtin:", 0) == 0) continue;
				const std::wstring relative = name.rfind("asset:", 0) == 0 ? Wide(name.substr(6)) : L"Textures\\" + Wide(name);
				input.keep.insert(PathKey(relative));
			}
		}
	}

	// 절대 경로 → Assets 기준 PathKey (프로젝트 Assets → 엔진 Assets 순). 어느 쪽 밑도 아니면 빈 문자열.
	std::wstring AssetRelativeKey(const std::wstring& absolute)
	{
		const std::wstring path = PathKey(absolute);
		std::vector<std::wstring> roots = { Paths::GetEngineAssetRoot() };
		if (Paths::HasProject())
		{
			roots.insert(roots.begin(), Paths::GetAssetRoot());
		}
		for (const std::wstring& root : roots)
		{
			std::wstring prefix = PathKey(root);
			if (!prefix.empty() && prefix.back() != L'\\')
			{
				prefix += L'\\';
			}
			if (path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0) return path.substr(prefix.size());
		}
		return L"";
	}

	struct TextureCookResult
	{
		std::vector<TextureImporter::CookedTexture> cooked;
		std::set<std::wstring> excluded;   // 원본을 뺄 파일 (Assets 기준 PathKey)
		size_t keptOriginals = 0;          // 변환하지 못해 원본을 싣는 텍스처 (이미 DDS 등)
	};

	// 씬·프리팹이 쓰는 모델을 읽어(모델 자신의 재질도 사용처에 넣음 — 스크립트가 모델을 놓는 경우) 텍스처마다 CookForBuild 를 부름.
	// 캐시가 없으면 여기서 변환하므로 처음 빌드는 오래 걸릴 수 있음 (4096² 한 장 약 3초).
	TextureCookResult CookTextures(const std::vector<fs::path>& jsonFiles)
	{
		TextureCookInput input;
		for (const fs::path& path : jsonFiles)
		{
			CollectTextureUses(path, input);
		}

		std::vector<std::unique_ptr<Model>> models;
		std::map<std::string, const ModelImage*> images;
		for (const std::wstring& path : input.models)
		{
			auto model = std::make_unique<Model>();
			if (!ModelLoader::LoadGltf(Paths::GetAssetPath(path.c_str()), ModelLoader::Options{}, *model))
			{
				Log::Warn("게임 빌드: 텍스처 변환용 모델 읽기 실패 (%s) — 이 모델의 이미지는 원본을 실음", Utf8(path).c_str());
				continue;
			}
			for (const Material& material : model->materials)
			{
				for (const TextureSlot& slot : kTextureSlots)
				{
					AddUse(input, material.*slot.member, slot.srgb < 0 ? material.albedoSrgb : slot.srgb != 0);
				}
			}
			for (const ModelImage& image : model->images)
			{
				if (image.encoded != nullptr)
				{
					images.emplace(image.name, &image);
				}
			}
			models.push_back(std::move(model));
		}

		TextureCookResult result;
		std::set<std::wstring> covered;   // 변환본이 있는 원본 파일
		std::set<std::string> failed;     // 변환하지 못한 텍스처 이름 (보고용)
		std::vector<uint8_t> bytes;
		for (const auto& [name, srgb] : input.uses)
		{
			const uint8_t* data = nullptr;
			size_t size = 0;
			std::wstring source;   // Assets 기준 PathKey. 비면 뺄 파일이 없음 (GLB·data URI 내장 이미지)
			if (const auto image = images.find(name); image != images.end())
			{
				data = image->second->encoded->data();
				size = image->second->encoded->size();
				if (!image->second->sourcePath.empty())
				{
					source = AssetRelativeKey(image->second->sourcePath);
				}
			}
			else if (const std::wstring relative = LooseTexturePath(name); !relative.empty())
			{
				std::ifstream file(Paths::GetAssetPath(relative.c_str()), std::ios::binary | std::ios::ate);
				if (!file) continue;   // 없는 파일 — 게임도 흰색 (지금과 같음)
				bytes.resize(static_cast<size_t>(file.tellg()));
				file.seekg(0);
				if (bytes.empty() || !file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) continue;
				data = bytes.data();
				size = bytes.size();
				source = PathKey(relative);
			}
			else
			{
				continue;   // 읽지 못한 모델의 이미지
			}

			const std::wstring cached = TextureImporter::CookForBuild(name, data, size, srgb);
			if (cached.empty())
			{
				if (failed.insert(name).second)
				{
					Log::Info("게임 빌드: 변환하지 않고 원본을 실음 — %s (이미 DDS 이거나 디코딩할 수 없음)", name.c_str());
				}
				if (!source.empty())
				{
					input.keep.insert(source);
				}
				continue;
			}
			result.cooked.push_back({ name, srgb, fs::path(cached).filename().wstring() });
			if (!source.empty())
			{
				covered.insert(source);
			}
		}
		// keep 은 파일이거나 폴더(면 이미지 6장이 든 큐브맵 폴더)임 — 폴더면 그 안의 파일도 남김.
		const auto kept = [&input](const std::wstring& source)
		{
			for (const std::wstring& keep : input.keep)
			{
				if (source == keep || (source.size() > keep.size() && source.compare(0, keep.size(), keep) == 0 && source[keep.size()] == L'\\')) return true;
			}
			return false;
		};
		for (const std::wstring& source : covered)
		{
			if (!kept(source))
			{
				result.excluded.insert(source);
			}
		}
		result.keptOriginals = failed.size();
		return result;
	}

	// dir 아래의 확장자 ext 파일 (하위 폴더 포함). 폴더가 없으면 아무것도 넣지 않음.
	void ListFiles(const fs::path& dir, const wchar_t* ext, std::vector<fs::path>& out)
	{
		std::error_code ec;
		if (!fs::is_directory(dir, ec)) return;
		for (const fs::directory_entry& entry : fs::recursive_directory_iterator(dir, ec))
		{
			if (entry.is_regular_file(ec) && entry.path().extension() == ext)
			{
				out.push_back(entry.path());
			}
		}
	}
}

bool GameBuilder::IsAvailable()
{
	return Paths::HasProject();
}

std::wstring GameBuilder::GetBuildRoot()
{
	return (Paths::HasProject() ? Paths::GetProjectRoot() : Paths::GetExecutableDir()) + L"Build\\";
}

std::vector<std::string> GameBuilder::ListScenes()
{
	std::vector<std::string> scenes;
	std::error_code ec;
	for (const fs::directory_entry& entry : fs::directory_iterator(Paths::GetAssetRoot() + L"Scenes", ec))
	{
		if (entry.is_regular_file(ec) && entry.path().extension() == L".json")
		{
			scenes.push_back(Utf8(entry.path().filename().wstring()));
		}
	}
	std::sort(scenes.begin(), scenes.end());
	return scenes;
}

GameBuilder::Result GameBuilder::Build(const Options& options)
{
	Result result;
	const auto started = std::chrono::steady_clock::now();
	if (options.name.empty() || options.startScene.empty())
	{
		result.message = "name and start scene are required";
		return result;
	}
	for (char c : options.name)
	{
		if (c == '\\' || c == '/' || c == ':' || c == '"')
		{
			result.message = "invalid game name";
			return result;
		}
	}
	if (!Paths::HasProject())
	{
		result.message = "no project is open";
		return result;
	}

	// 런타임 = SherlockGame.exe + SherlockEngine.dll(+ vcpkg DLL) — 모든 프로젝트가 같은 exe 를 씀 — 와
	// 프로젝트의 <이름>Scripts.dll. Release 를 켰으면 엔진 솔루션의 SherlockGame 과 프로젝트의 스크립트 DLL
	// 을 Release 로 빌드한 뒤 Release 폴더에서 가져옴.
	const std::wstring buildLog = GetBuildRoot() + L"msbuild.log";
	std::wstring runtimeDir = Paths::GetExecutableDir();
	const wchar_t* configuration = ScriptModule::GetConfiguration();
	if (options.releaseBuild)
	{
		configuration = L"Release";
		// 이 에디터가 Release 면 엔진 Release DLL 은 지금 쓰는 중이고 이미 빌드돼 있음 (다시 링크할 수 없음)
		if (wcscmp(ScriptModule::GetConfiguration(), L"Release") != 0)
		{
			Log::Info("게임 빌드: MSBuild SherlockGame (Release|x64) …");
			if (!ProjectGenerator::RunMsBuild(ProjectGenerator::GetEngineRepoDir() + L"SherlockEngine.sln", L"SherlockGame", L"Release", buildLog, result.message)) return result;
		}
		// 이 exe 가 x64\Debug 에 있으면 같은 위치의 Release 폴더를 씀
		const size_t debug = runtimeDir.rfind(L"\\Debug\\");
		if (debug != std::wstring::npos)
		{
			runtimeDir = runtimeDir.substr(0, debug) + L"\\Release\\";
		}
	}
	std::error_code ec;
	const fs::path runtimeExe = fs::path(runtimeDir) / L"SherlockGame.exe";
	if (!fs::exists(runtimeExe, ec))
	{
		result.message = "game runtime not found in " + Utf8(runtimeDir) + " (build SherlockGame in SherlockEngine.sln)";
		return result;
	}

	// 스크립트 DLL: <프로젝트>\Binaries\<구성>\<이름>Scripts.dll. Release 이거나 아직 없으면 먼저 빌드함
	// (엔진은 위에서 빌드했으므로 BuildProjectReferences=false).
	const std::wstring scriptsName = Wide(options.projectName) + L"Scripts";
	const fs::path scriptsDll = fs::path(Paths::GetProjectRoot()) / L"Binaries" / configuration / (scriptsName + L".dll");
	size_t scriptSources = 0;
	for (const fs::directory_entry& entry : fs::recursive_directory_iterator(Paths::GetProjectRoot() + L"Scripts", ec))
	{
		if (entry.is_regular_file(ec) && entry.path().extension() == L".cpp")
		{
			++scriptSources;
		}
	}
	if (scriptSources > 0 && (options.releaseBuild || !fs::exists(scriptsDll, ec)))
	{
		if (options.projectSolution.empty() || !fs::exists(options.projectSolution, ec))
		{
			result.message = "project solution not found: " + Utf8(options.projectSolution);
			return result;
		}
		Log::Info("게임 빌드: MSBuild %s (%s|x64) …", Utf8(scriptsName).c_str(), Utf8(configuration).c_str());
		if (!ProjectGenerator::RunMsBuild(options.projectSolution, scriptsName, configuration, buildLog, result.message, L"/p:BuildProjectReferences=false")) return result;
	}

	const fs::path out = fs::path(GetBuildRoot()) / Wide(options.name);
	result.outputDir = out.wstring() + L"\\";
	fs::create_directories(out, ec);

	// 1. 런타임: exe(게임 이름으로) + DLL (SherlockEngine.dll, vcpkg 가 둔 DirectXTex.dll 등)
	if (!CopyOne(runtimeExe, out / (Wide(options.name) + L".exe"), result.filesCopied, result.message)) return result;
	for (const fs::directory_entry& entry : fs::directory_iterator(runtimeDir, ec))
	{
		if (entry.is_regular_file(ec) && entry.path().extension() == L".dll")
		{
			if (!CopyOne(entry.path(), out / entry.path().filename(), result.filesCopied, result.message)) return result;
		}
	}
	// 스크립트 DLL 은 <게임 이름>Scripts.dll 로 둠 — 패키지의 .sherlock 이름이 게임 이름이고, 런타임은 exe
	// 옆의 <프로젝트 이름>Scripts.dll 을 찾음 (AppBase::LoadProjectScripts).
	if (fs::exists(scriptsDll, ec))
	{
		if (!CopyOne(scriptsDll, out / (Wide(options.name) + L"Scripts.dll"), result.filesCopied, result.message)) return result;
	}
	else if (scriptSources > 0)
	{
		result.message = "scripts DLL not found: " + Utf8(scriptsDll.wstring());
		return result;
	}
	// 2. 셰이더: exe 옆의 .cso + 엔진 루트의 .hlsl 소스 (런타임 컴파일 폴백)
	CopyTree(fs::path(runtimeDir) / L"Shaders", out / L"Shaders", result.filesCopied, result.message, false);
	if (!CopyTree(fs::path(Paths::GetEngineRoot()) / L"Shaders", out / L"Shaders", result.filesCopied, result.message)) return result;

	// 3. 에셋: 엔진 콘텐츠 위에 프로젝트 콘텐츠를 덮어씀 (런타임의 경로 해석 순서와 같음). 패키지 안에서는
	//    둘이 한 폴더로 합쳐짐.
	const fs::path outAssets = out / L"Assets";
	const fs::path scenePath = fs::path(Paths::GetAssetRoot()) / L"Scenes" / Wide(options.startScene);
	if (!fs::exists(scenePath, ec))
	{
		result.message = "start scene not found: " + options.startScene;
		return result;
	}

	// 3a. 텍스처 변환: 패키지에 들어가는 씬(에셋 전부면 모든 씬)과 프리팹이 쓰는 텍스처를 DDS 로 만들어 두고, 변환본이 생긴 원본
	//     PNG/JPG 는 아래 복사에서 뺌. 환경 맵(큐브)과 변환하지 못한 것(이미 DDS 등)은 원본을 실음.
	struct ExcludeScope
	{
		~ExcludeScope()
		{
			g_excludedOutputs.clear();
			g_skippedOutputs.clear();
		}
	} excludeScope;
	TextureCookResult cook;
	{
		std::vector<fs::path> jsonFiles;
		if (options.copyAllAssets)
		{
			ListFiles(fs::path(Paths::GetEngineAssetRoot()) / L"Scenes", L".json", jsonFiles);
			ListFiles(fs::path(Paths::GetAssetRoot()) / L"Scenes", L".json", jsonFiles);
		}
		else
		{
			jsonFiles.push_back(scenePath);
		}
		ListFiles(fs::path(Paths::GetEngineAssetRoot()) / L"Prefabs", L".prefab", jsonFiles);
		ListFiles(fs::path(Paths::GetAssetRoot()) / L"Prefabs", L".prefab", jsonFiles);
		Log::Info("게임 빌드: 텍스처 변환 (씬·프리팹 %zu 개) …", jsonFiles.size());
		cook = CookTextures(jsonFiles);
		for (const std::wstring& relative : cook.excluded)
		{
			g_excludedOutputs.insert(PathKey(outAssets / relative));
		}
	}

	if (options.copyAllAssets)
	{
		if (!CopyTree(Paths::GetEngineAssetRoot(), outAssets, result.filesCopied, result.message)) return result;
		if (!CopyTree(Paths::GetAssetRoot(), outAssets, result.filesCopied, result.message)) return result;
	}
	else
	{
		if (!CopyAssetTree(L"Config", outAssets, result.filesCopied, result.message)) return result;
		if (!CopyOne(scenePath, outAssets / L"Scenes" / Wide(options.startScene), result.filesCopied, result.message)) return result;
		std::set<std::wstring> modelFolders, textures;
		CollectReferences(scenePath, modelFolders, textures);
		// 프리팹. 스크립트가 "Prefabs\Bullet.prefab" 같은 문자열로 Instantiate 하므로 씬 파일이 참조하지 않음
		// — Prefabs 폴더를 통째로 복사하고, 프리팹이 쓰는 모델·텍스처도 씬과 같은 방식(같은 JSON 키)으로
		// 모음.
		{
			std::string ignored;
			CopyAssetTree(L"Prefabs", outAssets, result.filesCopied, ignored);   // 폴더가 없는 프로젝트도 있음
			std::vector<std::wstring> roots = { Paths::GetEngineAssetRoot() };
			if (Paths::HasProject())
			{
				roots.push_back(Paths::GetAssetRoot());
			}
			for (const std::wstring& root : roots)
			{
				for (const fs::directory_entry& entry : fs::recursive_directory_iterator(fs::path(root) / L"Prefabs", ec))
				{
					if (entry.is_regular_file(ec) && entry.path().extension() == L".prefab")
					{
						CollectReferences(entry.path(), modelFolders, textures);
					}
				}
			}
		}
		for (const std::wstring& folder : modelFolders)
		{
			if (!CopyAssetTree(folder, outAssets, result.filesCopied, result.message)) return result;
		}
		for (const std::wstring& texture : textures)
		{
			const std::wstring found = Paths::GetAssetPath(texture.c_str());
			if (fs::is_directory(found, ec))   // 스카이박스 면 이미지 폴더
			{
				if (!CopyTree(found, outAssets / texture, result.filesCopied, result.message)) return result;
			}
			else if (fs::exists(found, ec) && !CopyOne(found, outAssets / texture, result.filesCopied, result.message))
			{
				return result;
			}
		}
		CopyAssetTree(L"Textures", outAssets, result.filesCopied, result.message);   // 이름만으로 참조되는 기본 텍스처(bumps_normal 등)를 위해 전부 복사
	}

	// 3b. 변환된 DDS: 3a 가 쓴 것만 패키지의 TextureCache\ (프로젝트가 없는 게임의 캐시 위치)에 싣고, 이름 + 색공간 → 파일 목록
	//     (manifest.json)을 씀. 게임은 이 목록으로 DDS 를 바로 읽음 (TextureImporter::FindCooked). 캐시 폴더의 옛 항목은 싣지 않음.
	//     이전 빌드의 TextureCache\ 는 지우고 새로 만듦. DDS 를 싣지 못하면 원본도 없으므로 빌드를 실패로 함.
	{
		const fs::path outCache = out / L"TextureCache";
		fs::remove_all(outCache, ec);
		std::set<std::wstring> copied;
		for (const TextureImporter::CookedTexture& texture : cook.cooked)
		{
			if (!copied.insert(texture.file).second) continue;
			if (!CopyOne(fs::path(TextureImporter::GetCacheDir()) / texture.file, outCache / texture.file, result.filesCopied, result.message)) return result;
		}
		if (!TextureImporter::WriteManifest(outCache.wstring() + L"\\", cook.cooked, result.message)) return result;
		++result.filesCopied;
		Log::Info("게임 빌드: 텍스처 변환본 %zu 개 (DDS %zu 파일), 원본 %zu 파일 뺌, 변환하지 못해 원본을 싣는 텍스처 %zu 개", cook.cooked.size(), copied.size(), g_skippedOutputs.size(), cook.keptOriginals);
	}

	// 4. engine.ini: [game] startScene / title 을 빌드 사본에 씀 (원본은 건드리지 않음).
	{
		const fs::path ini = outAssets / L"Config" / L"engine.ini";
		std::ifstream in(ini, std::ios::binary);
		std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		in.close();
		text += "\r\n; --- Build Game (" + options.name + ") ---\r\n[game]\r\nstartScene = " + options.startScene + "\r\ntitle = " + options.name + "\r\n";
		fs::create_directories(ini.parent_path(), ec);
		std::ofstream outIni(ini, std::ios::binary | std::ios::trunc);
		outIni << text;   // 같은 키가 앞에 있어도 마지막 값이 적용됨 (Config 는 키 → 값 맵)
	}
	// 5. 프로젝트 파일 사본: 런타임이 exe 옆에서 이 파일을 찾아 exe 폴더를 프로젝트 루트로 잡음 (engineRoot
	//    가 비어 있으므로 모든 경로가 exe 옆을 가리킴).
	{
		std::ofstream sherlock(out / (Wide(options.name) + L".sherlock"), std::ios::binary | std::ios::trunc);
		sherlock << "{\n  \"name\": \"" << options.name << "\",\n  \"startScene\": \"" << options.startScene << "\",\n  \"engineRoot\": \"\",\n  \"format\": 1\n}\n";
		++result.filesCopied;
	}

	result.ok = true;
	result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
	result.message = std::format("built {} ({} files, {:.0f} s)", options.name, result.filesCopied, result.seconds);   // C++20 std::format
	Log::Info("게임 빌드: %s → %s (%d 파일, %.1f 초)", options.name.c_str(), Utf8(result.outputDir).c_str(), result.filesCopied, result.seconds);
	if (options.openFolder)
	{
		ShellExecuteW(nullptr, L"explore", result.outputDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}
	return result;
}
