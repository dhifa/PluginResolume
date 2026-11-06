// PowerPoint automation through late-bound COM (works with any Office version,
// no type library import needed). All calls must come from one STA thread.
#include "WinUtil.h"
#include "../common/Platform.h"
#include "../common/PowerPoint.h"

#include <oleauto.h>
#include <objbase.h>
#include <cmath>
#include <vector>

namespace
{
const long msoTrue  = -1;
const long msoFalse = 0;
const long ppShowTypeSpeaker = 1;
const long ppSaveAsPDF       = 32;
const long ppSlideShowDone   = 5;

// --- tiny VARIANT / IDispatch helpers -----------------------------------
struct Var
{
	VARIANT v;
	Var() { VariantInit( &v ); }
	Var( long l )
	{
		VariantInit( &v );
		v.vt   = VT_I4;
		v.lVal = l;
	}
	Var( float f )
	{
		VariantInit( &v );
		v.vt     = VT_R4;
		v.fltVal = f;
	}
	Var( const std::wstring& s )
	{
		VariantInit( &v );
		v.vt      = VT_BSTR;
		v.bstrVal = SysAllocString( s.c_str() );
	}
	Var( const Var& o )
	{
		VariantInit( &v );
		VariantCopy( &v, const_cast< VARIANT* >( &o.v ) );
	}
	Var& operator=( const Var& o )
	{
		if( this != &o )
		{
			VariantClear( &v );
			VariantCopy( &v, const_cast< VARIANT* >( &o.v ) );
		}
		return *this;
	}
	~Var() { VariantClear( &v ); }

	long toLong( long fallback = 0 ) const
	{
		VARIANT tmp;
		VariantInit( &tmp );
		long r = fallback;
		if( SUCCEEDED( VariantChangeType( &tmp, const_cast< VARIANT* >( &v ), 0, VT_I4 ) ) )
			r = tmp.lVal;
		VariantClear( &tmp );
		return r;
	}
	IDispatch* disp() const { return v.vt == VT_DISPATCH ? v.pdispVal : nullptr; }
};

class Disp
{
public:
	Disp() = default;
	explicit Disp( IDispatch* d, bool addRef = true ) :
		p( d )
	{
		if( p && addRef )
			p->AddRef();
	}
	Disp( const Disp& o ) :
		p( o.p )
	{
		if( p )
			p->AddRef();
	}
	Disp& operator=( const Disp& o )
	{
		if( o.p )
			o.p->AddRef();
		if( p )
			p->Release();
		p = o.p;
		return *this;
	}
	~Disp()
	{
		if( p )
			p->Release();
	}
	explicit operator bool() const { return p != nullptr; }
	void reset()
	{
		if( p )
			p->Release();
		p = nullptr;
	}

	// args in natural (left to right) order
	HRESULT invoke( WORD flags, const wchar_t* name, const std::vector< Var >& args, Var* result ) const
	{
		if( !p )
			return E_POINTER;
		DISPID id;
		LPOLESTR n = const_cast< LPOLESTR >( name );
		HRESULT hr = p->GetIDsOfNames( IID_NULL, &n, 1, LOCALE_USER_DEFAULT, &id );
		if( FAILED( hr ) )
			return hr;

		std::vector< VARIANT > rev( args.size() );
		for( size_t i = 0; i < args.size(); ++i )
			rev[ args.size() - 1 - i ] = args[ i ].v;// shallow copy, args keep ownership

		DISPPARAMS dp      = {};
		DISPID putId       = DISPID_PROPERTYPUT;
		dp.cArgs           = (UINT)rev.size();
		dp.rgvarg          = rev.empty() ? nullptr : rev.data();
		if( flags & DISPATCH_PROPERTYPUT )
		{
			dp.cNamedArgs        = 1;
			dp.rgdispidNamedArgs = &putId;
		}
		Var tmp;
		EXCEPINFO ex = {};
		hr           = p->Invoke( id, IID_NULL, LOCALE_USER_DEFAULT, flags, &dp, result ? &result->v : &tmp.v, &ex, nullptr );
		SysFreeString( ex.bstrSource );
		SysFreeString( ex.bstrDescription );
		SysFreeString( ex.bstrHelpFile );
		return hr;
	}

