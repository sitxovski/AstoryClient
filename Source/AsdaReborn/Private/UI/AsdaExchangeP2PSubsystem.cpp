// Copyright (c) 2026 @sitxovski (Discord)
// Прямой обмен между игроками — каркас `CExchange` (волна О1 торговли, 29.09.2026). Назначение, эталонные адреса,
//   границы волны и исправленные дефекты оригинала — в `Public/UI/AsdaExchangeP2PSubsystem.h`.
//   Разбор: `Reports/trade/A_exchange_client.md`, `C1_exchange_server.md`; итог волны — `Reports/trade/wave_O1.md`.
//   Окно 2008 (волна О2) — `AsdaExchangeP2PWindow.cpp`, итог — `Reports/trade/wave_O2.md`; готовность 5291/5292 и итог
//   5293 (волна О3) — там же, итог — `Reports/trade/wave_O3.md`; итог 5294/5295 (волна О4) — `AsdaExchangeP2PComplete.cpp`,
//   итог — `Reports/trade/wave_O4.md`.
#include "UI/AsdaExchangeP2PSubsystem.h"

#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaTradeStateSubsystem.h"     // +0x2C «окно 2008 открыто» — общая точка волны 0 (эта подсистема — писатель)
#include "UI/AsdaPlayerMenuSubsystem.h"     // меню 2000: `uiDialog_ClearAndHide`
#include "UI/AsdaHotbarSubsystem.h"         // значки уведомлений 4/5 (`statusBarAddIcon`, `MsgNotify_HasType`)
#include "UI/AsdaWindowSubsystem.h"         // TWOBUT 1520 (очередь окон-сообщений), ONEBUT 1500, звуки менеджера 5
#include "UI/AsdaNotice.h"                  // фасад нотисов (цветная строка чата, окно-сообщение по loc)
#include "UI/AsdaInventorySubsystem.h"      // окно рюкзака (`sub_93248D`), закрытие (`sowelWindowClose`)
#include "UI/AsdaPremiumShopSubsystem.h"    // элитный магазин (`helper_getField2075(g_pShopWnd)`)
#include "UI/AsdaEquipShopSubsystem.h"      // магазин снаряжения 6800 (`sub_93B448(g_pShopWnd)` → `shopWin_Close`)
#include "UI/AsdaHairShopSubsystem.h"       // парикмахерская (`CObject_GetField166(g_pHairShopWnd)`)
#include "UI/AsdaBoosterPanelSubsystem.h"   // барабан гачи 10050 (`CObject_GetField254(g_pInventoryWnd)`)
#include "UI/AsdaWarehouseSubsystem.h"      // склад (`CObject_GetField5581(g_pWarehouseWnd)`)
#include "UI/AsdaCraftSubsystem.h"          // окна ремесла 76000/77000 (`tileInteract_isControlVisible` / `isBoundControlVisible`)
#include "UI/AsdaExchangeSubsystem.h"       // АУКЦИОН 13000 (`g_pItemShopWnd`): гейт открытия и отложенное золото 9913
#include "UI/AsdaNpcShopSubsystem.h"        // магазин NPC (`g_pRecvUiHandler2`, vtbl+20)
#include "Npc/AsdaNpcDialogSubsystem.h"     // разговор с NPC (`g_bInventoryLocked`) и `UI_resetAllWindowsAndLeave_5045`
#include "Resources/AsdaSoundSubsystem.h"   // звук 215 заявки
#include "World/AsdaWorldSubsystem.h"       // реестр соседей: имя, актор по userNum, userNum по актору
#include "World/AsdaPlayerController.h"     // выбранная цель (`g_nTargetUserIdx`)
#include "World/AsdaPlayerCharacter.h"      // [ПОРТ] партнёр мёртв (одновременная смерть сторон, О3)
#include "Login/AsdaLoginData.h"            // cp1251 имён из пакетов
#include "AsdaNetSubsystem.h"               // отправители и подписка на приём
#include "AsdaSelf.h"                       // «это я?», мой userNum, мой уровень
#include "AsdaBytes.h"                      // чтение i16/i32 из тел
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): список типа 1 = вся сумка + ячейка золота.
static_assert(UAsdaExchangeP2PSubsystem::kList1Max == UAsdaInventorySubsystem::kShellSlots + 1, "список типа 1 разошёлся с ёмкостью сумки");

DEFINE_LOG_CATEGORY_STATIC(LogAsdaP2P, Log, All);

namespace
{
	// [ОРИГИНАЛ] Цвета строк чата по обработчику (`CChat_AddTokenizedLines(…, цвет, 10000, 0)`).
	const FColor kColDefault(0xFE, 0xD9, 0xF1, 0xFF);    // −75279 = 0xFFFED9F1 — почти все строки обмена
	const FColor kColIncoming(0x78, 0xFF, 0x3C, 0xFF);   // −8847556 = 0xFF78FF3C — 5058/5059 (`Recv_PartyMember`)
	const FColor kColWhite(0xFF, 0xFF, 0xFF, 0xFF);      // −1 — гейты «Принять» 5032/5033/5044/13016 (ui_core.c:57210-57244)

	constexpr double kRequestTimeoutSec = 33.0;   // `CExchange_SendCancelTimeout`: elapsed > 33000.0 мс
	constexpr double kIncomingGaugeSec  = 30.0;   // `uiProgressTickBuffAction`: v3 / 30000.0 * 100.0, исход при > 100 %
	constexpr int32  kSoundIncoming     = 215;    // `csoundPlayName2DStore(g_pSoundMgr, 215, 0)` (network.c:27351/27367)
	constexpr int32  kLevelNeeded       = 24;     // `CharManager_GetLocalPlayer()->level < 0x18` → 97101 (ui_core.c:35208)
	// [ПОРТ] Отсрочка «Отмены» стороны-цели при смерти обеих сторон (ревизия О2 МЕЛК-3): с запасом на пинг и тик сервера.
	constexpr double kDeathGraceSec     = 3.0;

	// ★ [ПОРТ] Ключ исправлений дефектов оригинала (Д1/Д2/Д3/Д5/Д6/Д11/Д12/Д15, список — в шапке заголовка).
	ASDA_DEV_CVAR(int32, CVarP2PFixDefects,
		TEXT("asda.Trade.P2PFixDefects"), 1,
		TEXT("Обмен игрок-игрок: 1 — исправлять дефекты оригинала Д1/Д2/Д3/Д5/Д6/Д11/Д12/Д15 (A_exchange_client.md §12), 0 — как эталон."),
		ECVF_Default);
	// ★ 29.09.2026 (волна О2): временная мера О1 `asda.Trade.P2PNoWindowCancel` СНЯТА — окно 2008 есть (реестр О1-1).
	//   Вместе с ней ушёл и путь, на котором ОБА клиента порта слали 5286 в одном кадре (ревизия О1, СРЕДН-1): других
	//   отложенных отмен в модели нет — закрытие шлёт только та сторона, у которой сработала кнопка/гейт/смерть.

	// Строка 20 б cp1251 из тела (имя персонажа).
	FString ReadName20(const TArray<uint8>& B, int32 Off)
	{
		if (Off < 0 || B.Num() < Off + 1) { return FString(); }
		return FAsdaLoginData::DecodeCp1251(B.GetData() + Off, FMath::Min(20, B.Num() - Off));
	}
}

// ═══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═══════════════════════════════════════════════

void UAsdaExchangeP2PSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaTradeStateSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	SlotAccum.Init(0, kTray);
	InvItemListDialogInit(TEXT("конструктор"));
	RegisterWindowHooks();   // О2: колесо 2051 (зона `.JUI`, узла в WBP нет)
	UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] подсистема CExchange поднята: 5282/5284/5286/5288/5290/5291/5293 ↔ 5283/5285/5287/5289/5292/5294/5295, окно 2008 (О2/О3/О4)"));
}

void UAsdaExchangeP2PSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr) { Net->OnPlayerMenuResult.Remove(HMenuResult); }
	if (UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr) { Hb->OnNoticeIconClicked.Remove(HNoticeClick); }
	UnregisterWindowHooks();
	ResetWindowWidget();
	Super::Deinitialize();
}

void UAsdaExchangeP2PSubsystem::EnsureBindings()
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }
	if (!bNetBound)
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			HMenuResult = Net->OnPlayerMenuResult.AddUObject(this, &UAsdaExchangeP2PSubsystem::OnMenuResult);
			bNetBound = true;
		}
	}
	if (!bHotbarBound)
	{
		if (UAsdaHotbarSubsystem* Hb = GI->GetSubsystem<UAsdaHotbarSubsystem>())
		{
			HNoticeClick = Hb->OnNoticeIconClicked.AddUObject(this, &UAsdaExchangeP2PSubsystem::OnHotbarNoticeClicked);
			bHotbarBound = true;
		}
	}
}

