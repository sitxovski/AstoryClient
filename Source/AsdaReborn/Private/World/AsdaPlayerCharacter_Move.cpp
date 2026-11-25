// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPlayerCharacter_Move.cpp — ★ 23.10.2026 часть AAsdaPlayerCharacter, вынесенная из AsdaPlayerCharacter.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaPlayerCharacter.plan.json).
//   Крылья, состояние анимации, позиция/разворот, хвост камеры, сетевое движение, смерть (обесцвечивание), Tick, транспорт, флаг клана.
//   #include — те же, что у AsdaPlayerCharacter.cpp, в том же порядке; общее — AsdaPlayerCharacterInternal.h.
#include "World/AsdaPlayerCharacter.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "World/AsdaNockedArrow.h"   // ★ 05.11.2026: смерть снимает стрелу на тетиве
#include "World/AsdaPlayerController.h"   // ★ ритм ударов задаёт тик анимации (OnSwingCycleCompleted)
#include "Misc/App.h"   // FApp::GetDeltaTime — оценка кадра (точка отправки за кадр до дедлайна)
#include "World/AsdaAttackRhythmSubsystem.h"   // ★ 15.10.2026: серверные часы атаки — темп отрезка замаха до точки отправки
#include "Resources/AsdaTextureSubsystem.h"      // ★ charsel_preload: реестр владения флагом полных мипов
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д8/Д9: ресурс только через менеджер
#include "Resources/AsdaSoundSubsystem.h"       // ★ Аудит №3 (A9): звук транспорта — только через менеджер
#include "World/AsdaGroundTrace.h"
#include "World/AsdaCoord.h"                    // ★ Аудит №3 (B1/B2/B9): единый перевод координат игра↔UE
#include "World/AsdaNameplateComponent.h"
#include "Components/MeshComponent.h"     // ★ режим «душа»: подмена слотов материалов тела и частей
#include "World/AsdaWorldSubsystem.h"   // LocalPlayerActor — «свой» персонаж (звук 2D против 3D)
#include "Engine/GameInstance.h"         // ★ В5fix: GetGameInstance()->GetSubsystem (сборка без unity)
#include "AsdaFxNames.h"               // ★ дедуп №2 В1-C (F6): теги эффектов персонажа + общее гашение по тегу
#include "World/AsdaWalkGrid.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"   // поза трупа: перемотка клипа death01 в последний кадр
#include "Engine/PostProcessVolume.h"                  // ч/б сцены на смерти: unbound-объём с высшим приоритетом
#include "Engine/StaticMesh.h"                 // ★ 10.10.2026 (копание): меш инструмента на `8_a1`
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"                  // ★ В5: таймер конца клипа действия рыбалки
#include "World/AsdaCombatTrace.h"         // ★ 18.10.2026: журнал боя — замах, кадры удара, стойка, смерть персонажа
#include "Npc/AsdaMobActor.h"              // ★ 18.10.2026: журнал боя — имя и rid цели разворота в замахе
#include "World/AsdaBlendAnimInstance.h"   // ★ 22.10.2026: плавность — смешивание клипов, доворот модели, хвост сетевых поправок
#include "Misc/ScopeExit.h"                // ★ 22.10.2026: доворот/хвост на любом выходе из Tick
#include "AsdaPlayerCharacterInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaPlayerCharacter*.cpp (LogAsdaPlayer, чистые хелперы)
#include "AsdaNetSubsystem.h"              // ★ 23.10.2026 (прыжок донора): 4024 код 131
#include "World/AsdaCrouchStand.h"         // ★ 05.11.2026: присед стоя — поза и 4024 132/133
#include "UI/AsdaSelfStoreSubsystem.h"     // ★ … фокус в чате личной лавки
#include "UI/AsdaPremiumGachaSubsystem.h"  // ★ … сцена премиум-гачи
#include "Framework/Application/SlateApplication.h"   // ★ … фокус в любом поле ввода
#include "Resources/AsdaGameUserSettings.h"           // ★ 25.10.2026 (присед): режим Ctrl «Нажатие/Зажатие» из окна настроек
#include "EngineUtils.h"                              // ★ 28.10.2026: обход объёмов пост-обработки карты (экспозиция)
#include "Engine/PostProcessVolume.h"
#include "Camera/CameraComponent.h"
// ★ постоянный форс мипов у текстур персонажа   ★ R3-c: комментарий снятого включения "Engine/Texture2D.h" (части им не пользуются)
// ★ charsel_preload: владение флагом полных мипов   ★ R3-c: комментарий снятого включения "Engine/StreamableRenderAsset.h" (части им не пользуются)
// FAsdaComboTimeRow — тайминги ударов замаха (combo_time)   ★ R3-c: комментарий снятого включения "Items/AsdaItemDbRows.h" (части им не пользуются)
// ★ Сет B: капсульная тень тела — одна точка на порт (игрок/моб/NPC)   ★ R3-c: комментарий снятого включения "World/AsdaCapsuleShadow.h" (части им не пользуются)
// ★ режим «душа»: снять спавн-фейд (эталон его у души не умножает)   ★ R3-c: комментарий снятого включения "World/AsdaFadeComponent.h" (части им не пользуются)
// кости/трансформы оружия «в руке ↔ за спиной»   ★ R3-c: комментарий снятого включения "Characters/AsdaCharAssembly.h" (части им не пользуются)
#include "AsdaPlayerCharacter_Move_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaPlayerCharacter_Move*.cpp

// ★ 28.10.2026 (владелец): ЭКСПОЗИЦИЯ — У КАРТЫ. Камера персонажа задаёт фиксированную экспозицию (конструктор,
//   AsdaPlayerCharacter.cpp: AutoExposure Min = Max = 1, Bias = 1) и тем перекрывала объём пост-обработки карты —
//   владелец не мог выставить яркость карты в редакторе. Теперь: если на карте есть включённый Unbound-объём с
//   заданной экспозицией (Min/Max/Compensation/метод/физическая камера), камера свои поля экспозиции снимает и
//   правит объём карты; объёма нет — прежняя фиксированная экспозиция (остальные карты не меняются). Объёмы смотрятся
//   один раз на мир; дальше — шесть присваиваний за кадр.
static TWeakObjectPtr<UWorld> GAsdaExposureWorld;
static bool GAsdaExposureMapOwns = false;

static void AsdaApplyMapExposure(UWorld* World, UCameraComponent* Camera)
{
	if (!World || !Camera) { return; }
	if (GAsdaExposureWorld.Get() != World)
	{
		GAsdaExposureWorld = World;
		GAsdaExposureMapOwns = false;
		for (TActorIterator<APostProcessVolume> It(World); It; ++It)
		{
			const APostProcessVolume* V = *It;
			const FPostProcessSettings& S = V->Settings;
			if (V->bUnbound && V->bEnabled && V->BlendWeight > 0.f
				&& (S.bOverride_AutoExposureMinBrightness || S.bOverride_AutoExposureMaxBrightness || S.bOverride_AutoExposureBias
					|| S.bOverride_AutoExposureMethod || S.bOverride_AutoExposureApplyPhysicalCameraExposure))
			{
				GAsdaExposureMapOwns = true;
				UE_LOG(LogAsdaPlayer, Log, TEXT("[экспозиция] %s: экспозицию задаёт объём карты %s — фиксированная экспозиция камеры снята"),
					*World->GetMapName(), *V->GetName());
				break;
			}
		}
	}
	const bool bFixed = !GAsdaExposureMapOwns;
	FPostProcessSettings& P = Camera->PostProcessSettings;
	P.bOverride_AutoExposureMinBrightness = bFixed;
	P.bOverride_AutoExposureMaxBrightness = bFixed;
	P.bOverride_AutoExposureBias = bFixed;
}

void AAsdaPlayerCharacter::SetWingsOn(bool bOn)
{
	// ★ КРЫЛЬЯ (эталон charUpdateActionStateTick@0xA216B0): бег = трек 35 (move_00, взмахи) вместо run01.
	//   Если крыло надели/сняли ПРЯМО в беге — перезапускаем клип движения (SetAnimState с тем же
	//   состоянием ранний выход, поэтому напрямую). Замах атаки не перебиваем (гейт 13/14).
	if (bWingsOn == bOn) { return; }
	bWingsOn = bOn;
	// ★ В3 S7: «эмоция или рыбалка» → `IsActionClipOwned` (+ лопата: недостижимо — вход в 40/43 переводит ход в покой).
	if (AnimState == EAsdaPlayerState::Move && !IsAttackAnimActive() && !IsActionClipOwned())
	{
		PlayWorldClip(MoveClip(), true);   // ★ эмоцию не перебиваем — см. пояснение в SetWeaponSubType
	}
}

