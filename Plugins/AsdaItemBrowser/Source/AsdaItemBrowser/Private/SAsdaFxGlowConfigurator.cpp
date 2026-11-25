// Copyright (c) 2026 @sitxovski (Discord)
// Реализация вкладки «Конфигуратор свечения Asda» — см. SAsdaFxGlowConfigurator.h.
#include "SAsdaFxGlowConfigurator.h"
#include "SAsdaItemPreviewViewport.h"
#include "AsdaItemBrowserData.h"

#include "AsdaEffectActor.h"
#include "World/AsdaFxLightSettings.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Styling/AppStyle.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "AssetRegistry/AssetRegistryModule.h"   // пример эффекта семейства для превью
#include "Internationalization/Regex.h"
#include "Misc/MessageDialog.h"               // ★ ревизия 13.09: вопрос о несохранённых правках при закрытии
#include "Misc/ConfigCacheIni.h"              // ★ волна Р0 п.2: раскрытие разделов помнится между сеансами

#define LOCTEXT_NAMESPACE "AsdaFxGlowConfigurator"

void SAsdaFxGlowConfigurator::Construct(const FArguments& InArgs)
{
	Model = MakeShared<FAsdaFxGlowConfigModel>();
	// ЛЕНИВО: экспорт JSON и таблицы предметов грузятся только при открытии вкладки
	if (!Model->Reload()) { Status = FString::Printf(TEXT("ОШИБКА ЗАГРУЗКИ: %s"), *Model->GetError()); }
	RebuildTree();

	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)

		// ── ЛЕВО: поиск + дерево ────────────────────────────────────────────
		+ SSplitter::Slot()
		.Value(0.34f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
			[
				SNew(SSearchBox)
				.HintText(LOCTEXT("SearchHint", "Поиск: название или код предмета, имя .jeb, семейство"))
				.OnTextChanged_Lambda([this](const FText& T) { Filter = T.ToString(); RebuildTree(); })
			]
			// ★ Волна Р1 (п.8): фильтр «только изменённые»
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 0.f, 4.f, 2.f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bOnlyChanged ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bOnlyChanged = (S == ECheckBoxState::Checked); RebuildTree(); })
				// ★ Ревизия Ф11 (С3): подсказка больше не обещает того, чего фильтр не делает. Отметка ● по
				//   ассету ставится только у эффектов, чей класс УЖЕ в памяти редактора (раскрытых или правленых):
				//   опросить 1283 незагруженных ассета ради дерева нельзя — это минуты и сотни мегабайт
				// ★★★ Ревизия Ф12 (С3): и подсказка, и сам фильтр теперь ЧЕСТНЫ про третье состояние «?».
				//   Прежняя фраза «раскройте его узел, отметка появится» была невыполнима: под фильтром
				//   такого узла в дереве нет вовсе — раскрывать нечего. Теперь сказано, что делать на самом деле
				.ToolTipText(LOCTEXT("OnlyChangedTip", "Оставить в дереве только то, у чего есть правки: ● — уже записанные, * — сделанные в этом сеансе и ещё не записанные.\n\n★ ТРИ СОСТОЯНИЯ, А НЕ ДВА. По ТАБЛИЦЕ свечения ● ставится всегда. Про ПОЭМИТТЕРНЫЕ числа (они лежат в самом ассете) мы знаем правду только у эффектов, чей ассет уже в памяти редактора — открытых, раскрытых или правленных. У остальных состояние НЕИЗВЕСТНО, и в дереве они помечены «?»: это не «правок нет», это «не знаем». Опрашивать ради отметки 1283 незагруженных ассета нельзя — это минуты и сотни мегабайт.\n\n★ ПОД ЭТИМ ФИЛЬТРОМ «?» НЕ ПОКАЗЫВАЮТСЯ (иначе в списке оказался бы почти весь архив) — их число напечатано строкой ниже. Ищете эффект с поэмиттерной настройкой из ПРОШЛОГО сеанса: снимите фильтр, найдите эффект поиском и раскройте его узел — ассет загрузится, и «?» сменится на ● или пропадёт."))
				[
					SNew(STextBlock).Text(LOCTEXT("OnlyChanged", "Только изменённые (● и *)"))
				]
			]
			// ★★★ Ревизия Ф12 (С3): ЧЕСТНОЕ ЧИСЛО «ЧЕГО МЫ НЕ ЗНАЕМ» — прямо под фильтром, а не только в подсказке
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 0.f, 4.f, 2.f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Visibility_Lambda([this]() { return (Model.IsValid() && Model->NumFxGlowUnknown() > 0) ? EVisibility::Visible : EVisibility::Collapsed; })
				.Text_Lambda([this]()
				{
					return FText::FromString(FString::Printf(
						TEXT("? — состояние неизвестно у %d эффект(ов): их ассеты не загружены, поэмиттерные числа прочитать нечем.%s"),
						Model.IsValid() ? Model->NumFxGlowUnknown() : 0,
						bOnlyChanged ? TEXT(" Под фильтром они не показаны.") : TEXT("")));
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 0.f, 4.f, 4.f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("TreeHelp", "● — правка уже записана, * — ещё нет. Правка видна сразу (окно уровня, PIE, превью) — в том числе у эффектов, УЖЕ РАЗМЕЩЁННЫХ на картах; в игру и упаковку она попадает после «Сохранить». ★ Настройки ЭМИТТЕРОВ — исключение: они лежат в самом ассете BP_Fx_…, «Сохранить» их не пишет. Правка эмиттера сразу помечает ассет изменённым, и на диск он попадает штатным «Save All» редактора."))
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				[
					SAssignNew(TreeView, STreeView<FNodePtr>)
					.TreeItemsSource(&RootNodes)
					.OnGenerateRow(this, &SAsdaFxGlowConfigurator::OnGenerateRow)
					.OnGetChildren(this, &SAsdaFxGlowConfigurator::OnGetChildren)
					.OnSelectionChanged(this, &SAsdaFxGlowConfigurator::OnSelectionChanged)
					// ★ Волна Р1 (п.7): ассет эффекта грузится ровно здесь — при раскрытии ЕГО узла,
					//   а не в OnGetChildren (её STreeView зовёт для всей раскрытой ветки разом)
					.OnExpansionChanged_Lambda([this](FNodePtr N, bool bExpanded)
					{
						if (bExpanded && N.IsValid() && N->bNeedEmitters)
						{
							Model->BuildEmitterChildren(N);
							if (TreeView.IsValid()) { TreeView->RequestTreeRefresh(); }
						}
					})
					.SelectionMode(ESelectionMode::Single)
				]
			]
		]

		// ── ПРАВО: превью + карточка + поля ────────────────────────────────
		+ SSplitter::Slot()
		.Value(0.66f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Vertical)
			+ SSplitter::Slot()
			.Value(0.36f)
			[
				SAssignNew(Viewport, SAsdaItemPreviewViewport)
			]
			+ SSplitter::Slot()
			.Value(0.64f)
			[
				SNew(SVerticalBox)
				// панель действий
				+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
					[
						SNew(SButton)
						.Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("Сохранить (%d)"), Model->NumDirty())); })
						.ToolTipText(LOCTEXT("SaveTip", "Записать все несохранённые правки: Tools/asda_fx_light.json (бэкап в _backup/<дата>_fx_configurator) → генератор Config/DefaultGame.ini → проверка --check. После перезапуска редактора, в игре и в упаковке действует ровно это."))
						.IsEnabled_Lambda([this]() { return Model->NumDirty() > 0; })
						.OnClicked(this, &SAsdaFxGlowConfigurator::OnSave)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("Revert", "Отменить несохранённое"))
						// ★★★ Ревизия Ф13 (МЕЛК): ПОДСКАЗКА БОЛЬШЕ НЕ ОБЕЩАЕТ Ctrl+Z БЕЗУСЛОВНО.
						//   Модель знает ФАКТ — попала ли последняя правка в буфер отмены (UObject::Modify
						//   это возвращает) и сколько правок за сеанс прошло мимо. Эти числа и показываем:
						//   прежний текст обещал отмену даже в прогоне, где буфера отмены нет вовсе
						.ToolTipText_Lambda([]()
						{
							const bool bUndo = FAsdaFxGlowConfigModel::LastEmitterEditUndoable();
							const int32 NoUndo = FAsdaFxGlowConfigModel::NumNonUndoableEmitterEdits();
							const int32 Resync = FAsdaFxGlowConfigModel::NumUndoResynced();
							return FText::FromString(FString::Printf(TEXT(
								"Выбросить все несохранённые правки ТАБЛИЦЫ: она перечитывается из Config/DefaultGame.ini.\n\n"
								"★ Правок ЭМИТТЕРОВ это не касается — они лежат в ассетах BP_Fx_…. Для них есть «Вернуть эмиттер как в ассете» и «Сброс» у поля.\n\n"
								"★ Ctrl+Z: последняя правка эмиттера %s. Правок мимо буфера отмены за сеанс: %d (в норме 0).\n"
								"★ Отмена раздаётся и размещённым на картах экземплярам (ревизия Ф13): после Ctrl+Z у них не остаётся числа, отличного от ассета, и сохранение карты ничего не впечатает в .umap. Пересинхронизировано за сеанс: %d актор(ов).\n"
								"Самый предсказуемый путь отката всё равно «Вернуть эмиттер как в ассете»: он идёт тем же путём, что и правка."),
								bUndo ? TEXT("В БУФЕРЕ ОТМЕНЫ (Ctrl+Z её вернёт)") : TEXT("★ В БУФЕР НЕ ПОПАЛА — Ctrl+Z её НЕ вернёт, пользуйтесь «Вернуть эмиттер как в ассете»"),
								NoUndo, Resync));
						})
						.IsEnabled_Lambda([this]() { return Model->NumDirty() > 0; })
						.OnClicked(this, &SAsdaFxGlowConfigurator::OnRevert)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
					[
						SNew(SButton)
						// ★ Ревизия Ф11 (К2): у ЭМИТТЕРА та же кнопка делает единственное, что ему нужно, —
						//   возвращает его ровно к тому, что лежало в ассете до правок этого сеанса. Прежде
						//   она была просто недоступна, и вернуть исходные числа было нечем
						.Text_Lambda([this]() { return IsEmitterMode() ? LOCTEXT("RemoveEm", "Вернуть эмиттер как в ассете") : LOCTEXT("Remove", "Сбросить всё у этой записи"); })
						.ToolTipText_Lambda([this]() { return IsEmitterMode()
							? LOCTEXT("RemoveEmTip", "Вернуть ВСЕ настройки свечения этого эмиттера к тому, что лежало в ассете до правок этого сеанса: и «откуда числа», и сами числа. Снимок берётся сам, перед первой правкой каждого эмиттера.")
							: LOCTEXT("RemoveTip", "Снять переопределение предмета/эффекта целиком — числа вернутся к наследуемым (эффект → семейство). У семейства сбрасывайте поля по одному."); })
						.IsEnabled_Lambda([this]() { if (IsEmitterMode()) { return Model->HasEmitterBaseline(CurrentEmitter()); } const FAsdaGlowTarget T = CurrentTarget(); return T.IsValid() && !T.bFamily; })
						.OnClicked(this, &SAsdaFxGlowConfigurator::OnRemoveOverride)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("Stand", "Стенд в PIE"))
						// ★ Волна Р0 (п.4): подсказка про ШТАТНУЮ визуализацию движка — она уже работает и
						//   ничего не стоит, но о ней никто не знал, и форму приходилось проверять на глаз
						.ToolTipText(LOCTEXT("StandTip", "Свет эффекта в превью вкладки не виден (мир превью не участвует в эффект-светах). Запустите Play (PIE) на тестовой карте и нажмите — перед камерой встанет стенд asda.Fx.Light.Stand с этим эффектом и кодом предмета.\n\n★ ФОРМУ И ДАЛЬНОСТЬ МОЖНО УВИДЕТЬ И БЕЗ PIE, прямо в окне уровня: выделите актор эффекта на карте и включите Show → Advanced → Light Radius. Движок сам нарисует сферу затухания, коробку прямоугольника со «шторками» и конус прожектора — рисовальщик ищется по РОДИТЕЛЬСКИМ классам (UnrealEdEngine.cpp:1489-1497), а наш UAsdaFxLightComponent унаследован от UPointLightComponent, поэтому это работает даром. ★ Набор рисовальщиков пересчитывается ТОЛЬКО при смене выделения (EditorSelectUtils.cpp:297-348): поменяли форму — снимите и верните выделение."))
						.IsEnabled_Lambda([this]() { return GEditor && GEditor->PlayWorld && Selected.IsValid() && !Selected->Jeb.IsEmpty(); })
						.OnClicked(this, &SAsdaFxGlowConfigurator::OnStandInPie)
					]
					// ★ Волна Р1 (п.7): поэмиттерные настройки лежат в ассете — «Сохранить» их не пишет
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
					[
						SNew(SButton)
						// ★★★ Ревизия Ф11 (К3): ЧИСЛО И ДЕЙСТВИЕ — ПРО ОДНО И ТО ЖЕ. Прежняя кнопка показывала
						//   «(5)» тронутых ассетов, а помечала ОДИН выбранный: четыре правки из пяти пропадали
						//   при закрытии редактора без вопроса — их пакеты не были изменёнными
						.Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("Пометить все тронутые ассеты (%d)"), Model->NumFxAssetsUnmarked())); })
						.ToolTipText(LOCTEXT("MarkDirtyTip", "Настройки свечения ЭМИТТЕРОВ хранятся в самом ассете BP_Fx_… (AAsdaEffectActor::Emitters[*].Glow и долговечная копия), а не в таблице Tools/asda_fx_light.json — ключа эмиттера в таблице нет вовсе, и «Сохранить» их не пишет.\n\n★ Пометку изменённого теперь ставит САМА правка (штатный Modify, как в панели деталей), поэтому в норме здесь ноль: редактор сам спросит про эти ассеты при выходе и покажет их в «Save All». Кнопка — страховка: проходит по ВСЕМ тронутым в этом сеансе ассетам и помечает тех, у кого признак снят.\n\n★ На диск ассеты пишет только ваше «Save All» — конфигуратор их не сохраняет."))
						.IsEnabled_Lambda([this]() { return Model->NumFxAssetsEdited() > 0; })
						.OnClicked(this, &SAsdaFxGlowConfigurator::OnMarkFxAssetDirty)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([this]() { return bShowAdvanced ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bShowAdvanced = (S == ECheckBoxState::Checked); RebuildFields(); })
						.ToolTipText(LOCTEXT("AdvancedTip", "Показать и редкие поля — те, что в панели деталей лежат под стрелкой «Advanced»."))
						[
							SNew(STextBlock).Text(LOCTEXT("Advanced", "Все поля"))
						]
					]
					// ★ Волна Р0 (п.1): по умолчанию поля, не работающие при нынешних числах, скрыты
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([this]() { return bShowInactive ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bShowInactive = (S == ECheckBoxState::Checked); RebuildFields(); })
						.ToolTipText(LOCTEXT("InactiveTip", "По умолчанию скрыты поля, которые при нынешних числах НЕ РАБОТАЮТ: амплитуда мерцания при нулевой частоте, размеры прямоугольника при другой форме, тонкая настройка тени при выключенной тени. Условие берётся из того же meta EditCondition, по которому их прячет панель деталей. Включите галочку, если нужно увидеть весь набор целиком."))
						[
							SNew(STextBlock).Text(LOCTEXT("Inactive", "И неработающие сейчас"))
						]
					]
				]
				// уровень правки для ступени оружия
				+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 0.f, 4.f, 4.f)
				[
					SNew(SHorizontalBox)
					.Visibility_Lambda([this]() { return (Selected.IsValid() && Selected->Kind == EAsdaGlowNodeKind::ItemState) ? EVisibility::Visible : EVisibility::Collapsed; })
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)
					[
						SNew(SCheckBox)
						.Style(FAppStyle::Get(), "RadioButton")
						.IsChecked_Lambda([this]() { return bItemLevel ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState) { bItemLevel = true; RebuildFields(); RefreshPreviewGlow(); })
						[
							SNew(STextBlock).Text(LOCTEXT("LevelItem", "Только этот предмет"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SCheckBox)
						.Style(FAppStyle::Get(), "RadioButton")
						.IsChecked_Lambda([this]() { return bItemLevel ? ECheckBoxState::Unchecked : ECheckBoxState::Checked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState) { bItemLevel = false; RebuildFields(); RefreshPreviewGlow(); })
						[
							SNew(STextBlock).Text_Lambda([this]()
							{
								const int32 N = Selected.IsValid() ? Model->NumItemsWithJeb(Selected->Jeb) : 0;
								return FText::FromString(FString::Printf(TEXT("Эффект у всех предметов с ним (%d)"), N));
							})
						]
					]
				]
				// ★★★ ВОЛНА Р1 (п.7): ОТКУДА ЭМИТТЕР БЕРЁТ ЧИСЛА СВЕЧЕНИЯ.
				//   Три состояния, а не галочка: «света нет» нужно не реже прочих — у дыма и меток свет
				//   выключают поштучно, а пламя того же эффекта светит дальше (шапка FAsdaFxEmitterGlow)
				+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 0.f, 4.f, 4.f)
				[
					SNew(SHorizontalBox)
					.Visibility_Lambda([this]() { return IsEmitterMode() ? EVisibility::Visible : EVisibility::Collapsed; })
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
					[
						SNew(STextBlock).Text(LOCTEXT("EmSrc", "Свечение эмиттера:"))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)
					[
						SNew(SCheckBox)
						.Style(FAppStyle::Get(), "RadioButton")
						.ToolTipText(LOCTEXT("EmSrcFromTip", "Эмиттер светит числами самого эффекта (раздел полей ниже показывает их же, только для чтения смысла — правка любого поля переведёт эмиттер в «свои числа»)."))
						.IsChecked_Lambda([this]() { return Model->EmitterSource(CurrentEmitter()) == EAsdaFxEmitterGlowSource::FromEffect ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { if (S == ECheckBoxState::Checked) { Model->SetEmitterSource(CurrentEmitter(), EAsdaFxEmitterGlowSource::FromEffect); WarnIfEmitterEditNotUndoable(); RebuildFields(); RefreshPreviewGlow(); } })
						[ SNew(STextBlock).Text(LOCTEXT("EmSrcFrom", "от эффекта")) ]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)
					[
						SNew(SCheckBox)
						.Style(FAppStyle::Get(), "RadioButton")
						.ToolTipText(LOCTEXT("EmSrcManTip", "У эмиттера свои цвет, форма, размещение, радиус и сила. В момент перевода числа подхватываются от эффекта, поэтому картинка не прыгает. ★ Действует, когда у самого эффекта выбрано «Свет: свой у каждого эмиттера» (поле «Свет: один на эффект или у каждого эмиттера» в разделе «Свет»)."))
						.IsChecked_Lambda([this]() { return Model->EmitterSource(CurrentEmitter()) == EAsdaFxEmitterGlowSource::Manual ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { if (S == ECheckBoxState::Checked) { Model->SetEmitterSource(CurrentEmitter(), EAsdaFxEmitterGlowSource::Manual); WarnIfEmitterEditNotUndoable(); RebuildFields(); RefreshPreviewGlow(); } })
						[ SNew(STextBlock).Text(LOCTEXT("EmSrcMan", "вручную")) ]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SCheckBox)
						.Style(FAppStyle::Get(), "RadioButton")
						.ToolTipText(LOCTEXT("EmSrcOffTip", "Этот эмиттер не даёт источника вовсе — дым, пыль, метка. Сам эффект при этом продолжает светить остальными эмиттерами."))
						.IsChecked_Lambda([this]() { return Model->EmitterSource(CurrentEmitter()) == EAsdaFxEmitterGlowSource::Off ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { if (S == ECheckBoxState::Checked) { Model->SetEmitterSource(CurrentEmitter(), EAsdaFxEmitterGlowSource::Off); WarnIfEmitterEditNotUndoable(); RebuildFields(); RefreshPreviewGlow(); } })
						[ SNew(STextBlock).Text(LOCTEXT("EmSrcOff", "света нет")) ]
					]
				]
				// карточка
				+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 0.f, 4.f, 4.f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
					.Padding(6.f)
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.Text(this, &SAsdaFxGlowConfigurator::GetCardText)
					]
				]
				// поля
				+ SVerticalBox::Slot().FillHeight(1.f).Padding(4.f, 0.f, 4.f, 4.f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SAssignNew(FieldsBox, SVerticalBox)
						]
					]
				]
			]
		]
	];
	RebuildFields();
}

