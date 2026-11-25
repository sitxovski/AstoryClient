// Copyright (c) 2026 @sitxovski (Discord)
// Реализация панели «Студия нормалей» — см. SAsdaNormalStudio.h.
#include "SAsdaNormalStudio.h"
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "AsdaNormalStudio"

// Где лежат модели проекта. Папки ПЕРВОГО уровня под этим путём и становятся списком слева.
static const TCHAR* kModelsRoot = AsdaAssetPaths::kModelsRoot;   // ★ волна 2 п. 7: корень — реестр путей

void SAsdaNormalStudio::Construct(const FArguments& InArgs)
{
	ScanModels();
	if (Folders.Num() > 0)
	{
		CurrentFolder = *Folders[0];
	}

	ChildSlot
	[
		SNew(SSplitter)
		+ SSplitter::Slot().Value(0.20f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(4)
			[
				SNew(STextBlock).Text(LOCTEXT("FolderLabel", "Папка моделей"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4, 0)
			[
				SNew(SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&Folders)
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
				{
					return SNew(STextBlock).Text(FText::FromString(*Item));
				})
				.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
				{
					if (Item.IsValid())
					{
						CurrentFolder = *Item;
						RebuildList();
					}
				})
				[
					SNew(STextBlock).Text_Lambda([this]()
					{
						return FText::FromString(CurrentFolder.IsEmpty()
							? TEXT("—") : FString::Printf(TEXT("%s (%d)"), *CurrentFolder,
								ByFolder.Contains(CurrentFolder) ? ByFolder[CurrentFolder].Num() : 0));
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4)
			[
				SAssignNew(SearchBox, SSearchBox)
				.HintText(LOCTEXT("SearchHint", "поиск по имени"))
				.OnTextChanged_Lambda([this](const FText& T)
				{
					Filter = T.ToString();
					RebuildList();
				})
			]
			+ SVerticalBox::Slot().FillHeight(1.f).Padding(4)
			[
				SAssignNew(ListView, SListView<FEntry>)
				.ListItemsSource(&Visible)
				.OnGenerateRow(this, &SAsdaNormalStudio::OnGenerateRow)
				.OnSelectionChanged(this, &SAsdaNormalStudio::OnSelectionChanged)
				.SelectionMode(ESelectionMode::Single)
			]
		]
		+ SSplitter::Slot().Value(0.58f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SAssignNew(Viewport, SAsdaNormalStudioViewport)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4)
			[
				SNew(STextBlock).Text(this, &SAsdaNormalStudio::GetStatusText)
			]
		]
		+ SSplitter::Slot().Value(0.22f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot().Padding(6)
			[
				SNew(SVerticalBox)

				// ── быстрые пресеты сравнения ──
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).Padding(1)
					[
						SNew(SButton)
						.Text(LOCTEXT("Before", "ДО"))
						.ToolTipText(LOCTEXT("BeforeTip", "Как сейчас в игре: нормаль плоская, шероховатость 0.9, свечения нет"))
						.OnClicked_Lambda([this]()
						{
							Settings.NormalStrength = 0.f;
							Settings.RoughFromMap = 0.f;
							Settings.EmissiveGain = 0.f;
							Settings.Delight = 0.f;
							Settings.AoAmount = 0.f;
							PushSettings();
							return FReply::Handled();
						})
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).Padding(1)
					[
						SNew(SButton)
						.Text(LOCTEXT("After", "ПОСЛЕ"))
						.ToolTipText(LOCTEXT("AfterTip", "С картой нормалей, картой шероховатости и свечением"))
						.OnClicked_Lambda([this]()
						{
							Settings.NormalStrength = 1.f;
							Settings.RoughFromMap = 1.f;
							Settings.EmissiveGain = 1.f;
							Settings.Delight = 1.f;
							Settings.AoAmount = 1.f;
							PushSettings();
							return FReply::Handled();
						})
					]
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
				[
					SNew(STextBlock).Text(LOCTEXT("ReliefGroup", "Рельеф"))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeSliderRow(LOCTEXT("NormalStrength", "сила нормали"), 0.f, 4.f,
						[this]() { return Settings.NormalStrength; },
						[this](float V) { Settings.NormalStrength = V; PushSettings(); })
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeSliderRow(LOCTEXT("RoughFromMap", "шероховатость из карты"), 0.f, 1.f,
						[this]() { return Settings.RoughFromMap; },
						[this](float V) { Settings.RoughFromMap = V; PushSettings(); })
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeSliderRow(LOCTEXT("RoughConst", "шероховатость-константа"), 0.05f, 1.f,
						[this]() { return Settings.RoughConst; },
						[this](float V) { Settings.RoughConst = V; PushSettings(); })
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					// ★ Делайтинг: снять с базового цвета тени, нарисованные художником в 2008-м.
					//   Без него поверхность не отзывается на поворот источника — свет «приклеен».
					MakeSliderRow(LOCTEXT("Delight", "убрать нарисованный свет"), 0.f, 1.f,
						[this]() { return Settings.Delight; },
						[this](float V) { Settings.Delight = V; PushSettings(); })
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					// ★ Затенение щелей: гасит рассеянный свет там, куда он не достаёт. Именно оно
					//   читается как глубина — одной карты нормалей для этого мало.
					MakeSliderRow(LOCTEXT("Ao", "затенение щелей"), 0.f, 1.f,
						[this]() { return Settings.AoAmount; },
						[this](float V) { Settings.AoAmount = V; PushSettings(); })
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeSliderRow(LOCTEXT("Emissive", "свечение"), 0.f, 6.f,
						[this]() { return Settings.EmissiveGain; },
						[this](float V) { Settings.EmissiveGain = V; PushSettings(); })
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeSliderRow(LOCTEXT("SpecScale", "сила блика (альфа)"), 0.f, 1.f,
						[this]() { return Settings.SpecScale; },
						[this](float V) { Settings.SpecScale = V; PushSettings(); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]()
					{
						return Settings.bAlphaIsSpecular ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this](ECheckBoxState S)
					{
						Settings.bAlphaIsSpecular = (S == ECheckBoxState::Checked);
						PushSettings();
					})
					.ToolTipText(LOCTEXT("AlphaTip", "Персонаж/экип: альфа текстуры — маска блика (эталон EmptyProject.fx). Снято — альфа режет прозрачность (листва, решётки)"))
					[
						SNew(STextBlock).Text(LOCTEXT("AlphaSpec", "альфа = блик, а не прозрачность"))
					]
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(0, 8)
				[
					SNew(SSeparator)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(LOCTEXT("LightGroup", "Свет"))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeSliderRow(LOCTEXT("LightYaw", "поворот"), -180.f, 180.f,
						[this]() { return LightYaw; },
						[this](float V) { LightYaw = V; if (Viewport.IsValid()) { Viewport->SetLightAngles(LightYaw, LightPitch); } })
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeSliderRow(LOCTEXT("LightPitch", "высота"), -89.f, 20.f,
						[this]() { return LightPitch; },
						[this](float V) { LightPitch = V; if (Viewport.IsValid()) { Viewport->SetLightAngles(LightYaw, LightPitch); } })
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeSliderRow(LOCTEXT("LightBrightness", "яркость"), 0.f, 12.f,
						[this]() { return LightBrightness; },
						[this](float V) { LightBrightness = V; if (Viewport.IsValid()) { Viewport->SetLightBrightness(V); } })
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeSliderRow(LOCTEXT("SkyBrightness", "небо (заполняющий)"), 0.f, 6.f,
						[this]() { return SkyBrightness; },
						[this](float V) { SkyBrightness = V; if (Viewport.IsValid()) { Viewport->SetSkyBrightness(V); } })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
				[
					SNew(SCheckBox)
					.OnCheckStateChanged_Lambda([this](ECheckBoxState S)
					{
						if (Viewport.IsValid()) { Viewport->SetLightSpin(S == ECheckBoxState::Checked); }
					})
					[
						SNew(STextBlock).Text(LOCTEXT("LightSpin", "вращать свет"))
					]
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(0, 8)
				[
					SNew(SSeparator)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(LOCTEXT("SceneGroup", "Сцена"))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
				[
					SNew(SCheckBox)
					.OnCheckStateChanged_Lambda([this](ECheckBoxState S)
					{
						if (Viewport.IsValid()) { Viewport->SetAutoRotate(S == ECheckBoxState::Checked); }
					})
					[
						SNew(STextBlock).Text(LOCTEXT("SpinModel", "вращать модель"))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
				[
					SNew(SCheckBox)
					.IsChecked(ECheckBoxState::Checked)
					.OnCheckStateChanged_Lambda([this](ECheckBoxState S)
					{
						if (Viewport.IsValid()) { Viewport->SetShowFloor(S == ECheckBoxState::Checked); }
					})
					[
						SNew(STextBlock).Text(LOCTEXT("Floor", "пол и тени на полу"))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
				[
					SNew(SCheckBox)
					.IsChecked(ECheckBoxState::Checked)
					.OnCheckStateChanged_Lambda([this](ECheckBoxState S)
					{
						if (Viewport.IsValid()) { Viewport->SetShowEnvironment(S == ECheckBoxState::Checked); }
					})
					[
						SNew(STextBlock).Text(LOCTEXT("Env", "окружение (отражения)"))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 8)
				[
					SNew(SButton)
					.Text(LOCTEXT("Focus", "Вписать в кадр"))
					.OnClicked_Lambda([this]()
					{
						if (Viewport.IsValid()) { Viewport->FocusOnPreview(); }
						return FReply::Handled();
					})
				]
			]
		]
	];

	RebuildList();
	PushSettings();
	if (Viewport.IsValid())
	{
		Viewport->SetLightAngles(LightYaw, LightPitch);
		Viewport->SetLightBrightness(LightBrightness);
		Viewport->SetSkyBrightness(SkyBrightness);
	}
}

// ── список моделей ──────────────────────────────────────────────────────────
void SAsdaNormalStudio::ScanModels()
{
	ByFolder.Reset();
	Folders.Reset();

	FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	FARFilter ArFilter;
	ArFilter.bRecursivePaths = true;
	ArFilter.PackagePaths.Add(FName(kModelsRoot));
	ArFilter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
	ArFilter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName());

	TArray<FAssetData> Assets;
	Registry.Get().GetAssets(ArFilter, Assets);

	const FString Root = FString(kModelsRoot) + TEXT("/");
	for (const FAssetData& A : Assets)
	{
		FString Path = A.PackagePath.ToString();
		if (!Path.StartsWith(Root))
		{
			continue;
		}
		// Папка первого уровня под Models — она и есть раздел списка (Char, Mob, NPC, Skeletal…).
		FString Rest = Path.RightChop(Root.Len());
		FString Folder = Rest;
		int32 Slash = INDEX_NONE;
		if (Rest.FindChar(TEXT('/'), Slash))
		{
			Folder = Rest.Left(Slash);
		}
		ByFolder.FindOrAdd(Folder).Add(MakeShared<FAssetData>(A));
	}

	TArray<FString> Keys;
	ByFolder.GetKeys(Keys);
	Keys.Sort();
	for (const FString& K : Keys)
	{
		ByFolder[K].Sort([](const FEntry& A, const FEntry& B)
		{
			return A->AssetName.LexicalLess(B->AssetName);
		});
		Folders.Add(MakeShared<FString>(K));
	}
}

void SAsdaNormalStudio::RebuildList()
{
	Visible.Reset();
	if (const TArray<FEntry>* Items = ByFolder.Find(CurrentFolder))
	{
		for (const FEntry& E : *Items)
		{
			if (Filter.IsEmpty() || E->AssetName.ToString().Contains(Filter))
			{
				Visible.Add(E);
			}
		}
	}
	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SAsdaNormalStudio::OnGenerateRow(FEntry Entry, const TSharedRef<STableViewBase>& Owner)
{
	const bool bSkeletal = Entry->AssetClassPath == USkeletalMesh::StaticClass()->GetClassPathName();
	return SNew(STableRow<FEntry>, Owner)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
			[
				SNew(STextBlock)
				.Text(FText::FromString(bSkeletal ? TEXT("[S]") : TEXT("[M]")))
				.ColorAndOpacity(FSlateColor(bSkeletal ? FLinearColor(0.55f, 0.75f, 1.f)
					: FLinearColor(0.75f, 0.75f, 0.78f)))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(STextBlock).Text(FText::FromName(Entry->AssetName))
			]
		];
}

void SAsdaNormalStudio::OnSelectionChanged(FEntry Entry, ESelectInfo::Type)
{
	if (!Entry.IsValid() || !Viewport.IsValid())
	{
		return;
	}
	UObject* Obj = Entry->GetAsset();
	CurrentModel = Entry->AssetName.ToString();
	// Персонажи, экипировка и оружие в оригинале используют альфу как маску блика, остальное — как
	// маску прозрачности (см. эталон EmptyProject.fx, техники tSkin/tRigid против tNoSkin).
	const FString Folder = CurrentFolder;
	const bool bAlphaSpec = Folder.Contains(TEXT("Char")) || Folder.Contains(TEXT("Skeletal"));
	Settings.bAlphaIsSpecular = bAlphaSpec;
	Viewport->SetPreview(Cast<UStaticMesh>(Obj), Cast<USkeletalMesh>(Obj), bAlphaSpec);
	PushSettings();
}

// ── правая панель ───────────────────────────────────────────────────────────
TSharedRef<SWidget> SAsdaNormalStudio::MakeSliderRow(const FText& Label, float Min, float Max,
	TFunction<float()> Get, TFunction<void(float)> Set)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.58f).VAlign(VAlign_Center).Padding(0, 1)
		[
			SNew(STextBlock).Text(Label)
		]
		+ SHorizontalBox::Slot().FillWidth(0.42f).Padding(2, 1)
		[
			SNew(SSpinBox<float>)
			.MinValue(Min).MaxValue(Max)
			.MinSliderValue(Min).MaxSliderValue(Max)
			.Delta(0.01f)
			.Value_Lambda([Get]() { return Get(); })
			.OnValueChanged_Lambda([Set](float V) { Set(V); })
		];
}

void SAsdaNormalStudio::PushSettings()
{
	if (Viewport.IsValid())
	{
		Viewport->ApplySettings(Settings);
	}
}

FText SAsdaNormalStudio::GetStatusText() const
{
	if (CurrentModel.IsEmpty())
	{
		return LOCTEXT("PickModel", "выберите модель слева");
	}
	const int32 With = Viewport.IsValid() ? Viewport->GetSlotsWithMaps() : 0;
	const int32 Total = Viewport.IsValid() ? Viewport->GetSlotsTotal() : 0;
	return FText::FromString(FString::Printf(
		TEXT("%s · слотов с картами %d из %d%s"),
		*CurrentModel, With, Total,
		(With < Total) ? TEXT("  (для остальных карт нет — показаны как сейчас)") : TEXT("")));
}

#undef LOCTEXT_NAMESPACE
