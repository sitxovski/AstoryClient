// Copyright (c) 2026 @sitxovski (Discord)
// Реализация подсветки выбранной цели — см. AsdaTargetHighlightComponent.h (там весь разбор эталона).
#include "World/AsdaTargetHighlightComponent.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "Components/MeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Actor.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectIterator.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "Resources/AsdaVideoSettingsSubsystem.h"   // ★ 19.09.2026: после снятия подсветки — пересчёт пометки «Свечения»

DEFINE_LOG_CATEGORY_STATIC(LogAsdaTargetFx, Log, All);

// Путь материала-оверлея по умолчанию (создаётся Tools/asda_build_target_highlight_mat.py).
static const TCHAR* kHighlightMat = TEXT("/Game/Materials/M_AsdaTargetHighlight.M_AsdaTargetHighlight");

// Имена параметров материала (должны совпадать со скриптом сборки материала).
static const FName kPColor(TEXT("Color"));
static const FName kPFill(TEXT("FillAdd"));
static const FName kPRimI(TEXT("RimIntensity"));
static const FName kPRimP(TEXT("RimPower"));
static const FName kPMaxA(TEXT("MaxAdd"));

const FAsdaHighlightStyle& UAsdaTargetHighlightSettings::StyleFor(EAsdaTargetRelation Rel) const
{
	switch (Rel)
	{
	case EAsdaTargetRelation::Hostile:  return Hostile;
	case EAsdaTargetRelation::Friendly: return Friendly;
	case EAsdaTargetRelation::Player:   return PlayerStyle;
	default:                            return Neutral;
	}
}

UAsdaTargetHighlightComponent::UAsdaTargetHighlightComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;   // тикаем только пока подсветка включена
	// Подсвечивать модель цели, но НЕ служебную мелочь над ней.
	IgnoreComponentSubstrings = { TEXT("QuestMark"), TEXT("Nameplate"), TEXT("Plate"), TEXT("Marker"), TEXT("Decal") };
}

const FAsdaHighlightStyle& UAsdaTargetHighlightComponent::CurrentStyle() const
{
	if (const UAsdaTargetHighlightSettings* S = ActiveSettings.Get()) { return S->StyleFor(ActiveRelation); }
	return DefaultStyle;
}

// ★ Консольный оверрайд режима: -1 = не задан (берётся из DataAsset/компонента). Нужен потому, что
//   headless слеп к этому визуалу — юзер за ОДИН прогон перебирает режимы прямо в игре.
static int32 GAsdaTargetHLMode = -1;

EAsdaHighlightMode UAsdaTargetHighlightComponent::EffectiveMode() const
{
	if (GAsdaTargetHLMode >= 0 && GAsdaTargetHLMode <= (int32)EAsdaHighlightMode::Both)
	{
		return (EAsdaHighlightMode)GAsdaTargetHLMode;
	}
	if (const UAsdaTargetHighlightSettings* S = ActiveSettings.Get()) { return S->Mode; }
	return Mode;
}

static const TCHAR* AsdaHighlightModeName(EAsdaHighlightMode M)
{
	switch (M)
	{
	case EAsdaHighlightMode::Brightness: return TEXT("яркость (задумка ориг. шейдера, +0.2)");
	case EAsdaHighlightMode::Outline:    return TEXT("обводка (улучшение порта)");
	case EAsdaHighlightMode::Both:       return TEXT("яркость+обводка (улучшение порта)");
	default:                             return TEXT("выключено (1:1 оригинал — модель не подсвечивается)");
	}
}

void UAsdaTargetHighlightComponent::ReapplyNow()
{
	if (!bActive) { return; }
	RemoveOverlay();
	ApplyOverlay();
	SetComponentTickEnabled(Painted.Num() > 0);
}

