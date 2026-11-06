#include "ScreenCapturePlugin.h"
#include <cmath>
#include <cstring>

enum ParamID : FFUInt32
{
	PT_SOURCE,
	PT_MONITOR,
	PT_WINDOW,
	PT_REFRESH,
	PT_CURSOR,
	PT_TITLEBAR,
	PT_FIT,
	PT_CROP_LEFT,
	PT_CROP_RIGHT,
	PT_CROP_TOP,
	PT_CROP_BOTTOM,
	PT_STATUS,
	PT_WINDOW_KEY,
};

static CFFGLPluginInfo PluginInfo(
	PluginFactory< ScreenCapturePlugin >,
	"SCsc",                                    // unique id
	"SC Screen Capture",                       // name
	2,                                         // API major
	1,                                         // API minor
	1,                                         // plugin major
	0,                                         // plugin minor
	FF_SOURCE,                                 // type
	"Capture a monitor or a single application window",
	"SlideCapture for Resolume" );

static int Index( float v, size_t count )
{
	int i = (int)std::lround( v );
	if( i < 0 || count == 0 )
		return 0;
	if( (size_t)i >= count )
		return (int)count - 1;
	return i;
}

ScreenCapturePlugin::ScreenCapturePlugin()
{
	SetMinInputs( 0 );
	SetMaxInputs( 0 );


	monitors = EnumerateCaptureTargets( TargetKind::Monitor );
	windows  = EnumerateCaptureTargets( TargetKind::Window );

	SetOptionParamInfo( PT_SOURCE, "Source", 2, 0.0f );
	SetParamElementInfo( PT_SOURCE, 0, "Monitor", 0.0f );
	SetParamElementInfo( PT_SOURCE, 1, "Window", 1.0f );

	// Dynamic lists get filled by refreshTargets(); start with at least one element.
	SetOptionParamInfo( PT_MONITOR, "Monitor", 1, 0.0f );
	SetParamElementInfo( PT_MONITOR, 0, "(none)", 0.0f );
	SetOptionParamInfo( PT_WINDOW, "Window", 1, 0.0f );
	SetParamElementInfo( PT_WINDOW, 0, "(none)", 0.0f );

	SetParamInfof( PT_REFRESH, "Refresh List", FF_TYPE_EVENT );
	SetParamInfo( PT_CURSOR, "Show Cursor", FF_TYPE_BOOLEAN, true );
	SetParamInfo( PT_TITLEBAR, "Hide Title Bar", FF_TYPE_BOOLEAN, true );

	SetOptionParamInfo( PT_FIT, "Fit", 3, 0.0f );
	SetParamElementInfo( PT_FIT, 0, "Fit", 0.0f );
	SetParamElementInfo( PT_FIT, 1, "Fill", 1.0f );
	SetParamElementInfo( PT_FIT, 2, "Stretch", 2.0f );

	SetParamInfo( PT_CROP_LEFT, "Crop Left", FF_TYPE_STANDARD, 0.0f );
	SetParamInfo( PT_CROP_RIGHT, "Crop Right", FF_TYPE_STANDARD, 0.0f );
	SetParamInfo( PT_CROP_TOP, "Crop Top", FF_TYPE_STANDARD, 0.0f );
	SetParamInfo( PT_CROP_BOTTOM, "Crop Bottom", FF_TYPE_STANDARD, 0.0f );
	for( FFUInt32 p = PT_CROP_LEFT; p <= PT_CROP_BOTTOM; ++p )
		SetParamGroup( p, "Crop" );

	SetParamInfo( PT_STATUS, "Status", FF_TYPE_TEXT, "" );
	SetParamInfo( PT_WINDOW_KEY, "WindowKey", FF_TYPE_TEXT, "" );
	SetParamVisibility( PT_WINDOW_KEY, false, false );

#if !defined( _WIN32 )
	SetParamVisibility( PT_TITLEBAR, false, false );// macOS: use Crop Top instead
#endif

	refreshTargets( false );
	updateVisibility( false );

	worker->post( [ e = engine ] {
		std::string why;
		if( !IsScreenCaptureAvailable( &why ) )
			e->setStatus( why );
		e->capture = CreateScreenCapture();
		if( e->capture )
			e->capture->setStoppedCallback( [ ep = e.get() ]( const std::string& reason ) { ep->setStatus( reason ); } );
	} );
}

ScreenCapturePlugin::~ScreenCapturePlugin()
{
	worker->clearPending();
	worker->post( [ e = engine ] {
		if( e->capture )
			e->capture->stop();
		e->capture.reset();
	} );
	// Never block Resolume for long: if the capture API hangs, the worker is
	// detached and keeps its own reference to the engine.
	if( !worker->shutdown( 2000 ) )
		plat::Log( "capture worker did not stop in time, detached" );
}

