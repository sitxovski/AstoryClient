// Copyright (c) 2026 @sitxovski (Discord)
// AsdaDevMenuSubsystem_Persist.cpp — ★ R3-c 12.11.2026 (разрез AsdaDevMenuSubsystem.cpp, дословный перенос): сохранение ключей/гаммы, туман, ползунки, нормали, RHI, сброс к умолчаниям.
//   Общее для частей — в AsdaDevMenuSubsystem_Private.h; шапка и реверс — в AsdaDevMenuSubsystem.cpp.
#include "UI/AsdaDevMenuSubsystem.h"
#include "World/AsdaWorldKind.h"   // ★ 31.10.2026: AsdaWorldKind::IsLoginScene — туман логина авторский (asda.Login.FogAsAuthored)
#include "Components/LocalLightComponent.h"   // ★ 16.10.2026: рассеяние светов уровня под объёмный туман (ApplyFogScale)
#include "World/AsdaRenderQualitySubsystemBase.h"   // ★ волна 3, хвосты (15.09.2026): читатель уровневых ключей Scalability-ini



#include "DynamicRHI.h"   // GDynamicRHI->GetName() — активный RHI (D3D12/D3D11)
#include "AsdaDevMenuSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaDevMenuSubsystem*.cpp

// ★ E-0b (06.09.2026): значение ключа в секции уровня `<Группа>@<N>` объединённого Scalability-ini (BaseScalability + наш
//   DefaultScalability; уровень 4 = секция @Cine — Scalability.cpp:340-358). Единственный источник «что даёт пресет» для меню:
//   базу дальности теней ×0.5/1/1.5 и подписи «[пресет]» считаем отсюда, а не из зашитых чисел (port-shared-points: один источник).
//   Def — если ключа в секции нет (напр. r.ContactShadows в BaseScalability отсутствует, живёт только в DS).
//   ★ E-4 (07.09.2026): тело было вынесено в публичный статик меню — тем же путём читала подсистема бюджета теней.
//   ★ Волна 3, хвосты (15.09.2026, аудит 04 (д)3): общий читатель конфигурации больше НЕ живёт в UI-подсистеме меню —
//   тело дословно перенесено в UAsdaRenderQualitySubsystemBase::ScalabilityLevelValue (база подсистем качества рендера);
//   здесь — короткий алиас для строк меню (те же ключи, умолчания и порядок вызовов).
static FString ScalabilityLevelValue(const TCHAR* Group, int32 Level, const TCHAR* Key, const TCHAR* Def)
{
	return UAsdaRenderQualitySubsystemBase::ScalabilityLevelValue(Group, Level, Key, Def);
}

