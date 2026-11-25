// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEffectActor.cpp — симуляция эффектов AsdaStory 1:1 (модель верифицирована live.dmp).
// Источники формул: CEmitter_SpawnParticle@0x12062A0, CEmitter_Tick@0x12588A0, CEmitter_Update_v3@0x1211C20,
// CEmitter_Update_Track@0x1204DA0 (track-march), effectQuad_updateAndDrawBillboard@0x120AEB0 (0xB).
#include "AsdaEffectActor.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaView.h"   // ★ волна 3 п.11 (15.09.2026): общая точка наблюдателя (E7)
#include "AsdaSelfTestBatch.h"   // ★ ревизия В2 дедупа №2 (11.10.2026): Quit — выход самотеста по итогу (перехватывается пакетом)
#include "Resources/AsdaMaterialNames.h"   // ★ дедуп №2 В1-C (F10): имя параметра EmissiveBoost
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "AsdaFxClassCache.h"   // ★ R4, доля a: единая точка загрузки класса эффекта (удержание и предзагрузка)
#include "World/AsdaCoord.h"                    // ★ Аудит №3 (B1): единый своп осей игра→UE
#include "ProceduralMeshComponent.h"
#include "AsdaFxNames.h"   // теги свечения заточки (цвет вершин)
#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "ScopedTransaction.h"   // ★ Ф17 (С-1): уборка сирот обязана отменяться Ctrl+Z — разбор у DropOldestGlowOrphans
#endif
#include "Kismet/GameplayStatics.h"
// ★ Для консольной проверки яркости `asda.Fx.MatDump` (конец файла): чтение скалярного параметра
//   с ЭКЗЕМПЛЯРА материала на секции, отложенные шаги сценария и выполнение консольных команд.
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameters.h"   // FMaterialParameterInfo (MaterialTypes.h объявлен устаревшим)
#include "Materials/MaterialInstanceDynamic.h"  // ★ Ф5: MID «силы свечения» поверх материала эмиттера
#include "TimerManager.h"
#include "EngineUtils.h"   // TActorIterator — чтение живых эффектов мира (`asda.Fx.SectionDump`)
#include "UObject/UObjectIterator.h"   // ★ Ф16 (МЕЛК-4): TObjectIterator — CDO запечённых BP_Fx_* (в мире их нет)
#include "World/AsdaFxLightSettings.h"    // ★ 13.09.2026: переопределения свечения предмета/эффекта и сила свечения семейств
#include "World/AsdaFxLightSubsystem.h"   // ★ 13.09.2026: ключ отката asda.Fx.Glow.Overrides, имя .jeb, перезаведение света
#include "Data/AsdaFxLoopRows.h"          // ★ 08.10.2026: ЦИКЛ 1:1 — endMode диска, контейнер, сид-узлы карт (DT_FxLoop)
#include "Engine/DataTable.h"             // ★ 08.10.2026: чтение DT_FxLoop
#include "AsdaDataTableCache.h"            // ★ дедуп №2 В2-C (F3): AsdaDt::Cached — общая загрузка DT_FxLoop
#include "UI/AsdaEffectsSubsystem.h"        // ★ дедуп №2 В2-C (F3): удержание DT_FxLoop на сессию (FAsdaHeldTables)
#include "Engine/GameInstance.h"           // ★ дедуп №2 В2-C (F3): GameInstance эффекта — владелец удержания
#include "Components/SkeletalMeshComponent.h"   // ★ 08.10.2026: холдер режима 2 — порог анимации носителя («≥ порога» с защёлкой)
#include "World/AsdaWorldKind.h"              // ★ F7 (дедуп №2, В3): общий признак «сцена логина»
#include "World/AsdaFxLightComponent.h"        // ★ 15.10.2026: RestVerts строятся только при свете эффекта (IsCarrierAnchorOn)
#if WITH_EDITOR
#include "HAL/PlatformApplicationMisc.h"      // ★ 27.10.2026: буфер обмена кнопок «Копировать/Вставить свет»
#endif
#include "AsdaEffectActorInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaEffectActor*.cpp

// ★ ФИКС ПОРЯДКА ТИКОВ у эффектов, ПРИВЯЗАННЫХ К ПЕРСОНАЖУ (см. комментарий у bFollowTickAfterPose).
//   1 = эффект тикает в TG_PostUpdateWork после носителя (штатно), 0 = как было (TG_PrePhysics) —
//   переключатель оставлен для A/B-замера отставания прямо на живой сцене (`asda.FxFollowTickFix 0`).
//   На эффекты БЕЗ флага bFollowTickAfterPose (все карты, весь запечённый контент) не влияет вообще.
static TAutoConsoleVariable<int32> CVarAsdaFxFollowTickFix(
	TEXT("asda.FxFollowTickFix"), 1,
	TEXT("1 = эффекты на персонаже тикают ПОСЛЕ его движения и поз (TG_PostUpdateWork); 0 = как было"), ECVF_Cheat);

