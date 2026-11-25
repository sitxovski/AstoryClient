// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ОКНА ПРИ СВОЁМ ВХОДЕ/ВОСКРЕШЕНИИ (4055/4056) И ЗАГРУЗКЕ КАРТЫ — реализация. Разбор и эталон — `Public/UI/AsdaEntryWindows.h`.
//   Своих копий «закрыть окно» здесь НЕТ (закон `port-shared-points`): каждое окно закрывается ИГРОВОЙ функцией владельца,
//   той же, что у его крестика/ESC, — флаги «открыто» снимаются, а не только виджет.

#include "UI/AsdaEntryWindows.h"
#include "AsdaDevConsole.h"   // ASDA_DEV_CVAR — дев-ключ, в Shipping константа
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaBoosterShopSubsystem.h"      // 15300: `uiStorageWindow_Close` + `uiCloseItemComposeDialog`
#include "UI/AsdaPremiumShopSubsystem.h"      // 6000: `uiInventoryResetAndLeave`
#include "UI/AsdaHairShopSubsystem.h"         // 15000: `uiLeaveAndResetAllWindows`
#include "UI/AsdaPremiumPostSubsystem.h"      // 69000: `sub_92548B(g_pInventoryBagWindow)`
#include "UI/AsdaPackagePanelSubsystem.h"     // 26500: `uiCloseWindow(g_pPackageStampWindow)`
#include "UI/AsdaMinimapSubsystem.h"          // большая карта 1710: `miniMapWndClose`
#include "UI/AsdaHotbarSubsystem.h"           // значки уведомлений: `uiNoticeIconPanelReset`
#include "UI/AsdaPartySubsystem.h"            // автоотказ заявки: `CPartyInviteDlg_OnAccept`
#include "UI/AsdaSystemMenuSubsystem.h"       // `uiPanelClearAllTabFlags`
#include "UI/AsdaSoulmateSkillSubsystem.h"    // вопрос зова друга (1, 38): 5237 код 2
#include "Resources/AsdaSoundSubsystem.h"     // звук 5 `CGameUI_CloseWindow`
#include "World/AsdaPlayerController.h"       // `hudResetCursorItemSlot` → снятие цели
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaEntryWin, Log, All);

namespace
{
	ASDA_DEV_CVAR(int32, CVarEntryWindowsEtalon,
		TEXT("asda.UI.EntryWindowsEtalon"), 1,
		TEXT("1 (по умолчанию) = своё 4055/4056 закрывает только окна из списков эталона (15300, 6000, парикмахерская, модалки\n")
		TEXT("31/2370/2318 …), а окна, которые эталон держит при смене карты, возвращаются после травела.\n")
		TEXT("0 = как прежде: своё 4055/4056 сносит все окна (CloseAll), после травела окна не возвращаются."),
		ECVF_Default);

	const FName kOneButMsgBox(TEXT("ONEBUT_MSGBOX"));   // 1500
	const FName kTwoButMsgBox(TEXT("TWOBUT_MSGBOX"));   // 1520

	// `hudHideAndResetSoundFlags` → `hudResetCursorItemSlot(g_pHUD)`: спрятать панель цели и обнулить её кэш — у порта
	//   снятие цели (та же точка, что у двойного ЛКМ мини-статуса, `AsdaMiniStateSubsystem.cpp`).
	void ResetTarget(UGameInstance* GI)
	{
		UWorld* W = GI ? GI->GetWorld() : nullptr;
		if (AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr) { PC->ClearTarget(); }
	}

	// Шаг 11 загрузки / ветка 4056: 15300, 6000, парикмахерская (порядок эталона 4056: склад, магазин, сборка, салон).
	int32 CloseShops(UGameInstance* GI)
	{
		int32 N = 0;
		if (UAsdaBoosterShopSubsystem* Bs = GI->GetSubsystem<UAsdaBoosterShopSubsystem>()) { if (Bs->IsOpen()) { Bs->CloseWindow(); ++N; } }
		if (UAsdaPremiumShopSubsystem* Ps = GI->GetSubsystem<UAsdaPremiumShopSubsystem>()) { if (Ps->IsOpen()) { Ps->CloseWindow(); ++N; } }
		if (UAsdaHairShopSubsystem* Hs = GI->GetSubsystem<UAsdaHairShopSubsystem>())       { if (Hs->IsSessionOpen()) { Hs->CloseWindow(); ++N; } }
		return N;
	}

	// `setField_p17(g_pUIManager, 0)` + `uiPanelClearAllTabFlags(g_pSystemMainWnd)`.
	void ClearSceneFlags(UGameInstance* GI)
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>()) { WM->SetPlayerWindowsHiddenFlag(false); }
		if (UAsdaSystemMenuSubsystem* Menu = GI->GetSubsystem<UAsdaSystemMenuSubsystem>()) { Menu->SetAllButtonsEnabled(true); }
	}
}

bool AsdaEntryWindows::IsEtalonEnabled()
{
	return CVarEntryWindowsEtalon.GetValueOnGameThread() != 0;
}

