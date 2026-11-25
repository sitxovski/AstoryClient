// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «эффект удара навыка у цели-игрока следит за ней» (R5-fix6, 13.11.2026): `asda.Skill.SelfTestHitFxPlayer [delay=N] [quit]`.
//   Проверяет исправление дефекта оригинала под ключом `asda.Skill.HitFxTrackPlayer` (разбор — шапка AsdaSkillFxSubsystem_Spawn.cpp)
//   настоящим путём: своя «Граната» 604 по игроку-соседу — ответ 5257 с записью вида «персонаж», штатный приём
//   (`DebugFeedPacket`); только офлайн (`-AsdaNoNet`). Свой персонаж — `Asda.World.FakeSelfSpawn`, сосед — `DebugSpawnFakePeer`,
//   моб — запись 4013 с номером, РАВНЫМ userIdx соседа (у оригинала эффект уезжал за таким мобом).
//   Шаги:
//     A) ключ 1: прилёт гранаты → эффект взрыва родился у соседа (виден, жив), носитель — актор соседа, а не моб с тем же номером;
//        сосед уходит → эффект идёт за ним (отрыв меньше доли пройденного), у моба эффекта нет;
//     B) контроль, ключ 0: то же → эффект стоит в точке рождения, сосед уходит от него (прежний порт);
//     B2) ключ 1: «Молния» 906 по соседу — эффект стоит на месте (у оригинала носитель этой записи не назван — не тронуто);
//     C) ключ 1: удар «Молнии» по МОБУ — эффект следит за мобом, как и раньше (ветка мобов не тронута).
//   Ничего не отправляет (исходящие перехвачены). Итог — «САМОТЕСТ-УДАР-ПО-ИГРОКУ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaEffectActor.h"
#include "Test/AsdaSelfTestRunGate.h"
#include "AsdaTestPackets.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaSkillUseSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCoord.h"
#include "Npc/AsdaMobActor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaHitFxPlayerTest, Log, All);

namespace AsdaHitFxPlayerTestLocal
{
	constexpr int32 kSelfFallback = 4322;    // свой userNum, если не известен
	constexpr int32 kMobType = 1;            // волк alpen (как у прочих самотестов)
	constexpr int32 kSpdDenom = 250;         // знаменатель скорости 4013
	constexpr int32 kPeer = 3141;            // userIdx соседа-цели; моб-двойник получает тот же номер
	constexpr int32 kMobRid = 31461;         // моб шага C (обычная цель-моб)
	constexpr int32 kHitSkill = 906;         // «Молния»: эффект удара «track», у оригинала носитель не назван — у цели-игрока стоит на месте
	constexpr int32 kGrenade = 604;          // «Граната»: эффект взрыва «track», у оригинала носитель — сам игрок-цель (исправляемый дефект)
	const TCHAR* const kBlastJeb = TEXT("grebom01");   // имя .jeb взрыва гранаты (подстрока имени класса BP_Fx_…)
	constexpr int32 kStaff = 5;              // подтип оружия навыка 906
	constexpr float kPeerSpeed = 0.35f;      // скорость хода соседа (как у пакетов хода самотестов)
	constexpr double kWalk = 150.0;          // сколько сосед должен пройти до проверки, игровых единиц
	const TCHAR* const kHitJeb = TEXT("lgtstmb03");   // имя .jeb эффекта удара навыка 906 (подстрока имени класса BP_Fx_…)

	// Шаг сценария: старт, опрос «готово», проверка.
	struct FStep
	{
		FString Name; float TimeoutSec = 10.f;
		TFunction<void()> Start; TFunction<bool()> Poll; TFunction<void()> Check;
	};

	// 5257 (8 + 29): [0] код, [1..2] кастер, [3..4] навык, [5] 1, [6..7] уровень; одна запись: вид@0 (0 = моб, 1 = персонаж),
	//   номер@1, урон@3, флаг попадания@7, HP после@11, −1@19.
	static TArray<uint8> Body5257(int32 Caster, int32 Skill, uint8 Kind, int32 Idx)
	{
		TArray<uint8> B; B.SetNumZeroed(8 + 29);
		B[0] = 1; AsdaBytes::WrI16(&B[1], Caster); AsdaBytes::WrI16(&B[3], Skill); B[5] = 1; AsdaBytes::WrI16(&B[6], 1);
		uint8* R = B.GetData() + 8; R[0] = Kind; AsdaBytes::WrI16(R + 1, Idx); AsdaBytes::WrI32(R + 3, 1); R[7] = 0x01;
		AsdaBytes::WrI32(R + 11, 900); AsdaBytes::WrI16(R + 19, -1);
		return B;
	}

