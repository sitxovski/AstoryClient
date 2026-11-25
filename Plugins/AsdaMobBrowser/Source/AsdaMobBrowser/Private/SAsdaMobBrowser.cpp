// Copyright (c) 2026 @sitxovski (Discord)
// Реализация вкладки обозревателя — см. SAsdaMobBrowser.h.
#include "SAsdaMobBrowser.h"

#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Views/STableRow.h"
#include "Styling/AppStyle.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

#include "Resources/AsdaModelAnimSubsystem.h"   // LoadClipChecked — загрузка клипа как у игры
#include "SAsdaMobPreviewViewport.h"

#define LOCTEXT_NAMESPACE "SAsdaMobBrowser"

const FName SAsdaMobBrowser::ColName(TEXT("Name"));
const FName SAsdaMobBrowser::ColLevel(TEXT("Lvl"));
const FName SAsdaMobBrowser::ColId(TEXT("ID"));

namespace
{
	const FLinearColor kSectionColor(0.55f, 0.78f, 1.f);    // заголовок раздела
	const FLinearColor kDimColor(0.6f, 0.6f, 0.6f);         // пояснения
	const TCHAR* const kDash = TEXT("-");                   // нет значения (решение владельца 09.11.2026)

	// Число; отрицательное (−1 у сервера = «не используется», INDEX_NONE у нас = «нет») → «-».
	FString StatNum(int32 V) { return V < 0 ? FString(kDash) : FString::FormatAsNumber(V); }
	FString StatRange(int32 A, int32 B, const TCHAR* Suffix = TEXT(""))
	{
		if (A < 0 || B < 0) { return kDash; }
		return A == B ? FString::Printf(TEXT("%s%s"), *StatNum(A), Suffix) : FString::Printf(TEXT("%s – %s%s"), *StatNum(A), *StatNum(B), Suffix);
	}
	FString StatPct(int32 V) { return V < 0 ? FString(kDash) : FString::Printf(TEXT("%d%%"), V); }
	FString OrDash(const FString& S) { return S.IsEmpty() ? FString(kDash) : S; }
}

// ════════════════════════════════════════════════════════════════════════════
//  Строка списка
// ════════════════════════════════════════════════════════════════════════════
class SAsdaMobRow : public SMultiColumnTableRow<TSharedPtr<FAsdaMobEntry>>
{
public:
	SLATE_BEGIN_ARGS(SAsdaMobRow) {}
		SLATE_ARGUMENT(TSharedPtr<FAsdaMobEntry>, Item)
		SLATE_ARGUMENT(bool, bEnglish)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& Owner)
	{
		Item = InArgs._Item;
		bEnglish = InArgs._bEnglish;
		SMultiColumnTableRow<TSharedPtr<FAsdaMobEntry>>::Construct(FSuperRowType::FArguments().Padding(FMargin(0.f, 1.f)), Owner);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& Column) override
	{
		FString S;
		if (Column == SAsdaMobBrowser::ColName) { S = Item->DisplayName(bEnglish); }
		else if (Column == SAsdaMobBrowser::ColLevel) { S = StatNum(Item->DisplayLevel()); }
		else if (Column == SAsdaMobBrowser::ColId) { S = FString::FromInt(Item->Id); }
		const bool bNum = Column != SAsdaMobBrowser::ColName;
		return SNew(SBox).Padding(FMargin(4.f, 0.f))
			.HAlign(bNum ? HAlign_Right : HAlign_Left)
			[
				SNew(STextBlock).Text(FText::FromString(S))
				.ToolTipText(FText::FromString(FString::Printf(TEXT("%s / %s · ID %d · model %s"),
					*OrDash(Item->NameRu), *OrDash(Item->NameEn), Item->Id, *OrDash(Item->Model))))
			];
	}

private:
	TSharedPtr<FAsdaMobEntry> Item;
	bool bEnglish = false;
};