void AsdaEntryWindows::ApplySelfEntry(UGameInstance* GI, int32 Opcode, bool bDeadArrival, bool bMapChange)
{
	if (!GI) { return; }
	UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
	int32 Shops = 0;   // закрыто окон ПО ФАКТУ (магазины, ящик, панель пакета, карта, вопрос 38)
	if (Opcode == 4056)
	{
		// ── `GameNet_OnLocalPlayerMapEntry`, своя ветка (game_main.c:12923-13034), в порядке эталона ──
		if (WM) { WM->CloseMsgBoxIdOnMapEntry(31, /*bTwoButOnly*/ true, TEXT("своё 4056 (uiComboListCloseTradePanel)")); }
		Shops = CloseShops(GI);
		ResetTarget(GI);   // `hudHideAndResetSoundFlags`
		if (!bDeadArrival)
		{
			if (WM)
			{
				WM->CloseDeathMsgBoxOnMapEntry(true);   // `uiCloseOnExchangeItemType`
				// `if (sub_92A242(ui)) { g_bItemUpgradeSent = 1; OnSendItemUpgrade(ui, …, 1503, 0); }` — «ОК» ONEBUT с id 2318.
				if (WM->IsMsgBoxDisplayedId(2318) && WM->IsWindowVisible(kOneButMsgBox)) { WM->AnswerMsgBox(/*bTwoBut*/ false, /*bYes*/ true); }
			}
			ClearSceneFlags(GI);
		}
	}
	else
	{
		// ── `netHandler_EnterWorld` (network.c:42652-42690): HUD-сбросы, флаги сцены; затем эталон ВСЕГДА грузит карту ──
		ResetTarget(GI);   // `hudHideAndResetSoundFlags`
		ClearSceneFlags(GI);
		Shops += ApplyMapLoadWindowCloses(GI, TEXT("своё 4055 (загрузка карты эталона)"), bMapChange);
	}
	UE_LOG(LogAsdaEntryWin, Log, TEXT("[окна-вход] своё %d%s: закрыто окон эталона %d, остальные окна НЕ тронуты (прежде — CloseAll)"),
		Opcode, bDeadArrival ? TEXT(" (прибыл мёртвым)") : TEXT(""), Shops);
}

int32 AsdaEntryWindows::ApplyMapLoadWindowCloses(UGameInstance* GI, const TCHAR* Why, bool bLevelReload)
{
	if (!GI || !IsEtalonEnabled()) { return 0; }
	int32 N = 0;
	UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
	// ── Шаг 0 (game_main.c:3540-3555), порядок эталона ──
	if (UAsdaPartySubsystem* Party = GI->GetSubsystem<UAsdaPartySubsystem>()) { Party->DeclineIncomingOnMapLoad(Why); }   // `CPartyInviteDlg_OnAccept`
	if (UAsdaHotbarSubsystem* Hb = GI->GetSubsystem<UAsdaHotbarSubsystem>()) { Hb->ClearNoticeIcons(); }                // `uiNoticeIconPanelReset`
	if (UAsdaPremiumPostSubsystem* Pp = GI->GetSubsystem<UAsdaPremiumPostSubsystem>())                                 // `sub_92548B(g_pInventoryBagWindow)`
	{
		if (Pp->IsWindowVisible()) { Pp->CloseWindow(); ++N; }
	}
	if (WM)
	{
		// `CGameUI_CloseWindow(5001)`, `(5002)`, `(0)` — следующая запись стека показывается СРАЗУ (как у эталона), чтобы
		//   вопрос 38, лежавший под окном вида 0, был виден строке ниже. При перегрузке уровня `StartTargetLoad` не повторяет.
		WM->MapLoadCloseMsgBoxes(/*bDeferDisplay*/ false);
		if (bLevelReload) { WM->MarkMapLoadMsgBoxesClosed(); }
	}
	if (UAsdaMinimapSubsystem* Mm = GI->GetSubsystem<UAsdaMinimapSubsystem>())                                          // `miniMapWndClose`
	{
		if (Mm->IsBigMapOpen()) { Mm->SetBigMapVisible(false); ++N; }
	}
	// `uiUpgradeResultSendConfirm@0xD7DA30` (ui_core.c, имя врёт): `if (displayNoticeId == 38 && TWOBUT виден)` →
	//   `netSkill_SendBuffApply5237(тип 2)` + `CGameUI_CloseWindow(this, contentKind)`. Щелчка кнопки (звук 4) нет — запись
	//   снимается молча, звук 5 закрытия — общей точкой звуков; отказ шлёт владелец зова (те же поля, что эхо эталона).
	if (WM && WM->IsMsgBoxDisplayedId(38) && WM->IsWindowVisible(kTwoButMsgBox))
	{
		WM->RemoveMsgBoxEntries(1, 38, Why, /*bRedisplayNow*/ true);
		if (UAsdaSoulmateSkillSubsystem* Ss = GI->GetSubsystem<UAsdaSoulmateSkillSubsystem>()) { Ss->AnswerMateRecall(false); }
		if (UAsdaSoundSubsystem* Snd = GI->GetSubsystem<UAsdaSoundSubsystem>()) { Snd->PlaySound2DById(5); }
		++N;
	}
	ResetTarget(GI);   // `hudHideAndResetSoundFlags`
	//   `uiWindowCloseAndFreeList(g_pQuestCraftWnd)` — окна квест-обмена в порту нет (`AsdaCraftWindowSubsystem.cpp:509`).
	// ── Шаг 11 (game_main.c:3711-3724) ──
	N += CloseShops(GI);
	if (UAsdaPackagePanelSubsystem* Pk = GI->GetSubsystem<UAsdaPackagePanelSubsystem>())                              // `uiCloseWindow(g_pPackageStampWindow)`
	{
		if (Pk->IsPanelOpen()) { Pk->ClosePanel(); ++N; }
	}
	UE_LOG(LogAsdaEntryWin, Log, TEXT("[окна-вход] загрузка карты эталона (%s%s): закрыто окон %d (69000/большая карта/38/15300/6000/парикмахерская/26500)"),
		Why, bLevelReload ? TEXT(", уровень перегружается") : TEXT(", та же карта"), N);
	return N;
}
