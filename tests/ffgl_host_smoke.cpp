// Minimal headless FFGL host used to smoke-test the plugins on Linux (EGL + Mesa).
// It is not part of the shipped plugins.
#include <GL/glew.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>
#include <ffgl/FFGL.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

typedef FFMixed ( *MainFn )( FFUInt32, FFMixed, FFInstanceID );
static MainFn plug;
static FFInstanceID inst;
static int failures = 0;

#define CHECK( cond, msg )                                   \
	do                                                       \
	{                                                        \
		if( !( cond ) )                                      \
		{                                                    \
			std::printf( "  FAIL: %s\n", msg );              \
			++failures;                                      \
		}                                                    \
		else                                                 \
			std::printf( "  ok:   %s\n", msg );              \
	} while( 0 )

static FFMixed U( FFUInt32 v )
{
	FFMixed m;
	m.UIntValue = v;
	return m;
}
static FFMixed P( void* p )
{
	FFMixed m;
	m.PointerValue = p;
	return m;
}

static int ParamIndex( const char* name )
{
	FFUInt32 n = plug( FF_GET_NUM_PARAMETERS, U( 0 ), nullptr ).UIntValue;
	for( FFUInt32 i = 0; i < n; ++i )
	{
		const char* pn = (const char*)plug( FF_GET_PARAMETER_NAME, U( i ), nullptr ).PointerValue;
		if( pn && std::strcmp( pn, name ) == 0 )
			return (int)i;
	}
	std::printf( "  param '%s' not found\n", name );
	++failures;
	return -1;
}

static void SetFloat( const char* name, float v )
{
	SetParameterStruct s;
	s.ParameterNumber = (FFUInt32)ParamIndex( name );
	std::memcpy( &s.NewParameterValue.UIntValue, &v, 4 );
	plug( FF_SET_PARAMETER, P( &s ), inst );
}

static void SetText( const char* name, const char* v )
{
	SetParameterStruct s;
	s.ParameterNumber                = (FFUInt32)ParamIndex( name );
	s.NewParameterValue.PointerValue = (void*)v;
	plug( FF_SET_PARAMETER, P( &s ), inst );
}

static std::string GetText( const char* name )
{
	const char* t = (const char*)plug( FF_GET_PARAMETER, U( (FFUInt32)ParamIndex( name ) ), inst ).PointerValue;
	return t ? t : "";
}

static float GetFloat( const char* name )
{
	FFUInt32 bits = plug( FF_GET_PARAMETER, U( (FFUInt32)ParamIndex( name ) ), inst ).UIntValue;
	float f;
	std::memcpy( &f, &bits, 4 );
	return f;
}

static int DrainEvents()
{
	GetParamEventsStruct q = { 0, nullptr };
	plug( FF_GET_PARAMETER_EVENTS, P( &q ), inst );
	std::vector< ParamEventStruct > ev( q.numEvents );
	q.events = ev.data();
	plug( FF_GET_PARAMETER_EVENTS, P( &q ), inst );
	return (int)q.numEvents;
}

static GLuint fbo, colorTex;
static const int W = 640, H = 360;

static void Frames( int n )
{
	for( int i = 0; i < n; ++i )
	{
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glViewport( 0, 0, W, H );
		glClearColor( 0, 0, 0, 0 );
		glClear( GL_COLOR_BUFFER_BIT );
		ProcessOpenGLStruct pgl = {};
		pgl.HostFBO             = fbo;
		plug( FF_PROCESS_OPENGL, P( &pgl ), inst );
		DrainEvents();
		std::this_thread::sleep_for( std::chrono::milliseconds( 16 ) );
	}
}

static bool CenterHasImage()
{
	unsigned char px[ 4 ] = {};
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glReadPixels( W / 2, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px );
	return px[ 3 ] == 255;
}

static void DumpParams()
{
	FFUInt32 n = plug( FF_GET_NUM_PARAMETERS, U( 0 ), nullptr ).UIntValue;
	for( FFUInt32 i = 0; i < n; ++i )
	{
		const char* pn = (const char*)plug( FF_GET_PARAMETER_NAME, U( i ), nullptr ).PointerValue;
		FFUInt32 type  = plug( FF_GET_PARAMETER_TYPE, U( i ), nullptr ).UIntValue;
		FFUInt32 vis   = plug( FF_GET_PRAMETER_VISIBILITY, U( i ), inst ).UIntValue;
		std::printf( "    [%2u] %-24s type=%3u visible=%u", i, pn, type, vis );
		if( type == FF_TYPE_OPTION )
		{
			FFUInt32 ne = plug( FF_GET_NUM_PARAMETER_ELEMENTS, U( i ), nullptr ).UIntValue;
			std::printf( " elements:" );
			for( FFUInt32 e = 0; e < ne; ++e )
			{
				GetParameterElementNameStruct q = { i, e };
				const char* en                  = (const char*)plug( FF_GET_PARAMETER_ELEMENT_NAME, P( &q ), nullptr ).PointerValue;
				std::printf( " '%s'", en ? en : "?" );
			}
		}
		std::printf( "\n" );
	}
}

static void* Load( const char* path )
{
	void* lib = dlopen( path, RTLD_NOW | RTLD_LOCAL );
	if( !lib )
	{
		std::printf( "dlopen failed: %s\n", dlerror() );
		std::exit( 1 );
	}
	plug                  = (MainFn)dlsym( lib, "plugMain" );
	PluginInfoStruct* inf = (PluginInfoStruct*)plug( FF_GET_INFO, U( 0 ), nullptr ).PointerValue;
	std::printf( "\n== %.16s (id %.4s, type %u)\n", (const char*)inf->PluginName, (const char*)inf->PluginUniqueID, inf->PluginType );
	plug( FF_INITIALISE_V2, U( 0 ), nullptr );
	FFGLViewportStruct vp = { 0, 0, W, H };
	inst                  = plug( FF_INSTANTIATE_GL, P( &vp ), nullptr ).PointerValue;
	CHECK( inst != nullptr, "instantiate" );
	return lib;
}

