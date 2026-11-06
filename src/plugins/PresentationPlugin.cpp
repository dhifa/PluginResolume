#include "PresentationPlugin.h"
#include <algorithm>
#include <chrono>
#include <cmath>

enum ParamID : FFUInt32
{
	PT_SOURCE,
	PT_FILE,
	PT_URL,
	PT_MODE,
	PT_CONVERTER,
	PT_PREV,
	PT_NEXT,
	PT_FIRST,
	PT_SLIDE,
	PT_RELOAD,
	PT_LOOP,
	PT_HIDE_WINDOW,
	PT_FIT,
	PT_CROP_LEFT,
	PT_CROP_RIGHT,
	PT_CROP_TOP,
	PT_CROP_BOTTOM,
	PT_STATUS,
};

static const int kMaxSlides     = 999;

static CFFGLPluginInfo PluginInfo(
	PluginFactory< PresentationPlugin >,
	"SCpr",
	"SC Presentation",
	2,
	1,
	1,
	0,
	FF_SOURCE,
	"PowerPoint (with animations), PDF and Google Slides",
	"SlideCapture for Resolume" );

PresentationPlugin::PresentationPlugin()
{
	SetMinInputs( 0 );
	SetMaxInputs( 0 );


	SetOptionParamInfo( PT_SOURCE, "Source", 2, 0.0f );
	SetParamElementInfo( PT_SOURCE, 0, "File (PPT/PDF)", 0.0f );
	SetParamElementInfo( PT_SOURCE, 1, "Google Slides", 1.0f );

	SetFileParamInfo( PT_FILE, "File", { "pptx", "ppt", "pptm", "ppsx", "pps", "ppsm", "potx", "pot", "odp", "pdf" }, "" );
	SetParamInfo( PT_URL, "Google Slides URL", FF_TYPE_TEXT, "" );

	SetOptionParamInfo( PT_MODE, "Mode", 3, 0.0f );
	SetParamElementInfo( PT_MODE, 0, "Auto", 0.0f );
	SetParamElementInfo( PT_MODE, 1, "Animated (needs PowerPoint)", 1.0f );
	SetParamElementInfo( PT_MODE, 2, "Static (no animation)", 2.0f );

	SetOptionParamInfo( PT_CONVERTER, "Static Converter", 3, 0.0f );
	SetParamElementInfo( PT_CONVERTER, 0, "Auto", 0.0f );
	SetParamElementInfo( PT_CONVERTER, 1, "PowerPoint", 1.0f );
	SetParamElementInfo( PT_CONVERTER, 2, "LibreOffice", 2.0f );

	SetParamInfof( PT_PREV, "Previous", FF_TYPE_EVENT );
	SetParamInfof( PT_NEXT, "Next", FF_TYPE_EVENT );
	SetParamInfof( PT_FIRST, "First Slide", FF_TYPE_EVENT );
	SetParamInfo( PT_SLIDE, "Slide", FF_TYPE_INTEGER, 1.0f );
	SetParamRange( PT_SLIDE, 1.0f, (float)kMaxSlides );
	SetParamInfof( PT_RELOAD, "Reload", FF_TYPE_EVENT );

	SetParamInfo( PT_LOOP, "Loop", FF_TYPE_BOOLEAN, false );
	SetParamInfo( PT_HIDE_WINDOW, "Hide PowerPoint Window", FF_TYPE_BOOLEAN, true );

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
	SetParamGroup( PT_CONVERTER, "Options" );
	SetParamGroup( PT_LOOP, "Options" );
	SetParamGroup( PT_HIDE_WINDOW, "Options" );

	SetParamInfo( PT_STATUS, "Status", FF_TYPE_TEXT, "" );

#if !defined( _WIN32 )
	SetParamVisibility( PT_HIDE_WINDOW, false, false );// macOS: the show runs in a normal window
#endif
	updateVisibility( false );
}

PresentationPlugin::~PresentationPlugin()
{
	// Bounded wait: a hanging PowerPoint can never freeze Resolume for long.
	engine->shutdown();
}

FFResult PresentationPlugin::InitGL( const FFGLViewportStruct* vp )
{
	if( !renderer.init() )
	{
		DeInitGL();
		return FF_FAIL;
	}
	return CFFGLPlugin::InitGL( vp );
}

FFResult PresentationPlugin::DeInitGL()
{
	renderer.release();
	return FF_SUCCESS;
}

void PresentationPlugin::updateVisibility( bool raiseEvents )
{
	bool url = std::lround( sourceKind ) == 1;
	SetParamVisibility( PT_FILE, !url, raiseEvents );
	SetParamVisibility( PT_URL, url, raiseEvents );
	SetParamVisibility( PT_MODE, !url, raiseEvents );
}

void PresentationPlugin::scheduleLoad( bool forceRefresh )
{
	loadPending = true;
	forceReload = forceReload || forceRefresh;
}

