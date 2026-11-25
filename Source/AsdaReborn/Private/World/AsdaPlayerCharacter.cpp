// Copyright (c) 2026 @sitxovski (Discord)
// ★ 23.10.2026 РАЗНОС ФАЙЛА (слово владельца «огромные файлы разделить … с гарантией 100 %»): методы AAsdaPlayerCharacter
//   разнесены по группам в AsdaPlayerCharacter_{Attack,Move,Fish}.cpp; общее (LogAsdaPlayer, чистые константы и хелперы) —
//   в AsdaPlayerCharacterInternal.h. Строки перенесены БАЙТ В БАЙТ:
//   `python Tools/asda_split_cpp.py verify --plan Reports/split_2026-10-23/AsdaPlayerCharacter.plan.json`.
//   Здесь осталось ядро: конструктор, внешность и оружие, мировые клипы, сесть/встать, занятость, качество, полупрозрачность «души» и GM-скрытия (с консолью «душа»).
// Реализация AAsdaPlayerCharacter — см. AsdaPlayerCharacter.h.
#include "World/AsdaPlayerCharacter.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "World/AsdaPlayerController.h"   // ★ ритм ударов задаёт тик анимации (OnSwingCycleCompleted)
#include "World/AsdaNockedArrow.h"        // ★ 05.11.2026: снятие стрелы на тетиве сменой/уборкой оружия
#include "Misc/App.h"   // FApp::GetDeltaTime — оценка кадра (точка отправки за кадр до дедлайна)
#include "World/AsdaAttackRhythmSubsystem.h"   // ★ 15.10.2026: серверные часы атаки — темп отрезка замаха до точки отправки
#include "Engine/Texture2D.h"          // ★ постоянный форс мипов у текстур персонажа
#include "Engine/StreamableRenderAsset.h"        // ★ charsel_preload: владение флагом полных мипов
#include "Resources/AsdaTextureSubsystem.h"      // ★ charsel_preload: реестр владения флагом полных мипов
#include "Materials/MaterialInterface.h"
#include "Components/StaticMeshComponent.h"
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д8/Д9: ресурс только через менеджер
#include "Resources/AsdaSoundSubsystem.h"       // ★ Аудит №3 (A9): звук транспорта — только через менеджер
#include "Items/AsdaItemDbRows.h"     // FAsdaComboTimeRow — тайминги ударов замаха (combo_time)
#include "Engine/DataTable.h"
#include "World/AsdaGroundTrace.h"
#include "World/AsdaCapsuleShadow.h"   // ★ Сет B: капсульная тень тела — одна точка на порт (игрок/моб/NPC)
#include "World/AsdaCoord.h"                    // ★ Аудит №3 (B1/B2/B9): единый перевод координат игра↔UE
#include "World/AsdaNameplateComponent.h"
#include "World/AsdaFadeComponent.h"      // ★ режим «душа»: снять спавн-фейд (эталон его у души не умножает)
#include "Components/MeshComponent.h"     // ★ режим «душа»: подмена слотов материалов тела и частей
#include "Components/CapsuleComponent.h"

#include "Items/AsdaItemTableSubsystem.h"
#include "Characters/AsdaCharAssembly.h"   // кости/трансформы оружия «в руке ↔ за спиной»
#include "World/AsdaWorldSubsystem.h"   // LocalPlayerActor — «свой» персонаж (звук 2D против 3D)
#include "Engine/GameInstance.h"         // ★ В5fix: GetGameInstance()->GetSubsystem (сборка без unity)
#include "AsdaEffectActor.h"            // шлейф езды = AAsdaEffectActor (attach-режим 6)
#include "Skill/AsdaSkillFxSubsystem.h" // ★ 01.11.2026 (soulmate-glow): AsdaSoulSkillGlowFactor — множитель показа навыков Дружбы
#include "AsdaFxNames.h"               // ★ дедуп №2 В1-C (F6): теги эффектов персонажа + общее гашение по тегу
#include "Resources/AsdaMaterialNames.h"   // ★ дедуп №2 В1-C (F10): пути мастеров и имена параметров
#include "World/AsdaWalkGrid.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "World/AsdaCameraArmComponent.h"   // ★ 25.10.2026: стрела камеры с правилом оригинала (cameraUpdate@0x959880)
#include "Camera/CameraComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"   // поза трупа: перемотка клипа death01 в последний кадр
#include "Engine/PostProcessVolume.h"                  // ч/б сцены на смерти: unbound-объём с высшим приоритетом
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"                 // ★ 10.10.2026 (копание): меш инструмента на `8_a1`
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/TextureRenderTarget2D.h"
#include "World/AsdaFishingSubsystem.h"      // ★ В5: конец клипа своего действия рыбалки → автомат; Fish_data (модель рыбы)
#include "TimerManager.h"                  // ★ В5: таймер конца клипа действия рыбалки
#include "World/AsdaCombatTrace.h"         // ★ 18.10.2026: журнал боя — замах, кадры удара, стойка, смерть персонажа
#include "Npc/AsdaMobActor.h"              // ★ 18.10.2026: журнал боя — имя и rid цели разворота в замахе
#include "World/AsdaBlendAnimInstance.h"   // ★ 22.10.2026: плавность — смешивание клипов, доворот модели, хвост сетевых поправок
#include "Misc/ScopeExit.h"                // ★ 22.10.2026: доворот/хвост на любом выходе из Tick
#include "AsdaNetSubsystem.h"              // ★ 24.10.2026: 4024 цикла сидения (108 «сидит» / 0 «встал») своего персонажа
#include "World/AsdaCrouchStand.h"         // ★ 05.11.2026: присед стоя — поза вместо клипа покоя
#include "AsdaPlayerCharacterInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaPlayerCharacter*.cpp (LogAsdaPlayer, чистые хелперы)

DEFINE_LOG_CATEGORY(LogAsdaPlayer);   // ★ 23.10.2026 разнос: было DEFINE_LOG_CATEGORY_STATIC; объявление — AsdaPlayerCharacterInternal.h (один экземпляр на все части)

AAsdaPlayerCharacter::AAsdaPlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// ★ Поворот флага клана относительно кости Flag_Link транспорта.
	//   Эталон (mapobj_updateFlagLinkAnim@0xFB0CB0): D3DXMatrixRotationYawPitchRoll(π, 0, π/2), что в
	//   колоночной записи D3D = [[0,1,0],[1,0,0],[0,0,−1]] (проверка геометрией: древко флага лежит
	//   вдоль +Y модели и после этого поворота встаёт вдоль локального +X кости, а тот смотрит вверх).
	//   Перенос в оси скелетала UE: T = C·R·C⁻¹, где C: (x,y,z)_D3D→(x,z,y)_UE (ПРОВЕРЕНО замером
	//   ref-позы Flag_Link, Tools/asda_probe_flaglink.py) ⇒ T = [[0,0,1],[0,−1,0],[1,0,0]] =
	//   поворот на 180° вокруг оси (1,0,1)/√2.
	ClanFlagAttachRotation = FQuat(FVector(1.f, 0.f, 1.f).GetSafeNormal(), PI).Rotator();

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	// ★ Единый масштаб тела+экипа: MeshRoot=0.01, тело и части экипа под ним (части крепятся к MeshRoot).
	//   Раньше масштабировался только BaseSkel → экип (на Root) был в 100× крупнее тела (гигантские сапоги).
	MeshRoot = CreateDefaultSubobject<USceneComponent>(TEXT("MeshRoot"));
	MeshRoot->SetupAttachment(Root);
	// ★ ЗЕРКАЛО (reverse: игра ПРАВОРУКАЯ RH, конвертер сделал лишний флип — det +1 вместо −1).
	//   Рантайм-фикс: негейт Y в локальном пространстве меша. Иначе право/лево модели зеркальны
	//   (оружие в левой руке вместо правой и т.д.). Полный фикс — перегенерация .glb (вариант 1 отчёта).
	MeshRoot->SetRelativeScale3D(FVector(0.01f, -0.01f, 0.01f));   // glTF-скелеталки метрами → см + зеркало
	BaseSkel = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("BaseSkel"));
	BaseSkel->SetupAttachment(MeshRoot);            // масштаб 1 внутри MeshRoot(0.01) = итог 0.01
	// ★ 22.10.2026 (плавность): одиночный узел СО СМЕШИВАНИЕМ клипов (`World/AsdaBlendAnimInstance.h`); при
	//   `asda.Anim.BlendSec 0` / `BlendAttackSec 0` он ведёт себя как прежний AnimationSingleNode дословно.
	AsdaAnimSmooth::Install(BaseSkel);
	BaseSkel->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	BaseSkel->SetCastShadow(true);
	// ★ Сет B (05.09.2026): непрямая капсульная тень ТЕЛА (не частей экипа) — пятно под персонажем в непрямом
	//   слое; ключи asda.Shadow.Capsule / asda.Shadow.CapsuleMinVisibility, дамп asda.Shadow.CapsuleDump (см. AsdaCapsuleShadow.h).
	//   Капсулы — из ShadowPhysicsAsset базового меша (скрипт asda_set_shadow_physics_assets.py).
	AsdaShadow::ApplyCapsule(BaseSkel);

	// ★ Хитбокс пикинга (рост персонажа ~156 см): ловит только трейсы видимости.
	PickHitbox = CreateDefaultSubobject<UCapsuleComponent>(TEXT("PickHitbox"));
	PickHitbox->SetupAttachment(Root);
	PickHitbox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PickHitbox->SetCollisionObjectType(ECC_WorldDynamic);
	PickHitbox->SetCollisionResponseToAllChannels(ECR_Ignore);
	PickHitbox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	PickHitbox->SetGenerateOverlapEvents(false);
	PickHitbox->SetCapsuleSize(35.f, 80.f);
	PickHitbox->SetRelativeLocation(FVector(0.f, 0.f, 80.f));

	// ★ Плашка над головой (JUI TARGETMARK_MAN1 1:1) — на Root (немасштабированный), видна в редакторе.
	Nameplate = CreateDefaultSubobject<UAsdaNameplateComponent>(TEXT("Nameplate"));
	Nameplate->SetupAttachment(Root);
	Nameplate->Kind = EAsdaNameplateKind::Player;
	Nameplate->bShowHp = false;      // над игроками HP-полосы нет (эталон: 1664/1665 не инстанцируются)
	Nameplate->WorldHeight = 200.f;  // над макушкой (рост модели ~156 см)

	// Орбит-камера (эталон: зум 170..1270, цель — на уровне груди ~120 см). Стартовый угол -20° (не ждём тик).
	// ★ 25.10.2026 [ОРИГИНАЛ]: стрела — `UAsdaCameraArmComponent` (World/AsdaCameraArmComponent.h): при
	//   `asda.Camera.OriginalNoCollide 1` игроки/мобы/NPC/объекты карты её не укорачивают, глаз не ниже `.TER + 60`
	//   (cameraUpdate@0x959880); при 0 — прежняя штатная стрела со сферой по каналу Camera. Тип поля в заголовке прежний.
	CameraArm = CreateDefaultSubobject<UAsdaCameraArmComponent>(TEXT("CameraArm"));
	CameraArm->SetupAttachment(Root);
	CameraArm->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
	CameraArm->SetRelativeRotation(FRotator(-20.f, 0.f, 0.f));
	CameraArm->TargetArmLength = 700.f;
	CameraArm->bDoCollisionTest = true;             // прежний путь (ключ 0): сфера по каналу Camera; при ключе 1 не используется
	CameraArm->bUsePawnControlRotation = false;     // углами рулит контроллер напрямую
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraArm);
	// Фиксированная экспозиция (старый D3D9-клиент без eye-adaptation) — сцена не «затухает» авто-экспозицией.
	Camera->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
	Camera->PostProcessSettings.AutoExposureMinBrightness = 1.f;
	Camera->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
	Camera->PostProcessSettings.AutoExposureMaxBrightness = 1.f;
	Camera->PostProcessSettings.bOverride_AutoExposureBias = true;
	Camera->PostProcessSettings.AutoExposureBias = 1.f;

	// Меш не должен блокировать спавн павна и трейсы камеры/земли.
	BaseSkel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SpawnCollisionHandlingMethod = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
}

// ★ 26.10.2026 (ревизия wave_1025d 1.1) [ОРИГИНАЛ]: смена ТОЛЬКО щита с транспорта не ссаживает. Эталон: ветка 8
//   `tagCHARACTER_SetPartsModelByType@0x9E18D0` (entity_char.c:11025) → `CCharacter_SetParts_Armor@0x9EB3C0` меняет одну деталь
//   модели, транспорт не трогает (у порта щит с 25.10 собирается общей пересборкой `InitAppearance`, а она ссаживала). 0 — как 25.10.
static ASDA_DEV_CVAR(int32, CVarAsdaCharShieldKeepsMount,
	TEXT("asda.Char.ShieldKeepsMount"), 1,
	TEXT("[ОРИГИНАЛ] 1 — пересборка внешности, в которой сменился только щит (экип-слот 8), с транспорта не ссаживает (эталон ")
	TEXT("CCharacter_SetParts_Armor); 0 — как 25.10: любая пересборка ссаживает."),
	ECVF_Default);

void AsdaPartFxRefresh(AAsdaPlayerCharacter* Ch, const FAsdaWornParts& Parts);   // ★ 07.11.2026: эффекты частей .PEB (AsdaPartFx.cpp)

// Отличаются ли две сборки ТОЛЬКО щитом (пол тот же, прочие части те же).
static bool AsdaOnlyShieldDiffers(const FAsdaWornParts& A, const FAsdaWornParts& B)
{
	return A.Chest == B.Chest && A.Pants == B.Pants && A.Shoes == B.Shoes && A.Gloves == B.Gloves && A.Helmet == B.Helmet
		&& A.FaceIndex == B.FaceIndex && A.HairStyle == B.HairStyle && A.HairColor == B.HairColor && A.Weapon == B.Weapon
		&& A.bHelmetShown == B.bHelmetShown && A.Shield != B.Shield;
}

