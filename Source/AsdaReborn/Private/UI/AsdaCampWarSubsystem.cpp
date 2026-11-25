// Copyright (c) 2026 @sitxovski (Discord)
// Реализация «Битвы армий» (CampWar): регистрация и вход — окна 47000 / 49530 / 49540 / 48099, пакеты 6701, 6704–6715, 6740.
//   Весь разбор эталона (поля `CCampWar` со смещениями, проверенными дизасмом, ветки, коды сервера) — в шапке
//   `AsdaCampWarSubsystem.h` и в `Reports/faction_select/REG_SPEC.md`; узлы ассетов — `campwar_wbp_tree.txt`.

#include "UI/AsdaCampWarSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "UI/AsdaUiHelpers.h"   // ★ R1: прямое включение (раньше приходило транзитивно)
#include "UI/AsdaCampWarBattleSubsystem.h"   // ★ 30.10.2026: боевая фаза (HUD, боевые поля ResetState, таймер боя, 6715)
#include "AsdaDevConsole.h"           // ASDA_DEV_CVAR / ASDA_DEV_CONSOLE
#include "UI/AsdaJuiWindowHandle.h"   // каркас окна JUI (Ensure + зона рамки) и FAsdaGaugeFill
#include "UI/AsdaNotice.h"            // фасад нотисов (Loc / MsgBox)
#include "UI/AsdaWindowSubsystem.h"

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Net/NetOpcodes.h"
#include "Login/AsdaButtonRelay.h"
#include "Npc/AsdaNpcDialogSubsystem.h"   // 6740 код 0 — текст в баннер окна NPC (`npcExchangeUiSetText`)
#include "Text/AsdaTextSubsystem.h"       // FormatById — подстановка [ss1]/[sd1]
#include "UI/AsdaInventorySubsystem.h"    // 6705 код 1 — запись предмета
#include "World/AsdaClanFlag.h"           // знак лагеря (`CampWar_MarkImage`) и «карта кампвара» (+69)
#include "World/AsdaLoadManagerSubsystem.h"   // текущая карта и строка таблицы карт (имя битвы)
#include "World/AsdaMapTable.h"
#include "World/AsdaWorldSubsystem.h"     // своя армия (`SelfCampId` = `CCampWar+0xB7C`)

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCampWar, Log, All);

// ═════════════════════════════ КЛЮЧИ (разбор — в шапке .h) ═══════════════════════════════════════
static ASDA_DEV_CVAR(int32, CVarRegion2Name,
	TEXT("asda.CampWar.Region2Name"), 1,
	TEXT("Дефект оригинала Д-КВ-2: 1 = имя битвы региона 2 (Пламения) — служебное имя карты поля боя 19, как у регионов 0/1; "
	     "0 = как оригинал (запись карты −1 — у оригинала 32 б мусора перед таблицей; у порта пустая строка)."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarKeepOnMapChange,
	TEXT("asda.CampWar.KeepOnMapChange"), 1,
	TEXT("[ИСПРАВЛЕНИЕ ПОРТА] 1 = смена карты НЕ сбрасывает модель «Битвы армий» (у эталона `CCampWar` статический: уход с карты 19 "
	     "сбрасывает 5055, а вход на неё токены 5055 СОХРАНЯЕТ до хендшейка 20000); 0 = прежний порт (сброс и при смене карты — "
	     "токены 5055 стирались до 20000, HUD боя гас на загрузке карты 19)."),
	ECVF_Default);

namespace
{
	// Окна и их виджеты. Ассеты уже импортированы заходом JUI (не перегенерировать!).
	const FName  kJoinId(TEXT("CAMPWAR_Join"));          // 47000 (ключ строки эталонной ESC-таблицы)
	const FName  kIngId(TEXT("CAMPWAR_Ing"));            // 49530
	const FName  kInListId(TEXT("CAMPWAR_In_List"));     // 49540
	const FName  kConfirmId(TEXT("CAMPWAR_JOIN_LOGIN")); // 48099
	const TCHAR* kJoinWbp    = TEXT("/Game/AsdaAssets/UI/Player_UI/CampWar/WBP_CAMPWAR_Join.WBP_CAMPWAR_Join_C");
	const TCHAR* kIngWbp     = TEXT("/Game/AsdaAssets/UI/Player_UI/CampWar/WBP_CAMPWAR_Ing.WBP_CAMPWAR_Ing_C");
	const TCHAR* kInListWbp  = TEXT("/Game/AsdaAssets/UI/Player_UI/CampWar/WBP_CAMPWAR_In_List.WBP_CAMPWAR_In_List_C");
	const TCHAR* kConfirmWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/CampWar/WBP_CAMPWAR_JOIN_LOGIN.WBP_CAMPWAR_JOIN_LOGIN_C");
	const TCHAR* kBaseTablePath = TEXT("/Game/AsdaAssets/Data/DT_CampWarBase.DT_CampWarBase");

	// Узлы WBP 47000 по строкам армий i (знак, имя, «N/макс», «Шанс», полоса ES0/ES1, процент) — дамп `campwar_wbp_tree.txt`.
	const TCHAR* kJMark[3]    = { TEXT("CAMPWAR_Join_1_MARK0_88_0"), TEXT("CAMPWAR_Join_2_MARK0_105_0"), TEXT("CAMPWAR_Join_3_MARK0_116_0") };
	const TCHAR* kJName[3]    = { TEXT("CAMPWAR_Join_1_T1"), TEXT("CAMPWAR_Join_2_T2"), TEXT("CAMPWAR_Join_3_T1") };
	const TCHAR* kJCount[3]   = { TEXT("CAMPWAR_Join_1_T2"), TEXT("CAMPWAR_Join_2_T2_1"), TEXT("CAMPWAR_Join_3_T2") };
	const TCHAR* kJChance[3]  = { TEXT("CAMPWAR_Join_1_T3"), TEXT("CAMPWAR_Join_2_T3"), TEXT("CAMPWAR_Join_3_T3") };
	const TCHAR* kJBarBack[3] = { TEXT("CAMPWAR_Join_1_SLIDE_93_0"), TEXT("CAMPWAR_Join_2_SLIDE_102_0"), TEXT("CAMPWAR_Join_3_SLIDE_113_0") };
	const TCHAR* kJBarFill[3] = { TEXT("CAMPWAR_Join_1_SLIDE_93_1"), TEXT("CAMPWAR_Join_2_SLIDE_102_1"), TEXT("CAMPWAR_Join_3_SLIDE_113_1") };
	const TCHAR* kJPct[3]     = { TEXT("CAMPWAR_Join_1_SLIDE_T"), TEXT("CAMPWAR_Join_2_SLIDE_T"), TEXT("CAMPWAR_Join_3_SLIDE_T") };
	// Строки списка битв j (47400+10j). ★ У IN3 порядок узлов в .JUI 47421, 47423, 47422 — `TXT01` = заявка, `TXT02` = загруженность.
	const TCHAR* kJRow[3]     = { TEXT("CAMPWAR_Join_4_IN1_grp"), TEXT("CAMPWAR_Join_4_IN2_grp"), TEXT("CAMPWAR_Join_4_IN3_grp") };
	const TCHAR* kJRowNum[3]  = { TEXT("CAMPWAR_Join_4_IN1_TXT03"), TEXT("CAMPWAR_Join_4_IN2_TXT03"), TEXT("CAMPWAR_Join_4_IN3_TXT03") };
	const TCHAR* kJRowName[3] = { TEXT("CAMPWAR_Join_4_IN1_TXT00"), TEXT("CAMPWAR_Join_4_IN2_TXT00"), TEXT("CAMPWAR_Join_4_IN3_TXT00") };
	const TCHAR* kJRowState[3]= { TEXT("CAMPWAR_Join_4_IN1_TXT01"), TEXT("CAMPWAR_Join_4_IN2_TXT01"), TEXT("CAMPWAR_Join_4_IN3_TXT02") };
	const TCHAR* kJRowApply[3]= { TEXT("CAMPWAR_Join_4_IN1_TXT02"), TEXT("CAMPWAR_Join_4_IN2_TXT02"), TEXT("CAMPWAR_Join_4_IN3_TXT01") };
	const TCHAR* kJRowAni[3]  = { TEXT("CAMPWAR_Join_ani1_37_0"), TEXT("CAMPWAR_Join_ani2_28_0"), TEXT("CAMPWAR_Join_ani3_19_0") };
	const TCHAR* kJRowHover[3]= { TEXT("CAMPWAR_Join_4_IN1_b_36_0"), TEXT("CAMPWAR_Join_4_IN2_b_27_0"), TEXT("CAMPWAR_Join_4_IN3_b_18_0") };
	const TCHAR* kJRowBlack[3]= { TEXT("black1_39_0"), TEXT("black2_30_0"), TEXT("black3_21_0") };
	// Узлы 49540 по строкам i (49541+10i).
	const TCHAR* kLRow[3]   = { TEXT("CAMPWAR_In_List_1_0_M_grp"), TEXT("CAMPWAR_In_List_1_1_M_grp"), TEXT("CAMPWAR_In_List_1_2_M_grp") };
	const TCHAR* kLName[3]  = { TEXT("CAMPWAR_In_List_1_Text1_0"), TEXT("CAMPWAR_In_List_1_Text2_0"), TEXT("CAMPWAR_In_List_1_Text3_0") };
	const TCHAR* kLPlace[3] = { TEXT("CAMPWAR_In_List_1_Text1_1"), TEXT("CAMPWAR_In_List_1_Text2_1"), TEXT("CAMPWAR_In_List_1_Text3_1") };
	const TCHAR* kLHover[3] = { TEXT("CAMPWAR_In_List_1_B1_10_0"), TEXT("CAMPWAR_In_List_1_B2_19_0"), TEXT("CAMPWAR_In_List_1_B3_28_0") };
	const TCHAR* kLBlack[3] = { TEXT("L_black_1_11_0"), TEXT("L_black_2_20_0"), TEXT("L_black_3_29_0") };

	// Записать текст в узел (пустой указатель — ничего).
	void SetT(UTextBlock* T, const FString& S) { if (T) { T->SetText(FText::FromString(S)); } }
	// Текст узла (пустой указатель — пустая строка).
	FString GetT(const UTextBlock* T) { return T ? T->GetText().ToString() : FString(); }
	// Видимость узла как у `CControl_SetVisible` (видимый — без перехвата клика: клики принимают кнопки/опрос мыши).
	void SetVis(UWidget* W, bool b) { if (W) { W->SetVisibility(b ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); } }
	// Узел видим.
	bool IsVis(const UWidget* W) { return W && W->GetVisibility() != ESlateVisibility::Collapsed && W->GetVisibility() != ESlateVisibility::Hidden; }
	// Позиция узла в канвасе родителя (нет слота — ноль). ★ R5-fix-b 13.11.2026: была своя копия `AsdaUi::SlotPos`
	//   (`UCanvasPanelSlot::GetPosition()` = `GetOffsets()` Left/Top — CanvasPanelSlot.cpp:171/214) — берём общую точку.
	using AsdaUi::SlotPos;
	// Поставить узел в позицию канваса родителя.
	void SetSlotPos(UWidget* W, const FVector2D& P) { if (UCanvasPanelSlot* S = W ? Cast<UCanvasPanelSlot>(W->Slot) : nullptr) { S->SetPosition(P); } }
}

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════

void UAsdaCampWarSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только КЛАДЁМ тело в очередь (6703 — окну выбора армии).
		HPacket = Net->OnCampWarSquadPacket.AddLambda([this](int32 Op, const TArray<uint8>& B)
		{
			if (Op != OP_CLAN_CREATE_RESULT) { Pending.Push(TPair<int32, TArray<uint8>>(Op, B)); }
		});
		// `Recv_ClanMyMembership@0xCA4AB0` кладёт очки в +0xB82, звание в +0xB86 — их читает 6705 код 1.
		HSelfFaction = Net->OnSelfFaction.AddWeakLambda(this, [this](int32, int32 Pts, int32 Gr) { LastSelfPoints = Pts; LastSelfGrade = Gr; });
	}
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		// [ОРИГИНАЛ] ESC: признак +0x228 у 47000 (`CCampWar_InitJoinInfo`), ветка события 0x15 → `sub_C9B2C0`.
		WM->RegisterEscCloser(kJoinId, FSimpleDelegate::CreateUObject(this, &UAsdaCampWarSubsystem::CloseJoinWindow));
	}
	BaseTable = LoadObject<UDataTable>(nullptr, kBaseTablePath);
	UE_LOG(LogAsdaCampWar, Log, TEXT("[битва армий] подсистема поднята, DT_CampWarBase: %s (%d строк)"),
		BaseTable ? TEXT("есть") : TEXT("НЕТ"), BaseTable ? BaseTable->GetRowMap().Num() : 0);
}

void UAsdaCampWarSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnCampWarSquadPacket.Remove(HPacket);
		Net->OnSelfFaction.Remove(HSelfFaction);
	}
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterEscCloser(kJoinId);
	}
	ResetWidgets();
	Super::Deinitialize();
}

void UAsdaCampWarSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// ★ [ИСПРАВЛЕНИЕ ПОРТА] 30.10.2026: смена карты — не сброс (объект эталона статический; см. ключ).
	if (!AsdaIsFullSessionReset(Reason) && CVarKeepOnMapChange.GetValueOnGameThread() != 0) { return; }
	// Окна оригинала — контролы `CCampWar`, перезагрузка интерфейса прячет их (корни `Visible = 0`).
	if (bConfirmOpen) { HideConfirm(); }
	if (bInListOpen) { CloseInList(); }
	if (bIngShown) { HideIng(); }
	if (bJoinOpen) { CloseJoinWindow(); }
	// Модель — как после `CCampWar_ctor` + `CCampWar_InitControls` (`CCampWar_ResetState` при создании контролов).
	ResetState();
	MapType = 0;
	for (int32 I = 0; I < kRegions; ++I) { TeamCount[I] = WinCount[I] = LoseCount[I] = 0; RegistCount[I] = 0; }
	TotalGame = 0; DayGame = 0; LevelMin = LevelMax = 0; Day = 0;
	SelRow = -1; InListSel = -1;
	bMainInfoReceived = false;
	LastMainInfo.Reset();
	LastSelfPoints = 0; LastSelfGrade = 0;
	PendingPortal = 0; bPendingLeaveCampMap = false;
	Pending.Clear();
	Mouse.Reset(); HoverRow = -1; bInListHover = false;
	UE_LOG(LogAsdaCampWar, Log, TEXT("[битва армий] сброс сессии (%s): окна скрыты, модель сброшена"), AsdaSessionResetReasonName(Reason));
}

void UAsdaCampWarSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.CampWar");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	// Пакеты — из Tick (закон no-reentrant-net-callbacks).
	if (TArray<TPair<int32, TArray<uint8>>> Batch; Pending.Drain(Batch))
	{
		for (const TPair<int32, TArray<uint8>>& P : Batch) { HandlePacket(P.Key, P.Value); }
	}
	// 5055 код 1 (переход), отложенный из сетевого колбэка.
	if (PendingPortal != 0)
	{
		UGameInstance* GI0 = GetGameInstance();
		const bool bHudOn = (PendingPortal == 1);
		PendingPortal = 0;
		// [ОРИГИНАЛ] `Recv_PortalChannelChange@0xF45BD0`: HUD разрешён → `ToggleVisible(1)` + `uiCtrlHideAtField374` (скрыть 49530);
		//   уходим С карты кампвара (`g_pLocalEntity+69 == 1`) → `CCampWar_ResetState`.
		// ★ 30.10.2026 (боевая фаза): сам переключатель HUD — `campWarWnd_ToggleVisible(bHudOn, bCampWarVisible)` (порядок эталона:
		//   Toggle → `uiCtrlHideAtField374` → ResetState при уходе с кампварной карты).
		if (UAsdaCampWarBattleSubsystem* Battle = GI0 ? GI0->GetSubsystem<UAsdaCampWarBattleSubsystem>() : nullptr)
		{
			Battle->ToggleVisible(bHudOn, bPendingLeaveParty);
		}
		if (bHudOn) { HideIng(); }
		if (bPendingLeaveCampMap) { ResetState(); }
		bPendingLeaveCampMap = false;
		bPendingLeaveParty = false;
	}
	// `ClanTimer_Tick` — из `MainLoop_Tick_SoundUpdate` раз в 1000 мс в игровом состоянии (`g_GameState ∈ {1, 8}`).
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const double NowMs = FPlatformTime::Seconds() * 1000.0;
	if (Net && Net->SelectedCharId > 0 && GI->GetWorld() && (NowMs - LastClanTickMs) > 1000.0)
	{
		LastClanTickMs = NowMs;
		ClanTimerTick(0);
		ClanTimerTick(1);
	}
	if (bJoinOpen || bInListOpen)
	{
		Mouse.Update();
		if (AsdaUi::IsModalDispatchBlocked(GI)) { Mouse.Reset(); return; }
		TickMouse();
	}
}

// ═════════════════════════════ МОДЕЛЬ ═════════════════════════════════════════════════════════

const FAsdaCampWarBaseRow* UAsdaCampWarSubsystem::GetBaseRow(int32 Slot) const
{
	// `sub_CA1440`: `0 ≤ idx ≤ count` (★ `idx == count` у эталона читает за концом — дефект; порт: только существующие строки).
	if (!BaseTable || Slot < 0) { return nullptr; }
	return BaseTable->FindRow<FAsdaCampWarBaseRow>(FName(*FString::FromInt(Slot)), TEXT("CampWar"), /*bWarnIfMissing*/ false);
}

int32 UAsdaCampWarSubsystem::CurrentMapSlot() const
{
	// `g_nCurMapId`: 3 → 0, 0 → 1, 7 → 2, иначе 0.
	const UAsdaLoadManagerSubsystem* LM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	const int32 Map = LM ? LM->CurrentMapId : -1;
	return (Map == 0) ? 1 : (Map == 7) ? 2 : 0;
}

FString UAsdaCampWarSubsystem::BattleName(int32 Region) const
{
	// `buildLocalPlayerNameStr@0xCA1580` (имя врёт): a2 ∈ {0, 1} → карта 19, иначе −1; имя = `g_aMapEntityTable[карта]+0`
	//   (служебное имя папки карты — «battlefield»), не локализованное.
	int32 MapId = (Region == 0 || Region == 1) ? 19 : -1;
	if (MapId < 0 && CVarRegion2Name.GetValueOnGameThread() != 0) { MapId = 19; }   // [ИСПРАВЛЕНИЕ] Д-КВ-2
	const UAsdaLoadManagerSubsystem* LM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	const FAsdaMapRow* Row = (LM && MapId >= 0) ? LM->GetMapRow(MapId) : nullptr;
	return Row ? Row->DirName : FString();
}

void UAsdaCampWarSubsystem::ResetState()
{
	// `CCampWar_ResetState@0xC96F20` (REG_SPEC.md §2.0): три k-массива, строка 0 списка (`sub_C96360`), регион вызова, токены,
	//   отсчёт 48099, таймер боя, время расписания, число битв = 1, последняя заявка = −1, `campWarSlotsHideAll`.
	for (int32 K = 0; K < kRegions; ++K) { RegGameIndex[K] = -1; RegIndex[K] = -1; WaitBefore[K] = -1; GameCount[K] = 1; }
	RowRegion[0] = -1; RowLoad[0] = 0; RowApplied[0] = 0;
	CallRegion = -1;
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->CampWarToken1 = 0xFF;   // +0x2308 = −1
		Net->CampWarToken2 = 0xFF;   // +0x2304 = −1
	}
	Countdown = 0; bCountdown = false; bBattleTimer = false;
	StartH = StartM = EndH = EndM = 0;
	LastRegIndex = -1;
	SlotsHideAll();
	// ★ 30.10.2026: боевая часть того же `CCampWar_ResetState` (очки, 10 слотов точек, число точек, таймер мин/сек, +0x235A).
	if (UAsdaCampWarBattleSubsystem* Battle = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCampWarBattleSubsystem>() : nullptr)
	{
		Battle->OnResetState();
	}
}

void UAsdaCampWarSubsystem::OnPortalTransfer5055(bool bHudOn, int32 TargetMapId)
{
	// Из сетевого колбэка — только запомнить; «стоим на карте кампвара» читаем СЕЙЧАС (до смены карты).
	PendingPortal = bHudOn ? 1 : 2;
	bPendingLeaveCampMap = UAsdaClanFlag::IsCampWarMap(GetGameInstance());
	// ★ 30.10.2026: `bCampWarVisible = (*(g_aMapEntityTable[цель] + 69) == 1)` — a3 переключателя HUD (выйти из отряда).
	const UAsdaLoadManagerSubsystem* LM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	const FAsdaMapRow* Row = (LM && TargetMapId >= 0) ? LM->GetMapRow(TargetMapId) : nullptr;
	bPendingLeaveParty = Row && Row->CampwarPkFlag == 1;
}

// ═════════════════════════════ ОКНА: СОЗДАНИЕ ═════════════════════════════════════════════════

bool UAsdaCampWarSubsystem::EnsureJoin()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kJoinId, kJoinWbp, kJoinZ).Ensure(GetGameInstance(), JoinWidget, bJoinReady,
		[this](UAsdaWindowSubsystem&) { JoinWidget = nullptr; bJoinReady = false; bJoinOpen = false; },
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupJoin();
			FAsdaJuiWindowHandle::RegisterFrameZone(WM, kJoinId, this, JoinWidget, bJoinOpen, TEXT("CAMPWAR_Join_0_%d"), 9);
		},
		[]() { UE_LOG(LogAsdaCampWar, Error, TEXT("[битва армий] WBP_CAMPWAR_Join не загрузился (%s)"), kJoinWbp); });
	return FAsdaJuiWindowHandle::Succeeded(R) && bJoinReady;
}

