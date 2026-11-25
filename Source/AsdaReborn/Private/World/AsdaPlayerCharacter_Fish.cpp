// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPlayerCharacter_Fish.cpp — ★ 23.10.2026 часть AAsdaPlayerCharacter, вынесенная из AsdaPlayerCharacter.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaPlayerCharacter.plan.json).
//   Танцпол Alpen, следы и звук шагов, консоль клипа «сесть», рыбалка (действия 60…67, удочка, рыба), добыча лопатой.
//   #include — те же, что у AsdaPlayerCharacter.cpp, в том же порядке; общее — AsdaPlayerCharacterInternal.h.
#include "World/AsdaPlayerCharacter.h"
#include "AsdaNetSubsystem.h"   // ★ R1: прямое включение (раньше приходило транзитивно)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "World/AsdaPlayerController.h"   // ★ ритм ударов задаёт тик анимации (OnSwingCycleCompleted)
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
#include "AsdaFxNames.h"               // ★ дедуп №2 В1-C (F6): теги эффектов персонажа + общее гашение по тегу
#include "Resources/AsdaMaterialNames.h"   // ★ дедуп №2 В1-C (F10): пути мастеров и имена параметров
#include "World/AsdaWalkGrid.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
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
#include "AsdaPlayerCharacterInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaPlayerCharacter*.cpp (LogAsdaPlayer, чистые хелперы)


// Клип флага (эталон mapobj_updateFlagLinkAnim@0xFB0CB0):
//   • состояние персонажа == 1 (движение) → клип 2 (small_flag002);
//   • вышли из движения, когда играл клип 2 → сразу клип 0;
//   • иначе смена ТОЛЬКО по концу текущего клипа (Anim_IsClipFinished: time >= length):
//     rand()%10 <= 5 → клип 1, иначе клип 0 (то есть 60/40 между двумя вариантами покоя).
// ═════════════════════════ ★★★ ТАНЦПОЛ В ALPEN ═══════════════════════════════════════════════
//  Единственная зона такого рода в игре, и она захардкожена в клиенте: сервер о ней не знает.
//  Эталон `sub_A220E0` (`render_core.c:3546-3564`; имя `Camera_UpdateListener` — миснейм) проверяет
//  зону каждый кадр по метке `LABEL_128`, общей для состояний покоя 0/5/6/7/8/9/0xD:
//
//      if (mapId == 3 /* Alpen */ && внутри прямоугольника)  CCharacter+0x33090 = 1;  else 0;
//
//  Потребитель — `CGameUI_UpdateFadeAnim@0xA17B00` (`game_main.c:8217-8241`): при взведённом флаге
//  вместо клипа покоя берётся `41 + (rand()%10 <= 5)`, то есть танец 1 или 2, клип играется один раз
//  и уходит пакет 4024 с кодом 112 или 113. Сервер (`CharEMotionSync@0x546730`) рассылает 4025 —
//  танец видят все вокруг. Клип кончился → снова покой → всё повторяется, пока стоишь на полу.
//
//  ★ Почему этой зоны долго «не было»: у механизма нет ни одной строки, а номер состояния
//  собирается арифметикой (41 + вариант), поэтому поиск и по слову «dance», и по числам 41/42
//  проходил мимо. Разбор: `Reports/alpen_dancefloor_trigger.md`.
//
//  Границы — из `.rdata` эталона (`0x13AE5B0..0x13AE5BC`), мировые координаты игры.
void AAsdaPlayerCharacter::TickAutoDanceZone(float DeltaSeconds)
{
	// Прямоугольник танцпола Alpen (мировые координаты игры; тайлы 151.4..167.6 / 368.5..381.7).
	constexpr float kZoneMinX = 15136.f, kZoneMaxX = 16758.f;
	constexpr float kZoneMinZ = 36847.f, kZoneMaxZ = 38172.f;
	constexpr int32 kAlpenMapId = 3;               // эталон: `getField3036(&g_pLocalPlayer) == 3`

	if (AutoDanceCooldown > 0.f) { AutoDanceCooldown -= DeltaSeconds; }

	// Зона работает только у своего персонажа: соседи танцуют по приходящему 4025, как и в эталоне.
	if (!IsLocalPlayerCharacter()) { return; }

	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bOnAlpen = (Net && Net->EnterMapId == kAlpenMapId);

	const FVector2D P = GetGamePosXZ();
	const bool bInside = bOnAlpen
		&& P.X > kZoneMinX && P.X < kZoneMaxX
		&& P.Y > kZoneMinZ && P.Y < kZoneMaxZ;

	if (bInside != bInAutoDanceZone)
	{
		bInAutoDanceZone = bInside;
		UE_LOG(LogAsdaPlayer, Log, TEXT("[танцпол] игрок %s зону (%.0f, %.0f)"),
			bInside ? TEXT("вошёл в") : TEXT("покинул"), P.X, P.Y);
		if (!bInside) { AutoDanceCooldown = 0.f; }
	}
	if (!bInside) { return; }

	// Танцуем только из покоя: бег, сидение, смерть и замах зону перебивают — как в эталоне,
	// где флаг читается лишь на ветке состояний покоя.
	if (AnimState != EAsdaPlayerState::Idle) { return; }
	// ★ В5: действие рыбалки 60…67 — не ветка покоя; ★ 10.10.2026: копание 40/43 — тоже.
	// ★ ИСПРАВЛЕНИЕ (В3 S7, 12.10.2026; ключ отката `asda.Action.LegacyBusyGates 1`). Эталон читает флаг танцпола только в
	//   ветке состояния 0 (`CGameUI_UpdateFadeAnim`, тик `case 0`, game_main.c:8222) — ЛЮБОЕ «занятое» состояние её не пускает.
	//   Было: гейт знал только рыбалку и лопату — в 68 «добыча» танец запускался поверх (эмоция переписывала 68 и уходил 4024
	//   112/113), а во время своей эмоции раз в секунду шла попытка с отказом и строкой «клип не проигран».
	//   Стало: любой ненулевой `GetBusyActionCode` (эмоция, пока идёт клип; 40/43; 60…67; 68) — не ветка покоя.
	if (IsLegacyBusyGates() ? (IsFishActionActive() || GetEtalonShovelState() != 0) : IsEtalonActionBusy()) { return; }
	if (AutoDanceCooldown > 0.f) { return; }       // текущий клип ещё играет

	// Эталон `CGameUI_UpdateFadeAnim` (game_main.c:8222-8233): `rand() % 10 <= 5` → вариант 1 → состояние 42, код 113;
	//   иначе вариант 0 → 41, код 112. То есть ЧАЩЕ (6 из 10) выпадает ТАНЕЦ 2.
	//   ★ 27.09.2026 (pkt4024_facing.md): до правки условие было перевёрнуто (`> 5`, танец 2 — 4 из 10).
	const bool  bSecond    = (FMath::RandRange(0, 9) <= 5);
	const int32 EmoteState = bSecond ? 42 : 41;
	const int32 ActionCode = bSecond ? 113 : 112;

	const float Len = PlayEmotionClip(EmoteState);
	if (Len <= 0.f)
	{
		// Клипа нет — не спамим ни пакетами, ни логом: ждём секунду и пробуем снова.
		AutoDanceCooldown = 1.f;
		UE_LOG(LogAsdaPlayer, Warning, TEXT("[танцпол] клип состояния %d не проигран"), EmoteState);
		return;
	}
	AutoDanceCooldown = Len;

	// ★ 27.09.2026 (pkt4024_facing.md): тело +5/+9 — НАПРАВЛЕНИЕ взгляда, не позиция: эталон берёт
	//   `thunk_copyVec3Field5(pGame + 47718)` (game_main.c:8235) — то же поле, что `charComputeFacingAngleFromDir`.
	if (Net)
	{
		const FVector2D Face = GetFacingDirGame();   // единичный курс (x, z)
		Net->SendCharStatePos4024(ActionCode, (float)Face.X, (float)Face.Y);
	}
	UE_LOG(LogAsdaPlayer, Verbose, TEXT("[танцпол] танец %d (состояние %d), клип %.2f с"),
		bSecond ? 2 : 1, EmoteState, Len);
}

// Сколько живёт облачко/всплеск из-под ноги. Общий расчёт эффектов держит минимум 1 с и максимум
// 30 с — для следа это слишком долго, он должен вспыхнуть и почти сразу погаснуть.
static TAutoConsoleVariable<float> CVarFootstepFxLife(
	TEXT("asda.Footstep.FxLife"), 0.45f,
	TEXT("Предельное время жизни следа от шага, секунды (по умолчанию 0.45)."),
	ECVF_Default);

// ═══════════════════ ★ СЛЕДЫ И ЗВУК ШАГОВ ПО МАТЕРИАЛУ ПОВЕРХНОСТИ ══════════════════════════
//  Эталон `CCharacter_ApplyMoveState@0xA199E0` (`entity_char.c:21014-21080`): пока персонаж бежит,
//  дважды за цикл клипа — на фазе 300 и 600 — он смотрит, по чему бежит, и роняет след со звуком.
//  Материал берётся из БАЙТА 0 флага тайла `.TER` (тот самый, что у нас числился «мёртвым полем»,
//  пока не нашёлся его читатель за тремя обёртками). Повтор в том же цикле не даёт пара флагов
//  0x400/0x800 — у нас это `FootstepDoneMask`.
//
//  Разделение эталона сохранено: ЗВУК слышит только владелец персонажа (в оригинале гейт
//  `effectSlots[112].field_40`), а СЛЕД видят все. Материал 1 (вода) роняет всплеск лишь тогда,
//  когда нога действительно над водой — иначе ни следа, ни звука.
//
//  Разбор: `Reports/alpen_dancefloor_trigger.md` §5.
void AAsdaPlayerCharacter::TickFootsteps(float DeltaSeconds)
{
	// Пороги фазы и длина цикла — из эталона (сравнения с 300.0 и 600.0).
	constexpr float kStep1 = 300.f, kStep2 = 600.f, kCycle = 900.f;

	// Шаги только на бегу. Всё прочее — покой, сидение, смерть, транспорт — сбрасывает цикл:
	// у всадника шаги не звучат вовсе (эталон гейтит их проверкой транспорта).
	// ★ И ПОД КРЫЛЬЯМИ ТОЖЕ: с надетым крылом Plus Package персонаж не бежит, а летит —
	//   бег-клип целиком подменяется треком взмахов (эталон `charUpdateActionStateTick@0xA216B0`),
	//   ноги земли не касаются, поэтому ни следа, ни звука шага быть не должно.
	if (AnimState != EAsdaPlayerState::Move || bMounted || bWingsOn)
	{
		FootstepPhase = 0.f; FootstepDoneMask = 0;
		return;
	}
	// ★ 28.10.2026 [ПОРТ] (плавание): плывущий шагов не делает — его след и звук дают брызги плавания (`SpawnSwimSplash`).
	if (bSwimming)
	{
		FootstepPhase = 0.f; FootstepDoneMask = 0;
		return;
	}

	// Фаза цикла в мс. Эталон берёт её из проигрывателя анимации; здесь считаем временем — при
	// одинаковой длине клипа это то же самое, а привязки к внутренностям аниматора не требует.
	FootstepPhase += DeltaSeconds * 1000.f;
	if (FootstepPhase >= kCycle) { FootstepPhase -= kCycle; FootstepDoneMask = 0; }

	const uint8 Which = (FootstepPhase >= kStep2) ? 2 : ((FootstepPhase >= kStep1) ? 1 : 0);
	if (Which == 0) { return; }
	const uint8 Bit = (Which == 1) ? 1 : 2;
	if (FootstepDoneMask & Bit) { return; }        // этот шаг в текущем цикле уже сыгран
	FootstepDoneMask |= Bit;

	const FVector2D P = GetGamePosXZ();
	int32 Material = WalkGrid ? WalkGrid->GetWorldStepMaterial((float)P.X, (float)P.Y) : 0;
	// ★ 28.10.2026 [ПОРТ] (открытая вода): тайлы воды, бывшие блоком, несут материал суши (в эталоне по ним не ходили) —
	//   бредущему по мелководью (вода выше ног, но ниже порога плавания) нужен всплеск, а не пыль. Вопрос к самой воде:
	//   поверхность `.WAT` над точкой выше ног. Только на открытых тайлах (`SwimBits`) — прежние тайлы как раньше.
	if (Material != 1 && WalkGrid && AsdaSwimOpenWaterOn()
		&& WalkGrid->IsTileSwimOpen(FMath::FloorToInt((float)P.X / 100.f), FMath::FloorToInt((float)P.Y / 100.f)))
	{
		const FVector L = GetActorLocation();
		const float Wz = AsdaGround::WaterSurfaceZ(GetWorld(), (float)L.X, (float)L.Y);
		if (FMath::IsFinite(Wz) && Wz > (float)L.Z) { Material = 1; }
	}

	const bool bMine = IsLocalPlayerCharacter();
	UGameInstance* GI = GetGameInstance();
	UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr;

	auto PlayStepSound = [&](const TCHAR* Name)
	{
		if (!Snd)
		{
			UE_LOG(LogAsdaPlayer, Warning, TEXT("[шаг] звуковой подсистемы нет — «%s» не сыгран"), Name);
			return;
		}
		if (bMine) { Snd->PlaySound2D(Name); }
		else       { Snd->PlaySound3D(Name, GetActorLocation()); }
		UE_LOG(LogAsdaPlayer, Log, TEXT("[шаг] звук «%s» (%s)"), Name, bMine ? TEXT("свой, 2D") : TEXT("чужой, 3D"));
	};
	auto DropFx = [&](const TCHAR* JebName)
	{
		UWorld* W = GetWorld();
		if (!W) { return; }
		AAsdaEffectActor* E = AAsdaEffectActor::SpawnByJebName(W, JebName, GetActorLocation(),
			FRotator::ZeroRotator, nullptr);
		// ★ Время жизни следа. Общий расчёт `NaturalLifeSpan` держит нижнюю границу в 1 с и верхнюю
		//   в 30 с — для облачка из-под ноги это ЦЕЛАЯ ВЕЧНОСТЬ: следы копились и тянулись за
		//   персонажем. След обязан вспыхнуть и почти сразу погаснуть, поэтому здесь свой, короткий
		//   клапан: живём по эмиттерам, но не дольше `asda.Footstep.FxLife` секунд.
		if (E)
		{
			const float Cap = FMath::Max(0.05f, CVarFootstepFxLife.GetValueOnGameThread());
			E->SetLifeSpan(FMath::Min(E->NaturalLifeSpan(/*Pad*/0.1f, /*Min*/0.1f, /*Max*/Cap), Cap));
		}
	};

	UE_LOG(LogAsdaPlayer, Log, TEXT("[шаг] %s, материал %d, фаза %.0f мс, поз=(%.0f,%.0f)"),
		(Which == 1) ? TEXT("первый") : TEXT("второй"), Material, FootstepPhase, P.X, P.Y);

	switch (Material)
	{
	case 0:   // земля: облачко пыли
		DropFx(TEXT("eff_1_char_99_move01-a_00"));
		PlayStepSound(TEXT("S_c_run1"));
		break;
	case 1:   // вода: всплеск — но только если нога действительно над водой
		// Эталон вдобавок к материалу тайла спрашивает саму геометрию воды (`sub_926A07` →
		// `pickElems_testAnyHit`): водный тайл может быть перекрыт мостиком или помостом,
		// и тогда всплеска быть не должно. Проверка ставит тот же вопрос — накрыта ли точка
		// водной геометрией (сравнения высот в оригинале нет, важен горизонтальный охват).
		if (WalkGrid && WalkGrid->IsWorldOverWater((float)P.X, (float)P.Y))
		{
			DropFx(TEXT("eff_1_char_99_move02-a_00"));
			PlayStepSound(TEXT("S_c_run0"));
		}
		break;
	case 2:  PlayStepSound(TEXT("S_c_run2")); break;
	case 3:  PlayStepSound(TEXT("S_c_run3")); break;
	case 4:  PlayStepSound(TEXT("S_c_run0")); break;
	default: PlayStepSound(TEXT("S_c_run1")); break;
	}
}

