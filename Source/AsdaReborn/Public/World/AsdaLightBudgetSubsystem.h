// Copyright (c) 2026 @sitxovski (Discord)
#pragma once

#include "CoreMinimal.h"
#include "World/AsdaRenderQualitySubsystemBase.h"   // ★ волна 3 п.12 (15.09.2026): общий каркас подсистем качества рендера
#include "AsdaLightBudgetSubsystem.generated.h"

class ULocalLightComponent;
class AActor;

// ★ Сет E, фаза E-4 (07.09.2026; setE4_prep_report.md §2.3, дополнение §2.4): БЮДЖЕТ ТЕНЕЙ ЛОКАЛЬНЫХ СВЕТОВ ПО УРОВНЮ КАЧЕСТВА.
//   Не в акторе — покрывает и «голые» PointLight/SpotLight/RectLight владельца и компоненты внутри BP. На старте мира, при
//   смене sg.* (Scalability::OnScalabilitySettingsChanged), при смене asda.Light.ShadowBudget и по авторским правкам актора
//   (SetAuthoredCastShadows) собирает все ULocalLightComponent мира, кандидаты = авторские CastShadows=true, сортирует
//   (тег Main → остальные; внутри — дистанция до камеры игрока), первым K оставляет авторское значение, остальным
//   SetCastShadows(false). Авторское значение кэшируется (как bAuthoredHidden у облаков) — при росте бюджета возвращается.
//   Перебалансировка по дистанции — раз в ShadowRebalanceIntervalSec (UAsdaRenderSettings) и только когда кандидатов > K
//   (смена CastShadows инвалидирует кэш VSM/CSM — не каждый кадр).
//   K: CVar asda.Light.ShadowBudget: ≥0 = ровно столько; −1 = по уровню — [AsdaShadow@N] Light.ShadowBudget из Scalability-ini
//   (N = sg.ShadowQuality; Config/DefaultScalability.ini 0/2/4/8/Cine 8); −2 = не вмешиваться (авторские значения возвращаются).
//   Заодно: Static/Stationary свет → предупреждение в лог (r.AllowStaticLighting=False — он не светит); MaxDrawDistance ≤ 0 →
//   дефолт UAsdaRenderSettings (иначе r.LightMaxDrawDistanceScale уровня бессилен). Консоль asda.Light.Dump — таблица в лог;
//   asda.Light.Enable 0/1 — все локальные света выкл/авторские (A/B «с фонарями / без» на одной карте, не персистится).
UCLASS()
class ASDAREBORN_API UAsdaLightBudgetSubsystem : public UAsdaRenderQualitySubsystemBase
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	// ★ волна 3 п.12 (15.09.2026): Initialize/Deinitialize (подписка на уровень качества и на появление актора) — в базе
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// Tick: отложенное применение (bDirty) и перебалансировка по дистанции
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaLightBudgetSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickableInEditor() const override { return false; }

	// Авторское значение теней компонента (актор/BP сообщает при применении пресета); применение — на ближайшем Tick
	void SetAuthoredCastShadows(ULocalLightComponent* L, bool bAuthored);

	// Пометить: пересобрать и переприменить бюджет на ближайшем Tick (карта загрузилась / уровень сменился / ключ)
	void MarkDirty() { bDirty = true; }

	// Применить сейчас (сбор + сортировка + переключение). bLog — строка сводки в лог
	void Apply(bool bLog);

	// Таблица всех локальных светов мира в лог (asda.Light.Dump) — не const: пользуется кэшем Collect (может пересобрать)
	void Dump();

	// Текущий бюджет K (без мира): CVar ≥0 / −1 → уровень / −2 → −2 (не вмешиваться)
	static int32 ResolveBudget();
	// Бюджет уровня L (0..4) из Scalability-ini [AsdaShadow@L] Light.ShadowBudget (fallback — UAsdaRenderSettings)
	static int32 LevelBudget(int32 Level);
	// Уровень sg.ShadowQuality из UGameUserSettings (3, если настроек нет)
	static int32 CurrentShadowLevel();

	// ★ ВОЛНА Ф6 (10.09.2026): ВТОРАЯ ПОЛОСА — тени ЭФФЕКТ-светов (UAsdaFxLightComponent). Кошелёк отдельный,
	//   подсистема та же: фонари карты и эффекты не конкурируют, и включение теней эффектам не отбирает
	//   тень у фонаря (обоснование — в шапке CVar asda.Light.FxShadowBudget в .cpp).
	static int32 ResolveFxBudget();
	// Бюджет эффектов уровня L (0..4) из [AsdaShadow@L] Light.FxShadowBudget (fallback — UAsdaRenderSettings)
	static int32 LevelFxBudget(int32 Level);

	// Все подсистемы игровых миров процесса → MarkDirty (колбэк CVar / смены уровня)
	static void MarkAllDirty();

	// Число светов / кандидатов / оставлено с тенями после последнего Apply (для SelfTest и лога)
	int32 LastTotal = 0;
	int32 LastCandidates = 0;
	int32 LastKept = 0;
	int32 LastBudget = -1;
	// ★ Ф6: то же по полосе эффект-светов (для дампа, лога и самопроверки)
	int32 LastFxCandidates = 0;
	int32 LastFxKept = 0;
	int32 LastFxBudget = -1;