	// Состояние прогона.
	struct FState
	{
		const TCHAR* Cmd = nullptr; const TCHAR* Tag = nullptr;   // команда и метка строк журнала
		TWeakObjectPtr<UWorld> World;                             // мир
		TWeakObjectPtr<UAsdaNetSubsystem> Net;                    // сеть
		TArray<FStep> Steps;                                      // шаги
		int32 StepIdx = 0; bool bStepStarted = false; double StepStartS = 0.0;   // текущий шаг, начат ли он, время его начала
		FTimerHandle Tick; bool bQuit = false;                    // таймер шагов; выход из игры по итогу
		FAsdaSelfTestRunGate Gate{ TEXT(""), false };             // допуск прогона (не идёт ли другой самотест)
		int32 Ok = 0, Fail = 0;                                   // счётчики
		uint32 Gen = 0;                                           // поколение перехвата исходящих
		TUniquePtr<FAsdaNetSelfScope> SelfScope;                  // «свой персонаж известен» на время прогона
		bool bWasInGame = false; bool bWasStand = false;          // прежние флаги сети
		bool bWasSelfDead = false;                                // прежний флаг «свой мёртв»
		int32 Self = -1; FIntPoint HomeTile = FIntPoint::ZeroValue;   // свой userNum; тайл своего персонажа
		int32 WasWeaponSub = 99;                                  // прежний подтип оружия
		TSet<TWeakObjectPtr<AAsdaEffectActor>> Before;            // акторы эффектов, жившие до подачи пакета шага
		TWeakObjectPtr<AAsdaEffectActor> HitFx;                   // эффект удара текущего шага
		FVector FxBorn = FVector::ZeroVector;                     // где эффект удара стоял в кадре удара
		FVector HostBorn = FVector::ZeroVector;                   // где цель стояла в кадре удара
		bool bHostWalking = false;                                // цель отправлена идти
		bool bFxAliveAtStart = false;                             // эффект был жив, когда цель пошла

		// Короткие доступы: сеть, экземпляр игры, подсистемы мира / навыков, свой контроллер и персонаж, сосед, мобы.
		UAsdaNetSubsystem* N() const { return Net.Get(); }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		UAsdaSkillUseSubsystem* SU() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		AAsdaPlayerCharacter* Peer() const { const UAsdaWorldSubsystem* W = WS(); return W ? Cast<AAsdaPlayerCharacter>(W->ActorByUserIdx(kPeer)) : nullptr; }
		AAsdaMobActor* Twin() const { const UAsdaWorldSubsystem* W = WS(); return W ? W->FindNetMobByRid(kPeer) : nullptr; }       // моб с номером соседа
		AAsdaMobActor* Mob() const { const UAsdaWorldSubsystem* W = WS(); return W ? W->FindNetMobByRid(kMobRid) : nullptr; }     // моб шага C
		double El() const { return FPlatformTime::Seconds() - StepStartS; }    // секунд с начала шага
		// Проверка с записью в журнал («OK» / «ПРОВАЛ» + имя шага).
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaHitFxPlayerTest, Log, TEXT("%s %s %s: %s"), Tag, bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		void Feed(int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(Op, B); } }   // входящий пакет штатным приёмом
		void SetKey(int32 V) { AsdaCVarSave::SetForTest(TEXT("asda.Skill.HitFxTrackPlayer"), V); }   // ключ правки; исходное вернёт `Finish`
		// Запомнить живые акторы эффектов — всё, что родится позже, считается «новым».
		void Snapshot()
		{
			Before.Reset(); HitFx.Reset(); bHostWalking = false; bFxAliveAtStart = false;
			if (UWorld* W = World.Get()) { for (TActorIterator<AAsdaEffectActor> It(W); It; ++It) { Before.Add(*It); } }
		}
		// Новый (после `Snapshot`) живой эффект удара — по имени .jeb `Jeb` в имени класса; nullptr — ещё не родился.
		AAsdaEffectActor* FindHitFx(const TCHAR* Jeb = kHitJeb) const
		{
			UWorld* W = World.Get();
			if (!W) { return nullptr; }
			for (TActorIterator<AAsdaEffectActor> It(W); It; ++It)
			{
				AAsdaEffectActor* E = *It;
				if (!IsValid(E) || E->IsActorBeingDestroyed() || Before.Contains(E)) { continue; }
				if (E->GetClass()->GetName().Contains(Jeb)) { return E; }
			}
			return nullptr;
		}
		// Расстояние в плоскости между двумя точками мира.
		static double D2(const FVector& A, const FVector& B) { return FVector::Dist2D(A, B); }
		// Снять свой навык между шагами.
		void Calm() { if (UAsdaSkillUseSubsystem* U = SU()) { U->DebugSetCastState(0, -1); } }
	};

	// Исполнитель шагов: спавн своего персонажа, шаги по таймеру.
	static void RunSteps(TSharedRef<FState> S, UWorld* W, const FAsdaSelfTestArgs& KitArgs, UAsdaNetSubsystem* Net)
	{
		TWeakObjectPtr<UWorld> WW(W);
		{
			FTimerHandle H; TWeakPtr<FState> WSg = S;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Net, [WW, WSg]()
			{
				UWorld* Wd = WW.Get(); const TSharedPtr<FState> Sg = WSg.Pin();
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
			if (!St->Steps.IsValidIndex(St->StepIdx)) { Wd->GetTimerManager().ClearTimer(St->Tick); AsdaSelfTestKit::Quit(Wd, St->bQuit); return; }
			const double Now = FPlatformTime::Seconds();
			FStep& Stp = St->Steps[St->StepIdx];
			if (!St->bStepStarted)
			{
				St->bStepStarted = true; St->StepStartS = Now;
				UE_LOG(LogAsdaHitFxPlayerTest, Log, TEXT("%s шаг %s"), St->Tag, *Stp.Name);
				if (Stp.Start) { Stp.Start(); }
				return;
			}
			const double El = Now - St->StepStartS;
			const bool bDone = !Stp.Poll || Stp.Poll();
			if (!bDone && El < Stp.TimeoutSec) { return; }
			if (Stp.Check) { Stp.Check(); }
			++St->StepIdx; St->bStepStarted = false;
		}), 0.05f, true, KitArgs.Delay + 2.5f);
	}
}

