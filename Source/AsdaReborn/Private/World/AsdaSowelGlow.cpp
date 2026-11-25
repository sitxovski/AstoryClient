// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ПОЛОСЫ «СОВЕЛА» — ОБВОДКА ОРУЖИЯ И ЩИТА СО ВСТАВЛЕННЫМ САМОЦВЕТОМ (05.11.2026). [ОРИГИНАЛ]
//   Эталон `itemSetupSowelGlow@0x9ED480` (RE/etalon/item_inventory.c:2782):
//     • ОРУЖИЕ — экип-слот 9 СВОЕГО персонажа (`CCharacter+1588+57*9`): в гнезде 0 (инстанс +16, i16) самоцвет
//       (код >= g_SowelCodeRangeLo и запись совела есть) И заточка (инстанс +24) >= 6. Цвет по классу самоцвета
//       `SowelRecord+0x25` (у нас `FAsdaSowellRow::PartGrade`): 0 синий, 1 зелёный, 2 (1, 0.6, 0), 3 красный, прочее белый.
//       Параметры — `Math_LerpClamp` по заточке 6…10: размер 1.8→0.2, альфа 0.05→0.3, длительность 1500→2000 мс,
//       ключ альфы 300→400 мс. Проход — `pbbBuildStripBatch@0x11C15F0` по части оружия (`pModelPartArray+3144`).
//     • ЩИТ — экип-слот 8 (`+2044`): самоцвет в гнезде 0, заточка >= 6, цвет ВСЕГДА зелёный, те же формулы на 6…11,
//       часть щита (`+3668`).
//     • Иначе `sub_9217AA` снимает проход с части.
//   `pbbBuildStripBatch` = второй проход той же геометрии, выдавленной по нормали (обводка): толщина a5→a6 за a10 мс,
//   альфа a7→a8 к a11 мс → a9 к концу, удержание a12, затем обратный ход (`uiTweenUpdatePhase@0x1250ED0`, 4 фазы).
//   Здесь: толщина 0→размер, альфа 0→альфа→0, удержание 0. Видимость — флаг частей оружия `char+208544` (`char[52136]`),
//   у нас — видимость самой части (рыбалка, транспорт, скрытия).
//   Вызывается ТОЛЬКО для своего персонажа: `charEquipApplyPartsModelByType@0xC280B0`, `charRefreshEquipmentParts@0xC46940`,
//   `invApplySlotSwapResult@0xD0FF10` (надеть/снять), `netRecvSowelActionResult@0xD0F7D0` (вставка/извлечение самоцвета).
//   Соседи полос не видят — 4005/4021 несут только код предмета и заточку.
//   Порт: тот же приём, что у обводки квестовой метки NPC (`M_AsdaQuestMarkOutline`: Thickness/OutlineAlpha/OutlineColor,
//   `AAsdaNpcActor::TickQuestMarkOutline`), — дочерний `UStaticMeshComponent` того же меша на части.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "World/AsdaPlayerCharacter.h"
#include "Characters/AsdaCharAssembly.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaSowelSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSowelGlow, Log, All);

static ASDA_DEV_CVAR(int32, CVarAsdaCharSowelGlow,
	TEXT("asda.Char.SowelGlow"), 1,
	TEXT("[ОРИГИНАЛ] 1 — у своего оружия и щита с самоцветом в гнезде 0 и заточкой >= 6 пульсирует цветная обводка "
	     "(itemSetupSowelGlow); 0 — как было (обводки нет)."),
	ECVF_Default);

// ★ [ИСПРАВЛЕНИЕ] (18.11.2026, баг «ружьё мигает синим»): ЯРКОСТЬ ОБВОДКИ В ГАММЕ, КАК У ОРИГИНАЛА.
//   Оригинал: D3D9 SRCALPHA/ONE в кадре БЕЗ гаммы — к значению экрана прибавляется цвет × альфа (синий 1 × 0.3 = +0.3).
//   Порт клал то же 0.3 в ЛИНЕЙНЫЙ кадр UE: на экране это +0.58, ствол заливало насыщенным синим.
//   Теперь в материал идёт линейный эквивалент гамма-прибавки поверх чёрного: FromSRGB(цвет × альфа), альфа 1
//   (тот же перевод, что у свечения заточки, `asda.Fx.GlowVertexColorLinear`). 0 — как было.
static ASDA_DEV_CVAR(int32, CVarAsdaCharSowelGlowGamma,
	TEXT("asda.Char.SowelGlowGamma"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 — яркость обводки совела переводится из гаммы оригинала в линейное (цвет × альфа как sRGB); "
	     "0 — как было (альфа прямо в линейный кадр, обводка в 2–4 раза ярче)."),
	ECVF_Default);