void AAsdaPlayerCharacter::InitAppearance(uint8 InSex, const FAsdaWornParts& Parts)
{
	// ★ СМЕНА ЭКИПИРОВКИ ссаживает с транспорта (эталон: смена оружия/брони = автовысадка, сервер затем
	//   сам вернёт в седло, если бонус ещё жив — придёт пакет посадки).
	// ★ 26.10.2026: кроме смены одного щита (`asda.Char.ShieldKeepsMount`, эталон SetParts_Armor).
	const bool bShieldOnly = bHasLastAppliedParts && LastAppliedSex == InSex && AsdaOnlyShieldDiffers(LastAppliedParts, Parts)
		&& CVarAsdaCharShieldKeepsMount.GetValueOnGameThread() != 0;
	// ★ 06.11.2026 [ОРИГИНАЛ] (`asda.Mount.ListHoldsModel`): комментарий выше неточен — сервер посадку НЕ присылает, транспорт
	//   возвращает сам эталон в конце `CCharacter_SetupModelFromItem` по записи (15, 2). Числится за игроком — модель остаётся.
	if (bMounted && !bShieldOnly && !IsMountHeldByServer()) { ClearMount(); }
	else if (bMounted) { UE_LOG(LogAsdaPlayer, Log, TEXT("Смена щита %d → %d в седле: транспорт сохранён (эталон SetParts_Armor)"), LastAppliedParts.Shield, Parts.Shield); }
	LastAppliedParts = Parts; LastAppliedSex = InSex; bHasLastAppliedParts = true;
	Sex = InSex;
	USkeletalMesh* Mesh = UAsdaModelAnimSubsystem::LoadMeshChecked(CharBase(Sex));   // Д2
	if (!Mesh) { UE_LOG(LogAsdaPlayer, Warning, TEXT("Базовый скелет не найден (%s)"), *CharBase(Sex)); return; }
	// ★★★ ПЕРЕСБОРКА НА ЛЕТУ (смена экипировки — 5001 у себя, 0xFB5/0xFB7 у чужих) НЕ ДОЛЖНА СБИВАТЬ
	//   АНИМАЦИЮ. `SetSkeletalMesh` пересоздаёт аним-инстанс и гасит играющий клип, поэтому базовый
	//   меш трогаем ТОЛЬКО когда он реально другой (первый вызов / смена пола). Части экипа
	//   деформируются LeaderPose'ом от этого же скелета, так что новый шлем/меч подхватят текущую позу.
	const bool bBaseChanged = (BaseSkel->GetSkeletalMeshAsset() != Mesh);
	if (bBaseChanged) { BaseSkel->SetSkeletalMesh(Mesh); }
	UAsdaItemTableSubsystem* Items = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	// Части экипа крепим к MeshRoot (масштаб 0.01) — единый размер с телом.
	const int32 N = UAsdaCharAssembly::BuildWornParts(this, MeshRoot, BaseSkel, Sex != 2, Parts, Items);
	FaceShownMode = -1;   // ★ 07.11.2026: сборка вернула обычную текстуру лица — режим поставить заново
	UE_LOG(LogAsdaPlayer, Log, TEXT("Внешность собрана: пол=%d частей=%d (chest=%d pants=%d shoes=%d gloves=%d helmet=%d hair=%d face=%d weapon=%d shield=%d)"),
		Sex, N, Parts.Chest, Parts.Pants, Parts.Shoes, Parts.Gloves, Parts.Helmet, Parts.HairStyle, Parts.FaceIndex, Parts.Weapon, Parts.Shield);
	// ★ НАБОР МОУШЕНОВ ПО ОРУЖИЮ (эталон CCharacter_SetupModelFromItem@0x9E97A0: вместе с мешом оружия
	//   перезагружает категории 0/2/3 клипов с ItemRecord[33]=ItemSubType). Ставим ДО SetAnimState,
	//   чтобы первый же idle пошёл из правильного набора.
	SetWeaponFromItem(Parts.Weapon, Items);
	// ★ Меш оружия только что создан «в руке» — если флаг «оружие убрано» УЖЕ взведён (пересборка
	//   внешности в городе / в седле), сразу пересаживаем его за спину, не дожидаясь смены флага.
	RefreshWeaponStow();
	// ★ 07.11.2026 [ОРИГИНАЛ] (`asda.Char.PartFx`): эффекты частей из .PEB (пламя брони семейства 305) — AsdaPartFx.cpp.
	AsdaPartFxRefresh(this, Parts);
	// ★★★ РЕЖИМ «ДУША»: новые части (смена экипировки 5001 / 4021 / 4023, причёска 5471/5472) обязаны
	//   стать полупрозрачными В ТОМ ЖЕ вызове, иначе «одна часть плотная» до следующего подхвата.
	//   Эталон раздаёт α ВСЕМ записям модели каждый кадр (`objArray_SetField7Propagate :859466`).
	if (bSoulMaterialsApplied) { ApplySoulTranslucency(); }
	// ★ В покой сбрасываемся ТОЛЬКО при первой сборке (или смене пола): там аним-инстанс новый и клипа
	//   нет вовсе. При смене вещи на бегу/в бою/во время эмоции состояние сохраняем — набор клипов уже
	//   перезапустил `SetWeaponFromItem` → `SetWeaponSubType` (он же пропускает атаку и эмоцию).
	if (bBaseChanged) { SetAnimState(EAsdaPlayerState::Idle); }
}

// ── ★ НАБОРЫ МОУШЕНОВ ПО ОРУЖИЮ (реверс CCharacter_LoadMotionAnims@0x9B84D0, entity_char.c:4597) ──
// ItemSubType надетого оружия → номер набора ch4_{m|f}_{SS}. Ровно свитч эталона (строки 4693-4735):
//   case 0,4 → _01 | case 1 → _01 | case 2 → _05 | case 3 → _02 | case 5 → _06 | case 6 → _09
//   case 8,10 → _03 | case 9 → _04 | case 11 → _08 | case 99/иное → _00
int32 AAsdaPlayerCharacter::MotionSetOf(int32 InSubType)
{
	switch (InSubType)
	{
	case 0: case 4: return 1;
	case 1:  return 1;
	case 2:  return 5;
	case 3:  return 2;
	case 5:  return 6;
	case 6:  return 9;
	case 8: case 10: return 3;
	case 9:  return 4;
	case 11: return 8;
	default: return 0;   // 99 / нет оружия / неизвестный подтип — безоружный набор _00
	}
}

// Индекс UE-ассета ch1_{g}_cremmy_previewanim_NN для слота набора.
//   Набор _00 импортирован ранее (anim_12..19), наборы оружия — anim_40..85 (Tools/export_char_weaponsets.py):
//     _01→40..45  _02→46..51  _03→52..57  _04→58..63  _05→64..69  _06→70..75  _08→76..81
//     (порядок шестёрки: idle01, idle02, idle03, walk01, run01, back01)
//     _08 доп.: idle04→82, idle05→83, idle06→84 ; _09: idle01→85.
int32 AAsdaPlayerCharacter::MotionClipIdx(EAsdaMotionSlot Slot) const
{
	// набор _00: idle01=12 idle02=13 idle03=14 (sitdown01=15) walk01=16 run01=17 (death01=18) back01=19
	static const int32 Set00[6] = { 12, 13, 14, 16, 17, 19 };
	const int32 Set = CurMotionSet();
	const int32 S = (int32)Slot;

	// idle04/05/06 существуют ТОЛЬКО у набора _08 (баллиста) — у остальных их в архиве нет.
	if (Set == 8)
	{
		if (Slot == EAsdaMotionSlot::Idle04) { return 82; }
		if (Slot == EAsdaMotionSlot::Idle05) { return 83; }
		if (Slot == EAsdaMotionSlot::Idle06) { return 84; }
		// ★ ЭТАЛОН: при animClass 11 в СЛОТ 0 грузится idle05 (а не idle01) — ветка `animState == 11`
		//   в LoadMotionAnims (entity_char.c:4751). Поэтому боевая стойка баллисты = idle05.
		if (Slot == EAsdaMotionSlot::Idle01) { return 83; }
	}
	if (S > (int32)EAsdaMotionSlot::Back01) { return Set00[0]; }   // idle04/05/06 у не-_08 наборов нет

	switch (Set)
	{
	case 0: return Set00[S];
	case 1: return 40 + S;
	case 2: return 46 + S;
	case 3: return 52 + S;
	case 4: return 58 + S;
	case 5: return 64 + S;
	case 6: return 70 + S;
	case 8: return 76 + S;
	case 9:
		// ★ В CharMotion.baz у набора _09 есть ТОЛЬКО idle01 (проверено по архиву). Эталон на первом же
		//   отсутствующем клипе обрывает загрузку и ОСТАВЛЯЕТ СЛОТЫ ПУСТЫМИ (персонаж без анимации);
		//   в порту недостающее берём из _00, чтобы не получить застывший меш. Предметов с ItemSubType==6
		//   в item_list.bin НЕТ (проверено: тип 9 даёт подтипы 0,1,2,3,5,9,10,11) — ветка недостижима.
		return (Slot == EAsdaMotionSlot::Idle01) ? 85 : Set00[S];
	default: return Set00[S];
	}
}

// Клип покоя (0) / движения (1) для подмены на сидячий в седле; -1 — не подменять.
int32 AAsdaPlayerCharacter::MountSubstKind(int32 AssetIdx)
{
	if (AssetIdx == 35) { return 1; }                       // бег с крыльями
	if (AssetIdx >= 12 && AssetIdx <= 14) { return 0; }     // набор _00: idle01..idle03
	if (AssetIdx == 16 || AssetIdx == 17 || AssetIdx == 19) { return 1; }   // _00: walk/run/back
	if (AssetIdx >= 82 && AssetIdx <= 85) { return 0; }     // _08 idle04/05/06, _09 idle01
	if (AssetIdx >= 40 && AssetIdx <= 81)
	{
		const int32 R = (AssetIdx - 40) % 6;                // 0,1,2 = idle; 3,4,5 = walk/run/back
		return (R <= 2) ? 0 : 1;
	}
	return -1;
}

void AAsdaPlayerCharacter::SetWeaponSubType(int32 InSubType)
{
	if (EquippedWeaponSubType == InSubType) { return; }
	const int32 Old = EquippedWeaponSubType;
	EquippedWeaponSubType = InSubType;
	UE_LOG(LogAsdaPlayer, Log, TEXT("[аним] оружие subType=%d (было %d) набор=_%02d состояние=%d → клип=anim_%02d"),
		EquippedWeaponSubType, Old, CurMotionSet(), (int32)AnimState,
		AnimState == EAsdaPlayerState::Move ? MoveClip() : IdleClipIdx());
	// Перезапуск клипа текущего состояния новым набором (эталон: SetupModelFromItem перезагружает
	//   категории 0/2/3, следующий тик проигрывает клип из ТЕХ ЖЕ слотов, но нового набора).
	if (IsAttackAnimActive()) { return; }
	// ★★★ Клип эмоции не перебиваем (владелец 2026-08-25: «длинные эмоции не доигрывают»).
	//   Сторож поймал подмену на 7.82 с из 13.33 — клип танца заменялся клипом текущего состояния.
	//   Законно прервать эмоцию может только смена состояния (движение/смерть) — она идёт через
	//   `SetAnimState`, который зовёт `StopEmotionClip`. Все ОСТАЛЬНЫЕ перезапуски клипа состояния
	//   (смена оружия, конец боевого цикла, крылья) обязаны эмоцию пропускать.
	// ★ Волна В5: клип действия рыбалки 60…67 так же не перебиваем (эталон: состояние 60…67 ≠ 0/1); ★ 10.10.2026: и копания 40/43.
	// ★ В3 S7: три признака (эмоция / рыбалка / лопата) — одним предикатом единого состояния.
	if (IsActionClipOwned()) { return; }
	if      (AnimState == EAsdaPlayerState::Move) { PlayWorldClip(MoveClip(), true); }
	else if (AnimState == EAsdaPlayerState::Idle) { PlayWorldClip(IdleClipIdx(), true); FidgetRemaining = 0.f; IdleTimer = 0.f; }
}

void AAsdaPlayerCharacter::SetWeaponFromItem(int32 ItemId, UAsdaItemTableSubsystem* Items)
{
	int32 Sub = 99;   // нет оружия → набор _00
	// ★ 05.11.2026 [ИСПРАВЛЕНИЕ] (`asda.Bow.NockLifecycle`): смена/снятие оружия снимает стрелу на тетиве — эталон
	//   `charEffectSlots_ClearSix` в ветке оружия `CCharacter_LoadEquipPart` / `CCharacter_SetupModelFromItem` (entity_char.c:14357,
	//   14680, 15064, 15636). Общая точка своего (5001 → `ApplySelfEquipPart`, 4021/4048 → `RefreshSelfWeaponMotion`) и соседа (4021/4023):
	//   своя смена сервером не рассылается, поэтому снимать обязан локальный путь. Пересборка внешности без смены оружия
	//   (причёска, шлем, броня) стрелу не трогает — у эталона ветка оружия в этих случаях не вызывается.
	const int32 NewWeaponItemId = (ItemId > 0) ? ItemId : 0;   // предмет оружия после вызова (0 — без оружия)
	if (NewWeaponItemId != EquippedWeaponItemId && AsdaNockedArrow::LifecycleOn()) { AsdaNockedArrow::Hide(this, TEXT("смена оружия")); }
	// ★ Волна В5 (рыбалка): код надетого оружия нужен удочке рыбалки (меш по img-записи `char+0x835` в момент вставки;
	//   перезагрузки по смене оружия у эталона нет — В5fix МЕЛК-1, разбор в `TickFishAttachments`).
	EquippedWeaponItemId = NewWeaponItemId;
	if (ItemId > 0 && Items)
	{
		if (const FAsdaItemRecord* R = Items->GetItem(ItemId))
		{
			Sub = R->ItemSubType;
			// ★ Спец-правила эталона (LoadMotionAnims, строки 4650-4668): удочки (ItemSubType 10,
			//   ItemVariant 1) идут набором _05 (класс 2), буры (Variant 2) — набором _03 (класс 10).
			if (R->ItemSubType == 10 && R->ItemVariant == 1) { Sub = 2; }
			else if (R->ItemSubType == 10 && R->ItemVariant == 2) { Sub = 10; }
		}
	}
	SetWeaponSubType(Sub);
}

// ★ Переключение «оружие убрано» (безопасная зона / седло) — эталон делает это ТИКОМ,
//   charUpdateActionStateTick@0xA216B0: при смене состояния флага заново зовётся SetupModelFromItem,
//   т.е. набор моушенов меняется ПРЯМО НА ХОДУ при входе в город и выходе из него.
// Ключ уборки оружия сидя: 1 = как эталон (состояния 7/8/9 убирают оружие и щит за спину), 0 = прежний порт (в руке).
static ASDA_DEV_CVAR(int32, CVarAsdaSitStowWeapon,
	TEXT("asda.Char.SitStowWeapon"), 1,
	TEXT("1 (по умолчанию) = сидя (состояния 7/8/9) оружие и щит за спиной, как эталон charActionStateIs7to9; 0 = прежний порт: остаются в руке."),
	ECVF_Default);
// Ключ уборки оружия в приседе: 1 = присев, оружие и щит за спиной (кроме замаха и навыка), 0 = как донор: в руке, поза клипа hidewalk_01.
static ASDA_DEV_CVAR(int32, CVarAsdaCrouchStowWeapon,
	TEXT("asda.Crouch.StowWeapon"), 1,
	TEXT("1 (по умолчанию) = в приседе оружие и щит за спиной, на время замаха и навыка в руке; 0 = как донор: в руке, клинок назад и вниз по позе клипа."),
	ECVF_Default);

