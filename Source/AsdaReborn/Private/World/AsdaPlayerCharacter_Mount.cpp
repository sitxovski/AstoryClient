// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPlayerCharacter_Mount.cpp — ★ R3-c 12.11.2026 (разрез AsdaPlayerCharacter_Move.cpp, дословный перенос): транспорт: посадка/снятие, клипы, след и вспышка, звук; флаг клана.
//   Общее для частей — в AsdaPlayerCharacter_Move_Private.h; шапка и реверс — в AsdaPlayerCharacter_Move.cpp.
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaClanFlag.h"   // ★ R3: прямое включение (раньше приходило транзитивно)
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "World/AsdaPlayerController.h"   // ★ ритм ударов задаёт тик анимации (OnSwingCycleCompleted)
#include "Misc/App.h"   // FApp::GetDeltaTime — оценка кадра (точка отправки за кадр до дедлайна)
#include "Materials/MaterialInterface.h"
#include "Components/StaticMeshComponent.h"
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д8/Д9: ресурс только через менеджер
#include "Resources/AsdaSoundSubsystem.h"       // ★ Аудит №3 (A9): звук транспорта — только через менеджер
#include "World/AsdaNameplateComponent.h"
#include "Components/MeshComponent.h"     // ★ режим «душа»: подмена слотов материалов тела и частей
#include "World/AsdaWorldSubsystem.h"   // LocalPlayerActor — «свой» персонаж (звук 2D против 3D)
#include "Engine/GameInstance.h"         // ★ В5fix: GetGameInstance()->GetSubsystem (сборка без unity)
#include "AsdaEffectActor.h"            // шлейф езды = AAsdaEffectActor (attach-режим 6)
#include "Resources/AsdaMaterialNames.h"   // ★ дедуп №2 В1-C (F10): пути мастеров и имена параметров
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"                 // ★ 10.10.2026 (копание): меш инструмента на `8_a1`
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/TextureRenderTarget2D.h"
#include "World/AsdaCombatTrace.h"         // ★ 18.10.2026: журнал боя — замах, кадры удара, стойка, смерть персонажа
#include "Npc/AsdaMobActor.h"              // ★ 18.10.2026: журнал боя — имя и rid цели разворота в замахе
#include "World/AsdaBlendAnimInstance.h"   // ★ 22.10.2026: плавность — смешивание клипов, доворот модели, хвост сетевых поправок
#include "AsdaPlayerCharacterInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaPlayerCharacter*.cpp (LogAsdaPlayer, чистые хелперы)
#include "AsdaNetSubsystem.h"              // ★ 23.10.2026 (прыжок донора): 4024 код 131
#include "UI/AsdaPremiumGachaSubsystem.h"  // ★ … сцена премиум-гачи
#include "Framework/Application/SlateApplication.h"   // ★ … фокус в любом поле ввода
#include "Resources/AsdaGameUserSettings.h"           // ★ 25.10.2026 (присед): режим Ctrl «Нажатие/Зажатие» из окна настроек
#include "AsdaPlayerCharacter_Move_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaPlayerCharacter_Move*.cpp

// ── ★ ТРАНСПОРТ (mount_system_re.md): меш в матрице персонажа + подмена клипов на сидячие ──
FString AAsdaPlayerCharacter::MountClipPath(const TCHAR* Variant) const
{
	// Ассеты импорта: ch1_{m|f}_cremmy_previewsit_<NNNN>_<VVV> (000 покой, 001 шаг, 002 движение).
	return FString::Printf(TEXT("%ssit_%s_%s"), *CharBase(Sex), *MountAnimSet, Variant);
}

