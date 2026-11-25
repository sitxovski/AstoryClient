// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEscChain — шаг «ESC снимает выбранную цель» эталонной цепочки ESC. [ОРИГИНАЛ] 25.10.2026 (баг игроков: «ESC не убирает
//   плашку цели»). Лёгкий заголовок без UCLASS: его подключают опрос ESC мира (`UAsdaSystemOptionSubsystem::PollHotkeys`) и
//   препроцессор Slate (`UAsdaEnterConfirmSubsystem::HandleEscDown` — ESC под модалкой и из поля ввода).
//
// ═════════════════════════ ЭТАЛОН: ЧТО ДЕЛАЕТ ОДНО НАЖАТИЕ ESC (сборка IDB, 13 344 768 б) ═════════════════════════════
//   Отпускание клавиши (`MainWndProc` → `CWindowMgr_QueueKeyUp`, тип 2; из поля ввода — `ctrlEditImeWndProc`, ui_core.c:116210)
//   разбирает `CControl_DispatchMessage@0x12B2440`: сначала `vtable+2` менеджера окон = `uiContainerHandleKey@0x12AF390`
//   (vtable 0x14FE8DC, слот 2 — снято с бинаря), затем ВСЕГДА, при любом исходе и под модалкой тоже, колбэк
//   `GameInput_DispatchEvent@0xC6DCA0` (`sub_92EF27(g_pWindowMgr, …)`, `this->eventCallback`). Порядок:
//     1) `uiContainerHandleKey`: модальный контрол (+0x1A0) — только он; иначе верхнее видимое корневое окно с признаком
//        `CControl+0x228` получает событие 21 и `CWindowMgr+0x208 = 1` («нажатие съедено»). Это `CloseTopmostOnEsc` порта.
//     2) `GameInput_DispatchEvent` case 2, key 27 (game_main.c:9265-9359):
//        а) `get_o215(g_pWindowMgr)` (`CWindowMgr+0x215`) → `set_o215(…, 0)` и выход. Флаг ставит НЕ проход, а оконная процедура
//           закрытого окна в своей ветке события 21 — у 16 окон из таблицы ESC (15 по 19 вызовам `sub_92CA8D(g_pWindowMgr, 1)` в
//           декомпиле + один, которого в декомпиле нет: 0xDB91EB, окно 8500, — 23 ссылки на `set_o215` в бинаре). Перечень —
//           `kEscFullyConsumed` в .cpp. Окно без этой строки (меню 69400, настройки 69500, улучшение 20000, ONEBUT 1500 — он
//           только «съедает», 2670 меню вкладок чата и др.) флаг НЕ ставит ⇒ то же нажатие идёт дальше;
//        б) цель выбрана (`g_nTargetUserIdx != -1`) и её вид (`g_nInputControlState`) — 3 NPC / 0 персонаж (и сам игрок) /
//           1 моб: `g_nTargetUserIdx = -1`, вид = 11, и прячется плашка своего вида (`uiPanel_CloseAndReset`,
//           `uiPanelHideAll`, `hudHideAndResetSoundFlags`) ⇒ выход. Гейтов (модалка, перетаскивание, меню смерти) у шага НЕТ;
//        в) дальше — ветки состояния своего персонажа, закрытие строки чата (`uiNameInputConfirmShow`) и последним — показ
//           ESC-меню 69400 под гейтами (`!CUIManager+0x04 && !CWindowMgr+0x200 && !CWindowMgr+0x208 …`).
//   Окно разговора NPC 570 (`NPC_LINE NPC`) признака 0x228 не имеет, события 21 его процедура `uiWindowMsgHandler@0xD91EC0` не
//   разбирает, `UI_resetAllWindowsAndLeave_5045` из цепочки ESC не зовётся (10 вызовов — клик по миру, шаг, магазины) ⇒
//   ESC разговор НЕ закрывает: снимается только выделение NPC, разговор остаётся.
//
// ПОРТ. Шаг (б) жил в комментарии `PollHotkeys` как «принадлежит контроллеру» и не был перенесён ни в одно место — отсюда
//   жалоба. Выбор и цель атаки у порта — одно поле (`AAsdaPlayerController::TargetActor`), у эталона — два
//   (`g_nTargetUserIdx` и `LocalPlayer+0x32DFC`): `ClearTarget` заодно гасит автоатаку. [ПОРТ] — иначе пришлось бы делить поле.
//
// ═══ ВЕТКИ (в) — СОСТОЯНИЕ СВОЕГО ПЕРСОНАЖА (game_main.c:9292-9359, дизасм 0xC6DE1B…0xC6E0F0) ═══════════════════════════════
//   Поля персонажа (`CCharacter`, размер 208384 = 0x32E00; `pChar[1].X` = 208384 + смещение X):
//     +190204 (`+47551`)  состояние действия (порт `KbEtalonActionState`: 0 покой, 1 бег, 5 удар, 6 смерть, 7/8/9 сидение,
//                         24 навык, 33…46 эмоция, 40/43 лопата, 60…67 рыбалка, 68 добыча);
//     +190568 (`+47642`)  подрежим покоя `effectSlots[112].field_174`: 10/20 — боевой покой (`CChar_SetState(…, 0, 0, 10/20)`);
//     +208468 [1].netWord54  renderId моба, ударившего меня (`Combat_ApplyDamage@0xF7C470`, только урон: значение ∉ {−1,−2,0}),
//                         сброс −1 при каждом входе в состояние 0 (`CCharacter_SetAction@0x9EE720` case 0);
//     +208484 [1].percent64  отложенное действие 52121 (100 — держать автоатаку, 102 — навык);
//     +208664 [1].field118   «заявка удара» (1 после 4026 `netSabzil_SendActionRaw`, после клика PvP; 0 при покое/выходе из 5/24);
//     +208386 [1].gap_0[2]   режим-маска цели навыка (бит 0 — цель-моб; `Recv_SkillCast`).
//   `objStateAllowsRender@0xA2A2F0` (миснейм, по смыслу «персонаж в бою»): состояние 0 и подрежим 10/20 → да; 0 / 1 / 7…9 /
//     33…47 / 51 / 60…67 → «есть ли моб netWord54 в мире»; любое другое состояние → да.
//   Ветки: «в бою» — 5/24: 52121 = 0 и выход; 1/2: field118 → 4008 (`Net_SendActionPacket`, OP_CHAR_STOP) в своей точке +
//     покой (`CCharacter_SetAction(0)`), маска бит 0 → то же; иначе — выход (меню НЕТ). «Не в бою» — 1/2: field118 → 4008 +
//     покой, выход (меню НЕТ); иначе — строка чата открыта → стереть и закрыть (`uiNameInputConfirmShow`), выход; иначе — меню.
//   4024 ни одна ветка не шлёт — страж кода действия P81 (`AllowedCodes`) их не касается; 4008 сервер не гейтит.
#pragma once

