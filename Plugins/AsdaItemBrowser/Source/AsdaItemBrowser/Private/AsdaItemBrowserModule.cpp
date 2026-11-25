// Copyright (c) 2026 @sitxovski (Discord)
// AsdaItemBrowserModule — точка входа редакторского модуля: вкладка «Браузер предметов Asda».
// Ничего не грузит на старте: данные (DataTable) и ассеты подтягиваются ТОЛЬКО при открытии вкладки.
// Игровые подсистемы (UGameInstanceSubsystem и пр.) модуль не трогает вовсе.
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Docking/SDockTab.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Interfaces/IMainFrameModule.h"   // ★ ревизия 13.09: вопрос о несохранённом свечении при выходе из редактора

#include "AstoryToolsMenu.h"        // ★ волна 3 п.15: общий каркас меню редакторских инструментов

#include "SAsdaItemBrowser.h"
#include "SAsdaNormalStudio.h"
#include "SAsdaFxGlowConfigurator.h"
#include "AsdaFxGlowConfigModel.h"   // ★ ревизия Ф13 (С2): снятие подписок склада снимков при выгрузке модуля

#define LOCTEXT_NAMESPACE "AsdaItemBrowserModule"

static const FName AsdaItemBrowserTabName("AsdaItemBrowser");
// Вторая вкладка того же модуля: просмотр любой модели с рельефом, снятым с её же текстуры.
static const FName AsdaNormalStudioTabName("AsdaNormalStudio");
// ★ 13.09.2026: третья вкладка — конфигуратор свечения эффектов (семейства, лут, оружие по предметам).
static const FName AsdaFxGlowConfigTabName("AsdaFxGlowConfigurator");

class FAsdaItemBrowserModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// В headless/commandlet-режиме Slate не поднят — вкладку не регистрируем (модуль просто пустой).
		if (IsRunningCommandlet() || !FSlateApplication::IsInitialized())
		{
			return;
		}

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(AsdaItemBrowserTabName,
			FOnSpawnTab::CreateRaw(this, &FAsdaItemBrowserModule::SpawnTab))
			.SetDisplayName(LOCTEXT("TabTitle", "Браузер предметов Asda"))
			.SetTooltipText(LOCTEXT("TabTooltip", "Дерево предметов item-БД с иконками и 3D-превью модели с эффектами"))
			.SetGroup(FAstoryToolsMenu::GetAsdaWorkspaceGroup())   // ★ общая группа «Asda», не штатная «Tools»
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.ContentBrowser"));

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(AsdaNormalStudioTabName,
			FOnSpawnTab::CreateRaw(this, &FAsdaItemBrowserModule::SpawnNormalStudioTab))
			.SetDisplayName(LOCTEXT("NsTabTitle", "Студия нормалей Asda"))
			.SetTooltipText(LOCTEXT("NsTabTooltip", "Любая модель проекта с рельефом из её же плоской текстуры: свет, тени, сравнение до/после"))
			.SetGroup(FAstoryToolsMenu::GetAsdaWorkspaceGroup())   // ★ общая группа «Asda»
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(AsdaFxGlowConfigTabName,
			FOnSpawnTab::CreateRaw(this, &FAsdaItemBrowserModule::SpawnGlowConfigTab))
			.SetDisplayName(LOCTEXT("GcTabTitle", "Конфигуратор свечения Asda"))
			.SetTooltipText(LOCTEXT("GcTabTooltip", "Свечение эффектов, которые не настроить в редакторе: глобальные семейства, лут, оружие и щиты по каждому предмету"))
			.SetGroup(FAstoryToolsMenu::GetAsdaWorkspaceGroup())   // ★ общая группа «Asda»
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAsdaItemBrowserModule::RegisterMenus));

		// ★ Ревизия 13.09 (п.3): выход из редактора при несохранённых правках конфигуратора — вопрос владельцу
		IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
		CanCloseEditorHandle = MainFrame.RegisterCanCloseEditor(
			IMainFrameModule::FMainFrameCanCloseEditor::CreateRaw(this, &FAsdaItemBrowserModule::CanCloseEditor));
	}

	virtual void ShutdownModule() override
	{
		// ★★★ РЕВИЗИЯ Ф13 (С2, 20.09.2026): СНЯТЬ ПОДПИСКИ СКЛАДА СНИМКОВ.
		//   Склад «как в ассете» подписан на UPackage::PackageSavedWithContextEvent,
		//   UPackage::PackageMarkedDirtyEvent и FEditorDelegates::PostUndoRedo. Подписки ставились
		//   через AddLambda, дескриптор отбрасывался, и здесь их никто не снимал: после выгрузки
		//   модуля (Live Coding, горячая пересборка плагина) делегаты указывали в выгруженный код.
		FAsdaFxGlowConfigModel::ShutdownStatics();
		if (CanCloseEditorHandle.IsValid() && FModuleManager::Get().IsModuleLoaded(TEXT("MainFrame")))
		{
			FModuleManager::GetModuleChecked<IMainFrameModule>(TEXT("MainFrame")).UnregisterCanCloseEditor(CanCloseEditorHandle);
			CanCloseEditorHandle.Reset();
		}
		if (UObjectInitialized())
		{
			// ★ Волна 3 п.15: пункты сняты через общий каркас (раньше их снимал UnregisterOwner)
			FAstoryToolsMenu::UnregisterTool("OpenAsdaItemBrowser");
			FAstoryToolsMenu::UnregisterTool("OpenAsdaNormalStudio");
			FAstoryToolsMenu::UnregisterTool("OpenAsdaFxGlowConfigurator");
			UToolMenus::UnRegisterStartupCallback(this);
			UToolMenus::UnregisterOwner(this);
		}
		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(AsdaItemBrowserTabName);
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(AsdaNormalStudioTabName);
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(AsdaFxGlowConfigTabName);
		}
	}

