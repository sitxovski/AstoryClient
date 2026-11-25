// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPlayerCharacter_Crouch.cpp — ★ R3-c 12.11.2026 (разрез AsdaPlayerCharacter_Move.cpp, дословный перенос): присед: ключи, удержание, эстафета позы стоя, статус хода 4006, клипы.
//   Общее для частей — в AsdaPlayerCharacter_Move_Private.h; шапка и реверс — в AsdaPlayerCharacter_Move.cpp.
#include "World/AsdaPlayerCharacter.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "World/AsdaPlayerController.h"   // ★ ритм ударов задаёт тик анимации (OnSwingCycleCompleted)
#include "Misc/App.h"   // FApp::GetDeltaTime — оценка кадра (точка отправки за кадр до дедлайна)
#include "Materials/MaterialInterface.h"
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д8/Д9: ресурс только через менеджер
#include "World/AsdaNameplateComponent.h"
#include "World/AsdaWorldSubsystem.h"   // LocalPlayerActor — «свой» персонаж (звук 2D против 3D)
#include "Engine/GameInstance.h"         // ★ В5fix: GetGameInstance()->GetSubsystem (сборка без unity)
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"   // поза трупа: перемотка клипа death01 в последний кадр
#include "Engine/StaticMesh.h"                 // ★ 10.10.2026 (копание): меш инструмента на `8_a1`
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"                  // ★ В5: таймер конца клипа действия рыбалки
#include "World/AsdaCombatTrace.h"         // ★ 18.10.2026: журнал боя — замах, кадры удара, стойка, смерть персонажа
#include "Npc/AsdaMobActor.h"              // ★ 18.10.2026: журнал боя — имя и rid цели разворота в замахе
#include "AsdaPlayerCharacterInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaPlayerCharacter*.cpp (LogAsdaPlayer, чистые хелперы)
#include "AsdaNetSubsystem.h"              // ★ 23.10.2026 (прыжок донора): 4024 код 131
#include "World/AsdaCrouchStand.h"         // ★ 05.11.2026: присед стоя — поза и 4024 132/133
#include "UI/AsdaPremiumGachaSubsystem.h"  // ★ … сцена премиум-гачи
#include "Framework/Application/SlateApplication.h"   // ★ … фокус в любом поле ввода
#include "Framework/Application/IInputProcessor.h"   // ★ 08.11.2026 (пачка 15, баг 3): нажатия, съеденные окнами UMG, при зажатом Ctrl
#include "World/AsdaVehicleRideSubsystem.h"           // ★ 24.10.2026 (присед): за рулём машины приседа нет
#include "Resources/AsdaGameUserSettings.h"           // ★ 25.10.2026 (присед): режим Ctrl «Нажатие/Зажатие» из окна настроек
#include "AsdaPlayerCharacter_Move_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaPlayerCharacter_Move*.cpp

// ★ 25.10.2026: «Зажатие» — с какого момента Ctrl отпущен (снятие приседа после `asda.Crouch.HoldReleaseMs`); −1 — Ctrl зажат/не ждём.
struct FAsdaCrouchHoldRelease
{
	TWeakObjectPtr<const AAsdaPlayerCharacter> Who;   // чей отсчёт
	double UpSinceS = -1.0;                           // FPlatformTime::Seconds() отпускания
};
static FAsdaCrouchHoldRelease GCrouchHoldRelease;

// ══════════ ★★★ ХОДЬБА ВПРИСЯДКУ (донор AsdaFableClassic + решение владельца 24.10.2026) ══════════════════════════════
//   Разбор — у объявлений в заголовке. Сервер (P80): статус хода 4006 @+3 = 4 делит скорость на 2 стоковой веткой скрытности
//   (`RecalcMoveSpeed@0x5588F0`) и эхом рассылает статус соседям в 4007 — новых опкодов нет. Без P80 присед виден, ход — обычный.
static TAutoConsoleVariable<int32> CVarAsdaCrouchEnable(
	TEXT("asda.Crouch.Enable"), 1,
	TEXT("[ПОРТ] Ходьба вприсядку (донор AsdaFableClassic): Ctrl — вкл/выкл, клип anim_239, присед соседей по статусу 4007. 0 — нет."),
	ECVF_Default);
