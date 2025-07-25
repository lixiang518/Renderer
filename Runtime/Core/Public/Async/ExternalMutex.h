// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/LockTags.h"
#include "CoreTypes.h"
#include <atomic>

#define UE_API CORE_API

namespace UE
{

/**
 * A mutex which takes it state from an external source and uses only its 2 LSBs.
 * The external source must ensure that the state is valid for the lifetime of the mutex.
 * 
 * Note: Changes to this class should also be ported to FMutex.
 *		 These classes could be merged via templatization but we would want 
 *		 to make sure this doesn't cause any undesired code-bloat / side effects.
 */
class FExternalMutex final
{
public:
	inline constexpr explicit FExternalMutex(std::atomic<uint8>& InState)
		: State(InState)
	{
	}

	/** Construct in a locked state. Avoids an expensive compare-and-swap at creation time. */
	inline explicit FExternalMutex(std::atomic<uint8>& InState, FAcquireLock)
		: State(InState)
	{
		State.fetch_or(IsLockedFlag, std::memory_order_acquire);
	}

	FExternalMutex(const FExternalMutex&) = delete;
	FExternalMutex& operator=(const FExternalMutex&) = delete;

	inline bool IsLocked() const
	{
		return (State.load(std::memory_order_relaxed) & IsLockedFlag);
	}

	inline bool TryLock()
	{
		uint8 Expected = State.load(std::memory_order_relaxed);
		return !(Expected & IsLockedFlag) &&
			State.compare_exchange_strong(Expected, Expected | IsLockedFlag, std::memory_order_acquire, std::memory_order_relaxed);
	}

	inline void Lock()
	{
		uint8 Expected = State.load(std::memory_order_relaxed) & ~IsLockedFlag & ~MayHaveWaitingLockFlag;
		if (LIKELY(State.compare_exchange_weak(Expected, Expected | IsLockedFlag, std::memory_order_acquire, std::memory_order_relaxed)))
		{
			return;
		}
		LockSlow();
	}

	inline void Unlock()
	{
		if constexpr (bUnlockImmediately)
		{
			// Unlock immediately to allow other threads to acquire the lock while this thread looks for a thread to wake.
			const uint8 LastState = State.fetch_sub(IsLockedFlag, std::memory_order_release);
			if (LIKELY(!(LastState & MayHaveWaitingLockFlag)))
			{
				return;
			}
			WakeWaitingThread();
		}
		else
		{
			uint8 Expected = (State.load(std::memory_order_relaxed) | IsLockedFlag) & ~MayHaveWaitingLockFlag;
			if (LIKELY(State.compare_exchange_weak(Expected, Expected & ~IsLockedFlag, std::memory_order_release, std::memory_order_relaxed)))
			{
				return;
			}
			WakeWaitingThread();
		}
	}

private:
	UE_API void LockSlow();
	UE_API void WakeWaitingThread();

	static constexpr uint8 IsLockedFlag = 1 << 0;
	static constexpr uint8 MayHaveWaitingLockFlag = 1 << 1;

	static constexpr bool bUnlockImmediately = true;

	std::atomic<uint8>& State;
};

} // UE

#undef UE_API
