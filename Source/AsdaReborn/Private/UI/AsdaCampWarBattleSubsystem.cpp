// Copyright (c) 2026 @sitxovski (Discord)
// Реализация боевой фазы «Битвы армий» (карта 19): модель `CCampWar` (боевые поля), приём 67xx, отправки, переключатель HUD,
//   кадровый тик точек. Окна — `AsdaCampWarBattleWindows.cpp`. Разбор эталона — шапка `AsdaCampWarBattleSubsystem.h` и
//   `Reports/faction_select/BATTLE_SPEC.md` (адреса функций и смещения — оттуда).

#include "UI/AsdaCampWarBattleSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "UI/AsdaUiHelpers.h"   // ★ R1: прямое включение (раньше приходило транзитивно)
#include "AsdaCampWarBattleInternal.h"
#include "AsdaDevConsole.h"                 // ASDA_DEV_CVAR / ASDA_DEV_CONSOLE
#include "UI/AsdaCampWarSubsystem.h"        // общая модель CCampWar (ResetState, расписание, таймер боя)
#include "UI/AsdaNotice.h"                  // Loc / MsgBox / Plank
#include "UI/AsdaWindowSubsystem.h"         // ShowGameConfirm, окна
#include "UI/AsdaChatSubsystem.h"           // `CChat_AddTokenizedLines` (журнал + буфер)
#include "UI/AsdaChatWindowSubsystem.h"     // режим чата 9 и флаг префикса (`toggleField105Swap`)
#include "UI/AsdaPartySubsystem.h"          // выход из отряда 5098 (ToggleVisible a3)
#include "UI/AsdaSystemMenuSubsystem.h"     // системное меню прячется на время боя
#include "UI/AsdaInventorySubsystem.h"      // 6733: золото и запись предметов
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaSelf.h"                       // свой charId / userNum
#include "AsdaWeightWire.h"                 // вес u16 по проводу
#include "Login/AsdaLoginData.h"            // DecodeCp1251 — имена в записях сквада
#include "AsdaEffectActor.h"                // эффекты `.jeb`
#include "Net/NetOpcodes.h"
#include "Npc/AsdaNpcActor.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "World/AsdaClanFlag.h"             // «карта кампвара» (+69)
#include "World/AsdaCoord.h"
#include "World/AsdaFloatingTextSubsystem.h"
#include "World/AsdaGroundTrace.h"
#include "World/AsdaKingCrownSubsystem.h"   // состояние действия своего персонажа (6 — мёртв)
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"        // ★ 30.10.2026: поза захвата 47 (`EnterCampPose`)

#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/Material.h"                // базовый мастер слота (тон точки)
#include "Materials/MaterialInstanceDynamic.h" // тон объекта точки — `ObjTint` мастера `M_AsdaObjectLit_CampTint`
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCampWarBattle, Log, All);

// ═════════════════════════════ КЛЮЧИ (разбор — шапка .h) ═════════════════════════════════════════
static ASDA_DEV_CVAR(int32, CVarCwBattle, TEXT("asda.CampWar.Battle"), 1,
	TEXT("[ПОРТ] Боевая фаза «Битвы армий» по оригиналу: 1 — HUD боя по 5055, пакеты 6716…6742, окна 49600/49900/49910/49940/49510/49800/49399; "
	     "0 — прежний порт (HUD не включается, боевые пакеты глотаются, 17520 по флагу карты и клик только пишет лог)."), ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarCwTickNoBreak, TEXT("asda.CampWar.TickNoBreak"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Д-Б2: 1 — конец захвата (≥ 10000 мс) не обрывает кадровый тик остальных точек; 0 — как оригинал (`return`)."), ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarCwTimerThirdDigit, TEXT("asda.CampWar.TimerThirdDigit"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Д-Б3: 1 — третья цифра таймера точки рисуется в глиф [3]; 0 — как оригинал (снова в [2], [3] пуст)."), ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarCwInviteFix, TEXT("asda.CampWar.InviteFix"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Д-Б4: 1 — 49410 «Пригласить»: цель уже в отряде → нотис 1047, имя — найденного члена; 0 — как оригинал "
	     "(пустая строка в чат; имя по индексу строки)."), ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarCwSlotCountFix, TEXT("asda.CampWar.SlotCountFix"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Д-Б5: 1 — число точек +0x23D0 = max(номер + 1) (повтор 6726 не раздувает); 0 — как оригинал (++ на каждый 6726)."), ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarCwHoldBarKeep, TEXT("asda.CampWar.HoldBarKeep"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Д-Б7: 1 — конец чужого захвата не прячет мою полосу 49510; 0 — как оригинал (прячет всегда)."), ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarCwZoneUnion, TEXT("asda.CampWar.ZoneUnion"), 0,
	TEXT("[ИСПРАВЛЕНИЕ ПОРТА] 31.10.2026: 0 — у табло очков 49600 и карты 49900 своя зона «окно» у каждого узла (клик в мир проходит "
	     "между ними); 1 — прежний порт: одна зона-объединение узлов, накрывавшая верхнюю половину экрана."), ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarCwNoticeNames, TEXT("asda.CampWar.NoticeNames"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Д-Б8: 1 — имя в нотисах 10070/10071/10072 стоит во всех слотах [ss1]…[ss3]; 0 — как оригинал (только [ss1] — "
	     "в 10071/10072 имени нет)."), ECVF_Default);

bool UAsdaCampWarBattleSubsystem::IsBattleOn() { return CVarCwBattle.GetValueOnGameThread() != 0; }
bool AsdaCwBattle::TimerThirdDigitFix() { return CVarCwTimerThirdDigit.GetValueOnGameThread() != 0; }
bool AsdaCwBattle::InviteFix() { return CVarCwInviteFix.GetValueOnGameThread() != 0; }
bool AsdaCwBattle::HoldBarKeep() { return CVarCwHoldBarKeep.GetValueOnGameThread() != 0; }
bool AsdaCwBattle::ZoneUnionLegacy() { return CVarCwZoneUnion.GetValueOnGameThread() != 0; }

using namespace AsdaCwBattle;

namespace
{
	// Эффекты света точек по армии (`aCampLight01Jeb[20·f]` @0x15B5C50, `aCampLight02Jeb` @0x15B5C8C; сверено с дампом).
	const TCHAR* kLight01[3] = { TEXT("Camp_Light_01"), TEXT("Camp_Dark_01"), TEXT("Camp_Chaos_01") };
	const TCHAR* kLight02[3] = { TEXT("Camp_Light_02"), TEXT("Camp_Dark_02"), TEXT("Camp_Chaos_02") };
	// Точки возрождения армий для `Camp_start.jeb` (`g_aCampSpawnPos` @0x2C00C94, игровые x/z; высота — по рельефу).
	const FVector2D kSpawn[3] = { FVector2D(26300.f, 9900.f), FVector2D(11000.f, 35900.f), FVector2D(42500.f, 36900.f) };
	// Позиции 10 точек на карте боя 200×200 (`CMiniMap_InitDefaultColors@0xB4BC60` [МИСНЕЙМ] → `g_pMiniMap+0x360`, дамп 0x2BCA000).
	const FVector2D kPointPos[10] = { FVector2D(86, 28), FVector2D(28, 60), FVector2D(143, 61), FVector2D(58, 74), FVector2D(114, 74),
		FVector2D(85, 89), FVector2D(28, 127), FVector2D(85, 125), FVector2D(143, 128), FVector2D(86, 163) };
	// Боевые опкоды, разбор которых — здесь (6738/6740 — у регистрации; 6722 — у мира). 6715 и 6737 — здесь, чтобы порядок с 6716/6721
	//   и 6726/6729 одного кадра сохранялся (у регистрации своя очередь, порядок тиков подсистем не задан).
	bool IsBattleOp(int32 Op)
	{
		switch (Op)
		{
		case 6715: case 6716: case 6720: case 6721: case 6724: case 6725: case 6726: case 6728: case 6729: case 6730:
		case 6732: case 6733: case 6735: case 6736: case 6737: case 6739: case 6741: case 6742:
			return true;
		default:
			return false;
		}
	}
}

// ═════════════════════════════ ЗАПИСЬ ЧЛЕНА СКВАДА ═══════════════════════════════════════════════

int32 FAsdaCwMember::UserNum() const { return AsdaBytes::RdI16(Raw + 1); }
int32 FAsdaCwMember::CharId() const { return AsdaBytes::RdI32(Raw + 3); }
int32 FAsdaCwMember::Level() const { return AsdaBytes::RdI16(Raw + 10); }
int32 FAsdaCwMember::Squad() const { return AsdaBytes::RdI16(Raw + 55); }
FString FAsdaCwMember::Name() const { return FAsdaLoginData::DecodeCp1251(Raw + 57, 20); }
FString FAsdaCwMember::Leader() const { return FAsdaLoginData::DecodeCp1251(Raw + 77, 20); }
FString FAsdaCwMember::Clan() const { return FAsdaLoginData::DecodeCp1251(Raw + 97, 20); }

FVector2D UAsdaCampWarBattleSubsystem::PointMapPos(int32 S) { return (S >= 0 && S < kSlots) ? kPointPos[S] : FVector2D::ZeroVector; }

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ════════════════════════════════════════════════════

void UAsdaCampWarBattleSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaCampWarSubsystem::StaticClass());
	UGameInstance* GI = GetGameInstance();
	for (int32 Id : { kWinPoint, kWinMap, kWinFont, kWinTime, kWinHold, kWinEnd, kWinAtk, kWinPop }) { Windows.Add(Id, FAsdaCwWindow()); }
	for (int32 S = 0; S < kSlots; ++S) { ResetSlot(Slots[S]); ObjTint[S] = FVector::OneVector; }
	for (int32 R = 0; R < kRows; ++R) { RowMember[R] = -1; RowCharId[R] = 0; }
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только КЛАДЁМ тело в очередь.
		HPacket = Net->OnCampWarSquadPacket.AddWeakLambda(this, [this](int32 Op, const TArray<uint8>& B)
		{
			if (IsBattleOp(Op)) { Pending.Push(TPair<int32, TArray<uint8>>(Op, B)); }
		});
	}
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		// [ОРИГИНАЛ] 49399: `helper_setByteField552(…, 1)` = признак +0x228 «закрывается ESC», событие 0x15 → `uiSynthResultWinClose`.
		WM->RegisterEscCloser(FName(TEXT("CAMPWAR_ATTACK")), FSimpleDelegate::CreateUObject(this, &UAsdaCampWarBattleSubsystem::CloseAttack));
	}
	UE_LOG(LogAsdaCampWarBattle, Log, TEXT("[битва армий/бой] подсистема поднята"));
}

void UAsdaCampWarBattleSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr) { Net->OnCampWarSquadPacket.Remove(HPacket); }
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr) { WM->UnregisterEscCloser(FName(TEXT("CAMPWAR_ATTACK"))); }
	if (CaptureLoop) { CaptureLoop->Stop(); CaptureLoop = nullptr; }
	DropMapLayer();
	ResetWidgets();
	Super::Deinitialize();
}

void UAsdaCampWarBattleSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// [ОРИГИНАЛ] `CCampWar` — статический объект: смена карты его не трогает (уход С карты 19 сбрасывает 5055 → ResetState).
	if (!AsdaIsFullSessionReset(Reason)) { return; }
	// `gameLeaveToCharSelect` → `campWarWndClose` ×2: Toggle(0, 1) + ResetState + ResetPositions + освобождение таблиц.
	//   [ПОРТ] сброс сессии обязан быть локальным (ничего не шлёт) — выход из отряда 5098 (a3) здесь не отправляется:
	//   отряд к этому моменту уже сброшен своей подсистемой (на сервере выход в выбор персонажа его и так закрывает).
	if (bHudOn) { ToggleVisible(false, false); }
	OnResetState();
	bHudOn = false;
	MyIndex = -1; bOperator = false;
	for (int32 F = 0; F < kArmies; ++F) { Headcount[F] = 0; }
	for (FAsdaCwMember& M : Members) { FMemory::Memzero(M.Raw, sizeof(M.Raw)); }
	ListTop = 0; ListSel = -1; MySlot = -1; Hud6741 = -1; ChatSavedState = -1;
	StartFx.Reset(); HandFx.Reset();
	LastEnd.Reset(); bEndDraw = false;
	HoverRow = -1; HoverBar = -1; bPendingServerMove = false;
	NodeVis.Reset(); NodeText.Reset(); TimerSig.Reset(); FadeMs.Reset();
	for (TPair<int32, FAsdaCwWindow>& P : Windows) { P.Value.bShown = false; }
	if (CaptureLoop) { CaptureLoop->Stop(); CaptureLoop = nullptr; }
	Pending.Clear();
	Mouse.Reset();
	UE_LOG(LogAsdaCampWarBattle, Log, TEXT("[битва армий/бой] сброс сессии (%s): HUD снят, модель боя сброшена"), AsdaSessionResetReasonName(Reason));
}

void UAsdaCampWarBattleSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.CampWarBattle");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	if (!IsBattleOn()) { Pending.Clear(); return; }
	if (TArray<TPair<int32, TArray<uint8>>> Batch; Pending.Drain(Batch))
	{
		for (const TPair<int32, TArray<uint8>>& P : Batch) { HandlePacket(P.Key, P.Value); }
	}
	// 4055 / 6013 из сетевого колбэка: `netHandler_EnterWorld@0xF43050` / `portalChannelTransferBegin@0xF49C40`.
	if (bPendingServerMove)
	{
		bPendingServerMove = false;
		if (bHudOn)
		{
			ToggleVisible(false, true);
			if (UAsdaCampWarSubsystem* CW = GetGameInstance()->GetSubsystem<UAsdaCampWarSubsystem>()) { CW->ResetState(); }
			ResetChildPositions();
		}
	}
	UGameInstance* GI = GetGameInstance();
	if (!GI || !GI->GetWorld()) { return; }
	SyncWindows();
	const float DtMs = FMath::Clamp(DeltaTime * 1000.f, 0.f, 1000.f);
	TickPoints(DtMs);
	TickFades(DtMs);
	LayoutPointPanel();
	EnsureMapLayer();
	TickMouse();
}

// ═════════════════════════════ ПЕРЕКЛЮЧАТЕЛЬ HUD ═════════════════════════════════════════════════

void UAsdaCampWarBattleSubsystem::ToggleVisible(bool bShow, bool bLeaveParty)
{
	// `campWarWnd_ToggleVisible@0xCA1690` (BATTLE_SPEC §3.1). Гейт `+0x26C0` («контролы созданы») у порта всегда поднят.
	if (!IsBattleOn()) { return; }
	UGameInstance* GI = GetGameInstance();
	// 1. `toggleField105Swap(g_pChatWnd, a2)`: флаг +0x6A0; a2 = 1 — сохранить режим (+0x1A4 → +0x1A8) и поставить 9 (сквад-чат 6718);
	//    a2 = 0 — если стоит 9, вернуть сохранённый.
	if (UAsdaChatWindowSubsystem* Chat = GI ? GI->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr)
	{
		Chat->SetChannelPrefixMode(bShow);
		if (bShow) { ChatSavedState = Chat->GetInputState(); Chat->SetInputState(9); }
		else if (Chat->GetInputState() == 9 && ChatSavedState >= 0) { Chat->SetInputState(ChatSavedState); }
	}
	// 2. `uiDlgSetDetailVisible(g_pMiniMapWnd, a2)` — миникарта читает `IsHudOn()` (17520, 17501, слой рисунка).
	// 3. Видимость = a2: 49500, 49900, 49904, 49902; 49903 = !a2.
	SetVisNode(kWinTime, 49500, bShow);
	ShowWin(kWinMap, bShow);
	SetVisNode(kWinMap, 49904, bShow);
	SetVisNode(kWinMap, 49902, bShow);
	SetVisNode(kWinMap, 49903, !bShow);
	// 4. a2 → `uiWidgetShowPanel` (49600 + цифры) и `sub_C9B2C0` (+0x231C = 0, спрятать 47000); иначе 49600 и 49510 спрятать.
	if (bShow)
	{
		ShowPointPanel();
		if (UAsdaCampWarSubsystem* CW = GI ? GI->GetSubsystem<UAsdaCampWarSubsystem>() : nullptr) { CW->CloseJoinWindow(); }
	}
	else
	{
		ShowWin(kWinPoint, false);
		ShowWin(kWinHold, false);
	}
	// 5. +0x2388 = a2.
	bHudOn = bShow;
	// 6. a3 и в отряде → `Party_Send_InviteByName(MyPartyIdx, 0, myName)` [МИСНЕЙМ: 5098 «выйти из отряда»].
	if (bLeaveParty)
	{
		UAsdaPartySubsystem* Party = GI ? GI->GetSubsystem<UAsdaPartySubsystem>() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (Party && Net && Party->IsInParty())
		{
			Net->SendPartyLeave5098(Party->GetPartyIndex(), 0, WS ? WS->GetSelfCharName() : FString());
			UE_LOG(LogAsdaCampWarBattle, Log, TEXT("[битва армий/бой] вход на поле боя: выход из отряда 5098 (отряд %d)"), Party->GetPartyIndex());
		}
	}
	// 7. 49940 = a2.
	ShowWin(kWinTime, bShow);
	// 8. `uiSynthResultWinClose` [МИСНЕЙМ] — спрятать 49400 и 49399.
	CloseAttack();
	// 9. `CUIWindow_ShowHide(g_pSystemMainWnd, !a2, 0)`.
	if (UAsdaSystemMenuSubsystem* Sys = GI ? GI->GetSubsystem<UAsdaSystemMenuSubsystem>() : nullptr) { Sys->ShowHide(!bShow, false); }
	// 10. `wnd_ToggleTwoControls(g_pChatWnd, a2)`: 2561 `CHAT_btn_TT` = !a2, 2560 (`MainQuick_Up`) = a2 (окно `CHAT_btn_Win`).
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		if (UUserWidget* Btn = WM->FindWindow(FName(TEXT("CHAT_btn_Win"))))
		{
			if (UWidget* TT = Btn->GetWidgetFromName(TEXT("CHAT_btn_TT_grp")))
			{
				TT->SetVisibility(bShow ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
			}
			if (UWidget* Up = Btn->GetWidgetFromName(TEXT("MainQuick_Up_9_0")))
			{
				Up->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
			}
		}
	}
	UE_LOG(LogAsdaCampWarBattle, Log, TEXT("[битва армий/бой] HUD боя %s (выйти из отряда: %s)"), bShow ? TEXT("ВКЛ") : TEXT("выкл"),
		bLeaveParty ? TEXT("да") : TEXT("нет"));
}

