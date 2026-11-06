#pragma once
// Platform independent screen/window capture interface.
//  - Windows: Windows.Graphics.Capture (Windows 10 1903+; cursor toggle needs 2004+)
//  - macOS  : ScreenCaptureKit (macOS 12.3+)
#include "Frame.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

enum class TargetKind
{
	Monitor,
	Window
};

struct CaptureTarget
{
	TargetKind kind = TargetKind::Monitor;
	uint64_t id     = 0;// HMONITOR / HWND on Windows, CGDirectDisplayID / CGWindowID on macOS
	std::string name;   // shown in the Resolume dropdown
	std::string owner;  // process (exe) name or bundle id
	std::string title;  // window title (empty for monitors)
	int width  = 0;     // pixels (best effort)
	int height = 0;
	bool primary = false;

	// Used to find the same window again after Resolume restarts / list refresh.
	std::string matchKey() const { return owner + "|" + title; }
};

std::vector< CaptureTarget > EnumerateCaptureTargets( TargetKind kind );

class ScreenCapture
{
public:
	virtual ~ScreenCapture() = default;

	// Starts capturing into sink. sink must outlive this object (or stop()).
	// Windows: must be called from a thread with an MTA (see Worker).
	virtual bool start( const CaptureTarget& target, bool showCursor, FrameSlot* sink ) = 0;
	virtual void stop()                                                               = 0;
	virtual void setShowCursor( bool show )                                           = 0;
	// Windows only: crop the title bar / borders of captured windows.
	virtual void setClientAreaOnly( bool clientOnly )                                 = 0;
	virtual bool isRunning() const                                                    = 0;
	// Called (from any thread) when capture stops by itself, e.g. window closed.
	virtual void setStoppedCallback( std::function< void( const std::string& reason ) > cb ) = 0;
	virtual std::string lastError() const                                             = 0;
};

std::unique_ptr< ScreenCapture > CreateScreenCapture();

// Checks OS support / permissions. Returns false and fills why on failure.
bool IsScreenCaptureAvailable( std::string* why );
