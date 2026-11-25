// Copyright (c) 2026 @sitxovski (Discord)

#include "World/AsdaFxLightSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "World/AsdaFxLightComponent.h"
#include "World/AsdaFxLightSettings.h"
#include "World/AsdaVisibilitySubsystem.h"   // ★ Волна Ф9: ОБЩАЯ точка «объект попадает в кадр» (расширенный конус, гистерезис, предсказание по угловой скорости)
#include "UI/AsdaDevMenuSubsystem.h"
#include "AsdaEffectActor.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#endif
#include "AsdaFxLightSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaFxLightSubsystem*.cpp

// ★ волна 3 п.12 (15.09.2026): категория LogAsdaFxLight объявлена один раз — World/AsdaFxLightSettings.h (была вторая статическая копия здесь)
// ─────────────── КЛЮЧИ ВОЛНЫ Ф2-ВСЕ (регистрация единственная в Source/) ───────────────
// Общий откат: 0 = ни один эффект-свет не горит (состояние «как было до Ф2»), 1 = механизм работает.
static int32 GAsdaFxLight = 1;
static FAutoConsoleVariableRef CVarAsdaFxLight(
	TEXT("asda.Fx.Light"),
	GAsdaFxLight,
	TEXT("Эффект-света: 1 = эффекты заводят рядом с собой источник света (числа — поле «Свет эффекта» ассета, дефолты — Tools/asda_fx_light.json); 0 = откат, света нет ни у кого."),
	ECVF_Default);

// Показ в ОКНЕ РЕДАКТОРА отдельным ключом: свет в редакторе иногда мешает (аудит освещения карты,
// снятие эталонных кадров), и гасить его ради этого во всей игре неправильно.
static int32 GAsdaFxLightEditor = 1;
static void OnAsdaFxLightEditorChanged(IConsoleVariable*) { UAsdaFxLightSubsystem::MarkAllDirty(); }
static ASDA_DEV_CVAR_REF(CVarAsdaFxLightEditor,
	TEXT("asda.Fx.Light.Editor"),
	GAsdaFxLightEditor,
	TEXT("Показывать эффект-света в окне редактора без запуска Play: 1 = да (нужно, чтобы крутить яркость руками), 0 = только в игре."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaFxLightEditorChanged),
	ECVF_Default);

// Бюджет КОЛИЧЕСТВА: ≥0 — ровно столько одновременно; −1 — по уровню [AsdaFxLight@N]; −2 — без ограничения.
int32 GAsdaFxLightBudget = -1;
static void OnAsdaFxLightBudgetChanged(IConsoleVariable*) { UAsdaFxLightSubsystem::MarkAllDirty(); }
static FAutoConsoleVariableRef CVarAsdaFxLightBudget(
	TEXT("asda.Fx.Light.Budget"),
	GAsdaFxLightBudget,
	TEXT("Сколько эффект-светов горит одновременно: >=0 ровно столько; -1 по уровню sg.EffectsQuality из [AsdaFxLight@N] Light.FxBudget; -2 без ограничения (замеры)."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaFxLightBudgetChanged),
	ECVF_Default);

// ★★★ ВОЛНА Ф8 (10.09.2026): СКОЛЬКО ИСТОЧНИКОВ РАЗРЕШЕНО ОДНОМУ ЭФФЕКТУ.
// Отдельный кошелёк, а НЕ общий бюджет количества, и вот почему. Общий бюджет отбирает по счёту
// «ярче и ближе», и эффект с семью светящими эмиттерами, стоящий у самого носа камеры, честно
// выиграл бы все семь мест — вытеснив семь ДРУГИХ эффектов сцены. Игрок увидел бы это не как
// «башня стала точнее», а как «вокруг всё погасло». Поэтому число светов ОДНОГО эффекта
// ограничивается ДО общего отбора, и лишние эмиттеры отсекаются по собственному вкладу.
// ★★★ ВОЛНА Ф8: ДВА ОТЛАДОЧНЫХ КЛЮЧА ДЛЯ ЗАМЕРА ЦЕНЫ. Оба нужны ровно потому, что мерить цену новой
// формы и поэмиттерного света иначе НЕЧЕМ: поля лежат в 1274 ассетах, и «поменять руками ради замера»
// означало бы менять ту самую величину, которую замеряешь. Ключи ничего не сохраняют и по умолчанию
// выключены (-1 = как в полях).
int32 GAsdaFxLightForceShape = -1;
static void OnAsdaFxLightForceShapeChanged(IConsoleVariable*) { UAsdaFxLightSubsystem::MarkAllDirty(); }
static ASDA_DEV_CVAR_REF(CVarAsdaFxLightForceShape,
	TEXT("asda.Fx.Light.ForceShape"),
	GAsdaFxLightForceShape,
	TEXT("ЗАМЕР: форма ВСЕХ эффект-светов. -1 как в полях (по умолчанию); 0 точечный; 1 прямоугольный; 2 конусный; 3 трубка; 4 кольцо; 5 авто по геометрии эмиттера; 6 блин (плоский диск); 7 шар (объёмный)."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaFxLightForceShapeChanged),
	ECVF_Cheat);

// ★★★ ВОЛНА Ф11 (20.09.2026): ПРИНУДИТЕЛЬНАЯ РАСКЛАДКА ФОРМЫ НА N ИСТОЧНИКОВ.
//   Нужен ровно для замера цены: «сколько стоит форма из N источников» проверяется одним ключом на
//   всей карте, а не правкой полей у каждого эффекта (и не переимпортом ассетов — закон
//   owner-asset-edits-never-lose). Действует на трубку, прямоугольник и блин.
int32 GAsdaFxLightForceSegments = -1;
static void OnAsdaFxLightForceSegmentsChanged(IConsoleVariable*) { UAsdaFxLightSubsystem::MarkAllDirty(); }
static ASDA_DEV_CVAR_REF(CVarAsdaFxLightForceSegments,
	TEXT("asda.Fx.Light.ForceSegments"),
	GAsdaFxLightForceSegments,
	TEXT("ЗАМЕР: на сколько источников раскладывается форма у ВСЕХ эффект-светов. -1 как в полях (по умолчанию); 1..16 ровно столько (у блина — число полос, 1..12; у кольца — дуг, 3..8). ★ fix9: у блина было написано «2..8» — нижняя граница устарела ещё в fix8 (одна полоса законна с КРИТ-2 fix3), верхняя — в fix9 вместе с потолком правила."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaFxLightForceSegmentsChanged),
	ECVF_Cheat);

// ★★★ РЕВИЗИЯ fix5 (КРИТ-А, 20.09.2026): ПРИНУДИТЕЛЬНЫЙ ПОТОЛОК ПЛИТОК ВДОЛЬ КУСКА.
//   Нужен для замера «цена покрытия»: 1 возвращает ровно поведение fix4 (полоса по середине формы),
//   больше — решётку плиток встык. Одним ключом на всей карте, а не правкой полей у каждого эффекта
//   (закон owner-asset-edits-never-lose), и ровно тем же путём, что и прочие замерные ключи формы.
int32 GAsdaFxLightForceTiles = -1;
static void OnAsdaFxLightForceTilesChanged(IConsoleVariable*) { UAsdaFxLightSubsystem::MarkAllDirty(); }
static ASDA_DEV_CVAR_REF(CVarAsdaFxLightForceTiles,
	TEXT("asda.Fx.Light.ForceTiles"),
	GAsdaFxLightForceTiles,
	TEXT("ЗАМЕР: потолок ПЛИТОК вдоль одного куска формы у ВСЕХ эффект-светов. -1 как в полях (по умолчанию); 1 — поведение fix4 (форма крупнее радиуса света светит полосой по середине); 2..8 — полоса складывается из плиток встык и форма светит целиком."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaFxLightForceTilesChanged),
	ECVF_Cheat);

