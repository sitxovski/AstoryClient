// Copyright (c) 2026 @sitxovski (Discord)
// ★ ПОДСВЕТКА ВЫБРАННОЙ ЦЕЛИ (порт Reports/target_selection.md).
//
// ЭТАЛОН (что делает оригинал при выборе цели — визуальная часть на МОДЕЛИ):
//   1. Каждый кадр (game_main.c:1449-1462, состояние мира g_GameState==1) клиент пересчитывает
//      ДВА указателя-«ключа» подсветки:
//        g_pTargetHighlightPosA / g_pTargetHighlightPosB
//      Для цели-сущности (моб/NPC, Entity_FindByRenderId(g_nTargetUserIdx)) это поля entity+0x14 и
//      entity+0x1C; для цели-игрока — CCharacter::boneAttachA/boneAttachB. Мёртвая цель
//      (Entity_GetActionState==10) и САМ локальный игрок подсветку НЕ получают.
//   2. В обходе мешей (render_core.c:40099 renderList_traverseAndDispatch@0x111B730 и
//      model_anim.c:26045) для каждого узла меша:
//          if (node[10] == g_pTargetHighlightPosA || node[14] == g_pTargetHighlightPosB)
//              g_fNoSkinTexScroll = 0x40400000;   // 3.0f  — ЦЕЛЬ
//          else
//              g_fNoSkinTexScroll = 0x3F800000;   // 1.0f  — обычный меш
//   3. Значение уезжает в шейдер как .z параметра float4 "NoSkinTexScroll"
//      (model_anim.c:24605-24608 и :25003-25006; .xy = UV-скролл, .w = альфа-множитель).
//   4. ...и НА ЭТОМ ОБРЫВАЕТСЯ. Пиксельные шейдеры .fx СБОРКИ ЮЗЕРА читают из float4
//      «NoSkinTexScroll» только .xy и .w — компоненты .z в шейдере НЕТ (см. вердикт ниже).
//      Задуманная строка «//Change Brightness: Output.RGBColor += NoSkinTexScroll.z*0.1f»
//      (обычный меш +0.1, цель +0.3, дельта +0.2) сохранилась только в ДРУГИХ сборках и там
//      закомментирована. ⇒ ВИЗУАЛЬНО подсветки модели в игре НЕТ.
//   ОБВОДКИ (силуэта), круга под ногами, отдельного .jeb-эффекта и звука на ВЫБОР цели тоже НЕТ.
//   Выбор цели в оригинале виден ТОЛЬКО по плашке цели, HUD-рамке, курсору и панелям.
//
// ПОРТ: до 2026-08-20 компонент по умолчанию был ВЫКЛЮЧЕН (Mode=None) — 1:1 оригинал.
//   Утром 2026-08-20 дефолт подняли до `Outline` как «исправление дефекта D15».
//
// ★★★ ОТКАТ ДЕФОЛТА (2026-08-20, вечер) — ПО ПРЯМОМУ УКАЗАНИЮ ВЛАДЕЛЬЦА ПОСЛЕ ЖИВОЙ ПРОВЕРКИ.
//   Слова владельца: «Выделение вижу, но оно НЕ ВЕРНОЕ! NPC красится белым цветом. А в оригинале
//   его модель не менялась». Владелец прав по существу: раскраска модели — НЕ эталон (вердикт
//   2026-08-04 ниже остаётся в силе), а «исправление дефекта» здесь ДОРИСОВЫВАЛО то, чего в
//   оригинале не видел никто. ⇒ `Mode` снова `None` в ОБОИХ местах, где живёт дефолт
//   (`UAsdaTargetHighlightSettings::Mode` и `UAsdaTargetHighlightComponent::Mode`).
//   Режимы `Brightness/Outline/Both` НИКУДА НЕ ДЕЛИСЬ — включаются вручную `asda.Target.HL 1|2|3`.
//   ⚠ ПОЧЕМУ «обводка» выглядела СПЛОШНОЙ ЗАЛИВКОЙ (гипотеза, живым замером НЕ подтверждена —
//   проверять, если режим когда-нибудь понадобится): край считается как
//   `pow(1 − clamp(dot(VertexNormalWS, CameraVectorWS), 0, 1), RimPower)`, и если у меша цели
//   вершинные нормали нулевые/битые, `dot ≈ 0` ⇒ край = 1 НА ВСЁМ меше ⇒ аддитивная добавка
//   упирается в потолок `MaxAdd` (0.6) по всей площади. Это ровно тот же механизм, что и баг
//   2026-08-04 с нодой `Fresnel`, только источник нормали другой.
//   Разбор и решение — `Reports/npc_visual_fix.md` §2.
//
// ★ БАГ «СПЛОШНОЙ БЕЛЫЙ СИЛУЭТ» (2026-08-04) — ПРИЧИНА И ЗАЩИТА.
//   Первая версия материала считала край нодой Fresnel, а та берёт нормаль из
//   `Parameters.WorldNormal` (FHLSLMaterialTranslator::PixelNormalWS, HLSLMaterialTranslator.cpp:11776).
//   В UNLIT-материале шейдинговая нормаль не вычисляется → dot(N,V)≈0 → Fresnel = 0.04+0.96·1 ≈ 1
//   НА ВСЁМ меше, и эмиссия становилась 0.2 + 1·1.6 = 1.8 → аддитивно это выбеливает модель в
//   ровный белый силуэт (ни текстуры, ни объёма). Fresnel — единственный член графа, способный
//   превысить 1. Меры: (1) край считается явно через VertexNormalWS·CameraVectorWS (вершинный
//   атрибут, от шейдинг-модели НЕ зависит); (2) жёсткий потолок MaxAdd (0.6) на сумму каналов.
//   Побочно найдено: `MaterialEditingLibrary.connect_material_expressions` при неверном имени входа
//   МОЛЧА возвращает False (Clamp/OneMinus вход = "", у Power вход = "Exp") — скрипт сборки теперь
//   логирует каждое соединение и падает в лог при битых.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "AsdaTargetHighlightComponent.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMeshComponent;

