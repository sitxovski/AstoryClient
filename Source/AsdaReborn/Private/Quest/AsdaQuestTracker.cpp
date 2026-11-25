// Copyright (c) 2026 @sitxovski (Discord)
// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ЭКРАННЫЙ ТРЕКЕР ЗАДАНИЙ — окно 1800 `Quest_State_Win`
//  Реализация методов `UAsdaQuestMarkerSubsystem`, объявленных в его заголовке. Отдельная
//  подсистема НЕ заводится: состояние трекера в оригинале живёт в том же `CQuestManager`, что и
//  журнал (см. шапку объявления). Полный реверс — `Reports/quest_tracker.md`.
//
//  ЧТО ГДЕ В ЭТАЛОНЕ:
//    `CQuest_State_BuildUI@0xC580E0`              → EnsureTrackerWidget
//    `CQuest_State_Update@0xC58340`               → UpdateTrackerWindow (блок проявления)
//    `CQuestTracker_BuildWaypointStrings@0xC58A10`→ UpdateTrackerWindow (строки + стрелки)
//    `CQuest_Episode_DrawWaypoints@0xC59660`      → UpdateTrackerWindow (кисть и поворот стрелки)
//    `questTracker_UpdateEntry@0xC58820`          → TrackerUpdateEntry
//    `questTracker_RemoveEntry@0xC58620`          → TrackerRemoveEntry
//    `questTracker_Clear@0xC585B0`                → TrackerClear
//    `questTracker_Hide@0xC58500` (режим 1)       → TrackerHideWindow
//    `pathGetNextStepCoords@0xFBE6A0`             → NextPortalStep
//    `pathGraph_BuildNavMesh@0xFBBE90`            → kPortalGraph (в оригинале 32 ребра; у нас 41 — из серверных зон)
// ════════════════════════════════════════════════════════════════════════════════════════════════
#include "Quest/AsdaQuestMarkerSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Login/AsdaText.h"                 // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): FAsdaText::LocRaw — формат заголовка с расстоянием
#include "Text/AsdaTextSubsystem.h"         // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): FormatPrintf — %s/%d в строке таблицы
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "World/AsdaWalkGrid.h"        // проходимость клеток зоны перехода

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"          // ★ ключ отката лимита: CVar `Asda.Quest.TrackerLimit`

#include "AsdaDataTableCache.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "UI/AsdaMarkup.h"                    // ★ UAsdaRichText::ApplyFormatted — ЕДИНАЯ точка тегов <FC=…>
#include "UI/AsdaUiHelpers.h"                 // ★ CameraConeYaw — ОБЩАЯ точка угла камеры
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaCoord.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaMapRegistry.h"   // ★ волна 2 (14.09.2026): реестр карт — генерируемый заголовок из Tools/data/asda_map_registry.json (kPortalGraph)
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaNameplateComponent.h"
#include "World/AsdaEntityText.h"             // имена мобов/NPC — единственный источник в порту (волна 3 п.14)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaQuestTrack, Log, All);

// ★ Часть удержания таблиц квестов (15.09.2026, dt_gc_hold): путь `DT_QuestGuide` (`kGuideDT`) объявлен в этом файле ниже,
//   поэтому тело — в конце файла (`UAsdaQuestMarkerSubsystem::HoldTrackerTables`).

namespace
{
	// ── Окно ────────────────────────────────────────────────────────────────────────────────────
	const TCHAR* kTrackWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/HUD/WBP_Quest_State_Win.WBP_Quest_State_Win_C");
	const FName  kTrackWinId(TEXT("Quest_State_Win"));
	// Ниже мини-карты и мини-статуса: окно фоновое, кликов почти не ловит.
	constexpr int32 kTrackZ = 11;

	const TCHAR* kGuideDT = TEXT("/Game/AsdaAssets/Data/Quest/DT_QuestGuide.DT_QuestGuide");

	// Текстуры стрелки. Имена — из `CMiniMap_Init@0xB4BDC0` (`world_map.c:2202-2205`), где они
	//   грузятся в поля `CMiniMap+0x0C` / `+0x10`; ровно эти поля читает `DrawWaypoints`.
	const TCHAR* kMapTexDir  = AsdaAssetPaths::kMapTexPrefix;   // ★ волна 2 п. 7: корень — реестр путей
	const TCHAR* kArrowFar   = TEXT("QuestDir");          // цель на ДРУГОЙ карте (ветка a4 == 0)
	const TCHAR* kArrowSame  = TEXT("QuestDirAnother");   // цель на ТЕКУЩЕЙ карте (ветка a4 == 1)

	// ── Раскладка (эталон, §1.5 отчёта) ──
	constexpr float kLineH        = 15.f;   // высота строки
	constexpr float kRowGap       = 5.f;    // `5 * slotIndex` — зазор между записями
	constexpr float kFirstLineTop = 18.f;   // `+ 18`
	constexpr float kWinExtraH    = 45.f;   // высота окна = 15*строк + 45
	constexpr float kArrowSize    = 20.f;   // квад 20×20
	constexpr float kArrowCenterX = 11.f;   // левый край окна + (titleLocalX 5 + 6)
	constexpr float kArrowCenterY = 14.f;   // `panelOriginY + titleLocalY + 14`
	constexpr float kWinW         = 222.f;  // ширина окна из JUI

	// ── ★★★ ГРАНИЦЫ ЭКРАНА, В КОТОРЫЕ ОБЯЗАН ВПИСАТЬСЯ СПИСОК ИЗ 12 ЗАПИСЕЙ ──────────────────────
	//   ЗАЧЕМ. Эталонное окно росло ВНИЗ от `Origin.y = 382` и упиралось максимум в 6 записей.
	//   С двенадцатью худший случай (12 записей по 3 строки условий) даёт `15*48 + 45 = 765` px —
	//   больше, чем вся высота базового экрана 768. Поэтому к эталонной формуле добавлено
	//   ОСОЗНАННОЕ ОТСТУПЛЕНИЕ: список сначала ПОДНИМАЕТСЯ вверх по свободной правой колонке, а
	//   если и этого мало — весь блок УЖИМАЕТСЯ рендер-масштабом (текст мельче, но НИЧЕГО не
	//   теряется и ничего не уезжает за кадр). Разбор — `Reports/quest_tracker_limit12.md`.
	//
	//   ЧИСЛА НЕ ПОДОБРАНЫ, а сняты тем же парсером .JUI, которым импортируются окна
	//   (`Tools/asda_jui_parse.compute_anchors`, база 1024×768, DPI проекта = 1.0 ⇒ юнит = пиксель):
	//     · `MINIMAP_WIN`   (1702): анкор ВЕРХ-право, прямоугольник y 34…168 → ниже 168 свободно;
	//     · `QUICK_NEW_WIN` (73000): анкор НИЗ-право, верх = H − 60          → выше H−60 свободно;
	//     · `CHAT_NEW`      (2500): анкор низ-ЛЕВО, x 1…475 — с колонкой трекера (x = W−222)
	//       не пересекается ни на одном разумном разрешении (пересеклись бы при W < 697).
	constexpr float kMiniMapBottom  = 168.f;   // низ мини-карты: выше него не поднимаемся
	constexpr float kHotbarTopFromH = 60.f;    // верх хотбара = H − 60: ниже него не опускаемся
	constexpr float kBandMargin     = 6.f;     // зазор до соседей, чтобы не «слипалось»
	constexpr float kMinFitScale    = 0.55f;   // предел ужимания (ниже текст уже нечитаем)

	// ── Проявление по наведению (`CQuest_State_Update`) ──
	constexpr float kSlideMax  = 60.f;      // потолок альфы фона (из 255)
	constexpr float kSlideStep = 0.1f;      // ±0.1 ЗА КАДР — эталон не нормирует на время
	// Эталон крутит это в кадровом цикле при ~60 fps. Чтобы при другом fps скорость проявления
	//   совпадала, шаг умножается на «сколько эталонных кадров прошло».
	constexpr float kRefFps = 60.f;

	// ── Дистанция ──
	constexpr float kUnitsPerMeter = AsdaCoord::kTileUnits;   // 100 юнитов = «метр» подписи
	constexpr float kAlphaK        = 0.5714286f;              // 0x13E3F50
	constexpr float kAlphaMax      = 255.f;                   // 0x13998B8
	constexpr float kAlphaMin      = 55.f;
	// Ветка «цель на ДРУГОЙ карте»: эталон рисует фиксированный diffuse `0x38FFFFFF` ⇒ альфа 56,
	//   от расстояния не зависит вовсе.
	constexpr float kAlphaFarMap   = 56.f;
	// Границы цвета (§3.4). ★ FIX-ORIG D7: у эталона обе границы строгие с обеих сторон, поэтому
	//   ровно 50 м и ровно 100 м не попадали ни в одну ветку и цвет прыгал на белый. Пишем
	//   полуинтервалы.
	constexpr float kNearM = 50.f;
	constexpr float kMidM  = 100.f;

	// Цвета числа дистанции. ★ FIX-ORIG D1: в эталоне средний цвет записан ДЕВЯТЬЮ hex-цифрами
	//   (`FFFFF2000`), парсер тега читает мусор. Правильное значение — `FFFFF200`.
	const TCHAR* kColNear = TEXT("FFF26C4F");
	const TCHAR* kColMid  = TEXT("FFFFF200");
	const TCHAR* kColFar  = TEXT("FFFFFFFF");

	// ══ ★★★ ГРАФ МЕЖКАРТОВЫХ ПЕРЕХОДОВ — у оригинала 32 ребра в коде; у нас 41 из серверных зон ══════════════════
	//   `pathGraph_BuildNavMesh@0xFBBE90` (`file_vfs.c:1690`) заполняет список вызовами
	//   `stl_containerEmplace(this, fromMap, toMap, cellX, cellY, 1)`. Файла данных у этого списка
	//   НЕТ — это код, поэтому и у нас он таблицей-константой, а не таблицей ассетов.
	//   Координаты — в КЛЕТКАХ карты, тех же, что `X`/`Y` в `DT_QuestGuide`.
	// ★★★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА (24.08.2026). Прежде таблица бралась 1:1 из клиента
	//   (`pathGraph_BuildNavMesh@0xFBBE90`, 32 ребра, прошиты в коде). Живая проверка показала,
	//   что она РАСХОДИТСЯ с настоящими зонами переходов, которые держит сервер, и персонаж
	//   вставал в шаге от перехода. Теперь рёбра собраны из САМИХ серверных зон
	//   (`GameServer\PortalIP_Info\*_PortalConfig.Ini`, 39 зон на 20 картах, выгрузка
	//   `Reports/map_triggers_dump.tsv`); попутно ушли фантомное ребро клиента и восемь
	//   недостающих переходов.
	//
	//   ★★ ХРАНИМ ПРЯМОУГОЛЬНИК ЗОНЫ, А НЕ ТОЧКУ. Второй заход живой проверки: персонаж упёрся
	//   «в стойку рамки портала». Оказалось, что у Alpen → Rein зона 463…465 / 345…349 почти вся
	//   НЕПРОХОДИМА по данным карты — свободна только колонка 463, а центр (464,347) в блоке.
	//   Целясь в центр, поход утыкался в стену и вставал. Поэтому цель выбирается на месте:
	//   ближайшая ПРОХОДИМАЯ клетка ВНУТРИ зоны (см. `NextPortalStep`).
	// ★★★ 14.09.2026 (реестр карт, волна 2): таблица переехала в ЕДИНЫЙ ИСТОЧНИК `Tools/data/asda_map_registry.json`
	//   → генерируемый `World/AsdaMapRegistry.h` (`AsdaMapRegistry::kPortalGraph`); сверка с живым сервером —
	//   `python Tools/asda_map_registry.py --check`. При переносе сверка с `PortalIP_Info` вскрыла ДЕФЕКТ ПОРТА:
	//   у турнирных карт были перепутаны номера карты-источника (зоны `T_Castle_of_Chess` стояли с From=10,
	//   `T_Palace_of_Queen` — с From=11, а по MapInfo.txt 10 = t_Palace_of_Queen, 11 = t_Castle_of_chess), и не было
	//   двух зон входа в турнирные карты, добавленных на сервер 25.08 (Conquester → 10, SunnyCoast → 11).
	using AsdaMapRegistry::FPortalEdge;
	using AsdaMapRegistry::kPortalGraph;



