// Copyright (c) 2026 @sitxovski (Discord)
#include "World/AsdaLightBudgetSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaView.h"   // ★ волна 3 п.11 (15.09.2026): общая точка наблюдателя (E7)
#include "World/AsdaRenderSettings.h"
#include "World/AsdaCullSettings.h"   // ★ список миров без отсечки дальности (Login, L_Loading) — один на проект
#include "World/AsdaLocalLightActor.h"    // ★ Ф9: общий ответ «отсечка дальности выключена ключом asda.Light.DrawCull»
#include "UI/AsdaDevMenuSubsystem.h"
#include "Components/LocalLightComponent.h"
#include "World/AsdaFxLightComponent.h"   // ★ Ф6: опознание эффект-света для его полосы бюджета
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Scalability.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaLightBudget, Log, All);

// ─────────────── asda.Light.ShadowBudget (регистрация единственная в Source/: grep "asda\.Light" 07.09.2026 = 0) ───────────────
// Бюджет теневых локальных светов: ≥0 = ровно столько; −1 = по уровню ([AsdaShadow@N] Light.ShadowBudget, N = sg.ShadowQuality);
// −2 = не вмешиваться. Персист [AsdaRender] (GRenderCVarKeys в AsdaDevMenuSubsystem.cpp), строка Ctrl+N «Свет и тени».
static int32 GAsdaLightShadowBudget = -1;
static void OnAsdaLightShadowBudgetChanged(IConsoleVariable*) { UAsdaLightBudgetSubsystem::MarkAllDirty(); }
static FAutoConsoleVariableRef CVarAsdaLightShadowBudget(
	TEXT("asda.Light.ShadowBudget"),
	GAsdaLightShadowBudget,
	TEXT("Сколько локальных светов (Point/Spot/Rect) оставить с тенями: -1 = по уровню sg.ShadowQuality ([AsdaShadow@N] Light.ShadowBudget: 0/2/4/8), 0..N = ровно столько, -2 = не вмешиваться (авторские)."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaLightShadowBudgetChanged),
	ECVF_Default);

// ★★★ ВОЛНА Ф6 (10.09.2026): ВТОРАЯ ПОЛОСА ТОГО ЖЕ БЮДЖЕТА — ТЕНИ ЭФФЕКТ-СВЕТОВ.
//   До Ф6 эффект-света приходили сюда с CastShadows=false и кандидатами не были вовсе (роль Glow). Теперь
//   эффект может попросить тень полем «Отбрасывает тень», и просьба идёт ЧЕРЕЗ ЭТУ ЖЕ подсистему —
//   второй бюджет не заводится сознательно: список светов, кэш, перебалансировка по дистанции и возврат
//   авторских значений уже написаны здесь, а две копии этого разошлись бы при первой же правке.
//   ПОЛОСА ОТДЕЛЬНАЯ (см. UAsdaRenderSettings::FxShadowBudgetFallback): фонари карты и эффекты не
//   конкурируют друг с другом, поэтому включение теней эффектам НЕ МОЖЕТ отобрать тень у фонаря.
static int32 GAsdaLightFxShadowBudget = -1;
static void OnAsdaLightFxShadowBudgetChanged(IConsoleVariable*) { UAsdaLightBudgetSubsystem::MarkAllDirty(); }
static FAutoConsoleVariableRef CVarAsdaLightFxShadowBudget(
	TEXT("asda.Light.FxShadowBudget"),
	GAsdaLightFxShadowBudget,
	TEXT("Сколько ЭФФЕКТ-светов оставить с тенями (отдельно от фонарей карты): -1 = по уровню sg.ShadowQuality ([AsdaShadow@N] Light.FxShadowBudget), 0..N = ровно столько, -2 = не вмешиваться."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaLightFxShadowBudgetChanged),
	ECVF_Default);

// asda.Light.Enable — A/B «с локальными светами / без» на той же карте (замер Δ мс, проверка «фонарь виден»): 0 = все ULocalLightComponent
// мира гасятся (bAffectsWorld=false, авторское значение кэшируется), 1 = вернуть авторское. Не персистится (только рантайм).
static int32 GAsdaLightEnable = 1;
static void OnAsdaLightEnableChanged(IConsoleVariable*) { UAsdaLightBudgetSubsystem::MarkAllDirty(); }
static FAutoConsoleVariableRef CVarAsdaLightEnable(
	TEXT("asda.Light.Enable"),
	GAsdaLightEnable,
	TEXT("Локальные света (Point/Spot/Rect) мира: 1 = авторские, 0 = все выключены (A/B цены и видимости фонарей). Только рантайм."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaLightEnableChanged),
	ECVF_Default);

