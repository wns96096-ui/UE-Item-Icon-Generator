#include "ItemIconGeneratorLibrary.h"

#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "FileHelpers.h"
#include "ItemIconPreviewRenderer.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace ItemIconGenerator
{
	FItemIconGenerationResult MakeFailure(FString Message)
	{
		FItemIconGenerationResult Result;
		Result.Message = MoveTemp(Message);
		return Result;
	}

	FItemIconGenerationResult SavePixelsAsTexture(
		UStaticMesh* StaticMesh,
		const TArray<FColor>& Pixels,
		const int32 TextureSize,
		const FItemIconSaveSettings& Settings)
	{
		FString PackagePath = Settings.PackagePath;
		PackagePath.TrimStartAndEndInline();
		PackagePath.RemoveFromEnd(TEXT("/"));

		if (PackagePath != TEXT("/Game") && !PackagePath.StartsWith(TEXT("/Game/")))
		{
			return MakeFailure(TEXT("PackagePath must be /Game or a folder below /Game."));
		}

		const FString RequestedAssetName = Settings.AssetName.IsEmpty()
			? FString::Printf(TEXT("T_Icon_%s"), *StaticMesh->GetName())
			: Settings.AssetName;
		const FString AssetName = ObjectTools::SanitizeObjectName(RequestedAssetName);
		if (AssetName.IsEmpty())
		{
			return MakeFailure(TEXT("AssetName is empty after sanitization."));
		}

		const FString FullPackageName = FString::Printf(TEXT("%s/%s"), *PackagePath, *AssetName);
		if (!FPackageName::IsValidLongPackageName(FullPackageName))
		{
			return MakeFailure(FString::Printf(TEXT("Invalid package name: %s"), *FullPackageName));
		}

		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *FullPackageName, *AssetName);
		UObject* ExistingObject = FindObject<UObject>(nullptr, *ObjectPath);
		if (!ExistingObject && FPackageName::DoesPackageExist(FullPackageName))
		{
			ExistingObject = LoadObject<UObject>(nullptr, *ObjectPath);
		}
		if (ExistingObject && !Settings.bOverwriteExisting)
		{
			return MakeFailure(FString::Printf(TEXT("An asset already exists at %s. Enable overwrite to replace it."), *ObjectPath));
		}

		UTexture2D* ExistingTexture = Cast<UTexture2D>(ExistingObject);
		if (ExistingObject && !ExistingTexture)
		{
			return MakeFailure(FString::Printf(TEXT("The existing asset at %s is not a Texture2D."), *ObjectPath));
		}

		UPackage* Package = CreatePackage(*FullPackageName);
		if (!Package)
		{
			return MakeFailure(FString::Printf(TEXT("Failed to create package %s."), *FullPackageName));
		}
		Package->FullyLoad();

		const bool bCreatedNewTexture = ExistingTexture == nullptr;
		UTexture2D* Texture = ExistingTexture;
		if (!Texture)
		{
			Texture = NewObject<UTexture2D>(
				Package,
				*AssetName,
				RF_Public | RF_Standalone | RF_Transactional);
		}

		if (!Texture)
		{
			return MakeFailure(FString::Printf(TEXT("Failed to create texture %s."), *ObjectPath));
		}

		Texture->Modify();
		Texture->PreEditChange(nullptr);
		Texture->SRGB = true;
		Texture->CompressionSettings = TC_Default;
		Texture->MipGenSettings = TMGS_NoMipmaps;
		Texture->Source.Init(
			TextureSize,
			TextureSize,
			1,
			1,
			TSF_BGRA8,
			reinterpret_cast<const uint8*>(Pixels.GetData()));
		Texture->PostEditChange();
		Texture->MarkPackageDirty();
		Package->MarkPackageDirty();

		if (bCreatedNewTexture)
		{
			FAssetRegistryModule::AssetCreated(Texture);
		}

		FItemIconGenerationResult Result;
		Result.Texture = Texture;
		Result.ObjectPath = Texture->GetPathName();

		if (Settings.bSaveImmediately && !UEditorLoadingAndSavingUtils::SavePackages({Package}, true))
		{
			Result.Message = FString::Printf(TEXT("Texture was generated but package save failed: %s"), *FullPackageName);
			return Result;
		}

		Result.bSuccess = true;
		Result.Message = Settings.bSaveImmediately
			? TEXT("Icon texture generated and saved.")
			: TEXT("Icon texture generated and left dirty for later saving.");
		return Result;
	}
}

