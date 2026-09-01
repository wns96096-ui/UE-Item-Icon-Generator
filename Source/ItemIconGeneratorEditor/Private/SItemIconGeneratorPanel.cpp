#include "SItemIconGeneratorPanel.h"

#include "AssetRegistry/AssetData.h"
#include "ContentBrowserModule.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "IContentBrowserSingleton.h"
#include "InputCoreTypes.h"
#include "ItemIconBatchJob.h"
#include "ItemIconGeneratorLibrary.h"
#include "ItemIconPreviewCache.h"
#include "Styling/AppStyle.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SItemIconGeneratorPanel"

namespace ItemIconGeneratorPanel
{
	const FName IncludeColumn(TEXT("Include"));
	const FName MeshColumn(TEXT("Mesh"));
	const FName OutputColumn(TEXT("Output"));
	const FName StatusColumn(TEXT("Status"));

	DECLARE_DELEGATE_OneParam(FOnPreviewDragged, FVector2D)
	DECLARE_DELEGATE_OneParam(FOnPreviewZoomed, float)
	DECLARE_DELEGATE_OneParam(FOnPreviewKeyPressed, FKey)

	class SInteractivePreview final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SInteractivePreview) {}
			SLATE_DEFAULT_SLOT(FArguments, Content)
			SLATE_EVENT(FOnPreviewDragged, OnDragged)
			SLATE_EVENT(FOnPreviewZoomed, OnZoomed)
			SLATE_EVENT(FOnPreviewKeyPressed, OnKeyPressed)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			OnDragged = InArgs._OnDragged;
			OnZoomed = InArgs._OnZoomed;
			OnKeyPressed = InArgs._OnKeyPressed;
			ChildSlot[InArgs._Content.Widget];
		}

		virtual bool SupportsKeyboardFocus() const override
		{
			return true;
		}

		virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			(void)MyGeometry;
			if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
			{
				return FReply::Handled()
					.CaptureMouse(SharedThis(this))
					.SetUserFocus(SharedThis(this), EFocusCause::Mouse);
			}
			return FReply::Unhandled();
		}

		virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			(void)MyGeometry;
			if (HasMouseCapture() && OnDragged.IsBound())
			{
				OnDragged.Execute(MouseEvent.GetCursorDelta());
				return FReply::Handled();
			}
			return FReply::Unhandled();
		}

		virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			(void)MyGeometry;
			if (HasMouseCapture() && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
			{
				return FReply::Handled().ReleaseMouseCapture();
			}
			return FReply::Unhandled();
		}

		virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			(void)MyGeometry;
			if (OnZoomed.IsBound())
			{
				OnZoomed.Execute(MouseEvent.GetWheelDelta());
				return FReply::Handled();
			}
			return FReply::Unhandled();
		}

		virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent) override
		{
			(void)MyGeometry;
			const FKey Key = KeyEvent.GetKey();
			if (OnKeyPressed.IsBound() && (Key == EKeys::W || Key == EKeys::A || Key == EKeys::S || Key == EKeys::D))
			{
				OnKeyPressed.Execute(Key);
				return FReply::Handled();
			}
			return FReply::Unhandled();
		}

	private:
		FOnPreviewDragged OnDragged;
		FOnPreviewZoomed OnZoomed;
		FOnPreviewKeyPressed OnKeyPressed;
	};

	FSlateColor GetStatusColor(const FString& Status)
	{
		if (Status == TEXT("저장 완료") || Status == TEXT("미리보기 준비"))
		{
			return FSlateColor(FLinearColor(0.25f, 0.8f, 0.42f));
		}
		if (Status == TEXT("실패"))
		{
			return FSlateColor(FLinearColor(0.95f, 0.28f, 0.24f));
		}
		if (Status == TEXT("생성 중") || Status == TEXT("준비 중"))
		{
			return FSlateColor(FLinearColor(0.25f, 0.6f, 1.0f));
		}
		if (Status == TEXT("취소됨") || Status == TEXT("미리보기 갱신 필요"))
		{
			return FSlateColor(FLinearColor(0.95f, 0.65f, 0.2f));
		}
		return FSlateColor::UseForeground();
	}

	FRotator RotateMeshInScreenSpace(
		const FRotator& MeshRotation,
		const float CameraPitch,
		const float CameraYaw,
		const FVector2D CursorDelta)
	{
		const FRotationMatrix CameraBasis(FRotator(CameraPitch, CameraYaw, 0.0f));
		const FVector ScreenRight = CameraBasis.GetUnitAxis(EAxis::Y);
		const FVector ScreenUp = CameraBasis.GetUnitAxis(EAxis::Z);
		const FQuat YawDelta(ScreenUp, FMath::DegreesToRadians(CursorDelta.X * 0.45f));
		const FQuat PitchDelta(ScreenRight, FMath::DegreesToRadians(CursorDelta.Y * 0.4f));
		const FQuat DragDelta = (PitchDelta * YawDelta).GetNormalized();
		return (DragDelta * MeshRotation.Quaternion()).GetNormalized().Rotator().GetNormalized();
	}

	class SQueueRow final : public SMultiColumnTableRow<FItemIconGeneratorQueueEntryPtr>
	{
	public:
		SLATE_BEGIN_ARGS(SQueueRow) {}
			SLATE_ARGUMENT(FItemIconGeneratorQueueEntryPtr, Item)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
		{
			Item = InArgs._Item;
			SMultiColumnTableRow<FItemIconGeneratorQueueEntryPtr>::Construct(
				FSuperRowType::FArguments().Padding(FMargin(2.0f, 1.0f)),
				OwnerTable);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
		{
			if (ColumnName == IncludeColumn)
			{
				return SNew(SCheckBox)
					.IsChecked_Lambda([Entry = Item]()
					{
						return Entry->bIncluded ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([Entry = Item](const ECheckBoxState State)
					{
						Entry->bIncluded = State == ECheckBoxState::Checked;
					});
			}

			if (ColumnName == MeshColumn)
			{
				return SNew(STextBlock)
					.Text_Lambda([Entry = Item]()
					{
						return FText::FromString(Entry->StaticMesh.GetAssetName());
					})
					.ToolTipText_Lambda([Entry = Item]()
					{
						return FText::FromString(Entry->StaticMesh.ToSoftObjectPath().ToString());
					});
			}

			if (ColumnName == OutputColumn)
			{
				return SNew(SEditableTextBox)
					.Text_Lambda([Entry = Item]() { return FText::FromString(Entry->OutputAssetName); })
					.OnTextCommitted_Lambda([Entry = Item](const FText& Text, ETextCommit::Type)
					{
						Entry->OutputAssetName = Text.ToString();
						Entry->bCustomOutputName = true;
					});
			}

			return SNew(STextBlock)
				.Text_Lambda([Entry = Item]() { return FText::FromString(Entry->Status); })
				.ColorAndOpacity_Lambda([Entry = Item]() { return GetStatusColor(Entry->Status); });
		}

	private:
		FItemIconGeneratorQueueEntryPtr Item;
	};
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FItemIconScreenSpaceRotationTest,
	"ItemIconGenerator.Preview.ScreenSpaceRotation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FItemIconScreenSpaceRotationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FRotator InitialRotation(15.0f, -30.0f, 5.0f);
	const FRotator NoDragRotation = ItemIconGeneratorPanel::RotateMeshInScreenSpace(
		InitialRotation, -45.0f, 45.0f, FVector2D::ZeroVector);
	TestTrue(TEXT("Zero cursor movement preserves mesh orientation"),
		NoDragRotation.Equals(InitialRotation, KINDA_SMALL_NUMBER));

	const FRotator DraggedRotation = ItemIconGeneratorPanel::RotateMeshInScreenSpace(
		InitialRotation, -45.0f, 45.0f, FVector2D(40.0f, -20.0f));
	TestFalse(TEXT("Cursor movement changes mesh orientation"),
		DraggedRotation.Equals(InitialRotation, KINDA_SMALL_NUMBER));
	const bool bAnglesAreNormalized =
		FMath::Abs(DraggedRotation.Pitch) <= 180.0f &&
		FMath::Abs(DraggedRotation.Yaw) <= 180.0f &&
		FMath::Abs(DraggedRotation.Roll) <= 180.0f;
	TestTrue(TEXT("Result remains a normalized rotator"),
		bAnglesAreNormalized && !DraggedRotation.ContainsNaN());
	return true;
}
#endif

void SItemIconGeneratorPanel::Construct(const FArguments& InArgs)
{
	(void)InArgs;
	PreviewCache.Reset(UItemIconPreviewCache::CreatePreviewCache());
	PreviewCache->SetMaximumEntries(4);
	PreviewBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
	PreviewBrush.ImageSize = FVector2D(512.0f, 512.0f);

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("AddSelection", "콘텐츠 브라우저 선택 추가"))
				.ToolTipText(LOCTEXT("AddSelectionTooltip", "콘텐츠 브라우저에서 선택한 모든 스태틱 메시를 목록에 추가합니다."))
				.IsEnabled_Lambda([this]() { return !IsBatchRunning(); })
				.OnClicked(this, &SItemIconGeneratorPanel::AddContentBrowserSelection)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("RemoveSelection", "선택 항목 제거"))
				.IsEnabled_Lambda([this]() { return !IsBatchRunning() && QueueList.IsValid() && QueueList->GetNumItemsSelected() > 0; })
				.OnClicked(this, &SItemIconGeneratorPanel::RemoveSelectedItems)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("Clear", "전체 비우기"))
				.IsEnabled_Lambda([this]() { return !IsBatchRunning() && !QueueItems.IsEmpty(); })
				.OnClicked(this, &SItemIconGeneratorPanel::ClearItems)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SSpacer)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(this, &SItemIconGeneratorPanel::GetQueueCountText)
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.0f)
		[
			SNew(SGridPanel)
			.FillColumn(1, 1.0f)
			.FillColumn(3, 1.0f)
			+ SGridPanel::Slot(0, 0).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 4.0f)
			[
				SNew(STextBlock).Text(LOCTEXT("OutputFolder", "출력 폴더"))
			]
			+ SGridPanel::Slot(1, 0).Padding(0.0f, 0.0f, 12.0f, 4.0f)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([this]() { return FText::FromString(OutputFolder); })
				.OnTextChanged_Lambda([this](const FText& Text) { OutputFolder = Text.ToString(); })
			]
			+ SGridPanel::Slot(2, 0).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 4.0f)
			[
				SNew(STextBlock).Text(LOCTEXT("NamingPattern", "이름 규칙"))
			]
			+ SGridPanel::Slot(3, 0).Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([this]() { return FText::FromString(NamingPattern); })
				.ToolTipText(LOCTEXT("NamingPatternTooltip", "원본 메시 이름이 들어갈 위치에 {MeshName}을 사용하세요."))
				.OnTextChanged_Lambda([this](const FText& Text)
				{
					NamingPattern = Text.ToString();
					RefreshGeneratedNames();
				})
			]
			+ SGridPanel::Slot(0, 1).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(STextBlock).Text(LOCTEXT("Resolution", "해상도"))
			]
			+ SGridPanel::Slot(1, 1).Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[
				SNew(SNumericEntryBox<int32>)
				.MinValue(64).MaxValue(4096).MinSliderValue(64).MaxSliderValue(2048)
				.Value_Lambda([this]() -> TOptional<int32> { return DefaultCaptureSettings.TextureSize; })
				.OnValueChanged_Lambda([this](const int32 Value)
				{
					DefaultCaptureSettings.TextureSize = FMath::Clamp(Value, 64, 4096);
					MarkAllPreviewsStale();
				})
			]
			+ SGridPanel::Slot(2, 1).Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return DefaultCaptureSettings.bTransparentBackground ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](const ECheckBoxState State)
				{
					DefaultCaptureSettings.bTransparentBackground = State == ECheckBoxState::Checked;
					MarkAllPreviewsStale();
				})
				[
					SNew(STextBlock).Text(LOCTEXT("Transparent", "투명 배경"))
				]
			]
			+ SGridPanel::Slot(3, 1)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bOverwriteExisting ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](const ECheckBoxState State) { bOverwriteExisting = State == ECheckBoxState::Checked; })
				[
					SNew(STextBlock).Text(LOCTEXT("Overwrite", "기존 에셋 덮어쓰기"))
				]
			]
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(6.0f, 0.0f)
		[
			SNew(SSplitter)
			+ SSplitter::Slot().Value(0.62f)
			[
				SAssignNew(QueueList, SListView<FItemIconGeneratorQueueEntryPtr>)
				.ListItemsSource(&QueueItems)
				.SelectionMode(ESelectionMode::Multi)
				.OnGenerateRow(this, &SItemIconGeneratorPanel::GenerateQueueRow)
				.OnSelectionChanged(this, &SItemIconGeneratorPanel::HandleQueueSelectionChanged)
				.HeaderRow
				(
					SNew(SHeaderRow)
					+ SHeaderRow::Column(ItemIconGeneratorPanel::IncludeColumn).FixedWidth(44.0f).DefaultLabel(LOCTEXT("IncludeColumn", "사용"))
					+ SHeaderRow::Column(ItemIconGeneratorPanel::MeshColumn).FillWidth(0.34f).DefaultLabel(LOCTEXT("MeshColumn", "스태틱 메시"))
					+ SHeaderRow::Column(ItemIconGeneratorPanel::OutputColumn).FillWidth(0.42f).DefaultLabel(LOCTEXT("OutputColumn", "출력 에셋"))
					+ SHeaderRow::Column(ItemIconGeneratorPanel::StatusColumn).FillWidth(0.24f).DefaultLabel(LOCTEXT("StatusColumn", "상태"))
				)
			]

			+ SSplitter::Slot().Value(0.38f)
			[
				SNew(SBorder)
				.Padding(8.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(this, &SItemIconGeneratorPanel::GetSelectedItemText)
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("AutoPreview", "자동 미리보기"))
							.ColorAndOpacity(FLinearColor(0.25f, 0.8f, 0.42f))
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SButton)
							.Text(LOCTEXT("RefreshPreview", "새로고침"))
							.IsEnabled_Lambda([this]() { return !IsBatchRunning() && GetSelectedItem().IsValid(); })
							.OnClicked_Lambda([this]() { return PreviewSelected(true); })
						]
					]
					+ SVerticalBox::Slot().FillHeight(1.0f).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(320.0f).HeightOverride(320.0f)
						[
							SNew(ItemIconGeneratorPanel::SInteractivePreview)
							.OnDragged(ItemIconGeneratorPanel::FOnPreviewDragged::CreateSP(this, &SItemIconGeneratorPanel::HandlePreviewDrag))
							.OnZoomed(ItemIconGeneratorPanel::FOnPreviewZoomed::CreateSP(this, &SItemIconGeneratorPanel::HandlePreviewZoom))
							.OnKeyPressed(ItemIconGeneratorPanel::FOnPreviewKeyPressed::CreateSP(this, &SItemIconGeneratorPanel::HandlePreviewKey))
							[
								SNew(SOverlay)
								+ SOverlay::Slot()
								[
									SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Checkerboard")))
								]
								+ SOverlay::Slot()
								[
									SAssignNew(PreviewImage, SImage).Image(&PreviewBrush)
								]
								+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(6.0f)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("PreviewControlsHint", "드래그: 화면 기준 회전  ·  WASD: 위치  ·  휠: 줌"))
									.ColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.85f, 0.8f))
								]
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 5.0f)
					[
						SNew(STextBlock)
						.Text_Lambda([this]() { return FText::FromString(PreviewMessage); })
						.AutoWrapText(true)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 5.0f)
					[
						SNew(SCheckBox)
						.IsEnabled_Lambda([this]() { return !IsBatchRunning() && GetSelectedItem().IsValid(); })
						.IsChecked_Lambda([this]()
						{
							const FItemIconGeneratorQueueEntryPtr Item = GetSelectedItem();
							return Item.IsValid() && Item->bUseOverrides ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						})
						.OnCheckStateChanged_Lambda([this](const ECheckBoxState State)
						{
							if (const FItemIconGeneratorQueueEntryPtr Item = GetSelectedItem())
							{
								if (State == ECheckBoxState::Checked && !Item->bUseOverrides)
								{
									Item->OverrideSettings = DefaultCaptureSettings;
								}
								Item->bUseOverrides = State == ECheckBoxState::Checked;
								MarkSelectedPreviewStale();
							}
						})
						[
							SNew(STextBlock).Text(LOCTEXT("OverrideDefaults", "이 메시만 별도 설정 (끄면 공통값 편집)"))
						]
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBox).MaxDesiredHeight(285.0f)
						[
							SNew(SScrollBox)
							+ SScrollBox::Slot()
							[
								SNew(SGridPanel).FillColumn(1, 1.0f)
								+ SGridPanel::Slot(0, 0).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 5.0f)
								[
									SNew(STextBlock).Text(LOCTEXT("MeshRotation", "메시 회전 P/Y/R"))
								]
								+ SGridPanel::Slot(1, 0).Padding(0.0f, 0.0f, 0.0f, 5.0f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 3.0f, 0.0f)
									[
										SNew(SNumericEntryBox<double>)
										.ToolTipText(LOCTEXT("MeshPitchTip", "메시 Pitch (-180~180)"))
										.MinValue(-180.0).MaxValue(180.0).MinSliderValue(-180.0).MaxSliderValue(180.0).Delta(1.0)
										.IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); })
										.Value_Lambda([this]() -> TOptional<double> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<double>(GetEffectiveCaptureSettings(*Item).MeshRotation.Pitch) : TOptional<double>(); })
										.OnValueChanged_Lambda([this](const double Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.MeshRotation.Pitch = FRotator::NormalizeAxis(Value); }); })
									]
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 3.0f, 0.0f)
									[
										SNew(SNumericEntryBox<double>)
										.ToolTipText(LOCTEXT("MeshYawTip", "메시 Yaw (-180~180)"))
										.MinValue(-180.0).MaxValue(180.0).MinSliderValue(-180.0).MaxSliderValue(180.0).Delta(1.0)
										.IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); })
										.Value_Lambda([this]() -> TOptional<double> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<double>(GetEffectiveCaptureSettings(*Item).MeshRotation.Yaw) : TOptional<double>(); })
										.OnValueChanged_Lambda([this](const double Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.MeshRotation.Yaw = FRotator::NormalizeAxis(Value); }); })
									]
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 3.0f, 0.0f)
									[
										SNew(SNumericEntryBox<double>)
										.ToolTipText(LOCTEXT("MeshRollTip", "메시 Roll (-180~180)"))
										.MinValue(-180.0).MaxValue(180.0).MinSliderValue(-180.0).MaxSliderValue(180.0).Delta(1.0)
										.IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); })
										.Value_Lambda([this]() -> TOptional<double> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<double>(GetEffectiveCaptureSettings(*Item).MeshRotation.Roll) : TOptional<double>(); })
										.OnValueChanged_Lambda([this](const double Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.MeshRotation.Roll = FRotator::NormalizeAxis(Value); }); })
									]
									+ SHorizontalBox::Slot().AutoWidth()
									[
										SNew(SButton)
										.Text(LOCTEXT("ResetMeshRotation", "초기화"))
										.ToolTipText(LOCTEXT("ResetMeshRotationTip", "메시 회전을 0도로 초기화합니다."))
										.IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); })
										.OnClicked_Lambda([this]()
										{
											ModifySelectedCaptureSettings([](auto& Settings) { Settings.MeshRotation = FRotator::ZeroRotator; });
											return FReply::Handled();
										})
									]
								]
								+ SGridPanel::Slot(0, 4).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 5.0f)[SNew(STextBlock).Text(LOCTEXT("LightRotation", "키 조명 회전 P/Y/R"))]
								+ SGridPanel::Slot(1, 4).Padding(0.0f, 0.0f, 0.0f, 5.0f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 3.0f, 0.0f)[SNew(SNumericEntryBox<double>).ToolTipText(LOCTEXT("LightPitchTip", "키 조명 Pitch")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<double> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<double>(GetEffectiveCaptureSettings(*Item).DirectionalLightRotation.Pitch) : TOptional<double>(); }).OnValueChanged_Lambda([this](const double Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.DirectionalLightRotation.Pitch = Value; }); })]
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 3.0f, 0.0f)[SNew(SNumericEntryBox<double>).ToolTipText(LOCTEXT("LightYawTip", "키 조명 Yaw")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<double> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<double>(GetEffectiveCaptureSettings(*Item).DirectionalLightRotation.Yaw) : TOptional<double>(); }).OnValueChanged_Lambda([this](const double Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.DirectionalLightRotation.Yaw = Value; }); })]
									+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SNumericEntryBox<double>).ToolTipText(LOCTEXT("LightRollTip", "키 조명 Roll")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<double> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<double>(GetEffectiveCaptureSettings(*Item).DirectionalLightRotation.Roll) : TOptional<double>(); }).OnValueChanged_Lambda([this](const double Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.DirectionalLightRotation.Roll = Value; }); })]
								]
								+ SGridPanel::Slot(0, 5).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 5.0f)[SNew(STextBlock).Text(LOCTEXT("LightIntensity", "키 / 필 / 백 강도"))]
								+ SGridPanel::Slot(1, 5).Padding(0.0f, 0.0f, 0.0f, 5.0f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 3.0f, 0.0f)[SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(20.0f).ToolTipText(LOCTEXT("KeyIntensityTip", "키 조명 강도")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<float> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<float>(GetEffectiveCaptureSettings(*Item).DirectionalLightIntensity) : TOptional<float>(); }).OnValueChanged_Lambda([this](const float Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.DirectionalLightIntensity = FMath::Max(Value, 0.0f); }); })]
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 3.0f, 0.0f)[SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(20.0f).ToolTipText(LOCTEXT("FillIntensityTip", "필 조명 강도")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<float> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<float>(GetEffectiveCaptureSettings(*Item).FillLightIntensity) : TOptional<float>(); }).OnValueChanged_Lambda([this](const float Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.FillLightIntensity = FMath::Max(Value, 0.0f); }); })]
									+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(20.0f).ToolTipText(LOCTEXT("BackIntensityTip", "백라이트 강도")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<float> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<float>(GetEffectiveCaptureSettings(*Item).BackLightIntensity) : TOptional<float>(); }).OnValueChanged_Lambda([this](const float Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.BackLightIntensity = FMath::Max(Value, 0.0f); }); })]
								]
								+ SGridPanel::Slot(0, 6).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 5.0f)[SNew(STextBlock).Text(LOCTEXT("CaptureColor", "스카이 / 노출 / 채도"))]
								+ SGridPanel::Slot(1, 6).Padding(0.0f, 0.0f, 0.0f, 5.0f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 3.0f, 0.0f)[SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(10.0f).ToolTipText(LOCTEXT("SkyIntensityTip", "스카이라이트 강도")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<float> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<float>(GetEffectiveCaptureSettings(*Item).SkyLightIntensity) : TOptional<float>(); }).OnValueChanged_Lambda([this](const float Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.SkyLightIntensity = FMath::Max(Value, 0.0f); }); })]
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 3.0f, 0.0f)[SNew(SNumericEntryBox<float>).MinValue(-15.0f).MaxValue(15.0f).ToolTipText(LOCTEXT("ExposureTip", "노출 보정")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<float> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<float>(GetEffectiveCaptureSettings(*Item).ExposureCompensation) : TOptional<float>(); }).OnValueChanged_Lambda([this](const float Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.ExposureCompensation = FMath::Clamp(Value, -15.0f, 15.0f); }); })]
									+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(2.0f).ToolTipText(LOCTEXT("SaturationTip", "색 채도 (1.0은 원본 유지)")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<float> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<float>(GetEffectiveCaptureSettings(*Item).ColorSaturation) : TOptional<float>(); }).OnValueChanged_Lambda([this](const float Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.ColorSaturation = FMath::Clamp(Value, 0.0f, 2.0f); }); })]
								]
								+ SGridPanel::Slot(0, 7).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 5.0f)[SNew(STextBlock).Text(LOCTEXT("FillRotation", "필 조명 회전 P/Y/R"))]
								+ SGridPanel::Slot(1, 7).Padding(0.0f, 0.0f, 0.0f, 5.0f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 3.0f, 0.0f)[SNew(SNumericEntryBox<double>).ToolTipText(LOCTEXT("FillPitchTip", "필 조명 Pitch")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<double> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<double>(GetEffectiveCaptureSettings(*Item).FillLightRotation.Pitch) : TOptional<double>(); }).OnValueChanged_Lambda([this](const double Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.FillLightRotation.Pitch = Value; }); })]
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 3.0f, 0.0f)[SNew(SNumericEntryBox<double>).ToolTipText(LOCTEXT("FillYawTip", "필 조명 Yaw")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<double> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<double>(GetEffectiveCaptureSettings(*Item).FillLightRotation.Yaw) : TOptional<double>(); }).OnValueChanged_Lambda([this](const double Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.FillLightRotation.Yaw = Value; }); })]
									+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SNumericEntryBox<double>).ToolTipText(LOCTEXT("FillRollTip", "필 조명 Roll")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<double> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<double>(GetEffectiveCaptureSettings(*Item).FillLightRotation.Roll) : TOptional<double>(); }).OnValueChanged_Lambda([this](const double Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.FillLightRotation.Roll = Value; }); })]
								]
								+ SGridPanel::Slot(0, 8).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 5.0f)[SNew(STextBlock).Text(LOCTEXT("BackRotation", "백라이트 회전 P/Y/R"))]
								+ SGridPanel::Slot(1, 8).Padding(0.0f, 0.0f, 0.0f, 5.0f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 3.0f, 0.0f)[SNew(SNumericEntryBox<double>).ToolTipText(LOCTEXT("BackPitchTip", "백라이트 Pitch")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<double> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<double>(GetEffectiveCaptureSettings(*Item).BackLightRotation.Pitch) : TOptional<double>(); }).OnValueChanged_Lambda([this](const double Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.BackLightRotation.Pitch = Value; }); })]
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 3.0f, 0.0f)[SNew(SNumericEntryBox<double>).ToolTipText(LOCTEXT("BackYawTip", "백라이트 Yaw")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<double> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<double>(GetEffectiveCaptureSettings(*Item).BackLightRotation.Yaw) : TOptional<double>(); }).OnValueChanged_Lambda([this](const double Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.BackLightRotation.Yaw = Value; }); })]
									+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SNumericEntryBox<double>).ToolTipText(LOCTEXT("BackRollTip", "백라이트 Roll")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<double> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<double>(GetEffectiveCaptureSettings(*Item).BackLightRotation.Roll) : TOptional<double>(); }).OnValueChanged_Lambda([this](const double Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.BackLightRotation.Roll = Value; }); })]
								]
								+ SGridPanel::Slot(0, 9).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 5.0f)[SNew(STextBlock).Text(LOCTEXT("KeyColor", "키 조명색 R/G/B"))]
								+ SGridPanel::Slot(1, 9).Padding(0.0f, 0.0f, 0.0f, 5.0f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 3.0f, 0.0f)[SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(1.0f).ToolTipText(LOCTEXT("KeyRTip", "키 조명 Red")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<float> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<float>(GetEffectiveCaptureSettings(*Item).DirectionalLightColor.R) : TOptional<float>(); }).OnValueChanged_Lambda([this](const float Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.DirectionalLightColor.R = FMath::Clamp(Value, 0.0f, 1.0f); }); })]
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 3.0f, 0.0f)[SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(1.0f).ToolTipText(LOCTEXT("KeyGTip", "키 조명 Green")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<float> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<float>(GetEffectiveCaptureSettings(*Item).DirectionalLightColor.G) : TOptional<float>(); }).OnValueChanged_Lambda([this](const float Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.DirectionalLightColor.G = FMath::Clamp(Value, 0.0f, 1.0f); }); })]
									+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(1.0f).ToolTipText(LOCTEXT("KeyBTip", "키 조명 Blue")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<float> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<float>(GetEffectiveCaptureSettings(*Item).DirectionalLightColor.B) : TOptional<float>(); }).OnValueChanged_Lambda([this](const float Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.DirectionalLightColor.B = FMath::Clamp(Value, 0.0f, 1.0f); }); })]
								]
								+ SGridPanel::Slot(0, 10).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 5.0f)[SNew(STextBlock).Text(LOCTEXT("FillColor", "필 조명색 R/G/B"))]
								+ SGridPanel::Slot(1, 10).Padding(0.0f, 0.0f, 0.0f, 5.0f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 3.0f, 0.0f)[SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(1.0f).ToolTipText(LOCTEXT("FillRTip", "필 조명 Red")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<float> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<float>(GetEffectiveCaptureSettings(*Item).FillLightColor.R) : TOptional<float>(); }).OnValueChanged_Lambda([this](const float Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.FillLightColor.R = FMath::Clamp(Value, 0.0f, 1.0f); }); })]
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 3.0f, 0.0f)[SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(1.0f).ToolTipText(LOCTEXT("FillGTip", "필 조명 Green")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<float> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<float>(GetEffectiveCaptureSettings(*Item).FillLightColor.G) : TOptional<float>(); }).OnValueChanged_Lambda([this](const float Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.FillLightColor.G = FMath::Clamp(Value, 0.0f, 1.0f); }); })]
									+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(1.0f).ToolTipText(LOCTEXT("FillBTip", "필 조명 Blue")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<float> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<float>(GetEffectiveCaptureSettings(*Item).FillLightColor.B) : TOptional<float>(); }).OnValueChanged_Lambda([this](const float Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.FillLightColor.B = FMath::Clamp(Value, 0.0f, 1.0f); }); })]
								]
								+ SGridPanel::Slot(0, 11).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 5.0f)[SNew(STextBlock).Text(LOCTEXT("BackColor", "백라이트색 R/G/B"))]
								+ SGridPanel::Slot(1, 11).Padding(0.0f, 0.0f, 0.0f, 5.0f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 3.0f, 0.0f)[SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(1.0f).ToolTipText(LOCTEXT("BackRTip", "백라이트 Red")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<float> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<float>(GetEffectiveCaptureSettings(*Item).BackLightColor.R) : TOptional<float>(); }).OnValueChanged_Lambda([this](const float Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.BackLightColor.R = FMath::Clamp(Value, 0.0f, 1.0f); }); })]
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 3.0f, 0.0f)[SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(1.0f).ToolTipText(LOCTEXT("BackGTip", "백라이트 Green")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<float> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<float>(GetEffectiveCaptureSettings(*Item).BackLightColor.G) : TOptional<float>(); }).OnValueChanged_Lambda([this](const float Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.BackLightColor.G = FMath::Clamp(Value, 0.0f, 1.0f); }); })]
									+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(1.0f).ToolTipText(LOCTEXT("BackBTip", "백라이트 Blue")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<float> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<float>(GetEffectiveCaptureSettings(*Item).BackLightColor.B) : TOptional<float>(); }).OnValueChanged_Lambda([this](const float Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.BackLightColor.B = FMath::Clamp(Value, 0.0f, 1.0f); }); })]
								]
								+ SGridPanel::Slot(0, 12).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)[SNew(STextBlock).Text(LOCTEXT("BackgroundColor", "배경색 R/G/B/A"))]
								+ SGridPanel::Slot(1, 12)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 3.0f, 0.0f)[SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(1.0f).ToolTipText(LOCTEXT("BackgroundRTip", "배경색 Red")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<float> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<float>(GetEffectiveCaptureSettings(*Item).BackgroundColor.R) : TOptional<float>(); }).OnValueChanged_Lambda([this](const float Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.BackgroundColor.R = FMath::Clamp(Value, 0.0f, 1.0f); }); })]
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 3.0f, 0.0f)[SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(1.0f).ToolTipText(LOCTEXT("BackgroundGTip", "배경색 Green")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<float> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<float>(GetEffectiveCaptureSettings(*Item).BackgroundColor.G) : TOptional<float>(); }).OnValueChanged_Lambda([this](const float Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.BackgroundColor.G = FMath::Clamp(Value, 0.0f, 1.0f); }); })]
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 3.0f, 0.0f)[SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(1.0f).ToolTipText(LOCTEXT("BackgroundBTip", "배경색 Blue")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<float> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<float>(GetEffectiveCaptureSettings(*Item).BackgroundColor.B) : TOptional<float>(); }).OnValueChanged_Lambda([this](const float Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.BackgroundColor.B = FMath::Clamp(Value, 0.0f, 1.0f); }); })]
									+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(1.0f).ToolTipText(LOCTEXT("BackgroundATip", "배경색 Alpha")).IsEnabled_Lambda([this]() { return GetSelectedItem().IsValid() && !IsBatchRunning(); }).Value_Lambda([this]() -> TOptional<float> { const auto Item = GetSelectedItem(); return Item.IsValid() ? TOptional<float>(GetEffectiveCaptureSettings(*Item).BackgroundColor.A) : TOptional<float>(); }).OnValueChanged_Lambda([this](const float Value) { ModifySelectedCaptureSettings([Value](auto& Settings) { Settings.BackgroundColor.A = FMath::Clamp(Value, 0.0f, 1.0f); }); })]
								]
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("SaveSelectedPreview", "선택 미리보기 저장"))
						.ToolTipText(LOCTEXT("SaveSelectedPreviewTooltip", "현재 구도를 출력 해상도로 렌더링해 에셋으로 저장합니다."))
						.HAlign(HAlign_Center)
						.IsEnabled_Lambda([this]() { return !IsBatchRunning() && GetSelectedItem().IsValid(); })
						.OnClicked(this, &SItemIconGeneratorPanel::SaveSelectedPreview)
					]
				]
			]
		]

		+ SVerticalBox::Slot().AutoHeight()
		.Padding(6.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SProgressBar).Percent(this, &SItemIconGeneratorPanel::GetProgressFraction)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock).Text(this, &SItemIconGeneratorPanel::GetProgressText)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.0f, 0.0f)
			[
				SNew(STextBlock).Text(this, &SItemIconGeneratorPanel::GetSummaryText)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Cancel", "취소"))
				.IsEnabled_Lambda([this]() { return IsBatchRunning(); })
				.OnClicked(this, &SItemIconGeneratorPanel::CancelBatch)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("RetryFailed", "실패 항목 재시도"))
				.IsEnabled_Lambda([this]() { return !IsBatchRunning() && HasFailedItems(); })
				.OnClicked(this, &SItemIconGeneratorPanel::RetryFailed)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), TEXT("PrimaryButton"))
				.Text(LOCTEXT("GenerateAll", "전체 생성"))
				.IsEnabled_Lambda([this]() { return !IsBatchRunning() && QueueItems.ContainsByPredicate([](const auto& Item) { return Item->bIncluded; }); })
				.OnClicked(this, &SItemIconGeneratorPanel::GenerateAll)
			]
		]
	];
}

