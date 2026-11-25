// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ СНИМОК ОТКРЫТЫХ ОКОН И ИХ ВОССТАНОВЛЕНИЕ — общая точка `UAsdaWindowSubsystem` (волна В3 ремесла, 25.09.2026).
//   Порт `uiCaptureOpenWindowsState@0xD96FD0` (ui_core.c:60910-60954), `uiCloseAllToggleWindows@0xD97280`
//   (:60958-60968) и `uiDispatchWindowToggle@0xD97300` (:60972-61024). Объявления и смысл — в
//   `Public/UI/AsdaWindowSubsystem.h` (раздел «СНИМОК ОТКРЫТЫХ ОКОН»), полный разбор 15 слотов —
//   `Reports/craft/wave3.md` §3.
//   Своих копий «открыть окно» здесь НЕТ (закон `port-shared-points`): каждый слот зовёт ИГРОВУЮ функцию
//   открытия владельца окна — ту же, что зовут кнопка меню/горячая клавиша (ревизия `craft_re_REVIEW.md`
//   МЕЛК-3: восстановление обязано идти игровыми «открыть» со звуком id 5 `S_e_button06`, а не
//   `SetWindowVisible`). Звук, которого нет в функции открытия порта, но есть в эталонной, играется здесь
//   общей точкой звуков — у слота это записано явно.

#include "UI/AsdaWindowSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaInventorySubsystem.h"       // слот 0: `uiOpenInventoryWindow`
#include "UI/AsdaCharaInfoSubsystem.h"       // слот 1: `uiWindow_OpenIfAllowed`
#include "UI/AsdaNpcShopSubsystem.h"         // слот 2: `g_pRecvUiHandler2` — магазин NPC (только снимок)
#include "Quest/AsdaQuestLogSubsystem.h"     // слот 4: `uiQuestGuide_Open`
#include "UI/AsdaCraftWindowSubsystem.h"     // слот 5: `tileInteractGateAndOpen` (в снимок не пишется — ветка недостижима)
#include "UI/AsdaDisassemblySubsystem.h"     // слот 6: `upgradeUiOnOpen` — окно разборки 32000 (волна 2-А, 06.10.2026)
#include "UI/AsdaPetCageSubsystem.h"         // слот 9: `CRefineWnd_Open` — окно питомца 43000
#include "UI/AsdaItemUpgradeSubsystem.h"     // слот 12: `uiCombineWindow_Close` — окно улучшения 20000
#include "UI/AsdaSkillWindowSubsystem.h"     // слот 13: `skillwin_OpenSkillWindow`
#include "UI/AsdaCommunitySubsystem.h"       // слот 14: `communityUI_Open`
#include "UI/AsdaSoulmateSubsystem.h"        // слот 11: `sub_92CBC8` — поп-карточка соулмейта 6500 (волна В4)
#include "Quest/AsdaQuestMarkerSubsystem.h"  // слот 7: `sub_91ECD5` — трекер заданий 1800 (волна В4)
#include "UI/AsdaTitleSubsystem.h"         // слот 8: `sub_923F91` / `sub_934373` — значок званий HUD 50000 (волна В5)
#include "Resources/AsdaSoundSubsystem.h"    // звук id 5 у слотов, чья функция открытия в порту молчит

#include "UI/AsdaEntryWindows.h"          // ★ 22.10.2026: ключ `asda.UI.EntryWindowsEtalon` (окна при смене карты)
#include "World/AsdaWorldKind.h"           // экран загрузки / логин — не «игровой мир»
#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/PackageName.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaWinSnap, Log, All);