SAsdaFxGlowConfigurator::~SAsdaFxGlowConfigurator()
{
	// ★★★ РЕВИЗИЯ Ф11 (МЕЛК, 20.09.2026): ПОЧЕМУ ЗДЕСЬ ВЫБРАСЫВАЕТСЯ ТОЛЬКО ТАБЛИЦА.
	//   Вопрос владельцу уже задан (ConfirmCloseWithUnsaved: закрытие вкладки / выход из редактора).
	//   Правило одно и объяснимое: выбрасывается то, у чего БОЛЬШЕ НЕТ ХОЗЯИНА, и остаётся то, у чего
	//   хозяин есть.
	//     * несохранённые записи ТАБЛИЦЫ живут только в CDO настроек и в этой вкладке — закрыли
	//       вкладку, и увидеть их больше негде: на сцене им оставаться нельзя;
	//     * правки ЭМИТТЕРОВ живут в самих ассетах BP_Fx_…, их пакеты помечены изменёнными, и о них
	//       спросит сам редактор при выходе («Save Content»). Откатывать их здесь значило бы молча
	//       выбросить работу владельца (закон owner-asset-edits-never-lose). Вернуть эмиттер к
	//       состоянию ассета можно кнопкой «Вернуть эмиттер как в ассете» (★ ревизия Ф12: она же
	//       и надёжнее Ctrl+Z — отмена возвращает ассет, но не размещённые на картах экземпляры).
	if (Model.IsValid() && Model->NumDirty() > 0) { Model->Revert(); }
}

