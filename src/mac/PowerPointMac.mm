// PowerPoint for Mac control through AppleScript (osascript).
// The first time, macOS asks "Resolume wants to control Microsoft PowerPoint": click OK.
#include "../common/Platform.h"
#include "../common/PowerPoint.h"
#import <AppKit/AppKit.h>
#include <cstdlib>
#include <set>

namespace
{
std::string Escape( const std::string& s )
{
	std::string out;
	for( char c : s )
	{
		if( c == '\\' || c == '"' )
			out += '\\';
		out += c;
	}
	return out;
}

// Runs one AppleScript. Each risky statement gets its own script, because an
// unknown term would otherwise make the whole script fail to compile.
bool RunScript( const std::string& source, std::string* result = nullptr, int timeoutMs = 30000 )
{
	std::string out;
	auto r = plat::RunProcessCapture( { "/usr/bin/osascript", "-e", source }, timeoutMs, out );
	while( !out.empty() && ( out.back() == '\n' || out.back() == '\r' ) )
		out.pop_back();
	if( result )
		*result = out;
	return r.started && !r.timedOut && r.exitCode == 0;
}

bool PowerPointRunning()
{
	@autoreleasepool
	{
		return [NSRunningApplication runningApplicationsWithBundleIdentifier:@"com.microsoft.Powerpoint"].count > 0;
	}
}

const char* kBundle = "com.microsoft.Powerpoint";

std::set< uint64_t > PowerPointWindowIds()
{
	std::set< uint64_t > ids;
	for( const auto& t : EnumerateCaptureTargets( TargetKind::Window ) )
		if( t.owner == kBundle )
			ids.insert( t.id );
	return ids;
}

std::string OpenScript( const std::string& path )
{
	return "set f to POSIX file \"" + Escape( path ) + "\"\n"
	       "tell application \"Microsoft PowerPoint\"\n"
	       "  open f\n"
	       "  set p to active presentation\n"
	       "  return (name of p) & \"|\" & (count of slides of p)\n"
	       "end tell";
}

void QuitIfIdle( bool startedByUs )
{
	if( !startedByUs )
		return;
	std::string n;
	if( RunScript( "tell application \"Microsoft PowerPoint\" to return count of presentations", &n ) && n == "0" )
		RunScript( "tell application \"Microsoft PowerPoint\" to quit" );
}
}// namespace

class MacPowerPointShow : public PowerPointShow
{
public:
	~MacPowerPointShow() override { close(); }

	bool open( const std::string& path, const PowerPointShowOptions& opt, std::string* error ) override
	{
		close();
		options     = opt;
		startedByUs = !PowerPointRunning();
		auto before = PowerPointWindowIds();

		std::string out;
		if( !RunScript( OpenScript( path ), &out, 60000 ) )
		{
			if( error )
				*error = "PowerPoint could not open the file (allow Automation for Resolume in Privacy & Security)";
			QuitIfIdle( startedByUs );
			return false;
		}
		size_t bar = out.rfind( '|' );
		name       = bar == std::string::npos ? out : out.substr( 0, bar );
		count      = bar == std::string::npos ? 0 : std::atoi( out.c_str() + bar + 1 );
		presRef    = "presentation \"" + Escape( name ) + "\"";

		const std::string tell = "tell application \"Microsoft PowerPoint\" to ";
		RunScript( tell + "set show type of slide show settings of " + presRef + " to slide show type window" );
		RunScript( tell + "set loop until stopped of slide show settings of " + presRef + " to " + ( opt.loop ? "true" : "false" ) );
		if( !RunScript( tell + "run slide show slide show settings of " + presRef ) )
		{
			if( error )
				*error = "Could not start the slide show";
			close();
			return false;
		}
		// Best effort: size the window to the composition (points, Retina = 2px/pt).
		double scale = [NSScreen mainScreen].backingScaleFactor;
		int wPt = int( opt.widthPx / scale ), hPt = int( opt.heightPx / scale );
		std::string ssw = "slide show window of " + presRef;
		RunScript( tell + "set width of " + ssw + " to " + std::to_string( wPt ) );
		RunScript( tell + "set height of " + ssw + " to " + std::to_string( hPt ) );

		// Find the new slide show window so we can capture it.
		for( int attempt = 0; attempt < 30 && windowId == 0; ++attempt )
		{
			plat::SleepMs( 200 );
			CaptureTarget best;
			for( const auto& t : EnumerateCaptureTargets( TargetKind::Window ) )
			{
				if( t.owner != kBundle || before.count( t.id ) )
					continue;
				if( t.width * t.height > best.width * best.height )
					best = t;
			}
			if( best.id )
			{
				windowId = best.id;
				target   = best;
			}
		}
		if( windowId == 0 )
		{
			if( error )
				*error = "Slide show window not found";
			close();
			return false;
		}
		opened = true;
		return true;
	}

	void close() override
	{
		if( !name.empty() )
		{
			RunScript( "tell application \"Microsoft PowerPoint\" to close " + presRef + " saving no" );
			QuitIfIdle( startedByUs );
		}
		name.clear();
		presRef.clear();
		windowId = 0;
		count    = 0;
		opened   = false;
	}

	bool isOpen() override { return opened; }

	bool next() override { return step( "go to next slide v" ); }
	bool previous() override { return step( "go to previous slide v" ); }
	bool gotoSlide( int oneBased ) override { return step( "go to slide v number " + std::to_string( oneBased ) ); }

	int currentSlide() override
	{
		if( !opened )
			return 0;
		std::string out;
		if( !RunScript( "tell application \"Microsoft PowerPoint\" to return slide index of slide of slideshow view of slide show window of " + presRef, &out, 5000 ) )
		{
			opened = false;// the show was ended in PowerPoint
			return last;
		}
		last = std::atoi( out.c_str() );
		return last;
	}

	int slideCount() override { return count; }

	bool captureTarget( CaptureTarget& out ) override
	{
		if( !windowId )
			return false;
		out = target;
		return true;
	}

private:
	bool step( const std::string& command )
	{
		if( !opened )
			return false;
		std::string out;
		bool ok = RunScript( "tell application \"Microsoft PowerPoint\"\n"
		                     "  set v to slideshow view of slide show window of " + presRef + "\n"
		                     "  " + command + "\n"
		                     "  return slide index of slide of v\n"
		                     "end tell",
		                     &out, 5000 );
		if( ok )
			last = std::atoi( out.c_str() );
		return ok;
	}

	PowerPointShowOptions options;
	std::string name, presRef;
	CaptureTarget target;
	uint64_t windowId = 0;
	int count         = 0;
	int last          = 0;
	bool opened       = false;
	bool startedByUs  = false;
};

std::unique_ptr< PowerPointShow > CreatePowerPointShow()
{
	return std::make_unique< MacPowerPointShow >();
}

bool PowerPointExportPdf( const std::string& input, const std::string& outputPdf, std::string* error )
{
	bool startedByUs = !PowerPointRunning();
	std::string script = "set f to POSIX file \"" + Escape( input ) + "\"\n"
	                     "set outPath to (POSIX file \"" + Escape( outputPdf ) + "\") as string\n"
	                     "tell application \"Microsoft PowerPoint\"\n"
	                     "  open f\n"
	                     "  set p to active presentation\n"
	                     "  save p in outPath as save as PDF\n"
	                     "  close p saving no\n"
	                     "end tell";
	bool ok = RunScript( script, nullptr, 180000 );
	QuitIfIdle( startedByUs );
	if( !ok && error )
		*error = "PowerPoint could not save the PDF";
	return ok && plat::FileExists( outputPdf );
}
