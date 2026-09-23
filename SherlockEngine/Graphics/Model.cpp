#include "pch.h"
#include "Graphics/Model.h"
#include "Core/Log.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>

// cgltf: 단일 헤더 C 라이브러리 (vcpkg). 이 파일에서만 구현을 켠다.
// 로드맵은 tinygltf 를 권장했지만 vcpkg 포트의 소스 아카이브 해시가 GitHub 재압축으로 어긋나
// 설치되지 않았다. cgltf 는 같은 자리(glTF 전용·경량·헤더 하나)의 대안이고, 이미지 디코딩을
// 하지 않는 점이 오히려 맞는다 — 디코딩은 5단계의 TextureLoader(DirectXTex)가 맡는다.
#define CGLTF_IMPLEMENTATION
#pragma warning(push)
#pragma warning(disable : 4996)   // cgltf 내부의 fopen/strncpy (CRT 보안 경고). 우리 코드가 아니다.
#include <cgltf.h>
#pragma warning(pop)

using namespace DirectX;   // 이 파일 안에서만

namespace
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

	const char* ResultName(cgltf_result result)
	{
		switch (result)
		{
		case cgltf_result_success: return "success";
		case cgltf_result_data_too_short: return "data_too_short";
		case cgltf_result_unknown_format: return "unknown_format";
		case cgltf_result_invalid_json: return "invalid_json";
		case cgltf_result_invalid_gltf: return "invalid_gltf";
		case cgltf_result_invalid_options: return "invalid_options";
		case cgltf_result_file_not_found: return "file_not_found";
		case cgltf_result_io_error: return "io_error";
		case cgltf_result_out_of_memory: return "out_of_memory";
		case cgltf_result_legacy_gltf: return "legacy_gltf";
		default: return "?";
		}
	}

	// glTF 샘플러 → 엔진 프리셋. 5단계 프리셋 다섯 개로 근사한다.
	SamplerPreset ToSamplerPreset(const cgltf_sampler* sampler)
	{
		if (sampler == nullptr) return SamplerPreset::AnisotropicWrap;
		const bool clamp = sampler->wrap_s == cgltf_wrap_mode_clamp_to_edge || sampler->wrap_t == cgltf_wrap_mode_clamp_to_edge;
		const bool nearest = sampler->mag_filter == cgltf_filter_type_nearest;
		if (nearest) return SamplerPreset::PointWrap;
		if (clamp) return SamplerPreset::LinearClamp;
		return SamplerPreset::AnisotropicWrap;
	}

	// 이미지 → 이름. 텍스처 캐시의 키가 되므로 모델 안에서 유일해야 한다.
	std::string ImageName(const std::string& modelName, const cgltf_data* data, const cgltf_image* image)
	{
		const size_t index = static_cast<size_t>(image - data->images);
		std::string name = modelName + "/" + std::to_string(index);
		if (image->uri != nullptr) name += std::string(" ") + image->uri;
		else if (image->name != nullptr) name += std::string(" ") + image->name;
		return name;
	}

	// 재질이 참조하는 텍스처의 이미지 이름. 텍스처가 없으면 빈 문자열.
	std::string TextureName(const std::string& modelName, const cgltf_data* data, const cgltf_texture_view& view)
	{
		if (view.texture == nullptr || view.texture->image == nullptr) return "";
		return ImageName(modelName, data, view.texture->image);
	}

	// 이미지 바이트를 모은다. GLB 내장(buffer_view), data: URI, 외부 파일 순.
	bool CollectImage(const cgltf_data* data, const cgltf_image& image, const std::wstring& directory, ModelImage& out)
	{
		if (image.buffer_view != nullptr && image.buffer_view->buffer != nullptr && image.buffer_view->buffer->data != nullptr)
		{
			const uint8_t* begin = static_cast<const uint8_t*>(image.buffer_view->buffer->data) + image.buffer_view->offset;
			out.encoded.assign(begin, begin + image.buffer_view->size);
			return true;
		}
		if (image.uri == nullptr) return false;

		if (strncmp(image.uri, "data:", 5) == 0)
		{
			const char* comma = strchr(image.uri, ',');
			if (comma == nullptr) return false;
			const char* base64 = comma + 1;
			const size_t encodedLength = strlen(base64);
			size_t decodedSize = encodedLength / 4 * 3;
			if (encodedLength >= 1 && base64[encodedLength - 1] == '=') --decodedSize;
			if (encodedLength >= 2 && base64[encodedLength - 2] == '=') --decodedSize;
			cgltf_options options = {};
			void* decoded = nullptr;
			if (cgltf_load_buffer_base64(&options, decodedSize, base64, &decoded) != cgltf_result_success || decoded == nullptr) return false;
			out.encoded.assign(static_cast<uint8_t*>(decoded), static_cast<uint8_t*>(decoded) + decodedSize);
			free(decoded);
			return true;
		}

		// 외부 파일. URI 의 %20 같은 이스케이프를 푼다.
		std::string uri = image.uri;
		cgltf_decode_uri(&uri[0]);
		uri.resize(strlen(uri.c_str()));
		std::wstring path = directory + ToWide(uri);
		std::replace(path.begin(), path.end(), L'/', L'\\');
		if (!ReadWholeFile(path, out.encoded))
		{
			Log::Warn("모델 이미지 파일을 열 수 없음: %s", ToUtf8(path).c_str());
			return false;
		}
		return true;
	}

	// glTF 재질 → 엔진 Material. 금속성·거칠기는 12단계(PBR) 전까지 Blinn-Phong 으로 근사한다.
	Material ConvertMaterial(const std::string& modelName, const cgltf_data* data, const cgltf_material* source, size_t index)
	{
		Material material;
		material.name = (source != nullptr && source->name != nullptr && source->name[0] != '\0')
			? source->name : (modelName + "/material" + std::to_string(index));
		material.sampler = SamplerPreset::AnisotropicWrap;
		if (source == nullptr) return material;

		float roughness = 1.0f;
		float metallic = 0.0f;
		if (source->has_pbr_metallic_roughness)
		{
			const cgltf_pbr_metallic_roughness& pbr = source->pbr_metallic_roughness;
			material.baseColor = XMFLOAT4(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3]);
			material.albedoTexture = TextureName(modelName, data, pbr.base_color_texture);
			material.albedoSrgb = true;
			roughness = pbr.roughness_factor;
			metallic = pbr.metallic_factor;
			if (pbr.base_color_texture.texture != nullptr) material.sampler = ToSamplerPreset(pbr.base_color_texture.texture->sampler);
		}
		// 거칠기 1(기본)이면 거의 무광, 0 이면 날카로운 하이라이트. 금속은 스페큘러가 기본색을 띤다.
		const float gloss = 1.0f - std::clamp(roughness, 0.0f, 1.0f);
		material.shininess = 8.0f + 120.0f * gloss * gloss;
		const float specular = 0.04f + 0.3f * gloss;
		material.specularColor = XMFLOAT3(
			specular + (material.baseColor.x - specular) * metallic,
			specular + (material.baseColor.y - specular) * metallic,
			specular + (material.baseColor.z - specular) * metallic);

		material.normalTexture = TextureName(modelName, data, source->normal_texture);
		if (!material.normalTexture.empty()) material.normalStrength = source->normal_texture.scale > 0.0f ? source->normal_texture.scale : 1.0f;

		material.doubleSided = source->double_sided != 0;
		// MASK 는 컷아웃. BLEND 는 아직 블렌딩이 없으니 컷아웃으로 근사한다 (12단계).
		if (source->alpha_mode == cgltf_alpha_mode_mask) material.alphaCutoff = source->alpha_cutoff > 0.0f ? source->alpha_cutoff : 0.5f;
		else if (source->alpha_mode == cgltf_alpha_mode_blend) material.alphaCutoff = 0.5f;
		return material;
	}

	const cgltf_accessor* FindAttribute(const cgltf_primitive& primitive, cgltf_attribute_type type, int set = 0)
	{
		for (cgltf_size i = 0; i < primitive.attributes_count; ++i)
		{
			if (primitive.attributes[i].type == type && primitive.attributes[i].index == set) return primitive.attributes[i].data;
		}
		return nullptr;
	}

	// glTF 열우선 float[16] → DirectXMath 행벡터 행렬. 원소를 그대로 읽으면 전치가 되는데,
	// 열벡터 M 에 대한 행벡터 행렬이 정확히 Mᵀ 이므로 그대로가 맞다.
	XMMATRIX LoadGltfMatrix(const float m[16])
	{
		return XMMATRIX(m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]);
	}

	// glTF mesh (primitive 여러 개) → 정점·인덱스 하나 + 서브메시. Z 뒤집기와 탄젠트 처리 포함.
	bool ConvertMesh(const cgltf_data* data, const cgltf_mesh& source, const ModelLoader::Options& options, uint32_t defaultSlot, Mesh& outMesh, ModelStats& stats)
	{
		std::vector<VERTEX> vertices;
		std::vector<uint32_t> indices;
		std::vector<Submesh> submeshes;
		bool needTangents = false;
		const float zSign = options.flipZ ? -1.0f : 1.0f;

		for (cgltf_size p = 0; p < source.primitives_count; ++p)
		{
			const cgltf_primitive& primitive = source.primitives[p];
			const cgltf_accessor* position = FindAttribute(primitive, cgltf_attribute_type_position);
			if (primitive.type != cgltf_primitive_type_triangles || position == nullptr)
			{
				++stats.skippedPrimitives;
				continue;
			}
			const cgltf_accessor* normal = FindAttribute(primitive, cgltf_attribute_type_normal);
			const cgltf_accessor* texcoord = FindAttribute(primitive, cgltf_attribute_type_texcoord);
			const cgltf_accessor* tangent = FindAttribute(primitive, cgltf_attribute_type_tangent);
			const cgltf_accessor* color = FindAttribute(primitive, cgltf_attribute_type_color);

			const uint32_t baseVertex = static_cast<uint32_t>(vertices.size());
			const cgltf_size vertexCount = position->count;
			vertices.reserve(vertices.size() + vertexCount);
			for (cgltf_size v = 0; v < vertexCount; ++v)
			{
				VERTEX vertex{ 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f };
				float f[4] = {};
				cgltf_accessor_read_float(position, v, f, 3);
				vertex.x = f[0]; vertex.y = f[1]; vertex.z = f[2] * zSign;
				if (normal != nullptr && cgltf_accessor_read_float(normal, v, f, 3))
				{
					vertex.nx = f[0]; vertex.ny = f[1]; vertex.nz = f[2] * zSign;
				}
				if (texcoord != nullptr && cgltf_accessor_read_float(texcoord, v, f, 2))
				{
					vertex.u = f[0]; vertex.v = f[1];   // glTF 도 (0,0) 이 왼쪽 위. 그대로.
				}
				if (color != nullptr)
				{
					f[3] = 1.0f;
					cgltf_accessor_read_float(color, v, f, cgltf_num_components(color->type) == 3 ? 3 : 4);
					vertex.r = f[0]; vertex.g = f[1]; vertex.b = f[2]; vertex.a = f[3];
				}
				if (tangent != nullptr && cgltf_accessor_read_float(tangent, v, f, 4))
				{
					// 반사(Z 뒤집기)는 cross 의 부호를 바꾸므로 손잡이 w 도 뒤집는다 (Model.h 주석).
					vertex.tx = f[0]; vertex.ty = f[1]; vertex.tz = f[2] * zSign; vertex.tw = options.flipZ ? -f[3] : f[3];
				}
				vertices.push_back(vertex);
			}
			if (tangent == nullptr) needTangents = true;

			Submesh submesh;
			submesh.indexStart = static_cast<uint32_t>(indices.size());
			// 재질 슬롯 = glTF 재질 배열의 인덱스. 재질이 없는 primitive 는 마지막 슬롯(기본 재질).
			submesh.materialSlot = primitive.material != nullptr ? static_cast<uint32_t>(primitive.material - data->materials) : defaultSlot;
			if (primitive.indices != nullptr)
			{
				for (cgltf_size i = 0; i < primitive.indices->count; ++i)
				{
					indices.push_back(baseVertex + static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, i)));
				}
			}
			else
			{
				for (cgltf_size i = 0; i < vertexCount; ++i) indices.push_back(baseVertex + static_cast<uint32_t>(i));
			}
			submesh.indexCount = static_cast<uint32_t>(indices.size()) - submesh.indexStart;
			submeshes.push_back(submesh);
		}

		if (vertices.empty() || indices.empty()) return false;

		if (needTangents && options.generateMissingTangents)
		{
			// glTF 노멀 맵은 초록 = 위(v 감소) 이므로 B = −dP/dv 가 되도록 w 를 만든다.
			Mesh::ComputeTangents(vertices, indices, false);
			++stats.generatedTangentMeshes;
		}
		if (vertices.size() <= 65535) ++stats.meshesWith16BitIndices;
		stats.vertices += static_cast<uint32_t>(vertices.size());
		stats.triangles += static_cast<uint32_t>(indices.size() / 3);
		stats.submeshes += static_cast<uint32_t>(submeshes.size());
		outMesh = Mesh::FromData(std::move(vertices), std::move(indices), std::move(submeshes));
		return true;
	}
}

