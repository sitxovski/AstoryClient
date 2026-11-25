// Copyright (c) 2026 @sitxovski (Discord)
// Реализация выбора армии (фракции): окно `CAMPWAR_Win` (JUI 44000) и подтверждение `CAMPWAR_select` (JUI 46000).
//   Весь разбор эталона, поля объекта и поток — в шапке `AsdaFactionSelectSubsystem.h` и в отчёте
//   `Reports/faction_select/FLOW.md`. Структурный дамп ассетов (каждый контрол) — `Reports/faction_select/wbp_tree.txt`.

#include "UI/AsdaFactionSelectSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "UI/AsdaCampWarSubsystem.h"   // ★ 30.10.2026: модель «Битвы армий» — владелец 6701 (численность армий)
#include "AsdaDevConsole.h"           // ASDA_DEV_CONSOLE — дев-команды вне Shipping
#include "UI/AsdaUiHelpers.h"         // AsdaUi::BindButton
#include "UI/AsdaJuiWindowHandle.h"   // каркас окна JUI (Ensure + зона рамки) и FAsdaGaugeFill
#include "UI/AsdaNotice.h"            // фасад нотисов (Loc / MsgBox)
#include "UI/AsdaWindowSubsystem.h"

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Net/NetOpcodes.h"
#include "Login/AsdaButtonRelay.h"
#include "Text/AsdaTextSubsystem.h"   // FormatById — подстановка [ss1] у 10008
#include "World/AsdaClanFlag.h"       // общая точка «знак лагеря» (`CampWar_MarkImage`, `uiMakeAxisVec2`)

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaFactionSel, Log, All);

namespace
{
	// Окна и их виджеты. Ассеты уже импортированы заходом JUI (не перегенерировать!).
	const FName  kSelectWindowId(TEXT("CAMPWAR_Win"));       // = ID корня .JUI 44000 (ключ строки ESC-таблицы)
	const FName  kConfirmWindowId(TEXT("CAMPWAR_select"));   // = ID корня .JUI 46000
	const TCHAR* kSelectWbp  = TEXT("/Game/AsdaAssets/UI/Player_UI/CampWar/WBP_CAMPWAR_Win.WBP_CAMPWAR_Win_C");
	const TCHAR* kConfirmWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/CampWar/WBP_CAMPWAR_select.WBP_CAMPWAR_select_C");

	// IDNum контролов (`.JUI` и `CCampWar_InitSelectWindow` / `CCampWar_InitSelectMsg`).
	constexpr int32 kIdTitle      = 44001;   // заголовок
	constexpr int32 kIdX          = 44002;   // X
	constexpr int32 kIdMarkBase   = 44020;   // знак строки i: 44020 + 10i
	constexpr int32 kIdSelectBase = 44025;   // «выбрать» строки i: 44025 + 10i
	constexpr int32 kIdNameBase   = 44027;   // имя строки i: 44027 + 10i
	constexpr int32 kIdPctBase    = 44029;   // процент строки i: 44029 + 10i
	constexpr int32 kIdBarBase    = 44050;   // полоса строки i: 44050 + 10i
	constexpr int32 kIdClose      = 44099;   // «Закрыть»
	constexpr int32 kIdCfMark     = 46012;   // знак в подтверждении
	constexpr int32 kIdCfName     = 46020;   // имя армии в подтверждении
	constexpr int32 kIdCfText     = 46030;   // текст подтверждения
	constexpr int32 kIdCfOk       = 46040;   // ОК
	constexpr int32 kIdCfNo       = 46050;   // Отмена

