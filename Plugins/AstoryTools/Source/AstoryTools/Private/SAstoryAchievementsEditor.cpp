// Copyright (c) 2026 @sitxovski (Discord)
// Реализация вкладки «Редактор достижений Asda» — см. SAstoryAchievementsEditor.h.
#include "SAstoryAchievementsEditor.h"
#include "AstoryServerExport.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Brushes/SlateColorBrush.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/PlatformApplicationMisc.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"

#define LOCTEXT_NAMESPACE "AstoryAchievements"

DEFINE_LOG_CATEGORY_STATIC(LogAstoryAchUI, Log, All);

// ═════════════════════════════════════════════════════════════════════════════
// Общее оформление
// ═════════════════════════════════════════════════════════════════════════════
namespace AstoryUi
{
	// Секция ini для запоминания состояния между запусками.
	static const TCHAR* IniSection = TEXT("AstoryTools.AchievementsEditor");

	// Стиль строки с чередованием: берём штатный стиль таблицы редактора и подменяем ТОЛЬКО
	//   фон чётных/нечётных строк цветами из ТЕМЫ (свой скин не заводим).
	static const FTableRowStyle& AltRowStyle()
	{
		static FTableRowStyle Style = []()
		{
			FTableRowStyle S = FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row");
			FSlateColorBrush Even(FLinearColor::White);
			Even.TintColor = FStyleColors::Recessed;
			FSlateColorBrush Odd(FLinearColor::White);
			Odd.TintColor = FStyleColors::Panel;
			S.SetEvenRowBackgroundBrush(Even);
			S.SetOddRowBackgroundBrush(Odd);
			return S;
		}();
		return Style;
	}

	// ── цвета состояний: только из палитры темы, чтобы жить и в тёмной, и в светлой ──
	static FSlateColor ColorNormal()      { return FStyleColors::Foreground; }
	static FSlateColor ColorMuted()       { return FStyleColors::White25; }
	static FSlateColor ColorUnreachable() { return FStyleColors::AccentPurple; }
	static FSlateColor ColorMismatch()    { return FStyleColors::Warning; }
	static FSlateColor ColorNoServer()    { return FStyleColors::AccentBlue; }
	static FSlateColor ColorError()       { return FStyleColors::Error; }

	static const FSlateBrush* IconWarn()  { return FAppStyle::GetBrush("Icons.Warning"); }
	static const FSlateBrush* IconError() { return FAppStyle::GetBrush("Icons.Error"); }
	static const FSlateBrush* IconInfo()  { return FAppStyle::GetBrush("Icons.Info"); }
	static const FSlateBrush* IconLock()  { return FAppStyle::GetBrush("Icons.Lock"); }

	// Колонки таблицы, показываемые всегда, — по смещению поля в записи.
	static const int32 ListColumnOffsets[] =
	{
		AstoryTitleOffsets::Id,
		AstoryTitleOffsets::SortNum,
		AstoryTitleOffsets::Name,
		AstoryTitleOffsets::CategoryLocIndex,
		AstoryTitleOffsets::ColorIndex,
		AstoryTitleOffsets::Group,
		AstoryTitleOffsets::Position,
		AstoryTitleOffsets::ClassReq,
		AstoryTitleOffsets::Point,
		AstoryTitleOffsets::Enabled
	};

	static bool IsListColumn(int32 Offset)
	{
		for (int32 O : ListColumnOffsets)
		{
			if (O == Offset) { return true; }
		}
		return false;
	}

	// Имя колонки для поля: стабильное (CsvName), иначе — по смещению.
	static FName ColumnIdFor(const FAstoryTitleFieldDef& F)
	{
		return F.CsvName ? FName(F.CsvName) : FName(*FString::Printf(TEXT("Off%d"), F.Offset));
	}

	static const FName ColStatus(TEXT("__Status"));
	static const FName ColServerPoint(TEXT("__SrvPoint"));

	// Значок «описание обрезано» — отдельный цвет, чтобы не путать
	//   с расхождением (жёлтый) и с недостижимостью (фиолетовый).
	static FSlateColor ColorTruncDesc() { return FStyleColors::AccentOrange; }
}

// ═════════════════════════════════════════════════════════════════════════════
// Строка таблицы
// ═════════════════════════════════════════════════════════════════════════════
class SAstoryTitleTableRow : public SMultiColumnTableRow<FAstoryRowPtr>
{
public:
	SLATE_BEGIN_ARGS(SAstoryTitleTableRow) {}
		SLATE_ARGUMENT(FAstoryRowPtr, Item)
		SLATE_ARGUMENT(FAstoryTitleData*, Data)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& Owner)
	{
		Item = InArgs._Item;
		Data = InArgs._Data;
		FSuperRowType::FArguments Args;
		Args.Style(&AstoryUi::AltRowStyle());
		SMultiColumnTableRow<FAstoryRowPtr>::Construct(Args, Owner);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnId) override
	{
		if (!Item.IsValid() || !Data || !Data->GetRecords().IsValidIndex(Item->RecordIndex))
		{
			return SNullWidget::NullWidget;
		}
		const FAstoryTitleRecord& Rec = Data->GetRecords()[Item->RecordIndex];

		if (ColumnId == AstoryUi::ColStatus)
		{
			return BuildStatusCell();
		}
		if (ColumnId == AstoryUi::ColServerPoint)
		{
			const FAstoryFameServerRow* Srv = Data->GetServerRows().Find(Rec.GetId());
			const bool bHas = Srv && Srv->Cols.IsValidIndex(42);
			return MakeCell(bHas ? FText::AsNumber(Srv->Cols[42]) : FText::FromString(TEXT("—")),
				HAlign_Right,
				bHas ? (Item->bMismatch ? AstoryUi::ColorMismatch() : TextColor()) : AstoryUi::ColorMuted());
		}

		// Поле записи по имени колонки.
		for (const FAstoryTitleFieldDef& F : AstoryTitleFields())
		{
			if (AstoryUi::ColumnIdFor(F) != ColumnId)
			{
				continue;
			}
			if (F.Type == EAstoryFieldType::String)
			{
				FText Txt = FText::FromString(Rec.GetStr(F.Offset));
				return MakeCell(Txt, HAlign_Left, TextColor());
			}
			const int32 V = Rec.GetInt(F.Offset);
			if (F.Enum != EAstoryFieldEnum::None)
			{
				return MakeCell(EnumText(F.Enum, V), HAlign_Left,
					F.bIdentified ? TextColor() : AstoryUi::ColorMuted());
			}
			return MakeCell(FText::AsNumber(V), HAlign_Right,
				F.bIdentified ? TextColor() : AstoryUi::ColorMuted());
		}
		return SNullWidget::NullWidget;
	}

private:
	FSlateColor TextColor() const
	{
		if (Item->bError)    { return AstoryUi::ColorError(); }
		if (Item->bDisabled) { return AstoryUi::ColorMuted(); }
		return AstoryUi::ColorNormal();
	}

	TSharedRef<SWidget> MakeCell(const FText& Text, EHorizontalAlignment HAlign, FSlateColor Color) const
	{
		return SNew(SBox)
			.VAlign(VAlign_Center)
			.HAlign(HAlign)
			.Padding(FMargin(6.f, 2.f))
			[
				SNew(STextBlock)
				.Text(Text)
				.ColorAndOpacity(Color)
				.ToolTipText(Text)
			];
	}

	FText EnumText(EAstoryFieldEnum Enum, int32 V) const;

	TSharedRef<SWidget> BuildStatusCell()
	{
		TSharedRef<SHorizontalBox> Icons = SNew(SHorizontalBox);
		auto AddIcon = [&Icons](const FSlateBrush* Brush, FSlateColor Color, const FText& Tip)
		{
			Icons->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(1.f, 0.f)
			[
				SNew(SBox).WidthOverride(16.f).HeightOverride(16.f)
				[
					SNew(SImage).Image(Brush).ColorAndOpacity(Color).ToolTipText(Tip)
				]
			];
		};
		if (Item->bUnreachable)
		{
			AddIcon(AstoryUi::IconLock(), AstoryUi::ColorUnreachable(),
				LOCTEXT("TipUnreachable",
					"НЕДОСТИЖИМОЕ ЗВАНИЕ.\nid ≥ 256, а маска «получено» в протоколе (опкоды 6066/6086) "
					"ровно 256 бит — сервер физически не может отметить такой бит.\n"
					"Игрок это звание не получит никогда."));
		}
		if (Item->bError)
		{
			AddIcon(AstoryUi::IconError(), AstoryUi::ColorError(),
				FText::FromString(Item->StatusTip));
		}
		if (Item->bMismatch)
		{
			AddIcon(AstoryUi::IconWarn(), AstoryUi::ColorMismatch(),
				LOCTEXT("TipMismatch", "Значение расходится с серверным FameInfo.txt (авторитет — сервер)."));
		}
		if (Item->bNoServerRow)
		{
			AddIcon(AstoryUi::IconInfo(), AstoryUi::ColorNoServer(),
				LOCTEXT("TipNoServer", "У сервера нет строки с таким id — звание существует только у клиента."));
		}
		if (Item->bTruncDesc)
		{
			AddIcon(AstoryUi::IconWarn(), AstoryUi::ColorTruncDesc(),
				LOCTEXT("TipTrunc",
					"ОПИСАНИЕ ОБРЕЗАНО В ДАННЫХ.\nЛибо текст занял всё поле (64 байта), либо строку "
					"обрезал конвертер, принявший букву «я» за конец строки: в шифре «я» — это байт "
					"0x00, а настоящий терминатор 0x01.\nЭто дефект самого title_list.bin, а не отображения."));
		}
		return Icons;
	}

	FAstoryRowPtr Item;
	FAstoryTitleData* Data = nullptr;
};

// Подписи перечислений (общие для строки таблицы и карточки).
static FText AstoryEnumText(const FAstoryTitleData* Data, EAstoryFieldEnum Enum, int32 V)
{
	auto WithLoc = [Data](int32 LocId, int32 Value) -> FText
	{
		bool bHas = false;
		const FString S = Data ? Data->Loc(LocId, &bHas) : FString();
		return bHas
			? FText::FromString(FString::Printf(TEXT("%d — %s"), Value, *S))
			: FText::FromString(FString::Printf(TEXT("%d — ★ нет строки %d"), Value, LocId));
	};

	switch (Enum)
	{
	case EAstoryFieldEnum::Grade:    return WithLoc(1450 + V, V);
	case EAstoryFieldEnum::Category: return WithLoc(1460 + V, V);
	case EAstoryFieldEnum::Position: return WithLoc(V == 0 ? 1466 : 1467, V);
	case EAstoryFieldEnum::Group:    return WithLoc(2405 + FMath::Clamp(V, 0, 2), V);
	case EAstoryFieldEnum::ClassReq:
		switch (V)
		{
		case 0:  return LOCTEXT("Cls0", "0 — любой класс");
		case 1:  return LOCTEXT("Cls1", "1 — class ≤ 4");
		case 2:  return LOCTEXT("Cls2", "2 — class 12…15");
		case 3:  return LOCTEXT("Cls3", "3 — class 23…26");
		default: return FText::FromString(FString::Printf(TEXT("%d — ★ вне 0…3"), V));
		}
	case EAstoryFieldEnum::Enabled:
		return V == 0 ? LOCTEXT("En0", "0 — не показывать")
		              : FText::FromString(FString::Printf(TEXT("%d — показывать"), V));
	default: return FText::AsNumber(V);
	}
}