void AAsdaPlayerCharacter::SetAnimState(EAsdaPlayerState NewState)
{
	// ★ В45fix2 (МЕЛК-5): любая смена состояния (`CChar_SetState`, запись безусловна — g:76456) снимает состояние 68 «добыча».
	//   Оружие вернёт покадровый `UpdateWeaponMotionHidden` (тик), здесь только признак.
	//   ★ 09.10.2026 (п. 5 владельца): то же для состояний лопаты 40/43. ★ В3 S7: из единого поля.
	if (IsEtalonState68()) { SetBusyAction(EAsdaActionState::Idle, TEXT("смена состояния (68)")); }
	ClearEtalonShovelState();
	// ★ 24.10.2026 [ПОРТ] свой присед снимается смертью (мёртвым приседать нельзя; после воскрешения — обычный ход).
	if (NewState == EAsdaPlayerState::Dead && bCrouchOwn) { bCrouchOwn = false; LastCrouchRefusal = TEXT("снят: смерть"); }
	// ★ 05.11.2026 [ПОРТ] (присед стоя): и присед соседа (4007/4025) — после воскрешения он не стоит в позе приседа, пока не скажет снова.
	if (NewState == EAsdaPlayerState::Dead) { bCrouchNet = false; }
	// ★ 05.11.2026 [ИСПРАВЛЕНИЕ] (`asda.Bow.NockLifecycle`): смерть снимает стрелу на тетиве (эталон `CChar_SetState` → `clearActiveEffectSlots`,
	//   entity_char.c:19707/19770); гейт замаха ниже смерть не держит, но ранний выход «то же состояние» её бы пропустил — снимаем до него.
	if (NewState == EAsdaPlayerState::Dead && AsdaNockedArrow::LifecycleOn()) { AsdaNockedArrow::Hide(this, TEXT("смерть")); }
	if (AnimState == NewState)
	{
		// ★ В5fix (ревизия В5, ВАЖН-2, 09.10.2026): действие рыбалки 60…67 живёт поверх `AnimState == Idle`
		//   (`PlayFishAction` переводит Move/Sit в Idle), поэтому «покой → покой» у соседа раньше был no-op и клип 61
		//   крутился вечно. Эталон `CChar_SetState(ch, 0)` (game.c:76390+) ВСЕГДА переписывает 60…67, независимо от
		//   прежнего состояния: 4025 0/400 (`Recv_CharSpawnFx` → `CChar_SetState(0)`, `LABEL_11`), 300 (GM-скрытие, тоже
		//   `CChar_SetState(0)`), остановка движения. [ОРИГИНАЛ]. Своё действие ведёт автомат рыбалки (своё состояние 0
		//   приходит через `OnSelfFishAction(0)`), поэтому здесь — только чужой: у своего порт зовёт «покой» и там,
		//   где эталон `CChar_SetState` не зовёт (окна крафта/гачи, повторная остановка WASD).
		//   ★ В45fix2 (ревизия В4/В5, МЕЛК-3): 4025 109 сюда НЕ идёт — это `CChar_SetState(…, 9)` «встаёт» (g:551808-551809),
		//   а не 0; его ведёт `EnterStandUpState` (удочка висит до конца вставания, как у эталона).
		if (IsFishActionActive() && !bFishActionSelf) { StopFishAction(TEXT("состояние 0 при покое")); }
		return;
	}
	// ★ Гейт замаха (порт state-гейта 13/14 эталона): пока играет атак-клип, смена состояния клип
	//   НЕ трогает (позиция двигается); по концу замаха таймер PlayAttackClip восстановит клип состояния.
	if (NewState != EAsdaPlayerState::Dead && IsAttackAnimActive())
	{
		ASDA_CTRACE_V(EAsdaCT::Swing, TEXT("%s: состояние %d → %d при идущем замахе — клип атаки НЕ трогаем (гейт 13/14), клип состояния вернёт конец прохода"),
			*AsdaCtPcWho(this), (int32)AnimState, (int32)NewState);
		AnimState = NewState;
		return;
	}
	if (AnimState == EAsdaPlayerState::Dead)   // журнал боя: выход из смерти (воскрешение/респаун) — событие, не тик
	{
		ASDA_CTRACE_IF(AsdaCtPcOurs(this), EAsdaCT::Death, TEXT("%s: ВСТАЛ после смерти — состояние %d → %d (воскрешение/респаун)"),
			*AsdaCtPcWho(this), (int32)AnimState, (int32)NewState);
	}
	// ★★★ ВЫХОД ИЗ ПОЛОЖЕНИЯ СИДЯ. Эталон различает ДВА пути (см. заголовок, блок kSitDownEndMs*):
	//   • «встать» (`charToggleSitStand@0xC54A70` при текущем состоянии 8) → состояние 9, то есть
	//     ХВОСТ клипа sitdown01, и только по его концу — покой. Именно этого не хватало: персонаж
	//     мгновенно оказывался стоящим (жалоба владельца 23.08.2026);
	//   • любая другая смена состояния (побежал, умер) перебивает сидение СРАЗУ — `CChar_SetState(…,1)`
	//     не заглядывает в состояние 7/8/9 вовсе. Повторяем это различие один в один.
	const bool bWasSitting = (AnimState == EAsdaPlayerState::Sit);
	if (bWasSitting && NewState == EAsdaPlayerState::Idle)
	{
		AnimState = NewState;
		StopEmotionClip();
		if (BeginStandUp() > 0.f)
		{
			// Клип покоя ставит `OnSitPhaseTimer` по концу хвоста — здесь НИЧЕГО не играем.
			return;
		}
		// Хвоста нет — падаем в обычную ветку покоя ниже.
	}
	else if (bWasSitting || SitPhase != 0)
	{
		// Сидение прерывается сменой состояния: гасим сегментный таймер, дальше как обычно.
		SitPhase = 0;
		if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(SitTimer); }
	}
	AnimState = NewState;
	// ★ Эмоция ПРЕРЫВАЕТСЯ сменой состояния — ровно как в эталоне: `CChar_SetState(…, 1)` на старте бега
	//   просто перебивает состояние 33…46, отдельного «стоп-пакета» у эмоции нет (отчёт §7).
	StopEmotionClip();
	// ★ Волна В5: действие рыбалки 60…67 так же перебивается состоянием 0/1/6/7 (у чужого — пакетами движения/смерти;
	//   своему движение закрыто гейтами «идёт рыбалка»). Клип ставит ветка ниже — здесь только снять признак.
	if (IsFishActionActive())
	{
		SetBusyAction(EAsdaActionState::Idle, TEXT("смена состояния"));   // ★ В3 S7: таймер и сторож клипа снимает единая запись
		UpdateWeaponMotionHidden();
	}
	// Ассеты: 12=idle01, 15=sitdown01, 16=walk01, 17=run01, 18=death01, 19=back01 (реверс-слоты 0/3/4/5/7/24).
	switch (NewState)
	{
	case EAsdaPlayerState::Idle:
		// Покой: боевая стойка = idle01 (руки подняты, субрежим 20 эталона), мирный = idle02.
		// ★ 23.10.2026 (донор): идёт прыжок — клип прыжка под новое состояние с перенесённым временем (0x4AF1A0 case 0).
		if (bJumpActive) { ShowJumpClipForState(); if (JumpClipShown < 0) { PlayWorldClip(IdleClipIdx(), true); } }
		else { PlayWorldClip(IdleClipIdx(), true); }
		IdleTimer = 0.f; FidgetRemaining = 0.f;
		NextFidgetDelay = FMath::RandRange(6.f, 12.f);
		break;
	case EAsdaPlayerState::Move:   // бег run01; пятимся (S) — back01 (эталон [208])
		// ★ 23.10.2026 (донор): идёт прыжок — runjump01 с перенесённым временем (0x4AF1A0 case 1); ход при этом идёт.
		if (bJumpActive) { ShowJumpClipForState(); if (JumpClipShown < 0) { PlayWorldClip(MoveClip(), true); } }
		else { PlayWorldClip(MoveClip(), true); }
		break;
	case EAsdaPlayerState::Sit:  BeginSitDown(); break;                    // 7 «садится» → 8 «сидит» (см. BeginSitDown)
	case EAsdaPlayerState::Dead:
		CancelJump(TEXT("смерть"));       // ★ 23.10.2026: донор снимает флаг прыжка в состоянии 6
		// ★ смерть ссаживает с транспорта (эталон); 06.11.2026: только если он не числится за игроком (`asda.Mount.ListHoldsModel`).
		if (bMounted && !IsMountHeldByServer()) { ClearMount(); }
		// ★ СВЕЧЕНИЕ ЗАТОЧКИ гаснет на смерти (эталон CCharacter_UpdateHeldItemEffect/UpdateEquipAuraEffect
		//   перестают строить эффект, когда персонаж не рендерится/сменил состояние). Без этого эффект
		//   висел бы над трупом (та же грабля, что была у крыльев).
		//   ★ Дедуп №2 В1-C (F6): тот же один проход «тег оружия ИЛИ щита» — общей точкой гашения по тегу.
		//   ★ 31.10.2026 [ОРИГИНАЛ] (`asda.WeaponGlow.Persist` = 1): утверждение выше было неверным — в гейте эталона
		//   (entity_char.c:28379, :28800) состояния смерти нет, труп свечение держит; гашение — только при ключе 0
		//   (раньше отсюда свечение пропадало до переодевания: после воскрешения его никто не пересобирал).
		if (!AsdaWeaponGlowPersistOn())
		{
			AsdaCharFxTags::DestroyAttached(this, FName(AsdaCharFxTags::kWeaponGlow), FName(AsdaCharFxTags::kShieldGlow));
		}
		{
			const float CtDeathLen = PlayWorldClip(18, false);
			ASDA_CTRACE_IF(AsdaCtPcOurs(this), EAsdaCT::Death, TEXT("%s: СМЕРТЬ — клип death01 (anim_18) %.2f с без петли, свечение заточки снято, замах %s"),
				*AsdaCtPcWho(this), CtDeathLen, IsAttackAnimActive() ? TEXT("шёл (перебит)") : TEXT("не шёл"));
			(void)CtDeathLen;
		}
		break;
	default: break;
	}
}

