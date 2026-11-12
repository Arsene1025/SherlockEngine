#include "pch.h"
#include "Graphics/Model.h"
#include "Graphics/Import/ImportCommon.h"
#include "Core/Log.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>
#include <string_view>
#include <nlohmann/json.hpp>

using namespace DirectX;
using json = nlohmann::json;

/*
Json 다루기 용으로 nlohmann을 사용.
glTF에서 JSON은 어디에 무엇이 있는지 설명.
바이너리(.bin 파일, base64문자열, GLB안의 청크)가 정점.인덱스.이미지의 바이트를 담고 있음
JSON안의 요소는 전부 배열이고 서로를 배열 번호로 가리킴.

"nodes":    [ { "mesh": 0 } ]                                     ← nodes[0] 은 meshes[0] 을 그린다
"meshes":   [ { "primitives": [ {
				 "attributes": { "POSITION": 0, "NORMAL": 1 },    ← accessors[0], accessors[1]
				 "indices": 3,                                    ← accessors[3]
				 "material": 0 } ] } ]                            ← materials[0]
"accessors":[ ... ]   ← 0번부터 3번까지, 각각 bufferViews[n] 을 가리킴

URI(Uniform Resource Identifier) : 자원이 어디에 있는지(혹은 그 자원 자체)를 문자열로 적은 주소
buffers[i]나 images[i]에 있는 uri는 해당 바이트 데이터가 어디 있는지 알려줌.
예시1 - 주소를 가리키는 경우 : "buffers": [ { "uri": "Triangle.bin", "byteLength": 1234 } ],
						   "images":  [ { "uri": "textures/wood.png" } ]
예시2 - 데이터를 직접 담은 경우 : "uri": "data:application/octet-stream;base64,AAABAAIAAAA..."
	   data:타입;인코딩,실제데이터의 형식으로 쉼표뒤 base64문자열을 디코딩 해서 사용

애니메이션 구조
animation
 ├─ channels[]  "어느 노드(target.node)의 무엇(path)을 바꿀지"
 │               path = translation | rotation | scale
 └─ samplers[]  "그 값이 어떻게 변하는지"
	   input  accessor → 키프레임 시각들  [0.0, 0.5, 1.0, ...]
	   output accessor → 각 시각의 값들   [값0, 값1, 값2, ...]
	   interpolation → LINEAR | STEP | CUBICSPLINE


데이터는 아래에서 위로 쌓임.
바이트(buffer) → 그 구간(bufferView) → 그 구간을 해석하는 방법(accessor) → 그것을 쓰는 곳(primitive)

LoadBuffers → LoadImages → LoadMaterials → LoadMeshes → BuildInstances -> LoadNodes ->  LoadAnimations
(바이트)      (이미지)      (재질)           (메시)        (노드 = 인스턴스)   (노드 TRS)        (키프레임)

JSON을 담는 방식 세 가지
방식							구성										바이너리가 있는 곳
분리형						모델.gltf + 모델.bin + 이미지 파일들		buffers[i].uri 가 파일 이름 ("Triangle_PosColor.bin")
내장형						모델.gltf 하나							uri 가 "data:application/octet-stream;base64,…". 파일이 base64 때문에 약 1/3 커짐
GLB							모델.glb 하나								파일 안의 BIN 청크. 이 버퍼는 uri 가 아예 없음


glb 파일의 파일 구조
오프셋  크기  내용
0       4     magic   = 0x46546C67   (바이트로는 67 6C 54 46 = ASCII "glTF")
4       4     version = 2
8       4     length  = 파일 전체 길이(바이트)
-------------------------------- 여기까지 12바이트 = GLB 헤더
12      4     chunk0 길이
16      4     chunk0 타입   (JSON → "JSON")
20      N     chunk0 데이터  ← JSON
..      4     chunk1 길이
..      4     chunk1 타입   (BIN → "BIN\0")
..      M     chunk1 데이터  ← 정점/인덱스/이미지 바이트

*/


namespace
{
	constexpr uint32_t kGlbMagic = 0x46546C67;
	constexpr uint32_t kGlbChunkJson = 0x4E4F534A;
	constexpr uint32_t kGlbChunkBin = 0x004E4942;

	//지정한 부분 p부터 4바이트(32비트) 읽어들이기
	uint32_t ReadU32(const uint8_t* p)
	{
		uint32_t value;
		std::memcpy(&value, p, sizeof(value));
		return value;
	}

	//파일 전체 바이트를 받아서 .gltf인지 .glb인지 판독
	//root(파싱된 Json객체), glbBin(glb의 bin청크 바이트, .gltf면 빈 채로 남음) 2가지 리턴
	//.gltf(맨 앞 4바이트가 magic이 아님) -> 파일 전체가 json이라는 의미이므로 그대로 파싱해서 root만 채우기
	//.glb 맨 앞 4바이트가 gltf -> 청크를 순회하며 root와 glbbin둘 다 채움
	bool ParseContainer(const std::vector<uint8_t>& bytes, json& root, std::vector<uint8_t>& glbBin, std::string& error)
	{
		glbBin.clear();

		const uint8_t* jsonBegin = bytes.data();
		const uint8_t* jsonEnd = bytes.data() + bytes.size();

		//앞에 4바이트 읽어서 GLB인지 .gltf(Json)인지 확인
		if (bytes.size() >= 4 && ReadU32(bytes.data()) == kGlbMagic)
		{
			//파일 맨 앞 12바이트짜리 헤더를 먼저 읽어야 함.
			if (bytes.size() < 12) { error = "GLB 헤더가 잘림"; return false; }

			//Version과 총 길이
			const uint32_t version = ReadU32(bytes.data() + 4);
			const uint32_t totalLength = ReadU32(bytes.data() + 8);
			//glTF ver.2버전만 지원
			if (version != 2) { error = std::format("지원하지 않는 GLB 버전 {}", version); return false; }
			//실제 파일과 헤더의 길이 비교(잘렸는지 확인)
			if (totalLength > bytes.size()) { error = "GLB 헤더의 길이가 실제 파일보다 큼 (잘린 파일)"; return false; }

			bool foundJson = false;

			//GLB헤더 크기
			size_t offset = 12;
			//남은 공간이 청크 헤더(8바이트) 크기보다 같거나 클 때만 반복.
			while (offset + 8 <= totalLength)
			{
				// 청크의 길이와 타입이 적힌거 합이 8이어서 +8해야함
				const uint32_t chunkLength = ReadU32(bytes.data() + offset);
				const uint32_t chunkType = ReadU32(bytes.data() + offset + 4);
				//현제 청크의 데이터 시작 지점으로 이동 (현재 위치 + 헤더 길이)
				offset += 8;

				//정상적이라면 전체 길이 - 데이터 시작 지점 이 청크의 길이보다 작을 수 없음.
				if (chunkLength > totalLength - offset) { error = "GLB 청크 길이가 파일 범위를 벗어남"; return false; }

				//실제 데이터위치는 파일 시작 지점 + 데이터 시작 지점으로 구할 수 있음
				const uint8_t* data = bytes.data() + offset;

				//청크 type이 json이면
				if (chunkType == kGlbChunkJson && !foundJson)
				{
					//Json의 청크 구간만 잡기
					foundJson = true;
					jsonBegin = data;
					jsonEnd = data + chunkLength;
				}
				else if (chunkType == kGlbChunkBin && glbBin.empty())
				{
					//bin이면
					//복사해서 glbbin에 보관함.
					glbBin.assign(data, data + chunkLength);
				}
				//다음 청크로 offset설정
				offset += chunkLength;
			}
			//json못찾으면 오류
			if (!foundJson) { error = "GLB 에 JSON 청크가 없음"; return false; }
		}

		//json범위 구한걸로 파싱
		root = json::parse(jsonBegin, jsonEnd, nullptr,  false);
		//json유효 한지 검사.
		if (root.is_discarded()) { error = "JSON 문법 오류"; return false; }
		if (!root.is_object()) { error = "JSON 루트가 객체가 아님"; return false; }
		return true;
	}

	//char를 6비트 값(0~63)으로 변경, DecodeBase64에서 디코딩
	int Base64Value(char c)
	{
		if (c >= 'A' && c <= 'Z') return c - 'A';
		if (c >= 'a' && c <= 'z') return c - 'a' + 26;
		if (c >= '0' && c <= '9') return c - '0' + 52;
		if (c == '+') return 62;
		if (c == '/') return 63;
		return -1;
	}

