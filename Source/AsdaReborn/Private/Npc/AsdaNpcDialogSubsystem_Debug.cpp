// Copyright (c) 2026 @sitxovski (Discord)
// AsdaNpcDialogSubsystem_Debug.cpp — ★ 23.10.2026 часть UAsdaNpcDialogSubsystem, вынесенная из AsdaNpcDialogSubsystem.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaNpcDialogSubsystem.plan.json).
//   Живой дамп окон разговора и консольные команды asda.Npc.* (только вне Shipping).
//   #include — те же, что у AsdaNpcDialogSubsystem.cpp, в том же порядке; общее — AsdaNpcDialogSubsystemInternal.h.
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"          // строка списка 2408/2410: ровно 20 px по эталону
#include "Components/ButtonSlot.h"       // выравнивание подписи внутри строки-кнопки
#include "Components/ContentWidget.h"    // обход строки списка (SizeBox → Button → TextBlock)
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/RichTextBlock.h"   // ★ 30.10.2026: строки реплики с разметкой (дамп)
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "AsdaDataTableCache.h"
#include "World/AsdaEntityText.h"   // ★ волна 3 п.14: имя NPC для заголовка диалога — одна точка
#include "Login/AsdaGameText.h"        // FAsdaTextRow — строка DT_Talk (поля npc / line / text)
#include "Npc/AsdaNpcActor.h"
#include "Quest/AsdaQuestTypes.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Login/AsdaText.h"                 // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): FAsdaText::Loc — подписи отказов по id
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaNotice.h"            // ★ 27.09 (clan_btn_6099_fix): MsgBoxLoc — окна нехватки/отказа обмена, как у эталона
#include "UI/AsdaCommunityFriendSubsystem.h"   // AsdaCommunityUi::ShownVisibilityFor — общая точка видимости узла
#include "UI/AsdaBoosterPanelSubsystem.h"   // ★ гейт 13101: «открыто окно бустера» (эталонное поле +0x3F8)
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 29.09.2026: гейт 1 `NPC_OpenWindow` — окно обмена 2008 открыто (`CExchange+0x2C`)
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaNpcShopSubsystem.h"
#include "UI/AsdaEquipShopSubsystem.h"   // 30020 — магазин снаряжения, окно 6800
#include "UI/AsdaBoosterShopSubsystem.h" // 30034 — магазин бустеров, окно 15300
#include "Quest/AsdaQuestLogSubsystem.h"   // ★ волна В6 ремесла: смерть закрывает журнал квестов (`uiWnd_Close`, entity_char.c:16898)
#include "UI/AsdaHairShopSubsystem.h"    // 30029 — салон причёсок, окно 15000
// 30030 — элитный магазин 6000 (волна В5); хвост смерти   ★ R3: комментарий снятого включения "UI/AsdaPremiumShopSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaClanSubsystem.h"       // 30015/30016 — создание и роспуск клана
#include "UI/AsdaSoulmateSubsystem.h"   // ★ волна 1-А: 30023/30024/30025 — соулмейт (окно 9100, разрыв 5204)
#include "UI/AsdaExchangeSubsystem.h"   // 30017/30018/30019/30021/30028 — аукцион, окно 13000
#include "UI/AsdaWarehouseSubsystem.h"   // 30011/30012 — склад кладовщика, окно 4000 (+ свиток 5500)
#include "UI/AsdaConstellationSubsystem.h" // 30073 — выбор знака зодиака, окно 74500
#include "UI/AsdaRepairSubsystem.h"      // 30069 — окно ремонта 57300
#include "UI/AsdaWarShopSubsystem.h"
#include "UI/AsdaExtractShopSubsystem.h"   // 30035 — передача параметров, окно 16000
#include "UI/AsdaItemResetSubsystem.h"     // 30046/30047 — сброс улучшений, окно 20200 (2 режима)
#include "UI/AsdaItemOptionSubsystem.h"    // 30060 — смена параметров предмета, окно 50300     // 30070 — военный магазин 67200   // ★ магазин NPC: коды 30001…30004 / 30026 (окно 1300)
// UAsdaRichText::ApplyFormatted — теги <FC=…> в баннере 599   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "UI/AsdaTipPanel.h"             // общая точка пересборки 9-slice (порт uiPanel9SliceLayout) + EAsdaTipOwner
// ОБЩАЯ точка подсказок предмета (второй копии билдеров нет)   ★ R3: комментарий снятого включения "UI/AsdaItemTooltipSubsystem.h" (файл им не пользовался) — сохранён
#include "Engine/Engine.h"               // GEngine->GameViewport: размер вьюпорта для проверки центра
#include "Engine/GameViewportClient.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "Quest/AsdaQuestMarkerSubsystem.h"
#include "Quest/AsdaQuestLogSubsystem.h"   // ОБЩАЯ точка `BuildRewardLines` — разбор RewardTable.BIN (второй копии нет)
#include "Data/AsdaEffectMobTableRows.h"
#include "AsdaBytes.h"              // ★ ДЕДУП Д4: чтение little-endian из тела пакета — одна точка на порт
#include "Resources/AsdaSoundSubsystem.h"
#include "UI/AsdaIconCellGrid.h"   // общая точка: прозрачные клик-кнопки поверх JUI-ячеек (строки выбора награды)
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaCraftWindowSubsystem.h"   // ★ 30009/30088/30010 — окно создания снаряжения 76000 (ремесло, волна В2)
#include "UI/AsdaCraftMakeWindowSubsystem.h"   // ★ хвост смерти entity_char.c:16910 — сброс окна изготовления 77500 (В8)
#include "UI/AsdaCraftSubsystem.h"             // ★ ключ К-9 `asda.Craft.FixDeathRestoreOrder` (В8)
#include "UI/AsdaHuntReportSubsystem.h"         // ★ волна 2-Б: 30068 — окно «Доклад об охоте» 56450
#include "UI/AsdaTownWarSubsystem.h"            // ★ волна 2-Г: 30076 — окно налога города 79300
#include "Npc/AsdaDonationSubsystem.h"         // ★ волна 2-Б: 30051 — пожертвование (нумпад режима 6 → 6132)
#include "TimerManager.h"
#include "EngineUtils.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "HAL/IConsoleManager.h"            // ★ 27.09 itemsfix: asda.EventExchange.ServerGuard
#include "UI/AsdaDisassemblySubsystem.h"    // ★ волна 2-А: окно разборки 32000 видно ⇒ разговор молча не открывается
#include "Text/AsdaLang.h"                  // [ПОРТ] 09.10.2026 (мультиязычность): заголовок/кнопки/реплики сценария на языке сессии
#include "Text/AsdaLangRows.h"              // [ПОРТ] 09.10.2026 (мультиязычность): перевод копии страницы сценария
#include "AsdaNpcDialogSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaNpcDialogSubsystem*.cpp (LogAsdaNpcDlg, чистые хелперы)


// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ЖИВОЙ ДАМП + КОНСОЛЬ
// ════════════════════════════════════════════════════════════════════════════════════════════════
// ★ Живой разбор условий страницы: печатает по каждой под-записи каждого шага, какие категории
//   заданы, какие сошлись, и итог. Без него «условия работают» — это слова.
FString UAsdaNpcDialogSubsystem::DumpConditions(int32 TalkId) const
{
	const int32 Want = (TalkId > 0) ? TalkId : CurrentTalkId;
	FString S = FString::Printf(TEXT("=== условия страницы talkId=%d сценария «%s» ===\n"), Want, *CurrentNsr);
	const FAsdaNsrPageRow* Page = FindPage(CurrentNsr, Want);
	if (!Page) { return S + TEXT("страницы нет в DT_NpcNsrPage\n"); }

	for (int32 i = 0; i < Page->Steps.Num(); ++i)
	{
		const FAsdaNsrStep& St = Page->Steps[i];
		S += FString::Printf(TEXT("шаг %d «%s»: прогресс=%d, первое состояние=%d\n"),
			i, *St.Caption, GetEntryProgressState(St), CraftSlotFindFirstStateIndex(i));
		for (int32 m = 0; m < St.Subs.Num(); ++m)
		{
			const FAsdaNsrStepSub& Sub = St.Subs[m];
			if (Sub.State == 0) { continue; }
			FString Why;
			const bool bOk = EvalCond(Sub.Cond, /*bStepOwner*/true, &Why);
			S += FString::Printf(TEXT("  под-запись %d: состояние=%d уровень=%d…%d класс=%d/%d квесты=%d/%d/%d/%d ")
				TEXT("ветка=%d результаты=%d/%d/%d принять-квест=%d → %s\n%s"),
				m, Sub.State, Sub.Cond.LevelMin, Sub.Cond.LevelMax, Sub.Cond.ClassId, Sub.Cond.ClassMode,
				Sub.Cond.QuestDone, Sub.Cond.QuestFreeA, Sub.Cond.QuestActive, Sub.Cond.QuestStage,
				Sub.BranchTalkId, Sub.Res0, Sub.Res1, Sub.Res2, Sub.AcceptQuestId,
				bOk ? TEXT("ВЫПОЛНЕНО") : TEXT("не выполнено"), *Why);
		}
	}
	// Кнопки меню: у них условие проверяется только при состоянии 2.
	for (int32 b = 0; b < MenuCount; ++b)
	{
		const FAsdaNsrButtonRow* B = FindButton(CurrentNsr, b);
		if (!B) { continue; }
		for (int32 j = 0; j < B->Subs.Num(); ++j)
		{
			const FAsdaNsrButtonSub& Sub = B->Subs[j];
			if (Sub.State == 0) { continue; }
			FString Why;
			const bool bOk = (Sub.State == 2) ? EvalCond(Sub.Cond, /*bStepOwner*/false, &Why) : true;
			S += FString::Printf(TEXT("кнопка %d («%s») под-запись %d: состояние=%d код=%d параметр=%d → %s\n%s"),
				b, *B->Caption, j, Sub.State, Sub.CmdCode, Sub.Param,
				(Sub.State == 2) ? (bOk ? TEXT("ВЫПОЛНЕНО") : TEXT("не выполнено")) : TEXT("без проверки"), *Why);
		}
	}
	return S;
}

