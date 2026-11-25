// Copyright (c) 2026 @sitxovski (Discord)
// Личная лавка, сторона продавца: окно подготовки 22000 и витрина хозяина 23000. Назначение, поток, эталонные адреса,
//   исправленные дефекты — в шапке `Public/UI/AsdaSelfStoreSubsystem.h`. Волна Л1 торговли (29.09.2026),
//   отчёт `Reports/trade/wave_L1.md`.
#include "UI/AsdaSelfStoreSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока

#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaUiHelpers.h"   // ★ U20: AsdaUi::BindButton
#include "UI/AsdaExchangeSubsystem.h"         // ★ пачка 20, жалоба 10: рынок 13000 открыт — лавка не открывается
#include "UI/AsdaTradeStateSubsystem.h"     // признаки 22000/23000 для чужих гейтов (общая точка волны 0)
#include "UI/AsdaJuiWindowHandle.h"         // каркас окна JUI (Ensure + зона рамки)
#include "UI/AsdaWindowSubsystem.h"         // окна, ESC, колесо, нумпад
#include "UI/AsdaNotice.h"                  // нотисы: msgbox 1500, плашка+чат
#include "UI/AsdaIconCellGrid.h"            // иконки предметов (общая точка)
#include "UI/AsdaItemTooltipSubsystem.h"    // подсказки предметов
#include "UI/AsdaTipPanel.h"                // EAsdaTipOwner::SelfStore
#include "UI/AsdaInventorySubsystem.h"      // ячейки, золото, закрытие сумки, записи 57 б
#include "UI/AsdaPremiumShopSubsystem.h"    // гейт 6300: элитный магазин (`helper_getField2075(g_pShopWnd)`)
#include "UI/AsdaChatWindowSubsystem.h"     // мат-фильтр `Chat_MatchBannedWord` (общая точка `IsCursed`)
#include "Npc/AsdaNpcDialogSubsystem.h"     // `g_bInventoryLocked` = открыт разговор с NPC (гейт 6302)
#include "World/AsdaPlayerController.h"     // поза 107/109 (`SendStallStatePose`), «мёртв»
#include "World/AsdaWorldSubsystem.h"       // имя своего персонажа
#include "AsdaNetSubsystem.h"
#include "AsdaSelf.h"                       // charId / userNum / уровень своего персонажа

#include "Blueprint/UserWidget.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"               // журнал 23402 (волна Л2)
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "AsdaSelfStoreSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaSelfStoreSubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaSelfStore);   // ★ R2: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

	ASDA_DEV_CVAR(int32, CVarFixK5K6, TEXT("asda.Trade.StoreFixK5K6"), 1,
		TEXT("Лавка, дефекты К-5/К-6: 1 = суммы окна 22000 и витрины 23000 в int64, все строки видны, лот с кол-во × цена > 2^31−1 "
		     "не отправляется (msgbox 107040 — [ПОРТ]: тот же текст, что дал бы сервер); 0 = как оригинал — int32, сумма > 2 100 000 000 "
		     "обнуляет строку и обрывает перерисовку, отрицательный итог (переполнение) шлёт 6305."),
		ECVF_Default);

	// ── ключи ревизии Л2 (волна Л3) ──
	ASDA_DEV_CVAR(int32, CVarStrayGuard, TEXT("asda.Trade.StoreStrayGuard"), 1,
		TEXT("[ИСПРАВЛЕНИЕ] Лавка, «чужие» пакеты гостя (ревизия Л2, СРЕДН-2): 1 = 6306 код 2 (107031 «Хозяин лавки закрыл ее») — "
		     "только при открытой витрине посетителя или окне подготовки; 6312 — только при открытой витрине посетителя; 6315 — "
		     "только при открытой витрине (своей или чужой). Защита от застрявшего на сервере гостевого слота (аварийные выходы "
		     "`DisConnectUser`): его userNum мог достаться другому игроку. 0 = как оригинал — всё без условий."),
		ECVF_Default);

