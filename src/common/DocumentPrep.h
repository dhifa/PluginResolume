#pragma once
// Turns "something the user picked" (PDF, PPT/PPTX, Google Slides link) into
// a PDF file on disk that the static renderer can rasterise.
#include <functional>
#include <string>

enum class ConverterChoice
{
	Auto,
	PowerPoint,
	LibreOffice
};

using StatusFn    = std::function< void( const std::string& ) >;
using PptExportFn = std::function< bool( const std::string& in, const std::string& out, std::string* error ) >;

bool IsPdfExtension( const std::string& ext );
bool IsPowerPointExtension( const std::string& ext );

// Returns the PDF export URL for a Google Slides link, empty if it isn't one.
std::string GoogleSlidesPdfUrl( const std::string& link, std::string* error );

struct PrepareRequest
{
	std::string input;   // file path or URL
	bool isUrl = false;
	ConverterChoice converter = ConverterChoice::Auto;
	bool forceRefresh = false;  // ignore the cache (Reload button)
	PptExportFn exportWithPowerPoint;// runs PowerPoint on the right thread
	std::function< bool() > cancelled;// true when a newer load request replaced this one
};

bool PrepareStaticPdf( const PrepareRequest& request, std::string& pdfPathOut, std::string& error, const StatusFn& status );
