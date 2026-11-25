// Copyright (c) 2026 @sitxovski (Discord)
// Реализация панели просмотрщика предметов — см. SAsdaItemBrowser.h.
// ★★★ ПЕРЕДЕЛКА 29.10.2026: фильтры/группировки (FAsdaItemBrowserData::BuildTreeEx) и карточка предмета, которую
//   строит ИГРОВОЙ построитель подсказки (`AsdaItemTipStandalone::BuildCard` → `UAsdaItemTooltipSubsystem::Build*`).
#include "SAsdaItemBrowser.h"
#include "SAsdaItemPreviewViewport.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"          // пояснение «модели нет» поверх вьюпорта
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SHyperlink.h"
#include "Framework/Text/ITextDecorator.h"
#include "Framework/Text/SlateTextRun.h"
#include "Framework/Text/TextLayout.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"

#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Editor.h"
#include "AsdaEffectActor.h"  // ★ 13.09.2026: код предмета на эффекте заточки превью (переопределения конфигуратора свечения)
#include "UI/AsdaItemTipStandalone.h"     // ★ 29.10.2026: игровой построитель подсказки без GameInstance
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaMarkup.h"                // FAsdaMarkup::ToSegments — разбор тегов <FC=…> подсказки (та же грамматика, что в игре)
#include "UI/AsdaUiHelpers.h"             // AsdaUi::SrgbU32ToLinear — цвет заголовка как у плашки
#include "Items/AsdaItemTableSubsystem.h"
#include "Text/AsdaLang.h"

#define LOCTEXT_NAMESPACE "AsdaItemBrowser"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaItemBrowserUI, Log, All);

namespace
{
	// Шрифт карточки (редактор): обычный и жирный.
	FSlateFontInfo CardFont(int32 Size, bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	// ★ Декоратор SRichTextBlock для разметки подсказки: run `<c v="r,g,b" b="1">текст</>` — цвет и жирность сегмента
	//   (сегменты даёт FAsdaMarkup::ToSegments — та же грамматика тегов <FC/FB/FO/FD/FS>, что у игровой плашки).
	class FAsdaTipRunDecorator : public ITextDecorator
	{
	public:
		explicit FAsdaTipRunDecorator(const FTextBlockStyle& InBase) : Base(InBase) {}
		virtual bool Supports(const FTextRunParseResults& RunInfo, const FString& Text) const override
		{
			return RunInfo.Name == TEXT("c");
		}
		virtual TSharedRef<ISlateRun> Create(const TSharedRef<FTextLayout>& TextLayout, const FTextRunParseResults& RunParseResult,
			const FString& OriginalText, const TSharedRef<FString>& InOutModelText, const ISlateStyle* Style) override
		{
			FRunInfo RunInfo(RunParseResult.Name);
			for (const TPair<FString, FTextRange>& P : RunParseResult.MetaData)
			{
				RunInfo.MetaData.Add(P.Key, OriginalText.Mid(P.Value.BeginIndex, P.Value.EndIndex - P.Value.BeginIndex));
			}
			FTextBlockStyle S = Base;
			if (const FString* V = RunInfo.MetaData.Find(TEXT("v")))
			{
				TArray<FString> Parts; V->ParseIntoArray(Parts, TEXT(","));
				if (Parts.Num() >= 3)
				{
					S.SetColorAndOpacity(FSlateColor(FLinearColor(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]), 1.f)));
				}
			}
			if (const FString* B = RunInfo.MetaData.Find(TEXT("b")); B && *B == TEXT("1"))
			{
				FSlateFontInfo F = S.Font; F.TypefaceFontName = TEXT("Bold"); S.SetFont(F);
			}
			FTextRange ModelRange;
			ModelRange.BeginIndex = InOutModelText->Len();
			*InOutModelText += OriginalText.Mid(RunParseResult.ContentRange.BeginIndex,
				RunParseResult.ContentRange.EndIndex - RunParseResult.ContentRange.BeginIndex);
			ModelRange.EndIndex = InOutModelText->Len();
			return FSlateTextRun::Create(RunInfo, InOutModelText, S, ModelRange);
		}
	private:
		FTextBlockStyle Base;
	};

	// Экранирование текста для разметки SRichTextBlock.
	FString EscapeRich(const FString& S)
	{
		FString O = S;
		O.ReplaceInline(TEXT("&"), TEXT("&amp;"));
		O.ReplaceInline(TEXT("<"), TEXT("&lt;"));
		O.ReplaceInline(TEXT(">"), TEXT("&gt;"));
		O.ReplaceInline(TEXT("\""), TEXT("&quot;"));
		return O;
	}

	// Игровая разметка → разметка SRichTextBlock. Сегмент без своего цвета (белый по умолчанию) рисуется BaseColor.
	//   Перенос строки закрывает run (разбор SRichTextBlock построчный).
	FString TipToRich(const FString& Raw, const FLinearColor& BaseColor)
	{
		TArray<FAsdaTextSegment> Segs;
		FAsdaMarkup::ToSegments(Raw, Segs);
		FString Out;
		for (const FAsdaTextSegment& Sg : Segs)
		{
			const FLinearColor C = Sg.Color.Equals(FLinearColor::White) ? BaseColor : Sg.Color;
			const FString Open = FString::Printf(TEXT("<c v=\"%.4f,%.4f,%.4f\" b=\"%d\">"), C.R, C.G, C.B, Sg.Style == EAsdaTextStyle::Bold ? 1 : 0);
			TArray<FString> Lines;
			Sg.Text.ParseIntoArray(Lines, TEXT("\n"), /*CullEmpty*/false);
			for (int32 L = 0; L < Lines.Num(); ++L)
			{
				if (L > 0) { Out += TEXT("\n"); }
				if (!Lines[L].IsEmpty()) { Out += Open + EscapeRich(Lines[L]) + TEXT("</>"); }
			}
		}
		return Out;
	}

	// Разделитель тысяч для цен (в игре — `AsdaUi::FormatThousands`, тот же вид).
	FString Num(int32 V) { return AsdaUi::FormatThousands(V); }
}

