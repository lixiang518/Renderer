// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/IndirectArray.h"
#include "UObject/PrintStaleReferencesOptions.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Misc/Guid.h"
#include "Templates/SubclassOf.h"
#include "Engine/EngineTypes.h"
#include "Engine/EngineBaseTypes.h"
#include "UObject/SoftObjectPath.h"
#include "Engine/World.h"
#include "Misc/FrameRate.h"
#include "Subsystems/SubsystemCollection.h"
#include "Subsystems/EngineSubsystem.h"
#include "RHIDefinitions.h"
#include "Templates/PimplPtr.h"
#include "Templates/UniqueObj.h"
#include "Containers/Ticker.h"
#include "DynamicRenderScaling.h"
#include "Misc/StatusLog.h"
#include "Engine.generated.h"

#define WITH_DYNAMIC_RESOLUTION (!UE_SERVER)

class APlayerController;
class Error;
class FAudioDeviceManager;
class FCanvas;
class FCommonViewportClient;
class FFineGrainedPerformanceTracker;
class FPerformanceTrackingChart;
class FScreenSaverInhibitor;
class FTypeContainer;
class FViewport;
class IEngineLoop;
class IHeadMountedDisplay;
class IMessageRpcClient;
class IPerformanceDataConsumer;
class IPortalRpcLocator;
class IPortalServiceLocator;
class FSceneViewExtensions;
class IStereoRendering;
class SViewport;
class UEditorEngine;
class UEngineCustomTimeStep;
class UGameUserSettings;
class UGameViewportClient;
class ULocalPlayer;
class UNetDriver;
class UTimecodeProvider;
class UActorFolder;

#if ALLOW_DEBUG_FILES
class FFineGrainedPerformanceTracker;
#endif

// The kind of failure handling that GetWorldFromContextObject uses 
enum class EGetWorldErrorMode
{
	// Silently returns nullptr, the calling code is expected to handle this gracefully
	ReturnNull,

	// Raises a runtime error but still returns nullptr, the calling code is expected to handle this gracefully
	LogAndReturnNull,

	// Asserts, the calling code is not expecting to handle a failure gracefully
	Assert
};

/**
 * Enumerates types of fully loaded packages.
 */
UENUM()
enum EFullyLoadPackageType : int
{
	/** Load the packages when the map in Tag is loaded. */
	FULLYLOAD_Map,
	/** Load the packages before the game class in Tag is loaded. The Game name MUST be specified in the URL (game=Package.GameName). Useful for loading packages needed to load the game type (a DLC game type, for instance). */
	FULLYLOAD_Game_PreLoadClass,
	/** Load the packages after the game class in Tag is loaded. Will work no matter how game is specified in UWorld::SetGameMode. Useful for modifying shipping gametypes by loading more packages (mutators, for instance). */
	FULLYLOAD_Game_PostLoadClass,
	/** Fully load the package as long as the DLC is loaded. */
	FULLYLOAD_Always,
	/** Load the package for a mutator that is active. */
	FULLYLOAD_Mutator,
	FULLYLOAD_MAX,
};


/**
 * Enumerates transition types.
 */
UENUM()
enum class ETransitionType : uint8
{
	None,
	Paused,
	Loading,
	Saving,
	Connecting,
	Precaching,
	WaitingToConnect,
	MAX
};

/** Status of dynamic resolution that depends on project setting cvar, game user settings, and pause */
enum class EDynamicResolutionStatus
{
	// Dynamic resolution is not supported by this platform.
	Unsupported,

	// Dynamic resolution is disabled by project setting cvar r.DynamicRes.OperationMode=0 or disabled by game user
	// settings with r.DynamicRes.OperationMode=1.
	Disabled,

	// Dynamic resolution has been paused by game thread.
	Paused,

	// Dynamic resolution is currently enabled.
	Enabled,

	// Forced enabled at static resolution fraction for profiling purpose with r.DynamicRes.TestScreenPercentage.
	DebugForceEnabled,
};

/** Information about the state of dynamic resolution. */
struct FDynamicResolutionStateInfos
{
	// Status of dynamic resolution.
	EDynamicResolutionStatus Status;

	// Approximation of the resolution fraction being applied. This is only an approximation because
	// of non (and unecessary) thread safety of this value between game thread, and render thread.
	DynamicRenderScaling::TMap<float> ResolutionFractionApproximations;

	// Maximum resolution fraction set, always MaxResolutionFraction >= ResolutionFractionApproximation.
	DynamicRenderScaling::TMap<float> ResolutionFractionUpperBounds;
};


/** Struct to help hold information about packages needing to be fully-loaded for DLC, etc. */
USTRUCT()
struct FFullyLoadedPackagesInfo
{
	GENERATED_USTRUCT_BODY()

	/** When to load these packages */
	UPROPERTY()
	TEnumAsByte<enum EFullyLoadPackageType> FullyLoadType;

	/** When this map or gametype is loaded, the packages in the following array will be loaded and added to root, then removed from root when map is unloaded */
	UPROPERTY()
	FString Tag;

	/** The list of packages that will be fully loaded when the above Map is loaded */
	UPROPERTY()
	TArray<FName> PackagesToLoad;

	/** List of objects that were loaded, for faster cleanup */
	UPROPERTY()
	TArray<TObjectPtr<class UObject>> LoadedObjects;


	FFullyLoadedPackagesInfo()
		: FullyLoadType(0)
	{
	}

};


/** level streaming updates that should be applied immediately after committing the map change */
USTRUCT()
struct FLevelStreamingStatus
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	FName PackageName;

	UPROPERTY()
	uint32 bShouldBeLoaded:1;

	UPROPERTY()
	uint32 bShouldBeVisible:1;

	UPROPERTY()
	uint32 LODIndex;


	/** Constructors */
	FLevelStreamingStatus(FName InPackageName, bool bInShouldBeLoaded, bool bInShouldBeVisible, int32 InLODIndex)
	: PackageName(InPackageName), bShouldBeLoaded(bInShouldBeLoaded), bShouldBeVisible(bInShouldBeVisible), LODIndex(InLODIndex)
	{}
	FLevelStreamingStatus()
	{
		FMemory::Memzero(this, sizeof(FLevelStreamingStatus));
		LODIndex = INDEX_NONE;
	}
	
};


/**
 * Container for describing various types of netdrivers available to the engine
 * The engine will try to construct a netdriver of a given type and, failing that,
 * the fallback version.
 */
USTRUCT()
struct FNetDriverDefinition
{
	GENERATED_USTRUCT_BODY()

	/** Unique name of this net driver definition */
	UPROPERTY()
	FName DefName;

	/** Class name of primary net driver */
	UPROPERTY()
	FName DriverClassName;

	/** Class name of the fallback net driver if the main net driver class fails to initialize */
	UPROPERTY()
	FName DriverClassNameFallback;

	UPROPERTY()
	int32 MaxChannelsOverride;

	FNetDriverDefinition() :
		DefName(NAME_None),
		DriverClassName(NAME_None),
		DriverClassNameFallback(NAME_None),
		MaxChannelsOverride(INDEX_NONE)
	{
	}
};

/**
* Struct used to configure which NetDriver is started with Iris enabled or not
* Only one attribute out of the NetDriverDefinition, NetDriverName or NetDriverWildcardName should be set along with the bEnableIris property
*/
USTRUCT()
struct FIrisNetDriverConfig 
{
	GENERATED_BODY()

	/**
	 * Name of the net driver definition to configure
	 * e.g. GameNetDriver, BeaconNetDriver, etc.
	 */
	UPROPERTY()
	FName NetDriverDefinition;

	/**
	 * Name of the named driver to configure.
	 * e.g. GameNetDriver, DemoNetDriver, etc.
	 */
	UPROPERTY()
	FName NetDriverName;

	/**
	 * Wildcard match the netdriver name to configure
	 * e.g. NetDriverWildcardName="UnitTestNetDriver*" matches with UnitTestNetDriver_1, UnitTestNetDriver_2, etc.
	 */
	UPROPERTY()
	FString NetDriverWildcardName;

	/**
	 * Configurable property that decides if the NetDriver will use the Iris replication system or not if Iris is enabled
	 */
	UPROPERTY()
	bool bCanUseIris = false;
};


/**
 * Active and named net drivers instantiated from an FNetDriverDefinition
 * The net driver will remain instantiated on this struct until it is destroyed
 */
USTRUCT()
struct FNamedNetDriver
{
	GENERATED_USTRUCT_BODY()

	/** Instantiation of named net driver */
	UPROPERTY(transient)
	TObjectPtr<class UNetDriver> NetDriver;

	/** Definition associated with this net driver */
	FNetDriverDefinition* NetDriverDef;

	FNamedNetDriver() :
		NetDriver(nullptr),
		NetDriverDef(nullptr)
	{}

	FNamedNetDriver(class UNetDriver* InNetDriver, FNetDriverDefinition* InNetDriverDef) :
		NetDriver(InNetDriver),
		NetDriverDef(InNetDriverDef)
	{}

	~FNamedNetDriver() {}
};


/** FWorldContext
 *	A context for dealing with UWorlds at the engine level. As the engine brings up and destroys world, we need a way to keep straight
 *	what world belongs to what.
 *
 *	WorldContexts can be thought of as a track. By default we have 1 track that we load and unload levels on. Adding a second context is adding
 *	a second track; another track of progression for worlds to live on. 
 *
 *	For the GameEngine, there will be one WorldContext until we decide to support multiple simultaneous worlds.
 *	For the EditorEngine, there may be one WorldContext for the EditorWorld and one for the PIE World.
 *
 *	FWorldContext provides both a way to manage 'the current PIE UWorld*' as well as state that goes along with connecting/travelling to 
 *  new worlds.
 *
 *	FWorldContext should remain internal to the UEngine classes. Outside code should not keep pointers or try to manage FWorldContexts directly.
 *	Outside code can still deal with UWorld*, and pass UWorld*s into Engine level functions. The Engine code can look up the relevant context 
 *	for a given UWorld*.
 *
 *  For convenience, FWorldContext can maintain outside pointers to UWorld*s. For example, PIE can tie UWorld* UEditorEngine::PlayWorld to the PIE
 *	world context. If the PIE UWorld changes, the UEditorEngine::PlayWorld pointer will be automatically updated. This is done with AddRef() and
 *  SetCurrentWorld().
 *
 */
USTRUCT()
struct FWorldContext
{
	GENERATED_USTRUCT_BODY()

	/**************************************************************/
	
	TEnumAsByte<EWorldType::Type>	WorldType;

	FSeamlessTravelHandler SeamlessTravelHandler;

	FName ContextHandle;

	/** URL to travel to for pending client connect */
	FString TravelURL;

	/** TravelType for pending client connects */
	uint8 TravelType;

	/** URL the last time we traveled */
	UPROPERTY()
	struct FURL LastURL;

	/** last server we connected to (for "reconnect" command) */
	UPROPERTY()
	struct FURL LastRemoteURL;

	UPROPERTY()
	TObjectPtr<UPendingNetGame>  PendingNetGame;

	/** A list of tag/array pairs that is used at LoadMap time to fully load packages that may be needed for the map/game with DLC, but we can't use DynamicLoadObject to load from the packages */
	UPROPERTY()
	TArray<struct FFullyLoadedPackagesInfo> PackagesToFullyLoad;

	/**
	 * Array of package/ level names that need to be loaded for the pending map change. First level in that array is
	 * going to be made a fake persistent one by using ULevelStreamingPersistent.
	 */
	TArray<FName> LevelsToLoadForPendingMapChange;

	/** Array of already loaded levels. The ordering is arbitrary and depends on what is already loaded and such.	*/
	UPROPERTY()
	TArray<TObjectPtr<class ULevel>> LoadedLevelsForPendingMapChange;

	/** Human readable error string for any failure during a map change request. Empty if there were no failures.	*/
	FString PendingMapChangeFailureDescription;

	/** If true, commit map change the next frame.																	*/
	uint32 bShouldCommitPendingMapChange:1;

	/** Handles to object references; used by the engine to e.g. the prevent objects from being garbage collected.	*/
	UPROPERTY()
	TArray<TObjectPtr<class UObjectReferencer>> ObjectReferencers;

	UPROPERTY()
	TArray<struct FLevelStreamingStatus> PendingLevelStreamingStatusUpdates;

	UPROPERTY()
	TObjectPtr<class UGameViewportClient> GameViewport;

	UPROPERTY()
	TObjectPtr<class UGameInstance> OwningGameInstance;

	/** A list of active net drivers */
	UPROPERTY(transient)
	TArray<FNamedNetDriver> ActiveNetDrivers;

	/** The PIE instance of this world, -1 is default */
	int32	PIEInstance;

	/** The Prefix in front of PIE level names, empty is default */
	FString	PIEPrefix;

	/** The feature level that PIE world should use */
	ERHIFeatureLevel::Type PIEWorldFeatureLevel;

	/** Is this running as a dedicated server */
	bool	RunAsDedicated;

	/** Is this world context waiting for an online login to complete (for PIE) */
	bool	bWaitingOnOnlineSubsystem;

	/** Is this the 'primary' PIE instance.  Primary is preferred when, for example, unique hardware like a VR headset can be used by only one PIE instance. */
	bool	bIsPrimaryPIEInstance;

	/** Handle to this world context's audio device.*/
	uint32 AudioDeviceID;

	/** Custom description to be display in blueprint debugger UI */
	FString CustomDescription;

	// If > 0, tick this world at a fixed rate in PIE
	float PIEFixedTickSeconds  = 0.f;
	float PIEAccumulatedTickSeconds = 0.f;

	/** On a transition to another level (e.g. LoadMap), the engine will verify that these objects have been cleaned up by garbage collection */
	TSet<FObjectKey> GarbageObjectsToVerify;

	/**************************************************************/

	/** Outside pointers to CurrentWorld that should be kept in sync if current world changes  */
	TArray<TObjectPtr<UWorld>*> ExternalReferences;

	/** Adds an external reference */
	void AddRef(UWorld*& WorldPtr)
	{
		AddRef(ObjectPtrWrap(WorldPtr));
	}

	void AddRef(TObjectPtr<UWorld>& WorldPtr)
	{
		WorldPtr = ThisCurrentWorld;
		ExternalReferences.AddUnique(&WorldPtr);
	}

	/** Removes an external reference */
	void RemoveRef(UWorld*& WorldPtr)
	{
		ExternalReferences.Remove(&ObjectPtrWrap(WorldPtr));
		WorldPtr = nullptr;
	}

	/** Set CurrentWorld and update external reference pointers to reflect this*/
	ENGINE_API void SetCurrentWorld(UWorld *World);

	/** Collect FWorldContext references for garbage collection */
	void AddReferencedObjects(FReferenceCollector& Collector, const UObject* ReferencingObject);

	FORCEINLINE UWorld* World() const
	{
		return ThisCurrentWorld;
	}

	FWorldContext()
		: WorldType(EWorldType::None)
		, ContextHandle(NAME_None)
		, TravelURL()
		, TravelType(0)
		, PendingNetGame(nullptr)
		, bShouldCommitPendingMapChange(0)
		, GameViewport(nullptr)
		, OwningGameInstance(nullptr)
		, PIEInstance(INDEX_NONE)
		, PIEWorldFeatureLevel(ERHIFeatureLevel::Num)
		, RunAsDedicated(false)
		, bWaitingOnOnlineSubsystem(false)
		, bIsPrimaryPIEInstance(false)
		, AudioDeviceID(INDEX_NONE)
		, ThisCurrentWorld(nullptr)
	{ }

private:

	TObjectPtr<UWorld>	ThisCurrentWorld;
};


USTRUCT()
struct FStatColorMapEntry
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(globalconfig)
	float In;

	UPROPERTY(globalconfig)
	FColor Out;


	FStatColorMapEntry()
		: In(0)
		, Out(ForceInit)
	{
	}

};


USTRUCT()
struct FStatColorMapping
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(globalconfig)
	FString StatName;

	UPROPERTY(globalconfig)
	TArray<struct FStatColorMapEntry> ColorMap;

	UPROPERTY(globalconfig)
	uint32 DisableBlend:1;


	FStatColorMapping()
		: DisableBlend(false)
	{
	}

};


/** Info about one note dropped in the map during PIE. */
USTRUCT()
struct FDropNoteInfo
{
	GENERATED_USTRUCT_BODY()

	/** Location to create Note actor in edited level. */
	UPROPERTY()
	FVector Location;

	/** Rotation to create Note actor in edited level. */
	UPROPERTY()
	FRotator Rotation;

	/** Text to assign to Note actor in edited level. */
	UPROPERTY()
	FString Comment;


	FDropNoteInfo()
		: Location(ForceInit)
		, Rotation(ForceInit)
	{
	}

};


/** On-screen debug message handling */
/** Helper struct for tracking on screen messages. */
USTRUCT()
struct FScreenMessageString
{
	GENERATED_USTRUCT_BODY()

	/** The 'key' for this message. */
	UPROPERTY(transient)
	uint64 Key;

	/** The message to display. */
	UPROPERTY(transient)
	FString ScreenMessage;

	/** The color to display the message in. */
	UPROPERTY(transient)
	FColor DisplayColor;

	/** The number of frames to display it. */
	UPROPERTY(transient)
	float TimeToDisplay;

	/** The number of frames it has been displayed so far. */
	UPROPERTY(transient)
	float CurrentTimeDisplayed;

	/** Scale of text */
	UPROPERTY(transient)
	FVector2D TextScale;

	FScreenMessageString()
		: Key(0)
		, DisplayColor(ForceInit)
		, TimeToDisplay(0)
		, CurrentTimeDisplayed(0)
		, TextScale(ForceInit)
	{
	}
};


USTRUCT()
struct FGameNameRedirect
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	FName OldGameName;

	UPROPERTY()
	FName NewGameName;
};


USTRUCT()
struct FClassRedirect
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	FName ObjectName;

	UPROPERTY()
	FName OldClassName;

	UPROPERTY()
	FName NewClassName;

	UPROPERTY()
	FName OldSubobjName;

	UPROPERTY()
	FName NewSubobjName;

	UPROPERTY()
	FName NewClassClass; 

	UPROPERTY()
	FName NewClassPackage; 

	UPROPERTY()
	bool InstanceOnly = false;
};


USTRUCT()
struct FStructRedirect
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	FName OldStructName;

	UPROPERTY()
	FName NewStructName;
};


USTRUCT()
struct FPluginRedirect
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	FString OldPluginName;

	UPROPERTY()
	FString NewPluginName;
};

/** Game thread events for dynamic resolution state. */
enum class EDynamicResolutionStateEvent : uint8;


class IAnalyticsProvider;

DECLARE_DELEGATE_OneParam(FBeginStreamingPauseDelegate, FViewport*);
DECLARE_DELEGATE(FEndStreamingPauseDelegate);

enum class EFrameHitchType : uint8;

DECLARE_MULTICAST_DELEGATE_TwoParams(FEngineHitchDetectedDelegate, EFrameHitchType /*HitchType*/, float /*HitchDurationInSeconds*/);

DECLARE_MULTICAST_DELEGATE(FPreRenderDelegate);
DECLARE_MULTICAST_DELEGATE_OneParam(FPreRenderDelegateEx, class FRDGBuilder&);
DECLARE_MULTICAST_DELEGATE(FPostRenderDelegate);
DECLARE_MULTICAST_DELEGATE_OneParam(FPostRenderDelegateEx, class FRDGBuilder&);

DECLARE_DELEGATE_RetVal_ThreeParams(EBrowseReturnVal::Type, FBrowseURL, FWorldContext& WorldContext, FURL URL, FString& Error);
DECLARE_DELEGATE_TwoParams(FPendingLevelUpdate, FWorldContext& Context, float DeltaSeconds);

/**
 * Type of UObject purge type to be performed by the engine
 */
enum class EGarbageCollectionType
{
	None,
	Incremental,
	Full
};

/**
 * Abstract base class of all Engine classes, responsible for management of systems critical to editor or game systems.
 * Also defines default classes for certain engine systems.
 */