FText SAstoryTitleTableRow::EnumText(EAstoryFieldEnum Enum, int32 V) const
{
	return AstoryEnumText(Data, Enum, V);
}

// ═════════════════════════════════════════════════════════════════════════════
// Строка панели проблем
// ═════════════════════════════════════════════════════════════════════════════
static FText IssueKindText(EAstoryIssueKind K)
{
	switch (K)
	{
	case EAstoryIssueKind::DuplicateId:   return LOCTEXT("IkDup", "дубль id");
	case EAstoryIssueKind::EmptyName:     return LOCTEXT("IkName", "пустое имя");
	case EAstoryIssueKind::BadCategory:   return LOCTEXT("IkCat", "категория вне диапазона");
	case EAstoryIssueKind::MissingLoc:    return LOCTEXT("IkLoc", "нет строки локализации");
	case EAstoryIssueKind::UnreachableId: return LOCTEXT("IkUnr", "недостижимый id");
	case EAstoryIssueKind::TruncatedDesc: return LOCTEXT("IkTrunc", "описание обрезано");
	case EAstoryIssueKind::IdOutOfRange:  return LOCTEXT("IkId", "id вне диапазона");
	case EAstoryIssueKind::ServerMissing: return LOCTEXT("IkSrvM", "нет у сервера");
	case EAstoryIssueKind::ServerMismatch:return LOCTEXT("IkSrvD", "расхождение с сервером");
	default: return FText::GetEmpty();
	}
}

static FSlateColor IssueColor(const FAstoryIssue& I)
{
	if (I.IsBlocking())                                     { return AstoryUi::ColorError(); }
	if (I.Kind == EAstoryIssueKind::UnreachableId)          { return AstoryUi::ColorUnreachable(); }
	if (I.Kind == EAstoryIssueKind::ServerMissing)          { return AstoryUi::ColorNoServer(); }
	if (I.Kind == EAstoryIssueKind::TruncatedDesc)          { return AstoryUi::ColorTruncDesc(); }
	return AstoryUi::ColorMismatch();
}

// ═════════════════════════════════════════════════════════════════════════════
// Конструирование
// ═════════════════════════════════════════════════════════════════════════════
void SAstoryAchievementsEditor::Construct(const FArguments& InArgs)
{
	OwnerTab = InArgs._OwnerTab;
	Data = MakeShared<FAstoryTitleData>();

	LoadUiState();

	ChildSlot
	[
		SNew(SVerticalBox)

		// ── панель инструментов ──
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
			.Padding(FMargin(8.f, 6.f))
			[
				BuildToolbar()
			]
		]

		// ── сводка ──
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
			.Padding(FMargin(8.f, 4.f))
			[
				BuildSummaryBar()
			]
		]

		// ── три зоны ──
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SAssignNew(MainSplitter, SSplitter)
			.Orientation(Orient_Horizontal)

			// ★ Доли зон привязаны к полям и сохраняются в ini — пропорции переживают перезапуск.
			+ SSplitter::Slot()
			.Value(TAttribute<float>::CreateLambda([this]() { return SplitFilters; }))
			.OnSlotResized(SSplitter::FOnSlotResized::CreateLambda([this](float V) { SplitFilters = V; }))
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
				.Padding(6.f)
				[
					BuildFilterPanel()
				]
			]

			+ SSplitter::Slot()
			.Value(TAttribute<float>::CreateLambda([this]() { return SplitCenter; }))
			.OnSlotResized(SSplitter::FOnSlotResized::CreateLambda([this](float V) { SplitCenter = V; }))
			[
				SNew(SSplitter)
				.Orientation(Orient_Vertical)

				+ SSplitter::Slot()
				.Value(TAttribute<float>::CreateLambda([this]() { return SplitTable; }))
				.OnSlotResized(SSplitter::FOnSlotResized::CreateLambda([this](float V) { SplitTable = V; }))
				[
					BuildTable()
				]
				+ SSplitter::Slot()
				.Value(TAttribute<float>::CreateLambda([this]() { return SplitIssues; }))
				.OnSlotResized(SSplitter::FOnSlotResized::CreateLambda([this](float V) { SplitIssues = V; }))
				[
					BuildIssuesPanel()
				]
			]

			+ SSplitter::Slot()
			.Value(TAttribute<float>::CreateLambda([this]() { return SplitCard; }))
			.OnSlotResized(SSplitter::FOnSlotResized::CreateLambda([this](float V) { SplitCard = V; }))
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
				.Padding(6.f)
				[
					BuildCard()
				]
			]
		]

		// ── строка состояния ──
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
			.Padding(FMargin(8.f, 4.f))
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(AstoryUi::ColorMuted())
				.Text_Lambda([this]() { return FText::FromString(StatusLine); })
			]
		]
	];

	// Восстановленный из ini текст поиска надо ПОКАЗАТЬ в поле: иначе фильтр применён,
	//   а поле выглядит пустым — и список кажется поломанным.
	if (SearchBox.IsValid() && !SearchText.IsEmpty())
	{
		SearchBox->SetText(FText::FromString(SearchText));
	}

	ReloadAll();
}

SAstoryAchievementsEditor::~SAstoryAchievementsEditor()
{
	SaveUiState();
}

// ═════════════════════════════════════════════════════════════════════════════
// Панель инструментов
// ═════════════════════════════════════════════════════════════════════════════
TSharedRef<SWidget> SAstoryAchievementsEditor::BuildToolbar()
{
	auto MakeButton = [this](const FText& Label, const FText& Tip, FOnClicked OnClick,
	                         TFunction<bool()> Enabled) -> TSharedRef<SWidget>
	{
		return SNew(SButton)
			.Text(Label)
			.ToolTipText(Tip)
			.OnClicked(OnClick)
			.IsEnabled_Lambda([Enabled]() { return Enabled ? Enabled() : true; })
			.ContentPadding(FMargin(10.f, 3.f));
	};

	TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);

	Box->AddSlot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
	[
		MakeButton(LOCTEXT("BtnSave", "Сохранить (Ctrl+S)"),
			LOCTEXT("BtnSaveTip",
				"Записать title_list.csv и .json (с резервной копией) и переимпортировать DT_title_list.\n"
				"Перед записью выполняется проверка: дубли id, пустые имена, битые ссылки локализации, "
				"категория вне диапазона."),
			FOnClicked::CreateSP(this, &SAstoryAchievementsEditor::OnSaveClicked),
			[this]() { return bDirty; })
	];
	Box->AddSlot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
	[
		MakeButton(LOCTEXT("BtnUndo", "Отменить (Ctrl+Z)"),
			LOCTEXT("BtnUndoTip", "Отменить последнее изменение."),
			FOnClicked::CreateSP(this, &SAstoryAchievementsEditor::OnUndoClicked),
			[this]() { return UndoStack.Num() > 0; })
	];
	Box->AddSlot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)
	[
		MakeButton(LOCTEXT("BtnReload", "Перечитать"),
			LOCTEXT("BtnReloadTip", "Перечитать все источники с диска, потеряв несохранённые правки."),
			FOnClicked::CreateSP(this, &SAstoryAchievementsEditor::OnReloadClicked), nullptr)
	];

	Box->AddSlot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
	[
		MakeButton(LOCTEXT("BtnAdd", "Добавить"),
			LOCTEXT("BtnAddTip",
				"Создать новую запись копией выделенной.\n"
				"★ Свободных id ниже 256 нет ни одного — новая запись получит id ≥ 256 и будет "
				"НЕДОСТИЖИМОЙ. Редактор об этом предупредит."),
			FOnClicked::CreateSP(this, &SAstoryAchievementsEditor::OnAddRecordClicked), nullptr)
	];
	Box->AddSlot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
	[
		MakeButton(LOCTEXT("BtnDel", "Удалить"),
			LOCTEXT("BtnDelTip", "Удалить выделенные записи."),
			FOnClicked::CreateSP(this, &SAstoryAchievementsEditor::OnDeleteRecordClicked),
			[this]() { return ListView.IsValid() && ListView->GetNumItemsSelected() > 0; })
	];
	Box->AddSlot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)
	[
		MakeButton(LOCTEXT("BtnBulk", "Массовая правка"),
			LOCTEXT("BtnBulkTip", "Задать одно поле сразу всем выделенным записям."),
			FOnClicked::CreateSP(this, &SAstoryAchievementsEditor::OnBulkEditClicked),
			[this]() { return ListView.IsValid() && ListView->GetNumItemsSelected() > 0; })
	];

	Box->AddSlot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
	[
		MakeButton(LOCTEXT("BtnExport", "Экспорт выборки в CSV"),
			LOCTEXT("BtnExportTip", "Сохранить видимые (или выделенные) записи в отдельный CSV — с подписями и состоянием."),
			FOnClicked::CreateSP(this, &SAstoryAchievementsEditor::OnExportSelectionClicked), nullptr)
	];
	Box->AddSlot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
	[
		MakeButton(LOCTEXT("BtnSrvExport", "Выгрузка для сервера"),
			LOCTEXT("BtnSrvExportTip",
				"Разложить готовый FameInfo.txt во временную папку зеркалом серверных путей и открыть её.\n"
				"⚠ На живой сервер ничего не пишется — подмену делает владелец."),
			FOnClicked::CreateSP(this, &SAstoryAchievementsEditor::OnServerExportClicked), nullptr)
	];
	Box->AddSlot().AutoWidth()
	[
		MakeButton(LOCTEXT("BtnRt", "Самопроверка"),
			LOCTEXT("BtnRtTip",
				"1) Разобрать title_list.bin и собрать обратно БЕЗ правок — результат обязан совпасть "
				"с оригиналом побайтово (доказательство, что формат понят верно).\n"
				"2) Заново собрать CSV и JSON и сверить с файлами на диске — доказательство, что "
				"сохранение вхолостую ничего не портит.\n"
				"То же доступно командой консоли Astory.Titles.SelfCheck."),
			FOnClicked::CreateSP(this, &SAstoryAchievementsEditor::OnRoundTripTestClicked), nullptr)
	];

	return Box;
}

