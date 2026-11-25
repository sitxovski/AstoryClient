// Copyright (c) 2026 @sitxovski (Discord)
// AsdaMobBrowserModule — точка входа редакторского модуля: вкладка «Asda Mob Browser» в группе инструментов «Asda».
// Ничего не грузит на старте: таблицы и ассеты подтягиваются ТОЛЬКО при открытии вкладки. Игровые подсистемы не трогает.
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Docking/SDockTab.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"

#include "AstoryToolsMenu.h"   // общий каркас меню Tools ▸ Asda и группа вкладок «Asda»
#include "SAsdaMobBrowser.h"

#define LOCTEXT_NAMESPACE "AsdaMobBrowserModule"

static const FName AsdaMobBrowserTabName("AsdaMobBrowser");

class FAsdaMobBrowserModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// В headless/commandlet-режиме Slate не поднят — вкладку не регистрируем.
		if (IsRunningCommandlet() || !FSlateApplication::IsInitialized())
		{
			return;
		}
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(AsdaMobBrowserTabName,
			FOnSpawnTab::CreateRaw(this, &FAsdaMobBrowserModule::SpawnTab))
			.SetDisplayName(LOCTEXT("TabTitle", "Asda Mob Browser"))
			.SetTooltipText(LOCTEXT("TabTip", "All NPC and mobs: list by name/level, 3D model with animations, fields of the game tables"))
			.SetGroup(FAstoryToolsMenu::GetAsdaWorkspaceGroup())   // общая группа «Asda», как у остальных инструментов порта
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.Pawn"));

		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAsdaMobBrowserModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		if (UObjectInitialized())
		{
			FAstoryToolsMenu::UnregisterTool("OpenAsdaMobBrowser");
			UToolMenus::UnRegisterStartupCallback(this);
			UToolMenus::UnregisterOwner(this);
		}
		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(AsdaMobBrowserTabName);
		}
	}

private:
	TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SAsdaMobBrowser)
			];
	}

	// Пункт раздела «Asda» меню Tools — через общий каркас (место и вид — как у браузера предметов и редактора достижений).
	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);
		FAstoryToolEntry Entry;
		Entry.EntryName = "OpenAsdaMobBrowser";
		Entry.Label = LOCTEXT("MenuEntry", "Asda Mob Browser");
		Entry.Tooltip = LOCTEXT("MenuEntryTip", "All NPC and mobs: list (name, level), 3D preview with animations and mouse rotation, game-table fields");
		Entry.Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.Pawn");
		Entry.RootSectionName = "Asda";
		Entry.RootSectionLabel = LOCTEXT("AsdaSection", "Asda");
		Entry.SortPriority = 500;   // после браузера предметов (100), студии нормалей (200), свечения (300), достижений (400)
		Entry.Action = FSimpleDelegate::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(AsdaMobBrowserTabName);
		});
		FAstoryToolsMenu::RegisterTool(Entry);
	}
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAsdaMobBrowserModule, AsdaMobBrowser)
