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
static const size_t kCachePages = 8;

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

	plat::SetLogger( []( const std::string& m ) { FFGLLog::LogToHost( ( "SC Presentation: " + m ).c_str() ); } );

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

	mainWorker.post( [ this ] {
		capture = CreateScreenCapture();
		if( capture )
			capture->setStoppedCallback( [ this ]( const std::string& reason ) {
				if( activeMode == (int)Active::Animated )
					setStatus( "Slide show stopped (" + reason + "). Press Reload" );
			} );
	} );
}

PresentationPlugin::~PresentationPlugin()
{
	loadGeneration++;
	mainWorker.clearPending();
	mainWorker.post( [ this ] { unloadAll(); } );
	mainWorker.stop();
	pptWorker.post( [ this ] { show.reset(); } );
	pptWorker.stop();
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

void PresentationPlugin::setStatus( const std::string& s )
{
	std::lock_guard< std::mutex > lock( statusMutex );
	pendingStatus = s;
	statusDirty   = true;
	plat::Log( s );
}

void PresentationPlugin::updateVisibility( bool raiseEvents )
{
	bool url = std::lround( sourceKind ) == 1;
	SetParamVisibility( PT_FILE, !url, raiseEvents );
	SetParamVisibility( PT_URL, url, raiseEvents );
	SetParamVisibility( PT_MODE, !url, raiseEvents );
}

// ---------------------------------------------------------------- host side

void PresentationPlugin::scheduleLoad( bool forceRefresh )
{
	loadPending = true;
	forceReload = forceReload || forceRefresh;
}

void PresentationPlugin::navigate( int delta )
{
	Active a = (Active)activeMode.load();
	if( a == Active::Animated )
	{
		pptWorker.post( [ this, delta ] {
			if( !show || !show->isOpen() )
				return;
			if( delta > 0 )
				show->next();
			else
				show->previous();
			currentSlide = show->currentSlide();
		} );
	}
	else if( a == Active::Static )
	{
		int count = slideCount.load();
		if( count <= 0 )
			return;
		int p = requestedPage.load() + delta;
		if( loop )
			p = ( p % count + count ) % count;
		else
			p = std::clamp( p, 0, count - 1 );
		requestedPage = p;
		currentSlide  = p + 1;
		mainWorker.post( [ this ] { renderRequestedPage(); } );
	}
}

void PresentationPlugin::gotoSlide( int oneBased )
{
	Active a = (Active)activeMode.load();
	int count = slideCount.load();
	if( count <= 0 )
		return;
	oneBased = std::clamp( oneBased, 1, count );
	if( a == Active::Animated )
	{
		pptWorker.post( [ this, oneBased ] {
			if( !show || !show->isOpen() )
				return;
			show->gotoSlide( oneBased );
			currentSlide = show->currentSlide();
		} );
	}
	else if( a == Active::Static )
	{
		requestedPage = oneBased - 1;
		currentSlide  = oneBased;
		mainWorker.post( [ this ] { renderRequestedPage(); } );
	}
}

FFResult PresentationPlugin::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	int vw = (int)currentViewport.width;
	int vh = (int)currentViewport.height;
	if( vw != lastViewportW || vh != lastViewportH )
	{
		lastViewportW = vw;
		lastViewportH = vh;
		if( (Active)activeMode.load() == Active::Static )
			mainWorker.post( [ this, vw, vh ] {
				renderW = vw;
				renderH = vh;
				pageCache.clear();
				shownPage = -1;
				renderRequestedPage();
			} );
	}

	if( loadPending )
	{
		LoadRequest req;
		req.generation   = ++loadGeneration;
		req.isUrl        = std::lround( sourceKind ) == 1;
		req.input        = req.isUrl ? slidesUrl : filePath;
		req.mode         = (Mode)std::clamp( (int)std::lround( modeValue ), 0, 2 );
		req.convert      = (ConverterChoice)std::clamp( (int)std::lround( converterValue ), 0, 2 );
		req.forceRefresh = forceReload;
		req.loop         = loop;
		req.hideWindow   = hideWindow;
		req.width        = std::clamp( vw > 0 ? vw : 1920, 64, 7680 );
		req.height       = std::clamp( vh > 0 ? vh : 1080, 64, 4320 );
		loadPending      = false;
		forceReload      = false;

		mainWorker.clearPending();
		mainWorker.post( [ this, req ] { doLoad( req ); } );
	}

	// Animated mode: follow slide changes made inside PowerPoint (timings, clicks).
	auto now = std::chrono::steady_clock::now();
	if( (Active)activeMode.load() == Active::Animated && now - lastPoll > std::chrono::milliseconds( 750 ) && pptWorker.pending() == 0 )
	{
		lastPoll = now;
		pptWorker.post( [ this ] {
			if( show && show->isOpen() )
				currentSlide = show->currentSlide();
		} );
	}

	int cs = currentSlide.load();
	if( cs > 0 && cs != reportedSlide )
	{
		reportedSlide = cs;
		slideValue    = (float)cs;
		RaiseParamEvent( PT_SLIDE, FF_EVENT_FLAG_VALUE );
	}

	if( statusDirty.exchange( false ) )
	{
		std::lock_guard< std::mutex > lock( statusMutex );
		statusText = pendingStatus;
		RaiseParamEvent( PT_STATUS, FF_EVENT_FLAG_VALUE );
	}

	renderer.update( slot );
	Crop c;
	c.left   = crop[ 0 ] * 0.5f;
	c.right  = crop[ 1 ] * 0.5f;
	c.top    = crop[ 2 ] * 0.5f;
	c.bottom = crop[ 3 ] * 0.5f;
	renderer.draw( vw, vh, (FitMode)std::lround( fitMode ), c );
	return FF_SUCCESS;
}