// ★★★ ВОЛНА Ф11: ПРИНУДИТЕЛЬНЫЙ РАЗМЕР ФОРМЫ, ЮНИТОВ.
//   Без него доказать «светит по форме, а не из центра» НЕЧЕМ: у запечённых эффектов секции мелкие
//   (десятки юнитов), и форма любого вида укладывается в радиус затухания целиком — то есть разницы
//   между «одним источником» и «раскладкой» на них не видно ни глазом, ни пикселем. Ключ задаёт
//   длину трубки / ширину прямоугольника / диаметр блина ЯВНО, переводя размер в ручной режим;
//   тогда при форме 1200 юн и радиусе 400 юн «свет из центра» виден как чёрные края формы.
float GAsdaFxLightForceShapeSize = -1.f;
static void OnAsdaFxLightForceShapeSizeChanged(IConsoleVariable*) { UAsdaFxLightSubsystem::MarkAllDirty(); }
static ASDA_DEV_CVAR_REF(CVarAsdaFxLightForceShapeSize,
	TEXT("asda.Fx.Light.ForceShapeSize"),
	GAsdaFxLightForceShapeSize,
	TEXT("ЗАМЕР: размер формы у ВСЕХ эффект-светов, юнитов. -1 как в полях (по умолчанию); >0 — длина трубки, ширина прямоугольника, диаметр блина и кольца, диаметр шара."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaFxLightForceShapeSizeChanged),
	ECVF_Cheat);

// ★★★ ВОЛНА Ф11: ПРИНУДИТЕЛЬНЫЙ ДОВОРОТ ФОРМЫ ПО ТАНГАЖУ, ГРАДУСОВ.
//   Тоже замерный: плоскость блина и прямоугольника задаёт ГЕОМЕТРИЯ эмиттера, и у запечённых
//   эффектов она какая есть. Чтобы снять кадр «блин лежит на полу и светит всей площадью», плоскость
//   надо повернуть — а править ради кадра ассет запрещено (закон owner-asset-edits-never-lose).
float GAsdaFxLightForceShapePitch = 0.f;
static void OnAsdaFxLightForceShapePitchChanged(IConsoleVariable*) { UAsdaFxLightSubsystem::MarkAllDirty(); }
static ASDA_DEV_CVAR_REF(CVarAsdaFxLightForceShapePitch,
	TEXT("asda.Fx.Light.ForceShapePitch"),
	GAsdaFxLightForceShapePitch,
	TEXT("ЗАМЕР: доворот формы по тангажу у ВСЕХ эффект-светов, градусов (0 = как в полях). 90 кладёт вертикальную плоскость блина или прямоугольника горизонтально."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaFxLightForceShapePitchChanged),
	ECVF_Cheat);

int32 GAsdaFxLightForceScope = -1;
static void OnAsdaFxLightForceScopeChanged(IConsoleVariable*) { UAsdaFxLightSubsystem::RescanAll(); }
static ASDA_DEV_CVAR_REF(CVarAsdaFxLightForceScope,
	TEXT("asda.Fx.Light.ForceScope"),
	GAsdaFxLightForceScope,
	TEXT("ЗАМЕР: уровень света у ВСЕХ эффектов. -1 как в полях (по умолчанию); 0 один на эффект; 1 свой у каждого эмиттера."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaFxLightForceScopeChanged),
	ECVF_Cheat);

int32 GAsdaFxLightPerEffect = -1;
static void OnAsdaFxLightPerEffectChanged(IConsoleVariable*) { UAsdaFxLightSubsystem::RescanAll(); }
static FAutoConsoleVariableRef CVarAsdaFxLightPerEffect(
	TEXT("asda.Fx.Light.PerEffect"),
	GAsdaFxLightPerEffect,
	TEXT("Сколько источников разрешено ОДНОМУ эффекту в режиме «свой свет у каждого эмиттера»: >=1 ровно столько; -1 по уровню sg.EffectsQuality из [AsdaFxLight@N] Light.FxPerEffect; -2 без ограничения (замеры)."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaFxLightPerEffectChanged),
	ECVF_Default);

// ★ ЭКРАННЫЙ БЮДЖЕТ: сумма (радиус/дистанция)² по светящим. ГЛАВНЫЙ ограничитель — цена бестеневого
// света определяется площадью его экранного следа, а не их числом (замер волны Ф2 §4).
float GAsdaFxLightScreenBudget = -1.f;
static void OnAsdaFxLightScreenBudgetChanged(IConsoleVariable*) { UAsdaFxLightSubsystem::MarkAllDirty(); }
static FAutoConsoleVariableRef CVarAsdaFxLightScreenBudget(
	TEXT("asda.Fx.Light.ScreenBudget"),
	GAsdaFxLightScreenBudget,
	TEXT("Экранный бюджет эффект-светов — сумма (радиус/дистанция)^2 по светящим: >=0 ровно столько; -1 по уровню из [AsdaFxLight@N] Light.FxScreenBudget; -2 без ограничения (замеры)."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaFxLightScreenBudgetChanged),
	ECVF_Default);

// ═══════════ ★★★ ВОЛНА Ф9 (10.09.2026): ОТБОР ПО КАДРУ, А НЕ ПО ДИСТАНЦИИ ═══════════
//   Требование владельца дословно: «Свет не должен исчезать при удалении камеры/игрока и рисоваться
//   только вблизи. Всё, что в зоне камеры/игрока, всё, что ему видно — рисуем и показываем.»
//   То есть критерий — ПОПАДАНИЕ В КАДР, а не расстояние.
//
//   ПОЧЕМУ ЭТО НЕ ДОРОГО (и почему всё же меряем). Цена источника в отложенном рендере растёт с
//   ПЛОЩАДЬЮ ЕГО ЭКРАННОГО СЛЕДА, а она падает как квадрат дистанции: далёкий свет занимает единицы
//   тайлов light grid. По количеству цена насыщается (замер Ф2-ВСЕ §5: 8 и 26 источников стоят
//   одинаково), дорог радиус. Поэтому дистанция — плохой ограничитель, а экранная площадь — хороший:
//   она и остаётся ограничителем (Light.FxScreenBudget), дистанционные гейты снимаются.
//
//   0 = ПО ДИСТАНЦИИ (поведение волн Ф2…Ф8: min(Spec.MaxDrawDistance, CullDistance эффекта)
//       × r.LightMaxDrawDistanceScale) — полный откат одной строкой;
//   1 = ПО КАДРУ (по умолчанию): кандидат светит, если его СФЕРА (позиция + радиус) попадает в
//       расширенный конус камеры UAsdaVisibilitySubsystem, а дальность движка снята (MaxDrawDistance 0).
static int32 GAsdaFxLightCull = 1;
static void OnAsdaFxLightCullChanged(IConsoleVariable*) { UAsdaFxLightSubsystem::MarkAllDirty(); }
static FAutoConsoleVariableRef CVarAsdaFxLightCull(
	TEXT("asda.Fx.Light.Cull"),
	GAsdaFxLightCull,
	TEXT("Чем отбирается эффект-свет: 1 = ПОПАДАНИЕМ В КАДР (расширенный конус камеры, дальность движка снята) — по умолчанию; 0 = дистанцией (поведение до волны Ф9, откат)."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaFxLightCullChanged),
	ECVF_Default);

// ★ ПЛАВНОЕ ПОЯВЛЕНИЕ, с. Отбор по кадру двоичен, и без хода силы влетевший в кадр свет вспыхивал бы
//   разом. 0 = мгновенно (сравнение «с ходом / без хода» в замере).
float GAsdaFxLightFadeIn = 0.25f;
static FAutoConsoleVariableRef CVarAsdaFxLightFadeIn(
	TEXT("asda.Fx.Light.FadeInSec"),
	GAsdaFxLightFadeIn,
	TEXT("За сколько секунд эффект-свет разгорается до полной силы, войдя в кадр (0 = мгновенно)."),
	ECVF_Default);