void AAsdaPlayerCharacter::SetMount(const FString& ModelName, const FString& AnimSet, float HeightOffset,
	const FString& RideEffect, const FString& RideSound, const FString& RideSound2, const FString& RideSound3)
{
	CancelJump(TEXT("посадка"));   // ★ 23.10.2026: верхом прыжка нет (донор)
	// ★ 24.10.2026 [ПОРТ] верхом приседа нет: свой присед снимается (статус хода снова 2 — сервер вернёт обычную скорость).
	if (bCrouchOwn) { bCrouchOwn = false; LastCrouchRefusal = TEXT("снят: посадка"); }
	bCrouchNet = false;   // ★ 05.11.2026 (присед стоя): сосед верхом — после высадки не в позе приседа
	MountAnimSet = AnimSet;
	MountModelName = ModelName;
	bMounted = true;
	UpdateSwimState(true);         // ★ 23.10.2026: в глубокой воде седок не плывёт — транспорт держится на воде (донор)
	// Высоту посадки раньше отбрасывали; её читает компаньон-хранитель (см. объявление в .h).
	MountHeightOffset = HeightOffset;
	// Меш транспорта: эталон рисует его в ТОЙ ЖЕ мировой матрице персонажа — у нас это тот же MeshRoot
	//   (масштаб/зеркало общие), локальный трансформ единичный. Своя анимация транспорта: anim_00 покой.
	if (!MountMesh)
	{
		MountMesh = NewObject<USkeletalMeshComponent>(this, TEXT("MountMesh"));
		MountMesh->SetupAttachment(MeshRoot);
		MountMesh->SetRelativeTransform(FTransform::Identity);
		AsdaAnimSmooth::Install(MountMesh);   // ★ 22.10.2026: смешивание клипов транспорта синхронно с сидячими клипами седока
		MountMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		MountMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MountMesh->SetCastShadow(true);
		MountMesh->RegisterComponent();
	}
	const FString MeshPath = UAsdaModelAnimSubsystem::BuildSkelPath(   // Д8
		AsdaAssetPaths::kSkelRootVehicle, ModelName);   // ★ волна 2 п. 7: корень — реестр путей
	if (USkeletalMesh* M = UAsdaModelAnimSubsystem::LoadMeshChecked(MeshPath))   // Д2
	{
		MountMesh->SetSkeletalMesh(M);
		MountMesh->SetHiddenInGame(false);
		// Начальный клип по текущему состоянию; ниже PlayWorldClip перезапустит ОБА синхронно (PlayMountClip).
		PlayMountClip(AnimState == EAsdaPlayerState::Move);
	}
	else
	{
		UE_LOG(LogAsdaPlayer, Warning, TEXT("[транспорт] модель '%s' не импортирована"), *ModelName);
	}
	// ★ ФЛАГ КЛАНА: посадка на транспорт — момент, когда он становится видимым (эталон: рендер флага
	//   включается только при bMounted; сам объект флага живёт у персонажа независимо от седла).
	bClanFlagBoneWarned = false;
	UpdateClanFlagVisual();
	// Клип персонажа сразу переводим в сидячий (покой/движение — по текущему состоянию).
	if (AnimState == EAsdaPlayerState::Move) { PlayWorldClip(MoveClip(), true); }
	else                                     { SetAnimState(EAsdaPlayerState::Idle); PlayWorldClip(IdleMainClipIdx(), true); }
	// ★ ШЛЕЙФ И ЗВУКИ ЕЗДЫ (Ride_effect_table) — только запоминаем имена; спавн ведёт TickRideEffect
	//   по эталонным аккумуляторам (эффект 2500 мс, звук 2000 мс). Класс BP резолвим сразу, чтобы
	//   отсутствие ассета всплыло в момент посадки, а не раз в 2.5 с.
	RideFxName = RideEffect; RideSndMove = RideSound; RideSndIdle1 = RideSound2; RideSndIdle2 = RideSound3;
	// ★ Эталон CChar_SetState@0xA13D50 case 2 (вход в седло): аккумуляторы ВЗВОДЯТСЯ на пороги
	//   (worldPosX := 2500.0f, worldPosY := 2000.0f) — первый шлейф и первый звук играют СРАЗУ,
	//   а не через 2.5/2 с после посадки.
	RideFxAccumMs = 2500.f; RideSndAccumMs = 2000.f; bRideSndFirstIdle = true; bRideWasMoving = false;
	// ★ Аудит №3 (A10): путь BP строит ЕДИНАЯ точка `AAsdaEffectActor::ResolveJebClass` (было своё
	//   `BP_Fx_%s` + `LoadClass`). Класс держим как ГЕЙТ: без него шлейф молча не спавнится, а жалоба
	//   должна прозвучать в момент посадки, а не раз в 2.5 с (прежнее поведение сохранено).
	RideFxClass = AAsdaEffectActor::ResolveJebClass(RideFxName);
	if (!RideFxName.IsEmpty() && !RideFxClass)
	{
		UE_LOG(LogAsdaPlayer, Warning, TEXT("[транспорт] шлейф BP_Fx_%s не найден"), *RideFxName);
	}
	// Один звуковой канал на персонажа (эталон держит ОДИН handle в char[1].worldMatrix[3] и глушит его
	//   перед каждым новым проигрышем) — 3D в позиции персонажа.
	if (!MountAudio && (!RideSndMove.IsEmpty() || !RideSndIdle1.IsEmpty() || !RideSndIdle2.IsEmpty()))
	{
		MountAudio = NewObject<UAudioComponent>(this, TEXT("MountAudio"));
		MountAudio->SetupAttachment(Root);
		MountAudio->bAutoActivate = false;
		MountAudio->bStopWhenOwnerDestroyed = true;
		MountAudio->SetVolumeMultiplier(0.6f);
		MountAudio->RegisterComponent();
	}
	// ★ ВСПЫШКА БЕЛОГО ДЫМА + ЗВУК ПОСАДКИ (эталон Recv_MountVehicleRide@0xF2BCF0):
	//   itm1_n_00_vehicle_0000_e.jeb на кости M_DUMMY/G_DUMMY (attach 7, scale 1.0) +
	//   s_e_riding_vehicle.wav — 2D своему (CSoundFlag_PlaySound2D), 3D чужим (Audio_Play3DSound).
	//   ГЕЙТ ПО ВОЗРАСТУ АКТОРА: применение кэша MountByUserIdx при СПАВНЕ соседа (появился в AoI
	//   уже верхом) — НЕ событие посадки; в эталоне парс появления вспышку не зовёт.
	if (GetGameTimeSinceCreation() > 1.5f)
	{
		SpawnMountFlashFx();
		// ★ Аудит №3 (A9): звук — ТОЛЬКО через `UAsdaSoundSubsystem` (закон [[resource-subsystems]]).
		//   Прежде здесь стоял свой `LoadObject` + `UGameplayStatics::PlaySound2D/AtLocation` мимо
		//   менеджера — единственное такое место в порту. Резолв, кэш и логирование теперь его.
		UAsdaSoundSubsystem* Snd = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr;
		if (Snd)
		{
			if (IsLocalPlayerCharacter()) { Snd->PlaySound2D(TEXT("s_e_riding_vehicle")); }
			else                          { Snd->PlaySound3DAs(TEXT("s_e_riding_vehicle"), GetActorLocation(), EAsdaSoundClass::Vehicle, this); }   // ★ 30.10: «транспорт»
		}
	}
	UE_LOG(LogAsdaPlayer, Log, TEXT("[транспорт] посадка: модель=%s набор анимаций=%s шлейф=%s звук=%s"),
		*ModelName, *AnimSet, RideFxName.IsEmpty() ? TEXT("—") : *RideFxName,
		RideSndMove.IsEmpty() ? TEXT("—") : *RideSndMove);
}

void AAsdaPlayerCharacter::ClearMount()
{
	if (!bMounted) { return; }
	// ★ ВСПЫШКА БЕЛОГО ДЫМА высадки — ДО пересборки модели, с точки кости M_DUMMY/G_DUMMY.
	//   Эталон: CItem_OnRemoveByTable2Record@0xF264F0 (bcast 0x154E) и авто-высадка
	//   CChar_SetState@0xA13D50 case 5/24 — тот же itm1_n_00_vehicle_0000_e.jeb; звука у высадки НЕТ.
	SpawnMountFlashFx();
	bMounted = false;
	MountHeightOffset = 0.f;
	MountAnimSet.Reset();
	MountModelName.Reset();
	// ★ Сначала снимаем флаг клана (он приаттачен к кости транспорта), потом убираем сам транспорт.
	UpdateClanFlagVisual();
	if (MountMesh)
	{
		MountMesh->DestroyComponent();
		MountMesh = nullptr;
	}
	// Уже отпущенные в мир шлейфы НЕ убиваем (они одноразовые и догорают сами — как ноды CEffectMgr).
	RideFxName.Reset(); RideSndMove.Reset(); RideSndIdle1.Reset(); RideSndIdle2.Reset();
	RideFxClass = nullptr; RideFxAccumMs = 0.f; RideSndAccumMs = 0.f; bRideWasMoving = false;
	if (MountAudio) { MountAudio->Stop(); MountAudio->DestroyComponent(); MountAudio = nullptr; }
	// Возврат к обычным клипам текущего состояния.
	UpdateSwimState(true);   // ★ 23.10.2026: высадка в глубокой воде — сразу плывёт (флаг до выбора клипа ниже)
	if (AnimState == EAsdaPlayerState::Move) { PlayWorldClip(MoveClip(), true); }
	else if (AnimState != EAsdaPlayerState::Dead) { PlayWorldClip(IdleClipIdx(), true); }
	UE_LOG(LogAsdaPlayer, Log, TEXT("[транспорт] высадка"));
}

