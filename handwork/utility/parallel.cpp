// Provide CPU parallel execution support.

#include "parallel.h"
#include "memory.h"
#include <list>
#include <thread>
#include <condition_variable>

namespace handwork
{
	// Parallel Local Definitions
	static std::vector<std::thread> threads;
	static bool shutdownThreads = false;
	class ParallelForLoop;
	static ParallelForLoop *workList = nullptr;
	static std::mutex workListMutex;

	// Bookkeeping variables to help with the implementation of
	// MergeWorkerThreadStats().
	static std::atomic<bool> reportWorkerStats{ false };
	// Number of workers that still need to report their stats.
	static std::atomic<int> reporterCount;
	// After kicking the workers to report their stats, the main thread waits
	// on this condition variable until they've all done so.
	static std::condition_variable reportDoneCondition;
	static std::mutex reportDoneMutex;

	class ParallelForLoop 
	{
	public:
		// ParallelForLoop Public Methods
		ParallelForLoop(std::function<void(int64_t)> func1D, int64_t maxIndex, int chunkSize)
			: func1D(std::move(func1D)),
			maxIndex(maxIndex),
			chunkSize(chunkSize) {}
		ParallelForLoop(const std::function<void(Vector2i)> &f, const Vector2i &count)
			: func2D(f),
			maxIndex(count.x * count.y),
			chunkSize(1) { nX = count.x; }

	public:
		// ParallelForLoop Private Data
		std::function<void(int64_t)> func1D;
		std::function<void(Vector2i)> func2D;
		const int64_t maxIndex;
		const int chunkSize;
		int64_t nextIndex = 0;
		int activeWorkers = 0;
		ParallelForLoop *next = nullptr;
		int nX = -1;

		// ParallelForLoop Private Methods
		bool Finished() const { return nextIndex >= maxIndex && activeWorkers == 0;	}
	};

	void Barrier::Wait() 
	{
		std::unique_lock<std::mutex> lock(mutex);
		CHECK_GT(count, 0);
		if (--count == 0)
			// This is the last thread to reach the barrier; wake up all of the
			// other ones before exiting.
			cv.notify_all();
		else
			// Otherwise there are still threads that haven't reached it. Give
			// up the lock and wait to be notified.
			cv.wait(lock, [this] { return count == 0; });
	}

	static std::condition_variable workListCondition;

	static void workerThreadFunc(int tIndex, std::shared_ptr<Barrier> barrier) 
	{
		LOG(INFO) << "Started execution in worker thread " << tIndex;
		ThreadIndex = tIndex;

		// The main thread sets up a barrier so that it can be sure that all
		// workers have called ProfilerWorkerThreadInit() before it continues
		// (and actually starts the profiling system).
		barrier->Wait();

		// Release our reference to the Barrier so that it's freed once all of
		// the threads have cleared it.
		barrier.reset();

		std::unique_lock<std::mutex> lock(workListMutex);
		while (!shutdownThreads) 
		{
			if (reportWorkerStats)
			{
				if (--reporterCount == 0)
					// Once all worker threads have merged their stats, wake up
					// the main thread.
					reportDoneCondition.notify_one();
				// Now sleep again.
				workListCondition.wait(lock);
			}
			else if (!workList) 
			{
				// Sleep until there are more tasks to run
				workListCondition.wait(lock);
			}
			else 
			{
				// Get work from _workList_ and run loop iterations
				ParallelForLoop &loop = *workList;

				// Run a chunk of loop iterations for _loop_

				// Find the set of loop iterations to run next
				int64_t indexStart = loop.nextIndex;
				int64_t indexEnd =
					std::min(indexStart + loop.chunkSize, loop.maxIndex);

				// Update _loop_ to reflect iterations this thread will run
				loop.nextIndex = indexEnd;
				if (loop.nextIndex == loop.maxIndex) workList = loop.next;
				loop.activeWorkers++;

				// Run loop indices in _[indexStart, indexEnd)_
				lock.unlock();
				for (int64_t index = indexStart; index < indexEnd; ++index) 
				{
					if (loop.func1D) 
					{
						loop.func1D(index);
					}
					// Handle other types of loops
					else 
					{
						CHECK(loop.func2D);
						loop.func2D(Vector2i(index % loop.nX, index / loop.nX));
					}
				}
				lock.lock();

				// Update _loop_ to reflect completion of iterations
				loop.activeWorkers--;
				if (loop.Finished()) workListCondition.notify_all();
			}
		}
		LOG(INFO) << "Exiting worker thread " << tIndex;
	}