	// Имена узлов WBP по строкам (сопоставление ID .JUI ↔ узел — по дампу `wbp_tree.txt`; у строки 2 кнопка импортирована
	//   под именем `CAMPWAR_2_B_C_1` — в .JUI у 44035 и 44045 одинаковый ID «CAMPWAR-2-B-C»).
	const TCHAR* kNodeMark[3]    = { TEXT("CAMPWAR_1_mark_F_3_0"),     TEXT("CAMPWAR_2_mark_F_21_0"), TEXT("CAMPWAR_3_mark_F_34_0") };
	const TCHAR* kNodeName[3]    = { TEXT("CAMPWAR_back_T"),           TEXT("CAMPWAR_back_T_1"),      TEXT("CAMPWAR_3_back_TEXT") };
	const TCHAR* kNodeBarBack[3] = { TEXT("CAMPWAR_1_back_SLIDE_7_0"), TEXT("CAMPWAR_2_SLIDE_25_0"),  TEXT("CAMPWAR_3_back_SLIDE_36_0") };
	const TCHAR* kNodeBarFill[3] = { TEXT("CAMPWAR_1_back_SLIDE_7_1"), TEXT("CAMPWAR_2_SLIDE_25_1"),  TEXT("CAMPWAR_3_back_SLIDE_36_1") };
	const TCHAR* kNodePct[3]     = { TEXT("CAMPWAR_1_back_SLIDE_T"),   TEXT("CAMPWAR_2_TEXT"),        TEXT("CAMPWAR_3_back_SLIDE_T") };
	const TCHAR* kNodeBtn[3]     = { TEXT("CAMPWAR_1_B_C"),            TEXT("CAMPWAR_2_B_C"),         TEXT("CAMPWAR_2_B_C_1") };
	const TCHAR* kNodeBtnText[3] = { TEXT("CAMPWAR_1_B_C_Text"),       TEXT("CAMPWAR_2_B_C_Text"),    TEXT("CAMPWAR_2_B_C_Text_1") };

	// Записать текст в узел (пустой указатель — ничего).
	void SetT(UTextBlock* T, const FString& S) { if (T) { T->SetText(FText::FromString(S)); } }
	// Текст узла (пустой указатель — пустая строка).
	FString GetT(const UTextBlock* T) { return T ? T->GetText().ToString() : FString(); }
}

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════

void UAsdaFactionSelectSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только КЛАДЁМ тело в очередь.
		HSquadPacket = Net->OnCampWarSquadPacket.AddLambda([this](int32 Op, const TArray<uint8>& B)
		{
			// ★ 30.10.2026: 6701 разбирает модель «Битвы армий» (`UAsdaCampWarSubsystem` — единственный владелец `CCampWar`);
			//   численность армий окно берёт оттуда (`GetTeamCount`).
			if (Op == OP_CLAN_CREATE_RESULT) { PendingResults.Push(B); }
		});
		// `Recv_ClanMyMembership@0xCA4AB0` кладёт очки в `CCampWar+2946`, звание в `+2950` — их читает ответ 6703 код 1.
		HSelfFaction = Net->OnSelfFaction.AddWeakLambda(this, [this](int32 /*Fid*/, int32 Pts, int32 Gr)
		{
			LastSelfPoints = Pts;
			LastSelfGrade = Gr;
		});
	}
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		// [ОРИГИНАЛ] ESC: окну 44000 `CCampWar_InitSelectWindow` ставит признак +0x228 (`helper_setByteField552`), ветка
		//   события 0x15 оконной процедуры (`CClan_OnClanUIInput`, a2 == 0xABE0) — `sub_9242D9` → скрыть 44000.
		//   У 46000 признака нет — проход ESC его не закрывает.
		WM->RegisterEscCloser(kSelectWindowId, FSimpleDelegate::CreateUObject(this, &UAsdaFactionSelectSubsystem::CloseSelectWindow));
	}
	UE_LOG(LogAsdaFactionSel, Log, TEXT("[армия] подсистема окон 44000/46000 поднята"));
}

void UAsdaFactionSelectSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnCampWarSquadPacket.Remove(HSquadPacket);
		Net->OnSelfFaction.Remove(HSelfFaction);
	}
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterEscCloser(kSelectWindowId);
	}
	ResetConfirmWidget();
	ResetSelectWidget();
	Super::Deinitialize();
}

void UAsdaFactionSelectSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Окна оригинала — контролы `CCampWar`; перезагрузка интерфейсов прячет их (корни 44000/46000 `Visible = 0`).
	if (bConfirmOpen) { HideConfirm(); }
	if (bSelectOpen) { CloseSelectWindow(); }
	PendingSquad = -1;
	LastSelfPoints = 0;
	LastSelfGrade = 0;
	PendingResults.Clear();
	UE_LOG(LogAsdaFactionSel, Log, TEXT("[армия] сброс сессии (%s): окна скрыты, очередь 6703 пуста"), AsdaSessionResetReasonName(Reason));
}

void UAsdaFactionSelectSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.FactionSelect");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	// 6703 разбираем из Tick (закон no-reentrant-net-callbacks).
	if (TArray<TArray<uint8>> Batch; PendingResults.Drain(Batch))
	{
		for (const TArray<uint8>& B : Batch) { OnSelectResult(B); }
	}
}

// ═════════════════════════════ ОКНА ═══════════════════════════════════════════════════════════

