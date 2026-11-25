// Copyright (c) 2026 @sitxovski (Discord)
// AsdaCraftMakeWindowSubsystem_Private.h — ★ R3-c 12.11.2026 (разрез AsdaCraftMakeWindowSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaCraftMakeWindowSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaCraftMakeWindowSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaCraftMakeWindowSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaCraftSubsystem.h"             // модель: рецепты, rate, материалы по стекам (К-6), 6602, нотисы
#include "UI/AsdaCraftWindowSubsystem.h"       // окно 76000: `uiWindowHideAndCleanup` при открытии, сводка после штуки
#include "UI/AsdaCraftRecipeWindowSubsystem.h" // окно 77000: защёлка снимка на время «закрыть всё»
#include "UI/AsdaNotice.h"                     // Loc, чат с цветом, плашка 221
#include "UI/AsdaIconCellGrid.h"               // иконки ступеней (общая точка сеток)
#include "UI/AsdaWindowSubsystem.h"            // реестр окон, «закрыть всё», снимок окон
#include "UI/AsdaInventorySubsystem.h"         // продукт в сумку (`OnRecv_ItemUpdate`), самотест: материалы
#include "UI/AsdaHudSubsystem.h"               // `CItemManager_NotifyItemInfo` — анонс редкого предмета
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaCharaInfoSubsystem.h"         // самотест снимка: слот 1
#include "UI/AsdaSkillWindowSubsystem.h"       // самотест снимка: слот 13
#include "UI/AsdaExtractShopSubsystem.h"       // самотест: 30035 (окно 16000)
#include "UI/AsdaEquipShopSubsystem.h"         // самотест: 30020 (магазин снаряжения 6800)
#include "UI/AsdaPremiumShopSubsystem.h"       // самотест: элитный магазин 6000 (снимок окон)
#include "UI/AsdaPetCageSubsystem.h"           // самотест: слот 9 и «закрыть всё»
#include "Quest/AsdaQuestLogSubsystem.h"       // самотест снимка: слот 4
#include "Npc/AsdaNpcDialogSubsystem.h"        // самотест: вход/выход из разговора
#include "UI/AsdaChatSubsystem.h"              // самотест В5: плашка 221 после смены карты
#include "UI/AsdaTitleSubsystem.h"             // самотест В5: значок званий в снимке окон (слот 8)
#include "UI/AsdaHotbarSubsystem.h"            // самотест В8 (С-1): хотбар разблокирован после сброса сессии
#include "UI/AsdaSystemMenuSubsystem.h"         // самотест В8 (С-1): трей меню развёрнут после сброса сессии
#include "AsdaNetSubsystem.h"
#include "AsdaOutgoingGate.h"                // Р-2: RefuseDeferredStartWhenOnline — повторная проверка офлайна на отложенном старте
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaCVarSave.h"                    // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотестов — запомнить и вернуть (значение + SetBy)
#include "AsdaBytes.h"
#include "AsdaSessionReset.h"
#include "Items/AsdaItemTableSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Layout/ArrangedChildren.h"          // самотест С-1: фактический порядок рисования детей канваса (Slate)
#include "Widgets/SWidget.h"
#include "Components/EditableText.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "TimerManager.h"
#include "UnrealClient.h"                      // FScreenshotRequest — именованный кадр самотеста
#include "Text/AsdaLang.h"   // ★ 08.11.2026: самотест asda.SelfTest.CraftMoveStop
#include "UI/AsdaMinimapSubsystem.h"   // ★ 08.11.2026: самотест asda.SelfTest.CraftMoveStop

// Категория журнала частей AsdaCraftMakeWindowSubsystem*.cpp: определение — AsdaCraftMakeWindowSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaCraftMake, Log, All );

namespace {
	// ★ Ревизия В4 МЕЛК-6: видимость узла — общая точка `AsdaUi::IsShown`, здесь только имя.
	bool IsShown(const UWidget* W) { return AsdaUi::IsShown(W); }

// ═════════════════════════════ САМОТЕСТ ═══════════════════════════════════════════════════════
	// Тело 6603 (525 б, `recvDBCraftEnter`, раскладка — шапка `AsdaCraftSubsystem.h`): код, ступень, «+1 шт.», продукт и
	//   до шести записей материалов {код, контейнер, слот, остаток}.
	struct FMat { int32 Code; int32 Cell; int32 Left; };
	TArray<uint8> Build6603(int32 Code, int32 Reached, int32 Bonus, int32 ProductCode, int32 ProductCell, int32 ProductQty,
		const TArray<FMat>& Mats, int32 Level = 6)
	{
		TArray<uint8> B; B.SetNumZeroed(525);
		B[0] = (uint8)Code;
		AsdaBytes::WrI16(&B[1], 0);                    // вес 0 — сетевой слой не трогает
		B[3] = (uint8)Level;
		AsdaBytes::WrI32(&B[4], 10);
		AsdaBytes::WrI16(&B[8], 5);
		B[10] = (uint8)Reached;
		B[11] = (uint8)Bonus;
		if (ProductCode > 0)
		{
			AsdaBytes::WrI32(&B[12], ProductCode); B[12 + 4] = 2; AsdaBytes::WrI16(&B[12 + 5], ProductCell); AsdaBytes::WrI32(&B[12 + 9], ProductQty);
		}
		else
		{
			AsdaBytes::WrI16(&B[12 + 5], -1);          // `sub_40A9F0`: код 0, контейнер 0, слот −1
		}
		for (int32 K = 0; K < Mats.Num() && K < 6; ++K)
		{
			uint8* R = &B[69 + 57 * K];
			AsdaBytes::WrI32(R, Mats[K].Code); R[4] = 2; AsdaBytes::WrI16(R + 5, Mats[K].Cell); AsdaBytes::WrI32(R + 9, Mats[K].Left);
		}
		return B;
	}
}

// ★ R3-c 12.11.2026: многоимённая константа без состояния — нужна нескольким частям (перенесено дословно из AsdaCraftMakeWindowSubsystem.cpp)
namespace {
	// Dword-индексы контролов `CMakeingItem` (шапка `.h`): базы групп.
	constexpr int32 kDwFMark = 2, kDwMake = 4, kDwAni = 11, kDwBack = 18, kDwFail = 25;
}