// ════════════════════════════════════════════════════════════════════════════
//  Окно
// ════════════════════════════════════════════════════════════════════════════
void SAsdaMobBrowser::Construct(const FArguments& InArgs)
{
	// Язык имён — язык редактора (русская культура → RU, иначе EN); переключатель — в верхней строке.
	bEnglish = FInternationalization::Get().GetCurrentCulture()->GetTwoLetterISOLanguageName() != TEXT("ru");
	// ★ Каждое открытие окна — свежие игровые таблицы (добавили запись в DataTable — она уже здесь).
	Data->Reload();
	Groups[0].Kind = EAsdaBrowserKind::Npc;
	Groups[1].Kind = EAsdaBrowserKind::Mob;
	Groups[0].SortColumn = Groups[1].SortColumn = ColName;
	// Заголовки групп создают и списки — ДО сборки дерева (порядок вычисления операндов «+» в C++ не задан).
	const TSharedRef<SWidget> NpcHeader = MakeGroupWidget(EAsdaBrowserKind::Npc);
	const TSharedRef<SWidget> MobHeader = MakeGroupWidget(EAsdaBrowserKind::Mob);

	ChildSlot
	[
		SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(4.f)
		[
			SNew(SVerticalBox)
			// ── верхняя строка ──
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(0.28f).Padding(0.f, 0.f, 6.f, 0.f)
				[
					SAssignNew(Search, SSearchBox)
					.HintText(LOCTEXT("SearchHint", "Search name, model or ID"))
					.OnTextChanged(this, &SAsdaMobBrowser::OnSearchChanged)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
				[
					SNew(SSegmentedControl<bool>)
					.Value_Lambda([this]() { return bEnglish; })
					.OnValueChanged_Lambda([this](bool b) { SetEnglish(b); })
					+ SSegmentedControl<bool>::Slot(false).Text(LOCTEXT("LangRu", "RU")).ToolTip(LOCTEXT("LangRuTip", "Names from the Russian assets DT_MobNames / DT_NpcNames"))
					+ SSegmentedControl<bool>::Slot(true).Text(LOCTEXT("LangEn", "EN")).ToolTip(LOCTEXT("LangEnTip", "Names from the L10N/en overlays MobNames / NpcNames"))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
				[
					SNew(SButton).Text(LOCTEXT("Reload", "Reload"))
					.ToolTipText(LOCTEXT("ReloadTip", "Re-read the game tables (also done every time the window opens)"))
					.OnClicked(this, &SAsdaMobBrowser::OnReload)
				]
				+ SHorizontalBox::Slot().FillWidth(0.72f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(this, &SAsdaMobBrowser::GetSourceText).ColorAndOpacity(kDimColor)
					.Font(FAppStyle::GetFontStyle("SmallFont")).AutoWrapText(true)
				]
			]
			// ── группы | превью + характеристики ──
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SSplitter).Orientation(Orient_Horizontal)
				+ SSplitter::Slot().Value(0.27f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[ NpcHeader ]
					+ SVerticalBox::Slot().FillHeight(1.f)
					[
						SNew(SBox).Visibility_Lambda([this]() { return bAnyExpanded && Expanded == EAsdaBrowserKind::Npc ? EVisibility::Visible : EVisibility::Collapsed; })
						[ GroupOf(EAsdaBrowserKind::Npc).List.ToSharedRef() ]
					]
					+ SVerticalBox::Slot().AutoHeight()[ MobHeader ]
					+ SVerticalBox::Slot().FillHeight(1.f)
					[
						SNew(SBox).Visibility_Lambda([this]() { return bAnyExpanded && Expanded == EAsdaBrowserKind::Mob ? EVisibility::Visible : EVisibility::Collapsed; })
						[ GroupOf(EAsdaBrowserKind::Mob).List.ToSharedRef() ]
					]
				]
				+ SSplitter::Slot().Value(0.73f)
				[
					SNew(SSplitter).Orientation(Orient_Vertical)
					+ SSplitter::Slot().Value(0.66f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
							[
								SNew(STextBlock).Text(LOCTEXT("Anim", "Animation:"))
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
							[
								SNew(SBox).MinDesiredWidth(220.f)
								[
									SAssignNew(ClipCombo, SComboBox<TSharedPtr<FAsdaMobClip>>)
									.OptionsSource(&ClipOptions)
									.OnGenerateWidget(this, &SAsdaMobBrowser::MakeClipItem)
									.OnSelectionChanged(this, &SAsdaMobBrowser::OnClipSelected)
									.IsEnabled_Lambda([this]() { return ClipOptions.Num() > 0; })
									[
										SNew(STextBlock).Text(this, &SAsdaMobBrowser::GetClipComboText)
									]
								]
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
							[
								SNew(SCheckBox)
								.IsChecked_Lambda([this]() { return Viewport.IsValid() && Viewport->IsPaused() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
								.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { if (Viewport.IsValid()) { Viewport->SetPaused(S == ECheckBoxState::Checked); } })
								[ SNew(STextBlock).Text(LOCTEXT("Pause", "Pause")) ]
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
							[
								SNew(SCheckBox)
								.IsChecked_Lambda([this]() { return !Viewport.IsValid() || Viewport->IsShowFloor() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
								.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { if (Viewport.IsValid()) { Viewport->SetShowFloor(S == ECheckBoxState::Checked); } })
								[ SNew(STextBlock).Text(LOCTEXT("Floor", "Floor")) ]
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
							[
								SNew(SButton).Text(LOCTEXT("ResetView", "Reset view"))
								.OnClicked_Lambda([this]() { if (Viewport.IsValid()) { Viewport->ResetView(); } return FReply::Handled(); })
							]
							+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
							[
								SNew(STextBlock).Text(this, &SAsdaMobBrowser::GetPreviewInfoText).ColorAndOpacity(kDimColor)
								.Font(FAppStyle::GetFontStyle("SmallFont")).AutoWrapText(true)
							]
						]
						+ SVerticalBox::Slot().FillHeight(1.f)
						[
							SAssignNew(Viewport, SAsdaMobPreviewViewport)
						]
					]
					+ SSplitter::Slot().Value(0.34f)
					[
						SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder")).Padding(6.f)
						[
							SNew(SScrollBox)
							+ SScrollBox::Slot()
							[
								SAssignNew(StatsBox, SVerticalBox)
							]
						]
					]
				]
			]
		]
	];

	RebuildAll();
	SelectFirstShown();
}

// Заголовок группы (кнопка-аккордеон) + создание её списка.
TSharedRef<SWidget> SAsdaMobBrowser::MakeGroupWidget(EAsdaBrowserKind Kind)
{
	FGroup& G = GroupOf(Kind);
	TSharedRef<SHeaderRow> Header = SNew(SHeaderRow)
		+ SHeaderRow::Column(ColName).DefaultLabel(LOCTEXT("ColName", "Name")).FillWidth(1.f)
			.SortMode_Lambda([this, Kind]() { return GetSortMode(Kind, ColName); })
			.OnSort_Lambda([this, Kind](EColumnSortPriority::Type, const FName& C, EColumnSortMode::Type M) { SetSort(Kind, C, M); })
		+ SHeaderRow::Column(ColLevel).DefaultLabel(LOCTEXT("ColLvl", "Lvl")).FixedWidth(48.f).HAlignHeader(HAlign_Right)
			.SortMode_Lambda([this, Kind]() { return GetSortMode(Kind, ColLevel); })
			.OnSort_Lambda([this, Kind](EColumnSortPriority::Type, const FName& C, EColumnSortMode::Type M) { SetSort(Kind, C, M); })
		+ SHeaderRow::Column(ColId).DefaultLabel(LOCTEXT("ColId", "ID")).FixedWidth(48.f).HAlignHeader(HAlign_Right)
			.SortMode_Lambda([this, Kind]() { return GetSortMode(Kind, ColId); })
			.OnSort_Lambda([this, Kind](EColumnSortPriority::Type, const FName& C, EColumnSortMode::Type M) { SetSort(Kind, C, M); });
	SAssignNew(G.List, SListView<TSharedPtr<FAsdaMobEntry>>)
		.ListItemsSource(&G.Shown)
		.SelectionMode(ESelectionMode::Single)
		.HeaderRow(Header)
		.OnGenerateRow(this, &SAsdaMobBrowser::OnGenerateRow)
		.OnSelectionChanged(this, &SAsdaMobBrowser::OnSelectionChanged);

	const FText Title = Kind == EAsdaBrowserKind::Npc ? LOCTEXT("GroupNpc", "NPC") : LOCTEXT("GroupMobs", "Mobs");
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMargin(4.f, 3.f))
		.OnClicked_Lambda([this, Kind]()
		{
			if (bAnyExpanded && Expanded == Kind) { bAnyExpanded = false; }   // щелчок по раскрытой — свернуть
			else { ExpandGroup(Kind); }
			return FReply::Handled();
		})
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SImage).Image_Lambda([this, Kind]()
				{
					return FAppStyle::GetBrush(bAnyExpanded && Expanded == Kind ? "TreeArrow_Expanded" : "TreeArrow_Collapsed");
				})
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(FAppStyle::GetFontStyle("NormalFontBold"))
				.Text_Lambda([this, Kind, Title]()
				{
					return FText::FromString(FString::Printf(TEXT("%s   (%d / %d)"), *Title.ToString(), GroupOf(Kind).Shown.Num(), Data->GetList(Kind).Num()));
				})
			]
		];
}

