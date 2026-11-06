#pragma once
#include <FFGLSDK.h>
#include "../common/FrameRenderer.h"
#include "PresentationEngine.h"
#include <chrono>

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
	void scheduleLoad( bool forceRefresh );
	void updateVisibility( bool raiseEvents );
	bool animatedActive() const { return engine->active() == PresentationEngine::Active::Animated; }

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

	bool loadPending  = true;
	bool forceReload  = false;
	int lastViewportW = 0, lastViewportH = 0;
	int reportedSlide = 0;
	std::chrono::steady_clock::time_point lastPoll;

	FrameRenderer renderer;
	std::shared_ptr< PresentationEngine > engine = PresentationEngine::create();
};