// ★★★ КОНЕЦ ПЕРИОДА У ПАРТИКЛ-ЭМИТТЕРОВ (2026-09-10, задача «эффекты в конце прыгают к первому кадру»).
//   Разбор и числа — `Reports/fx_loop/`. Затронута ТОЛЬКО партикл-ветка (типы 0x03/0x0D/0x0F/0x1B/0x20);
//   квад/кольцо/меш живут на своих машинах (`effectUpdateKeyframeTrack@0x11F7B10`,
//   `fxTrack_updateKeyframe@0x11FD800`, `effectEmitter_tickUpdate@0x12293B0`) и НЕ меняются ни одним ключом.
//
//   ЭТАЛОН — `CEmitter_Update_Track@0x1204DA0` (0x0D/0x1B, трек), `CEmitter_Update_v3@0x1211C20`
//   (0x03/0x0F, rate) и `CEmitter_SpawnParticle@0x12062A0`:
//   [A] ТРЕК НЕ СЭМПЛИРУЕТСЯ ПО ВРЕМЕНИ. Эмиттер держит ИНДЕКС узла (`trackFrameIdx`), каждый кадр
//       марширует свои каналы `param += dt·node[rate]` и СНАПАЕТ их на значения узла в момент
//       перехода (0x1204DA0, ветка `lifeTimer <= lifetime && lifeTimer > trackCurNode->time`,
//       затем `++trackFrameIdx`).
//   [B] ЧАСТИЦА ПРИ СПАВНЕ берёт `birthAttr = node[trackFrameIdx]`, цель марша `deathAttr =
//       node[trackFrameIdx+1]` (0x12062A0: alpha birthAttr[13], sizeX [15], sizeY [17],
//       RGB [19..21], gravity [47]) — то есть значения УЗЛА, БЕЗ интерполяции по времени: внутри
//       сегмента спавн-значения ПОСТОЯННЫ. Скорость (`emitVelHistA[2]`), угол конуса (`sizeRand`)
//       и spawnRate — НАОБОРОТ, маршевые поля эмиттера; их непрерывность эталонна и не трогается.
//   [C] `endMode == 3` НЕ ПЕРЕЗАПУСКАЕТ трек: `case 3` пишет `lifeTimer = 0.0` и НЕ трогает
//       `trackFrameIdx` ⇒ эмиттер навсегда остаётся на ПОСЛЕДНЕМ сегменте (у 2-узлового трека —
//       на сегменте birth→death: частицы вечно рождаются «birth» и вечно маршируют в «death»).
//   [D] `endMode == 0` НЕ УБИВАЕТ живые частицы: `case 0` пишет только `lifeTimer = −1.0`, а цикл
//       пер-частичного тика в ОБЕИХ update-функциях стоит ВЫШЕ проверки `lifeTimer <= −1.0` ⇒
//       уже рождённые частицы доигрывают жизнь и гаснут по своим рампам (хвост затухания).
//
//   ПОРТ ДО ПРАВКИ: спавн-значения брались из `NodeAt(LifeTimer)` (непрерывный лерп), `endMode 3`
//   у партикл-ветки делал `Fmod` + `ResetEmitterMotion` (полный рестарт), `endMode 0` гасил все
//   100 слотов и чистил секцию за один кадр. Итог — канал (альфа/размер/цвет) плавно уезжал к
//   death-значению и РАЗОМ прыгал обратно к birth раз в период, а одноразовые эффекты обрывались
//   без затухания. Ровно то, на что жалуется владелец.
//
//   ★★★ ДЕФОЛТ ВСЕХ ТРЁХ = 0 («КАК БЫЛО») — по закону проекта (`AsdaEffectActor.h`, ЗАКОН у
//   `bStrictMeshPos`): изменение ОБЩЕГО кода эффектов включается только с явного разрешения
//   владельца, потому что оно меняет ПРИНЯТУЮ картину. Замер (`Reports/fx_loop/`, прогон
//   `run_fx_loop_probe.ps1`): у свечения оружия `ohs_a_co_ma_03` включение ключей поднимает радиус
//   искровых 0x0D-эмиттеров 88→129 и 40→59 юн — это ЭТАЛОННО (частица рождается размером birth-узла,
//   а не «текущим» на рампе), но заметно, а сага свечения принята владельцем.
//   ВКЛЮЧЕНИЕ ЦЕЛИКОМ (одна строка в консоли или в `-ExecCmds`):
//     asda.Fx.SpawnFromNode 1, asda.Fx.LoopHold 1, asda.Fx.EndTail 1
// ★★★ 08.10.2026 (ЦИКЛ-5, решение владельца): три ключа ниже были ECVF_Cheat — их `=1` из
//   Config/DefaultEngine.ini движок НЕ применял (ConsoleManager.cpp:3335, `if(!Var->TestFlags(ECVF_Cheat) ...)`:
//   значение из ini у cheat-переменной, зарегистрированной позже чтения ini, выбрасывается) — в игре было 0,
//   «LastSetBy: Constructor». Решение 11.09.2026 фактически не действовало. Флаг сменён на ECVF_Default
//   (как у asda.Fx.Loop): ini применяется, откат из консоли — как и прежде (`asda.Fx.LoopHold 0` и т. п.).
TAutoConsoleVariable<int32> CVarAsdaFxSpawnFromNode(
	TEXT("asda.Fx.SpawnFromNode"), 0,
	TEXT("1 = частица рождается со значениями УЗЛА сегмента (эталон 0x12062A0); 0 = прежнее поведение порта (лерп по времени). Умолчание в коде 0; в проекте задано 1 в Config/DefaultEngine.ini [SystemSettings] (решение владельца 11.09.2026; из ini применяется с 08.10.2026)."), ECVF_Default);
TAutoConsoleVariable<int32> CVarAsdaFxLoopHold(
	TEXT("asda.Fx.LoopHold"), 0,
	TEXT("1 = endMode 3 у партикл-эмиттера удерживает последний сегмент трека (эталон 0x1204DA0 case 3); 0 = прежнее поведение порта (рестарт). Умолчание в коде 0; в проекте задано 1 в Config/DefaultEngine.ini [SystemSettings] (решение владельца 11.09.2026; из ini применяется с 08.10.2026)."), ECVF_Default);
TAutoConsoleVariable<int32> CVarAsdaFxEndTail(
	TEXT("asda.Fx.EndTail"), 0,
	TEXT("1 = endMode 0 даёт живым частицам доиграть (эталон 0x1211C20 case 0); 0 = прежнее поведение порта (гасить все слоты сразу). Умолчание в коде 0; в проекте задано 1 в Config/DefaultEngine.ini [SystemSettings] (решение владельца 11.09.2026; из ini применяется с 08.10.2026)."), ECVF_Default);

