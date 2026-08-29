#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ItemIconGeneratorTypes.h"
#include "ItemIconGeneratorLibrary.generated.h"

class UStaticMesh;

/** Editor-only, project-agnostic static mesh icon generation. */
UCLASS()
class ITEMICONGENERATOREDITOR_API UItemIconGeneratorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Waits for pending asset/shader compilation, captures a static mesh in an isolated preview scene,
	 * and saves it as a Texture2D asset.
	 * The generated texture is returned but is not assigned to any project-specific object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Item Icon Generator")
	static FItemIconGenerationResult GenerateIconFromStaticMesh(
		UStaticMesh* StaticMesh,
		FItemIconCaptureSettings CaptureSettings,
		FItemIconSaveSettings SaveSettings);

	/**
	 * C++ batch path used after all queued assets have completed compilation.
	 * Callers must run this on the game thread and perform the compilation wait once beforehand.
	 */
	static FItemIconGenerationResult GenerateIconFromPreparedStaticMesh(
		UStaticMesh* StaticMesh,
		FItemIconCaptureSettings CaptureSettings,
		FItemIconSaveSettings SaveSettings);

	/** Captures pixels without creating or saving a project asset. Compilation must already be complete. */
	static bool CapturePixelsFromPreparedStaticMesh(
		UStaticMesh* StaticMesh,
		FItemIconCaptureSettings CaptureSettings,
		TArray<FColor>& OutPixels,
		FString& OutError);

	/** Saves previously captured pixels, allowing a preview to become the exact final asset. */
	static FItemIconGenerationResult SaveCapturedPixelsAsTexture(
		UStaticMesh* StaticMesh,
		const TArray<FColor>& Pixels,
		int32 TextureSize,
		FItemIconSaveSettings SaveSettings);
};