// ═════════════════════════════════════════════════════════════════════════════
// Сводка
// ═════════════════════════════════════════════════════════════════════════════
TSharedRef<SWidget> SAstoryAchievementsEditor::BuildSummaryBar()
{
	auto Link = [this](TFunction<FText()> TextGetter, FSlateColor Color, TAttribute<FText> Tip,
	                   TFunction<void()> OnClick) -> TSharedRef<SWidget>
	{
		return SNew(SHyperlink)
			.Text_Lambda([TextGetter]() { return TextGetter(); })
			.ToolTipText(Tip)
			.OnNavigate_Lambda([OnClick]() { OnClick(); })
			.Style(&FAppStyle::Get().GetWidgetStyle<FHyperlinkStyle>("Hyperlink"));
	};

	auto ResetFlags = [this]()
	{
		bOnlyMismatch = bOnlyUnreachable = bOnlyErrors = bOnlyNoServer = bOnlyTruncDesc = false;
	};

	return SNew(SHorizontalBox)

	+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 16.f, 0.f)
	[
		Link([this]() { return FText::FromString(FString::Printf(TEXT("Всего: %d"), CountTotal)); },
			AstoryUi::ColorNormal(),
			LOCTEXT("SumTotalTip", "Все записи title_list. Клик — снять все фильтры."),
			[this, ResetFlags]()
			{
				ResetFlags();
				FilterCategory = FilterGroup = FilterClass = FilterGrade = FilterEnabled = -1;
				SearchText.Empty();
				if (SearchBox.IsValid()) { SearchBox->SetText(FText::GetEmpty()); }
				RefreshView();
			})
	]

	+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 16.f, 0.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SBox).WidthOverride(14.f).HeightOverride(14.f)
			[
				SNew(SImage).Image(AstoryUi::IconWarn()).ColorAndOpacity(AstoryUi::ColorMismatch())
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			Link([this]() { return FText::FromString(FString::Printf(TEXT("Расхождений с сервером: %d"), CountMismatch)); },
				AstoryUi::ColorMismatch(),
				TAttribute<FText>::CreateLambda([this]()
				{
					return FText::FromString(FString::Printf(
						TEXT("Записи, у которых сопоставленное поле не совпадает с серверным FameInfo.txt.\n")
						TEXT("По полям: %s.\n")
						TEXT("У одной записи расхождений бывает несколько, поэтому сумма больше числа строк.\n")
						TEXT("Авторитет данных — сервер.\nКлик — показать только эти записи."),
						*MismatchBreakdown));
				}),
				[this, ResetFlags]() { ResetFlags(); bOnlyMismatch = true; RefreshView(); })
		]
	]

	+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 16.f, 0.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SBox).WidthOverride(14.f).HeightOverride(14.f)
			[
				SNew(SImage).Image(AstoryUi::IconInfo()).ColorAndOpacity(AstoryUi::ColorNoServer())
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			Link([this]() { return FText::FromString(FString::Printf(TEXT("Нет у сервера: %d"), CountNoServer)); },
				AstoryUi::ColorNoServer(),
				LOCTEXT("SumNoSrvTip", "Клиентские id, для которых серверной строки не существует. Клик — показать только их."),
				[this, ResetFlags]() { ResetFlags(); bOnlyNoServer = true; RefreshView(); })
		]
	]

	+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 16.f, 0.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SBox).WidthOverride(14.f).HeightOverride(14.f)
			[
				SNew(SImage).Image(AstoryUi::IconLock()).ColorAndOpacity(AstoryUi::ColorUnreachable())
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			Link([this]() { return FText::FromString(FString::Printf(TEXT("Недостижимых (id ≥ 256): %d"), CountUnreachable)); },
				AstoryUi::ColorUnreachable(),
				LOCTEXT("SumUnrTip",
					"Маска «получено» в протоколе ровно 256 бит ⇒ звания с id ≥ 256 получить нельзя.\n"
					"★ Свободных id ниже 256 нет ни одного — все 256 заняты.\nКлик — показать только их."),
				[this, ResetFlags]() { ResetFlags(); bOnlyUnreachable = true; RefreshView(); })
		]
	]

	+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SBox).WidthOverride(14.f).HeightOverride(14.f)
			[
				SNew(SImage).Image(AstoryUi::IconError()).ColorAndOpacity(AstoryUi::ColorError())
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			Link([this]() { return FText::FromString(FString::Printf(TEXT("С ошибками: %d"), CountErrors)); },
				AstoryUi::ColorError(),
				LOCTEXT("SumErrTip", "Записи с блокирующими ошибками — сохранить не дадут. Клик — показать только их."),
				[this, ResetFlags]() { ResetFlags(); bOnlyErrors = true; RefreshView(); })
		]
	]

	+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(16.f, 0.f, 0.f, 0.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SBox).WidthOverride(14.f).HeightOverride(14.f)
			[
				SNew(SImage).Image(AstoryUi::IconWarn()).ColorAndOpacity(AstoryUi::ColorTruncDesc())
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			Link([this]() { return FText::FromString(FString::Printf(TEXT("Описаний обрезано: %d"), CountTruncDesc)); },
				AstoryUi::ColorTruncDesc(),
				LOCTEXT("SumTruncTip",
					"Дефект данных: описание в title_list.bin обрезано. Клик — показать только их."),
				[this, ResetFlags]() { ResetFlags(); bOnlyTruncDesc = true; RefreshView(); })
		]
	]

	+ SHorizontalBox::Slot().FillWidth(1.f)
	[
		SNew(SSpacer)
	]

	+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
	[
		SNew(STextBlock)
		.ColorAndOpacity(AstoryUi::ColorMuted())
		.Text_Lambda([this]()
		{
			const FString P = Data.IsValid() ? Data->GetServerFilePath() : FString();
			return P.IsEmpty()
				? LOCTEXT("SrvNone", "сервер: недоступен")
				: FText::FromString(FString::Printf(TEXT("сервер: %s (строк %d)"),
					*P, Data->GetServerRows().Num()));
		})
		.ToolTipText(LOCTEXT("SrvTip",
			"Серверная таблица FameInfo.txt открыта ТОЛЬКО НА ЧТЕНИЕ. Приоритет: живой сервер, "
			"затем локальные копии."))
	];
}

// ═════════════════════════════════════════════════════════════════════════════
// Панель фильтров
// ═════════════════════════════════════════════════════════════════════════════
TSharedRef<SWidget> SAstoryAchievementsEditor::BuildFilterPanel()
{
	// Выпадающий фильтр по перечислению: «все» + перечисленные значения.
	auto EnumFilter = [this](const FText& Title, EAstoryFieldEnum Enum, int32 MaxValue,
	                         int32* Target) -> TSharedRef<SWidget>
	{
		return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
		[
			SNew(STextBlock).Text(Title).ColorAndOpacity(AstoryUi::ColorMuted())
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SComboButton)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text_Lambda([this, Enum, Target]()
				{
					return *Target < 0 ? LOCTEXT("FilterAll", "все")
					                   : AstoryEnumText(Data.Get(), Enum, *Target);
				})
			]
			.OnGetMenuContent_Lambda([this, Enum, MaxValue, Target]()
			{
				FMenuBuilder MB(true, nullptr);
				MB.AddMenuEntry(LOCTEXT("FilterAll", "все"), FText::GetEmpty(), FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([this, Target]()
					{ *Target = -1; RefreshView(); })));
				for (int32 V = 0; V <= MaxValue; ++V)
				{
					MB.AddMenuEntry(AstoryEnumText(Data.Get(), Enum, V), FText::GetEmpty(), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([this, Target, V]()
						{ *Target = V; RefreshView(); })));
				}
				return MB.MakeWidget();
			})
		];
	};

	auto Check = [this](const FText& Label, const FText& Tip, bool* Target) -> TSharedRef<SWidget>
	{
		return SNew(SCheckBox)
			.ToolTipText(Tip)
			.IsChecked_Lambda([Target]() { return *Target ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, Target](ECheckBoxState S)
			{
				*Target = (S == ECheckBoxState::Checked);
				RefreshView();
			})
			[
				SNew(STextBlock).Text(Label).Margin(FMargin(4.f, 0.f, 0.f, 0.f))
			];
	};

	return SNew(SScrollBox)
	+ SScrollBox::Slot()
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SearchLbl", "Поиск по имени и id (Ctrl+F)"))
			.ColorAndOpacity(AstoryUi::ColorMuted())
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SAssignNew(SearchBox, SSearchBox)
			.HintText(LOCTEXT("SearchHint", "имя, часть имени или id"))
			.OnTextChanged_Lambda([this](const FText& T)
			{
				SearchText = T.ToString();
				RefreshView();
			})
		]

		+ SVerticalBox::Slot().AutoHeight()
		[ EnumFilter(LOCTEXT("FCat", "Категория"), EAstoryFieldEnum::Category, 5, &FilterCategory) ]
		+ SVerticalBox::Slot().AutoHeight()
		[ EnumFilter(LOCTEXT("FGrp", "Вкладка (группа)"), EAstoryFieldEnum::Group, 2, &FilterGroup) ]
		+ SVerticalBox::Slot().AutoHeight()
		[ EnumFilter(LOCTEXT("FCls", "Класс"), EAstoryFieldEnum::ClassReq, 3, &FilterClass) ]
		+ SVerticalBox::Slot().AutoHeight()
		[ EnumFilter(LOCTEXT("FGrd", "Грейд"), EAstoryFieldEnum::Grade, 8, &FilterGrade) ]
		+ SVerticalBox::Slot().AutoHeight()
		[ EnumFilter(LOCTEXT("FEn", "Показывать в списке"), EAstoryFieldEnum::Enabled, 2, &FilterEnabled) ]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 4.f)
		[
			SNew(SSeparator)
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[ Check(LOCTEXT("ChkMism", "только расхождения"),
			LOCTEXT("ChkMismTip", "Только записи, чьи значения не совпадают с серверной FameInfo.txt."),
			&bOnlyMismatch) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[ Check(LOCTEXT("ChkNoSrv", "только «нет у сервера»"),
			LOCTEXT("ChkNoSrvTip", "Только клиентские id, которых у сервера нет."), &bOnlyNoServer) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[ Check(LOCTEXT("ChkUnr", "только недостижимые (id ≥ 256)"),
			LOCTEXT("ChkUnrTip", "Маска «получено» в протоколе ровно 256 бит."), &bOnlyUnreachable) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[ Check(LOCTEXT("ChkTrunc", "только с обрезанным описанием"),
			LOCTEXT("ChkTruncTip",
				"Дефект самих данных: описание в title_list.bin обрезано — либо не влезло в 64 байта, "
				"либо конвертер принял букву «я» за конец строки."),
			&bOnlyTruncDesc) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[ Check(LOCTEXT("ChkErr", "только с ошибками"),
			LOCTEXT("ChkErrTip", "Только записи с блокирующими ошибками валидации."), &bOnlyErrors) ]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 4.f)
		[
			SNew(SSeparator)
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SCheckBox)
			.ToolTipText(LOCTEXT("ChkAllColTip",
				"Показать в таблице ВСЕ поля записи, включая бонусы и неопознанные смещения."))
			.IsChecked_Lambda([this]() { return bShowAllColumns ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState S)
			{
				bShowAllColumns = (S == ECheckBoxState::Checked);
				RebuildHeader();
				RefreshView();
			})
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ChkAllCol", "показать все поля"))
				.Margin(FMargin(4.f, 0.f, 0.f, 0.f))
			]
		]
	];
}

// ═════════════════════════════════════════════════════════════════════════════
// Таблица
// ═════════════════════════════════════════════════════════════════════════════
TSharedRef<SWidget> SAstoryAchievementsEditor::BuildTable()
{
	SAssignNew(HeaderRow, SHeaderRow);

	TSharedRef<SWidget> Table =
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
		.Padding(0.f)
		[
			SAssignNew(ListView, SListView<FAstoryRowPtr>)
			.ListItemsSource(&VisibleRows)
			.SelectionMode(ESelectionMode::Multi)
			.HeaderRow(HeaderRow)
			.OnGenerateRow(this, &SAstoryAchievementsEditor::OnGenerateRow)
			.OnSelectionChanged(this, &SAstoryAchievementsEditor::OnSelectionChanged)
		];

	RebuildHeader();
	return Table;
}

