// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «АВТОПУТЬ ВО ВРЕМЯ АТАКИ» (26.10.2026, волна navattack). [ИСПРАВЛЕНИЕ] `asda.Move.AutoPathExitsCombat`
//
// ★ ЗАЧЕМ. Баг владельца: игрок бьёт моба и жмёт в журнале заданий кнопку маршрута («=> Моб — Карта, x, y»): персонаж идёт к новой
//   цели, но остаётся в клипе атаки и продолжает бить прежнего моба. Разбор (эталон, причина, исправление) — у
//   `AsdaExitCombatForAutoPath` (World/AsdaPlayerController_Move.cpp). Самотест путём игрока (бой кликом по мобу, исходящие — в
//   перехватчик, ответ сервера — `DebugFeedPacket` тем же разбором, что у сокета) проверяет:
//   • кнопка маршрута журнала 705 (своя карта; `OnClicked` кнопки → реле → `TravelToRoute`) посреди серии: автоатака и серия
//     сняты, клип замаха снят, ход — клипом бега, 4006 с целью −1, ни 4028, ни 4026, ни подхода 4006 с rid прежнего моба; цель
//     остаётся выбранной; показаны только применённые сервером значения (серия брошена — правка 25.10 «фантомный урон»);
//   • то же в седле (сидячий клип хода транспорта) и с крыльями (клип 35 «полёт»);
//   • клик по мини-карте (`HandleMapClick`, своя карта);
//   • кнопка маршрута на ЧУЖУЮ карту (межкартовый автопуть `BeginMapTravel` → шаг к порталу);
//   • «Следовать» посреди атаки (`StartFollow` → шаг следования);
//   • [ОРИГИНАЛ, LABEL_111] атака, начатая игроком при идущем следовании / межкартовом автопути, не ломается: следование ждёт,
//     шаг автопути снимает заявку;
//   • контроль: ключ 0 — дефект виден (замах жив / 4028 по прежней цели / клип не бега).
//
// Консоль: `asda.Combat.SelfTestNavAttack [delay=N] [quit]`. Только офлайн. Строки — «САМОТЕСТ-НАВАТАКА», итог — «… ИТОГ».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"                         // запись полей тел пакетов
#include "AsdaCVarSave.h"                      // ключи на время прогона (вернутся в итоге)
#include "AsdaOutgoingGate.h"                  // FAsdaSelfTestRunGate — офлайн повторно на отложенном старте
#include "AsdaTestPackets.h"                   // запись 4013
#include "Test/AsdaSelfTestKit.h"              // каркас: аргументы, отказ, перехват, итог, выход
#include "World/AsdaPlayerController.h"        // бой кликом
#include "World/AsdaPlayerCharacter.h"         // свой персонаж: замах, клип, седло, крылья
#include "World/AsdaWorldSubsystem.h"          // показ цифр, транспорт, сосед-заглушка
#include "World/AsdaAttackRhythmSubsystem.h"   // серверные часы атаки (сброс между сценариями)
#include "World/AsdaLoadManagerSubsystem.h"    // номер текущей карты
#include "World/AsdaCoord.h"                   // игра ↔ UE, клетки
#include "World/AsdaWalkGrid.h"                // проходимость клетки цели
#include "World/AsdaMapRegistry.h"             // число карт (поиск карты с переходом)
#include "Items/AsdaItemTableSubsystem.h"      // оружие подтипа
#include "Npc/AsdaMobActor.h"                  // моб
#include "Quest/AsdaQuestLogSubsystem.h"       // журнал 700: маршруты и кнопки 705…708 (friend)
#include "Quest/AsdaQuestMarkerSubsystem.h"    // NextPortalStep — есть ли путь на чужую карту
#include "UI/AsdaMinimapSubsystem.h"           // клик по мини-карте, заявка автопути (friend)
#include "UI/AsdaPlayerMenuSubsystem.h"        // «Следовать»
#include "Login/AsdaButtonRelay.h"             // реле кнопок маршрута
#include "Components/Button.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimationAsset.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaNavAttackTest, Log, All);

int32 AsdaOwnPhantomDroppedCount();   // `AsdaWorldSubsystem_Hit.cpp`: сколько своих неприменённых значений не показано

namespace AsdaNavAttackTestLocal
{
	constexpr int32 kMobType = 1;         // тип моба (волк alpen) — как у `asda.Combat.SelfTestPhantom`
	constexpr int32 kSpdDenom = 250;      // знаменатель скорости (бег сервера)
	constexpr int32 kMobHp = 100;         // HP моба из записи 4013
	constexpr int32 kPeerNum = 7731;      // userNum соседа-заглушки (следование)
	constexpr double kObserveS = 2.5;     // окно наблюдения после приказа (с)
	constexpr double kSampleS = 0.3;      // момент снимка состояния после приказа (с)
	const TCHAR* const kCmd = TEXT("asda.Combat.SelfTestNavAttack");   // имя команды

	// Вид сценария.
	enum class EKind : uint8
	{
		JournalSame,        // кнопка маршрута журнала 705, цель на своей карте
		JournalMount,       // то же в седле (посадка посреди атаки)
		JournalWings,       // то же с крыльями (клип 35 «полёт»)
		Minimap,            // клик по мини-карте
		JournalOther,       // кнопка маршрута журнала на чужую карту (межкартовый автопуть)
		FollowStart,        // «Следовать» посреди атаки
		FollowThenAttack,   // [ОРИГИНАЛ] следование идёт, игрок начал атаку — следование ждёт
		TravelThenAttack,   // [ОРИГИНАЛ] заявка автопути на чужую карту, игрок начал атаку — шаг снимает заявку
		LootFar,            // ★ navattack2: клик по луту в чужой клетке посреди атаки (StartLootPickup — тело ветки Loot клика)
		LootOwnCell,        // ★ navattack2 [ОРИГИНАЛ]: клик по луту в своей клетке посреди атаки — 5012 сразу, атака идёт
	};

