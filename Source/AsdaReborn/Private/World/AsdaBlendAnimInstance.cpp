// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ПЛАВНОСТЬ АНИМАЦИЙ (22.10.2026) — разбор, эталон и ключи в заголовке `World/AsdaBlendAnimInstance.h`.
#include "World/AsdaBlendAnimInstance.h"

#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AnimNodeBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "World/AsdaAnimThrottle.h"   // ★ 31.10: удержание полной частоты позы на время вздрагивания
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"   // мировое время хода бленда

DEFINE_LOG_CATEGORY_STATIC(LogAsdaAnimSmooth, Log, All);

// ── Ключи ─────────────────────────────────────────────────────────────────────────────────────────
static float GAsdaAnimBlendSec = 0.15f;
static FAutoConsoleVariableRef CVarAsdaAnimBlendSec(TEXT("asda.Anim.BlendSec"), GAsdaAnimBlendSec,
	TEXT("Смешивание клипов покоя/бега/стопа/фиджета/сидения/эмоций/смерти, с. 0.15 — по умолчанию [ПОРТ: по слову владельца; у оригинала 0.06]; ")
	TEXT("0 — смена клипа в один кадр (прежний порт)."), ECVF_Default);
static float GAsdaAnimBlendAttackSec = 0.06f;
static FAutoConsoleVariableRef CVarAsdaAnimBlendAttackSec(TEXT("asda.Anim.BlendAttackSec"), GAsdaAnimBlendAttackSec,
	TEXT("Смешивание при входе в клип замаха/навыка, с. 0.06 — [ОРИГИНАЛ] 60 мс (charAnim_UpdateActionBlend@0xAB3060); 0 — в один кадр. ")
	TEXT("Время клипа не сдвигается: момент удара, звуки и эффекты остаются на своих кадрах."), ECVF_Default);
static int32 GAsdaAnimBlendLive = 1;
static FAutoConsoleVariableRef CVarAsdaAnimBlendLive(TEXT("asda.Anim.BlendLive"), GAsdaAnimBlendLive,
	TEXT("1 — прежний клип доигрывает своё время во время бленда [ИСПРАВЛЕНИЕ]; 0 — замёрзшая поза прежнего клипа [ОРИГИНАЛ]."), ECVF_Default);
static int32 GAsdaAnimBlendCurve = 1;
static FAutoConsoleVariableRef CVarAsdaAnimBlendCurve(TEXT("asda.Anim.BlendCurve"), GAsdaAnimBlendCurve,
	TEXT("1 — плавная кривая веса бленда (smoothstep) [ИСПРАВЛЕНИЕ]; 0 — линейная [ОРИГИНАЛ]."), ECVF_Default);
static float GAsdaAnimTurnDegPerSec = 600.f;
static FAutoConsoleVariableRef CVarAsdaAnimTurnDegPerSec(TEXT("asda.Anim.TurnDegPerSec"), GAsdaAnimTurnDegPerSec,
	TEXT("Скорость доворота модели игрока (свой и соседи), °/с. 600 — [ОРИГИНАЛ] 0.0104666 рад/мс (objAI_FaceTargetByUserIdx@0xA23350); ")
	TEXT("0 — мгновенно (прежний порт). Логический курс (пакеты, мини-карта) ставится сразу при любом значении."), ECVF_Default);
static float GAsdaAnimMobTurnDegPerSec = 360.f;
static FAutoConsoleVariableRef CVarAsdaAnimMobTurnDegPerSec(TEXT("asda.Anim.MobTurnDegPerSec"), GAsdaAnimMobTurnDegPerSec,
	TEXT("Скорость доворота модели моба, °/с. 360 — [ОРИГИНАЛ] 0.0062831854 рад/мс (charUpdateRotateAndFx@0xF86CB0); 0 — мгновенно (прежний порт)."),
	ECVF_Default);
