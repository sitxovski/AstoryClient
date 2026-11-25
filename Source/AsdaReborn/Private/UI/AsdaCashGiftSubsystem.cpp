// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaCashGiftSubsystem — P101_CashGift: подарок из кэш-магазина (шапка и ссылки — в `.h`).
//   Каждая ветка — с эталонным адресом и строкой (`RE/etalon/*.c`). Метки: [ОРИГИНАЛ] / [ИСПРАВЛЕНИЕ] / [ПОРТ].
#include "UI/AsdaCashGiftSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока

#include "AsdaDevConsole.h"                 // ASDA_DEV_CVAR — дев-ключи (в Shipping — константы умолчания)
#include "AsdaNetSubsystem.h"               // отправители 5072/5074, приём `OnCashGiftPacket`, баланс 5467, вес
#include "AsdaBytes.h"                      // чтение/запись i16/i32 little-endian
#include "AsdaWeightWire.h"                 // ★ P97: вес персонажа u16 (`AsdaWeightFromWire`)
#include "Items/AsdaItemTableSubsystem.h"   // записи item_list / special_itemlist (цена, имя)
#include "Items/AsdaItemDbRows.h"           // FAsdaSpecialItemlistRow / подсказка спецпредмета (имя)
#include "Login/AsdaLoginData.h"            // cp1251 имён провода
#include "Text/AsdaTextSubsystem.h"         // строки с [ss1]/[ss2]
#include "UI/AsdaNotice.h"                  // фасад нотисов: окно 1500 и цветная строка чата
#include "UI/AsdaNoticeColors.h"            // −11514 = Gold
#include "UI/AsdaHotbarSubsystem.h"         // значок уведомления 3 `mes_present`
#include "UI/AsdaInventorySubsystem.h"      // запись в сумку по ячейке (+5) — общая точка `ApplyExternalRecord`
#include "UI/AsdaPurchaseBoxSubsystem.h"    // модалка 1559 (`uiPopupClose(g_pItemTooltipUI)`)
#include "UI/AsdaTradeStateSubsystem.h"     // окно обмена 2008 открыто (`uiCanInteractWithTarget`)
#include "UI/AsdaEquipShopSubsystem.h"      // магазин NPC 6800 открыт (`uiCanInteractWithTarget`: this+10327)
#include "UI/AsdaSystemOptionSubsystem.h"   // персонаж занят (`objStateAllowsRender`)
#include "Resources/AsdaSoundSubsystem.h"   // звук 215
#include "World/AsdaWorldSubsystem.h"       // имя своего персонажа
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCashGift, Log, All);

namespace
{
	// ★ ОБЩИЙ ВЫКЛЮЧАТЕЛЬ: 02.11.2026 P101 выложен на сервер (v58, Enabled=1 Sites=15) — подарок включён по умолчанию (alpha-5.1.5).
	//   0 = как до P101: 5072/5074 не уходят, кнопки подарка в окнах 6000/15300 скрыты.
	static ASDA_DEV_CVAR(int32, CVarCashGiftEnable,
		TEXT("asda.CashGift.Enable"), 1,
		TEXT("P101_CashGift: 1 = подарок из кэш-магазина включён (кнопки «Подарок» 6085/15307, отправка 5072/5074); "
		     "0 = выключен, как до P101 (сервер без патча эти опкоды не принимает)."),
		ECVF_Default);
	// [ИСПРАВЛЕНИЕ] Д-К1: 15527 открывал 1559 в режиме покупки («Хочешь купить?» 12923 без «Кому»).
	static ASDA_DEV_CVAR(int32, CVarFixK1, TEXT("asda.CashGift.FixK1"), 1,
		TEXT("Д-К1: 1 = модалка 1559 из 15300 в режиме подарка показывает «Кому» 20196 + 12922; 0 = как оригинал (12923)."), ECVF_Default);
	// [ИСПРАВЛЕНИЕ] Д-К2: 5078 выбрасывался целиком, если получатель в этот момент сам отправлял подарок ([2072]).
	static ASDA_DEV_CVAR(int32, CVarFixK2, TEXT("asda.CashGift.FixK2"), 1,
		TEXT("Д-К2: 1 = подарок 5078 принимается всегда; 0 = как оригинал (при своей отправке пакет выбрасывается)."), ECVF_Default);
	// [ИСПРАВЛЕНИЕ] Д-К3: «себе» (12946) чистил только поле 6311, даже если имя вводили в 6345.
	static ASDA_DEV_CVAR(int32, CVarFixK3, TEXT("asda.CashGift.FixK3"), 1,
		TEXT("Д-К3: 1 = «себе» чистит поле той панели, где вводили имя; 0 = как оригинал (всегда 6311)."), ECVF_Default);
	// [ИСПРАВЛЕНИЕ] Д-К5: 15300 не проверял «себе», кэш и повтор, а 5074 из 15300 не ставил [2072]/[2079].
	static ASDA_DEV_CVAR(int32, CVarFixK5, TEXT("asda.CashGift.FixK5"), 1,
		TEXT("Д-К5: 1 = 15300 проверяет как 6000 (12919, «себе» 12946, в полёте, кэш 6778) и ставит флаги; 0 = как оригинал."), ECVF_Default);
	// ★ 08.11.2026 пачка 20 баг 9 — РЕШЕНИЕ ВЛАДЕЛЬЦА (не дефект оригинала: у эталона такой проверки нет): подарок персонажу, чьё
	//   имя начинается с «GM_» (без учёта регистра), не отправляется — окно 1500 «Admins are not accepting gifts :(» (loc 991211).
	//   Проверка в общей точке: 5072 (6000 — панели 6296/6333, 15300 — поле 15519) и 5074 (подтверждённое имя и поле 15519).
	static ASDA_DEV_CVAR(int32, CVarRefuseGm, TEXT("asda.CashGift.RefuseGm"), 1,
		TEXT("1 = подарок персонажу с именем «GM_…» не отправляется (5072/5074), окно «администраторы не принимают подарки» (loc 991211); "
		     "0 = как оригинал."), ECVF_Default);