private:
	// Вкладка браузера предметов
	TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SAsdaItemBrowser)
			];
	}

	// Вкладка студии нормалей
	TSharedRef<SDockTab> SpawnNormalStudioTab(const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SAsdaNormalStudio)
			];
	}

	// Вкладка конфигуратора свечения
	TSharedRef<SDockTab> SpawnGlowConfigTab(const FSpawnTabArgs& Args)
	{
		TSharedRef<SAsdaFxGlowConfigurator> W = SNew(SAsdaFxGlowConfigurator);
		GlowTabs.RemoveAll([](const TWeakPtr<SAsdaFxGlowConfigurator>& P) { return !P.IsValid(); });
		GlowTabs.Add(W);
		const TWeakPtr<SAsdaFxGlowConfigurator> Weak = W;
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			// ★ Ревизия 13.09 (п.3): закрытие вкладки с несохранёнными правками — вопрос, а не молчаливая отмена
			.OnCanCloseTab_Lambda([Weak]()
			{
				const TSharedPtr<SAsdaFxGlowConfigurator> P = Weak.Pin();
				return !P.IsValid() || P->ConfirmCloseWithUnsaved();
			})
			[
				W
			];
	}

	// ★ Ревизия 13.09 (п.3): выход из редактора — тот же вопрос по каждой открытой вкладке конфигуратора
	bool CanCloseEditor()
	{
		for (const TWeakPtr<SAsdaFxGlowConfigurator>& Weak : GlowTabs)
		{
			const TSharedPtr<SAsdaFxGlowConfigurator> P = Weak.Pin();
			if (P.IsValid() && !P->ConfirmCloseWithUnsaved()) { return false; }
		}
		return true;
	}

	// Пункты раздела Asda в меню Tools.
	// ★ Волна 3 п.15 (15.09.2026): регистрация идёт через общий каркас FAstoryToolsMenu (раньше — напрямую
	//   в LevelEditor.MainMenu.Tools мимо него). Место (корень Tools, раздел «Asda» перед «Astory»), имена
	//   пунктов, подписи, подсказки, значки и порядок — прежние: владелец пользуется этими пунктами.
	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);
		const auto AddAsdaEntry = [](FName EntryName, const FText& Label, const FText& Tooltip,
			FName IconName, FName TabName, int32 SortPriority)
		{
			FAstoryToolEntry Entry;
			Entry.EntryName = EntryName;
			Entry.Label = Label;
			Entry.Tooltip = Tooltip;
			Entry.Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), IconName);
			Entry.SortPriority = SortPriority;
			Entry.RootSectionName = "Asda";
			Entry.RootSectionLabel = LOCTEXT("AsdaSection", "Asda");
			Entry.Action = FSimpleDelegate::CreateLambda([TabName]()
			{
				FGlobalTabmanager::Get()->TryInvokeTab(TabName);
			});
			FAstoryToolsMenu::RegisterTool(Entry);
		};
		AddAsdaEntry(
			"OpenAsdaItemBrowser",
			LOCTEXT("MenuEntry", "Браузер предметов Asda"),
			LOCTEXT("MenuEntryTip", "Открыть дерево предметов с 3D-превью"),
			"LevelEditor.Tabs.ContentBrowser", AsdaItemBrowserTabName, 100);
		AddAsdaEntry(
			"OpenAsdaNormalStudio",
			LOCTEXT("NsMenuEntry", "Студия нормалей Asda"),
			LOCTEXT("NsMenuEntryTip", "Модели проекта с рельефом из плоских текстур: свет, тени, сравнение до/после"),
			"LevelEditor.Tabs.Viewports", AsdaNormalStudioTabName, 200);
		AddAsdaEntry(
			"OpenAsdaFxGlowConfigurator",
			LOCTEXT("GcMenuEntry", "Конфигуратор свечения Asda"),
			LOCTEXT("GcMenuEntryTip", "Свет и сила свечения эффектов: глобальные семейства, лут, оружие и щиты по предметам (+5/+6/+7)"),
			"LevelEditor.Tabs.Details", AsdaFxGlowConfigTabName, 300);
	}

	// Открытые вкладки конфигуратора (слабые ссылки: вкладка живёт в менеджере вкладок, не в модуле)
	TArray<TWeakPtr<SAsdaFxGlowConfigurator>> GlowTabs;
	// Подписка на вопрос «можно ли закрыть редактор» (IMainFrameModule)
	FDelegateHandle CanCloseEditorHandle;
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAsdaItemBrowserModule, AsdaItemBrowser)
