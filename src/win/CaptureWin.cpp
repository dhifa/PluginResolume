// Windows.Graphics.Capture backend (Windows 10 1903+).
#include "WinUtil.h"
#include "../common/Capture.h"
#include "../common/Platform.h"

#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <psapi.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <algorithm>
#include <atomic>
#include <mutex>

namespace wgc  = winrt::Windows::Graphics::Capture;
namespace wdx  = winrt::Windows::Graphics::DirectX;
namespace wd3d = winrt::Windows::Graphics::DirectX::Direct3D11;
using winrt::Windows::Foundation::Metadata::ApiInformation;

// ------------------------------------------------------------ enumeration

static std::string ProcessName( DWORD pid )
{
	std::string name;
	HANDLE h = OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid );
	if( h )
	{
		wchar_t buf[ MAX_PATH ] = {};
		DWORD size              = MAX_PATH;
		if( QueryFullProcessImageNameW( h, 0, buf, &size ) )
		{
			std::wstring full( buf, size );
			size_t slash = full.find_last_of( L'\\' );
			name         = Narrow( slash == std::wstring::npos ? full : full.substr( slash + 1 ) );
		}
		CloseHandle( h );
	}
	return name;
}

static bool IsCapturableWindow( HWND hwnd )
{
	if( !IsWindowVisible( hwnd ) || GetAncestor( hwnd, GA_ROOT ) != hwnd )
		return false;
	LONG ex = GetWindowLongW( hwnd, GWL_EXSTYLE );
	if( ex & WS_EX_TOOLWINDOW )
		return false;
	if( GetWindow( hwnd, GW_OWNER ) != nullptr && !( ex & WS_EX_APPWINDOW ) )
		return false;
	DWORD cloaked = 0;
	if( SUCCEEDED( DwmGetWindowAttribute( hwnd, DWMWA_CLOAKED, &cloaked, sizeof( cloaked ) ) ) && cloaked )
		return false;
	if( GetWindowTextLengthW( hwnd ) == 0 )
		return false;
	DWORD pid = 0;
	GetWindowThreadProcessId( hwnd, &pid );
	if( pid == GetCurrentProcessId() )
		return false;// don't offer Resolume itself (feedback loop)
	RECT r;
	GetWindowRect( hwnd, &r );
	return ( r.right - r.left ) > 40 && ( r.bottom - r.top ) > 40;
}

static BOOL CALLBACK EnumWindowProc( HWND hwnd, LPARAM lp )
{
	auto* out = reinterpret_cast< std::vector< CaptureTarget >* >( lp );
	if( !IsCapturableWindow( hwnd ) )
		return TRUE;

	int len = GetWindowTextLengthW( hwnd );
	std::wstring title( len + 1, L'\0' );
	GetWindowTextW( hwnd, &title[ 0 ], len + 1 );
	title.resize( wcslen( title.c_str() ) );

	DWORD pid = 0;
	GetWindowThreadProcessId( hwnd, &pid );

	CaptureTarget t;
	t.kind  = TargetKind::Window;
	t.id    = (uint64_t)(uintptr_t)hwnd;
	t.title = Narrow( title );
	t.owner = ProcessName( pid );
	RECT r;
	GetWindowRect( hwnd, &r );
	t.width  = r.right - r.left;
	t.height = r.bottom - r.top;

	std::string shown = t.title.size() > 60 ? t.title.substr( 0, 57 ) + "..." : t.title;
	t.name            = shown + "  [" + t.owner + "]" + ( IsIconic( hwnd ) ? " (minimized)" : "" );
	out->push_back( t );
	return TRUE;
}