bool UAsdaExchangeP2PSubsystem::FixDefects() const
{
	return CVarP2PFixDefects.GetValueOnGameThread() != 0;
}

int32 UAsdaExchangeP2PSubsystem::MyUserNum() const
{
	return AsdaSelf::GetUserIdxOr(GetGameInstance(), -1);
}

bool UAsdaExchangeP2PSubsystem::IsPartnerDead() const
{
	// Партнёр — сторона 5285 (+0x46/+0x48), не совпадающая с моим userNum; его актор в реестре соседей в позе «мёртв».
	const UGameInstance* GI = GetGameInstance();
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!WS) { return false; }
	const int32 Me = MyUserNum();
	const int32 P = (PartnerNum1 >= 0 && PartnerNum1 != Me) ? PartnerNum1 : ((PartnerNum2 >= 0 && PartnerNum2 != Me) ? PartnerNum2 : -1);
	const AAsdaPlayerCharacter* Ch = (P >= 0) ? Cast<AAsdaPlayerCharacter>(WS->GetPlayerActorByUserNum(P)) : nullptr;
	return Ch && Ch->IsIncapacitated();
}

void UAsdaExchangeP2PSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	UGameInstance* GI = GetGameInstance();
	const bool bHad = bRequestActive || bTimerActive || bIncomingGauge || WindowIndex >= 0 || Pending.Num() > 0 || bWinShown;
	Pending.Clear();   // ответы старого соединения к новой сессии не относятся
	// О2: окно 2008 — спрятать без пакетов (сервер снимает обмен сам), перенос и подсказку погасить, мышь отпустить.
	HideWindow(TEXT("сброс сессии"));
	bDeathCloseDone = false;
	// Значки заявки 4/5 и свои записи вопроса 1520 — молча (ответа игрок не давал; сервер обмен снимет сам).
	if (UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr)
	{
		Hb->RemoveNoticeIcon(4);
		Hb->RemoveNoticeIcon(5);
	}
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		const bool bRedisplay = Reason != EAsdaSessionResetReason::MapChanged;   // смена карты: окно не рисуем посреди травела
		WM->RemoveMsgBoxEntries(5, 5001, TEXT("обмен: сброс сессии"), bRedisplay);
		WM->RemoveMsgBoxEntries(6, 5002, TEXT("обмен: сброс сессии"), bRedisplay);
	}
	InvItemListDialogInit(TEXT("сброс сессии"));
	HotbarNoticeName.Reset();
	IncomingGaugeStartReal = -1.0;
	MenuTargetIdx = -1;
	if (bHad)
	{
		UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] сброс сессии (%s): модель CExchange сброшена молча, значки 4/5 и вопросы 1520 сняты"),
			AsdaSessionResetReasonName(Reason));
	}
}

void UAsdaExchangeP2PSubsystem::Tick(float /*DeltaTime*/)
{
	EnsureBindings();
	DrainPending();
	// Порядок игрового цикла эталона (game_main.c:2055-2062): сначала шкала входящей, затем таймер своей заявки.
	TickIncomingGauge();
	if (bTimerActive && NowReal() - RequestStartReal > kRequestTimeoutSec) { SendCancelTimeout(); }
	// ★ [ПОРТ] Смерть при открытом окне обмена. У клиента эталона ссылок на обмен при смерти нет, сервер без P44 снимает
	//   обмен только при регене (`CharRegenVillage` S:113416) и шлёт 5287 лишь ПАРТНЁРУ — у умершего окно висит, а его
	//   поздняя «Отмена» 5286(idx) у стока может попасть в уже переиспользованное окно. Порт закрывает окно путём кнопки
	//   «Отмена» (5286(idx) + 5041) — один раз на смерть; с P44 (`DeathCancelExchange`) сервер отменит обмен сам, а наш
	//   5286 он отвергнет гейтом участия (без вреда). Ключ `asda.Trade.P2PCloseOnDeath` (1).
	if (UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GetGameInstance()) && CloseOnDeath())
	{
		const UWorld* W = GetWorld();
		const AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
		const bool bDead = PC && PC->IsSelfDead();
		if (bDead && !bDeathCloseDone)
		{
			// ★ Ревизия О2 (МЕЛК-3): обе стороны мертвы одновременно — у порта обе закрыли бы окно, и два 5286 по одному окну
			//   у стока освободили бы его дважды. [ПОРТ] Шлёт инициатор (+0x04 = 1) сразу; цель (+0x04 = 0) ждёт
			//   `kDeathGraceSec`: 5287 от партнёра закроет её окно (`Recv_ExchangeClosed`) без её пакета. Не пришёл (у партнёра
			//   ключ 0 или клиент оригинала) — шлёт сама. Партнёр жив — как раньше, сразу.
			const bool bWaitPartner = (Role == 0) && IsPartnerDead();
			if (!bWaitPartner || (DeathGraceStart >= 0.0 && NowReal() - DeathGraceStart >= kDeathGraceSec))
			{
				bDeathCloseDone = true;
				DeathGraceStart = -1.0;
				CloseByCancelButton(bWaitPartner ? TEXT("[ПОРТ] смерть обеих сторон: 5287 от инициатора не пришёл — «Отмена» цели")
					: TEXT("[ПОРТ] смерть при открытом окне обмена"));
			}
			else if (DeathGraceStart < 0.0)
			{
				DeathGraceStart = NowReal();
				UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] [ПОРТ] смерть обеих сторон: я цель — жду 5287 от инициатора %.0f с, свой 5286 не шлю"), kDeathGraceSec);
			}
		}
		if (!bDead) { bDeathCloseDone = false; DeathGraceStart = -1.0; }
	}
	else if (!UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GetGameInstance()))
	{
		bDeathCloseDone = false;
		DeathGraceStart = -1.0;
	}
	if (bWinShown && bWinReady) { TickWindowMouse(); }
}

// ═══════════════════════════════ ПРИЁМ ════════════════════════════════════════════════════════

void UAsdaExchangeP2PSubsystem::OnMenuResult(int32 Opcode, const TArray<uint8>& Body)
{
	// Только копим (закон no-reentrant-net-callbacks): разбор закрывает окна и ставит модалки — это делает `Tick`.
	if (Opcode != 5283 && Opcode != 5285 && Opcode != 5287 && Opcode != 5289 && Opcode != 5292 && Opcode != 5294 && Opcode != 5295) { return; }
	FPendingPkt P;
	P.Opcode = Opcode;
	P.Body = Body;
	Pending.Push(MoveTemp(P));   // ★ U7: TAsdaNetQueue
}

void UAsdaExchangeP2PSubsystem::DrainPending()
{
	// разбор может поставить новые (самотест) — берём снимок (★ U7 (В1 дедупа №2): общая очередь `TAsdaNetQueue` (замок + Drain) вместо своего массива).
	TArray<FPendingPkt> Batch;
	if (!Pending.Drain(Batch)) { return; }
	for (const FPendingPkt& P : Batch)
	{
		switch (P.Opcode)
		{
		case 5283: Recv5283(P.Body); break;
		case 5285: Recv5285(P.Body); break;
		case 5287:
			// Диспетчер эталона (network.c:1457-1459): `if (!dword_2BF9638) Recv_Exchange_Closed(…)` — «закрыл я» (+0x08).
			if (bClosedByMe)
			{
				UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] 5287: закрытие отправлено мной (+0x08=1) — пакет игнорируется (network.c:1458)"));
				break;
			}
			Recv5287(P.Body);
			break;
		case 5289: Recv5289(P.Body); break;   // О2: `Recv_Exchange_SlotUpdate` (network.c:1462, без гейтов)
		case 5292: Recv5292(P.Body); break;   // О3: `Recv_Exchange_LockState` (network.c:1463, без гейтов)
		case 5294: Recv5294(P.Body); break;   // О4: `Recv_Exchange_Complete_Full` (network.c:1468, без гейтов)
		case 5295: Recv5295(P.Body); break;   // О4: `Recv_Exchange_Complete` (network.c:1471, без гейтов)
		default: break;
		}
	}
}

