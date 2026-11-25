// Copyright (c) 2026 @sitxovski (Discord)
#include "Resources/AsdaVideoSettingsSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Npc/AsdaMobFxMaterial.h"   // ★ 01.11.2026 (mob_fx_audit): Custom Depth прозрачных подсеток при снятии свечения
#include "Resources/AsdaGameUserSettings.h"   // ★ 19.09.2026: яркость хранится в профиле игрока
#include "Resources/AsdaMaterialNames.h"   // ★ дедуп №2 В1-C (F10/F16): пути мастеров, имена параметров, экранные ассеты
#include "GameFramework/GameUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"      // asda.Video.Preset: подсистема через GameInstance контекста мира
#include "Engine/GameViewportClient.h"
#include "HAL/IConsoleManager.h"   // r.BloomQuality (LEVEL OF GLOW), asda.Video.AutoDetect, asda.Video.Preset
#include "Misc/ConfigCacheIni.h"   // маркер первого запуска [AsdaVideo] bInitialized, таблица [AsdaGlow]
#include "Scalability.h"           // ReapplyQuality / GetQualityPreset: живое состояние sg-уровней
#include "Engine/PostProcessVolume.h"   // ★ уровень свечения: служебный unbound-объём PP_Glow (сила ореола)
#include "Engine/World.h"               // ★ уровень свечения: спаун PP_Glow в текущем мире
#include "EngineUtils.h"                // ★ фаза 2: перебор мобов мира для трафарета выборочного свечения
#include "Components/MeshComponent.h"   // ★ фаза 2: пометка мешей мобов CustomStencil
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"               // ★ 19.09.2026 (glow_fix4): базовый мастер MID слота — прозрачность меша для веса свечения
#include "Npc/AsdaMobActor.h"           // ★ фаза 2: «Монстр» пункта свечения = мобы (эталон g_pMobMgr)
#include "Camera/CameraActor.h"        // ★ 19.09.2026: камера проверки `asda.Video.GlowSelect look`
#include "Camera/CameraComponent.h"    // ★ 19.09.2026: угол обзора камеры проверки (экранная рамка моба)
#include "GameFramework/PlayerController.h"   // ★ 19.09.2026: вид контроллера → камера проверки
#include "Components/DirectionalLightComponent.h"   // ★ 19.09.2026: `look sun` — направление на солнце (блики)
#include "Components/CapsuleComponent.h"   // ★ 19.09.2026: `look` — рамка тела моба по капсуле пикинга
#include "Components/SkeletalMeshComponent.h"   // ★ 19.09.2026: `look` — у моба есть видимая модель
#include "Components/InstancedStaticMeshComponent.h"   // ★ 19.09.2026: `look` — заслоны обзора по экземплярам ISM
#include "Engine/StaticMesh.h"   // ★ 19.09.2026: `look` — границы меша экземпляра ISM
#include "Components/StaticMeshComponent.h"       // ★ 19.09.2026: `look` — заслоны обзора и модель моба (статик-меши)
#include "Components/WidgetComponent.h"          // ★ 19.09.2026: плашки-виджеты в группу свечения не входят
#include "Engine/Level.h"                         // ★ 19.09.2026: акторы подгружаемого уровня (LevelAddedToWorld)
#include "World/AsdaPlayerCharacter.h"            // ★ 19.09.2026: первая группа — игрок (список 0x29080FC эталона)
#include "World/AsdaPetActor.h"                   // ★ 19.09.2026: первая группа — питомец (список 0x3518CC0)
#include "World/AsdaSoulGuardianActor.h"          // ★ 19.09.2026: первая группа — хранитель `CCharActor` (0x35180C0)
#include "World/AsdaProjectileActor.h"            // ★ 19.09.2026: первая группа — снаряд (`Projectile_LoadArrowModel` → 0x29080FC)
#include "World/AsdaRideVehicle.h"                // ★ 19.09.2026: [ПОРТ] машина пасхалки «Транспорт» — как езда эталона (`CCharacter_SetParts_RidingSystem` → 0x29080FC)
#include "UI/AsdaGachaSceneActor.h"               // ★ 19.09.2026: первая группа — автоматы гачи (`g_SceneRenderMgr` 0x2BDDA98)
#include "Login/AsdaLoginGameMode.h"              // ★ 19.09.2026 (С2): сцена логина = второй путь кадра эталона
#include "World/AsdaWorldKind.h"                   // ★ F7 (дедуп №2, В3): общий признак «сцена логина»
#include "AsdaEffectActor.h"                      // ★ 19.09.2026 (ревизия С1): эффекты предметов носителя — в его группе
#include "World/AsdaTargetHighlightComponent.h"   // ★ 19.09.2026 (МЕЛК): CustomDepth, который держит подсветка цели
#include "World/AsdaWorldSubsystem.h"             // ★ 19.09.2026: `asda.Video.GlowSelect fx` — эффекты предметов офлайн
#include "World/AsdaFadeComponent.h"              // ★ 19.09.2026: `asda.Video.GlowSelect fx … fade` — появление игрока
#include "World/AsdaMapRegistry.h"                // ★ 19.09.2026 (glow_fix5, С3): «туман-альфа» карты (.SCE) — вес свечения
#include "Misc/PackageName.h"                     // ★ 19.09.2026 (glow_fix5, С3): имя ассета уровня мира → запись реестра карт
#include "Materials/MaterialParameterCollection.h"          // ★ 10.10.2026: MPC_AsdaFx — альфа-тест прохода эффектов
#include "Materials/MaterialParameterCollectionInstance.h"  // ★ 10.10.2026: значение MPC в конкретном мире

DEFINE_LOG_CATEGORY_STATIC(LogAsdaVideo, Log, All);

// ★ E-0b (06.09.2026): автопробник железа на первом запуске (setE_plan_addendum §1.3 п.2, компромисс К7). 1 = RunHardwareBenchmark
//   решает уровень (на iGPU — Low + ResolutionQuality 71 %), 0 = без пробника, первый запуск = пресет по умолчанию. Риск Р2 дополнения:
//   ноутбук с гибридной графикой может замерить iGPU вместо dGPU.
//   ★ 12.10.2026 (решение владельца): по умолчанию 0 — первый запуск ВСЕГДА в пресете `kDefaultPreset` (High, «Высокое»);
//   пробник — только по явному ключу 1 или кнопкой «Определить автоматически».
//   Регистрация единственная в Source/ (grep "asda.Video." 06.09.2026 = 0; закон ue-console-duplicate-registration).
static int32 GAsdaVideoAutoDetect = 0;
static FAutoConsoleVariableRef CVarAsdaVideoAutoDetect(
	TEXT("asda.Video.AutoDetect"),
	GAsdaVideoAutoDetect,
	TEXT("1 = на ПЕРВОМ запуске уровень качества выбирает пробник железа (UGameUserSettings::RunHardwareBenchmark; пороги PerfIndexThresholds_* ")
	TEXT("BaseScalability.ini), 0 (умолчание с 12.10.2026) = без пробника — первый запуск в пресете по умолчанию (High, sg 2). ")
	TEXT("Действует только пока нет маркера [AsdaVideo] bInitialized."),
	ECVF_Default);

void UAsdaVideoSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// ★ Дефолт-видео — ТОЛЬКО при ПЕРВОМ запуске (маркер [AsdaVideo] bInitialized в GameUserSettings.ini).
	//   Иначе безусловный ApplyDefaultVideo затирал бы сохранённый выбор игрока из dev-меню Ctrl+N
	//   (разрешение/качество). На последующих стартах UGameUserSettings применяет движок сам, а
	//   CVar-оверрайды ([AsdaVideo]) накатывает UAsdaDevMenuSubsystem::ApplySavedSettings.
	bool bInit = false;
	if (GConfig) { GConfig->GetBool(TEXT("AsdaVideo"), TEXT("bInitialized"), bInit, GGameUserSettingsIni); }
	if (!bInit)
	{
		ApplyDefaultVideo();
		if (GConfig)
		{
			GConfig->SetBool(TEXT("AsdaVideo"), TEXT("bInitialized"), true, GGameUserSettingsIni);
			GConfig->Flush(false, GGameUserSettingsIni);
		}
	}
	UE_LOG(LogAsdaVideo, Log, TEXT("Старт: первый запуск=%d, пресет=%s%s, %s"), bInit ? 0 : 1, PresetName(GetQualityPreset()),
		IsQualityMixed() ? TEXT(" (группы вразнобой)") : TEXT(""), *DescribeQualityLevels());

	// ★★★ УРОВЕНЬ СВЕЧЕНИЯ. Сохранённый выбор игрока переприменяем КАЖДЫЙ старт.
	//   ★ 19.09.2026 (фаза 2): источник — поле `GlowLevel` ПРОФИЛЯ игрока (прежняя строка `[AsdaVideo] GlowLevel`
	//   dev-окна перенесена туда миграцией v2 и больше не читается). Сохранять тут нечего — значение уже на диске.
	{
		int32 Saved = GlowLevel;
		if (const UAsdaGameUserSettings* Profile = UAsdaGameUserSettings::Get()) { Saved = Profile->GlowLevel; }
		SetGlowLevel(Saved, /*bSave*/ false);
	}
	// Ступень должна доезжать до КАЖДОГО мира: смена карты и переход «логин → игра» создают новый мир, и
	//   транзиентный PP_Glow старого мира вместе с ним умирает. Момент выбран после инициализации акторов —
	//   спаун здесь безопасен, а BeginPlay ещё не прошёл, то есть первый же кадр рисуется уже с нужным ореолом.
	//   ★ 19.09.2026: делегат ОБЩИЙ для всех миров процесса — в PIE с несколькими клиентами подсистема
	//   берёт только миры СВОЕГО GameInstance (иначе клиент A добавил бы свой материал в объём клиента B).
	GlowWorldHandle = FWorldDelegates::OnWorldInitializedActors.AddLambda(
		[this](const UWorld::FActorsInitializedParams& P)
		{
			if (P.World && P.World->GetGameInstance() == GetGameInstance()) { ApplyGlowToWorld(P.World); }
		});
}

void UAsdaVideoSettingsSubsystem::Deinitialize()
{
	if (GlowWorldHandle.IsValid())
	{
		FWorldDelegates::OnWorldInitializedActors.Remove(GlowWorldHandle);
		GlowWorldHandle.Reset();
	}
	UnbindGlowMarks();   // ★ 19.09.2026: подписки мира и пометки групп свечения
	Super::Deinitialize();
}

static UGameUserSettings* GetUserSettings()
{
	return GEngine ? GEngine->GetGameUserSettings() : nullptr;
}

FIntPoint UAsdaVideoSettingsSubsystem::GetDesktopResolution() const
{
	if (UGameUserSettings* S = GetUserSettings())
	{
		const FIntPoint D = S->GetDesktopResolution();
		if (D.X > 0 && D.Y > 0) { return D; }
	}
	return FIntPoint(1920, 1080);
}

FIntPoint UAsdaVideoSettingsSubsystem::GetCurrentResolution() const
{
	if (UGameUserSettings* S = GetUserSettings())
	{
		return S->GetScreenResolution();
	}
	return GetOriginalDefaultResolution();
}

TArray<FIntPoint> UAsdaVideoSettingsSubsystem::GetAvailableResolutions() const
{
	TArray<FIntPoint> List;
	// Оригинал-набор (RESOLUTION_SYSTEM_RE.md): 4:3 (база 1024×768) + 16:9.
	static const FIntPoint Orig[] = {
		FIntPoint(1024, 768), FIntPoint(1280, 960), FIntPoint(1600, 1200),  // 4:3
		FIntPoint(1280, 720), FIntPoint(1600, 900), FIntPoint(1920, 1080),  // 16:9
	};
	for (const FIntPoint& R : Orig) { List.AddUnique(R); }

	// + поддерживаемые адаптером фуллскрин-режимы (реальные возможности железа).
	TArray<FIntPoint> Supported;
	UKismetSystemLibrary::GetSupportedFullscreenResolutions(Supported);
	for (const FIntPoint& R : Supported)
	{
		if (R.X >= 1024 && R.Y >= 768) { List.AddUnique(R); }   // не мельче базы UI
	}

	// Сортировка по площади (мелкие → крупные) для меню.
	List.Sort([](const FIntPoint& A, const FIntPoint& B) { return (int64)A.X * A.Y < (int64)B.X * B.Y; });
	return List;
}

void UAsdaVideoSettingsSubsystem::ApplyResolution(FIntPoint Resolution, int32 WindowMode)
{
	UGameUserSettings* S = GetUserSettings();
	if (!S || Resolution.X <= 0 || Resolution.Y <= 0) { return; }

	EWindowMode::Type Mode = EWindowMode::WindowedFullscreen;
	switch (WindowMode)
	{
		case 0: Mode = EWindowMode::Fullscreen; break;
		case 1: Mode = EWindowMode::WindowedFullscreen; break;
		case 2: Mode = EWindowMode::Windowed; break;
		default: break;
	}
	S->SetScreenResolution(Resolution);
	S->SetFullscreenMode(Mode);
	// ★ 19.09.2026 (фаза 2): масштаб рендера — ВЫБОР ИГРОКА (строка «Масштаб рендера» окна настроек), и смена
	//   разрешения/режима его больше НЕ сбрасывает. Прежде здесь стояло безусловное 100 %: сменил разрешение —
	//   потерял выбранный масштаб. Пересчёт при том же значении обновляет «желаемый размер» под новое окно.
	S->SetResolutionScaleValueEx(S->GetResolutionScaleNormalized() * 100.f > 0.f ? S->GetResolutionScaleNormalized() * 100.f : 100.f);
	S->ApplyResolutionSettings(false);
	S->ApplySettings(false);
	S->SaveSettings();
	UE_LOG(LogAsdaVideo, Log, TEXT("Разрешение применено: %dx%d режим=%d"), Resolution.X, Resolution.Y, WindowMode);
}

void UAsdaVideoSettingsSubsystem::ApplyDefaultVideo()
{
	// WindowedFullscreen на нативном разрешении рабочего стола: viewport заполняет окно (нет чёрных полей),
	//   полное качество. UI сам масштабируется DPI-кривой (ShortestSide база768).
	ApplyResolution(GetDesktopResolution(), /*WindowedFullscreen*/1);
	ApplyDefaultQuality();
}

// ───────────────────────── КАЧЕСТВО: маппинг оригинала → UE-scalability (RENDER_QUALITY_FIELDS_RE.md) ─────────────────────────

void UAsdaVideoSettingsSubsystem::SaveQuality()
{
	if (UGameUserSettings* S = GetUserSettings())
	{
		S->ApplyNonResolutionSettings();   // применяет sg.*-CVar'ы (текстуры/тени/эффекты/дистанция/…)
		S->SaveSettings();
	}
}

const TCHAR* UAsdaVideoSettingsSubsystem::PresetName(EAsdaQualityPreset P)
{
	switch (P)
	{
		case EAsdaQualityPreset::Low:       return TEXT("Low");
		case EAsdaQualityPreset::Medium:    return TEXT("Medium");
		case EAsdaQualityPreset::High:      return TEXT("High");
		case EAsdaQualityPreset::Epic:      return TEXT("Epic");
		case EAsdaQualityPreset::Cinematic: return TEXT("Cinematic");
		default:                            return TEXT("?");
	}
}

FString UAsdaVideoSettingsSubsystem::DescribeQualityLevels()
{
	// ★ М-1: печатаем и landscape — раньше его не было, а SingleLevelIgnoringResolution/GetQualityPreset его учитывают (несогласованность).
	const Scalability::FQualityLevels L = Scalability::GetQualityLevels();
	return FString::Printf(TEXT("sg: view=%d aa=%d shadow=%d gi=%d refl=%d pp=%d tex=%d fx=%d fol=%d shading=%d land=%d res=%.0f"),
		L.ViewDistanceQuality, L.AntiAliasingQuality, L.ShadowQuality, L.GlobalIlluminationQuality, L.ReflectionQuality,
		L.PostProcessQuality, L.TextureQuality, L.EffectsQuality, L.FoliageQuality, L.ShadingQuality, L.LandscapeQuality, L.ResolutionQuality);
}

void UAsdaVideoSettingsSubsystem::SetQualityPreset(EAsdaQualityPreset Preset)
{
	if (UGameUserSettings* S = GetUserSettings())
	{
		// ★ E-0b: уровень == значение enum (0..4). SetOverallScalabilityLevel ставит все группы разом; sg.ResolutionQuality не трогаем
		//   (масштаб рендера — отдельная ручка «Экран»/автопробник).
		// SetFromSingleQualityLevel внутри движка ставит и ResolutionQuality по PerfIndexValues (Low 50 %, Medium 71 %, High 87 %) —
		//   до E-0b это глушил пин r.ScreenPercentage=100; матрица §1: масштаб рендера = 100 на всех уровнях (71 % только автопробнику
		//   для iGPU) ⇒ масштаб сохраняем, пресет меняет только группы.
		const float KeepRes = S->GetResolutionScaleNormalized() * 100.f;
		S->SetOverallScalabilityLevel(PresetToLevel(Preset));
		S->SetResolutionScaleValueEx(KeepRes > 0.f ? KeepRes : 100.f);
		// ★★★ 12.10.2026 (прогон первого запуска с пресетом High): на старте подсистемы движок ещё НЕ инициализирован, а
		//   UGameUserSettings::ApplyNonResolutionSettings применяет уровни ТОЛЬКО «if (GEngine->IsInitialized())»
		//   (GameUserSettings.cpp:541) — SaveQuality() ниже молча ничего не применял, живые sg оставались движковыми 3,
		//   журнал писал «Пресет High … sg: view=3 …», а подбор владельца видел 3 и поднимал тени до 4. При прежнем умолчании
		//   Epic (= 3) это совпадало и не было заметно. Как и в ApplyOwnerQualityBaseline / штатном
		//   ApplyHardwareBenchmarkResults (GameUserSettings.cpp:1132-1143): уровни — напрямую + состояние в профиль.
		if (!(GEngine && GEngine->IsInitialized()))
		{
			Scalability::SetQualityLevels(S->ScalabilityQuality);
			Scalability::SaveState(GGameUserSettingsIni);   // [ScalabilityGroups] профиля игрока
		}
		SaveQuality();
		UE_LOG(LogAsdaVideo, Log, TEXT("Пресет качества: %s (scalability=%d); %s"), PresetName(Preset), PresetToLevel(Preset), *DescribeQualityLevels());
	}
}

