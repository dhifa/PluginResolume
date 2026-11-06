#pragma once
// Uploads BGRA frames to a GL texture and draws them into the host FBO
// with fit / crop options. GL thread only.
#include <FFGLSDK.h>
#include "Frame.h"

enum class FitMode
{
	Fit,    // letterbox, whole image visible
	Fill,   // crop to fill the output
	Stretch // ignore aspect ratio
};

struct Crop
{
	float left = 0, right = 0, top = 0, bottom = 0;// fractions 0..0.5
};

class FrameRenderer
{
public:
	bool init();
	void release();

	// Takes the newest frame from slot (if any) and uploads it.
	void update( FrameSlot& slot );
	bool hasImage() const { return texture != 0 && texWidth > 0; }
	int imageWidth() const { return texWidth; }
	int imageHeight() const { return texHeight; }

	void draw( int viewportW, int viewportH, FitMode fit, const Crop& crop );

private:
	ffglex::FFGLShader shader;
	ffglex::FFGLScreenQuad quad;
	GLuint texture = 0;
	int texWidth   = 0;
	int texHeight  = 0;
	Frame scratch;
};