// ★ Клип транспорта по состоянию: anim_00 = покой (_000.jbb, слот [1316]+0), anim_02 = движение
//   (_002.jbb, слот [1316]+128); anim_01 эталоном не используется. Всегда луп. Вызывается ВМЕСТЕ с
//   запуском сидячего клипа седока (PlayWorldClip) — оба стартуют с кадра 0 в один тик, как в эталоне
//   (таблица состояний §2.2: слоты персонажа и транспорта переключаются в одном месте CChar_SetState).
void AAsdaPlayerCharacter::PlayMountClip(bool bMove)
{
	if (!MountMesh || !MountMesh->GetSkeletalMeshAsset() || MountModelName.IsEmpty()) { return; }
	const FString Path = UAsdaModelAnimSubsystem::BuildSkelAnimPath(   // Д8
		AsdaAssetPaths::kSkelRootVehicle, MountModelName, bMove ? 2 : 0);   // ★ волна 2 п. 7
	if (UAnimSequence* Clip = UAsdaModelAnimSubsystem::LoadClipChecked(Path))   // Д2
	{
		AsdaAnimSmooth::PlayClip(MountMesh, Clip, true);   // ★ 22.10.2026: тот же бленд, что у сидячего клипа седока
	}
	else
	{
		UE_LOG(LogAsdaPlayer, Warning, TEXT("[транспорт] клип транспорта не найден: %s"), *Path);
	}
}