bool UAsdaFactionSelectSubsystem::EnsureSelectWidget()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kSelectWindowId, kSelectWbp, kSelectZ).Ensure(GetGameInstance(), SelectWidget, bSelectReady,
		[this](UAsdaWindowSubsystem&) { ResetSelectWidget(); },
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupSelectControls();
			// Рамка окна — девять планов `CAMPWAR_Win_0_0…8` (скин `Win_Mg`, дамп ассета).
			FAsdaJuiWindowHandle::RegisterFrameZone(WM, kSelectWindowId, this, SelectWidget, bSelectOpen, TEXT("CAMPWAR_Win_0_%d"), 9);
		},
		[]() { UE_LOG(LogAsdaFactionSel, Error, TEXT("[армия] WBP_CAMPWAR_Win не загрузился (%s)"), kSelectWbp); });
	return FAsdaJuiWindowHandle::Succeeded(R) && bSelectReady;
}

bool UAsdaFactionSelectSubsystem::EnsureConfirmWidget()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kConfirmWindowId, kConfirmWbp, kConfirmZ).Ensure(GetGameInstance(), ConfirmWidget, bConfirmReady,
		[this](UAsdaWindowSubsystem&) { ResetConfirmWidget(); },
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupConfirmControls();
			// Рамка окна — девять планов `CAMPWAR_select_0_0…8` (дамп ассета).
			FAsdaJuiWindowHandle::RegisterFrameZone(WM, kConfirmWindowId, this, ConfirmWidget, bConfirmOpen, TEXT("CAMPWAR_select_0_%d"), 9);
		},
		[]() { UE_LOG(LogAsdaFactionSel, Error, TEXT("[армия] WBP_CAMPWAR_select не загрузился (%s)"), kConfirmWbp); });
	return FAsdaJuiWindowHandle::Succeeded(R) && bConfirmReady;
}

void UAsdaFactionSelectSubsystem::ResetSelectWidget()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kSelectWindowId);
		WM->CloseWindow(kSelectWindowId);
	}
	SelectWidget = nullptr;
	bSelectReady = false;
	bSelectOpen = false;
	TxtTitle = TxtClose = nullptr;
	BtnX = BtnClose = nullptr;
	for (int32 I = 0; I < kSquadCount; ++I)
	{
		Marks[I] = BarBack[I] = BarFill[I] = nullptr;
		Names[I] = Percents[I] = TxtSelect[I] = nullptr;
		BtnSelect[I] = nullptr;
		BarFrac[I] = 0.f;
	}
}

void UAsdaFactionSelectSubsystem::ResetConfirmWidget()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kConfirmWindowId);
		WM->CloseWindow(kConfirmWindowId);
	}
	ConfirmWidget = nullptr;
	bConfirmReady = false;
	bConfirmOpen = false;
	ConfirmMark = nullptr;
	ConfirmName = ConfirmText = TxtOk = TxtNo = nullptr;
	BtnOk = BtnNo = nullptr;
}