// ★ Ключ общей точки (не дефект оригинала — это перенос поведения оригинала, поэтому ключ `asda.UI.*`).
//   1 = как эталон: снимок при входе в диалог NPC / открытии окна состава 77000, восстановление при уходе от NPC
//   (и при сбросе окна 77500 — волна В4). 0 = поведение порта до 25.09.2026: окна, закрытые разговором, не
//   возвращаются.
// ★ Волна В4 ремесла (ревизия В3 МЕЛК-2): ДВА ЗАЩИТНЫХ ОТСТУПЛЕНИЯ ПОРТА от эталона, у которого флаги снимка
//   обнуляют только конструктор (`gameObj_ctor`, AsdaStory_game.c:391696) и само восстановление:
//   (1) сброс сессии (разрыв, выход, травел) снимает флаги БЕЗ открытия окон — иначе окна прошлой сессии «вернулись» бы
//       при первом уходе от NPC в новой;
//   (2) если разговор с NPC (или окно изготовления 77500) закрывается ВНУТРИ чужого «закрыть всё»
//       (`CloseAllPlayerWindows`), окна не переоткрываются: закрыватели, отработавшие раньше по списку, уже закрыли
//       своё, и переоткрытие посреди цикла дало бы окна, открытые под полноэкранной сценой.
//   1 = защиты включены (поведение порта с 25.09); 0 = как оригинал дословно.
static ASDA_DEV_CVAR(int32, CVarAsdaWindowSnapshotGuards,
	TEXT("asda.UI.WindowSnapshotGuards"), 1,
	TEXT("Снимок открытых окон, защиты порта: 1 = сброс сессии снимает флаги без открытия окон, а уход от NPC / сброс окна "
	     "77500 внутри чужого «закрыть всё» окна не переоткрывает; 0 = как оригинал — флаги живут до восстановления, "
	     "восстановление идёт всегда."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarAsdaWindowSnapshot,
	TEXT("asda.UI.WindowSnapshot"), 1,
	TEXT("Снимок открытых окон (uiCaptureOpenWindowsState / uiCloseAllToggleWindows): 1 = как оригинал — окна, "
	     "открытые до разговора с NPC (и до окна состава ремесла), переоткрываются их игровыми функциями открытия; "
	     "0 = не снимать и не восстанавливать (поведение порта до 25.09.2026)."),
	ECVF_Default);

namespace
{
	// Описание слота снимка: имя окна эталона и признак «окно есть в порту» (для дампа и лога).
	struct FSnapSlotInfo
	{
		const TCHAR* Etalon;    // что это в эталоне (предикат снимка → функция восстановления)
		bool bPorted;           // есть ли в порту и предикат, и функция открытия
	};
	// Порядок и номера — эталонные (`uiDispatchWindowToggle` case 0…14).
	const FSnapSlotInfo kSlots[UAsdaWindowSubsystem::kWindowSnapshotSlots] =
	{
		/* 0 */ { TEXT("инвентарь 400 (sub_93248D → uiOpenInventoryWindow, звук id 5)"), true },
		/* 1 */ { TEXT("окно персонажа 900 (sub_921944 → uiWindow_OpenIfAllowed, звук S_e_button06)"), true },
		/* 2 */ { TEXT("магазин NPC g_pRecvUiHandler2 (sub_92DCF8; в восстановлении ветки НЕТ — как эталон)"), true },
		/* 3 */ { TEXT("g_pRecvUiHandler (sub_932DF2 → vtbl+4; полиморфный получатель — окна в порту нет)"), false },
		/* 4 */ { TEXT("квест-гид 700 (g_bQuestGuideOpen → uiQuestGuide_Open, звук id 5)"), true },
		/* 5 */ { TEXT("ремесло 76000 (в снимок НЕ пишется — байты 0xD96FD0; tileInteractGateAndOpen)"), true },
		/* 6 */ { TEXT("разборка 32000 (sub_933CA7 → upgradeUiOnOpen, тумблер; порт волны 2-А)"), true },
		/* 7 */ { TEXT("трекер заданий 1800 (CQuest_Episode_GetState → sub_91ECD5 = sub_C584C0 «показать окно»)"), true },
		/* 8 */ { TEXT("значок званий HUD 50000 (sub_923F91 = sub_D97230 «контрол виден» → sub_934373 = set_o370_7470 «[880]=1»; поле +880 = UAsdaTitleSubsystem::bNoticeActive)"), true },
		/* 9 */ { TEXT("окно питомца 43000 (sub_92D4A1 = sub_B797C0 → CRefineWnd_Open, тумблер)"), true },
		/* 10 */ { TEXT("плашка питомца 43200 (sub_93DBDA = sub_D97200 → refineWindow_Open)"), true },
		/* 11 */ { TEXT("поп-карточка соулмейта 6500 (sub_9344D6 = sub_B7A590 → sub_92CBC8 = sub_E65450)"), true },
		/* 12 */ { TEXT("окно улучшения снаряжения 20000 (sub_939058 → uiCombineWindow_Close, тумблер)"), true },
		/* 13 */ { TEXT("книга навыков 12000 (sub_92546D → skillwin_OpenSkillWindow, звук id 5)"), true },
		/* 14 */ { TEXT("сообщество 6400 (sub_936574 → communityUI_Open, звук id 5)"), true },
	};

	// Звук id 5 (`S_e_button06`) эталонной функции открытия — там, где функция порта его не играет.
	void PlayOpenSound(UGameInstance* GI)
	{
		if (UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr) { Snd->PlaySound2DById(5); }
	}
}

int32 UAsdaWindowSubsystem::CaptureOpenWindows(EWindowSnapshotOwner Owner, const TCHAR* Why)
{
	if (CVarAsdaWindowSnapshot.GetValueOnGameThread() == 0) { return 0; }
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return 0; }
	const uint8 Bit = (uint8)Owner;
	// Предикаты — строго эталонные, по порядку `uiCaptureOpenWindowsState` (ui_core.c:60914-60952).
	bool Open[kWindowSnapshotSlots] = {};
	if (const UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>()) { Open[0] = Inv->IsWindowOpen(); }
	if (const UAsdaCharaInfoSubsystem* Ci = GI->GetSubsystem<UAsdaCharaInfoSubsystem>())  { Open[1] = Ci->IsWindowOpen(); }
	if (const UAsdaNpcShopSubsystem* Sh = GI->GetSubsystem<UAsdaNpcShopSubsystem>())      { Open[2] = Sh->IsOpen(); }
	//   слот 3 `g_pRecvUiHandler` — окна в порту нет (ложно).
	if (const UAsdaQuestLogSubsystem* Ql = GI->GetSubsystem<UAsdaQuestLogSubsystem>())    { Open[4] = Ql->IsOpen(); }
	//   слот 5 — НЕ пишется эталоном (`gap_5A08 + 37888` = `g_pNpcWnd + 0xEE08` в байтах функции нет; слот 0 = +0xEDF4).
	// ★ Волна 2-А (06.10.2026): слот 6 — окно разборки 32000 (`sub_933CA7(g_pRecvUiHandler3)` = окно видно, ui_core.c:60924).
	if (const UAsdaDisassemblySubsystem* Ds = GI->GetSubsystem<UAsdaDisassemblySubsystem>()) { Open[6] = Ds->IsOpen(); }
	if (const UAsdaQuestMarkerSubsystem* Qm = GI->GetSubsystem<UAsdaQuestMarkerSubsystem>()) { Open[7] = Qm->IsTrackerWindowShown(); }
	// ★ Волна В5 (ревизия В4 С-2, поправка ложного «читателя нет»): поле CUITitle +880 в порту есть — `bNoticeActive`.
	if (const UAsdaTitleSubsystem* Ti = GI->GetSubsystem<UAsdaTitleSubsystem>()) { Open[8] = Ti->IsNoticeIconShown(); }   // `sub_923F91`
	if (const UAsdaPetCageSubsystem* Pc = GI->GetSubsystem<UAsdaPetCageSubsystem>())
	{
		Open[9]  = Pc->IsCageOpen();           // `sub_92D4A1` — окно 43000
		Open[10] = Pc->IsStatusPanelShown();   // `sub_93DBDA` — плашка 43200
	}
	if (const UAsdaSoulmateSubsystem* Sm = GI->GetSubsystem<UAsdaSoulmateSubsystem>())     { Open[11] = Sm->IsPopCardShown(); }
	if (const UAsdaItemUpgradeSubsystem* Up = GI->GetSubsystem<UAsdaItemUpgradeSubsystem>()) { Open[12] = Up->IsOpen(); }
	if (const UAsdaSkillWindowSubsystem* Sk = GI->GetSubsystem<UAsdaSkillWindowSubsystem>()) { Open[13] = Sk->IsWindowOpen(); }
	if (const UAsdaCommunitySubsystem* Cm = GI->GetSubsystem<UAsdaCommunitySubsystem>())  { Open[14] = Cm->IsWindowOpen(); }

	int32 Raised = 0;
	FString List;
	for (int32 S = 0; S < kWindowSnapshotSlots; ++S)
	{
		if (!Open[S]) { continue; }
		// Эталон только ставит `= 1` (флаги ИЛИ-ятся с прежними снимками — снимок не сбрасывается).
		WindowSnapshot[S] |= Bit;
		++Raised;
		List += FString::Printf(TEXT(" %d"), S);
	}
	UE_LOG(LogAsdaWinSnap, Log, TEXT("[окна-снимок] снимок (%s, владелец %d): поднято флагов %d —%s"),
		Why, (int32)Bit, Raised, Raised ? *List : TEXT(" нет открытых окон"));
	return Raised;
}