// ── группы и список ─────────────────────────────────────────────────────────
void SAsdaMobBrowser::ExpandGroup(EAsdaBrowserKind Kind)
{
	const bool bChanged = !bAnyExpanded || Expanded != Kind;
	Expanded = Kind;
	bAnyExpanded = true;
	if (bChanged) { SelectFirstShown(); }
}

bool SAsdaMobBrowser::IsGroupExpanded(EAsdaBrowserKind Kind) const { return bAnyExpanded && Expanded == Kind; }

TSharedRef<ITableRow> SAsdaMobBrowser::OnGenerateRow(TSharedPtr<FAsdaMobEntry> Item, const TSharedRef<STableViewBase>& Owner)
{
	return SNew(SAsdaMobRow, Owner).Item(Item).bEnglish(bEnglish);
}

EColumnSortMode::Type SAsdaMobBrowser::GetSortMode(EAsdaBrowserKind Kind, FName Column) const
{
	const FGroup& G = GroupOf(Kind);
	return Column == G.SortColumn ? G.SortMode : EColumnSortMode::None;
}

void SAsdaMobBrowser::SetSort(EAsdaBrowserKind Kind, FName Column, EColumnSortMode::Type Mode)
{
	FGroup& G = GroupOf(Kind);
	G.SortColumn = Column;
	G.SortMode = (Mode == EColumnSortMode::None) ? EColumnSortMode::Ascending : Mode;
	RebuildShown(G);
}

