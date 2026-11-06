// ScreenCaptureKit backend (macOS 12.3+).
#include "../common/Capture.h"
#include "../common/Platform.h"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <atomic>
#include <unistd.h>

static std::string ToStd( NSString* s )
{
	return s ? std::string( [s UTF8String] ) : std::string();
}

API_AVAILABLE( macos( 12.3 ) )
static SCShareableContent* FetchContent( bool onScreenOnly, NSError** errorOut )
{
	__block SCShareableContent* result = nil;
	__block NSError* err               = nil;
	dispatch_semaphore_t sem           = dispatch_semaphore_create( 0 );
	[SCShareableContent getShareableContentExcludingDesktopWindows:YES
	                                           onScreenWindowsOnly:onScreenOnly
	                                             completionHandler:^( SCShareableContent* content, NSError* error ) {
		                                             result = content;
		                                             err    = error;
		                                             dispatch_semaphore_signal( sem );
	                                             }];
	dispatch_semaphore_wait( sem, dispatch_time( DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC ) );
	if( errorOut )
		*errorOut = err;
	return result;
}

static NSString* ScreenName( CGDirectDisplayID displayID )
{
	for( NSScreen* screen in [NSScreen screens] )
	{
		NSNumber* num = screen.deviceDescription[ @"NSScreenNumber" ];
		if( num && num.unsignedIntValue == displayID )
		{
			if( @available( macOS 10.15, * ) )
				return screen.localizedName;
		}
	}
	return nil;
}

std::vector< CaptureTarget > EnumerateCaptureTargets( TargetKind kind )
{
	std::vector< CaptureTarget > out;
	if( @available( macOS 12.3, * ) )
	{
		@autoreleasepool
		{
			SCShareableContent* content = FetchContent( kind == TargetKind::Window, nil );
			if( !content )
				return out;

			if( kind == TargetKind::Monitor )
			{
				int n = 0;
				for( SCDisplay* d in content.displays )
				{
					CaptureTarget t;
					t.kind              = TargetKind::Monitor;
					t.id                = d.displayID;
					CGDisplayModeRef mode = CGDisplayCopyDisplayMode( d.displayID );
					t.width             = mode ? (int)CGDisplayModeGetPixelWidth( mode ) : (int)d.width;
					t.height            = mode ? (int)CGDisplayModeGetPixelHeight( mode ) : (int)d.height;
					if( mode )
						CGDisplayModeRelease( mode );
					t.primary         = CGDisplayIsMain( d.displayID );
					NSString* name    = ScreenName( d.displayID );
					t.owner           = ToStd( name );
					t.name            = "Display " + std::to_string( ++n ) + ( name ? " " + ToStd( name ) : std::string() ) + " (" +
					         std::to_string( t.width ) + "x" + std::to_string( t.height ) + ")";
					out.push_back( t );
				}
			}
			else
			{
				pid_t self = getpid();
				for( SCWindow* w in content.windows )
				{
					if( w.windowLayer != 0 || w.owningApplication == nil )
						continue;
					if( w.owningApplication.processID == self )
						continue;
					if( w.frame.size.width < 40 || w.frame.size.height < 40 )
						continue;
					NSString* title = w.title;
					if( title.length == 0 )
						continue;

					CaptureTarget t;
					t.kind           = TargetKind::Window;
					t.id             = w.windowID;
					t.title          = ToStd( title );
					t.owner          = ToStd( w.owningApplication.bundleIdentifier );
					t.width          = (int)w.frame.size.width;
					t.height         = (int)w.frame.size.height;
					std::string app  = ToStd( w.owningApplication.applicationName );
					std::string shown = t.title.size() > 60 ? t.title.substr( 0, 57 ) + "..." : t.title;
					t.name           = shown + "  [" + app + "]";
					out.push_back( t );
				}
			}
		}
	}
	return out;
}

bool IsScreenCaptureAvailable( std::string* why )
{
	if( @available( macOS 12.3, * ) )
	{
		if( CGPreflightScreenCaptureAccess() )
			return true;
		CGRequestScreenCaptureAccess();
		if( why )
			*why = "Allow Resolume in System Settings > Privacy & Security > Screen Recording, then restart Resolume";
		return false;
	}
	if( why )
		*why = "Screen capture needs macOS 12.3 or newer";
	return false;
}

// ------------------------------------------------------------ stream output

struct MacCaptureShared
{
	std::atomic< FrameSlot* > sink{ nullptr };
	std::atomic< bool > running{ false };
	std::function< void( const std::string& ) > stoppedCallback;
};

API_AVAILABLE( macos( 12.3 ) )
@interface SCCaptureOutput : NSObject < SCStreamOutput, SCStreamDelegate >
{
@public
	std::shared_ptr< MacCaptureShared > shared;
}
@end

@implementation SCCaptureOutput
- (void)stream:(SCStream*)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type
{
	if( type != SCStreamOutputTypeScreen || !CMSampleBufferIsValid( sampleBuffer ) )
		return;
	FrameSlot* sink = shared->sink.load();
	if( !sink )
		return;

	CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray( sampleBuffer, false );
	if( attachments && CFArrayGetCount( attachments ) > 0 )
	{
		NSDictionary* info = (__bridge NSDictionary*)CFArrayGetValueAtIndex( attachments, 0 );
		NSNumber* status   = info[ SCStreamFrameInfoStatus ];
		if( status && status.integerValue != SCFrameStatusComplete )
			return;// idle / blank frames carry no new pixels
	}

	CVPixelBufferRef pb = CMSampleBufferGetImageBuffer( sampleBuffer );
	if( !pb )
		return;
	CVPixelBufferLockBaseAddress( pb, kCVPixelBufferLock_ReadOnly );
	const uint8_t* base = (const uint8_t*)CVPixelBufferGetBaseAddress( pb );
	int w               = (int)CVPixelBufferGetWidth( pb );
	int h               = (int)CVPixelBufferGetHeight( pb );
	int stride          = (int)CVPixelBufferGetBytesPerRow( pb );
	if( base )
		sink->publish( base, w, h, stride );
	CVPixelBufferUnlockBaseAddress( pb, kCVPixelBufferLock_ReadOnly );
}

- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error
{
	shared->running = false;
	if( shared->stoppedCallback )
		shared->stoppedCallback( error ? ToStd( error.localizedDescription ) : std::string( "Capture stopped" ) );
}
@end

// ------------------------------------------------------------ capture

class MacScreenCapture : public ScreenCapture
{
public:
	MacScreenCapture() :
		shared( std::make_shared< MacCaptureShared >() )
	{
	}
	~MacScreenCapture() override { stop(); }

	bool start( const CaptureTarget& target, bool showCursor, FrameSlot* sink ) override
	{
		stop();
		error.clear();
		if( @available( macOS 12.3, * ) )
		{
			@autoreleasepool
			{
				NSError* err                = nil;
				SCShareableContent* content = FetchContent( false, &err );
				if( !content )
				{
					error = err ? ToStd( err.localizedDescription ) : "Screen Recording permission missing";
					return false;
				}

				SCContentFilter* filter = nil;
				int width = 0, height = 0;
				if( target.kind == TargetKind::Monitor )
				{
					for( SCDisplay* d in content.displays )
						if( d.displayID == (CGDirectDisplayID)target.id )
						{
							filter = [[SCContentFilter alloc] initWithDisplay:d excludingWindows:@[]];
							CGDisplayModeRef mode = CGDisplayCopyDisplayMode( d.displayID );
							width  = mode ? (int)CGDisplayModeGetPixelWidth( mode ) : (int)d.width * 2;
							height = mode ? (int)CGDisplayModeGetPixelHeight( mode ) : (int)d.height * 2;
							if( mode )
								CGDisplayModeRelease( mode );
						}
				}
				else
				{
					for( SCWindow* w in content.windows )
						if( w.windowID == (CGWindowID)target.id )
						{
							filter       = [[SCContentFilter alloc] initWithDesktopIndependentWindow:w];
							double scale = [NSScreen mainScreen].backingScaleFactor;
							if( @available( macOS 14.0, * ) )
								scale = filter.pointPixelScale;
							width  = (int)( w.frame.size.width * scale );
							height = (int)( w.frame.size.height * scale );
						}
				}
				if( !filter || width <= 0 || height <= 0 )
				{
					error = target.kind == TargetKind::Window ? "Window no longer exists. Press Refresh List" : "Display not found";
					return false;
				}

				config                      = [[SCStreamConfiguration alloc] init];
				config.width                = (size_t)width;
				config.height               = (size_t)height;
				config.pixelFormat          = kCVPixelFormatType_32BGRA;
				config.showsCursor          = showCursor;
				config.minimumFrameInterval = CMTimeMake( 1, 60 );
				config.queueDepth           = 5;

				output         = [[SCCaptureOutput alloc] init];
				output->shared = shared;
				shared->sink   = sink;

				stream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:output];
				queue  = dispatch_queue_create( "slidecapture.capture", DISPATCH_QUEUE_SERIAL );
				if( ![stream addStreamOutput:output type:SCStreamOutputTypeScreen sampleHandlerQueue:queue error:&err] )
				{
					error = err ? ToStd( err.localizedDescription ) : "addStreamOutput failed";
					stream = nil;
					return false;
				}

				__block NSError* startErr = nil;
				dispatch_semaphore_t sem  = dispatch_semaphore_create( 0 );
				[stream startCaptureWithCompletionHandler:^( NSError* e ) {
					startErr = e;
					dispatch_semaphore_signal( sem );
				}];
				dispatch_semaphore_wait( sem, dispatch_time( DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC ) );
				if( startErr )
				{
					error  = ToStd( startErr.localizedDescription );
					stream = nil;
					return false;
				}
				shared->running = true;
				return true;
			}
		}
		error = "Screen capture needs macOS 12.3 or newer";
		return false;
	}

	void stop() override
	{
		shared->sink = nullptr;
		if( @available( macOS 12.3, * ) )
		{
			if( stream )
			{
				dispatch_semaphore_t sem = dispatch_semaphore_create( 0 );
				[stream stopCaptureWithCompletionHandler:^( NSError* ) {
					dispatch_semaphore_signal( sem );
				}];
				dispatch_semaphore_wait( sem, dispatch_time( DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC ) );
			}
			stream = nil;
			output = nil;
			config = nil;
		}
		shared->running = false;
	}

	void setShowCursor( bool show ) override
	{
		if( @available( macOS 12.3, * ) )
		{
			if( !stream || !config )
				return;
			config.showsCursor = show;
			[stream updateConfiguration:config completionHandler:nil];
		}
	}

	void setClientAreaOnly( bool ) override {}// macOS: use the Crop parameters
	bool isRunning() const override { return shared->running; }
	std::string lastError() const override { return error; }
	void setStoppedCallback( std::function< void( const std::string& ) > cb ) override { shared->stoppedCallback = std::move( cb ); }

private:
	std::shared_ptr< MacCaptureShared > shared;
	// The bundle's deployment target is macOS 12.3, so ScreenCaptureKit is always present.
	SCStream* stream              = nil;
	SCStreamConfiguration* config = nil;
	SCCaptureOutput* output       = nil;
	dispatch_queue_t queue        = nil;
	std::string error;
};

std::unique_ptr< ScreenCapture > CreateScreenCapture()
{
	return std::make_unique< MacScreenCapture >();
}