// ── ★ ШЛЕЙФ И ЗВУКИ ЕЗДЫ — 1:1 с эталоном ───────────────────────────────────────────────────────
// render_core.c:3488 / entity_char.c:23085 (пер-кадровый тик персонажа в седле):
//   state 0 (покой)  → charItemSoundChargeTick@0xF27740: accumSnd += dt(мс); >=2000 → стоп канала +
//                      SoundFile2 (rec+132) в первый раз / SoundFile3 (rec+196) далее. ЭФФЕКТА НЕТ.
//   state 1 (ход)    → при ВХОДЕ: itemStopLoopSoundOnState + accumSnd := 2000.0f (звук сразу), затем
//                      Item_UpdateSpecialEffectAndSound@0xF272E0:
//                        accumFx  += dt; >=2500 → Effect_SpawnByName(EffectFile, поз. кости M_DUMMY/G_DUMMY,
//                                                  boneIdx, attach 6, scale 1.0), accumFx = 0;
//                        accumSnd += dt; >=2000 → стоп канала + SoundFile (rec+68), accumSnd = 0.
// Аккумуляторы живут в самом персонаже (char[1].worldPosX/worldPosY) и НЕ сбрасываются на остановке.
void AAsdaPlayerCharacter::TickRideEffect(float DeltaSeconds)
{
	const float DtMs = DeltaSeconds * 1000.f;
	if (bMoving)
	{
		if (!bRideWasMoving)
		{
			// Вход в state 1: прежний канал глушится, аккумулятор звука взводится на порог (звук сразу).
			if (MountAudio) { MountAudio->Stop(); }
			RideSndAccumMs = 2000.f;
			bRideWasMoving = true;
		}
		RideFxAccumMs += DtMs;
		if (RideFxAccumMs >= 2500.f) { SpawnRideTrailFx(); RideFxAccumMs = 0.f; }
		RideSndAccumMs += DtMs;
		if (RideSndAccumMs >= 2000.f) { PlayRideSound(RideSndMove); RideSndAccumMs = 0.f; }
	}
	else
	{
		bRideWasMoving = false;
		RideSndAccumMs += DtMs;
		if (RideSndAccumMs >= 2000.f)
		{
			PlayRideSound(bRideSndFirstIdle ? RideSndIdle1 : RideSndIdle2);
			bRideSndFirstIdle = false;
			RideSndAccumMs = 0.f;
		}
	}
}

// Одноразовый шлейф: .jeb транспорта живут endMode=0 (эмиссия ~3 с и полное гашение), поэтому движок
//   пересевает их каждые 2.5 с. Актор сам умирает по истечении периода эмиттеров.
void AAsdaPlayerCharacter::SpawnRideTrailFx()
{
	if (!RideFxClass || !GetWorld()) { return; }
	// Эталон берёт стартовую точку с кости M_DUMMY (муж) / G_DUMMY (жен); наш скелет унисекс — экспортёр
	//   ремапит G_DUMMY→M_DUMMY (см. §H5 wings_soulmate_re.md), поэтому имя одно.
	FVector Start = GetActorLocation();
	if (BaseSkel && BaseSkel->DoesSocketExist(TEXT("M_DUMMY")))
	{
		Start = BaseSkel->GetSocketLocation(TEXT("M_DUMMY"));
	}
	// ★ Аудит №3 (A10): спавн через ЕДИНУЮ точку (было своё построение пути + LoadClass + SpawnActor).
	// attach-режим 6 (effectNodeFollowPlayerBone): нода каждый кадр = мировая матрица персонажа.
	// ★ Д1: attach-6 через ЕДИНУЮ точку. BaseSkel = аналог char->worldMatrix (MeshRoot ×0.01, зеркало,
	//   курс); bFollowPosition + тик после движения/поз носителя (иначе трансформ прошлого кадра) — внутри.
	// ★ Дедуп №2 В1-C (F12): спавн, привязка и срок `NaturalLifeSpan()` — общей точкой одноразового эффекта на носителе.
	AAsdaEffectActor* E = AAsdaEffectActor::SpawnOnCarrier(GetWorld(), RideFxName, Start, this, BaseSkel, NAME_None, EAsdaFxAttachMode::CharWorld);
	if (!E) { return; }
	const float Life = E->NaturalLifeSpan();   // ★ Аудит №3 (B5): одно время жизни на все одноразовые эффекты (для лога — тот же срок)
	if (RideFxSpawnCount < 3)
	{
		UE_LOG(LogAsdaPlayer, Log, TEXT("[транспорт] шлейф #%d: %s эмиттеров=%d жизнь=%.1fс поз=%s"),
			RideFxSpawnCount + 1, *RideFxName, E->Emitters.Num(), Life, *E->GetActorLocation().ToCompactString());
	}
	++RideFxSpawnCount;
}