	//base64 글자 문자열을 원래 바이트로 되돌림
	//6비트씩 쌓다가 8비트가 되면 한 바이트를 꺼내는 방식.
	/*
	"TWFu" → "Man"

	T=19(010011)  W=22(010110)  F=5(000101)  u=46(101110)

	T 읽음: bits=6											 (아직 8 미만)
	W 읽음: bits=12 → 8 이상 → 꺼냄 → 01001101 = 0x4D = 'M'    (남은 bits=4)
	F 읽음: bits=10 → 꺼냄 → 01100001 = 0x61 = 'a'            (남은 bits=2)
	u 읽음: bits=8  → 꺼냄 → 01101110 = 0x6E = 'n'            (남은 bits=0)

	*/
	bool DecodeBase64(std::string_view text, std::vector<uint8_t>& out)
	{
		out.clear();
		out.reserve(text.size() / 4 * 3);

		
		uint32_t accumulator = 0; //비트 저장용
		int bits = 0;			  //저장된 비트 수
		for (const char c : text)
		{
			if (c == '=') break;											 //패딩이면 끝
			if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;   //공백, 개행 무시
			const int value = Base64Value(c);								 //0~63으로 변환
			if (value < 0) return false;									 //범위 밖은 base64아님
			accumulator = (accumulator << 6) | static_cast<uint32_t>(value); //6비트를 아래에 붙임
			bits += 6;
			if (bits >= 8)													 //한 바이트가 모였으면
			{
				bits -= 8;													 //8비트 꺼냄
				out.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xFF));
			}
		}
		return true;
	}

	
	/*
	URI에서 특수 문자는 %뒤에 16진수 두 자리로 인코딩 됨. ex) My%20Texture.png(%20은 공백)
	외부 파일을 가리키는 uri에 이런 표기가 들어있으면 파일 경로로 쓰지 못함.
	DecodeUriEscapes을 통해 원래 글자로 되돌리기
	*/
	std::string DecodeUriEscapes(const std::string& uri)
	{
		//'0'~'9', 'a'~'f', 'A'~'F'를 0~15로 변경, 실패시 -1 반환
		const auto hexValue = [](char c) -> int
			{
				if (c >= '0' && c <= '9') return c - '0';
				if (c >= 'a' && c <= 'f') return c - 'a' + 10;
				if (c >= 'A' && c <= 'F') return c - 'A' + 10;
				return -1;
			};
		std::string decoded;
		decoded.reserve(uri.size());

		//%뒤 두 글자가 모두 유효하면(-1이 아니면) 상위 * 16 + 하위로 글자 하나를 생성
		//그리고 i+2로 글자 생성한 부분 을 건너뜀
		//아니면 그 글자 그대로 복사
		for (size_t i = 0; i < uri.size(); ++i)
		{
			if (uri[i] == '%' && i + 2 < uri.size() && hexValue(uri[i + 1]) >= 0 && hexValue(uri[i + 2]) >= 0)
			{
				decoded.push_back(static_cast<char>(hexValue(uri[i + 1]) * 16 + hexValue(uri[i + 2])));
				i += 2;
			}
			else decoded.push_back(uri[i]);
		}
		return decoded;
	}

	//uri하나를 byte로 읽기
	bool LoadUriBytes(const std::string& uri, const std::wstring& directory, std::vector<uint8_t>& out, std::string& error)
	{
		//data:...;base64,XXXX 형식이면 쉼표를 찾음
		//그리고 그 뒤의 Base64들을 decode함.
		if (uri.starts_with("data:"))
		{
			const size_t comma = uri.find(',');
			if (comma == std::string::npos) { error = "data URI 에 ',' 가 없음"; return false; }
			if (uri.substr(0, comma).find(";base64") == std::string::npos) { error = "base64 가 아닌 data URI 는 지원하지 않음"; return false; }
			if (!DecodeBase64(std::string_view(uri).substr(comma + 1), out)) { error = "base64 디코딩 실패"; return false; }
			return true;
		}

		//그 이외의 경우(파일 이름)
		//directory + DecodeUriEscapes(uri) → '/'를 '\\'로 바꿔서 ReadWholeFile
		std::wstring filePath = directory + ModelImport::ToWide(DecodeUriEscapes(uri));
		std::replace(filePath.begin(), filePath.end(), L'/', L'\\');
		if (!ModelImport::ReadWholeFile(filePath, out)) { error = "파일을 읽을 수 없음: " + ModelImport::ToUtf8(filePath); return false; }
		return true;
	}

	//buffers[]를 전부 로딩.
	bool LoadBuffers(const json& root, std::vector<uint8_t>& glbBin, const std::wstring& directory,
		std::vector<std::vector<uint8_t>>& buffers, std::string& error)
	{
		buffers.clear();
		const auto found = root.find("buffers");
		if (found == root.end()) return true;
		if (!found->is_array()) { error = "buffers 가 배열이 아님"; return false; }

		buffers.resize(found->size());
		for (size_t i = 0; i < found->size(); ++i)
		{
			const json& entry = (*found)[i];
			if (!entry.is_object()) { error = std::format("buffers[{}] 가 객체가 아님", i); return false; }

			//byteLength가 필수, 길이는 음수일 수 가 없음.
			const auto length = entry.find("byteLength");
			if (length == entry.end() || !length->is_number_unsigned()) { error = std::format("buffers[{}].byteLength 가 없거나 잘못됨", i); return false; }
			const size_t byteLength = length->get<size_t>();

			//Uri가 있는지 
			const auto uri = entry.find("uri");
			//uri가 없으면 glb의 bin 청크를 사용하는 경우. glbbin이 비어있으면 안됨.
			//복사가 되지 않게 move로 옮기기
			if (uri == entry.end())
			{
				if (i != 0 || glbBin.empty()) { error = std::format("buffers[{}] 에 uri 가 없는데 GLB BIN 청크도 없음", i); return false; }
				buffers[i] = std::move(glbBin);
			}
			//uri가 있으면 LoadUriBytes사용해서 읽기
			else
			{
				if (!uri->is_string()) { error = std::format("buffers[{}].uri 가 문자열이 아님", i); return false; }
				std::string reason;
				if (!LoadUriBytes(uri->get<std::string>(), directory, buffers[i], reason)) { error = std::format("buffers[{}]: {}", i, reason); return false; }
			}
			//실제 크기가 byteLength보다 작으면 오류임.
			if (buffers[i].size() < byteLength) { error = std::format("buffers[{}] 가 byteLength({})보다 짧음 ({})", i, byteLength, buffers[i].size()); return false; }
		}
		return true;
	}
	//OpenGL상수와 같은 숫자
	constexpr int kComponentByte = 5120;		 //BYTE (부호있는 8비트)
	constexpr int kComponentUnsignedByte = 5121; //UNSIGNED_BYTE
	constexpr int kComponentShort = 5122;		 //Short
	constexpr int kComponentUnsignedShort = 5123;//U_Short
	constexpr int kComponentUnsignedInt = 5125;  //u_int
	constexpr int kComponentFloat = 5126;        //float

	//읽을 준비가 끝난 accessor
	struct AccessorView
	{
		const uint8_t* data = nullptr; //첫 번째 요소가 있는 메모리 주소
		size_t count = 0;			   //요소 개수(ex 정점 수)
		size_t stride = 0;			   //요소와 요소 사이 간격(byte)
		int componentType = 0;         //성분 하나의 타입
		int componentCount = 0;        //요소가 가진 성분 수(Vector4 면 4)
		bool normalized = false;       //정수를 0~1 or -1~1로 정규화 할지
	};

	//성분 타입에 따라 크기 반환
	size_t ComponentSize(int componentType)
	{
		switch (componentType)
		{
		case kComponentByte: case kComponentUnsignedByte: return 1;
		case kComponentShort: case kComponentUnsignedShort: return 2;
		case kComponentUnsignedInt: case kComponentFloat: return 4;
		default: return 0;
		}
	}

	//입력받은 문자열을 성분의 수로 리턴
	int ComponentCountOf(const std::string& type)
	{
		if (type == "SCALAR") return 1;
		if (type == "VEC2") return 2;
		if (type == "VEC3") return 3;
		if (type == "VEC4") return 4;
		if (type == "MAT2") return 4;
		if (type == "MAT3") return 9;
		if (type == "MAT4") return 16;
		return 0;
	}

	//Json에서 정수를 읽기
	bool ReadSize(const json& obj, const char* key, bool required, size_t defaultValue, size_t& out)
	{
		const auto found = obj.find(key);
		if (found == obj.end())
		{
			out = defaultValue;
			return !required;
		}
		if (!found->is_number_unsigned()) return false;
		out = found->get<size_t>();
		return true;
	}

	//Accessor로드
	bool LoadAccessor(const json& root, const std::vector<std::vector<uint8_t>>& buffers, size_t index, AccessorView& out, std::string& error)
	{
		//출력용 AccessorView만들기
		out = AccessorView{};

		//accessor찾기
		const auto accessors = root.find("accessors");
		//존재하지 않으면 오류
		if (accessors == root.end() || !accessors->is_array() || index >= accessors->size())
		{
			error = std::format("accessors[{}] 가 없음", index);
			return false;
		}
		//accessor유효한지 검사
		const json& accessor = (*accessors)[index];
		if (!accessor.is_object()) { error = std::format("accessors[{}] 가 객체가 아님", index); return false; }
		if (accessor.contains("sparse")) { error = std::format("accessors[{}]: sparse accessor 는 지원하지 않음", index); return false; }

		//view안에 대입할 정보들 선언
		size_t componentType = 0, count = 0, bufferViewIndex = 0, accessorOffset = 0;
		//그 정보들 있는지 확인
		if (!ReadSize(accessor, "componentType", true, 0, componentType) || !ReadSize(accessor, "count", true, 0, count)
			|| !ReadSize(accessor, "bufferView", true, 0, bufferViewIndex) || !ReadSize(accessor, "byteOffset", false, 0, accessorOffset))
		{
			error = std::format("accessors[{}]: componentType/count/bufferView/byteOffset 가 없거나 정수가 아님 (bufferView 없는 accessor 는 지원하지 않음)", index);
			return false;
		}
		//type찾기
		const auto typeField = accessor.find("type");
		if (typeField == accessor.end() || !typeField->is_string()) { error = std::format("accessors[{}].type 이 없거나 문자열이 아님", index); return false; }

		//타입의 크기와 성분 수 확인
		const size_t componentSize = ComponentSize(static_cast<int>(componentType));
		const int componentCount = ComponentCountOf(typeField->get<std::string>());
		if (componentSize == 0) { error = std::format("accessors[{}]: 알 수 없는 componentType {}", index, componentType); return false; }
		if (componentCount == 0) { error = std::format("accessors[{}]: 알 수 없는 type '{}'", index, typeField->get<std::string>()); return false; }
		//요소 크기 성분수 * 타입 크기
		const size_t elementSize = componentSize * static_cast<size_t>(componentCount);

		//View찾기
		const auto views = root.find("bufferViews");
		if (views == root.end() || !views->is_array() || bufferViewIndex >= views->size())
		{
			error = std::format("accessors[{}]: bufferViews[{}] 가 없음", index, bufferViewIndex);
			return false;
		}
		//view에 정보들 채워넣기
		const json& view = (*views)[bufferViewIndex];
		size_t bufferIndex = 0, viewOffset = 0, viewLength = 0, byteStride = 0;
		if (!view.is_object() || !ReadSize(view, "buffer", true, 0, bufferIndex) || !ReadSize(view, "byteLength", true, 0, viewLength)
			|| !ReadSize(view, "byteOffset", false, 0, viewOffset) || !ReadSize(view, "byteStride", false, 0, byteStride))
		{
			error = std::format("bufferViews[{}]: buffer/byteLength/byteOffset/byteStride 가 없거나 정수가 아님", bufferViewIndex);
			return false;
		}
		if (bufferIndex >= buffers.size()) { error = std::format("bufferViews[{}]: buffers[{}] 가 없음", bufferViewIndex, bufferIndex); return false; }
		//생성한 버퍼 넣기?
		const std::vector<uint8_t>& buffer = buffers[bufferIndex];

		//view 구간이 버퍼 안에 들어가는지 검사
		//더하지 않고 빼서 비교하는 건 오버플로 방지
		if (viewOffset > buffer.size() || viewLength > buffer.size() - viewOffset)
		{
			error = std::format("bufferViews[{}] 구간이 buffers[{}] 크기({})를 벗어남", bufferViewIndex, bufferIndex, buffer.size());
			return false;
		}

		//byteStride가 없으면(0) 요소들이 빈틈없이 붙어있는 것이라 stride = 요소 크기
		//stride가 요소 크기보다 작으면 요소끼리 겹치니까 오류
		const size_t stride = byteStride != 0 ? byteStride : elementSize;
		if (stride < elementSize) { error = std::format("bufferViews[{}].byteStride({})가 요소 크기({})보다 작음", bufferViewIndex, stride, elementSize); return false; }

		//마지막 요소까지 view 구간 안에 들어가는지 검사
		//accessorOffset + (count-1) * stride + 요소 크기 <= viewLength 를 오버플로 안 나게 바꾼 식
		if (count > 0)
		{
			if (accessorOffset > viewLength || viewLength - accessorOffset < elementSize || (count - 1) > (viewLength - accessorOffset - elementSize) / stride)
			{
				error = std::format("accessors[{}]: 요소 {}개가 bufferViews[{}] 구간(길이 {})을 벗어남", index, count, bufferViewIndex, viewLength);
				return false;
			}
		}

		//첫 요소 주소 = 버퍼 시작 + view 시작 + accessor 시작
		out.data = buffer.data() + viewOffset + accessorOffset;
		out.count = count;
		out.stride = stride;
		out.componentType = static_cast<int>(componentType);
		out.componentCount = componentCount;
		//normalized는 선택 필드. 없거나 bool이 아니면 false
		const auto normalized = accessor.find("normalized");
		out.normalized = normalized != accessor.end() && normalized->is_boolean() && normalized->get<bool>();
		return true;
	}

	//Compoenet float로 변환
	float ComponentToFloat(const uint8_t* p, int componentType, bool normalized)
	{
		switch (componentType)
		{
		//값을 memcpy로 읽는 건 정렬 문제 없이 읽기 위해서(ReadU32와 같음)
		case kComponentFloat: { float v; std::memcpy(&v, p, sizeof(v)); return v; }
		//normalized면 정수를 최댓값으로 나눠 -1~1로. -128은 -1보다 작아져서 max로 -1에 맞춤
		case kComponentByte: { int8_t v; std::memcpy(&v, p, sizeof(v)); return normalized ? (std::max)(v / 127.0f, -1.0f) : static_cast<float>(v); }
		//부호 없는 정수는 최댓값으로 나눠 0~1로
		case kComponentUnsignedByte: { uint8_t v; std::memcpy(&v, p, sizeof(v)); return normalized ? v / 255.0f : static_cast<float>(v); }
		case kComponentShort: { int16_t v; std::memcpy(&v, p, sizeof(v)); return normalized ? (std::max)(v / 32767.0f, -1.0f) : static_cast<float>(v); }
		case kComponentUnsignedShort: { uint16_t v; std::memcpy(&v, p, sizeof(v)); return normalized ? v / 65535.0f : static_cast<float>(v); }
		//UNSIGNED_INT는 정규화 대상이 아니라서 그대로 float로
		case kComponentUnsignedInt: { uint32_t v; std::memcpy(&v, p, sizeof(v)); return static_cast<float>(v); }
		default: return 0.0f;
		}
	}

	//float 읽기
	bool ReadFloat(const AccessorView& view, size_t i, float* out, size_t n)
	{
		//범위 밖이거나 요소가 가진 성분 수보다 많이 읽으려 하면 실패
		if (i >= view.count || n > static_cast<size_t>(view.componentCount)) return false;
		//i번째 요소의 시작 주소 = 시작 주소 + i * stride
		const uint8_t* element = view.data + i * view.stride;
		const size_t componentSize = ComponentSize(view.componentType);
		//성분을 하나씩 float로 변환. c번째 성분의 위치는 요소 시작 + c * 성분 크기
		for (size_t c = 0; c < n; ++c) out[c] = ComponentToFloat(element + c * componentSize, view.componentType, view.normalized);
		return true;
	}

	//index확인
	bool ReadIndex(const AccessorView& view, size_t i, uint32_t& out)
	{
		//index는 SCALAR(성분 1개)여야 함
		if (i >= view.count || view.componentCount != 1) return false;
		//i번째 index의 주소
		const uint8_t* p = view.data + i * view.stride;
		//index는 부호 없는 정수(1, 2, 4바이트)만 가능. 크기가 달라도 uint32로 통일해서 반환
		switch (view.componentType)
		{
		case kComponentUnsignedByte: { uint8_t v; std::memcpy(&v, p, sizeof(v)); out = v; return true; }
		case kComponentUnsignedShort: { uint16_t v; std::memcpy(&v, p, sizeof(v)); out = v; return true; }
		case kComponentUnsignedInt: { uint32_t v; std::memcpy(&v, p, sizeof(v)); out = v; return true; }
		default: return false;
		}
	}
	//ImageName
	std::string ImageName(const std::string& modelName, const json& root, size_t imageIndex);
	//imageLoad하기
	bool LoadImages(const json& root, const std::vector<std::vector<uint8_t>>& buffers, const std::wstring& directory,
		const std::string& modelName, std::vector<ModelImage>& outImages, std::string& error)
	{
		outImages.clear();

		//images가 없으면 이미지 없는 모델
		const auto images = root.find("images");
		if (images == root.end()) return true;
		if (!images->is_array()) { error = "images 가 배열이 아님"; return false; }

		outImages.reserve(images->size());
		for (size_t i = 0; i < images->size(); ++i)
		{
			const json& image = (*images)[i];
			//이미지 이름. 재질의 텍스처 이름과 연결하는 데 쓰임(TextureName도 ImageName을 사용)
			const std::string name = ImageName(modelName, root, i);
			std::vector<uint8_t> bytes;
			std::string reason;

			//이미지 바이트를 얻는 방법은 두 가지
			//bufferView(GLB처럼 버퍼 안에 들어있는 경우) 또는 uri(파일, data URI)
			//실패하면 reason에 이유를 적음
			if (!image.is_object())
			{
				reason = "images 원소가 객체가 아님";
			}
			else if (image.contains("bufferView"))
			{
				//bufferView 번호 → view 필드 → 버퍼 범위 순서로 검사하고, 통과하면 그 구간만 복사
				size_t viewIndex = 0, bufferIndex = 0, viewOffset = 0, viewLength = 0;
				const auto views = root.find("bufferViews");
				if (!ReadSize(image, "bufferView", true, 0, viewIndex) || views == root.end() || !views->is_array() || viewIndex >= views->size())
				{
					reason = "bufferView 번호가 잘못됨";
				}
				else if (const json& view = (*views)[viewIndex];
					!view.is_object() || !ReadSize(view, "buffer", true, 0, bufferIndex) || !ReadSize(view, "byteLength", true, 0, viewLength)
					|| !ReadSize(view, "byteOffset", false, 0, viewOffset) || bufferIndex >= buffers.size())
				{
					reason = "bufferView 의 buffer/byteOffset/byteLength 가 잘못됨";
				}
				else if (const std::vector<uint8_t>& buffer = buffers[bufferIndex]; viewOffset > buffer.size() || viewLength > buffer.size() - viewOffset)
				{
					reason = "bufferView 구간이 버퍼를 벗어남";
				}
				else
				{
					bytes.assign(buffer.begin() + viewOffset, buffer.begin() + viewOffset + viewLength);
				}
			}
			//uri가 있으면 LoadUriBytes로 읽음(파일 or base64)
			else if (const auto uri = image.find("uri"); uri != image.end() && uri->is_string())
			{
				LoadUriBytes(uri->get<std::string>(), directory, bytes, reason);
			}
			else
			{
				reason = "bufferView 도 uri 도 없음";
			}

			//읽지 못했거나 비어있으면 경고만 남기고 이 이미지는 건너뜀(흰색으로 대체됨)
			//이미지 하나 때문에 모델 전체가 실패하지 않게
			if (!reason.empty() || bytes.empty())
			{
				Log::Warn("모델 이미지 %zu 를 읽지 못함 (%s): %s. 흰색으로 대체된다.", i, name.c_str(), reason.empty() ? "빈 데이터" : reason.c_str());
				continue;
			}

			//인코딩된(png, jpg 등) 바이트를 이름과 함께 보관
			ModelImage entry;
			entry.name = name;
			entry.encoded = std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
			outImages.push_back(std::move(entry));
		}
		return true;
	}

	//이미지 이름 만들기. 기본은 '모델이름/번호'
	//uri가 있으면 뒤에 붙이고, 없으면 name을 붙임
	std::string ImageName(const std::string& modelName, const json& root, size_t imageIndex)
	{
		std::string name = std::format("{}/{}", modelName, imageIndex);

		//images가 없거나 번호가 범위 밖이면 기본 이름만
		const auto images = root.find("images");
		if (images == root.end() || !images->is_array() || imageIndex >= images->size()) return name;
		const json& image = (*images)[imageIndex];
		if (!image.is_object()) return name;

		const auto uri = image.find("uri");
		const auto label = image.find("name");
		if (uri != image.end() && uri->is_string()) name += " " + uri->get<std::string>();
		else if (label != image.end() && label->is_string()) name += " " + label->get<std::string>();
		return name;
	}

	Material ConvertMaterial(const std::string& modelName, const json& root, const json* source, size_t index);

	//materials[]를 Material로 변환
	//맨 끝에 기본 재질을 하나 더 추가함(primitive에 material이 없을 때 쓰려고)
	bool LoadMaterials(const json& root, const std::string& modelName, std::vector<Material>& outMaterials, std::string& error)
	{
		outMaterials.clear();

		//materials가 없어도 오류가 아님. 이 경우 기본 재질만 생김
		size_t count = 0;
		const auto materials = root.find("materials");
		if (materials != root.end())
		{
			if (!materials->is_array()) { error = "materials 가 배열이 아님"; return false; }
			count = materials->size();
		}

		outMaterials.reserve(count + 1);
		for (size_t i = 0; i < count; ++i)
		{
			outMaterials.push_back(ConvertMaterial(modelName, root, &(*materials)[i], i));
		}
		//source가 nullptr이면 기본 재질
		outMaterials.push_back(ConvertMaterial(modelName, root, nullptr, count));
		return true;
	}

	//Json에서 float 읽기. 없거나 숫자가 아니면 기본값
	float JsonFloat(const json& obj, const char* key, float defaultValue)
	{
		const auto found = obj.find(key);
		return (found != obj.end() && found->is_number()) ? found->get<float>() : defaultValue;
	}

	//Json 배열에서 float n개 읽기. 배열 크기가 n과 다르거나 숫자가 아닌 게 있으면 false
	bool JsonFloats(const json& obj, const char* key, float* out, size_t n)
	{
		const auto found = obj.find(key);
		if (found == obj.end() || !found->is_array() || found->size() != n) return false;
		//out에 바로 쓰지 않고 임시 배열에 먼저 읽음(중간에 실패해도 out이 망가지지 않게). 최대 16개 = 4x4 행렬
		float values[16] = {};
		if (n > 16) return false;
		for (size_t i = 0; i < n; ++i)
		{
			if (!(*found)[i].is_number()) return false;
			values[i] = (*found)[i].get<float>();
		}
		for (size_t i = 0; i < n; ++i) out[i] = values[i];
		return true;
	}

	//Json에서 객체 찾기. 없거나 객체가 아니면 nullptr
	const json* JsonObject(const json& obj, const char* key)
	{
		const auto found = obj.find(key);
		return (found != obj.end() && found->is_object()) ? &*found : nullptr;
	}

	//textureInfo({ "index": N })의 index로 textures[N]을 찾음. 없으면 nullptr
	const json* FindTexture(const json& root, const json* textureInfo)
	{
		size_t index = 0;
		if (textureInfo == nullptr || !ReadSize(*textureInfo, "index", true, 0, index)) return nullptr;
		const auto textures = root.find("textures");
		if (textures == root.end() || !textures->is_array() || index >= textures->size()) return nullptr;
		const json& texture = (*textures)[index];
		return texture.is_object() ? &texture : nullptr;
	}

	//재질이 쓰는 텍스처의 이름. textures[index].source → images[source] → ImageName
	//못 찾으면 빈 문자열
	std::string TextureName(const std::string& modelName, const json& root, const json* textureInfo)
	{
		const json* texture = FindTexture(root, textureInfo);
		size_t imageIndex = 0;
		if (texture == nullptr || !ReadSize(*texture, "source", true, 0, imageIndex)) return "";
		const auto images = root.find("images");
		if (images == root.end() || !images->is_array() || imageIndex >= images->size()) return "";
		return ImageName(modelName, root, imageIndex);
	}

	//glTF sampler를 엔진의 SamplerPreset으로 단순화해서 변환
	//sampler가 없으면 기본(Anisotropic Wrap)
	SamplerPreset ToSamplerPreset(const json* sampler)
	{
		if (sampler == nullptr) return SamplerPreset::AnisotropicWrap;
		//OpenGL 상수. 33071 = CLAMP_TO_EDGE, 9728 = NEAREST
		constexpr size_t kClampToEdge = 33071;
		constexpr size_t kNearest = 9728;
		size_t wrapS = 0, wrapT = 0, magFilter = 0;
		ReadSize(*sampler, "wrapS", false, 0, wrapS);
		ReadSize(*sampler, "wrapT", false, 0, wrapT);
		ReadSize(*sampler, "magFilter", false, 0, magFilter);
		//확대 필터가 NEAREST면 Point, wrap이 CLAMP_TO_EDGE면 Clamp, 나머지는 기본값
		if (magFilter == kNearest) return SamplerPreset::PointWrap;
		if (wrapS == kClampToEdge || wrapT == kClampToEdge) return SamplerPreset::LinearClamp;
		return SamplerPreset::AnisotropicWrap;
	}

	//glTF material 하나를 Material로 변환. source가 nullptr이면 기본 재질
	Material ConvertMaterial(const std::string& modelName, const json& root, const json* source, size_t index)
	{
		Material material;
		material.name = std::format("{}/material{}", modelName, index);
		material.sampler = SamplerPreset::AnisotropicWrap;
		//source가 없으면(기본 재질) 기본값 그대로 반환
		if (source == nullptr || !source->is_object()) return material;

		//name이 있으면 이름으로 사용
		const auto name = source->find("name");
		if (name != source->end() && name->is_string() && !name->get<std::string>().empty()) material.name = name->get<std::string>();

		//metallic, roughness 기본값은 둘 다 1(glTF 기본값)
		material.metallic = 1.0f;
		material.roughness = 1.0f;
		//pbrMetallicRoughness : 기본 색, 금속성, 거칠기와 그 텍스처가 들어있는 객체
		if (const json* pbr = JsonObject(*source, "pbrMetallicRoughness"))
		{
			//baseColorFactor(rgba). 없으면 흰색
			float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			JsonFloats(*pbr, "baseColorFactor", color, 4);
			material.baseColor = XMFLOAT4(color[0], color[1], color[2], color[3]);
			//baseColor는 sRGB로 해석
			material.albedoSrgb = true;
			material.roughness = JsonFloat(*pbr, "roughnessFactor", 1.0f);
			material.metallic = JsonFloat(*pbr, "metallicFactor", 1.0f);

			//텍스처는 이름만 기록. 이미지와는 ImageName과 같은 이름으로 연결됨
			const json* baseColorTexture = JsonObject(*pbr, "baseColorTexture");
			material.albedoTexture = TextureName(modelName, root, baseColorTexture);
			material.metallicRoughnessTexture = TextureName(modelName, root, JsonObject(*pbr, "metallicRoughnessTexture"));

			//샘플러는 baseColor 텍스처의 sampler 하나만 재질 전체에 적용
			if (const json* texture = FindTexture(root, baseColorTexture))
			{
				size_t samplerIndex = 0;
				const auto samplers = root.find("samplers");
				if (ReadSize(*texture, "sampler", true, 0, samplerIndex) && samplers != root.end() && samplers->is_array()
					&& samplerIndex < samplers->size() && (*samplers)[samplerIndex].is_object())
				{
					material.sampler = ToSamplerPreset(&(*samplers)[samplerIndex]);
				}
			}
		}
		else
		{
			//pbrMetallicRoughness가 없는 재질은 금속이 아닌 것으로 처리
			material.metallic = 0.0f;
		}

		//occlusion(주변광 차폐) 텍스처와 강도. 텍스처가 없으면 강도 1
		const json* occlusion = JsonObject(*source, "occlusionTexture");
		material.occlusionTexture = TextureName(modelName, root, occlusion);
		material.occlusionStrength = (material.occlusionTexture.empty() || occlusion == nullptr) ? 1.0f : JsonFloat(*occlusion, "strength", 1.0f);

		//emissive(자체 발광) : 텍스처와 emissiveFactor(rgb)
		material.emissiveTexture = TextureName(modelName, root, JsonObject(*source, "emissiveTexture"));
		float emissive[3] = { 0.0f, 0.0f, 0.0f };
		JsonFloats(*source, "emissiveFactor", emissive, 3);
		//KHR_materials_emissive_strength 확장이 있으면 발광 세기를 곱함
		if (const json* extensions = JsonObject(*source, "extensions"))
		{
			if (const json* strength = JsonObject(*extensions, "KHR_materials_emissive_strength"))
			{
				const float s = JsonFloat(*strength, "emissiveStrength", 1.0f);
				for (float& e : emissive) e *= s;
			}
		}
		material.emissive = XMFLOAT3(emissive[0], emissive[1], emissive[2]);

		//normal map 텍스처와 scale(강도). scale이 0 이하면 1로
		const json* normal = JsonObject(*source, "normalTexture");
		material.normalTexture = TextureName(modelName, root, normal);
		if (!material.normalTexture.empty() && normal != nullptr)
		{
			const float scale = JsonFloat(*normal, "scale", 1.0f);
			material.normalStrength = scale > 0.0f ? scale : 1.0f;
		}

		//doubleSided : 뒷면도 그릴지 여부. 기본 false
		const auto doubleSided = source->find("doubleSided");
		material.doubleSided = doubleSided != source->end() && doubleSided->is_boolean() && doubleSided->get<bool>();

		//alphaMode가 MASK면 alphaCutoff(기본 0.5, 0 이하면 0.5)를 사용, BLEND도 0.5로 둠
		//OPAQUE(기본)면 건드리지 않음
		const auto alphaMode = source->find("alphaMode");
		if (alphaMode != source->end() && alphaMode->is_string())
		{
			const std::string mode = alphaMode->get<std::string>();
			if (mode == "MASK")
			{
				const float cutoff = JsonFloat(*source, "alphaCutoff", 0.5f);
				material.alphaCutoff = cutoff > 0.0f ? cutoff : 0.5f;
			}
			else if (mode == "BLEND")
			{
				material.alphaCutoff = 0.5f;
			}
		}
		return material;
	}

	bool ConvertMesh(const json& root, const std::vector<std::vector<uint8_t>>& buffers, const json& mesh,
		const ModelLoader::Options& options, uint32_t defaultSlot, Mesh& outMesh, ModelStats& stats);

	//meshes[]를 Mesh로 변환
	//meshRemap[JSON 번호] = outMeshes 번호. 변환에 실패한 mesh는 -1
	bool LoadMeshes(const json& root, const std::vector<std::vector<uint8_t>>& buffers, const ModelLoader::Options& options,
		uint32_t defaultSlot, std::vector<std::shared_ptr<const Mesh>>& outMeshes, std::vector<int>& meshRemap,
		ModelStats& stats, std::string& error)
	{
		outMeshes.clear();
		meshRemap.clear();

		const auto meshes = root.find("meshes");
		if (meshes == root.end()) return true;
		if (!meshes->is_array()) { error = "meshes 가 배열이 아님"; return false; }

		//전부 -1로 채워 둠
		meshRemap.assign(meshes->size(), -1);
		outMeshes.reserve(meshes->size());
		for (size_t m = 0; m < meshes->size(); ++m)
		{
			Mesh mesh;
			//변환에 실패하면 건너뜀(-1 유지). 그래서 JSON 번호와 outMeshes 번호가 달라질 수 있음
			if (!ConvertMesh(root, buffers, (*meshes)[m], options, defaultSlot, mesh, stats)) continue;
			meshRemap[m] = static_cast<int>(outMeshes.size());
			outMeshes.push_back(std::make_shared<const Mesh>(std::move(mesh)));
		}
		return true;
	}

	//primitive의 attributes에서 이름(POSITION 등)으로 accessor를 찾아 view로 로드
	//속성이 없거나 읽을 수 없으면 false
	bool FindAttribute(const json& root, const std::vector<std::vector<uint8_t>>& buffers, const json* attributes, const char* name, AccessorView& view)
	{
		size_t accessorIndex = 0;
		//속성이 없으면 조용히 false(선택 속성이라)
		if (attributes == nullptr || !attributes->contains(name)) return false;
		std::string reason;
		if (!ReadSize(*attributes, name, true, 0, accessorIndex) || !LoadAccessor(root, buffers, accessorIndex, view, reason))
		{
			//있는데 읽을 수 없는 경우만 경고
			Log::Warn("glTF 속성 %s 를 읽을 수 없어 건너뜀: %s", name, reason.empty() ? "accessor 번호가 정수가 아님" : reason.c_str());
			return false;
		}
		return true;
	}

	//mesh 하나를 Mesh로 변환
	//mesh 안의 primitive들을 하나의 정점, 인덱스 배열에 이어 붙이고
	//primitive마다 Submesh(인덱스 구간 + 재질)를 만듦
	bool ConvertMesh(const json& root, const std::vector<std::vector<uint8_t>>& buffers, const json& mesh,
		const ModelLoader::Options& options, uint32_t defaultSlot, Mesh& outMesh, ModelStats& stats)
	{
		std::vector<VERTEX> vertices;
		std::vector<VertexSkin> skin;   //vertices와 같은 길이로 채움(hasSkin이 거짓이면 마지막에 버림)
		bool hasSkin = false;           //JOINTS_0 + WEIGHTS_0 를 가진 primitive가 하나라도 있는지
		bool skinOverflow = false;      //조인트 번호가 255를 넘는 정점이 있는지(uint8 로 담을 수 없음)
		std::vector<uint32_t> indices;
		std::vector<Submesh> submeshes;
		bool needTangents = false;
		//flipZ면 z 부호를 뒤집어서 좌표계를 변환(glTF는 오른손 좌표계)
		const float zSign = options.flipZ ? -1.0f : 1.0f;

		const auto primitives = mesh.find("primitives");
		if (primitives == mesh.end() || !primitives->is_array()) return false;

		//primitive 하나 = 재질 하나로 그려지는 삼각형 묶음 = Submesh 하나
		for (size_t p = 0; p < primitives->size(); ++p)
		{
			const json& primitive = (*primitives)[p];
			if (!primitive.is_object()) { ++stats.skippedPrimitives; continue; }

			//primitive 그리기 모드. 기본 4(TRIANGLES)이고 이것만 지원. 나머지(선, 점 등)는 건너뜀
			size_t mode = 4;
			const json* attributes = JsonObject(primitive, "attributes");
			AccessorView position;
			//POSITION이 없거나 성분이 3개보다 적으면 건너뜀
			if (!ReadSize(primitive, "mode", false, 4, mode) || mode != 4
				|| !FindAttribute(root, buffers, attributes, "POSITION", position) || position.componentCount < 3)
			{
				++stats.skippedPrimitives;
				continue;
			}
			//나머지 속성은 선택. 있는 것만 읽음(법선, uv, 탄젠트, 정점 색)
			AccessorView normal, texcoord, tangent, color;
			const bool hasNormal = FindAttribute(root, buffers, attributes, "NORMAL", normal);
			const bool hasTexcoord = FindAttribute(root, buffers, attributes, "TEXCOORD_0", texcoord);
			const bool hasTangent = FindAttribute(root, buffers, attributes, "TANGENT", tangent);
			const bool hasColor = FindAttribute(root, buffers, attributes, "COLOR_0", color);
			//스키닝 속성. 조인트 번호와 가중치가 둘 다 있고 VEC4 일 때만 씀
			AccessorView joints, weights;
			const bool primitiveSkinned = FindAttribute(root, buffers, attributes, "JOINTS_0", joints) && joints.componentCount == 4
				&& FindAttribute(root, buffers, attributes, "WEIGHTS_0", weights) && weights.componentCount == 4
				&& joints.count == position.count && weights.count == position.count;
			if (primitiveSkinned) hasSkin = true;

			//합쳐진 정점 배열에서 이 primitive의 정점이 시작하는 위치. 인덱스에 더해 줌
			const uint32_t baseVertex = static_cast<uint32_t>(vertices.size());
			const size_t vertexCount = position.count;
			//local : 이 primitive 안에서의 인덱스(0부터 시작)
			std::vector<uint32_t> local;
			bool valid = true;
			//indices가 있으면 accessor에서 읽고, 없으면 정점 순서대로 0, 1, 2...
			if (primitive.contains("indices"))
			{
				size_t accessorIndex = 0;
				AccessorView view;
				std::string reason;
				if (!ReadSize(primitive, "indices", true, 0, accessorIndex) || !LoadAccessor(root, buffers, accessorIndex, view, reason))
				{
					Log::Warn("glTF primitive 의 indices 를 읽을 수 없어 건너뜀: %s", reason.c_str());
					valid = false;
				}
				else
				{
					local.reserve(view.count);
					for (size_t i = 0; i < view.count && valid; ++i)
					{
						uint32_t index = 0;
						//정점 수 이상인 인덱스는 잘못된 파일이라 이 primitive를 버림
						valid = ReadIndex(view, i, index) && index < vertexCount;
						local.push_back(index);
					}
				}
			}
			else
			{
				local.reserve(vertexCount);
				for (size_t i = 0; i < vertexCount; ++i) local.push_back(static_cast<uint32_t>(i));
			}
			//삼각형(3개 단위)으로 떨어지지 않는 나머지는 버림
			local.resize(local.size() - local.size() % 3);
			//잘못됐거나 삼각형이 없으면 건너뜀
			if (!valid || local.empty()) { ++stats.skippedPrimitives; continue; }

			vertices.reserve(vertices.size() + vertexCount);
			for (size_t v = 0; v < vertexCount; ++v)
			{
				//기본값으로 먼저 채우고, 읽은 속성만 덮어씀
				VERTEX vertex{ 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f };
				float f[4] = {};
				ReadFloat(position, v, f, 3);
				//위치(z는 zSign을 곱해서 좌표계 변환)
				vertex.x = f[0]; vertex.y = f[1]; vertex.z = f[2] * zSign;
				//법선도 z 변환
				if (hasNormal && ReadFloat(normal, v, f, 3))
				{
					vertex.nx = f[0]; vertex.ny = f[1]; vertex.nz = f[2] * zSign;
				}
				//uv
				if (hasTexcoord && ReadFloat(texcoord, v, f, 2))
				{
					vertex.u = f[0]; vertex.v = f[1];
				}
				//정점 색(COLOR_0). VEC3이면 rgb만 읽고 a는 1
				if (hasColor)
				{
					float c[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
					if (ReadFloat(color, v, c, color.componentCount == 3 ? 3 : 4))
					{
						vertex.r = c[0]; vertex.g = c[1]; vertex.b = c[2]; vertex.a = c[3];
					}
				}
				//탄젠트. xyz는 z 변환, w는 방향(부호)이라 flipZ면 뒤집음
				if (hasTangent && ReadFloat(tangent, v, f, 4))
				{
					vertex.tx = f[0]; vertex.ty = f[1]; vertex.tz = f[2] * zSign; vertex.tw = options.flipZ ? -f[3] : f[3];
				}
				vertices.push_back(vertex);

				//스킨 속성도 정점과 같은 순서로 쌓음. 읽지 못한 정점(또는 스킨 없는 primitive)은 조인트 0 에 가중치 1
				VertexSkin vertexSkin;
				vertexSkin.weights[0] = 1.0f;
				if (primitiveSkinned)
				{
					float j[4] = {}, w[4] = {};
					if (ReadFloat(joints, v, j, 4) && ReadFloat(weights, v, w, 4))
					{
						float sum = 0.0f;
						for (int k = 0; k < 4; ++k) { w[k] = (std::max)(w[k], 0.0f); sum += w[k]; }
						//가중치 합이 0 이면(영향 없는 정점) 기본값(조인트 0, 가중치 1)을 유지. 아니면 합이 1이 되게 다시 맞춤
						if (sum > 0.0f)
						{
							for (int k = 0; k < 4; ++k)
							{
								if (j[k] > 255.0f) { skinOverflow = true; j[k] = 0.0f; w[k] = 0.0f; }
								vertexSkin.joints[k] = static_cast<uint8_t>(j[k]);
								vertexSkin.weights[k] = w[k] / sum;
							}
						}
					}
				}
				skin.push_back(vertexSkin);
			}
			//TANGENT가 없으면 나중에 계산해야 한다고 표시
			if (!hasTangent) needTangents = true;

			//Submesh : 합쳐진 인덱스 배열에서 이 primitive가 차지하는 구간(indexStart, indexCount)과 재질 번호
			Submesh submesh;
			submesh.indexStart = static_cast<uint32_t>(indices.size());
			size_t material = defaultSlot;
			//material이 없거나 범위 밖이면 기본 재질 슬롯(맨 마지막)을 사용
			if (!ReadSize(primitive, "material", false, defaultSlot, material) || material >= defaultSlot) material = defaultSlot;
			submesh.materialSlot = static_cast<uint32_t>(material);
			//합쳐진 정점 배열 기준이 되도록 baseVertex를 더해서 저장
			for (const uint32_t index : local) indices.push_back(baseVertex + index);
			submesh.indexCount = static_cast<uint32_t>(indices.size()) - submesh.indexStart;
			//좌표계를 뒤집으면 삼각형 감는 방향이 뒤집히므로 인덱스 두 개를 서로 바꿈
			if (options.flipZ)
			{
				for (size_t i = submesh.indexStart; i + 2 < indices.size(); i += 3) std::swap(indices[i + 1], indices[i + 2]);
			}
			submeshes.push_back(submesh);
		}

		//정점이나 인덱스가 하나도 없으면 실패
		if (vertices.empty() || indices.empty()) return false;

		//탄젠트가 없었고 옵션이 켜져 있으면 계산
		if (needTangents && options.generateMissingTangents)
		{
			//스킨이 있으면 정점을 복제할 때 스킨 속성도 같이 복제하도록 넘김
			Mesh::ComputeTangents(vertices, indices, false, hasSkin ? &skin : nullptr);
			++stats.generatedTangentMeshes;
		}
		if (skinOverflow)
		{
			Log::Warn("glTF mesh: 조인트 번호가 255 를 넘는 정점이 있어 해당 영향을 버림 (스킨 조인트는 최대 256개)");
		}
		//정점이 65535개 이하면 16비트 인덱스로 쓸 수 있음(통계)
		if (vertices.size() <= 65535) ++stats.meshesWith16BitIndices;
		//통계 누적
		stats.vertices += static_cast<uint32_t>(vertices.size());
		stats.triangles += static_cast<uint32_t>(indices.size() / 3);
		stats.submeshes += static_cast<uint32_t>(submeshes.size());
		//합친 데이터로 Mesh 생성
		if (hasSkin)
		{
			++stats.skinnedMeshes;
			outMesh = Mesh::FromData(std::move(vertices), std::move(skin), std::move(indices), std::move(submeshes));
		}
		else outMesh = Mesh::FromData(std::move(vertices), std::move(indices), std::move(submeshes));
		return true;
	}

	XMMATRIX NodeLocalMatrix(const json& node);

	//mesh가 달린 노드를 ModelInstance로 변환
	//부모 사슬을 따라 월드 행렬을 한 번 계산해서 굳힘(정지 상태용)
	bool BuildInstances(const json& root, const std::vector<int>& meshRemap, const std::vector<std::shared_ptr<const Mesh>>& meshes,
		const ModelLoader::Options& options, const std::string& modelName, std::vector<ModelInstance>& outInstances,
		ModelImport::BoundsAccumulator& bounds, std::string& error)
	{
		outInstances.clear();
		const auto nodes = root.find("nodes");
		if (nodes == root.end()) return true;
		if (!nodes->is_array()) { error = "nodes 가 배열이 아님"; return false; }
		const size_t count = nodes->size();

		//parent[n] = n번 노드의 부모 번호(없으면 -1)
		//glTF는 부모가 children으로 자식을 가리키는 방식이라 거꾸로 뒤집어서 만듦
		std::vector<int> parent(count, -1);
		for (size_t n = 0; n < count; ++n)
		{
			const json& node = (*nodes)[n];
			const auto children = node.is_object() ? node.find("children") : node.end();
			if (!node.is_object() || children == node.end() || !children->is_array()) continue;
			for (const json& child : *children)
			{
				if (child.is_number_unsigned() && child.get<size_t>() < count && parent[child.get<size_t>()] < 0) parent[child.get<size_t>()] = static_cast<int>(n);
			}
		}

		//mirror : z축 뒤집기(flipZ일 때), rootScale : 모델 전체 크기 옵션
		const XMMATRIX mirror = XMMatrixScaling(1.0f, 1.0f, options.flipZ ? -1.0f : 1.0f);
		const XMMATRIX rootScale = XMMatrixScaling(options.scale, options.scale, options.scale);

		for (size_t n = 0; n < count; ++n)
		{
			const json& node = (*nodes)[n];
			size_t meshNumber = 0;
			//mesh가 없는 노드, 변환에 실패한(-1) mesh를 가진 노드는 건너뜀
			if (!node.is_object() || !node.contains("mesh") || !ReadSize(node, "mesh", true, 0, meshNumber)) continue;
			if (meshNumber >= meshRemap.size() || meshRemap[meshNumber] < 0) continue;
			//meshRemap으로 JSON의 mesh 번호를 outMeshes 번호로 바꿈
			const int meshIndex = meshRemap[meshNumber];

			//월드 행렬 = 내 로컬 * 부모 로컬 * 조부모 로컬 ... 루트까지 (행 벡터 방식이라 왼쪽이 자식)
			XMMATRIX gltfWorld = NodeLocalMatrix(node);
			size_t steps = 0;
			for (int p = parent[n]; p >= 0; p = parent[static_cast<size_t>(p)])
			{
				//부모 사슬이 노드 수보다 길면 순환이라서 오류(무한 루프 방지)
				if (++steps > count) { error = std::format("nodes[{}] 의 부모 사슬이 순환함", n); return false; }
				gltfWorld = gltfWorld * NodeLocalMatrix((*nodes)[static_cast<size_t>(p)]);
			}
			//glTF → 엔진 좌표계로 변환(mirror * M * mirror)하고 모델 크기를 곱함
			const XMMATRIX world = mirror * gltfWorld * mirror * rootScale;

			//어떤 mesh를 어떤 월드 행렬로 그릴지
			ModelInstance instance;
			instance.meshIndex = static_cast<uint32_t>(meshIndex);
			instance.node = static_cast<int>(n);
			//skin 번호는 skins[] 의 JSON 번호 그대로(LoadSkins 가 같은 번호로 만듦)
			size_t skinNumber = 0;
			if (node.contains("skin") && ReadSize(node, "skin", true, 0, skinNumber)) instance.skin = static_cast<int>(skinNumber);
			XMStoreFloat4x4(&instance.world, world);
			//이름은 노드의 name, 없으면 '모델이름/node번호'
			const auto name = node.find("name");
			instance.name = (name != node.end() && name->is_string()) ? name->get<std::string>() : std::format("{}/node{}", modelName, n);
			outInstances.push_back(std::move(instance));

			//모델 전체 경계에 이 mesh의 경계를 월드 행렬로 변환해서 누적
			bounds.Add(meshes[static_cast<size_t>(meshIndex)]->GetBounds(), world);
		}
		return true;
	}

	//노드의 로컬 행렬. matrix가 있으면 그대로 사용
	//없으면 translation, rotation, scale로 만듦(S * R * T 순서)
	XMMATRIX NodeLocalMatrix(const json& node)
	{
		float m[16];
		if (JsonFloats(node, "matrix", m, 16))
		{
			return XMMATRIX(m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]);
		}
		//기본값 : 이동 0, 회전 없음(쿼터니언 0,0,0,1), 크기 1
		float t[3] = { 0.0f, 0.0f, 0.0f };
		float r[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		float s[3] = { 1.0f, 1.0f, 1.0f };
		JsonFloats(node, "translation", t, 3);
		JsonFloats(node, "rotation", r, 4);
		JsonFloats(node, "scale", s, 3);
		//쿼터니언은 정규화해서 사용. 길이가 0이면 회전 없음으로
		XMVECTOR rotation = XMVectorSet(r[0], r[1], r[2], r[3]);
		if (XMVectorGetX(XMVector4LengthSq(rotation)) > 0.0f) rotation = XMQuaternionNormalize(rotation);
		else rotation = XMQuaternionIdentity();
		return XMMatrixScaling(s[0], s[1], s[2]) * XMMatrixRotationQuaternion(rotation) * XMMatrixTranslation(t[0], t[1], t[2]);
	}

	//노드 트리를 ModelNode 표로 보존(애니메이션용). 행렬이 아니라 TRS로 저장
	bool LoadNodes(const json& root, const std::vector<int>& meshRemap, const ModelLoader::Options& options,
		const std::string& modelName, std::vector<ModelNode>& outNodes, std::string& error)
	{
		outNodes.clear();
		const auto nodes = root.find("nodes");
		if (nodes == root.end()) return true;
		if (!nodes->is_array()) { error = "nodes 가 배열이 아님"; return false; }

		//nodes와 같은 번호로 만듦(애니메이션 channel이 노드 번호로 가리키기 때문)
		outNodes.resize(nodes->size());
		const float zSign = options.flipZ ? -1.0f : 1.0f;
		const XMMATRIX mirror = XMMatrixScaling(1.0f, 1.0f, zSign);
		for (size_t n = 0; n < nodes->size(); ++n)
		{
			const json& node = (*nodes)[n];
			ModelNode& out = outNodes[n];
			//이름은 name이 없으면 '모델이름/node번호'
			out.name = std::format("{}/node{}", modelName, n);
			if (!node.is_object()) continue;

			const auto name = node.find("name");
			if (name != node.end() && name->is_string()) out.name = name->get<std::string>();

			//mesh 번호는 meshRemap으로 변환. 변환에 실패한 mesh면 -1
			size_t meshNumber = 0;
			if (node.contains("mesh") && ReadSize(node, "mesh", true, 0, meshNumber) && meshNumber < meshRemap.size()) out.meshIndex = meshRemap[meshNumber];
			size_t skinNumber = 0;
			if (node.contains("skin") && ReadSize(node, "skin", true, 0, skinNumber)) out.skin = static_cast<int>(skinNumber);

			//자식들의 parent에 내 번호를 기록. 이미 부모가 있으면 유지
			const auto children = node.find("children");
			if (children != node.end() && children->is_array())
			{
				for (const json& child : *children)
				{
					if (!child.is_number_unsigned() || child.get<size_t>() >= outNodes.size()) continue;
					ModelNode& target = outNodes[child.get<size_t>()];
					if (target.parent < 0) target.parent = static_cast<int>(n);
				}
			}

			//matrix로 들어온 노드는 z 변환을 적용한 뒤 TRS로 분해해서 저장
			float matrix[16];
			if (JsonFloats(node, "matrix", matrix, 16))
			{
				const XMMATRIX local = mirror * NodeLocalMatrix(node) * mirror;
				XMVECTOR scaleV, rotationV, translationV;
				if (XMMatrixDecompose(&scaleV, &rotationV, &translationV, local))
				{
					XMStoreFloat3(&out.scale, scaleV);
					XMStoreFloat4(&out.rotation, rotationV);
					XMStoreFloat3(&out.translation, translationV);
				}
				continue;
			}

			//matrix가 없으면 translation, rotation, scale을 직접 읽음
			float t[3] = { 0.0f, 0.0f, 0.0f };
			float r[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			float s[3] = { 1.0f, 1.0f, 1.0f };
			JsonFloats(node, "translation", t, 3);
			JsonFloats(node, "rotation", r, 4);
			JsonFloats(node, "scale", s, 3);
			XMVECTOR rotation = XMVectorSet(r[0], r[1], r[2], r[3]);
			if (XMVectorGetX(XMVector4LengthSq(rotation)) > 0.0f) rotation = XMQuaternionNormalize(rotation);
			else rotation = XMQuaternionIdentity();
			XMFLOAT4 q;
			XMStoreFloat4(&q, rotation);
			//좌표계 변환. 이동은 z 부호 반전, 회전은 x,y 부호 반전(z축 거울 반사), 크기는 그대로
			out.translation = XMFLOAT3(t[0], t[1], t[2] * zSign);
			out.rotation = options.flipZ ? XMFLOAT4(-q.x, -q.y, q.z, q.w) : q;
			out.scale = XMFLOAT3(s[0], s[1], s[2]);
		}
		return true;
	}

	//skins[]를 ModelSkin으로 변환. JSON 번호와 같은 번호로 만듦(노드의 skin 번호가 그대로 가리키도록).
	//읽을 수 없는 skin은 조인트 없는 빈 skin으로 남김
	//inverseBindMatrices 는 glTF(열 우선 MAT4, 오른손)이고, 엔진 좌표계로 mirror * M * mirror 로 바꿈
	//(노드 월드 행렬과 같은 변환). accessor 가 없으면 단위행렬
	bool LoadSkins(const json& root, const std::vector<std::vector<uint8_t>>& buffers, const ModelLoader::Options& options,
		const std::vector<ModelNode>& nodes, std::vector<ModelSkin>& outSkins, std::string& error)
	{
		outSkins.clear();
		const auto skins = root.find("skins");
		if (skins == root.end()) return true;
		if (!skins->is_array()) { error = "skins 가 배열이 아님"; return false; }

		const XMMATRIX mirror = XMMatrixScaling(1.0f, 1.0f, options.flipZ ? -1.0f : 1.0f);
		outSkins.resize(skins->size());
		for (size_t s = 0; s < skins->size(); ++s)
		{
			const json& source = (*skins)[s];
			ModelSkin& out = outSkins[s];
			if (!source.is_object()) continue;
			out.name = std::format("skin{}", s);
			const auto name = source.find("name");
			if (name != source.end() && name->is_string() && !name->get<std::string>().empty()) out.name = name->get<std::string>();

			//joints : 이 skin이 쓰는 노드 번호 목록
			const auto joints = source.find("joints");
			if (joints == source.end() || !joints->is_array() || joints->empty())
			{
				Log::Warn("glTF skin '%s': joints 가 없음", out.name.c_str());
				continue;
			}
			bool valid = true;
			for (const json& joint : *joints)
			{
				if (!joint.is_number_unsigned() || joint.get<size_t>() >= nodes.size()) { valid = false; break; }
				out.joints.push_back(joint.get<uint32_t>());
			}
			if (!valid)
			{
				Log::Warn("glTF skin '%s': joints 에 잘못된 노드 번호가 있음", out.name.c_str());
				out.joints.clear();
				continue;
			}

			//역바인드 행렬. 없으면 전부 단위행렬
			XMFLOAT4X4 identity;
			XMStoreFloat4x4(&identity, XMMatrixIdentity());
			out.inverseBind.assign(out.joints.size(), identity);
			size_t accessorIndex = 0;
			if (source.contains("inverseBindMatrices") && ReadSize(source, "inverseBindMatrices", true, 0, accessorIndex))
			{
				AccessorView view;
				std::string reason;
				if (!LoadAccessor(root, buffers, accessorIndex, view, reason) || view.componentCount != 16 || view.count < out.joints.size())
				{
					Log::Warn("glTF skin '%s': inverseBindMatrices 를 읽을 수 없어 단위행렬로 대체함 %s", out.name.c_str(), reason.c_str());
				}
				else
				{
					for (size_t i = 0; i < out.joints.size(); ++i)
					{
						float m[16];
						ReadFloat(view, i, m, 16);
						//NodeLocalMatrix 와 같은 방식으로 읽음: 열 우선 16개를 그대로 행렬 생성자에 넣으면 행벡터용(전치)이 됨
						const XMMATRIX inverseBind = XMMATRIX(m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]);
						XMStoreFloat4x4(&out.inverseBind[i], mirror * inverseBind * mirror);
					}
				}
			}
		}
		return true;
	}

	//animations[]를 AnimationClip으로 변환
	//animation 하나 = clip 하나, 그 안의 channel마다 AnimationChannel 하나
	bool LoadAnimations(const json& root, const std::vector<std::vector<uint8_t>>& buffers, const ModelLoader::Options& options,
		const std::vector<ModelNode>& nodes, std::vector<AnimationClip>& outClips, std::string& error)
	{
		outClips.clear();
		const auto animations = root.find("animations");
		if (animations == root.end()) return true;
		if (!animations->is_array()) { error = "animations 가 배열이 아님"; return false; }

		const float zSign = options.flipZ ? -1.0f : 1.0f;
		for (size_t a = 0; a < animations->size(); ++a)
		{
			const json& animation = (*animations)[a];
			if (!animation.is_object()) continue;
			//animation은 samplers(값이 변하는 방법)와 channels(어느 노드의 무엇을)로 이루어짐
			const auto samplers = animation.find("samplers");
			const auto channels = animation.find("channels");
			if (samplers == animation.end() || !samplers->is_array() || channels == animation.end() || !channels->is_array()) continue;

			AnimationClip clip;
			//이름 기본값은 'Animation번호'
			clip.name = std::format("Animation{}", a);
			const auto name = animation.find("name");
			if (name != animation.end() && name->is_string() && !name->get<std::string>().empty()) clip.name = name->get<std::string>();

			for (size_t c = 0; c < channels->size(); ++c)
			{
				const json& channel = (*channels)[c];
				//channel이 가리키는 target(어느 노드의 무엇)과 sampler 번호
				const json* target = JsonObject(channel, "target");
				size_t samplerIndex = 0;
				if (target == nullptr || !ReadSize(channel, "sampler", true, 0, samplerIndex) || samplerIndex >= samplers->size()) continue;
				// target.node 가 없으면(확장용 채널) 건너뜀
				size_t nodeIndex = 0;
				if (!target->contains("node") || !ReadSize(*target, "node", true, 0, nodeIndex) || nodeIndex >= nodes.size()) continue;

				//path : 바꿀 속성. translation(3), rotation(4, 쿼터니언), scale(3)만 지원
				//components = 값 하나의 성분 수
				const auto pathValue = target->find("path");
				if (pathValue == target->end() || !pathValue->is_string()) continue;
				const std::string pathName = pathValue->get<std::string>();
				AnimationChannel out;
				out.node = static_cast<uint32_t>(nodeIndex);
				size_t components = 0;
				if (pathName == "translation") { out.path = AnimationPath::Translation; components = 3; }
				else if (pathName == "rotation") { out.path = AnimationPath::Rotation; components = 4; }
				else if (pathName == "scale") { out.path = AnimationPath::Scale; components = 3; }
				else
				{
					Log::Warn("glTF 애니메이션 '%s': 지원하지 않는 path '%s' 채널을 건너뜀", clip.name.c_str(), pathName.c_str());
					continue;
				}

				//channel이 쓰는 sampler. 보간 방식(interpolation)은 기본 Linear
				const json& sampler = (*samplers)[samplerIndex];
				out.interpolation = AnimationInterpolation::Linear;
				const auto interpolation = sampler.find("interpolation");
				if (interpolation != sampler.end() && interpolation->is_string())
				{
					const std::string mode = interpolation->get<std::string>();
					if (mode == "STEP") out.interpolation = AnimationInterpolation::Step;
					else if (mode == "CUBICSPLINE") out.interpolation = AnimationInterpolation::CubicSpline;
				}

				//input = 키프레임 시각 accessor, output = 각 시각의 값 accessor
				size_t inputIndex = 0, outputIndex = 0;
				AccessorView input, output;
				std::string reason;
				if (!ReadSize(sampler, "input", true, 0, inputIndex) || !LoadAccessor(root, buffers, inputIndex, input, reason) ||
					!ReadSize(sampler, "output", true, 0, outputIndex) || !LoadAccessor(root, buffers, outputIndex, output, reason))
				{
					error = std::format("animations[{}].samplers[{}] accessor 오류: {}", a, samplerIndex, reason);
					return false;
				}
				//input은 SCALAR(시각), output은 path에 맞는 성분 수여야 함
				if (input.componentCount != 1 || output.componentCount != static_cast<int>(components))
				{
					error = std::format("animations[{}].samplers[{}] accessor 형식이 맞지 않음", a, samplerIndex);
					return false;
				}
				//CUBICSPLINE은 키 하나당 값이 3개(inTangent, value, outTangent)라서 output이 3배
				const size_t perKey = out.interpolation == AnimationInterpolation::CubicSpline ? 3 : 1;
				if (input.count == 0 || output.count != input.count * perKey)
				{
					error = std::format("animations[{}].samplers[{}] 키 수가 맞지 않음 (input {}, output {})", a, samplerIndex, input.count, output.count);
					return false;
				}

				//키프레임 시각들을 읽고, 증가 순서인지 검사
				out.times.resize(input.count);
				for (size_t i = 0; i < input.count; ++i) ReadFloat(input, i, &out.times[i], 1);
				bool sorted = true;
				for (size_t i = 1; i < out.times.size(); ++i) if (out.times[i] < out.times[i - 1]) sorted = false;
				if (!sorted) { error = std::format("animations[{}].samplers[{}] 시간이 증가 순서가 아님", a, samplerIndex); return false; }

				//output 값들을 읽어서 저장
				out.values.resize(output.count * components);
				for (size_t i = 0; i < output.count; ++i)
				{
					float* v = &out.values[i * components];
					ReadFloat(output, i, v, components);
					// 우리 좌표계(flipZ)로 바꿈. 탄젠트도 같은 변환 (선형이므로 값과 같음).
					if (out.path == AnimationPath::Translation) v[2] *= zSign;
					else if (out.path == AnimationPath::Rotation && options.flipZ) { v[0] = -v[0]; v[1] = -v[1]; }
				}
				//clip 길이 = 채널들 중 가장 늦은 키 시각
				clip.duration = (std::max)(clip.duration, out.times.back());
				clip.channels.push_back(std::move(out));
			}
			//채널이 하나도 없으면 clip을 만들지 않음
			if (!clip.channels.empty()) outClips.push_back(std::move(clip));
		}
		return true;
	}
}




