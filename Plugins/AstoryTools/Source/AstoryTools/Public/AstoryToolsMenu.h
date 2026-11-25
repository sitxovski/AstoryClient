// Copyright (c) 2026 @sitxovski (Discord)
// FAstoryToolsMenu — ★ ОБЩАЯ ТОЧКА раздела меню «Tools ▸ Astory_Tools».
//
// Зачем отдельная точка. Раздел задуман РАСШИРЯЕМЫМ: сюда пойдут будущие редакторы
// (магазинов, дропа, цен и пр.). Чтобы каждый новый редактор не переписывал регистрацию меню
// заново (и не заводил свой собственный пункт в корне Tools, как это сделал плагин
// `AsdaItemBrowser`), он просто зовёт `FAstoryToolsMenu::RegisterTool(...)` из своего
// `StartupModule()` — пункт сам появится в подменю в порядке `SortPriority`.
//
// Использование из нового редактора:
//   FAstoryToolsMenu::RegisterTool(FAstoryToolEntry{
//       TEXT("MyShopEditor"),                                  // уникальное имя пункта
//       LOCTEXT("MyShop", "Редактор магазинов"),               // подпись
//       LOCTEXT("MyShopTip", "Что делает"),                    // подсказка
//       FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"),
//       FSimpleDelegate::CreateStatic(&OpenMyShopEditor),      // что вызвать
//       200 });                                                // порядок в подменю
#pragma once

#include "CoreMinimal.h"
#include "Textures/SlateIcon.h"
#include "Delegates/Delegate.h"
#include "WorkspaceMenuStructure.h"   // FWorkspaceItem — группа вкладок «Asda»

// Один пункт подменю «Astory_Tools».
struct ASTORYTOOLS_API FAstoryToolEntry
{
	// Уникальное имя пункта (FName в меню). Повторная регистрация с тем же именем — замена.
	FName EntryName;
	// Подпись пункта в меню.
	FText Label;
	// Подсказка при наведении.
	FText Tooltip;
	// Значок пункта (берётся из стиля редактора, свои иконки не заводим).
	FSlateIcon Icon;
	// Что выполнить по клику (обычно — открыть вкладку инструмента).
	FSimpleDelegate Action;
	// Порядок в подменю: меньше — выше. Шаг 100, чтобы можно было вставлять между.
	int32 SortPriority = 1000;
	// ★ Волна 3 п.15 (15.09.2026): где стоит пункт. NAME_None — в подменю «Astory_Tools» (по умолчанию).
	//   Иначе — в разделе с этим именем прямо в корне меню Tools. Так живут пункты браузера предметов
	//   (раздел «Asda»): владелец ими пользуется, поэтому место, подписи и порядок сохранены как были.
	//   Корневые разделы каркаса стоят в меню ПЕРЕД разделом «Astory» (исторический порядок).
	FName RootSectionName = NAME_None;
	// Подпись корневого раздела (используется, только если задан RootSectionName).
	FText RootSectionLabel;
};

class ASTORYTOOLS_API FAstoryToolsMenu
{
public:
	// Зарегистрировать пункт подменю. Можно звать до инициализации UToolMenus — пункт
	//   запомнится и появится, когда меню будет построено.
	static void RegisterTool(const FAstoryToolEntry& Entry);

	// Снять пункт (вызывать из ShutdownModule() владельца пункта).
	static void UnregisterTool(FName EntryName);

	// Полное имя раздела/подменю — на случай, если кому-то понадобится расширить его напрямую
	//   средствами UToolMenus (`UToolMenus::Get()->ExtendMenu(FAstoryToolsMenu::GetSubMenuName())`).
	static FName GetSubMenuName();

	// ★★★ ОБЩАЯ ГРУППА «Asda» В СПИСКЕ ВКЛАДОК (меню Window и палитра вкладок).
	//   Зачем: вкладки наших инструментов лежали в штатной категории «Tools» вперемешку со вкладками
	//   самого редактора, и найти их там трудно. Теперь у них своя группа рядом с остальными.
	//   ★ Точка ОБЩАЯ намеренно (закон port-shared-points): если каждый плагин заведёт группу сам,
	//   в меню появятся ДВЕ одноимённые группы «Asda» и инструменты снова окажутся порознь.
	//   Звать только когда Slate поднят (в commandlet-режиме структура меню не существует).
	static TSharedRef<FWorkspaceItem> GetAsdaWorkspaceGroup();

	// Перестроить подменю (зовётся автоматически из Register/Unregister).
	static void RefreshMenu();

	// Внутреннее: построение подменю. Зовётся модулем AstoryTools при готовности UToolMenus.
	static void BuildMenu();
};