void AAsdaPlayerCharacter::UpdateWeaponMotionHidden()
{
	// ★★★ (в) НА ВРЕМЯ ЭМОЦИИ оружие тоже уходит за спину (требование владельца 2026-08-25):
	//   эмоция играется набором клипов «без оружия», и меч в руке в неё не вписывается.
	//   Механизм тот же, что у мирной зоны и седла — один флаг переключает И набор клипов,
	//   И кость меша, И кость свечения.
	// ★ Волна В5: рыбалка 60…67 — то же `bActionBusy = 1` (`CChar_SetState` game.c:77136), условие эталона
	//   `self[52164] == 1` (`charUpdateActionStateTick@0xA216B0`, game.c:81364; поле +0x32F10 = bActionBusy).
	//   ★ В5fix (ревизия В5, МЕЛК-7): писателей поля больше, чем «CChar_SetState / SetAction / конец клипа» — скан `.text`
	//   по +0x32F10: ещё `Net_RecvPacketDispatch_Loop` 0x96ED77, `CCharacter_ctor` 0xA1BD69, `CCampWar_OnObjectEffectPacket`
	//   0xCA5B1F/0xCA5B6D, `Item_UseShovelingTool` 0xD0C189/0xD0C3F7, `Player_TickSendStatePosOnAnimEnd` 0x9F15FC,
	//   `charCopyFromNetStruct` 0xEFE8FF. Для рыбалки значимы `CChar_SetState` (60…67 → 1) и конец клипа.
	// ★ В45fix2 (МЕЛК-5): состояние 68 «добыча» — тоже `bActionBusy = 1` (`CChar_SetState` g:77139-77144).
	// ★ 09.10: + лопата 40/43. ★ В3 S7: четыре признака «занят» — одно состояние (`IsEtalonActionBusy`).
	bool bWantHidden = bMounted || IsEtalonActionBusy();   // (б) в седле
	// ★ 07.11.2026 [ОРИГИНАЛ] (`asda.Char.SitStowWeapon`): сидение 7/8/9 — третье условие эталонного тика (`sub_9231EA` = `charActionStateIs7to9`,
	//   entity_char.c:charUpdateActionStateTick); порт его не повторял: меч проходил сквозь колено, щит лежал на земле.
	if (!bWantHidden && CVarAsdaSitStowWeapon.GetValueOnGameThread() != 0 && GetEtalonSitState() != 0) { bWantHidden = true; }
	// ★ 07.11.2026 [ИСПРАВЛЕНИЕ] (`asda.Crouch.StowWeapon`): поза приседа — донорский клип hidewalk_01, правая кисть уведена за спину, клинок смотрит
	//   назад и вниз, щит заслоняет лицо. У донора в тике уборки приседа нет (Asda2.exe 0x4AE120: те же четыре условия), у эталона приседа нет вовсе,
	//   клипа с оружием нет ни там, ни там. Присел — оружие и щит за спиной тем же флагом; на время замаха и навыка оружие в руке.
	if (!bWantHidden && CVarAsdaCrouchStowWeapon.GetValueOnGameThread() != 0 && IsCrouchWalkShown()
		&& !IsAttackAnimActive() && !IsSkillAnimActive() && !IsSkillFaceActive()) { bWantHidden = true; }
	if (!bWantHidden && bPeaceZoneUnarmedMotion)
	{
		// Сетка карты лежит на ЛОКАЛЬНОМ павне (её ставит UAsdaWorldSubsystem при входе в мир);
		//   соседям берём ту же — карта одна на всех.
		const UAsdaWalkGrid* Grid = WalkGrid;
		if (!Grid)
		{
			if (const UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
			{
				if (const AAsdaPlayerCharacter* Local = Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor)) { Grid = Local->WalkGrid; }
			}
		}
		if (Grid)
		{
			const FVector2D G = GetGamePosXZ();
			bWantHidden = Grid->IsWorldPeaceZone(G.X, G.Y);   // (а) бит 0x80 атрибута тайла .TER
		}
	}
	if (bWantHidden == bWeaponMotionHidden) { return; }
	bWeaponMotionHidden = bWantHidden;
	// ★ 05.11.2026 [ИСПРАВЛЕНИЕ] (`asda.Bow.NockLifecycle`): уборка/возврат оружия (город, седло, эмоция, действие) снимает стрелу на тетиве —
	//   эталон на смене флага заново зовёт `CCharacter_SetupModelFromItem`, а тот `charEffectSlots_ClearSix` (entity_char.c:15064).
	if (AsdaNockedArrow::LifecycleOn()) { AsdaNockedArrow::Hide(this, bWantHidden ? TEXT("оружие убрано") : TEXT("оружие в руке заново")); }
	// ★ Меш оружия и свечение заточки ПЕРЕЕЗЖАЮТ на кость спины (эталон SetupModelFromItem case 19 +
	//   UpdateHeldItemEffect — обе ветки читают тот же флаг gap_A42[15160]).
	RefreshWeaponStow();
	UE_LOG(LogAsdaPlayer, Log, TEXT("[стойка] зона=%s бой=%s оружие=subType %d набор=_%02d меш на кости=%s"),
		bWantHidden ? TEXT("город/седло") : TEXT("поле"), bCombatStance ? TEXT("да") : TEXT("нет"),
		EquippedWeaponSubType, CurMotionSet(), *CurrentWeaponBone().ToString());
	if (IsAttackAnimActive()) { return; }
	// ★★★ И КЛИП ЭМОЦИИ ТОЖЕ НЕ ПЕРЕБИВАЕМ (владелец 2026-08-25: «эмоции не доигрывают до конца»).
	//   Сюда приходят и смена зоны, и уборка оружия при самом старте эмоции — без этого гейта
	//   эмоция сбрасывалась в покой на первом же кадре, тем самым вызовом, который убирает оружие.
	// ★ В5: и клип действия рыбалки, ★ 10.10.2026: и копания 40/43 (уборка оружия на их старте). ★ В3 S7: один предикат.
	if (IsActionClipOwned()) { return; }
	// ★ 07.11.2026 (`asda.Char.SitStowWeapon`): клип сидения 7/8/9 — свой, уборка оружия его не перебивает. Состояние 9 живёт поверх `AnimState == Idle`,
	//   поэтому без этой проверки флаг на входе в «встаёт» ставил клип покоя вместо хвоста sitdown01.
	if (CVarAsdaSitStowWeapon.GetValueOnGameThread() != 0 && SitPhase != 0) { return; }
	if      (AnimState == EAsdaPlayerState::Move) { PlayWorldClip(MoveClip(), true); }
	else if (AnimState == EAsdaPlayerState::Idle) { PlayWorldClip(IdleClipIdx(), true); FidgetRemaining = 0.f; IdleTimer = 0.f; }
}

// ★ ОРУЖИЕ ЗА СПИНУ (эталон CCharacter_SetupModelFromItem@0x9E97A0 case 19, entity_char.c:14710-14776
//   и CCharacter_UpdateHeldItemEffect@0xA4C070, entity_char.c:28471-28564). Один и тот же флаг
//   char+226184 (по сети — gap_A42[15160]) переключает И набор клипов, И кость меша, И кость свечения:
//     в руке  — оружие "8_a1" (луки "_8_a1"), щит "_7_a1";
//     убрано  — оружие "2_a1" (ВСЕ типы, включая луки), щит "_2_a1" + эталонные поправки локальной матрицы.
//   Работает и для СЕБЯ, и для СОСЕДЕЙ: класс общий, вызывается из тика UpdateWeaponMotionHidden.
void AAsdaPlayerCharacter::RefreshWeaponStow()
{
	// 1. МЕШ оружия (тег ставит UAsdaCharAssembly::AttachWeapon).
	TArray<UStaticMeshComponent*> SMs;
	GetComponents<UStaticMeshComponent>(SMs);
	for (UStaticMeshComponent* C : SMs)
	{
		if (C && C->ComponentHasTag(UAsdaCharAssembly::WeaponMeshTag))
		{
			UAsdaCharAssembly::ApplyWeaponStow(BaseSkel, C, EquippedWeaponSubType, bWeaponMotionHidden);
			break;
		}
	}
	// 1а. ★ 25.10.2026 [ОРИГИНАЛ] МЕШ ЩИТА — тот же флаг: эталон `charUpdateActionStateTick@0xA216B0` при его смене зовёт
	//     подряд `SetupModelFromItem` и `CCharacter_SetParts_Armor` (кость "_2_a1" убран / "_7_a1" в руке, без поправок).
	for (UStaticMeshComponent* C : SMs)
	{
		if (C && C->ComponentHasTag(UAsdaCharAssembly::ShieldMeshTag))
		{
			UAsdaCharAssembly::ApplyShieldStow(BaseSkel, C, bWeaponMotionHidden);
			break;
		}
	}
	// 2. СВЕЧЕНИЕ ЗАТОЧКИ — те же кости + эталонная attachMatrix (у эффектов она вставляется между
	//    fx-пространством и костью, см. AAsdaEffectActor::FollowBoneAttach).
	TArray<AActor*> Attached;
	GetAttachedActors(Attached);
	for (AActor* A : Attached)
	{
		if (!A) { continue; }
		const bool bWeaponFx = A->ActorHasTag(FName(AsdaCharFxTags::kWeaponGlow));   // ★ В1-C F6: общий тег
		const bool bShieldFx = A->ActorHasTag(FName(AsdaCharFxTags::kShieldGlow));
		if (!bWeaponFx && !bShieldFx) { continue; }
		AAsdaEffectActor* E = Cast<AAsdaEffectActor>(A);
		if (!E) { continue; }
		const FName NewBone = UAsdaCharAssembly::WeaponGlowBone(EquippedWeaponSubType, bWeaponMotionHidden, bShieldFx);
		if (BaseSkel && BaseSkel->GetBoneIndex(NewBone) == INDEX_NONE) { continue; }   // кости нет — не трогаем
		E->FollowBone = NewBone;
		E->FollowBoneAttach = UAsdaCharAssembly::WeaponGlowAttachRaw(EquippedWeaponSubType, bWeaponMotionHidden, bShieldFx);
	}
}

FName AAsdaPlayerCharacter::CurrentWeaponBone() const
{
	TArray<UStaticMeshComponent*> SMs;
	const_cast<AAsdaPlayerCharacter*>(this)->GetComponents<UStaticMeshComponent>(SMs);
	for (const UStaticMeshComponent* C : SMs)
	{
		if (C && C->ComponentHasTag(UAsdaCharAssembly::WeaponMeshTag)) { return C->GetAttachSocketName(); }
	}
	return NAME_None;   // оружия нет
}

float AAsdaPlayerCharacter::PlayWorldClip(int32 AssetIdx, bool bLoop)
{
	// ★★★ 05.11.2026 [ПОРТ] ПРИСЕД СТОЯ (баг «присед только в движении»; разбор — `World/AsdaCrouchStand.h`): клип покоя (idle01/02/03
	//   набора) под приседом на экране — поза приседа: кадр `AsdaCrouchStand::PoseSec()` клипа anim_239, удерживаемый (темп 0;
	//   тик хода держит 0). Смена клипа — со смешиванием, как любая (вход в позу и выход из неё плавные). Ключ `asda.Crouch.StandPose 0` —
	//   прежний покой.
	if (AsdaCrouchStand::Shown(this) && BaseSkel
		&& (AssetIdx == IdleMainClipIdx() || AssetIdx == IdleCombatClipIdx() || AssetIdx == IdleFidgetClipIdx()))
	{
		const FString CrouchPath = FString::Printf(TEXT("%sanim_%02d"), *CharBase(Sex), kClipCrouchWalk);
		if (UAnimSequence* Crouch = UAsdaModelAnimSubsystem::LoadClipChecked(CrouchPath))
		{
			AsdaAnimSmooth::PlayClip(BaseSkel, Crouch, true, AsdaAnimSmooth::BlendSec());
			AttackClipTempo = 1.f;
			if (UAnimSingleNodeInstance* SNI = BaseSkel->GetSingleNodeInstance())
			{
				SNI->SetPosition(FMath::Min(AsdaCrouchStand::PoseSec(), Crouch->GetPlayLength()), /*bFireNotifies*/ false);
			}
			BaseSkel->SetPlayRate(0.f);
			return Crouch->GetPlayLength();
		}
		UE_LOG(LogAsdaPlayer, Warning, TEXT("[присед] клип позы стоя не найден: %s — обычный покой"), *CrouchPath);
	}
	// ★ В СЕДЛЕ клипы персонажа подменяются на СИДЯЧИЕ (эталон SetParts_RidingSystem: слоты 0/1/5 →
	//   Ch1_n_sit_<набор>_{000,001,002}); посадка запечена в анимациях. Замах атаки не подменяем —
	//   при атаке эталон вообще ссаживает с транспорта (автовысадка по state 5).
	const int32 SubstKind = MountSubstKind(AssetIdx);
	if (bMounted && !MountAnimSet.IsEmpty() && SubstKind >= 0)
	{
		const bool bSitMoving = (SubstKind == 1);
		const FString SitPath = MountClipPath(bSitMoving ? TEXT("002") : TEXT("000"));
		if (UAnimSequence* SitClip = UAsdaModelAnimSubsystem::LoadClipChecked(SitPath))   // Д2
		{
			AsdaAnimSmooth::PlayClip(BaseSkel, SitClip, bLoop);   // ★ 22.10.2026: со смешиванием (ключ asda.Anim.BlendSec)
			BaseSkel->SetPlayRate(1.f);   // ★ сидячие клипы темпом атаки не ускоряются (в седле не бьют)
			// ★ ФИКС «качания в противофазе» (живой тест): клип ТРАНСПОРТА перезапускаем ВМЕСТЕ с сидячим.
			//   Эталон (§2.2 таблица состояний): смена состояния переключает слот транспорта [1316]+0/_000 ↔
			//   +128/_002 в ТОМ ЖЕ месте, где клип персонажа — оба начинаются с кадра 0 синхронно. Раньше
			//   anim_00 стартовал один раз при посадке и жил вечно: каждый перезапуск сидячего клипа
			//   (стоп/старт движения, фиджет каждые 6–12 с) сдвигал фазу седока → случайная противофаза.
			//   Данные чисты: idle-пары .jbb и .glb в фазе (corr +0.999, Tools/_probe_mount_phase*.py).
			PlayMountClip(bSitMoving);
			return SitClip->GetPlayLength();
		}
		UE_LOG(LogAsdaPlayer, Warning, TEXT("[транспорт] сидячий клип не найден: %s"), *SitPath);
	}
	const FString Path = FString::Printf(TEXT("%sanim_%02d"), *CharBase(Sex), AssetIdx);
	if (UAnimSequence* Clip = UAsdaModelAnimSubsystem::LoadClipChecked(Path))   // Д2
	{
		// ★ 22.10.2026 (плавность): смена клипа со смешиванием позы; клип замаха — короткий бленд эталона
		//   (`asda.Anim.BlendAttackSec`, [ОРИГИНАЛ] 60 мс `charAnim_UpdateActionBlend@0xAB3060`), прочие — `asda.Anim.BlendSec`.
		//   Время клипа не трогается: кадры удара/шага/звука идут от позиции клипа, как прежде.
		AsdaAnimSmooth::PlayClip(BaseSkel, Clip, bLoop,
			IsAttackClipAsset(AssetIdx) ? AsdaAnimSmooth::BlendAttackSec() : AsdaAnimSmooth::BlendSec());
		// ★★★ ВТОРАЯ ТОЧКА ПРИМЕНЕНИЯ ТЕМПА АТАКИ (первая — тайминги в `GetComboTiming`).
		//   В эталоне множитель растягивает ШАГ ВРЕМЕНИ часов клипа замаха, то есть картинка и
		//   тайминги едут от одних и тех же часов. У нас тайминги считаются реальным временем и уже
		//   поделены на темп — значит клип обязан играть во столько же раз быстрее, иначе удар
		//   «случится» раньше, чем персонаж донесёт оружие. Скорость получают ТОЛЬКО клипы атаки;
		//   все остальные (покой, движение, фиджет, эмоция, «сесть/встать») возвращаются к 1.0 —
		//   скорость живёт на компоненте и без явного сброса протекла бы в следующий клип.
		//   ★ Множитель ЗАПОМИНАЕТСЯ на весь проход: позиция клипа копится именно по этой скорости,
		//     и приводить её обратно в реальное время нужно тем же числом. Опрос «живого» множителя
		//     при каждом чтении давал скачок оценки в момент наложения или спада баффа посреди замаха.
		AttackClipTempo = IsAttackClipAsset(AssetIdx) ? FMath::Max(0.1f, AttackTempoMul()) : 1.f;
		// ★ 24.10.2026 ходьба вприсядку: темп по формуле донора (`0x4A8130`: цикл приседа — на ту же дистанцию, что цикл бега).
		BaseSkel->SetPlayRate(AssetIdx == kClipCrouchWalk ? CrouchClipRate() : AttackClipTempo);
		return Clip->GetPlayLength();
	}
	// ★ Клипа нет — на компоненте продолжает играть ПРЕДЫДУЩИЙ, и если это был замах, он остался бы
	//   крутиться на скорости темпа атаки уже в роли покоя/бега. Скорость живёт на компоненте, а не
	//   на клипе, поэтому сбрасываем её и на этом пути.
	BaseSkel->SetPlayRate(1.f);
	AttackClipTempo = 1.f;
	UE_LOG(LogAsdaPlayer, Warning, TEXT("Клип не найден: %s"), *Path);
	return 0.f;
}

