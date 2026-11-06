#include "WinUtil.h"
#include "../common/Platform.h"
#include <objbase.h>
#include <shlobj.h>
#include <algorithm>
#include <chrono>
#include <vector>

namespace plat
{
void InitThread( Apartment apartment )
{
	if( apartment == Apartment::SingleThreaded )
		CoInitializeEx( nullptr, COINIT_APARTMENTTHREADED );
	else if( apartment == Apartment::MultiThreaded )
		CoInitializeEx( nullptr, COINIT_MULTITHREADED );// also initialises WinRT for C++/WinRT calls
}

void UninitThread( Apartment apartment )
{
	if( apartment != Apartment::None )
		CoUninitialize();
}

std::string CacheDir()
{
	wchar_t tmp[ MAX_PATH + 1 ] = {};
	DWORD n                     = GetTempPathW( MAX_PATH, tmp );
	std::wstring dir            = std::wstring( tmp, n );
	if( !dir.empty() && dir.back() != L'\\' )
		dir += L'\\';
	dir += L"ResolumeSlideCapture";
	CreateDirectoryW( dir.c_str(), nullptr );
	return Narrow( dir );
}

bool FileExists( const std::string& path )
{
	if( path.empty() )
		return false;
	return GetFileAttributesW( WinPath( path ).c_str() ) != INVALID_FILE_ATTRIBUTES;
}

bool FileStamp( const std::string& path, uint64_t& sizeOut, int64_t& mtimeOut )
{
	WIN32_FILE_ATTRIBUTE_DATA data;
	if( !GetFileAttributesExW( WinPath( path ).c_str(), GetFileExInfoStandard, &data ) )
		return false;
	if( data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
		return false;
	sizeOut  = ( uint64_t( data.nFileSizeHigh ) << 32 ) | data.nFileSizeLow;
	mtimeOut = int64_t( ( uint64_t( data.ftLastWriteTime.dwHighDateTime ) << 32 ) | data.ftLastWriteTime.dwLowDateTime );
	return true;
}

bool RemoveFile( const std::string& path )
{
	return DeleteFileW( WinPath( path ).c_str() ) != 0;
}

bool CreateDir( const std::string& path )
{
	return CreateDirectoryW( WinPath( path ).c_str(), nullptr ) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool ReadFileHead( const std::string& path, char* buf, size_t len )
{
	HANDLE h = CreateFileW( WinPath( path ).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr );
	if( h == INVALID_HANDLE_VALUE )
		return false;
	DWORD read = 0;
	BOOL ok    = ReadFile( h, buf, (DWORD)len, &read, nullptr );
	CloseHandle( h );
	return ok && read == len;
}

static std::wstring QuoteArg( const std::wstring& arg )
{
	if( !arg.empty() && arg.find_first_of( L" \t\"" ) == std::wstring::npos )
		return arg;
	std::wstring out = L"\"";
	size_t slashes   = 0;
	for( wchar_t c : arg )
	{
		if( c == L'\\' )
		{
			++slashes;
			continue;
		}
		if( c == L'"' )
			out.append( slashes * 2 + 1, L'\\' );
		else
			out.append( slashes, L'\\' );
		slashes = 0;
		out += c;
	}
	out.append( slashes * 2, L'\\' );
	out += L'"';
	return out;
}

ProcessResult RunProcessCapture( const std::vector< std::string >& argv, int timeoutMs, std::string& stdoutText, const CancelFn& cancel )
{
	ProcessResult result;
	stdoutText.clear();
	if( argv.empty() )
		return result;

	std::wstring cmd;
	for( size_t i = 0; i < argv.size(); ++i )
	{
		if( i )
			cmd += L' ';
		cmd += QuoteArg( i == 0 ? WinPath( argv[ i ] ) : Widen( argv[ i ] ) );
	}

	SECURITY_ATTRIBUTES sa = { sizeof( sa ), nullptr, TRUE };
	HANDLE readPipe = nullptr, writePipe = nullptr;
	if( !CreatePipe( &readPipe, &writePipe, &sa, 0 ) )
		return result;
	SetHandleInformation( readPipe, HANDLE_FLAG_INHERIT, 0 );

	STARTUPINFOW si = {};
	si.cb           = sizeof( si );
	si.dwFlags      = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	si.wShowWindow  = SW_HIDE;
	si.hStdOutput   = writePipe;
	si.hStdError    = writePipe;
	si.hStdInput    = GetStdHandle( STD_INPUT_HANDLE );

	PROCESS_INFORMATION pi = {};
	std::vector< wchar_t > cmdBuf( cmd.begin(), cmd.end() );
	cmdBuf.push_back( 0 );
	BOOL ok = CreateProcessW( WinPath( argv[ 0 ] ).c_str(), cmdBuf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi );
	CloseHandle( writePipe );
	if( !ok )
	{
		CloseHandle( readPipe );
		return result;
	}
	result.started = true;

	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds( timeoutMs );
	for( ;; )
	{
		DWORD avail = 0;
		while( PeekNamedPipe( readPipe, nullptr, 0, nullptr, &avail, nullptr ) && avail > 0 )
		{
			char buf[ 4096 ];
			DWORD n = 0;
			if( !ReadFile( readPipe, buf, (DWORD)std::min< DWORD >( avail, sizeof( buf ) ), &n, nullptr ) || n == 0 )
				break;
			stdoutText.append( buf, n );
		}
		DWORD w = WaitForSingleObject( pi.hProcess, 20 );
		if( w == WAIT_OBJECT_0 )
		{
			DWORD code = 0;
			GetExitCodeProcess( pi.hProcess, &code );
			result.exitCode = (int)code;
			break;
		}
		bool cancelNow = cancel && cancel();
		if( cancelNow || std::chrono::steady_clock::now() > deadline )
		{
			TerminateProcess( pi.hProcess, 1 );
			WaitForSingleObject( pi.hProcess, 2000 );
			result.timedOut  = !cancelNow;
			result.cancelled = cancelNow;
			break;
		}
	}
	CloseHandle( readPipe );
	CloseHandle( pi.hThread );
	CloseHandle( pi.hProcess );
	return result;
}

static std::string KnownFolder( REFKNOWNFOLDERID id )
{
	PWSTR p = nullptr;
	std::string out;
	if( SUCCEEDED( SHGetKnownFolderPath( id, 0, nullptr, &p ) ) )
		out = Narrow( p );
	CoTaskMemFree( p );
	return out;
}

std::string FindLibreOffice()
{
	std::vector< std::string > roots = { KnownFolder( FOLDERID_ProgramFiles ), KnownFolder( FOLDERID_ProgramFilesX86 ) };
	for( const auto& r : roots )
	{
		if( r.empty() )
			continue;
		std::string p = r + "\\LibreOffice\\program\\soffice.exe";
		if( FileExists( p ) )
			return p;
	}
	return std::string();
}

std::string FindCurl()
{
	wchar_t sys[ MAX_PATH ] = {};
	GetSystemDirectoryW( sys, MAX_PATH );// curl.exe ships with Windows 10 1803+
	std::string p = Narrow( sys ) + "\\curl.exe";
	return FileExists( p ) ? p : std::string();
}

bool IsPowerPointInstalled()
{
	CLSID clsid;
	return SUCCEEDED( CLSIDFromProgID( L"PowerPoint.Application", &clsid ) );
}
}// namespace plat