void UAsdaFactionSelectSubsystem::SetupSelectControls()
{
	bSelectReady = false;
	if (!SelectWidget || !SelectWidget->WidgetTree) { return; }
	auto Find = [this](const TCHAR* Name) { return SelectWidget->GetWidgetFromName(FName(Name)); };
	TxtTitle = Cast<UTextBlock>(Find(TEXT("CAMPWAR_text")));        // 44001
	BtnX     = Cast<UButton>(Find(TEXT("CAMPWAR_Win_BTN00")));      // 44002
	BtnClose = Cast<UButton>(Find(TEXT("CAMPWAR_Clik_M")));         // 44099
	TxtClose = Cast<UTextBlock>(Find(TEXT("CAMPWAR_Clik_M_Text"))); // подпись 44099
	for (int32 I = 0; I < kSquadCount; ++I)
	{
		Marks[I]     = Cast<UImage>(Find(kNodeMark[I]));
		Names[I]     = Cast<UTextBlock>(Find(kNodeName[I]));
		BarBack[I]   = Cast<UImage>(Find(kNodeBarBack[I]));
		BarFill[I]   = Cast<UImage>(Find(kNodeBarFill[I]));
		Percents[I]  = Cast<UTextBlock>(Find(kNodePct[I]));
		BtnSelect[I] = Cast<UButton>(Find(kNodeBtn[I]));
		TxtSelect[I] = Cast<UTextBlock>(Find(kNodeBtnText[I]));
	}

	// ── Кнопки: `CClan_OnMemberListUI@0xC9E920` (диспетчер по IDNum). Звука клика у окна НЕТ. ──
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void()>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, [F = MoveTemp(Fn)](int32) { F(); }, Relays);
	};
	for (int32 I = 0; I < kSquadCount; ++I)
	{
		// a2 ∈ {44025, 44035, 44045} → `rankIdx = (a2 − 44025) / 10` → `campWarShowSlotWindow(rankIdx)`.
		Bind(BtnSelect[I], kIdSelectBase + 10 * I, [this, I]() { OnSelectClicked(I); });
	}
	Bind(BtnX,     kIdX,     [this]() { CloseSelectWindow(); });   // 44002 → `sub_9242D9`
	Bind(BtnClose, kIdClose, [this]() { CloseSelectWindow(); });   // 44099 → `sub_9242D9`

	// ── Статичные подписи и начальное состояние `CCampWar_InitSelectWindow@0xC971A0` (combat_pvp.c:4317-4386) ──
	SetT(TxtTitle, Loc(AsdaFactionSelectLoc::kTitle));                           // 44001 ← 10001
	for (int32 I = 0; I < kSquadCount; ++I)
	{
		UAsdaClanFlag::ApplyCampMark(Marks[I], I, 64.f);                         // 44020+10i ← ячейка `uiMakeAxisVec2(i)` 64×64
		SetT(Names[I], Loc(AsdaFactionSelectLoc::kFactionBase + I));             // 44027+10i ← 10191+i
		SetT(Percents[I], TEXT("0%"));                                           // 44029+10i ← «0%»
		SetT(TxtSelect[I], Loc(AsdaFactionSelectLoc::kBtnSelect));               // 44025+10i ← 10002 (`Collection_AddString`)
		// Полоса 44050+10i: `CProgressBar_ctor` — диапазон 0…100, тип 5 (слева направо); `uiProgress_applyTypeFromXML`
		//   обнуляет правый край заливки ⇒ до первого `SetValue` полоса пуста.
		FAsdaGaugeFill::Clip(BarFill[I], 0.f, kBarFullW, 0.f);
		BarFrac[I] = 0.f;
	}
	SetT(TxtClose, Loc(AsdaFactionSelectLoc::kBtnClose));                        // 44099 ← 5035

	FoundControls = 0; TotalControls = 0;
	auto Count = [this](const UObject* O) { ++TotalControls; if (O) { ++FoundControls; } };
	Count(TxtTitle); Count(BtnX); Count(BtnClose); Count(TxtClose);
	bool bRows = true;
	for (int32 I = 0; I < kSquadCount; ++I)
	{
		Count(Marks[I]); Count(Names[I]); Count(BarBack[I]); Count(BarFill[I]); Count(Percents[I]); Count(BtnSelect[I]); Count(TxtSelect[I]);
		bRows = bRows && BtnSelect[I] && Percents[I] && BarFill[I] && Names[I];
	}
	bSelectReady = bRows && BtnX && BtnClose;
	UE_LOG(LogAsdaFactionSel, Log, TEXT("%s"), *DumpControls());
	if (!bSelectReady)
	{
		UE_LOG(LogAsdaFactionSel, Error, TEXT("[армия] ключевые контролы окна 44000 не разобраны (найдено %d/%d)"), FoundControls, TotalControls);
	}
}

