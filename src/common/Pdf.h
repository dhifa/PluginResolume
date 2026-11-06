#pragma once
// Native PDF rasteriser: Windows.Data.Pdf on Windows, CoreGraphics on macOS.
#include "Frame.h"
#include <memory>
#include <string>

class PdfDocument
{
public:
	virtual ~PdfDocument()      = default;
	virtual int pageCount() const = 0;
	// Renders page (0 based) as large as possible inside maxW x maxH while
	// keeping the aspect ratio. White background. Output is BGRA top-down.
	// Windows: call from an MTA thread.
	virtual bool renderPage( int index, int maxW, int maxH, Frame& out ) = 0;
};

std::unique_ptr< PdfDocument > OpenPdfDocument( const std::string& utf8Path, std::string* error );