static TAutoConsoleVariable<int32> CVarAsdaCrouchHideAnim(
	TEXT("asda.Crouch.HideAnim"), 1,
	TEXT("[ПОРТ] 1 — под невидимостью (112/186, GM-скрытие) ход вприсядку, как у донора; 0 — шагом walk01, как эталон AsdaStory."),
	ECVF_Default);
static TAutoConsoleVariable<int32> CVarAsdaCrouchNetStatus(
	TEXT("asda.Crouch.NetStatus"), 1,
	TEXT("[ПОРТ] 1 — в приседе 4006 со статусом хода 4 (соседи видят присед, сервер с P80 делит скорость на 2); 0 — статус 2 (эталон)."),
	ECVF_Default);
static TAutoConsoleVariable<int32> CVarAsdaStealthWalkAnim(
	TEXT("asda.Stealth.WalkAnim"), 1,
	TEXT("[ОРИГИНАЛ] 1 — под невидимостью 112/186 ход шагом walk01 (эталон skillHasBuffType112or186, при asda.Crouch.HideAnim 0); ")
	TEXT("0 — бег run01, как порт до 24.10."),
	ECVF_Default);
static TAutoConsoleVariable<int32> CVarAsdaCrouchClipTempo(
	TEXT("asda.Crouch.ClipTempo"), 1,
	TEXT("[ПОРТ] 1 — темп клипа приседа по донору: (скорость / 0.35) · (длина приседа / длина бега); 0 — темп 1."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarAsdaCrouchBackwardReverse(
	TEXT("asda.Crouch.BackwardReverse"), 1,
	TEXT("[ПОРТ] 1 — ход вприсядку назад относительно курса (пятясь на S, S+A/S+D) играет клип приседа в обратную сторону тем же темпом; ")
	TEXT("0 — всегда вперёд (ноги перебирают как при ходе вперёд, прежний порт)."),
	ECVF_Default);
// ★★★ 25.10.2026 [ПОРТ] (запрос владельца) РЕЖИМ CTRL «НАЖАТИЕ / ЗАЖАТИЕ» — пункт «Присед» окна настроек (поле профиля `CrouchMode`).
static ASDA_DEV_CVAR(int32, CVarAsdaCrouchHoldMode,
	TEXT("asda.Crouch.HoldMode"), 1,
	TEXT("[ПОРТ] 1 — Ctrl приседа по пункту «Присед» окна настроек (Нажатие — переключает, Зажатие — присед, пока Ctrl удерживается); ")
	TEXT("0 — всегда «Нажатие», как до 25.10.2026."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarAsdaCrouchHoldReleaseMs,
	TEXT("asda.Crouch.HoldReleaseMs"), 50,
	TEXT("[ПОРТ] «Зажатие»: присед снимается, когда Ctrl отпущен не меньше N мс (дребезг клавиши не дёргает клип и сеть). 0 — сразу."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarAsdaCrouchNetCoalesce,
	TEXT("asda.Crouch.NetCoalesce"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 — смена приседа на ходу «в точку» шлёт 4006, только если статус отличается от последнего отправленного, ")
	TEXT("и не чаще раза в 250 мс (частые переключения не флудят сервер); 0 — прежнее (4006 на каждую смену, если прежний не в пути)."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarAsdaCrouchHoldNoMoveCombo,
	TEXT("asda.Crouch.HoldNoMoveCombo"), 1,
	TEXT("[ПОРТ] «Зажатие»: клавиши хода (команды 51–54, W/S/A/D) при зажатом Ctrl — только ход: их сочетания с Ctrl (Ctrl+W шёпот, ")
	TEXT("Ctrl+S друг) не исполняются, иначе ход вприсядку на W/S открывал бы строку чата. 0 — сочетания исполняются."),
	ECVF_Default);

// Действует ли «Зажатие» (пункт окна настроек и ключ отката). Аксессор — и для контроллера (`PollKeys`): имя ключа в его файл не идёт.
bool AsdaCrouchHoldModeOn()
{
	return CVarAsdaCrouchHoldMode.GetValueOnGameThread() != 0 && UAsdaGameUserSettings::GetCrouchMode() == UAsdaGameUserSettings::kCrouchHold;
}
// «Зажатие» и клавиши хода при зажатом Ctrl — без сочетаний чата (`asda.Crouch.HoldNoMoveCombo`). Аксессор для `PollKeys`.
bool AsdaCrouchHoldBlocksMoveCombos()
{
	return AsdaCrouchHoldModeOn() && CVarAsdaCrouchHoldNoMoveCombo.GetValueOnGameThread() != 0;
}
// Досылка приседа без повторов/флуда (`asda.Crouch.NetCoalesce`).
bool AsdaCrouchNetCoalesceOn()
{
	return CVarAsdaCrouchNetCoalesce.GetValueOnGameThread() != 0;
}

// ★★ 08.11.2026 (пачка 15, баг 3 «присед сам, Ctrl не нажат») [ИСПРАВЛЕНИЕ] ЛОЖНОЕ ПЕРЕКЛЮЧЕНИЕ ПРИСЕДА В «НАЖАТИИ».
//   [ПОРТ] Клавиши приседа нет ни у эталона AsdaStory (Ctrl там только модификатор: `Keybind_ResolveModifier` Shift → Ctrl → Alt), ни у
//   донора — правило «одиночный Ctrl переключает, Ctrl с чем-то ещё — сочетание» наше (24.10). Свой присед ставит ТОЛЬКО оно
//   (журналы игроков: все ВКЛ — «(Ctrl)»; сервер свой 4025 132 отправителю не шлёт — `SendPacket_ClippingSectorInOtherUser@0x41EA80`
//   пропускает userIdx отправителя). «Сочетание» распознавалось по УРОВНЮ клавиш контроллера и пропускало четыре случая:
//   (1) нажатие и отпускание в одном кадре (колесо мыши — всегда; быстрая клавиша/клик при низком FPS); (2) клавиши, клики и колесо,
//   съеденные окном UMG (контроллер их не видит); (3) Shift/Win, зажатые ДО Ctrl (Shift+Ctrl — смена раскладки); (4) Ctrl, зажатый
//   при заглушённом вводе (строка чата), отпущенный после её закрытия. Во всех четырёх отпускание Ctrl сажало в присед.
static TAutoConsoleVariable<int32> CVarAsdaCrouchStrayGuard(
	TEXT("asda.Crouch.StrayGuard"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] «Нажатие»: сочетанием (присед не переключается) считаются и колесо/быстрые нажатия кадра, и нажатия, съеденные ")
	TEXT("окнами, и Shift/Win, зажатые до Ctrl, и Ctrl, зажатый в строке чата. 0 — прежнее (только новые зажатые клавиши)."),
	ECVF_Default);

bool AsdaCrouchStrayGuardOn()
{
	return CVarAsdaCrouchStrayGuard.GetValueOnGameThread() != 0;
}

namespace AsdaCrouchStray {
	// Счётчик нажатий, увиденных Slate до виджетов (клавиши кроме Ctrl и повторов, кнопки мыши, колесо).
	static uint32 GSlateEvents = 0;      // события Slate приходят на игровом потоке
	static uint32 GSlateEventsSeen = 0;   // значение на прошлом опросе

	// Перехватчик Slate: только считает, ничего не поглощает (возврат false — событие идёт дальше как прежде).
	class FWatch final : public IInputProcessor
	{
	public:
		virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
		virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& E) override
		{
			const FKey K = E.GetKey();
			if (!E.IsRepeat() && K != EKeys::LeftControl && K != EKeys::RightControl) { ++GSlateEvents; }
			return false;
		}
		virtual bool HandleMouseButtonDownEvent(FSlateApplication&, const FPointerEvent&) override { ++GSlateEvents; return false; }
		virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication&, const FPointerEvent&) override { ++GSlateEvents; return false; }
		virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication&, const FPointerEvent&, const FPointerEvent*) override { ++GSlateEvents; return false; }
		virtual const TCHAR* GetDebugName() const override { return TEXT("AsdaCrouchStrayWatch"); }
	};
	static TSharedPtr<FWatch> GWatch;   // один на процесс; регистрируется при первом опросе
}

bool AsdaCrouchSlateEventsSince()
{
	using namespace AsdaCrouchStray;
	if (!GWatch.IsValid() && FSlateApplication::IsInitialized())
	{
		GWatch = MakeShared<FWatch>();
		FSlateApplication::Get().RegisterInputPreProcessor(GWatch);
		GSlateEventsSeen = GSlateEvents;
	}
	const uint32 Now = GSlateEvents;
	const bool bAny = Now != GSlateEventsSeen;
	GSlateEventsSeen = Now;
	return bAny;
}

// Самотест: подать событие так, как его видит перехватчик Slate (нажатие, съеденное окном UMG).
void AsdaCrouchStrayDebugSlateEvent()
{
	++AsdaCrouchStray::GSlateEvents;
}

// ══════════ ★★★ 05.11.2026 [ПОРТ] ПРИСЕД СТОЯ — поза и сеть (разбор — `World/AsdaCrouchStand.h`) ═════════════════════════
static TAutoConsoleVariable<int32> CVarAsdaCrouchStandPose(
	TEXT("asda.Crouch.StandPose"), 1,
	TEXT("[ПОРТ] 1 — стоя под приседом (Ctrl, присед соседа, невидимость) поза приседа: кадр клипа anim_239, удерживаемый; ")
	TEXT("0 — как до 05.11 (стоя обычный покой, присед только в ходу)."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarAsdaCrouchStandPoseFrame,
	TEXT("asda.Crouch.StandPoseFrame"), AsdaCrouchStand::kPoseFrameDefault,
	TEXT("Кадр anim_239 (30 к/с) для позы приседа стоя: 10 — стопы проходят друг друга (разбор glb), 0…44."),
	ECVF_Default);
TAutoConsoleVariable<int32> CVarAsdaCrouchStandRelay(
	TEXT("asda.Crouch.StandRelay"), 1,
	TEXT("[ПОРТ] 1 — свой присед/вставание СТОЯ уходит соседям 4024 кодом 132/133 (после 4008 — присед заново), чужой 4025 132/133 и ")
	TEXT("код появления 132 показывают присед соседа стоя; 0 — как до 05.11 (стоя соседи узнают присед только со следующим ходом)."),
	ECVF_Default);

namespace AsdaCrouchStand {
	FRelayState GRelay;

	bool PoseOn() { return CVarAsdaCrouchStandPose.GetValueOnGameThread() != 0; }

	bool RelayOn() { return CVarAsdaCrouchStandRelay.GetValueOnGameThread() != 0; }

	float PoseSec()
	{
		const int32 Frame = FMath::Clamp(CVarAsdaCrouchStandPoseFrame.GetValueOnGameThread(), 0, 44);   // клип: кадры 0…44
		return (float)Frame / 30.f;
	}

	bool Shown(const AAsdaPlayerCharacter* Ch)
	{
		return Ch && PoseOn() && Ch->IsCrouchWalkShown() && !Ch->IsSwimming()
			&& Ch->AnimState != EAsdaPlayerState::Move && Ch->AnimState != EAsdaPlayerState::Dead;
	}

	void Relay(AAsdaPlayerCharacter* Me, bool bOn, bool bForce, const TCHAR* Why)
	{
		if (!Me || CVarAsdaCrouchStandRelay.GetValueOnGameThread() == 0 || !Me->IsLocalPlayerCharacter()) { return; }
		if (Me->IsMoving()) { GRelay.PendingCode = -1; return; }   // в ходу статус несёт 4006 (сток 4024 в ходу отбрасывает)
		const int32 Code = bOn ? kStandOnCode : kStandOffCode;
		if (GRelay.Who.Get() != Me) { GRelay = FRelayState(); GRelay.Who = Me; }
		if (!bForce && GRelay.SentCode == Code) { GRelay.PendingCode = -1; return; }   // соседи уже знают (быстрое вкл→выкл→вкл)
		if (bForce) { GRelay.SentCode = -1; }
		// В очередь: уйдёт из `TickRelay`, когда переключение отстоится `kRelaySettleS` и с прошлой отправки пройдёт `kRelayGapS`.
		if (GRelay.PendingCode != Code) { GRelay.PendingCode = Code; GRelay.PendingS = FPlatformTime::Seconds(); }
		UE_LOG(LogAsdaPlayer, Verbose, TEXT("[присед] стоя — 4024 код %d в очереди (%s)"), Code, Why);
	}

	void Note4006Sent(const AAsdaPlayerCharacter* Me)
	{
		if (Me && GRelay.Who.Get() == Me) { GRelay.SentCode = -1; GRelay.PendingCode = -1; }
	}

	void Note4008Sent(AAsdaPlayerCharacter* Me)
	{
		// Сервер на 4008 пишет статус 0 (вошедший в обзор увидел бы стоящего). Присевший объявляет присед стоя заново — в очередь:
		//   4008 уходит, пока признак хода ещё взведён; код уйдёт из `TickRelay`, когда персонаж стоит (новый ход очередь снимет).
		if (!Me || !Me->IsCrouchOn() || CVarAsdaCrouchStandRelay.GetValueOnGameThread() == 0 || !Me->IsLocalPlayerCharacter()) { return; }
		if (GRelay.Who.Get() != Me) { GRelay = FRelayState(); GRelay.Who = Me; }
		GRelay.SentCode = -1;
		GRelay.PendingCode = kStandOnCode;
		GRelay.PendingS = FPlatformTime::Seconds();
	}

	int32 DebugSentCount()    { return GRelay.SentCount; }
	int32 DebugLastSentCode() { return GRelay.SentCode; }
}

bool AAsdaPlayerCharacter::IsCrouchWalkShown() const
{
	if (CVarAsdaCrouchEnable.GetValueOnGameThread() == 0 || bMounted) { return false; }   // верхом — сидячие клипы седла
	if (bCrouchOwn || bCrouchNet) { return true; }
	// Невидимость: навык (112/186) или GM-скрытие своего (соседей под GM-скрытием не видно вовсе).
	return CVarAsdaCrouchHideAnim.GetValueOnGameThread() != 0 && (bSkillStealth || bGmHideTranslucent);
}

int32 AAsdaPlayerCharacter::GetMoveStatusFor4006() const
{
	// 4 — только если присед и правда на экране (не верхом, не в воде): скорость сервера совпадает с тем, что видно.
	const bool bCrouch = bCrouchOwn && CVarAsdaCrouchEnable.GetValueOnGameThread() != 0
		&& CVarAsdaCrouchNetStatus.GetValueOnGameThread() != 0 && !bMounted && !bSwimming;
	return bCrouch ? kCrouchMoveStatus : 2;
}

float AAsdaPlayerCharacter::CrouchClipRate() const
{
	const float Sign = CrouchClipTimeSign();   // ★ 24.10.2026: пятясь — время клипа назад
	if (CVarAsdaCrouchClipTempo.GetValueOnGameThread() == 0) { return Sign; }
	// Донор `0x4A8130`: время клипа += 2.857·скорость·dt·(len40/len5), 2.857·0.35 = 1 — при базовой скорости клип бега идёт
	//   темпом 1, а присед растянут так, чтобы его цикл покрывал ту же дистанцию, что цикл бега текущего набора.
	if (CrouchLenCache <= 0.f)
	{
		if (UAnimSequence* A = UAsdaModelAnimSubsystem::LoadClipChecked(FString::Printf(TEXT("%sanim_%02d"), *CharBase(Sex), kClipCrouchWalk)))
		{
			CrouchLenCache = A->GetPlayLength();
		}
	}
	const int32 RunIdx = MotionClipIdx(EAsdaMotionSlot::Run01);
	if (RunIdx != CrouchRunIdxCache)
	{
		CrouchRunIdxCache = RunIdx;
		UAnimSequence* R = UAsdaModelAnimSubsystem::LoadClipChecked(FString::Printf(TEXT("%sanim_%02d"), *CharBase(Sex), RunIdx));
		CrouchRunLenCache = R ? R->GetPlayLength() : 0.f;
	}
	if (CrouchLenCache <= 0.f || CrouchRunLenCache <= 0.f) { return Sign; }
	const float SpeedK = FMath::Max(0.01f, MoveSpeed) / 0.35f;
	return Sign * FMath::Clamp(SpeedK * (CrouchLenCache / CrouchRunLenCache), 0.25f, 4.f);
}

// ★ 24.10.2026 [ПОРТ] ход вприсядку назад (владелец: «пятясь перебирает ногами как вперёд»). Разбор у объявления.
//   [ОРИГИНАЛ] обычный ход назад — отдельный клип: `CCharacter_ApplyMoveState` (game.c:81756) при байте +209108 (пятка, [208])
//   двигает часы ТРЕКА 24 (back01) на +dt — вперёд; байт пишет только `updatePlayerMovement` своему (game.c:8882), соседи
//   пятку не получают (4007 несёт лишь cur/dst — курс соседа = направление хода). Клипа «присед назад» у донора нет — отсюда
//   обратное время anim_239. Правило — знак проекции направления хода на ЛОГИЧЕСКИЙ курс (`GetFacingDirGame`: ставится сразу,
//   видимый доворот 600 °/с на знак не влияет): < −0.17 (угол > ~100°) — назад, иначе вперёд (чистый бок, |проекция| мала, —
//   вперёд, как стрейф). На практике порт ставит курс = ±направление (пятка — минус), поэтому проекция ±1 и мерцания нет.
float AAsdaPlayerCharacter::CrouchClipTimeSign() const
{
	if (CVarAsdaCrouchBackwardReverse.GetValueOnGameThread() == 0 || !bMoving) { return 1.f; }
	FVector2D Dir = bMoveToPoint ? (MoveDstGame - GetGamePosXZ()) : MoveDirGame;
	if (Dir.SizeSquared() < 1.f) { return bBackpedal ? -1.f : 1.f; }   // у самой цели направления нет — по признаку пятки
	Dir.Normalize();
	constexpr float kBackCos = -0.17f;   // cos ~100°: назад только то, что заметно позади бока
	return (FVector2D::DotProduct(Dir, GetFacingDirGame()) < kBackCos) ? -1.f : 1.f;
}

void AAsdaPlayerCharacter::PlayMoveClipAfterBackpedalSwitch()
{
	// ★ 09.11.2026 [ИСПРАВЛЕНИЕ]: плывущий и пятясь плывёт swim01 по ходу — клип хода не меняется, рывка на кадр 0 нет.
	if (SwimFacesMoveDir() && MoveClip() == kClipSwimMove && GetPlayingClipName().EndsWith(FString::Printf(TEXT("anim_%d"), kClipSwimMove))) { return; }
	// Присед остаётся приседом: разворот времени с текущей позы (бленда и рывка на кадр 0 нет). Ключ 0 — прежний перезапуск.
	if (CVarAsdaCrouchBackwardReverse.GetValueOnGameThread() != 0 && BaseSkel && MoveClip() == kClipCrouchWalk
		&& GetPlayingClipName().EndsWith(FString::Printf(TEXT("anim_%d"), kClipCrouchWalk)))
	{
		BaseSkel->SetPlayRate(CrouchClipRate());
		return;
	}
	PlayWorldClip(MoveClip(), true);
}

void AAsdaPlayerCharacter::RefreshMoveClipAfterCrouchChange()
{
	// ★ 05.11.2026 [ПОРТ] стоя: клип покоя ↔ поза приседа (`PlayWorldClip` подменяет клип покоя кадром anim_239). Те же запреты,
	//   что у хода: эмоция, замах, навык, сидение, действие, прыжок, седло, вода — свои клипы.
	if (AnimState == EAsdaPlayerState::Idle && AsdaCrouchStand::PoseOn() && !bMounted && SitPhase == 0 && !bSwimming)
	{
		if (IsEtalonActionBusy() || IsAttackAnimActive() || IsSkillAnimActive() || IsEmotionAnimActive()) { return; }
		if (bJumpActive && JumpClipShown >= 0) { return; }
		PlayWorldClip(IdleClipIdx(), true);
		IdleTimer = 0.f; FidgetRemaining = 0.f;
		return;
	}
	// Клип меняем только там, где играет клип хода (эмоция, замах, навык, сидение, действие, прыжок, седло — свои).
	if (AnimState != EAsdaPlayerState::Move || bMounted || SitPhase != 0) { return; }
	if (IsEtalonActionBusy() || IsAttackAnimActive() || IsSkillAnimActive() || IsEmotionAnimActive()) { return; }
	if (bJumpActive && JumpClipShown >= 0) { return; }   // клип прыжка доиграет и сам вернёт клип хода (OnJumpClipEnd)
	PlayWorldClip(MoveClip(), true);
}

bool AAsdaPlayerCharacter::SetCrouch(bool bOn, const TCHAR* Why)
{
	LastCrouchRefusal.Reset();
	if (bOn == bCrouchOwn) { return bCrouchOwn; }
	if (bOn)
	{
		// [ПОРТ] у донора клавиши приседа нет — запреты наши, по образцу прыжка: верхом / за рулём / мёртвым / сидя / в действии.
		if (CVarAsdaCrouchEnable.GetValueOnGameThread() == 0) { LastCrouchRefusal = TEXT("ключ asda.Crouch.Enable 0"); return false; }
		if (bMounted) { LastCrouchRefusal = TEXT("верхом"); return false; }
		if (AnimState == EAsdaPlayerState::Dead) { LastCrouchRefusal = TEXT("мёртв"); return false; }
		if (SitPhase != 0) { LastCrouchRefusal = TEXT("сидит"); return false; }
		if (IsEtalonActionBusy()) { LastCrouchRefusal = TEXT("занят действием (эмоция/рыбалка/копание)"); return false; }
		if (const UGameInstance* GI = GetGameInstance())
		{
			if (const UAsdaVehicleRideSubsystem* Vr = GI->GetSubsystem<UAsdaVehicleRideSubsystem>()) { if (Vr->IsRiding()) { LastCrouchRefusal = TEXT("за рулём"); return false; } }
		}
	}
	bCrouchOwn = bOn;
	UE_LOG(LogAsdaPlayer, Log, TEXT("[присед] %s: %s (%s), статус хода 4006 = %d"), *GetName(), bOn ? TEXT("ВКЛ") : TEXT("ВЫКЛ"), Why,
		GetMoveStatusFor4006());
	RefreshMoveClipAfterCrouchChange();
	// ★ 05.11.2026 [ПОРТ] стоя — соседям 4024 132/133 (4006 в ту же клетку сервер отвергает; разбор — AsdaCrouchStand.h).
	if (!bMoving && IsLocalPlayerCharacter()) { AsdaCrouchStand::Relay(this, bCrouchOwn, /*bForce*/false, Why); }
	// Идёт «в точку» — сразу сообщаем серверу новый статус: 4006 из текущей точки к той же цели (ветка «идёт» CharMove принимает
	//   её; сервер пересчитает скорость и разошлёт соседям 4007 с новым статусом). Клавиатурный ход повторяет 4006 сам (≤ 1 с),
	//   стоя — статус уйдёт со следующим ходом.
	if (bMoving && IsMovingToPoint() && IsLocalPlayerCharacter())
	{
		if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
		{
			if (Net->bInGameConnected && AsdaCrouchNetCoalesceOn())
			{
				AsdaSyncCrouchStatusToServer(this, Net);   // ★ 25.10.2026: тот же статус, что уже у сервера, — не шлём; не чаще 250 мс
			}
			else if (Net->bInGameConnected && !Net->IsMoveRequestInFlight())
			{
				const FVector2D Cur = GetGamePosXZ();
				if (FVector2D::Distance(Cur, MoveDstGame) > 1.f)
				{
					// ★ Ревизия 24.10: цель хода (подход к мобу/персонажу, 4006 @+25) сохраняется — тот же renderId, что у прежнего 4006.
					Net->SendMove4006((float)Cur.X, (float)Cur.Y, (float)MoveDstGame.X, (float)MoveDstGame.Y, LastSent4006TargetRid);
				}
				GAsdaCrouchResendPending.Reset();
			}
			else if (Net->bInGameConnected)
			{
				GAsdaCrouchResendPending = this;   // прежний 4006 в пути — дошлём после ответа
			}
		}
	}
	return bCrouchOwn;
}

void AAsdaPlayerCharacter::TickCrouchInput(bool bCtrlDown, bool bOtherKeyDown, bool bInputAllowed)
{
	// ★★★ 25.10.2026 [ПОРТ] «ЗАЖАТИЕ» (пункт «Присед» окна настроек, `UAsdaGameUserSettings::CrouchMode` = 1): присед, пока Ctrl
	//   удерживается. Уровень, а не фронт: Ctrl зажат — присед сразу (запрет — верхом/мёртв/сидя/в действии — проверяется каждый кадр,
	//   присед встанет, как только запрет снимется); Ctrl отпущен не меньше `asda.Crouch.HoldReleaseMs` (дребезг) — встал. Сочетания
	//   с Ctrl присед не снимают (Ctrl зажат — значит, присед). Ввод заглушён (набор в чате, модалка, рыбалка…) — состояние замирает,
	//   после снятия глушения снова идёт за Ctrl. Окно потеряло фокус — движок отпускает клавиши → встал. Смена карты: новый персонаж
	//   стоит и присядет, когда новый контроллер увидит Ctrl зажатым. Невидимость показывает присед сама (`IsCrouchWalkShown`) — от
	//   режима не зависит. Сеть — общий путь `SetCrouch` (досылка без повторов, `asda.Crouch.NetCoalesce`).
	if (AsdaCrouchHoldModeOn())
	{
		bCrouchCtrlWasDown = false; bCrouchOtherDuring = false;   // фронты «Нажатия» не ведутся (переход в «Нажатие» — с чистого листа)
		if (!bInputAllowed) { GCrouchHoldRelease.UpSinceS = -1.0; return; }
		if (bCtrlDown)
		{
			GCrouchHoldRelease.UpSinceS = -1.0;
			if (!bCrouchOwn) { SetCrouch(true, TEXT("Ctrl зажат")); }
			return;
		}
		if (!bCrouchOwn) { GCrouchHoldRelease.UpSinceS = -1.0; return; }
		const double Now = FPlatformTime::Seconds();
		if (GCrouchHoldRelease.Who.Get() != this || GCrouchHoldRelease.UpSinceS < 0.0) { GCrouchHoldRelease.Who = this; GCrouchHoldRelease.UpSinceS = Now; }
		if ((Now - GCrouchHoldRelease.UpSinceS) * 1000.0 >= (double)FMath::Max(0, CVarAsdaCrouchHoldReleaseMs.GetValueOnGameThread()))
		{
			GCrouchHoldRelease.UpSinceS = -1.0;
			SetCrouch(false, TEXT("Ctrl отпущен"));
		}
		return;
	}
	if (!bInputAllowed)   // набор в чате и т.п. — не наше
	{
		// ★ 08.11.2026 (пачка 15, баг 3) [ИСПРАВЛЕНИЕ] Ctrl, зажатый, пока ввод заглушён (Ctrl+V в строке чата, Ctrl в модалке), —
		//   модификатор того окна: строка закрылась (Enter) при ещё зажатом Ctrl — его отпускание прежде считалось одиночным
		//   нажатием и сажало в присед. Теперь он помечен сочетанием до отпускания. Ключ 0 — прежнее (фронты сбрасываются).
		const bool bSwallow = bCtrlDown && AsdaCrouchStrayGuardOn();
		bCrouchCtrlWasDown = bSwallow; bCrouchOtherDuring = bSwallow;
		return;
	}
	if (bCtrlDown)
	{
		if (!bCrouchCtrlWasDown) { bCrouchOtherDuring = false; }   // фронт нажатия
		if (bOtherKeyDown) { bCrouchOtherDuring = true; }
		bCrouchCtrlWasDown = true;
		return;
	}
	if (!bCrouchCtrlWasDown) { return; }
	bCrouchCtrlWasDown = false;
	const bool bCombo = bCrouchOtherDuring || bOtherKeyDown;   // признак кадра отпускания (потеря фокуса) — тоже сочетание
	bCrouchOtherDuring = false;
	if (bCombo) { return; }                                         // было сочетание — Ctrl как модификатор
	SetCrouch(!bCrouchOwn, TEXT("Ctrl"));
}

void AAsdaPlayerCharacter::SetNetCrouch(bool bOn)
{
	if (bCrouchNet == bOn) { return; }
	bCrouchNet = bOn;
	UE_LOG(LogAsdaPlayer, Verbose, TEXT("[присед] %s: сосед %s (статус 4007)"), *GetName(), bOn ? TEXT("вприсядку") : TEXT("обычный ход"));
	RefreshMoveClipAfterCrouchChange();
}

void AAsdaPlayerCharacter::SetSkillStealth(bool bOn)
{
	if (bSkillStealth == bOn) { return; }
	bSkillStealth = bOn;
	// эталон: ход слотом walk01 при 112/186 (при ключе HideAnim 1 — присед донора). ★ Ревизия 24.10: свой ключ отката —
	//   `asda.Stealth.WalkAnim 0` вместе с `asda.Crouch.HideAnim 0` возвращают прежний вид порта (бег run01).
	bSlowWalkBuff = bOn && CVarAsdaStealthWalkAnim.GetValueOnGameThread() != 0;
	UE_LOG(LogAsdaPlayer, Log, TEXT("[невидимость] %s: состояние 112/186 %s"), *GetName(), bOn ? TEXT("ЕСТЬ") : TEXT("снято"));
	RefreshMoveClipAfterCrouchChange();
}