// ★★★ ВЕРДИКТ 2026-08-04 (после двух живых тестов): В СБОРКЕ ЮЗЕРА ПОДСВЕТКИ МОДЕЛИ НЕТ ВООБЩЕ.
//   Путь данных живой ТОЛЬКО до шейдера: значение 3.0/1.0 действительно уходит в параметр эффекта
//   «NoSkinTexScroll» (хендл кешируется в render_core.c:40405 GetParameterByName(0,"NoSkinTexScroll"),
//   запись — model_anim.c:24607/25007). НО пиксельные шейдеры .fx сборки юзера
//   (client/system/EmptyProject.fx, md5 d23db2c455144447f5dcb6f1b941a980, рядом с AsdaStory.exe
//   13 344 768 = сборка IDB) читают из этого float4 ТОЛЬКО .xy (UV-скролл, стр. 620/1008) и
//   .w (альфа, стр. 694/828). Компоненты .z в шейдере НЕТ НИ ОДНОГО ВХОЖДЕНИЯ — строка
//   «//Change Brightness: Output.RGBColor += NoSkinTexScroll.z*0.1f» есть только в ДРУГИХ сборках
//   (AsdaGlobal/FableClassic/WorldOfDreams) и там ЗАКОММЕНТИРОВАНА.
//   ⇒ Выбор цели в оригинале НЕ меняет вид модели.
//
// ★ ИСТОРИЯ ДЕФЕКТА D15 (утро 2026-08-20, ОТМЕНЕНО вечером того же дня — см. блок «ОТКАТ ДЕФОЛТА»).
//   Рассуждение было такое: раз клиент каждый кадр вычисляет множитель 3.0 и гонит его в параметр
//   эффекта, а шейдер именно этой сборки его не читает, — это оборванная ветка, то есть дефект,
//   и закон `fix-original-defects` велит его исправлять; практическое следствие — у NPC нет полосы
//   HP, плашка видна всегда, и выбор цели не давал на экране ничего.
//   ПОЧЕМУ ОТМЕНЕНО: «исправление» подменяло НЕВИДИМОЕ на ВЫДУМАННОЕ. Закон про дефекты применим
//   там, где известно, КАК оригинал должен был выглядеть; здесь известна только формула яркости
//   (+0.2 аддитивно, `asda.Target.HL 1`), а «обводка» — целиком наша выдумка. Обратная связь при
//   выборе NPC в оригинале всё-таки есть, просто не на модели: курсор `talk` при наведении
//   (`Input_PickEntityHover@0x993DD0`, input.c:1100-1210 — в порту это `EAsdaCursor::Talk`) и,
//   собственно, открывающееся окно разговора.
//
// Режим подсветки цели.
UENUM(BlueprintType)
enum class EAsdaHighlightMode : uint8
{
	None       UMETA(DisplayName="Выключено (как в оригинале)"),
	Brightness UMETA(DisplayName="Яркость (задумка ориг. шейдера)"),// аддитивная заливка +0.2
	Outline    UMETA(DisplayName="Обводка (НЕ эталон, надстройка порта)"), // только краевое свечение
	Both       UMETA(DisplayName="Яркость + обводка")
};

