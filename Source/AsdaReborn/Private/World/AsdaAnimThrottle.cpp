// Copyright (c) 2026 @sitxovski (Discord)
#include "World/AsdaAnimThrottle.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CONSOLE — дев-команда дампа вне Shipping
#include "AsdaPerf.h"         // ★ R4, доля a: области замера урезания анимации

#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ObjectKey.h"

#include "Npc/AsdaMobActor.h"
#include "Npc/AsdaNpcActor.h"
#include "World/AsdaPetActor.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaBlendAnimInstance.h"  // ★ 31.10: вздрагивание держит часы позы полной частотой
#include "World/AsdaPlayerController.h"   // AAsdaWorldGameMode — гейт «только игровые карты»
#include "World/AsdaWorldSubsystem.h"     // LocalPlayerActor — свой персонаж (страховка поверх IsLocallyControlled)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaAnimThr, Log, All);

// ─────────────── Ключи (регистрация единственная в Source/: grep "asda\.Anim\.Throttle" 29.10.2026 = 0) ───────────────
// Рубильник и ключ отката: 1 = урезание анимации чужих скелетов вдали/вне кадра, 0 = прежнее поведение (на лету).
static int32 GAsdaAnimThrottle = 1;
// Поколение настроек: растёт при смене любого ключа — тик мира переприменяет состояние всем записям.
static int32 GAsdaAnimThrottleGen = 1;
static void OnAsdaAnimThrottleChanged(IConsoleVariable*) { ++GAsdaAnimThrottleGen; }
static FAutoConsoleVariableRef CVarAsdaAnimThrottle(
	TEXT("asda.Anim.Throttle"),
	GAsdaAnimThrottle,
	TEXT("[ПОРТ] Урезание анимации чужих скелетов: 1 = декорации карты считают позу только в кадре, декорации/мобы/NPC/чужие игроки/питомцы — URO (вдали реже, с интерполяцией); 0 = как было. Свой персонаж не трогается."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaAnimThrottleChanged),
	ECVF_Default);

// Интерполяция пропущенных кадров URO: 1 = кости между расчётами плавно интерполируются (глазом не отличить),
// 0 = поза «ступенькой» (дешевле, но на 2–3-кадровых шагах видно дёргание вблизи порогов).
static int32 GAsdaAnimThrottleInterp = 1;
static FAutoConsoleVariableRef CVarAsdaAnimThrottleInterp(
	TEXT("asda.Anim.ThrottleInterp"),
	GAsdaAnimThrottleInterp,
	TEXT("Интерполяция пропущенных кадров URO у урезанных скелетов: 1 = да (умолчание), 0 = нет."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaAnimThrottleChanged),
	ECVF_Default);

// ★ 30.10.2026 [ИСПРАВЛЕНИЕ] Ключ отката: 1 = чужой игрок на время цикла замаха без урезания (часы боевого клипа полной частотой,
//   разбор у `AsdaAnimThrottle::HoldCombatClock`), 0 = под урезанием и в замахе (как 29.10).
static int32 GAsdaAnimThrottleCombatHold = 1;
static FAutoConsoleVariableRef CVarAsdaAnimThrottleCombatHold(
	TEXT("asda.Anim.ThrottleCombatHold"),
	GAsdaAnimThrottleCombatHold,
	TEXT("[ИСПРАВЛЕНИЕ] Чужой игрок в цикле замаха: 1 = анимация полной частотой (по позиции клипа строятся отрезки замаха и кадры цифр урона от 4029), 0 = под урезанием, как 29.10."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaAnimThrottleChanged),
	ECVF_Default);

namespace
{
	// Роль скелета — от класса владельца. Unknown = не трогаем.
	enum class ERole : uint8 { Unknown, Prop, Mob, Npc, Peer, Pet };

	// Запись одного компонента: прежние значения (для отката) и что применено сейчас.
	struct FRec
	{
		TWeakObjectPtr<USkeletalMeshComponent> Comp;                                  // компонент
		EVisibilityBasedAnimTickOption OrigTickOpt = EVisibilityBasedAnimTickOption::AlwaysTickPose;   // как было
		bool bOrigUro = false;                                                         // URO как было
		ERole Role = ERole::Unknown;                                                   // роль
		bool bApplied = false;                                                         // урезание стоит сейчас
		bool bInterpSet = false;                                                       // интерполяция выставлена в параметрах трекера
		bool bCombatHeld = false;                                                      // ★ 30.10: чужой игрок в замахе — урезание снято
	};