// ★ Отступление (ревизия В3 МЕЛК-3), записано: эталонные `uiOpenInventoryWindow` (гейт +2576), `uiWindow_OpenIfAllowed` и
//   `communityUI_Open` при уже открытом окне переинициализируют его и снова играют звук; порт уже открытое окно пропускает
//   (иначе тумблеры 20000/43000 закрыли бы его). Разница видна только если окно слота открыли заново до восстановления.
int32 UAsdaWindowSubsystem::ReopenCapturedWindows(const TCHAR* Why)
{
	UGameInstance* GI = GetGameInstance();
	if (CVarAsdaWindowSnapshot.GetValueOnGameThread() == 0 || !GI)
	{
		ClearWindowSnapshot(TEXT("asda.UI.WindowSnapshot 0"));
		return 0;
	}
	// ★ ИСПРАВЛЕНИЕ ПОРЯДКА (ревизия В3 МЕЛК-4): эталон обнуляет флаги ПОСЛЕ цикла (`memset(this + 15229, 0, 0x3C)`),
	//   и функция открытия может войти в восстановление повторно (`shopWin_Close` → 5045 → снова этот цикл) — тогда
	//   слоты открылись бы дважды. Порт копирует и обнуляет флаги ДО открытий: повторный вход видит пустой снимок.
	//   Итог для игрока тот же (каждое окно открыто один раз) — защита от повторного входа, а не смена поведения.
	uint8 Now[kWindowSnapshotSlots];
	FMemory::Memcpy(Now, WindowSnapshot, sizeof(Now));
	FMemory::Memzero(WindowSnapshot, sizeof(WindowSnapshot));   // `memset(this + 15229, 0, 0x3C)`
	// ★★★ РЕВИЗИЯ В3 К-1 (КРИТ): у эталона функции открытия окна персонажа (`uiWindow_OpenIfAllowed`, ui_core.c:74270)
	//   и квест-гида (`uiQuestGuide_Open`, :33571) признак «окна убраны сценой» `*(g_pUIManager+17)` НЕ проверяют, а в
	//   порту обе выходят по `ArePlayerWindowsHidden()`. Восстановление идёт ДО `RestoreAfterSceneExit` (порядок эталона:
	//   `uiCloseAllToggleWindows`, затем `uiExpandPartyPanel`, ui_core.c:61245), поэтому слоты 1 и 4 не возвращались
	//   никогда. На время цикла признак снимается и затем возвращается как был — порядок эталона не меняется, а гейт
	//   «сцена» у этих окон на пути восстановления не срабатывает (как у эталона).
	TGuardValue<bool> SceneHiddenGuard(bPlayerWindowsHidden, false);
	int32 Opened = 0;
	FString List;
	for (int32 S = 0; S < kWindowSnapshotSlots; ++S)
	{
		if (Now[S] == 0) { continue; }   // `if (*(this + i + 15229) > 0) uiDispatchWindowToggle(i)`
		bool bDone = false;
		switch (S)
		{
		case 0:
			// `uiOpenInventoryWindow(g_pInventoryWnd, 1)` играет id 5 сам (ui_core.c:46666); `OpenWindow` порта — молча.
			if (UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>())
			{
				if (!Inv->IsWindowOpen()) { Inv->OpenWindow(); if (Inv->IsWindowOpen()) { PlayOpenSound(GI); bDone = true; } }
			}
			break;
		case 1:
			// `uiWindow_OpenIfAllowed` — порт играет `S_e_button06` сам (AsdaCharaInfoSubsystem.cpp).
			if (UAsdaCharaInfoSubsystem* Ci = GI->GetSubsystem<UAsdaCharaInfoSubsystem>())
			{
				if (!Ci->IsWindowOpen()) { Ci->OpenWindow(); bDone = Ci->IsWindowOpen(); }
			}
			break;
		case 6:
			// `upgradeUiOnOpen(g_pRecvUiHandler3)` — ТУМБЛЕР (волна 2-А): у эталона окно к этому моменту закрыто
			//   `uiCloseAllWindows` (оно в его списке); открытое окно порт не трогает — тумблер закрыл бы его.
			if (UAsdaDisassemblySubsystem* Ds = GI->GetSubsystem<UAsdaDisassemblySubsystem>())
			{
				if (!Ds->IsOpen()) { Ds->Toggle(TEXT("восстановление снимка окон, слот 6")); bDone = Ds->IsOpen(); }
			}
			break;
		case 4:
			// `uiQuestGuide_Open` — порт играет id 5 сам.
			if (UAsdaQuestLogSubsystem* Ql = GI->GetSubsystem<UAsdaQuestLogSubsystem>())
			{
				if (!Ql->IsOpen()) { Ql->Open(); bDone = Ql->IsOpen(); }
			}
			break;
		case 5:
			// `tileInteractGateAndOpen(g_pItemMainWnd)` — флаг слота 5 никто не поднимает (как эталон); ветка ради полноты.
			if (UAsdaCraftWindowSubsystem* Cw = GI->GetSubsystem<UAsdaCraftWindowSubsystem>())
			{
				if (!Cw->IsOpen()) { Cw->GateAndToggle(TEXT("восстановление снимка, слот 5")); bDone = Cw->IsOpen(); }
			}
			break;
		case 9:
			// `CRefineWnd_Open` — ТУМБЛЕР: у эталона окно к этому моменту закрыто `uiCloseAllWindows`. У порта
			//   закрывателя 43000 в CloseAll нет, окно могло остаться открытым — тогда тумблер закрыл бы его.
			if (UAsdaPetCageSubsystem* Pc = GI->GetSubsystem<UAsdaPetCageSubsystem>())
			{
				if (!Pc->IsCageOpen()) { Pc->OpenCage(); bDone = Pc->IsCageOpen(); }
			}
			break;
		case 7:
			// `sub_91ECD5` → `sub_C584C0@0xC584C0`: показать окно трекера 1800, если оно создано. Звука нет.
			if (UAsdaQuestMarkerSubsystem* Qm = GI->GetSubsystem<UAsdaQuestMarkerSubsystem>())
			{
				if (!Qm->IsTrackerWindowShown()) { Qm->TrackerShowWindow(); bDone = Qm->IsTrackerWindowShown(); }
			}
			break;
		case 8:
			// `sub_934373` → `set_o370_7470@0xD97470`: `[880] = 1` — значок званий снова мигает. Звука нет.
			if (UAsdaTitleSubsystem* Ti = GI->GetSubsystem<UAsdaTitleSubsystem>())
			{
				if (!Ti->IsNoticeIconShown()) { Ti->ShowNoticeIconFromSnapshot(); bDone = Ti->IsNoticeIconShown(); }
			}
			break;
		case 10:
			// `refineWindow_Open@0xDC6FE0`: показать плашку 43200, если есть призванный (иначе прячет). Звука нет.
			if (UAsdaPetCageSubsystem* Pc = GI->GetSubsystem<UAsdaPetCageSubsystem>())
			{
				if (!Pc->IsStatusPanelShown()) { Pc->ReopenStatusPanelFromSnapshot(); bDone = Pc->IsStatusPanelShown(); }
			}
			break;
		case 11:
			// `sub_92CBC8` = `sub_E65450@0xE65450`: показать поп-карточку 6500 при галке 6546. Звука нет.
			if (UAsdaSoulmateSubsystem* Sm = GI->GetSubsystem<UAsdaSoulmateSubsystem>())
			{
				if (!Sm->IsPopCardShown()) { Sm->ShowPopCardFromSnapshot(); bDone = Sm->IsPopCardShown(); }
			}
			break;
		case 12:
			// `uiCombineWindow_Close` — ТУМБЛЕР окна 20000 (имя IDA врёт); открываем только закрытое. Звука нет и у эталона.
			if (UAsdaItemUpgradeSubsystem* Up = GI->GetSubsystem<UAsdaItemUpgradeSubsystem>())
			{
				if (!Up->IsOpen()) { Up->OpenWindow(); bDone = Up->IsOpen(); }
			}
			break;
		case 13:
			// `skillwin_OpenSkillWindow` — порт играет id 5 сам.
			if (UAsdaSkillWindowSubsystem* Sk = GI->GetSubsystem<UAsdaSkillWindowSubsystem>())
			{
				if (!Sk->IsWindowOpen()) { Sk->OpenWindow(); bDone = Sk->IsWindowOpen(); }
			}
			break;
		case 14:
			// `communityUI_Open` играет id 5 сам (social.c:3797); `OpenWindow` порта — молча.
			if (UAsdaCommunitySubsystem* Cm = GI->GetSubsystem<UAsdaCommunitySubsystem>())
			{
				if (!Cm->IsWindowOpen()) { Cm->OpenWindow(); if (Cm->IsWindowOpen()) { PlayOpenSound(GI); bDone = true; } }
			}
			break;
		default:
			// Слот 2 — ветки у эталона нет (`default: return`); слоты 3/6 — окон в порту нет.
			break;
		}
		if (bDone) { ++Opened; }
		List += FString::Printf(TEXT(" %d%s"), S, bDone ? TEXT("") : TEXT("(—)"));
	}
	UE_LOG(LogAsdaWinSnap, Log, TEXT("[окна-снимок] восстановление (%s): открыто окон %d; слоты:%s"),
		Why, Opened, List.IsEmpty() ? TEXT(" пусто") : *List);
	return Opened;
}