SItemIconGeneratorPanel::~SItemIconGeneratorPanel()
{
	if (PreviewCache)
	{
		PreviewCache->ReleaseRenderResources();
	}
}

void SItemIconGeneratorPanel::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	PollBatchState();
	if (bAutoPreviewPending && !IsBatchRunning())
	{
		AutoPreviewDelayRemaining -= InDeltaTime;
		if (AutoPreviewDelayRemaining <= 0.0f)
		{
			bAutoPreviewPending = false;
			PreviewSelected(false);
		}
	}
}

TSharedRef<ITableRow> SItemIconGeneratorPanel::GenerateQueueRow(
	FItemIconGeneratorQueueEntryPtr Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(ItemIconGeneratorPanel::SQueueRow, OwnerTable).Item(Item);
}

void SItemIconGeneratorPanel::HandleQueueSelectionChanged(
	FItemIconGeneratorQueueEntryPtr Item,
	const ESelectInfo::Type SelectInfo)
{
	(void)SelectInfo;
	if (Item.IsValid())
	{
		SelectedItem = Item;
	}
	else if (QueueList.IsValid() && QueueList->GetNumItemsSelected() == 0)
	{
		SelectedItem.Reset();
	}
	RefreshPreviewForSelection();
}

FReply SItemIconGeneratorPanel::AddContentBrowserSelection()
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	TArray<FAssetData> SelectedAssets;
	ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);

	int32 AddedCount = 0;
	FItemIconGeneratorQueueEntryPtr FirstAdded;
	for (const FAssetData& AssetData : SelectedAssets)
	{
		if (AssetData.AssetClassPath != UStaticMesh::StaticClass()->GetClassPathName())
		{
			continue;
		}

		UStaticMesh* StaticMesh = Cast<UStaticMesh>(AssetData.GetAsset());
		if (!StaticMesh || QueueItems.ContainsByPredicate([StaticMesh](const FItemIconGeneratorQueueEntryPtr& Existing)
		{
			return Existing->StaticMesh.Get() == StaticMesh || Existing->StaticMesh.ToSoftObjectPath() == FSoftObjectPath(StaticMesh);
		}))
		{
			continue;
		}

		FItemIconGeneratorQueueEntryPtr Entry = MakeShared<FItemIconGeneratorQueueEntry>();
		Entry->StaticMesh = StaticMesh;
		Entry->OutputAssetName = MakeOutputName(StaticMesh->GetName());
		Entry->OverrideSettings = DefaultCaptureSettings;
		QueueItems.Add(Entry);
		FirstAdded = FirstAdded.IsValid() ? FirstAdded : Entry;
		++AddedCount;
	}

	if (QueueList.IsValid())
	{
		QueueList->RequestListRefresh();
		if (FirstAdded.IsValid())
		{
			QueueList->SetSelection(FirstAdded);
		}
	}

	SummaryMessage = AddedCount > 0
		? FString::Printf(TEXT("스태틱 메시 %d개를 추가했습니다."), AddedCount)
		: TEXT("콘텐츠 브라우저에서 새로 추가할 스태틱 메시가 선택되지 않았습니다.");
	return FReply::Handled();
}

