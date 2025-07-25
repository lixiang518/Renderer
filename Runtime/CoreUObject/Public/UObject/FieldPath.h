// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SoftObjectPtr.h: Pointer to UObject asset, keeps extra information so that it is works even if the asset is not in memory
=============================================================================*/

#pragma once

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "CoreTypes.h"
#include "HAL/ThreadSafeCounter.h"
#include "Misc/AssertionMacros.h"
#include "Templates/Requires.h"
#include "Templates/TypeHash.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/Field.h"
#include "UObject/NameTypes.h"
#include "UObject/UObjectArray.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include <type_traits>

class FArchive;
class FLinkerLoad;
class UField;
class UStruct;
struct FUObjectItem;
template <typename T> struct TIsPODType;
template <typename T> struct TIsWeakPointerType;
template <typename T> struct TIsZeroConstructType;

struct FFieldPath
{
	friend struct FGCInternals;

	// TWeakFieldPtr needs access to ClearCachedField
	template<class T>
	friend struct TWeakFieldPtr;

	// FFieldPathProperty needs access to ConvertFromFullPath
	friend class FFieldPathProperty;

protected:

	/* Determines the behavior when resolving stored path */
	enum EPathResolveType
	{
		UseStructIfOuterNotFound = 0,
		UseStructAlways = 1
	};

	/** Untracked pointer to the resolved property */
	mutable FField* ResolvedField = nullptr;
#if WITH_EDITORONLY_DATA
	/** In editor builds, store the original class of the resolved property in case it changes after recompiling BPs */
	mutable FFieldClass* InitialFieldClass = nullptr;
	/** In editor builds, fields may get deleted even though their owner struct remains */
	mutable int32 FieldPathSerialNumber = 0;
#endif
	/** The cached owner of this field. Even though implemented as a weak pointer, GC will keep a strong reference to it if exposed through UPROPERTY macro */
	mutable TWeakObjectPtr<UStruct> ResolvedOwner;

	/** Path to the FField object from the innermost FField to the outermost UObject (UPackage) */
	TArray<FName> Path;

	FORCEINLINE bool NeedsResolving() const
	{
		if (ResolvedField)
		{
#if WITH_EDITORONLY_DATA
			UStruct* Owner = ResolvedOwner.Get();
			// In uncooked builds we also need to check if the serial number on the owner struct is identical
			// It will change if the struct has been recompiled or its properties have been destroyed
			if (Owner && IsFieldPathSerialNumberIdentical(Owner))
			{
				return false;
			}
#else
			// The assumption is that if we already resolved a field and its owner is still valid, there's no need to resolve again
			return !ResolvedOwner.IsValid();
#endif // WITH_EDITORONLY_DATA
		}
		return true;
	}

	/** Clears the cached value so that the next time Get() is called, it will be resolved again */
	FORCEINLINE void ClearCachedField() const
	{
		ResolvedField = nullptr;
#if WITH_EDITORONLY_DATA
		InitialFieldClass = nullptr;
		FieldPathSerialNumber = 0;
#endif // WITH_EDITORONLY_DATA
	}

private:

#if WITH_EDITORONLY_DATA
	/** Used to check if the serial number on the provided struct is identical to the one stored in this FFieldPath */
	COREUOBJECT_API bool IsFieldPathSerialNumberIdentical(UStruct* InStruct) const;
	/** Gets the serial number stored on the provided struct */
	COREUOBJECT_API int32 GetFieldPathSerialNumber(UStruct* InStruct) const;
#endif

	/** FOR INTERNAL USE ONLY: gets the pointer to the resolved field without trying to resolve it */
	FORCEINLINE FUObjectItem* GetResolvedOwnerItemInternal()
	{
		return ResolvedOwner.Internal_GetObjectItem();
	}
	FORCEINLINE void ClearCachedFieldInternal()
	{
		ResolvedField = nullptr;
		ResolvedOwner.Reset();
	}

