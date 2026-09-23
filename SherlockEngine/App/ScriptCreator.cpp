#include "pch.h"
#include "App/ScriptCreator.h"
#include "Core/Paths.h"
#include "Core/Log.h"
#include <shellapi.h>
#include <fstream>
#include <cctype>

namespace
{
	bool FileExists(const std::wstring& path)
	{
		const DWORD attributes = GetFileAttributesW(path.c_str());
		return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
	}

	bool WriteAll(const std::wstring& path, const std::string& text)
	{
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file) return false;
		file << text;
		return true;
	}

	std::string HeaderTemplate(const std::string& name)
	{
		std::string t;
		t += "#pragma once\r\n";
		t += "#include \"Scene/Script.h\"\r\n";
		t += "\r\n";
		t += "// " + name + " — 에디터의 New Script 로 만든 스크립트 (유니티 MonoBehaviour / 언리얼 컴포넌트).\r\n";
		t += "// 선언은 여기, 구현은 " + name + ".cpp. 다른 스크립트가 #include \"" + name + ".h\" 뒤 GetBehaviour<" + name + ">() 로 이 클래스를 부를 수 있다.\r\n";
		t += "// 다른 스크립트를 참조할 때는 여기서 전방 선언(class Other;)만 하고 .cpp 에서 include 한다 (순환 include 방지).\r\n";
		t += "// 엔진 컴포넌트(Rigidbody 등)는 #include \"Game/Components/Rigidbody.h\" 뒤 GetComponent<Rigidbody>().\r\n";
		t += "class " + name + " : public Script\r\n";
		t += "{\r\n";
		t += "public:\r\n";
		t += "\tconst char* GetTypeName() const override;   // SHERLOCK_SCRIPT 가 정의한다 (.cpp)\r\n";
		t += "\tvoid Reflect(PropertyVisitor& v) override;  // Inspector 에 보이고 씬에 저장되는 필드\r\n";
		t += "\tvoid Start() override;                       // 재생 시작 때 한 번\r\n";
		t += "\tvoid Update(float dt) override;              // 매 프레임 (dt 초)\r\n";
		t += "\r\n";
		t += "\tfloat speed = 1.0f;\r\n";
		t += "};\r\n";
		return t;
	}

	std::string SourceTemplate(const std::string& name)
	{
		std::string t;
		t += "#include \"pch.h\"\r\n";
		t += "#include \"" + name + ".h\"\r\n";
		t += "\r\n";
		t += "// 쓸 수 있는 것: Translate / TranslateLocal / Rotate / LookAt / SetPosition / SetScale, GetInput().IsKeyDown(VK_UP),\r\n";
		t += "//              GetTime(), Find(\"이름\")->GetBehaviour<다른스크립트>(), GetComponent<Rigidbody>() (#include \"Game/Components/Rigidbody.h\").\r\n";
		t += "// 고친 뒤에는 빌드(Ctrl+Shift+B)하고 다시 실행한다.\r\n";
		t += "\r\n";
		t += "void " + name + "::Reflect(PropertyVisitor& v)\r\n";
		t += "{\r\n";
		t += "\tv.Float(\"speed\", speed);\r\n";
		t += "}\r\n";
		t += "\r\n";
		t += "void " + name + "::Start()\r\n";
		t += "{\r\n";
		t += "}\r\n";
		t += "\r\n";
		t += "void " + name + "::Update(float dt)\r\n";
		t += "{\r\n";
		t += "\t// 예: Rotate(0.0f, speed * dt, 0.0f);\r\n";
		t += "\t// 예: if (GetInput().IsKeyDown(VK_UP)) Translate(0.0f, 0.0f, speed * dt);\r\n";
		t += "\t(void)dt;\r\n";
		t += "}\r\n";
		t += "\r\n";
		t += "SHERLOCK_SCRIPT(" + name + ")\r\n";
		return t;
	}
}

bool ScriptCreator::IsAvailable()
{
	return Paths::HasProject();
}

std::wstring ScriptCreator::GetScriptsDir()
{
	return Paths::HasProject() ? Paths::GetProjectRoot() + L"Scripts\\" : L"";
}

std::wstring ScriptCreator::GetScriptPath(const std::string& typeName)
{
	const std::wstring dir = GetScriptsDir();
	if (dir.empty()) return L"";
	return dir + std::wstring(typeName.begin(), typeName.end()) + L".cpp";
}

std::wstring ScriptCreator::GetScriptHeaderPath(const std::string& typeName)
{
	const std::wstring dir = GetScriptsDir();
	if (dir.empty()) return L"";
	return dir + std::wstring(typeName.begin(), typeName.end()) + L".h";
}

bool ScriptCreator::IsValidClassName(const std::string& name)
{
	if (name.empty() || name.size() > 64) return false;
	if (!(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) return false;
	for (char c : name) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
	static const char* reserved[] = { "Script", "Behaviour", "GameObject", "Scene", "Camera", "Transform", "Input", "Rigidbody", "CameraComponent", "FollowTarget", "class", "int", "float", "void" };
	for (const char* r : reserved) if (name == r) return false;
	return true;
}

bool ScriptCreator::Create(const std::string& className, std::string& error)
{
	if (!IsAvailable()) { error = "no project is open"; return false; }
	if (!IsValidClassName(className)) { error = "invalid class name (letters, digits, _; not a reserved name)"; return false; }
	const std::wstring sourcePath = GetScriptPath(className);
	const std::wstring headerPath = GetScriptHeaderPath(className);
	if (FileExists(sourcePath) || FileExists(headerPath)) { error = "already exists: " + Log::ToUtf8(sourcePath.c_str()); return false; }

	CreateDirectoryW(GetScriptsDir().c_str(), nullptr);
	if (!WriteAll(headerPath, HeaderTemplate(className))) { error = "cannot write " + Log::ToUtf8(headerPath.c_str()); return false; }
	if (!WriteAll(sourcePath, SourceTemplate(className))) { error = "cannot write " + Log::ToUtf8(sourcePath.c_str()); return false; }
	Log::Info("새 스크립트: %s (+ .h). 프로젝트 솔루션이 Scripts\\*.cpp 를 와일드카드로 컴파일한다 — 빌드 후 Add Component 에 나타난다", Log::ToUtf8(sourcePath.c_str()).c_str());
	return true;
}

bool ScriptCreator::Open(const std::wstring& path)
{
	if (path.empty() || !FileExists(path)) return false;
	const HINSTANCE result = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	return reinterpret_cast<INT_PTR>(result) > 32;
}

bool ScriptCreator::OpenScript(const std::string& typeName)
{
	const bool header = Open(GetScriptHeaderPath(typeName));
	const bool source = Open(GetScriptPath(typeName));
	return header || source;
}
