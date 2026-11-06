// PDF rendering with CoreGraphics.
#include "../common/Pdf.h"
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#include <algorithm>
#include <cmath>

class MacPdfDocument : public PdfDocument
{
public:
	explicit MacPdfDocument( CGPDFDocumentRef d ) :
		doc( d )
	{
	}
	~MacPdfDocument() override { CGPDFDocumentRelease( doc ); }

	int pageCount() const override { return (int)CGPDFDocumentGetNumberOfPages( doc ); }

	bool renderPage( int index, int maxW, int maxH, Frame& out ) override
	{
		CGPDFPageRef page = CGPDFDocumentGetPage( doc, (size_t)index + 1 );
		if( !page )
			return false;
		CGRect box = CGPDFPageGetBoxRect( page, kCGPDFCropBox );
		int rot    = ( ( CGPDFPageGetRotationAngle( page ) % 360 ) + 360 ) % 360;
		double pw  = box.size.width, ph = box.size.height;
		if( rot == 90 || rot == 270 )
			std::swap( pw, ph );
		if( pw <= 0 || ph <= 0 )
			return false;

		double scale = std::min( maxW / pw, maxH / ph );
		int w        = std::max( 1, (int)std::floor( pw * scale ) );
		int h        = std::max( 1, (int)std::floor( ph * scale ) );

		out.width  = w;
		out.height = h;
		out.stride = w * 4;
		out.pixels.assign( size_t( out.stride ) * h, 0 );

		CGColorSpaceRef cs = CGColorSpaceCreateWithName( kCGColorSpaceSRGB );
		CGContextRef ctx   = CGBitmapContextCreate( out.pixels.data(), w, h, 8, out.stride, cs,
                                                  kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little );// = BGRA in memory
		CGColorSpaceRelease( cs );
		if( !ctx )
			return false;

		CGContextSetRGBFillColor( ctx, 1, 1, 1, 1 );
		CGContextFillRect( ctx, CGRectMake( 0, 0, w, h ) );
		CGContextSetInterpolationQuality( ctx, kCGInterpolationHigh );
		CGContextSetShouldAntialias( ctx, true );
		CGContextSetRenderingIntent( ctx, kCGRenderingIntentDefault );

		// GetDrawingTransform never scales up, so scale first and let it handle rotation/offset at 1:1.
		CGContextScaleCTM( ctx, w / pw, h / ph );
		CGContextConcatCTM( ctx, CGPDFPageGetDrawingTransform( page, kCGPDFCropBox, CGRectMake( 0, 0, pw, ph ), 0, true ) );
		CGContextDrawPDFPage( ctx, page );
		CGContextRelease( ctx );
		return true;
	}

private:
	CGPDFDocumentRef doc;
};

std::unique_ptr< PdfDocument > OpenPdfDocument( const std::string& utf8Path, std::string* error )
{
	@autoreleasepool
	{
		NSURL* url           = [NSURL fileURLWithPath:[NSString stringWithUTF8String:utf8Path.c_str()]];
		CGPDFDocumentRef doc = CGPDFDocumentCreateWithURL( (__bridge CFURLRef)url );
		if( !doc )
		{
			if( error )
				*error = "not a valid PDF";
			return nullptr;
		}
		if( CGPDFDocumentIsEncrypted( doc ) && !CGPDFDocumentUnlockWithPassword( doc, "" ) )
		{
			CGPDFDocumentRelease( doc );
			if( error )
				*error = "PDF is password protected";
			return nullptr;
		}
		return std::make_unique< MacPdfDocument >( doc );
	}
}