// ★ Вспышка белого дыма посадки/высадки (itm1_n_00_vehicle_0000_e.jeb, 15 эмиттеров, additive).
//   Эталон везде спавнит её attach-режимом 7 — в отличие от шлейфа (6) эффект НЕ следует за
//   персонажем: облако остаётся в мировой точке кости на момент спавна и догорает само.
//   BP генерирует Tools/asda_build_mount_fx.py (список EXTRA — хардкод-имена клиента вне таблицы).
void AAsdaPlayerCharacter::SpawnMountFlashFx()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	// Точка кости M_DUMMY (жен. G_DUMMY ремапится экспортёром в M_DUMMY — §H5 wings_soulmate_re.md).
	FVector Start = GetActorLocation();
	if (BaseSkel && BaseSkel->DoesSocketExist(TEXT("M_DUMMY")))
	{
		Start = BaseSkel->GetSocketLocation(TEXT("M_DUMMY"));
	}
	// ★ Аудит №3 (A10): спавн через ЕДИНУЮ точку по .jeb-базе (было своё `LoadClass` полного пути BP).
	// ★ Дедуп №2 В1-C (F12): спавн и срок (Аудит №3 B5: одно время жизни на все одноразовые эффекты) — общей точкой.
	AAsdaEffectActor* E = AAsdaEffectActor::SpawnOnCarrier(W, TEXT("itm1_n_00_vehicle_0000_e"), Start, this);
	if (!E)
	{
		UE_LOG(LogAsdaPlayer, Warning,
			TEXT("[транспорт] вспышка BP_Fx_itm1_n_00_vehicle_0000_e не найдена (запусти asda_build_mount_fx.py)"));
		return;
	}
}

// ★ Аудит №3 (A9): «свой персонаж?» — одна проверка на файл (эталон различает 2D своему и 3D чужим).
bool AAsdaPlayerCharacter::IsLocalPlayerCharacter() const
{
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>())
		{
			return WS->LocalPlayerActor.Get() == this;
		}
	}
	return false;
}

// Один звуковой канал: эталон глушит прежний handle и запускает новый (2D для своего персонажа, 3D для чужих).
// ★ Аудит №3 (A9): САМ ЗВУК резолвит менеджер (`UAsdaSoundSubsystem::ResolveSound` — кэш, единый путь
//   /Game/AsdaAssets/Sound). Свой `UAudioComponent` остаётся намеренно: у транспорта ОДИН канал —
//   эталон держит один handle в `char[1].worldMatrix[3]` и глушит его перед каждым новым проигрышем,
//   а `PlaySound2D/3D` менеджера каждый раз заводили бы новый голос и звуки наслаивались бы.
void AAsdaPlayerCharacter::PlayRideSound(const FString& SoundName)
{
	if (SoundName.IsEmpty() || !MountAudio) { return; }
	UAsdaSoundSubsystem* SndMgr = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr;
	USoundBase* Snd = SndMgr ? SndMgr->ResolveSound(FName(*SoundName)) : nullptr;
	if (!Snd)
	{
		UE_LOG(LogAsdaPlayer, Warning, TEXT("[транспорт] звук '%s' не импортирован"), *SoundName);
		return;
	}
	MountAudio->Stop();
	const bool bSelf = IsLocalPlayerCharacter();
	MountAudio->bAllowSpatialization = !bSelf;   // свой — 2D (CSoundFlag_PlaySound2D), чужой — 3D
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-C, F13): чужой — 3D тем же законом, что прочие 3D-звуки (`PlaySound3D`):
	//   кривая FMOD3 с эталонными min/max 100/10000 (`Item_UpdateSpecialEffectAndSound@0xF272E0` → `Audio_Play3DSound_v2` →
	//   `sound3DPlayPositioned`, тот же путь, что у остальных 3D-звуков) и купол слышимости `asda.Sound.HearingRadiusM`.
	//   Было: `bAllowSpatialization` без затухания — звук чужого транспорта был слышен одинаково на любом расстоянии.
	//   За куполом (при `asda.Sound.SilenceBeyondMax 1`) канал не заводится — как `PlaySound3D` (прежний звук уже заглушён).
	if (!bSelf)
	{
		// ★ 30.10.2026: класс «транспорт» (радиус по слуху человека), затухание объектом ДО `Play` — см. `Configure3DAs`.
		if (!SndMgr->Configure3DAs(MountAudio, GetActorLocation(), EAsdaSoundClass::Vehicle)) { return; }
	}
	else
	{
		MountAudio->bOverrideAttenuation = false;   // свой — 2D: затухания нет
		MountAudio->AttenuationSettings = nullptr;   // ★ 30.10: объект затухания чужого (после пересадки компонента) не наследуем
	}
	MountAudio->SetSound(Snd);
	// ★ 19.09.2026: канал транспорта держит СВОЙ компонент мимо менеджера, поэтому громкость эффектов
	//   из окна настроек (полоса 70016 / «заглушить» 70015) до него не доходила. Базовые 0.6 канала
	//   сохраняются, поверх — действующий множитель эффектов профиля игрока.
	MountAudio->SetVolumeMultiplier(0.6f * UAsdaSoundSubsystem::GetSfxVolumeMul());
	MountAudio->Play();
}