// ★ Живой разбор облачка выбора квеста.
FString UAsdaNpcDialogSubsystem::DumpMultiQuest() const
{
	FString S = FString::Printf(TEXT("=== MULTYQUEST_WIN (2406): живое состояние ===\nnpcId=%d выдача=%d сдача=%d\n"),
		CurrentNpcId, GiveList.Num(), TakeList.Num());
	if (!MqWin) { return S + TEXT("виджет окна НЕ создан\n"); }
	UAsdaQuestMarkerSubsystem* QM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
	auto Line = [&](const TCHAR* Tag, const UWidget* W, const FString& Extra)
	{
		FVector2D Mn(0, 0), Mx(0, 0);
		const bool bRect = W && AsdaUi::WidgetAbsRect(W, Mn, Mx);
		S += FString::Printf(TEXT("  %-24s виджет=%d видим=%d rect=%s %s\n"),
			Tag, W ? 1 : 0, (W && IsEffectivelyVisible(W)) ? 1 : 0,
			bRect ? *FString::Printf(TEXT("(%.0f,%.0f)-(%.0f,%.0f)"), Mn.X, Mn.Y, Mx.X, Mx.Y) : TEXT("нет"), *Extra);
	};
	Line(TEXT("MULTYQUEST_TXT00 (2407)"), MqTxt00,
		MqTxt00Text ? FString::Printf(TEXT("текст=«%s»"), *MqTxt00Text->GetText().ToString()) : FString());
	Line(TEXT("MULTYQUEST_LIST00 (2408)"), MqList00,
		MqList00 ? FString::Printf(TEXT("строк=%d"), MqList00->GetChildrenCount()) : FString());
	Line(TEXT("MULTYQUEST_TXT01 (2409)"), MqTxt01,
		MqTxt01Text ? FString::Printf(TEXT("текст=«%s»"), *MqTxt01Text->GetText().ToString()) : FString());
	Line(TEXT("MULTYQUEST_LIST01 (2410)"), MqList01,
		MqList01 ? FString::Printf(TEXT("строк=%d"), MqList01->GetChildrenCount()) : FString());
	Line(TEXT("MULTYQUEST_CLOSE (2411)"), MqClose, FString());

	// ★ ЧИСЛОВОЕ ДОКАЗАТЕЛЬСТВО ЦЕНТРА И РАЗМЕРА (закон `false-negatives-method`: «окно видно» —
	//   не доказательство). Печатаем прямоугольник РАМКИ (не bbox всех групп), центр вьюпорта и
	//   расхождение между ними; на любом разрешении оба числа обязаны быть около нуля.
	{
		FVector2D FMn(0, 0), FMx(0, 0);
		const UWidget* Center = (MqFrame.Num() == 9) ? ToRawPtr(MqFrame[4]) : nullptr;
		const UWidget* TL = (MqFrame.Num() == 9) ? ToRawPtr(MqFrame[0]) : nullptr;
		const UWidget* BR = (MqFrame.Num() == 9) ? ToRawPtr(MqFrame[8]) : nullptr;
		FVector2D TLMn(0, 0), TLMx(0, 0), BRMn(0, 0), BRMx(0, 0);
		if (TL && BR && AsdaUi::WidgetAbsRect(TL, TLMn, TLMx) && AsdaUi::WidgetAbsRect(BR, BRMn, BRMx))
		{
			FMn = TLMn; FMx = BRMx;
		}
		// ★ Сравнивать с `ViewportSizeUnits` НЕЛЬЗЯ: абсолютные координаты Slate отсчитываются от
		//   окна приложения, и в редакторе игровой вьюпорт смещён вниз на высоту его обвязки —
		//   получилось бы фальшивое расхождение в сотню пикселей (ложный отрицательный результат,
		//   закон `false-negatives-method`). Экран для окна — это ЕГО СОБСТВЕННАЯ геометрия:
		//   корневой виджет добавлен во вьюпорт и занимает его целиком.
		FVector2D VpMn(0, 0), VpMx(0, 0);
		const bool bVp = AsdaUi::WidgetAbsRect(MqWin, VpMn, VpMx);
		const FVector2D WinC = (FMn + FMx) * 0.5f;
		const FVector2D VpC  = (VpMn + VpMx) * 0.5f;
		S += FString::Printf(
			TEXT("  рамка окна: (%.0f,%.0f)-(%.0f,%.0f) размер=%.0fx%.0f (JUI 258x58); ")
			TEXT("вьюпорт=(%.0f,%.0f)-(%.0f,%.0f) центр окна=(%.0f,%.0f) центр вьюпорта=(%.0f,%.0f) ")
			TEXT("расхождение=(%.1f,%.1f)%s%s\n"),
			FMn.X, FMn.Y, FMx.X, FMx.Y, FMx.X - FMn.X, FMx.Y - FMn.Y,
			VpMn.X, VpMn.Y, VpMx.X, VpMx.Y, WinC.X, WinC.Y, VpC.X, VpC.Y,
			WinC.X - VpC.X, WinC.Y - VpC.Y,
			Center ? TEXT("") : TEXT(" [рамка НЕ собрана]"),
			bVp ? TEXT("") : TEXT(" [геометрия корня не готова]"));
	}
	// Строки списков: текст, ширина текста и признак «выбрана» (эталонное поле `+327`).
	{
		auto DumpRows = [&](const TCHAR* Tag, UScrollBox* Box, int32 CtrlId)
		{
			if (!Box) { return; }
			for (int32 i = 0; i < Box->GetChildrenCount(); ++i)
			{
				USizeBox* SzB = Cast<USizeBox>(Box->GetChildAt(i));
				UButton* B = SzB ? Cast<UButton>(SzB->GetContent()) : nullptr;
				UTextBlock* T = B ? Cast<UTextBlock>(B->GetContent()) : nullptr;
				FVector2D Mn(0, 0), Mx(0, 0);
				const bool bR = B && AsdaUi::WidgetAbsRect(B, Mn, Mx);
				S += FString::Printf(TEXT("      %s строка %d: кнопка=%d rect=%s текст=«%s» ширина_текста=%.0f%s\n"),
					Tag, i, B ? 1 : 0,
					bR ? *FString::Printf(TEXT("(%.0f,%.0f)-(%.0f,%.0f)"), Mn.X, Mn.Y, Mx.X, Mx.Y) : TEXT("нет"),
					T ? *T->GetText().ToString() : TEXT(""),
					T ? AsdaUi::MeasureTextWidth(T->GetText().ToString(), T->GetFont(), 0.f) : 0.f,
					(CtrlId == MqSelCtrl && i == MqSelIndex) ? TEXT(" ВЫБРАНА") : TEXT(""));
			}
		};
		DumpRows(TEXT("2408"), MqList00, 2408);
		DumpRows(TEXT("2410"), MqList01, 2410);
	}
	for (int32 i = 0; i < GiveList.Num(); ++i)
	{
		S += FString::Printf(TEXT("      выдача %d: квест=%d ключ=%d talkId=%d «%s»\n"), i,
			GiveList[i].QuestId, GiveList[i].Key, GiveList[i].TalkId,
			QM ? *QM->EpisodeTitleByQuestKey(GiveList[i].QuestId) : TEXT(""));
	}
	for (int32 i = 0; i < TakeList.Num(); ++i)
	{
		S += FString::Printf(TEXT("      сдача %d: квест=%d ключ=%d talkId=%d «%s»\n"), i,
			TakeList[i].QuestId, TakeList[i].Key, TakeList[i].TalkId,
			QM ? *QM->EpisodeTitleByQuestKey(TakeList[i].QuestId) : TEXT(""));
	}
	return S;
}

