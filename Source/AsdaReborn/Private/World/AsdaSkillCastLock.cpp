// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSkillCastLock.cpp — «свой навык занят»: гейты ввода и одно отложенное действие после навыка. Разбор эталона, ключи и
//   правила сброса — в шапке `World/AsdaSkillCastLock.h`. ★ 05.11.2026 (сессия skill-cast-lock).
#include "World/AsdaSkillCastLock.h"
#include "AsdaDevConsole.h"                  // ASDA_DEV_CVAR — ключи отката
#include "World/AsdaPlayerController.h"
#include "AsdaNetSubsystem.h"                  // бит 2 (4006 ждёт 4007) для очереди клика
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaWorldSubsystem.h"
#include "UI/AsdaSkillUseSubsystem.h"        // состояние действия заявки навыка 25/24/14
#include "UI/AsdaCastBarSubsystem.h"         // состояние полосы каста 13/14/24/27
#include "UI/AsdaInventorySubsystem.h"       // повтор применения расходника
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCastLock, Log, All);

// [ОРИГИНАЛ] Ход во время навыка: клик по земле и приказ автопути в 24 — отложенное действие 16 (после клипа), в 25/14/13/27 —
//   отброшены; в 14/13/27 мировой клик игнорируется целиком; клавиши хода видят 25/14 (разбор — шапка .h).
static ASDA_DEV_CVAR(int32, CVarAsdaCastLockMove,
	TEXT("asda.Skill.CastLockMove"), 1,
	TEXT("[ОРИГИНАЛ] 1 (по умолчанию) = во время своего навыка хода нет: клик по земле/приказ автопути во время клипа (24) исполняется ")
	TEXT("сразу после него (отложенное действие 16, game_main.c:6693), во время заявки/каста (25/14/13/27) — отброшен; в 14/13/27 мировой ")
	TEXT("клик игнорируется целиком (input.c:150); клавиши хода видят 25/14. 0 = прежний порт (клик по земле сразу давал ход)."),
	ECVF_Default);

// [ОРИГИНАЛ] Эмоция только из состояния действия 0 (`UI_DispatchTargetContextMenu`, дизасм 0xC53349).
static ASDA_DEV_CVAR(int32, CVarAsdaCastLockEmote,
	TEXT("asda.Skill.CastLockEmote"), 1,
	TEXT("[ОРИГИНАЛ] 1 (по умолчанию) = во время своего навыка (25/24/14/13/27) эмоция не играет, молча (эталон: только состояние 0). ")
	TEXT("0 = прежний порт (эмоция ложилась поверх клипа навыка)."),
	ECVF_Default);

