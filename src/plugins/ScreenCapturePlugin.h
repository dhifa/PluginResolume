#pragma once
#include <FFGLSDK.h>
#include "../common/Capture.h"
#include "../common/FrameRenderer.h"
#include "../common/Worker.h"
#include <atomic>
#include <mutex>

class ScreenCapturePlugin : public CFFGLPlugin
{
public:
	ScreenCapturePlugin();
	~ScreenCapturePlugin() override;

	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	char* GetTextParameter( unsigned int index ) override;

private:
	void refreshTargets( bool raiseEvents );
	void applyChanges();
	void updateVisibility( bool raiseEvents );
	void setStatus( const std::string& s );
	int findWindowByKey( const std::string& key ) const;

	// parameter values
	float sourceKind  = 0.0f;// 0 monitor, 1 window
	float monitorIdx  = 0.0f;
	float windowIdx   = 0.0f;
	bool showCursor   = true;
	bool hideTitleBar = true;
	float fitMode     = 0.0f;
	float crop[ 4 ]   = { 0, 0, 0, 0 };
	std::string windowKey;// hidden: remembers the window across sessions
	std::string statusText;

	// change tracking (host thread -> GL thread)
	bool restartNeeded     = true;
	bool windowKeyChanged  = false;
	bool windowIdxChanged  = false;
	bool cursorChanged     = false;
	bool titleBarChanged   = false;

	std::vector< CaptureTarget > monitors;
	std::vector< CaptureTarget > windows;

	std::mutex statusMutex;
	std::string pendingStatus;
	std::atomic< bool > statusDirty{ false };

	FrameSlot slot;
	FrameRenderer renderer;
	std::unique_ptr< ScreenCapture > capture;// worker thread only
	Worker worker{ plat::Apartment::MultiThreaded };
};