void SAsdaMobBrowser::OnSearchChanged(const FText& Text)
{
	SearchText = Text.ToString().TrimStartAndEnd();
	RebuildAll();
}

void SAsdaMobBrowser::SetSearchText(const FString& Text)
{
	if (Search.IsValid()) { Search->SetText(FText::FromString(Text)); }
	SearchText = Text.TrimStartAndEnd();
	RebuildAll();
}

void SAsdaMobBrowser::SetEnglish(bool bInEnglish)
{
	bEnglish = bInEnglish;
	RebuildAll();
	for (FGroup& G : Groups) { if (G.List.IsValid()) { G.List->RebuildList(); } }   // строки построены на прежнем языке
	RebuildStats();
}

void SAsdaMobBrowser::RebuildAll()
{
	for (FGroup& G : Groups) { RebuildShown(G); }
}

void SAsdaMobBrowser::RebuildShown(FGroup& G)
{
	G.Shown.Reset();
	const bool bNumeric = !SearchText.IsEmpty() && SearchText.IsNumeric();
	const FString Needle = FAsdaMobBrowserData::SearchKey(SearchText);   // регистр — по правилам языка (кириллица тоже)
	for (const TSharedPtr<FAsdaMobEntry>& E : Data->GetList(G.Kind))
	{
		if (!SearchText.IsEmpty())
		{
			const auto Has = [&Needle](const FString& Hay) { return !Hay.IsEmpty() && FAsdaMobBrowserData::SearchKey(Hay).Contains(Needle); };
			const bool bName = Has(E->NameRu) || Has(E->NameEn) || Has(E->Model);
			const bool bId = bNumeric && FString::FromInt(E->Id).StartsWith(SearchText);
			if (!bName && !bId) { continue; }
		}
		G.Shown.Add(E);
	}
	const bool bEn = bEnglish;
	const FName Col = G.SortColumn;
	const bool bDesc = G.SortMode == EColumnSortMode::Descending;
	G.Shown.StableSort([bEn, Col, bDesc](const TSharedPtr<FAsdaMobEntry>& A, const TSharedPtr<FAsdaMobEntry>& B)
	{
		int32 C = 0;
		if (Col == ColLevel) { C = A->DisplayLevel() - B->DisplayLevel(); }
		else if (Col == ColId) { C = A->Id - B->Id; }
		if (C == 0 && Col != ColId) { C = FAsdaMobBrowserData::CompareNames(A->DisplayName(bEn), B->DisplayName(bEn)); }
		if (C == 0) { C = A->Id - B->Id; }
		return bDesc ? C > 0 : C < 0;
	});
	if (G.List.IsValid())
	{
		G.List->RequestListRefresh();
		if (Selected.IsValid() && Selected->Kind == G.Kind && G.Shown.Contains(Selected))
		{
			G.List->SetItemSelection(Selected, true, ESelectInfo::Direct);
			G.List->RequestScrollIntoView(Selected);
		}
	}
}