// ★ ПЛАВНОЕ УГАСАНИЕ, с. Должно укладываться в отсрочку GraceSec: место в бюджете держится ровно
//   столько, и не догоревший к её концу свет пришлось бы снять рывком.
float GAsdaFxLightFadeOut = 0.35f;
static FAutoConsoleVariableRef CVarAsdaFxLightFadeOut(
	TEXT("asda.Fx.Light.FadeOutSec"),
	GAsdaFxLightFadeOut,
	TEXT("За сколько секунд эффект-свет гаснет, выйдя из кадра (0 = мгновенно). Должно быть меньше asda.Fx.Light.GraceSec."),
	ECVF_Default);

// ★ ОТСРОЧКА ПОСЛЕ ВЫХОДА ИЗ КАДРА, с. Кандидат, ушедший за границу расширенного конуса, ещё
//   столько секунд ДЕРЖИТ место и гаснет ходом. Смысл — быстрое вращение камеры: при рывке
//   «туда-обратно» свет не снимается и не заводится заново, а просто разворачивает ход.
static float GAsdaFxLightGrace = 0.5f;
static FAutoConsoleVariableRef CVarAsdaFxLightGrace(
	TEXT("asda.Fx.Light.GraceSec"),
	GAsdaFxLightGrace,
	TEXT("Сколько секунд вышедший из кадра эффект-свет держит место в бюджете, догорая (0 = снимать сразу)."),
	ECVF_Default);

// ★★★ РЕВИЗИЯ fix3 (С-4, 20.09.2026): ГИСТЕРЕЗИС ВЫДАННОЙ ПОДРОБНОСТИ ФОРМЫ, с.
//   У аренды («кто горит») гистерезис был с волны Ф2, у ПОДРОБНОСТИ — не было вовсе: кандидат на
//   самом краю бюджета получал то 2 места, то 1, и форма моргала покадрово. Понижение подробности
//   откладывается на столько секунд; всё это время удержанные источники ЧЕСТНО списываются с обоих
//   бюджетов (иначе гистерезис стал бы способом их пробить). Повышение — мгновенное.
float GAsdaFxLightDetailHold = 0.5f;
static FAutoConsoleVariableRef CVarAsdaFxLightDetailHold(
	TEXT("asda.Fx.Light.DetailHoldSec"),
	GAsdaFxLightDetailHold,
	TEXT("Сколько секунд удерживается ранее выданная ПОДРОБНОСТЬ формы (число источников), прежде чем раздача понизит её. 0 = понижать сразу (поведение до ревизии fix3: форма моргает на границе бюджета)."),
	ECVF_Default);

// ═══ ★★★ ВОЛНА fix9 (пункт С-4 ревизии fix8, 20.09.2026): ПОТОЛОК МЕСТ НА ОДНОГО КАНДИДАТА ═══════
//
//   ЧТО ПОКАЗАЛ ЗАМЕР. Волна fix8 подняла массив раскладки 64 → 80, и вместе с ним вырос СПРОС
//   ОДНОГО БЛИНА на бюджет количества при полной выдаче (an9_cap11.py, свет 608, потолок полос 10):
//   9× — 56 мест (было 48 при массиве 64 и потолке 8), 12× — 70, 16× — 76, 20× и крупнее — 78…80.
//   Бюджет владельца — 96 на ВСЮ карту, то есть ОДИН крупный блин вправе занять до 80 % карты.
//   Раздача (Select, проходы A…D) потолка на кандидата не имеет вовсе: пройдя «каждому по одному»,
//   она поднимает подробность до ПОЛНОЙ цены в порядке счёта.
//   ★ ЧЕСТНО: на alpen при умолчаниях блинов НЕТ ни одного (замер fix8: 97 кандидатов, все точкой),
//     поэтому сегодня это не дефект картинки, а открытая дверь. Ключ её закрывает, но дефолт НЕ
//     МЕНЯЕТСЯ: молча урезать выдачу — значит сделать блин грубее там, где владелец этого не просил
//     (тот же закон, что у ForceShape: замерный ключ не вправе менять умолчание).
//   ★ Проход B (минимум формы: кольцу три дуги) потолком НЕ режется — ниже него форма перестаёт
//     быть собой, и резать там нечего, кроме самой формы.
int32 GAsdaFxLightPerCandMax = 0;
static FAutoConsoleVariableRef CVarAsdaFxLightPerCandMax(
	TEXT("asda.Fx.Light.PerCandMax"),
	GAsdaFxLightPerCandMax,
	TEXT("Потолок мест бюджета количества на ОДНОГО кандидата (0 = без потолка, умолчание — поведение волн Ф11…fix8). Зачем: при массиве раскладки 80 один блин 16× просит 76 мест из владельческих 96 на всю карту, и раздача отдаёт их ему целиком — соседние эффекты остаются без света. Минимум формы (кольцу три дуги) потолком не режется: ниже него форма перестаёт быть собой."),
	ECVF_Default);

// ★ ПОТОЛОК РАДИУСА, юн: радиус — самая дорогая величина механизма, и на слабых уровнях рубить надо его.
float GAsdaFxLightRadiusCap = -1.f;
static FAutoConsoleVariableRef CVarAsdaFxLightRadiusCap(
	TEXT("asda.Fx.Light.RadiusCap"),
	GAsdaFxLightRadiusCap,
	TEXT("Потолок радиуса затухания эффект-света, юн: >=1 ровно столько; -1 по уровню из [AsdaFxLight@N] Light.FxRadiusCap; -2 без ограничения (замеры)."),
	ECVF_Default);

// Глобальный множитель силы всех эффект-светов — калибровка шкалы яркости на живой карте без пересборки.
float GAsdaFxLightScale = 1.f;
static FAutoConsoleVariableRef CVarAsdaFxLightScale(
	TEXT("asda.Fx.Light.Scale"),
	GAsdaFxLightScale,
	TEXT("Глобальный множитель силы всех эффект-светов (1 = числа полей «Свет эффекта»). Только рантайм, не персистится."),
	ECVF_Default);

// Глобальный множитель РАДИУСА всех эффект-светов — замер цены по площади экранного следа.
float GAsdaFxLightRadiusScale = 1.f;
static FAutoConsoleVariableRef CVarAsdaFxLightRadiusScale(
	TEXT("asda.Fx.Light.RadiusScale"),
	GAsdaFxLightRadiusScale,
	TEXT("Глобальный множитель радиуса затухания всех эффект-светов (1 = числа полей). Замеры цены по радиусу; не персистится."),
	ECVF_Default);

// ★★★ ВОЛНА «форма-2» (21.09.2026), п. 1 ревизии: УДЕРЖАНИЕ ГАБАРИТА ФОРМЫ — ЗАМЕРНЫЙ КЛЮЧ.
//   Поле эффекта — «6.40. Размер формы: удержание габарита, с»; довод и замер дыхания секций
//   (×40…×150 за период) — в шапке этого поля (World/AsdaFxLightSpec.h). Ключ нужен ровно затем,
//   чтобы «с удержанием» и «без» снимались ОДНИМ прогоном на ОДНОМ бинаре, а не двумя запусками.
//   −1 = «как в поле у каждого эффекта» (поставка), 0 = выключить всем, >0 = задать всем.
float GAsdaFxLightGeomHoldSec = -1.f;
static FAutoConsoleVariableRef CVarAsdaFxLightGeomHoldSec(
	TEXT("asda.Fx.Light.GeomHoldSec"),
	GAsdaFxLightGeomHoldSec,
	TEXT("Удержание габарита формы, секунд, ПОВЕРХ поля эффекта: −1 = как в поле (поставка), 0 = выключить удержание всем эффектам (поведение до 21.09.2026), >0 = задать всем. Габарит растёт мгновенно, падает не быстрее этого времени; лечит «свет из точки» на дне вдоха дышащей секции. Не персистится."),
	ECVF_Default);

