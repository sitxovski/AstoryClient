// Copyright (c) 2026 @sitxovski (Discord)
// Реализация вьюпорта «Студии нормалей» — см. SAsdaNormalStudioViewport.h.
#include "SAsdaNormalStudioViewport.h"

#include "AdvancedPreviewScene.h"
#include "EditorViewportClient.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInstanceConstant.h"
#include "UObject/ConstructorHelpers.h"
#include "Misc/PackageName.h"

// Мастера предпросмотра (создаются скриптом @@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Tools/NormalStudio/ue_build_materials.py — не входит в репозиторий).
// Живут ОТДЕЛЬНО от материалов игры и в рантайме не используются.
static const TCHAR* kMatObject = TEXT("/Game/_NormalStudio/M_NS_Object");   // альфа = прозрачность
static const TCHAR* kMatChar   = TEXT("/Game/_NormalStudio/M_NS_Char");     // альфа = маска блика
static const TCHAR* kMapsPath  = TEXT("/Game/_NormalStudio/Maps");

// ★ Имя ассета карты собрано упаковщиком pack_for_ue.py: все не-буквенно-цифровые символы имени
//   текстуры заменены на подчёркивание (в путях пакетов UE точки и дефисы недопустимы).
static FString SafeMapName(const FString& TextureName)
{
	FString Out;
	Out.Reserve(TextureName.Len());
	for (const TCHAR C : TextureName)
	{
		Out.AppendChar((FChar::IsAlnum(C) || C == TEXT('-') || C == TEXT('_')) ? C : TEXT('_'));
	}
	return Out;
}

// ★★★ ОСНОВНОЙ КЛЮЧ КАРТЫ — ПУТЬ ПАКЕТА ТЕКСТУРЫ, а не её имя.
//   Имена в проекте НЕ уникальны: `CHEK_002` лежит в девяти папках в пяти разных вариантах, а
//   портрет NPC зовётся так же, как текстура его модели (113 таких пар). По имени 19% материалов
//   получали карту, посчитанную с ЧУЖОЙ картинки. Путь уникален — по нему и ищем.
static FString MapKeyFor(const UTexture2D* Tex)
{
	FString Key = Tex->GetOutermost()->GetName();
	Key.RemoveFromStart(TEXT("/"));
	Key.ReplaceInline(TEXT("/"), TEXT("_"));
	return Key;
}