	// Д-К6 из `re/client.md` §5 («`uiResetWindowState` не чистит имя +10734») — НЕ ДЕФЕКТ (ошибка реверса, перепроверено 01.11):
	//   `uiResetWindowState@0xC34570` зовёт `uiDialogCloseReset@0xC42600` (ui_core.c:27644), а тот — `uiPanelClearTextAndState@0xC46BA0`
	//   (ui_core.c:28876), который делает `memset(this + 10734, 0, 20)` и чистит поля 6311/6345. Ключа нет: исправлять нечего.

	// ── loc-id (все есть в RU `GameText.csv` и EN `DT_Loc_GameText_en.csv`; таблица `re/client.md` §4) ──
	constexpr int32 kLocCantUse     = 12919;   // «В текущих обстоятельствах…» — чат
	constexpr int32 kLocSelf        = 12946;   // «Нельзя дарить подарки себе»
	constexpr int32 kLocNoName      = 12939;   // «Не указано имя получателя подарка» — ONEBUT
	constexpr int32 kLocNoGxp       = 6778;    // «Не хватает GXP.» — чат
	constexpr int32 kLocGmRefuse    = 991211;  // ★ 08.11.2026 порт: «Администраторы не принимают подарки :(» — ONEBUT (решение владельца)
	constexpr int32 kLoc5073[6]     = { 6876, 20070, 20040, 6879, 6880, 12946 };   // 5073 код 0…5
	constexpr int32 kLoc5075[7]     = { 6891, 20071, 6892, 6893, 1216, 6895, 6896 }; // 5075 код 0…6
	constexpr int32 kLoc5077[3]     = { 6891, 20071, 6894 };                         // 5077 код 0…2
	constexpr int32 kLocGotItem     = 20072;   // «[ss1] шлет тебе подарок. Это [ss2].»
	constexpr int32 kLocGotGift     = 5060;    // «Получен подарок.»
	constexpr int32 kLocGiftIcon    = 6886;    // «Тебе прислали подарок.» — клик по значку 3
	constexpr int32 kNoticeType     = 3;       // значок `mes_present` (`MsgNotify_TypeToIcon`)
	constexpr int32 kSoundGift      = 215;     // `csoundPlayName2DStore(g_pSoundMgr, 215, 0)` network.c:27334
	// Цвета строк чата (эталонные ARGB): 20072 item_list −6881324 = 0xFF96FFD4, special −11514 = 0xFFFFD306; 5060 −8847556 = 0xFF78FF3C.
	const FColor kColItemList(0x96, 0xFF, 0xD4, 0xFF);
	const FColor kColGotGift(0x78, 0xFF, 0x3C, 0xFF);

	// Строка → char[20] cp1251 с NUL-добивкой (имена провода — только cp1251, закон arabic-multilang-plan).
	void NameToWire(const FString& S, uint8 Out[UAsdaCashGiftSubsystem::kNameBytes])
	{
		FMemory::Memzero(Out, UAsdaCashGiftSubsystem::kNameBytes);
		FAsdaLoginData::EncodeCp1251(S, Out, UAsdaCashGiftSubsystem::kNameBytes);
	}
	// ★ 08.11.2026 (решение владельца): имя получателя начинается с «GM_» без учёта регистра (пробелы по краям поля не в счёт).
	bool IsGmName(const FString& S)
	{
		return CVarRefuseGm.GetValueOnGameThread() != 0 && S.TrimStartAndEnd().StartsWith(TEXT("GM_"), ESearchCase::IgnoreCase);
	}
	// `strncmp(a, b, 20) == 0` по байтам провода.
	bool SameWireName(const FString& A, const FString& B)
	{
		uint8 WA[UAsdaCashGiftSubsystem::kNameBytes], WB[UAsdaCashGiftSubsystem::kNameBytes];
		NameToWire(A, WA); NameToWire(B, WB);
		for (int32 i = 0; i < UAsdaCashGiftSubsystem::kNameBytes; ++i)
		{
			if (WA[i] != WB[i]) { return false; }
			if (WA[i] == 0) { return true; }
		}
		return true;
	}
}

bool AsdaCashGiftOn() { return CVarCashGiftEnable.GetValueOnGameThread() != 0; }

bool AsdaShopCanInteract(const UObject* Ctx)
{
	// `uiCanInteractWithTarget@0xC43310` (ui_core.c:28972-28982), порядок эталона.
	const UWorld* W = Ctx ? Ctx->GetWorld() : nullptr;
	const UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	if (!GI) { return true; }
	UGameInstance* G = const_cast<UGameInstance*>(GI);
	if (const UAsdaEquipShopSubsystem* Es = G->GetSubsystem<UAsdaEquipShopSubsystem>()) { if (Es->IsOpen()) { return false; } }   // this+10327
	if (UAsdaTradeStateSubsystem::IsExchangeWindowOpen(Ctx)) { return false; }                                                       // g_bPartyInviteFlag
	if (const UAsdaSystemOptionSubsystem* So = G->GetSubsystem<UAsdaSystemOptionSubsystem>()) { if (So->IsLocalPlayerBusy()) { return false; } }   // objStateAllowsRender
	if (const UAsdaInventorySubsystem* Inv = G->GetSubsystem<UAsdaInventorySubsystem>()) { if (Inv->IsWindowOpen()) { return false; } }          // sub_93248D
	return true;
}