void SAsdaItemBrowser::Construct(const FArguments& InArgs)
{
	Data = FAsdaItemBrowserData::Get();
	// ЛЕНИВО: таблицы грузятся только при открытии вкладки (не на старте редактора).
	Data->EnsureLoaded();

	EnhanceOptions.Add(MakeShared<FString>(TEXT("Заточка: нет")));
	EnhanceOptions.Add(MakeShared<FString>(TEXT("Заточка: +5")));
	EnhanceOptions.Add(MakeShared<FString>(TEXT("Заточка: +6")));
	EnhanceOptions.Add(MakeShared<FString>(TEXT("Заточка: +7 и выше")));
	SexOptions.Add(MakeShared<FString>(TEXT("Мужской вариант")));
	SexOptions.Add(MakeShared<FString>(TEXT("Женский вариант")));

	// ── Опции фильтров ──
	SpaceOptions.Add(MakeShared<FString>(TEXT("Все таблицы")));
	SpaceOptions.Add(MakeShared<FString>(TEXT("item_list — предметы")));
	SpaceOptions.Add(MakeShared<FString>(TEXT("sowell_list — живые камни")));
	SpaceOptions.Add(MakeShared<FString>(TEXT("special_itemlist — спец-предметы")));
	CatOptions.Add(MakeShared<FString>(TEXT("Все категории")));
	for (int32 C = 0; C < (int32)EAsdaItemCat::Count; ++C)
	{
		CatOptions.Add(MakeShared<FString>(FAsdaItemBrowserData::CatLabel((EAsdaItemCat)C)));
	}
	const int32 NJobs = FMath::Max(Data->NumJobs(), 45);
	JobOptions.Add(MakeShared<FString>(TEXT("Любой класс")));
	for (int32 J = 0; J < NJobs; ++J)
	{
		const FString Name = Data->JobName(J);
		const FString Par = Data->JobParent(J);
		const FString Line = FString::Printf(TEXT("%d — %s%s"), J, Name.IsEmpty() ? TEXT("?") : *Name,
			(Par.IsEmpty() || Par == Name) ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *Par));
		JobOptions.Add(MakeShared<FString>(Line));
		ViewerJobOptions.Add(MakeShared<FString>(Line));
	}
	JobModeOptions.Add(MakeShared<FString>(TEXT("точное требование")));
	JobModeOptions.Add(MakeShared<FString>(TEXT("вся ветка архетипа")));
	JobModeOptions.Add(MakeShared<FString>(TEXT("может надеть (правило игры)")));
	GradeOptions.Add(MakeShared<FString>(TEXT("Любой грейд")));
	for (int32 G = 0; G <= 8; ++G) { GradeOptions.Add(MakeShared<FString>(FAsdaItemBrowserData::GradeLabel(G))); }
	PremiumOptions.Add(MakeShared<FString>(TEXT("Обычные и премиум")));
	PremiumOptions.Add(MakeShared<FString>(TEXT("Только обычные")));
	PremiumOptions.Add(MakeShared<FString>(TEXT("Только премиум (categoryFlag 3 / special_itemlist)")));
	GroupOptions.Add(MakeShared<FString>(TEXT("Группировка: по типу")));
	GroupOptions.Add(MakeShared<FString>(TEXT("Группировка: по классу")));
	GroupOptions.Add(MakeShared<FString>(TEXT("Группировка: по уровню")));
	GroupOptions.Add(MakeShared<FString>(TEXT("Группировка: по грейду")));
	CardKindOptions.Add(MakeShared<FString>(TEXT("Подсказка: как в игре (авто)")));
	CardKindOptions.Add(MakeShared<FString>(TEXT("Подсказка: полная (вкладка «Снаряжение»)")));
	CardKindOptions.Add(MakeShared<FString>(TEXT("Подсказка: простая (вкладка «Расходники»)")));

	RebuildTree();

	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)

		// ── ЛЕВО: поиск + фильтры + дерево ───────────────────────────────────
		+ SSplitter::Slot()
		.Value(0.38f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
			[
				SAssignNew(SearchBox, SSearchBox)
				.HintText(LOCTEXT("SearchHint", "Поиск: имя (RU или EN) или код — во всех трёх таблицах"))
				.ToolTipText(LOCTEXT("SearchTip",
					"Число трактуется и как код: точные совпадения выносятся отдельной веткой сверху с указанием таблицы. "
					"Коды item_list, sowell_list и special_itemlist не пересекаются (461 — спец-предмет, 7001 — живой камень)."))
				.OnTextChanged_Lambda([this](const FText& T)
				{
					Filter.Text = T.ToString();
					RebuildTree();
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 0.f, 4.f, 4.f)
			[
				BuildFilterPanel()
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 0.f, 4.f, 4.f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text_Lambda([this]()
				{
					if (!Data.IsValid() || !Data->IsLoaded())
					{
						return FText::FromString(FString::Printf(TEXT("Данные не загружены: %s"),
							Data.IsValid() ? *Data->GetLoadError() : TEXT("нет объекта данных")));
					}
					return FText::FromString(FString::Printf(
						TEXT("Записей: %d (item_list %d + sowell_list %d + special_itemlist %d)   прошло фильтр: %d"),
						Data->Num(), Data->NumInSpace(EAsdaItemSpace::ItemList), Data->NumInSpace(EAsdaItemSpace::SowelList),
						Data->NumInSpace(EAsdaItemSpace::SpecialList), ShownCount));
				})
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				[
					SAssignNew(TreeView, STreeView<FNodePtr>)
					.TreeItemsSource(&RootNodes)
					.OnGenerateRow(this, &SAsdaItemBrowser::OnGenerateRow)
					.OnGetChildren(this, &SAsdaItemBrowser::OnGetChildren)
					.OnSelectionChanged(this, &SAsdaItemBrowser::OnSelectionChanged)
					.SelectionMode(ESelectionMode::Single)
				]
			]
		]

		// ── ПРАВО: превью + карточка ─────────────────────────────────────────
		+ SSplitter::Slot()
		.Value(0.62f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Vertical)
			+ SSplitter::Slot()
			.Value(0.36f)
			[
				SNew(SVerticalBox)
				// панель управления превью
				+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
				[
					SNew(SWrapBox)
					.UseAllottedSize(true)
					+ SWrapBox::Slot().Padding(0.f, 0.f, 6.f, 2.f)
					[
						MakeCombo(EnhanceOptions, EnhanceIdx, [this]() { RefreshPreview(); }, LOCTEXT("EnhTip", "Ступень свечения заточки в превью"))
					]
					+ SWrapBox::Slot().Padding(0.f, 0.f, 6.f, 2.f)
					[
						MakeCombo(SexOptions, SexIdx, [this]()
						{
							bMale = (SexIdx == 0);
							IconBrushes.Empty();
							if (TreeView.IsValid()) { TreeView->RequestListRefresh(); }
							RefreshPreview();
						}, LOCTEXT("SexTip", "Мужская или женская модель/иконка (у предметов с раздельными вариантами)"))
					]
					+ SWrapBox::Slot().Padding(0.f, 0.f, 6.f, 2.f).VAlign(VAlign_Center)
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([this]() { return (Viewport.IsValid() && Viewport->IsAutoRotate()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { if (Viewport.IsValid()) { Viewport->SetAutoRotate(S == ECheckBoxState::Checked); } })
						[
							SNew(STextBlock).Text(LOCTEXT("AutoRotate", "Вращение"))
						]
					]
					+ SWrapBox::Slot().Padding(0.f, 0.f, 6.f, 2.f).VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(80.f)
						[
							SNew(SSpinBox<float>)
							.MinValue(0.f).MaxValue(180.f)
							.Value_Lambda([this]() { return Viewport.IsValid() ? Viewport->GetRotateSpeed() : 30.f; })
							.OnValueChanged_Lambda([this](float V) { if (Viewport.IsValid()) { Viewport->SetRotateSpeed(V); } })
							.ToolTipText(LOCTEXT("SpeedTip", "Скорость вращения, град/с"))
						]
					]
					+ SWrapBox::Slot().Padding(0.f, 0.f, 6.f, 2.f).VAlign(VAlign_Center)
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([this]() { return (Viewport.IsValid() && Viewport->IsShowFloor()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { if (Viewport.IsValid()) { Viewport->SetShowFloor(S == ECheckBoxState::Checked); } })
						[
							SNew(STextBlock).Text(LOCTEXT("ShowFloor", "Пол/фон"))
						]
					]
					+ SWrapBox::Slot().Padding(0.f, 0.f, 6.f, 2.f).VAlign(VAlign_Center)
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([this]() { return (Viewport.IsValid() && Viewport->IsAutoOrient()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { if (Viewport.IsValid()) { Viewport->SetAutoOrient(S == ECheckBoxState::Checked); } })
						.ToolTipText(LOCTEXT("AutoOrientTip", "Поставить модель по главной оси габаритов (модели авторены в осях игры; в мире их разворачивает кость персонажа)"))
						[
							SNew(STextBlock).Text(LOCTEXT("AutoOrient", "Автоориентация"))
						]
					]
					+ SWrapBox::Slot().Padding(0.f, 0.f, 6.f, 2.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("Refocus", "Кадрировать"))
						.OnClicked_Lambda([this]() { if (Viewport.IsValid()) { Viewport->FocusOnPreview(); } return FReply::Handled(); })
					]
					+ SWrapBox::Slot().Padding(0.f, 0.f, 6.f, 2.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("ShowInCB", "Показать ассеты"))
						.ToolTipText(LOCTEXT("ShowInCBTip", "Выделить модель/текстуру/эффект предмета в Content Browser"))
						.OnClicked_Lambda([this]()
						{
							if (!Data.IsValid() || SelectedIndex == INDEX_NONE || !GEditor) { return FReply::Handled(); }
							const FAsdaBrowserItemAssets A = Data->ResolveAssets(SelectedIndex, bMale, GetEnhance());
							TArray<UObject*> Objs;
							auto AddObj = [&Objs](const FString& Path)
							{
								if (Path.IsEmpty()) { return; }
								if (UObject* O = LoadObject<UObject>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet)) { Objs.Add(O); }
							};
							AddObj(A.MeshObjectPath);
							AddObj(A.TexObjectPath);
							AddObj(A.Icon.AtlasObjectPath);
							AddObj(A.BuffIconObjectPath);             // атлас иконки баффа (special_itemlist)
							AddObj(A.GlowBpObjectPath.LeftChop(2));   // "..._C" → сам Blueprint-ассет
							if (Objs.Num() > 0) { GEditor->SyncBrowserToObjects(Objs); }
							return FReply::Handled();
						})
					]
					+ SWrapBox::Slot().Padding(0.f, 0.f, 6.f, 2.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("Reload", "Перечитать таблицы"))
						.ToolTipText(LOCTEXT("ReloadTip",
							"Заново загрузить DataTable-ассеты item-БД (после импорта новых предметов — например, переноса "
							"WorldOfDreams). Просмотрщик только читает таблицы и ничего в них не пишет."))
						.OnClicked_Lambda([this]()
						{
							AsdaItemTipStandalone::Release();
							Data->Reload();
							InBoxes.Reset(); bInBoxesBuilt = false;
							IconBrushes.Empty(); AtlasCache.Empty(); BadAtlases.Empty();
							SelectedIndex = INDEX_NONE;
							RebuildTree();
							RefreshPreview();
							return FReply::Handled();
						})
					]
				]
				// вьюпорт (+ поясняющая надпись, когда показывать нечего)
				+ SVerticalBox::Slot().FillHeight(1.f).Padding(4.f, 0.f, 4.f, 4.f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SAssignNew(Viewport, SAsdaItemPreviewViewport)
					]
					// ★ У самоцветов и особенно у спец-предметов модели часто НЕТ и в оригинале
					//   (свитки, зелья — только иконка). Пустой чёрный вьюпорт выглядел бы поломкой,
					//   поэтому пишем причину прямо поверх него.
					+ SOverlay::Slot()
					.HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(SBox)
						.Visibility_Lambda([this]() { return NoMeshNote.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
						.MaxDesiredWidth(520.f)
						[
							SNew(SBorder)
							.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
							.Padding(10.f)
							[
								SNew(STextBlock)
								.AutoWrapText(true)
								.Justification(ETextJustify::Center)
								.ColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.85f, 0.85f)))
								.Text_Lambda([this]() { return FText::FromString(NoMeshNote); })
							]
						]
					]
				]
			]
			// карточка предмета
			+ SSplitter::Slot()
			.Value(0.64f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot().Padding(6.f)
					[
						SAssignNew(CardBox, SVerticalBox)
					]
				]
			]
		]
	];

	// Начальное состояние панели: вьюпорт ещё пуст — сразу пишем почему, а не показываем чёрный экран.
	RefreshPreview();
}

TSharedRef<SWidget> SAsdaItemBrowser::MakeCombo(TArray<FStrPtr>& Options, int32& Index, TFunction<void()> OnChanged, const FText& Tip)
{
	if (!Options.IsValidIndex(Index)) { Index = 0; }
	TArray<FStrPtr>* Opts = &Options;
	int32* Idx = &Index;
	return SNew(SComboBox<FStrPtr>)
		.OptionsSource(Opts)
		.InitiallySelectedItem(Options.IsValidIndex(Index) ? Options[Index] : FStrPtr())
		.ToolTipText(Tip)
		.OnGenerateWidget_Lambda([](FStrPtr S) { return SNew(STextBlock).Text(FText::FromString(S.IsValid() ? *S : FString())); })
		.OnSelectionChanged_Lambda([Opts, Idx, OnChanged](FStrPtr S, ESelectInfo::Type)
		{
			const int32 I = Opts->IndexOfByKey(S);
			*Idx = (I == INDEX_NONE) ? 0 : I;
			if (OnChanged) { OnChanged(); }
		})
		[
			SNew(STextBlock).Text_Lambda([Opts, Idx]()
			{
				return FText::FromString(Opts->IsValidIndex(*Idx) ? *(*Opts)[*Idx] : FString());
			})
		];
}