TArray<int32> SAsdaMobBrowser::GetShownIds(EAsdaBrowserKind Kind) const
{
	TArray<int32> Ids;
	for (const TSharedPtr<FAsdaMobEntry>& E : GroupOf(Kind).Shown) { Ids.Add(E->Id); }
	return Ids;
}

void SAsdaMobBrowser::SelectFirstShown()
{
	if (!bAnyExpanded) { return; }
	FGroup& G = GroupOf(Expanded);
	if (G.Shown.Num() == 0) { RebuildStats(); return; }
	if (Selected.IsValid() && Selected->Kind == G.Kind && G.Shown.Contains(Selected)) { return; }   // выбор группы сохраняется
	G.List->SetSelection(G.Shown[0], ESelectInfo::Direct);
	if (Selected != G.Shown[0]) { OnSelectionChanged(G.Shown[0], ESelectInfo::Direct); }   // событие при Direct приходит не всегда
}

bool SAsdaMobBrowser::SelectEntry(EAsdaBrowserKind Kind, int32 Id)
{
	TSharedPtr<FAsdaMobEntry> E = Data->Find(Kind, Id);
	if (!E.IsValid()) { return false; }
	FGroup& G = GroupOf(Kind);
	if (!G.Shown.Contains(E)) { SetSearchText(FString()); }
	Expanded = Kind;
	bAnyExpanded = true;
	G.List->SetSelection(E, ESelectInfo::OnMouseClick);
	G.List->RequestScrollIntoView(E);
	if (Selected != E) { OnSelectionChanged(E, ESelectInfo::OnMouseClick); }
	return true;
}

void SAsdaMobBrowser::OnSelectionChanged(TSharedPtr<FAsdaMobEntry> Item, ESelectInfo::Type)
{
	if (!Item.IsValid() || Item == Selected) { return; }
	// Выбор в одной группе снимает выделение в другой (в окне одна выбранная запись).
	for (FGroup& G : Groups) { if (G.Kind != Item->Kind && G.List.IsValid()) { G.List->ClearSelection(); } }
	Selected = Item;
	CurAssets = FAsdaMobBrowserData::ResolveModel(*Item);
	// Ассеты выбранной записи держатся сильно, пока она выбрана (материалы-оверрайды без слота иначе собрал бы GC).
	CurKeep.Reset();
	if (CurAssets.Skel) { CurKeep.Emplace(CurAssets.Skel); }
	if (CurAssets.Static) { CurKeep.Emplace(CurAssets.Static); }
	for (const TPair<FName, UMaterialInterface*>& M : CurAssets.MaterialOverrides) { if (M.Value) { CurKeep.Emplace(M.Value); } }
	ClipOptions.Reset();
	CurClip.Reset();
	for (const FAsdaMobClip& C : CurAssets.Clips) { ClipOptions.Add(MakeShared<FAsdaMobClip>(C)); }
	if (Viewport.IsValid()) { Viewport->SetModel(CurAssets, Item->Kind == EAsdaBrowserKind::Npc); }
	if (ClipCombo.IsValid()) { ClipCombo->ClearSelection(); ClipCombo->RefreshOptions(); }
	// По умолчанию — покой (слот 0), как у игры; нет слота 0 — первый клип.
	if (ClipOptions.Num() > 0 && !SelectClipIndex(0)) { SelectClipIndex(ClipOptions[0]->Index); }
	RebuildStats();
}

