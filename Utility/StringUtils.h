#pragma once 
#include <string>
#include <stringapiset.h>

inline std::wstring ConvertUtf8ToWstring(const char* utf8Str) {
	int len = MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, nullptr, 0);

	std::wstring result(len, 0);
	MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, &result[0], len);

	result.resize(wcslen(result.c_str()));
	return result;
}

inline std::string ConvertWstringToUtf8(const std::wstring& wstr) {
	int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);

	std::string result(len, 0);
	WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], len, nullptr, nullptr);

	result.resize(strlen(result.c_str()));
	return result;
}