void UAsdaDevMenuSubsystem::ApplyCVarPersist(const FString& Name, const FString& Value)
{
	// ★★★ 19.09.2026 (фаза 2): ключи, ставшие полями ПРОФИЛЯ игрока, пишутся ТЕМИ ЖЕ функциями, что и в
	//   игровом окне настроек (один источник): метод AA, резкость, размытие, облака, дальность прорисовки,
	//   масштаб рендера (штатное поле). ★ 12.10.2026: ветка «Bloom» (→ пункт свечения) убрана вместе со строкой.
	if (UAsdaVideoSettingsSubsystem* Video = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr)
	{
		if (Video->SetRenderOverride(Name, Value, /*bSave*/ true))
		{
			UE_LOG(LogAsdaDevMenu, Log, TEXT("Настройка: %s=%s → профиль игрока (общий путь с окном настроек)"), *Name, *Value);
			return;
		}
		if (Name.Equals(TEXT("r.ScreenPercentage"), ESearchCase::IgnoreCase))
		{
			Video->SetRenderScalePercent(FCString::Atoi(*Value), /*bSave*/ true);
			return;
		}
	}
	if (IConsoleVariable* CV = FindCVar(*Name))
	{
		CV->Set(*Value, ECVF_SetByConsole);
	}
	// asda.* — наши системы ([AsdaRender]); всё остальное (r.*, t.*) — [AsdaVideo].
	const TCHAR* Section = Name.StartsWith(TEXT("asda.")) ? TEXT("AsdaRender") : TEXT("AsdaVideo");
	if (GConfig)
	{
		GConfig->SetString(Section, *Name, *Value, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	UE_LOG(LogAsdaDevMenu, Log, TEXT("Настройка: [%s] %s=%s"), Section, *Name, *Value);
}

void UAsdaDevMenuSubsystem::ApplyGammaPersist(float Gamma)
{
	// ★ 19.09.2026 (фаза 2): гамма dev-окна = та же «Яркость», что полоса 70058 окна настроек: переводим
	//   DisplayGamma в шкалу профиля 1…100 обратной формулой `UAsdaGameUserSettings::ApplyGammaSetting`
	//   (DisplayGamma = Lerp(3.0, 1.4, Яркость / 100); 2.2 → 50) и пишем ТЕМ ЖЕ `SetGamma`.
	const int32 Level = FMath::Clamp(FMath::RoundToInt((3.0f - Gamma) / 1.6f * 100.f), 1, 100);
	if (UAsdaVideoSettingsSubsystem* Video = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr)
	{
		Video->SetGamma(Level);
		Video->SaveProfile();
	}
	else if (GEngine)
	{
		GEngine->DisplayGamma = Gamma;
	}
	UE_LOG(LogAsdaDevMenu, Log, TEXT("Настройка: яркость %d (гамма %.2f) → профиль игрока"), Level, Gamma);
}

// ─────────────────────────── Туман: asda.Fog.DensityScale + asda.Fog.DistanceScale (общий накат обоих множителей) ───────────────────────────
// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (владелец: «сцена логина в игре темнее, а Holy Light пересвечен — сделай как в редакторе»):
//   туман сцены логина в игре — авторский, как в редакторе. Разрежение тумана карт (asda.Fog.DensityScale/DistanceScale,
//   подбор 09.09 для дальнего тумана карт мира) на логине делало дымку в 7.4 раза жиже, чем владелец ставит в редакторе:
//   пропадало рассеяние неба и солнца в объёмном тумане — сцена темнела (замер 31.10: яркость сцены ×0.67), а компенсация
//   лучей (asda.Fog.VolumetricLightComp) при меньшем поглощении пересвечивала луч. На логине множители не применяются,
//   компенсация лучей — тоже (мир не попадает в GFogScaledWorlds, рассеяние светов = авторское).
//   Ключ: `asda.Login.FogAsAuthored` (1 — туман логина авторский; 0 — разрежение как на картах, как раньше).
static int32 GAsdaLoginFogAsAuthored = 1;
static ASDA_DEV_CVAR_REF(CVarAsdaLoginFogAsAuthored,
	TEXT("asda.Login.FogAsAuthored"),
	GAsdaLoginFogAsAuthored,
	TEXT("Туман сцены логина: 1 = авторский, как в редакторе (множители asda.Fog.* и компенсация лучей на логине не действуют; по умолчанию, 31.10.2026); 0 = разрежение как на картах мира, как раньше."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaFogChanged),
	ECVF_Default);

void UAsdaDevMenuSubsystem::ApplyFogScale(UWorld* World)
{
	if (!World) { return; }
	// ★ 31.10.2026: сцена логина — туман авторский (множители 1), лучи без компенсации (см. asda.Login.FogAsAuthored)
	const bool bLoginAuthored = GAsdaLoginFogAsAuthored != 0 && AsdaWorldKind::IsLoginScene(World);
	const float DensityScale = bLoginAuthored ? 1.f : GAsdaFogDensityScale;
	const float DistScale = bLoginAuthored ? 1.f : FMath::Max(GAsdaFogDistanceScale, 0.01f);   // защита от деления на 0 при плотности
	int32 Count = 0;   // сколько компонентов пересчитано (в лог)
	for (TObjectIterator<UExponentialHeightFogComponent> It; It; ++It)
	{
		UExponentialHeightFogComponent* C = *It;
		if (!C || C->IsTemplate() || C->GetWorld() != World) { continue; }
		// Авторские значения — при первом касании компонента (до любого множителя), как для плотности, так и для дальности.
		float* BaseD = FogBaseDensity.Find(C);
		if (!BaseD) { BaseD = &FogBaseDensity.Add(C, C->FogDensity); }
		float* BaseS = FogBaseStartDistance.Find(C);
		if (!BaseS) { BaseS = &FogBaseStartDistance.Add(C, C->StartDistance); }
		// Дальность: туман начинается на авторском расстоянии × DistanceScale.
		C->SetStartDistance(*BaseS * DistScale);
		// Плотность: авторская × DensityScale / DistanceScale — дальний туман не сгущается на прежней насыщенности.
		C->SetFogDensity(*BaseD * DensityScale / DistScale);
		// ★ 16.10.2026: объёмный проход (лучи локальных светов) — asda.Fog.Volumetric 1 включает, 0 (умолч.) — авторский флаг актора
		bool* BaseV = FogBaseVolumetric.Find(C);
		if (!BaseV) { BaseV = &FogBaseVolumetric.Add(C, C->bEnableVolumetricFog != 0); }
		const bool bWantVF = GAsdaFogVolumetric != 0 ? true : *BaseV;
		if ((C->bEnableVolumetricFog != 0) != bWantVF) { C->SetVolumetricFog(bWantVF); }
		// Пока ручку тянут (bFogSliderLive) — каждый тик даёт накат; строки уходят в Verbose, итог печатается один раз по отпусканию (М-2).
		UE_CLOG(!bFogSliderLive, LogAsdaDevMenu, Log, TEXT("[AsdaRender] туман %s: плотн %.5f→%.5f (×%.2f/%.2f), дальн %.1f→%.1f (×%.2f)"),
			*C->GetPathName(), *BaseD, C->FogDensity, DensityScale, DistScale, *BaseS, C->StartDistance, DistScale);
		UE_CLOG(bFogSliderLive, LogAsdaDevMenu, Verbose, TEXT("[AsdaRender] туман %s: плотн %.5f→%.5f (×%.2f/%.2f), дальн %.1f→%.1f (×%.2f)"),
			*C->GetPathName(), *BaseD, C->FogDensity, DensityScale, DistScale, *BaseS, C->StartDistance, DistScale);
		++Count;
	}
	// ★ 16.10.2026: мир прошёл разрежение тумана — с этого момента FogVolumetricLightScale(World) отдаёт компенсацию (ревизия: в PIE
	//   первая карта ApplyFogScale не получает — туман там авторский, и множитель лучей там давал бы ×7.4 сверх редактора).
	GFogScaledWorlds.RemoveAll([](const TWeakObjectPtr<UWorld>& W) { return !W.IsValid(); });
	if (bLoginAuthored) { GFogScaledWorlds.Remove(World); }   // ★ 31.10.2026: логин — туман авторский, компенсации лучей нет
	else { GFogScaledWorlds.AddUnique(World); }
	// ★ 16.10.2026: рассеяние локальных светов уровня (HolyLight_SpotLight, SpotLight владельца и любые поставленные в карту) —
	//   авторское × FogVolumetricLightScale(). Эффект-свет (и ВСЕ его дочерние светы форм — они обычные Point/Rect/Spot на акторе
	//   эффекта) и AAsdaLocalLightActor с пресетом ставят рассеяние сами (каждый кадр / в ApplyPreset) уже с множителем — здесь их
	//   пропускаем, иначе множитель лёг бы дважды (ревизия: ×K² у дочерних светов). Custom-пресет AAsdaLocalLightActor рассеяние
	//   не пишет — его ведёт этот обход. Static/Stationary пропускаются: на них смена рассеяния не действует.
	const float VolK = FogVolumetricLightScale(World);
	int32 NLights = 0;
	for (TObjectIterator<ULocalLightComponent> It; It; ++It)
	{
		ULocalLightComponent* L = *It;
		if (!L || L->IsTemplate() || L->GetWorld() != World || L->Mobility != EComponentMobility::Movable) { continue; }
		const AActor* Owner = L->GetOwner();
		if (L->IsA<UAsdaFxLightComponent>() || (Owner && Owner->FindComponentByClass<UAsdaFxLightComponent>())) { continue; }
		const AAsdaLocalLightActor* LA = Cast<AAsdaLocalLightActor>(Owner);
		if (LA && UAsdaRenderSettings::Get()->FindPreset(LA->Preset)) { continue; }
		float* BaseVS = LightBaseVolScatter.Find(L);
		if (!BaseVS) { BaseVS = &LightBaseVolScatter.Add(L, L->VolumetricScatteringIntensity); }
		const float Want = *BaseVS * VolK;
		if (!FMath::IsNearlyEqual(L->VolumetricScatteringIntensity, Want)) { L->SetVolumetricScatteringIntensity(Want); }
		UE_CLOG(!bFogSliderLive && *BaseVS > 0.f, LogAsdaDevMenu, Log, TEXT("[AsdaRender] рассеяние света %s: %.2f → %.2f (×%.3f)"), *L->GetPathName(), *BaseVS, Want, VolK);
		++NLights;
	}
	// ★ 16.10.2026: светы карт с пресетом берут рассеяние в ApplyPreset × FogVolumetricLightScale(World) — переприменить в ЭТОМ мире
	//   (их BeginPlay мог пройти до разрежения тумана). Пока тянут ползунок — не каждый тик: итог по отпусканию (bFogSliderLive=false).
	if (!bFogSliderLive)
	{
		for (TObjectIterator<AAsdaLocalLightActor> It; It; ++It)
		{
			if (IsValid(*It) && It->GetWorld() == World) { It->ApplyPreset(); }
		}
	}
	UE_CLOG(!bFogSliderLive, LogAsdaDevMenu, Log, TEXT("[AsdaRender] asda.Fog.DensityScale=%.2f DistanceScale=%.2f — компонентов тумана: %d; объёмный туман %d, лучи светов ×%.3f (светов уровня %d) [мир %s]"),
		DensityScale, DistScale, Count, GAsdaFogVolumetric, VolK, NLights, *World->GetName());
	UE_CLOG(bFogSliderLive, LogAsdaDevMenu, Verbose, TEXT("[AsdaRender] asda.Fog.DensityScale=%.2f DistanceScale=%.2f — компонентов тумана: %d [мир %s]"),
		DensityScale, DistScale, Count, *World->GetName());
}

void UAsdaDevMenuSubsystem::OnFogKeyChanged()
{
	ApplyFogScaleAllWorlds();
	// Внешняя смена ключа (консоль, сброс, ini) при открытой панели: подпись и ручка ползунка следуют за значением (В-1).
	// Из колбэка самого ползунка (bFogSliderLive) не трогаем — ручка уже там, куда её поставил игрок.
	if (MenuWidget && !bFogSliderLive) { RefreshRows(); }
}

void UAsdaDevMenuSubsystem::ScheduleSliderPersist(const FString& Name, float Def)
{
	const uint32 Seq = ++SliderPersistSeq;
	TWeakObjectPtr<UAsdaDevMenuSubsystem> WeakThis(this);
	AsdaDevMenuAfter(0.6f, [WeakThis, Seq, Name, Def]()
	{
		UAsdaDevMenuSubsystem* Self = WeakThis.Get();
		if (!Self || Self->SliderPersistSeq != Seq) { return; }   // было новое изменение — его таймер допишет
		Self->ApplyCVarPersist(Name, FString::SanitizeFloat(GetCVarFloat(*Name, Def)));
	});
}

void UAsdaDevMenuSubsystem::SelfTestSliderKeyboard(const FString& Needle, float T01)
{
	for (const FRow& R : Rows)
	{
		if (!R.Slider.IsValid() || !R.Label || !R.Label().Contains(Needle)) { continue; }
		R.Slider->SetValue(T01);
		R.Slider->OnValueChanged.Broadcast(T01);   // как SSlider::CommitValue со стрелки без Enter-захвата
		UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest[slider-kbd]: OnValueChanged(%.2f) без захвата → «%s»"), T01, *R.Label());
	}
}

void UAsdaDevMenuSubsystem::SelfTestSliderDrag(const FString& Needle, float T01, int32 Phase)
{
	// ★★★ ПЕРЕТАСКИВАНИЕ ползунка мышью — путь, который самопроверка раньше НЕ ПРОХОДИЛА (ревизия review_drawdistance.md §6:
	//   «путь ни разу не выполнялся, потому что asda.DevMenu.SelfTest ползунки не кликает»). SelfTestClickRows умеет только
	//   кнопки (у ползунка RowRelayIndex = −1), а SelfTestSliderKeyboard шлёт ОДНО значение без захвата. Здесь воспроизводится
	//   полный жест: захват ручки → череда OnValueChanged (по одному на кадр самопроверки) → отпускание, то есть ровно те
	//   делегаты, которые шлёт SSlider (Slider.cpp: OnMouseCaptureBegin/OnValueChanged/OnMouseCaptureEnd).
	for (const FRow& R : Rows)
	{
		if (!R.Slider.IsValid() || !R.Label || !R.Label().Contains(Needle)) { continue; }
		if (Phase == 0)
		{
			R.Slider->OnMouseCaptureBegin.Broadcast();
			UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest[slider-drag]: захват ручки «%s»"), *R.Label());
		}
		if (Phase <= 1)
		{
			R.Slider->SetValue(T01);
			R.Slider->OnValueChanged.Broadcast(T01);   // как SSlider::CommitValue при движении мыши с захватом
		}
		if (Phase == 2)
		{
			R.Slider->OnMouseCaptureEnd.Broadcast();
			UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest[slider-drag]: отпускание ручки «%s» на %.3f"), *R.Label(), R.Slider->GetValue());
		}
	}
}

int32 UAsdaDevMenuSubsystem::SelfTestLogSliders(const TCHAR* Tag)
{
	int32 Mismatch = 0;   // ручек не на месте (★ 12.10.2026: возвращается вызывающему как число провалов)
	for (const FRow& R : Rows)
	{
		if (!R.Slider.IsValid() || !R.SliderT01) { continue; }
		const float Have = R.Slider->GetValue(), Want = R.SliderT01();
		const bool bOk = FMath::IsNearlyEqual(Have, Want, 0.005f);
		Mismatch += bOk ? 0 : 1;
		UE_LOG(LogAsdaDevMenu, Log, TEXT("SelfTest[%s]: ползунок «%s» ручка=%.3f ожидание=%.3f %s"), Tag, R.Label ? *R.Label() : TEXT("?"),
			Have, Want, bOk ? TEXT("OK") : TEXT("РАСХОЖДЕНИЕ"));
	}
	return Mismatch;
}

void UAsdaDevMenuSubsystem::ApplyFogScaleAllWorlds()
{
	if (!GEngine) { return; }
	for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
	{
		if (Ctx.World() && Ctx.World()->IsGameWorld() && Ctx.OwningGameInstance == GetGameInstance()) { ApplyFogScale(Ctx.World()); }
	}
}

// ─────────────────────────── Рельеф: MPC_AsdaNormals ───────────────────────────
UMaterialParameterCollection* UAsdaDevMenuSubsystem::GetNormalsMPC()
{
	if (!NormalsMPC)
	{
		NormalsMPC = LoadObject<UMaterialParameterCollection>(nullptr, AsdaMat::kNormalsMpcPath);   // ★ В1-C F10
		if (!NormalsMPC) { UE_LOG(LogAsdaDevMenu, Warning, TEXT("MPC_AsdaNormals не загружена — вкладка «Рельеф» без эффекта")); }
	}
	return NormalsMPC;
}

// ★ E-0b: уровень нашего per-level слоя [AsdaQuality@N] (Config/DefaultScalability.ini) = sg.EffectsQuality — рельеф живёт в
//   материалах, там же r.MaterialQualityLevel (EffectsQuality@N BaseScalability). Не отдельный sg (у движка их 11, свой не завести
//   без правки Scalability.cpp) и не число в C++ (второй источник истины запрещён — port-shared-points).
int32 UAsdaDevMenuSubsystem::GetAsdaQualityLevel()
{
	// ★ Дедуп №2 В1-C (F17): чтение — базой подсистем качества (без профиля — 3, как прежде).
	// ★ Дедуп №2 В2-C (хвост F17, 11.10.2026): зажим 0..4 перенесён в базу (`EffectsQualityLevel`) — своего здесь больше нет.
	return UAsdaRenderQualitySubsystemBase::EffectsQualityLevel();
}

void UAsdaDevMenuSubsystem::ApplyNormals(UWorld* World)
{
	UMaterialParameterCollection* MPC = GetNormalsMPC();
	if (!World || !MPC) { return; }
	const int32 Level = GetAsdaQualityLevel();
	for (const FName& P : GNormalParams)
	{
		bool bFound = false;
		float V = MPC->GetScalarParameterDefaultValue(P, bFound);   // нет ключа → дефолт ассета (так работает и сброс)
		if (!bFound) { UE_LOG(LogAsdaDevMenu, Warning, TEXT("MPC_AsdaNormals: параметра %s нет в ассете"), *P.ToString()); continue; }
		// ★ E-0b: база уровня — [AsdaQuality@N] Normals.<Параметр> из DefaultScalability.ini (Low: Enable 0; Medium: RoughFromMap 0);
		//   нет ключа в секции → дефолт ассета. Пользовательский [AsdaNormals] (Ctrl+N «Рельеф») — поверх, как и раньше.
		const FString LevelV = ScalabilityLevelValue(TEXT("AsdaQuality"), Level, *FString::Printf(TEXT("Normals.%s"), *P.ToString()), TEXT(""));
		const bool bLevel = !LevelV.IsEmpty();
		if (bLevel) { V = FCString::Atof(*LevelV); }
		float Saved = 0.f;
		const bool bSaved = GConfig && GConfig->GetFloat(TEXT("AsdaNormals"), *P.ToString(), Saved, GGameUserSettingsIni);
		if (bSaved) { V = Saved; }
		UKismetMaterialLibrary::SetScalarParameterValue(World, MPC, P, V);
		UE_LOG(LogAsdaDevMenu, Log, TEXT("[AsdaNormals] %s=%.3f%s [мир %s]"), *P.ToString(), V,
			bSaved ? TEXT(" (вручную)") : (bLevel ? *FString::Printf(TEXT(" (уровень AsdaQuality@%d)"), Level) : TEXT(" (дефолт ассета)")), *World->GetName());
	}
}

void UAsdaDevMenuSubsystem::OnScalabilityChanged(const Scalability::FQualityLevels& Levels)
{
	// Все игровые миры этого процесса (в -game один; в PIE может быть несколько) — перенакат рельефа по новому уровню.
	int32 N = 0;   // сколько миров обработано (в лог)
	if (GEngine)
	{
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.World() && Ctx.World()->IsGameWorld()) { ApplyNormals(Ctx.World()); ++N; }
		}
	}
	UE_LOG(LogAsdaDevMenu, Log, TEXT("Уровень качества изменён (fx=%d → AsdaQuality@%d): рельеф перенакачен в %d мир(ов)"), Levels.EffectsQuality, GetAsdaQualityLevel(), N);
	RefreshRows();
}

