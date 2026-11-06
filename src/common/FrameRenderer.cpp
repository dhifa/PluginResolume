#include "FrameRenderer.h"
#include <algorithm>

using namespace ffglex;

static const char vertexShaderCode[] = R"(#version 410 core
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;
out vec2 uv;
void main()
{
	gl_Position = vPosition;
	uv = vUV;
}
)";

// uv is bottom-left based (FFGL), the image is stored top-down.
static const char fragmentShaderCode[] = R"(#version 410 core
uniform sampler2D image;
uniform vec2 scale;    // size of the image on screen relative to the output
uniform vec4 cropRect; // x0, y0, x1, y1 in top-down image coordinates
in vec2 uv;
out vec4 fragColor;
void main()
{
	vec2 p = ( uv - 0.5 ) / scale + 0.5;
	if( p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 )
	{
		fragColor = vec4( 0.0 );
		return;
	}
	vec2 t = vec2( mix( cropRect.x, cropRect.z, p.x ), mix( cropRect.y, cropRect.w, 1.0 - p.y ) );
	fragColor = vec4( texture( image, t ).rgb, 1.0 );
}
)";

bool FrameRenderer::init()
{
	if( !shader.Compile( vertexShaderCode, fragmentShaderCode ) )
		return false;
	if( !quad.Initialise() )
	{
		shader.FreeGLResources();
		return false;
	}
	return true;
}

void FrameRenderer::release()
{
	if( texture )
		glDeleteTextures( 1, &texture );
	texture  = 0;
	texWidth = texHeight = 0;
	shader.FreeGLResources();
	quad.Release();
}

void FrameRenderer::update( FrameSlot& slot )
{
	if( !slot.fetch( scratch ) )
		return;

	if( scratch.empty() )
	{
		texWidth = texHeight = 0;
		return;
	}

	if( !texture )
		glGenTextures( 1, &texture );

	ScopedTextureBinding binding( GL_TEXTURE_2D, texture );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
	glPixelStorei( GL_UNPACK_ROW_LENGTH, scratch.stride / 4 );
	if( scratch.width != texWidth || scratch.height != texHeight )
	{
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, scratch.width, scratch.height, 0, GL_BGRA, GL_UNSIGNED_BYTE, scratch.pixels.data() );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		texWidth  = scratch.width;
		texHeight = scratch.height;
	}
	else
	{
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, scratch.width, scratch.height, GL_BGRA, GL_UNSIGNED_BYTE, scratch.pixels.data() );
	}
	glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
}

void FrameRenderer::draw( int viewportW, int viewportH, FitMode fit, const Crop& crop )
{
	if( !hasImage() || viewportW <= 0 || viewportH <= 0 || !shader.IsReady() )
		return;

	float cl = std::clamp( crop.left, 0.0f, 0.49f );
	float cr = std::clamp( crop.right, 0.0f, 0.49f );
	float ct = std::clamp( crop.top, 0.0f, 0.49f );
	float cb = std::clamp( crop.bottom, 0.0f, 0.49f );

	float imgW      = float( texWidth ) * ( 1.0f - cl - cr );
	float imgH      = float( texHeight ) * ( 1.0f - ct - cb );
	float imgAspect = imgW / std::max( imgH, 1.0f );
	float outAspect = float( viewportW ) / float( viewportH );

	float sx = 1.0f, sy = 1.0f;
	if( fit == FitMode::Fit )
	{
		if( imgAspect > outAspect )
			sy = outAspect / imgAspect;
		else
			sx = imgAspect / outAspect;
	}
	else if( fit == FitMode::Fill )
	{
		if( imgAspect > outAspect )
			sx = imgAspect / outAspect;
		else
			sy = outAspect / imgAspect;
	}

	ScopedShaderBinding shaderBinding( shader.GetGLID() );
	ScopedSamplerActivation activateSampler( 0 );
	ScopedTextureBinding textureBinding( GL_TEXTURE_2D, texture );
	shader.Set( "image", 0 );
	shader.Set( "scale", sx, sy );
	shader.Set( "cropRect", cl, ct, 1.0f - cr, 1.0f - cb );
	quad.Draw();
}
