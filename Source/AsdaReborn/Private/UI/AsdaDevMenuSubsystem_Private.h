// Copyright (c) 2026 @sitxovski (Discord)
// AsdaDevMenuSubsystem_Private.h — ★ R3-c 12.11.2026 (разрез AsdaDevMenuSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaDevMenuSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaDevMenuSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaDevMenuSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "World/AsdaFxLightComponent.h"       // ★ 16.10.2026: эффект-свет умножает рассеяние сам — в обходе пропускаем
#include "World/AsdaLocalLightActor.h"        // ★ 16.10.2026: то же для светов карт
#include "World/AsdaRenderSettings.h"         // ★ 16.10.2026: FindPreset — Custom-пресет света карты ведёт обход ApplyFogScale
#include "World/AsdaKeybindConfig.h"
#include "World/AsdaPlayerController.h"
#include "Resources/AsdaVideoSettingsSubsystem.h"
#include "Resources/AsdaMaterialNames.h"   // ★ дедуп №2 В1-C (F10/F16): пути мастеров, имена параметров, экранные ассеты
#include "Text/AsdaLang.h"   // ★ 19.10.2026: подписи меню на языке сессии (FAsdaLang::IsRu)

#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWidget.h"
#include "InputCoreTypes.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Slider.h"           // USlider — ползунки «Дальность тумана»/«Плотность тумана» (asda.Fog.*Scale)

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Containers/Ticker.h"          // FTSTicker — отложенные шаги самопроверки (без мира)
#include "UnrealClient.h"               // FScreenshotRequest — Shot showui из кода
#include "UObject/UObjectIterator.h"
#include "Components/ExponentialHeightFogComponent.h"   // asda.Fog.DensityScale
#include "Kismet/KismetMaterialLibrary.h"              // MPC_AsdaNormals / MPC_AsdaToon в экземпляре мира
#include "Materials/MaterialParameterCollection.h"
#include "Resources/AsdaGameUserSettings.h"   // ★ фаза 2: RHI и профильные ключи — одно место
#include "World/AsdaCullDistanceSubsystem.h"      // самопроверка culldrag: счётчик полных проходов мира и применённый Scale
#include "Resources/AsdaBenchSubsystem.h"            // ★ 12.10.2026: «Определить автоматически» = замер кадра на месте (StartInPlace), как «Автоподбор» окна настроек
#include "GameFramework/PlayerController.h"      // самопроверка clouds: BugItGo через ConsoleCommand контроллера

// Категория журнала частей AsdaDevMenuSubsystem*.cpp: определение — AsdaDevMenuSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaDevMenu, Log, All );

// ───────────────────────────── CVar-утилиты ─────────────────────────────
// Найти консольную переменную по имени (nullptr, если не зарегистрирована — напр. r.* другого RHI).
static IConsoleVariable* FindCVar(const TCHAR* Name)
{
	return IConsoleManager::Get().FindConsoleVariable(Name);
}
// Текущее int-значение CVar; Def — если переменной нет (подписи строк не падают на отсутствующем ключе).
static int32 GetCVarInt(const TCHAR* Name, int32 Def = 0)
{
	IConsoleVariable* CV = FindCVar(Name);
	return CV ? CV->GetInt() : Def;
}
// Текущее float-значение CVar; Def — если переменной нет.
static float GetCVarFloat(const TCHAR* Name, float Def = 0.f)
{
	IConsoleVariable* CV = FindCVar(Name);
	return CV ? CV->GetFloat() : Def;
}
// Имя приоритета, которым сейчас выставлен CVar (ECVF_SetBy*) — для лога сброса/самопроверки.
static const TCHAR* CVarSetByName(const IConsoleVariable* CV)
{
	if (!CV) { return TEXT("-"); }
	switch ((uint32)CV->GetFlags() & ECVF_SetByMask)
	{
	case ECVF_SetByConstructor:       return TEXT("Constructor");
	case ECVF_SetByScalability:       return TEXT("Scalability");
	case ECVF_SetByGameSetting:       return TEXT("GameSetting");
	case ECVF_SetByProjectSetting:    return TEXT("ProjectSetting");
	case ECVF_SetBySystemSettingsIni: return TEXT("SystemSettingsIni");
	case ECVF_SetByDeviceProfile:     return TEXT("DeviceProfile");
	case ECVF_SetByConsoleVariablesIni: return TEXT("ConsoleVariablesIni");
	case ECVF_SetByCommandline:       return TEXT("Commandline");
	case ECVF_SetByCode:              return TEXT("Code");
	case ECVF_SetByConsole:           return TEXT("Console");
	default:                          return TEXT("другой");
	}
}

