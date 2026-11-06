#pragma once
// Small OS abstraction layer (no FFGL / OpenGL includes on purpose, because
// FFGL.h #defines NOUSER etc. before including windows.h).
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace plat
{
using LogFn = std::function< void( const std::string& ) >;
void SetLogger( LogFn fn );
void Log( const std::string& msg );

// Initialises COM / WinRT for the calling thread (no-op on macOS).
enum class Apartment
{
	None,
	SingleThreaded,
	MultiThreaded
};
void InitThread( Apartment apartment );
void UninitThread( Apartment apartment );

std::string CacheDir();// created on demand, ends without separator
std::string JoinPath( const std::string& a, const std::string& b );
bool FileExists( const std::string& path );
bool FileStamp( const std::string& path, uint64_t& sizeOut, int64_t& mtimeOut );
bool RemoveFile( const std::string& path );
bool CreateDir( const std::string& path );// single level
bool ReadFileHead( const std::string& path, char* buf, size_t len );// first len bytes
std::string FileName( const std::string& path );     // "C:\a\b.pptx" -> "b.pptx"
std::string Extension( const std::string& path );    // lower case, without dot
std::string ReplaceExtension( const std::string& path, const std::string& extNoDot );

struct ProcessResult
{
	bool started  = false;
	bool timedOut = false;
	bool cancelled = false;
	int exitCode  = -1;
};
using CancelFn = std::function< bool() >;// returns true to kill the process early

// Runs argv[0] with arguments, no console window, waits up to timeoutMs.
ProcessResult RunProcess( const std::vector< std::string >& argv, int timeoutMs, const CancelFn& cancel = nullptr );
// Same, but also captures stdout (used for osascript on macOS).
ProcessResult RunProcessCapture( const std::vector< std::string >& argv, int timeoutMs, std::string& stdoutText, const CancelFn& cancel = nullptr );

std::string FindLibreOffice();// path to soffice, empty if not installed
std::string FindCurl();       // path to curl, empty if not available
bool IsPowerPointInstalled();

uint64_t Hash64( const std::string& s );
std::string Hex64( uint64_t v );
void SleepMs( int ms );
}// namespace plat