// Найти альбедо слота: у материалов проекта оно лежит в параметре "Tex", у прочих берём первую текстуру.
static UTexture2D* FindAlbedo(UMaterialInterface* Mat)
{
	if (!Mat)
	{
		return nullptr;
	}
	UTexture* Found = nullptr;
	if (Mat->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Tex")), Found) && Found)
	{
		return Cast<UTexture2D>(Found);
	}
	// Без аргументов = все уровни качества и все платформы шейдеров (в 5.7 старая перегрузка
	// с ERHIFeatureLevel помечена устаревшей).
	TArray<UTexture*> Used;
	Mat->GetUsedTextures(Used);
	for (UTexture* T : Used)
	{
		if (UTexture2D* T2 = Cast<UTexture2D>(T))
		{
			return T2;
		}
	}
	return nullptr;
}

// ════════════════════════════════════════════════════════════════════════════
//  Вьюпорт-клиент: орбита, автовращение модели и вращение света
// ════════════════════════════════════════════════════════════════════════════
// ★ Волна 3 п.15: общие настройки клиента и тик мира превью — в базе FAstoryPreviewViewportClient
//   (без отдельного прохода прозрачности, как было); здесь только автовращение модели и света.
class FAsdaNormalStudioClient : public FAstoryPreviewViewportClient
{
public:
	FAsdaNormalStudioClient(FAdvancedPreviewScene* InScene, const TSharedRef<SAsdaNormalStudioViewport>& InViewport)
		: FAstoryPreviewViewportClient(InScene, StaticCastSharedRef<SEditorViewport>(InViewport),
			FVector(0.f, -180.f, 70.f), FRotator(-12.f, 90.f, 0.f), /*bSeparateTranslucency*/ false)
		, ViewportWidget(InViewport)
	{
	}

	bool  bAutoRotate = false;
	bool  bLightSpin = false;
	float RotSpeed = 30.f;        // град/с — модель
	float LightSpeed = 40.f;      // град/с — свет
	float Yaw = 0.f;

protected:
	virtual void PostWorldTick(float DeltaSeconds) override
	{
		TSharedPtr<SAsdaNormalStudioViewport> W = ViewportWidget.Pin();
		if (!W.IsValid())
		{
			return;
		}
		if (bAutoRotate)
		{
			Yaw = FMath::Fmod(Yaw + RotSpeed * DeltaSeconds, 360.f);
			W->ApplySpin(Yaw);
		}
		if (bLightSpin)
		{
			W->StepLight(LightSpeed * DeltaSeconds);
		}
	}

private:
	TWeakPtr<SAsdaNormalStudioViewport> ViewportWidget;
};

// ════════════════════════════════════════════════════════════════════════════
void SAsdaNormalStudioViewport::Construct(const FArguments& InArgs)
{
	// Сцена, пол (виден), SEditorViewport и компоненты-носители — общая база (тот же порядок, что был здесь).
	ConstructPreviewScene(true);

	SetLightAngles(LightYaw, LightPitch);
}

SAsdaNormalStudioViewport::~SAsdaNormalStudioViewport()
{
	Client.Reset();
	// PreviewScene сбрасывает деструктор базы SAstoryPreviewViewport — сразу после этого.
}

TSharedRef<FEditorViewportClient> SAsdaNormalStudioViewport::MakeEditorViewportClient()
{
	Client = MakeShared<FAsdaNormalStudioClient>(PreviewScene.Get(), SharedThis(this));
	return Client.ToSharedRef();
}

void SAsdaNormalStudioViewport::ClearPreview()
{
	Mids.Reset();
	SlotsWithMaps = SlotsTotal = 0;
	// ★★★ Смена меша НЕ сбрасывает переопределения материалов компонента: они переживают
	//   SetStaticMesh/SetSkeletalMesh. Без явной очистки следующая модель читала альбедо из
	//   материала ПРЕДЫДУЩЕЙ — и вся категория показывалась с текстурой первой открытой модели.
	if (StaticComp)
	{
		StaticComp->EmptyOverrideMaterials();
		StaticComp->SetStaticMesh(nullptr);
	}
	if (SkelComp)
	{
		SkelComp->EmptyOverrideMaterials();
		SkelComp->SetSkeletalMesh(nullptr);
	}
}

void SAsdaNormalStudioViewport::SetPreview(UStaticMesh* StaticMesh, USkeletalMesh* SkeletalMesh,
	bool bAlphaIsSpecular)
{
	ClearPreview();
	Settings.bAlphaIsSpecular = bAlphaIsSpecular;

	if (StaticMesh && StaticComp)
	{
		StaticComp->SetStaticMesh(StaticMesh);
		StaticComp->SetVisibility(true);
		BuildMaterials(StaticComp, bAlphaIsSpecular);
	}
	if (SkeletalMesh && SkelComp)
	{
		SkelComp->SetSkeletalMesh(SkeletalMesh);
		SkelComp->SetVisibility(true);
		BuildMaterials(SkelComp, bAlphaIsSpecular);
	}
	if (StaticComp)
	{
		StaticComp->SetVisibility(StaticMesh != nullptr);
	}
	if (SkelComp)
	{
		SkelComp->SetVisibility(SkeletalMesh != nullptr);
	}
	PushSettingsToMaterials();
	FocusOnPreview();
}

void SAsdaNormalStudioViewport::BuildMaterials(UMeshComponent* Comp, bool bAlphaIsSpecular)
{
	if (!Comp)
	{
		return;
	}
	// ★ Читать альбедо надо из РОДНЫХ материалов ассета, поэтому свои подстановки сначала снимаем
	//   (иначе источником станет материал, который мы же и поставили в прошлый раз).
	Comp->EmptyOverrideMaterials();

	UMaterialInterface* Master = LoadObject<UMaterialInterface>(nullptr,
		bAlphaIsSpecular ? kMatChar : kMatObject);
	if (!Master)
	{
		// Мастеров нет — покажем модель её родными материалами, чтобы вкладка не была пустой.
		UE_LOG(LogTemp, Warning, TEXT("[Студия нормалей] нет материала предпросмотра %s — запустите Tools/NormalStudio/ue_build_materials.py"),
			bAlphaIsSpecular ? kMatChar : kMatObject);
		return;
	}

	const int32 Num = Comp->GetNumMaterials();
	for (int32 Slot = 0; Slot < Num; ++Slot)
	{
		++SlotsTotal;
		UTexture2D* Albedo = FindAlbedo(Comp->GetMaterial(Slot));
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Master, Comp);
		if (!Mid)
		{
			continue;
		}
		if (Albedo)
		{
			Mid->SetTextureParameterValue(TEXT("Tex"), Albedo);
			// Сначала ключ по пути (он уникален); если карт под ним нет — старый ключ по имени,
			// чтобы уже сгенерированные наборы продолжали находиться.
			FString Safe = MapKeyFor(Albedo);
			if (!FPackageName::DoesPackageExist(FString::Printf(TEXT("%s/%s_N"), kMapsPath, *Safe)))
			{
				const FString ByName = SafeMapName(Albedo->GetName());
				if (FPackageName::DoesPackageExist(FString::Printf(TEXT("%s/%s_N"), kMapsPath, *ByName)))
				{
					Safe = ByName;
				}
			}
			UTexture2D* NrmMap = LoadObject<UTexture2D>(nullptr,
				*FString::Printf(TEXT("%s/%s_N.%s_N"), kMapsPath, *Safe, *Safe));
			UTexture2D* MaskMap = LoadObject<UTexture2D>(nullptr,
				*FString::Printf(TEXT("%s/%s_RE.%s_RE"), kMapsPath, *Safe, *Safe));
			// ★ Базовый цвет без нарисованного света: старое альбедо несёт тени художника, из-за
			//   которых поверхность не отзывается на поворот источника.
			UTexture2D* ColorMap = LoadObject<UTexture2D>(nullptr,
				*FString::Printf(TEXT("%s/%s_C.%s_C"), kMapsPath, *Safe, *Safe));
			if (ColorMap)
			{
				Mid->SetTextureParameterValue(TEXT("TexC"), ColorMap);
			}
			else
			{
				Mid->SetScalarParameterValue(TEXT("DelightAmount"), 0.f);
			}
			if (NrmMap)
			{
				Mid->SetTextureParameterValue(TEXT("TexN"), NrmMap);
			}
			if (MaskMap)
			{
				Mid->SetTextureParameterValue(TEXT("TexRE"), MaskMap);
			}
			if (NrmMap && MaskMap)
			{
				++SlotsWithMaps;
			}
			else
			{
				// Карт нет — слот показывается «как сейчас»: нормаль плоская, шероховатость-константа.
				Mid->SetScalarParameterValue(TEXT("HasMaps"), 0.f);
			}
		}
		Comp->SetMaterial(Slot, Mid);
		Mids.Add(Mid);
	}
}