	struct FSent { uint16 Op = 0; int32 Tgt = -2; double At = 0.0; };   // исходящий пакет (4006: цель @+25)

	struct FStep
	{
		FString Name;                  // имя шага
		float TimeoutSec = 10.f;       // потолок шага (с)
		TFunction<void()> Start;       // начало шага
		TFunction<bool()> Poll;        // опрос: true — шаг завершён
		TFunction<void()> Check;       // проверки после шага
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UAsdaNetSubsystem> Net;
		TArray<FStep> Steps;
		int32 StepIdx = 0;
		bool bStepStarted = false;
		double StepStartS = 0.0;
		FTimerHandle Tick;
		bool bQuit = false;
		FAsdaSelfTestRunGate Gate{ kCmd, false };
		int32 Ok = 0, Fail = 0;
		uint32 Gen = 0;                     // перехват исходящих
		TArray<FSent> Sent;                 // исходящие за сценарий
		FVector2D Home = FVector2D::ZeroVector;
		FIntPoint HomeTile = FIntPoint::ZeroValue;
		int32 SavedWeapon = 0, SavedEquipped = 99;
		bool bWasInGame = false;
		bool bJournalOpened = false;        // журнал открыт самотестом (закрыть в итоге)
		TUniquePtr<FAsdaNetSelfScope> SelfScope;
		TArray<int32> Rids;                 // мобы прогона
		int32 NextRid = 31981;
		// Сценарий.
		int32 Rid = -1;
		int32 Phase = 0;
		double PhaseS = 0.0;
		int32 SeenFrom = 0;                 // начало наблюдений `HitResultsSeen`
		int32 NavFrom = 0;                  // индекс первого исходящего после приказа
		double NavS = 0.0;                  // момент приказа
		float DistAtNav = 0.f, DistEnd = 0.f, DistMin = 1e9f;   // до прежнего моба (UE)
		bool bSampled = false;              // снимок состояния снят
		bool bSAuto = false, bSSeries = false, bSLoop = false, bSAnim = false, bSMoving = false, bSMounted = false;
		EAsdaPlayerState SAnimState = EAsdaPlayerState::None;
		FString SClip, SMountClip;          // клип своего персонажа / транспорта на снимке
		int32 OtherMap = -1;                // карта с переходом (межкартовый сценарий)
		bool bTravelAtSample = false;       // заявка автопути жива на снимке
		bool bApplicable = true;            // сценарий применим к данным
		bool bFed5 = false;                 // [ОРИГИНАЛ]-сценарий: ответ 4029 на 4026 уже дан

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
		AAsdaMobActor* Mob(int32 R) const { const UAsdaWorldSubsystem* W = Sub<UAsdaWorldSubsystem>(); return W ? W->FindNetMobByRid(R) : nullptr; }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaNavAttackTest, Log, TEXT("САМОТЕСТ-НАВАТАКА %s %s: %s"), bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		int32 NewRid() { const int32 R = NextRid++; Rids.Add(R); return R; }
		void SetPhase(int32 P) { Phase = P; PhaseS = FPlatformTime::Seconds(); }
		double InPhase() const { return FPlatformTime::Seconds() - PhaseS; }
		// Исходящие после приказа: число пакетов `Op`; для 4006 — с целью −1 (`bFree`) или с целью ≥ 0.
		int32 CountAfter(uint16 Op, int32 TgtMode = 0) const   // TgtMode: 0 — все, 1 — цель −1, 2 — цель ≥ 0
		{
			int32 C = 0;
			for (int32 i = NavFrom; i < Sent.Num(); ++i)
			{
				if (Sent[i].Op != Op) { continue; }
				if (TgtMode == 1 && Sent[i].Tgt != -1) { continue; }
				if (TgtMode == 2 && Sent[i].Tgt < 0) { continue; }
				++C;
			}
			return C;
		}
		int32 CountAll(uint16 Op) const { int32 C = 0; for (const FSent& E : Sent) { if (E.Op == Op) { ++C; } } return C; }
		// Значения, показанные по цели сценария своим ударом.
		TArray<int32> Shown() const
		{
			TArray<int32> Out;
			if (const UAsdaWorldSubsystem* W = Sub<UAsdaWorldSubsystem>())
			{
				for (int32 i = SeenFrom; i < W->HitResultsSeen.Num(); ++i)
				{
					const UAsdaWorldSubsystem::FAsdaHitResultSeen& E = W->HitResultsSeen[i];
					if (E.Rid == Rid && E.bOurs && !E.bOnCorpse) { Out.Add(E.Raw); }
				}
			}
			return Out;
		}
	};