// ★★★ 12.10.2026 (сокращение меню): список r.*-ключей [AsdaVideo] GLiveCVarKeys УДАЛЁН — он опустел. Все его ключи были
//   строками, ушедшими в уровни или убранными (r.Shadow.Virtual.Enable, r.ContactShadows, r.GTAO.TemporalFilter,
//   r.Shadow.DistanceScale, четыре SMRT, r.Shadow.RadiusThreshold, r.Shadow.Virtual.ResolutionLodBiasDirectional, r.Fog,
//   r.MipMapLODBias); оставшиеся r.*-строки меню (метод AA, резкость, размытие, масштаб рендера) ведёт профиль игрока
//   (UAsdaGameUserSettings, с 19.09.2026). Секция [AsdaVideo] у меню теперь только служебная (DevMenuTab); старые пины
//   в ней снимает SweepRetiredPins при старте.
// Живые asda.*-CVar наших систем, персистуемые в [AsdaRender] (ключ = имя CVar). Регистрации — в своих файлах:
//   AsdaCapsuleShadow.cpp (Capsule*), AsdaRimLightActor.cpp (Rim.*), asda.Fog.* — ниже в этом файле.
//   ★ 12.10.2026: из списка выкинуты ключи убранных строк — asda.Clouds.Shadows, asda.Light.ShadowBudget,
//   asda.Light.FxShadowBudget, asda.Vis.Enable, asda.Clouds.UpdateEvery, asda.Cull.Enable, asda.Cull.FogFloor, asda.Cull.VisFloor
//   (ключи консоли живы, регистрации не тронуты). Каждый ключ списка ОБЯЗАН иметь строку-ручку (GPinRows; режим pins проверяет).
static const TCHAR* GRenderCVarKeys[] = {
	TEXT("asda.Shadow.Capsule"),              // непрямая капсульная тень тела 0/1 («Персонажи»)
	TEXT("asda.Shadow.CapsuleMinVisibility"), // сила пятна 0.1 / 0.25 / 0.5 («Персонажи»)
	TEXT("asda.Fog.DensityScale"),            // множитель плотности тумана карты (ползунок 0.25…2.0)
	TEXT("asda.Fog.DistanceScale"),           // множитель дальности начала тумана карты (ползунок 0.5…2.0)
	// ★ фаза 2: asda.Clouds.Field («Объёмные облака») и asda.Cull.Scale («Дальность прорисовки») — поля профиля.
	TEXT("asda.Rim.Enable"),                  // рим персонажей: 1 по карте / 0 выкл
	TEXT("asda.Rim.Scale"),                   // множитель силы рима 0.5 / 1 / 1.5
	TEXT("asda.Rim.OriginalOff"),             // ★ 09.11: 1 = рима нет на всех картах (оригинал, умолч.) / 0 = по актору карты
};

// ★ 12.10.2026: служебные НЕ-CVar ключи ini убранных строк — их тоже снимает чистка при старте (SweepRetiredPins):
//   [AsdaRender] LightMaxDrawMul — множитель дальности локальных светов (строка «Дальность локальных светов ×» ушла в уровень).
static const TCHAR* GRetiredServiceIniKeys[] = {
	TEXT("LightMaxDrawMul"),
};