static float GAsdaAnimTurnEaseSec = 0.05f;
static FAutoConsoleVariableRef CVarAsdaAnimTurnEaseSec(TEXT("asda.Anim.TurnEaseSec"), GAsdaAnimTurnEaseSec,
	TEXT("Мягкое торможение доворота у цели, с (последние «скорость × N» градусов — экспонента) [ИСПРАВЛЕНИЕ]; 0 — постоянная скорость до упора [ОРИГИНАЛ]."),
	ECVF_Default);
static float GAsdaAnimSnapSmoothSec = 0.12f;
static FAutoConsoleVariableRef CVarAsdaAnimSnapSmoothSec(TEXT("asda.Anim.SnapSmoothSec"), GAsdaAnimSnapSmoothSec,
	TEXT("«Хвост» сетевой поправки позиции игрока (4009, 4007 result 5, рассинхрон > 150): тело догоняет точку сервера экспонентой с этой ")
	TEXT("постоянной времени, с. Актор (логика, камера, плашка) встаёт в точку сразу. [ИСПРАВЛЕНИЕ: у оригинала снап]; 0 — снап (прежний порт)."),
	ECVF_Default);
static float GAsdaAnimSnapSmoothMax = 400.f;
static FAutoConsoleVariableRef CVarAsdaAnimSnapSmoothMax(TEXT("asda.Anim.SnapSmoothMaxUnits"), GAsdaAnimSnapSmoothMax,
	TEXT("Поправка длиннее (игровых единиц) — настоящий телепорт: без «хвоста»."), ECVF_Default);
static int32 GAsdaAnimTrace = 0;
static FAutoConsoleVariableRef CVarAsdaAnimTrace(TEXT("asda.Anim.Trace"), GAsdaAnimTrace,
	TEXT("1 — строка «[аним-плавность]» в журнал на каждую смену клипа со смешиванием (клип, источник, длительность)."), ECVF_Default);

// ★ 29.10.2026 вздрагивание от удара (разбор — у `UAsdaBlendAnimInstance::StartHitFlinchGT` в заголовке).
static int32 GAsdaCharHitFlinch = 1;
static FAutoConsoleVariableRef CVarAsdaCharHitFlinch(TEXT("asda.Char.HitFlinch"), GAsdaCharHitFlinch,
	TEXT("1 — персонаж вздрагивает от удара: 125 мс поворот кости головы «4» на −0.5 рад [ОРИГИНАЛ gfxBias_LazyInitHalfTexel@0xA2C7B0 + ")
	TEXT("charEntityUpdateTick@0xA1ED30]; 0 — без реакции (прежний порт)."), ECVF_Default);
static float GAsdaCharHitFlinchMs = 125.f;
static FAutoConsoleVariableRef CVarAsdaCharHitFlinchMs(TEXT("asda.Char.HitFlinchMs"), GAsdaCharHitFlinchMs,
	TEXT("Длительность вздрагивания, мс. 125 — [ОРИГИНАЛ] (fcomp [0x13AE568] = 125.0f в 0xA1FD83)."), ECVF_Default);
static float GAsdaCharHitFlinchTintGB = 0.3f;
static FAutoConsoleVariableRef CVarAsdaCharHitFlinchTintGB(TEXT("asda.Char.HitFlinchTintGB"), GAsdaCharHitFlinchTintGB,
	TEXT("Покраснение во вздрагивании: множитель зелёного и синего каналов модели, 0.3 — [ОРИГИНАЛ] амбиент +360 = 0.3 при точечном (1,0,0) ")
	TEXT("(render_core.c:3747, entity_char.c:22790); 1 — без покраснения. Материал: CPD[1] = 1 − значение."), ECVF_Default);
static float GAsdaCharHitFlinchRad = -0.5f;
static FAutoConsoleVariableRef CVarAsdaCharHitFlinchRad(TEXT("asda.Char.HitFlinchRad"), GAsdaCharHitFlinchRad,
	TEXT("Угол вздрагивания вокруг оси кости «4», рад. −0.5 — [ОРИГИНАЛ] (char[52287] = −0.5, render_core.c:3743)."), ECVF_Default);