// asda.Light.Dump — таблица всех локальных светов мира (авторские/текущие тени, сила, радиус, дальность, тег, дистанция) в лог.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld CmdAsdaLightDump(
	TEXT("asda.Light.Dump"),
	TEXT("Таблица локальных светов мира: класс, mobility, сила (кд), радиус, MaxDraw, тени авторские→текущие, тег роли, дистанция до игрока; сводка бюджета."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
	{
		if (UAsdaLightBudgetSubsystem* S = W ? W->GetSubsystem<UAsdaLightBudgetSubsystem>() : nullptr) { S->Dump(); }
		else { UE_LOG(LogAsdaLightBudget, Warning, TEXT("asda.Light.Dump: в этом мире подсистемы бюджета нет (не игровой мир)")); }
	}));
#endif // ASDA_DEV_CONSOLE

bool UAsdaLightBudgetSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer)) { return false; }
	// Только игровые миры: Game всегда; PIE — по настройке; редакторские/превью — никогда (владелец видит авторские тени)
	const UWorld* W = Cast<UWorld>(Outer);
	if (!W) { return false; }
	if (W->WorldType == EWorldType::Game) { return true; }
	if (W->WorldType == EWorldType::PIE)  { return UAsdaRenderSettings::Get()->bApplyBudgetInPIE; }
	return false;
}

// ★ волна 3 п.12 (15.09.2026): Initialize/Deinitialize — в UAsdaRenderQualitySubsystemBase (смена уровня → OnQualityLevelsChanged → MarkDirty;
//   снятие подписки на появление актора — там же, ★ доводка E-4 М-2)

void UAsdaLightBudgetSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	// ★ Доводка E-4 (М-2): подписка на появление актора — новый свет инвалидирует кэш (иначе не попадёт в бюджет до смены карты/уровня)
	BindActorSpawned(InWorld);   // ★ волна 3 п.12 (15.09.2026): подписка — в базе (вызов уходит в OnActorSpawned ниже)
	// Кэш соберётся при первом Apply; акторы ещё не прошли BeginPlay (AAsdaLocalLightActor сообщит авторские тени) — применить на первом Tick
	bCacheValid = false;
	MarkDirty();
}

void UAsdaLightBudgetSubsystem::OnActorSpawned(AActor* A)
{
	if (!A) { return; }
	// Появился актор со светом (фонарь/BP) — кэш устарел, пересобрать и переприменить бюджет на ближайшем Tick
	TInlineComponentArray<ULocalLightComponent*> Comps;
	A->GetComponents<ULocalLightComponent>(Comps);
	if (Comps.Num() > 0) { bCacheValid = false; MarkDirty(); }
}

void UAsdaLightBudgetSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bDirty)
	{
		bDirty = false;
		Apply(/*bLog*/true);
		return;
	}
	// Перебалансировка по дистанции: только если кандидатов больше бюджета и игрок сдвинулся
	const UAsdaRenderSettings* S = UAsdaRenderSettings::Get();
	if (!bNeedsRebalance || S->ShadowRebalanceIntervalSec <= 0.f) { return; }
	RebalanceTimer += DeltaTime;
	if (RebalanceTimer < S->ShadowRebalanceIntervalSec) { return; }
	RebalanceTimer = 0.f;
	if (FVector::Dist(PlayerPos(), LastPlayerPos) < S->ShadowRebalanceMoveThreshold) { return; }
	Apply(/*bLog*/false);
}