// ★ CVar, которыми УПРАВЛЯЕТ sg-пресет (Engine/Config/BaseScalability.ini 5.8 + наш Config/DefaultScalability.ini, сет E-0b 06.09.2026):
//   r.MaxAnisotropy (TextureQuality@0..Cine = 1/2/4/8/8), r.BloomQuality (PostProcessQuality = 4/4/5/5/5),
//   r.LensFlareQuality (PostProcessQuality = 0/0/2/2/3), r.AmbientOcclusionLevels (PostProcessQuality = 0/-1/-1/-1/-1),
//   r.ScreenPercentage (ResolutionQuality ← sg.ResolutionQuality), r.Shadow.DistanceScale (ShadowQuality: DS 0.6/0.7/0.7/0.7),
//   ★ E-0b: r.Shadow.Virtual.Enable (DS 0/0/1/1), r.ContactShadows (DS 0/1/1/1), r.Shadow.CSM.MaxCascades (DS 1/1/4/4),
//   r.ShadowQuality, r.CapsuleShadows, r.LightFunctionQuality, r.VolumetricFog, r.LightMaxDrawDistanceScale (BS ShadowQuality@N),
//   r.DistanceFieldAO, r.SkylightIntensityMultiplier, r.Lumen.DiffuseIndirect.Allow (GlobalIlluminationQuality@N), r.SSR.Quality,
//   r.Lumen.Reflections.Allow (ReflectionQuality@N). r.GTAO.TemporalFilter/SpatialFilter НЕ пер-уровневые — остались в DefaultEngine.ini.
//   r.MipMapLODBias в BaseScalability НЕТ (там r.Streaming.MipBias) — ревизия В-5 причислила его к пресетным ошибочно; его
//   сброс = дефолт кода CVar (0). Если ключ из списка ЕСТЬ в проектном ini ([RendererSettings]/[SystemSettings]), проектный
//   приоритет (ECVF_SetByProjectSetting 0x04 / SystemSettingsIni 0x05) СИЛЬНЕЕ Scalability (0x02): пресет его не меняет, и сброс
//   возвращает значение ini (IsPresetManaged → false, решается по данным, не списком). До E-0b так были пришпилены
//   r.AmbientOcclusionLevels=2, r.Shadow.DistanceScale=0.7, r.ScreenPercentage=100, r.Shadow.Virtual.Enable=1, r.ContactShadows=1,
//   r.Shadow.CSM.MaxCascades=4 — «Low не был Low» (setE0_report.md); E-0b перенёс их в DefaultScalability.ini.
//   Для пресетных сброс = снять оверрайд [AsdaVideo] + UAsdaVideoSettingsSubsystem::ReapplyQuality().
//   ★ 12.10.2026 (сокращение меню): список ОСТАВЛЕН ПОЛНЫМ, включая ключи убранных строк. Он не накатывается при старте и
//   пинов не создаёт — это перечень «чем управляет уровень» для IsPresetManaged и для печати режима verify самопроверки
//   (приёмка пресетов 03_tester.md §1.3: SetBy Scalability у всех ключей, любой Console — необъяснённый пин).
static const TCHAR* GPresetManagedCVarKeys[] = {
	TEXT("r.MaxAnisotropy"),
	TEXT("r.BloomQuality"),
	TEXT("r.LensFlareQuality"),
	TEXT("r.AmbientOcclusionLevels"),
	TEXT("r.ScreenPercentage"),
	TEXT("r.Shadow.DistanceScale"),
	// ★ E-0b (06.09.2026): пер-уровневые ключи матрицы уровней (setE_plan_addendum §1) — управляются DefaultScalability/BaseScalability
	TEXT("r.Shadow.Virtual.Enable"),
	TEXT("r.ContactShadows"),
	TEXT("r.Shadow.CSM.MaxCascades"),
	TEXT("r.Shadow.CSM.TransitionScale"),
	TEXT("r.ShadowQuality"),
	TEXT("r.CapsuleShadows"),
	TEXT("r.LightFunctionQuality"),
	TEXT("r.VolumetricFog"),
	TEXT("r.LightMaxDrawDistanceScale"),
	TEXT("r.DistanceFieldAO"),
	TEXT("r.SkylightIntensityMultiplier"),
	TEXT("r.Lumen.DiffuseIndirect.Allow"),
	TEXT("r.Lumen.Reflections.Allow"),
	TEXT("r.SSR.Quality"),
	// ★ E-3 (07.09.2026, Lumen): ключи, которые пресет трогает через DefaultScalability [GlobalIlluminationQuality@N] / BS [ReflectionQuality@N]
	TEXT("r.Lumen.TraceMeshSDFs"),
	TEXT("r.Lumen.FinalGatherMethod"),
	TEXT("r.Lumen.ScreenProbeGather.DownsampleFactor"),
	TEXT("r.Lumen.Reflections.DownsampleFactor"),
	TEXT("r.AOQuality"),
	// ★★★ ВОЛНА ТЕНЕЙ (08.09.2026): шесть ключей теней, которые раздаёт уровень — BS [ShadowQuality@N] плюс наши дельты
	//   в Config/DefaultScalability.ini (@2 SMRT 4×2, @3 LodBias 0 + отсев 0.03, @Cine выборок 4 + отсев 0.03).
	//   В DefaultEngine.ini их НЕТ, значит приоритет Scalability не перебит и сброс строки меню вернёт значение уровня.
	TEXT("r.Shadow.Virtual.SMRT.RayCountDirectional"),
	TEXT("r.Shadow.Virtual.SMRT.SamplesPerRayDirectional"),
	TEXT("r.Shadow.Virtual.SMRT.RayCountLocal"),
	TEXT("r.Shadow.Virtual.SMRT.SamplesPerRayLocal"),
	TEXT("r.Shadow.RadiusThreshold"),
	TEXT("r.Shadow.Virtual.ResolutionLodBiasDirectional"),
	// ★ 12.10.2026 (пересмотр пресетов, ревизия МЕЛК): новые пер-уровневые ключи DefaultScalability.ini — печатаются режимом verify
	//   (r.Shadow.DistanceScale уже есть выше).
	TEXT("r.Shadow.Virtual.NonNanite.IncludeInCoarsePages"),
	TEXT("r.GTAO.TemporalFilter"),
	TEXT("r.FXAA.Quality"),
	TEXT("r.Streaming.MipBias"),
	TEXT("r.TSR.History.ScreenPercentage"),
};