bool UAsdaCampWarSubsystem::EnsureIng()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kIngId, kIngWbp, kIngZ).Ensure(GetGameInstance(), IngWidget, bIngReady,
		[this](UAsdaWindowSubsystem&) { IngWidget = nullptr; bIngReady = false; bIngShown = false; },
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupIng();
			// У 49530 рамки нет — зона = сама кнопка (`Mac_Btn`).
			WM.RegisterUiZone(kIngId, FAsdaUiZoneRect::CreateWeakLambda(this, [this](FVector2D& Mn, FVector2D& Mx)
			{
				return bIngShown && IngBtn && AsdaUi::WidgetAbsRect(IngBtn, Mn, Mx);
			}));
		},
		[]() { UE_LOG(LogAsdaCampWar, Error, TEXT("[битва армий] WBP_CAMPWAR_Ing не загрузился (%s)"), kIngWbp); });
	return FAsdaJuiWindowHandle::Succeeded(R) && bIngReady;
}

bool UAsdaCampWarSubsystem::EnsureInList()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kInListId, kInListWbp, kInListZ).Ensure(GetGameInstance(), InListWidget, bInListReady,
		[this](UAsdaWindowSubsystem&) { InListWidget = nullptr; bInListReady = false; bInListOpen = false; },
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupInList();
			FAsdaJuiWindowHandle::RegisterFrameZone(WM, kInListId, this, InListWidget, bInListOpen, TEXT("CAMPWAR_In_List_0_%d"), 9);
		},
		[]() { UE_LOG(LogAsdaCampWar, Error, TEXT("[битва армий] WBP_CAMPWAR_In_List не загрузился (%s)"), kInListWbp); });
	return FAsdaJuiWindowHandle::Succeeded(R) && bInListReady;
}

bool UAsdaCampWarSubsystem::EnsureConfirm()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kConfirmId, kConfirmWbp, kConfirmZ).Ensure(GetGameInstance(), ConfirmWidget, bConfirmReady,
		[this](UAsdaWindowSubsystem&) { ConfirmWidget = nullptr; bConfirmReady = false; bConfirmOpen = false; },
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupConfirm();
			FAsdaJuiWindowHandle::RegisterFrameZone(WM, kConfirmId, this, ConfirmWidget, bConfirmOpen, TEXT("CAMPWAR_JOIN_LOGIN_0_%d"), 9);
		},
		[]() { UE_LOG(LogAsdaCampWar, Error, TEXT("[битва армий] WBP_CAMPWAR_JOIN_LOGIN не загрузился (%s)"), kConfirmWbp); });
	return FAsdaJuiWindowHandle::Succeeded(R) && bConfirmReady;
}

void UAsdaCampWarSubsystem::ResetWidgets()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		for (const FName& Id : { kJoinId, kIngId, kInListId, kConfirmId }) { WM->UnregisterUiZone(Id); WM->CloseWindow(Id); }
	}
	JoinWidget = IngWidget = InListWidget = ConfirmWidget = nullptr;
	bJoinReady = bIngReady = bInListReady = bConfirmReady = false;
	bJoinOpen = bIngShown = bInListOpen = bConfirmOpen = false;
	JSelFrame.Reset(); LSelFrame.Reset();
	Relays.Reset();
}

void UAsdaCampWarSubsystem::SetupJoin()
{
	bJoinReady = false;
	if (!JoinWidget) { return; }
	auto F = [this](const TCHAR* N) { return JoinWidget->GetWidgetFromName(FName(N)); };
	auto T = [&F](const TCHAR* N) { return Cast<UTextBlock>(F(N)); };
	JTitle = T(TEXT("CAMPWAR_Join_text"));          JGameCap = T(TEXT("CAMPWAR_Join_0_M1_T1")); JGameVal = T(TEXT("CAMPWAR_Join_0_M1_T2"));
	JDayCap = T(TEXT("CAMPWAR_Join_0_M2_T1"));      JDayVal = T(TEXT("CAMPWAR_Join_0_M2_T2"));
	JDay1 = T(TEXT("CAMWAR_day1"));                 JDay2 = T(TEXT("CAMWAR_day2"));
	JStartCap = T(TEXT("CAMPWAR_Join_0_M1_T3"));    JStartVal = T(TEXT("CAMPWAR_Join_0_M1_T4"));
	JLevelCap = T(TEXT("CAMPWAR_Join_0_M1_T5"));    JLevelVal = T(TEXT("CAMPWAR_Join_0_M1_T6"));
	JEndCap = T(TEXT("CAMPWAR_Join_0_M2_T3"));      JEndVal = T(TEXT("CAMPWAR_Join_0_M2_T4"));
	JListCap = T(TEXT("CAMPWAR_Join_4_Text_top"));  JHint = T(TEXT("CAMPWAR_Join_JJ_Text"));
	JBtnJoin = Cast<UButton>(F(TEXT("CAMPWAR_Join_OK_B")));   JBtnJoinText = T(TEXT("CAMPWAR_Join_OK_B_Text"));
	JBtnEnter = Cast<UButton>(F(TEXT("CAMPWAR_Join_OK_2B"))); JBtnEnterText = T(TEXT("CAMPWAR_Join_OK_2B_Text"));
	JBtnClose = Cast<UButton>(F(TEXT("CAMPWAR_Join_NO_B")));  JBtnCloseText = T(TEXT("CAMPWAR_Join_NO_B_Text"));
	JBtnX = Cast<UButton>(F(TEXT("CAMPWAR_Join_BTN00")));
	for (int32 I = 0; I < 3; ++I)
	{
		JMark[I] = Cast<UImage>(F(kJMark[I]));     JName[I] = T(kJName[I]);   JCount[I] = T(kJCount[I]); JChance[I] = T(kJChance[I]);
		JBarBack[I] = Cast<UImage>(F(kJBarBack[I])); JBarFill[I] = Cast<UImage>(F(kJBarFill[I])); JPct[I] = T(kJPct[I]);
		JRow[I] = F(kJRow[I]);   JRowNum[I] = T(kJRowNum[I]);   JRowName[I] = T(kJRowName[I]);   JRowApply[I] = T(kJRowApply[I]);
		UAsdaRichText::CaptureFmt(JRowState[I], T(kJRowState[I]));
		JRowAni[I] = F(kJRowAni[I]);   JRowHover[I] = Cast<UImage>(F(kJRowHover[I]));   JRowBlack[I] = Cast<UImage>(F(kJRowBlack[I]));
		JRowBase[I] = SlotPos(JRow[I]);
	}
	JSelFrame.Reset();
	for (int32 K = 0; K < 8; ++K)
	{
		UImage* P = Cast<UImage>(F(*FString::Printf(TEXT("CAMPWAR_Join_4_Click_40_%d"), K)));
		JSelFrame.Add(P);
		JSelFrameBase[K] = SlotPos(P);
	}

	// ── Кнопки: 0x10401 → `CClan_OnMemberListUI`. Звука у кнопок окна нет. ──
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void()>&& Fn)
	{
		if (Btn) { AsdaUi::BindButton(this, Btn, IdNum, [Fn2 = MoveTemp(Fn)](int32) { Fn2(); }, Relays); }
	};
	Bind(JBtnJoin,  47600, [this]() { OnJoinParticipate(); });
	Bind(JBtnEnter, 47601, [this]() { OnJoinEnter(); });
	Bind(JBtnClose, 47700, [this]() { CloseJoinWindow(); });
	Bind(JBtnX,     47015, [this]() { CloseJoinWindow(); });

	// ── `CCampWar_InitJoinInfo@0xC97890` (G:306203): статичные подписи и начальное состояние ──
	SetT(JTitle, Loc(AsdaCampWarLoc::kJoinTitle));     // 47010 (в .JUI скрыт — так и остаётся)
	SetT(JGameCap, Loc(AsdaCampWarLoc::kGameCount));   SetT(JGameVal, FString());
	SetT(JDayCap, Loc(AsdaCampWarLoc::kDayCount));     SetT(JDayVal, FString());
	SetT(JDay1, FString()); SetVis(JDay1, true);       SetT(JDay2, FString()); SetVis(JDay2, true);
	SetT(JStartCap, Loc(AsdaCampWarLoc::kStart));      SetT(JStartVal, FString());
	SetVis(JLevelCap, true); SetT(JLevelCap, Loc(AsdaCampWarLoc::kLevelCaption));
	SetT(JLevelVal, FString()); SetVis(JLevelVal, true);
	SetT(JEndCap, Loc(AsdaCampWarLoc::kEnd));          SetT(JEndVal, FString());
	SetT(JListCap, Loc(AsdaCampWarLoc::kBattleList));
	for (int32 I = 0; I < 3; ++I)
	{
		UAsdaClanFlag::ApplyCampMark(JMark[I], I, 64.f);                      // 47100+100i ← ячейка `uiMakeAxisVec2(i)`
		SetT(JName[I], Loc(AsdaCampWarLoc::kFactionBase + I));                // 47110+100i ← 10191+i
		SetT(JCount[I], FString());                                           // 47120+100i
		SetT(JChance[I], Loc(AsdaCampWarLoc::kChance));                       // 47130+100i ← 10026
		FAsdaGaugeFill::Clip(JBarFill[I], 0.f, JBarBack[I] ? SlotPos(JBarBack[I]).X + JBarBack[I]->GetDesiredSize().X : 0.f, 0.f);
		BarFrac[I] = 0.f;
		SetT(JPct[I], TEXT("0%"));                                            // 47140+100i ← «0%»
		SetT(JRowNum[I], FString()); SetT(JRowName[I], FString()); UAsdaRichText::SetFmt(JRowState[I], FString()); SetT(JRowApply[I], FString());
		SetVis(JRowAni[I], false);                                            // 47425+j скрыт
		SetVis(JRowBlack[I], false);                                          // 47101+j скрыт
	}
	SetT(JHint, Loc(AsdaCampWarLoc::kNoEnterHint));                           // 47602 ← 111947
	SetT(JBtnJoinText, Loc(AsdaCampWarLoc::kJoinBtn));                        // 47600 ← 10030
	SetT(JBtnEnterText, Loc(AsdaCampWarLoc::kEnterBtn));                      // 47601 ← 10028
	SetT(JBtnCloseText, Loc(AsdaCampWarLoc::kClose));                         // 47700 ← 5035

	int32 Found = 0, Total = 0;
	auto Count = [&Found, &Total](const UObject* O) { ++Total; if (O) { ++Found; } };
	const UObject* Objs1[] = { JTitle, JGameCap, JGameVal, JDayCap, JDayVal, JDay1, JDay2, JStartCap, JStartVal,
		JLevelCap, JLevelVal, JEndCap, JEndVal, JListCap, JHint, JBtnJoin, JBtnEnter, JBtnClose, JBtnX };

	for (const UObject* O : Objs1) { Count(O); }
	bool bRows = true;
	for (int32 I = 0; I < 3; ++I)
	{
		const UObject* Objs2[] = { JMark[I], JName[I], JCount[I], JChance[I], JBarBack[I], JBarFill[I], JPct[I], JRow[I],
			JRowNum[I], JRowName[I], JRowState[I].Node.Get(), JRowApply[I], JRowAni[I], JRowHover[I], JRowBlack[I] };

		for (const UObject* O : Objs2) { Count(O); }
		bRows = bRows && JRow[I] && JRowName[I] && JRowNum[I] && JBarFill[I] && JPct[I];
	}
	for (const TObjectPtr<UImage>& P : JSelFrame) { Count(P); }
	FoundControls += Found; TotalControls += Total;
	bJoinReady = bRows && JBtnJoin && JBtnEnter && JBtnClose && JBtnX;
	UE_LOG(LogAsdaCampWar, Log, TEXT("[битва армий] окно 47000: разобрано %d из %d контролов"), Found, Total);
	if (!bJoinReady) { UE_LOG(LogAsdaCampWar, Error, TEXT("[битва армий] ключевые контролы 47000 не разобраны")); }
}