// ── анимация ────────────────────────────────────────────────────────────────
TSharedRef<SWidget> SAsdaMobBrowser::MakeClipItem(TSharedPtr<FAsdaMobClip> Clip) const
{
	const EAsdaBrowserKind K = Selected.IsValid() ? Selected->Kind : EAsdaBrowserKind::Mob;
	return SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%02d  %s"), Clip->Index, *FAsdaMobBrowserData::ClipLabel(K, Clip->Index))));
}

FText SAsdaMobBrowser::GetClipComboText() const
{
	if (ClipOptions.Num() == 0) { return LOCTEXT("NoClips", "-"); }
	const EAsdaBrowserKind K = Selected.IsValid() ? Selected->Kind : EAsdaBrowserKind::Mob;
	return CurClip.IsValid() ? FText::FromString(FString::Printf(TEXT("%02d  %s"), CurClip->Index, *FAsdaMobBrowserData::ClipLabel(K, CurClip->Index)))
	                         : LOCTEXT("PickClip", "-");
}

void SAsdaMobBrowser::OnClipSelected(TSharedPtr<FAsdaMobClip> Clip, ESelectInfo::Type)
{
	if (!Clip.IsValid() || !Viewport.IsValid()) { return; }
	UAnimSequence* A = UAsdaModelAnimSubsystem::LoadClipChecked(Clip->AssetPath);
	if (Viewport->PlayClip(A, /*bLoop*/ true)) { CurClip = Clip; }
}

bool SAsdaMobBrowser::SelectClipIndex(int32 ClipIndex)
{
	for (const TSharedPtr<FAsdaMobClip>& C : ClipOptions)
	{
		if (C->Index != ClipIndex) { continue; }
		if (ClipCombo.IsValid()) { ClipCombo->SetSelectedItem(C); }   // зовёт OnClipSelected
		if (CurClip != C) { OnClipSelected(C, ESelectInfo::Direct); }
		return CurClip == C;
	}
	return false;
}

FText SAsdaMobBrowser::GetPreviewInfoText() const
{
	if (!Selected.IsValid()) { return FText::GetEmpty(); }
	FString S;
	if (CurAssets.Model.IsEmpty()) { S = TEXT("Model: -"); }
	else if (CurAssets.Skel)
	{
		S = FString::Printf(TEXT("Skeletal %s · %d clips"), *CurAssets.SkelModel, CurAssets.Clips.Num());
		if (CurAssets.bClipBaseVariant) { S += TEXT(" · clips from base model (+0x24)"); }
	}
	else if (CurAssets.Static) { S = FString::Printf(TEXT("Static model %s — no animations (same fallback as in game)"), *CurAssets.Model); }
	else { S = FString::Printf(TEXT("Model %s: no asset in the project"), *CurAssets.Model); }
	S += TEXT("   ·   LMB drag — rotate, RMB drag — tilt, wheel — zoom, double-click — reset");
	return FText::FromString(S);
}

// ── верхняя строка ──────────────────────────────────────────────────────────
FText SAsdaMobBrowser::GetSourceText() const
{
	FString S = TEXT("Game tables: DT_MobNames, DT_MobStats, DT_MobTextures, DT_NpcNames, DT_NpcQuestNpc (re-read on open)");
	if (!Data->GetLoadError().IsEmpty()) { S += TEXT(" · ERROR: ") + Data->GetLoadError(); }
	return FText::FromString(S);
}

FReply SAsdaMobBrowser::OnReload()
{
	const TSharedPtr<FAsdaMobEntry> Keep = Selected;
	Selected.Reset();
	Data->Reload();
	RebuildAll();
	for (FGroup& G : Groups) { if (G.List.IsValid()) { G.List->RebuildList(); } }
	if (!Keep.IsValid() || !SelectEntry(Keep->Kind, Keep->Id)) { SelectFirstShown(); }
	return FReply::Handled();
}

