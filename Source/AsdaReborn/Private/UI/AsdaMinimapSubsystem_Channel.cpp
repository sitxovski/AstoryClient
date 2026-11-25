// Copyright (c) 2026 @sitxovski (Discord)
// AsdaMinimapSubsystem_Channel.cpp — ★ R3-c 12.11.2026 (разрез AsdaMinimapSubsystem.cpp, дословный перенос): каналы: ключи, удержание движения, шлюзы/нотисы, кнопки и панель каналов.
//   Общее для частей — в AsdaMinimapSubsystem_Private.h; шапка и реверс — в AsdaMinimapSubsystem.cpp.
#include "UI/AsdaMinimapSubsystem.h"



#include "UI/AsdaNoticeColors.h"           // ★ 26.10.2026: цвет −11514 (0xFFFFD306) строки отказа
#include "AsdaMinimapSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaMinimapSubsystem*.cpp

// Подпись канала мини-карты (★ R3-c чистка 12.11.2026: перенесено из AsdaMinimapSubsystem.cpp — нужно только этой части, static как до разреза).
static int32 GAsdaMinimapShortChannelWord = 1;
static ASDA_DEV_CVAR_REF(CVarAsdaMinimapShortChannelWord,
	TEXT("Asda.Minimap.ShortChannelWord"),
	GAsdaMinimapShortChannelWord,
	TEXT("1 (по умолчанию) — подпись мини-карты «Альпеон - 1 кан.»; 0 — как в оригинале «… 1 канал»."),
	ECVF_Default);

// ── ★ ПЕРЕКЛЮЧАТЕЛЬ КАНАЛОВ (эталон MiniMap_BuildUI world_map.c:7120-7167,
//    обработчик события 0xB world_map.c:7495-7585, ChannelSelectWnd_UpdateChannelLabels@0xD64730) ──
//
//  Состав (JUI 1702): 17500 Change_Channel_Btn — кнопка-«шапка»; 1717 MMap_SvrCh — подпись
//  «<сервер> - <канал> канал» (нотис 15200 «[ss1] - [sd1] канал», ss1 = имя сервера нотис 11000+id,
//  sd1 = g_nCurrentChannel+1); 17501 Change_Channel_Win — выпадающая панель; 17503..17517
//  Change_Channel00..14 — строки «N CH»; 17502 Change_Channel_Sel — жёлтая рамка под курсором.
//  Строк показывается ровно g_nChannelCount, панель укорачивается на (12−count)·rowH.
// ★ 26.10.2026 [ОРИГИНАЛ] гейты смены канала и их нотисы (жалоба игроков «канал в игре не меняется»). Прежде порт проверял
//   только «тот же канал» и печатал его лишь в журнал: игрок не видел ни одного отказа. 0 — прежнее поведение порта.
static ASDA_DEV_CVAR(int32, CVarAsdaMinimapChannelGates,
	TEXT("asda.Minimap.ChannelGates"), 1,
	TEXT("1 = гейты смены канала оригинала (мёртв — молча; отряд 98004; тот же канал 98003; особая карта 98001; подземелье 98002), ")
	TEXT("нотис — системный чат (0xFFFFD306, 10 с) + окно-сообщение; 0 = как прежде: только «тот же канал», в журнал."),
	ECVF_Default);

// ★★ 08.11.2026 [ИСПРАВЛЕНИЕ] (пачка 16, баг 2, жалоба владельца «при смене канала персонаж продолжает идти»). Разбор эталона:
//   клик по строке канала (world_map.c:7524-7585) только шлёт 6084 — ход НЕ останавливается; остановку оригинал делает лишь по
//   ответу 6015 (`netHandleChannelTransfer@0xF49660`, network.c:43886: `inputClearCommandQueue` + состояние 11 + сразу экран
//   загрузки). Между 6084 и 6015 (круг через RelayServer) персонаж оригинала шёл дальше. В порту хуже: экран загрузки встаёт
//   только по 6004, а до портального логона 5058 зажатая клавиша вела персонажа локально, автопуть после смены возобновлялся.
//   Ключ 1: в момент запроса смены (до 6084) — стоп хода (локально + 4008, если ход был), снятие маршрута/подхода к NPC/навыку,
//   отмена автопути; затем ход держится общей точкой остановки `AAsdaPlayerController::IsStunHardStopped` (клавиши, клик по
//   земле и мини-карте, автопуть, опоздавший грант 4007, верхом — то же) — на ChannelMoveStopSec секунд ожидания ответа
//   (сервер `Recv_UserChannelMove@0x54FF80` при отказе молчит или шлёт нотис 6143 — бессрочный замок был бы тупиком) и всё
//   время перехода (`bTransferInProgress`: от 6015/5055/6013/6201 до отправки логона на новом соединении — там пакеты хода
//   всё равно держит гейт отправки, а локальный ход уводил персонажа «в пустоту»). 0 — прежнее поведение порта.
static ASDA_DEV_CVAR(int32, CVarAsdaMinimapChannelMoveStop,
	TEXT("asda.Minimap.ChannelMoveStop"), 1,
	TEXT("1 = [ИСПРАВЛЕНИЕ] запрос смены канала (6084) останавливает ход (4008), снимает маршрут и автопуть; ход держится до ответа ")
	TEXT("(не дольше asda.Minimap.ChannelMoveStopSec) и всё время перехода между картами/каналами; 0 = прежнее поведение порта."),
	ECVF_Default);