void SAstoryAchievementsEditor::RebuildHeader()
{
	if (!HeaderRow.IsValid())
	{
		return;
	}
	HeaderRow->ClearColumns();

	// Колонка состояний — без подписи, только значки.
	HeaderRow->AddColumn(
		SHeaderRow::Column(AstoryUi::ColStatus)
		.DefaultLabel(LOCTEXT("ColState", ""))
		.DefaultTooltip(LOCTEXT("ColStateTip",
			"Состояния: замок — недостижимое (id ≥ 256) · треугольник — расхождение с сервером · "
			"«i» — нет строки у сервера · крест — ошибка валидации."))
		.FixedWidth(74.f)
		.HAlignHeader(HAlign_Center));

	auto AddFieldColumn = [this](const FAstoryTitleFieldDef& F, float Width, bool bFill)
	{
		const FName Id = AstoryUi::ColumnIdFor(F);
		SHeaderRow::FColumn::FArguments Col = SHeaderRow::Column(Id)
			.DefaultLabel(FText::FromString(F.Label))
			.DefaultTooltip(FText::FromString(FString(F.Tip) +
				(F.bIdentified ? TEXT("") : TEXT("\n★ Смысл поля НЕ УСТАНОВЛЕН."))))
			.HAlignHeader(HAlign_Left)
			.SortMode(TAttribute<EColumnSortMode::Type>::Create(
				TAttribute<EColumnSortMode::Type>::FGetter::CreateSP(
					this, &SAstoryAchievementsEditor::GetSortMode, Id)))
			.OnSort(FOnSortModeChanged::CreateSP(this, &SAstoryAchievementsEditor::OnSortColumn));
		if (bFill) { Col.FillWidth(Width); } else { Col.ManualWidth(Width); }
		HeaderRow->AddColumn(Col);
	};

	const TArray<FAstoryTitleFieldDef>& Fields = AstoryTitleFields();

	// 1) Основные колонки — в заданном порядке.
	for (int32 Offset : AstoryUi::ListColumnOffsets)
	{
		for (const FAstoryTitleFieldDef& F : Fields)
		{
			if (F.Offset != Offset)
			{
				continue;
			}
			// Ширины подобраны под содержимое: числа узкие, имя тянется.
			if (F.Offset == AstoryTitleOffsets::Name)                       { AddFieldColumn(F, 0.34f, true); }
			else if (F.Type == EAstoryFieldType::Int32 && F.Enum != EAstoryFieldEnum::None)
			                                                                { AddFieldColumn(F, 150.f, false); }
			else                                                            { AddFieldColumn(F, 66.f, false); }
			break;
		}
	}

	// 2) Серверные очки — рядом с клиентскими.
	HeaderRow->AddColumn(
		SHeaderRow::Column(AstoryUi::ColServerPoint)
		.DefaultLabel(LOCTEXT("ColSrvPt", "Очки (сервер)"))
		.DefaultTooltip(LOCTEXT("ColSrvPtTip",
			"Колонка 42 серверного FameInfo.txt. «—» = серверной строки с таким id нет.\n"
			"Авторитет данных — сервер."))
		.ManualWidth(110.f)
		.HAlignHeader(HAlign_Left)
		.SortMode(TAttribute<EColumnSortMode::Type>::Create(
			TAttribute<EColumnSortMode::Type>::FGetter::CreateSP(
				this, &SAstoryAchievementsEditor::GetSortMode, AstoryUi::ColServerPoint)))
		.OnSort(FOnSortModeChanged::CreateSP(this, &SAstoryAchievementsEditor::OnSortColumn)));

	// 3) Второстепенные поля — только по переключателю «показать все поля».
	if (bShowAllColumns)
	{
		for (const FAstoryTitleFieldDef& F : Fields)
		{
			if (!AstoryUi::IsListColumn(F.Offset))
			{
				AddFieldColumn(F, F.Type == EAstoryFieldType::String ? 240.f : 96.f, false);
			}
		}
	}
}

TSharedRef<ITableRow> SAstoryAchievementsEditor::OnGenerateRow(FAstoryRowPtr Item,
	const TSharedRef<STableViewBase>& Owner)
{
	return SNew(SAstoryTitleTableRow, Owner).Item(Item).Data(Data.Get());
}

void SAstoryAchievementsEditor::OnSelectionChanged(FAstoryRowPtr Item, ESelectInfo::Type)
{
	SelectedRecordIndex = Item.IsValid() ? Item->RecordIndex : INDEX_NONE;
	RefreshCard();
}

EColumnSortMode::Type SAstoryAchievementsEditor::GetSortMode(FName ColumnId) const
{
	return ColumnId == SortColumn ? SortMode : EColumnSortMode::None;
}

void SAstoryAchievementsEditor::OnSortColumn(EColumnSortPriority::Type, const FName& ColumnId,
                                             EColumnSortMode::Type Mode)
{
	SortColumn = ColumnId;
	SortMode = Mode;
	RefreshView();
}

// ═════════════════════════════════════════════════════════════════════════════
// Панель проблем
// ═════════════════════════════════════════════════════════════════════════════
TSharedRef<SWidget> SAstoryAchievementsEditor::BuildIssuesPanel()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
		.Padding(4.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(2.f, 2.f, 2.f, 4.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return FText::FromString(FString::Printf(TEXT("Проблемы: %d"), Issues.Num()));
					})
				]
				+ SHorizontalBox::Slot().FillWidth(1.f) [ SNew(SSpacer) ]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("CopyIssues", "Копировать список"))
					.ToolTipText(LOCTEXT("CopyIssuesTip", "Скопировать все проблемы в буфер обмена."))
					.ContentPadding(FMargin(8.f, 1.f))
					.OnClicked_Lambda([this]()
					{
						FString S;
						for (const FAstoryIssue& I : Issues)
						{
							S += FString::Printf(TEXT("id %d (строка %d): %s — %s\r\n"),
								I.Id, I.RowIndex, *IssueKindText(I.Kind).ToString(), *I.Text);
						}
						FPlatformApplicationMisc::ClipboardCopy(*S);
						StatusLine = FString::Printf(TEXT("скопировано проблем: %d"), Issues.Num());
						return FReply::Handled();
					})
				]
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SAssignNew(IssuesList, SListView<TSharedPtr<FAstoryIssue>>)
				.ListItemsSource(&VisibleIssues)
				.SelectionMode(ESelectionMode::Single)
				.OnGenerateRow_Lambda([this](TSharedPtr<FAstoryIssue> Item,
					const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
				{
					return SNew(STableRow<TSharedPtr<FAstoryIssue>>, Owner)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 1.f)
						[
							SNew(SBox).WidthOverride(14.f).HeightOverride(14.f)
							[
								SNew(SImage)
								.Image(Item->IsBlocking() ? AstoryUi::IconError() : AstoryUi::IconWarn())
								.ColorAndOpacity(IssueColor(*Item))
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.f, 1.f)
						[
							SNew(SBox).WidthOverride(70.f)
							[
								SNew(STextBlock)
								.Text(FText::FromString(FString::Printf(TEXT("id %d"), Item->Id)))
								.ColorAndOpacity(AstoryUi::ColorMuted())
							]
						]
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::FromString(FString::Printf(TEXT("%s — %s"),
								*IssueKindText(Item->Kind).ToString(), *Item->Text)))
							.ColorAndOpacity(IssueColor(*Item))
						]
					];
				})
				.OnSelectionChanged_Lambda([this](TSharedPtr<FAstoryIssue> Item, ESelectInfo::Type Info)
				{
					// Клик по проблеме — переход к записи (если она сейчас видна).
					if (!Item.IsValid() || Info == ESelectInfo::Direct || !ListView.IsValid())
					{
						return;
					}
					for (const FAstoryRowPtr& R : VisibleRows)
					{
						if (Data.IsValid() && Data->GetRecords().IsValidIndex(R->RecordIndex) &&
						    Data->GetRecords()[R->RecordIndex].RowIndex == Item->RowIndex)
						{
							ListView->SetSelection(R);
							ListView->RequestScrollIntoView(R);
							break;
						}
					}
				})
			]
		];
}

// ═════════════════════════════════════════════════════════════════════════════
// Карточка записи
// ═════════════════════════════════════════════════════════════════════════════
EVisibility SAstoryAchievementsEditor::CardVisibility() const
{
	return SelectedRecord() ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SAstoryAchievementsEditor::NoSelectionVisibility() const
{
	return SelectedRecord() ? EVisibility::Collapsed : EVisibility::Visible;
}

// ★ Карточка собирается ОДИН раз. Все её поля привязаны лямбдами к текущей выбранной записи,
//   поэтому смена выделения и правка значения обновляют её сами. Пересобирать карточку на
//   каждую правку нельзя: она уничтожала бы поле ввода прямо во время редактирования.
//   Перестраивается только серверная секция — у неё переменное число строк.
TSharedRef<SWidget> SAstoryAchievementsEditor::BuildCard()
{
	auto Sect = [this](EAstoryFieldGroup Grp, const FText& Title) -> TSharedRef<SWidget>
	{
		return SNew(SBox)
			.Visibility(this, &SAstoryAchievementsEditor::CardVisibility)
			[
				BuildCardSection(Title, Grp)
			];
	};

	SAssignNew(CardBox, SScrollBox)

	// ── заглушка «ничего не выбрано» ──
	+ SScrollBox::Slot().Padding(10.f)
	[
		SNew(STextBlock)
		.Visibility(this, &SAstoryAchievementsEditor::NoSelectionVisibility)
		.AutoWrapText(true)
		.ColorAndOpacity(AstoryUi::ColorMuted())
		.Text(LOCTEXT("NoSel", "Выберите звание в таблице, чтобы увидеть и править его поля."))
	]

	// ── шапка карточки ──
	+ SScrollBox::Slot().Padding(4.f, 4.f, 4.f, 8.f)
	[
		SNew(SVerticalBox)
		.Visibility(this, &SAstoryAchievementsEditor::CardVisibility)

		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Font(FAppStyle::Get().GetFontStyle("HeadingExtraSmall"))
			.AutoWrapText(true)
			.Text_Lambda([this]()
			{
				const FAstoryTitleRecord* R = SelectedRecord();
				return R ? FText::FromString(R->GetName()) : FText::GetEmpty();
			})
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.ColorAndOpacity(AstoryUi::ColorMuted())
			.Text_Lambda([this]()
			{
				const FAstoryTitleRecord* R = SelectedRecord();
				return R ? FText::FromString(FString::Printf(TEXT("id %d · строка CSV %d"),
					R->GetId(), R->RowIndex)) : FText::GetEmpty();
			})
		]

		// ★ предупреждение о недостижимости
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.ColorAndOpacity(AstoryUi::ColorUnreachable())
			.Text(LOCTEXT("CardUnreachable",
				"★ НЕДОСТИЖИМОЕ ЗВАНИЕ: id ≥ 256, а маска «получено» в протоколе (опкоды 6066/6086) "
				"ровно 256 бит. Сервер не может отметить такой бит — игрок это звание не получит."))
			.Visibility_Lambda([this]()
			{
				const FAstoryTitleRecord* R = SelectedRecord();
				return (R && R->IsUnreachable()) ? EVisibility::Visible : EVisibility::Collapsed;
			})
		]

		// ★ метка обрезанного описания
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.ColorAndOpacity(AstoryUi::ColorTruncDesc())
			.Text(LOCTEXT("CardDescCut",
				"★ ОПИСАНИЕ ОБРЕЗАНО В ДАННЫХ: либо текст занял всё поле (64 байта), либо строку "
				"обрезал конвертер, принявший букву «я» за конец строки. Это дефект самого "
				"title_list.bin, а не отображения — фразу можно дописать здесь."))
			.Visibility_Lambda([this]()
			{
				const FAstoryTitleRecord* R = SelectedRecord();
				return (R && R->IsDescLikelyTruncated()) ? EVisibility::Visible : EVisibility::Collapsed;
			})
		]
	]

	// ── поля по смыслу ──
	+ SScrollBox::Slot()[ Sect(EAstoryFieldGroup::Identity,     LOCTEXT("SecId",    "Идентификация")) ]
	+ SScrollBox::Slot()[ Sect(EAstoryFieldGroup::Display,      LOCTEXT("SecDisp",  "Отображение")) ]
	+ SScrollBox::Slot()[ Sect(EAstoryFieldGroup::Conditions,   LOCTEXT("SecCond",  "Условия")) ]
	+ SScrollBox::Slot()[ Sect(EAstoryFieldGroup::Bonuses,      LOCTEXT("SecBonus", "Бонусы")) ]
	+ SScrollBox::Slot()[ Sect(EAstoryFieldGroup::Unidentified, LOCTEXT("SecUnk",   "Не опознано")) ]

	// ── серверные значения ──
	+ SScrollBox::Slot()
	[
		SNew(SExpandableArea)
		.Visibility(this, &SAstoryAchievementsEditor::CardVisibility)
		.InitiallyCollapsed(false)
		.AreaTitle(LOCTEXT("SecSrv", "Серверные значения (FameInfo.txt)"))
		.Padding(FMargin(8.f, 4.f))
		.BodyContent()
		[
			SAssignNew(ServerBox, SVerticalBox)
		]
	];

	return CardBox.ToSharedRef();
}