// Дамп ФАКТИЧЕСКИХ значений (не дефолтов ассета): что реально лежит в MID и на компонентах.
void UAsdaTargetHighlightComponent::DumpRuntimeState() const
{
	const AActor* Ow = GetOwner();
	UE_LOG(LogAsdaTargetFx, Log, TEXT("[таргет/HL] %s: активна=%d, режим=%s, стилей-источник=%s, покрашено компонентов=%d"),
		Ow ? *Ow->GetName() : TEXT("<нет владельца>"), bActive ? 1 : 0,
		AsdaHighlightModeName(EffectiveMode()),
		ActiveSettings.IsValid() ? TEXT("DataAsset") : TEXT("дефолты компонента"),
		Painted.Num());
	if (!Mid)
	{
		UE_LOG(LogAsdaTargetFx, Log, TEXT("[таргет/HL]   MID НЕТ (материал не создан)"));
		return;
	}
	UE_LOG(LogAsdaTargetFx, Log, TEXT("[таргет/HL]   материал=%s"),
		Mid->Parent ? *Mid->Parent->GetPathName() : TEXT("<нет мастера>"));
	auto DumpF = [this](const FName& N)
	{
		float V = -12345.f;
		const bool bOk = Mid->GetScalarParameterValue(FMaterialParameterInfo(N), V);
		UE_LOG(LogAsdaTargetFx, Log, TEXT("[таргет/HL]   %s = %s"), *N.ToString(),
			bOk ? *FString::SanitizeFloat(V) : TEXT("ПАРАМЕТРА НЕТ В МАТЕРИАЛЕ"));
	};
	DumpF(kPFill); DumpF(kPRimI); DumpF(kPRimP); DumpF(kPMaxA);
	FLinearColor C;
	if (Mid->GetVectorParameterValue(FMaterialParameterInfo(kPColor), C))
	{
		UE_LOG(LogAsdaTargetFx, Log, TEXT("[таргет/HL]   Color = (%.3f, %.3f, %.3f)"), C.R, C.G, C.B);
	}
	for (const TWeakObjectPtr<UMeshComponent>& W : Painted)
	{
		if (const UMeshComponent* M = W.Get())
		{
			UE_LOG(LogAsdaTargetFx, Log, TEXT("[таргет/HL]   меш %s: overlay=%s"), *M->GetName(),
				M->GetOverlayMaterial() ? *M->GetOverlayMaterial()->GetName() : TEXT("НЕТ"));
		}
	}
}