	// Parallel Definitions
	void ParallelFor(std::function<void(int64_t)> func, int64_t count, int chunkSize) 
	{
		CHECK(threads.size() > 0 || MaxThreadIndex() == 1);

		// Run iterations immediately if not using threads or if _count_ is small
		if (threads.empty() || count < chunkSize) 
		{
			for (int64_t i = 0; i < count; ++i) func(i);
			return;
		}

		// Create and enqueue _ParallelForLoop_ for this loop
		ParallelForLoop loop(std::move(func), count, chunkSize);
		workListMutex.lock();
		loop.next = workList;
		workList = &loop;
		workListMutex.unlock();

		// Notify worker threads of work to be done
		std::unique_lock<std::mutex> lock(workListMutex);
		workListCondition.notify_all();

		// Help out with parallel loop iterations in the current thread
		while (!loop.Finished()) 
		{
			// Run a chunk of loop iterations for _loop_

			// Find the set of loop iterations to run next
			int64_t indexStart = loop.nextIndex;
			int64_t indexEnd = std::min(indexStart + loop.chunkSize, loop.maxIndex);

			// Update _loop_ to reflect iterations this thread will run
			loop.nextIndex = indexEnd;
			if (loop.nextIndex == loop.maxIndex) workList = loop.next;
			loop.activeWorkers++;

			// Run loop indices in _[indexStart, indexEnd)_
			lock.unlock();
			for (int64_t index = indexStart; index < indexEnd; ++index) 
			{
				if (loop.func1D) 
				{
					loop.func1D(index);
				}
				// Handle other types of loops
				else 
				{
					CHECK(loop.func2D);
					loop.func2D(Vector2i(index % loop.nX, index / loop.nX));
				}
			}
			lock.lock();

			// Update _loop_ to reflect completion of iterations
			loop.activeWorkers--;
		}
	}

	HANDWORK_THREAD_LOCAL int ThreadIndex;

	int MaxThreadIndex() 
	{
		return NumSystemCores();
	}

	void ParallelFor2D(std::function<void(Vector2i)> func, const Vector2i &count)
	{
		CHECK(threads.size() > 0 || MaxThreadIndex() == 1);

		if (threads.empty() || count.x * count.y <= 1)
		{
			for (int y = 0; y < count.y; ++y)
				for (int x = 0; x < count.x; ++x) func(Vector2i(x, y));
			return;
		}

		ParallelForLoop loop(std::move(func), count);
		{
			std::lock_guard<std::mutex> lock(workListMutex);
			loop.next = workList;
			workList = &loop;
		}

		std::unique_lock<std::mutex> lock(workListMutex);
		workListCondition.notify_all();

		// Help out with parallel loop iterations in the current thread
		while (!loop.Finished()) 
		{
			// Run a chunk of loop iterations for _loop_

			// Find the set of loop iterations to run next
			int64_t indexStart = loop.nextIndex;
			int64_t indexEnd = std::min(indexStart + loop.chunkSize, loop.maxIndex);

			// Update _loop_ to reflect iterations this thread will run
			loop.nextIndex = indexEnd;
			if (loop.nextIndex == loop.maxIndex) workList = loop.next;
			loop.activeWorkers++;

			// Run loop indices in _[indexStart, indexEnd)_
			lock.unlock();
			for (int64_t index = indexStart; index < indexEnd; ++index)
			{
				if (loop.func1D) 
				{
					loop.func1D(index);
				}
				// Handle other types of loops
				else 
				{
					CHECK(loop.func2D);
					loop.func2D(Vector2i(index % loop.nX, index / loop.nX));
				}
			}
			lock.lock();

			// Update _loop_ to reflect completion of iterations
			loop.activeWorkers--;
		}
	}

	int NumSystemCores() 
	{
		return std::max(1u, std::thread::hardware_concurrency());
	}

	void ParallelInit() 
	{
		CHECK_EQ(threads.size(), 0);
		int nThreads = MaxThreadIndex();
		ThreadIndex = 0;

		// Create a barrier so that we can be sure all worker threads get past
		// their call to ProfilerWorkerThreadInit() before we return from this
		// function.  In turn, we can be sure that the profiling system isn't
		// started until after all worker threads have done that.
		std::shared_ptr<Barrier> barrier = std::make_shared<Barrier>(nThreads);

		// Launch one fewer worker thread than the total number we want doing
		// work, since the main thread helps out, too.
		for (int i = 0; i < nThreads - 1; ++i)
			threads.push_back(std::thread(workerThreadFunc, i + 1, barrier));

		barrier->Wait();
	}

	void ParallelCleanup() 
	{
		if (threads.empty()) return;

		{
			std::lock_guard<std::mutex> lock(workListMutex);
			shutdownThreads = true;
			workListCondition.notify_all();
		}

		for (std::thread &thread : threads) thread.join();
		threads.erase(threads.begin(), threads.end());
		shutdownThreads = false;
	}

	void MergeWorkerThreadStats() 
	{
		std::unique_lock<std::mutex> lock(workListMutex);
		std::unique_lock<std::mutex> doneLock(reportDoneMutex);
		// Set up state so that the worker threads will know that we would like
		// them to report their thread-specific stats when they wake up.
		reportWorkerStats = true;
		reporterCount = (int)threads.size();

		// Wake up the worker threads.
		workListCondition.notify_all();

		// Wait for all of them to merge their stats.
		reportDoneCondition.wait(lock, []() { return reporterCount == 0; });

		reportWorkerStats = false;
	}

}	// namespace handwork