// ════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ ЦИКЛ 1:1 (08.10.2026, жалоба владельца «почти все зацикленные эффекты зациклены РЕЗКО —
//   последний кадр обрывает цикл и начинается заново»). Разбор — `Reports/owner_1008/fx_loop.md`.
//
//   ЧТО БЫЛО. Квады и меши в ассетах почти поголовно лежат с `EndMode = 1` (ветки growQuad/mesh12
//   `build_emitter` пишут рестарт безусловно — «закон вечных карт»), а рантайм делал по нему
//   `Fmod` — рестарт с узла 0 каждый период; у карт вдобавок трек квадов/колец с ключами лежит БЕЗ
//   конечного сида. Скан ассетов (дамп 1282 BP + 20 карт против `.jeb`, `classify_all.txt` отчёта):
//   конец цикла ВИДИМО обрывался у 1 131 экземпляра зацикленных квадов/колец/мешей, из них 1 000 —
//   ТОЛЬКО у порта: фонари шахмат (564), порталы `eff_*move*`, огненные башни, идол alpen, луки
//   (0x14: поворот прыгал назад), лопасти alpen, драгоценности alpen, ледяной щит (меш 0x12).
//   Остальные 131 обрываются и в оригинале (endMode 1 диска при разных СТАРТ/КОНЕЦ — авторский
//   «вспых»: сияние заточки +7, мины, дроп) — их не трогаем.
//
//   ЭТАЛОН — конец периода у update-функций по типам (vtbl[14], сняты с vtable ctor-ов; таблица
//   целиком — `Reports/owner_0927/gfx_w1/C_lifetime.md` §1, тела перечитаны 08.10):
//     0x0B `effectUpdateKeyframeTrack@0x11F7B10`, 0x0C `animAdvanceKeyframe@0x11FA8E0`,
//     0x0E `fxTrack_updateKeyframe@0x11FD800`, 0x18 `effectUpdateKeyframeUV@0x1246480`,
//     0x19 `effectUpdateKeyframeUV2@0x12470E0`, 0x13/0x14 `effectEmitterUpdateAnimated(Variant)@0x123E090/0x123EEC0`,
//     меш 0x12 `effectAnimTickKeyframe@0x1239250`. Марш каналов `cur += dt·node[rate]` идёт ДО
//     проверки конца, поэтому всё, что ветка конца НЕ снапнула, продолжает идти наклоном ПОСЛЕДНЕГО
//     сегмента (у трека `trackFrameIdx` не дорастает до последнего узла — см. `SegIndexAt`).
//       endMode 0  — снап КОНЦА всех каналов + `lifeTimer = −1`: эмиттер ЗАСТЫВАЕТ, но рисуется
//                    (рендер `objListRenderInRange@0x114E470` таймер не проверяет) до конца контейнера;
//       endMode 1  — снап КОНЦА + vtbl[1] (реинит) = рестарт;
//       endMode 2  — 0x0B/0x0C/0x0E: `return 0` без сброса, марш ВСЕХ каналов вечный;
//                    0x18/0x19/0x1A: снап КОНЦА каждый кадр (удержание), флипбук идёт;
//                    0x01/0x02/0x04: пинг-понг (свап СТАРТ↔КОНЕЦ); 0x13/0x14/0x15: реинит;
//       endMode 3  — 0x0B: держатся RGB, альфа, u, v — маршируют полу-экстенты, roll/rot, pos, basePos;
//                    0x0C: держатся RGB, альфа, полу-экстенты, u, v — маршируют roll/rot, pos, basePos;
//                    0x0E: держатся RGB, альфа, scale, u, v, высота/радиусы — маршируют roll/rot, pos, basePos;
//                    0x18/0x19/0x1A: полное удержание, кадр флипбука `floor(life/frameTime)` растёт
//                    (V за 1 заворачивается TA_WRAP ⇒ атлас крутится дальше);
//                    0x13/0x14: `lifeTimer = 0` (флипбук заново), ВСЕ каналы маршируют дальше;
//                    0x01/0x02: снап КОНЦА RGBA/W/H/pos/uv, маршируют rot и basePos.
//   КОНТЕЙНЕР (шапка .jeb, `objAnimRotateUpdate@0x114E050`): по концу длительности 1 = рестарт ВСЕГО
//     эффекта (`effectParticleReset@0x114D0F0`), 0/2 = эффект окончен (у карты холдер
//     `sceneComposite_UpdateGridsAndEffects@0x11DD150` поднимает его заново; режим 2 — «анимация
//     носителя ≥ порога» с защёлкой, 08.10 МЕЛК-3), 3 = вечный.
//
//   ЧТО ДЕЛАЕТ ПОРТ ТЕПЕРЬ (только квады/кольца/меши; частицы — прежние ключи LoopHold/EndTail):
//     режим эмиттера выводится из endMode ДИСКА (`DT_FxLoop`), если ассет с ним согласен или его
//     значение — вынужденное сборщиком (1 вместо 0/2/3; у колец 3 вместо 2). Осознанные правки ассета
//     (кольцо выбора цели: диск 0 → ассет 3 «держать кадр») НЕ перебиваются — режим Legacy.
//   ★ endMode 0 у ОДНОРАЗОВЫХ каналов (ассет = 0, эффект НЕ на карте): при `asda.Fx.End0Freeze 1` —
//     эталон (застывает видимым до конца контейнера; ВКЛЮЧЕНО владельцем 08.10.2026 в DefaultEngine.ini,
//     после правок ревизии: переопределение контейнера состояниями, гашение застывших по концу
//     контейнера, срок жизни ≥ контейнера). `asda.Fx.End0Freeze 0` — откат: гасится, как раньше.
//   МЕШИ: 0x12/0x1F endMode 3 — `age = 0`, снап RGB/альфы/масштаба/u/v на узел, поворот/posLocal/
//     offsetWorld маршируют дальше (раскладка каналов — замер живого оригинала, `mesh12_key_nodes`);
//     0x11 endMode 3 — пила от КОНЦА; endMode 2: 0x12/0x1F марш вечный, 0x11 пинг-понг.
//   ★ У эффекта НЕ на карте по концу контейнера 0/2 гаснет ВЕСЬ эффект, как у эталона
//     (`emitterRecordsResetTimers@0x114DC20` → рендер не рисует ни одной записи). До 08.10.2026 [ПОРТ] гасил только
//     ЗАСТЫВШИЕ эмиттеры; эмиттеров с диском 1/2/3 в контейнерах 0/2 — 471, они оставались видимыми до сноса
//     актора владельцем. Исправлено 08.10.2026 (ЦИКЛ-4, решение владельца), откат — `asda.Fx.Loop 0`.
//   ОТКАТ без пересборки: `asda.Fx.Loop 0` — прежний путь целиком (бит-в-бит), `asda.Fx.LoopSeeds 0`
//   — не подставлять сид-узлы карт.
// ════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ 08.10.2026 (fx_loop_fixes, находка при проверке ключей путём игрока): три ключа ЦИКЛА 1:1 были с флагом
//   ECVF_Cheat, а движок ЗАПРЕЩАЕТ брать значение из ini для cheat-переменной, зарегистрированной позже
//   (модуль игры): `FConsoleManager::AddConsoleObject`, ConsoleManager.cpp:3333–3335 (проверка — 3335) «This is to prevent cheaters
//   to set a value from an ini of a cvar that is created later». Итог: `asda.Fx.Loop=1`/`LoopSeeds=1` из
//   DefaultEngine.ini в игре НЕ действовали («LastSetBy: Constructor», значение 0) — все прогоны включали их
//   из консоли. Флаг сменён на ECVF_Default: ini теперь работает; откат из консоли — как и прежде.
TAutoConsoleVariable<int32> CVarAsdaFxLoop(
	TEXT("asda.Fx.Loop"), 0,
	TEXT("1 = конец периода квадов/колец/мешей и контейнер эффекта 1:1 с оригиналом (DT_FxLoop); 0 = прежний путь порта. Умолчание в коде 0; в проекте задано 1 в Config/DefaultEngine.ini [SystemSettings] (задача владельца 08.10.2026)."), ECVF_Default);