namespace AsdaAnimSmooth
{
	bool  HitFlinchOn()        { return GAsdaCharHitFlinch != 0; }
	float HitFlinchSec()       { return FMath::Max(0.f, GAsdaCharHitFlinchMs) / 1000.f; }

	bool StartHitFlinch(USkeletalMeshComponent* Comp)
	{
		UAsdaBlendAnimInstance* I = Comp ? Cast<UAsdaBlendAnimInstance>(Comp->GetAnimInstance()) : nullptr;
		if (!I || !I->StartHitFlinchGT()) { return false; }
		// ★ 31.10.2026: чужой игрок под урезанием анимации — на время вздрагивания полная частота (иначе окно 125 мс пропускается).
		AsdaAnimThrottle::HoldCombatClock(Comp);
		return true;
	}

	float BlendSec()           { return FMath::Max(0.f, GAsdaAnimBlendSec); }
	float BlendAttackSec()     { return FMath::Max(0.f, GAsdaAnimBlendAttackSec); }
	bool  BlendLive()          { return GAsdaAnimBlendLive != 0; }
	bool  BlendCurve()         { return GAsdaAnimBlendCurve != 0; }
	float TurnDegPerSec()      { return FMath::Max(0.f, GAsdaAnimTurnDegPerSec); }
	float MobTurnDegPerSec()   { return FMath::Max(0.f, GAsdaAnimMobTurnDegPerSec); }
	float TurnEaseSec()        { return FMath::Max(0.f, GAsdaAnimTurnEaseSec); }
	float SnapSmoothSec()      { return FMath::Max(0.f, GAsdaAnimSnapSmoothSec); }
	float SnapSmoothMaxUnits() { return FMath::Max(0.f, GAsdaAnimSnapSmoothMax); }
	bool  Trace()              { return GAsdaAnimTrace != 0; }

	void Install(USkeletalMeshComponent* Comp)
	{
		if (!Comp) { return; }
		// В конструкторе меша ещё нет — `SetAnimationMode`/`AnimClass` только запоминаются; инстанс создаст регистрация.
		//   Вне конструктора (компонент создан на лету) — штатный `SetAnimInstanceClass` пересоздаёт инстанс сам.
		if (Comp->IsRegistered()) { Comp->SetAnimInstanceClass(UAsdaBlendAnimInstance::StaticClass()); return; }
		Comp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Comp->AnimClass = UAsdaBlendAnimInstance::StaticClass();
	}

	bool IsInstalled(const USkeletalMeshComponent* Comp)
	{
		return Comp && Comp->GetAnimationMode() == EAnimationMode::AnimationBlueprint && Comp->AnimClass
			&& Comp->AnimClass->IsChildOf(UAsdaBlendAnimInstance::StaticClass());
	}

