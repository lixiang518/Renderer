// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetDataTagMap.h"
#include "Async/Async.h"
#include "AutoRTFM.h"
#include "HAL/ThreadSafeCounter.h"

struct FAssetRegistrySerializationOptions;

namespace FixedTagPrivate
{
	// Legacy version of FAssetRegistryExportPath (before FAssetRegistryVersion::ClassPaths)
	struct FLegacyAssetRegistryExportPath
	{
		FName Class;
		FName Package;
		FName Object;
	};

	/**
	 * The AssetRegistry's representation of an FText AssetData Tag value.
	 * It can be stored and copied without being interpreted as an FText.
	 */
	class FMarshalledText
	{
	public:
		FMarshalledText() = default;
		COREUOBJECT_API explicit FMarshalledText(const FUtf8String& InString);
		COREUOBJECT_API explicit FMarshalledText(const FString& InString);
		COREUOBJECT_API explicit FMarshalledText(FUtf8String&& InString);
		COREUOBJECT_API explicit FMarshalledText(const FText& InText);
		COREUOBJECT_API explicit FMarshalledText(FText&& InText);

		COREUOBJECT_API const FUtf8String& GetAsComplexString() const;
		COREUOBJECT_API FText GetAsText() const;
		COREUOBJECT_API int32 CompareToCaseIgnored(const FMarshalledText& Other) const;
		COREUOBJECT_API int64 GetResourceSize() const;

	private:
		FUtf8String String;
	};

	/// Stores a fixed set of values and all the key-values maps used for lookup
	struct FStore
	{
		// Pairs for all unsorted maps that uses this store 
		TArrayView<FNumberedPair> Pairs;
		TArrayView<FNumberlessPair> NumberlessPairs;

		// Values for all maps in this store
		TArrayView<uint32> AnsiStringOffsets;
		TArrayView<ANSICHAR> AnsiStrings;
		TArrayView<uint32> WideStringOffsets;
		TArrayView<WIDECHAR> WideStrings;
		TArrayView<FDisplayNameEntryId> NumberlessNames;
		TArrayView<FName> Names;
		TArrayView<FNumberlessExportPath> NumberlessExportPaths;
		TArrayView<FAssetRegistryExportPath> ExportPaths;
		TArrayView<FMarshalledText> Texts;

		const uint32 Index;
		void* Data = nullptr;

		void AddRef() const
		{
			UE_AUTORTFM_OPEN
			{
				RefCount.Increment();
			};

			UE_AUTORTFM_ONABORT(this)
			{
				RefCount.Decrement();
			};
		}

		COREUOBJECT_API void Release() const;
		
		const ANSICHAR* GetAnsiString(uint32 Idx) const { return &AnsiStrings[AnsiStringOffsets[Idx]]; }
		const WIDECHAR* GetWideString(uint32 Idx) const { return &WideStrings[WideStringOffsets[Idx]]; }

	private:
		friend class FStoreManager;

		explicit FStore(uint32 InIndex) : Index(InIndex)
		{
			checkf(!AutoRTFM::IsClosed() || !AutoRTFM::IsOnCurrentTransactionStack(this), TEXT("Not allowed to construct a stack local within a transaction."));
		}

		~FStore();

		mutable FThreadSafeCounter RefCount;
	};

	struct FOptions
	{
		TSet<FName> StoreAsName;
		TSet<FName> StoreAsPath;
	};

	// Incomplete handle to a map in an unspecified FStore.
	// Used for serialization where the store index is implicit.
	struct COREUOBJECT_API alignas(uint64) FPartialMapHandle
	{
		bool bHasNumberlessKeys = false;
		uint16 Num = 0;
		uint32 PairBegin = 0;

		FMapHandle MakeFullHandle(uint32 StoreIndex) const;
		uint64 ToInt() const;
		static FPartialMapHandle FromInt(uint64 Int);
	};

	// Note: Can be changed to a single allocation and array views to improve cooker performance
	struct FStoreData
	{
		TArray<FNumberedPair> Pairs;
		TArray<FNumberlessPair> NumberlessPairs;

		TArray<uint32> AnsiStringOffsets;
		TArray<ANSICHAR> AnsiStrings;
		TArray<uint32> WideStringOffsets;
		TArray<WIDECHAR> WideStrings;
		TArray<FDisplayNameEntryId> NumberlessNames;
		TArray<FName> Names;
		TArray<FNumberlessExportPath> NumberlessExportPaths;
		TArray<FAssetRegistryExportPath> ExportPaths;
		TArray<FMarshalledText> Texts;
	};

	uint32 HashCaseSensitive(const TCHAR* Str, int32 Len);
	uint32 HashCaseSensitive(const UTF8CHAR* Str, int32 Len);
	uint32 HashCombineQuick(uint32 A, uint32 B);
	uint32 HashCombineQuick(uint32 A, uint32 B, uint32 C);

	// Helper class for saving or constructing an FStore
	class FStoreBuilder
	{
	public:
		explicit FStoreBuilder(const FOptions& InOptions) : Options(InOptions) {}
		explicit FStoreBuilder(FOptions&& InOptions) : Options(MoveTemp(InOptions)) {}

		COREUOBJECT_API FPartialMapHandle AddTagMap(const FAssetDataTagMapSharedView& Map);

		// Call once after all tag maps have been added
		COREUOBJECT_API FStoreData Finalize();

	private:

		template <typename ValueType>
		struct FCaseSensitiveFuncs : BaseKeyFuncs<ValueType, FString, /*bInAllowDuplicateKeys*/ false>
		{
			template<typename KeyType>
			static const KeyType& GetSetKey(const TPair<KeyType, ValueType>& Element)
			{
				return Element.Key;
			}