TAutoConsoleVariable<int32> CVarAsdaFxLoopSeeds(
	TEXT("asda.Fx.LoopSeeds"), 0,
	TEXT("1 = эффектам карт, чей трек потерял СТАРТ/КОНЕЦ-сиды, дописать недостающие сид-узлы эталона из DT_FxLoop (только игровой мир, только если трек ассета поузлово равен печке без сидов); 0 = трек ассета как есть. Умолчание в коде 0; в проекте задано 1 в Config/DefaultEngine.ini [SystemSettings]."), ECVF_Default);
TAutoConsoleVariable<int32> CVarAsdaFxEnd0Freeze(
	TEXT("asda.Fx.End0Freeze"), 0,
	TEXT("1 = endMode 0 и у одноразовых эффектов застывает ВИДИМЫМ на конце до конца контейнера (эталон); 0 = гасится, как раньше. Умолчание в коде 0; в проекте задано 1 в Config/DefaultEngine.ini [SystemSettings] (одобрено владельцем 08.10.2026)."), ECVF_Default);

#if !UE_BUILD_SHIPPING
// ★ СТЕНД СВЕРКИ ТОЧКИ НАБЛЮДАТЕЛЯ В СБОРКЕ КВАДОВ (15.09.2026). С этого дня BuildQuads получает снимок
//   наблюдателя шага (FAsdaFxViewSample) вместо собственных вызовов AsdaView на каждый эмиттер. При ключе 1
//   перед каждой сборкой секции ПРЕЖНИЙ путь выборки (GetViewPoint для поворота + GetViewLocation для
//   биллборда 0xC — в тот же момент, где они были) сравнивается побитно с переданным снимком, и геометрия
//   секции строится дважды (с прежней выборкой и со снимком) и сравнивается побайтно (V/I/N/UV/цвет).
//   Итог — `asda.Fx.ViewSampleCheck.Report`; прогон на заспавненном эффекте — `asda.Fx.ViewSampleCheck.Stand`.
//   0 (умолчание) — стенд спит, накладных нет. В Shipping ключа нет.
int32 GAsdaFxViewSampleCheck = 0;
static FAutoConsoleVariableRef CVarAsdaFxViewSampleCheck(
	TEXT("asda.Fx.ViewSampleCheck"),
	GAsdaFxViewSampleCheck,
	TEXT("1 = сверять при каждой сборке секции прежнюю выборку точки наблюдателя со снимком шага и буферы вершин побайтно (стенд, только чтение); 0 = выкл"),
	ECVF_Cheat);
#endif // !UE_BUILD_SHIPPING