// ★★★ ВОЛНА «форма-2» (21.09.2026), п. 2 ревизии: ПЕРЕВЫБОР НАБОРА, КОТОРЫЙ ВЕСЬ ТЁМЕН.
//   Набор поэмиттерных светов выбирается ОДИН раз и по СТАТИЧЕСКОМУ весу узлов — до первого кадра,
//   когда живой геометрии нет ни у кого. У эффектов из десятков ОДИНАКОВЫХ по числам эмиттеров
//   (ch1_a_4th_ft_06_1: 24 эмиттера S_001…S_024 с одними и теми же SizeX 60 и альфой 1) веса
//   РАВНЫ, и кошелёк достаётся произвольной восьмёрке. Замер показал, чем это кончается: из восьми
//   выбранных у `ft_06_1` пятеро (#2 #4 #6 #12 #13) не построили секцию НИ В ОДНОМ из пяти замеров
//   периода, то есть занимали место в кошельке, не умея светить вовсе; у `wholicb03` из восьми
//   выбранных живыми были четверо, а горело НОЛЬ при 99 живых эмиттерах в том же кадре.
//   Отсюда и «асимметрия зажигания» прежней волны: при охвате «один на эффект» агрегат берёт ВСЕ
//   эмиттеры и потому горит, а поэмиттерный набор — застывшая восьмёрка, которая может целиком
//   попасть в тёмную фазу.
//   ЛЕЧЕНИЕ: если У ВСЕГО набора эффекта свет погас и держится погасшим дольше этого времени —
//   набор пересобирается ОДИН раз, с предпочтением тех эмиттеров, у которых ПРЯМО СЕЙЧАС есть
//   построенная секция и ненулевая альфа. Перевыбор редкий и с отсрочкой, поэтому «мигания при
//   подходе», ради которого набор и делали неподвижным, он не возвращает.
//   0 = выключить перевыбор (поведение до 21.09.2026).
float GAsdaFxLightDarkRepickSec = 0.5f;
static FAutoConsoleVariableRef CVarAsdaFxLightDarkRepickSec(
	TEXT("asda.Fx.Light.DarkRepickSec"),
	GAsdaFxLightDarkRepickSec,
	TEXT("Через сколько секунд полной темноты ВСЕГО набора поэмиттерных светов эффекта набор пересобирается с предпочтением живых эмиттеров. 0 = не пересобирать (поведение до 21.09.2026)."),
	ECVF_Default);

// ★★★ ВОЛНА Ф10 (11.09.2026): МАСШТАБ ДЛИНЫ МИРА (сцена логина ×100). 1 = применять список
//   UAsdaFxLightSettings::WorldScales (по умолчанию), 0 = откат: все миры считаются обычными.
static int32 GAsdaFxLightWorldScale = 1;
static void OnAsdaFxLightWorldScaleChanged(IConsoleVariable*) { UAsdaFxLightSubsystem::MarkAllDirty(); }
static FAutoConsoleVariableRef CVarAsdaFxLightWorldScale(
	TEXT("asda.Fx.Light.WorldScale"),
	GAsdaFxLightWorldScale,
	TEXT("Масштаб длины миров для эффект-светов: 1 = по списку WorldScales из настроек (длины дефолтов ×K, сила ×K², потолок радиуса ×K); 0 = откат, все миры обычные. С 12.09.2026 у логина K=1 — сцена переведена в обычный масштаб, и список ничего не меняет."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaFxLightWorldScaleChanged),
	ECVF_Default);

float UAsdaFxLightSubsystem::WorldLengthScale(const UWorld* W)
{
	if (GAsdaFxLightWorldScale == 0 || !W) { return 1.f; }
	// Имя мира кэшируется по указателю: ResolveSpec зовётся на каждого кандидата несколько раз за кадр,
	// а сборка строки без PIE-префикса — это аллокация. Поток один (игровой), миров процесса единицы.
	static TWeakObjectPtr<const UWorld> CachedWorld;
	static FName CachedName;
	if (CachedWorld.Get() != W)
	{
		CachedWorld = W;
		CachedName = FName(*UWorld::RemovePIEPrefix(W->GetName()));
	}
	return UAsdaFxLightSettings::Get()->FindWorldLengthScale(CachedName);
}

// ★★★ ВОЛНА Ф4: ВРЕМЕННЫЙ ПЕРЕВОД ВСЕХ СВЕТОВ В ЗАДАННЫЕ РЕЖИМЫ — ключ сравнения «Ф2 против Ф4».
//
//   ЗАЧЕМ КЛЮЧ, А НЕ ПРАВКА ПОЛЕЙ ИЗ PYTHON. Числа режимов лежат в FAsdaFxLightSpec, а в процессе `-game`
//   (то есть на ПУТИ ИГРОКА, которым и положено проверять — закон ui-check-game-path-geometry) UE-Python
//   не видит ни поля `FxLight` актора, ни UENUM без BlueprintType: привязки там генерируются только для
//   blueprint-видимых членов. Значит A/B через питон возможен ТОЛЬКО в редакторе, а редакторный кадр —
//   не тот кадр. Ключ решает это без расширения blueprint-поверхности ради замера.
//
//   ЗНАЧЕНИЯ: −1 = не трогать (числа эффекта/семейства), иначе номер режима перечисления:
//     ForceColorMode  0 Node(донор Ф2) 1 Fixed 2 Mean 3 Median
//     ForceRadiusMode 0 Section(донор Ф2) 1 Fixed 2 Bounds
//     ForcePosMode    0 Section(донор Ф2) 1 Actor 2 Bounds 3 BoundsBox(центр габаритов, Ф7)
//   Ключи РАНТАЙМНЫЕ и не персистятся: в ассеты и ini ничего не пишут, «залипнуть» у владельца не могут.
int32 GAsdaFxLightForceColor = -1;
int32 GAsdaFxLightForceRadius = -1;
int32 GAsdaFxLightForcePos = -1;
static ASDA_DEV_CVAR_REF(CVarAsdaFxLightForceColor,
	TEXT("asda.Fx.Light.ForceColorMode"),
	GAsdaFxLightForceColor,
	TEXT("Сравнение механизмов: перевести ВСЕ эффект-света в режим цвета. -1 не трогать; 0 узел донора (Ф2), 1 явный, 2 средний по вкладу, 3 медиана."),
	ECVF_Cheat);
static ASDA_DEV_CVAR_REF(CVarAsdaFxLightForceRadius,
	TEXT("asda.Fx.Light.ForceRadiusMode"),
	GAsdaFxLightForceRadius,
	TEXT("Сравнение механизмов: перевести ВСЕ эффект-света в режим радиуса. -1 не трогать; 0 геометрия донора (Ф2), 1 постоянный, 2 габарит всего эффекта."),
	ECVF_Cheat);
static ASDA_DEV_CVAR_REF(CVarAsdaFxLightForcePos,
	TEXT("asda.Fx.Light.ForcePosMode"),
	GAsdaFxLightForcePos,
	TEXT("Сравнение механизмов: перевести ВСЕ эффект-света в режим позиции. -1 не трогать; 0 центроид донора (Ф2), 1 актор, 2 центр тяжести всего эффекта, 3 центр ГАБАРИТОВ эффекта (Ф7: источник поднимается над каменным основанием, и тень идёт кольцом, а не клином)."),
	ECVF_Cheat);

void UAsdaFxLightSubsystem::ForcedModes(int32& OutColor, int32& OutRadius, int32& OutPos)
{
	OutColor = GAsdaFxLightForceColor;
	OutRadius = GAsdaFxLightForceRadius;
	OutPos = GAsdaFxLightForcePos;
}

void UAsdaFxLightSubsystem::ForcedShapeScope(int32& OutShape, int32& OutScope, int32* OutSegments)
{
	OutShape = GAsdaFxLightForceShape;
	OutScope = GAsdaFxLightForceScope;
	if (OutSegments) { *OutSegments = GAsdaFxLightForceSegments; }
}

float UAsdaFxLightSubsystem::ForcedShapeSize()
{
	return GAsdaFxLightForceShapeSize;
}

float UAsdaFxLightSubsystem::ForcedShapePitch()
{
	return GAsdaFxLightForceShapePitch;
}

// ★ fix5 (КРИТ-А): потолок плиток вдоль куска, заданный ключом замера. -1 = как в полях.
int32 UAsdaFxLightSubsystem::ForcedShapeTiles()
{
	return GAsdaFxLightForceTiles;
}

