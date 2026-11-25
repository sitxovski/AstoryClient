// Copyright (c) 2026 @sitxovski (Discord)
// AstoryTools — точка входа редакторского модуля.
//
// Что делает на старте:
//   1) заводит РАСШИРЯЕМОЕ подменю «Tools ▸ Astory_Tools» (общая точка FAstoryToolsMenu);
//   2) регистрирует nomad-вкладку «Редактор достижений Asda» и её пункт в этом подменю.
// Ничего не грузит: CSV/BIN/серверные таблицы читаются ТОЛЬКО при открытии вкладки.
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Docking/SDockTab.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "ToolMenuSection.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#include "AstoryToolsMenu.h"
#include "SAstoryAchievementsEditor.h"

#define LOCTEXT_NAMESPACE "AstoryTools"

// ─────────────────────────────────────────────────────────────────────────────
// FAstoryToolsMenu — реестр пунктов подменю
// ─────────────────────────────────────────────────────────────────────────────
namespace AstoryToolsMenuInternal
{
	// Реестр переживает горячую пересборку модуля-владельца пункта: пункт снимается явным
	//   UnregisterTool(), а не «по счастливой случайности».
	static TArray<FAstoryToolEntry>& Registry()
	{
		static TArray<FAstoryToolEntry> Entries;
		return Entries;
	}

	static bool bMenusReady = false;

	// ★ Волна 3 п.15: пункты, добавленные каркасом в КОРНЕВЫЕ разделы Tools (раздел, имя пункта).
	//   Корневой раздел не пересоздаётся (его место в меню не должно «уезжать» при каждой перестройке),
	//   поэтому при перестройке снимаются только ранее добавленные НАМИ пункты.
	static TArray<TPair<FName, FName>>& RootAdded()
	{
		static TArray<TPair<FName, FName>> Added;
		return Added;
	}
}

