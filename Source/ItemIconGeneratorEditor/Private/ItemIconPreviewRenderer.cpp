#include "ItemIconPreviewRenderer.h"

#include "AssetCompilingManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "ContentStreaming.h"
#include "Engine/Scene.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInterface.h"
#include "RenderingThread.h"
#include "UObject/UObjectGlobals.h"

namespace ItemIconPreviewRenderer
{
	UDirectionalLightComponent* AddDirectionalLight(FPreviewScene& PreviewScene)
	{
		UDirectionalLightComponent* Light = NewObject<UDirectionalLightComponent>(
			GetTransientPackage(), NAME_None, RF_Transient);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetCastShadows(false);
		Light->bTransmission = true;
		PreviewScene.AddComponent(Light, FTransform::Identity);
		return Light;
	}

	void ConfigureDirectionalLight(
		UDirectionalLightComponent* Light,
		const FRotator& Rotation,
		const float Intensity,
		const FLinearColor& Color)
	{
		Light->SetWorldRotation(Rotation);
		Light->SetIntensity(FMath::Max(Intensity, 0.0f));
		Light->SetLightColor(Color, false);
	}

	bool ReadColorPixels(UTextureRenderTarget2D* RenderTarget, TArray<FColor>& OutPixels)
	{
		FTextureRenderTargetResource* Resource = RenderTarget
			? RenderTarget->GameThread_GetRenderTargetResource()
			: nullptr;
		if (!Resource)
		{
			return false;
		}

		FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
		ReadFlags.SetLinearToGamma(true);
		const FIntRect SourceRect(0, 0, RenderTarget->SizeX, RenderTarget->SizeY);
		return Resource->ReadPixels(OutPixels, ReadFlags, SourceRect)
			&& OutPixels.Num() == RenderTarget->SizeX * RenderTarget->SizeY;
	}

	bool ReadAlphaPixels(UTextureRenderTarget2D* RenderTarget, TArray<FFloat16Color>& OutPixels)
	{
		FTextureRenderTargetResource* Resource = RenderTarget
			? RenderTarget->GameThread_GetRenderTargetResource()
			: nullptr;
		if (!Resource)
		{
			return false;
		}

		const FIntRect SourceRect(0, 0, RenderTarget->SizeX, RenderTarget->SizeY);
		return Resource->ReadFloat16Pixels(OutPixels, FReadSurfaceDataFlags(RCM_MinMax), SourceRect)
			&& OutPixels.Num() == RenderTarget->SizeX * RenderTarget->SizeY;
	}
}

FItemIconPreviewRenderer::~FItemIconPreviewRenderer()
{
	if (CaptureComponent)
	{
		CaptureComponent->TextureTarget = nullptr;
	}
	if (MeshComponent)
	{
		MeshComponent->SetStaticMesh(nullptr);
	}

	AlphaRenderTarget.Reset();
	ColorRenderTarget.Reset();
	PreviewScene.Reset();
}