// ★ Клип атаки? Множество строится ИЗ `AttackClipIdx` (единый список индексов) — своей копии
//   «20..28» здесь нет намеренно: разойдись она с таблицей, часть замахов молча играла бы в темпе 1.
bool AAsdaPlayerCharacter::IsAttackClipAsset(int32 AssetIdx)
{
	// Подтипы оружия, встречающиеся в таблице наборов: 0..6 и 8..11 (7 отсутствует, default → 20).
	static const int32 kWeaponSubTypes[] = { 0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11 };
	for (int32 W : kWeaponSubTypes)
	{
		if (AttackClipIdx(W) == AssetIdx) { return true; }
	}
	return false;
}

int32 AAsdaPlayerCharacter::AttackClipIdx(int32 W)
{
	switch (W)
	{
	case 0: case 4: return 20;   // мечи (ch4_*_01_normalattack01)
	case 1:  return 21;          // двуруч (01_normalattack03)
	case 3:  return 22;          // лук (02)
	case 8: case 10: return 23;  // (03)
	case 9:  return 24;          // (04)
	case 2:  return 25;          // копьё (05)
	case 5:  return 26;          // посох (06)
	case 11: return 27;          // (08_normalattack02)
	case 6:  return 28;          // (09)
	default: return 20;          // кулаки/неизвестное — меч-набор (в set 00 normalattack нет)
	}
}

// ══════════ ★★★ «СЕСТЬ / СИДИТ / ВСТАТЬ»: три сегмента ОДНОГО клипа ═════════════════════════
// Полный разбор и адреса — в объявлении (`AsdaPlayerCharacter.h`, блок kSitDownEndMs*) и в
//   `Reports/char_info_fix2.md` §3. Коротко: эталон не проигрывает клип задом наперёд и не
//   держит отдельного клипа вставания — он перематывает ОДИН `sitdown01` по времени.
float AAsdaPlayerCharacter::SitDownEndSec() const
{
	return ((Sex == 1) ? kSitDownEndMsMale : kSitDownEndMsFemale) * 0.001f;
}

float AAsdaPlayerCharacter::SitLoopEndSec() const
{
	return ((Sex == 1) ? kSitLoopEndMsMale : kSitLoopEndMsFemale) * 0.001f;
}

float AAsdaPlayerCharacter::SitClipLenSec() const
{
	const FString Path = FString::Printf(TEXT("%sanim_%02d"), *CharBase(Sex), kSitClipAsset);
	if (UAnimSequence* Clip = UAsdaModelAnimSubsystem::LoadClipChecked(Path))   // Д2: ресурс через менеджер
	{
		return Clip->GetPlayLength();
	}
	return 0.f;
}

// Перемотка клипа `sitdown01` на заданное ВРЕМЯ (мс) — прямой порт `CObject_SetField13(obj+192, t)`.
void AAsdaPlayerCharacter::SetSitClipPositionMs(float Ms)
{
	if (!BaseSkel) { return; }
	if (UAnimSingleNodeInstance* SNI = BaseSkel->GetSingleNodeInstance())
	{
		SNI->SetPosition(FMath::Max(0.f, Ms * 0.001f), /*bFireNotifies*/ false);
		SNI->SetPlaying(true);
	}
}

// ★★★ 24.10.2026 (задача владельца «108 сижу»): 4024 «СИДИТ» (108) и «ВСТАЛ» (0) СВОЕГО персонажа. [ОРИГИНАЛ]
//   Посадка: `charUpdateSabzilAnimAndSendPos@0x9F1310` (entity_char.c:17458) — тик СВОЕГО персонажа в состоянии 7
//   (`render_core.c:3572`, case 7). Трек 3 (`sitdown01`, model+0xC0) продвигается на кадр; когда его время (поле 13 трека, мс;
//   `Object_GetFloat13@0x99EDA0`) СТРОГО больше 1500 (пол `+0x437 == 1`, константа 0x13998BC) или 1999.9999 (иначе, 0x13AB6A4) —
//   `CharFx_AutoSabzil_State(8)` и `Send_CharStatePos(свой userNum, курс взгляда, 108)`. Сверено дизасмом 0x9F141B…0x9F14DE
//   (`fcomp` + `test ah, 41h` → отправка только при «больше»). Дальше состояние 8 — функция не зовётся: 108 ровно один раз.
//   Прерванная посадка (7 → иное состояние до границы) 108 не шлёт. Те же 1500/2000 мс — `kSitDownEndMs*` порта.
//   Вставание: `Player_TickSendStatePosOnAnimEnd@0x9F1560` (entity_char.c:17534; case 9) — по концу трека 3 (`Game_IsPausedOrMenu` —
//   МИСНЕЙМ: время ≥ длины) `CharFx_AutoSabzil_State(0)` и 4024 код 0. Это конец ТОГО ЖЕ цикла — переносим. Код 400
//   (`CCharacter_SetAction@0x9EE720` case 0, entity_char.c:16547-16556) шлётся при выходе из состояния 5 (атака) — к сидению не относится.
//   Сервер (`sub_546730`, GameServer.c:124794): код 100…400 пишется в `CUser+gap_4954[2928]`, `TimeSyncProcess_1` при 108 ускоряет
//   восстановление (0x54371E/0x543882: делитель /12 вместо /40). Код 0 сток отбрасывает до записи (P81, проба T2) — сервер не меняется.
//   Порт: граница 7 → 8 — переход `ResumeSitIdle` из фазы 7 (таймер `SitTimer`); конец 9 — `OnSitPhaseTimer`. Только свой персонаж
//   (`IsLocalPlayerCharacter`): у соседа фазы ведёт 4025, отправителя нет. Лавка (`SendStallStatePose` 107/109) идёт той же фазой.
static ASDA_DEV_CVAR(int32, CVarAsdaCharSitStateSync,
	TEXT("asda.Char.SitStateSync"), 1,
	TEXT("1 (по умолчанию) [ОРИГИНАЛ] = свой персонаж по концу «садится» (1,5 с муж / 2 с жен) шлёт 4024 код 108 «сидит» ")
	TEXT("(сервер ускоряет восстановление), по концу «встаёт» — 4024 код 0; 0 = прежний порт: 108 и 0 не шлются."),
	ECVF_Default);

// Отправка 4024 цикла сидения (108 «сидит» / 0 «встал») — только свой персонаж и только при ключе; тело — курс взгляда.
static void AsdaSendSitCycle4024(AAsdaPlayerCharacter* Ch, int32 Code, const TCHAR* Why)
{
	if (!Ch || CVarAsdaCharSitStateSync.GetValueOnGameThread() == 0 || !Ch->IsLocalPlayerCharacter()) { return; }
	UGameInstance* GI = Ch->GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	const FVector2D F = Ch->GetFacingDirGame();   // `thunk_copyVec3Field5(self + 47718)` — курс взгляда, как у 107/109
	const bool bOk = Net->SendCharStatePos4024(Code, (float)F.X, (float)F.Y);
	UE_LOG(LogAsdaPlayer, Log, TEXT("[сидение] %s — 4024 код %d (%s)"), Why, Code, bOk ? TEXT("ушёл") : TEXT("не ушёл: нет связи"));
}

// Состояние 7: клип с нуля, по границе `SitDownEndSec` уходим в состояние 8.
void AAsdaPlayerCharacter::BeginSitDown()
{
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(SitTimer); }
	const float Len = PlayWorldClip(kSitClipAsset, /*bLoop*/ false);
	if (Len <= 0.f)
	{
		// [ПОРТ] Клипа нет — фазы нет, и `GetEtalonSitState` сразу отвечает 8 («сидит» без фазы). Сервер должен знать то же,
		//   иначе сидячего восстановления не будет: 108 уходит сразу (у эталона клип есть всегда — ветки нет).
		SitPhase = 0;
		// ★ 25.10.2026 (ревизия sit108, М-1): НЕ сразу — вызывающий (`ApplySitPose4024`, лавка) шлёт 107 ПОСЛЕ `SetAnimState(Sit)`, и 108
		//   ушёл бы раньше 107 (сервер остался бы на 107). Следующий кадр: 107 → 108, как у эталона; сидение за кадр могли снять.
		if (GetWorld())
		{
			GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (AnimState == EAsdaPlayerState::Sit && SitPhase == 0) { AsdaSendSitCycle4024(this, 108, TEXT("сидит (клипа посадки нет — кадром позже 107)")); }
			}));
		}
		return;
	}
	SetSitClipPositionMs(0.f);
	SitPhase = 7;
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(SitTimer, this, &AAsdaPlayerCharacter::OnSitPhaseTimer,
			FMath::Max(0.01f, SitDownEndSec()), false);
	}
	UE_LOG(LogAsdaPlayer, Log, TEXT("[сесть] состояние 7: клип anim_%02d 0…%.2fс"), kSitClipAsset, SitDownEndSec());
}

// Состояние 9: хвост клипа от `SitLoopEndSec` до конца; по концу прохода — покой (эталон
//   `charSetActionTrackAndState` → `CChar_SetState(…, 0)`). Возвращает длительность хвоста, сек.
float AAsdaPlayerCharacter::BeginStandUp()
{
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(SitTimer); }
	const float Len = PlayWorldClip(kSitClipAsset, /*bLoop*/ false);
	const float Start = SitLoopEndSec();
	const float Tail = Len - Start;
	if (Len <= 0.f || Tail <= 0.02f)
	{
		// Клипа нет или он короче границы — вставать нечем, сразу покой (без «рывка» не обойтись).
		SitPhase = 0;
		UE_LOG(LogAsdaPlayer, Warning, TEXT("[встать] хвост клипа anim_%02d пуст (длина %.2fс, старт %.2fс)"),
			kSitClipAsset, Len, Start);
		// ★ 24.10.2026: состояние 9 закончилось сразу — конец цикла, как `Player_TickSendStatePosOnAnimEnd` (4024 код 0).
		//   ★ 25.10.2026 (ревизия sit108, М-1): кадром позже — после 109 вызывающего (порядок эталона 109 → … → 0).
		if (GetWorld())
		{
			GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (AnimState != EAsdaPlayerState::Sit && SitPhase == 0) { AsdaSendSitCycle4024(this, 0, TEXT("встал (хвоста вставания нет — кадром позже 109)")); }
			}));
		}
		return 0.f;
	}
	SetSitClipPositionMs(Start * 1000.f);
	SitPhase = 9;
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(SitTimer, this, &AAsdaPlayerCharacter::OnSitPhaseTimer, Tail, false);
	}
	UE_LOG(LogAsdaPlayer, Log, TEXT("[встать] состояние 9: клип anim_%02d %.2f…%.2fс (%.2fс)"),
		kSitClipAsset, Start, Len, Tail);
	return Tail;
}

// Состояние 8 «сидит»: кадр = начало цикла, дальше повторяющийся таймер отматывает его назад
//   (эталонный `objAnimTickMode32`: при t >= 3500/4000 вернуть t на 1520/2020).
void AAsdaPlayerCharacter::ResumeSitIdle()
{
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(SitTimer); }
	// ★ 24.10.2026: переход 7 → 8 — та самая точка эталона `charUpdateSabzilAnimAndSendPos` (4024 код 108). Возврат в цикл
	//   «сидит» из иных мест (конец эмоции/замаха сидя) переходом 7 → 8 не является — 108 не шлём.
	const bool bFromSitDown = (SitPhase == 7);
	if (!bFromSitDown)   // пришли не из «садится» — клип надо поставить заново
	{
		if (PlayWorldClip(kSitClipAsset, /*bLoop*/ false) <= 0.f) { SitPhase = 0; return; }
	}
	SitPhase = 8;
	SetSitClipPositionMs(SitDownEndSec() * 1000.f);
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(SitTimer, this, &AAsdaPlayerCharacter::OnSitPhaseTimer,
			FMath::Max(0.05f, SitLoopEndSec() - SitDownEndSec()), /*bLoop*/ true);
	}
	// Порядок эталона: сперва `CharFx_AutoSabzil_State(8)`, затем `Send_CharStatePos(…, 108)`.
	if (bFromSitDown) { AsdaSendSitCycle4024(this, 108, TEXT("сидит (конец «садится», 7 → 8)")); }
}

// Конец сегмента: 7 → 8, 8 → перемотка в начало цикла, 9 → покой.
void AAsdaPlayerCharacter::OnSitPhaseTimer()
{
	if (SitPhase == 7)
	{
		ResumeSitIdle();   // состояние 8 — цикл «сидит»
		return;
	}
	if (SitPhase == 8)
	{
		SetSitClipPositionMs(SitDownEndSec() * 1000.f);   // эталонный возврат к началу цикла «сидит»
		return;
	}
	if (SitPhase == 9)
	{
		SitPhase = 0;
		if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(SitTimer); }
		// Эталон по концу прохода зовёт `CChar_SetState(…, 0)`: клип покоя. Состояние уже Idle —
		//   осталось поставить клип (в SetAnimState он был СОЗНАТЕЛЬНО отложен на время вставания).
		// ★ В3 S7: «эмоция или рыбалка» → `IsActionClipOwned` (+ лопата: недостижимо — вход в 40/43 снимает фазу сидения).
		if (AnimState == EAsdaPlayerState::Idle && !IsAttackAnimActive() && !IsActionClipOwned())
		{
			PlayWorldClip(IdleClipIdx(), true);
			IdleTimer = 0.f; FidgetRemaining = 0.f;
		}
		// ★ 24.10.2026: `Player_TickSendStatePosOnAnimEnd` — по концу «встаёт» `CharFx_AutoSabzil_State(0)`, затем 4024 код 0.
		AsdaSendSitCycle4024(this, 0, TEXT("встал (конец «встаёт», 9 → 0)"));
	}
}