	void PlayClip(USkeletalMeshComponent* Comp, UAnimationAsset* Clip, bool bLoop, float BlendSecOverride)
	{
		if (!Comp) { return; }
		if (!Comp->bEnableAnimation) { Comp->PlayAnimation(Clip, bLoop); return; }   // штатный путь сам откажет (режим не меняет)
		UAsdaBlendAnimInstance* I = Cast<UAsdaBlendAnimInstance>(Comp->GetAnimInstance());
		if (!I)
		{
			if (IsInstalled(Comp))
			{
				// Инстанса ещё нет (меш не назначен / компонент не зарегистрирован): отложенный запуск БЕЗ смены режима —
				//   `UAnimSingleNodeInstance::NativeInitializeAnimation` возьмёт клип из `AnimationData` (бленда нет: первый клип).
				Comp->AnimationData.AnimToPlay = Clip;
				Comp->AnimationData.bSavedLooping = bLoop;
				Comp->AnimationData.bSavedPlaying = true;
				Comp->AnimationData.SavedPosition = 0.f;
				Comp->AnimationData.SavedPlayRate = 1.f;
				return;
			}
			// Компонент не наш (режим уже сменён чужим `PlayAnimation` или класс не поставлен) — штатный путь, смешивания нет.
			UE_LOG(LogAsdaAnimSmooth, Warning, TEXT("[аним-плавность] %s.%s: на компоненте нет инстанса со смешиванием — клип %s штатным PlayAnimation (без бленда)"),
				*GetNameSafe(Comp->GetOwner()), *Comp->GetName(), *GetNameSafe(Clip));
			Comp->PlayAnimation(Clip, bLoop);
			return;
		}
		// Ровно то, что делает `USkeletalMeshComponent::PlayAnimation` (SetAnimation + Play), но без смены режима.
		I->SetNextBlendSec(BlendSecOverride);
		I->SetAnimationAsset(Clip, false);
		I->SetPlaying(false);
		I->SetPlaying(true);
		I->SetLooping(bLoop);
	}

	float StepYaw(float CurYaw, float TargetYaw, float DegPerSec, float DeltaSeconds)
	{
		const float Delta = FMath::FindDeltaAngleDegrees(CurYaw, TargetYaw);   // кратчайший путь, −180…180
		const float AbsD = FMath::Abs(Delta);
		if (DegPerSec <= 0.f || AbsD <= 0.01f) { return FRotator::NormalizeAxis(TargetYaw); }
		float Rate = DegPerSec;
		// [ИСПРАВЛЕНИЕ] торможение у цели: скорость не выше «остаток / Ease» (экспонента), с полом 30 °/с, чтобы дойти до конца.
		const float Ease = TurnEaseSec();
		if (Ease > 0.f) { Rate = FMath::Min(DegPerSec, FMath::Max(AbsD / Ease, 30.f)); }
		const float Step = FMath::Min(AbsD, Rate * FMath::Max(0.f, DeltaSeconds));
		return FRotator::NormalizeAxis(CurYaw + FMath::Sign(Delta) * Step);
	}
}

// ── Прокси ────────────────────────────────────────────────────────────────────────────────────────
void FAsdaBlendNodeProxy::Initialize(UAnimInstance* InAnimInstance)
{
	FAnimSingleNodeInstanceProxy::Initialize(InAnimInstance);
	BlendTotal = 0.f; BlendElapsed = 0.f; FromSeq = nullptr; FromSnap.Reset(); LastAlpha = 1.f;
}

void FAsdaBlendNodeProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimSingleNodeInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	if (const UWorld* W = InAnimInstance ? InAnimInstance->GetWorld() : nullptr) { WorldNowS = W->GetTimeSeconds(); }
	// ★ 29.10.2026: вздрагивание — данные игрового потока копируются здесь (PreUpdate идёт на игровом потоке).
	if (const UAsdaBlendAnimInstance* I = Cast<UAsdaBlendAnimInstance>(InAnimInstance))
	{
		FlinchStartS = I->FlinchStartS;
		FlinchRad = I->FlinchRad;
	}
}

void FAsdaBlendNodeProxy::ApplyHitFlinch(FPoseContext& Output)
{
	bFlinchApplied = false;
	if (FlinchStartS < 0.0 || !AsdaAnimSmooth::HitFlinchOn()) { return; }
	// Эталон: поворот лежит, пока таймер (после прибавки dt) < 125 мс; дальше — поза клипа.
	if (WorldNowS - FlinchStartS >= (double)AsdaAnimSmooth::HitFlinchSec()) { return; }
	const FBoneContainer& BC = Output.Pose.GetBoneContainer();
	static const FName kHeadBone(TEXT("4"));   // `char[52088]` — индекс кости "4" (entity_char.c:21238)
	const int32 Idx = BC.GetPoseBoneIndexForBoneName(kHeadBone);
	if (Idx == INDEX_NONE) { return; }   // [ПОРТ] эталон без кости вращает всю модель; у тела игрока кость «4» есть всегда
	const FCompactPoseBoneIndex CI(Idx);
	if (!Output.Pose.IsValidIndex(CI)) { return; }
	FTransform& T = Output.Pose[CI];
	// `bone = RotY(a) · bone` (row-векторы) = поворот в СОБСТВЕННЫХ осях кости ⇒ кватернион справа; RotY эталона = Yaw кости UE.
	T.SetRotation((T.GetRotation() * FQuat(FVector::ZAxisVector, FlinchRad)).GetNormalized());
	bFlinchApplied = true;
}