// `Recv_PartyMember(g_pQuickSlotBar, 7, pkt)` → a2 = 4 (тип 0) / 5 (тип 1) (network.c:27302-27377).
void UAsdaExchangeP2PSubsystem::Recv5283(const TArray<uint8>& Body)
{
	if (Body.Num() < 24)
	{
		UE_LOG(LogAsdaP2P, Warning, TEXT("[обмен] 5283: тело %d б < 24 — пропуск"), Body.Num());
		return;
	}
	UGameInstance* GI = GetGameInstance();
	const int32 NoticeType = Body[23] ? 5 : 4;   // `HIBYTE(memberInfo[5])` — байт @23 «тип»
	UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
	const bool bIconHangs = Hb && (Hb->HasNoticeIcon(4) || Hb->HasNoticeIcon(5));
	const bool bWindowOpen = UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI);
	// ★★★ ПАЧКА 20, жалоба 10 — [ИСПРАВЛЕНИЕ]: пока открыт рынок 13000, входящая заявка отклоняется ТЕМ ЖЕ штатным
	//   авто-ответом «занят» 5284/3, что и при открытом окне 2008 (сервер передаёт его инициатору — протокол не меняется).
	//   В оригинале заявка при открытом аукционе показывалась значком, а принять её было нельзя (гейт открытия молчит).
	//   Ключ `asda.Exchange.ExclusiveWindows` 0 — как в оригинале.
	const bool bMarketOpen = UAsdaExchangeSubsystem::IsMarketWindowOpen(GI);
	// case 4/5: `MsgNotify_HasType(4) || MsgNotify_HasType(5) || g_bPartyInviteFlag == 1` → `CExchange_SendRequestAck` (5284/3).
	if (bIconHangs || bWindowOpen || bMarketOpen)
	{
		const bool bSent = SendRequestAck(Body);
		UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] 5283 (код %d, от userNum=%d): %s — авто-ответ 5284/3 «занят» %s (network.c:27344)"),
			(int32)Body[0], (int32)AsdaBytes::RdI16(Body.GetData() + 1),
			bWindowOpen ? TEXT("при открытом окне обмена 2008") : bMarketOpen ? TEXT("при открытом рынке 13000 (пачка 20)") : TEXT("значок заявки 4/5 уже висит"),
			bSent ? TEXT("отправлен") : TEXT("не отправлен (эталонные условия: код 1, не эхо, userNum ≥ 0)"));
		return;
	}
	// `exchangeCheckSlotItemMatch` — эхо своей заявки (+0x3C == +0x3E): пропуск.
	if (CheckSlotItemMatch(Body))
	{
		UE_LOG(LogAsdaP2P, Verbose, TEXT("[обмен] 5283: +0x3C == +0x3E (эхо своей заявки) — пропуск (exchangeCheckSlotItemMatch)"));
		return;
	}
	if (!TradeSwapOnResult(Body)) { return; }
	// Заявка показана: значок 4/5, звук 215, чат 5058/5059 цветом 0xFF78FF3C.
	if (Hb) { Hb->AddNoticeIcon(NoticeType); }
	if (UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr) { Snd->PlaySound2DById(kSoundIncoming); }
	Notice(NoticeType == 5 ? 5059 : 5058, kColIncoming);
	UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] 5283: заявка от «%s» (userNum=%d, тип %d) — значок %d, звук 215, шкала 30 с пошла"),
		*InitiatorName, InitiatorNum, NoticeType - 4, NoticeType);
}

bool UAsdaExchangeP2PSubsystem::CheckSlotItemMatch(const TArray<uint8>& Body)
{
	// [ОРИГИНАЛ] shop_trade.c:10388-10405: при коде 1 пишет +0x3C = мой userNum, +0x3E = @1; возврат +0x3C == +0x3E.
	if (Body[0] == 1)
	{
		MyNum = MyUserNum();
		InitiatorNum = AsdaBytes::RdI16(Body.GetData() + 1);
	}
	return MyNum == InitiatorNum;
}

bool UAsdaExchangeP2PSubsystem::TradeSwapOnResult(const TArray<uint8>& Body)
{
	const int32 Code = (int32)Body[0];
	if (Code == 1)
	{
		MyNum = MyUserNum();
		InitiatorNum = AsdaBytes::RdI16(Body.GetData() + 1);
		ExchangeType = (int32)Body[23];
		InitiatorName = ReadName20(Body, 3);
		if (MyNum == InitiatorNum) { PartnerNum1 = MyNum; PartnerNum2 = InitiatorNum; Role = 1; }
		else { PartnerNum1 = InitiatorNum; PartnerNum2 = MyNum; Role = 0; }
	}
	// Общий хвост веток отказа (network.c:14113-14258): снять заявку, таймер, меню.
	auto Refuse = [this](int32 Loc)
	{
		Notice(Loc, kColDefault);
		bRequestActive = 0; bAcceptSent = 0; bTimerActive = 0; RequestStartReal = 0.0;
		DialogClearAndHide();
	};
	switch (Code)
	{
	case 0:   // 5007 «Обмен прошел неудачно» + сброс сторон (network.c:14113-14131)
		Notice(5007, kColDefault);
		MyNum = -1; InitiatorNum = -1; ExchangeType = -1; PartnerNum1 = -1; PartnerNum2 = -1; MenuTargetNum = -1;
		Role = 1; bRequestActive = 0; bAcceptSent = 0;
		DialogClearAndHide();
		InitiatorName.Reset();
		bTimerActive = 0; RequestStartReal = 0.0;
		return false;
	case 1:
		if (Role == 1) { bClosedByMe = 0; return true; }   // инициатор (достижимо только мимо фильтра эха)
		// Цель: +0x14 = 1, +0x1C = 1 (шкала 30 с — старт в ПЕРВОМ ТИКЕ после признака, см. `TickIncomingGauge`), имя в хотбар, 5049/5048.
		bRequestActive = 1;
		// ★ Ревизия О1 (МЕЛК-2): старт шкалы живёт у менеджера сообщений (`gap_32D4[8]`) и сбрасывается только исходом шкалы
		//   (ui_core.c:58277) или закрытием вопроса (game_main.c:9810). Вторая заявка, пришедшая при уже идущей шкале (вопрос
		//   открыт кликом по значку), шкалу НЕ перезапускает — как эталон.
		if (!bIncomingGauge) { IncomingGaugeStartReal = -1.0; }
		bIncomingGauge = 1;
		HotbarNoticeName = InitiatorName;   // `storeStringFieldByType(g_pQuickSlotBar, 7, имя)`
		Notice(Body[23] ? 5049 : 5048, kColDefault);
		return true;
	case 2:   // 5008 «Игрок не хочет торговать» + сброс цели (network.c:14151-14163)
		Notice(5008, kColDefault);
		MyNum = -1; InitiatorNum = -1; MenuTargetNum = -1; InitiatorName.Reset();
		bRequestActive = 0; bAcceptSent = 0; bTimerActive = 0; RequestStartReal = 0.0;
		DialogClearAndHide();
		return false;
	case 3:   // 5009 (network.c:14164-14173)
		Notice(5009, kColDefault);
		InitiatorName.Reset();
		bRequestActive = 0; bAcceptSent = 0; bTimerActive = 0; RequestStartReal = 0.0;
		DialogClearAndHide();
		return false;
	case 4:  Refuse(5010); return false;   // игрок уже занят обменом
	case 5:  Refuse(5011); return false;   // ты уже ведёшь обмен
	case 6:  Refuse(6033); return false;   // бой (я)
	case 7:  Refuse(6032); return false;   // бой (вторая сторона)
	case 8:  Refuse(5051); return false;   // убит/обездвижен (я)
	case 9:  Refuse(5052); return false;   // убит/обездвижен (он)
	case 10: Refuse(5062); return false;   // не в своём теле (я)
	case 11: Refuse(5063); return false;   // не в своём теле (он)
	case 12: Refuse(5064); return false;   // уровень низок (я)
	case 13: Refuse(5065); return false;   // уровень низок (он)
	default:
		UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] 5283: код %d — ветки у эталона нет (default: return 0)"), Code);
		return false;
	}
}

