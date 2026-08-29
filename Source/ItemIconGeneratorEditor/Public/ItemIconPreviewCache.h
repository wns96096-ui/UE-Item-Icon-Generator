#pragma once

#include "CoreMinimal.h"
#include "ItemIconGeneratorTypes.h"
#include "ItemIconPreviewCache.generated.h"

class UStaticMesh;
class FItemIconPreviewRenderer;

USTRUCT()
struct FItemIconPreviewCacheEntry
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> StaticMesh = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> Texture = nullptr;

	UPROPERTY(Transient)
	TArray<FColor> Pixels;

	UPROPERTY(Transient)
	int32 TextureSize = 0;

	UPROPERTY(Transient)
	int64 LastAccessSerial = 0;
};

/**
 * Session-only preview cache. It keeps transient textures and source pixels in memory,
 * so saving a preview never requires a second capture.
 */
UCLASS(BlueprintType)
class ITEMICONGENERATOREDITOR_API UItemIconPreviewCache : public UObject
{
	GENERATED_BODY()

public:
	/** Creates an empty transient cache. The caller should retain the returned object. */
	UFUNCTION(BlueprintCallable, Category = "Item Icon Generator|Preview")
	static UItemIconPreviewCache* CreatePreviewCache();

	/** Returns a cached preview or captures and caches one. No project asset is created. */
	UFUNCTION(BlueprintCallable, Category = "Item Icon Generator|Preview")
	FItemIconPreviewResult CapturePreview(
		UStaticMesh* StaticMesh,
		FItemIconCaptureSettings CaptureSettings,
		bool bForceRefresh = false);

	/** Looks up a preview without rendering. */
	UFUNCTION(BlueprintCallable, Category = "Item Icon Generator|Preview")
	FItemIconPreviewResult FindPreview(
		UStaticMesh* StaticMesh,
		FItemIconCaptureSettings CaptureSettings);

	/** Saves the exact cached pixels represented by CacheKey as a Texture2D asset. */
	UFUNCTION(BlueprintCallable, Category = "Item Icon Generator|Preview")
	FItemIconGenerationResult SavePreview(
		const FString& CacheKey,
		FItemIconSaveSettings SaveSettings);

	/** Removes all previews for one source mesh, regardless of capture settings. */
	UFUNCTION(BlueprintCallable, Category = "Item Icon Generator|Preview")
	int32 InvalidateMesh(UStaticMesh* StaticMesh);

	UFUNCTION(BlueprintCallable, Category = "Item Icon Generator|Preview")
	bool InvalidatePreview(const FString& CacheKey);

	UFUNCTION(BlueprintCallable, Category = "Item Icon Generator|Preview")
	void Clear();

	UFUNCTION(BlueprintPure, Category = "Item Icon Generator|Preview")
	int32 Num() const { return Entries.Num(); }

	/** Limits retained pixel memory. Oldest entries are evicted first. */
	UFUNCTION(BlueprintCallable, Category = "Item Icon Generator|Preview")
	void SetMaximumEntries(int32 InMaximumEntries);

	UFUNCTION(BlueprintPure, Category = "Item Icon Generator|Preview")
	int32 GetMaximumEntries() const { return MaximumEntries; }

	/** Builds the key used by CapturePreview and FindPreview. */
	UFUNCTION(BlueprintPure, Category = "Item Icon Generator|Preview")
	static FString MakePreviewCacheKey(
		UStaticMesh* StaticMesh,
		FItemIconCaptureSettings CaptureSettings);

	/** Releases the persistent preview scene and GPU render targets without clearing cached previews. */
	void ReleaseRenderResources();

	virtual void BeginDestroy() override;

private:
	FItemIconPreviewResult MakeResult(const FString& CacheKey, bool bFromCache, const FString& Message);
	void Touch(FItemIconPreviewCacheEntry& Entry);
	void EvictToLimit();

	UPROPERTY(Transient)
	TMap<FString, FItemIconPreviewCacheEntry> Entries;

	UPROPERTY(Transient)
	int32 MaximumEntries = 32;

	UPROPERTY(Transient)
	int64 AccessSerial = 0;

	FItemIconPreviewRenderer* PreviewRenderer = nullptr;
};