#include "CoreMinimal.h"

class UGameInstance;

namespace AsdaEscChain
{
	// Ключ `asda.UI.EscClearTarget` (1 — шаг «ESC снимает цель» включён; 0 — как до 25.10.2026: цель ESC не снимается).
	bool ClearTargetOn();
	// Ключ `asda.UI.EscFromTextField` (1 — ESC из поля ввода вне модалки идёт по цепочке эталона: верхнее окно, затем цель).
	bool FromTextFieldOn();

	// Окно, ответившее последнему проходу `UAsdaWindowSubsystem::CloseTopmostOnEsc` (закрыло себя или «съело» нажатие);
	//   NAME_None — проход ничего не тронул. Пишет сам проход (AsdaWindowSubsystem_Close.cpp).
	FName LastAnsweredWindow();
	// Запись прохода: кто ответил (зовёт только `CloseTopmostOnEsc`).
	void NoteAnsweredWindow(FName WindowId);

	// Ставит ли оконная процедура окна эталонный флаг `CWindowMgr+0x215` в ветке события 21 (ESC целиком потрачен окном).
	bool IsFullyConsumedWindow(FName WindowId);

	// Шаг (а)+(б) эталона ПОСЛЕ прохода окон. `bWindowAnswered` — итог `CloseTopmostOnEsc` этого же нажатия.
	//   Возврат true — нажатие потрачено здесь (окно с флагом 0x215 или снята цель): вызывающий дальше НЕ идёт (меню 69400 не
	//   тоггается, поле ввода ESC не получает). false — цепочка продолжается прежним путём. `Path` — для журнала.
	//   ★ 25.10.2026 [ИСПРАВЛЕНИЕ]: между (а) и (б) — закрытие разговора NPC (`CloseNpcDialogOn`), если это нажатие не закрыло
	//   окно и `bAllowDialog` (под модалкой разговор под ней не трогаем).
	bool RunAfterWindowPass(UGameInstance* GI, bool bWindowAnswered, const TCHAR* Path, bool bAllowDialog = true);

	// Сколько раз шаг снял цель (самотест).
	int32 GetClearCount();

	// Ключ `asda.UI.EscCloseNpcDialog` (1 — [ИСПРАВЛЕНИЕ] решение владельца 25.10.2026: ESC закрывает разговор NPC 570 штатным
	//   закрытием кнопки 575 после верхнего окна и до снятия цели; 0 — как у оригинала, разговор ESC не закрывает).
	bool CloseNpcDialogOn();
	// Ключ `asda.UI.EscStateBranches` (1 — ветки (в) по состоянию своего персонажа как у эталона; 0 — как до 25.10.2026).
	bool StateBranchesOn();

	// Ветки (в) эталона: состояние своего персонажа, затем строка чата. Возврат true — нажатие потрачено (меню 69400 НЕ
	//   открывается, поле ввода ESC не получает); false — дальше гейты и меню. Строку чата закрывает и при ключе 0.
	bool RunStateBranches(UGameInstance* GI, const TCHAR* Path);
	// Покадровый учёт для `netWord54`: вход своего персонажа в состояние 0 стирает «кто меня ударил» (зовёт опрос ESC мира).
	void TickSelfState(UGameInstance* GI);
	// Моб ударил своего персонажа на кадре удара (порт `Combat_ApplyDamage`): запомнить renderId, если это урон (∉ {−1,−2,0}).
	void NoteSelfHitByMob(int32 MobRid, int32 HitValue);
	// Кто ударил своего последним (renderId; −1 — никто/сброшено) — самотест.
	int32 GetSelfAttackerRid();
	// ★ 26.10.2026 (ревизия wave_1025d 3.1) [ОРИГИНАЛ]: сброс «кто меня ударил» (`[1].netWord54`) и прошлого состояния. У эталона это
	//   поле персонажа: персонаж пересоздаётся при смене карты и перелогине, поле обнуляется вместе с ним. Зовёт сброс сессии
	//   (`UAsdaSystemOptionSubsystem::ResetSessionState`) при ЛЮБОЙ причине, включая смену карты. Ключ `asda.UI.EscAttackerReset`
	//   (0 — как до 26.10: поле переживало смену карты и перелогин). `Why` — для журнала.
	void ResetSelfHitState(const TCHAR* Why);
	// Сколько раз ветки (в) остановили персонажа (4008 + покой) / потратили нажатие — самотест.
	int32 GetStateStopCount();
	int32 GetStateConsumeCount();
}