FString UAsdaNpcDialogSubsystem::DumpWindow() const
{
	FString S = TEXT("=== NPC_LINE NPC (570): живое состояние ===\n");
	S += FString::Printf(TEXT("окно открыто=%d npcId=%d сценарий=«%s» talkId=%d страница=#%d кнопок меню=%d\n"),
		bDialogOpen ? 1 : 0, CurrentNpcId, *CurrentNsr, CurrentTalkId, CurrentPageIndex, MenuCount);
	if (!Win) { return S + TEXT("виджет окна НЕ создан\n"); }

	// ★ Кроме «виджет есть / видим» печатаем ЖИВОЙ ПРЯМОУГОЛЬНИК (абсолютные координаты Slate).
	//   Без него нельзя отличить «контрол не построен» от «контрол построен, но вьюпорт мал и он за
	//   краем» — ровно этот случай даёт headless-редактор с вьюпортом ~765×420 при базе окна 768.
	auto Line = [&](const TCHAR* Tag, const UWidget* W, const FString& Extra)
	{
		FVector2D Mn(0, 0), Mx(0, 0);
		const bool bRect = W && AsdaUi::WidgetAbsRect(W, Mn, Mx);
		S += FString::Printf(TEXT("  %-22s виджет=%d видим=%d rect=%s %s\n"),
			Tag, W ? 1 : 0, (W && IsEffectivelyVisible(W)) ? 1 : 0,
			bRect ? *FString::Printf(TEXT("(%.0f,%.0f)-(%.0f,%.0f)"), Mn.X, Mn.Y, Mx.X, Mx.Y) : TEXT("нет"),
			*Extra);
	};
	Line(TEXT("NPC_TALKBOX_grp"), TalkBoxGrp, FString());
	Line(TEXT("NPC_Talkbox2_grp"), BubbleGrp, FString());
	Line(TEXT("NPC_Function_Box_grp"), FuncBoxGrp, FString());
	Line(TEXT("NPC_TNAME (573)"), NameText, NameText ? FString::Printf(TEXT("текст=«%s»"), *NameText->GetText().ToString()) : FString());
	Line(TEXT("NPC_POSI (портрет)"), Portrait, FString());
	Line(TEXT("NPC_TTALK (574)"), TalkLines, TalkLines ? FString::Printf(TEXT("строк=%d перенос=%.0f"), TalkLines->GetChildrenCount(), TalkWrapWidth) : FString());
	// ★ САМИ СТРОКИ реплики. Без них «строк=7» ничего не доказывает: текст мог прийти пустым,
	//   с непроставленным `[NAME]` или с потерянными переводами строк.
	if (TalkLines)
	{
		for (int32 i = 0; i < TalkLines->GetChildrenCount(); ++i)
		{
			const UWidget* Ch = TalkLines->GetChildAt(i);
			const UTextBlock* Tb = Cast<UTextBlock>(Ch);
			// ★ 30.10.2026: строка с тегами — URichTextBlock (`asda.NpcDlg.TalkMarkup`); печатаем её rich-разметку.
			const URichTextBlock* Rt = Cast<URichTextBlock>(Ch);
			S += FString::Printf(TEXT("      строка %d: %s«%s»\n"), i, Rt ? TEXT("[rich] ") : TEXT(""),
				Tb ? *Tb->GetText().ToString() : Rt ? *Rt->GetText().ToString() : TEXT("<не текст>"));
		}
	}
	Line(TEXT("NPC_TALKCLOSE (575)"), CloseBtn, FString());
	// У стрелок печатаем И статическую подпись (то, что видит игрок), И подпись ШАГА из данных —
	//   так видно, что данные прочитаны, хотя эталон их на кнопке не показывает.
	auto StepCap = [&](int32 Idx) -> FString
	{
		const FAsdaNsrPageRow* Pg = (Idx >= 0) ? FindPage(CurrentNsr, CurrentTalkId) : nullptr;
		return (Pg && Pg->Steps.IsValidIndex(Idx)) ? Pg->Steps[Idx].Caption : FString();
	};
	Line(TEXT("NPC_PRE_BUT (576)"), PreGrp, FString::Printf(TEXT("шаг=%d подпись=«%s» подпись_шага=«%s»"),
		PreStepIndex, PreText ? *PreText->GetText().ToString() : TEXT(""), *StepCap(PreStepIndex)));
	Line(TEXT("NPC_NEXT_BUT (577)"), NextGrp, FString::Printf(TEXT("шаг=%d подпись=«%s» подпись_шага=«%s»"),
		NextStepIndex, NextText ? *NextText->GetText().ToString() : TEXT(""), *StepCap(NextStepIndex)));
	for (int32 i = 0; i < QaButtons.Num(); ++i)
	{
		Line(*FString::Printf(TEXT("NPC_QA_BUT%02d (%d)"), i, 578 + i), QaButtons[i],
			FString::Printf(TEXT("шаг=%d подпись=«%s»"), QaSlot[i],
				(QaTexts.IsValidIndex(i) && QaTexts[i]) ? *QaTexts[i]->GetText().ToString() : TEXT("")));
	}
	for (int32 i = 0; i < ShopButtons.Num(); ++i)
	{
		const FAsdaNsrButtonRow* B = FindButton(CurrentNsr, i);
		Line(*FString::Printf(TEXT("NPC_SHOP_BUT%02d (%d)"), i, 581 + i), ShopButtons[i],
			FString::Printf(TEXT("подпись=«%s» код=%d ширина=%.0f"),   // ★ 06.11.2026: ширина подписи — проверка «влезает в кнопку 182» (asda.Clan.BtnSelfTest)
				(ShopTexts.IsValidIndex(i) && ShopTexts[i]) ? *ShopTexts[i]->GetText().ToString() : TEXT(""),
				(B && B->Subs.Num() > 0) ? B->Subs[0].CmdCode : 0,
				(ShopTexts.IsValidIndex(i) && ShopTexts[i]) ? (ShopTexts[i]->ForceLayoutPrepass(), ShopTexts[i]->GetDesiredSize().X) : 0.f));
	}
	return S;
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GNpcDialogOpen(
	TEXT("asda.Npc.Dialog"),
	TEXT("asda.Npc.Dialog <npcId> — открыть окно разговора с NPC по его id (проверка на живом PIE)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaNpcDialogSubsystem* S = DlgSub(W);
		if (!S || Args.Num() < 1) { return; }
		const int32 Id = FCString::Atoi(*Args[0]);
		// ★ Идём ТЕМ ЖЕ путём, что и мышь (`AAsdaPlayerController::OnLeftClick`: `SetTarget(Npc)` →
		//   `StartNpcTalk` → `OpenForNpc(Npc)`), если актор с таким `NpcId` на уровне есть. Иначе
		//   остаётся прежний вход по id. Без актора не взводится ни `CurrentNpcActor`, ни выделение,
		//   и живая проверка «побежал → окно закрылось, выделение снялось» проверяла бы полправила.
		AAsdaNpcActor* Actor = nullptr;
		if (W)
		{
			for (TActorIterator<AAsdaNpcActor> It(W); It; ++It)
			{
				if (*It && (*It)->NpcId == Id) { Actor = *It; break; }
			}
		}
		bool bOk = false;
		if (Actor)
		{
			if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W->GetFirstPlayerController()))
			{
				PC->SetTarget(Actor);
			}
			bOk = S->OpenForNpc(Actor);
		}
		else
		{
			bOk = S->OpenForNpcId(Id);
		}
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("asda.Npc.Dialog %d (актор %s) → %s"),
			Id, Actor ? TEXT("найден") : TEXT("нет"), bOk ? TEXT("открыто") : TEXT("отказ"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GNpcMultiQuest(
	TEXT("asda.Npc.MultiQuest"),
	TEXT("asda.Npc.MultiQuest — построить и показать облачко выбора квеста (2406) для текущего NPC"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		if (UAsdaNpcDialogSubsystem* S = DlgSub(W)) { S->ShowMultiQuest(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GNpcMultiQuestDump(
	TEXT("asda.Npc.MultiQuestDump"),
	TEXT("asda.Npc.MultiQuestDump — живой разбор облачка 2406: оба списка, режим, прямоугольники"),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		if (UAsdaNpcDialogSubsystem* S = DlgSub(W)) { UE_LOG(LogAsdaNpcDlg, Log, TEXT("%s"), *S->DumpMultiQuest()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GNpcMultiQuestPick(
	TEXT("asda.Npc.MultiQuestPick"),
	TEXT("asda.Npc.MultiQuestPick <2408|2410> <индекс> — выбрать строку облачка (проверка перехода)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaNpcDialogSubsystem* S = DlgSub(W);
		if (!S || Args.Num() < 2) { return; }
		S->PickMultiQuest(FCString::Atoi(*Args[0]), FCString::Atoi(*Args[1]));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GNpcCommand(
	TEXT("asda.Npc.Command"),
	TEXT("asda.Npc.Command <код> [параметр] — прогнать код диспетчера 30001…31000 ТЕМ ЖЕ путём, что кнопка"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: любой код диспетчера 30001…31000 без открытого диалога и без проверки кнопки — при живом соединении 5477 (телепорт), 6098, 6124, 6091, 6059, 6746, 6749, 6146, 5455 и т. п.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Npc.Command"))) { return; }
		UAsdaNpcDialogSubsystem* S = DlgSub(W);
		if (!S || Args.Num() < 1) { return; }
		S->RunCommandForCheck(FCString::Atoi(*Args[0]), Args.Num() > 1 ? FCString::Atoi(*Args[1]) : -1);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GNpcCondDump(
	TEXT("asda.Npc.CondDump"),
	TEXT("asda.Npc.CondDump <talkId> — разбор условий ВСЕХ под-записей шагов страницы (категория/итог)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaNpcDialogSubsystem* S = DlgSub(W);
		if (!S || Args.Num() < 1) { return; }
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("%s"), *S->DumpConditions(FCString::Atoi(*Args[0])));
	}));
#endif // ASDA_DEV_CONSOLE

// ★ ЖИВАЯ ПРОВЕРКА ПОПАПА НАГРАД БЕЗ СЕРВЕРА (этап Э6). Идёт ТЕМ ЖЕ путём, что и ветка
//   «состояние прогресса 2» — отдельной тестовой ветки НЕТ: команда лишь подставляет тройку
//   (сценарий, страница, шаг) и зовёт `ShowRewardPopup`.
//   Готовые тройки с наградой видны командой `asda.Npc.RewardScan`.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GNpcRewardShow(
	TEXT("asda.Npc.RewardShow"),
	TEXT("asda.Npc.RewardShow <nsr> <talkId> <шаг> [под-запись] — показать попап наград 540 по данным"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: попап награды «для проверки» с NSR/шагом из аргументов — при наградах только видов 8/9 сразу шлёт 5041 (сдача НАСТОЯЩЕГО квеста без разговора с NPC).
		static const uint16 kGuardOps[] = { 5041 };
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Npc.RewardShow"), 5.f, MakeArrayView(kGuardOps))) { return; }
		UAsdaNpcDialogSubsystem* S = DlgSub(W);
		if (!S || Args.Num() < 3) { UE_LOG(LogAsdaNpcDlg, Warning, TEXT("asda.Npc.RewardShow <nsr> <talkId> <шаг> [под-запись]")); return; }
		S->ShowRewardPopupForCheck(Args[0], FCString::Atoi(*Args[1]), FCString::Atoi(*Args[2]),
			Args.Num() > 3 ? FCString::Atoi(*Args[3]) : 0);
	}));
#endif // ASDA_DEV_CONSOLE

// ★ ЖИВАЯ ПРОВЕРКА ОКНА ПРИЁМА ЗАДАНИЯ 750 БЕЗ СЕРВЕРА. Отдельной тестовой ветки НЕТ: команда
//   зовёт РОВНО то же тело, что и приём 5044 (`ShowQuestAcceptWindow`), только тройку
//   (эпизод, квест, шаг) подставляет руками.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GNpcAcceptShow(
	TEXT("asda.Npc.AcceptShow"),
	TEXT("asda.Npc.AcceptShow <эпизод> <квест> <шаг> — показать окно приёма задания 750 по данным RewardTable"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaNpcDialogSubsystem* S = DlgSub(W);
		if (!S || Args.Num() < 3) { UE_LOG(LogAsdaNpcDlg, Warning, TEXT("asda.Npc.AcceptShow <эпизод> <квест> <шаг>")); return; }
		S->ShowQuestAcceptWindow(FCString::Atoi(*Args[0]), FCString::Atoi(*Args[1]), FCString::Atoi(*Args[2]));
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("%s"), *S->DumpQuestAcceptWindow());
	}));
#endif // ASDA_DEV_CONSOLE

// ★ Числовой дамп окна 750 — «проверять числами». Печатает эффективную видимость и подписи.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GNpcAcceptDump(
	TEXT("asda.Npc.AcceptDump"),
	TEXT("asda.Npc.AcceptDump — состояние окна приёма задания 750 (видимость, высота, плитки)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaNpcDialogSubsystem* S = DlgSub(W)) { UE_LOG(LogAsdaNpcDlg, Log, TEXT("%s"), *S->DumpQuestAcceptWindow()); }
	}));
#endif // ASDA_DEV_CONSOLE

// ★ Закрытие окна 750 «как кнопкой ОК» — своей ветки нет, зовётся то же тело, что и релей 780.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GNpcAcceptOk(
	TEXT("asda.Npc.AcceptOk"),
	TEXT("asda.Npc.AcceptOk — нажать «ОК» (780) в окне приёма задания 750"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaNpcDialogSubsystem* S = DlgSub(W))
		{
			S->HideQuestAcceptWindow();
			UE_LOG(LogAsdaNpcDlg, Log, TEXT("%s"), *S->DumpQuestAcceptWindow());
		}
	}));
#endif // ASDA_DEV_CONSOLE

// ★ ЖИВАЯ ПРОВЕРКА РАМКИ ВЫДЕЛЕНИЯ (замечание владельца №2). Своей ветки НЕТ: команда зовёт ровно
//   то же тело, что и прозрачная клик-кнопка строки (`ClickRewardSelectCell`).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GNpcRewardSelect(
	TEXT("asda.Npc.RewardSelect"),
	TEXT("asda.Npc.RewardSelect <строка 0..5> — выбрать строку панели выбора попапа 540 (как клик мышью)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaNpcDialogSubsystem* S = DlgSub(W);
		if (!S || Args.Num() < 1) { UE_LOG(LogAsdaNpcDlg, Warning, TEXT("asda.Npc.RewardSelect <строка 0..5>")); return; }
		S->ClickRewardSelectCell(FCString::Atoi(*Args[0]));
	}));
