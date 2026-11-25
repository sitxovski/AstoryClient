// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ОКНА ПРИ СВОЁМ ВХОДЕ/ВОСКРЕШЕНИИ (4055/4056) И ЗАГРУЗКЕ КАРТЫ — общая точка (22.10.2026).
//   До этого дня порт на своё 4055/4056 звал `UAsdaWorldSubsystem::RevealWorld` → `UAsdaWindowSubsystem::CloseAll`, то есть
//   сносил ВСЕ окна (инвентарь, персонаж, журнал, сообщество, панель отряда …). Эталон закрывает ТОЛЬКО свои списки:
//
//   4056 своё — `GameNet_OnLocalPlayerMapEntry@0xF43390`, ветка `effectSlots[112].field_40` (game_main.c:12917-13034):
//     `uiComboListCloseTradePanel(g_pItemTooltipUI)`  — TWOBUT с id 31 («воскреснуть?» → 4054) ⇒ `CloseMsgBoxIdOnMapEntry(31)`;
//     `uiStorageWindow_Close(g_bModalNoticeActive)`  — МАГАЗИН БУСТЕРОВ 15300 (имя глобала врёт; разбор у
//                                                     `UAsdaBoosterShopSubsystem::CloseWindow`);
//     `if (g_bPanel2BE5730Visible) uiInventoryResetAndLeave(g_pShopWnd)` — 0x2BE5730 = начало `g_pShopWnd` (0x2BE779C − 4·2075);
//                                                     сама функция гейтит поле 2075 «открыт ЭЛИТНЫЙ магазин 6000» ⇒ 6000;
//     `uiCloseItemComposeDialog(g_bModalNoticeActive)` — вторая половина закрытия 15300 (та же `CloseWindow`);
//     `if (g_bLevelFilterGridActive) uiLeaveAndResetAllWindows(g_pHairShopWnd)` — ПАРИКМАХЕРСКАЯ 15000;
//     `hudShowTargetNameplate` (под `g_bWorldActive`) — порт: `UAsdaTargetMarkManSubsystem` по `OnMapEntry4056`;
//     `hudHideAndResetSoundFlags` → `hudResetCursorItemSlot(g_pHUD)` — снять цель и спрятать панель цели
//                                                     ⇒ `AAsdaPlayerController::ClearTarget` (та же точка, что у двойного ЛКМ мини-статуса);
//     `uiPanelHideAll` — подсказка имени NPC 1620 (`gap_1430[180]`), метка 1660 (`[200]`), подсветка строк отряда
//                                                     (`partyHideMemberControls`) — у порта это не окна, а плашки и обводки,
//                                                     которые пересчитываются каждый кадр от наведения/цели;
//     не мёртвым (`pPacket[10] != 200`): `uiCloseOnExchangeItemType` — модалка смерти 2370/2371/2376 ⇒ `CloseDeathMsgBoxOnMapEntry`;
//                                                     `deathMenuWnd_reset` (если меню смерти есть) ⇒ меню 25000 закрывает
//                                                     `UAsdaAfterDeathSubsystem::OnCharRevived`; ONEBUT с id 2318 → «ОК» 1503
//                                                     (`sub_92A242` → `OnSendItemUpgrade(…, 1503)`) ⇒ `AnswerMsgBox(false, true)`;
//                                                     `setField_p17(g_pUIManager, 0)` ⇒ `SetPlayerWindowsHiddenFlag(false)`;
//                                                     `uiPanelClearAllTabFlags(g_pSystemMainWnd)` ⇒ `SetAllButtonsEnabled(true)`;
//                                                     `CInsDungeonWnd_RefreshState` — окна инстанса в порту нет.
//   4055 своё — `netHandler_EnterWorld@0xF43050` (network.c:42652-42690): `uiPanelHideAll`, `hudHideAndResetSoundFlags`,
//     `uiPanel_CloseAndReset` (плашка `+1337`) и `hudHideControlReset` (метка цели `gap_1430[204]`) — плашки HUD, не окна;
//     `setField_p17(…,0)`, `uiPanelClearAllTabFlags`; окно кампвара (`campWarWnd_ToggleVisible`) — в порту ещё нет.
//     Затем эталон ВСЕГДА грузит карту (`CMap_LoadLoadingScreen`, `g_nGameFlags |= 4`) — даже ту же: шаги загрузки
//     (game_main.c:3540-3725), в ПОРЯДКЕ ЭТАЛОНА: шаг 0 — автоотказ заявки в отряд (`CPartyInviteDlg_OnAccept` → 5092/5096
//     код 0), значки уведомлений хотбара (`uiNoticeIconPanelReset`), элитный ящик 69000 (`sub_92548B(g_pInventoryBagWindow)`),
//     окна-сообщения `CGameUI_CloseWindow(5001/5002/0)` (закрытие вида 0 показывает следующую запись стека), большая карта
//     (`miniMapWndClose`), затем `uiUpgradeResultSendConfirm` (имя врёт): ПОКАЗАН вопрос зова друга (1, 38) на TWOBUT →
//     5237 код 2 (`netSkill_SendBuffApply5237`, тип 2) и `CGameUI_CloseWindow(вид)` — без щелчка кнопки (звук 4 эталон здесь
//     не играет, только 5 закрытия); шаг 11 — 15300, парикмахерская, 6000, панель пакета 26500 (`uiCloseWindow(g_pPackageStampWindow)`).
//     ВСЕ пути загрузки эталона ведут сюда: 4055 (`netHandler_EnterWorld`), 5055 код 1 (`Recv_PortalChannelChange@0xF45BD0`,
//     network.c:43075-43080), 6201 (`portalHandleChannelWarp@0xF464C0`, world_map.c:8500-8516), призыв 6013, канал 6015, свиток —
//     и ВСЕГДА грузят карту, даже ту же. Порт на той же карте уровень не перегружает, поэтому список отдаётся здесь владельцам
//     окон в момент приёма пакета (соединение прежней карты ещё живо — 5092/5237 уходят ему). При перегрузке уровня окна-сообщения
//     закрываются здесь же (порядок эталона: до вопроса 38), а `UAsdaLoadManagerSubsystem::StartTargetLoad` повторно их не закрывает.
//   `sub_934201(g_pWindowMgr)` → `sub_966070@0x966070` (4055/4056, network.c:42680, game_main.c:12972): `WindowMgr+348 = 0`,
//     `+341 = 0` — снятие «гасителя» горячих клавиш, который ставят 4055/5055/6201 (`sub_92FC56` → `set_o15C`, `+348 = 1`) на время
//     загрузки; читает его `sub_9386BC` → `get_o15C@0x986470` в обработчиках клавиш (game_main.c:6140/6218/6223, ui_core.c:67351).
//     У порта отдельного периода «загрузка без экрана» нет: перегрузка уровня идёт через L_Loading, где клавиш игрового мира
//     нет, а без перегрузки (та же карта) загрузки нет вовсе — флаг не переносится [ОТМЕЧЕНО, не дефект].
//   Окна, которых в этих списках нет (инвентарь, персонаж, журнал, книга навыков, сообщество, разборка, клетка питомца,
//   улучшение снаряжения, панель отряда), эталон держит открытыми. При травеле, где виджеты сносит сам UE, их возвращает
//   `UAsdaWindowSubsystem::ReopenTravelKeptWindows` (панель отряда — `UAsdaPartySubsystem::HealRosterIfLost`).
//   [ИСПРАВЛЕНИЕ ПОРТА] Откат: `asda.UI.EntryWindowsEtalon 0` — прежнее поведение (своё 4055/4056 = `CloseAll`, окна после
//   травела не возвращаются).
#pragma once

#include "CoreMinimal.h"

class UGameInstance;

namespace AsdaEntryWindows
{
	// Ключ `asda.UI.EntryWindowsEtalon` (1 по умолчанию).
	ASDAREBORN_API bool IsEtalonEnabled();
	// Своё 4055/4056, мир уже показан: закрыть ровно окна эталона (без `CloseAll`). bDeadArrival — 4056 со state 200,
	//   bMapChange — 4055 на другую карту (порт перегрузит уровень).
	ASDAREBORN_API void ApplySelfEntry(UGameInstance* GI, int32 Opcode, bool bDeadArrival, bool bMapChange);
	// Шаги загрузки карты эталона, которые закрывают окна (game_main.c:3540-3555 и 3711-3724). bLevelReload — порт перегрузит
	//   уровень (тогда `StartTargetLoad` окна-сообщения второй раз не закрывает). Возвращает число закрытых окон.
	ASDAREBORN_API int32 ApplyMapLoadWindowCloses(UGameInstance* GI, const TCHAR* Why, bool bLevelReload);
}