FReply SItemIconGeneratorPanel::RemoveSelectedItems()
{
	if (!QueueList.IsValid())
	{
		return FReply::Handled();
	}

	const TArray<FItemIconGeneratorQueueEntryPtr> ItemsToRemove = QueueList->GetSelectedItems();
	for (const FItemIconGeneratorQueueEntryPtr& Item : ItemsToRemove)
	{
		if (UStaticMesh* StaticMesh = Item->StaticMesh.Get())
		{
			PreviewCache->InvalidateMesh(StaticMesh);
		}
		QueueItems.Remove(Item);
	}

	SelectedItem.Reset();
	bAutoPreviewPending = false;
	QueueList->ClearSelection();
	QueueList->RequestListRefresh();
	ClearPreview(TEXT("메시를 선택하면 미리보기가 자동으로 생성됩니다."));
	SummaryMessage = FString::Printf(TEXT("항목 %d개를 제거했습니다."), ItemsToRemove.Num());
	return FReply::Handled();
}

FReply SItemIconGeneratorPanel::ClearItems()
{
	QueueItems.Reset();
	SelectedItem.Reset();
	bAutoPreviewPending = false;
	PreviewCache->Clear();
	QueueList->ClearSelection();
	QueueList->RequestListRefresh();
	ClearPreview(TEXT("메시를 선택하면 미리보기가 자동으로 생성됩니다."));
	SummaryMessage = TEXT("목록을 비웠습니다.");
	return FReply::Handled();
}

FReply SItemIconGeneratorPanel::PreviewSelected(const bool bForceRefresh)
{
	bAutoPreviewPending = false;
	const FItemIconGeneratorQueueEntryPtr Item = GetSelectedItem();
	UStaticMesh* StaticMesh = Item.IsValid() ? Item->StaticMesh.LoadSynchronous() : nullptr;
	if (!StaticMesh)
	{
		ClearPreview(TEXT("선택한 스태틱 메시를 불러오지 못했습니다."));
		if (Item.IsValid())
		{
			Item->Status = TEXT("실패");
		}
		return FReply::Handled();
	}

	const FString PreviousPreviewKey = Item->LastPreviewCacheKey;
	const FItemIconPreviewResult Result = PreviewCache->CapturePreview(
		StaticMesh,
		GetPreviewCaptureSettings(*Item),
		bForceRefresh);
	if (!Result.bSuccess)
	{
		Item->Status = TEXT("실패");
		ClearPreview(Result.Message);
	}
	else
	{
		Item->LastPreviewCacheKey = Result.CacheKey;
		Item->Status = TEXT("미리보기 준비");
		PreviewMessage = Result.bFromCache ? TEXT("캐시된 미리보기를 불러왔습니다.") : TEXT("미리보기를 생성하고 캐시에 보관했습니다.");
		SetPreviewTexture(Result.Texture);
		if (!PreviousPreviewKey.IsEmpty() && PreviousPreviewKey != Result.CacheKey)
		{
			PreviewCache->InvalidatePreview(PreviousPreviewKey);
		}
	}

	QueueList->RequestListRefresh();
	SummaryMessage = Result.bSuccess ? PreviewMessage : Result.Message;
	return FReply::Handled();
}

FReply SItemIconGeneratorPanel::SaveSelectedPreview()
{
	const FItemIconGeneratorQueueEntryPtr Item = GetSelectedItem();
	UStaticMesh* StaticMesh = Item.IsValid() ? Item->StaticMesh.LoadSynchronous() : nullptr;
	if (!StaticMesh)
	{
		SummaryMessage = TEXT("선택한 스태틱 메시를 불러오지 못했습니다.");
		return FReply::Handled();
	}

	FItemIconSaveSettings SaveSettings;
	SaveSettings.PackagePath = OutputFolder;
	SaveSettings.AssetName = Item->OutputAssetName;
	SaveSettings.bOverwriteExisting = bOverwriteExisting;
	SaveSettings.bSaveImmediately = true;
	const FItemIconCaptureSettings FinalCaptureSettings = GetEffectiveCaptureSettings(*Item);
	FItemIconGenerationResult SaveResult;
	if (FinalCaptureSettings.TextureSize <= 512)
	{
		FItemIconPreviewResult PreviewResult = PreviewCache->FindPreview(StaticMesh, GetPreviewCaptureSettings(*Item));
		if (!PreviewResult.bSuccess)
		{
			PreviewResult = PreviewCache->CapturePreview(StaticMesh, GetPreviewCaptureSettings(*Item), false);
		}
		if (PreviewResult.bSuccess)
		{
			SaveResult = PreviewCache->SavePreview(PreviewResult.CacheKey, SaveSettings);
			Item->LastPreviewCacheKey = PreviewResult.CacheKey;
			SetPreviewTexture(PreviewResult.Texture);
		}
		else
		{
			SaveResult.Message = PreviewResult.Message;
		}
	}
	else
	{
		SaveResult = UItemIconGeneratorLibrary::GenerateIconFromStaticMesh(
			StaticMesh,
			FinalCaptureSettings,
			SaveSettings);
	}
	Item->Status = SaveResult.bSuccess ? TEXT("저장 완료") : TEXT("실패");
	SummaryMessage = SaveResult.bSuccess ? TEXT("현재 구도를 출력 해상도로 저장했습니다.") : SaveResult.Message;
	QueueList->RequestListRefresh();
	return FReply::Handled();
}

FReply SItemIconGeneratorPanel::GenerateAll()
{
	TArray<int32> QueueIndices;
	for (int32 QueueIndex = 0; QueueIndex < QueueItems.Num(); ++QueueIndex)
	{
		if (QueueItems[QueueIndex]->bIncluded)
		{
			QueueIndices.Add(QueueIndex);
		}
	}
	StartBatchForIndices(QueueIndices);
	return FReply::Handled();
}

FReply SItemIconGeneratorPanel::RetryFailed()
{
	TArray<int32> QueueIndices;
	for (int32 QueueIndex = 0; QueueIndex < QueueItems.Num(); ++QueueIndex)
	{
		if (QueueItems[QueueIndex]->bIncluded && QueueItems[QueueIndex]->Status == TEXT("실패"))
		{
			QueueIndices.Add(QueueIndex);
		}
	}
	StartBatchForIndices(QueueIndices);
	return FReply::Handled();
}

FReply SItemIconGeneratorPanel::CancelBatch()
{
	if (ActiveBatchJob)
	{
		ActiveBatchJob->RequestCancel();
		SummaryMessage = TEXT("취소를 요청했습니다. 현재 항목을 마친 뒤 중단합니다.");
	}
	return FReply::Handled();
}

void SItemIconGeneratorPanel::StartBatchForIndices(const TArray<int32>& QueueIndices)
{
	if (IsBatchRunning() || QueueIndices.IsEmpty())
	{
		return;
	}

	FItemIconBatchRequest BatchRequest;
	BatchRequest.bContinueOnFailure = true;
	ActiveBatchQueueIndices.Reset();
	for (const int32 QueueIndex : QueueIndices)
	{
		if (!QueueItems.IsValidIndex(QueueIndex))
		{
			continue;
		}

		const FItemIconGeneratorQueueEntryPtr& Item = QueueItems[QueueIndex];
		FItemIconGenerationRequest& Request = BatchRequest.Items.AddDefaulted_GetRef();
		Request.StaticMesh = Item->StaticMesh;
		Request.CaptureSettings = GetEffectiveCaptureSettings(*Item);
		Request.SaveSettings.PackagePath = OutputFolder;
		Request.SaveSettings.AssetName = Item->OutputAssetName;
		Request.SaveSettings.bOverwriteExisting = bOverwriteExisting;
		Request.SaveSettings.bSaveImmediately = true;
		Item->Status = TEXT("대기");
		ActiveBatchQueueIndices.Add(QueueIndex);
	}

	ActiveBatchJob.Reset(NewObject<UItemIconBatchJob>(GetTransientPackage(), NAME_None, RF_Transient));
	if (!ActiveBatchJob->Start(BatchRequest))
	{
		SummaryMessage = TEXT("일괄 생성을 시작하지 못했습니다.");
		ActiveBatchJob.Reset();
		ActiveBatchQueueIndices.Reset();
		return;
	}

	DisplayedBatchProgress = ActiveBatchJob->GetProgress();
	bBatchCompletionHandled = false;
	SummaryMessage = FString::Printf(TEXT("아이콘 %d개를 생성합니다."), BatchRequest.Items.Num());
	QueueList->RequestListRefresh();
}

