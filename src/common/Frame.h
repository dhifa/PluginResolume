#pragma once
// Thread-safe "latest frame" mailbox shared by capture backends, the PDF
// renderer and the GL thread. Pixels are always BGRA, 8 bit, top-down rows.
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

struct Frame
{
	int width  = 0;
	int height = 0;
	int stride = 0;// bytes per row
	std::vector< uint8_t > pixels;
	uint64_t serial = 0;

	bool empty() const { return width <= 0 || height <= 0 || pixels.empty(); }
};

class FrameSlot
{
public:
	// Copies the image into the slot. srcStride is in bytes. Any thread.
	void publish( const uint8_t* src, int w, int h, int srcStride )
	{
		if( !src || w <= 0 || h <= 0 )
			return;
		std::lock_guard< std::mutex > lock( mutex );
		const int dstStride = w * 4;
		back.width          = w;
		back.height         = h;
		back.stride         = dstStride;
		back.pixels.resize( size_t( dstStride ) * size_t( h ) );
		if( srcStride == dstStride )
		{
			std::memcpy( back.pixels.data(), src, back.pixels.size() );
		}
		else
		{
			for( int y = 0; y < h; ++y )
				std::memcpy( back.pixels.data() + size_t( y ) * dstStride, src + size_t( y ) * srcStride, dstStride );
		}
		back.serial = ++counter;
		hasNew      = true;
	}

	void publish( const Frame& f ) { publish( f.pixels.data(), f.width, f.height, f.stride ); }

	// Hands out the newest frame if it is newer than what the caller has.
	// Swaps buffers so no copy happens on the GL thread.
	bool fetch( Frame& out )
	{
		std::lock_guard< std::mutex > lock( mutex );
		if( !hasNew )
			return false;
		std::swap( out, back );
		hasNew = false;
		return true;
	}

	// Makes the GL side drop its image (e.g. when a source is unloaded).
	void clear()
	{
		std::lock_guard< std::mutex > lock( mutex );
		back        = Frame();
		back.serial = ++counter;
		hasNew      = true;
	}

private:
	std::mutex mutex;
	Frame back;
	uint64_t counter = 0;
	bool hasNew      = false;
};