void UAsdaDevMenuSubsystem::SetNormalPersist(const FName& Param, float Value)
{
	UMaterialParameterCollection* MPC = GetNormalsMPC();
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (MPC && W) { UKismetMaterialLibrary::SetScalarParameterValue(W, MPC, Param, Value); }
	if (GConfig)
	{
		GConfig->SetFloat(TEXT("AsdaNormals"), *Param.ToString(), Value, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	UE_LOG(LogAsdaDevMenu, Log, TEXT("Настройка: [AsdaNormals] %s=%.3f"), *Param.ToString(), Value);
}

float UAsdaDevMenuSubsystem::GetNormalValue(const FName& Param)
{
	UMaterialParameterCollection* MPC = GetNormalsMPC();
	if (!MPC) { return 0.f; }
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (W) { return UKismetMaterialLibrary::GetScalarParameterValue(W, MPC, Param); }
	bool bFound = false;
	return MPC->GetScalarParameterDefaultValue(Param, bFound);
}

// ─────────────────── «Требует перезапуска»: RHI + skin cache ───────────────────
// RHI — ШТАТНЫЙ механизм UE (проверен по исходникам 5.8, WindowsDynamicRHI.cpp::ChoosePreferredRHI):
// GameUserSettings.ini [D3DRHIPreference] PreferredRHI = "dx12" | "dx11". В игре разрешён по
// умолчанию (IsPreferredRHIAllowed: fallback true, отключаем только [RHI] bUsePreferredRHI=false).
// Шейдер-форматы проекта: DX12→SM6 (VSM живёт), DX11→SM5 (VSM сам отключится → CSM-фолбэк).
bool UAsdaDevMenuSubsystem::GetPendingRHIIsDx12()
{
	// ★ 19.09.2026 (фаза 2): одно место чтения/записи — профиль игрока (его же зовёт окно настроек).
	return UAsdaGameUserSettings::GetPendingRHIIsDx12();
}

bool UAsdaDevMenuSubsystem::GetActiveRHIIsDx12()
{
	return GDynamicRHI && FCString::Strstr(GDynamicRHI->GetName(), TEXT("D3D12")) != nullptr;
}

void UAsdaDevMenuSubsystem::SetPendingRHI(bool bDx12)
{
	UAsdaGameUserSettings::SetPendingRHI(bDx12);
}

// ─────────────────────────── Сброс к дефолтам проекта ───────────────────────────
// ★ 05.09.2026 (ревизия review_devmenu_tabs.md В-3/В-5). Дефолт проекта для CVar — из merged Engine.ini (ProjectDefault), не из
// зашитых чисел: было `r.AntiAliasingMethod=2` (TAA, «выбор юзера 02.08») при тогдашнем DefaultEngine.ini 4 (TSR; с 09.09.2026 в проекте 1 = FXAA) —
// сброс менял картинку относительно чистого проекта. Сброс CVar = удалить ключ [AsdaVideo]/[AsdaRender] + Unset(ECVF_SetByConsole):
// история приоритетов возвращает значение ProjectSetting/SystemSettings/Scalability/дефолт кода; пресетные ключи (GPresetManagedCVarKeys,
// не пришпиленные ini) получают значение повторным применением пресета — sg-группы снова влияют на них (при пришпиливании
// SetByConsole «Low» давал анизотропию 8 и bloom 5). Остальное (не CVar): гамма 2.2 (ключ [AsdaVideo] DisplayGamma удаляется),
// VSync off, лимит FPS off, desktop-WFS, RHI — ключ [D3DRHIPreference] удаляется (нет ключа = проектный DX12), пресет по умолчанию
// (★ 12.10.2026: High «Высокое», `UAsdaVideoSettingsSubsystem::kDefaultPreset`; раньше Epic) + glow 4
// БЕЗ автопробника (★ В-1: ApplyDefaultQuality(false) — сброс не гоняет SynthBenchmark), рельеф = дефолты MPC_AsdaNormals (ключи удаляются), skin cache on.
FString UAsdaDevMenuSubsystem::ProjectDefault(const FString& Name, FString* OutSource)
{
	// "True"/"False" из ini → "1"/"0" (у CVar строковый Set понимает числа).
	auto Norm = [](const FString& V) -> FString
	{
		if (V.Equals(TEXT("True"), ESearchCase::IgnoreCase))  { return TEXT("1"); }
		if (V.Equals(TEXT("False"), ESearchCase::IgnoreCase)) { return TEXT("0"); }
		return V.TrimStartAndEnd();
	};
	FString V;
	if (GConfig)
	{
		if (GConfig->GetString(TEXT("/Script/Engine.RendererSettings"), *Name, V, GEngineIni) && !V.IsEmpty())
		{
			if (OutSource) { *OutSource = TEXT("Engine.ini [/Script/Engine.RendererSettings]"); }
			return Norm(V);
		}
		if (GConfig->GetString(TEXT("SystemSettings"), *Name, V, GEngineIni) && !V.IsEmpty())
		{
			if (OutSource) { *OutSource = TEXT("Engine.ini [SystemSettings]"); }
			return Norm(V);
		}
	}
	if (IConsoleVariable* CV = FindCVar(*Name))
	{
		if (OutSource) { *OutSource = TEXT("дефолт кода CVar"); }
		return Norm(CV->GetDefaultValue());
	}
	if (OutSource) { *OutSource = TEXT("нет такой CVar"); }
	return FString();
}

bool UAsdaDevMenuSubsystem::IsPresetManaged(const FString& Name)
{
	bool bInList = false;   // ключ есть в списке scalability-управляемых
	for (const TCHAR* K : GPresetManagedCVarKeys) { if (Name.Equals(K, ESearchCase::IgnoreCase)) { bInList = true; break; } }
	if (!bInList) { return false; }
	// Пришпилен проектным ini → приоритет проекта выше Scalability, пресет его не трогает → не «пресетный».
	FString Src;
	ProjectDefault(Name, &Src);
	return !Src.StartsWith(TEXT("Engine.ini"));
}

bool UAsdaDevMenuSubsystem::ResetCVar(const FString& Name)
{
	// ★ 19.09.2026 (фаза 2): профильные ключи сбрасываются в профиле («не менялось» → значение проекта/уровня).
	if (UAsdaVideoSettingsSubsystem* Video = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr)
	{
		if (Video->ClearRenderOverride(Name, /*bSave*/ true)) { return IsPresetManaged(Name); }
		if (Name.Equals(TEXT("r.ScreenPercentage"), ESearchCase::IgnoreCase)) { Video->SetRenderScalePercent(100, /*bSave*/ true); return false; }
	}
	const TCHAR* Section = Name.StartsWith(TEXT("asda.")) ? TEXT("AsdaRender") : TEXT("AsdaVideo");
	if (GConfig) { GConfig->RemoveKey(Section, *Name, GGameUserSettingsIni); }
	IConsoleVariable* CV = FindCVar(*Name);
	if (!CV) { UE_LOG(LogAsdaDevMenu, Warning, TEXT("Сброс: CVar %s не найдена"), *Name); return false; }
	const FString Before = CV->GetString();
	CV->Unset(ECVF_SetByConsole);   // снять наш оверрайд; история приоритетов вернёт значение проекта/пресета (колбэки вызываются)
	const bool bPreset = IsPresetManaged(Name);
	FString Src;
	const FString Def = ProjectDefault(Name, &Src);
	if (!bPreset && !Def.IsEmpty())
	{
		// Совпадение считаем численно (строки "0.1" vs "0.100000" одно и то же), иначе — по строке.
		const bool bNumeric = Def.IsNumeric() && CV->GetString().IsNumeric();
		const bool bSame = bNumeric ? FMath::IsNearlyEqual(FCString::Atof(*Def), CV->GetFloat(), 1e-4f)
		                            : CV->GetString().Equals(Def, ESearchCase::IgnoreCase);
		if (!bSame)
		{
			// Истории нет (ключ никогда не ставился консолью) или она вернула не проектное — ставим дефолт проекта.
			CV->Set(*Def, ECVF_SetByConsole);
		}
	}
	UE_LOG(LogAsdaDevMenu, Log, TEXT("Сброс: [%s] %s удалён; %s → %s (SetBy %s); дефолт проекта %s из %s%s"),
		Section, *Name, *Before, *CV->GetString(), CVarSetByName(CV), Def.IsEmpty() ? TEXT("-") : *Def, *Src,
		bPreset ? TEXT("; управляется пресетом → переприменение пресета") : TEXT(""));
	return bPreset;
}

// ★ 12.10.2026: список Rc(...) каждой вкладки = РОВНО оставшиеся строки (03_tester.md §3.2 Р2: лишний Rc — мёртвый код,
//   недостающий — незачищаемый пин). Ключи убранных строк здесь не сбрасываются: их старые пины снимает SweepRetiredPins при старте.
void UAsdaDevMenuSubsystem::ResetTab(EAsdaDevTab Tab)
{
	UAsdaVideoSettingsSubsystem* Video = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr;
	UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	bool bPreset = false;   // задет хотя бы один CVar, управляемый пресетом → переприменить пресет один раз в конце
	auto Rc = [this, &bPreset](const TCHAR* K) { bPreset |= ResetCVar(K); };
	switch (Tab)
	{
	case EAsdaDevTab::Screen:
		Rc(TEXT("r.ScreenPercentage"));   // ★ фаза 2: штатный масштаб профиля → 100 %
		// Гамма: яркость профиля → 50 (= UE-дефолт 2.2), тем же путём, что полоса 70058 окна настроек.
		if (Video) { Video->SetGamma(50); Video->SaveProfile(); }
		if (S) { S->SetVSyncEnabled(false); S->SetFrameRateLimit(0.f); S->ApplyNonResolutionSettings(); S->SaveSettings(); }
		if (Video) { Video->ApplyResolution(Video->GetDesktopResolution(), /*WindowedFullscreen*/1); }
		// RHI: нет ключа [D3DRHIPreference] = проектный DefaultGraphicsRHI (DX12) — GetPendingRHIIsDx12 так и читает.
		if (GConfig) { GConfig->RemoveKey(TEXT("D3DRHIPreference"), TEXT("PreferredRHI"), GGameUserSettingsIni); }
		break;
	case EAsdaDevTab::Quality:
		// ★ В-1 (review_setE0.md): сброс = пресет по умолчанию (★ 12.10.2026 High, `kDefaultPreset`; раньше Epic) + glow 4 БЕЗ
		//   автопробника (bAllowBenchmark=false) — сброс не гонит SynthBenchmark; замер кадра — только по кнопке «Определить автоматически».
		if (Video) { Video->ApplyDefaultQuality(/*bAllowBenchmark*/false); }
		Rc(TEXT("r.AntiAliasingMethod"));   // из ini проекта (DefaultEngine.ini; на 14.09.2026 это 1 = FXAA), не зашитое 2
		Rc(TEXT("r.Tonemapper.Sharpen"));
		Rc(TEXT("r.MotionBlur.Amount"));    // дефолт кода (-1 = PP; у проекта r.DefaultFeature.MotionBlur=False)
		Rc(TEXT("asda.Cull.Scale"));        // ползунок «Дальность прорисовки» → поле профиля «не менялось» (= множитель уровня × 1)
		break;
	case EAsdaDevTab::Sky:
		Rc(TEXT("asda.Fog.DensityScale"));    // ползунок плотности → дефолт проекта (снятие ключа + Unset)
		Rc(TEXT("asda.Fog.DistanceScale"));   // ползунок дальности → дефолт проекта (колбэк OnAsdaFogChanged пересчитает туман)
		Rc(TEXT("asda.Clouds.Field"));        // 3D-облака → поле профиля «не менялось»
		break;
	case EAsdaDevTab::Relief:
		// Снять оверрайд единственной строки (NormalStrength) → ApplyNormals вернёт значение уровня/дефолт ассета в экземпляр мира.
		if (GConfig) { GConfig->RemoveKey(TEXT("AsdaNormals"), *GNormalParams[kNormalStrengthIdx].ToString(), GGameUserSettingsIni); }
		if (UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr) { ApplyNormals(W); }
		break;
	case EAsdaDevTab::Chars:
		Rc(TEXT("asda.Rim.Enable"));
		Rc(TEXT("asda.Rim.Scale"));
		Rc(TEXT("asda.Rim.OriginalOff"));
		Rc(TEXT("asda.Shadow.Capsule"));
		Rc(TEXT("asda.Shadow.CapsuleMinVisibility"));   // дефолт кода (решение владельца 05.09 зашито в регистрации CVar)
		break;
	default:
		break;
	}
	if (GConfig) { GConfig->Flush(false, GGameUserSettingsIni); }
	if (bPreset && Video) { Video->ReapplyQuality(); }
	UE_LOG(LogAsdaDevMenu, Log, TEXT("Сброс вкладки %d «%s» к дефолтам проекта выполнен%s"), (int32)Tab,
		((int32)Tab >= 0 && (int32)Tab < (int32)EAsdaDevTab::Count) ? GDevTabNames[(int32)Tab] : TEXT("?"), bPreset ? TEXT(" (пресет переприменён)") : TEXT(""));
	RefreshRows();
}

void UAsdaDevMenuSubsystem::ResetToProjectDefaults()
{
	for (int32 i = 0; i < (int32)EAsdaDevTab::Reset; ++i) { ResetTab((EAsdaDevTab)i); }
	UE_LOG(LogAsdaDevMenu, Log, TEXT("Сброс к дефолтам проекта выполнен (все вкладки)"));
	RefreshRows();
}