#endif // ASDA_DEV_CONSOLE

// ★ ЖИВАЯ ПРОВЕРКА ПОДСКАЗКИ ПО ИКОНКЕ НАГРАДЫ (порт msg 8 окна 540). Своей ветки показа НЕТ:
//   команда зовёт ровно то же тело, что и покадровый опрос курсора (`TickRewardTooltips`).
//   В headless-PIE мышью на иконку 32×32 не навести, поэтому проверка идёт через слот.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GNpcRewardTip(
	TEXT("asda.Npc.RewardTip"),
	TEXT("asda.Npc.RewardTip <панель 0=обычная,1=выбор> <слот 0..5> — подсказка предмета награды (как наведение)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaNpcDialogSubsystem* S = DlgSub(W);
		if (!S || Args.Num() < 2)
		{
			UE_LOG(LogAsdaNpcDlg, Warning, TEXT("asda.Npc.RewardTip <панель 0|1> <слот 0..5>"));
			return;
		}
		S->ShowRewardSlotTip(FCString::Atoi(*Args[0]) != 0, FCString::Atoi(*Args[1]));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GNpcRewardScan(
	TEXT("asda.Npc.RewardScan"),
	TEXT("asda.Npc.RewardScan — перечислить ВСЕ страницы с наградой (сценарий/talkId/шаг/под-запись и состав)"),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		if (UAsdaNpcDialogSubsystem* S = DlgSub(W)) { UE_LOG(LogAsdaNpcDlg, Log, TEXT("%s"), *S->DumpRewardScan()); }
	}));
