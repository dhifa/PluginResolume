// Linux is NOT a Resolume platform. These stubs only exist so the shared code
// and the plugin logic can be compiled and smoke-tested on a Linux CI box.
// The "capture" produces a moving test pattern.
#include "../common/Capture.h"
#include "../common/Pdf.h"
#include "../common/PowerPoint.h"
#include <atomic>
#include <chrono>
#include <thread>

std::vector< CaptureTarget > EnumerateCaptureTargets( TargetKind kind )
{
	CaptureTarget t;
	t.kind   = kind;
	t.id     = 1;
	t.width  = 640;
	t.height = 360;
	t.owner  = "test";
	t.title  = kind == TargetKind::Window ? "Test Window" : "";
	t.name   = kind == TargetKind::Window ? "Test Window  [test]" : "Test Monitor (640x360)";
	return { t };
}

bool IsScreenCaptureAvailable( std::string* )
{
	return true;
}

class TestPatternCapture : public ScreenCapture
{
public:
	~TestPatternCapture() override { stop(); }
	bool start( const CaptureTarget& target, bool, FrameSlot* sink ) override
	{
		stop();
		running = true;
		thread  = std::thread( [ this, target, sink ] {
            std::vector< uint8_t > px( size_t( target.width ) * target.height * 4 );
            int frame = 0;
            while( running )
            {
                for( int y = 0; y < target.height; ++y )
                    for( int x = 0; x < target.width; ++x )
                    {
                        uint8_t* p = &px[ ( size_t( y ) * target.width + x ) * 4 ];
                        p[ 0 ] = uint8_t( x + frame ), p[ 1 ] = uint8_t( y ), p[ 2 ] = uint8_t( frame * 3 ), p[ 3 ] = 255;
                    }
                sink->publish( px.data(), target.width, target.height, target.width * 4 );
                ++frame;
                std::this_thread::sleep_for( std::chrono::milliseconds( 16 ) );
            }
        } );
		return true;
	}
	void stop() override
	{
		running = false;
		if( thread.joinable() )
			thread.join();
	}
	void setShowCursor( bool ) override {}
	void setClientAreaOnly( bool ) override {}
	bool isRunning() const override { return running; }
	std::string lastError() const override { return std::string(); }
	void setStoppedCallback( std::function< void( const std::string& ) > ) override {}

private:
	std::atomic< bool > running{ false };
	std::thread thread;
};

std::unique_ptr< ScreenCapture > CreateScreenCapture()
{
	return std::make_unique< TestPatternCapture >();
}

std::unique_ptr< PdfDocument > OpenPdfDocument( const std::string&, std::string* error )
{
	if( error )
		*error = "PDF rendering is not implemented on Linux";
	return nullptr;
}

std::unique_ptr< PowerPointShow > CreatePowerPointShow()
{
	return nullptr;
}

bool PowerPointExportPdf( const std::string&, const std::string&, std::string* error )
{
	if( error )
		*error = "PowerPoint is not available on Linux";
	return false;
}