TSharedRef<FWorkspaceItem> FAstoryToolsMenu::GetAsdaWorkspaceGroup()
{
	// Группа создаётся ОДИН раз и переживает горячую пересборку модулей: обе стороны (этот плагин и
	//   браузер предметов) получают ОДНУ И ТУ ЖЕ группу, иначе в меню было бы две одноимённые.
	static TSharedPtr<FWorkspaceItem> Group;
	if (!Group.IsValid())
	{
		Group = WorkspaceMenu::GetMenuStructure().GetToolsCategory()->AddGroup(
			FName(TEXT("AsdaTools")),
			LOCTEXT("AsdaWorkspaceGroup", "Asda"),
			LOCTEXT("AsdaWorkspaceGroupTip", "Редакторские инструменты порта Asda"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Star"));
	}
	return Group.ToSharedRef();
}

FName FAstoryToolsMenu::GetSubMenuName()
{
	// Полное имя подменю в системе UToolMenus (для ExtendMenu из сторонних модулей).
	return FName(TEXT("LevelEditor.MainMenu.Tools.AstoryTools"));
}

void FAstoryToolsMenu::RegisterTool(const FAstoryToolEntry& Entry)
{
	TArray<FAstoryToolEntry>& Entries = AstoryToolsMenuInternal::Registry();
	const int32 Existing = Entries.IndexOfByPredicate(
		[&Entry](const FAstoryToolEntry& E) { return E.EntryName == Entry.EntryName; });
	if (Existing != INDEX_NONE)
	{
		Entries[Existing] = Entry;
	}
	else
	{
		Entries.Add(Entry);
	}
	RefreshMenu();
}

void FAstoryToolsMenu::UnregisterTool(FName EntryName)
{
	AstoryToolsMenuInternal::Registry().RemoveAll(
		[EntryName](const FAstoryToolEntry& E) { return E.EntryName == EntryName; });
	RefreshMenu();
}

void FAstoryToolsMenu::RefreshMenu()
{
	// До готовности UToolMenus строить нечего — пункты просто лежат в реестре и появятся
	//   при BuildMenu() из StartupCallback.
	if (AstoryToolsMenuInternal::bMenusReady && UToolMenus::IsToolMenuUIEnabled())
	{
		BuildMenu();
		UToolMenus::Get()->RefreshAllWidgets();
	}
}

void FAstoryToolsMenu::BuildMenu()
{
	if (!UToolMenus::IsToolMenuUIEnabled())
	{
		return;
	}
	AstoryToolsMenuInternal::bMenusReady = true;

	UToolMenus* Menus = UToolMenus::Get();

	// 1) Пункт-подменю в корневом меню Tools.
	if (UToolMenu* ToolsMenu = Menus->ExtendMenu("LevelEditor.MainMenu.Tools"))
	{
		// ★ 16.09.2026: подменю «Astory_Tools» заводим ТОЛЬКО если в него кто-то зарегистрировался.
		//   После переноса всех инструментов в раздел «Asda» (просьба владельца) оно оставалось висеть
		//   в меню ПУСТЫМ, с одной записью-заглушкой «(инструментов не зарегистрировано)» — проверка
		//   реальным путём (`Astory.Tools.DumpMenu`) это и показала. Механизм остаётся расширяемым:
		//   зарегистрируется будущий редактор без `RootSectionName` — подменю появится снова само.
		const bool bHaveSubMenuTools = AstoryToolsMenuInternal::Registry().ContainsByPredicate(
			[](const FAstoryToolEntry& E) { return E.RootSectionName.IsNone(); });
		if (bHaveSubMenuTools)
		{
			FToolMenuSection& Section = ToolsMenu->FindOrAddSection(
				"Astory", LOCTEXT("AstorySection", "Asda Story"));
			// AddSubMenu идемпотентен по имени: повторный вызов заменяет запись, дублей не будет.
			Section.AddSubMenu(
				"AstoryTools",
				LOCTEXT("AstoryToolsMenu", "Astory_Tools"),
				LOCTEXT("AstoryToolsMenuTip", "Редакторы игровых таблиц AsdaStory: достижения и т. д."),
				FNewToolMenuChoice(),
				/*bInOpenSubMenuOnClick=*/false,
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Toolbar.Settings"));
		}

		// 1б) ★ Волна 3 п.15: пункты корневых разделов (RootSectionName задан), напр. «Asda» браузера предметов.
		//   Раздел ставится ПЕРЕД «Astory»: раньше плагин AsdaItemBrowser грузился раньше AstoryTools и его раздел
		//   появлялся первым; теперь браузер зависит от AstoryTools, и порядок держим явной позицией вставки.
		for (const TPair<FName, FName>& Prev : AstoryToolsMenuInternal::RootAdded())
		{
			if (FToolMenuSection* Old = ToolsMenu->FindSection(Prev.Key))
			{
				// RemoveEntry у FToolMenuSection закрытый — снимаем свой пункт из открытого списка блоков.
				const FName EntryName = Prev.Value;
				Old->Blocks.RemoveAll([EntryName](const FToolMenuEntry& Block) { return Block.Name == EntryName; });
			}
		}
		AstoryToolsMenuInternal::RootAdded().Reset();

		TArray<FAstoryToolEntry> RootSorted = AstoryToolsMenuInternal::Registry().FilterByPredicate(
			[](const FAstoryToolEntry& E) { return !E.RootSectionName.IsNone(); });
		RootSorted.StableSort([](const FAstoryToolEntry& A, const FAstoryToolEntry& B)
		{
			return A.SortPriority < B.SortPriority;
		});
		for (const FAstoryToolEntry& Entry : RootSorted)
		{
			FToolMenuSection& RootSection = ToolsMenu->FindOrAddSection(
				Entry.RootSectionName, Entry.RootSectionLabel,
				FToolMenuInsert("Astory", EToolMenuInsertType::Before));
			FSimpleDelegate Action = Entry.Action;
			RootSection.AddMenuEntry(
				Entry.EntryName,
				Entry.Label,
				Entry.Tooltip,
				Entry.Icon,
				FUIAction(FExecuteAction::CreateLambda([Action]()
				{
					Action.ExecuteIfBound();
				})));
			AstoryToolsMenuInternal::RootAdded().Emplace(Entry.RootSectionName, Entry.EntryName);
		}
	}

	// 2) Наполнение подменю из реестра.
	if (UToolMenu* SubMenu = Menus->ExtendMenu(GetSubMenuName()))
	{
		// Раздел пересобирается целиком, поэтому сначала снимаем прошлый.
		SubMenu->RemoveSection("AstoryToolsEntries");
		FToolMenuSection& Section = SubMenu->AddSection(
			"AstoryToolsEntries", LOCTEXT("AstoryToolsEntriesSection", "Инструменты"));

		// В подменю — только пункты без корневого раздела.
		TArray<FAstoryToolEntry> Sorted = AstoryToolsMenuInternal::Registry().FilterByPredicate(
			[](const FAstoryToolEntry& E) { return E.RootSectionName.IsNone(); });
		Sorted.StableSort([](const FAstoryToolEntry& A, const FAstoryToolEntry& B)
		{
			return A.SortPriority < B.SortPriority;
		});

		for (const FAstoryToolEntry& Entry : Sorted)
		{
			FSimpleDelegate Action = Entry.Action;
			Section.AddMenuEntry(
				Entry.EntryName,
				Entry.Label,
				Entry.Tooltip,
				Entry.Icon,
				FUIAction(FExecuteAction::CreateLambda([Action]()
				{
					Action.ExecuteIfBound();
				})));
		}

		if (Sorted.Num() == 0)
		{
			// Пустое подменю без пояснения выглядит как поломка — пишем прямо.
			Section.AddMenuEntry(
				"AstoryToolsEmpty",
				LOCTEXT("AstoryToolsEmpty", "(инструментов не зарегистрировано)"),
				LOCTEXT("AstoryToolsEmptyTip", "Ни один редакторский модуль не вызвал FAstoryToolsMenu::RegisterTool()"),
				FSlateIcon(),
				FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Модуль
// ─────────────────────────────────────────────────────────────────────────────
static const FName AstoryAchievementsTabName("AstoryAchievementsEditor");

class FAstoryToolsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// В headless/commandlet-режиме Slate не поднят — вкладку не регистрируем.
		if (IsRunningCommandlet() || !FSlateApplication::IsInitialized())
		{
			return;
		}

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(AstoryAchievementsTabName,
			FOnSpawnTab::CreateRaw(this, &FAstoryToolsModule::SpawnAchievementsTab))
			.SetDisplayName(LOCTEXT("AchTabTitle", "Редактор достижений Asda"))
			.SetTooltipText(LOCTEXT("AchTabTip", "Просмотр и правка 280 званий (Fame) со сверкой с серверной FameInfo.txt"))
			.SetGroup(FAstoryToolsMenu::GetAsdaWorkspaceGroup())   // ★ общая группа «Asda», не штатная «Tools»
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Star"));

		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAstoryToolsModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		if (UObjectInitialized())
		{
			FAstoryToolsMenu::UnregisterTool("OpenAstoryAchievementsEditor");
			UToolMenus::UnRegisterStartupCallback(this);
			UToolMenus::UnregisterOwner(this);
		}
		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(AstoryAchievementsTabName);
		}
	}

private:
	TSharedRef<SDockTab> SpawnAchievementsTab(const FSpawnTabArgs& Args)
	{
		TSharedRef<SDockTab> Tab = SNew(SDockTab).TabRole(ETabRole::NomadTab);
		TSharedRef<SAstoryAchievementsEditor> Editor = SNew(SAstoryAchievementsEditor).OwnerTab(Tab);
		Tab->SetContent(Editor);
		// ★ Подтверждение при закрытии вкладки с несохранёнными правками.
		Tab->SetCanCloseTab(SDockTab::FCanCloseTab::CreateSP(Editor, &SAstoryAchievementsEditor::CanCloseTab));
		return Tab;
	}

	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);

		FAstoryToolEntry Entry;
		Entry.EntryName = "OpenAstoryAchievementsEditor";
		Entry.Label = LOCTEXT("AchMenuEntry", "Achievements Editor");
		Entry.Tooltip = LOCTEXT("AchMenuEntryTip",
			"Просмотрщик и редактор званий (достижений): 280 записей, сверка с серверной FameInfo.txt, "
			"выгрузка файла для сервера");
		Entry.Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Star");
		// ★ 16.09.2026 (просьба владельца): пункт переехал из подменю «Astory_Tools» в раздел «Asda»
		//   меню Tools — туда же, где живут остальные инструменты порта. Порядок 400: после трёх
		//   пунктов браузера предметов (100/200/300), шаг тот же.
		Entry.RootSectionName = "Asda";
		Entry.RootSectionLabel = LOCTEXT("AsdaSection", "Asda");
		Entry.SortPriority = 400;
		Entry.Action = FSimpleDelegate::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(AstoryAchievementsTabName);
		});

		FAstoryToolsMenu::BuildMenu();          // подменю существует до первой регистрации
		FAstoryToolsMenu::RegisterTool(Entry);
	}
};