	// Угол вектора в игровой плоскости, отсчитанный от оси +Z по направлению +X, в градусах 0…360.
	//   Порт `0xC57CB0`: `acos(v.z / sqrt(v.x² + v.z²))`, и `360 − a` при `v.x < 0`.
	//   (Нормализация вектора, которую делает эталон, на результат не влияет — она сокращается.)
	float AngleFromGameZ(const FVector2D& GameXZ)
	{
		const float Len = FMath::Sqrt(GameXZ.X * GameXZ.X + GameXZ.Y * GameXZ.Y);
		if (Len <= KINDA_SMALL_NUMBER) { return 0.f; }
		const float A = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(GameXZ.Y / Len, -1.f, 1.f)));
		return (GameXZ.X < 0.f) ? (360.f - A) : A;
	}

	// ★★★ УГОЛ СТРЕЛКИ-КОМПАСА относительно взгляда камеры. ОДНА формула на весь трекер.
	//
	//   ★★★ ПЕРЕДЕЛАНО 2026-08-25 (владелец: «стрелки неправильно крутятся» — второй раз).
	//   Прошлая формула выводилась умозрительно: направление на цель считалось в ИГРОВЫХ осях,
	//   курс камеры приходил в осях движка, а связь между ними подбиралась по четырём румбам —
	//   и знак оказался противоположным, отчего стрелка зеркалила при повороте камеры.
	//   Больше не выводим: берём ДВЕ уже проверенные живой игрой точки.
	//     • `AsdaCoord::FaceYawFromGameDir` — ею персонаж разворачивается к цели, то есть она
	//       заведомо верно переводит игровое направление в курс движка;
	//     • конус обзора на мини-карте крутится ровно на `CameraConeYaw` без всякой поправки —
	//       значит «курс движка → угол виджета» здесь тождественно.
	//   Отсюда угол стрелки относительно взгляда = курс на цель − курс камеры, обе величины уже
	//   в одной системе.
	float ArrowAngleVsCamera(const FVector2D& ToTargetGame, float CamYawUE)
	{
		return AsdaCoord::FaceYawFromGameDir(ToTargetGame) - CamYawUE;
	}

	// ★ ЕДИНСТВЕННАЯ формула альфы стрелки-компаса: её зовёт и отрисовка, и диагностика
	//   (`Asda.Quest.Tracker` печатает то же число, что видит игрок, а не пересчитанное второй раз).
	//   Эталон: своя карта — `clamp(255 − 0.5714286·d, 55, 255)`, чужая — фиксированные 56.
	//   Подъём (отступление № 4, заявка владельца «на 10 % виднее») прибавляется к обеим ветвям,
	//   к дну кривой и к потолку не выходит: `Asda.Quest.TrackerArrowAlphaBoost` в процентах шкалы.
	float ArrowAlpha255(bool bSameMap, float DistM)
	{
		const float Boost = 0.01f * UAsdaQuestMarkerSubsystem::TrackerArrowAlphaBoost() * 255.f;
		return bSameMap
			? FMath::Clamp(kAlphaMax + Boost - kAlphaK * DistM, kAlphaMin + Boost, kAlphaMax)
			: FMath::Min(kAlphaFarMap + Boost, kAlphaMax);
	}
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ★★★ ЛИМИТ ЗАПИСЕЙ — ЕДИНСТВЕННАЯ ТОЧКА (объявление и «зачем» — в заголовке подсистемы)
// ════════════════════════════════════════════════════════════════════════════════════════════════
//  Раньше число «6» было выписано ДВАЖДЫ: `kTrackerMaxEntries` здесь и `kMaxTracked` в
//  `AsdaQuestLogSubsystem.cpp`. Два независимых объявления одного правила — заготовка расхождения
//  (журнал пустил бы седьмую галку, а трекер молча её не показал бы). Точка теперь одна, и её
//  спрашивают ОБА потребителя.
static ASDA_DEV_CVAR(int32, CVarTrackerLimit,
	TEXT("Asda.Quest.TrackerLimit"),
	UAsdaQuestMarkerSubsystem::kTrackerDefaultMaxEntries,
	TEXT("Сколько заданий можно отслеживать в экранном трекере (окно 1800).\n")
	TEXT("  12 — значение порта (по умолчанию): записи 7…12 клонируются с первой.\n")
	TEXT("   6 — ЭТАЛОННОЕ значение оригинала (ключ отката, `questTracker_UpdateEntry@0xC58820`).\n")
	TEXT("Зажимается в 1…12: контролов под большее число записей окно не порождает."),
	ECVF_Default);

int32 UAsdaQuestMarkerSubsystem::TrackerLimit()
{
	return FMath::Clamp(CVarTrackerLimit.GetValueOnAnyThread(), 1, kTrackerHardMaxEntries);
}

// ★ Ключ отката отступления № 3 (задания без требований по предметам) — см. заголовок.
static ASDA_DEV_CVAR(int32, CVarTrackerNoItemQuests,
	TEXT("Asda.Quest.TrackerNoItemQuests"),
	1,
	TEXT("Брать ли в трекер задания БЕЗ требований по предметам (разговорные).\n")
	TEXT("  1 — значение порта (по умолчанию): берём, если у задания есть цель с координатами\n")
	TEXT("      в QDT — показываем заголовок, расстояние и стрелку, строк условий нет.\n")
	TEXT("  0 — ЭТАЛОН: `QuestReward_UpdateProgressList@0xC65AA0` выходит нулём и галка 743\n")
	TEXT("      гаснет молча (так ведёт себя оригинал у 195 книг заданий из 442)."),
	ECVF_Default);

bool UAsdaQuestMarkerSubsystem::TrackerAllowNoItemQuests()
{
	return CVarTrackerNoItemQuests.GetValueOnAnyThread() != 0;
}

// ★ Ключ отката отступления № 4 (видимость стрелки на дистанции) — см. заголовок.
static ASDA_DEV_CVAR(float, CVarTrackerArrowAlphaBoost,
	TEXT("Asda.Quest.TrackerArrowAlphaBoost"),
	10.f,
	TEXT("Подъём альфы стрелки-компаса, в ПРОЦЕНТАХ ПОЛНОЙ ШКАЛЫ (0…50).\n")
	TEXT("  10 — значение порта (по умолчанию): +0.10*255 = +25.5 к кривой, к её дну и к\n")
	TEXT("       фиксированной альфе «цель на другой карте» (55→80.5, 56→81.5).\n")
	TEXT("   0 — ЭТАЛОН: clamp(255 - 0.5714286*d, 55, 255) и фикс. 56 у чужой карты."),
	ECVF_Default);

float UAsdaQuestMarkerSubsystem::TrackerArrowAlphaBoost()
{
	return FMath::Clamp(CVarTrackerArrowAlphaBoost.GetValueOnAnyThread(), 0.f, 50.f);
}

// ★ Ключ отката отступления № 5 (недоступная галка 743) — см. заголовок.
static ASDA_DEV_CVAR(int32, CVarTrackerLockUselessCheckbox,
	TEXT("Asda.Quest.TrackerLockUselessCheckbox"),
	1,
	TEXT("Что делать с галкой «Показывать информацию» у задания, которое отслеживать нечем.\n")
	TEXT("  1 — значение порта (по умолчанию): галка НЕДОСТУПНА сразу — не реагирует\n")
	TEXT("      (эталонный латч +609) и приглушена вместе с подписью 744.\n")
	TEXT("  0 — ЭТАЛОН: галка ставится и тут же гаснет, молча и без объяснения."),
	ECVF_Default);

bool UAsdaQuestMarkerSubsystem::TrackerLockUselessCheckbox()
{
	return CVarTrackerLockUselessCheckbox.GetValueOnAnyThread() != 0;
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ЗАПИСИ ТРЕКЕРА
// ════════════════════════════════════════════════════════════════════════════════════════════════

// Порт `questTracker_UpdateEntry@0xC58820`.
bool UAsdaQuestMarkerSubsystem::TrackerUpdateEntry(int32 QuestId, int32 QuestNo, const FString& Title,
                                                   const FString& Line, int32 LineIdx)
{
	if (QuestId <= 0 || LineIdx < 0 || LineIdx >= kTrackerMaxLines) { return false; }

	int32 Slot = INDEX_NONE;
	for (int32 i = 0; i < TrackEntries.Num(); ++i)
	{
		if (TrackEntries[i].QuestId == QuestId) { Slot = i; break; }
	}
	const bool bFound = (Slot != INDEX_NONE);
	if (!bFound)
	{
		// Эталон: `if (activeSlotCount >= 6) return;` — молча, без нотиса. Нотис про лимит
		//   выдаёт ЖУРНАЛ при клике по чекбоксу 743 (`UAsdaQuestLogSubsystem::SetTrackSelected`).
		//   ★ Само ЧИСЛО берётся из ЕДИНОЙ точки `TrackerLimit()` (по умолчанию 12; эталонные 6
		//   возвращает `Asda.Quest.TrackerLimit 6`) — второго объявления лимита в порту нет.
		if (TrackEntries.Num() >= TrackerLimit()) { return false; }
		Slot = TrackEntries.AddDefaulted();
	}
	const bool bAdded = !bFound;   // состав отслеживаемых сменился — значки целей карты мира пересчитать (ниже)

	FAsdaQuestTrackEntry& E = TrackEntries[Slot];
	E.QuestId   = QuestId;
	E.QuestNo   = QuestNo;
	E.Title     = Title;
	E.LineCount = LineIdx + 1;              // эталон: `entry.lineCount = lineIdx + 1`
	if (E.Cond.Num() != kTrackerMaxLines) { E.Cond.SetNum(kTrackerMaxLines); }
	E.Cond[LineIdx] = Line;

	// Эталон меняет местами ОБНОВЛЁННУЮ запись и нулевую (`swap(entry[0], entry[slot])`) —
	//   из-за чего последнее изменившееся задание всплывает наверх списка. Это поведение
	//   оригинала, а не дефект: воспроизводим.
	if (bFound && Slot != 0)
	{
		TrackEntries.Swap(0, Slot);
	}
	if (bAdded)
	{
		TrackedDoneQuests.Remove(QuestNo);   // снова отслеживается обычным образом
		RefreshMapMarksForTracker();
	}
	return true;
}

// ★ 31.10.2026 — разбор у объявления: авто-снятие выполненного задания (журнал, эталон `QuestReward_UpdateProgressList` →
//   `questTracker_RemoveEntry`) + пометка, чтобы точка карты мира переключилась на NPC сдачи.
bool UAsdaQuestMarkerSubsystem::TrackerRemoveEntryDone(int32 QuestNo)
{
	if (!IsTrackedQuest(QuestNo)) { return false; }
	TrackedDoneQuests.Add(QuestNo);
	return TrackerRemoveEntry(QuestNo);   // снимает запись и пересчитывает метки карты
}

// ★ 31.10.2026: значки целей на карте мира показываются только у отслеживаемых заданий — при смене состава трекера
//   метки карты пересчитываются сразу (открытая карта обновляется без переоткрытия), а не через секунду периодики.
void UAsdaQuestMarkerSubsystem::RefreshMapMarksForTracker()
{
	const UAsdaLoadManagerSubsystem* LM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	PushMapMarks(LM ? LM->CurrentMapId : ShownMapId);
}

// Порт `questTracker_RemoveEntry@0xC58620` (сдвиг хвоста влево = обычное удаление с сохранением
//   порядка) плюс уменьшение счётчика записей.
bool UAsdaQuestMarkerSubsystem::TrackerRemoveEntry(int32 QuestId)
{
	for (int32 i = 0; i < TrackEntries.Num(); ++i)
	{
		if (TrackEntries[i].QuestId == QuestId)
		{
			TrackEntries.RemoveAt(i);
			UE_LOG(LogAsdaQuestTrack, Log, TEXT("[трекер] задание %d снято с отслеживания (осталось %d)"),
				QuestId, TrackEntries.Num());
			RefreshMapMarksForTracker();
			return true;
		}
	}
	return false;
}

void UAsdaQuestMarkerSubsystem::TrackerClear()
{
	if (TrackEntries.Num() > 0)
	{
		UE_LOG(LogAsdaQuestTrack, Log, TEXT("[трекер] очистка: снято записей %d"), TrackEntries.Num());
	}
	const bool bHad = TrackEntries.Num() > 0 || TrackedDoneQuests.Num() > 0;
	TrackEntries.Reset();
	TrackedDoneQuests.Reset();   // другой набор отслеживаемых (смена персонажа, восстановление списка) — с чистого листа
	if (bHad) { RefreshMapMarksForTracker(); }
}

void UAsdaQuestMarkerSubsystem::TrackerSetLineCount(int32 QuestId, int32 LineCount)
{
	for (FAsdaQuestTrackEntry& E : TrackEntries)
	{
		if (E.QuestId == QuestId) { E.LineCount = FMath::Clamp(LineCount, 0, kTrackerMaxLines); return; }
	}
}

bool UAsdaQuestMarkerSubsystem::IsTrackedQuest(int32 QuestId) const
{
	for (const FAsdaQuestTrackEntry& E : TrackEntries)
	{
		if (E.QuestId == QuestId) { return true; }
	}
	return false;
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ЦЕЛЬ И МАРШРУТ
// ════════════════════════════════════════════════════════════════════════════════════════════════

// Порт `pathGetNextStepCoords@0xFBE6A0` + `pathSearchRouteRecursive@0xFBE220`.
//
// ★ FIX-ORIG D8. Эталон ищет маршрут ОБХОДОМ В ГЛУБИНУ с пометкой посещённых рёбер и
//   останавливается на ПЕРВОМ найденном пути (`g_bSearchFound = 1`), а «лучший» отбирает по
//   счётчику пройденных карт, который в рекурсии наращивается не на каждом ребре. На этом графе
//   такой поиск местами возвращает не тот первый шаг. Данные графа переносим 1:1 (это данные
//   оригинала), а сам поиск делаем ОБХОДОМ В ШИРИНУ по числу переходов: там, где оригинал прав,
//   ответ тот же, а где он ошибался — правильный.
bool UAsdaQuestMarkerSubsystem::NextPortalStep(int32 CurMap, int32 TargetMap, int32& OutCellX, int32& OutCellY,
	const UAsdaWalkGrid* Grid, const FIntPoint* MeTile, FIntPoint* OutApproach)
{
	if (CurMap == TargetMap) { return false; }          // эталон координаты не трогает

	// ★★ ЗАКРЫТЫЕ НА СЕРВЕРЕ КАРТЫ — НЕ В МАРШРУТЕ (27.09.2026, отчёт `Reports/owner_0927/tracker_closed_maps.md`).
	//   Напольный портал сервера (`sub_52C4C0`) молча игнорирует зону, ведущую на карту с `Use = 0` в
	//   `OpenMapInfo.ini` (сейчас 5, 15, 17, 18 и спец-карты 19…23). Эталон (`pathGraph_BuildNavMesh`) этого не
	//   знает и вёл бы к молчащему порталу; клиенту закрытость взять неоткуда (пакета нет) — источник один:
	//   реестр карт, `AsdaMapRegistry::IsMapOpenOnServer` (зеркало OpenMapInfo, сверяется `--check`).
	//   Отсекаем РЕБРО по карте НАЗНАЧЕНИЯ — ровно то, что проверяет сервер; выход С закрытой карты (если игрок
	//   как-то на ней оказался) остаётся в графе. Цель на закрытой карте ⇒ маршрута нет ⇒ ветка эталона
	//   «`pathGetNextStepCoords` вернул 0»: трекер оставляет координаты цели, автопуть снимает заявку.
	if (!AsdaMapRegistry::IsMapOpenOnServer(TargetMap)) { return false; }

	// BFS от текущей карты; для каждой достигнутой карты помним, с какой пришли.
	TMap<int32, int32> Prev;                            // карта → откуда пришли
	TArray<int32> Queue;
	Queue.Add(CurMap);
	Prev.Add(CurMap, INDEX_NONE);
	int32 Head = 0;
	bool bReached = false;
	while (Head < Queue.Num() && !bReached)
	{
		const int32 M = Queue[Head++];
		for (const FPortalEdge& E : kPortalGraph)
		{
			if (E.From != M || Prev.Contains(E.To)) { continue; }
			if (!AsdaMapRegistry::IsMapOpenOnServer(E.To)) { continue; }   // ★ зона на закрытую карту молчит
			Prev.Add(E.To, M);
			if (E.To == TargetMap) { bReached = true; break; }
			Queue.Add(E.To);
		}
	}
	if (!bReached) { return false; }

	// Разматываем путь назад до первого шага из текущей карты.
	int32 Step = TargetMap;
	while (true)
	{
		const int32* P = Prev.Find(Step);
		if (!P || *P == INDEX_NONE) { return false; }
		if (*P == CurMap) { break; }
		Step = *P;
	}

	// Зона РЕБРА `CurMap → Step` — это и есть портал, к которому надо идти.
	//   ★ У некоторых пар в данных НЕСКОЛЬКО зон (например 8→1); эталон берёт первую подходящую
	//     и выходит — повторяем порядок таблицы.
	for (const FPortalEdge& E : kPortalGraph)
	{
		if (E.From == CurMap && E.To == Step)
		{
			PickCellInPortalZone(E.X0, E.Z0, E.X1, E.Z1, Grid, MeTile, OutCellX, OutCellY, OutApproach);
			return true;
		}
	}
	return false;
}

// ★ Куда именно идти внутри зоны перехода. Зона — прямоугольник в клетках, и она вполне может быть
//   почти целиком НЕПРОХОДИМОЙ: у Alpen → Rein свободна лишь колонка 463, а центр зоны стоит в
//   стене (живая проверка 24.08.2026 — персонаж утыкался «в стойку рамки портала» и замирал).
//   Поэтому: берём проходимые клетки зоны и выбираем ближайшую к игроку; если сетки нет или
//   проходимых клеток не нашлось — центр зоны, как раньше (лучше идти хоть куда-то, чем никуда).
// ★★★ КУДА ИМЕННО ИДТИ В ЗОНЕ ПЕРЕХОДА — «в лицо порталу», а не в первый попавшийся край.
//
//  Живая проверка 24.08.2026 показала две беды подряд. Сначала мы целились в ЦЕНТР зоны — а он
//  у Alpen → Rein стоит в стене (проходима одна колонка 463, всё восточнее — блок). Затем стали
//  брать ближайшую свободную клетку — и попали в УГОЛ зоны (463,345), где персонаж вставал
//  и через пять попыток заявка снималась.
//
//  Правильный подход (решение владельца): подходить по прямой со стороны «лица» портала.
//  Лицо определяется по самим данным: это та сторона зоны, с которой снаружи есть проходимые
//  клетки — то есть откуда в неё вообще можно войти. Целимся в СЕРЕДИНУ лицевой линии, а точку
//  подвода ставим снаружи ровно напротив, чтобы последний отрезок пути был прямым и вёл внутрь
//  зоны перпендикулярно её кромке.
//
//  Для Alpen → Rein это даёт: лицо — западное, цель (463,347) — середина колонки, подвод (462,347).
void UAsdaQuestMarkerSubsystem::PickCellInPortalZone(int32 X0, int32 Z0, int32 X1, int32 Z1,
	const UAsdaWalkGrid* Grid, const FIntPoint* MeTile,
	int32& OutCellX, int32& OutCellY, FIntPoint* OutApproach)
{
	OutCellX = (X0 + X1) / 2;
	OutCellY = (Z0 + Z1) / 2;
	if (OutApproach) { *OutApproach = FIntPoint(OutCellX, OutCellY); }
	if (!Grid) { return; }                     // сетки нет — идём в центр, как раньше

	const int32 MinX = FMath::Min(X0, X1), MaxX = FMath::Max(X0, X1);
	const int32 MinZ = FMath::Min(Z0, Z1), MaxZ = FMath::Max(Z0, Z1);

	TArray<FIntPoint> Free;
	for (int32 Z = MinZ; Z <= MaxZ; ++Z)
	{
		for (int32 X = MinX; X <= MaxX; ++X)
		{
			if (Grid->IsTileWalkable(X, Z)) { Free.Add(FIntPoint(X, Z)); }
		}
	}
	if (Free.Num() == 0)
	{
		UE_LOG(LogAsdaQuestTrack, Warning,
			TEXT("[автопуть] в зоне перехода (%d,%d)…(%d,%d) нет ни одной проходимой клетки — идём в центр"),
			X0, Z0, X1, Z1);
		return;
	}

	// ── Лицо зоны: сторона, с которой снаружи можно подойти ────────────────────────────────────
	//   Считаем по каждой из четырёх сторон, сколько свободных клеток зоны имеют проходимого
	//   соседа СНАРУЖИ в этом направлении. Побеждает сторона с наибольшим числом входов;
	//   при равенстве — та, что ближе к игроку (ему меньше обходить).
	const FIntPoint Dirs[4] = { FIntPoint(-1,0), FIntPoint(1,0), FIntPoint(0,-1), FIntPoint(0,1) };
	int32 BestDir = -1, BestCount = 0;
	double BestDirDist = TNumericLimits<double>::Max();
	for (int32 d = 0; d < 4; ++d)
	{
		int32 Count = 0;
		double SumX = 0.0, SumZ = 0.0;
		for (const FIntPoint& C : Free)
		{
			const FIntPoint Outside(C.X + Dirs[d].X, C.Y + Dirs[d].Y);
			// сосед должен быть ВНЕ зоны и проходим — тогда с этой стороны в зону входят
			const bool bOutside = (Outside.X < MinX || Outside.X > MaxX || Outside.Y < MinZ || Outside.Y > MaxZ);
			if (bOutside && Grid->IsTileWalkable(Outside.X, Outside.Y))
			{
				++Count; SumX += C.X; SumZ += C.Y;
			}
		}
		if (Count == 0) { continue; }
		const double Dist = MeTile
			? FVector2D::DistSquared(FVector2D((float)MeTile->X, (float)MeTile->Y),
			                         FVector2D((float)(SumX / Count), (float)(SumZ / Count)))
			: 0.0;
		if (Count > BestCount || (Count == BestCount && Dist < BestDirDist))
		{
			BestDir = d; BestCount = Count; BestDirDist = Dist;
		}
	}

	// ── Цель: середина лицевой линии (если лицо не нашлось — середина всех свободных клеток) ──
	TArray<FIntPoint> Face;
	if (BestDir >= 0)
	{
		for (const FIntPoint& C : Free)
		{
			const FIntPoint Outside(C.X + Dirs[BestDir].X, C.Y + Dirs[BestDir].Y);
			const bool bOutside = (Outside.X < MinX || Outside.X > MaxX || Outside.Y < MinZ || Outside.Y > MaxZ);
			if (bOutside && Grid->IsTileWalkable(Outside.X, Outside.Y)) { Face.Add(C); }
		}
	}
	const TArray<FIntPoint>& Pool = (Face.Num() > 0) ? Face : Free;

	double MidX = 0.0, MidZ = 0.0;
	for (const FIntPoint& C : Pool) { MidX += C.X; MidZ += C.Y; }
	MidX /= Pool.Num(); MidZ /= Pool.Num();

	FIntPoint Target = Pool[0];
	double BestD = TNumericLimits<double>::Max();
	for (const FIntPoint& C : Pool)
	{
		const double D = FVector2D::DistSquared(FVector2D((float)MidX, (float)MidZ),
		                                        FVector2D((float)C.X, (float)C.Y));
		if (D < BestD) { BestD = D; Target = C; }
	}
	OutCellX = Target.X;
	OutCellY = Target.Y;

	// ── Точка подвода: снаружи ровно напротив цели, чтобы вход был прямым и перпендикулярным ──
	if (OutApproach)
	{
		*OutApproach = Target;
		if (BestDir >= 0)
		{
			const FIntPoint App(Target.X + Dirs[BestDir].X, Target.Y + Dirs[BestDir].Y);
			if (Grid->IsTileWalkable(App.X, App.Y)) { *OutApproach = App; }
		}
	}

	UE_LOG(LogAsdaQuestTrack, Log,
		TEXT("[автопуть] зона перехода (%d,%d)…(%d,%d): свободных %d, лицо %s (%d входов) → цель (%d,%d), подвод (%d,%d)"),
		X0, Z0, X1, Z1, Free.Num(),
		(BestDir < 0) ? TEXT("не определено")
			: (BestDir == 0 ? TEXT("запад") : BestDir == 1 ? TEXT("восток") : BestDir == 2 ? TEXT("север") : TEXT("юг")),
		BestCount, OutCellX, OutCellY,
		OutApproach ? OutApproach->X : OutCellX, OutApproach ? OutApproach->Y : OutCellY);
}

// Порт блока выбора цели из `CQuestTracker_BuildWaypointStrings` (§3.1 отчёта).
bool UAsdaQuestMarkerSubsystem::GetTrackTarget(int32 QuestNo, int32 CurMapId, FAsdaTrackTarget& Out) const
{
	HoldTrackerTables();   // ★ 15.09 (dt_gc_hold): DT_QuestGuide — сильно у подсистемы на сессию
	Out = FAsdaTrackTarget();
	if (QuestNo < 0) { return false; }

	// Стадия задания берётся из слота журнала — эталон ищет её перебором тех же 12 слотов.
	int32 Stage = -1;
	for (const FAsdaQuestSlot& S : Slots)
	{
		if (S.QuestNo == QuestNo) { Stage = S.Stage; }
	}
	if (Stage < 0) { return false; }

	const UDataTable* Dt = AsdaDt::Cached(kGuideDT);
	if (!Dt) { return false; }

	// ★ Эталон перебирает ВСЕ записи гида БЕЗ выхода из цикла, поэтому в дело идёт ПОСЛЕДНЯЯ
	//   подходящая запись С КООРДИНАТАМИ, а имя цели — последней подходящей вообще. Повторяем 1:1.
	int32 CellX = -1, CellY = -1, MapId = -1;
	for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
	{
		const FAsdaQuestGuideRow* R = reinterpret_cast<const FAsdaQuestGuideRow*>(It.Value);
		if (!R || R->QuestNo != QuestNo || R->Stage != Stage) { continue; }
		Out.TargetName = AsdaEntityText::QuestTargetName(R->TargetKind, R->TargetId);   // ★ волна 3 п.14: 1 = моб, иначе NPC
		if (R->X != -1 && R->Y != -1)
		{
			CellX = R->X; CellY = R->Y; MapId = R->MapId;
		}
	}
	// ★ FIX-ORIG D4: у эталона `mapId` — НЕИНИЦИАЛИЗИРОВАННЫЙ локал. Если у записей гида нет
	//   координат, сравнение «моя ли это карта» читало мусор со стека, и ветка стрелки выбиралась
	//   случайно. У нас нет координат ⇒ цели нет, показываем только заголовок.
	if (MapId < 0 || CellX < 0 || CellY < 0) { return false; }

	Out.MapId    = MapId;
	Out.bSameMap = (MapId == CurMapId);
	Out.CellX    = CellX;
	Out.CellY    = CellY;
	Out.bValid   = true;

	if (!Out.bSameMap)
	{
		int32 PX = 0, PY = 0;
		if (NextPortalStep(CurMapId, MapId, PX, PY))
		{
			// «Расстояние до ближайшего портала по пути к той локации» — так это описывает владелец,
			//   и ровно это делает эталон: подменяет координаты цели координатами портала.
			Out.CellX = PX; Out.CellY = PY; Out.bViaPortal = true;
		}
		// Маршрута нет (напр. карта-остров вроде 19, или ★ 27.09 цель на карте, закрытой на сервере, —
		//   5/15/17/18) — остаются координаты цели на чужой карте.
		// Эталон ведёт себя так же: `pathGetNextStepCoords` возвращает 0 и ничего не меняет.
	}
	return true;
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ОКНО 1800
// ════════════════════════════════════════════════════════════════════════════════════════════════

// Порт `CQuest_State_BuildUI@0xC580E0`: найти окно и все N×(заголовок + 3 строки).
//   ★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА: записей 12, а в `.JUI` контролов ровно на 6 (1801…1824) —
//     недостающие шесть групп КЛОНИРУЮТСЯ с первой (блок «записи 7…12» ниже).
bool UAsdaQuestMarkerSubsystem::EnsureTrackerWidget()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	if (WM->IsWindowLive(TrackWin) && bTrackUiReady) { return true; }

	bTrackUiReady = false;
	TrackGroups.Reset(); TrackTitles.Reset(); TrackConds.Reset(); TrackArrows.Reset();
	TrackBg = nullptr; TrackDragZone = nullptr; TrackCloseBtn = nullptr;

	// ★ SelfHitTestInvisible на корне: окно растянуто на вьюпорт, а с `Visible` оно съедало бы
	//   клики по миру (та же мина, что была у мини-карты 2026-08-01).
	TrackWin = WM->OpenWindowByPath(kTrackWinId, kTrackWbp, kTrackZ, ESlateVisibility::SelfHitTestInvisible);
	if (!TrackWin) { return false; }

	TrackBg = Cast<UImage>(AsdaUi::FindNodeExact(TrackWin, TEXT("Quest_State_Win_0_0")));
	TrackDragZone = AsdaUi::FindNodeExact(TrackWin, TEXT("Quest_State_Win_0_DRAG"));
	TrackCloseBtn = AsdaUi::FindNodeExact(TrackWin, TEXT("Quest_State_CloseBtn"));
	// База отсчёта строк — ДИЗАЙН-верх окна (в импортированном ассете 382: Origin 374 + Align 8).
	//   ★ Вычитаем уже применённый сдвиг перетаскивания: верх окна мы теперь пишем САМИ
	//   (см. `UpdateTrackerWindow`), и без этой поправки восстановленная из ini позиция
	//   складывалась бы сама с собой при каждом открытии окна.
	FVector2D DragOff0(0.f, 0.f);
	WM->GetAppliedWindowOffset(kTrackWinId, NAME_None, DragOff0);
	float BgTop = 0.f;
	if (const UCanvasPanelSlot* BgSlot = AsdaUi::SlotOf(TrackBg))
	{
		BgTop = BgSlot->GetOffsets().Top;
		TrackBaseTop = BgTop - DragOff0.Y;
	}
	// Смещения ручки и кнопки закрытия относительно верха окна — из САМОГО ассета (в .JUI 0 и +2),
	//   а не константой в коде: окно двигается целиком, и они обязаны ехать вместе с ним.
	if (const UCanvasPanelSlot* DS = AsdaUi::SlotOf(TrackDragZone)) { TrackDragDY  = DS->GetOffsets().Top - BgTop; }
	if (const UCanvasPanelSlot* CS = AsdaUi::SlotOf(TrackCloseBtn)) { TrackCloseDY = CS->GetOffsets().Top - BgTop; }

	// ══ ★★★ ЗАПИСИ 7…12: КЛОНЫ ПЕРВОЙ ЗАПИСИ ══════════════════════════════════════
	//   В `gamemain_jui.ini` у окна 1800 заготовлены контролы 1801…1824 — РОВНО ШЕСТЬ групп
	//   «заголовок + 3 строки условий». Лимит порта — 12 (решение владельца), и недостающие шесть
	//   групп нельзя ни выдумать (закон `no-invented-ui`), ни дописать в .JUI (переимпорт затрёт
	//   правку — закон `tools-no-import-side-effects`). Поэтому берётся УЖЕ ИМПОРТИРОВАННАЯ первая
	//   группа и клонируется общей точкой `AsdaUi::CloneWidgetSubtree`: у клона те же скины,
	//   шрифты, цвета, размеры и отступы, потому что это буквально она сама.
	//   Имена продолжают эталонный ряд (`Quest_State_Title7_grp` / `Quest_State_Title7` /
	//   `Quest_State_Condition7_1…3`), поэтому дальше всё дерево разбирается ОДНИМ И ТЕМ ЖЕ циклом —
	//   особого пути для клонов нет ни в разборе, ни в раскладке, ни в дампах окна.
	//   ★ Клонируем ДО первого `ApplyFormatted`: тот подменяет `UTextBlock` на `URichTextBlock`,
	//     и клон, снятый позже, унаследовал бы уже подменённый вид.
	//   ★ Клонов ровно `kTrackerHardMaxEntries`, а не `TrackerLimit()`: CVar отката можно поменять
	//     прямо в игре, поэтому контролы готовятся заранее (лишние группы просто свёрнуты).
	if (UCanvasPanel* RootCv = Cast<UCanvasPanel>(TrackWin->GetRootWidget()))
	{
		UWidget* SrcGrp = AsdaUi::FindNodeExact(TrackWin, TEXT("Quest_State_Title1_grp"));
		for (int32 i = kTrackerEtalonMaxEntries; SrcGrp && i < kTrackerHardMaxEntries; ++i)
		{
			const int32 N = i + 1;
			const FString GrpName = FString::Printf(TEXT("Quest_State_Title%d_grp"), N);
			if (AsdaUi::FindNodeExact(TrackWin, *GrpName)) { continue; }   // уже склонировано
			const bool bCloned = AsdaUi::CloneWidgetSubtree(TrackWin, SrcGrp, RootCv,
				[N](const UWidget* W) -> FString
				{
					// Единственное отличие клона — НОМЕР записи в имени узла.
					FString Nm = W->GetName();
					Nm.ReplaceInline(TEXT("Title1"),      *FString::Printf(TEXT("Title%d"), N));
					Nm.ReplaceInline(TEXT("Condition1_"), *FString::Printf(TEXT("Condition%d_"), N));
					return Nm;
				}) != nullptr;
			UE_LOG(LogAsdaQuestTrack, Verbose, TEXT("[трекер] запись %d: клон группы %s"),
				N, bCloned ? TEXT("создан") : TEXT("НЕ создан"));
		}
	}

	TrackGroups.SetNum(kTrackerHardMaxEntries);
	TrackTitles.SetNum(kTrackerHardMaxEntries);
	TrackConds.SetNum(kTrackerHardMaxEntries * kTrackerMaxLines);
	TrackArrows.SetNum(kTrackerHardMaxEntries);

	for (int32 i = 0; i < kTrackerHardMaxEntries; ++i)
	{
		const FString Grp = FString::Printf(TEXT("Quest_State_Title%d_grp"), i + 1);
		const FString Ttl = FString::Printf(TEXT("Quest_State_Title%d"), i + 1);
		TrackGroups[i] = Cast<UCanvasPanel>(AsdaUi::FindNodeExact(TrackWin, *Grp));
		TrackTitles[i] = AsdaUi::FindNodeExact(TrackWin, *Ttl);
		// ★ Стиль снимаем ЗДЕСЬ, пока подпись ещё `UTextBlock` из JUI: после первого
		//   `ApplyFormatted` она станет `URichTextBlock`, и кегль/цвет/тень уже не прочитать.
		if (!bTrackTitleStyleCaptured)
		{
			if (const UTextBlock* Tb = Cast<UTextBlock>(TrackTitles[i]))
			{
				TrackTitleStyle = FTextBlockStyle()
					.SetFont(Tb->GetFont())
					.SetColorAndOpacity(Tb->GetColorAndOpacity())
					.SetShadowOffset(Tb->GetShadowOffset())
					.SetShadowColorAndOpacity(Tb->GetShadowColorAndOpacity());
				TrackTitleJustify = AsdaUi::ReadJustification(Tb);
				bTrackTitleStyleCaptured = true;
			}
		}
		for (int32 j = 0; j < kTrackerMaxLines; ++j)
		{
			const FString Cnd = FString::Printf(TEXT("Quest_State_Condition%d_%d"), i + 1, j + 1);
			TrackConds[i * kTrackerMaxLines + j] = Cast<UTextBlock>(AsdaUi::FindNodeExact(TrackWin, *Cnd));
		}
	}

	// ── Стрелка компаса ──
	//   В оригинале это не контрол JUI, а квад, который рисует `CQuest_Episode_DrawWaypoints`
	//   прямо в потоке отрисовки мини-карты. Контрола под него в `.JUI` нет и быть не может,
	//   поэтому создаём `UImage` кодом — по одному на запись, детьми группы записи. Дублирования
	//   тут нет: это ровно тот же единственный значок, что рисует эталон.
	UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();   // общий inline-доступ из AsdaTextureSubsystem.h
	if (TexMgr)
	{
		ArrowTexSame = TexMgr->LoadTextureCached(FString(kMapTexDir) + kArrowSame);
		ArrowTexFar  = TexMgr->LoadTextureCached(FString(kMapTexDir) + kArrowFar);
	}
	for (int32 i = 0; i < kTrackerHardMaxEntries; ++i)
	{
		UCanvasPanel* Grp = TrackGroups[i];
		if (!Grp) { continue; }
		const FName ArrowName(*FString::Printf(TEXT("Quest_State_Arrow%d"), i + 1));
		UImage* Arrow = Cast<UImage>(AsdaUi::FindNodeExact(TrackWin, *ArrowName.ToString()));
		if (!Arrow)
		{
			Arrow = TrackWin->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), ArrowName);
			UCanvasPanelSlot* S = Grp->AddChildToCanvas(Arrow);
			if (S)
			{
				S->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
				S->SetAlignment(FVector2D(0.5f, 0.5f));   // (x, y) эталона — ЦЕНТР квада 20×20
				S->SetSize(FVector2D(kArrowSize, kArrowSize));
				S->SetZOrder(1);
			}
		}
		Arrow->SetVisibility(ESlateVisibility::Collapsed);
		Arrow->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		TrackArrows[i] = Arrow;
	}

	// ★ FIX-ORIG D3: кнопку закрытия 1850 эталонный `BuildUI` не привязывает вовсе — окно нельзя
	//   было закрыть кнопкой, только общим `uiCloseAllWindows`. Привязываем: кнопка прячет окно,
	//   как эталонный `questTracker_Hide(this, 1)`.
	if (UButton* Close = Cast<UButton>(TrackCloseBtn))
	{
		Close->OnClicked.RemoveAll(this);
		Close->OnClicked.AddDynamic(this, &UAsdaQuestMarkerSubsystem::OnTrackerCloseClicked);
	}

	bTrackUiReady = (TrackBg != nullptr) && (TrackTitles.Num() > 0) && (TrackTitles[0] != nullptr);
	// Сколько групп записей реально найдено — доказательство ЧИСЛОМ, что клоны 7…12 встали в дерево.
	int32 GroupsFound = 0;
	for (int32 i = 0; i < TrackGroups.Num(); ++i) { if (TrackGroups[i] && TrackTitles[i]) { ++GroupsFound; } }
	UE_LOG(LogAsdaQuestTrack, Log,
		TEXT("[трекер] окно 1800 открыто (z=%d), дерево разобрано: %s; групп записей %d из %d ")
		TEXT("(в .JUI заготовлено %d, остальные клонированы), лимит %d"),
		kTrackZ, bTrackUiReady ? TEXT("да") : TEXT("НЕТ"), GroupsFound, kTrackerHardMaxEntries,
		kTrackerEtalonMaxEntries, TrackerLimit());
	return bTrackUiReady;
}

void UAsdaQuestMarkerSubsystem::OnTrackerCloseClicked()
{
	TrackerHideWindow();
}

void UAsdaQuestMarkerSubsystem::TrackerHideWindow()
{
	if (TrackWin)
	{
		TrackWin->SetVisibility(ESlateVisibility::Collapsed);
		UE_LOG(LogAsdaQuestTrack, Log, TEXT("[трекер] окно скрыто (записей осталось %d)"), TrackEntries.Num());
	}
}

bool UAsdaQuestMarkerSubsystem::IsTrackerWindowShown() const
{
	// `CQuest_Episode_GetState` = байт видимости `pMainWindow + 292` — у нас видимость корня окна 1800.
	return AsdaUi::IsShown(TrackWin);   // ★ волна В6 ремесла (МЕЛК-8): общая точка видимости узла
}

void UAsdaQuestMarkerSubsystem::TrackerShowWindow()
{
	// `sub_C584C0@0xC584C0`: `if (this[1]) SetVisible(this[1], 1)` — только уже созданное окно.
	if (TrackWin && !IsTrackerWindowShown())
	{
		TrackWin->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		UE_LOG(LogAsdaQuestTrack, Log, TEXT("[трекер] окно показано (восстановление снимка окон, записей %d)"), TrackEntries.Num());
	}
}

// Порт `CQuestTracker_BuildWaypointStrings@0xC58A10` + блок проявления `CQuest_State_Update`.
//   Зовётся каждый кадр — в эталоне так же (из `MiniMap_Render@0xB4FCC0`), потому что расстояние
//   и угол стрелки меняются с каждым шагом игрока и поворотом камеры.
void UAsdaQuestMarkerSubsystem::UpdateTrackerWindow(float DeltaTime)
{
	ASDA_PERF_SCOPE("Quest.UpdateTrackerWindow");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }

	// Нечего показывать — окно не держим открытым (и не тратим кадры на разбор дерева).
	if (TrackEntries.Num() == 0)
	{
		if (TrackWin && TrackWin->GetVisibility() != ESlateVisibility::Collapsed)
		{
			TrackWin->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}
	if (!EnsureTrackerWidget()) { return; }
	if (TrackWin->GetVisibility() == ESlateVisibility::Collapsed)
	{
		TrackWin->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	// ── Игрок и карта ──
	//   ★ Отсутствие павна окно НЕ прячет: эталон в `BuildWaypointStrings` игрока не проверяет
	//     вовсе и всё равно печатает заголовки записей. Без павна просто нет точки отсчёта —
	//     дистанция и угол считаются от нуля, а заголовки и строки целей видны. Ранняя проверка
	//     `if (!P) return;` дала бы ложное «трекера нет» в редакторском просмотре без павна.
	const UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
	AAsdaPlayerCharacter* P = WS ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor) : nullptr;
	if (P && (!IsValid(P) || P->GetWorld() != GI->GetWorld())) { P = nullptr; }
	const UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>();
	const int32 CurMap = LM ? LM->CurrentMapId : -1;

	const FVector2D SelfGame = P ? P->GetGamePosXZ() : FVector2D::ZeroVector;
	// ★★★ Угол камеры — ЕДИНАЯ точка порта (та же, что у конуса мини-карты и карты мира).
	//   Эталон здесь берёт вектор направления камеры `Vector3_GetAt4(g_pRenderer)`, а НЕ поворот
	//   персонажа: именно на этом мы один раз уже обожглись с конусом мини-карты.
	const float CamYaw = P ? AsdaUi::CameraConeYaw(P) : 0.f;

	// ── Проявление окна по наведению (`CQuest_State_Update@0xC58340`) ──
	//   Событие 8/10 у эталона = курсор вошёл/вышел из окна 1800. У нас то же самое даёт
	//   общая проверка `AsdaUi::IsCursorOverWidget` по фону окна.
	const bool bHover = TrackBg && AsdaUi::IsCursorOverWidget(TrackBg);
	const float Frames = FMath::Max(DeltaTime, 0.f) * kRefFps;
	TrackSlideAnim = bHover
		? FMath::Min(TrackSlideAnim + kSlideStep * Frames, kSlideMax)
		: FMath::Max(TrackSlideAnim - kSlideStep * Frames, 0.f);
	if (TrackBg)
	{
		FLinearColor C = TrackBg->GetColorAndOpacity();
		C.A = TrackSlideAnim / 255.f;      // эталон кладёт 0…60 в АЛЬФУ фона окна
		TrackBg->SetColorAndOpacity(C);
	}

	// ══ ★★★ КУДА ВСТАЁТ СПИСОК: ОКНО НЕ ДОЛЖНО УЕХАТЬ ЗА КРАЙ И НАКРЫТЬ СОСЕДЕЙ ════════════
	//   ОСОЗНАННОЕ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА (лимит 12 вместо 6). Сама раскладка строк остаётся
	//   эталонной (`lineY = 15*строк + 5*i + 18`, высота окна `15*строк + 45`), но у эталона
	//   список не мог быть выше 6×4 строк, а у нас может — 12×4 строки дают 765 px, больше
	//   базового экрана 768. Поэтому здесь считается ТРИ вещи, и все три — числами:
	//     1) `ContentH`  — эталонная высота списка ЦЕЛИКОМ (до раскладки, чтобы знать заранее);
	//     2) `WinTop`    — верх окна: эталонные 382 (+ сдвиг мышью), а если не влезает — подъём
	//        вверх по СВОБОДНОЙ правой колонке, но не выше низа мини-карты;
	//     3) `Fit`       — рендер-масштаб всего блока, если не хватило и подъёма (текст мельче,
	//        но НИ ОДНА запись не теряется и ничего не вылезает за кадр).
	//   При 6 записях и обычных разрешениях всё это тождественно эталону: `WinTop = 382`, `Fit = 1`.
	int32 PlanLines = 0;
	for (int32 i = 0; i < TrackEntries.Num(); ++i)
	{
		if (!TrackGroups.IsValidIndex(i) || !TrackGroups[i] || !TrackTitles.IsValidIndex(i) || !TrackTitles[i]) { continue; }
		PlanLines += 1 + TrackEntries[i].LineCount;   // заголовок + строки условий
	}
	const float ContentH = kLineH * PlanLines + kWinExtraH;

	// Сдвиг окна пользователем (перетаскивание + восстановленная из ini раскладка) — ОБЩАЯ точка
	//   `UAsdaWindowSubsystem`, своего учёта позиции трекер не ведёт.
	FVector2D DragOff(0.f, 0.f);
	if (UAsdaWindowSubsystem* WMv = GI->GetSubsystem<UAsdaWindowSubsystem>())
	{
		WMv->GetAppliedWindowOffset(kTrackWinId, NAME_None, DragOff);
	}
	const FVector2D VpUnits = AsdaUi::ViewportSizeUnits(GI->GetWorld());
	const float UserTop    = TrackBaseTop + DragOff.Y;                     // где окно стоит «по дизайну + мышь»
	const float BandTop    = FMath::Min(kMiniMapBottom + kBandMargin, UserTop);   // выше не поднимаем
	const float BandBottom = FMath::Max(VpUnits.Y - kHotbarTopFromH - kBandMargin, BandTop + kLineH);
	float WinTop = UserTop;
	if (WinTop + ContentH > BandBottom)
	{
		WinTop = FMath::Max(BandTop, BandBottom - ContentH);   // ★ двигаем ТОЛЬКО вверх, вниз — никогда
	}
	float Fit = 1.f;
	const float Avail = BandBottom - WinTop;
	if (ContentH > Avail && ContentH > KINDA_SMALL_NUMBER)
	{
		Fit = FMath::Max(Avail / ContentH, kMinFitScale);
	}

	// ── Записи ──
	int32 VisibleLines = 0;
	for (int32 i = 0; i < kTrackerHardMaxEntries; ++i)
	{
		UCanvasPanel* Grp = TrackGroups.IsValidIndex(i) ? TrackGroups[i].Get() : nullptr;
		UWidget* Title    = TrackTitles.IsValidIndex(i) ? TrackTitles[i].Get() : nullptr;
		UImage* Arrow     = TrackArrows.IsValidIndex(i) ? TrackArrows[i].Get() : nullptr;
		if (!Grp || !Title) { continue; }

		if (!TrackEntries.IsValidIndex(i))
		{
			Grp->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		const FAsdaQuestTrackEntry& E = TrackEntries[i];

		// ── Цель, расстояние, цвет ──
		FAsdaTrackTarget T;
		const bool bHasTarget = GetTrackTarget(E.QuestNo, CurMap, T);
		float DistM = 0.f;
		if (bHasTarget)
		{
			// Клетка → игровые единицы (эталон: `mapX * 100`), расстояние строго по плоскости:
			//   и у камеры, и у игрока, и у цели вертикаль обнулена.
			const FVector2D TargetGame(T.CellX * kUnitsPerMeter, T.CellY * kUnitsPerMeter);
			DistM = FVector2D::Distance(TargetGame, SelfGame) / kUnitsPerMeter;
		}

		const TCHAR* Col = kColFar;
		if (bHasTarget && T.bSameMap && DistM > 0.f)
		{
			// ★ FIX-ORIG D7: полуинтервалы вместо строгих границ с обеих сторон.
			Col = (DistM < kNearM) ? kColNear : (DistM < kMidM ? kColMid : kColFar);
		}
		// Пять ведущих пробелов — место под значок стрелки, ровно как в эталонном формате.
		const FString TitleText = bHasTarget
			// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): формат «     [<FC=%s>%d</F>м]%s» — строка 990449 (единица «м» на языке сессии).
			? UAsdaTextSubsystem::FormatPrintf(FAsdaText::LocRaw(990449),
				{ FString(Col), FString::FromInt(FMath::FloorToInt(DistM)), E.Title })
			: FString::Printf(TEXT("     %s"), *E.Title);

		Grp->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		// ★ Заголовок несёт тег цвета — только через единую точку форматированного текста.
		//   Она идемпотентна и возвращает актуальный виджет (подменённый `URichTextBlock`).
		FAsdaRichApplyOpts Opts;
		Opts.BaseStyle = &TrackTitleStyle;
		Opts.bSetJustify = true;
		Opts.Justify   = TrackTitleJustify;
		Opts.bWrap     = false;      // ★ грабля: Autowrap внутри Canvas ломает раскладку
		if (UWidget* New = UAsdaRichText::ApplyFormatted(Title, TitleText, bTrackTitleStyleCaptured ? &Opts : nullptr))
		{
			TrackTitles[i] = New;
			Title = New;
		}
		Title->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

		// ── Положение записи (эталон: `lineY = 15*visibleLines + 5*i + 18`) ──
		//   ★ `lineY` эталона — координата ВНУТРИ ОКНА (`CControl_SetField74_75` двигает контрол
		//     относительно родителя-окна). У нас группы записей — дети корневого канваса, поэтому
		//     к `lineY` прибавляется верх окна. Без этого весь трекер уезжал к верхней кромке экрана.
		const float LineY = kLineH * VisibleLines + kRowGap * i + kFirstLineTop;
		if (UCanvasPanelSlot* GS = AsdaUi::SlotOf(Grp))
		{
			FMargin M = GS->GetOffsets();
			M.Top = WinTop + Fit * LineY;   // ★ `WinTop`/`Fit` — см. блок «куда встаёт список»
			GS->SetOffsets(M);
		}
		// Ужимание блока (`Fit < 1`) — РЕНДЕР-масштаб от левого-верха группы: раскладка внутри
		//   записи остаётся эталонной, меняется только то, как она рисуется. При `Fit == 1` это
		//   тождественная операция, то есть на обычных разрешениях вид эталонный.
		Grp->SetRenderTransformPivot(FVector2D(0.f, 0.f));
		Grp->SetRenderScale(FVector2D(Fit, Fit));
		++VisibleLines;

		// ── Строки условий ──
		for (int32 j = 0; j < kTrackerMaxLines; ++j)
		{
			UTextBlock* Cnd = TrackConds.IsValidIndex(i * kTrackerMaxLines + j)
				? TrackConds[i * kTrackerMaxLines + j].Get() : nullptr;
			if (!Cnd) { continue; }
			// Эталон: строка показывается, если она непустая И её номер меньше `lineCount`.
			const bool bShow = E.Cond.IsValidIndex(j) && !E.Cond[j].IsEmpty() && j < E.LineCount;
			Cnd->SetVisibility(bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			if (bShow) { Cnd->SetText(FText::FromString(E.Cond[j])); }
		}
		VisibleLines += E.LineCount;

		// ── Стрелка компаса ──
		if (Arrow)
		{
			if (!bHasTarget)
			{
				Arrow->SetVisibility(ESlateVisibility::Collapsed);
			}
			else
			{
				// Угол = «направление на цель» − «направление камеры» (§4.1). Обе величины
				//   отсчитываются от игровой оси +Z, поэтому и разность — в тех же градусах.
				const FVector2D TargetGame(T.CellX * kUnitsPerMeter, T.CellY * kUnitsPerMeter);
				const float Ang = ArrowAngleVsCamera(TargetGame - SelfGame, CamYaw);

				// Альфа по дистанции (только у «своей» карты); у чужой — фиксированные 56/255,
				//   как в эталонном `0x38FFFFFF`.
				// ★ ОТСТУПЛЕНИЕ № 4 (заявка владельца «подними на 10 %»): к обеим веткам
				//   прибавляется `Boost` = проценты ПОЛНОЙ ШКАЛЫ × 255 (по умолчанию +25.5).
				//   Поднимается и кривая, и её дно, и фикс. альфа чужой карты — числа и
				//   обоснование выбора в `Reports/quest_tracker_fix.md` §Д2.3.
				//   `Asda.Quest.TrackerArrowAlphaBoost 0` возвращает эталонную формулу.
				const float Alpha = ArrowAlpha255(T.bSameMap, DistM) / 255.f;

				UTexture2D* Tex = T.bSameMap ? ArrowTexSame : ArrowTexFar;
				if (Tex)
				{
					FSlateBrush B;
					B.SetResourceObject(Tex);
					B.ImageSize = FVector2D(kArrowSize, kArrowSize);
					Arrow->SetBrush(B);
				}
				Arrow->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, Alpha));
				// ★ FIX-ORIG D9: эталон после поворота обрезает координаты углов до целых пикселей,
				//   и значок заметно дёргается. В UMG поворот субпиксельный — округление не повторяем.
				Arrow->SetRenderTransformAngle(Ang);
				if (UCanvasPanelSlot* AS = AsdaUi::SlotOf(Arrow))
				{
					// Центр значка: левый край окна + 11 по X, середина строки заголовка по Y.
					AS->SetPosition(FVector2D(kArrowCenterX - (kWinW - 217.f), kArrowCenterY));
				}
				Arrow->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
		}
	}

	// ── Плита окна: верх + высота (эталон: `15 * visibleLines + 45`) ──
	//   Эталон писал только ВЫСОТУ — верх у него всегда 382. Мы пишем и верх: список из 12 записей
	//   при нехватке места поднимается (см. блок «куда встаёт список»), и плита обязана ехать с ним.
	const float WinH = Fit * (kLineH * VisibleLines + kWinExtraH);
	if (UCanvasPanelSlot* BS = AsdaUi::SlotOf(TrackBg))
	{
		FMargin M = BS->GetOffsets();       // POINT-анкор: Left/Top — позиция, Right/Bottom — размер
		M.Top = WinTop;
		M.Bottom = WinH;
		BS->SetOffsets(M);
	}
	// Ручка перетаскивания обязана совпадать с плитой — иначе окно тянулось бы за пустое место
	//   (в .JUI зона 222×200 стоит поверх плиты, а плита у нас теперь переменной высоты).
	if (UCanvasPanelSlot* DS = AsdaUi::SlotOf(TrackDragZone))
	{
		FMargin M = DS->GetOffsets();
		M.Top = WinTop + TrackDragDY;
		M.Bottom = WinH;
		DS->SetOffsets(M);
	}
	// Кнопка закрытия 1850 — в углу плиты (в .JUI её верх на 2 px ниже верха окна).
	if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(TrackCloseBtn))
	{
		FMargin M = CS->GetOffsets();
		M.Top = WinTop + TrackCloseDY;
		CS->SetOffsets(M);
	}
	TrackLastTop = WinTop; TrackLastH = WinH; TrackLastFit = Fit;
	TrackLastBandTop = BandTop; TrackLastBandBot = BandBottom; TrackLastVpH = VpUnits.Y;

	// След в логе, если пришлось отступить от эталонной геометрии (нужен для разбора жалоб
	//   «окно не там»): печатаем ОДИН раз на изменение, а не каждый кадр.
	const bool bDeviated = (!FMath::IsNearlyEqual(WinTop, UserTop, 0.5f) || Fit < 0.999f);
	if (bDeviated != bTrackLayoutDeviated)
	{
		bTrackLayoutDeviated = bDeviated;
		if (bDeviated)
		{
			UE_LOG(LogAsdaQuestTrack, Log,
				TEXT("[трекер] список не влезал: строк %d, нужно %.0f px, полоса %.0f…%.0f → верх %.0f (эталон %.0f), масштаб %.2f"),
				VisibleLines, ContentH, BandTop, BandBottom, WinTop, UserTop, Fit);
		}
		else
		{
			UE_LOG(LogAsdaQuestTrack, Log, TEXT("[трекер] геометрия окна эталонная (верх %.0f, масштаб 1.00)"), WinTop);
		}
	}
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ДИАГНОСТИКА
// ════════════════════════════════════════════════════════════════════════════════════════════════
FString UAsdaQuestMarkerSubsystem::DescribeTracker() const
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	const int32 CurMap = LM ? LM->CurrentMapId : -1;

	FString S = FString::Printf(TEXT("=== ТРЕКЕР ЗАДАНИЙ (окно 1800) === записей %d из %d, карта %d, окно %s\n"),
		TrackEntries.Num(), TrackerLimit(), CurMap,
		TrackWin ? (TrackWin->GetVisibility() == ESlateVisibility::Collapsed ? TEXT("скрыто") : TEXT("показано"))
		         : TEXT("не открыто"));

	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	AAsdaPlayerCharacter* P = WS ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor) : nullptr;
	const FVector2D SelfGame = P ? P->GetGamePosXZ() : FVector2D::ZeroVector;
	S += FString::Printf(TEXT("игрок (game) = (%.0f, %.0f), угол камеры = %.1f°\n"),
		SelfGame.X, SelfGame.Y, P ? AsdaUi::CameraConeYaw(P) : 0.f);

	// ★ ГЕОМЕТРИЯ ЧИСЛАМИ: верх/высота окна, полоса, в которую его вписывают, и масштаб.
	//   Эталонный случай — «верх 382, масштаб 1.00»; всё иное значит, что список не влезал.
	S += FString::Printf(
		TEXT("окно: верх %.0f (дизайн %.0f), высота %.0f, масштаб %.2f; вьюпорт H %.0f, полоса %.0f…%.0f; ")
		TEXT("групп записей %d, кнопка закрытия %s\n"),
		TrackLastTop, TrackBaseTop, TrackLastH, TrackLastFit, TrackLastVpH,
		TrackLastBandTop, TrackLastBandBot, TrackGroups.Num(),
		TrackCloseBtn ? TEXT("есть") : TEXT("НЕТ"));
	// Позиции всех групп — видно и то, что клоны 7…12 участвуют в раскладке.
	for (int32 i = 0; i < TrackGroups.Num(); ++i)
	{
		const UCanvasPanel* G = TrackGroups[i];
		if (!G) { S += FString::Printf(TEXT("   группа %2d: НЕТ ВИДЖЕТА\n"), i + 1); continue; }
		FVector2D Pos(0.f, 0.f), Sz(0.f, 0.f);
		AsdaUi::SlotRect(G, Pos, Sz);
		S += FString::Printf(TEXT("   группа %2d «%s»: %s, верх %.0f%s\n"),
			i + 1, *G->GetName(),
			G->GetVisibility() == ESlateVisibility::Collapsed ? TEXT("свёрнута") : TEXT("показана"),
			Pos.Y, i >= kTrackerEtalonMaxEntries ? TEXT("  (КЛОН)") : TEXT(""));
	}

	for (int32 i = 0; i < TrackEntries.Num(); ++i)
	{
		const FAsdaQuestTrackEntry& E = TrackEntries[i];
		FAsdaTrackTarget T;
		const bool bHas = GetTrackTarget(E.QuestNo, CurMap, T);
		S += FString::Printf(TEXT("[%d] id=%d questNo=%d «%s» строк=%d\n"), i, E.QuestId, E.QuestNo, *E.Title, E.LineCount);
		for (int32 j = 0; j < E.Cond.Num(); ++j)
		{
			if (!E.Cond[j].IsEmpty()) { S += FString::Printf(TEXT("      · %s\n"), *E.Cond[j]); }
		}
		if (!bHas)
		{
			S += TEXT("      цель: НЕТ (у записей DT_QuestGuide нет координат либо стадия не найдена)\n");
			continue;
		}
		const FVector2D TargetGame(T.CellX * kUnitsPerMeter, T.CellY * kUnitsPerMeter);
		const float DistM = FVector2D::Distance(TargetGame, SelfGame) / kUnitsPerMeter;
		S += FString::Printf(TEXT("      цель «%s» карта %d%s, клетка (%d,%d), %d м, угол %.1f°\n"),
			*T.TargetName, T.MapId,
			T.bSameMap ? TEXT(" (эта же)") : (T.bViaPortal ? TEXT(" → идём к порталу") : TEXT(" (маршрута нет)")),
			T.CellX, T.CellY, FMath::FloorToInt(DistM),
			ArrowAngleVsCamera(TargetGame - SelfGame, P ? AsdaUi::CameraConeYaw(P) : 0.f));
		// ★ Стрелка ЧИСЛАМИ: та же формула, что рисует значок (общая точка `ArrowAlpha255`),
		//   плюс эталонное значение — чтобы подъём был виден как разница, а не на слово.
		const float A = ArrowAlpha255(T.bSameMap, DistM);
		const float AEt = T.bSameMap
			? FMath::Clamp(255.f - kAlphaK * DistM, kAlphaMin, kAlphaMax)
			: kAlphaFarMap;
		const UImage* Arw = TrackArrows.IsValidIndex(i) ? TrackArrows[i].Get() : nullptr;
		const UTexture2D* Tex = T.bSameMap ? ArrowTexSame : ArrowTexFar;
		S += FString::Printf(
			TEXT("      стрелка: текстура «%s», альфа %.1f/255 (%.0f%%), эталон был %.1f (%.0f%%), подъём +%.1f%% шкалы; виджет %s\n"),
			Tex ? *Tex->GetName() : TEXT("НЕТ"), A, 100.f * A / 255.f, AEt, 100.f * AEt / 255.f,
			TrackerArrowAlphaBoost(),
			Arw ? (Arw->GetVisibility() == ESlateVisibility::Collapsed ? TEXT("свёрнут") : TEXT("показан")) : TEXT("НЕТ"));
	}
	return S;
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  КОНСОЛЬ
// ════════════════════════════════════════════════════════════════════════════════════════════════
#include "Quest/AsdaQuestLogSubsystem.h"

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaQuestTracker(
	TEXT("Asda.Quest.Tracker"),
	TEXT("Экранный трекер заданий (окно 1800). Без аргументов — что отслеживается, цель, дистанция, угол. "
	     "«track <ячейка 0..11>» — поставить ячейку журнала на отслеживание (как галка 743). "
	     "«untrack <ячейка>» — снять. «clear» — очистить всё."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
			UAsdaQuestMarkerSubsystem* Q = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
			UAsdaQuestLogSubsystem* L = GI ? GI->GetSubsystem<UAsdaQuestLogSubsystem>() : nullptr;
			if (!Q || !L) { UE_LOG(LogAsdaQuestTrack, Warning, TEXT("[трекер] подсистемы недоступны")); return; }

			if (Args.Num() >= 1 && Args[0].Equals(TEXT("clear"), ESearchCase::IgnoreCase))
			{
				Q->TrackerClear();
			}
			else if (Args.Num() >= 2 && Args[0].Equals(TEXT("track"), ESearchCase::IgnoreCase))
			{
				const int32 Slot = FCString::Atoi(*Args[1]);
				// ★ ЧЕРЕЗ ТОТ ЖЕ путь, что и галка 743 (`SetSlotTracked`): он строит строки целей,
				//   проверяет лимит и пишет список в настройки. Прямой `TrackerUpdateEntry` дал бы
				//   запись без строк, которую первый же приход состояния снёс бы как «протухшую».
				const bool bOk = L->SetSlotTracked(Slot, true);
				UE_LOG(LogAsdaQuestTrack, Log, TEXT("[трекер] ячейка %d → отслеживание %s"),
					Slot, bOk ? TEXT("включено") : TEXT("ОТКАЗАНО (нет строк целей / лимит / уже выполнено)"));
			}
			else if (Args.Num() >= 1 && Args[0].Equals(TEXT("limittest"), ESearchCase::IgnoreCase))
			{
				// ★ ПРОВЕРКА ЛИМИТА НА ЖИВОМ КОДЕ. На реальных данных лимит упирается редко
				//   (у большинства заданий шага нет отслеживаемых целей), поэтому проверяем сам
				//   отказ `if (записей >= лимит) return;` — лимит+1 попыток подряд.
				//   Число строк условий задаётся вторым аргументом (`limittest 3`) — им проверяется
				//   САМЫЙ ВЫСОКИЙ список (12 записей × 3 строки), на котором окно обязано вписаться
				//   в экран.
				const int32 Lines = (Args.Num() >= 2) ? FMath::Clamp(FCString::Atoi(*Args[1]), 1, 3) : 1;
				const int32 Limit = UAsdaQuestMarkerSubsystem::TrackerLimit();
				Q->TrackerClear();
				int32 Ok = 0;
				for (int32 i = 1; i <= Limit + 1; ++i)
				{
					bool bAdded = false;
					for (int32 j = 0; j < Lines; ++j)
					{
						bAdded = Q->TrackerUpdateEntry(900000 + i, 900000 + i,
							FString::Printf(TEXT("проверка лимита %d"), i),
							FString::Printf(TEXT("тест %d : 0/1 "), j + 1), j) || bAdded;
					}
					if (bAdded) { ++Ok; }
					UE_LOG(LogAsdaQuestTrack, Log, TEXT("[трекер] лимит-тест: попытка %d → %s"),
						i, bAdded ? TEXT("добавлено") : TEXT("ОТКАЗ"));
				}
				UE_LOG(LogAsdaQuestTrack, Log,
					TEXT("[трекер] лимит-тест: принято %d из %d, строк условий в записи %d ")
					TEXT("(лимит порта %d, эталон %d)"),
					Ok, Limit + 1, Lines, Limit, UAsdaQuestMarkerSubsystem::kTrackerEtalonMaxEntries);
			}
			else if (Args.Num() >= 2 && Args[0].Equals(TEXT("untrack"), ESearchCase::IgnoreCase))
			{
				const int32 Slot = FCString::Atoi(*Args[1]);
				L->SetSlotTracked(Slot, false);
			}
			UE_LOG(LogAsdaQuestTrack, Log, TEXT("%s"), *Q->DescribeTracker());
		}));
#endif // ASDA_DEV_CONSOLE

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ★ САМОТЕСТ МАРШРУТОВ: ЗАКРЫТЫЕ НА СЕРВЕРЕ КАРТЫ (27.09.2026)
//  Отчёт — `Reports/owner_0927/tracker_closed_maps.md`. Офлайн, пакетов не шлёт. Проверяет:
//    А. по ВСЕМ парам карт реестра: маршрут `NextPortalStep`, размотанный шаг за шагом, идёт только по
//       рёбрам `kPortalGraph` и ни разу не ступает на карту, закрытую на сервере;
//    Б. цель на закрытой карте ⇒ false, выходные координаты НЕ тронуты (ветка эталона «маршрута нет»);
//    В. регрессия: для каждой ОТКРЫТОЙ цели первый шаг и число переходов совпадают с прежним поиском без
//       фильтра (закрытые карты — тупики графа, объезд через них не нужен ни одному маршруту);
//    Г. реальный путь исполнителя: заявка автопути `BeginMapTravel` на закрытую карту — `DescribeMapTravel`
//       (та же функция шага) пишет «закрыта», на открытую — шаг к порталу; при наличии павна через 2 с
//       заявка на закрытую карту обязана быть снята самим тиком.
//  Запуск: `-game -RenderOffscreen /Game/Map/alpen/alpen … -ExecCmds="Asda.Load.CurrentMap 3, asda.Quest.RouteSelfTest quit"` → в лог
//    `[маршрут-тест] ИТОГ` (номер карты для части Г: при прямом открытии уровня менеджер загрузки его не ставит).
// ════════════════════════════════════════════════════════════════════════════════════════════════
// ★ общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "UI/AsdaMinimapSubsystem.h"        // заявка межкартового автопути (часть Г)
#include "Kismet/GameplayStatics.h"        // павн игрока (часть Г: тикает ли исполнитель)