FAstoryTitleRecord* SAstoryAchievementsEditor::SelectedRecord()
{
	if (Data.IsValid() && Data->GetRecords().IsValidIndex(SelectedRecordIndex))
	{
		return &Data->GetRecords()[SelectedRecordIndex];
	}
	return nullptr;
}

const FAstoryTitleRecord* SAstoryAchievementsEditor::SelectedRecord() const
{
	if (Data.IsValid() && Data->GetRecords().IsValidIndex(SelectedRecordIndex))
	{
		return &Data->GetRecords()[SelectedRecordIndex];
	}
	return nullptr;
}

FText SAstoryAchievementsEditor::EnumLabel(EAstoryFieldEnum Enum, int32 Value) const
{
	return AstoryEnumText(Data.Get(), Enum, Value);
}

TSharedRef<SWidget> SAstoryAchievementsEditor::BuildFieldEditor(const FAstoryTitleFieldDef& Field)
{
	const int32 Offset = Field.Offset;
	const int32 ByteLen = Field.ByteLen;
	const EAstoryFieldType Type = Field.Type;
	const EAstoryFieldEnum Enum = Field.Enum;

	if (Type == EAstoryFieldType::String)
	{
		if (Offset == AstoryTitleOffsets::Desc)
		{
			return SNew(SMultiLineEditableTextBox)
				.AutoWrapText(true)
				.Text_Lambda([this, Offset]()
				{
					const FAstoryTitleRecord* R = SelectedRecord();
					return R ? FText::FromString(R->GetStr(Offset)) : FText::GetEmpty();
				})
				.OnTextCommitted_Lambda([this, Offset, ByteLen](const FText& T, ETextCommit::Type C)
				{
					FAstoryTitleRecord* R = SelectedRecord();
					if (!R || C == ETextCommit::OnCleared || R->GetStr(Offset) == T.ToString())
					{
						return;
					}
					PushUndo();
					R->SetStr(Offset, ByteLen, T.ToString());
					MarkDirty();
					RefreshView();
				});
		}
		return SNew(SEditableTextBox)
			.Text_Lambda([this, Offset]()
			{
				const FAstoryTitleRecord* R = SelectedRecord();
				return R ? FText::FromString(R->GetStr(Offset)) : FText::GetEmpty();
			})
			.OnTextCommitted_Lambda([this, Offset, ByteLen](const FText& T, ETextCommit::Type C)
			{
				FAstoryTitleRecord* R = SelectedRecord();
				if (!R || C == ETextCommit::OnCleared || R->GetStr(Offset) == T.ToString())
				{
					return;
				}
				PushUndo();
				R->SetStr(Offset, ByteLen, T.ToString());
				MarkDirty();
				RefreshView();
			});
	}

	if (Enum != EAstoryFieldEnum::None)
	{
		int32 MaxV = 3;
		switch (Enum)
		{
		case EAstoryFieldEnum::Grade:    MaxV = 8; break;
		case EAstoryFieldEnum::Category: MaxV = 5; break;
		case EAstoryFieldEnum::Group:    MaxV = 2; break;
		case EAstoryFieldEnum::Position: MaxV = 1; break;
		case EAstoryFieldEnum::Enabled:  MaxV = 2; break;
		default: break;
		}
		return SNew(SComboButton)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text_Lambda([this, Offset, Enum]()
				{
					const FAstoryTitleRecord* R = SelectedRecord();
					return R ? EnumLabel(Enum, R->GetInt(Offset)) : FText::GetEmpty();
				})
			]
			.OnGetMenuContent_Lambda([this, Offset, Enum, MaxV]()
			{
				FMenuBuilder MB(true, nullptr);
				for (int32 V = 0; V <= MaxV; ++V)
				{
					MB.AddMenuEntry(EnumLabel(Enum, V), FText::GetEmpty(), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([this, Offset, V]()
						{
							FAstoryTitleRecord* R = SelectedRecord();
							if (R && R->GetInt(Offset) != V)
							{
								PushUndo();
								R->SetInt(Offset, V);
								MarkDirty();
								RefreshView();
							}
						})));
				}
				return MB.MakeWidget();
			});
	}

	return SNew(SSpinBox<int32>)
		.MinValue(TOptional<int32>())
		.MaxValue(TOptional<int32>())
		.MinDesiredWidth(90.f)
		.Value_Lambda([this, Offset]()
		{
			const FAstoryTitleRecord* R = SelectedRecord();
			return R ? R->GetInt(Offset) : 0;
		})
		.OnValueCommitted_Lambda([this, Offset](int32 V, ETextCommit::Type)
		{
			FAstoryTitleRecord* R = SelectedRecord();
			if (R && R->GetInt(Offset) != V)
			{
				PushUndo();
				R->SetInt(Offset, V);
				MarkDirty();
				RefreshView();
			}
		});
}

TSharedRef<SWidget> SAstoryAchievementsEditor::BuildCardSection(const FText& Title,
                                                                EAstoryFieldGroup Group)
{
	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);

	for (const FAstoryTitleFieldDef& F : AstoryTitleFields())
	{
		if (F.Group != Group)
		{
			continue;
		}
		const int32 Offset = F.Offset;
		const FString Tip = FString(F.Tip) +
			(F.bIdentified ? TEXT("") :
				TEXT("\n★ Смысл поля НЕ УСТАНОВЛЕН. Правка уходит в DataHex и .bin, "
				     "отдельной колонки в CSV у него нет."));

		Body->AddSlot().AutoHeight().Padding(0.f, 3.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(F.Label))
					.ToolTipText(FText::FromString(Tip))
					.ColorAndOpacity(F.bIdentified ? AstoryUi::ColorNormal() : AstoryUi::ColorMuted())
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 0.f, 0.f)
				[
					// Мелко и приглушённо — смещение в записи и имя колонки CSV.
					SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(TEXT("+%d%s"), Offset,
						F.CsvName ? *FString::Printf(TEXT(" · %s"), F.CsvName) : TEXT(" · только DataHex"))))
					.ColorAndOpacity(AstoryUi::ColorMuted())
					.ToolTipText(FText::FromString(Tip))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
			[
				BuildFieldEditor(F)
			]
			// ★ Для полей-ссылок на локализацию показываем И id строки, И сам текст.
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Visibility(F.Enum == EAstoryFieldEnum::Grade || F.Enum == EAstoryFieldEnum::Category ||
				            F.Enum == EAstoryFieldEnum::Position || F.Enum == EAstoryFieldEnum::Group
				            ? EVisibility::Visible : EVisibility::Collapsed)
				.ColorAndOpacity(AstoryUi::ColorMuted())
				.AutoWrapText(true)
				.Text_Lambda([this, Offset, En = F.Enum]()
				{
					const FAstoryTitleRecord* R = SelectedRecord();
					if (!R) { return FText::GetEmpty(); }
					const int32 V = R->GetInt(Offset);
					int32 LocId = 0;
					switch (En)
					{
					case EAstoryFieldEnum::Grade:    LocId = 1450 + V; break;
					case EAstoryFieldEnum::Category: LocId = 1460 + V; break;
					case EAstoryFieldEnum::Position: LocId = (V == 0 ? 1466 : 1467); break;
					case EAstoryFieldEnum::Group:    LocId = 2405 + FMath::Clamp(V, 0, 2); break;
					default: return FText::GetEmpty();
					}
					bool bHas = false;
					const FString S = Data->Loc(LocId, &bHas);
					return FText::FromString(bHas
						? FString::Printf(TEXT("строка %d: «%s»"), LocId, *S)
						: FString::Printf(TEXT("★ строки %d в локализации НЕТ"), LocId));
				})
			]
		];
	}

	return SNew(SExpandableArea)
		.InitiallyCollapsed(Group == EAstoryFieldGroup::Bonuses || Group == EAstoryFieldGroup::Unidentified)
		.AreaTitle(Title)
		.Padding(FMargin(8.f, 4.f))
		.BodyContent()
		[
			Body
		];
}

void SAstoryAchievementsEditor::RefreshCard()
{
	if (!ServerBox.IsValid())
	{
		return;
	}
	ServerBox->ClearChildren();

	const FAstoryTitleRecord* Rec = SelectedRecord();
	if (!Rec || !Data.IsValid())
	{
		return;
	}

	const FAstoryFameServerRow* SrvRow = Data->GetServerRows().Find(Rec->GetId());
	if (Data->GetServerRows().Num() == 0)
	{
		ServerBox->AddSlot().AutoHeight()
		[
			SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(AstoryUi::ColorMuted())
			.Text(LOCTEXT("SrvOff",
				"Серверная таблица FameInfo.txt недоступна — сверка не выполнялась."))
		];
		return;
	}
	if (!SrvRow)
	{
		ServerBox->AddSlot().AutoHeight()
		[
			SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(AstoryUi::ColorNoServer())
			.Text(FText::FromString(FString::Printf(
				TEXT("У сервера НЕТ строки с id %d.\nТакие звания существуют только у клиента "
				     "(id 251…279) и сервером не выдаются."), Rec->GetId())))
		];
		return;
	}

	for (const FAstoryServerColMap& M : AstoryServerColMap())
	{
		if (!SrvRow->Cols.IsValidIndex(M.ServerCol))
		{
			continue;
		}
		const int32 ClientOffset = M.ClientOffset;
		const int32 ServerCol = M.ServerCol;
		const int32 SV = SrvRow->Cols[ServerCol];
		ServerBox->AddSlot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.45f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("%s (кол. %d)"), M.Label, ServerCol)))
				.ColorAndOpacity(AstoryUi::ColorMuted())
			]
			+ SHorizontalBox::Slot().FillWidth(0.55f).VAlign(VAlign_Center)
			[
				// Значение клиента читаем лямбдой: правка поля сразу видна в сверке.
				SNew(STextBlock)
				.Text_Lambda([this, ClientOffset, ServerCol, SV]()
				{
					const FAstoryTitleRecord* R = SelectedRecord();
					if (!R) { return FText::GetEmpty(); }
					const int32 CV = R->GetInt(ClientOffset);
					return FText::FromString((CV != SV && ServerCol != 0)
						? FString::Printf(TEXT("клиент %d  ≠  сервер %d"), CV, SV)
						: FString::Printf(TEXT("%d (совпадает)"), CV));
				})
				.ColorAndOpacity_Lambda([this, ClientOffset, ServerCol, SV]()
				{
					const FAstoryTitleRecord* R = SelectedRecord();
					const bool bDiff = R && ServerCol != 0 && R->GetInt(ClientOffset) != SV;
					return bDiff ? AstoryUi::ColorMismatch() : AstoryUi::ColorNormal();
				})
			]
		];
	}

	// Полная строка сервера — как есть, для сверки глазами.
	FString Raw;
	for (int32 i = 0; i < SrvRow->Cols.Num(); ++i)
	{
		Raw += FString::Printf(TEXT("%d:%d  "), i, SrvRow->Cols[i]);
	}
	ServerBox->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
	[
		SNew(STextBlock)
		.AutoWrapText(true)
		.ColorAndOpacity(AstoryUi::ColorMuted())
		.Text(FText::FromString(FString::Printf(
			TEXT("строка сервера целиком (колонка:значение):\n%s"), *Raw)))
	];
}