// Группа тика + tick-prerequisite на компонент-носитель. Ничего не делает у эффектов без флага.
void AAsdaEffectActor::ApplyFollowTickOrder()
{
	const bool bWant = bFollowTickAfterPose && (CVarAsdaFxFollowTickFix.GetValueOnGameThread() != 0);
	// Компонент, чья поза/трансформ читается в Tick: явный скелет → явный компонент → первый скелет носителя.
	USceneComponent* Src = nullptr;
	if (bWant)
	{
		if (FollowSkelComponent) { Src = ToRawPtr(FollowSkelComponent); }
		else if (FollowComponent) { Src = ToRawPtr(FollowComponent); }
		else if (FollowActor) { Src = FollowActor->FindComponentByClass<USkeletalMeshComponent>(); }
		if (!Src && FollowActor) { Src = FollowActor->GetRootComponent(); }
	}
	if (bWant == bFollowTickOrderApplied && TickPrereqComp.Get() == Src) { return; }   // ничего не поменялось
	// снять старую зависимость
	if (USceneComponent* Old = TickPrereqComp.Get())
	{
		RemoveTickPrerequisiteComponent(Old);
		if (AActor* OldOwner = Old->GetOwner()) { RemoveTickPrerequisiteActor(OldOwner); }
	}
	TickPrereqComp = nullptr;
	if (bWant)
	{
		// TG_PostUpdateWork: после TG_PrePhysics (движение павна + запуск анимации), после физики и после
		// завершения параллельной оценки поз ⇒ GetComponentSpaceTransforms() = поза ТЕКУЩЕГО кадра.
		PrimaryActorTick.TickGroup = TG_PostUpdateWork;
		if (Src)
		{
			AddTickPrerequisiteComponent(Src);                                     // страховка на случай демоции групп
			if (AActor* Own = Src->GetOwner()) { AddTickPrerequisiteActor(Own); }  // ...и на тик самого носителя
			TickPrereqComp = Src;
		}
	}
	else
	{
		PrimaryActorTick.TickGroup = TG_PrePhysics;   // ровно как было до фикса
	}
	bFollowTickOrderApplied = bWant;
}

// ★★★ Д1: ЕДИНАЯ точка настройки attach-10/attach-6 (см. заголовок).
// ★★★ 08.11.2026 [ОРИГИНАЛ] ЭФФЕКТ НА МАТРИЦЕ ПЕРСОНАЖА НЕ РАЗВОРАЧИВАЕТСЯ (баг: полосы шлейфа транспорта идут впереди него).
//   Привязка 6 (`effectNodeFollowPlayerBone@0xF58850`; шлейф езды `Item_UpdateSpecialEffectAndSound@0xF272E0`) кладёт эффект в мировую матрицу
//   персонажа, а отрисовка квада `effectQuad_updateAndDrawBillboard@0x120AEB0` разворота на π не содержит. Порт для квадов на носителе поворачивает
//   геометрию на π («швартовка», подобрана под башню alpen, `if (bAttachedQuad && !bStrictQuadAttach)`), поэтому всё, что в данных лежит
//   за спиной, рисовалось впереди. Канал навыков и предметов флаг ставит сам; здесь — привязка 6 всех подсистем: шлейф езды, звания, реквизит, корона,
//   битва армий. 0 — прежнее.
static ASDA_DEV_CVAR(int32, CVarAsdaFxCharWorldStrict,
	TEXT("asda.Fx.CharWorldStrict"), 1,
	TEXT("[ОРИГИНАЛ 08.11.2026] 1 = эффект на матрице персонажа (привязка 6) без разворота квадов на π, как эталон; 0 = прежнее (шлейф транспорта впереди него)."),
	ECVF_Default);

void AAsdaEffectActor::ConfigureBoneAttach(AActor* InOwner, USkeletalMeshComponent* Skel, FName Bone,
	EAsdaFxAttachMode Mode, const FVector& Offset, const FMatrix& AttachMatrix)
{
	FollowActor = InOwner;
	FollowOffset = Offset;
	AuthoredRot = FRotator::ZeroRotator;   // авторский поворот не смешиваем с attach
	bFollowActorYaw = false;               // устаревший костыль крыльев — не смешивать с attach-10
	bFollowPosition = true;                // ★ ГЕЙТ ТИКА: без него слежение НЕ исполняется (боевой баг хранителя)
	bFollowTickAfterPose = true;           // тик после позы носителя (иначе дрожание/отставание на кадр)

	if (Mode == EAsdaFxAttachMode::CharWorld)
	{
		// attach-6: нода = мировая матрица персонажа (шлейф транспорта), без кости.
		FollowComponent = Skel;
		bFollowCharWorld = true;
		bFollowBoneFull = false;
		bFollowCharPos = false;
		// ★ 08.11.2026 [ОРИГИНАЛ] (`asda.Fx.CharWorldStrict`, разбор у ключа).
		bStrictQuadAttach = CVarAsdaFxCharWorldStrict.GetValueOnGameThread() != 0;
	}
	else if (Mode == EAsdaFxAttachMode::CharPos)
	{
		// attach-3/5/9/12: КАЖДЫЙ КАДР только позиция носителя (`CEffect_SetPos`), ориентация — своя.
		bFollowCharPos = true;
		bFollowCharWorld = false;
		bFollowBoneFull = false;
		bFollowRotation = false;
		bStrictQuadAttach = false;
	}
	else if (Mode == EAsdaFxAttachMode::BonePos)
	{
		// attach-7: позиция КОСТИ (`боневая матрица × мир персонажа`), ориентация — своя.
		//   Ветка «сокет без поворота» в Tick уже реализует ровно это.
		FollowBone = Bone;
		FollowSkelComponent = Skel;
		bFollowCharPos = false;
		bFollowCharWorld = false;
		bFollowBoneFull = false;
		bFollowRotation = false;
		bStrictQuadAttach = false;
	}
	else
	{
		// attach-10: полный трансформ живой кости.
		FollowBone = Bone;
		FollowSkelComponent = Skel;        // nullptr = как было (FindComponentByClass) — так у крыльев
		bFollowCharWorld = false;
		bFollowCharPos = false;
		bFollowBoneFull = true;
		// strictQuad — только у квадов-на-кости (свечение заточки, эффект хранителя); у крыльев НЕТ.
		bStrictQuadAttach = (Mode == EAsdaFxAttachMode::WeaponGlow || Mode == EAsdaFxAttachMode::OwnerBoundFx);
		FollowBoneAttach = AttachMatrix;   // «оружие за спиной» / Identity
	}
	ApplyFollowTickOrder();
}