	// Тело 4029 (49 б, `Recv_CastState@0xF41620`): @0 i16 атакующий, @2 i16 rid, @4 i32 HP цели, @8 i32[4] очередь, @25 i16[4] (−1).
	using AsdaTestPackets::Body4029;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPackets.h`) — копия была дословной.

	// Первое оружие подтипа (тип 9, не удочка) с дальностью. 0 — нет.
	static int32 FindWeapon(const UAsdaItemTableSubsystem* IT, int32 Wt)
	{
		if (!IT) { return 0; }
		for (int32 Code = 20000; Code < 80000; ++Code)
		{
			const FAsdaItemRecord* R = IT->GetItem(Code);
			if (R && R->ItemType == 9 && R->ItemSubType == Wt && R->ItemVariant != 1 && R->AttackRangeTiles > 0) { return Code; }
		}
		return 0;
	}

	static FString Join(const TArray<int32>& A)
	{
		FString S;
		for (int32 V : A) { S += FString::Printf(TEXT("%s%d"), S.IsEmpty() ? TEXT("") : TEXT(" "), V); }
		return TEXT("[") + S + TEXT("]");
	}

	// Имя ассета, который сейчас играет компонент (одиночный инстанс; у своего — инстанс со смешиванием, его наследник).
	static FString ClipName(const USkeletalMeshComponent* C)
	{
		const UAnimSingleNodeInstance* I = C ? C->GetSingleNodeInstance() : nullptr;
		const UAnimationAsset* A = I ? I->GetAnimationAsset() : nullptr;
		return A ? A->GetName() : FString(TEXT("(нет)"));
	}

	static const TCHAR* KindName(EKind K)
	{
		switch (K)
		{
		case EKind::JournalSame:      return TEXT("бой → кнопка маршрута журнала 705 (своя карта)");
		case EKind::JournalMount:     return TEXT("бой → посадка → кнопка маршрута журнала 705 (в седле)");
		case EKind::JournalWings:     return TEXT("бой → крылья → кнопка маршрута журнала 705 (полёт)");
		case EKind::Minimap:          return TEXT("бой → клик по мини-карте");
		case EKind::JournalOther:     return TEXT("бой → кнопка маршрута журнала на чужую карту (межкартовый автопуть)");
		case EKind::FollowStart:      return TEXT("бой → «Следовать» за соседом");
		case EKind::FollowThenAttack: return TEXT("следование → игрок начал атаку (следование ждёт)");
		case EKind::TravelThenAttack: return TEXT("межкартовый автопуть → игрок начал атаку (шаг снимает заявку)");
		case EKind::LootFar:          return TEXT("бой → клик по луту в чужой клетке");
		case EKind::LootOwnCell:      return TEXT("бой → клик по луту в своей клетке (5012 сразу)");
		}
		return TEXT("?");
	}
}

struct FAsdaNavAttackSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W);
	// Нажать кнопку маршрута 705 журнала с одним маршрутом (карта, клетка): `OnClicked` кнопки → реле → `TravelToRoute`.
	static FString PressJournalRoute(UAsdaQuestLogSubsystem* QL, int32 MapId, const FIntPoint& Tile);
	// Клик по мини-карте в клетку `Tile` (обратный пересчёт клетки в пиксели мини-карты — та же арифметика, что в `HandleMapClick`).
	static bool ClickMinimap(UAsdaMinimapSubsystem* MM, const FIntPoint& Tile);
};

FString FAsdaNavAttackSelfTest::PressJournalRoute(UAsdaQuestLogSubsystem* QL, int32 MapId, const FIntPoint& Tile)
{
	if (!QL) { return TEXT("нет журнала"); }
	FAsdaQuestRoute R; R.Text = TEXT("=> Самотест — карта, x, y"); R.MapId = MapId; R.X = Tile.X; R.Y = Tile.Y;
	QL->Routes = { R };
	if (QL->RouteBtns.IsValidIndex(0) && QL->RouteBtns[0])
	{
		QL->RouteBtns[0]->OnClicked.Broadcast();   // клик игрока: кнопка → реле → обработчик
		return TEXT("кнопка 705 (OnClicked)");
	}
	if (QL->Relays.IsValidIndex(0) && QL->Relays[0] && QL->Relays[0]->Callback)
	{
		QL->Relays[0]->Callback(0);
		return TEXT("реле кнопки 705");
	}
	QL->TravelToRoute(0);
	return TEXT("обработчик TravelToRoute (кнопок нет)");
}

bool FAsdaNavAttackSelfTest::ClickMinimap(UAsdaMinimapSubsystem* MM, const FIntPoint& Tile)
{
	if (!MM) { return false; }
	AAsdaPlayerCharacter* P = nullptr;
	const UAsdaWalkGrid* G = MM->GetGrid(P);
	if (!P || !G) { return false; }
	const FVector2D Pos = P->GetGamePosXZ();
	const float Scale = MM->WorldToTexelScale(G);
	const FVector2D Scroll = MM->ScrollTexels(FVector2D(Pos.X * Scale, Pos.Y * Scale));
	const FVector2D TileC((double)Tile.X + 0.5, (double)Tile.Y + 0.5);
	const FVector2D LocalPx = (TileC * (Scale * 100.f) - Scroll) * MM->MarkPxPerTexel();
	MM->LastClickTime = 0.0;   // троттл 250 мс прошлого клика
	MM->HandleMapClick(LocalPx, /*bBig=*/false);
	return true;
}

void FAsdaNavAttackSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaNavAttackTestLocal;
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 4026, 4028 };
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { UE_LOG(LogAsdaNavAttackTest, Warning, TEXT("САМОТЕСТ-НАВАТАКА: нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate.bQuit = KitArgs.bQuit;

	auto Add = [S](const FString& Name, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.TimeoutSec = Timeout;
		St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};
	auto Feed = [S](int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = S->N()) { Nn->DebugFeedPacket(Op, B); } };
	// Сброс боя, хода, седла, крыльев, следования и автопути (сценарии не зависят друг от друга).
	auto Reset = [S, Feed]()
	{
		AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
		if (UAsdaPlayerMenuSubsystem* PM = S->Sub<UAsdaPlayerMenuSubsystem>()) { if (PM->IsFollowing()) { PM->CancelFollow(TEXT("самотест: сброс")); } }
		if (UAsdaMinimapSubsystem* MM = S->Sub<UAsdaMinimapSubsystem>()) { if (MM->IsTravelRequested()) { MM->CancelTravelRequest(); } MM->SetDestinationTile(0, 0, false); }
		if (PC)
		{
			PC->bAutoAttack = false; PC->bCombatStarted = false; PC->bInCombatStance = false; PC->bWaitingCombatAck = false;
			PC->bPvpAttack = false; PC->bArmImpactOnSwingStart = false;
			PC->NextAttackTickTime = 0.0; PC->TargetActor = nullptr;
			PC->GetWorldTimerManager().ClearTimer(PC->DeferredAttackSendTimer);
			PC->CancelRoute();
		}
		if (UAsdaNetSubsystem* Nn = S->N()) { Nn->ClearMoveRequestInFlight(TEXT("самотест наватаки: сброс")); Nn->CancelPending4028(TEXT("самотест наватаки: сброс")); Nn->bItemPickPending = false; }   // 5012 без ответа (офлайн)
		if (UAsdaAttackRhythmSubsystem* R = S->Sub<UAsdaAttackRhythmSubsystem>()) { R->DebugReset(); }
		if (C)
		{
			C->StopAttackAnim(); C->AttackFaceTarget = nullptr;
			if (C->IsMounted()) { C->ClearMount(); }
			if (C->bWingsOn) { C->SetWingsOn(false); }
			C->StopMove(); C->NetSnap(S->Home);
			C->SetCombatStance(false);
		}
		if (PC) { PC->ExitCombatStance(); }
		for (const int32 R : S->Rids) { if (AAsdaMobActor* M = S->Mob(R)) { M->Destroy(); } }
		S->Rids.Reset();
		if (const UAsdaWorldSubsystem* WSs = S->Sub<UAsdaWorldSubsystem>())
		{
			if (WSs->GetPlayerActorByUserNum(kPeerNum)) { TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], (int16)kPeerNum); Feed(4011, B); }
		}
		S->Rid = -1; S->SetPhase(0); S->Sent.Reset(); S->NavFrom = 0; S->NavS = 0.0;
		S->DistAtNav = 0.f; S->DistEnd = 0.f; S->DistMin = 1e9f; S->bSampled = false; S->bTravelAtSample = false; S->bApplicable = true; S->bFed5 = false;
		S->SClip.Reset(); S->SMountClip.Reset(); S->SAnimState = EAsdaPlayerState::None;
		S->SeenFrom = S->Sub<UAsdaWorldSubsystem>() ? S->Sub<UAsdaWorldSubsystem>()->HitResultsSeen.Num() : 0;
	};
	// Проходимая клетка в стороне `Dir` от дома на расстоянии `Len` клеток (ближайшая проходимая по лучу, затем вбок).
	auto FreeTile = [S](const FIntPoint& Dir, int32 Len) -> FIntPoint
	{
		AAsdaPlayerCharacter* C = S->Ch();
		const UAsdaWalkGrid* G = C ? C->WalkGrid.Get() : nullptr;
		for (int32 L = Len; L >= 3; --L)
		{
			for (int32 Side = 0; Side <= 3; ++Side)
			{
				for (int32 Sg = -1; Sg <= 1; Sg += 2)
				{
					const FIntPoint T = S->HomeTile + Dir * L + FIntPoint(Dir.Y, Dir.X) * (Side * Sg);
					if (!G || G->IsTileWalkable(T.X, T.Y)) { return T; }
				}
			}
		}
		return S->HomeTile + Dir * Len;
	};
	// Снимок состояния своего персонажа после приказа.
	auto Sample = [S]()
	{
		AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
		S->bSampled = true;
		S->bSAuto = PC && PC->bAutoAttack; S->bSSeries = PC && PC->bCombatStarted;
		S->bSLoop = C && C->IsAttackLoopAlive(); S->bSAnim = C && C->IsAttackAnimActive();
		S->bSMoving = C && C->IsMoving(); S->bSMounted = C && C->IsMounted();
		S->SAnimState = C ? C->AnimState : EAsdaPlayerState::None;
		S->SClip = C ? ClipName(C->BaseSkel) : FString();
		S->SMountClip = (C && C->MountMesh) ? ClipName(C->MountMesh) : FString();
		const UAsdaMinimapSubsystem* MM = S->Sub<UAsdaMinimapSubsystem>();
		S->bTravelAtSample = MM && MM->IsTravelRequested();
		UE_LOG(LogAsdaNavAttackTest, Log, TEXT("САМОТЕСТ-НАВАТАКА снимок +%.1f с: автоатака=%d серия=%d цикл замаха=%d замах=%d идёт=%d седло=%d состояние=%d клип=%s транспорт=%s автопуть=%d"),
			kSampleS, S->bSAuto, S->bSSeries, S->bSLoop, S->bSAnim, S->bSMoving, S->bSMounted, (int32)S->SAnimState, *S->SClip, *S->SMountClip, S->bTravelAtSample);
	};
	auto MobDist = [S]() -> float
	{
		AAsdaPlayerCharacter* C = S->Ch(); AAsdaMobActor* M = S->Mob(S->Rid);
		return (C && M) ? (float)FVector::Dist2D(C->GetActorLocation(), M->GetActorLocation()) : -1.f;
	};

	// ─────────────────────── Подготовка ───────────────────────
	Add(TEXT("подготовка"), 2.f, [S, Net]()
	{
		AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerController* PC = S->PC();
		if (!C || !PC) { S->Expect(false, TEXT("нет своего персонажа/контроллера после FakeSelfSpawn")); return; }
		S->Home = AsdaCoord::TileCenterGame(AsdaCoord::GameToTile(C->GetGamePosXZ()));
		S->HomeTile = AsdaCoord::GameToTile(S->Home);
		C->NetSnap(S->Home);
		S->SavedWeapon = Net->SelfWeaponItemId;
		S->SavedEquipped = C->EquippedWeaponSubType;
		S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = 4321; Net->bSelfUserNumKnown = true; }
		Net->bSelfDead = false;
		TWeakPtr<FState> WSp = S;
		S->Gen = AsdaSelfTestKit::ArmCapture(Net, [WSp](uint16 Op, const uint8* Body, int32 Size, bool, TCHAR)
		{
			TSharedPtr<FState> St = WSp.Pin();
			if (!St) { return; }
			FSent E; E.Op = Op; E.At = FPlatformTime::Seconds();
			if (Op == 4006 && Body && Size >= 43) { E.Tgt = (int32)AsdaBytes::RdI16(Body + 16 + 25); }   // MD5 16 + тело: цель @+25
			St->Sent.Add(E);
		}, (uint16)0, kCmd);
		S->Expect(S->Gen != 0, TEXT("перехват исходящих взведён"));
		S->bWasInGame = Net->bInGameConnected;
		Net->bInGameConnected = true;
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.ServerRhythm"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.RhythmFromAnim"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.OwnHitsServerCommit"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.RestartSeriesGate"), 1);
		if (UAsdaWorldSubsystem* WSs = S->Sub<UAsdaWorldSubsystem>()) { WSs->bRecordHitResults = true; }
		if (UAsdaQuestLogSubsystem* QL = S->Sub<UAsdaQuestLogSubsystem>())
		{
			if (!QL->IsOpen()) { QL->Open(); S->bJournalOpened = QL->IsOpen(); }
			S->Expect(QL->IsOpen() && QL->RouteBtns.IsValidIndex(0) && QL->RouteBtns[0] != nullptr,
				FString::Printf(TEXT("журнал 700 открыт, кнопка маршрута 705 есть (кнопок %d)"), QL->RouteBtns.Num()));
		}
		// Карта с путём через порталы (межкартовый сценарий).
		const UAsdaLoadManagerSubsystem* LM = S->Sub<UAsdaLoadManagerSubsystem>();
		const int32 Cur = LM ? LM->CurrentMapId : -1;
		for (int32 B = 0; B < AsdaMapRegistry::kMapCount && Cur >= 0 && S->OtherMap < 0; ++B)
		{
			int32 Px = 0, Py = 0;
			if (B != Cur && UAsdaQuestMarkerSubsystem::NextPortalStep(Cur, B, Px, Py, C->WalkGrid.Get())) { S->OtherMap = B; }
		}
		UE_LOG(LogAsdaNavAttackTest, Log, TEXT("САМОТЕСТ-НАВАТАКА дом: клетка (%d,%d), карта %d, сетка проходимости %s, чужая карта с переходом %d"),
			S->HomeTile.X, S->HomeTile.Y, Cur, C->WalkGrid ? TEXT("есть") : TEXT("НЕТ"), S->OtherMap);
	}, []() { return true; }, []() {});

	// ─────────────────────── Сценарии ───────────────────────
	auto AddCase = [S, Add, Feed, Reset, FreeTile, Sample, MobDist](EKind Kind, int32 Key)
	{
		const bool bAttackFirst = Kind != EKind::FollowThenAttack && Kind != EKind::TravelThenAttack;   // бой до приказа
		Add(FString::Printf(TEXT("%s, ключ %d%s"), KindName(Kind), Key, Key == 0 ? TEXT(" (контроль)") : TEXT("")), 16.f,
		[S, Feed, Reset, Key, Kind]()
		{
			Reset();
			AsdaCVarSave::SetForTest(TEXT("asda.Move.AutoPathExitsCombat"), Key);
			UGameInstance* G = S->GI(); AAsdaPlayerCharacter* C = S->Ch(); UAsdaNetSubsystem* Nn = S->N();
			if (!G || !C || !Nn) { return; }
			Nn->SelfWeaponItemId = FindWeapon(G->GetSubsystem<UAsdaItemTableSubsystem>(), 1);   // одноручный меч
			C->EquippedWeaponSubType = 1;
			S->Expect(Nn->SelfWeaponItemId > 0, FString::Printf(TEXT("оружие подтипа 1: предмет %d"), Nn->SelfWeaponItemId));
			if ((Kind == EKind::JournalOther || Kind == EKind::TravelThenAttack) && S->OtherMap < 0) { S->bApplicable = false; S->SetPhase(99); return; }
			S->Rid = S->NewRid();
			Feed(4013, AsdaTestPackets::MobSpawn4013(S->Rid, kMobType, S->HomeTile + FIntPoint(1, 0), /*State*/1, kSpdDenom));
		},
		[S, Feed, FreeTile, Sample, MobDist, Kind, bAttackFirst]()
		{
			if (S->Phase == 99) { return true; }
			AAsdaMobActor* M = S->Mob(S->Rid); AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
			const UAsdaNetSubsystem* Nn = S->N();
			const int32 Self = Nn ? (int32)Nn->SelfUserNum : 0;
			const UAsdaLoadManagerSubsystem* LM = S->Sub<UAsdaLoadManagerSubsystem>();
			const int32 CurMap = LM ? LM->CurrentMapId : -1;
			auto Engage = [PC, M]() { PC->SetTarget(M); PC->EngageMobFromClick(M); };
			// Приказ автопути (момент приказа и граница исходящих — до него).
			auto Order = [S, CurMap, FreeTile, MobDist](EKind K)
			{
				S->NavFrom = S->Sent.Num(); S->NavS = FPlatformTime::Seconds(); S->DistAtNav = MobDist();
				const FIntPoint Away = FreeTile(FIntPoint(-1, 0), 8);   // от моба (он на +1 по X)
				FString How;
				switch (K)
				{
				case EKind::JournalSame: case EKind::JournalMount: case EKind::JournalWings:
					How = FAsdaNavAttackSelfTest::PressJournalRoute(S->Sub<UAsdaQuestLogSubsystem>(), CurMap, Away); break;
				case EKind::Minimap:
					How = FAsdaNavAttackSelfTest::ClickMinimap(S->Sub<UAsdaMinimapSubsystem>(), Away) ? TEXT("HandleMapClick (мини-карта)") : TEXT("НЕТ сетки мини-карты");
					break;
				case EKind::JournalOther:
					How = FAsdaNavAttackSelfTest::PressJournalRoute(S->Sub<UAsdaQuestLogSubsystem>(), S->OtherMap, FIntPoint(100, 100)); break;
				case EKind::LootFar:
					if (AAsdaPlayerController* P = S->PC()) { P->StartLootPickup(Away); }
					How = TEXT("StartLootPickup (ветка Loot клика по миру)"); break;
				case EKind::LootOwnCell:
					if (AAsdaPlayerController* P = S->PC()) { P->StartLootPickup(AsdaCoord::GameToTile(S->Ch() ? S->Ch()->GetGamePosXZ() : S->Home)); }
					How = TEXT("StartLootPickup в своей клетке"); break;
				case EKind::FollowStart:
					if (UAsdaPlayerMenuSubsystem* PM = S->Sub<UAsdaPlayerMenuSubsystem>()) { PM->StartFollow(kPeerNum, 0, TEXT("СоседНавАтака"), 0); }
					How = TEXT("StartFollow (пункт 2062 «Следовать»)"); break;
				default: break;
				}
				UE_LOG(LogAsdaNavAttackTest, Log, TEXT("САМОТЕСТ-НАВАТАКА приказ: %s → клетка (%d,%d); до моба %.0f"), *How, Away.X, Away.Y, S->DistAtNav);
			};
			switch (S->Phase)
			{
			case 0:   // спаун моба (и соседа) → для «следование/автопуть, потом атака» — сначала приказ
				if (!M || !PC || !C || S->InPhase() < 0.6) { return false; }
				if (Kind == EKind::FollowStart || Kind == EKind::FollowThenAttack)
				{
					if (UAsdaWorldSubsystem* WSs = S->Sub<UAsdaWorldSubsystem>())
					{
						WSs->DebugSpawnFakePeer(kPeerNum, TEXT("СоседНавАтака"), AsdaCoord::TileCenterGame(FreeTile(FIntPoint(-1, 0), 9)));
					}
				}
				if (bAttackFirst) { Engage(); S->SetPhase(1); return false; }
				// [ОРИГИНАЛ]-сценарии: приказ, затем в том же кадре игрок кликает по мобу рядом (в дальности).
				S->NavFrom = S->Sent.Num(); S->NavS = FPlatformTime::Seconds(); S->DistAtNav = MobDist();
				if (Kind == EKind::FollowThenAttack)
				{
					if (UAsdaPlayerMenuSubsystem* PM = S->Sub<UAsdaPlayerMenuSubsystem>()) { PM->StartFollow(kPeerNum, 0, TEXT("СоседНавАтака"), 0); }
				}
				else if (UAsdaMinimapSubsystem* MM = S->Sub<UAsdaMinimapSubsystem>())
				{
					MM->BeginMapTravel(S->OtherMap, 100, 100);
				}
				Engage();
				S->SetPhase(5);
				return false;
			case 1:   // ушёл 4026 → ответ сервера: 4 значения, применены первые 2 (меч)
				if (S->CountAll(4026) >= 1)
				{
					Feed(4029, Body4029(Self, S->Rid, kMobHp - 21, { 10, 11, 12, 13 }));
					S->SetPhase(2);
				}
				return false;
			case 2:   // посреди серии (до кадров ударов) — седло/крылья, затем приказ автопути
				if (S->InPhase() < 0.15) { return false; }
				if (Kind == EKind::JournalMount)
				{
					UAsdaWorldSubsystem* WSs = S->Sub<UAsdaWorldSubsystem>();
					bool bOn = false;
					for (int32 Code = 1; Code <= 4000 && WSs && !bOn; ++Code)
					{
						FString Model, Set; float H = 0.f;
						if (WSs->LookupMount(Code, Model, Set, H)) { C->SetMount(Model, Set, H); bOn = C->IsMounted(); }
					}
					S->Expect(bOn, TEXT("посадка на транспорт посреди атаки (цикл замаха жив)"));
				}
				if (Kind == EKind::JournalWings) { C->SetWingsOn(true); }
				Order(Kind);
				S->SetPhase(3);
				return false;
			case 3:   // наблюдение
			case 5:
			{
				const float D = MobDist();
				if (D >= 0.f) { S->DistMin = FMath::Min(S->DistMin, D); S->DistEnd = D; }
				const double Since = FPlatformTime::Seconds() - S->NavS;
				if (!S->bSampled && Since >= kSampleS) { Sample(); }
				// [ОРИГИНАЛ]-сценарии: сервер отвечает на 4026 — серия идёт (цикл замаха держится до конца окна).
				if (S->Phase == 5 && !S->bFed5 && S->CountAll(4026) >= 1)
				{
					Feed(4029, Body4029(Self, S->Rid, kMobHp - 21, { 10, 11, 12, 13 }));
					S->bFed5 = true;   // ответ — один раз
				}
				return Since >= kObserveS;
			}
			default:
				return false;
			}
		},
		[S, Kind, Key, bAttackFirst]()
		{
			if (!S->bApplicable)
			{
				S->Expect(true, TEXT("чужой карты с переходом из текущей нет — сценарий не применим"));
				return;
			}
			AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch(); AAsdaMobActor* M = S->Mob(S->Rid);
			const TArray<int32> Shown = S->Shown();
			const int32 N28 = S->CountAfter(4028), N26 = S->CountAfter(4026);
			const int32 NFree = S->CountAfter(4006, 1), NTgt = S->CountAfter(4006, 2), N08 = S->CountAfter(4008);
			UE_LOG(LogAsdaNavAttackTest, Log, TEXT("САМОТЕСТ-НАВАТАКА замер после приказа: 4006 цель −1 — %d, 4006 цель ≥ 0 — %d, 4028 — %d, 4026 — %d, 4008 — %d; показано %s; до моба %.0f → %.0f (мин %.0f); цель выбрана=%d"),
				NFree, NTgt, N28, N26, N08, *Join(Shown), S->DistAtNav, S->DistEnd, S->DistMin, PC && M && PC->TargetActor.Get() == M);
			if (!bAttackFirst)
			{
				// [ОРИГИНАЛ] атака, начатая игроком позже приказа, идёт; следование/автопуть не ведут персонажа.
				S->Expect(S->CountAll(4026) >= 1, FString::Printf(TEXT("атака началась: 4026 ушёл (%d)"), S->CountAll(4026)));
				S->Expect(S->bSAuto && (S->bSLoop || S->bSAnim), FString::Printf(TEXT("на снимке бой идёт: автоатака=%d, замах=%d/%d"), S->bSAuto, S->bSLoop, S->bSAnim));
				S->Expect(NFree == 0, FString::Printf(TEXT("ход с целью −1 (следование/шаг автопути) при атаке не уходил (%d)"), NFree));
				if (Kind == EKind::TravelThenAttack)
				{
					S->Expect(!S->bTravelAtSample, TEXT("заявка межкартового автопути снята шагом при атаке (эталон LABEL_111)"));
				}
				else
				{
					const UAsdaPlayerMenuSubsystem* PM = S->Sub<UAsdaPlayerMenuSubsystem>();
					S->Expect(PM && PM->IsFollowing(), TEXT("следование не снято — ждёт конца атаки"));
				}
				AsdaCVarSave::SetForTest(TEXT("asda.Move.AutoPathExitsCombat"), 1);
				return;
			}
			if (Kind == EKind::LootOwnCell)
			{
				// [ОРИГИНАЛ] `netHandleTargetObjectClick` (network.c:4089): своя клетка — 5012 сразу, состояние атаки не трогается.
				S->Expect(S->CountAfter(5012) == 1, FString::Printf(TEXT("5012 ушёл сразу (%d)"), S->CountAfter(5012)));
				S->Expect(S->bSAuto && (S->bSLoop || S->bSAnim), FString::Printf(TEXT("атака идёт (автоатака=%d, замах=%d/%d)"), S->bSAuto, S->bSLoop, S->bSAnim));
				S->Expect(NFree == 0 && NTgt == 0, FString::Printf(TEXT("хода нет: 4006 цель −1 %d, к мобу %d"), NFree, NTgt));
				AsdaCVarSave::SetForTest(TEXT("asda.Move.AutoPathExitsCombat"), 1);
				return;
			}
			if (Key == 0)
			{
				const bool bDefect = S->bSLoop || S->bSAnim || S->bSAuto || N28 > 0 || NTgt > 0;
				S->Expect(bDefect, FString::Printf(TEXT("КОНТРОЛЬ: дефект виден — после приказа автоатака=%d, цикл замаха=%d, замах=%d, 4028=%d, подход 4006 к мобу=%d, клип %s"),
					S->bSAuto, S->bSLoop, S->bSAnim, N28, NTgt, *S->SClip));
				AsdaCVarSave::SetForTest(TEXT("asda.Move.AutoPathExitsCombat"), 1);
				return;
			}
			S->Expect(!S->bSAuto && !S->bSSeries, FString::Printf(TEXT("автоатака и серия сняты (автоатака=%d, серия=%d)"), S->bSAuto, S->bSSeries));
			S->Expect(!S->bSLoop && !S->bSAnim, FString::Printf(TEXT("клип атаки снят (цикл=%d, замах=%d)"), S->bSLoop, S->bSAnim));
			S->Expect(S->SAnimState == EAsdaPlayerState::Move && S->bSMoving, FString::Printf(TEXT("обычное движение: состояние %d (ждём Move), идёт=%d"), (int32)S->SAnimState, S->bSMoving));
			if (C)
			{
				if (Kind == EKind::JournalMount)
				{
					S->Expect(S->bSMounted && S->SClip.Contains(TEXT("sit_")) && S->SClip.EndsWith(TEXT("_002")),
						FString::Printf(TEXT("в седле: клип седока — сидячий клип хода _002 (%s), седло=%d"), *S->SClip, S->bSMounted));
					S->Expect(!S->SMountClip.IsEmpty() && S->SMountClip != TEXT("(нет)"), FString::Printf(TEXT("клип транспорта играет (%s)"), *S->SMountClip));
				}
				else if (Kind == EKind::JournalWings)
				{
					S->Expect(S->SClip.EndsWith(TEXT("anim_35")), FString::Printf(TEXT("крылья: клип полёта anim_35 (%s)"), *S->SClip));   // имя ассета — <база>anim_35
				}
				else
				{
					const FString Run = FString::Printf(TEXT("anim_%02d"), C->MotionClipIdx(EAsdaMotionSlot::Run01));
					S->Expect(S->SClip.EndsWith(Run), FString::Printf(TEXT("клип бега %s (сейчас %s)"), *Run, *S->SClip));   // имя ассета — <база>anim_NN
				}
			}
			S->Expect(NFree >= 1, FString::Printf(TEXT("ход 4006 с целью −1 ушёл (%d)"), NFree));
			S->Expect(NTgt == 0, FString::Printf(TEXT("подхода 4006 к прежнему мобу нет (%d)"), NTgt));
			S->Expect(N28 == 0 && N26 == 0, FString::Printf(TEXT("удары по прежнему мобу не идут: 4028 %d, 4026 %d"), N28, N26));
			S->Expect(PC && M && PC->TargetActor.Get() == M, TEXT("цель остаётся выбранной (у эталона снимается только цель атаки)"));
			if (Kind != EKind::JournalOther)
			{
				S->Expect(S->DistEnd > S->DistAtNav + 150.f, FString::Printf(TEXT("персонаж ушёл от моба и не вернулся: %.0f → %.0f"), S->DistAtNav, S->DistEnd));
			}
			else
			{
				S->Expect(S->bTravelAtSample, TEXT("заявка межкартового автопути жива (шаг к порталу)"));
			}
			S->Expect(Shown == TArray<int32>({ 10, 11 }), FString::Printf(TEXT("серия брошена: показаны только применённые сервером 10 и 11 (%s)"), *Join(Shown)));
			AsdaCVarSave::SetForTest(TEXT("asda.Move.AutoPathExitsCombat"), 1);
		});
	};
	AddCase(EKind::JournalSame, 1);
	AddCase(EKind::JournalSame, 0);
	AddCase(EKind::JournalMount, 1);
	AddCase(EKind::JournalWings, 1);
	AddCase(EKind::Minimap, 1);
	AddCase(EKind::Minimap, 0);
	AddCase(EKind::JournalOther, 1);
	AddCase(EKind::FollowStart, 1);
	AddCase(EKind::FollowThenAttack, 1);
	AddCase(EKind::TravelThenAttack, 1);
	AddCase(EKind::LootFar, 1);
	AddCase(EKind::LootFar, 0);
	AddCase(EKind::LootOwnCell, 1);

	// ─────────────────────── Итог ───────────────────────
	Add(TEXT("итог"), 1.f, [S, Reset, Net]()
	{
		Reset();
		if (UAsdaWorldSubsystem* WSs = S->Sub<UAsdaWorldSubsystem>()) { WSs->bRecordHitResults = false; }
		if (S->bJournalOpened) { if (UAsdaQuestLogSubsystem* QL = S->Sub<UAsdaQuestLogSubsystem>()) { QL->Routes.Reset(); QL->Close(); } }
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		Net->bInGameConnected = S->bWasInGame;
		Net->SelfWeaponItemId = S->SavedWeapon;
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = S->SavedEquipped; }
		S->SelfScope.Reset();
		AsdaSelfTestKit::Finish(S->World.Get(), kCmd, false, LogAsdaNavAttackTest, TEXT("САМОТЕСТ-НАВАТАКА"), S->Ok, S->Fail, TEXT(""));
	}, []() { return true; }, []() {});

	// ─────────────────────── Спавн и исполнитель ───────────────────────
	TWeakObjectPtr<UWorld> WW(W);
	{
		FTimerHandle H;
		TWeakPtr<FState> WSg = S;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Net, [WW, WSg]()
		{
			UWorld* Wd = WW.Get();
			const TSharedPtr<FState> Sg = WSg.Pin();
			if (Wd && Sg && !Sg->Gate.Pass(Wd)) { return; }
			APlayerController* P = Wd ? Wd->GetFirstPlayerController() : nullptr;
			if (Wd && (!P || !P->GetPawn()) && GEngine)
			{
				GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
		}), FMath::Max(0.01f, KitArgs.Delay), false);
	}
	TWeakPtr<FState> WSt = S;
	W->GetTimerManager().SetTimer(S->Tick, FTimerDelegate::CreateWeakLambda(Net, [WSt, WW]()
	{
		TSharedPtr<FState> St = WSt.Pin(); UWorld* Wd = WW.Get();
		if (!St || !Wd) { return; }
		if (!St->Gate.Pass(Wd)) { Wd->GetTimerManager().ClearTimer(St->Tick); return; }
		if (!St->Steps.IsValidIndex(St->StepIdx))
		{
			Wd->GetTimerManager().ClearTimer(St->Tick);
			AsdaSelfTestKit::Quit(Wd, St->bQuit);
			return;
		}
		const double Now = FPlatformTime::Seconds();
		FStep& Stp = St->Steps[St->StepIdx];
		if (!St->bStepStarted)
		{
			St->bStepStarted = true; St->StepStartS = Now;
			UE_LOG(LogAsdaNavAttackTest, Log, TEXT("САМОТЕСТ-НАВАТАКА шаг %s"), *Stp.Name);
			if (Stp.Start) { Stp.Start(); }
			return;
		}
		const double El = Now - St->StepStartS;
		const bool bDone = !Stp.Poll || Stp.Poll();
		if (!bDone && El < Stp.TimeoutSec) { return; }
		if (!bDone) { St->Expect(false, FString::Printf(TEXT("шаг не завершился за %.1f с (фаза %d)"), Stp.TimeoutSec, St->Phase)); }
		if (Stp.Check) { Stp.Check(); }
		++St->StepIdx; St->bStepStarted = false;
	}), 0.01f, true, KitArgs.Delay + 2.5f);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestCombatNavAttack(
	AsdaNavAttackTestLocal::kCmd,
	TEXT("Самотест «автопуть во время атаки»: бой с мобом → кнопка маршрута журнала 705 (своя/чужая карта, в седле, с крыльями), клик по "
	     "мини-карте, «Следовать» — атака снята, обычный ход (бег/полёт/ход транспорта), 4006 с целью −1, ударов по прежнему мобу нет; "
	     "атака, начатая игроком при следовании/автопути, не ломается (эталон LABEL_111). Контроль — asda.Move.AutoPathExitsCombat 0. "
	     "Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, AsdaNavAttackTestLocal::kCmd);
		FAsdaNavAttackSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
