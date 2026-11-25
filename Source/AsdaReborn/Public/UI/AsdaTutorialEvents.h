// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ОБУЧЕНИЕ: ФЛАГИ СОБЫТИЙ ДЛЯ ШАГОВ ГИДА 1950 — общая точка «отметить событие» (10.11.2026).
//   Полный разбор системы — `Docs/Port/systems/TUTORIAL.md` (§2.3).
//
// ЧТО В ОРИГИНАЛЕ. Гид держит буфер `g_aUiToggleStateBuf` (0x2C71598); байт `+N` — «событие N произошло»
//   (обнуляются 40…99 при каждой смене шага, `uiTutorialWindowUpdate@0xEBBD40`). Байты ставят ОБЫЧНЫЕ функции игры
//   строкой `byte_2C715xx = 1` прямо в своём теле — независимо от того, открыт ли гид. Места установки сняты поиском
//   абсолютных ссылок в exe эталона (13 344 768 б) и совпали с декомпилом — таблица `EAsdaTutorialFlag` ниже.
//
// ПОРТ. Те же функции порта зовут `AsdaTutorial::Mark(…)` в тех же местах (одна строка в теле). Флаги ввода
//   (клавиши движения, кнопки мыши, сдвиг игрока) гид снимает сам в своём тике — как оригинал (`GetAsyncKeyState`).
//   Заголовок без UCLASS: его можно включать в любую .cpp без перегенерации.
#pragma once
#include "CoreMinimal.h"

class UObject;
class UGameInstance;

// Номер = смещение байта в буфере гида оригинала (0x2C71598 + N).
enum class EAsdaTutorialFlag : uint8
{
	MoveFwd         = 40,   // `g_bMoveFwd` — клавиша «вперёд»
	MoveBack        = 41,   // `g_bMoveBack`
	MoveLeft        = 42,   // `g_bMoveLeft`
	MoveRight       = 43,   // `g_bMoveRight`
	MouseWheel      = 47,   // 0x2C715C7 `MainWndProc` WM_MOUSEWHEEL (game_main.c:5872)
	BigMapClick     = 74,   // 0x2C715E2 `CBigMap_RenderAndHandleClick@0xB521F0` — клик «идти сюда» по большой карте
	ExpGained       = 75,   // 0x2C715E3 `netHandleHpExpUpdate@0xF427A0` — нотис опыта 100900
	TargetObject    = 76,   // 0x2C715E4 `Input_WorldClickHandler@0x968890` / `itemPickupGroundItem@0xC54ED0`
	QuestPacket     = 77,   // 0x2C715E5 `CQuest_RecvPacket@0xD9D260` (5044)
	MapToggle       = 78,   // 0x2C715E6 `inventory_ToggleControlBarWindow@0xD64A30` (МИСНЕЙМ: окно карты, клавиша M)
	InventoryOpen   = 79,   // 0x2C715E7 `uiOpenInventoryWindow@0xCFF300`
	ItemPickup      = 80,   // 0x2C715E8 те же два места, что 76
	PlayerFull      = 81,   // 0x2C715E9 `net_ApplyPlayerFullUpdate@0xD828A0` (5042 — итог сдачи задания)
	NpcTalkOpen     = 83,   // 0x2C715EB `regionWindow_OpenAndPathMove@0xD99290` — открыт разговор с NPC
	NpcTalkLeave    = 84,   // 0x2C715EC `UI_resetAllWindowsAndLeave_5045@0xD995C0` — уход от NPC
	MobTargetInfo   = 85,   // 0x2C715ED `hudShowMobTargetInfo@0xE7AB50` — панель цели-моба
	QuestWindowOpen = 92,   // 0x2C715F4 `uiQuestGuide_Open@0xC60280` — окно заданий
	QuestTalkPanel  = 93,   // 0x2C715F5 `uiToggleStatePanelOn@0xC5DF10` — разворот страницы разговора журнала
	QuestLogIcons   = 96,   // 0x2C715F8 `CQuestLog_RefreshSlotIcons@0xC5D500` — перерисовка значков журнала
};

namespace AsdaTutorial
{
	// Отметить событие (`byte_2C715xx = 1`). Ctx — любой объект с доступом к GameInstance.
	ASDAREBORN_API void Mark(const UObject* Ctx, EAsdaTutorialFlag Flag);

	// ★ Начало открытия разговора с NPC (`regionWindow_OpenAndPathMove@0xD99290`, первые две строки):
	//   `uiSelectFirstValidTab(книга)` — выбрать раздел книги и показать «?» 69800, затем гейт
	//   `if (книга открыта) return` — false: открыта полоса книги 69200, разговор НЕ открывается (молча).
	ASDAREBORN_API bool OnNpcTalkOpening(const UObject* Ctx);

	// Приём 6185 (`skillTutorial_RecvBlob32@0xB7ADC0`): 32 байта маски пройденных разделов с тела +0.
	ASDAREBORN_API void OnMaskPacket(const UObject* Ctx, const uint8* Data, int32 Len);
}