TSharedRef<SWidget> SAsdaItemBrowser::BuildFilterPanel()
{
	auto Apply = [this]() { ApplyFilterControls(); };
	auto Label = [](const FText& T) { return SNew(STextBlock).Text(T).ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f))); };
	auto LvSpin = [this](int32* Field, const FText& Tip)
	{
		return SNew(SBox).WidthOverride(64.f)
			[
				SNew(SSpinBox<int32>)
				.MinValue(0).MaxValue(255).Delta(1)
				.ToolTipText(Tip)
				.Value_Lambda([Field]() { return *Field; })
				.OnValueChanged_Lambda([Field](int32 V) { *Field = V; })
				.OnValueCommitted_Lambda([this, Field](int32 V, ETextCommit::Type) { *Field = V; RebuildTree(); })
			];
	};

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
		[
			SNew(SWrapBox).UseAllottedSize(true)
			+ SWrapBox::Slot().Padding(0.f, 0.f, 4.f, 2.f)
			[ MakeCombo(SpaceOptions, SpaceIdx, Apply, LOCTEXT("SpaceTip", "Таблица: игра адресует предметы тремя независимыми таблицами")) ]
			+ SWrapBox::Slot().Padding(0.f, 0.f, 4.f, 2.f)
			[ MakeCombo(CatOptions, CatIdx, Apply, LOCTEXT("CatTip",
				"Категория — игровая классификация: «Место в снаряжении» (нотис 8569 + itemType), вкладки биржи, "
				"группы подтипов расходников (itemTypeToCategory_44F0), материалы 60–68, коробки 200–201")) ]
			+ SWrapBox::Slot().Padding(0.f, 0.f, 4.f, 2.f)
			[ MakeCombo(GroupOptions, GroupIdx, Apply, LOCTEXT("GroupTip", "Как ветвить дерево")) ]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
		[
			SNew(SWrapBox).UseAllottedSize(true)
			+ SWrapBox::Slot().Padding(0.f, 0.f, 4.f, 2.f)
			[ MakeCombo(JobOptions, JobIdx, Apply, LOCTEXT("JobTip",
				"Класс — требование предмета UsageCategory (байт «Профессия» подсказки), имена — таблица jobtype")) ]
			+ SWrapBox::Slot().Padding(0.f, 0.f, 4.f, 2.f)
			[ MakeCombo(JobModeOptions, JobModeIdx, Apply, LOCTEXT("JobModeTip",
				"Точное требование; вся ветка архетипа (1–11 / 12–22 / 23–33 / 34–39 / 40–44); "
				"или «может надеть» — правило игры upgradeCheckGradeReachable@0xEC9010")) ]
			+ SWrapBox::Slot().Padding(0.f, 0.f, 4.f, 2.f)
			[ MakeCombo(GradeOptions, GradeIdx, Apply, LOCTEXT("GradeTip", "Грейд (нотис 1421 + грейд, цвет — палитра подсказки)")) ]
			+ SWrapBox::Slot().Padding(0.f, 0.f, 4.f, 2.f)
			[ MakeCombo(PremiumOptions, PremiumIdx, Apply, LOCTEXT("PremTip",
				"Премиум: у item_list — categoryFlag == 3 (значок и подвал «элитного» предмета, нотис 109832); special_itemlist — весь")) ]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SWrapBox).UseAllottedSize(true)
			+ SWrapBox::Slot().Padding(0.f, 0.f, 4.f, 2.f).VAlign(VAlign_Center) [ Label(LOCTEXT("LvFrom", "Треб. уровень от")) ]
			+ SWrapBox::Slot().Padding(0.f, 0.f, 4.f, 2.f) [ LvSpin(&Filter.LvFrom, LOCTEXT("LvFromTip", "RequiredLevel не ниже (0 — без границы)")) ]
			+ SWrapBox::Slot().Padding(0.f, 0.f, 4.f, 2.f).VAlign(VAlign_Center) [ Label(LOCTEXT("LvTo", "до")) ]
			+ SWrapBox::Slot().Padding(0.f, 0.f, 10.f, 2.f) [ LvSpin(&Filter.LvTo, LOCTEXT("LvToTip", "RequiredLevel не выше (0 — без границы)")) ]
			+ SWrapBox::Slot().Padding(0.f, 0.f, 4.f, 2.f).VAlign(VAlign_Center) [ Label(LOCTEXT("CharLv", "годится уровню")) ]
			+ SWrapBox::Slot().Padding(0.f, 0.f, 4.f, 2.f) [ LvSpin(&Filter.CharLevel, LOCTEXT("CharLvTip",
				"Предмет годится персонажу этого уровня: RequiredLevel ≤ N ≤ RequiredLevelMax (0 — не проверять)")) ]
			+ SWrapBox::Slot().Padding(0.f, 0.f, 4.f, 2.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("ResetFilters", "Сбросить фильтры"))
				.OnClicked_Lambda([this]()
				{
					SpaceIdx = CatIdx = JobIdx = JobModeIdx = GradeIdx = PremiumIdx = 0;
					Filter.LvFrom = Filter.LvTo = Filter.CharLevel = 0;
					if (SearchBox.IsValid()) { SearchBox->SetText(FText::GetEmpty()); }
					Filter.Text.Empty();
					ApplyFilterControls();
					return FReply::Handled();
				})
			]
		];
}

void SAsdaItemBrowser::SetFilterControls(const FString& Search, int32 Space, int32 Cat, int32 Job, int32 JobMode, int32 LvFrom,
	int32 LvTo, int32 CharLevel, int32 Grade, int32 Premium, int32 Grouping)
{
	SpaceIdx = FMath::Clamp(Space, 0, SpaceOptions.Num() - 1);
	CatIdx = FMath::Clamp(Cat + 1, 0, CatOptions.Num() - 1);
	JobIdx = FMath::Clamp(Job + 1, 0, JobOptions.Num() - 1);
	JobModeIdx = FMath::Clamp(JobMode, 0, JobModeOptions.Num() - 1);
	GradeIdx = FMath::Clamp(Grade + 1, 0, GradeOptions.Num() - 1);
	PremiumIdx = (Premium < 0) ? 0 : (Premium == 0 ? 1 : 2);
	GroupIdx = FMath::Clamp(Grouping, 0, GroupOptions.Num() - 1);
	Filter.LvFrom = FMath::Max(0, LvFrom);
	Filter.LvTo = FMath::Max(0, LvTo);
	Filter.CharLevel = FMath::Max(0, CharLevel);
	Filter.Text = Search;
	if (SearchBox.IsValid()) { SearchBox->SetText(FText::FromString(Search)); }
	ApplyFilterControls();
}

void SAsdaItemBrowser::SetCardControls(int32 CardKind, int32 ViewerJob, int32 InViewerLevel)
{
	CardKindIdx = FMath::Clamp(CardKind, 0, CardKindOptions.Num() - 1);
	ViewerJobIdx = FMath::Clamp(ViewerJob, 0, FMath::Max(0, ViewerJobOptions.Num() - 1));
	ViewerLevel = FMath::Max(0, InViewerLevel);
	RebuildCard();
}

void SAsdaItemBrowser::ExpandAll(int32 MaxDepth)
{
	if (!TreeView.IsValid()) { return; }
	TFunction<void(const FNodePtr&, int32)> Expand = [&](const FNodePtr& N, int32 Depth)
	{
		if (!N.IsValid() || N->Kind == EAsdaBrowserNodeKind::Item || Depth > MaxDepth) { return; }
		TreeView->SetItemExpansion(N, true);
		for (const FNodePtr& C : N->Children) { Expand(C, Depth + 1); }
	};
	for (const FNodePtr& N : RootNodes) { Expand(N, 0); }
}

void SAsdaItemBrowser::SelectForTest(int32 GlobalIndex)
{
	TArray<FNodePtr> Chain;
	TFunction<bool(const FNodePtr&)> Find = [&](const FNodePtr& N) -> bool
	{
		if (!N.IsValid()) { return false; }
		if (N->Kind == EAsdaBrowserNodeKind::Item) { if (N->ItemIndex == GlobalIndex) { Chain.Add(N); return true; } return false; }
		for (const FNodePtr& C : N->Children) { if (Find(C)) { Chain.Add(N); return true; } }
		return false;
	};
	for (const FNodePtr& R : RootNodes) { if (Find(R)) { break; } }
	if (Chain.Num() > 0 && TreeView.IsValid())
	{
		for (int32 i = 1; i < Chain.Num(); ++i) { TreeView->SetItemExpansion(Chain[i], true); }
		TreeView->SetSelection(Chain[0], ESelectInfo::Direct);
		TreeView->RequestScrollIntoView(Chain[0]);
	}
	SelectedIndex = GlobalIndex;
	RefreshPreview();
}

void SAsdaItemBrowser::ApplyFilterControls()
{
	switch (SpaceIdx)
	{
	case 1:  Filter.Space = EAsdaItemSpace::ItemList; break;
	case 2:  Filter.Space = EAsdaItemSpace::SowelList; break;
	case 3:  Filter.Space = EAsdaItemSpace::SpecialList; break;
	default: Filter.Space.Reset(); break;
	}
	Filter.Cat = CatIdx - 1;
	Filter.Job = JobIdx - 1;
	Filter.JobMode = (EAsdaJobFilterMode)FMath::Clamp(JobModeIdx, 0, 2);
	Filter.Grade = GradeIdx - 1;
	Filter.Premium = (PremiumIdx == 0) ? -1 : (PremiumIdx == 1 ? 0 : 1);
	Filter.Grouping = (EAsdaBrowserGrouping)FMath::Clamp(GroupIdx, 0, (int32)EAsdaBrowserGrouping::Count - 1);
	RebuildTree();
}