void UAsdaLightBudgetSubsystem::SetAuthoredCastShadows(ULocalLightComponent* L, bool bAuthored)
{
	if (!L) { return; }
	bool* Cur = Authored.Find(L);
	if (Cur && *Cur == bAuthored) { return; }
	Authored.Add(L, bAuthored);
	// ★ Ф6: ЭФФЕКТ-СВЕТ РОЖДАЕТСЯ ПОЗЖЕ СВОЕГО АКТОРА. Компонент навешивается на уже существующий актор
	//   эффекта (UAsdaFxLightSubsystem::RegisterEffect), поэтому подписка OnActorSpawned его НЕ ловит:
	//   в момент спавна актора локальных светов на нём ещё нет. Без этой строки просьба «хочу тень» была бы
	//   записана в кэш авторских значений, а сам компонент в список кандидатов не попал бы вовсе —
	//   и тень не появилась бы, притом молча (ровно тот класс «нет», который обязан называть причину).
	//   ★ Только на просьбе «ХОЧУ тень»: отказ ничего не ищет, а полный обход мира (TActorIterator) на каждом
	//     погасшем эффекте вернул бы ровно тот спайк GT, ради устранения которого кэш и заводился (ревизия E-4 М-2).
	if (bAuthored && bCacheValid && !CachedLights.Contains(L)) { bCacheValid = false; }
	// До применения компонент несёт авторское значение (если бюджет −2 — так и останется)
	if (L->CastShadows != bAuthored) { L->SetCastShadows(bAuthored); }
	MarkDirty();
}

int32 UAsdaLightBudgetSubsystem::CurrentShadowLevel()
{
	return ShadowQualityLevel();   // ★ волна 3 п.12 (15.09.2026): чтение уровня — в базе
}

int32 UAsdaLightBudgetSubsystem::LevelBudget(int32 Level)
{
	// [AsdaShadow@N] Light.ShadowBudget из объединённого Scalability-ini (BaseScalability + Config/DefaultScalability.ini);
	// хелпер тот же, что у меню (UAsdaRenderQualitySubsystemBase::ScalabilityLevelValue — ★ хвосты волны 3 15.09.2026: перенесён из меню) — один способ чтения уровня.
	return LevelInt(TEXT("AsdaShadow"), Level, TEXT("Light.ShadowBudget"), UAsdaRenderSettings::Get()->ShadowBudgetFallback);   // ★ волна 3 п.12 (15.09.2026)
}

int32 UAsdaLightBudgetSubsystem::ResolveBudget()
{
	if (GAsdaLightShadowBudget >= 0)  { return GAsdaLightShadowBudget; }
	if (GAsdaLightShadowBudget <= -2) { return -2; }
	return LevelBudget(CurrentShadowLevel());
}

int32 UAsdaLightBudgetSubsystem::LevelFxBudget(int32 Level)
{
	// [AsdaShadow@N] Light.FxShadowBudget — та же секция и тот же индекс (sg.ShadowQuality), что у фонарей:
	// величина про одно и то же (цену теней), и разносить её по разным секциям значило бы прятать половину настройки
	return LevelInt(TEXT("AsdaShadow"), Level, TEXT("Light.FxShadowBudget"), UAsdaRenderSettings::Get()->FxShadowBudgetFallback);   // ★ волна 3 п.12 (15.09.2026)
}

int32 UAsdaLightBudgetSubsystem::ResolveFxBudget()
{
	if (GAsdaLightFxShadowBudget >= 0)  { return GAsdaLightFxShadowBudget; }
	if (GAsdaLightFxShadowBudget <= -2) { return -2; }
	return LevelFxBudget(CurrentShadowLevel());
}

void UAsdaLightBudgetSubsystem::MarkAllDirty()
{
	ForEachInstance<UAsdaLightBudgetSubsystem>([](UAsdaLightBudgetSubsystem& S) { S.MarkDirty(); });   // ★ волна 3 п.12 (15.09.2026)
}

void UAsdaLightBudgetSubsystem::RebuildCache()
{
	// ★ Доводка E-4 (М-2): единственное место полного обхода мира — кэш заполняется здесь, дальше читается из него
	CachedLights.Reset();
	UWorld* W = GetWorld();
	if (W)
	{
		for (TActorIterator<AActor> It(W); It; ++It)
		{
			AActor* A = *It;
			if (!A || !IsValid(A)) { continue; }
			TInlineComponentArray<ULocalLightComponent*> Comps;
			A->GetComponents<ULocalLightComponent>(Comps);
			for (ULocalLightComponent* L : Comps)
			{
				if (L && IsValid(L) && L->IsRegistered()) { CachedLights.Add(L); }
			}
		}
	}
	bCacheValid = true;
}