	// Состояние одного мира.
	struct FWorldState
	{
		TWeakObjectPtr<UWorld> World;                       // мир
		bool bInitialScanDone = false;                      // начальный проход по акторам уровня сделан
		FDelegateHandle SpawnHandle;                        // подписка на спавн акторов
		TArray<TWeakObjectPtr<AActor>> Pending;             // акторы, появившиеся после начального прохода
		uint64 PendingFrame = 0;                            // кадр, в котором очередь пополнялась (разбор — со следующего)
		TArray<FRec> Recs;                                  // все учтённые компоненты
		TSet<TObjectKey<USkeletalMeshComponent>> Known;     // уже учтённые компоненты (без квадратичного поиска по Recs)
		int32 AppliedGen = 0;                               // поколение ключей, применённое к Recs
		uint64 NextSweepFrame = 0;                          // медленный проход: чистка мёртвых и досылка интерполяции
		TArray<TWeakObjectPtr<USkeletalMeshComponent>> Held;   // ★ 30.10: чужие игроки в замахе без урезания (проверка конца — каждый кадр)
		TMap<TObjectKey<USkeletalMeshComponent>, int32> Index;   // ★ R4: компонент → номер записи в Recs (ведётся вместе с Known)
	};

	// Миры по ключу. Записей единицы (игровой мир + редкие переходы).
	TMap<TObjectKey<UWorld>, FWorldState> GStates;

	// ★ R4, доля a (14.11.2026; запись 11 от 15.11.2026 — ключ убран) [ПОРТ, производительность]: запись компонента — по карте, а не
	//   линейным поиском по всем учтённым скелетам мира (их сотни–тысячи: декорации, мобы, NPC, игроки). Поиск идёт при старте
	//   замаха и вздрагивания чужого игрока (`HoldCombatClock`) и когда у него кончился цикл замаха (`TickWorld`). Карта ведётся
	//   там же, где множество `Known`; найденный номер сверяется с самой записью, при промахе карты — линейный поиск, так что
	//   итог всегда тот же, что у прежнего кода.

	// Линейный поиск записи компонента (страховка при промахе карты). nullptr — нет.
	FRec* FindRecLinear(FWorldState& S, const USkeletalMeshComponent* C)
	{
		for (FRec& R : S.Recs)
		{
			if (R.Comp.Get() == C) { return &R; }
		}
		return nullptr;
	}

	// Запись компонента: по карте, при промахе — линейным поиском. nullptr — нет.
	FRec* FindRec(FWorldState& S, const USkeletalMeshComponent* C)
	{
		if (const int32* I = S.Index.Find(TObjectKey<USkeletalMeshComponent>(C)))
		{
			if (S.Recs.IsValidIndex(*I) && S.Recs[*I].Comp.Get() == C) { return &S.Recs[*I]; }
		}
		return FindRecLinear(S, C);
	}

	// Свой персонаж — никогда. Чужой игрок — павн без локального управления и не LocalPlayerActor подсистемы мира.
	bool IsLocalSelf(const AActor* A)
	{
		const APawn* P = Cast<APawn>(A);
		if (!P) { return false; }
		if (P->IsLocallyControlled() || P->GetController() != nullptr) { return true; }
		if (const UWorld* W = A->GetWorld())
		{
			if (const UGameInstance* GI = W->GetGameInstance())
			{
				if (const UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>())
				{
					if (WS->LocalPlayerActor.Get() == A) { return true; }
				}
			}
			if (UGameplayStatics::GetPlayerPawn(W, 0) == P) { return true; }
		}
		return false;
	}

	// Роль по классу владельца.
	ERole RoleOf(const AActor* A)
	{
		if (!A) { return ERole::Unknown; }
		if (A->IsA<ASkeletalMeshActor>()) { return ERole::Prop; }
		if (A->IsA<AAsdaMobActor>()) { return ERole::Mob; }
		if (A->IsA<AAsdaNpcActor>()) { return ERole::Npc; }
		if (A->IsA<AAsdaPetActor>()) { return ERole::Pet; }
		if (A->IsA<AAsdaPlayerCharacter>()) { return IsLocalSelf(A) ? ERole::Unknown : ERole::Peer; }
		return ERole::Unknown;
	}

	// Выставить интерполяцию в параметрах трекера (они общие на актор и создаются при регистрации компонента).
	void SetInterp(FRec& R, USkeletalMeshComponent* C)
	{
		if (C->AnimUpdateRateParams)
		{
			C->AnimUpdateRateParams->bInterpolateSkippedFrames = (GAsdaAnimThrottleInterp != 0);
			R.bInterpSet = true;
		}
	}