void AAsdaPlayerCharacter::TickClanFlagAnim(float DeltaSeconds)
{
	if (!ClanFlagMesh) { return; }
	if (bMoving)
	{
		if (ClanFlagClip != 2) { PlayClanFlagClip(2); }
		return;
	}
	if (ClanFlagClip == 2) { PlayClanFlagClip(0); return; }
	ClanFlagClipLeft -= DeltaSeconds;
	if (ClanFlagClipLeft <= 0.f)
	{
		PlayClanFlagClip(FMath::RandRange(0, 9) <= 5 ? 1 : 0);
	}
}


// ══════════ КОНСОЛЬНАЯ ПРОВЕРКА СЕГМЕНТОВ КЛИПА «СЕСТЬ/СИДИТ/ВСТАТЬ» ═════════════════════════════
// Проверяет ДАННЫЕ, а не картинку: что `anim_15` — это действительно `sitdown01` нужной длины и что
//   хвост «встаёт» (от 3500 мс у мужчин / 4000 у женщин до конца) НЕ пуст. Именно пустой хвост —
//   единственный случай, когда порт был бы вынужден встать «рывком».
// Сама раскладка сегментов доказана в `Reports/char_info_fix2.md` §3.1 (эталон + заголовок .jbb).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaSitClipInfo(
	TEXT("asda.Player.SitClipInfo"),
	TEXT("Длина клипа sitdown01 (anim_15) и границы сегментов «садится/сидит/встаёт» для обоих полов."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld*)
	{
		struct FRow { const TCHAR* Who; const TCHAR* Base; float DownMs; float LoopMs; };
		// ★ 02.09.2026: базы путей — из общей точки (закон `port-shared-points`), а не двумя копиями литерала.
		const FRow Rows[2] = {
			{ TEXT("муж"), kAsdaCharPreviewBaseM,
			  AAsdaPlayerCharacter::kSitDownEndMsMale,   AAsdaPlayerCharacter::kSitLoopEndMsMale },
			{ TEXT("жен"), kAsdaCharPreviewBaseF,
			  AAsdaPlayerCharacter::kSitDownEndMsFemale, AAsdaPlayerCharacter::kSitLoopEndMsFemale },
		};
		for (const FRow& R : Rows)
		{
			const FString Path = FString::Printf(TEXT("%sanim_%02d"), R.Base, AAsdaPlayerCharacter::kSitClipAsset);
			UAnimSequence* Clip = UAsdaModelAnimSubsystem::LoadClipChecked(Path);
			if (!Clip)
			{
				UE_LOG(LogAsdaPlayer, Error, TEXT("[сидение] %s: клип %s НЕ НАЙДЕН"), R.Who, *Path);
				continue;
			}
			const float LenMs = Clip->GetPlayLength() * 1000.f;
			const float TailMs = LenMs - R.LoopMs;
			UE_LOG(LogAsdaPlayer, Log,
				TEXT("[сидение] %s: %s длина %.1f мс | садится 0…%.0f | сидит %.0f…%.0f (цикл) | встаёт %.0f…%.1f (%.1f мс) — %s"),
				R.Who, *Clip->GetName(), LenMs, R.DownMs, R.DownMs, R.LoopMs, R.LoopMs, LenMs, TailMs,
				(TailMs > 20.f) ? TEXT("ХВОСТ ЕСТЬ") : TEXT("!! ХВОСТА НЕТ — вставание будет рывком"));
		}
	}));
#endif // ASDA_DEV_CONSOLE

// ═══════════════════════ ★★★ РЫБАЛКА: ДЕЙСТВИЯ 60…67, УДОЧКА, РЫБА (волна В5, 08.10.2026) ═══════════════════════
//   Реверс: `Reports/owner_1008/fishing_parts/world_anim.md` §2-§3; отчёт волны `fishing_impl_W5.md`.

// [ИСПРАВЛЕНИЕ] новая удочка берёт клип ТЕКУЩЕГО действия. Эталон `fishingInitRodModel` (`sub_FCDED0`) всегда ставит
//   клип 0 («заброс»), а клип действия выставляется только существующей удочке (`sub_935007` в `CChar_SetState`,
//   game.c:77089 — у чужого он зовётся ДО вставки удочки в `CharManager_UpdateAll`). Итог оригинала: рыбак, появившийся
//   в видимости (6173 фаза 1 → действие 61), стоит в ожидании, а его удочка по кругу «забрасывает».
static ASDA_DEV_CVAR(int32, CVarFishLegacyRodClip0,
	TEXT("asda.Fish.LegacyRodClip0"), 0,
	TEXT("1 = как оригинал: вставленная удочка рыбалки всегда начинает с клипа 0 (заброс), даже если персонаж уже в ")
	TEXT("действии 61…67; 0 (по умолчанию) = клип текущего действия − 60 [ИСПРАВЛЕНИЕ]."),
	ECVF_Default);
// Диагностика (самотест `asda.Fish.AnimSelfTest`, ревизия В5 МЕЛК-5): действие N считать «клип не запустился» —
//   проверка страховки моста своего автомата. 0 — выключено (штатно).
static ASDA_DEV_CVAR(int32, CVarFishDebugFailClip,
	TEXT("asda.Fish.DebugFailClip"), 0,
	TEXT("Диагностика: действие рыбалки N (60…67) считать «клип не найден» — проверка страховки моста. 0 = штатно."),
	ECVF_Cheat);
// [ИСПРАВЛЕНИЕ] рыба на крючке без удочки скрыта. Эталон `AttachedObject_UpdateAndRender@0xFD1B30` (game.c:609634) ставит
//   `RenderCtx_SetEnable(рыба, 1)` В НАЧАЛЕ цикла, а позу и видимость по действию (62/65 и [15066]) пересчитывает только
//   при найденной удочке (`findChildByMemberId`) — без неё рыба рисуется в устаревшей позе при любом действии (дефект
//   оригинала: удочка не вставилась — нет img-меша, или уже снята). Ревизия В5, МЕЛК-4.
static ASDA_DEV_CVAR(int32, CVarFishLegacyHookNoRod,
	TEXT("asda.Fish.LegacyHookNoRod"), 0,
	TEXT("1 = как оригинал: рыба на крючке без удочки видна всегда, в последней посчитанной позе; ")
	TEXT("0 (по умолчанию) = без удочки рыба скрыта [ИСПРАВЛЕНИЕ]."),
	ECVF_Default);
// [ПОРТ] модели удочки `itm_n_Fishrod_01.jbm` (фолбэк эталона) нет ни в одном клиенте (отчёт В5а §2) — берём fird_00.
static const TCHAR* const kFishRodFallbackMesh = TEXT("ch1_n_00_fird_00_00_0_00");
// Кость персонажа, к которой крепится кость `8a1_dummy` удочки (`CharFx_UpdateAttachedEffects@0xFCE3A0`, game.c:607927).
static const FName kFishCharHandBone(TEXT("8_a1"));
// Кость удочки, совмещаемая с рукой.
static const FName kFishRodDummyBone(TEXT("8a1_dummy"));
// Кончик лески удочки — сюда кость `fs_Bone01` рыбы на действии 62 (`AttachedObject_UpdateAndRender@0xFD1B30`).
static const FName kFishRodTipBone(TEXT("Bone24"));
// Кость рыбы.
static const FName kFishHookBone(TEXT("fs_Bone01"));
// Кость персонажа для рыбы «в руке» на действии 65 (там же, ветка `animSlotIdx == 32`).
static const FName kFishCharOffHandBone(TEXT("_8_a1"));

// Поворот рыбы «в руке» (действие 65): эталон `thunk_callVtbl15B600C(m, −1.5707951)` = D3DXMatrixRotationZ(−π/2)
//   (указатели 0x15B6004/8/C = RotX/RotY/RotZ — 0x15B600C ведёт в 0xFF4C76 → реализация 0xFF4CED с рядами
//   [c s 0][−s c 0][0 0 1], второй метод — байты). Перевод в оси кости UE тем же Q̂ = (x, −z, y), что у оружия за спиной
//   (`UAsdaCharAssembly::WeaponStowAttachRaw`): RotZ(−90°) game ⇒ (u1,u2,u3) → (u3, u2, −u1) = поворот вокруг UE-Y на +90°.
static FMatrix AsdaFishInHandRot()
{
	return FMatrix(FPlane(0.f, 0.f, -1.f, 0.f), FPlane(0.f, 1.f, 0.f, 0.f), FPlane(1.f, 0.f, 0.f, 0.f), FPlane(0.f, 0.f, 0.f, 1.f));
}

float AAsdaPlayerCharacter::PlayFishAction(int32 Action, bool bSelf)
{
	UWorld* W = GetWorld();
	if (!W) { return 0.f; }
	if (Action == 0) { StopFishAction(TEXT("состояние 0")); return 0.f; }
	if (Action < 60 || Action > 67) { return 0.f; }
	// ★ fishfix2 09.10 (ревизия fishing_fixes_REVIEW СРЕДН-1) [ПОРТ] исправление дефекта оригинала: у трупа (своего или
	//   соседа по 4025 122…129 / 6173) клип рыбалки не ставится — иначе тело «оживает» и застывает стоя (конец действия у
	//   мёртвого клип не меняет). Своему проход автомата доводит страховка по таймеру .jbb. Ключ `asda.Fish.LegacyDeadClip 1`.
	if (AnimState == EAsdaPlayerState::Dead && !UAsdaFishingSubsystem::IsLegacyDeadClip())
	{
		UE_LOG(LogAsdaPlayer, Log, TEXT("[рыбалка] %s мёртв: действие %d — клип на труп не ставим"), bSelf ? TEXT("свой") : TEXT("чужой"), Action);
		if (bSelf)
		{
			if (UAsdaFishingSubsystem* Fish = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaFishingSubsystem>() : nullptr)
			{
				Fish->ArmClipBridgeFallback(Action, TEXT("свой персонаж мёртв"));
			}
		}
		return 0.f;
	}
	// ── `CChar_SetState(…, 60…67)` ПЕРЕПИСЫВАЕТ состояние: движение (1), сидение (7/8/9), эмоция (33…46),
	//    замах (5) прерываются без собственного клипа — ставится клип действия. Движение снимаем «на месте»
	//    (без телепорта и без клипа покоя — его сразу сменит клип действия).
	//    ★ В45fix2 (МЕЛК-5): и состояние 68 «добыча»; ★ 09.10.2026 (п. 5 владельца): и лопату 40/43.
	//    ★ В3 S7: одна преамбула (`StopMotionForAction`) и одна запись поля (`SetBusyAction` снимает эмоцию/68/лопату).
	BeginEtalonAction((EAsdaActionState)Action, bSelf ? TEXT("рыбалка, свой") : TEXT("рыбалка, чужой"));
	bFishActionSelf = bSelf;
	// ── клип удочки = действие − 60 (`sub_935007` / `attachedObjectSetField464ById@0xFCE050`, game.c:77089 и 604302) ──
	SetFishRodClip(Action - 60);
	// ── эффект fish_end у ЧУЖОГО — при ВХОДЕ в 65/66, защита от повтора [15100] (game.c:77092-77131).
	//    Свой показывает его из `Fishing_OnStateMessage` 0x1D (подсистема рыбалки) — здесь не дублируем.
	if (!bSelf && FishFxLastAction != Action)
	{
		FishFxLastAction = Action;
		if (Action == 65) { SpawnFishEndFx(TEXT("fish_end_01")); }
		if (Action == 66) { SpawnFishEndFx(TEXT("fish_end_02")); }
	}
	// ★★★ ПОРЯДОК (как у эмоций): признак действия → уборка оружия (меняет набор клипов) → запуск клипа.
	UpdateWeaponMotionHidden();
	const int32 Idx = kFishClipBase + (Action - 60);
	const bool bDebugFail = CVarFishDebugFailClip.GetValueOnGameThread() == Action;
	const float Len = bDebugFail ? 0.f : PlayWorldClip(Idx, /*bLoop*/false);
	if (Len <= 0.f)
	{
		UE_LOG(LogAsdaPlayer, Warning, TEXT("[рыбалка] действие %d: клип anim_%d %s"), Action, Idx,
			bDebugFail ? TEXT("сорван ключом asda.Fish.DebugFailClip (диагностика)") : TEXT("не найден"));
		SetBusyAction(EAsdaActionState::Idle, TEXT("клипа рыбалки нет"));
		UpdateWeaponMotionHidden();
		// ★ В5fix (ревизия В5, МЕЛК-5): своему — страховка от «вечного ожидания конца клипа» (закон «молчание = вечный
		//   тупик»): автомат рыбалки сам доводит этот проход по таймеру длительности .jbb (мост В1 только для действия). [ПОРТ]
		if (bSelf)
		{
			if (UAsdaFishingSubsystem* Fish = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaFishingSubsystem>() : nullptr)
			{
				Fish->ArmClipBridgeFallback(Action, TEXT("клип персонажа не запустился"));
			}
		}
		return 0.f;
	}
	FishClipStartedS = W->GetTimeSeconds();
	FishClipAsset = (BaseSkel && BaseSkel->GetSingleNodeInstance())
		? Cast<UAnimSequence>(BaseSkel->GetSingleNodeInstance()->GetAnimationAsset()) : nullptr;
	W->GetTimerManager().SetTimer(FishClipTimer, this, &AAsdaPlayerCharacter::OnFishClipEnd, Len, false);
	IdleTimer = 0.f; FidgetRemaining = 0.f;
	UE_LOG(LogAsdaPlayer, Log, TEXT("[рыбалка] %s: действие %d → клип anim_%d (%.2f с), удочка клип %d"),
		bSelf ? TEXT("свой") : TEXT("чужой"), Action, Idx, Len, GetFishRodClip());
	return Len;
}