FFResult ScreenCapturePlugin::InitGL( const FFGLViewportStruct* vp )
{
	if( !renderer.init() )
	{
		DeInitGL();
		return FF_FAIL;
	}
	return CFFGLPlugin::InitGL( vp );
}

FFResult ScreenCapturePlugin::DeInitGL()
{
	renderer.release();
	return FF_SUCCESS;
}

int ScreenCapturePlugin::findWindowByKey( const std::string& key ) const
{
	if( key.empty() )
		return -1;
	for( size_t i = 0; i < windows.size(); ++i )
		if( windows[ i ].matchKey() == key )
			return (int)i;
	// Same application, title changed (e.g. browser tab): take the first window of that app.
	std::string owner = key.substr( 0, key.find( '|' ) );
	for( size_t i = 0; i < windows.size(); ++i )
		if( windows[ i ].owner == owner )
			return (int)i;
	return -1;
}

void ScreenCapturePlugin::refreshTargets( bool raiseEvents )
{
	if( raiseEvents )
	{
		monitors = EnumerateCaptureTargets( TargetKind::Monitor );
		windows  = EnumerateCaptureTargets( TargetKind::Window );
	}

	std::vector< std::string > names;
	std::vector< float > values;
	for( size_t i = 0; i < monitors.size(); ++i )
	{
		names.push_back( monitors[ i ].name );
		values.push_back( (float)i );
	}
	if( names.empty() )
	{
		names.push_back( "(no monitor found)" );
		values.push_back( 0.0f );
	}
	SetParamElements( PT_MONITOR, names, values, raiseEvents );

	names.clear();
	values.clear();
	for( size_t i = 0; i < windows.size(); ++i )
	{
		names.push_back( windows[ i ].name );
		values.push_back( (float)i );
	}
	if( names.empty() )
	{
		names.push_back( "(no window found)" );
		values.push_back( 0.0f );
	}
	SetParamElements( PT_WINDOW, names, values, raiseEvents );

	// Keep pointing at the same window after the list order changed.
	int found = findWindowByKey( windowKey );
	if( found >= 0 && found != (int)windowIdx )
	{
		windowIdx = (float)found;
		if( raiseEvents )
			RaiseParamEvent( PT_WINDOW, FF_EVENT_FLAG_VALUE );
	}
}

void ScreenCapturePlugin::updateVisibility( bool raiseEvents )
{
	bool window = std::lround( sourceKind ) == 1;
	SetParamVisibility( PT_MONITOR, !window, raiseEvents );
	SetParamVisibility( PT_WINDOW, window, raiseEvents );
#if defined( _WIN32 )
	SetParamVisibility( PT_TITLEBAR, window, raiseEvents );
#endif
}

void ScreenCapturePlugin::applyChanges()
{
	bool window = std::lround( sourceKind ) == 1;

	if( windowKeyChanged )
	{
		// Preset / composition load: the key is the reliable identification.
		int found = findWindowByKey( windowKey );
		if( found >= 0 )
		{
			windowIdx = (float)found;
			RaiseParamEvent( PT_WINDOW, FF_EVENT_FLAG_VALUE );
		}
		windowKeyChanged = false;
		windowIdxChanged = false;
	}
	else if( windowIdxChanged )
	{
		if( !windows.empty() )
		{
			windowKey = windows[ Index( windowIdx, windows.size() ) ].matchKey();
			RaiseParamEvent( PT_WINDOW_KEY, FF_EVENT_FLAG_VALUE );
		}
		windowIdxChanged = false;
	}

	if( cursorChanged )
	{
		bool c = showCursor;
		worker->post( [ e = engine, c ] {
			if( e->capture )
				e->capture->setShowCursor( c );
		} );
		cursorChanged = false;
	}
	if( titleBarChanged )
	{
		bool h = hideTitleBar;
		worker->post( [ e = engine, h ] {
			if( e->capture )
				e->capture->setClientAreaOnly( h );
		} );
		titleBarChanged = false;
	}

	if( !restartNeeded )
		return;
	restartNeeded = false;

	const auto& list = window ? windows : monitors;
	if( list.empty() )
	{
		worker->post( [ e = engine ] {
			if( e->capture )
				e->capture->stop();
		} );
		engine->slot.clear();
		engine->setStatus( window ? "No window selected" : "No monitor found" );
		return;
	}

	CaptureTarget target = list[ Index( window ? windowIdx : monitorIdx, list.size() ) ];
	if( window && target.matchKey() != windowKey )
	{
		windowKey = target.matchKey();// saved with the composition
		RaiseParamEvent( PT_WINDOW_KEY, FF_EVENT_FLAG_VALUE );
	}
	bool cursor          = showCursor;
	bool clientOnly      = hideTitleBar;
	worker->clearPending();
	worker->post( [ e = engine, target, cursor, clientOnly ] {
		if( !e->capture )
			return;
		e->capture->stop();
		e->capture->setClientAreaOnly( clientOnly );
		if( e->capture->start( target, cursor, &e->slot ) )
			e->setStatus( "Capturing: " + target.name );
		else
			e->setStatus( "Error: " + e->capture->lastError() );
	} );
}

