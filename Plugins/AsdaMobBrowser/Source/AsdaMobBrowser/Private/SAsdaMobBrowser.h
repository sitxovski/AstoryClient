// Copyright (c) 2026 @sitxovski (Discord)
// SAsdaMobBrowser — вкладка «Asda Mob Browser» (Tools ▸ Asda): обзор всех NPC и мобов игры.
//
//   ┌ поиск · язык RU/EN · Reload · источники ──────────────────────────────────────┐
//   │ ▸ NPC (N)                │ анимация ▾ · пауза · пол · вид по умолчанию       │
//   │ ▾ Mobs (N)               │ 3D-превью (ЛКМ — поворот, ПКМ — наклон, колесо)    │
//   │   Name | Lvl | ID        ├────────────────────────────────────────────────────┤
//   │   …                      │ характеристики (нет значения — «-»)                 │
//   └──────────────────────────┴────────────────────────────────────────────────────┘
// Группы — аккордеон: раскрыта одна, в списке только её записи; сортировка (имя/уровень/ID) — своя у каждой.
// Таблицы перечитываются при каждом открытии окна. Только чтение: ни ассеты, ни таблицы не меняются.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Input/SComboBox.h"
#include "AsdaMobBrowserData.h"
#include "UObject/StrongObjectPtr.h"

class SAsdaMobPreviewViewport;
class SSearchBox;
class SVerticalBox;

class SAsdaMobBrowser : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAsdaMobBrowser) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Имена колонок списка.
	static const FName ColName;
	static const FName ColLevel;
	static const FName ColId;

	// ── Управление (и для самопроверки — тот же путь, что щелчки пользователя) ──
	void ExpandGroup(EAsdaBrowserKind Kind);              // щелчок по заголовку группы (раскрыть её, свернуть другую)
	bool IsGroupExpanded(EAsdaBrowserKind Kind) const;
	bool SelectEntry(EAsdaBrowserKind Kind, int32 Id);    // выбор строки (раскрывает её группу)
	void SetSort(EAsdaBrowserKind Kind, FName Column, EColumnSortMode::Type Mode); // щелчок по заголовку колонки
	void SetSearchText(const FString& Text);              // ввод в поиск
	void SetEnglish(bool bEnglish);                       // язык имён
	bool SelectClipIndex(int32 ClipIndex);                // выбор клипа в выпадающем списке
	TArray<int32> GetShownIds(EAsdaBrowserKind Kind) const; // ID строк группы в порядке показа
	TSharedPtr<FAsdaMobEntry> GetSelected() const { return Selected; }
	TSharedPtr<SAsdaMobPreviewViewport> GetViewport() const { return Viewport; }
	const FAsdaMobModelAssets& GetCurrentAssets() const { return CurAssets; }
	FString DescribeStatsForTest() const;                 // текст панели характеристик
	FReply OnReload();                                    // кнопка Reload

private:
	// Группа списка.
	struct FGroup
	{
		EAsdaBrowserKind Kind = EAsdaBrowserKind::Mob;
		TArray<TSharedPtr<FAsdaMobEntry>> Shown;          // строки в порядке показа
		TSharedPtr<SListView<TSharedPtr<FAsdaMobEntry>>> List;
		FName SortColumn;                                 // по умолчанию — имя (алфавит)
		EColumnSortMode::Type SortMode = EColumnSortMode::Ascending;
	};
	FGroup& GroupOf(EAsdaBrowserKind K) { return K == EAsdaBrowserKind::Npc ? Groups[0] : Groups[1]; }
	const FGroup& GroupOf(EAsdaBrowserKind K) const { return K == EAsdaBrowserKind::Npc ? Groups[0] : Groups[1]; }
	TSharedRef<SWidget> MakeGroupWidget(EAsdaBrowserKind Kind);   // заголовок + список группы

	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FAsdaMobEntry> Item, const TSharedRef<STableViewBase>& Owner);
	void OnSelectionChanged(TSharedPtr<FAsdaMobEntry> Item, ESelectInfo::Type Info);
	EColumnSortMode::Type GetSortMode(EAsdaBrowserKind Kind, FName Column) const;
	void RebuildShown(FGroup& G);                         // фильтр + сортировка → G.Shown
	void RebuildAll();
	void SelectFirstShown();                              // первая строка раскрытой группы
	void OnSearchChanged(const FText& Text);

	TSharedRef<SWidget> MakeClipItem(TSharedPtr<FAsdaMobClip> Clip) const;
	void OnClipSelected(TSharedPtr<FAsdaMobClip> Clip, ESelectInfo::Type Info);
	FText GetClipComboText() const;
	FText GetPreviewInfoText() const;

	void RebuildStats();
	FText GetSourceText() const;

	TSharedRef<FAsdaMobBrowserData> Data = FAsdaMobBrowserData::Get();
	FGroup Groups[2];                                     // [0] NPC, [1] Mobs
	EAsdaBrowserKind Expanded = EAsdaBrowserKind::Mob;    // раскрытая группа
	bool bAnyExpanded = true;                             // false — обе свёрнуты
	TSharedPtr<SSearchBox> Search;
	FString SearchText;
	bool bEnglish = false;                                // язык имён (по умолчанию — язык редактора)

	TSharedPtr<FAsdaMobEntry> Selected;
	FAsdaMobModelAssets CurAssets;                        // ассеты выбранной записи
	TArray<TStrongObjectPtr<UObject>> CurKeep;            // сильные ссылки на ассеты выбранной записи (меш, материалы)
	TArray<TSharedPtr<FAsdaMobClip>> ClipOptions;         // клипы выбранной записи
	TSharedPtr<FAsdaMobClip> CurClip;
	TSharedPtr<SComboBox<TSharedPtr<FAsdaMobClip>>> ClipCombo;
	TSharedPtr<SAsdaMobPreviewViewport> Viewport;
	TSharedPtr<SVerticalBox> StatsBox;
	TArray<FString> StatsLinesForTest;                    // те же строки, что на панели
};