void AAsdaPlayerCharacter::StopFishAction(const TCHAR* Why)
{
	if (!IsFishActionActive()) { return; }
	const int32 Was = GetFishAction();
	SetBusyAction(EAsdaActionState::Idle, Why);   // ★ В3 S7: таймер и сторож клипа снимает единая запись
	UpdateWeaponMotionHidden();   // признак «занят» снят — оружие из-за спины (если зона позволяет)
	UE_LOG(LogAsdaPlayer, Log, TEXT("[рыбалка] действие %d снято (%s)"), Was, Why);
	if (IsAttackAnimActive()) { return; }
	switch (AnimState)
	{
	case EAsdaPlayerState::Move: PlayWorldClip(MoveClip(), true); break;
	case EAsdaPlayerState::Sit:  ResumeSitIdle(); break;
	case EAsdaPlayerState::Dead: break;
	default:
		PlayWorldClip(IdleClipIdx(), true);
		IdleTimer = 0.f; FidgetRemaining = 0.f;
		break;
	}
}

// ★ В45fix2 (ревизия В4/В5, МЕЛК-3, 09.10.2026): `CChar_SetState(…, 9)` — «встаёт», 4025 109 (`Recv_CharSpawnFx`
//   g:551808-551809). Прежде 109 шёл как 0 (`SetAnimState(Idle)`), и у рыбачащего соседа удочка снималась сразу; у эталона
//   состояние 9 переписывает 60…67 (запись безусловна, g:76456), кадр клипа sitdown01 ставится на хвост вставания (трек 3,
//   3504/4004 мс, g:76869-76878), `bActionBusy = 0`; флаг [15036] (а с ним удочку) эталон снимает только в ≤ 2/5/6
//   (g:607922) — т.е. после конца клипа 9, когда состояние станет 0. [ОРИГИНАЛ]
//   Сидение, замах и смерть идут прежним путём `SetAnimState(Idle)`: из сидения он уже ставит хвост вставания (`BeginStandUp`),
//   замах — гейт состояний 13/14 порта, смерть — без изменений.
void AAsdaPlayerCharacter::EnterStandUpState()
{
	if (AnimState == EAsdaPlayerState::Sit || SitPhase != 0 || IsAttackAnimActive() || AnimState == EAsdaPlayerState::Dead)
	{
		SetAnimState(EAsdaPlayerState::Idle);
		return;
	}
	// ★ 09.10.2026 (п. 5 владельца): «встаёт» (9) переписывает и лопату 40/43, и 68, и эмоцию, и рыбалку.
	// ★ В3 S7: одна запись единого поля (прежде — 68, лопата, эмоция и рыбалка по отдельности).
	const int32 WasFish = GetFishAction();
	SetBusyAction(EAsdaActionState::Idle, TEXT("встаёт (9)"));
	AnimState = EAsdaPlayerState::Idle;
	UpdateWeaponMotionHidden();   // `bActionBusy = 0` — оружие из-за спины (если зона позволяет)
	if (BeginStandUp() <= 0.f)
	{
		PlayWorldClip(IdleClipIdx(), true);   // хвоста нет — сразу покой (как ветка сидения)
		IdleTimer = 0.f; FidgetRemaining = 0.f;
	}
	UE_LOG(LogAsdaPlayer, Log, TEXT("[встать] состояние 9 по 4025 109 (действие рыбалки %d снято; удочка — по концу вставания)"), WasFish);
}

// ★ 10.10.2026 (копание): ключ отката клипа копания (anim_233 / anim_234) — разбор у `EnterEtalonShovelState`.
static ASDA_DEV_CVAR(int32, CVarAsdaShovelClip,
	TEXT("asda.Shovel.Clip"), 1,
	TEXT("Копание 40/43: 1 (по умолчанию) = как оригинал — клип копания (трек 17 `ch4_98_Shoveling_{m|f}` / трек 20 `…_01`) ")
	TEXT("с ударами по порогам combo time; 0 = прежний порт — клип покоя (удары, звук и эффекты ударов идут по треку всё равно)."),
	ECVF_Default);