// ---------------------------------------------------------------- workers

void PresentationPlugin::unloadAll()
{
	if( capture )
		capture->stop();
	pptWorker.call( [ this ] {
		if( show )
			show->close();
		show.reset();
		return 0;
	} );
	pdf.reset();
	pageCache.clear();
	shownPage   = -1;
	activeMode  = (int)Active::None;
	slideCount  = 0;
	currentSlide = 0;
	slot.clear();
}

void PresentationPlugin::doLoad( const LoadRequest& req )
{
	unloadAll();
	if( cancelled( req.generation ) )
		return;

	if( req.input.empty() )
	{
		setStatus( req.isUrl ? "Paste a Google Slides share link" : "Choose a PPT/PPTX or PDF file" );
		return;
	}

	std::string ext   = plat::Extension( req.input );
	bool isPowerPoint = !req.isUrl && IsPowerPointExtension( ext );
	bool hasPowerPoint = plat::IsPowerPointInstalled();

	bool animated = false;
	if( isPowerPoint )
	{
		if( req.mode == Mode::Animated )
		{
			if( !hasPowerPoint )
			{
				setStatus( "Animated mode needs Microsoft PowerPoint. Install it or set Mode to Static" );
				return;
			}
			animated = true;
		}
		else if( req.mode == Mode::Auto )
		{
			animated = hasPowerPoint;
		}
	}

	if( animated )
	{
		setStatus( "Starting PowerPoint slide show..." );
		PowerPointShowOptions opt;
		opt.widthPx    = req.width;
		opt.heightPx   = req.height;
		opt.hideWindow = req.hideWindow;
		opt.loop       = req.loop;

		std::string err;
		CaptureTarget target;
		bool ok = pptWorker.call( [ & ] {
			show = CreatePowerPointShow();
			if( !show || !show->open( req.input, opt, &err ) )
				return false;
			slideCount   = show->slideCount();
			currentSlide = show->currentSlide();
			return show->captureTarget( target );
		} );
		if( cancelled( req.generation ) )
			return;
		if( !ok )
		{
			setStatus( "PowerPoint: " + ( err.empty() ? std::string( "could not start the slide show" ) : err ) );
			return;
		}
		if( !capture )
		{
			setStatus( "Screen capture not available" );
			return;
		}
		capture->setClientAreaOnly( true );
		if( !capture->start( target, false, &slot ) )
		{
			setStatus( "Capture of the slide show failed: " + capture->lastError() );
			return;
		}
		activeMode = (int)Active::Animated;
		setStatus( "Animated: " + plat::FileName( req.input ) + " (" + std::to_string( slideCount.load() ) + " slides)" );
		return;
	}

	// Static: everything becomes a PDF first.
	PrepareRequest prep;
	prep.input        = req.input;
	prep.isUrl        = req.isUrl;
	prep.converter    = req.convert;
	prep.forceRefresh = req.forceRefresh;
	prep.cancelled    = [ this, gen = req.generation ] { return cancelled( gen ); };
	prep.exportWithPowerPoint = [ this ]( const std::string& in, const std::string& out, std::string* err ) {
		return pptWorker.call( [ & ] { return PowerPointExportPdf( in, out, err ); } );
	};

	std::string pdfPath, err;
	bool ok = PrepareStaticPdf( prep, pdfPath, err, [ this ]( const std::string& s ) { setStatus( s ); } );
	if( cancelled( req.generation ) )
		return;
	if( !ok )
	{
		setStatus( "Error: " + err );
		return;
	}

	pdf = OpenPdfDocument( pdfPath, &err );
	if( !pdf || pdf->pageCount() <= 0 )
	{
		pdf.reset();
		setStatus( "Could not open PDF: " + err );
		return;
	}

	renderW       = req.width;
	renderH       = req.height;
	slideCount    = pdf->pageCount();
	requestedPage = 0;
	currentSlide  = 1;
	activeMode    = (int)Active::Static;
	renderRequestedPage();

	std::string what = req.isUrl ? std::string( "Google Slides" ) : plat::FileName( req.input );
	std::string note = isPowerPoint ? " - no animation" : "";
	setStatus( "Static: " + what + " (" + std::to_string( slideCount.load() ) + " slides" + note + ")" );
}