static ASDA_DEV_CVAR(float, CVarAsdaMinimapChannelMoveStopSec,
	TEXT("asda.Minimap.ChannelMoveStopSec"), 3.f,
	TEXT("Сколько секунд после запроса смены канала (6084) держать ход, если сервер не ответил 6015 (отказ сервера — молчание)."),
	ECVF_Default);

// Момент (FPlatformTime::Seconds), до которого держится ход после запроса смены канала; 0 — не держится. Один игровой экземпляр.
static double GAsdaChannelMoveHoldUntil = 0.0;

// Ход своего персонажа держится сменой канала или переходом (общая точка — `AAsdaPlayerController::IsStunHardStopped`).
//   Объявление — у вызывающих через extern (как `AsdaDuelLoserLocked`).
bool AsdaSelfMoveHeldForChannel(const UGameInstance* GI)
{
	if (CVarAsdaMinimapChannelMoveStop.GetValueOnGameThread() == 0) { return false; }
	if (GAsdaChannelMoveHoldUntil > 0.0)
	{
		if (FPlatformTime::Seconds() < GAsdaChannelMoveHoldUntil) { return true; }
		GAsdaChannelMoveHoldUntil = 0.0;
		UE_LOG(LogAsdaMinimap, Log, TEXT("[миникарта] ответа на смену канала нет за %.1f с — ход снова разрешён [ИСПРАВЛЕНИЕ 08.11]"),
			CVarAsdaMinimapChannelMoveStopSec.GetValueOnGameThread());
	}
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	return Net && Net->bTransferInProgress;
}

// Снять удержание ожидания ответа (ответ 6015 пришёл — дальше держит `bTransferInProgress`; самотест).
void AsdaClearChannelMoveHold()
{
	GAsdaChannelMoveHoldUntil = 0.0;
}

// Остановить ход в момент запроса смены канала (до 6084, чтобы 4008 ушёл прежде запроса) и взвести удержание.
static void AsdaStopMoveForChannelRequest(UGameInstance* GI, UAsdaMinimapSubsystem* Mini, int32 Channel)
{
	if (!GI || CVarAsdaMinimapChannelMoveStop.GetValueOnGameThread() == 0) { return; }
	GAsdaChannelMoveHoldUntil = FPlatformTime::Seconds() + FMath::Max(0.1f, CVarAsdaMinimapChannelMoveStopSec.GetValueOnGameThread());
	const bool bTravel = Mini && Mini->IsTravelRequested();
	if (bTravel) { Mini->CancelTravelRequest(); }   // карта та же — после смены поход сам не продолжается
	AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GI->GetFirstLocalPlayerController());
	AAsdaPlayerCharacter* Ch = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
	const bool bWalking = Ch && Ch->IsMoving();
	if (PC)
	{
		PC->StopMovementForMapChange();                        // маршрут, подход к NPC/навыку, покой (без пакета)
		PC->StopMoveAndNotify(TEXT("запрос смены канала"));    // локальный стоп + 4008, если ход не закрыт
	}
	UE_LOG(LogAsdaMinimap, Log, TEXT("[миникарта] смена канала на %d: ход остановлен (шёл %d, автопуть %s), удержание до ответа [ИСПРАВЛЕНИЕ 08.11]"),
		Channel + 1, bWalking ? 1 : 0, bTravel ? TEXT("отменён") : TEXT("не было"));
}