// ★ 10.10.2026 (копание): ключ исправления выбора меша инструмента — разбор у `EnterEtalonShovelState`, п. 2.
static ASDA_DEV_CVAR(int32, CVarAsdaShovelToolMeshFix,
	TEXT("asda.Shovel.ToolMeshFix"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Меш инструмента в руке при копании: 1 (по умолчанию) = по инструменту (лопата 40 — ch4_98_Shoveling, ")
	TEXT("авто-лопата 43 — ch4_98_AutoShovel_01); 0 = как оригинал — авто-меш своему только после ручной лопаты, соседу по 4025 114 ")
	TEXT("никогда (перевёрнутое условие), первая авто-лопата после входа — с ручной лопатой в руке."),
	ECVF_Default);
// ★ 10.10.2026 (яма копания, отчёт `digging_hole.md`): ключ исправления стартового эффекта СВОЕЙ авто-лопаты 43.
//   Эталон `CCharacter_SetAction` case 43 (entity_char.c:17367) спавнит своему `c-shovel-a_01` — тот же файл, что у лопаты 40
//   (контейнер 6.0 с: яма `badakmeshnoalpha` живёт 4.5 с и застывает видимой до конца контейнера — endMode 0, `asda.Fx.End0Freeze`),
//   хотя строка ошибки той же ветки называет эффект «AUTOSABZIL_PLAYING_BASE», а соседу и при появлении в 114 тот же клиент
//   ставит `c-shovel-b_01` (контейнер 8.5 с, яма 8.5 с, пыль до 8.12 с) — копипаст имени. Клип 43 идёт 9.034 с ⇒ у своего
//   яма и пыль гасли на 6.0 с, за 3 с до конца копки (жалоба владельца 10.10).
static ASDA_DEV_CVAR(int32, CVarAsdaShovelStartFxFix,
	TEXT("asda.Shovel.StartFxFix"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Стартовый эффект своей авто-лопаты 43: 1 (по умолчанию) = `c-shovel-b_01`, как у соседей и при появлении ")
	TEXT("(яма и пыль до 8.5 с на клип 9.03 с); 0 = как оригинал — `c-shovel-a_01` (эффект с ямой гаснет на 6.0 с)."),
	ECVF_Default);
// ★ Ревизия six_items СРЕДН-1 (09.10.2026): ключ отката конца состояния лопаты 40/43 по концу клипа копания.
static ASDA_DEV_CVAR(int32, CVarAsdaShovelClipEnd,
	TEXT("asda.Shovel.ClipEnd"), 1,
	TEXT("Состояние лопаты 40/43: 1 (по умолчанию) = как оригинал — снимается само концом клипа копания (40 — 6.0 с, 43 — 9.034 с: ")
	TEXT("клип 6.0 с с перемоткой на 4-м ударе), «занят» и оружие за спиной снимаются; 0 = прежний порт — конца нет, снимает ")
	TEXT("только следующая смена состояния (ход, эмоция, 4025 0 …)."),
	ECVF_Default);
// ★ Ревизия six_items СРЕДН-1/СРЕДН-2 (09.10.2026): ключ отката сетевой части копания своего персонажа — 4024 110/114 на старте
//   (`Item_UseShovelingTool`, item_inventory.c:39364/39410) и 5428 (1) в конце клипа (`Combat_SendSelectTarget(1)`, case 7/10).
static ASDA_DEV_CVAR(int32, CVarAsdaShovelNetSync,
	TEXT("asda.Shovel.NetSync"), 1,
	TEXT("Копание своим персонажем: 1 (по умолчанию) = как оригинал — на старте 4024 с кодом 110 (лопата) / 114 (авто-лопата), ")
	TEXT("в конце клипа копания 5428 «конец добычи» (тело: свой userNum, 1); 0 = прежний порт — ни 4024, ни 5428 не шлются."),
	ECVF_Default);

// ★ В45fix2 (ревизия В4/В5, МЕЛК-5, 09.10.2026): `CChar_SetState(…, 68)` — 4025 130 (`Recv_CharSpawnFx` g:551554-551555).
//   Состояние «добыча» эталона: трек 36 = `Dr_ch_ani_{m|f}_idle_00.jbb` (`Char_LoadMotionAnim` case 25), петля со звуком
//   `S_e_minewar_mining_01.wav` (`CCharacter_UpdateActionState` case 25, g:77746-77754), `bActionBusy = 1` (g:77139-77144).
//   Пишут его ещё спаун (g:52618), конец клипа (g:77753), `CharFx_AutoSabzil_State` (g:617757) и очередь 6753/6756
//   (g:619087) — системы добычи в порту нет. Здесь минимум 1:1 ради рыбалки: 68 ПЕРЕПИСЫВАЕТ действие 60…67 (как и эмоцию,
//   сидение, ход и замах — запись безусловна, g:76456), держит признак «занят», флаг [15036] и удочку НЕ трогает (снятие
//   флага — только в ≤ 2/5/6, g:607922). Снимает 68 любая следующая смена состояния (`SetAnimState`, эмоция, действие
//   рыбалки, навык/замах — см. `TickFishAttachments`).
//   [ПОРТ] Клипа трека 36 и звука добычи в порту нет — играет клип покоя (PORT_PENDING «Рыбалка, ревизия В4/В5», п.1).
void AAsdaPlayerCharacter::EnterEtalonState68()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	const int32 WasFish = GetFishAction();
	// Ход, сидение и замах переписываются так же, как в `PlayFishAction` (состояние 68 — действие «на месте»); ★ 09.10.2026: 68
	//   переписывает и лопату 40/43, и эмоцию, и рыбалку (запись состояния безусловна). ★ В3 S7: одна преамбула и одна запись.
	BeginEtalonAction(EAsdaActionState::Mining68, TEXT("4025 130"));
	UpdateWeaponMotionHidden();   // `bActionBusy = 1` — оружие за спину, как у 60…67
	PlayWorldClip(IdleClipIdx(), true);   // [ПОРТ] вместо петли трека 36 (клип добычи не импортирован)
	IdleTimer = 0.f; FidgetRemaining = 0.f;
	UE_LOG(LogAsdaPlayer, Log, TEXT("[действие] состояние 68 «добыча» по 4025 130: действие рыбалки %d переписано, флаг рыбалки %d сохранён ")
		TEXT("(клипа добычи в порту нет — покой)"), WasFish, bFishFlag ? 1 : 0);
}

// ★★ Решение владельца 09.10.2026 (п. 5): состояния 40 (лопата) / 43 (авто-лопата) — «эмоции 40/43» отчёта рыбалки.
//   Эталон `CChar_SetState@0xA13D50` (entity_char.c:19817-19843): case 40 / case 43 → LABEL_154: `animPlayFlag = 1`,
//   `digMineState = 0`, **`bActionBusy = 1`** — тот же признак «занят», что у эмоций 33…46 и рыбалки 60…67, а он в
//   `charUpdateActionStateTick@0xA216B0` уводит оружие ЗА СПИНУ (флаг `char+226184`: набор клипов, кость меша `2_a1`,
//   кость свечения). Ветка «не занят» `charEntityUpdateTick@0xA1ED30` (entity_char.c:22677-22688) при 40/43 тоже гасит
//   оружие (`[52136] = 0`) и показывает инструмент (`sub_93362B(…, 1)`). Кто ставит: свой — `Item_UseShovelingTool@0xD0BFE0`
//   (`CharFx_AutoSabzil_State(…, 40|43)`, item_inventory.c:39364/39410), соседи — 4025 110 → 40 (network.c:39958),
//   114 → 43 (network.c:39716). Снимает любая смена состояния (как 68) — и КОНЕЦ КЛИПА КОПАНИЯ (ревизия six_items СРЕДН-1,
//   09.10.2026): таймер `ShovelClipTimer` на длину `ShovelStateSeconds`, по нему `OnShovelClipEnd` (ветка конца case 7/10).
//   (До 10.10.2026 здесь были только состояние, правило оружия и конец по длине клипа — вместо клипа играл покой.)
//   ★ 10.10.2026 (копание, `Reports/owner_1010/digging.md`): СИСТЕМА КОПАНИЯ ЦЕЛИКОМ — клип трека 17/20 с кадра 0
//   (`CChar_SetState` case 40/43: `CObject_SetField13(трек, 0)`), удары по порогам (`TickShovel`), меш инструмента на `8_a1`
//   (`charEntityUpdateTick` → `sub_93362B(…, 1)` ровно в 40/43), стартовый эффект по источнику (`EAsdaShovelEntry`), конец —
//   по треку клипа (`Game_IsPausedOrMenu(трек)` = время трека ≥ длины клипа), а не по таймеру.
//   Ключи: `asda.Shovel.Clip 0` — без клипа (покой, прежний порт), `asda.Shovel.ToolMeshFix 0` — меш инструмента по правилам
//   оригинала (с его дефектами), `asda.Shovel.ClipEnd 0` — конца нет.
void AAsdaPlayerCharacter::EnterEtalonShovelState(int32 State, EAsdaShovelEntry Src, int32 PrevTimerMode)
{
	UWorld* W = GetWorld();
	if (!W || (State != 40 && State != 43)) { return; }
	// Ход, сидение и замах переписываются, как у 68 (действие «на месте»); эмоция, 68 и рыбалка снимаются записью поля.
	// ★ В3 S7: одна преамбула и одна запись (`BeginEtalonAction`).
	BeginEtalonAction((EAsdaActionState)State, Src == EAsdaShovelEntry::Self ? TEXT("лопата, свой")
		: (Src == EAsdaShovelEntry::Spawn ? TEXT("лопата, появление") : TEXT("лопата, 4025")));
	UpdateWeaponMotionHidden();           // `bActionBusy = 1` — оружие за спину
	W->GetTimerManager().ClearTimer(ShovelClipTimer);   // ★ 10.10: конец ведёт трек клипа (`TickShovel`), таймер не ставится

	// ── 1. КЛИП: трек 17 (40) / 20 (43) с кадра 0 — `CChar_SetState` case 40/43 (entity_char.c:19816-19838); пороги ударов —
	//      `Char_LoadMotionAnim` case 0x1F (entity_char.c:21282-21345). Счётчики ударов эталон здесь НЕ трогает (их обнуляют
	//      конец клипа своего и покадровое гашение стартового эффекта вне 40/43 — см. `TickShovel`).
	ShovelTrackMs = 0.f;
	ShovelElapsedS = 0.f; ShovelStrikeLog.Reset();   // диагностика самотеста
	ShovelLenMs = ShovelClipMs(State);   // длина трека (ассет / 6000 мс заголовка .jbb)
	LoadShovelThresholds();
	const int32 ClipIdx = (State == 40) ? kShovelClipIdx40 : kShovelClipIdx43;   // anim_233 / anim_234
	float ClipLen = 0.f;                  // длина запущенного клипа, с (0 — клипа нет)
	if (CVarAsdaShovelClip.GetValueOnGameThread() != 0) { ClipLen = PlayWorldClip(ClipIdx, /*bLoop*/false); }
	if (ClipLen <= 0.f) { PlayWorldClip(IdleClipIdx(), true); }   // [ПОРТ] ключ 0 / ассета нет — покой (прежний порт)
	IdleTimer = 0.f; FidgetRemaining = 0.f;

	// ── 2. МЕШ ИНСТРУМЕНТА на `8_a1` (`pModelPartArray+4716`, флаг [216] = загружен авто-меш). Правила эталона:
	//      • свой (`Item_UseShovelingTool`, item_inventory.c:39369/39427): 33 — сменить на ручной, если прежний режим таймера 34 и
	//        загружен авто; 34 — сменить на авто, если прежний режим 33 и загружен ручной. ⚠ Режим таймера в начале сессии = 0
	//        (писатель +0x20 только `set_o8`), поэтому ПЕРВАЯ авто-лопата после входа и любая авто-лопата без ручной перед ней
	//        показывают РУЧНУЮ лопату — дефект оригинала;
	//      • сосед 4025 110 (network.c:39966): авто → ручной; 4025 114 (network.c:39724): `if ([216]) [212] = 1` — условие
	//        ПЕРЕВЁРНУТО (у появления в 114 — `!= 1`, game_main.c:7314): авто-меш соседу не грузится никогда — дефект оригинала;
	//      • появление в 110/114 (game_main.c:7314/7435): авто → ручной / ручной → авто (верно).
	//      Начальный меш персонажа — ручной (`Char_LoadMotionAnim` case 0x1E, `[216] = 0`).
	//      [ИСПРАВЛЕНИЕ] (ключ `asda.Shovel.ToolMeshFix`, по умолчанию 1): меш всегда по инструменту — 40 ручной, 43 авто.
	const bool bWantAuto = (State == 43);   // меш, соответствующий инструменту
	if (CVarAsdaShovelToolMeshFix.GetValueOnGameThread() != 0)
	{
		if (!ShovelToolComp || bShovelToolAuto != bWantAuto) { LoadShovelToolMesh(bWantAuto); }
	}
	else
	{
		if (!ShovelToolComp) { LoadShovelToolMesh(false); }   // ручной — начальный меш (case 0x1E)
		bool bSwap = false;                                  // эталон меняет меш в этой ветке
		switch (Src)
		{
		case EAsdaShovelEntry::Self:
			bSwap = (State == 40) ? (PrevTimerMode == 34 && bShovelToolAuto) : (PrevTimerMode == 33 && !bShovelToolAuto);
			break;
		case EAsdaShovelEntry::Peer4025:
			bSwap = (State == 40) && bShovelToolAuto;   // 110: авто → ручной; 114: `if ([216])` грузит авто поверх авто — смены нет
			break;
		case EAsdaShovelEntry::Spawn:
			bSwap = (State == 40) ? bShovelToolAuto : !bShovelToolAuto;
			break;
		}
		if (bSwap) { LoadShovelToolMesh(bWantAuto); }
	}
	UpdateShovelToolVisible();            // `sub_93362B(…, 1)` — инструмент в руке

	// ── 3. СТАРТОВЫЙ ЭФФЕКТ (`worldMatrix[0]`): свой — `c-shovel-a_01` у ОБОИХ состояний (entity_char.c:17271/17367), сосед —
	//      a_01 (110) / b_01 (114); 4025 и свой — матрица по курсу/рельефу без привязки, появление — привязка 6 (game_main.c:7308/7429).
	//      Прежний хэндл эталон затирает без гашения; порт гасит его (иначе актор живёт без владельца) — [ПОРТ].
	//      [ИСПРАВЛЕНИЕ] (ключ `asda.Shovel.StartFxFix`, по умолчанию 1): свой 43 — `c-shovel-b_01`, как сосед/появление (см. ключ).
	StopShovelStartFx(/*bResetStrokes*/false);
	const bool bSelfAutoA01 = (Src == EAsdaShovelEntry::Self && CVarAsdaShovelStartFxFix.GetValueOnGameThread() == 0);   // эталонный a_01 своему 43
	const TCHAR* StartJeb = (State == 40 || bSelfAutoA01)
		? TEXT("Eff_0_char_99_c-shovel-a_01") : TEXT("Eff_0_char_99_c-shovel-b_01");
	SpawnShovelStartFx(StartJeb, /*bFollow*/Src == EAsdaShovelEntry::Spawn);

	UE_LOG(LogAsdaPlayer, Log, TEXT("[копание] состояние %d «%s» (%s): «занят», оружие за спину (набор _%02d, кость %s); клип anim_%d %s; ")
		TEXT("пороги %d/%d/%d/%d/%d мс; инструмент «%s»; стартовый эффект %s; конец клипа через %.3f с"),
		State, State == 40 ? TEXT("лопата") : TEXT("авто-лопата"),
		Src == EAsdaShovelEntry::Self ? TEXT("свой") : (Src == EAsdaShovelEntry::Spawn ? TEXT("появление") : TEXT("4025")),
		CurMotionSet(), *CurrentWeaponBone().ToString(), ClipIdx,
		ClipLen > 0.f ? *FString::Printf(TEXT("%.3f с"), ClipLen) : TEXT("НЕ играет (ключ asda.Shovel.Clip 0 / ассета нет) — покой"),
		ShovelThrMs[0], ShovelThrMs[1], ShovelThrMs[2], ShovelThrMs[3], ShovelThrMs[4],
		(ShovelToolComp && ShovelToolComp->GetStaticMesh()) ? *ShovelToolComp->GetStaticMesh()->GetName() : TEXT("нет"),
		ShovelStartFx.IsValid() ? StartJeb : TEXT("НЕ создан"),
		CVarAsdaShovelClipEnd.GetValueOnGameThread() != 0 ? ShovelStateSeconds(State) : 0.f);
}

// ★ Ревизия six_items СРЕДН-1 (09.10.2026): ДЛИТЕЛЬНОСТЬ СОСТОЯНИЯ ЛОПАТЫ = КОНЕЦ КЛИПА КОПАНИЯ ЭТАЛОНА.
//   `CCharacter_UpdateActionState@0xA16D20` (entity_char.c:20378-20475): конец — `Game_IsPausedOrMenu(трек)` (миснейм:
//   `*(this+13) >= *(this+11)`, т.е. время трека ≥ длины клипа; game_main.c:6749). Длина клипа (`this+11`) — поле файла +0x30
//   заголовка .jbb (jbb_motion_format.md §1: `fread(this, 0x40)` после магии), мс.
//   • 40 (case 7, трек 17 = `ch4_98_Shoveling_{m|f}.jbb`, +1088): удары по порогам `digStrokeThresholds`, не больше 4; конец —
//     конец клипа: T40 = L.
//   • 43 (case 10, трек 20 = `ch4_98_Shoveling_{m|f}_01.jbb`, +1280): удары по порогам `mineStrokeThresholds[idx]`, idx = n%4
//     (+1 при n ≥ 4), не больше 7; на 4-м ударе (n == 4) трек ПЕРЕМАТЫВАЕТСЯ на порог [1] (`CObject_SetField13(трек, thr[1])`,
//     :20452-20455), конец — конец клипа при n > 4. Удар 4 случается на пороге [3] ⇒ T43 = thr[3] + (L − thr[1]).
//   Пороги — таблица `combo time.bin` (`Char_LoadMotionAnim` case 0x1F, entity_char.c:21282-21322: строка `ch4_98_Shoveling_{m|f}`,
//   [0] = блок +48 запись 0, [1…] = блок +84 записи 0…; кадры уже в мс — `CComboManager_LoadComboTime@0x9BC5F0` ×1000/30 — и
//   усечены `__ftol2`). В порту: `DT_combo_time` (FrameTimeA0 / FrameTimeB0…), строки 266/267.
//   ДАННЫЕ (сверено 09.10.2026): `CharMotion.baz` клиента MINE — все четыре клипа `ch4_98_shoveling_{m|f}[_01].jbb` +0x30 = 6000.0 мс
//   (= последний ключ, fps 30); строки 266/267: A0 = 18, B0 = 62, B1 = 107, B2 = 153 кадра ⇒ пороги 600 / 2066 / 3566 / 5100 мс.
//   Итог: T40 = 6.000 с, T43 = 5.100 + (6.000 − 2.066) = 9.034 с (м и ж одинаково).
//   ★ 10.10.2026: клипы импортированы (anim_233/234) — длина берётся из ассета (`ShovelClipMs`); константа ниже — запас на
//   случай, если ассета нет (тогда и клип не играет). Пороги читаются из таблицы на лету.
static constexpr float kShovelClipMs = 6000.f;   // +0x30 заголовка `ch4_98_shoveling_{m|f}[_01].jbb` (CharMotion.baz MINE), мс
float AAsdaPlayerCharacter::ShovelStateSeconds(int32 State) const
{
	if (State != 40 && State != 43) { return 0.f; }
	const float ClipMs = ShovelClipMs(State);   // ★ 10.10.2026: длина ассета клипа (есть импорт) / 6000 мс заголовка .jbb
	if (State == 40) { return ClipMs / 1000.f; }
	// Пороги 43: строка таблицы по полу (`pThis[1079] == 1` — мужской; у порта Sex 2 = женский).
	const FString Row = (Sex == 2) ? TEXT("ch4_98_Shoveling_f") : TEXT("ch4_98_Shoveling_m");
	const FAsdaComboTimeRow* R = FindComboTimeRow(this, Row);
	const float Thr1 = R ? FMath::TruncToFloat(R->FrameTimeB0 * kFrameToMs) : 0.f;   // `mineStrokeThresholds[1]` = блок +84 [0]
	const float Thr3 = R ? FMath::TruncToFloat(R->FrameTimeB2 * kFrameToMs) : 0.f;   // `mineStrokeThresholds[3]` = блок +84 [2]
	if (!R || Thr1 <= 0.f || Thr3 <= Thr1 || Thr3 >= ClipMs)
	{
		// Строки нет / пороги вне клипа: перемотки не будет — конец по длине клипа (эталон без строки берёт запись 0 и пишет
		//   «ComboTable Load error», это дефект данных, не ветка поведения).
		UE_LOG(LogAsdaPlayer, Warning, TEXT("[действие] 43: строка combo_time «%s» %s (пороги %.0f/%.0f мс) — конец по длине клипа %.0f мс"),
			*Row, R ? TEXT("с порогами вне клипа") : TEXT("не найдена"), Thr1, Thr3, ClipMs);
		return ClipMs / 1000.f;
	}
	return (Thr3 + (ClipMs - Thr1)) / 1000.f;
}

// ★ Ревизия six_items СРЕДН-1 (09.10.2026): снять состояние лопаты и таймер конца её клипа (любая смена состояния).
void AAsdaPlayerCharacter::ClearEtalonShovelState()
{
	// ★ В3 S7: снимает только лопату (прежде у лопаты было своё поле, его обнуление эмоцию/рыбалку не задевало).
	if (AsdaActionState::IsShovel((int32)BusyAction)) { SetBusyAction(EAsdaActionState::Idle, TEXT("лопата снята")); }
	if (UWorld* W = GetWorld()) { W->GetTimerManager().ClearTimer(ShovelClipTimer); }
	UpdateShovelToolVisible();   // ★ 10.10.2026: инструмент виден только в 40/43; стартовый эффект гасит покадровый `TickShovel`
}

// ★ 09.10.2026 (автолопата, МЕЛК-1): сброс сессии — разбор у объявления.
void AAsdaPlayerCharacter::AbortEtalonShovelState(const TCHAR* Why)
{
	if (GetEtalonShovelState() == 0) { return; }
	const int32 Was = GetEtalonShovelState();   // снимаемое состояние (для журнала)
	ClearEtalonShovelState();
	UpdateWeaponMotionHidden();            // «занят» снят — оружие из-за спины (если зона позволяет)
	UE_LOG(LogAsdaPlayer, Log, TEXT("[действие] состояние %d снято сбросом сессии (%s): таймер конца клипа снят, 5428 не шлём"),
		Was, Why ? Why : TEXT("?"));
}

// ★ Ревизия six_items СРЕДН-1 (09.10.2026): КОНЕЦ КЛИПА КОПАНИЯ — ветка конца case 7/10 `CCharacter_UpdateActionState`:
//   `sub_93362B(…, 0)` (меш инструмента скрыт — `UpdateShovelToolVisible`), `bActionBusy = 0` (оружие из-за спины), затем
//   • свой (`CClass_GetField47565` = `selectionFlag`, [47565] == 1 у локального игрока): `CharFx_AutoSabzil_State(…, 0)` — это
//     `CCharacter_SetAction@0x9EE720` case 0 (entity_char.c:16523-16604): покой, сброс кадров; 4024 он шлёт ТОЛЬКО если прежнее
//     действие было 5 (код 400) — после 40/43 НЕ шлёт; и `Combat_SendSelectTarget(1)` (миснейм) — это **5428**
//     `OP_CHAR_E_MOTION_MINING_END`, тело [u16 свой userNum][u8 1] (combat_pvp.c:1349-1474; сервер `CharEMotion_MiningEnd`
//     GameServer.c:124858 сверяет userNum с отправителем, обнуляет своё состояние действия и раздаёт 5428 + 11/12/38);
//     у 43 ещё `Net_RecvUiTick()` — пустая функция (network.c:9);
//   • сосед: `CChar_SetState(…, 0)` — покой, ничего не шлёт.
void AAsdaPlayerCharacter::OnShovelClipEnd()
{
	const int32 St = GetEtalonShovelState();
	if (St != 40 && St != 43) { return; }
	SetBusyAction(EAsdaActionState::Idle, TEXT("конец клипа копания"));   // ★ В3 S7; ★ 10.10.2026: `sub_93362B(…, 0)` — инструмент из руки
	UpdateWeaponMotionHidden();           // `bActionBusy = 0` — оружие из-за спины (если зона позволяет)
	PlayWorldClip(IdleClipIdx(), true);   // состояние 0 — покой
	IdleTimer = 0.f; FidgetRemaining = 0.f;
	const bool bSelf = IsLocalPlayerCharacter();   // ветка «свой» эталона ([47565] == 1)
	// ★ 10.10.2026: свой обнуляет счётчик ударов своего трека (`digStrokeCount = 0` / `mineStrokeCount = 0`, entity_char.c:20408/20466);
	//   сосед — нет (`CChar_SetState(…, 0)`), его счётчики обнулит покадровое гашение стартового эффекта (`TickShovel`).
	if (bSelf) { if (St == 40) { ShovelDigStroke = 0; } else { ShovelMineStroke = 0; } }
	bool bSent = false;                            // ушёл ли 5428
	if (bSelf && CVarAsdaShovelNetSync.GetValueOnGameThread() != 0)
	{
		UGameInstance* GI = GetGameInstance();
		if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr) { bSent = Net->SendMiningEnd5428(1); }
	}
	UE_LOG(LogAsdaPlayer, Log, TEXT("[действие] конец клипа копания: состояние %d → 0, оружие из-за спины%s"),
		St, bSelf ? (bSent ? TEXT("; свой — 5428 (1) ушёл") : TEXT("; свой — 5428 не ушёл (нет связи / ключ asda.Shovel.NetSync 0)")) : TEXT(""));
}

// Ключ `asda.Shovel.NetSync` (общая точка для старта копания в `UAsdaInventorySubsystem::ApplyShovelingTool`).
bool AAsdaPlayerCharacter::IsShovelNetSync()
{
	return CVarAsdaShovelNetSync.GetValueOnGameThread() != 0;
}

// ═══════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ КОПАНИЕ (10.10.2026, `Reports/owner_1010/digging.md`): клип, удары, инструмент, эффекты, итог 5430.
//   Эталон: `CCharacter_UpdateActionState@0xA16D20` case 7 (40) / case 10 (43) (entity_char.c:20377-20469),
//   `charEntityUpdateTick` (инструмент, entity_char.c:22652-22712; гашение стартового эффекта, entity_char.c:8064-8073 свой /
//   8766-8771 все), `Char_LoadMotionAnim` case 0x1E/0x1F (меш и клипы, entity_char.c:21263-21345), итог — `network.c:1530-1592`.
// ═══════════════════════════════════════════════════════════════════════════════════════════════

// Пороги ударов из строки `ch4_98_Shoveling_{m|f}` таблицы `combo time.bin` (`DT_combo_time`): [0] = `CComboTable_GetEntryFloat48(…, 0)`
//   (кадры A0), [1..4] = `CComboTable_GetEntryFloat84(…, 0..3)` (кадры B0..B3); кадры → мс (×1000/30, `CComboManager_LoadComboTime`)
//   и усечение `__ftol2`. Одна строка у обоих треков: `digStrokeThresholds[0..3]` (40) = [0..3], `mineStrokeThresholds[0..4]` (43) = [0..4].
//   Данные 10.10.2026 (строки 266/267, м и ж одинаково): 600 / 2066 / 3566 / 5100 / 0 мс.
void AAsdaPlayerCharacter::LoadShovelThresholds()
{
	const FString Row = (Sex == 2) ? TEXT("ch4_98_Shoveling_f") : TEXT("ch4_98_Shoveling_m");   // `pThis[1079] == 1` — мужской
	const FAsdaComboTimeRow* R = FindComboTimeRow(this, Row);
	if (!R)
	{
		// Эталон без строки пишет «ComboTable Load error» и берёт запись −1 (дефект данных, не ветка поведения): ударов не будет.
		FMemory::Memzero(ShovelThrMs, sizeof(ShovelThrMs));
		UE_LOG(LogAsdaPlayer, Warning, TEXT("[копание] строки combo_time «%s» нет — пороги ударов 0, ударов не будет"), *Row);
		return;
	}
	const float Fr[5] = { R->FrameTimeA0, R->FrameTimeB0, R->FrameTimeB1, R->FrameTimeB2, R->FrameTimeB3 };   // кадры
	for (int32 I = 0; I < 5; ++I) { ShovelThrMs[I] = (int32)FMath::TruncToFloat(Fr[I] * kFrameToMs); }
}

// Длина клипа копания, мс: ассет anim_233 (40) / anim_234 (43) по полу; ассета нет — 6000 мс (+0x30 заголовка .jbb).
float AAsdaPlayerCharacter::ShovelClipMs(int32 State) const
{
	const int32 Idx = (State == 43) ? kShovelClipIdx43 : kShovelClipIdx40;   // индекс ассета клипа
	const FString Path = FString::Printf(TEXT("%sanim_%02d"), *CharBase(Sex), Idx);
	if (const UAnimSequence* Clip = UAsdaModelAnimSubsystem::LoadClipChecked(Path))
	{
		const float Len = Clip->GetPlayLength();
		if (Len > 0.f) { return Len * 1000.f; }
	}
	return kShovelClipMs;
}

// Покадрово (ветка состояния 40/43 `CCharacter_UpdateActionState` и гашение стартового эффекта `charEntityUpdateTick`).
void AAsdaPlayerCharacter::TickShovel(float DeltaSeconds)
{
	const int32 St = GetEtalonShovelState();
	if (St != 40 && St != 43)
	{
		// entity_char.c:8766-8771 (все) / 8064-8073 (свой): `if (state ∉ {40,43} && [52188] != −1) { StopByOwner; счётчики = 0 }` —
		//   хэндл после гашения не сбрасывается в −1, поэтому, раз появившись, обнуление идёт каждый кадр вне копания.
		if (bShovelStartHandle) { StopShovelStartFx(/*bResetStrokes*/true); }
		return;
	}
	// `animSetTrackValueByIndex(трек, animTrackValue)` — время трека растёт вместе с клипом (мс).
	ShovelTrackMs += DeltaSeconds * 1000.f;
	ShovelElapsedS += DeltaSeconds;   // диагностика: часы прохода
	if (St == 40)
	{
		// case 7: удар, когда трек дошёл до `digStrokeThresholds[n]`, n < 4 (4 удара за клип).
		const int32 N = ShovelDigStroke;                                      // `digStrokeCount`
		const float Thr = (float)ShovelThrMs[FMath::Clamp(N, 0, 4)];           // эталон читает [n] до проверки n < 4
		if (ShovelTrackMs >= Thr && N < 4)
		{
			ShovelStrike();
			++ShovelDigStroke;
		}
	}
	else
	{
		// case 10: индекс порога = n % 4 (+1 при n ≥ 4), ударов не больше 7; на 4-м ударе трек перематывается на порог [1]
		//   (`CObject_SetField13(трек, mineStrokeThresholds[1])`) — удары 5…7 идут по порогам [1]…[3] второй раз.
		const int32 N = ShovelMineStroke;                                     // `mineStrokeCount`
		const int32 Idx = (N % 4) + (N >= 4 ? 1 : 0);                         // `digCount`
		if (ShovelTrackMs >= (float)ShovelThrMs[FMath::Clamp(Idx, 0, 4)] && N < 7)
		{
			ShovelStrike();
			if (++ShovelMineStroke == 4)
			{
				ShovelTrackMs = (float)ShovelThrMs[1];
				// Клип на скелете — туда же (только если играет именно клип 43; при ключе `asda.Shovel.Clip 0` играет покой).
				if (BaseSkel && GetPlayingClipName().EndsWith(FString::Printf(TEXT("anim_%d"), kShovelClipIdx43)))
				{
					BaseSkel->SetPosition(ShovelTrackMs / 1000.f, /*bFireNotifies*/false);
				}
			}
		}
	}
	// Конец: `Game_IsPausedOrMenu(трек)` (миснейм) = время трека ≥ длины клипа; у 43 — только после 4-го удара (n > 4).
	if (CVarAsdaShovelClipEnd.GetValueOnGameThread() == 0) { return; }
	if (St == 43 && ShovelMineStroke <= 4) { return; }
	if (ShovelTrackMs >= ShovelLenMs) { OnShovelClipEnd(); }   // длина клипа снята на входе в состояние
}

// Удар (case 7/10): `Audio_Play3DSound(…, "S_e_shovelling01.wav")` в позиции персонажа (`Class_GetVec3_86`), гашение прежнего
//   эффекта удара (`CEffectMgr_StopByOwner(worldMatrix[1])`), `Effect_SpawnByName("Eff_0_char_99_c-shovel-a_00.jeb", …, posVec, 0, 6, …)`
//   в позиции персонажа (`CObj_GetPosition(gap_1B0)`) с привязкой 6 (мировая матрица персонажа). Аргумент 7 вызова
//   (`effectSlots[100].field_4`) — индекс слота навыка, на вид эффекта не влияет.
void AAsdaPlayerCharacter::ShovelStrike()
{
	++ShovelStrikeTotal;
	ShovelStrikeLog.Add(ShovelElapsedS);
	UWorld* W = GetWorld();
	if (!W) { return; }
	UGameInstance* GI = GetGameInstance();
	if (UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		Snd->PlaySound3D(FName(TEXT("S_e_shovelling01")), GetActorLocation());
	}
	if (AActor* Old = ShovelHitFx.Get()) { Old->Destroy(); }
	ShovelHitFx = nullptr;
	// ★ Дедуп №2 В1-C (F12): спавн, привязка 6 и срок — общей точкой одноразового эффекта на носителе.
	if (AAsdaEffectActor* Fx = AAsdaEffectActor::SpawnOnCarrier(W, TEXT("Eff_0_char_99_c-shovel-a_00"), GetActorLocation(), this,
		BaseSkel, NAME_None, EAsdaFxAttachMode::CharWorld, FVector::ZeroVector))
	{
		ShovelHitFx = Fx;
	}
	else
	{
		UE_LOG(LogAsdaPlayer, Warning, TEXT("[копание] BP_Fx_Eff_0_char_99_c_shovel_a_00 не собран (Tools/digging_build_fx.py) — эффект удара не показан"));
	}
	UE_LOG(LogAsdaPlayer, Verbose, TEXT("[копание] удар: состояние %d, трек %.0f мс, удар №%d"),
		GetEtalonShovelState(), ShovelTrackMs, GetEtalonShovelState() == 43 ? ShovelMineStroke + 1 : ShovelDigStroke + 1);
}

// Меш инструмента (`meshLoad_BindSubsetTexturesWithState` + `sub_93DE55(часть, «8_a1»)`): ручной `ch4_98_Shoveling.jbm` /
//   авто `ch4_98_AutoShovel_01.jbm`. Оба меша ригидны по одной кости `8_a1` (палитра из одной кости, mtype 0x100 — как у оружия),
//   поэтому посадка — та же, что у оружия в руке (`UAsdaCharAssembly::WeaponAttachInfo`, класс 0: кость `8_a1`, bind .jbb).
void AAsdaPlayerCharacter::LoadShovelToolMesh(bool bAuto)
{
	if (!BaseSkel) { return; }
	if (!ShovelToolComp)
	{
		ShovelToolComp = NewObject<UStaticMeshComponent>(this, TEXT("ShovelToolMesh"));
		FName Bone; FTransform Xf;
		UAsdaCharAssembly::WeaponAttachInfo(/*WeaponClass*/0, /*bStowed*/false, Bone, Xf);   // `8_a1`, как оружие в руке
		ShovelToolComp->SetupAttachment(BaseSkel, Bone);
		ShovelToolComp->SetRelativeTransform(Xf);
		ShovelToolComp->SetCastShadow(true);
		ShovelToolComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ShovelToolComp->SetVisibility(false);
		ShovelToolComp->RegisterComponent();
	}
	const TCHAR* Mesh = bAuto ? TEXT("ch4_98_autoshovel_01") : TEXT("ch4_98_shoveling");   // имя статик-меша (Models/Char)
	if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *UAsdaCharAssembly::CharStaticMeshPath(Mesh), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		ShovelToolComp->SetStaticMesh(M);
	}
	else
	{
		// Эталон: «MOVE_AUTOSABZIL : GET MESH CHARACTER_LOAD_MESH_AUTOSABZIL» в журнал ошибок и выход.
		UE_LOG(LogAsdaPlayer, Warning, TEXT("[копание] меш инструмента %s не импортирован — инструмента в руке нет"), Mesh);
	}
	bShovelToolAuto = bAuto;   // `[216]`
}