bool FItemIconPreviewRenderer::Capture(
	UStaticMesh* StaticMesh,
	const FItemIconCaptureSettings& Settings,
	TArray<FColor>& OutPixels,
	FString& OutError)
{
	if (!StaticMesh)
	{
		OutError = TEXT("StaticMesh is required.");
		return false;
	}

	if (!EnsureInitialized(OutError))
	{
		return false;
	}

	const int32 TextureSize = FMath::Clamp(Settings.TextureSize, 64, 4096);
	if (!EnsureRenderTarget(
		ColorRenderTarget.Get(),
		TextureSize,
		RTF_RGBA8_SRGB,
		Settings.BackgroundColor,
		OutError))
	{
		return false;
	}

	PreviewScene->SetLightDirection(Settings.DirectionalLightRotation);
	PreviewScene->SetLightBrightness(FMath::Max(Settings.DirectionalLightIntensity, 0.0f));
	PreviewScene->SetLightColor(Settings.DirectionalLightColor.ToFColorSRGB());
	PreviewScene->SetSkyBrightness(FMath::Max(Settings.SkyLightIntensity, 0.0f));
	ItemIconPreviewRenderer::ConfigureDirectionalLight(
		FillLight,
		Settings.FillLightRotation,
		Settings.FillLightIntensity,
		Settings.FillLightColor);
	ItemIconPreviewRenderer::ConfigureDirectionalLight(
		BackLight,
		Settings.BackLightRotation,
		Settings.BackLightIntensity,
		Settings.BackLightColor);

	const bool bMeshChanged = MeshComponent->GetStaticMesh() != StaticMesh;
	MeshComponent->SetStaticMesh(StaticMesh);
	MeshComponent->SetWorldTransform(FTransform(Settings.MeshRotation, Settings.MeshLocationOffset));
	MeshComponent->UpdateBounds();
	if (bMeshChanged || FAssetCompilingManager::Get().GetNumRemainingAssets() > 0)
	{
		PrepareStaticMeshForCapture();
	}

	const FBoxSphereBounds Bounds = MeshComponent->Bounds;
	const FVector PivotLocation = MeshComponent->GetComponentLocation();
	const FVector TargetLocation = PivotLocation + Settings.CameraTargetOffset;
	const FVector BoundsMin = Bounds.Origin - Bounds.BoxExtent;
	const FVector BoundsMax = Bounds.Origin + Bounds.BoxExtent;

	float RadiusFromPivot = 0.0f;
	for (int32 XIndex = 0; XIndex < 2; ++XIndex)
	{
		for (int32 YIndex = 0; YIndex < 2; ++YIndex)
		{
			for (int32 ZIndex = 0; ZIndex < 2; ++ZIndex)
			{
				const FVector Corner(
					XIndex == 0 ? BoundsMin.X : BoundsMax.X,
					YIndex == 0 ? BoundsMin.Y : BoundsMax.Y,
					ZIndex == 0 ? BoundsMin.Z : BoundsMax.Z);
				RadiusFromPivot = FMath::Max(RadiusFromPivot, FVector::Distance(PivotLocation, Corner));
			}
		}
	}

	const float Radius = FMath::Max(RadiusFromPivot, 10.0f);
	const float CameraDistance = Radius * FMath::Max(Settings.CameraDistanceMultiplier, 0.01f);
	const FRotator CameraOrbitRotation(
		FMath::Clamp(Settings.CameraPitch, -89.0f, 89.0f),
		Settings.CameraYaw,
		0.0f);
	const FVector CameraLocation = TargetLocation - CameraOrbitRotation.Vector() * CameraDistance;
	const FRotator CameraRotation = FRotationMatrix::MakeFromX(TargetLocation - CameraLocation).Rotator();

	CaptureComponent->SetWorldTransform(FTransform(CameraRotation, CameraLocation));
	CaptureComponent->FOVAngle = FMath::Clamp(Settings.FieldOfView, 5.0f, 170.0f);
	CaptureComponent->PostProcessSettings.AutoExposureBias = FMath::Clamp(
		Settings.ExposureCompensation, -15.0f, 15.0f);
	const float Saturation = FMath::Clamp(Settings.ColorSaturation, 0.0f, 2.0f);
	CaptureComponent->PostProcessSettings.ColorSaturation = FVector4(Saturation, Saturation, Saturation, 1.0f);

	CaptureComponent->TextureTarget = ColorRenderTarget.Get();
	CaptureComponent->CaptureSource = SCS_FinalColorLDR;
	CaptureComponent->CaptureScene();
	FlushRenderingCommands();

	if (!ItemIconPreviewRenderer::ReadColorPixels(ColorRenderTarget.Get(), OutPixels))
	{
		OutError = TEXT("Failed to read color pixels from the render target.");
		return false;
	}

	if (Settings.bTransparentBackground)
	{
		if (!EnsureRenderTarget(
			AlphaRenderTarget.Get(),
			TextureSize,
			RTF_RGBA16f,
			FLinearColor::Black,
			OutError))
		{
			return false;
		}

		CaptureComponent->TextureTarget = AlphaRenderTarget.Get();
		CaptureComponent->CaptureSource = SCS_SceneColorHDR;
		CaptureComponent->CaptureScene();
		FlushRenderingCommands();

		AlphaPixels.Reset();
		if (!ItemIconPreviewRenderer::ReadAlphaPixels(AlphaRenderTarget.Get(), AlphaPixels)
			|| AlphaPixels.Num() != OutPixels.Num())
		{
			OutError = TEXT("Failed to read transparency pixels from the render target.");
			return false;
		}

		for (int32 PixelIndex = 0; PixelIndex < OutPixels.Num(); ++PixelIndex)
		{
			const float Opacity = FMath::Clamp(1.0f - AlphaPixels[PixelIndex].A.GetFloat(), 0.0f, 1.0f);
			OutPixels[PixelIndex].A = static_cast<uint8>(FMath::RoundToInt(Opacity * 255.0f));
		}
	}
	else
	{
		for (FColor& Pixel : OutPixels)
		{
			Pixel.A = 255;
		}
	}

	return true;
}