FItemIconGenerationResult UItemIconGeneratorLibrary::GenerateIconFromStaticMesh(
	UStaticMesh* StaticMesh,
	FItemIconCaptureSettings CaptureSettings,
	FItemIconSaveSettings SaveSettings)
{
	if (!IsInGameThread())
	{
		return ItemIconGenerator::MakeFailure(TEXT("Icon generation must run on the game thread."));
	}

	if (!StaticMesh)
	{
		return ItemIconGenerator::MakeFailure(TEXT("StaticMesh is required."));
	}

	// Icon generation is a synchronous editor operation. Never capture fallback materials or incomplete meshes.
	FAssetCompilingManager::Get().FinishAllCompilation();
	return GenerateIconFromPreparedStaticMesh(StaticMesh, CaptureSettings, SaveSettings);
}

FItemIconGenerationResult UItemIconGeneratorLibrary::GenerateIconFromPreparedStaticMesh(
	UStaticMesh* StaticMesh,
	FItemIconCaptureSettings CaptureSettings,
	FItemIconSaveSettings SaveSettings)
{
	if (!IsInGameThread())
	{
		return ItemIconGenerator::MakeFailure(TEXT("Icon generation must run on the game thread."));
	}

	if (!StaticMesh)
	{
		return ItemIconGenerator::MakeFailure(TEXT("StaticMesh is required."));
	}

	TArray<FColor> Pixels;
	FString CaptureError;
	if (!CapturePixelsFromPreparedStaticMesh(StaticMesh, CaptureSettings, Pixels, CaptureError))
	{
		return ItemIconGenerator::MakeFailure(MoveTemp(CaptureError));
	}

	return SaveCapturedPixelsAsTexture(StaticMesh, Pixels, CaptureSettings.TextureSize, SaveSettings);
}

bool UItemIconGeneratorLibrary::CapturePixelsFromPreparedStaticMesh(
	UStaticMesh* StaticMesh,
	FItemIconCaptureSettings CaptureSettings,
	TArray<FColor>& OutPixels,
	FString& OutError)
{
	if (!IsInGameThread())
	{
		OutError = TEXT("Icon capture must run on the game thread.");
		return false;
	}

	if (!StaticMesh)
	{
		OutError = TEXT("StaticMesh is required.");
		return false;
	}

	TStrongObjectPtr<UStaticMesh> StaticMeshGuard(StaticMesh);
	CaptureSettings.TextureSize = FMath::Clamp(CaptureSettings.TextureSize, 64, 4096);
	FItemIconPreviewRenderer Renderer;
	return Renderer.Capture(StaticMeshGuard.Get(), CaptureSettings, OutPixels, OutError);
}

FItemIconGenerationResult UItemIconGeneratorLibrary::SaveCapturedPixelsAsTexture(
	UStaticMesh* StaticMesh,
	const TArray<FColor>& Pixels,
	const int32 TextureSize,
	FItemIconSaveSettings SaveSettings)
{
	if (!IsInGameThread())
	{
		return ItemIconGenerator::MakeFailure(TEXT("Icon saving must run on the game thread."));
	}

	if (!StaticMesh)
	{
		return ItemIconGenerator::MakeFailure(TEXT("StaticMesh is required."));
	}

	const int32 ClampedTextureSize = FMath::Clamp(TextureSize, 64, 4096);
	if (Pixels.Num() != ClampedTextureSize * ClampedTextureSize)
	{
		return ItemIconGenerator::MakeFailure(TEXT("Captured pixel count does not match TextureSize."));
	}

	TStrongObjectPtr<UStaticMesh> StaticMeshGuard(StaticMesh);
	return ItemIconGenerator::SavePixelsAsTexture(
		StaticMeshGuard.Get(),
		Pixels,
		ClampedTextureSize,
		SaveSettings);
}