// [ИСПРАВЛЕНИЕ] (просьба владельца 05.11.2026) Цель во время навыка не меняется — нажатие исполняется после навыка.
static ASDA_DEV_CVAR(int32, CVarAsdaCastLockTarget,
	TEXT("asda.Skill.CastLockTarget"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = во время своего навыка клик по мобу/игроку/NPC/луту и клавиши цели 59/60/62/64 цель не меняют: ")
	TEXT("последнее нажатие исполняется сразу после навыка (у оригинала цель менялась посреди навыка, input.c:150/4678). 0 = как раньше."),
	ECVF_Default);

// [ИСПРАВЛЕНИЕ] (просьба владельца 05.11.2026) Расходник во время навыка применяется после навыка.
static ASDA_DEV_CVAR(int32, CVarAsdaCastLockItems,
	TEXT("asda.Skill.CastLockItems"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = расходник (5028: зелья, свитки, маунты) во время своего навыка не применяется — последний ")
	TEXT("нажатый применяется сразу после навыка (у оригинала гейта нет, item_inventory.c:38650/40485). 0 = как раньше (сразу)."),
	ECVF_Default);

// [ИСПРАВЛЕНИЕ] (баг игроков 05.11.2026 «частые клики теряются»): защита от флуда оригинала (бит 2, троттл 150 мс) сохранена —
//   серверу уходит не чаще, — но последнее нажатие не теряется.
static ASDA_DEV_CVAR(int32, CVarAsdaClickQueue,
	TEXT("asda.Input.ClickQueue"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = мировой клик, который оригинал выбрасывал (прошлый 4006 ещё без ответа 4007 — бит 2; ход чаще ")
	TEXT("150 мс — input.c:570) не теряется: последнее нажатие исполняется, как только гейт ")
	TEXT("открылся (очередь с вытеснением, серверу — не чаще прежнего). 0 = как оригинал (такие клики пропадали)."),
	ECVF_Default);

// Доступ к закрытым методам контроллера (friend в AsdaPlayerController.h): тот же путь, что у мыши и клавиатуры.
struct FAsdaSkillCastLockAccess
{
	// Пикинг мирового клика (`PickForClick`) — вид попадания под курсором.
	static AAsdaPlayerController::FAsdaPick Pick(AAsdaPlayerController* PC) { return PC->PickForClick(); }
	// Мировой клик (`OnLeftClick`) — одиночный/двойной.
	static void Click(AAsdaPlayerController* PC, bool bDouble) { PC->OnLeftClick(bDouble); }
	// Клавиша раскладки (`ExecuteCommand`).
	static void Command(AAsdaPlayerController* PC, int32 Cmd) { PC->ExecuteCommand(Cmd); }
	// Состояние действия эталона (`KbEtalonActionState`).
	static int32 ActionState(const AAsdaPlayerController* PC) { return PC->KbEtalonActionState(); }
	// Обездвиживающее состояние (`IsMoveInputLockedByState`).
	static bool MoveLocked(const AAsdaPlayerController* PC) { return PC->IsMoveInputLockedByState(); }
	// Подмена экранной точки мирового клика (`DebugClickScreen`): повтор и самотест.
	static TOptional<FVector2D>& Screen(AAsdaPlayerController* PC) { return PC->DebugClickScreen; }
	// Признак «этот приказ движения — шаг межкартового автопути» (`bTravelStepIssued`, взводит `BeginTravelStep`).
	static bool& TravelStep(AAsdaPlayerController* PC) { return PC->bTravelStepIssued; }
	// Ход по клику в точку (хвост ветки земли: троттл, клавиатура, 1013, маршрут).
	static void GroundMove(AAsdaPlayerController* PC, const FVector& P) { PC->GroundClickMove(P); }
	// Время последней отправки 4006 по клику (троттл 150 мс).
	static double LastMoveSend(const AAsdaPlayerController* PC) { return PC->LastMoveSendTime; }
	// Конечная точка маршрута (самотест).
	static bool RouteFinal(const AAsdaPlayerController* PC, FVector2D& Out) { if (!PC->HasRoute()) { return false; } Out = PC->Route.Last(); return true; }
};

namespace AsdaSkillCastLockLocal
{
	using FPick = AAsdaPlayerController::FAsdaPick;
	using EPick = AAsdaPlayerController::EAsdaPickKind;

	constexpr double kStaleS = 15.0;          // слот без конца навыка дольше — сброс (страховка от вечного ожидания)
	constexpr float kCamSameDist = 2.f;       // камера «не сдвинулась»: смещение, юн UE
	constexpr float kCamSameDeg = 0.5f;       // и поворот, градусы

	enum class EAction : int32 { None = 0, Click = 1, MapMove = 2, Command = 3, GroundMove = 4 };   // вид отложенного действия
	constexpr double kFloodStaleS = 3.0;      // клик, ждущий ответа 4007/троттла, дольше — сброс (бит 2 сам снимается сторожем 10 с)
	constexpr double kMoveThrottleS = 0.150;  // троттл хода по клику эталона (input.c:570, 0x96 мс)

	// Отложенное действие (одно, как поле +52121 эталона: новое вытесняет прежнее).
	struct FActionSlot
	{
		EAction Kind = EAction::None;              // что исполнить
		FPick Pick;                                // клик: запомненный результат пикинга
		bool bSingle = false, bDouble = false;     // клик: одиночное и/или двойное событие
		bool bHaveScreen = false;                  // клик: экранная точка известна
		FVector2D Screen = FVector2D::ZeroVector;  // клик: экранная точка
		FVector CamLoc = FVector::ZeroVector;      // клик: камера в момент клика
		FRotator CamRot = FRotator::ZeroRotator;   // клик: её поворот
		FVector2D MapDst = FVector2D::ZeroVector;  // автопуть: точка назначения (game XZ)
		FVector MovePointUe = FVector::ZeroVector; // ход по клику (троттл): точка UE
		bool bFlood = false;                       // поставлено защитой от флуда (бит 2 / троттл), а не навыком
		bool bTravelStep = false;                  // автопуть: это шаг межкартового пути (`BeginTravelStep`)
		int32 Cmd = 0;                             // клавиша раскладки
		int32 Phase = 0;                           // фаза навыка в момент нажатия (журнал)
		double SinceS = 0.0;                       // когда поставлено
	};
	// Отложенный расходник (последний нажатый).
	struct FItemSlot
	{
		bool bSet = false;                                   // слот занят
		TWeakObjectPtr<UAsdaInventorySubsystem> Inv;         // инвентарь, в который применять
		int32 Container = 0, Cell = 0, Target = -1, ItemCode = 0;   // аргументы `UseConsumable` и код для сверки ячейки
		int32 Phase = 0;                                     // фаза навыка в момент нажатия
		double SinceS = 0.0;                                 // когда поставлено
	};

	FActionSlot GAction;                       // слот действия
	FItemSlot GItem;                           // слот предмета
	TWeakObjectPtr<APawn> GOwnerPawn;          // чей персонаж поставил слоты (смена персонажа/карты — сброс)
	bool GReplaying = false;                   // идёт повтор клика
	bool GReplayNoScreen = false;              // повтор без экранной точки
	FPick GReplayPick;                         // результат пикинга для повтора
	bool GExecutingItem = false;               // идёт повтор расходника (гейт не ставит его снова)
	int32 GExecuted = 0;                       // исполнено отложенных (самотест)
	int32 GDropped = 0;                        // отброшено гейтами / сброшено из слотов (самотест)

	const TCHAR* PickName(EPick K)
	{
		switch (K)
		{
		case EPick::Ground: return TEXT("земля");
		case EPick::Mob:    return TEXT("моб");
		case EPick::Player: return TEXT("игрок");
		case EPick::Npc:    return TEXT("NPC");
		case EPick::Loot:   return TEXT("лут");
		default:            return TEXT("пусто");
		}
	}

	AAsdaPlayerCharacter* SelfChar(const UGameInstance* GI)
	{
		const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		return (WS && IsValid(WS->LocalPlayerActor)) ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get()) : nullptr;
	}

	void NoteOwner(const AAsdaPlayerController* PC)
	{
		GOwnerPawn = PC ? PC->GetPawn() : nullptr;
	}

	void ClearAction() { GAction = FActionSlot(); }
	bool IsGroundPick(EPick K) { return K == EPick::Ground || K == EPick::None; }   // попадание «земля/пусто» (ход, не цель)
	void ClearItem() { GItem = FItemSlot(); }

	// Записать клик в слот действия (последний вытесняет прежний; двойное событие того же нажатия дописывается).
	void StoreClick(AAsdaPlayerController* PC, const FPick& P, bool bDouble, int32 Phase)
	{
		if (bDouble && GAction.Kind == EAction::Click && GAction.Pick.Kind == P.Kind && GAction.Pick.Actor == P.Actor && !GAction.bDouble)
		{
			GAction.bDouble = true;   // второе нажатие двойного клика по тому же попаданию
			UE_LOG(LogAsdaCastLock, Log, TEXT("[навык занят] двойной клик по «%s» — дописан к отложенному клику"), PickName(P.Kind));
			return;
		}
		if (GAction.Kind != EAction::None)
		{
			UE_LOG(LogAsdaCastLock, Log, TEXT("[навык занят] прежнее отложенное действие %d вытеснено новым кликом (одно поле, как +52121)"), (int32)GAction.Kind);
		}
		ClearAction();
		GAction.Kind = EAction::Click;
		GAction.Pick = P;
		GAction.bSingle = !bDouble;
		GAction.bDouble = bDouble;
		float X = 0.f, Y = 0.f;
		GAction.bHaveScreen = PC->GetClickScreenPos(X, Y);
		GAction.Screen = FVector2D(X, Y);
		// Земля: точку хода считаем СЕЙЧАС лучом по .TER (как ветка земли `OnLeftClick`) — повтор после сдвига камеры (рывок, поворот)
		//   идёт без экранной точки и иначе взял бы сырое попадание луча (дальняя геометрия → «недоступная область»).
		FVector TerrUe;
		if (IsGroundPick(P.Kind) && GAction.bHaveScreen && AsdaSkillCastLock::TerrainPointUnderScreen(PC, GAction.Screen, TerrUe))
		{
			GAction.Pick.Point = TerrUe;
			GAction.Pick.Kind = EPick::Ground;
		}
		if (const APlayerCameraManager* Cam = PC->PlayerCameraManager) { GAction.CamLoc = Cam->GetCameraLocation(); GAction.CamRot = Cam->GetCameraRotation(); }
		GAction.Phase = Phase;
		GAction.SinceS = FPlatformTime::Seconds();
		NoteOwner(PC);
	}

	// Повтор клика: запомненный пикинг; экранная точка — если камера та же, иначе без неё (луч по земле не пересчитывается).
	void ReplayClick(AAsdaPlayerController* PC, const FActionSlot& A)
	{
		bool bCamSame = false;
		if (const APlayerCameraManager* Cam = PC->PlayerCameraManager)
		{
			bCamSame = FVector::Dist(Cam->GetCameraLocation(), A.CamLoc) <= kCamSameDist
				&& FMath::Abs(FRotator::NormalizeAxis(Cam->GetCameraRotation().Yaw - A.CamRot.Yaw)) <= kCamSameDeg
				&& FMath::Abs(FRotator::NormalizeAxis(Cam->GetCameraRotation().Pitch - A.CamRot.Pitch)) <= kCamSameDeg;
		}
		const TOptional<FVector2D> SavedScreen = FAsdaSkillCastLockAccess::Screen(PC);   // подмена самотеста — вернуть после повтора
		TGuardValue<bool> GuardR(GReplaying, true);
		TGuardValue<bool> GuardN(GReplayNoScreen, !(A.bHaveScreen && bCamSame));
		GReplayPick = A.Pick;
		if (A.bHaveScreen && bCamSame) { FAsdaSkillCastLockAccess::Screen(PC) = A.Screen; }
		UE_LOG(LogAsdaCastLock, Log, TEXT("[навык занят] навык окончен — исполняется отложенный клик по «%s» (одиночный %d, двойной %d, экранная точка %s)"),
			PickName(A.Pick.Kind), A.bSingle ? 1 : 0, A.bDouble ? 1 : 0, (A.bHaveScreen && bCamSame) ? TEXT("та же") : TEXT("нет — камера сдвинулась"));
		if (A.bSingle) { FAsdaSkillCastLockAccess::Click(PC, /*bDouble*/false); }
		if (A.bDouble) { FAsdaSkillCastLockAccess::Click(PC, /*bDouble*/true); }
		FAsdaSkillCastLockAccess::Screen(PC) = SavedScreen;
		GReplayPick = FPick();
	}
}

int32 AsdaSkillCastLock::SelfSkillPhase(const UGameInstance* GI)
{
	using namespace AsdaSkillCastLockLocal;
	if (!GI) { return 0; }
	const UAsdaSkillUseSubsystem* SU = GI->GetSubsystem<UAsdaSkillUseSubsystem>();
	const int32 S = SU ? SU->GetSelfCharState() : 0;   // заявка 25 / клип 24 / каст-ожидание 14 (ставит диспетчер навыков)
	if (S == 14) { return 14; }
	if (const UAsdaCastBarSubsystem* Cb = GI->GetSubsystem<UAsdaCastBarSubsystem>())
	{
		const int32 Cs = Cb->GetState();
		if (Cb->IsActive() && (Cs == 13 || Cs == 14 || Cs == 27)) { return Cs; }
		if (Cb->IsActive() && Cs == 24) { return 24; }
	}
	if (S == 24) { return 24; }
	if (const AAsdaPlayerCharacter* Ch = SelfChar(GI); Ch && Ch->IsSkillAnimActive()) { return 24; }   // клип навыка ещё играет
	if (S == 25) { return 25; }
	return 0;
}

bool AsdaSkillCastLock::MoveLockOn() { return CVarAsdaCastLockMove.GetValueOnGameThread() != 0; }
bool AsdaSkillCastLock::TargetLockOn() { return CVarAsdaCastLockTarget.GetValueOnGameThread() != 0; }
bool AsdaSkillCastLock::ItemLockOn() { return CVarAsdaCastLockItems.GetValueOnGameThread() != 0; }

int32 AsdaSkillCastLock::PhaseForActionState(const UGameInstance* GI)
{
	return MoveLockOn() ? SelfSkillPhase(GI) : 0;
}

bool AsdaSkillCastLock::GateWorldClick(AAsdaPlayerController* PC, bool bDouble)
{
	using namespace AsdaSkillCastLockLocal;
	if (!PC || GReplaying) { return false; }
	if (!MoveLockOn() && !TargetLockOn()) { return false; }
	const int32 Phase = SelfSkillPhase(PC->GetGameInstance());
	if (Phase == 0) { return false; }
	const FPick P = FAsdaSkillCastLockAccess::Pick(PC);
	if (IsGroundPick(P.Kind))
	{
		if (!MoveLockOn()) { return false; }
		if (Phase == 24)
		{
			// [ОРИГИНАЛ] `CGame_HandleGroundClickAttack` case 24 → отложенное действие 16 (game_main.c:6693-6708).
			StoreClick(PC, P, bDouble, Phase);
			UE_LOG(LogAsdaCastLock, Log, TEXT("[навык занят] клик по земле во время клипа навыка (24) — ход после клипа (отложенное действие 16)"));
			return true;
		}
		// [ОРИГИНАЛ] 25/14/13/27: `CGame_HandleGroundClickAttack` case 14/15/25 → 0, input.c:150/568 — хода нет, клик потерян.
		++GDropped;
		UE_LOG(LogAsdaCastLock, Log, TEXT("[навык занят] клик по земле в состоянии %d (заявка/каст навыка) — без хода (эталон input.c:150/568)"), Phase);
		return true;
	}
	if (TargetLockOn())
	{
		// [ИСПРАВЛЕНИЕ] цель посреди навыка не меняется — клик по мобу/игроку/NPC/луту исполняется после навыка.
		StoreClick(PC, P, bDouble, Phase);
		UE_LOG(LogAsdaCastLock, Log, TEXT("[навык занят] клик по «%s» в состоянии %d — цель не меняется, клик исполнится после навыка"),
			PickName(P.Kind), Phase);
		return true;
	}
	if (MoveLockOn() && (Phase == 14 || Phase == 13 || Phase == 27))
	{
		++GDropped;   // [ОРИГИНАЛ] input.c:150 — в 14/13/27 мировой клик игнорируется целиком
		UE_LOG(LogAsdaCastLock, Log, TEXT("[навык занят] клик по «%s» в состоянии %d (каст) — игнорирован (эталон input.c:150)"), PickName(P.Kind), Phase);
		return true;
	}
	return false;
}

bool AsdaSkillCastLock::ClickQueueOn() { return CVarAsdaClickQueue.GetValueOnGameThread() != 0; }

bool AsdaSkillCastLock::QueueBlockedClick(AAsdaPlayerController* PC, bool bDouble)
{
	using namespace AsdaSkillCastLockLocal;
	if (!PC || !ClickQueueOn()) { return false; }
	// При повторе (новый 4006 ушёл раньше, бит 2 снова взведён) пикинг — запомненный: экран мог уехать.
	const FPick P = GReplaying ? GReplayPick : FAsdaSkillCastLockAccess::Pick(PC);
	const bool bHadFlood = GAction.Kind != EAction::None && GAction.bFlood;
	if (GReplaying)
	{
		GAction = FActionSlot(); GAction.Kind = EAction::Click; GAction.Pick = P; GAction.bSingle = !bDouble; GAction.bDouble = bDouble;
		NoteOwner(PC);
	}
	else { StoreClick(PC, P, bDouble, 0); }
	GAction.bFlood = true;
	GAction.SinceS = FPlatformTime::Seconds();
	UE_LOG(LogAsdaCastLock, Log, TEXT("[очередь клика] клик по «%s» (%s) при 4006 без ответа (бит 2) — исполнится после ответа 4007%s"),
		PickName(P.Kind), bDouble ? TEXT("двойной") : TEXT("одиночный"), (bHadFlood && !bDouble) ? TEXT(", прежнее нажатие вытеснено") : TEXT(""));
	return true;
}

bool AsdaSkillCastLock::QueueGroundMove(AAsdaPlayerController* PC, const FVector& PointUe)
{
	using namespace AsdaSkillCastLockLocal;
	if (!PC || !ClickQueueOn()) { return false; }
	if (GAction.Kind != EAction::None && !GAction.bFlood)
	{
		UE_LOG(LogAsdaCastLock, Log, TEXT("[очередь клика] прежнее отложенное действие %d вытеснено ходом по клику"), (int32)GAction.Kind);
	}
	ClearAction();
	GAction.Kind = EAction::GroundMove;
	GAction.MovePointUe = PointUe;
	GAction.bFlood = true;
	GAction.SinceS = FPlatformTime::Seconds();
	NoteOwner(PC);
	UE_LOG(LogAsdaCastLock, Log, TEXT("[очередь клика] ход по клику чаще 150 мс — точка (%.0f,%.0f) исполнится по окончании троттла"), PointUe.X, PointUe.Y);
	return true;
}

bool AsdaSkillCastLock::IsReplayingClick() { return AsdaSkillCastLockLocal::GReplaying; }
bool AsdaSkillCastLock::IsReplayNoScreen() { return AsdaSkillCastLockLocal::GReplaying && AsdaSkillCastLockLocal::GReplayNoScreen; }

bool AsdaSkillCastLock::GetReplayPick(uint8& OutKind, AActor*& OutActor, FVector& OutPoint, UPrimitiveComponent*& OutComp)
{
	using namespace AsdaSkillCastLockLocal;
	if (!GReplaying) { return false; }
	OutKind = (uint8)GReplayPick.Kind;
	OutActor = GReplayPick.Actor.Get();
	OutPoint = GReplayPick.Point;
	OutComp = GReplayPick.Component.Get();
	if (!IsGroundPick(GReplayPick.Kind) && !OutActor)
	{
		OutKind = (uint8)EPick::None;   // цель исчезла за время навыка (умерла/ушла) — клик в пустоту, без хода
	}
	return true;
}

bool AsdaSkillCastLock::GateMapMove(AAsdaPlayerController* PC, const FVector2D& DstGame)
{
	using namespace AsdaSkillCastLockLocal;
	if (!PC || GReplaying || !MoveLockOn()) { return false; }
	const int32 Phase = SelfSkillPhase(PC->GetGameInstance());
	if (Phase == 0) { return false; }
	if (Phase == 24)
	{
		// [ОРИГИНАЛ] шаг автопути = `CGame_HandleGroundClickAttack` → действие 16; [ИСПРАВЛЕНИЕ] маршрут целиком исполняется после
		//   клипа (у оригинала LABEL_111 снимал заявку и персонаж шёл только к первому узлу).
		if (GAction.Kind != EAction::None && GAction.Kind != EAction::MapMove)
		{
			UE_LOG(LogAsdaCastLock, Log, TEXT("[навык занят] прежнее отложенное действие %d вытеснено приказом автопути"), (int32)GAction.Kind);
		}
		ClearAction();
		GAction.Kind = EAction::MapMove;
		GAction.MapDst = DstGame;
		GAction.bTravelStep = FAsdaSkillCastLockAccess::TravelStep(PC);
		FAsdaSkillCastLockAccess::TravelStep(PC) = false;   // признак — на один приказ; повтор взведёт его снова
		GAction.Phase = Phase;
		GAction.SinceS = FPlatformTime::Seconds();
		NoteOwner(PC);
		UE_LOG(LogAsdaCastLock, Log, TEXT("[навык занят] приказ автопути в (%.0f,%.0f) во время клипа навыка — исполнится после клипа"), DstGame.X, DstGame.Y);
		return true;
	}
	if (FAsdaSkillCastLockAccess::TravelStep(PC))
	{
		// Шаг межкартового автопути в 25/14: у эталона заявка снималась бы (LABEL_111) и путь терялся. [ИСПРАВЛЕНИЕ] шаг не теряем —
		//   откладываем, как в 24 (иначе подсистема карты ждёт прихода в клетку шага, которого не было).
		ClearAction();
		GAction.Kind = EAction::MapMove;
		GAction.MapDst = DstGame;
		GAction.bTravelStep = true;
		FAsdaSkillCastLockAccess::TravelStep(PC) = false;
		GAction.Phase = Phase;
		GAction.SinceS = FPlatformTime::Seconds();
		NoteOwner(PC);
		UE_LOG(LogAsdaCastLock, Log, TEXT("[навык занят] шаг автопути в (%.0f,%.0f) в состоянии %d — исполнится после навыка"), DstGame.X, DstGame.Y, Phase);
		return true;
	}
	++GDropped;
	UE_LOG(LogAsdaCastLock, Log, TEXT("[навык занят] приказ автопути в (%.0f,%.0f) в состоянии %d — отброшен (эталон: заявка снимается, LABEL_111)"),
		DstGame.X, DstGame.Y, Phase);
	return true;
}

bool AsdaSkillCastLock::GateTargetCommand(AAsdaPlayerController* PC, int32 Cmd)
{
	using namespace AsdaSkillCastLockLocal;
	if (!PC || GReplaying || !TargetLockOn()) { return false; }
	if (Cmd != 59 && Cmd != 60 && Cmd != 62 && Cmd != 64) { return false; }
	const int32 Phase = SelfSkillPhase(PC->GetGameInstance());
	if (Phase == 0) { return false; }
	if (GAction.Kind != EAction::None)
	{
		UE_LOG(LogAsdaCastLock, Log, TEXT("[навык занят] прежнее отложенное действие %d вытеснено клавишей %d"), (int32)GAction.Kind, Cmd);
	}
	ClearAction();
	GAction.Kind = EAction::Command;
	GAction.Cmd = Cmd;
	GAction.Phase = Phase;
	GAction.SinceS = FPlatformTime::Seconds();
	NoteOwner(PC);
	UE_LOG(LogAsdaCastLock, Log, TEXT("[навык занят] клавиша цели %d в состоянии %d — исполнится после навыка"), Cmd, Phase);
	return true;
}

bool AsdaSkillCastLock::GateItemUse(UAsdaInventorySubsystem* Inv, int32 ContainerId, int32 CellIndex, int32 TargetUserIdx, int32 ItemCode)
{
	using namespace AsdaSkillCastLockLocal;
	if (!Inv || GExecutingItem || !ItemLockOn()) { return false; }
	UGameInstance* GI = Inv->GetGameInstance();
	const int32 Phase = SelfSkillPhase(GI);
	if (Phase == 0) { return false; }
	if (GItem.bSet)
	{
		UE_LOG(LogAsdaCastLock, Log, TEXT("[навык занят] прежний отложенный предмет %d вытеснен предметом %d"), GItem.ItemCode, ItemCode);
	}
	GItem.bSet = true;
	GItem.Inv = Inv;
	GItem.Container = ContainerId; GItem.Cell = CellIndex; GItem.Target = TargetUserIdx; GItem.ItemCode = ItemCode;
	GItem.Phase = Phase;
	GItem.SinceS = FPlatformTime::Seconds();
	const UWorld* W = GI ? GI->GetWorld() : nullptr;
	const AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
	NoteOwner(PC);
	UE_LOG(LogAsdaCastLock, Log, TEXT("[навык занят] предмет %d (контейнер %d, ячейка %d) в состоянии %d — применится после навыка"),
		ItemCode, ContainerId, CellIndex, Phase);
	return true;
}

bool AsdaSkillCastLock::EmoteBlocked(const UGameInstance* GI)
{
	if (CVarAsdaCastLockEmote.GetValueOnGameThread() == 0) { return false; }
	const int32 Phase = SelfSkillPhase(GI);
	if (Phase == 0) { return false; }
	UE_LOG(LogAsdaCastLock, Log, TEXT("[навык занят] эмоция в состоянии %d — не играет (эталон: только состояние 0)"), Phase);
	return true;
}

// ★★★ R5-fix6 (13.11.2026) [ИСПРАВЛЕНИЕ]: ХОД КЛАВИШАМИ ВЫТЕСНЯЕТ ОТЛОЖЕННЫЙ КЛИК ХОДА.
//   Очередь клика (`asda.Input.ClickQueue`, 05.11.2026) — правка порта: у оригинала клик, пришедший под защитой от флуда (прошлый
//   4006 без ответа — бит 2; ход чаще 150 мс, input.c:570), просто пропадает, очереди нет и сверять не с чем. Смысл очереди —
//   «последнее нажатие не теряется»: новый клик вытесняет прежний. Но ход клавишами её не трогал: игрок кликнул по земле, тут же
//   пошёл клавишами и отпустил их — а персонаж после этого убегал в точку старого клика (находка сессии c: `Fish.ZoneSelfTest`,
//   метки карты мерились на бегу). Ход клавишами — намерение новее клика, и старый клик исполняться не должен: так же ход
//   клавишами уже снимает автопуть (`CancelTravelIfManual`) и маршрут.
//   ОБЛАСТЬ: только клик хода, поставленный защитой от флуда (клик по земле либо ход по клику). Действия, отложенные своим
//   навыком, не трогаются. Вызов — на свежем нажатии W или S (`KeyboardEtalonTick`, шаг сообщений клавиатуры).
//   ★ R5-fix8 (14.11.2026) [ИСПРАВЛЕНИЕ] (находка сессии c: «остановка не снимает отложенный клик хода»): то же верно для прочих
//   новых намерений игрока. Игрок дважды кликнул по земле (второй клик лёг в очередь — ход чаще 150 мс либо бит 2) и тут же применил
//   навык, сел, нажал «подобрать» или «атака» — а через мгновение отложенный клик исполнялся и уводил персонажа от только что
//   начатого действия. У оригинала такой клик пропал бы (очереди нет), то есть нового действия он не срывал. Теперь навык,
//   прошедший гейты (`UAsdaSkillUseSubsystem`), и клавиши 55 «сесть/встать», 56 «подобрать», 59 «атака» (`ExecuteCommand`)
//   вытесняют отложенный клик хода. Сама остановка персонажа (`StopMove`) очередь НЕ трогает намеренно: её зовут и приход ответа
//   сервера, и конец отрезка пути — это не намерение игрока, и клик, ждущий ответа 4007, после неё обязан исполниться.
//   Ключ `asda.Input.ClickQueueSupersede`: 2 = ход клавишами и прочие намерения; 1 = только ход клавишами (как в R5-fix6);
//   0 = как было (старый клик исполнится позже).
static ASDA_DEV_CVAR(int32, CVarAsdaClickQueueSupersede,
	TEXT("asda.Input.ClickQueueSupersede"), 2,
	TEXT("[ИСПРАВЛЕНИЕ] 2 (по умолчанию) = новое намерение игрока (ход клавишами, навык, сесть/встать, подобрать, атака клавишей) снимает ")
	TEXT("отложенный очередью клик хода (asda.Input.ClickQueue): старый клик позже не исполняется. 1 = только ход клавишами. ")
	TEXT("0 = как было: отложенный клик исполнится, когда откроется защита от флуда."),
	ECVF_Default);

void AsdaSkillCastLock::SupersedeQueuedMove(const TCHAR* Why, int32 MinKey)
{
	using namespace AsdaSkillCastLockLocal;
	if (CVarAsdaClickQueueSupersede.GetValueOnGameThread() < FMath::Max(MinKey, 1)) { return; }
	if (GAction.Kind == EAction::None || !GAction.bFlood) { return; }
	const bool bMove = GAction.Kind == EAction::GroundMove || (GAction.Kind == EAction::Click && IsGroundPick(GAction.Pick.Kind));
	if (!bMove) { return; }
	UE_LOG(LogAsdaCastLock, Log, TEXT("[очередь клика] отложенный клик хода снят без исполнения: новое намерение — %s"), Why ? Why : TEXT("?"));
	++GDropped;
	ClearAction();
}

void AsdaSkillCastLock::Reset(const TCHAR* Why)
{
	using namespace AsdaSkillCastLockLocal;
	if (GAction.Kind == EAction::None && !GItem.bSet) { return; }
	UE_LOG(LogAsdaCastLock, Log, TEXT("[навык занят] отложенное сброшено без исполнения (%s): действие %d, предмет %d"),
		Why ? Why : TEXT("?"), (int32)GAction.Kind, GItem.bSet ? GItem.ItemCode : 0);
	if (GAction.Kind != EAction::None) { ++GDropped; }
	if (GItem.bSet) { ++GDropped; }
	ClearAction();
	ClearItem();
}

void AsdaSkillCastLock::Tick(AAsdaPlayerController* PC)
{
	using namespace AsdaSkillCastLockLocal;
	if (!PC || (GAction.Kind == EAction::None && !GItem.bSet)) { return; }
	UGameInstance* GI = PC->GetGameInstance();
	// Слоты поставил другой персонаж (смена карты: новый павн; выход на выбор персонажа) — сброс.
	if (!GOwnerPawn.IsValid() || GOwnerPawn.Get() != PC->GetPawn()) { Reset(TEXT("смена карты или персонажа")); return; }
	if (PC->IsSelfDead()) { Reset(TEXT("смерть своего персонажа")); return; }
	const double Now = FPlatformTime::Seconds();
	const int32 Phase = SelfSkillPhase(GI);
	if (Phase != 0)
	{
		const double Oldest = FMath::Min(GAction.Kind != EAction::None ? GAction.SinceS : Now, GItem.bSet ? GItem.SinceS : Now);
		if (Now - Oldest > kStaleS) { Reset(*FString::Printf(TEXT("навык (состояние %d) не окончился за %.0f с"), Phase, kStaleS)); }
		return;
	}
	// Навык окончен (конец клипа, отказ сервера, конец каста): сначала предмет, затем действие — ввод не теряется.
	if (GItem.bSet)
	{
		const FItemSlot It = GItem;
		ClearItem();
		UAsdaInventorySubsystem* Inv = It.Inv.Get();
		const int32 NowCode = Inv ? Inv->GetCellItemCode(It.Container, It.Cell) : 0;
		if (!Inv || NowCode != It.ItemCode)
		{
			++GDropped;
			UE_LOG(LogAsdaCastLock, Log, TEXT("[навык занят] отложенный предмет %d не применён: ячейка (%d,%d) теперь %d"), It.ItemCode, It.Container, It.Cell, NowCode);
		}
		else
		{
			TGuardValue<bool> Guard(GExecutingItem, true);
			const bool bOk = Inv->UseConsumable(It.Container, It.Cell, It.Target);
			++GExecuted;
			UE_LOG(LogAsdaCastLock, Log, TEXT("[навык занят] навык окончен — отложенный предмет %d применён: %s"), It.ItemCode, bOk ? TEXT("5028 ушёл") : TEXT("отказ гейтов"));
		}
	}
	if (GAction.Kind == EAction::None) { return; }
	// ★ Очередь клика (`asda.Input.ClickQueue`): клик и ход по клику ждут, пока закрыта защита от флуда оригинала — ответ 4007 на прошлый
	//   4006 (бит 2) и троттл 150 мс. Отложенные навыком клики ждут того же (у эталона их исполнение тоже упёрлось бы в эти гейты).
	if (ClickQueueOn() && (GAction.Kind == EAction::Click || GAction.Kind == EAction::GroundMove))
	{
		UAsdaNetSubsystem* QNet = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		const bool bInFlight = QNet && QNet->IsMoveRequestInFlight();
		const bool bThrottle = Now - FAsdaSkillCastLockAccess::LastMoveSend(PC) < kMoveThrottleS;
		if (bInFlight || bThrottle)
		{
			if (GAction.bFlood && Now - GAction.SinceS > kFloodStaleS) { Reset(TEXT("клик ждал ответа 4007 дольше 3 с")); }
			return;
		}
	}
	const FActionSlot A = GAction;
	ClearAction();
	const bool bMove = A.Kind == EAction::MapMove || A.Kind == EAction::GroundMove || (A.Kind == EAction::Click && IsGroundPick(A.Pick.Kind));
	if (bMove && FAsdaSkillCastLockAccess::MoveLocked(PC))
	{
		++GDropped;   // обездвижен после навыка — ход не исполняется (молча; иначе старый клик дал бы плашку 1019)
		UE_LOG(LogAsdaCastLock, Log, TEXT("[навык занят] отложенный ход снят: персонаж обездвижен состоянием"));
		return;
	}
	++GExecuted;
	switch (A.Kind)
	{
	case EAction::Click:
		ReplayClick(PC, A);
		break;
	case EAction::GroundMove:
		UE_LOG(LogAsdaCastLock, Log, TEXT("[очередь клика] троттл окончен — ход по клику в (%.0f,%.0f)"), A.MovePointUe.X, A.MovePointUe.Y);
		FAsdaSkillCastLockAccess::GroundMove(PC, A.MovePointUe);
		break;
	case EAction::MapMove:
		UE_LOG(LogAsdaCastLock, Log, TEXT("[навык занят] навык окончен — исполняется отложенный приказ автопути в (%.0f,%.0f)"), A.MapDst.X, A.MapDst.Y);
		if (A.bTravelStep) { PC->BeginTravelStep(); }   // шаг межкартового пути остаётся шагом (не «воля игрока»)
		PC->MoveToGamePoint(A.MapDst);
		break;
	case EAction::Command:
		UE_LOG(LogAsdaCastLock, Log, TEXT("[навык занят] навык окончен — исполняется отложенная клавиша %d"), A.Cmd);
		FAsdaSkillCastLockAccess::Command(PC, A.Cmd);
		break;
	default:
		break;
	}
}

int32 AsdaSkillCastLock::DebugPendingActionKind() { return (int32)AsdaSkillCastLockLocal::GAction.Kind; }
bool AsdaSkillCastLock::DebugHasPendingItem() { return AsdaSkillCastLockLocal::GItem.bSet; }
int32 AsdaSkillCastLock::DebugExecutedCount() { return AsdaSkillCastLockLocal::GExecuted; }
int32 AsdaSkillCastLock::DebugDroppedCount() { return AsdaSkillCastLockLocal::GDropped; }

void AsdaSkillCastLock::DebugWorldClick(AAsdaPlayerController* PC, const FVector2D& Screen, bool bDouble)
{
	if (!PC) { return; }
	FAsdaSkillCastLockAccess::Screen(PC) = Screen;
	FAsdaSkillCastLockAccess::Click(PC, bDouble);
	FAsdaSkillCastLockAccess::Screen(PC).Reset();
}

void AsdaSkillCastLock::DebugSetClickPoint(AAsdaPlayerController* PC, const FVector2D* Screen)
{
	if (!PC) { return; }
	if (Screen) { FAsdaSkillCastLockAccess::Screen(PC) = *Screen; }
	else { FAsdaSkillCastLockAccess::Screen(PC).Reset(); }
}

void AsdaSkillCastLock::DebugExecuteCommand(AAsdaPlayerController* PC, int32 Cmd)
{
	if (PC) { FAsdaSkillCastLockAccess::Command(PC, Cmd); }
}

int32 AsdaSkillCastLock::DebugActionState(const AAsdaPlayerController* PC)
{
	return PC ? FAsdaSkillCastLockAccess::ActionState(PC) : 0;
}

uint8 AsdaSkillCastLock::DebugPickPointAt(AAsdaPlayerController* PC, const FVector2D& Screen, FVector& OutUe)
{
	OutUe = FVector::ZeroVector;
	if (!PC) { return 0; }
	FAsdaSkillCastLockAccess::Screen(PC) = Screen;
	const AAsdaPlayerController::FAsdaPick P = FAsdaSkillCastLockAccess::Pick(PC);
	FAsdaSkillCastLockAccess::Screen(PC).Reset();
	OutUe = P.Point;
	return (uint8)P.Kind;
}

uint8 AsdaSkillCastLock::DebugPickKindAt(AAsdaPlayerController* PC, const FVector2D& Screen, AActor*& OutActor)
{
	OutActor = nullptr;
	if (!PC) { return 0; }
	FAsdaSkillCastLockAccess::Screen(PC) = Screen;
	const AAsdaPlayerController::FAsdaPick P = FAsdaSkillCastLockAccess::Pick(PC);
	FAsdaSkillCastLockAccess::Screen(PC).Reset();
	OutActor = P.Actor.Get();
	return (uint8)P.Kind;
}

bool AsdaSkillCastLock::DebugRouteFinal(const AAsdaPlayerController* PC, FVector2D& OutGame)
{
	return PC && FAsdaSkillCastLockAccess::RouteFinal(PC, OutGame);
}