	/**
	 * Tries to resolve the field owner
	 * @param InCurrentStruct Struct that's trying to resolve this field path
	 * @param InResolveType Type of the resolve operation
	 * @return Resolved owner struct
	 */
	COREUOBJECT_API UStruct* TryToResolveOwnerFromStruct(UStruct* InCurrentStruct = nullptr, EPathResolveType InResolveType = FFieldPath::UseStructIfOuterNotFound) const;
	

	/**
	 * Tries to resolve the field owner
	 * @param InLinker the current linker load serializing this field path
	 * @return Resolved owner struct
	 */
	COREUOBJECT_API UStruct* TryToResolveOwnerFromLinker(FLinkerLoad* InLinker) const;


	/**
	 * Tries to convert the full path stored in this FFieldPath to the new format (Owner reference + path to the field)
	 * @param InLinker the current linker load serializing this field path
	 * @return Resulved owner struct
	 */
	COREUOBJECT_API UStruct* ConvertFromFullPath(FLinkerLoad* InLinker);

public:

	FFieldPath() = default;

	FFieldPath(FField* InField)
	{
		Generate(InField);
	}

#if WITH_EDITORONLY_DATA
	COREUOBJECT_API FFieldPath(UField* InField, const FName& InPropertyTypeName);
#endif

	/** Generates path from the passed in field pointer */
	COREUOBJECT_API void Generate(FField* InField);

	/** Generates path from the passed in field pointer */
	COREUOBJECT_API void Generate(const TCHAR* InFieldPathString);

#if WITH_EDITORONLY_DATA
	COREUOBJECT_API void GenerateFromUField(UField* InField);
#endif

	/**
	 * Tries to resolve the path without caching the resolved pointer 
	 * @param InCurrentStruct Struct that's trying to resolve this field path
	 * @param OutOwnerIndex ObjectIndex of the Owner UObject
	 * @return Resolved field or null
	 */
	COREUOBJECT_API FField* TryToResolvePath(UStruct* InCurrentStruct, EPathResolveType InResolveType = FFieldPath::UseStructIfOuterNotFound) const;

	/**
	 * Tries to resolve the path and caches the result
	 * @param ExpectedClass Expected class of the resolved field
	 * @param InCurrentStruct Struct that's trying to resolve this field path	 
	 */
	FORCEINLINE void ResolveField(FFieldClass* ExpectedClass = FField::StaticClass(), UStruct* InCurrentStruct = nullptr, EPathResolveType InResolveType = FFieldPath::UseStructIfOuterNotFound) const
	{
		FField* FoundField = TryToResolvePath(InCurrentStruct, InResolveType);
		if (FoundField && FoundField->IsA(ExpectedClass) 
#if WITH_EDITORONLY_DATA
			&& (!InitialFieldClass || FoundField->IsA(InitialFieldClass))
#endif // WITH_EDITORONLY_DATA
			)
		{
			ResolvedField = FoundField;
#if WITH_EDITORONLY_DATA
			if (!InitialFieldClass)
			{
				InitialFieldClass = FoundField->GetClass();
			}
			UStruct* Owner = ResolvedOwner.Get();
			check(Owner);
			FieldPathSerialNumber = GetFieldPathSerialNumber(Owner);
#endif // WITH_EDITORONLY_DATA
		}
		else if (ResolvedField)
		{
			// In case this field has been previously resolved, clear the owner as well as it's impossible the original field
			// will ever come back (it's most likely been deleted) and we don't want to resolve to a newly created one even if its name and class match
			ResolvedOwner.Reset();
			ResolvedField = nullptr;
		}
	}

	/**
	 * Gets the field represented by this FFieldPath
	 * @param ExpectedType Expected type of the resolved field
	 * @param InCurrentStruct Struct that's trying to resolve this field path
	 * @return Field represented by this FFieldPath or null if it couldn't be resolved
	 */
	FORCEINLINE FField* GetTyped(FFieldClass* ExpectedType, UStruct* InCurrentStruct = nullptr) const
	{
		if (NeedsResolving() && Path.Num())
		{
			ResolveField(ExpectedType, InCurrentStruct, FFieldPath::UseStructIfOuterNotFound);
		}
		return ResolvedField;
	}

