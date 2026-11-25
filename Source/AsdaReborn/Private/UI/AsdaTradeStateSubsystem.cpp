// Copyright (c) 2026 @sitxovski (Discord)
// Общая точка «состояние торговли»: признаки окна обмена 2008 и личной лавки 22000/23000. Назначение, эталонные
//   адреса и правило сброса — в `Public/UI/AsdaTradeStateSubsystem.h`. Волна 0 торговли (29.09.2026).
#include "UI/AsdaTradeStateSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "UI/AsdaExchangeP2PSubsystem.h"   // ★ 29.09.2026 (волна О1): выход из окна обмена при «закрыть всё при уходе»
#include "UI/AsdaNotice.h"                 // ★ 30.09.2026 (волна Л5): канал отказов «лавка открыта» (чат 0xFFFFD306, 10 с)
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaTradeState, Log, All);

namespace
{
	// ★ Волна Л5 торговли (30.09.2026): [ИСПРАВЛЕНИЕ] К-17 — см. `InventoryShowcaseRefusalLoc` в заголовке.
	ASDA_DEV_CVAR(int32, CVarStoreFixK17, TEXT("asda.Trade.StoreFixK17"), 1,
		TEXT("[ИСПРАВЛЕНИЕ] К-17: рюкзак при открытой витрине личной лавки 23000 — 1 = нотис 107022 «Ты в своей лавке» (как у прочих "
		     "читателей «лавка открыта»); 0 = как оригинал, 13106 «Ничего не выйдет, пока идет работа с бустером.» (ui_core.c:46616)."),
		ECVF_Default);
}

int32 UAsdaTradeStateSubsystem::InventoryShowcaseRefusalLoc()
{
	return CVarStoreFixK17.GetValueOnGameThread() != 0 ? 107022 : 13106;
}

const UAsdaTradeStateSubsystem* UAsdaTradeStateSubsystem::Get(const UObject* Ctx)
{
	if (!Ctx) { return nullptr; }
	const UGameInstance* GI = Cast<UGameInstance>(Ctx);   // контекст — сам GameInstance
	if (!GI)
	{
		if (const UGameInstanceSubsystem* S = Cast<UGameInstanceSubsystem>(Ctx)) { GI = S->GetGameInstance(); }   // подсистема GI
		else if (const UWorld* W = Cast<UWorld>(Ctx)) { GI = W->GetGameInstance(); }                              // мир
		else if (const UWorld* W2 = Ctx->GetWorld()) { GI = W2->GetGameInstance(); }                               // актор/виджет мира
	}
	return GI ? GI->GetSubsystem<UAsdaTradeStateSubsystem>() : nullptr;
}

bool UAsdaTradeStateSubsystem::IsExchangeWindowOpen(const UObject* Ctx)
{
	const UAsdaTradeStateSubsystem* S = Get(Ctx);
	return S && S->bExchangeWindowOpen;
}

bool UAsdaTradeStateSubsystem::IsSellWindowOpen(const UObject* Ctx)
{
	const UAsdaTradeStateSubsystem* S = Get(Ctx);
	return S && S->bSellWindowOpen;
}

bool UAsdaTradeStateSubsystem::IsShopDlgOpen(const UObject* Ctx)
{
	const UAsdaTradeStateSubsystem* S = Get(Ctx);
	return S && S->bShopDlgOpen;
}

bool UAsdaTradeStateSubsystem::IsSelfStoreOpen(const UObject* Ctx)
{
	// [ОРИГИНАЛ] social.c:3780 — `sub_934E86(g_pSellWindow) || sub_929DC4(g_bSomeFeatureEnabled)`.
	return IsSellWindowOpen(Ctx) || IsShopDlgOpen(Ctx);
}

bool UAsdaTradeStateSubsystem::ChatStoreGate(const UObject* Ctx, int32 LocId, const TCHAR* Who)
{
	// [ОРИГИНАЛ] `CChat_AddTokenizedLines(g_pChatWnd, UI_FormatLocalizedNotice(loc), −11514, 10000, 0)` — −11514 = 0xFFFFD306.
	const UAsdaTradeStateSubsystem* SC = Get(Ctx);
	UAsdaTradeStateSubsystem* S = const_cast<UAsdaTradeStateSubsystem*>(SC);
	const UGameInstance* GI = S ? S->GetGameInstance() : nullptr;
	if (!GI) { return false; }
	const bool bOk = AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, AsdaNotice::LocRaw(GI, LocId), AsdaNoticeColor::Gold, 10000.f);
	++S->StoreGateSeq;
	S->LastStoreGateLoc = LocId;
	S->LastStoreGateWho = Who ? Who : TEXT("-");
	UE_LOG(LogAsdaTradeState, Log, TEXT("[торговля] гейт лавки: %s → loc %d в чат (0xFFFFD306, 10 с) %s; %s"), *S->LastStoreGateWho, LocId,
		bOk ? TEXT("выведен") : TEXT("НЕ выведен"), *S->DebugString());
	return bOk;
}