// ── ★ ФЛАГ КЛАНА НА ТРАНСПОРТЕ (clan_flag_mount_re.md) ───────────────────────────────────────────
// Эталон: uiClanFlagGetOrCreate(g_pGameObjectMgr, −1, charId, /*тип*/1) создаёт объект-флаг у КАЖДОГО
//   персонажа с включённым флагом клана (charSpawnLocalPlayer/charSpawnRemoteEntity@0x9D2470 —
//   и для СВОЕГО, и для ЧУЖИХ), а mapobj_updateFlagLinkAnim@0xFB0CB0 показывает его ТОЛЬКО в седле.
// Путь ассетов флага (импорт: Tools/asda_import_clanflag_model.py).
static const TCHAR* kClanFlagMeshPath = AsdaAssetPaths::kClanFlagMesh;   // ★ волна 2 п. 7: путь — реестр путей

void AAsdaPlayerCharacter::SetClanFlag(bool bOn, const FAsdaClanFlagData& InFlag)
{
	bClanFlagOn  = bOn;
	ClanFlagData = InFlag;
	UpdateClanFlagVisual();
}

void AAsdaPlayerCharacter::PlayClanFlagClip(int32 Idx)
{
	if (!ClanFlagMesh || Idx < 0 || Idx > 2) { return; }
	UAnimSequence* Clip = UAsdaModelAnimSubsystem::LoadClipChecked(   // Д2
		FString::Printf(TEXT("%sanim_%02d"), kClanFlagMeshPath, Idx));
	if (!Clip) { return; }
	ClanFlagMesh->PlayAnimation(Clip, /*bLooping=*/true);
	ClanFlagClip     = Idx;
	ClanFlagClipLeft = Clip->GetPlayLength();
}