static BOOL CALLBACK EnumMonitorProc( HMONITOR mon, HDC, LPRECT, LPARAM lp )
{
	auto* out        = reinterpret_cast< std::vector< CaptureTarget >* >( lp );
	MONITORINFOEXW mi = {};
	mi.cbSize        = sizeof( mi );
	if( !GetMonitorInfoW( mon, &mi ) )
		return TRUE;

	DEVMODEW dm = {};
	dm.dmSize   = sizeof( dm );
	int w = mi.rcMonitor.right - mi.rcMonitor.left, h = mi.rcMonitor.bottom - mi.rcMonitor.top;
	if( EnumDisplaySettingsW( mi.szDevice, ENUM_CURRENT_SETTINGS, &dm ) )
	{
		w = (int)dm.dmPelsWidth;
		h = (int)dm.dmPelsHeight;
	}

	CaptureTarget t;
	t.kind    = TargetKind::Monitor;
	t.id      = (uint64_t)(uintptr_t)mon;
	t.owner   = Narrow( mi.szDevice );
	t.width   = w;
	t.height  = h;
	t.primary = ( mi.dwFlags & MONITORINFOF_PRIMARY ) != 0;
	out->push_back( t );
	return TRUE;
}

std::vector< CaptureTarget > EnumerateCaptureTargets( TargetKind kind )
{
	std::vector< CaptureTarget > out;
	if( kind == TargetKind::Window )
	{
		EnumWindows( EnumWindowProc, reinterpret_cast< LPARAM >( &out ) );
	}
	else
	{
		EnumDisplayMonitors( nullptr, nullptr, EnumMonitorProc, reinterpret_cast< LPARAM >( &out ) );
		for( size_t i = 0; i < out.size(); ++i )
		{
			out[ i ].name = "Monitor " + std::to_string( i + 1 ) + " (" + std::to_string( out[ i ].width ) + "x" +
			                std::to_string( out[ i ].height ) + ")" + ( out[ i ].primary ? " Primary" : "" );
		}
	}
	return out;
}

bool IsScreenCaptureAvailable( std::string* why )
{
	try
	{
		if( wgc::GraphicsCaptureSession::IsSupported() )
			return true;
	}
	catch( ... )
	{
	}
	if( why )
		*why = "Windows Graphics Capture needs Windows 10 version 1903 or newer";
	return false;
}

// ------------------------------------------------------------ capture

class WinScreenCapture : public ScreenCapture
{
public:
	~WinScreenCapture() override { stop(); }

	bool start( const CaptureTarget& target, bool showCursor, FrameSlot* sinkSlot ) override
	{
		stop();
		error.clear();
		try
		{
			if( !ensureDevice() )
				return false;

			auto interop = winrt::get_activation_factory< wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop >();
			wgc::GraphicsCaptureItem newItem{ nullptr };
			if( target.kind == TargetKind::Window )
			{
				hwnd = (HWND)(uintptr_t)target.id;
				if( !IsWindow( hwnd ) )
				{
					error = "Window no longer exists. Press Refresh List";
					return false;
				}
				if( IsIconic( hwnd ) )
				{
					error = "Window is minimized. Restore it first";
					return false;
				}
				winrt::check_hresult( interop->CreateForWindow( hwnd, winrt::guid_of< wgc::IGraphicsCaptureItem >(), winrt::put_abi( newItem ) ) );
			}
			else
			{
				hwnd = nullptr;
				winrt::check_hresult( interop->CreateForMonitor( (HMONITOR)(uintptr_t)target.id, winrt::guid_of< wgc::IGraphicsCaptureItem >(), winrt::put_abi( newItem ) ) );
			}

			{
				std::lock_guard< std::mutex > lock( mutex );
				sink     = sinkSlot;
				item     = newItem;
				lastSize = item.Size();
				pool     = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded( device, wdx::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, lastSize );
				session  = pool.CreateCaptureSession( item );
			}

			frameRevoker  = pool.FrameArrived( winrt::auto_revoke, { this, &WinScreenCapture::onFrame } );
			closedRevoker = item.Closed( winrt::auto_revoke, [ this ]( auto&&, auto&& ) {
				running = false;
				if( stoppedCallback )
					stoppedCallback( "Captured window was closed" );
			} );

			applyCursor( showCursor );
			disableBorder();
			session.StartCapture();
			running = true;
			return true;
		}
		catch( const winrt::hresult_error& e )
		{
			error = "Capture failed: " + winrt::to_string( e.message() );
		}
		catch( ... )
		{
			error = "Capture failed";
		}
		stop();
		return false;
	}

