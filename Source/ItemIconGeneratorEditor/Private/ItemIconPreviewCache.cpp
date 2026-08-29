#include "ItemIconPreviewCache.h"

#include "AssetCompilingManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "ItemIconGeneratorLibrary.h"
#include "ItemIconPreviewRenderer.h"
#include "Misc/SecureHash.h"

namespace ItemIconPreviewCache
{
	FItemIconPreviewResult MakeFailure(FString Message, FString CacheKey = FString())
	{
		FItemIconPreviewResult Result;
		Result.CacheKey = MoveTemp(CacheKey);
		Result.Message = MoveTemp(Message);
		return Result;
	}

	FItemIconCaptureSettings NormalizeSettings(FItemIconCaptureSettings Settings)
	{
		Settings.TextureSize = FMath::Clamp(Settings.TextureSize, 64, 4096);
		Settings.CameraDistanceMultiplier = FMath::Max(Settings.CameraDistanceMultiplier, 0.01f);
		Settings.FieldOfView = FMath::Clamp(Settings.FieldOfView, 5.0f, 170.0f);
		Settings.CameraPitch = FMath::Clamp(Settings.CameraPitch, -89.0f, 89.0f);
		Settings.CameraYaw = FRotator::NormalizeAxis(Settings.CameraYaw);
		Settings.DirectionalLightIntensity = FMath::Max(Settings.DirectionalLightIntensity, 0.0f);
		Settings.FillLightIntensity = FMath::Max(Settings.FillLightIntensity, 0.0f);
		Settings.BackLightIntensity = FMath::Max(Settings.BackLightIntensity, 0.0f);
		Settings.SkyLightIntensity = FMath::Max(Settings.SkyLightIntensity, 0.0f);
		Settings.ExposureCompensation = FMath::Clamp(Settings.ExposureCompensation, -15.0f, 15.0f);
		Settings.ColorSaturation = FMath::Clamp(Settings.ColorSaturation, 0.0f, 2.0f);
		return Settings;
	}
}

UItemIconPreviewCache* UItemIconPreviewCache::CreatePreviewCache()
{
	return NewObject<UItemIconPreviewCache>(GetTransientPackage(), NAME_None, RF_Transient);
}

void UItemIconPreviewCache::BeginDestroy()
{
	ReleaseRenderResources();
	Super::BeginDestroy();
}

void UItemIconPreviewCache::ReleaseRenderResources()
{
	delete PreviewRenderer;
	PreviewRenderer = nullptr;
}

FItemIconPreviewResult UItemIconPreviewCache::CapturePreview(
	UStaticMesh* StaticMesh,
	FItemIconCaptureSettings CaptureSettings,
	const bool bForceRefresh)
{
	if (!IsInGameThread())
	{
		return ItemIconPreviewCache::MakeFailure(TEXT("Preview capture must run on the game thread."));
	}

	if (!StaticMesh)
	{
		return ItemIconPreviewCache::MakeFailure(TEXT("StaticMesh is required."));
	}

	CaptureSettings = ItemIconPreviewCache::NormalizeSettings(CaptureSettings);
	const FString CacheKey = MakePreviewCacheKey(StaticMesh, CaptureSettings);
	if (!bForceRefresh)
	{
		if (FItemIconPreviewCacheEntry* CachedEntry = Entries.Find(CacheKey))
		{
			Touch(*CachedEntry);
			return MakeResult(CacheKey, true, TEXT("Preview returned from cache."));
		}
	}

	if (FAssetCompilingManager::Get().GetNumRemainingAssets() > 0)
	{
		FAssetCompilingManager::Get().FinishAllCompilation();
	}
	TArray<FColor> Pixels;
	FString CaptureError;
	if (!PreviewRenderer)
	{
		PreviewRenderer = new FItemIconPreviewRenderer();
	}
	if (!PreviewRenderer->Capture(StaticMesh, CaptureSettings, Pixels, CaptureError))
	{
		return ItemIconPreviewCache::MakeFailure(MoveTemp(CaptureError), CacheKey);
	}

	const TConstArrayView64<uint8> PixelBytes(
		reinterpret_cast<const uint8*>(Pixels.GetData()),
		static_cast<int64>(Pixels.Num()) * sizeof(FColor));
	UTexture2D* PreviewTexture = UTexture2D::CreateTransient(
		CaptureSettings.TextureSize,
		CaptureSettings.TextureSize,
		PF_B8G8R8A8,
		NAME_None,
		PixelBytes);
	if (!PreviewTexture)
	{
		return ItemIconPreviewCache::MakeFailure(TEXT("Failed to create the transient preview texture."), CacheKey);
	}

	PreviewTexture->SRGB = true;
	PreviewTexture->NeverStream = true;
	PreviewTexture->AddressX = TA_Clamp;
	PreviewTexture->AddressY = TA_Clamp;
	PreviewTexture->UpdateResource();

	FItemIconPreviewCacheEntry& Entry = Entries.FindOrAdd(CacheKey);
	Entry.StaticMesh = StaticMesh;
	Entry.Texture = PreviewTexture;
	Entry.Pixels = MoveTemp(Pixels);
	Entry.TextureSize = CaptureSettings.TextureSize;
	Touch(Entry);
	EvictToLimit();

	return MakeResult(CacheKey, false, TEXT("Preview captured and cached."));
}