void UAsdaCampWarSubsystem::SetupIng()
{
	bIngReady = false;
	if (!IngWidget) { return; }
	IngBtn = Cast<UButton>(IngWidget->GetWidgetFromName(TEXT("CAMPWAR_Ing")));
	IngText = Cast<UTextBlock>(IngWidget->GetWidgetFromName(TEXT("CAMPWAR_Ing_Text")));
	// `CCampWar_InitInList@0xC98BC0`: `CWidget_SetTextAll4(49530, 10032, 0)` — подпись во всех состояниях кнопки.
	SetT(IngText, Loc(AsdaCampWarLoc::kWaitStart));
	if (IngBtn) { AsdaUi::BindButton(this, IngBtn.Get(), 49530, [this](int32) { OnIngClick(); }, Relays); }
	FoundControls += (IngBtn ? 1 : 0) + (IngText ? 1 : 0); TotalControls += 2;
	bIngReady = IngBtn != nullptr;
}

void UAsdaCampWarSubsystem::SetupInList()
{
	bInListReady = false;
	if (!InListWidget) { return; }
	auto F = [this](const TCHAR* N) { return InListWidget->GetWidgetFromName(FName(N)); };
	LTitle = Cast<UTextBlock>(F(TEXT("CAMPWAR_In_List_Top_Text")));
	LBtnQuit = Cast<UButton>(F(TEXT("CAMPWAR_In_List_Ok_B")));   LBtnQuitText = Cast<UTextBlock>(F(TEXT("CAMPWAR_In_List_Ok_B_Text")));
	LBtnClose = Cast<UButton>(F(TEXT("CAMPWAR_In_List_No_B")));  LBtnCloseText = Cast<UTextBlock>(F(TEXT("CAMPWAR_In_List_No_B_Text")));
	LBtnX = Cast<UButton>(F(TEXT("CAMPWAR_In_List_X")));
	for (int32 I = 0; I < 3; ++I)
	{
		LRow[I] = F(kLRow[I]);   LName[I] = Cast<UTextBlock>(F(kLName[I]));   LPlace[I] = Cast<UTextBlock>(F(kLPlace[I]));
		LHover[I] = Cast<UImage>(F(kLHover[I]));   LBlack[I] = Cast<UImage>(F(kLBlack[I]));
	}
	LSelFrame.Reset();
	for (int32 K = 0; K < 8; ++K) { LSelFrame.Add(Cast<UImage>(F(*FString::Printf(TEXT("CAMPWAR_In_List_1_S_30_%d"), K)))); }

	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void()>&& Fn)
	{
		if (Btn) { AsdaUi::BindButton(this, Btn, IdNum, [Fn2 = MoveTemp(Fn)](int32) { Fn2(); }, Relays); }
	};
	Bind(LBtnQuit,  49570, [this]() { OnInListQuit(); });
	Bind(LBtnClose, 49571, [this]() { CloseInList(); });
	Bind(LBtnX,     49599, [this]() { CloseInList(); });

	// `CCampWar_InitInList@0xC98BC0`: заголовок 10033, кнопки 10034/10035, тексты строк пустые, затемнение строк ВИДНО.
	SetT(LTitle, Loc(AsdaCampWarLoc::kWaitList));
	SetT(LBtnQuitText, Loc(AsdaCampWarLoc::kQuitQueue));
	SetT(LBtnCloseText, Loc(AsdaCampWarLoc::kCloseList));
	for (int32 I = 0; I < 3; ++I) { SetT(LName[I], FString()); SetT(LPlace[I], FString()); SetVis(LBlack[I], true); }
	for (const TObjectPtr<UImage>& P : LSelFrame) { SetVis(P, false); }

	int32 Found = 0, Total = 0;
	const UObject* Objs3[] = { LTitle, LBtnQuit, LBtnClose, LBtnX, LRow[0], LName[0], LPlace[0], LHover[0], LBlack[0] };

	for (const UObject* O : Objs3)
	{
		++Total; if (O) { ++Found; }
	}
	FoundControls += Found; TotalControls += Total;
	bInListReady = LBtnQuit && LBtnClose && LRow[0] && LName[0];
}

void UAsdaCampWarSubsystem::SetupConfirm()
{
	bConfirmReady = false;
	if (!ConfirmWidget) { return; }
	auto F = [this](const TCHAR* N) { return ConfirmWidget->GetWidgetFromName(FName(N)); };
	CTitle = Cast<UTextBlock>(F(TEXT("CAMPWAR_JOIN_LOGIN_MOK_T")));
	CMsg = Cast<UTextBlock>(F(TEXT("CAMPWAR_JOIN_LOGIN_T_M")));
	CTimeCap = Cast<UTextBlock>(F(TEXT("CAMPWAR_JOIN_LOGIN_T1")));
	CTime = Cast<UTextBlock>(F(TEXT("CAMPWAR_JOIN_LOGIN_TIME_T")));
	CBtnEnter = Cast<UButton>(F(TEXT("CAMPWAR_JOIN_LOGIN_P")));     CBtnEnterText = Cast<UTextBlock>(F(TEXT("CAMPWAR_JOIN_LOGIN_P_Text")));
	CBtnQuit = Cast<UButton>(F(TEXT("CAMPWAR_JOIN_LOGIN_P_1")));    CBtnQuitText = Cast<UTextBlock>(F(TEXT("CAMPWAR_JOIN_LOGIN_P_Text_1")));
	CBtnX = Cast<UButton>(F(TEXT("CAMPWAR_JOIN_LOGIN_X")));
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void()>&& Fn)
	{
		if (Btn) { AsdaUi::BindButton(this, Btn, IdNum, [Fn2 = MoveTemp(Fn)](int32) { Fn2(); }, Relays); }
	};
	Bind(CBtnEnter, 48050, [this]() { OnConfirmEnter(); });
	Bind(CBtnQuit,  48060, [this]() { OnConfirmQuit(); });
	Bind(CBtnX,     48010, [this]() { OnConfirmQuit(); });
	// `CCampWar_InitJoinConfirm@0xC988D0`: 48000 ← 10037, 48020 ← "", 48030 ← 10038, 48040 ← "", 48050 ← 10039, 48060 ← 10034.
	SetT(CTitle, Loc(AsdaCampWarLoc::kConfirmTitle));
	SetT(CMsg, FString());
	SetT(CTimeCap, Loc(AsdaCampWarLoc::kTimeLeft));
	SetT(CTime, FString());
	SetT(CBtnEnterText, Loc(AsdaCampWarLoc::kEnterWar));
	SetT(CBtnQuitText, Loc(AsdaCampWarLoc::kQuitQueue));
	int32 Found = 0, Total = 0;
	const UObject* Objs4[] = { CTitle, CMsg, CTimeCap, CTime, CBtnEnter, CBtnEnterText, CBtnQuit, CBtnQuitText, CBtnX };

	for (const UObject* O : Objs4)
	{
		++Total; if (O) { ++Found; }
	}
	FoundControls += Found; TotalControls += Total;
	bConfirmReady = CBtnEnter && CBtnQuit && CBtnX && CTime && CMsg;
}

// ═════════════════════════════ ОКНО 47000 ═════════════════════════════════════════════════════

void UAsdaCampWarSubsystem::OpenJoinWindow()
{
	// `CCampWar_ShowInfoWindow@0xC9ADF0`: видим + наверх (`CClass_MapInsert`), затем заполнение по строке BaseTable слота карты.
	if (!EnsureJoin()) { return; }
	bJoinOpen = true;
	if (UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>())
	{
		WM->SetWindowVisible(kJoinId, true);
		WM->BringWindowToFront(kJoinId);
	}
	FillJoin();
	UE_LOG(LogAsdaCampWar, Log, TEXT("[битва армий] окно 47000 открыто: регион %d, битв %d, выбрана строка %d"), (int32)MapType,
		IsReg(MapType) ? (int32)GameCount[MapType] : -1, SelRow);
}

void UAsdaCampWarSubsystem::CloseJoinWindow()
{
	// `sub_C9B2C0`: +0x231C = 0 (★ не −1), `SetVisible(0)`.
	SelRow = 0;
	bJoinOpen = false;
	HoverRow = -1;
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kJoinId, false);
	}
}

void UAsdaCampWarSubsystem::FillJoin()
{
	if (!bJoinReady) { return; }
	const FAsdaCampWarBaseRow* Row = GetBaseRow(CurrentMapSlot());
	if (!Row) { UE_LOG(LogAsdaCampWar, Warning, TEXT("[битва армий] нет строки DT_CampWarBase слота %d — окно не заполнено (как эталон)"), CurrentMapSlot()); return; }
	SetT(JGameVal, LocSd1(AsdaCampWarLoc::kSd1, (int32)TotalGame));                       // 47040 ← [sd1] всего битв
	SetT(JLevelVal, FString::Printf(TEXT("%d~%d"), LevelMin, LevelMax));                   // 47062
	SetT(JDayVal, LocSd1(AsdaCampWarLoc::kSd1, (int32)DayGame + 1));                       // 47080 ← [sd1] битв сегодня + 1
	SetT(JStartVal, FString::Printf(TEXT("%02d:%02d"), StartH, StartM));                  // 47060
	SetT(JEndVal, FString::Printf(TEXT("%02d:%02d"), EndH, EndM));                        // 47095
	const FString DayText = (Day == 7) ? FString() : Loc(AsdaCampWarLoc::kDayBase + Day);   // 47096/47097
	SetT(JDay1, DayText); SetT(JDay2, DayText);
	for (int32 I = 0; I < 3; ++I)
	{
		// «Шанс» = WinCount[i] / TotalGame · 100 (TotalGame ≤ 0 → 0).
		const float Pct = (TotalGame > 0) ? (float)((double)WinCount[I] / (double)TotalGame * 100.0) : 0.f;
		BarFrac[I] = FMath::Clamp(Pct, 0.f, kBarMax) / kBarMax;
		const float FullW = JBarBack[I] ? JBarBack[I]->GetDesiredSize().X : 0.f;
		FAsdaGaugeFill::Clip(JBarFill[I], 0.f, FullW > 0.f ? FullW : 140.f, BarFrac[I]);
		SetT(JPct[I], FString::Printf(TEXT("%.2f%%"), Pct));
		SetT(JCount[I], FString::Printf(TEXT("%d/%d"), RegistCount[I], Row->Field10));
	}
	UpdateRankingList();
}

