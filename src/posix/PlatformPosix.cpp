// Process / file helpers shared by macOS and Linux.
#include "../common/Platform.h"
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace plat
{
#if !defined( __APPLE__ )
void InitThread( Apartment ) {}
void UninitThread( Apartment ) {}
#endif

static bool MakeDir( const std::string& path )
{
	if( mkdir( path.c_str(), 0755 ) == 0 || errno == EEXIST )
		return true;
	return false;
}

std::string CacheDir()
{
	const char* tmp = std::getenv( "TMPDIR" );
	std::string base = ( tmp && *tmp ) ? tmp : "/tmp";
	while( base.size() > 1 && base.back() == '/' )
		base.pop_back();
	std::string dir = base + "/ResolumeSlideCapture";
	MakeDir( dir );
	return dir;
}

bool FileExists( const std::string& path )
{
	struct stat st;
	return !path.empty() && stat( path.c_str(), &st ) == 0;
}

bool FileStamp( const std::string& path, uint64_t& sizeOut, int64_t& mtimeOut )
{
	struct stat st;
	if( stat( path.c_str(), &st ) != 0 )
		return false;
	sizeOut  = (uint64_t)st.st_size;
	mtimeOut = (int64_t)st.st_mtime;
	return true;
}

bool RemoveFile( const std::string& path )
{
	return unlink( path.c_str() ) == 0;
}

bool CreateDir( const std::string& path )
{
	return MakeDir( path );
}

bool ReadFileHead( const std::string& path, char* buf, size_t len )
{
	int fd = open( path.c_str(), O_RDONLY );
	if( fd < 0 )
		return false;
	ssize_t n = read( fd, buf, len );
	close( fd );
	return n == (ssize_t)len;
}

ProcessResult RunProcessCapture( const std::vector< std::string >& argv, int timeoutMs, std::string& stdoutText, const CancelFn& cancel )
{
	ProcessResult result;
	stdoutText.clear();
	if( argv.empty() )
		return result;

	int pipeFd[ 2 ];
	if( pipe( pipeFd ) != 0 )
		return result;

	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init( &actions );
	posix_spawn_file_actions_adddup2( &actions, pipeFd[ 1 ], STDOUT_FILENO );
	posix_spawn_file_actions_addclose( &actions, pipeFd[ 0 ] );
	posix_spawn_file_actions_addopen( &actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0 );

	std::vector< char* > args;
	for( const auto& a : argv )
		args.push_back( const_cast< char* >( a.c_str() ) );
	args.push_back( nullptr );

	pid_t pid = 0;
	int rc    = posix_spawn( &pid, argv[ 0 ].c_str(), &actions, nullptr, args.data(), environ );
	posix_spawn_file_actions_destroy( &actions );
	close( pipeFd[ 1 ] );
	if( rc != 0 )
	{
		close( pipeFd[ 0 ] );
		return result;
	}
	result.started = true;

	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds( timeoutMs );
	bool pipeOpen = true;
	int status    = 0;
	for( ;; )
	{
		if( pipeOpen )
		{
			struct pollfd pfd = { pipeFd[ 0 ], POLLIN, 0 };
			if( poll( &pfd, 1, 20 ) > 0 )
			{
				char buf[ 4096 ];
				ssize_t n = read( pipeFd[ 0 ], buf, sizeof( buf ) );
				if( n > 0 )
					stdoutText.append( buf, size_t( n ) );
				else
					pipeOpen = false;
			}
		}
		else
		{
			usleep( 20000 );
		}

		pid_t w = waitpid( pid, &status, WNOHANG );
		if( w == pid )
		{
			// drain remaining output
			char buf[ 4096 ];
			ssize_t n;
			while( pipeOpen && ( n = read( pipeFd[ 0 ], buf, sizeof( buf ) ) ) > 0 )
				stdoutText.append( buf, size_t( n ) );
			result.exitCode = WIFEXITED( status ) ? WEXITSTATUS( status ) : -1;
			break;
		}
		bool cancelNow = cancel && cancel();
		if( cancelNow || std::chrono::steady_clock::now() > deadline )
		{
			kill( pid, SIGKILL );
			waitpid( pid, &status, 0 );
			result.timedOut  = !cancelNow;
			result.cancelled = cancelNow;
			break;
		}
	}
	close( pipeFd[ 0 ] );
	return result;
}

std::string FindLibreOffice()
{
	const char* candidates[] = {
#if defined( __APPLE__ )
		"/Applications/LibreOffice.app/Contents/MacOS/soffice",
#else
		"/usr/bin/soffice",
		"/usr/bin/libreoffice",
		"/usr/local/bin/soffice",
		"/opt/libreoffice/program/soffice",
#endif
	};
	for( const char* c : candidates )
		if( access( c, X_OK ) == 0 )
			return c;
	return std::string();
}

std::string FindCurl()
{
	const char* candidates[] = { "/usr/bin/curl", "/usr/local/bin/curl", "/opt/homebrew/bin/curl" };
	for( const char* c : candidates )
		if( access( c, X_OK ) == 0 )
			return c;
	return std::string();
}

#if !defined( __APPLE__ )
bool IsPowerPointInstalled()
{
	return false;
}
#endif
}// namespace plat
