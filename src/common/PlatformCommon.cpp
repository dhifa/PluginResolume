#include "Platform.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <mutex>
#include <thread>

namespace plat
{
static std::mutex logMutex;
static LogFn logger;

void SetLogger( LogFn fn )
{
	std::lock_guard< std::mutex > lock( logMutex );
	logger = std::move( fn );
}

void Log( const std::string& msg )
{
	std::lock_guard< std::mutex > lock( logMutex );
	if( logger )
		logger( msg );
	else
		std::fprintf( stderr, "[SlideCapture] %s\n", msg.c_str() );
}

static size_t LastSeparator( const std::string& path )
{
	return path.find_last_of( "/\\" );
}

std::string JoinPath( const std::string& a, const std::string& b )
{
	if( a.empty() )
		return b;
	char last = a.back();
	if( last == '/' || last == '\\' )
		return a + b;
#if defined( _WIN32 )
	return a + "\\" + b;
#else
	return a + "/" + b;
#endif
}

std::string FileName( const std::string& path )
{
	size_t p = LastSeparator( path );
	return p == std::string::npos ? path : path.substr( p + 1 );
}

std::string Extension( const std::string& path )
{
	std::string name = FileName( path );
	size_t dot       = name.find_last_of( '.' );
	if( dot == std::string::npos )
		return std::string();
	std::string ext = name.substr( dot + 1 );
	std::transform( ext.begin(), ext.end(), ext.begin(), []( unsigned char c ) { return (char)std::tolower( c ); } );
	return ext;
}

std::string ReplaceExtension( const std::string& path, const std::string& extNoDot )
{
	size_t sep = LastSeparator( path );
	size_t dot = path.find_last_of( '.' );
	if( dot == std::string::npos || ( sep != std::string::npos && dot < sep ) )
		return path + "." + extNoDot;
	return path.substr( 0, dot + 1 ) + extNoDot;
}

uint64_t Hash64( const std::string& s )
{
	uint64_t h = 1469598103934665603ull;// FNV-1a
	for( unsigned char c : s )
	{
		h ^= c;
		h *= 1099511628211ull;
	}
	return h;
}

std::string Hex64( uint64_t v )
{
	char buf[ 17 ];
	std::snprintf( buf, sizeof( buf ), "%016llx", (unsigned long long)v );
	return buf;
}

void SleepMs( int ms )
{
	std::this_thread::sleep_for( std::chrono::milliseconds( ms ) );
}

ProcessResult RunProcess( const std::vector< std::string >& argv, int timeoutMs, const CancelFn& cancel )
{
	std::string ignored;
	return RunProcessCapture( argv, timeoutMs, ignored, cancel );
}
}// namespace plat
