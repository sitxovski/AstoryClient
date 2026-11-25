// Copyright (c) 2026 @sitxovski (Discord)
// Реализация свитков перемещения/призыва (окно ввода имени 1552, 5458/6134, 5459/6135/6136/6137).
//   Разбор эталона и поток — в шапке `AsdaScrollCallSubsystem.h` и в отчёте `Reports/owner_1006/wave2D_gift_townwar.md` §1.

#include "UI/AsdaScrollCallSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"            // фасад нотисов (Loc / MsgBox / PlankLoc)
#include "UI/AsdaWindowSubsystem.h"   // очередь окон-сообщений: 1552 (виды 59/60) и 1520 (1, 61)
#include "UI/AsdaUiHelpers.h"         // FormatPrintfArgs
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Net/NetOpcodes.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Login/AsdaLoginData.h"      // DecodeCp1251

#include "World/AsdaWorldSubsystem.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaScroll, Log, All);

// ★ 07.10.2026 (волна 2-Г, ревизия М-2) [ПОРТ]: дефект оригинала — признак «меня зовут» (байт +62 буфера 6136) снимает
//   ТОЛЬКО ответ 6137. Если запись TWOBUT (1, 61) снята молча (нотис поверх `itemBuyPanelRefreshOrHide`, снятие записей,
//   полный сброс очереди), признак остаётся навсегда и все следующие призывы отбрасываются без следа.
//   1 = признак снимается, как только записи (1, 61) в очереди больше нет (без 6137); 0 = как оригинал.
static ASDA_DEV_CVAR(int32, CVarReleaseCallLatch,
	TEXT("asda.Scroll.ReleaseCallLatch"), 1,
	TEXT("Дефект оригинала (волна 2-Г М-2): 1 = признак ожидающего призыва 6136 снимается при молчаливом снятии окна "
	     "подтверждения (1, 61); 0 = как оригинал (защёлка до ответа 6137)."),
	ECVF_Default);

namespace
{
	// Цвет плашки 221 отказов 5459: −11514 = 0xFFFFD306 (золотой).
	const FColor kPlankColor(0xFF, 0xD3, 0x06, 0xFF);
	// Длины тел (сервер: `sub_4C28F0(…, 6135/6136, 62, …)`, `(…, 5459, 84, …)`).
	constexpr int32 kLen62 = 62;
	constexpr int32 kLen84 = 84;
}

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════

void UAsdaScrollCallSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только КЛАДЁМ тело в очередь.
		HWave = Net->OnWave2DPacket.AddLambda([this](int32 Op, const TArray<uint8>& B)
		{
			if (Op == OP_NET_RECV_INVENTORY_ITEM_UPDATE || Op == OP_CAMP_WAR_BUILD_RESULT_TOOLTIP) { Pending.Push(TPair<int32, TArray<uint8>>(Op, B)); }
		});
		HMeet = Net->OnItemStatusNotice.AddLambda([this](const TArray<uint8>& B) { Pending.Push(TPair<int32, TArray<uint8>>((int32)OP_RECV_ITEM_STATUS_NOTICE, B)); });
	}
	UE_LOG(LogAsdaScroll, Log, TEXT("[свитки] подсистема поднята (окно 1552, 5458/6134, 5459/6135/6136/6137)"));
}

void UAsdaScrollCallSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnWave2DPacket.Remove(HWave);
		Net->OnItemStatusNotice.Remove(HMeet);
	}
	Super::Deinitialize();
}

void UAsdaScrollCallSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Смена карты: эталон буфер 6136 и ячейки окна 1552 не трогает (очередь окон переживает загрузку) — оставляем.
	//   Полный сброс: очередь окон-сообщений снята молча (`ClearMsgBoxQueue`) — запрос «меня зовут» без окна был бы
	//   вечным замком (закон «не защёлкивать состояние»), поэтому признак снимаем.
	if (!AsdaIsFullSessionReset(Reason)) { return; }
	bCallPending = false;
	CallBody.Reset();
	MeetCell = -1;
	SummonCell = -1;
	Pending.Clear();
	UE_LOG(LogAsdaScroll, Log, TEXT("[свитки] полный сброс (%s): признак запроса 6136 и ячейки окна 1552 сняты"), AsdaSessionResetReasonName(Reason));
}

void UAsdaScrollCallSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.ScrollCall");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	// ★ М-2: окно подтверждения (1, 61) снято молча — признак не держим (ключ `asda.Scroll.ReleaseCallLatch`).
	if (bCallPending && CVarReleaseCallLatch.GetValueOnGameThread() != 0)
	{
		const UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (WM && !WM->IsMsgBoxQueued(1, 61))
		{
			bCallPending = false;
			UE_LOG(LogAsdaScroll, Log, TEXT("[свитки] запись (1, 61) снята без ответа — признак запроса 6136 снят [ПОРТ М-2], 6137 не шлётся"));
		}
	}
	// ★ U7 (В1 дедупа №2): общая очередь `TAsdaNetQueue` (замок + Drain) вместо своего массива.
	TArray<TPair<int32, TArray<uint8>>> Batch;
	if (!Pending.Drain(Batch)) { return; }
	for (const TPair<int32, TArray<uint8>>& P : Batch)
	{
		if (P.Key == OP_RECV_ITEM_STATUS_NOTICE)              { OnMeetResult(P.Value); }
		else if (P.Key == OP_NET_RECV_INVENTORY_ITEM_UPDATE) { OnSummonResult(P.Value); }
		else if (P.Key == OP_CAMP_WAR_BUILD_RESULT_TOOLTIP)  { OnCallRequest(P.Value); }
	}
}

// ═════════════════════════════ ОКНО 1552 ═══════════════════════════════════════════════════════

bool UAsdaScrollCallSubsystem::OpenNameBox(int32 ContainerId, int32 CellIndex, int32 SubType)
{
	// `CItemManager_UseInventoryItem` адресует ячейку СУМКИ (`&g_InventoryBag + 57·слот`); сервер берёт предмет из
	//   контейнера 1 (`sub_4D61E0(this, …, 1, слот, …)` у обоих обработчиков). Иной контейнер — пакет ушёл бы на чужой
	//   предмет сумки с тем же номером: отказываем (как `UsePremiumItem`).
	if (ContainerId != 1)
	{
		UE_LOG(LogAsdaScroll, Warning, TEXT("[свитки] свиток (подтип %d) в контейнере %d — канал работает только с сумкой, окно не открыто"),
			SubType, ContainerId);
		return false;
	}
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	int32 Kind = 0, LocId = 0;
	if (SubType == 38)      { MeetCell = CellIndex;   Kind = 60; LocId = AsdaScrollLoc::kMeetPrompt; }     // `sub_92A67A` → +0x32F0
	else if (SubType == 64) { SummonCell = CellIndex; Kind = 59; LocId = AsdaScrollLoc::kSummonPrompt; }   // `sub_92C8D0` → +0x32F4
	else { return false; }
	TWeakObjectPtr<UAsdaScrollCallSubsystem> Self(this);
	const UUserWidget* W = WM->ShowGiftMsgBox(Kind, Loc(LocId), [Self, Kind](bool bOk, const FString& Name)
	{
		if (UAsdaScrollCallSubsystem* S = Self.Get()) { S->OnNameBoxAnswer(Kind, bOk, Name); }
	});
	UE_LOG(LogAsdaScroll, Log, TEXT("[свитки] подтип %d, ячейка сумки %d → окно 1552 вид %d (loc %d)%s"),
		SubType, CellIndex, Kind, LocId, W ? TEXT("") : TEXT(" — окно не показано"));
	return W != nullptr;
}

