// Copyright (c) 2026 @sitxovski (Discord)
// SAstoryAchievementsEditor — вкладка «Редактор достижений Asda» (звания, Fame).
//
// Композиция (три зоны, разделители тянутся мышью, пропорции запоминаются между запусками):
//   ┌─ панель инструментов: сохранить · отменить · перечитать · экспорт · выгрузка для сервера ─┐
//   ├─ сводка одной строкой: всего · расхождений · недостижимых · с ошибками (числа кликабельны) ┤
//   │ фильтры и поиск │        таблица званий          │      карточка выбранной записи         │
//   │   (слева)       │  (+ панель проблем снизу)      │   (поля по смыслу + сервер)            │
//   └──────────────────────────────────────────────────────────────────────────────────────────┘
//
// Внешний вид строится ТОЛЬКО на палитре редактора (FStyleColors / FAppStyle) — своего скина нет,
// поэтому инструмент одинаково читается в тёмной и светлой теме UE.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/SHeaderRow.h"

#include "AstoryTitleData.h"

class SDockTab;
class SSearchBox;
class SScrollBox;
class SSplitter;
template <typename T> class SComboBox;

// Строка таблицы: индекс записи + предпосчитанные состояния (чтобы не считать их на каждый кадр).
struct FAstoryRowView
{
	int32 RecordIndex = INDEX_NONE;
	bool bUnreachable = false;      // id ≥ 256 — недостижимо
	bool bMismatch = false;         // расхождение с сервером
	bool bNoServerRow = false;      // серверной строки нет
	bool bDisabled = false;         // Enabled == 0
	bool bTruncDesc = false;        // описание обрезано в самих данных
	bool bError = false;            // блокирующая ошибка валидации
	FString StatusTip;              // подсказка над значками
};
using FAstoryRowPtr = TSharedPtr<FAstoryRowView>;

class SAstoryAchievementsEditor : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAstoryAchievementsEditor) {}
		SLATE_ARGUMENT(TSharedPtr<SDockTab>, OwnerTab)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SAstoryAchievementsEditor();

	// ★ Подтверждение при закрытии вкладки с несохранёнными правками.
	bool CanCloseTab();

	// Горячие клавиши (Ctrl+S сохранить, Ctrl+F поиск, Ctrl+Z отменить).
	virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

