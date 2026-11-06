#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <windows.h>
#include <unknwn.h>
#include <string>

inline std::wstring Widen( const std::string& s )
{
	if( s.empty() )
		return std::wstring();
	int n = MultiByteToWideChar( CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0 );
	std::wstring w( n, L'\0' );
	MultiByteToWideChar( CP_UTF8, 0, s.data(), (int)s.size(), &w[ 0 ], n );
	return w;
}

inline std::string Narrow( const std::wstring& w )
{
	if( w.empty() )
		return std::string();
	int n = WideCharToMultiByte( CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr );
	std::string s( n, '\0' );
	WideCharToMultiByte( CP_UTF8, 0, w.data(), (int)w.size(), &s[ 0 ], n, nullptr, nullptr );
	return s;
}

// FFGL passes paths with forward or back slashes; Windows APIs want backslashes.
inline std::wstring WinPath( const std::string& utf8 )
{
	std::wstring w = Widen( utf8 );
	for( auto& c : w )
		if( c == L'/' )
			c = L'\\';
	return w;
}