// ═════════════════════════════════════════════════════════════════════════════
// Загрузка и обновление вида
// ═════════════════════════════════════════════════════════════════════════════
void SAstoryAchievementsEditor::ReloadAll()
{
	FString Error;
	if (!Data->LoadAll(Error))
	{
		StatusLine = FString::Printf(TEXT("★ не загрузилось: %s"), *Error);
		VisibleRows.Reset();
		if (ListView.IsValid()) { ListView->RequestListRefresh(); }
		return;
	}
	bDirty = false;
	UndoStack.Reset();
	SelectedRecordIndex = INDEX_NONE;
	StatusLine = FString::Printf(TEXT("загружено записей: %d · строк локализации из GameText.csv · сервер: %s"),
		Data->GetRecords().Num(),
		Data->GetServerFilePath().IsEmpty() ? TEXT("недоступен") : *Data->GetServerFilePath());
	RefreshView();
	RefreshCard();
}

void SAstoryAchievementsEditor::RefreshView()
{
	if (!Data.IsValid())
	{
		return;
	}

	// 1) Проблемы и состояния.
	Issues = Data->Validate();
	const TArray<FAstoryTitleRecord>& Recs = Data->GetRecords();

	// ★ Объекты строк ПЕРЕИСПОЛЬЗУЕМ: пересоздание рвёт выделение в SListView.
	if (AllRows.Num() != Recs.Num())
	{
		AllRows.Reset();
		for (int32 i = 0; i < Recs.Num(); ++i)
		{
			AllRows.Add(MakeShared<FAstoryRowView>());
		}
	}
	for (int32 i = 0; i < Recs.Num(); ++i)
	{
		FAstoryRowView& V = *AllRows[i];
		V.RecordIndex = i;
		V.bUnreachable = Recs[i].IsUnreachable();
		V.bDisabled = Recs[i].IsDisabled();
		V.bNoServerRow = Data->GetServerRows().Num() > 0 && !Data->HasServerRow(Recs[i].GetId());
		V.bMismatch = Data->DiffWithServer(Recs[i]).Num() > 0;
		V.bTruncDesc = Recs[i].IsDescLikelyTruncated();
		V.bError = false;
		V.StatusTip.Empty();
	}
	TMap<int32, int32> RowToIndex;
	for (int32 i = 0; i < Recs.Num(); ++i)
	{
		RowToIndex.Add(Recs[i].RowIndex, i);
	}
	for (const FAstoryIssue& I : Issues)
	{
		if (!I.IsBlocking())
		{
			continue;
		}
		if (const int32* Idx = RowToIndex.Find(I.RowIndex))
		{
			FAstoryRowView& V = *AllRows[*Idx];
			V.bError = true;
			V.StatusTip += (V.StatusTip.IsEmpty() ? TEXT("") : TEXT("\n")) + I.Text;
		}
	}

	// 2) Сводка.
	CountTotal = Recs.Num();
	CountMismatch = CountUnreachable = CountErrors = CountNoServer = CountTruncDesc = 0;
	for (const FAstoryRowPtr& Row : AllRows)
	{
		const FAstoryRowView& V = *Row;
		if (V.bMismatch)    { ++CountMismatch; }
		if (V.bUnreachable) { ++CountUnreachable; }
		if (V.bError)       { ++CountErrors; }
		if (V.bNoServerRow) { ++CountNoServer; }
		if (V.bTruncDesc)   { ++CountTruncDesc; }
	}
	MismatchBreakdown = Data->DescribeServerMismatchBreakdown();

	// 3) Фильтр.
	VisibleRows.Reset();
	for (int32 i = 0; i < AllRows.Num(); ++i)
	{
		if (PassesFilter(Recs[i], *AllRows[i]))
		{
			VisibleRows.Add(AllRows[i]);
		}
	}

	// 4) Сортировка.
	const FName Col = SortColumn;
	const bool bAsc = (SortMode != EColumnSortMode::Descending);
	int32 SortOffset = INDEX_NONE;
	bool bSortString = false;
	for (const FAstoryTitleFieldDef& F : AstoryTitleFields())
	{
		if (AstoryUi::ColumnIdFor(F) == Col)
		{
			SortOffset = F.Offset;
			bSortString = (F.Type == EAstoryFieldType::String);
			break;
		}
	}
	const bool bSortSrv = (Col == AstoryUi::ColServerPoint);

	VisibleRows.Sort([this, &Recs, SortOffset, bSortString, bSortSrv, bAsc]
		(const FAstoryRowPtr& A, const FAstoryRowPtr& B)
	{
		const FAstoryTitleRecord& RA = Recs[A->RecordIndex];
		const FAstoryTitleRecord& RB = Recs[B->RecordIndex];
		int32 Cmp = 0;
		if (bSortSrv)
		{
			auto Pt = [this](const FAstoryTitleRecord& R) -> int32
			{
				const FAstoryFameServerRow* S = Data->GetServerRows().Find(R.GetId());
				return (S && S->Cols.IsValidIndex(42)) ? S->Cols[42] : TNumericLimits<int32>::Lowest();
			};
			const int32 PA = Pt(RA), PB = Pt(RB);
			Cmp = PA < PB ? -1 : (PA > PB ? 1 : 0);
		}
		else if (SortOffset == INDEX_NONE)
		{
			Cmp = RA.GetId() < RB.GetId() ? -1 : (RA.GetId() > RB.GetId() ? 1 : 0);
		}
		else if (bSortString)
		{
			Cmp = RA.GetStr(SortOffset).Compare(RB.GetStr(SortOffset), ESearchCase::IgnoreCase);
		}
		else
		{
			const int32 VA = RA.GetInt(SortOffset), VB = RB.GetInt(SortOffset);
			Cmp = VA < VB ? -1 : (VA > VB ? 1 : 0);
		}
		if (Cmp == 0)
		{
			Cmp = RA.GetId() < RB.GetId() ? -1 : (RA.GetId() > RB.GetId() ? 1 : 0);
		}
		return bAsc ? Cmp < 0 : Cmp > 0;
	});

	// 5) Проблемы для панели.
	VisibleIssues.Reset();
	for (const FAstoryIssue& I : Issues)
	{
		VisibleIssues.Add(MakeShared<FAstoryIssue>(I));
	}

	if (ListView.IsValid())   { ListView->RequestListRefresh(); }
	if (IssuesList.IsValid()) { IssuesList->RequestListRefresh(); }
}

bool SAstoryAchievementsEditor::PassesFilter(const FAstoryTitleRecord& Rec,
                                             const FAstoryRowView& View) const
{
	if (!SearchText.IsEmpty())
	{
		const bool bByName = Rec.GetName().Contains(SearchText, ESearchCase::IgnoreCase);
		const bool bById = FString::FromInt(Rec.GetId()) == SearchText ||
		                   FString::FromInt(Rec.GetId()).Contains(SearchText);
		const bool bByDesc = Rec.GetStr(AstoryTitleOffsets::Desc).Contains(SearchText, ESearchCase::IgnoreCase);
		if (!bByName && !bById && !bByDesc)
		{
			return false;
		}
	}
	if (FilterCategory >= 0 && Rec.GetInt(AstoryTitleOffsets::CategoryLocIndex) != FilterCategory) { return false; }
	if (FilterGroup >= 0    && Rec.GetInt(AstoryTitleOffsets::Group) != FilterGroup)               { return false; }
	if (FilterClass >= 0    && Rec.GetInt(AstoryTitleOffsets::ClassReq) != FilterClass)            { return false; }
	if (FilterGrade >= 0    && Rec.GetInt(AstoryTitleOffsets::ColorIndex) != FilterGrade)          { return false; }
	if (FilterEnabled >= 0  && Rec.GetInt(AstoryTitleOffsets::Enabled) != FilterEnabled)           { return false; }
	if (bOnlyMismatch    && !View.bMismatch)     { return false; }
	if (bOnlyUnreachable && !View.bUnreachable)  { return false; }
	if (bOnlyErrors      && !View.bError)        { return false; }
	if (bOnlyNoServer    && !View.bNoServerRow)  { return false; }
	if (bOnlyTruncDesc   && !View.bTruncDesc)    { return false; }
	return true;
}

// ═════════════════════════════════════════════════════════════════════════════
// Правки
// ═════════════════════════════════════════════════════════════════════════════
void SAstoryAchievementsEditor::PushUndo()
{
	if (!Data.IsValid())
	{
		return;
	}
	UndoStack.Add(Data->GetRecords());
	if (UndoStack.Num() > 50)
	{
		UndoStack.RemoveAt(0);
	}
}

void SAstoryAchievementsEditor::ApplyUndo()
{
	if (UndoStack.Num() == 0 || !Data.IsValid())
	{
		return;
	}
	Data->GetRecords() = UndoStack.Pop();
	MarkDirty();
	RefreshView();
	RefreshCard();
}

void SAstoryAchievementsEditor::MarkDirty()
{
	bDirty = true;
	if (TSharedPtr<SDockTab> Tab = OwnerTab.Pin())
	{
		Tab->SetLabel(LOCTEXT("TabDirty", "Редактор достижений Asda*"));
	}
}

void SAstoryAchievementsEditor::SetFieldOnSelection(int32 Offset, int32 Value)
{
	if (!ListView.IsValid() || !Data.IsValid())
	{
		return;
	}
	TArray<FAstoryRowPtr> Sel = ListView->GetSelectedItems();
	if (Sel.Num() == 0)
	{
		return;
	}
	PushUndo();
	for (const FAstoryRowPtr& R : Sel)
	{
		if (Data->GetRecords().IsValidIndex(R->RecordIndex))
		{
			Data->GetRecords()[R->RecordIndex].SetInt(Offset, Value);
		}
	}
	MarkDirty();
	StatusLine = FString::Printf(TEXT("массовая правка: изменено записей %d"), Sel.Num());
	RefreshView();
	RefreshCard();
}