bool UAsdaWindowSubsystem::AreSnapshotGuardsEnabled()
{
	return CVarAsdaWindowSnapshotGuards.GetValueOnGameThread() != 0;
}

bool UAsdaWindowSubsystem::IsWindowSnapshotEnabled()
{
	return CVarAsdaWindowSnapshot.GetValueOnGameThread() != 0;
}

void UAsdaWindowSubsystem::DropCapturedWindows(EWindowSnapshotOwner Owner, const TCHAR* Why)
{
	const uint8 Bit = (uint8)Owner;
	int32 Dropped = 0;
	for (int32 S = 0; S < kWindowSnapshotSlots; ++S)
	{
		if ((WindowSnapshot[S] & Bit) == 0) { continue; }
		WindowSnapshot[S] &= (uint8)~Bit;
		++Dropped;
	}
	if (Dropped > 0)
	{
		UE_LOG(LogAsdaWinSnap, Log, TEXT("[окна-снимок] сняты флаги владельца %d (%s): %d"), (int32)Bit, Why, Dropped);
	}
}

void UAsdaWindowSubsystem::ClearWindowSnapshot(const TCHAR* Why)
{
	int32 Was = 0;
	for (int32 S = 0; S < kWindowSnapshotSlots; ++S) { if (WindowSnapshot[S]) { ++Was; } }
	FMemory::Memzero(WindowSnapshot, sizeof(WindowSnapshot));
	if (Was > 0) { UE_LOG(LogAsdaWinSnap, Log, TEXT("[окна-снимок] снимок очищен без восстановления (%s): флагов было %d"), Why, Was); }
}

