#pragma once
// Background side of the Presentation plugin: loading, PowerPoint control,
// PDF rendering and capture. Owned through shared_ptr and referenced by its own
// worker tasks, so the plugin can be destroyed even if PowerPoint hangs.
#include "../common/Capture.h"
#include "../common/DocumentPrep.h"
#include "../common/Pdf.h"
#include "../common/PowerPoint.h"
#include "../common/Worker.h"
#include <atomic>
#include <list>
#include <memory>
#include <mutex>

class PresentationEngine : public std::enable_shared_from_this< PresentationEngine >
{
public:
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

	static std::shared_ptr< PresentationEngine > create();

	// --- called from the host / GL thread (never block)
	void load( LoadRequest req );// req.generation is assigned here
	void navigate( int delta, bool loop );
	void gotoSlide( int oneBased );
	void viewportChanged( int w, int h );
	void pollCurrentSlide();
	void shutdown();// bounded wait, safe to call from the plugin destructor

	Active active() const { return (Active)activeMode.load(); }
	int currentSlide() const { return currentSlideNo.load(); }
	bool takeStatus( std::string& out );

	FrameSlot slot;// read by the GL thread

private:
	PresentationEngine() = default;

	void doLoad( const LoadRequest& req );
	void unloadAll();
	void renderRequestedPage();
	bool renderPageCached( int page, int w, int h, Frame*& out );
	void setStatus( const std::string& s );
	bool cancelled( uint64_t generation ) const { return generation != loadGeneration.load(); }

	std::shared_ptr< Worker > pptWorker  = Worker::create( plat::Apartment::SingleThreaded );// COM needs an STA
	std::shared_ptr< Worker > mainWorker = Worker::create( plat::Apartment::MultiThreaded );

	std::atomic< uint64_t > loadGeneration{ 0 };
	std::atomic< int > activeMode{ (int)Active::None };
	std::atomic< int > slideCount{ 0 };
	std::atomic< int > currentSlideNo{ 0 };// 1 based
	std::atomic< int > requestedPage{ 0 }; // static mode, 0 based

	std::mutex statusMutex;
	std::string pendingStatus;
	bool statusDirty = false;

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

	// PowerPoint worker only
	std::unique_ptr< PowerPointShow > show;
};