// Параметры MPC_AsdaNormals (вкладка «Рельеф»; персист [AsdaNormals], ключ = имя параметра).
//   ★ 12.10.2026: строка меню осталась только у NormalStrength (kNormalStrengthIdx); остальные параметры ставит уровень
//   [AsdaQuality@N] (Enable, RoughFromMap) или ассет (AoAmount/Delight/EmissiveGain, «0 = рекомендуемое»). Массив нужен
//   целиком: ApplyNormals накатывает уровень на все шесть, а чистка SweepRetiredNormals снимает старые ключи пяти убранных.
static const FName GNormalParams[] = {
	FName(TEXT("Enable")),          // 0/1 — весь рельеф (lerp-гейт: 0 = буквально прежний вид) — уровень
	FName(TEXT("NormalStrength")),  // 0.5 / 1 / 1.5 — сила нормали (поверх ClassStrength MIC) — СТРОКА МЕНЮ
	FName(TEXT("RoughFromMap")),    // 0/1 — шероховатость из карты _RE.R — уровень
	FName(TEXT("AoAmount")),        // 0 / 0.5 / 1 — AO из карты _RE.B — ассет
	FName(TEXT("Delight")),         // 0 / 0.5 / 1 — делайтинг альбедо (0 = рекомендуемое) — ассет
	FName(TEXT("EmissiveGain")),    // 0 / 0.5 / 1 — свечение из карты _RE.G (0 = рекомендуемое) — ассет
};
// Индекс единственного параметра MPC_AsdaNormals со строкой меню (NormalStrength).
static constexpr int32 kNormalStrengthIdx = 1;