// Гейт смены канала (эталон `MiniMap_BigMapWindowProc@0xD65210`, событие 0xB). Возврат: 0 — можно; −1 — молчаливый отказ;
//   > 0 — id нотиса отказа. bRow = false — кнопка 17500 (world_map.c:7509-7518: мёртв, карта {4,5,8,9,12,14,15} → 98001, байт +68
//   записи карты → 98002); true — строка канала Channel (0-based; world_map.c:7523-7560: мёртв, отряд 98004, тот же канал 98003,
//   карта {4,5,12,14} → 98001, +68 → 98002). Молчаливые условия эталона `sub_92B95D == 0x4000` и `effectSlots[112].field_C & 1` —
//   биты занятости персонажа, в порте не заведены (PORT_PENDING_ON_SYSTEMS.md, «Биты занятости»); проверяется «мёртв» (+47551 == 6).
static int32 AsdaMinimapChannelGate(const UGameInstance* GI, bool bRow, int32 Channel)
{
	if (!GI || CVarAsdaMinimapChannelGates.GetValueOnGameThread() == 0) { return 0; }
	if (const AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GI->GetFirstLocalPlayerController()))
	{
		if (PC->IsSelfDead()) { return -1; }
	}
	const UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>();
	if (bRow)
	{
		if (const UAsdaPartySubsystem* Party = GI->GetSubsystem<UAsdaPartySubsystem>())
		{
			if (Party->IsInParty()) { return 98004; }
		}
		if (Net && Channel == Net->CurrentChannel) { return 98003; }
	}
	const int32 Map = Net ? Net->EnterMapId : -1;   // эталон `getField3036(&g_pLocalPlayer)` — номер текущей карты
	const bool bSpecial = bRow
		? (Map == 4 || Map == 5 || Map == 12 || Map == 14)
		: (Map == 4 || Map == 5 || Map == 12 || Map == 14 || Map == 15 || Map == 8 || Map == 9);
	if (bSpecial) { return 98001; }
	// `getSlotByIndex(&g_pLocalPlayer, карта)` + байт +68 — флаг подземелья записи карты (`FAsdaMapRow::ItemRestrict`,
	//   раскладка записи — Reports/minimap_re.md §22, indun_campwar_channel.md §1.0).
	if (const UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>())
	{
		if (const FAsdaMapRow* Row = LM->GetMapRow(Map))
		{
			if (Row->ItemRestrict != 0) { return 98002; }
		}
	}
	return 0;
}

// Нотис отказа смены канала: эталон `CChat_AddTokenizedLines(g_pChatWnd, txt, −11514, 10000, 0)`, затем
//   `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, txt, 1)` — однокнопочное окно-сообщение 1500 (AsdaNotice::MsgBox).
static void AsdaMinimapChannelNotice(const UGameInstance* GI, int32 LocId)
{
	if (!GI || LocId <= 0) { return; }
	AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, AsdaNotice::LocRaw(GI, LocId), AsdaNoticeColor::Gold, 10000.f);
	AsdaNotice::MsgBoxLoc(GI, EAsdaMsgCategory::System, LocId);
}

int32 UAsdaMinimapSubsystem::NetChannelCount() const
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	// Эталон: g_nChannelCount, ограничение сверху 12 строками окна (world_map.c:7160).
	return Net ? FMath::Clamp(Net->ChannelCount, 0, 12) : 0;
}

int32 UAsdaMinimapSubsystem::NetCurrentChannel() const
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	return Net ? Net->CurrentChannel : 0;
}