EAsdaQualityPreset UAsdaVideoSettingsSubsystem::GetQualityPreset() const
{
	const Scalability::FQualityLevels L = Scalability::GetQualityLevels();
	int32 Level = SingleLevelIgnoringResolution(L);   // -1 если группы вразнобой
	if (Level < 0)
	{
		// ★ М-1 (review_setE0.md): вразнобой → максимум по ВСЕМ 11 группам (те же, что сравнивает SingleLevelIgnoringResolution) —
		//   раньше Shading/Landscape в максимуме не учитывались, и «смешанный» с высоким Shading округлялся вниз. Честно: IsQualityMixed() = true.
		Level = FMath::Max3(FMath::Max3(L.ViewDistanceQuality, L.AntiAliasingQuality, L.ShadowQuality),
		                    FMath::Max3(L.GlobalIlluminationQuality, L.ReflectionQuality, L.PostProcessQuality),
		                    FMath::Max3(FMath::Max3(L.TextureQuality, L.EffectsQuality, L.FoliageQuality),
		                                L.ShadingQuality, L.LandscapeQuality));
	}
	return (EAsdaQualityPreset)FMath::Clamp(Level, 0, 4);
}

bool UAsdaVideoSettingsSubsystem::IsQualityMixed() const
{
	return SingleLevelIgnoringResolution(Scalability::GetQualityLevels()) < 0;
}

int32 UAsdaVideoSettingsSubsystem::SingleLevelIgnoringResolution(const Scalability::FQualityLevels& L)
{
	// Движковый FQualityLevels::GetSingleQualityLevel (Scalability.cpp:1083-1097) требует ещё и ResolutionQuality == PerfIndexValues
	//   уровня (Low 50 %, Medium 71 %, High 87 %) — у нас масштаб рендера от пресета не зависит (100 на всех уровнях), иначе Low
	//   при 100 % считался бы «вразнобой». Сравниваем только 11 групп.
	const int32 T = L.ViewDistanceQuality;
	const bool bSame = (T == L.AntiAliasingQuality) && (T == L.ShadowQuality) && (T == L.GlobalIlluminationQuality) && (T == L.ReflectionQuality)
		&& (T == L.PostProcessQuality) && (T == L.TextureQuality) && (T == L.EffectsQuality) && (T == L.FoliageQuality)
		&& (T == L.ShadingQuality) && (T == L.LandscapeQuality);
	return bSame ? T : -1;
}

// Индивидуальные группы — прямые сеттеры UGameUserSettings (0..4). Клампим вход.
static int32 ClampL(int32 L) { return FMath::Clamp(L, 0, 4); }

void UAsdaVideoSettingsSubsystem::SetTextureQuality(int32 L)      { if (UGameUserSettings* S = GetUserSettings()) S->SetTextureQuality(ClampL(L)); }
void UAsdaVideoSettingsSubsystem::SetShadowQuality(int32 L)       { if (UGameUserSettings* S = GetUserSettings()) S->SetShadowQuality(ClampL(L)); }
void UAsdaVideoSettingsSubsystem::SetEffectsQuality(int32 L)      { if (UGameUserSettings* S = GetUserSettings()) S->SetVisualEffectQuality(ClampL(L)); }
void UAsdaVideoSettingsSubsystem::SetPostProcessQuality(int32 L)  { if (UGameUserSettings* S = GetUserSettings()) S->SetPostProcessingQuality(ClampL(L)); }
void UAsdaVideoSettingsSubsystem::SetFoliageQuality(int32 L)      { if (UGameUserSettings* S = GetUserSettings()) S->SetFoliageQuality(ClampL(L)); }
void UAsdaVideoSettingsSubsystem::SetViewDistanceQuality(int32 L) { if (UGameUserSettings* S = GetUserSettings()) S->SetViewDistanceQuality(ClampL(L)); }
void UAsdaVideoSettingsSubsystem::SetAntiAliasingQuality(int32 L) { if (UGameUserSettings* S = GetUserSettings()) S->SetAntiAliasingQuality(ClampL(L)); }
void UAsdaVideoSettingsSubsystem::SetReflectionQuality(int32 L)   { if (UGameUserSettings* S = GetUserSettings()) S->SetReflectionQuality(ClampL(L)); }
void UAsdaVideoSettingsSubsystem::SetGlobalIlluminationQuality(int32 L) { if (UGameUserSettings* S = GetUserSettings()) S->SetGlobalIlluminationQuality(ClampL(L)); }
void UAsdaVideoSettingsSubsystem::SetShadingQuality(int32 L)      { if (UGameUserSettings* S = GetUserSettings()) S->SetShadingQuality(ClampL(L)); }

void UAsdaVideoSettingsSubsystem::SetGroupQuality(int32 TextureL, int32 ShadowL, int32 EffectsL, int32 PostProcessL,
	int32 FoliageL, int32 ViewDistL, int32 AntiAliasL, int32 ReflectionL)
{
	SetTextureQuality(TextureL);       SetShadowQuality(ShadowL);
	SetEffectsQuality(EffectsL);       SetPostProcessQuality(PostProcessL);
	SetFoliageQuality(FoliageL);       SetViewDistanceQuality(ViewDistL);
	SetAntiAliasingQuality(AntiAliasL); SetReflectionQuality(ReflectionL);
	SaveQuality();
}

// ★★★ УРОВЕНЬ СВЕЧЕНИЯ 09.09.2026 — читать ступень из таблицы владельца [AsdaGlow] (Config/DefaultScalability.ini).
//   Числа в C++ НЕ дублируются (port-shared-points): нет строки для уровня → ореол не переопределяем вовсе.
bool UAsdaVideoSettingsSubsystem::ReadGlowStep(int32 Level, float& OutIntensity, float& OutThreshold)
{
	if (!GConfig) { return false; }
	const FString Suffix = FString::Printf(TEXT("@%d"), FMath::Clamp(Level, 0, 4));
	float I = 0.f, T = 0.f;
	if (!GConfig->GetFloat(TEXT("AsdaGlow"), *(FString(TEXT("Intensity")) + Suffix), I, GScalabilityIni)) { return false; }
	if (!GConfig->GetFloat(TEXT("AsdaGlow"), *(FString(TEXT("Threshold")) + Suffix), T, GScalabilityIni)) { return false; }
	OutIntensity = I;
	OutThreshold = T;
	return true;
}

// ★★★ УРОВЕНЬ СВЕЧЕНИЯ 09.09.2026 — сила ореола в КАДРЕ, а не только число ступеней размытия.
//   ПОЧЕМУ ОТДЕЛЬНЫЙ PP-ОБЪЁМ, А НЕ КЛЮЧ И НЕ ЗНАЧЕНИЕ КАРТ-ОБЪЁМА:
//   • движковой CVar «сила ореола» не существует вовсе (r.BloomQuality — это ЧИСЛО гауссиан, фактически «есть/нет»),
//     а пин r.* в [SystemSettings] получил бы приоритет 0x05 и глушил бы пресеты — прямой запрет закона E-0b;
//   • значение карт-объёма PP_<карта> в рантайме менять нельзя: это АССЕТ уровня, правка в игре либо не сохранится,
//     либо испортит уровень (закон no-runtime-widget-fixes — дефект правим в ассете, а не кодом);
//   • у СЦЕНЫ ЛОГИНА PP-объёма нет вообще (прогон 09.09: «волюмов 0»), и никакое «значение волюма» туда не доедет.
//   Служебный unbound-объём поверх всего решает все три: он один и тот же на картах и на логине, не трогает АССЕТЫ,
//   и несёт РОВНО две ручки — интенсивность и порог. Экспозиция, радиус ореола и блики не переопределяются: на логине
//   живая авто-экспозиция (0.03…8.0) остаётся нетронутой, там меняется только вес и отсечка свечения.
//   Приоритет 9000: выше карт-объёмов (у них 0), но ниже объёма ч/б на смерти (10000) — свойства у них разные,
//   пересечения нет, порядок оставлен читаемым.
// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (владелец: «сцена логина в игре не как в редакторе»): на сцене логина пункт свечения «Все» (4)
//   перебивал блум объёма карты своим (сила 4.0, порог 2.5 из [AsdaGlow]), а в редакторе действует блум карты (у Login.umap
//   свой не задан — движковый 0.675 с порогом −1, мягкое свечение всего кадра). Итог в игре: ореол вокруг луча Holy Light
//   и темнее полутона (замер 31.10: сцена ×0.92 без прочих отличий). Здесь на логине объём свечения на пункте 4 силу и порог
//   блума не переопределяет — их задаёт карта, как в редакторе. Пункты 0..3, блики и выборочное свечение — без изменений.
//   Ключ: `asda.Login.GlowFromMap` (1 — блум логина от карты; 0 — блум пункта свечения, как раньше).
static ASDA_DEV_CVAR(int32, CVarAsdaLoginGlowFromMap,
	TEXT("asda.Login.GlowFromMap"), 1,
	TEXT("Блум сцены логина на пункте свечения «Все»: 1 = сила и порог блума от объёма карты, как в редакторе (по умолчанию, 31.10.2026); 0 = из [AsdaGlow] пункта 4, как раньше."),
	ECVF_Default);

void UAsdaVideoSettingsSubsystem::ApplyGlowToWorld(UWorld* World)
{
	if (!World || !World->IsGameWorld()) { return; }
	// ★ 19.09.2026: только мир своего GameInstance (PIE с несколькими клиентами — у каждого свой объём и материал).
	if (World->GetGameInstance() != GetGameInstance()) { return; }
	// ★ 10.10.2026 (fx_alphatest.md): альфа-тест эффектов зависит от пункта свечения и сцены — ДО ранних выходов ниже
	//   (строки [AsdaGlow] к нему отношения не имеют).
	ApplyFxAlphaTest(World);

	float Intensity = 0.f, Threshold = 0.f;
	if (!ReadGlowStep(GlowLevel, Intensity, Threshold))
	{
		UE_LOG(LogAsdaVideo, Warning, TEXT("Уровень свечения %d: в [AsdaGlow] (%s) нет строк Intensity@%d/Threshold@%d — "
			"силу ореола не переопределяю"), GlowLevel, *GScalabilityIni, GlowLevel, GlowLevel);
		return;
	}

	APostProcessVolume* Vol = GlowPPV.Get();
	if (Vol && Vol->GetWorld() != World) { Vol = nullptr; }   // объём остался в прошлом мире — он умер вместе с ним
	if (!Vol)
	{
		// Повторный заход в тот же мир (напр. подписка сработала после ручного SetGlowLevel): имя уже занято,
		//   и SpawnActor вернул бы nullptr. Сначала ищем свой объём, и только потом создаём.
		Vol = FindObject<APostProcessVolume>(World->PersistentLevel, TEXT("AsdaGlowVolume"));
	}
	if (!Vol)
	{
		FActorSpawnParameters SP;
		SP.Name = FName(TEXT("AsdaGlowVolume"));
		SP.ObjectFlags |= RF_Transient;                 // служебный актор — в уровень не сохраняем
		Vol = World->SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass(), FTransform::Identity, SP);
		if (!Vol)
		{
			UE_LOG(LogAsdaVideo, Warning, TEXT("Уровень свечения: не удалось создать PP_Glow в мире %s"), *World->GetName());
			return;
		}
#if WITH_EDITOR
		Vol->SetActorLabel(TEXT("PP_Glow"));   // подпись только в редакторских сборках (SetActorLabel — WITH_EDITOR)
#endif
	}
	// Форму объёма ставим и найденному, и только что созданному: подписка на новый мир может застать
	//   объём, созданный ручным вызовом SetGlowLevel, и он обязан быть безграничным и приоритетным.
	Vol->bUnbound = true;
	Vol->Priority = 9000.f;
	Vol->BlendWeight = 1.f;
	GlowPPV = Vol;
	// Пункт 0 = свечения нет вовсе: интенсивность 0 из таблицы + r.BloomQuality 0 (его ставит SetGlowLevel).
	//   Сам объём при этом НЕ гасим —
	//   иначе на карте снова заработал бы её собственный PP_<карта> с ореолом 4.0, и «выключено» не выключило бы ничего.
	// ★ Фаза 2: пункты 1 «Персонаж» и 2 «Монстр» — штатный блум кадра ВЫКЛЮЧЕН (интенсивность 0), свечение
	//   собирает выборочный пост-материал (ApplyGlowSelect).
	// ★ 19.09.2026 (реверс эталона): и пункт 3 — он тоже выборочный (первая группа + мобы; прочее в группы не входит),
	//   формула эталона одна на 1..3 (разбор — у объявления SetGlowLevel). Кадр целиком — только пункт 4.
	const bool bSelective = (GlowLevel >= 1 && GlowLevel <= 3);
	// ★ 19.09.2026 (хвосты ревизии): «только блики» — пункты 0..3 при включённых бликах. Движок строит блики
	//   лишь внутри блума (`bLensFlareEnabled = bBloomEnabled && …`, а `bBloomEnabled` требует
	//   BloomIntensity > 0 И BloomGaussianIntensity > 0), а их яркость = LensFlareIntensity × BloomIntensity
	//   (PostProcessLensFlares.cpp). Поэтому сила блума = сила пункта 4 (блики те же, что на «Все»), а доля
	//   гауссиан = kFlareOnlyGaussianScale: ореол кадра (TintScale = сила × доля) ниже шага 8-битного кадра —
	//   пункты 0..3 визуально как без блума.
	const bool bFlareOnly = (GlowLevel < 4) && IsLensFlareWanted();
	float BloomI = bSelective ? 0.f : Intensity;
	if (bFlareOnly)
	{
		float RefI = 0.f, RefT = 0.f;
		BloomI = (ReadGlowStep(4, RefI, RefT) && RefI > 0.f) ? RefI : 1.f;
	}
	// ★ 31.10.2026 [ИСПРАВЛЕНИЕ]: сцена логина на пункте «Все» — блум карты, как в редакторе (asda.Login.GlowFromMap).
	const bool bLoginMapBloom = CVarAsdaLoginGlowFromMap.GetValueOnGameThread() != 0 && IsLoginSceneWorld(World)
		&& !bSelective && !bFlareOnly && GlowLevel != 0;
	Vol->Settings.bOverride_BloomIntensity = !bLoginMapBloom;
	Vol->Settings.BloomIntensity = BloomI;
	Vol->Settings.bOverride_BloomGaussianIntensity = bFlareOnly;
	Vol->Settings.BloomGaussianIntensity = bFlareOnly ? kFlareOnlyGaussianScale : 1.f;
	Vol->Settings.bOverride_BloomConvolutionIntensity = bFlareOnly;   // то же для FFT-блума, если карта его выберет
	Vol->Settings.BloomConvolutionIntensity = bFlareOnly ? kFlareOnlyGaussianScale : 1.f;
	Vol->Settings.bOverride_BloomThreshold = !bLoginMapBloom;
	Vol->Settings.BloomThreshold = Threshold;
	UE_CLOG(bLoginMapBloom, LogAsdaVideo, Log, TEXT("Свечение %d: сцена логина — сила и порог блума от объёма карты, как в редакторе"), GlowLevel);
	ApplyLensFlareToVolume(Vol);
	ApplyGlowSelect(World, Vol);
	Vol->bEnabled = true;
	// ★ 19.09.2026: в лог — ДЕЙСТВУЮЩАЯ сила блума объёма (на 1..3 она 0, строка таблицы им не применяется).
	UE_LOG(LogAsdaVideo, Log, TEXT("Уровень свечения %d → PP_Glow(%s): сила блума=%.3f порог=%.3f%s%s (приоритет %.0f, unbound=%d)"),
		GlowLevel, *World->GetName(), BloomI, Threshold,
		bSelective ? (IsLoginSceneWorld(World)
			? (GlowLevel == 2 ? TEXT(" — сцена логина: на «Монстр» свечения нет") : TEXT(" — сцена логина: формула эталона на весь кадр"))
			: (GlowLevel == 1 ? TEXT(" — выборочно: первая группа (игроки и привязанное к ним)")
				: (GlowLevel == 2 ? TEXT(" — выборочно: только мобы") : TEXT(" — выборочно: первая группа + мобы")))) : TEXT(""),
		bFlareOnly ? *FString::Printf(TEXT(" + блики: блум-носитель сила %.2f, доля гауссиан %g"), BloomI, kFlareOnlyGaussianScale) : TEXT(""),
		Vol->Priority, Vol->bUnbound ? 1 : 0);
}