FItemIconPreviewResult UItemIconPreviewCache::FindPreview(
	UStaticMesh* StaticMesh,
	FItemIconCaptureSettings CaptureSettings)
{
	if (!StaticMesh)
	{
		return ItemIconPreviewCache::MakeFailure(TEXT("StaticMesh is required."));
	}

	CaptureSettings = ItemIconPreviewCache::NormalizeSettings(CaptureSettings);
	const FString CacheKey = MakePreviewCacheKey(StaticMesh, CaptureSettings);
	FItemIconPreviewCacheEntry* Entry = Entries.Find(CacheKey);
	if (!Entry)
	{
		return ItemIconPreviewCache::MakeFailure(TEXT("No cached preview matches the mesh and settings."), CacheKey);
	}

	Touch(*Entry);
	return MakeResult(CacheKey, true, TEXT("Preview found in cache."));
}

FItemIconGenerationResult UItemIconPreviewCache::SavePreview(
	const FString& CacheKey,
	FItemIconSaveSettings SaveSettings)
{
	FItemIconPreviewCacheEntry* Entry = Entries.Find(CacheKey);
	if (!Entry || !Entry->StaticMesh || Entry->Pixels.IsEmpty())
	{
		FItemIconGenerationResult Result;
		Result.Message = TEXT("The preview cache entry is missing or no longer valid.");
		return Result;
	}

	Touch(*Entry);
	return UItemIconGeneratorLibrary::SaveCapturedPixelsAsTexture(
		Entry->StaticMesh,
		Entry->Pixels,
		Entry->TextureSize,
		SaveSettings);
}

int32 UItemIconPreviewCache::InvalidateMesh(UStaticMesh* StaticMesh)
{
	if (!StaticMesh)
	{
		return 0;
	}

	int32 RemovedCount = 0;
	for (auto Iterator = Entries.CreateIterator(); Iterator; ++Iterator)
	{
		if (Iterator.Value().StaticMesh == StaticMesh)
		{
			Iterator.RemoveCurrent();
			++RemovedCount;
		}
	}
	return RemovedCount;
}

bool UItemIconPreviewCache::InvalidatePreview(const FString& CacheKey)
{
	return Entries.Remove(CacheKey) > 0;
}

void UItemIconPreviewCache::Clear()
{
	Entries.Reset();
}

void UItemIconPreviewCache::SetMaximumEntries(const int32 InMaximumEntries)
{
	MaximumEntries = FMath::Clamp(InMaximumEntries, 1, 256);
	EvictToLimit();
}