bool SAsdaFxGlowConfigurator::ConfirmCloseWithUnsaved()
{
	const int32 N = Model.IsValid() ? Model->NumDirty() : 0;
	// ★ Волна Р1 (п.7): правки эмиттеров лежат в ассетах и этим диалогом НЕ управляются — ни «Да», ни
	//   «Нет» их не касаются. Молчать об этом нельзя: владелец решит, что «Нет» отменило всё
	//   ★★★ Ревизия Ф11 (К3): считаются НЕ «все тронутые», а те, что ещё не на диске, и они
	//   перечисляются ПОИМЁННО — иначе «у 5 ассетов что-то правилось» не помогает найти их в «Save All»
	TArray<FString> Unsaved;
	if (Model.IsValid()) { Model->GetUnsavedFxAssets(Unsaved); }
	const int32 Touched = Unsaved.Num();
	FString List;
	for (int32 i = 0; i < Unsaved.Num() && i < 12; ++i) { List += FString::Printf(TEXT("\n    %s"), *Unsaved[i]); }
	if (Unsaved.Num() > 12) { List += FString::Printf(TEXT("\n    … и ещё %d"), Unsaved.Num() - 12); }
	if (N == 0 && Touched == 0) { return true; }
	if (N == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(
			TEXT("Записей таблицы свечения без сохранения нет, но у %d ассет(ов) правились настройки ЭМИТТЕРОВ и они ещё НЕ ЗАПИСАНЫ на диск:%s\n\n")
			TEXT("Эти ассеты уже помечены изменёнными — сохраните их штатным «Save All» редактора (о них же спросит и сам редактор при выходе). ")
			TEXT("Этот диалог их не пишет."), Touched, *List)),
			LOCTEXT("CloseEmitterTitle", "Правки эмиттеров — в ассетах"));
		return true;
	}
	const EAppReturnType::Type R = FMessageDialog::Open(EAppMsgType::YesNoCancel,
		FText::FromString(FString::Printf(
			TEXT("В «Конфигураторе свечения Asda» несохранённых записей: %d.\n\n")
			TEXT("Да — сохранить (Tools/asda_fx_light.json → Config/DefaultGame.ini)\n")
			TEXT("Нет — не сохранять, правки будут отменены\n")
			TEXT("Отмена — вернуться к правке\n\n")
			TEXT("★ Правки настроек ЭМИТТЕРОВ этот вопрос НЕ ЗАТРАГИВАЕТ: они лежат в самих ассетах BP_Fx_…, ")
			TEXT("уже помечены изменёнными и сохраняются штатным «Save All». Не записано на диск ассетов: %d%s"), N, Touched, *List)),
		LOCTEXT("CloseTitle", "Несохранённое свечение"));
	if (R == EAppReturnType::Yes)
	{
		FString Msg;
		if (Model->Save(Msg))
		{
			Status = Msg;
			return true;
		}
		// Не записалось ничего — закрывать нельзя, иначе правки пропадут молча
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Msg), LOCTEXT("SaveFailTitle", "Свечение не сохранено"));
		Status = Msg;
		return false;
	}
	if (R == EAppReturnType::No)
	{
		Model->Revert();
		return true;
	}
	return false;
}

// ── дерево ──────────────────────────────────────────────────────────────────
void SAsdaFxGlowConfigurator::RebuildTree()
{
	Model->BuildTree(Filter, bOnlyChanged, RootNodes);
	if (!TreeView.IsValid()) { return; }
	TreeView->RequestTreeRefresh();
	for (const FNodePtr& R : RootNodes)
	{
		TreeView->SetItemExpansion(R, true);
		// При поиске (и при фильтре «только изменённые») раскрываем и второй уровень — иначе
		// совпадения спрятаны в свёрнутых классах оружия и семействах эффектов
		if (!Filter.IsEmpty() || bOnlyChanged)
		{
			for (const FNodePtr& C : R->Children) { if (C->Kind == EAsdaGlowNodeKind::Group) { TreeView->SetItemExpansion(C, true); } }
		}
	}
}

void SAsdaFxGlowConfigurator::OnGetChildren(FNodePtr Node, TArray<FNodePtr>& OutChildren)
{
	// ★ Волна Р1 (п.7): здесь НИЧЕГО не грузится. STreeView зовёт OnGetChildren для КАЖДОГО узла
	//   раскрытой ветки (а не только для видимых строк), поэтому загрузка ассета отсюда означала бы
	//   «раскрыл семейство — загрузил все его эффекты». Ассет грузится в OnExpansionChanged, а до
	//   раскрытия у узла эффекта стоит один узел-заглушка, ради которого и рисуется стрелка
	if (Node.IsValid()) { OutChildren = Node->Children; }
}