int32 SAsdaItemBrowser::GetEnhance() const
{
	switch (EnhanceIdx)
	{
	case 1: return 5;
	case 2: return 6;
	case 3: return 7;
	default: return 0;
	}
}

void SAsdaItemBrowser::RebuildTree()
{
	if (!Data.IsValid()) { return; }
	Data->BuildTreeEx(Filter, RootNodes, ShownCount);
	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();
		const bool bExpandAll = !Filter.Text.TrimStartAndEnd().IsEmpty() || ShownCount <= 400;
		TFunction<void(const FNodePtr&, int32)> Expand = [&](const FNodePtr& N, int32 Depth)
		{
			if (!N.IsValid() || N->Kind == EAsdaBrowserNodeKind::Item) { return; }
			// Корень «точное совпадение по коду» раскрываем ВСЕГДА — ради него поиск и делался; верхний уровень — тоже.
			if (N->Kind == EAsdaBrowserNodeKind::CodeHit || Depth == 0 || bExpandAll) { TreeView->SetItemExpansion(N, true); }
			if (bExpandAll) { for (const FNodePtr& C : N->Children) { Expand(C, Depth + 1); } }
		};
		for (const FNodePtr& N : RootNodes) { Expand(N, 0); }
	}
}

void SAsdaItemBrowser::OnGetChildren(FNodePtr Node, TArray<FNodePtr>& OutChildren)
{
	if (Node.IsValid()) { OutChildren = Node->Children; }
}

// ── Иконка: атлас + UV-регион ячейки, размер = НАТИВНЫЙ (32/64 px) ──────────
const FSlateBrush* SAsdaItemBrowser::GetIconBrush(int32 ItemIndex, int32& OutSize)
{
	OutSize = 64;
	if (!Data.IsValid()) { return nullptr; }
	const FAsdaBrowserIcon Icon = Data->ResolveIcon(ItemIndex, bMale);
	OutSize = Icon.CellSize;
	if (!Icon.HasAtlasName() || Icon.AtlasObjectPath.IsEmpty()) { return nullptr; }

	const FString Key = FString::Printf(TEXT("%s|%d|%d"), *Icon.Atlas, Icon.Cell, Icon.CellSize);
	if (TSharedPtr<FSlateBrush>* Found = IconBrushes.Find(Key)) { return Found->Get(); }
	if (BadAtlases.Contains(Icon.Atlas)) { return nullptr; }

	TStrongObjectPtr<UTexture2D>* CachedTex = AtlasCache.Find(Icon.AtlasObjectPath);
	if (!CachedTex)
	{
		UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *Icon.AtlasObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Tex) { BadAtlases.Add(Icon.Atlas); return nullptr; }
#if WITH_EDITOR
		Tex->BlockOnAnyAsyncBuild();   // иначе движок отдаёт дефолтную 32×32 (item_texture_audit.md §8.4)
#endif
		CachedTex = &AtlasCache.Add(Icon.AtlasObjectPath, TStrongObjectPtr<UTexture2D>(Tex));
	}
	UTexture2D* Tex = CachedTex->Get();
	if (!Tex) { return nullptr; }

	// ★ 07.11.2026: логический размер, как у игры: у увеличенных текстур (иконки совела 32 → 64, `DT_AtlasSourceSize`) иначе
	//   ячейка в координатах исходника дала бы четверть картинки.
	const FIntPoint LogicalSize = AsdaUi::LogicalAtlasSize(Tex);
	const float W = (float)LogicalSize.X;
	const float H = (float)LogicalSize.Y;
	if (W <= 0.f || H <= 0.f) { return nullptr; }
	// ★ ЗАКРЫТ пункт item_texture_audit.md §8.6 (формула ПОДТВЕРЖДЕНА эталоном, не выдумана):
	//   сетка ячеек жёстко 8 колонок × 64px (Grid_IndexToCellOffset8x64 @0xEC0050: x=(i%8)<<6, y=(i/8)<<6)
	//   либо 16 × 32px (gridIndexToXY_16col @0xEBFEF0), а UV делятся на РЕАЛЬНУЮ ширину/высоту атласа
	//   из .tex-заголовка (planeFrame_setUV @0x12E3E60 → frameRec+532/+536, которые заполняются
	//   первыми двумя dword'ами .tex — render_core @0x…51301). У s_fish_l_00 это 384×1024 (6 колонок),
	//   поэтому 8-колоночный индекс 14/15 даёт u>1. UI-планы оригинала рисуются с
	//   D3DSAMP_ADDRESSU/V = 1 (D3DTADDRESS_WRAP), т.е. вылет ЗАВОРАЧИВАЕТСЯ по модулю — это и рисует
	//   оригинал. Ширина/высота атласов кратны размеру ячейки, так что регион шов не пересекает.
	const int32 TexWi = FMath::Max(1, (int32)W);
	const int32 TexHi = FMath::Max(1, (int32)H);
	const int32 CellX = ((Icon.CellX % TexWi) + TexWi) % TexWi;
	const int32 CellY = ((Icon.CellY % TexHi) + TexHi) % TexHi;

	TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
	Brush->SetResourceObject(Tex);
	Brush->ImageSize = FVector2D(Icon.CellSize, Icon.CellSize);   // НАТИВНОЕ разрешение ячейки
	Brush->DrawAs = ESlateBrushDrawType::Image;
	Brush->SetUVRegion(FBox2f(
		FVector2f(CellX / W, CellY / H),
		FVector2f((CellX + Icon.CellSize) / W, (CellY + Icon.CellSize) / H)));
	IconBrushes.Add(Key, Brush);
	return Brush.Get();
}

TSharedRef<ITableRow> SAsdaItemBrowser::OnGenerateRow(FNodePtr Node, const TSharedRef<STableViewBase>& Owner)
{
	if (!Node.IsValid())
	{
		return SNew(STableRow<FNodePtr>, Owner)[SNew(STextBlock).Text(FText::FromString(TEXT("—")))];
	}

	if (Node->Kind != EAsdaBrowserNodeKind::Item)
	{
		// Корни и «точное совпадение» подсвечиваем — это навигационные заголовки; ветки грейда — цветом грейда.
		const bool bTop = (Node->Kind == EAsdaBrowserNodeKind::Space || Node->Kind == EAsdaBrowserNodeKind::CodeHit);
		return SNew(STableRow<FNodePtr>, Owner)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Node->Label))
				.Font(FAppStyle::Get().GetFontStyle("PropertyWindow.BoldFont"))
				.ColorAndOpacity(Node->bColored ? FSlateColor(Node->LabelColor)
					: bTop ? FSlateColor(FLinearColor(1.f, 0.82f, 0.42f)) : FSlateColor::UseForeground())
			];
	}

	const int32 Code = Data.IsValid() ? Data->GetCode(Node->ItemIndex) : 0;
	const FString Name = Data.IsValid() ? Data->GetName(Node->ItemIndex) : FString();
	const FAsdaBrowserFacets* Fc = Data.IsValid() ? Data->GetFacets(Node->ItemIndex) : nullptr;
	int32 IconSize = 64;
	const FSlateBrush* Brush = GetIconBrush(Node->ItemIndex, IconSize);

	// ★ ТЗ: высота строки = высоте иконки, иконка в нативном разрешении (не растягиваем).
	TSharedRef<SWidget> IconWidget = Brush
		? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Brush))
		: StaticCastSharedRef<SWidget>(
			SNew(SBox).WidthOverride((float)IconSize).HeightOverride((float)IconSize)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoIconMark", "—"))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.45f, 0.45f)))
				.ToolTipText(LOCTEXT("NoIconTip", "иконки нет (нет атласа в данных или ассет не импортирован)"))
			]);

	// ── Колонка-признак пространства: без неё код 461 не отличить от кода 461 другой таблицы ──
	const FLinearColor SpaceColor =
		Node->Space == EAsdaItemSpace::SowelList   ? FLinearColor(0.55f, 0.85f, 1.00f) :
		Node->Space == EAsdaItemSpace::SpecialList ? FLinearColor(1.00f, 0.72f, 0.55f) :
													 FLinearColor(0.70f, 0.70f, 0.70f);

	// Имя. Пусто = записи имени в данных НЕТ — так и пишем, не выдумываем название. Цвет — грейд, как в игре.
	const bool bNoName = Name.IsEmpty();
	const FString Title = bNoName
		? FString::Printf(TEXT("%d : (имени нет в данных)"), Code)
		: FString::Printf(TEXT("%d : %s"), Code, *Name);
	const FLinearColor NameColor = bNoName ? FLinearColor(0.6f, 0.6f, 0.6f)
		: (Fc ? FAsdaItemBrowserData::GradeColor(Fc->Grade) : FLinearColor::White);
	FString Tail;
	if (Fc)
	{
		Tail = FString::Printf(TEXT("ур. %d"), Fc->LvMin);
		if (Fc->bPremium) { Tail += TEXT("  прем."); }
	}

	return SNew(STableRow<FNodePtr>, Owner)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride((float)IconSize).HeightOverride((float)IconSize)
				[
					IconWidget
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 0.f, 0.f)
			[
				SNew(SBox).WidthOverride(52.f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(AsdaSpaceTag(Node->Space)))
					.ColorAndOpacity(FSlateColor(SpaceColor))
					.ToolTipText(FText::FromString(FString::Printf(
						TEXT("Таблица %s — %s"), AsdaSpaceTable(Node->Space), *AsdaSpaceLabel(Node->Space))))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(6.f, 0.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Title))
				.ColorAndOpacity(FSlateColor(NameColor))
				.ToolTipText(bNoName
					? LOCTEXT("NoNameTip", "В данных клиента имени у этого кода нет (там «-» либо пусто) — как и в оригинале")
					: FText::FromString(Data->GetNameEn(Node->ItemIndex)))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 4.f, 0.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Tail))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
			]
		];
}

