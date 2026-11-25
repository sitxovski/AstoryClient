// Copyright (c) 2026 @sitxovski (Discord)
// AsdaQuestLogSubsystem_View.cpp — ★ R3-c 12.11.2026 (разрез AsdaQuestLogSubsystem.cpp, дословный перенос): обновление вида: иконки ячеек, заголовок и текст, награды, маршруты и переход по маршруту.
//   Общее для частей — в AsdaQuestLogSubsystem_Private.h; шапка и реверс — в AsdaQuestLogSubsystem.cpp.
#include "Quest/AsdaQuestLogSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI

#include "Quest/AsdaQuestMarkerSubsystem.h"
#include "Npc/AsdaNpcDialogTypes.h"          // FAsdaEpisodeRow, FAsdaQuestRewardTableRow
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaSystemMenuSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaTipPanel.h"   // ★ общая точка Resize9Slice (порт uiPanel9SliceLayout)
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaMinimapSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "Login/AsdaText.h"   // ★ 09.10.2026: FAsdaText::Get — название рейд-задания из текстовой таблицы
#include "Items/AsdaItemTableSubsystem.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaNameplateComponent.h"
#include "World/AsdaWorldSubsystem.h"      // ★ имя своего персонажа — ключ сохранения списка отслеживаемых
#include "AsdaNetSubsystem.h"
#include "Npc/AsdaNpcDialogSubsystem.h"   // ★ 19.10.2026: награды завершения 5052 — `ApplyQuestCompletionRewards5052`
#include "AsdaDataTableCache.h"