void UAsdaCampWarBattleSubsystem::OnResetState()
{
	// Боевая часть `CCampWar_ResetState@0xC96F20`: 0 в +0xB9C+4i (очки), 10 слотов `sub_C96060`, +0x23D0 = 0, +0x232C/+0x2330 = 0
	//   (+0x2334 — у регистрации), +0x235A = 0. НЕ трогает: +0xB7C, +0x2388, видимость окон, +0x23D4, +0x23E0.
	for (int32 F = 0; F < kArmies; ++F) { Score[F] = 0; }
	for (FAsdaCwPointSlot& S : Slots) { ResetSlot(S); }
	SlotCount = 0;
	TimerMin = 0; TimerSec = 0;
	bWaitList = false;
}

void UAsdaCampWarBattleSubsystem::ResetSlot(FAsdaCwPointSlot& S)
{
	// `sub_C96060`: id −1, state 0, анимация 0, владелец −1, захватчик −1, эффект −1 (БЕЗ `StopByOwner` — 1:1), «свет» −1,
	//   флаг 0, +0x20 0, X/Y 0, цвет (0,0,0); +0x24/+0x28 не трогает.
	S.Id = -1; S.State = 0; S.Anim = 0; S.Owner = -1; S.Capturer = -1; S.Light.Reset(); S.LightKind = -1;
	S.bSent6723 = false; S.CaptureMs = 0; S.CellX = 0; S.CellY = 0; S.Color = FVector::ZeroVector;
}

void UAsdaCampWarBattleSubsystem::StopAllPointEffects()
{
	// `charEffectSlots_StopAll` (из `Clan_Send_6714`): эффекты 10 точек погашены, слоты сброшены, +0x23D0 = 0, +0x23D8 = −1, +0x23DC = 0.
	for (FAsdaCwPointSlot& S : Slots) { StopLight(S); ResetSlot(S); }
	SlotCount = 0;
}

void UAsdaCampWarBattleSubsystem::StopLight(FAsdaCwPointSlot& S)
{
	if (AActor* A = S.Light.Get()) { A->Destroy(); }
	S.Light.Reset();
}

int32 UAsdaCampWarBattleSubsystem::GetSelfArmy() const
{
	// +0xB7C (i16): у порта — мир (`SelfCampId`, общая точка 6713; −2 «ещё не приходил» → −1, как у ctor).
	const UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const int32 C = WS ? WS->GetSelfCampId() : -1;
	return (C < -1) ? -1 : C;
}

void UAsdaCampWarBattleSubsystem::OnLeaveByServerMove(const TCHAR* Why)
{
	if (!IsBattleOn()) { return; }
	bPendingServerMove = true;
	UE_LOG(LogAsdaCampWarBattle, Log, TEXT("[битва армий/бой] %s: HUD %s — сброс в Tick"), Why, bHudOn ? TEXT("включён") : TEXT("выключен"));
}

// ═════════════════════════════ КАДРОВЫЙ ТИК ТОЧЕК (`LightFx_UpdateAll@0xCA5F90`) ════════════════════

int32 UAsdaCampWarBattleSubsystem::FindSlotIndexById(int32 Id) const
{
	// `findSlotIndexById@0xCA76F0`: среди первых +0x23D0 слотов — слот с этим id, иначе −1.
	const int32 N = FMath::Min(SlotCount, (int32)kSlots);
	for (int32 S = 0; S < N; ++S) { if (Slots[S].Id == Id) { return S; } }
	return -1;
}

AAsdaNpcActor* UAsdaCampWarBattleSubsystem::FindPointActor(int32 Id) const
{
	// `List_FindNodeByField460(g_pEntityList, id)` — сущность с ключом +0x1CC = id (у порта — рантайм-копия NPC с этим renderId).
	if (Id < 0) { return nullptr; }
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W) { return nullptr; }
	for (TActorIterator<AAsdaNpcActor> It(W); It; ++It)
	{
		if (It->bNetSpawned && It->NetRenderId == Id) { return *It; }
	}
	return nullptr;
}

FVector UAsdaCampWarBattleSubsystem::HalfColor(int32 F) const { return AsdaCwBattle::HalfColor(F); }
FVector UAsdaCampWarBattleSubsystem::FullColor(int32 F) const { return AsdaCwBattle::FullColor(F); }

void UAsdaCampWarBattleSubsystem::ApplyObjectColor(int32 S, const FVector& C)
{
	// `sub_9354E4(obj, r, g, b)` → `childArray_BatchApply@0x114A5D0` → поле +360 («амбиент» вершинного света) каждой части меша.
	//   [ПОРТ] BaseColor/Emissive умножаются на векторный параметр `ObjTint` ОТДЕЛЬНОГО мастера `M_AsdaObjectLit_CampTint`
	//   (`Tools/asda_campwar_point_tint_mat.py`): на каждый слот меша ТОЛЬКО объекта точки ставится динамический инстанс этого мастера
	//   с параметрами исходного MIC (текстуры NormalStudio, HasMaps, ClassStrength…). Общий `M_AsdaObjectLit` не трогается.
	//   ★ 31.10.2026: прежний путь (Custom Primitive Data 2…4 в общем мастере) обнулил текстуры всех объектов карт и логина —
	//   непрописанные CPD движок читает как 0, умолчание параметра в шейдер не попадает.
	if (S < 0 || S >= kSlots) { return; }
	ObjTint[S] = C;
	AAsdaNpcActor* Obj = FindPointActor(Slots[S].Id);
	UMeshComponent* M = Obj ? Obj->Mesh.Get() : nullptr;
	if (!M) { return; }
	if (!TintMaster)
	{
		TintMaster = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_AsdaObjectLit_CampTint.M_AsdaObjectLit_CampTint"));
		if (!TintMaster)
		{
			UE_LOG(LogAsdaCampWarBattle, Warning, TEXT("[битва армий/бой] нет мастера M_AsdaObjectLit_CampTint — тон точки %d не наложен"), S);
			return;
		}
	}
	const FLinearColor Tint((float)C.X, (float)C.Y, (float)C.Z, 1.f);
	for (int32 I = 0; I < M->GetNumMaterials(); ++I)
	{
		UMaterialInterface* Cur = M->GetMaterial(I);
		UMaterialInstanceDynamic* D = Cast<UMaterialInstanceDynamic>(Cur);
		if (!D || D->Parent != TintMaster)
		{
			// Слот ещё на исходном MIC мастера `M_AsdaObjectLit` — перевести на мастер тона с теми же переопределениями.
			// Другой мастер (не `M_AsdaObjectLit`) — граф иной, тон не применим, слот не трогаем.
			UMaterialInstance* Src = Cast<UMaterialInstance>(Cur);
			const UMaterial* Base = Src ? Src->GetBaseMaterial() : nullptr;
			if (!Base || Base->GetFName() != FName(TEXT("M_AsdaObjectLit"))) { continue; }
			D = UMaterialInstanceDynamic::Create(TintMaster, M);
			D->CopyParameterOverrides(Src);
			M->SetMaterial(I, D);
		}
		D->SetVectorParameterValue(TEXT("ObjTint"), Tint);
	}
}

bool UAsdaCampWarBattleSubsystem::DebugGetMeshTint(int32 Id, FVector& Out, int32* OutSlots) const
{
	AAsdaNpcActor* Obj = FindPointActor(Id);
	UMeshComponent* M = Obj ? Obj->Mesh.Get() : nullptr;
	int32 N = 0;
	if (M && TintMaster)
	{
		for (int32 I = 0; I < M->GetNumMaterials(); ++I)
		{
			UMaterialInstanceDynamic* D = Cast<UMaterialInstanceDynamic>(M->GetMaterial(I));
			if (!D || D->Parent != TintMaster) { continue; }
			FLinearColor V = FLinearColor::White;
			D->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("ObjTint")), V);
			Out = FVector(V.R, V.G, V.B);
			++N;
		}
	}
	if (OutSlots) { *OutSlots = N; }
	return N > 0;
}