void UAsdaFactionSelectSubsystem::SetupConfirmControls()
{
	bConfirmReady = false;
	if (!ConfirmWidget || !ConfirmWidget->WidgetTree) { return; }
	auto Find = [this](const TCHAR* Name) { return ConfirmWidget->GetWidgetFromName(FName(Name)); };
	ConfirmMark = Cast<UImage>(Find(TEXT("CAMPWAR_select_M3_18_0")));       // 46012
	ConfirmName = Cast<UTextBlock>(Find(TEXT("CAMPWAR_select_TEXT_1")));    // 46020
	ConfirmText = Cast<UTextBlock>(Find(TEXT("CAMPWAR_select_TEXT")));      // 46030
	BtnOk = Cast<UButton>(Find(TEXT("CAMPWAR_select_OK_C")));               // 46040
	TxtOk = Cast<UTextBlock>(Find(TEXT("CAMPWAR_select_OK_C_Text")));       // подпись 46040
	BtnNo = Cast<UButton>(Find(TEXT("CAMPWAR_select_NO_C")));               // 46050
	TxtNo = Cast<UTextBlock>(Find(TEXT("CAMPWAR_select_NO_C_Text")));       // подпись 46050

	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void()>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, [F = MoveTemp(Fn)](int32) { F(); }, Relays);
	};
	Bind(BtnOk, kIdCfOk, [this]() { OnConfirmOk(); });       // 46040 → 6702 + скрыть
	Bind(BtnNo, kIdCfNo, [this]() { OnConfirmCancel(); });   // 46050 → скрыть

	// ── `CCampWar_InitSelectMsg@0xC97630` (combat_pvp.c:4390-4423) ──
	SetT(ConfirmName, FString());                                        // 46020 ← пусто (`&Src`)
	SetT(ConfirmText, Loc(AsdaFactionSelectLoc::kConfirmText));         // 46030 ← 10003
	SetT(TxtOk, Loc(AsdaFactionSelectLoc::kConfirmOk));                 // 46040 ← 2288
	SetT(TxtNo, Loc(AsdaFactionSelectLoc::kConfirmNo));                 // 46050 ← 2289

	auto Count = [this](const UObject* O) { ++TotalControls; if (O) { ++FoundControls; } };
	Count(ConfirmMark); Count(ConfirmName); Count(ConfirmText); Count(BtnOk); Count(TxtOk); Count(BtnNo); Count(TxtNo);
	bConfirmReady = BtnOk && BtnNo && ConfirmName && ConfirmMark;
	if (!bConfirmReady)
	{
		UE_LOG(LogAsdaFactionSel, Error, TEXT("[армия] ключевые контролы окна 46000 не разобраны"));
	}
}

// ═════════════════════════════ ВХОД / ЗАКРЫТИЕ ════════════════════════════════════════════════

bool UAsdaFactionSelectSubsystem::IsWarInfoNpc(int32 NpcId)
{
	// `itemIsCategory133or179_184@0xDA3E90` (имя врёт: это НЕ предмет, а id NPC разговора): 133, 179, 180, 184 → 1.
	return NpcId == 133 || NpcId == 179 || NpcId == 180 || NpcId == 184;
}

bool UAsdaFactionSelectSubsystem::OnNpcTalkOpened(int32 NpcId)
{
	if (!IsWarInfoNpc(NpcId)) { return false; }
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	// [ОРИГИНАЛ] `Clan_Send_Op6700(g_pCampWarClan, 0)` — ответ сети не проверяется.
	const bool bSent = Net && Net->SendCampWarMainInfo6700(Net->SelectedCharId, 0);
	UE_LOG(LogAsdaFactionSel, Log, TEXT("[армия] разговор с NPC войны %d → запрос 6700 (режим 0) %s"),
		NpcId, bSent ? TEXT("ушёл") : TEXT("НЕ ушёл (нет связи/шлюз)"));
	return true;
}

void UAsdaFactionSelectSubsystem::OpenFromNpc()
{
	// `campWarWindowShow@0xC9AB10` (combat_pvp.c:5076-5131).
	if (!EnsureSelectWidget()) { return; }
	bSelectOpen = true;
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kSelectWindowId, true);
		// [ПОРТ] эталон здесь `CClass_MapInsert` не зовёт: окно и так верхнее — разговор и прочие окна закрыты
		//   `uiCloseAllWindows` при входе в диалог. У порта z-порядок окон живёт дольше — поднимаем явно.
		WM->BringWindowToFront(kSelectWindowId);
	}
	// [ОРИГИНАЛ] Строка `CampWar_BaseTable` для слота карты (`sub_CA1440`, 3 → 0, 0 → 1, 7 → 2, иначе 0) у эталона есть
	//   всегда (в таблице 3 записи, слот 0…2), поэтому проценты пересчитываются при каждом показе.
	ApplyPercents();
	UE_LOG(LogAsdaFactionSel, Log, TEXT("[армия] окно 44000 открыто: численность %d/%d/%d (6701 %s)"),
		GetSquadCount(0), GetSquadCount(1), GetSquadCount(2), HasMainInfo() ? TEXT("пришёл") : TEXT("не приходил"));
}

void UAsdaFactionSelectSubsystem::CloseSelectWindow()
{
	// `sub_C9ACE0`: `CControl_SetVisible(this[70], 0)`. Окно 46000 не трогается (как эталон).
	bSelectOpen = false;
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kSelectWindowId, false);
	}
	UE_LOG(LogAsdaFactionSel, Log, TEXT("[армия] окно 44000 закрыто"));
}