			static bool Matches(const FString& A, const FString& B)
			{
				return A.Equals(B, ESearchCase::CaseSensitive);
			}
			static bool Matches(const FUtf8String& A, const FUtf8String& B)
			{
				return A.Equals(B, ESearchCase::CaseSensitive);
			}
			static uint32 GetKeyHash(const FString& Key)
			{
				return HashCaseSensitive(&Key[0], Key.Len());
			}
			static uint32 GetKeyHash(const FUtf8String& Key)
			{
				return HashCaseSensitive(&Key[0], Key.Len());
			}

			static bool Matches(FNameEntryId A, FNameEntryId B)
			{
				return A == B;
			}
			static uint32 GetKeyHash(FNameEntryId Key)
			{
				return GetTypeHash(Key);
			}

			static bool Matches(FName A, FName B)
			{
				return (A.GetDisplayIndex() == B.GetDisplayIndex()) & (A.GetNumber() == B.GetNumber());
			}
			static uint32 GetKeyHash(FName Key)
			{
				return HashCombineQuick(GetTypeHash(Key.GetDisplayIndex()), Key.GetNumber());
			}

			static bool Matches(const FNumberlessExportPath& A, const FNumberlessExportPath& B)
			{
				return Matches(A.ClassPackage, B.ClassPackage) & Matches(A.ClassObject, B.ClassObject) & Matches(A.Package, B.Package) & Matches(A.Object, B.Object); //-V792
			}
			static bool Matches(const FAssetRegistryExportPath& A, const FAssetRegistryExportPath& B)
			{
				return Matches(A.ClassPath.GetPackageName(), B.ClassPath.GetPackageName()) &  Matches(A.ClassPath.GetAssetName(), B.ClassPath.GetAssetName()) & Matches(A.Package, B.Package) & Matches(A.Object, B.Object); //-V792
			}

			static uint32 GetKeyHash(const FNumberlessExportPath& Key)
			{
				return HashCombineQuick(HashCombineQuick(GetKeyHash(Key.ClassPackage), GetKeyHash(Key.ClassObject)), GetKeyHash(Key.Package), GetKeyHash(Key.Object));
			}
			static uint32 GetKeyHash(const FAssetRegistryExportPath& Key)
			{
				return HashCombineQuick(HashCombineQuick(GetKeyHash(Key.ClassPath.GetPackageName()), GetKeyHash(Key.ClassPath.GetAssetName())), GetKeyHash(Key.Package), GetKeyHash(Key.Object));
			}

			static bool Matches(const FMarshalledText& A, const FMarshalledText& B)
			{
				return Matches(A.GetAsComplexString(), B.GetAsComplexString());
			}

			static uint32 GetKeyHash(const FMarshalledText& Key)
			{
				return GetKeyHash(Key.GetAsComplexString());
			}
		};

		struct FStringIndexer
		{
			uint32 NumCharacters = 0;
			TMap<FString, uint32, FDefaultSetAllocator, FCaseSensitiveFuncs<uint32>> StringIndices;
			TArray<uint32> Offsets;

			uint32 Index(FString&& String);

			TArray<ANSICHAR> FlattenAsAnsi() const;
			TArray<WIDECHAR> FlattenAsWide() const;
		};

		const FOptions Options;
		FStringIndexer AnsiStrings;
		FStringIndexer WideStrings;
		TMap<FDisplayNameEntryId, uint32> NumberlessNameIndices;
		TMap<FName, uint32, FDefaultSetAllocator, FCaseSensitiveFuncs<uint32>> NameIndices;
		TMap<FNumberlessExportPath, uint32, FDefaultSetAllocator, FCaseSensitiveFuncs<uint32>> NumberlessExportPathIndices;
		TMap<FAssetRegistryExportPath, uint32, FDefaultSetAllocator, FCaseSensitiveFuncs<uint32>> ExportPathIndices;
		TMap<FMarshalledText, uint32, FDefaultSetAllocator, FCaseSensitiveFuncs<uint32>> TextIndices;

		TArray<FNumberedPair> NumberedPairs;
		TArray<FNumberedPair> NumberlessPairs; // Stored as numbered for convenience

		bool bFinalized = false;

		FValueId IndexValue(FName Key, FAssetTagValueRef Value);
	};

	enum class ELoadOrder { Member, TextFirst };

	COREUOBJECT_API void SaveStore(const FStoreData& Store, FArchive& Ar);
	COREUOBJECT_API TRefCountPtr<const FStore> LoadStore(FArchive& Ar, FAssetRegistryVersion::Type Version = FAssetRegistryVersion::LatestVersion);

	/// Loads tag store with async creation of expensive tag values
	///
	/// Caller should:
	/// * Call ReadInitialDataAndKickLoad()
	/// * Call LoadFinalData()
	/// * Wait for future before resolving stored tag values
	class FAsyncStoreLoader
	{
	public:
		COREUOBJECT_API FAsyncStoreLoader();

		/// 1) Read initial data and kick expensive tag value creation task
		///
		/// Won't load FNames to allow concurrent name batch loading
		/// 
		/// @return handle to step 3
		COREUOBJECT_API TFuture<void> ReadInitialDataAndKickLoad(FArchive& Ar, uint32 MaxWorkerTasks, FAssetRegistryVersion::Type HeaderVersion);

		/// 2) Read remaining data, including FNames
		///
		/// @return indexed store, usable for FPartialMapHandle::MakeFullHandle()
		COREUOBJECT_API TRefCountPtr<const FStore> LoadFinalData(FArchive& Ar, FAssetRegistryVersion::Type HeaderVersion);

	private:
		TRefCountPtr<FStore> Store;
		TOptional<ELoadOrder> Order;
	};

} // end namespace FixedTagPrivate