void UAsdaScrollCallSubsystem::OnNameBoxAnswer(int32 Kind, bool bOk, const FString& Name)
{
	// `OnSendItemUpgrade` case 1554/1555 (ui_core.c:57955-57989). 1555 — только закрытие (его делает очередь).
	if (!bOk) { UE_LOG(LogAsdaScroll, Log, TEXT("[свитки] окно 1552 вид %d: «Отмена» 1555"), Kind); return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	// Имя эталон не проверяет (пустое тоже уходит: сервер ответит «нет такого игрока»).
	if (Kind == 59)
	{
		Net->SendScrollSummon6134(Net->SelectedCharId, SummonCell, Name);   // `netSendNamedTargetCmd_6134`
	}
	else if (Kind == 60)
	{
		Net->SendScrollMeet5458(MeetCell, Name);                             // `net_sendSecureCmdPacket`
	}
	// `objClearArraySlot(g_pQuickSlotBar, …)` эталона снимает признак «клавиша слота хотбара зажата» (поле ввода съело
	//   отпускание). [ПОРТ] у порта нажатие клавиши хотбара флагом не хранится (ввод UE сбрасывается при смене фокуса).
}

// ═════════════════════════════ ОТВЕТЫ ═════════════════════════════════════════════════════════

void UAsdaScrollCallSubsystem::OnSummonResult(const TArray<uint8>& B)
{
	// `netRecvInventoryItemUpdate@0xDCE180`: `memcpy(itemRecordBuf, Src + 9, 0x3E)`.
	if (B.Num() < kLen62) { return; }
	const int32 Code = B[4];
	UGameInstance* GI = GetGameInstance();
	if (Code == 1)
	{
		const uint8* Rec = B.GetData() + 5;
		const int32 Count = AsdaBytes::RdI32(Rec + 9);
		const int32 Cont  = Rec[4];
		const int32 Slot  = AsdaBytes::RdI16(Rec + 5);
		if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
		{
			Inv->BeginCellBatch();   // `CClass_BuildItemRecord(g_pQuickSlotBar, 1/2, …)` + `CInventoryUI_ResetAndRefresh` — один раз
			if (Count == 0)
			{
				if (Cont == 1 || Cont == 2) { Inv->ClearCell(Cont, Slot); }   // `memset(&g_Inventory… + 57·слот, 0, 0x39)`
			}
			else if (Count > 0 && (Cont == 1 || Cont == 2))
			{
				TArray<uint8> R57; R57.Append(Rec, 57);
				Inv->ApplyExternalRecord(R57, UAsdaInventorySubsystem::EExternalRecordMode::AsIs);   // запись целиком
			}
			Inv->EndCellBatch();
		}
		NoticeMsgBox(AsdaScrollLoc::kSummonOk);   // 104004
		UE_LOG(LogAsdaScroll, Log, TEXT("[свитки] 6135 код 1: свиток призыва израсходован (контейнер %d, слот %d, осталось %d)"), Cont, Slot, Count);
		return;
	}
	if (Code == 0 || (Code >= 2 && Code <= 11))
	{
		NoticeMsgBox(Code == 0 ? AsdaScrollLoc::kSummonBase : 104005 + Code);   // 0 → 104006; 2…11 → 104007…104016
	}
	else
	{
		UE_LOG(LogAsdaScroll, Log, TEXT("[свитки] 6135 код %d — у эталона ветки нет (молча)"), Code);
	}
}

void UAsdaScrollCallSubsystem::OnMeetResult(const TArray<uint8>& B)
{
	// `uiTooltip_ShowItemStatusNotice@0xF26880` (ui_core.c:97921-97995).
	if (B.Num() < kLen84) { return; }
	const int32 Code = B[0];
	UGameInstance* GI = GetGameInstance();
	if (Code == 1)
	{
		const uint8* Rec = B.GetData() + 25;
		const int32 ItemCode = AsdaBytes::RdI32(Rec);
		// Откат: `CItemManager_GetTable2Record(код)` → `sub_9339C8(…, cooldownBase)` (только при записи таблицы).
		const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		FAsdaSpecialItemlistRow Sp;
		if (IT && IT->GetSpecialItem(ItemCode, Sp))
		{
			if (UAsdaHotbarSubsystem* Hb = GI->GetSubsystem<UAsdaHotbarSubsystem>())
			{
				Hb->PushItemCooldownRaw(ItemCode, Rec[4], AsdaBytes::RdI16(Rec + 5), Sp.LinkGroupId, Sp.CooldownBase);
			}
		}
		// `netPortalChannelTransfer(a2)`: +1 IP[16], +17 порт, +19 X, +21 Y, +23 карта.
		const FString Ip = AsdaBytes::AnsiFieldTrim(B.GetData() + 1, 16);   // N7 (дедуп №2): общая точка разбора IP
		const int32 Port = (int32)AsdaBytes::RdU16(B.GetData() + 17);
		const int32 TX   = (int32)AsdaBytes::RdU16(B.GetData() + 19);
		const int32 TZ   = (int32)AsdaBytes::RdU16(B.GetData() + 21);
		const int32 Map  = (int32)AsdaBytes::RdU16(B.GetData() + 23);
		NoticeTrace.Add(0);   // след: «код 1 — переход» (нотиса у эталона нет)
		// ★ М-6: `netPortalChannelTransfer` начинается с `if (CharManager_GetLocalPlayer())` — без своего персонажа перехода нет.
		const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!WS || !IsValid(WS->LocalPlayerActor))
		{
			UE_LOG(LogAsdaScroll, Warning, TEXT("[свитки] 5459 код 1: своего персонажа нет — переход не начат (как эталон)"));
			return;
		}
		if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
		{
			Net->StartScrollChannelTransfer(Ip, Port, Map, TX, TZ, TEXT("свиток перемещения 5459"));
		}
		UE_LOG(LogAsdaScroll, Log, TEXT("[свитки] 5459 код 1: предмет %d — откат и переход на %s:%d карта %d (%d,%d)"),
			ItemCode, *Ip, Port, Map, TX, TZ);
		return;
	}
	static const TMap<int32, int32> kLoc = {
		{ 0, 12837 }, { 2, 12838 }, { 3, 12839 }, { 4, 12844 }, { 5, 12853 }, { 6, 12846 },
		{ 7, 12165 }, { 8, 12169 }, { 9, 13919 }, { 0x1A, 12857 },
	};
	if (const int32* L = kLoc.Find(Code)) { NoticePlank(*L); }
	else { UE_LOG(LogAsdaScroll, Log, TEXT("[свитки] 5459 код %d — у эталона ветки нет (молча)"), Code); }
}