bool AsdaCashGiftFixOn(int32 N)
{
	switch (N)
	{
	case 1: return CVarFixK1.GetValueOnGameThread() != 0;
	case 2: return CVarFixK2.GetValueOnGameThread() != 0;
	case 3: return CVarFixK3.GetValueOnGameThread() != 0;
	case 5: return CVarFixK5.GetValueOnGameThread() != 0;
	default: return false;   // Д-К4 — текст loc 20070 (ведомости); Д-К6 — не дефект (см. выше)
	}
}

// ═══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═══════════════════════════════════════════════════════════════════

void UAsdaCashGiftSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	BindLate();
	UE_LOG(LogAsdaCashGift, Log, TEXT("[подарок] подсистема поднята (P101_CashGift; выключатель — общий ключ подарка, сейчас %s)"),
		AsdaCashGiftOn() ? TEXT("ВКЛ") : TEXT("выкл"));
}

void UAsdaCashGiftSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>()) { Net->OnCashGiftPacket.Remove(HNet); }
		if (UAsdaHotbarSubsystem* Hb = GI->GetSubsystem<UAsdaHotbarSubsystem>()) { Hb->OnNoticeIconClicked.Remove(HIcon); }
	}
	HNet.Reset(); HIcon.Reset(); bBound = false;
	Super::Deinitialize();
}

void UAsdaCashGiftSubsystem::BindLate()
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }
	if (!HNet.IsValid())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			HNet = Net->OnCashGiftPacket.AddUObject(this, &UAsdaCashGiftSubsystem::OnNetPacket);
		}
	}
	if (!HIcon.IsValid())
	{
		if (UAsdaHotbarSubsystem* Hb = GI->GetSubsystem<UAsdaHotbarSubsystem>())
		{
			HIcon = Hb->OnNoticeIconClicked.AddUObject(this, &UAsdaCashGiftSubsystem::OnNoticeIcon);
		}
	}
	bBound = HNet.IsValid() && HIcon.IsValid();
}

void UAsdaCashGiftSubsystem::ResetSessionState(EAsdaSessionResetReason /*Reason*/)
{
	// Новый процесс оригинала начинает с конструктора `g_pShopWnd`: маршрута, имени и флагов нет.
	FMemory::Memzero(Route, sizeof(Route));
	bRoute = false;
	ConfirmedName.Reset();
	bWaitCheck = bWaitPresent = bPresentSent = false;
	bCheckByBooster = bPresentByBooster = false;
	LastGift.Reset();
	Queue.Reset();
	ShowcaseMode = 1;
}

void UAsdaCashGiftSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.CashGift");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	if (!bBound) { BindLate(); }
	if (Queue.Num() > 0) { DebugPump(); }
}

int32 UAsdaCashGiftSubsystem::DebugPump()
{
	TArray<TPair<int32, TArray<uint8>>> Work = MoveTemp(Queue);
	Queue.Reset();
	for (const TPair<int32, TArray<uint8>>& P : Work)
	{
		switch (P.Key)
		{
		case 5073: Handle5073(P.Value); break;
		case 5075: Handle5075(P.Value); break;
		case 5077: Handle5077(P.Value); break;
		case 5078: Handle5078(P.Value); break;
		default: break;
		}
	}
	return Work.Num();
}

void UAsdaCashGiftSubsystem::OnNetPacket(int32 Opcode, const TArray<uint8>& Body)
{
	// ★ Закон no-reentrant-net-callbacks: из сетевого колбэка ни окон, ни записей сумки — только в очередь.
	Queue.Emplace(Opcode, Body);
}

// ═══════════════════════════════ ОБЩИЕ ПОМОЩНИКИ ══════════════════════════════════════════════════════════════════

bool UAsdaCashGiftSubsystem::CanInteract(bool bChat) const
{
	// Порт `uiCanInteractWithTarget@0xC43310` целиком — общая точка `AsdaShopCanInteract` (её же зовёт отправитель 5070 окна 6000).
	const bool bOk = AsdaShopCanInteract(this);
	if (!bOk && bChat)
	{
		// network.c:13664-13667 / 13246-13250: чат 12919 цветом −11514, 10 с.
		AsdaNotice::ChatColored(GetGameInstance(), EAsdaMsgCategory::Trade,
			AsdaNotice::LocRaw(GetGameInstance(), kLocCantUse), AsdaNoticeColor::Gold, 10000.f);
		const_cast<UAsdaCashGiftSubsystem*>(this)->LastLocId = kLocCantUse;
	}
	return bOk;
}

