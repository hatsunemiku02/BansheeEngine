//__________________________ Banshee Project - A modern game development toolkit _________________________________//
//_____________________________________ www.banshee-project.com __________________________________________________//
//________________________ Copyright (c) 2014 Marko Pintera. All rights reserved. ________________________________//
#include "BsProfilerCPU.h"
#include "BsDebug.h"
#include "BsPlatform.h"

namespace BansheeEngine
{
	ProfilerCPU::Timer::Timer()
	{
		time = 0.0f;
	}

	void ProfilerCPU::Timer::start()
	{
		startTime = getCurrentTime();
	}

	void ProfilerCPU::Timer::stop()
	{
		time += getCurrentTime() - startTime;
	}

	void ProfilerCPU::Timer::reset()
	{
		time = 0.0f;
	}

	inline double ProfilerCPU::Timer::getCurrentTime() 
	{
		return Platform::queryPerformanceTimerMs();
	}

	ProfilerCPU::TimerPrecise::TimerPrecise()
	{
		cycles = 0;
	}

	void ProfilerCPU::TimerPrecise::start()
	{
		startCycles = getNumCycles();
	}

	void ProfilerCPU::TimerPrecise::stop()
	{
		cycles += getNumCycles() - startCycles;
	}

	void ProfilerCPU::TimerPrecise::reset()
	{
		cycles = 0;
	}

	inline UINT64 ProfilerCPU::TimerPrecise::getNumCycles() 
	{
#if BS_COMPILER == BS_COMPILER_GNUC
		asm volatile("cpuid" : : : "%eax", "%ebx", "%ecx", "%edx" );
		UINT32 __a,__d;
		asm volatile("rdtsc" : "=a" (__a), "=d" (__d));
		return ( UINT64(__a) | UINT64(__d) << 32 );
#else
		int a[4];
		int b = 0;
		__cpuid(a, b);
		return __rdtsc();
#endif		
	}

	void ProfilerCPU::ProfileData::beginSample()
	{
		memAllocs = MemoryCounter::getNumAllocs();
		memFrees = MemoryCounter::getNumFrees();

		timer.reset();
		timer.start();
	}

	void ProfilerCPU::ProfileData::endSample()
	{
		timer.stop();

		UINT64 numAllocs = MemoryCounter::getNumAllocs() - memAllocs;
		UINT64 numFrees = MemoryCounter::getNumFrees() - memFrees;

		samples.push_back(ProfileSample(timer.time, numAllocs, numFrees));
	}

	void ProfilerCPU::ProfileData::resumeLastSample()
	{
		timer.start();
		samples.erase(samples.end() - 1);
	}

	void ProfilerCPU::PreciseProfileData::beginSample()
	{
		memAllocs = MemoryCounter::getNumAllocs();
		memFrees = MemoryCounter::getNumFrees();

		timer.reset();
		timer.start();
	}

	void ProfilerCPU::PreciseProfileData::endSample()
	{
		timer.stop();

		UINT64 numAllocs = MemoryCounter::getNumAllocs() - memAllocs;
		UINT64 numFrees = MemoryCounter::getNumFrees() - memFrees;

		samples.push_back(PreciseProfileSample(timer.cycles, numAllocs, numFrees));
	}

	void ProfilerCPU::PreciseProfileData::resumeLastSample()
	{
		timer.start();
		samples.erase(samples.end() - 1);
	}

	BS_THREADLOCAL ProfilerCPU::ThreadInfo* ProfilerCPU::ThreadInfo::activeThread = nullptr;

	ProfilerCPU::ThreadInfo::ThreadInfo()
		:isActive(false), rootBlock(nullptr)
	{

	}

	void ProfilerCPU::ThreadInfo::begin(const ProfilerString& _name)
	{
		if(isActive)
		{
			LOGWRN("Profiler::beginThread called on a thread that was already being sampled");
			return;
		}

		if(rootBlock == nullptr)
			rootBlock = getBlock();

		activeBlock = ActiveBlock(ActiveSamplingType::Basic, rootBlock);
		activeBlocks.push(activeBlock);
		
		rootBlock->name = _name; 
		rootBlock->basic.beginSample();
		isActive = true;
	}

	void ProfilerCPU::ThreadInfo::end()
	{
		if(activeBlock.type == ActiveSamplingType::Basic)
			activeBlock.block->basic.endSample();
		else
			activeBlock.block->precise.endSample();

		activeBlocks.pop();

		if(!isActive)
			LOGWRN("Profiler::endThread called on a thread that isn't being sampled.");

		if(activeBlocks.size() > 0)
		{
			LOGWRN("Profiler::endThread called but not all sample pairs were closed. Sampling data will not be valid.");

			while(activeBlocks.size() > 0)
			{
				ActiveBlock& curBlock = activeBlocks.top();
				if(curBlock.type == ActiveSamplingType::Basic)
					curBlock.block->basic.endSample();
				else
					curBlock.block->precise.endSample();

				activeBlocks.pop();
			}
		}

		isActive = false;
		activeBlocks = ProfilerStack<ActiveBlock>();
		activeBlock = ActiveBlock();
	}

	void ProfilerCPU::ThreadInfo::reset()
	{
		if(isActive)
			end();

		if(rootBlock != nullptr)
			releaseBlock(rootBlock);

		rootBlock = nullptr;
	}

	ProfilerCPU::ProfiledBlock* ProfilerCPU::ThreadInfo::getBlock()
	{
		// TODO - Pool this, if possible using the memory allocator stuff
		// TODO - Also consider moving all samples in ThreadInfo, and also pool them (otherwise I can't pool ProfiledBlock since it will be variable size)
		return bs_new<ProfiledBlock, ProfilerAlloc>();
	}

	void ProfilerCPU::ThreadInfo::releaseBlock(ProfilerCPU::ProfiledBlock* block)
	{
		bs_delete<ProfilerAlloc>(block);
	}

	ProfilerCPU::ProfiledBlock::ProfiledBlock()
	{ }

	ProfilerCPU::ProfiledBlock::~ProfiledBlock()
	{
		ThreadInfo* thread = ThreadInfo::activeThread;

		for(auto& child : children)
			thread->releaseBlock(child);

		children.clear();
	}

	ProfilerCPU::ProfiledBlock* ProfilerCPU::ProfiledBlock::findChild(const ProfilerString& name) const
	{
		for(auto& child : children)
		{
			if(child->name == name)
				return child;
		}

		return nullptr;
	}

	ProfilerCPU::ProfilerCPU()
		:mBasicTimerOverhead(0.0), mPreciseTimerOverhead(0), mBasicSamplingOverheadMs(0.0), mPreciseSamplingOverheadCycles(0),
		mBasicSamplingOverheadCycles(0), mPreciseSamplingOverheadMs(0.0)
	{
		// TODO - We only estimate overhead on program start. It might be better to estimate it each time beginThread is called,
		// and keep separate values per thread.
		estimateTimerOverhead();
	}

	ProfilerCPU::~ProfilerCPU()
	{
		reset();

		BS_LOCK_MUTEX(mThreadSync);

		for(auto& threadInfo : mActiveThreads)
			bs_delete<ProfilerAlloc>(threadInfo);
	}

	void ProfilerCPU::beginThread(const ProfilerString& name)
	{
		ThreadInfo* thread = ThreadInfo::activeThread;
		if(thread == nullptr)
		{
			ThreadInfo::activeThread = bs_new<ThreadInfo, ProfilerAlloc>();
			thread = ThreadInfo::activeThread;

			{
				BS_LOCK_MUTEX(mThreadSync);

				mActiveThreads.push_back(thread);
			}
		}

		thread->begin(name);
	}

	void ProfilerCPU::endThread()
	{
		// I don't do a nullcheck where on purpose, so endSample can be called ASAP
		ThreadInfo::activeThread->end();
	}

	void ProfilerCPU::beginSample(const ProfilerString& name)
	{
		ThreadInfo* thread = ThreadInfo::activeThread;
		if(thread == nullptr || !thread->isActive)
		{
			beginThread("Unknown");
			thread = ThreadInfo::activeThread;
		}

		ProfiledBlock* parent = thread->activeBlock.block;
		ProfiledBlock* block = nullptr;
		
		if(parent != nullptr)
			block = parent->findChild(name);

		if(block == nullptr)
		{
			block = thread->getBlock();
			block->name = name;

			if(parent != nullptr)
				parent->children.push_back(block);
			else
				thread->rootBlock->children.push_back(block);
		}

		thread->activeBlock = ActiveBlock(ActiveSamplingType::Basic, block);
		thread->activeBlocks.push(thread->activeBlock);

		block->basic.beginSample();
	}

	void ProfilerCPU::endSample(const ProfilerString& name)
	{
		ThreadInfo* thread = ThreadInfo::activeThread;
		ProfiledBlock* block = thread->activeBlock.block;

#if BS_DEBUG_MODE
		if(block == nullptr)
		{
			LOGWRN("Mismatched CPUProfiler::endSample. No beginSample was called.");
			return;
		}

		if(thread->activeBlock.type == ActiveSamplingType::Precise)
		{
			LOGWRN("Mismatched CPUProfiler::endSample. Was expecting Profiler::endSamplePrecise.");
			return;
		}

		if(block->name != name)
		{
			LOGWRN("Mismatched CPUProfiler::endSample. Was expecting \"" + String(block->name.c_str()) + 
				"\" but got \"" + String(name.c_str()) + "\". Sampling data will not be valid.");
			return;
		}
#endif

		block->basic.endSample();

		thread->activeBlocks.pop();

		if(!thread->activeBlocks.empty())
			thread->activeBlock = thread->activeBlocks.top();
		else
			thread->activeBlock = ActiveBlock();
	}

	void ProfilerCPU::beginSamplePrecise(const ProfilerString& name)
	{
		// Note: There is a (small) possibility a context switch will happen during this measurement in which case result will be skewed. 
		// Increasing thread priority might help. This is generally only a problem with code that executes a long time (10-15+ ms - depending on OS quant length)
		
		ThreadInfo* thread = ThreadInfo::activeThread;
		if(thread == nullptr || !thread->isActive)
			beginThread("Unknown");

		ProfiledBlock* parent = thread->activeBlock.block;
		ProfiledBlock* block = nullptr;
		
		if(parent != nullptr)
			block = parent->findChild(name);

		if(block == nullptr)
		{
			block = thread->getBlock();
			block->name = name;

			if(parent != nullptr)
				parent->children.push_back(block);
			else
				thread->rootBlock->children.push_back(block);
		}

		thread->activeBlock = ActiveBlock(ActiveSamplingType::Precise, block);
		thread->activeBlocks.push(thread->activeBlock);

		block->precise.beginSample();
	}

	void ProfilerCPU::endSamplePrecise(const ProfilerString& name)
	{
		ThreadInfo* thread = ThreadInfo::activeThread;
		ProfiledBlock* block = thread->activeBlock.block;

#if BS_DEBUG_MODE
		if(block == nullptr)
		{
			LOGWRN("Mismatched Profiler::endSamplePrecise. No beginSamplePrecise was called.");
			return;
		}

		if(thread->activeBlock.type == ActiveSamplingType::Basic)
		{
			LOGWRN("Mismatched CPUProfiler::endSamplePrecise. Was expecting Profiler::endSample.");
			return;
		}

		if(block->name != name)
		{
			LOGWRN("Mismatched Profiler::endSamplePrecise. Was expecting \"" + String(block->name.c_str()) + 
				"\" but got \"" + String(name.c_str()) + "\". Sampling data will not be valid.");
			return;
		}
#endif

		block->precise.endSample();

		thread->activeBlocks.pop();

		if(!thread->activeBlocks.empty())
			thread->activeBlock = thread->activeBlocks.top();
		else
			thread->activeBlock = ActiveBlock();
	}

	void ProfilerCPU::reset()
	{
		ThreadInfo* thread = ThreadInfo::activeThread;

		if(thread != nullptr)
			thread->reset();
	}