void UAsdaLightBudgetSubsystem::Collect(TArray<ULocalLightComponent*>& Out)
{
	// ★ Доводка E-4 (М-2): из кэша (полный обход мира только при инвалидации). Умершие компоненты (свет удалён) чистим на месте —
	//   освободившийся слот бюджета переприменяем на ближайшем Tick через MarkDirty.
	if (!bCacheValid) { RebuildCache(); }
	int32 Stale = 0;
	for (int32 i = CachedLights.Num() - 1; i >= 0; --i)
	{
		ULocalLightComponent* L = CachedLights[i].Get();
		if (L && IsValid(L) && L->IsRegistered()) { Out.Add(L); }
		else { CachedLights.RemoveAtSwap(i); ++Stale; }   // порядок не важен — кандидаты пересортировываются
	}
	if (Stale > 0) { MarkDirty(); }
}

FVector UAsdaLightBudgetSubsystem::PlayerPos() const
{
	// ★ волна 3 п.11 (15.09.2026): общая точка наблюдателя — AsdaView (камера первого локального игрока → павн — как было).
	//   ★ ИСПРАВЛЕНИЕ E7 (узкое): без контроллера и павна раньше возвращался НОЛЬ мира (сортировка теней от угла карты);
	//   теперь — последний отрисованный вид этого мира, если он есть, иначе ноль, как было.
	FVector P = FVector::ZeroVector;
	AsdaView::GetViewLocation(GetWorld(), P);
	return P;
}

bool UAsdaLightBudgetSubsystem::AuthoredOf(ULocalLightComponent* L)
{
	if (bool* V = Authored.Find(L)) { return *V; }
	// Первое касание («голый» PointLight владельца): авторское = текущее значение компонента
	const bool Cur = L->CastShadows != 0;
	Authored.Add(L, Cur);
	return Cur;
}

FString UAsdaLightBudgetSubsystem::NameOf(const ULocalLightComponent* L)
{
	const AActor* A = L ? L->GetOwner() : nullptr;
	if (!A) { return L ? L->GetName() : FString(TEXT("?")); }
	TInlineComponentArray<ULocalLightComponent*> Comps;
	A->GetComponents<ULocalLightComponent>(Comps);
	return Comps.Num() > 1 ? FString::Printf(TEXT("%s/%s"), *A->GetActorNameOrLabel(), *L->GetName()) : A->GetActorNameOrLabel();
}