	// ★ 30.10.2026: часы боевого клипа заняты — чужой игрок в цикле замаха (разбор у `AsdaAnimThrottle::HoldCombatClock`).
	bool IsCombatClockBusy(const USkeletalMeshComponent* C)
	{
		const AAsdaPlayerCharacter* P = C ? Cast<AAsdaPlayerCharacter>(C->GetOwner()) : nullptr;
		if (P && P->IsAttackLoopAlive()) { return true; }
		// ★ 31.10.2026 [ИСПРАВЛЕНИЕ]: и на время вздрагивания от удара (125 мс поворота кости «4», `UAsdaBlendAnimInstance`) — под URO
		//   поза чужого игрока вне кадра/вдали считается раз в 2–4 кадра, и окно вздрагивания проходило без оценки (самотест 4д).
		const UAsdaBlendAnimInstance* I = C ? Cast<UAsdaBlendAnimInstance>(C->GetAnimInstance()) : nullptr;
		return I && I->IsHitFlinchActiveGT();
	}

	// Вернуть компоненту прежние значения (урезание снято).
	void RestoreOrig(FRec& R, USkeletalMeshComponent* C)
	{
		C->VisibilityBasedAnimTickOption = R.OrigTickOpt;
		C->bEnableUpdateRateOptimizations = R.bOrigUro;
		if (C->AnimUpdateRateParams) { C->AnimUpdateRateParams->bInterpolateSkippedFrames = false; }
		R.bApplied = false;
	}

	// Применить или откатить урезание одной записи по текущим ключам.
	void ApplyRec(FRec& R)
	{
		USkeletalMeshComponent* C = R.Comp.Get();
		if (!C) { return; }
		R.bCombatHeld = false;
		if (GAsdaAnimThrottle != 0)
		{
			if (R.Role == ERole::Unknown) { return; }   // снятая с учёта запись (свой персонаж) — не трогаем
			// ★ 30.10.2026: чужой игрок, учтённый (или переприменённый) посреди замаха, — без урезания до конца цикла.
			if (R.Role == ERole::Peer && GAsdaAnimThrottleCombatHold != 0 && IsCombatClockBusy(C))
			{
				if (R.bApplied) { RestoreOrig(R, C); }
				R.bCombatHeld = true;
				return;
			}
			if (R.Role == ERole::Prop) { C->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered; }
			C->bEnableUpdateRateOptimizations = true;
			R.bInterpSet = false;
			SetInterp(R, C);
			R.bApplied = true;
		}
		else if (R.bApplied)
		{
			RestoreOrig(R, C);
		}
	}

	// Применить запись в состоянии мира: запись, оставленная без урезания на время замаха, — в список проверки конца цикла.
	void ApplyRecIn(FWorldState& S, FRec& R)
	{
		ApplyRec(R);
		if (R.bCombatHeld) { S.Held.AddUnique(R.Comp); }
	}

	// Учесть все скелеты актора (повторный учёт того же компонента исключён проверкой по списку).
	void AddActor(FWorldState& S, AActor* A)
	{
		if (!IsValid(A)) { return; }
		const ERole Role = RoleOf(A);
		if (Role == ERole::Unknown) { return; }
		if (Role == ERole::Peer)
		{
			UE_LOG(LogAsdaAnimThr, Log, TEXT("[анимация] чужой игрок под урезанием: %s (контроллер %s)"),
				*A->GetName(), *GetNameSafe(Cast<APawn>(A)->GetController()));
		}
		TInlineComponentArray<USkeletalMeshComponent*> Comps(A);
		for (USkeletalMeshComponent* C : Comps)
		{
			if (!C) { continue; }
			bool bKnown = false;
			S.Known.Add(TObjectKey<USkeletalMeshComponent>(C), &bKnown);
			if (bKnown) { continue; }
			FRec& R = S.Recs.AddDefaulted_GetRef();
			S.Index.Add(TObjectKey<USkeletalMeshComponent>(C), S.Recs.Num() - 1);   // ★ R4: номер новой записи — в карту
			R.Comp = C;
			R.OrigTickOpt = C->VisibilityBasedAnimTickOption;
			R.bOrigUro = C->bEnableUpdateRateOptimizations;
			R.Role = Role;
			ApplyRecIn(S, R);
		}
	}

	// Мир игровой карты (не логин, не превью редактора).
	bool IsMapWorld(const UWorld* W)
	{
		return W && W->IsGameWorld() && W->GetAuthGameMode() && W->GetAuthGameMode()->IsA<AAsdaWorldGameMode>();
	}
}

namespace AsdaAnimThrottle
{
	bool IsEnabled() { return GAsdaAnimThrottle != 0; }