void UAsdaCampWarSubsystem::UpdateRankingList()
{
	// `CCampWar_UpdateRankingList@0xC9B300`.
	if (!bJoinReady) { return; }
	const FAsdaCampWarBaseRow* Row = GetBaseRow(CurrentMapSlot());
	if (!Row) { return; }
	for (int32 J = 0; J < kRows; ++J) { SetT(JRowNum[J], FString()); SetT(JRowName[J], FString()); UAsdaRichText::SetFmt(JRowState[J], FString()); }
	// [ПОРТ] у эталона при MapType вне 0..2 число строк читается мимо массива; у порта — 0 строк.
	const int32 N = IsReg(MapType) ? FMath::Clamp((int32)GameCount[MapType], 0, kRows) : 0;
	for (int32 J = 0; J < N; ++J)
	{
		SetVis(JRowBlack[J], false);
		SetT(JRowNum[J], FString::Printf(TEXT("%d"), J + 1));
		if (MapType < 0) { return; }   // [ОРИГИНАЛ] выход без выделения строки
		SetT(JRowName[J], BattleName(MapType));
		// Загруженность: `+0x22F0+12j` / BaseRow+0x10 · 100 — <50 «Обычный» (белый), <75 «Битком», иначе «Переполнено».
		const float Load = (Row->Field10 != 0) ? (float)((double)RowLoad[J] / (double)Row->Field10 * 100.0) : 0.f;
		const TCHAR* Color = (Load < 50.f) ? TEXT("ffffffff") : (Load < 75.f) ? TEXT("ffdc9600") : TEXT("ff960000");
		const int32 LoadLoc = (Load < 50.f) ? AsdaCampWarLoc::kLoadNormal : (Load < 75.f) ? AsdaCampWarLoc::kLoadCrowded : AsdaCampWarLoc::kLoadFull;
		UAsdaRichText::SetFmt(JRowState[J], FString::Printf(TEXT("<FC=%s>%s</F>"), Color, *Loc(LoadLoc)));
		if (RowApplied[J] == 1)
		{
			SetT(JRowApply[J], Loc(AsdaCampWarLoc::kApplied));
			SetVis(JRowAni[J], true);
		}
		else
		{
			SetT(JRowApply[J], FString());
			SetVis(JRowAni[J], false);
			if (LastRegIndex >= 0) { SetVis(JRowBlack[J], true); }   // заявка уже есть — прочие строки гасятся
		}
	}
	SlotShowControl(SelRow >= 0 ? SelRow : 0);
}

void UAsdaCampWarSubsystem::SlotShowControl(int32 Row)
{
	// `campWarSlotShowControl@0xC9BAF0`: рамка 47430 → позиция строки (`CControl_SetField74_75(47430, row+0x128, row+0x12C)`),
	//   видна, +0x231C = j. Рамка — восемь планов: начало рамки ставится в начало строки, планы сохраняют свой рисунок.
	const int32 J = FMath::Clamp(Row, 0, kRows - 1);
	if (JSelFrame.Num() == 8 && JSelFrame[0])
	{
		const FVector2D Origin = JRowBase[J];
		for (int32 K = 0; K < 8; ++K)
		{
			SetSlotPos(JSelFrame[K], Origin + (JSelFrameBase[K] - JSelFrameBase[0]));
			SetVis(JSelFrame[K], true);
		}
	}
	SelRow = J;
}

void UAsdaCampWarSubsystem::SlotsHideAll()
{
	// `campWarSlotsHideAll@0xC9B880`: тексты строк пусты, значок скрыт, затемнение 47101+i скрыто.
	if (!bJoinReady) { return; }
	for (int32 J = 0; J < kRows; ++J)
	{
		SetT(JRowNum[J], FString()); SetT(JRowName[J], FString()); UAsdaRichText::SetFmt(JRowState[J], FString()); SetT(JRowApply[J], FString());
		SetVis(JRowAni[J], false);
		SetVis(JRowBlack[J], false);
	}
}

void UAsdaCampWarSubsystem::OnRowClick(int32 Row)
{
	// Событие 0xB по 47400+10j (j < N): `campWarSlotShowControl(id)` + `Clan_Send_Op6700(self, +0x231C)`.
	const int32 N = IsReg(MapType) ? FMath::Clamp((int32)GameCount[MapType], 0, kRows) : 0;
	if (Row < 0 || Row >= N) { return; }
	SlotShowControl(Row);
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (Net) { Net->SendCampWarMainInfo6700(Net->SelectedCharId, (uint8)SelRow); }
	UE_LOG(LogAsdaCampWar, Log, TEXT("[битва армий] строка списка %d выбрана → 6700 (режим %d)"), Row, SelRow);
}

void UAsdaCampWarSubsystem::OnRowHover(int32 Row, bool bOn)
{
	// `campWarSlotResetAndShow@0xC9BA00`: подсветка 47404+10j — у всех скрыта, при наведении — у строки j.
	for (int32 J = 0; J < kRows; ++J) { SetVis(JRowHover[J], false); }
	if (bOn && Row >= 0 && Row < kRows) { SetVis(JRowHover[Row], true); }
}

TArray<uint8> UAsdaCampWarSubsystem::MakeHead(int32 Len) const
{
	// Общая голова тел 6706/6708/6712/6714: [0..3] charId (`+0x22D0`), [4] слот персонажа (`g_LocalPlayer+0x434` —
	//   iCharIndex экрана выбора; у порта `LastSelectedRawSlot`, его же шлёт блок 4001).
	TArray<uint8> B; B.SetNumZeroed(Len);
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (Net && Len >= 5)
	{
		AsdaBytes::WrI32(B.GetData(), Net->SelectedCharId);
		B[4] = Net->LastSelectedRawSlot;
	}
	return B;
}

bool UAsdaCampWarSubsystem::SendBody(uint16 Op, const TArray<uint8>& Body, const TCHAR* What)
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	return Net && Net->SendCampWar(Op, Body.GetData(), Body.Num(), What);
}

// Своя армия `+0xB7C` (i16): у порта — мир (`SelfCampId`, −2 = 6713 ещё не приходил → −1 как у ctor).
static int32 SelfCamp(const UGameInstance* GI)
{
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const int32 C = WS ? WS->GetSelfCampId() : -1;
	return (C < -1) ? -1 : C;
}

void UAsdaCampWarSubsystem::OnJoinParticipate()
{
	// `Clan_Send_Op6706@0xCA26A0`: без строки → ONEBUT 10049; иначе 8 б: голова, [5..6] армия, [7] строка (GameIndex).
	if (SelRow < 0) { NoticeLoc(AsdaCampWarLoc::kNoSelect); return; }
	TArray<uint8> B = MakeHead(8);
	AsdaBytes::WrI16(B.GetData() + 5, SelfCamp(GetGameInstance()));
	B[7] = (uint8)SelRow;
	SendBody(6706, B, TEXT("«Участвовать»"));
}

void UAsdaCampWarSubsystem::OnJoinEnter()
{
	// `Clan_Send_6714@0xCA2D70`: строка выбрана → 9 б (голова, [5..6] армия, [7] регион +0xBAE, [8] строка) + остановка
	//   эффект-слотов боевых точек (`charEffectSlots_StopAll` — боевая фаза); иначе ONEBUT 10049 (заявки нет) / 10050.
	if (SelRow >= 0)
	{
		TArray<uint8> B = MakeHead(9);
		AsdaBytes::WrI16(B.GetData() + 5, SelfCamp(GetGameInstance()));
		B[7] = (uint8)MapType;
		B[8] = (uint8)SelRow;
		SendBody(6714, B, TEXT("«Вход на поле боя»"));
		// ★ 30.10.2026: `charEffectSlots_StopAll` — эффекты 10 точек погашены, слоты очищены (боевая фаза).
		if (UAsdaCampWarBattleSubsystem* Battle = GetGameInstance()->GetSubsystem<UAsdaCampWarBattleSubsystem>()) { Battle->StopAllPointEffects(); }
		return;
	}
	NoticeLoc(LastRegIndex < 0 ? AsdaCampWarLoc::kNoSelect : AsdaCampWarLoc::kNoSelect2);
}

// ═════════════════════════════ 49530 / 49540 ══════════════════════════════════════════════════

void UAsdaCampWarSubsystem::ShowIng()
{
	// `uiCampWarWindow_Show@0xC9BC10`: видим + наверх.
	if (!EnsureIng()) { return; }
	bIngShown = true;
	if (UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>())
	{
		WM->SetWindowVisible(kIngId, true);
		WM->BringWindowToFront(kIngId);
	}
}

void UAsdaCampWarSubsystem::HideIng()
{
	// `uiCtrlHideAtField374@0xC9BC90`.
	bIngShown = false;
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kIngId, false);
	}
}

void UAsdaCampWarSubsystem::OnIngClick()
{
	ShowInList();
}

void UAsdaCampWarSubsystem::ShowInList()
{
	// `uiCampWarWindowShow@0xC9BCD0`: видим + наверх; для i = 0..2 тексты строки i пусты; если +0x22F8+4i ≥ 0 — ★ имя и место
	//   пишутся в контролы СТРОКИ 0 (Д-КВ-1, 1:1), затемнение строки 0 снимается. В конце снять выбор (`sub_C9C110`).
	if (!EnsureInList()) { return; }
	bInListOpen = true;
	if (UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>())
	{
		WM->SetWindowVisible(kInListId, true);
		WM->BringWindowToFront(kInListId);
	}
	for (int32 I = 0; I < kRegions; ++I)
	{
		SetT(LName[I], FString()); SetT(LPlace[I], FString());
		if (WaitBefore[I] >= 0)
		{
			SetT(LName[0], BattleName(I));
			SetT(LPlace[0], FString::Printf(TEXT("%d"), WaitBefore[I]));
			SetVis(LBlack[0], false);
		}
	}
	InListClearSel();
}

void UAsdaCampWarSubsystem::InListClearSel()
{
	// `sub_C9C110`: +0x2320 = −1, рамка 49565 скрыта.
	InListSel = -1;
	for (const TObjectPtr<UImage>& P : LSelFrame) { SetVis(P, false); }
}

void UAsdaCampWarSubsystem::OnInListRowClick()
{
	// Событие 0xB по 49541 — `campWarPointWnd_SelectShow@0xC9C040` (имя врёт): выбрана строка 0 → снять; иначе рамка 49565
	//   в позицию строки 0, видна, +0x2320 = 0.
	if (InListSel == 0) { InListClearSel(); return; }
	for (const TObjectPtr<UImage>& P : LSelFrame) { SetVis(P, true); }
	InListSel = 0;
}

