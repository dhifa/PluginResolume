// PDF rendering with the built-in Windows.Data.Pdf API (Windows 10+).
#include "WinUtil.h"
#include "../common/Pdf.h"

#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.UI.h>

#include <algorithm>
#include <cmath>

namespace wpdf = winrt::Windows::Data::Pdf;
namespace wimg = winrt::Windows::Graphics::Imaging;
namespace wss  = winrt::Windows::Storage::Streams;

class WinPdfDocument : public PdfDocument
{
public:
	explicit WinPdfDocument( wpdf::PdfDocument d ) :
		doc( std::move( d ) )
	{
	}

	int pageCount() const override { return (int)doc.PageCount(); }

	bool renderPage( int index, int maxW, int maxH, Frame& out ) override
	{
		try
		{
			if( index < 0 || index >= pageCount() )
				return false;
			auto page = doc.GetPage( (uint32_t)index );
			auto size = page.Size();// DIPs, rotation already applied
			if( size.Width <= 0 || size.Height <= 0 )
				return false;

			double scale = std::min( maxW / (double)size.Width, maxH / (double)size.Height );
			uint32_t w   = (uint32_t)std::max( 1.0, std::floor( size.Width * scale ) );
			uint32_t h   = (uint32_t)std::max( 1.0, std::floor( size.Height * scale ) );

			wpdf::PdfPageRenderOptions options;
			options.DestinationWidth( w );
			options.DestinationHeight( h );
			options.BackgroundColor( winrt::Windows::UI::Color{ 255, 255, 255, 255 } );

			wss::InMemoryRandomAccessStream stream;
			page.RenderToStreamAsync( stream, options ).get();
			stream.Seek( 0 );

			auto decoder = wimg::BitmapDecoder::CreateAsync( stream ).get();
			auto pixels  = decoder.GetPixelDataAsync( wimg::BitmapPixelFormat::Bgra8, wimg::BitmapAlphaMode::Premultiplied, wimg::BitmapTransform(),
                                                     wimg::ExifOrientationMode::IgnoreExifOrientation, wimg::ColorManagementMode::DoNotColorManage )
			                  .get();
			auto data = pixels.DetachPixelData();

			out.width  = (int)decoder.PixelWidth();
			out.height = (int)decoder.PixelHeight();
			out.stride = out.width * 4;
			if( data.size() < size_t( out.stride ) * out.height )
				return false;
			out.pixels.assign( data.begin(), data.begin() + size_t( out.stride ) * out.height );
			return true;
		}
		catch( ... )
		{
			return false;
		}
	}

private:
	wpdf::PdfDocument doc;
};

std::unique_ptr< PdfDocument > OpenPdfDocument( const std::string& utf8Path, std::string* error )
{
	try
	{
		auto file = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync( winrt::hstring( WinPath( utf8Path ) ) ).get();
		auto doc  = wpdf::PdfDocument::LoadFromFileAsync( file ).get();
		return std::make_unique< WinPdfDocument >( doc );
	}
	catch( const winrt::hresult_error& e )
	{
		if( error )
			*error = winrt::to_string( e.message() );
	}
	catch( ... )
	{
		if( error )
			*error = "unknown error";
	}
	return nullptr;
}