FFResult ScreenCapturePlugin::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	applyChanges();

	if( engine->statusDirty.exchange( false ) )
	{
		std::lock_guard< std::mutex > lock( engine->statusMutex );
		statusText = engine->pendingStatus;
		RaiseParamEvent( PT_STATUS, FF_EVENT_FLAG_VALUE );
	}

	renderer.update( engine->slot );
	Crop c;
	c.left   = crop[ 0 ] * 0.5f;
	c.right  = crop[ 1 ] * 0.5f;
	c.top    = crop[ 2 ] * 0.5f;
	c.bottom = crop[ 3 ] * 0.5f;
	renderer.draw( (int)currentViewport.width, (int)currentViewport.height, (FitMode)std::lround( fitMode ), c );
	return FF_SUCCESS;
}

FFResult ScreenCapturePlugin::SetFloatParameter( unsigned int index, float value )
{
	switch( index )
	{
	case PT_SOURCE:
		if( value != sourceKind )
		{
			sourceKind    = value;
			restartNeeded = true;
			updateVisibility( true );
		}
		break;
	case PT_MONITOR:
		if( value != monitorIdx )
		{
			monitorIdx    = value;
			restartNeeded = true;
		}
		break;
	case PT_WINDOW:
		if( value != windowIdx )
		{
			windowIdx        = value;
			windowIdxChanged = true;
			restartNeeded    = true;
		}
		break;
	case PT_REFRESH:
		if( value > 0.5f )
		{
			refreshTargets( true );
			restartNeeded = true;
		}
		break;
	case PT_CURSOR:
		if( ( value > 0.5f ) != showCursor )
		{
			showCursor    = value > 0.5f;
			cursorChanged = true;
		}
		break;
	case PT_TITLEBAR:
		if( ( value > 0.5f ) != hideTitleBar )
		{
			hideTitleBar    = value > 0.5f;
			titleBarChanged = true;
		}
		break;
	case PT_FIT:
		fitMode = value;
		break;
	case PT_CROP_LEFT:
	case PT_CROP_RIGHT:
	case PT_CROP_TOP:
	case PT_CROP_BOTTOM:
		crop[ index - PT_CROP_LEFT ] = value;
		break;
	default:
		return FF_FAIL;
	}
	return FF_SUCCESS;
}

float ScreenCapturePlugin::GetFloatParameter( unsigned int index )
{
	switch( index )
	{
	case PT_SOURCE:
		return sourceKind;
	case PT_MONITOR:
		return monitorIdx;
	case PT_WINDOW:
		return windowIdx;
	case PT_CURSOR:
		return showCursor ? 1.0f : 0.0f;
	case PT_TITLEBAR:
		return hideTitleBar ? 1.0f : 0.0f;
	case PT_FIT:
		return fitMode;
	case PT_CROP_LEFT:
	case PT_CROP_RIGHT:
	case PT_CROP_TOP:
	case PT_CROP_BOTTOM:
		return crop[ index - PT_CROP_LEFT ];
	}
	return 0.0f;
}

FFResult ScreenCapturePlugin::SetTextParameter( unsigned int index, const char* value )
{
	std::string v = value ? value : "";
	switch( index )
	{
	case PT_WINDOW_KEY:
		if( v != windowKey )
		{
			windowKey        = v;
			windowKeyChanged = true;
			restartNeeded    = true;
		}
		return FF_SUCCESS;
	case PT_STATUS:
		return FF_SUCCESS;// read only
	}
	return FF_FAIL;
}

char* ScreenCapturePlugin::GetTextParameter( unsigned int index )
{
	static thread_local std::string buffer;
	switch( index )
	{
	case PT_STATUS:
		buffer = statusText;
		break;
	case PT_WINDOW_KEY:
		buffer = windowKey;
		break;
	default:
		buffer.clear();
	}
	return const_cast< char* >( buffer.c_str() );
}
