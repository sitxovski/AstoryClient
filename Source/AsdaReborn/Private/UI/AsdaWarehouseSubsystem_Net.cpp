// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWarehouseSubsystem_Net.cpp — ★ R3-c 12.11.2026 (разрез AsdaWarehouseSubsystem.cpp, дословный перенос): кнопки сдать/забрать, отправка, ответы склада, нотисы, дампы и дев-команды.
//   Общее для частей — в AsdaWarehouseSubsystem_Private.h; шапка и реверс — в AsdaWarehouseSubsystem.cpp.
#include "UI/AsdaWarehouseSubsystem.h"
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой

#include "UI/AsdaAnimatedPlane.h"

#include "AsdaWarehouseSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaWarehouseSubsystem*.cpp

namespace WareLoc {
	constexpr int32 kKeepRefused  = 2240;     // 5009 код 0 → ПЛАШКА
	constexpr int32 kKeepNoSpace  = 2241;     // 5009 код 2 → ПЛАШКА
	constexpr int32 kSoulmateOnly = 8013;     // 5009 код 3 / 5011 код 4 → МОДАЛ-OK
	constexpr int32 kNeedAtLeast1 = 2364;     // 5009 код 4 → МОДАЛ-OK
	constexpr int32 kKeepBound    = 2246;     // 5009 код 5 → МОДАЛ-OK
	constexpr int32 kNoGoldDeposit= 2250;     // 5009 код 6 → МОДАЛ-OK
	constexpr int32 kKeepOverload = 2252;     // 5009 код 7 → МОДАЛ-OK
	constexpr int32 kTakeNotFound = 2242;     // 5011 код 0 → ПЛАШКА
	constexpr int32 kTakeNoSpace  = 2243;     // 5011 код 2 → ПЛАШКА
	constexpr int32 kTakeOverload = 2244;     // 5011 код 3 → ПЛАШКА И МОДАЛ-OK (оба сразу)
	constexpr int32 kTakeNoMoney  = 2251;     // 5011 код 5 → МОДАЛ-OK
	constexpr int32 kNoMoneyLocal = 2245;     // локальная проверка перед 5010 → МОДАЛ-OK
}

static constexpr float  kNoticeTtlMs = 10000.f;

// ★ [ИСПРАВЛЕНИЕ] 05.11.2026. Гейты обмена (меню shop_trade.c:6627, «Принять» ui_core.c:57205-57306) и ремесла (`tileInteractGateAndOpen`,
//   ui_core.c:100525) читают `CObject_GetField5581(g_pWarehouseWnd)` = `+0x5734 isOpen` — его пишут ТОЛЬКО `uiWindowOpen_EA20` (свиток, 1) и
//   `uiHidePanelAndChild` (0), у кладовщика он не ставится. Порт спрашивал `IsOpen()` (окно 4000). Разница видна при свитке возврата:
//   панель 5500 открыта, окно 4000 ещё закрыто или закрыто крестиком — эталон отказывает (10307 / молча), порт пропускал.
//   1 = как эталон (`IsRecallActive`); 0 = прежнее поведение порта (`IsOpen`).
static ASDA_DEV_CVAR(int32, CVarWareGateRecallFlag,
	TEXT("asda.Warehouse.GateRecallFlag"), 1,
	TEXT("1 = гейты обмена и ремесла смотрят на флаг свитка возврата склада (эталон, поле 5581); 0 = прежнее: на «окно 4000 открыто»."),
	ECVF_Default);

// ═════════════════════════ СЕТЬ ═══════════════════════════════════════════════════════════════
// Кнопка 4002 → `Send_CharItemKeeping@0xE8C3A0`, пакет 5008.
// Гейт чужих окон «склад» — разбор у ключа `CVarWareGateRecallFlag`.
bool UAsdaWarehouseSubsystem::IsGateField5581() const
{
	return (CVarWareGateRecallFlag.GetValueOnGameThread() != 0) ? bRecallActive : IsOpen();
}