#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Kismet/GameplayStatics.h"
#include "UI/AsdaCraftSubsystem.h"   // ★ волна В3 ремесла: единый предикат «окно ремесла открыто» (`IsAnyCraftWindowOpen`)
#include "World/AsdaFishingSubsystem.h"     // ★ 08.10.2026 (рыбалка В4): гейт «идёт рыбалка» кнопок окна заданий (событие 66561)
#include "Text/AsdaLangRows.h"               // [ПОРТ] 09.10.2026 (мультиязычность): тексты книги задания / эпизода / рейда на языке сессии
#include "Quest/AsdaBoardQuestSubsystem.h"   // ★ 28.10.2026: задания доски объявлений 20000…30000 — тип, награды, готовность, имя
#include "UI/AsdaNotice.h"                   // ★ 31.10.2026: нотис провала отказа от задания (5007) — канал эталонного 6956
#include "Quest/AsdaQuestGuideTarget.h"      // ★ 31.10.2026 (баг 6): место цели строки гида без координат — общая точка с картой мира
#include "World/AsdaCoord.h"                 // ★ 31.10.2026 (баг 6): клетка персонажа (ближайшее место цели)
#include "UI/AsdaTutorialEvents.h"   // ★ 10.11.2026 обучение: флаги шагов гида 1950 (TUTORIAL.md §2.3)
#include "AsdaQuestLogSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaQuestLogSubsystem*.cpp

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ОТРИСОВКА
// ════════════════════════════════════════════════════════════════════════════════════════════════
void UAsdaQuestLogSubsystem::RefreshAll()
{
	if (!bReady) { return; }
	// Порядок как в эталонном `uiQuestGuide_Open`: иконки → блок наград → строки маршрута.
	RefreshSlotIcons();
	RefreshTitleAndText();
	RefreshRewards();
	RefreshRoutes();

	// FIX-ORIG D6: чекбокс 743 один на все 12 ячеек, а эталон выставлял его В ЦИКЛЕ по слотам —
	//   галка отражала ПОСЛЕДНИЙ обработанный слот, а не выбранный. Выводим из выбранного.
	if (TrackBox)
	{
		const bool bBusy = IsSlotBusy(SlotAt(ActiveSlot));
		TrackBox->SetVisibility(bBusy ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (TrackLabel) { TrackLabel->SetVisibility(bBusy ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
		const bool bOn = bBusy && IsSlotTracked(ActiveSlot);
		TrackBox->SetIsChecked(bOn);
		// ★★★ Е1: галка НЕДОСТУПНА, если у задания нечего отслеживать. Уже отслеживаемое задание
		//   остаётся доступным всегда — иначе его нельзя было бы снять.
		ApplyTrackBoxEnabled(bOn || (bBusy && CanTrackSlot(ActiveSlot)));
	}
}

// ★★★ Е1. ВИД НЕДОСТУПНОЙ ГАЛКИ — ОСОЗНАННОЕ ОТСТУПЛЕНИЕ, помечено здесь.
//   ЧТО В ЭТАЛОНЕ: запрет у чекбокса есть — латч `+609`; `checkBox_OnToggle@0x12FDF70`
//   первым делом проверяет `!*(this + 609)` и при взведённом флаге НЕ меняет кадр и НЕ шлёт
//   событий 67073/67074. То есть оригинал умеет «кнопка не реагирует», но ОТДЕЛЬНОГО ВИДА
//   у запрещённого состояния не имеет: в скине чекбокса всего четыре кадра (вкл/выкл × наведение),
//   пятого «серого» нет, и запрет игроку никак не показан.
//   ЧТО ДЕЛАЕМ МЫ: реакцию убираем ровно по эталону (`SetIsEnabled(false)` = тот же латч +609),
//   а поверх приглушаем галку и её подпись 744, чтобы игрок ВИДЕЛ, что отслеживать нечего —
//   именно этого просил владелец. Своей картинки не рисуем и текста не выдумываем
//   (законы `no-invented-ui`, `no-simplifications-stubs`): это та же кисть, только тусклее.
//   Откат к оригиналу — консоль `Asda.Quest.TrackerLockUseless 0` (галка снова будет
//   «ставиться и тут же гаснуть», как в игре 2008 года).
void UAsdaQuestLogSubsystem::ApplyTrackBoxEnabled(bool bEnabled)
{
	const bool bLock = UAsdaQuestMarkerSubsystem::TrackerLockUselessCheckbox();
	const bool bWant = bEnabled || !bLock;
	if (TrackBox)
	{
		TrackBox->SetIsEnabled(bWant);
		TrackBox->SetRenderOpacity(bWant ? 1.f : kDisabledOpacity);
	}
	if (TrackLabel) { TrackLabel->SetRenderOpacity(bWant ? 1.f : kDisabledOpacity); }
}

// Порт `CQuestLog_RefreshSlotIcons@0xC5D500`.
void UAsdaQuestLogSubsystem::RefreshSlotIcons()
{
	UAsdaQuestMarkerSubsystem* QM = Marks();
	UAsdaTextureSubsystem* Tex = AsdaTexMgr();   // ★ Аудит №3 (B8): общий короткий доступ к менеджеру текстур
	if (!QM || !Tex) { return; }

	bool bAnyIconUpdated = false;
	for (int32 i = 0; i < 12; ++i)
	{
		UImage* Img = SlotIcons.IsValidIndex(i) ? SlotIcons[i].Get() : nullptr;
		if (!Img) { continue; }
		const FAsdaQuestSlot* S = SlotAt(i);
		if (!IsSlotBusy(S))
		{
			// Эталон рисует пустую 32×32 «подложку» — у нас это прозрачная кисть поверх Slot_Base
			//   (общая точка `AsdaUi::ClearImageBrush`, дедуп Д8).
			AsdaUi::ClearImageBrush(Img);
			continue;
		}
		const FAsdaEpisodeRow* Ep = EpisodeRow(S->QuestId);
		if (!Ep || Ep->IconName.IsEmpty())
		{
			// ★ 28.10.2026 ДОСКА ОБЪЯВЛЕНИЙ (закрыт PENDING п. 77) — ветка `questCraftRec` эталона (quest.c:2116-2160): иконка
			//   `q_item_s_%02d` по ТИПУ задания (+3). Готово к сдаче (`craftCheckRequirementItemsMet`) — яркий вариант `_c`
			//   (`TexturePath_FormatCompressed`) и снятие с отслеживания; иначе по виду слота (+7): 0 — `_c`, 1 — `%02d_c` +
			//   `_c` = «_c_c» (такой текстуры нет — ★ [ИСПРАВЛЕНИЕ ОРИГИНАЛА]: рисуем `_c`), прочие (у доски 2) — обычный.
			UAsdaBoardQuestSubsystem* Board = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaBoardQuestSubsystem>() : nullptr;
			const FAsdaRaidQuestRow* BR = Board ? Board->FindQuest(S->QuestNo) : nullptr;
			if (!BR) { continue; }
			const FString Base = FString::Printf(TEXT("q_item_s_%02d"), Board->EffectiveQuestType(S->QuestNo) + 3);
			const bool bBoardReady = Board->IsQuestReady(S->QuestNo);
			// ★ 10.11.2026 [ОРИГИНАЛ] обучение: `g_bQuestIconUpdated` (0x2C715F8, байт 96 гида) — ветки «готово к сдаче» и вида слота 1.
			if (bBoardReady || S->ObjType == 1) { AsdaTutorial::Mark(GetGameInstance(), EAsdaTutorialFlag::QuestLogIcons); }
			if (bBoardReady)
			{
				if (QM->TrackerRemoveEntryDone(S->QuestNo)) { bAnyIconUpdated = true; SaveTrackedToSettings(); }   // ★ 31.10: выполнено — точка на NPC сдачи
				bAnyIconUpdated = true;
			}
			const FString IconName = (bBoardReady || S->ObjType == 0 || S->ObjType == 1) ? (Base + TEXT("_c")) : Base;
			if (UTexture2D* T = Tex->LoadUITexture(FName(*IconName)))
			{
				FSlateBrush B;
				B.SetResourceObject(T);
				B.ImageSize = FVector2D(32.f, 32.f);
				Img->SetBrush(B);
			}
			continue;
		}
		// «Задание выполнено» считает ЕДИНСТВЕННЫЙ вычислитель условий (таблица QCT).
		const bool bDone = QM->IsQuestObjectiveDone(S->QuestNo);
		if (bDone)
		{
			// ★ 10.11.2026 [ОРИГИНАЛ] обучение: `g_bQuestIconUpdated = 1` (0x2C715F8, байт 96 гида) — цель задания выполнена.
			AsdaTutorial::Mark(GetGameInstance(), EAsdaTutorialFlag::QuestLogIcons);
			// Эталон при переходе в «выполнено» снимает задание с отслеживания и гасит чекбокс
			//   (`QuestReward_UpdateProgressList` зовёт `questTracker_RemoveEntry`). Снимаем ЗАПИСЬ —
			//   она же и есть пометка «отслеживается», второго флага у нас нет.
			if (QM->TrackerRemoveEntryDone(S->QuestNo))   // ★ 31.10.2026: выполнено — точка карты мира переходит на NPC сдачи
			{
				bAnyIconUpdated = true;
				SaveTrackedToSettings();
			}
		}
		// `TexturePath_Format` → «<имя>.tex», `TexturePath_FormatCompressed` → «<имя>_c.tex»
		//   (render_core.c:5691/5703). То есть «_c» — ЯРКИЙ вариант выполненного задания.
		const FString IconName = bDone ? (Ep->IconName + TEXT("_c")) : Ep->IconName;
		if (UTexture2D* T = Tex->LoadUITexture(FName(*IconName)))
		{
			FSlateBrush B;
			B.SetResourceObject(T);
			B.ImageSize = FVector2D(32.f, 32.f);
			Img->SetBrush(B);
		}
	}

	// Рамка выделения 737 — на выбранную ячейку.
	if (SlotSel)
	{
		UImage* Cell = (ActiveSlot >= 0 && SlotIcons.IsValidIndex(ActiveSlot)) ? SlotIcons[ActiveSlot].Get() : nullptr;
		UCanvasPanelSlot* CS = Cell ? AsdaUi::SlotOf(Cell) : nullptr;
		UCanvasPanelSlot* SS = AsdaUi::SlotOf(SlotSel);
		if (CS && SS)
		{
			SS->SetPosition(CS->GetPosition());
			SlotSel->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			SlotSel->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// Эталон: если хоть одна иконка сменила состояние — пересчитать метки над NPC и на картах.
	//   В порту это ровно `RebuildMarks()` подсистемы меток (второго механизма нет).
	if (bAnyIconUpdated) { QM->RebuildMarks(); }
}

// Порт `CQuest_RefreshEpisodeObjectiveText@0xC5E710` (заголовок 703) и `questPanel_RefreshSlot@0xC5EE50`
//   (описание 714 + лоток целей 715…719) + `uiBuildItemSkillTooltipText@0xC5FB70` (список 734).
void UAsdaQuestLogSubsystem::RefreshTitleAndText()
{
	UAsdaQuestMarkerSubsystem* QM = Marks();
	const FAsdaQuestSlot* S = SlotAt(ActiveSlot);
	TrayCodes.Init(0, 5);

	if (!QM || !IsSlotBusy(S))
	{
		if (TitleText)   { TitleText->SetText(FText::GetEmpty()); }
		if (DisplayText) { DisplayText->SetText(FText::GetEmpty()); }
		if (DescText)    { DescText->SetText(FText::GetEmpty()); }
		for (int32 j = 0; j < 5; ++j) { if (TrayCounts.IsValidIndex(j) && TrayCounts[j]) { TrayCounts[j]->SetText(FText::GetEmpty()); } }
		if (TrayGrid) { TrayGrid->Refresh(); }
		return;
	}

	// FIX-ORIG D2: у эталона в ветке рейд-задания индекс слота был жёстко 0 (`44 * i`, где `i`
	//   только что обнулён) — в заголовке показывалось название задания из ЯЧЕЙКИ 0.
	//   Здесь всё берётся из ВЫБРАННОЙ ячейки, отдельной ветки с нулём нет.
	// ★ 28.10.2026 ДОСКА ОБЪЯВЛЕНИЙ: задания 20000…30000 с записью доски (эталон `mapObjFindEntryById(g_pQuestCraftWnd, …)`):
	//   заголовок 703 — вид 1 «имя (6163)», вид 2 «имя»; описание 714 (`uiBuildItemSkillTooltipText`) — «3586 | 3582 + тип»,
	//   пустая строка, описание `+132`; у вида 1 (`questPanel_RefreshSlot`) — заголовок вида + нотис 20080 с именем;
	//   список-свиток 734 — «[имя]» и описание; лоток целей — счётчики слота.
	if (S->QuestNo >= 20000 && S->QuestNo <= 30000)
	{
		UAsdaBoardQuestSubsystem* Board = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaBoardQuestSubsystem>() : nullptr;
		if (const FAsdaRaidQuestRow* BR = Board ? Board->FindQuest(S->QuestNo) : nullptr)
		{
			const FString Name = UAsdaBoardQuestSubsystem::QuestName(S->QuestNo);
			const FString Desc = UAsdaBoardQuestSubsystem::QuestDesc(S->QuestNo);
			if (TitleText)
			{
				TitleText->SetText(FText::FromString(S->ObjType == 1 ? FString::Printf(TEXT("%s (%s)"), *Name, *Loc(QLoc::kDone)) : Name));
			}
			const FString Head = Loc(BR->Mode == 1 ? 3586 : 3582 + Board->EffectiveQuestType(S->QuestNo));
			if (DisplayText)
			{
				FString Body = Desc;
				if (S->ObjType == 1)
				{
					if (UAsdaTextSubsystem* T = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr)
					{
						Body = T->FormatById(QLoc::kQuestDoneLine, { Name }, {});
					}
				}
				DisplayText->SetText(FText::FromString(FString::Printf(TEXT("%s\n\n%s\n"), *Head, *Body)));
			}
			if (DescText)
			{
				DescText->SetText(FText::FromString(FString::Printf(TEXT("[%s]\n\n\n%s"), *Name, *Desc)));
				if (DescScroll) { DescScroll->SetScrollOffset(0.f); }
			}
			for (int32 j = 0; j < 5; ++j)
			{
				const int32 Code  = S->ObjIds.IsValidIndex(j) ? S->ObjIds[j] : 0;
				const int32 Count = S->ObjCounts.IsValidIndex(j) ? S->ObjCounts[j] : 0;
				TrayCodes[j] = (Code > 0) ? Code : 0;
				if (TrayCounts.IsValidIndex(j) && TrayCounts[j])
				{
					TrayCounts[j]->SetText(Code > 0 ? FText::FromString(FString::Printf(TEXT("%d"), Count)) : FText::GetEmpty());
				}
			}
			if (TrayGrid) { TrayGrid->Refresh(); }
			return;
		}
	}
	const FAsdaEpisodeRow* Ep = EpisodeRow(S->QuestId);
	const FAsdaQuestBookRow* Book = Ep ? BookRow(Ep->QstFile) : nullptr;

	FString Title = Book ? QLogLang::BookTitle(Book) : QLogLang::EpTitle(Ep);   // [ПОРТ] 09.10.2026 (мультиязычность)
	// «(Готово)» — эталон приписывает нотис 6163, когда шаг ПОСЛЕДНИЙ (`stage == blockCount - 1`)
	//   и условие завершения (QCT) выполнено.
	int32 LastStep = -1;
	if (Book)
	{
		for (int32 j = 0; j < Book->Steps.Num(); ++j) { if (!Book->Steps[j].IsEmpty()) { LastStep = j; } }
	}
	const bool bDone = QM->IsQuestObjectiveDone(S->QuestNo);
	if (bDone && LastStep >= 0 && S->Stage == LastStep)
	{
		Title = FString::Printf(TEXT("%s (%s)"), *Title, *Loc(QLoc::kDone));
	}
	if (TitleText) { TitleText->SetText(FText::FromString(Title)); }

	// ── Описание 714: «нотис(3580 + вид)» + пустая строка + текст ТЕКУЩЕГО шага + перевод строки ──
	if (DisplayText)
	{
		const int32 Kind = Book ? Book->Kind : 0;
		FString Head = Loc(QLoc::kDescKindBase + Kind);
		FString Step;
		if (Book && Book->Steps.IsValidIndex(S->Stage)) { Step = QLogLang::BookStep(Book, S->Stage); }   // [ПОРТ] 09.10.2026 (мультиязычность)
		// Ветка «собрать предметы» (`objType == 1`) в эталоне вместо текста шага показывает
		//   нотис 20080 «Задание '[ss1]' выполнено» с подставленным названием.
		if (S->ObjType == 1)
		{
			if (UAsdaTextSubsystem* T = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr)
			{
				Step = T->FormatById(QLoc::kQuestDoneLine, { Title }, {});
			}
		}
		DisplayText->SetText(FText::FromString(FString::Printf(TEXT("%s\n\n%s\n"), *Head, *Step)));
	}

	// ── Список-свиток 734: полное описание задания из `.QST` ──
	if (DescText)
	{
		FString Desc = QLogLang::BookDesc(Book);   // [ПОРТ] 09.10.2026 (мультиязычность)
		Desc.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
		Desc.ReplaceInline(TEXT("\t"), TEXT(" "));
		DescText->SetText(FText::FromString(Desc));
		if (DescScroll) { DescScroll->SetScrollOffset(0.f); }
	}

	// ── Лоток целей 715…719: иконка предмета-цели + счётчик ──
	for (int32 j = 0; j < 5; ++j)
	{
		const int32 Code  = S->ObjIds.IsValidIndex(j) ? S->ObjIds[j] : 0;
		const int32 Count = S->ObjCounts.IsValidIndex(j) ? S->ObjCounts[j] : 0;
		TrayCodes[j] = (Code > 0) ? Code : 0;
		if (TrayCounts.IsValidIndex(j) && TrayCounts[j])
		{
			TrayCounts[j]->SetText(Code > 0 ? FText::FromString(FString::Printf(TEXT("%d"), Count)) : FText::GetEmpty());
		}
	}
	if (TrayGrid) { TrayGrid->Refresh(); }
}

// ★ ОДНА иконка награды. Виды берутся из `RewardTable.BIN` (48 б): ДЕНЬГИ (`+12`), ОПЫТ (`+16`),
//   ПРЕДМЕТ 1/2 (`+20`/`+28`) и ПРЕДМЕТ НА ВЫБОР (`+40`). Других видов в этой таблице НЕТ —
//   харизмы и «кода эпизода» (виды 2 и 3 попапа 540) здесь не бывает, у записи просто нет таких
//   полей. Деньги эталон рисует иконкой ПРЕДМЕТА 20551, опыт — плоской текстурой `exp2`
//   (`QuestReward_SetSlotDisplay@0xD80230`, те же две ветки, что и в окне 540).
void UAsdaQuestLogSubsystem::DrawRewardIcon(UImage* Img, int32 ItemCode, const TCHAR* FlatIcon)
{
	if (!Img) { return; }
	// Обе ветки — ОБЩИЕ точки порта, своих копий здесь нет (дедуп Д2, та же пара, что и в попапе 540):
	//   атласная иконка предмета — `UAsdaInventorySubsystem::ApplyIconToImage`,
	//   плоская текстура (`exp2`) — `AsdaUi::ApplyFlatJuiIcon`.
	bool bDrawn = false;
	UGameInstance* GI = GetGameInstance();
	if (ItemCode > 0)
	{
		if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
		{
			bDrawn = Inv->ApplyIconToImage(Img, ItemCode, 32.f);
		}
	}
	else
	{
		bDrawn = AsdaUi::ApplyFlatJuiIcon(Img, FlatIcon ? FString(FlatIcon) : FString(), 32.f);
	}
	if (!bDrawn) { AsdaUi::ClearImageBrush(Img); }   // дедуп Д8
	Img->SetVisibility(bDrawn ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ★★★ ОБЩАЯ ТОЧКА: НАБОР НАГРАД КВЕСТА — порт `questBuildRewardSet@0xC663A0` (`quest.c:4059`)
//
//  Ею пользуются ОБА потребителя эталона, и второй копии разбора `RewardTable.BIN` в порту нет:
//    · панель наград журнала (окно 700) — эталон `CQuest_RefreshRewardItemGrid@0xC677B0`;
//    · окно приёма задания `QUEST_Confirm_Win` 750 — эталон `CQuest_PopulateRewardWindow@0xC66A20`
//      (код тождественный, отличаются только адреса контролов).
//
//  ЧТО ДЕЛАЕТ ЭТАЛОН, построчно:
//    · перебирает ВСЕ записи таблицы, берёт совпавшие по паре (`+4 QuestId`, `+8 Stage`) —
//      `break` не делает, поэтому записей может быть несколько;
//    · ветка `+36 == 0` («обычная запись»): деньги `+12 >= 0` → ячейка 0 тип 0;
//      опыт `+16 > 0` → ячейка 1 тип 1; предмет 1 `+20 >= 0` → ячейка 2 тип 2;
//      предмет 2 `+28 >= 0` → ячейка 3 тип 2;
//    · ветка `+36 != 0` («на выбор»): те же четыре ячейки, но ПОРОГИ СТРОГИЕ (`> 0`), плюс
//      предмет `+40/+44` уходит в очередную ячейку 4…7 (счётчик `v7` начинается с 4);
//    · при промахе пары вызывающий повторяет поиск с `Stage + 1` — здесь это сделано внутри.
//
//  ⚠ ПОРОГ ДЕНЕГ. У эталона в «обычной» записи он НЕСТРОГИЙ (`>= 0`), то есть при `Money == 0`
//    (150 записей из 560) оригинал рисует плитку «0 Золото». По решению владельца 2026-08-23 мы
//    от этого ОСОЗНАННО отступаем и такую плитку не показываем — обоснование, числа и ключ отката
//    `asda.Quest.EtalonZeroMoney` в блоке CVar в шапке этого файла.
//
//  ⚠ СДВИГ ПЛИТОК (отступление 2026-08-23). Готовый ряд перед возвратом проходит через
//    `CollapseRewardCells`: ПУСТАЯ ячейка выкидывается, соседи сдвигаются влево (по умолчанию —
//    любая пустая, сквозь границу рядов). ⇒ ПОСЛЕ этой функции номер ячейки роли награды больше
//    не задаёт — вид только в поле `Kind`. Ключ отката `asda.Quest.CollapseRewardCells 0`,
//    обоснование — блок CVar в шапке файла.
//
//  ⚠ ДЕФЕКТ ОРИГИНАЛА (исправлен и помечен, закон `fix-original-defects`): `++v7` в эталоне ничем
//    не ограничен, а массив типов — восемь элементов. У пары (QuestId 157, Stage 1) записей с
//    флагом «на выбор» ВОСЕМЬ, то есть оригинал пишет за границу массива (ячейки 8…11). Мы просто
//    останавливаемся на четырёх и говорим об этом в лог.
// ════════════════════════════════════════════════════════════════════════════════════════════════
bool UAsdaQuestLogSubsystem::BuildRewardLines(int32 QuestNo, int32 Stage,
	TArray<FAsdaQuestRewardLine>& OutBasic, TArray<FAsdaQuestRewardLine>& OutChoice)
{
	OutBasic.Reset();
	OutBasic.SetNum(4);            // ячейки с фиксированными ролями; пустая = `Kind == -1`
	OutChoice.Reset();

	const UDataTable* Dt = AsdaDt::Cached(kRewardDT);
	if (!Dt || QuestNo < 0) { return false; }

	// ★ Диапазон 20000…30000 в ЭТУ таблицу не идёт: эталон берёт награду из записи окна ремесла
	//   (`mapObjFindEntryById(g_pQuestCraftWnd, questId)`, запись 388 б, поля `[83…96]`).
	//   Такой таблицы в порту ПОКА НЕТ — молчать нельзя, пишем вслух.
	if (QuestNo >= 20000 && QuestNo <= 30000)
	{
		// ★ 28.10.2026 ДОСКА ОБЪЯВЛЕНИЙ (закрыт PENDING п. 85): запись 388 б доски — `DT_RaidQuestData` (полные поля).
		//   Эталон: золото `[92]` > 0 → ячейка 0, опыт `[91]` > 0 → 1, предметы `[93]/[95]` и `[94]/[96]` ≥ 0 → 2/3,
		//   «на выбор» `[83…86]/[87…90]` ≥ 0 → ячейки 4…7.
		const FAsdaRaidQuestRow* BR = AsdaDt::RowByInt<FAsdaRaidQuestRow>(kRaidDT, QuestNo, TEXT("QuestLog"));
		if (!BR) { return false; }
		bool bZeroMoney = true, bZeroExp = true;
		if (BR->Gold > 0) { OutBasic[0].Kind = 0; OutBasic[0].Amount = BR->Gold; bZeroMoney = false; }
		if (BR->Exp > 0)  { OutBasic[1].Kind = 1; OutBasic[1].Amount = BR->Exp;  bZeroExp = false; }
		for (int32 n = 0; n < 2; ++n)
		{
			const int32 Id = BR->RewardItemIds.IsValidIndex(n) ? BR->RewardItemIds[n] : -1;
			if (Id >= 0)
			{
				OutBasic[2 + n].Kind = 2; OutBasic[2 + n].ItemCode = Id;
				OutBasic[2 + n].Count = BR->RewardItemCounts.IsValidIndex(n) ? BR->RewardItemCounts[n] : 0;
			}
		}
		for (int32 k = 0; k < 4; ++k)
		{
			const int32 Id = BR->ChoiceItemIds.IsValidIndex(k) ? BR->ChoiceItemIds[k] : -1;
			if (Id < 0) { continue; }
			FAsdaQuestRewardLine L;
			L.Kind = 2; L.ItemCode = Id; L.Count = BR->ChoiceItemCounts.IsValidIndex(k) ? BR->ChoiceItemCounts[k] : 0;
			OutChoice.Add(L);
		}
		CollapseRewardCells(OutBasic, bZeroMoney, bZeroExp, QuestNo, Stage);
		return true;
	}

	auto Collect = [Dt](int32 Q, int32 St, TArray<const FAsdaQuestRewardTableRow*>& Out)
	{
		for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
		{
			const FAsdaQuestRewardTableRow* R = reinterpret_cast<const FAsdaQuestRewardTableRow*>(It.Value);
			if (R && R->QuestId == Q && R->Stage == St) { Out.Add(R); }
		}
	};
	TArray<const FAsdaQuestRewardTableRow*> Rows;
	Collect(QuestNo, Stage, Rows);
	// `if (!questBuildRewardSet(this, q, st)) questBuildRewardSet(this, q, st + 1);`
	if (Rows.Num() == 0) { Collect(QuestNo, Stage + 1, Rows); }
	if (Rows.Num() == 0) { return false; }

	// Порядок записей внутри пары эталон берёт файловым; ключ строки `RecordIndex` сплошной 1…636,
	//   а `GetRowMap()` порядок не гарантирует — восстанавливаем его явно.
	Rows.Sort([](const FAsdaQuestRewardTableRow& A, const FAsdaQuestRewardTableRow& B)
		{ return A.RecordIndex < B.RecordIndex; });

	int32 Dropped = 0;
	// ★ «Ячейка пуста ИМЕННО из-за нулевой величины» — сведения для схлопывания (см. блок CVar
	//   `asda.Quest.CollapseRewardCells`, режимы 1 и 2). Величина есть только у двух ячеек:
	//   0 деньги, 1 опыт. В режиме 3 (по умолчанию) схлопывается любая пустота и признак не нужен,
	//   но считаем его всегда — он же попадает в лог и объясняет, ПОЧЕМУ ячейка опустела.
	//   У предметов отсутствие закодировано ЯВНО (`−1`), поэтому им такой признак не нужен.
	bool bSawZeroMoney = false;
	bool bSawZeroExp   = false;
	for (const FAsdaQuestRewardTableRow* R : Rows)
	{
		if (!R) { continue; }
		const bool bChoice = (R->ChoiceFlag != 0);
		if (R->Money == 0) { bSawZeroMoney = true; }
		if (R->Exp   == 0) { bSawZeroExp   = true; }
		// Пороги эталона: обычная запись — `>= 0` у денег и предметов, `> 0` у опыта; запись
		//   «на выбор» — `> 0` у всего (ровно так и написано в двух ветках `questBuildRewardSet`).
		// ★ ОТСТУПЛЕНИЕ по решению владельца 2026-08-23: у ДЕНЕГ порог строгий и в обычной записи —
		//   плитка «0 Золото» не рисуется. Обоснование и ключ отката — блок CVar
		//   `asda.Quest.EtalonZeroMoney` в шапке файла. Порог ПРЕДМЕТОВ не тронут (в данных код
		//   предмета либо −1, либо > 0, так что `>= 0` и `> 0` там неразличимы).
		const int32 MoneyMin = (bChoice || GAsdaQuestEtalonZeroMoney == 0) ? 1 : 0;
		const int32 ItemMin  = bChoice ? 1 : 0;
		if (R->Money >= MoneyMin) { OutBasic[0].Kind = 0; OutBasic[0].Amount = R->Money; }
		if (R->Exp > 0)           { OutBasic[1].Kind = 1; OutBasic[1].Amount = R->Exp; }
		if (R->ItemCode1 >= ItemMin)
		{
			OutBasic[2].Kind = 2; OutBasic[2].ItemCode = R->ItemCode1; OutBasic[2].Count = R->ItemCount1;
		}
		if (R->ItemCode2 >= ItemMin)
		{
			OutBasic[3].Kind = 2; OutBasic[3].ItemCode = R->ItemCode2; OutBasic[3].Count = R->ItemCount2;
		}
		if (!bChoice) { continue; }
		if (OutChoice.Num() >= 4) { ++Dropped; continue; }      // ДЕФЕКТ ОРИГИНАЛА: у него границы нет
		FAsdaQuestRewardLine L;
		L.Kind = 2; L.ItemCode = R->ChoiceItemCode; L.Count = R->ChoiceItemCount;
		OutChoice.Add(L);
	}
	if (Dropped > 0)
	{
		UE_LOG(LogAsdaQuestLog, Warning,
			TEXT("[журнал/награда] квест %d шаг %d: записей «на выбор» %d, а ячеек 4…7 всего четыре — ")
			TEXT("лишние %d отброшены (у оригинала здесь запись ЗА ГРАНИЦУ массива типов)"),
			QuestNo, Stage, OutChoice.Num() + Dropped, Dropped);
	}

	CollapseRewardCells(OutBasic, bSawZeroMoney, bSawZeroExp, QuestNo, Stage);
	return true;
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ★★★ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ: сдвиг плиток влево, чтобы в ряду не оставалось ДЫР.
//  Обоснование, выбор поведения на границе рядов и ключ отката — блок CVar
//  `asda.Quest.CollapseRewardCells` в шапке этого файла; разбор по данным и кадры —
//  `Reports/quest_reward_collapse.md`.
//
//  Тело ОДНО на все окна: его зовёт `BuildRewardLines`, то есть и окно приёма 750, и панель наград
//  журнала 700 получают уже сдвинутый ряд. Роли ячеек после сдвига теряют смысл — потребители и
//  так рисуют плитку по полю `Kind`, а не по номеру (единственное место, где номер что-то значил,
//  высота окна 750, переведено на «занят ли второй ряд»).
//
//  ЧТО СХЛОПЫВАЕТСЯ (режим 3, по умолчанию): ЛЮБАЯ пустая ячейка — и спрятанная из-за нулевой
//  величины, и настоящая пустота (предмет с кодом −1). Порядок оставшихся наград сохраняется.
//  Режимы 1 и 2 оставлены как более осторожные, режим 4 — сдвиг только внутри своего ряда.
//
//  ⚠ Массив «на выбор» (`OutChoice`) сюда не передаётся и не трогается СОЗНАТЕЛЬНО: он и так
//    плотный, а его индекс уходит на сервер (см. блок CVar).
// ════════════════════════════════════════════════════════════════════════════════════════════════
void UAsdaQuestLogSubsystem::CollapseRewardCells(TArray<FAsdaQuestRewardLine>& Basic,
	bool bZeroMoney, bool bZeroExp, int32 QuestNo, int32 Stage)
{
	const int32 Mode = FMath::Clamp(GAsdaQuestCollapseRewardCells, 0, 4);
	if (Mode == 0 || Basic.Num() != 4) { return; }

	TArray<FAsdaQuestRewardLine> Packed;
	Packed.Reserve(4);
	if (Mode <= 2)
	{
		// Осторожные режимы: выкидываем ТОЛЬКО ячейку-величину, опустошённую нулём. Настоящая
		//   пустота (у предметов она закодирована явно, `−1`) остаётся дырой, как в эталоне.
		bool bDrop[4] = {};
		bDrop[0] = (Basic[0].Kind < 0) && bZeroMoney;
		bDrop[1] = (Mode >= 2) && (Basic[1].Kind < 0) && bZeroExp;
		for (int32 i = 0; i < 4; ++i) { if (!bDrop[i]) { Packed.Add(Basic[i]); } }
	}
	else if (Mode == 3)
	{
		// ★ Решение владельца: «сдвигать всегда, когда там пустота». Плотная укладка в порядке
		//   чтения — сквозь границу рядов, поэтому дыр не остаётся нигде.
		for (int32 i = 0; i < 4; ++i) { if (Basic[i].Kind >= 0) { Packed.Add(Basic[i]); } }
	}
	else
	{
		// Режим 4: то же самое, но каждый ряд уплотняется отдельно (ячейки 0/1 — верхний ряд,
		//   2/3 — нижний), поэтому награда из нижнего ряда наверх не поднимается.
		for (int32 Row = 0; Row < 2; ++Row)
		{
			const int32 Base = Row * 2;
			int32 Was = Packed.Num();
			for (int32 i = Base; i < Base + 2; ++i) { if (Basic[i].Kind >= 0) { Packed.Add(Basic[i]); } }
			while (Packed.Num() < Was + 2) { Packed.Add(FAsdaQuestRewardLine()); }
		}
	}
	Packed.SetNum(4);        // хвост добивается пустыми ячейками (`Kind == -1`)
	// «Сдвигать было нечего» — когда ни одна плитка не сменила место. Своего `operator==` у строки
	//   награды нет и заводить его незачем: место определяется видом, его и сверяем.
	bool bMoved = false;
	for (int32 i = 0; i < 4 && !bMoved; ++i) { bMoved = (Packed[i].Kind != Basic[i].Kind); }
	if (!bMoved) { return; }

	UE_LOG(LogAsdaQuestLog, Log,
		TEXT("[журнал/награда] квест %d шаг %d: пустые ячейки схлопнуты (режим %d) — ")
		TEXT("виды по местам стали %d/%d/%d/%d (было %d/%d/%d/%d)"),
		QuestNo, Stage, Mode,
		Packed[0].Kind, Packed[1].Kind, Packed[2].Kind, Packed[3].Kind,
		Basic[0].Kind, Basic[1].Kind, Basic[2].Kind, Basic[3].Kind);

	Basic = MoveTemp(Packed);
}

// Порт `questRewardRefreshSlot@0xC68AC0` — панель наград журнала (строки 786…789, иконки 1690…1693).
//   ★ ИСПРАВЛЕНО 2026-08-23: ячейки рисуются ПО ФИКСИРОВАННЫМ МЕСТАМ, как в эталоне — пустая
//     строка ПРЯЧЕТСЯ, а не схлопывается. Прежде порт сдвигал награды вверх счётчиком `Row`;
//     это была отсебятина: у эталона номер ячейки И ЕСТЬ вид награды (0 деньги, 1 опыт, 2/3
//     предметы), и при сдвиге «Опыт» вставал на место денег.
//   ★★★ И ТУТ ЖЕ УТОЧНЕНО (то же число, решение владельца): ряд приходит из общей точки уже
//     СХЛОПНУТЫМ (`CollapseRewardCells`): дыр в ряду наград не остаётся вовсе — решение владельца
//     «сдвигать всегда, когда там пустота». Своего счётчика строк здесь по-прежнему НЕТ — рисуем
//     ровно то, что дал набор, по полю `Kind`. Ключ отката `asda.Quest.CollapseRewardCells 0`
//     возвращает эталонные фиксированные роли.
void UAsdaQuestLogSubsystem::RefreshRewards()
{
	RewardCodes.Init(0, 4);
	ChoiceCodes.Init(0, 4);
	// Плоская иконка строки награды (вид «опыт»): пусто — значит иконка предметная.
	FString RewardFlat[4], ChoiceFlat[4];
	for (int32 j = 0; j < 4; ++j)
	{
		if (RewardTexts.IsValidIndex(j) && RewardTexts[j]) { RewardTexts[j]->SetText(FText::GetEmpty()); }
		if (ChoiceTexts.IsValidIndex(j) && ChoiceTexts[j]) { ChoiceTexts[j]->SetText(FText::GetEmpty()); }
	}

	const FAsdaQuestSlot* S = SlotAt(ActiveSlot);
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	auto Redraw = [this, &RewardFlat, &ChoiceFlat]()
	{
		for (int32 j = 0; j < 4; ++j)
		{
			DrawRewardIcon(RewardIcons.IsValidIndex(j) ? RewardIcons[j].Get() : nullptr,
				RewardCodes.IsValidIndex(j) ? RewardCodes[j] : 0, *RewardFlat[j]);
			DrawRewardIcon(ChoiceIcons.IsValidIndex(j) ? ChoiceIcons[j].Get() : nullptr,
				ChoiceCodes.IsValidIndex(j) ? ChoiceCodes[j] : 0, *ChoiceFlat[j]);
		}
	};

	HoldGameTables();   // ★ 15.09 (dt_gc_hold): таблицы — сильно у подсистемы на сессию
	TArray<FAsdaQuestRewardLine> Basic, Choice;
	if (!IsSlotBusy(S) || !BuildRewardLines(S->QuestNo, S->Stage, Basic, Choice))
	{
		Redraw();
		if (RewardHdrB) { RewardHdrB->SetVisibility(ESlateVisibility::Collapsed); }
		return;
	}

	auto ItemName = [Items](int32 Code) -> FString
	{
		return Items ? Items->GetItemName(Code) : FString::Printf(TEXT("#%d"), Code);
	};
	// Подпись ячейки — ровно те же три ветки, что в `CQuest_RefreshRewardItemGrid`:
	//   вид 0 «%d <имя предмета 20551>», вид 1 «%d <нотис 6010>» с ПЛОСКОЙ иконкой `exp2`,
	//   вид 2 «<количество> <имя предмета>» с атласной иконкой предмета.
	auto Fill = [this, &ItemName](const FAsdaQuestRewardLine& L, int32& OutCode, FString& OutFlat, FString& OutText)
	{
		switch (L.Kind)
		{
		case 0:
			OutCode = QLoc::kGoldItemCode;
			OutText = FString::Printf(TEXT("%d %s"), L.Amount, *ItemName(QLoc::kGoldItemCode));
			break;
		case 1:
			// ★ ОПЫТ — вид БЕЗ кода предмета: иконка плоская, `exp2` (эталон зашивает имя в код).
			OutCode = 0;
			OutFlat = TEXT("exp2");
			OutText = FString::Printf(TEXT("%d %s"), L.Amount, *Loc(QLoc::kExpWord));
			break;
		case 2:
			OutCode = L.ItemCode;
			OutText = FString::Printf(TEXT("%d %s"), FMath::Max(1, L.Count), *ItemName(L.ItemCode));
			break;
		default:
			break;      // ячейка пуста — строка остаётся погашенной
		}
	};

	for (int32 j = 0; j < 4; ++j)
	{
		FString Text;
		Fill(Basic[j], RewardCodes[j], RewardFlat[j], Text);
		if (RewardTexts.IsValidIndex(j) && RewardTexts[j]) { RewardTexts[j]->SetText(FText::FromString(Text)); }

		FString CText;
		if (Choice.IsValidIndex(j)) { Fill(Choice[j], ChoiceCodes[j], ChoiceFlat[j], CText); }
		if (ChoiceTexts.IsValidIndex(j) && ChoiceTexts[j]) { ChoiceTexts[j]->SetText(FText::FromString(CText)); }
	}
	// ── FIX-ORIG D8: заголовок 790 «Дополнительная награда». В эталоне журнала он привязан, погашен
	//   и больше нигде не упоминается (мёртв), хотя данные для него есть. Оживляем СТРУКТУРНО:
	//   показываем ровно тогда, когда набор дал хотя бы одну запись «на выбор» — так же, как это
	//   делает окно 750 (`SetVisible(pConfirmTextT2, hasExtraReward)`).
	if (RewardHdrB)
	{
		RewardHdrB->SetVisibility(Choice.Num() > 0 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	Redraw();
}

// ★ 31.10.2026 (баг 6): строки гида, которые попадают на кнопки 705…708, — ОДИН отбор и для подписей
//   (`BuildRoutesFor`), и для цели строки без координат (`TravelToRoute`): индекс кнопки = индекс в этом списке.
//   Строки одного ключа идут в файле подряд; эталон читает их по порядку и останавливается, когда `ContinueFlag == 0`
//   (или когда набралось 4 строки — столько кнопок; ключей с 5+ строками в данных нет — аудит 31.10).
static TArray<const FAsdaQuestGuideRow*> GuideRowsForButtons(int32 QuestNo, int32 Stage)
{
	TArray<const FAsdaQuestGuideRow*> Rows, Out;
	const UDataTable* Dt = AsdaDt::Cached(kGuideDT);
	if (!Dt || QuestNo < 0) { return Out; }
	for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
	{
		const FAsdaQuestGuideRow* R = reinterpret_cast<const FAsdaQuestGuideRow*>(It.Value);
		if (R && R->QuestNo == QuestNo && R->Stage == Stage) { Rows.Add(R); }
	}
	Rows.Sort([](const FAsdaQuestGuideRow& A, const FAsdaQuestGuideRow& B) { return A.Index < B.Index; });
	for (const FAsdaQuestGuideRow* R : Rows)
	{
		Out.Add(R);
		if (Out.Num() >= 4) { break; }        // эталон: `if (lineIdx + 1 < 4) ++lineIdx;`
		if (R->ContinueFlag == 0) { break; }  // строка последняя
	}
	return Out;
}

// Порт `QuestGuide_BuildObjectiveText@0xC68E00`: до 4 строк «=> Цель — Карта, x:…, y:…» из `QDT.bin`.
TArray<FAsdaQuestRoute> UAsdaQuestLogSubsystem::BuildRoutesFor(int32 QuestNo, int32 Stage) const
{
	HoldGameTables();   // ★ 15.09 (dt_gc_hold): таблицы — сильно у подсистемы на сессию
	TArray<FAsdaQuestRoute> Out;
	// ★ [ИСПРАВЛЕНИЕ] 31.10.2026 (решение владельца по багу 6): цель шага ВЫПОЛНЕНА — кнопки ведут к NPC СДАЧИ, подпись — его
	//   имя, карта и клетка в формате строки оригинала. Эталон и в этом состоянии показывал строки гида шага (к мобам), а карта
	//   мира (значок «NPC сдачи», 31.10) — NPC: кнопка и карта расходились. Источник NPC и точки — общий с картой мира
	//   (`AsdaQuestGuideTarget::TurnInNpcs`). Подпись и точка меняются сразу по пакету прогресса: `OnQuestStateChanged` →
	//   `bDirty` → `RefreshAll` → `RefreshRoutes`. Ключ отката `asda.Quest.FollowDoneToNpc 0`.
	UAsdaQuestMarkerSubsystem* QM = Marks();
	if (AsdaQuestGuideTarget::FollowDoneToNpcOn() && QM && QuestNo >= 0 && QM->IsQuestObjectiveDone(QuestNo))
	{
		TArray<AsdaQuestGuideTarget::FTurnIn> Npcs;
		AsdaQuestGuideTarget::TurnInNpcs(QM, QuestNo, Stage, /*bByStep*/ true, Npcs);
		// РОВНО ОДИН NPC — тот, у кого сервер примет сдачу на этой стадии (`DT_QuestTurnIn`, правило `Process_Quest`), точка —
		//   его серверный спавн. Законная пара (сервер примет у обоих: 104/2, 112/0) — ближайший к персонажу.
		//   Сверка всех шагов — `Tools/asda_quest_turnin_table.py` → `Reports/bug_quest_follow/turnin_all.csv`.
		UGameInstance* GI = GetGameInstance();
		const UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
		const APlayerController* PC0 = (GI && GI->GetWorld()) ? GI->GetWorld()->GetFirstPlayerController() : nullptr;
		const APawn* Me = PC0 ? PC0->GetPawn() : nullptr;
		const FIntPoint MyTile = Me ? AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(Me->GetActorLocation())) : FIntPoint(0, 0);
		AsdaQuestGuideTarget::FTurnIn T;
		if (AsdaQuestGuideTarget::PickTurnIn(Npcs, LM ? LM->CurrentMapId : -1, MyTile, T))
		{
			FAsdaQuestRoute Route;
			Route.MapId = T.Place.MapId;
			Route.X = T.Place.TileX;
			Route.Y = T.Place.TileY;
			Route.Text = FString::Printf(TEXT("=> %s - %s, x:%d, y: %d"), *RouteTargetName(0, T.NpcId), *MapNameById(T.Place.MapId),
				Route.X, Route.Y);
			Out.Add(Route);
		}
		if (Out.Num() > 0) { return Out; }
		// Сервер на этой стадии сдачу не принимает ни у кого (94 состояния: 76 — задания, которые сервер не выдаёт; 2 — недостижимы; 16 — сломаны данными сервера, ячеек нет;
		//   `turnin_undetermined.txt`) — остаются строки гида шага, как в оригинале; случайного NPC не выбираем.
	}
	for (const FAsdaQuestGuideRow* R : GuideRowsForButtons(QuestNo, Stage))
	{
		FAsdaQuestRoute Route;
		Route.MapId = R->MapId;
		Route.X = R->X;
		Route.Y = R->Y;
		const FString Target = RouteTargetName(R->TargetKind, R->TargetId);
		const FString Map    = MapNameById(R->MapId);
		// Формат строки 1:1 с эталоном: с координатами и без.
		Route.Text = (R->X == -1 || R->Y == -1)
			? FString::Printf(TEXT("=> %s - %s"), *Target, *Map)
			: FString::Printf(TEXT("=> %s - %s, x:%d, y: %d"), *Target, *Map, R->X, R->Y);
		Out.Add(Route);
	}
	return Out;
}

void UAsdaQuestLogSubsystem::RefreshRoutes()
{
	Routes.Reset();
	const FAsdaQuestSlot* S = SlotAt(ActiveSlot);
	if (IsSlotBusy(S)) { Routes = BuildRoutesFor(S->QuestNo, S->Stage); }

	for (int32 i = 0; i < 4; ++i)
	{
		const bool bHas = Routes.IsValidIndex(i);
		if (RouteBtns.IsValidIndex(i) && RouteBtns[i])
		{
			RouteBtns[i]->SetVisibility(bHas ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}
		if (RouteTexts.IsValidIndex(i) && RouteTexts[i])
		{
			RouteTexts[i]->SetVisibility(bHas ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			RouteTexts[i]->SetText(bHas ? FText::FromString(Routes[i].Text) : FText::GetEmpty());
		}
	}
	// Анимированный указатель 745 «Click» висит над первой кнопкой — вместе с ней и живёт.
	if (UWidget* Grp = Node(Win, QLogUi::RouteGrp1))
	{
		Grp->SetVisibility(Routes.Num() > 0 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
}

// ── Идти к цели маршрута (порт `miniMapClick_TravelTo@0xC692D0`) ─────────────────────────────────
void UAsdaQuestLogSubsystem::TravelToRoute(int32 RouteIdx)
{
	if (!Routes.IsValidIndex(RouteIdx)) { return; }
	// Точка маршрута — копия строки: у строки без координат её подменяет место цели (ниже), подпись кнопки не меняется.
	FAsdaQuestRoute R = Routes[RouteIdx];

	UGameInstance* GI = GetGameInstance();
	UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	const int32 CurMap = LM ? LM->CurrentMapId : -1;

	// ★★★ FIX-ORIG D1. Эталон: `if (a3 != -1 && a4 != 1)` — вместо `a4 != -1`. Из-за опечатки
	//   (а) цель с `y == 1` не отрабатывала вовсе; (б) цель БЕЗ координат (`y == -1`) проходила
	//   проверку и уводила персонажа в точку −50. Пишем правильное условие.
	if (R.X == -1 || R.Y == -1)
	{
		// ★ [ИСПРАВЛЕНИЕ] 31.10.2026 (баг 6): строка без координат (35 строк оригинала: мобы дворца королевы, замка
		//   шахмат, гор Пламении, NPC там же) ведёт к МЕСТУ ЦЕЛИ — тому же, что карта мира показывает значком цели:
		//   зона спавна моба (`DT_QuestTargetArea`) или NPC (`DT_NpcQuestNpc`). На своей карте — ближайшее к персонажу.
		//   Ключ отката `asda.Quest.FollowNoCoordTarget 0` — прежнее «идти некуда».
		const FAsdaQuestSlot* S = SlotAt(ActiveSlot);
		if (UAsdaQuestMarkerSubsystem* QM = Marks()) { QM->HoldGameTables(); }   // места мобов / SNQ — сильно на сессию (dt_gc_hold)
		const TArray<const FAsdaQuestGuideRow*> Rows = IsSlotBusy(S) ? GuideRowsForButtons(S->QuestNo, S->Stage)
			: TArray<const FAsdaQuestGuideRow*>();
		AsdaQuestGuideTarget::FPlace Pick;
		bool bPicked = false;
		if (AsdaQuestGuideTarget::FollowNoCoordOn() && Rows.IsValidIndex(RouteIdx))
		{
			const FAsdaQuestGuideRow* G = Rows[RouteIdx];
			TArray<AsdaQuestGuideTarget::FPlace> Places;
			AsdaQuestGuideTarget::Places(S->QuestNo, S->Stage, G->TargetKind, G->TargetId, G->MapId, Places);
			UWorld* W0 = GI ? GI->GetWorld() : nullptr;
			const APlayerController* PC0 = W0 ? W0->GetFirstPlayerController() : nullptr;
			const APawn* Me = PC0 ? PC0->GetPawn() : nullptr;
			const FIntPoint MyTile = Me ? AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(Me->GetActorLocation())) : FIntPoint(0, 0);
			bPicked = AsdaQuestGuideTarget::PickForFollow(Places, CurMap, MyTile, Pick);
			UE_LOG(LogAsdaQuestLog, Log,
				TEXT("[журнал] маршрут %d: в гиде нет координат (задание %d, шаг %d, %s %d, карта %d) — мест цели %d, выбрано %s карта %d (%d,%d)"),
				RouteIdx, S->QuestNo, S->Stage, G->TargetKind == 1 ? TEXT("моб-строка") : TEXT("NPC"), G->TargetId, G->MapId,
				Places.Num(), bPicked ? TEXT("") : TEXT("НЕТ"), Pick.MapId, Pick.TileX, Pick.TileY);
		}
		if (!bPicked)
		{
			UE_LOG(LogAsdaQuestLog, Log,
				TEXT("[журнал] маршрут %d: у цели нет координат (x=%d, y=%d) — идти некуда (FIX-ORIG D1)"),
				RouteIdx, R.X, R.Y);
			return;
		}
		R.MapId = Pick.MapId; R.X = Pick.TileX; R.Y = Pick.TileY;
	}
	if (R.MapId != CurMap)
	{
		// ★★★ ЦЕЛЬ НА ДРУГОЙ КАРТЕ — эталон здесь НЕ бездействует:
		//   `miniMapClick_TravelTo@0xC692D0` в ветке `g_nCurMapId != a2` зовёт
		//   `beginMoveToTile@0xFBE140`, то есть взводит межкартовый автопуть, а главный тик игры
		//   ведёт персонажа к КЛЕТКЕ ПОРТАЛА на следующую карту маршрута и после перехода
		//   продолжает сам, пока карты не совпадут (разбор — `quest_tracker_fix.md` §Д3).
		//   ★ Заявку взводит ЕДИНАЯ точка порта — та же, что и клик по большой карте мира;
		//   расчёт шага через порталы тоже единый (`UAsdaQuestMarkerSubsystem::NextPortalStep`).
		UAsdaMinimapSubsystem* MM = GI ? GI->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr;
		if (MM && MM->BeginMapTravel(R.MapId, R.X, R.Y))
		{
			// Строка маршрута («=> цель — карта, x, y») в чат: она уже собрана из QDT, поэтому
			//   идёт полем `Text`, а не через loc-ID. Игрок видит, куда его повели.
			if (UAsdaMessageSubsystem* Msg = GI->GetSubsystem<UAsdaMessageSubsystem>())
			{
				FAsdaMsgParams MP;
				MP.Category = EAsdaMsgCategory::Quest;
				MP.Text = Routes[RouteIdx].Text;
				Msg->Post(MP);
			}
			UE_LOG(LogAsdaQuestLog, Log,
				TEXT("[журнал] маршрут %d ведёт на карту %d (мы на %d) — межкартовый автопуть взведён"),
				RouteIdx, R.MapId, CurMap);
		}
		else
		{
			UE_LOG(LogAsdaQuestLog, Log,
				TEXT("[журнал] маршрут %d: заявку на карту %d взвести не удалось (клетка %d,%d)"),
				RouteIdx, R.MapId, R.X, R.Y);
		}
		return;
	}
	// Цель на СВОЕЙ карте: эталон в этой ветке идёт напрямую (`mapClick_BeginPathMove`), никакой
	//   заявки не взводит. Если автопуть был активен — снимаем: новый приказ его замещает.
	if (UAsdaMinimapSubsystem* MM = GI ? GI->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr)
	{
		if (MM->IsTravelRequested()) { MM->CancelTravelRequest(); }
	}

	UWorld* W = GI ? GI->GetWorld() : nullptr;
	AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
	if (!PC) { return; }
	// Клетка → игровые координаты: центр клетки (эталонная арифметика мини-карты).
	const FVector2D Dst(100.f * R.X + 50.f, 100.f * R.Y + 50.f);
	PC->MoveToGamePoint(Dst);
	// Флажок цели на мини-карте — тот же, что ставит клик по карте (общая точка, своего не заводим).
	if (UAsdaMinimapSubsystem* MM = GI->GetSubsystem<UAsdaMinimapSubsystem>())
	{
		MM->SetDestinationTile(R.X, R.Y, true);
	}
	UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] маршрут %d: идём в клетку (%d,%d) карты %d"),
		RouteIdx, R.X, R.Y, R.MapId);
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ДАННЫЕ И ПОМОЩНИКИ
// ════════════════════════════════════════════════════════════════════════════════════════════════
// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ★★★ ПОРТ `QuestReward_UpdateProgressList@0xC65AA0` — СТРОКИ ЦЕЛЕЙ ДЛЯ ЭКРАННОГО ТРЕКЕРА 1800
// ════════════════════════════════════════════════════════════════════════════════════════════════
// Эталон разбирает ДВЕ ветки по номеру квеста и в обеих пишет строки вида «Название : 3/10 »
//   вызовом `questTracker_UpdateEntry`, а в конце дёргает перестроение окна.
//   Полный разбор с раскладками — `Reports/quest_tracker.md` §5.2/§5.3.
//
// ОТКУДА «НАДО» (это и был блокер записи № 72 реестра, теперь снятый):
//   обычные задания  → под-запись шага книги `.QST` (+796+68·стадия, поле +12) даёт `RecId`
//                      в `DT_SmInfo`, а там `ReqItemIds[5]` / `ReqItemCounts[5]`;
//   крафт (20000…30000) → `DT_RaidQuestData`: `ReqItemIds[5]` / `ReqItemCounts[5]`, имя `Name`.
// ОТКУДА «ЕСТЬ»: счётчики целей самого слота журнала (`ObjIds`/`ObjCounts`, их двигает пакет 5052),
//   а у крафт-записей с `Mode == 1` — сумка (`CountItemById`, эталон зовёт `CInventory_CountItemById`).
// Обводка у экранной строки прогресса задания: 1 (по умолчанию) — с обводкой, 0 — как в оригинале.
static ASDA_DEV_CVAR(int32, CVarQuestProgressOutline,
	TEXT("Asda.Quest.ProgressOutline"), 1,
	TEXT("Обводка у экранной строки прогресса задания: 1 = с обводкой (наше), 0 = как в оригинале."),
	ECVF_Default);