void FItemIconPreviewRenderer::PrepareStaticMeshForCapture()
{
	// Adding a mesh to the isolated preview world can enqueue material shaders after
	// callers have already waited for global asset compilation. Submit those jobs for
	// this world and stream texture mips before the first capture to avoid fallback
	// materials in projects with a cold DDC.
	FlushAsyncLoading();
	UMaterialInterface::SubmitRemainingJobsForWorld(PreviewScene ? PreviewScene->GetWorld() : nullptr);
	FAssetCompilingManager::Get().FinishAllCompilation();
	FAssetCompilingManager::Get().ProcessAsyncTasks();

	UTexture::ForceUpdateTextureStreaming();
	IStreamingManager::Get().StreamAllResources(0.0f);

	MeshComponent->MarkRenderStateDirty();
	FlushRenderingCommands();
}

bool FItemIconPreviewRenderer::EnsureInitialized(FString& OutError)
{
	if (PreviewScene)
	{
		return true;
	}

	FPreviewScene::ConstructionValues PreviewValues;
	PreviewValues
		.SetCreateDefaultLighting(true)
		.SetCreatePhysicsScene(false)
		.SetTransactional(false);
	PreviewScene = MakeUnique<FPreviewScene>(PreviewValues);
	if (!PreviewScene->IsInitialized())
	{
		PreviewScene.Reset();
		OutError = TEXT("Failed to create the isolated preview scene.");
		return false;
	}

	FillLight = ItemIconPreviewRenderer::AddDirectionalLight(*PreviewScene);
	BackLight = ItemIconPreviewRenderer::AddDirectionalLight(*PreviewScene);

	MeshComponent = NewObject<UStaticMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	MeshComponent->SetMobility(EComponentMobility::Movable);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetCastShadow(true);
	PreviewScene->AddComponent(MeshComponent, FTransform::Identity);

	CaptureComponent = NewObject<USceneCaptureComponent2D>(GetTransientPackage(), NAME_None, RF_Transient);
	CaptureComponent->SetMobility(EComponentMobility::Movable);
	CaptureComponent->bCaptureEveryFrame = false;
	CaptureComponent->bCaptureOnMovement = false;
	CaptureComponent->PostProcessBlendWeight = 1.0f;
	CaptureComponent->PostProcessSettings.bOverride_AutoExposureMethod = true;
	CaptureComponent->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	CaptureComponent->PostProcessSettings.bOverride_AutoExposureBias = true;
	CaptureComponent->PostProcessSettings.bOverride_ColorSaturation = true;
	CaptureComponent->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	PreviewScene->AddComponent(CaptureComponent, FTransform::Identity);
	CaptureComponent->ShowOnlyComponent(MeshComponent);

	ColorRenderTarget.Reset(NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient));
	AlphaRenderTarget.Reset(NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient));
	if (!FillLight || !BackLight || !MeshComponent || !CaptureComponent
		|| !ColorRenderTarget || !AlphaRenderTarget)
	{
		OutError = TEXT("Failed to create reusable preview render resources.");
		return false;
	}

	return true;
}

bool FItemIconPreviewRenderer::EnsureRenderTarget(
	UTextureRenderTarget2D* RenderTarget,
	const int32 TextureSize,
	const ETextureRenderTargetFormat Format,
	const FLinearColor& ClearColor,
	FString& OutError)
{
	if (!RenderTarget)
	{
		OutError = TEXT("Reusable preview render target is missing.");
		return false;
	}

	const bool bNeedsResourceUpdate = RenderTarget->SizeX != TextureSize
		|| RenderTarget->SizeY != TextureSize
		|| RenderTarget->RenderTargetFormat != Format
		|| RenderTarget->ClearColor != ClearColor
		|| RenderTarget->GetResource() == nullptr;
	if (bNeedsResourceUpdate)
	{
		RenderTarget->ClearColor = ClearColor;
		RenderTarget->RenderTargetFormat = Format;
		RenderTarget->InitAutoFormat(TextureSize, TextureSize);
		RenderTarget->UpdateResourceImmediate(true);
	}

	if (!RenderTarget->GameThread_GetRenderTargetResource())
	{
		OutError = TEXT("Failed to initialize the reusable preview render target.");
		return false;
	}

	return true;
}