// `Recv_ExchangeRequestResult@0xC84160` (network.c:14263-14368).
void UAsdaExchangeP2PSubsystem::Recv5285(const TArray<uint8>& Body)
{
	if (Body.Num() < 50)
	{
		UE_LOG(LogAsdaP2P, Warning, TEXT("[обмен] 5285: тело %d б < 50 — пропуск"), Body.Num());
		return;
	}
	UGameInstance* GI = GetGameInstance();
	const int32 Code = (int32)Body[0];
	const int32 Type = (int32)Body[1];
	// Таймер моей заявки стоп — ПРИ ЛЮБОМ коде (network.c:14276-14277).
	bTimerActive = 0;
	RequestStartReal = 0.0;
	auto ReadSides = [this, &Body, Type]()
	{
		PartnerNum1 = AsdaBytes::RdI16(Body.GetData() + 6);
		SideName1 = ReadName20(Body, 8);
		PartnerNum2 = AsdaBytes::RdI16(Body.GetData() + 28);
		SideName2 = ReadName20(Body, 30);
		WindowIndex = AsdaBytes::RdI32(Body.GetData() + 2);
		ExchangeType = Type;
	};
	auto Declined = [this]()   // LABEL_3: 20091 «[ss1] отклоняет предложение обмена» ([ss1] = +0x4C8)
	{
		Notice(20091, kColDefault, MenuTargetName);
		InvItemListDialogInit(TEXT("5285 код 0"));
		DialogClearAndHide();
	};
	if (Role == 1)
	{
		switch (Code)
		{
		case 0: Declined(); return;
		case 1:
		{
			// Инициатор: СНАЧАЛА закрыть магазины, рюкзак, парикмахерскую, разговор NPC и магазин NPC (network.c:14286-14300).
			if (UAsdaEquipShopSubsystem* Eq = GI ? GI->GetSubsystem<UAsdaEquipShopSubsystem>() : nullptr) { if (Eq->IsOpen()) { Eq->CloseWindow(); } }
			if (UAsdaPremiumShopSubsystem* Ps = GI ? GI->GetSubsystem<UAsdaPremiumShopSubsystem>() : nullptr) { if (Ps->IsOpen()) { Ps->CloseWindow(); } }
			if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr) { if (Inv->IsWindowOpen()) { Inv->CloseWindow(); } }
			if (UAsdaHairShopSubsystem* Hs = GI ? GI->GetSubsystem<UAsdaHairShopSubsystem>() : nullptr) { if (Hs->IsSessionOpen()) { Hs->CloseWindow(); } }
			if (UAsdaNpcDialogSubsystem* Dlg = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr)
			{
				if (Dlg->IsDialogOpen()) { Dlg->ResetAllWindowsAndLeave(TEXT("обмен 5285 код 1 (UI_resetAllWindowsAndLeave_5045)")); }
			}
			if (UAsdaNpcShopSubsystem* Ns = GI ? GI->GetSubsystem<UAsdaNpcShopSubsystem>() : nullptr) { if (Ns->IsOpen()) { Ns->CloseShop(); } }
			ReadSides();
			break;
		}
		case 2:
			Notice(5006, kColDefault);
			InvItemListDialogInit(TEXT("5285 код 2"));
			DialogClearAndHide();
			return;
		case 3:
			Notice(5074, kColDefault);
			InvItemListDialogInit(TEXT("5285 код 3"));
			DialogClearAndHide();
			return;
		default:
			UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] 5285 (инициатор): код %d — ветки у эталона нет"), Code);
			return;
		}
	}
	else
	{
		if (Code == 0) { Declined(); return; }
		if (Code == 2)
		{
			Notice(5036, kColDefault);
			InvItemListDialogInit(TEXT("5285 код 2 (цель)"));
			DialogClearAndHide();
			return;
		}
		if (Code != 1)
		{
			UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] 5285 (цель): код %d — эталон молчит (network.c:14352)"), Code);
			return;
		}
		ReadSides();
	}
	// ── Общее открытие (код 1, network.c:14334-14347) ──
	CloseQuestionBox(Type, TEXT("5285 код 1"));   // `CGameUI_CloseWindow(g_pItemTooltipUI, тип ? 6 : 5)` — звук 5 (W6-4)
	DialogClearAndHide();
	bRequestActive = 0; bAcceptSent = 0; bIncomingGauge = 0;
	IncomingGaugeStartReal = -1.0;
	// network.c:14344-14345: `uiSlotIconsReset` + `invListFilterConsumablesRefresh(тип)` — сетки окна 2008 (О2). Виджет у
	//   эталона живёт с начала игры; у порта он поднимается лениво — здесь, до перерисовок.
	EnsureWindow();
	SlotIconsReset();
	BuildSourceList(ExchangeType);
	UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] 5285 код 1: окно сервера #%d, тип %d, стороны «%s»(%d) / «%s»(%d), роль %d, кандидатов %d"),
		WindowIndex, ExchangeType, *SideName1, PartnerNum1, *SideName2, PartnerNum2, Role, ListCount);
	OpenWithGuards();
}

// `Recv_ExchangeClosed@0xC86BA0` (network.c:15077-15099).
void UAsdaExchangeP2PSubsystem::Recv5287(const TArray<uint8>& /*Body*/)
{
	InvItemListDialogInit(TEXT("5287"));
	InventoryDialogReset(TEXT("5287"));
	Notice(5040, kColDefault);   // «Вторая сторона прекратила обмен.»
	// Хвост: `sub_92282B(g_pItemShopWnd) > 0` → вывод отложенного золота 9913 (уже выведено в `uiInventoryDialogReset`).
	if (UAsdaExchangeSubsystem* Ex = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaExchangeSubsystem>() : nullptr)
	{
		Ex->FlushDeferredSaleGold(TEXT("5287 обмен закрыт партнёром (Recv_ExchangeClosed)"));
	}
}

// ═══════════════════════════════ ОТПРАВИТЕЛИ ══════════════════════════════════════════════════

bool UAsdaExchangeP2PSubsystem::SendRequest(bool bPersonal, const TCHAR* Source)
{
	UGameInstance* GI = GetGameInstance();
	// `if (ownPlayerSerial != myUserIndex)` — самообмен: молча ничего (shop_trade.c:7905).
	if (AsdaSelf::IsSelfUserIdx(GI, MenuTargetIdx))
	{
		UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] %s: цель == я (+0x2700) — молча"), Source);
		return false;
	}
	if (bRequestActive)
	{
		Notice(5054, kColDefault);   // «Предложение обмена уже сделано…»
		return false;
	}
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	// `sub_C7B480`: +0x04 = 1, +0x08 = 0, +0x0C = 1; +0x3C = мой userNum.
	Role = 1; bClosedByMe = 0; bLockOff = 1;
	MyNum = MyUserNum();
	if (!Net || !Net->SendExchangeReq5282(MenuTargetNum, bPersonal))
	{
		// [ПОРТ] В эталоне отправка не проваливается; у порта нет связи/гейт отказал — защёлку не ставим (иначе 33 с тупика
		//   и 5286 в мёртвый сокет), 20090 не печатаем (заявка не ушла).
		UE_LOG(LogAsdaP2P, Warning, TEXT("[обмен] %s: 5282 (цель userNum=%d, тип %d) НЕ отправлен — защёлка не ставится"),
			Source, MenuTargetNum, bPersonal ? 1 : 0);
		return false;
	}
	Notice(20090, kColDefault, MenuTargetName);   // «Ты предлагаешь обмен игроку [ss1]» ([ss1] = +0x4C8)
	bRequestActive = 1;
	bTimerActive = 1;
	RequestStartReal = NowReal();
	UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] %s: 5282 → «%s» (userNum=%d, тип %d); таймер 33 с (5286(−2) → userNum=%d)"),
		Source, *MenuTargetName, MenuTargetNum, bPersonal ? 1 : 0, TimeoutTargetNum);
	return true;
}

void UAsdaExchangeP2PSubsystem::SendCancelTimeout()
{
	// shop_trade.c:7730-7866: снять заявку, 5286(−2, +0x2708), сброс полей, чат 5053.
	bRequestActive = 0; bTimerActive = 0; RequestStartReal = 0.0;
	const int32 To = TimeoutTargetNum;
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bSent = Net && Net->SendExchangeCancel5286(To);
	Role = 1; bClosedByMe = 1; bLockOff = 1;
	MyNum = -1; InitiatorNum = -1; WindowIndex = -1; ExchangeType = -1; PartnerNum1 = -1; PartnerNum2 = -1;
	TimeoutTargetNum = -1; bMyLock = 0; MenuTargetNum = -1;
	InitiatorName.Reset(); SideName1.Reset(); SideName2.Reset();
	Notice(5053, kColDefault);
	UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] заявка без ответа %.0f с — 5286(−2, userNum=%d) %s, сброс (CExchange_SendCancelTimeout)"),
		kRequestTimeoutSec, To, bSent ? TEXT("отправлен") : TEXT("НЕ отправлен"));
}

