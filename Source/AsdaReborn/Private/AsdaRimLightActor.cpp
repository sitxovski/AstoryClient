// Copyright (c) 2026 @sitxovski (Discord)
#include "AsdaRimLightActor.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "UObject/ConstructorHelpers.h"
#include "HAL/IConsoleManager.h"     // FAutoConsoleVariableRef asda.Rim.*
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaRim, Log, All);

// имена параметров коллекции (asda_build_toon_shaders.py); RimWidth = сила («RimStrength» стола).
//   file-static, не члены класса: несколько static-членов в одном объявлении экспортируемого класса дают C2487 (MSVC).
static const FName NameEnable(TEXT("RimEnable"));
static const FName NamePower(TEXT("RimPower"));
static const FName NameWidth(TEXT("RimWidth"));
static const FName NameColor(TEXT("RimColor"));

// ★ Глобальные рычаги рима (05.09.2026, dev-меню Ctrl+N → «Персонажи»). Значения актора — авторские ПЕР-КАРТОЧНЫЕ;
//   ключи лишь накладываются поверх в Apply(): RimEnable = bEnable && asda.Rim.Enable; RimWidth = RimStrength × asda.Rim.Scale.
//   Карта без актора рима не получает (RimEnable 0 в дефолтах коллекции) — ключ Enable=1 её не включает (цвета нет).
//   Смена на лету переприменяет все акторы рима мира. Регистрации единственные в Source/ (grep "asda.Rim." = 0 до правки).
static int32 GAsdaRimEnable = 1;   // asda.Rim.Enable: 1 = как задано актором карты, 0 = рим выключен везде
static float GAsdaRimScale  = 1.f; // asda.Rim.Scale: множитель RimStrength (0.5 / 1 / 1.5)
// ★★★ 09.11.2026 [ОРИГИНАЛ] dungeon_mob_purple — жалоба владельца «мобы в данжах фиолетовые, с фиолетовым свечением; должны быть
//   в основном цвете». Причина — рим порта (сет D 05.09, [ПОРТ]): акторы Rim_castle_of_chess / Rim_castle_of_chaos / Rim_nightvalley
//   кладут в Emissive мастеров персонажа (персонажи, NPC, мобы) обводку цветом тумана карты (chess 0.15,0.52,1.00 — голубой); на красной
//   доске это даёт сиреневый налёт и фиолетовый контур. В шейдерах оригинала рима нет: свет скина = c94 + c87·max(N·c95,0)
//   (CRenderer_SetDirLight@0x1124F60, render_core.c:41396: c95 = −направление, c94 = цвет). Цвет карты (dword_2BCD204 = ambient
//   .mapraw) ставится в начале кадра (render_core.c:810) и для анимированных объектов карты (:1393 objApplyDirLight); перед
//   проходами списков персонажей (sub_93C9E2) и мобов (g_pMobMgr, sub_9256E3) sceneSetDirLight@0x979F50 (:2022-2041) ставит
//   c94 = 0.8 серый — :1402→6420-6430, :4776, :5283, :6457/6476. Остальной свет карты (солнце, небо, огни) порт оставляет как есть:
//   моб принимает тон карты вместе с окружением. Разложение кадров (asda.SelfTest.MobFx exec=): выключение рима убирает
//   фиолетовый; туман, локальные огни и небесный свет вблизи цвет моба не меняют.
//   1 (умолч.) = рима нет ни на одной карте (как в оригинале); 0 = прежнее поведение (рим по актору Rim_<map> и asda.Rim.Enable).
//   Карты не правятся (акторы Rim_* остаются в уровнях как есть) — свет и уровни не трогаем.
static int32 GAsdaRimOriginalOff = 1;
static void OnAsdaRimChanged(IConsoleVariable*)
{
	int32 Count = 0;   // сколько акторов переприменено (в лог)
	for (TObjectIterator<AAsdaRimLightActor> It; It; ++It)
	{
		if (!*It || It->IsTemplate() || !It->GetWorld()) { continue; }
		It->Apply();
		++Count;
	}
	UE_LOG(LogTemp, Log, TEXT("[AsdaRim] asda.Rim.Enable=%d asda.Rim.Scale=%.2f без рима (оригинал)=%d — переприменено акторов: %d"), GAsdaRimEnable, GAsdaRimScale, GAsdaRimOriginalOff, Count);
}
static FAutoConsoleVariableRef CVarAsdaRimEnable(
	TEXT("asda.Rim.Enable"), GAsdaRimEnable,
	TEXT("1 = рим персонажей как задан актором Rim_<map>, 0 = выключить рим на всех картах. Смена на лету переприменяет акторы."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaRimChanged), ECVF_Default);