	void stop() override
	{
		frameRevoker.revoke();
		closedRevoker.revoke();
		std::lock_guard< std::mutex > lock( mutex );
		try
		{
			if( session )
				session.Close();
			if( pool )
				pool.Close();
		}
		catch( ... )
		{
		}
		session = nullptr;
		pool    = nullptr;
		item    = nullptr;
		staging = nullptr;
		sink    = nullptr;
		hwnd    = nullptr;
		running = false;
	}

	void setShowCursor( bool show ) override
	{
		std::lock_guard< std::mutex > lock( mutex );
		applyCursor( show );
	}

	void setClientAreaOnly( bool clientOnly ) override { clientAreaOnly = clientOnly; }
	bool isRunning() const override { return running; }
	std::string lastError() const override { return error; }
	void setStoppedCallback( std::function< void( const std::string& ) > cb ) override { stoppedCallback = std::move( cb ); }

private:
	bool ensureDevice()
	{
		if( d3dDevice )
			return true;
		UINT flags         = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
		D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
		HRESULT hr = D3D11CreateDevice( nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, ARRAYSIZE( levels ), D3D11_SDK_VERSION,
		                                d3dDevice.put(), nullptr, d3dContext.put() );
		if( FAILED( hr ) )
			hr = D3D11CreateDevice( nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, ARRAYSIZE( levels ), D3D11_SDK_VERSION,
			                        d3dDevice.put(), nullptr, d3dContext.put() );
		if( FAILED( hr ) )
		{
			error = "Could not create a Direct3D 11 device";
			return false;
		}
		auto dxgiDevice = d3dDevice.as< IDXGIDevice >();
		winrt::com_ptr<::IInspectable > inspectable;
		winrt::check_hresult( CreateDirect3D11DeviceFromDXGIDevice( dxgiDevice.get(), inspectable.put() ) );
		device = inspectable.as< wd3d::IDirect3DDevice >();
		return true;
	}