// `sub_93362B(…, 1|0)`: инструмент виден ровно в состояниях 40/43 (charEntityUpdateTick, entity_char.c:22663-22688).
void AAsdaPlayerCharacter::UpdateShovelToolVisible()
{
	const bool bOn = (GetEtalonShovelState() != 0);   // ★ В3 S7: из единого поля
	if (ShovelToolComp) { ShovelToolComp->SetVisibility(bOn); }
}

// Стартовый эффект копания. bFollow = привязка 6 (появление в 110/114: `Effect_SpawnByName(…, posVec, 0, 6, …)`);
//   иначе — матрица БЕЗ привязки (`effectMgrSpawnJEB(…, матрица, 0, 1.0)`), построенная эталоном так: ось «вперёд» — от точки
//   земли под персонажем к точке земли на 100 единиц по курсу (`terrainGetCellByPos` дважды), «вверх» — (0,1,0), «вправо» —
//   их произведение; перенос — позиция персонажа (`worldMatrix[12..14]`). В порту: матрица персонажа (привязка 6 на кадре
//   спавна) + поворот горизонтального курса на склон по высотам `.TER` (`AsdaGround::GroundZ` = `terrainGetCellByPos`).
void AAsdaPlayerCharacter::SpawnShovelStartFx(const TCHAR* JebBase, bool bFollow)
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	AAsdaEffectActor* Fx = AAsdaEffectActor::SpawnByJebName(W, JebBase, GetActorLocation(), FRotator::ZeroRotator, this);
	if (!Fx)
	{
		// Эталон: «… EFFECT MANAGER ADD CHARACTER_LOAD_EFFECT_(AUTO)SABZIL_PLAYING_BASE» — хэндл не ставится.
		UE_LOG(LogAsdaPlayer, Warning, TEXT("[копание] BP стартового эффекта %s не собран (Tools/digging_build_fx.py) — не показан"), JebBase);
		return;
	}
	Fx->ConfigureBoneAttach(this, BaseSkel, NAME_None, EAsdaFxAttachMode::CharWorld, FVector::ZeroVector);
	if (!bFollow)
	{
		FVector L; FQuat Q;
		if (Fx->ComputeFollowTargetWorld(L, Q))
		{
			const FVector2D F2 = AsdaCoord::GameDirToUe(GetFacingDirGame()).GetSafeNormal();   // курс в UE-плоскости
			if (!F2.IsNearlyZero())
			{
				const FVector Here = GetActorLocation();
				const float Z0 = AsdaGround::GroundZ(W, Here.X, Here.Y, Here.Z, this);
				const float Z1 = AsdaGround::GroundZ(W, Here.X + F2.X * 100.f, Here.Y + F2.Y * 100.f, Z0, this);
				const FVector Fs = FVector(F2.X * 100.f, F2.Y * 100.f, Z1 - Z0).GetSafeNormal();   // курс по склону
				if (!Fs.IsNearlyZero()) { Q = FQuat::FindBetweenNormals(FVector(F2.X, F2.Y, 0.f), Fs) * Q; }
			}
			Fx->SetActorLocationAndRotation(L, Q);
		}
		Fx->bFollowPosition = false;   // матрица задана один раз — эффект стоит на месте
	}
	ShovelStartFx = Fx;
	bShovelStartHandle = true;
}