void UAsdaCampWarBattleSubsystem::TickPoints(float DtMs)
{
	UpdateObjectTimers();
	const int32 N = FMath::Min(SlotCount, (int32)kSlots);
	for (int32 S = 0; S < N; ++S)
	{
		FAsdaCwPointSlot& P = Slots[S];
		// ── Свет точки: state 1 → `Camp_*_01.jeb`, state 2 → `Camp_*_02.jeb`; объекта нет — погасить, «свет» 0 ──
		if (P.State == 1 || P.State == 2)
		{
			AAsdaNpcActor* Obj = FindPointActor(P.Id);
			if (Obj)
			{
				if (P.LightKind != P.State && P.Owner >= 0 && P.Owner < kArmies)
				{
					StopLight(P);
					// `paneArray_setRect4(obj, 1, 1, 1, 0.0001)` + тон = цвет армии (state 1 — половинный, 2 — полный).
					ApplyObjectColor(S, P.State == 1 ? HalfColor(P.Owner) : FullColor(P.Owner));
					if (AActor* Fx = SpawnFx(P.State == 1 ? kLight01[P.Owner] : kLight02[P.Owner], Obj->GetActorLocation(), nullptr))
					{
						P.Light = Fx;
						P.LightKind = P.State;
					}
				}
			}
			else
			{
				StopLight(P);
				P.LightKind = 0;
			}
		}
		P.TimerMs += (int32)DtMs;   // +0x28 += dt
		// ── Шаги анимации захвата ──
		if (P.Anim == 2)
		{
			if (FindPointActor(P.Id))
			{
				if (MySlot == S) { ShowWin(kWinHold, false); MySlot = -1; }
				HoldBarMs = 0.f;
				P.Anim = 0;
				P.bSent6723 = false;
				// [ОРИГИНАЛ] вторая `SetVisible(49510, 0)` безусловна (Д-Б7) — ключ `asda.CampWar.HoldBarKeep 1` её снимает.
				if (!HoldBarKeep()) { ShowWin(kWinHold, false); }
				ApplyObjectColor(S, P.Color);
			}
		}
		else if (P.Anim == 3)
		{
			if (FindPointActor(P.Id))
			{
				P.Anim = 1;
				HoldBarMs = 0.f;   // `CProgressBar_SetRange(0, 10000.0)` + `SetValue(0)`
				if (MySlot == S) { ShowWin(kWinHold, true); }
			}
		}
		else if (P.Anim == 1)
		{
			AAsdaNpcActor* Obj = FindPointActor(P.Id);
			if (!Obj) { continue; }
			P.CaptureMs += (uint32)DtMs;
			if (P.CaptureMs >= (uint32)kCaptureMs)
			{
				P.Anim = 0;
				if (MySlot == S) { ShowWin(kWinHold, false); MySlot = -1; }
				// [ОРИГИНАЛ] `break` из цикла — остальные точки этого кадра пропускаются (Д-Б2).
				if (CVarCwTickNoBreak.GetValueOnGameThread() == 0) { return; }
				continue;
			}
			if (MySlot == S)
			{
				HoldBarMs = (float)P.CaptureMs;
				PlaceHoldBar(S);
			}
			// Тон тускнеет к цвету армии захватчика: каждый канал −= 0.0001·dt, пока «половинный» цвет ≤ текущего.
			const float Fade = 0.0001f * DtMs;
			const FVector H = HalfColor(P.Capturer);
			if (H.X <= P.Color.X) { P.Color.X -= Fade; }
			if (H.Y <= P.Color.Y) { P.Color.Y -= Fade; }
			if (H.Z <= P.Color.Z) { P.Color.Z -= Fade; }
			ApplyObjectColor(S, P.Color);
		}
	}
	// В конце — `netHandleFactionUpdateInRange` (очередь 6722): у порта 6722 применяет мир (`OnPeerFaction`), очереди здесь нет.
}

void UAsdaCampWarBattleSubsystem::DebugAdvanceTick(float Ms)
{
	TickPoints(Ms);
	TickFades(Ms);
}

// ═════════════════════════════ ТАЙМЕР БОЯ (`ClanTimer_Tick(1)`) ═════════════════════════════════

void UAsdaCampWarBattleSubsystem::TickBattleTimer()
{
	// `ClanTimer_Tick@0xCA18D0`, режим 1 (флаг +0x2334 проверил вызывающий): сек > 0 → −1; иначе сек = 59 и (мин > 0 → −1;
	//   иначе 0:0 и флаг 0). Текст `"%02d:%02d"` в 49942.
	UAsdaCampWarSubsystem* CW = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCampWarSubsystem>() : nullptr;
	if (TimerSec <= 0)
	{
		TimerSec = 59;
		if (TimerMin <= 0)
		{
			TimerMin = 0; TimerSec = 0;
			if (CW) { CW->SetBattleTimerOn(false); }
		}
		else { --TimerMin; }
	}
	else { --TimerSec; }
	SetTextNode(kWinTime, 49942, FString::Printf(TEXT("%02d:%02d"), TimerMin, TimerSec));
}

// ═════════════════════════════ СЕТЬ: ПРИЁМ ═══════════════════════════════════════════════════════

void UAsdaCampWarBattleSubsystem::HandlePacket(int32 Op, const TArray<uint8>& B)
{
	switch (Op)
	{
	case 6715:
		if (ApplyMemberList6715(B)) { if (UAsdaCampWarSubsystem* CW = GetGameInstance()->GetSubsystem<UAsdaCampWarSubsystem>()) { CW->OnSelfInSquadList(); } }
		break;
	case 6716: On6716(B); break;
	case 6737:
	{
		// 6737 (0 б, inline-ветка диспетчера G:14828): `CCampWar_ResetState` → `ToggleVisible(0, 0)` → спрятать 49530 → спрятать 49540.
		UAsdaCampWarSubsystem* CW = GetGameInstance()->GetSubsystem<UAsdaCampWarSubsystem>();
		if (CW) { CW->ResetState(); }
		ToggleVisible(false, false);
		if (CW) { CW->HideWaitWindows(); }
		UE_LOG(LogAsdaCampWarBattle, Log, TEXT("[битва армий/бой] ←6737: сброс, HUD боя снят, 49530/49540 скрыты"));
		break;
	}
	case 6720: On6720(B); break;
	case 6721: On6721(B); break;
	case 6724: On6724(B); break;
	case 6725: On6725(B); break;
	case 6726: On6726(B); break;
	case 6728: On6728(B); break;
	case 6729: On6729(B); break;
	case 6730: On6730(B); break;
	case 6732: On6732(B); break;
	case 6733: On6733(B); break;
	case 6735: On6735(B); break;
	case 6736: On6736(B); break;
	case 6739: On6739(B); break;
	case 6741: On6741(B); break;
	case 6742: On6742(B); break;
	default: break;
	}
}

bool UAsdaCampWarBattleSubsystem::ApplyMemberList6715(const TArray<uint8>& B)
{
	// `Recv_ClanMemberListBulk@0xCA4B50`: записи по 119 б — [0] слот (≤ 49), [1..118] → запись[0..117]; своя запись (charId +3 ==
	//   мой) → +0xB7E = слот (+0xB80 = −1 делает регистрация).
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	bool bSelf = false;
	int32 N = 0;
	for (int32 Off = 0; Off + 119 <= B.Num(); Off += 119)
	{
		const int32 Slot = (int8)B[Off];
		if (Slot < 0 || Slot > 0x31) { continue; }
		FMemory::Memcpy(Members[Slot].Raw, B.GetData() + Off + 1, 118);
		++N;
		if (Net && Members[Slot].CharId() == Net->SelectedCharId) { MyIndex = Slot; bSelf = true; }
	}
	UE_LOG(LogAsdaCampWarBattle, Log, TEXT("[битва армий/бой] ←6715: записей %d, своя %s (индекс %d)"), N, bSelf ? TEXT("есть") : TEXT("нет"), MyIndex);
	return bSelf;
}

void UAsdaCampWarBattleSubsystem::On6716(const TArray<uint8>&)
{
	// `Recv_ClanDrawMemberList`: при +0x235A == 1 — нарисовать (окно открывается только при HUD +0x2388) и сбросить флаг.
	if (!bWaitList) { return; }
	if (bHudOn)
	{
		ShowWin(kWinAtk, true, /*bTop*/ true);
		DrawMemberList();
	}
	bWaitList = false;
}

void UAsdaCampWarBattleSubsystem::On6720(const TArray<uint8>& B)
{
	// 6720 (12 б): [0..1] армия = моя → `clanMemberSetCellByPlayerId(charId [2..5], [8..9], [10..11])` → поля записи +102/+104.
	if (B.Num() < 12) { return; }
	if (AsdaBytes::RdI16(&B[0]) != GetSelfArmy()) { return; }
	const int32 CharId = AsdaBytes::RdI32(&B[2]);
	for (FAsdaCwMember& M : Members)
	{
		if (M.CharId() != CharId) { continue; }
		AsdaBytes::WrI16(M.Raw + 102, AsdaBytes::RdI16(&B[8]));
		AsdaBytes::WrI16(M.Raw + 104, AsdaBytes::RdI16(&B[10]));
	}
}

void UAsdaCampWarBattleSubsystem::On6721(const TArray<uint8>& B)
{
	// `Recv_ClanMemberUpdate@0xCA4F80` (26 б): [0] 0 вышел / 1 вошёл, [1..4] charId, [5] слот (беззнаково ≤ 0x31), [6..25] имя.
	if (B.Num() < 26) { return; }
	const UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	const int32 CharId = AsdaBytes::RdI32(&B[1]);
	const uint32 Slot = (uint32)(int8)B[5];
	const bool bSelf = Net && CharId == Net->SelectedCharId;
	const bool bNames = CVarCwNoticeNames.GetValueOnGameThread() != 0;
	auto NameNotice = [&](int32 LocId, const FString& Name)
	{
		// `CObject_SetSlotString(ctx, 0, имя)` → слот 0 = [ss1]; Д-Б8: строки ждут [ss2]/[ss3].
		const FString Text = bNames ? LocSsN(LocId, 3, Name) : LocSs1(LocId, Name);
		ChatLine(LocId, Text, 0xFFFF0080u, 10000.f, false);
	};
	if (Slot <= 0x31)
	{
		FAsdaCwMember& M = Members[Slot];
		if (B[0] == 1)
		{
			M.Raw[0] = 1;
			AsdaBytes::WrI32(M.Raw + 3, CharId);
			FMemory::Memcpy(M.Raw + 57, B.GetData() + 6, 20);
			if (!bSelf) { NameNotice(10070, M.Name()); }
		}
		else if (B[0] == 0)
		{
			if (!bSelf) { NameNotice(10071, M.Name()); }
			FMemory::Memzero(M.Raw, sizeof(M.Raw));   // `itemRecordReset`
		}
	}
	// При видимом 49399 — снова 6717.
	if (const FAsdaCwWindow* W = Win(kWinAtk); W && W->bShown)
	{
		bWaitList = true;
		SendMemberList6717();
	}
}

