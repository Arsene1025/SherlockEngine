#pragma once
#include <string>
#include <unordered_map>

// 설정 파일 (10단계). "key = value" 줄과 "[section]" 헤더의 단순 INI.
//
//   [engine]
//   backend = d3d12        ; 주석은 ; 또는 #
//
// 키는 "section.key" 로 평탄화해 저장함 ("engine.backend"). 섹션 밖의 키는 그대로.
// 값은 문자열로 저장하고 Get* 이 변환함. 실패하면 기본값. 파싱 중에는 로그를 남기지 않음 — 설정이 Logger 보다 먼저
// 읽히기 때문임(로그 파일 경로가 여기서 나옴). 오류는 나중에 GetErrors 로 꺼내 기록함.
class Config
{
public:
	bool LoadFromFile(const std::wstring& path);   // 파일이 없으면 false (기본값으로 계속)
	void Set(const std::string& key, const std::string& value);   // 실행 인자 덮어쓰기용

	// 파일의 [section] 아래 key 한 줄의 값만 바꿔 저장함 (에디터의 백엔드 선택용). 주석·다른 줄·줄 끝 문자·BOM 은 그대로 두고,
	// 키가 없으면 섹션 끝에 줄을 넣고, 섹션이 없으면 파일 끝에 섹션을 만듦. 실패하면 false 와 사유(error). 읽어 둔 값(m_values)은 건드리지 않음 —
	// 이미 떠 있는 장치는 그대로이고 다음 실행부터 바뀜.
	static bool WriteValue(const std::wstring& path, const std::string& section, const std::string& key, const std::string& value, std::string* error = nullptr);

	bool Has(const std::string& key) const;
	std::string GetString(const std::string& key, const std::string& defaultValue = "") const;
	int GetInt(const std::string& key, int defaultValue) const;
	float GetFloat(const std::string& key, float defaultValue) const;
	bool GetBool(const std::string& key, bool defaultValue) const;   // true/false, 1/0, yes/no, on/off

	const std::wstring& GetPath() const { return m_path; }
	const std::string& GetErrors() const { return m_errors; }   // 줄 단위 파싱 오류 요약
	const std::unordered_map<std::string, std::string>& GetAll() const { return m_values; }

private:
	std::wstring m_path;
	std::string m_errors;
	std::unordered_map<std::string, std::string> m_values;
};