FVector2D AAsdaPlayerCharacter::GetGamePosXZ() const
{
	// UE→game: gameX = UE.Y, gameZ = -UE.X (обратно (x,y,z)→(-z,x,y)).
	return AsdaCoord::UeToGame2D(GetActorLocation());   // ★ Аудит №3 (B1)
}

float AAsdaPlayerCharacter::TerrainZ(float UeX, float UeY) const
{
	// ★ Высота ЗЕМЛИ (SM_terrain*), как у мобов (AsdaGround::GroundZ): first-hit-трейс сажал персонажа
	//   на КРОНЫ деревьев (листва шире ствола) и крыши. Фильтр по имени террейн-меша решает и прежнюю
	//   проблему арки/моста НАД головой (не SM_terrain → пробивается насквозь). Оригинал вообще берёт
	//   высоту из тайловой heightmap террейна — объекты в высоте не участвуют.
	return AsdaGround::GroundZ(GetWorld(), UeX, UeY, GetActorLocation().Z, this);
}

// Курс «носа» персонажа в UE-градусах: снимаем базовый доворот модели с ротации MeshRoot (см. заголовок).
//   ★ 22.10.2026 (плавность): курс ЛОГИЧЕСКИЙ — тот, что поставил последний `FaceGameDir`, даже пока видимая модель ещё
//   доворачивается (пакеты 4024, мини-карта, выстрелы ведут себя как до правки). До первого `FaceGameDir` — поворот MeshRoot.
float AAsdaPlayerCharacter::GetFacingYawUE() const
{
	if (bFacingYawSet) { return FRotator::NormalizeAxis(FacingYawLogical - MeshYawOffset); }
	return GetVisualFacingYawUE();
}

// ★ 22.10.2026: видимый курс модели (то, что на экране), без доворота модели.
float AAsdaPlayerCharacter::GetVisualFacingYawUE() const
{
	const float MeshYaw = MeshRoot ? MeshRoot->GetComponentRotation().Yaw : GetActorRotation().Yaw;
	return FRotator::NormalizeAxis(MeshYaw - MeshYawOffset);
}

// Направление взгляда в игровых (x, z) — обратное к `FaceGameDir`: курс меша → единичный UE-вектор → своп в игру.
FVector2D AAsdaPlayerCharacter::GetFacingDirGame() const
{
	const float YawRad = FMath::DegreesToRadians(GetFacingYawUE());   // курс «носа» в радианах
	return AsdaCoord::UeDirToGame(FVector2D(FMath::Cos(YawRad), FMath::Sin(YawRad)));
}

// Повернуть МЕШ (MeshRoot), НЕ актор — камера сидит на Root и не должна крутиться с персонажем.
void AAsdaPlayerCharacter::FaceGameDir(const FVector2D& DirGame)
{
	if (DirGame.IsNearlyZero()) { return; }
	// ★ Аудит №3 (B9): курс из игрового направления — общая математика `AsdaCoord`. Доворот модели
	//   (`MeshYawOffset`) и КРУТИМЫЙ компонент у игрока свои: только `MeshRoot` (актор с камерой — нет).
	const float Yaw = AsdaCoord::FaceYawFromGameDir(DirGame) + MeshYawOffset;
	// ★ 22.10.2026 (плавность): логический курс — сразу; видимая модель доворачивается в `TickAnimSmooth`
	//   ([ОРИГИНАЛ] `objAI_FaceTargetByUserIdx@0xA23350`: `self[47641]` идёт к курсу шагом 0.0104666 рад/мс).
	//   Сразу (как прежде) — при ключе 0, у скрытого/неактивного актора и в первые 0.25 с жизни (спавн не должен «крутиться») —
	//   [ИСПРАВЛЕНИЕ] последнее: у оригинала курс на спавне/телепорте не снапается (доворот и там идёт шагом `objAI_FaceTargetByUserIdx`).
	FacingYawLogical = FRotator::NormalizeAxis(Yaw);
	bFacingYawSet = true;
	const bool bSnap = AsdaAnimSmooth::TurnDegPerSec() <= 0.f || IsHidden() || !IsActorTickEnabled() || !HasActorBegunPlay()
		|| GetGameTimeSinceCreation() < 0.25f;
	if (bSnap) { MeshRoot->SetWorldRotation(FRotator(0.f, Yaw, 0.f)); }
}

// ★ 22.10.2026: повернуть сразу (видимая модель = логический курс).
void AAsdaPlayerCharacter::SnapFacingToLogical()
{
	if (!MeshRoot || !bFacingYawSet) { return; }
	MeshRoot->SetWorldRotation(FRotator(0.f, FacingYawLogical, 0.f));
}

// ★ 22.10.2026 (плавность): доворот видимой модели к логическому курсу и гашение «хвоста» сетевой поправки.
//   Зовётся в конце каждого `Tick` (ON_SCOPE_EXIT — у тика много ранних выходов).
void AAsdaPlayerCharacter::TickAnimSmooth(float DeltaSeconds)
{
	if (!MeshRoot) { return; }
	if (bFacingYawSet)
	{
		const float Cur = MeshRoot->GetComponentRotation().Yaw;
		if (!FMath::IsNearlyZero(FMath::FindDeltaAngleDegrees(Cur, FacingYawLogical), 0.01f))
		{
			const float NewYaw = AsdaAnimSmooth::StepYaw(Cur, FacingYawLogical, AsdaAnimSmooth::TurnDegPerSec(), DeltaSeconds);
			MeshRoot->SetWorldRotation(FRotator(0.f, NewYaw, 0.f));
		}
	}
	if (!VisualTail.IsZero())
	{
		const float Tau = AsdaAnimSmooth::SnapSmoothSec();
		VisualTail = (Tau > 0.f) ? VisualTail * FMath::Exp(-FMath::Max(0.f, DeltaSeconds) / Tau) : FVector::ZeroVector;
		if (VisualTail.SizeSquared() < 0.25f) { VisualTail = FVector::ZeroVector; }   // < 0.5 единицы — невидимо
		MeshRoot->SetRelativeLocation(VisualTail);
		ApplyVisualTailToViews();
	}
	else if (bTailOnViews) { ApplyVisualTailToViews(); }
}

// ★ 22.10.2026 (плавность) [ИСПРАВЛЕНИЕ: у оригинала снап]: актор уже стоит в точке сервера (логика, камера, плашка,
//   хитбокс — сразу), тело (MeshRoot) остаётся на прежнем месте и догоняет экспонентой `asda.Anim.SnapSmoothSec`.
//   Длиннее `asda.Anim.SnapSmoothMaxUnits` — настоящий телепорт: без хвоста. Ключ 0 — снап, как прежде.
void AAsdaPlayerCharacter::AddVisualTail(const FVector& OldLoc)
{
	if (!MeshRoot) { return; }
	FVector Tail = VisualTail + (OldLoc - GetActorLocation());
	if (AsdaAnimSmooth::SnapSmoothSec() <= 0.f || IsHidden() || Tail.Size() > AsdaAnimSmooth::SnapSmoothMaxUnits()) { Tail = FVector::ZeroVector; }
	VisualTail = Tail;
	MeshRoot->SetRelativeLocation(VisualTail);
	ApplyVisualTailToViews();
}

// ★ 22.10.2026 (ревизия плавности, ВАЖНО-А) — разбор у объявления. Свой персонаж = тот, кем управляет локальный контроллер.
static TAutoConsoleVariable<int32> CVarAsdaAnimSelfCameraTail(
	TEXT("asda.Anim.SelfCameraTail"), 1,
	TEXT("Сетевая поправка своего персонажа (4009, отказ, result 5): 1 = камера и плашка догоняют вместе с телом (экран не прыгает); ")
	TEXT("0 = догоняет только тело (камера прыгает сразу)."),
	ECVF_Default);