// ★★★ Д3: ЕДИНАЯ ground-truth матрица посадки на кость (attach-10). См. заголовок.
bool AAsdaEffectActor::ComputeFollowBoneMatrix(FMatrix& OutA) const
{
	if (!bFollowBoneFull || !FollowActor) { return false; }
	const USkeletalMeshComponent* FSk = FollowSkelComponent
		? ToRawPtr(FollowSkelComponent)
		: FollowActor->FindComponentByClass<USkeletalMeshComponent>();
	if (!FSk) { return false; }
	const int32 BI = FSk->GetBoneIndex(FollowBone);
	if (BI == INDEX_NONE || !FSk->GetComponentSpaceTransforms().IsValidIndex(BI)) { return false; }
	// stored-fx оси (импортёр jem: ue=(fy,fx,fz)) → сырой скелет = SWAP(X,Y)·100; ground truth live.dmp §H.
	static const FMatrix FxToBone(
		FPlane(0, 100, 0, 0),   // stored ueX (=file Y) → сырой скелет Y ×100
		FPlane(100, 0, 0, 0),   // stored ueY (=file X) → сырой скелет X ×100
		FPlane(0, 0, 100, 0),   // stored ueZ (=file Z) → сырой скелет Z ×100
		FPlane(0, 0, 0, 1));
	const FMatrix BoneCS = FSk->GetComponentSpaceTransforms()[BI].ToMatrixNoScale();
	const FMatrix Comp = FSk->GetComponentTransform().ToMatrixWithScale();
	// A = FxToBone · attachMatrix(«оружие за спиной») · живая кость · компонент (0.01·зеркало·курс)
	OutA = FxToBone * FollowBoneAttach * BoneCS * Comp;
	return true;
}

bool AAsdaEffectActor::ComputeFollowTargetWorld(FVector& OutLoc, FQuat& OutRot) const
{
	// attach-6 (шлейф транспорта): нода = мировая матрица персонажа, BoneCS = единица.
	if (bFollowCharWorld && FollowComponent)
	{
		static const FMatrix FxToBoneW(
			FPlane(0, 100, 0, 0), FPlane(100, 0, 0, 0), FPlane(0, 0, 100, 0), FPlane(0, 0, 0, 1));
		FMatrix A = FxToBoneW * FollowComponent->GetComponentTransform().ToMatrixWithScale();
		OutLoc = A.GetOrigin();
		A.RemoveScaling();
		OutRot = A.ToQuat();
		return true;
	}
	// attach-10: полная матрица живой кости.
	FMatrix A;
	if (ComputeFollowBoneMatrix(A))
	{
		OutLoc = A.GetOrigin();      // мировая позиция кости (позиция зеркалом не страдает)
		A.RemoveScaling();           // 100·0.01=1; строки ортонормальны, det=+1
		OutRot = A.ToQuat();
		return true;
	}
	return false;
}

// ★ Аудит №3 (A10): единое построение класса эффект-BP по .jeb-базе. См. заголовок.
UClass* AAsdaEffectActor::ResolveJebClass(const FString& JebBase)
{
	if (JebBase.IsEmpty()) { return nullptr; }
	// ★ R4, доля a (15.11.2026): загрузка — `AsdaFxClassCache::Load` (тот же `LoadClass`; удержанный класс берётся без загрузчика).
	return AsdaFxClassCache::Load(AsdaAssetPaths::FxClassPathFromJeb(JebBase));   // ★ волна 2 п. 7: правило в реестре путей
}

// ★ Аудит №3 (A10): спавн эффект-BP по готовому имени ассета. См. заголовок.
AAsdaEffectActor* AAsdaEffectActor::SpawnByBPName(UWorld* World, const FString& BPName,
	const FVector& Loc, const FRotator& Rot, AActor* Owner)
{
	if (!World || BPName.IsEmpty()) { return nullptr; }
	// ★ R4, доля a (15.11.2026): загрузка — `AsdaFxClassCache::Load` (тот же `LoadClass`; удержанный класс берётся без загрузчика).
	UClass* Cls = AsdaFxClassCache::Load(AsdaAssetPaths::FxClassPathFromBp(BPName));   // ★ волна 2 п. 7
	if (!Cls) { return nullptr; }   // BP не собран — логирует вызывающий (свой контекст лога)
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SP.Owner = Owner;
	return World->SpawnActor<AAsdaEffectActor>(Cls, Loc, Rot, SP);
}

// ★ Д4: ЕДИНЫЙ спавн эффект-BP по .jeb-базе. См. заголовок.
AAsdaEffectActor* AAsdaEffectActor::SpawnByJebName(UWorld* World, const FString& JebBase,
	const FVector& Loc, const FRotator& Rot, AActor* Owner)
{
	if (JebBase.IsEmpty()) { return nullptr; }
	return SpawnByBPName(World, AsdaAssetPaths::FxBpName(JebBase), Loc, Rot, Owner);   // ★ волна 2 п. 7
}

// ★ Аудит №3 (B5): единое время жизни одноразового эффекта — см. шапку метода в заголовке.
float AAsdaEffectActor::NaturalLifeSpan(float Pad, float MinS, float MaxS) const
{
	float Life = 0.f;
	for (const FAsdaFxEmitter& Em : Emitters) { Life = FMath::Max(Life, Em.StartDelay + Em.Period); }
	// ★ 08.10.2026 (ЦИКЛ 1:1, МЕЛК-1 ревизии fx_loop): эталон держит эффект до конца КОНТЕЙНЕРА 0/2
	//   (`objAnimRotateUpdate@0x114E050` → `emitterRecordsResetTimers`), застывшие эмиттеры endMode 0 видны
	//   до того же момента. Срок по самому длинному эмиттеру обрезал хвост у 269 из 1461 застывающих
	//   (медиана 0.67 с). Контейнер 1/3 срок не продлевает: вечный повтор одноразового эффекта ограничен
	//   как и раньше (`MaxS`). Откат — `asda.Fx.Loop 0`.
	float CDur = 0.f; int32 CEnd = 3;
	if (PeekLoopContainer(CDur, CEnd) && (CEnd == 0 || CEnd == 2) && CDur > 0.f) { Life = FMath::Max(Life, CDur); }
	return FMath::Clamp(Life + Pad, MinS, MaxS);
}