void FAsdaBlendNodeProxy::UpdateAnimationNode(const FAnimationUpdateContext& InContext)
{
	FAnimSingleNodeInstanceProxy::UpdateAnimationNode(InContext);
	if (!(BlendTotal > 0.f)) { return; }
	const float Dt = InContext.GetDeltaTime();
	// Ход бленда — по МИРОВОМУ времени: у моба вне экрана поза не тикает, и после возвращения бленд не доигрывает
	//   устаревший источник (прошло больше длительности — бленд окончен).
	BlendElapsed = FMath::Max(0.f, (float)(WorldNowS - BlendStartWorldS));
	if (bFromLive && FromSeq)
	{
		// Живой источник доигрывает своё время в своём темпе (петля — по кругу, одиночный — до последнего кадра).
		FromTime += Dt * FromRate;
		const float L = FromSeq->GetPlayLength();
		if (L > 0.f)
		{
			if (bFromLoop) { FromTime = FMath::Fmod(FromTime, L); if (FromTime < 0.f) { FromTime += L; } }
			else           { FromTime = FMath::Clamp(FromTime, 0.f, L); }
		}
	}
	if (BlendElapsed >= BlendTotal)
	{
		BlendTotal = 0.f; BlendElapsed = 0.f; FromSeq = nullptr; FromSnap.Reset();   // бленд окончен — дальше чистый клип
	}
}

bool FAsdaBlendNodeProxy::Evaluate(FPoseContext& Output)
{
	FAnimSingleNodeInstanceProxy::Evaluate(Output);   // поза нового клипа в его собственном времени
	++EvalSerial;
	if (!(BlendTotal > 0.f) || BlendElapsed >= BlendTotal) { LastAlpha = 1.f; ApplyHitFlinch(Output); return true; }
	float A = FMath::Clamp(BlendElapsed / BlendTotal, 0.f, 1.f);   // вес НОВОГО клипа
	if (bSmoothCurve) { A = A * A * (3.f - 2.f * A); }
	LastAlpha = A;
	if (bFromLive && FromSeq)
	{
		FPoseContext From(Output);
		FAnimationPoseData FromData(From);
		FromSeq->GetAnimationPose(FromData, FAnimExtractContext((double)FromTime, false, FDeltaTimeRecord(), bFromLoop));
		for (const FCompactPoseBoneIndex I : Output.Pose.ForEachBoneIndex())
		{
			FTransform T;
			T.Blend(From.Pose[I], Output.Pose[I], A);
			Output.Pose[I] = T;
		}
	}
	else if (FromSnap.Num() > 0)
	{
		const FBoneContainer& BC = Output.Pose.GetBoneContainer();
		for (const FCompactPoseBoneIndex I : Output.Pose.ForEachBoneIndex())
		{
			const int32 M = BC.MakeMeshPoseIndex(I).GetInt();
			if (!FromSnap.IsValidIndex(M)) { continue; }
			FTransform T;
			T.Blend(FromSnap[M], Output.Pose[I], A);
			Output.Pose[I] = T;
		}
	}
	ApplyHitFlinch(Output);   // ★ 29.10.2026: вздрагивание — поверх смешанной позы
	return true;
}

