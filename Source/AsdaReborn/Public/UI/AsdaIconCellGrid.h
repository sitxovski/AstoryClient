// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaIconCellGrid — переиспользуемый контроллер СЕТКИ ИКОНОК предметов поверх JUI-ячеек.
// Обобщение механического примитива, общего для char-create (выбор частей), инвентаря, магазина, склада,
// дерева скиллов (в оригинале — общие storage-функции: storageScrollTabFields/uiArrangeSkillSlots5).
// Делает РОВНО то, что повторяется во всех этих экранах:
//   1) кладёт прозрачные КЛИК-КНОПКИ поверх JUI Image-ячеек (сам Image не кликабелен);
//   2) РЕНДЕРИТ иконку каждой ячейки (itemCode → имя иконки item-таблицы → текстура через менеджер);
//   3) МАРШРУТИЗИРУЕТ клик → колбэк с индексом ячейки.
// Модель данных «что в ячейке» НЕ зашита сюда (у char-create — карусель категорий, у инвентаря — плоский
// список стаков): её задаёт владелец через CodeProvider. Так система не выдумывает семантику инвентаря
// (never-invent) и одинаково служит всем экранам. Layout ячеек — родной JUI, не выдуманный (no-invented-ui).
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UI/AsdaUiHelpers.h"   // AsdaUi::IconSexSelf — пол атласа иконок (08.11.2026)
#include "AsdaIconCellGrid.generated.h"

class UUserWidget;
class UWidget;

UCLASS()
class ASDAREBORN_API UAsdaIconCellGrid : public UObject
{
	GENERATED_BODY()

	// ★ 06.11.2026: живой аудит `asda.UI.ZoneOverlapAudit` читает окно и ячейки сетки.
	friend struct FAsdaZoneAuditAccess;
public:
	// Инициализация над JUI-ячейками с именами InCellPrefix+"NN" (0..InNumCells-1). Один раз строит клик-оверлей.
	//   InCodeProvider(cellIdx) → itemCode ячейки (0 = пусто). InOnClick(cellIdx) → обработчик клика владельца.
	//   bMale — пол для выбора иконки (male/female варианты). Повторный Setup обновляет провайдеры/пол, оверлей не дублирует.
	//   ★★★ [ОРИГИНАЛ] 08.11.2026: true = атлас ПОЛА СВОЕГО ПЕРСОНАЖА (эталон рисует сетки окон через
	//   `g_ItemImgOptionVariant`, см. `UAsdaItemTableSubsystem::GetItemIconSelf`), false = женский. Явный пол
	//   другого персонажа (окно осмотра) — `SetMale` после Setup. Ключ отката — `asda.Item.IconSelfGender 0`.
	void Setup(UUserWidget* InOwner, const FString& InCellPrefix, int32 InNumCells, bool bMale,
		TFunction<int32(int32)> InCodeProvider, TFunction<void(int32)> InOnClick);

	// ★ ВТОРОЙ вход — ЯВНЫЙ СПИСОК ИМЁН ячеек (порядок списка = порядок ячеек).
	//   ЗАЧЕМ. Схема `префикс+NN` работает только там, где у JUI-ячейки есть `idc`. У сетки призов
	//   премиум-гачи (окно 68300 `Premium_Pick_View_Win`, ячейки IDNum 68311…68335) поле `ID` в
	//   `gamemain_jui.ini` ПУСТОЕ, поэтому импортёр назвал их по скину с idx-суффиксом
	//   (`NoImage_icon_16_0` … `NoImage_icon_68_0`) — закон `jui-importer-widget-naming`.
	//   Имена сняты дампом реального WBP (`Tools/asda_ui_dump_window.py`), не угаданы.
	//   Всё остальное (клик-оверлей, рендер иконки, маршрутизация) — общее, поэтому это ТОЧКА ВХОДА,
	//   а не вторая копия сетки (закон `port-shared-points`).
	void SetupNamed(UUserWidget* InOwner, const TArray<FName>& InCellNames, bool bMale,
		TFunction<int32(int32)> InCodeProvider, TFunction<void(int32)> InOnClick);

	// Проставить иконки всех ячеек по текущему провайдеру (code→иконка). Вызывать после смены данных/скролла/пола.
	void Refresh();

	// Сменить пол иконок (male/female атлас) без пересборки оверлея. Пол ЯВНЫЙ (true = мужской, false = женский).
	void SetMale(bool bMale) { IconSex = bMale ? 1 : 0; }