// ═══ ★★★ ВЫБОРОЧНОЕ СВЕЧЕНИЕ (пункты 1 «Персонаж», 2 «Монстр», 3 «Персонаж+монстр») ════════════════════════
//   Эталон рисует выбранную группу ЦЕЛИКОМ в отдельный буфер свечения (белый фон, альфа 0), размывает его двумя
//   проходами и накладывает ТОЛЬКО на пиксели группы: out = min(1, 2·G·min(1, 2·70/255·B2))
//   (`sceneRenderShadowLightPass@0xF5FCC0` → `effectSetupRenderStateAndDraw@0xF5EFF0` → `effectQuadRender@0xF5F440`;
//   разбор с адресами — у объявления SetGlowLevel и в шапке Tools/asda_build_glow_select_mat.py). Порога нет.
//   ★ 19.09.2026: прежний вариант (ореол с пикселей ярче порога 2.5, перед блумом) эталону не отвечал — у мобов
//   ни одного пикселя ярче порога, пункт 2 не светился. Теперь `M_AsdaGlowSelect` — эталонная формула после
//   тонмаппера, числа — [AsdaGlow] UnitOffset/UnitBlurFactor/UnitComposeFactor и BlurBuf* (числа .data эталона).
//   ★★ 19.09.2026 (К1, ревизия): маска ОБРАТНАЯ. Состав первой группы эталона — девять списков кадра, и все они —
//   игрок и привязанное к нему (разбор с адресами списков — у объявления SetGlowLevel); застройки карты (маска 256),
//   рельефа (8), NPC (32), лута (16), воды и неба там нет. Поэтому трафарет ставится ТОЛЬКО акторам групп
//   (GlowGroupOf: первая группа — kGlowCharStencil, мобы — kGlowMobStencil), всё непомеченное в группу не входит,
//   и новый вид объектов карты сам собой не «протечёт». Пометки ставятся только тем пунктам, которым нужны:
//   лишний меш в проходе CustomDepth — лишняя цена.
//   Пометки ведёт покадровый проход после тика акторов (OnWorldPostActorTick) — до отрисовки того же кадра:
//   новый игрок/моб от сервера (подписка на спавн мира), новая экипировка/волосы/езда/флаг клана (новые
//   компоненты) и пометка, снятая чужим кодом (подсветка цели), восстанавливаются без таймера.
//   ★★ 19.09.2026 (С2): на сцене логина (второй путь кадра эталона `CRenderer_RenderFrame@0x975EF0`) пункты 1/3
//   сводят весь кадр без маски (режим материала 4), пункт 2 — без свечения (эталон: `CRenderer_BeginScenePass`
//   берёт в буфер кадр только при пунктах 1/3/4).
//   `r.CustomDepth` = 3 (глубина + трафарет) держит DefaultEngine.ini постоянно: смена ключа перерегистрирует
//   весь мир (SceneUtils.cpp:24-30) — рывок; здесь ключ только страхуется, если его кто-то опустил.
//   ★★★ 19.09.2026 (glow_fix6, К1 — доказано статикой и дампом, разбор у объявления GlowStencilGroup в .h):
//   ПОЛУПРОЗРАЧНОЕ (α < 1) ТЕЛО В ГРУППУ НЕ ВХОДИТ. При α < 1 эталон ставит всем подмешам модели бит 2 «в очередь
//   прозрачных» (`meshPartsSetRenderPassFlags@0x11C2720`), и проход буфера свечения их пропускает
//   (`meshNodeRenderDispatch@0x111E110`, @0x111E145–0x111E15F): «душа», появление и невидимость рисуются очередью
//   0x3537FA8 на шаге «5.1.5» — ПОСЛЕ сведения, без свечения. Поэтому меш с α < 1 не помечается вовсе; прежние
//   полосы трафарета по α (glow_fix4/5: GlowStencilFor, kGlow*AlphaBase), флаг «Allow Custom Depth Writes» и порог
//   0.01 у мастеров «души»/появления и эффектов (Tools/asda_add_glow_custom_depth.py) сняты, ассеты возвращены к
//   состоянию до волн (_backup/2026-09-19_glow_fix3). Вес непрозрачного тела — fog (альфа PShade tSkin/tRigid при
//   α = 1, EmptyProject.fx:828), смешения в проходе G нет (С1 ниже).
//   ★ [ПОРТ] части с ФАЙЛОВЫМ битом 1 того же поля (`подмеш+4` из .jbm: у персонажа/экипировки 237 мешей
//   tSkin/tRigid, у мобов 141, у питомцев 7 — `Reports/jbm_blend_flag_study.md` §3.2) эталон тоже в буфер свечения
//   не рисует, а порт метит компонент целиком (трафарет — на примитив, не на секцию); tNoSkin-части без бита дают в
//   эталоне альфу G = Tex.w·fog·α (EmptyProject.fx:694), в порту вес = fog. Отложено — реестр PORT_PENDING_ON_SYSTEMS.md,
//   пункт glow_fix6 (нужен признак бита по секциям при импорте).
//   ★ [ПОРТ] (С2, поведение НЕ меняется): эталон на пунктах 1/3 группу обычным проходом не рисует
//   (`sceneRenderShadowFogPasses@0xF5FA80` — только при 0/2/4) и сводит lerp(ФОН за группой, o, fog); порт
//   рисует тело в кадре и сводит lerp(КАДР С ТЕЛОМ, o, fog) — у пикселя с fog < 1 вместо фона просвечивает само
//   тело (в тумане UE). Убрать тело из кадра только на пунктах 1/3 штатно нельзя: пост-материал видит один кадр.
//   ★★ 19.09.2026 (glow_fix6, С1 — доказано СТАТИКОЙ; дампы live.dmp/asda.dmp сняты ВНЕ кадра и подтверждают лишь состояние на входе в кадр — ревизия rev_glow6): на рисовании тела в G ALPHABLENDENABLE = 0.
//   Статика (граф вызовов CRenderer_RenderWorldFrame@0x9773B0 с учётом кэша рендерера +0x1B8 бит 2 — вкл
//   `CRenderer_ApplyBlendRenderState@0x9764F0`/`render_EnableAlphaBlend@0x113BF20` ставят и бит, выкл
//   `rendDisableAlphaTestState@0x976600` снимает только при бите): кадр с интерфейсом кончается выкл. @0x978A6F (после
//   Present) вслед за вкл. @0x97820E ⇒ (0, 0); PetWindow/тени/отражение воды пишут ALPHABLENDENABLE только парами Get→Set→Set(сохранённое); шаг «2»
//   неба кончается выкл. @0x977811 (g_dwRenderFeatureFlags 0x2BCD024 = 1 в дампе); шаг «3» рельефа: пункт 0x40 —
//   `CTerrain_SetupRenderState(1)` включает с битом, `mapObjectsRenderVisiblePass` @0x1135265 выключает; без 0x40 —
//   рельеф смешение не трогает (`terrainApplyTextureStageState` не выключает, но и не включает);
//   `mapObjectsProcessVisible` (без выключения) — только при бите 2 в CTerrain+0x2DB8, а его единственная запись
//   @0x1134C9E ставит 1; `mapMesh_RenderMultipassEnd` — только без пиксельного шейдера (caps 0x352912C бит 2 = PS ≥
//   1.1, в дампе 0x1B); шаг «4» и путь тела в G смешение не трогают; прочие объекты G — пары сохранение/возврат.
//   Дамп live.dmp и asda.dmp (между кадрами): массив состояний устройства d3d9 ([устройство+0x331C], как читает
//   его GetRenderState) ALPHABLENDENABLE = 0, кэш +0x1B8 = 0x21 (бит 2 снят). Живым кадром в момент G не проверено.
//   ★★ 19.09.2026 (glow_fix5, С3 — реверс АЛЬФЫ G; прежний разбор «α = tex.a·α, смешение SRCALPHA/INVSRCALPHA ⇒ α²»
//   был неверен: `CRenderer_ApplyBlendRenderState@0x9764F0` ставит ТОЛЬКО смешение, и в проходе G его никто не зовёт):
//   • Путь рисования игрока — .fx: `FxConfig.ini` эталонного клиента `FXShader=TRUE` (client/system; чтение —
//     `CFxConfig_LoadSettings@0xECE610`, strlwr+"true" → байт +104) ⇒ `gfxApplyEffectSettings` грузит
//     system/EmptyProject.fx и ставит g_bUseEffectPath = 1 (render_core.c:5737-5757; 0 — только без vs/ps 3.0).
//     Тело — `CMeshNode_DrawSkinned@0x1119540` (model_anim.c:24829, ветка g_bUseEffectPath == 1) → техника tSkin →
//     PShade: альфа = fog·NoSkinTexScroll.w (EmptyProject.fx:828; NoSkinTexScroll.w = *pTexScroll — прозрачность
//     записи модели: появление/«душа»), tex.a в альфу НЕ входит (Tex.w — только блик и кубмап).
//   • fog — ВЕРШИННЫЙ туман-альфа карты, у НЕПРОЗРАЧНЫХ тоже: fog = saturate(Near + (Far − Near)·max(0, d² − S²)/
//     (E² − S²)) (EmptyProject.fx:634-645, c90 = FogInfo от `CRenderer_SetFogAndLightConstants@0x1124D30`), числа
//     S/E/Near/Far — поля +0x0C…+0x18 файла map\<карта>\<карта>.SCE (`CMap_LoadExTileInfo@0x11D3370`), по картам
//     разные (alpen 5000/11000/1/0.3; Mountain Flamion 700/9000/0.8/0.2 — «у камеры» уже 0.8). Порт: таблица
//     AsdaMapRegistry::kSceFogs (реестр карт, поле sce_fog), параметры FogStart/FogEnd/FogNear/FogFar материала.
//   • Смешение тела в проходе G проход НЕ задаёт: `renderList_traverseAndDispatch@0x111B730` (список 0x29080FC
//     через `sub_976A60`) зовёт только `meshNodeApplyRenderState` (отсечение/стадии) и `renderApplyMeshState`
//     (шейдеры); `d3dApplyMaterialRenderState@0x111DA90` (единственное место материального смешения тела) — лишь в
//     `CMeshNode_DrawDispatch` очереди основного кадра. Состояние наследуется от шагов «2»–«4» кадра: статический
//     обход графа вызовов (311 функций от шагов «3»/«4» и тела G) достаёт до включения смешения только в рельефе,
//     и обе такие ветки кончаются выключением (`mapMesh_RenderMultipassEnd` / `mapObjectsRenderVisiblePass` →
//     `rendDisableAlphaTestState@0x976600`, ALPHABLENDENABLE = 0; шаг «2» неба — тоже 0, render_core.c:1332).
//     Единственная ветка со смешением: `CTerrain_SetupRenderState(1)` (флаг 0x40 g_dwSystemOptionFlags) +
//     `mapObjectsProcessVisible` (бит 2 CMap+0x2DB8). Из прочих восьми объектов группы четыре на время рисования
//     явно ВЫКЛЮЧАЮТ смешение (0x3518758 `renderShadowPassRenderState0`, 0x3517B58 `fxRenderWithFogAlphaPass`,
//     0x2C2FCC0 `uiCharRenderWithFogState`, 0x3524968 `renderChildrenWithAlphaBlend`), автоматы гачи 0x2BDD530
//     (`rendererSetTexTransformAndRenderShadow`) — включают (у них альфа G = fog², порт даёт им тот же линейный вес
//     fog — расхождение только в тумане, прозрачности у автоматов нет).
//     ⇒ Смешения НЕТ (★ glow_fix6: доказано статическим учётом кэша; дамп — только вход в кадр, см. С1 выше) — G пикселя тела = цвет тела
//     (белая очистка не подмешивается), альфа G = fog, вес сведения ЛИНЕЙНЫЙ: fog (α < 1 в группу не входит — К1).
//     Живым кадром эталона в момент G не проверено.
//   Нет материала или чисел таблицы — предупреждение и НИКАКОГО свечения (честно, а не «похоже»).
static const TCHAR* kGlowSelectMatPath = TEXT("/Game/Materials/M_AsdaGlowSelect.M_AsdaGlowSelect");

// ═══ ★★ 10.10.2026: АЛЬФА-ТЕСТ ПРОХОДА ЭФФЕКТОВ (разбор с адресами — у объявления IsFxAlphaTestOn) ═══════════
//   Ключ зарегистрирован один раз (grep "asda.Fx.AlphaTest" по Source/ до добавления = 0; закон ue-console-duplicate-registration).
static int32 GAsdaFxAlphaTest = -1;
// Путь коллекции параметров эффектов (её скаляр `AlphaTest` читают все мастера M_AsdaEffect*).
static const TCHAR* const kAsdaFxMpcPath = TEXT("/Game/Effects/MPC_AsdaFx.MPC_AsdaFx");
// Имя скаляра в коллекции (0 — тест выключен, 1 — отсечка srcA < 8/255).
static const FName kAsdaFxAlphaTestParam(TEXT("AlphaTest"));

// Переприменить значение в мирах игры после смены ключа (у каждого GameInstance — своя подсистема).
static void AsdaFxAlphaTestReapply(IConsoleVariable*)
{
	if (!GEngine) { return; }
	for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
	{
		UWorld* W = Ctx.World();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (const UAsdaVideoSettingsSubsystem* V = GI ? GI->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr)
		{
			V->ApplyFxAlphaTest(W);
		}
	}
}

// Ключ проверки: -1 — как эталон (логин/выбор — всегда, мир — только пункт свечения 4), 0 — выкл, 1 — вкл.
static FAutoConsoleVariableRef CVarAsdaFxAlphaTest(
	TEXT("asda.Fx.AlphaTest"),
	GAsdaFxAlphaTest,
	TEXT("Альфа-тест прохода эффектов (эталон: ALPHAREF 8, GREATEREQUAL; srcA = Tex.a×VC.a < 8/255 отбрасывается целиком). ")
	TEXT("-1 = как эталон: сцены логина/выбора/создания — всегда, мир — только при пункте свечения 4 «Все»; 0 = выкл; 1 = вкл. ")
	TEXT("Не сохраняется."),
	FConsoleVariableDelegate::CreateStatic(&AsdaFxAlphaTestReapply),
	ECVF_Default);

bool UAsdaVideoSettingsSubsystem::IsFxAlphaTestOn(const UWorld* World) const
{
	if (GAsdaFxAlphaTest >= 0) { return GAsdaFxAlphaTest != 0; }   // ручной режим проверки
	// Эталон: сцены логина идут через `CRenderer_RenderFrame` (тест безусловно), мир — через
	//   `CRenderer_RenderWorldFrame` (тест только при `sub_928F5F() == 4`).
	return IsLoginSceneWorld(World) || GlowLevel == 4;
}

void UAsdaVideoSettingsSubsystem::ApplyFxAlphaTest(UWorld* World) const
{
	if (!World || !World->IsGameWorld()) { return; }
	UMaterialParameterCollection* Mpc = LoadObject<UMaterialParameterCollection>(nullptr, kAsdaFxMpcPath);
	if (!Mpc)
	{
		UE_LOG(LogAsdaVideo, Warning, TEXT("Альфа-тест эффектов: нет коллекции %s (Tools/asda_fx_alphatest.py)"), kAsdaFxMpcPath);
		return;
	}
	UMaterialParameterCollectionInstance* Inst = World->GetParameterCollectionInstance(Mpc);
	const bool bOn = IsFxAlphaTestOn(World);
	const bool bSet = Inst && Inst->SetScalarParameterValue(kAsdaFxAlphaTestParam, bOn ? 1.f : 0.f);
	UE_LOG(LogAsdaVideo, Log, TEXT("Альфа-тест эффектов: %s (мир %s, %s, пункт свечения %d, ключ %d)%s"),
		bOn ? TEXT("ВКЛ") : TEXT("выкл"), *World->GetName(),
		IsLoginSceneWorld(World) ? TEXT("сцена логина") : TEXT("игровой мир"), GlowLevel, GAsdaFxAlphaTest,
		bSet ? TEXT("") : TEXT(" — НЕ ЗАПИСАН: в коллекции нет скаляра AlphaTest"));
}

bool UAsdaVideoSettingsSubsystem::IsLoginSceneWorld(const UWorld* World)
{
	// Карта логина/выбора/создания персонажа порта — это мир с режимом AAsdaLoginGameMode (эталон: состояния
	//   2/3/5/6 главного цикла → `Game_UpdateAndRenderFrame@0xF20B80` → `CRenderer_RenderFrame`).
	// ★ F7 (дедуп №2, В3, 12.10.2026): общий признак `AsdaWorldKind::IsLoginScene` — в игре это тот же «режим игры»
	//   (подсистема живёт только в GameInstance, мира без режима здесь не бывает).
	return AsdaWorldKind::IsLoginScene(World);
}

int32 UAsdaVideoSettingsSubsystem::GlowGroupOf(const AActor* Actor)
{
	if (!Actor) { return 0; }
	// ★ 19.09.2026: эффект с признаком `AAsdaEffectActor::bGlowWithCarrier` — в группе своего носителя (носитель =
	//   владелец эффекта, SpawnByJebName ставит Owner). ★★ glow_fix5 (С2, реверс): свечение заточки оружия/щита,
	//   крылья и кольцо соулмейта в эталоне рисуются очередью 0x3537FA8 на шаге «5.1.5» — ПОСЛЕ сведения свечения,
	//   в группу не входят, признак им не ставится (разбор с адресами — у объявления признака). Прежняя ссылка на
	//   `itemSetupSowelGlow` (полосы «совела», в порту их нет) и «загрузчик .jeb @0x9EDF51» (это загрузчик модели
	//   груди .jbm, функция 0x9EDE30) была ошибкой. Прочие эффекты (навыки, карта, прицел, появление) — тоже вне групп.
	//   ★ 19.09.2026 (glow_fix4, МЕЛК ревизии): прежний запасной путь «родитель прикрепления» снят — пометка его
	//   никогда не видела (она перебирает `Children` носителя = акторы с Owner носителя), и группа с пометкой
	//   расходились. Теперь одно правило на обе стороны — IsGlowCarriedFx.
	//   ★ 19.09.2026 (glow_fix4, реверс): шлейф езды (`Item_UpdateSpecialEffectAndSound@0xF272E0`) и вспышка
	//   посадки (`itm1_n_00_vehicle_0000_e.jeb`, entity_char.c:19394/19739, network.c:37011) рождаются
	//   `Effect_SpawnByName` менеджера эффектов 0x349E860 (список +0xB4C), а он в проходе буфера свечения не
	//   рисуется (девять объектов `sceneRenderShadowLightPass@0xF5FD79…0xF5FDCE` — 0x29079E0, 0x351BF00, 0x3518758,
	//   0x3517B58, 0x2BDD530, 0x2C2FCC0, 0x3519358, 0x351FA70, 0x3524968; 0x349E860 среди них нет): его узлы
	//   `effectMgr_BuildDepthSortQueue@0xF55DA0` кладёт в очередь 0x3537FA8, которую `renderShadowMeshQueue@0x97E870`
	//   (шаг «5.1.5» кадра) рисует ПОСЛЕ сведения свечения (`effectQuadRender_FC00`, шаг «5»). Шлейф стрелы
	//   (`eff_0_char_03_flyarr01-a_00.jeb`) — эффект внутри слота снаряда (`CEffect_LoadJEB_Wrapper(slot+411)`),
	//   цепочка его загрузки ни одного из списков группы не касается (в список 0x29080FC пишет только модель
	//   стрелы, `Projectile_LoadArrowModel@0xA58910`). Поэтому все три признака не несут — вне групп.
	if (const AAsdaEffectActor* Fx = Cast<AAsdaEffectActor>(Actor))
	{
		const AActor* Carrier = Fx->GetOwner();
		return (Carrier && IsGlowCarriedFx(Actor, Carrier) && GlowGroupOf(Carrier) == 1) ? 1 : 0;
	}
	// Мобы — список 0x34A0364 менеджера g_pMobMgr (`mobEffect_LoadArchiveStep` кладёт туда модели мобов).
	if (Actor->IsA<AAsdaMobActor>()) { return 2; }
	// Первая группа — игрок и привязанное к нему (адреса списков — у объявления SetGlowLevel):
	//   игрок со всеми навесными частями (компоненты актора: экипировка, волосы, оружие, езда-маунт, флаг клана);
	//   питомец; хранитель (`CCharActor`); снаряд дальнобойного оружия; автоматы гачи. [ПОРТ] Машина пасхалки
	//   «Транспорт» в оригинале отсутствует — отнесена к группе как езда игрока (маунт эталона — список 0x29080FC).
	if (Actor->IsA<AAsdaPlayerCharacter>() || Actor->IsA<AAsdaPetActor>() || Actor->IsA<AAsdaSoulGuardianActor>()
		|| Actor->IsA<AAsdaProjectileActor>() || Actor->IsA<AAsdaRideVehicle>() || Actor->IsA<AAsdaGachaSceneActor>())
	{
		return 1;
	}
	return 0;   // NPC, лут, застройка, рельеф, вода, небо, эффекты карты — ни в одной группе эталона
}

int32 UAsdaVideoSettingsSubsystem::WantedGlowStencil(const AActor* Actor) const
{
	const int32 Group = GlowGroupOf(Actor);
	if (Group == 1 && (GlowLevel == 1 || GlowLevel == 3)) { return kGlowCharStencil; }
	if (Group == 2 && (GlowLevel == 2 || GlowLevel == 3)) { return kGlowMobStencil; }
	return 0;
}

int32 UAsdaVideoSettingsSubsystem::ApplyGlowStencilToActor(AActor* Actor, int32 Stencil)
{
	if (!Actor) { return 0; }
	int32 N = ApplyGlowStencilToComponents(Actor, Stencil);   // число изменённых компонентов
	// ★ 19.09.2026 (ревизия С1): эффекты предметов носителя — отдельные акторы, владелец которых — носитель
	//   (SpawnCharFx → SpawnByJebName, Owner). Их группа = группа носителя, значение трафарета то же.
	//   Перебор `Children` каждый проход: новый эффект (смена оружия, крылья из пакета) помечается в том же
	//   кадре без описи и без подписок; снятый/умерший из `Children` уходит сам.
	//   ★ 19.09.2026 (glow_fix4): отбор — то же правило, что у GlowGroupOf (IsGlowCarriedFx).
	//   ★ 19.09.2026 (glow_fix6): признак `bGlowWithCarrier` сейчас не ставит никто — перебор не нужен и не
	//   выполняется (if constexpr по kGlowCarriedFxLive; механизм носителя оставлен для полос «совела» — реестр отложенного).
	if constexpr (kGlowCarriedFxLive)
	{
		for (AActor* Child : Actor->Children)
		{
			if (IsGlowCarriedFx(Child, Actor)) { N += ApplyGlowStencilToComponents(Child, Stencil); }
		}
	}
	return N;
}

bool UAsdaVideoSettingsSubsystem::IsGlowCarriedFx(const AActor* Child, const AActor* Carrier)
{
	const AAsdaEffectActor* Fx = Cast<AAsdaEffectActor>(Child);
	return Fx && Carrier && Fx != Carrier && Fx->bGlowWithCarrier && Fx->GetOwner() == Carrier
		&& !Carrier->IsA<AAsdaEffectActor>();
}

int32 UAsdaVideoSettingsSubsystem::GlowStencilGroup(int32 Value)
{
	// ★ 19.09.2026 (glow_fix6): полос α больше нет — свои значения только 10 и 11.
	if (Value == kGlowCharStencil) { return 1; }
	if (Value == kGlowMobStencil) { return 2; }
	return 0;
}

float UAsdaVideoSettingsSubsystem::GlowAlphaOfMesh(UMeshComponent* Mesh)
{
	if (!Mesh) { return 1.f; }
	// Имена мастеров прозрачности, через которые порт рисует полупрозрачное тело: «душа»/GM-hide
	//   (AAsdaPlayerCharacter::ApplySoulTranslucency) и появление/гашение (UAsdaFadeComponent — игроки, мобы,
	//   хранитель). Сравнение по имени базового материала — без загрузки ассетов на покадровом проходе.
	//   ★ Дедуп №2 В1-C (F10): имена — общие константы `AsdaMat` (переименование мастера в одном месте ломало пометку молча).
	static const FName kSoulBase(AsdaMat::kCharLitSoulName);    // мастер «души»/GM-hide
	static const FName kFadeBase(AsdaMat::kPreviewFadeName);    // мастер появления/гашения
	static const FName kSoulParam(AsdaMat::kParamSoulOpacity);  // α «души»/GM-hide
	static const FName kFadeParam(AsdaMat::kParamFadeIn);       // α появления/гашения
	const int32 NumSlots = Mesh->GetNumMaterials();   // слотов материала у меша
	if (NumSlots <= 0) { return 1.f; }
	float A = 0.f;   // наибольшая α слотов
	for (int32 S = 0; S < NumSlots; ++S)
	{
		float SlotA = 1.f;   // α слота (непрозрачный — 1)
		if (UMaterialInstanceDynamic* D = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(S)))
		{
			const UMaterial* B = D->GetMaterial();   // базовый мастер экземпляра
			const FName BN = B ? B->GetFName() : NAME_None;
			if (BN == kSoulBase) { D->GetScalarParameterValue(FHashedMaterialParameterInfo(kSoulParam), SlotA); }
			else if (BN == kFadeBase) { D->GetScalarParameterValue(FHashedMaterialParameterInfo(kFadeParam), SlotA); }
		}
		A = FMath::Max(A, FMath::Clamp(SlotA, 0.f, 1.f));
		if (A >= 1.f) { break; }
	}
	return A;
}

int32 UAsdaVideoSettingsSubsystem::ApplyGlowStencilToComponents(AActor* Actor, int32 Stencil)
{
	if (!Actor) { return 0; }
	int32 N = 0;   // число изменённых компонентов
	// ★ 19.09.2026 (МЕЛК «подсветка ↔ пометки»): CustomDepth, который держит включённая подсветка цели, не
	//   трогаем — у одного меша одно значение трафарета, и пока цель выбрана, приоритет у её подсветки. После
	//   снятия подсветка возвращает прежнее и зовёт RefreshGlowMarksFor — пометка восстанавливается сразу.
	const UAsdaTargetHighlightComponent* HL = Actor->FindComponentByClass<UAsdaTargetHighlightComponent>();
	if (HL && !HL->IsHighlighted()) { HL = nullptr; }
	// ★ 19.09.2026 (glow_fix4): эффект предмета носителя — его прозрачность = множитель GM-hide `FxAlphaScale`
	//   (у эффектов нет MID — множитель живёт в акторе, AsdaEffectActor.h). ★ glow_fix6: правило то же, что у тела —
	//   α < 1 не помечается (К1).
	const AAsdaEffectActor* Fx = Cast<AAsdaEffectActor>(Actor);
	Actor->ForEachComponent<UMeshComponent>(/*bIncludeFromChildActors*/ false, [&N, Stencil, HL, Fx](UMeshComponent* M)
	{
		// Плашка имени (виджет в мире) — не тело: в буфер свечения эталона плашки не рисуются.
		// ★ 19.09.2026 (glow_fix5, МЕЛК а): компоненты «только для редактора» — тоже не тело. Прогон D1: при фейде
		//   `CameraProxyMeshComponent_0` (иконка-меш камеры UCameraComponent, MatineeCam_mat, `SetIsVisualizationComponent`
		//   → bIsEditorOnly, bHiddenInGame; CameraComponent.cpp:166-177) оставался на 10: слота Tex у него нет, фейд/«душа»
		//   его не подменяют. В кадре он не рисуется, в сборке игры его нет — пометка ему не нужна вовсе.
		if (!M || M->IsA<UWidgetComponent>() || M->IsEditorOnly()) { return; }
		if (HL && HL->OwnsCustomDepth(M)) { return; }
		// ★ 19.09.2026 (glow_fix6, К1): полупрозрачный (α < 1) меш в группу не входит — эталон такие подмеши в буфер
		//   свечения не рисует (бит 2 `подмеш+4`, `meshNodeRenderDispatch@0x111E110`); непрозрачный — 10/11.
		const float Alpha = Fx ? Fx->FxAlphaScale : ((Stencil > 0) ? GlowAlphaOfMesh(M) : 1.f);   // α меша
		const int32 Want = (Stencil > 0 && Alpha >= 1.f) ? Stencil : 0;   // нужное значение трафарета
		if (Want > 0)
		{
			// Уже помечен тем же значением — не дёргаем рендер-состояние.
			if (M->bRenderCustomDepth && M->CustomDepthStencilValue == Want) { return; }
			M->SetCustomDepthStencilValue(Want);
			M->SetRenderCustomDepth(true);
			++N;
		}
		else if (GlowStencilGroup(M->CustomDepthStencilValue) != 0)
		{
			// Снимаем ТОЛЬКО свои пометки (10/11): чужое значение трафарета (обводка цели и т.п.) не трогаем.
			// ★ 01.11.2026 (mob_fx_audit) [ИСПРАВЛЕНИЕ] Custom Depth гасим, только если он не нужен «записи глубины» прозрачных
			//   подсеток (M_AsdaMobFxBlend / M_AsdaCharLitBlend): иначе после выключения свечения призрак и прозрачная деталь
			//   экипировки снова показывали внутренние слои до респавна/переодевания.
			M->SetCustomDepthStencilValue(0);
			M->SetRenderCustomDepth(AsdaMobFx::WantsCustomDepth(M));
			++N;
		}
	});
	return N;
}

int32 UAsdaVideoSettingsSubsystem::BindGlowMarks(UWorld* World)
{
	if (!World || !World->IsGameWorld()) { return 0; }
	if (GlowMarkWorld.Get() != World)
	{
		UnbindGlowMarks();
		GlowMarkWorld = World;
		// Опись акторов групп мира: запечённые в уровнях (мобы-шаблоны, расставленные вручную) и уже живые.
		// ★ 19.09.2026: эффекты предметов в опись не идут — их помечает проход носителя (перебор его `Children`,
		//   ApplyGlowStencilToActor); в момент спавна признак `bGlowWithCarrier` ещё не взведён.
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (GlowGroupOf(*It) != 0 && !It->IsA<AAsdaEffectActor>()) { GlowTracked.Add(*It); }
		}
		TWeakObjectPtr<UAsdaVideoSettingsSubsystem> Weak(this);
		// Новые акторы (живые игроки/мобы от сервера, питомцы, снаряды) — в момент спавна, до первого кадра.
		//   ★ 19.09.2026 (ревизия, МЕЛК): AddUnique — актор мог уже попасть в опись с подгружаемым уровнем.
		GlowSpawnHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([Weak](AActor* A)
		{
			UAsdaVideoSettingsSubsystem* Self = Weak.Get();
			if (!Self || GlowGroupOf(A) == 0 || A->IsA<AAsdaEffectActor>()) { return; }
			Self->GlowTracked.AddUnique(A);
			if (Self->GlowMarkedLevel >= 1 && Self->GlowMarkedLevel <= 3) { ApplyGlowStencilToActor(A, Self->WantedGlowStencil(A)); }
		}));
		// Подгружаемые уровни — их акторы в опись (делегат общий для процесса: берём только свой мир).
		//   ★ 19.09.2026 (ревизия, МЕЛК): без повторов — уровень может прийти повторно (выгрузка/загрузка), а его
		//   акторы уже учтены описью мира или спавном; дубль в описи = двойной проход по актору каждый кадр.
		GlowLevelAddedHandle = FWorldDelegates::LevelAddedToWorld.AddLambda([Weak](ULevel* Level, UWorld* W)
		{
			UAsdaVideoSettingsSubsystem* Self = Weak.Get();
			if (!Self || !Level || W != Self->GlowMarkWorld.Get()) { return; }
			for (AActor* A : Level->Actors)
			{
				if (GlowGroupOf(A) != 0 && !A->IsA<AAsdaEffectActor>()) { Self->GlowTracked.AddUnique(A); }
			}
		});
		// Покадровый проход после тика акторов (тот же кадр ещё не отрисован).
		GlowPostTickHandle = FWorldDelegates::OnWorldPostActorTick.AddLambda([Weak](UWorld* W, ELevelTick, float)
		{
			UAsdaVideoSettingsSubsystem* Self = Weak.Get();
			if (!Self || W != Self->GlowMarkWorld.Get()) { return; }
			if (Self->GlowMarkedLevel < 1 || Self->GlowMarkedLevel > 3) { return; }
			Self->SyncGlowMarks();
		});
		GlowMarkedLevel = -1;   // новый мир — пометки ещё не выставлены
	}
	// Пункт сменился (или мир новый): полный пересчёт по ВСЕМ учтённым, включая скрытых (снять старые пометки).
	int32 N = 0;
	const bool bMarking = (GlowLevel >= 1 && GlowLevel <= 3) && !IsLoginSceneWorld(World);
	GlowMarkedLevel = bMarking ? GlowLevel : 0;
	GlowTracked.RemoveAllSwap([](const TWeakObjectPtr<AActor>& W) { return !W.IsValid(); });
	for (const TWeakObjectPtr<AActor>& W : GlowTracked)
	{
		N += ApplyGlowStencilToActor(W.Get(), bMarking ? WantedGlowStencil(W.Get()) : 0);
	}
	return N;
}

void UAsdaVideoSettingsSubsystem::UnbindGlowMarks()
{
	UWorld* W = GlowMarkWorld.Get();
	for (const TWeakObjectPtr<AActor>& A : GlowTracked)
	{
		ApplyGlowStencilToActor(A.Get(), 0);   // свои пометки 10/11 — снять (мир может жить дальше)
	}
	GlowTracked.Reset();
	if (W && GlowSpawnHandle.IsValid()) { W->RemoveOnActorSpawnedHandler(GlowSpawnHandle); }
	GlowSpawnHandle.Reset();
	if (GlowLevelAddedHandle.IsValid()) { FWorldDelegates::LevelAddedToWorld.Remove(GlowLevelAddedHandle); GlowLevelAddedHandle.Reset(); }
	if (GlowPostTickHandle.IsValid()) { FWorldDelegates::OnWorldPostActorTick.Remove(GlowPostTickHandle); GlowPostTickHandle.Reset(); }
	GlowMarkWorld.Reset();
	GlowMarkedLevel = -1;
}

int32 UAsdaVideoSettingsSubsystem::SyncGlowMarks()
{
	int32 N = 0;        // число изменённых компонентов за проход
	bool bDead = false; // в описи есть мёртвые слоты
	for (const TWeakObjectPtr<AActor>& W : GlowTracked)
	{
		AActor* A = W.Get();
		if (!A) { bDead = true; continue; }
		// Скрытый актор не рисуется (запечённые мобы-шаблоны): его пометит проход того кадра, где он покажется.
		if (A->IsHidden()) { continue; }
		N += ApplyGlowStencilToActor(A, WantedGlowStencil(A));
	}
	if (bDead) { GlowTracked.RemoveAllSwap([](const TWeakObjectPtr<AActor>& W) { return !W.IsValid(); }); }
	return N;
}

void UAsdaVideoSettingsSubsystem::RefreshGlowMarksFor(AActor* Actor)
{
	if (!IsValid(Actor)) { return; }
	UWorld* W = Actor->GetWorld();
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaVideoSettingsSubsystem* Self = GI ? GI->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr;
	// Пометки ведутся только в своём мире и только на пунктах 1..3 вне логина (GlowMarkedLevel ставит BindGlowMarks);
	//   иначе — снять свои 10/11, если снимок подсветки их вернул.
	const bool bMarking = Self && W == Self->GlowMarkWorld.Get() && Self->GlowMarkedLevel >= 1 && Self->GlowMarkedLevel <= 3;
	ApplyGlowStencilToActor(Actor, bMarking ? Self->WantedGlowStencil(Actor) : 0);
}

FString UAsdaVideoSettingsSubsystem::DescribeEffectiveGlow() const
{
	const APostProcessVolume* V = GlowPPV.Get();
	FString S = V
		? FString::Printf(TEXT("действует: сила блума=%.3f%s порог=%.3f"), V->Settings.BloomIntensity,
			V->Settings.bOverride_BloomGaussianIntensity
				? *FString::Printf(TEXT(" (доля гауссиан %g — только блики, ореола нет)"), V->Settings.BloomGaussianIntensity) : TEXT(""),
			V->Settings.BloomThreshold)
		: FString(TEXT("служебного объёма PP_Glow нет — сила ореола не переопределена"));
	if (GlowSelectMID)
	{
		float Mode = 0.f;   // режим маски живого материала (1 первая группа / 2 мобы / 3 обе / 4 весь кадр)
		GlowSelectMID->GetScalarParameterValue(TEXT("MaskMode"), Mode);
		S += FString::Printf(TEXT("; выборочное свечение M_AsdaGlowSelect, режим маски %.0f"), Mode);
	}
	return S;
}

void UAsdaVideoSettingsSubsystem::ApplyGlowSelect(UWorld* World, APostProcessVolume* Vol)
{
	if (!World || !Vol) { return; }
	const bool bLogin = IsLoginSceneWorld(World);   // второй путь кадра эталона (С2)
	// Материал нужен: вне логина — пунктам 1..3; на логине — только 1 и 3 (кадр целиком), на 2 эталон кадр не берёт.
	const bool bSelective = bLogin ? (GlowLevel == 1 || GlowLevel == 3) : (GlowLevel >= 1 && GlowLevel <= 3);

	// Снять прежний экземпляр с объёма (пункт мог смениться или выключиться).
	if (GlowSelectMID)
	{
		Vol->Settings.WeightedBlendables.Array.RemoveAll([this](const FWeightedBlendable& B) { return B.Object == GlowSelectMID; });
	}
	// Пометки групп — под текущий пункт (на логине и на 0/4 — снять свои).
	const int32 Changed = BindGlowMarks(World);
	if (!bSelective)
	{
		GlowSelectMID = nullptr;
		UE_LOG(LogAsdaVideo, Log, TEXT("Свечение %d: выборочного свечения нет%s; пометок изменено %d"), GlowLevel,
			(bLogin && GlowLevel == 2) ? TEXT(" (сцена логина: эталон на «Монстр» кадр не сводит)") : TEXT(""), Changed);
		return;
	}
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, kGlowSelectMatPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Base)
	{
		UE_LOG(LogAsdaVideo, Warning, TEXT("Свечение %d: материал %s не найден — выборочного свечения нет (собери его Tools/asda_build_glow_select_mat.py)"),
			GlowLevel, kGlowSelectMatPath);
		return;
	}
	// Числа эталона — только из таблицы [AsdaGlow] (в C++ не дублируются): нет строки — свечения нет.
	static const TCHAR* const kKeys[6] = { TEXT("UnitOffset"), TEXT("UnitBlurFactor"), TEXT("UnitComposeFactor"),
		TEXT("BlurBufSize"), TEXT("BlurBufSizeWide"), TEXT("BlurBufWideFromWidth") };
	float Vals[6] = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };   // значения строк kKeys по порядку
	for (int32 K = 0; K < 6; ++K)
	{
		if (!GConfig || !GConfig->GetFloat(TEXT("AsdaGlow"), kKeys[K], Vals[K], GScalabilityIni))
		{
			UE_LOG(LogAsdaVideo, Warning, TEXT("Свечение %d: в [AsdaGlow] (%s) нет строки %s — выборочного свечения нет"),
				GlowLevel, *GScalabilityIni, kKeys[K]);
			return;
		}
	}
	// Страховка трафарета: проектное значение 3 (DefaultEngine.ini). Опущено кем-то ниже — поднять ОДИН раз и
	//   НЕ возвращать (каждая смена ключа — перерегистрация всего мира), запечённых мобов усыпить снова.
	if (IConsoleVariable* CD = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepth")))
	{
		if (CD->GetInt() < 3)
		{
			UE_LOG(LogAsdaVideo, Warning, TEXT("Свечение %d: r.CustomDepth=%d, а трафарету нужно 3 (DefaultEngine.ini) — поднимаю"), GlowLevel, CD->GetInt());
			CD->Set(3, ECVF_SetByCode);
			ResleepBakedMobs(World);
		}
	}
	// Режим маски материала: 1 — первая группа, 2 — мобы, 3 — обе; 4 — весь кадр (сцена логина, пункты 1/3).
	const float Mode = bLogin ? 4.f : (float)GlowLevel;
	GlowSelectMID = UMaterialInstanceDynamic::Create(Base, this);
	GlowSelectMID->SetScalarParameterValue(TEXT("BlurOffset"), Vals[0]);
	GlowSelectMID->SetScalarParameterValue(TEXT("BlurFactor"), Vals[1]);
	GlowSelectMID->SetScalarParameterValue(TEXT("ComposeFactor"), Vals[2]);
	GlowSelectMID->SetScalarParameterValue(TEXT("BufSize"), Vals[3]);
	GlowSelectMID->SetScalarParameterValue(TEXT("BufSizeWide"), Vals[4]);
	GlowSelectMID->SetScalarParameterValue(TEXT("WideFromWidth"), Vals[5]);
	GlowSelectMID->SetScalarParameterValue(TEXT("MaskMode"), Mode);
	GlowSelectMID->SetScalarParameterValue(TEXT("MobStencil"), (float)kGlowMobStencil);
	GlowSelectMID->SetScalarParameterValue(TEXT("CharStencil"), (float)kGlowCharStencil);
	// ★ 19.09.2026 (glow_fix5, С3; glow_fix6): «туман-альфа» карты — вес пикселя группы = fog (разбор в шапке раздела).
	//   [ПОРТ] (С2): сводится lerp(кадр С ТЕЛОМ, o, fog), в эталоне — lerp(фон за группой, o, fog) — разбор в шапке.
	//   Карта мира = ассет уровня (/Game/Map/<Asset>/<Asset>, OpenLevel по короткому имени); записи нет (логин,
	//   служебный уровень) — Near = Far = 1: вес без тумана, как у ближнего плана.
	const FString LevelAsset = UWorld::RemovePIEPrefix(FPackageName::GetShortName(World->GetOutermost()->GetName()));   // имя ассета уровня
	const AsdaMapRegistry::FSceFog* Fog = AsdaMapRegistry::FindSceFogByAsset(LevelAsset);   // запись .SCE карты
	GlowSelectMID->SetScalarParameterValue(TEXT("FogStart"), Fog ? Fog->Start : 0.f);
	GlowSelectMID->SetScalarParameterValue(TEXT("FogEnd"), Fog ? Fog->End : 1.f);
	GlowSelectMID->SetScalarParameterValue(TEXT("FogNear"), Fog ? Fog->Near : 1.f);
	GlowSelectMID->SetScalarParameterValue(TEXT("FogFar"), Fog ? Fog->Far : 1.f);
	UE_LOG(LogAsdaVideo, Log, TEXT("Свечение %d: туман-альфа карты «%s»: %s"), GlowLevel, *LevelAsset,
		Fog ? *FString::Printf(TEXT("начало %.0f, конец %.0f, у камеры %.2f, в конце %.2f (map\\.SCE, id %d)"), Fog->Start, Fog->End, Fog->Near, Fog->Far, Fog->MapId)
			: TEXT("записи в реестре нет — вес без тумана"));
	Vol->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, GlowSelectMID));
	int32 NChar = 0, NMob = 0;   // учтённых акторов первой группы и мобов (для лога)
	for (const TWeakObjectPtr<AActor>& W : GlowTracked)
	{
		const int32 G = GlowGroupOf(W.Get());
		NChar += (G == 1) ? 1 : 0;
		NMob += (G == 2) ? 1 : 0;
	}
	UE_LOG(LogAsdaVideo, Log, TEXT("Свечение %d: выборочное свечение по эталону (режим маски %.0f, сдвиг %.2f, размытие %.0f/255, сведение %.0f/255, буфер %.0f/%.0f с ширины %.0f); учтено акторов: первая группа %d, мобы %d; пометок изменено %d"),
		GlowLevel, Mode, Vals[0], Vals[1], Vals[2], Vals[3], Vals[4], Vals[5], NChar, NMob, Changed);
}

// ★ 19.09.2026 (хвосты ревизии, замер цены пунктов 1/2): смена `r.CustomDepth` в движке — это
//   `OnCustomDepthChanged` → `FGlobalComponentReregisterContext` (Engine/Private/SceneUtils.cpp:24-30), то есть
//   перерегистрация ВСЕХ компонентов мира, и она будит усыплённые компоненты запечённых мобов (сон — волна CPU
//   07.09, AsdaMobActor.cpp BeginPlay). Прогон P4: после `r.CustomDepth 3→1` тиков скелетных мешей 258 → 644,
//   отрисовок Slate 1.9 → 774, игровой поток 3.7 → 10.2 мс — и так до конца сеанса, на ЛЮБОМ пункте свечения.
//   Поэтому сразу после смены ключа запечённые (скрытые, не сетевые) мобы усыпляются снова — тем же правилом
//   и тем же ключом отката `asda.Tick.DormantBaked`, что и в BeginPlay. Показанных (`look`) не трогаем.
//   ★ 19.09.2026 (реверс эталона): ключ = 3 теперь держит DefaultEngine.ini, смена пункта его не трогает;
//   функция нужна только страховке ApplyGlowSelect (ключ опустили ниже 3 консолью/чужим кодом).
void UAsdaVideoSettingsSubsystem::ResleepBakedMobs(UWorld* World)
{
	if (!World || !World->IsGameWorld()) { return; }
	// ★ 19.09.2026 (ревизия, МЕЛК): при `asda.Tick.DormantBaked 0` BeginPlay оставляет компоненты запечённых
	//   мобов бодрствующими, а тик самого актора всё равно выключает (AsdaMobActor.cpp BeginPlay). Будить здесь
	//   нечего, а прежнее «разбудить → через 0.3 с НЕ усыплять» оставляло тик актора включённым навсегда.
	//   Состояние после перерегистрации и так совпадает с BeginPlay — выходим, ничего не трогая.
	static IConsoleVariable* CVarDormant = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Tick.DormantBaked"));
	if (CVarDormant && CVarDormant->GetInt() == 0) { return; }
	// Не сразу, а через 0.3 с: перерегистрация заново создала экранные виджеты плашек (OnRegister →
	//   EnsureScreenWidget, AsdaNameplateComponent.cpp:273) видимыми в углу экрана (прогон P5: отрисовок
	//   Slate 2 → 772, игровой поток UI +6 мс). Прячет их только тик плашки (скрытый владелец / гейт
	//   значимости), а тик плашки перерегистрация НЕ будит (P6: тиков +386 — только скелетные меши), поэтому
	//   сначала будим компоненты целиком, даём несколько кадров тика и лишь затем усыпляем.
	for (TActorIterator<AAsdaMobActor> It(World); It; ++It)
	{
		if (!It->bNetSpawned && It->IsHidden()) { It->SetDormantComponentTicks(true); }
	}
	TWeakObjectPtr<UWorld> WeakW(World);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakW](float) -> bool
	{
		UWorld* W = WeakW.Get();
		if (!W) { return false; }
		// Ключ отката мог смениться за эти 0.3 с: при 0 — как BeginPlay, компоненты бодрствуют, тик актора выключен.
		const bool bSleepComps = !(CVarDormant && CVarDormant->GetInt() == 0);
		int32 N = 0;
		for (TActorIterator<AAsdaMobActor> It(W); It; ++It)
		{
			if (It->bNetSpawned || !It->IsHidden()) { continue; }
			if (bSleepComps) { It->SetDormantComponentTicks(false); }
			else { It->SetActorTickEnabled(false); }
			++N;
		}
		UE_LOG(LogAsdaVideo, Log, TEXT("Свечение: r.CustomDepth сменён (движок перерегистрировал компоненты) — запечённых мобов снова усыплено: %d"), N);
		return false;   // разово
	}), 0.3f);
}

void UAsdaVideoSettingsSubsystem::DebugGlowSelect(const TArray<FString>& Args)
{
	UGameInstance* GI = GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	// ★ 19.09.2026 (ревизия С1/С2): `fx <N> item <itemId> <заточка> <wingId> <кольцо 0|1>` — эффекты предметов на
	//   N-го игрока штатным путём пакетов (UAsdaWorldSubsystem::DebugApplyItemFx); `fx <N> soul <0|1>` — режим
	//   «душа» (SetSoulTranslucent, как SoulState 1); `fx <N> fade <сек>` — появление (UAsdaFadeComponent, как
	//   у входа игрока в мир); `fx <N> still <0|1>` — заморозка времени игрока и его эффектов (пары кадров).
	//   Проверка кадрами пунктов 1/3 без сервера; числа предметов задаёт вызывающий.
	if (Args.Num() > 2 && Args[0].Equals(TEXT("fx"), ESearchCase::IgnoreCase))
	{
		const int32 WantP = FMath::Max(0, FCString::Atoi(*Args[1]));   // номер игрока в мире
		AAsdaPlayerCharacter* P = nullptr;   // найденный игрок
		int32 Idx = 0;                       // счётчик перебора игроков
		for (TActorIterator<AAsdaPlayerCharacter> It(World); It && !P; ++It)
		{
			if (Idx++ == WantP) { P = *It; }
		}
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!P || !WS) { UE_LOG(LogAsdaVideo, Warning, TEXT("asda.Video.GlowSelect fx: игрок №%d не найден"), WantP); return; }
		const FString What = Args[2].ToLower();   // вид проверки: item / soul / fade
		auto ArgI = [&Args](int32 K) { return Args.IsValidIndex(K) ? FCString::Atoi(*Args[K]) : 0; };
		if (What == TEXT("item")) { WS->DebugApplyItemFx(P, ArgI(3), ArgI(4), ArgI(5), ArgI(6) != 0); }
		else if (What == TEXT("soul")) { P->SetSoulTranslucent(ArgI(3) != 0); }
		else if (What == TEXT("fade")) { UAsdaFadeComponent::Begin(P, /*bIn*/ true, FMath::Max(0.1f, (float)FCString::Atof(Args.IsValidIndex(3) ? *Args[3] : TEXT("1"))), /*bDestroyOwner*/ false); }
		// ★ 19.09.2026 (glow_fix4): `fx <N> hide <0|1>` — GM-«скрыться» штатным путём (тело α asda.Gm.HideAlpha,
		//   эффекты — asda.Gm.HideFxAlpha): проверка веса свечения полупрозрачного тела и притушенных эффектов.
		else if (What == TEXT("hide")) { P->SetGmHideTranslucent(ArgI(3) != 0); }
		// ★ 19.09.2026 (glow_fix5, С4): `fx <N> behind <M> <см>` — поставить игрока N за игрока M по взгляду текущей
		//   камеры на <см> дальше (та же высота): проверка «тело группы за полупрозрачной «душой»» кадром.
		else if (What == TEXT("behind"))
		{
			AAsdaPlayerCharacter* Front = nullptr;   // игрок, за которого ставим
			int32 J = 0;                             // счётчик перебора игроков
			for (TActorIterator<AAsdaPlayerCharacter> It(World); It && !Front; ++It)
			{
				if (J++ == ArgI(3)) { Front = *It; }
			}
			APlayerController* PCb = GI ? GI->GetFirstLocalPlayerController(World) : nullptr;   // вид камеры
			if (!Front || Front == P || !PCb || !PCb->PlayerCameraManager)
			{
				UE_LOG(LogAsdaVideo, Warning, TEXT("asda.Video.GlowSelect fx behind: нет игрока №%d, он же сам или нет камеры"), ArgI(3));
				return;
			}
			FVector Fwd = PCb->PlayerCameraManager->GetCameraRotation().Vector();   // взгляд камеры
			Fwd.Z = 0.f;
			Fwd = Fwd.GetSafeNormal();
			const float Dist = Args.IsValidIndex(4) ? (float)FCString::Atof(*Args[4]) : 150.f;   // отступ за спину, см
			P->SetActorLocation(Front->GetActorLocation() + Fwd * Dist, /*bSweep*/ false, nullptr, ETeleportType::TeleportPhysics);
			P->SetActorRotation(Front->GetActorRotation());
			UE_LOG(LogAsdaVideo, Display, TEXT("asda.Video.GlowSelect fx behind: %s поставлен за %s на %.0f см по взгляду камеры"),
				*P->GetName(), *Front->GetName(), Dist);
		}
		else if (What == TEXT("still"))
		{
			// Заморозить (1) / разморозить (0) симуляцию эффектов предметов и появления игрока: пары кадров
			//   «пункт / контроль» сравнимы попиксельно только при неподвижных частицах (тик × CustomTimeDilation).
			const float Dil = (ArgI(3) != 0) ? 0.f : 1.f;   // множитель времени тика
			P->CustomTimeDilation = Dil;
			for (AActor* C : P->Children) { if (C) { C->CustomTimeDilation = Dil; } }
		}
		else { UE_LOG(LogAsdaVideo, Warning, TEXT("asda.Video.GlowSelect fx: неизвестный вид «%s» (item|soul|fade|hide|still|behind)"), *What); return; }
		// Опись: эффекты предметов носителя и их пометка трафаретом.
		// Пометка первой группы = 10 (★ glow_fix6: полос α нет); печать — значения трафарета тела и эффектов.
		int32 NFx = 0, NFxMarked = 0;   // эффектов предметов у игрока / из них с пометкой первой группы
		FString FxVals;                 // значения трафарета эффектов (печать)
		for (AActor* C : P->Children)
		{
			if (!IsGlowCarriedFx(C, P)) { continue; }
			++NFx;
			TInlineComponentArray<UMeshComponent*> Ms(C);
			for (const UMeshComponent* M : Ms)
			{
				if (M && M->bRenderCustomDepth && GlowStencilGroup(M->CustomDepthStencilValue) == 1)
				{
					++NFxMarked;
					FxVals += FString::Printf(TEXT(" %d"), M->CustomDepthStencilValue);
					break;
				}
			}
		}
		FString BodyVals;   // значения трафарета мешей самого игрока (печать)
		FString BodyWho;    // ★ glow_fix5 (МЕЛК а): чей это меш — имя компонента, трафарет, материал слота 0, α веса
		{
			TInlineComponentArray<UMeshComponent*> Ms(P);
			for (UMeshComponent* M : Ms)
			{
				if (M && M->bRenderCustomDepth && BodyVals.Len() < 200) { BodyVals += FString::Printf(TEXT(" %d"), M->CustomDepthStencilValue); }
				if (M && M->bRenderCustomDepth && BodyWho.Len() < 900)
				{
					const UMaterialInterface* M0 = M->GetNumMaterials() > 0 ? M->GetMaterial(0) : nullptr;   // материал слота 0
					BodyWho += FString::Printf(TEXT(" [%s=%d %s слотов %d α %.2f]"), *M->GetName(), M->CustomDepthStencilValue,
						*GetNameSafe(M0), M->GetNumMaterials(), GlowAlphaOfMesh(M));
				}
			}
		}
		UE_LOG(LogAsdaVideo, Display, TEXT("asda.Video.GlowSelect fx: меши тела с трафаретом:%s"), *BodyWho);
		// ★ glow_fix5 (С4): пишет ли мастер «души» глубину/скорость (проход TranslucentVelocity берёт только материалы
		//   с IsTranslucencyWritingVelocity, VelocityRendering.cpp:1092) и его порог отсечки.
		if (const UMaterial* SoulM = LoadObject<UMaterial>(nullptr, AsdaMat::kCharLitSoulPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			UE_LOG(LogAsdaVideo, Display, TEXT("asda.Video.GlowSelect fx: мастер «души» %s: скорость/глубина=%d, трафарет=%d, порог=%.4f, тень-маска=%d"),
				*SoulM->GetName(), SoulM->IsTranslucencyWritingVelocity() ? 1 : 0, SoulM->IsTranslucencyWritingCustomDepth() ? 1 : 0,
				SoulM->GetOpacityMaskClipValue(), SoulM->GetCastDynamicShadowAsMasked() ? 1 : 0);
		}
		UE_LOG(LogAsdaVideo, Display, TEXT("asda.Video.GlowSelect fx: игрок %s (№%d) — %s; эффектов предметов %d, из них с трафаретом первой группы: %d [%s]; трафарет тела [%s]"),
			*P->GetName(), WantP, *What, NFx, NFxMarked, *FxVals, *BodyVals);
		return;
	}
	// ★ 19.09.2026 (glow_fix5, С3): `fog <начало> <конец> <у камеры> <в конце>` — подменить туман-альфу живого
	//   материала выборочного свечения (до следующей смены пункта/мира, там снова числа карты из реестра): проверка
	//   кадрами, что вес линеен по туману (разность «пункт − без свечения» при «у камеры» 0.5 — ровно половина).
	if (Args.Num() > 4 && Args[0].Equals(TEXT("fog"), ESearchCase::IgnoreCase))
	{
		if (!GlowSelectMID) { UE_LOG(LogAsdaVideo, Warning, TEXT("asda.Video.GlowSelect fog: выборочное свечение не включено (пункты 1..3)")); return; }
		static const TCHAR* const kFogP[4] = { TEXT("FogStart"), TEXT("FogEnd"), TEXT("FogNear"), TEXT("FogFar") };   // параметры материала
		for (int32 K = 0; K < 4; ++K) { GlowSelectMID->SetScalarParameterValue(kFogP[K], (float)FCString::Atof(*Args[1 + K])); }
		UE_LOG(LogAsdaVideo, Display, TEXT("asda.Video.GlowSelect fog: начало %s, конец %s, у камеры %s, в конце %s"), *Args[1], *Args[2], *Args[3], *Args[4]);
		return;
	}
	// `look [N]` — камера лицом к N-му по величине мобу (0 = самый крупный), моб в центре кадра (проверка
	//   «только мобы» глазами и попиксельно).
	//   ★ 19.09.2026 (хвосты ревизии): прежний `BugItGo` без павна (офлайн павна нет — его спавнит 4003) двигал
	//   только ПОВОРОТ контроллера, камера оставалась под рельефом. Теперь — своя транзиентная камера
	//   (вид контроллера переводится на неё), место подбирается по ВИДИМЫМ границам мешей моба: 8 сторон,
	//   луч обзора до центра не перекрыт, камера выше земли. Печать — экранная рамка моба в пикселях вьюпорта.
	if (Args.Num() > 0 && Args[0].Equals(TEXT("look"), ESearchCase::IgnoreCase))
	{
		APlayerController* PC = GI ? GI->GetFirstLocalPlayerController(World) : nullptr;
		if (!World || !PC) { UE_LOG(LogAsdaVideo, Warning, TEXT("asda.Video.GlowSelect look: нет мира или контроллера")); return; }
		// `look still [0|1]` — заморозить (1) / разморозить (0) позу моба проверки: попиксельное сравнение
		//   пунктов свечения требует неподвижного кадра (офлайн `pause`/`slomo` в -game не действуют — прогон G5).
		if (Args.Num() > 1 && Args[1].Equals(TEXT("still"), ESearchCase::IgnoreCase))
		{
			AAsdaMobActor* Mob = GlowLookMob.Get();
			const bool bStill = (Args.Num() < 3) || FCString::Atoi(*Args[2]) != 0;
			if (!Mob) { UE_LOG(LogAsdaVideo, Warning, TEXT("asda.Video.GlowSelect look still: нет моба проверки (сначала `look`)")); return; }
			Mob->SetDormantComponentTicks(!bStill);
			// Тика компонентов мало (прогон G8: поза всё равно менялась между кадрами) — останавливаем и сами клипы.
			TInlineComponentArray<USkeletalMeshComponent*> Skels(Mob);
			for (USkeletalMeshComponent* SK : Skels)
			{
				if (!SK) { continue; }
				SK->bPauseAnims = bStill;
				SK->GlobalAnimRateScale = bStill ? 0.f : 1.f;
			}
			UE_LOG(LogAsdaVideo, Display, TEXT("asda.Video.GlowSelect look still: поза %s %s"), *Mob->GetName(), bStill ? TEXT("заморожена") : TEXT("снова анимируется"));
			return;
		}
		// `look sun` — камера проверки (после `look`) разворачивается НА солнце: проверка бликов объектива.
		if (Args.Num() > 1 && Args[1].Equals(TEXT("sun"), ESearchCase::IgnoreCase))
		{
			ACameraActor* Cm = GlowLookCam.Get();
			const UDirectionalLightComponent* Sun = nullptr;
			for (TObjectIterator<UDirectionalLightComponent> It; It; ++It)
			{
				if (It->GetWorld() == World && It->IsRegistered() && (!Sun || It->IsUsedAsAtmosphereSunLight())) { Sun = *It; }
			}
			if (!Cm || Cm->GetWorld() != World || !Sun) { UE_LOG(LogAsdaVideo, Warning, TEXT("asda.Video.GlowSelect look sun: нет камеры проверки (сначала `look`) или солнца")); return; }
			const FRotator Rot = (-Sun->GetDirection()).Rotation();
			Cm->SetActorRotation(Rot);
			PC->SetViewTargetWithBlend(Cm, 0.f);
			UE_LOG(LogAsdaVideo, Display, TEXT("asda.Video.GlowSelect look sun: камера %s смотрит на солнце %s, поворот %s"),
				*Cm->GetActorLocation().ToCompactString(), *Sun->GetOwner()->GetName(), *Rot.ToCompactString());
			return;
		}
		// ★ 19.09.2026 (ревизия К1): `look player [N]` — камера к N-му игроку (первая группа) и заморозка его
		//   клипов: проверка пункта 1 кадрами (игрок меняется, застройка — нет). Офлайн игрока даёт
		//   `asda.PlayerMenu.FakePeer`.
		if (Args.Num() > 1 && Args[1].Equals(TEXT("player"), ESearchCase::IgnoreCase))
		{
			const int32 WantP = (Args.Num() > 2) ? FMath::Max(0, FCString::Atoi(*Args[2])) : 0;   // номер игрока в мире
			// ★ 19.09.2026: необязательная дистанция камеры, см (0 — как было, 3 радиуса тела): рамка эффектов
			//   предметов раздувает радиус до потолка 400, и игрок в кадре выходил 30×70 пикселей.
			const float WantDist = (Args.Num() > 3) ? FMath::Max(0.f, (float)FCString::Atof(*Args[3])) : 0.f;
			int32 Idx = 0;   // счётчик перебора игроков
			for (TActorIterator<AAsdaPlayerCharacter> It(World); It; ++It)
			{
				if (Idx++ < WantP) { continue; }
				AAsdaPlayerCharacter* P = *It;
				FVector Org, Ext;
				P->GetActorBounds(/*bOnlyCollidingComponents*/ false, Org, Ext);
				const float R = FMath::Clamp(Ext.Size(), 60.f, 400.f);   // радиус тела (плашка/тени могут раздуть рамку)
				const FVector Ctr = P->GetActorLocation() + FVector(0.f, 0.f, R * 0.3f);
				FCollisionQueryParams Q(SCENE_QUERY_STAT(AsdaGlowLookPlayer), /*bTraceComplex*/ true, P);
				for (int32 Side = 0; Side < 8; ++Side)
				{
					const float D = (WantDist > 0.f) ? WantDist : R * 3.f;   // дистанция камеры до центра тела
					const FVector Cam = Ctr + FRotator(0.f, Side * 45.f, 0.f).Vector() * D + FVector(0.f, 0.f, D / 6.f);
					FHitResult H;
					if (World->LineTraceSingleByChannel(H, Cam, Ctr, ECC_Visibility, Q)) { continue; }   // обзор перекрыт
					const FRotator Rot = (Ctr - Cam).Rotation();
					ACameraActor* Cm = GlowLookCam.Get();
					if (!Cm || Cm->GetWorld() != World)
					{
						FActorSpawnParameters SP;
						SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
						SP.ObjectFlags |= RF_Transient;
						Cm = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Cam, Rot, SP);
						if (Cm && Cm->GetCameraComponent()) { Cm->GetCameraComponent()->SetConstraintAspectRatio(false); }
						GlowLookCam = Cm;
					}
					if (!Cm) { return; }
					Cm->SetActorLocationAndRotation(Cam, Rot);
					PC->SetViewTargetWithBlend(Cm, 0.f);
					// Поза неподвижна — пары кадров сравнимы.
					TInlineComponentArray<USkeletalMeshComponent*> Skels(P);
					for (USkeletalMeshComponent* SK : Skels)
					{
						if (SK) { SK->bPauseAnims = true; SK->GlobalAnimRateScale = 0.f; }
					}
					UE_LOG(LogAsdaVideo, Display, TEXT("asda.Video.GlowSelect look player: игрок %s (№%d), центр %s, радиус %.0f; камера %s поворот %s"),
						*P->GetName(), WantP, *Ctr.ToCompactString(), R, *Cam.ToCompactString(), *Rot.ToCompactString());
					return;
				}
			}
			UE_LOG(LogAsdaVideo, Warning, TEXT("asda.Video.GlowSelect look player: игрок №%d не найден или обзор перекрыт со всех сторон"), WantP);
			return;
		}
		// Кандидат: моб и рамка его тела — капсула пикинга (подогнана под модель, AsdaPickHitbox); границы
		//   скелетного меша у мобов раздуты (8 м у волка), по ним «крупный план» промахивается.
		struct FCand { AAsdaMobActor* Mob; FBox Box; };
		TArray<FCand> Cands;
		int32 NoCap = 0, Hidden = 0, NoModel = 0;   // отсеянные: без капсулы / скрытые / без видимой модели
		for (TActorIterator<AAsdaMobActor> It(World); It; ++It)
		{
			const UCapsuleComponent* Cap = It->PickHitbox;
			if (!Cap || !Cap->IsRegistered()) { ++NoCap; continue; }
			// Скрытые НЕ отсеиваем: офлайн (без сервера) все мобы карты — запечённые шаблоны, скрытые до 4013
			//   (AsdaMobActor.cpp, BeginPlay `!bNetSpawned`). Выбранного покажем так же, как `asda.Target.RingNearest mob`.
			if (It->IsHidden()) { ++Hidden; }
			// Моб без видимой модели (не подгружена/скрыта) в кадре не виден — такой кандидат бесполезен.
			bool bModel = false;
			TInlineComponentArray<UMeshComponent*> Meshes(*It);
			for (const UMeshComponent* M : Meshes)
			{
				if (!M || !M->IsVisible()) { continue; }
				if (const USkinnedMeshComponent* SK = Cast<USkinnedMeshComponent>(M)) { bModel |= SK->GetSkinnedAsset() != nullptr; }
				else if (const UStaticMeshComponent* SM = Cast<UStaticMeshComponent>(M)) { bModel |= SM->GetStaticMesh() != nullptr; }
			}
			if (!bModel) { ++NoModel; continue; }
			const float CR = Cap->GetScaledCapsuleRadius(), CH = Cap->GetScaledCapsuleHalfHeight();
			const FVector P = Cap->GetComponentLocation();
			const FBox B(P - FVector(CR, CR, CH), P + FVector(CR, CR, CH));
			if (CH > 20.f) { Cands.Add({ *It, B }); }
		}
		Cands.Sort([](const FCand& A, const FCand& B) { return A.Box.GetExtent().Size() > B.Box.GetExtent().Size(); });
		FString Top;
		for (int32 K = 0; K < FMath::Min(8, Cands.Num()); ++K)
		{
			Top += FString::Printf(TEXT(" [%s r=%.0f]"), *Cands[K].Mob->GetName(), Cands[K].Box.GetExtent().Size());
		}
		UE_LOG(LogAsdaVideo, Display, TEXT("asda.Video.GlowSelect look: мобов-кандидатов %d (отсеяно: без капсулы %d, без модели %d; скрытых среди всех %d), крупнейшие:%s"), Cands.Num(), NoCap, NoModel, Hidden, *Top);
		// Заслоны обзора по ВИДИМЫМ границам (у деревьев карты коллизии может не быть — луч физики их не видит):
		//   рамки статических мешей и каждого экземпляра ISM; огромные (> 50 м — рельеф/небо) пропускаем, рельеф
		//   проверяет луч физики ниже.
		TArray<FBox> Occ;
		for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
		{
			UStaticMeshComponent* S = *It;
			if (!S || S->GetWorld() != World || !S->IsRegistered() || !S->IsVisible() || S->GetOwner() == nullptr || S->GetOwner()->IsA<AAsdaMobActor>()) { continue; }
			if (UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(S))
			{
				if (!ISM->GetStaticMesh()) { continue; }
				const FBox MB = ISM->GetStaticMesh()->GetBounds().GetBox();
				FTransform T;
				for (int32 I = 0; I < ISM->GetInstanceCount(); ++I)
				{
					if (ISM->GetInstanceTransform(I, T, /*bWorldSpace*/ true)) { const FBox WB = MB.TransformBy(T); if (WB.GetExtent().Size() < 5000.f) { Occ.Add(WB); } }
				}
			}
			else if (S->Bounds.BoxExtent.Size() < 5000.f) { Occ.Add(S->Bounds.GetBox()); }
		}
		const int32 Want = (Args.Num() > 1) ? FMath::Max(0, FCString::Atoi(*Args[1])) : 0;
		int32 Seen = 0;
		int32 NOcc = 0, NRay = 0, NGround = 0;   // отказы по видимым заслонам / лучу физики / земле над камерой
		for (const FCand& C : Cands)
		{
			const FVector Ctr = C.Box.GetCenter();
			const float R = C.Box.GetExtent().Size();
			const float Dist = FMath::Max(250.f, R * 2.2f);   // моб ≈ половина высоты кадра при FOV 90
			FCollisionQueryParams Q(SCENE_QUERY_STAT(AsdaGlowLook), /*bTraceComplex*/ true, C.Mob);
			for (int32 Side = 0; Side < 8; ++Side)
			{
				const float Yaw = Side * 45.f;
				const FVector Cam = Ctr + FRotator(0.f, Yaw, 0.f).Vector() * Dist + FVector(0.f, 0.f, R * 0.35f);
				// Видимые заслоны: отрезок камера→центр моба и сама камера не должны задевать чужие рамки.
				bool bBlocked = false;
				const FVector Dir = (Ctr - Cam);
				for (const FBox& O : Occ)
				{
					if (O.IsInside(Cam) || FMath::LineBoxIntersection(O, Cam, Ctr, Dir)) { bBlocked = true; break; }
				}
				if (bBlocked) { ++NOcc; continue; }
				FHitResult H;
				if (World->LineTraceSingleByChannel(H, Cam, Ctr, ECC_Visibility, Q)) { ++NRay; continue; }   // обзор перекрыт
				// Земля под камерой: луч сверху вниз; камера обязана быть выше первого попадания.
				FHitResult G;
				if (World->LineTraceSingleByChannel(G, Cam + FVector(0, 0, 20000.f), Cam - FVector(0, 0, 20000.f), ECC_Visibility, Q)
					&& G.ImpactPoint.Z > Cam.Z - 50.f) { ++NGround; continue; }
				if (Seen++ < Want) { break; }   // этот моб годится, но нужен следующий по величине
				const FRotator Rot = (Ctr - Cam).Rotation();
				ACameraActor* Cm = GlowLookCam.Get();
				if (!Cm || Cm->GetWorld() != World)
				{
					FActorSpawnParameters SP;
					SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					SP.ObjectFlags |= RF_Transient;
					Cm = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Cam, Rot, SP);
					if (Cm && Cm->GetCameraComponent()) { Cm->GetCameraComponent()->SetConstraintAspectRatio(false); }
					GlowLookCam = Cm;
				}
				if (!Cm) { UE_LOG(LogAsdaVideo, Warning, TEXT("asda.Video.GlowSelect look: камеру создать не удалось")); return; }
				Cm->SetActorLocationAndRotation(Cam, Rot);
				const bool bWasHidden = C.Mob->IsHidden();
				GlowLookMob = C.Mob;
				if (bWasHidden)
				{
					C.Mob->SetActorHiddenInGame(false);
					C.Mob->SetDormantComponentTicks(true);   // поза и плашка — как у живого (см. RingNearest)
				}
				PC->SetViewTargetWithBlend(Cm, 0.f);
				// Экранная рамка моба: 8 углов границ → пиксели вьюпорта (для попиксельного сравнения области моба).
				FVector2D Mn(1e9, 1e9), Mx(-1e9, -1e9);
				const FVector E = C.Box.GetExtent();
				int32 Proj = 0;
				for (int32 K = 0; K < 8; ++K)
				{
					const FVector Pt = Ctr + FVector((K & 1) ? E.X : -E.X, (K & 2) ? E.Y : -E.Y, (K & 4) ? E.Z : -E.Z);
					FVector2D S;
					// Проекция по положению новой камеры: вид контроллера обновится только в следующем кадре.
					const FVector L = Rot.UnrotateVector(Pt - Cam);   // X вперёд, Y вправо, Z вверх
					if (L.X <= 1.f) { continue; }
					int32 VX = 0, VY = 0;
					PC->GetViewportSize(VX, VY);
					const float Half = 0.5f * VX / FMath::Tan(FMath::DegreesToRadians(Cm->GetCameraComponent() ? Cm->GetCameraComponent()->FieldOfView * 0.5f : 45.f));
					S.X = VX * 0.5f + L.Y / L.X * Half;
					S.Y = VY * 0.5f - L.Z / L.X * Half;
					Mn.X = FMath::Min(Mn.X, S.X); Mn.Y = FMath::Min(Mn.Y, S.Y);
					Mx.X = FMath::Max(Mx.X, S.X); Mx.Y = FMath::Max(Mx.Y, S.Y);
					++Proj;
				}
				int32 VX = 0, VY = 0;
				PC->GetViewportSize(VX, VY);
				UE_LOG(LogAsdaVideo, Display, TEXT("asda.Video.GlowSelect look: моб %s%s (№%d по величине), центр %s, радиус %.0f; камера %s поворот %s; вьюпорт %dx%d; рамка моба (%.0f,%.0f)-(%.0f,%.0f) по %d углам"),
					*C.Mob->GetName(), bWasHidden ? TEXT(" (запечённый, показан для проверки)") : TEXT(""), Want, *Ctr.ToCompactString(), R, *Cam.ToCompactString(), *Rot.ToCompactString(), VX, VY, Mn.X, Mn.Y, Mx.X, Mx.Y, Proj);
				return;
			}
		}
		UE_LOG(LogAsdaVideo, Warning, TEXT("asda.Video.GlowSelect look: из %d мобов не нашлось места с открытым обзором (запрошен №%d; заслонов %d, отказов: рамки %d, луч %d, земля %d)"), Cands.Num(), Want, Occ.Num(), NOcc, NRay, NGround);
		return;
	}
	if (GlowSelectMID && Args.Num() > 0)
	{
		// ★ 19.09.2026: числа эталона — сдвиг копий (тексели буфера), множители размытия и сведения (из 255).
		if (Args.Num() > 0) { GlowSelectMID->SetScalarParameterValue(TEXT("BlurOffset"), FCString::Atof(*Args[0])); }
		if (Args.Num() > 1) { GlowSelectMID->SetScalarParameterValue(TEXT("BlurFactor"), FCString::Atof(*Args[1])); }
		if (Args.Num() > 2) { GlowSelectMID->SetScalarParameterValue(TEXT("ComposeFactor"), FCString::Atof(*Args[2])); }
		// Проверочные: режим маски (1 — первая группа; 2 — мобы; 3 — обе; 4 — весь кадр) и значение трафарета
		//   ОБЕИХ групп (заведомо чужое значение, напр. 999, — контроль «материал без группы кадр не меняет»).
		if (Args.Num() > 3) { GlowSelectMID->SetScalarParameterValue(TEXT("MaskMode"), FCString::Atof(*Args[3])); }
		if (Args.Num() > 4)
		{
			GlowSelectMID->SetScalarParameterValue(TEXT("MobStencil"), FCString::Atof(*Args[4]));
			GlowSelectMID->SetScalarParameterValue(TEXT("CharStencil"), FCString::Atof(*Args[4]));
		}
	}
	float Off = 0.f, Blur = 0.f, Comp = 0.f;   // действующие числа живого экземпляра материала
	if (GlowSelectMID)
	{
		GlowSelectMID->GetScalarParameterValue(TEXT("BlurOffset"), Off);
		GlowSelectMID->GetScalarParameterValue(TEXT("BlurFactor"), Blur);
		GlowSelectMID->GetScalarParameterValue(TEXT("ComposeFactor"), Comp);
	}
	int32 Mobs = 0, Marked = 0;
	// ★ 19.09.2026: и вся опись по мешам мира: помечено трафаретом первой группы / мобов / чужих акторов (NPC, лут,
	//   застройка — должно быть 0) — проверка обратной маски.
	int32 MarkedChar = 0, MarkedMob = 0, MarkedAlien = 0;
	FString Where;
	if (World)
	{
		for (TActorIterator<AAsdaMobActor> It(World); It; ++It)
		{
			++Mobs;
			TInlineComponentArray<UMeshComponent*> Meshes(*It);
			bool bAny = false;
			for (UMeshComponent* M : Meshes) { if (M && M->bRenderCustomDepth && GlowStencilGroup(M->CustomDepthStencilValue) == 2) { ++Marked; bAny = true; } }
			if (bAny && Where.Len() < 400) { Where += FString::Printf(TEXT(" [%s %s]"), *It->GetName(), *It->GetActorLocation().ToCompactString()); }
		}
		for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
		{
			if (It->GetWorld() != World || !It->bRenderCustomDepth) { continue; }
			const int32 V = It->CustomDepthStencilValue;
			const int32 VG = GlowStencilGroup(V);   // группа по значению (10/11)
			if (VG == 0) { continue; }
			const int32 G = GlowGroupOf(It->GetOwner());
			if (G == 1 && VG == 1) { ++MarkedChar; }
			else if (G == 2 && VG == 2) { ++MarkedMob; }
			else { ++MarkedAlien; }
		}
	}
	UE_LOG(LogAsdaVideo, Display, TEXT("asda.Video.GlowSelect: пункт %d, материал %s, сдвиг %.3f размытие %.1f сведение %.1f; мобов %d, помеченных мешей %d; меши мира с трафаретом: первая группа %d, мобы %d, чужие %d; учтено акторов %d;%s"),
		GlowLevel, GlowSelectMID ? TEXT("включён") : TEXT("нет"), Off, Blur, Comp, Mobs, Marked, MarkedChar, MarkedMob, MarkedAlien, GlowTracked.Num(), *Where);
}

// Консоль калибровки выборочного свечения (регистрация единственная: grep "asda.Video.GlowSelect" = 0 до фазы 2).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs CmdAsdaVideoGlowSelect(
	TEXT("asda.Video.GlowSelect"),
	TEXT("asda.Video.GlowSelect [сдвиг] [размытие] [сведение] [режим] [трафарет] | look — выборочное свечение пунктов 1..3: печать параметров, мобов и их мест; `look [N]` — камера к N-му по величине мобу (0 — самый крупный), печать его экранной рамки; `look sun` — та же камера на солнце (блики); `look still [0|1]` — заморозить позу моба проверки; `look player [N] [дистанция см]` — камера к N-му игроку (поза заморожена); ")
	TEXT("`fx <N> item <itemId> <заточка> <wingId> <кольцо 0|1>` / `fx <N> soul <0|1>` / `fx <N> fade <сек>` / `fx <N> hide <0|1>` / `fx <N> still <0|1>` — эффекты предметов, «душа», появление, GM-hide на N-м игроке, заморозка их времени (проверка без сервера); `fx <N> behind <M> <см>` — игрока N за игрока M по взгляду камеры; `fog <начало> <конец> <у камеры> <в конце>` — подменить туман-альфу живого материала; ")
	TEXT("с аргументами — подменить у живого материала на время сеанса (постоянные числа — [AsdaGlow] DefaultScalability.ini)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		if (UAsdaVideoSettingsSubsystem* Video = GI ? GI->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr)
		{
			Video->DebugGlowSelect(Args);
		}
	}));
#endif // ASDA_DEV_CONSOLE

// ═══ БЛИКИ ОБЪЕКТИВА (фаза 2) ═══════════════════════════════════════════════════════════════════════
//   У проекта `r.DefaultFeature.LensFlare=False` — сила бликов пост-обработки по умолчанию 0, поэтому один
//   `r.LensFlareQuality` (им до фазы 2 щёлкала строка dev-окна) на экране НИЧЕГО не включал. «Вкл» игрока =
//   качество 2 (его ставит профиль) + сила 1.0 (движковое значение по умолчанию) в служебном объёме;
//   «выкл» = сила 0; «не менялось» = объём силу не переопределяет (как было).
void UAsdaVideoSettingsSubsystem::ApplyLensFlareToVolume(APostProcessVolume* Vol) const
{
	if (!Vol) { return; }
	const UAsdaGameUserSettings* Profile = UAsdaGameUserSettings::Get();
	const int32 Flare = Profile ? Profile->LensFlare : -1;
	Vol->Settings.bOverride_LensFlareIntensity = (Flare >= 0);
	Vol->Settings.LensFlareIntensity = (Flare > 0) ? 1.f : 0.f;
}

bool UAsdaVideoSettingsSubsystem::IsLensFlareWanted() const
{
	const UAsdaGameUserSettings* Profile = UAsdaGameUserSettings::Get();
	return Profile && Profile->LensFlare > 0;
}

void UAsdaVideoSettingsSubsystem::ApplyGlowBloomQuality() const
{
	// Пункт 0..4 → r.BloomQuality: полный блум (5) — только у 4; у 0..3 блума нет (0), а при включённых бликах —
	//   «носитель» качества 1 (три стадии гауссиан, самый дешёвый), ореол которого гасит объём.
	//   ★ 19.09.2026: пункт 3 по эталону выборочный (материал), штатный блум ему не нужен.
	static const int32 Map[5] = { 0, 0, 0, 0, 5 };
	const int32 Idx = FMath::Clamp(GlowLevel, 0, 4);
	const int32 Q = (Map[Idx] == 0 && IsLensFlareWanted()) ? 1 : Map[Idx];
	if (IConsoleVariable* CV = IConsoleManager::Get().FindConsoleVariable(TEXT("r.BloomQuality")))
	{
		CV->Set(Q, ECVF_SetByGameSetting);
	}
	UE_LOG(LogAsdaVideo, Log, TEXT("Свечение (LEVEL OF GLOW)=%d, блики %s → r.BloomQuality=%d"), Idx, IsLensFlareWanted() ? TEXT("вкл") : TEXT("выкл"), Q);
}

void UAsdaVideoSettingsSubsystem::RefreshLensFlare()
{
	ApplyGlowBloomQuality();
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (World && World->IsGameWorld()) { ApplyGlowToWorld(World); }   // сила бликов + режим «только блики» в объёме
	else { ApplyLensFlareToVolume(GlowPPV.Get()); }
}

void UAsdaVideoSettingsSubsystem::SetGlowLevel(int32 Level0to4, bool bSave)
{
	// Пункт 0..4 → r.BloomQuality: штатный блум нужен ТОЛЬКО пункту 4 (кадр целиком, полное качество 5);
	//   у 0 свечения нет, у 1..3 свечение собирает выборочный материал, а штатный проход блума не нужен вовсе.
	//   ★ Фаза 2: прежняя лестница { 0, 2, 3, 4, 5 } трактовала пункт как «силу» — эталон так не делает (см. объявление).
	//   ★ 19.09.2026: при включённых бликах пункты 0..3 держат блум-«носитель» качества 1 (ApplyGlowBloomQuality).
	const int32 Idx = FMath::Clamp(Level0to4, 0, 4);
	GlowLevel = Idx;
	ApplyGlowBloomQuality();
	// ★ 19.09.2026 (фаза 2): ЕДИНСТВЕННОЕ хранилище — поле профиля (прежде строка [AsdaVideo] GlowLevel).
	if (UAsdaGameUserSettings* Profile = UAsdaGameUserSettings::Get())
	{
		Profile->GlowLevel = Idx;
		if (bSave) { Profile->SaveSettings(); }
	}
	// Текущему миру — сразу; каждому следующему — по подписке OnWorldInitializedActors (см. Initialize).
	if (UGameInstance* GI = GetGameInstance()) { ApplyGlowToWorld(GI->GetWorld()); }
}

// ═══ ★ ФАЗА 2: ОБЩИЙ API ВЫНЕСЕННЫХ РУЧЕК (разбор — в объявлении) ════════════════════════════════════
bool UAsdaVideoSettingsSubsystem::SetRenderOverride(const FString& CVarName, const FString& Value, bool bSave)
{
	UAsdaGameUserSettings* Profile = UAsdaGameUserSettings::Get();
	if (!Profile || !UAsdaGameUserSettings::IsProfileOwnedCVar(CVarName)) { return false; }
	Profile->SetCVarOverride(CVarName, Value, bSave);
	// Блики живут ещё и в служебном объёме (сила) — переложить её сразу.
	if (CVarName.Equals(TEXT("r.LensFlareQuality"), ESearchCase::IgnoreCase))
	{
		RefreshLensFlare();   // ★ 19.09.2026: и r.BloomQuality — на пунктах 0..3 блики держатся на блуме-«носителе»
	}
	return true;
}

bool UAsdaVideoSettingsSubsystem::ClearRenderOverride(const FString& CVarName, bool bSave)
{
	UAsdaGameUserSettings* Profile = UAsdaGameUserSettings::Get();
	if (!Profile || !UAsdaGameUserSettings::IsProfileOwnedCVar(CVarName)) { return false; }
	Profile->ClearCVarOverride(CVarName, bSave);
	if (CVarName.Equals(TEXT("r.LensFlareQuality"), ESearchCase::IgnoreCase))
	{
		RefreshLensFlare();   // ★ 19.09.2026: и r.BloomQuality — на пунктах 0..3 блики держатся на блуме-«носителе»
	}
	return true;
}

// ★ 19.09.2026: масштаб — через профиль (`UAsdaGameUserSettings::Get/SetRenderScalePercent`), диапазон
//   10…200 %: выше 100 — суперсэмплинг, который штатное поле движка (потолок 100) хранить не умеет.
int32 UAsdaVideoSettingsSubsystem::GetRenderScalePercent() const
{
	const UAsdaGameUserSettings* P = UAsdaGameUserSettings::Get();
	return P ? P->GetRenderScalePercent() : 100;
}

void UAsdaVideoSettingsSubsystem::SetRenderScalePercent(int32 Percent, bool bSave)
{
	UAsdaGameUserSettings* P = UAsdaGameUserSettings::Get();
	if (!P) { return; }
	P->SetRenderScalePercent(Percent);
	P->ApplyNonResolutionSettings();   // штатное sg.ResolutionQuality → r.ScreenPercentage, сверху — поле > 100
	if (bSave) { P->SaveSettings(); }
	UE_LOG(LogAsdaVideo, Log, TEXT("Масштаб рендера %d %%"), P->GetRenderScalePercent());
}

void UAsdaVideoSettingsSubsystem::SetFrameRateLimitFps(int32 Fps, bool bSave)
{
	UGameUserSettings* S = GetUserSettings();
	if (!S) { return; }
	S->SetFrameRateLimit((float)FMath::Max(0, Fps));
	S->ApplyNonResolutionSettings();
	if (bSave) { S->SaveSettings(); }
	UE_LOG(LogAsdaVideo, Log, TEXT("Лимит кадров: %s"), Fps > 0 ? *FString::FromInt(Fps) : TEXT("без лимита"));
}

void UAsdaVideoSettingsSubsystem::SetVSync(bool bOn, bool bSave)
{
	UGameUserSettings* S = GetUserSettings();
	if (!S) { return; }
	S->SetVSyncEnabled(bOn);
	S->ApplyNonResolutionSettings();
	if (bSave) { S->SaveSettings(); }
	UE_LOG(LogAsdaVideo, Log, TEXT("Вертикальная синхронизация: %s"), bOn ? TEXT("вкл") : TEXT("выкл"));
}

