#pragma once
// Remote control of Microsoft PowerPoint.
//  - Windows: COM automation (late bound IDispatch). Call from an STA thread.
//  - macOS  : AppleScript through osascript.
#include "Capture.h"
#include <memory>
#include <string>

struct PowerPointShowOptions
{
	int widthPx      = 1920;// size of the slide show window (= composition size)
	int heightPx     = 1080;
	bool hideWindow  = true;// move the slide show window off screen (Windows)
	bool loop        = false;
};

class PowerPointShow
{
public:
	virtual ~PowerPointShow() = default;

	virtual bool open( const std::string& path, const PowerPointShowOptions& options, std::string* error ) = 0;
	virtual void close()                                                                                = 0;
	virtual bool isOpen()                                                                               = 0;// false if the show was ended in PowerPoint
	virtual bool next()                                                                                 = 0;// next animation step or slide
	virtual bool previous()                                                                             = 0;
	virtual bool gotoSlide( int oneBased )                                                              = 0;
	virtual int currentSlide()                                                                          = 0;// 1 based, 0 if unknown
	virtual int slideCount()                                                                            = 0;
	// The window that has to be captured to see the running show.
	virtual bool captureTarget( CaptureTarget& out )                                                    = 0;
};

std::unique_ptr< PowerPointShow > CreatePowerPointShow();

// Converts any PowerPoint file to PDF with PowerPoint itself (no animation).
bool PowerPointExportPdf( const std::string& input, const std::string& outputPdf, std::string* error );