void UAsdaCashGiftSubsystem::MsgBoxLoc(int32 LocId, const FString& Slot1)
{
	// ONEBUT 1500: `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, UI_FormatLocalizedNotice(id), 1)`.
	const UGameInstance* GI = GetGameInstance();
	const FString Text = Slot1.IsEmpty() ? AsdaNotice::LocRaw(GI, LocId) : AsdaNotice::LocSlot(GI, LocId, Slot1);
	AsdaNotice::MsgBox(GI, EAsdaMsgCategory::Trade, Text);
	LastLocId = LocId;
	LastText = Text;
	UE_LOG(LogAsdaCashGift, Log, TEXT("[подарок] окно 1500: loc %d «%s»"), LocId, *Text);
}

void UAsdaCashGiftSubsystem::ChatLoc(int32 LocId, FColor Color, const FString& S1, const FString& S2)
{
	// `CChat_AddTokenizedLines(g_pChatWnd, UI_FormatLocalizedNotice(id), цвет, 10000, 0)`.
	const UGameInstance* GI = GetGameInstance();
	FString Text;
	if (S1.IsEmpty() && S2.IsEmpty()) { Text = AsdaNotice::LocRaw(GI, LocId); }
	else if (UAsdaTextSubsystem* T = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr)
	{
		T->NoticeSetString(0, S1);
		T->NoticeSetString(1, S2);
		Text = T->FormatNotice(LocId);
	}
	AsdaNotice::ChatColored(GI, EAsdaMsgCategory::Trade, Text, Color, 10000.f);
	LastLocId = LocId;
	LastText = Text;
	UE_LOG(LogAsdaCashGift, Log, TEXT("[подарок] чат: loc %d «%s»"), LocId, *Text);
}

bool UAsdaCashGiftSubsystem::IsSelfName(const FString& Name) const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const FString Self = WS ? WS->GetSelfCharName() : FString();
	if (Self.IsEmpty()) { return false; }
	return SameWireName(Name, Self);
}

bool UAsdaCashGiftSubsystem::IsSelfKnown() const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	return WS && !WS->GetSelfCharName().IsEmpty();
}

void UAsdaCashGiftSubsystem::ResetWindowState()
{
	// Порт `uiResetWindowState@0xC34570` (ui_core.c:27631-27652): +9471 = −1, +9474 = 0, +9476 = −1, +9480 = 0, +9482 = −1
	//   (+9470/+9472/+9473 эталон НЕ трогает — маршрут чистится частично; 1:1), [2072] = 0, +9468 = 1, затем
	//   `uiDialogCloseReset@0xC42600` (ui_core.c:28865): панели 6296/6333 закрыты, [2078] = 0, [2072] = 0 и
	//   `uiPanelClearTextAndState` — поля 6311/6345 и ИМЯ +10734 пустые; модалка 1559 закрыта, код товара −1, копия 5078 = 0.
	Route[1] = 0xFF;
	Route[4] = 0;
	AsdaBytes::WrI32(&Route[6], -1);
	Route[10] = 0;
	AsdaBytes::WrI16(&Route[11], -1);
	bRoute = false;
	bPresentSent = false;
	bWaitCheck = false;      // `uiDialogCloseReset`: `gap_EA0[4568]` = [2078] = 0
	bCheckByBooster = bPresentByBooster = false;
	ShowcaseMode = 1;
	LastGift.Reset();
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaPurchaseBoxSubsystem* Box = GI->GetSubsystem<UAsdaPurchaseBoxSubsystem>()) { if (Box->IsOpen()) { Box->Close(); } }
	}
	ClearNameFieldsAndName();   // `uiPanelClearTextAndState` (из `uiDialogCloseReset`)
	OnWindowReset.Broadcast();  // окно 6000: панели 6296/6333 закрыть (`uiDialogCloseReset`)
}

void UAsdaCashGiftSubsystem::ClearNameFieldsAndName()
{
	// Порт `uiPanelClearTextAndState@0xC46BA0`: поля 6311 и 6345 пустые, имя +10734 = 0.
	ConfirmedName.Reset();
	OnClearNameFields.Broadcast(/*bBoth*/true, 0);
}

void UAsdaCashGiftSubsystem::OnShopPanelOpened()
{
	// item_inventory.c:21250-21258: `gap_EA0[4568]` = [2078] = 0, `memset(+10734, 0, 20)`, `gap_EA0[4544]` = [2072] = 0.
	bWaitCheck = false;
	bPresentSent = false;
	ConfirmedName.Reset();
}

void UAsdaCashGiftSubsystem::OnBoosterPanelReset(bool bWindowClosed)
{
	if (bCheckByBooster) { bWaitCheck = false; bCheckByBooster = false; }
	if (bWindowClosed && bPresentByBooster) { bWaitPresent = false; bPresentSent = false; bPresentByBooster = false; }
}

void UAsdaCashGiftSubsystem::OnShopDialogCloseReset()
{
	// ui_core.c:28866-28890: [2078] = 0, [2072] = 0, `uiPanelClearTextAndState` (поля 6311/6345 и имя).
	bWaitCheck = false;
	bPresentSent = false;
	ClearNameFieldsAndName();
}

// ═══════════════════════════════ ОТПРАВКА 5072 ════════════════════════════════════════════════════════════════════

