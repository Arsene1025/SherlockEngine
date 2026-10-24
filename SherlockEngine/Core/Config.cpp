#include "pch.h"
#include "Core/Config.h"
#include <fstream>
#include <format>   // C++20: 오류 메시지 조립
#include <cctype>
#include <vector>
#include <iterator>

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

		// 주석 제거 (; 또는 #). 값 안의 ; 와 # 은 지원하지 않음 — 설정 파일에 그런 값을 둘 일이 없음.
		const size_t comment = line.find_first_of(";#");
		if (comment != std::string::npos) line.erase(comment);
		line = Trim(line);
		if (line.empty()) continue;

		if (line.front() == '[')
		{
			const size_t close = line.find(']');
			if (close == std::string::npos) { m_errors += std::format("{}줄: 닫히지 않은 섹션\n", lineNumber); continue; }   // C++20 std::format
			section = Lower(Trim(line.substr(1, close - 1)));
			continue;
		}
		const size_t equals = line.find('=');
		if (equals == std::string::npos) { m_errors += std::format("{}줄: '=' 없음\n", lineNumber); continue; }
		const std::string key = Lower(Trim(line.substr(0, equals)));
		const std::string value = Trim(line.substr(equals + 1));
		if (key.empty()) { m_errors += std::format("{}줄: 키 없음\n", lineNumber); continue; }
		m_values[section.empty() ? key : section + "." + key] = value;
	}
	return true;
}

void Config::Set(const std::string& key, const std::string& value)
{
	m_values[Lower(key)] = value;
}

bool Config::WriteValue(const std::wstring& path, const std::string& section, const std::string& key, const std::string& value, std::string* error)
{
	auto fail = [&](const char* reason) { if (error != nullptr) *error = reason; return false; };

	std::string text;
	{
		std::ifstream in(path, std::ios::binary);
		if (!in) return fail("설정 파일을 열 수 없음");
		text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	}
	const bool crlf = text.find("\r\n") != std::string::npos;
	const char* eol = crlf ? "\r\n" : "\n";

	// 줄 단위로 나눔 (줄 끝의 \r 은 떼어 두고 저장할 때 eol 로 다시 붙임). BOM 은 첫 줄에 그대로 남음.
	std::vector<std::string> lines;
	size_t start = 0;
	while (start <= text.size())
	{
		size_t end = text.find('\n', start);
		if (end == std::string::npos) end = text.size();
		std::string line = text.substr(start, end - start);
		if (!line.empty() && line.back() == '\r') line.pop_back();
		lines.push_back(std::move(line));
		start = end + 1;
	}
	if (!lines.empty() && lines.back().empty()) lines.pop_back();   // 마지막 줄바꿈 뒤의 빈 조각

	const std::string wantSection = Lower(section);
	const std::string wantKey = Lower(key);
	std::string current;
	int sectionHeader = -1;   // 대상 섹션 헤더 줄
	int sectionEnd = -1;      // 대상 섹션의 마지막 줄 (키를 새로 넣을 자리)
	bool replaced = false;
	for (size_t i = 0; i < lines.size() && !replaced; ++i)
	{
		std::string body = lines[i];
		if (i == 0 && body.size() >= 3 && static_cast<unsigned char>(body[0]) == 0xEF) body.erase(0, 3);   // BOM
		const size_t comment = body.find_first_of(";#");
		const std::string code = Trim(comment == std::string::npos ? body : body.substr(0, comment));
		if (code.empty()) continue;
		if (code.front() == '[')
		{
			const size_t close = code.find(']');
			current = close == std::string::npos ? std::string() : Lower(Trim(code.substr(1, close - 1)));
			if (current == wantSection && sectionHeader < 0) sectionHeader = static_cast<int>(i);
			continue;
		}
		if (current != wantSection) continue;
		sectionEnd = static_cast<int>(i);
		const size_t equals = code.find('=');
		if (equals == std::string::npos || Lower(Trim(code.substr(0, equals))) != wantKey) continue;

		// 같은 줄의 값 부분만 교체함. 값 뒤의 공백과 주석은 보존.
		const size_t lineEquals = lines[i].find('=');
		const size_t lineComment = lines[i].find_first_of(";#", lineEquals);
		size_t valueEnd = lineComment == std::string::npos ? lines[i].size() : lineComment;
		size_t gapStart = valueEnd;
		while (gapStart > lineEquals + 1 && std::isspace(static_cast<unsigned char>(lines[i][gapStart - 1]))) --gapStart;
		const std::string gap = lines[i].substr(gapStart, valueEnd - gapStart);
		const std::string tail = lineComment == std::string::npos ? std::string() : lines[i].substr(lineComment);
		lines[i] = lines[i].substr(0, lineEquals + 1) + " " + value + (tail.empty() ? std::string() : (gap.empty() ? " " : gap) + tail);
		replaced = true;
	}
	if (!replaced)
	{
		const std::string newLine = key + " = " + value;
		if (sectionHeader >= 0) lines.insert(lines.begin() + (sectionEnd >= 0 ? sectionEnd : sectionHeader) + 1, newLine);
		else { lines.push_back(std::string()); lines.push_back("[" + section + "]"); lines.push_back(newLine); }
	}

	std::string out;
	for (const std::string& line : lines) { out += line; out += eol; }
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (!file) return fail("설정 파일에 쓸 수 없음 (읽기 전용이거나 권한 없음)");
	file.write(out.data(), static_cast<std::streamsize>(out.size()));
	if (!file) return fail("설정 파일 쓰기 실패");
	return true;
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
