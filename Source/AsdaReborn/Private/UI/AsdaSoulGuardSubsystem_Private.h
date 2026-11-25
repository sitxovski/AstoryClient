// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSoulGuardSubsystem_Private.h — ★ R3-c 12.11.2026 (разрез AsdaSoulGuardSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaSoulGuardSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaSoulGuardSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaSoulGuardSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

// ★ цель-МОБ как точка вылета монеты (FindNetMobByRid отдаёт AAsdaMobActor*)   ★ R3: комментарий снятого включения "Npc/AsdaMobActor.h" (файл им не пользовался) — сохранён

// Категория журнала частей AsdaSoulGuardSubsystem*.cpp: определение — AsdaSoulGuardSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaSoul, Log, All );

namespace SoulUi {
// ────────────────────────────── имена узлов импортированного WBP ────────────────────────────────
// Закон jui-importer-widget-naming: интерактив/Text = ТОЧНОЕ имя idc; план = `{idc}_{idx}_{plane}`;
//   группа = `{idc}_grp`; невидимая зона хит-теста = `{idc}_{idx}_HIT`; безымянный декоратив =
//   `{имя скина}_{idx}`. Индексы взяты из самого ассета (см. отчёт, таблица 95 контролов).
	static const TCHAR* RootGroup   = TEXT("Soul_Boost_Win_grp");
	static const TCHAR* RootHit     = TEXT("Soul_Boost_Win_161_HIT");
	static const TCHAR* BarLeft     = TEXT("SOULBar_left_162_0");
	static const TCHAR* BarRight    = TEXT("SOULBar_right_163_0");
	static const TCHAR* BarLeftDrag = TEXT("SOULBar_left_162_DRAG");
	static const TCHAR* BarRightDrag= TEXT("SOULBar_right_163_DRAG");
	static const TCHAR* PinCheck    = TEXT("Soul_Boost_cbox");
	static const TCHAR* FlashRed    = TEXT("Soul_Boost_Ball_Red_222_0");
	static const TCHAR* FlashBlue   = TEXT("Soul_Boost_Ball_Blue_223_0");
	static const TCHAR* FlashGreen  = TEXT("Soul_Boost_Ball_green_224_0");
	static const TCHAR* ArrowDown   = TEXT("Soul_Boost_b_ani1_220_0");   // 56207 [76]
	static const TCHAR* ArrowUp     = TEXT("Soul_Boost_b_ani2_221_0");   // 56208 [75]
	static const TCHAR* TextsGroup  = TEXT("Soul_Boost_Texts_grp");
	static const TCHAR* TextsHit    = TEXT("Soul_Boost_Texts_235_HIT");

	static const int32 BurstPanel[3]  = { 236, 239, 242 };   // Soul_Boost_Texts_{1..3}_{idx}_0
	static const int32 BurstLabel[3]  = { 237, 240, 243 };   // Text_burst_g/b/r_{idx}_0
	static const int32 BurstDigit[3]  = { 238, 241, 244 };   // SOUL_{1..3}_{idx}_0
	static const int32 GuardPanel[3]  = { 245, 248, 251 };   // Soul_Boost_Texts_{4..6}_{idx}_0
	static const int32 GuardLabel[3]  = { 246, 249, 252 };   // Text_gaurder_g/b/r_{idx}_0
	static const int32 GuardDigit[3]  = { 247, 250, 253 };   // SOUL_{1..3}_{idx}_0
}