// ═════════════════════════════════════════════════════════════════════════════
// Действия
// ═════════════════════════════════════════════════════════════════════════════
FReply SAstoryAchievementsEditor::OnSaveClicked()
{
	if (!Data.IsValid())
	{
		return FReply::Handled();
	}

	// ★ Валидация ПЕРЕД сохранением: блокирующие ошибки показываем списком и не даём сохранить.
	// ★ Блокируют только ошибки, внесённые В ЭТОЙ СЕССИИ. Унаследованные дефекты данных
	//   (например, 15 записей с категорией вне 1460…1465) показываем, но сохранить не мешаем —
	//   иначе старый дефект навсегда запретил бы любую несвязанную правку.
	TArray<FAstoryIssue> Blocking, Inherited;
	for (const FAstoryIssue& I : Data->Validate())
	{
		if (!I.IsBlocking())
		{
			continue;
		}
		(I.bPreExisting ? Inherited : Blocking).Add(I);
	}
	if (Blocking.Num() > 0)
	{
		FString Msg = FString::Printf(
			TEXT("Сохранение отменено: ошибок, внесённых этой правкой, — %d.\n\n"), Blocking.Num());
		for (int32 i = 0; i < FMath::Min(Blocking.Num(), 20); ++i)
		{
			Msg += FString::Printf(TEXT("• id %d: %s\n"), Blocking[i].Id, *Blocking[i].Text);
		}
		if (Blocking.Num() > 20)
		{
			Msg += FString::Printf(TEXT("… и ещё %d — полный список в панели «Проблемы».\n"),
				Blocking.Num() - 20);
		}
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Msg));
		bOnlyErrors = true;
		RefreshView();
		return FReply::Handled();
	}

	if (Inherited.Num() > 0)
	{
		StatusLine = FString::Printf(
			TEXT("⚠ в данных есть %d унаследованных дефектов (сохранять не мешают) — см. «Проблемы»"),
			Inherited.Num());
	}

	// Пересборка .bin — только если самопроверка обратной сборки прошла (проверяется внутри).
	const EAppReturnType::Type Ans = FMessageDialog::Open(EAppMsgType::YesNoCancel,
		LOCTEXT("SaveBinAsk",
			"Записать также исходный title_list.bin?\n\n"
			"Да — перед записью выполняется побайтовая самопроверка обратной сборки; если она "
			"не сойдётся, .bin не будет записан.\n"
			"Нет — обновить только title_list.csv/.json и DataTable (именно их читает порт)."));
	if (Ans == EAppReturnType::Cancel)
	{
		return FReply::Handled();
	}

	FString Log, Error;
	if (!Data->Save(Ans == EAppReturnType::Yes, Log, Error))
	{
		StatusLine = FString::Printf(TEXT("★ ошибка сохранения: %s"), *Error);
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(StatusLine + TEXT("\n\n") + Log));
		return FReply::Handled();
	}

	bDirty = false;
	if (TSharedPtr<SDockTab> Tab = OwnerTab.Pin())
	{
		Tab->SetLabel(LOCTEXT("TabClean", "Редактор достижений Asda"));
	}
	StatusLine = Log.Replace(TEXT("\n"), TEXT(" · "));
	UE_LOG(LogAstoryAchUI, Log, TEXT("сохранение:\n%s"), *Log);

	// ★ Сразу после сохранения — выгрузка файла для сервера.
	OnServerExportClicked();
	return FReply::Handled();
}

FReply SAstoryAchievementsEditor::OnReloadClicked()
{
	if (bDirty)
	{
		const EAppReturnType::Type A = FMessageDialog::Open(EAppMsgType::YesNo,
			LOCTEXT("ReloadAsk", "Есть несохранённые правки. Перечитать с диска и потерять их?"));
		if (A != EAppReturnType::Yes)
		{
			return FReply::Handled();
		}
	}
	ReloadAll();
	return FReply::Handled();
}

FReply SAstoryAchievementsEditor::OnUndoClicked()
{
	ApplyUndo();
	return FReply::Handled();
}

FReply SAstoryAchievementsEditor::OnExportSelectionClicked()
{
	IDesktopPlatform* DP = FDesktopPlatformModule::Get();
	if (!DP || !Data.IsValid())
	{
		return FReply::Handled();
	}
	TArray<FString> Out;
	const void* Handle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(SharedThis(this));
	if (!DP->SaveFileDialog(Handle, TEXT("Экспорт выборки званий"),
		FPaths::ProjectDir(), TEXT("titles_selection.csv"),
		TEXT("CSV|*.csv"), EFileDialogFlags::None, Out) || Out.Num() == 0)
	{
		return FReply::Handled();
	}

	// Выделенные, а если ничего не выделено — все видимые.
	TArray<int32> Indices;
	if (ListView.IsValid() && ListView->GetNumItemsSelected() > 0)
	{
		for (const FAstoryRowPtr& R : ListView->GetSelectedItems()) { Indices.Add(R->RecordIndex); }
	}
	else
	{
		for (const FAstoryRowPtr& R : VisibleRows) { Indices.Add(R->RecordIndex); }
	}

	FString Error;
	if (Data->ExportSelectionCsv(Indices, Out[0], Error))
	{
		StatusLine = FString::Printf(TEXT("экспортировано записей %d → %s"), Indices.Num(), *Out[0]);
	}
	else
	{
		StatusLine = FString::Printf(TEXT("★ экспорт не удался: %s"), *Error);
	}
	return FReply::Handled();
}

FReply SAstoryAchievementsEditor::OnServerExportClicked()
{
	if (!Data.IsValid() || Data->GetServerRows().Num() == 0)
	{
		StatusLine = TEXT("★ выгрузка невозможна: серверный FameInfo.txt не загружен");
		return FReply::Handled();
	}

	// ★ Спрашиваем режим осмысленно: показываем оба числа. По умолчанию на сервер едут
	//   ТОЛЬКО правки этой сессии — расхождения, которые были в данных до нас, не трогаем
	//   (авторитет данных — сервер).
	int32 MyEdits = 0, AllMapped = 0;
	Data->CountServerChanges(MyEdits, AllMapped);

	const EAppReturnType::Type Mode = FMessageDialog::Open(EAppMsgType::YesNoCancel,
		FText::FromString(FString::Printf(
			TEXT("Выгрузка FameInfo.txt для сервера.\n\n")
			TEXT("• Ваших правок, затрагивающих серверные колонки: %d строк.\n")
			TEXT("• Всего расхождений клиент ↔ сервер по сопоставленным колонкам: %d строк\n")
			TEXT("  (часть из них была в данных ещё до этой сессии).\n\n")
			TEXT("Да — перенести ТОЛЬКО ваши правки (%d строк). Безопасный режим.\n")
			TEXT("Нет — выровнять ВСЕ сопоставленные колонки по клиенту (%d строк).\n")
			TEXT("      ⚠ Так вы перепишете и те расхождения, которые возникли не по вашей вине;\n")
			TEXT("      авторитет данных вообще-то за сервером."),
			MyEdits, AllMapped, MyEdits, AllMapped)));
	if (Mode == EAppReturnType::Cancel)
	{
		return FReply::Handled();
	}

	FAstoryServerExportRequest Req;
	int32 Changed = 0;
	FString Error;
	if (!Data->BuildServerExportRequest(Req, Changed, Error,
			/*bAllMappedColumns=*/Mode == EAppReturnType::No))
	{
		StatusLine = FString::Printf(TEXT("★ выгрузка не удалась: %s"), *Error);
		return FReply::Handled();
	}

	FString ExportError;
	const FString Folder = FAstoryServerExport::Run(Req, ExportError);
	StatusLine = Folder.IsEmpty()
		? FString::Printf(TEXT("★ выгрузка не удалась: %s"), *ExportError)
		: FString::Printf(TEXT("выгрузка для сервера готова (изменено строк %d): %s"), Changed, *Folder);
	return FReply::Handled();
}

FReply SAstoryAchievementsEditor::OnBulkEditClicked()
{
	if (!ListView.IsValid() || ListView->GetNumItemsSelected() == 0)
	{
		return FReply::Handled();
	}

	// Простое модальное окно: поле + значение.
	TArray<const FAstoryTitleFieldDef*> IntFields;
	for (const FAstoryTitleFieldDef& F : AstoryTitleFields())
	{
		if (F.Type == EAstoryFieldType::Int32 && F.Offset != AstoryTitleOffsets::Id)
		{
			IntFields.Add(&F);
		}
	}
	if (IntFields.Num() == 0)
	{
		return FReply::Handled();
	}

	TSharedRef<int32> ChosenIdx = MakeShared<int32>(0);
	TSharedRef<int32> Value = MakeShared<int32>(0);
	TSharedRef<bool> bAccepted = MakeShared<bool>(false);

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("BulkTitle", "Массовая правка поля"))
		.ClientSize(FVector2D(460.f, 170.f))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	Window->SetContent(
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
		.Padding(12.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(FText::FromString(FString::Printf(
					TEXT("Значение будет задано %d выделенным записям."),
					ListView->GetNumItemsSelected())))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
			[
				SNew(SComboButton)
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text_Lambda([IntFields, ChosenIdx]()
					{
						return FText::FromString(IntFields[*ChosenIdx]->Label);
					})
				]
				.OnGetMenuContent_Lambda([IntFields, ChosenIdx]()
				{
					FMenuBuilder MB(true, nullptr);
					for (int32 i = 0; i < IntFields.Num(); ++i)
					{
						MB.AddMenuEntry(FText::FromString(IntFields[i]->Label),
							FText::FromString(IntFields[i]->Tip), FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([ChosenIdx, i]() { *ChosenIdx = i; })));
					}
					return MB.MakeWidget();
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
			[
				SNew(SSpinBox<int32>)
				.MinValue(TOptional<int32>())
				.MaxValue(TOptional<int32>())
				.Value_Lambda([Value]() { return *Value; })
				.OnValueChanged_Lambda([Value](int32 V) { *Value = V; })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f).HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
				[
					SNew(SButton).Text(LOCTEXT("BulkOk", "Применить"))
					.OnClicked_Lambda([Window, bAccepted]()
					{
						*bAccepted = true;
						Window->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton).Text(LOCTEXT("BulkCancel", "Отмена"))
					.OnClicked_Lambda([Window]()
					{
						Window->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
			]
		]);

	FSlateApplication::Get().AddModalWindow(Window, SharedThis(this));

	if (*bAccepted)
	{
		SetFieldOnSelection(IntFields[*ChosenIdx]->Offset, *Value);
	}
	return FReply::Handled();
}

FReply SAstoryAchievementsEditor::OnAddRecordClicked()
{
	if (!Data.IsValid())
	{
		return FReply::Handled();
	}
	TArray<FAstoryTitleRecord>& Recs = Data->GetRecords();

	// Свободный id: сначала ищем ниже 256 (их, скорее всего, нет), затем берём max+1.
	TSet<int32> Used;
	int32 MaxId = -1;
	for (const FAstoryTitleRecord& R : Recs)
	{
		Used.Add(R.GetId());
		MaxId = FMath::Max(MaxId, R.GetId());
	}
	int32 NewId = INDEX_NONE;
	for (int32 i = 0; i < AstoryFameMaskBits; ++i)
	{
		if (!Used.Contains(i)) { NewId = i; break; }
	}
	const bool bUnreachable = (NewId == INDEX_NONE);
	if (bUnreachable)
	{
		NewId = MaxId + 1;
		const EAppReturnType::Type A = FMessageDialog::Open(EAppMsgType::YesNo,
			FText::FromString(FString::Printf(
				TEXT("★ Свободных id ниже 256 НЕТ НИ ОДНОГО — все 256 заняты.\n\n")
				TEXT("Новая запись получит id %d. Маска «получено» в протоколе (опкоды 6066/6086) ")
				TEXT("ровно 256 бит, поэтому такое звание игрок ПОЛУЧИТЬ НЕ СМОЖЕТ — оно будет ")
				TEXT("видно в таблице, но недостижимо.\n\nВсё равно создать?"), NewId)));
		if (A != EAppReturnType::Yes)
		{
			return FReply::Handled();
		}
	}

	PushUndo();
	FAstoryTitleRecord New = Recs.IsValidIndex(SelectedRecordIndex) ? Recs[SelectedRecordIndex]
	                                                                : FAstoryTitleRecord();
	if (New.Payload.Num() != 240)
	{
		New.Payload.SetNumZeroed(240);
	}
	New.RowIndex = Recs.Num();
	New.SetInt(AstoryTitleOffsets::Id, NewId);
	New.SetStr(AstoryTitleOffsets::Name, 64,
		FString::Printf(TEXT("Новое звание %d"), NewId));
	Recs.Add(MoveTemp(New));
	MarkDirty();
	RefreshView();
	StatusLine = FString::Printf(TEXT("добавлена запись id %d%s"), NewId,
		bUnreachable ? TEXT(" (★ НЕДОСТИЖИМА: id ≥ 256)") : TEXT(""));
	return FReply::Handled();
}

FReply SAstoryAchievementsEditor::OnDeleteRecordClicked()
{
	if (!ListView.IsValid() || !Data.IsValid() || ListView->GetNumItemsSelected() == 0)
	{
		return FReply::Handled();
	}
	TArray<FAstoryRowPtr> Sel = ListView->GetSelectedItems();
	const EAppReturnType::Type A = FMessageDialog::Open(EAppMsgType::YesNo,
		FText::FromString(FString::Printf(TEXT("Удалить выделенных записей: %d?"), Sel.Num())));
	if (A != EAppReturnType::Yes)
	{
		return FReply::Handled();
	}

	PushUndo();
	TArray<int32> Indices;
	for (const FAstoryRowPtr& R : Sel) { Indices.Add(R->RecordIndex); }
	Indices.Sort([](int32 X, int32 Y) { return X > Y; });
	for (int32 I : Indices)
	{
		if (Data->GetRecords().IsValidIndex(I)) { Data->GetRecords().RemoveAt(I); }
	}
	// `_row` — просто порядковый номер строки CSV, после удаления его надо перенумеровать.
	for (int32 i = 0; i < Data->GetRecords().Num(); ++i)
	{
		Data->GetRecords()[i].RowIndex = i;
	}
	SelectedRecordIndex = INDEX_NONE;
	MarkDirty();
	RefreshView();
	RefreshCard();
	StatusLine = FString::Printf(TEXT("удалено записей: %d"), Indices.Num());
	return FReply::Handled();
}

FReply SAstoryAchievementsEditor::OnRoundTripTestClicked()
{
	FString Report;
	FAstoryTitleData::RunSelfCheck(Report);
	StatusLine = TEXT("самопроверка выполнена — подробности в окне и в логе редактора");
	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Report));
	return FReply::Handled();
}

// ═════════════════════════════════════════════════════════════════════════════
// Закрытие вкладки и горячие клавиши
// ═════════════════════════════════════════════════════════════════════════════
bool SAstoryAchievementsEditor::CanCloseTab()
{
	if (!bDirty)
	{
		return true;
	}
	const EAppReturnType::Type A = FMessageDialog::Open(EAppMsgType::YesNo,
		LOCTEXT("CloseAsk", "В редакторе достижений есть несохранённые правки. Закрыть и потерять их?"));
	return A == EAppReturnType::Yes;
}

FReply SAstoryAchievementsEditor::OnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	if (KeyEvent.IsControlDown())
	{
		if (KeyEvent.GetKey() == EKeys::S) { OnSaveClicked(); return FReply::Handled(); }
		if (KeyEvent.GetKey() == EKeys::Z) { ApplyUndo();     return FReply::Handled(); }
		if (KeyEvent.GetKey() == EKeys::F && SearchBox.IsValid())
		{
			FSlateApplication::Get().SetKeyboardFocus(SearchBox);
			return FReply::Handled();
		}
	}
	return SCompoundWidget::OnKeyDown(Geometry, KeyEvent);
}