static void Unload( void* lib )
{
	auto t0 = std::chrono::steady_clock::now();
	plug( FF_DEINSTANTIATE_GL, U( 0 ), inst );
	plug( FF_DEINITIALISE, U( 0 ), nullptr );
	double ms = std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - t0 ).count();
	CHECK( ms < 3000, ( "deinstantiate without hang (" + std::to_string( (int)ms ) + " ms)" ).c_str() );
	dlclose( lib );
}

int main( int argc, char** argv )
{
	if( argc < 3 )
		return 2;
	auto getPlatformDisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress( "eglGetPlatformDisplayEXT" );
	EGLDisplay dpy          = getPlatformDisplay ? getPlatformDisplay( EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr ) : eglGetDisplay( EGL_DEFAULT_DISPLAY );
	eglInitialize( dpy, nullptr, nullptr );
	eglBindAPI( EGL_OPENGL_API );
	EGLint cfgAttr[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE };
	EGLConfig cfg;
	EGLint ncfg = 0;
	eglChooseConfig( dpy, cfgAttr, &cfg, 1, &ncfg );
	EGLint ctxAttr[] = { EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 1, EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE };
	EGLContext ctx   = eglCreateContext( dpy, cfg, EGL_NO_CONTEXT, ctxAttr );
	if( !ctx || !eglMakeCurrent( dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx ) )
	{
		std::printf( "no EGL context\n" );
		return 1;
	}
	glewExperimental = GL_TRUE;
	glewInit();
	std::printf( "GL: %s\n", glGetString( GL_VERSION ) );

	glGenTextures( 1, &colorTex );
	glBindTexture( GL_TEXTURE_2D, colorTex );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glGenFramebuffers( 1, &fbo );
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0 );

	// ---------------- Screen capture
	{
		void* lib = Load( argv[ 1 ] );
		DumpParams();
		Frames( 20 );
		CHECK( CenterHasImage(), "monitor capture draws pixels" );
		std::printf( "  status: %s\n", GetText( "Status" ).c_str() );
		SetFloat( "Source", 1.0f );
		CHECK( plug( FF_GET_PRAMETER_VISIBILITY, U( ParamIndex( "Window" ) ), inst ).UIntValue == 1, "window list visible in Window mode" );
		CHECK( plug( FF_GET_PRAMETER_VISIBILITY, U( ParamIndex( "Monitor" ) ), inst ).UIntValue == 0, "monitor list hidden in Window mode" );
		SetFloat( "Window", 0.0f );
		Frames( 20 );
		CHECK( CenterHasImage(), "window capture draws pixels" );
		CHECK( GetText( "WindowKey" ) == "test|Test Window", "window key remembered" );
		std::printf( "  status: %s\n", GetText( "Status" ).c_str() );
		SetFloat( "Show Cursor", 0.0f );
		SetFloat( "Fit", 1.0f );
		SetFloat( "Crop Top", 0.2f );
		SetFloat( "Refresh List", 1.0f );
		SetFloat( "Refresh List", 0.0f );
		Frames( 10 );
		CHECK( CenterHasImage(), "still drawing after refresh/crop" );
		Unload( lib );
	}

	// ---------------- Presentation
	{
		void* lib = Load( argv[ 2 ] );
		DumpParams();
		Frames( 5 );
		std::printf( "  status: %s\n", GetText( "Status" ).c_str() );
		CHECK( GetText( "Status" ).find( "Choose" ) != std::string::npos, "asks for a file when empty" );

		SetText( "File", "/nonexistent/deck.pptx" );
		Frames( 10 );
		std::printf( "  status: %s\n", GetText( "Status" ).c_str() );
		CHECK( GetText( "Status" ).find( "not found" ) != std::string::npos, "missing file reported" );

		if( argc > 3 )
		{
			SetText( "File", argv[ 3 ] );// an existing .pptx, no PowerPoint/LibreOffice here
			Frames( 10 );
			std::printf( "  status: %s\n", GetText( "Status" ).c_str() );
			CHECK( GetText( "Status" ).find( "LibreOffice" ) != std::string::npos, "PPT goes to LibreOffice (or explains it is needed)" );
			SetFloat( "Mode", 1.0f );
			Frames( 10 );
			std::printf( "  status: %s\n", GetText( "Status" ).c_str() );
			CHECK( GetText( "Status" ).find( "needs Microsoft PowerPoint" ) != std::string::npos, "animated mode requires PowerPoint" );
		}

		SetFloat( "Source", 1.0f );
		SetText( "Google Slides URL", "  https://example.com/not-slides  " );
		Frames( 10 );
		std::printf( "  status: %s\n", GetText( "Status" ).c_str() );
		CHECK( GetText( "Status" ).find( "Not a Google Slides link" ) != std::string::npos, "rejects non Google link" );
		CHECK( GetText( "Google Slides URL" ) == "https://example.com/not-slides", "trims pasted url" );

		SetFloat( "Next", 1.0f );
		SetFloat( "Next", 0.0f );
		SetFloat( "Slide", 3.0f );
		Frames( 3 );
		Unload( lib );
	}

	std::printf( "\n%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures );
	return failures ? 1 : 0;
}