// ═══ ★★★ ВОЛНА fix13 (КРИТ-1 ревизии fix12, 20.09.2026): СЛЕД ЗАВЕДЕНИЯ ═══════════════════════
//   `asda.Fx.Light.TraceRegister 1` — по строке на КАЖДОЕ заведение набора светов эффекта:
//   имя .jeb, ОТКУДА взялись числа (ветка цепочки разрешения), замок ассета, охват, сколько
//   светов заказано и сколько было. Заведён затем, что расхождение «при спавне 8 поэмиттерных
//   светов, после пересканирования 1» ревизия ловила КОСВЕННО — по числу кандидатов в дампе,
//   который печатается кадром позже и ветку разрешения не называет вовсе.
//   Умолчание 0: на карте с 700 эффектами это 700 строк на каждое пересканирование.
// ★★★ fix13 (КРИТ-1): СВЕРЯТЬ ЛИ НАБОР СВЕТОВ С ДЕЙСТВУЮЩЕЙ СПЕЦИФИКАЦИЕЙ КАЖДЫЙ ОТБОР.
//   1 (умолчание) — набор следует за спецификацией, кто бы её ни записал. 0 — поведение ровно как
//   до fix13: набор строится один раз при заведении и живёт до пересканирования. Ключ отката нужен
//   затем, чтобы «стало хуже» можно было проверить одним словом, а не пересборкой.
// ★★★ fix17 (ЛОВУШКА, п. 1б задания): ЭТОТ ОТКАТ ВЫКЛЮЧАЕТ НЕ ТОЛЬКО СВЕРКУ. ReconcileSets
//   выходит по нему В САМОМ НАЧАЛЕ, а перепроверку погасших (RecheckExtinguished, fix16) зовёт
//   СВОИМ ХВОСТОМ — и другого вызова у неё нет. Значит `AutoReconcile 0` молча возвращает и
//   дефект fix16: «погасил свечение всем эмиттерам и вернул — света нет». Владельцу, которому
//   мешает именно сверка, отключать надо ЕЁ и знать цену; у перепроверки есть СВОЙ ключ
//   `asda.Fx.Light.EmptyRecheck`. Дамп теперь называет эту причину прямо (см. Dump).
//   ★ ЕДИНСТВЕННАЯ ЛАЗЕЙКА: `bForce` этот ключ перебивает, а bForce взводит смена уровня
//   «Эффекты» (OnQualityLevelsChanged) — на ЭТОМ кадре перепроверка проходит и погасшие
//   возвращаются. Рассчитывать на неё нельзя: владелец в редакторе уровень не меняет.
//   Замер лестницей `f17_lovushka` (fix17): базис 8 светов → погасили всем 0 → AutoReconcile 0 →
//   вернули свечение всем 0 (возвратов 0!) → AutoReconcile 1 снова 8 (возвратов 1).
int32 GAsdaFxLightAutoReconcile = 1;
static FAutoConsoleVariableRef CVarAsdaFxLightAutoReconcile(
	TEXT("asda.Fx.Light.AutoReconcile"),
	GAsdaFxLightAutoReconcile,
	TEXT("1 (умолчание) = каждый отбор сверять набор светов эффекта с действующей спецификацией и заводить заново при расхождении; 0 = как до fix13. ★ ВНИМАНИЕ: 0 выключает ЗАОДНО и возврат погасших эффектов (asda.Fx.Light.EmptyRecheck зовётся хвостом сверки) — погасил свечение всем эмиттерам и вернул, а света нет."),
	ECVF_Default);

// ★ 29.10.2026 (волна производительности): ЧАСТОТА СВЕРКИ наборов в игровом мире, Гц. Сверка (ResolveSpec +
//   ComputeDesiredSet по каждому эффекту-кандидату) шла каждый кадр: на alpen «Низкое» 0.22 мс игрового потока
//   (попарно 3/3, asda.Fx.Light.AutoReconcile 1↔0). Спецификация меняется только правкой/сменой уровня, «тёмный
//   набор» ждёт отсрочку в секунды — 10 Гц (≤ 0.1 с запаздывания) глазом не отличить. 0 = каждый кадр (как было).
float GAsdaFxLightReconcileHz = 10.f;
static FAutoConsoleVariableRef CVarAsdaFxLightReconcileHz(
	TEXT("asda.Fx.Light.ReconcileHz"),
	GAsdaFxLightReconcileHz,
	TEXT("[ПОРТ] Частота сверки наборов света эффектов в игровом мире, Гц (умолчание 10; 0 = каждый кадр, как до 29.10.2026). Смена уровня качества и редактор — всегда сразу."),
	ECVF_Default);

// ★★★ fix14 (С-4 ревизии fix13): ПОДПИСКА РЕДАКТОРА НА ПРАВКУ СВОЙСТВ — С КЛЮЧОМ.
//   1 (умолчание) — как было: правка полей эффекта в редакторе/PIE/`-game` редакторского бинаря
//   сразу пересобирает набор светов. 0 — подписка молчит, и набор держит одна ReconcileSets, то
//   есть ровно так, как в СОБРАННОМ клиенте, где всего этого блока нет (он под `#if WITH_EDITOR`).
//   Ключ нужен ради замера: иначе работу сверки на редакторском бинаре не увидеть никогда —
//   подписка перехватывает правку первой, и расхождения сверке просто не достаётся.
int32 GAsdaFxLightEditorPropWatch = 1;
static ASDA_DEV_CVAR_REF(CVarAsdaFxLightEditorPropWatch,
	TEXT("asda.Fx.Light.EditorPropWatch"),
	GAsdaFxLightEditorPropWatch,
	TEXT("1 (умолчание) = редакторская подписка на правку свойств эффекта пересобирает набор светов; 0 = подписка молчит (модель собранного клиента: набор держит только сверка)."),
	ECVF_Default);

int32 GAsdaFxLightTraceRegister = 0;
static ASDA_DEV_CVAR_REF(CVarAsdaFxLightTraceRegister,
	TEXT("asda.Fx.Light.TraceRegister"),
	GAsdaFxLightTraceRegister,
	TEXT("1 = печатать строку на каждое заведение набора светов эффекта (откуда числа, охват, сколько светов). Умолчание 0."),
	ECVF_Default);

// ★★★ fix15 (СРЕДН-2 ревизии fix14): НЕ СЧИТАТЬ РАСХОЖДЕНИЕМ ТО, ЧЕГО ЗАВЕДЕНИЕ НЕ МОЖЕТ ИСПРАВИТЬ.
//   1 (умолчание) — сверка МОЛЧА пропускает эффект, у которого набор короче заказа ТОЛЬКО потому,
//   что упёрся предохранитель `Предел кандидатов` (UAsdaFxLightSettings::MaxCandidates): лишних
//   светов у него нет, а завести недостающие RegisterEffect физически не может — место кончилось.
//   0 — прежнее поведение fix14: такой эффект объявляется расходящимся КАЖДЫЙ КАДР, и каждый кадр
//   по нему идёт заведение плюс полный отбор (SelectTimer = 1.e6f). Ключ нужен ровно затем, чтобы
//   петлю можно было воспроизвести на ТОМ ЖЕ бинаре и показать замером, а не рассуждением.
// ★★★ fix15 (СРЕДН-2 ревизии fix14): У ПРЕДОХРАНИТЕЛЯ «ПРЕДЕЛ КАНДИДАТОВ» ПОЯВИЛСЯ КЛЮЧ.
//   Он был ЕДИНСТВЕННЫМ ограничителем механизма без ключа: у бюджета количества, экранного бюджета,
//   кошелька на эффект и потолка радиуса ключ с «-1 = из настроек» есть с волны Ф2. Без ключа
//   поведение «мест больше нет» нельзя ни показать, ни отладить: на наших картах кандидатов два
//   десятка при пределе 2048, и упор не достигается никогда. -1 (умолчание) — как в настройках.
int32 GAsdaFxLightMaxCandidates = -1;
static FAutoConsoleVariableRef CVarAsdaFxLightMaxCandidates(
	TEXT("asda.Fx.Light.MaxCandidates"),
	GAsdaFxLightMaxCandidates,
	TEXT("Предел числа эффект-светов на карту: -1 (умолчание) = из настроек (UAsdaFxLightSettings::MaxCandidates, 2048), иначе это число. Понижение нужно, чтобы воспроизвести упор в предохранитель на карте, где кандидатов мало."),
	ECVF_Default);