void UAsdaCampWarBattleSubsystem::On6724(const TArray<uint8>& B)
{
	// `CCampWar_OnObjectEffectPacket@0xCA5910` (7 б): [0] i16 точка, [2] sub, [3..6] charId захватчика.
	if (B.Num() < 7) { return; }
	const int32 S = FindSlotIndexById(AsdaBytes::RdI16(&B[0]));
	if (S < 0) { return; }
	FAsdaCwPointSlot& P = Slots[S];
	const int32 Sub = B[2];
	const int32 CharId = AsdaBytes::RdI32(&B[3]);
	UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
	UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>();
	AActor* Ch = WS ? WS->ActorByCharId(CharId) : nullptr;
	const bool bSelf = Ch && AsdaSelf::IsSelfCharId(Net, CharId);
	auto ColorByOwner = [&]()
	{
		// state 1 → половинный цвет владельца, 2 → полный, иначе белый.
		P.Color = (P.State == 1) ? HalfColor(P.Owner) : (P.State == 2) ? FullColor(P.Owner) : FVector::OneVector;
	};
	if (Sub == 1)
	{
		if (!Ch) { return; }
		// Свой: `CharFx_AutoSabzil_State(…, 47)`, +0x23D4 = s, 3D-звук `S_c_get_motion.wav` (петля); чужой: `CChar_SetState(…, 47)`.
		//   ★ 30.10.2026 [ОРИГИНАЛ]: оба пути ставят состояние 47 — слот 25, трек 25 `ch4_{m|f}_00_occupa_00_0.jbb` в цикле (anim_98,
		//   `AAsdaPlayerCharacter::EnterCampPose`; BATTLE_GAPS_RE.md §2). Прежде позы не было: клипа в ассетах не было.
		if (AAsdaPlayerCharacter* PCh = Cast<AAsdaPlayerCharacter>(Ch))
		{
			PCh->EnterCampPose(47, bSelf ? TEXT("6724 sub 1, свой (CharFx_AutoSabzil_State)") : TEXT("6724 sub 1, чужой (CChar_SetState)"));
		}
		if (bSelf)
		{
			MySlot = S;
			PlaySound3D(TEXT("S_c_get_motion"), Ch->GetActorLocation(), /*bLoop*/ true, &CaptureLoop);
		}
		// `Effect_SpawnByName("Camp_hand_01.jeb", …, char->posVec, 0, 6, 0, 1.0)` → хэндл в `char+0x33080`.
		if (AActor* Old = HandFx.FindRef(CharId).Get()) { Old->Destroy(); }
		if (AActor* Fx = SpawnFx(TEXT("Camp_hand_01"), Ch->GetActorLocation(), Ch)) { HandFx.Add(CharId, Fx); }
		// slot+0x10 = армия захватчика (`char+0x33078`), цвет (1,1,1), шаг 3, мс 0.
		P.Capturer = bSelf ? GetSelfArmy() : (WS ? WS->GetPeerCampId(WS->FindUserIdxByActor(Ch)) : -1);
		P.Color = FVector::OneVector;
		P.Anim = 3;
		P.CaptureMs = 0;
		UE_LOG(LogAsdaCampWarBattle, Log, TEXT("[битва армий/бой] ←6724 точка %d: захват начат (charId %d, %s, армия %d)"), P.Id, CharId,
			bSelf ? TEXT("я") : TEXT("чужой"), P.Capturer);
	}
	else if (Sub == 0)
	{
		if (Ch)
		{
			if (bSelf)
			{
				MySlot = -1;
				ShowWin(kWinHold, false);
				// +0x23DD «стоп-звук»: ставится 1 и тут же гасится — `sub_9349C7(g_pSoundMgr, "S_c_get_motion.wav")`.
				if (CaptureLoop) { CaptureLoop->Stop(); CaptureLoop = nullptr; }
			}
			ColorByOwner();
			if (AActor* Fx = HandFx.FindRef(CharId).Get()) { Fx->Destroy(); }
			HandFx.Remove(CharId);
			// ★ 30.10.2026: `if (char+0x2E6FC == 47)` → свой `CharFx_AutoSabzil_State(0)`, чужой `CChar_SetState(0)`, `bActionBusy = 0`
			//   (combat_pvp.c:5691-5706). Уже перебитое (ход, смерть, атака) состояние не трогается.
			if (AAsdaPlayerCharacter* PCh = Cast<AAsdaPlayerCharacter>(Ch)) { PCh->LeaveCampPose(47, TEXT("6724 sub 0")); }
		}
		P.Anim = 2;
		UE_LOG(LogAsdaCampWarBattle, Log, TEXT("[битва армий/бой] ←6724 точка %d: захват окончен/прерван (charId %d)"), P.Id, CharId);
	}
	else if (Sub == 3)
	{
		ColorByOwner();
		P.Anim = 2;
		MySlot = -1;
		MsgBox(10182, Loc(10182), 10182);
	}
}

void UAsdaCampWarBattleSubsystem::On6725(const TArray<uint8>& B)
{
	// `QuickSlot_OnUpdatePacket@0xCA51B0` [МИСНЕЙМ] (11 б): [0] точка, [2..5] клетка, [6] армия-владелец, [8] state, [9] бонус.
	if (B.Num() < 11) { return; }
	const int32 Id = AsdaBytes::RdI16(&B[0]);
	const int32 Owner = AsdaBytes::RdI16(&B[6]);
	const int32 State = (int8)B[8];
	const int32 Bonus = AsdaBytes::RdI16(&B[9]);
	const int32 S = FindSlotIndexById(Id);
	if (S < 0)
	{
		// [ПОРТ] Д-Б1: эталон без проверки пишет по индексу −1 (поля перед массивом слотов). Повторять порчу памяти нечем — пропуск.
		UE_LOG(LogAsdaCampWarBattle, Warning, TEXT("[битва армий/бой] ←6725: точки %d нет среди %d слотов — эталон писал бы по индексу −1, пропуск"), Id, SlotCount);
		return;
	}
	FAsdaCwPointSlot& P = Slots[S];
	if (P.Owner != Owner) { P.LightKind = 0; P.Owner = Owner; }
	// +0x24 и +0x28 пишутся по [0], а не по s (совпадают: номер точки = индекс слота).
	if (Id >= 0 && Id < kSlots)
	{
		Slots[Id].Raw24 = Bonus;
		if (State == 1 && P.Owner == Owner) { Slots[Id].TimerMs = 0; }
		if (State == 2) { Slots[Id].TimerMs = (int32)((double)Bonus * 6000.0); }
	}
	P.State = State;
	P.bSent6723 = false;
	P.Anim = 0;
	if (MySlot == S) { MySlot = -1; ShowWin(kWinHold, false); }
	if (State > 0)
	{
		if (AAsdaNpcActor* Obj = FindPointActor(P.Id))
		{
			PlaySound3D(State == 1 ? TEXT("S_e_get1") : TEXT("S_e_get2"), Obj->GetActorLocation(), false, nullptr);
		}
	}
	UE_LOG(LogAsdaCampWarBattle, Log, TEXT("[битва армий/бой] ←6725 точка %d: владелец %d, state %d, бонус %d"), Id, Owner, State, Bonus);
}

void UAsdaCampWarBattleSubsystem::On6726(const TArray<uint8>& B)
{
	// `campWarSetObjectiveSlot@0xCA6AE0` (11 б, раскладка 6725): `[0] < 10` иначе выход.
	if (B.Num() < 11) { return; }
	const int32 Id = AsdaBytes::RdI16(&B[0]);
	if (Id >= 10) { return; }
	if (Id < 0)
	{
		// [ПОРТ] эталон проверяет только `< 10` — отрицательный номер писал бы перед массивом. Пропуск.
		UE_LOG(LogAsdaCampWarBattle, Warning, TEXT("[битва армий/бой] ←6726: номер точки %d < 0 — пропуск"), Id);
		return;
	}
	FAsdaCwPointSlot& P = Slots[Id];
	P.Id = Id;
	P.State = (int8)B[8];
	P.Owner = AsdaBytes::RdI16(&B[6]);
	P.Raw24 = AsdaBytes::RdI16(&B[9]);
	if (P.State == 1) { P.TimerMs = 0; }   // сравнение с только что записанным владельцем — всегда истина
	if (P.State == 2) { P.TimerMs = (int32)((double)P.Raw24 * 6000.0); }
	P.CellX = AsdaBytes::RdI16(&B[2]);
	P.CellY = AsdaBytes::RdI16(&B[4]);
	// [ОРИГИНАЛ] `++*(+0x23D0)` (Д-Б5) — ключ `asda.CampWar.SlotCountFix 1`: max(номер + 1).
	if (CVarCwSlotCountFix.GetValueOnGameThread() != 0) { SlotCount = FMath::Max(SlotCount, Id + 1); }
	else { ++SlotCount; }
	UE_LOG(LogAsdaCampWarBattle, Log, TEXT("[битва армий/бой] ←6726 точка %d: владелец %d, state %d, бонус %d, клетка (%d,%d), точек %d"),
		Id, P.Owner, P.State, P.Raw24, P.CellX, P.CellY, SlotCount);
}