void SAsdaNormalStudioViewport::ApplySettings(const FAsdaNsSettings& InSettings)
{
	const bool bAlphaChanged = (InSettings.bAlphaIsSpecular != Settings.bAlphaIsSpecular);
	Settings = InSettings;
	if (bAlphaChanged)
	{
		// Прозрачность и «альфа = блик» — разные мастера (режим смешивания в MID не меняется),
		// поэтому пересобираем материалы целиком.
		Mids.Reset();
		SlotsWithMaps = SlotsTotal = 0;
		if (StaticComp && StaticComp->GetStaticMesh())
		{
			BuildMaterials(StaticComp, Settings.bAlphaIsSpecular);
		}
		if (SkelComp && SkelComp->GetSkeletalMeshAsset())
		{
			BuildMaterials(SkelComp, Settings.bAlphaIsSpecular);
		}
	}
	PushSettingsToMaterials();
}

void SAsdaNormalStudioViewport::PushSettingsToMaterials()
{
	for (UMaterialInstanceDynamic* Mid : Mids)
	{
		if (!Mid)
		{
			continue;
		}
		Mid->SetScalarParameterValue(TEXT("NormalStrength"), Settings.NormalStrength);
		Mid->SetScalarParameterValue(TEXT("RoughFromMap"), Settings.RoughFromMap);
		Mid->SetScalarParameterValue(TEXT("RoughConst"), Settings.RoughConst);
		Mid->SetScalarParameterValue(TEXT("EmissiveGain"), Settings.EmissiveGain);
		Mid->SetScalarParameterValue(TEXT("SpecScale"), Settings.SpecScale);
		Mid->SetScalarParameterValue(TEXT("DelightAmount"), Settings.Delight);
		Mid->SetScalarParameterValue(TEXT("AoAmount"), Settings.AoAmount);
	}
}