	void applyCursor( bool show )
	{
		if( !session )
			return;
		try
		{
			static const bool hasCursorApi = ApiInformation::IsPropertyPresent( L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsCursorCaptureEnabled" );
			if( hasCursorApi )
				session.IsCursorCaptureEnabled( show );
		}
		catch( ... )
		{
		}
	}

	void disableBorder()
	{
		// Windows 11: remove the yellow capture border.
		try
		{
			static const bool hasBorderApi = ApiInformation::IsPropertyPresent( L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired" );
			if( !hasBorderApi )
				return;
			wgc::GraphicsCaptureAccess::RequestAccessAsync( wgc::GraphicsCaptureAccessKind::Borderless ).get();
			session.IsBorderRequired( false );
		}
		catch( ... )
		{
		}
	}

	// Part of the captured surface that is the window's client area.
	bool clientBox( UINT texW, UINT texH, D3D11_BOX& box )
	{
		RECT frame, client;
		if( !hwnd || FAILED( DwmGetWindowAttribute( hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof( frame ) ) ) )
			return false;
		if( !GetClientRect( hwnd, &client ) )
			return false;
		POINT tl = { client.left, client.top };
		POINT br = { client.right, client.bottom };
		ClientToScreen( hwnd, &tl );
		ClientToScreen( hwnd, &br );
		int frameW = frame.right - frame.left, frameH = frame.bottom - frame.top;
		if( frameW <= 0 || frameH <= 0 )
			return false;
		// The surface is in physical pixels; scale in case the window is DPI virtualised.
		double sx = double( texW ) / frameW, sy = double( texH ) / frameH;
		LONG l = LONG( ( tl.x - frame.left ) * sx ), t = LONG( ( tl.y - frame.top ) * sy );
		LONG r = LONG( ( br.x - frame.left ) * sx ), b = LONG( ( br.y - frame.top ) * sy );
		l = std::max< LONG >( 0, l );
		t = std::max< LONG >( 0, t );
		r = std::min< LONG >( (LONG)texW, r );
		b = std::min< LONG >( (LONG)texH, b );
		if( r - l < 8 || b - t < 8 )
			return false;
		box = { (UINT)l, (UINT)t, 0, (UINT)r, (UINT)b, 1 };
		return true;
	}

	void onFrame( const wgc::Direct3D11CaptureFramePool& sender, const winrt::Windows::Foundation::IInspectable& )
	{
		std::lock_guard< std::mutex > lock( mutex );
		if( !pool || !sink )
			return;
		auto frame = sender.TryGetNextFrame();
		if( !frame )
			return;

		auto contentSize = frame.ContentSize();
		auto access      = frame.Surface().as< ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess >();
		winrt::com_ptr< ID3D11Texture2D > texture;
		if( FAILED( access->GetInterface( winrt::guid_of< ID3D11Texture2D >(), texture.put_void() ) ) )
			return;

		D3D11_TEXTURE2D_DESC desc;
		texture->GetDesc( &desc );
		UINT w = std::min< UINT >( desc.Width, (UINT)std::max( 0, contentSize.Width ) );
		UINT h = std::min< UINT >( desc.Height, (UINT)std::max( 0, contentSize.Height ) );

		D3D11_BOX box = { 0, 0, 0, w, h, 1 };
		if( hwnd && clientAreaOnly )
			clientBox( w, h, box );
		UINT outW = box.right - box.left, outH = box.bottom - box.top;

		if( outW > 0 && outH > 0 )
		{
			if( !staging || stagingW != outW || stagingH != outH )
			{
				D3D11_TEXTURE2D_DESC sd = {};
				sd.Width                = outW;
				sd.Height               = outH;
				sd.MipLevels            = 1;
				sd.ArraySize            = 1;
				sd.Format               = DXGI_FORMAT_B8G8R8A8_UNORM;
				sd.SampleDesc.Count     = 1;
				sd.Usage                = D3D11_USAGE_STAGING;
				sd.CPUAccessFlags       = D3D11_CPU_ACCESS_READ;
				staging                 = nullptr;
				if( FAILED( d3dDevice->CreateTexture2D( &sd, nullptr, staging.put() ) ) )
					return;
				stagingW = outW;
				stagingH = outH;
			}
			d3dContext->CopySubresourceRegion( staging.get(), 0, 0, 0, 0, texture.get(), 0, &box );
			D3D11_MAPPED_SUBRESOURCE mapped;
			if( SUCCEEDED( d3dContext->Map( staging.get(), 0, D3D11_MAP_READ, 0, &mapped ) ) )
			{
				sink->publish( static_cast< const uint8_t* >( mapped.pData ), (int)outW, (int)outH, (int)mapped.RowPitch );
				d3dContext->Unmap( staging.get(), 0 );
			}
		}

		// Window was resized: recreate the pool so new frames use the new size.
		if( contentSize.Width != lastSize.Width || contentSize.Height != lastSize.Height )
		{
			lastSize = contentSize;
			pool.Recreate( device, wdx::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, lastSize );
		}
	}

	winrt::com_ptr< ID3D11Device > d3dDevice;
	winrt::com_ptr< ID3D11DeviceContext > d3dContext;
	wd3d::IDirect3DDevice device{ nullptr };
	wgc::GraphicsCaptureItem item{ nullptr };
	wgc::Direct3D11CaptureFramePool pool{ nullptr };
	wgc::GraphicsCaptureSession session{ nullptr };
	wgc::Direct3D11CaptureFramePool::FrameArrived_revoker frameRevoker;
	wgc::GraphicsCaptureItem::Closed_revoker closedRevoker;
	winrt::Windows::Graphics::SizeInt32 lastSize{ 0, 0 };
	winrt::com_ptr< ID3D11Texture2D > staging;
	UINT stagingW = 0, stagingH = 0;

	std::mutex mutex;
	FrameSlot* sink = nullptr;
	HWND hwnd       = nullptr;
	std::atomic< bool > clientAreaOnly{ true };
	std::atomic< bool > running{ false };
	std::string error;
	std::function< void( const std::string& ) > stoppedCallback;
};

std::unique_ptr< ScreenCapture > CreateScreenCapture()
{
	return std::make_unique< WinScreenCapture >();
}