bool UAsdaCashGiftSubsystem::SendCheckName(EAsdaGiftSource Src, const FString& Name)
{
	if (!AsdaCashGiftOn())
	{
		UE_LOG(LogAsdaCashGift, Warning, TEXT("[подарок] 5072 не отправлен: подарок выключен (сервер без P101)"));
		return false;
	}
	const bool bShop = (Src == EAsdaGiftSource::Shop6000);
	const bool bGates = bShop || AsdaCashGiftFixOn(5);   // [ИСПРАВЛЕНИЕ] Д-К5: 15300 с гейтами 6000
	if (bGates)
	{
		if (!CanInteract(/*bChat*/true)) { return false; }                // 12919
		if (bWaitCheck)                                                   // [2078] ≠ 0 → молча (network.c:13529)
		{
			UE_LOG(LogAsdaCashGift, Log, TEXT("[подарок] 5072: ждём ответ 5073 на прошлую проверку — молча"));
			return false;
		}
	}
	if (Name.IsEmpty()) { UE_LOG(LogAsdaCashGift, Log, TEXT("[подарок] 5072: имя пустое — молча")); return false; }
	if (IsGmName(Name))
	{
		// ★ 08.11.2026 решение владельца (`asda.CashGift.RefuseGm`): администраторам подарки не шлём — 5072 не уходит.
		MsgBoxLoc(kLocGmRefuse);
		UE_LOG(LogAsdaCashGift, Log, TEXT("[подарок] 5072 из %s: «%s» — администратор, не отправлен"), bShop ? TEXT("6000") : TEXT("15300"), *Name);
		return false;
	}
	if (!bShop && AsdaCashGiftFixOn(5) && IsSelfName(Name))
	{
		// [ИСПРАВЛЕНИЕ] Д-К5: у 15300 проверки «себе» нет; та же строка и канал, что у 6000 (`uiConfirmPasswordMatch`, ui_core.c:29653).
		AsdaNotice::ChatColored(GetGameInstance(), EAsdaMsgCategory::Trade,
			AsdaNotice::LocRaw(GetGameInstance(), kLocSelf), AsdaNoticeColor::Gold, 10000.f);
		LastLocId = kLocSelf;
		return false;
	}
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return false; }
	uint8 Body[kNameBytes];
	NameToWire(Name, Body);
	const bool bOk = Net->SendCashGiftCheck5072(Body);
	if (bOk && bGates) { bWaitCheck = true; bCheckByBooster = !bShop; }   // network.c:13655 (`self[2078] = 1`); у 15300 эталон флаг не ставит
	UE_LOG(LogAsdaCashGift, Log, TEXT("[подарок] 5072 из %s: «%s» -> %s"), bShop ? TEXT("6000") : TEXT("15300"), *Name,
		bOk ? TEXT("ушёл") : TEXT("НЕ ушёл"));
	return bOk;
}

// ═══════════════════════════════ ОТПРАВКА 5074 ════════════════════════════════════════════════════════════════════

void UAsdaCashGiftSubsystem::BuildPresentBody(int32 ItemCode, bool bTable1, int32 Qty, uint8 Out[kPresentBody]) const
{
	// Конструктор `sub_B852A0@0xB852A0` (shop_trade.c:1250-1268): +0 = 0; +1…+7 = 0; +8…+35 коды −1; +36…+175 пары {−1, 0}
	//   (`sub_926E4E` — opidx −1, opval 0, network.c:59606); +176 = 0, +177 = 0, +178 = −1, +179 = −1, +180 = −1, +184 = 0, +185 = −1.
	FMemory::Memzero(Out, kPresentBody);
	for (int32 k = 0; k < kMaxPresentItems; ++k) { AsdaBytes::WrI32(&Out[8 + 4 * k], -1); }
	for (int32 j = 0; j < 35; ++j) { AsdaBytes::WrI16(&Out[36 + 4 * j], -1); AsdaBytes::WrI16(&Out[38 + 4 * j], 0); }
	Out[176] = 0; Out[177] = 0; Out[178] = 0xFF; Out[179] = 0xFF;
	AsdaBytes::WrI32(&Out[180], -1);
	Out[184] = 0;
	AsdaBytes::WrI16(&Out[185], -1);

	// Заполнение отправителя: +0 режим витрины (6000: `v25[9468]` = 1; 15300: 1), позиции k < Qty: таблица и код.
	Out[0] = ShowcaseMode;
	// 15300: `for (slotIdx = 0; slotIdx < [609]; …) if (slotIdx < 7)` — при количестве 0 позиции не заполняются (shop_trade.c:1100).
	const int32 N = FMath::Clamp(Qty, 0, kMaxPresentItems);
	for (int32 k = 0; k < N; ++k)
	{
		Out[1 + k] = bTable1 ? 1 : 0;
		AsdaBytes::WrI32(&Out[8 + 4 * k], ItemCode);
	}
	// Маршрут получателя: `*(u32*)(+9470)` → +176…+179; +9476 → +180; +9480 → +184; +9482 → +185 (network.c:13125-13129).
	Out[176] = Route[0]; Out[177] = Route[1]; Out[178] = Route[2]; Out[179] = Route[3];
	FMemory::Memcpy(&Out[180], &Route[6], 4);
	Out[184] = Route[10];
	FMemory::Memcpy(&Out[185], &Route[11], 2);
}