void UAsdaCampWarSubsystem::OnInListRowHover(bool bOn)
{
	// `sub_C9BF50(49541, …)`: подсветка 49544+10i — у всех скрыта, при наведении — у строки 0.
	for (int32 I = 0; I < kRegions; ++I) { SetVis(LHover[I], false); }
	if (bOn) { SetVis(LHover[0], true); }
}

void UAsdaCampWarSubsystem::OnInListQuit()
{
	// 49570: +0x2320 < 0 → ничего; иначе TWOBUT id 10036 «Прекратить ожидание [ss1]?», ss1 = имя битвы строки → «Да» 6708(0).
	if (InListSel < 0) { return; }
	const FString Text = LocSs1(AsdaCampWarLoc::kStopWait, BattleName(InListSel));
	NoticeTrace.Add(AsdaCampWarLoc::kStopWait);
	LastNoticeText = Text;
	if (UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>())
	{
		TWeakObjectPtr<UAsdaCampWarSubsystem> Self(this);
		WM->ShowGameConfirm(Text, [Self](bool bYes) { if (Self.IsValid() && bYes) { Self->SendCancel6708(0); } },
			/*Gauge*/ 0.f, AsdaLoc::kBtnOk, 2289, /*EtalonNoticeId*/ AsdaCampWarLoc::kStopWait, /*EtalonKind*/ 1);
	}
}

void UAsdaCampWarSubsystem::CloseInList()
{
	// `sub_C9BF10@0xC9BF10`.
	bInListOpen = false;
	bInListHover = false;
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kInListId, false);
	}
}

void UAsdaCampWarSubsystem::SendCancel6708(int32 Mode)
{
	// `Clan_Send_Op6708@0xCA28F0`: 8 б — голова, [5] регион (0 — регион карты +0xBAE; 1 — регион вызова +0x230C и скрыть 48099),
	//   [6..7] армия.
	TArray<uint8> B = MakeHead(8);
	if (Mode == 1) { B[5] = (uint8)CallRegion; HideConfirm(); }
	else           { B[5] = (uint8)MapType; }
	AsdaBytes::WrI16(B.GetData() + 6, SelfCamp(GetGameInstance()));
	SendBody(6708, B, Mode == 1 ? TEXT("выйти из очереди (вызов)") : TEXT("выйти из очереди"));
}

// ═════════════════════════════ 48099 ══════════════════════════════════════════════════════════

void UAsdaCampWarSubsystem::ShowConfirm()
{
	// `uiCampWarPointWndShow@0xC9C150` (имя врёт): видим (★ без `CClass_MapInsert`), 48020 = 10043 [ss1] = имя битвы(0),
	//   отсчёт 30 с (+0x2324 = 30, +0x2328 = 1), 48040 = «00:30». Повторный 6711 перезапускает отсчёт.
	if (!EnsureConfirm()) { return; }
	bConfirmOpen = true;
	if (UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>())
	{
		WM->SetWindowVisible(kConfirmId, true);
	}
	SetT(CMsg, LocSs1(AsdaCampWarLoc::kCanEnterName, BattleName(0)));
	Countdown = kCountdownStart;
	bCountdown = true;
	SetT(CTime, FString::Printf(TEXT("00:%02d"), Countdown));
	UE_LOG(LogAsdaCampWar, Log, TEXT("[битва армий] окно 48099 «Подтверди участие»: регион вызова %d, отсчёт %d с"), CallRegion, Countdown);
}

void UAsdaCampWarSubsystem::HideConfirm()
{
	// `uiCampWarPointWndHide@0xC9C280` (имя врёт): скрыт, +0x2324 = 0, +0x2328 = 0.
	bConfirmOpen = false;
	Countdown = 0;
	bCountdown = false;
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kConfirmId, false);
	}
}

void UAsdaCampWarSubsystem::OnConfirmEnter()
{
	// 48050: `uiCampWarPointWndHide` → `Clan_Send_SetRank@0xCA2B20` (имя врёт; 6712): 11 б — голова, [5..6] армия, [7] регион
	//   вызова, [8] GameIndex заявки региона, [9..10] RegistIndex заявки региона.
	HideConfirm();
	TArray<uint8> B = MakeHead(11);
	AsdaBytes::WrI16(B.GetData() + 5, SelfCamp(GetGameInstance()));
	B[7] = (uint8)CallRegion;
	// [ПОРТ] эталон индексирует массивы по +0x230C без проверки (−1 читает соседние поля); у порта — «нет заявки» (−1).
	const int32 K = CallRegion;
	B[8] = (uint8)(IsReg(K) ? RegGameIndex[K] : -1);
	AsdaBytes::WrI16(B.GetData() + 9, IsReg(K) ? RegIndex[K] : -1);
	SendBody(6712, B, TEXT("«Войти в битву»"));
}

void UAsdaCampWarSubsystem::OnConfirmQuit()
{
	// 48060 / X 48010: TWOBUT id 100360, текст 10036 [ss1] = имя битвы(+0x230C) → «Да» 6708(1).
	const FString Text = LocSs1(AsdaCampWarLoc::kStopWait, BattleName(CallRegion));
	NoticeTrace.Add(100360);
	LastNoticeText = Text;
	if (UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>())
	{
		TWeakObjectPtr<UAsdaCampWarSubsystem> Self(this);
		WM->ShowGameConfirm(Text, [Self](bool bYes) { if (Self.IsValid() && bYes) { Self->SendCancel6708(1); } },
			/*Gauge*/ 0.f, AsdaLoc::kBtnOk, 2289, /*EtalonNoticeId*/ 100360, /*EtalonKind*/ 1);
	}
}

void UAsdaCampWarSubsystem::ClanTimerTick(int32 Mode)
{
	// `ClanTimer_Tick@0xCA18D0`: ветка 1 без таймера боя (+0x2334 == 0) выходит до блока 48099 (таймер боя — боевая фаза).
	if (Mode == 1 && !bBattleTimer) { return; }
	// ★ 30.10.2026 (боевая фаза): блок таймера боя «MM:SS» 49942 идёт ПЕРЕД блоком 48099 (`ClanTimer_Tick` режим 1).
	if (Mode == 1)
	{
		if (UAsdaCampWarBattleSubsystem* Battle = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCampWarBattleSubsystem>() : nullptr)
		{
			Battle->TickBattleTimer();
		}
	}
	if (!bCountdown) { return; }
	if (Countdown <= 0)
	{
		// Отсчёт кончился — окно скрыто и 6708(1) БЕЗ вопроса.
		UE_LOG(LogAsdaCampWar, Log, TEXT("[битва армий] отсчёт 48099 истёк → 6708(1)"));
		HideConfirm();
		SendCancel6708(1);
		return;
	}
	--Countdown;
	SetT(CTime, FString::Printf(TEXT("00:%02d"), Countdown));
}

// ═════════════════════════════ ОПРОС МЫШИ (события 8 / 0xA / 0xB) ═════════════════════════════

void UAsdaCampWarSubsystem::TickMouse()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// 47000: строки 47400+10j (j < N) — наведение / уход / щелчок ЛКМ.
	if (bJoinOpen && bJoinReady && !WM->IsPointerBlockedByTopmost(kJoinId) && !WM->IsCursorOverHigherWindow(kJoinId))
	{
		const int32 N = IsReg(MapType) ? FMath::Clamp((int32)GameCount[MapType], 0, kRows) : 0;
		int32 Over = -1;
		for (int32 J = 0; J < N; ++J) { if (JRow[J] && AsdaUi::IsCursorOverWidget(JRow[J])) { Over = J; break; } }
		if (Over != HoverRow) { OnRowHover(Over, Over >= 0); HoverRow = Over; }
		if (Over >= 0 && Mouse.bLmbDown) { OnRowClick(Over); }
	}
	// 49540: события только у строки 49541.
	if (bInListOpen && bInListReady && !WM->IsPointerBlockedByTopmost(kInListId) && !WM->IsCursorOverHigherWindow(kInListId))
	{
		const bool bOver = LRow[0] && AsdaUi::IsCursorOverWidget(LRow[0]);
		if (bOver != bInListHover) { OnInListRowHover(bOver); bInListHover = bOver; }
		if (bOver && Mouse.bLmbDown) { OnInListRowClick(); }
	}
}

// ═════════════════════════════ СЕТЬ ═══════════════════════════════════════════════════════════

void UAsdaCampWarSubsystem::HandlePacket(int32 Op, const TArray<uint8>& B)
{
	switch (Op)
	{
	case OP_CLAN_INFO:                          OnMainInfo(B);       break;
	case OP_CLAN_STORAGE_ITEM:                  OnSquadReset(B);     break;
	case OP_CLAN_RANK_RESULT:                   OnRegistResult(B);   break;
	case OP_CLAN_KICK_RESULT:                   OnCancelResult(B);   break;
	case OP_CLAN_MEMBER_POINT:                  OnWaitInfo(B);       break;
	case OP_CLAN_APPLY_RESULT:                  OnCallResult(B);     break;
	case OP_UI_CAMP_WAR_RANKING_RESULT_HANDLER: OnQueueClear(B);     break;
	case OP_CLAN_MEMBER_LIST_BULK:              OnMemberListBulk(B); break;
	case OP_C_CAMP_WAR_RESET_STATE:             OnBattleReset(B);    break;   // ★ 30.10.2026 боевая фаза (при `asda.CampWar.Battle 0`)
	case OP_CAMP_WAR_SET_FACTION_FLAG_TOOLTIP:  OnArmyHolder(B);     break;   // ★ 30.10.2026 боевая фаза
	default: break;
	}
}

void UAsdaCampWarSubsystem::OnMainInfo(const TArray<uint8>& B)
{
	// `Recv_ClanInfo@0xCA3C60` (дизасм; REG_SPEC.md §3.2).
	if (B.Num() < 44) { return; }
	MapType = (int8)B[18];
	for (int32 I = 0; I < 3; ++I)
	{
		TeamCount[I] = (int16)AsdaBytes::RdI16(&B[19 + 2 * I]);
		WinCount[I]  = (int16)AsdaBytes::RdI16(&B[25 + 2 * I]);
		LoseCount[I] = (int16)AsdaBytes::RdI16(&B[31 + 2 * I]);
		RegistCount[I] = AsdaBytes::RdI16(&B[12 + 2 * I]);
	}
	TotalGame = (int16)AsdaBytes::RdI16(&B[37]);
	DayGame = (int8)B[39];
	StartH = (int32)AsdaBytes::RdU16(&B[2]); StartM = (int32)AsdaBytes::RdU16(&B[4]);
	EndH = (int32)AsdaBytes::RdU16(&B[6]);   EndM = (int32)AsdaBytes::RdU16(&B[8]);
	// [ПОРТ] эталон пишет по индексу MapType без проверки; у порта — только регион 0..2.
	if (IsReg(MapType)) { GameCount[MapType] = 1; GameCount[MapType] = (int16)AsdaBytes::RdU16(&B[10]); }
	LevelMin = AsdaBytes::RdI16(&B[40]);
	LevelMax = AsdaBytes::RdI16(&B[42]);
	Day = (int32)AsdaBytes::RdU16(&B[0]);
	bMainInfoReceived = true;
	LastMainInfo = B;
	UE_LOG(LogAsdaCampWar, Log, TEXT("[битва армий] ←6701: регион %d, армии %d/%d/%d, побед %d/%d/%d из %d, сегодня %d, битв %d, %02d:%02d–%02d:%02d, уровни %d~%d, день %d"),
		(int32)MapType, (int32)TeamCount[0], (int32)TeamCount[1], (int32)TeamCount[2], (int32)WinCount[0], (int32)WinCount[1], (int32)WinCount[2],
		(int32)TotalGame, (int32)DayGame, IsReg(MapType) ? (int32)GameCount[MapType] : -1, StartH, StartM, EndH, EndM, LevelMin, LevelMax, Day);
	// [ОРИГИНАЛ] если 47000 видно — `CCampWar_ShowInfoWindow` (полная перерисовка).
	if (bJoinOpen) { OpenJoinWindow(); }
}

