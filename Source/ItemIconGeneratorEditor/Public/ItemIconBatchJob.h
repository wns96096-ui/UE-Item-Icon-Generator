#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "ItemIconGeneratorTypes.h"
#include "ItemIconBatchJob.generated.h"

class UStaticMesh;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FItemIconBatchItemCompleted,
	const FItemIconBatchItemResult&,
	ItemResult);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FItemIconBatchProgressUpdated,
	const FItemIconBatchProgress&,
	Progress);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FItemIconBatchCompleted,
	const FItemIconBatchResult&,
	BatchResult);

/**
 * Editor-only batch runner that prepares assets once and generates at most one icon per editor tick.
 * A failed item is isolated by default, and cancellation takes effect before the next item starts.
 */
UCLASS(BlueprintType)
class ITEMICONGENERATOREDITOR_API UItemIconBatchJob : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Item Icon|Batch")
	FItemIconBatchItemCompleted OnItemCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Item Icon|Batch")
	FItemIconBatchProgressUpdated OnProgressUpdated;

	UPROPERTY(BlueprintAssignable, Category = "Item Icon|Batch")
	FItemIconBatchCompleted OnCompleted;

	/** Starts a new queue. Returns false when already running, off the game thread, or empty. */
	UFUNCTION(BlueprintCallable, Category = "Item Icon Generator|Batch")
	bool Start(const FItemIconBatchRequest& Request);

	/** Stops safely before the next queued item begins. The current synchronous capture is not interrupted. */
	UFUNCTION(BlueprintCallable, Category = "Item Icon Generator|Batch")
	void RequestCancel();

	UFUNCTION(BlueprintPure, Category = "Item Icon Generator|Batch")
	bool IsRunning() const;

	UFUNCTION(BlueprintPure, Category = "Item Icon Generator|Batch")
	const FItemIconBatchProgress& GetProgress() const { return Progress; }

	UFUNCTION(BlueprintPure, Category = "Item Icon Generator|Batch")
	const FItemIconBatchResult& GetResult() const { return Result; }

	virtual void BeginDestroy() override;

private:
	bool Tick(float DeltaTime);
	void PrepareBatch();
	void ProcessNextItem();
	void MarkRemainingCancelled();
	void FinishBatch(bool bWasCancelled, bool bStoppedOnFailure);
	void BroadcastProgress();

	UPROPERTY(Transient)
	FItemIconBatchRequest BatchRequest;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> LoadedMeshes;

	UPROPERTY(Transient)
	FItemIconBatchProgress Progress;

	UPROPERTY(Transient)
	FItemIconBatchResult Result;

	FTSTicker::FDelegateHandle TickerHandle;
	int32 NextItemIndex = 0;
	bool bCancellationRequested = false;
	bool bRootedForExecution = false;
};