FString UAsdaWindowSubsystem::DumpWindowSnapshot() const
{
	FString S = TEXT("=== снимок открытых окон (uiCaptureOpenWindowsState, 15 слотов) ===\n");
	for (int32 I = 0; I < kWindowSnapshotSlots; ++I)
	{
		S += FString::Printf(TEXT("  слот %2d: флаг=%d владельцы=%d · %s%s\n"), I, WindowSnapshot[I] ? 1 : 0, (int32)WindowSnapshot[I],
			kSlots[I].Etalon, kSlots[I].bPorted ? TEXT("") : TEXT(" [в порту не снимается]"));
	}
	return S;
}


// ══════ ★★★ 22.10.2026: ОКНА, КОТОРЫЕ ЭТАЛОН ДЕРЖИТ ПРИ СМЕНЕ КАРТЫ — разбор у объявления (AsdaWindowSubsystem.h) ══════
namespace
{
	// Слоты снимка, которые загрузка карты эталона НЕ закрывает и которые игрок открывает сам. Прочие слоты: 2 — магазин NPC
	//   (уходит с разговором), 3 — окна в порту нет, 5 — ремесло (в снимок не пишется), 7/8/10/11 — плашки HUD (трекер,
	//   значок званий, плашка питомца, поп-карточка соулмейта), их владельцы показывают сами по своему состоянию.
	constexpr int32 kTravelKeptSlots[] = { 0, 1, 4, 6, 9, 12, 13, 14 };

