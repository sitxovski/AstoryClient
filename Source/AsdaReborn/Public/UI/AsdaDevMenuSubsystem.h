// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaDevMenuSubsystem — dev-меню настроек графики (Ctrl+N), работает на ЛЮБОЙ карте (логин/мир).
//   GameInstance-подсистема регистрирует ЕДИНЫЙ Slate input-preprocessor: ловит Ctrl+N ДО UMG и ДО
//   PlayerInput — работает и в UIOnly (логин-сцена), и в GameAndUI/GameOnly (мир). Заменяет прежний
//   пер-картовый пикер логина (AsdaLoginGameMode::ToggleResolutionPicker — удалён, вызов общий).
//   Dev-инструмент, НЕ игровой JUI-экран (санкция юзера; закон no-invented-ui не распространяется).
//
//   ★ 12.10.2026 — СОКРАЩЕНИЕ МЕНЮ (решение владельца; вердикт дизайнера Reports/owner_1012/presets/01_designer.md §1,
//   грабли инженера 02_engineer.md §2.5/§3.2, дыры самотеста 03_tester.md §3.3; отчёт — 06_devmenu_cut.md).
//   Было 59 ручек в 7 вкладках → стало 28 в 6 (вкладка «Свет и тени» опустела и удалена):
//   • УШЛО В УРОВНИ (значение задаёт матрица Config/DefaultScalability.ini + BaseScalability, строки нет): VSM↔CSM,
//     дальность динамических теней ×, контактные тени, мягкость SMRT, отсев мелких теней, подробность VSM, бюджеты теней
//     локальных/эффект-светов, дальность локальных светов ×, Ambient Occlusion, группы «Листва» и «Шейдинг» (на ISM-мир
//     порта не действуют), анизотропия (→ «Текстуры»), «Рельеф (Enable)» и «Шероховатость из карты» ([AsdaQuality@N]).
//   • УБРАНО КАК ДУБЛЬ/ПУСТЫШКА: «Качество теней (sg)» (дубль группы «Тени»), Bloom (дубль «Свечения»), Lens flare
//     (решение 10.09 «блики выключены совсем»), капсульные тени во вкладке теней (одна строка — в «Персонажах»),
//     временной фильтр GTAO, смещение мипов, скин-кеш (всегда вкл), «AO/Делайтинг/Свечение из карты» (авторские, 0 = рекомендуемое).
//   • УБРАНО В КОНСОЛЬ (ключ жив, строки и пина нет): asda.Cull.Enable, строка-диагностика «множитель × ползунок» (asda.Cull.Dump),
//     asda.Vis.Enable, asda.Clouds.UpdateEvery, r.Fog, asda.Clouds.Shadows.
//   • ЗАМЕНЕНО: «Определить автоматически» зовёт ТОТ ЖЕ замер настоящего кадра, что «Автоподбор» окна настроек
//     (UAsdaBenchSubsystem::StartInPlace); синтетический пробник железа из меню убран.
//   • ГРАБЛЯ ПИНОВ: убранные ключи выкинуты из массивов персиста (список [AsdaVideo] GLiveCVarKeys опустел и удалён —
//     все оставшиеся r.*-строки ведёт профиль игрока; [AsdaRender] GRenderCVarKeys — 6 ключей) и из ResetTab; при старте
//     ApplySavedSettings один раз ЧИСТИТ устаревшие пины [AsdaVideo]/[AsdaRender]/[AsdaNormals] (SweepRetiredPins) с
//     записью «[меню] снят устаревший пин X=Y» в лог — иначе старые профили накатывали бы невидимые игроку значения.
//
//   ★ 05.09.2026 — ВКЛАДКИ (приказ владельца «разделим по категориям и вкладкам»). Панель = заголовок +
//   ряд кнопок-табов (одна активная, остальные поддеревья Collapsed) + прокрутка содержимого активной вкладки:
//   • «Экран»        — разрешение/оконный режим, масштаб рендера (50…200 %), лимит FPS, VSync, гамма, графический API (рестарт).
//   • «Качество»     — пресет (★ E-0b 06.09.2026: = уровень sg.* 0..3 честно, Cinematic 4 только консолью asda.Video.Preset),
//                      «Определить автоматически» (тест кадра), свечение LEVEL OF GLOW 0..4, 8 действующих групп sg,
//                      ползунок «Дальность прорисовки» asda.Cull.Scale (поверх множителя уровня), метод сглаживания,
//                      резкость Sharpen 0.6/0.9/1.2, motion blur.
//   • «Небо и туман» — ДВА ползунка-слайдера (USlider, стиль панели): «Плотность тумана» asda.Fog.DensityScale (0.25…2.0)
//                      и «Дальность тумана» asda.Fog.DistanceScale (0.5…2.0) поверх авторских значений
//                      ExponentialHeightFogComponent активной карты (накат при каждой загрузке карты и при движении
//                      ползунка); 3D-облака asda.Clouds.Field.
//   • «Рельеф»       — MPC_AsdaNormals: только NormalStrength 0.5/1/1.5 (остальные параметры — уровень/ассет).
//   • «Персонажи»    — рим (asda.Rim.Enable по карте/выкл, asda.Rim.Scale ×0.5/1/1.5 → MPC_AsdaToon через
//                      AAsdaRimLightActor::Apply), капсульная тень тел (asda.Shadow.Capsule + CapsuleMinVisibility 0.1/0.25/0.5).
//   • «Сброс»        — сброс каждой вкладки к дефолтам проекта и сброс всего.
//
//   ★ СБРОС (05.09.2026, ревизия review_devmenu_tabs.md В-3/В-5): «дефолт проекта» НЕ зашит в код — читается из merged
//   Engine.ini ([/Script/Engine.RendererSettings] → [SystemSettings] → дефолт кода CVar), см. ProjectDefault(). Сброс CVar =
//   удалить ключ из [AsdaVideo]/[AsdaRender] + Unset(ECVF_SetByConsole) (история приоритетов возвращает значение
//   проекта/пресета); ключи, которыми управляет sg-пресет (GPresetManagedCVarKeys в .cpp), НЕ пришпиливаются значением,
//   а получают его повторным применением пресета (UAsdaVideoSettingsSubsystem::ReapplyQuality).
//
//   ПЕРСИСТ (GameUserSettings.ini): asda.*-CVar → [AsdaRender] (ключ = имя CVar), параметр MPC_AsdaNormals → [AsdaNormals]
//   (ключ = имя параметра), активная вкладка → [AsdaVideo] DevMenuTab. r.*-ключи оставшихся строк (метод AA, резкость,
//   размытие, масштаб рендера) и asda.Clouds.Field/asda.Cull.Scale — ПОЛЯ ПРОФИЛЯ игрока (UAsdaGameUserSettings), пишутся
//   теми же функциями, что окно настроек 69500 (SetRenderOverride). Накат: [AsdaRender] — при старте (ApplySavedSettings
//   в Initialize), параметры на акторах/коллекциях мира (туман, MPC) — при каждой загрузке карты (PostLoadMapWithWorld);
//   облака/рим читают свои asda.*-ключи сами в BeginPlay/Apply своих акторов. Разрешение/качество/VSync/FPS — штатный UGameUserSettings.
//
//   «ТРЕБУЕТ ПЕРЕЗАПУСКА»: графический API DX12↔DX11 — [D3DRHIPreference] PreferredRHI в GameUserSettings.ini (штатный
//     механизм UE: WindowsDynamicRHI.cpp::ChoosePreferredRHI читает эту секцию; значения "dx12"/"dx11").
//
//   САМОПРОВЕРКА путём игрока: консольная команда `asda.DevMenu.SelfTest <задержкаСек> <режим> [quit]` (см. .cpp):
//   shots|toggle|verify|rows|pins|reset|clouds|fogdist|slider|culldrag. ★ 12.10.2026: каждый режим считает ok/fail и
//   кончается строкой «САМОТЕСТ-МЕНЮ ИТОГ: ok=N fail=M» (AsdaSelfTestKit::Finish, как asda.Bench.SelfTest); ненайденная
//   строка меню = ПРОВАЛ, а не Warning.
//
//   КОНФЛИКТ БИНДА: Ctrl+N в оригинале = cmd 67 CHAT NORMAL (AsdaKeybindConfig {67,78,2}) —
//   переключатель канала чата, нужен ТОЛЬКО при наборе текста. Разруливание (решение юзера:
//   dev-меню приоритетнее, игровую функцию не терять):
//   • фокус в текстовом поле (чат/инпут) → препроцессор ПРОПУСКАЕТ Ctrl+N (игровой CHAT NORMAL
//     дойдёт до будущей чат-системы; в поле ничего не печатается — N с Ctrl не символ);
//   • вне текстового фокуса → Ctrl+N открывает/закрывает dev-меню (событие поглощается, до
//     PlayerInput не доходит — cmd 67 не стреляет дублем);
//   • пока меню открыто → игровые хоткей-команды подавлены (AsdaPlayerController::PollKeys
//     проверяет IsMenuOpen), движение оставлено (удобно сравнивать графику на бегу). Esc закрывает.
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Scalability.h"   // ★ E-0b: Scalability::FQualityLevels в сигнатуре OnScalabilityChanged
#include "AsdaDevMenuSubsystem.generated.h"

class UUserWidget;
class UTextBlock;
class UVerticalBox;
class UButton;
class USlider;
class UWidgetTree;
class UAsdaButtonRelay;
class UMaterialParameterCollection;
class UExponentialHeightFogComponent;

// Вкладки dev-меню (порядок = порядок кнопок-табов слева направо).
// ★ 12.10.2026: вкладка «Свет и тени» удалена — все её строки ушли в уровни или убраны как дубли (см. шапку).
//   Сохранённый [AsdaVideo] DevMenuTab старых профилей зажимается в Count−1 (ApplySavedSettings).
enum class EAsdaDevTab : uint8
{
	Screen = 0,   // «Экран»
	Quality,      // «Качество»
	Sky,          // «Небо и туман»
	Relief,       // «Рельеф»
	Chars,        // «Персонажи»
	Reset,        // «Сброс»
	Count
};

UCLASS()
class ASDAREBORN_API UAsdaDevMenuSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void ToggleMenu();                                   // Ctrl+N: открыть/закрыть
	void CloseMenu();                                    // Esc / повторный Ctrl+N
	// ★ 25.10.2026 (ui_toggles) [ИСПРАВЛЕНИЕ]: «открыто» = панель ВО ВЬЮПОРТЕ, а не «указатель есть». Травел UE снимает все
	//   виджеты вьюпорта (`UEngine::LoadMap` → `RemoveAllViewportWidgets`), а панель создана на GameInstance и указатель
	//   переживал карту: после смены карты меню числилось открытым — Ctrl+N «закрывал» невидимое (открывалось со второго раза),
	//   Esc съедался, игровые хоткеи `PollKeys` были подавлены. Ключ `asda.DevMenu.ToggleFromFact 0` — прежнее. Реализация — в .cpp.
	bool IsMenuOpen() const;
	bool DebugHasMenuWidget() const { return MenuWidget != nullptr; }   // самотест: указатель панели жив (даже вне вьюпорта)

	// ★ Совпало ли событие клавиатуры с биндом «EXT DEV MENU:» (cmd 91) из
	//   `system/Shortcutkey.ini`? Раньше клавиша была зашита (CTRL+N) — теперь данные из файла,
	//   владелец переназначает без пересборки. Дефолт прежний.
	bool MatchesDevMenuKey(const struct FKeyEvent& Ev) const;

	// Сбросить ВСЁ (все вкладки) к дефолтам проекта: CVar — из merged Engine.ini (ProjectDefault), профильные ключи — «не менялось»
	// (ClearRenderOverride), пресетные — переприменением пресета, остальное — DX12, гамма 2.2, High + glow 4 БЕЗ автопробника
	// (★ В-1: сброс не гоняет SynthBenchmark), desktop-WFS, рельеф = значение уровня/ассета.
	void ResetToProjectDefaults();

	// Сбросить ОДНУ вкладку к дефолтам проекта (кнопки вкладки «Сброс»).
	void ResetTab(EAsdaDevTab Tab);

	// Переключить активную вкладку (кнопки-табы; персист [AsdaVideo] DevMenuTab).
	void SetActiveTab(EAsdaDevTab Tab);
	EAsdaDevTab GetActiveTab() const { return ActiveTab; }

	// Самопроверка путём игрока (asda.DevMenu.SelfTest): печатает в лог геометрию панели и ширину подписей
	// каждой вкладки против доступной ширины; возвращает число подписей шире поля (переносятся на 2 строки), −1 = меню закрыто.
	int32 LogRowWidths();
	// Самопроверка: «кликнуть» строки панели по подстроке подписи (тот же обработчик, что у UButton::OnClicked).
	//   ★ 12.10.2026: возвращает число НЕнайденных строк; каждая ненайденная = ПРОВАЛ в счётчике итога (раньше — Warning).
	int32 SelfTestClickRows(const TArray<FString>& Needles);
	// Самопроверка: подпись первой строки, содержащей Needle (пусто — строки нет). Для сравнения «до/после клика» (режим toggle).
	FString SelfTestRowLabel(const FString& Needle) const;
	// ★ 12.10.2026 — режим rows: инвентарь подписей по вкладкам в лог + ожидания НОВОГО состава (каждая ожидаемая строка
	//   есть, каждая убранная отсутствует, у каждого ключа [AsdaRender] есть строка-ручка, подписей шире поля нет).
	void SelfTestRows();
	// ★ 12.10.2026 — режим pins: для каждой строки с консольным ключом клик/жест → ключ лёг туда, куда пишет
	//   ApplyCVarPersist ([AsdaRender] или профиль игрока, и только туда) → имитация перезапуска (Unset + ApplySavedSettings:
	//   пин вернулся) → ResetTab всех вкладок: ключей в ini нет, SetBy не Console; «пин без ручки» — ПРОВАЛ. Done — по окончании.
	void SelfTestPins(TFunction<void()> Done);
	// ★ 12.10.2026: накат сохранённых пинов [AsdaRender] (то, что делает старт игры) — для имитации перезапуска в режиме pins.
	void SelfTestReapplySaved() { ApplySavedSettings(); }
	// Дефолт проекта для CVar: merged Engine.ini [/Script/Engine.RendererSettings] → [SystemSettings] → дефолт кода CVar
	// (IConsoleVariable::GetDefaultValue). Зашитых чисел нет: DefaultEngine.ini — единственный источник (например r.AntiAliasingMethod: на 14.09.2026 это 1 = FXAA).
	// OutSource (опц.) — откуда взято, для лога/самопроверки. "True"/"False" нормализуются в "1"/"0". ★ 12.10.2026 публичный — его читают проверки самотеста.
	static FString ProjectDefault(const FString& Name, FString* OutSource = nullptr);

	// Пересчитать туман во всех игровых мирах этого GameInstance (для обоих ключей asda.Fog.DensityScale / asda.Fog.DistanceScale).
	void ApplyFogScaleAllWorlds();

	// ★ 16.10.2026 (жалоба владельца «HolyLight_SpotLight и такие же источники не рендерятся ни на одном пресете»):
	//   множитель объёмного рассеяния локальных светов. Яркость луча в объёмном тумане ∝ плотность тумана ×
	//   VolumetricScatteringIntensity, а в игре плотность = авторская × DensityScale / DistanceScale (×0.135 при подборе
	//   владельца 09.09) — луч слабел в 7.4 раза. Возвращает DistanceScale / DensityScale при asda.Fog.VolumetricLightComp 1,
	//   иначе 1. Вне игрового мира (редактор, превью) — 1: там туман авторский, множитель плотности не применяется.
	//   Зовут все места, где свет получает рассеяние: эффект-свет, AAsdaLocalLightActor, обход ApplyFogScale.
	static float FogVolumetricLightScale(const UWorld* World);

	// ★ Волна 3, хвосты (15.09.2026, аудит 04 (д)3): читатель уровневых ключей Scalability-ini (бывший статик E-4 07.09
	//   ScalabilityLevelValue) перенесён в UAsdaRenderQualitySubsystemBase::ScalabilityLevelValue — меню зовёт его оттуда.
	// Колбэк смены ЛЮБОГО из ключей тумана (консоль, ini-накат, ползунок, сброс): пересчёт тумана + при открытой панели
	// обновить строки (подпись И ручку ползунка — ревизия review_devmenu_fog_slider.md В-1). Пока значение ставит сам
	// ползунок (bFogSliderLive) — RefreshRows не зовётся (ручка уже на месте), лог наката понижен до Verbose (М-2).
	void OnFogKeyChanged();
	// Самопроверка: напечатать в лог положение ручки каждого ползунка против ожидания из текущего CVar (проверка В-1).
	//   ★ 12.10.2026: возвращает число расхождений ручка/ожидание (0 = все ручки на месте).
	int32 SelfTestLogSliders(const TCHAR* Tag);
	// Самопроверка клавиатурного пути (М-1): у ползунка с подстрокой Needle в подписи поставить ручку и стрельнуть OnValueChanged
	// БЕЗ захвата — как стрелка клавиатуры без Enter; ожидается отложенный персист в [AsdaRender] через 0.6 с.
	void SelfTestSliderKeyboard(const FString& Needle, float T01);
	// ★★★ Самопроверка ПЕРЕТАСКИВАНИЯ ползунка (ревизия review_drawdistance.md §6: дефект «полный обход мира на каждый кадр
	//   перетаскивания» прошёл мимо самопроверки именно потому, что она ползунки только РИСОВАЛА). Phase: 0 = захват ручки
	//   (+первое значение), 1 = движение с захватом, 2 = отпускание. Шлёт ровно те делегаты, что SSlider при работе мышью.
	void SelfTestSliderDrag(const FString& Needle, float T01, int32 Phase);