	// ★★★ РАЗМЕР ЯЧЕЙКИ В ПИКСЕЛЯХ. Раньше был жёстко зашит как 64 — по размеру `ICONBOX`
	//   инвентаря/витрины (`Size = "64 64"`). Но у элитного почтового ящика (окно 69000) ячейки
	//   32×32: `.JUI` даёт 69011…69077 `Size = "32 32"`, и эталон рисует их
	//   `CControl_DrawTextureRect(ctrl, tex, 0, 0, 32, 32)` (`uiInitMixWindowGrid@0xEEFE60`,
	//   `gridWindowPopulate7x7@0xEF1DE0`), а шаг между иконками там 33 px — с клик-зоной 64×64
	//   соседние ячейки и соседние СТРОКИ перекрывались бы.
	//   ⇒ размер стал параметром общей точки, а не её константой (закон `port-shared-points`:
	//   расширяем общую точку, а не заводим вторую копию сетки).
	//   Ставить ДО `Setup`/`SetupNamed` — оверлей строится их размером.
	void SetCellSizePx(float Px) { SetCellSizePx(Px, Px); }

	// ★ ПРЯМОУГОЛЬНАЯ клик-зона. Расширение той же общей точки (не вторая сетка): у попапа наград
	//   квеста (окно 540) «ячейка» — это СТРОКА награды `EPISODEPOP_ITEMBACK06…11`, и в `.JUI` она
	//   объявлена `Size = "160 42"`. Квадратной зоной 42×42 половина строки перестала бы нажиматься,
	//   а зоной 160×160 строки перекрыли бы друг друга (шаг между ними 50 px).
	void SetCellSizePx(float W, float H)
	{
		CellSizePx = FMath::Max(1.f, W); CellSizeYPx = FMath::Max(1.f, H);
		// Совместимость: пока владелец не задал размер ИКОНКИ отдельно, он равен размеру ячейки —
		//   ровно как было до 2026-08-28 у всех окон порта.
		if (IconSizePx <= 0.f) { IconSizePx = CellSizePx; IconSizeYPx = CellSizeYPx; }
	}

	/** ★★★ РАЗМЕР САМОЙ ИКОНКИ, ОТДЕЛЬНО ОТ КЛИК-ЗОНЫ (заведён 2026-08-28).
	 *
	 *  ДЕФЕКТ, который это лечит. `Refresh()` растягивал слот ячейки до `CellSizePx × CellSizePx`
	 *  («плейсхолдер `Log_NoImage_Icon` 10×10 → иконка во всю ячейку 64×64»). Но у СПИСКОВ
	 *  клик-зона занимает ВСЮ СТРОКУ (аукцион: 537×32 «мои лоты», 722×32 выдача — §14.3), и та же
	 *  величина уезжала в размер картинки: иконка предмета в строке выдачи становилась **722×722**
	 *  и накрывала окно целиком (живой дамп 2026-08-28: `EXCHANGER_ITEM01_334_0 … слот(21,0
	 *  722x722)`, жалоба владельца «зелье размером с окно поверх всего списка»).
	 *  ★ Второй дефект той же строки: высота бралась из `CellSizePx`, а не `CellSizeYPx`, поэтому
	 *  НЕквадратная ячейка получала квадратную картинку. Здесь исправлено.
	 *  Звать ПОСЛЕ `SetCellSizePx`. */
	void SetIconSizePx(float W, float H) { IconSizePx = FMath::Max(1.f, W); IconSizeYPx = FMath::Max(1.f, H); }

	// ★ Прогнать клик по ячейке ТЕМ ЖЕ путём, что и прозрачная кнопка-оверлей (та зовёт ровно этот
	//   колбэк владельца). Нужен живой проверке из консоли: мышью в headless-PIE не кликнуть, а
	//   заводить ВТОРОЙ путь обработки клика ради теста нельзя.
	void SimulateClick(int32 Cell) { if (OnClick && Cell >= 0 && Cell < NumCells) { OnClick(Cell); } }

	// Число ячеек и виджет ячейки — для диагностики владельца (дампы, хит-тесты).
	int32 GetNumCells() const { return NumCells; }

	// Найти виджет по префиксу имени (JUI-имена могут нести idx-суффикс). Общий хелпер (SEL-маркеры и др.).
	static UWidget* FindCell(UUserWidget* W, const FString& Prefix);

private:
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Owner = nullptr;
	// Реле прозрачных клик-кнопок (GC-рут; живут пока жив контроллер).
	UPROPERTY(Transient) TArray<TObjectPtr<class UAsdaButtonRelay>> CellRelays;
	// ★ Сами прозрачные клик-кнопки — держим, чтобы можно было ПОКАЗАТЬ их фактическое положение
	//   (перекрытия клик-зон иначе не видны: иконка на месте, а зона может быть где угодно).
	UPROPERTY(Transient) TArray<TObjectPtr<class UButton>> CellHitButtons;

public:
	// Отключить кнопки-оверлеи (клик владелец ловит сам). Звать ДО Setup/SetupNamed.
	void SetClickOverlayEnabled(bool bEnabled) { bWantClickOverlay = bEnabled; }