TSharedRef<ITableRow> SAsdaFxGlowConfigurator::OnGenerateRow(FNodePtr Node, const TSharedRef<STableViewBase>& Owner)
{
	const bool bGroup = Node->Kind == EAsdaGlowNodeKind::Group;
	return SNew(STableRow<FNodePtr>, Owner)
	[
		SNew(STextBlock)
		.Font(bGroup ? FAppStyle::GetFontStyle("BoldFont") : FAppStyle::GetFontStyle("NormalFont"))
		.Text_Lambda([this, Node]()
		{
			// Отметка сохранённого переопределения — живая (после «Сохранить» дерево не пересобирается)
			bool bMark = Node->bHasOverride;
			bool bPend = Node->bDirty;
			if (Node->Kind == EAsdaGlowNodeKind::ItemState)
			{
				FAsdaGlowTarget A; A.Jeb = Node->Jeb; A.ItemId = Node->ItemCode;
				FAsdaGlowTarget B; B.Jeb = Node->Jeb; B.ItemId = 0;
				bMark = Model->HasSaved(A) || Model->HasSaved(B);
				bPend = Model->HasPending(A) || Model->HasPending(B);
			}
			else if (Node->Kind == EAsdaGlowNodeKind::Effect)
			{
				FAsdaGlowTarget A; A.Jeb = Node->Jeb; A.ItemId = Node->ItemCode;
				bMark = Model->HasSaved(A);
				// ★ Волна Р1: у эффекта «несохранённое» бывает двух родов — запись таблицы и правка
				//   поэмиттерных настроек в ассете; и то и другое ещё не лежит на диске
				bPend = Model->HasPending(A) || Model->IsFxAssetUnsaved(Node->FxClassPath);
			}
			else if (Node->Kind == EAsdaGlowNodeKind::Emitter)
			{
				FAsdaGlowEmitterRef R; R.ClassPath = Node->FxClassPath; R.Index = Node->EmitterIndex;
				bMark = Model->EmitterSource(R) != EAsdaFxEmitterGlowSource::FromEffect;
				// ★ Ревизия Ф11 (С8): * — только у ТОГО эмиттера, который отличается от ассета.
				//   Прежде его получали ВСЕ эмиттеры тронутого ассета, и по отметке нельзя было
				//   понять, какой из семи ты правил
				bPend = Model->IsEmitterPending(R);
			}
			// ● — уже записано (в таблице или в ассете на диске), * — ещё не записано,
			// ★ Ревизия Ф12 (С3): ? — НЕ ЗНАЕМ: ассет эффекта не загружен, поэмиттерные числа
			//   прочитать нечем. Раньше здесь было пусто, то есть уверенное «правок нет»
			const TCHAR* Pre = bMark ? TEXT("● ") : (Node->bMarkUnknown ? TEXT("? ") : TEXT(""));
			return FText::FromString(FString::Printf(TEXT("%s%s%s"), Pre, *Node->Label, bPend ? TEXT("  *") : TEXT("")));
		})
	];
}

void SAsdaFxGlowConfigurator::OnSelectionChanged(FNodePtr Node, ESelectInfo::Type)
{
	Selected = Node;
	bItemLevel = true;
	RebuildFields();
	RefreshPreview();
}

// ── цель и поля ─────────────────────────────────────────────────────────────
FAsdaGlowTarget SAsdaFxGlowConfigurator::CurrentTarget() const
{
	FAsdaGlowTarget T;
	if (!Selected.IsValid()) { return T; }
	const TSharedPtr<FAsdaItemBrowserData> Items = Model->GetItems();
	auto ItemLabel = [&Items](int32 Idx, int32 Code)
	{
		const FString N = (Items.IsValid() && Idx != INDEX_NONE) ? Items->GetName(Idx) : FString();
		return FString::Printf(TEXT("%d : %s"), Code, N.IsEmpty() ? TEXT("(имени нет)") : *N);
	};
	switch (Selected->Kind)
	{
	case EAsdaGlowNodeKind::Family:
		T.bFamily = true; T.FamilyId = Selected->FamilyId; T.Label = Selected->FamilyId;
		break;
	case EAsdaGlowNodeKind::Effect:
		T.Jeb = Selected->Jeb; T.ItemId = Selected->ItemCode;
		T.Label = Selected->ItemCode ? ItemLabel(Selected->ItemIndex, Selected->ItemCode) : TEXT("блёстки дропа — все предметы");
		break;
	case EAsdaGlowNodeKind::ItemState:
		T.Jeb = Selected->Jeb; T.ItemId = bItemLevel ? Selected->ItemCode : 0;
		T.Label = bItemLevel ? FString::Printf(TEXT("%s +%d"), *ItemLabel(Selected->ItemIndex, Selected->ItemCode), Selected->Enhance)
			: FString::Printf(TEXT("эффект %s у всех предметов"), *Selected->Jeb);
		break;
	case EAsdaGlowNodeKind::Emitter:
		// ★ Волна Р1 (п.7): у эмиттера цель таблицы — его ЭФФЕКТ. Сами поля эмиттера в таблицу не
		//   ложатся (ключа эмиттера в ней нет), но карточке и «Стенду в PIE» нужен именно эффект
		T.Jeb = Selected->Jeb; T.ItemId = Selected->ItemCode;
		T.Label = FString::Printf(TEXT("эффект %s у всех предметов"), *Selected->Jeb);
		break;
	default:
		break;
	}
	return T;
}

// ── ★ ВОЛНА Р1 (п.7): единый доступ к правимым числам ───────────────────────
bool SAsdaFxGlowConfigurator::IsEmitterMode() const
{
	return Selected.IsValid() && Selected->Kind == EAsdaGlowNodeKind::Emitter;
}

FAsdaGlowEmitterRef SAsdaFxGlowConfigurator::CurrentEmitter() const
{
	FAsdaGlowEmitterRef R;
	if (IsEmitterMode()) { R.ClassPath = Selected->FxClassPath; R.Index = Selected->EmitterIndex; }
	return R;
}

FAsdaFxLightSpec SAsdaFxGlowConfigurator::WorkSpec() const
{
	if (IsEmitterMode()) { return Model->EmitterWork(CurrentEmitter()); }
	return Model->GetState(CurrentTarget()).Work;
}

// ★★★ ВОЛНА Ф13 (защита): правка не попала в буфер отмены — сказать вслух, один раз за сеанс.
//   Обоснование целиком — у объявления в заголовке. Показывается ПОСЛЕ правки, потому что до неё
//   результат Modify неизвестен: это замер, а не предсказание.
void SAsdaFxGlowConfigurator::WarnIfEmitterEditNotUndoable()
{
	if (bUndoWarningShown || FAsdaFxGlowConfigModel::LastEmitterEditUndoable()) { return; }
	bUndoWarningShown = true;
	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT(
		"★ Эта правка эмиттера НЕ попала в буфер отмены редактора.\n\n"
		"Значит, Ctrl+Z её не вернёт — и, что опаснее, отменит ваше ПРЕДЫДУЩЕЕ действие, о котором вы уже не думаете.\n\n"
		"Откатывайте правки эмиттеров кнопкой «Вернуть эмиттер как в ассете»: она возвращает и числа, и долговечную копию внутри ассета, и раздаёт откат эффектам, уже размещённым на картах.\n\n"
		"Правок мимо буфера отмены за сеанс: %d. Это предупреждение показывается один раз."),
		FAsdaFxGlowConfigModel::NumNonUndoableEmitterEdits())),
		LOCTEXT("NoUndoTitle", "Ctrl+Z эту правку не вернёт"));
}

void SAsdaFxGlowConfigurator::CommitField(const FAsdaGlowField& F, const FAsdaFxLightSpec& Src)
{
	if (IsEmitterMode()) { Model->SetEmitterField(CurrentEmitter(), F, Src); WarnIfEmitterEditNotUndoable(); }
	else { Model->SetField(CurrentTarget(), F, Src); }
	RefreshPreviewGlow();
}

void SAsdaFxGlowConfigurator::ResetFieldCur(const FAsdaGlowField& F)
{
	if (IsEmitterMode()) { Model->ResetEmitterField(CurrentEmitter(), F); WarnIfEmitterEditNotUndoable(); }
	else { Model->ResetField(CurrentTarget(), F); }
	RefreshPreviewGlow();
}

bool SAsdaFxGlowConfigurator::FieldOwn(const FAsdaGlowField& F) const
{
	if (IsEmitterMode()) { return Model->IsEmitterFieldOwn(CurrentEmitter(), F); }
	return Model->GetState(CurrentTarget()).IsOwn(F.CppName);
}

bool SAsdaFxGlowConfigurator::FieldPending(const FAsdaGlowField& F) const
{
	// ★ Ревизия Ф11 (С8): у эмиттера «несохранено» считается ПО ПОЛЮ — сравнением со снимком ассета,
	//   а не «весь ассет тронут и поле чем-то отличается от эффекта» (прежняя отметка врала на полях,
	//   которые в ассете и так были свои)
	if (IsEmitterMode()) { return Model->IsEmitterFieldPending(CurrentEmitter(), F); }
	const FAsdaGlowEditState& St = Model->GetState(CurrentTarget());
	return St.PendingSet.Contains(F.CppName) || St.PendingReset.Contains(F.CppName);
}