// Подписи вкладок (индекс = EAsdaDevTab): кнопки-табы панели и инвентарь режима rows самопроверки.
static const TCHAR* GDevTabNames[(int32)EAsdaDevTab::Count] = {
	TEXT("Экран"), TEXT("Качество"), TEXT("Небо и туман"), TEXT("Рельеф"), TEXT("Персонажи"), TEXT("Сброс")
};

// ★ 19.10.2026 (поручение владельца «меню настроек — на английский»): подпись меню на ЯЗЫКЕ СЕССИИ — в английском клиенте
//   английская, в русском — прежняя русская. Меню собрано кодом, loc-строк GameText у него нет, поэтому пара RU/EN стоит
//   прямо у места показа (язык сессии до перезапуска не меняется — см. AsdaLang.h). Самотесты ищут строки по РУССКИМ
//   подписям (GPinRows, режимы rows/toggle/reset) — их гонять в RU-сессии (-AsdaLang=ru); режим shots годится для обоих языков.
static const TCHAR* DmT(const TCHAR* Ru, const TCHAR* En) { return FAsdaLang::IsRu() ? Ru : En; }


// GAsdaFogDensityScale: определение — в домашней части (★ R3-c 12.11.2026).
extern float GAsdaFogDensityScale;
static void OnAsdaFogChanged(IConsoleVariable*);

// GAsdaFogDistanceScale: определение — в домашней части (★ R3-c 12.11.2026).
extern float GAsdaFogDistanceScale;

// GAsdaFogVolumetric: определение — в домашней части (★ R3-c 12.11.2026).
extern int32 GAsdaFogVolumetric;

// GFogScaledWorlds: определение — в домашней части (★ R3-c 12.11.2026).
extern TArray<TWeakObjectPtr<UWorld>> GFogScaledWorlds;

// Найти подсистему dev-меню по живым мирам (для CVar-колбэков и консольной самопроверки — у них нет мира).
static UAsdaDevMenuSubsystem* FindDevMenuSubsystem()
{
	if (!GEngine) { return nullptr; }
	for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
	{
		if (Ctx.OwningGameInstance)
		{
			if (UAsdaDevMenuSubsystem* S = Ctx.OwningGameInstance->GetSubsystem<UAsdaDevMenuSubsystem>()) { return S; }
		}
	}
	return nullptr;
}

// Общий колбэк обоих ключей тумана (asda.Fog.DensityScale / asda.Fog.DistanceScale): пересчитать все игровые миры
// и обновить строки открытой панели (подпись + ручка ползунка), см. OnFogKeyChanged.
static void OnAsdaFogChanged(IConsoleVariable*)
{
	if (UAsdaDevMenuSubsystem* S = FindDevMenuSubsystem()) { S->OnFogKeyChanged(); }
}

// Отложенный шаг без мира (FTSTicker); определение — у самопроверки ниже.
static void AsdaDevMenuAfter(float Sec, TFunction<void()> Fn);