	CPUProfilerReport ProfilerCPU::generateReport()
	{
		CPUProfilerReport report;

		ThreadInfo* thread = ThreadInfo::activeThread;
		if(thread == nullptr)
			return report;

		if(thread->isActive)
			thread->end();

		// We need to separate out basic and precise data and form two separate hierarchies
		if(thread->rootBlock == nullptr)
			return report;

		struct TempEntry
		{
			TempEntry(ProfiledBlock* _parentBlock, UINT32 _entryIdx)
				:parentBlock(_parentBlock), entryIdx(_entryIdx)
			{ }

			ProfiledBlock* parentBlock;
			UINT32 entryIdx;
			ProfilerVector<UINT32> childIndexes;
		};

		ProfilerVector<CPUProfilerBasicSamplingEntry> basicEntries;
		ProfilerVector<CPUProfilerPreciseSamplingEntry> preciseEntries;	

		// Fill up flatHierarchy array in a way so we always process children before parents
		ProfilerStack<UINT32> todo;
		ProfilerVector<TempEntry> flatHierarchy;

		UINT32 entryIdx = 0;
		todo.push(entryIdx);
		flatHierarchy.push_back(TempEntry(thread->rootBlock, entryIdx));

		entryIdx++;
		while(!todo.empty())
		{
			UINT32 curDataIdx = todo.top();
			ProfiledBlock* curBlock = flatHierarchy[curDataIdx].parentBlock;

			todo.pop();

			for(auto& child : curBlock->children)
			{
				flatHierarchy[curDataIdx].childIndexes.push_back(entryIdx);

				todo.push(entryIdx);
				flatHierarchy.push_back(TempEntry(child, entryIdx));

				entryIdx++;
			}
		}
		
		// Calculate sampling data for all entries
		basicEntries.resize(flatHierarchy.size());
		preciseEntries.resize(flatHierarchy.size());

		for(auto& iter = flatHierarchy.rbegin(); iter != flatHierarchy.rend(); ++iter)
		{
			TempEntry& curData = *iter;
			ProfiledBlock* curBlock = curData.parentBlock;

			CPUProfilerBasicSamplingEntry* entryBasic = &basicEntries[curData.entryIdx];
			CPUProfilerPreciseSamplingEntry* entryPrecise = &preciseEntries[curData.entryIdx];

			// Calculate basic data
			entryBasic->data.name = String(curBlock->name.c_str());

			entryBasic->data.memAllocs = 0;
			entryBasic->data.memFrees = 0;
			entryBasic->data.totalTimeMs = 0.0;
			entryBasic->data.maxTimeMs = 0.0;
			for(auto& sample : curBlock->basic.samples)
			{
				entryBasic->data.totalTimeMs += sample.time;
				entryBasic->data.maxTimeMs = std::max(entryBasic->data.maxTimeMs, sample.time);
				entryBasic->data.memAllocs += sample.numAllocs;
				entryBasic->data.memFrees += sample.numFrees;
			}

			entryBasic->data.numCalls = (UINT32)curBlock->basic.samples.size();

			if(entryBasic->data.numCalls > 0)
				entryBasic->data.avgTimeMs = entryBasic->data.totalTimeMs / entryBasic->data.numCalls;

			double totalChildTime = 0.0;
			for(auto& childIdx : curData.childIndexes)
			{
				CPUProfilerBasicSamplingEntry* childEntry = &basicEntries[childIdx];
				totalChildTime += childEntry->data.totalTimeMs;
				childEntry->data.pctOfParent = (float)(childEntry->data.totalTimeMs / entryBasic->data.totalTimeMs);

				entryBasic->data.estimatedOverheadMs += childEntry->data.estimatedOverheadMs;
			}

			entryBasic->data.estimatedOverheadMs += curBlock->basic.samples.size() * mBasicSamplingOverheadMs;
			entryBasic->data.estimatedOverheadMs += curBlock->precise.samples.size() * mPreciseSamplingOverheadMs;

			entryBasic->data.totalSelfTimeMs = entryBasic->data.totalTimeMs - totalChildTime;

			if(entryBasic->data.numCalls > 0)
				entryBasic->data.avgSelfTimeMs = entryBasic->data.totalSelfTimeMs / entryBasic->data.numCalls;

			entryBasic->data.estimatedSelfOverheadMs = mBasicTimerOverhead;

			// Calculate precise data
			entryPrecise->data.name = String(curBlock->name.c_str());

			entryPrecise->data.memAllocs = 0;
			entryPrecise->data.memFrees = 0;
			entryPrecise->data.totalCycles = 0;
			entryPrecise->data.maxCycles = 0;
			for(auto& sample : curBlock->precise.samples)
			{
				entryPrecise->data.totalCycles += sample.cycles;
				entryPrecise->data.maxCycles = std::max(entryPrecise->data.maxCycles, sample.cycles);
				entryPrecise->data.memAllocs += sample.numAllocs;
				entryPrecise->data.memFrees += sample.numFrees;
			}

			entryPrecise->data.numCalls = (UINT32)curBlock->precise.samples.size();

			if(entryPrecise->data.numCalls > 0)
				entryPrecise->data.avgCycles = entryPrecise->data.totalCycles / entryPrecise->data.numCalls;

			UINT64 totalChildCycles = 0;
			for(auto& childIdx : curData.childIndexes)
			{
				CPUProfilerPreciseSamplingEntry* childEntry = &preciseEntries[childIdx];
				totalChildCycles += childEntry->data.totalCycles;
				childEntry->data.pctOfParent = childEntry->data.totalCycles / (float)entryPrecise->data.totalCycles;

				entryPrecise->data.estimatedOverhead += childEntry->data.estimatedOverhead;
			}

			entryPrecise->data.estimatedOverhead += curBlock->precise.samples.size() * mPreciseSamplingOverheadCycles;
			entryPrecise->data.estimatedOverhead += curBlock->basic.samples.size() * mBasicSamplingOverheadCycles;

			entryPrecise->data.totalSelfCycles = entryPrecise->data.totalCycles - totalChildCycles;

			if(entryPrecise->data.numCalls > 0)
				entryPrecise->data.avgSelfCycles = entryPrecise->data.totalSelfCycles / entryPrecise->data.numCalls;

			entryPrecise->data.estimatedSelfOverhead = mPreciseTimerOverhead;
		}

		// Prune empty basic entries
		ProfilerStack<UINT32> finalBasicHierarchyTodo;
		ProfilerStack<UINT32> parentBasicEntryIndexes;
		ProfilerVector<TempEntry> newBasicEntries;

		finalBasicHierarchyTodo.push(0);

		entryIdx = 0;
		parentBasicEntryIndexes.push(entryIdx);
		newBasicEntries.push_back(TempEntry(nullptr, entryIdx));

		entryIdx++;

		while(!finalBasicHierarchyTodo.empty())
		{
			UINT32 parentEntryIdx = parentBasicEntryIndexes.top();
			parentBasicEntryIndexes.pop();

			UINT32 curEntryIdx = finalBasicHierarchyTodo.top();
			TempEntry& curEntry = flatHierarchy[curEntryIdx];
			finalBasicHierarchyTodo.pop();

			for(auto& childIdx : curEntry.childIndexes)
			{
				finalBasicHierarchyTodo.push(childIdx);

				CPUProfilerBasicSamplingEntry& basicEntry = basicEntries[childIdx];
				if(basicEntry.data.numCalls > 0)
				{
					newBasicEntries.push_back(TempEntry(nullptr, childIdx));
					newBasicEntries[parentEntryIdx].childIndexes.push_back(entryIdx);

					parentBasicEntryIndexes.push(entryIdx);

					entryIdx++;
				}
				else
					parentBasicEntryIndexes.push(parentEntryIdx);
			}
		}

		if(newBasicEntries.size() > 0)
		{
			ProfilerVector<CPUProfilerBasicSamplingEntry*> finalBasicEntries;

			report.mBasicSamplingRootEntry = basicEntries[newBasicEntries[0].entryIdx];
			finalBasicEntries.push_back(&report.mBasicSamplingRootEntry);

			finalBasicHierarchyTodo.push(0);

			while(!finalBasicHierarchyTodo.empty())
			{
				UINT32 curEntryIdx = finalBasicHierarchyTodo.top();
				finalBasicHierarchyTodo.pop();

				TempEntry& curEntry = newBasicEntries[curEntryIdx];

				CPUProfilerBasicSamplingEntry* basicEntry = finalBasicEntries[curEntryIdx];

				basicEntry->childEntries.resize(curEntry.childIndexes.size());
				UINT32 idx = 0;

				for(auto& childIdx : curEntry.childIndexes)
				{
					TempEntry& childEntry = newBasicEntries[childIdx];
					basicEntry->childEntries[idx] = basicEntries[childEntry.entryIdx];

					finalBasicEntries.push_back(&(basicEntry->childEntries[idx]));
					finalBasicHierarchyTodo.push(childIdx);
					idx++;
				}
			}
		}

		// Prune empty precise entries
		ProfilerStack<UINT32> finalPreciseHierarchyTodo;
		ProfilerStack<UINT32> parentPreciseEntryIndexes;
		ProfilerVector<TempEntry> newPreciseEntries;

		finalPreciseHierarchyTodo.push(0);

		entryIdx = 0;
		parentPreciseEntryIndexes.push(entryIdx);
		newPreciseEntries.push_back(TempEntry(nullptr, entryIdx));

		entryIdx++;

		while(!finalPreciseHierarchyTodo.empty())
		{
			UINT32 parentEntryIdx = parentPreciseEntryIndexes.top();
			parentPreciseEntryIndexes.pop();

			UINT32 curEntryIdx = finalPreciseHierarchyTodo.top();
			TempEntry& curEntry = flatHierarchy[curEntryIdx];
			finalPreciseHierarchyTodo.pop();

			for(auto& childIdx : curEntry.childIndexes)
			{
				finalPreciseHierarchyTodo.push(childIdx);

				CPUProfilerPreciseSamplingEntry& preciseEntry = preciseEntries[childIdx];
				if(preciseEntry.data.numCalls > 0)
				{
					newPreciseEntries.push_back(TempEntry(nullptr, childIdx));
					newPreciseEntries[parentEntryIdx].childIndexes.push_back(entryIdx);

					parentPreciseEntryIndexes.push(entryIdx);

					entryIdx++;
				}
				else
					parentPreciseEntryIndexes.push(parentEntryIdx);
			}
		}

		if(newPreciseEntries.size() > 0)
		{
			ProfilerVector<CPUProfilerPreciseSamplingEntry*> finalPreciseEntries;

			report.mPreciseSamplingRootEntry = preciseEntries[newPreciseEntries[0].entryIdx];
			finalPreciseEntries.push_back(&report.mPreciseSamplingRootEntry);

			finalPreciseHierarchyTodo.push(0);

			while(!finalPreciseHierarchyTodo.empty())
			{
				UINT32 curEntryIdx = finalPreciseHierarchyTodo.top();
				finalPreciseHierarchyTodo.pop();

				TempEntry& curEntry = newPreciseEntries[curEntryIdx];

				CPUProfilerPreciseSamplingEntry* preciseEntry = finalPreciseEntries[curEntryIdx];

				preciseEntry->childEntries.resize(curEntry.childIndexes.size());
				UINT32 idx = 0;

				for(auto& childIdx : curEntry.childIndexes)
				{
					TempEntry& childEntry = newPreciseEntries[childIdx];
					preciseEntry->childEntries[idx] = preciseEntries[childEntry.entryIdx];

					finalPreciseEntries.push_back(&preciseEntry->childEntries.back());
					finalPreciseHierarchyTodo.push(childIdx);
					idx++;
				}
			}
		}

		return report;
	}