void UAsdaCampWarBattleSubsystem::On6728(const TArray<uint8>& B)
{
	// `campWarHandleStateNotice@0xCA5490` (3 б): [0] код 0…10, [1..2] N.
	if (B.Num() < 3) { return; }
	const int32 Code = (int8)B[0];
	const int32 N = AsdaBytes::RdI16(&B[1]);
	if (Code < 0 || Code > 10) { return; }
	if (Code < 7)
	{
		// Слот [sd1] = N + 1 (номер точки), чат 10171+код, 0xFFFF0080, 10000 мс.
		ChatLine(10171 + Code, LocSd1(10171 + Code, N + 1), 0xFFFF0080u, 10000.f, false);
	}
	else
	{
		// Коды 7…10 — только крупные надписи 49910 (10178…10181 не выводятся).
		ShowModeControls(Code, N);
		if (Code == 10)
		{
			// Погасить прежний `Camp_start.jeb`, новый — на точке возрождения своей армии (`localChar+0x33078`), высота по рельефу.
			if (AActor* Old = StartFx.Get()) { Old->Destroy(); }
			StartFx.Reset();
			const int32 Army = GetSelfArmy();
			UWorld* W = GetGameInstance()->GetWorld();
			if (W && Army >= 0 && Army < kArmies)
			{
				const FVector2D Ue = AsdaCoord::GameToUe2D(kSpawn[Army]);
				const FVector At(Ue.X, Ue.Y, AsdaGround::GroundZ(W, Ue.X, Ue.Y));
				StartFx = SpawnFx(TEXT("Camp_start"), At, nullptr);
			}
		}
	}
	if (Code == 8)
	{
		if (AActor* Old = StartFx.Get()) { Old->Destroy(); }
		StartFx.Reset();
	}
	UE_LOG(LogAsdaCampWarBattle, Log, TEXT("[битва армий/бой] ←6728 код %d, N %d"), Code, N);
}

void UAsdaCampWarBattleSubsystem::On6729(const TArray<uint8>& B)
{
	// `uiPanelShowWith3@0xCA56F0` [МИСНЕЙМ] (12 б): 3 × i32 очки → +0xB9C+4f, затем `uiWidgetShowPanel` (даже при выключенном HUD).
	if (B.Num() < 12) { return; }
	for (int32 F = 0; F < kArmies; ++F) { Score[F] = AsdaBytes::RdI32(&B[4 * F]); }
	ShowPointPanel();
	UE_LOG(LogAsdaCampWarBattle, Log, TEXT("[битва армий/бой] ←6729 очки %d / %d / %d"), Score[0], Score[1], Score[2]);
}

void UAsdaCampWarBattleSubsystem::On6730(const TArray<uint8>& B)
{
	// `floatingText_SpawnValueOverChar@0xCA7100` (10 б): [0..1] userNum убийцы, [2..5] charId, [6..9] очки. Только если убийца — я.
	if (B.Num() < 10) { return; }
	UGameInstance* GI = GetGameInstance();
	const int32 UserNum = AsdaBytes::RdI16(&B[0]);
	const int32 Pts = AsdaBytes::RdI32(&B[6]);
	UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>();
	UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
	AActor* Ch = WS ? WS->ActorByUserIdx(UserNum) : nullptr;
	if (!Ch || !AsdaSelf::IsSelfUserIdx(Net, UserNum)) { return; }
	// Экранная точка `char.soundPos`: «N» на (x + 105, y − 2) и «POINT» на (x, y), стиль 0x15 (системный жёлтый).
	if (UAsdaFloatingTextSubsystem* FT = GI->GetSubsystem<UAsdaFloatingTextSubsystem>())
	{
		FT->SpawnOverActor(0x15, FString::FromInt(Pts), Ch, FVector2D(105.f, -2.f));
		FT->SpawnOverActor(0x15, TEXT("POINT"), Ch, FVector2D::ZeroVector);
	}
	// Чат: `sprintf(лок. 108118, очки)`, 0xFFFFFF00, TTL 0, дорожка 1.
	ChatLine(108118, AsdaUi::FormatPrintfArgs(Loc(108118), { FString::FromInt(Pts) }), 0xFFFFFF00u, 0.f, true);
}

void UAsdaCampWarBattleSubsystem::On6732(const TArray<uint8>& B)
{
	// `recvCampWarSynthResultRefresh` → `CItemSynthResultWin_Refresh@0xC9CF00` [МИСНЕЙМ: окно итога 49800] (65 б).
	if (B.Num() < 65) { return; }
	ShowEndWindow(B);
}

void UAsdaCampWarBattleSubsystem::On6733(const TArray<uint8>& B)
{
	// `onRecvHonorAndInventoryUpdate@0xCA7040` (235 б): 49882 видим всегда; [0] == 1 → золото [3..6]
	//   (`uiSetHonorPointsDisplay` [МИСНЕЙМ] = показ денег), вес [1..2], 4 записи по 57 б с [7] (`OnRecv_ItemUpdate`).
	if (B.Num() < 235) { return; }
	SetVisNode(kWinEnd, 49882, true);
	if (B[0] != 1) { return; }
	UGameInstance* GI = GetGameInstance();
	if (UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>())
	{
		Inv->SetGoldDisplay(AsdaBytes::RdI32(&B[3]));
		for (int32 I = 0; I < 4; ++I)
		{
			TArray<uint8> Rec; Rec.Append(B.GetData() + 7 + 57 * I, 57);
			Inv->ApplyExternalRecord(Rec, UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
		}
	}
	if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
	{
		Net->SetSelfWeight(AsdaWeightFromWire(AsdaBytes::RdI16(&B[1])), -1, TEXT("6733 награда битвы армий"));
	}
	UE_LOG(LogAsdaCampWarBattle, Log, TEXT("[битва армий/бой] ←6733: золото %d, вес %d, 4 записи предметов"), AsdaBytes::RdI32(&B[3]),
		AsdaWeightFromWire(AsdaBytes::RdI16(&B[1])));
}

void UAsdaCampWarBattleSubsystem::On6735(const TArray<uint8>& B)
{
	// `clanMember_HandleResultCode@0xCA6E60` (7 б, ответ на 6734): 0 → 10073; 1 → [1..4] новый капитан, [5..6] слот; 2 → 10068; 3 → 10069.
	if (B.Num() < 7) { return; }
	switch (B[0])
	{
	case 0: MsgBox(10073, Loc(10073), 10073); break;
	case 1:
	{
		const int32 CharId = AsdaBytes::RdI32(&B[1]);
		const int32 Slot = AsdaBytes::RdI16(&B[5]);
		// [ПОРТ] эталон индексирует слот без проверки; у порта — только 0…49.
		const FString Name = (Slot >= 0 && Slot < kMembers) ? Members[Slot].Name() : FString();
		if (Slot >= 0 && Slot < kMembers && Members[Slot].CharId() == CharId) { Members[Slot].Raw[8] = 0; }
		const FString Text = (CVarCwNoticeNames.GetValueOnGameThread() != 0) ? LocSsN(10072, 3, Name) : LocSs1(10072, Name);
		MsgBox(10072, Text, 10072);
		for (FAsdaCwMember& M : Members) { if (M.CharId() != CharId) { M.Raw[8] = 3; } }
		break;
	}
	case 2: MsgBox(10068, Loc(10068), 10068); break;
	case 3: MsgBox(10069, Loc(10069), 10069); break;
	default: break;
	}
}

void UAsdaCampWarBattleSubsystem::On6736(const TArray<uint8>& B)
{
	// `campWarUpdateCountdown@0xCA72D0` (6 б, локальное время сервера ч/мин/сек): +0x2330 = 60 − сек;
	//   +0x232C = 60·конецЧ − 60·ч + конецМ − мин + ((+0x2330 ≤ 0) − 1), < 0 → 0; +0x2334 = 1; сразу `ClanTimer_Tick(1)`.
	if (B.Num() < 6) { return; }
	UAsdaCampWarSubsystem* CW = GetGameInstance()->GetSubsystem<UAsdaCampWarSubsystem>();
	if (!CW) { return; }
	const int32 H = AsdaBytes::RdU16(&B[0]), M = AsdaBytes::RdU16(&B[2]), Sec = AsdaBytes::RdU16(&B[4]);
	TimerSec = 60 - Sec;
	TimerMin = 60 * CW->GetEndH() - 60 * H + CW->GetEndM() - M + ((TimerSec <= 0 ? 1 : 0) - 1);
	if (TimerMin < 0) { TimerMin = 0; }
	CW->SetBattleTimerOn(true);
	CW->StepClanTimer(1);
	UE_LOG(LogAsdaCampWarBattle, Log, TEXT("[битва армий/бой] ←6736 сервер %02d:%02d:%02d, конец %02d:%02d → осталось %02d:%02d"), H, M, Sec,
		CW->GetEndH(), CW->GetEndM(), TimerMin, TimerSec);
}

void UAsdaCampWarBattleSubsystem::On6739(const TArray<uint8>& B)
{
	// `campWarRankingApply@0xCA7480` [МИСНЕЙМ] (9 б, ОПЕРАТОР/наблюдатель): конец +0x2340/+0x2344 = [5..6]/[7..8]; запись +0xB7C
	//   сброшена (`sub_C95E20`: армия −1, индекс −1, очки 0, звание −1), +0xB7E = [4]; `ToggleVisible(1, 0)`.
	if (B.Num() < 9) { return; }
	UGameInstance* GI = GetGameInstance();
	if (UAsdaCampWarSubsystem* CW = GI->GetSubsystem<UAsdaCampWarSubsystem>()) { CW->SetEndTime(AsdaBytes::RdU16(&B[5]), AsdaBytes::RdU16(&B[7])); }
	if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>()) { Net->OnSelfFaction.Broadcast(-1, 0, -1); }   // общая точка «своя армия»
	MyIndex = (int8)B[4];
	bOperator = true;
	ToggleVisible(true, false);
	UE_LOG(LogAsdaCampWarBattle, Log, TEXT("[битва армий/бой] ←6739 оператор: слот %d, конец %02d:%02d"), MyIndex, AsdaBytes::RdU16(&B[5]), AsdaBytes::RdU16(&B[7]));
}