void AAsdaPlayerCharacter::ApplyVisualTailToViews()
{
	const AController* C = GetController();
	const bool bSelf = C && C->IsLocalPlayerController();
	const bool bOn = bSelf && CVarAsdaAnimSelfCameraTail.GetValueOnGameThread() != 0;
	if (!bOn && !bTailOnViews) { return; }
	if (!bTailBasesCaptured)
	{
		bTailBasesCaptured = true;
		TailBaseCamera = CameraArm ? CameraArm->GetRelativeLocation() : FVector::ZeroVector;
		TailBaseNameplate = Nameplate ? Nameplate->GetRelativeLocation() : FVector::ZeroVector;
	}
	const FVector Tail = bOn ? VisualTail : FVector::ZeroVector;
	if (CameraArm) { CameraArm->SetRelativeLocation(TailBaseCamera + Tail); }
	if (Nameplate) { Nameplate->SetRelativeLocation(TailBaseNameplate + Tail); }
	bTailOnViews = bOn && !Tail.IsZero();
}

void AAsdaPlayerCharacter::NetMove(const FVector2D& CurGame, const FVector2D& DstGame, float SpeedUnitsPerMs)
{
	// Идти В ТОЧКУ (click-to-move): линейная интерполяция к dst, мгновенный поворот к цели.
	// ★ НЕ снапить в серверный cur (он ОТСТАЁТ на RTT): грант 4007 result=1 приходит на каждый ход, и снап
	//   дёргал персонажа НАЗАД («микро-телепорт» — юзер 2026-07-23). Эталон Recv_EntityMove case 1: cur
	//   используется ТОЛЬКО для расчёта времени пути, движение к dst идёт от текущей позиции; снап — отдельный
	//   case 5 (античит, у нас NetSnap). Снапим только при большом рассинхроне (>150 юнитов).
	// ★ Грант 4007 во время КЛАВИАТУРНОГО движения (bMoveToPoint=false): курсом/лицом/клипом рулит
	//   контроллер (оптимистичный ввод; эталон Recv_EntityMove case 1: cur — только для тайминга пути).
	//   Иначе каждый грант на кадр разворачивал пятящегося персонажа лицом по ходу движения
	//   (микро-разворот раз в тик — юзер 2026-07-29). От сервера берём только скорость.
	if (bMoving && !bMoveToPoint)
	{
		if (SpeedUnitsPerMs > 0.001f) { MoveSpeed = SpeedUnitsPerMs; }
		return;
	}
	MoveDstGame = DstGame;
	MoveSpeed = (SpeedUnitsPerMs > 0.001f) ? SpeedUnitsPerMs : 0.35f;
	bMoving = true; bMoveToPoint = true;
	// Click-to-move всегда бежит вперёд; если до этого пятились — сменить клип на run01.
	// ★ 24.10.2026: смена клипа — после курса (ниже): знак темпа приседа считается от нового курса.
	const bool bBackReset = bBackpedal;
	bBackpedal = false;
	const FVector L = GetActorLocation();
	const FVector2D NowGame = AsdaCoord::UeToGame2D(L);   // ★ Аудит №3 (B1)
	if (FVector2D::Distance(NowGame, CurGame) > AsdaCoord::kDesyncSnapUnits)   // ★ Аудит №3 (B9): порог 150
	{
		const FVector2D UeXY = AsdaCoord::GameToUe2D(CurGame);   // ★ Аудит №3 (B1)
		SetActorLocation(FVector(UeXY.X, UeXY.Y, StandZ(UeXY.X, UeXY.Y)));   // ★ 23.10.2026: StandZ = земля, в глубокой воде — у поверхности (донор)
		AddVisualTail(L);   // ★ 22.10.2026: тело догоняет плавно (логика — уже в cur)
		FaceGameDir(DstGame - CurGame);
	}
	else
	{
		FaceGameDir(DstGame - NowGame);
	}
	if (bBackReset && AnimState == EAsdaPlayerState::Move) { PlayMoveClipAfterBackpedalSwitch(); }
	SetAnimState(EAsdaPlayerState::Move);
}

// ★ 27.09.2026 — `charMoveToWorldPos@0xA1E3D0` (разбор — `Reports/owner_0927/move_4007.md` §2.4).
//   Эталон: берёт ТЕКУЩУЮ позицию персонажа (`sub_93CB95(char+47649)`), направление = цель − позиция, скорость =
//   параметр как есть (`pChar[47744] = moveSpeed`), длительность = расстояние / скорость; поворот к цели; состояние
//   «идёт». Снапа в серверный cur нет вовсе — поэтому и здесь его нет (в отличие от `NetMove`, у которого порог 150).
//   ⚠ Ветка эталона «та же клетка» (текущая и целевая точки в одной 100-юнитовой клетке, персонаж стоит) меняет
//   только направление без рестарта шага — у нас непрерывный шаг, разница невидима; отмечено в отчёте.
void AAsdaPlayerCharacter::NetMoveFromActor(const FVector2D& DstGame, float SpeedUnitsPerMs)
{
	if (!(SpeedUnitsPerMs > 0.f)) { return; }   // вызывающий гарантирует > 0; защита от деления в Tick не нужна, но ход с нулём бессмыслен
	MoveDstGame = DstGame;
	MoveSpeed = SpeedUnitsPerMs;
	bMoving = true; bMoveToPoint = true;
	// Ход «в точку» вперёд, КРОМЕ эталонной клавиатурной «пятки» (`bEtalonBackpedal` = [208], см. `SetEtalonBackpedal`):
	//   если признак сменился — клип состояния перезапускается (как в `NetMove` при сбросе пятки).
	const bool bWantBack = bEtalonBackpedal;
	const bool bBackSwitch = (bBackpedal != bWantBack);
	bBackpedal = bWantBack;
	const FVector2D ToDst = DstGame - AsdaCoord::UeToGame2D(GetActorLocation());
	FaceGameDir((bBackpedal && !SwimFacesMoveDir()) ? -ToDst : ToDst);   // [ОРИГИНАЛ] пятясь — лицом на −направление шага (читатель [208] 0xA236DD); ★ 09.11 плывущий — по ходу
	// ★ 24.10.2026: клип — ПОСЛЕ курса (знак темпа приседа считается от нового курса).
	if (bBackSwitch && AnimState == EAsdaPlayerState::Move) { PlayMoveClipAfterBackpedalSwitch(); }
	SetAnimState(EAsdaPlayerState::Move);
}

// ★ 27.09.2026 — признак «S нажата» эталонной клавиатурной схемы (обоснование — в объявлении).
void AAsdaPlayerCharacter::SetEtalonBackpedal(bool bOn)
{
	bEtalonBackpedal = bOn;
	// Действует только на идущий ход «в точку» (клавиатурный грант). Режим «по направлению» (ключ 0) пятку ведёт сам.
	if (!bMoving || !bMoveToPoint || bBackpedal == bOn) { return; }
	bBackpedal = bOn;
	const FVector2D ToDst = MoveDstGame - GetGamePosXZ();
	if (!ToDst.IsNearlyZero()) { FaceGameDir((bOn && !SwimFacesMoveDir()) ? -ToDst : ToDst); }   // ★ 09.11 плывущий — по ходу
	// ★ 24.10.2026: клип — ПОСЛЕ курса (знак темпа приседа считается от нового курса).
	if (AnimState == EAsdaPlayerState::Move && !IsAttackAnimActive()) { PlayMoveClipAfterBackpedalSwitch(); }
}

bool AAsdaPlayerCharacter::SetServerMoveSpeed(float SpeedUnitsPerMs)
{
	// ★ 6071. Гейт эталона — состояние действия `== 1`, то есть «персонаж идёт» (в `CChar_SetState`
	//   case 1/2 ставится слот анимации движения). Наш эквивалент — `bMoving`: он покрывает и клик-мув,
	//   и клавиатуру, как и состояние 1 у эталона. Стоящему скорость не меняем: эталон в этом случае
	//   не делает ничего, а следующий ход всё равно принесёт актуальную скорость в 4007.
	if (!bMoving) { return false; }
	// ★ Пороги — те же, что в сетевом слое для 4007/4062/6071 (было рассогласование: там 0.0001…5.0,
	//   здесь только > 0.001 и без верхней границы). У эталона фильтра нет вовсе; сервер клампит
	//   скорость в 0…0.66, так что фильтр — наша защита от мусора, и он должен быть одинаковым везде.
	// ★ ОТЛИЧИЕ ОТ ЭТАЛОНА, оставлено сознательно: ровно 0 эталон бы принял (и персонаж встал бы),
	//   а мы игнорируем. Сервер шлёт 0 только при клампе снизу, и остановка приходит своими пакетами
	//   (4008/4009); принимать 0 здесь опасно — у эталона на этом пути считается время шага делением
	//   на скорость. Записано в PORT_PENDING_ON_SYSTEMS.md.
	if (!(SpeedUnitsPerMs > 0.0001f && SpeedUnitsPerMs < 5.f)) { return false; }
	MoveSpeed = SpeedUnitsPerMs;
	return true;
}