void UAsdaMinimapSubsystem::BindChannelUI()
{
	if (!MinimapWidget) { return; }
	ChannelRelays.Reset();
	// 17500 — раскрыть/свернуть панель.
	if (UButton* Btn = Cast<UButton>(MinimapWidget->GetWidgetFromName(TEXT("Change_Channel_Btn"))))
	{
		Btn->OnClicked.Clear();
		Btn->OnClicked.AddDynamic(this, &UAsdaMinimapSubsystem::OnChannelBtnClicked);
	}
	// 17503+i — строки. Импортёр сделал из них TextBlock (не кликаются), поверх каждой лежит
	//   прозрачная кнопка {row}_Click ровно по её rect (Tools/asda_fix_minimap_ui.py).
	for (int32 i = 0; i < 15; ++i)
	{
		const FString BName = FString::Printf(TEXT("Change_Channel%02d_Click"), i);
		UButton* Row = Cast<UButton>(MinimapWidget->GetWidgetFromName(FName(*BName)));
		if (!Row) { continue; }
		Row->OnClicked.Clear(); Row->OnHovered.Clear(); Row->OnUnhovered.Clear();
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Index = i;
		R->Callback        = [this](int32 Idx)
		{
			// ★ В4 рыбалки: строки 0x445F…0x446D — тот же гейт события 0xB (g:374375).
			if (AsdaMinimapFishingGate(GetGameInstance(), TEXT("строка канала"))) { return; }
			SelectChannel(Idx);
		};
		R->HoverCallback   = [this, i]()       { SetHoverChannel(i); };
		R->UnhoverCallback = [this]()          { SetHoverChannel(-1); };
		Row->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		Row->OnHovered.AddDynamic(R, &UAsdaButtonRelay::OnHovered);
		Row->OnUnhovered.AddDynamic(R, &UAsdaButtonRelay::OnUnhovered);
		AsdaUi::AnchorRelay(ChannelRelays, R, Row);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}
	SetChannelPanelOpen(false);   // JUI Visible=0 у 17501
}

void UAsdaMinimapSubsystem::OnChannelBtnClicked()
{
	if (AsdaMinimapFishingGate(GetGameInstance(), TEXT("17500 каналы"))) { return; }   // ★ В4 рыбалки: первым, до гейтов лавки

	// ★ Волна Л5 торговли (30.09.2026): [ОРИГИНАЛ] `MiniMap_BigMapWindowProc@0xD65210`, событие 0xB по 17500 (world_map.c:7496-7502):
	//   `if (sub_934E86(g_pSellWindow) || sub_929DC4(g_bSomeFeatureEnabled))` → 107022 в чат (−11514, 10 с), панель каналов не трогается.
	if (UAsdaTradeStateSubsystem::IsSelfStoreOpen(GetGameInstance()))
	{
		UAsdaTradeStateSubsystem::ChatStoreGate(GetGameInstance(), 107022, TEXT("MiniMap_BigMapWindowProc 17500"));
		return;
	}
	// ★ Ревизия Л4/Л5 (СРЕДН-1): [ОРИГИНАЛ] следом (world_map.c:7503-7508) — свиток возврата склада активен → 10305 в чат, панель не
	//   трогается. (Дальше у эталона молчаливые проверки состояния персонажа `sub_92B95D == 0x4000` / мёртв / `effectSlots[112] & 1` —
	//   вне торговли, реестр Л5-1.)
	if (UAsdaWarehouseSubsystem* Ware = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWarehouseSubsystem>() : nullptr)
	{
		if (Ware->IsRecallActive()) { Ware->NoticeRecallBlocked(10305); return; }
	}
	// ★ 26.10.2026 [ОРИГИНАЛ] (world_map.c:7509-7518): мёртв — молча; особая карта {4,5,8,9,12,14,15} → 98001; подземелье
	//   (байт +68 записи карты) → 98002. Панель при отказе не раскрывается. Ключ asda.Minimap.ChannelGates (0 — без гейтов).
	const int32 Gate = AsdaMinimapChannelGate(GetGameInstance(), /*bRow*/ false, /*Channel*/ -1);
	if (Gate != 0)
	{
		// Эталон: при отказе на кнопке панель не трогается (видимость 17501/1717 прежняя), только нотис.
		if (Gate > 0) { AsdaMinimapChannelNotice(GetGameInstance(), Gate); }
		UE_LOG(LogAsdaMinimap, Log, TEXT("[миникарта] 17500 каналы: отказ %s"), Gate > 0 ? *FString::FromInt(Gate) : TEXT("молча (мёртв)"));
		return;
	}
	SetChannelPanelOpen(!bChannelPanelOpen);
}