// `CEffectMgr_StopByOwner(worldMatrix[0])`; bResetStrokes — ветка покадрового гашения вне 40/43 (там же обнуляются оба счётчика).
void AAsdaPlayerCharacter::StopShovelStartFx(bool bResetStrokes)
{
	if (AActor* Fx = ShovelStartFx.Get()) { Fx->Destroy(); }
	ShovelStartFx = nullptr;
	if (bResetStrokes) { ShovelDigStroke = 0; ShovelMineStroke = 0; }
}

// ★ ИТОГ КОПКИ 5430 (`network.c:1530-1592`, case 0x1536; сервер `sub_546C00` рассылает его СЕКТОРУ, 3 б {u8 итог, i16 userNum}):
//   точка = `Class_GetVec3_86(персонаж)` + по высоте: свой — +50 при `g_bWorldActive == 1`, чужой — +40 при `g_bUpgradeFxActive == 1`
//   (это признаки «показывать своё / чужие имена»: тот же выбор у плашки имени, game.c:594891), и ещё +10 при клане (`clanId > 0`).
//   [ПОРТ] плашки имён в порту видны всегда (настройки скрытия нет) — оба признака = 1. Затем гашение эффекта удара
//   (`worldMatrix[1]`), эффект итога `c-suc-a_00` / `c-fail-a_00` (`Effect_SpawnByName(…, posVec, 0, 0, …)` — привязки нет, стоит
//   в точке), звук `S_e_shovelling02.wav` / `03.wav` (3D), `bActionBusy = 0`. Нотис 9003/9004 — у вызывающего, только свой.
void AAsdaPlayerCharacter::OnMiningResult5430(bool bSuccess)
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	const bool bSelf = IsLocalPlayerCharacter();   // `effectSlots[112].field_40 == 1`
	float Up = bSelf ? 50.f : 40.f;                // имена видны: свой +50 / чужой +40
	const UAsdaNameplateComponent* NP = FindComponentByClass<UAsdaNameplateComponent>();
	const int32 Clan = NP ? NP->ClanId : -1;       // `*(i16*)&gap_93A[32]` — id клана
	if (Clan > 0) { Up += 10.f; }
	if (AActor* Old = ShovelHitFx.Get()) { Old->Destroy(); }
	ShovelHitFx = nullptr;
	const FVector Base = GetActorLocation();       // `Class_GetVec3_86`
	const TCHAR* Jeb = bSuccess ? TEXT("Eff_0_char_99_c-suc-a_00") : TEXT("Eff_0_char_99_c-fail-a_00");
	if (!AAsdaEffectActor::SpawnOnCarrier(W, Jeb, Base + FVector(0.f, 0.f, Up), this))   // ★ В1-C F12: спавн + срок общей точкой
	{
		UE_LOG(LogAsdaPlayer, Warning, TEXT("[копание] BP эффекта итога %s не собран (Tools/digging_build_fx.py) — не показан"), Jeb);
	}
	UGameInstance* GI = GetGameInstance();
	if (UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		Snd->PlaySound3D(FName(bSuccess ? TEXT("S_e_shovelling02") : TEXT("S_e_shovelling03")), Base);
	}
	// `bActionBusy = 0`. В порту «занят» выводится из состояния; к приходу 5430 (ответ на 5428 из конца клипа) 40/43 уже снято —
	//   оружие из-за спины вернул конец клипа. Если 5430 пришёл раньше конца (у эталона так не бывает), состояние не трогаем.
	UpdateWeaponMotionHidden();
	UE_LOG(LogAsdaPlayer, Log, TEXT("[копание] итог 5430: %s у %s — эффект %s (+%.0f по высоте), звук %s"),
		bSuccess ? TEXT("удачно") : TEXT("не удалось"), bSelf ? TEXT("своего") : TEXT("соседа"), Jeb, Up,
		bSuccess ? TEXT("S_e_shovelling02") : TEXT("S_e_shovelling03"));
}

// Конец прохода клипа действия = `CCharacter_UpdateActionState@0xA16D20` case 16…23 (game.c:77722-77743).
void AAsdaPlayerCharacter::OnFishClipEnd()
{
	if (!IsFishActionActive() || !GetWorld()) { return; }
	const int32 A = GetFishAction();
	// `CObject_SetField13(трек, 0)` — кадр клипа на 0: клип идёт следующим проходом с начала (у 61 — петля ожидания,
	//   у 62/67 чужого — по кругу; ★ world_anim §2.1 «стоят на последнем кадре» неверно: время трека обнуляется).
	const int32 Idx = kFishClipBase + (A - 60);
	const float Len = PlayWorldClip(Idx, /*bLoop*/false);
	if (Len > 0.f)
	{
		FishClipStartedS = GetWorld()->GetTimeSeconds();
		FishClipAsset = (BaseSkel && BaseSkel->GetSingleNodeInstance())
			? Cast<UAnimSequence>(BaseSkel->GetSingleNodeInstance()->GetAnimationAsset()) : nullptr;
		GetWorld()->GetTimerManager().SetTimer(FishClipTimer, this, &AAsdaPlayerCharacter::OnFishClipEnd, Len, false);
	}
	if (bFishActionSelf)
	{
		// Свой: `Fishing_OnStateMessage(…, трек = действие − 33)` — автомат рыбалки. Пока включён [ПОРТ]-мост волны В1
		//   (`asda.Fish.ClipBridge 1`), конец клипа отдаёт он — здесь молчим, чтобы переход не случился дважды.
		static IConsoleVariable* CvBridge = ASDA_FIND_DEV_CVAR(TEXT("asda.Fish.ClipBridge"));
		if (CvBridge && CvBridge->GetInt() != 0) { return; }
		if (UAsdaFishingSubsystem* Fish = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaFishingSubsystem>() : nullptr)
		{
			Fish->OnFishingClipEnd(A);
		}
		return;
	}
	// Чужой (game.c:77733-77742).
	switch (A)
	{
	case 60: case 61: case 63: case 64:
		PlayFishAction(61, false);
		break;
	case 65: case 66:
		bFishFlag = false;                       // [15036] = 0
		StopFishAction(TEXT("конец 65/66 у чужого"));
		break;
	default:
		break;                                   // 62/67 — перехода нет
	}
}

void AAsdaPlayerCharacter::TickFishClipWatch()
{
	if (!IsFishActionActive() || !FishClipAsset.IsValid() || !BaseSkel || !BaseSkel->GetSingleNodeInstance()) { return; }
	const UAnimSequence* Now = Cast<UAnimSequence>(BaseSkel->GetSingleNodeInstance()->GetAnimationAsset());
	if (Now == FishClipAsset.Get()) { return; }
	UE_LOG(LogAsdaPlayer, Warning, TEXT("[рыбалка] КЛИП ДЕЙСТВИЯ %d ПОДМЕНЁН на %.2f с: было '%s', стало '%s' (AnimState=%d, замах=%d)"),
		GetFishAction(), (float)(GetWorld()->GetTimeSeconds() - FishClipStartedS), *GetNameSafe(FishClipAsset.Get()), *GetNameSafe(Now),
		(int32)AnimState, IsAttackAnimActive() ? 1 : 0);
	FishClipAsset = nullptr;   // сообщаем один раз на проход
}

void AAsdaPlayerCharacter::SetFishFields(bool bFlag, bool bHasFish, int32 SizePct, int32 Code)
{
	bFishFlag = bFlag;
	bFishHasFish = bHasFish;
	FishSizePct = SizePct;
	FishCode = Code;
}

bool AAsdaPlayerCharacter::IsFishHookVisible() const
{
	return FishHookComp && FishHookComp->GetSkeletalMeshAsset() && !FishHookComp->bHiddenInGame;
}

FString AAsdaPlayerCharacter::GetPlayingClipName() const
{
	const UAnimSingleNodeInstance* SN = BaseSkel ? BaseSkel->GetSingleNodeInstance() : nullptr;
	return SN ? GetNameSafe(SN->GetAnimationAsset()) : FString();
}