FFResult PresentationPlugin::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	int vw = (int)currentViewport.width;
	int vh = (int)currentViewport.height;
	if( vw != lastViewportW || vh != lastViewportH )
	{
		lastViewportW = vw;
		lastViewportH = vh;
		engine->viewportChanged( vw, vh );
	}

	if( loadPending )
	{
		PresentationEngine::LoadRequest req;
		req.isUrl        = std::lround( sourceKind ) == 1;
		req.input        = req.isUrl ? slidesUrl : filePath;
		req.mode         = (PresentationEngine::Mode)std::clamp( (int)std::lround( modeValue ), 0, 2 );
		req.convert      = (ConverterChoice)std::clamp( (int)std::lround( converterValue ), 0, 2 );
		req.forceRefresh = forceReload;
		req.loop         = loop;
		req.hideWindow   = hideWindow;
		req.width        = std::clamp( vw > 0 ? vw : 1920, 64, 7680 );
		req.height       = std::clamp( vh > 0 ? vh : 1080, 64, 4320 );
		loadPending      = false;
		forceReload      = false;
		engine->load( req );
	}

	// Animated mode: follow slide changes made inside PowerPoint (timings, clicks).
	auto now = std::chrono::steady_clock::now();
	if( now - lastPoll > std::chrono::milliseconds( 750 ) )
	{
		lastPoll = now;
		engine->pollCurrentSlide();
	}

	int cs = engine->currentSlide();
	if( cs > 0 && cs != reportedSlide )
	{
		reportedSlide = cs;
		slideValue    = (float)cs;
		RaiseParamEvent( PT_SLIDE, FF_EVENT_FLAG_VALUE );
	}

	if( engine->takeStatus( statusText ) )
		RaiseParamEvent( PT_STATUS, FF_EVENT_FLAG_VALUE );

	renderer.update( engine->slot );
	Crop c;
	c.left   = crop[ 0 ] * 0.5f;
	c.right  = crop[ 1 ] * 0.5f;
	c.top    = crop[ 2 ] * 0.5f;
	c.bottom = crop[ 3 ] * 0.5f;
	renderer.draw( vw, vh, (FitMode)std::lround( fitMode ), c );
	return FF_SUCCESS;
}

// ---------------------------------------------------------------- params

FFResult PresentationPlugin::SetFloatParameter( unsigned int index, float value )
{
	switch( index )
	{
	case PT_SOURCE:
		if( value != sourceKind )
		{
			sourceKind = value;
			updateVisibility( true );
			scheduleLoad( false );
		}
		break;
	case PT_MODE:
		if( value != modeValue )
		{
			modeValue = value;
			scheduleLoad( false );
		}
		break;
	case PT_CONVERTER:
		if( value != converterValue )
		{
			converterValue = value;
			if( !animatedActive() )
				scheduleLoad( false );
		}
		break;
	case PT_PREV:
		if( value > 0.5f )
			engine->navigate( -1, loop );
		break;
	case PT_NEXT:
		if( value > 0.5f )
			engine->navigate( +1, loop );
		break;
	case PT_FIRST:
		if( value > 0.5f )
			engine->gotoSlide( 1 );
		break;
	case PT_SLIDE:
	{
		int v = (int)std::lround( value );
		if( v != (int)std::lround( slideValue ) )
		{
			slideValue = (float)v;
			if( v != engine->currentSlide() )
				engine->gotoSlide( v );
		}
		break;
	}
	case PT_RELOAD:
		if( value > 0.5f )
			scheduleLoad( true );
		break;
	case PT_LOOP:
		if( ( value > 0.5f ) != loop )
		{
			loop = value > 0.5f;
			if( animatedActive() )
				scheduleLoad( false );
		}
		break;
	case PT_HIDE_WINDOW:
		if( ( value > 0.5f ) != hideWindow )
		{
			hideWindow = value > 0.5f;
			if( animatedActive() )
				scheduleLoad( false );
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

float PresentationPlugin::GetFloatParameter( unsigned int index )
{
	switch( index )
	{
	case PT_SOURCE:
		return sourceKind;
	case PT_MODE:
		return modeValue;
	case PT_CONVERTER:
		return converterValue;
	case PT_SLIDE:
		return slideValue;
	case PT_LOOP:
		return loop ? 1.0f : 0.0f;
	case PT_HIDE_WINDOW:
		return hideWindow ? 1.0f : 0.0f;
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

FFResult PresentationPlugin::SetTextParameter( unsigned int index, const char* value )
{
	std::string v = value ? value : "";
	switch( index )
	{
	case PT_FILE:
		if( v != filePath )
		{
			filePath = v;
			if( std::lround( sourceKind ) == 0 )
				scheduleLoad( false );
		}
		return FF_SUCCESS;
	case PT_URL:
	{
		// trim whitespace from pasted links
		size_t b = v.find_first_not_of( " \t\r\n" );
		size_t e = v.find_last_not_of( " \t\r\n" );
		v        = b == std::string::npos ? std::string() : v.substr( b, e - b + 1 );
		if( v != slidesUrl )
		{
			slidesUrl = v;
			if( std::lround( sourceKind ) == 1 )
				scheduleLoad( false );
		}
		return FF_SUCCESS;
	}
	case PT_STATUS:
		return FF_SUCCESS;
	}
	return FF_FAIL;
}

char* PresentationPlugin::GetTextParameter( unsigned int index )
{
	static thread_local std::string buffer;
	switch( index )
	{
	case PT_FILE:
		buffer = filePath;
		break;
	case PT_URL:
		buffer = slidesUrl;
		break;
	case PT_STATUS:
		buffer = statusText;
		break;
	default:
		buffer.clear();
	}
	return const_cast< char* >( buffer.c_str() );
}