// ═══ ★★★ ВОЛНА fix16 (20.09.2026): ВОЗВРАТ ПОГАСШЕГО ЭФФЕКТА В КАНДИДАТЫ ══════════════════════
//   СКОЛЬКО ПОГАСШИХ ЭФФЕКТОВ ПЕРЕПРОВЕРЯТЬ ЗА КАДР. Эффект, у которого не осталось ни одного
//   света, из Candidates вычеркнут, и сверка набора (она ходит по кандидатам) его не видит —
//   значит сам он не засветится уже никогда, даже когда причина гашения исчезнет. Лечение —
//   отдельный короткий список Extinguished и круговой обход по нему с ПОТОЛКОМ НА КАДР: полный
//   обход мира ради ответа «нет» стоил бы дороже всего механизма, а потолок делает цену ровной
//   и предсказуемой (за кадр ровно столько ResolveSpec, сколько здесь написано).
//   8 — потому что список на наших картах короткий (alpen: погасших 64), и при восьми проверках
//   за кадр очередь доходит до каждого за 8 кадров (★ fix17, МЕЛК-1: это оценка СНИЗУ — уборка
//   элемента переставляет хвостовой на пройденное место, поэтому верхняя оценка два круга, 16
//   кадров ≈ 0.27 с при 60 к/с; голодания нет, курсор монотонный). Для глаза это мгновенно.
//   0 = ОТКАТ к поведению до fix16 (погасший не возвращается никогда) — им и снимается пара A/B
//   на одном бинаре.
int32 GAsdaFxLightEmptyRecheck = 8;
static FAutoConsoleVariableRef CVarAsdaFxLightEmptyRecheck(
	TEXT("asda.Fx.Light.EmptyRecheck"),
	GAsdaFxLightEmptyRecheck,
	TEXT("Сколько эффектов с ПУСТЫМ набором светов перепроверять за кадр (круговой обход): 8 (умолчание) = погасший эффект возвращается в кандидаты сам, как только условие света снова выполнено; 0 = как до fix16 (не возвращается никогда)."),
	ECVF_Default);

int32 GAsdaFxLightCeilingSkip = 1;
static FAutoConsoleVariableRef CVarAsdaFxLightCeilingSkip(
	TEXT("asda.Fx.Light.CeilingSkip"),
	GAsdaFxLightCeilingSkip,
	TEXT("1 (умолчание) = сверка не считает расхождением набор, урезанный пределом кандидатов (исправить его заведение всё равно не может); 0 = как в fix14, то есть покадровая петля «сверка ↔ заведение»."),
	ECVF_Default);

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// Пересборка кандидатов: после правки таблицы семейств или поля «Свет включён» на живой сцене.
static FAutoConsoleCommand CmdAsdaFxLightRescan(
	TEXT("asda.Fx.Light.Rescan"),
	TEXT("Снести кандидатов эффект-светов и завести заново (после правки таблицы семейств или полей эффекта)."),
	FConsoleCommandDelegate::CreateStatic(&UAsdaFxLightSubsystem::RescanAll));
#endif // ASDA_DEV_CONSOLE

// ─────────────── жизненный цикл ───────────────
bool UAsdaFxLightSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer)) { return false; }
	const UWorld* W = Cast<UWorld>(Outer);
	if (!W) { return false; }
	// ★ Игровые миры (Game, PIE) И окно редактора (Editor): без последнего владелец не увидит свет,
	//   пока крутит поля, и настроить их руками физически невозможно.
	//   Превью-миры (EditorPreview, GamePreview) НЕ включаем намеренно: у панелей превью своя камера
	//   и свой свет, а гейт дальности этой подсистемы отсчитывается от вьюпорта УРОВНЯ — в превью-сцене
	//   он всегда «слишком далеко» и всё равно ничего не зажёг бы (тот же класс бага, что лечит
	//   AAsdaEffectActor::bUseViewOverride).
	//   ⚠ С 15.09.2026 (волна 3 п.11) точка наблюдателя — AsdaView: в превью-сцене она была бы своей, но превью-миры
	//   по-прежнему не включаем — у панелей превью свой свет.
	return W->WorldType == EWorldType::Game
		|| W->WorldType == EWorldType::PIE
		|| W->WorldType == EWorldType::Editor;
}

// ★ 27.10.2026 (владелец): ПОКАРТОВЫЙ УРОВЕНЬ ЭФФЕКТ-СВЕТОВ (UAsdaFxLightSettings::MapEffectsLevel). Решатели бюджета статические
//   (без мира), поэтому уровень карты держится здесь: его ставит подсистема последнего заведённого мира (Game/PIE/Editor —
//   в каждом процессе рендерится один), снимает — она же при выходе. −1 = у карты своего уровня нет.
static int32 GAsdaFxLightMapLevel = -1;
static TWeakObjectPtr<UWorld> GAsdaFxLightMapLevelWorld;

void UAsdaFxLightSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);   // ★ волна 3 п.12 (15.09.2026): там же подписка на смену уровня качества → OnQualityLevelsChanged (SelectTimer = 1e6)

	if (UWorld* W = GetWorld())
	{
		const FString Map = UWorld::RemovePIEPrefix(W->GetMapName());
		int32 Lvl = -1;
		for (const TPair<FString, int32>& P : UAsdaFxLightSettings::Get()->MapEffectsLevel)
		{
			if (P.Key.Equals(Map, ESearchCase::IgnoreCase)) { Lvl = FMath::Clamp(P.Value, 0, 4); break; }
		}
		GAsdaFxLightMapLevel = Lvl;
		GAsdaFxLightMapLevelWorld = W;
		if (Lvl >= 0)
		{
			UE_LOG(LogAsdaFxLight, Log, TEXT("[эффект-света] карта %s: покартовый уровень %d вместо sg.EffectsQuality"), *Map, Lvl);
		}
		MarkAllDirty();
	}