UCLASS(abstract, config=Engine, defaultconfig, transient, MinimalAPI)
class UEngine
	: public UObject
	, public FExec
{
	GENERATED_UCLASS_BODY()

private:
	UPROPERTY()
	TObjectPtr<class UFont> TinyFont;

public:
	/** Sets the font used for the smallest engine text */
	UPROPERTY(globalconfig, EditAnywhere, Category=Fonts, meta=(AllowedClasses="/Script/Engine.Font", DisplayName="Tiny Font", ConfigRestartRequired=true))
	FSoftObjectPath TinyFontName;

private:
	UPROPERTY()
	TObjectPtr<class UFont> SmallFont;

public:
	/** Sets the font used for small engine text, used for most debug displays */
	UPROPERTY(globalconfig, EditAnywhere, Category=Fonts, meta=(AllowedClasses="/Script/Engine.Font", DisplayName="Small Font", ConfigRestartRequired=true))
	FSoftObjectPath SmallFontName;

private:
	UPROPERTY()
	TObjectPtr<class UFont> MediumFont;

public:
	/** Sets the font used for medium engine text */
	UPROPERTY(globalconfig, EditAnywhere, Category=Fonts, meta=(AllowedClasses="/Script/Engine.Font", DisplayName="Medium Font", ConfigRestartRequired=true))
	FSoftObjectPath MediumFontName;

private:
	UPROPERTY()
	TObjectPtr<class UFont> LargeFont;

public:
	/** Sets the font used for large engine text */
	UPROPERTY(globalconfig, EditAnywhere, Category=Fonts, meta=(AllowedClasses="/Script/Engine.Font", DisplayName="Large Font", ConfigRestartRequired=true))
	FSoftObjectPath LargeFontName;

private:
	UPROPERTY()
	TObjectPtr<class UFont> SubtitleFont;

public:
	/** Sets the font used by the default Subtitle Manager */
	UPROPERTY(globalconfig, EditAnywhere, Category=Fonts, meta=(AllowedClasses="/Script/Engine.Font", DisplayName="Subtitle Font", ConfigRestartRequired=true), AdvancedDisplay)
	FSoftObjectPath SubtitleFontName;

private:
	UPROPERTY()
	TArray<TObjectPtr<class UFont>> AdditionalFonts;

public:
	/** Sets additional fonts that will be loaded at startup and available using GetAdditionalFont. */
	UPROPERTY(globalconfig, EditAnywhere, Category=Fonts, AdvancedDisplay)
	TArray<FString> AdditionalFontNames;

	UPROPERTY()
	TSubclassOf<class UConsole>  ConsoleClass;

	/** Sets the class to use for the game console summoned with ~ */
	UPROPERTY(globalconfig, noclear, EditAnywhere, Category=DefaultClasses, meta=(MetaClass="/Script/Engine.Console", DisplayName="Console Class", ConfigRestartRequired=true))
	FSoftClassPath ConsoleClassName;

	UPROPERTY()
	TSubclassOf<class UGameViewportClient>  GameViewportClientClass;

	/** Sets the class to use for the game viewport client, which can be overridden to change game-specific input and display behavior. */
	UPROPERTY(globalconfig, noclear, EditAnywhere, Category=DefaultClasses, meta=(MetaClass="/Script/Engine.GameViewportClient", DisplayName="Game Viewport Client Class", ConfigRestartRequired=true))
	FSoftClassPath GameViewportClientClassName;

	UPROPERTY()
	TSubclassOf<class ULocalPlayer>  LocalPlayerClass;

	/** Sets the class to use for local players, which can be overridden to store game-specific information for a local player. */
	UPROPERTY(globalconfig, noclear, EditAnywhere, Category=DefaultClasses, meta=(MetaClass="/Script/Engine.LocalPlayer", DisplayName="Local Player Class", ConfigRestartRequired=true))
	FSoftClassPath LocalPlayerClassName;

	UPROPERTY()
	TSubclassOf<class AWorldSettings>  WorldSettingsClass;

	/** Sets the class to use for WorldSettings, which can be overridden to store game-specific information on map/world. */
	UPROPERTY(globalconfig, noclear, EditAnywhere, Category=DefaultClasses, meta=(MetaClass="/Script/Engine.WorldSettings", DisplayName="World Settings Class", ConfigRestartRequired=true))
	FSoftClassPath WorldSettingsClassName;

	UPROPERTY(globalconfig, noclear, meta=(MetaClass="/Script/NavigationSystem.NavigationSystem", DisplayName="Navigation System Class"))
	FSoftClassPath NavigationSystemClassName;

	/** Sets the class to use for NavigationSystem, which can be overridden to change game-specific navigation/AI behavior. */
	UPROPERTY()
	TSubclassOf<class UNavigationSystemBase>  NavigationSystemClass;

	/** Sets the Navigation System Config class, which can be overridden to change game-specific navigation/AI behavior. */
	UPROPERTY(globalconfig, noclear, meta = (MetaClass = "/Script/NavigationSystem.NavigationSystem", DisplayName = "Navigation System Config Class"))
	FSoftClassPath NavigationSystemConfigClassName;

	UPROPERTY()
	TSubclassOf<class UNavigationSystemConfig>  NavigationSystemConfigClass;
	
	/** Sets the AvoidanceManager class, which can be overridden to change AI crowd behavior. */
	UPROPERTY(globalconfig, noclear, meta=(MetaClass="/Script/Engine.AvoidanceManager", DisplayName="Avoidance Manager Class"))
	FSoftClassPath AvoidanceManagerClassName;
	
	UPROPERTY()
	TSubclassOf<class UAvoidanceManager>  AvoidanceManagerClass;

	/** Sets the class to be used as the default AIController class for pawns. */
	UPROPERTY(globalconfig, noclear, meta = (MetaClass = "/Script/AIModule.AIController", DisplayName = "Default AIController class for all Pawns"))
	FSoftClassPath AIControllerClassName;

	UPROPERTY()
	TSubclassOf<class UPhysicsCollisionHandler>	PhysicsCollisionHandlerClass;

	/** Sets the PhysicsCollisionHandler class to use by default, which can be overridden to change game-specific behavior when objects collide using physics. */
	UPROPERTY(globalconfig, noclear, EditAnywhere, Category=DefaultClasses, meta=(MetaClass="/Script/Engine.PhysicsCollisionHandler", DisplayName="Physics Collision Handler Class", ConfigRestartRequired=true), AdvancedDisplay)
	FSoftClassPath PhysicsCollisionHandlerClassName;

	/** Sets the GameUserSettings class, which can be overridden to support game-specific options for Graphics/Sound/Gameplay. */
	UPROPERTY(globalconfig, noclear, EditAnywhere, Category=DefaultClasses, meta=(MetaClass="/Script/Engine.GameUserSettings", DisplayName="Game User Settings Class", ConfigRestartRequired=true), AdvancedDisplay)
	FSoftClassPath GameUserSettingsClassName;

	UPROPERTY()
	TSubclassOf<class UGameUserSettings> GameUserSettingsClass;

	/** Global instance of the user game settings */
	UPROPERTY()
	TObjectPtr<class UGameUserSettings> GameUserSettings;

	UPROPERTY()
	TSubclassOf<class ALevelScriptActor>  LevelScriptActorClass;

	/** Sets the Level Script Actor class, which can be overridden to allow game-specific behavior in per-map blueprint scripting */
	UPROPERTY(globalconfig, noclear, EditAnywhere, Category=DefaultClasses, meta=(MetaClass="/Script/Engine.LevelScriptActor", DisplayName="Level Script Actor Class", ConfigRestartRequired=true))
	FSoftClassPath LevelScriptActorClassName;
	
	/** Sets the base class to use for new blueprints created in the editor, configurable on a per-game basis */
	UPROPERTY(globalconfig, noclear, EditAnywhere, Category=DefaultClasses, meta=(MetaClass="/Script/CoreUObject.Object", DisplayName="Default Blueprint Base Class", AllowAbstract, BlueprintBaseOnly), AdvancedDisplay)
	FSoftClassPath DefaultBlueprintBaseClassName;

	/** Sets the class for a global object spawned at startup to handle game-specific data. If empty, it will not spawn one */
	UPROPERTY(globalconfig, noclear, EditAnywhere, Category=DefaultClasses, meta=(MetaClass="/Script/CoreUObject.Object", DisplayName="Game Singleton Class", ConfigRestartRequired=true), AdvancedDisplay)
	FSoftClassPath GameSingletonClassName;

	/** A UObject spawned at initialization time to handle game-specific data */
	UPROPERTY()
	TObjectPtr<UObject> GameSingleton;

	/** Sets the class to spawn as the global AssetManager, configurable per game. If empty, it will not spawn one */
	UPROPERTY(globalconfig, noclear, EditAnywhere, Category=DefaultClasses, meta=(MetaClass="/Script/CoreUObject.Object", DisplayName="Asset Manager Class", ConfigRestartRequired=true), AdvancedDisplay)
	FSoftClassPath AssetManagerClassName;

	/** A UObject spawned at initialization time to handle runtime asset loading and management */
	UPROPERTY()
	TObjectPtr<class UAssetManager> AssetManager;

	/** A global default texture. */
	UPROPERTY()
	TObjectPtr<class UTexture2D> DefaultTexture;

	/** Path of the global default texture that is used when no texture is specified. */
	UPROPERTY(globalconfig)
	FSoftObjectPath DefaultTextureName;

	/** A global default diffuse texture.*/
	UPROPERTY()
	TObjectPtr<class UTexture> DefaultDiffuseTexture;

	/** Path of the global default diffuse texture.*/
	UPROPERTY(globalconfig)
	FSoftObjectPath DefaultDiffuseTextureName;

	/** Texture used to render a vertex in the editor */
	UPROPERTY()
	TObjectPtr<class UTexture2D> DefaultBSPVertexTexture;

	/** Path of the texture used to render a vertex in the editor */
	UPROPERTY(globalconfig)
	FSoftObjectPath DefaultBSPVertexTextureName;

	/** Texture used to get random image grain values for post processing */
	UPROPERTY()
	TObjectPtr<class UTexture2D> HighFrequencyNoiseTexture;

	/** Path of the texture used to get random image grain values for post processing */
	UPROPERTY(globalconfig)
	FSoftObjectPath HighFrequencyNoiseTextureName;

	/** Texture used to blur out of focus content, mimics the Bokeh shape of actual cameras */
	UPROPERTY()
	TObjectPtr<class UTexture2D> DefaultBokehTexture;

	/** Path of the texture used to blur out of focus content, mimics the Bokeh shape of actual cameras */
	UPROPERTY(globalconfig)
	FSoftObjectPath DefaultBokehTextureName;

	/** Texture used to bloom when using FFT, mimics characteristic bloom produced in a camera from a signle bright source */
	UPROPERTY()
	TObjectPtr<class UTexture2D> DefaultBloomKernelTexture;

	/** Path of the texture used to bloom when using FFT, mimics characteristic bloom produced in a camera from a signle bright source */
	UPROPERTY(globalconfig)
	FSoftObjectPath DefaultBloomKernelTextureName;
	
	/** Texture used to film grain by default. */
	UPROPERTY()
	TObjectPtr<class UTexture2D> DefaultFilmGrainTexture;

	/** Path of the texture used by film grain by default. */
	UPROPERTY(globalconfig)
	FSoftObjectPath DefaultFilmGrainTextureName;

	/** The material used to render wireframe meshes. */
	UPROPERTY()
	TObjectPtr<class UMaterial> WireframeMaterial;
	
	/** Path of the material used to render wireframe meshes in the editor and debug tools. */
	UPROPERTY(globalconfig)
	FString WireframeMaterialName;

#if WITH_EDITORONLY_DATA
	/** A translucent material used to render things in geometry mode. */
	UPROPERTY()
	TObjectPtr<class UMaterial> GeomMaterial;

	/** Path of the translucent material used to render things in geometry mode. */
	UPROPERTY(globalconfig)
	FSoftObjectPath GeomMaterialName;
#endif

	/** A material used to render debug meshes. */
	UPROPERTY()
	TObjectPtr<class UMaterial> DebugMeshMaterial;

	/** Path of the default material for debug mesh */
	UPROPERTY(globalconfig)
	FSoftObjectPath DebugMeshMaterialName;

	/** Material used for removing Nanite mesh sections from rasterization. */
	UPROPERTY()
	TObjectPtr<class UMaterial> NaniteHiddenSectionMaterial;

	/** Path of the material used for removing Nanite mesh sections from rasterization. */
	UPROPERTY(globalconfig)
	FString NaniteHiddenSectionMaterialName;

	/** A material used to render emissive meshes (e.g. light source surface). */
	UPROPERTY()
	TObjectPtr<class UMaterial> EmissiveMeshMaterial;

	/** Path of the default material for emissive mesh */
	UPROPERTY(globalconfig)
	FSoftObjectPath EmissiveMeshMaterialName;

	/** Material used for visualizing level membership in lit view port modes. */
	UPROPERTY()
	TObjectPtr<class UMaterial> LevelColorationLitMaterial;

	/** Path of the material used for visualizing level membership in lit view port modes. */
	UPROPERTY(globalconfig)
	FString LevelColorationLitMaterialName;

	/** Material used for visualizing level membership in unlit view port modes. */
	UPROPERTY()
	TObjectPtr<class UMaterial> LevelColorationUnlitMaterial;

	/** Path of the material used for visualizing level membership in unlit view port modes. */
	UPROPERTY(globalconfig)
	FString LevelColorationUnlitMaterialName;

	/** Material used for visualizing lighting only w/ lightmap texel density. */
	UPROPERTY()
	TObjectPtr<class UMaterial> LightingTexelDensityMaterial;

	/** Path of the material used for visualizing lighting only w/ lightmap texel density. */
	UPROPERTY(globalconfig)
	FString LightingTexelDensityName;

	/** Material used for visualizing level membership in lit view port modes. Uses shading to show axis directions. */
	UPROPERTY()
	TObjectPtr<class UMaterial> ShadedLevelColorationLitMaterial;

	/** Path of the material used for visualizing level membership in lit view port modes. Uses shading to show axis directions. */
	UPROPERTY(globalconfig)
	FString ShadedLevelColorationLitMaterialName;

	/** Material used for visualizing level membership in unlit view port modes.  Uses shading to show axis directions. */
	UPROPERTY()
	TObjectPtr<class UMaterial> ShadedLevelColorationUnlitMaterial;

	/** Path of the material used for visualizing level membership in unlit view port modes.  Uses shading to show axis directions. */
	UPROPERTY(globalconfig)
	FString ShadedLevelColorationUnlitMaterialName;

	/** Material used to indicate that the associated BSP surface should be removed. */
	UPROPERTY()
	TObjectPtr<class UMaterial> RemoveSurfaceMaterial;

	/** Path of the material used to indicate that the associated BSP surface should be removed. */
	UPROPERTY(globalconfig)
	FSoftObjectPath RemoveSurfaceMaterialName;

	/** Material used to visualize vertex colors as emissive */
	UPROPERTY()
	TObjectPtr<class UMaterial> VertexColorMaterial;

	/** Path of the material used to visualize vertex colors as emissive */
	UPROPERTY(globalconfig)
	FString VertexColorMaterialName;

	/** Material for visualizing vertex colors on meshes in the scene (color only, no alpha) */
	UPROPERTY()
	TObjectPtr<class UMaterial> VertexColorViewModeMaterial_ColorOnly;

	/** Path of the material for visualizing vertex colors on meshes in the scene (color only, no alpha) */
	UPROPERTY(globalconfig)
	FString VertexColorViewModeMaterialName_ColorOnly;

	/** Material for visualizing vertex colors on meshes in the scene (alpha channel as color) */
	UPROPERTY()
	TObjectPtr<class UMaterial> VertexColorViewModeMaterial_AlphaAsColor;

	/** Path of the material for visualizing vertex colors on meshes in the scene (alpha channel as color) */
	UPROPERTY(globalconfig)
	FString VertexColorViewModeMaterialName_AlphaAsColor;

	/** Material for visualizing vertex colors on meshes in the scene (red only) */
	UPROPERTY()
	TObjectPtr<class UMaterial> VertexColorViewModeMaterial_RedOnly;

	/** Path of the material for visualizing vertex colors on meshes in the scene (red only) */
	UPROPERTY(globalconfig)
	FString VertexColorViewModeMaterialName_RedOnly;

	/** Material for visualizing vertex colors on meshes in the scene (green only) */
	UPROPERTY()
	TObjectPtr<class UMaterial> VertexColorViewModeMaterial_GreenOnly;

	/** Path of the material for visualizing vertex colors on meshes in the scene (green only) */
	UPROPERTY(globalconfig)
	FString VertexColorViewModeMaterialName_GreenOnly;

	/** Material for visualizing vertex colors on meshes in the scene (blue only) */
	UPROPERTY()
	TObjectPtr<class UMaterial> VertexColorViewModeMaterial_BlueOnly;

	/** Path of the material for visualizing vertex colors on meshes in the scene (blue only) */
	UPROPERTY(globalconfig)
	FString VertexColorViewModeMaterialName_BlueOnly;

	/** Material for visualizing mesh paint texture colors on meshes in the scene */
	UPROPERTY()
	TObjectPtr<class UMaterial> TextureColorViewModeMaterial;

	/** Path of the material for visualizing mesh paint texture colors on meshes in the scene */
	UPROPERTY(globalconfig)
	FString TextureColorViewModeMaterialName;

	/** Path of the texture used to indicate zen streaming is active. */
	UPROPERTY(globalconfig)
	FSoftObjectPath DefaultZenStreamingTextureName;

#if WITH_EDITORONLY_DATA
	/** Material used to render bone weights on skeletal meshes */
	UPROPERTY()
	TObjectPtr<class UMaterial> BoneWeightMaterial;

	/** Path of the material used to render bone weights on skeletal meshes */
	UPROPERTY(globalconfig)
	FSoftObjectPath BoneWeightMaterialName;

	/** Materials used to render cloth properties on skeletal meshes */
	UPROPERTY()
	TObjectPtr<class UMaterial> ClothPaintMaterial;
	UPROPERTY()
	TObjectPtr<class UMaterial> ClothPaintOpaqueMaterial;
	UPROPERTY()
	TObjectPtr<class UMaterial> ClothPaintMaterialWireframe;
	UPROPERTY()
	TObjectPtr<class UMaterial> ClothPaintOpaqueMaterialWireframe;
	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> ClothPaintMaterialInstance;
	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> ClothPaintOpaqueMaterialInstance;
	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> ClothPaintMaterialWireframeInstance;
	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> ClothPaintOpaqueMaterialWireframeInstance;

	/** Name of the material used to render cloth in the clothing tools */
	UPROPERTY(globalconfig)
	FSoftObjectPath ClothPaintMaterialName;

	/** Name of the material used to render cloth in the clothing tools with full opacity */
	UPROPERTY(globalconfig)
	FSoftObjectPath ClothPaintOpaqueMaterialName;

	/** Name of the material used to render cloth wireframe in the clothing tools */
	UPROPERTY(globalconfig)
	FSoftObjectPath ClothPaintMaterialWireframeName;

	/** Name of the material used to render cloth wireframe in the clothing tools with full opacity */
	UPROPERTY(globalconfig)
	FSoftObjectPath ClothPaintOpaqueMaterialWireframeName;

	/** A material used to render physical material mask on mesh. */
	UPROPERTY()
	TObjectPtr<class UMaterial> PhysicalMaterialMaskMaterial;

	/** A material used to render physical material mask on mesh. */
	UPROPERTY(globalconfig)
	FSoftObjectPath PhysicalMaterialMaskMaterialName;

	/** A material used to render debug meshes. */
	UPROPERTY()
	TObjectPtr<class UMaterial> DebugEditorMaterial;

	/** A material used to flatten materials. */
	UPROPERTY(globalconfig)
	FSoftObjectPath DefaultFlattenMaterialName;

	/** A material used to flatten materials to VT textures. */
	UPROPERTY(globalconfig)
	FSoftObjectPath DefaultHLODFlattenMaterialName;

	/** A material used to flatten materials to VT textures, with the normals being in world space. */
	UPROPERTY(globalconfig)
	FSoftObjectPath DefaultLandscapeFlattenMaterialName;

	/** Materials used when flattening materials */
	UPROPERTY()
	TObjectPtr<class UMaterial> DefaultFlattenMaterial;
	UPROPERTY()
	TObjectPtr<class UMaterial> DefaultHLODFlattenMaterial;
	UPROPERTY()
	TObjectPtr<class UMaterial> DefaultLandscapeFlattenMaterial;
	/** A material used to render the debug texture painting mask on mesh. */
	UPROPERTY()
	TObjectPtr<class UMaterial> TexturePaintingMaskMaterial;

	/** A material used to render the debug texture painting mask on mesh. */
	UPROPERTY(globalconfig)
	FSoftObjectPath TexturePaintingMaskMaterialName;
#endif

	/** A material used to render debug opaque material. Used in various animation editor viewport features. */
	UPROPERTY(globalconfig)
	FSoftObjectPath DebugEditorMaterialName;

	/** Material used to render constraint limits */
	UPROPERTY()
	TObjectPtr<class UMaterial> ConstraintLimitMaterial;

	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> ConstraintLimitMaterialX;

	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> ConstraintLimitMaterialXAxis;

	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> ConstraintLimitMaterialY;
	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> ConstraintLimitMaterialYAxis;

	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> ConstraintLimitMaterialZ;
	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> ConstraintLimitMaterialZAxis;

	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> ConstraintLimitMaterialPrismatic;

	/** Material that renders a message about lightmap settings being invalid. */
	UPROPERTY()
	TObjectPtr<class UMaterial> InvalidLightmapSettingsMaterial;

	/** Path of the material that renders a message about lightmap settings being invalid. */
	UPROPERTY(globalconfig)
	FSoftObjectPath InvalidLightmapSettingsMaterialName;

	/** Material that renders a message about preview shadows being used. */
	UPROPERTY()
	TObjectPtr<class UMaterial> PreviewShadowsIndicatorMaterial;

	/** Path of the material that renders a message about preview shadows being used. */
	UPROPERTY(globalconfig, EditAnywhere, Category=DefaultMaterials, meta=(AllowedClasses="/Script/Engine.Material", DisplayName="Preview Shadows Indicator Material"))
	FSoftObjectPath PreviewShadowsIndicatorMaterialName;

	/** Material that 'fakes' lighting, used for arrows, widgets. */
	UPROPERTY()
	TObjectPtr<class UMaterial> ArrowMaterial;

	/** Arrow material instance with yellow color. */
	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> ArrowMaterialYellow;

	/** Path of the material that 'fakes' lighting, used for arrows, widgets. */
	UPROPERTY(globalconfig)
	FSoftObjectPath ArrowMaterialName;

	/** Color used for the lighting only render mode */
	UPROPERTY(globalconfig)
	FLinearColor LightingOnlyBrightness;

	/** The colors used to render shader complexity. */
	UPROPERTY(globalconfig)
	TArray<FLinearColor> ShaderComplexityColors;

	/** The colors used to render quad complexity. */
	UPROPERTY(globalconfig)
	TArray<FLinearColor> QuadComplexityColors;

	/** The colors used to render light complexity. */
	UPROPERTY(globalconfig)
	TArray<FLinearColor> LightComplexityColors;

	/** The colors used to render stationary light overlap. */
	UPROPERTY(globalconfig)
	TArray<FLinearColor> StationaryLightOverlapColors;

	/** The colors used to render LOD coloration. */
	UPROPERTY(globalconfig)
	TArray<FLinearColor> LODColorationColors;

	/** The colors used to render LOD coloration. */
	UPROPERTY(globalconfig)
	TArray<FLinearColor> HLODColorationColors;

	/** The colors used for texture streaming accuracy debug view modes. */
	UPROPERTY(globalconfig)
	TArray<FLinearColor> StreamingAccuracyColors;

	/** The visualization color when sk mesh not using skin cache. */
	UPROPERTY(globalconfig)
	FLinearColor GPUSkinCacheVisualizationExcludedColor;

	/** The visualization color when sk mesh using skin cache. */
	UPROPERTY(globalconfig)
	FLinearColor GPUSkinCacheVisualizationIncludedColor;

	/** The visualization color when sk mesh using recompute tangents. */
	UPROPERTY(globalconfig)
	FLinearColor GPUSkinCacheVisualizationRecomputeTangentsColor;

	/** The memory visualization threshold in MB for a skin cache entry */
	UPROPERTY(globalconfig)
	float GPUSkinCacheVisualizationLowMemoryThresholdInMB;
	UPROPERTY(globalconfig)
	float GPUSkinCacheVisualizationHighMemoryThresholdInMB;

	/** The memory visualization colors of skin cache */
	UPROPERTY(globalconfig)
	FLinearColor GPUSkinCacheVisualizationLowMemoryColor;
	UPROPERTY(globalconfig)
	FLinearColor GPUSkinCacheVisualizationMidMemoryColor;
	UPROPERTY(globalconfig)
	FLinearColor GPUSkinCacheVisualizationHighMemoryColor;

	/** The visualization colors of ray tracing LOD index offset from raster LOD */
	UPROPERTY(globalconfig)
	TArray<FLinearColor> GPUSkinCacheVisualizationRayTracingLODOffsetColors;

	/**
	* Complexity limits for the various complexity view mode combinations.
	* These limits are used to map instruction counts to ShaderComplexityColors.
	*/
	UPROPERTY(globalconfig)
	float MaxPixelShaderAdditiveComplexityCount;

	UPROPERTY(globalconfig)
	float MaxES3PixelShaderAdditiveComplexityCount;

	/** Minimum lightmap density value for coloring. */
	UPROPERTY(globalconfig)
	float MinLightMapDensity;

	/** Ideal lightmap density value for coloring. */
	UPROPERTY(globalconfig)
	float IdealLightMapDensity;

	/** Maximum lightmap density value for coloring. */
	UPROPERTY(globalconfig)
	float MaxLightMapDensity;

	/** If true, then render gray scale density. */
	UPROPERTY(globalconfig)
	uint32 bRenderLightMapDensityGrayscale:1;

	/** The scale factor when rendering gray scale density. */
	UPROPERTY(globalconfig)
	float RenderLightMapDensityGrayscaleScale;

	/** The scale factor when rendering color density. */
	UPROPERTY(globalconfig)
	float RenderLightMapDensityColorScale;

	/** The color to render vertex mapped objects in for LightMap Density view mode. */
	UPROPERTY(globalconfig)
	FLinearColor LightMapDensityVertexMappedColor;

	/** The color to render selected objects in for LightMap Density view mode. */
	UPROPERTY(globalconfig)
	FLinearColor LightMapDensitySelectedColor;

	/** Colors used to display specific profiling stats */
	UPROPERTY(globalconfig)
	TArray<struct FStatColorMapping> StatColorMappings;

#if WITH_EDITORONLY_DATA
	/** A material used to render the sides of the builder brush/volumes/etc. */
	UPROPERTY()
	TObjectPtr<class UMaterial> EditorBrushMaterial;

	/** Path of the material used to render the sides of the builder brush/volumes/etc. */
	UPROPERTY(globalconfig)
	FSoftObjectPath EditorBrushMaterialName;
#endif

	/** PhysicalMaterial to use if none is defined for a particular object. */
	UPROPERTY()
	TObjectPtr<class UPhysicalMaterial> DefaultPhysMaterial;

	/** Path of the PhysicalMaterial to use if none is defined for a particular object. */
	UPROPERTY(globalconfig)
	FSoftObjectPath DefaultPhysMaterialName;

	/** PhysicalMaterial to use if none is defined for a Destructible object. */
	UPROPERTY()
	TObjectPtr<class UPhysicalMaterial> DefaultDestructiblePhysMaterial;

	/** Path of the PhysicalMaterial to use if none is defined for a particular object. */
	UPROPERTY(globalconfig, EditAnywhere, Category = DefaultMaterials, meta = (AllowedClasses = "/Script/PhysicsCore.PhysicalMaterial", DisplayName = "Destructible Physics Material"))
	FSoftObjectPath DefaultDestructiblePhysMaterialName;

	/** Deprecated rules for redirecting renamed objects, replaced by the CoreRedirects system*/
	UPROPERTY(config)
	TArray<FGameNameRedirect> ActiveGameNameRedirects;

	UPROPERTY(config)
	TArray<FClassRedirect> ActiveClassRedirects;

	UPROPERTY(config)
	TArray<FPluginRedirect> ActivePluginRedirects;

	UPROPERTY(config)
	TArray<FStructRedirect> ActiveStructRedirects;

	/** Texture used for pre-integrated skin shading */
	UPROPERTY()
	TObjectPtr<class UTexture2D> PreIntegratedSkinBRDFTexture;

	/** Path of the texture used for pre-integrated skin shading */
	UPROPERTY(globalconfig)
	FSoftObjectPath PreIntegratedSkinBRDFTextureName;

	/** Tiled blue-noise texture */
	UPROPERTY()
	TObjectPtr<class UTexture2D> BlueNoiseScalarTexture;

	/** Spatial-temporal blue noise texture with two channel output */
	UPROPERTY()
	TObjectPtr<class UTexture2D> BlueNoiseVec2Texture;

	/** Path of the tiled blue-noise texture */
	UPROPERTY(globalconfig)
	FSoftObjectPath BlueNoiseScalarTextureName;

	/** Path of the tiled blue-noise texture */
	UPROPERTY(globalconfig)
	FSoftObjectPath BlueNoiseScalarMobileTextureName;

	/** Path of the tiled blue-noise texture */
	UPROPERTY(globalconfig)
	FSoftObjectPath BlueNoiseVec2TextureName;

	/** Texture used for GGX LTC integration (Amplitude Texture) */
	UPROPERTY()
	TObjectPtr<class UTexture2D> GGXLTCAmpTexture;
	/** Path of the texture used for GGX LTC integration (Amplitude Texture) */
	UPROPERTY(globalconfig)
	FSoftObjectPath GGXLTCAmpTextureName;

	/** Texture used for GGX LTC integration (Matrix Texture) */
	UPROPERTY()
	TObjectPtr<class UTexture2D> GGXLTCMatTexture;
	/** Path of the texture used for GGX LTC integration (Matrix Texture) */
	UPROPERTY(globalconfig)
	FSoftObjectPath GGXLTCMatTextureName;

	/** Texture used for Sheen LTC integration (Matrix Texture) */
	UPROPERTY()
	TObjectPtr<class UTexture2D> SheenLTCTexture;	
	/** Path of the texture used for Sheen LTC integration (Matrix Texture) */
	UPROPERTY(globalconfig)
	FSoftObjectPath SheenLTCTextureName;

	/** Texture used for specular reflection energy conservation */
	UPROPERTY()
	TObjectPtr<class UTexture2D> GGXReflectionEnergyTexture;
	/** Path of the texture used for specular reflection energy conservation */
	UPROPERTY(globalconfig)
	FSoftObjectPath GGXReflectionEnergyTextureName;

	/** Texture used for specular transmission energy conservation */
	UPROPERTY()
	TObjectPtr<class UTexture2D> GGXTransmissionEnergyTexture;
	/** Path of the texture used for specular transmission energy conservation */
	UPROPERTY(globalconfig)
	FSoftObjectPath GGXTransmissionEnergyTextureName;
		
	/** Texture used for sheen energy conservation */
	UPROPERTY()
	TObjectPtr<class UTexture2D> SheenEnergyTexture;
	/** Path of the texture used for sheen energy conservation */
	UPROPERTY(globalconfig)
	FSoftObjectPath SheenLegacyEnergyTextureName;		
	/** Path of the texture used for sheen energy conservation */
	UPROPERTY(globalconfig)
	FSoftObjectPath SheenEnergyTextureName;
		
	/** Texture used for rough diffuse energy conservation */
	UPROPERTY()
	TObjectPtr<class UTexture2D> DiffuseEnergyTexture;
	/** Path of the texture used for rough diffuse energy conservation */
	UPROPERTY(globalconfig)
	FSoftObjectPath DiffuseEnergyTextureName;

	/** Stable glint BSDF texture */
	UPROPERTY()
	TObjectPtr<class UTexture2DArray> GlintTexture;
	/** Stable glint BSDF texture with more variety to cover slope space and avoid circular artifact */
	UPROPERTY()
	TObjectPtr<class UTexture2DArray> GlintTexture2;

	/** Path of the glint BSDF texture */
	UPROPERTY(globalconfig)
	FSoftObjectPath GlintTextureName;

	/** Path of the glint BSDF texture 2 */
	UPROPERTY(globalconfig)
	FSoftObjectPath GlintTexture2Name;
	
	/** Simple volume LUT texture */
	UPROPERTY()
	TObjectPtr<class UVolumeTexture> SimpleVolumeTexture;

	/** Path of the simple volume LUT texture */
	UPROPERTY(globalconfig)
	FSoftObjectPath SimpleVolumeTextureName;
	
	/** Simple volume environment LUT texture */
	UPROPERTY()
	TObjectPtr<class UVolumeTexture> SimpleVolumeEnvTexture;

	/** Path of the simple volume environment LUT texture */
	UPROPERTY(globalconfig)
	FSoftObjectPath SimpleVolumeEnvTextureName;

	/** Texture used to do font rendering in shaders */
	UPROPERTY()
	TObjectPtr<class UTexture2D> MiniFontTexture;

	/** Path of the texture used to do font rendering in shaders */
	UPROPERTY(globalconfig)
	FSoftObjectPath MiniFontTextureName;

	/** Texture used as a placeholder for terrain weight-maps to give the material the correct texture format. */
	UPROPERTY()
	TObjectPtr<class UTexture> WeightMapPlaceholderTexture;

	UPROPERTY()
	TObjectPtr<class UTexture> WeightMapArrayPlaceholderTexture;
	
	/** Path of the texture used as a placeholder for terrain weight-maps to give the material the correct texture format. */
	UPROPERTY(globalconfig)
	FSoftObjectPath WeightMapPlaceholderTextureName;

	UPROPERTY(globalconfig)
	FSoftObjectPath WeightMapArrayPlaceholderTextureName;

	/** Texture used to display LightMapDensity */
	UPROPERTY()
	TObjectPtr<class UTexture2D> LightMapDensityTexture;

	/** Path of the texture used to display LightMapDensity */
	UPROPERTY(globalconfig)
	FSoftObjectPath LightMapDensityTextureName;

	// Variables.

	/** Engine loop, used for callbacks from the engine module into launch. */
	class IEngineLoop* EngineLoop;

	/** The view port representing the current game instance. Can be 0 so don't use without checking. */
	UPROPERTY()
	TObjectPtr<class UGameViewportClient> GameViewport;

	/** Array of deferred command strings/ execs that get executed at the end of the frame */
	UPROPERTY()
	TArray<FString> DeferredCommands;

	/** The distance of the camera's near clipping plane. */
	UPROPERTY(EditAnywhere, config, Category=Settings)
	float NearClipPlane;

	/** Flag for completely disabling subtitles for localized sounds. */
	UPROPERTY(EditAnywhere, config, Category=Subtitles)
	uint32 bSubtitlesEnabled:1;

	/** Flag for forcibly disabling subtitles even if you try to turn them back on they will be off */
	UPROPERTY(EditAnywhere, config, Category=Subtitles)
	uint32 bSubtitlesForcedOff:1;

	/** Script maximum loop iteration count used as a threshold to warn users about script execution runaway */
	UPROPERTY(EditAnywhere, config, Category=Blueprints)
	int32 MaximumLoopIterationCount;

	// Controls whether Blueprint subclasses of actors or components can tick by default.
	//
	// Blueprints that derive from native C++ classes that have bCanEverTick=true will always be able to tick
	// Blueprints that derive from exactly AActor or UActorComponent will always be able to tick
	// Otherwise, they can tick as long as the parent doesn't have meta=(ChildCannotTick) and either bCanBlueprintsTickByDefault is true or the parent has meta=(ChildCanTick)
	UPROPERTY(EditAnywhere, config, Category=Blueprints)
	uint32 bCanBlueprintsTickByDefault:1;

	/** Controls whether anim blueprint nodes that access member variables of their class directly should use the optimized path that avoids a thunk to the Blueprint VM. This will force all anim blueprints to be recompiled. */
	UPROPERTY(EditAnywhere, config, Category="Anim Blueprints")
	uint32 bOptimizeAnimBlueprintMemberVariableAccess:1;

	/** Controls whether by default we allow anim blueprint graph updates to be performed on non-game threads. This enables some extra checks in the anim blueprint compiler that will warn when unsafe operations are being attempted. This will force all anim blueprints to be recompiled. */
	UPROPERTY(EditAnywhere, config, Category="Anim Blueprints")
	uint32 bAllowMultiThreadedAnimationUpdate:1;

	/** Controls whether cascade particle system LODs are updated in real time, or use the set value */
	UPROPERTY(config)
	uint32 bEnableEditorPSysRealtimeLOD:1;

	/** Hook for external systems to transiently and forcibly disable framerate smoothing without stomping the original setting. */
	uint32 bForceDisableFrameRateSmoothing : 1;

	/** Whether to enable framerate smoothing. */
	UPROPERTY(config, EditAnywhere, Category=Framerate, meta=(EditCondition="!bUseFixedFrameRate"))
	uint32 bSmoothFrameRate:1;

	/** Whether to use a fixed framerate. */
	UPROPERTY(config, EditAnywhere, Category=Framerate)
	uint32 bUseFixedFrameRate : 1;
	
	/** The fixed framerate to use. */
	UPROPERTY(config, EditAnywhere, Category=Framerate, meta=(EditCondition="bUseFixedFrameRate", ClampMin = "15.0"))
	float FixedFrameRate;

	/** Range of framerates in which smoothing will kick in */
	UPROPERTY(config, EditAnywhere, Category=Framerate, meta=(UIMin=0, UIMax=200, EditCondition="!bUseFixedFrameRate"))
	FFloatRange SmoothedFrameRateRange;

private:
	/** Controls how the Engine process the Framerate/Timestep */
	UPROPERTY(transient)
	TObjectPtr<UEngineCustomTimeStep> CustomTimeStep;

	/** Broadcasts whenever the custom time step changed. */
	FSimpleMulticastDelegate CustomTimeStepChangedEvent;

	/** Is the current custom time step was initialized properly and if we should shut it down. */
	bool bIsCurrentCustomTimeStepInitialized;

public:
	/**
	 * Override how the Engine process the Framerate/Timestep.
	 * This class will be responsible of updating the application Time and DeltaTime.
	 * Can be used to synchronize the engine with another process (gen-lock).
	 */
	UPROPERTY(AdvancedDisplay, config, EditAnywhere, Category=Framerate, meta=(MetaClass="/Script/Engine.EngineCustomTimeStep", DisplayName="Custom TimeStep"))
	FSoftClassPath CustomTimeStepClassName;

private:
	/** Controls the Engine's timecode. */
	UPROPERTY(transient)
	TObjectPtr<UTimecodeProvider> TimecodeProvider;

	/** Broadcasts whenever the timecode provider changed. */
	FSimpleMulticastDelegate TimecodeProviderChangedEvent;

	/** Is the current timecode provider was initialized properly and if we should shut it down. */
	bool bIsCurrentTimecodeProviderInitialized;

public:
	/** Set TimecodeProvider when the engine is started. */
	UPROPERTY(config, EditAnywhere, Category=Timecode, meta=(MetaClass="/Script/Engine.TimecodeProvider", DisplayName="Timecode Provider"))
	FSoftClassPath TimecodeProviderClassName;

	/**
	 * Generate a default timecode from the computer clock when there is no timecode provider.
	 * On desktop, the system time will be used and will behave as if a USystemTimecodeProvider was set.
	 * On console, the high performance clock will be used. That may introduce drift over time.
	 * If you wish to use the system time on console, set the timecode provider to USystemeTimecodeProvider.
	 */
	UPROPERTY(config, EditAnywhere, Category=Timecode)
	bool bGenerateDefaultTimecode;

	/** When generating a default timecode (bGenerateDefaultTimecode is true and no timecode provider is set) at which frame rate it should be generated (number of frames). */
	UPROPERTY(config, EditAnywhere, Category=Timecode, meta=(EditCondition="bGenerateDefaultTimecode"))
	FFrameRate GenerateDefaultTimecodeFrameRate;

	/** Number of frames to subtract from generated default timecode. */
	UPROPERTY(AdvancedDisplay, config, EditAnywhere, Category=Timecode, meta=(EditCondition="bGenerateDefaultTimecode"))
	float GenerateDefaultTimecodeFrameDelay;

public:
	/** 
	 * Whether we should check for more than N pawns spawning in a single frame.
	 * Basically, spawning pawns and all of their attachments can be slow.  And on consoles it
	 * can be really slow.  If enabled, we will display an on-screen warning whenever this multi-spawn occurs.
	 **/
	UPROPERTY(config)
	uint32 bCheckForMultiplePawnsSpawnedInAFrame:1;

	/** If bCheckForMultiplePawnsSpawnedInAFrame==true, then we will check to see that no more than this number of pawns are spawned in a frame. **/
	UPROPERTY(config)
	int32 NumPawnsAllowedToBeSpawnedInAFrame;

	/**
	 * Whether or not the LQ lightmaps should be generated during lighting rebuilds.  This has been moved to r.SupportLowQualityLightmaps.
	 */
	UPROPERTY(globalconfig)
	uint32 bShouldGenerateLowQualityLightmaps_DEPRECATED :1;

	// Various Colors used for editor and debug rendering

	UPROPERTY()
	FColor C_WorldBox;

	UPROPERTY()
	FColor C_BrushWire;

	UPROPERTY()
	FColor C_AddWire;

	UPROPERTY()
	FColor C_SubtractWire;

	UPROPERTY()
	FColor C_SemiSolidWire;

	UPROPERTY()
	FColor C_NonSolidWire;

	UPROPERTY()
	FColor C_WireBackground;

	UPROPERTY()
	FColor C_ScaleBoxHi;

	UPROPERTY()
	FColor C_VolumeCollision;

	UPROPERTY()
	FColor C_BSPCollision;

	UPROPERTY()
	FColor C_OrthoBackground;

	UPROPERTY()
	FColor C_Volume;

	UPROPERTY()
	FColor C_BrushShape;

	/** The save directory for newly created screenshots */
	UPROPERTY(config, EditAnywhere, Category = Screenshots)
	FDirectoryPath GameScreenshotSaveDirectory;

	UPROPERTY(config, EditAnywhere, Category = PerQualityLevelProperty, AdvancedDisplay)
	bool UseStaticMeshMinLODPerQualityLevels;

	UPROPERTY(config, EditAnywhere, Category = PerQualityLevelProperty, AdvancedDisplay)
	bool UseSkeletalMeshMinLODPerQualityLevels;

	UPROPERTY(config, EditAnywhere, Category = PerQualityLevelProperty, AdvancedDisplay)
	bool UseClothAssetMinLODPerQualityLevels;

	UPROPERTY(config, EditAnywhere, Category = PerQualityLevelProperty, AdvancedDisplay)
	bool UseGrassVarityPerQualityLevels;

	/** The state of the current map transition.  */
	UPROPERTY()
	ETransitionType TransitionType;

	/** The current transition description text. */
	UPROPERTY()
	FString TransitionDescription;

	/** The gamemode for the destination map */
	UPROPERTY()
	FString TransitionGameMode;

	/** Whether to play mature language sound nodes */
	UPROPERTY(config)
	uint32 bAllowMatureLanguage:1;

	/** camera rotation (deg) beyond which occlusion queries are ignored from previous frame (because they are likely not valid) */
	UPROPERTY(config)
	float CameraRotationThreshold;

	/** camera movement beyond which occlusion queries are ignored from previous frame (because they are likely not valid) */
	UPROPERTY(config)
	float CameraTranslationThreshold;

	/** The amount of time a primitive is considered to be probably visible after it was last actually visible. */
	UPROPERTY(config)
	float PrimitiveProbablyVisibleTime;

	/** Max screen pixel fraction where retesting when unoccluded is worth the GPU time. */
	UPROPERTY(config)
	float MaxOcclusionPixelsFraction;

	/** Whether to pause the game if focus is lost. */
	UPROPERTY(config)
	uint32 bPauseOnLossOfFocus:1;

	/**
	 *	The maximum allowed size to a ParticleEmitterInstance::Resize call.
	 *	If larger, the function will return without resizing.
	 */
	UPROPERTY(config)
	int32 MaxParticleResize;

	/** If the resize request is larger than this, spew out a warning to the log */
	UPROPERTY(config)
	int32 MaxParticleResizeWarn;

	/** List of notes to place during Play in Editor */
	UPROPERTY(transient)
	TArray<struct FDropNoteInfo> PendingDroppedNotes;

	/** Number of times to tick each client per second */
	UPROPERTY(globalconfig)
	float NetClientTicksPerSecond;

	/** Current display gamma setting */
	UPROPERTY(config)
	float DisplayGamma;

	/** Minimum desired framerate setting, below this frame rate visual detail may be lowered */
	UPROPERTY(config, EditAnywhere, Category=Framerate, meta=(UIMin=0, ClampMin=0, EditCondition="!bUseFixedFrameRate"))
	float MinDesiredFrameRate;

private:
	/** Default color of selected objects in the level viewport (additive) */
	UPROPERTY(globalconfig)
	FLinearColor DefaultSelectedMaterialColor;

	/** Color of selected objects in the level viewport (additive) */
	UPROPERTY(transient)
	FLinearColor SelectedMaterialColor;

	/** Color of the selection outline color.  Generally the same as selected material color unless the selection material color is being overridden */
	UPROPERTY(transient)
	FLinearColor SelectionOutlineColor;

	/** Subdued version of the selection outline color. Used for indicating sub-selection of components vs actors */
	UPROPERTY(transient)
	FLinearColor SubduedSelectionOutlineColor;

	/** An override to use in some cases instead of the selected material color */
	UPROPERTY(transient)
	FLinearColor SelectedMaterialColorOverride;

	/** Whether or not selection color is being overridden */
	UPROPERTY(transient)
	bool bIsOverridingSelectedColor;
public:

	/** If true, then disable OnScreenDebug messages. Can be toggled in real-time. */
	UPROPERTY(globalconfig)
	uint32 bEnableOnScreenDebugMessages:1;

	/** If true, then disable the display of OnScreenDebug messages (used when running) */
	UPROPERTY(transient)
	uint32 bEnableOnScreenDebugMessagesDisplay:1;

	/** If true, then skip drawing map warnings on screen even in non (UE_BUILD_SHIPPING || UE_BUILD_TEST) builds */
	UPROPERTY(globalconfig)
	uint32 bSuppressMapWarnings:1;

	/** determines whether AI logging should be processed or not */
	UPROPERTY(globalconfig)
	uint32 bDisableAILogging:1;

	/** If true, the visual logger will start recording as soon as the engine starts */
	UPROPERTY(globalconfig)
	uint32 bEnableVisualLogRecordingOnStart;

private:
	
	/** Semaphore to control screen saver inhibitor thread access. */
	UPROPERTY(transient)
	int32 ScreenSaverInhibitorSemaphore;

public:

	/** true if the the user cannot modify levels that are read only. */
	UPROPERTY(transient)
	uint32 bLockReadOnlyLevels:1;

	/** Sets the class to use to spawn a ParticleEventManager that can handle game-specific particle system behavior */
	UPROPERTY(globalconfig)
	FString ParticleEventManagerClassPath;

	/** Used to alter the intensity level of the selection highlight on selected objects */
	UPROPERTY(transient)
	float SelectionHighlightIntensity;

	/** Used to alter the intensity level of the selection highlight on selected BSP surfaces */
	UPROPERTY(transient)
	float BSPSelectionHighlightIntensity;

	/** Used to alter the intensity level of the selection highlight on selected billboard objects */
	UPROPERTY(transient)
	float SelectionHighlightIntensityBillboards;

	/** Delegate handling when streaming pause begins. Set initially in FStreamingPauseRenderingModule::StartupModule() but can then be overridden by games. */
	ENGINE_API void RegisterBeginStreamingPauseRenderingDelegate( FBeginStreamingPauseDelegate* InDelegate );
	FBeginStreamingPauseDelegate* BeginStreamingPauseDelegate;

	/** Delegate handling when streaming pause ends. Set initially in FStreamingPauseRenderingModule::StartupModule() but can then be overridden by games. */
	ENGINE_API void RegisterEndStreamingPauseRenderingDelegate( FEndStreamingPauseDelegate* InDelegate );
	FEndStreamingPauseDelegate* EndStreamingPauseDelegate;

private:
	/** Delegate called just prior to rendering. */
	FPreRenderDelegate PreRenderDelegate;
	FPreRenderDelegateEx PreRenderDelegateEx;
	/** Delegate called just after to rendering. */
	FPostRenderDelegate PostRenderDelegate;
	FPostRenderDelegateEx PostRenderDelegateEx;

public:
	UE_DEPRECATED(5.0, "Please use GetPreRenderDelegateEx().")
	FPreRenderDelegate& GetPreRenderDelegate() { return PreRenderDelegate; }
	FPreRenderDelegateEx& GetPreRenderDelegateEx() { return PreRenderDelegateEx; }
	UE_DEPRECATED(5.0, "Please use GetPostRenderDelegateEx().")
	FPostRenderDelegate& GetPostRenderDelegate() { return PostRenderDelegate; }
	FPostRenderDelegateEx& GetPostRenderDelegateEx() { return PostRenderDelegateEx; }

	/** 
	 * Error message event relating to server travel failures 
	 * 
	 * @param Type type of travel failure
	 * @param ErrorString additional error message
	 */
	DECLARE_EVENT_ThreeParams(UEngine, FOnTravelFailure, UWorld*, ETravelFailure::Type, const FString&);
	FOnTravelFailure TravelFailureEvent;

	/** 
	 * Error message event relating to network failures 
	 * 
	 * @param Type type of network failure
	 * @param Name name of netdriver that generated the failure
	 * @param ErrorString additional error message
	 */
	DECLARE_EVENT_FourParams(UEngine, FOnNetworkFailure, UWorld*, UNetDriver*, ENetworkFailure::Type, const FString&);
	FOnNetworkFailure NetworkFailureEvent;

	/** 
	 * Network lag detected. For the server this means all clients are timing out. On the client it means you are timing out.
	 */
	DECLARE_EVENT_ThreeParams(UEngine, FOnNetworkLagStateChanged, UWorld*, UNetDriver*, ENetworkLagState::Type);
	FOnNetworkLagStateChanged NetworkLagStateChangedEvent;

	/**
	 * Network burst or DDoS detected. Used for triggering analytics, mostly
	 *
	 * @param SeverityCategory	The name of the severity category the DDoS detection escalated to
	 */
	DECLARE_EVENT_ThreeParams(UEngine, FOnNetworkDDoSEscalation, UWorld*, UNetDriver*, FString /*SeverityCategory*/);
	FOnNetworkDDoSEscalation NetworkDDoSEscalationEvent;

	// for IsInitialized()
	bool bIsInitialized;

private:

	/** The last frame GC was run from ConditionalCollectGarbage to avoid multiple GCs in one frame */
	uint64 LastGCFrame;

	/** Time in seconds (game time so we respect time dilation) since the last time we purged references to pending kill objects */
	float TimeSinceLastPendingKillPurge;

	/** Whether a full purge has been triggered, so that the next GarbageCollect will do a full purge no matter what. */
	bool bFullPurgeTriggered;

	/** Whether a full purge is being performed during GC. */
	bool bGCPerformingFullPurge = false;

	/** Whether we should delay GC for one frame to finish some pending operation */
	bool bShouldDelayGarbageCollect; 

public:

	/**
	 * Get the color to use for object selection
	 */
	const FLinearColor& GetSelectedMaterialColor() const { return bIsOverridingSelectedColor ? SelectedMaterialColorOverride : SelectedMaterialColor; }

	const FLinearColor& GetSelectionOutlineColor() const { return SelectionOutlineColor; }

	const FLinearColor& GetSubduedSelectionOutlineColor() const { return SubduedSelectionOutlineColor; }

	const FLinearColor& GetHoveredMaterialColor() const { return GetSelectedMaterialColor(); }

	/**
	 * Sets the selected material color.  
	 * Do not use this if you plan to override the selected material color.  Use OverrideSelectedMaterialColor instead
	 * This is set by the editor preferences
	 *
	 * @param SelectedMaterialColor	The new selection color
	 */
	void SetSelectedMaterialColor( const FLinearColor& InSelectedMaterialColor ) { SelectedMaterialColor = InSelectedMaterialColor; }

	void SetSelectionOutlineColor( const FLinearColor& InSelectionOutlineColor ) { SelectionOutlineColor = InSelectionOutlineColor; }

	void SetSubduedSelectionOutlineColor( const FLinearColor& InSubduedSelectionOutlineColor ) { SubduedSelectionOutlineColor = InSubduedSelectionOutlineColor; }
	/**
	 * Sets an override color to use instead of the user setting
	 *
	 * @param OverrideColor	The override color to use
	 */
	ENGINE_API void OverrideSelectedMaterialColor( const FLinearColor& OverrideColor );

	/**
	 * Restores the selected material color back to the user setting
	 */
	ENGINE_API void RestoreSelectedMaterialColor();

	/** Queries informations about the current state dynamic resolution. */
	ENGINE_API void GetDynamicResolutionCurrentStateInfos(FDynamicResolutionStateInfos& OutInfos) const;

	/** Pause dynamic resolution for this frame. */
	ENGINE_API void PauseDynamicResolution();

	/** Resume dynamic resolution for this frame. */
	FORCEINLINE void ResumeDynamicResolution()
	{
		#if WITH_DYNAMIC_RESOLUTION
			bIsDynamicResolutionPaused = false;
			UpdateDynamicResolutionStatus();
		#endif // !UE_SERVER
	}

	/** Emit an event for dynamic resolution if not already done. */
	ENGINE_API void EmitDynamicResolutionEvent(EDynamicResolutionStateEvent Event);

	/** Get's global dynamic resolution state */
	FORCEINLINE class IDynamicResolutionState* GetDynamicResolutionState()
	{
		#if !WITH_DYNAMIC_RESOLUTION
			return nullptr;
		#else
			// Returns next's frame dynamic resolution state to keep game thread consistency after a ChangeDynamicResolutionStateAtNextFrame().
			check(NextDynamicResolutionState.IsValid() || IsRunningCommandlet() || IsRunningDedicatedServer());
			return NextDynamicResolutionState.Get();
		#endif
	}

	/** Override dynamic resolution state for next frame.
	 * Old dynamic resolution state will be disabled, and the new one will be enabled automatically at next frame.
	 */
	ENGINE_API void ChangeDynamicResolutionStateAtNextFrame(TSharedPtr< class IDynamicResolutionState > NewState);

	/** Get the user setting for dynamic resolution. */
	FORCEINLINE bool GetDynamicResolutionUserSetting() const
	{
		#if !WITH_DYNAMIC_RESOLUTION
			return false;
		#else
			return bDynamicResolutionEnableUserSetting;
		#endif
	}

	/** Set the user setting for dynamic resolution. */
	FORCEINLINE void SetDynamicResolutionUserSetting(bool Enable)
	{
		#if WITH_DYNAMIC_RESOLUTION
			bDynamicResolutionEnableUserSetting = Enable;
			UpdateDynamicResolutionStatus();
		#endif
	}

	/** Delay loading this texture until it is needed by the renderer.
	* The texture is uncompressed and contains no mips so it can't be streamed.
	*/
	ENGINE_API void LoadDefaultBloomTexture();

	/** Delay loading this texture until it is needed by the renderer.
	* The texture is uncompressed and contains no mips so it can't be streamed.
	*/
	ENGINE_API void LoadBlueNoiseTexture(bool LoadVector2BlueNoiseTexture);

	/** Delay loading this texture until it is needed by the renderer. */
	ENGINE_API void LoadDefaultFilmGrainTexture();

	/** Conditionally load this texture for a platform. Always loaded in Editor */
	ENGINE_API void ConditionallyLoadPreIntegratedSkinBRDFTexture();

	/** Delay loading the LTC texture until it is needed by the renderer. */
	ENGINE_API void LoadLTCTextures();

	/** Delay loading the energy shading texture until it is needed by the renderer. */
	ENGINE_API void LoadEnergyTextures();

	/** Delay loading the glint texture until it is needed by the renderer.
	* This texture is not going to be streamed to be available right away.
	*/
	ENGINE_API void LoadGlintTextures();


	/** Delay loading the SimpleVolume texture until it is needed by the renderer.
	* This texture is not going to be streamed to be available right away.
	*/
	ENGINE_API void LoadSimpleVolumeTextures();

private:
	#if WITH_DYNAMIC_RESOLUTION
		/** Last dynamic resolution event. */
		EDynamicResolutionStateEvent LastDynamicResolutionEvent;

		/** Global state for dynamic resolution's heuristic. */
		TSharedPtr< class IDynamicResolutionState > DynamicResolutionState;

		/** Next frame's Global state for dynamic resolution's heuristic. */
		TSharedPtr< class IDynamicResolutionState > NextDynamicResolutionState;

		/** Whether dynamic resolution is paused or not. */
		bool bIsDynamicResolutionPaused;

		/** Game user setting for dynamic resolution that has been committed. */
		bool bDynamicResolutionEnableUserSetting;

		/** Returns whether should be enabled or not. */
		ENGINE_API bool ShouldEnableDynamicResolutionState() const;

		/** Enable/Disable dynamic resolution state according to ShouldEnableDynamicResolutionState(). */
		ENGINE_API void UpdateDynamicResolutionStatus();
	#endif

protected:

	/** The audio device manager */
	FAudioDeviceManager* AudioDeviceManager = nullptr;

	/** Audio device handle to the main audio device. */
	FAudioDeviceHandle MainAudioDeviceHandle;

private:
	/** A collection of messages to display on-screen. */
	TArray<struct FScreenMessageString> PriorityScreenMessages;

	/** A collection of messages to display on-screen. */
	TMap<int32, FScreenMessageString> ScreenMessages;

public:
	ENGINE_API float DrawOnscreenDebugMessages(UWorld* World, FViewport* Viewport, FCanvas* Canvas, UCanvas* CanvasObject, float MessageX, float MessageY);

	/** Add a FString to the On-screen debug message system. bNewerOnTop only works with Key == INDEX_NONE */
	ENGINE_API void AddOnScreenDebugMessage(uint64 Key, float TimeToDisplay, FColor DisplayColor, const FString& DebugMessage, bool bNewerOnTop = true, const FVector2D& TextScale = FVector2D::UnitVector);

	/** Add a FString to the On-screen debug message system. bNewerOnTop only works with Key == INDEX_NONE */
	ENGINE_API void AddOnScreenDebugMessage(int32 Key, float TimeToDisplay, FColor DisplayColor, const FString& DebugMessage, bool bNewerOnTop = true, const FVector2D& TextScale = FVector2D::UnitVector);

	/** Retrieve the message for the given key */
	ENGINE_API bool OnScreenDebugMessageExists(uint64 Key);

	/** Clear any existing debug messages */
	ENGINE_API void ClearOnScreenDebugMessages();

	//Remove the message for the given key
	ENGINE_API void RemoveOnScreenDebugMessage(uint64 Key);

	/** Reference to the stereoscopic rendering interface, if any */
	TSharedPtr< class IStereoRendering, ESPMode::ThreadSafe > StereoRenderingDevice;

	/** Reference to the VR/AR/MR tracking system that is attached, if any */
	TSharedPtr< class IXRTrackingSystem, ESPMode::ThreadSafe > XRSystem;

	/** Extensions that can modify view parameters on the render thread. */
	TSharedPtr<FSceneViewExtensions> ViewExtensions;

	/** Reference to the HMD device that is attached, if any */
	TSharedPtr< class IEyeTracker, ESPMode::ThreadSafe > EyeTrackingDevice;
	
	/** Triggered when a world is added. */	
	DECLARE_EVENT_OneParam( UEngine, FWorldAddedEvent , UWorld* );
	
	/** Return the world added event. */
	FWorldAddedEvent&		OnWorldAdded() { return WorldAddedEvent; }
	
	/** Triggered when a world is destroyed. */	
	DECLARE_EVENT_OneParam( UEngine, FWorldDestroyedEvent , UWorld* );
	
	/** Return the world destroyed event. */	
	FWorldDestroyedEvent&	OnWorldDestroyed() { return WorldDestroyedEvent; }
	
	/** Needs to be called when a world is added to broadcast messages. */	
	ENGINE_API virtual void			WorldAdded( UWorld* World );
	
	/** Needs to be called when a world is destroyed to broadcast messages. */	
	ENGINE_API virtual void			WorldDestroyed( UWorld* InWorld );

	virtual bool IsInitialized() const { return bIsInitialized; }

	/** The feature used to create new worlds, by default. Overridden for feature level preview in the editor */
	ENGINE_API virtual ERHIFeatureLevel::Type GetDefaultWorldFeatureLevel() const;

#if WITH_EDITOR
	/** Return the ini platform name the current preview platform, or false if there is no preview platform. */
	ENGINE_API virtual bool GetPreviewPlatformName(FName& PlatformName) const;

	/** Editor-only event triggered when the actor list of the world has changed */
	DECLARE_EVENT( UEngine, FLevelActorListChangedEvent );
	FLevelActorListChangedEvent& OnLevelActorListChanged() { return LevelActorListChangedEvent; }

	/** Called by internal engine systems after a world's actor list changes in a way not specifiable through other LevelActor__Events to notify other subsystems */
	void BroadcastLevelActorListChanged() { LevelActorListChangedEvent.Broadcast(); }

	/** Editor-only event triggered when actors are added to the world */
	DECLARE_EVENT_OneParam( UEngine, FLevelActorAddedEvent, AActor* );
	FLevelActorAddedEvent& OnLevelActorAdded() { return LevelActorAddedEvent; }

	/** Called by internal engine systems after a level actor has been added */
	void BroadcastLevelActorAdded(AActor* InActor) { LevelActorAddedEvent.Broadcast(InActor); }

	/** Editor-only event triggered when actors are deleted from the world */
	DECLARE_EVENT_OneParam( UEngine, FLevelActorDeletedEvent, AActor* );
	FLevelActorDeletedEvent& OnLevelActorDeleted() { return LevelActorDeletedEvent; }

	/** Called by internal engine systems after level actors have changed to notify other subsystems */
	void BroadcastLevelActorDeleted(AActor* InActor) { LevelActorDeletedEvent.Broadcast(InActor); }

	/** Editor-only event triggered when an actor folder is added to the world */
	DECLARE_EVENT_OneParam(UEngine, FActorFolderAddedEvent, UActorFolder*);
	FActorFolderAddedEvent& OnActorFolderAdded() { return ActorFolderAddedEvent; }

	/** Called by internal engine systems after actor folder is added  */
	void BroadcastActorFolderAdded(UActorFolder* InActorFolder) { ActorFolderAddedEvent.Broadcast(InActorFolder); }

	/** Editor-only event triggered when an actor folder is removed from the world */
	DECLARE_EVENT_OneParam(UEngine, FActorFolderRemovedEvent, UActorFolder*);
	FActorFolderRemovedEvent& OnActorFolderRemoved() { return ActorFolderRemovedEvent; }

	/** Called by internal engine systems after actor folder is removed  */
	void BroadcastActorFolderRemoved(UActorFolder* InActorFolder) { ActorFolderRemovedEvent.Broadcast(InActorFolder); }

	/** Editor-only event triggered when actor folders are updated for a level */
	DECLARE_EVENT_OneParam(UEngine, FActorFoldersUpdatedEvent, ULevel*);
	FActorFoldersUpdatedEvent& OnActorFoldersUpdatedEvent() { return ActorFoldersUpdatedEvent; }

	/** Called by internal engine systems after a level has finished updating its actor folder list */
	void BroadcastActorFoldersUpdated(ULevel* InLevel) { ActorFoldersUpdatedEvent.Broadcast(InLevel); }

	/** Editor-only event triggered when actors outer changes */
	DECLARE_EVENT_TwoParams(UEngine, FLevelActorOuterChangedEvent, AActor*, UObject*);
	FLevelActorOuterChangedEvent& OnLevelActorOuterChanged() { return LevelActorOuterChangedEvent; }

	/** Called by internal engine systems after level actors have changed outer */
	void BroadcastLevelActorOuterChanged(AActor* InActor, UObject* InOldOuter) { LevelActorOuterChangedEvent.Broadcast(InActor, InOldOuter); }

	/** Editor-only event triggered when actors are attached in the world */
	DECLARE_EVENT_TwoParams( UEngine, FLevelActorAttachedEvent, AActor*, const AActor* );
	FLevelActorAttachedEvent& OnLevelActorAttached() { return LevelActorAttachedEvent; }

	/** Called by internal engine systems after a level actor has been attached */
	void BroadcastLevelActorAttached(AActor* InActor, const AActor* InParent) { LevelActorAttachedEvent.Broadcast(InActor, InParent); }

	/** Editor-only event triggered when actors are detached in the world */
	DECLARE_EVENT_TwoParams( UEngine, FLevelActorDetachedEvent, AActor*, const AActor* );
	FLevelActorDetachedEvent& OnLevelActorDetached() { return LevelActorDetachedEvent; }

	/** Called by internal engine systems after a level actor has been detached */
	void BroadcastLevelActorDetached(AActor* InActor, const AActor* InParent) { LevelActorDetachedEvent.Broadcast(InActor, InParent); }

	/** Editor-only event triggered when actors' folders are changed */
	DECLARE_EVENT_TwoParams( UEngine, FLevelActorFolderChangedEvent, const AActor*, FName );
	FLevelActorFolderChangedEvent& OnLevelActorFolderChanged() { return LevelActorFolderChangedEvent; }

	/** Called by internal engine systems after a level actor's folder has been changed */
	void BroadcastLevelActorFolderChanged(const AActor* InActor, FName OldPath) { LevelActorFolderChangedEvent.Broadcast(InActor, OldPath); }

	/** Editor-only event triggered when an actor is being moved, rotated or scaled (AActor::PostEditMove) */
	DECLARE_EVENT_OneParam(UEngine, FOnActorMovingEvent, AActor*);
	FOnActorMovingEvent& OnActorMoving() { return OnActorMovingEvent; }

	/** Called by internal engine systems when an actor is being moved to notify other subsystems */
	void BroadcastOnActorMoving(AActor* Actor) { OnActorMovingEvent.Broadcast(Actor); }

	/** Editor-only event triggered after actors are moved, rotated or scaled by an editor system */
	DECLARE_EVENT_OneParam(UEditorEngine, FOnActorsMovedEvent, TArray<AActor*>&);
	FOnActorsMovedEvent& OnActorsMoved() { return OnActorsMovedEvent; }

	/**
	 * Called when actors have been translated, rotated, or scaled by the editor
	 */
	void BroadcastActorsMoved(TArray<AActor*>& Actors) const { OnActorsMovedEvent.Broadcast(Actors); }

	/** Editor-only event triggered after an actor is moved, rotated or scaled (AActor::PostEditMove) */
	DECLARE_EVENT_OneParam( UEngine, FOnActorMovedEvent, AActor* );
	FOnActorMovedEvent& OnActorMoved() { return OnActorMovedEvent; }

	/** Called by internal engine systems after an actor has been moved to notify other subsystems */
	void BroadcastOnActorMoved( AActor* Actor ) { OnActorMovedEvent.Broadcast( Actor ); }

	/** Editor-only event triggered when any component transform is changed */
	DECLARE_EVENT_TwoParams(UEngine, FOnComponentTransformChangedEvent, USceneComponent*, ETeleportType);
	FOnComponentTransformChangedEvent& OnComponentTransformChanged() { return OnComponentTransformChangedEvent; }

	/** Called by SceneComponent PropagateTransformUpdate to nofify of any component transform change */
	void BroadcastOnComponentTransformChanged(USceneComponent* InComponent, ETeleportType InTeleport) { OnComponentTransformChangedEvent.Broadcast(InComponent, InTeleport); }

	/** Editor-only event triggered when actors are being requested to be renamed */
	DECLARE_EVENT_OneParam( UEngine, FLevelActorRequestRenameEvent, const AActor* );
	FLevelActorRequestRenameEvent& OnLevelActorRequestRename() { return LevelActorRequestRenameEvent; }

	/** Called by internal engine systems after a level actor has been requested to be renamed */
	void BroadcastLevelActorRequestRename(const AActor* InActor) { LevelActorRequestRenameEvent.Broadcast(InActor); }

	/** Editor-only event triggered when actors are being requested to be renamed */
	DECLARE_EVENT_OneParam(UEngine, FLevelComponentRequestRenameEvent, const UActorComponent*);
	FLevelComponentRequestRenameEvent& OnLevelComponentRequestRename() { return LevelComponentRequestRenameEvent; }

	/** Called by internal engine systems after a level actor has been requested to be renamed */
	void BroadcastLevelComponentRequestRename(const UActorComponent* InComponent) { LevelComponentRequestRenameEvent.Broadcast(InComponent); }

	/** Delegate broadcast after UEditorEngine::Tick has been called (or UGameEngine::Tick in standalone) */
	DECLARE_EVENT_OneParam(UEditorEngine, FPostEditorTick, float /* DeltaTime */);
	FPostEditorTick& OnPostEditorTick() { return PostEditorTickEvent; }

	/** Called after UEditorEngine::Tick has been called (or UGameEngine::Tick in standalone) */
	void BroadcastPostEditorTick(float DeltaSeconds) { PostEditorTickEvent.Broadcast(DeltaSeconds); }

	/** Delegate broadcast after UEditorEngine::Tick has been called (or UGameEngine::Tick in standalone) */
	DECLARE_EVENT(UEditorEngine, FEditorCloseEvent);
	FEditorCloseEvent& OnEditorClose() { return EditorCloseEvent; }

	/** Called after UEditorEngine::Tick has been called (or UGameEngine::Tick in standalone) */
	void BroadcastEditorClose() { EditorCloseEvent.Broadcast(); }

#endif // #if WITH_EDITOR

	/** Event triggered after a server travel failure of any kind has occurred */
	FOnTravelFailure& OnTravelFailure() { return TravelFailureEvent; }
	/** Called by internal engine systems after a travel failure has occurred */
	void BroadcastTravelFailure(UWorld* InWorld, ETravelFailure::Type FailureType, const FString& ErrorString = TEXT(""))
	{
		UE_LOGSTATUS(Warning, TEXT("Travel failed, type: %s, reason: \"%s\""), *UEnum::GetValueAsString(FailureType), *ErrorString);
		TravelFailureEvent.Broadcast(InWorld, FailureType, ErrorString);
	}

	/** Event triggered after a network failure of any kind has occurred */
	FOnNetworkFailure& OnNetworkFailure() { return NetworkFailureEvent; }
	/** Called by internal engine systems after a network failure has occurred */
	ENGINE_API void BroadcastNetworkFailure(UWorld * World, UNetDriver *NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString = TEXT(""));

	/** Event triggered after network lag is being experienced or lag has ended */
	FOnNetworkLagStateChanged& OnNetworkLagStateChanged() { return NetworkLagStateChangedEvent; }
	/** Called by internal engine systems after network lag has been detected */
	void BroadcastNetworkLagStateChanged(UWorld * World, UNetDriver *NetDriver, ENetworkLagState::Type LagType)
	{
		NetworkLagStateChangedEvent.Broadcast(World, NetDriver, LagType);
	}

	/** Event triggered when network burst or DDoS is detected */
	FOnNetworkDDoSEscalation& OnNetworkDDoSEscalation() { return NetworkDDoSEscalationEvent; }
	/** Called by internal engine systems after network burst or DDoS is detected */
	void BroadcastNetworkDDosSEscalation(UWorld* World, UNetDriver* NetDriver, FString SeverityCategory)
	{
		NetworkDDoSEscalationEvent.Broadcast(World, NetDriver, SeverityCategory);
	}

	//~ Begin UObject Interface.
	ENGINE_API virtual void FinishDestroy() override;
	ENGINE_API virtual void Serialize(FArchive& Ar) override;
	static ENGINE_API void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
#if WITH_EDITOR
	ENGINE_API virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject Interface.

	/** Initialize the game engine. */
	ENGINE_API virtual void Init(IEngineLoop* InEngineLoop);

	/** Start the game, separate from the initialize call to allow for post initialize configuration before the game starts. */
	ENGINE_API virtual void Start();

	/** Called at shutdown, just before the exit purge.	 */
	ENGINE_API virtual void PreExit();
	ENGINE_API virtual void ReleaseAudioDeviceManager();
	
	ENGINE_API void ShutdownHMD();

	/** Called at startup, in the middle of FEngineLoop::Init.	 */
	ENGINE_API void ParseCommandline();

	//~ Begin FExec Interface
public:
#if UE_ALLOW_EXEC_COMMANDS
	ENGINE_API virtual bool Exec( UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Out=*GLog ) override;
#endif

protected:
	ENGINE_API virtual bool Exec_Dev( UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Out=*GLog ) override;
	ENGINE_API virtual bool Exec_Editor(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Out = *GLog) override;
	//~ End FExec Interface

public:
	/** 
	 * Exec command handlers
	 */
	ENGINE_API bool HandleFlushLogCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleGameVerCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleStatCommand( UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleStopMovieCaptureCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleCrackURLCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleDeferCommand( const TCHAR* Cmd, FOutputDevice& Ar );

	ENGINE_API bool HandleCeCommand( UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleDumpTicksCommand( UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleGammaCommand( const TCHAR* Cmd, FOutputDevice& Ar );

	ENGINE_API bool HandleShowLogCommand( const TCHAR* Cmd, FOutputDevice& Ar );

	// Only compile in when STATS is set
#if STATS
	ENGINE_API bool HandleDumpParticleMemCommand( const TCHAR* Cmd, FOutputDevice& Ar );
#endif

#if WITH_PROFILEGPU && (RHI_NEW_GPU_PROFILER == 0)
	ENGINE_API bool HandleProfileGPUCommand( const TCHAR* Cmd, FOutputDevice& Ar );	
#endif

#if WITH_DUMPGPU
	ENGINE_API bool HandleDumpGPUCommand( const TCHAR* Cmd, FOutputDevice& Ar );
#endif

#if WITH_GPUDEBUGCRASH
	ENGINE_API bool HandleGPUDebugCrashCommand( const TCHAR* Cmd, FOutputDevice& Ar );
#endif

	// Compile in Debug or Development
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST) && WITH_HOT_RELOAD
	ENGINE_API bool HandleHotReloadCommand( const TCHAR* Cmd, FOutputDevice& Ar );
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST) && WITH_HOT_RELOAD

	// Compile in Debug, Development, and Test
#if !UE_BUILD_SHIPPING
	ENGINE_API bool HandleDumpConsoleCommandsCommand( const TCHAR* Cmd, FOutputDevice& Ar, UWorld* InWorld );
	ENGINE_API bool HandleDumpAvailableResolutionsCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleAnimSeqStatsCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleCountDisabledParticleItemsCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleViewnamesCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleFreezeStreamingCommand( const TCHAR* Cmd, FOutputDevice& Ar, UWorld* InWorld );		// Smedis
	ENGINE_API bool HandleFreezeAllCommand( const TCHAR* Cmd, FOutputDevice& Ar, UWorld* InWorld );			// Smedis

	ENGINE_API bool HandleToggleRenderingThreadCommand( const TCHAR* Cmd, FOutputDevice& Ar );	
	ENGINE_API bool HandleRecompileShadersCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleRecompileGlobalShadersCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleDumpShaderStatsCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleDumpMaterialStatsCommand( const TCHAR* Cmd, FOutputDevice& Ar );
#if WITH_EDITOR
	ENGINE_API bool HandleDumpShaderCompileStatsCommand( const TCHAR* Cmd, FOutputDevice& Ar );
#endif
	ENGINE_API bool HandleProfileCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleProfileGPUHitchesCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleShaderComplexityCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleFreezeRenderingCommand( const TCHAR* Cmd, FOutputDevice& Ar, UWorld* InWorld );
	ENGINE_API bool HandleStartFPSChartCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleStopFPSChartCommand( const TCHAR* Cmd, FOutputDevice& Ar, UWorld* InWorld );
	ENGINE_API bool HandleDumpLevelScriptActorsCommand( UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleKismetEventCommand( UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleListTexturesCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleListStaticMeshesCommand(const TCHAR* Cmd, FOutputDevice& Ar);
	ENGINE_API bool HandleListSkeletalMeshesCommand(const TCHAR* Cmd, FOutputDevice& Ar);
	ENGINE_API bool HandleListAnimsCommand(const TCHAR* Cmd, FOutputDevice& Ar);
	ENGINE_API bool HandleRemoteTextureStatsCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleListParticleSystemsCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleListSpawnedActorsCommand( const TCHAR* Cmd, FOutputDevice& Ar, UWorld* InWorld );
	ENGINE_API bool HandleLogoutStatLevelsCommand( const TCHAR* Cmd, FOutputDevice& Ar, UWorld* InWorld );
	ENGINE_API virtual void WriteMemReportMetadata( FOutputDevice& Ar, UWorld* InWorld );
	ENGINE_API bool HandleMemReportCommand( const TCHAR* Cmd, FOutputDevice& Ar, UWorld* InWorld );
	ENGINE_API bool HandleMemReportDeferredCommand( const TCHAR* Cmd, FOutputDevice& Ar, UWorld* InWorld );
	ENGINE_API bool HandleSkeletalMeshReportCommand(const TCHAR* Cmd, FOutputDevice& Ar, UWorld* InWorld);
	ENGINE_API bool HandleParticleMeshUsageCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleDumpParticleCountsCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleListLoadedPackagesCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleMemCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleDebugCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleMergeMeshCommand( const TCHAR* Cmd, FOutputDevice& Ar, UWorld* InWorld );
	ENGINE_API bool HandleContentComparisonCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleTogglegtPsysLODCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleObjCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleTestslateGameUICommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleDirCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleTrackParticleRenderingStatsCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleDumpAllocatorStats( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleHeapCheckCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleToggleOnscreenDebugMessageDisplayCommand( const TCHAR* Cmd, FOutputDevice& Ar );
	ENGINE_API bool HandleToggleOnscreenDebugMessageSystemCommand( const TCHAR* Cmd, FOutputDevice& Ar );	
	ENGINE_API bool HandleDisableAllScreenMessagesCommand( const TCHAR* Cmd, FOutputDevice& Ar );			
	ENGINE_API bool HandleEnableAllScreenMessagesCommand( const TCHAR* Cmd, FOutputDevice& Ar );			
	ENGINE_API bool HandleToggleAllScreenMessagesCommand( const TCHAR* Cmd, FOutputDevice& Ar );			
	ENGINE_API bool HandleConfigHashCommand( const TCHAR* Cmd, FOutputDevice& Ar );						
	ENGINE_API bool HandleConfigMemCommand( const TCHAR* Cmd, FOutputDevice& Ar );	
	ENGINE_API bool HandleGetIniCommand(const TCHAR* Cmd, FOutputDevice& Ar);
	ENGINE_API bool HandleRedirectOutputCommand(const TCHAR* Cmd, FOutputDevice& Ar, UWorld* InWorld);
#endif // !UE_BUILD_SHIPPING

	/** Update everything. */
	ENGINE_API virtual void Tick( float DeltaSeconds, bool bIdleMode ) PURE_VIRTUAL(UEngine::Tick,);

	/**
	 * Update FApp::CurrentTime / FApp::DeltaTime while taking into account max tick rate.
	 */
	ENGINE_API virtual void UpdateTimeAndHandleMaxTickRate();

	static ENGINE_API void SetInputSampleLatencyMarker(uint64 FrameNumber);

	static ENGINE_API void SetSimulationLatencyMarkerStart(uint64 FrameNumber);
	static ENGINE_API void SetSimulationLatencyMarkerEnd(uint64 FrameNumber);

	static ENGINE_API void SetPresentLatencyMarkerStart(uint64 FrameNumber);
	static ENGINE_API void SetPresentLatencyMarkerEnd(uint64 FrameNumber);

	static ENGINE_API void SetRenderSubmitLatencyMarkerStart(uint64 FrameNumber);
	static ENGINE_API void SetRenderSubmitLatencyMarkerEnd(uint64 FrameNumber);

	static ENGINE_API void SetFlashIndicatorLatencyMarker(uint64 FrameNumber);

	/**
	 * Allows games to correct the negative delta
	 *
	 * @return new delta
	 */
	ENGINE_API virtual double CorrectNegativeTimeDelta(double DeltaRealTime);

	/** Causes the current custom time step to be shut down and then reinitialized. */
	ENGINE_API void ReinitializeCustomTimeStep();

	/**
	 * Set the custom time step that will control the Engine Framerate/Timestep.
	 * It will shutdown the previous custom time step.
	 * The new custom time step will be initialized.
	 *
	 * @return	the result of the custom time step initialization.
	 */
	ENGINE_API bool SetCustomTimeStep(UEngineCustomTimeStep* InCustomTimeStep);

	/** Get the custom time step that control the Engine Framerate/Timestep */
	UEngineCustomTimeStep* GetCustomTimeStep() const { return CustomTimeStep; };

	/** Return custom time step changed event. */
	FSimpleMulticastDelegate& OnCustomTimeStepChanged() { return CustomTimeStepChangedEvent; }

	/** Executes the deferred commands */
	ENGINE_API void TickDeferredCommands();

	/** Get tick rate limiter. */
	ENGINE_API virtual float GetMaxTickRate(float DeltaTime, bool bAllowFrameRateSmoothing = true) const;

	/** Get max fps. */
	ENGINE_API virtual float GetMaxFPS() const;

	/** Set max fps. Overrides console variable. */
	ENGINE_API virtual void SetMaxFPS(const float MaxFPS);

	/** Updates the running average delta time */
	ENGINE_API virtual void UpdateRunningAverageDeltaTime(float DeltaTime, bool bAllowFrameRateSmoothing = true);

	/** Whether we're allowed to do frame rate smoothing */
	ENGINE_API virtual bool IsAllowedFramerateSmoothing() const;

	/** Whether the application should avoid rendering anything to give GPU resources to other applications */
	virtual bool IsRenderingSuspended() const { return false; }

	/** Update FApp::Timecode. */
	ENGINE_API void UpdateTimecode();

	/** Causes the current timecode provider to be shut down and then reinitialized. */
	ENGINE_API void ReinitializeTimecodeProvider();

	/**
	 * Set the timecode provider that will control the Engine's timecode.
	 * It will shutdown the previous timecode provider.
	 * The new timecode provider will be initialized.
	 *
	 * @return	the result value of the new timecode provider initialization.
	 */
	ENGINE_API bool SetTimecodeProvider(UTimecodeProvider* InTimecodeProvider);

	/** Get the TimecodeProvider that control the Engine's Timecode. */
	UTimecodeProvider* GetTimecodeProvider() const { return TimecodeProvider; };

	/** Return timecode provider changed event. */
	FSimpleMulticastDelegate& OnTimecodeProviderChanged() { return TimecodeProviderChangedEvent; }

public:

	/**
	 * Pauses / un-pauses the game-play when focus of the game's window gets lost / gained.
	 * @param EnablePause true to pause; false to unpause the game
	 */
	ENGINE_API virtual void OnLostFocusPause( bool EnablePause );

	/** 
	 * Returns the average game/render/gpu/total time since this function was last called
	 */
	ENGINE_API void GetAverageUnitTimes( TArray<float>& AverageTimes );

	/**
	 * Updates the values used to calculate the average game/render/gpu/total time
	 */
	ENGINE_API void SetAverageUnitTimes(float FrameTime, float RenderThreadTime, float GameThreadTime, float GPUFrameTime, float RHITFrameTime);

	/**
	 * Returns the display color for a given frame time (based on t.TargetFrameTimeThreshold and t.UnacceptableFrameTimeThreshold)
	 */
	ENGINE_API FColor GetFrameTimeDisplayColor(float FrameTimeMS) const;

	/**
	 * @return true to throttle CPU usage based on current state (usually editor minimized or not in foreground)
	 */
	ENGINE_API virtual bool ShouldThrottleCPUUsage() const;

	/**
	 * @return true if all windows are minimized or hidden (Per OS definition)
	 */
	ENGINE_API bool AreAllWindowsHidden() const;

public:
	/** 
	 * Return a reference to the GamePlayers array. 
	 */

	ENGINE_API TArray<class ULocalPlayer*>::TConstIterator	GetLocalPlayerIterator(UWorld *World);
	ENGINE_API TArray<class ULocalPlayer*>::TConstIterator GetLocalPlayerIterator(const UGameViewportClient *Viewport);

	ENGINE_API const TArray<class ULocalPlayer*>& GetGamePlayers(UWorld *World) const;
	ENGINE_API const TArray<class ULocalPlayer*>& GetGamePlayers(const UGameViewportClient *Viewport) const;

	/**
	 *	Returns the first ULocalPlayer that matches the given ControllerId. 
	 *  This will search across all world contexts.
	 */
	ENGINE_API ULocalPlayer* FindFirstLocalPlayerFromControllerId(int32 ControllerId) const;

	/** 
	 * If true, we're running in a backward compatible mode where FPlatformUserId and ControllerId are the same.
	 * If false, there can be more than one local player with the same platform user id
	 */
	virtual bool IsControllerIdUsingPlatformUserId() const { return true; }

	/**
	 * Returns the first ULocalPlayer that matches the given platform user id, or the first player if the id is invalid
	 * This will search across all world contexts.
	 */
	ENGINE_API ULocalPlayer* FindFirstLocalPlayerFromPlatformUserId(FPlatformUserId PlatformUserId) const;

	/**
	 * Returns the first LocalPlayer that matches the given platform user id
	 *
	 * @param	PlatformUserId	Platform user id to search for
	 * @return	The player that has the PlatformUserId specified, or nullptr if no players have that PlatformUserId
	 */
	ENGINE_API ULocalPlayer* GetLocalPlayerFromPlatformUserId(UWorld* InWorld, const FPlatformUserId PlatformUserId) const;

	/**
	 * return the number of entries in the GamePlayers array
	 */
	ENGINE_API int32 GetNumGamePlayers(UWorld *InWorld);
	ENGINE_API int32 GetNumGamePlayers(const UGameViewportClient *InViewport);

	/**
	 * return the ULocalPlayer with the given index.
	 *
	 * @param	InPlayer		Index of the player required
	 *
	 * @returns	pointer to the LocalPlayer with the given index
	 */
	ENGINE_API ULocalPlayer* GetGamePlayer( UWorld * InWorld, int32 InPlayer );
	ENGINE_API ULocalPlayer* GetGamePlayer( const UGameViewportClient* InViewport, int32 InPlayer );
	
	/**
	 * return the first ULocalPlayer in the GamePlayers array.
	 *
	 * @returns	first ULocalPlayer or nullptr if the array is empty
	 */
	ENGINE_API ULocalPlayer* GetFirstGamePlayer( UWorld *InWorld );
	ENGINE_API ULocalPlayer* GetFirstGamePlayer(const UGameViewportClient *InViewport );
	ENGINE_API ULocalPlayer* GetFirstGamePlayer( UPendingNetGame *PendingNetGame );

	/**
	 * returns the first ULocalPlayer that should be used for debug purposes.
	 * This should only be used in very special cases where no UWorld* is available!
	 * Anything using this will not function properly under multiple worlds.
	 * Always prefer to use GetFirstGamePlayer() or even better - FLocalPlayerIterator
	 *
	 * @returns the first ULocalPlayer
	 */
	ENGINE_API ULocalPlayer* GetDebugLocalPlayer();

	/** Clean up the GameViewport */
	ENGINE_API void CleanupGameViewport();

	/** Allows the editor to accept or reject the drawing of wire frame brush shapes based on mode and tool. */
	virtual bool ShouldDrawBrushWireframe( class AActor* InActor ) { return true; }

	/** Returns whether or not the map build in progressed was canceled by the user. */
	virtual bool GetMapBuildCancelled() const
	{
		return false;
	}

	/**
	 * Sets the flag that states whether or not the map build was canceled.
	 *
	 * @param InCancelled	New state for the canceled flag.
	 */
	virtual void SetMapBuildCancelled( bool InCancelled )
	{
		// Intentionally empty.
	}

	/** Uses StatColorMappings to find a color for this stat's value. */
	ENGINE_API bool GetStatValueColoration(const FString& StatName, float Value, FColor& OutColor);

	/** @return true if selection of translucent objects in perspective view ports is allowed */
	virtual bool AllowSelectTranslucent() const
	{
		// The editor may override this to disallow translucent selection based on user preferences
		return true;
	}

	/** @return true if only editor-visible levels should be loaded in Play-In-Editor sessions */
	virtual bool OnlyLoadEditorVisibleLevelsInPIE() const
	{
		// The editor may override this to apply the user's preference state
		return true;
	}

	/** 
	 * Computes the amount of time in seconds that should be used for unified asset/level streaming for this frame.
	 * This is only used if UseUnifiedTimeBudgetForStreaming is enabled, and can be overridden by a game engine.
	 * @param DeltaSeconds		How long the last frame tick took
	 * @param bHighPriority		True if this is considered a high priority load such as during a loading screen
	 */
	ENGINE_API virtual double GetUnifiedTimeBudgetForStreaming(float DeltaSeconds, bool bHighPriority);

	/**
	 * Called once per frame to execute unified async asset and level streaming using the time budget from GetUnifiedTimeBudgetForStreaming.
	 * This is only used if UseUnifiedTimeBudgetForStreaming is enabled, and can be overridden by a game engine.
	 */
	ENGINE_API virtual void HandleUnifiedStreaming(float DeltaSeconds);

	/**
	 * @return true if level streaming should prefer to stream levels from disk instead of duplicating them from editor world
	 */
	virtual bool PreferToStreamLevelsInPIE() const
	{
		return false;
	}

	/**
	 * Enables or disables the ScreenSaver (PC only)
	 *
	 * @param bEnable	If true the enable the screen saver, if false disable it.
	 */
	ENGINE_API void EnableScreenSaver( bool bEnable );
	
	/**
	 * Get the index of the provided sprite category
	 *
	 * @param	InSpriteCategory	Sprite category to get the index of
	 *
	 * @return	Index of the provided sprite category, if possible; INDEX_NONE otherwise
	 */
	virtual int32 GetSpriteCategoryIndex( const FName& InSpriteCategory )
	{
		// The editor may override this to handle sprite categories as necessary
		return INDEX_NONE;
	}

	static ENGINE_API void PreGarbageCollect();

	/**
	 *  Collect garbage once per frame driven by World ticks
	 */
	ENGINE_API void ConditionalCollectGarbage();

	/**
	 *  Interface to allow WorldSettings to request immediate garbage collection
	 */
	ENGINE_API void PerformGarbageCollectionAndCleanupActors();

	/** Updates the timer between garbage collection such that at the next opportunity garbage collection will be run. */
	ENGINE_API void ForceGarbageCollection(bool bFullPurge = false);

	/**
	 *  Requests a one frame delay of Garbage Collection
	 */
	ENGINE_API void DelayGarbageCollection();

	/**
	 * Updates the timer (as a one-off) that is used to trigger garbage collection; this should only be used for things
	 * like performance tests, using it recklessly can dramatically increase memory usage and cost of the eventual GC.
	 *
	 * Note: Things that force a GC will still force a GC after using this method (and they will also reset the timer)
	 */
	ENGINE_API void SetTimeUntilNextGarbageCollection(float MinTimeUntilNextPass);

	/**
	 * Returns the current desired time between garbage collection passes (not the time remaining)
	 */
	ENGINE_API float GetTimeBetweenGarbageCollectionPasses() const;

#if !UE_BUILD_SHIPPING
	/** 
	 * Capture screenshots and performance metrics
	 * @param EventTime time of the Sequencer event
	 */
	ENGINE_API void PerformanceCapture(UWorld* World, const FString& MapName, const FString& SequenceName, float EventTime);

	/**
	 * Logs performance capture for use in automation analytics
	 * @param EventTime time of the Sequencer event
	 */
	ENGINE_API void LogPerformanceCapture(UWorld* World, const FString& MapName, const FString& SequenceName, float EventTime);
#endif	// UE_BUILD_SHIPPING

	/**
	 * Starts the FPS chart data capture (if another run is already active then this command is ignored except to change the active label).
	 *
	 * @param	Label		Label for this run
	 * @param	bRecordPerFrameTimes	Should we record per-frame times (potentially unbounded memory growth; used when triggered via the console but not when triggered by game code)
	 */
	ENGINE_API virtual void StartFPSChart(const FString& Label, bool bRecordPerFrameTimes);

	/**
	 * Stops the FPS chart data capture (if no run is active then this command is ignored).
	 */
	ENGINE_API virtual void StopFPSChart(const FString& MapName);

	/**
	* Attempts to reclaim any idle memory by performing a garbage collection and broadcasting FCoreDelegates::OnMemoryTrim. Pending rendering commands are first flushed. This is called
	* between level loads and may be called at other times, but is expensive and should be used sparingly. Do
	*/
	static ENGINE_API void TrimMemory();

	/**
	 * Calculates information about the previous frame and passes it to all active performance data consumers.
	 *
	 * @param DeltaSeconds	Time in seconds passed since last tick.
	 */
	ENGINE_API void TickPerformanceMonitoring(float DeltaSeconds);

	/** Register a performance data consumer with the engine; it will be passed performance information each frame */
	ENGINE_API void AddPerformanceDataConsumer(TSharedPtr<IPerformanceDataConsumer> Consumer);

	/** Remove a previously registered performance data consumer */
	ENGINE_API void RemovePerformanceDataConsumer(TSharedPtr<IPerformanceDataConsumer> Consumer);

public:
	/** Delegate called when FPS charting detects a hitch (it is not triggered if there are no active performance data consumers). */
	FEngineHitchDetectedDelegate OnHitchDetectedDelegate;

private:

	/**
	 * Callback for external UI being opened.
	 *
	 * @param bInIsOpening			true if the UI is opening, false if it is being closed.
	*/
	ENGINE_API void OnExternalUIChange(bool bInIsOpening);

protected:

	/** Returns GetTimeBetweenGarbageCollectionPasses but tweaked if its an idle server or not */
	ENGINE_API virtual float GetTimeBetweenGarbageCollectionPasses(bool bHasPlayersConnected) const;

	/**
	 * Handles freezing/unfreezing of rendering 
	 * 
	 * @param InWorld	World context
	 */
	virtual void ProcessToggleFreezeCommand( UWorld* InWorld )
	{
		// Intentionally empty.
	}

	/** Handles frezing/unfreezing of streaming */
	 virtual void ProcessToggleFreezeStreamingCommand(UWorld* InWorld)
	 {
		// Intentionally empty.
	 }

	 /**
	 * Requests that the engine intentionally performs an invalid operation. Used for testing error handling
	 * and external crash reporters
	 *
	 * @param Cmd			Error to perform. See implementation for options
	 */
	 ENGINE_API bool PerformError(const TCHAR* Cmd, FOutputDevice& Out = *GLog);

	 /**
	  * Dispatches EndOfFrameUpdates for all UWorlds
	  */
	 static ENGINE_API void SendWorldEndOfFrameUpdates();

	/**
	 * Allows derived classes to force garbage collection based on various factors (low on available UObject slots / other resources)
	 */
	ENGINE_API virtual EGarbageCollectionType ShouldForceGarbageCollection();

	/**
	 * Allows derived classes to set per-frame GC budget depending on various factors.
	 * Default implementation uses gc.IncrementalGCTimePerFrame and gc.LowMemory.IncrementalGCTimePerFrame
	 * depending on current memory usage and value of gc.LowMemory.MemoryThresholdMB
	 */
	ENGINE_API virtual float GetIncrementalGCTimePerFrame();

public:
	/** @return the GIsEditor flag setting */
	ENGINE_API bool IsEditor();

	/** @return the audio device manager of the UEngine, this allows the creation and management of multiple audio devices. */
	ENGINE_API FAudioDeviceManager* GetAudioDeviceManager();

	/** @return the main audio device handle used by the engine. */
	ENGINE_API uint32 GetMainAudioDeviceID() const;

	/** @return the main audio device. */
	ENGINE_API FAudioDeviceHandle GetMainAudioDevice();
	ENGINE_API class FAudioDevice* GetMainAudioDeviceRaw();

	/** @return the currently active audio device */
	ENGINE_API FAudioDeviceHandle GetActiveAudioDevice();

	/** @returns whether there are currently multiple local players in the given world */
	ENGINE_API virtual bool HasMultipleLocalPlayers(UWorld* InWorld);

	/** @return whether we're currently running with stereoscopic 3D enabled for the specified viewport (or globally, if viewport is nullptr) */
	ENGINE_API bool IsStereoscopic3D(const FViewport* InViewport = nullptr) const;

	/**
	 * Adds a world location as a secondary view location for purposes of texture streaming.
	 * Lasts one frame, or a specified number of seconds (for overriding locations only).
	 *
	 * @param InLoc					Location to add to texture streaming for this frame
	 * @param BoostFactor			A factor that affects all streaming distances for this location. 1.0f is default. Higher means higher-resolution textures and vice versa.
	 * @param bOverrideLocation		Whether this is an override location, which forces the streaming system to ignore all other locations
	 * @param OverrideDuration		How long the streaming system should keep checking this location if bOverrideLocation is true, in seconds. 0 means just for the next Tick.
	 */
	ENGINE_API void AddTextureStreamingLoc(FVector InLoc, float BoostFactor, bool bOverrideLocation, float OverrideDuration);

	/** 
	 * Obtain a world object pointer from an object with has a world context.
	 *
	 * @param Object		Object whose owning world we require.
	 * @param ErrorMode		Controls what happens if the Object cannot be found
	 * @return				The world to which the object belongs or nullptr if it cannot be found.
	 */
	ENGINE_API UWorld* GetWorldFromContextObject(const UObject* Object, EGetWorldErrorMode ErrorMode) const;

	/** 
	 * Obtain a world object pointer from an object with has a world context.
	 *
	 * @param Object		Object whose owning world we require.
	 * @return				The world to which the object belongs; asserts if the world cannot be found!
	 */
	UWorld* GetWorldFromContextObjectChecked(const UObject* Object) const
	{
		return GetWorldFromContextObject(Object, EGetWorldErrorMode::Assert);
	}

	/** 
	 * mostly done to check if PIE is being set up, go GWorld is going to change, and it's not really _the_G_World_
	 * NOTE: hope this goes away once PIE and regular game triggering are not that separate code paths
	 */
	virtual bool IsSettingUpPlayWorld() const { return false; }

	/**
	 * Retrieves the LocalPlayer for the player which has the ControllerId specified
	 *
	 * @param	ControllerId	the game pad index of the player to search for
	 * @return	The player that has the ControllerId specified, or nullptr if no players have that ControllerId
	 */
	ENGINE_API ULocalPlayer* GetLocalPlayerFromControllerId( const UGameViewportClient* InViewport, const int32 ControllerId ) const;
	ENGINE_API ULocalPlayer* GetLocalPlayerFromControllerId( UWorld * InWorld, const int32 ControllerId ) const;

	ENGINE_API ULocalPlayer* GetLocalPlayerFromInputDevice(const UGameViewportClient* InViewport, const FInputDeviceId InputDevice) const;
	ENGINE_API ULocalPlayer* GetLocalPlayerFromInputDevice(UWorld * InWorld, const FInputDeviceId InputDevice) const;

	ENGINE_API void SwapControllerId(ULocalPlayer *NewPlayer, const int32 CurrentControllerId, const int32 NewControllerID) const;
	
	ENGINE_API void SwapPlatformUserId(ULocalPlayer *NewPlayer, const FPlatformUserId CurrentUserId, const FPlatformUserId NewUserID) const;

	/** 
	 * Find a Local Player Controller, which may not exist at all if this is a server.
	 * @return first found LocalPlayerController. Fine for single player, in split screen, one will be picked. 
	 */
	ENGINE_API class APlayerController* GetFirstLocalPlayerController(const UWorld* InWorld);

	/** Gets all local players associated with the engine. 
	 *	This function should only be used in rare cases where no UWorld* is available to get a player list associated with the world.
	 *  E.g, - use GetFirstLocalPlayerController(UWorld *InWorld) when possible!
	 */
	ENGINE_API void GetAllLocalPlayerControllers(TArray<APlayerController*>& PlayerList);

	/** Returns the GameViewport widget */
	virtual TSharedPtr<class SViewport> GetGameViewportWidget() const
	{
		return nullptr;
	}

	/** Returns the current display gamma value */
	float GetDisplayGamma() const { return DisplayGamma; }

	virtual void FocusNextPIEWorld(UWorld *CurrentPieWorld, bool previous=false) { }

	virtual void ResetPIEAudioSetting(UWorld *CurrentPieWorld) {}

	virtual class UGameViewportClient* GetNextPIEViewport(UGameViewportClient * CurrentViewport) { return nullptr; }

	virtual void RemapGamepadControllerIdForPIE(class UGameViewportClient* InGameViewport, int32 &ControllerId) { }

	/**
	 * Get a locator for Portal services.
	 *
	 * @return The service locator.
	 */
	TSharedRef<IPortalServiceLocator> GetServiceLocator()
	{
		return ServiceLocator.ToSharedRef();
	}

protected:

	/** Portal RPC client. */
	TSharedPtr<IMessageRpcClient> PortalRpcClient;

	/** Portal RPC server locator. */
	TSharedPtr<IPortalRpcLocator> PortalRpcLocator;

	/** Holds a type container for service dependencies. */
	TSharedPtr<FTypeContainer> ServiceDependencies;

	/** Holds registered service instances. */
	TSharedPtr<IPortalServiceLocator> ServiceLocator;

	/** Active FPS chart (initialized by startfpschart, finalized by stopfpschart) */
	TSharedPtr<FPerformanceTrackingChart> ActivePerformanceChart;

#if ALLOW_DEBUG_FILES
	/** Active fine-grained per-frame chart (initialized by startfpschart, finalized by stopfpschart) */
	TSharedPtr<FFineGrainedPerformanceTracker> ActiveFrameTimesChart;
#endif

	/** List of all active performance consumers */
	TArray<TSharedPtr<IPerformanceDataConsumer>> ActivePerformanceDataConsumers;

public:

	/**
	 * Gets the engine's default tiny font.
	 *
	 * @return Tiny font.
	 */
	static ENGINE_API class UFont* GetTinyFont();

	/**
	 * Gets the engine's default small font
	 *
	 * @return Small font.
	 */
	static ENGINE_API class UFont* GetSmallFont();

	/**
	 * Gets the engine's default medium font.
	 *
	 * @return Medium font.
	 */
	static ENGINE_API class UFont* GetMediumFont();

	/**
	 * Gets the engine's default large font.
	 *
	 * @return Large font.
	 */
	static ENGINE_API class UFont* GetLargeFont();

	/**
	 * Gets the engine's default subtitle font.
	 *
	 * @return Subtitle font.
	 */
	static ENGINE_API class UFont* GetSubtitleFont();

	/**
	 * Gets the specified additional font.
	 *
	 * @param AdditionalFontIndex - Index into the AddtionalFonts array.
	 */
	static ENGINE_API class UFont* GetAdditionalFont(int32 AdditionalFontIndex);

	/** Makes a strong effort to copy everything possible from and old object to a new object of a different class, used for blueprint to update things after a recompile. */
	struct FCopyPropertiesForUnrelatedObjectsParams
	{
		UE_DEPRECATED(5.1, "Aggressive Default Subobject Replacement is no longer being done. An ensure has been left in place to catch any cases that was making use of this feature.")
		bool bAggressiveDefaultSubobjectReplacement;
		bool bDoDelta;
		bool bReplaceObjectClassReferences;
		bool bCopyDeprecatedProperties;
		bool bPreserveRootComponent;
		bool bPerformDuplication;
		bool bOnlyHandleDirectSubObjects;

		/** Skips copying properties with BlueprintCompilerGeneratedDefaults metadata */
		bool bSkipCompilerGeneratedDefaults;
		bool bNotifyObjectReplacement;
		bool bClearReferences;
		UE_DEPRECATED(5.4, "This isn't used anymore by the code.")
		bool bDontClearReferenceIfNewerClassExists;
		bool bReplaceInternalReferenceUponRead; // While reading back object ptr, immediately replace them if they are in the replacement map.

		// In cases where the SourceObject will no longer be able to look up its correct Archetype, it can be supplied
		UObject* SourceObjectArchetype;
		TMap<UObject*, UObject*>* OptionalReplacementMappings;
		const TMap<UClass*, UClass*>* OptionalOldToNewClassMappings; // Will be used along with the bReplaceInternalReferenceUponRead;

		ENGINE_API FCopyPropertiesForUnrelatedObjectsParams();
		ENGINE_API FCopyPropertiesForUnrelatedObjectsParams(const FCopyPropertiesForUnrelatedObjectsParams&);
	};
	static ENGINE_API void CopyPropertiesForUnrelatedObjects(UObject* OldObject, UObject* NewObject, FCopyPropertiesForUnrelatedObjectsParams Params = FCopyPropertiesForUnrelatedObjectsParams());
	virtual void NotifyToolsOfObjectReplacement(const TMap<UObject*, UObject*>& OldToNewInstanceMap) { }

	ENGINE_API virtual bool UseSound() const;

	// This should only ever be called for a EditorEngine
	virtual UWorld* CreatePIEWorldByDuplication(FWorldContext &Context, UWorld* InWorld, FString &PlayWorldMapName) { check(false); return nullptr; }
	virtual void PostCreatePIEWorld(UWorld* InWorld) { check(false); }

	/** 
	 *	If this function returns true, the DynamicSourceLevels collection will be duplicated for the given map.
	 *	This is necessary to do outside of the editor when we don't have the original editor world, and it's 
	 *	not safe to copy the dynamic levels once they've been fully initialized, so we pre-duplicate them when the original levels are first created.
	 *	If you implement this, enable s.World.CreateStaticLevelCollection to stop it from duplicating static streaming levels.
	 */
	virtual bool Experimental_ShouldPreDuplicateMap(const FName MapName) const { return false; }

protected:

	/**
	 *	Initialize the audio device manager
	 */
	ENGINE_API virtual void InitializeAudioDeviceManager();

	/**
	 *	Detects and initializes any attached HMD devices
	 *
	 *	@return true if there is an initialized device, false otherwise
	 */
	ENGINE_API virtual bool InitializeHMDDevice();

	/**
	 *	Detects and initializes any attached eye-tracking devices
	 *
	 *	@return true if there is an initialized device, false otherwise
	 */
	ENGINE_API virtual bool InitializeEyeTrackingDevice();

	/**	Record EngineAnalytics information for attached HMD devices. */
	ENGINE_API virtual void RecordHMDAnalytics();

	/** Loads all Engine object references from their corresponding config entries. */
	ENGINE_API virtual void InitializeObjectReferences();

	/** Initialize Portal services. */
	ENGINE_API virtual void InitializePortalServices();

	/** Initializes the running average delta to some good initial framerate. */
	ENGINE_API virtual void InitializeRunningAverageDeltaTime();

	float RunningAverageDeltaTime;

	/** Broadcasts when a world is added. */
	FWorldAddedEvent			WorldAddedEvent;

	/** Broadcasts when a world is destroyed. */
	FWorldDestroyedEvent		WorldDestroyedEvent;
private:

#if WITH_EDITOR

	/** Broadcasts whenever a world's actor list changes in a way not specifiable through other LevelActor__Events */
	FLevelActorListChangedEvent LevelActorListChangedEvent;

	/** Broadcasts whenever an actor is added. */
	FLevelActorAddedEvent LevelActorAddedEvent;

	/** Broadcasts whenever an actor is removed. */
	FLevelActorDeletedEvent LevelActorDeletedEvent;

	/** Broadcasts whenever an actor folder is added. */
	FActorFolderAddedEvent ActorFolderAddedEvent;

	/** Broadcasts whenever an actor folder is removed. */
	FActorFolderRemovedEvent ActorFolderRemovedEvent;

	/** Broadcasts whenever a level rebuilds its actor folder list. */
	FActorFoldersUpdatedEvent ActorFoldersUpdatedEvent;

	/** Broadcasts whenever an actor's outer changes */
	FLevelActorOuterChangedEvent LevelActorOuterChangedEvent;

	/** Broadcasts whenever an actor is attached. */
	FLevelActorAttachedEvent LevelActorAttachedEvent;

	/** Broadcasts whenever an actor is detached. */
	FLevelActorDetachedEvent LevelActorDetachedEvent;

	/** Broadcasts whenever an actor's folder has changed. */
	FLevelActorFolderChangedEvent LevelActorFolderChangedEvent;

	/** Broadcasts whenever an actor is being renamed */
	FLevelActorRequestRenameEvent LevelActorRequestRenameEvent;

	/** Broadcasts whenever a component is being renamed */
	FLevelComponentRequestRenameEvent LevelComponentRequestRenameEvent;

	/** Broadcasts when an actor is being moved, rotated or scaled */
	FOnActorMovingEvent	OnActorMovingEvent;

	/** Broadcasts after an actor has been moved, rotated or scaled */
	FOnActorMovedEvent	OnActorMovedEvent;

	/** Broadcast when a group of actors have been moved, rotated, or scaled */
	FOnActorsMovedEvent OnActorsMovedEvent;

	/** Broadcasts after a component has been moved, rotated or scaled */
	FOnComponentTransformChangedEvent OnComponentTransformChangedEvent;
	
	/** Delegate broadcast after UEditorEngine::Tick has been called (or UGameEngine::Tick in standalone) */
	FPostEditorTick PostEditorTickEvent;

	/** Delegate broadcast when the editor is closing */
	FEditorCloseEvent EditorCloseEvent;

#endif // #if WITH_EDITOR

	/** Thread preventing screen saver from kicking. Suspend most of the time. */
	FRunnableThread*		ScreenSaverInhibitor;
	FScreenSaverInhibitor*  ScreenSaverInhibitorRunnable;


	/** Increments every time a non-seamless travel happens on a server, to generate net session id's. Written to config to preserve id upon crash. */
	UPROPERTY(Config)
	uint32 GlobalNetTravelCount = 0;

public:
	void IncrementGlobalNetTravelCount()
	{
		GlobalNetTravelCount++;
	}

	uint32 GetGlobalNetTravelCount() const
	{
		return GlobalNetTravelCount;
	}
public:

	/** A list of named UNetDriver definitions */
	UPROPERTY(Config, transient)
	TArray<FNetDriverDefinition> NetDriverDefinitions;

	/** A list of Iris NetDriverConfigs */
	UPROPERTY(Config, transient)
	TArray<FIrisNetDriverConfig> IrisNetDriverConfigs;

	/** 
	 * Returns the Iris config for the corresponding NetDriver 
	 * Priority order for the IrisNetDriverConfigs are:
	 *		1. NetDriverName exact match
	 *		2. NetDriverName wildcard match
	 *		3. NetDriverDefinition match
	 */
	ENGINE_API const FIrisNetDriverConfig* GetIrisNetDriverConfig(FName InNetDriverDefinition, FName InNetDriverName) const;

	/** Returns true if the netdriver will run with Iris enable. */
	ENGINE_API bool WillNetDriverUseIris(const FWorldContext& Context, FName InNetDriverDefinition, FName InNetDriverName) const;
	
	/** A configurable list of actors that are automatically spawned upon server startup (just prior to InitGame) */
	UPROPERTY(config)
	TArray<FString> ServerActors;

	/** Runtime-modified list of server actors, allowing plugins to use serveractors, without permanently adding them to config files */
	UPROPERTY()
	TArray<FString> RuntimeServerActors;

	/** Amount of time in seconds between network error logging */
	UPROPERTY(globalconfig)
	float NetErrorLogInterval;

	/** Spawns all of the registered server actors */
	ENGINE_API virtual void SpawnServerActors(UWorld *World);

	/**
	 * Notification of network error messages, allows the engine to handle the failure
	 *
	 * @param	World associated with failure
	 * @param	NetDriver associated with failure
	 * @param	FailureType	the type of error
	 * @param	ErrorString	additional string detailing the error
	 */
	ENGINE_API virtual void HandleNetworkFailure(UWorld *World, UNetDriver *NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);

	/**
	 * Notification of server travel error messages, generally network connection related (package verification, client server handshaking, etc) 
	 * allows the engine to handle the failure
	 *
	 * @param   InWorld     the world we were in when the travel failure occurred
	 * @param	FailureType	the type of error
	 * @param	ErrorString	additional string detailing the error
	 */
	ENGINE_API virtual void HandleTravelFailure(UWorld* InWorld, ETravelFailure::Type FailureType, const FString& ErrorString);

	
	/**
	 * Notification of network lag state change messages.
	 *
	 * @param	World associated with the lag
	 * @param	NetDriver associated with the lag
	 * @param	LagType	Whether we started lagging or we are no longer lagging
	 */
	ENGINE_API virtual void HandleNetworkLagStateChanged(UWorld* World, UNetDriver* NetDriver, ENetworkLagState::Type LagType);

	/**
	 * Shutdown any relevant net drivers
	 */
	ENGINE_API void ShutdownWorldNetDriver(UWorld*);

	ENGINE_API void ShutdownAllNetDrivers();

	/**
	 * Finds a UNetDriver based on its name.
	 *
	 * @param NetDriverName The name associated with the driver to find.
	 *
	 * @return A pointer to the UNetDriver that was found, or nullptr if it wasn't found.
	 */
	ENGINE_API UNetDriver* FindNamedNetDriver(const UWorld* InWorld, FName NetDriverName);
	ENGINE_API UNetDriver* FindNamedNetDriver(const UPendingNetGame* InPendingNetGame, FName NetDriverName);

	/**
	 * Returns the current netmode
	 * @param 	NetDriverName    Name of the net driver to get mode for
	 * @return current netmode
	 *
	 * Note: if there is no valid net driver, returns NM_StandAlone
	 */
	//virtual ENetMode GetNetMode(FName NetDriverName = NAME_GameNetDriver) const;
	ENGINE_API ENetMode GetNetMode(const UWorld *World) const;

	/**
	 * Creates a UNetDriver with an engine assigned name
	 *
	 * @param InWorld the world context
	 * @param NetDriverDefinition The name of the definition to use
	 *
	 * @return new netdriver if successful, nullptr otherwise
	 */
	ENGINE_API UNetDriver* CreateNetDriver(UWorld *InWorld, FName NetDriverDefinition);

	/**
	 * Creates a UNetDriver and associates a name with it.
	 *
	 * @param InWorld the world context
	 * @param NetDriverName The name to associate with the driver.
	 * @param NetDriverDefinition The name of the definition to use
	 *
	 * @return True if the driver was created successfully, false if there was an error.
	 */
	ENGINE_API bool CreateNamedNetDriver(UWorld *InWorld, FName NetDriverName, FName NetDriverDefinition);

	/**
	 * Creates a UNetDriver and associates a name with it.
	 *
	 * @param PendingNetGame the pending net game context
	 * @param NetDriverName The name to associate with the driver.
	 * @param NetDriverDefinition The name of the definition to use
	 *
	 * @return True if the driver was created successfully, false if there was an error.
	 */
	ENGINE_API bool CreateNamedNetDriver(UPendingNetGame *PendingNetGame, FName NetDriverName, FName NetDriverDefinition);
	
	/**
	 * Destroys a UNetDriver based on its name.
	 *
	 * @param NetDriverName The name associated with the driver to destroy.
	 */
	ENGINE_API void DestroyNamedNetDriver(UWorld *InWorld, FName NetDriverName);
	ENGINE_API void DestroyNamedNetDriver(UPendingNetGame *PendingNetGame, FName NetDriverName);

	virtual bool NetworkRemapPath(UNetConnection* Connection, FString& Str, bool bReading=true) { return false; }
	virtual bool NetworkRemapPath(UPendingNetGame *PendingNetGame, FString &Str, bool bReading=true) { return false; }

	ENGINE_API virtual bool HandleOpenCommand( const TCHAR* Cmd, FOutputDevice& Ar, UWorld * InWorld );

	ENGINE_API virtual bool HandleTravelCommand( const TCHAR* Cmd, FOutputDevice& Ar, UWorld* InWorld );
	
	ENGINE_API virtual bool HandleStreamMapCommand( const TCHAR* Cmd, FOutputDevice& Ar, UWorld *InWorld );

#if WITH_SERVER_CODE
	ENGINE_API virtual bool HandleServerTravelCommand( const TCHAR* Cmd, FOutputDevice& Ar, UWorld* InWorld );
#endif

	ENGINE_API virtual bool HandleDisconnectCommand( const TCHAR* Cmd, FOutputDevice& Ar, UWorld *InWorld );

	ENGINE_API virtual bool HandleReconnectCommand( const TCHAR* Cmd, FOutputDevice& Ar, UWorld *InWorld );

	
	/**
	 * The proper way to disconnect a given World and NetDriver. Travels world if necessary, cleans up pending connects if necessary.
	 *	
	 * @param InWorld	The world being disconnected (might be nullptr in case of pending net dupl)
	 * @param NetDriver The net driver being disconnect (will be InWorld's net driver if there is a world)
	 *	
	 */
	ENGINE_API void HandleDisconnect( UWorld *InWorld, UNetDriver *NetDriver );

	/**
	 * Makes sure map name is a long package name.
	 *
	 * @param InOutMapName Map name. In non-final builds code will attempt to convert to long package name if short name is provided.
	 * @param true if the map name was valid, false otherwise.
	 */
	ENGINE_API bool MakeSureMapNameIsValid(FString& InOutMapName);

	ENGINE_API void SetClientTravel( UWorld *InWorld, const TCHAR* NextURL, ETravelType InTravelType );

	ENGINE_API void SetClientTravel( UPendingNetGame *PendingNetGame, const TCHAR* NextURL, ETravelType InTravelType );

	ENGINE_API void SetClientTravelFromPendingGameNetDriver( UNetDriver *PendingGameNetDriverGame, const TCHAR* NextURL, ETravelType InTravelType );

	/** Browse to a specified URL, relative to the current one. */
	ENGINE_API virtual EBrowseReturnVal::Type Browse( FWorldContext& WorldContext, FURL URL, FString& Error );

	ENGINE_API virtual void TickWorldTravel(FWorldContext& WorldContext, float DeltaSeconds);

	ENGINE_API void BrowseToDefaultMap( FWorldContext& WorldContext );

	ENGINE_API virtual bool LoadMap( FWorldContext& WorldContext, FURL URL, class UPendingNetGame* Pending, FString& Error );

	virtual void RedrawViewports( bool bShouldPresent = true ) { }

	virtual void TriggerStreamingDataRebuild() { }

	/**
	 * Updates level streaming state using active game players view and blocks until all sub-levels are loaded/ visible/ hidden
	 * so further calls to UpdateLevelStreaming won't do any work unless state changes.
	 *
	 * @param InWorld Target world
	 */
	ENGINE_API void BlockTillLevelStreamingCompleted(UWorld* InWorld);

	/**
	 * true if the loading movie was started during LoadMap().
	 */
	UPROPERTY(transient)
	uint32 bStartedLoadMapMovie:1;

	/**
	 * Removes the PerMapPackages from the RootSet
	 *
	 * @param FullyLoadType When to load the packages (based on map, GameMode, etc)
	 * @param Tag Name of the map/game to cleanup packages for
	 */
	ENGINE_API void CleanupPackagesToFullyLoad(FWorldContext &Context, EFullyLoadPackageType FullyLoadType, const FString& Tag);

	/**
	 * Called to allow overloading by child engines
	 */
	virtual void LoadMapRedrawViewports(void)
	{
		RedrawViewports(false);
	}

	ENGINE_API void ClearDebugDisplayProperties();

	/**
	 * Loads the PerMapPackages for the given map, and adds them to the RootSet
	 *
	 * @param FullyLoadType When to load the packages (based on map, GameMode, etc)
	 * @param Tag Name of the map/game to load packages for
	 */
	ENGINE_API void LoadPackagesFully(UWorld * InWorld, EFullyLoadPackageType FullyLoadType, const FString& Tag);

	ENGINE_API void UpdateTransitionType(UWorld *CurrentWorld);

	ENGINE_API UPendingNetGame* PendingNetGameFromWorld( UWorld* InWorld );

	/** Cancel pending level. */
	ENGINE_API virtual void CancelAllPending();

	ENGINE_API virtual void CancelPending(UWorld *InWorld, UPendingNetGame *NewPendingNetGame=nullptr );

	ENGINE_API virtual bool WorldIsPIEInNewViewport(UWorld *InWorld);

	ENGINE_API FWorldContext* GetWorldContextFromWorld(const UWorld* InWorld);
	ENGINE_API FWorldContext* GetWorldContextFromGameViewport(const UGameViewportClient *InViewport);
	ENGINE_API FWorldContext* GetWorldContextFromPendingNetGame(const UPendingNetGame *InPendingNetGame);	
	ENGINE_API FWorldContext* GetWorldContextFromPendingNetGameNetDriver(const UNetDriver *InPendingNetGame);	
	ENGINE_API FWorldContext* GetWorldContextFromHandle(const FName WorldContextHandle);
	ENGINE_API FWorldContext* GetWorldContextFromPIEInstance(const int32 PIEInstance);

	ENGINE_API const FWorldContext* GetWorldContextFromWorld(const UWorld* InWorld) const;
	ENGINE_API const FWorldContext* GetWorldContextFromGameViewport(const UGameViewportClient *InViewport) const;
	ENGINE_API const FWorldContext* GetWorldContextFromPendingNetGame(const UPendingNetGame *InPendingNetGame) const;	
	ENGINE_API const FWorldContext* GetWorldContextFromPendingNetGameNetDriver(const UNetDriver *InPendingNetGame) const;	
	ENGINE_API const FWorldContext* GetWorldContextFromHandle(const FName WorldContextHandle) const;
	ENGINE_API const FWorldContext* GetWorldContextFromPIEInstance(const int32 PIEInstance) const;

	ENGINE_API FWorldContext& GetWorldContextFromWorldChecked(const UWorld * InWorld);
	ENGINE_API FWorldContext& GetWorldContextFromGameViewportChecked(const UGameViewportClient *InViewport);
	ENGINE_API FWorldContext& GetWorldContextFromPendingNetGameChecked(const UPendingNetGame *InPendingNetGame);	
	ENGINE_API FWorldContext& GetWorldContextFromPendingNetGameNetDriverChecked(const UNetDriver *InPendingNetGame);	
	ENGINE_API FWorldContext& GetWorldContextFromHandleChecked(const FName WorldContextHandle);
	ENGINE_API FWorldContext& GetWorldContextFromPIEInstanceChecked(const int32 PIEInstance);

	ENGINE_API const FWorldContext& GetWorldContextFromWorldChecked(const UWorld * InWorld) const;
	ENGINE_API const FWorldContext& GetWorldContextFromGameViewportChecked(const UGameViewportClient *InViewport) const;
	ENGINE_API const FWorldContext& GetWorldContextFromPendingNetGameChecked(const UPendingNetGame *InPendingNetGame) const;	
	ENGINE_API const FWorldContext& GetWorldContextFromPendingNetGameNetDriverChecked(const UNetDriver *InPendingNetGame) const;	
	ENGINE_API const FWorldContext& GetWorldContextFromHandleChecked(const FName WorldContextHandle) const;
	ENGINE_API const FWorldContext& GetWorldContextFromPIEInstanceChecked(const int32 PIEInstance) const;

	const TIndirectArray<FWorldContext>& GetWorldContexts() const { return WorldList;	}

	/** 
	 * Tries to find the currently active primary Game or Play in Editor world, returning null if it is ambiguous.
	 * This should only be called if you do not have a reliable world context object to use.
	 *
	 * @param PossiblePlayWorld If set, this will be checked first and returned if valid. If this is not the active play world, null will be returned due to ambiguity
	 * @return either nullptr or a World that is guaranteed to be of type Game or PIE
	 */
	ENGINE_API UWorld* GetCurrentPlayWorld(UWorld* PossiblePlayWorld = nullptr) const;

	/**
	 * Finds any World(s) and related objects that are still referenced after being destroyed by ::LoadMap and logs which objects are holding the references.
	 * May rename packages for the dangling objects to allow the world to be reloaded without conflicting with the existing one.
	 * @param InWorldContext The optional world context for which we want to check references to additional "must-destroy" objects.
	 */
	ENGINE_API virtual void CheckAndHandleStaleWorldObjectReferences(FWorldContext* InWorldContext = nullptr);

	/**
	 * Attempts to find what is referencing a world that should have been garbage collected
	 * @param ObjectToFindReferencesTo World or its package (or any object from the world package that should've been destroyed)
	 * @param Verbosity Verbosity (can be fatal or non-fatal) with which to print the error message with
	 */
	UE_DEPRECATED(5.1, "Please use FReferenceChainSearch::FindAndPrintStaleReferencesToObject")
	static ENGINE_API void FindAndPrintStaleReferencesToObject(UObject* ObjectToFindReferencesTo, ELogVerbosity::Type Verbosity);

	/**
	 * Attempts to find a reference chain leading to a world that should have been garbage collected
	 * @param ObjectToFindReferencesTo World or its package (or any object from the world package that should've been destroyed)
	 * @param Options Determines how the stale references messages should be logged
	 */
	UE_DEPRECATED(5.3, "Please use FReferenceChainSearch::FindAndPrintStaleReferencesToObject")
	static ENGINE_API FString FindAndPrintStaleReferencesToObject(UObject* ObjectToFindReferencesTo, EPrintStaleReferencesOptions Options);
	UE_DEPRECATED(5.3, "Please use FReferenceChainSearch::FindAndPrintStaleReferencesToObjects")
	static ENGINE_API TArray<FString> FindAndPrintStaleReferencesToObjects(TConstArrayView<UObject*> ObjectsToFindReferencesTo, EPrintStaleReferencesOptions Options);

	ENGINE_API FWorldContext& CreateNewWorldContext(EWorldType::Type WorldType);

	ENGINE_API virtual void DestroyWorldContext(UWorld * InWorld);

	/** Triggered when a world context is destroyed. */
	DECLARE_EVENT_OneParam(UEngine, FWorldContextDestroyedEvent, FWorldContext&);

	/** Return the world context destroyed event. */
	FWorldContextDestroyedEvent&	OnWorldContextDestroyed() { return WorldContextDestroyedEvent; }

private:
	/** Delegate broadcast when a world context is destroyed */
	FWorldContextDestroyedEvent WorldContextDestroyedEvent;

public:
	/** 
	 * Called from GetFunctionCallspace on specific objects to check for authority/cosmetic function tags using global state
	 *
	 * @param	Function		Function to check for network mode flags like AuthorityOnly, cannot be null
	 * @param	FunctionTarget	Object this function will be called on, if not null this may be used to determine context
	 * @param	Stack			Function call stack, if not null this may be used to determine context
	 */
	ENGINE_API int32 GetGlobalFunctionCallspace(UFunction* Function, UObject* FunctionTarget, FFrame* Stack);

	/** 
	 * Returns true if the global context is client-only and authority only events should always be ignored. 
	 * This will return false if it is unknown, use GetCurrentPlayWorld if you have a possible world.
	 */
	ENGINE_API bool ShouldAbsorbAuthorityOnlyEvent();

	/** 
	 * Returns true if the global context is dedicated server and cosmetic only events should always be ignored. 
	 * This will return false if it is unknown, use GetCurrentPlayWorld if you have a possible world.
	 */
	ENGINE_API bool ShouldAbsorbCosmeticOnlyEvent();

	ENGINE_API UGameViewportClient* GameViewportForWorld(const UWorld *InWorld) const;

	/** @return true if editor analytics are enabled */
	virtual bool AreEditorAnalyticsEnabled() const { return false; }
	virtual void CreateStartupAnalyticsAttributes( TArray<struct FAnalyticsEventAttribute>& StartSessionAttributes ) const {}
	
	/** @return true if the engine is autosaving a package (default is to only check for transient auto-saves for legacy reasons) */
	virtual bool IsAutosaving(const EPackageAutoSaveType AutoSaveType = EPackageAutoSaveType::Transient) const { return false; }

	virtual bool ShouldDoAsyncEndOfFrameTasks() const { return false; }

	bool IsVanillaProduct() const { return bIsVanillaProduct; }

protected:
	ENGINE_API void SetIsVanillaProduct(bool bInIsVanillaProduct);

private:
	bool bIsVanillaProduct;

protected:
	/**
	 * Delegate for overriding the method Browse in the part that parses an URL
	 * and loads specified level or creates PendingNetGame.
	 * Parameter are the same as those passed to the calling method Browse
	 */
	FBrowseURL OnOverrideBrowseURL;

	/**
	 * Delegate for overriding the method TickWorldTravel in the part that
	 * controls the state of PendingNetGame
	 * Parameter are the same as those passed to the calling method Browse
	 */
	FPendingLevelUpdate OnOverridePendingNetGameUpdate;

	TIndirectArray<FWorldContext>	WorldList;

	UPROPERTY()
	int32	NextWorldContextHandle;


	ENGINE_API virtual void CancelPending(FWorldContext& WorldContext);

	ENGINE_API virtual void CancelPending(UNetDriver* PendingNetGameDriver);

	ENGINE_API virtual void MovePendingLevel(FWorldContext &Context);

	/**
	 *	Returns true if BROWSE should shuts down the current network driver.
	 **/
	virtual bool ShouldShutdownWorldNetDriver()
	{
		return true;
	}

	ENGINE_API bool WorldHasValidContext(UWorld *InWorld);

	/**
	 * Attempts to gracefully handle a failure to travel to the default map.
	 *
	 * @param Error the error string result from the LoadMap call that attempted to load the default map.
	 */
	ENGINE_API virtual void HandleBrowseToDefaultMapFailure(FWorldContext& Context, const FString& TextURL, const FString& Error);

	/**
	 * Helper function that returns true if InWorld is the outer of a level in a collection of type DynamicDuplicatedLevels.
	 * For internal engine use.
	 */
	ENGINE_API bool IsWorldDuplicate(const UWorld* const InWorld);

protected:

	// Async map change/ persistent level transition code.

	/**
	 * Finalizes the pending map change that was being kicked off by PrepareMapChange.
	 *
	 * @param InCurrentWorld	the world context
	 * @return	true if successful, false if there were errors (use GetMapChangeFailureDescription 
	 *			for error description)
	 */
	ENGINE_API bool CommitMapChange( FWorldContext &Context);

	/**
	 * Returns whether the prepared map change is ready for commit having called.
	 *
	 * @return true if we're ready to commit the map change, false otherwise
	 */
	ENGINE_API bool IsReadyForMapChange(FWorldContext &Context);

	/**
	 * Returns whether we are currently preparing for a map change or not.
	 *
	 * @return true if we are preparing for a map change, false otherwise
	 */
	ENGINE_API bool IsPreparingMapChange(FWorldContext &Context);

	/**
	 * Prepares the engine for a map change by pre-loading level packages in the background.
	 *
	 * @param	LevelNames	Array of levels to load in the background; the first level in this
	 *						list is assumed to be the new "persistent" one.
	 *
	 * @return	true if all packages were in the package file cache and the operation succeeded,
	 *			false otherwise. false as a return value also indicates that the code has given
	 *			up.
	 */
	ENGINE_API bool PrepareMapChange(FWorldContext &WorldContext, const TArray<FName>& LevelNames);

	/**
	 * Returns the failure description in case of a failed map change request.
	 *
	 * @return	Human readable failure description in case of failure, empty string otherwise
	 */
	ENGINE_API FString GetMapChangeFailureDescription(FWorldContext &Context);

	/** Commit map change if requested and map change is pending. Called every frame.	 */
	ENGINE_API void ConditionalCommitMapChange(FWorldContext &WorldContext);

	/** Cancels pending map change.	 */
	ENGINE_API void CancelPendingMapChange(FWorldContext &Context);

public:

	//~ Begin Public Interface for async map change functions

	bool CommitMapChange(UWorld* InWorld) { return CommitMapChange(GetWorldContextFromWorldChecked(InWorld)); }
	bool IsReadyForMapChange(UWorld* InWorld) { return IsReadyForMapChange(GetWorldContextFromWorldChecked(InWorld)); }
	bool IsPreparingMapChange(UWorld* InWorld) { return IsPreparingMapChange(GetWorldContextFromWorldChecked(InWorld)); }
	bool PrepareMapChange(UWorld* InWorld, const TArray<FName>& LevelNames) { return PrepareMapChange(GetWorldContextFromWorldChecked(InWorld), LevelNames); }
	void ConditionalCommitMapChange(UWorld* InWorld) { return ConditionalCommitMapChange(GetWorldContextFromWorldChecked(InWorld)); }

	FString GetMapChangeFailureDescription(UWorld *InWorld) { return GetMapChangeFailureDescription(GetWorldContextFromWorldChecked(InWorld)); }

	/** Cancels pending map change.	 */
	void CancelPendingMapChange(UWorld *InWorld) { return CancelPendingMapChange(GetWorldContextFromWorldChecked(InWorld)); }

	ENGINE_API void AddNewPendingStreamingLevel(UWorld *InWorld, FName PackageName, bool bNewShouldBeLoaded, bool bNewShouldBeVisible, int32 LODIndex);

	ENGINE_API bool ShouldCommitPendingMapChange(const UWorld *InWorld) const;
	ENGINE_API void SetShouldCommitPendingMapChange(UWorld *InWorld, bool NewShouldCommitPendingMapChange);

	ENGINE_API FSeamlessTravelHandler&	SeamlessTravelHandlerForWorld(UWorld *World);

	ENGINE_API FURL & LastURLFromWorld(UWorld *World);

	/**
	 * Returns the global instance of the game user settings class.
	 */
	ENGINE_API const UGameUserSettings* GetGameUserSettings() const;
	ENGINE_API UGameUserSettings* GetGameUserSettings();

private:
	ENGINE_API void CreateGameUserSettings();

	/** Allows subclasses to pass the failure to a UGameInstance if possible (mainly for blueprints) */
	ENGINE_API virtual void HandleNetworkFailure_NotifyGameInstance(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType);

	/** Allows subclasses to pass the failure to a UGameInstance if possible (mainly for blueprints) */
	ENGINE_API virtual void HandleTravelFailure_NotifyGameInstance(UWorld* World, ETravelFailure::Type FailureType);

public:
#if WITH_EDITOR
	//~ Begin Transaction Interfaces.
	virtual int32 BeginTransaction(const TCHAR* TransactionContext, const FText& Description, UObject* PrimaryObject) { return INDEX_NONE; }
	virtual int32 EndTransaction() { return INDEX_NONE; }
	virtual bool CanTransact() { return false; }
	virtual void CancelTransaction(int32 Index) { }
#endif

public:
	/**
	 * Get an Engine Subsystem of specified type
	 */
	UEngineSubsystem* GetEngineSubsystemBase(TSubclassOf<UEngineSubsystem> SubsystemClass) const
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		checkSlow(IsThisNotNull(this, "UEngine::GetEditorSubsystemBase"));
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		return EngineSubsystemCollection.GetSubsystem<UEngineSubsystem>(SubsystemClass);
	}

	/**
	 * Get an Engine Subsystem of specified type
	 */
	template <typename TSubsystemClass>
	TSubsystemClass* GetEngineSubsystem() const
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		checkSlow(IsThisNotNull(this, "UEngine::GetEditorSubsystem"));
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		return EngineSubsystemCollection.GetSubsystem<TSubsystemClass>(TSubsystemClass::StaticClass());
	}

	/**
	 * Get all Engine Subsystem of specified type, this is only necessary for interfaces that can have multiple implementations instanced at a time.
	 */
	template <typename TSubsystemClass>
	UE_DEPRECATED(5.4, "This function is unsafe for re-entrancy and has been deprecated. Use ForEachEngineSubsystem or GetEngineSubsystemArrayCopy instead")
	const TArray<TSubsystemClass*>& GetEngineSubsystemArray() const
	{
		return EngineSubsystemCollection.GetSubsystemArray<TSubsystemClass>(TSubsystemClass::StaticClass());
	}

	/**
	 * Get all Subsystem of specified type, this is only necessary for interfaces that can have multiple implementations instanced at a time.
	 *
	 * Do not hold onto this Array reference unless you are sure the lifetime is less than that of UGameInstance
	 */
	template <typename TSubsystemClass>
	TArray<TSubsystemClass*> GetEngineSubsystemArrayCopy() const
	{
		return EngineSubsystemCollection.GetSubsystemArrayCopy<TSubsystemClass>(TSubsystemClass::StaticClass());
	}

	/**
	 * Performs an operation on all all Subsystem of specified type, this is only necessary for interfaces that can have multiple implementations instanced at a time.
	 */
	template <typename TSubsystemClass>
	void ForEachEngineSubsystem(TFunctionRef<void(TSubsystemClass*)> Operation) const
	{
		static_assert(TIsDerivedFrom<TSubsystemClass, UEngineSubsystem>::IsDerived, "TSubsystemClass must be derived from UEngineSubsystem");
		return EngineSubsystemCollection.ForEachSubsystem([Operation=MoveTemp(Operation)](UEngineSubsystem* Subsystem){
			Operation(CastChecked<TSubsystemClass>(Subsystem));
		}, TSubsystemClass::StaticClass());
	}

protected:
	FObjectSubsystemCollection<UEngineSubsystem> EngineSubsystemCollection;

public:
	/**
	 * Delegate we fire every time a new stat has been registered.
	 *
	 * @param FName The name of the new stat.
	 * @param FName The category of the new stat.
	 * @param FText The description of the new stat.
	 */
	DECLARE_EVENT_ThreeParams(UEngine, FOnNewStatRegistered, const FName&, const FName&, const FText&);
	static ENGINE_API FOnNewStatRegistered NewStatDelegate;
	
	/**
	 * Wrapper for firing a simple stat exec.
	 *
	 * @param World	The world to apply the exec to.
	 * @param ViewportClient The viewport to apply the exec to.
	 * @param InName The exec string.
	 */
	ENGINE_API void ExecEngineStat(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* InName);

	/**
	 * Check to see if the specified stat name is a simple stat.
	 *
	 * @param InName The name of the stat we're checking.
	 * @returns true if the stat is a registered simple stat.
	 */
	ENGINE_API bool IsEngineStat(const FString& InName);

	/**
	 * Set the state of the specified stat.
	 *
	 * @param World	The world to apply the exec to.
	 * @param ViewportClient The viewport to apply the exec to.
	 * @param InName The stat name.
	 * @param bShow The state we would like the stat to be in.
	 */
	ENGINE_API void SetEngineStat(UWorld* World, FCommonViewportClient* ViewportClient, const FString& InName, const bool bShow);

	/**
	 * Set the state of the specified stats (note: array processed in reverse order when !bShow).
	 *
	 * @param World	The world to apply the exec to.
	 * @param ViewportClient The viewport to apply the exec to.
	 * @param InNames The stat names.
	 * @param bShow The state we would like the stat to be in.
	 */
	ENGINE_API void SetEngineStats(UWorld* World, FCommonViewportClient* ViewportClient, const TArray<FString>& InNames, const bool bShow);

	/**
	 * Function to render all the simple stats
	 *
	 * @param World	The world being drawn to.
	 * @param ViewportClient The viewport being drawn to.
	 * @param Canvas The canvas to use when drawing.
	 * @param LHSX The left hand side X position to start drawing from.
	 * @param InOutLHSY The left hand side Y position to start drawing from.
	 * @param RHSX The right hand side X position to start drawing from.
	 * @param InOutRHSY The right hand side Y position to start drawing from.
	 * @param ViewLocation The world space view location.
	 * @param ViewRotation The world space view rotation.
	 */
	ENGINE_API void RenderEngineStats(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 LHSX, int32& InOutLHSY, int32 RHSX, int32& InOutRHSY, const FVector* ViewLocation, const FRotator* ViewRotation);

	/**
	 * Function to render text indicating whether named events are enabled.
	 *
	 * @param Canvas The canvas to use when drawing.
	 * @param X The X position to start drawing from.
	 * @param Y The Y position to start drawing from.
	 * @return The ending Y position to continue rendering stats at.
	 */
	ENGINE_API int32 RenderNamedEventsEnabled(FCanvas* Canvas, int32 X, int32 Y);

	/**
	 * Function definition for those stats which have their own toggle functions (or toggle other stats).
	 *
	 * @param World	The world being drawn to.
	 * @param ViewportClient The viewport being drawn to.
	 * @param Stream The remaining characters from the Exec call.
	 */
	 //typedef bool (UEngine::* EngineStatToggle)(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream);
	DECLARE_DELEGATE_RetVal_ThreeParams(bool, FEngineStatToggle, UWorld* /*World*/, FCommonViewportClient* /*ViewportClient*/, const TCHAR* /*Stream*/)

	/**
	 * Function definition for those stats which have their own render functions (or affect another render functions).
	 *
	 * @param World	The world being drawn to.
	 * @param ViewportClient The viewport being drawn to.
	 * @param Canvas The canvas to use when drawing.
	 * @param X The X position to draw to.
	 * @param Y The Y position to draw to.
	 * @param ViewLocation The world space view location.
	 * @param ViewRotation The world space view rotation.
	 */
	//typedef int32(UEngine::* EngineStatRender)(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation);
	DECLARE_DELEGATE_RetVal_SevenParams(int32, FEngineStatRender, UWorld* /*World*/, FViewport* /*Viewport*/, FCanvas* /*Canvas*/, int32 /*X*/, int32 /*Y*/, const FVector* /*ViewLocation*/, const FRotator* /*ViewRotation*/)

	/** Allows external systems to add a new simple engine stat function. 
	*/
	ENGINE_API void AddEngineStat(const FName& InCommandName, const FName& InCategoryName, const FText& InDescriptionString, FEngineStatRender InRenderFunc = nullptr, FEngineStatToggle InToggleFunc = nullptr, const bool bInIsRHS = false);

	ENGINE_API void RemoveEngineStat(const FName& InCommandName);
private:

	/** Struct for keeping track off all the info regarding a specific simple stat exec */
	struct FEngineStatFuncs
	{
		/** The name of the command, e.g. STAT FPS would just have FPS as it's CommandName */
		FName CommandName;

		/** A string version of CommandName without STAT_ at the beginning. Cached for optimization. */
		FString CommandNameString;

		/** The category the command falls into (only used by UI) */
		FName CategoryName;

		/** The description of what this command does (only used by UI) */
		FText DescriptionString;

		/** The function needed to render the stat when it's enabled 
		 *  Note: This is only called when it should be rendered */
		FEngineStatRender RenderFunc;

		/** The function we call after the stat has been toggled 
		 *  Note: This is only needed if you need to do something else depending on the state of the stat */
		FEngineStatToggle ToggleFunc;

		/** If true, this stat should render on the right side of the viewport, otherwise left */
		bool bIsRHS;

		/** Constructor */
		FEngineStatFuncs(const FName& InCommandName, const FName& InCategoryName, const FText& InDescriptionString, FEngineStatRender InRenderFunc, FEngineStatToggle InToggleFunc, const bool bInIsRHS = false)
			: CommandName(InCommandName)
			, CommandNameString(InCommandName.ToString())
			, CategoryName(InCategoryName)
			, DescriptionString(InDescriptionString)
			, RenderFunc(InRenderFunc)
			, ToggleFunc(InToggleFunc)
			, bIsRHS(bInIsRHS)
		{
			CommandNameString.RemoveFromStart(TEXT("STAT_"));
		}
	};

	/** A list of all the simple stats functions that have been registered */
	TArray<FEngineStatFuncs> EngineStats;

	// Helper struct that registers itself with the output redirector and copies off warnings
	// and errors that we'll overlay on the client viewport
	struct FErrorsAndWarningsCollector;
	TPimplPtr<FErrorsAndWarningsCollector> ErrorsAndWarningsCollector;

private:

	/**
	 * Functions for performing other actions when the stat is toggled, should only be used when registering with EngineStats.
	 *
	 * @param World	The world being drawn to.
	 * @param ViewportClient The viewport being drawn to.
	 * @param Stream The remaining characters from the Exec call (optional).
	 */
	ENGINE_API bool ToggleStatFPS(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream = nullptr);
	ENGINE_API bool ToggleStatDetailed(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream = nullptr);
	ENGINE_API bool ToggleStatHitches(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream = nullptr);
	ENGINE_API bool ToggleStatNamedEvents(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream = nullptr);
	ENGINE_API bool ToggleStatVerboseNamedEvents(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream = nullptr);
	ENGINE_API bool ToggleStatUnit(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream = nullptr);
#if !UE_BUILD_SHIPPING
	ENGINE_API bool PostStatSoundModulatorHelp(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream = nullptr);
	ENGINE_API bool ToggleStatUnitCriticalPath(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream = nullptr);
	ENGINE_API bool ToggleStatUnitMax(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream = nullptr);
	ENGINE_API bool ToggleStatUnitGraph(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream = nullptr);
	ENGINE_API bool ToggleStatUnitTime(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream = nullptr);
	ENGINE_API bool ToggleStatRaw(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream = nullptr);
	ENGINE_API bool ToggleStatParticlePerf(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream = nullptr);
	ENGINE_API bool ToggleStatTSR(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream = nullptr);
#endif

	/**
	 * Functions for rendering the various simple stats, should only be used when registering with EngineStats.
	 *
	 * @param World	The world being drawn to.
	 * @param ViewportClient The viewport being drawn to.
	 * @param Canvas The canvas to use when drawing.
	 * @param X The X position to draw to.
	 * @param Y The Y position to draw to.
	 * @param ViewLocation The world space view location.
	 * @param ViewRotation The world space view rotation.
	 */
#if !UE_BUILD_SHIPPING
	ENGINE_API int32 RenderStatVersion(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation = nullptr, const FRotator* ViewRotation = nullptr);
#endif // !UE_BUILD_SHIPPING
	ENGINE_API int32 RenderStatFPS(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation = nullptr, const FRotator* ViewRotation = nullptr);
	ENGINE_API int32 RenderStatHitches(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation = nullptr, const FRotator* ViewRotation = nullptr);
	ENGINE_API int32 RenderStatSummary(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation = nullptr, const FRotator* ViewRotation = nullptr);
	ENGINE_API int32 RenderStatColorList(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation = nullptr, const FRotator* ViewRotation = nullptr);
	ENGINE_API int32 RenderStatLevels(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation = nullptr, const FRotator* ViewRotation = nullptr);
	ENGINE_API int32 RenderStatLevelMap(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation = nullptr, const FRotator* ViewRotation = nullptr);
	ENGINE_API int32 RenderStatUnit(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation = nullptr, const FRotator* ViewRotation = nullptr);
	ENGINE_API int32 RenderStatDrawCount(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation = nullptr, const FRotator* ViewRotation = nullptr);
#if !UE_BUILD_SHIPPING
	ENGINE_API int32 RenderStatSoundReverb(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation = nullptr, const FRotator* ViewRotation = nullptr);
 	ENGINE_API int32 RenderStatSoundMixes(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation = nullptr, const FRotator* ViewRotation = nullptr);
	ENGINE_API int32 RenderStatSoundModulators(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation = nullptr, const FRotator* ViewRotation = nullptr);
	ENGINE_API int32 RenderStatSoundWaves(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation = nullptr, const FRotator* ViewRotation = nullptr);
	ENGINE_API int32 RenderStatAudioStreaming(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation = nullptr, const FRotator* ViewRotation = nullptr);
	ENGINE_API int32 RenderStatSoundCues(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation = nullptr, const FRotator* ViewRotation = nullptr);
	ENGINE_API int32 RenderStatSounds(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation = nullptr, const FRotator* ViewRotation = nullptr);
	ENGINE_API int32 RenderStatParticlePerf(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation = nullptr, const FRotator* ViewRotation = nullptr);
#endif // !UE_BUILD_SHIPPING
	ENGINE_API int32 RenderStatAI(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation = nullptr, const FRotator* ViewRotation = nullptr);
	ENGINE_API int32 RenderStatTimecode(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation = nullptr, const FRotator* ViewRotation = nullptr);
	ENGINE_API int32 RenderStatFrameCounter(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation = nullptr, const FRotator* ViewRotation = nullptr);
#if STATS
	ENGINE_API int32 RenderStatSlateBatches(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation = nullptr, const FRotator* ViewRotation = nullptr);
#endif

	FDelegateHandle HandleScreenshotCapturedDelegateHandle;

public:
	/** Set priority and affinity on game thread either from ini file or from FPlatformAffinity::GetGameThreadPriority()*/
	ENGINE_API void SetPriorityAndAffinityOnGameThread();
};

/** Global engine pointer. Can be 0 so don't use without checking. */
extern ENGINE_API class UEngine*			GEngine;