// ★ Дедуп №2 В1-C (F12): одноразовый эффект на носителе без привязки. См. заголовок.
AAsdaEffectActor* AAsdaEffectActor::SpawnOnCarrier(UWorld* World, const FString& JebBase, const FVector& At, AActor* Carrier,
	const FRotator& Rot)
{
	AAsdaEffectActor* Fx = SpawnByJebName(World, JebBase, At, Rot, Carrier);
	if (Fx) { Fx->SetLifeSpan(Fx->NaturalLifeSpan()); }
	return Fx;
}

// ★ Дедуп №2 В1-C (F12): одноразовый эффект на носителе с привязкой. См. заголовок.
AAsdaEffectActor* AAsdaEffectActor::SpawnOnCarrier(UWorld* World, const FString& JebBase, const FVector& At, AActor* Carrier,
	USkeletalMeshComponent* Skel, FName Bone, EAsdaFxAttachMode Mode, const FVector& Offset)
{
	AAsdaEffectActor* Fx = SpawnByJebName(World, JebBase, At, FRotator::ZeroRotator, Carrier);
	if (!Fx) { return nullptr; }
	Fx->ConfigureBoneAttach(Carrier, Skel, Bone, Mode, Offset);
	Fx->SetLifeSpan(Fx->NaturalLifeSpan());
	return Fx;
}

// ★ Дедуп №2 В1-C (F12): таблица режимов привязки эталона. Тело — дословно бывший `switch` канала состояний
//   (`UAsdaEffectsSubsystem::SpawnBuffFx`), подъём 5/7 берётся из аргумента (у канала — его свойство `BuffFxTrackRiseZ`).
bool AAsdaEffectActor::EtalonAttach(int32 Code, int32 SizeClass, float TrackRiseZ, EAsdaFxAttachMode& OutMode, float& OutRiseZ)
{
	OutMode = EAsdaFxAttachMode::CharPos;
	OutRiseZ = 0.f;
	switch (Code)
	{
	case 5:  OutMode = EAsdaFxAttachMode::CharPos;   OutRiseZ = TrackRiseZ; return true;   // +15 по игровой Y
	case 7:  OutMode = EAsdaFxAttachMode::BonePos;   OutRiseZ = TrackRiseZ; return true;   // кость + 15
	case 6:
	case 8:  OutMode = EAsdaFxAttachMode::CharWorld; return true;                          // полная матрица носителя
	case 3:
	case 9:  OutMode = EAsdaFxAttachMode::CharPos;   return true;                          // позиция сущности
	case 12: OutMode = EAsdaFxAttachMode::CharPos;                                         // +25/45/60 по классу
	         OutRiseZ = (SizeClass == 0) ? 25.f : (SizeClass == 1 ? 45.f : 60.f); return true;
	case 13: OutMode = EAsdaFxAttachMode::CharPos;                                         // НАД сущностью
	         OutRiseZ = (SizeClass == 0) ? 200.f : (SizeClass == 1 ? 400.f : 600.f); return true;
	default: return false;
	}
}

AAsdaEffectActor::AAsdaEffectActor()
{
	PrimaryActorTick.bCanEverTick = true;
	// Корень — отдельный SceneComponent (несёт трансформ размещения). Меш — ДОЧЕРНИЙ с абсолютным
	// identity-трансформом: частицы симулируются в МИРОВЫХ координатах (vel/gravity мировые), и вершины
	// секций уже мировые. (Если меш = корень, трансформ актора применился бы к мировым вершинам ДВАЖДЫ —
	// геометрия улетала за карту, фокус двойным кликом телепортировал в никуда.)
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FxMesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetUsingAbsoluteLocation(true);
	Mesh->SetUsingAbsoluteRotation(true);
	Mesh->SetUsingAbsoluteScale(true);
	Mesh->SetCastShadow(false);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// ★ Эффекты рисуются ПОСЛЕ прозрачной воды (движок: эффекты после мира, ZWRITE=0). Без приоритета UE
	//   сортирует translucent-vs-translucent по глубине per-frame → порядок квад/вода флипается = мерцание
	//   («рвётся в моменте») у волн pado на воде.
	Mesh->SetTranslucentSortPriority(5);
}

void AAsdaEffectActor::OnConstruction(const FTransform&)
{
	if (Mesh) Mesh->SetWorldTransform(FTransform::Identity);   // мировые вершины → identity-компонент
	ElapsedTime = 0.f;
	ResetStates();
	// ПРОГРЕВ: ~3 сек симуляции при конструировании — геометрия появляется сразу (секции PMC сериализуются
	// в уровень), даже если вьюпорт без Realtime (RDP его отключает) и Tick не идёт.
	// ★ ТОЛЬКО В РЕДАКТОРЕ. В ИГРЕ прогрев ломает короткоживущие эффекты (метка клика живёт 1.1 с):
	//   спавн сразу «прокручивал» эффект за пределы жизни → цвет/альфа уходили в 0, оставался серый след.
	//   В игре симуляцию с нуля ведёт Tick, прогрев не нужен.
	const UWorld* W = GetWorld();
	if (!W || !W->IsGameWorld())
	{
		// ★ 15.09.2026: точка наблюдателя — ОДИН снимок на весь прогрев (было: GetViewPoint + GetViewLocation на
		//   каждый эмиттер каждого из 90 шагов). Цикл синхронный, внутри него не меняются ни вьюпорты редактора,
		//   ни отрисованные виды мира (их пишет рендер/конец UWorld::Tick), ни оверрайд превью (его пишет панель
		//   вне прогрева). Точка частиц на прогреве, как и раньше, фолбэк (nullptr) — снимок даёт только поворот
		//   частиц и камеру биллборда 0xC, ровно то, что прежде спрашивалось внутри BuildQuads.
		FAsdaFxViewSample WarmView;
		SampleView(WarmView);
		for (int32 K = 0; K < 90; ++K) SimulateStep(1.f / 30.f, nullptr, WarmView);
	}
}