namespace {
	ASDA_DEV_CVAR(int32, CVarVisitorDeathLeave, TEXT("asda.Trade.StoreVisitorLeaveOnDeath"), 1,
		TEXT("[ПОРТ] Лавка: 1 = посетитель, умерший с открытой чужой витриной, выходит из неё (6305, раз в 4 с, как правило хозяина "
		     "«свой и состояние 6»); 0 = как оригинал — витрина гостя остаётся открытой."),
		ECVF_Default);
}

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════
void UAsdaSelfStoreSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaTradeStateSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	Ticket45 = MakeResetLot45();   // ctor `sub_DD6070`: `slotStructReset(this + 906)`
	GridRef.Init(FIntPoint(-1, -1), kGridCells);
	RowEntry.Init(-1, kMaxLots);
	EditCache.Init(FString(), 2 * kMaxLots);
	CartCellLot.Init(-1, 10);   // волна Л2: ячейки корзины пусты
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		HNet = Net->OnSelfStorePacket.AddUObject(this, &UAsdaSelfStoreSubsystem::OnPacket);
	}
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		// ESC (событие 0x15) у обоих окон шлёт 6305, окно НЕ прячет (прячет ответ 6306): `uiSellWindowMsgProc` case 0x15u
		//   (ui_core.c:65038) и `CShopDlg_OnCommand` `notifyCode == 21` (shop_trade.c:17564). Строки 22000/23000 уже есть в
		//   эталонной ESC-таблице порта (`kEscCloseWindows`).
		WM->RegisterEscCloser(kSellId, FSimpleDelegate::CreateWeakLambda(this, [this]() { if (bSellOpen) { SendClose6305(TEXT("ESC окна 22000 (0x15)")); } }));
		WM->RegisterEscCloser(kShopId, FSimpleDelegate::CreateWeakLambda(this, [this]() { if (bShopOpen) { SendClose6305(TEXT("ESC окна 23000 (0x15)")); } }));
		// Колесо 22199 `Stall_Open_InvenWheel` (0x17): в WBP узла нет (импортёр не строит WheelWin), зона колеса по `.JUI` —
		//   (376,118) 198×164 от корня окна = ровно сетка 22100…22129 ⇒ курсор над любой ячейкой. На вкладке 1 колесо скрыто.
		// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): колесо и на вкладке 1 (сумка), когда сумка не помещается в 30 ячеек сетки.
		WM->RegisterWheelHandler(kSellId, FAsdaUiWheelHandler::CreateWeakLambda(this, [this](int32 Notches) -> bool
		{
			if (!bSellOpen || AsdaSellScrollMax(SourceTab) == 0) { return false; }
			bool bOver = false;
			for (const TObjectPtr<UImage>& I : GridImg) { if (I && AsdaUi::IsCursorOverWidget(I)) { bOver = true; break; } }
			if (!bOver) { return false; }
			OnWheel(Notches);
			return true;
		}));
	}
	UE_LOG(LogAsdaSelfStore, Log, TEXT("[лавка] подсистема продавца поднята (22000/23000, опкоды 6300/6301/6302/6303/6305/6306)"));
}

void UAsdaSelfStoreSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr) { Net->OnSelfStorePacket.Remove(HNet); }
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterEscCloser(kSellId);
		WM->UnregisterEscCloser(kShopId);
		WM->UnregisterWheelHandler(kSellId);
	}
	ResetSellWidget();
	ResetShopWidget();
	Super::Deinitialize();
}

void UAsdaSelfStoreSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// [ПОРТ] Эталон при разрыве закрывает клиент (память original-quits-client-on-disconnect), выход в выбор персонажа
	//   пересоздаёт объекты. Сервер при разрыве сам закрывает лавку (`DisConnectUser` S:111416-111420). Поэтому окна
	//   прячутся БЕЗ пакетов (ни 6305, ни 4024 — соединение уже другое), признаки и списки гаснут.
	const bool bWasAny = bSellOpen || bShopOpen || SellList.Num() > 0 || Lots.Num() > 0;
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (bSellOpen && WM) { WM->SetWindowVisible(kSellId, false); }
	if (bShopOpen && WM) { WM->SetWindowVisible(kShopId, false); }
	bSellOpen = false;
	bShopOpen = false;
	bSpecialShop = false;
	bReopen = false;
	bCanSendOpen = false;
	SellList.Reset();
	Lots.Reset();
	GridRef.Init(FIntPoint(-1, -1), kGridCells);
	RowEntry.Init(-1, kMaxLots);
	PendingItem = FIntPoint(-1, -1);
	PendingPackets.Clear();
	// Волна Л2 — посетитель: корзина, выбор, нумпад 8, «можно спросить 62», гости, журнал 23402. Сервер при разрыве/смене карты
	//   сам снимает гостя (`DisConnectUser` → `CharSelfStoreClose(…, 1)`), 6305 не шлём.
	Cart.Reset();
	SelLot = -1;
	PendingQtyLot = -1;
	bPendingBuy = true;
	GuestCount = 0;
	bLogAutoScroll = true;
	VisitorDeathAt = 0.0;
	CartCellLot.Init(-1, 10);
	LotDbl.Reset();   // [ПОРТ] ревизия 10.10 МЕЛК-2: пара двойного щелчка по лоту не переживает сброс сессии
	if (LogList) { LogList->ClearChildren(); }
	// [ПОРТ] Волна Л4: недописанная строка чата 23403 к новой сессии не относится (эталон при разрыве закрывает клиент).
	if (EdChat) { EdChat->SetText(FText::GetEmpty()); }
	if (UAsdaTradeStateSubsystem* TS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTradeStateSubsystem>() : nullptr)
	{
		TS->SetSellWindowOpen(false, TEXT("сброс сессии (лавка)"));
		TS->SetShopDlgOpen(false, TEXT("сброс сессии (лавка)"));
	}
	TickTip(-1, -1);
	Mouse.Reset();
	if (AsdaIsFullSessionReset(Reason))
	{
		// Другой персонаж: поля объекта-синглтона эталона умерли бы с процессом — премиум, билет, кэш хозяина, название.
		bPremium = false;
		Ticket45 = MakeResetLot45();
		HostCharId = 0;
		HostUserNum = 0;
		StoreTitle.Reset();
		OwnerName.Reset();
		ResetSellWidget();   // название по умолчанию (107061 с именем) ставит разбор окна — у нового персонажа своё имя
		ResetShopWidget();
	}
	if (bWasAny) { JournalAdd(FString::Printf(TEXT("сброс сессии (%s): окна скрыты без пакетов"), AsdaSessionResetReasonName(Reason))); }
}

void UAsdaSelfStoreSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.SelfStore");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	PumpPending();
	// ★ Волна Л3 (реестр Л1-2 ЗАКРЫТ): правило «свой и состояние 6 → 6305 раз в 4 с» переехало туда, где оно живёт у эталона, —
	//   в рендер СВОЕЙ вывески (`Chat_RenderHeadBubble@0xDE10E0`, chat.c:6481-6483 — МИСНЕЙМ): `UAsdaStallSignSubsystem::RenderSigns`.
	//   Вывеска есть ровно пока свой признак «в лавке» (6304 от 6302) = 1; её нет на подготовке 22000 — и проверки там нет.
	// ★ [ПОРТ] Волна Л2: посетитель умер с открытой чужой витриной → 6305 (ключ `asda.Trade.StoreVisitorLeaveOnDeath`). У оригинала
	//   правило «свой и состояние 6 → 6305» живёт в рендере СВОЕЙ вывески, у гостя вывески нет — витрина оставалась открытой, покупка
	//   мёртвым отвергалась сервером (гейт `sub_52BFF0`, 6310 код 0). Порт уводит гостя из лавки, пока не пришёл реген.
	if (IsVisitorShowcaseOpen() && CVarVisitorDeathLeave.GetValueOnGameThread() != 0)
	{
		const UWorld* W = GetWorld();
		const AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
		const double Now = FPlatformTime::Seconds();
		if (PC && PC->IsSelfDead() && Now >= VisitorDeathAt)
		{
			VisitorDeathAt = Now + 4.0;
			SendClose6305(TEXT("[ПОРТ] смерть посетителя при открытой чужой витрине"));
		}
	}
	if (!bSellOpen && !bShopOpen) { return; }
	Mouse.Update();
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		TickTip(-1, -1);
		Mouse.Reset();
		LotDbl.Reset();   // [ПОРТ] ревизия 10.10 МЕЛК-1: модальный нотис между щелчками рвёт пару (как щелчок по другому окну)
		return;
	}
	if (bSellOpen && bSellReady) { TickSellMouse(); }
	if (bShopOpen && bShopReady) { TickShopMouse(); }
}