int32 UAsdaVideoSettingsSubsystem::GetWindowModeIndex() const
{
	UGameUserSettings* S = GetUserSettings();
	const EWindowMode::Type M = S ? S->GetFullscreenMode() : EWindowMode::WindowedFullscreen;
	return (M == EWindowMode::Fullscreen) ? 0 : (M == EWindowMode::WindowedFullscreen) ? 1 : 2;
}

void UAsdaVideoSettingsSubsystem::SaveProfile()
{
	if (UGameUserSettings* S = GetUserSettings()) { S->SaveSettings(); }
}

// ★ Уровень свечения: одиночная регистрация ключа (grep "asda.Video.Glow" по Source/ 09.09.2026 = 0;
//   закон ue-console-duplicate-registration — у команд выживает ПОСЛЕДНЯЯ регистрация).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs CmdAsdaVideoGlow(
	TEXT("asda.Video.Glow"),
	TEXT("asda.Video.Glow <0..4> — уровень свечения (LEVEL OF GLOW): сила ореола по таблице [AsdaGlow] Config/DefaultScalability.ini ")
	TEXT("(4 = калибровка владельца 09.09.2026: интенсивность 4.0, порог 2.5; 0 = свечения нет). Без аргумента — печать текущего."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		UAsdaVideoSettingsSubsystem* Video = GI ? GI->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr;
		if (!Video)
		{
			UE_LOG(LogAsdaVideo, Warning, TEXT("asda.Video.Glow: подсистема видео недоступна (нет GameInstance)"));
			return;
		}
		if (Args.Num() > 0)
		{
			Video->SetGlowLevel(FCString::Atoi(*Args[0]));
		}
		// ★ 19.09.2026 (ревизия, МЕЛК): печать — ДЕЙСТВУЮЩЕЕ состояние объёма, а не строка таблицы: на пунктах
		//   1..3 сила блума 0 (свечение даёт материал), и прежнее «интенсивность=4.000» вводило в заблуждение.
		const int32 L = Video->GetGlowLevel();
		UE_LOG(LogAsdaVideo, Display, TEXT("asda.Video.Glow: уровень=%d, %s"), L, *Video->DescribeEffectiveGlow());
	}));
#endif // ASDA_DEV_CONSOLE

// ★ 19.09.2026: масштаб рендера 10…200 % (выше 100 — суперсэмплинг) тем же путём, что строка «Масштаб рендера»
//   окна настроек и Ctrl+N; значение сохраняется в профиль. grep "asda.Video.RenderScale" по Source/ до
//   добавления = 0 (закон ue-console-duplicate-registration).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs CmdAsdaVideoRenderScale(
	TEXT("asda.Video.RenderScale"),
	TEXT("asda.Video.RenderScale <10..200> — масштаб рендера, % (выше 100 — суперсэмплинг), сохраняется в профиль. Без аргумента — печать текущего."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		UAsdaVideoSettingsSubsystem* Video = GI ? GI->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr;
		if (!Video)
		{
			UE_LOG(LogAsdaVideo, Warning, TEXT("asda.Video.RenderScale: подсистема видео недоступна (нет GameInstance)"));
			return;
		}
		if (Args.Num() > 0)
		{
			Video->SetRenderScalePercent(FCString::Atoi(*Args[0]), /*bSave*/ true);
		}
		IConsoleVariable* SP = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
		UE_LOG(LogAsdaVideo, Display, TEXT("asda.Video.RenderScale: профиль %d %%, r.ScreenPercentage=%.0f"),
			Video->GetRenderScalePercent(), SP ? SP->GetFloat() : -1.f);
	}));
#endif // ASDA_DEV_CONSOLE

void UAsdaVideoSettingsSubsystem::SetGamma(int32 Gamma0to100)
{
	// ★ 19.09.2026 (фаза 2): ЕДИНСТВЕННЫЙ путь яркости — его зовут и полоса 70058 окна настроек, и строка
	//   «Яркость» dev-окна Ctrl+N (прежде dev-окно писало свою `[AsdaVideo] DisplayGamma` и при старте
	//   побеждало, потому что накатывалось позже профиля).
	// config.txt GAMMA 0..100 (дефолт 50=нейтраль) → GEngine->DisplayGamma. 50→2.2 (дефолт UE); линейно 0→3.0 (темнее), 100→1.4 (светлее).
	// ★ 19.09.2026: значение живёт в профиле игрока — там же и формула (`ApplyGammaSetting`), чтобы
	//   сохранённая яркость накатывалась при старте тем же кодом, что и из окна настроек.
	if (UAsdaGameUserSettings* Profile = UAsdaGameUserSettings::Get())
	{
		Profile->Gamma = FMath::Clamp(Gamma0to100, 0, 100);
		Profile->ApplyGammaSetting();
		return;
	}
	// Профиль не зарегистрирован (чужой конфиг) — прежнее прямое действие без сохранения.
	const float G = FMath::Clamp((float)Gamma0to100, 0.f, 100.f);
	if (GEngine)
	{
		GEngine->DisplayGamma = FMath::Lerp(3.0f, 1.4f, G / 100.f);
	}
}

void UAsdaVideoSettingsSubsystem::ReapplyQuality()
{
	UGameUserSettings* S = GetUserSettings();
	if (S) { S->ApplyNonResolutionSettings(); }   // sg.* из UGameUserSettings (без force — no-op, если уровни не менялись)
	// bForce: переписать все CVar групп даже при неизменных уровнях — после Unset(ECVF_SetByConsole) значение пресета возвращается
	// и из истории приоритетов, это страховка. Ключи, пришпиленные проектным ini (ProjectSetting > Scalability), движок пропустит —
	// с E-0b таких пер-уровневых ключей в DefaultEngine.ini нет (перенесены в DefaultScalability.ini).
	Scalability::SetQualityLevels(Scalability::GetQualityLevels(), /*bForce*/true);
	UE_LOG(LogAsdaVideo, Log, TEXT("Пресет качества переприменён принудительно; %s"), *DescribeQualityLevels());
}

// ★ В-2 (review_setE0.md): эффективный флаг автопробника — ini ПЕРЕД CVar, потому что `-ExecCmds`/консоль ставят CVar уже после
//   InitializeStandalone (пробник отработал). Порядок: [AsdaVideo] GameUserSettings.ini (наш персист) → [SystemSettings] →
//   [ConsoleVariables] Engine.ini; если нигде нет ключа — значение CVar (по умолчанию 1).
static int32 ResolveAutoDetectFlag(int32 CVarValue)
{
	if (GConfig)
	{
		int32 V = 0;
		if (GConfig->GetInt(TEXT("AsdaVideo"), TEXT("asda.Video.AutoDetect"), V, GGameUserSettingsIni)) { return V; }
		if (GConfig->GetInt(TEXT("SystemSettings"), TEXT("asda.Video.AutoDetect"), V, GEngineIni)) { return V; }
		if (GConfig->GetInt(TEXT("ConsoleVariables"), TEXT("asda.Video.AutoDetect"), V, GEngineIni)) { return V; }
	}
	return CVarValue;
}

bool UAsdaVideoSettingsSubsystem::RunAutoDetect()
{
	// Флаг читаем из ini ДО замера (В-2); CVar — только если в ini ключа нет.
	const int32 AutoDetect = ResolveAutoDetectFlag(GAsdaVideoAutoDetect);
	if (AutoDetect == 0)
	{
		UE_LOG(LogAsdaVideo, Log, TEXT("Автопробник железа выключен (asda.Video.AutoDetect=0, умолчание) — первый запуск в пресете по умолчанию %s"),
			PresetName(kDefaultPreset));
		return false;
	}
	return DoHardwareBenchmark();
}

bool UAsdaVideoSettingsSubsystem::DoHardwareBenchmark()
{
	UGameUserSettings* S = GetUserSettings();
	if (!S) { return false; }
	// Пробник движка: SynthBenchmark (CPU + GPU, WorkScale 10 ≈ 1–3 с) → PerfIndex → уровни по PerfIndexThresholds_* (BaseScalability.ini
	//   [ScalabilitySettings], 3 порога ⇒ максимум уровень 3 = Epic, Cinematic пробник не выдаёт) + ResolutionQuality по PerfIndexValues.
	S->RunHardwareBenchmark(/*WorkScale*/10);
	const float Cpu = S->GetLastCPUBenchmarkResult();
	const float Gpu = S->GetLastGPUBenchmarkResult();
	if (!(Cpu > 0.f) || !(Gpu > 0.f))
	{
		UE_LOG(LogAsdaVideo, Warning, TEXT("Автопробник железа не дал индексов (CPU=%.1f GPU=%.1f) — первый запуск в пресете по умолчанию %s"),
			Cpu, Gpu, PresetName(kDefaultPreset));
		return false;
	}
	// Ниже 0 движок не опустит, выше 3 не поднимет; уровни применяются и сохраняются штатно ([ScalabilityGroups] GameUserSettings.ini).
	S->ApplyHardwareBenchmarkResults();
	UE_LOG(LogAsdaVideo, Log, TEXT("Автопробник железа: CPU PerfIndex=%.1f GPU PerfIndex=%.1f → пресет %s%s; %s"), Cpu, Gpu,
		PresetName(GetQualityPreset()), IsQualityMixed() ? TEXT(" (группы вразнобой)") : TEXT(""), *DescribeQualityLevels());
	return true;
}

void UAsdaVideoSettingsSubsystem::ApplyDefaultQuality(bool bAllowBenchmark)
{
	// ★★★ 12.10.2026 (решение владельца): первый запуск — пресет по умолчанию `kDefaultPreset` = High («Высокое» в окне настроек),
	//   БЕЗ пробника железа (ключ asda.Video.AutoDetect по умолчанию 0; 1 — пробник, как было с E-0b 06.09.2026, когда без него
	//   ставился Epic). До E-0b дефолт был sg 4 = Cinematic («красивый UE5-вид» 02.08) — на iGPU слайд-шоу с первого экрана.
	// ★ В-1 (review_setE0.md): при СБРОСЕ вкладки «Качество» bAllowBenchmark=false — сброс НЕ гоняет 4.7-секундный SynthBenchmark
	//   в игре (риск Р2 на гибридной графике при каждом сбросе). Пробник — только первый запуск с ключом 1 и кнопка «Определить автоматически».
	if (!(bAllowBenchmark && RunAutoDetect()))
	{
		SetQualityPreset(kDefaultPreset);
	}
	// ★★★ ПОДБОР ВЛАДЕЛЬЦА 09.09.2026: поверх ровного результата — точка отсчёта отдельных групп (GI 2, тени 4)
	//   из [AsdaDefaultQuality]. Именно ПОСЛЕ, а не вместо: слабую машину пробник уже опустил, и подбор её не поднимет.
	//   ★ 12.10.2026: при пресете по умолчанию High (2) правило Min() обе группы оставляет на 2 (подбор вступает от уровня 3).
	ApplyOwnerQualityBaseline();
	SetGlowLevel(4);
	SetGamma(50);
}

// ★★★ ПОДБОР ВЛАДЕЛЬЦА 09.09.2026 — стартовые группы качества. Подробности правила и причина выбора файла —
//   в шапке [AsdaDefaultQuality] Config/DefaultScalability.ini и в объявлении метода.
void UAsdaVideoSettingsSubsystem::ApplyOwnerQualityBaseline()
{
	UGameUserSettings* S = GetUserSettings();
	if (!S || !GConfig) { return; }

	// Группы, у которых у владельца свой уровень. Поле — в живом наборе уровней (Scalability), сеттер — в
	//   UGameUserSettings (профиль игрока): менять надо ОБА, иначе рассинхрон «в CVar одно, в профиле другое».
	struct FBaselineGroup
	{
		const TCHAR* Key;                                    // имя ключа в [AsdaDefaultQuality] = имя sg-группы
		int32 Scalability::FQualityLevels::*Field;
		void (UGameUserSettings::*Set)(int32);
	};
	static const FBaselineGroup Groups[] = {
		{ TEXT("sg.GlobalIlluminationQuality"), &Scalability::FQualityLevels::GlobalIlluminationQuality, &UGameUserSettings::SetGlobalIlluminationQuality },
		{ TEXT("sg.ShadowQuality"),             &Scalability::FQualityLevels::ShadowQuality,             &UGameUserSettings::SetShadowQuality             },
	};

	Scalability::FQualityLevels Levels = Scalability::GetQualityLevels();
	bool bChanged = false;
	for (const FBaselineGroup& G : Groups)
	{
		int32 Want = 0;
		// Нет строки в ini → группу не трогаем вовсе (число в C++ не дублируем: port-shared-points).
		if (!GConfig->GetInt(TEXT("AsdaDefaultQuality"), G.Key, Want, GScalabilityIni)) { continue; }
		Want = FMath::Clamp(Want, 0, 4);

		const int32 Detected = Levels.*G.Field;
		// Пробник железа выдаёт максимум 3 (BaseScalability.ini [ScalabilitySettings] — три порога PerfIndexThresholds_*),
		//   поэтому «>= 3» и означает «железо нормальное, уровень не понижен». Ниже 3 подбор работает только как потолок.
		const int32 Result = (Detected >= 3) ? Want : FMath::Min(Detected, Want);
		if (Result != Detected)
		{
			Levels.*G.Field = Result;
			(S->*G.Set)(Result);
			bChanged = true;
		}
		UE_LOG(LogAsdaVideo, Log, TEXT("Подбор владельца [AsdaDefaultQuality] %s: пробник дал %d, подбор %d → %d%s"),
			G.Key, Detected, Want, Result,
			(Detected >= 3) ? TEXT("") : TEXT(" (железо слабее нормального — подбор только как потолок)"));
	}
	if (bChanged)
	{
		// ★ ПОЧЕМУ НЕ SaveQuality(): ApplyDefaultQuality идёт из Initialize подсистемы, а UGameUserSettings::
		//   ApplyNonResolutionSettings применяет уровни ТОЛЬКО «if (GEngine->IsInitialized())» (GameUserSettings.cpp:541-547) —
		//   на этом этапе движок ещё не инициализирован, и вызов молча ничего не делал (проверено прогоном: в логе стояло
		//   «подбор 4 → 4», а sg оставался 3). Поэтому тем же путём, что и штатный ApplyHardwareBenchmarkResults
		//   (GameUserSettings.cpp:1132-1143): применить уровни напрямую и сохранить состояние.
		Scalability::SetQualityLevels(Levels);
		Scalability::SaveState(GGameUserSettingsIni);   // [ScalabilityGroups] профиля игрока
		if (UGameUserSettings* SS = GetUserSettings()) { SS->SaveSettings(); }
		UE_LOG(LogAsdaVideo, Log, TEXT("Стартовые группы после подбора владельца; %s"), *DescribeQualityLevels());
	}
}

void UAsdaVideoSettingsSubsystem::RunHardwareBenchmarkNow()
{
	// ★ В-1: явный замер по кнопке «Определить автоматически» — игнорирует флаг AutoDetect (владелец нажал сам).
	if (!DoHardwareBenchmark())
	{
		UE_LOG(LogAsdaVideo, Warning, TEXT("«Определить автоматически»: пробник не дал индексов — уровни качества не изменены"));
	}
}

// ★ E-0b: консольный выбор пресета — единственный путь к Cinematic (4), в меню Ctrl+N его нет.
//   Регистрация единственная в Source/ (grep "asda.Video.Preset" 06.09.2026 = 0; закон ue-console-duplicate-registration).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommand CmdAsdaVideoPreset(
	TEXT("asda.Video.Preset"),
	TEXT("asda.Video.Preset <0..4|low|medium|high|epic|cine> — пресет качества (все sg.* разом) с сохранением в GameUserSettings.ini; без аргумента — печать текущего"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		UAsdaVideoSettingsSubsystem* V = nullptr;
		if (GEngine)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.OwningGameInstance) { V = Ctx.OwningGameInstance->GetSubsystem<UAsdaVideoSettingsSubsystem>(); if (V) { break; } }
			}
		}
		if (!V) { UE_LOG(LogAsdaVideo, Error, TEXT("asda.Video.Preset: подсистема видео не найдена")); return; }
		if (Args.Num() == 0)
		{
			UE_LOG(LogAsdaVideo, Display, TEXT("asda.Video.Preset: %s%s; %s"), UAsdaVideoSettingsSubsystem::PresetName(V->GetQualityPreset()),
				V->IsQualityMixed() ? TEXT(" (группы вразнобой)") : TEXT(""), *UAsdaVideoSettingsSubsystem::DescribeQualityLevels());
			return;
		}
		const FString A = Args[0].ToLower();
		int32 L = -1;
		if (A.IsNumeric())            { L = FCString::Atoi(*A); }
		else if (A == TEXT("low"))    { L = 0; }
		else if (A == TEXT("medium")) { L = 1; }
		else if (A == TEXT("high"))   { L = 2; }
		else if (A == TEXT("epic"))   { L = 3; }
		else if (A == TEXT("cine") || A == TEXT("cinematic") || A == TEXT("ultra")) { L = 4; }
		if (L < 0 || L > 4) { UE_LOG(LogAsdaVideo, Error, TEXT("asda.Video.Preset: неверный аргумент «%s» (0..4|low|medium|high|epic|cine)"), *Args[0]); return; }
		V->SetQualityPreset((EAsdaQualityPreset)L);
	}));
#endif // ASDA_DEV_CONSOLE