private:
	void BuildMenu();                                    // построить рантайм-панель (стиль прежнего пикера)
	void RefreshRows();                                  // обновить подписи всех строк и ручки ползунков (SetValue из текущего CVar) после клика/сброса/консоли
	// ★ USlider::SetValue в UE 5.8 СТРЕЛЯЕТ OnValueChanged, если значение изменилось (Slider.cpp:159 HandleOnValueChanged) —
	//   без этого флага RefreshRows после консоли/сброса запускал колбэк ползунка и отложенный персист по кругу.
	bool bSliderSetFromCode = false;                     // true внутри RefreshRows на время SetValue: колбэк ползунка молчит
	void ApplySavedSettings();                           // чистка устаревших пинов + накат [AsdaRender]-оверрайдов при старте игры
	void OnPostLoadMap(UWorld* World);                   // накат параметров мира (туман, MPC_AsdaNormals) при загрузке карты
	// ★ 12.10.2026: кнопка «Определить автоматически» — закрыть панель и запустить замер кадра на месте
	//   (UAsdaBenchSubsystem::StartInPlace, тот же путь, что «Автоподбор» окна настроек). Зовётся отложенно (следующий тик),
	//   чтобы не рушить панель из обработчика её же кнопки.
	void StartFrameBenchFromMenu();

	// Живой CVar: применить (ECVF_SetByConsole — перекрывает [SystemSettings]/ProjectSetting из ini)
	// + персист: профильные ключи → поле профиля (SetRenderOverride), asda.* → [AsdaRender], прочие r.* → [AsdaVideo] (ключ = имя CVar).
	void ApplyCVarPersist(const FString& Name, const FString& Value);
	// Гамма: «Яркость» профиля игрока (та же полоса 70058 окна настроек), см. реализацию.
	void ApplyGammaPersist(float Gamma);

	// ── Туман: два множителя поверх авторских значений актора карты (кэшируются при первом касании компонента) ──
	//   asda.Fog.DensityScale (плотность) и asda.Fog.DistanceScale (дальность начала тумана). Совместно:
	//     StartDistance = авторская × DistanceScale;  FogDensity = авторская × DensityScale / DistanceScale
	//   (при увеличении дальности плотность падает — та же насыщенность достигается на большем расстоянии).
	void ApplyFogScale(UWorld* World);                   // накат обоих множителей на все ExponentialHeightFogComponent мира
	bool bFogSliderLive = false;                         // true только внутри Set() из колбэка ползунка: тише лог, без RefreshRows
	// Отложенный персист значения ползунка, пришедшего БЕЗ захвата (клавиатура/геймпад без Enter — OnMouseCaptureEnd не стреляет,
	// ревизия М-1): через 0.6 с после последнего изменения, если новых изменений не было (номер серии совпал).
	void ScheduleSliderPersist(const FString& Name, float Def);
	uint32 SliderPersistSeq = 0;                         // номер серии клавиатурных изменений (каждое изменение отменяет прежний таймер)
	TMap<TWeakObjectPtr<UExponentialHeightFogComponent>, float> FogBaseDensity;        // авторская плотность (до множителей)
	TMap<TWeakObjectPtr<UExponentialHeightFogComponent>, float> FogBaseStartDistance;  // авторская дальность начала (до множителей)
	TMap<TWeakObjectPtr<UExponentialHeightFogComponent>, bool> FogBaseVolumetric;      // ★ 16.10.2026: авторский bEnableVolumetricFog (до asda.Fog.Volumetric)
	TMap<TWeakObjectPtr<class ULocalLightComponent>, float> LightBaseVolScatter;      // ★ 16.10.2026: авторское рассеяние светов уровня (не эффект-свет, не AAsdaLocalLightActor)

	// ── Рельеф: MPC_AsdaNormals ──
	UMaterialParameterCollection* GetNormalsMPC();       // /Game/Materials/MPC_AsdaNormals (ленивая загрузка)
	// ★ E-0b (06.09.2026): база = [AsdaQuality@N] Normals.<Параметр> из Config/DefaultScalability.ini (N = sg.EffectsQuality; Low: Enable 0,
	//   Medium: RoughFromMap 0), поверх — пользовательский [AsdaNormals] (с 12.10.2026 — только NormalStrength); нет ключей → дефолт ассета.
	void ApplyNormals(UWorld* World);
	static int32 GetAsdaQualityLevel();                  // уровень нашего per-level слоя [AsdaQuality@N] = sg.EffectsQuality (0..4)
	void OnScalabilityChanged(const Scalability::FQualityLevels& Levels);   // Scalability::OnScalabilitySettingsChanged → ApplyNormals во все игровые миры
	FDelegateHandle ScalabilityChangedHandle;            // подписка на смену уровней sg.* (снимается в Deinitialize)
	void SetNormalPersist(const FName& Param, float Value);   // живьём в текущий мир + персист [AsdaNormals]
	float GetNormalValue(const FName& Param);            // текущее значение из экземпляра мира (или дефолт ассета)
	UPROPERTY() TObjectPtr<UMaterialParameterCollection> NormalsMPC;   // ассет коллекции (GC-якорь ленивой загрузки GetNormalsMPC)

	// ── Сброс к дефолтам проекта (05.09.2026, review_devmenu_tabs.md В-3/В-5); ProjectDefault — в public выше ──
	// CVar, значение которого выставляет sg-пресет (BaseScalability.ini) и которое НЕ пришпилено проектным ini
	// (ProjectSetting/SystemSettingsIni сильнее Scalability): для него сброс = снять оверрайд + переприменить пресет, а не Set(...).
	static bool IsPresetManaged(const FString& Name);
	// Сброс одного CVar: удалить ключ из [AsdaVideo]/[AsdaRender], Unset(ECVF_SetByConsole); если ключ не пресетный и история
	// не вернула дефолт проекта — поставить его. Возвращает true, если ключ управляется пресетом (вызывающий переприменит пресет
	// ОДИН раз в конце). Flush ini делает вызывающий.
	bool ResetCVar(const FString& Name);

	// ── «Требует перезапуска» (графический API) ──
	static bool   GetPendingRHIIsDx12();                 // [D3DRHIPreference] PreferredRHI (нет ключа = дефолт проекта DX12)
	static bool   GetActiveRHIIsDx12();                  // GDynamicRHI (что реально запущено)
	static void   SetPendingRHI(bool bDx12);             // dx12/dx11 → GameUserSettings.ini [D3DRHIPreference]

	UPROPERTY() TObjectPtr<UUserWidget> MenuWidget;      // открытая панель (nullptr = закрыто)
	UPROPERTY() TArray<TObjectPtr<UAsdaButtonRelay>> Relays;   // GC-якоря обработчиков кнопок

	// Строка меню с живой подписью: TextBlock + генератор текста (RefreshRows обновляет все) + вкладка (для отчёта ширин).
	// У строк-ползунков дополнительно сам USlider и генератор его нормированного положения 0..1 из текущего CVar —
	// RefreshRows ставит ручку SetValue под флагом bSliderSetFromCode (SetValue стреляет OnValueChanged). У кнопок Slider пуст.
	struct FRow
	{
		TWeakObjectPtr<UTextBlock> Text;                 // живая подпись строки
		TFunction<FString()> Label;                      // генератор текста подписи из текущего состояния
		EAsdaDevTab Tab;                                 // вкладка строки (отчёт ширин, сброс)
		TWeakObjectPtr<USlider> Slider;                  // ползунок строки (только у AddSlider)
		TFunction<float()> SliderT01;                    // положение ручки 0..1 = (CVar − Min)/Range (только у AddSlider)
	};
	TArray<FRow> Rows;
	TArray<int32> RowRelayIndex;                         // Rows[i] → индекс её UAsdaButtonRelay в Relays (для SelfTestClickRows)

	// Вкладки: контейнер содержимого + кнопка-таб + её подпись (индекс = EAsdaDevTab).
	TArray<TWeakObjectPtr<UVerticalBox>> TabBoxes;
	TArray<TWeakObjectPtr<UButton>> TabButtons;
	TArray<TWeakObjectPtr<UTextBlock>> TabTexts;
	EAsdaDevTab ActiveTab = EAsdaDevTab::Screen;         // активная вкладка (восстанавливается из [AsdaVideo] DevMenuTab)
	float PanelContentWidth = 0.f;                       // ширина области содержимого (от ширины вьюпорта; табы переносятся по ней)
	float PanelMaxContentHeight = 600.f;                 // макс. высота прокрутки содержимого (от высоты вьюпорта, ревизия М-6)

	FDelegateHandle PostLoadMapHandle;                   // подписка FCoreUObjectDelegates::PostLoadMapWithWorld
	TSharedPtr<class FAsdaDevMenuKeyProcessor> KeyProc;  // Slate-перехват Ctrl+N/Esc (один на GameInstance)
};