void SItemIconGeneratorPanel::PollBatchState()
{
	if (!ActiveBatchJob)
	{
		return;
	}

	const FItemIconBatchProgress& Progress = ActiveBatchJob->GetProgress();
	const FItemIconBatchResult& BatchResult = ActiveBatchJob->GetResult();
	DisplayedBatchProgress = Progress;
	for (const FItemIconBatchItemResult& ItemResult : BatchResult.Items)
	{
		if (!ActiveBatchQueueIndices.IsValidIndex(ItemResult.RequestIndex))
		{
			continue;
		}

		const int32 QueueIndex = ActiveBatchQueueIndices[ItemResult.RequestIndex];
		if (!QueueItems.IsValidIndex(QueueIndex))
		{
			continue;
		}

		switch (ItemResult.Status)
		{
		case EItemIconBatchItemStatus::Running:
			QueueItems[QueueIndex]->Status = TEXT("생성 중");
			break;
		case EItemIconBatchItemStatus::Succeeded:
			QueueItems[QueueIndex]->Status = TEXT("저장 완료");
			break;
		case EItemIconBatchItemStatus::Failed:
			QueueItems[QueueIndex]->Status = TEXT("실패");
			break;
		case EItemIconBatchItemStatus::Cancelled:
			QueueItems[QueueIndex]->Status = TEXT("취소됨");
			break;
		default:
			if (Progress.State == EItemIconBatchState::Preparing)
			{
				QueueItems[QueueIndex]->Status = TEXT("준비 중");
			}
			break;
		}
	}

	if (QueueList.IsValid() && IsBatchRunning())
	{
		QueueList->RequestListRefresh();
	}

	if (Progress.State == EItemIconBatchState::Completed && !bBatchCompletionHandled)
	{
		bBatchCompletionHandled = true;
		SummaryMessage = FString::Printf(
			TEXT("일괄 생성 완료: 저장 %d, 실패 %d, 취소 %d"),
			Progress.SucceededItems,
			Progress.FailedItems,
			Progress.CancelledItems);
		if (QueueList.IsValid())
		{
			QueueList->RequestListRefresh();
		}

		ActiveBatchQueueIndices.Reset();
		ActiveBatchJob.Reset();
	}
}

void SItemIconGeneratorPanel::RefreshPreviewForSelection()
{
	const FItemIconGeneratorQueueEntryPtr Item = GetSelectedItem();
	UStaticMesh* StaticMesh = Item.IsValid() ? Item->StaticMesh.Get() : nullptr;
	if (!StaticMesh)
	{
		bAutoPreviewPending = false;
		ClearPreview(Item.IsValid() ? TEXT("선택한 메시를 불러오지 못했습니다.") : TEXT("메시를 선택하면 미리보기가 자동으로 생성됩니다."));
		return;
	}

	const FItemIconPreviewResult Result = PreviewCache->FindPreview(StaticMesh, GetPreviewCaptureSettings(*Item));
	if (Result.bSuccess)
	{
		bAutoPreviewPending = false;
		Item->LastPreviewCacheKey = Result.CacheKey;
		SetPreviewTexture(Result.Texture);
		PreviewMessage = TEXT("자동 미리보기 · 드래그 메시 회전 · WASD 위치 · 휠 줌");
	}
	else
	{
		ClearPreview(TEXT("자동 미리보기를 생성하는 중입니다."));
		RequestAutoPreview(0.0f);
	}
}