void AAsdaPlayerCharacter::UpdateClanFlagVisual()
{
	// Условие показа 1:1 с эталоном: включён флаг клана И игрок В СЕДЛЕ (плюс есть сам меш транспорта).
	const bool bWant = bClanFlagOn && ClanFlagData.IsValid() && bMounted
		&& MountMesh && MountMesh->GetSkeletalMeshAsset();
	if (!bWant)
	{
		if (IsValid(ClanFlagMesh)) { ClanFlagMesh->DestroyComponent(); }
		ClanFlagMesh = nullptr;
		ClanFlagMID = nullptr;
		ClanFlagClip = -1; ClanFlagClipLeft = 0.f;
		return;
	}
	// Кость крепления ищется В МЕШЕ ТРАНСПОРТА (эталон: List_FindIndexByName(...,"Flag_Link")).
	//   У 2 моделей из 52 (vehicle_0039/0040) её нет — там флага не будет, как и в оригинале.
	if (MountMesh->GetBoneIndex(ClanFlagBone) == INDEX_NONE)
	{
		if (!bClanFlagBoneWarned)
		{
			bClanFlagBoneWarned = true;
			UE_LOG(LogAsdaPlayer, Log, TEXT("[флаг клана] у транспорта нет кости '%s' — флаг не крепится"),
				*ClanFlagBone.ToString());
		}
		if (IsValid(ClanFlagMesh)) { ClanFlagMesh->DestroyComponent(); }
		ClanFlagMesh = nullptr; ClanFlagMID = nullptr;
		return;
	}

	if (!ClanFlagMesh)
	{
		ClanFlagMesh = NewObject<USkeletalMeshComponent>(this, TEXT("ClanFlagMesh"));
		ClanFlagMesh->SetupAttachment(MountMesh, ClanFlagBone);
		ClanFlagMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		ClanFlagMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		ClanFlagMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ClanFlagMesh->SetCastShadow(true);
		ClanFlagMesh->RegisterComponent();
		ClanFlagMesh->AttachToComponent(MountMesh,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale, ClanFlagBone);
		ClanFlagMesh->SetRelativeLocation(FVector::ZeroVector);
		ClanFlagMesh->SetRelativeRotation(ClanFlagAttachRotation);
		ClanFlagMesh->SetRelativeScale3D(FVector::OneVector);

		if (USkeletalMesh* M = UAsdaModelAnimSubsystem::LoadMeshChecked(kClanFlagMeshPath))   // Д2
		{
			ClanFlagMesh->SetSkeletalMesh(M);
		}
		else
		{
			UE_LOG(LogAsdaPlayer, Warning, TEXT("[флаг клана] модель small_flag не импортирована (%s)"), kClanFlagMeshPath);
		}
		ClanFlagMID = nullptr;
		PlayClanFlagClip(bMoving ? 2 : 0);
	}

	// ПОЛОТНИЩЕ = подсетка, у которой в .jbm стояла текстура "template.TEX" (эталон
	//   mapDecoModelLoadStage@0xFB29F0: mesh_BuildAndAssignParts(..., "clan_flag_<id>.tex", "template.TEX")).
	//   Древко ("flag.TEX") остаётся со своей текстурой. У нас имена слотов пришли из .glb-материалов.
	if (UTextureRenderTarget2D* RT = UAsdaClanFlag::GetOrBake(this, ClanFlagData))
	{
		if (!ClanFlagMID)
		{
			const TArray<FSkeletalMaterial>& Mats = ClanFlagMesh->GetSkeletalMeshAsset()
				? ClanFlagMesh->GetSkeletalMeshAsset()->GetMaterials() : TArray<FSkeletalMaterial>();
			for (int32 i = 0; i < Mats.Num(); ++i)
			{
				const FString SlotName = Mats[i].MaterialSlotName.ToString().ToLower();
				const FString MatName  = Mats[i].MaterialInterface
					? Mats[i].MaterialInterface->GetName().ToLower() : FString();
				if (SlotName.Contains(TEXT("template")) || MatName.Contains(TEXT("template")))
				{
					ClanFlagMID = ClanFlagMesh->CreateDynamicMaterialInstance(i);
					break;
				}
			}
			if (!ClanFlagMID)
			{
				UE_LOG(LogAsdaPlayer, Warning, TEXT("[флаг клана] не найден слот полотнища ('template') — эмблема не наложена"));
			}
		}
		if (ClanFlagMID) { ClanFlagMID->SetTextureParameterValue(AsdaMat::kParamTex, RT); }   // ★ В1-C F10
	}
}
