// Copyright (c) 2026 @sitxovski (Discord)
// SAsdaItemBrowser — главная панель просмотрщика предметов (вкладка редактора «Asda Item Browser»).
// ★★★ ПЕРЕДЕЛКА 29.10.2026 (отчёт Reports/item_viewer/REPORT.md):
//   Слева: поиск (имя RU/EN или код) + комбинируемые фильтры — таблица, категория, класс (профессия: точное
//          требование / ветка архетипа / «доступно персонажу» по правилу игры), требуемый уровень от…до, уровень
//          персонажа, грейд, премиум — и группировка дерева (тип / класс / уровень / грейд). Разбивка — игровая
//          (FAsdaItemBrowserData: шапка «ТОЧНАЯ РАЗБИВКА»).
//   Справа: 3D-превью модели (М/Ж, заточка) и КАРТОЧКА: шапка (иконка, имя цветом грейда, EN-имя), игровая подсказка
//          RU и EN — тем же построителем, что в игре (`AsdaItemTipStandalone::BuildCard`), поля записи, возможные опции
//          (itemoptionrange), состав коробки/бустера, в каких коробках лежит, родственные записи (SetCode), описание
//          RU/EN и ассеты. «Профиль зрителя» (класс + уровень) — для правил окраски подсказки.
// Данные и ассеты — только из Content/ (см. AsdaItemBrowserData.h).
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "AsdaItemBrowserData.h"

class SAsdaItemPreviewViewport;
class UTexture2D;
class SSearchBox;
class SVerticalBox;
struct FAsdaItemCard;

class SAsdaItemBrowser : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAsdaItemBrowser) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// ★ Для самопроверки (Debug) и ссылок карточки: выбрать запись по глобальному индексу (поиск = её код).
	void NavigateToIndex(int32 GlobalIndex);
	// Текущий фильтр (самопроверка печатает его состав).
	FAsdaBrowserFilter& MutableFilter() { return Filter; }
	// Перестроить дерево по текущему фильтру.
	void RebuildTree();
	// Число записей, прошедших фильтр.
	int32 GetShownCount() const { return ShownCount; }
	// ★ Для самопроверки: выставить контролы фильтра (индексы пунктов комбобоксов, как выбрал бы человек) и
	//   перестроить дерево. Space 0 все/1/2/3; Cat −1 любая; Job −1 любой; JobMode 0/1/2; Grade −1; Premium −1/0/1;
	//   Grouping 0…3.
	void SetFilterControls(const FString& Search, int32 Space, int32 Cat, int32 Job, int32 JobMode, int32 LvFrom, int32 LvTo,
		int32 CharLevel, int32 Grade, int32 Premium, int32 Grouping);
	// ★ Для самопроверки: вид подсказки (0 авто / 1 полный / 2 простой) и профиль зрителя (профессия, уровень).
	void SetCardControls(int32 CardKind, int32 ViewerJob, int32 InViewerLevel);
	// Раскрыть всё дерево (для кадра самопроверки).
	void ExpandAll(int32 MaxDepth);
	// ★ Для самопроверки: выбрать запись, как кликом в дереве (ветки до неё раскрываются); нет в дереве — только карточка.
	void SelectForTest(int32 GlobalIndex);