void UAsdaCampWarBattleSubsystem::On6741(const TArray<uint8>& B)
{
	// `uiToggleByFlagByte@0xCA7690` (7 б, читается [0]): видимость контрола `g_pHUD+0x9B4` = [0].
	//   [ОРИГИНАЛ] ★ 30.10.2026 (BATTLE_GAPS_RE.md §1): в этой сборке поле `g_pHUD+0x9B4` НЕ ЗАПОЛНЯЕТСЯ НИКОГДА — единственная запись
	//   0 в сбросе HUD (`0xE84254`), инициализация его пропускает (дизасм), в обоих дампах памяти 0 при живых соседях. Приём у
	//   оригинала — no-op; порт 1:1: значение хранится моделью и пишется в журнал, показывать нечего. Смысл по серверу — флаг
	//   таймера «исключён из битвы» (`CUser+259156`: 1 — запрет входа действует, 0 — истёк). Гипотеза «контрол 272
	//   `MINISTATE_ClanMarkBase2`» вторым методом не доказана — не подключается (решение у координатора).
	if (B.Num() < 1) { return; }
	Hud6741 = B[0];
	UE_LOG(LogAsdaCampWarBattle, Log, TEXT("[битва армий/бой] ←6741: флаг «исключён из битвы» = %d (у оригинала поле HUD +0x9B4 пусто — показа нет, 1:1)"), Hud6741);
}

void UAsdaCampWarBattleSubsystem::On6742(const TArray<uint8>& B)
{
	// `campWar_UpdateScoreLabels@0xCA6D00` [МИСНЕЙМ] (6 б): 3 × i16 численность армий → +0xBA8+2f и «%d/%d».
	if (B.Num() < 6) { return; }
	for (int32 F = 0; F < kArmies; ++F) { Headcount[F] = AsdaBytes::RdI16(&B[2 * F]); }
	DrawHeadcount();
}

// ═════════════════════════════ СЕТЬ: ОТПРАВКА ════════════════════════════════════════════════════

bool UAsdaCampWarBattleSubsystem::SendBody(uint16 Op, const TArray<uint8>& Body, const TCHAR* What)
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	return Net && Net->SendCampWar(Op, Body.GetData(), Body.Num(), What);
}

// ★★★ 31.10.2026 ЧАТ АРМИИ 6718 (отзыв игроков «Can't use faction chat»; `Reports/campwar_bugs/RE_SPEC.md` §1).
//   Эталон `Clan_Send_SetNotice@0xCA3010` (G:309774): тело 0xE4 = 228 б — [0..3] charId (+0x22D0), [4..7] dword +0xB7C (армия i16 |
//   индекс в скваде +0xB7E i16 << 16), [8..27] своё имя 20, [28..227] текст 200; ведущий loc 5337 «[Армия] » срезается; MD5 по 228.
//   Сервер `User_Game_RegistCancel_2@0x466F50` (S:41990, «User_Game_SquadChat()») считает MD5 по 0x6C = 108 б и читает тело 108:
//   [0..3] UserIndex, [4..7] армия|индекс, [8..27] имя, [28..107] текст 80 → при MD5 по 228 `memcmp` не сходится и пакет выброшен;
//   валидатор P55 (OpcodeLengths 6718: кадр 142…161 = 9 + 8 + 16 + 108 + 1 + пад) тоже отбросил бы 252-байтное тело.
//   [ПОРТ] (закон «сервер старый»): шлём СЕРВЕРНУЮ раскладку — 108 б, MD5 по 108 (конверт с секретом `SendCampWar`), текст ≤ 79 б +
//   ноль. Признак рассылки у сервера — армия и индекс (не клан и не отряд); без них (< 0) сервер молча выбросит — не шлём (журнал).
//   Ответ — 6719 всем членам сквада армии, включая отправителя (эхо строки в ленту — оттуда).
bool UAsdaCampWarBattleSubsystem::SendArmyChat6718(const FString& Text)
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	FString T = Text;
	const FString Pfx = Loc(5337);   // «[Армия] » — приглашение режима 9
	if (!Pfx.IsEmpty())
	{
		if (T == Pfx || T == Pfx.TrimEnd()) { return false; }    // эталон: текст == приглашение → ничего
		if (T.StartsWith(Pfx)) { T.RightChopInline(Pfx.Len()); }  // эталон: префикс срезается
	}
	if (T.IsEmpty()) { return false; }
	const int32 Army = GetSelfArmy();
	if (Army < 0 || MyIndex < 0)
	{
		UE_LOG(LogAsdaCampWarBattle, Warning, TEXT("[битва армий/бой] чат армии: армия %d / индекс %d — сервер такое выбросит (S:41990), 6718 не ушёл"), Army, MyIndex);
		return false;
	}
	TArray<uint8> B; B.SetNumZeroed(108);
	AsdaBytes::WrI32(B.GetData(), Net ? Net->SelectedCharId : 0);
	AsdaBytes::WrI16(B.GetData() + 4, Army);
	AsdaBytes::WrI16(B.GetData() + 6, MyIndex);
	FAsdaLoginData::EncodeCp1251(WS ? WS->GetSelfCharName() : FString(), B.GetData() + 8, 19);   // имя 20 с нулём
	const int32 N = FAsdaLoginData::EncodeWireText(T, B.GetData() + 28, 79);                    // текст 80 с нулём
	if (N <= 0) { return false; }
	UE_LOG(LogAsdaCampWarBattle, Log, TEXT("[битва армий/бой] →6718 чат армии: армия %d, индекс %d, текст %d б «%s»"), Army, MyIndex, N, *T);
	return SendBody(6718, B, TEXT("чат армии"));
}

void UAsdaCampWarBattleSubsystem::SendMemberList6717()
{
	// `Clan_Send_ReqMemberList@0xCA33B0`: 6 б — [0..3] charId (+0x22D0), [4..5] армия (+0xB7C); затем очистить 50 записей.
	const UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	TArray<uint8> B; B.SetNumZeroed(6);
	AsdaBytes::WrI32(B.GetData(), Net ? Net->SelectedCharId : 0);
	AsdaBytes::WrI16(B.GetData() + 4, GetSelfArmy());
	SendBody(6717, B, TEXT("список сквада «Рейд»"));
	for (FAsdaCwMember& M : Members) { FMemory::Memzero(M.Raw, sizeof(M.Raw)); }
}

void UAsdaCampWarBattleSubsystem::SendLeave6731()
{
	// `Clan_Send_Leave@0xCA37B0` [МИСНЕЙМ]: 8 б — [0..3] charId, [4..7] dword +0xB7C (армия | индекс +0xB7E << 16);
	//   затем ResetState + ResetPositions.
	UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>();
	TArray<uint8> B; B.SetNumZeroed(8);
	AsdaBytes::WrI32(B.GetData(), Net ? Net->SelectedCharId : 0);
	AsdaBytes::WrI16(B.GetData() + 4, GetSelfArmy());
	AsdaBytes::WrI16(B.GetData() + 6, MyIndex);
	SendBody(6731, B, TEXT("«Выйти из боя»"));
	if (UAsdaCampWarSubsystem* CW = GI->GetSubsystem<UAsdaCampWarSubsystem>()) { CW->ResetState(); }
	ResetChildPositions();
}

void UAsdaCampWarBattleSubsystem::RequestCapture(AAsdaNpcActor* Target)
{
	// `netSendCampWarjoin@0xCA5750` [МИСНЕЙМ]: 8 б — [0..3] charId, [4..5] армия (+0xB7C), [6..7] младшее слово ключа сущности;
	//   ставит `slot+0x1C = 1` слоту `findSlotIndexById(ключ)`.
	if (!Target) { return; }
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	const int32 Key = Target->NetRenderId;
	const int32 S = FindSlotIndexById(Key);
	// [ПОРТ] у эталона результат −1 не проверяется (байт по `this + 0x2408`) — у порта пометка только существующему слоту.
	if (S >= 0) { Slots[S].bSent6723 = true; }
	Net->SendCampWarJoin6723(Net->SelectedCharId, GetSelfArmy(), Key);
}