// Включена ли обводка совела (ключ `asda.Char.SowelGlow`).
bool AsdaSowelGlowOn() { return CVarAsdaCharSowelGlow.GetValueOnGameThread() != 0; }

namespace AsdaSowelGlow
{
	static const TCHAR* kMaster = TEXT("/Game/Materials/M_AsdaQuestMarkOutline.M_AsdaQuestMarkOutline");   // обводка (как у метки NPC)
	static const FName kTag(TEXT("AsdaSowelGlow"));   // тег компонента обводки

	// Одна обводка на части: компонент, его родитель-часть и параметры tween.
	struct FStrip
	{
		TWeakObjectPtr<UStaticMeshComponent> Comp;     // обводка
		TWeakObjectPtr<UStaticMeshComponent> Part;     // меш оружия/щита
		TWeakObjectPtr<UMaterialInstanceDynamic> MID;  // её материал
		FLinearColor Gamma = FLinearColor::White;      // цвет оригинала (гамма), для перевода цвет × альфа
		float Size = 0.f;      // толщина в конце нарастания (a6)
		float Alpha = 0.f;     // альфа в ключе (a8)
		float RampMs = 0.f;    // длительность нарастания (a10)
		float MidMs = 0.f;     // время ключа альфы (a11)
		float TimeMs = 0.f;    // время в фазе
		int32 Phase = 0;       // 0 нарастание, 1 удержание, 2 спад, 3 удержание
	};
	// Обводки по носителям (слабые ключи).
	static TMap<TWeakObjectPtr<AAsdaPlayerCharacter>, TArray<FStrip>> GStrips;
	// Подписка на «самоцвет вставлен/извлечён» (`netRecvSowelActionResult`).
	static FDelegateHandle GDirtyHandle;
	static TWeakObjectPtr<UAsdaSowelSubsystem> GDirtySource;

	// Снять обводки носителя.
	static void Clear(AAsdaPlayerCharacter* Ch)
	{
		if (!Ch) { return; }
		TInlineComponentArray<UStaticMeshComponent*> Comps(Ch);
		for (UStaticMeshComponent* C : Comps)
		{
			if (IsValid(C) && C->ComponentHasTag(kTag)) { C->DestroyComponent(); }
		}
		GStrips.Remove(TWeakObjectPtr<AAsdaPlayerCharacter>(Ch));
	}

	// Меш части по тегу.
	static UStaticMeshComponent* PartOf(AAsdaPlayerCharacter* Ch, const FName& Tag)
	{
		TInlineComponentArray<UStaticMeshComponent*> Comps(Ch);
		for (UStaticMeshComponent* C : Comps)
		{
			if (IsValid(C) && C->ComponentHasTag(Tag) && C->GetStaticMesh()) { return C; }
		}
		return nullptr;
	}

	// Цвет по классу самоцвета в гамме оригинала (эталон: switch по partGrade[0]).
	static FLinearColor GammaOfGrade(int32 PartGrade)
	{
		switch (PartGrade)
		{
		case 0: return FLinearColor(0.f, 0.f, 1.f);
		case 1: return FLinearColor(0.f, 1.f, 0.f);
		case 2: return FLinearColor(1.f, 0.6f, 0.f);   // 1059095314 = 0.6f
		case 3: return FLinearColor(1.f, 0.f, 0.f);
		default: return FLinearColor(1.f, 1.f, 1.f);
		}
	}