void UAsdaFactionSelectSubsystem::ApplyPercents()
{
	// Тело цикла `campWarWindowShow`: сумма трёх i16, доля строки ×100; сумма ≤ 0 ⇒ 0.
	// Численность армий — `CCampWar+2991/2993/2995` (6701), владелец модели — `UAsdaCampWarSubsystem`.
	int32 Cnt[kSquadCount] = { 0, 0, 0 };
	if (const UAsdaCampWarSubsystem* CW = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCampWarSubsystem>() : nullptr)
	{
		for (int32 I = 0; I < kSquadCount; ++I) { Cnt[I] = CW->GetTeamCount(I); }   // i16 со знаком, как эталон
	}
	const int32 Total = Cnt[0] + Cnt[1] + Cnt[2];
	for (int32 I = 0; I < kSquadCount; ++I)
	{
		float Pct = (Total > 0) ? (float)((double)Cnt[I] / (double)Total * 100.0) : 0.f;
		// `CProgressBar_SetValue`: значение вне диапазона 0…100 прижимается к краю.
		const float Clamped = FMath::Clamp(Pct, 0.f, kBarMax);
		BarFrac[I] = Clamped / kBarMax;
		FAsdaGaugeFill::Clip(BarFill[I], 0.f, kBarFullW, BarFrac[I]);
		SetT(Percents[I], FString::Printf(TEXT("%.2f%%"), Pct));
	}
}

// ═════════════════════════════ КНОПКИ ═════════════════════════════════════════════════════════

void UAsdaFactionSelectSubsystem::OnSelectClicked(int32 Squad)
{
	// `campWarShowSlotWindow@0xC9AD10`: `rankIdx <= 2` проверил вызывающий.
	if (Squad < 0 || Squad >= kSquadCount) { return; }
	if (!EnsureConfirmWidget()) { return; }
	PendingSquad = Squad;                                                   // `self[2244] = a2`
	bConfirmOpen = true;
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kConfirmWindowId, true);
		WM->BringWindowToFront(kConfirmWindowId);                          // `CClass_MapInsert(g_pWindowMgr, self[89])`
	}
	SetT(ConfirmName, Loc(AsdaFactionSelectLoc::kFactionBase + Squad));    // 46020 ← 10191+i
	UAsdaClanFlag::ApplyCampMark(ConfirmMark, Squad, 64.f);                 // 46012 ← ячейка `uiMakeAxisVec2(i)`
	UE_LOG(LogAsdaFactionSel, Log, TEXT("[армия] «выбрать» строки %d → подтверждение 46000"), Squad);
}

void UAsdaFactionSelectSubsystem::OnConfirmOk()
{
	// Ветка a2 == 46040: `Clan_Send_Op6702(self, self[2244])`, затем `CControl_SetVisible(self[89], 0)`.
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bSent = Net && Net->SendCampWarSelectSquad6702(Net->SelectedCharId, PendingSquad);
	HideConfirm();
	UE_LOG(LogAsdaFactionSel, Log, TEXT("[армия] ОК подтверждения: 6702 армия %d %s"), PendingSquad,
		bSent ? TEXT("ушёл") : TEXT("НЕ ушёл (нет связи/шлюз)"));
}

void UAsdaFactionSelectSubsystem::OnConfirmCancel()
{
	// Ветка a2 == 46050: только скрыть, пакета нет.
	HideConfirm();
	UE_LOG(LogAsdaFactionSel, Log, TEXT("[армия] отмена подтверждения 46000"));
}

void UAsdaFactionSelectSubsystem::HideConfirm()
{
	bConfirmOpen = false;
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kConfirmWindowId, false);
	}
}

// ═════════════════════════════ СЕТЬ ═══════════════════════════════════════════════════════════