void AAsdaPlayerCharacter::SetMoveDirection(const FVector2D& DirGame, float SpeedUnitsPerMs,
	const FVector2D& InFaceDirGame, bool bInBackpedal)
{
	// Непрерывное движение (WASD): едем в направлении, пока задано; стоп по StopMove (отпускание клавиш).
	if (DirGame.IsNearlyZero()) { StopMove(); return; }
	MoveDirGame = DirGame.GetSafeNormal();
	FaceDirGame = InFaceDirGame.IsNearlyZero() ? MoveDirGame : InFaceDirGame.GetSafeNormal();
	MoveSpeed = (SpeedUnitsPerMs > 0.001f) ? SpeedUnitsPerMs : 0.35f;
	bMoving = true; bMoveToPoint = false;
	// ★ Смена бег↔пятки на лету (нажали/отпустили S во время движения) — перезапустить клип состояния.
	const bool bClipSwitch = (bBackpedal != bInBackpedal);
	bBackpedal = bInBackpedal;
	FaceGameDir(SwimFacesMoveDir() ? MoveDirGame : FaceDirGame);   // ★ 09.11 плывущий — по ходу
	if (AnimState == EAsdaPlayerState::Move)
	{
		if (bClipSwitch && !IsAttackAnimActive()) { PlayMoveClipAfterBackpedalSwitch(); }   // ★ 24.10.2026: присед — только знак темпа
	}
	else { SetAnimState(EAsdaPlayerState::Move); }
}

void AAsdaPlayerCharacter::StopMove()
{
	if (!bMoving) { return; }
	bMoving = false;
	SetAnimState(EAsdaPlayerState::Idle);
}

void AAsdaPlayerCharacter::SetCombatStance(bool bOn)
{
	// ★ 15.10.2026: эталон `CChar_SetState(…, 20)` перевзводит `fAutoActionStartTime` каждым входом в подтип 20.
	if (bOn && GetWorld()) { CombatStanceSinceS = GetWorld()->GetTimeSeconds(); }
	if (bCombatStance == bOn) { return; }
	bCombatStance = bOn;
	ASDA_CTRACE_IF(AsdaCtPcOurs(this), EAsdaCT::State, TEXT("%s: боевая стойка %s (%s)%s"), *AsdaCtPcWho(this), bOn ? TEXT("ВКЛ") : TEXT("ВЫКЛ"),
		bOn ? TEXT("покой idle01, фиджетов нет") : TEXT("мирный покой idle02, фиджеты через 6–12 с"),
		(AnimState == EAsdaPlayerState::Idle && !IsAttackAnimActive()) ? TEXT(", клип покоя переключён сразу") : TEXT(""));
	// Если сейчас стоим — сразу переключить покой (боевой idle01 ↔ мирный idle02).
	if (AnimState == EAsdaPlayerState::Idle && !IsAttackAnimActive())
	{
		PlayWorldClip(bOn ? IdleCombatClipIdx() : IdleMainClipIdx(), true);
	}
	IdleTimer = 0.f; FidgetRemaining = 0.f;   // в стойке фиджетов нет (эталон: боевой покой статичен)
	UE_LOG(LogAsdaPlayer, Log, TEXT("[стойка] зона=%s бой=%s оружие=subType %d набор=_%02d меш на кости=%s"),
		bWeaponMotionHidden ? TEXT("город/седло") : TEXT("поле"), bOn ? TEXT("да") : TEXT("нет"),
		EquippedWeaponSubType, CurMotionSet(), *CurrentWeaponBone().ToString());
}

void AAsdaPlayerCharacter::StopMoving()
{
	if (!bMoving && MoveDirGame.IsNearlyZero()) { return; }
	bMoving = false;
	MoveDirGame = FVector2D::ZeroVector;
	MoveDstGame = GetGamePosXZ();   // цель = текущая точка (никакого доезда)
	if (AnimState == EAsdaPlayerState::Move) { SetAnimState(EAsdaPlayerState::Idle); }
}

void AAsdaPlayerCharacter::NetSnap(const FVector2D& CurGame)
{
	bMoving = false;
	const FVector OldLoc = GetActorLocation();
	const FVector2D UeXY = AsdaCoord::GameToUe2D(CurGame);   // ★ Аудит №3 (B1)
	SetActorLocation(FVector(UeXY.X, UeXY.Y, StandZ(UeXY.X, UeXY.Y)));   // ★ 23.10.2026: StandZ = земля, в глубокой воде — у поверхности (донор)
	AddVisualTail(OldLoc);   // ★ 22.10.2026: тело догоняет точку сервера плавно (asda.Anim.SnapSmoothSec; 0 — снап)
	// ★★ 25.10.2026 (ревизия sit108, ВАЖНО-1) [ОРИГИНАЛ]: свой 4009 (`Recv_EntityStop@0xF3AB50`, network.c:39466-39477) в своей ветке
	//   двигает ТОЛЬКО позицию (`player_setPositionAndTarget`); состояние — лишь при флаге отпускания клавиш `[228]` (`CharFx(0)`).
	//   «Сел на ходу»: `ToggleSitStand` шлёт 4008, затем 107; ответ 4009 приходит, когда персонаж уже в 7/8 — прежде порт поднимал его
	//   («встаёт» по 4009, 108 не уходил, по концу — лишний 0). Теперь 7/8/9 своего не трогаются. Прочие вызывающие сидящего не застают
	//   (ход, бой, отказ хода; поправка 4007/5 сначала снимает сидение сама). Ключ `asda.Char.SitExitEtalon 0` — прежнее.
	if (IsSitExitEtalon() && IsLocalPlayerCharacter())
	{
		const int32 Sit = GetEtalonSitState();
		if (Sit == 7 || Sit == 8 || Sit == 9)
		{
			UE_LOG(LogAsdaPlayer, Log, TEXT("[сидение] свой снап в (%.0f,%.0f) при состоянии %d — только позиция (эталон Recv_EntityStop)"),
				CurGame.X, CurGame.Y, Sit);
			return;
		}
	}
	SetAnimState(EAsdaPlayerState::Idle);
}

// ★ 22.10.2026 (ревизия плавности, ВАЖНО-Б) — разбор у объявления.
void AAsdaPlayerCharacter::NetSnapHard(const FVector2D& CurGame)
{
	bMoving = false;
	const FVector2D UeXY = AsdaCoord::GameToUe2D(CurGame);
	SetActorLocation(FVector(UeXY.X, UeXY.Y, StandZ(UeXY.X, UeXY.Y)));   // ★ 23.10.2026: StandZ = земля, в глубокой воде — у поверхности (донор)
	VisualTail = FVector::ZeroVector;
	if (MeshRoot) { MeshRoot->SetRelativeLocation(FVector::ZeroVector); }
	ApplyVisualTailToViews();
	SnapFacingToLogical();
	SetAnimState(EAsdaPlayerState::Idle);
}

void AAsdaPlayerCharacter::SetDashPosGame(const FVector2D& PosGame)
{
	bMoving = false;
	MoveDirGame = FVector2D::ZeroVector;
	MoveDstGame = PosGame;   // цель хода = точка рывка (никакого доезда после)
	const FVector2D UeXY = AsdaCoord::GameToUe2D(PosGame);
	SetActorLocation(FVector(UeXY.X, UeXY.Y, StandZ(UeXY.X, UeXY.Y)));
	VisualTail = FVector::ZeroVector;
	if (MeshRoot) { MeshRoot->SetRelativeLocation(FVector::ZeroVector); }
	ApplyVisualTailToViews();
}

// ★ Уносим с собой ВСЕ приаттаченные эффект-акторы (крылья, кольцо соулмейта, прочее): UE при Destroy
//   родителя дочерние акторы лишь ОТЦЕПЛЯЕТ — без этого крылья ушедшего игрока висели в воздухе.
//   Покрывает все пути ухода: деспавн 4011, AoI, смена карты, выход из мира.
void AAsdaPlayerCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
	// ★ charsel_preload: персонаж ушёл — его доля во флаге полных мипов отпускается (снимется, если больше ничьё).
	UAsdaTextureSubsystem::ReleaseAllFullMips(StableQualityMips);
	TArray<AActor*> Attached;
	GetAttachedActors(Attached);
	for (AActor* A : Attached)
	{
		if (A && !A->IsPendingKillPending()) { A->Destroy(); }
	}
	Super::EndPlay(Reason);
}

// ★ Поза трупа БЕЗ анимации падения — для игрока, который вошёл в мир уже мёртвым.
//   SetAnimState(Dead) выполняет весь эталонный набор состояния 6 (ссаживание с транспорта, снятие
//   свечения заточки, запуск клипа 18 без лупа), после чего мы перематываем клип в КОНЕЦ и
//   останавливаем: получается ровно тот кадр, на котором замирает обычная смерть.
void AAsdaPlayerCharacter::EnterDeadPoseInstant()
{
	const bool bWasDead = (AnimState == EAsdaPlayerState::Dead);
	SetAnimState(EAsdaPlayerState::Dead);
	if (bWasDead) { return; }   // уже лежим — клип не трогаем
	ASDA_CTRACE_IF(AsdaCtPcOurs(this), EAsdaCT::Death, TEXT("%s: поза трупа БЕЗ анимации падения (вошёл в мир мёртвым) — клип смерти перемотан в конец"),
		*AsdaCtPcWho(this));
	if (!BaseSkel) { return; }
	if (UAnimSingleNodeInstance* Single = BaseSkel->GetSingleNodeInstance())
	{
		if (UAnimSequence* Seq = Cast<UAnimSequence>(Single->GetAnimationAsset()))
		{
			Single->SetPosition(FMath::Max(0.f, Seq->GetPlayLength() - KINDA_SMALL_NUMBER), false);
			Single->SetPlaying(false);
			BaseSkel->RefreshBoneTransforms();
		}
	}
}