	/** 
	 * Returns true if the field path is empty (does not test if the owner is valid) 
	 * This is usually used to verify if the reason behind this field being unresolved is because the owner is missing or the property couldn't be found.
	 **/
	inline bool IsPathToFieldEmpty() const
	{
		return !Path.Num();
	}

	/**
	* Slightly different than !IsValid(), returns true if this used to point to a FField, but doesn't any more and has not been assigned or reset in the mean time.
	* @return true if this used to point at a real object but no longer does.
	**/
	inline bool IsStale() const
	{
		return ResolvedField && (!ResolvedOwner.IsValid()
#if WITH_EDITORONLY_DATA
			|| !IsFieldPathSerialNumberIdentical(ResolvedOwner.Get())
#endif // WITH_EDITORONLY_DATA
			);
	}

	/**
	* Reset the weak pointer back to the NULL state
	*/
	inline void Reset()
	{
		ClearCachedField();
		ResolvedOwner.Reset();
		Path.Empty();
	}

	FORCEINLINE bool operator==(const FFieldPath& InOther) const
	{
		return ResolvedOwner == InOther.ResolvedOwner && Path == InOther.Path;
	}

	FORCEINLINE bool operator!=(const FFieldPath& InOther) const
	{
		return ResolvedOwner != InOther.ResolvedOwner || Path != InOther.Path;
	}

	COREUOBJECT_API FString ToString() const;

	COREUOBJECT_API friend FArchive& operator<<(FArchive& Ar, FFieldPath& InOutPropertyPath);

	/** Hash function. */
	[[nodiscard]] FORCEINLINE friend uint32 GetTypeHash(const FFieldPath& InPropertyPath)
	{
		uint32 HashValue = 0;
		for (const FName& PathSegment : InPropertyPath.Path)
		{
			HashValue = HashCombine(HashValue, GetTypeHash(PathSegment));
		}
		return HashValue;
	}
};

template<class PropertyType>
struct TFieldPath : public FFieldPath
{
private:

	// These exists only to disambiguate the two constructors below
	enum EDummy1 { Dummy1 };

public:
	TFieldPath()
	{}
	FORCEINLINE TFieldPath(const TFieldPath& Other)
	{
		//  First refresh the serial number from the other path
		Other.Get();
		// Now that the Other path is refreshed, we can copy from it
		FFieldPath::operator=(Other);
	}
	FORCEINLINE TFieldPath& operator=(const TFieldPath& Other)
	{
		//  First refresh the serial number from the other path
		Other.Get();
		// Now that the Other path is refreshed, we can copy from it
		FFieldPath::operator=(Other);
		return *this;
	}

	/**
	* Construct from a null pointer
	**/
	FORCEINLINE TFieldPath(TYPE_OF_NULLPTR)
		: FFieldPath()
	{
	}

	/**
	* Construct from a string
	**/
	FORCEINLINE TFieldPath(const TCHAR* InPath)
		: FFieldPath()
	{
		Generate(InPath);
	}

#if WITH_EDITORONLY_DATA
	TFieldPath(UField* InField)
		: FFieldPath(InField, PropertyType::StaticClass()->GetFName())
	{
		// This static assert is in here rather than in the body of the class because we want
		// to be able to define TWeakFieldPtr<UUndefinedClass>.
		static_assert(std::is_convertible_v<PropertyType*, const volatile FField*>, "TFieldPath can only be constructed with FField types");
	}
#endif

	/**
	* Construct from an object pointer
	* @param Object object to create a weak pointer to
	**/
	template <
		typename OtherPropertyType
		UE_REQUIRES(std::is_convertible_v<OtherPropertyType*, PropertyType*>)
	>
	FORCEINLINE TFieldPath(OtherPropertyType* InProperty, EDummy1 = Dummy1)
		: FFieldPath((FField*)CastField<PropertyType>(InProperty))
	{
		// This static assert is in here rather than in the body of the class because we want
		// to be able to define TFieldPath<UUndefinedClass>.
		static_assert(std::is_convertible_v<PropertyType*, const volatile FField*>, "TFieldPath can only be constructed with FField types");
	}

