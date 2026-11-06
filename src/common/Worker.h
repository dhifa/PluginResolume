#pragma once
// A single background thread with a task queue.
#include "Platform.h"
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>

class Worker
{
public:
	explicit Worker( plat::Apartment apartment ) :
		apartment( apartment )
	{
		thread = std::thread( [ this ] { run(); } );
	}
	~Worker() { stop(); }

	Worker( const Worker& )            = delete;
	Worker& operator=( const Worker& ) = delete;

	void post( std::function< void() > fn )
	{
		{
			std::lock_guard< std::mutex > lock( mutex );
			if( stopping )
				return;
			queue.push_back( std::move( fn ) );
		}
		cv.notify_one();
	}

	// Runs fn on the worker and waits for the result.
	template< class F >
	auto call( F fn ) -> decltype( fn() )
	{
		using R   = decltype( fn() );
		auto task = std::make_shared< std::packaged_task< R() > >( std::move( fn ) );
		auto fut  = task->get_future();
		if( std::this_thread::get_id() == thread.get_id() )
			( *task )();
		else
			post( [ task ] { ( *task )(); } );
		return fut.get();
	}

	// Drops tasks that have not started yet (e.g. superseded slide changes).
	void clearPending()
	{
		std::lock_guard< std::mutex > lock( mutex );
		queue.clear();
	}

	size_t pending()
	{
		std::lock_guard< std::mutex > lock( mutex );
		return queue.size();
	}

	// Finishes the queued tasks, then joins.
	void stop()
	{
		{
			std::lock_guard< std::mutex > lock( mutex );
			if( stopping && !thread.joinable() )
				return;
			stopping = true;
		}
		cv.notify_one();
		if( thread.joinable() && std::this_thread::get_id() != thread.get_id() )
			thread.join();
	}

private:
	void run()
	{
		plat::InitThread( apartment );
		for( ;; )
		{
			std::function< void() > fn;
			{
				std::unique_lock< std::mutex > lock( mutex );
				cv.wait( lock, [ this ] { return stopping || !queue.empty(); } );
				if( queue.empty() )
					break;
				fn = std::move( queue.front() );
				queue.pop_front();
			}
			try
			{
				fn();
			}
			catch( const std::exception& e )
			{
				plat::Log( std::string( "worker task failed: " ) + e.what() );
			}
			catch( ... )
			{
				plat::Log( "worker task failed (unknown exception)" );
			}
		}
		plat::UninitThread( apartment );
	}

	plat::Apartment apartment;
	std::thread thread;
	std::mutex mutex;
	std::condition_variable cv;
	std::deque< std::function< void() > > queue;
	bool stopping = false;
};
