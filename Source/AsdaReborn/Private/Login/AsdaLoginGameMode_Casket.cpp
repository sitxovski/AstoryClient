// Copyright (c) 2026 @sitxovski (Discord)
// AsdaLoginGameMode_Casket.cpp — ★ R3-c 12.11.2026 (разрез AsdaLoginGameMode.cpp, дословный перенос): сцена: шкатулка, экспозиция камеры, свет, полёт камеры, последовательности кузницы.
//   Общее для частей — в AsdaLoginGameMode_Private.h; шапка и реверс — в AsdaLoginGameMode.cpp.
#include "Login/AsdaLoginGameMode.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Resources/AsdaMaterialNames.h"   // ★ дедуп №2 В1-C (F10/F16): пути мастеров, имена параметров, экранные ассеты
#include "Resources/AsdaSoundSubsystem.h"
#include "Resources/AsdaModelAnimSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Login/AsdaText.h"          // FAsdaText::Strip (срез JUI-разметки)
#include "UI/AsdaMarkup.h"           // UAsdaRichText::ApplyFormatted — единая точка форматированного текста
#include "UI/AsdaTipPanel.h"         // UAsdaTipPanel::CountWrappedLines — единственный счётчик строк с переносом
// ★ Аудит №4 (A8): `Login/AsdaGameText.h` больше не нужен — строка помощника теперь
//   `FAsdaLoginhelpertextRow` из `Items/AsdaItemDbRows.h` (тянется через AsdaItemTableSubsystem.h).
#include "Blueprint/WidgetTree.h"    // GetAllWidgets — поиск план-виджетов по префиксу (имена с idx-суффиксом)
#include "Login/AsdaLoginConfig.h"
#include "LevelSequence.h"
#include "LevelSequencePlayer.h"
#include "LevelSequenceActor.h"
#include "MovieSceneSequencePlaybackSettings.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/WidgetTree.h"
#include "AsdaOutgoingGate.h"           // [САМОТЕСТ] 08.10.2026: офлайн-гейт asda.Login.CreateNameTest
// ── Ctrl+N-пикер разрешения/качества (dev) ──
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"       // FWidgetPath::ContainsWidget — маппинг focus-события на наш контрол
#include "Kismet/GameplayStatics.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Misc/Paths.h"
#include "AsdaNetSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"                 // FScreenshotRequest (движковый захват вместо RDP CopyFromScreen)
#include "GameFramework/PlayerController.h"
#include "Animation/SkeletalMeshActor.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "AsdaEffectActor.h"
#include "EngineUtils.h"
#include "Engine/Light.h"                      // ★ 31.10.2026: ALight — световые акторы уровня (AsdaLoginFreezeBlueprintLights)
#include "Components/LightComponent.h"         // ★ 31.10.2026: ULightComponent::Intensity в лог
#include "Engine/BlueprintGeneratedClass.h"    // ★ 31.10.2026: признак Blueprint-наследника света
#include "Camera/CameraComponent.h"   // ★ 31.10.2026: снятие своих настроек экспозиции у камер логина
#include "World/AsdaPlayerCharacter.h" // ★ общая точка kAsdaCharPreviewBaseM/F (база пути превью-скелета)
#include "World/AsdaLoadManagerSubsystem.h" // ★ charsel_preload: фоновая предзагрузка превью экрана выбора
#include "PipelineStateCache.h"             // ★ charsel_preload: IsPSOPrecachingEnabled — прогрев PSO только где он есть
#include "AsdaLoginGameModeInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaLoginGameMode*.cpp (LogAsdaLogin, чистые хелперы)
#include "AsdaLoginGameMode_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaLoginGameMode*.cpp

static const TCHAR* GCasketOpenAnimPath =
	TEXT("/Game/AsdaAssets/Models/Char/cre_creation01-a_00/SkeletalMeshes/cre_creation01-a_00anim_00");

void AAsdaLoginGameMode::FreezeCasketClosed()
{
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_CASKET_PLAY")) == TEXT("1")) return; // ТЕСТ: не морозить
	if (!CasketActor) return;
	USkeletalMeshComponent* C = CasketActor->GetSkeletalMeshComponent();
	UAnimSequence* Open = LoadClip(GCasketOpenAnimPath);
	if (!C || !Open) return;
	C->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	C->SetAnimation(Open);
	C->Stop();
	C->SetPosition(0.f, false);    // кадр 0 клипа _0 = ЗАКРЫТА (jbm-верты авторены открытыми)
}

const UAsdaLoginConfig* AAsdaLoginGameMode::ResolveConfig()
{
	if (!LoginConfig)
	{
		// лениво подгрузить дефолтный ассет-конфиг (если создан); иначе останется nullptr → C++-дефолты
		LoginConfig = LoadObject<UAsdaLoginConfig>(nullptr, TEXT("/Game/AsdaAssets/Config/DA_LoginConfig.DA_LoginConfig"));
	}
	return LoginConfig;
}

void AAsdaLoginGameMode::FireCasketCues(const TArray<FAsdaCasketSoundCue>& Cues, float TimeSec, uint32& Mask)
{
	const UAsdaLoginConfig* Cfg = ResolveConfig();
	const float Fps = (Cfg && Cfg->AnimFrameRate > 1.f) ? Cfg->AnimFrameRate : 30.f;
	const float Frame = TimeSec * Fps;
	for (int32 i = 0; i < Cues.Num() && i < 32; ++i)
	{
		if (!(Mask & (1u << i)) && Frame >= (float)Cues[i].Frame)
		{
			Mask |= (1u << i);
			PlayCreationSound(Cues[i].CreationSound);
		}
	}
}