	// Окно слота «живо» во вьюпорте СВОЕГО мира (у слота 9 своего id в реестре нет — судим по флагу владельца).
	FName TravelSlotWindowId(int32 Slot)
	{
		switch (Slot)
		{
		case 0:  return FName(TEXT("INVEN_WIN"));
		case 1:  return FName(TEXT("CHARAINFO_WIN"));
		case 4:  return FName(TEXT("QUEST_WIN"));
		case 6:  return FName(TEXT("Disassembly_Win"));
		case 12: return FName(TEXT("Item_Upgrade_Win"));
		case 13: return FName(TEXT("SKILL_WIN"));
		case 14: return FName(TEXT("COMMUNITY_WIN"));
		default: return NAME_None;
		}
	}

	// Флаг «открыто» владельца слота (те же предикаты, что у `CaptureOpenWindows`).
	bool TravelSlotOwnerOpen(UGameInstance* GI, int32 Slot)
	{
		switch (Slot)
		{
		case 0:  { const UAsdaInventorySubsystem* S = GI->GetSubsystem<UAsdaInventorySubsystem>();     return S && S->IsWindowOpen(); }
		case 1:  { const UAsdaCharaInfoSubsystem* S = GI->GetSubsystem<UAsdaCharaInfoSubsystem>();     return S && S->IsWindowOpen(); }
		case 4:  { const UAsdaQuestLogSubsystem* S = GI->GetSubsystem<UAsdaQuestLogSubsystem>();       return S && S->IsOpen(); }
		case 6:  { const UAsdaDisassemblySubsystem* S = GI->GetSubsystem<UAsdaDisassemblySubsystem>(); return S && S->IsOpen(); }
		case 9:  { const UAsdaPetCageSubsystem* S = GI->GetSubsystem<UAsdaPetCageSubsystem>();         return S && S->IsCageOpen(); }
		case 12: { const UAsdaItemUpgradeSubsystem* S = GI->GetSubsystem<UAsdaItemUpgradeSubsystem>(); return S && S->IsOpen(); }
		case 13: { const UAsdaSkillWindowSubsystem* S = GI->GetSubsystem<UAsdaSkillWindowSubsystem>(); return S && S->IsWindowOpen(); }
		case 14: { const UAsdaCommunitySubsystem* S = GI->GetSubsystem<UAsdaCommunitySubsystem>();     return S && S->IsWindowOpen(); }
		default: return false;
		}
	}