	// Цвет и альфа в материал: с ключом `asda.Char.SowelGlowGamma` — линейный эквивалент гамма-прибавки «цвет × альфа».
	static void SetColorAlpha(UMaterialInstanceDynamic* MID, const FLinearColor& Gamma, float Alpha)
	{
		if (CVarAsdaCharSowelGlowGamma.GetValueOnGameThread() != 0)
		{
			const FLinearColor G(Gamma.R * Alpha, Gamma.G * Alpha, Gamma.B * Alpha);
			MID->SetVectorParameterValue(TEXT("OutlineColor"), FLinearColor::FromSRGBColor(G.ToFColor(false)));
			MID->SetScalarParameterValue(TEXT("OutlineAlpha"), 1.f);
			return;
		}
		MID->SetVectorParameterValue(TEXT("OutlineColor"), FLinearColor::FromSRGBColor(Gamma.ToFColor(false)));
		MID->SetScalarParameterValue(TEXT("OutlineAlpha"), Alpha);
	}

	// `Math_LerpClamp(outMin, outMax, inMin, inVal, inMax)` для возрастающего диапазона.
	static float LerpClamp(float OutMin, float OutMax, float InMin, float InVal, float InMax)
	{
		if (InVal <= InMin) { return OutMin; }
		if (InVal >= InMax) { return OutMax; }
		return (OutMax - OutMin) * (InVal - InMin) / (InMax - InMin) + OutMin;
	}

	// Поставить обводку на часть.
	static bool Build(AAsdaPlayerCharacter* Ch, UStaticMeshComponent* Part, const FLinearColor& Color, int32 Grade, float GradeHi)
	{
		UMaterialInterface* Master = LoadObject<UMaterialInterface>(nullptr, kMaster, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Part || !Master) { return false; }
		const float G = (float)Grade;
		FStrip S;
		S.Size   = LerpClamp(1.8f, 0.2f, 6.f, G, GradeHi);
		S.Alpha  = LerpClamp(0.05f, 0.3f, 6.f, G, GradeHi);
		S.RampMs = LerpClamp(1500.f, 2000.f, 6.f, G, GradeHi);
		S.MidMs  = LerpClamp(300.f, 400.f, 6.f, G, GradeHi);
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Ch, NAME_None, RF_Transient);
		C->SetStaticMesh(Part->GetStaticMesh());
		C->SetupAttachment(Part);
		C->SetRelativeTransform(FTransform::Identity);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->ComponentTags.Add(kTag);
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Master, C);
		S.Gamma = Color;
		SetColorAlpha(MID, S.Gamma, 0.f);
		MID->SetScalarParameterValue(TEXT("Thickness"), 0.f);
		for (int32 k = 0; k < FMath::Max(1, C->GetNumMaterials()); ++k) { C->SetMaterial(k, MID); }
		C->RegisterComponent();
		S.Comp = C; S.Part = Part; S.MID = MID;
		GStrips.FindOrAdd(TWeakObjectPtr<AAsdaPlayerCharacter>(Ch)).Add(S);
		return true;
	}
}

// Пересобрать обводки своего персонажа по надетому (контейнер 3) — зовётся после каждой пересборки модели своего.
void AsdaSowelGlowRefresh(AAsdaPlayerCharacter* Ch)
{
	using namespace AsdaSowelGlow;
	if (!Ch) { return; }
	Clear(Ch);
	UGameInstance* GI = Ch->GetGameInstance();
	if (!GI) { return; }
	// Вставка/извлечение самоцвета — тот же вход (`netRecvSowelActionResult` зовёт itemSetupSowelGlow).
	if (UAsdaSowelSubsystem* SW = GI->GetSubsystem<UAsdaSowelSubsystem>(); SW && GDirtySource.Get() != SW)
	{
		if (UAsdaSowelSubsystem* Old = GDirtySource.Get()) { Old->OnSowelGlowDirty.Remove(GDirtyHandle); }
		GDirtySource = SW;
		const TWeakObjectPtr<UGameInstance> WGI(GI);
		GDirtyHandle = SW->OnSowelGlowDirty.AddLambda([WGI]()
		{
			UGameInstance* G = WGI.Get();
			UAsdaWorldSubsystem* WS = G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
			if (AAsdaPlayerCharacter* Me = WS ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get()) : nullptr) { AsdaSowelGlowRefresh(Me); }
		});
	}
	if (!AsdaSowelGlowOn()) { return; }
	const UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>();
	const UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>();
	if (!Inv || !IT) { return; }
	// Слот 9 — цвет по классу самоцвета, диапазон 6…10; слот 8 — зелёный, 6…11.
	for (const int32 Slot : { 9, 8 })
	{
		const FAsdaItemInstance* I = Inv->GetCell(3, Slot);
		if (!I || I->IsEmpty() || I->Sowel0 <= 0 || !IT->IsSowelCode(I->Sowel0)) { continue; }
		FAsdaSowellRow Row;
		if (!IT->GetSowell(I->Sowel0, Row)) { continue; }
		if (I->GemGrade < 6) { continue; }
		UStaticMeshComponent* Part = PartOf(Ch, Slot == 9 ? UAsdaCharAssembly::WeaponMeshTag : UAsdaCharAssembly::ShieldMeshTag);
		if (!Part) { continue; }
		const FLinearColor Color = (Slot == 9) ? GammaOfGrade(Row.PartGrade) : GammaOfGrade(1);   // гамма; в материал — SetColorAlpha
		if (Build(Ch, Part, Color, I->GemGrade, Slot == 9 ? 10.f : 11.f))
		{
			UE_LOG(LogAsdaSowelGlow, Log, TEXT("[совел] %s: обводка слота %d (предмет %d +%d, самоцвет %d класс %d)"),
				*Ch->GetName(), Slot, I->ItemCode, (int32)I->GemGrade, I->Sowel0, Row.PartGrade);
		}
	}
}