void SAsdaItemBrowser::OnSelectionChanged(FNodePtr Node, ESelectInfo::Type)
{
	SelectedIndex = (Node.IsValid() && Node->Kind == EAsdaBrowserNodeKind::Item) ? Node->ItemIndex : INDEX_NONE;
	RefreshPreview();
}

void SAsdaItemBrowser::NavigateToIndex(int32 GlobalIndex)
{
	if (!Data.IsValid() || Data->GetEntry(GlobalIndex) == nullptr) { return; }
	const FString CodeStr = FString::FromInt(Data->GetCode(GlobalIndex));
	if (SearchBox.IsValid()) { SearchBox->SetText(FText::FromString(CodeStr)); }
	Filter.Text = CodeStr;
	RebuildTree();
	for (const FNodePtr& R : RootNodes)
	{
		if (R->Kind != EAsdaBrowserNodeKind::CodeHit) { continue; }
		for (const FNodePtr& C : R->Children)
		{
			if (C->ItemIndex != GlobalIndex) { continue; }
			if (TreeView.IsValid())
			{
				TreeView->SetItemExpansion(R, true);
				TreeView->SetSelection(C, ESelectInfo::Direct);
				TreeView->RequestScrollIntoView(C);
			}
			SelectedIndex = GlobalIndex;
			RefreshPreview();
			return;
		}
	}
	// Запись в дереве не нашлась (не должно быть) — карточку всё равно показываем.
	SelectedIndex = GlobalIndex;
	RefreshPreview();
}

// ── Превью выбранного предмета ──────────────────────────────────────────────
void SAsdaItemBrowser::RefreshPreview()
{
	if (!Viewport.IsValid() || !Data.IsValid()) { return; }
	if (SelectedIndex == INDEX_NONE)
	{
		Viewport->ClearPreview();
		AssetsText.Empty();
		NoMeshNote = TEXT("Предмет не выбран — выберите запись в списке слева.");
		RebuildCard();
		return;
	}

	const FAsdaBrowserItemAssets A = Data->ResolveAssets(SelectedIndex, bMale, GetEnhance());

	FAsdaPreviewRequest Req;
	switch (A.MeshKind)
	{
	case EAsdaBrowserMeshKind::EquipSkeletal:
		Req.SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, *A.MeshObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		break;
	case EAsdaBrowserMeshKind::CharStatic:
	case EAsdaBrowserMeshKind::ItemStatic:
		Req.StaticMesh = LoadObject<UStaticMesh>(nullptr, *A.MeshObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		break;
	default:
		break;
	}
	// Оружие в руке: тот же сдвиг от кости, что даёт UAsdaCharAssembly::AttachWeapon.
	// ★ Гейты по ItemType действуют ТОЛЬКО для item_list: у самоцвета и спец-предмета такого поля нет
	//   (ResolveAssets ставит там −1), поэтому обе ветки автоматически выключаются.
	Req.bWeaponBoneOffset = (A.ItemType == 9 && A.MeshKind == EAsdaBrowserMeshKind::CharStatic);
	// Автоориентация — только для предметов В РУКЕ (их модели авторены в осях игры и ставятся костью).
	Req.bAllowAutoOrient = (A.ItemType == 9 || A.ItemType == 8);
	if (!A.TexObjectPath.IsEmpty())
	{
		Req.Texture = LoadObject<UTexture2D>(nullptr, *A.TexObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	if (!A.GlowBpObjectPath.IsEmpty())
	{
		Req.FxClass = LoadClass<AActor>(nullptr, *A.GlowBpObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	Viewport->SetPreview(Req);
	// ★ 13.09.2026: эффект заточки знает свой предмет — как в игре (SpawnGlow) и в конфигураторе свечения,
	//   иначе переопределения «этот предмет + эффект» в превью браузера не видны
	if (AAsdaEffectActor* Fx = Cast<AAsdaEffectActor>(Viewport->GetFxActor()))
	{
		Fx->SetSourceItemId(A.Space == EAsdaItemSpace::ItemList ? A.ItemCode : 0);
	}

	// ── текст «ассеты» (честно про отсутствующие ассеты) ──
	auto Mark = [](bool bOk) { return bOk ? TEXT("есть") : TEXT("НЕТ"); };
	const TCHAR* KindStr =
		A.MeshKind == EAsdaBrowserMeshKind::EquipSkeletal ? TEXT("SkeletalMesh (EquipParts)") :
		A.MeshKind == EAsdaBrowserMeshKind::CharStatic ? TEXT("StaticMesh (Char)") :
		A.MeshKind == EAsdaBrowserMeshKind::ItemStatic ? TEXT("StaticMesh (Items)") :
		A.MeshKind == EAsdaBrowserMeshKind::Missing ? TEXT("ассет НЕ импортирован") : TEXT("имени модели в данных нет");

	// ── Пояснение поверх пустого вьюпорта: ПОЧЕМУ модели не видно ──
	const bool bHasMesh = (Req.StaticMesh != nullptr || Req.SkeletalMesh != nullptr);
	NoMeshNote.Empty();
	if (!bHasMesh)
	{
		const FString Who = FString::Printf(TEXT("%s %d («%s», таблица %s)"),
			AsdaSpaceTag(A.Space), A.ItemCode,
			A.bHasName ? *A.Name : TEXT("имени нет в данных"), AsdaSpaceTable(A.Space));
		switch (A.MeshKind)
		{
		case EAsdaBrowserMeshKind::Missing:
			NoMeshNote = FString::Printf(
				TEXT("МОДЕЛЬ НЕ ИМПОРТИРОВАНА\n%s\nВ данных указан меш «%s», но ассета в Content/ нет."), *Who, *A.MeshName);
			break;
		case EAsdaBrowserMeshKind::None:
			NoMeshNote = (A.Space == EAsdaItemSpace::SpecialList)
				? FString::Printf(
					TEXT("МОДЕЛИ НЕТ — так и в оригинале\n%s\nУ спец-предмета поле ModelEntry пустое: модель есть только у транспорта, ")
					TEXT("остальные — свитки, зелья и бустеры, у них лишь иконка."), *Who)
				: FString::Printf(TEXT("МОДЕЛИ НЕТ — так и в оригинале\n%s\nВ данных имени меша нет, показывать нечего."), *Who);
			break;
		default:
			NoMeshNote = FString::Printf(
				TEXT("МОДЕЛЬ НЕ ЗАГРУЗИЛАСЬ\n%s\nАссет найден (%s), но объект не открылся — смотри Output Log."),
				*Who, *A.MeshObjectPath);
			break;
		}
	}

	FString S;
	if (A.Space == EAsdaItemSpace::ItemList)
	{
		S += FString::Printf(TEXT("ImgIndex %d · ImgSubType %d\n"), A.ImgIndex, A.ImgSubType);
		// Варианты модели М/Ж — оба имени из img-записи (наличие ассета проверяется у выбранного пола).
		const FAsdaBrowserItemAssets Other = Data->ResolveAssets(SelectedIndex, !bMale, 0);
		const FAsdaBrowserItemAssets& M = bMale ? A : Other;
		const FAsdaBrowserItemAssets& W = bMale ? Other : A;
		S += FString::Printf(TEXT("Модель М: %s — %s\n"), M.MeshName.IsEmpty() ? TEXT("(в данных пусто)") : *M.MeshName,
			M.MeshObjectPath.IsEmpty() ? (M.MeshName.IsEmpty() ? TEXT("—") : TEXT("ассета НЕТ")) : *M.MeshObjectPath);
		S += FString::Printf(TEXT("Модель Ж: %s — %s\n"), W.MeshName.IsEmpty() ? TEXT("(в данных пусто)") : *W.MeshName,
			W.MeshObjectPath.IsEmpty() ? (W.MeshName.IsEmpty() ? TEXT("—") : TEXT("ассета НЕТ")) : *W.MeshObjectPath);
		S += FString::Printf(TEXT("Показана: %s — %s\n"), bMale ? TEXT("мужская") : TEXT("женская"), KindStr);
	}
	else
	{
		S += FString::Printf(TEXT("Модель: %s — %s\n"), A.MeshName.IsEmpty() ? TEXT("(в данных пусто)") : *A.MeshName, KindStr);
		if (!A.MeshObjectPath.IsEmpty()) { S += FString::Printf(TEXT("    %s\n"), *A.MeshObjectPath); }
	}
	S += FString::Printf(TEXT("Текстура: %s — %s\n"), A.TexName.IsEmpty() ? TEXT("(в данных пусто)") : *A.TexName, Mark(!A.TexObjectPath.IsEmpty()));
	S += (A.Space == EAsdaItemSpace::SowelList)
		? FString::Printf(TEXT("Иконка: %s — вся текстура, квадрат 32×32 из угла — %s\n"),
			A.Icon.Atlas.IsEmpty() ? TEXT("(атласа в данных нет)") : *A.Icon.Atlas, Mark(!A.Icon.AtlasObjectPath.IsEmpty()))
		: FString::Printf(TEXT("Иконка: %s ячейка %d (%d px, %d колонок) — %s\n"),
			A.Icon.Atlas.IsEmpty() ? TEXT("(атласа в данных нет)") : *A.Icon.Atlas,
			A.Icon.Cell, A.Icon.CellSize, A.Icon.Cols, Mark(!A.Icon.AtlasObjectPath.IsEmpty()));
	if (!A.BuffIconAtlas.IsEmpty())
	{
		S += FString::Printf(TEXT("Иконка баффа: %s ячейка %d (признак +116 = %d) — %s\n"),
			*A.BuffIconAtlas, A.BuffIconCell, A.BuffFlag116, Mark(!A.BuffIconObjectPath.IsEmpty()));
	}
	if (A.Space != EAsdaItemSpace::ItemList)
	{
		S += TEXT("Эффект заточки: не применим (заточку носят только оружие и щиты из item_list)\n");
	}
	else if (GetEnhance() >= 5)
	{
		if (A.GlowJebName.IsEmpty())
		{
			S += (A.ItemType == 8 || A.ItemType == 9)
				? TEXT("Эффект заточки: нет (в данных нет буквы модели/редкости — как в оригинале)\n")
				: TEXT("Эффект заточки: не применим (свечение только у оружия и щитов)\n");
		}
		else
		{
			S += FString::Printf(TEXT("Эффект заточки: %s → BP_Fx_%s — %s\n"),
				*A.GlowJebName, *A.GlowJebName.Replace(TEXT("-"), TEXT("_")), Mark(!A.GlowBpObjectPath.IsEmpty()));
		}
	}
	else
	{
		S += TEXT("Эффект заточки: выключен (выбери +5/+6/+7 над превью)\n");
	}
	if (!A.AuraName1.IsEmpty() || !A.AuraName2.IsEmpty())
	{
		S += FString::Printf(TEXT("Доп. аура щита (+8): '%s' / '%s'\n"), *A.AuraName1, *A.AuraName2);
	}
	AssetsText = S;
	RebuildCard();
}

// ════════════════════════════════════════════════════════════════════════════
// КАРТОЧКА
// ════════════════════════════════════════════════════════════════════════════

TSharedRef<SWidget> SAsdaItemBrowser::MakeText(const FString& S, const FLinearColor& Color)
{
	return SNew(STextBlock)
		.Text(FText::FromString(S))
		.AutoWrapText(true)
		.Font(CardFont(9))
		.ColorAndOpacity(FSlateColor(Color));
}

void SAsdaItemBrowser::AddSection(const FString& Caption, const TSharedRef<SWidget>& Body)
{
	if (!CardBox.IsValid()) { return; }
	CardBox->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 2.f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(Caption))
		.Font(CardFont(10, true))
		.ColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.82f, 0.42f)))
	];
	CardBox->AddSlot().AutoHeight().Padding(8.f, 0.f, 0.f, 2.f)
	[
		Body
	];
}

TSharedRef<SWidget> SAsdaItemBrowser::MakeFieldGrid(const TArray<TPair<FString, FString>>& Rows)
{
	TSharedRef<SGridPanel> G = SNew(SGridPanel).FillColumn(1, 1.f);
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		G->AddSlot(0, i).Padding(0.f, 1.f, 12.f, 1.f).VAlign(VAlign_Top)
		[
			SNew(STextBlock).Text(FText::FromString(Rows[i].Key)).Font(CardFont(9)).ColorAndOpacity(FSlateColor(FLinearColor(0.62f, 0.62f, 0.62f)))
		];
		G->AddSlot(1, i).Padding(0.f, 1.f)
		[
			SNew(STextBlock).Text(FText::FromString(Rows[i].Value)).Font(CardFont(9)).AutoWrapText(true)
			.ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.9f, 0.9f)))
		];
	}
	return G;
}

TSharedRef<SWidget> SAsdaItemBrowser::MakeLinkList(const TArray<TPair<int32, FString>>& IndexAndSuffix, int32 MaxShown)
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	int32 Shown = 0;
	for (const TPair<int32, FString>& P : IndexAndSuffix)
	{
		if (Shown++ >= MaxShown) { break; }
		const int32 Idx = P.Key;
		const FString Name = Data->GetName(Idx);
		const FAsdaBrowserFacets* Fc = Data->GetFacets(Idx);
		const FString Text = FString::Printf(TEXT("%s %d : %s%s"), AsdaSpaceTag(Data->GetSpace(Idx)), Data->GetCode(Idx),
			Name.IsEmpty() ? TEXT("(имени нет в данных)") : *Name, *P.Value);
		V->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(10.f).HeightOverride(10.f)
				[
					SNew(SImage).Image(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
					.ColorAndOpacity(FSlateColor(Fc ? FAsdaItemBrowserData::GradeColor(Fc->Grade) : FLinearColor::White))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 0.f, 0.f)
			[
				SNew(SHyperlink)
				.Text(FText::FromString(Text))
				.ToolTipText(LOCTEXT("LinkTip", "Перейти к записи"))
				.OnNavigate_Lambda([this, Idx]() { NavigateToIndex(Idx); })
			]
		];
	}
	if (IndexAndSuffix.Num() > MaxShown)
	{
		V->AddSlot().AutoHeight()[ MakeText(FString::Printf(TEXT("… и ещё %d"), IndexAndSuffix.Num() - MaxShown)) ];
	}
	return V;
}

TSharedRef<SWidget> SAsdaItemBrowser::MakeTipPanel(const FAsdaItemCard& Card, const FText& Caption)
{
	// Стиль тела: кегль редактора (в игре — 8 px растрового шрифта; здесь крупнее для чтения), белый по умолчанию.
	FTextBlockStyle Base = FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");
	Base.SetFont(CardFont(10));
	Base.SetColorAndOpacity(FSlateColor(FLinearColor::White));
	Base.SetShadowOffset(FVector2D(1.f, 1.f));
	Base.SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.9f));
	FTextBlockStyle TitleStyle = Base;
	TitleStyle.SetFont(CardFont(11, true));

	// Ширина плашки: у полного построителя эталон держит 250 px жёстко, у прочих — по тексту в пределах MaxW + прибавка.
	const float GameW = (Card.M.FixedWidth > 0.f) ? Card.M.FixedWidth : (Card.M.MaxW + Card.M.WidthAdd);
	const float Scale = 1.45f;   // кегль карточки крупнее игрового 8 px — во столько же раз шире плашка
	const FLinearColor TitleColor = AsdaUi::SrgbU32ToLinear(Card.Tip.TitleArgb);

	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
	if (!Card.bValid)
	{
		Body->AddSlot().AutoHeight()[ MakeText(TEXT("Подсказка не собрана (построитель игры вернул «нет текста» — в игре она тоже не показалась бы)."),
			FLinearColor(1.f, 0.5f, 0.4f)) ];
	}
	else
	{
		Body->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 4.f)
		[
			SNew(SRichTextBlock)
			.Text(FText::FromString(TipToRich(Card.Tip.Title, TitleColor)))
			.TextStyle(&TitleStyle)
			.AutoWrapText(true)
			.Justification(ETextJustify::Center)
			+ SRichTextBlock::Decorator(MakeShared<FAsdaTipRunDecorator>(TitleStyle))
		];
		Body->AddSlot().AutoHeight()
		[
			SNew(SRichTextBlock)
			.Text(FText::FromString(TipToRich(Card.Tip.Body, FLinearColor::White)))
			.TextStyle(&Base)
			.AutoWrapText(true)
			+ SRichTextBlock::Decorator(MakeShared<FAsdaTipRunDecorator>(Base))
		];
		if (Card.M.bPremiumFooter && !Card.M.FooterText.IsEmpty())
		{
			Body->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
				.BorderBackgroundColor(FLinearColor(0.10f, 0.07f, 0.02f, 1.f))
				.Padding(4.f)
				[
					SNew(SRichTextBlock)
					.Text(FText::FromString(TipToRich(Card.M.FooterText, FLinearColor(1.f, 0.85f, 0.5f))))
					.TextStyle(&Base)
					.AutoWrapText(true)
					+ SRichTextBlock::Decorator(MakeShared<FAsdaTipRunDecorator>(Base))
				]
			];
		}
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
		[
			SNew(STextBlock).Text(Caption).Font(CardFont(9, true)).ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).WidthOverride(GameW * Scale)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
				.BorderBackgroundColor(FLinearColor(0.012f, 0.012f, 0.018f, 1.f))
				.Padding(FMargin(10.f, 8.f))
				[
					Body
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
		[
			MakeText(FString::Printf(TEXT("Ширина плашки в игре: %s; построитель: %s"),
				Card.M.FixedWidth > 0.f ? *FString::Printf(TEXT("%.0f px (жёстко)"), Card.M.FixedWidth)
				                        : *FString::Printf(TEXT("по тексту %.0f…%.0f + %.0f px"), Card.M.MinW, Card.M.MaxW, Card.M.WidthAdd),
				Card.Pipeline.IsEmpty() ? TEXT("—") : *Card.Pipeline), FLinearColor(0.5f, 0.5f, 0.5f))
		];
}

void SAsdaItemBrowser::EnsureInBoxesIndex()
{
	if (bInBoxesBuilt) { return; }
	bInBoxesBuilt = true;
	InBoxes.Reset();
	UAsdaItemTableSubsystem* IT = (AsdaItemTipStandalone::Acquire() != nullptr) ? AsdaItemTipStandalone::Items() : nullptr;
	if (!IT || !Data.IsValid()) { return; }
	const double T0 = FPlatformTime::Seconds();
	TArray<FAsdaPackageEntry> Pack;
	TArray<int32> Boost;
	for (int32 i = 0; i < Data->Num(); ++i)
	{
		if (Data->GetSpace(i) != EAsdaItemSpace::ItemList) { continue; }
		const int32 Code = Data->GetCode(i);
		if (IT->GetPackageContents(Code, Pack))
		{
			for (const FAsdaPackageEntry& E : Pack) { if (E.ItemCode > 0) { InBoxes.AddUnique(E.ItemCode, Code); } }
		}
		if (IT->GetBoosterPreview(Code, Boost))
		{
			for (int32 B : Boost) { if (B > 0) { InBoxes.AddUnique(B, Code); } }
		}
	}
	UE_LOG(LogAsdaItemBrowserUI, Log, TEXT("[просмотрщик] индекс «лежит в коробках/бустерах»: %d связей за %.0f мс"),
		InBoxes.Num(), (FPlatformTime::Seconds() - T0) * 1000.0);
}