	Disp getObj( const wchar_t* name, const std::vector< Var >& args = {} ) const
	{
		Var r;
		if( FAILED( invoke( DISPATCH_PROPERTYGET | DISPATCH_METHOD, name, args, &r ) ) || !r.disp() )
			return Disp();
		return Disp( r.disp() );
	}
	bool get( const wchar_t* name, Var& out ) const { return SUCCEEDED( invoke( DISPATCH_PROPERTYGET, name, {}, &out ) ); }
	long getLong( const wchar_t* name, long fallback = 0 ) const
	{
		Var r;
		return get( name, r ) ? r.toLong( fallback ) : fallback;
	}
	bool put( const wchar_t* name, const Var& value ) const { return SUCCEEDED( invoke( DISPATCH_PROPERTYPUT, name, { value }, nullptr ) ); }
	bool call( const wchar_t* name, const std::vector< Var >& args = {}, Var* result = nullptr ) const
	{
		return SUCCEEDED( invoke( DISPATCH_METHOD, name, args, result ) );
	}
	Disp callObj( const wchar_t* name, const std::vector< Var >& args = {} ) const
	{
		Var r;
		if( !call( name, args, &r ) || !r.disp() )
			return Disp();
		return Disp( r.disp() );
	}

	IDispatch* p = nullptr;
};

bool IsPowerPointRunning()
{
	CLSID clsid;
	if( FAILED( CLSIDFromProgID( L"PowerPoint.Application", &clsid ) ) )
		return false;
	IUnknown* unk = nullptr;
	if( SUCCEEDED( GetActiveObject( clsid, nullptr, &unk ) ) && unk )
	{
		unk->Release();
		return true;
	}
	return false;
}

Disp ConnectPowerPoint( std::string* error )
{
	CLSID clsid;
	if( FAILED( CLSIDFromProgID( L"PowerPoint.Application", &clsid ) ) )
	{
		if( error )
			*error = "Microsoft PowerPoint is not installed";
		return Disp();
	}
	IDispatch* d = nullptr;
	HRESULT hr   = CoCreateInstance( clsid, nullptr, CLSCTX_LOCAL_SERVER, IID_IDispatch, (void**)&d );
	if( FAILED( hr ) || !d )
	{
		if( error )
			*error = "Could not start PowerPoint (0x" + plat::Hex64( (uint32_t)hr ).substr( 8 ) + ")";
		return Disp();
	}
	return Disp( d, false );
}

Disp OpenPresentation( const Disp& app, const std::string& path, std::string* error )
{
	Disp presentations = app.getObj( L"Presentations" );
	// Open(FileName, ReadOnly, Untitled, WithWindow)
	Disp pres = presentations.callObj( L"Open", { Var( WinPath( path ) ), Var( msoTrue ), Var( msoFalse ), Var( msoFalse ) } );
	if( !pres && error )
		*error = "PowerPoint could not open " + plat::FileName( path );
	return pres;
}

void QuitIfIdle( Disp& app, bool weStartedIt )
{
	if( !app || !weStartedIt )
		return;
	Disp presentations = app.getObj( L"Presentations" );
	if( presentations && presentations.getLong( L"Count", 1 ) == 0 )
		app.call( L"Quit" );
}
}// namespace

class WinPowerPointShow : public PowerPointShow
{
public:
	~WinPowerPointShow() override { close(); }

	bool open( const std::string& path, const PowerPointShowOptions& opt, std::string* error ) override
	{
		close();
		options      = opt;
		startedByUs  = !IsPowerPointRunning();
		app          = ConnectPowerPoint( error );
		if( !app )
			return false;
		pres = OpenPresentation( app, path, error );
		if( !pres )
		{
			QuitIfIdle( app, startedByUs );
			app.reset();
			return false;
		}

		Disp slides = pres.getObj( L"Slides" );
		count       = slides ? (int)slides.getLong( L"Count" ) : 0;

		Disp settings = pres.getObj( L"SlideShowSettings" );
		if( !settings )
		{
			if( error )
				*error = "No SlideShowSettings";
			close();
			return false;
		}
		// A normal (speaker) show has no window chrome. Afterwards we resize it into a window.
		settings.put( L"ShowType", Var( ppShowTypeSpeaker ) );
		settings.put( L"ShowPresenterView", Var( msoFalse ) );
		settings.put( L"ShowWithAnimation", Var( msoTrue ) );
		settings.put( L"LoopUntilStopped", Var( opt.loop ? msoTrue : msoFalse ) );

		HWND previousForeground = GetForegroundWindow();
		window                  = settings.callObj( L"Run" );
		if( !window )
		{
			if( error )
				*error = "Could not start the slide show";
			close();
			return false;
		}
		view = window.getObj( L"View" );
		hwnd = (HWND)(intptr_t)window.getLong( L"HWND" );
		placeWindow();
		if( previousForeground )
			SetForegroundWindow( previousForeground );// give focus back to Resolume
		return hwnd != nullptr;
	}

