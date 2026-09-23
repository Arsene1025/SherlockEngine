#include "pch.h"
#include "Core/Config.h"
#include <fstream>
#include <cctype>

namespace
{
	std::string Trim(const std::string& text)
	{
		size_t begin = 0;
		while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
		size_t end = text.size();
		while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
		return text.substr(begin, end - begin);
	}

	std::string Lower(const std::string& text)
	{
		std::string out;
		out.reserve(text.size());
		for (char c : text) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return out;
	}
}

bool Config::LoadFromFile(const std::wstring& path)
{
	std::ifstream file(path);
	if (!file) return false;
	m_path = path;

	std::string section;
	std::string line;
	int lineNumber = 0;
	while (std::getline(file, line))
	{
		++lineNumber;
		if (lineNumber == 1 && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF) line.erase(0, 3);   // UTF-8 BOM

		// 주석 제거 (; 또는 #). 값 안의 ; 는 지원하지 않는다 — 설정 파일에 그런 값을 둘 일이 없다.
		const size_t comment = line.find_first_of(";#");
		if (comment != std::string::npos) line.erase(comment);
		line = Trim(line);
		if (line.empty()) continue;

		if (line.front() == '[')
		{
			const size_t close = line.find(']');
			if (close == std::string::npos) { m_errors += std::to_string(lineNumber) + "줄: 닫히지 않은 섹션\n"; continue; }
			section = Lower(Trim(line.substr(1, close - 1)));
			continue;
		}
		const size_t equals = line.find('=');
		if (equals == std::string::npos) { m_errors += std::to_string(lineNumber) + "줄: '=' 없음\n"; continue; }
		const std::string key = Lower(Trim(line.substr(0, equals)));
		const std::string value = Trim(line.substr(equals + 1));
		if (key.empty()) { m_errors += std::to_string(lineNumber) + "줄: 키 없음\n"; continue; }
		m_values[section.empty() ? key : section + "." + key] = value;
	}
	return true;
}

void Config::Set(const std::string& key, const std::string& value)
{
	m_values[Lower(key)] = value;
}

bool Config::Has(const std::string& key) const
{
	return m_values.find(Lower(key)) != m_values.end();
}

std::string Config::GetString(const std::string& key, const std::string& defaultValue) const
{
	auto found = m_values.find(Lower(key));
	return found == m_values.end() ? defaultValue : found->second;
}

int Config::GetInt(const std::string& key, int defaultValue) const
{
	auto found = m_values.find(Lower(key));
	if (found == m_values.end()) return defaultValue;
	char* end = nullptr;
	const long value = std::strtol(found->second.c_str(), &end, 10);
	return (end == found->second.c_str()) ? defaultValue : static_cast<int>(value);
}

float Config::GetFloat(const std::string& key, float defaultValue) const
{
	auto found = m_values.find(Lower(key));
	if (found == m_values.end()) return defaultValue;
	char* end = nullptr;
	const float value = std::strtof(found->second.c_str(), &end);
	return (end == found->second.c_str()) ? defaultValue : value;
}

bool Config::GetBool(const std::string& key, bool defaultValue) const
{
	auto found = m_values.find(Lower(key));
	if (found == m_values.end()) return defaultValue;
	const std::string value = Lower(found->second);
	if (value == "true" || value == "1" || value == "yes" || value == "on") return true;
	if (value == "false" || value == "0" || value == "no" || value == "off") return false;
	return defaultValue;
}
