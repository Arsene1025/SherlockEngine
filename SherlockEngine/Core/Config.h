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