	void close() override
	{
		if( view )
			view.call( L"Exit" );
		view.reset();
		window.reset();
		if( pres )
			pres.call( L"Close" );
		pres.reset();
		QuitIfIdle( app, startedByUs );
		app.reset();
		hwnd  = nullptr;
		count = 0;
	}

	bool isOpen() override
	{
		if( !view || !hwnd || !IsWindow( hwnd ) )
			return false;
		long state = view.getLong( L"State", ppSlideShowDone );
		return state != ppSlideShowDone;
	}

	bool next() override
	{
		if( !view )
			return false;
		if( !options.loop )
		{
			// Don't run into PowerPoint's black "End of slide show" screen.
			int cur = currentSlide();
			if( cur >= count && count > 0 )
			{
				Var clickIndex, clickCount;
				bool ok1 = SUCCEEDED( view.invoke( DISPATCH_METHOD, L"GetClickIndex", {}, &clickIndex ) );
				bool ok2 = SUCCEEDED( view.invoke( DISPATCH_METHOD, L"GetClickCount", {}, &clickCount ) );
				if( !ok1 || !ok2 || clickIndex.toLong() >= clickCount.toLong() )
					return false;
			}
		}
		return view.call( L"Next" );
	}

	bool previous() override { return view && view.call( L"Previous" ); }

	bool gotoSlide( int oneBased ) override
	{
		if( !view )
			return false;
		// GotoSlide(Index, ResetSlide)
		return view.call( L"GotoSlide", { Var( (long)oneBased ), Var( msoTrue ) } );
	}

	int currentSlide() override
	{
		if( !view )
			return 0;
		Disp slide = view.getObj( L"Slide" );
		if( slide )
			return (int)slide.getLong( L"SlideIndex" );
		return (int)view.getLong( L"CurrentShowPosition" );
	}

	int slideCount() override { return count; }

	bool captureTarget( CaptureTarget& out ) override
	{
		if( !hwnd )
			return false;
		out        = CaptureTarget();
		out.kind   = TargetKind::Window;
		out.id     = (uint64_t)(uintptr_t)hwnd;
		out.owner  = "POWERPNT.EXE";
		out.title  = "Slide Show";
		out.name   = "PowerPoint Slide Show";
		out.width  = options.widthPx;
		out.height = options.heightPx;
		return true;
	}

private:
	// SlideShowWindow positions are in points. Measure the actual pixel/point
	// ratio once, because it depends on PowerPoint's DPI awareness.
	void placeWindow()
	{
		if( !window || !hwnd )
			return;
		float ratio = 96.0f / 72.0f;
		for( int pass = 0; pass < 2; ++pass )
		{
			float wPt = options.widthPx / ratio, hPt = options.heightPx / ratio;
			float xPx = 0, yPx = 0;
			if( options.hideWindow )
			{
				// Left of the whole virtual desktop: invisible, but still rendered by DWM.
				xPx = float( GetSystemMetrics( SM_XVIRTUALSCREEN ) - options.widthPx - 200 );
				yPx = float( GetSystemMetrics( SM_YVIRTUALSCREEN ) );
			}
			window.put( L"Left", Var( xPx / ratio ) );
			window.put( L"Top", Var( yPx / ratio ) );
			window.put( L"Width", Var( wPt ) );
			window.put( L"Height", Var( hPt ) );

			RECT r;
			if( !GetWindowRect( hwnd, &r ) || r.right <= r.left )
				break;
			float actual = float( r.right - r.left ) / wPt;
			if( std::fabs( actual - ratio ) < 0.01f )
				break;
			ratio = actual;
		}
	}

	PowerPointShowOptions options;
	Disp app, pres, window, view;
	HWND hwnd        = nullptr;
	int count        = 0;
	bool startedByUs = false;
};

std::unique_ptr< PowerPointShow > CreatePowerPointShow()
{
	return std::make_unique< WinPowerPointShow >();
}

bool PowerPointExportPdf( const std::string& input, const std::string& outputPdf, std::string* error )
{
	bool startedByUs = !IsPowerPointRunning();
	Disp app         = ConnectPowerPoint( error );
	if( !app )
		return false;
	Disp pres = OpenPresentation( app, input, error );
	if( !pres )
	{
		QuitIfIdle( app, startedByUs );
		return false;
	}
	bool ok = pres.call( L"SaveAs", { Var( WinPath( outputPdf ) ), Var( ppSaveAsPDF ) } );
	pres.call( L"Close" );
	pres.reset();
	QuitIfIdle( app, startedByUs );
	if( !ok && error )
		*error = "SaveAs PDF failed";
	return ok;
}