private:
	using FNodePtr = TSharedPtr<FAsdaBrowserNode>;
	using FStrPtr = TSharedPtr<FString>;

	// ── дерево ──
	TSharedRef<ITableRow> OnGenerateRow(FNodePtr Node, const TSharedRef<STableViewBase>& Owner);
	void OnGetChildren(FNodePtr Node, TArray<FNodePtr>& OutChildren);
	void OnSelectionChanged(FNodePtr Node, ESelectInfo::Type SelectInfo);

	// ── левая панель: фильтры ──
	TSharedRef<SWidget> BuildFilterPanel();
	// Комбобокс по массиву строк: Index — ссылка на выбранный номер, OnChanged — после смены.
	TSharedRef<SWidget> MakeCombo(TArray<FStrPtr>& Options, int32& Index, TFunction<void()> OnChanged, const FText& Tip);
	// Перенести выбор комбобоксов в Filter и перестроить дерево.
	void ApplyFilterControls();

	// ── иконки (ленивая загрузка атласов + кэш кистей) ──
	const FSlateBrush* GetIconBrush(int32 ItemIndex, int32& OutSize);

	// ── превью и карточка ──
	void RefreshPreview();
	void RebuildCard();
	// Блок игровой подсказки (плашка ITEM_TIPWIN): заголовок цветом грейда + тело с тегами + подвал «элитного».
	TSharedRef<SWidget> MakeTipPanel(const FAsdaItemCard& Card, const FText& Caption);
	// Секция карточки с заголовком.
	void AddSection(const FString& Caption, const TSharedRef<SWidget>& Body);
	// Строки «поле : значение».
	TSharedRef<SWidget> MakeFieldGrid(const TArray<TPair<FString, FString>>& Rows);
	// Список ссылок на записи (код : имя), клик — перейти к записи.
	TSharedRef<SWidget> MakeLinkList(const TArray<TPair<int32, FString>>& IndexAndSuffix, int32 MaxShown = 200);
	// Многострочный текст с выделением (копируется).
	TSharedRef<SWidget> MakeText(const FString& S, const FLinearColor& Color = FLinearColor(0.85f, 0.85f, 0.85f));

	// ── обратный индекс «предмет → коробки/бустеры, где он лежит» (строится лениво) ──
	void EnsureInBoxesIndex();

	// ── контролы ──
	int32 GetEnhance() const;

	TSharedPtr<FAsdaItemBrowserData> Data;
	TArray<FNodePtr> RootNodes;
	TSharedPtr<STreeView<FNodePtr>> TreeView;
	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SAsdaItemPreviewViewport> Viewport;
	TSharedPtr<SVerticalBox> CardBox;

	FAsdaBrowserFilter Filter;
	int32 ShownCount = 0;
	int32 SelectedIndex = INDEX_NONE;      // ГЛОБАЛЬНЫЙ индекс записи (по всем трём таблицам)
	bool bMale = true;
	int32 EnhanceIdx = 0;                  // 0=нет, 1=+5, 2=+6, 3=+7 и выше
	int32 SexIdx = 0;                      // 0 — мужской вариант, 1 — женский

	// Выбор комбобоксов фильтра (индексы в массивах опций).
	int32 SpaceIdx = 0, CatIdx = 0, JobIdx = 0, JobModeIdx = 0, GradeIdx = 0, PremiumIdx = 0, GroupIdx = 0;
	int32 CardKindIdx = 0;                 // 0 авто, 1 полный, 2 простой
	int32 ViewerJobIdx = 0;                // профиль зрителя: профессия (0…44)
	int32 ViewerLevel = 0;                 // профиль зрителя: уровень (0 — не задан)
	TArray<FStrPtr> SpaceOptions, CatOptions, JobOptions, JobModeOptions, GradeOptions, PremiumOptions, GroupOptions;
	TArray<FStrPtr> CardKindOptions, ViewerJobOptions, EnhanceOptions, SexOptions;

	FString AssetsText;                    // текст секции «ассеты» (из RefreshPreview)
	// Почему во вьюпорте пусто («» — модель есть, показывать нечего). Печатается поверх вьюпорта,
	// чтобы отсутствие модели у самоцвета/спец-предмета не выглядело поломкой.
	FString NoMeshNote;

	TMultiMap<int32, int32> InBoxes;       // код содержимого → код коробки/бустера
	bool bInBoxesBuilt = false;

	// Кэш атласов и кистей иконок.
	TMap<FString, TStrongObjectPtr<UTexture2D>> AtlasCache;   // путь → текстура (держим от GC)
	TMap<FString, TSharedPtr<FSlateBrush>> IconBrushes;       // "атлас|ячейка|размер" → кисть
	TSet<FString> BadAtlases;                                 // атласы, где ячейка вне текстуры (напр. s_fish_l_00)
};