bool PresentationPlugin::renderPageCached( int page, int w, int h, Frame*& out )
{
	for( auto it = pageCache.begin(); it != pageCache.end(); ++it )
	{
		if( it->page == page )
		{
			pageCache.splice( pageCache.begin(), pageCache, it );
			out = &pageCache.front().frame;
			return true;
		}
	}
	CachedPage entry;
	entry.page = page;
	if( !pdf->renderPage( page, w, h, entry.frame ) )
		return false;
	pageCache.push_front( std::move( entry ) );
	while( pageCache.size() > kCachePages )
		pageCache.pop_back();
	out = &pageCache.front().frame;
	return true;
}

void PresentationPlugin::renderRequestedPage()
{
	if( !pdf || (Active)activeMode.load() != Active::Static )
		return;
	int page = std::clamp( requestedPage.load(), 0, pdf->pageCount() - 1 );
	if( page != shownPage )
	{
		Frame* frame = nullptr;
		if( !renderPageCached( page, renderW, renderH, frame ) )
		{
			setStatus( "Could not render page " + std::to_string( page + 1 ) );
			return;
		}
		slot.publish( *frame );
		shownPage = page;
	}
	// Pre-render the next page while idle so "Next" is instant.
	int next = page + 1;
	if( next < pdf->pageCount() && mainWorker.pending() == 0 )
	{
		Frame* ignored = nullptr;
		renderPageCached( next, renderW, renderH, ignored );
		// keep the visible page at the front of the cache
		renderPageCached( page, renderW, renderH, ignored );
	}
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
			if( (Active)activeMode.load() != Active::Animated )
				scheduleLoad( false );
		}
		break;
	case PT_PREV:
		if( value > 0.5f )
			navigate( -1 );
		break;
	case PT_NEXT:
		if( value > 0.5f )
			navigate( +1 );
		break;
	case PT_FIRST:
		if( value > 0.5f )
			gotoSlide( 1 );
		break;
	case PT_SLIDE:
	{
		int v = (int)std::lround( value );
		if( v != (int)std::lround( slideValue ) )
		{
			slideValue = (float)v;
			if( v != currentSlide.load() )
				gotoSlide( v );
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
			if( (Active)activeMode.load() == Active::Animated )
				scheduleLoad( false );
		}
		break;
	case PT_HIDE_WINDOW:
		if( ( value > 0.5f ) != hideWindow )
		{
			hideWindow = value > 0.5f;
			if( (Active)activeMode.load() == Active::Animated )
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