FString SAsdaFxGlowConfigurator::FieldDefaultText(const FAsdaGlowField& F) const
{
	if (IsEmitterMode())
	{
		// Наследуемое значение эмиттера — число самого эффекта (категория Effect Glow ассета)
		const FAsdaFxLightSpec Eff = Model->EmitterInherited(CurrentEmitter());
		return FAsdaFxGlowConfigModel::ValueText(F, Eff) + TEXT("   (от эффекта)");
	}
	const FAsdaGlowEditState& St = Model->GetState(CurrentTarget());
	if (St.DefaultUnset.Contains(F.CppName))
	{
		return F.CppName == GET_MEMBER_NAME_CHECKED(FAsdaFxLightSpec, EmissiveBoost)
			? FString(TEXT("не задано — рисуется материал эмиттера")) : FString(TEXT("нет"));
	}
	return FAsdaFxGlowConfigModel::ValueText(F, St.Defaults);
}

bool SAsdaFxGlowConfigurator::CanResetField(const FAsdaGlowField& F) const
{
	// ★ Ревизия Ф11 (С4): у поля «только чтение» сбрасывать нечего — кнопка обязана быть серой
	if (F.bReadOnly) { return false; }
	return FieldOwn(F);
}

void SAsdaFxGlowConfigurator::RebuildFields()
{
	if (!FieldsBox.IsValid()) { return; }
	FieldsBox->ClearChildren();
	const FAsdaGlowTarget T = CurrentTarget();
	CardText.Empty();
	if (!Model->IsReady())
	{
		CardText = FString::Printf(TEXT("Конфигуратор не загрузился: %s"), *Model->GetError());
		return;
	}
	const bool bEmitter = IsEmitterMode();
	if (!T.IsValid() && !bEmitter)
	{
		CardText = TEXT("Выберите слева семейство, эффект карты или навыка, блёстки дропа, ступень свечения предмета (+5 / +6 / +7) — или отдельный ЭМИТТЕР внутри эффекта (раскройте узел эффекта).");
		return;
	}

	// ── карточка ──
	const TSharedPtr<FAsdaItemBrowserData> Items = Model->GetItems();
	FString C;
	if (bEmitter)
	{
		// ★★★ ВОЛНА Р1 (п.7): КАРТОЧКА ЭМИТТЕРА. Главное, что она обязана сказать, — «Сохранить» сюда
		//   не доедет: числа эмиттера лежат в ассете, а не в таблице Tools/asda_fx_light.json
		const FAsdaGlowEmitterRef R = CurrentEmitter();
		const AAsdaEffectActor* Cdo = Model->EmitterCdo(R);
		C += FString::Printf(TEXT("ЭМИТТЕР %d эффекта %s   семейство %s\n"), R.Index, *T.Jeb, *Model->FamilyOfJeb(T.Jeb));
		C += FString::Printf(TEXT("Ассет: %s\n"), *R.ClassPath.LeftChop(2));
		if (Cdo)
		{
			C += FString::Printf(TEXT("Эмиттеров у эффекта: %d. Охват света у самого эффекта: «%s».\n"), Cdo->Emitters.Num(),
				Cdo->FxLight.Scope == EAsdaFxLightScope::PerEmitter ? TEXT("свой у каждого эмиттера") : TEXT("один на весь эффект"));
			if (Cdo->FxLight.Scope != EAsdaFxLightScope::PerEmitter)
			{
				C += TEXT("⚠ Пока у эффекта стоит «один на весь эффект», поэмиттерные числа НЕ ПРИМЕНЯЮТСЯ: источник один, а цвет, радиус и позиция считаются агрегатом по всем эмиттерам. Переключите у САМОГО эффекта поле «Свет: один на эффект или у каждого эмиттера» — именно оно и лечит «свет идёт из центра, а не по форме».\n");
			}
		}
		// ★★★ Ревизия Ф11 (К1): обещание «видна сразу» теперь ВЫПОЛНЯЕТСЯ и для размещённых эффектов —
		//   правка CDO раздаётся в экземпляры тем же законом, что у панели деталей
		C += TEXT("★ ЭТИ ЧИСЛА ЛЕЖАТ В АССЕТЕ, А НЕ В ТАБЛИЦЕ. Кнопка «Сохранить» пишет Tools/asda_fx_light.json и Config/DefaultGame.ini, а ключа эмиттера в той таблице нет вовсе. Правка видна сразу — в окне уровня и в PIE, в том числе у эффектов, УЖЕ РАЗМЕЩЁННЫХ на картах (кроме тех, кому это число задавали поштучно: их не трогаем). Ассет помечается изменённым сразу же, на диск попадёт вашим «Save All». «Отменить несохранённое» её НЕ отменяет — для отката есть «Вернуть эмиттер как в ассете» (★ ревизия Ф12: он возвращает и числа, и долговечную копию внутри ассета, поэтому откат полный).\n");
		// ★★★ Ревизия Ф13 (С3 и МЕЛК): ПРО Ctrl+Z — ФАКТ, А НЕ ОБЕЩАНИЕ, И ПРО ВПЕЧАТЫВАНИЕ ЧЕСТНО.
		//   Прежний текст говорил «у размещённых останется», но не называл это тем, чем оно было:
		//   расхождением экземпляра с архетипом, которое сохранение карты впечатает в .umap.
		//   Теперь расхождения не остаётся — откат раздаётся, — и текст говорит именно это.
		C += FString::Printf(TEXT("★ Ctrl+Z: последняя правка эмиттера %s (правок мимо буфера отмены за сеанс: %d). После отмены откат сам раздаётся размещённым на картах экземплярам — у них не остаётся числа, отличного от ассета, и сохранение карты НЕ впишет его в .umap отдельным переопределением. Пересинхронизировано за сеанс: %d актор(ов).\n"),
			FAsdaFxGlowConfigModel::LastEmitterEditUndoable() ? TEXT("попала в буфер отмены") : TEXT("★ В БУФЕР НЕ ПОПАЛА — Ctrl+Z её не вернёт"),
			FAsdaFxGlowConfigModel::NumNonUndoableEmitterEdits(), FAsdaFxGlowConfigModel::NumUndoResynced());
		// ★ Ф13 (К2): ассет правили не только мы — значит, «Вернуть как в ассете» признак «изменён»
		//   не снимет, и обещать обратное нельзя
		if (Model->IsFxAssetForeignDirty(R.ClassPath))
		{
			C += TEXT("⚠ Этот ассет помечен изменённым не только правками конфигуратора (его правили и в панели деталей, либо он был изменён ещё до открытия вкладки). «Вернуть эмиттер как в ассете» вернёт ЧИСЛА ЭМИТТЕРА, но признак «изменён» снимать не станет: иначе ваши остальные правки в этом ассете пропали бы из «Save All» и из вопроса при выходе.\n");
		}
		C += TEXT("★ Форму видно в окне уровня: выделите актор эффекта и включите Show → Advanced → Light Radius — движок рисует сферу затухания, коробку прямоугольника и конус сам. Набор рисовальщиков обновляется только при смене выделения.");
		CardText = C;
	}
	else if (T.bFamily)
	{
		const FAsdaFxLightRule* R = UAsdaFxLightSettings::Get()->Families.FindByPredicate([&T](const FAsdaFxLightRule& X) { return X.Id == T.FamilyId; });
		C += FString::Printf(TEXT("СЕМЕЙСТВО «%s», шаблон имени .jeb: %s\n"), *T.FamilyId, R ? *R->Jeb : TEXT("?"));
		C += Model->DescribeFamily(T.FamilyId) + TEXT("\n");
		C += TEXT("Действует на эффекты семейства, у которых нет переопределения конфигуратора и не взведён замок ассета «Переопределено вручную». ");
		C += TEXT("«Сила свечения» семейства не задана — рисуется материал по таблице asda_fx_boost.json; задать её здесь = одна яркость на всё семейство.\n");
	}
	else
	{
		C += FString::Printf(TEXT("ЭФФЕКТ %s   семейство %s   предметов с этим эффектом: %d\n"),
			*T.Jeb, *Model->FamilyOfJeb(T.Jeb), Model->NumItemsWithJeb(T.Jeb));
		C += FString::Printf(TEXT("Правка ляжет на: %s\n"), *T.Label);
		const FString ClassPath = Selected.IsValid() ? Selected->FxClassPath : FString();
		if (ClassPath.IsEmpty())
		{
			C += TEXT("Ассета BP_Fx_… нет — в игре этот эффект не спавнится, настройка сохранится впрок.\n");
		}
		else
		{
			C += FString::Printf(TEXT("Ассет: %s\n"), *ClassPath.LeftChop(2));
			if (UClass* Cls = LoadClass<AActor>(nullptr, *ClassPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				if (const AAsdaEffectActor* Cdo = Cast<AAsdaEffectActor>(Cls->GetDefaultObject()))
				{
					if (Cdo->FxLight.bManual) { C += TEXT("⚠ У ассета взведён замок «Переопределено вручную»: семейство на него не действует, переопределение конфигуратора — действует (оно выше замка).\n"); }
					if (Cdo->FxLight.bOverrideEmissive) { C += FString::Printf(TEXT("⚠ У ассета задана своя сила свечения ×%.2f — она выше семейства, но ниже переопределения конфигуратора.\n"), Cdo->FxLight.EmissiveBoost); }
				}
			}
		}
		C += Model->DescribeBoost(T.Jeb) + TEXT("\n");
	}
	if (!bEmitter)
	{
		C += TEXT("Порядок данных: эмиттер (ассет) → ПРЕДМЕТ → ЭФФЕКТ → замок ассета → СЕМЕЙСТВО → поля ассета. Откат уровня конфигуратора: консоль asda.Fx.Glow.Overrides 0.\n");
		C += TEXT("★ ФОРМУ И ДАЛЬНОСТЬ ВИДНО В ОКНЕ УРОВНЯ ДАРОМ: выделите актор эффекта на карте и включите Show → Advanced → Light Radius — движок сам рисует сферу затухания, коробку прямоугольника со «шторками» и конус прожектора (рисовальщик ищется по родительским классам, а наш источник унаследован от точечного света движка). Набор рисовальщиков пересчитывается только при СМЕНЕ ВЫДЕЛЕНИЯ: поменяли форму — снимите и верните выделение.");
		CardText = C;
	}

	// ── заголовок таблицы полей ──
	EnumOptionStore.Reset();
	FieldsBox->AddSlot().AutoHeight().Padding(2.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth() [ SNew(SBox).WidthOverride(300.f) [ SNew(STextBlock).Font(FAppStyle::GetFontStyle("BoldFont")).Text(LOCTEXT("ColName", "Параметр (● — задан здесь, * — не записано)")).ToolTipText(LOCTEXT("ColNameTip", "● — значение задано этой записью (или этим эмиттером), а не унаследовано. * — правка сделана в этом сеансе и ещё не записана: у записи таблицы — не нажато «Сохранить», у эмиттера — ассет ещё не сохранён вашим «Save All».")) ] ]
		+ SHorizontalBox::Slot().AutoWidth() [ SNew(SBox).WidthOverride(240.f) [ SNew(STextBlock).Font(FAppStyle::GetFontStyle("BoldFont")).Text(LOCTEXT("ColValue", "Значение")) ] ]
		+ SHorizontalBox::Slot().FillWidth(1.f) [ SNew(STextBlock).Font(FAppStyle::GetFontStyle("BoldFont")).Text(LOCTEXT("ColDefault", "По умолчанию (наследуется)")) ]
	];

	// ── ★★★ ВОЛНА Р0 (пп.1 и 2): СТРОКИ ПО РАЗДЕЛАМ, БЕЗ НЕРАБОТАЮЩИХ ПОЛЕЙ ──────────────────────
	//   Порядок внутри раздела — порядок таблицы полей модели: сначала то, что владелец крутит чаще
	//   всего, затем редкое. Раздел без единой строки в панель не добавляется вовсе — пустых
	//   заголовков «Тень» у эффекта без тени быть не должно.
	const FAsdaFxLightSpec Work = WorkSpec();
	const TArray<FString>& Sections = FAsdaFxGlowConfigModel::GetSections();
	TArray<TSharedPtr<SVerticalBox>> Rows;
	Rows.SetNum(Sections.Num());
	for (const FAsdaGlowField& F : Model->GetFields())
	{
		if (!F.Prop || (F.bAdvanced && !bShowAdvanced)) { continue; }
		bool bHides = false;
		const bool bActive = FAsdaFxGlowConfigModel::IsFieldActive(F, Work, bHides);
		// Прячем только то, что при невыполненном условии прячет и панель деталей (EditConditionHides).
		// Поля с условием БЕЗ «Hides» (сила свечения под галочкой) остаются видимыми и правимыми:
		// правка такого числа сама взводит свою галочку (FAsdaFxGlowConfigModel::SetField)
		if (!bActive && bHides && !bShowInactive) { continue; }
		const int32 Sec = FMath::Clamp(F.Section, 0, Sections.Num() - 1);
		if (!Rows[Sec].IsValid()) { Rows[Sec] = SNew(SVerticalBox); }
		Rows[Sec]->AddSlot().AutoHeight().Padding(2.f, 1.f) [ MakeFieldRow(F) ];
	}
	for (int32 i = 0; i < Sections.Num(); ++i)
	{
		if (!Rows[i].IsValid()) { continue; }
		FieldsBox->AddSlot().AutoHeight().Padding(2.f, 2.f) [ MakeSection(i, Rows[i].ToSharedRef()) ];
	}
}

// ★★★ ВОЛНА Р0 (п.2): РАЗДЕЛ ПАНЕЛИ ПОЛЕЙ. Состояние раскрытия помнится МЕЖДУ СЕАНСАМИ — в
//   EditorPerProjectUserSettings.ini (личная настройка владельца, в репозиторий не попадает и правки
//   проекта не трогает). Без этого каждый запуск редактора начинался бы с шести свёрнутых разделов.
TSharedRef<SWidget> SAsdaFxGlowConfigurator::MakeSection(int32 SectionIndex, const TSharedRef<SVerticalBox>& Rows)
{
	static const TCHAR* IniSection = TEXT("AsdaFxGlowConfigurator");
	const FString Key = FString::Printf(TEXT("SectionExpanded_%d"), SectionIndex);
	// Умолчание: «Свет», «Свечение эффекта» и «Форма и размещение» раскрыты, остальное свёрнуто
	bool bExpanded = SectionIndex <= 2;
	GConfig->GetBool(IniSection, *Key, bExpanded, GEditorPerProjectIni);
	return SNew(SExpandableArea)
		.InitiallyCollapsed(!bExpanded)
		.AreaTitle(FText::FromString(FAsdaFxGlowConfigModel::GetSections()[SectionIndex]))
		.AreaTitleFont(FAppStyle::GetFontStyle("BoldFont"))
		.Padding(FMargin(8.f, 2.f, 2.f, 6.f))
		.OnAreaExpansionChanged_Lambda([Key](bool bNowExpanded)
		{
			GConfig->SetBool(IniSection, *Key, bNowExpanded, GEditorPerProjectIni);
			GConfig->Flush(false, GEditorPerProjectIni);
		})
		.BodyContent()
		[
			Rows
		];
}

TSharedRef<SWidget> SAsdaFxGlowConfigurator::MakeFieldRow(const FAsdaGlowField& F)
{
	return SNew(SHorizontalBox)
		// ★ Волна Р0 (п.5): подсказка — с поправкой конфигуратора, а не голый ToolTip свойства
		.ToolTipText(FText::FromString(F.Tip))
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(300.f)
			[
				SNew(STextBlock)
				.Text_Lambda([this, F]()
				{
					const bool bOwn = FieldOwn(F);
					const bool bDirty = FieldPending(F);
					// ★ Волна Р0 (п.1): поле с невыполненным условием БЕЗ EditConditionHides остаётся
					//   видимым (иначе до него не добраться), но честно говорит, что сейчас не работает
					bool bHides = false;
					const bool bActive = FAsdaFxGlowConfigModel::IsFieldActive(F, WorkSpec(), bHides);
					return FText::FromString(FString::Printf(TEXT("%s%s%s%s%s"), bOwn ? TEXT("● ") : TEXT("   "),
						*F.Label, bDirty ? TEXT("  *") : TEXT(""), bActive ? TEXT("") : TEXT("   (сейчас не действует)"),
						// ★ Ревизия Ф11 (С4): поле, которое видно, но записывается не отсюда, говорит об этом само
						F.bReadOnly ? TEXT("   (только чтение)") : TEXT("")));
				})
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			// ★ Ревизия Ф11 (С4): у поля «только чтение» виджет значения ПОГАШЕН — он показывает число,
			//   но нажать на него нельзя. Врать «правится» нельзя, прятать поле — тоже (SPEC_FIELDS его содержит)
			SNew(SBox).WidthOverride(240.f).IsEnabled(!F.bReadOnly) [ MakeValueWidget(F) ]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(8.f, 0.f)
		[
			SNew(STextBlock)
			.Text_Lambda([this, F]() { return FText::FromString(FieldDefaultText(F)); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("ResetField", "Сброс"))
			// ★ Ревизия Ф11 (К2): у эмиттера «Сброс» возвращает то, что ЛЕЖАЛО В АССЕТЕ, а не число
			//   эффекта: иначе собственное число эмиттера исчезало навсегда
			.ToolTipText(LOCTEXT("ResetFieldTip", "Вернуть исходное значение: у записи таблицы — снять её ключ (число станет наследуемым), у ЭМИТТЕРА — вернуть то, что лежало в ассете до правок этого сеанса (а если своих чисел в ассете не было — число самого эффекта)."))
			.IsEnabled_Lambda([this, F]() { return CanResetField(F); })
			.OnClicked_Lambda([this, F]() { ResetFieldCur(F); return FReply::Handled(); })
		];
}

TSharedRef<SWidget> SAsdaFxGlowConfigurator::MakeValueWidget(const FAsdaGlowField& F)
{
	// ★ Волна Р1 (п.7): виджет значения больше не знает, где лежат числа и куда ляжет правка:
	//   запись таблицы или эмиттер ассета — оба хранилища спрятаны за WorkSpec / CommitField
	auto Commit = [this, F](const FAsdaFxLightSpec& Tmp) { CommitField(F, Tmp); };
	auto MetaF = [&F](const TCHAR* Key) { return F.Prop->HasMetaData(Key) ? TOptional<float>(FCString::Atof(*F.Prop->GetMetaData(Key))) : TOptional<float>(); };

	if (FBoolProperty* B = CastField<FBoolProperty>(F.Prop))
	{
		return SNew(SCheckBox)
			.IsChecked_Lambda([this, B]() { const FAsdaFxLightSpec Cur = WorkSpec(); return B->GetPropertyValue_InContainer(&Cur) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, B, Commit](ECheckBoxState S)
			{
				FAsdaFxLightSpec Tmp = WorkSpec();
				B->SetPropertyValue_InContainer(&Tmp, S == ECheckBoxState::Checked);
				Commit(Tmp);
			});
	}
	if (FFloatProperty* Fl = CastField<FFloatProperty>(F.Prop))
	{
		return SNew(SNumericEntryBox<float>)
			.AllowSpin(true)
			.MinValue(MetaF(TEXT("ClampMin"))).MaxValue(MetaF(TEXT("ClampMax")))
			.MinSliderValue(MetaF(TEXT("UIMin"))).MaxSliderValue(MetaF(TEXT("UIMax")))
			.Value_Lambda([this, Fl]() { const FAsdaFxLightSpec Cur = WorkSpec(); return TOptional<float>(Fl->GetPropertyValue_InContainer(&Cur)); })
			.OnValueCommitted_Lambda([this, Fl, Commit](float V, ETextCommit::Type)
			{
				FAsdaFxLightSpec Tmp = WorkSpec();
				Fl->SetPropertyValue_InContainer(&Tmp, V);
				Commit(Tmp);
			});
	}
	if (FIntProperty* I = CastField<FIntProperty>(F.Prop))
	{
		TOptional<int32> Mn, Mx;
		if (F.Prop->HasMetaData(TEXT("ClampMin"))) { Mn = FCString::Atoi(*F.Prop->GetMetaData(TEXT("ClampMin"))); }
		if (F.Prop->HasMetaData(TEXT("ClampMax"))) { Mx = FCString::Atoi(*F.Prop->GetMetaData(TEXT("ClampMax"))); }
		return SNew(SNumericEntryBox<int32>)
			.AllowSpin(true).MinValue(Mn).MaxValue(Mx)
			.Value_Lambda([this, I]() { const FAsdaFxLightSpec Cur = WorkSpec(); return TOptional<int32>(I->GetPropertyValue_InContainer(&Cur)); })
			.OnValueCommitted_Lambda([this, I, Commit](int32 V, ETextCommit::Type)
			{
				FAsdaFxLightSpec Tmp = WorkSpec();
				I->SetPropertyValue_InContainer(&Tmp, V);
				Commit(Tmp);
			});
	}
	if (FEnumProperty* E = CastField<FEnumProperty>(F.Prop))
	{
		TSharedPtr<TArray<TSharedPtr<int64>>> Opts = MakeShared<TArray<TSharedPtr<int64>>>();
		const UEnum* En = E->GetEnum();
		for (int32 k = 0; k < En->NumEnums() - 1; ++k)   // последний — служебный _MAX
		{
			Opts->Add(MakeShared<int64>(En->GetValueByIndex(k)));
		}
		EnumOptionStore.Add(Opts);
		return SNew(SComboBox<TSharedPtr<int64>>)
			.OptionsSource(Opts.Get())
			.OnGenerateWidget_Lambda([En](TSharedPtr<int64> V) { return SNew(STextBlock).Text(En->GetDisplayNameTextByValue(*V)); })
			.OnSelectionChanged_Lambda([this, E, Commit](TSharedPtr<int64> V, ESelectInfo::Type Info)
			{
				if (!V.IsValid() || Info == ESelectInfo::Direct) { return; }
				FAsdaFxLightSpec Tmp = WorkSpec();
				E->GetUnderlyingProperty()->SetIntPropertyValue(E->ContainerPtrToValuePtr<void>(&Tmp), *V);
				Commit(Tmp);
			})
			[
				SNew(STextBlock).Text_Lambda([this, E, En]()
				{
					const FAsdaFxLightSpec Cur = WorkSpec();
					const int64 V = E->GetUnderlyingProperty()->GetSignedIntPropertyValue(E->ContainerPtrToValuePtr<void>(&Cur));
					return En->GetDisplayNameTextByValue(V);
				})
			];
	}
	if (FStructProperty* SP = CastField<FStructProperty>(F.Prop))
	{
		const bool bColor = SP->Struct == TBaseStructure<FColor>::Get();
		// ★★★ РЕВИЗИЯ Ф11 (К1, 20.09.2026): ПОВОРОТ — СВОЙ РЕДАКТОР, А НЕ «ТРИ ЧИСЛА БЕЗ ИМЁН».
		//   Было: любая структура, кроме FColor, разбиралась как FVector — то есть на FRotator-свойстве
		//   бралось `ContainerPtrToValuePtr<FVector>()`. Совпадение раскладки (три double) спасало от
		//   падения, но владелец видел три безымянных поля и не мог знать, где тангаж, а где крен.
		//   Теперь у каждого угла подпись и своя шкала: тангаж/рыскание/крен — общепринятые русские
		//   имена Pitch/Yaw/Roll, порядок тот же, что в панели деталей UE и в таблице генератора.
		if (SP->Struct == TBaseStructure<FRotator>::Get())
		{
			static const TCHAR* AxisName[3] = { TEXT("тангаж"), TEXT("рыскание"), TEXT("крен") };
			static const TCHAR* AxisTip[3] = {
				TEXT("Тангаж (Pitch): наклон формы «носом» вверх-вниз, градусов. У блина 90° кладёт круг плашмя."),
				TEXT("Рыскание (Yaw): разворот формы вокруг вертикали, градусов."),
				TEXT("Крен (Roll): вращение формы вокруг её собственной продольной оси, градусов.") };
			TSharedRef<SHorizontalBox> RotBox = SNew(SHorizontalBox);
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				RotBox->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(Axis ? 6.f : 0.f, 0.f, 2.f, 0.f)
				[
					SNew(STextBlock).Text(FText::FromString(AxisName[Axis])).ToolTipText(FText::FromString(AxisTip[Axis]))
				];
				RotBox->AddSlot().FillWidth(1.f)
				[
					SNew(SNumericEntryBox<float>).AllowSpin(true)
					.MinSliderValue(-180.f).MaxSliderValue(180.f)
					.ToolTipText(FText::FromString(AxisTip[Axis]))
					.Value_Lambda([this, SP, Axis]()
					{
						const FAsdaFxLightSpec Cur = WorkSpec();
						const FRotator* R = SP->ContainerPtrToValuePtr<FRotator>(&Cur);
						return TOptional<float>((float)(Axis == 0 ? R->Pitch : Axis == 1 ? R->Yaw : R->Roll));
					})
					.OnValueCommitted_Lambda([this, SP, Axis, Commit](float V, ETextCommit::Type)
					{
						FAsdaFxLightSpec Tmp = WorkSpec();
						FRotator* R = SP->ContainerPtrToValuePtr<FRotator>(&Tmp);
						if (Axis == 0) { R->Pitch = V; } else if (Axis == 1) { R->Yaw = V; } else { R->Roll = V; }
						Commit(Tmp);
					})
				];
			}
			return RotBox;
		}
		// ★ Ревизия Ф11 (К1): ниже разбирается ТОЛЬКО то, что мы действительно умеем. Незнакомая
		//   структура больше не притворяется вектором — иначе следующее новое поле снова молча
		//   получило бы чужой редактор (закон false-negatives-method).
		if (!bColor && SP->Struct != TBaseStructure<FVector>::Get())
		{
			return SNew(STextBlock).Text(LOCTEXT("UnsupportedStruct", "(структура не поддержана конфигуратором)"));
		}
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
		if (bColor)
		{
			Box->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
			[
				SNew(SBox).WidthOverride(28.f).HeightOverride(16.f)
				[
					SNew(SColorBlock).Color_Lambda([this, SP]() { const FAsdaFxLightSpec Cur = WorkSpec(); return FLinearColor::FromSRGBColor(*SP->ContainerPtrToValuePtr<FColor>(&Cur)); })
				]
			];
		}
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (bColor)
			{
				Box->AddSlot().FillWidth(1.f)
				[
					SNew(SNumericEntryBox<int32>).AllowSpin(true).MinValue(0).MaxValue(255).MinSliderValue(0).MaxSliderValue(255)
					.Value_Lambda([this, SP, Axis]() { const FAsdaFxLightSpec Cur = WorkSpec(); const FColor* Cc = SP->ContainerPtrToValuePtr<FColor>(&Cur); return TOptional<int32>(Axis == 0 ? Cc->R : Axis == 1 ? Cc->G : Cc->B); })
					.OnValueCommitted_Lambda([this, SP, Axis, Commit](int32 V, ETextCommit::Type)
					{
						FAsdaFxLightSpec Tmp = WorkSpec();
						FColor* Cc = SP->ContainerPtrToValuePtr<FColor>(&Tmp);
						const uint8 U = (uint8)FMath::Clamp(V, 0, 255);
						if (Axis == 0) { Cc->R = U; } else if (Axis == 1) { Cc->G = U; } else { Cc->B = U; }
						Cc->A = 255;
						Commit(Tmp);
					})
				];
			}
			else
			{
				Box->AddSlot().FillWidth(1.f)
				[
					SNew(SNumericEntryBox<float>).AllowSpin(true)
					.Value_Lambda([this, SP, Axis]() { const FAsdaFxLightSpec Cur = WorkSpec(); return TOptional<float>((float)(*SP->ContainerPtrToValuePtr<FVector>(&Cur))[Axis]); })
					.OnValueCommitted_Lambda([this, SP, Axis, Commit](float V, ETextCommit::Type)
					{
						FAsdaFxLightSpec Tmp = WorkSpec();
						(*SP->ContainerPtrToValuePtr<FVector>(&Tmp))[Axis] = V;
						Commit(Tmp);
					})
				];
			}
		}
		return Box;
	}
	return SNew(STextBlock).Text(LOCTEXT("Unsupported", "(тип поля не поддержан)"));
}

FText SAsdaFxGlowConfigurator::GetCardText() const
{
	FString S = CardText;
	const int32 Dirty = Model.IsValid() ? Model->NumDirty() : 0;
	if (Dirty > 0) { S += FString::Printf(TEXT("\nНЕСОХРАНЁННЫХ записей: %d — видны на сцене, в игру попадут после «Сохранить»."), Dirty); }
	// ★ Волна Р1 (п.7): поэмиттерные правки считаются ОТДЕЛЬНО — их путь на диск другой
	const int32 Edited = Model.IsValid() ? Model->NumFxAssetsEdited() : 0;
	const int32 Unsaved = Model.IsValid() ? Model->NumFxAssetsUnsaved() : 0;
	if (Edited > 0)
	{
		S += FString::Printf(TEXT("\nАССЕТОВ с правками эмиттеров: %d, из них ещё НЕ ЗАПИСАНО на диск: %d — «Сохранить» их не пишет, они уже помечены изменёнными и ждут вашего «Save All»."), Edited, Unsaved);
	}
	if (!Status.IsEmpty()) { S += TEXT("\n") + Status; }
	return FText::FromString(S);
}

// ── превью ──────────────────────────────────────────────────────────────────
void SAsdaFxGlowConfigurator::RefreshPreview()
{
	if (!Viewport.IsValid()) { return; }
	if (!Selected.IsValid() || Selected->Kind == EAsdaGlowNodeKind::Group)
	{
		Viewport->ClearPreview();
		return;
	}
	FAsdaPreviewRequest Req;
	const TSharedPtr<FAsdaItemBrowserData> Items = Model->GetItems();
	int32 ItemIdx = Selected->ItemIndex;
	if (Selected->Kind == EAsdaGlowNodeKind::Item && Selected->Children.Num() > 0) { ItemIdx = Selected->Children.Last()->ItemIndex; }
	if (Items.IsValid() && ItemIdx != INDEX_NONE)
	{
		// Та же сборка запроса, что у браузера предметов: модель + эталонная текстура + сдвиг кости оружия
		const int32 Enh = Selected->Kind == EAsdaGlowNodeKind::ItemState ? Selected->Enhance : (Selected->Kind == EAsdaGlowNodeKind::Item ? 7 : 0);
		const FAsdaBrowserItemAssets A = Items->ResolveAssets(ItemIdx, true, Enh);
		if (A.MeshKind == EAsdaBrowserMeshKind::EquipSkeletal) { Req.SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, *A.MeshObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet); }
		else if (A.MeshKind == EAsdaBrowserMeshKind::CharStatic || A.MeshKind == EAsdaBrowserMeshKind::ItemStatic) { Req.StaticMesh = LoadObject<UStaticMesh>(nullptr, *A.MeshObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet); }
		if (!A.TexObjectPath.IsEmpty()) { Req.Texture = LoadObject<UTexture2D>(nullptr, *A.TexObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet); }
		Req.bWeaponBoneOffset = (A.ItemType == 9 && A.MeshKind == EAsdaBrowserMeshKind::CharStatic);
		Req.bAllowAutoOrient = (A.ItemType == 9 || A.ItemType == 8);
		if (!A.GlowBpObjectPath.IsEmpty()) { Req.FxClass = LoadClass<AActor>(nullptr, *A.GlowBpObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet); }
	}
	if (!Req.FxClass && !Selected->FxClassPath.IsEmpty())
	{
		Req.FxClass = LoadClass<AActor>(nullptr, *Selected->FxClassPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	if (!Req.FxClass && Selected->Kind == EAsdaGlowNodeKind::Family)
	{
		// Пример эффекта семейства — общий помощник модели (им же берётся текущая сила свечения семейства)
		FString ExampleJeb;
		const FString P = Model->FindFamilyExample(Selected->FamilyId, ExampleJeb);
		if (!P.IsEmpty())
		{
			Req.FxClass = LoadClass<AActor>(nullptr, *P, nullptr, LOAD_NoWarn | LOAD_Quiet);
			Status = FString::Printf(TEXT("Превью семейства: пример %s"), *ExampleJeb);
		}
	}
	Viewport->SetPreview(Req);
	RefreshPreviewGlow();
	Viewport->FocusOnPreview();
}

void SAsdaFxGlowConfigurator::RefreshPreviewGlow()
{
	if (!Viewport.IsValid()) { return; }
	if (AAsdaEffectActor* E = Cast<AAsdaEffectActor>(Viewport->GetFxActor()))
	{
		// В игре эффект всегда знает свой предмет — превью обязано показывать ровно то же
		E->SetSourceItemId(Selected.IsValid() ? Selected->ItemCode : 0);
		E->RefreshGlowMaterials();
	}
}

// ── действия ────────────────────────────────────────────────────────────────
FReply SAsdaFxGlowConfigurator::OnSave()
{
	FString Msg;
	Model->Save(Msg);
	Status = Msg;
	RebuildFields();
	RefreshPreviewGlow();
	return FReply::Handled();
}

FReply SAsdaFxGlowConfigurator::OnRevert()
{
	Model->Revert();
	Status = TEXT("Несохранённые правки отменены: таблицы перечитаны из Config/DefaultGame.ini.");
	RebuildFields();
	RefreshPreviewGlow();
	return FReply::Handled();
}

FReply SAsdaFxGlowConfigurator::OnRemoveOverride()
{
	// ★ Ревизия Ф11 (К2): у ЭМИТТЕРА снимать из таблицы нечего (его чисел там нет), зато есть куда
	//   вернуть: снимок «как в ассете», снятый перед первой правкой этого сеанса
	if (IsEmitterMode())
	{
		const FAsdaGlowEmitterRef R = CurrentEmitter();
		if (Model->HasEmitterBaseline(R))
		{
			Model->RevertEmitterToAsset(R);
			WarnIfEmitterEditNotUndoable();
			Status = FString::Printf(TEXT("Эмиттер %d возвращён к тому, что лежало в ассете."), R.Index);
			RebuildFields();
			RefreshPreviewGlow();
			if (TreeView.IsValid()) { TreeView->RequestTreeRefresh(); }
		}
		return FReply::Handled();
	}
	const FAsdaGlowTarget T = CurrentTarget();
	if (T.IsValid() && !T.bFamily)
	{
		Model->RemoveOverride(T);
		Status = TEXT("Переопределение снято (не сохранено).");
		RebuildFields();
		RefreshPreviewGlow();
	}
	return FReply::Handled();
}

FReply SAsdaFxGlowConfigurator::OnStandInPie()
{
	if (GEditor && GEditor->PlayWorld && GEngine && Selected.IsValid() && !Selected->Jeb.IsEmpty())
	{
		const FString Cmd = FString::Printf(TEXT("asda.Fx.Light.Stand %s 700 150 item=%d"), *Selected->Jeb, Selected->ItemCode);
		GEngine->Exec(GEditor->PlayWorld, *Cmd);
		Status = FString::Printf(TEXT("PIE: %s"), *Cmd);
	}
	return FReply::Handled();
}

// ★★★ РЕВИЗИЯ Ф11 (К3): ПОМЕТИТЬ ИЗМЕНЁННЫМИ ВСЕ ТРОНУТЫЕ АССЕТЫ.
//   БЫЛО: кнопка помечала ОДИН выбранный ассет, а число на ней считало ВСЕ тронутые. Владелец правил
//   пять эффектов, видел «(5)», жал один раз и делал Save All — уходил один, остальные четыре
//   пропадали при закрытии редактора БЕЗ ВОПРОСА: их пакеты изменёнными не были.
//   СТАЛО: пометку ставит сама правка (Modify в FAsdaFxGlowConfigModel::EmitterEditBegin — штатный
//   путь панели деталей), а кнопка проходит по всем тронутым и чинит тех, у кого признак снят.
//   Решение ЗАПИСАТЬ ассет по-прежнему только владельца: на диск мы не пишем ничего.
FReply SAsdaFxGlowConfigurator::OnMarkFxAssetDirty()
{
	FString Msg;
	Model->MarkAllTouchedFxDirty(Msg);
	Status = Msg;
	if (TreeView.IsValid()) { TreeView->RequestTreeRefresh(); }
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