bool UAsdaExchangeP2PSubsystem::SendWindowReady()
{
	// shop_trade.c:8222-8284: только если «ответ ещё не слали» (+0x18 == 0) и я цель (+0x04 == 0), userNum инициатора ≥ 0.
	if (bAcceptSent || Role != 0 || InitiatorNum < 0)
	{
		UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] 5284/1 не отправлен: +0x18=%d роль=%d +0x3E=%d (эталонные условия)"), bAcceptSent, Role, InitiatorNum);
		return false;
	}
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bOk = Net && Net->SendExchangeAnswer5284(1, InitiatorNum, ExchangeType != 0);
	if (bOk) { bAcceptSent = 1; }
	return bOk;
}

bool UAsdaExchangeP2PSubsystem::SendWindowCancel()
{
	// shop_trade.c:8330-8410: если +0x3E ≥ 0 — 5284/0, затем +0x14 = +0x18 = +0x1C = 0.
	if (InitiatorNum < 0) { return false; }
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bOk = Net && Net->SendExchangeAnswer5284(0, InitiatorNum, ExchangeType != 0);
	bRequestActive = 0; bAcceptSent = 0; bIncomingGauge = 0;
	IncomingGaugeStartReal = -1.0;
	return bOk;
}

bool UAsdaExchangeP2PSubsystem::SendRequestAck(const TArray<uint8>& Body)
{
	// shop_trade.c:8485-8640: код 1 → mySlot = мой userNum, reqSlot = @1; если разные и reqSlot ≥ 0 — 5284/3 с типом @23.
	int32 Mine = -1, Req = -1;
	if (Body[0] == 1) { Mine = MyUserNum(); Req = AsdaBytes::RdI16(Body.GetData() + 1); }
	if (Mine == Req || Req < 0) { return false; }
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bOk = Net && Net->SendExchangeAnswer5284(3, Req, Body[23] != 0);
	if (FixDefects())
	{
		// [ПОРТ, Д15] Эталон здесь пишет +0x14 = +0x18 = +0x1C = 0 — поля ПЕРВОЙ (висящей) заявки: её шкала встаёт, значок
		//   живёт вечно. Отклоняемая вторая заявка этих полей не касается — порт их не трогает.
		return bOk;
	}
	bRequestActive = 0; bAcceptSent = 0; bIncomingGauge = 0;
	IncomingGaugeStartReal = -1.0;
	return bOk;
}

bool UAsdaExchangeP2PSubsystem::SendClose()
{
	// shop_trade.c:10240-10376: 5286(windowIndex, 0), затем +0x04 = +0x08 = +0x0C = 1 и сброс сторон/лотков/имён.
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const int32 Idx = WindowIndex;
	const bool bOk = Net && Net->SendExchangeClose5286(Idx);
	Role = 1; bClosedByMe = 1; bLockOff = 1;
	MyNum = -1; InitiatorNum = -1; WindowIndex = -1; ExchangeType = -1; PartnerNum1 = -1; PartnerNum2 = -1;
	bMyLock = 0; MenuTargetNum = -1;
	InitiatorName.Reset(); SideName1.Reset(); SideName2.Reset();
	UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] 5286(окно #%d) %s (CExchange_SendClose)"), Idx, bOk ? TEXT("отправлен") : TEXT("НЕ отправлен"));
	return bOk;
}

// ═══════════════════════════════ ВХОДЫ ════════════════════════════════════════════════════════

bool UAsdaExchangeP2PSubsystem::OnMenuCommand(bool bPersonal, int32 TargetUserNum, int32 TargetUserIdx, const FString& TargetName)
{
	// Цель меню: +0x4C6/+0x4C8 пишет ПКМ-пик (`Input_PickEntityAltClick`), +0x2700 — ПКМ-нажатие.
	MenuTargetNum = TargetUserNum;
	MenuTargetName = TargetName;
	MenuTargetIdx = TargetUserIdx;
	const int32 Gate = MenuGateLoc();
	if (Gate != 0)
	{
		// LABEL_39…LABEL_64: `invItemListDialogInit` + `uiDialog_ClearAndHide` + нотис (крафт — молча).
		InvItemListDialogInit(TEXT("гейт меню 2004/2005"));
		if (Gate > 0) { Notice(Gate, kColDefault); }
		UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] меню %s: гейт %d (shop_trade.c:6617-6717)"), bPersonal ? TEXT("2005") : TEXT("2004"), Gate);
		return true;
	}
	if (UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GetGameInstance()))
	{
		Notice(5042, kColDefault);   // «Открыто окно обмена…»; меню НЕ скрывается (return до `uiDialog_ClearAndHide`)
		UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] меню %s: окно обмена 2008 открыто — 5042, меню остаётся (shop_trade.c:6631)"),
			bPersonal ? TEXT("2005") : TEXT("2004"));
		return false;
	}
	// [ПОРТ, Д11] +0x2708 пишет ПКМ (`NoteMenuTargetForTimeout`); если его нет (вход не мышью) — цель меню.
	if (FixDefects() && TimeoutTargetNum < 0 && !bTimerActive) { TimeoutTargetNum = MenuTargetNum; }
	SendRequest(bPersonal, bPersonal ? TEXT("меню 2005 (CExchange_SendAcceptRequest, тип 1)") : TEXT("меню 2004 (CExchange_SendRequest, тип 0)"));
	return true;
}

bool UAsdaExchangeP2PSubsystem::SendRequestFromSelectedTarget(bool bPersonal)
{
	UGameInstance* GI = GetGameInstance();
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	const AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	// `if (!g_nInputControlState)` — цель ПЕРСОНАЖ (не моб). У порта: выбранный актор есть в реестре игроков.
	AActor* Tgt = PC ? PC->TargetActor.Get() : nullptr;
	const int32 U = (Tgt && WS) ? WS->FindUserIdxByActor(Tgt) : -1;
	if (U < 0 || !WS || !WS->GetPlayerActorByUserNum(U))
	{
		UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] %s: выбранной цели-персонажа нет — молча (ui_core.c:32168)"), bPersonal ? TEXT("203") : TEXT("202"));
		return false;
	}
	// `!targetChar->effectSlots[112].field_40` — не я.
	if (AsdaSelf::IsSelfUserIdx(GI, U)) { return false; }
	// `g_nExchangeTargetUserIdx = g_nTargetUserIdx; g_nSelectedTargetUserIdx = LOWORD(…); strcpy(g_szSelectedTargetName, имя)`.
	MenuTargetIdx = U;
	MenuTargetNum = U;
	FString Name; int32 Cls = 0, Lvl = 0, Clan = -1;
	WS->GetNetPlayerInfo(U, Name, Cls, Lvl, Clan);
	MenuTargetName = Name;
	// [ПОРТ, Д11] эталон `+0x2708` здесь не пишет → таймаут 5286(−2) уходил бы с чужим/−1 userNum.
	if (FixDefects() && !bTimerActive) { TimeoutTargetNum = U; }
	const bool bOk = SendRequest(bPersonal, bPersonal ? TEXT("действие 203 (CExchange_SendAcceptRequest)") : TEXT("действие 202 (CExchange_SendRequest)"));
	// `sub_931B23(g_pQuickSlotBar)` = `sub_C54A20`: снять 13 защёлок антидребезга хотбара (`CHotbar+0x5FA0`).
	if (UAsdaHotbarSubsystem* Hb = GI->GetSubsystem<UAsdaHotbarSubsystem>())
	{
		for (int32 i = 0; i < 13; ++i) { Hb->ReleaseSlotByColumn(i); }
	}
	return bOk;
}

void UAsdaExchangeP2PSubsystem::NoteMenuTargetForTimeout(int32 TargetUserNum)
{
	// game_main.c:9423-9424: `if (!g_bExchangeTimerActive) set_o9C2(g_pExchangeWnd, p->userNum)`.
	if (!bTimerActive) { TimeoutTargetNum = TargetUserNum; }
}

void UAsdaExchangeP2PSubsystem::CloseByCancelButton(const TCHAR* Why)
{
	// A §5.6: `CExchange_SendClose` → `invItemListDialogInit` → `uiInventoryDialogReset` → чат 5041 «Обмен прерван.»
	SendClose();
	InvItemListDialogInit(Why);
	InventoryDialogReset(Why);
	Notice(5041, kColDefault);
	UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] отмена (2012/2050): %s"), Why);
}