// ── Консольные команды (headless слеп к визуалу — переключаем и смотрим в игре) ──
static void AsdaTargetHL_Cmd(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
{
	if (Args.Num() > 0)
	{
		const int32 V = FCString::Atoi(*Args[0]);
		GAsdaTargetHLMode = (V >= 0 && V <= (int32)EAsdaHighlightMode::Both) ? V : -1;
	}
	else
	{
		GAsdaTargetHLMode = -1;   // без аргумента — вернуть управление настройкам
	}
	int32 N = 0;
	for (TObjectIterator<UAsdaTargetHighlightComponent> It; It; ++It)
	{
		UAsdaTargetHighlightComponent* C = *It;
		if (!IsValid(C) || (World && C->GetWorld() != World)) { continue; }
		C->ReapplyNow();
		++N;
	}
	Ar.Logf(TEXT("[таргет/HL] режим=%s (оверрайд=%d), перекрашено компонентов=%d. "
	             "0=выкл(оригинал) 1=яркость 2=обводка 3=обе; без аргумента — из настроек."),
	        GAsdaTargetHLMode >= 0 ? AsdaHighlightModeName((EAsdaHighlightMode)GAsdaTargetHLMode)
	                               : TEXT("из настроек"), GAsdaTargetHLMode, N);
}

static void AsdaTargetHLDump_Cmd(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
{
	int32 N = 0;
	for (TObjectIterator<UAsdaTargetHighlightComponent> It; It; ++It)
	{
		const UAsdaTargetHighlightComponent* C = *It;
		if (!IsValid(C) || (World && C->GetWorld() != World)) { continue; }
		C->DumpRuntimeState();
		++N;
	}
	Ar.Logf(TEXT("[таргет/HL] компонентов подсветки в мире: %d (подробности — в логе)"), N);
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldArgsAndOutputDevice GAsdaCmdHL(
	TEXT("asda.Target.HL"),
	TEXT("Режим подсветки цели: 0=выкл (как в оригинале), 1=яркость, 2=обводка, 3=обе. Без аргумента — брать из настроек."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&AsdaTargetHL_Cmd));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldArgsAndOutputDevice GAsdaCmdHLDump(
	TEXT("asda.Target.HLDump"),
	TEXT("Дамп ФАКТИЧЕСКИХ параметров подсветки цели в рантайме (материал, FillAdd/Rim/MaxAdd/Color, оверлеи)."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&AsdaTargetHLDump_Cmd));
#endif // ASDA_DEV_CONSOLE

void UAsdaTargetHighlightComponent::CollectMeshes(TArray<UMeshComponent*>& Out) const
{
	const AActor* Owner = GetOwner();
	if (!Owner) { return; }
	TArray<UMeshComponent*> All;
	Owner->GetComponents<UMeshComponent>(All);
	for (UMeshComponent* M : All)
	{
		if (!M) { continue; }
		const FString Name = M->GetName();
		bool bSkip = false;
		for (const FString& Sub : IgnoreComponentSubstrings)
		{
			if (!Sub.IsEmpty() && Name.Contains(Sub)) { bSkip = true; break; }
		}
		if (bSkip) { continue; }
		Out.Add(M);
	}
}

void UAsdaTargetHighlightComponent::ApplyOverlay()
{
	const EAsdaHighlightMode M = EffectiveMode();
	if (M == EAsdaHighlightMode::None) { return; }

	// Материал: из DataAsset либо дефолтный ассет.
	UMaterialInterface* Master = nullptr;
	if (const UAsdaTargetHighlightSettings* S = ActiveSettings.Get())
	{
		Master = S->HighlightMaterial.LoadSynchronous();
	}
	if (!Master)
	{
		Master = LoadObject<UMaterialInterface>(nullptr, kHighlightMat, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	if (!Master)
	{
		UE_LOG(LogAsdaTargetFx, Warning,
			TEXT("[таргет] материал подсветки %s не найден — подсветка не показана (собери его Tools/asda_build_target_highlight_mat.py)"),
			kHighlightMat);
		return;
	}
	if (!Mid || Mid->Parent != Master)
	{
		Mid = UMaterialInstanceDynamic::Create(Master, this);
	}
	PushParams(1.f);

	const bool bDepth = ActiveSettings.IsValid() ? ActiveSettings->bWriteCustomDepth : false;
	const int32 Stencil = ActiveSettings.IsValid() ? ActiveSettings->CustomDepthStencil : 1;

	TArray<UMeshComponent*> Meshes;
	CollectMeshes(Meshes);
	Painted.Reset();
	PaintedDepth.Reset();
	for (UMeshComponent* Mesh : Meshes)
	{
		Mesh->SetOverlayMaterial(Mid);
		FDepthBefore Before;   // состояние CustomDepth до подсветки (для точного возврата)
		if (bDepth)
		{
			Before.bChanged = true;
			Before.bWasOn = Mesh->bRenderCustomDepth;
			Before.Stencil = Mesh->CustomDepthStencilValue;
			Mesh->SetCustomDepthStencilValue(Stencil);
			Mesh->SetRenderCustomDepth(true);
		}
		Painted.Add(Mesh);
		PaintedDepth.Add(Before);
	}
}

void UAsdaTargetHighlightComponent::RemoveOverlay()
{
	for (int32 I = 0; I < Painted.Num(); ++I)
	{
		if (UMeshComponent* Mesh = Painted[I].Get())
		{
			Mesh->SetOverlayMaterial(nullptr);
			// ★ 19.09.2026 (ревизия свечения, С1): CustomDepth трогаем ТОЛЬКО если его включала сама подсветка,
			//   и возвращаем ровно прежнее состояние. Прежнее безусловное `SetRenderCustomDepth(false)` снимало
			//   пометку трафарета пункта «Свечение» (UAsdaVideoSettingsSubsystem, 10/11) — моб после снятия цели
			//   до следующей перепометки то гас, то светился.
			const FDepthBefore& B = PaintedDepth.IsValidIndex(I) ? PaintedDepth[I] : FDepthBefore();
			if (B.bChanged)
			{
				Mesh->SetCustomDepthStencilValue(B.Stencil);
				Mesh->SetRenderCustomDepth(B.bWasOn);
			}
		}
	}
	const bool bHadDepth = PaintedDepth.ContainsByPredicate([](const FDepthBefore& B) { return B.bChanged; });   // подсветка держала CustomDepth
	Painted.Reset();
	PaintedDepth.Reset();
	// ★ 19.09.2026 (свечение, МЕЛК): снимок «до подсветки» мог устареть — за время подсветки игрок сменил пункт
	//   «Свечения» (вернули пометку 11 на пункте 0/4, где её снимать уже некому). Проход свечения пересчитывает
	//   пометку этого актора под ТЕКУЩИЙ пункт: нужна — ставит, не нужна — снимает только свои 10/11.
	if (bHadDepth) { UAsdaVideoSettingsSubsystem::RefreshGlowMarksFor(GetOwner()); }
}

bool UAsdaTargetHighlightComponent::OwnsCustomDepth(const UMeshComponent* Mesh) const
{
	if (!bActive || !Mesh) { return false; }
	for (int32 I = 0; I < Painted.Num(); ++I)
	{
		if (Painted[I].Get() == Mesh && PaintedDepth.IsValidIndex(I) && PaintedDepth[I].bChanged) { return true; }
	}
	return false;
}

void UAsdaTargetHighlightComponent::PushParams(float PulseValue)
{
	if (!Mid) { return; }
	const FAsdaHighlightStyle& St = CurrentStyle();
	const EAsdaHighlightMode M = EffectiveMode();
	// Режим = какие каналы включены. Яркость — 1:1 оригинал (аддитивная заливка), обводка — надстройка.
	const float Fill = (M == EAsdaHighlightMode::Brightness || M == EAsdaHighlightMode::Both) ? St.FillAdd : 0.f;
	const float Rim  = (M == EAsdaHighlightMode::Outline    || M == EAsdaHighlightMode::Both) ? St.RimIntensity : 0.f;
	Mid->SetVectorParameterValue(kPColor, St.Color);
	Mid->SetScalarParameterValue(kPFill,  Fill * PulseValue);
	Mid->SetScalarParameterValue(kPRimI,  Rim  * PulseValue);
	Mid->SetScalarParameterValue(kPRimP,  FMath::Max(0.5f, St.RimPower));
	// ★ Потолок добавки — материал физически не может выбелить меш (баг «белый силуэт» 2026-08-04).
	Mid->SetScalarParameterValue(kPMaxA,  FMath::Max(0.05f, St.MaxAdd));
}

void UAsdaTargetHighlightComponent::SetHighlighted(bool bOn, EAsdaTargetRelation Rel, const UAsdaTargetHighlightSettings* Settings)
{
	if (Settings) { ActiveSettings = Settings; }
	if (bOn && bActive && Rel == ActiveRelation) { return; }
	ActiveRelation = Rel;

	if (bOn)
	{
		RemoveOverlay();
		bActive = true;
		PulsePhase = 0.f;
		ApplyOverlay();
		SetComponentTickEnabled(Painted.Num() > 0);
	}
	else
	{
		bActive = false;
		RemoveOverlay();
		SetComponentTickEnabled(false);
	}
}

void UAsdaTargetHighlightComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bActive) { return; }
	const FAsdaHighlightStyle& St = CurrentStyle();
	if (St.PulseAmp <= KINDA_SMALL_NUMBER || St.PulseHz <= KINDA_SMALL_NUMBER)
	{
		return;   // ровное свечение — параметры уже записаны, тик ничего не меняет
	}
	PulsePhase += DeltaTime * St.PulseHz * 2.f * PI;
	const float Pulse = 1.f + St.PulseAmp * 0.5f * (1.f - FMath::Cos(PulsePhase));   // 1 .. 1+Amp
	PushParams(Pulse);
}

void UAsdaTargetHighlightComponent::OnUnregister()
{
	RemoveOverlay();
	bActive = false;
	Super::OnUnregister();
}