	void HoldCombatClock(USkeletalMeshComponent* C)
	{
		if (!C || GAsdaAnimThrottle == 0 || GAsdaAnimThrottleCombatHold == 0) { return; }
		ASDA_PERF_SCOPE("AnimThrottle.HoldCombatClock");   // R4, доля a: поиск записи и снятие урезания (в Shipping пусто)
		UWorld* W = C->GetWorld();
		FWorldState* S = W ? GStates.Find(TObjectKey<UWorld>(W)) : nullptr;
		// Ещё не учтён (пришёл по сети в этом кадре) — учёт застанет цикл замаха и урезание не поставит (`ApplyRec`).
		if (!S || !S->Known.Contains(TObjectKey<USkeletalMeshComponent>(C))) { return; }
		// ★ R4, доля a (14.11.2026): запись — `FindRec` (по карте; при промахе карты — линейный поиск).
		if (FRec* Rp = FindRec(*S, C))
		{
			FRec& R = *Rp;
			if (R.Role != ERole::Peer || R.bCombatHeld) { return; }
			// Снятие в том же кадре, ДО запуска клипа замаха: с кадра 0 клип тикает каждый кадр, пропущенное время не копится.
			if (R.bApplied) { RestoreOrig(R, C); }
			R.bCombatHeld = true;
			S->Held.AddUnique(R.Comp);
			UE_LOG(LogAsdaAnimThr, Verbose, TEXT("[анимация] %s: замах — урезание снято до конца цикла"), *GetNameSafe(C->GetOwner()));
			return;
		}
	}