bool UAsdaExchangeP2PSubsystem::OnCloseAllWindowsOnLeave(const TCHAR* Why)
{
	if (!UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GetGameInstance())) { return false; }
	if (FixDefects())
	{
		// [ПОРТ, Д1] Эталон шлёт 5284/0 (отказ ЗАЯВКИ): у инициатора сервер отвечает «RequestUserNum == UserNum», у цели
		//   окно сервера не освобождается. Порт закрывает окно штатно — 5286(windowIndex) (индекс неизвестен — не шлёт).
		if (WindowIndex >= 0) { SendClose(); }
	}
	else
	{
		SendWindowCancel();   // [ОРИГИНАЛ] ui_core.c:54728 `CExchange_SendWindowCancel`
	}
	InventoryDialogReset(Why);   // ui_core.c:54729 `uiInventoryDialogReset`
	UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] закрыть всё при уходе (%s): окно обмена закрыто (%s)"), Why,
		FixDefects() ? TEXT("5286, дефект Д1 исправлен") : TEXT("5284/0, как эталон"));
	return true;
}

// ═══════════════════════════════ ОКНА И СБРОСЫ ════════════════════════════════════════════════

void UAsdaExchangeP2PSubsystem::InvItemListDialogInit(const TCHAR* Why)
{
	// item_inventory.c:23583-23636 (`set_o134` = `invItemListDialogInit@0xC78AB0`).
	Role = 1; bClosedByMe = 0; bLockOff = 1;
	// О2: списки, лотки, выбор, прокрутка, накопитель (filler[360…10308]) и скрытие 2011 / рамки 2047 (filler[10084]/[10228]).
	TrayCopy.Init(0, kTray * kCopyRec);        // memset(+360, 0, 0x140)
	MyTray.Init(0, kTray * kRec);              // memset(+680, 0, 0x11D)
	PartnerTray.Init(0, kTray * kPartnerRec);  // memset(+965, 0, 0xFF)
	List1.Init(0, kList1Max * kRec);           // memset(+1301, 0, 0x6E7)
	List0.Init(0, kList0Max * kRec);           // memset(+3068, 0, 0xD5C) (+6488 — копия контейнера 2 для 5295, О4)
	ScrollRow = 0; CachedPage = -2; SelListCell = -1; SelTrayCell = -1; ListCount = -1; SelCode = -1; SelSlotIndex = -1;
	SelSource = -1; SelectedQty = 0; ActiveSlotIndex = -1;
	SlotAccum.Init(0, kTray);                  // memset(+9956, 0, 0x14)
	// [ПОРТ, Д18] очередь ожидающих 5289 — принадлежность окна: закрытое окно ответов уже не ждёт (у эталона очереди нет).
	AddQueue.Reset(); ReplySrcIdx = -1; ReplyKind = 0;
	DeathGraceStart = -1.0;                    // [ПОРТ] отсрочка «Отмены» при смерти обеих сторон — у закрытого окна её нет
	SetOkButtonShown(false);                   // `CControl_SetVisible(+10084 = 2011, 0)`
	if (TraySel) { TraySel->SetVisibility(ESlateVisibility::Collapsed); }   // `CControl_SetVisible(+10228 = 2047, 0)`
	bCellTaken = 0; bRequestActive = 0; bAcceptSent = 0; bIncomingGauge = 0; bDragging = 0; bCompleteSent = 0;
	bStackAdd = 0; bTimerActive = 0;
	// +0x2C = 0 (filler[44]) — признак окна; +0x34 = 0 (filler[52]) — признак меню, само меню скрывают вызывающие.
	if (UAsdaTradeStateSubsystem* TS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTradeStateSubsystem>() : nullptr)
	{
		TS->SetExchangeWindowOpen(false, TEXT("invItemListDialogInit"));
	}
	MyNum = -1; InitiatorNum = -1; WindowIndex = -1; ExchangeType = -1; PartnerNum1 = -1; PartnerNum2 = -1;
	bMyLock = 0; MenuTargetNum = -1; MenuTargetName.Reset();
	ReadyCounter = 0;
	bLockCounted = false;     // [ПОРТ] инкремент своего взвода — вместе со счётчиком (ревизия О3 МЕЛК-3)
	RequestStartReal = 0.0;   // filler[9988] = +0x2704
	TimeoutTargetNum = -1;    // filler[9992] = +0x2708
	InitiatorName.Reset(); SideName1.Reset(); SideName2.Reset();
	IncomingGaugeStartReal = -1.0;   // [ПОРТ] старт шкалы живёт у менеджера сообщений эталона; при +0x1C = 0 он не читается
	UE_LOG(LogAsdaP2P, Verbose, TEXT("[обмен] invItemListDialogInit (%s)"), Why);
}

void UAsdaExchangeP2PSubsystem::InventoryDialogReset(const TCHAR* Why)
{
	// ui_core.c:35228-35248: +0x2C = 0, `uiSlotIconsReset`, скрыть корень 2008, `invItemListDialogInit`, подсказку погасить,
	//   вывод отложенного золота 9913.
	if (UAsdaTradeStateSubsystem* TS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTradeStateSubsystem>() : nullptr)
	{
		TS->SetExchangeWindowOpen(false, TEXT("uiInventoryDialogReset"));
	}
	SlotIconsReset();
	HideWindow(Why);   // `CControl_SetVisible(pSlotCtrl1 = 2008, 0)`; внутри же `CControl_KillFocus(&g_ItemTooltip)` при `tooltipShown`
	InvItemListDialogInit(Why);
	if (UAsdaExchangeSubsystem* Ex = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaExchangeSubsystem>() : nullptr)
	{
		Ex->FlushDeferredSaleGold(TEXT("закрытие окна обмена (uiInventoryDialogReset)"));   // Т0-2
	}
}

void UAsdaExchangeP2PSubsystem::DialogClearAndHide()
{
	if (UAsdaPlayerMenuSubsystem* PM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPlayerMenuSubsystem>() : nullptr)
	{
		if (PM->IsMenuOpen()) { PM->CloseMenu(); }
	}
}

void UAsdaExchangeP2PSubsystem::OpenWithGuards()
{
	UGameInstance* GI = GetGameInstance();
	const int32 Gate = OpenGateLoc();
	if (Gate != 0)
	{
		if (FixDefects() && WindowIndex >= 0)
		{
			// [ПОРТ, Д2] Эталон окно не открывает и серверу НИЧЕГО не шлёт: окно сервера и окно партнёра остаются открытыми.
			//   Порт освобождает окно штатно — 5286(windowIndex); партнёр получит 5287 (5040).
			SendClose();
		}
		InvItemListDialogInit(TEXT("гейт открытия uiWindow_OpenWithGuards"));
		DialogClearAndHide();
		if (Gate > 0) { Notice(Gate, kColDefault); }
		UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] открытие окна 2008 отклонено гейтом %d (ui_core.c:35163-35202)%s"), Gate,
			FixDefects() ? TEXT("; окно сервера освобождено 5286 [ПОРТ, Д2]") : TEXT(""));
		return;
	}
	// ★ Ревизия О2 (МЕЛК-4): [ПОРТ] WBP окна 2008 не поднялся — у эталона окно живёт с начала игры, такого исхода нет. Признак
	//   +0x2C без окна оставил бы обмен неотменяемым (кнопки «Отмена» нет): идём путём гейта открытия Д2 — 5286(windowIndex)
	//   освобождает окно сервера (партнёр получит 5287), модель сбрасывается, в лог — ошибка.
	bool bWinOk = bWinReady || EnsureWindow();
#if !UE_BUILD_SHIPPING
	if (bDebugFailEnsure) { bWinOk = false; }
#endif
	if (!bWinOk)
	{
		UE_LOG(LogAsdaP2P, Error, TEXT("[обмен] [ПОРТ] окно 2008 (WBP_TRAN_WIN) не поднялось — обмен #%d отменён штатно: 5286 + invItemListDialogInit"), WindowIndex);
		if (WindowIndex >= 0) { SendClose(); }
		InvItemListDialogInit(TEXT("окно 2008 не поднялось"));
		DialogClearAndHide();
		return;
	}
	// ui_core.c:35205-35214: +0x2C = 1, окно видно; уровень < 24 → ONEBUT 97101 (окно всё равно открыто).
	if (UAsdaTradeStateSubsystem* TS = GI ? GI->GetSubsystem<UAsdaTradeStateSubsystem>() : nullptr)
	{
		TS->SetExchangeWindowOpen(true, TEXT("uiWindow_OpenWithGuards"));
	}
	// `CClass_MapInsert(g_pWindowMgr, 2008)` + `CControl_SetVisible(2008, 1)` — окно наверх и видно (О2).
	ShowWindow();
	if (AsdaSelf::GetLevel(GI) < kLevelNeeded)
	{
		AsdaNotice::MsgBoxLoc(GI, EAsdaMsgCategory::Trade, 97101);   // `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, 97101)`
	}
	// ui_core.c:35212-35214: статичная шестерня 2058 видна, анимированная 2059 скрыта.
	bGearHover = false;
	if (Gear) { Gear->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
	PlaneHide(AniGear);   // `CControl_CloseHide(2059)`
	UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] окно 2008 открыто: сервер #%d, тип %d, кандидатов %d"), WindowIndex, ExchangeType, ListCount);
}