	void ProfilerCPU::estimateTimerOverhead()
	{
		// Get an idea of how long timer calls and RDTSC takes
		const UINT32 reps = 1000, sampleReps = 100;

		mBasicTimerOverhead = 1000000.0;
		mPreciseTimerOverhead = 1000000;
		for (UINT32 tries = 0; tries < 20; tries++) 
		{
			Timer timer;
			for (UINT32 i = 0; i < reps; i++) 
			{
				timer.start();
				timer.stop();
			}

			double avgTime = double(timer.time)/double(reps);
			if (avgTime < mBasicTimerOverhead)
				mBasicTimerOverhead = avgTime;

			TimerPrecise timerPrecise;
			for (UINT32 i = 0; i < reps; i++) 
			{
				timerPrecise.start();
				timerPrecise.stop();
			}

			UINT64 avgCycles = timerPrecise.cycles/reps;
			if (avgCycles < mPreciseTimerOverhead)
				mPreciseTimerOverhead = avgCycles;
		}

		mBasicSamplingOverheadMs = 1000000.0;
		mPreciseSamplingOverheadMs = 1000000.0;
		mBasicSamplingOverheadCycles = 1000000;
		mPreciseSamplingOverheadCycles = 1000000;
		for (UINT32 tries = 0; tries < 20; tries++) 
		{
			/************************************************************************/
			/* 				AVERAGE TIME IN MS FOR BASIC SAMPLING                   */
			/************************************************************************/

			Timer timerA;
			timerA.start();

			beginThread("Main");

			// Two different cases that can effect performance, one where
			// sample already exists and other where new one needs to be created
			for (UINT32 i = 0; i < sampleReps; i++) 
			{
				beginSample("TestAvg1");
				endSample("TestAvg1");
				beginSample("TestAvg2");
				endSample("TestAvg2");
				beginSample("TestAvg3");
				endSample("TestAvg3");
				beginSample("TestAvg4");
				endSample("TestAvg4");
				beginSample("TestAvg5");
				endSample("TestAvg5");
				beginSample("TestAvg6");
				endSample("TestAvg6");
				beginSample("TestAvg7");
				endSample("TestAvg7");
				beginSample("TestAvg8");
				endSample("TestAvg8");
				beginSample("TestAvg9");
				endSample("TestAvg9");
				beginSample("TestAvg10");
				endSample("TestAvg10");
			}

			for (UINT32 i = 0; i < sampleReps * 5; i++) 
			{
				beginSample("TestAvg#" + ProfilerString(toString(i).c_str()));
				endSample("TestAvg#" + ProfilerString(toString(i).c_str()));
			}

			endThread();

			timerA.stop();

			reset();

			double avgTimeBasic = double(timerA.time)/double(sampleReps * 10 + sampleReps * 5) - mBasicTimerOverhead;
			if (avgTimeBasic < mBasicSamplingOverheadMs)
				mBasicSamplingOverheadMs = avgTimeBasic;

			/************************************************************************/
			/* 					AVERAGE CYCLES FOR BASIC SAMPLING                   */
			/************************************************************************/

			TimerPrecise timerPreciseA;
			timerPreciseA.start();

			beginThread("Main");

			// Two different cases that can effect performance, one where
			// sample already exists and other where new one needs to be created
			for (UINT32 i = 0; i < sampleReps; i++) 
			{
				beginSample("TestAvg1");
				endSample("TestAvg1");
				beginSample("TestAvg2");
				endSample("TestAvg2");
				beginSample("TestAvg3");
				endSample("TestAvg3");
				beginSample("TestAvg4");
				endSample("TestAvg4");
				beginSample("TestAvg5");
				endSample("TestAvg5");
				beginSample("TestAvg6");
				endSample("TestAvg6");
				beginSample("TestAvg7");
				endSample("TestAvg7");
				beginSample("TestAvg8");
				endSample("TestAvg8");
				beginSample("TestAvg9");
				endSample("TestAvg9");
				beginSample("TestAvg10");
				endSample("TestAvg10");
			}

			for (UINT32 i = 0; i < sampleReps * 5; i++) 
			{
				beginSample("TestAvg#" + ProfilerString(toString(i).c_str()));
				endSample("TestAvg#" + ProfilerString(toString(i).c_str()));
			}

			endThread();
			timerPreciseA.stop();

			reset();

			UINT64 avgCyclesBasic = timerPreciseA.cycles/(sampleReps * 10 + sampleReps * 5) - mPreciseTimerOverhead;
			if (avgCyclesBasic < mBasicSamplingOverheadCycles)
				mBasicSamplingOverheadCycles = avgCyclesBasic;

			/************************************************************************/
			/* 				AVERAGE TIME IN MS FOR PRECISE SAMPLING                 */
			/************************************************************************/

			Timer timerB;
			timerB.start();
			beginThread("Main");

			// Two different cases that can effect performance, one where
			// sample already exists and other where new one needs to be created
			for (UINT32 i = 0; i < sampleReps; i++) 
			{
				beginSamplePrecise("TestAvg1");
				endSamplePrecise("TestAvg1");
				beginSamplePrecise("TestAvg2");
				endSamplePrecise("TestAvg2");
				beginSamplePrecise("TestAvg3");
				endSamplePrecise("TestAvg3");
				beginSamplePrecise("TestAvg4");
				endSamplePrecise("TestAvg4");
				beginSamplePrecise("TestAvg5");
				endSamplePrecise("TestAvg5");
				beginSamplePrecise("TestAvg6");
				endSamplePrecise("TestAvg6");
				beginSamplePrecise("TestAvg7");
				endSamplePrecise("TestAvg7");
				beginSamplePrecise("TestAvg8");
				endSamplePrecise("TestAvg8");
				beginSamplePrecise("TestAvg9");
				endSamplePrecise("TestAvg9");
				beginSamplePrecise("TestAvg10");
				endSamplePrecise("TestAvg10");
			}

			for (UINT32 i = 0; i < sampleReps * 5; i++) 
			{
				beginSamplePrecise("TestAvg#" + ProfilerString(toString(i).c_str()));
				endSamplePrecise("TestAvg#" + ProfilerString(toString(i).c_str()));
			}

			endThread();
			timerB.stop();

			reset();

			double avgTimesPrecise = timerB.time/(sampleReps * 10 + sampleReps * 5);
			if (avgTimesPrecise < mPreciseSamplingOverheadMs)
				mPreciseSamplingOverheadMs = avgTimesPrecise;

			/************************************************************************/
			/* 				AVERAGE CYCLES FOR PRECISE SAMPLING                     */
			/************************************************************************/

			TimerPrecise timerPreciseB;
			timerPreciseB.start();
			beginThread("Main");

			// Two different cases that can effect performance, one where
			// sample already exists and other where new one needs to be created
			for (UINT32 i = 0; i < sampleReps; i++) 
			{
				beginSamplePrecise("TestAvg1");
				endSamplePrecise("TestAvg1");
				beginSamplePrecise("TestAvg2");
				endSamplePrecise("TestAvg2");
				beginSamplePrecise("TestAvg3");
				endSamplePrecise("TestAvg3");
				beginSamplePrecise("TestAvg4");
				endSamplePrecise("TestAvg4");
				beginSamplePrecise("TestAvg5");
				endSamplePrecise("TestAvg5");
				beginSamplePrecise("TestAvg6");
				endSamplePrecise("TestAvg6");
				beginSamplePrecise("TestAvg7");
				endSamplePrecise("TestAvg7");
				beginSamplePrecise("TestAvg8");
				endSamplePrecise("TestAvg8");
				beginSamplePrecise("TestAvg9");
				endSamplePrecise("TestAvg9");
				beginSamplePrecise("TestAvg10");
				endSamplePrecise("TestAvg10");
			}

			for (UINT32 i = 0; i < sampleReps * 5; i++) 
			{
				beginSamplePrecise("TestAvg#" + ProfilerString(toString(i).c_str()));
				endSamplePrecise("TestAvg#" + ProfilerString(toString(i).c_str()));
			}

			endThread();
			timerPreciseB.stop();

			reset();

			UINT64 avgCyclesPrecise = timerPreciseB.cycles/(sampleReps * 10 + sampleReps * 5);
			if (avgCyclesPrecise < mPreciseSamplingOverheadCycles)
				mPreciseSamplingOverheadCycles = avgCyclesPrecise;
		}
	}

	CPUProfilerBasicSamplingEntry::Data::Data()
		:numCalls(0), avgTimeMs(0.0), maxTimeMs(0.0), totalTimeMs(0.0),
		avgSelfTimeMs(0.0), totalSelfTimeMs(0.0), estimatedSelfOverheadMs(0.0),
		estimatedOverheadMs(0.0), pctOfParent(1.0f)
	{ }

	CPUProfilerPreciseSamplingEntry::Data::Data()
		:numCalls(0), avgCycles(0), maxCycles(0), totalCycles(0),
		avgSelfCycles(0), totalSelfCycles(0), estimatedSelfOverhead(0),
		estimatedOverhead(0), pctOfParent(1.0f)
	{ }

	CPUProfilerReport::CPUProfilerReport()
	{

	}

	ProfilerCPU& gProfilerCPU()
	{
		return ProfilerCPU::instance();
	}
}