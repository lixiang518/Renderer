// Copyright Epic Games, Inc. All Rights Reserved.
#include "VisualLogger/VisualLoggerTraceDevice.h"

#if ENABLE_VISUAL_LOG

#include "Serialization/BufferArchive.h"
#include "ObjectTrace.h"
#include "Trace/Trace.inl"
#include "VisualLogger/VisualLoggerCustomVersion.h"
#include "UObject/UE5MainStreamObjectVersion.h"

UE_TRACE_CHANNEL_DEFINE(VisualLoggerChannel);

UE_TRACE_EVENT_BEGIN(VisualLogger, VisualLogEntry)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(double, RecordingTime)
	UE_TRACE_EVENT_FIELD(uint64, OwnerId)
	UE_TRACE_EVENT_FIELD(uint8[], LogEntry)
UE_TRACE_EVENT_END()

FVisualLoggerTraceDevice& FVisualLoggerTraceDevice::Get()
{
	static FVisualLoggerTraceDevice GDevice;
	return GDevice;
}

FVisualLoggerTraceDevice::FVisualLoggerTraceDevice()
{

}

void FVisualLoggerTraceDevice::Cleanup(bool bReleaseMemory)
{

}

void FVisualLoggerTraceDevice::StartRecordingToFile(double TimeStamp)
{
#if UE_TRACE_ENABLED
	UE::Trace::ToggleChannel(TEXT("VisualLogger"), true); 
#endif
}

void FVisualLoggerTraceDevice::StopRecordingToFile(double TimeStamp)
{
#if UE_TRACE_ENABLED
	UE::Trace::ToggleChannel(TEXT("VisualLogger"), false); 
#endif
}

void FVisualLoggerTraceDevice::DiscardRecordingToFile()
{
}

void FVisualLoggerTraceDevice::SetFileName(const FString& InFileName)
{
}

void FVisualLoggerTraceDevice::Serialize(const UObject* InLogOwner, const FName& InOwnerName, const FName& InOwnerDisplayName, const FName& InOwnerClassName, const FVisualLogEntry& InLogEntry)
{
#if OBJECT_TRACE_ENABLED
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(VisualLoggerChannel))
	{
		FBufferArchive Archive;
		Archive.Reserve(1024);
		Archive.UsingCustomVersion(EVisualLoggerVersion::GUID);
		Archive.UsingCustomVersion(FUE5MainStreamObjectVersion::GUID);
		Archive.SetCustomVersion(FUE5MainStreamObjectVersion::GUID, FUE5MainStreamObjectVersion::LatestVersion, "UE5MainStreamObjectVersion");

		Archive << const_cast<FVisualLogEntry&>(InLogEntry);

		UE_TRACE_LOG(VisualLogger, VisualLogEntry, VisualLoggerChannel)
			<< VisualLogEntry.Cycle(FPlatformTime::Cycles64())
			<< VisualLogEntry.RecordingTime(FObjectTrace::GetWorldElapsedTime(InLogOwner->GetWorld()))
			<< VisualLogEntry.OwnerId(FObjectTrace::GetObjectId(InLogOwner))
			<< VisualLogEntry.LogEntry(Archive.GetData(), Archive.Num());
	}
#endif

	ImmediateRenderDelegate.ExecuteIfBound(InLogOwner, InLogEntry);
}

#endif