void AAsdaLoginGameMode::PlayCreationSound(int32 CreationIdx)
{
	// Эталон csoundPlayName2DStore(g_pSoundMgr, id, 0) — 2D-звук ПО ЧИСЛОВОМУ ID. id 45..54 = S_e_creation01..10
	//   (SFT.BIN → DT_SoundTable). creationIdx 1..10 → soundId 44+idx. Звук-менеджер резолвит id→имя из таблицы.
	if (CreationIdx < 1 || CreationIdx > 10) return;
	if (UAsdaSoundSubsystem* SoundMgr = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		SoundMgr->PlaySound2DById(44 + CreationIdx, /*bLoop*/false);
	}
}

void AAsdaLoginGameMode::StartCasketTurn(bool bRight)
{
	// Запуск клипа поворота карусели-шкатулки. Эталон charSelect_PlaySlotAnim@0xF9F090 (charselect_present_fix.md §E):
	//   mode 2 (влево)  → трек 8-curSlot   → anim_08/07/06 для curSlot 0/1/2
	//   mode 3 (вправо) → трек curSlot+9   → anim_09/10/11 для curSlot 0/1/2
	//   curSlot = текущий SelectedSlot; активный слот сменится ПО ЗАВЕРШЕНИИ клипа (эталон uiCyclePageThreeState:
	//   влево --curSlot, вправо ++curSlot). idle-поза = трек curSlot (anim_00/01/02 — своя поза на каждый слот).
	if (!bCasketOpened || bTurnPlaying || !CasketActor) return;
	USkeletalMeshComponent* C = CasketActor->GetSkeletalMeshComponent();
	if (!C) return;

	const int32 CurSlot = SelectedSlot;                          // 0..2 — какой из 3 клипов поворота
	const int32 ClipIdx = bRight ? (CurSlot + 9) : (8 - CurSlot);   // вправо anim_09..11 / влево anim_08..06 (8-curSlot)
	PendingSlot = bRight ? (SelectedSlot + 1) % 3 : (SelectedSlot + 2) % 3;   // слот ПОСЛЕ поворота

	const FString TurnPath = FString::Printf(
		TEXT("/Game/AsdaAssets/Models/Char/cre_creation01-a_00/SkeletalMeshes/cre_creation01-a_00anim_%02d"), ClipIdx);
	UAnimSequence* Turn = LoadClip(TurnPath);
	if (!Turn)
	{
		UE_LOG(LogAsdaLogin, Warning, TEXT("Клип поворота anim_%02d не загружен — пропуск"), ClipIdx);
		SelectedSlot = PendingSlot; UpdateCharSelectInfo();      // без анимации — хотя бы сменить слот
		return;
	}

	C->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	C->SetAnimation(Turn);
	C->SetPosition(0.f, false);
	if (UAnimSingleNodeInstance* SN = C->GetSingleNodeInstance())
	{
		SN->SetLooping(false);
		TurnDur = Turn->GetPlayLength();                         // точная длительность (2.0с/61 кадр)
	}
	C->Play(false);                                             // один раз, НЕ зациклено
	bTurnPlaying = true;
	TurnTime = 0.f;
	TurnSndMask = 0;                                            // сброс латча звука поворота

	// ★ Перепривязать контент слотов к костям пьедестала ПОВОРОТА (T2 влево / T3 вправо) на текущем curSlot —
	//   иначе контент едет по кости idle-маппинга (T0) → неверная дуга/«основной персонаж на неверной кости».
	//   Кость анимируется клипом поворота, привязанный контент следует за ней (UE-socket).
	//   ★★★ ИСПРАВЛЕНО 02.09.2026 (жалоба владельца: «при прокрутке виден силуэт не того пола, при
	//   остановке персонаж и силуэт моргают, силуэт меняется»). Раньше на кость поворота переезжал
	//   ТОЛЬКО мой персонаж (`SlotContent`), а партнёр и «душа» слота оставались на кости idle-раскладки
	//   T0. Таблицы T0/T2/T3 для одного слота дают РАЗНЫЕ кости (например, curSlot 1: T0 = CH_002,
	//   T3 = CH_003), поэтому во время клипа мой персонаж ехал на одной кости, а его силуэт — на
	//   другой, вместе с ЧУЖИМ персонажем слота (у которого другой пол ⇒ «не тот силуэт»). По концу
	//   клипа `ReattachCarousel` рывком возвращал всех на T0[нового слота] — это и есть «моргание» и
	//   «смена силуэта». Пол/клип силуэта при этом НЕ пересчитывались — менялось лишь то, ЧЕЙ силуэт
	//   стоял рядом. Теперь партнёр и «душа» едут на ТОЙ ЖЕ кости, что и мой персонаж, всю дорогу:
	//   эталон держит их дочерними узлами моего объекта (`posSetWithChildOffset` двигает всю тройку
	//   одной матрицей), то есть у них по определению одна и та же кость пьедестала.
	{
		const int32 RotState = bRight ? 3 : 2;
		for (int32 i = 0; i < SlotContent.Num() && i < 3; ++i)
		{
			AActor* A = SlotContent[i];
			if (!A) continue;
			const FName Bone = CarouselPedestalBone(i, CurSlot, RotState);
			if (C->DoesSocketExist(Bone))
			{
				A->AttachToComponent(C, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Bone);
			}
			AttachSlotCompanionsToBone(i, C, Bone);   // партнёр + «душа» — на ту же кость, что и мой
		}
	}

	// Звук начала поворота (эталон csoundPlayName2DStore(...,53) на кадре 0) — если ассет есть, опционально.
	UE_LOG(LogAsdaLogin, Log, TEXT("Поворот шкатулки %s: anim_%02d (curSlot=%d → slot=%d), %.2fс"),
		bRight ? TEXT("вправо") : TEXT("влево"), ClipIdx, CurSlot, PendingSlot, TurnDur);
}