bool UAsdaCashGiftSubsystem::SendPresent(EAsdaGiftSource Src, int32 ItemCode, int32 Qty, int32 CashGxp, const FString& FieldName, int32 WireCode)
{
	if (!AsdaCashGiftOn())
	{
		UE_LOG(LogAsdaCashGift, Warning, TEXT("[подарок] 5074 не отправлен: подарок выключен (сервер без P101)"));
		return false;
	}
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaNetSubsystem* Net = GI ? const_cast<UGameInstance*>(GI)->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!IT || !Net) { return false; }
	const bool bShop = (Src == EAsdaGiftSource::Shop6000);
	const bool bGates = bShop || AsdaCashGiftFixOn(5);

	// ★ 08.11.2026 решение владельца (`asda.CashGift.RefuseGm`): второй рубеж — подтверждённое имя или поле 15519 «GM_…» → 5074 не уходит.
	if (IsGmName(ConfirmedName) || (!bShop && IsGmName(FieldName)))
	{
		MsgBoxLoc(kLocGmRefuse);
		UE_LOG(LogAsdaCashGift, Log, TEXT("[подарок] 5074 из %s: получатель «%s» — администратор, не отправлен"),
			bShop ? TEXT("6000") : TEXT("15300"), bShop ? *ConfirmedName : *FieldName);
		return false;
	}
	if (!bShop)
	{
		// `BoosterShop_SendBuyPresent@0xB84840` (shop_trade.c:1076-1094, 1235-1239): имя поля 15519 непустое и РАВНО
		//   подтверждённому 5073 (`byte_2BE811E`), иначе ONEBUT 12939.
		if (FieldName.IsEmpty() || ConfirmedName.IsEmpty() || !SameWireName(FieldName, ConfirmedName))
		{
			MsgBoxLoc(kLocNoName);
			UE_LOG(LogAsdaCashGift, Log, TEXT("[подарок] 5074 из 15300: имя «%s» ≠ подтверждённому «%s» — 12939"), *FieldName, *ConfirmedName);
			return false;
		}
	}
	if (bGates)
	{
		if (!CanInteract(/*bChat*/true)) { return false; }                        // 12919 (network.c:13246)
		if (bWaitPresent) { UE_LOG(LogAsdaCashGift, Log, TEXT("[подарок] 5074: ждём 5075 — молча")); return false; }   // [2079]
		if (ConfirmedName.IsEmpty()) { UE_LOG(LogAsdaCashGift, Log, TEXT("[подарок] 5074: имя не подтверждено — молча")); return false; }
	}
	const FAsdaItemRecord* Rec = IT->GetItem(ItemCode);
	FAsdaSpecialItemlistRow Sp;
	const bool bSp = !Rec && IT->GetSpecialItem(ItemCode, Sp);
	if (!Rec && !bSp)
	{
		// Эталон: нет записи предмета → молча (6000: network.c:13101; 15300: shop_trade.c:1225).
		UE_LOG(LogAsdaCashGift, Warning, TEXT("[подарок] 5074: кода %d нет ни в item_list, ни в special_itemlist — молча"), ItemCode);
		return false;
	}
	const int32 N = bShop ? 1 : FMath::Clamp(Qty, 0, kMaxPresentItems);   // ревизия Н-9: 0 — как эталон (пакет без позиций)
	if (bGates)
	{
		// Кэш против цены (6000: `ownedAmount >= requiredAmount` — цена ОДНОЙ вещи; 15300 [ИСПРАВЛЕНИЕ] Д-К5 — N × цена) → чат 6778.
		const int64 Need = (int64)(Rec ? Rec->Price : Sp.Price) * N;
		if ((int64)CashGxp < Need)
		{
			AsdaNotice::ChatColored(GI, EAsdaMsgCategory::Trade, AsdaNotice::LocRaw(GI, kLocNoGxp), AsdaNoticeColor::Gold, 10000.f);
			LastLocId = kLocNoGxp;
			UE_LOG(LogAsdaCashGift, Log, TEXT("[подарок] 5074: GXP %d < цены %lld — 6778"), CashGxp, Need);
			return false;
		}
	}
	uint8 Body[kPresentBody];
	const bool bWire = WireCode > 0 && WireCode != ItemCode;   // коробка выдачи — всегда запись item_list
	BuildPresentBody(bWire ? WireCode : ItemCode, bWire || Rec != nullptr, N, Body);
	if (bShop || AsdaCashGiftFixOn(5)) { bPresentSent = true; }   // [2072] = 1 до отправки (network.c:13130)
	const bool bOk = Net->SendCashGiftPresent5074(Body);
	if (bOk && (bShop || AsdaCashGiftFixOn(5))) { bWaitPresent = true; bPresentByBooster = !bShop; }   // [2079] = 1 (network.c:13239)
	UE_LOG(LogAsdaCashGift, Log, TEXT("[подарок] 5074 из %s: код %d × %d получателю «%s» -> %s"),
		bShop ? TEXT("6000") : TEXT("15300"), ItemCode, N, *ConfirmedName, bOk ? TEXT("ушёл") : TEXT("НЕ ушёл"));
	return bOk;
}

// ═══════════════════════════════ ПРИЁМ ═════════════════════════════════════════════════════════════════════════════