void UAsdaExchangeP2PSubsystem::CloseQuestionBox(int32 Type, const TCHAR* Why)
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// [ОРИГИНАЛ] `CGameUI_CloseWindow(вид 5/6)` прячет TWOBUT целиком и играет 5 ВСЕГДА (game_main.c:9860; реестр W6-4).
	// [ПОРТ, Д12] снимаем только СВОЮ запись вопроса: чужой вопрос в том же окне остаётся в очереди и получит ответ.
	//   Прятать чужую запись порт не умеет и при `asda.Trade.P2PFixDefects 0` (порча чужого состояния — не повторяется).
	const int32 Kind = Type ? 6 : 5;
	const int32 Id = Type ? 5002 : 5001;
	const int32 N = WM->RemoveMsgBoxEntries(Kind, Id, Why);
	WM->PlayMsgBoxSfx(5, TEXT("закрытие вопроса обмена из 5285 (CGameUI_CloseWindow, network.c:14338)"));
	UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] %s: вопрос 1520 (вид %d) закрыт, снято записей %d, звук 5"), Why, Kind, N);
}

void UAsdaExchangeP2PSubsystem::OnHotbarNoticeClicked(int32 NoticeType)
{
	// Гейт «окно обмена открыто» → 5042 и значок остаётся — делает сам хотбар (`uiNoticeButtonHandler`, ui_core.c:69361).
	if (NoticeType != 4 && NoticeType != 5) { return; }
	ShowQuestionBox(NoticeType);
}

void UAsdaExchangeP2PSubsystem::ShowQuestionBox(int32 NoticeType)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// `sub_93D36F(g_pItemTooltipUI, 1)` (показать шкалу 1522) + `CItem_BuildTooltipText(ui, 5/6, 5001/5002, имя)`:
	//   текст «<имя> <loc>» (`sprintf("%s %s")`, ui_core.c:56556), кнопки 5003 «Принять» / 5004 «Отказать».
	const int32 Type = (NoticeType == 5) ? 1 : 0;
	const int32 Kind = Type ? 6 : 5;
	const int32 Id = Type ? 5002 : 5001;
	const FString Text = FString::Printf(TEXT("%s %s"), *HotbarNoticeName, *AsdaNotice::LocRaw(GI, Id));
	// Шкала — 30 с от ПРИХОДА 5283 (её тикает игровой цикл, окно только рисует заливку). Старт ещё не взят (клик в том же
	//   кадре, что приход) — берём сейчас, как взял бы первый тик.
	if (bIncomingGauge && IncomingGaugeStartReal < 0.0) { IncomingGaugeStartReal = NowReal(); }
	const bool bGauge = bIncomingGauge && IncomingGaugeStartReal >= 0.0;
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Text, [this, Type](bool bYes) { OnQuestionAnswer(Type, bYes); }, 0.f, 5003, 5004, Id, Kind,
		bGauge ? (float)kIncomingGaugeSec : 0.f, bGauge ? IncomingGaugeStartReal : -1.0);
	UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] значок %d: вопрос 1520 (%d, %d) «%s», шкала %s"), NoticeType, Kind, Id, *Text,
		bGauge ? *FString::Printf(TEXT("%.1f с из 30"), NowReal() - IncomingGaugeStartReal) : TEXT("не идёт"));
}

void UAsdaExchangeP2PSubsystem::OnQuestionAnswer(int32 Type, bool bYes)
{
	if (!bYes)
	{
		// 1524 «Отказать»/ESC → `uiItemUpgradeWnd_CloseDispatch` 0x1389/0x138A: 5284/0, шкала скрыта, +0x1C = 0.
		SendWindowCancel();
		bIncomingGauge = 0; IncomingGaugeStartReal = -1.0;
		UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] вопрос 1520: «Отказать» — 5284/0"));
		return;
	}
	FColor Col = kColDefault;
	const int32 Gate = AcceptGateLoc(Type, Col);
	if (Gate != 0)
	{
		if (Gate > 0) { Notice(Gate, Col); }
		SendWindowCancel();
		UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] «Принять» (тип %d): гейт %d → 5284/0 (ui_core.c:57205-57308)"), Type, Gate);
		return;
	}
	SendWindowReady();   // LABEL_148: 5284/1
	bIncomingGauge = 0; IncomingGaugeStartReal = -1.0;
	UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] «Принять» (тип %d): 5284/1 → «%s» (userNum=%d)"), Type, *InitiatorName, InitiatorNum);
}

void UAsdaExchangeP2PSubsystem::TickIncomingGauge()
{
	if (!bIncomingGauge) { return; }
	// ui_core.c:58255-58260: `if (!gap_32D4[8]) { gap_3300[68] = now; gap_32D4[8] = 1; }` — старт в первом тике после +0x1C = 1.
	if (IncomingGaugeStartReal < 0.0) { IncomingGaugeStartReal = NowReal(); return; }
	if (NowReal() - IncomingGaugeStartReal <= kIncomingGaugeSec) { return; }   // исход — при > 100 %
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	const int32 Type = ExchangeType ? 1 : 0;
	const int32 Kind = Type ? 6 : 5;
	const int32 Id = Type ? 5002 : 5001;
	// ★ Ревизия О1 (МЕЛК-3): эталон решает по ТЕКУЩЕМУ окну (`displayNoticeId`), а не по «наша запись где-то в очереди»:
	//   вопрос, стоящий под чужим окном, эталон по таймауту не отвечает. ★ Ревизия О2 (МЕЛК-9): показанное окно — пара
	//   `+0x32F8/+0x32FC` менеджера, а не верхняя запись очереди (при `asda.MsgBox.QueueFixes 0` на экране ПЕРВАЯ запись).
	const bool bOwnBoxOnScreen = WM && WM->IsMsgBoxDisplayed(Kind, Id);
	if (!FixDefects())
	{
		// [ОРИГИНАЛ] ui_core.c:58270-58277: `uiItemUpgradeWnd_CloseDispatch` по ТЕКУЩЕМУ окну; если это наш вопрос — 5284/0.
		//   Чужое окно эталон закрыл бы — этого порт не повторяет (порча чужого состояния). Значок типа 7 (его нет), +0x1C
		//   остаётся — шкала пойдёт заново.
		if (bOwnBoxOnScreen)
		{
			SendWindowCancel();
			WM->RemoveMsgBoxEntries(Kind, Id, TEXT("обмен: шкала 30 с"));
			WM->PlayMsgBoxSfx(5, TEXT("закрытие вопроса обмена по шкале (CGameUI_CloseWindow)"));
			return;
		}
		IncomingGaugeStartReal = NowReal();
		return;
	}
	// [ПОРТ, Д5/Д6] 30 с истекли: отказ 5284/0 уходит ВСЕГДА (и при неоткрытом вопросе), снимается СВОЯ запись 1520 и значок 4/5.
	SendWindowCancel();
	bIncomingGauge = 0; IncomingGaugeStartReal = -1.0;
	int32 Removed = 0;
	if (WM)
	{
		Removed = WM->RemoveMsgBoxEntries(Kind, Id, TEXT("обмен: шкала 30 с"));
		if (Removed > 0) { WM->PlayMsgBoxSfx(5, TEXT("закрытие вопроса обмена по шкале (CGameUI_CloseWindow)")); }
	}
	if (UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr) { Hb->RemoveNoticeIcon(Type ? 5 : 4); }
	UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] шкала 30 с истекла: 5284/0, снято записей 1520 %d, значок %d снят [ПОРТ, Д5/Д6]"), Removed, Type ? 5 : 4);
}

// ═══════════════════════════════ ГЕЙТЫ ════════════════════════════════════════════════════════

