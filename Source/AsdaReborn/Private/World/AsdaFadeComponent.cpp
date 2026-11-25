// Copyright (c) 2026 @sitxovski (Discord)
// Реализация UAsdaFadeComponent — см. AsdaFadeComponent.h.
#include "World/AsdaFadeComponent.h"
#include "GameFramework/Actor.h"        // B3: FindComponentByClass в статическом Begin
#include "Components/MeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Resources/AsdaMaterialNames.h"   // ★ дедуп №2 В1-C (F10/F16): пути мастеров, имена параметров, экранные ассеты
#include "World/AsdaPlayerCharacter.h"   // ★ режим «душа»: у SoulState==1 спавн-фейд не запускается (эталон)

UAsdaFadeComponent::UAsdaFadeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

// ★★ 19.09.2026 (glow_fix4): ссылки записей свапа для сборщика мусора — разбор у объявления.
void UAsdaFadeComponent::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UAsdaFadeComponent* This = CastChecked<UAsdaFadeComponent>(InThis);
	for (FSlotSwap& Sw : This->Swaps)
	{
		Collector.AddReferencedObject(Sw.Orig, This);   // исходный материал слота (вернётся по окончании)
		Collector.AddReferencedObject(Sw.Mid, This);    // фейд-MID слота
	}
	// ★ 19.09.2026 (ревизия glow_fix4, С1): и кэш базового материала фейда — в -game его не держит ничто, если
	//   свапов ещё нет (сосед заспавнен до экипа), а `AdoptSlots` создаёт из него MID.
	Collector.AddReferencedObject(This->FadeBase, This);
	Super::AddReferencedObjects(InThis, Collector);
}

// ★★★ Аудит №4 (B3): find-or-create — обоснование в шапке объявления (AsdaFadeComponent.h).
UAsdaFadeComponent* UAsdaFadeComponent::Begin(AActor* Owner, bool bIn, float Duration,
                                              bool bDestroyOwner, FName CreateName)
{
	if (!Owner) { return nullptr; }
	UAsdaFadeComponent* F = Owner->FindComponentByClass<UAsdaFadeComponent>();
	if (!F)
	{
		F = NewObject<UAsdaFadeComponent>(Owner, CreateName);
		if (!F) { return nullptr; }
		F->RegisterComponent();
	}
	F->BeginFade(bIn, Duration, bDestroyOwner);
	return F;
}

void UAsdaFadeComponent::BeginFade(bool bIn, float Duration, bool bDestroyOwner)
{
	AActor* Owner = GetOwner();
	if (!Owner) { return; }
	// Перезапуск поверх активного фейда: вернуть исходники, собрать заново.
	RestoreMaterials();
	Swaps.Reset();
	// ★★★ [ОРИГИНАЛ] РЕЖИМ «ДУША» (SoulState==1): спавн-фейд НЕ запускается. Тик персонажа
	//   `charEntityUpdateTick@0xA1ED30 :80703` при `+2241 == 1` каждый кадр ставит α = 0.3 и в ветку
	//   `else` (спавн-фейд `alpha = fadeIn·fadeOut·visAlpha`) не заходит — душа появляется сразу с 0.3,
	//   без подъёма 0→1. Слоты такого персонажа уже стоят на translucent-MID «души»; фейд поверх них
	//   принял бы их за исходники и на 1.5 с перебил бы эталонную α. Фейд-аут (уход) не гейтим.
	if (bIn)
	{
		if (const AAsdaPlayerCharacter* PC = Cast<AAsdaPlayerCharacter>(Owner))
		{
			// ★ Причин две: «душа» (SoulState==1, эталон) и GM-«скрыться» (порт). Обе держат слоты
			//   на translucent-MID, и фейд поверх них принял бы наш MID за исходник.
			if (PC->IsTranslucencyActive())
			{
				T = -1.f;
				SetComponentTickEnabled(false);
				DestroyComponent();
				return;
			}
		}
	}
	bFadeIn = bIn;
	bDestroyOnDone = bDestroyOwner;
	Dur = FMath::Max(0.05f, Duration);
	T = 0.f;
	FadeBase = LoadObject<UMaterialInterface>(nullptr, AsdaMat::kPreviewFadePath);   // ★ В1-C F10
	if (!FadeBase)
	{
		// Без фейд-материала — мгновенный результат (не оставлять сущность навсегда).
		if (!bIn) { if (bDestroyOnDone) { Owner->Destroy(); } else { Owner->SetActorHiddenInGame(true); DestroyComponent(); } }
		else { DestroyComponent(); }
		return;
	}
	AdoptSlots(bIn ? 0.f : 1.f);
	SetComponentTickEnabled(true);
}

// ★ Снятие идущего фейд-ина — см. объявление. Фейд-аут (T>=0, !bFadeIn) оставляем как есть.
void UAsdaFadeComponent::CancelFadeIn()
{
	if (T < 0.f || !bFadeIn) { return; }   // неактивен или это фейд-аут
	T = -1.f;
	SetComponentTickEnabled(false);
	RestoreMaterials();
	Swaps.Reset();
	DestroyComponent();
}

// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-C, F9): обход и подмена слотов — общая точка `AsdaTranslucentSlots`
//   (разбор в AsdaFadeComponent.h). Отличие от прежнего тела одно: MID называется `MIDFade_<исходник>`, а не
//   безымянно — поиск секций сборки (`face`/`hair`/`chst`…) по имени материала слота работает и во время фейда.
//   Уже наш слот пропускается; материал, сменённый поверх нашего MID (поздний экип), подменяется заново.
void UAsdaFadeComponent::AdoptSlots(float Alpha)
{
	AActor* Owner = GetOwner();
	if (!Owner || !FadeBase) { return; }
	TArray<UMeshComponent*> Comps;
	Owner->GetComponents<UMeshComponent>(Comps);
	UMaterialInterface* Base = FadeBase;
	AsdaTranslucentSlots::Adopt(Comps, Swaps,
		[this, Base, Alpha](UMaterialInterface* Cur)
		{
			return AsdaTranslucentSlots::MakeSwapMid(Cur, Base, this, TEXT("MIDFade"), FName(AsdaMat::kParamFadeIn), Alpha);
		},
		[](UMaterialInterface*) { return false; });   // фейд подменяет любой слот с Tex (как прежде)
}

// ★ Дедуп №2, В2-C (F9): одна точка создания MID-подмены — разбор у объявления (AsdaFadeComponent.h).
UMaterialInstanceDynamic* AsdaTranslucentSlots::MakeSwapMid(UMaterialInterface* Cur, UMaterialInterface* Base, UObject* Outer,
	const TCHAR* Prefix, FName AlphaParam, float Alpha, TConstArrayView<const TCHAR*> CopiedScalars)
{
	if (!Cur || !Base) { return nullptr; }
	UTexture* Tex = nullptr;
	if (!Cur->GetTextureParameterValue(FName(AsdaMat::kParamTex), Tex) || !Tex) { return nullptr; }
	// Имя несёт имя исходника — `MakeUniqueObjectName`, чтобы не затереть живой объект с тем же именем у того же владельца.
	const FName MidName = MakeUniqueObjectName(Outer, UMaterialInstanceDynamic::StaticClass(),
		FName(*FString::Printf(TEXT("%s_%s"), Prefix, *Cur->GetName())));
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, Outer, MidName);
	if (!Mid) { return nullptr; }
	Mid->SetTextureParameterValue(FName(AsdaMat::kParamTex), Tex);
	for (const TCHAR* PName : CopiedScalars)
	{
		float V = 0.f;
		if (Cur->GetScalarParameterValue(FName(PName), V)) { Mid->SetScalarParameterValue(FName(PName), V); }
	}
	Mid->SetScalarParameterValue(AlphaParam, Alpha);
	return Mid;
}

void UAsdaFadeComponent::ApplyAlpha(float Alpha)
{
	for (FSlotSwap& Sw : Swaps)
	{
		if (Sw.Mid) { Sw.Mid->SetScalarParameterValue(FName(AsdaMat::kParamFadeIn), Alpha); }
	}
}

void UAsdaFadeComponent::RestoreMaterials()
{
	for (FSlotSwap& Sw : Swaps)
	{
		if (UMeshComponent* Cm = Sw.Comp.Get())
		{
			// Возвращаем исходник ТОЛЬКО если в слоте всё ещё наш MID — материал, поставленный
			//   внешним кодом после фейда, не затираем.
			if (Sw.Mid && Cm->GetMaterial(Sw.Slot) == Sw.Mid) { Cm->SetMaterial(Sw.Slot, Sw.Orig); }
		}
	}
}

void UAsdaFadeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (T < 0.f) { return; }
	T += DeltaTime;
	const float P = FMath::Clamp(T / Dur, 0.f, 1.f);
	const float A = bFadeIn ? P : 1.f - P;
	AdoptSlots(A);   // экип/крылья/транспорт, приехавшие ПОСЛЕ старта фейда, подхватывают текущую альфу
	ApplyAlpha(A);
	if (P >= 1.f)
	{
		T = -1.f;
		SetComponentTickEnabled(false);
		if (bFadeIn)
		{
			RestoreMaterials();   // видимый на исходных материалах (Lit, тени и т.д.)
			Swaps.Reset();
			DestroyComponent();
		}
		else if (AActor* Owner = GetOwner())
		{
			if (bDestroyOnDone) { Owner->Destroy(); }   // клон — деспавн завершён
			else                                        // запечённый NPC — вернуть материалы + скрыть (не уничтожать)
			{
				RestoreMaterials();
				Swaps.Reset();
				Owner->SetActorHiddenInGame(true);
				DestroyComponent();
			}
		}
	}
}