void AAsdaLoginGameMode::FinishCasketTurn()
{
	// Клип поворота отыгран (эталон Game_IsPausedOrMenu → uiCyclePageThreeState меняет активный слот).
	//   Сменить активный слот, обновить инфо-панель и вернуть шкатулку в статичную idle-позу открытой (anim_00,
	//   последний кадр = полностью открыта). Персонажи на костях CH_00N уже в новых позициях после клипа.
	bTurnPlaying = false;
	SelectedSlot = PendingSlot;
	ReattachCarousel();   // ★ перепривязать контент к костям пьедестала нового curSlot (front/next/prev) — не «за активным»
	UpdateCharSelectInfo();

	if (CasketActor)
	{
		if (USkeletalMeshComponent* C = CasketActor->GetSkeletalMeshComponent())
		{
			// ★ idle-поза = трек curSlot (эталон PlaySlotAnim mode 0): anim_00/01/02 — платформа развёрнута так,
			//   что ВЫБРАННЫЙ слот стоит по центру-фронту. Раньше всегда anim_00 → витрина не проворачивалась.
			const FString IdlePath = FString::Printf(
				TEXT("/Game/AsdaAssets/Models/Char/cre_creation01-a_00/SkeletalMeshes/cre_creation01-a_00anim_%02d"), SelectedSlot);
			UAnimSequence* Idle = LoadClip(IdlePath);
			if (!Idle) { Idle = LoadClip(GCasketOpenAnimPath); }   // фолбэк anim_00
			if (Idle)
			{
				C->SetAnimationMode(EAnimationMode::AnimationSingleNode);
				C->SetAnimation(Idle);
				C->Stop();
				C->SetPosition(Idle->GetPlayLength(), false);   // последний кадр = ОТКРЫТА, слот по центру
			}
		}
	}
	UE_LOG(LogAsdaLogin, Log, TEXT("Поворот завершён → активный слот %d (idle anim_%02d)"), SelectedSlot, SelectedSlot);
}

// ★ 31.10.2026 (владелец): ЭКСПОЗИЦИЯ ЛОГИНА — У КАРТЫ, как в мире (28.10, `AsdaApplyMapExposure`). Камеры сцены
//   логина (LoginCamera и прочие ACameraActor уровня) несли свои поля экспозиции и перекрывали объём пост-обработки
//   Login.umap — владелец не мог выставить яркость сцены в редакторе. Здесь во время игры у камер снимаются ВСЕ
//   флаги переопределения экспозиции (автоэкспозиция, кривая, гистограмма, физическая камера ISO/выдержка,
//   локальная экспозиция); значения в уровне не меняются — Login.umap не трогаем (защищённая сцена владельца).
//   Ключ: `asda.Login.CamExposureFromMap` (1 — экспозиция от объёма карты; 0 — как раньше, камера со своей).
static ASDA_DEV_CVAR(int32, CVarAsdaLoginCamExposureFromMap,
	TEXT("asda.Login.CamExposureFromMap"), 1,
	TEXT("Экспозиция сцены логина: 1 = у камер сняты свои настройки экспозиции, яркость задаёт объём пост-обработки карты ")
	TEXT("(по умолчанию, решение владельца 31.10.2026); 0 = камеры со своей экспозицией, как раньше."),
	ECVF_Default);

// Снять у всех камер уровня логина флаги переопределения экспозиции (только в памяти, уровень не сохраняется).
static void AsdaLoginDropCameraExposure(UWorld* World)
{
	if (!World || CVarAsdaLoginCamExposureFromMap.GetValueOnGameThread() == 0) { return; }
	int32 NumCams = 0;
	for (TActorIterator<ACameraActor> It(World); It; ++It)
	{
		UCameraComponent* Cam = It->GetCameraComponent();
		if (!Cam) { continue; }
		FPostProcessSettings& P = Cam->PostProcessSettings;
		P.bOverride_AutoExposureMethod = false;
		P.bOverride_AutoExposureBias = false;
		P.bOverride_AutoExposureBiasBackup = false;
		P.bOverride_AutoExposureBiasCurve = false;
		P.bOverride_AutoExposureMeterMask = false;
		P.bOverride_AutoExposureLowPercent = false;
		P.bOverride_AutoExposureHighPercent = false;
		P.bOverride_AutoExposureMinBrightness = false;
		P.bOverride_AutoExposureMaxBrightness = false;
		P.bOverride_AutoExposureSpeedUp = false;
		P.bOverride_AutoExposureSpeedDown = false;
		P.bOverride_AutoExposureApplyPhysicalCameraExposure = false;
		P.bOverride_HistogramLogMin = false;
		P.bOverride_HistogramLogMax = false;
		P.bOverride_CameraISO = false;
		P.bOverride_CameraShutterSpeed = false;
		P.bOverride_LocalExposureMethod = false;
		P.bOverride_LocalExposureHighlightContrastCurve = false;
		P.bOverride_LocalExposureShadowContrastCurve = false;
		P.bOverride_LocalExposureHighlightContrastScale = false;
		P.bOverride_LocalExposureShadowContrastScale = false;
		P.bOverride_LocalExposureHighlightThreshold = false;
		P.bOverride_LocalExposureShadowThreshold = false;
		P.bOverride_LocalExposureHighlightThresholdStrength = false;
		P.bOverride_LocalExposureShadowThresholdStrength = false;
		P.bOverride_LocalExposureDetailStrength = false;
		P.bOverride_LocalExposureBlurredLuminanceBlend = false;
		P.bOverride_LocalExposureBlurredLuminanceKernelSizePercent = false;
		P.bOverride_LocalExposureMiddleGreyBias = false;
		++NumCams;
	}
	UE_LOG(LogAsdaLogin, Log, TEXT("[экспозиция] логин: у %d камер сняты свои настройки экспозиции — яркость задаёт объём пост-обработки карты"), NumCams);
}

// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (владелец: «сцена логина в игре темнее, а Holy Light пересвечен — сделай как в редакторе»):
//   свет логина в игре = свет, который владелец видит в редакторе. Blueprint `HolyLight_SpotLight` (Login.umap) каждый Tick
//   сам ставит силу света: своему лучу и «ближнему» SpotLight (переменная CloseUpLight) — по расстоянию до камеры
//   (MapRangeClamped × множитель). В редакторе Tick не идёт, и владелец ставит свет по значениям уровня; в игре же Tick их
//   перебивает: замер 31.10 из позы камеры логина — луч 1000 лм вместо 5 лм уровня (×200, пересвет), ближний свет 0 вместо
//   22.6 лм (передний план темнеет). Здесь у световых акторов уровня с Blueprint-тиком тик выключается ДО первого кадра —
//   действуют значения уровня, как в редакторе. Уровень и Blueprint не меняются.
//   Ключ: `asda.Login.LightsAsEditor` (1 — как в редакторе; 0 — Blueprint правит силу света в игре, как раньше).
static ASDA_DEV_CVAR(int32, CVarAsdaLoginLightsAsEditor,
	TEXT("asda.Login.LightsAsEditor"), 1,
	TEXT("Свет сцены логина как в редакторе: 1 = у световых акторов уровня с Blueprint-тиком (HolyLight_SpotLight) тик выключен, ")
	TEXT("действуют значения уровня (по умолчанию, 31.10.2026); 0 = Blueprint каждый кадр ставит силу света по расстоянию до камеры, как раньше."),
	ECVF_Default);

// Выключить Blueprint-тик у световых акторов уровня логина (вызов до первого тика мира; значения в памяти, уровень не сохраняется).
static void AsdaLoginFreezeBlueprintLights(UWorld* World)
{
	if (!World || CVarAsdaLoginLightsAsEditor.GetValueOnGameThread() == 0) { return; }
	int32 NumFrozen = 0;   // сколько световых Blueprint-акторов остановлено (в лог)
	for (TActorIterator<ALight> It(World); It; ++It)
	{
		ALight* L = *It;
		// Только Blueprint-наследники света с включённым тиком: штатные ALight не тикают, наши эффект-светы — не ALight
		if (!L || !Cast<UBlueprintGeneratedClass>(L->GetClass()) || !L->PrimaryActorTick.bCanEverTick) { continue; }
		L->SetActorTickEnabled(false);
		const ULightComponent* C = L->GetLightComponent();
		UE_LOG(LogAsdaLogin, Log, TEXT("[свет] логин: тик Blueprint-света %s выключен — сила %.2f (значение уровня, как в редакторе)"),
			*L->GetName(), C ? C->Intensity : -1.f);
		++NumFrozen;
	}
	UE_LOG(LogAsdaLogin, Log, TEXT("[свет] логин: Blueprint-светов с тиком остановлено %d"), NumFrozen);
}

void AAsdaLoginGameMode::SetupFlightSequence()
{
	// ★★★ ПЕРЕЛЁТ ВЫПОЛНЯЕТ КАМЕРА (13.09.2026, решение владельца). Раньше секвенция поссессила фон+шкатулку
	//   и везла их к неподвижной камере — и свет, поставленный владельцем в редакторе на шкатулку, в игре
	//   оставался на месте, а шкатулка уезжала. Теперь фон и шкатулка СТОЯТ в конечной позе перелёта
	//   (последний ключ трека = тождество, замер 13.09), а LS_LoginFlight ведёт LoginCamera:
	//       Cam(t) = Cam_fixed · S(t)⁻¹ · S_end        (порядок FTransform: сначала левый)
	//   Для зрителя перелёт тот же: поза камеры относительно сцены в каждом ключе совпадает с прежней
	//   (самопроверка — в Tools/asda_build_login_flight_sequence.py). Сцена в редакторе = сцена в игре.
	//   ⚠ Мировые солнце и небо при этом неподвижны относительно сцены (раньше сцена поворачивалась под ними),
	//   поэтому освещённость ВО ВРЕМЯ перелёта и на экране логина может отличаться — это цена схемы.
	//   ОТКАТ к прежней схеме: DA_LoginConfig.bFlightMovesCamera = false (тогда берётся FlightSequenceSceneMove =
	//   LS_LoginFlight_SceneMove) или env ASDA_LOGIN_FLIGHT_SCENE=1. Уровень для обоих режимов один: в режиме
	//   «сцена летит» секвенция сама задаёт позу сцены, камера стоит в Cam_fixed.
	//   Фолбэк на запечённый массив, если ассет не загрузился (регресс-защита) — в обоих режимах.
	bFlightSeqMode = false;
	const UAsdaLoginConfig* FlightCfg = ResolveConfig();
	bFlightMovesCamera = FlightCfg ? FlightCfg->bFlightMovesCamera : true;
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_LOGIN_FLIGHT_SCENE")) == TEXT("1")) { bFlightMovesCamera = false; }

	// Неподвижная камера прежней схемы = поза LoginCamera в уровне = поза камеры в ПОСЛЕДНЕМ кадре перелёта.
	//   Снимаем ДО прыжка секвенции в кадр 0 — в режиме камеры прыжок её сдвинет.
	FlightCamActor = nullptr;
	for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
	{
		if (It->GetAutoActivatePlayerIndex() == 0) { FlightCamActor = *It; break; }
	}
	FlightCamFixed = FlightCamActor.IsValid() ? FlightCamActor->GetActorTransform() : FTransform::Identity;
	FlightCamFixed.SetScale3D(FVector::OneVector);
	AsdaLoginDropCameraExposure(GetWorld());   // ★ 31.10.2026: экспозицию логина задаёт объём пост-обработки карты, не камера
	AsdaLoginFreezeBlueprintLights(GetWorld()); // ★ 31.10.2026: сила света — значения уровня, как в редакторе (Blueprint-тик HolyLight выкл)

	ULevelSequence* Seq = nullptr;
	if (FlightCfg)
	{
		const TSoftObjectPtr<ULevelSequence>& Ref = bFlightMovesCamera ? FlightCfg->FlightSequence : FlightCfg->FlightSequenceSceneMove;
		if (!Ref.IsNull()) { Seq = Ref.LoadSynchronous(); }
	}
	if (!Seq)
	{
		Seq = LoadObject<ULevelSequence>(nullptr, bFlightMovesCamera
			? TEXT("/Game/AsdaAssets/Cinematics/LS_LoginFlight.LS_LoginFlight")
			: TEXT("/Game/AsdaAssets/Cinematics/LS_LoginFlight_SceneMove.LS_LoginFlight_SceneMove"));
	}
	if (!Seq) { ApplyFlightPose(0.f); UE_LOG(LogAsdaLogin, Warning, TEXT("LS_LoginFlight не загружен — фолбэк на массив-трек")); return; }

	FMovieSceneSequencePlaybackSettings Settings;
	Settings.bAutoPlay = false;
	ALevelSequenceActor* SeqActor = nullptr;
	FlightPlayer = ULevelSequencePlayer::CreateLevelSequencePlayer(GetWorld(), Seq, Settings, SeqActor);
	if (!FlightPlayer) { ApplyFlightPose(0.f); UE_LOG(LogAsdaLogin, Warning, TEXT("Плеер LS_LoginFlight не создан — фолбэк")); return; }

	// поза дальнего кадра (frame0) до старта перелёта
	FlightPlayer->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(FFrameTime(FFrameNumber(0)), EUpdatePositionMethod::Jump));
	bFlightSeqMode = true;
	UE_LOG(LogAsdaLogin, Log, TEXT("Перелёт: LevelSequence %s (Editor-Friendly), режим %s, камера %s, поза frame0"),
		*Seq->GetName(), bFlightMovesCamera ? TEXT("ЛЕТИТ КАМЕРА") : TEXT("ЛЕТИТ СЦЕНА (откат)"),
		FlightCamActor.IsValid() ? *FlightCamFixed.GetLocation().ToString() : TEXT("НЕ НАЙДЕНА"));
}

void AAsdaLoginGameMode::ApplyFlightPose(float TimeSec)
{
	// Сэмпл трека якоря: лерп позиции + слерп кватерниона между соседними ключами.
	const float T = FMath::Clamp(TimeSec, 0.f, GLoginFlightTimes[GLoginFlightNum - 1]);
	int32 K = 0;
	while (K + 1 < GLoginFlightNum && GLoginFlightTimes[K + 1] < T) { ++K; }
	const int32 K2 = FMath::Min(K + 1, GLoginFlightNum - 1);
	const float T0 = GLoginFlightTimes[K], T1 = GLoginFlightTimes[K2];
	const float A = (T1 > T0) ? (T - T0) / (T1 - T0) : 0.f;
	const FVector L = FMath::Lerp(GLoginFlightLoc[K], GLoginFlightLoc[K2], A);
	const FQuat Q = FQuat::Slerp(GLoginFlightQuat[K], GLoginFlightQuat[K2], A);
	if (bFlightMovesCamera)
	{
		// ★ 13.09.2026: летит КАМЕРА, сцена стоит в S_end. Cam(t) = Cam_fixed · S(t)⁻¹ · S_end — та же формула,
		//   что у билдера секвенции. Масштаб сцены (зеркало шкатулки) в перелёт не входит: трансформы жёсткие.
		if (AActor* Cam = FlightCamActor.Get())
		{
			const FTransform St(Q, L);
			const FTransform SEnd(GLoginFlightQuat[GLoginFlightNum - 1], GLoginFlightLoc[GLoginFlightNum - 1]);
			FTransform C = FlightCamFixed * St.Inverse() * SEnd;
			C.SetScale3D(Cam->GetActorScale3D());
			Cam->SetActorTransform(C);
		}
		return;
	}
	// ОТКАТ «сцена летит»: прежнее поведение. ⚠ BackdropActor ищется по мешу cre_creback (старый скелетный
	//   задник, в уровне его нет — лог «задник=НЕТ» был и до 13.09), поэтому массив-фолбэк двигал только шкатулку.
	if (BackdropActor) { BackdropActor->SetActorLocationAndRotation(L, Q); }
	if (CasketActor)   { CasketActor->SetActorLocationAndRotation(L, Q); }
}

// ══ Секвенции шкатулки: ФОРДЖИНГ (create-успех) и ЗАКРЫТИЕ на back ══
// Эталон-форджинг (netHandleNameEntryDialogPacket case 1 → charApplyAppearanceChange@0xF09880 + тик
// entity_char.c:51418 по флагу 3075037, pageState 1→2→0): закрытие шкатулки (CLOSE-трек slot+3) →
// свечение eft_creation01 (+769360, «ковка» перса) → открытие (OPEN-трек slot) → фейд нового перса 1с.
// Камера (cameraUpdateCutsceneTransition@0xF0B540): на закрытии отлетает (dist 730→1700, pitch 0→−0.5,
// по прогрессу клипа), на открытии подлетает (1700→680) — в порту это реверс/повтор перелёта LS_LoginFlight,
// растянутый на длительность соответствующего клипа (та же пропорция «лерп по прогрессу клипа»).
// Эталон-back (net_sendLoginCharEnterOpcode1028@0xF0C420, флаг 3075044 → Game_SetState(2) по концу CLOSE):
// UI прячется сразу → шкатулка закрывается → камера уходит в дальнюю login-позу → потом список серверов/каналов.
float AAsdaLoginGameMode::FlightDurationSec() const
{
	if (bFlightSeqMode && FlightPlayer)
	{
		const float S = (float)FlightPlayer->GetDuration().AsSeconds();
		if (S > 0.f) { return S; }
	}
	return GLoginFlightTimes[GLoginFlightNum - 1];
}

float AAsdaLoginGameMode::GlowDurationSec() const
{
	// Эталон: свечение играет, пока эффект не «закончился» (sub_928D34) = один полный период eft_creation01.
	float Dur = 0.f;
	if (const AAsdaEffectActor* FX = Cast<AAsdaEffectActor>(CasketEffectActor))
	{
		for (const FAsdaFxEmitter& E : FX->Emitters) { Dur = FMath::Max(Dur, E.StartDelay + E.Period); }
	}
	return FMath::Clamp(Dur, 1.f, 10.f);   // защита от пустых/битых данных эмиттеров
}

void AAsdaLoginGameMode::SetForgeCamera(float Alpha)
{
	// Эталонная орбита (cameraUpdateCutsceneTransition@0xF0B540): камера остаётся НАЦЕЛЕННОЙ на шкатулку
	// (target game (0,187,0) → UE (0,0,187·GLoginScale), ось (x,−z,y)·GLoginScale; ★ 12.09: было ×100 = (0,0,18700)),
	// дистанция base→1700 game, подъём
	// elevation base→+0.5 рад (эталон g_fCameraPitch 0→−0.5 = камера ВЫШЕ, смотрит вниз). Азимут не меняется.
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	AActor* Cam = PC ? PC->GetViewTarget() : nullptr;
	if (!Cam) { return; }
	if (!bOrbitSaved)
	{
		OrbitCamActor = Cam;
		OrbitBaseLoc = Cam->GetActorLocation();
		OrbitBaseRot = Cam->GetActorRotation();
		bOrbitSaved = true;
	}
	const FVector T(0.f, 0.f, 187.f * GLoginScale);   // ★ 12.09: было 18700.f (187 game × 100)
	const FVector Off = OrbitBaseLoc - T;
	const float BaseDist = FMath::Max(Off.Size(), 1.f);
	const float BaseEl = FMath::Asin(FMath::Clamp(Off.Z / BaseDist, -1.f, 1.f));
	const float Az = FMath::Atan2(Off.Y, Off.X);
	const float Dist = FMath::Lerp(BaseDist, 1700.f * GLoginScale, Alpha);   // 1700 game (дальняя точка эталона); ★ 12.09: было 170000.f = 1700 × 100
	const float El   = FMath::Lerp(BaseEl, 0.5f, Alpha);
	const FVector Eye = T + FVector(FMath::Cos(Az) * FMath::Cos(El), FMath::Sin(Az) * FMath::Cos(El), FMath::Sin(El)) * Dist;
	Cam->SetActorLocation(Eye);
	Cam->SetActorRotation((T - Eye).Rotation());
}

void AAsdaLoginGameMode::RestoreForgeCamera()
{
	if (!bOrbitSaved) { return; }
	if (AActor* Cam = OrbitCamActor.Get())
	{
		Cam->SetActorLocation(OrbitBaseLoc);
		Cam->SetActorRotation(OrbitBaseRot);
	}
	bOrbitSaved = false;
}

void AAsdaLoginGameMode::StartFlightScaled(bool bReverse, float DurSec)
{
	if (!bFlightSeqMode || !FlightPlayer) { return; }   // массив-фолбэк ведёт TickCasketSequence через ApplyFlightPose
	const float FlightDur = FlightDurationSec();
	FlightPlayer->SetPlayRate((DurSec > KINDA_SMALL_NUMBER) ? FlightDur / DurSec : 1.f);
	if (bReverse)
	{
		// с текущей позы (после перелёта Keep State держит конец) назад к frame0
		FlightPlayer->PlayReverse();
	}
	else
	{
		FlightPlayer->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(FFrameTime(FFrameNumber(0)), EUpdatePositionMethod::Jump));
		FlightPlayer->Play();
	}
}

bool AAsdaLoginGameMode::PlayCasketClip(int32 ClipIdx, float& OutDur)
{
	USkeletalMeshComponent* C = CasketActor ? CasketActor->GetSkeletalMeshComponent() : nullptr;
	if (!C) { return false; }
	const FString Path = FString::Printf(
		TEXT("/Game/AsdaAssets/Models/Char/cre_creation01-a_00/SkeletalMeshes/cre_creation01-a_00anim_%02d"), ClipIdx);
	UAnimSequence* Clip = LoadClip(Path);
	if (!Clip)
	{
		UE_LOG(LogAsdaLogin, Warning, TEXT("Клип шкатулки anim_%02d не загружен"), ClipIdx);
		return false;
	}
	C->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	C->SetAnimation(Clip);
	C->SetPosition(0.f, false);
	if (UAnimSingleNodeInstance* SN = C->GetSingleNodeInstance()) { SN->SetLooping(false); }
	C->Play(false);   // один раз, холд последнего кадра
	OutDur = Clip->GetPlayLength();
	return true;
}

void AAsdaLoginGameMode::StartForgeSequence()
{
	if (CasketSeq != ECasketSeqPhase::None) { return; }
	// Эталон: UI прячется на всю секвенцию (ui3TabSetVisible(0)); старый контент слота живёт до конца свечения.
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(FName(TEXT("CHARSELECT_WIN")), false);
		WM->SetWindowVisible(FName(TEXT("WBP_ANI_WIN")), false);   // робот скрыт на время анимации (как на перелёте)
		// ★ Панель «одержимости» и ловец кликов по сцене живут ровно вместе с char-select.
		ShowSoulInfoPanel(false, -1);
		WM->SetWindowVisible(FName(TEXT("CHARSELECT_PICK")), false);
	}
	float Dur = 2.f;
	if (!PlayCasketClip(3 + (CreateSlot % 3), Dur))   // CLOSE-трек слота (эталон (slot%3+3)<<6)
	{
		// нет клипа закрытия → деградация до прежнего мгновенного поведения (spawn придёт по 1006)
		ShowCharSelectUI();
		return;
	}
	CasketSeq = ECasketSeqPhase::ForgeClose;
	CasketSeqTime = 0.f;
	CasketSeqDur = Dur;
	// Камера = орбита вокруг шкатулки (SetForgeCamera в TickCasketSequence по прогрессу клипа) — перелёт НЕ трогаем.
	UE_LOG(LogAsdaLogin, Log, TEXT("Форджинг: шкатулка ЗАКРЫВАЕТСЯ (anim_%02d, %.2fс) + камера-орбита вверх"),
		3 + (CreateSlot % 3), Dur);
}

// ★ R5 (12.11.2026, дубли): СНОС СЦЕНЫ ВЫБОРА ПЕРСОНАЖА при закрытии шкатулки — одна копия. До R5 эти восемь строк стояли
//   дословно в `StartBackSequence` (нет клипа закрытия → мгновенный путь) и в фазе `BackClose` (клип закрытия доигран).
//   Порядок действий тот же: акторы персонажей, сцена соулмейта, указатель выбранного, содержимое слотов, глифы,
//   закрытая поза шкатулки, флаг «открыта».
void AAsdaLoginGameMode::ClearCharSelectSceneClosed()
{
	for (AActor* A : CharacterActors) { if (A) { A->Destroy(); } }
	CharacterActors.Reset();
	DestroyPossessScene();   // ★ персонажи соулмейта живут ровно столько же, сколько мои
	CharacterActor = nullptr;
	for (int32 i = 0; i < SlotContent.Num(); ++i) { SlotContent[i] = nullptr; }
	for (AActor* G : GlyphActors) { if (G) { G->SetActorHiddenInGame(true); } }
	FreezeCasketClosed();       // конец close-клипа визуально = закрытая поза (кадр 0 клипа _0)
	bCasketOpened = false;
}

void AAsdaLoginGameMode::StartBackSequence()
{
	if (CasketSeq != ECasketSeqPhase::None || bTurnPlaying) { return; }
	// Эталон 1028-путь: UI прячется СРАЗУ (sub_92B6F6(0)+ui3TabSetVisible(0)+статус-панель), потом закрытие.
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(FName(TEXT("CHARSELECT_WIN")), false);
		// ★ Панель «одержимости» и ловец кликов по сцене живут ровно вместе с char-select.
		ShowSoulInfoPanel(false, -1);
		WM->SetWindowVisible(FName(TEXT("CHARSELECT_PICK")), false);
	}
	float Dur = 2.f;
	if (!PlayCasketClip(3 + (SelectedSlot % 3), Dur))
	{
		// нет клипа закрытия → прежнее мгновенное поведение (DIV-5 чистка + сразу server-select)
		ClearCharSelectSceneClosed();   // ★ R5: общий снос сцены выбора (персонажи, партнёры, глифы) + закрытая поза шкатулки
		bFlightActive = false;
		bTurnPlaying  = false;
		ShowServerSelect();
		ShowServerOrChannelView(true);
		return;
	}
	CasketSeq = ECasketSeqPhase::BackClose;
	CasketSeqTime = 0.f;
	CasketSeqDur = Dur;
	UE_LOG(LogAsdaLogin, Log, TEXT("Back: шкатулка ЗАКРЫВАЕТСЯ (anim_%02d, %.2fс)"), 3 + (SelectedSlot % 3), Dur);
}

