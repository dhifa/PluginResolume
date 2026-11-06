#include "PresentationEngine.h"
#include <algorithm>

static const size_t kCachePages = 8;

std::shared_ptr< PresentationEngine > PresentationEngine::create()
{
	std::shared_ptr< PresentationEngine > e( new PresentationEngine() );
	e->mainWorker->post( [ e ] {
		e->capture = CreateScreenCapture();
		if( e->capture )
			e->capture->setStoppedCallback( [ ep = e.get() ]( const std::string& reason ) {
				if( ep->activeMode == (int)Active::Animated )
					ep->setStatus( "Slide show stopped (" + reason + "). Press Reload" );
			} );
	} );
	return e;
}

void PresentationEngine::setStatus( const std::string& s )
{
	{
		std::lock_guard< std::mutex > lock( statusMutex );
		pendingStatus = s;
		statusDirty   = true;
	}
}

bool PresentationEngine::takeStatus( std::string& out )
{
	std::lock_guard< std::mutex > lock( statusMutex );
	if( !statusDirty )
		return false;
	out         = pendingStatus;
	statusDirty = false;
	return true;
}

// ---------------------------------------------------------------- host side

void PresentationEngine::load( LoadRequest req )
{
	req.generation = ++loadGeneration;
	mainWorker->clearPending();
	mainWorker->post( [ self = shared_from_this(), req ] { self->doLoad( req ); } );
}

void PresentationEngine::navigate( int delta, bool loop )
{
	Active a = active();
	if( a == Active::Animated )
	{
		if( pptWorker->pending() > 4 )
			return;// PowerPoint is busy, don't pile up clicks
		pptWorker->post( [ self = shared_from_this(), delta ] {
			if( !self->show || !self->show->isOpen() )
				return;
			if( delta > 0 )
				self->show->next();
			else
				self->show->previous();
			self->currentSlideNo = self->show->currentSlide();
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
		requestedPage  = p;
		currentSlideNo = p + 1;
		mainWorker->post( [ self = shared_from_this() ] { self->renderRequestedPage(); } );
	}
}

void PresentationEngine::gotoSlide( int oneBased )
{
	int count = slideCount.load();
	if( count <= 0 )
		return;
	oneBased = std::clamp( oneBased, 1, count );
	Active a = active();
	if( a == Active::Animated )
	{
		pptWorker->post( [ self = shared_from_this(), oneBased ] {
			if( !self->show || !self->show->isOpen() )
				return;
			self->show->gotoSlide( oneBased );
			self->currentSlideNo = self->show->currentSlide();
		} );
	}
	else if( a == Active::Static )
	{
		requestedPage  = oneBased - 1;
		currentSlideNo = oneBased;
		mainWorker->post( [ self = shared_from_this() ] { self->renderRequestedPage(); } );
	}
}

void PresentationEngine::viewportChanged( int w, int h )
{
	if( active() != Active::Static )
		return;
	mainWorker->post( [ self = shared_from_this(), w, h ] {
		self->renderW = w;
		self->renderH = h;
		self->pageCache.clear();
		self->shownPage = -1;
		self->renderRequestedPage();
	} );
}

void PresentationEngine::pollCurrentSlide()
{
	if( active() != Active::Animated || pptWorker->pending() != 0 )
		return;
	pptWorker->post( [ self = shared_from_this() ] {
		if( self->show && self->show->isOpen() )
			self->currentSlideNo = self->show->currentSlide();
	} );
}

void PresentationEngine::shutdown()
{
	loadGeneration++;// cancels a running conversion / download
	mainWorker->clearPending();
	pptWorker->clearPending();
	mainWorker->post( [ self = shared_from_this() ] { self->unloadAll(); } );
	bool mainDone = mainWorker->shutdown( 3000 );
	pptWorker->post( [ self = shared_from_this() ] {
		if( self->show )
			self->show->close();
		self->show.reset();
	} );
	bool pptDone = pptWorker->shutdown( mainDone ? 3000 : 500 );
	if( !mainDone || !pptDone )
		plat::Log( "workers did not stop in time (PowerPoint busy?), detached" );
}

// ---------------------------------------------------------------- workers

void PresentationEngine::unloadAll()
{
	if( capture )
		capture->stop();
	try
	{
		pptWorker->call( [ this ] {
			if( show )
				show->close();
			show.reset();
			return 0;
		} );
	}
	catch( ... )
	{
		// PowerPoint worker already stopped (plugin is being destroyed)
	}
	pdf.reset();
	pageCache.clear();
	shownPage      = -1;
	activeMode     = (int)Active::None;
	slideCount     = 0;
	currentSlideNo = 0;
	slot.clear();
}

void PresentationEngine::doLoad( const LoadRequest& req )
{
	unloadAll();
	if( cancelled( req.generation ) )
		return;

	if( req.input.empty() )
	{
		setStatus( req.isUrl ? "Paste a Google Slides share link" : "Choose a PPT/PPTX or PDF file" );
		return;
	}

	std::string ext    = plat::Extension( req.input );
	bool isPowerPoint  = !req.isUrl && IsPowerPointExtension( ext );
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
		bool ok = pptWorker->call( [ & ] {
			show = CreatePowerPointShow();
			if( !show || !show->open( req.input, opt, &err ) )
				return false;
			slideCount     = show->slideCount();
			currentSlideNo = show->currentSlide();
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
		return pptWorker->call( [ & ] { return PowerPointExportPdf( in, out, err ); } );
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

	renderW        = req.width;
	renderH        = req.height;
	slideCount     = pdf->pageCount();
	requestedPage  = 0;
	currentSlideNo = 1;
	activeMode     = (int)Active::Static;
	renderRequestedPage();

	std::string what = req.isUrl ? std::string( "Google Slides" ) : plat::FileName( req.input );
	std::string note = isPowerPoint ? " - no animation" : "";
	setStatus( "Static: " + what + " (" + std::to_string( slideCount.load() ) + " slides" + note + ")" );
}

bool PresentationEngine::renderPageCached( int page, int w, int h, Frame*& out )
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

void PresentationEngine::renderRequestedPage()
{
	if( !pdf || active() != Active::Static )
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
	if( next < pdf->pageCount() && mainWorker->pending() == 0 )
	{
		Frame* ignored = nullptr;
		renderPageCached( next, renderW, renderH, ignored );
		renderPageCached( page, renderW, renderH, ignored );// keep the visible page most recent
	}
}
