#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "ItemIconBatchJob.h"
#include "ItemIconGeneratorTypes.h"
#include "ItemIconPreviewCache.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class SImage;
class SProgressBar;
class UStaticMesh;

struct FItemIconGeneratorQueueEntry
{
	TSoftObjectPtr<UStaticMesh> StaticMesh;
	FString OutputAssetName;
	FString Status = TEXT("대기");
	FItemIconCaptureSettings OverrideSettings;
	FString LastPreviewCacheKey;
	bool bIncluded = true;
	bool bUseOverrides = false;
	bool bCustomOutputName = false;
};

using FItemIconGeneratorQueueEntryPtr = TSharedPtr<FItemIconGeneratorQueueEntry>;

class SItemIconGeneratorPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SItemIconGeneratorPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SItemIconGeneratorPanel() override;
	virtual void Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime) override;

private:
	TSharedRef<ITableRow> GenerateQueueRow(
		FItemIconGeneratorQueueEntryPtr Item,
		const TSharedRef<STableViewBase>& OwnerTable);
	void HandleQueueSelectionChanged(FItemIconGeneratorQueueEntryPtr Item, ESelectInfo::Type SelectInfo);

	FReply AddContentBrowserSelection();
	FReply RemoveSelectedItems();
	FReply ClearItems();
	FReply PreviewSelected(bool bForceRefresh);
	FReply SaveSelectedPreview();
	FReply GenerateAll();
	FReply RetryFailed();
	FReply CancelBatch();

	void StartBatchForIndices(const TArray<int32>& QueueIndices);
	void PollBatchState();
	void RefreshPreviewForSelection();
	void SetPreviewTexture(UTexture2D* Texture);
	void ClearPreview(const FString& Message);
	void MarkAllPreviewsStale();
	void MarkSelectedPreviewStale();
	void RequestAutoPreview(float DelaySeconds = 0.12f);
	void HandlePreviewDrag(FVector2D CursorDelta);
	void HandlePreviewZoom(float WheelDelta);
	void HandlePreviewKey(FKey Key);
	void EnsureSelectedOverrideSettings();
	void RefreshGeneratedNames();
	void ModifySelectedCaptureSettings(TFunctionRef<void(FItemIconCaptureSettings&)> Modifier);

	FItemIconGeneratorQueueEntryPtr GetSelectedItem() const;
	FItemIconCaptureSettings GetEffectiveCaptureSettings(const FItemIconGeneratorQueueEntry& Item) const;
	FItemIconCaptureSettings GetPreviewCaptureSettings(const FItemIconGeneratorQueueEntry& Item) const;
	FString MakeOutputName(const FString& MeshName) const;
	bool IsBatchRunning() const;
	bool HasFailedItems() const;

	FText GetQueueCountText() const;
	FText GetSelectedItemText() const;
	FText GetProgressText() const;
	FText GetSummaryText() const;
	TOptional<float> GetProgressFraction() const;

	TArray<FItemIconGeneratorQueueEntryPtr> QueueItems;
	TSharedPtr<SListView<FItemIconGeneratorQueueEntryPtr>> QueueList;
	TWeakPtr<FItemIconGeneratorQueueEntry> SelectedItem;
	TArray<int32> ActiveBatchQueueIndices;
	FItemIconBatchProgress DisplayedBatchProgress;

	TStrongObjectPtr<UItemIconPreviewCache> PreviewCache;
	TStrongObjectPtr<UItemIconBatchJob> ActiveBatchJob;
	FSlateBrush PreviewBrush;
	TSharedPtr<SImage> PreviewImage;

	FItemIconCaptureSettings DefaultCaptureSettings;
	FString OutputFolder = TEXT("/Game/Generated/ItemIcons");
	FString NamingPattern = TEXT("T_Icon_{MeshName}");
	FString PreviewMessage = TEXT("메시를 선택하면 미리보기가 자동으로 생성됩니다.");
	FString SummaryMessage = TEXT("콘텐츠 브라우저에서 스태틱 메시를 추가하세요.");
	bool bOverwriteExisting = false;
	bool bBatchCompletionHandled = true;
	bool bAutoPreviewPending = false;
	float AutoPreviewDelayRemaining = 0.0f;
};