void AAsdaPlayerCharacter::SpawnFishEndFx(const TCHAR* JebBase)
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	// `Effect_SpawnByName(g_pEffectMgr, имя, 1, x, y + 130, z, 0, char->posVec, 0, 0, 0, 1.0)`: режим привязки (a10) = 0 —
	//   эффект стоит в точке спавна (posVec — только владелец узла, `Effect_SpawnByName@0xF56340` +201/+200).
	//   Игровая ось Y = UE Z, единицы 1:1 (flt_13AD520 = 130.0).
	const FVector At = GetActorLocation() + FVector(0.f, 0.f, 130.f);
	// ★ Дедуп №2 В1-C (F12): спавн и срок — общей точкой (тот же вызов у своего: `UAsdaFishingSubsystem::SpawnEndFx`).
	AAsdaEffectActor* Fx = AAsdaEffectActor::SpawnOnCarrier(W, JebBase, At, this);
	if (!Fx)
	{
		UE_LOG(LogAsdaPlayer, Warning, TEXT("[рыбалка] эффект %s: BP_Fx_%s не найден"), JebBase, JebBase);
		return;
	}
	++FishPeerFxCount;
	UE_LOG(LogAsdaPlayer, Log, TEXT("[рыбалка] чужой: эффект %s над персонажем"), JebBase);
}

void AAsdaPlayerCharacter::SetFishRodClip(int32 Clip)
{
	if (!FishRodComp) { FishRodClip = FMath::Clamp(Clip, 0, 7); return; }   // удочки нет — эталон ставит поле только существующей
	Clip = FMath::Clamp(Clip, 0, 7);
	UAnimSingleNodeInstance* SN = FishRodComp->GetSingleNodeInstance();
	// Время старого трека запоминается: у эталона каждый из 8 слотов держит своё время, смена `+464` его не сбрасывает.
	if (SN && FishRodClip >= 0 && FishRodClip < 8) { FishRodTrackPos[FishRodClip] = SN->GetCurrentTime(); }
	FishRodClip = Clip;
	const FString Path = UAsdaModelAnimSubsystem::BuildSkelAnimPath(AsdaAssetPaths::kSkelRootFishing, FishRodMeshName, Clip);
	UAnimSequence* Seq = UAsdaModelAnimSubsystem::LoadClipChecked(Path);
	if (!Seq) { UE_LOG(LogAsdaPlayer, Warning, TEXT("[рыбалка] клип удочки не найден: %s"), *Path); return; }
	// Петля: по концу трека эталон зовёт `CClass_DispatchByFlag(трек, 0)` — время на 0 (game.c:607905).
	FishRodComp->PlayAnimation(Seq, /*bLooping*/true);
	if (UAnimSingleNodeInstance* SN2 = FishRodComp->GetSingleNodeInstance())
	{
		const float Len = Seq->GetPlayLength();
		const float Pos = (Len > 0.f) ? FMath::Fmod(FishRodTrackPos[Clip], Len) : 0.f;
		SN2->SetPosition(Pos, false);
	}
}

void AAsdaPlayerCharacter::InsertFishRod()
{
	if (bFishRodInserted || !BaseSkel) { return; }
	// `fishingInsertEntry` создаёт объект удочки и эталон ставит [15037]/+226061 = 1 СРАЗУ, даже если меш не загрузился
	//   (game.c:55034 / 55841). ★ В5fix (ревизия В5, МЕЛК-3): та же защёлка — повтор только после снятия по флагу 0,
	//   а не каждый кадр с предупреждением.
	bFishRodInserted = true;
	FishRodLoadedItem = EquippedWeaponItemId;   // [15088] = [15084] (game.c:607585; оба поля у эталона не меняются — см. TickFishAttachments)
	// Меш (`CFishing_LoadRodAnimations@0xFCD6F0`, game.c:607586-607620): запись предмета оружия (`char+0x835`) →
	//   img-запись → «<ImgRecord+4>_00.jbm». Есть img-запись — фолбэк НЕ берётся (`bUsedItemModel = 1`), даже если
	//   такого .jbm нет (тогда у эталона удочки просто не видно). Фолбэк — только когда нет записи предмета или img.
	const UAsdaItemTableSubsystem* Items = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* IRec = (Items && EquippedWeaponItemId > 0) ? Items->GetItem(EquippedWeaponItemId) : nullptr;
	const FAsdaImgRecord* ImgRec = (Items && IRec) ? Items->GetImgRecord(IRec->ImgIndex) : nullptr;
	FString Name;
	USkeletalMesh* Mesh = nullptr;
	if (ImgRec)
	{
		Name = (ImgRec->MeshMale + TEXT("_00")).ToLower();
		Mesh = UAsdaModelAnimSubsystem::LoadMeshChecked(UAsdaModelAnimSubsystem::BuildSkelPath(AsdaAssetPaths::kSkelRootFishing, Name));
		if (!Mesh)
		{
			UE_LOG(LogAsdaPlayer, Warning, TEXT("[рыбалка] удочка: предмет %d, img-запись есть, меша '%s' нет — как оригинал, удочки не видно ")
				TEXT("(фолбэк эталон в этой ветке не берёт); повтор — после снятия по флагу"), EquippedWeaponItemId, *Name);
			return;
		}
	}
	else
	{
		// [ПОРТ] эталонный фолбэк `itm_n_Fishrod_01.jbm` не существует ни в одном клиенте (отчёт В5а §2) — fird_00.
		Name = kFishRodFallbackMesh;
		UE_LOG(LogAsdaPlayer, Log, TEXT("[рыбалка] удочка: нет %s для предмета %d — [ПОРТ] фолбэк %s (эталон itm_n_Fishrod_01)"),
			IRec ? TEXT("img-записи") : TEXT("записи предмета"), EquippedWeaponItemId, kFishRodFallbackMesh);
		Mesh = UAsdaModelAnimSubsystem::LoadMeshChecked(UAsdaModelAnimSubsystem::BuildSkelPath(AsdaAssetPaths::kSkelRootFishing, Name));
		if (!Mesh)
		{
			UE_LOG(LogAsdaPlayer, Warning, TEXT("[рыбалка] меш фолбэка удочки %s не импортирован — удочки не видно"), *Name);
			return;
		}
	}
	USkeletalMeshComponent* C = NewObject<USkeletalMeshComponent>(this, NAME_None, RF_Transient);
	C->SetSkeletalMesh(Mesh);
	C->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	C->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(true);
	const bool bHand = BaseSkel->GetBoneIndex(kFishCharHandBone) != INDEX_NONE;
	C->SetupAttachment(BaseSkel, bHand ? kFishCharHandBone : NAME_None);
	C->RegisterComponent();
	FishRodComp = C;
	FishRodMeshName = Name;
	for (float& P : FishRodTrackPos) { P = 0.f; }
	// `fishingInitRodModel` → `sub_FCDED0`: клип 0. [ИСПРАВЛЕНИЕ] — клип текущего действия (см. ключ выше).
	const int32 Clip = (CVarFishLegacyRodClip0.GetValueOnGameThread() == 0 && IsFishActionActive()) ? (GetFishAction() - 60) : 0;
	FishRodClip = -1;
	SetFishRodClip(Clip);
	UE_LOG(LogAsdaPlayer, Log, TEXT("[рыбалка] удочка %s вставлена (предмет %d, %s, клип %d, кость %s)"),
		*Name, EquippedWeaponItemId, bFishOwnerSelf ? TEXT("свой") : TEXT("чужой"), Clip, bHand ? TEXT("8_a1") : TEXT("НЕТ"));
}

void AAsdaPlayerCharacter::RemoveFishRod()
{
	bFishRodInserted = false;   // [15037] = 0 (`charFxRemoveAttachedById`, game.c:55037 / 55851)
	if (!FishRodComp) { return; }
	FishRodComp->DestroyComponent();
	FishRodComp = nullptr;
	FishRodMeshName.Reset();
	UE_LOG(LogAsdaPlayer, Log, TEXT("[рыбалка] удочка снята"));
}

void AAsdaPlayerCharacter::InsertFishHook()
{
	if (bFishHookInserted) { return; }
	bFishHookInserted = true;
	FishHookLoadedCode = FishCode;   // [15080] = [15076] (game.c:609380)
	// `CItemCollectUI_LoadPreviewModel@0xFD1370`: запись `fishGameFindRecordById([15076])`; нет записи — модели НЕТ.
	//   ★ Ветка «Fs_fsm_crucian_00» в эталоне стоит под `else` от `if (pChar)` ПОСЛЕ разыменования pChar — недостижима
	//   (прежнее «−1 → карась» в world_anim §2.4 / В5а §3 — ошибка). Имя «-1» (37 записей) грузит несуществующий
	//   `-1.jbm` ⇒ рыбы не видно. [ОРИГИНАЛ].
	const UAsdaFishingSubsystem* Fish = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaFishingSubsystem>() : nullptr;
	const FAsdaFishDataRow* Row = Fish ? Fish->FindFishData(FishCode) : nullptr;
	const FString Name = Row ? Row->Model.ToLower() : FString();
	if (Name.IsEmpty() || Name == TEXT("-1"))
	{
		UE_LOG(LogAsdaPlayer, Log, TEXT("[рыбалка] рыба %d: модели нет (%s) — как оригинал, не показывается"),
			FishCode, Row ? TEXT("имя «-1»") : TEXT("нет записи Fish_data"));
		return;
	}
	USkeletalMesh* Mesh = UAsdaModelAnimSubsystem::LoadMeshChecked(UAsdaModelAnimSubsystem::BuildSkelPath(AsdaAssetPaths::kSkelRootFishing, Name));
	if (!Mesh) { UE_LOG(LogAsdaPlayer, Warning, TEXT("[рыбалка] меш рыбы %s не импортирован"), *Name); return; }
	USkeletalMeshComponent* C = NewObject<USkeletalMeshComponent>(this, NAME_None, RF_Transient);
	C->SetSkeletalMesh(Mesh);
	C->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	C->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(true);
	C->SetHiddenInGame(true);                    // видимость решает посадка (действие 62/65 и [15066])
	C->SetupAttachment(BaseSkel, NAME_None);
	C->RegisterComponent();
	// Клип `<имя>_00.jbb` по кругу (`AttachedObject_UpdateAndRender`: по концу трека — время 0).
	if (UAnimSequence* Seq = UAsdaModelAnimSubsystem::LoadClipChecked(UAsdaModelAnimSubsystem::BuildSkelAnimPath(AsdaAssetPaths::kSkelRootFishing, Name, 0)))
	{
		C->PlayAnimation(Seq, /*bLooping*/true);
	}
	FishHookComp = C;
	UE_LOG(LogAsdaPlayer, Log, TEXT("[рыбалка] рыба %s вставлена (код %d, размер %d%%)"), *Name, FishCode, FishSizePct);
}

void AAsdaPlayerCharacter::RemoveFishHook()
{
	bFishHookInserted = false;
	if (!FishHookComp) { return; }
	FishHookComp->DestroyComponent();
	FishHookComp = nullptr;
	UE_LOG(LogAsdaPlayer, Log, TEXT("[рыбалка] рыба снята"));
}

// Метка «спрятано рыбалкой» на компоненте/акторе: при показе возвращаем ТОЛЬКО помеченное нами — то, что спрятала другая
//   система (GM-скрытие `AsdaGmHiddenByHost`, смерть и т.п.), не «проявляем» (ревизия В5, МЕЛК-9).
static const FName kFishHiddenTag(TEXT("AsdaFishHidden"));

