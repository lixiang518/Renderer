// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 
#include "Async/Fundamental/TaskDelegate.h"
#include "Containers/Array.h"
#include "Containers/ContainerAllocationPolicies.h"
#include "CoreTypes.h"
#include "HAL/Event.h"
#include "HAL/PlatformAffinity.h"
#include "HAL/PlatformMutex.h"
#include "HAL/Thread.h"
#include "Scheduler.h"
#include "Templates/UniquePtr.h"
#include "Templates/UnrealTemplate.h"

#include <atomic>

namespace LowLevelTasks
{
	class UE_DEPRECATED(5.5, "This class will be removed.") FReserveScheduler final : public FSchedulerTls
	{
		UE_NONCOPYABLE(FReserveScheduler);
		static CORE_API FReserveScheduler Singleton;

	public:
		FReserveScheduler() = default;
		~FReserveScheduler();

		using FConditional = TTaskDelegate<bool(), 16>;

		FORCEINLINE_DEBUGGABLE static FReserveScheduler& Get();

		//start number of reserve workers where 0 is the system default
		CORE_API void StartWorkers(uint32 ReserveWorkers = 0, FThread::EForkable IsForkable = FThread::NonForkable, EThreadPriority WorkerPriority = EThreadPriority::TPri_Normal);
		CORE_API void StopWorkers();

		//tries to yield this thread using the YieldEvent and do busywork on a reserve worker.
		CORE_API bool DoReserveWorkUntil(FConditional&& Condition);

	private: 
		struct alignas(64) FYieldedWork
		{
			FEventRef		SleepEvent;
			FConditional	CompletedDelegate = []() { return true; };
			std::atomic<FYieldedWork*> Next { nullptr };
			bool			bPermitBackgroundWork = false;
		};

		TUniquePtr<FThread> CreateWorker(FThread::EForkable IsForkable = FThread::NonForkable, FYieldedWork* ReserveEvent = nullptr, EThreadPriority Priority = EThreadPriority::TPri_Normal);
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
		TEventStack<FYieldedWork> 						EventStack = { ReserveEvents };
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
		TAlignedArray<FYieldedWork>						ReserveEvents;
		UE::FPlatformRecursiveMutex						WorkerThreadsCS;
		TArray<TUniquePtr<FThread>>						WorkerThreads;
		std::atomic_uint								ActiveWorkers { 0 };
		std::atomic_uint								NextWorkerId { 0 };
	};

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	inline FReserveScheduler& FReserveScheduler::Get()
	{
		return Singleton;
	}

	FORCEINLINE_DEBUGGABLE bool DoReserveWorkUntil(FReserveScheduler::FConditional&& Condition)
	{
		return FReserveScheduler::Get().DoReserveWorkUntil(MoveTemp(Condition));
	}

	inline FReserveScheduler::~FReserveScheduler()
	{
		StopWorkers();
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "HAL/CriticalSection.h"
#endif