// 5073 `netHandleCraftResultPacket@0xC337E0` (network.c:13001-13069).
void UAsdaCashGiftSubsystem::Handle5073(const TArray<uint8>& B)
{
	if (B.Num() < 34) { return; }
	bWaitCheck = false;   // `pCraftWnd[2078] = 0`
	bCheckByBooster = false;
	const uint8 Code = B[0];
	const FString Name = FAsdaLoginData::DecodeCp1251(&B[14], 20);
	UE_LOG(LogAsdaCashGift, Log, TEXT("[подарок] 5073: код %d, имя «%s», маршрут ch=%d pt=%d gs=%d th=%d ui=%d ci=%d un=%d"),
		(int32)Code, *Name, (int32)B[1], (int32)B[2], (int32)(int8)B[3], (int32)(int8)B[4],
		AsdaBytes::RdI32(&B[7]), (int32)B[11], (int32)AsdaBytes::RdI16(&B[12]));
	switch (Code)
	{
	case 1:
		// Маршрут: +1 → +9470, слово +2 → +9471, слово +4 → +9473, +7 → +9476, +11 → +9480, +12 → +9482; байт +6 (Lvl) не читается.
		Route[0] = B[1];
		Route[1] = B[2]; Route[2] = B[3];
		Route[3] = B[4]; Route[4] = B[5];
		FMemory::Memcpy(&Route[6], &B[7], 4);
		Route[10] = B[11];
		FMemory::Memcpy(&Route[11], &B[12], 2);
		bRoute = true;
		ConfirmedName = Name;   // `memcpy(+10734, &craftResult[7], 20)`
		MsgBoxLoc(kLoc5073[1], Name);   // 20070 [ss1] (Д-К4: текст исправлен в таблице)
		break;
	case 2:
		MsgBoxLoc(kLoc5073[2], Name);   // 20040 [ss1] — не в сети
		ClearNameFieldsAndName();
		break;
	case 0: case 3: case 4: case 5:
		MsgBoxLoc(kLoc5073[Code]);
		ClearNameFieldsAndName();
		break;
	default:
		UE_LOG(LogAsdaCashGift, Log, TEXT("[подарок] 5073: код %d — эталон `default: return` (молча)"), (int32)Code);
		break;
	}
}

// 5075 `bankGoldResultPacket_Handle@0xC34F80` (ui_core.c:27656-27725).
void UAsdaCashGiftSubsystem::Handle5075(const TArray<uint8>& B)
{
	if (B.Num() < 6) { return; }
	bWaitPresent = false;   // `gap_EA0[4572]` = [2079] = 0
	bPresentByBooster = false;
	const uint8 Code = B[0];
	const int32 Balance = AsdaBytes::RdI32(&B[2]);
	UE_LOG(LogAsdaCashGift, Log, TEXT("[подарок] 5075: код %d, байт+1 %d, баланс %d"), (int32)Code, (int32)B[1], Balance);
	switch (Code)
	{
	case 0:
		ResetWindowState();
		MsgBoxLoc(kLoc5075[0]);
		break;
	case 1:
	{
		// Баланс кэша → `bag+gap_43B[17]`, подписи 6000 (`m_pStoreTxt09`), окна персонажа, 15300 (`uiRefreshStatTexts`):
		//   [ПОРТ] общая точка баланса — делегат 5467 «статус 1» (на нём висят 6000, 15300, гача, парикмахерская).
		if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
		{
			Net->OnPremiumBalance.Broadcast(1, Balance);
		}
		const FString Name = ConfirmedName;   // `CObject_SetSlotString(…, +10734)` ДО сброса
		MsgBoxLoc(kLoc5075[1], Name);
		ResetWindowState();
		break;
	}
	case 2: case 3: case 4: case 5: case 6:
		MsgBoxLoc(kLoc5075[Code]);
		ResetWindowState();
		break;
	default:
		UE_LOG(LogAsdaCashGift, Log, TEXT("[подарок] 5075: код %d — эталон `default: return`"), (int32)Code);
		break;
	}
}

// 5077 `tradeOnItemBindResult@0xC346B0` (shop_trade.c:3561-3625): нотисы — только при [2072].
void UAsdaCashGiftSubsystem::Handle5077(const TArray<uint8>& B)
{
	if (B.Num() < 61) { return; }
	bWaitPresent = false;
	bPresentByBooster = false;
	const uint8 Code = B[0];
	UE_LOG(LogAsdaCashGift, Log, TEXT("[подарок] 5077: код %d, [2072]=%d"), (int32)Code, bPresentSent ? 1 : 0);
	if (Code > 2 || !bPresentSent) { return; }
	const FString Name = ConfirmedName;
	MsgBoxLoc(kLoc5077[Code], Code == 1 ? Name : FString());
	ResetWindowState();
}