void SAsdaItemBrowser::RebuildCard()
{
	if (!CardBox.IsValid() || !Data.IsValid()) { return; }
	CardBox->ClearChildren();
	if (SelectedIndex == INDEX_NONE)
	{
		CardBox->AddSlot().AutoHeight()[ MakeText(TEXT("Предмет не выбран. Выберите запись в дереве слева — здесь появится карточка: ")
			TEXT("игровая подсказка RU/EN, характеристики записи, опции, состав коробки, набор, описание и ассеты.")) ];
		return;
	}

	const FAsdaBrowserEntry* E = Data->GetEntry(SelectedIndex);
	const FAsdaBrowserFacets* Fc = Data->GetFacets(SelectedIndex);
	if (!E || !Fc) { return; }
	const int32 Code = E->Code;
	const FString Name = Data->GetName(SelectedIndex);
	const FString NameEn = Data->GetNameEn(SelectedIndex);

	UAsdaItemTooltipSubsystem* Kit = AsdaItemTipStandalone::Acquire();
	UAsdaItemTableSubsystem* IT = Kit ? AsdaItemTipStandalone::Items() : nullptr;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(Code) : nullptr;
	auto L = [](int32 Id) { return AsdaItemTipStandalone::Loc(Id, EAsdaLang::Ru).TrimStartAndEnd(); };

	// ── 1. Шапка ──
	{
		int32 IconSize = 64;
		const FSlateBrush* Brush = GetIconBrush(SelectedIndex, IconSize);
		const FLinearColor GC = FAsdaItemBrowserData::GradeColor(Fc->Grade);
		FString Path = FAsdaItemBrowserData::CatLabel(Fc->Cat);
		const FString Sub = Data->SubLabel(Fc->Cat, Fc->Sub, INDEX_NONE);
		if (!Sub.IsEmpty()) { Path += TEXT(" › ") + Sub; }
		CardBox->AddSlot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
			[
				SNew(SBox).WidthOverride(64.f).HeightOverride(64.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					Brush ? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Brush))
					      : StaticCastSharedRef<SWidget>(MakeText(TEXT("нет\nиконки"), FLinearColor(0.5f, 0.5f, 0.5f)))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(10.f, 0.f, 0.f, 0.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(FText::FromString(Name.IsEmpty() ? FString(TEXT("(имени нет в данных)")) : Name))
					.Font(CardFont(15, true)).ColorAndOpacity(FSlateColor(GC)).AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeText(FString::Printf(TEXT("EN: %s"), NameEn.IsEmpty() ? TEXT("(перевода нет в оверлее L10N/en)") : *NameEn), FLinearColor(0.7f, 0.7f, 0.7f))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
				[
					MakeText(FString::Printf(TEXT("Код %d · таблица %s · %s"), Code, AsdaSpaceTable(E->Space), *Path), FLinearColor(0.85f, 0.85f, 0.85f))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(CardFont(9, true)).ColorAndOpacity(FSlateColor(GC))
					.Text(FText::FromString(FString::Printf(TEXT("%s%s"), *FAsdaItemBrowserData::GradeLabel(Fc->Grade),
						Fc->bPremium ? TEXT(" · ПРЕМИУМ") : TEXT(""))))
				]
			]
		];
	}

	// ── 2. Игровая подсказка RU / EN ──
	{
		AsdaItemTipStandalone::SetViewer(ViewerLevel, ViewerJobIdx, 0);
		const EAsdaItemCardKind Kind = (CardKindIdx == 1) ? EAsdaItemCardKind::Full : (CardKindIdx == 2 ? EAsdaItemCardKind::Simple : EAsdaItemCardKind::Auto);
		FAsdaItemCard Ru, En;
		AsdaItemTipStandalone::BuildCard(Code, EAsdaLang::Ru, Kind, Ru);
		AsdaItemTipStandalone::BuildCard(Code, EAsdaLang::En, Kind, En);

		TSharedRef<SVerticalBox> Tip = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
			[
				SNew(SWrapBox).UseAllottedSize(true)
				+ SWrapBox::Slot().Padding(0.f, 0.f, 6.f, 2.f)
				[ MakeCombo(CardKindOptions, CardKindIdx, [this]() { RebuildCard(); }, LOCTEXT("KindTip",
					"Какой построитель игры: авто — правило витрины cItemBar_BuildSlotTooltip@0xB6F3C0 (полный у брони/щита/оружия/премиума); "
					"полный — ItemTooltip_Build (вкладка «Снаряжение»); простой — CItem_BuildSowelTooltipText (вкладка «Расходники»)")) ]
				+ SWrapBox::Slot().Padding(0.f, 0.f, 6.f, 2.f).VAlign(VAlign_Center)
				[ SNew(STextBlock).Text(LOCTEXT("ViewerLbl", "Персонаж-зритель:")).ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f))) ]
				+ SWrapBox::Slot().Padding(0.f, 0.f, 6.f, 2.f)
				[ MakeCombo(ViewerJobOptions, ViewerJobIdx, [this]() { RebuildCard(); }, LOCTEXT("ViewerJobTip",
					"Класс персонажа, для которого строится подсказка: «Профессия» красится красным, если класс не подходит (правило игры)")) ]
				+ SWrapBox::Slot().Padding(0.f, 0.f, 6.f, 2.f).VAlign(VAlign_Center)
				[ SNew(STextBlock).Text(LOCTEXT("ViewerLvLbl", "уровень")).ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f))) ]
				+ SWrapBox::Slot().Padding(0.f, 0.f, 6.f, 2.f)
				[
					SNew(SBox).WidthOverride(64.f)
					[
						SNew(SSpinBox<int32>).MinValue(0).MaxValue(255)
						.ToolTipText(LOCTEXT("ViewerLvTip", "Уровень персонажа: «Уровень» красится красным при нехватке; у премиум-предмета от него зависит «Уровень возможностей»"))
						.Value_Lambda([this]() { return ViewerLevel; })
						.OnValueChanged_Lambda([this](int32 V) { ViewerLevel = V; })
						.OnValueCommitted_Lambda([this](int32 V, ETextCommit::Type) { ViewerLevel = V; RebuildCard(); })
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SWrapBox).UseAllottedSize(true)
				+ SWrapBox::Slot().Padding(0.f, 0.f, 12.f, 6.f)[ MakeTipPanel(Ru, LOCTEXT("TipRu", "RU — как в игре на русском")) ]
				+ SWrapBox::Slot().Padding(0.f, 0.f, 12.f, 6.f)[ MakeTipPanel(En, LOCTEXT("TipEn", "EN — тот же построитель на английском (оверлеи L10N/en)")) ]
			];
		AddSection(TEXT("Игровая подсказка"), Tip);
	}

	// ── 3. Поля записи ──
	TArray<TPair<FString, FString>> Rows;
	auto Row = [&Rows](const FString& K, const FString& V) { Rows.Add(TPair<FString, FString>(K, V)); };
	const FAsdaItemRow* R = Data->GetItemRow(SelectedIndex);
	if (R)
	{
		Row(TEXT("Категория"), FString::Printf(TEXT("%s › %s"), *FAsdaItemBrowserData::CatLabel(Fc->Cat), *Data->SubLabel(Fc->Cat, Fc->Sub, INDEX_NONE)));
		Row(TEXT("Тип / подтип / вариант"), FString::Printf(TEXT("itemType %d · itemSubType %d · itemVariant %d · SubCategory %d"),
			R->ItemType, R->ItemSubType, R->ItemVariant, R->SubCategory));
		{
			const FString J = Data->JobName(R->UsageCategory);
			Row(TEXT("Класс (UsageCategory)"), FString::Printf(TEXT("%d — %s; %s; в подсказке «%s : %s»"), R->UsageCategory,
				J.IsEmpty() ? TEXT("?") : *J, *Data->JobBranchLabel(FAsdaItemBrowserData::JobBranch(R->UsageCategory)),
				*L(2297), *L(12556 + R->UsageCategory)));
		}
		Row(TEXT("ClassRestriction (+0x58)"), FString::FromInt(R->ClassRestriction));
		Row(TEXT("Требуемый уровень"), FString::Printf(TEXT("%d … %d  (разряд биржи: %s)"), R->RequiredLevel, R->RequiredLevelMax,
			*FAsdaItemBrowserData::TierLabel(Fc->Tier)));
		Row(TEXT("Грейд"), FAsdaItemBrowserData::GradeLabel(R->ItemGrade));
		Row(TEXT("categoryFlag (+0x9C)"), R->CategoryFlag == 3
			? FString(TEXT("3 — премиум/«элитный»: значок премиума, подвал 109832, «Уровень возможностей» по уровню персонажа"))
			: FString::FromInt(R->CategoryFlag));
		Row(TEXT("Цена (item_list +0xA4)"), Num(R->Price));
		if (IT)
		{
			FAsdaPriceRow P;
			if (IT->GetPrice(Code, P))
			{
				Row(TEXT("pricetable"), FString::Printf(TEXT("цена %s · доступно улучшений (dw03) %d · уровень возможностей dw10…dw11 = %d…%d · опции dw06/07, dw08/09 = %d/%d, %d/%d"),
					*Num(P.Price), P.dw03, P.dw10, P.dw11, P.dw06, P.dw07, P.dw08, P.dw09));
			}
		}
		if (Rec)
		{
			Row(TEXT("Вес единицы (+0x48)"), FString::FromInt(Rec->Weight));
			Row(TEXT("Прочность-максимум (+0x44)"), FString::FromInt(Rec->DurabilityMax));
			Row(TEXT("«Эффект ЖК» база (+0x88/+0x8A)"), FString::Printf(TEXT("%d ~ %d"), Rec->StatBaseHi, Rec->StatBaseLo));
			if (Rec->CooldownGroup != 0 || Rec->CooldownMs != 0)
			{
				Row(TEXT("Откат применения"), FString::Printf(TEXT("группа %d, %d мс"), Rec->CooldownGroup, Rec->CooldownMs));
			}
			Row(TEXT("Группы опций (+0x68/+0x74)"), FString::Printf(TEXT("%d / %d"), Rec->OptionGroup0, Rec->OptionGroup1));
		}
		Row(TEXT("Гнёзда ЖК (EnchantOrSocket)"), FString::Printf(TEXT("%d → гнёзд %d (правило подсказки: 4 → 0, иначе +1)"),
			R->EnchantOrSocket, (R->EnchantOrSocket == 4) ? 0 : (R->EnchantOrSocket + 1)));
		Row(TEXT("Передача / продажа"), FString::Printf(TEXT("TradableFlag %d · TooltipFlag200 %d%s · FlagWord176 %d"),
			R->TradableFlag, R->TooltipFlag200,
			R->TooltipFlag200 == 1 ? *FString::Printf(TEXT(" («%s», секция 14 подсказки)"), *L(8503)) : TEXT(""), R->FlagWord176));
		if (R->AttackRangeTiles > 0) { Row(TEXT("Дальность атаки"), FString::Printf(TEXT("%d тайл(ов)"), R->AttackRangeTiles)); }
		Row(TEXT("Связи записи"), FString::Printf(TEXT("SetCode %d · GroupCode %d · LinkCode %d · LinkedGroupCode %d · LinkWordCode %d · DurationOrEnchant %d"),
			R->SetCode, R->GroupCode, R->LinkCode, R->LinkedGroupCode, R->LinkWordCode, R->DurationOrEnchant));
	}
	else if (const FAsdaSowellRow* W = Data->GetSowelRow(SelectedIndex))
	{
		Row(TEXT("Бонус"), FString::Printf(TEXT("тип %d «%s» · величина %d"), W->BonusStatType, *L(8510 + W->BonusStatType), W->BonusValue));
		Row(TEXT("Вид (SpecialFlag)"), FString::Printf(TEXT("%d — %s"), W->SpecialFlag, *L(W->SpecialFlag ? 8585 : 8584)));
		Row(TEXT("Профессия (PartGrade)"), FString::Printf(TEXT("%d — %s"), W->PartGrade, *L(8564 + W->PartGrade)));
		Row(TEXT("Место в снаряжении"), FString::Printf(TEXT("%d / %d — %s"), W->SocketTargetType, W->SocketTargetType2,
			W->SocketTargetType <= 14 ? *L(8569 + W->SocketTargetType) : TEXT("(вне таблицы 8569…)")));
		{
			FString Weapons;
			for (int32 Bit = 0; Bit < 19; ++Bit)
			{
				if ((W->OptionMask >> Bit) & 1) { Weapons += (Weapons.IsEmpty() ? TEXT("") : TEXT(", ")) + L(9455 + Bit); }
			}
			Row(TEXT("Доступное оружие (маска +0x44)"), FString::Printf(TEXT("0x%X%s%s"), W->OptionMask, Weapons.IsEmpty() ? TEXT("") : TEXT(" — "), *Weapons));
		}
		Row(TEXT("Требуемый уровень / грейд"), FString::Printf(TEXT("уровень %d · требуемый грейд %d · цвет грейда %d"), W->RequiredLevel, W->GradeReq, W->GradeColorIndex));
		Row(TEXT("Цена"), FString::Printf(TEXT("продажа %s · покупка %s"), *Num(W->SellPrice), *Num(W->BuyPrice)));
	}
	else if (const FAsdaSpecialItemlistRow* P = Data->GetSpecialRow(SelectedIndex))
	{
		Row(TEXT("Функция (SubType +0x50)"), FString::FromInt(P->SubType));
		Row(TEXT("Уровень предмета (+0x48)"), FString::FromInt(P->ItemLevel));
		Row(TEXT("Вес (+0x44, нотис 6787)"), FString::FromInt(P->RequiredLevel));
		Row(TEXT("Число применений (+0x58)"), FString::FromInt(P->Field88));
		Row(TEXT("Цена (+0x70)"), Num(P->Price));
		Row(TEXT("Состояние передачи (+0x7C)"), FString::Printf(TEXT("%d%s"), P->Flag124,
			P->Flag124 == 0xA ? *FString::Printf(TEXT(" — «%s»"), *L(8401)) : P->Flag124 == 1 ? *FString::Printf(TEXT(" — «%s»"), *L(8503))
			: P->Flag124 == 2 ? *FString::Printf(TEXT(" — «%s»"), *L(8504)) : TEXT("")));
		Row(TEXT("Кулдаун (+0x60)"), FString::Printf(TEXT("%d мс"), P->CooldownBase));
		Row(TEXT("Цвет дропа (+0x128)"), FAsdaItemBrowserData::GradeLabel(P->DropColorIndex));
		Row(TEXT("Прочие поля"), FString::Printf(TEXT("ResultModel %d · Field92 %d · LinkGroupId %d · Model '%s' · Effect '%s'"),
			P->ResultModel, P->Field92, P->LinkGroupId, *P->ModelEntry, *P->EffectEntry));
	}
	AddSection(TEXT("Поля записи"), MakeFieldGrid(Rows));

	// ── 4. Возможные опции (item_list: группы itemoptionrange +0x68/+0x74 — окно «Изменение параметров», JUI 50300) ──
	if (Rec && Kit && (Rec->OptionGroup0 > 0 || Rec->OptionGroup1 > 0))
	{
		FString S;
		FAsdaLangScope Ru(EAsdaLang::Ru);
		for (int32 Slot = 0; Slot < 2; ++Slot)
		{
			const int32 Group = Slot == 0 ? Rec->OptionGroup0 : Rec->OptionGroup1;
			if (Group <= 0) { continue; }
			TArray<FAsdaItemoptionrangeRow> Ranges;
			IT->GetItemOptionRangesByGroup(Group, Ranges);
			S += FString::Printf(TEXT("Гнездо параметра %d (группа %d): %d вариант(ов)\n"), Slot + 1, Group, Ranges.Num());
			for (const FAsdaItemoptionrangeRow& O : Ranges)
			{
				const FString Lo = Kit->FormatOptionValue(O.OptionType, O.MinValue);
				const FString Hi = Kit->FormatOptionValue(O.OptionType, O.MaxValue);
				// Диапазон из одного значения печатаем один раз.
				S += (O.MinValue == O.MaxValue)
					? FString::Printf(TEXT("   • %s   (тип %d, значение %d)\n"), Lo.IsEmpty() ? TEXT("?") : *Lo, O.OptionType, O.MinValue)
					: FString::Printf(TEXT("   • %s   …   %s   (тип %d, %d…%d)\n"),
						Lo.IsEmpty() ? TEXT("?") : *Lo, Hi.IsEmpty() ? TEXT("?") : *Hi, O.OptionType, O.MinValue, O.MaxValue);
			}
		}
		AddSection(TEXT("Возможные параметры (itemoptionrange)"), MakeText(S.TrimEnd()));
	}

	// ── 5. Связи: состав коробки/бустера, где лежит, родственные записи ──
	if (IT && R)
	{
		TArray<FAsdaPackageEntry> Pack;
		if (IT->GetPackageContents(Code, Pack) && Pack.Num() > 0)
		{
			TArray<TPair<int32, FString>> Links;
			for (const FAsdaPackageEntry& PE : Pack)
			{
				const int32 Idx = Data->IndexByCode(PE.ItemCode);
				if (Idx != INDEX_NONE) { Links.Add(TPair<int32, FString>(Idx, FString::Printf(TEXT("  ×%d"), PE.Amount))); }
			}
			AddSection(FString::Printf(TEXT("Состав коробки/пакета (packagetable, строка %d): %d поз."), R->DurationOrEnchant, Pack.Num()), MakeLinkList(Links));
		}
		TArray<int32> Boost;
		if (IT->GetBoosterPreview(Code, Boost) && Boost.Num() > 0)
		{
			TArray<TPair<int32, FString>> Links;
			for (int32 B : Boost) { const int32 Idx = Data->IndexByCode(B); if (Idx != INDEX_NONE) { Links.Add(TPair<int32, FString>(Idx, FString())); } }
			AddSection(FString::Printf(TEXT("Содержимое бустера (booster_list): %d"), Boost.Num()), MakeLinkList(Links));
		}
	}
	{
		EnsureInBoxesIndex();
		TArray<int32> Boxes;
		InBoxes.MultiFind(Code, Boxes);
		if (Boxes.Num() > 0)
		{
			Boxes.Sort();
			TArray<TPair<int32, FString>> Links;
			for (int32 B : Boxes) { const int32 Idx = Data->IndexByCodeInSpace(EAsdaItemSpace::ItemList, B); if (Idx != INDEX_NONE) { Links.Add(TPair<int32, FString>(Idx, FString())); } }
			AddSection(FString::Printf(TEXT("Лежит в коробках/бустерах: %d"), Links.Num()), MakeLinkList(Links));
		}
	}
	{
		TArray<int32> Sib;
		Data->SetSiblings(SelectedIndex, Sib);
		if (Sib.Num() > 0)
		{
			TArray<TPair<int32, FString>> Links;
			for (int32 S : Sib) { Links.Add(TPair<int32, FString>(S, FString())); }
			AddSection(FString::Printf(TEXT("Родственные записи — тот же SetCode %d (код набора/родственных записей): %d"), R ? R->SetCode : 0, Sib.Num()),
				MakeLinkList(Links, 60));
		}
	}

	// ── 6. Описание RU / EN (сырой текст таблицы описаний своего пространства) ──
	{
		const FString DescRu = Data->GetDescription(E->Space, Code);
		const FString DescEn = Data->GetDescriptionEn(E->Space, Code);
		AddSection(FString::Printf(TEXT("Описание (%s)"), FAsdaItemBrowserData::DescriptionTableName(E->Space)),
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ MakeText(FString::Printf(TEXT("RU: %s"), DescRu.IsEmpty() ? TEXT("(записи нет)") : *FAsdaMarkup::StripTags(DescRu))) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)[ MakeText(FString::Printf(TEXT("EN: %s"), DescEn.IsEmpty() ? TEXT("(перевода нет)") : *FAsdaMarkup::StripTags(DescEn)), FLinearColor(0.75f, 0.75f, 0.75f)) ]);
	}

	// ── 7. Ассеты ──
	AddSection(TEXT("Ассеты проекта"), MakeText(AssetsText.TrimEnd()));
}

#undef LOCTEXT_NAMESPACE