void UAsdaScrollCallSubsystem::OnCallRequest(const TArray<uint8>& B)
{
	// `campWarBuildResultTooltip@0xDCED80`: `if (this[62] != 1) { копия 62 б; TWOBUT(1, 61, sprintf(104100, имя+24)); this[62] = 1; }`.
	if (B.Num() < kLen62) { return; }
	if (bCallPending)
	{
		UE_LOG(LogAsdaScroll, Log, TEXT("[свитки] 6136: прежний запрос ещё без ответа — новый молча отброшен (байт +62)"));
		return;
	}
	CallBody = B;
	const FString Caller = FAsdaLoginData::DecodeCp1251(B.GetData() + 24, 20).TrimStartAndEnd();
	const FString Text = AsdaUi::FormatPrintfArgs(Loc(AsdaScrollLoc::kCallPrompt), { Caller });
	NoticeTrace.Add(AsdaScrollLoc::kCallPrompt);
	LastNoticeText = Text;
	bCallPending = true;
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { bCallPending = false; return; }   // не защёлкивать без окна
	TWeakObjectPtr<UAsdaScrollCallSubsystem> Self(this);
	const UUserWidget* W = AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Text, [Self](bool bYes)
	{
		UAsdaScrollCallSubsystem* S = Self.Get();
		if (!S) { return; }
		UGameInstance* G = S->GetGameInstance();
		UAsdaNetSubsystem* N = G ? G->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		// «Да» — `case 0x3D: net_SendActionWithAntiCheatMask(byte_2C2F380, 1)` (ui_core.c:57082);
		//   «Нет» — `case 0x3D: …(byte_2C2F380, 0)` (:57709) → ответ 10. Признак снимает сам отправитель (`srcAction[62] = 0`).
		if (N) { N->SendSummonAnswer6137(S->CallBody, bYes ? 1 : 10); }
		S->bCallPending = false;
	}, /*GaugeGateSec*/0.f, AsdaScrollLoc::kCallYes, AsdaScrollLoc::kCallNo, /*EtalonNoticeId*/61, /*EtalonKind*/1);
	if (!W && !WM->IsMsgBoxQueued(1, 61)) { bCallPending = false; }   // окно не встало и записи нет — признак не держим
	UE_LOG(LogAsdaScroll, Log, TEXT("[свитки] 6136: меня зовёт «%s» → TWOBUT (1, 61)"), *Caller);
}

// ═════════════════════════════ НОТИСЫ ══════════════════════════════════════════════════════════

void UAsdaScrollCallSubsystem::NoticeMsgBox(int32 LocId)
{
	NoticeTrace.Add(LocId);
	LastNoticeText = Loc(LocId);
	AsdaNotice::MsgBoxLoc(GetGameInstance(), EAsdaMsgCategory::ItemRefused, LocId);
}

void UAsdaScrollCallSubsystem::NoticePlank(int32 LocId)
{
	NoticeTrace.Add(LocId);
	LastNoticeText = Loc(LocId);
	AsdaNotice::PlankLoc(GetGameInstance(), EAsdaMsgCategory::ItemRefused, LocId, kPlankColor, 10000.f, 5459);
}

FString UAsdaScrollCallSubsystem::Loc(int32 LocId) const
{
	return AsdaNotice::Loc(GetGameInstance(), LocId);
}