// ★★★ 24.10.2026 (задача владельца «выход из сидения 1:1»): ключ отката выхода из 7/8/9 по эталону. [ОРИГИНАЛ]
//   Ветки выхода из 8 у эталона (своего персонажа), разобранные по декомпилу и дизасму:
//   • «встать» (`charToggleSitStand@0xC54A70`: кнопка, клавиша 55, клавиши хода, клик по миру) и закрытие лавки
//     (`CharAction_SendStatePose@0xDD71C0`) — 9 + 4024 109, по концу «встаёт» 4024 0 (уже в порту);
//   • эмоция — гейт `Net_GetCurrentMapType(LP) == 0` (МИСНЕЙМ: состояние действия; дизасм 0xC53349: `call 0x93C749 /
//     test eax,eax / jne` — выход), т.е. сидя/садясь/вставая эмоций НЕТ; навык — `combatHandleSkillUse@0xE4B960`
//     (combat_pvp.c:7080-7091: 6/7/8/9 в списке запрета); атака — мировой клик при 6…9 = «встать» (`isMovementActionState`,
//     input.c:170); удар по себе — состояние жертвы эталон не трогает (обработчики урона в network.c пишут состояние
//     только атакующему/кастеру); смерть — `CChar_SetState(6)` переписывает 8 (в порту `SetAnimState(Dead)`);
//   • сцены: элитный магазин / парикмахерская / магазин снаряжения / бустер в городе — 4008 на ходу, затем
//     `charActionStateIs7to9@0xA210C0 ? CharFx(9) : CharFx(0)` — 9 БЕЗ 109 (по концу клипа — 0); гача (`premiumGacha_BeginRoll`)
//     и ремесло — `CharFx(0)`: 8 → 0 сразу, без хвоста и без кодов.
//   • любое состояние, пришедшее поверх 8 (эмоция/замах/навык соседа по 4025/4029/4102 — `CChar_SetState`), переписывает 8;
//     по его концу персонаж в 0 (стоит), а не снова «сидит». Сервер получает ту же цепочку кодов: свой персонаж до
//     этих состояний сидя не доходит (гейты выше), а `CChar_SetState` сам 4024 не шлёт.
//   [ПОРТ] до правки клип эмоции/замаха сидя возвращал цикл «сидит» (`ResumeSitIdle`), сцены сидя ничего не делали, гача и
//   ремесло ставили хвост «встаёт» и по его концу код 0, эмоция в «встаёт» (9) разрешалась.
static ASDA_DEV_CVAR(int32, CVarAsdaCharSitExitEtalon,
	TEXT("asda.Char.SitExitEtalon"), 1,
	TEXT("1 (по умолчанию) [ОРИГИНАЛ] = выход из сидения как у оригинала: эмоция/замах/навык поверх сидения снимают его без ")
	TEXT("«встаёт» (по концу — покой), сцены (элитный магазин, парикмахерская, магазин снаряжения, бустер в городе) поднимают ")
	TEXT("сидящего (9 без 109), гача/ремесло — сразу покой, эмоция при «встаёт» запрещена; 0 = прежний порт."),
	ECVF_Default);

bool AAsdaPlayerCharacter::IsSitExitEtalon()
{
	return CVarAsdaCharSitExitEtalon.GetValueOnGameThread() != 0;
}

// Переписать 7/8/9 эталонной записью состояния (объявление в .h): фаза и её таймер сняты, `Sit` → `Idle`, кодов нет.
bool AAsdaPlayerCharacter::DropSitForEtalonState(bool bIdleClip, const TCHAR* Why)
{
	if (!IsSitExitEtalon()) { return false; }
	if (AnimState != EAsdaPlayerState::Sit && SitPhase == 0) { return false; }
	const int32 Was = GetEtalonSitState();
	SitPhase = 0;
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(SitTimer); }
	if (AnimState == EAsdaPlayerState::Sit) { AnimState = EAsdaPlayerState::Idle; }
	if (bIdleClip && !IsAttackAnimActive() && !IsSkillAnimActive() && !IsActionClipOwned())
	{
		PlayWorldClip(IdleClipIdx(), true);
		IdleTimer = 0.f; FidgetRemaining = 0.f;
	}
	UE_LOG(LogAsdaPlayer, Log, TEXT("[сидение] %s: состояние %d переписано (%s) — без «встаёт» и без 4024 (эталон CChar_SetState)"),
		*GetName(), Was, Why ? Why : TEXT("?"));
	return true;
}

// Вход в сцену эталона: на ходу 4008, затем «встаёт» (9, без 109) при 7/8, ничего при 9, покой (0) иначе (объявление в .h).
void AAsdaPlayerCharacter::ApplyEtalonSceneStop(const TCHAR* Why)
{
	if (!IsSitExitEtalon() || AnimState == EAsdaPlayerState::Dead) { return; }
	// 1. `getFlag_47551` (идёт) → `Net_SendActionPacket` = 4008 в точке персонажа.
	if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GetController()))
	{
		if (PC->IsMovingNow()) { PC->StopMoveAndNotify(Why); }
	}
	const int32 Sit = GetEtalonSitState();
	if (Sit == 7 || Sit == 8)
	{
		// 2а. `charActionStateIs7to9` → `CharFx_AutoSabzil_State(9)`: хвост «встаёт», 109 НЕ уходит; по концу — 4024 0 (`OnSitPhaseTimer`).
		SetAnimState(EAsdaPlayerState::Idle);
		UE_LOG(LogAsdaPlayer, Log, TEXT("[сидение] %s: сидел (%d) — «встаёт» без 109 (эталон CharFx 9)"), Why, Sit);
		return;
	}
	if (Sit == 9) { return; }   // 2б. уже «встаёт»: запись того же состояния у эталона ничего не меняет (`field_8 == newAction`)
	// 2в. `CharFx_AutoSabzil_State(0)`: эмоция/ход сняты, покой.
	if (IsEmotionAnimActive())
	{
		StopEmotionClip();
		if (!IsAttackAnimActive() && !IsSkillAnimActive()) { PlayWorldClip(IdleClipIdx(), true); IdleTimer = 0.f; FidgetRemaining = 0.f; }
	}
	SetAnimState(EAsdaPlayerState::Idle);
}

// ══════════ ★★★ ЭМОЦИИ: одноразовый клип по эталонному состоянию 33…46 ═══════════════════════
// Реверс: `Reports/char_info_window.md` §7. `CChar_SetState@0xA13D50` (entity_char.c:19788-19867)
//   берёт клип из слота `(state-23)`, перематывает на кадр 0 и взводит «занят действием»; тик
//   `CCharacter_UpdateActionState@0xA16D20` по концу прохода возвращает состояние 0. Зацикливания НЕТ.
// Порядок ассетов задан экспортом (Tools/export_char_emotions.py): состояния идут подряд, кроме
//   пропусков эталона (40 и 43 — это копка «сабзиль», у неё свои клипы, здесь их нет).
int32 AAsdaPlayerCharacter::EmotionClipIdx(int32 EmoteState)
{
	switch (EmoteState)
	{
	case 33: return 86;   // happy01
	case 34: return 87;   // angry01
	case 35: return 88;   // sad01
	case 36: return 89;   // thanks01
	case 37: return 90;   // hi01
	case 38: return 91;   // return01
	case 39: return 92;   // battle01
	case 41: return 93;   // dance01
	case 42: return 94;   // dance02
	case 44: return 95;   // courtship
	case 45: return 96;   // juggling
	case 46: return 97;   // upperarm
	default: return -1;
	}
}

float AAsdaPlayerCharacter::GetAttackClipPositionSec() const
{
	if (!BaseSkel) { return 0.f; }
	const UAnimSingleNodeInstance* SN = BaseSkel->GetSingleNodeInstance();
	return SN ? (float)SN->GetCurrentTime() : 0.f;
}

// ★★★ Пройденная часть замаха В РЕАЛЬНОМ ВРЕМЕНИ.
//   ★ 16.09.2026 (ревизия, МЕЛК): прежний комментарий здесь УТВЕРЖДАЛ, что позиция клипа живёт «в
//   собственном времени клипа (скорость 1)». Это неправда — клипу атаки выставлена скорость темпа
//   (`PlayWorldClip`), поэтому позиция копится БЫСТРЕЕ реального времени ровно во столько же раз:
//       позиция = реальное_время × темп   ⇒   реальное_время = позиция / темп.
//   Формула была верна, но обоснование — ложным, а по ложному обоснованию её «поправили» бы не в ту
//   сторону. Делим на ЗАПОМНЕННЫЙ множитель прохода, а не на опрошенный заново: позиция накоплена
//   именно по нему (см. `AttackClipTempo`). Тайминги ударов из `GetComboTiming` уже в реальном времени.
float AAsdaPlayerCharacter::GetAttackSwingElapsedSec() const
{
	return GetAttackClipPositionSec() / FMath::Max(0.1f, AttackClipTempo);
}

// ★★★ Множитель темпа атаки ЭТОГО персонажа. Считает единая точка порта — подсистема мира (у неё
//   реестр соседей и их экипировка); здесь только доступ от актора. Разбор эталона, формула и оба
//   исправленных дефекта оригинала — у `UAsdaWorldSubsystem::GetAttackTempoMul`.
float AAsdaPlayerCharacter::AttackTempoMul() const
{
	// ★ 15.10.2026: свой персонаж под серверными часами — скорость текущего отрезка замаха (`BeginSendSegment`).
	//   Ею играет клип, по ней же переводятся тайминги ударов (`GetComboTiming`) и позиция клипа в реальное время.
	if (AttackSyncRate > 0.f && IsServerRhythmOwner()) { return AttackSyncRate; }
	// ★ 17.10.2026 (Ф2): сосед с замахом от пакетов — скорость текущего отрезка (номинальный темп × коэффициент по интервалам 4029).
	if (bPeerPktDriven && PeerSegRate > 0.f) { return PeerSegRate; }
	const UGameInstance* GI = GetGameInstance();
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	return WS ? WS->GetAttackTempoMul(this) : 1.f;
}

// ★★★ Множитель темпа АНИМАЦИЙ НАВЫКОВ И ЭФФЕКТОВ этого персонажа (эталонное `animSpeedScale`,
//   `CCharacter + 0x500`). Считает та же единая точка порта — подсистема мира (у неё реестр
//   соседей и их экипировка); здесь только доступ от актора. С `AttackTempoMul` НЕ складывается и
//   не перемножается: у эталона это две независимые шкалы, и ветка атаки вторую не читает вовсе.
float AAsdaPlayerCharacter::AnimSpeedScaleMul() const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	return WS ? WS->GetAnimSpeedScale(this) : 1.f;
}

bool AAsdaPlayerCharacter::IsEmotionAnimActive() const
{
	// ★ В3 S7: номер эмоции — из единого поля `BusyAction`.
	return GetWorld() && AsdaActionState::IsEmote((int32)BusyAction) && GetWorld()->GetTimeSeconds() < EmotionUntilS;
}

// ★ В3 S7 (12.10.2026): ключ отката исправления единого состояния действия (`★ ИСПРАВЛЕНИЕ` в `TickAutoDanceZone`).
//   Разбор — `Reports/dedup_audit2/wave3_S7.md` §3.
static ASDA_DEV_CVAR(int32, CVarAsdaActionLegacyBusyGates,
	TEXT("asda.Action.LegacyBusyGates"), 0,
	TEXT("Гейты «персонаж занят» (состояние действия эталона +47551): 0 (по умолчанию) = как оригинал — танцпол Alpen не ")
	TEXT("запускается ни в одном «занятом» состоянии (эмоция, 40/43, 60…67, 68); ")
	TEXT("1 = прежний порт (до 12.10.2026) — танцпол гейтят только рыбалка и лопата (в 68 танец запускается поверх)."),
	ECVF_Default);

bool AAsdaPlayerCharacter::IsLegacyBusyGates()
{
	return CVarAsdaActionLegacyBusyGates.GetValueOnGameThread() != 0;
}

// ★ В3 S7: номер «занятого» состояния эталона (`+47551`). Эмоция считается только пока идёт её клип: эталон по концу прохода
//   переводит в 0 (`CCharacter_UpdateActionState`), у порта это таймер `OnEmotionClipEnd` — между ними состояние уже «не занят».
int32 AAsdaPlayerCharacter::GetBusyActionCode() const
{
	const int32 S = (int32)BusyAction;
	if (AsdaActionState::IsEmote(S)) { return IsEmotionAnimActive() ? S : 0; }
	return S;
}

// ★★ В3 S7 (12.10.2026): ЕДИНАЯ ЗАПИСЬ «ЗАНЯТОГО» СОСТОЯНИЯ. `CChar_SetState@0xA13D50` (entity_char.c:19243-19257) пишет поле
//   `+47551` безусловно — прежнее состояние перестаёт существовать. Порт держал 4 признака и гасил их друг другом вручную в
//   6 местах (эмоция ↔ рыбалка ↔ 68 ↔ лопата); здесь — одно поле и снятие хвостов прежнего состояния: таймер конца прохода
//   эмоции/рыбалки/лопаты, сторож клипа рыбалки, видимость инструмента лопаты. Клип и оружие — у вызывающего (порядок
//   «признак → уборка оружия → клип», память emotion-clip-rules).
void AAsdaPlayerCharacter::SetBusyAction(EAsdaActionState New, const TCHAR* Why)
{
	const EAsdaActionState Old = BusyAction;
	const int32 O = (int32)Old;
	if (Old != New)
	{
		if (UWorld* W = GetWorld())
		{
			FTimerManager& TM = W->GetTimerManager();
			if (AsdaActionState::IsEmote(O))  { TM.ClearTimer(EmotionTimer); }
			if (AsdaActionState::IsFish(O))   { TM.ClearTimer(FishClipTimer); }
			if (AsdaActionState::IsShovel(O)) { TM.ClearTimer(ShovelClipTimer); }
			if (O == (int32)EAsdaActionState::Action51) { TM.ClearTimer(CampPoseTimer); }   // ★ 30.10.2026: конец клипа 51
		}
		if (AsdaActionState::IsEmote(O)) { EmotionUntilS = 0.0; }
		if (AsdaActionState::IsFish(O))  { FishClipAsset = nullptr; }
	}
	BusyAction = New;
	if (AsdaActionState::IsShovel(O) || AsdaActionState::IsShovel((int32)New)) { UpdateShovelToolVisible(); }   // инструмент — ровно в 40/43
	if (Old != New)
	{
		UE_LOG(LogAsdaPlayer, Log, TEXT("[действие] состояние %d → %d (%s)"), O, (int32)New, Why ? Why : TEXT("?"));
	}
}