// ═══ ★ ОБЕСЦВЕЧИВАНИЕ СЦЕНЫ НА СМЕРТИ ═════════════════════════════════════════════════════════
//   Пост-обработка КАМЕРЫ (ColorSaturation) — она применяется к 3D-кадру ДО композита UMG, поэтому
//   окно выбора воскрешения остаётся цветным (требование юзера). Никаких материалов/ассетов не нужно,
//   параметры видны и правятся в редакторе на компоненте камеры.
void AAsdaPlayerCharacter::SetDeathDesaturation(bool bOn)
{
	bDeathDesatOn = bOn;
	if (DeathDesatBlendSec <= 0.f) { DeathDesatCur = bOn ? 1.f : 0.f; ApplyDeathDesaturation(); }
}

void AAsdaPlayerCharacter::ApplyDeathDesaturation()
{
	// Saturation 1 → 0 по доле обесцвечивания (W-компонента = общий множитель, держим 1).
	const float S = FMath::Clamp(1.f - DeathDesatCur, 0.f, 1.f);
	const bool bActive = (DeathDesatCur > KINDA_SMALL_NUMBER);

	// ── Слой 1: пост-обработка КОМПОНЕНТА КАМЕРЫ ──
	if (Camera)
	{
		FPostProcessSettings& PP = Camera->PostProcessSettings;
		PP.bOverride_ColorSaturation = bActive;
		PP.ColorSaturation = FVector4(S, S, S, 1.f);
		Camera->PostProcessBlendWeight = bActive ? 1.f : 0.f;
	}

	// ── Слой 2: ★ СОБСТВЕННЫЙ UNBOUND-ОБЪЁМ С ВЫСШИМ ПРИОРИТЕТОМ ──
	//   ЖИВОЙ ТЕСТ 2026-08-03: обесцвечивания на экране НЕ БЫЛО — на картах лежат наши PP_<map>-объёмы
	//   (свет 23 карт), и полагаться на порядок смешивания «камера против объёмов» нельзя. Поэтому
	//   ставим ЯВНЫЙ APostProcessVolume: bUnbound = true, Priority = 10000 (выше любого карт-объёма),
	//   BlendWeight = доля обесцвечивания. Так гаснет цвет ГАРАНТИРОВАННО и только у 3D-сцены —
	//   UMG композитится после пост-обработки, поэтому окно воскрешения остаётся ЦВЕТНЫМ.
	//   Актор виден и правится в редакторе (закон ue-editor-friendly).
	UWorld* W = GetWorld();
	if (!W) { return; }
	APostProcessVolume* Vol = DeathPPV.Get();
	if (!Vol)
	{
		if (!bActive) { return; }                       // нечего включать — объём не плодим
		// ★ E16 (14.09.2026): свой персонаж пересоздаётся в том же уровне (`AsdaWorldSubsystem`: `LocalPlayerActor->Destroy()`),
		//   а объём — служебный актор уровня и остаётся жить. Слабый указатель у нового персонажа пуст, и повторный
		//   `SpawnActor` с тем же именем при умолчании `NameMode = Required_Fatal` (движок, World.cpp:607) — ПАДЕНИЕ.
		//   Поэтому сначала подхватываем уже существующий объём (тот же приём, что у `AsdaGlowVolume`).
		Vol = FindObject<APostProcessVolume>(W->PersistentLevel, TEXT("AsdaDeathDesaturateVolume"));
		if (Vol)
		{
			DeathPPV = Vol;
			UE_LOG(LogAsdaPlayer, Log, TEXT("[смерть] ЧБ: подхвачен уже существующий PP-объём (персонаж пересоздан в том же уровне)"));
		}
	}
	if (!Vol)
	{
		FActorSpawnParameters SP;
		SP.Name = FName(TEXT("AsdaDeathDesaturateVolume"));
		SP.ObjectFlags |= RF_Transient;                 // служебный актор — в уровень не сохраняем
		Vol = W->SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass(), FTransform::Identity, SP);
		if (!Vol) { return; }
		Vol->bUnbound = true;
		Vol->Priority = 10000.f;
		Vol->Settings.bOverride_ColorSaturation = true;
		DeathPPV = Vol;
		UE_LOG(LogAsdaPlayer, Warning, TEXT("[смерть] ЧБ включён (PP-объём создан, приоритет %.0f, unbound=%d)"),
			Vol->Priority, Vol->bUnbound ? 1 : 0);
	}
	Vol->Settings.bOverride_ColorSaturation = true;
	Vol->Settings.ColorSaturation = FVector4(S, S, S, 1.f);
	Vol->BlendWeight = bActive ? 1.f : 0.f;
	Vol->bEnabled = bActive;
}