#if WITH_EDITOR
	// ★ РЕДАКТОРСКИЙ МИР ЖИВОЙ: владелец таскает эффекты на карту, удаляет их и правит поля «Свет эффекта»
	//   в панели деталей. Без этих трёх подписок свет появился бы только после перезагрузки карты.
	if (GIsEditor && GEngine && GetWorld() && GetWorld()->WorldType == EWorldType::Editor)
	{
		EditorActorAddedHandle = GEngine->OnLevelActorAdded().AddLambda([this](AActor* A)
		{
			if (A && A->GetWorld() == GetWorld()) { OnActorSpawned(A); }
		});
		EditorActorDeletedHandle = GEngine->OnLevelActorDeleted().AddLambda([this](AActor* A)
		{
			// Компонент живёт на самом акторе и умрёт вместе с ним; здесь достаточно попросить пересчёт,
			// чтобы освободившееся место в бюджете тут же ушло следующему кандидату.
			if (A && A->GetWorld() == GetWorld()) { SelectTimer = 1.e6f; }
		});
	}
	// ═══ ★★★ ВОЛНА fix13 (КРИТ-1 ревизии fix12): ПРАВКА ПОЛЕЙ ЛОВИТСЯ ВО ВСЕХ МИРАХ, А НЕ ТОЛЬКО
	//   В ОКНЕ РЕДАКТОРА. Подписка стояла ВНУТРИ блока выше, то есть только при
	//   WorldType == Editor. Но поля эффекта правят и в PIE (кнопка «Стенд в PIE» конфигуратора), и
	//   в игровом мире редакторского бинаря (наш стенд `asda.Fx.Light.Stand` + UE-Python
	//   `set_editor_property`) — там набор светов оставался от прежней спецификации до первого
	//   пересканирования. Два других обработчика (актор добавлен/удалён на карте) остаются
	//   редакторскими намеренно: OnLevelActor* в игре не срабатывают вовсе, спавн ловит OnActorSpawned.
	if (GetWorld())
	{
		EditorPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
			[this](UObject* Obj, FPropertyChangedEvent& /*Event*/)
		{
			// ЧИСЛА света компонент перечитывает каждый тик сам — пересборка нужна только ради ФАКТА
			// существования кандидата: «Свет включён» могли только что взвести у эффекта, у которого
			// компонента ещё нет, или снять у того, у кого он есть.
			// ★★★ fix14 (С-4 ревизии fix13): КЛЮЧ, КОТОРЫЙ ГЛУШИТ ЭТУ ПОДПИСКУ.
			//   Весь блок стоит под `#if WITH_EDITOR` — в СОБРАННОМ клиенте его нет вовсе, и там за
			//   набором светов следит ОДНА ReconcileSets. Проверить это прогоном было нельзя:
			//   любой прогон идёт на редакторском бинаре, подписка перехватывает правку первой, и
			//   сверка не успевает увидеть расхождение (отчёт fix13 так и записал — «не сработала
			//   ни разу»). Ключ `asda.Fx.Light.EditorPropWatch 0` выключает подписку и оставляет
			//   ровно то, что есть у игрока. Собранный клиент этим НЕ заменяется — это модель его
			//   поведения, и в отчёте она названа моделью.
			if (GAsdaFxLightEditorPropWatch == 0) { return; }
			AAsdaEffectActor* Fx = Cast<AAsdaEffectActor>(Obj);
			// Правка ассета BP_Fx_* правит CDO, а не размещённый актор — тогда пересобираем всё
			const bool bIsCdoOfEffect = Obj && Obj->HasAnyFlags(RF_ClassDefaultObject) && Obj->IsA<AAsdaEffectActor>();
			if (bIsCdoOfEffect) { bRescanRequested = true; return; }
			if (Fx && Fx->GetWorld() == GetWorld()) { RegisterEffect(Fx); SelectTimer = 1.e6f; }
		});
	}
#endif
}

void UAsdaFxLightSubsystem::Deinitialize()
{
	// ★ 27.10.2026: покартовый уровень снимает тот мир, что его поставил (выход с карты / смена уровня в редакторе)
	if (GAsdaFxLightMapLevelWorld.Get() == GetWorld())
	{
		GAsdaFxLightMapLevel = -1;
		GAsdaFxLightMapLevelWorld.Reset();
#if WITH_EDITOR
		// Конец PIE: окно уровня редактора живо — вернуть уровень ЕГО карты (иначе он пропал бы до перезагрузки карты).
		UWorld* EW = (GIsEditor && GEditor) ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (EW && EW != GetWorld())
		{
			const FString Map = UWorld::RemovePIEPrefix(EW->GetMapName());
			for (const TPair<FString, int32>& P : UAsdaFxLightSettings::Get()->MapEffectsLevel)
			{
				if (P.Key.Equals(Map, ESearchCase::IgnoreCase)) { GAsdaFxLightMapLevel = FMath::Clamp(P.Value, 0, 4); break; }
			}
			GAsdaFxLightMapLevelWorld = EW;
		}
#endif
		MarkAllDirty();
	}
	UnbindQualityHooks();   // ★ волна 3 п.12 (15.09.2026): снятие подписок базы — ПЕРВЫМ, как было (Super::Deinitialize повторит без эффекта)
#if WITH_EDITOR
	if (GEngine)
	{
		if (EditorActorAddedHandle.IsValid()) { GEngine->OnLevelActorAdded().Remove(EditorActorAddedHandle); EditorActorAddedHandle.Reset(); }
		if (EditorActorDeletedHandle.IsValid()) { GEngine->OnLevelActorDeleted().Remove(EditorActorDeletedHandle); EditorActorDeletedHandle.Reset(); }
	}
	if (EditorPropertyChangedHandle.IsValid()) { FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(EditorPropertyChangedHandle); EditorPropertyChangedHandle.Reset(); }
#endif
	Candidates.Reset();
	LeaseAge.Reset();
	OffAge.Reset();
	Super::Deinitialize();
}

void UAsdaFxLightSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Эффекты КАРТ расставлены в уровне и через OnActorSpawned не проходят — нужен полный обход
	ScanWorld();

	// Эффекты навыков/баффов спавнятся в рантайме
	BindActorSpawned(InWorld);   // ★ волна 3 п.12 (15.09.2026): подписка — в базе (вызов уходит в OnActorSpawned)

	UE_LOG(LogAsdaFxLight, Log, TEXT("[эффект-света] мир %s: кандидатов %d, бюджет количества %d, экранный %.1f, потолок радиуса %.0f (уровень эффектов %d)"),
		*InWorld.GetName(), Candidates.Num(), ResolveBudget(), ResolveScreenBudget(), ResolveRadiusCap(), CurrentEffectsLevel());
}

UAsdaFxLightSubsystem* UAsdaFxLightSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* W = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return W ? W->GetSubsystem<UAsdaFxLightSubsystem>() : nullptr;
}

bool UAsdaFxLightSubsystem::IsFeatureEnabled()
{
	return GAsdaFxLight != 0 && UAsdaFxLightSettings::Get()->bEnabled;
}

bool UAsdaFxLightSubsystem::IsEditorPreviewEnabled()
{
	return GAsdaFxLightEditor != 0 && UAsdaFxLightSettings::Get()->bShowInEditor;
}

float UAsdaFxLightSubsystem::GlobalIntensityScale()
{
	return FMath::Max(GAsdaFxLightScale, 0.f);
}

float UAsdaFxLightSubsystem::GlobalRadiusScale()
{
	return FMath::Max(GAsdaFxLightRadiusScale, 0.01f);
}

// ★ «форма-2», п. 1: удержание габарита — ключ важнее поля (ключ ставят только на замер)
float UAsdaFxLightSubsystem::ResolveGeomHoldSec(float FromSpec)
{
	return GAsdaFxLightGeomHoldSec >= 0.f ? GAsdaFxLightGeomHoldSec : FMath::Max(FromSpec, 0.f);
}

float UAsdaFxLightSubsystem::DarkRepickSec() { return FMath::Max(GAsdaFxLightDarkRepickSec, 0.f); }

// ★ Ревизия «форма-2» (СРЕДН-6): потолок радиуса задан КЛЮЧОМ, а не уровнем? Развилка ровно та же,
//   что в ResolveRadiusCap, — колонка «срез» обязана называть настоящий источник среза.
bool UAsdaFxLightSubsystem::RadiusCapFromKey() { return GAsdaFxLightRadiusCap >= 1.f || GAsdaFxLightRadiusCap <= -2.f; }

bool  UAsdaFxLightSubsystem::IsFrustumCull() { return GAsdaFxLightCull != 0; }
float UAsdaFxLightSubsystem::FadeInSec()     { return FMath::Max(GAsdaFxLightFadeIn, 0.f); }
float UAsdaFxLightSubsystem::FadeOutSec()    { return FMath::Max(GAsdaFxLightFadeOut, 0.f); }
float UAsdaFxLightSubsystem::GraceSec()      { return FMath::Max(GAsdaFxLightGrace, 0.f); }

int32 UAsdaFxLightSubsystem::CurrentEffectsLevel()
{
	// Индекс — sg.EffectsQuality (а не ShadowQuality, как у бюджета теней): эффект-света это ЭФФЕКТЫ,
	// и игрок, снизивший в Ctrl+N только «Эффекты», должен получить и меньше эффект-светов.
	// ★ 27.10.2026 (владелец): у карты свой уровень (MapEffectsLevel) — он главнее и в игре, и в редакторе.
	if (GAsdaFxLightMapLevel >= 0) { return GAsdaFxLightMapLevel; }
#if WITH_EDITOR
	// ★ 27.10.2026 (владелец): в редакторе (окно уровня и PIE) — уровень игрока, чтобы видно было то, что увидит игра.
	if (GIsEditor)
	{
		const int32 EditorLvl = UAsdaFxLightSettings::Get()->EditorEffectsLevel;
		if (EditorLvl >= 0) { return FMath::Clamp(EditorLvl, 0, 4); }
	}
#endif
	return EffectsQualityLevel();   // ★ волна 3 п.12 (15.09.2026): чтение уровня — в базе
}