// ★★ В3 S7: ОДНА ПРЕАМБУЛА действия «на месте» — разбор у объявления. Прежде три копии: `PlayFishAction`, `EnterEtalonState68`,
//   `EnterEtalonShovelState` (02_cpp_world_net_data.md N5).
void AAsdaPlayerCharacter::StopMotionForAction()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	if (bMoving || !MoveDirGame.IsNearlyZero())
	{
		bMoving = false;
		MoveDirGame = FVector2D::ZeroVector;
		MoveDstGame = GetGamePosXZ();
	}
	if (SitPhase != 0) { SitPhase = 0; W->GetTimerManager().ClearTimer(SitTimer); }
	if (AnimState == EAsdaPlayerState::Move || AnimState == EAsdaPlayerState::Sit) { AnimState = EAsdaPlayerState::Idle; }
	if (IsAttackAnimActive() || AttackLoopClipIdx >= 0)
	{
		AttackAnimUntilS = 0.0;
		AttackLoopClipIdx = -1; AttackLoopHoldUntilS = 0.0;
		W->GetTimerManager().ClearTimer(AttackLoopTimer);
		W->GetTimerManager().ClearTimer(AttackSendTimer);   // ★ ревизия Ф2 (мелочь 3): точка отправки отрезка тоже снята
	}
	// ★ 17.10.2026 (ревизия Ф2, мелочь 3): действие на месте выводит и из пакетного замаха соседа — иначе граница отрезка
	//   (`BeginPeerSegment`) и «тишина» продолжали жить без цикла. Отсчёт кадров удара PvP (Ф4) — тоже.
	ResetPeerPktState();
	ResetHitFrameScan();
}

// ★ В3 S7: вход в «занятое» действие на месте (рыбалка 60…67, 68, лопата 40/43) — преамбула и единое поле.
void AAsdaPlayerCharacter::BeginEtalonAction(EAsdaActionState New, const TCHAR* Why)
{
	StopMotionForAction();
	SetBusyAction(New, Why);
}

float AAsdaPlayerCharacter::PlayEmotionClip(int32 EmoteState)
{
	const int32 Idx = EmotionClipIdx(EmoteState);
	if (Idx < 0 || !GetWorld()) { return 0.f; }
	// ★ Эталонного «эмоция верхом» не бывает: `Emote_Execute` (ui_core.c:31976) отсекает любую эмоцию
	//   при активном баффе транспорта нотисом 2040. Здесь — последний рубеж, чтобы клип не играл в седле.
	if (bMounted) { return 0.f; }
	// ★ Волна В5: действие рыбалки — тот же признак «занят» (`bActionBusy = 1`). Своему автомат рыбалки ждёт
	//   конца клипа — эмоцию не начинаем; у ЧУЖОГО пришедшее состояние 33…46 перебивает 60…67 (`CChar_SetState`).
	if (IsFishActionActive())
	{
		if (bFishActionSelf)
		{
			UE_LOG(LogAsdaPlayer, Log, TEXT("[эмоция] состояние %d отклонено: идёт действие рыбалки %d (признак «занят»)"), EmoteState, GetFishAction());
			return 0.f;
		}
		StopFishAction(TEXT("эмоция соседа"));
	}
	// ★★★ ПОКА ИГРАЕТ ЭМОЦИЯ — НОВУЮ НЕ НАЧИНАЕМ (требование владельца 2026-08-25; в оригинале это
	//   признак «персонаж занят»: ветка эмоции ставит `bActionBusy = 1` вместе с запуском клипа,
	//   `entity_char.c:17174-17204`). Сбросить эмоцию досрочно может только движение — оно зовёт
	//   `StopEmotionClip` через `SetAnimState`.
	if (IsEmotionAnimActive())
	{
		UE_LOG(LogAsdaPlayer, Log,
			TEXT("[эмоция] состояние %d отклонено: ещё играет %d (осталось %.2f с) — оригинал держит признак «занят»"),
			EmoteState, GetEmotionState(), (float)(EmotionUntilS - GetWorld()->GetTimeSeconds()));
		return 0.f;
	}
	// ★★★ ПОРЯДОК ВАЖЕН: сначала признак эмоции и уборка оружия, и только потом запуск клипа.
	//   Уборка оружия меняет НАБОР клипов (тот же флаг переключает набор, кость меша и кость
	//   свечения), поэтому запусти мы клип раньше — он бы стартовал из старого набора и был бы
	//   заменён на первом же кадре. Признак ставим до уборки, иначе она не увидит эмоцию и оружие
	//   останется в руке; длительность уточняем сразу после запуска.
	// ★ В45fix2 (МЕЛК-5): эмоция 33…46 переписывает состояние 68 (`CChar_SetState`, g:76456); ★ 09.10.2026 (п. 5 владельца): и
	//   лопату 40/43 (запись безусловна). ★ В3 S7: одна запись единого поля — прежнее снимает `SetBusyAction`.
	// ★ 24.10.2026 (выход из сидения 1:1): и сидение 7/8/9 — без хвоста «встаёт»; по концу эмоции — покой, не «сидит».
	// ★ 25.10.2026 (ревизия sit108, М-4): только если клип эмоции есть — иначе состояние не меняется вовсе (эмоции не будет), а снятое
	//   сидение «застыло» бы без клипа покоя. Путь тот же, что у `PlayWorldClip` (вне седла: седло отсекается выше).
	if (UAsdaModelAnimSubsystem::LoadClipChecked(FString::Printf(TEXT("%sanim_%02d"), *CharBase(Sex), Idx)))
	{
		DropSitForEtalonState(/*bIdleClip*/ false, TEXT("эмоция 33…46"));
	}
	SetBusyAction((EAsdaActionState)EmoteState, TEXT("эмоция"));
	EmotionUntilS = GetWorld()->GetTimeSeconds() + 3600.0;   // временно, до измерения клипа
	UpdateWeaponMotionHidden();                              // ★ оружие за спину на время эмоции
	const float Len = PlayWorldClip(Idx, false);
	if (Len <= 0.f)
	{
		SetBusyAction(EAsdaActionState::Idle, TEXT("клипа эмоции нет"));
		UpdateWeaponMotionHidden();   // клипа нет — вернуть оружие в руку
		return 0.f;
	}
	EmotionUntilS = GetWorld()->GetTimeSeconds() + (double)Len;
	EmotionStartedS = GetWorld()->GetTimeSeconds();
	EmotionClipAsset = (BaseSkel && BaseSkel->GetSingleNodeInstance())
		? Cast<UAnimSequence>(BaseSkel->GetSingleNodeInstance()->GetAnimationAsset()) : nullptr;
	GetWorld()->GetTimerManager().SetTimer(EmotionTimer, this, &AAsdaPlayerCharacter::OnEmotionClipEnd, Len, false);
	UE_LOG(LogAsdaPlayer, Log, TEXT("[эмоция] состояние %d → клип anim_%02d, %.2fс"), EmoteState, Idx, Len);
	return Len;
}

// ══════════ ★★★ 30.10.2026 ПОЗЫ 47 «ЗАХВАТ ТОЧКИ» / 51 «ПРОИГРАВШИЙ PvP» [ОРИГИНАЛ] ═══════════════════════════════════════
// Разбор — у объявления и `Reports/faction_select/BATTLE_GAPS_RE.md` §2. Прежде 47/51 в порту не ставились вовсе (клипов
//   трека 25/26 не было в ассетах, реестр отложенного): захватчик точки CampWar стоял в покое с оружием в руке, проигравший дуэль —
//   без жеста. `CChar_SetState` пишет `+47551` безусловно и переписывает ход/сидение/замах (`LABEL_213`) — общая преамбула
//   `BeginEtalonAction`, как у рыбалки/лопаты/68; «занят» → оружие за спину (`UpdateWeaponMotionHidden`), фиджета нет.
float AAsdaPlayerCharacter::EnterCampPose(int32 State, const TCHAR* Why)
{
	if ((State != 47 && State != 51) || !GetWorld()) { return 0.f; }
	const int32 Idx = (State == 47) ? kClipOccupy : kClipLoser;
	// Клип проверяется ДО записи состояния: без него позы нет вовсе, а снятые ход/сидение «застыли» бы без клипа.
	if (!UAsdaModelAnimSubsystem::LoadClipChecked(FString::Printf(TEXT("%sanim_%02d"), *CharBase(Sex), Idx)))
	{
		UE_LOG(LogAsdaPlayer, Warning, TEXT("[поза] состояние %d (%s): клипа anim_%02d нет в ассетах — поза не поставлена"), State, Why ? Why : TEXT("?"), Idx);
		return 0.f;
	}
	BeginEtalonAction((EAsdaActionState)State, Why);   // преамбула + единое поле (прежнее «занятое» снимает `SetBusyAction`)
	UpdateWeaponMotionHidden();                        // `bActionBusy = 1` — оружие за спину ДО клипа (порядок как у эмоции)
	const bool bLoop = (State == 47);                  // 47: case 14 перематывает на кадр 0 без смены состояния
	const float Len = PlayWorldClip(Idx, bLoop);
	if (Len <= 0.f)
	{
		SetBusyAction(EAsdaActionState::Idle, TEXT("клипа позы нет"));
		UpdateWeaponMotionHidden();
		return 0.f;
	}
	IdleTimer = 0.f; FidgetRemaining = 0.f;
	if (!bLoop) { GetWorld()->GetTimerManager().SetTimer(CampPoseTimer, this, &AAsdaPlayerCharacter::OnCampPoseClipEnd, Len, false); }
	UE_LOG(LogAsdaPlayer, Log, TEXT("[поза] состояние %d → клип anim_%02d, %.3f с, %s (%s)"), State, Idx, Len, bLoop ? TEXT("цикл") : TEXT("один раз"), Why ? Why : TEXT("?"));
	return Len;
}

void AAsdaPlayerCharacter::LeaveCampPose(int32 State, const TCHAR* Why)
{
	if ((State != 47 && State != 51) || (int32)BusyAction != State) { return; }   // эталон: `if (+47551 == 47)`
	SetBusyAction(EAsdaActionState::Idle, Why);   // таймер 51 снимает единая запись
	UpdateWeaponMotionHidden();                   // `bActionBusy = 0` — оружие в руку (если зона позволяет)
	// Клип покоя — только если персонаж в покое и не в замахе/навыке: при смене состояния на ход/сидение/смерть свой клип
	//   ставит `SetAnimState` следом, замах/навык вернут клип состояния своим таймером.
	if (AnimState == EAsdaPlayerState::Idle && !IsAttackAnimActive() && !IsSkillAnimActive())
	{
		PlayWorldClip(IdleClipIdx(), true);
		IdleTimer = 0.f; FidgetRemaining = 0.f;
	}
}

void AAsdaPlayerCharacter::OnCampPoseClipEnd()
{
	LeaveCampPose(51, TEXT("конец клипа 51 (эталон SetState(0))"));
}

// ★★ ПОСТОЯННОЕ КАЧЕСТВО ПЕРСОНАЖА. Движок по умолчанию упрощает и модель (уровни детализации),
//   и текстуры (потоковая подкачка мипов) по мере удаления камеры — на дефолтном отдалении это уже
//   читается как «персонаж мыльный». Здесь мы это отключаем ДЛЯ ПЕРСОНАЖЕЙ: нулевой уровень
//   детализации у всех их мешей и полная детализация у всех их текстур.
//   Раз в секунду, потому что модель пересобирается на ходу (смена экипировки, уборка оружия,
//   надетые части появляются и исчезают) и новые компоненты обязаны получить то же правило.
static TAutoConsoleVariable<int32> CVarAsdaCharStableQuality(
	TEXT("asda.Char.StableQuality"), 1,
	TEXT("1 = у персонажей всегда нулевой уровень детализации модели и полные мипы текстур; 0 = как решит движок."),
	ECVF_Default);

// ★★ ПОЛОВИНА ТЕКСТУР — ОТДЕЛЬНЫМ КЛЮЧОМ (ревизия charsel_preload 13.09.2026). До 13.09 она фактически не
//   работала (вызов-заглушка `GetUsedTextures`, см. `CollectActorTextures`), в мире действовал только LOD0.
//   Включить её для ВСЕХ персонажей в мире — не бесплатно: полные мипы всей увиденной экипировки в видеопамяти
//   и перебор текстур материалов раз в секунду. Поэтому по умолчанию — только экран выбора персонажа (там
//   2–6 превью и память отпускается при выходе); мир — решение владельца (значение 2).
static TAutoConsoleVariable<int32> CVarAsdaCharStableQualityTextures(
	TEXT("asda.Char.StableQuality.Textures"), 1,
	TEXT("Полные мипы текстур персонажей (при asda.Char.StableQuality 1): 0 = нигде; 1 = только превью экрана выбора "
	     "персонажа и его предзагрузка (по умолчанию); 2 = ещё и все видимые персонажи в мире (флаг снимается, когда "
	     "текстуру не держит ни один видимый персонаж)."),
	ECVF_Default);

bool AAsdaPlayerCharacter::IsStableQualityEnabled()
{
	return CVarAsdaCharStableQuality.GetValueOnGameThread() != 0;
}

int32 AAsdaPlayerCharacter::GetStableQualityTexturesMode()
{
	return FMath::Clamp(CVarAsdaCharStableQualityTextures.GetValueOnGameThread(), 0, 2);
}

void AAsdaPlayerCharacter::ApplyStableQuality()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	const double Now = W->GetTimeSeconds();
	const bool bOn = IsStableQualityEnabled();
	if (NextStableQualityS <= 0.0)
	{
		// ★ Первый проход — только LOD (дёшево, сразу при появлении), а полный пересмотр — со случайной фазой
		//   в пределах секунды: массово заспавненные соседи (вход в мир, телепорт) не делают его в одном кадре.
		ApplyStableLodToActor(this, bOn);
		NextStableQualityS = Now + 0.05 + 0.95 * (double)FMath::FRand();
		return;
	}
	if (Now < NextStableQualityS) { return; }
	NextStableQualityS = Now + 1.0;
	ApplyStableLodToActor(this, bOn);
	// Половина текстур в мире — только при значении 2 и пока персонаж видим; иначе отпускаем своё.
	if (bOn && GetStableQualityTexturesMode() >= 2 && !IsHidden())
	{
		TSet<UStreamableRenderAsset*> Want;
		CollectActorTextures(this, Want);
		UAsdaTextureSubsystem::SyncFullMips(StableQualityMips, Want);
	}
	else if (StableQualityMips.Num() > 0)
	{
		UAsdaTextureSubsystem::ReleaseAllFullMips(StableQualityMips);
	}
}

void AAsdaPlayerCharacter::ApplyStableLodToActor(AActor* Actor, bool bOn)
{
	if (!Actor) { return; }
	TArray<USkeletalMeshComponent*> Skels;
	Actor->GetComponents<USkeletalMeshComponent>(Skels);
	for (USkeletalMeshComponent* SC : Skels)
	{
		// 0 = «как решит движок», 1 = принудительно самый детальный уровень.
		if (SC) { SC->SetForcedLOD(bOn ? 1 : 0); }
	}
	TArray<UStaticMeshComponent*> Statics;
	Actor->GetComponents<UStaticMeshComponent>(Statics);
	for (UStaticMeshComponent* MC : Statics)
	{
		if (MC) { MC->SetForcedLodModel(bOn ? 1 : 0); }
	}
}