private:
	// Авторское CastShadows по компоненту (кэш; заполняется при первом касании или SetAuthoredCastShadows)
	TMap<TWeakObjectPtr<ULocalLightComponent>, bool> Authored;
	// Авторское bAffectsWorld по компоненту (для asda.Light.Enable 0/1)
	TMap<TWeakObjectPtr<ULocalLightComponent>, bool> AuthoredAffects;
	// Компоненты, о которых уже предупредили (Static/Stationary) / которым уже поставили MaxDrawDistance
	TSet<TWeakObjectPtr<ULocalLightComponent>> WarnedStatic;
	TSet<TWeakObjectPtr<ULocalLightComponent>> FixedDraw;
	// Нужно применить на ближайшем Tick
	bool bDirty = false;
	// Таймер перебалансировки и позиция игрока при последнем применении
	float RebalanceTimer = 0.f;
	FVector LastPlayerPos = FVector::ZeroVector;
	// Кандидатов было больше бюджета — перебалансировка по дистанции имеет смысл
	bool bNeedsRebalance = false;

	// ★ Доводка E-4 (07.09.2026, ревизия review_setE4.md М-2): КЭШ списка локальных светов мира — раньше Apply() сканировал
	//   ВСЕ актёры мира (TActorIterator) при каждой перебалансировке по дистанции (раз в 2 с), спайк GT на плотных картах.
	//   Теперь полный обход только при инвалидации (загрузка карты / смена уровня / появление актора света), а перебалансировка
	//   берёт готовый список и лишь пересортировывает по дистанции. Умершие компоненты (свет удалён) чистятся при чтении.
	TArray<TWeakObjectPtr<ULocalLightComponent>> CachedLights;
	// Кэш актуален (иначе Collect пересоберёт при первом обращении)
	bool bCacheValid = false;

	// Пересобрать кэш полным обходом мира
	void RebuildCache();
	// Актор появился в мире: если несёт локальный свет — инвалидировать кэш и переприменить бюджет
	virtual void OnActorSpawned(AActor* A) override;
	// Смена уровня качества → переприменить бюджет на ближайшем Tick (★ волна 3 п.12 (15.09.2026): подписка — в базе)
	virtual void OnQualityLevelsChanged() override { MarkDirty(); }

	// Собрать все ULocalLightComponent мира (из кэша; при инвалидации — полный обход). Не const: чистит/пересобирает кэш
	void Collect(TArray<ULocalLightComponent*>& Out);
	// Точка наблюдателя AsdaView::GetViewLocation (камера игрока → павн → отрисованный вид; нет — ноль мира), для сортировки по дистанции
	FVector PlayerPos() const;
	// Авторское значение (кэш или текущее CastShadows компонента при первом касании)
	bool AuthoredOf(ULocalLightComponent* L);
	// Имя для лога: метка/имя актора (+ имя компонента, если у актора их несколько)
	static FString NameOf(const ULocalLightComponent* L);
};