void UAsdaFactionSelectSubsystem::OnSelectResult(const TArray<uint8>& B)
{
	// `Recv_ClanCreateResult@0xCA3DF0`: u8 код, i16 армия.
	if (B.Num() < k6703Len) { return; }
	const int32 Code = B[0];
	const int32 Squad = AsdaBytes::RdI16(&B[1]);
	UE_LOG(LogAsdaFactionSel, Log, TEXT("[армия] ←6703: код %d, армия %d"), Code, Squad);
	switch (Code)
	{
	case 1:
	{
		// `*(i16*)(this+2940) = армия` (своя армия) и `CFactionUI_UpdateInfo(g_pCharInfoWnd, армия, очки, звание)`:
		//   звание ≤ 0 ⇒ (армия, 0, 0); иначе — очки и звание из последнего 6713 (`this+2946`, `this+2950`). Общая точка
		//   порта для «своей армии» — делегат 6713 `OnSelfFaction` (мир: `SelfCampId` + плашка; окно персонажа 900).
		const bool bGrade = LastSelfGrade > 0;
		const int32 Pts = bGrade ? LastSelfPoints : 0;
		const int32 Gr  = bGrade ? LastSelfGrade : 0;
		const int32 KeepPts = LastSelfPoints, KeepGr = LastSelfGrade;   // эталон `this+2946/2950` не трогает
		if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
		{
			Net->OnSelfFaction.Broadcast(Squad, Pts, Gr);
		}
		LastSelfPoints = KeepPts; LastSelfGrade = KeepGr;
		// 10008 «Ты присоединяешься к армии '[ss1]'», [ss1] = имя армии (`CObject_SetSlotString(…, 0, имя)`).
		const FString Name = Loc(AsdaFactionSelectLoc::kFactionBase + Squad);
		FString Text;
		if (UAsdaTextSubsystem* TS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr)
		{
			Text = TS->FormatById(AsdaFactionSelectLoc::kResJoined, { Name }, {});
		}
		if (Text.IsEmpty()) { Text = Loc(AsdaFactionSelectLoc::kResJoined).Replace(TEXT("[ss1]"), *Name); }
		Notice(AsdaFactionSelectLoc::kResJoined, Text);
		break;
	}
	case 0: Notice(AsdaFactionSelectLoc::kResFailed,  Loc(AsdaFactionSelectLoc::kResFailed));  break;
	case 2: Notice(AsdaFactionSelectLoc::kResAlready, Loc(AsdaFactionSelectLoc::kResAlready)); break;
	case 3: Notice(AsdaFactionSelectLoc::kResJob,     Loc(AsdaFactionSelectLoc::kResJob));     break;
	case 4: Notice(AsdaFactionSelectLoc::kResFull,    Loc(AsdaFactionSelectLoc::kResFull));    break;
	case 5: Notice(AsdaFactionSelectLoc::kResBusy,    Loc(AsdaFactionSelectLoc::kResBusy));    break;
	default:
		// [ОРИГИНАЛ] `default: return` — молча.
		break;
	}
}

// ═════════════════════════════ ПРОЧЕЕ ═════════════════════════════════════════════════════════

void UAsdaFactionSelectSubsystem::Notice(int32 LocId, const FString& Text)
{
	// Все исходы 6703 — однокнопочное окно-сообщение `CItem_BuildTooltipText(g_pItemTooltipUI, 0, id, текст, 1)`:
	//   noticeId эталона = сам loc-id (пара очереди окон (0, id) глушит повтор, как у оригинала).
	NoticeTrace.Add(LocId);
	LastNoticeText = Text;
	AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::War, Text, AsdaLoc::kBtnOk, LocId);
}

FString UAsdaFactionSelectSubsystem::Loc(int32 LocId) const
{
	return AsdaNotice::Loc(GetGameInstance(), LocId);
}

bool UAsdaFactionSelectSubsystem::HasMainInfo() const
{
	const UAsdaCampWarSubsystem* CW = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCampWarSubsystem>() : nullptr;
	return CW && CW->HasMainInfo();
}

int32 UAsdaFactionSelectSubsystem::GetSquadCount(int32 Squad) const
{
	const UAsdaCampWarSubsystem* CW = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCampWarSubsystem>() : nullptr;
	return CW ? CW->GetTeamCount(Squad) : -1;
}

float UAsdaFactionSelectSubsystem::GetBarFrac(int32 Squad) const
{
	return (Squad >= 0 && Squad < kSquadCount && BarFill[Squad]) ? BarFrac[Squad] : -1.f;
}

FString UAsdaFactionSelectSubsystem::GetText(int32 IdNum) const
{
	switch (IdNum)
	{
	case kIdTitle:  return GetT(TxtTitle);
	case kIdClose:  return GetT(TxtClose);
	case kIdCfName: return GetT(ConfirmName);
	case kIdCfText: return GetT(ConfirmText);
	case kIdCfOk:   return GetT(TxtOk);
	case kIdCfNo:   return GetT(TxtNo);
	default: break;
	}
	for (int32 I = 0; I < kSquadCount; ++I)
	{
		if (IdNum == kIdNameBase + 10 * I)   { return GetT(Names[I]); }
		if (IdNum == kIdPctBase + 10 * I)    { return GetT(Percents[I]); }
		if (IdNum == kIdSelectBase + 10 * I) { return GetT(TxtSelect[I]); }
	}
	return FString();
}