//glTF/GLB 로딩 진입점
//버퍼 → 이미지 → 재질 → 메시 → 인스턴스 → 노드 → 애니메이션 순서로 변환
//하나라도 실패하면 로그를 남기고 false
bool ModelLoader::LoadGltf(const std::wstring& path, const Options& options, Model& out)
{
	//시작 시간 기록. 마지막에 FinishModel로 넘김
	const auto startTime = std::chrono::steady_clock::now();
	//모델을 초기화. 이름과 폴더는 경로에서 뽑음(폴더는 상대 경로 uri를 읽을 때 사용)
	out = Model{};
	out.name = ModelImport::ModelNameFromPath(path);
	const std::wstring directory = ModelImport::DirectoryFromPath(path);

	//파일 전체를 바이트로 읽기
	std::vector<uint8_t> fileBytes;
	if (!ModelImport::ReadWholeFile(path, fileBytes))
	{
		Log::Error("glTF 파일을 열 수 없음: %s", ModelImport::ToUtf8(path).c_str());
		return false;
	}

	//gltf/glb를 구분해서 JSON(root)과 GLB의 BIN 청크(glbBin)를 꺼냄
	json root;
	std::vector<uint8_t> glbBin;
	{
		std::string error;
		if (!ParseContainer(fileBytes, root, glbBin, error))
		{
			Log::Error("glTF 컨테이너 파싱 실패 (%s): %s", error.c_str(), ModelImport::ToUtf8(path).c_str());
			return false;
		}
	}

	//buffers[] 로딩
	std::vector<std::vector<uint8_t>> buffers;
	{
		std::string error;
		if (!LoadBuffers(root, glbBin, directory, buffers, error))
		{
			Log::Error("glTF 버퍼 로드 실패 (%s): %s", error.c_str(), ModelImport::ToUtf8(path).c_str());
			return false;
		}
	}

	std::string error;

	//이미지 로딩
	if (!LoadImages(root, buffers, directory, out.name, out.images, error))
	{
		Log::Error("glTF 이미지 로드 실패 (%s): %s", error.c_str(), ModelImport::ToUtf8(path).c_str());
		return false;
	}

	//재질 변환
	if (!LoadMaterials(root, out.name, out.materials, error))
	{
		Log::Error("glTF 재질 변환 실패 (%s): %s", error.c_str(), ModelImport::ToUtf8(path).c_str());
		return false;
	}
	//맨 마지막 재질(기본 재질)의 번호. material이 없는 primitive가 사용
	const uint32_t defaultSlot = static_cast<uint32_t>(out.materials.size() - 1);

	//메시 변환
	std::vector<int> meshRemap;
	if (!LoadMeshes(root, buffers, options, defaultSlot, out.meshes, meshRemap, out.stats, error))
	{
		Log::Error("glTF 메시 변환 실패 (%s): %s", error.c_str(), ModelImport::ToUtf8(path).c_str());
		return false;
	}

	//노드 → 인스턴스(정지 상태 월드 행렬)와 모델 전체 경계 계산
	ModelImport::BoundsAccumulator bounds;
	if (!BuildInstances(root, meshRemap, out.meshes, options, out.name, out.instances, bounds, error))
	{
		Log::Error("glTF 노드 변환 실패 (%s): %s", error.c_str(), ModelImport::ToUtf8(path).c_str());
		return false;
	}
	//누적한 경계를 모델에 적용
	bounds.ApplyTo(out);

	//노드 트리(TRS) 보존. 애니메이션에서 사용
	if (!LoadNodes(root, meshRemap, options, out.name, out.nodes, error))
	{
		Log::Error("glTF 노드 표 변환 실패 (%s): %s", error.c_str(), ModelImport::ToUtf8(path).c_str());
		return false;
	}

	//스킨(조인트 목록 + 역바인드 행렬)
	if (!LoadSkins(root, buffers, options, out.nodes, out.skins, error))
	{
		Log::Error("glTF 스킨 변환 실패 (%s): %s", error.c_str(), ModelImport::ToUtf8(path).c_str());
		return false;
	}

	//애니메이션 변환
	if (!LoadAnimations(root, buffers, options, out.nodes, out.animations, error))
	{
		Log::Error("glTF 애니메이션 변환 실패 (%s): %s", error.c_str(), ModelImport::ToUtf8(path).c_str());
		return false;
	}

	//마무리. 시작 시간을 같이 넘김
	return ModelImport::FinishModel(out, startTime);
}