void UAsdaTradeStateSubsystem::SetExchangeWindowOpen(bool bOpen, const TCHAR* Why)
{
	if (bExchangeWindowOpen == bOpen) { return; }
	bExchangeWindowOpen = bOpen;
	UE_LOG(LogAsdaTradeState, Log, TEXT("[торговля] окно обмена 2008 (CExchange+0x2C) ← %d (%s)"), bOpen ? 1 : 0, Why ? Why : TEXT("-"));
}

void UAsdaTradeStateSubsystem::SetSellWindowOpen(bool bOpen, const TCHAR* Why)
{
	if (bSellWindowOpen == bOpen) { return; }
	bSellWindowOpen = bOpen;
	UE_LOG(LogAsdaTradeState, Log, TEXT("[торговля] подготовка лавки 22000 (CSellWindow+0x300) ← %d (%s)"), bOpen ? 1 : 0, Why ? Why : TEXT("-"));
}

void UAsdaTradeStateSubsystem::SetShopDlgOpen(bool bOpen, const TCHAR* Why)
{
	if (bShopDlgOpen == bOpen) { return; }
	bShopDlgOpen = bOpen;
	UE_LOG(LogAsdaTradeState, Log, TEXT("[торговля] витрина лавки 23000 (CShopDlg+0x168) ← %d (%s)"), bOpen ? 1 : 0, Why ? Why : TEXT("-"));
}

bool UAsdaTradeStateSubsystem::OnCloseAllWindowsOnLeave(const TCHAR* Why)
{
	if (!bExchangeWindowOpen) { return false; }
	// [ОРИГИНАЛ] ui_core.c:54726-54730: `CExchange_SendWindowCancel` (5284/0 — дефект Д1) + `uiInventoryDialogReset`.
	// ★ 29.09.2026 (волна О1, Т0-4): выход из окна делает модель обмена — 5286(windowIndex) вместо 5284/0 (Д1 исправлен,
	//   ключ `asda.Trade.P2PFixDefects`), затем `uiInventoryDialogReset` (признак → 0, вывод отложенного золота 9913).
	if (UAsdaExchangeP2PSubsystem* P2P = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaExchangeP2PSubsystem>() : nullptr)
	{
		return P2P->OnCloseAllWindowsOnLeave(Why ? Why : TEXT("-"));
	}
	UE_LOG(LogAsdaTradeState, Warning, TEXT("[торговля] закрыть всё при уходе (%s): подсистемы обмена нет — снят только признак"), Why ? Why : TEXT("-"));
	SetExchangeWindowOpen(false, TEXT("uiCloseAllWindowsOnLeave → uiInventoryDialogReset"));
	return true;
}

FString UAsdaTradeStateSubsystem::DebugString() const
{
	return FString::Printf(TEXT("обмен 2008=%d подготовка 22000=%d витрина 23000=%d (лавка=%d)"),
		bExchangeWindowOpen ? 1 : 0, bSellWindowOpen ? 1 : 0, bShopDlgOpen ? 1 : 0, (bSellWindowOpen || bShopDlgOpen) ? 1 : 0);
}

void UAsdaTradeStateSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	if (bExchangeWindowOpen || bSellWindowOpen || bShopDlgOpen)
	{
		UE_LOG(LogAsdaTradeState, Log, TEXT("[торговля] сброс сессии (%s): %s → все 0"), AsdaSessionResetReasonName(Reason), *DebugString());
	}
	bExchangeWindowOpen = false;
	bSellWindowOpen = false;
	bShopDlgOpen = false;
}

#if !UE_BUILD_SHIPPING
namespace
{
	// `asda.Trade.State` — печать признаков торговли (только чтение).
	FAutoConsoleCommandWithWorldAndArgs GAsdaTradeStateDump(
		TEXT("asda.Trade.State"),
		TEXT("asda.Trade.State — признаки торговли: окно обмена 2008 (CExchange+0x2C), подготовка лавки 22000, витрина 23000."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
		{
			const UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
			const UAsdaTradeStateSubsystem* S = GI ? GI->GetSubsystem<UAsdaTradeStateSubsystem>() : nullptr;
			UE_LOG(LogAsdaTradeState, Log, TEXT("[торговля] %s"), S ? *S->DebugString() : TEXT("подсистемы нет"));
		}));
}
#endif