	// Закрыть окно слота ИГРОВЫМ закрытием владельца (снимает его флаг) / открыть ИГРОВЫМ открытием.
	void TravelSlotOwnerClose(UGameInstance* GI, int32 Slot)
	{
		switch (Slot)
		{
		case 0:  if (UAsdaInventorySubsystem* S = GI->GetSubsystem<UAsdaInventorySubsystem>())     { S->CloseWindow(); } break;
		case 1:  if (UAsdaCharaInfoSubsystem* S = GI->GetSubsystem<UAsdaCharaInfoSubsystem>())     { S->CloseWindow(); } break;
		case 4:  if (UAsdaQuestLogSubsystem* S = GI->GetSubsystem<UAsdaQuestLogSubsystem>())       { S->Close(); } break;
		case 6:  if (UAsdaDisassemblySubsystem* S = GI->GetSubsystem<UAsdaDisassemblySubsystem>()) { S->CloseWindow(); } break;
		case 9:  if (UAsdaPetCageSubsystem* S = GI->GetSubsystem<UAsdaPetCageSubsystem>())         { S->CloseCage(); } break;
		case 12: if (UAsdaItemUpgradeSubsystem* S = GI->GetSubsystem<UAsdaItemUpgradeSubsystem>()) { if (S->IsOpen()) { S->ToggleWindow(); } } break;   // тумблер: открытое закрывает
		case 13: if (UAsdaSkillWindowSubsystem* S = GI->GetSubsystem<UAsdaSkillWindowSubsystem>()) { S->CloseWindow(); } break;
		case 14: if (UAsdaCommunitySubsystem* S = GI->GetSubsystem<UAsdaCommunitySubsystem>())     { S->CloseWindow(); } break;
		default: break;
		}
	}
	void TravelSlotOwnerOpenWindow(UGameInstance* GI, int32 Slot)
	{
		switch (Slot)
		{
		case 0:  if (UAsdaInventorySubsystem* S = GI->GetSubsystem<UAsdaInventorySubsystem>())     { S->OpenWindow(); } break;
		case 1:  if (UAsdaCharaInfoSubsystem* S = GI->GetSubsystem<UAsdaCharaInfoSubsystem>())     { S->OpenWindow(); } break;
		case 4:  if (UAsdaQuestLogSubsystem* S = GI->GetSubsystem<UAsdaQuestLogSubsystem>())       { S->Open(); } break;
		case 6:  if (UAsdaDisassemblySubsystem* S = GI->GetSubsystem<UAsdaDisassemblySubsystem>()) { if (!S->IsOpen()) { S->Toggle(TEXT("окно пережило смену карты")); } } break;   // тумблер
		case 9:  if (UAsdaPetCageSubsystem* S = GI->GetSubsystem<UAsdaPetCageSubsystem>())         { if (!S->IsCageOpen()) { S->OpenCage(); } } break;   // тумблер
		case 12: if (UAsdaItemUpgradeSubsystem* S = GI->GetSubsystem<UAsdaItemUpgradeSubsystem>()) { if (!S->IsOpen()) { S->OpenWindow(); } } break;
		case 13: if (UAsdaSkillWindowSubsystem* S = GI->GetSubsystem<UAsdaSkillWindowSubsystem>()) { S->OpenWindow(); } break;
		case 14: if (UAsdaCommunitySubsystem* S = GI->GetSubsystem<UAsdaCommunitySubsystem>())     { S->OpenWindow(); } break;
		default: break;
		}
	}
}