// Самотест «эффект удара навыка у цели-игрока следит за ней» — запуск и сценарий.
struct FAsdaSkillHitFxPlayerSelfTest
{
	// Запуск: разбор аргументов (`delay=N`, `quit`), сборка шагов A–C и их исполнение по таймеру.
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaSkillHitFxPlayerSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaHitFxPlayerTestLocal;
	const TCHAR* const kCmd = TEXT("asda.Skill.SelfTestHitFxPlayer");
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 5256 };
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, KitArgs.bQuit, TEXT("нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Cmd = kCmd; S->Tag = TEXT("САМОТЕСТ-УДАР-ПО-ИГРОКУ");
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate = FAsdaSelfTestRunGate{ kCmd, KitArgs.bQuit };
	S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);

	auto Add = [S](const FString& Name, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.TimeoutSec = Timeout; St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};
	// Шаг «сосед и моб-двойник на местах»: сосед в 3 клетках, моб с номером соседа — в 6 клетках в другую сторону.
	auto AddScene = [S, Add](const FString& Name)
	{
		Add(Name, 6.f, [S]()
		{
			S->Calm();
			{ TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], kPeer); S->Feed(4011, B); }   // прежние сосед и моб-двойник (номер один)
			if (UAsdaWorldSubsystem* Ws = S->WS())
			{
				Ws->DebugSpawnFakePeer(kPeer, TEXT("Цель"), AsdaCoord::TileCenterGame(S->HomeTile.X + 3, S->HomeTile.Y));
			}
			S->Feed(4013, AsdaTestPackets::MobSpawn4013(kPeer, kMobType, S->HomeTile + FIntPoint(-6, 0), 1, kSpdDenom));
		}, [S]() { return S->El() > 0.8 && S->Peer() && S->Twin(); }, [S]()
		{
			S->Expect(S->Peer() != nullptr, FString::Printf(TEXT("сосед userIdx=%d на месте"), kPeer));
			S->Expect(S->Twin() != nullptr, FString::Printf(TEXT("моб с тем же номером rid=%d на месте (у оригинала эффект уезжал за ним)"), kPeer));
		});
	};
	// Шаг «свой навык Skill по соседу: эффект удара Jeb, затем сосед уходит». bFollow — ждём слежения или стояния.
	auto AddHit = [S, Add](const FString& Name, bool bFollow, int32 Skill, const TCHAR* Jeb)
	{
		Add(Name, 14.f, [S, Skill]()
		{
			S->Snapshot();
			if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(S->Peer()); }
			S->Feed(5257, Body5257(S->Self, Skill, 1, kPeer));
		}, [S, Jeb]()
		{
			AAsdaPlayerCharacter* P = S->Peer();
			if (!P) { return true; }
			// 1) ждём кадр удара — эффект удара родился; 2) отправляем соседа идти; 3) ждём, пока он пройдёт kWalk либо эффект догорит.
			if (!S->HitFx.IsValid() && !S->bHostWalking)
			{
				AAsdaEffectActor* E = S->FindHitFx(Jeb);
				if (!E) { return false; }
				S->HitFx = E; S->FxBorn = E->GetActorLocation(); S->HostBorn = P->GetActorLocation();
				UE_LOG(LogAsdaHitFxPlayerTest, Log, TEXT("%s кадр удара через %.2f с: эффект «%s» в %.0f ед. от соседа, носитель %s"), S->Tag, S->El(), Jeb,
					FState::D2(S->FxBorn, S->HostBorn), E->FollowActor ? *E->FollowActor->GetName() : TEXT("нет (стоит на месте)"));
				const FVector2D From = P->GetGamePosXZ();
				P->NetMove(From, From + FVector2D(0.0, 600.0), kPeerSpeed);
				S->bHostWalking = true; S->bFxAliveAtStart = true;
				return false;
			}
			if (!S->HitFx.IsValid()) { return true; }   // эффект догорел раньше, чем сосед прошёл путь — проверка скажет об этом
			return FState::D2(P->GetActorLocation(), S->HostBorn) >= kWalk;
		}, [S, bFollow, Jeb]()
		{
			AAsdaPlayerCharacter* P = S->Peer(); AAsdaEffectActor* E = S->HitFx.Get(); const AAsdaMobActor* Twin = S->Twin();
			S->Expect(S->bFxAliveAtStart, FString::Printf(TEXT("кадр удара наступил: эффект удара «%s» родился и виден"), Jeb));
			S->Expect(S->bFxAliveAtStart && FState::D2(S->FxBorn, S->HostBorn) < 5.0,
				FString::Printf(TEXT("эффект родился у цели-игрока: %.1f ед. от неё (ждём < 5)"), FState::D2(S->FxBorn, S->HostBorn)));
			const double Walked = P ? FState::D2(P->GetActorLocation(), S->HostBorn) : 0.0;
			S->Expect(P && E && Walked >= kWalk, FString::Printf(TEXT("сосед прошёл %.0f ед. (ждём ≥ %.0f), эффект ещё жив: %d"), Walked, kWalk, E ? 1 : 0));
			if (!P || !E) { return; }
			const double Gap = FState::D2(E->GetActorLocation(), P->GetActorLocation());        // отрыв эффекта от соседа
			const double Drift = FState::D2(E->GetActorLocation(), S->FxBorn);                  // уход эффекта от точки рождения
			if (bFollow)
			{
				S->Expect(E->FollowActor == P, FString::Printf(TEXT("носитель эффекта — актор соседа (%s)"), E->FollowActor ? *E->FollowActor->GetName() : TEXT("нет")));
				S->Expect(Gap < 0.4 * Walked, FString::Printf(TEXT("эффект идёт за соседом: отрыв %.0f ед. при пройденных %.0f (ждём < %.0f)"), Gap, Walked, 0.4 * Walked));
			}
			else
			{
				S->Expect(E->FollowActor == nullptr, FString::Printf(TEXT("носителя у эффекта нет (%s)"), E->FollowActor ? *E->FollowActor->GetName() : TEXT("нет")));
				S->Expect(Drift < 5.0 && Gap > 0.8 * Walked,
					FString::Printf(TEXT("эффект стоит в точке рождения: уход %.1f ед. (ждём < 5), сосед ушёл от него на %.0f при пройденных %.0f"), Drift, Gap, Walked));
			}
			S->Expect(!Twin || (E->FollowActor != Twin && FState::D2(E->GetActorLocation(), Twin->GetActorLocation()) > 200.0),
				FString::Printf(TEXT("за мобом с номером соседа эффект не едет: до моба %.0f ед. (ждём > 200)"), Twin ? FState::D2(E->GetActorLocation(), Twin->GetActorLocation()) : -1.0));
		});
	};

	Add(TEXT("подготовка"), 5.f, [S, Net]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		if (!C || !S->PC() || !S->WS()) { S->Expect(false, TEXT("нет своего персонажа/мира после FakeSelfSpawn")); return; }
		S->HomeTile = AsdaCoord::GameToTile(C->GetGamePosXZ());
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = kSelfFallback; Net->bSelfUserNumKnown = true; }
		S->bWasSelfDead = Net->bSelfDead; Net->bSelfDead = false;
		S->Self = (int32)Net->SelfUserNum;
		S->Gen = AsdaSelfTestKit::ArmCapture(Net, [](uint16, const uint8*, int32, bool, TCHAR) {}, (uint16)0, S->Cmd);
		S->Expect(S->Gen != 0, TEXT("перехват исходящих взведён"));
		S->bWasInGame = Net->bInGameConnected; Net->bInGameConnected = true;
		S->WasWeaponSub = C->EquippedWeaponSubType; C->EquippedWeaponSubType = kStaff;
		S->bWasStand = Net->bStandCaptureSkillUse; Net->bStandCaptureSkillUse = true;
		S->SetKey(1);
	}, [S]() { return S->El() > 0.5; }, nullptr);

	// ── A. ключ 1: эффект удара у цели-игрока следит за ней ──
	AddScene(TEXT("A сосед и моб с его номером"));
	AddHit(TEXT("A «Граната» по соседу (ключ 1) — эффект взрыва идёт за соседом"), true, kGrenade, kBlastJeb);

	// ── B. контроль, ключ 0: прежний порт — эффект стоит в точке рождения ──
	Add(TEXT("B ключ 0"), 1.f, [S]() { S->SetKey(0); }, nullptr, nullptr);
	AddScene(TEXT("B сосед и моб с его номером"));
	AddHit(TEXT("B «Граната» по соседу (ключ 0) — эффект взрыва стоит на месте (прежний порт, дефект виден)"), false, kGrenade, kBlastJeb);
	Add(TEXT("B ключ 1"), 1.f, [S]() { S->SetKey(1); }, nullptr, nullptr);

	// ── B2. ключ 1: «Молния» 906 по соседу — как у оригинала, эффект стоит на месте (носитель записи не назван) ──
	AddScene(TEXT("B2 сосед и моб с его номером"));
	AddHit(TEXT("B2 «Молния» по соседу (ключ 1) — эффект стоит на месте, как у оригинала"), false, kHitSkill, kHitJeb);

	// ── D. ★ R5-fix8 (14.11.2026): записи «track» ФАЗЫ КАСТА у цели-игрока (ключ `asda.Skill.CastFxTrackPlayer`) ──
	//   756 «Ловушка»: у оригинала номер игрока-цели передан не в носитель, а в параметр a7 создателя (дефект) — при ключе 1 эффект следит за целью,
	//   при 0 — стоит (прежний порт). 908: у оригинала носитель не назван — при ключе 1 стоит, при 2 следит.
	auto SetCastKey = [](int32 V) { AsdaCVarSave::SetForTest(TEXT("asda.Skill.CastFxTrackPlayer"), V); };
	Add(TEXT("D ключ каста 1"), 1.f, [SetCastKey]() { SetCastKey(1); }, nullptr, nullptr);
	AddScene(TEXT("D1 сосед и моб с его номером"));
	AddHit(TEXT("D1 «Ловушка» 756 по соседу (ключ 1) — эффект фазы каста идёт за соседом (дефект оригинала исправлен)"), true, 756, TEXT("trapth01"));
	Add(TEXT("D ключ каста 0"), 1.f, [SetCastKey]() { SetCastKey(0); }, nullptr, nullptr);
	AddScene(TEXT("D2 сосед и моб с его номером"));
	AddHit(TEXT("D2 «Ловушка» 756 по соседу (ключ 0) — эффект стоит на месте (прежний порт, дефект виден)"), false, 756, TEXT("trapth01"));
	Add(TEXT("D ключ каста 1 снова"), 1.f, [SetCastKey]() { SetCastKey(1); }, nullptr, nullptr);
	AddScene(TEXT("D3 сосед и моб с его номером"));
	AddHit(TEXT("D3 навык 908 по соседу (ключ 1) — эффект фазы каста стоит на месте, как у оригинала"), false, 908, TEXT("dakhlpb03"));
	Add(TEXT("D ключ каста 2"), 1.f, [SetCastKey]() { SetCastKey(2); }, nullptr, nullptr);
	AddScene(TEXT("D4 сосед и моб с его номером"));
	AddHit(TEXT("D4 навык 908 по соседу (ключ 2) — эффект идёт за соседом (по выбору владельца)"), true, 908, TEXT("dakhlpb03"));
	Add(TEXT("D ключ каста 1 в итоге"), 1.f, [SetCastKey]() { SetCastKey(1); }, nullptr, nullptr);

	// ── E. ★ R5-fix8: навык 1064 — эффект удара у цели-игрока у оригинала стоит (создатель без носителя): ключ удара 1 — стоит,
	//   значение 2 — следит (по выбору владельца) ──
	AddScene(TEXT("E1 сосед и моб с его номером"));
	AddHit(TEXT("E1 навык 1064 по соседу (ключ удара 1) — эффект удара стоит на месте, как у оригинала"), false, 1064, TEXT("fireragb03"));
	Add(TEXT("E ключ удара 2"), 1.f, [S]() { S->SetKey(2); }, nullptr, nullptr);
	AddScene(TEXT("E2 сосед и моб с его номером"));
	AddHit(TEXT("E2 навык 1064 по соседу (ключ удара 2) — эффект удара идёт за соседом (по выбору владельца)"), true, 1064, TEXT("fireragb03"));
	Add(TEXT("E ключ удара 1"), 1.f, [S]() { S->SetKey(1); }, nullptr, nullptr);

	// ── C. ключ 1: удар по мобу — ветка мобов прежняя (эффект следит за мобом) ──
	Add(TEXT("C моб-цель"), 6.f, [S]()
	{
		S->Calm();
		S->Feed(4013, AsdaTestPackets::MobSpawn4013(kMobRid, kMobType, S->HomeTile + FIntPoint(4, 2), 1, kSpdDenom));
	}, [S]() { return S->El() > 0.8 && S->Mob(); }, [S]() { S->Expect(S->Mob() != nullptr, FString::Printf(TEXT("моб rid=%d на месте"), kMobRid)); });
	Add(TEXT("C удар «Молнии» по мобу (ключ 1) — эффект следит за мобом, как раньше"), 8.f, [S]()
	{
		S->Snapshot();
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(S->Mob()); }
		S->Feed(5257, Body5257(S->Self, kHitSkill, 0, kMobRid));
	}, [S]() { if (AAsdaEffectActor* E = S->FindHitFx()) { S->HitFx = E; return true; } return false; }, [S]()
	{
		const AAsdaEffectActor* E = S->HitFx.Get();
		S->Expect(E && E->FollowActor == S->Mob(), FString::Printf(TEXT("эффект удара родился, носитель — моб-цель (%s)"),
			(E && E->FollowActor) ? *E->FollowActor->GetName() : TEXT("нет")));
	});

	Add(TEXT("итог"), 2.f, [S, Net]()
	{
		S->Calm();
		{ TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], kPeer); S->Feed(4011, B); }
		{ TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], kMobRid); S->Feed(4011, B); }
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(nullptr); P->bAutoAttack = false; P->ExitCombatStance(); }
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = S->WasWeaponSub; }
		Net->ClearMoveRequestInFlight(TEXT("самотест эффекта удара по игроку: итог"));
		Net->bStandCaptureSkillUse = S->bWasStand;
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		Net->bInGameConnected = S->bWasInGame;
		Net->bSelfDead = S->bWasSelfDead;
		S->SelfScope.Reset();
	}, [S]() { return S->El() > 0.3; }, [S]()
	{
		AsdaSelfTestKit::Finish(S->World.Get(), S->Cmd, false, LogAsdaHitFxPlayerTest, S->Tag, S->Ok, S->Fail);
	});
	RunSteps(S, W, KitArgs, Net);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestSkillHitFxPlayer(
	TEXT("asda.Skill.SelfTestHitFxPlayer"),
	TEXT("Самотест R5-fix6 «эффект удара навыка у цели-игрока следит за ней» (дефект оригинала исправлен): взрыв «Гранаты» 604 по соседу — эффект ")
	TEXT("идёт за соседом и не едет за мобом с тем же номером (ключ asda.Skill.HitFxTrackPlayer 1); контроль ключа 0 — эффект стоит на месте; ")
	TEXT("«Молния» 906 по соседу стоит (как оригинал); удар по мобу — как раньше. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Skill.SelfTestHitFxPlayer"));
		FAsdaSkillHitFxPlayerSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