void UAsdaCampWarSubsystem::OnSquadReset(const TArray<uint8>& B)
{
	// `Recv_ClanStorageItem@0xCA4090` (имя врёт): 0/2/3 → 10143; 1 → запись предмета (+1, 57 б; количество ≠ 0 — положить как
	//   есть, 0 — снять ячейку), своя армия −1, окно персонажа, 10142; 4 → 10144; 5 → 10145; иное — молча.
	if (B.Num() < 58) { return; }
	switch (B[0])
	{
	case 0: case 2: case 3: NoticeLoc(AsdaCampWarLoc::kResetFail); break;
	case 1:
	{
		TArray<uint8> Rec; Rec.Append(B.GetData() + 1, 57);
		const int32 Amount = AsdaBytes::RdI32(Rec.GetData() + 9);
		if (UAsdaInventorySubsystem* Inv = GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>())
		{
			Inv->ApplyExternalRecord(Rec, Amount != 0 ? UAsdaInventorySubsystem::EExternalRecordMode::AsIs
			                                          : UAsdaInventorySubsystem::EExternalRecordMode::CountOrClear);
		}
		// `+0xB7C = −1`, `CFactionUI_UpdateInfo(−1, +0xB82, +0xB86)` — общая точка «своя армия» (6713).
		if (UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>())
		{
			const int32 KeepPts = LastSelfPoints, KeepGr = LastSelfGrade;   // эталон +0xB82/+0xB86 не трогает
			Net->OnSelfFaction.Broadcast(-1, KeepPts, KeepGr);
			LastSelfPoints = KeepPts; LastSelfGrade = KeepGr;
		}
		NoticeLoc(AsdaCampWarLoc::kResetOk);
		break;
	}
	case 4: NoticeLoc(AsdaCampWarLoc::kResetFail4); break;
	case 5: NoticeLoc(AsdaCampWarLoc::kResetFail5); break;
	default: break;
	}
}

void UAsdaCampWarSubsystem::OnRegistResult(const TArray<uint8>& B)
{
	// `Recv_ClanRankResult@0xCA42C0` (имя врёт; 6707): [0] код, [1] k, [2] j, [3..4] RegistIndex, [5..6] стоящих перед тобой.
	if (B.Num() < 7) { return; }
	const int32 Code = (int8)B[0];
	const int32 K = (int8)B[1], J = (int8)B[2];
	const int32 RIdx = AsdaBytes::RdI16(&B[3]), Wait = AsdaBytes::RdI16(&B[5]);
	UE_LOG(LogAsdaCampWar, Log, TEXT("[битва армий] ←6707: код %d, регион %d, строка %d, заявка %d, перед тобой %d"), Code, K, J, RIdx, Wait);
	if (Code == 1 || Code == 2 || Code == 5)
	{
		// [ПОРТ] эталон пишет по индексам k/j без проверки; у порта — только существующие.
		if (IsReg(K)) { RegGameIndex[K] = J; RegIndex[K] = RIdx; WaitBefore[K] = Wait; }
		LastRegIndex = (int16)RIdx;
		if (J >= 0 && J < kRows) { RowRegion[J] = K; RowApplied[J] = 1; }
		ShowIng();
		if (Code == 5) { UpdateRankingList(); NoticeLoc(AsdaCampWarLoc::kRegistResult[5]); }   // ★ у кода 5 сначала список
		else           { NoticeLoc(AsdaCampWarLoc::kRegistResult[Code]); UpdateRankingList(); }
		return;
	}
	if (Code >= 0 && Code <= 8) { NoticeLoc(AsdaCampWarLoc::kRegistResult[Code]); return; }
	if (Code == 9)  { Notice(AsdaCampWarLoc::kKicked, Loc(AsdaCampWarLoc::kKicked), 10159); return; }
	if (Code == 10)
	{
		Notice(AsdaCampWarLoc::kLevelRange, AsdaUi::FormatPrintfArgs(Loc(AsdaCampWarLoc::kLevelRange),
			{ FString::FromInt(LevelMin), FString::FromInt(LevelMax) }), 10159);
		return;
	}
	// Прочие коды — молча (jumptable на 11 записей).
}

void UAsdaCampWarSubsystem::OnCancelResult(const TArray<uint8>& B)
{
	// `Recv_ClanKickResult@0xCA4810` (имя врёт; 6709): 0 → 10161; 1 → (свой регион — строка 0 сброшена) скрыть 49530 и 49540,
	//   `CCampWar_ResetState`, 10162, `UpdateRankingList`; 2/3/4 → 10163/10164/10165; иное — молча.
	if (B.Num() < 2) { return; }
	const int32 Code = (int8)B[0];
	UE_LOG(LogAsdaCampWar, Log, TEXT("[битва армий] ←6709: код %d, регион %d"), Code, (int32)(int8)B[1]);
	if (Code == 1)
	{
		if ((int8)B[1] == MapType) { RowRegion[0] = -1; RowLoad[0] = 0; RowApplied[0] = 0; }
		HideIng();
		CloseInList();
		ResetState();
		NoticeLoc(AsdaCampWarLoc::kCancelResult[1]);
		UpdateRankingList();
		return;
	}
	if (Code >= 0 && Code <= 4) { NoticeLoc(AsdaCampWarLoc::kCancelResult[Code]); }
}

void UAsdaCampWarSubsystem::OnWaitInfo(const TArray<uint8>& B)
{
	// `Recv_ClanMemberPoint@0xCA49F0` (имя врёт; 6710): [0] k, [3..4] → +0x22F8+4k. Перерисовки нет.
	//   [ПОРТ] сервер в одной ветке шлёт k = −1 (эталон писал бы во флаг строки 0) — пропускаем.
	if (B.Num() < 5) { return; }
	const int32 K = (int8)B[0];
	if (IsReg(K)) { WaitBefore[K] = AsdaBytes::RdI16(&B[3]); }
	UE_LOG(LogAsdaCampWar, Log, TEXT("[битва армий] ←6710: регион %d, перед тобой %d"), K, AsdaBytes::RdI16(&B[3]));
}

void UAsdaCampWarSubsystem::OnCallResult(const TArray<uint8>& B)
{
	// `Recv_ClanApplyResult@0xCA4A30` (имя врёт; 6711): +0x230C = [1]; [0]==1 → 48099; иначе ONEBUT 10044.
	if (B.Num() < 5) { return; }
	CallRegion = (int8)B[1];
	UE_LOG(LogAsdaCampWar, Log, TEXT("[битва армий] ←6711: результат %d, регион %d"), (int32)B[0], CallRegion);
	if (B[0] == 1) { ShowConfirm(); }
	else           { NoticeLoc(AsdaCampWarLoc::kCanEnter); }
}

void UAsdaCampWarSubsystem::OnQueueClear(const TArray<uint8>& B)
{
	// `uiCampWarRankingResultHandler@0xCA7500` (6740): 0 → «…Осталось %d мин.» в баннер окна NPC (`npcExchangeUiSetText`);
	//   1/2 → строка 0 сброшена, 49530 и 49540 скрыты, `ResetState`, `UpdateRankingList`, ONEBUT 10202 / 10203.
	if (B.Num() < 2) { return; }
	const int32 Code = B[0];
	UE_LOG(LogAsdaCampWar, Log, TEXT("[битва армий] ←6740: код %d, минут %d"), Code, (int32)B[1]);
	if (Code == 0)
	{
		const FString Text = AsdaUi::FormatPrintfArgs(Loc(AsdaCampWarLoc::kClearMinutes), { FString::FromInt(B[1]) });
		if (UAsdaNpcDialogSubsystem* Npc = GetGameInstance()->GetSubsystem<UAsdaNpcDialogSubsystem>()) { Npc->ShowQuestInfo(Text); }
		NoticeTrace.Add(AsdaCampWarLoc::kClearMinutes);
		LastNoticeText = Text;
		return;
	}
	if (Code == 1 || Code == 2)
	{
		RowRegion[0] = -1; RowLoad[0] = 0; RowApplied[0] = 0;
		HideIng();
		CloseInList();
		ResetState();
		UpdateRankingList();
		NoticeLoc(Code == 1 ? AsdaCampWarLoc::kQueueCleared : AsdaCampWarLoc::kFieldClosed);
	}
}

void UAsdaCampWarSubsystem::OnMemberListBulk(const TArray<uint8>& B)
{
	// `Recv_ClanMemberListBulk@0xCA4B50` (6715): записи по 119 б; своя запись (charId [1..4] == свой) → +0xB7E = индекс,
	//   +0xB80 = −1. Сам список сквада — боевая фаза.
	//   ★ 30.10.2026: при боевой фазе (`asda.CampWar.Battle 1`) разбор ОДИН — у боя (`ApplyMemberList6715` в ЕГО очереди: 6715/6716/6721
	//   идут строго по порядку прихода), +0xB80 = −1 он ставит через `OnSelfInSquadList`. Здесь — только при выключенном бое.
	if (UAsdaCampWarBattleSubsystem::IsBattleOn()) { return; }
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	for (int32 Off = 0; Off + 119 <= B.Num(); Off += 119)
	{
		const int32 Idx = (int8)B[Off];
		if (Idx < 0 || Idx > 0x31) { continue; }
		if (Net && AsdaBytes::RdI32(&B[Off + 4]) == Net->SelectedCharId) { LastRegIndex = -1; }
	}
}

void UAsdaCampWarSubsystem::OnBattleReset(const TArray<uint8>&)
{
	// ★ 30.10.2026: при боевой фазе 6737 разбирает бой (его очередь — порядок с 6726/6729 того же кадра); здесь — только при выключенной.
	if (UAsdaCampWarBattleSubsystem::IsBattleOn()) { return; }
	// 6737 (0 б) — inline-ветка диспетчера (G:14828): `CCampWar_ResetState` → `ToggleVisible(0, 0)` → `uiCtrlHideAtField374` (49530)
	//   → `sub_C9BF10` (49540). Сервер `InitAllRoseWarWaitMember` — всем в игре с флагом ожидания.
	ResetState();
	if (UAsdaCampWarBattleSubsystem* Battle = GetGameInstance()->GetSubsystem<UAsdaCampWarBattleSubsystem>()) { Battle->ToggleVisible(false, false); }
	HideIng();
	CloseInList();
	UE_LOG(LogAsdaCampWar, Log, TEXT("[битва армий] ←6737: сброс, HUD боя снят, 49530/49540 скрыты"));
}

void UAsdaCampWarSubsystem::OnArmyHolder(const TArray<uint8>& B)
{
	// 6738 (1 б) `campWarSetFactionFlagTooltip@0xCA73B0`: +0x2354+2·армия = 1; `sprintf(10111, 1)` (в строке `$d` — подстановки нет,
	//   1:1); `CControl_SetTooltipText(g_pChatWnd, …, 0xFFFFD306, 10000)` + ONEBUT 10111; `CCampWar_UpdateRankingList`.
	//   Серверного отправителя нет (ни 6738, ни 0x1A52 в `GameServer.c`/`seg_text.asm`) — приёмник переносится ради полноты.
	if (B.Num() < 1) { return; }
	const int32 Army = (int8)B[0];
	if (IsReg(Army)) { GameCount[Army] = 1; }   // [ПОРТ] эталон индексирует без проверки
	const FString Text = AsdaUi::FormatPrintfArgs(Loc(AsdaCampWarLoc::kHolder), { TEXT("1") });
	AsdaNotice::Plank(GetGameInstance(), EAsdaMsgCategory::War, Text, FColor(0xFF, 0xD3, 0x06, 0xFF), 10000.f, 6738);
	Notice(AsdaCampWarLoc::kHolder, Text, AsdaCampWarLoc::kHolder);
	UpdateRankingList();
}

void UAsdaCampWarSubsystem::OnSquadResetItemConfirmed(int32 ItemDword, uint8 A4, int32 A2)
{
	// `CItemManager_UseInventoryItem@0xA0FDF0` case 78 → «Да» TWOBUT 10141 (`case 0x279D`) → `Clan_Send_Create@0xCA2530` (имя врёт; 6704):
	//   11 б — [0..3] charId, [4..7] код предмета, [8] a4 (сумка), [9..10] a2 (ячейка).
	TArray<uint8> B; B.SetNumZeroed(11);
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	AsdaBytes::WrI32(B.GetData(), Net ? Net->SelectedCharId : 0);
	AsdaBytes::WrI32(B.GetData() + 4, ItemDword);
	B[8] = A4;
	AsdaBytes::WrI16(B.GetData() + 9, A2);
	SendBody(6704, B, TEXT("сброс армии (предмет)"));
}

// ═════════════════════════════ НОТИСЫ / ТЕКСТЫ ════════════════════════════════════════════════

void UAsdaCampWarSubsystem::Notice(int32 LocId, const FString& Text, int32 EtalonId)
{
	// ONEBUT `CItem_BuildTooltipText(g_pItemTooltipUI, 0, id, текст, 1)` — пара очереди (0, id) глушит повтор, как у эталона.
	NoticeTrace.Add(LocId);
	LastNoticeText = Text;
	AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::War, Text, AsdaLoc::kBtnOk, EtalonId);
}

void UAsdaCampWarSubsystem::NoticeLoc(int32 LocId)
{
	Notice(LocId, Loc(LocId), LocId);
}

FString UAsdaCampWarSubsystem::Loc(int32 LocId) const
{
	return AsdaNotice::Loc(GetGameInstance(), LocId);
}

FString UAsdaCampWarSubsystem::LocSs1(int32 LocId, const FString& Ss1) const
{
	FString S;
	if (UAsdaTextSubsystem* TS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr)
	{
		S = TS->FormatById(LocId, { Ss1 }, {});
	}
	return S.IsEmpty() ? Loc(LocId).Replace(TEXT("[ss1]"), *Ss1) : S;
}

FString UAsdaCampWarSubsystem::LocSd1(int32 LocId, int32 Sd1) const
{
	FString S;
	if (UAsdaTextSubsystem* TS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr)
	{
		S = TS->FormatById(LocId, {}, { Sd1 });
	}
	return S.IsEmpty() ? Loc(LocId).Replace(TEXT("[sd1]"), *FString::FromInt(Sd1)) : S;
}

// ═════════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════════

FString UAsdaCampWarSubsystem::GetText(int32 IdNum) const
{
	switch (IdNum)
	{
	case 47010: return GetT(JTitle);    case 47030: return GetT(JGameCap);  case 47040: return GetT(JGameVal);
	case 47070: return GetT(JDayCap);   case 47080: return GetT(JDayVal);   case 47096: return GetT(JDay1);
	case 47097: return GetT(JDay2);     case 47050: return GetT(JStartCap); case 47060: return GetT(JStartVal);
	case 47061: return GetT(JLevelCap); case 47062: return GetT(JLevelVal); case 47090: return GetT(JEndCap);
	case 47095: return GetT(JEndVal);   case 47405: return GetT(JListCap);  case 47602: return GetT(JHint);
	case 47600: return GetT(JBtnJoinText); case 47601: return GetT(JBtnEnterText); case 47700: return GetT(JBtnCloseText);
	case 49530: return GetT(IngText);   case 49573: return GetT(LTitle);    case 49570: return GetT(LBtnQuitText);
	case 49571: return GetT(LBtnCloseText);
	case 48000: return GetT(CTitle);    case 48020: return GetT(CMsg);      case 48030: return GetT(CTimeCap);
	case 48040: return GetT(CTime);     case 48050: return GetT(CBtnEnterText); case 48060: return GetT(CBtnQuitText);
	default: break;
	}
	for (int32 I = 0; I < 3; ++I)
	{
		if (IdNum == 47110 + 100 * I) { return GetT(JName[I]); }
		if (IdNum == 47120 + 100 * I) { return GetT(JCount[I]); }
		if (IdNum == 47130 + 100 * I) { return GetT(JChance[I]); }
		if (IdNum == 47140 + 100 * I) { return GetT(JPct[I]); }
		if (IdNum == 47432 + I)       { return GetT(JRowNum[I]); }
		if (IdNum == 47401 + 10 * I)  { return GetT(JRowName[I]); }
		if (IdNum == 47402 + 10 * I)  { return JRowState[I].LastRaw; }
		if (IdNum == 47403 + 10 * I)  { return GetT(JRowApply[I]); }
		if (IdNum == 49542 + 10 * I)  { return GetT(LName[I]); }
		if (IdNum == 49543 + 10 * I)  { return GetT(LPlace[I]); }
	}
	return FString();
}

bool UAsdaCampWarSubsystem::IsShown(int32 IdNum) const
{
	for (int32 I = 0; I < 3; ++I)
	{
		if (IdNum == 47101 + I)      { return IsVis(JRowBlack[I]); }
		if (IdNum == 47425 + I)      { return IsVis(JRowAni[I]); }
		if (IdNum == 47404 + 10 * I) { return IsVis(JRowHover[I]); }
		if (IdNum == 49549 + 10 * I) { return IsVis(LBlack[I]); }
		if (IdNum == 49544 + 10 * I) { return IsVis(LHover[I]); }
		if (IdNum == 47100 + 100 * I) { return JMark[I] && JMark[I]->GetBrush().GetResourceObject() != nullptr; }
	}
	if (IdNum == 47430) { return JSelFrame.Num() > 0 && IsVis(JSelFrame[0]); }
	if (IdNum == 49565) { return LSelFrame.Num() > 0 && IsVis(LSelFrame[0]); }
	return false;
}

float UAsdaCampWarSubsystem::GetBarFrac(int32 Squad) const
{
	return (Squad >= 0 && Squad < 3 && JBarFill[Squad]) ? BarFrac[Squad] : -1.f;
}

UWidget* UAsdaCampWarSubsystem::GetHitWidget(int32 IdNum) const
{
	switch (IdNum)
	{
	case 47600: return JBtnJoin;  case 47601: return JBtnEnter; case 47700: return JBtnClose; case 47015: return JBtnX;
	case 49530: return IngBtn;    case 49570: return LBtnQuit;  case 49571: return LBtnClose; case 49599: return LBtnX;
	case 49541: return LRow[0];   case 48050: return CBtnEnter; case 48060: return CBtnQuit;  case 48010: return CBtnX;
	default: break;
	}
	for (int32 J = 0; J < kRows; ++J) { if (IdNum == 47400 + 10 * J) { return JRow[J]; } }
	return nullptr;
}

FVector2D UAsdaCampWarSubsystem::GetSelFramePos() const
{
	return (JSelFrame.Num() > 0 && JSelFrame[0]) ? SlotPos(JSelFrame[0]) : FVector2D(-1.f, -1.f);
}

FString UAsdaCampWarSubsystem::DumpControls() const
{
	return FString::Printf(TEXT("[битва армий] контролов разобрано %d из %d; 47000 %s, 49530 %s, 49540 %s, 48099 %s"),
		FoundControls, TotalControls, bJoinReady ? TEXT("готово") : TEXT("нет"), bIngReady ? TEXT("готово") : TEXT("нет"),
		bInListReady ? TEXT("готово") : TEXT("нет"), bConfirmReady ? TEXT("готово") : TEXT("нет"));
}

// ═════════════════════════════ КОНСОЛЬ ════════════════════════════════════════════════════════
#if ASDA_DEV_CONSOLE   // античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaCampWarDump(
	TEXT("asda.CampWar.Dump"),
	TEXT("asda.CampWar.Dump — модель «Битвы армий» (6701, заявки по регионам, очередь, вызов, отсчёт) и открытые окна"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaCampWarSubsystem* C = GI ? GI->GetSubsystem<UAsdaCampWarSubsystem>() : nullptr;
		if (!C) { UE_LOG(LogAsdaCampWar, Warning, TEXT("[битва армий] подсистемы нет")); return; }
		UE_LOG(LogAsdaCampWar, Log, TEXT("[битва армий] 6701 %s, регион %d, строка %d, последняя заявка %d, вызов %d, отсчёт %d (%s)"),
			C->HasMainInfo() ? TEXT("есть") : TEXT("не было"), C->GetMapType(), C->GetSelRow(), C->GetLastRegIndex(), C->GetCallRegion(),
			C->GetCountdown(), C->IsCountdownOn() ? TEXT("идёт") : TEXT("стоит"));
		for (int32 K = 0; K < UAsdaCampWarSubsystem::kRegions; ++K)
		{
			UE_LOG(LogAsdaCampWar, Log, TEXT("  регион %d: заявка GameIndex %d RegistIndex %d, перед тобой %d"), K,
				C->GetRegGameIndex(K), C->GetRegIndex(K), C->GetWaitBefore(K));
		}
		UE_LOG(LogAsdaCampWar, Log, TEXT("%s"), *C->DumpControls());
	}));
#endif // ASDA_DEV_CONSOLE
