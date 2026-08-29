#include "ItemIconBatchJob.h"

#include "AssetCompilingManager.h"
#include "Engine/StaticMesh.h"
#include "ItemIconGeneratorLibrary.h"

bool UItemIconBatchJob::Start(const FItemIconBatchRequest& Request)
{
	if (!IsInGameThread() || IsRunning() || Request.Items.IsEmpty())
	{
		return false;
	}

	BatchRequest = Request;
	LoadedMeshes.Reset();
	LoadedMeshes.SetNum(Request.Items.Num());
	Result = FItemIconBatchResult();
	Result.Items.SetNum(Request.Items.Num());
	Progress = FItemIconBatchProgress();
	Progress.State = EItemIconBatchState::Preparing;
	Progress.TotalItems = Request.Items.Num();
	NextItemIndex = 0;
	bCancellationRequested = false;

	for (int32 ItemIndex = 0; ItemIndex < Request.Items.Num(); ++ItemIndex)
	{
		FItemIconBatchItemResult& ItemResult = Result.Items[ItemIndex];
		ItemResult.RequestIndex = ItemIndex;
		ItemResult.StaticMeshPath = Request.Items[ItemIndex].StaticMesh.ToSoftObjectPath();
	}

	if (!IsRooted())
	{
		AddToRoot();
		bRootedForExecution = true;
	}

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UItemIconBatchJob::Tick));
	BroadcastProgress();
	return true;
}

void UItemIconBatchJob::RequestCancel()
{
	if (!IsInGameThread() || !IsRunning())
	{
		return;
	}

	bCancellationRequested = true;
	Progress.State = EItemIconBatchState::Cancelling;
	BroadcastProgress();
}

bool UItemIconBatchJob::IsRunning() const
{
	return Progress.State == EItemIconBatchState::Preparing
		|| Progress.State == EItemIconBatchState::Running
		|| Progress.State == EItemIconBatchState::Cancelling;
}

void UItemIconBatchJob::BeginDestroy()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	Super::BeginDestroy();
}

bool UItemIconBatchJob::Tick(float DeltaTime)
{
	(void)DeltaTime;

	if (bCancellationRequested)
	{
		MarkRemainingCancelled();
		FinishBatch(true, false);
		return false;
	}

	if (Progress.State == EItemIconBatchState::Preparing)
	{
		PrepareBatch();
		return IsRunning();
	}

	if (Progress.State == EItemIconBatchState::Running)
	{
		ProcessNextItem();
		return IsRunning();
	}

	return false;
}

void UItemIconBatchJob::PrepareBatch()
{
	for (int32 ItemIndex = 0; ItemIndex < BatchRequest.Items.Num(); ++ItemIndex)
	{
		LoadedMeshes[ItemIndex] = BatchRequest.Items[ItemIndex].StaticMesh.LoadSynchronous();
	}

	// A batch pays the asset/material compilation cost once instead of once per icon.
	FAssetCompilingManager::Get().FinishAllCompilation();
	Progress.State = EItemIconBatchState::Running;
	BroadcastProgress();
}

void UItemIconBatchJob::ProcessNextItem()
{
	if (!Result.Items.IsValidIndex(NextItemIndex))
	{
		FinishBatch(false, false);
		return;
	}

	const int32 ItemIndex = NextItemIndex++;
	FItemIconBatchItemResult& ItemResult = Result.Items[ItemIndex];
	ItemResult.Status = EItemIconBatchItemStatus::Running;
	Progress.CurrentItemIndex = ItemIndex;
	BroadcastProgress();

	UStaticMesh* StaticMesh = LoadedMeshes[ItemIndex];
	if (StaticMesh)
	{
		const FItemIconGenerationRequest& Request = BatchRequest.Items[ItemIndex];
		ItemResult.GenerationResult = UItemIconGeneratorLibrary::GenerateIconFromPreparedStaticMesh(
			StaticMesh,
			Request.CaptureSettings,
			Request.SaveSettings);
	}
	else
	{
		ItemResult.GenerationResult.Message = ItemResult.StaticMeshPath.IsValid()
			? FString::Printf(TEXT("Failed to load StaticMesh %s."), *ItemResult.StaticMeshPath.ToString())
			: TEXT("StaticMesh is required.");
	}

	if (ItemResult.GenerationResult.bSuccess)
	{
		ItemResult.Status = EItemIconBatchItemStatus::Succeeded;
		++Progress.SucceededItems;
	}
	else
	{
		ItemResult.Status = EItemIconBatchItemStatus::Failed;
		++Progress.FailedItems;
	}

	++Progress.CompletedItems;
	Progress.CurrentItemIndex = INDEX_NONE;
	OnItemCompleted.Broadcast(ItemResult);
	BroadcastProgress();

	if (!ItemResult.GenerationResult.bSuccess && !BatchRequest.bContinueOnFailure)
	{
		MarkRemainingCancelled();
		FinishBatch(false, true);
		return;
	}

	if (NextItemIndex >= BatchRequest.Items.Num())
	{
		FinishBatch(false, false);
	}
}

void UItemIconBatchJob::MarkRemainingCancelled()
{
	for (int32 ItemIndex = NextItemIndex; ItemIndex < Result.Items.Num(); ++ItemIndex)
	{
		FItemIconBatchItemResult& ItemResult = Result.Items[ItemIndex];
		if (ItemResult.Status == EItemIconBatchItemStatus::Pending)
		{
			ItemResult.Status = EItemIconBatchItemStatus::Cancelled;
			ItemResult.GenerationResult.Message = TEXT("Cancelled before generation started.");
			++Progress.CancelledItems;
		}
	}
}

void UItemIconBatchJob::FinishBatch(const bool bWasCancelled, const bool bStoppedOnFailure)
{
	Progress.State = EItemIconBatchState::Completed;
	Progress.CurrentItemIndex = INDEX_NONE;
	Result.bCancelled = bWasCancelled;
	Result.bStoppedOnFailure = bStoppedOnFailure;
	TickerHandle.Reset();
	BroadcastProgress();
	OnCompleted.Broadcast(Result);

	// Completion listeners may inspect the generated textures during the callback. Once every
	// listener has returned, retain only the lightweight status, path, and message metadata.
	for (FItemIconBatchItemResult& ItemResult : Result.Items)
	{
		ItemResult.GenerationResult.Texture = nullptr;
	}

	LoadedMeshes.Reset();
	BatchRequest = FItemIconBatchRequest();
	if (bRootedForExecution)
	{
		RemoveFromRoot();
		bRootedForExecution = false;
	}
}

void UItemIconBatchJob::BroadcastProgress()
{
	OnProgressUpdated.Broadcast(Progress);
}