int32 UAsdaExchangeP2PSubsystem::MenuGateLoc() const
{
	const UGameInstance* GI = GetGameInstance();
	if (!GI) { return 0; }
	UGameInstance* G = const_cast<UGameInstance*>(GI);
	// shop_trade.c:6617-6629, порядок эталона.
	if (UAsdaNpcDialogSubsystem* Dlg = G->GetSubsystem<UAsdaNpcDialogSubsystem>()) { if (Dlg->IsDialogOpen()) { return 5043; } }     // `g_bInventoryLocked`
	if (UAsdaPremiumShopSubsystem* Ps = G->GetSubsystem<UAsdaPremiumShopSubsystem>()) { if (Ps->IsOpen()) { return 5033; } }       // `helper_getField2075(g_pShopWnd)`
	if (UAsdaInventorySubsystem* Inv = G->GetSubsystem<UAsdaInventorySubsystem>()) { if (Inv->IsWindowOpen()) { return 5032; } }   // `sub_93248D(g_pInventoryWnd)`
	if (UAsdaHairShopSubsystem* Hs = G->GetSubsystem<UAsdaHairShopSubsystem>()) { if (Hs->IsSessionOpen()) { return 13016; } }     // `CObject_GetField166`
	if (UAsdaBoosterPanelSubsystem* Bo = G->GetSubsystem<UAsdaBoosterPanelSubsystem>()) { if (Bo->IsWindowOpen()) { return 13104; } }   // `CObject_GetField254`
	if (UAsdaWarehouseSubsystem* Wh = G->GetSubsystem<UAsdaWarehouseSubsystem>()) { if (Wh->IsGateField5581()) { return 10307; } }         // `CObject_GetField5581` — флаг свитка, ★ 05.11.2026 `asda.Warehouse.GateRecallFlag`
	if (UAsdaCraftSubsystem::IsAnyCraftWindowOpen(GI)) { return -1; }   // окна ремесла — молча
	// ★ Пачка 20, жалоба 10 — [ИСПРАВЛЕНИЕ]: при открытом рынке 13000 своя заявка обмена не уходит (молча, как окна ремесла).
	if (UAsdaExchangeSubsystem::IsMarketWindowOpen(GI)) { return -1; }
	return 0;
}

int32 UAsdaExchangeP2PSubsystem::AcceptGateLoc(int32 Type, FColor& OutColor) const
{
	const UGameInstance* GI = GetGameInstance();
	if (!GI) { return 0; }
	UGameInstance* G = const_cast<UGameInstance*>(GI);
	// ui_core.c:57205-57306, порядок эталона. Первые четыре — белым (−1), бустер/склад — −75279.
	OutColor = kColWhite;
	if (UAsdaInventorySubsystem* Inv = G->GetSubsystem<UAsdaInventorySubsystem>()) { if (Inv->IsWindowOpen()) { return 5032; } }
	if (UAsdaPremiumShopSubsystem* Ps = G->GetSubsystem<UAsdaPremiumShopSubsystem>()) { if (Ps->IsOpen()) { return 5033; } }
	if (UAsdaNpcDialogSubsystem* Dlg = G->GetSubsystem<UAsdaNpcDialogSubsystem>()) { if (Dlg->IsDialogOpen()) { return 5044; } }
	if (UAsdaHairShopSubsystem* Hs = G->GetSubsystem<UAsdaHairShopSubsystem>()) { if (Hs->IsSessionOpen()) { return 13016; } }
	OutColor = kColDefault;
	// [ОРИГИНАЛ, Д3] у типа 1 (0x138A) строки перепутаны: бустер → 10307, склад → 13104. [ПОРТ] при исправлении — как у типа 0.
	const bool bSwap = (Type != 0) && !FixDefects();
	if (UAsdaBoosterPanelSubsystem* Bo = G->GetSubsystem<UAsdaBoosterPanelSubsystem>()) { if (Bo->IsWindowOpen()) { return bSwap ? 10307 : 13104; } }
	if (UAsdaWarehouseSubsystem* Wh = G->GetSubsystem<UAsdaWarehouseSubsystem>()) { if (Wh->IsGateField5581()) { return bSwap ? 13104 : 10307; } }   // ★ 05.11.2026 флаг свитка, как эталон
	if (UAsdaCraftSubsystem::IsAnyCraftWindowOpen(GI)) { return -1; }   // LABEL_158: молча 5284/0
	return 0;
}

int32 UAsdaExchangeP2PSubsystem::OpenGateLoc() const
{
	const UGameInstance* GI = GetGameInstance();
	if (!GI) { return 0; }
	UGameInstance* G = const_cast<UGameInstance*>(GI);
	// ui_core.c:35163-35202, порядок эталона.
	if (UAsdaPremiumShopSubsystem* Ps = G->GetSubsystem<UAsdaPremiumShopSubsystem>()) { if (Ps->IsOpen()) { return 5033; } }
	if (UAsdaInventorySubsystem* Inv = G->GetSubsystem<UAsdaInventorySubsystem>()) { if (Inv->IsWindowOpen()) { return 5032; } }
	if (UAsdaNpcDialogSubsystem* Dlg = G->GetSubsystem<UAsdaNpcDialogSubsystem>()) { if (Dlg->IsDialogOpen()) { return 5043; } }
	//   `sub_930BE2(g_pItemShopWnd)` (= `get_o130`, аукцион 13000 открыт) || парикмахерская → молча (LABEL_14).
	if (UAsdaExchangeSubsystem* Au = G->GetSubsystem<UAsdaExchangeSubsystem>()) { if (Au->IsOpen()) { return -1; } }
	if (UAsdaHairShopSubsystem* Hs = G->GetSubsystem<UAsdaHairShopSubsystem>()) { if (Hs->IsSessionOpen()) { return -1; } }
	//   бустер → 97101 (★ дефект оригинала Д4: не тот текст; истинной строки в данных нет — не исправляется).
	if (UAsdaBoosterPanelSubsystem* Bo = G->GetSubsystem<UAsdaBoosterPanelSubsystem>()) { if (Bo->IsWindowOpen()) { return 97101; } }
	if (UAsdaCraftSubsystem::IsAnyCraftWindowOpen(GI)) { return -1; }
	return 0;
}

// ═══════════════════════════════ ОБЩЕЕ ════════════════════════════════════════════════════════

void UAsdaExchangeP2PSubsystem::Notice(int32 LocId, FColor Color, const FString& Slot) const
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }
	++NoticeSeq; LastNoticeLoc = LocId; LastNoticeSlot = Slot;
	// `UI_FormatLocalizedNotice` (+ `CObject_SetSlotString` для [ss1]) → `CChat_AddTokenizedLines(g_pChatWnd, txt, цвет, 10000, 0)`.
	const FString Text = Slot.IsEmpty() ? AsdaNotice::LocRaw(GI, LocId) : AsdaNotice::LocSlot(GI, LocId, Slot);
	AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, Text, Color, 10000.f);   // System: цвет — обработчика, звука нет (как 5042 хотбара и меню, ревизия С-1 волны 0)
	UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] нотис %d%s: %s"), LocId, Slot.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" [ss1]=«%s»"), *Slot), *Text);
}

FString UAsdaExchangeP2PSubsystem::DebugString() const
{
	return FString::Printf(TEXT("роль=%d закрыл_я=%d заявка(+14)=%d ответ(+18)=%d шкала(+1C)=%d таймер(+38)=%d я=%d инициатор=%d "
		"окно#=%d тип=%d стороны=%d/%d цель=%d «%s» таймаут→%d имя_инициатора=«%s» окно_2008=%d | %s"),
		Role, bClosedByMe, bRequestActive, bAcceptSent, bIncomingGauge, bTimerActive, MyNum, InitiatorNum, WindowIndex, ExchangeType,
		PartnerNum1, PartnerNum2, MenuTargetNum, *MenuTargetName, TimeoutTargetNum, *InitiatorName,
		UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GetGameInstance()) ? 1 : 0, *DebugDumpModel());
}

#if !UE_BUILD_SHIPPING
namespace
{
	// `asda.Trade.P2P` — дамп модели `CExchange` (только чтение).
	FAutoConsoleCommandWithWorldAndArgs GAsdaTradeP2PDump(
		TEXT("asda.Trade.P2P"),
		TEXT("asda.Trade.P2P — дамп модели обмена игрок-игрок (поля CExchange: роль, заявка, шкала, таймер, окно, стороны)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
		{
			const UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
			const UAsdaExchangeP2PSubsystem* S = GI ? GI->GetSubsystem<UAsdaExchangeP2PSubsystem>() : nullptr;
			UE_LOG(LogAsdaP2P, Log, TEXT("[обмен] %s"), S ? *S->DebugString() : TEXT("подсистемы нет"));
		}));
}
#endif
