#pragma once
// A single background thread with a task queue.
//
// Workers are always owned through std::shared_ptr and the thread keeps its own
// reference, so shutdown() can give up after a timeout (e.g. PowerPoint hangs)
// and detach the thread without anything being freed underneath it. Tasks must
// therefore only capture shared state (shared_ptr), never the plugin's `this`.
#include "Platform.h"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

class Worker
{
public:
	static std::shared_ptr< Worker > create( plat::Apartment apartment )
	{
		std::shared_ptr< Worker > w( new Worker( apartment ) );
		w->thread = std::thread( [ w ] { w->run(); } );
		return w;
	}

	~Worker()
	{
		if( thread.joinable() )
		{
			if( std::this_thread::get_id() == thread.get_id() )
				thread.detach();// last reference dropped by our own thread
			else
				thread.join();
		}
	}

	Worker( const Worker& )            = delete;
	Worker& operator=( const Worker& ) = delete;

	bool post( std::function< void() > fn )
	{
		{
			std::lock_guard< std::mutex > lock( mutex );
			if( stopping )
				return false;
			queue.push_back( std::move( fn ) );
		}
		cv.notify_one();
		return true;
	}

	// Runs fn on the worker and waits for the result. Throws if the worker is shutting down.
	template< class F >
	auto call( F fn ) -> decltype( fn() )
	{
		using R   = decltype( fn() );
		auto task = std::make_shared< std::packaged_task< R() > >( std::move( fn ) );
		auto fut  = task->get_future();
		if( isWorkerThread() )
			( *task )();
		else if( !post( [ task ] { ( *task )(); } ) )
			throw std::runtime_error( "worker stopped" );
		return fut.get();
	}

	bool isWorkerThread() const { return std::this_thread::get_id() == thread.get_id(); }

	// Drops tasks that have not started yet (e.g. superseded requests).
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

	// Finishes the queued tasks and stops. Waits at most timeoutMs; if the thread
	// is stuck it is detached and finishes (or not) on its own. Returns false then.
	bool shutdown( int timeoutMs )
	{
		std::unique_lock< std::mutex > lock( mutex );
		stopping = true;
		cv.notify_all();
		if( isWorkerThread() )
			return true;
		bool done = doneCv.wait_for( lock, std::chrono::milliseconds( timeoutMs ), [ this ] { return finished; } );
		lock.unlock();
		if( thread.joinable() )
		{
			if( done )
				thread.join();
			else
				thread.detach();
		}
		return done;
	}

private:
	explicit Worker( plat::Apartment apartment ) :
		apartment( apartment )
	{
	}

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
		std::lock_guard< std::mutex > lock( mutex );
		finished = true;
		doneCv.notify_all();
	}

	plat::Apartment apartment;
	std::thread thread;
	std::mutex mutex;
	std::condition_variable cv;
	std::condition_variable doneCv;
	std::deque< std::function< void() > > queue;
	bool stopping = false;
	bool finished = false;
};