void UAsdaWarehouseSubsystem::OnKeepClicked()
{
	if (Mode != kModeKeep) { return; }
	if (WareBlockedBySelfStore(this, TEXT("склад: «Хранить» 4002 (5008) при открытой лавке"))) { return; }   // ★ 05.11.2026
	SendKeep();
}

// Кнопка 4049 → `Send_CharItemRetake@0xE8BAF0`, пакет 5010. ПЕРЕД отправкой — локальная проверка
//   «деньги ≥ комиссии», иначе модальное ОК с нотисом 2245 (network.c:31196-31197).
void UAsdaWarehouseSubsystem::OnTakeClicked()
{
	if (Mode != kModeTake) { return; }
	const int32 Fee = kFeePerItem * BasketOccupied();
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const int32 Gold = Inv ? Inv->GetGold() : 0;
	// ★ Проверка идёт по ЧИСЛУ, а не по подписи: на своей фракционной карте поле 4045 показывает
	//   «Free», но эталон всё равно сравнивает деньги с комиссией (§17 п.7).
	if (Gold < Fee) { NoticeMsgBox(WareLoc::kNoMoneyLocal); return; }
	SendTake();
}

void UAsdaWarehouseSubsystem::SendKeep()
{
	if (bRequestSent) { return; }                      // гейт `bRequestPending != 1`
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	TArray<TArray<uint8>> Body;
	for (const FAsdaItemInstance& B : Basket)
	{
		if (B.IsEmpty() || B.Raw.Num() < FAsdaItemInstance::kRecordSize) { continue; }
		Body.Add(B.Raw);
	}
	if (Body.Num() == 0) { return; }                   // эталон: пустая корзина — пакет не шлётся
	if (Net->SendItemKeeping5008(Body))
	{
		bRequestSent = true;
		UE_LOG(LogAsdaWare, Log, TEXT("[склад] 5008 отправлен: записей %d"), Body.Num());
	}
}

void UAsdaWarehouseSubsystem::SendTake()
{
	if (bRequestSent) { return; }
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	TArray<TArray<uint8>> Body;
	for (const FAsdaItemInstance& B : Basket)
	{
		if (B.IsEmpty() || B.Raw.Num() < FAsdaItemInstance::kRecordSize) { continue; }
		Body.Add(B.Raw);
	}
	if (Body.Num() == 0) { return; }
	if (Net->SendItemRetake5010(Body))
	{
		bRequestSent = true;
		UE_LOG(LogAsdaWare, Log, TEXT("[склад] 5010 отправлен: записей %d, комиссия %d"),
			Body.Num(), kFeePerItem * BasketOccupied());
	}
}

void UAsdaWarehouseSubsystem::DrainPendingPackets()
{
	// ★ U7 (В1 дедупа №2): общая очередь `TAsdaNetQueue` (замок + Drain) вместо своего массива. Прежний цикл `while … [0]; RemoveAt(0)` давал тот же порядок:
	//   во время разбора новые ответы в очередь не попадают (колбэк сети приходит из `UAsdaNetSubsystem::Tick`, не отсюда).
	TArray<FWarePacket> Local;
	if (!PendingResults.Drain(Local)) { return; }
	for (const FWarePacket& P : Local) { OnWarehouseResult(P.bKeeping, P.Result, P.Body); }
}