bool UAsdaFactionSelectSubsystem::IsMarkShown(int32 IdNum) const
{
	const UImage* Img = nullptr;
	if (IdNum == kIdCfMark) { Img = ConfirmMark; }
	for (int32 I = 0; I < kSquadCount; ++I) { if (IdNum == kIdMarkBase + 10 * I) { Img = Marks[I]; } }
	return Img && Img->GetBrush().GetResourceObject() != nullptr;
}

UWidget* UAsdaFactionSelectSubsystem::GetHitWidget(int32 IdNum) const
{
	switch (IdNum)
	{
	case kIdX:     return BtnX;
	case kIdClose: return BtnClose;
	case kIdCfOk:  return BtnOk;
	case kIdCfNo:  return BtnNo;
	default: break;
	}
	for (int32 I = 0; I < kSquadCount; ++I) { if (IdNum == kIdSelectBase + 10 * I) { return BtnSelect[I]; } }
	return nullptr;
}

FString UAsdaFactionSelectSubsystem::DumpControls() const
{
	auto L = [](const TCHAR* What, const UObject* O)
	{
		return FString::Printf(TEXT("\n  %-34s %s"), What, O ? *O->GetName() : TEXT("— НЕ НАЙДЕН"));
	};
	FString S = FString::Printf(TEXT("[армия] контролы окон 44000/46000 (найдено %d из %d):"), FoundControls, TotalControls);
	S += L(TEXT("44001 заголовок"), TxtTitle);
	S += L(TEXT("44002 X"), BtnX);
	S += L(TEXT("44099 «Закрыть»"), BtnClose);
	S += L(TEXT("44099 подпись"), TxtClose);
	for (int32 I = 0; I < kSquadCount; ++I)
	{
		S += L(*FString::Printf(TEXT("%d знак"), kIdMarkBase + 10 * I), Marks[I]);
		S += L(*FString::Printf(TEXT("%d имя"), kIdNameBase + 10 * I), Names[I]);
		S += L(*FString::Printf(TEXT("%d полоса ES0"), kIdBarBase + 10 * I), BarBack[I]);
		S += L(*FString::Printf(TEXT("%d полоса ES1"), kIdBarBase + 10 * I), BarFill[I]);
		S += L(*FString::Printf(TEXT("%d процент"), kIdPctBase + 10 * I), Percents[I]);
		S += L(*FString::Printf(TEXT("%d «выбрать»"), kIdSelectBase + 10 * I), BtnSelect[I]);
		S += L(*FString::Printf(TEXT("%d подпись"), kIdSelectBase + 10 * I), TxtSelect[I]);
	}
	S += L(TEXT("46012 знак"), ConfirmMark);
	S += L(TEXT("46020 имя"), ConfirmName);
	S += L(TEXT("46030 текст"), ConfirmText);
	S += L(TEXT("46040 ОК"), BtnOk);
	S += L(TEXT("46050 Отмена"), BtnNo);
	return S;
}

// ═════════════════════════════ КОНСОЛЬ ════════════════════════════════════════════════════════
#if ASDA_DEV_CONSOLE   // античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaFactionSelDump(
	TEXT("asda.Faction.Dump"),
	TEXT("asda.Faction.Dump — окно выбора армии 44000/46000: численность из 6701, открытость окон, своя армия"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaFactionSelectSubsystem* F = GI ? GI->GetSubsystem<UAsdaFactionSelectSubsystem>() : nullptr;
		if (!F) { UE_LOG(LogAsdaFactionSel, Warning, TEXT("[армия] подсистемы нет")); return; }
		UE_LOG(LogAsdaFactionSel, Log, TEXT("[армия] 6701 %s: %d/%d/%d; окно 44000 %s, 46000 %s (армия %d); 6713 очки %d звание %d"),
			F->HasMainInfo() ? TEXT("есть") : TEXT("не было"), F->GetSquadCount(0), F->GetSquadCount(1), F->GetSquadCount(2),
			F->IsSelectOpen() ? TEXT("открыто") : TEXT("закрыто"), F->IsConfirmOpen() ? TEXT("открыто") : TEXT("закрыто"),
			F->GetPendingSquad(), F->GetLastSelfPoints(), F->GetLastSelfGrade());
		UE_LOG(LogAsdaFactionSel, Log, TEXT("%s"), *F->DumpControls());
	}));
#endif // ASDA_DEV_CONSOLE