// ═════════════════════════════════════════════════════════════════════════════
// Состояние между запусками
// ═════════════════════════════════════════════════════════════════════════════
void SAstoryAchievementsEditor::LoadUiState()
{
	if (!GConfig)
	{
		return;
	}
	GConfig->GetString(AstoryUi::IniSection, TEXT("SearchText"), SearchText, GEditorPerProjectIni);
	GConfig->GetInt(AstoryUi::IniSection, TEXT("FilterCategory"), FilterCategory, GEditorPerProjectIni);
	GConfig->GetInt(AstoryUi::IniSection, TEXT("FilterGroup"), FilterGroup, GEditorPerProjectIni);
	GConfig->GetInt(AstoryUi::IniSection, TEXT("FilterClass"), FilterClass, GEditorPerProjectIni);
	GConfig->GetInt(AstoryUi::IniSection, TEXT("FilterGrade"), FilterGrade, GEditorPerProjectIni);
	GConfig->GetInt(AstoryUi::IniSection, TEXT("FilterEnabled"), FilterEnabled, GEditorPerProjectIni);
	GConfig->GetBool(AstoryUi::IniSection, TEXT("OnlyMismatch"), bOnlyMismatch, GEditorPerProjectIni);
	GConfig->GetBool(AstoryUi::IniSection, TEXT("OnlyUnreachable"), bOnlyUnreachable, GEditorPerProjectIni);
	GConfig->GetBool(AstoryUi::IniSection, TEXT("OnlyErrors"), bOnlyErrors, GEditorPerProjectIni);
	GConfig->GetBool(AstoryUi::IniSection, TEXT("OnlyNoServer"), bOnlyNoServer, GEditorPerProjectIni);
	GConfig->GetBool(AstoryUi::IniSection, TEXT("OnlyTruncDesc"), bOnlyTruncDesc, GEditorPerProjectIni);
	GConfig->GetBool(AstoryUi::IniSection, TEXT("ShowAllColumns"), bShowAllColumns, GEditorPerProjectIni);

	FString Col;
	if (GConfig->GetString(AstoryUi::IniSection, TEXT("SortColumn"), Col, GEditorPerProjectIni) && !Col.IsEmpty())
	{
		SortColumn = FName(*Col);
	}
	int32 Mode = (int32)EColumnSortMode::Ascending;
	GConfig->GetInt(AstoryUi::IniSection, TEXT("SortMode"), Mode, GEditorPerProjectIni);
	SortMode = (EColumnSortMode::Type)Mode;

	GConfig->GetFloat(AstoryUi::IniSection, TEXT("SplitFilters"), SplitFilters, GEditorPerProjectIni);
	GConfig->GetFloat(AstoryUi::IniSection, TEXT("SplitCenter"), SplitCenter, GEditorPerProjectIni);
	GConfig->GetFloat(AstoryUi::IniSection, TEXT("SplitCard"), SplitCard, GEditorPerProjectIni);
	GConfig->GetFloat(AstoryUi::IniSection, TEXT("SplitTable"), SplitTable, GEditorPerProjectIni);
	GConfig->GetFloat(AstoryUi::IniSection, TEXT("SplitIssues"), SplitIssues, GEditorPerProjectIni);
	// Защита от испорченного ini: нулевая доля схлопнула бы зону навсегда.
	SplitFilters = FMath::Clamp(SplitFilters, 0.05f, 0.6f);
	SplitCenter  = FMath::Clamp(SplitCenter,  0.2f,  0.9f);
	SplitCard    = FMath::Clamp(SplitCard,    0.1f,  0.6f);
	SplitTable   = FMath::Clamp(SplitTable,   0.2f,  0.95f);
	SplitIssues  = FMath::Clamp(SplitIssues,  0.05f, 0.8f);
}

void SAstoryAchievementsEditor::SaveUiState()
{
	if (!GConfig)
	{
		return;
	}
	GConfig->SetString(AstoryUi::IniSection, TEXT("SearchText"), *SearchText, GEditorPerProjectIni);
	GConfig->SetInt(AstoryUi::IniSection, TEXT("FilterCategory"), FilterCategory, GEditorPerProjectIni);
	GConfig->SetInt(AstoryUi::IniSection, TEXT("FilterGroup"), FilterGroup, GEditorPerProjectIni);
	GConfig->SetInt(AstoryUi::IniSection, TEXT("FilterClass"), FilterClass, GEditorPerProjectIni);
	GConfig->SetInt(AstoryUi::IniSection, TEXT("FilterGrade"), FilterGrade, GEditorPerProjectIni);
	GConfig->SetInt(AstoryUi::IniSection, TEXT("FilterEnabled"), FilterEnabled, GEditorPerProjectIni);
	GConfig->SetBool(AstoryUi::IniSection, TEXT("OnlyMismatch"), bOnlyMismatch, GEditorPerProjectIni);
	GConfig->SetBool(AstoryUi::IniSection, TEXT("OnlyUnreachable"), bOnlyUnreachable, GEditorPerProjectIni);
	GConfig->SetBool(AstoryUi::IniSection, TEXT("OnlyErrors"), bOnlyErrors, GEditorPerProjectIni);
	GConfig->SetBool(AstoryUi::IniSection, TEXT("OnlyNoServer"), bOnlyNoServer, GEditorPerProjectIni);
	GConfig->SetBool(AstoryUi::IniSection, TEXT("OnlyTruncDesc"), bOnlyTruncDesc, GEditorPerProjectIni);
	GConfig->SetBool(AstoryUi::IniSection, TEXT("ShowAllColumns"), bShowAllColumns, GEditorPerProjectIni);
	GConfig->SetString(AstoryUi::IniSection, TEXT("SortColumn"), *SortColumn.ToString(), GEditorPerProjectIni);
	GConfig->SetInt(AstoryUi::IniSection, TEXT("SortMode"), (int32)SortMode, GEditorPerProjectIni);
	GConfig->SetFloat(AstoryUi::IniSection, TEXT("SplitFilters"), SplitFilters, GEditorPerProjectIni);
	GConfig->SetFloat(AstoryUi::IniSection, TEXT("SplitCenter"), SplitCenter, GEditorPerProjectIni);
	GConfig->SetFloat(AstoryUi::IniSection, TEXT("SplitCard"), SplitCard, GEditorPerProjectIni);
	GConfig->SetFloat(AstoryUi::IniSection, TEXT("SplitTable"), SplitTable, GEditorPerProjectIni);
	GConfig->SetFloat(AstoryUi::IniSection, TEXT("SplitIssues"), SplitIssues, GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

// ═════════════════════════════════════════════════════════════════════════════
// Дымовой тест виджета: собрать всё дерево Slate и тут же освободить.
//   Construct() создаёт панель инструментов, сводку, фильтры, таблицу, панель проблем и
//   карточку, а также загружает данные — то есть проходит по ВСЕМ именам стилей и кистей.
//   Неверное имя стиля UE логирует ошибкой, поэтому такой прогон ловит их headless,
//   не открывая владельцу ни одного окна.
// ═════════════════════════════════════════════════════════════════════════════
static FAutoConsoleCommand GAstoryTitlesUiSmokeCmd(
	TEXT("Astory.Titles.UiSmokeTest"),
	TEXT("Звания: собрать виджет редактора целиком и освободить (проверка стилей и данных)."),
	FConsoleCommandDelegate::CreateStatic([]()
	{
		if (!FSlateApplication::IsInitialized())
		{
			UE_LOG(LogAstoryAchUI, Warning, TEXT("дымовой тест пропущен: Slate не поднят"));
			return;
		}
		TSharedPtr<SAstoryAchievementsEditor> W = SNew(SAstoryAchievementsEditor);
		UE_LOG(LogAstoryAchUI, Log, TEXT("дымовой тест виджета: дерево собрано, ссылок %d"),
			W.GetSharedReferenceCount());
		W.Reset();
		UE_LOG(LogAstoryAchUI, Log, TEXT("дымовой тест виджета: освобождён, падений нет"));
	}));

#undef LOCTEXT_NAMESPACE