bool ModelLoader::LoadGltf(const std::wstring& path, const Options& options, Model& out)
{
	const auto startTime = std::chrono::steady_clock::now();
	out = Model{};

	const std::string utf8Path = ToUtf8(path);
	cgltf_options parseOptions = {};
	cgltf_data* data = nullptr;
	cgltf_result result = cgltf_parse_file(&parseOptions, utf8Path.c_str(), &data);
	if (result != cgltf_result_success)
	{
		Log::Error("glTF 파싱 실패 (%s): %s", ResultName(result), utf8Path.c_str());
		return false;
	}
	result = cgltf_load_buffers(&parseOptions, data, utf8Path.c_str());
	if (result != cgltf_result_success)
	{
		Log::Error("glTF 버퍼 로드 실패 (%s): %s", ResultName(result), utf8Path.c_str());
		cgltf_free(data);
		return false;
	}
	result = cgltf_validate(data);
	if (result != cgltf_result_success)
	{
		Log::Warn("glTF 검증 경고 (%s): %s", ResultName(result), utf8Path.c_str());
	}

	// 모델 이름 = 파일 이름(확장자 제외). 이미지·재질 이름의 접두사.
	{
		const size_t slash = path.find_last_of(L"\\/");
		const std::wstring file = slash == std::wstring::npos ? path : path.substr(slash + 1);
		const size_t dot = file.find_last_of(L'.');
		out.name = ToUtf8(dot == std::wstring::npos ? file : file.substr(0, dot));
	}
	const size_t slash = path.find_last_of(L"\\/");
	const std::wstring directory = slash == std::wstring::npos ? L"" : path.substr(0, slash + 1);

	// ---- 이미지 ----
	out.images.reserve(data->images_count);
	for (cgltf_size i = 0; i < data->images_count; ++i)
	{
		ModelImage image;
		image.name = ImageName(out.name, data, &data->images[i]);
		if (!CollectImage(data, data->images[i], directory, image))
		{
			Log::Warn("모델 이미지 %zu 를 읽지 못함 (%s). 흰색으로 대체된다.", static_cast<size_t>(i), image.name.c_str());
			continue;
		}
		out.images.push_back(std::move(image));
	}

	// ---- 재질. 마지막에 "재질 없음" 슬롯을 하나 더 둔다 ----
	out.materials.reserve(data->materials_count + 1);
	for (cgltf_size i = 0; i < data->materials_count; ++i)
	{
		out.materials.push_back(ConvertMaterial(out.name, data, &data->materials[i], i));
	}
	out.materials.push_back(ConvertMaterial(out.name, data, nullptr, data->materials_count));
	const uint32_t defaultSlot = static_cast<uint32_t>(data->materials_count);

	// ---- 메시 ----
	std::vector<int> meshRemap(data->meshes_count, -1);   // glTF mesh 번호 → out.meshes 인덱스
	out.meshes.reserve(data->meshes_count);
	for (cgltf_size m = 0; m < data->meshes_count; ++m)
	{
		Mesh mesh;
		if (!ConvertMesh(data, data->meshes[m], options, defaultSlot, mesh, out.stats)) continue;
		meshRemap[m] = static_cast<int>(out.meshes.size());
		out.meshes.push_back(std::move(mesh));
	}

	// ---- 노드 → 인스턴스. 반사 M = diag(1,1,−1): 행벡터 행렬 A 에 대해 A' = M·A·M ----
	const XMMATRIX mirror = XMMatrixScaling(1.0f, 1.0f, options.flipZ ? -1.0f : 1.0f);
	const XMMATRIX rootScale = XMMatrixScaling(options.scale, options.scale, options.scale);
	XMVECTOR boundsMin = XMVectorReplicate(FLT_MAX);
	XMVECTOR boundsMax = XMVectorReplicate(-FLT_MAX);
	for (cgltf_size n = 0; n < data->nodes_count; ++n)
	{
		const cgltf_node& node = data->nodes[n];
		if (node.mesh == nullptr) continue;
		const int meshIndex = meshRemap[static_cast<size_t>(node.mesh - data->meshes)];
		if (meshIndex < 0) continue;

		float m[16];
		cgltf_node_transform_world(&node, m);
		const XMMATRIX world = mirror * LoadGltfMatrix(m) * mirror * rootScale;

		ModelInstance instance;
		instance.meshIndex = static_cast<uint32_t>(meshIndex);
		XMStoreFloat4x4(&instance.world, world);
		instance.name = node.name != nullptr ? node.name : (out.name + "/node" + std::to_string(n));
		out.instances.push_back(std::move(instance));

		// 바운드 합집합: 로컬 AABB 의 여덟 꼭짓점을 월드로.
		const Bounds& b = out.meshes[meshIndex].GetBounds();
		for (int corner = 0; corner < 8; ++corner)
		{
			const XMVECTOR p = XMVectorSet((corner & 1) ? b.max.x : b.min.x, (corner & 2) ? b.max.y : b.min.y, (corner & 4) ? b.max.z : b.min.z, 1.0f);
			const XMVECTOR w = XMVector3TransformCoord(p, world);
			boundsMin = XMVectorMin(boundsMin, w);
			boundsMax = XMVectorMax(boundsMax, w);
		}
	}
	if (!out.instances.empty())
	{
		XMStoreFloat3(&out.bounds.min, boundsMin);
		XMStoreFloat3(&out.bounds.max, boundsMax);
	}

	out.stats.meshes = static_cast<uint32_t>(out.meshes.size());
	out.stats.instances = static_cast<uint32_t>(out.instances.size());
	out.stats.materials = static_cast<uint32_t>(out.materials.size());
	out.stats.images = static_cast<uint32_t>(out.images.size());
	cgltf_free(data);

	out.stats.loadMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startTime).count();
	Log::Info("모델 로드: %s — 메시 %u (서브메시 %u, 인스턴스 %u), 재질 %u, 이미지 %u, 정점 %u, 삼각형 %u, 16비트 인덱스 %u, 탄젠트 생성 %u, 건너뜀 %u, %.0f ms",
		out.name.c_str(), out.stats.meshes, out.stats.submeshes, out.stats.instances, out.stats.materials, out.stats.images,
		out.stats.vertices, out.stats.triangles, out.stats.meshesWith16BitIndices, out.stats.generatedTangentMeshes, out.stats.skippedPrimitives, out.stats.loadMilliseconds);
	Log::Info("모델 바운드: (%.2f, %.2f, %.2f) ~ (%.2f, %.2f, %.2f)", out.bounds.min.x, out.bounds.min.y, out.bounds.min.z, out.bounds.max.x, out.bounds.max.y, out.bounds.max.z);
	return out.IsValid();
}