void AAsdaPlayerCharacter::Tick(float DeltaSeconds)
{
	FrameDtEma = FMath::Lerp(FrameDtEma, FMath::Clamp(DeltaSeconds, 0.001f, 0.25f), 0.1f);   // ★ 15.10.2026: оценка кадра для точки отправки
	Super::Tick(DeltaSeconds);
	AsdaApplyMapExposure(GetWorld(), Camera);   // ★ 28.10.2026: экспозицию задаёт объём пост-обработки карты, если он есть
	ON_SCOPE_EXIT { TickAnimSmooth(DeltaSeconds); TickJumpLift(DeltaSeconds); };   // ★ 22.10.2026: доворот модели и хвост поправки — после всей логики кадра; ★ 24.10.2026: подъём «шапки» в прыжке
	TickEmotionWatch();
	TickFishClipWatch();                 // ★ В5: сторож клипа действия рыбалки
	TickFishAttachments(DeltaSeconds);   // ★ В5: удочка и рыба на крючке (вставка/снятие/посадка на кости)
	TickShovel(DeltaSeconds);            // ★ 10.10.2026: копание — удары по порогам, перемотка 43, конец клипа, гашение стартового эффекта
	ApplyStableQuality();
	// ★ Режим «душа»: сверка с ключом отката + подхват новых слотов (раз в секунду).
	TickSoulTranslucency();
	TickFaceExpression(DeltaSeconds);    // ★ 07.11.2026: выражение лица (режим, моргание)

	// ★ Плавный переход цвет ↔ ч/б (смерть / воскрешение).
	{
		const float Target = bDeathDesatOn ? 1.f : 0.f;
		if (!FMath::IsNearlyEqual(DeathDesatCur, Target, 0.001f))
		{
			const float Step = (DeathDesatBlendSec > 0.f) ? (DeltaSeconds / DeathDesatBlendSec) : 1.f;
			DeathDesatCur = FMath::FInterpConstantTo(DeathDesatCur, Target, 1.f, Step);
			ApplyDeathDesaturation();
		}
	}

	// ★ «Оружие убрано» (город / седло) → безоружный набор моушенов. Эталон проверяет ЭТО КАЖДЫЙ ТИК
	//   (charUpdateActionStateTick@0xA216B0) и на переходе перезагружает набор — потому набор меняется
	//   прямо при входе в город и выходе из него.
	UpdateWeaponMotionHidden();

	// ★ 31.10.2026 [ОРИГИНАЛ] Свечение заточки — состояние кадра (эталон `CCharacter_UpdateHeldItemEffect` каждый кадр):
	//   пропавшее или залипшее скрытым возвращается само (`asda.WeaponGlow.Persist`; разбор — у определения в _Visuals.cpp).
	AsdaWeaponGlowReconcile(this);
	AsdaPartFxTick(this);
	AsdaSowelGlowTick(this, DeltaSeconds);   // ★ 05.11.2026: обводка самоцвета (ping-pong pbbBuildStripBatch), только у своего

	// ★ ТАНЦПОЛ В ALPEN: стоишь на полу — персонаж танцует сам (см. TickAutoDanceZone).
	TickAutoDanceZone(DeltaSeconds);

	// ★ СЛЕДЫ И ЗВУК ШАГОВ по материалу поверхности под ногами (см. TickFootsteps).
	TickFootsteps(DeltaSeconds);

	// ★ ТРАНСПОРТ: шлейф и звук езды (эталонная модель — см. TickRideEffect).
	if (bMounted)
	{
		TickRideEffect(DeltaSeconds);
		// ★ ФЛАГ КЛАНА: покой = случайное чередование клипов 0/1 по их окончании, движение = клип 2.
		TickClanFlagAnim(DeltaSeconds);
	}

	// ★ РАЗВОРОТ К ЦЕЛИ В ЗАМАХЕ — 1:1 objAI_FaceTargetByUserIdx@0xA23350 (вызов каждый кадр из цикла
	//   CharManager, entity_char.c:8541): пока персонаж в состоянии атаки и цель жива в списке сущностей,
	//   dir = normalize(posЦели − posСвоя). Без этого ЧУЖОЙ игрок бил/стрелял «стоя спиной» (юзер
	//   2026-08-02): его замах запускался по 4029, но yaw никто не ставил. Не в движении (эталон:
	//   state-атаки исключает state-движения).
	// ★★★ ДОБАВЛЕНО 2026-08-10: тот же доворот работает и в ПРИМЕНЕНИИ НАВЫКА. Гейт эталона —
	//   `state ∈ {5, 24, 14, 27, 13}` (entity_char.c:23317), т.е. не только замах (5), но и
	//   состояния каста 24/5257 и 14/5270. Цель для них ставит мир-подсистема из 29-байтных записей
	//   пакета (OnSkillCastRecs), ровно как `Recv_SkillCast` пишет `targetRenderId = targetWords[0]`.
	// ★★★ 18.10.2026 (ключ `asda.Combat.DeathAtLastPendingHit`, разбор у объявления в AsdaWorldSubsystem.cpp) [ОРИГИНАЛ]:
	//   `charUpdateActionStateTick` состояние 5 (entity_char.c:23132–23137) — цель в состоянии 10 (или её HP ≤ 0) → в ПЕРВОМ ЖЕ тике
	//   `CChar_SetState(self, 0, 0, 20)`: боевая стойка, текущий взмах не доигрывается. Для любого персонажа (свой и сосед): у
	//   соседа порт прежде доигрывал отрезок/проход клипа по трупу до тишины пакетов. Свой обычно уже снят `ClearTarget` в момент
	//   смерти — здесь страховка того же правила.
	if (IsAttackAnimActive() && !IsSkillFaceActive() && UAsdaWorldSubsystem::IsDeathAtLastPendingHit())
	{
		if (const AAsdaMobActor* DeadTgt = Cast<AAsdaMobActor>(AttackFaceTarget.Get()); DeadTgt && DeadTgt->IsDead())
		{
			ASDA_CTRACE_IF(AsdaCtPcOurs(this), EAsdaCT::Swing, TEXT("%s: цель замаха %s — ТРУП: сразу боевая стойка 0/0/20, взмах не доигрывается (эталон entity_char.c:23132)"),
				*AsdaCtPcWho(this), *AsdaCtPcTarget(AttackFaceTarget.Get()));
			StopAttackAnim();
		}
		// ★ Ревизия 18.10 (п.7) [ОРИГИНАЛ entity_char.c:23124–23130]: `Entity_FindByRenderId` цели не нашёл (сущность снята 4011/AoI) →
		//   та же стойка 0/0/20. Слабая ссылка на цель была, а актора больше нет.
		else if (AttackFaceTarget.IsStale())
		{
			ASDA_CTRACE_IF(AsdaCtPcOurs(this), EAsdaCT::Swing, TEXT("%s: цели замаха больше нет в мире (4011/AoI) — сразу боевая стойка 0/0/20 (эталон entity_char.c:23128)"),
				*AsdaCtPcWho(this));
			StopAttackAnim();
		}
	}
	if (!bMoving && (IsAttackAnimActive() || IsSkillFaceActive()))
	{
		if (AActor* FaceTgt = AttackFaceTarget.Get())
		{
			const FVector TL = FaceTgt->GetActorLocation();
			FaceGameDir(AsdaCoord::UeToGame2D(TL) - GetGamePosXZ());   // ★ Аудит №3 (B1)
		}
	}

	// ★ Бездействие: стоим в Idle → через рандом.задержку играем фиджет idle03 один раз, затем назад в idle02.
	//   (реверс: state 0 держит слоты idle01-03, вариативность в апдейте; idle01=боевой резерв, в мире не крутим)
	// ★★★ ГЕЙТ ПРИМЕНЕНИЯ НАВЫКА: фиджет — ветка СОСТОЯНИЯ 0, а во время каста эталонный персонаж
	//   стоит в состоянии 24 и до неё не доходит (см. SkillAnimUntilS в заголовке). Без этого гейта
	//   фиджет обрывал клип навыка (у соседа — почти всегда: он в покое).
	// ★★★ И ГЕЙТ ЭМОЦИИ (владелец 2026-08-25: «длинные эмоции не доигрывают, сбрасывает idle»).
	//   Фиджет взводится через случайные 6…12 с — ровно в это окно и попадал танец длиной 13.3 с
	//   (сторож поймал подмену на 7.82 с). У ветки уже был гейт применения навыка, про эмоцию она
	//   не знала. Заодно держим отсчёт до фиджета сброшенным, пока эмоция играет: иначе он
	//   сработает в первый же кадр после её конца.
	// ★ Волна В5: действие рыбалки 60…67 — не состояние 0, ветка фиджета эталона до него не доходит.
	// ★ В45fix2 (МЕЛК-5): и состояние 68 «добыча» (не состояние 0; клип его — петля трека 36, фиджета нет).
	// ★ В3 S7: четыре признака — одно состояние `IsEtalonActionBusy`.
	// ★★★ 15.10.2026 (ритм атаки, самотест `asda.Combat.SelfTestRhythm`): и ЗАМАХ, и БОЕВАЯ СТОЙКА. Фиджет — ветка
	//   состояния 0 эталона; в состоянии 5 (замах) до неё не доходит, а в боевом покое (подтип 20) фиджета нет вовсе —
	//   об этом и говорит сброс счётчиков в `SetCombatStance`. Но счётчик копился и во время боя: через 6–12 с idle03
	//   ложился ПОВЕРХ клипа замаха (самотест поймал: двуручный меч, позиция клипа 0.875 вместо 6.93 в точке удара) —
	//   картинка уходила от отправки ударов на целый проход.
	//   Под ключом `asda.Combat.ServerRhythm` (полный откат — прежнее поведение).
	// ★★ Ревизия СРЕДН-1: боевая стойка соседа больше не вечная — выход по таймеру эталона ниже (`TickCombatStanceExit`).
	TickCombatStanceExit();
	const bool bRhythmFix = UAsdaAttackRhythmSubsystem::IsEnabled();
	// ★★★ 18.10.2026 (разброс смерти моба у АТАКУЮЩЕГО, самотест `asda.Combat.SelfTestObserverSync`, Ф1 «добыча 5005»): с Ф2
	//   момент своего удара (`OwnPlanDelay` → `UpcomingHitDelaySec`) берётся из ПОЗИЦИИ КЛИПА замаха и при `ServerRhythm 0` тоже
	//   (ключ `SwingPlanFromPackets`). Гейт «фиджет не ложится на идущий замах» оставался только под `ServerRhythm`, и под
	//   `ServerRhythm 0` idle03 срабатывал посреди замаха: прогон final/r5_coord — ответ 4029 через 0.213 с застал «позицию клипа
	//   0.128 с» (это уже idle03), удар перепланирован на +0.339 с — цифра 0.554 и труп 0.607 против 0.468/0.521 у наблюдателя.
	//   [ОРИГИНАЛ] фиджет — ветка состояния 0, в замахе (состояние 5) до неё не доходит ни в каком режиме. Замах теперь закрыт
	//   от фиджета и при `ServerRhythm 0`, пока включён Ф2; полный откат — оба ключа в 0 (прежний порт).
	const bool bSwingGuard = bRhythmFix || IsSwingPlanFromPackets();   // ★ 18.10.2026: замах закрыт от фиджета
	// ★ 23.10.2026 (донор AsdaFableClassic): клавиша прыжка, вода, брызги. Фиджета нет в воде (ветка плавания донора перебивает
	//   слот покоя каждый кадр) и во время прыжка (клип прыжка — не состояние покоя).
	TickSwimAndJump(DeltaSeconds);
	if (IsEtalonActionBusy() || (bSwingGuard && IsAttackAnimActive()) || (bRhythmFix && bCombatStance) || bSwimming || bJumpActive
		|| AsdaCrouchStand::Shown(this)) { IdleTimer = 0.f; FidgetRemaining = 0.f; }   // ★ 05.11.2026: присед стоя — без фиджета idle03
	else if (!bMoving && AnimState == EAsdaPlayerState::Idle && !IsSkillAnimActive())
	{
		if (FidgetRemaining > 0.f)
		{
			FidgetRemaining -= DeltaSeconds;
			if (FidgetRemaining <= 0.f)
			{
				PlayWorldClip(IdleMainClipIdx(), true);   // фиджет закончился → назад в мирный покой idle02
				IdleTimer = 0.f;
				NextFidgetDelay = FMath::RandRange(6.f, 12.f);
			}
		}
		else
		{
			IdleTimer += DeltaSeconds;
			if (IdleTimer >= NextFidgetDelay)
			{
				if (IsAttackAnimActive())   // ★ 18.10.2026: виден только в полном откате (оба ключа 0) — фиджет обрывает замах
				{
					UE_LOG(LogAsdaPlayer, Log, TEXT("[аним] фиджет idle03 ПОВЕРХ идущего замаха (откат: ServerRhythm 0 и SwingPlanFromPackets 0)"));
					ASDA_CTRACE_IF(AsdaCtPcOurs(this), EAsdaCT::Swing, TEXT("%s: фиджет idle03 ПОВЕРХ идущего замаха (позиция клипа %.3f с) — гейт bSwingGuard выключен (ServerRhythm 0 и SwingPlanFromPackets 0)"),
						*AsdaCtPcWho(this), GetAttackClipPositionSec());
				}
				ASDA_CTRACE_V(EAsdaCT::State, TEXT("%s: фиджет покоя idle03 после %.1f с бездействия (стойка=%s)"),
					*AsdaCtPcWho(this), IdleTimer, bCombatStance ? TEXT("да") : TEXT("нет"));
				const float Len = PlayWorldClip(IdleFidgetClipIdx(), false);   // idle03 одноразово
				FidgetRemaining = (Len > 0.1f) ? Len : 2.f;
				IdleTimer = 0.f;
			}
		}
	}

	if (!bMoving || AnimState == EAsdaPlayerState::Dead) { return; }

	const FVector2D CurGame = GetGamePosXZ();
	const float Step = MoveSpeed * 1000.f * DeltaSeconds;   // юнит/мс → см/с

	if (bMoveToPoint)
	{
		const FVector2D Delta = MoveDstGame - CurGame;
		const float Dist = Delta.Size();
		if (Dist <= Step || Dist < 1.f)
		{
			// Финальный тайл цели непроходим → стоп у текущей позиции (не входим в стену).
			// ★ 27.09.2026: каждое окончание хода «в точку» самим персонажем считается (`PointMoveEndSerial`) — по нему
			//   клавиатурная схема эталона шлёт 4008 (см. объявление `GetPointMoveEndSerial`).
			if (!IsGameWalkable(MoveDstGame)) { ++PointMoveEndSerial; StopMove(); return; }
			const FVector2D UeXY = AsdaCoord::GameToUe2D(MoveDstGame);   // ★ Аудит №3 (B1)
			SetActorLocation(FVector(UeXY.X, UeXY.Y, StandZ(UeXY.X, UeXY.Y)));   // ★ 23.10.2026: StandZ = земля, в глубокой воде — у поверхности (донор)
			++PointMoveEndSerial;
			StopMove();
			return;
		}
		const FVector2D Dir = Delta / Dist;
		const FVector2D NewGame = CurGame + Dir * Step;
		if (!IsGameWalkable(NewGame)) { ++PointMoveEndSerial; StopMove(); return; }   // упёрлись в непроходимый тайл
		const FVector2D UeXY = AsdaCoord::GameToUe2D(NewGame);   // ★ Аудит №3 (B1)
		SetActorLocation(FVector(UeXY.X, UeXY.Y, StandZ(UeXY.X, UeXY.Y)));   // ★ 23.10.2026: StandZ = земля, в глубокой воде — у поверхности (донор)
		// ★ 27.09.2026: пятясь (эталонная клавиатурная «пятка» [208]) — лицом на −направление шага; иначе — по шагу.
		// ★ 09.11.2026 [ИСПРАВЛЕНИЕ]: плывущий — по шагу и пятясь (`asda.Swim.FaceMoveDir`).
		FaceGameDir((bBackpedal && !SwimFacesMoveDir()) ? -Dir : Dir);
	}
	else
	{
		// Непрерывный режим (WASD): шаг в текущем направлении. Дистанция = скорость × время удержания.
		const FVector2D NewGame = CurGame + MoveDirGame * Step;
		FaceGameDir(SwimFacesMoveDir() ? MoveDirGame : FaceDirGame);   // лицом по FaceDir (пятясь — вперёд; ★ 09.11 плывущий — по ходу); и при упоре
		if (!IsGameWalkable(NewGame)) { return; }   // непроходимо → стоим на месте (клавишу не гасим — можно свернуть)
		const FVector2D UeXY = AsdaCoord::GameToUe2D(NewGame);   // ★ Аудит №3 (B1)
		SetActorLocation(FVector(UeXY.X, UeXY.Y, StandZ(UeXY.X, UeXY.Y)));   // ★ 23.10.2026: StandZ = земля, в глубокой воде — у поверхности (донор)
	}
}