// ── свет превью-сцены ───────────────────────────────────────────────────────
void SAsdaNormalStudioViewport::SetLightAngles(float Yaw, float Pitch)
{
	LightYaw = Yaw;
	LightPitch = Pitch;
	if (PreviewScene.IsValid())
	{
		PreviewScene->SetLightDirection(FRotator(LightPitch, LightYaw, 0.f));
	}
}

void SAsdaNormalStudioViewport::StepLight(float DeltaDegrees)
{
	SetLightAngles(FMath::Fmod(LightYaw + DeltaDegrees + 180.f, 360.f) - 180.f, LightPitch);
}

void SAsdaNormalStudioViewport::SetLightBrightness(float Value)
{
	if (PreviewScene.IsValid())
	{
		PreviewScene->SetLightBrightness(Value);
	}
}

void SAsdaNormalStudioViewport::SetSkyBrightness(float Value)
{
	if (PreviewScene.IsValid())
	{
		PreviewScene->SetSkyBrightness(Value);
	}
}

void SAsdaNormalStudioViewport::SetShowFloor(bool bVisible)
{
	if (PreviewScene.IsValid())
	{
		PreviewScene->SetFloorVisibility(bVisible);
	}
}

void SAsdaNormalStudioViewport::SetShowEnvironment(bool bVisible)
{
	if (PreviewScene.IsValid())
	{
		PreviewScene->SetEnvironmentVisibility(bVisible);
	}
}

void SAsdaNormalStudioViewport::SetAutoRotate(bool bEnable)
{
	if (Client.IsValid())
	{
		Client->bAutoRotate = bEnable;
	}
}

void SAsdaNormalStudioViewport::SetLightSpin(bool bEnable)
{
	if (Client.IsValid())
	{
		Client->bLightSpin = bEnable;
	}
}

void SAsdaNormalStudioViewport::ApplySpin(float YawDegrees)
{
	const FTransform T(FRotator(0.f, YawDegrees, 0.f));
	if (StaticComp)
	{
		StaticComp->SetRelativeTransform(T);
	}
	if (SkelComp)
	{
		SkelComp->SetRelativeTransform(T);
	}
}

void SAsdaNormalStudioViewport::FocusOnPreview()
{
	if (!Client.IsValid())
	{
		return;
	}
	FBoxSphereBounds Bounds(ForceInit);
	bool bHas = false;
	if (StaticComp && StaticComp->GetStaticMesh())
	{
		Bounds = StaticComp->Bounds;
		bHas = true;
	}
	if (SkelComp && SkelComp->GetSkeletalMeshAsset())
	{
		Bounds = bHas ? Bounds + SkelComp->Bounds : SkelComp->Bounds;
		bHas = true;
	}
	if (!bHas || Bounds.SphereRadius <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	Client->FocusViewportOnBox(Bounds.GetBox(), /*bInstant*/ true);
}