void AAsdaPlayerCharacter::CollectActorTextures(AActor* Actor, TSet<UStreamableRenderAsset*>& Out)
{
	if (!Actor) { return; }
	TArray<UMeshComponent*> Comps;
	Actor->GetComponents<UMeshComponent>(Comps);
	for (UMeshComponent* MC : Comps)
	{
		if (!MC) { continue; }
		const int32 N = MC->GetNumMaterials();
		for (int32 i = 0; i < N; ++i)
		{
			UMaterialInterface* MI = MC->GetMaterial(i);
			if (!MI) { continue; }
			TArray<UTexture*> Used;
			// ★ 13.09.2026 (charsel_preload): прежний вызов с пятью аргументами (качество/уровень фич) в UE 5.7+ —
			//   УСТАРЕВШАЯ ЗАГЛУШКА С ПУСТЫМ ТЕЛОМ (`MaterialInterface.h`: `... const final {}`). Список был пуст
			//   всегда, и мипы персонажа не форсировались вовсе — работала только половина правила (LOD).
			MI->GetUsedTextures(Used);
			for (UTexture* T : Used)
			{
				if (UTexture2D* T2 = Cast<UTexture2D>(T)) { Out.Add(T2); }
			}
		}
	}
}

// ═══ ★★★ РЕЖИМ «ДУША» (SoulState == 1): ПОЛУПРОЗРАЧНАЯ МОДЕЛЬ α = 0.3 ═══════════════════════════
//   Разбор и ссылки на эталон — у объявления `SetSoulTranslucent` в заголовке. Здесь — механика порта.
//   Ключ отката: 1 = переносим эталон (тело + надетые части на translucent-двойнике мастера);
//   0 = материалы не трогаем (персонаж-душа выглядит обычным; плашка/гейт от ключа не зависят).
static ASDA_DEV_CVAR(int32, CVarAsdaSoulTranslucent,
	TEXT("asda.Soul.Translucent"), 1,
	TEXT("1 = персонаж в режиме «душа» (SoulState==1) рисуется полупрозрачным, α=0.3 на тело и всю надетую "
	     "экипировку (эталон charEntityUpdateTick :80703); 0 = материалы не подменять."),
	ECVF_Default);

// Эталонная константа α «души»: 0x3E99999A = 0.3f (`objArr_setField7Broadcast(pChar + 2, 0x3E99999A)`).
static constexpr float kAsdaSoulAlpha = 0.3f;

// ═══ ★★★ GM-«СКРЫТЬСЯ»: ПОЛУПРОЗРАЧНОСТЬ СВОЕГО ПЕРСОНАЖА ═══════════════════════════════════════
//   [ПОРТ] Отступление от оригинала, оговорённое владельцем: у эталона свой скрытый оператор
//   пропадает целиком (`localPlayerInputToggleByFlags@0xF49BD0` → `sub_9210D4(localPlayer,1)`),
//   здесь он остаётся видимым себе, но прозрачным. Ключ 0 = поведение ОРИГИНАЛА (актор просто
//   скрывается мир-подсистемой, как и у соседей).
static ASDA_DEV_CVAR(int32, CVarAsdaGmHideTranslucent,
	TEXT("asda.Gm.HideTranslucent"), 1,
	TEXT("1 = свой персонаж под GM-hide рисуется полупрозрачным (тело + экипировка), α из asda.Gm.HideAlpha; "
	     "0 = как в оригинале — свой персонаж не рисуется вовсе."),
	ECVF_Default);

// ★ 01.11.2026 [ИСПРАВЛЕНИЕ] (soulmate-glow, ревизия): эффекты навыков, которые следуют за персонажем БЕЗ прикрепления
//   (`ConfigureBoneAttach` в режиме `CharWorld`/`OwnerBoundFx` — позицию берут тиком, `GetAttachedActors` их не видит), под
//   GM-«скрыться» оставались в полную яркость: самотест SoulmateGlow (G1) — показ навыка Дружбы под скрытием держал α 0.5 вместо
//   0.15. Владелец таких эффектов — персонаж (`SpawnByJebName(..., Owner)`), поэтому притушиваются и неприкреплённые эффекты из
//   `Children`. 1 — притушивать и их; 0 — прежний порт (только прикреплённые).
static ASDA_DEV_CVAR(int32, CVarAsdaGmHideOwnedFx,
	TEXT("asda.Gm.HideOwnedFx"), 1,
	TEXT("GM-hide: притушивать и эффекты, которыми персонаж владеет, но которые к нему не прикреплены (навыки в режиме следования): 1 = да, 0 = прежний порт"),
	ECVF_Default);

// α НАЛОЖЕННЫХ ЭФФЕКТОВ (ауры, свечение оружия, крылья, эффекты навыков и баффов) под hide.
//   Отдельный ключ от α модели: эффекты почти все аддитивные, и одна и та же цифра читается на них
//   иначе, чем на теле. У «души» эффекты α НЕ получают вовсе (эталон: отдельные узлы очереди),
//   поэтому множитель ставится ТОЛЬКО по причине GM-hide.
static ASDA_DEV_CVAR(float, CVarAsdaGmHideFxAlpha,
	TEXT("asda.Gm.HideFxAlpha"), 0.3f,
	TEXT("Прозрачность наложенных на своего персонажа эффектов под GM-hide (ауры, свечение оружия, "
	     "крылья, эффекты навыков): 0 = погашены, 1 = как обычно."),
	ECVF_Default);

// ★ Тень скрытого оператора. `M_AsdaCharLitSoul` намеренно сделан с masked-shadow, чтобы у «души»
//   тень оставалась ПОЛНОЙ (так у эталона). Для hide это неверно: у оригинала скрытый оператор не
//   рисуется вовсе и тени не отбрасывает, а плотная чёрная тень под полупрозрачным силуэтом читается
//   как дефект. Гасим — с возможностью вернуть.
static ASDA_DEV_CVAR(int32, CVarAsdaGmHideCastShadow,
	TEXT("asda.Gm.HideCastShadow"), 0,
	TEXT("0 = скрытый оператор не отбрасывает тень (как в оригинале, где он не рисуется вовсе); "
	     "1 = тень остаётся полной, как у режима «душа»."),
	ECVF_Default);

// α своего персонажа под hide. Эталонного числа НЕТ (у оригинала прозрачности здесь нет вовсе),
//   поэтому взято эталонное значение «души» 0.3 — единственная подтверждённая в игре α модели.
static ASDA_DEV_CVAR(float, CVarAsdaGmHideAlpha,
	TEXT("asda.Gm.HideAlpha"), 0.3f,
	TEXT("Прозрачность своего персонажа под GM-hide: 0 = невидим, 1 = обычный. По умолчанию 0.3 — "
	     "то же число, что у режима «душа» (эталон charEntityUpdateTick :80703)."),
	ECVF_Default);
// Translucent-двойник мастера персонажа (Tools/asda_build_charlit_soul_mat.py).
static const TCHAR* kAsdaSoulMaterialPath = AsdaMat::kCharLitSoulPath;   // ★ В1-C F10: путь мастера — общая константа
// Скалярные параметры мастера персонажа, которые MIC/MID части могли переопределить — переносим в наш
//   MID, чтобы светотень «души» совпадала с обычным видом той же части (тот же граф, те же числа).
static const TCHAR* kAsdaSoulCopiedScalars[] = {
	TEXT("Roughness"), TEXT("Metallic"), TEXT("SpecBase"), TEXT("SpecAlphaBoost"), TEXT("AmbientFloor")
};

bool AAsdaPlayerCharacter::IsSoulTranslucencyActive() const
{
	return bSoulTranslucent && CVarAsdaSoulTranslucent.GetValueOnGameThread() != 0;
}

bool AAsdaPlayerCharacter::IsGmHideTranslucencyActive() const
{
	return bGmHideTranslucent && CVarAsdaGmHideTranslucent.GetValueOnGameThread() != 0;
}

bool AAsdaPlayerCharacter::IsTranslucencyActive() const
{
	return IsSoulTranslucencyActive() || IsGmHideTranslucencyActive() || IsStealthTranslucencyActive();
}

// ★ 24.10.2026: вид невидимости навыка (112/186) — эталонная α +6329 (`CharManager_UpdateAll@0x9D56C0`, `charEntityUpdateTick`).
//   Ключ 0 — прежний вид порта (невидимые рисуются как обычно).
static ASDA_DEV_CVAR(int32, CVarAsdaStealthVisual,
	TEXT("asda.Stealth.Visual"), 1,
	TEXT("[ОРИГИНАЛ] 1 — невидимость навыка (состояния 112/186) гасит персонажа, как эталон: сам и отряд 0.3 на время затухания, ")
	TEXT("чужие 0 (не рисуются), у наблюдателя — отряд 0.2, Друг 0.4, «Обнаружение маскировки» (154) в радиусе 0.5; снятие — ")
	TEXT("проявление за 1 с. 0 — невидимые рисуются как обычно (поведение порта до 24.10)."),
	ECVF_Default);
// Значение `asda.Stealth.Visual` для AsdaStealthSubsystem.cpp (ключ живёт здесь; поиск по имени оставил бы имя дев-ключа в Shipping).
bool AsdaStealthVisualOn() { return CVarAsdaStealthVisual.GetValueOnGameThread() != 0; }

bool AAsdaPlayerCharacter::IsStealthTranslucencyActive() const
{
	return StealthAlpha < 0.999f && CVarAsdaStealthVisual.GetValueOnGameThread() != 0;
}

float AAsdaPlayerCharacter::CurrentTranslucentAlpha() const
{
	// Приоритет у «души»: её α подтверждена эталоном, у hide число — наше. Если взведены обе
	//   причины, берём меньшую (более прозрачную) — иначе включение hide поверх души «проявило» бы
	//   персонажа, чего игрок точно не ждёт. ★ 24.10.2026: то же для третьей причины — невидимости навыка.
	const bool bSoul = IsSoulTranslucencyActive();
	const bool bHide = IsGmHideTranslucencyActive();
	const float HideA = FMath::Clamp(CVarAsdaGmHideAlpha.GetValueOnGameThread(), 0.f, 1.f);
	float A = 1.f;
	if (bSoul) { A = FMath::Min(A, kAsdaSoulAlpha); }
	if (bHide) { A = FMath::Min(A, HideA); }
	if (IsStealthTranslucencyActive()) { A = FMath::Min(A, FMath::Clamp(StealthAlpha, 0.f, 1.f)); }
	return A;
}

void AAsdaPlayerCharacter::UpdateTranslucentAlphaOnly()
{
	if (!bSoulMaterialsApplied) { return; }
	const float Alpha = CurrentTranslucentAlpha();
	if (FMath::IsNearlyEqual(Alpha, AppliedTranslucentAlpha)) { return; }
	for (FAsdaSoulSlotSwap& Sw : SoulSwaps)
	{
		if (Sw.Mid) { Sw.Mid->SetScalarParameterValue(FName(AsdaMat::kParamSoulOpacity), Alpha); }
	}
	AppliedTranslucentAlpha = Alpha;
}

// ★★★ Вид невидимости навыка (разбор — у объявления). Свой — α 0.3, соседи — по наблюдателю; ставит UAsdaStealthSubsystem.
void AAsdaPlayerCharacter::SetStealthVisual(float Alpha, bool bRender)
{
	Alpha = FMath::Clamp(Alpha, 0.f, 1.f);
	const bool bWasTrans = IsStealthTranslucencyActive();
	StealthAlpha = Alpha;
	bStealthRendered = bRender;
	const bool bNowTrans = IsStealthTranslucencyActive();
	if (bNowTrans && !bWasTrans)
	{
		// Та же грабля, что у души/hide: идущий спавн-фейд держит слоты на своих MID — снимаем его до подмены.
		if (UAsdaFadeComponent* F = FindComponentByClass<UAsdaFadeComponent>()) { F->CancelFadeIn(); }
	}
	if (bNowTrans != bWasTrans) { TickSoulTranslucency(); return; }   // вход/выход причины — подмена/возврат слотов
	UpdateTranslucentAlphaOnly();                                         // α меняется каждый кадр затухания
}

// ★★★ GM-«скрыться»: ВТОРАЯ причина той же подмены слотов. Разбор — у объявления в заголовке.
void AAsdaPlayerCharacter::SetGmHideTranslucent(bool bOn)
{
	if (bGmHideTranslucent == bOn) { return; }
	bGmHideTranslucent = bOn;
	UE_LOG(LogAsdaPlayer, Log, TEXT("[GM-hide] %s: скрытие %s (полупрозрачность=%d, α=%.2f)"),
		*GetName(), bOn ? TEXT("ВКЛ — модель полупрозрачна") : TEXT("ВЫКЛ — материалы возвращаются"),
		CVarAsdaGmHideTranslucent.GetValueOnGameThread(), CVarAsdaGmHideAlpha.GetValueOnGameThread());
	if (bOn)
	{
		// Та же грабля, что у души: идущий спавн-фейд уже держит слоты на СВОИХ MID, и принять их
		//   за «исходники» нельзя — фейд снимаем целиком (см. `UAsdaFadeComponent::CancelFadeIn`).
		if (UAsdaFadeComponent* F = FindComponentByClass<UAsdaFadeComponent>()) { F->CancelFadeIn(); }
	}
	TickSoulTranslucency();   // применить/снять немедленно, не дожидаясь тика
	RefreshMoveClipAfterCrouchChange();   // ★ 24.10.2026: под GM-скрытием ход вприсядку (ключ asda.Crouch.HideAnim)
}

void AAsdaPlayerCharacter::SetSoulTranslucent(bool bOn)
{
	if (bSoulTranslucent == bOn) { return; }
	bSoulTranslucent = bOn;
	UE_LOG(LogAsdaPlayer, Log, TEXT("[душа] %s: режим SoulState==1 %s (полупрозрачность=%d)"),
		*GetName(), bOn ? TEXT("ВКЛ — модель α=0.3") : TEXT("ВЫКЛ — материалы возвращаются"),
		CVarAsdaSoulTranslucent.GetValueOnGameThread());
	if (bOn)
	{
		// ★ [ОРИГИНАЛ] Спавн-фейд у души не умножается: ветка `+2241 == 1` тика ставит ровно 0.3 и до
		//   ветки фейда (`alpha = fadeIn·fadeOut·visAlpha`) не доходит. Идущий фейд-ин (вход в мир /
		//   прибытие соседа — 6152 может прийти и после спавна) снимаем: иначе он подменил бы слоты
		//   своим MID, а мы приняли бы этот MID за «исходник».
		if (UAsdaFadeComponent* F = FindComponentByClass<UAsdaFadeComponent>()) { F->CancelFadeIn(); }
	}
	TickSoulTranslucency();   // применить/снять немедленно, не дожидаясь тика
}