void AAsdaLoginGameMode::TickCasketSequence(float DeltaSeconds)
{
	if (CasketSeq == ECasketSeqPhase::None) { return; }
	CasketSeqTime += DeltaSeconds;
	const float Prog = (CasketSeqDur > KINDA_SMALL_NUMBER) ? FMath::Clamp(CasketSeqTime / CasketSeqDur, 0.f, 1.f) : 1.f;
	// Форджинг: камера-орбита вокруг шкатулки по прогрессу клипа (эталонный лерп по прогрессу, кламп у клипа).
	switch (CasketSeq)
	{
	case ECasketSeqPhase::ForgeClose: SetForgeCamera(Prog); break;         // отлёт вверх (0→1)
	case ECasketSeqPhase::ForgeOpen:  SetForgeCamera(1.f - Prog); break;   // возврат вниз (1→0)
	default: break;
	}
	// Массив-фолбэк ОБРАТНОГО перелёта back-фазы (в seq-режиме плеер едет сам).
	if (!bFlightSeqMode && CasketSeq == ECasketSeqPhase::BackFlight)
	{
		ApplyFlightPose(FlightDurationSec() * (1.f - Prog));
	}
	if (CasketSeqTime < CasketSeqDur) { return; }
	switch (CasketSeq)
	{
	case ECasketSeqPhase::ForgeClose:
	{
		// Эталон pageState 1→2 (закрыто): charSlotClearEffects гасит глиф создания текущего слота,
		//   затем старт свечения — перезапуск эмиттеров eft_creation01 (vtbl+4 Play).
		if (GlyphActors.IsValidIndex(CreateSlot) && GlyphActors[CreateSlot]) { GlyphActors[CreateSlot]->SetActorHiddenInGame(true); }
		if (AAsdaEffectActor* FX = Cast<AAsdaEffectActor>(CasketEffectActor))
		{
			// Эталон: мировая матрица эффекта = ШКАТУЛКА (не слот!) — актор уже стоит на ней (place-скрипт),
			//   мировой скейл GLoginScale гарантирован в StartPlay (★ 12.09: был ×100), диски «между крышкой и основанием» лежат в самих
			//   .jem-вертах (Z 32..127 game). Показ ТОЛЬКО на время глоу + перезапуск эмиттеров (vtbl+4 Play).
			FX->SetActorHiddenInGame(false);
			FX->RestartEmitters();
		}
		CasketSeq = ECasketSeqPhase::ForgeGlow;
		CasketSeqTime = 0.f;
		CasketSeqDur = GlowDurationSec();
		UE_LOG(LogAsdaLogin, Log, TEXT("Форджинг: закрыто → СВЕЧЕНИЕ eft_creation01 (%.2fс)"), CasketSeqDur);
		break;
	}
	case ECasketSeqPhase::ForgeGlow:
	{
		// Эталон pageState 2→0: свечение закончилось → скрыть (тик рендерит +769360 ТОЛЬКО в pageState 2) → открытие.
		if (CasketEffectActor) { CasketEffectActor->SetActorHiddenInGame(true); }
		float Dur = 2.f;
		if (!PlayCasketClip(CreateSlot % 3, Dur)) { Dur = 0.01f; }   // OPEN-трек слота; без клипа — мгновенно
		CasketSeq = ECasketSeqPhase::ForgeOpen;
		CasketSeqTime = 0.f;
		CasketSeqDur = Dur;
		OpenSndMask = 0;   // звук открытия (кью case0) — как при входе в char-select
		UE_LOG(LogAsdaLogin, Log, TEXT("Форджинг: свечение → ОТКРЫТИЕ (anim_%02d, %.2fс) + камера-орбита вниз"),
			CreateSlot % 3, Dur);
		break;
	}
	case ECasketSeqPhase::ForgeOpen:
		// Эталон pageState 0 (открыто): спавн нового перса + фейд ~1с + UI (существующие механизмы).
		CasketSeq = ECasketSeqPhase::None;
		RestoreForgeCamera();       // камера точно в базовую позу char-select
		bCasketOpened = true;
		SpawnCharacterInSlot();     // свежий char-list уже пришёл за время анимации (SendCharListRequest при 1026)
		ShowCharSelectUI();
		StartPreviewFadeIn();
		UE_LOG(LogAsdaLogin, Log, TEXT("Форджинг ЗАВЕРШЁН: перс в слоте %d"), CreateSlot);
		break;
	case ECasketSeqPhase::BackClose:
		// Закрыто → снести контент слотов и лететь назад (реверс LS_LoginFlight — «обратная перемотка» входа).
		ClearCharSelectSceneClosed();   // ★ R5: общий снос сцены выбора (персонажи, партнёры, глифы) + закрытая поза шкатулки
		bTurnPlaying  = false;
		CasketSeq = ECasketSeqPhase::BackFlight;
		CasketSeqTime = 0.f;
		CasketSeqDur = FlightDurationSec();
		StartFlightScaled(true, CasketSeqDur);   // естественная скорость (rate 1)
		UE_LOG(LogAsdaLogin, Log, TEXT("Back: закрыто → ОБРАТНЫЙ перелёт (%.2fс)"), CasketSeqDur);
		break;
	case ECasketSeqPhase::BackFlight:
		// Прилетели в дальнюю login-позу → только теперь список серверов/каналов (порт-решение A2: этап канала).
		CasketSeq = ECasketSeqPhase::None;
		if (bFlightSeqMode && FlightPlayer) { FlightPlayer->SetPlayRate(1.f); }
		bFlightActive = false;
		ReleaseCharSelectPreload();   // ★ charsel_preload: ушли с экрана выбора — превью больше не нужны
		ShowServerSelect();
		ShowServerOrChannelView(true);
		UE_LOG(LogAsdaLogin, Log, TEXT("Back ЗАВЕРШЁН: server/channel-view"));
		break;
	default:
		CasketSeq = ECasketSeqPhase::None;
		break;
	}
}