	/**
	* Construct from another weak pointer of another type, intended for derived-to-base conversions
	* @param Other weak pointer to copy from
	**/
	template <
		typename OtherPropertyType
		UE_REQUIRES(std::is_convertible_v<OtherPropertyType*, PropertyType*>)
	>
	FORCEINLINE TFieldPath(const TFieldPath<OtherPropertyType>& Other)
		: FFieldPath(Other)
	{
		// This static assert is in here rather than in the body of the class because we want
		// to be able to define TFieldPath<UUndefinedClass>.
		static_assert(std::is_convertible_v<PropertyType*, const volatile FField*>, "TFieldPath can only be constructed with FField types");
	}

	/**
	* Copy from an object pointer
	* @param Object object to create a weak pointer to
	**/
	template <
		typename OtherPropertyType
		UE_REQUIRES(std::is_convertible_v<OtherPropertyType*, PropertyType*>)
	>
	FORCEINLINE void operator=(OtherPropertyType* InProperty)
	{
		// This (FField*) cast is effectively a const_cast, as we've already validated the convertibility in the
		// constraint, and that PropertyType is an FField type in the constructors.
		ResolvedField = (FField*)InProperty; 
		Generate(ResolvedField);
	}

	/**
	* Assign from another weak pointer, intended for derived-to-base conversions
	* @param Other weak pointer to copy from
	**/
	template <
		typename OtherPropertyType
		UE_REQUIRES(std::is_convertible_v<OtherPropertyType*, PropertyType*>)
	>
	FORCEINLINE void operator=(const TFieldPath<OtherPropertyType>& Other)
	{
		// First make sure the Other path has the serial number up to date, otherwise we'll keep having to
		// reevealuate this path because it gets the serial number copied from the Other path
		Other.Get();
		// Now that the Other path is refreshed, we can copy from it
		FFieldPath::operator=(Other);
	}

	/**
	 * Gets the field represented by this TFieldPath
	 * @param InCurrentStruct Struct that's trying to resolve this field path
	 * @return Field represented by this FFieldPath or null if it couldn't be resolved
	 */
	FORCEINLINE PropertyType* Get(UStruct* InCurrentStruct = nullptr) const
	{
		return (PropertyType*)GetTyped(PropertyType::StaticClass(), InCurrentStruct);
	}

	FORCEINLINE PropertyType* ResolveWithRenamedStructPackage(UStruct* InCurrentStruct)
	{
		ClearCachedField();
		ResolveField(PropertyType::StaticClass(), InCurrentStruct, FFieldPath::UseStructAlways);
		return static_cast<PropertyType*>(ResolvedField);
	}

	/**
	* Dereference the weak pointer
	**/
	FORCEINLINE PropertyType* operator*() const
	{
		return Get();
	}

	/**
	* Dereference the weak pointer
	**/
	FORCEINLINE PropertyType* operator->() const
	{
		return Get();
	}
};

/**
* Compare weak pointers for equality
* @param Lhs weak pointer to compare
* @param Rhs weak pointer to compare
**/
template <typename LhsType, typename RhsType>
FORCEINLINE auto operator==(const TFieldPath<LhsType>& Lhs, const TFieldPath<LhsType>& Rhs)
	-> decltype((LhsType*)nullptr == (RhsType*)nullptr)
{
	return *(const FFieldPath*)&Lhs == *(const FFieldPath*)&Rhs;
}

/**
* Compare weak pointers for equality
* @param Lhs weak pointer to compare
* @param Rhs pointer to compare
**/
template <typename LhsType, typename RhsType>
FORCEINLINE auto operator==(const TFieldPath<LhsType>& Lhs, const RhsType* Rhs)
	-> decltype((LhsType*)nullptr == Rhs)
{
	return Lhs.Get() == Rhs;
}