	void TickWorld(UWorld* W)
	{
		if (!IsMapWorld(W)) { return; }
		ASDA_PERF_SCOPE("AnimThrottle.TickWorld");   // R4, доля a: кадровый шаг урезания анимации (в Shipping пусто)
		// Миры, которых больше нет, — вон (переход между картами).
		for (auto It = GStates.CreateIterator(); It; ++It)
		{
			if (!It.Value().World.IsValid()) { It.RemoveCurrent(); }
		}
		FWorldState& S = GStates.FindOrAdd(TObjectKey<UWorld>(W));
		if (!S.World.IsValid())
		{
			S.World = W;
			// Новые акторы (мобы/NPC/игроки/питомцы приходят по сети) — в очередь; разбор со следующего кадра,
			// когда спавнер уже выставил меш и (для своего персонажа) Possess.
			TWeakObjectPtr<UWorld> WeakW(W);
			S.SpawnHandle = W->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([WeakW](AActor* A)
			{
				if (FWorldState* St = GStates.Find(TObjectKey<UWorld>(WeakW.Get())))
				{
					St->Pending.Add(A);
					St->PendingFrame = GFrameCounter;
				}
			}));
		}
		if (!S.bInitialScanDone)
		{
			S.bInitialScanDone = true;
			for (TActorIterator<AActor> It(W); It; ++It) { AddActor(S, *It); }
			S.AppliedGen = GAsdaAnimThrottleGen;
			int32 Np = 0, Nm = 0, Nn = 0, Npe = 0, Npt = 0;
			const int32 N = NumThrottled(&Np, &Nm, &Nn, &Npe, &Npt);
			UE_LOG(LogAsdaAnimThr, Log, TEXT("[анимация] урезание %s: скелетов %d (декорации %d, мобы %d, NPC %d, игроки %d, питомцы %d)"),
				GAsdaAnimThrottle ? TEXT("вкл") : TEXT("выкл"), S.Recs.Num(), Np, Nm, Nn, Npe, Npt);
			(void)N;
		}
		if (S.Pending.Num() > 0 && GFrameCounter > S.PendingFrame)
		{
			TArray<TWeakObjectPtr<AActor>> Batch = MoveTemp(S.Pending);
			S.Pending.Reset();
			for (const TWeakObjectPtr<AActor>& A : Batch) { AddActor(S, A.Get()); }
		}
		if (S.AppliedGen != GAsdaAnimThrottleGen)
		{
			S.AppliedGen = GAsdaAnimThrottleGen;
			S.Held.Reset();
			for (FRec& R : S.Recs) { ApplyRecIn(S, R); }
			UE_LOG(LogAsdaAnimThr, Log, TEXT("[анимация] ключи изменены: урезание %s, интерполяция %d, замах без урезания %d, записей %d"),
				GAsdaAnimThrottle ? TEXT("вкл") : TEXT("выкл"), GAsdaAnimThrottleInterp, GAsdaAnimThrottleCombatHold, S.Recs.Num());
		}
		// ★ 30.10.2026: чужие игроки, у которых кончился цикл замаха, — урезание обратно (их обычно 0–несколько).
		for (int32 i = S.Held.Num() - 1; i >= 0; --i)
		{
			USkeletalMeshComponent* C = S.Held[i].Get();
			if (C && IsCombatClockBusy(C)) { continue; }
			S.Held.RemoveAtSwap(i);
			if (!C) { continue; }
			if (FRec* Rp = FindRec(S, C)) { ApplyRecIn(S, *Rp); }   // ★ R4: запись — `FindRec` (прежде — линейный поиск до первого совпадения)
		}
		// Медленный проход (раз в ~120 кадров): мёртвые записи вон, интерполяция тем, у кого трекер появился позже.
		if (GFrameCounter >= S.NextSweepFrame)
		{
			S.NextSweepFrame = GFrameCounter + 120;
			const int32 Before = S.Recs.Num();
			S.Recs.RemoveAllSwap([](const FRec& R) { return !R.Comp.IsValid(); });
			if (S.Recs.Num() != Before)
			{
				S.Known.Reset();
				for (const FRec& R : S.Recs) { S.Known.Add(TObjectKey<USkeletalMeshComponent>(R.Comp.Get())); }
				S.Index.Reset();   // ★ R4: после удаления мёртвых записей номера сдвинулись — карта заново
				for (int32 i = 0; i < S.Recs.Num(); ++i) { S.Index.Add(TObjectKey<USkeletalMeshComponent>(S.Recs[i].Comp.Get()), i); }
			}
			if (GAsdaAnimThrottle != 0)
			{
				for (FRec& R : S.Recs)
				{
					if (R.bApplied && !R.bInterpSet) { if (USkeletalMeshComponent* C = R.Comp.Get()) { SetInterp(R, C); } }
				}
			}
			// Страховка: «чужой игрок», ставший своим (Possess пришёл позже учёта), — откат и исключение навсегда.
			for (FRec& R : S.Recs)
			{
				if (R.Role != ERole::Peer) { continue; }
				USkeletalMeshComponent* C = R.Comp.Get();
				if (C && IsLocalSelf(C->GetOwner()))
				{
					const int32 Saved = GAsdaAnimThrottle;
					GAsdaAnimThrottle = 0; ApplyRec(R); GAsdaAnimThrottle = Saved;   // откат к прежним значениям
					R.Role = ERole::Unknown;
					UE_LOG(LogAsdaAnimThr, Log, TEXT("[анимация] свой персонаж %s снят с урезания"), *GetNameSafe(C->GetOwner()));
				}
			}
		}
	}

	int32 NumThrottled(int32* OutProps, int32* OutMobs, int32* OutNpcs, int32* OutPeers, int32* OutPets)
	{
		int32 N = 0, Np = 0, Nm = 0, Nn = 0, Npe = 0, Npt = 0;
		for (const auto& KV : GStates)
		{
			for (const FRec& R : KV.Value.Recs)
			{
				if (!R.bApplied || !R.Comp.IsValid()) { continue; }
				++N;
				switch (R.Role)
				{
				case ERole::Prop: ++Np; break;
				case ERole::Mob:  ++Nm; break;
				case ERole::Npc:  ++Nn; break;
				case ERole::Peer: ++Npe; break;
				case ERole::Pet:  ++Npt; break;
				default: break;
				}
			}
		}
		if (OutProps) { *OutProps = Np; }
		if (OutMobs) { *OutMobs = Nm; }
		if (OutNpcs) { *OutNpcs = Nn; }
		if (OutPeers) { *OutPeers = Npe; }
		if (OutPets) { *OutPets = Npt; }
		return N;
	}
}

#if ASDA_DEV_CONSOLE   // ★ дев-команда дампа, в Shipping не регистрируется
// asda.Anim.ThrottleDump — сколько скелетов под урезанием по ролям (проверка, что ключ подействовал).
static FAutoConsoleCommand GAsdaAnimThrottleDump(
	TEXT("asda.Anim.ThrottleDump"),
	TEXT("Печатает число скелетов под урезанием анимации по ролям (декорации/мобы/NPC/чужие игроки/питомцы)."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		int32 Np = 0, Nm = 0, Nn = 0, Npe = 0, Npt = 0;
		const int32 N = AsdaAnimThrottle::NumThrottled(&Np, &Nm, &Nn, &Npe, &Npt);
		UE_LOG(LogAsdaAnimThr, Log, TEXT("[анимация] под урезанием %d: декорации %d, мобы %d, NPC %d, игроки %d, питомцы %d (ключ %d)"),
			N, Np, Nm, Nn, Npe, Npt, GAsdaAnimThrottle);
	}));
#endif // ASDA_DEV_CONSOLE