// Покадрово: tween обводки (4-фазный ping-pong эталона) и видимость по части.
void AsdaSowelGlowTick(AAsdaPlayerCharacter* Ch, float DeltaSeconds)
{
	using namespace AsdaSowelGlow;
	TArray<FStrip>* L = Ch ? GStrips.Find(TWeakObjectPtr<AAsdaPlayerCharacter>(Ch)) : nullptr;
	if (!L) { return; }
	for (FStrip& S : *L)
	{
		UStaticMeshComponent* C = S.Comp.Get();
		UStaticMeshComponent* Part = S.Part.Get();
		UMaterialInstanceDynamic* MID = S.MID.Get();
		if (!C || !MID || !Part || Part->GetAttachParent() == nullptr)
		{
			AsdaSowelGlowRefresh(Ch);   // часть пересобрали мимо общей точки — обводку строим заново
			return;
		}
		const bool bVis = AsdaSowelGlowOn() && Part->IsVisible() && !Part->bHiddenInGame && !Ch->IsHidden();
		if (C->IsVisible() != bVis) { C->SetVisibility(bVis); }
		if (!bVis) { continue; }
		S.TimeMs += DeltaSeconds * 1000.f;
		for (int32 Guard = 0; Guard < 8; ++Guard)
		{
			const float PhaseMs = (S.Phase == 1 || S.Phase == 3) ? 0.f : S.RampMs;   // удержание a12 = 0
			if (S.TimeMs < PhaseMs) { break; }
			S.TimeMs -= PhaseMs;
			S.Phase = (S.Phase + 1) & 3;
		}
		auto AlphaAt = [&S](float T) -> float
		{
			return (T < S.MidMs) ? FMath::Lerp(0.f, S.Alpha, T / FMath::Max(S.MidMs, 1.f))
			                     : FMath::Lerp(S.Alpha, 0.f, (T - S.MidMs) / FMath::Max(S.RampMs - S.MidMs, 1.f));
		};
		const float T = (S.Phase == 2) ? FMath::Clamp(S.RampMs - S.TimeMs, 0.f, S.RampMs) : FMath::Clamp(S.TimeMs, 0.f, S.RampMs);
		const float Thick = FMath::Lerp(0.f, S.Size, T / FMath::Max(S.RampMs, 1.f));
		MID->SetScalarParameterValue(TEXT("Thickness"), Thick);
		SetColorAlpha(MID, S.Gamma, AlphaAt(T));
	}
}

// Число живых обводок носителя (самотест).
int32 AsdaSowelGlowCount(const AAsdaPlayerCharacter* Ch)
{
	if (!Ch) { return 0; }
	int32 N = 0;
	TInlineComponentArray<UStaticMeshComponent*> Comps(Ch);
	for (const UStaticMeshComponent* C : Comps)
	{
		if (IsValid(C) && C->IsRegistered() && C->ComponentHasTag(AsdaSowelGlow::kTag) && C->IsVisible()) { ++N; }
	}
	return N;
}