void UAsdaCampWarBattleSubsystem::OnOutButton()
{
	// `MiniMap_Window_OnCommand@0xD64050` case 0x4470: состояние действия ≠ 6 (мёртв) и ≠ 29 → TWOBUT 10047 → «Да» (`OnSendItemUpgrade`
	//   [МИСНЕЙМ: общий «Да»] case 0x273F) → 6731.
	const UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const int32 St = UAsdaKingCrownSubsystem::EtalonActionStateOf(WS ? WS->LocalPlayerActor.Get() : nullptr);
	if (St == 6 || St == 29) { return; }
	TWeakObjectPtr<UAsdaCampWarBattleSubsystem> Self(this);
	Confirm(10047, Loc(10047), 10047, [Self]() { if (Self.IsValid()) { Self->SendLeave6731(); } });
}

// ═════════════════════════════ ТЕКСТЫ / НОТИСЫ / ЗВУК / ЭФФЕКТЫ ═════════════════════════════════

void UAsdaCampWarBattleSubsystem::ChatLine(int32 LocId, const FString& Text, uint32 ArgbColor, float LifeMs, bool bTrackB)
{
	// `CChat_AddTokenizedLines(g_pChatWnd, текст, цвет, ttl, дорожка)` — единая точка системного чата порта.
	NoticeTrace.Add(LocId);
	LastNoticeText = Text;
	if (UAsdaChatSubsystem* Chat = GetGameInstance()->GetSubsystem<UAsdaChatSubsystem>())
	{
		Chat->AddSystemMessage(Text, Argb(ArgbColor), bTrackB ? EAsdaSysTrack::B : EAsdaSysTrack::A, LifeMs);
	}
}

void UAsdaCampWarBattleSubsystem::PlankLine(int32 LocId, const FString& Text, uint32 ArgbColor, float LifeMs)
{
	// `CControl_SetTooltipText(g_pChatWnd, текст, цвет, ttl)` [МИСНЕЙМ: плашка 221 над чатом].
	NoticeTrace.Add(LocId);
	LastNoticeText = Text;
	AsdaNotice::Plank(GetGameInstance(), EAsdaMsgCategory::War, Text, Argb(ArgbColor), LifeMs, LocId);
}

void UAsdaCampWarBattleSubsystem::MsgBox(int32 LocId, const FString& Text, int32 EtalonId)
{
	// ONEBUT `CItem_BuildTooltipText(g_pItemTooltipUI, 0, id, текст, 1)` — пара (0, id) глушит повтор, как у эталона.
	NoticeTrace.Add(LocId);
	LastNoticeText = Text;
	AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::War, Text, AsdaLoc::kBtnOk, EtalonId);
}

void UAsdaCampWarBattleSubsystem::Confirm(int32 LocId, const FString& Text, int32 EtalonId, TFunction<void()> OnYes)
{
	// TWOBUT `CItem_BuildTooltipText(g_pItemTooltipUI, 1, id, текст, 1)`.
	NoticeTrace.Add(LocId);
	LastNoticeText = Text;
	if (UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>())
	{
		WM->ShowGameConfirm(Text, [Fn = MoveTemp(OnYes)](bool bYes) { if (bYes && Fn) { Fn(); } },
			/*Gauge*/ 0.f, AsdaLoc::kBtnOk, 2289, EtalonId, /*EtalonKind*/ 1);
	}
}

FString UAsdaCampWarBattleSubsystem::Loc(int32 LocId) const
{
	return AsdaNotice::Loc(GetGameInstance(), LocId);
}

FString UAsdaCampWarBattleSubsystem::LocSd1(int32 LocId, int32 Sd1) const
{
	FString S;
	if (UAsdaTextSubsystem* TS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr) { S = TS->FormatById(LocId, {}, { Sd1 }); }
	return S.IsEmpty() ? Loc(LocId).Replace(TEXT("[sd1]"), *FString::FromInt(Sd1)) : S;
}

FString UAsdaCampWarBattleSubsystem::LocSs1(int32 LocId, const FString& Ss1) const
{
	FString S;
	if (UAsdaTextSubsystem* TS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr) { S = TS->FormatById(LocId, { Ss1 }, {}); }
	return S.IsEmpty() ? Loc(LocId).Replace(TEXT("[ss1]"), *Ss1) : S;
}

FString UAsdaCampWarBattleSubsystem::LocSsN(int32 LocId, int32 N, const FString& Str) const
{
	// Одна строка во всех слотах [ss1]…[ssN] (Д-Б8).
	TArray<FString> Args;
	for (int32 I = 0; I < N; ++I) { Args.Add(Str); }
	FString S;
	if (UAsdaTextSubsystem* TS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr) { S = TS->FormatById(LocId, Args, {}); }
	if (S.IsEmpty())
	{
		S = Loc(LocId);
		for (int32 I = 1; I <= N; ++I) { S = S.Replace(*FString::Printf(TEXT("[ss%d]"), I), *Str); }
	}
	return S;
}

void UAsdaCampWarBattleSubsystem::PlaySound3D(const TCHAR* Name, const FVector& At, bool bLoop, TObjectPtr<UAudioComponent>* OutLoop)
{
	// `Audio_Play3DSound(g_pSoundMgr, x, y, z, "<имя>.wav", петля)`.
	UAsdaSoundSubsystem* Snd = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr;
	if (!Snd) { return; }
	if (OutLoop && *OutLoop) { (*OutLoop)->Stop(); *OutLoop = nullptr; }
	UAudioComponent* AC = Snd->PlaySound3DAs(FName(Name), At, EAsdaSoundClass::Auto, nullptr, bLoop);
	if (OutLoop) { *OutLoop = AC; }
}

AActor* UAsdaCampWarBattleSubsystem::SpawnFx(const TCHAR* Jeb, const FVector& At, AActor* Carrier)
{
	// `Effect_SpawnByName` / `CEffectMgr_CreateEffect_F561F0`. Эффект-BP `BP_Fx_<имя>` (имя — как на диске, нижний регистр) не собран →
	//   nullptr (эталон: хэндла нет). Проверка «собран ли» кэшируется: свет точки пробуется каждый кадр, пока эффекта нет (как у эталона).
	//   Все восемь `camp_*` ЗАЦИКЛЕНЫ и гасятся кодом (`CEffectMgr_StopByOwner` → `Destroy`) — срока жизни у актора нет.
	static TMap<FString, bool> GBuilt;
	const FString Key = FString(Jeb).ToLower();
	bool* Known = GBuilt.Find(Key);
	if (!Known)
	{
		const bool bBuilt = AAsdaEffectActor::ResolveJebClass(Key) != nullptr;
		Known = &GBuilt.Add(Key, bBuilt);
		if (!bBuilt) { UE_LOG(LogAsdaCampWarBattle, Warning, TEXT("[битва армий/бой] эффект %s.jeb не перенесён (нет BP_Fx_%s) — пропуск"), Jeb, Jeb); }
	}
	if (!*Known) { return nullptr; }
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W) { return nullptr; }
	AAsdaEffectActor* Fx = AAsdaEffectActor::SpawnByJebName(W, Key, At, FRotator::ZeroRotator, Carrier);
	if (Fx && Carrier)
	{
		// Привязка 6 эталона = мировая матрица носителя (`EAsdaFxAttachMode::CharWorld`).
		Fx->ConfigureBoneAttach(Carrier, Carrier->FindComponentByClass<USkeletalMeshComponent>(), NAME_None, EAsdaFxAttachMode::CharWorld);
	}
	return Fx;
}

// ═════════════════════════════ КОНСОЛЬ ═══════════════════════════════════════════════════════════
#if ASDA_DEV_CONSOLE   // античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaCampWarBattleDump(
	TEXT("asda.CampWar.BattleDump"),
	TEXT("asda.CampWar.BattleDump — модель боевой фазы «Битвы армий»: HUD, очки, численность, таймер, точки, сквад, окна"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaCampWarBattleSubsystem* B = GI ? GI->GetSubsystem<UAsdaCampWarBattleSubsystem>() : nullptr;
		if (!B) { return; }
		UE_LOG(LogAsdaCampWarBattle, Log, TEXT("[битва армий/бой] HUD %s, очки %d/%d/%d, численность %d/%d/%d, таймер %02d:%02d, точек %d, мой слот %d, индекс %d"),
			B->IsHudOn() ? TEXT("вкл") : TEXT("выкл"), B->GetScore(0), B->GetScore(1), B->GetScore(2), B->GetHeadcount(0), B->GetHeadcount(1),
			B->GetHeadcount(2), B->GetTimerMin(), B->GetTimerSec(), B->GetSlotCount(), B->GetMySlot(), B->GetMyIndex());
		for (int32 S = 0; S < B->GetSlotCount() && S < UAsdaCampWarBattleSubsystem::kSlots; ++S)
		{
			const FAsdaCwPointSlot& P = B->GetSlot(S);
			UE_LOG(LogAsdaCampWarBattle, Log, TEXT("  точка %d: state %d, владелец %d, шаг %d, захватчик %d, таймер %d мс"), P.Id, P.State, P.Owner, P.Anim,
				P.Capturer, P.TimerMs);
		}
		UE_LOG(LogAsdaCampWarBattle, Log, TEXT("%s"), *B->DumpControls());
	}));
#endif // ASDA_DEV_CONSOLE
