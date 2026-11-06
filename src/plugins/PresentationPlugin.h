#pragma once
#include <FFGLSDK.h>
#include "../common/Capture.h"
#include "../common/DocumentPrep.h"
#include "../common/FrameRenderer.h"
#include "../common/Pdf.h"
#include "../common/PowerPoint.h"
#include "../common/Worker.h"
#include <atomic>
#include <chrono>
#include <list>
#include <mutex>

class PresentationPlugin : public CFFGLPlugin
{
public:
	PresentationPlugin();
	~PresentationPlugin() override;

	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	char* GetTextParameter( unsigned int index ) override;

private:
	enum class Mode
	{
		Auto,
		Animated,
		Static
	};
	enum class Active
	{
		None,
		Animated,
		Static
	};

	struct LoadRequest
	{
		uint64_t generation = 0;
		std::string input;
		bool isUrl              = false;
		Mode mode               = Mode::Auto;
		ConverterChoice convert = ConverterChoice::Auto;
		bool forceRefresh       = false;
		bool loop               = false;
		bool hideWindow         = true;
		int width               = 1920;
		int height              = 1080;
	};

	// GL / host thread
	void scheduleLoad( bool forceRefresh );
	void navigate( int delta );
	void gotoSlide( int oneBased );
	void updateVisibility( bool raiseEvents );

	// main worker
	void doLoad( const LoadRequest& req );
	void unloadAll();
	void renderRequestedPage();
	bool renderPageCached( int page, int w, int h, Frame*& out );
	void setStatus( const std::string& s );
	bool cancelled( uint64_t generation ) const { return generation != loadGeneration.load(); }

	// parameter values
	float sourceKind = 0.0f;// 0 file, 1 Google Slides
	std::string filePath;
	std::string slidesUrl;
	float modeValue      = 0.0f;
	float converterValue = 0.0f;
	float slideValue     = 1.0f;
	bool loop            = false;
	bool hideWindow      = true;
	float fitMode        = 0.0f;
	float crop[ 4 ]      = { 0, 0, 0, 0 };
	std::string statusText;

	bool loadPending = true;
	bool forceReload = false;
	int lastViewportW = 0, lastViewportH = 0;
	std::chrono::steady_clock::time_point lastPoll;

	// shared state
	std::atomic< uint64_t > loadGeneration{ 0 };
	std::atomic< int > activeMode{ (int)Active::None };
	std::atomic< int > slideCount{ 0 };
	std::atomic< int > currentSlide{ 0 };// 1 based, published by workers
	std::atomic< int > requestedPage{ 0 };// static mode, 0 based
	int reportedSlide = 0;                 // GL thread: last value raised to the host

	std::mutex statusMutex;
	std::string pendingStatus;
	std::atomic< bool > statusDirty{ false };

	FrameSlot slot;
	FrameRenderer renderer;

	// main worker only
	std::unique_ptr< ScreenCapture > capture;
	std::unique_ptr< PdfDocument > pdf;
	struct CachedPage
	{
		int page;
		Frame frame;
	};
	std::list< CachedPage > pageCache;// most recent first
	int renderW = 0, renderH = 0;
	int shownPage = -1;

	// PowerPoint worker only (COM needs a single threaded apartment)
	std::unique_ptr< PowerPointShow > show;

	Worker pptWorker{ plat::Apartment::SingleThreaded };
	Worker mainWorker{ plat::Apartment::MultiThreaded };
};