void UAsdaMinimapSubsystem::SetChannelPanelOpen(bool bOn)
{
	if (!MinimapWidget) { return; }
	bChannelPanelOpen = bOn;
	// ★ 07.11.2026 [ОРИГИНАЛ] щелчок по земле скрывает панель 17501: `Input_WorldClickHandler` (input.c:493) → `sub_D66EC0` (world_map.c:7125
	//   кладёт 17501 в поле +17). В порт этот вызов не перешёл. [ИСПРАВЛЕНИЕ] как у остальных меню: панель гасит щелчок мимо и потеря фокуса,
	//   разбор — `AsdaWindowSubsystem_TopPress.cpp`. Кнопка 17500 входит в зону: её щелчок — переключатель, иначе панель откроется заново.
	if (bOn)
	{
		if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
		{
			static const FName kChannelPanelZone(TEXT("MINIMAP_CHANNEL_PANEL"));   // зона панели 17501 и кнопки 17500
			FAsdaUiZoneRect Rect;
			Rect.BindWeakLambda(this, [this](FVector2D& Mn, FVector2D& Mx)
			{
				if (!bChannelPanelOpen || !MinimapWidget) { return false; }
				FVector2D BtnMn, BtnMx;
				if (!AsdaUi::WidgetAbsRect(MinimapWidget->GetWidgetFromName(TEXT("Change_Channel_Win_37_0")), Mn, Mx)) { return false; }
				if (AsdaUi::WidgetAbsRect(MinimapWidget->GetWidgetFromName(TEXT("Change_Channel_Btn")), BtnMn, BtnMx))
				{
					Mn = FVector2D(FMath::Min(Mn.X, BtnMn.X), FMath::Min(Mn.Y, BtnMn.Y));
					Mx = FVector2D(FMath::Max(Mx.X, BtnMx.X), FMath::Max(Mx.Y, BtnMx.Y));
				}
				return true;
			});
			WM->RegisterUiZone(kChannelPanelZone, MoveTemp(Rect));
			WM->RegisterClickAwayMenu(kChannelPanelZone, FSimpleDelegate::CreateWeakLambda(this, [this]() { SetChannelPanelOpen(false); }));
		}
	}
	// Панель 17501: показываем и КОНТЕЙНЕР, и её собственный план-подложку (скин Box_BlackOt40,
	//   74×240) — в JUI у контрола Visible="0", поэтому импортёр скрыл и группу, и картинку; без
	//   картинки список висел «в воздухе» поверх мира (живой тест).
	if (UWidget* Panel = MinimapWidget->GetWidgetFromName(TEXT("Change_Channel_Win_grp")))
	{
		AsdaUi::SetShownDecor(Panel, bOn);
	}
	if (UWidget* PanelBg = MinimapWidget->GetWidgetFromName(TEXT("Change_Channel_Win_37_0")))
	{
		AsdaUi::SetShownNoHit(PanelBg, bOn);
	}
	// ★ ПОДПИСЬ 1717 MMap_SvrCh ОСТАЁТСЯ ВИДНОЙ при открытом списке (живой тест оригинала).
	//   Разбор: её видимость в эталоне завязана на НАВЕДЕНИЕ на кнопку 17500, а не на панель:
	//     событие 0x9 (курсор вошёл, world_map.c:7495-7499): SetVisible(1717, 0) + текст уходит
	//       в СОБСТВЕННУЮ подпись кнопки (CWidget_SetTextAll4(self[16], pThis+132)) — текст не исчезает;
	//     событие 0xA (курсор ушёл, world_map.c:7508-7515): SetVisible(1717, 1) БЕЗУСЛОВНО;
	//     событие 0xB (нажатие, world_map.c:7519-7521): SetVisible(17501, !было); SetVisible(1717, было)
	//       — то есть при открытии панель гасит подпись лишь на время, пока курсор ещё на кнопке;
	//       как только он уходит, 0xA включает её обратно ПРИ ОТКРЫТОЙ панели.
	//   Своей подписи у кнопки импортёр не создал (в JUI у 17500 нет CaptionText), поэтому мигание
	//   1717 на ховере не воспроизводим — держим подпись видимой всегда: итог на экране тот же.
	if (UWidget* Lbl = MinimapWidget->GetWidgetFromName(TEXT("MMap_SvrCh")))
	{
		Lbl->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	if (!bOn) { SetHoverChannel(-1); }
	UpdateChannelUI();
	UE_LOG(LogAsdaMinimap, Log, TEXT("[миникарта] панель каналов=%s (строк=%d, текущий=%d)"),
		bOn ? TEXT("открыта") : TEXT("закрыта"), NetChannelCount(), NetCurrentChannel() + 1);
}

// 17502 Change_Channel_Sel — жёлтая рамка (Box_YellowOt20) под строкой, над которой курсор.
void UAsdaMinimapSubsystem::SetHoverChannel(int32 Channel)
{
	HoverChannel = Channel;
	if (!MinimapWidget) { return; }
	UWidget* Sel = MinimapWidget->GetWidgetFromName(TEXT("Change_Channel_Sel_50_0"));
	if (!Sel) { return; }
	const bool bShow = bChannelPanelOpen && Channel >= 0 && Channel < NetChannelCount();
	AsdaUi::SetShownNoHit(Sel, bShow);
	if (!bShow) { return; }
	// Рамка встаёт ровно на rect строки (JUI Origin 2, 2+16·i, Size 70×15).
	const FString RName = FString::Printf(TEXT("Change_Channel%02d"), Channel);
	if (UCanvasPanelSlot* RowSlot = SlotOf(MinimapWidget->GetWidgetFromName(FName(*RName))))
	{
		if (UCanvasPanelSlot* SelSlot = SlotOf(Sel)) { SelSlot->SetOffsets(RowSlot->GetOffsets()); }
	}
}

void UAsdaMinimapSubsystem::UpdateChannelUI()
{
	if (!MinimapWidget) { return; }
	const int32 Count = NetChannelCount();
	const int32 Cur   = NetCurrentChannel();
	// подписи не дёргаем каждый кадр; ShownShortWord — чтобы переключение CVar перерисовало подпись
	if (Count == ShownChannelCount && Cur == ShownChannel && ShownShortWord == GAsdaMinimapShortChannelWord) { return; }
	ShownChannelCount = Count; ShownChannel = Cur; ShownShortWord = GAsdaMinimapShortChannelWord;

	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* TS  = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;

	// 1717 MMap_SvrCh: нотис 15200 «[ss1] - [sd1] канал»; ss1 = имя сервера (нотис 11000 + индекс сервера,
	//   эталон dword_340F328), sd1 = номер канала (1-based).
	if (UTextBlock* Lbl = Cast<UTextBlock>(MinimapWidget->GetWidgetFromName(TEXT("MMap_SvrCh"))))
	{
		FString Text;
		if (TS)
		{
			const int32 SrvIdx = Net ? Net->SelectedServerIndex : 0;
			const FString SrvName = TS->GetTextById(11000 + SrvIdx);
			// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): сокращённая форма — своя строка 990506 «[ss1] - [sd1] кан.» (раньше код искал
			//   в готовой русской строке слово «канал» — на английском это молча перестало бы работать).
			//   Своей строки нет — полная строка игры 15200.
			if (GAsdaMinimapShortChannelWord != 0) { Text = TS->FormatById(990506, { SrvName }, { Cur + 1 }); }
			if (Text.IsEmpty()) { Text = TS->FormatById(15200, { SrvName }, { Cur + 1 }); }
			if (Text.IsEmpty()) { Text = SrvName; }
			// ★★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА по просьбе юзера: в подписи мини-карты слово «канал» пишем
			//    сокращённо — «Альпеон - 1 кан.». Игровую строку (нотис 15200 «[ss1] - [sd1] канал»)
			//    НЕ правим: сокращаем только готовый результат и только здесь. Полный вариант остаётся
			//    везде, где строку берут напрямую (экран выбора канала на логине — там свои нотисы
			//    11051+i «Канал N»). Отключается: Asda.Minimap.ShortChannelWord 0.
		}
		Lbl->SetText(FText::FromString(Text));
		ShownSvrChText = Text;   // только для лога перерисовки в конце функции
	}

	// Строки 17503+i: подпись «N CH», цвет — эталон MiniMap_BuildUI world_map.c:7148-7156:
	//   текущий канал 0xFF80FF00 (жёлто-зелёный), остальные 0xFFFEFEFE. Строки с idx >= count скрыты.
	// ★ D1 (дедуп UI 2026-08-08): единая AsdaUi::SrgbU32ToLinear вместо локальной лямбды.
	auto Srgb = [](uint32 C) { return AsdaUi::SrgbU32ToLinear(C); };
	for (int32 i = 0; i < 15; ++i)
	{
		const bool bShown = (i < Count);
		if (UTextBlock* Row = Cast<UTextBlock>(MinimapWidget->GetWidgetFromName(FName(*FString::Printf(TEXT("Change_Channel%02d"), i)))))
		{
			AsdaUi::SetShownDecor(Row, bShown);
			if (bShown)
			{
				Row->SetText(FText::FromString(FString::Printf(TEXT("%d CH"), i + 1)));
				Row->SetColorAndOpacity(FSlateColor(Srgb(i == Cur ? 0xFF80FF00u : 0xFFFEFEFEu)));
			}
		}
		if (UWidget* Btn = MinimapWidget->GetWidgetFromName(FName(*FString::Printf(TEXT("Change_Channel%02d_Click"), i))))
		{
			AsdaUi::SetShownHit(Btn, bShown);
		}
	}
	// Высота панели: эталон world_map.c:7160-7166 — при count<12 окно короче на (12−count)·rowH (16 px).
	if (UCanvasPanelSlot* PanelSlot = SlotOf(MinimapWidget->GetWidgetFromName(TEXT("Change_Channel_Win_grp"))))
	{
		const float RowH = 16.f;                       // JUI: Origin.y строк = 2 + 16·i
		const float FullH = 240.f;                     // JUI Size 74×240 (12 строк)
		FMargin M = PanelSlot->GetOffsets();
		M.Bottom = FullH - (float)FMath::Max(0, 12 - Count) * RowH;
		PanelSlot->SetOffsets(M);
	}
	if (UCanvasPanelSlot* BgSlot = SlotOf(MinimapWidget->GetWidgetFromName(TEXT("Change_Channel_Win_37_0"))))
	{
		FMargin M = BgSlot->GetOffsets();
		M.Bottom = 240.f - (float)FMath::Max(0, 12 - Count) * 16.f;
		BgSlot->SetOffsets(M);
	}
	// ★ Лог перерисовки — чтобы «пустой список каналов» больше не приходилось искать по кадру:
	//   строка печатается РОВНО тогда, когда подписи 1717/17503+i реально записаны в дерево.
	//   Её отсутствие после «окно создано (1702 + 1700)» = кэш опять съел перерисовку.
	UE_LOG(LogAsdaMinimap, Log, TEXT("[миникарта] каналы перерисованы: подпись «%s», строк %d, текущий %d"),
		*ShownSvrChText, Count, Cur + 1);
}

// Клик по строке канала → 6084 (эталон world_map.c:7524-7585).
void UAsdaMinimapSubsystem::SelectChannel(int32 Channel)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaTextSubsystem* TS = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	if (!Net) { return; }
	if (Channel < 0 || Channel >= NetChannelCount()) { return; }
	auto Deny = [&](int32 LocId)
	{
		// Эталон: панель закрывается, подпись возвращается, нотис уходит в чат + всплывашку.
		SetChannelPanelOpen(false);
		UE_LOG(LogAsdaMinimap, Log, TEXT("[миникарта] смена канала отклонена: нотис %d «%s»"),
			LocId, (TS && LocId > 0) ? *TS->GetTextById(LocId) : TEXT("молча"));
	};
	// Гейты оригинала по порядку (world_map.c:7524-7560): мёртв — молча; 98004 — отряд; 98003 — тот же канал;
	//   98001 — карта 4/5/12/14 (особая локация); 98002 — подземелье (байт +68 записи карты). Общая точка с кнопкой 17500 —
	//   `AsdaMinimapChannelGate` (★ 26.10.2026, ключ asda.Minimap.ChannelGates; 0 — прежнее: только 98003 в журнал).
	if (CVarAsdaMinimapChannelGates.GetValueOnGameThread() != 0)
	{
		const int32 Gate = AsdaMinimapChannelGate(GI, /*bRow*/ true, Channel);
		if (Gate != 0)
		{
			Deny(Gate > 0 ? Gate : 0);
			if (Gate > 0) { AsdaMinimapChannelNotice(GI, Gate); }
			return;
		}
	}
	else if (Channel == Net->CurrentChannel) { Deny(98003); return; }
	// ★ 08.11.2026 [ИСПРАВЛЕНИЕ] (`asda.Minimap.ChannelMoveStop`): смена канала начинается — ход останавливается (4008 раньше 6084).
	AsdaStopMoveForChannelRequest(GI, this, Channel);
	if (!Net->SendChannelMove6084((uint8)Channel))
	{
		AsdaClearChannelMoveHold();   // запрос не ушёл — держать нечего
		UE_LOG(LogAsdaMinimap, Warning, TEXT("[миникарта] 6084 не отправлен (нет game-соединения)"));
		return;
	}
	// Эталон: панель закрывается сразу, подпись перерисовывается; фактический переход — по ответу 6015.
	SetChannelPanelOpen(false);
	UE_LOG(LogAsdaMinimap, Log, TEXT("[миникарта] запрошен переход на канал %d (6084)"), Channel + 1);
}