// ── Инстанс ───────────────────────────────────────────────────────────────────────────────────────
void UAsdaBlendAnimInstance::SetAnimationAsset(UAnimationAsset* NewAsset, bool bIsLooping, float InPlayRate)
{
	FAsdaBlendNodeProxy& P = GetProxyOnGameThread<FAsdaBlendNodeProxy>();
	const float Sec = (NextBlendSec >= 0.f) ? NextBlendSec : AsdaAnimSmooth::BlendSec();
	NextBlendSec = -1.f;
	USkeletalMeshComponent* Comp = GetSkelMeshComponent();
	UAnimationAsset* Prev = CurrentAsset;
	const bool bCan = Sec > 0.f && Prev && NewAsset && Comp && Comp->GetSkeletalMeshAsset();
	const double NowS = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const bool bShownSince = (P.EvalSerial != EvalSerialAtLastSet);   // поза оценивалась (показывалась) с прошлой смены
	const bool bSameFrame = (GFrameCounter == FrameAtLastSet);
	auto NoBlend = [&](EAsdaBlendSource Why)
	{
		P.BlendTotal = 0.f; P.BlendElapsed = 0.f; P.FromSeq = nullptr; P.FromSnap.Reset(); FromSeqRef = nullptr;
		LastBlendSec = 0.f; LastSource = Why;
	};
	if (!bCan)
	{
		// Без бленда (ключ 0, первый клип после создания/смены меша, пустой клип) — прежний порт дословно.
		NoBlend(EAsdaBlendSource::None);
	}
	else if (!bShownSince && bSameFrame)
	{
		// Вторая смена в ТОМ ЖЕ кадре до показа первой: источник остаётся прежним — это поза, которую видел игрок.
		if (P.BlendTotal > 0.f) { P.BlendStartWorldS = NowS; P.BlendElapsed = 0.f; P.BlendTotal = Sec; LastBlendSec = Sec; LastSource = EAsdaBlendSource::SameFrame; }
	}
	else if (!bShownSince)
	{
		// Поза с прошлой смены не показывалась (моб вне экрана, `OnlyTickPoseWhenRendered`): смешивать не с чем —
		//   иначе при возвращении на экран мелькнула бы устаревшая поза-источник.
		NoBlend(EAsdaBlendSource::Unseen);
	}
	else
	{
		const bool bChain = P.BlendTotal > 0.f && P.BlendElapsed < P.BlendTotal;   // смена посреди бленда
		UAnimSequenceBase* PrevSeq = Cast<UAnimSequenceBase>(Prev);
		if (!bChain && AsdaAnimSmooth::BlendLive() && PrevSeq)
		{
			P.bFromLive = true; P.FromSeq = PrevSeq; FromSeqRef = PrevSeq;
			P.FromTime = P.GetCurTime(); P.FromRate = P.IsPlaying() ? P.GetPlayRate() : 0.f; P.bFromLoop = P.IsLooping();
			P.FromSnap.Reset();
		}
		else
		{
			// Снимок показанной позы (при цепочке — уже смешанной): метод оригинала, поза источника замёрзшая.
			P.bFromLive = false; P.FromSeq = nullptr; FromSeqRef = nullptr;
			P.FromSnap = Comp->GetBoneSpaceTransforms();
		}
		P.bSmoothCurve = AsdaAnimSmooth::BlendCurve();
		P.BlendStartWorldS = NowS; P.WorldNowS = NowS; P.BlendElapsed = 0.f; P.BlendTotal = Sec; LastBlendSec = Sec;
		LastSource = P.bFromLive ? EAsdaBlendSource::Live : (bChain ? EAsdaBlendSource::ChainSnapshot : EAsdaBlendSource::Snapshot);
		if (AsdaAnimSmooth::Trace())
		{
			UE_LOG(LogAsdaAnimSmooth, Log, TEXT("[аним-плавность] %s: %s → %s, бленд %.3f с, источник %s (время %.3f)"),
				*GetNameSafe(Comp->GetOwner()), *GetNameSafe(Prev), *GetNameSafe(NewAsset), Sec,
				P.bFromLive ? TEXT("живой клип") : (bChain ? TEXT("снимок (цепочка)") : TEXT("снимок")), P.bFromLive ? P.FromTime : 0.f);
		}
	}
	EvalSerialAtLastSet = P.EvalSerial;
	FrameAtLastSet = GFrameCounter;
	Super::SetAnimationAsset(NewAsset, bIsLooping, InPlayRate);
}

