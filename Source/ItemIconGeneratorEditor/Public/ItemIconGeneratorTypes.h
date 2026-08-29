#pragma once

#include "CoreMinimal.h"
#include "ItemIconGeneratorTypes.generated.h"

class UTexture2D;
class UStaticMesh;

/** Controls how a static mesh is framed and rendered into an icon. */
USTRUCT(BlueprintType)
struct ITEMICONGENERATOREDITOR_API FItemIconCaptureSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Mesh")
	FRotator MeshRotation = FRotator(0.0f, -35.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Mesh")
	FVector MeshLocationOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Camera")
	FVector CameraTargetOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Camera", meta = (ClampMin = "0.01", UIMin = "0.5", UIMax = "10.0"))
	float CameraDistanceMultiplier = 2.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Camera", meta = (ClampMin = "5.0", ClampMax = "170.0", UIMin = "5.0", UIMax = "90.0"))
	float FieldOfView = 28.0f;

	/** Vertical orbit angle in degrees. -90 looks down from above. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Camera", meta = (ClampMin = "-89.0", ClampMax = "89.0", UIMin = "-89.0", UIMax = "89.0"))
	float CameraPitch = -49.6f;

	/** Horizontal orbit angle in degrees. 0 is front and 180 is back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Camera", meta = (UIMin = "-180.0", UIMax = "180.0"))
	float CameraYaw = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Output", meta = (ClampMin = "64", ClampMax = "4096", UIMin = "64", UIMax = "2048"))
	int32 TextureSize = 512;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Output")
	bool bTransparentBackground = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Output")
	FLinearColor BackgroundColor = FLinearColor(0.72f, 0.76f, 0.80f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Lighting", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "20.0"))
	float DirectionalLightIntensity = 3.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Lighting")
	FRotator DirectionalLightRotation = FRotator(-45.0f, -35.0f, 0.0f);

	/** Key light color. The DirectionalLight name is retained for source compatibility. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Lighting")
	FLinearColor DirectionalLightColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Lighting", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "20.0"))
	float FillLightIntensity = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Lighting")
	FRotator FillLightRotation = FRotator(-25.0f, 125.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Lighting")
	FLinearColor FillLightColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Lighting", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "20.0"))
	float BackLightIntensity = 2.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Lighting")
	FRotator BackLightRotation = FRotator(-35.0f, -135.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Lighting")
	FLinearColor BackLightColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Lighting", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "10.0"))
	float SkyLightIntensity = 0.8f;

	/** Fixed capture exposure, independent of the destination project's default post process. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Color", meta = (ClampMin = "-15.0", ClampMax = "15.0", UIMin = "-5.0", UIMax = "5.0"))
	float ExposureCompensation = 0.0f;

	/** Global saturation applied by the capture. 1.0 preserves the material color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Color", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "2.0"))
	float ColorSaturation = 1.0f;
};

/** Controls the project-content asset created for an icon. */
USTRUCT(BlueprintType)
struct ITEMICONGENERATOREDITOR_API FItemIconSaveSettings
{
	GENERATED_BODY()

	/** Must be /Game or a folder below /Game. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Save")
	FString PackagePath = TEXT("/Game/Generated/ItemIcons");

	/** Empty uses T_Icon_<StaticMeshName>. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Save")
	FString AssetName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Save")
	bool bOverwriteExisting = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Save")
	bool bSaveImmediately = true;
};

/** Describes the generated texture or why generation failed. */
USTRUCT(BlueprintType)
struct ITEMICONGENERATOREDITOR_API FItemIconGenerationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Item Icon")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "Item Icon")
	TObjectPtr<UTexture2D> Texture = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Item Icon")
	FString ObjectPath;

	UPROPERTY(BlueprintReadOnly, Category = "Item Icon")
	FString Message;
};

/** A transient preview lookup or capture result. Preview textures are never project assets. */
USTRUCT(BlueprintType)
struct ITEMICONGENERATOREDITOR_API FItemIconPreviewResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Item Icon|Preview")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "Item Icon|Preview")
	bool bFromCache = false;

	/** Stable for the mesh path and all capture settings during the editor session. */
	UPROPERTY(BlueprintReadOnly, Category = "Item Icon|Preview")
	FString CacheKey;

	UPROPERTY(BlueprintReadOnly, Category = "Item Icon|Preview")
	TObjectPtr<UTexture2D> Texture = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Item Icon|Preview")
	int32 TextureSize = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Item Icon|Preview")
	FString Message;
};

/** Lifecycle state for one request in a batch. */
UENUM(BlueprintType)
enum class EItemIconBatchItemStatus : uint8
{
	Pending,
	Running,
	Succeeded,
	Failed,
	Cancelled
};

/** Lifecycle state for a batch job. */
UENUM(BlueprintType)
enum class EItemIconBatchState : uint8
{
	Idle,
	Preparing,
	Running,
	Cancelling,
	Completed
};

/** One project-agnostic icon generation request. */
USTRUCT(BlueprintType)
struct ITEMICONGENERATOREDITOR_API FItemIconGenerationRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Batch")
	TSoftObjectPtr<UStaticMesh> StaticMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Batch")
	FItemIconCaptureSettings CaptureSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Batch")
	FItemIconSaveSettings SaveSettings;
};

/** Settings and items submitted to a sequential batch job. */
USTRUCT(BlueprintType)
struct ITEMICONGENERATOREDITOR_API FItemIconBatchRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Batch")
	TArray<FItemIconGenerationRequest> Items;

	/** When false, the first failed item stops the remaining queue. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Icon|Batch")
	bool bContinueOnFailure = true;
};

/** Result for one item, retaining its original request index. */
USTRUCT(BlueprintType)
struct ITEMICONGENERATOREDITOR_API FItemIconBatchItemResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Item Icon|Batch")
	int32 RequestIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Item Icon|Batch")
	FSoftObjectPath StaticMeshPath;

	UPROPERTY(BlueprintReadOnly, Category = "Item Icon|Batch")
	EItemIconBatchItemStatus Status = EItemIconBatchItemStatus::Pending;

	/** Texture is available through completion callbacks and released afterward; ObjectPath remains available. */
	UPROPERTY(BlueprintReadOnly, Category = "Item Icon|Batch")
	FItemIconGenerationResult GenerationResult;
};

/** Lightweight state intended for progress displays or logging. */
USTRUCT(BlueprintType)
struct ITEMICONGENERATOREDITOR_API FItemIconBatchProgress
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Item Icon|Batch")
	EItemIconBatchState State = EItemIconBatchState::Idle;

	UPROPERTY(BlueprintReadOnly, Category = "Item Icon|Batch")
	int32 TotalItems = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Item Icon|Batch")
	int32 CompletedItems = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Item Icon|Batch")
	int32 SucceededItems = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Item Icon|Batch")
	int32 FailedItems = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Item Icon|Batch")
	int32 CancelledItems = 0;

	/** INDEX_NONE while preparing or between items. */
	UPROPERTY(BlueprintReadOnly, Category = "Item Icon|Batch")
	int32 CurrentItemIndex = INDEX_NONE;
};

/** Aggregate result emitted exactly once when a batch finishes. */
USTRUCT(BlueprintType)
struct ITEMICONGENERATOREDITOR_API FItemIconBatchResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Item Icon|Batch")
	TArray<FItemIconBatchItemResult> Items;

	UPROPERTY(BlueprintReadOnly, Category = "Item Icon|Batch")
	bool bCancelled = false;

	UPROPERTY(BlueprintReadOnly, Category = "Item Icon|Batch")
	bool bStoppedOnFailure = false;
};