static FAutoConsoleVariableRef CVarAsdaRimScale(
	TEXT("asda.Rim.Scale"), GAsdaRimScale,
	TEXT("Множитель силы рима (RimStrength актора × Scale → RimWidth коллекции MPC_AsdaToon), 1 = авторское значение карты."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaRimChanged), ECVF_Default);
static FAutoConsoleVariableRef CVarAsdaRimOriginalOff(
	TEXT("asda.Rim.OriginalOff"), GAsdaRimOriginalOff,
	TEXT("[ОРИГИНАЛ] 1 = рима персонажей и мобов нет ни на одной карте, как в оригинале (мобы в данжах в своём цвете, без фиолетовой обводки); "
	     "0 = прежнее поведение: рим по актору Rim_<map> и asda.Rim.Enable. Смена на лету переприменяет акторы."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaRimChanged), ECVF_Default);

AAsdaRimLightActor::AAsdaRimLightActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	// Коллекция по умолчанию — существующая MPC_AsdaToon (рим-параметры заведены там с 17.08, asda_build_toon_shaders.py)
	static ConstructorHelpers::FObjectFinder<UMaterialParameterCollection> Mpc(TEXT("/Game/Materials/MPC_AsdaToon.MPC_AsdaToon"));
	if (Mpc.Succeeded()) { Collection = Mpc.Object; }
}

void AAsdaRimLightActor::OnConstruction(const FTransform&)
{
	Apply();
}

void AAsdaRimLightActor::BeginPlay()
{
	Super::BeginPlay();
	Apply();
}

#if WITH_EDITOR
void AAsdaRimLightActor::PostEditChangeProperty(FPropertyChangedEvent& E)
{
	Super::PostEditChangeProperty(E);
	Apply();
}
#endif

void AAsdaRimLightActor::Apply()
{
	UWorld* W = GetWorld();
	if (!W || !Collection) { return; }
	// Значения — в экземпляр коллекции ЭТОГО мира (per-world; дефолты ассета не трогаются: RimEnable 0 у карт без актора)
	// Поверх авторских значений — глобальные ключи dev-меню (asda.Rim.Enable / asda.Rim.Scale, см. выше).
	// ★ 09.11.2026: asda.Rim.OriginalOff 1 (умолч.) — рим выключен на всех картах, как в оригинале (см. объявление ключа).
	UKismetMaterialLibrary::SetScalarParameterValue(W, Collection, NameEnable, (bEnable && GAsdaRimEnable != 0 && GAsdaRimOriginalOff == 0) ? 1.0f : 0.0f);
	UKismetMaterialLibrary::SetScalarParameterValue(W, Collection, NamePower, RimPower);
	UKismetMaterialLibrary::SetScalarParameterValue(W, Collection, NameWidth, RimStrength * GAsdaRimScale);
	UKismetMaterialLibrary::SetVectorParameterValue(W, Collection, NameColor, RimColor);
	// Самопроверка ИЗ ЭКЗЕМПЛЯРА (в лог -game — по нему снимается приёмка)
	const float GotEnable = UKismetMaterialLibrary::GetScalarParameterValue(W, Collection, NameEnable);
	const float GotWidth = UKismetMaterialLibrary::GetScalarParameterValue(W, Collection, NameWidth);
	const FLinearColor GotColor = UKismetMaterialLibrary::GetVectorParameterValue(W, Collection, NameColor);
	// ★ review_setD.md ВАЖНО №1 (05.09.2026): GetActorLabel() объявлен под #if WITH_EDITOR (Actor.h) — модуль AsdaReborn
	//   Runtime, упакованная сборка с ним не компилируется. В редакторе печатаем метку (Rim_<map>), в игре — имя объекта.
#if WITH_EDITOR
	const FString Label = GetActorLabel();
#else
	const FString Label = GetName();
#endif
	UE_LOG(LogAsdaRim, Log, TEXT("AsdaRim %s: %s → RimEnable=%.0f RimWidth=%.3f RimPower=%.2f RimColor=(%.2f,%.2f,%.2f) [мир %s]"),
		*Label, *Collection->GetName(), GotEnable, GotWidth, RimPower, GotColor.R, GotColor.G, GotColor.B, *W->GetName());
}