bool UAsdaBlendAnimInstance::IsBlendingGT()
{
	const FAsdaBlendNodeProxy& P = GetProxyOnGameThread<FAsdaBlendNodeProxy>();
	return P.BlendTotal > 0.f && P.BlendElapsed < P.BlendTotal;
}

float UAsdaBlendAnimInstance::GetBlendAlphaGT()
{
	const FAsdaBlendNodeProxy& P = GetProxyOnGameThread<FAsdaBlendNodeProxy>();
	return (P.BlendTotal > 0.f && P.BlendElapsed < P.BlendTotal) ? P.LastAlpha : 1.f;
}

bool UAsdaBlendAnimInstance::StartHitFlinchGT()
{
	if (!AsdaAnimSmooth::HitFlinchOn()) { return false; }
	const UWorld* W = GetWorld();
	if (!W) { return false; }
	// Флаг 0x200 уже стоит — эталон старт пропускает (`if ((char[47552] & 0x200) == 0)`, render_core.c:3739).
	if (IsHitFlinchActiveGT()) { return false; }
	FlinchStartS = W->GetTimeSeconds();
	FlinchRad = GAsdaCharHitFlinchRad;
	// Покраснение — на всё время вздрагивания (эталон: +360/+640 ставятся на старте и каждый тик, снимаются по t > 125).
	ApplyFlinchTint(1.f - FMath::Clamp(GAsdaCharHitFlinchTintGB, 0.f, 1.f));
	TWeakObjectPtr<UAsdaBlendAnimInstance> WeakThis(this);
	const_cast<UWorld*>(W)->GetTimerManager().SetTimer(FlinchTintTimer, FTimerDelegate::CreateWeakLambda(this, [WeakThis]()
	{
		if (UAsdaBlendAnimInstance* I = WeakThis.Get()) { I->ApplyFlinchTint(0.f); }
	}), FMath::Max(0.01f, AsdaAnimSmooth::HitFlinchSec()), false);
	return true;
}

void UAsdaBlendAnimInstance::ApplyFlinchTint(float F)
{
	const USkeletalMeshComponent* Comp = GetSkelMeshComponent();
	AActor* Owner = Comp ? Comp->GetOwner() : nullptr;
	if (!Owner) { return; }
	// Только меши модели (скелеталы и статики частей/оружия); плашки-виджеты (тоже UMeshComponent) и эффекты (отдельные акторы) — мимо.
	TInlineComponentArray<UMeshComponent*> Meshes(Owner);
	for (UMeshComponent* M : Meshes)
	{
		if (M && (M->IsA<USkeletalMeshComponent>() || M->IsA<UStaticMeshComponent>()))
		{
			M->SetCustomPrimitiveDataFloat(1, F);   // индекс 1 — параметр HitFlinchTint мастеров персонажа
		}
	}
}

bool UAsdaBlendAnimInstance::IsHitFlinchActiveGT() const
{
	const UWorld* W = GetWorld();
	return W && FlinchStartS >= 0.0 && (W->GetTimeSeconds() - FlinchStartS) < (double)AsdaAnimSmooth::HitFlinchSec();
}

bool UAsdaBlendAnimInstance::WasHitFlinchAppliedGT()
{
	return GetProxyOnGameThread<FAsdaBlendNodeProxy>().bFlinchApplied;
}