void UAsdaLightBudgetSubsystem::Apply(bool bLog)
{
	// Кэш авторских значений: выбросить умершие компоненты
	for (auto It = Authored.CreateIterator(); It; ++It) { if (!It->Key.IsValid()) { It.RemoveCurrent(); } }

	TArray<ULocalLightComponent*> All;
	Collect(All);
	const UAsdaRenderSettings* S = UAsdaRenderSettings::Get();
	const int32 Level = CurrentShadowLevel();
	// asda.Light.Enable 0/1: гасим/возвращаем bAffectsWorld (авторское — в кэше AuthoredAffects при первом касании)
	for (ULocalLightComponent* L : All)
	{
		// ★★★ НАЙДЕНО ВОЛНОЙ Ф6 (10.09.2026), ДЕФЕКТ ЖИЛ С ВОЛНЫ Ф2: ЭФФЕКТ-СВЕТ САМ ВЕДЁТ СВОЁ bAffectsWorld.
		//   Кэш «авторского» значения снимается ПРИ ПЕРВОМ КАСАНИИ, а эффект-свет РОЖДАЕТСЯ ПОГАШЕННЫМ
		//   (bAffectsWorld = false, место в бюджете количества ещё не выдано). Поэтому в кэш попадал false,
		//   и КАЖДЫЙ Apply гасил горящий эффект-свет: сначала bAffectsWorld = false + MarkRenderStateDirty,
		//   а тиком позже сам компонент зажигал его обратно — лишняя пересборка состояния рендера на ровном
		//   месте. Хуже того, следом в ЭТОМ ЖЕ проходе кандидат отсеивался проверкой «!bAffectsWorld» как
		//   «спящий близнец», и просьба эффекта о тени не доходила до бюджета НИКОГДА — при том, что в кэше
		//   авторских теней она честно лежала (дамп показывал «1→1», а сводка — «кандидатов 0»).
		//   Эффект-свет включается/выключается своими ключами (asda.Fx.Light, бюджет количества и экранный),
		//   поэтому здесь его просто не трогаем.
		if (L->IsA<UAsdaFxLightComponent>()) { continue; }
		bool* A = AuthoredAffects.Find(L);
		if (!A) { A = &AuthoredAffects.Add(L, L->bAffectsWorld != 0); }
		const bool bWant = GAsdaLightEnable != 0 ? *A : false;
		if ((L->bAffectsWorld != 0) != bWant) { L->bAffectsWorld = bWant; L->MarkRenderStateDirty(); }
	}
	for (auto It = AuthoredAffects.CreateIterator(); It; ++It) { if (!It->Key.IsValid()) { It.RemoveCurrent(); } }
	const int32 Budget = ResolveBudget();
	const FVector P = PlayerPos();
	LastPlayerPos = P;

	// Кандидаты (авторские тени) + попутные проверки
	// ★ Ф6: bFx — эффект-свет (UAsdaFxLightComponent). Он идёт в СВОЮ полосу бюджета, см. шапку CVar-а
	struct FCand { ULocalLightComponent* L; bool bMain; float Dist; bool bFx; };
	TArray<FCand> Cands;
	int32 NStatic = 0, NFixedDraw = 0;
	// Мир из списка «отсечку дальности не применять» — см. пояснение у самой страховки ниже
	const UWorld* CullW = GetWorld();
	// ★★★ ВОЛНА Ф9 (10.09.2026): ключ asda.Light.DrawCull 0 снимает отсечку дальности во ВСЕХ мирах.
	//   Спрашиваем ОДНУ И ТУ ЖЕ функцию, что и сам AAsdaLocalLightActor: иначе актор ставил бы 0, а эта
	//   страховка в тот же кадр возвращала бы 4000, и ключ выглядел бы неработающим (проверено на L_LightTest).
	const bool bNoDrawCullWorld = AAsdaLocalLightActor::IsDrawCullDisabled()
		|| (CullW && UAsdaCullSettings::Get()->SkipWorlds.Contains(FName(*CullW->GetName())));
	for (ULocalLightComponent* L : All)
	{
		AActor* A = L->GetOwner();
		if (L->Mobility != EComponentMobility::Movable && !WarnedStatic.Contains(L))
		{
			WarnedStatic.Add(L); ++NStatic;
			UE_LOG(LogAsdaLightBudget, Warning, TEXT("Локальный свет %s: mobility %s — при r.AllowStaticLighting=False он НЕ светит; поставьте Movable (аудит E01)"),
				*NameOf(L), L->Mobility == EComponentMobility::Static ? TEXT("Static") : TEXT("Stationary"));
		}
		// ★★★ МИРЫ БЕЗ ОТСЕЧКИ ДАЛЬНОСТИ (10.09.2026, жалоба «на карте Login источники света игнорируются»).
		//   Сцена логина построена в масштабе ×100: камера стоит в 68 000–87 000 юн от постановки (шкатулка в
		//   (0,0,18700), глифы в ±10 500/6 150/13 800, камера (0,−79 996,19 500)). Страховка «MaxDrawDistance=0 →
		//   4000 юн» гасила ТАМ ЛЮБОЙ поставленный источник за 40 м от камеры — замер: свет с радиусом 1000 юн и
		//   MaxDraw 4000 давал Δ средней яркости кадра +0.26 при шуме контрольных пар ±0.25, то есть НИЧЕГО.
		//   В этих же мирах не применяется и отсечка мешей (UAsdaCullDistanceSubsystem, ТОТ ЖЕ список
		//   UAsdaCullSettings::SkipWorlds) — по той же причине: авторская постановка, проблемы с кадром нет.
		//   Поэтому здесь MaxDrawDistance=0 остаётся авторским «никогда не гаснет».
		// ★★★ ВОЛНА Ф9: ЭФФЕКТ-СВЕТ СЮДА НЕ ОТНОСИТСЯ. Он ULocalLightComponent, поэтому в общий список
		//   попадает, но дальность себе ставит САМ и КАЖДЫЙ КАДР (UAsdaFxLightComponent::ApplyShapeFrame),
		//   в режиме отбора по кадру — ноль. Страховка успевала бы только записать в лог 27 ложных
		//   предупреждений на карту и была бы тут же затёрта: чинить чужое, что чинится само, незачем.
		if (!bNoDrawCullWorld && !Cast<UAsdaFxLightComponent>(L) && L->MaxDrawDistance <= 0.f && !FixedDraw.Contains(L))
		{
			FixedDraw.Add(L); ++NFixedDraw;
			L->SetMaxDrawDistance(S->DefaultMaxDrawDistance);
			if (L->MaxDistanceFadeRange <= 0.f) { L->SetMaxDistanceFadeRange(S->DefaultMaxDistanceFadeRange); }
			UE_LOG(LogAsdaLightBudget, Warning, TEXT("Локальный свет %s: MaxDrawDistance=0 (никогда не гаснет; r.LightMaxDrawDistanceScale бессилен) → %.0f/%.0f (дефолт UAsdaRenderSettings; аудит E02/W03)"),
				*NameOf(L), S->DefaultMaxDrawDistance, S->DefaultMaxDistanceFadeRange);
		}
		const bool bAuthored = AuthoredOf(L);
		if (!bAuthored)
		{
			if (L->CastShadows) { L->SetCastShadows(false); }   // авторское «без теней» держим (кто-то включил мимо нас)
			continue;
		}
		if (!L->bAffectsWorld || !L->IsVisible()) { continue; }   // спящий близнец актора / asda.Light.Enable 0 — не кандидат
		Cands.Add({ L, A && A->ActorHasTag(FName(TEXT("Main"))), (float)FVector::Dist(L->GetComponentLocation(), P), L->IsA<UAsdaFxLightComponent>() });
	}

	const int32 FxBudget = ResolveFxBudget();
	int32 Kept = 0, FxCands = 0, FxKept = 0;
	if (Budget == -2 && FxBudget == -2)
	{
		// Не вмешиваться: вернуть авторские
		for (const FCand& C : Cands) { if (!C.L->CastShadows) { C.L->SetCastShadows(true); } ++Kept; if (C.bFx) { ++FxCands; ++FxKept; } }
		bNeedsRebalance = false;
	}
	else
	{
		// Main → остальные; внутри группы — ближе к игроку первым.
		// ★ Ф6: СЧЁТЧИК МЕСТ У КАЖДОЙ ПОЛОСЫ СВОЙ. Сортировка общая (порядок внутри полосы от этого не меняется:
		//   относительный порядок двух эффектов или двух фонарей тот же), а места выдаются из разных кошельков —
		//   поэтому 437 эффектов плотной карты физически не могут отобрать тень у фонаря.
		Cands.Sort([](const FCand& A, const FCand& B) { if (A.bMain != B.bMain) { return A.bMain; } return A.Dist < B.Dist; });
		int32 UsedMap = 0, UsedFx = 0;
		for (int32 i = 0; i < Cands.Num(); ++i)
		{
			const bool bFx = Cands[i].bFx;
			const int32 Lane = bFx ? FxBudget : Budget;
			int32& Used = bFx ? UsedFx : UsedMap;
			const bool bOn = (Lane == -2) || (Used < Lane);
			if (bOn) { ++Used; }
			if ((Cands[i].L->CastShadows != 0) != bOn) { Cands[i].L->SetCastShadows(bOn); }
			if (bOn) { ++Kept; }
			if (bFx) { ++FxCands; if (bOn) { ++FxKept; } }
		}
		bNeedsRebalance = ((Cands.Num() - FxCands) > Budget && Budget > 0) || (FxCands > FxBudget && FxBudget > 0);
	}

	LastTotal = All.Num(); LastCandidates = Cands.Num(); LastKept = Kept; LastBudget = Budget;
	LastFxCandidates = FxCands; LastFxKept = FxKept; LastFxBudget = FxBudget;
	if (bLog)
	{
		UE_LOG(LogAsdaLightBudget, Log, TEXT("Бюджет теней локальных светов [%s]: светов %d, с тенями авторских %d → оставлено %d (бюджет %s; из них ЭФФЕКТ-светов %d → %d, бюджет эффектов %s; sg.ShadowQuality %d%s%s)"),
			GetWorld() ? *GetWorld()->GetName() : TEXT("?"), All.Num(), Cands.Num(), Kept,
			Budget == -2 ? TEXT("не вмешиваться") : *FString::FromInt(Budget),
			FxCands, FxKept, FxBudget == -2 ? TEXT("не вмешиваться") : *FString::FromInt(FxBudget), Level,
			NStatic ? *FString::Printf(TEXT("; статических %d — не светят"), NStatic) : TEXT(""),
			NFixedDraw ? *FString::Printf(TEXT("; MaxDrawDistance=0 исправлено у %d"), NFixedDraw) : TEXT(""));
		for (int32 i = 0; i < Cands.Num(); ++i)
		{
			UE_LOG(LogAsdaLightBudget, Log, TEXT("  %2d. %-40s %s %s дист %.0f → тени %s"), i + 1, *NameOf(Cands[i].L), Cands[i].bMain ? TEXT("Main") : TEXT("    "),
				Cands[i].bFx ? TEXT("эффект") : TEXT("карта "), Cands[i].Dist, Cands[i].L->CastShadows ? TEXT("ВКЛ") : TEXT("выкл (бюджет)"));
		}
	}
	else
	{
		UE_LOG(LogAsdaLightBudget, Verbose, TEXT("Перебалансировка теней по дистанции: кандидатов %d, оставлено %d (бюджет %d)"), Cands.Num(), Kept, Budget);
	}
}

void UAsdaLightBudgetSubsystem::Dump()
{
	TArray<ULocalLightComponent*> All;
	Collect(All);
	const FVector P = PlayerPos();
	UE_LOG(LogAsdaLightBudget, Log, TEXT("asda.Light.Dump [%s]: локальных светов %d (asda.Light.Enable %d); кэш %s (%d записей); бюджет %d (CVar %d, sg.ShadowQuality %d → уровень %d); последний Apply: кандидатов %d, оставлено %d | ПОЛОСА ЭФФЕКТОВ: бюджет %d (CVar %d, уровень %d), кандидатов %d, оставлено %d"),
		GetWorld() ? *GetWorld()->GetName() : TEXT("?"), All.Num(), GAsdaLightEnable, bCacheValid ? TEXT("актуален") : TEXT("устарел"), CachedLights.Num(), ResolveBudget(), GAsdaLightShadowBudget, CurrentShadowLevel(), LevelBudget(CurrentShadowLevel()), LastCandidates, LastKept,
		ResolveFxBudget(), GAsdaLightFxShadowBudget, LevelFxBudget(CurrentShadowLevel()), LastFxCandidates, LastFxKept);
	UE_LOG(LogAsdaLightBudget, Log, TEXT("  %-40s %-6s %-9s %9s %6s %7s %5s %5s %-5s %8s %-3s %s"), TEXT("имя"), TEXT("класс"), TEXT("mobility"), TEXT("сила"), TEXT("ед."), TEXT("радиус"), TEXT("draw"), TEXT("fade"), TEXT("тени"), TEXT("дист"), TEXT("вкл"), TEXT("теги"));
	for (ULocalLightComponent* L : All)
	{
		const AActor* A = L->GetOwner();
		FString TagsStr;
		if (A) { for (const FName& T : A->Tags) { TagsStr += T.ToString() + TEXT(" "); } }
		const bool* Auth = Authored.Find(L);
		const TCHAR* Cls = L->IsA<USpotLightComponent>() ? TEXT("Spot") : L->IsA<URectLightComponent>() ? TEXT("Rect") : L->IsA<UPointLightComponent>() ? TEXT("Point") : TEXT("Local");
		const TCHAR* Mob = L->Mobility == EComponentMobility::Movable ? TEXT("Movable") : L->Mobility == EComponentMobility::Static ? TEXT("STATIC") : TEXT("STATION.");
		UE_LOG(LogAsdaLightBudget, Log, TEXT("  %-40s %-6s %-9s %9.3f %6s %7.0f %5.0f %5.0f %s→%s %8.0f %-3s %s"), *NameOf(L), Cls, Mob, L->Intensity,
			*UEnum::GetDisplayValueAsText(L->IntensityUnits).ToString(), L->AttenuationRadius, L->MaxDrawDistance, L->MaxDistanceFadeRange,
			Auth ? (*Auth ? TEXT("1") : TEXT("0")) : TEXT("?"), L->CastShadows ? TEXT("1") : TEXT("0"), FVector::Dist(L->GetComponentLocation(), P),
			L->bAffectsWorld ? TEXT("1") : TEXT("0"), *TagsStr);
	}
}