void SItemIconGeneratorPanel::SetPreviewTexture(UTexture2D* Texture)
{
	if (!Texture)
	{
		ClearPreview(TEXT("미리보기 텍스처를 사용할 수 없습니다."));
		return;
	}

	PreviewBrush.SetResourceObject(Texture);
	PreviewBrush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
	PreviewBrush.DrawAs = ESlateBrushDrawType::Image;
	if (PreviewImage.IsValid())
	{
		PreviewImage->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SItemIconGeneratorPanel::ClearPreview(const FString& Message)
{
	PreviewBrush.SetResourceObject(nullptr);
	PreviewBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
	PreviewMessage = Message;
	if (PreviewImage.IsValid())
	{
		PreviewImage->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SItemIconGeneratorPanel::MarkAllPreviewsStale()
{
	const FItemIconGeneratorQueueEntryPtr Selected = GetSelectedItem();
	for (const FItemIconGeneratorQueueEntryPtr& Item : QueueItems)
	{
		if (!Item->bUseOverrides && !Item->LastPreviewCacheKey.IsEmpty())
		{
			Item->Status = TEXT("미리보기 갱신 필요");
			if (Item != Selected)
			{
				PreviewCache->InvalidatePreview(Item->LastPreviewCacheKey);
				Item->LastPreviewCacheKey.Reset();
			}
		}
	}
	if (PreviewBrush.GetResourceObject())
	{
		PreviewMessage = TEXT("설정 변경 감지 · 미리보기를 자동 갱신합니다.");
	}
	else
	{
		ClearPreview(TEXT("자동 미리보기를 생성하는 중입니다."));
	}
	RequestAutoPreview();
	if (QueueList.IsValid())
	{
		QueueList->RequestListRefresh();
	}
}

void SItemIconGeneratorPanel::MarkSelectedPreviewStale()
{
	if (const FItemIconGeneratorQueueEntryPtr Item = GetSelectedItem())
	{
		if (!Item->LastPreviewCacheKey.IsEmpty())
		{
			Item->Status = TEXT("미리보기 갱신 필요");
		}
	}
	if (PreviewBrush.GetResourceObject())
	{
		PreviewMessage = TEXT("설정 변경 감지 · 미리보기를 자동 갱신합니다.");
	}
	else
	{
		ClearPreview(TEXT("자동 미리보기를 생성하는 중입니다."));
	}
	RequestAutoPreview();
	if (QueueList.IsValid())
	{
		QueueList->RequestListRefresh();
	}
}

void SItemIconGeneratorPanel::RequestAutoPreview(const float DelaySeconds)
{
	if (!GetSelectedItem().IsValid())
	{
		bAutoPreviewPending = false;
		return;
	}

	const float ClampedDelay = FMath::Max(DelaySeconds, 0.0f);
	if (bAutoPreviewPending)
	{
		AutoPreviewDelayRemaining = FMath::Min(AutoPreviewDelayRemaining, ClampedDelay);
	}
	else
	{
		bAutoPreviewPending = true;
		AutoPreviewDelayRemaining = ClampedDelay;
	}
}

void SItemIconGeneratorPanel::HandlePreviewDrag(const FVector2D CursorDelta)
{
	if (CursorDelta.IsNearlyZero())
	{
		return;
	}

	const FItemIconGeneratorQueueEntryPtr Item = GetSelectedItem();
	if (!Item.IsValid() || IsBatchRunning())
	{
		return;
	}

	const FItemIconCaptureSettings CurrentSettings = GetEffectiveCaptureSettings(*Item);
	const FRotator NewRotation = ItemIconGeneratorPanel::RotateMeshInScreenSpace(
		CurrentSettings.MeshRotation,
		CurrentSettings.CameraPitch,
		CurrentSettings.CameraYaw,
		CursorDelta);

	EnsureSelectedOverrideSettings();
	ModifySelectedCaptureSettings([NewRotation](FItemIconCaptureSettings& Settings)
	{
		Settings.MeshRotation = NewRotation;
	});
}

void SItemIconGeneratorPanel::HandlePreviewZoom(const float WheelDelta)
{
	if (FMath::IsNearlyZero(WheelDelta))
	{
		return;
	}

	EnsureSelectedOverrideSettings();
	ModifySelectedCaptureSettings([WheelDelta](FItemIconCaptureSettings& Settings)
	{
		Settings.CameraDistanceMultiplier = FMath::Clamp(
			Settings.CameraDistanceMultiplier * FMath::Pow(0.85f, WheelDelta),
			0.5f,
			10.0f);
	});
}

void SItemIconGeneratorPanel::HandlePreviewKey(const FKey Key)
{
	const FItemIconGeneratorQueueEntryPtr Item = GetSelectedItem();
	if (!Item.IsValid() || IsBatchRunning())
	{
		return;
	}

	const FItemIconCaptureSettings CurrentSettings = GetEffectiveCaptureSettings(*Item);
	const FRotationMatrix CameraBasis(FRotator(CurrentSettings.CameraPitch, CurrentSettings.CameraYaw, 0.0f));
	const FVector ScreenRight = CameraBasis.GetUnitAxis(EAxis::Y);
	const FVector ScreenUp = CameraBasis.GetUnitAxis(EAxis::Z);
	const UStaticMesh* StaticMesh = Item->StaticMesh.Get();
	const float MoveStep = FMath::Clamp(StaticMesh ? StaticMesh->GetBounds().SphereRadius * 0.03f : 1.0f, 0.5f, 50.0f);

	FVector TargetDelta = FVector::ZeroVector;
	if (Key == EKeys::W)
	{
		TargetDelta = -ScreenUp * MoveStep;
	}
	else if (Key == EKeys::S)
	{
		TargetDelta = ScreenUp * MoveStep;
	}
	else if (Key == EKeys::A)
	{
		TargetDelta = ScreenRight * MoveStep;
	}
	else if (Key == EKeys::D)
	{
		TargetDelta = -ScreenRight * MoveStep;
	}

	if (!TargetDelta.IsNearlyZero())
	{
		EnsureSelectedOverrideSettings();
		ModifySelectedCaptureSettings([TargetDelta](FItemIconCaptureSettings& Settings)
		{
			Settings.CameraTargetOffset += TargetDelta;
		});
	}
}

void SItemIconGeneratorPanel::EnsureSelectedOverrideSettings()
{
	if (const FItemIconGeneratorQueueEntryPtr Item = GetSelectedItem(); Item.IsValid() && !Item->bUseOverrides)
	{
		Item->OverrideSettings = DefaultCaptureSettings;
		Item->bUseOverrides = true;
	}
}

void SItemIconGeneratorPanel::RefreshGeneratedNames()
{
	for (const FItemIconGeneratorQueueEntryPtr& Item : QueueItems)
	{
		if (!Item->bCustomOutputName)
		{
			Item->OutputAssetName = MakeOutputName(Item->StaticMesh.GetAssetName());
		}
	}
	if (QueueList.IsValid())
	{
		QueueList->RequestListRefresh();
	}
}

void SItemIconGeneratorPanel::ModifySelectedCaptureSettings(
	TFunctionRef<void(FItemIconCaptureSettings&)> Modifier)
{
	const FItemIconGeneratorQueueEntryPtr Item = GetSelectedItem();
	if (!Item.IsValid() || IsBatchRunning())
	{
		return;
	}

	if (Item->bUseOverrides)
	{
		Modifier(Item->OverrideSettings);
		MarkSelectedPreviewStale();
	}
	else
	{
		Modifier(DefaultCaptureSettings);
		MarkAllPreviewsStale();
	}
}

FItemIconGeneratorQueueEntryPtr SItemIconGeneratorPanel::GetSelectedItem() const
{
	return SelectedItem.Pin();
}

FItemIconCaptureSettings SItemIconGeneratorPanel::GetEffectiveCaptureSettings(
	const FItemIconGeneratorQueueEntry& Item) const
{
	return Item.bUseOverrides ? Item.OverrideSettings : DefaultCaptureSettings;
}

FItemIconCaptureSettings SItemIconGeneratorPanel::GetPreviewCaptureSettings(
	const FItemIconGeneratorQueueEntry& Item) const
{
	FItemIconCaptureSettings Settings = GetEffectiveCaptureSettings(Item);
	Settings.TextureSize = FMath::Clamp(Settings.TextureSize, 64, 512);
	return Settings;
}

FString SItemIconGeneratorPanel::MakeOutputName(const FString& MeshName) const
{
	FString Result = NamingPattern.IsEmpty() ? TEXT("T_Icon_{MeshName}") : NamingPattern;
	Result.ReplaceInline(TEXT("{MeshName}"), *MeshName, ESearchCase::CaseSensitive);
	return Result;
}

bool SItemIconGeneratorPanel::IsBatchRunning() const
{
	return ActiveBatchJob && ActiveBatchJob->IsRunning();
}

bool SItemIconGeneratorPanel::HasFailedItems() const
{
	return QueueItems.ContainsByPredicate([](const FItemIconGeneratorQueueEntryPtr& Item)
	{
		return Item->bIncluded && Item->Status == TEXT("실패");
	});
}

FText SItemIconGeneratorPanel::GetQueueCountText() const
{
	return FText::Format(LOCTEXT("QueueCount", "메시 {0}개"), FText::AsNumber(QueueItems.Num()));
}

FText SItemIconGeneratorPanel::GetSelectedItemText() const
{
	const FItemIconGeneratorQueueEntryPtr Item = GetSelectedItem();
	return Item.IsValid() ? FText::FromString(Item->StaticMesh.GetAssetName()) : LOCTEXT("NoSelection", "선택한 메시 없음");
}

FText SItemIconGeneratorPanel::GetProgressText() const
{
	const FItemIconBatchProgress& Progress = ActiveBatchJob
		? ActiveBatchJob->GetProgress()
		: DisplayedBatchProgress;
	if (Progress.TotalItems <= 0)
	{
		return LOCTEXT("Ready", "준비됨");
	}

	const int32 AccountedItems = Progress.CompletedItems + Progress.CancelledItems;
	return FText::Format(
		LOCTEXT("Progress", "{0} / {1}"),
		FText::AsNumber(AccountedItems),
		FText::AsNumber(Progress.TotalItems));
}

FText SItemIconGeneratorPanel::GetSummaryText() const
{
	return FText::FromString(SummaryMessage);
}

TOptional<float> SItemIconGeneratorPanel::GetProgressFraction() const
{
	const FItemIconBatchProgress& Progress = ActiveBatchJob
		? ActiveBatchJob->GetProgress()
		: DisplayedBatchProgress;
	if (Progress.TotalItems <= 0)
	{
		return 0.0f;
	}
	return static_cast<float>(Progress.CompletedItems + Progress.CancelledItems) / Progress.TotalItems;
}

#undef LOCTEXT_NAMESPACE