// ★★★ [ИСПРАВЛЕНИЕ 11.11.2026, задача A3] «Верхом можно на несколько кадров заехать в закрытую клетку» (самотест
//   `asda.SelfTest.SwimWalls`, шаг S5, Альпен, клетка 106,391: 5 опросов подряд персонаж стоял в клетке стены при защите 1).
//   ПРИЧИНА — дефект порта, у оригинала его нет. Позиция персонажа у порта хранится с двойной точностью, а шаг хода проверял
//   клетку через `UAsdaWalkGrid::IsWorldWalkable(float, float)`: координата сначала округлялась до одинарной точности. У границы
//   клетки на игровых координатах 16 384…65 536 шаг одинарной точности — 1/256 единицы, поэтому точка, лежащая в закрытой клетке
//   ближе 0,002 единицы к границе (например z = 39 199,9990 при границе 39 200), округлялась РОВНО в границу и считалась клеткой
//   по эту сторону — проходимой. Шаг разрешался, персонаж вставал в закрытую клетку (на тысячные доли единицы вглубь), а
//   клетку стояния читают по точной координате (`AsdaCoord::GameToTile`: самотест стен, автопуть, мини-карта). Дефект
//   односторонний: только когда закрытая клетка лежит со стороны МЕНЬШЕЙ координаты (ход в −X/−Z). От транспорта не зависит —
//   верхом и пешком шаг один и тот же; упирающийся в стену персонаж короткими шагами подползает к границе, и попыток много.
//   [ОРИГИНАЛ] позиция и её клетка считаются из одного и того же числа одинарной точности (`(int)(pos / 100)`, проверка шага —
//   байт флага тайла `.TER`, см. `walkable_grid_re.md`), расхождения «клетка проверки ≠ клетка стояния» у него быть не может.
//   ПРАВКА: шаг разрешён, только если проходима клетка И по точной координате (где персонаж встанет), И по округлённой
//   (какую позицию увидит сервер в 4006/4008 и вернёт в 4009). Вторая половина — прежняя проверка: без неё открылся бы
//   зеркальный случай (закрытая клетка со стороны БОЛЬШЕЙ координаты: персонаж в открытой клетке вплотную к границе, а в пакет
//   уходит точка ровно на границе, то есть в закрытой клетке). Пакеты хода не меняются. Ключ `asda.Walk.StepTileExact`:
//   1 — обе проверки (умолчание); 0 — прежнее поведение (только округлённая). Самотест — `asda.SelfTest.StepTileExact`.
//   Не тронуто: `IsWorldWalkable(float, float)` у клика по земле и точки навыка «по земле» — там проверяется цель, а не шаг.
static ASDA_DEV_CVAR(int32, CVarAsdaWalkStepTileExact,
	TEXT("asda.Walk.StepTileExact"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Шаг хода проверяет клетку и по точной координате (клетка стояния), и по округлённой (позиция в пакете): "
	     "1 — да (персонаж не встаёт в закрытую клетку вплотную к её границе); 0 — прежнее (только округлённая: полоска 0,002 ед. "
	     "у границы закрытой клетки считалась проходимой)."),
	ECVF_Default);

// Проходима ли game-точка по сетке карты (DA_WalkGrid). Нет сетки → разрешаем (fail-open).
bool AAsdaPlayerCharacter::IsGameWalkable(const FVector2D& GameXZ) const
{
	if (!WalkGrid) { return true; }
	const bool bRounded = WalkGrid->IsWorldWalkable(GameXZ.X, GameXZ.Y);   // прежняя проверка: координата в одинарной точности
	if (CVarAsdaWalkStepTileExact.GetValueOnGameThread() == 0) { return bRounded; }
	const FIntPoint Tile = AsdaCoord::GameToTile(GameXZ);                  // клетка стояния — по точной координате
	const bool bExact = WalkGrid->IsTileWalkable(Tile.X, Tile.Y);
	if (bRounded && !bExact)
	{
		// Тот самый случай: прежняя проверка пустила бы в закрытую клетку. Не чаще раза в секунду — для разбора журналов.
		static double LastLogS = 0.0;   // время последней строки (игровой поток)
		const double NowS = FPlatformTime::Seconds();
		if (NowS - LastLogS > 1.0)
		{
			LastLogS = NowS;
			UE_LOG(LogAsdaPlayer, Log, TEXT("[ход] шаг в (%.6f,%.6f) отклонён: точка в закрытой клетке (%d,%d) вплотную к границе — прежняя проверка её пропускала"),
				GameXZ.X, GameXZ.Y, Tile.X, Tile.Y);
		}
	}
	return bExact && bRounded;   // обе: точная — где встанет персонаж, округлённая — что увидит сервер
}