// Отношение цели к игроку — влияет только на выбор стиля (в ОРИГИНАЛЕ подсветка одинаковая).
UENUM(BlueprintType)
enum class EAsdaTargetRelation : uint8
{
	Neutral  UMETA(DisplayName="Нейтральная"),
	Hostile  UMETA(DisplayName="Враждебная (моб)"),
	Friendly UMETA(DisplayName="Дружественная (NPC)"),
	Player   UMETA(DisplayName="Игрок")
};

// Стиль подсветки одного типа цели.
USTRUCT(BlueprintType)
struct FAsdaHighlightStyle
{
	GENERATED_BODY()

	// Цвет подсветки. Эталон цвета не задаёт (добавка белая по всем каналам) → дефолт белый.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Подсветка")
	FLinearColor Color = FLinearColor::White;

	// Аддитивная заливка по всему мешу. 0.2 = ДЕЛЬТА оригинала (3.0*0.1 - 1.0*0.1).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Подсветка", meta=(ClampMin="0.0", ClampMax="4.0"))
	float FillAdd = 0.2f;

	// Сила краевого свечения (обводки). Современная надстройка, в оригинале 0.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Подсветка", meta=(ClampMin="0.0", ClampMax="8.0"))
	float RimIntensity = 0.8f;

	// Резкость края: больше — тоньше обводка.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Подсветка", meta=(ClampMin="0.5", ClampMax="16.0"))
	float RimPower = 3.0f;

	// ★ ПОТОЛОК суммарной добавки: amount = min(FillAdd + Rim, MaxAdd). Защита от «белого силуэта»
	//   (баг 2026-08-04: край мог дать 1.8 и аддитивно выбелить модель). Модель обязана оставаться
	//   узнаваемой — как в оригинале, где добавка всего +0.2.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Подсветка", meta=(ClampMin="0.05", ClampMax="2.0"))
	float MaxAdd = 0.6f;

	// Амплитуда пульсации (0 = ровное свечение, как в оригинале).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Подсветка", meta=(ClampMin="0.0", ClampMax="1.0"))
	float PulseAmp = 0.22f;

	// Частота пульсации, Гц.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Подсветка", meta=(ClampMin="0.0", ClampMax="8.0"))
	float PulseHz = 1.1f;
};

// DataAsset с настройками подсветки — назначается контроллеру игрока (Asda|Target).
// Нужен потому, что цели (мобы/NPC) спавнятся в рантайме: править дефолты на их экземплярах нельзя.
UCLASS(BlueprintType, meta=(DisplayName="Asda: настройки подсветки цели"))
class ASDAREBORN_API UAsdaTargetHighlightSettings : public UDataAsset
{
	GENERATED_BODY()

public:
	// ★★★ ДЕФОЛТ = None (ВОЗВРАТ К ОРИГИНАЛУ 2026-08-20 по прямому указанию владельца, см. шапку
	//   файла, блок «ОТКАТ ДЕФОЛТА»). Раскраска модели — НЕ эталон; включается вручную
	//   `asda.Target.HL 1|2|3` или этим полем.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Подсветка цели")
	EAsdaHighlightMode Mode = EAsdaHighlightMode::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Подсветка цели")
	FAsdaHighlightStyle Hostile;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Подсветка цели")
	FAsdaHighlightStyle Friendly;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Подсветка цели")
	FAsdaHighlightStyle PlayerStyle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Подсветка цели")
	FAsdaHighlightStyle Neutral;

	// Материал-оверлей подсветки (по умолчанию /Game/Materials/M_AsdaTargetHighlight).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Подсветка цели")
	TSoftObjectPtr<UMaterialInterface> HighlightMaterial;

	// Писать CustomDepth/Stencil у подсвеченной цели (для пост-процесс-обводки, если её добавят).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Подсветка цели")
	bool bWriteCustomDepth = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Подсветка цели", meta=(ClampMin="0", ClampMax="255"))
	int32 CustomDepthStencil = 1;

	// Стиль по отношению цели.
	const FAsdaHighlightStyle& StyleFor(EAsdaTargetRelation Rel) const;
};

// Компонент подсветки: вешается на актора-цель (моб/NPC/игрок) и красит ВСЕ его меш-компоненты
// оверлей-материалом. Создаётся автоматически контроллером при выборе цели, если его нет.
UCLASS(ClassGroup=Asda, meta=(BlueprintSpawnableComponent, DisplayName="Asda: подсветка цели"))
class ASDAREBORN_API UAsdaTargetHighlightComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAsdaTargetHighlightComponent();

	// Локальные настройки (используются, если контроллер не передал DataAsset).
	// ★★★ ДЕФОЛТ = None — ровно как в оригинале (откат 2026-08-20, см. шапку файла).
	//   Быстро включить в рантайме: `asda.Target.HL 1|2|3`, фактика: `asda.Target.HLDump`.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Подсветка цели")
	EAsdaHighlightMode Mode = EAsdaHighlightMode::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Подсветка цели")
	FAsdaHighlightStyle DefaultStyle;

	// Имена/подстроки компонентов, которые подсвечивать НЕЛЬЗЯ (метки квестов, плашки, метки клика).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Подсветка цели")
	TArray<FString> IgnoreComponentSubstrings;

	// Включить/выключить подсветку. Settings может быть nullptr — тогда берутся локальные поля.
	UFUNCTION(BlueprintCallable, Category="Asda|Подсветка цели")
	void SetHighlighted(bool bOn, EAsdaTargetRelation Rel, const UAsdaTargetHighlightSettings* Settings = nullptr);

	UFUNCTION(BlueprintCallable, Category="Asda|Подсветка цели")
	void ClearHighlight() { SetHighlighted(false, ActiveRelation, ActiveSettings.Get()); }

	UFUNCTION(BlueprintPure, Category="Asda|Подсветка цели")
	bool IsHighlighted() const { return bActive; }

	// Текущий режим с учётом консольного оверрайда и DataAsset (для лога/чек-листа).
	EAsdaHighlightMode EffectiveMode() const;
	// Перекрасить заново текущим режимом (после `asda.Target.HL`).
	void ReapplyNow();
	// Дамп ФАКТИЧЕСКИХ параметров материала в рантайме (`asda.Target.HLDump`).
	void DumpRuntimeState() const;
	// ★ 19.09.2026 (свечение, МЕЛК «подсветка ↔ пометки»): подсветка СЕЙЧАС держит CustomDepth этого меша
	//   (включала его сама — `bWriteCustomDepth` у настроек). Пока держит, покадровый проход пункта «Свечение»
	//   (UAsdaVideoSettingsSubsystem) трафарет этого меша не трогает — приоритет у подсветки цели; после снятия
	//   подсветка возвращает прежнее состояние и просит проход пересчитать пометку (RefreshGlowMarksFor).
	bool OwnsCustomDepth(const UMeshComponent* Mesh) const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void OnUnregister() override;

protected:
	// Собрать меш-компоненты владельца (с фильтром IgnoreComponentSubstrings).
	void CollectMeshes(TArray<UMeshComponent*>& Out) const;
	// Навесить/снять оверлей и CustomDepth.
	void ApplyOverlay();
	void RemoveOverlay();
	// Записать параметры стиля в MID.
	void PushParams(float PulseValue);
	const FAsdaHighlightStyle& CurrentStyle() const;

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Mid = nullptr;
	UPROPERTY(Transient) TArray<TWeakObjectPtr<UMeshComponent>> Painted;
	// ★ 19.09.2026 (ревизия свечения, С1): CustomDepth меша ДО подсветки — чтобы при снятии вернуть ровно его,
	//   а не безусловно выключать (снимало пометку трафарета «Свечения» 10/11 → моб мерцал).
	struct FDepthBefore
	{
		bool bChanged = false;    // подсветка сама включала CustomDepth этому мешу (bWriteCustomDepth)
		bool bWasOn = false;      // было ли CustomDepth включено до подсветки
		int32 Stencil = 0;        // значение трафарета до подсветки
	};
	// Параллельно Painted: что было у каждого меша до подсветки.
	TArray<FDepthBefore> PaintedDepth;
	UPROPERTY(Transient) TWeakObjectPtr<const UAsdaTargetHighlightSettings> ActiveSettings;

	EAsdaTargetRelation ActiveRelation = EAsdaTargetRelation::Neutral;
	bool  bActive = false;
	float PulsePhase = 0.f;
};