// 5078: `Recv_PartyMember(g_pQuickSlotBar, 3)` → `tradeSendConfirmPacket` → `tradeOnConfirmPacket@0xC34950` (shop_trade.c:3641-3725).
void UAsdaCashGiftSubsystem::Handle5078(const TArray<uint8>& B)
{
	if (B.Num() < 421) { return; }
	UGameInstance* GI = GetGameInstance();
	LastPlaced = 0;
	// [ИСПРАВЛЕНИЕ] Д-К2: эталон при [2072] ≠ 0 (сам отправляет) выбрасывает пакет целиком — вещи уже на сервере, но невидимы.
	if (bPresentSent && !AsdaCashGiftFixOn(2))
	{
		UE_LOG(LogAsdaCashGift, Warning, TEXT("[подарок] 5078 выброшен: идёт своя отправка ([2072]) — как оригинал (Д-К2 выключен)"));
		return;
	}
	int32 Valid = 0;
	for (int32 i = 0; i < 7; ++i) { if (AsdaBytes::RdI32(&B[20 + 57 * i]) > 0) { ++Valid; } }
	if (Valid <= 0) { UE_LOG(LogAsdaCashGift, Log, TEXT("[подарок] 5078: ни одной позиции — выход")); return; }
	LastGift = B;   // `gap_EA0[4548]` = 1 и копия в +9508
	const FString Sender = FAsdaLoginData::DecodeCp1251(&B[0], 20);
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	for (int32 i = 0; i < 7; ++i)
	{
		const uint8* R = &B[20 + 57 * i];
		const int32 Code = AsdaBytes::RdI32(R);
		if (Code <= 0) { continue; }
		const FAsdaItemRecord* Rec = IT ? IT->GetItem(Code) : nullptr;
		FAsdaSpecialItemlistRow Sp;
		const bool bSp = !Rec && IT && IT->GetSpecialItem(Code, Sp);
		if (Rec || bSp)
		{
			// `Inventory_SetBagSlot@0xD61650`: запись 57 б — В СУМКУ по Posx (+5), Plate (+4) не смотрится ⇒ контейнер 1.
			//   ★ Ревизия Н-7 (разобрано 02.11): эталон копирует запись в `g_InventoryBag` КАК ЕСТЬ, Plate сервера (GS `sub_513910`,
			//   GameServer.c:105175 — байт `v18[55j+61]` из ответа DB 2135) остаётся в ячейке. В модели порта +4 ячейки = её контейнер
			//   (так делают все пути `PutRecord` с `ForcedContainer`), поэтому байт приводится к 1 — иначе последующие пакеты ячейки
			//   (перемещение, использование) взяли бы чужой контейнер. Ячейку выбирает эталон тот же (+5, сумка).
			TArray<uint8> Rec57; Rec57.Append(R, FAsdaItemInstance::kRecordSize);
			Rec57[4] = 1;
			if (Inv && Inv->ApplyExternalRecord(Rec57, UAsdaInventorySubsystem::EExternalRecordMode::AsIs)) { ++LastPlaced; }
		}
		if (Rec)
		{
			ChatLoc(kLocGotItem, kColItemList, Sender, IT->GetItemName(Code));   // 0xFF96FFD4
		}
		else if (bSp)
		{
			FAsdaSpecialitemTooltiplistRow Tip;   // `itemGetTooltipById(…)->name`
			const FString ItemName = IT->GetSpecialItemTooltip(Code, Tip) && !Tip.Name.IsEmpty() ? Tip.Name : IT->GetItemName(Code);
			ChatLoc(kLocGotItem, AsdaNoticeColor::Gold, Sender, ItemName);       // −11514
		}
	}
	// Вес `+419` i16 → `HUD_UpdateMiniWeightBar`, если > 0. ★ P97: вес персонажа u16 (`AsdaWeightFromWire`).
	const int32 Weight = AsdaWeightFromWire((int32)AsdaBytes::RdI16(&B[419]));
	if (Weight > 0)
	{
		if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr) { Net->SetSelfWeight(Weight, -1, TEXT("5078 подарок")); }
	}
	// `uiResetWindowState(savedThis)` (shop_trade.c:3723). ★ [ИСПРАВЛЕНИЕ] Д-К2 / ревизия С-1: если пакет принят во время СВОЕЙ отправки
	//   ([2072], оригинал до сюда не доходил — выбрасывал пакет), сброс НЕ делаем: он стёр бы имя получателя (20071 из 5075 вышел бы
	//   с пустым [ss1]), снял [2072] (пропали бы нотисы 5077) и закрыл бы панель/модалку посреди действия.
	if (!(bPresentSent && AsdaCashGiftFixOn(2))) { ResetWindowState(); }
	else { LastGift.Reset(); }
	// Успех `tradeOnConfirmPacket` → значок 3, звук 215, чат 5060 (network.c:27332-27341).
	if (UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr) { Hb->AddNoticeIcon(kNoticeType); }
	if (UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr) { Snd->PlaySound2DById(kSoundGift); }
	ChatLoc(kLocGotGift, kColGotGift);
	UE_LOG(LogAsdaCashGift, Log, TEXT("[подарок] 5078 от «%s»: позиций %d, положено %d, вес %d"), *Sender, Valid, LastPlaced, Weight);
}

// Клик по значку 3: `uiNoticeButtonDispatch` case 3 → `uiShowItemBuyTooltipNotice@0xC35330` (ui_core.c:27741-27748, 69407-69408).
void UAsdaCashGiftSubsystem::OnNoticeIcon(int32 Type)
{
	if (Type != kNoticeType) { return; }
	MsgBoxLoc(kLocGiftIcon);
	ResetWindowState();
}

// ═══════════════════════════════ ДИАГНОСТИКА ══════════════════════════════════════════════════════════════════════

void UAsdaCashGiftSubsystem::DebugSetConfirmed(const FString& Name, const uint8 Route13[kRouteBytes])
{
	FMemory::Memcpy(Route, Route13, kRouteBytes);
	bRoute = true;
	ConfirmedName = Name;
}

FString UAsdaCashGiftSubsystem::DumpState() const
{
	return FString::Printf(TEXT("подарок: вкл=%d имя=«%s» маршрут=%d [ch=%d pt=%d gs=%d th=%d ui=%d ci=%d un=%d] [2078]=%d [2079]=%d [2072]=%d очередь=%d"),
		AsdaCashGiftOn() ? 1 : 0, *ConfirmedName, bRoute ? 1 : 0, (int32)Route[0], (int32)Route[1], (int32)(int8)Route[2], (int32)(int8)Route[3],
		AsdaBytes::RdI32(&Route[6]), (int32)Route[10], (int32)AsdaBytes::RdI16(&Route[11]),
		bWaitCheck ? 1 : 0, bWaitPresent ? 1 : 0, bPresentSent ? 1 : 0, Queue.Num());
}