// ── характеристики ──────────────────────────────────────────────────────────
void SAsdaMobBrowser::RebuildStats()
{
	if (!StatsBox.IsValid()) { return; }
	StatsBox->ClearChildren();
	StatsLinesForTest.Reset();
	if (!Selected.IsValid())
	{
		StatsBox->AddSlot().AutoHeight()[ SNew(STextBlock).Text(LOCTEXT("NoSel", "Select an entry in the list")) ];
		return;
	}
	const FAsdaMobEntry& E = *Selected;
	const FAsdaMobClientStats& C = E.Client;
	const FAsdaNpcInfo& N = E.Npc;
	const bool bMob = E.Kind == EAsdaBrowserKind::Mob;

	const FString Title = FString::Printf(TEXT("%s   ·   %s ID %d"), *E.DisplayName(bEnglish), bMob ? TEXT("Mob") : TEXT("NPC"), E.Id);
	StatsBox->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
	[
		SNew(STextBlock).Text(FText::FromString(Title)).Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
	];
	StatsLinesForTest.Add(Title);

	TSharedPtr<SGridPanel> Grid;
	int32 Cell = 0;   // номер пары «подпись — значение» в текущей сетке (по 3 пары в строке)
	const auto Section = [&](const FString& Name, const FString& Note)
	{
		StatsBox->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[ SNew(STextBlock).Text(FText::FromString(Name)).ColorAndOpacity(kSectionColor) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
			[ SNew(STextBlock).Text(FText::FromString(Note)).ColorAndOpacity(kDimColor).Font(FAppStyle::GetFontStyle("SmallFont")) ]
		];
		StatsBox->AddSlot().AutoHeight()[ SAssignNew(Grid, SGridPanel).FillColumn(1, 1.f).FillColumn(3, 1.f).FillColumn(5, 1.f) ];
		Cell = 0;
		StatsLinesForTest.Add(FString::Printf(TEXT("== %s %s"), *Name, *Note));
	};
	// Пара «подпись — значение»; пустое значение — «-».
	const auto Pair = [&](const FString& Label, const FString& Value, const FString& Tip = FString())
	{
		const int32 Col = (Cell % 3) * 2;
		const int32 Row = Cell / 3;
		++Cell;
		const FString V = OrDash(Value);
		Grid->AddSlot(Col, Row).Padding(0.f, 1.f, 8.f, 1.f)
		[
			SNew(STextBlock).Text(FText::FromString(Label + TEXT(":"))).ColorAndOpacity(kDimColor).ToolTipText(FText::FromString(Tip))
		];
		Grid->AddSlot(Col + 1, Row).Padding(0.f, 1.f, 18.f, 1.f)
		[
			SNew(STextBlock).Text(FText::FromString(V)).ToolTipText(FText::FromString(Tip))
		];
		StatsLinesForTest.Add(FString::Printf(TEXT("%s: %s"), *Label, *V));
	};
	// Значение клиентской строки или «-», если строки нет.
	const auto Cli = [&](const FString& V) { return C.bValid ? V : FString(kDash); };

	// ── общее ──
	Section(TEXT("General"), bMob ? TEXT("(DT_MobNames, DT_MobStats)") : TEXT("(DT_NpcNames — NPC have no level/HP in the game tables)"));
	if (bMob)
	{
		Pair(TEXT("Level"), Cli(StatNum(C.Level)), TEXT("DT_MobStats.Level = Mob list +0x98"));
		Pair(TEXT("HP"), Cli(StatNum(C.MaxHp)), TEXT("DT_MobStats.MaxHp = Mob list +0x94"));
		Pair(TEXT("EXP"), Cli(StatNum(C.Exp)), TEXT("DT_MobStats.Exp = Mob list +0x9C"));
	}
	else
	{
		Pair(TEXT("Level"), kDash, TEXT("No level field in npc_list"));
		Pair(TEXT("HP"), kDash, TEXT("No HP field in npc_list"));
		Pair(TEXT("EXP"), kDash, TEXT("No EXP field in npc_list"));
	}
	Pair(TEXT("Name RU"), E.NameRu, bMob ? TEXT("DT_MobNames.Name") : TEXT("DT_NpcNames.Name"));
	Pair(TEXT("Name EN"), E.NameEn, bMob ? TEXT("L10N/en MobNames") : TEXT("L10N/en NpcNames"));
	Pair(TEXT("Model"), E.Model, bMob ? TEXT("DT_MobNames.Model = Mob list +0x24") : TEXT("DT_NpcQuestNpc.ModelName = npc_list +0x46"));

	if (bMob)
	{
		Section(TEXT("Mob · Mob list.BIN"), C.bValid ? TEXT("(DT_MobStats)") : TEXT("(no row in DT_MobStats)"));
		Pair(TEXT("Rank"), Cli(FAsdaMobBrowserData::RankLabel(C.Rank)), TEXT("+0xFC: target frame ring and name colour"));
		Pair(TEXT("Element"), Cli(FAsdaMobBrowserData::ElementLabel(C.Element)), TEXT("+0xF8: element mark PVP_MARK00..05"));
		Pair(TEXT("Attacks first"), Cli(C.AttackFirst ? TEXT("Yes") : TEXT("No")), TEXT("+0x100: BAT_FIRST mark of the target frame"));
		Pair(TEXT("HP bar segments"), Cli(StatNum(C.Lives)), TEXT("+0x10C: boss HP bar «lives» (display only)"));
		Pair(TEXT("Size class"), Cli(StatNum(C.SizeClass)), TEXT("+0x90: effect branch / attach height"));
		Pair(TEXT("Hit FX scale"), Cli(StatPct(C.SizePercent)), TEXT("+0x108: scale of hit effects on this mob"));
		Pair(TEXT("Half-size"), Cli(FString::Printf(TEXT("%.0f × %.0f × %.0f"), C.HalfX, C.HalfY, C.HalfZ)), TEXT("+0x84: AABB half-size; nameplate height = 2·Y + 50"));
		Pair(TEXT("Sound range"), Cli(StatNum(C.SoundRange)), TEXT("+0x104"));
	}
	else
	{
		Section(TEXT("NPC · npc_list"), N.bValid ? TEXT("(DT_NpcQuestNpc)") : TEXT("(no row in DT_NpcQuestNpc)"));
		const auto Npc = [&](const FString& V) { return N.bValid ? V : FString(kDash); };
		Pair(TEXT("Type"), Npc(FString::Printf(TEXT("%d%s"), N.NpcType, (N.NpcType == 1 || N.NpcType == 3 || N.NpcType == 5) ? TEXT(" (static)") : TEXT(""))),
			TEXT("npc_list +0x7C: entity class; 1/3/5 — static object, 0/4/5 — listed on the map"));
		Pair(TEXT("Dialog script"), Npc(N.NsrName), TEXT("npc_list +0x26: .nsr script name"));
		Pair(TEXT("Height"), Npc(StatNum(N.PlateHeight)), TEXT("npc_list +0x68: entity height (- = not set)"));
		Pair(TEXT("Map"), Npc(StatNum(N.MapId)), TEXT("npc_list +0x6C: map id"));
		Pair(TEXT("Position"), Npc(FString::Printf(TEXT("%.0f, %.0f"), N.WorldX, N.WorldY)), TEXT("npc_list +0x70/+0x74: world X, Y"));
	}

	// ── модель ──
	Section(TEXT("Model assets"), TEXT("(same mapping as the game)"));
	Pair(TEXT("Asset"), CurAssets.Skel ? FString::Printf(TEXT("skeletal %s%s"), *CurAssets.SkelModel, CurAssets.bClipBaseVariant ? TEXT(" (clip-base variant)") : TEXT(""))
		: (CurAssets.Static ? FString(TEXT("static fallback")) : FString(kDash)), bMob ? TEXT("/Models/MobSkel, otherwise /Models/Mob") : TEXT("/Models/NPC"));
	Pair(TEXT("Clips"), CurAssets.Skel ? FString::FromInt(CurAssets.Clips.Num()) : FString(kDash), TEXT("anim_NN (or a single _Anim) of the skeletal folder"));
	Pair(TEXT("Texture overrides"), bMob ? FString::FromInt(CurAssets.MaterialOverrides.Num()) : FString(kDash), TEXT("DT_MobTextures slots applied (per-type recolour)"));
}

FString SAsdaMobBrowser::DescribeStatsForTest() const
{
	return FString::Join(StatsLinesForTest, TEXT("\n"));
}

#undef LOCTEXT_NAMESPACE