	/** ★ НАВЕДЕНИЕ НА ЯЧЕЙКУ — общая точка (заведена 2026-08-28 для тултипов аукциона).
	 *  Клик-зоны сетки уже `UButton` с реле `UAsdaButtonRelay`, у которого есть Hover/Unhover;
	 *  раньше сетка их просто не подключала, поэтому НИ ОДНО окно на её ячейках не могло показать
	 *  подсказку без собственной копии оверлея. Ставить ДО `Setup*` (зоны строятся в нём). */
	void SetHoverHandlers(TFunction<void(int32)> InOnHover, TFunction<void()> InOnUnhover)
	{
		OnHover = MoveTemp(InOnHover);
		OnUnhover = MoveTemp(InOnUnhover);
	}

	/** ★★★ ЖИВОЙ ХИТ-ТЕСТ: НОМЕР ЯЧЕЙКИ ПОД КУРСОРОМ (−1 — курсор не над сеткой).
	 *
	 *  ЗАЧЕМ ЭТО ЗДЕСЬ, А НЕ У ОКНА-ВЛАДЕЛЬЦА (закон `port-shared-points`). Клик по ячейке ловит
	 *  кнопка-оверлей (`OnClicked` = нажатие И отпускание над одной и той же зоной), а ПЕРЕТАСКИВАНИЕ
	 *  так поймать нельзя: у него нажатие и отпускание лежат над РАЗНЫМИ окнами. Владельцу нужен
	 *  покадровый ответ «над какой ячейкой курсор сейчас», и единственный честный источник —
	 *  ЖИВАЯ геометрия самой клик-зоны (закон `jui-cell-chain-nesting`: считать по слоту нельзя,
	 *  ячейки лежат на третьем уровне вложенности и слот врёт про абсолютное положение).
	 *  Зона — тот же `UButton`, что маршрутизирует клик, поэтому «где кликается» и «где хватается»
	 *  по построению совпадают; своей копии обхода дерева владельцу заводить не нужно.
	 *
	 *  ⚠ Погашенные зоны (`Collapsed`) пропускаются — сетка строит зоны только над живыми ячейками,
	 *  но владелец может гасить их сам (`SetHitZonesVisible`). */
	int32 HitTestCellUnderCursor() const;

	// Строка дампа: где реально лежат клик-зоны ячеек (в корне окна) и какого они размера.
	// ★★★ ОБЩАЯ ТОЧКА: НАРИСОВАТЬ ИКОНКУ ПРЕДМЕТА В ОДНОМ `UImage`.
	//   Тело вынесено из `Refresh` (второй копии не заведено): атлас из `GetItemIcon`, UV-подрегион
	//   ячейки, размер картинки. Нужна там, где иконка ОДНА и сетки нет — например панель покупки
	//   магазина бустеров (контрол 15533 слева от названия товара).
	//   `Code <= 0` — иконка гасится. false, если данных/атласа нет (узел остаётся как был).
	//   `InIconSex`: -1 = свой персонаж (эталон), 0 = женский, 1 = мужской (bool прежних вызовов: false → 0, true → 1).
	static bool ApplyItemIcon(UObject* WorldCtx, class UImage* Box, int32 Code, int32 InIconSex,
		float IconW, float IconH, bool bClearIfEmpty);

	FString DumpHitZones(const TCHAR* Title) const;

	// ЖИВАЯ геометрия клик-зон (Slate, после отрисовки) — первые `Count` штук. Нужна, чтобы
	//   доказать, что зона стоит РОВНО поверх иконки: заданная раскладка слота может быть верной,
	//   а живой прямоугольник — уехавшим, если по дороге есть анкоры (см. `CanvasRootPos`).
	FString DumpHitZonesLive(const TCHAR* Title, int32 Count) const;

	// ★★★ ТЕГ ИМЕНИ КЛИК-ЗОН. Ставить ДО `SetupNamed`.
	//   ЗАЧЕМ (дефект найден 2026-08-27 на аукционе). У `SetupNamed` префикса ячеек нет, поэтому
	//   зоны звались `CELL_HIT00`, `CELL_HIT01`… — ОДИНАКОВО у всех именованных сеток окна.
	//   В окне 13000 таких сеток ПЯТЬ (60 ячеек выставления, 5 строк лота, 8 «моих лотов»,
	//   8 строк выдачи, 40 гнёзд самоцветов): `WidgetTree->ConstructWidget` с уже занятым `FName`
	//   отдаёт имя НОВОМУ виджету, и клик-зоны ранее собранных сеток становились чужими.
	//   Тег делает имя уникальным: `<тег>_HIT<NN>`.
	void SetOverlayTag(const FString& Tag) { OverlayTag = Tag; }