// ★ `char[52136] = 0` на время рыбалки (`charEntityUpdateTick@0xA1ED30`, game.c:80805-80810). ★ В5fix (ревизия В5, ВАЖН-1):
//   это поле — общий указатель видимости частей ОРУЖИЯ И ЩИТА, а не только оружия (второй метод — байты: адрес
//   `char+0x32EA0` передаётся загрузчику части оружия +0xC48 `CCharacter_SetupModelFromItem` 0x9E9BBF и загрузчику щита
//   `CCharacter_SetParts_Armor@0x9EB3C0` (слот 7, часть +0xE54) 0x9EB558/0x9EB7C6/0x9EB902/0x9EBBBE; он же гейт свечения
//   оружия `CCharacter_UpdateHeldItemEffect` 0xA4C36C, свечения щита `itemSetupSowelGlow` (game.c:66301, 0x9ED85F) и АУРЫ
//   щита `CCharacter_UpdateEquipAuraEffect` 0xA4E01B (game.c:94787: `!digMineState` → без эффекта, кость `_7_a1`).
//   Поле 52137 (+0x32EA4) загрузчикам частей не передаётся — к видимости отношения не имеет (прежний комментарий был ложным).
//   В порту: меш оружия (`UAsdaCharAssembly::WeaponMeshTag`), свечение заточки оружия (`WeaponGlowFx`), аура щита
//   (`ShieldGlowFx` = эффект `UpdateEquipAuraEffect`). ★ 25.10.2026 — и меш щита (`UAsdaCharAssembly::ShieldMeshTag`,
//   эталонная часть +0xE54 `SetParts_Armor`). Полос «совела» в порту нет — прятать больше нечего.
//   Проход — только при смене `bHide` или когда набор частей пересобран (подпись: число компонентов, привязанных к
//   корню/скелету, скрытость самого персонажа, живость запомненных частей), а не перебор каждый кадр (МЕЛК-9).
void AAsdaPlayerCharacter::ApplyFishWeaponHide(bool bHide)
{
	if (!bHide && !bFishWeaponHidden) { return; }
	const int32 Sig = GetComponents().Num() * 7919
		+ (BaseSkel ? BaseSkel->GetAttachChildren().Num() : 0) * 131
		+ (RootComponent ? RootComponent->GetAttachChildren().Num() : 0) * 2
		+ (IsHidden() ? 1 : 0);
	bool bStale = false;
	for (const TWeakObjectPtr<UObject>& W : FishHiddenParts) { if (!W.IsValid()) { bStale = true; break; } }
	if (bHide == bFishWeaponHidden && Sig == FishHideSig && !bStale) { return; }
	FishHideSig = Sig;
	FishHiddenParts.RemoveAll([](const TWeakObjectPtr<UObject>& W) { return !W.IsValid(); });
	int32 nMesh = 0, nWeaponFx = 0, nShieldFx = 0;
	TArray<UStaticMeshComponent*> SMs;
	GetComponents<UStaticMeshComponent>(SMs);
	for (UStaticMeshComponent* C : SMs)
	{
		// Меш оружия и ★ 25.10.2026 меш щита: оба загрузчика эталона получают один указатель видимости `char+0x32EA0`.
		if (!C || !(C->ComponentHasTag(UAsdaCharAssembly::WeaponMeshTag) || C->ComponentHasTag(UAsdaCharAssembly::ShieldMeshTag))) { continue; }
		if (bHide && !C->bHiddenInGame)
		{
			C->SetHiddenInGame(true);
			C->ComponentTags.AddUnique(kFishHiddenTag);
			FishHiddenParts.AddUnique(TWeakObjectPtr<UObject>(C));
			++nMesh;
		}
		else if (!bHide && C->ComponentHasTag(kFishHiddenTag))
		{
			C->ComponentTags.Remove(kFishHiddenTag);
			C->SetHiddenInGame(false);
			++nMesh;
		}
	}
	static const FName kWeaponGlow(AsdaCharFxTags::kWeaponGlow);   // ★ В1-C F6: общий тег
	static const FName kShieldGlow(AsdaCharFxTags::kShieldGlow);
	TArray<AActor*> Attached;
	GetAttachedActors(Attached);
	for (AActor* A : Attached)
	{
		if (!A) { continue; }
		const bool bW = A->ActorHasTag(kWeaponGlow);
		const bool bS = A->ActorHasTag(kShieldGlow);
		if (!bW && !bS) { continue; }
		if (bHide && !A->IsHidden())
		{
			A->SetActorHiddenInGame(true);
			A->Tags.AddUnique(kFishHiddenTag);
			FishHiddenParts.AddUnique(TWeakObjectPtr<UObject>(A));
			(bW ? nWeaponFx : nShieldFx) += 1;
		}
		else if (!bHide && A->Tags.Contains(kFishHiddenTag))
		{
			A->Tags.Remove(kFishHiddenTag);
			// Носитель под GM-скрытием (метка `kAsdaGmHiddenTagName`) — не показываем, а передаём скрытие системе GM: её
			//   метка `AsdaGmHiddenByHost` вернёт эффект при показе носителя (`AAsdaEffectActor::Tick`). Иначе — показать.
			//   ★ 24.10.2026: то же для невидимости навыка — общая проверка «скрыт любой системой» (`AsdaHostHiddenByTag`).
			if (IsHidden() && AsdaHostHiddenByTag(this)) { A->Tags.AddUnique(FName(TEXT("AsdaGmHiddenByHost"))); }
			else { A->SetActorHiddenInGame(false); }
			(bW ? nWeaponFx : nShieldFx) += 1;
		}
	}
	if (!bHide) { FishHiddenParts.Reset(); FishHideSig = -1; }
	if (bFishWeaponHidden != bHide || nMesh + nWeaponFx + nShieldFx > 0)
	{
		UE_LOG(LogAsdaPlayer, Log, TEXT("[рыбалка] оружие и щит %s (char[52136]): меши оружия/щита %d, свечение оружия %d, аура щита %d"),
			bHide ? TEXT("скрыты") : TEXT("снова видны"), nMesh, nWeaponFx, nShieldFx);
	}
	bFishWeaponHidden = bHide;
}

// Покадрово — эталон `LocalPlayer_UpdateRender@0x9D4170` (game.c:55024-55056, свой) и `CharManager_UpdateAll@0x9D56C0`
//   (game.c:55827-55870, чужой), затем посадка `CharFx_UpdateAttachedEffects@0xFCE3A0` / `AttachedObject_UpdateAndRender@0xFD1B30`.
void AAsdaPlayerCharacter::TickFishAttachments(float DeltaSeconds)
{
	// ── чужой с удочкой в состоянии ≤ 2 / 5 / 6 (покой, движение, замах, смерть): флаг [15036] := 0 (game.c:607922) ──
	//   ★ В5fix (ревизия В5, МЕЛК-2): ТОЛЬКО эти состояния, как эталон. Эмоция (33…46), сидение (7/8/9) и навык (24)
	//   флаг не снимают — удочка у такого соседа остаётся (у эталона проверка `state <= 2 || state == 5 || state == 6`).
	//   ★ В45fix2 (МЕЛК-5): состояние 68 «добыча» тоже флаг не снимает; навык (24) и замах (5) переписывают 68 — признак гасим
	//   здесь (общие точки показа навыка/замаха ставят свои признаки времени, а не зовут `SetAnimState`).
	// ★ 09.10: лопата 40/43 — так же. ★ В3 S7: из единого поля.
	if (IsEtalonState68() && (IsSkillAnimActive() || IsAttackAnimActive())) { SetBusyAction(EAsdaActionState::Idle, TEXT("навык/замах переписал 68")); }
	if (GetEtalonShovelState() != 0 && (IsSkillAnimActive() || IsAttackAnimActive())) { ClearEtalonShovelState(); }
	// ★ В3 S7: «не рыбалка, не эмоция, не 68, не лопата» = «не занят» (`IsEtalonActionBusy`) — то же условие одним предикатом.
	if (!bFishOwnerSelf && bFishRodInserted && bFishFlag && !IsEtalonActionBusy()
		&& !IsSkillAnimActive() && SitPhase == 0 && AnimState != EAsdaPlayerState::Sit)
	{
		bFishFlag = false;
		UE_LOG(LogAsdaPlayer, Log, TEXT("[рыбалка] чужой вышел из действия рыбалки — флаг снят (game.c:607922)"));
	}
	// ── удочка ──
	// ★ В5fix (ревизия В5, МЕЛК-1): перезагрузки «по смене удочки» у эталона НЕТ. Проверка `[15088] != [15084]`
	//   (game.c:55027 свой / 55830 чужой) стоит, но поле [15084] (+0x3733C) оригинал НИКОГДА не пишет: два метода —
	//   (1) декомпил: запись только копией персонаж→персонаж `charCopyFromNetStruct` (game.c:528974-528975, обе ячейки
	//   из одного источника, пара остаётся равной) и `[15088] = [15084]` в `CFishing_LoadRodAnimations` (game.c:607585);
	//   (2) скан `.text` по смещениям 0x3733C/0x37340: чтения 0x9D4285, 0x9D60DF, 0xFCD732, копия 0xEFF3A6/0xEFF3AC,
	//   запись [15088] 0xFCD738 и 0xEFF3BE; иных записей нет. Итог оригинала: модель удочки меняется только через
	//   снятие по флагу 0 и новую вставку (меш берётся из `char+0x835` в момент вставки). Повторяем это. [ОРИГИНАЛ]
	if (!bFishRodInserted && bFishFlag)
	{
		bool bWant = false;
		if (bFishOwnerSelf) { bWant = true; }        // свой: по флагу (game.c:55032)
		else if (IsFishActionActive() && EquippedWeaponItemId > 0)   // чужой: действие 60…67 И в руке удочка 10/1
		{
			const UAsdaItemTableSubsystem* Items = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
			const FAsdaItemRecord* R = Items ? Items->GetItem(EquippedWeaponItemId) : nullptr;
			bWant = R && R->ItemSubType == UAsdaFishingSubsystem::kRodSubType && R->ItemVariant == UAsdaFishingSubsystem::kRodVariant;
		}
		if (bWant) { InsertFishRod(); }
	}
	// (Чужой без флага: вставка эталона тут же снимается следующей проверкой того же кадра — сразу не вставляем.)
	if (bFishRodInserted && !bFishFlag) { RemoveFishRod(); }
	// ── рыба ──
	if (bFishHookInserted && FishHookLoadedCode != FishCode) { RemoveFishHook(); }        // 15080 ≠ 15076
	if (!bFishHookInserted && bFishFlag && (bFishOwnerSelf || IsFishActionActive())) { InsertFishHook(); }
	if (bFishHookInserted && !bFishFlag) { RemoveFishHook(); }

	// ── посадка удочки: кость `8a1_dummy` удочки совмещается с костью `8_a1` персонажа:
	//    world = inv(dummy) · bone(8_a1) · charWorld (game.c:607927-607950). Дамми берётся из ТЕКУЩЕЙ позы клипа. ──
	if (FishRodComp && FishRodComp->GetBoneIndex(kFishRodDummyBone) != INDEX_NONE)
	{
		const FTransform Dummy = FishRodComp->GetSocketTransform(kFishRodDummyBone, RTS_Component);
		FishRodComp->SetRelativeTransform(Dummy.Inverse());
	}
	// ── посадка рыбы (game.c:609642-609690): 62 → кость `fs_Bone01` на кончик лески `Bone24`, 65 → на кость `_8_a1`
	//    персонажа с поворотом RotZ(−π/2); масштаб = [15068]/100, мусор (тип 4) — 2.0; видна только на треках 29/32
	//    (действия 62/65) при [15066] == 1 (и при существующей удочке — без неё эталон позу не пересчитывает). ──
	if (FishHookComp)
	{
		const int32 A = GetFishAction();
		float S = (float)FishSizePct / 100.f;
		if (const UAsdaFishingSubsystem* Fish = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaFishingSubsystem>() : nullptr)
		{
			if (const FAsdaFishDataRow* Row = Fish->FindFishData(FishCode)) { if (Row->FishType == 4) { S = 2.f; } }
		}
		// Без удочки: [ИСПРАВЛЕНИЕ] скрыта; ключ `asda.Fish.LegacyHookNoRod 1` — как оригинал (видна, поза не пересчитывается).
		const bool bLegacyNoRod = !FishRodComp && CVarFishLegacyHookNoRod.GetValueOnGameThread() != 0;
		const bool bVis = bLegacyNoRod || (FishRodComp && (A == 62 || A == 65) && bFishHasFish);
		FMatrix Inv = FMatrix::Identity;
		if (FishHookComp->GetBoneIndex(kFishHookBone) != INDEX_NONE)
		{
			Inv = FishHookComp->GetSocketTransform(kFishHookBone, RTS_Component).ToMatrixWithScale().Inverse();
		}
		if (bLegacyNoRod) {}   // эталон без удочки позу не трогает
		else if (A == 65 && BaseSkel && BaseSkel->GetBoneIndex(kFishCharOffHandBone) != INDEX_NONE)
		{
			if (FishHookComp->GetAttachParent() != BaseSkel || FishHookComp->GetAttachSocketName() != kFishCharOffHandBone)
			{
				FishHookComp->AttachToComponent(BaseSkel, FAttachmentTransformRules::KeepRelativeTransform, kFishCharOffHandBone);
			}
			FishHookComp->SetRelativeTransform(FTransform(Inv * FScaleMatrix(FVector(S)) * AsdaFishInHandRot()));
		}
		else if (FishRodComp && FishRodComp->GetBoneIndex(kFishRodTipBone) != INDEX_NONE)
		{
			if (FishHookComp->GetAttachParent() != FishRodComp || FishHookComp->GetAttachSocketName() != kFishRodTipBone)
			{
				FishHookComp->AttachToComponent(FishRodComp, FAttachmentTransformRules::KeepRelativeTransform, kFishRodTipBone);
			}
			FishHookComp->SetRelativeTransform(FTransform(Inv * FScaleMatrix(FVector(S))));
		}
		if (FishHookComp->bHiddenInGame == bVis) { FishHookComp->SetHiddenInGame(!bVis); }
	}

	// ── оружие и щит на время рыбалки: `charEntityUpdateTick@0xA1ED30` (game.c:80805-80810) — при `bActionBusy == 1`
	//    и флаге рыбалки `char[52136] = 0` (видимость частей оружия И щита, их свечения и ауры щита — разбор у
	//    `ApplyFishWeaponHide`). Проход внутри — только по смене состояния или пересборке частей. ──
	//    Условие эталона — `bActionBusy == 1` (`self[52164]`) И флаг [15036], а не «идёт действие 60…67»: признак «занят»
	//    в порту ставят действие рыбалки и эмоция (сосед с флагом в эмоции — флаг она не снимает, МЕЛК-2 — тоже без оружия).
	//    ★ В45fix2 (ревизия В4/В5, МЕЛК-6, 09.10.2026): «ЗАЛИПАНИЕ» эталона повторено. В ветке `bActionBusy == 1`
	//    (g:80805-80813) поле 52136 только ГАСИТСЯ при флаге рыбалки/копки, а при флаге 0 НЕ возвращается в 1 — остаётся
	//    прежним; показ (`= 1`) — только в ветках «не занят» (g:80815-80835). Пример: сосед в 61 получил 6173 с фазой 0
	//    (флаг 0, смены состояния нет) — оружие остаётся скрытым до конца «занят». Прежде порт показывал его сразу. [ОРИГИНАЛ]
	//    Признак «занят» порта: действие рыбалки, эмоция, состояние 68 «добыча» (МЕЛК-5).
	const bool bBusy = IsEtalonActionBusy();   // ★ 09.10: + лопата 40/43; ★ В3 S7: одно состояние вместо 4 признаков
	ApplyFishWeaponHide(bBusy ? (bFishFlag || bFishWeaponHidden) : false);
}