// ═══ ★★★ ВОЛНА Ф13 (20.09.2026): КАТЕГОРИЯ ЛОГА — ВНЕ РЕДАКТОРСКОГО БЛОКА ═════════════════════════
//   ЧТО БЫЛО НЕ ТАК. Категория объявлялась ВНУТРИ `#if WITH_EDITOR` (ниже по файлу), а пишет в неё
//   не только редакторский код: сторож `SaveEmitterGlow` (см. ниже) живёт в ОБЩЕМ коде и зовётся из
//   `PreSave`. Редакторская цель собиралась, игровая — нет: «'LogAsdaFxManualLock': undeclared
//   identifier». Прошлая волна этого не поймала, потому что собирала только AsdaRebornEditor.
//   ЛЕЧЕНИЕ. Объявление вынесено из редакторского блока — оно общее для обеих целей. Имя оставлено
//   прежним (по нему уже написаны `log LogAsdaFxManualLock Verbose` в справках и памятках), хотя
//   пишут в категорию теперь два соседних сюжета: замок «переопределено вручную» и поэмиттерное
//   свечение. В shipping-цели макрос сам сворачивается в пустую категорию — лишнего веса нет.
// ★ [ИСПРАВЛЕНИЕ] ЦВЕТ ВЕРШИН СВЕЧЕНИЯ ЗАТОЧКИ (08.11.2026, пачка 14, свечение Hero).
//   ДВИЖОК. `CreateMeshSection_LinearColor` по умолчанию пишет цвет как есть, `UpdateMeshSection_LinearColor` — кодирует
//   в sRGB. Порт звал обе без параметра, поэтому со второго кадра цвет вершины поднят: 0.24 → 0.53, 0.70 → 0.85.
//   ЭТАЛОН. Цвет вершины (FVF 0x142, diffuse) умножается на текстуру и смешивается в кадре D3D9 БЕЗ гаммы: значения
//   складываются как значения экрана. Коэффициенты смешивания — из шапки .jeb (+0x44/+0x48, D3DBLEND = диск + 1):
//   свечение заточки 4/1 → SRCALPHA/ONE, брызги водопада eff_waterfall-a 2/1 → SRCCOLOR/ONE (вклад = цвет²).
//   UE складывает в ЛИНЕЙНОМ кадре. Для ОДНОГО слоя поверх тёмного честный перевод — sRGB → линейное. Для ПЛОТНЫХ
//   наложений (десятки частиц брызг, цвет гаснет 0.75 → 0 за жизнь) сумма в линейном набирает белое во много раз
//   медленнее, чем сумма значений экрана у эталона, и брызги почти пропадали (кадр
//   Reports/batch14/presets/frames/coast_spray_vertexcolor.jpg). Подъём цвета старым путём это наложение и
//   приближал, на нём откалиброваны карты, навыки и ауры. Поэтому:
//   • все эффекты — прежнее поведение (ключ `asda.Fx.VertexColorMode` 0);
//   • свечение заточки оружия и щита (теги WeaponGlowFx/ShieldGlowFx) — цвет переводится из sRGB в линейное
//     (`asda.Fx.GlowVertexColorLinear` 1): несколько слоёв поверх клинка, на подъёме зелёный Hero выходил серо-белым.
static int32 GAsdaFxVertexColorMode = 0;
static ASDA_DEV_CVAR_REF(CVarAsdaFxVertexColorMode,
	TEXT("asda.Fx.VertexColorMode"),
	GAsdaFxVertexColorMode,
	TEXT("Цвет вершин всех эффектов: 0 (умолчание) = как было (создание секции — как есть, обновление — с кодированием sRGB); "
	     "1 = всегда как есть; 2 = всегда перевод из sRGB в линейное. Свечение заточки — отдельным ключом asda.Fx.GlowVertexColorLinear."),
	ECVF_Default);

static int32 GAsdaFxGlowVertexColorLinear = 1;
static ASDA_DEV_CVAR_REF(CVarAsdaFxGlowVertexColorLinear,
	TEXT("asda.Fx.GlowVertexColorLinear"),
	GAsdaFxGlowVertexColorLinear,
	TEXT("1 (умолчание) = у свечения заточки оружия и щита цвет вершин переводится из sRGB в линейное (Hero зелёный); "
	     "0 = как у всех эффектов (откат)."),
	ECVF_Default);

// Режим цвета вершин секций этого меша: 0 — прежний, 1 — как есть, 2 — линейный.
int32 AsdaFxVertexColorModeFor(const UProceduralMeshComponent* Mesh)
{
	if (GAsdaFxGlowVertexColorLinear != 0)
	{
		static const FName kTagW(AsdaCharFxTags::kWeaponGlow);
		static const FName kTagS(AsdaCharFxTags::kShieldGlow);
		const AActor* Owner = Mesh ? Mesh->GetOwner() : nullptr;
		if (Owner && (Owner->ActorHasTag(kTagW) || Owner->ActorHasTag(kTagS))) { return 2; }
	}
	return GAsdaFxVertexColorMode;
}

DEFINE_LOG_CATEGORY(LogAsdaFxManualLock);   // ★ R2: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей
