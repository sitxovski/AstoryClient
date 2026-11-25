// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaLoadManagerSubsystem — СИСТЕМА загрузки карт (реверс map_load_system.md). По образу оригинала:
//   6004(успех)→очистка→загрузочный экран(картинка карты либо Start_logo + прогресс-бар + звук)→загрузка карты
//   (стадии 0-3 геометрия = async-load UE-уровня; стадии 4-11 = приём world-state = ФАЗА C)→BGM→в мире.
//   Порт: реестр карт = DT_MapTable (mapId→уровень); карта = экспортированный уровень Content/Map/<dir>.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "World/AsdaMapTable.h"
#include "UObject/ObjectKey.h"   // ★ charsel_preload: TObjectKey — владение флагом полных мипов
#include "Containers/Ticker.h"   // ★ 31.10.2026 затемнение: FTSTicker::FDelegateHandle (шаг переживает смену мира)
#include "AsdaLoadManagerSubsystem.generated.h"

class UUserWidget;
class UImage;
class UTextBlock;

UCLASS()
class ASDAREBORN_API UAsdaLoadManagerSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── Реестр карт ──
	// Строка карты по mapId (из DT_MapTable). nullptr если карты нет.
	const FAsdaMapRow* GetMapRow(int32 MapId) const;
	// Имя папки карты оригинала (g_aMapDirNames+0), напр. mapId 3 → "alpen". Пусто если нет.
	UFUNCTION(BlueprintCallable, Category="AsdaLoad")
	FString GetMapDirName(int32 MapId) const;
	// Отображаемое имя карты (для UI/миникарты).
	UFUNCTION(BlueprintCallable, Category="AsdaLoad")
	FText GetMapDisplayName(int32 MapId) const;

	// ── Загрузка карты ──
	// Полный вход на карту (реверс: экран+async-load+BGM). Зовётся по 6004-успеху / варпу.
	UFUNCTION(BlueprintCallable, Category="AsdaLoad")
	void BeginMapLoad(int32 MapId);

	// Показать загрузочный экран для карты (картинка по реверс-логике + прогресс 0). Без загрузки уровня.
	UFUNCTION(BlueprintCallable, Category="AsdaLoad")
	void ShowLoadingScreen(int32 MapId);
	// ★ 12.10.2026 (владелец): РАЗОВОЕ переопределение фона СЛЕДУЮЩЕГО загрузочного экрана — путь объекта текстуры
	//   (формат штатных картинок загрузки: контент в верхних 0.75). Ставит тест производительности (pt_RU/pt_EN по языку);
	//   снимается после первого показа, обычные загрузки карт не затрагивает. Пусто — штатный выбор (картинки карты).
	void SetNextLoadingTexture(const FString& TextureObjectPath) { NextLoadingTextureOverride = TextureObjectPath; }
	// Прогресс бара 0..1 (стадии загрузки/сети).
	UFUNCTION(BlueprintCallable, Category="AsdaLoad")
	void SetLoadProgress(float Frac01);
	// Убрать загрузочный экран (по завершении входа в мир).
	UFUNCTION(BlueprintCallable, Category="AsdaLoad")
	void HideLoadingScreen();

	// ★★★ ПРОГРЕВ КОНТЕНТА ПЕРЕД СНЯТИЕМ ЭКРАНА (задача владельца 2026-08-25: «модели/текстуры/
	//   материалы сначала мыльные на секундочку»). Причина — потоковая подкачка мип-уровней: UE
	//   показывает мир, как только готова ГЕОМЕТРИЯ, а мелкие мипы текстур доезжают уже под
	//   открытым миром. Здесь мы доводим подкачку ДО КОНЦА, пока экран ещё висит.
	//   Бюджет ожидания — `asda.Load.StreamWaitSec` (0 = выключить прогрев, вернуть прежнее).
	void WaitForStreamedContent();

	// ★★★ ПРЕДЗАГРУЗКА ЭКРАНА ВЫБОРА ПЕРСОНАЖА (задача владельца 13.09.2026 «на выборе персонажа видна прогрузка
	//   деталей», замеры `Reports/charsel_preload/`). Пути собирает логин (`AAsdaLoginGameMode::
	//   RequestCharSelectPreload`) теми же функциями, что строят превью; здесь — фоновая загрузка, заказ ПОЛНЫХ
	//   мипов/LOD всему загруженному (текстуры материалов мешей, явные текстуры, сами меши) и сильная ссылка до
	//   `ReleaseCharSelectPreload`. Повторный вызов с тем же набором — ничего не делает; с новым — новая ручка,
	//   прежняя живёт до её готовности. OnLoaded зовётся, когда всё в памяти. Ключ отката `asda.Login.CharSelPreload 0`.
	void StartCharSelectPreload(const TArray<FString>& ObjectPaths, FSimpleDelegate OnLoaded);
	// Отпустить ручку и снять заказ полных мипов с тех ассетов, кому его поставила предзагрузка.
	void ReleaseCharSelectPreload();
	// Ассеты текущей ручки (для прогрева PSO на стороне логина).
	void GetCharSelectPreloadedAssets(TArray<UObject*>& Out) const;
	// Включена ли предзагрузка (`asda.Login.CharSelPreload`).
	static bool IsCharSelectPreloadEnabled();

	// Висит ли сейчас загрузочный экран (полноэкранный, ZOrder 30000). Нужен зонной подсистеме:
	//   баннер названия локации показывается только после его снятия, иначе уходит под картинку.
	UFUNCTION(BlueprintPure, Category="AsdaLoad")
	bool IsLoadingScreenVisible() const { return LoadingWidget != nullptr; }

	// ★ 10.11.2026: путь выбранной картинки загрузки текущего перехода (пусто — не выбрана). Лог экрана и самотест
	//   `asda.Load.PictureSelfTest` (какая картинка у какой карты: своя или Start_logo).
	FString GetPictureTexPath() const;

	// ★★★ ЭКРАННОЕ ЗАТЕМНЕНИЕ СМЕНЫ КАРТЫ (31.10.2026, баг 5 «загрузка появляется и исчезает обрывом, на секунду видно
	//   небо без земли»). Эталон — объект затемнения `g_nMainLoopState`, кадровый шаг `uiColorFadeUpdate@0x97A020`
	//   (ui_core.c:1349): поле +3 = накопленное время, мс (не 0 = идёт), +9 = режим; длительность 1500 мс, линейно.
	//   Режимы, которые оригинал включает (`set_o9@0x9666D0`): 3 — картинка загрузки ПРОЯВЛЯЕТСЯ поверх кадра (переходы:
	//   6004/6203, 4055, порталы, каналы — 9 мест); 4 — картинка РАСТВОРЯЕТСЯ над готовым миром (конец загрузки,
	//   game_main.c:3431/3850); 0 — ЧЁРНЫЙ экран рассеивается (своё 4056 `GameNet_OnLocalPlayerMapEntry@0xF43390`,
	//   своё 6025 `char_onLocalPlayerMapEntry@0xF49090`). Разбор и решения — в шапке реализации (.cpp).
	enum class EAsdaScreenFade : uint8
	{
		None,        // затемнения нет
		PictureIn,   // режим 3: картинка проявляется (над застывшим последним кадром мира)
		Loading,     // экран загрузки непрозрачен (идёт загрузка)
		WaitReady,   // экран снят логически, картинка ещё непрозрачна — ждём первых кадров мира с камерой у персонажа
		PictureOut,  // режим 4: картинка растворяется над миром
		BlackOut,    // режим 0: чёрный экран рассеивается
	};
	// Режим 0 оригинала: чёрный экран рассеивается за `asda.Load.FadeSec` (своё воскрешение/перенос на месте, 6025 ГМ).
	//   Пока висит загрузка или растворяется её картинка — не включается (в оригинале его перекрывает режим 4 конца загрузки).
	void BeginBlackFadeOut(const TCHAR* Why);
	// ★ Ревизия 31.10: СНЯТЬ ЗАГРУЗКУ НЕМЕДЛЕННО (без растворения и без ожиданий подкачки) — обрыв/отказ сервера во время
	//   загрузки, уход на экран входа. Отменяет ожидаемый травел на целевую карту и предел ожидания 4003, снимает все слои.
	void AbortLoadingScreen(const TCHAR* Why);
	// Ключ `asda.Load.DisconnectDropsScreen`: обрыв во время загрузки снимает экран и показывает попап (1) / как было (0).
	static bool IsDisconnectDropsScreenOn();
	// Текущее состояние затемнения и непрозрачность его слоя 0..1 (для самотеста и журналов).
	EAsdaScreenFade GetScreenFadeState() const { return FadeState; }
	float GetScreenFadeAlpha() const { return FadeAlpha; }
	// Стоит ли виджет экрана загрузки (или растворяющейся картинки) во вьюпорте — проверка «экран пережил травел».
	bool IsLoadScreenOnViewport() const;
	// ★ ТЕСТОВЫЙ КРЮК самотеста `asda.Load.FadeSelfTest`: офлайн целевая карта ведёт себя как онлайн — экран НЕ снимается
	//   по загрузке уровня, его снимет 4003 (самотест эмулирует его `FakeSelfSpawn`). В игровом пути не взводится.
	bool bDebugHoldScreenAsOnline = false;

	// Текущая карта (в процессе/загружена).
	UPROPERTY(BlueprintReadOnly, Category="AsdaLoad")
	int32 CurrentMapId = -1;

private:
	// DT_MapTable (реестр). Грузится в Initialize.
	UPROPERTY() TObjectPtr<UDataTable> MapTable;
	// Загрузочный виджет (WBP_LoadingScreen — concrete class, наполняется в рантайме).
	UPROPERTY() TObjectPtr<UUserWidget> LoadingWidget;
	UPROPERTY() TObjectPtr<UImage> LoadingImage;   // полноэкранный фон = картинка загрузки (с ВШИТЫМ баром-фоном)
	UPROPERTY() TObjectPtr<UImage> LoadingFill;    // заливка прогресса ПОВЕРХ бара картинки (реверс uiRenderLoadingBar self+6)
	UPROPERTY() TObjectPtr<UTextBlock> LoadingText;

	// ── Экранное затемнение (см. EAsdaScreenFade) ──
	UPROPERTY() TObjectPtr<UImage> LoadingFrozen;          // застывший последний кадр мира под проявляющейся картинкой
	UPROPERTY() TObjectPtr<class UTexture2D> FrozenTex;    // текстура застывшего кадра (живёт до конца проявления)
	UPROPERTY() TObjectPtr<class UTexture2D> PictureTex;   // выбранная картинка загрузки (одна на весь переход)
	UPROPERTY() TObjectPtr<UUserWidget> FadingWidget;      // экран загрузки после снятия: картинка растворяется над миром
	UPROPERTY() TObjectPtr<UImage> FadingImage;            // слой картинки в FadingWidget
	UPROPERTY() TObjectPtr<UUserWidget> BlackWidget;       // чёрный слой режима 0
	UPROPERTY() TObjectPtr<UImage> BlackImage;             // его картинка (сплошной чёрный)
	EAsdaScreenFade FadeState = EAsdaScreenFade::None;     // текущий режим
	float FadeAlpha = 0.f;                                 // непрозрачность слоя затемнения 0..1
	double FadeElapsed = 0.0;                              // накопленное время режима, с (эталон +3, мс)
	uint64 ReadyFrame0 = 0;                                // GFrameCounter начала WaitReady
	int32 PictureMapId = -1;                               // карта, для которой выбрана PictureTex
	FTSTicker::FDelegateHandle FadeTicker;                 // шаг затемнения (тикер ядра: переживает смену мира)
	FDelegateHandle WorldReadyHandle;                      // подписка на «я в мире» (4055/4056) — режим 0 на своё 4056
	// Создать виджет экрана загрузки (если его нет) и поставить во вьюпорт; без выбора картинки.
	void EnsureLoadingWidget();
	// Поставить виджет во вьюпорт с ZOrder; `asda.Load.KeepScreenOnTravel` 1 — травел его не снимает.
	void AddScreenWidget(UUserWidget* W, int32 ZOrder);
	// Применить картинку загрузки к слою (кисть-текстура с кадрированием V 0..0.75 либо прежний материал M_LoadingBG).
	void ApplyPictureBrush(UImage* Img, class UTexture2D* Tex, bool bFadeCapable);
	// Снять застывший кадр текущего вьюпорта в FrozenTex (false — снять не удалось).
	bool CaptureFrozenFrame();
	// Начать проявление картинки над застывшим кадром (BeginMapLoad).
	void BeginPictureFadeIn(int32 MapId);
	// Шаг затемнения; false — снять тикер.
	bool TickScreenFade(float Dt);
	// Перевести экран загрузки в растворение (вызывается из HideLoadingScreen).
	void BeginPictureFadeOut();
	// Убрать все слои затемнения немедленно.
	void ClearScreenFade();
	// Обработчик «я в мире» сетевой подсистемы: своё 4056 → режим 0.
	void OnNetWorldReady();

	FString PendingLevelName;   // пакет целевого уровня (async-load на L_Loading)
	FString FinalShortName;     // короткое имя целевого уровня для отложенного OpenLevel (мин. показ экрана)
	int32 PendingTargetMapId = -1;   // целевая карта, ожидающая загрузки после травела на L_Loading
	FTimerHandle LoadProgressTimer;   // опрос GetAsyncLoadPercentage → плавная заливка бара
	FTimerHandle MinShowTimer;  // добор минимальной длительности загрузочного экрана
	FTimerHandle NetSyncTimeoutTimer; // ★ B3: страховка ожидания world-state после 4001 (эталон 120с → state16); ★ 31.10: таймер GameInstance
	double LoadShowTime = 0.0;  // FPlatformTime показа экрана (для гарантии минимума)
	FDelegateHandle PostLoadMapHandle;   // подписка на завершение травела (детект L_Loading/целевого)

	// ★★★ ПРЕДЗАГРУЗКА «ПЕРВОГО РАЗА» (задача load_hitch 11.09.2026, замеры `Reports/load_hitch/`).
	//   Первый показ персонажа и первое открытие инвентаря грузили ассеты СИНХРОННО прямо в кадре события:
	//   спавн — 55 пакетов за 122..222 мс (из них сетка высот карты ≈66 мс: её слабую ссылку в
	//   `AsdaGround` съедала сборка мусора при травеле), инвентарь — 265 мс из 278 мс на класс окна.
	//   Здесь те же ассеты запрашиваются АСИНХРОННО в момент показа загрузочного экрана — параллельно
	//   загрузке уровня, т.е. без удлинения ожидания. Ручка держит сильные ссылки до следующей загрузки
	//   карты, поэтому травел их не выгружает. Ключ отката: `asda.Load.FirstUsePreload 0`.
	TSharedPtr<struct FStreamableHandle> FirstUsePreloadHandle;
	// Запросить предзагрузку для карты MapId (зовётся из StartTargetLoad; повторный вызов заменяет ручку).
	void StartFirstUsePreload(int32 MapId);
	// Путь DA_WalkGrid карты — ОДНА точка для FinishLoadAndOpen и предзагрузки (пусто, если у карты нет папки).
	FString WalkGridObjectPath(int32 MapId) const;
	// Ждать ли (и сколько) фоновой компиляции PSO перед снятием экрана; в сборке редактора прекэш PSO выключен движком.
	void WaitForPsoPrecache(double Budget);

	// ── Предзагрузка экрана выбора персонажа (см. StartCharSelectPreload) ──
	TSharedPtr<struct FStreamableHandle> CharSelectPreloadHandle;   // текущий набор
	TSharedPtr<struct FStreamableHandle> CharSelectPreloadPrev;     // прежний набор — держится до готовности текущего
	TArray<FString> CharSelectPreloadKey;                           // отсортированные пути текущего набора (отсев повторов)
	TSet<TObjectKey<class UStreamableRenderAsset>> CharSelectForcedAssets;   // доля предзагрузки во флаге полных мипов (реестр UAsdaTextureSubsystem)
	int32 CharSelectPreloadGen = 0;         // номер последнего запроса (устаревшие колбэки отбрасываются)
	int32 CharSelectPreloadHandleGen = -1;  // номер запроса, чья ручка присвоена
	int32 CharSelectPreloadDoneGen = -1;    // номер запроса, который уже доведён (форс мипов + OnLoaded)
	// Довести готовый запрос: заказ полных мипов/LOD и OnLoaded (ровно один раз на запрос).
	void FinishCharSelectPreload(int32 Gen, double T0, int32 N, FSimpleDelegate OnLoaded);

	void PollLoadProgress();
	// Колбэк завершения травела: на L_Loading → StartTargetLoad; на целевом → снять экран.
	void OnPostLoadMap(UWorld* LoadedWorld);
	// На уровне L_Loading: показать экран + async-load целевой карты.
	void StartTargetLoad();
	// Финал загрузки: BGM + OpenLevel целевого (после добора мин. времени экрана).
	void FinishLoadAndOpen();

	// Выбор загрузочной текстуры (реверс `CMap_LoadLoadingScreen@0x975650`, 1:1): случайная картинка
	//   карты из `Textures/Maps/<dir>/load`, а если папка пуста — `Start_logo`. ★ РЕКЛАМНОЙ ВЕТКИ НЕТ:
	//   у оригинала её вероятность `g_nLoadingScreenChannel@0x15CB4C8` = 0 и записать её нечем
	//   (доказательство по байтам двух образов — в шапке реализации в .cpp).
	class UTexture2D* PickLoadingTexture(int32 MapId) const;
	// Разовое переопределение фона (см. SetNextLoadingTexture); читается PickLoadingTexture, стирается в ShowLoadingScreen.
	FString NextLoadingTextureOverride;
	// Колбэк завершения async-load пакета уровня → OpenLevel.
	void OnLevelPackageLoaded(const FName& PackageName, class UPackage* Package, EAsyncLoadingResult::Type Result);
	// BGM карты стартует UAsdaZoneSubsystem по зоне тайла, на котором стоит персонаж
	//   (эталон game_main.c:3404-3418: слот 0 «город» / слот 1 «поле» из заголовка .TER).
};