// ═════════════════════════════════════════════════════════════════════════════
// Проверка меню без открытия окон: генерируем подменю «Astory_Tools» и печатаем его состав.
//   Нужна, чтобы убедиться, что пункт Tools ▸ Astory_Tools ▸ Achievements Editor на месте,
//   не открывая владельцу редактор.
// ═════════════════════════════════════════════════════════════════════════════
static FAutoConsoleCommand GAstoryToolsDumpMenuCmd(
	TEXT("Astory.Tools.DumpMenu"),
	TEXT("Astory_Tools: напечатать состав подменю Tools ▸ Astory_Tools."),
	FConsoleCommandDelegate::CreateStatic([]()
	{
		if (!UToolMenus::IsToolMenuUIEnabled())
		{
			UE_LOG(LogTemp, Warning, TEXT("[Astory_Tools] меню недоступно (UI меню выключен)"));
			return;
		}
		UToolMenus* Menus = UToolMenus::Get();

		// 1) Есть ли сам пункт-подменю в корневом меню Tools.
		bool bFoundSubMenu = false;
		if (UToolMenu* Tools = Menus->GenerateMenu("LevelEditor.MainMenu.Tools", FToolMenuContext()))
		{
			for (const FToolMenuSection& S : Tools->Sections)
			{
				for (const FToolMenuEntry& E : S.Blocks)
				{
					if (E.Name == FName("AstoryTools"))
					{
						bFoundSubMenu = true;
						UE_LOG(LogTemp, Log, TEXT("[Astory_Tools] пункт в Tools: раздел «%s», имя «%s»"),
							*S.Label.Get().ToString(), *E.Name.ToString());
					}
				}
			}
		}
		if (!bFoundSubMenu)
		{
			// ★ 16.09.2026: подменю может отсутствовать ЗАКОННО — если все инструменты зарегистрированы
			//   в корневые разделы (сейчас все четыре живут в разделе «Asda» по просьбе владельца).
			//   Ошибкой это остаётся только тогда, когда пункты для подменю ЕСТЬ, а самого подменю нет.
			const bool bWantSubMenu = AstoryToolsMenuInternal::Registry().ContainsByPredicate(
				[](const FAstoryToolEntry& E) { return E.RootSectionName.IsNone(); });
			if (bWantSubMenu)
			{
				UE_LOG(LogTemp, Error, TEXT("[Astory_Tools] ★ подменю в Tools НЕ НАЙДЕНО, хотя пункты для него есть"));
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("[Astory_Tools] подменю не создано — все инструменты стоят в корневых разделах Tools (норма)"));
			}
		}

		// 1б) ★ Волна 3 п.15: пункты каркаса в корневых разделах Tools (напр. «Asda» браузера предметов).
		for (const TPair<FName, FName>& Added : AstoryToolsMenuInternal::RootAdded())
		{
			UE_LOG(LogTemp, Log, TEXT("[Astory_Tools] корень Tools, раздел «%s»: ▸ %s"),
				*Added.Key.ToString(), *Added.Value.ToString());
		}

		// 2) Что внутри подменю.
		if (UToolMenu* Sub = Menus->GenerateMenu(FAstoryToolsMenu::GetSubMenuName(), FToolMenuContext()))
		{
			for (const FToolMenuSection& S : Sub->Sections)
			{
				for (const FToolMenuEntry& E : S.Blocks)
				{
					UE_LOG(LogTemp, Log, TEXT("[Astory_Tools]   ▸ %s  («%s»)"),
						*E.Name.ToString(), *E.Label.Get().ToString());
				}
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[Astory_Tools] ★ подменю %s не генерируется"),
				*FAstoryToolsMenu::GetSubMenuName().ToString());
		}
	}));

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAstoryToolsModule, AstoryTools)
