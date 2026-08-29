#include "Modules/ModuleManager.h"

#include "Framework/Docking/TabManager.h"
#include "SItemIconGeneratorPanel.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "FItemIconGeneratorEditorModule"

namespace ItemIconGeneratorEditor
{
	const FName TabName(TEXT("ItemIconGenerator"));
}

class FItemIconGeneratorEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		if (IsRunningCommandlet())
		{
			return;
		}

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
			ItemIconGeneratorEditor::TabName,
			FOnSpawnTab::CreateRaw(this, &FItemIconGeneratorEditorModule::SpawnTab))
			.SetDisplayName(LOCTEXT("TabTitle", "아이템 아이콘 생성기"))
			.SetTooltipText(LOCTEXT("TabTooltip", "스태틱 메시 아이콘을 개별 또는 일괄 생성합니다."))
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Image"))
			.SetMenuType(ETabSpawnerMenuType::Hidden);

		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FItemIconGeneratorEditorModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		if (IsRunningCommandlet())
		{
			return;
		}

		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ItemIconGeneratorEditor::TabName);
	}

private:
	TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& SpawnTabArgs)
	{
		(void)SpawnTabArgs;
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SItemIconGeneratorPanel)
			];
	}

	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
		FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("Programming"));
		Section.AddMenuEntry(
			TEXT("OpenItemIconGenerator"),
			LOCTEXT("OpenMenuLabel", "아이템 아이콘 생성기"),
			LOCTEXT("OpenMenuTooltip", "스태틱 메시 아이콘 일괄 생성기를 엽니다."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Image"),
			FUIAction(FExecuteAction::CreateRaw(this, &FItemIconGeneratorEditorModule::OpenTab)));
	}

	void OpenTab()
	{
		FGlobalTabmanager::Get()->TryInvokeTab(ItemIconGeneratorEditor::TabName);
	}
};

IMPLEMENT_MODULE(FItemIconGeneratorEditorModule, ItemIconGeneratorEditor)

#undef LOCTEXT_NAMESPACE