// `Recv_InventorySwap@0xE87220` (5009) и `Recv_InventorySwap2@0xE874F0` (5011), network.c:30776/30835.
//   ★ Флаг ожидания снимается ВСЕГДА, ещё до разбора кода (`*((BYTE*)this + 22328) = 0`).
void UAsdaWarehouseSubsystem::OnWarehouseResult(bool bKeeping, uint8 Result, const TArray<uint8>& Body)
{
	bRequestSent = false;
	PendingTab = -1;

	if (Result == 1)
	{
		// Успех: записи применяет ВЛАДЕЛЕЦ КОНТЕЙНЕРОВ — подсистема инвентаря (закон
		//   `port-shared-points`: второй копии модели не заводим). Затем корзина очищается
		//   (`uiWarehouseResetTab(this, 9)`) и окно перерисовывается.
		int32 Applied = 0;
		if (UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
		{
			Applied = bKeeping ? Inv->ApplyWarehouseKeepResult5009(Body) : Inv->ApplyWarehouseTakeResult5011(Body);
		}
		for (FAsdaItemInstance& B : Basket) { B.Clear(); }
		LastBasketCtrl = -1;
		SyncMirrors();
		if (bControlsReady) { RefreshAll(); }
		UE_LOG(LogAsdaWare, Log, TEXT("[склад] %s: успех, применено записей %d"),
			bKeeping ? TEXT("5009") : TEXT("5011"), Applied);
		return;
	}

	// ── Отказы. Каналы — из тел обработчиков (§10 отчёта), не выдуманы. ──
	if (bKeeping)
	{
		switch (Result)
		{
		case 0: NoticePlank(WareLoc::kKeepRefused);   break;   // 2240 → ПЛАШКА
		case 2: NoticePlank(WareLoc::kKeepNoSpace);   break;   // 2241 → ПЛАШКА
		case 3: NoticeMsgBox(WareLoc::kSoulmateOnly); break;   // 8013 → МОДАЛ-OK
		case 4: NoticeMsgBox(WareLoc::kNeedAtLeast1); break;   // 2364 → МОДАЛ-OK
		case 5: NoticeMsgBox(WareLoc::kKeepBound);    break;   // 2246 → МОДАЛ-OK
		case 6: NoticeMsgBox(WareLoc::kNoGoldDeposit);break;   // 2250 → МОДАЛ-OK «золото класть нельзя»
		case 7: NoticeMsgBox(WareLoc::kKeepOverload); break;   // 2252 → МОДАЛ-OK
		default:
			UE_LOG(LogAsdaWare, Warning, TEXT("[склад] 5009 код %d — текста под него в эталоне нет"), Result);
			break;
		}
	}
	else
	{
		switch (Result)
		{
		case 0: NoticePlank(WareLoc::kTakeNotFound);  break;   // 2242 → ПЛАШКА
		case 2: NoticePlank(WareLoc::kTakeNoSpace);   break;   // 2243 → ПЛАШКА
		case 3:                                                // 2244 → ПЛАШКА **И** МОДАЛ-OK (оба сразу)
			NoticePlank(WareLoc::kTakeOverload);
			NoticeMsgBox(WareLoc::kTakeOverload);
			break;
		case 4: NoticeMsgBox(WareLoc::kSoulmateOnly); break;   // 8013 → МОДАЛ-OK
		case 5: NoticeMsgBox(WareLoc::kTakeNoMoney);  break;   // 2251 → МОДАЛ-OK
		default:
			UE_LOG(LogAsdaWare, Warning, TEXT("[склад] 5011 код %d — текста под него в эталоне нет"), Result);
			break;
		}
	}
	// При отказе корзина НЕ очищается (эталон трогает её только в ветке успеха).
	if (bControlsReady) { RefreshAll(); }
}

// ═════════════════════════ НОТИСЫ ═════════════════════════════════════════════════════════════
FString UAsdaWarehouseSubsystem::Loc(int32 LocId) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` — теги срезаны, маркеры как в таблице (как было).
	return AsdaNotice::Loc(GetGameInstance(), LocId);
}

// `CChat_AddTokenizedLines(g_pChatWnd, txt, −11514, 10000, 0)` — СИСТЕМНЫЙ ЧАТ (журнал 2572).
void UAsdaWarehouseSubsystem::NoticeChat(int32 LocId) const
{
	UAsdaMessageSubsystem* Msg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr;
	const FString S = Loc(LocId);
	if (!Msg || S.IsEmpty()) { return; }
	FAsdaMsgParams P;
	P.Category  = EAsdaMsgCategory::ItemRefused;
	P.Text      = S;
	P.Targets   = (int32)EAsdaMsgTarget::SystemChat;
	P.bUseColor = true;
	P.Color     = AsdaNoticeColor::Gold;   // −11514 = 0xFFFFD306
	P.SrcOpcode = LocId;
	Msg->Post(P);
}

// `CControl_SetTooltipText@0xBA9A30` — МИСНЕЙМ: это строка в `WARMESSAGE_WIN` (ID 221),
//   крупная всплывающая плашка поверх экрана (ui_core.c:12069-12081). TTL эталона — 10000 мс.
void UAsdaWarehouseSubsystem::NoticePlank(int32 LocId) const
{
	// ★ Волна В3 ремесла (ревизия В0–В2 М-2): общая точка плашки `AsdaNotice::Plank` — канал 221, цвет, TTL прежние.
	AsdaNotice::Plank(GetGameInstance(), EAsdaMsgCategory::ItemRefused, Loc(LocId), AsdaNoticeColor::Gold, kNoticeTtlMs, LocId);
}

// `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, txt, 1)` = ONEBUT_MSGBOX 1500.
void UAsdaWarehouseSubsystem::NoticeMsgBox(int32 LocId) const
{
	// Эталон: `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст, 1)` = ONEBUT_MSGBOX 1500.
	// ★ Волна 1 (14.09.2026, В2): единый маршрут `AsdaNotice::MsgBoxLoc` — окно 1500, без оконной подсистемы —
	//   строка в ленту той же категорией. Категория/подпись кнопки — прежние.
	AsdaNotice::MsgBoxLoc(GetGameInstance(), EAsdaMsgCategory::ItemRefused, LocId, 2288);
}

// `isLocalCharOwnFactionOnMap@0xBCF030` (ui_core.c:17593): слот города карты + сравнение фракций.
bool UAsdaWarehouseSubsystem::IsOwnFactionMap() const
{
	const int32 Slot = AsdaUi::WarCitySlot(GetGameInstance());
	if (Slot < 0) { return false; }
	int32 Percent = 0; bool bNeutral = true;
	return AsdaUi::WarCityOwnedBySelf(GetGameInstance(), Slot, Percent, bNeutral);
}

// ═════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════════════
FString UAsdaWarehouseSubsystem::DumpControls() const
{
	FString S = TEXT("=== СКЛАД: разобранные контролы ===\n");
	auto Line = [&S](const TCHAR* Id, const UObject* W)
	{
		S += FString::Printf(TEXT("  %-38s %s\n"), Id, W ? TEXT("есть") : TEXT("НЕТ"));
	};
	S += TEXT("-- окно 4000 WARE_WIN --\n");
	Line(TEXT("4050 WARE_CLOBUT"),        BtnClose);
	Line(TEXT("4002 WAREKEEP_BUT"),       BtnKeep);
	Line(TEXT("4049 WARETAKE_BUT"),       BtnTake);
	Line(TEXT("4188 WAREKEEP_2BUT_OPT0"), BtnCat0);
	Line(TEXT("4188 WAREKEEP_2BUT_OPT1"), BtnCat1);
	Line(TEXT("4003 WAREKEEP_TXT00"),     TxtCap);
	Line(TEXT("4038 WAREKEEP_TXT01"),     TxtHeader);
	Line(TEXT("4044 WAREKEEP_TXT02"),     TxtFeeLbl);
	Line(TEXT("4045 WAREKEEP_TXT03 (КОМИССИЯ)"), TxtFee);
	Line(TEXT("4046 WAREKEEP_TXT04"),     TxtGoldLbl);
	Line(TEXT("4047 WAREKEEP_TXT05 (ДЕНЬГИ)"),   TxtGold);
	Line(TEXT("4190 WAREKEEP_TXT06"),     TxtExtra);
	Line(TEXT("4001 WAREKEEP_TOP (группа)"),     TabBlockRoot);
	Line(TEXT("4191 Ware_Gear"),          Gear);
	Line(TEXT("4192 Ware_AniGear"),       AniGear);
	Line(TEXT("4051 WARE_SEL00"),         GridSel);
	S += FString::Printf(TEXT("  4189 WAREKEEP_LIST (полоса из скина)   %s\n"),
		ScrollBar.IsBuilt() ? TEXT("построена") : TEXT("НЕТ"));
	S += FString::Printf(TEXT("  узлов в поддереве блока вкладок: %d, «вечно скрытых»: %d\n"),
		TabBlockParts.Num(), AlwaysHidden.Num());
	for (int32 c = 0; c < kGridCells; ++c)
	{
		S += FString::Printf(TEXT("  %d WAREKEEP_TRAY%02d икона %s подпись %s\n"),
			WareIds::kTrayBase[0] + c, c,
			GridUi[c].Icon ? TEXT("есть") : TEXT("НЕТ"), GridUi[c].Label ? TEXT("есть") : TEXT("НЕТ"));
	}
	for (int32 c = 0; c < kBasketCells; ++c)
	{
		S += FString::Printf(TEXT("  %d WAREKEEP_TRAY%02d (КОРЗИНА) икона %s подпись %s\n"),
			WareIds::kBasketBase + c, 30 + c,
			BasketUi[c].Icon ? TEXT("есть") : TEXT("НЕТ"), BasketUi[c].Label ? TEXT("есть") : TEXT("НЕТ"));
	}
	for (int32 k = 0; k < kBags; ++k)
	{
		int32 Icons = 0;
		for (int32 c = 0; c < kWareBagCells; ++c) { if (BagCellUi[k * kWareBagCells + c].Icon) { ++Icons; } }
		S += FString::Printf(
			TEXT("  сумка %d (окно %d, кнопка %d): группа %s крестик %s подпись %s SEL %s икон %d/%d\n"),
			k + 1, WareIds::kBagWindow[k], WareIds::kBagButton[k],
			Bags[k].Root ? TEXT("есть") : TEXT("НЕТ"), Bags[k].Close ? TEXT("есть") : TEXT("НЕТ"),
			Bags[k].Title ? TEXT("есть") : TEXT("НЕТ"), Bags[k].Sel ? TEXT("есть") : TEXT("НЕТ"),
			Icons, kWareBagCells);
	}
	S += TEXT("-- окно 5500 Warehouse_Win --\n");
	Line(TEXT("5501 Warehouse_Top_T"), RcTitle);
	Line(TEXT("5502 Warehouse_BT1"),   RcBtnTake);
	Line(TEXT("5503 Warehouse_BT2"),   RcBtnKeep);
	Line(TEXT("5504 Warehouse_No_B"),  RcBtnNo);
	Line(TEXT("5509 Warehouse_X"),     RcBtnX);
	return S;
}

FString UAsdaWarehouseSubsystem::DumpState() const
{
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	FString S = FString::Printf(
		TEXT("=== СКЛАД: состояние ===\n")
		TEXT("  режим=%d (%s)  категория=%d  прокрутка=%d/%d  ждём ответ=%s  свиток активен=%s\n")
		TEXT("  вместимость=%d занято=%d  деньги=%d  комиссия=%d  своя фракция на карте=%s\n"),
		Mode, Mode == kModeKeep ? TEXT("хранить") : (Mode == kModeTake ? TEXT("забрать") : TEXT("закрыто")),
		Category, ScrollRow, GetMaxScrollRow(),
		bRequestSent ? TEXT("да") : TEXT("нет"), bRecallActive ? TEXT("да") : TEXT("нет"),
		Net ? Net->SelfStoreSlotsMax : -1, Net ? Net->SelfStoreSlotsUsed : -1,
		Inv ? Inv->GetGold() : 0, kFeePerItem * BasketOccupied(),
		IsOwnFactionMap() ? TEXT("да") : TEXT("нет"));
	S += TEXT("  вкладки: ");
	for (int32 t = 0; t < kStoreTabs; ++t)
	{
		S += FString::Printf(TEXT("%d%s%s "), t, bTabLoaded[t] ? TEXT("=загр") : TEXT("=нет"),
			(t > 0 && bBagOpen[t - 1]) ? TEXT(",откр") : TEXT(""));
	}
	S += TEXT("\n");
	for (int32 c = 0; c < kBasketCells; ++c)
	{
		if (Basket[c].IsEmpty()) { continue; }
		S += FString::Printf(TEXT("  корзина[%d] код=%d кол-во=%d контейнер=%d слот=%d\n"),
			c, Basket[c].ItemCode, Basket[c].Amount, Basket[c].ContainerType, Basket[c].SlotIndex);
	}
	int32 Shown = 0;
	for (int32 i = 0; i < kStoreSlots && Shown < 40; ++i)
	{
		if (Store[i].IsEmpty()) { continue; }
		S += FString::Printf(TEXT("  склад[%3d] (вкладка %d, ячейка %2d) код=%d кол-во=%d\n"),
			i, i / kWareBagCells, i % kWareBagCells, Store[i].ItemCode, Store[i].Amount);
		++Shown;
	}
	return S;
}

namespace {
// ═══════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ (проверка без NPC) ════════════════════════════════
	UAsdaWarehouseSubsystem* WareSub(UWorld* W)
	{
		return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaWarehouseSubsystem>() : nullptr;
	}
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GWareOpen(
	TEXT("asda.Warehouse.Open"),
	TEXT("asda.Warehouse.Open <0|1> — открыть окно 4000: 0 «Забрать» (код NPC 30012), 1 «Хранить» (30011)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaWarehouseSubsystem* S = WareSub(W);
		if (!S) { UE_LOG(LogAsdaWare, Warning, TEXT("подсистема недоступна")); return; }
		S->OpenWindow(A.Num() > 0 ? FCString::Atoi(*A[0]) : 0);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GWareClose(
	TEXT("asda.Warehouse.Close"),
	TEXT("asda.Warehouse.Close — закрыть окно 4000 (эталонный storageUIClearTab(this, 0))"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaWarehouseSubsystem* S = WareSub(W)) { S->CloseWindow(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GWareRecall(
	TEXT("asda.Warehouse.Recall"),
	TEXT("asda.Warehouse.Recall <0|1> — показать (1) либо скрыть (0) окно 5500 «свиток возврата склада»"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaWarehouseSubsystem* S = WareSub(W);
		if (!S) { UE_LOG(LogAsdaWare, Warning, TEXT("подсистема недоступна")); return; }
		const bool bOn = (A.Num() == 0) || (FCString::Atoi(*A[0]) != 0);
		if (bOn) { S->OpenRecallPanel(); } else { S->CloseRecallPanel(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GWareTab(
	TEXT("asda.Warehouse.Tab"),
	TEXT("asda.Warehouse.Tab <1..8> <0|1> — открыть/закрыть окно-сумку (эталонные события 67073/67074)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaWarehouseSubsystem* S = WareSub(W);
		if (!S || A.Num() == 0) { return; }
		S->OnBagButtonToggled(FCString::Atoi(*A[0]), A.Num() < 2 || FCString::Atoi(*A[1]) != 0);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GWareDbl(
	TEXT("asda.Warehouse.DoubleClick"),
	TEXT("asda.Warehouse.DoubleClick <idnum> — прогнать двойной щелчок по ячейке ТЕМ ЖЕ путём, что мышь "
	     "(в headless-PIE мышью не кликнуть, а второй путь обработки заводить нельзя)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaWarehouseSubsystem* S = WareSub(W);
		if (!S || A.Num() == 0) { return; }
		S->OnCellDoubleClick(FCString::Atoi(*A[0]));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GWareButton(
	TEXT("asda.Warehouse.Button"),
	TEXT("asda.Warehouse.Button <keep|take> — нажать 4002 «Хранить» либо 4049 «Забрать»"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaWarehouseSubsystem* S = WareSub(W);
		if (!S) { return; }
		if (A.Num() > 0 && A[0].Equals(TEXT("take"), ESearchCase::IgnoreCase)) { S->OnTakeClicked(); }
		else { S->OnKeepClicked(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GWareDump(
	TEXT("asda.Warehouse.Dump"),
	TEXT("asda.Warehouse.Dump — дамп КАЖДОГО контрола обоих окон + состояние модели"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaWarehouseSubsystem* S = WareSub(W);
		if (!S) { UE_LOG(LogAsdaWare, Warning, TEXT("подсистема недоступна")); return; }
		UE_LOG(LogAsdaWare, Log, TEXT("\n%s\n%s"), *S->DumpControls(), *S->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE
