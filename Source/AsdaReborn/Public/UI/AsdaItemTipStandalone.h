// Copyright (c) 2026 @sitxovski (Discord)
// AsdaItemTipStandalone.h — ★ [ПОРТ] 29.10.2026 (просмотрщик предметов редактора, отчёт Reports/item_viewer/REPORT.md).
//
// ЗАЧЕМ. Карточка предмета в редакторском просмотрщике (плагин AsdaItemBrowser) обязана показывать ровно то, что
//   показывает игровая подсказка, — тем же кодом, а не своей копией правил (закон `port-shared-points`). Построители
//   подсказки (`UAsdaItemTooltipSubsystem::BuildTooltip*`) — чистые функции над таблицами item-БД и текстом, но
//   берут их через GameInstance (`Items()` / `Text()` / `LocRaw`), которого в редакторе вне PIE нет.
//
// ЧТО ЗДЕСЬ. «Отдельный комплект» подсистем без игры: служебный GameInstance-владелец (НЕ инициализируется —
//   ни одна игровая подсистема не создаётся, сеть не поднимается), таблицы предметов (`UAsdaItemTableSubsystem`:
//   те же DataTable-ассеты, что в игре), текст (`UAsdaTextSubsystem`) и сама подсистема подсказки. Подсистема
//   подсказки, распознав себя как экземпляр комплекта (`IsStandaloneTip`), берёт таблицы/текст отсюда, а уровень
//   и класс игрока — из «профиля зрителя» (`SetViewer`), который выбирают в просмотрщике.
//   ★ Игровые экземпляры не затрагиваются НИКАК: подмена срабатывает только для объекта, созданного `Acquire`.
//
// ЯЗЫК. `BuildCard(…, Lang)` собирает подсказку на заданном языке тем же построителем: на время вызова язык
//   данных FAsdaLang подменяется (`FAsdaLangScope`, только игровой поток, только на время сборки).
#pragma once
#include "CoreMinimal.h"
#include "UI/AsdaItemTooltipSubsystem.h"   // FAsdaItemTooltipText
#include "UI/AsdaTipPanel.h"               // FAsdaTipMetrics
#include "Text/AsdaLang.h"                 // EAsdaLang

class UAsdaItemTableSubsystem;
class UAsdaTextSubsystem;

// Каким построителем собирать карточку (эталон гоняет подсказку предмета РАЗНЫМИ путями — см. шапку
//   `UAsdaItemTooltipSubsystem::ShowItemTooltip`).
enum class EAsdaItemCardKind : uint8
{
	Auto,     // правило витрины `cItemBar_BuildSlotTooltip@0xB6F3C0`: полный у брони/щита/оружия/премиума, иначе простой
	Full,     // полный `ItemTooltip_Build@0xEA5260` (вкладка «Снаряжение»; у спец-предмета — ветка второй таблицы)
	Simple,   // простой `CItem_BuildSowelTooltipText@0xEA2EC0` (вкладка «Расходники», надетые 5–7/10–11)
};

// Готовая карточка: текст подсказки (заголовок + тело с тегами <FC=…>) + метрики плашки + каким путём собрана.
struct ASDAREBORN_API FAsdaItemCard
{
	FAsdaItemTooltipText Tip;     // заголовок/цвет/тело — 1:1 с тем, что получает плашка ITEM_TIPWIN
	FAsdaTipMetrics      M;       // ширина/подвал «элитного» предмета (5005/5006, нотис 109832)
	FString Pipeline;             // имя эталонного построителя (для подписи в карточке)
	bool bValid = false;
};

namespace AsdaItemTipStandalone
{
	// Создать комплект (повторный вызов возвращает тот же). Только для редакторских инструментов: в игре не зовётся.
	//   nullptr — таблицы item-БД не загрузились.
	ASDAREBORN_API UAsdaItemTooltipSubsystem* Acquire();
	// Выбросить комплект (перечитать таблицы после переимпорта — следующий `Acquire` загрузит их заново).
	ASDAREBORN_API void Release();
	// Комплект создан.
	ASDAREBORN_API bool IsAcquired();

	// Объект подсказки — экземпляр комплекта (а не игровой). Его спрашивают точки данных подсистемы подсказки.
	ASDAREBORN_API bool IsStandaloneTip(const UObject* Obj);
	// Таблицы / текст комплекта (nullptr — комплекта нет).
	ASDAREBORN_API UAsdaItemTableSubsystem* Items();
	ASDAREBORN_API UAsdaTextSubsystem* Text();

	// «Профиль зрителя» — уровень, класс (`gap_438[0]`, ступень 0…44) и подкласс персонажа, ДЛЯ КОТОРОГО строится
	//   подсказка: от них эталон красит «Профессия»/«Уровень» красным и считает «Уровень возможностей». 0 = не задан.
	ASDAREBORN_API void SetViewer(int32 Level, int32 ClassId, int32 SubJob);
	ASDAREBORN_API int32 ViewerLevel();
	ASDAREBORN_API int32 ViewerClass();
	ASDAREBORN_API int32 ViewerSubJob();

	// Собрать карточку предмета по коду (любая из трёх таблиц) на языке `Lang`. Синтетическая запись слота —
	//   эталонная запись предпросмотра (`UAsdaItemTooltipSubsystem::BuildPreviewSlotRecord`, порт
	//   `uiInventoryShowSlotTooltip@0xB87200`: код, прочность-максимум, вес, флаг +50).
	ASDAREBORN_API bool BuildCard(int32 ItemCode, EAsdaLang Lang, EAsdaItemCardKind Kind, FAsdaItemCard& Out);
	// Строка GameText на языке `Lang` с подставленными маркерами (как `LocRaw` подсказки). Пусто — строки нет.
	ASDAREBORN_API FString Loc(int32 Id, EAsdaLang Lang);
}

// ★ Подмена языка данных FAsdaLang на время области (только игровой поток). Вложенные области восстанавливают
//   прежнее значение. Используется сборкой карточки RU/EN — игровой язык сессии вне области не меняется.
struct ASDAREBORN_API FAsdaLangScope
{
	explicit FAsdaLangScope(EAsdaLang L);
	~FAsdaLangScope();
private:
	int32 Prev = -1;   // прежняя подмена (−1 — не было)
};