private:
	// ── построение зон ──
	TSharedRef<SWidget> BuildToolbar();
	TSharedRef<SWidget> BuildSummaryBar();
	TSharedRef<SWidget> BuildFilterPanel();
	TSharedRef<SWidget> BuildTable();
	TSharedRef<SWidget> BuildIssuesPanel();
	TSharedRef<SWidget> BuildCard();

	// ── таблица ──
	void RebuildHeader();
	TSharedRef<ITableRow> OnGenerateRow(FAstoryRowPtr Item, const TSharedRef<STableViewBase>& Owner);
	void OnSelectionChanged(FAstoryRowPtr Item, ESelectInfo::Type SelectInfo);
	EColumnSortMode::Type GetSortMode(FName ColumnId) const;
	void OnSortColumn(EColumnSortPriority::Type Priority, const FName& ColumnId, EColumnSortMode::Type Mode);

	// ── данные и фильтрация ──
	void ReloadAll();
	void RefreshView();                 // пересобрать список по фильтрам + пересчитать сводку
	bool PassesFilter(const FAstoryTitleRecord& Rec, const FAstoryRowView& View) const;
	// Есть ли выбранная запись — по этому признаку карточка показывает поля или заглушку.
	EVisibility CardVisibility() const;
	EVisibility NoSelectionVisibility() const;
	FAstoryTitleRecord* SelectedRecord();
	const FAstoryTitleRecord* SelectedRecord() const;

	// ── правки ──
	void PushUndo();                    // снимок ПЕРЕД изменением
	void ApplyUndo();
	void MarkDirty();
	void SetFieldOnSelection(int32 Offset, int32 Value);     // массовая правка целого поля

	// ── действия ──
	FReply OnSaveClicked();
	FReply OnReloadClicked();
	FReply OnUndoClicked();
	FReply OnExportSelectionClicked();
	FReply OnServerExportClicked();
	FReply OnBulkEditClicked();
	FReply OnAddRecordClicked();
	FReply OnDeleteRecordClicked();
	FReply OnRoundTripTestClicked();

	// ── подписи ──
	FText EnumLabel(EAstoryFieldEnum Enum, int32 Value) const;
	TSharedRef<SWidget> BuildFieldEditor(const FAstoryTitleFieldDef& Field);
	TSharedRef<SWidget> BuildCardSection(const FText& Title, EAstoryFieldGroup Group);
	// Серверная секция карточки перестраивается при смене выделения (число строк переменное).
	void RefreshCard();

	// ── состояние между запусками ──
	void LoadUiState();
	void SaveUiState();

	// ── данные ──
	TSharedPtr<FAstoryTitleData> Data;
	// ★ Строки СТАБИЛЬНЫ: один и тот же объект живёт всё время, пока запись существует.
	//   Если пересоздавать их на каждом пересчёте, SListView теряет выделение — и карточка
	//   очищалась бы после каждой правки поля.
	TArray<FAstoryRowPtr> AllRows;              // по одной на запись, в порядке записей
	TArray<FAstoryRowPtr> VisibleRows;          // после фильтра и сортировки
	TArray<FAstoryIssue> Issues;
	TArray<TSharedPtr<FAstoryIssue>> VisibleIssues;

	// ── виджеты ──
	TWeakPtr<SDockTab> OwnerTab;
	TSharedPtr<SListView<FAstoryRowPtr>> ListView;
	TSharedPtr<SHeaderRow> HeaderRow;
	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SScrollBox> CardBox;
	// Серверная секция карточки — единственная часть с переменным числом строк.
	TSharedPtr<SVerticalBox> ServerBox;
	TSharedPtr<SSplitter> MainSplitter;
	TSharedPtr<SListView<TSharedPtr<FAstoryIssue>>> IssuesList;

	// ── фильтры ──
	FString SearchText;
	int32 FilterCategory = -1;      // -1 = все
	int32 FilterGroup = -1;
	int32 FilterClass = -1;
	int32 FilterGrade = -1;
	int32 FilterEnabled = -1;       // -1 все · 0 только выключенные · 1 только включённые
	bool bOnlyMismatch = false;
	bool bOnlyUnreachable = false;
	bool bOnlyErrors = false;
	bool bOnlyNoServer = false;
	bool bOnlyTruncDesc = false;
	bool bShowAllColumns = false;

	// ── пропорции разделителей (запоминаются между запусками) ──
	float SplitFilters = 0.17f;     // левая зона: фильтры и поиск
	float SplitCenter = 0.55f;      // центр: таблица + панель проблем
	float SplitCard = 0.28f;        // правая зона: карточка записи
	float SplitTable = 0.74f;       // внутри центра: таблица
	float SplitIssues = 0.26f;      // внутри центра: панель проблем

	// ── сортировка ──
	FName SortColumn = TEXT("Id");
	EColumnSortMode::Type SortMode = EColumnSortMode::Ascending;

	// ── состояние правок ──
	bool bDirty = false;
	TArray<TArray<FAstoryTitleRecord>> UndoStack;
	int32 SelectedRecordIndex = INDEX_NONE;

	// ── сводка ──
	int32 CountTotal = 0, CountMismatch = 0, CountUnreachable = 0, CountErrors = 0, CountNoServer = 0;
	int32 CountTruncDesc = 0;
	// Разбивка расхождений по полям («Очки славы — 42, Показывать в списке — 98») — в подсказку.
	FString MismatchBreakdown;

	// Текст последней операции (сохранение/самопроверка) — показывается под панелью инструментов.
	FString StatusLine;
};