// ★★★ 12.10.2026 — ЧИСТКА УСТАРЕВШИХ ПИНОВ (грабля 02_engineer.md §2.5/§3.2: строка из меню убрана, а её ключ в
//   [AsdaVideo]/[AsdaRender] старого профиля накатывался бы при каждом старте с ECVF_SetByConsole — выше любого уровня, и
//   игрок его уже не видит и не снимет). Сканирует секцию GameUserSettings.ini: ключ УСТАРЕЛ, если
//     • это зарегистрированная консольная переменная (служебные DevMenuTab/bInitialized/bBenchDone — не CVar, не трогаются),
//     • её нет среди живых ключей меню (GRenderCVarKeys — единственный накатываемый список),
//     • она не принадлежит профилю (UAsdaGameUserSettings::IsProfileOwnedCVar — такие ключи ведёт сам профиль, их миграция v2 уже перенесла),
//   либо это служебный не-CVar ключ убранной строки (GRetiredServiceIniKeys). bRemove=false — только посчитать (режим verify).
//   Каждый снятый пин пишется в лог строкой «[меню] снят устаревший пин X=Y» — по ней значение можно вернуть консолью.
//   Возвращает число устаревших пинов.
static int32 SweepRetiredPins(const TCHAR* Section, bool bRemove)
{
	if (!GConfig) { return 0; }
	TArray<FString> Lines;
	if (!GConfig->GetSection(Section, Lines, GGameUserSettingsIni)) { return 0; }
	int32 N = 0;   // найдено устаревших
	for (const FString& Line : Lines)
	{
		FString Key, Val;
		if (!Line.Split(TEXT("="), &Key, &Val)) { continue; }
		Key.TrimStartAndEndInline(); Val.TrimStartAndEndInline();
		bool bLive = false;   // ключ по-прежнему пишет строка меню
		for (const TCHAR* K : GRenderCVarKeys) { if (Key.Equals(K, ESearchCase::IgnoreCase)) { bLive = true; break; } }
		if (bLive) { continue; }
		bool bRetiredService = false;   // служебный ключ убранной строки
		for (const TCHAR* K : GRetiredServiceIniKeys) { if (Key.Equals(K, ESearchCase::IgnoreCase)) { bRetiredService = true; break; } }
		if (!bRetiredService)
		{
			if (!FindCVar(*Key)) { continue; }                                  // не CVar — служебный ключ (DevMenuTab, bInitialized, bBenchDone…)
			if (UAsdaGameUserSettings::IsProfileOwnedCVar(Key)) { continue; }   // ведёт профиль игрока
			if (Key.StartsWith(TEXT("asda.Video."))) { continue; }              // ключи видео-подсистемы в её же секции (asda.Video.AutoDetect — ResolveAutoDetectFlag), не пины меню
		}
		++N;
		if (bRemove)
		{
			GConfig->RemoveKey(Section, *Key, GGameUserSettingsIni);
			UE_LOG(LogAsdaDevMenu, Log, TEXT("[меню] снят устаревший пин %s=%s (секция [%s]; строки в меню больше нет — значение задаёт уровень; вернуть можно консолью)"), *Key, *Val, Section);
		}
		else
		{
			UE_LOG(LogAsdaDevMenu, Warning, TEXT("[меню] устаревший пин %s=%s в [%s] (строки в меню нет; чистка при старте его снимет)"), *Key, *Val, Section);
		}
	}
	return N;
}

// ★ 12.10.2026: то же для [AsdaNormals] — остаётся только ключ параметра со строкой меню (NormalStrength); ключи пяти убранных
//   строк рельефа (Enable, RoughFromMap, AoAmount, Delight, EmissiveGain) иначе перекрывали бы уровень [AsdaQuality@N] невидимо.
static int32 SweepRetiredNormals(bool bRemove)
{
	if (!GConfig) { return 0; }
	TArray<FString> Lines;
	if (!GConfig->GetSection(TEXT("AsdaNormals"), Lines, GGameUserSettingsIni)) { return 0; }
	int32 N = 0;   // найдено устаревших
	for (const FString& Line : Lines)
	{
		FString Key, Val;
		if (!Line.Split(TEXT("="), &Key, &Val)) { continue; }
		Key.TrimStartAndEndInline(); Val.TrimStartAndEndInline();
		if (Key.Equals(GNormalParams[kNormalStrengthIdx].ToString(), ESearchCase::IgnoreCase)) { continue; }
		++N;
		if (bRemove)
		{
			GConfig->RemoveKey(TEXT("AsdaNormals"), *Key, GGameUserSettingsIni);
			UE_LOG(LogAsdaDevMenu, Log, TEXT("[меню] снят устаревший пин %s=%s (секция [AsdaNormals]; параметр рельефа задаёт уровень/ассет)"), *Key, *Val);
		}
		else
		{
			UE_LOG(LogAsdaDevMenu, Warning, TEXT("[меню] устаревший пин %s=%s в [AsdaNormals] (строки в меню нет)"), *Key, *Val);
		}
	}
	return N;
}

// Отложенный шаг без мира (FTSTicker). Объявлен выше (нужен ScheduleSliderPersist).
static void AsdaDevMenuAfter(float Sec, TFunction<void()> Fn)
{
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Fn](float) { Fn(); return false; }), Sec);
}
