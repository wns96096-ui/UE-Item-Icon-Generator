#pragma once

#include "CoreMinimal.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ItemIconGeneratorTypes.h"
#include "PreviewScene.h"
#include "UObject/StrongObjectPtr.h"

class UDirectionalLightComponent;
class USceneCaptureComponent2D;
class UStaticMesh;
class UStaticMeshComponent;

/** Reusable isolated scene and render resources used by interactive preview capture. */
class FItemIconPreviewRenderer final
{
public:
	FItemIconPreviewRenderer() = default;
	~FItemIconPreviewRenderer();

	bool Capture(
		UStaticMesh* StaticMesh,
		const FItemIconCaptureSettings& Settings,
		TArray<FColor>& OutPixels,
		FString& OutError);

private:
	bool EnsureInitialized(FString& OutError);
	void PrepareStaticMeshForCapture();
	bool EnsureRenderTarget(
		UTextureRenderTarget2D* RenderTarget,
		int32 TextureSize,
		ETextureRenderTargetFormat Format,
		const FLinearColor& ClearColor,
		FString& OutError);

	TUniquePtr<FPreviewScene> PreviewScene;
	UStaticMeshComponent* MeshComponent = nullptr;
	UDirectionalLightComponent* FillLight = nullptr;
	UDirectionalLightComponent* BackLight = nullptr;
	USceneCaptureComponent2D* CaptureComponent = nullptr;
	TStrongObjectPtr<UTextureRenderTarget2D> ColorRenderTarget;
	TStrongObjectPtr<UTextureRenderTarget2D> AlphaRenderTarget;
	TArray<FFloat16Color> AlphaPixels;
};