void AAsdaPlayerCharacter::ApplySoulTranslucency()
{
	if (!SoulMaterialBase)
	{
		SoulMaterialBase = LoadObject<UMaterialInterface>(nullptr, kAsdaSoulMaterialPath);
		if (!SoulMaterialBase)
		{
			// Ассет не собран (скрипт не прогнан) — молчаливого «работает» не будет: кричим один раз за запуск.
			static bool bWarned = false;
			if (!bWarned)
			{
				bWarned = true;
				UE_LOG(LogAsdaPlayer, Error, TEXT("[душа] нет материала %s — прогнать Tools/asda_build_charlit_soul_mat.py; персонаж-душа останется непрозрачным"),
					kAsdaSoulMaterialPath);
			}
			return;
		}
	}
	// Чистим записи об уничтоженных компонентах (ClearWornParts при пересборке внешности).
	SoulSwaps.RemoveAll([](const FAsdaSoulSlotSwap& Sw) { return !Sw.Comp.IsValid(); });

	// ★★★ α зависит от ПРИЧИНЫ (душа 0.3 / GM-hide — ключ) и от значения ключа, которое владелец
	//   крутит на живом. Смена α не меняет набор слотов, поэтому основной цикл её не заметил бы:
	//   пере-записываем параметр во ВСЕ уже подменённые слоты отдельным проходом.
	const float Alpha = CurrentTranslucentAlpha();
	if (!FMath::IsNearlyEqual(Alpha, AppliedTranslucentAlpha))
	{
		for (FAsdaSoulSlotSwap& Sw : SoulSwaps)
		{
			if (Sw.Mid) { Sw.Mid->SetScalarParameterValue(FName(AsdaMat::kParamSoulOpacity), Alpha); }
		}
		AppliedTranslucentAlpha = Alpha;
	}

	// Охват = эталонные записи модели: тело (запись 0) + всё, что надела система сборки (записи 1..N:
	//   броня/перчатки/шлем/причёска/оружие — все несут `WornPartTag`). Транспорт, флаг клана, плашка,
	//   а также крылья/питомец/свечение (отдельные акторы) — вне охвата, как в эталоне.
	TArray<UMeshComponent*> Targets;
	if (BaseSkel) { Targets.Add(BaseSkel); }
	{
		TArray<UMeshComponent*> All;
		GetComponents<UMeshComponent>(All);
		for (UMeshComponent* MC : All)
		{
			if (MC && MC != BaseSkel && MC->ComponentHasTag(UAsdaCharAssembly::WornPartTag)) { Targets.Add(MC); }
		}
	}
	// ★★★ [ПОРТ] РАСШИРЕНИЕ ОХВАТА ТОЛЬКО ДЛЯ GM-HIDE. У «души» охват обязан остаться эталонным
	//   (транспорт и флаг клана — отдельные узлы очереди, α не получают), а у скрытого оператора
	//   непрозрачный конь под полупрозрачным всадником — очевидный дефект нашей же прозрачности:
	//   у оригинала под hide не рисуется вообще ничего. Слот без параметра `Tex` цикл ниже
	//   пропускает сам, поэтому добавление безопасно: чужие мастера не тронутся.
	if (IsGmHideTranslucencyActive())
	{
		if (MountMesh)    { Targets.AddUnique(MountMesh); }
		if (ClanFlagMesh) { Targets.AddUnique(ClanFlagMesh); }
	}
	// ★ Дедуп №2, В2-C (F9, 11.10.2026): обход и подмена слотов — общая точка `AsdaTranslucentSlots` (AsdaFadeComponent.h),
	//   та же, что у фейда. Для «души» поведение прежнее, бит в бит: тот же порядок проверок, то же имя `MIDSoul_<исходник>`,
	//   те же переносимые скаляры `kAsdaSoulCopiedScalars`.
	//   ★★★ ИМЯ MID ОБЯЗАНО НЕСТИ ИМЯ ИСХОДНИКА. Система сборки находит секции базового тела ПО ИМЕНИ
	//   материала слота: `ApplyFace` ищет «face», `HideBaseHair` — «hair», `AttachPart` — «chst/pant/
	//   shoe». Безымянный MID от `M_AsdaCharLitSoul` эти проверки сломал бы: при пересборке экипировки в
	//   режиме «душа» базовая секция под нагрудником осталась бы видимой, а лицо — не применилось.
	UMaterialInterface* const SoulBase = SoulMaterialBase;
	const int32 nNew = AsdaTranslucentSlots::Adopt(Targets, SoulSwaps,
		[this, SoulBase, Alpha](UMaterialInterface* Cur)
		{
			return AsdaTranslucentSlots::MakeSwapMid(Cur, SoulBase, this, TEXT("MIDSoul"), FName(AsdaMat::kParamSoulOpacity), Alpha,
				MakeArrayView(kAsdaSoulCopiedScalars));
		},
		[SoulBase](UMaterialInterface* Cur)
		{
			// Наш же MID (после пересоздания записи) — просто перепривязать; чужой translucent-MID
			//   фейда (`M_AsdaPreviewFade`) исходником считать нельзя — пропускаем, подхватим, когда
			//   фейд вернёт исходник (страховка: сам фейд у души уже не запускается).
			if (const UMaterialInstanceDynamic* CurMid = Cast<UMaterialInstanceDynamic>(Cur))
			{
				if (CurMid->Parent == SoulBase) { return true; }
				if (CurMid->Parent && CurMid->Parent->GetName() == AsdaMat::kPreviewFadeName) { return true; }   // ★ В1-C F10
			}
			return false;
		});
	if (nNew > 0)
	{
		UE_LOG(LogAsdaPlayer, Log, TEXT("[прозрачность] %s: подменено слотов %d (всего %d) на %s, α=%.2f (причина: %s)"),
			*GetName(), nNew, SoulSwaps.Num(), *SoulMaterialBase->GetName(), Alpha,
			IsSoulTranslucencyActive() ? (IsGmHideTranslucencyActive() ? TEXT("душа+GM-hide") : TEXT("душа"))
			                           : (IsGmHideTranslucencyActive() ? TEXT("GM-hide") : TEXT("невидимость навыка")));
	}
}

void AAsdaPlayerCharacter::RestoreSoulMaterials()
{
	int32 nBack = 0;
	for (FAsdaSoulSlotSwap& Sw : SoulSwaps)
	{
		UMeshComponent* Cm = Sw.Comp.Get();
		if (!Cm) { continue; }
		// Возвращаем исходник ТОЛЬКО если в слоте всё ещё наш MID — материал, поставленный внешним
		//   кодом поверх нашего (пересборка части), не затираем.
		if (Sw.Mid && Cm->GetMaterial(Sw.Slot) == Sw.Mid) { Cm->SetMaterial(Sw.Slot, Sw.Orig); ++nBack; }
	}
	if (SoulSwaps.Num() > 0)
	{
		UE_LOG(LogAsdaPlayer, Log, TEXT("[душа] %s: исходные материалы возвращены в %d из %d слотов"),
			*GetName(), nBack, SoulSwaps.Num());
	}
	SoulSwaps.Reset();
}

// ★★★ [ПОРТ] GM-«скрыться»: ПРИТУШИТЬ НАЛОЖЕННЫЕ ЭФФЕКТЫ.
//   Требование владельца: «полупрозрачным … как и все эффекты наложенные на него + оружие и доспехи».
//   Тело и экипировка — свап материалов слотов (см. ApplySoulTranslucency); эффекты — ДРУГАЯ система:
//   `AAsdaEffectActor` на `UProceduralMeshComponent` с Unlit-материалами ИЗ ДАННЫХ ЭМИТТЕРА и БЕЗ
//   единого MID. Держать их прозрачность в материале нельзя: пересборка секции ставит материал
//   эмиттера заново и снимает динамический экземпляр (`AsdaEffectActor.cpp`, ветка Create в
//   RebuildSection). Поэтому множитель живёт в САМОМ АКТОРЕ (`FxAlphaScale`) и умножает вершинную
//   альфу во всех четырёх каналах записи — частицы, кольцо, одиночный квад, .jem-меш (крылья).
//   Охват: все эффект-акторы, привязанные к нашему актору (`SpawnCharFx` делает `AttachToComponent`
//   к корню владельца) — сюда попадают крылья `WingFx`, свечение оружия/щита `WeaponGlowFx`/
//   `ShieldGlowFx`, кольца соулмейта и эффекты навыков/баффов.
//   ⚠ ВНЕ охвата: `AAsdaSoulGuardianActor` (хранитель — самостоятельный актор-компаньон, не
//   привязан к персонажу) и `MountMesh` (компонент транспорта, не эффект).
// ★★★ GM-«скрыться»: ПОГАСИТЬ/ВЕРНУТЬ ТЕНЬ. Возвращаем ровно тем компонентам, у которых мы её и
//   снимали (`GmHideShadowOff`), — иначе включили бы тень частям, у которых её выключил чужой код.
void AAsdaPlayerCharacter::ApplyGmHideShadow(bool bHiddenNow)
{
	const bool bWantOff = bHiddenNow && CVarAsdaGmHideCastShadow.GetValueOnGameThread() == 0;
	if (!bWantOff)
	{
		for (const TWeakObjectPtr<UMeshComponent>& W : GmHideShadowOff)
		{
			if (UMeshComponent* Cm = W.Get()) { Cm->SetCastShadow(true); }
		}
		if (GmHideShadowOff.Num() > 0)
		{
			UE_LOG(LogAsdaPlayer, Log, TEXT("[GM-hide] %s: тень возвращена %d компонент(ам)"), *GetName(), GmHideShadowOff.Num());
		}
		GmHideShadowOff.Reset();
		return;
	}
	TArray<UMeshComponent*> All;
	GetComponents<UMeshComponent>(All);
	for (UMeshComponent* Cm : All)
	{
		if (!Cm || !Cm->CastShadow) { continue; }   // уже без тени — не наша забота, возвращать нечего
		Cm->SetCastShadow(false);
		GmHideShadowOff.Add(Cm);
	}
	if (GmHideShadowOff.Num() > 0)
	{
		UE_LOG(LogAsdaPlayer, Log, TEXT("[GM-hide] %s: тень снята с %d компонент(ов)"), *GetName(), GmHideShadowOff.Num());
	}
}

void AAsdaPlayerCharacter::ApplyGmHideToAttachedFx(float Scale)
{
	// ★ РЕКУРСИВНО. `GetAttachedActors` по умолчанию отдаёт ТОЛЬКО прямых детей, а часть эффектов
	//   висит вторым ярусом: шлейф снаряда цепляется к актору снаряда, эффекты навыков — к цели
	//   (`AsdaSkillFxSubsystem.cpp:754`, `:922` — `AttachToActor` уже к другому эффекту). Без третьего
	//   аргумента такие «внуки» оставались бы непрозрачными и выдавали бы скрытого оператора.
	TArray<AActor*> Attached;
	GetAttachedActors(Attached, /*bResetArray*/true, /*bRecursivelyIncludeAttachedActors*/true);
	// ★ 01.11.2026 (ключ `asda.Gm.HideOwnedFx`): плюс эффекты-«дети по владельцу», не прикреплённые к персонажу.
	if (CVarAsdaGmHideOwnedFx.GetValueOnGameThread() != 0)
	{
		for (AActor* Ch : Children)
		{
			if (Cast<AAsdaEffectActor>(Ch) && IsValid(Ch)) { Attached.AddUnique(Ch); }
		}
	}
	int32 n = 0;
	for (AActor* A : Attached)
	{
		// ★ 01.11.2026 [ИСПРАВЛЕНИЕ] (soulmate-glow): α ставится абсолютной, поэтому собственный множитель эффекта показа
		//   навыка Дружбы (`asda.Soul.SkillGlowScale`) домножается здесь же — иначе выход из скрытия (Scale = 1) и секундный
		//   подхват под скрытием возвращали такому эффекту прежнюю полную яркость.
		if (AAsdaEffectActor* E = Cast<AAsdaEffectActor>(A)) { E->SetFxAlphaScale(Scale * AsdaSoulSkillGlowFactor(E)); ++n; }
	}
	if (n > 0 && !FMath::IsNearlyEqual(Scale, AppliedFxAlphaScale))
	{
		UE_LOG(LogAsdaPlayer, Log, TEXT("[GM-hide] %s: эффектов притушено %d, множитель α = %.2f"),
			*GetName(), n, Scale);
	}
	AppliedFxAlphaScale = Scale;
}

void AAsdaPlayerCharacter::TickSoulTranslucency()
{
	// ★ Причин две — «душа» (SoulState==1) и GM-«скрыться» (6005/6006); подмена слотов у них ОДНА.
	const bool bWant = IsTranslucencyActive();
	if (bWant != bSoulMaterialsApplied)
	{
		// Переход: вход в 1 / выход из 1 / переключение ключа отката — немедленно.
		bSoulMaterialsApplied = bWant;
		if (bWant) { ApplySoulTranslucency(); }
		else       { RestoreSoulMaterials(); AppliedTranslucentAlpha = -1.f; }
		FaceShownMode = -1;   // ★ 07.11.2026: материал слота лица сменился — текстуру режима поставить заново
		// Эффекты гасим ТОЛЬКО по причине GM-hide (у «души» эталон их не трогает). На выходе из
		//   состояния множитель обязан вернуться в 1 — поэтому вызов стоит в обеих ветках перехода.
		ApplyGmHideToAttachedFx(IsGmHideTranslucencyActive()
			? FMath::Clamp(CVarAsdaGmHideFxAlpha.GetValueOnGameThread(), 0.f, 1.f) : 1.f);
		ApplyGmHideShadow(IsGmHideTranslucencyActive());
		NextSoulAdoptS = GetWorld() ? GetWorld()->GetTimeSeconds() + 1.0 : 0.0;
		return;
	}
	if (!bWant || !GetWorld()) { return; }
	// Состояние постоянное (эталон ставит α каждый кадр) — раз в секунду подхватываем компоненты,
	//   появившиеся мимо `InitAppearance` (страховка; основной путь — вызов из `InitAppearance`).
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < NextSoulAdoptS) { return; }
	NextSoulAdoptS = Now + 1.0;
	ApplySoulTranslucency();
	// ★ Эффекты спавнятся и уничтожаются на ходу (свечение при смене оружия, ауры баффов), поэтому
	//   множитель раздаётся заново тем же секундным подхватом, что и слоты материалов.
	if (IsGmHideTranslucencyActive())
	{
		ApplyGmHideToAttachedFx(FMath::Clamp(CVarAsdaGmHideFxAlpha.GetValueOnGameThread(), 0.f, 1.f));
	}
	// Части экипировки приходят пакетами после спавна — тень с новых снимаем тем же подхватом.
	ApplyGmHideShadow(IsGmHideTranslucencyActive());
}

// ★ Живая проверка без входа душой (кнопка сцены выбора ещё не шлёт 1016 режимом 1):
//   `asda.Soul.ForceTranslucent 1|0` — взвести/снять режим у СВОЕГО персонажа напрямую.
//   Ровно тот же путь, что у сетевого SoulState==1 (`SetSoulTranslucent`), без плашки/гейта.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaSoulForceTranslucent(
	TEXT("asda.Soul.ForceTranslucent"),
	TEXT("Проверка вида «души»: asda.Soul.ForceTranslucent 1|0 — полупрозрачность α=0.3 на своём персонаже"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		const bool bOn = Args.Num() == 0 || Args[0] != TEXT("0");
		const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		AAsdaPlayerCharacter* Me = WS ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get()) : nullptr;
		if (!Me) { UE_LOG(LogAsdaPlayer, Warning, TEXT("asda.Soul.ForceTranslucent: своего персонажа в мире нет")); return; }
		Me->SetSoulTranslucent(bOn);
		UE_LOG(LogAsdaPlayer, Warning, TEXT("asda.Soul.ForceTranslucent: %s (ключ asda.Soul.Translucent=%d)"),
			bOn ? TEXT("ВКЛ") : TEXT("ВЫКЛ"), CVarAsdaSoulTranslucent.GetValueOnGameThread());
	}));
#endif // ASDA_DEV_CONSOLE