#endif // ASDA_DEV_CONSOLE

// ★ ЖИВОЕ ДОКАЗАТЕЛЬСТВО трёх правок 2026-08-22 (обводка / название задания / центр экрана).
//   Печатает то, что нельзя увидеть на снимке однозначно: контур КАЖДОЙ подписи, текст заголовка
//   и прямоугольник рамки против центра вьюпорта.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GNpcRewardDump(
	TEXT("asda.Npc.RewardDump"),
	TEXT("asda.Npc.RewardDump — живой разбор попапа 540: обводка каждой подписи, заголовок, центр"),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		if (UAsdaNpcDialogSubsystem* S = DlgSub(W)) { UE_LOG(LogAsdaNpcDlg, Log, TEXT("%s"), *S->DumpRewardWindow()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GNpcDialogDump(
	TEXT("asda.Npc.DialogDump"),
	TEXT("asda.Npc.DialogDump — живой разбор окна 570: эффективная видимость, подписи, коды кнопок"),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		if (UAsdaNpcDialogSubsystem* S = DlgSub(W)) { UE_LOG(LogAsdaNpcDlg, Log, TEXT("%s"), *S->DumpWindow()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GNpcDialogPage(
	TEXT("asda.Npc.DialogPage"),
	TEXT("asda.Npc.DialogPage <talkId> — перейти на страницу диалога (0 = закрыть, как в эталоне)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaNpcDialogSubsystem* S = DlgSub(W);
		if (!S || Args.Num() < 1) { return; }
		S->RenderPage(FCString::Atoi(*Args[0]));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GNpcDialogClick(
	TEXT("asda.Npc.DialogClick"),
	TEXT("asda.Npc.DialogClick <ctrlId> — нажать кнопку окна (575/576/577/578…580/581…585) тем же кодом, что мышь"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaNpcDialogSubsystem* S = DlgSub(W);
		if (!S || Args.Num() < 1) { return; }
		S->OnDialogButton(FCString::Atoi(*Args[0]));
	}));
#endif // ASDA_DEV_CONSOLE

// ★★★ ЖИВАЯ ПРОВЕРКА ПОДСКАЗКИ 599 БЕЗ СЕРВЕРА (дефект «крестик Минопио не закрывает окно»).
//   `Asda.Quest.AwaitMaps show` без сервера ничего не покажет: строка собирается из журнала 5047.
//   Эти три команды дают ту же цепочку на любых данных: показать → снять состояние → нажать
//   крестик РЕАЛЬНЫМ путём кнопки → снова снять состояние.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GNpcQuestInfoShow(
	TEXT("asda.Npc.QuestInfo"),
	TEXT("asda.Npc.QuestInfo <текст> — поднять баннер подсказки 599 с указанным текстом (проверка без сервера)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaNpcDialogSubsystem* S = DlgSub(W);
		if (!S) { return; }
		const FString Text = Args.Num() > 0 ? FString::Join(Args, TEXT(" ")) : TEXT("проверка крестика");
		S->ShowQuestInfoEx(Text, /*bMarkBusy*/ false, /*bMayDefer*/ false);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GNpcQuestInfoDump(
	TEXT("asda.Npc.QuestInfoDump"),
	TEXT("asda.Npc.QuestInfoDump — живой разбор подсказки 599: видимость окна и крестика, признаки, таймер"),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		if (UAsdaNpcDialogSubsystem* S = DlgSub(W)) { UE_LOG(LogAsdaNpcDlg, Log, TEXT("%s"), *S->DumpQuestInfo()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GNpcQuestInfoX(
	TEXT("asda.Npc.QuestInfoX"),
	TEXT("asda.Npc.QuestInfoX — нажать крестик QUEST_INFO_x (586) тем же путём, что и мышь (OnClicked кнопки)"),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		if (UAsdaNpcDialogSubsystem* S = DlgSub(W)) { S->PressQuestInfoClose(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GNpcDialogClose(
	TEXT("asda.Npc.DialogClose"),
	TEXT("asda.Npc.DialogClose — закрыть окно разговора"),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		if (UAsdaNpcDialogSubsystem* S = DlgSub(W)) { S->Close(); }
	}));
#endif // ASDA_DEV_CONSOLE

// ★ ЖИВАЯ ПРОВЕРКА «побежал → окно закрылось, выделение снялось» БЕЗ СЕРВЕРА.
//   Павна в PIE нет (его спавнит серверный 4003), поэтому ни клик по земле, ни WASD маршрут не
//   построят — `StartRoutedMove` выйдет на `!Ch`. Команда дёргает РОВНО ТУ ЖЕ общую точку
//   `NotifyPlayerStartedMoving`, в которую сходятся клик/мини-карта/клавиши, а не её копию:
//   иначе проверялась бы вторая реализация правила (закон `false-negatives-method`).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GNpcDialogMoveClose(
	TEXT("asda.Npc.DialogMoveClose"),
	TEXT("asda.Npc.DialogMoveClose — прогнать общую точку «игрок начал двигаться» (закрытие окна 570 + снятие выделения)"),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		UAsdaNpcDialogSubsystem* S = DlgSub(W);
		AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
		if (!S || !PC)
		{
			UE_LOG(LogAsdaNpcDlg, Warning, TEXT("[NPC-диалог] MoveClose: нет подсистемы/контроллера"));
			return;
		}
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] MoveClose ДО: окно открыто=%d, npcId=%d"),
			S->IsDialogOpen() ? 1 : 0, S->GetCurrentNpcId());
		PC->NotifyPlayerStartedMoving(TEXT("живая проверка движения"));
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] MoveClose ПОСЛЕ: окно открыто=%d, цель=%s"),
			S->IsDialogOpen() ? 1 : 0,
			PC->TargetActor.IsValid() ? *PC->TargetActor->GetName() : TEXT("нет"));
	}));
#endif // ASDA_DEV_CONSOLE

// ★★★ ЖИВАЯ ПРОВЕРКА ЭТАПА Э1 БЕЗ СЕРВЕРА (`asda.Npc.TalkNearest` / `asda.Npc.TalkZone`).
//   ЗАЧЕМ. Игровые NPC в порту появляются ТОЛЬКО по сетевому спавну (0xFAD), а запечённые в уровне
//   акторы — это каталог-превью: `AAsdaNpcActor::BeginPlay` их прячет и снимает коллизию. Значит
//   без живого сервера мышью по NPC не кликнуть. Но ветка клика читает у актора лишь `NpcId` и
//   позицию, поэтому её можно прогнать по ЗАПЕЧЁННОМУ актору — это ровно тот же код, что дёргает
//   мышь (`AAsdaPlayerController::StartNpcTalk`), без единой параллельной ветки «для тестов».
namespace
{
	AAsdaNpcActor* NearestNpc(UWorld* W, const FVector& From, float& OutDist)
	{
		AAsdaNpcActor* Best = nullptr;
		OutDist = TNumericLimits<float>::Max();
		if (!W) { return nullptr; }
		for (TActorIterator<AAsdaNpcActor> It(W); It; ++It)
		{
			AAsdaNpcActor* A = *It;
			if (!A || A->NpcId <= 0) { continue; }
			const float D = FVector::Dist2D(A->GetActorLocation(), From);
			if (D < OutDist) { OutDist = D; Best = A; }
		}
		return Best;
	}

	AAsdaPlayerController* Pc(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? Cast<AAsdaPlayerController>(GI->GetFirstLocalPlayerController()) : nullptr;
	}
}

// ★ Живая проверка РАЗГОВОРНОЙ АНИМАЦИИ NPC (порт `uiPanelSelectTab@0xF947A0`): состояние 0…9 на
//   ближайшего NPC. Тот же вход, что зовёт открытие окна, — своей ветки нет.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GNpcAnimState(
	TEXT("asda.Npc.AnimState"),
	TEXT("asda.Npc.AnimState <0..9> — поставить ближайшему NPC анимационное состояние (1 = «разговор»)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		AAsdaPlayerController* PC = Pc(W);
		APawn* P = PC ? PC->GetPawn() : nullptr;
		if (!W || Args.Num() < 1) { UE_LOG(LogAsdaNpcDlg, Warning, TEXT("asda.Npc.AnimState <0..9>")); return; }
		float D = 0.f;
		AAsdaNpcActor* N = NearestNpc(W, P ? P->GetActorLocation() : FVector::ZeroVector, D);
		if (!N) { UE_LOG(LogAsdaNpcDlg, Warning, TEXT("asda.Npc.AnimState: NPC на уровне нет")); return; }
		const int32 St = FCString::Atoi(*Args[0]);
		N->SetEntityAnimState(St);
		UE_LOG(LogAsdaNpcDlg, Log,
			TEXT("asda.Npc.AnimState: npcId=%d (тип-функции %d, дистанция %.0f) → состояние %d, фактически %d"),
			N->NpcId, N->TypeFunc, D, St, N->EntityAnimState);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GNpcTalkNearest(
	TEXT("asda.Npc.TalkNearest"),
	TEXT("asda.Npc.TalkNearest — прогнать ветку клика по БЛИЖАЙШЕМУ NPC уровня (тот же код, что мышь)"),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		AAsdaPlayerController* PC = Pc(W);
		APawn* P = PC ? PC->GetPawn() : nullptr;
		if (!PC || !P) { UE_LOG(LogAsdaNpcDlg, Warning, TEXT("asda.Npc.TalkNearest: нет игрока")); return; }
		float D = 0.f;
		AAsdaNpcActor* N = NearestNpc(W, P->GetActorLocation(), D);
		if (!N) { UE_LOG(LogAsdaNpcDlg, Warning, TEXT("asda.Npc.TalkNearest: NPC на уровне нет")); return; }
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("asda.Npc.TalkNearest: npcId=%d, дистанция %.0f, в зоне=%d"),
			N->NpcId, D, PC->IsInsideNpcTalkZone(N) ? 1 : 0);
		PC->StartNpcTalk(N);
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("после клика: отложенная цель=%d (renderId=%d), окно открыто=%d"),
			PC->IsNpcTalkPending() ? 1 : 0, PC->GetPendingNpcRenderId(),
			DlgSub(W) && DlgSub(W)->IsDialogOpen() ? 1 : 0);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GNpcTalkZone(
	TEXT("asda.Npc.TalkZone"),
	TEXT("asda.Npc.TalkZone — состояние зоны разговора и отложенной цели (проверка исправлений D5/D9)"),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		AAsdaPlayerController* PC = Pc(W);
		APawn* P = PC ? PC->GetPawn() : nullptr;
		if (!PC || !P) { UE_LOG(LogAsdaNpcDlg, Warning, TEXT("asda.Npc.TalkZone: нет игрока")); return; }
		float D = 0.f;
		AAsdaNpcActor* N = NearestNpc(W, P->GetActorLocation(), D);
		UE_LOG(LogAsdaNpcDlg, Log,
			TEXT("зона: ближайший npcId=%d дистанция %.0f в_зоне=%d | отложено=%d renderId=%d актор=%s | окно=%d"),
			N ? N->NpcId : -1, D, (N && PC->IsInsideNpcTalkZone(N)) ? 1 : 0,
			PC->IsNpcTalkPending() ? 1 : 0, PC->GetPendingNpcRenderId(),
			PC->GetPendingNpcActor() ? *PC->GetPendingNpcActor()->GetName() : TEXT("нет"),
			(DlgSub(W) && DlgSub(W)->IsDialogOpen()) ? 1 : 0);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GNpcZoneCalc(
	TEXT("asda.Npc.TalkZoneCalc"),
	TEXT("asda.Npc.TalkZoneCalc <npcId> — таблица «смещение от NPC → в зоне разговора?» (проверка порта bboxContainsPoint)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		AAsdaPlayerController* PC = Pc(W);
		if (!PC || Args.Num() < 1) { UE_LOG(LogAsdaNpcDlg, Warning, TEXT("asda.Npc.TalkZoneCalc: нет контроллера/аргумента")); return; }
		const int32 Id = FCString::Atoi(*Args[0]);
		AAsdaNpcActor* N = nullptr;
		for (TActorIterator<AAsdaNpcActor> It(W); It; ++It) { if (*It && (*It)->NpcId == Id) { N = *It; break; } }
		if (!N) { UE_LOG(LogAsdaNpcDlg, Warning, TEXT("asda.Npc.TalkZoneCalc: npcId=%d на уровне нет"), Id); return; }
		const FVector L = N->GetActorLocation();
		// Проверяем ключевые точки: центр, границы эталонного ящика ±50, границы расширенного ±150,
		//   чуть за ними и по вертикали. Ожидаемое — в комментарии у каждой строки.
		const float Probe[] = { 0.f, 49.f, 51.f, 149.f, 151.f, 300.f };
		for (float P : Probe)
		{
			const bool bX = PC->IsPointInNpcTalkZone(N, L + FVector(P, 0.f, 0.f));
			const bool bY = PC->IsPointInNpcTalkZone(N, L + FVector(0.f, P, 0.f));
			const bool bD = PC->IsPointInNpcTalkZone(N, L + FVector(P, P, 0.f));   // угол квадрата
			UE_LOG(LogAsdaNpcDlg, Log, TEXT("зона npcId=%d смещение %.0f: по X=%d по Y=%d по диагонали=%d"),
				Id, P, bX ? 1 : 0, bY ? 1 : 0, bD ? 1 : 0);
		}
		const float Up[] = { -101.f, -99.f, 100.f, 5000.f };
		for (float Z : Up)
		{
			UE_LOG(LogAsdaNpcDlg, Log, TEXT("зона npcId=%d по высоте %+.0f: %d"),
				Id, Z, PC->IsPointInNpcTalkZone(N, L + FVector(0.f, 0.f, Z)) ? 1 : 0);
		}
	}));
#endif // ASDA_DEV_CONSOLE