FString UItemIconPreviewCache::MakePreviewCacheKey(
	UStaticMesh* StaticMesh,
	FItemIconCaptureSettings CaptureSettings)
{
	if (!StaticMesh)
	{
		return FString();
	}

	CaptureSettings = ItemIconPreviewCache::NormalizeSettings(CaptureSettings);
	FString Signature = FString::Printf(
		TEXT("%s|%.17g|%.17g|%.17g|%.17g|%.17g|%.17g|%.17g|%.17g|%.17g|%.17g|%.17g|%d|%d|%.9g|%.9g|%.9g|%.9g|%.9g|%.17g|%.17g|%.17g|%.9g"),
		*StaticMesh->GetPathName(),
		CaptureSettings.MeshRotation.Pitch,
		CaptureSettings.MeshRotation.Yaw,
		CaptureSettings.MeshRotation.Roll,
		CaptureSettings.MeshLocationOffset.X,
		CaptureSettings.MeshLocationOffset.Y,
		CaptureSettings.MeshLocationOffset.Z,
		CaptureSettings.CameraTargetOffset.X,
		CaptureSettings.CameraTargetOffset.Y,
		CaptureSettings.CameraTargetOffset.Z,
		CaptureSettings.CameraDistanceMultiplier,
		CaptureSettings.FieldOfView,
		CaptureSettings.TextureSize,
		CaptureSettings.bTransparentBackground ? 1 : 0,
		CaptureSettings.BackgroundColor.R,
		CaptureSettings.BackgroundColor.G,
		CaptureSettings.BackgroundColor.B,
		CaptureSettings.BackgroundColor.A,
		CaptureSettings.DirectionalLightIntensity,
		CaptureSettings.DirectionalLightRotation.Pitch,
		CaptureSettings.DirectionalLightRotation.Yaw,
		CaptureSettings.DirectionalLightRotation.Roll,
		CaptureSettings.SkyLightIntensity);
	Signature += FString::Printf(
		TEXT("|%.9g|%.9g"),
		CaptureSettings.CameraPitch,
		CaptureSettings.CameraYaw);
	Signature += FString::Printf(
		TEXT("|%.9g|%.9g|%.9g|%.9g|%.9g|%.17g|%.17g|%.17g|%.9g|%.9g|%.9g|%.9g|%.17g|%.17g|%.17g|%.9g|%.9g|%.9g|%.9g|%.9g|%.9g|%.9g"),
		CaptureSettings.DirectionalLightColor.R,
		CaptureSettings.DirectionalLightColor.G,
		CaptureSettings.DirectionalLightColor.B,
		CaptureSettings.DirectionalLightColor.A,
		CaptureSettings.FillLightIntensity,
		CaptureSettings.FillLightRotation.Pitch,
		CaptureSettings.FillLightRotation.Yaw,
		CaptureSettings.FillLightRotation.Roll,
		CaptureSettings.FillLightColor.R,
		CaptureSettings.FillLightColor.G,
		CaptureSettings.FillLightColor.B,
		CaptureSettings.FillLightColor.A,
		CaptureSettings.BackLightIntensity,
		CaptureSettings.BackLightRotation.Pitch,
		CaptureSettings.BackLightRotation.Yaw,
		CaptureSettings.BackLightRotation.Roll,
		CaptureSettings.BackLightColor.R,
		CaptureSettings.BackLightColor.G,
		CaptureSettings.BackLightColor.B,
		CaptureSettings.BackLightColor.A,
		CaptureSettings.ExposureCompensation,
		CaptureSettings.ColorSaturation);
	const FTCHARToUTF8 Utf8Signature(*Signature);
	return FMD5::HashBytes(
		reinterpret_cast<const uint8*>(Utf8Signature.Get()),
		Utf8Signature.Length());
}

FItemIconPreviewResult UItemIconPreviewCache::MakeResult(
	const FString& CacheKey,
	const bool bFromCache,
	const FString& Message)
{
	FItemIconPreviewResult Result;
	FItemIconPreviewCacheEntry* Entry = Entries.Find(CacheKey);
	if (!Entry || !Entry->Texture)
	{
		Result.CacheKey = CacheKey;
		Result.Message = TEXT("The preview cache entry is missing or invalid.");
		return Result;
	}

	Result.bSuccess = true;
	Result.bFromCache = bFromCache;
	Result.CacheKey = CacheKey;
	Result.Texture = Entry->Texture;
	Result.TextureSize = Entry->TextureSize;
	Result.Message = Message;
	return Result;
}

void UItemIconPreviewCache::Touch(FItemIconPreviewCacheEntry& Entry)
{
	Entry.LastAccessSerial = ++AccessSerial;
}

void UItemIconPreviewCache::EvictToLimit()
{
	while (Entries.Num() > MaximumEntries)
	{
		FString OldestKey;
		int64 OldestSerial = MAX_int64;
		for (const TPair<FString, FItemIconPreviewCacheEntry>& Pair : Entries)
		{
			if (Pair.Value.LastAccessSerial < OldestSerial)
			{
				OldestSerial = Pair.Value.LastAccessSerial;
				OldestKey = Pair.Key;
			}
		}

		if (OldestKey.IsEmpty())
		{
			break;
		}
		Entries.Remove(OldestKey);
	}
}