	// ★ Показать/погасить клик-зоны сетки. Зоны живут в КОРНЕВОМ канвасе окна (см. `BuildClickOverlay`),
	//   поэтому гашение панели-родителя их НЕ ЗАДЕВАЕТ: невидимая сетка продолжала бы ловить мышь
	//   поверх соседней панели. Владелец обязан звать это вместе с показом/гашением своей панели.
	void SetHitZonesVisible(bool bVisible);

	/** ★★★ [ИСПРАВЛЕНИЕ] 29.10.2026 ПЕРЕСТАВИТЬ КЛИК-ЗОНЫ НА ТЕКУЩЕЕ МЕСТО ЯЧЕЕК (жалоба игроков «зоны выбора награды
	 *  сдвинуты», окно 540). Зоны строятся ОДИН раз (`BuildClickOverlay`) по абсолютной позиции ячейки в корне на момент
	 *  постройки. Если владелец ПОТОМ двигает панель-родителя ячеек своим кодом (у окна 540 — `RelayoutRewardWindow` ставит
	 *  панель выбора 553 на «541 + Δ» при каждом показе), ячейки уезжают, а зоны остаются на запечённом в WBP месте —
	 *  клик попадает в соседнюю строку или мимо. Владелец, двигающий ячейки, обязан звать это ПОСЛЕ перестановки.
	 *  `bFollowCellVisibility` — зона ловит мышь, только если сама ячейка и вся цепочка её родителей видимы
	 *  (погашенная строка не должна перехватывать клик у кнопки под ней). Ключ отката `asda.UI.CellGridSyncOverlay 0`. */
	void SyncClickOverlay(bool bFollowCellVisibility);

	// Клик-зона ячейки `Cell` (nullptr — зоны нет) — живая проверка «зона стоит поверх ячейки» в самотестах.
	class UButton* GetHitButton(int32 Cell) const;

	FString CellPrefix;
	// Тег имени клик-зон (см. SetOverlayTag). Пусто ⇒ прежняя схема `CELL` / `CellPrefix`.
	FString OverlayTag;
	// Явные имена ячеек (режим `SetupNamed`). Пусто ⇒ работает схема `CellPrefix + NN`.
	TArray<FName> CellNames;
	int32 NumCells = 0;
	int32 IconSex = AsdaUi::IconSexSelf;   // ★ 08.11.2026: -1 свой пол (эталон), 0 жен., 1 муж. — см. `AsdaUi::ApplyItemIcon`
	bool bOverlayBuilt = false;
	// ★ Строить ли кнопки-оверлеи. Окна, где ячейки заданы ЦЕПОЧКОЙ ВЛОЖЕННОСТИ (`.JUI`:
	//   `TRAY01` внутри `TRAY00` и т.д.), ловят клик сами — по живой геометрии `IsCursorOverWidget`,
	//   как это делает перетаскивание. Для них оверлей вреден: его зоны съезжают на чужие панели.
	bool bWantClickOverlay = true;
	// Гасить ячейку, для которой провайдер вернул 0 (нужно страничным сеткам — см. Refresh).
	bool bClearEmptyCells = false;
	// Размер ячейки в пикселях (см. SetCellSizePx). 64 — историческое значение ICONBOX инвентаря.
	float CellSizePx = 64.f;
	// Высота клик-зоны; по умолчанию равна ширине (квадрат — как было у всех прежних владельцев).
	float CellSizeYPx = 64.f;
	// Размер ИКОНКИ (см. SetIconSizePx). 0 = «не задан» ⇒ берётся размер ячейки (старое поведение).
	float IconSizePx = 0.f;
	float IconSizeYPx = 0.f;

	TFunction<int32(int32)> CodeProvider;
	TFunction<void(int32)> OnClick;
	TFunction<void(int32)> OnHover;
	TFunction<void()>      OnUnhover;

	// Создать прозрачные клик-кнопки поверх каждой JUI-ячейки (копирует анкоры/оффсеты ячейки, z=50).
	void BuildClickOverlay();

	// Виджет ячейки №Cell: явное имя (`SetupNamed`) либо `CellPrefix + NN` (`Setup`).
	UWidget* CellWidget(int32 Cell) const;
};
