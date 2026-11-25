// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaVideoSettingsSubsystem — система РАЗРЕШЕНИЯ/МАСШТАБА (по образу оригинала, RESOLUTION_SYSTEM_RE.md).
//   Оригинал: config.txt RESOLUTION (дефолт 1024×768 32 -1, WINDOW MODE TRUE), бэкбуфер = размер окна 1:1,
//   POINT-фильтр; список разрешений — оконный любой + адаптер-режимы (EnumAdapterModes). База UI 1024×768.
//   Порт: даёт ДОСТУПНЫЕ разрешения (оригинал-набор 4:3+16:9 + поддерживаемые адаптером) и применяет их;
//   на старте — WindowedFullscreen на нативном разрешении (viewport заполняет окно, нет чёрных полей) + полное
//   качество (иначе рендер мелкий/мыльный). UI масштабирует DPI-кривая проекта (ShortestSide база768).
//
//   ★ Сет E-0b (06.09.2026, setE_plan_addendum_tiers_and_lights.md §0/§1.3, отчёт setE0_report.md) — ЧЕСТНЫЕ УРОВНИ:
//   • пресет = ровно уровень sg.* 0/1/2/3 (Low/Medium/High/Epic); 4 = Cinematic (@Cine BaseScalability: VSM 8192 стр., TSR 200 %,
//     ViewDistanceScale 10) — в меню НЕ показывается, только консолью `asda.Video.Preset cine|4` (раньше Epic=4 и первый запуск
//     стартовал в Cinematic даже на iGPU — факт F3 дополнения);
//   • ★ 12.10.2026 (решение владельца): первый запуск = пресет ПО УМОЛЧАНИЮ `kDefaultPreset` = High (2, в окне настроек
//     «Высокое») БЕЗ пробника железа; пробник — только по ключу `asda.Video.AutoDetect 1` (opt-in) или кнопкой
//     «Определить автоматически». До 12.10 первый запуск = автопробник UGameUserSettings::RunHardwareBenchmark/
//     ApplyHardwareBenchmarkResults (пороги [ScalabilitySettings] PerfIndexThresholds_* BaseScalability.ini), без него — Epic;
//   • значения уровней живут ТОЛЬКО в Config/DefaultScalability.ini (проектный слой поверх BaseScalability) — подсистема ничего не
//     дублирует числом; наш per-level слой `[AsdaQuality@N]` (рельеф MPC_AsdaNormals) накатывает UAsdaDevMenuSubsystem по
//     Scalability::OnScalabilitySettingsChanged.
//
//   ★ КАК ВКЛЮЧИТЬ АВТОПРОБНИК ПЕРВОГО ЗАПУСКА (с 12.10.2026 выключен по умолчанию; В-2, review_setE0.md: на ноутбуке с
//     гибридной графикой замер может попасть на iGPU и занизить уровень). Работают (флаг читается ДО пробника):
//       • GameUserSettings.ini `[AsdaVideo] asda.Video.AutoDetect=1` (наш персист, per-user);
//       • DefaultEngine.ini `[SystemSettings]` или `[ConsoleVariables] asda.Video.AutoDetect=1` (применяются движком РАНО, до GameInstance);
//       • командная строка `-dpcvars=asda.Video.AutoDetect=1`.
//     НЕ работает: `-ExecCmds="asda.Video.AutoDetect 1"` и ручная консоль — они выполняются ПОСЛЕ InitializeStandalone,
//     где дефолт уже применён и записан маркер [AsdaVideo] bInitialized. После первого запуска пресет меняют окном настроек/Ctrl+N.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Scalability.h"   // Scalability::FQualityLevels (SingleLevelIgnoringResolution)
#include "Containers/Ticker.h"   // ★ свечение: отложенное усыпление запечённых мобов (ResleepBakedMobs)
#include "AsdaVideoSettingsSubsystem.generated.h"

// Пресет качества рендера. Оригинал (RENDER_QUALITY_FIELDS_RE.md) — не единый пресет, а набор фича-тоглов
//   (config.txt + FxConfig.ini); auto-preset CConfig_ApplyAutoGraphicsPreset даёт по сути Low/High по объёму RAM.
//   Порт (санкция юзера «использовать UE-инструменты») кладёт качество на UE-scalability группы (sg.*).
//   ★ E-0b: значение enum == уровень sg.* (0..4); Cinematic скрыт из меню.
UENUM(BlueprintType)
enum class EAsdaQualityPreset : uint8
{
	Low       = 0,   // sg.* = 0: CSM 1 каскад 512², без AO/контактных/капсульных теней, без отражений (DefaultScalability @0)
	Medium    = 1,   // sg.* = 1: CSM 1 каскад 1024², GTAO, SSR, контакт/капсулы (@1)
	High      = 2,   // sg.* = 2: VSM 2048 стр., 4 каскада фолбэка (@2)
	Epic      = 3,   // sg.* = 3: VSM 4096 стр. — картинка проекта до E-0b (@3)
	Cinematic = 4    // sg.* = 4: @Cine BaseScalability — только консолью asda.Video.Preset cine (в меню не показывается)
};

UCLASS()
class ASDAREBORN_API UAsdaVideoSettingsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// ★ Уровень свечения: снять подписку OnWorldInitializedActors (иначе лямбда переживёт подсистему).
	virtual void Deinitialize() override;

	// Доступные разрешения для меню настроек: оригинал-набор (4:3 1024×768/1280×960/1600×1200 + 16:9
	//   1280×720/1600×900/1920×1080) ∪ поддерживаемые адаптером, ≥1024×768, дедуп, сортировка по площади.
	UFUNCTION(BlueprintCallable, Category="AsdaVideo")
	TArray<FIntPoint> GetAvailableResolutions() const;

	// Применить разрешение + режим окна (0=Fullscreen, 1=WindowedFullscreen, 2=Windowed) + сохранить.
	UFUNCTION(BlueprintCallable, Category="AsdaVideo")
	void ApplyResolution(FIntPoint Resolution, int32 WindowMode);

	// Текущее разрешение вьюпорта.
	UFUNCTION(BlueprintCallable, Category="AsdaVideo")
	FIntPoint GetCurrentResolution() const;

	// Нативное разрешение рабочего стола.
	UFUNCTION(BlueprintCallable, Category="AsdaVideo")
	FIntPoint GetDesktopResolution() const;

	// Применить дефолт: WindowedFullscreen на разрешении рабочего стола + качество первого запуска (автопробник, см. ApplyDefaultQuality).
	UFUNCTION(BlueprintCallable, Category="AsdaVideo")
	void ApplyDefaultVideo();

	// Дефолтное разрешение оригинала (config.txt): 1024×768.
	UFUNCTION(BlueprintCallable, Category="AsdaVideo")
	static FIntPoint GetOriginalDefaultResolution() { return FIntPoint(1024, 768); }

	// ───────────────────── КАЧЕСТВО (маппинг оригинала → UE-scalability, RENDER_QUALITY_FIELDS_RE.md) ─────────────────────
	// Общий пресет: SetOverallScalabilityLevel(0..4). Применяет все sg.*-группы разом + сохраняет.
	UFUNCTION(BlueprintCallable, Category="AsdaVideo|Quality")
	void SetQualityPreset(EAsdaQualityPreset Preset);

	// Текущий пресет по ЖИВОМУ состоянию Scalability::GetQualityLevels() через SingleLevelIgnoringResolution (не по копии UGameUserSettings —
	//   иначе консольная `Scalability N` не отражалась бы в меню). Группы вразнобой → IsQualityMixed()=true и ближайший вверх по максимуму групп.
	UFUNCTION(BlueprintCallable, Category="AsdaVideo|Quality")
	EAsdaQualityPreset GetQualityPreset() const;

	// Группы вразнобой? (после ручной правки отдельных sg-групп в Ctrl+N) — меню пишет «смешанный».
	UFUNCTION(BlueprintCallable, Category="AsdaVideo|Quality")
	bool IsQualityMixed() const;

	// Уровень sg.* пресета (значение enum) — единая точка маппинга, чтобы 4 = Cinematic нигде не расходилось.
	static int32 PresetToLevel(EAsdaQualityPreset P) { return FMath::Clamp((int32)P, 0, 4); }
	// Единый уровень 11 групп без учёта ResolutionQuality (движковый GetSingleQualityLevel требует ещё масштаб = PerfIndexValues); -1 = вразнобой.
	static int32 SingleLevelIgnoringResolution(const Scalability::FQualityLevels& L);
	// Имя пресета для меню/лога (Low/Medium/High/Epic/Cinematic).
	static const TCHAR* PresetName(EAsdaQualityPreset P);

	// Индивидуальные группы качества (Level 0=Low..4=Cinematic). Каждая = поле(я) оригинала:
	void SetTextureQuality(int32 Level);        // TEXTURE DETAIL (LOW/MID/HIGH) + FORCE TEXTURE MIN + FXMipFilter
	void SetShadowQuality(int32 Level);         // SHADOW USE + FXShadow (g_bShadowMapEnable)
	void SetEffectsQuality(int32 Level);        // эффекты частиц (плотность/детализация)
	void SetPostProcessQuality(int32 Level);    // пост-обработка (glow/bloom база) + FXShock
	void SetFoliageQuality(int32 Level);        // GRASS RENDER + TERRAIN SHADER
	void SetViewDistanceQuality(int32 Level);   // дальность моделей (SHOW OTHER CHA / LOD)
	void SetAntiAliasingQuality(int32 Level);   // FXAntiAlias (MSAA→UE AA)
	void SetReflectionQuality(int32 Level);     // WATER SHADER + FXWater (g_bWaterReflectEnable)
	// ★ E-0b: группы, которых у оригинала нет (UE-сущности): глобальное освещение (выкл/DFAO/Lumen/Lumen+ — секции
	//   [GlobalIlluminationQuality@N] подготовлены в DefaultScalability.ini, метод Lumen включит E-5) и шейдинг (r.Substrate.*).
	void SetGlobalIlluminationQuality(int32 Level);
	void SetShadingQuality(int32 Level);
	// ★ М-2 (review_setE0.md): один вызов для ВОСЬМИ групп-полей оригинала (Level 0..4). Группы UE, которых у оригинала нет —
	//   GlobalIllumination и Shading — сюда НЕ входят намеренно (их ставит пресет/отдельные сеттеры); не «все группы сразу».
	UFUNCTION(BlueprintCallable, Category="AsdaVideo|Quality")
	void SetGroupQuality(int32 TextureL, int32 ShadowL, int32 EffectsL, int32 PostProcessL,
		int32 FoliageL, int32 ViewDistL, int32 AntiAliasL, int32 ReflectionL);

	// LEVEL OF GLOW EFFECT оригинала (0..4, config[32]).
	//   ★★★ 19.09.2026 (фаза 2 окна настроек, решение владельца №1): СМЫСЛ ПУНКТОВ — ПО ЭТАЛОНУ, «НА ЧЁМ светится».
	//   Разбор (эталон `RE/etalon/effects_particles.c` + `render_core.c`): уровень кладётся в поле +4 объекта
	//   свечения `g_pRenderer_F658` (`effectQuad_SetTargetCountAndBuild@0xF5E700`), и дальше его читают ТРИ места:
	//     • `sceneRenderShadowLightPass@0xF5FCC0` рисует в буфер свечения при 1 и 3 — первую группу (флаг
	//       `Input_TestPressMask(1)`), при 2 и 3 — мобов (`sub_9256E3(g_pMobMgr)` → список 0x34A0364, флаг 2).
	//       ★★ 19.09.2026 (состав первой группы, доказательство — дизассемблер сборки IDB @0xF5FD79…0xF5FDCE и
	//       скан .text на адреса списков): ровно ДЕВЯТЬ списков, и все они — персонаж игрока и то, что к нему
	//       привязано: 0x29080FC части тела/экипировка/волосы/езда/стрелы/эффекты предметов (.PEB) и сцены
	//       логина/выбора; 0x35180C0 `CCharActor` (хранитель/превращение, `charMgr_createSummonActor`);
	//       0x351C468 «душа» (soul_base_idle01); 0x3518CC0 питомец (`mapDecoModelGetOrCreate`, g_PetDecoModelId);
	//       0x2BDDA98 автоматы гачи; лавки игроков `g_pShopMgr`; 0x35198C0 флаги клана/метки над игроками
	//       (`uiClanFlagGetOrCreate`); три списка `g_pMap` (рыбалка: зона/удочка, превью коллекции) и
	//       0x3524ECC превью оружия. ПОСТРОЕК/ДЕРЕВЬЕВ/КАМНЕЙ (маска 256, `mapPatchRenderSkinMeshes`),
	//       РЕЛЬЕФА (маска 8), NPC (маска 32, список 0x34D1D50 — `NPC_OpenWindow`), ЛУТА (маска 16, список
	//       0x349BC18 — `DropItemMgr_SpawnGroundItem`), ВОДЫ и НЕБА в первой группе НЕТ — они рисуются только в
	//       обычном кадре (`CRenderer_RenderWorldFrame@0x9773B0`, шаги «2»–«4»);
	//     • `effectRenderType4QuadShadowPass@0xF5FE60` / `effectRenderType4Quad@0xF5FFB0` при 4 перехватывают
	//       ВЕСЬ кадр (рендер-цель на время сцены) — светится всё, включая рельеф и небо;
	//     • 0 — буфер освобождается (`comInterfacesReleaseAll`), свечения нет.
	//   ⇒ пункты loc 12190…12194 «Нет / Персонаж / Монстр / Персонаж+монстр / Все» переводятся ТОЧНО.
	//   ★★★ 19.09.2026 (реверс ЧТО попадает в буфер): группа рисуется в буфер свечения ЦЕЛИКОМ, порога нет.
	//     `sceneRenderShadowLightPass` чистит буфер в 0x00FFFFFF (белый, альфа 0) и зовёт те же функции рисования,
	//     что и обычный кадр (мобы — `sub_9256E3` и в `mapDragRotateOnInput@0xF5FBA0`, и здесь), а обычный кадр
	//     эту группу на пунктах 1..3 пропускает (мобы — только при 0/1/4, первая группа — только при 0/2/4,
	//     `sceneRenderShadowFogPasses@0xF5FA80`). Рельеф (`terrainRenderDispatchByFlags`, шаг «3» кадра) и небо
	//     (`CTerrain_SetupMultiTexBlend` со звёздной текстурой space11_0.nds, шаг «2») в группы НЕ входят.
	//     Сведение (`effectQuadRender@0xF5F440`): out = min(1, 2·G·min(1, 2·70/255·B2)) на пикселях группы
	//     (альфа G), B1/B2 — два прохода по 4 копии со сдвигом ±0.65 текселя буфера 256² (512² при ширине
	//     ≥ 1600), каждая ×90/255. Числа из .data (0x15B5E28…0x15B5E44), одинаковы для ALL (пункт 4) и UNIT (1..3);
	//     второй метод — байтовый скан кода: пишет их ТОЛЬКО чат-команда «+GLOW» (`effectQuadSetAnchorTag`).
	//   ПОРТ: служебный unbound-объём «PP_Glow» (приоритет 9000):
	//     4 «Все» — штатный блум кадра с калибровкой владельца (таблица [AsdaGlow], 09.09.2026: интенсивность 4.0,
	//       порог 2.5) — это ВЫБОР ВЛАДЕЛЬЦА, не формула эталона; не трогать;
	//     1 «Персонаж», 2 «Монстр», 3 «Персонаж+монстр» — штатный блум выключен, пост-материал `M_AsdaGlowSelect`
	//       (Tools/asda_build_glow_select_mat.py, после тонмаппера) даёт формулу эталона на пикселях группы.
	//       ★ 19.09.2026: маска ОБРАТНАЯ — трафарет ставится ТОЛЬКО объектам группы (первая группа —
	//       kGlowCharStencil, мобы — kGlowMobStencil, разбор классов — GlowGroupOf); всё непомеченное (застройка,
	//       рельеф, вода, небо, NPC, лут и любой будущий вид объектов) в группу не входит само собой;
	//     • ★ 19.09.2026 (С2): второй путь кадра эталона `CRenderer_RenderFrame@0x975EF0` (состояния 2 логин,
	//       3 выбор сервера, 5 выбор персонажа, 6 создание — `Game_UpdateAndRenderFrame@0xF20B80`; 10 загрузка)
	//       на пунктах 1/3/4 сводит ВЕСЬ кадр сцены без маски (`CRenderer_BeginScenePass@0xF60020` /
	//       `EndScenePass@0xF60140`, байт +8 = 0 → `effectQuadRender` без смешения), на пункте 2 — ничего.
	//       В порту это карта логина (AAsdaLoginGameMode): пункты 1/3 — формула на весь кадр, 2 — без свечения;
	//     0 «Нет» — ореола нет.
	//   ЕДИНСТВЕННОЕ ХРАНИЛИЩЕ — поле `GlowLevel` профиля игрока (`UAsdaGameUserSettings`); его зовут и
	//   комбо 70121 окна настроек, и строка dev-окна Ctrl+N. bSave — сразу записать профиль на диск.
	UFUNCTION(BlueprintCallable, Category="AsdaVideo|Quality")
	void SetGlowLevel(int32 Level0to4, bool bSave = true);

	// Значение трафарета, которым помечаются мобы для выборочного свечения (категория «существа»
	//   принятой в проекте раскладки CustomStencil: 10 персонажи, 11 существа, 12 мир — Tools/asda_build_toon_shaders.py).
	static constexpr int32 kGlowMobStencil = 11;
	// ★ 19.09.2026: трафарет ПЕРВОЙ группы (игроки и привязанное к ним — разбор у SetGlowLevel) — категория
	//   «персонажи» той же раскладки. Прежний трафарет «мир» (12, рельеф/небо) снят: маска обратная.
	static constexpr int32 kGlowCharStencil = 10;
	// ★★★ 19.09.2026 (glow_fix6, К1 — ДОКАЗАНО статикой и дампом): полупрозрачное (α < 1) тело группы в буфер
	//   свечения эталона НЕ рисуется вовсе. Цепочка: `charEntityUpdateTick@0xA1ED30` при α < 1 («душа», появление,
	//   невидимость) → `array_releaseAllElements@0x114A350` → `meshPartsSetRenderPassFlags@0x11C2720`: у КАЖДОГО
	//   подмеша всех записей модели `подмеш+4 |= 2`. Узел отрисовки подмеша = `подмеш+0x180`, его поле +0x30 —
	//   указатель на `подмеш+4` (пишут `listInsertNodeHashed@0x111B1C0` @0x111B241 — части персонажа, вызов
	//   @0x9E4F9D кладёт `lea ecx,[подмеш+4]`; `meshLinkDrawNodeHead@0x11A6FE0` @0x11A703C — мобы/NPC/тело);
	//   дамп live.dmp (та же сборка, код 10/10): у всех 8 подмешей персонажа `узел+0x30 == подмеш+4`. Проход
	//   буфера свечения (`renderList_traverseAndDispatch@0x111B730` → `meshNodeRenderDispatch@0x111E110`,
	//   @0x111E145–0x111E15F) пропускает узел с битом 2 или 1 в `*(узел+0x30)`, если не взведены 0x3528C44/45
	//   (тень/принудительный — ставятся и снимаются только внутри своих RTT-функций). Такие подмеши рисует очередь
	//   0x3537FA8 на шаге «5.1.5» — ПОСЛЕ сведения. Поэтому полупрозрачный меш группы НЕ помечается (полосы α
	//   glow_fix4/5 сняты); файловый бит 1 того же поля (данные .jbm) — см. реестр отложенного, пункт glow_fix6.
	// Группа, которой принадлежит СВОЁ значение трафарета: 1 — kGlowCharStencil, 2 — kGlowMobStencil,
	//   0 — чужое значение (подсветка цели и т.п.) — его пометки не снимают.
	static int32 GlowStencilGroup(int32 Value);
	// Прозрачность меша: у слотов на translucent-MID «души»/GM-hide (`M_AsdaCharLitSoul`, параметр SoulOpacity) и
	//   появления/гашения (`M_AsdaPreviewFade`, параметр FadeIn) — значение параметра, у прочих слотов — 1; по мешу —
	//   наибольшее. ★ glow_fix6: α < 1 — меш в группу свечения не входит (К1 выше).
	static float GlowAlphaOfMesh(class UMeshComponent* Mesh);
	// ★ 19.09.2026 (glow_fix6): есть ли в порту хоть один эффект с `AAsdaEffectActor::bGlowWithCarrier`. Сейчас
	//   признак не ставит никто (эффекты предметов в эталоне вне групп — С2 glow_fix5) ⇒ перебор `Children` носителя
	//   на каждом проходе пометок не нужен и не выполняется (if constexpr). Поднять в true вместе с первым присваиванием признака
	//   (полосы «совела» `itemSetupSowelGlow@0x9ED480` — реестр отложенного, пункт glow_fix6).
	static constexpr bool kGlowCarriedFxLive = false;
	// ★ 19.09.2026: к какой группе свечения эталона относится актор: 1 — первая группа (игрок, питомец,
	//   хранитель, снаряд, транспорт езды, сцена гачи; эффект с признаком `AAsdaEffectActor::bGlowWithCarrier` — в
	//   группе носителя; ★ glow_fix5 (С2): свечение заточки, крылья и кольцо соулмейта в эталоне ВНЕ групп — признак им
	//   не ставится), 2 — мобы, 0 — ни к какой (NPC, лут, карта, эффекты карты, навыков и предметов…).
	static int32 GlowGroupOf(const class AActor* Actor);
	// ★ 19.09.2026 (МЕЛК «подсветка ↔ пометки»): пересчитать пометку свечения ОДНОГО актора (и его эффектов
	//   предметов) под текущий пункт: нужна — поставить, не нужна — снять только свои 10/11. Зовёт подсветка
	//   цели после снятия (её снимок «до» мог устареть, если пункт сменили за время подсветки).
	static void RefreshGlowMarksFor(class AActor* Actor);
	// ★ 19.09.2026 (МЕЛК консоли): ДЕЙСТВУЮЩЕЕ состояние свечения для печати `asda.Video.Glow` — сила/порог
	//   блума служебного объёма (на 1..3 сила 0, строка таблицы им не применяется), режим «только блики»,
	//   выборочный материал и его режим маски.
	FString DescribeEffectiveGlow() const;

	// ★★ 10.10.2026 (fx_alphatest.md): АЛЬФА-ТЕСТ ПРОХОДА ЭФФЕКТОВ как в эталоне — ALPHATESTENABLE 1, ALPHAREF 8,
	//   ALPHAFUNC GREATEREQUAL вокруг очереди эффектов g_RenderQueue@0x3537FA8:
	//   • сцены логина/выбора/создания (состояния 2/3/5/6 → `CRenderer_RenderFrame@0x975EF0`, render_core.c:820-827) — ВСЕГДА;
	//   • мир (состояния 1/8 → `CRenderer_RenderWorldFrame@0x9773B0`, render_core.c:1419-1432) — только при пункте
	//     свечения 4 «Все» (`sub_928F5F` = геттер g_hMainWnd+0x80 = g_nCurrentChannelId@0x15CB4E0 = пункт комбо 871);
	//     на 0..3 очередь идёт через `clanShadowRenderQueue` без включения теста.
	//   Порт: скаляр `AlphaTest` коллекции /Game/Effects/MPC_AsdaFx (0/1) — отсечку srcA = Tex.a×VC.a < 8/255 делают
	//   мастера эффектов (Tools/asda_fx_alphatest.py). Ключ `asda.Fx.AlphaTest` (-1 авто / 0 / 1) — для проверки.
	//   Действующее значение для мира (true — тест включён).
	bool IsFxAlphaTestOn(const class UWorld* World) const;
	// Записать действующее значение в MPC_AsdaFx указанного мира (зовут ApplyGlowToWorld и ключ консоли).
	void ApplyFxAlphaTest(class UWorld* World) const;

	// ══════════ ★ ФАЗА 2 ОКНА НАСТРОЕК: ОБЩИЙ API ВЫНЕСЕННЫХ ИЗ Ctrl+N РУЧЕК ══════════
	//   Зовут ОБА окна (игровое 69500 и dev Ctrl+N) — второго пути записи нет. Хранилище — профиль игрока
	//   (`UAsdaGameUserSettings`), штатные поля родителя либо поля-переопределения с правилом «< 0 = не менялось».
	// Ключ консоли из таблицы профиля (метод AA, резкость, анизотропия, AO, размытие, блики, облака, дальность):
	//   записать в профиль и применить. false — ключ не профильный (вызывающий идёт своим путём).
	bool SetRenderOverride(const FString& CVarName, const FString& Value, bool bSave);
	// Снять переопределение (вернётся значение проекта/уровня). false — ключ не профильный.
	bool ClearRenderOverride(const FString& CVarName, bool bSave);
	// Масштаб рендера — ШТАТНОЕ `ResolutionQuality` профиля (`SetResolutionScaleValueEx`), не голый
	//   `r.ScreenPercentage`: иначе ближайшее `ApplyNonResolutionSettings` перезаписало бы выбор. 10…100 %.
	int32 GetRenderScalePercent() const;
	void SetRenderScalePercent(int32 Percent, bool bSave);
	// Лимит кадров (0 = без лимита) и вертикальная синхронизация — штатные поля профиля.
	void SetFrameRateLimitFps(int32 Fps, bool bSave);
	void SetVSync(bool bOn, bool bSave);
	// Оконный режим: 0 полный экран, 1 без рамки (WindowedFullscreen), 2 окно — на ТЕКУЩЕМ разрешении.
	int32 GetWindowModeIndex() const;
	// Сохранить профиль на диск (штатный `SaveSettings`).
	void SaveProfile();

	// ★ Калибровка выборочного свечения (пункты 1..3) на лету — консоль `asda.Video.GlowSelect`: печать
	//   действующих параметров, числа помеченных мобов и их мест; с аргументами — подмена сдвига/размытия/сведения
	//   у живого экземпляра материала (НЕ сохраняется: постоянные числа — таблица [AsdaGlow]).
	void DebugGlowSelect(const TArray<FString>& Args);

	// Текущий уровень свечения 0..4 (для меню/самопроверки).
	UFUNCTION(BlueprintCallable, Category="AsdaVideo|Quality")
	int32 GetGlowLevel() const { return GlowLevel; }

	// Прочитать ступень уровня свечения из таблицы владельца [AsdaGlow] (Config/DefaultScalability.ini).
	//   false = строки для этого уровня нет ⇒ ореол им не переопределяется (числа в C++ не дублируем).
	//   Публичная, потому что её печатает ключ `asda.Video.Glow` (лямбда — не член класса).
	static bool ReadGlowStep(int32 Level, float& OutIntensity, float& OutThreshold);

	// GAMMA оригинала (config.txt 0..100, дефолт 50=нейтраль) → GEngine->DisplayGamma (SetGammaRamp-эквивалент).
	//   ★ 19.09.2026: значение кладётся в поле `Gamma` профиля игрока `UAsdaGameUserSettings` (оно и
	//   пишется в GameUserSettings.ini штатным `SaveSettings`, и накатывается при старте игры).
	UFUNCTION(BlueprintCallable, Category="AsdaVideo|Quality")
	void SetGamma(int32 Gamma0to100);

	// ★ 19.09.2026: применить и сохранить группы качества, выставленные отдельными `Set*Quality`,
	//   ШТАТНЫМ путём (`ApplyNonResolutionSettings` + `SaveSettings`). Нужен окну настроек 69500:
	//   прежде его флаги 3D меняли уровни в объекте профиля, но не применяли и не сохраняли их.
	void CommitQuality() { SaveQuality(); }

	// ★★★ 12.10.2026 (решение владельца): ПРЕСЕТ ПО УМОЛЧАНИЮ — «Высокое» (High, sg 2). Им стартует первый запуск (без
	//   пробника железа), к нему возвращает «сброс вкладки Качество» и «по умолчанию» окна настроек; его же берут флаги
	//   3D окна настроек как уровень «включить», когда группа была выключена. Единственное число, менять только здесь.
	static constexpr EAsdaQualityPreset kDefaultPreset = EAsdaQualityPreset::High;

	// Применить качество по умолчанию: при bAllowBenchmark И включённом ключе asda.Video.AutoDetect=1 — автопробник железа →
	//   уровни по PerfIndex; иначе (ключ выключен — умолчание с 12.10.2026, пробник отказал, bAllowBenchmark=false) —
	//   `kDefaultPreset` (High). Плюс glow=4 (оригинал HIGH) и гамма 50.
	//   ★ В-1 (review_setE0.md): первый запуск (ApplyDefaultVideo) зовёт с bAllowBenchmark=true (замер уместен ОДИН раз, если
	//   его включили); СБРОС вкладки «Качество» Ctrl+N — с false: сброс не гоняет 4.7-секундный SynthBenchmark в игре.
	//   Явный пробник по требованию — кнопка «Определить автоматически» (RunHardwareBenchmarkNow).
	void ApplyDefaultQuality(bool bAllowBenchmark = true);

	// ★ В-1: явный запуск автопробника железа по кнопке «Определить автоматически» (вкладка «Качество» Ctrl+N) — игнорирует
	//   флаг asda.Video.AutoDetect (владелец нажал сам); при отказе замера уровни не меняются. Единственное место, где пробник
	//   гоняется НЕ на первом запуске.
	UFUNCTION(BlueprintCallable, Category="AsdaVideo|Quality")
	void RunHardwareBenchmarkNow();

	// ★ 05.09.2026 (review_devmenu_tabs.md В-5): принудительно переприменить ТЕКУЩИЕ sg-уровни (Scalability::SetQualityLevels
	//   с bForce) — после снятия консольного оверрайда с CVar, которым управляет пресет (r.MaxAnisotropy, r.BloomQuality,
	//   r.LensFlareQuality, а с E-0b — и r.Shadow.Virtual.Enable/r.ContactShadows/r.AmbientOcclusionLevels/…), чтобы значение
	//   пресета вернулось на место. Уровни групп не меняет.
	void ReapplyQuality();

	// ★ E-0b: строка состояния уровней для лога/самопроверки: «sg: view=3 aa=3 shadow=3 gi=3 refl=3 pp=3 tex=3 fx=3 fol=3 shading=3 res=100».
	static FString DescribeQualityLevels();

private:
	// Утилита: сохранить и применить не-разрешенческие настройки (scalability) через UGameUserSettings.
	void SaveQuality();

	// ★ E-0b: автопробник железа ПЕРВОГО ЗАПУСКА. Возвращает true, если уровни выставлены пробником (и сохранены).
	//   ★ В-2 (review_setE0.md): флаг выключения читается ДО замера НАПРЯМУЮ из ini ([AsdaVideo] GameUserSettings.ini →
	//   [SystemSettings]/[ConsoleVariables] Engine.ini), а не только из CVar asda.Video.AutoDetect — потому что `-ExecCmds`
	//   ставит CVar уже ПОСЛЕ InitializeStandalone (пробник к тому моменту отработал). Сам замер — DoHardwareBenchmark().
	bool RunAutoDetect();
	// Сам замер SynthBenchmark + применение результата, БЕЗ учёта флага AutoDetect (общий код первого запуска и кнопки).
	bool DoHardwareBenchmark();

	// ★★★ ПОДБОР ВЛАДЕЛЬЦА 09.09.2026 — точка отсчёта стартовых групп качества поверх результата пробника/пресета по умолчанию.
	//   ★ 12.10.2026: при пресете по умолчанию High (2) правило Min() оставляет обе группы на 2 — подбор (GI 2, тени 4) вступает
	//   только когда стартовый уровень ≥ 3 (включённый пробник на сильном железе).
	//   Таблица «группа sg.* → уровень» лежит в Config/DefaultScalability.ini [AsdaDefaultQuality] (данные для кода,
	//   НЕ CVar-пин: пин sg.* в [SystemSettings] дал бы приоритет 0x05 и запер уровни — грабля E-0b). Числа в C++ не
	//   дублируются (port-shared-points): нет строки в ini → группа не трогается.
	//   Правило на группу: результат = (уровень_пробника >= 3) ? подбор : Min(уровень_пробника, подбор). То есть на
	//   нормальном железе (пробник дал свой максимум 3 = Epic) берётся ровно число владельца, а на слабом подбор
	//   работает лишь как потолок и ПОДНЯТЬ уровень не может — ноутбук с уровнем 0/1 не получит тени 4.
	//   Зовётся ТОЛЬКО из ApplyDefaultQuality (первый запуск и сброс вкладки «Качество»); пресеты asda.Video.Preset
	//   и ручной выбор в Ctrl+N не трогает — там уровни ровные, как и были.
	void ApplyOwnerQualityBaseline();

	// ★★★ УРОВЕНЬ СВЕЧЕНИЯ (числа — выбор владельца, таблица [AsdaGlow] Config/DefaultScalability.ini).
	// Текущий пункт 0..4 — КОПИЯ поля профиля `UAsdaGameUserSettings::GlowLevel` (источник — профиль).
	int32 GlowLevel = 4;
	// ★ Выборочное свечение (пункты 1..3): динамический экземпляр `M_AsdaGlowSelect` текущего мира.
	//   ★ 19.09.2026: прежний таймер перепометки мобов (0.5 с) снят — пометки ведёт покадровый проход (ниже).
	//   ★ 19.09.2026: `r.CustomDepth` = 3 держит конфиг проекта (DefaultEngine.ini) — прежнее «запомнить и
	//   вернуть» снято: каждая смена ключа перерегистрировала весь мир (рывок).
	UPROPERTY(Transient) TObjectPtr<class UMaterialInstanceDynamic> GlowSelectMID = nullptr;
	// ★ 19.09.2026 (К1): учёт пометок — ПО МИРУ, без таймера. Мир, к которому сейчас привязаны пометки и подписки
	//   (PIE с несколькими клиентами: у каждого клиента своя подсистема и свой мир, чужие миры не трогаем).
	TWeakObjectPtr<class UWorld> GlowMarkWorld;
	// Акторы групп свечения этого мира (GlowGroupOf ≠ 0): запечённые при входе в мир, заспавненные позже,
	//   пришедшие с подгружаемыми уровнями. Мёртвые слоты вычищаются на проходе.
	TArray<TWeakObjectPtr<class AActor>> GlowTracked;
	// Подписка мира «актор заспавнен» (живые игроки/мобы от сервера — сразу, в момент спавна).
	FDelegateHandle GlowSpawnHandle;
	// Подписка «уровень добавлен в мир» (подгружаемые уровни со своими акторами).
	FDelegateHandle GlowLevelAddedHandle;
	// Подписка «акторы мира оттикали» — проход пометок КАЖДЫЙ кадр до отрисовки: новые/сменённые навесные
	//   компоненты (экипировка, волосы, езда, флаг клана) и снятая чужим кодом пометка чинятся в том же кадре.
	FDelegateHandle GlowPostTickHandle;
	// Пункт, под который пометки выставлены в GlowMarkWorld (смена пункта — полный пересчёт).
	int32 GlowMarkedLevel = -1;
	// Нужный актору трафарет под текущий пункт: kGlowCharStencil / kGlowMobStencil / 0 — не помечать.
	int32 WantedGlowStencil(const class AActor* Actor) const;
	// ★ 19.09.2026 (glow_fix4): эффект ПРЕДМЕТА, несомый актором Carrier, — ОДНО правило и для группы (GlowGroupOf),
	//   и для пометки (ApplyGlowStencilToActor): `AAsdaEffectActor::bGlowWithCarrier` и владелец эффекта = Carrier
	//   (SpawnByJebName ставит Owner; перебор пометки идёт по `Carrier->Children` — списку тех, чей Owner = Carrier).
	static bool IsGlowCarriedFx(const class AActor* Child, const class AActor* Carrier);
	// Выставить актору трафарет Stencil на все меш-компоненты (0 — снять ТОЛЬКО свои пометки 10/11).
	//   ★ 19.09.2026 (glow_fix6): меш с α < 1 (GlowAlphaOfMesh; у эффекта предмета — `FxAlphaScale`, GM-hide) не
	//   помечается — в эталоне такие подмеши в буфер свечения не попадают (К1 — разбор над GlowStencilGroup).
	//   Возвращает число изменённых компонентов (уже верные не трогаются — рендер-состояние не дёргается).
	//   ★ 19.09.2026: то же значение получают эффекты носителя с признаком `AAsdaEffectActor::bGlowWithCarrier`
	//   среди `Children` (владелец эффекта = носитель; glow_fix5: сейчас таких нет — С2); меши, CustomDepth которых держит подсветка цели
	//   (`UAsdaTargetHighlightComponent::OwnsCustomDepth`), не трогаются — приоритет у подсветки.
	static int32 ApplyGlowStencilToActor(class AActor* Actor, int32 Stencil);
	// Трафарет на меш-компоненты ОДНОГО актора (без эффектов-детей) — общий шаг ApplyGlowStencilToActor.
	static int32 ApplyGlowStencilToComponents(class AActor* Actor, int32 Stencil);
	// Привязать учёт пометок к миру: подписки, опись акторов групп, пометка под текущий пункт.
	//   Возвращает число изменённых компонентов.
	int32 BindGlowMarks(class UWorld* World);
	// Отвязать учёт от мира: снять свои пометки со всех учтённых акторов, отписаться.
	void UnbindGlowMarks();
	// Проход пометок по учтённым видимым акторам (каждый кадр, пункты 1..3 вне логина).
	int32 SyncGlowMarks();
	// ★ 19.09.2026 (С2): мир — сцена логина/выбора/создания персонажа (второй путь кадра эталона, без маски).
	static bool IsLoginSceneWorld(const class UWorld* World);
	// ★ 19.09.2026: смена `r.CustomDepth` перерегистрирует все компоненты мира и будит запечённых мобов —
	//   усыпить их снова (разбор — у определения).
	void ResleepBakedMobs(class UWorld* World);
	// Включить/выключить выборочное свечение в мире под текущий пункт (1..3 — вкл, 0/4 — выкл; на сцене
	//   логина 1/3 — весь кадр, 2 — выкл).
	void ApplyGlowSelect(class UWorld* World, class APostProcessVolume* Vol);
	// Блики объектива в служебном объёме — по полю профиля `LensFlare` (незаданное = проект, бликов нет).
	void ApplyLensFlareToVolume(class APostProcessVolume* Vol) const;
	// ★ 19.09.2026 (хвосты ревизии фазы 2): игрок включил блики (поле профиля `LensFlare` > 0).
	bool IsLensFlareWanted() const;
	// ★ 19.09.2026: `r.BloomQuality` под текущий пункт свечения И блики. Движок пускает блики только вместе с
	//   блумом (PostProcessing.cpp: `bLensFlareEnabled = bBloomEnabled && …`), поэтому на пунктах 0..3 при
	//   включённых бликах блум нужен как «носитель» (качество 1), а его ореол гасится в объёме (см. ApplyGlowToWorld).
	void ApplyGlowBloomQuality() const;
	// ★ 19.09.2026: блики сменились (окно, Ctrl+N) — пересобрать `r.BloomQuality` и служебный объём текущего мира.
	void RefreshLensFlare();
	// ★ 19.09.2026: доля гауссиан блума в режиме «только блики» (пункты 0..3 + блики вкл). Больше нуля — чтобы
	//   движок счёл блум включённым (`BloomGaussianIntensity > 0`), но ореол = сила × 1e-6 — ниже шага 8-битного кадра.
	static constexpr float kFlareOnlyGaussianScale = 1e-6f;
	// ★ 19.09.2026: камера проверки `asda.Video.GlowSelect look` (без павна BugItGo двигает только поворот).
	TWeakObjectPtr<class ACameraActor> GlowLookCam;
	// ★ 19.09.2026: моб, на которого смотрит камера проверки (`look still` замораживает его позу).
	TWeakObjectPtr<class AAsdaMobActor> GlowLookMob;
	// Служебный unbound-объём «PP_Glow» текущего мира: несёт ТОЛЬКО две ручки ореола (интенсивность и порог).
	//   Транзиентный, в уровень не сохраняется, живёт ровно столько же, сколько мир.
	TWeakObjectPtr<class APostProcessVolume> GlowPPV;
	// Подписка на «в мире созданы акторы» — чтобы ступень доезжала до КАЖДОГО нового мира (смена карты, вход в игру
	//   со сцены логина), а не только до того, что был жив в момент выбора пункта.
	FDelegateHandle GlowWorldHandle;
	// Создать/обновить «PP_Glow» в указанном мире под текущую ступень. Мир не игровой или ступень без строки — ничего не делает.
	void ApplyGlowToWorld(class UWorld* World);
};