/**
* Compare weak pointers for equality
* @param Lhs pointer to compare
* @param Rhs weak pointer to compare
**/
template <typename LhsType, typename RhsType>
FORCEINLINE auto operator==(const LhsType* Lhs, const TFieldPath<RhsType>& Rhs)
	-> decltype(Lhs == (RhsType*)nullptr)
{
	return Lhs == Rhs.Get();
}

/**
* Test weak pointer for null
* @param Lhs pointer to test
**/
template <typename LhsType>
FORCEINLINE bool operator==(TFieldPath<LhsType>& Lhs, TYPE_OF_NULLPTR)
{
	return !Lhs.Get();
}

/**
* Test weak pointer for null
* @param Rhs pointer to test
**/
template <typename RhsType>
FORCEINLINE bool operator==(TYPE_OF_NULLPTR, TFieldPath<RhsType>& Rhs)
{
	return !Rhs.Get();
}

#if !PLATFORM_COMPILER_HAS_GENERATED_COMPARISON_OPERATORS
/**
* Compare weak pointers for inequality
* @param Lhs weak pointer to compare
* @param Rhs weak pointer to compare
**/
template <typename LhsType, typename RhsType>
FORCEINLINE auto operator!=(const TFieldPath<LhsType>& Lhs, const TFieldPath<LhsType>& Rhs)
	-> decltype((LhsType*)nullptr != (RhsType*)nullptr)
{
	return !(Lhs == Rhs);
}

/**
* Compare weak pointers for inequality
* @param Lhs weak pointer to compare
* @param Rhs pointer to compare
**/
template <typename LhsType, typename RhsType>
FORCEINLINE auto operator!=(const TFieldPath<LhsType>& Lhs, const RhsType* Rhs)
	-> decltype((LhsType*)nullptr != Rhs)
{
	return !(Lhs == Rhs);
}

/**
* Compare weak pointers for inequality
* @param Lhs pointer to compare
* @param Rhs weak pointer to compare
**/
template <typename LhsType, typename RhsType>
FORCEINLINE auto operator!=(const LhsType* Lhs, const TFieldPath<RhsType>& Rhs)
	-> decltype(Lhs != (RhsType*)nullptr)
{
	return !(Lhs == Rhs);
}

/**
* Test weak pointer for non-null
* @param Lhs pointer to test
**/
template <typename LhsType>
FORCEINLINE bool operator!=(TFieldPath<LhsType>& Lhs, TYPE_OF_NULLPTR)
{
	return !(Lhs == nullptr);
}

/**
* Test weak pointers for non-null
* @param Rhs pointer to test
**/
template <typename RhsType>
FORCEINLINE bool operator!=(TYPE_OF_NULLPTR, TFieldPath<RhsType>& Rhs)
{
	return !(nullptr == Rhs);
}
#endif

// Helper function which deduces the type of the initializer
template <typename PropertyType>
FORCEINLINE TFieldPath<PropertyType> MakePropertyPath(PropertyType* Ptr)
{
	return TFieldPath<PropertyType>(Ptr);
}


template<class T> struct TIsPODType<TFieldPath<T> > { enum { Value = true }; };
template<class T> struct TIsZeroConstructType<TFieldPath<T> > { enum { Value = true }; };
template<class T> struct TIsWeakPointerType<TFieldPath<T> > { enum { Value = true }; };


/**
* MapKeyFuncs for TFieldPath which allow the key to become stale without invalidating the map.
*/
template <typename KeyType, typename ValueType, bool bInAllowDuplicateKeys = false>
struct TPropertyPathMapKeyFuncs : public TDefaultMapKeyFuncs<KeyType, ValueType, bInAllowDuplicateKeys>
{
	typedef typename TDefaultMapKeyFuncs<KeyType, ValueType, bInAllowDuplicateKeys>::KeyInitType KeyInitType;

	static FORCEINLINE bool Matches(KeyInitType A, KeyInitType B)
	{
		return A == B;
	}

	static FORCEINLINE uint32 GetKeyHash(KeyInitType Key)
	{
		return GetTypeHash(Key);
	}
};