bool UAsdaWindowSubsystem::HasTravelKeptWindows() const
{
	for (const bool B : TravelKept) { if (B) { return true; } }
	return false;
}

void UAsdaWindowSubsystem::OnPreLoadMapForTravelKeep(const FString& MapName)
{
	if (!AsdaEntryWindows::IsEtalonEnabled()) { return; }
	UGameInstance* GI = GetGameInstance();
	const UWorld* W = GI ? GI->GetWorld() : nullptr;
	// Уходим ИЗ ИГРОВОГО мира: экран загрузки (промежуточный уровень того же перехода) и логин не считаются.
	if (!W || AsdaWorldKind::IsLoadingMap(W) || AsdaWorldKind::IsLoginScene(W)) { return; }
	// Травел В ЛОГИН (выход в выбор персонажа, обрыв связи) — окна новой сессии не наследуют прошлые: не запоминать.
	if (FPackageName::GetShortName(MapName).Equals(TEXT("Login"), ESearchCase::IgnoreCase))
	{
		FMemory::Memzero(TravelKept, sizeof(TravelKept));
		return;
	}
	CaptureTravelKeptWindows(*FString::Printf(TEXT("травел на «%s»"), *MapName));
}

void UAsdaWindowSubsystem::CaptureTravelKeptWindows(const TCHAR* Why)
{
	UGameInstance* GI = GetGameInstance();
	FMemory::Memzero(TravelKept, sizeof(TravelKept));
	if (!GI) { return; }
	FString List;
	for (const int32 S : kTravelKeptSlots)
	{
		const FName Id = TravelSlotWindowId(S);
		UUserWidget* Wd = Id.IsNone() ? nullptr : FindWindow(Id);
		const bool bLive = Id.IsNone() || (Wd && IsWindowLive(Wd) && IsWindowVisible(Id));
		TravelKept[S] = TravelSlotOwnerOpen(GI, S) && bLive;
		if (TravelKept[S]) { List += FString::Printf(TEXT(" %d"), S); }
	}
	UE_LOG(LogAsdaWinSnap, Log, TEXT("[окна-травел] перед травелом (%s) запомнены окна, которые эталон держит при смене карты, слоты:%s"),
		Why, List.IsEmpty() ? TEXT(" нет") : *List);
}

int32 UAsdaWindowSubsystem::ReopenTravelKeptWindows(const TCHAR* Why)
{
	UGameInstance* GI = GetGameInstance();
	if (!GI || !HasTravelKeptWindows()) { return 0; }
	bool Now[kWindowSnapshotSlots];
	FMemory::Memcpy(Now, TravelKept, sizeof(Now));
	FMemory::Memzero(TravelKept, sizeof(TravelKept));   // до открытий: повторный вход видит пустой список
	if (!AsdaEntryWindows::IsEtalonEnabled()) { return 0; }
	// У оригинала окна при смене карты не закрывались — ни звука закрытия, ни звука открытия, ни запросов первого открытия.
	UAsdaSoundSubsystem::FUiSoundMute Mute;
	TGuardValue<bool> ReopenGuard(bReopeningTravelKept, true);
	int32 Opened = 0;
	FString List;
	for (const int32 S : kTravelKeptSlots)
	{
		if (!Now[S]) { continue; }
		const FName Id = TravelSlotWindowId(S);
		auto IsLive = [this, Id]() { UUserWidget* Wd = Id.IsNone() ? nullptr : FindWindow(Id); return Wd && IsWindowLive(Wd) && IsWindowVisible(Id); };
		const bool bOwner = TravelSlotOwnerOpen(GI, S);
		if (bOwner && (Id.IsNone() || IsLive()))
		{
			List += FString::Printf(TEXT(" %d(жило)"), S);   // владелец сам пересобрал окно (самовосстановление в Tick)
			++Opened;
			continue;
		}
		if (bOwner) { TravelSlotOwnerClose(GI, S); }      // «открыто» при мёртвом виджете — снять флаг игровым закрытием
		TravelSlotOwnerOpenWindow(GI, S);
		const bool bDone = TravelSlotOwnerOpen(GI, S) && (Id.IsNone() || IsLive());
		if (bDone) { ++Opened; }
		List += FString::Printf(TEXT(" %d%s"), S, bDone ? TEXT("") : TEXT("(не открылось)"));
	}
	UE_LOG(LogAsdaWinSnap, Log, TEXT("[окна-травел] после входа на карту (%s): окон на месте %d; слоты:%s [ИСПРАВЛЕНИЕ ПОРТА, у оригинала окна не закрываются]"),
		Why, Opened, *List);
	return Opened;
}