namespace
{
	// Прежний поиск (до 27.09) без учёта закрытости — эталон для проверки регрессии (часть В).
	//   Возвращает первый шаг и число переходов; false — пути нет.
	bool RouteRefUnfiltered(int32 CurMap, int32 TargetMap, int32& OutFirst, int32& OutHops)
	{
		TMap<int32, int32> Prev;
		TArray<int32> Queue;
		Queue.Add(CurMap);
		Prev.Add(CurMap, INDEX_NONE);
		int32 Head = 0;
		bool bReached = false;
		while (Head < Queue.Num() && !bReached)
		{
			const int32 M = Queue[Head++];
			for (const FPortalEdge& E : kPortalGraph)
			{
				if (E.From != M || Prev.Contains(E.To)) { continue; }
				Prev.Add(E.To, M);
				if (E.To == TargetMap) { bReached = true; break; }
				Queue.Add(E.To);
			}
		}
		if (!bReached) { return false; }
		OutHops = 0;
		int32 Step = TargetMap;
		while (true)
		{
			++OutHops;
			const int32 P = Prev.FindChecked(Step);
			if (P == CurMap) { break; }
			Step = P;
		}
		OutFirst = Step;
		return true;
	}

	// Какое ребро `From → ?` содержит клетку (центр зоны без сетки — всегда внутри своей зоны). −1 — ни одно.
	int32 EdgeToByCell(int32 From, int32 CellX, int32 CellY)
	{
		for (const FPortalEdge& E : kPortalGraph)
		{
			if (E.From != From) { continue; }
			if (CellX >= FMath::Min(E.X0, E.X1) && CellX <= FMath::Max(E.X0, E.X1) &&
			    CellY >= FMath::Min(E.Z0, E.Z1) && CellY <= FMath::Max(E.Z0, E.Z1))
			{
				return E.To;
			}
		}
		return INDEX_NONE;
	}
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaQuestRouteSelfTest(
	TEXT("asda.Quest.RouteSelfTest"),
	TEXT("Самотест маршрутов трекера/автопути: закрытые на сервере карты (зеркало OpenMapInfo в реестре) в маршрут "
	     "не попадают, прочие маршруты без изменений. `quit` — выйти после прогона."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		// ★ Офлайн-гейт первым оператором: часть Г взводит автопуть — онлайн он двинул бы живого персонажа (движение 4011).
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Quest.RouteSelfTest"), KitArgs)) { return; }
		const bool bQuit = Args.ContainsByPredicate([](const FString& A) { return A.Equals(TEXT("quit"), ESearchCase::IgnoreCase); });
		int32 Fail = 0, Checks = 0;
		auto Expect = [&Fail, &Checks](bool bOk, const FString& What)
		{
			++Checks;
			if (!bOk) { ++Fail; UE_LOG(LogAsdaQuestTrack, Error, TEXT("[маршрут-тест] ПРОВАЛ: %s"), *What); }
		};

		FString Closed;
		for (int32 M = 0; M < AsdaMapRegistry::kMapCount; ++M)
		{
			if (!AsdaMapRegistry::IsMapOpenOnServer(M)) { Closed += FString::Printf(TEXT("%d "), M); }
		}
		UE_LOG(LogAsdaQuestTrack, Log, TEXT("[маршрут-тест] закрыты на сервере (реестр): %s"), *Closed);

		int32 Routes = 0, ClosedRefused = 0, Same = 0, WasViaClosed = 0;
		for (int32 A = 0; A < AsdaMapRegistry::kMapCount; ++A)
		{
			for (int32 B = 0; B < AsdaMapRegistry::kMapCount; ++B)
			{
				if (A == B) { continue; }
				int32 PX = -7777, PY = -7777;
				const bool bRoute = UAsdaQuestMarkerSubsystem::NextPortalStep(A, B, PX, PY);
				int32 RefFirst = -1, RefHops = 0;
				const bool bRef = RouteRefUnfiltered(A, B, RefFirst, RefHops);

				// ── Б: цель на закрытой карте ──
				if (!AsdaMapRegistry::IsMapOpenOnServer(B))
				{
					Expect(!bRoute, FString::Printf(TEXT("%d→%d: маршрут на ЗАКРЫТУЮ карту построен"), A, B));
					Expect(PX == -7777 && PY == -7777, FString::Printf(TEXT("%d→%d: выходные координаты тронуты при отказе"), A, B));
					if (!bRoute && bRef) { ++ClosedRefused; }
					continue;
				}

				// ── В: открытая цель — как прежде ──
				Expect(bRoute == bRef, FString::Printf(TEXT("%d→%d: наличие пути %d, прежде %d"), A, B, (int32)bRoute, (int32)bRef));
				if (!bRoute) { continue; }
				++Routes;

				// ── А: размотать маршрут шаг за шагом ──
				int32 Cur = A, Hops = 0, First = -1;
				bool bOkWalk = true;
				while (Cur != B && Hops <= AsdaMapRegistry::kMapCount)
				{
					int32 SX = 0, SY = 0;
					if (!UAsdaQuestMarkerSubsystem::NextPortalStep(Cur, B, SX, SY))
					{
						Expect(false, FString::Printf(TEXT("%d→%d: на карте %d маршрут оборвался"), A, B, Cur)); bOkWalk = false; break;
					}
					const int32 Next = EdgeToByCell(Cur, SX, SY);
					if (Next == INDEX_NONE)
					{
						Expect(false, FString::Printf(TEXT("%d→%d: клетка (%d,%d) на карте %d не в зоне перехода"), A, B, SX, SY, Cur)); bOkWalk = false; break;
					}
					Expect(AsdaMapRegistry::IsMapOpenOnServer(Next),
						FString::Printf(TEXT("%d→%d: маршрут ступает на ЗАКРЫТУЮ карту %d"), A, B, Next));
					if (First < 0) { First = Next; }
					Cur = Next; ++Hops;
				}
				if (!bOkWalk) { continue; }
				Expect(Cur == B, FString::Printf(TEXT("%d→%d: маршрут зациклился"), A, B));
				Expect(First == RefFirst && Hops == RefHops,
					FString::Printf(TEXT("%d→%d: первый шаг %d/переходов %d, прежде %d/%d"), A, B, First, Hops, RefFirst, RefHops));
				if (First == RefFirst && Hops == RefHops) { ++Same; }
			}
		}
		// Сколько маршрутов прежний поиск вёл к закрытой карте (т. е. к молчащему порталу) — для отчёта.
		for (int32 A = 0; A < AsdaMapRegistry::kMapCount; ++A)
		{
			for (int32 B = 0; B < AsdaMapRegistry::kMapCount; ++B)
			{
				int32 F = -1, H = 0;
				if (A != B && !AsdaMapRegistry::IsMapOpenOnServer(B) && RouteRefUnfiltered(A, B, F, H)) { ++WasViaClosed; }
			}
		}
		UE_LOG(LogAsdaQuestTrack, Log,
			TEXT("[маршрут-тест] А/Б/В: открытых маршрутов %d (совпали с прежними %d); на закрытые карты прежде вели %d, ")
			TEXT("теперь отказано %d"), Routes, Same, WasViaClosed, ClosedRefused);

		// ── Г: реальный путь заявки автопути ──
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaMinimapSubsystem* MM = GI ? GI->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr;
		const UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
		const int32 CurMap = LM ? LM->CurrentMapId : -1;
		// Закрытая цель, до которой прежний граф довёл бы; и открытая цель — для контроля.
		int32 ClosedTarget = -1, OpenTarget = -1;
		for (int32 B = 0; B < AsdaMapRegistry::kMapCount && CurMap >= 0; ++B)
		{
			int32 F = -1, H = 0;
			if (B == CurMap || !RouteRefUnfiltered(CurMap, B, F, H)) { continue; }
			if (!AsdaMapRegistry::IsMapOpenOnServer(B)) { if (ClosedTarget < 0) { ClosedTarget = B; } }
			else if (OpenTarget < 0) { OpenTarget = B; }
		}
		auto Finish = [W, bQuit](int32 InFail, int32 InChecks)
		{
			// ★ 11.10.2026 (В3 S1): единый итог «ИТОГ: ok=N fail=M» (было «PASS/FAIL — проверок N, провалов M»).
			AsdaSelfTestKit::Summary(LogAsdaQuestTrack, TEXT("[маршрут-тест]"), InChecks - InFail, InFail);
			if (bQuit && GEngine) { GEngine->DeferredCommands.Add(TEXT("quit")); }
		};
		// Номер карты ставит менеджер загрузки; при прямом открытии уровня (-game офлайн) он −1 — тогда перед
		//   самотестом задать его штатным крюком `Asda.Load.CurrentMap <id>`. `wait=N` — ждать номер до N с,
		//   перезапуская самотест раз в секунду (части А–В дешёвые, повтор безвреден); по умолчанию не ждём.
		int32 WaitLeft = 0;
		for (const FString& A : Args) { if (A.StartsWith(TEXT("wait="))) { WaitLeft = FCString::Atoi(*A.Mid(5)); } }
		if (MM && CurMap < 0 && WaitLeft > 0 && W)
		{
			const FString Again = FString::Printf(TEXT("asda.Quest.RouteSelfTest wait=%d%s"), WaitLeft - 1, bQuit ? TEXT(" quit") : TEXT(""));
			UE_LOG(LogAsdaQuestTrack, Log, TEXT("[маршрут-тест] Г: номер карты ещё не известен — повтор через 1 с (осталось %d)"), WaitLeft);
			FTimerHandle HW;
			W->GetTimerManager().SetTimer(HW, FTimerDelegate::CreateWeakLambda(MM, [W, Again]()
			{
				if (GEngine) { GEngine->Exec(W, *Again); }
			}), 1.f, false);
			return;
		}
		if (!MM || CurMap < 0 || ClosedTarget < 0 || OpenTarget < 0)
		{
			UE_LOG(LogAsdaQuestTrack, Warning,
				TEXT("[маршрут-тест] Г пропущена: автопуть=%s, текущая карта=%d, закрытая цель=%d, открытая цель=%d"),
				MM ? TEXT("есть") : TEXT("НЕТ"), CurMap, ClosedTarget, OpenTarget);
			Finish(Fail, Checks);
			return;
		}
		MM->BeginMapTravel(OpenTarget, 100, 100);
		const FString DOpen = MM->DescribeMapTravel();
		Expect(DOpen.Contains(TEXT("ПОРТАЛУ")), FString::Printf(TEXT("Г: заявка на открытую карту %d — шаг не к порталу: %s"), OpenTarget, *DOpen));
		MM->CancelTravelRequest();
		MM->BeginMapTravel(ClosedTarget, 100, 100);
		const FString DClosed = MM->DescribeMapTravel();
		Expect(DClosed.Contains(TEXT("закрыта на сервере")), FString::Printf(TEXT("Г: заявка на закрытую карту %d — нет отказа: %s"), ClosedTarget, *DClosed));
		UE_LOG(LogAsdaQuestTrack, Log, TEXT("[маршрут-тест] Г: с карты %d на открытую %d:\n%s[маршрут-тест] Г: на закрытую %d:\n%s"),
			CurMap, OpenTarget, *DOpen, ClosedTarget, *DClosed);

		// Через 2 с: если павн есть, тик исполнителя обязан снять заявку сам; если нет — исполнитель не тикает.
		const bool bPawn = UGameplayStatics::GetPlayerPawn(W, 0) != nullptr;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(MM,
			[MM, bPawn, ClosedTarget, Fail, Checks, Finish]() mutable
			{
				++Checks;
				if (bPawn && MM->IsTravelRequested())
				{
					++Fail;
					UE_LOG(LogAsdaQuestTrack, Error, TEXT("[маршрут-тест] ПРОВАЛ: Г: заявка на закрытую карту %d не снята тиком"), ClosedTarget);
				}
				else
				{
					UE_LOG(LogAsdaQuestTrack, Log, TEXT("[маршрут-тест] Г: тик исполнителя — %s"),
						bPawn ? TEXT("заявка снята (ok)") : TEXT("павна нет, исполнитель не тикает; решение проверено через DescribeMapTravel"));
				}
				MM->CancelTravelRequest();
				Finish(Fail, Checks);
			}), 2.f, false);
	}));
#endif // ASDA_DEV_CONSOLE

// ★ Часть удержания таблиц квестов (15.09.2026, dt_gc_hold): путь `DT_QuestGuide` объявлен в этом файле.
int32 UAsdaQuestMarkerSubsystem::HoldTrackerTables() const
{
	static const FName Group(TEXT("QuestTracker"));
	const TCHAR* const Paths[] = { kGuideDT };
	return HeldTables.Hold(Group, Paths);
}