int32 UAsdaFxLightSubsystem::LevelBudget(int32 Level)
{
	// [AsdaFxLight@N] Light.FxBudget из объединённого Scalability-ini (BaseScalability + Config/DefaultScalability.ini);
	// хелпер тот же, что у меню и у бюджета теней — один способ чтения уровневых значений.
	return LevelInt(TEXT("AsdaFxLight"), Level, TEXT("Light.FxBudget"), UAsdaFxLightSettings::Get()->BudgetFallback);   // ★ волна 3 п.12 (15.09.2026)
}

float UAsdaFxLightSubsystem::LevelScreenBudget(int32 Level)
{
	return LevelFloat(TEXT("AsdaFxLight"), Level, TEXT("Light.FxScreenBudget"), UAsdaFxLightSettings::Get()->ScreenBudgetFallback);   // ★ волна 3 п.12 (15.09.2026)
}

float UAsdaFxLightSubsystem::LevelRadiusCap(int32 Level)
{
	return LevelFloat(TEXT("AsdaFxLight"), Level, TEXT("Light.FxRadiusCap"), UAsdaFxLightSettings::Get()->RadiusCapFallback);   // ★ волна 3 п.12 (15.09.2026)
}

int32 UAsdaFxLightSubsystem::LevelPerEffect(int32 Level)
{
	// [AsdaFxLight@N] Light.FxPerEffect. Запасное значение (секции нет) — 1: это ровно поведение
	// волн Ф2…Ф7, то есть «не хуже, чем было», а не «сколько влезет».
	return LevelInt(TEXT("AsdaFxLight"), Level, TEXT("Light.FxPerEffect"), 1);   // ★ волна 3 п.12 (15.09.2026)
}

int32 UAsdaFxLightSubsystem::ResolvePerEffect()
{
	if (GAsdaFxLightPerEffect >= 1)  { return GAsdaFxLightPerEffect; }
	if (GAsdaFxLightPerEffect <= -2) { return MAX_int32; }
	return FMath::Max(1, LevelPerEffect(CurrentEffectsLevel()));
}

// ★ fix15 (СРЕДН-2): действующий предел кандидатов — ключ важнее настройки, как у прочих ограничителей
int32 UAsdaFxLightSubsystem::ResolveMaxCandidates()
{
	if (GAsdaFxLightMaxCandidates >= 1) { return GAsdaFxLightMaxCandidates; }
	return FMath::Max(1, UAsdaFxLightSettings::Get()->MaxCandidates);
}

int32 UAsdaFxLightSubsystem::ResolveBudget()
{
	if (GAsdaFxLightBudget >= 0)  { return GAsdaFxLightBudget; }
	if (GAsdaFxLightBudget <= -2) { return MAX_int32; }
	return FMath::Max(0, LevelBudget(CurrentEffectsLevel()));
}

float UAsdaFxLightSubsystem::ResolveScreenBudget()
{
	if (GAsdaFxLightScreenBudget >= 0.f)  { return GAsdaFxLightScreenBudget; }
	if (GAsdaFxLightScreenBudget <= -2.f) { return TNumericLimits<float>::Max(); }
	return FMath::Max(0.f, LevelScreenBudget(CurrentEffectsLevel()));
}

float UAsdaFxLightSubsystem::ResolveRadiusCap()
{
	if (GAsdaFxLightRadiusCap >= 1.f)  { return GAsdaFxLightRadiusCap; }
	if (GAsdaFxLightRadiusCap <= -2.f) { return TNumericLimits<float>::Max(); }
	return FMath::Max(1.f, LevelRadiusCap(CurrentEffectsLevel()));
}

void UAsdaFxLightSubsystem::MarkAllDirty()
{
	ForEachInstance<UAsdaFxLightSubsystem>([](UAsdaFxLightSubsystem& S) { S.SelectTimer = 1.e6f; });   // ★ волна 3 п.12 (15.09.2026)
}

void UAsdaFxLightSubsystem::RescanAll()
{
	ForEachInstance<UAsdaFxLightSubsystem>([](UAsdaFxLightSubsystem& S) { S.Rescan(); });   // ★ волна 3 п.12 (15.09.2026)
}

// ═══ ★★★ 13.09.2026: КОНФИГУРАТОР СВЕЧЕНИЯ — КЛЮЧ ОТКАТА И СМЕНА ТАБЛИЦ НА ЖИВОЙ СЦЕНЕ ═══════════════
//   asda.Fx.Glow.Overrides 1 (умолчание) — действуют переопределения «предмет / эффект» из
//   UAsdaFxLightSettings::Overrides и сила свечения семейств (bOverrideEmissive в дефолтах семейства).
//   0 — оба механизма спят: свет разрешается как до конфигуратора (замок ассета → семейство → поля),
//   эмиссия — только поле ассета (Ф5) либо материал из таблицы asda_fx_boost.json.
static int32 GAsdaFxGlowOverrides = 1;
static void OnAsdaFxGlowOverridesChanged(IConsoleVariable*) { UAsdaFxLightSubsystem::NotifyGlowTablesChanged(); }
static ASDA_DEV_CVAR_REF(CVarAsdaFxGlowOverrides,
	TEXT("asda.Fx.Glow.Overrides"),
	GAsdaFxGlowOverrides,
	TEXT("Конфигуратор свечения: 1 (умолчание) = действуют переопределения предмета/эффекта (Overrides) и сила свечения семейств; 0 = откат, всё как до конфигуратора."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaFxGlowOverridesChanged),
	ECVF_Default);

bool UAsdaFxLightSubsystem::GlowOverridesEnabled()
{
	return GAsdaFxGlowOverrides != 0;
}

void UAsdaFxLightSubsystem::NotifyGlowTablesChanged()
{
	UAsdaFxLightSettings::BumpTablesRevision();
	if (!GEngine) { return; }
	for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
	{
		UWorld* W = Ctx.World();
		if (!W) { continue; }
		// Набор светов эффекта зависит от ДЕЙСТВУЮЩИХ чисел («свет включён», «свой у каждого эмиттера») —
		// после смены таблиц его надо привести заново; числа компонент перечитывает каждый тик сам
		if (UAsdaFxLightSubsystem* S = W->GetSubsystem<UAsdaFxLightSubsystem>()) { S->Rescan(); }
		// Эмиссия живёт в MID на секциях — пересборка секции её донесёт, но ждать её нельзя (статичный эффект
		// может не пересобираться вовсе). Хранитель не нужен: материалы секций в уровень не сериализуются
		for (TActorIterator<AAsdaEffectActor> It(W); It; ++It)
		{
			if (IsValid(*It)) { It->RefreshGlowMaterials(); }
		}
	}
}

#if WITH_EDITOR
// Перечитать таблицы из Config/DefaultGame.ini и применить на живой сцене (то же делает конфигуратор после «Сохранить»)
static FAutoConsoleCommand CmdAsdaFxGlowReload(
	TEXT("asda.Fx.Glow.Reload"),
	TEXT("Перечитать семейства и переопределения свечения из Config/DefaultGame.ini (после Tools/asda_gen_fx_light_ini.py) и применить во всех мирах."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FString Err;
		if (GetMutableDefault<UAsdaFxLightSettings>()->ReloadGlowTablesFromDefaultIni(Err))
		{
			UAsdaFxLightSubsystem::NotifyGlowTablesChanged();
		}
		else
		{
			UE_LOG(LogAsdaFxLight, Warning, TEXT("[свечение] перечитать таблицы не удалось: %s"), *Err);
		}
	}));
#endif
