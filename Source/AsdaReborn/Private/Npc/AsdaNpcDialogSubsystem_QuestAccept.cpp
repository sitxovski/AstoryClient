// Copyright (c) 2026 @sitxovski (Discord)
// AsdaNpcDialogSubsystem_QuestAccept.cpp — ★ R3-c 12.11.2026 (разрез AsdaNpcDialogSubsystem_Popups.cpp, дословный перенос): окно принятия задания: виджет, раскладка, плитка награды, показ, дамп.
//   Общее для частей — в AsdaNpcDialogSubsystemInternal.h; шапка и реверс — в AsdaNpcDialogSubsystem_Popups.cpp.
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "AsdaDataTableCache.h"
#include "Quest/AsdaQuestGuideTarget.h"   // ★ 31.10.2026: ключ asda.Npc.CellsBeyond10 (ячейки NPC сверх 10, патч сервера N22)
#include "Text/AsdaTextSubsystem.h"
#include "Login/AsdaText.h"                 // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): FAsdaText::Loc — подписи отказов по id
#include "UI/AsdaNotice.h"            // ★ 27.09 (clan_btn_6099_fix): MsgBoxLoc — окна нехватки/отказа обмена, как у эталона
#include "UI/AsdaCommunityFriendSubsystem.h"   // AsdaCommunityUi::ShownVisibilityFor — общая точка видимости узла
#include "UI/AsdaBoosterPanelSubsystem.h"   // ★ гейт 13101: «открыто окно бустера» (эталонное поле +0x3F8)
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
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
#include "UI/AsdaMarkup.h"          // UAsdaRichText::ApplyFormatted — теги <FC=…> в баннере 599
#include "UI/AsdaTipPanel.h"             // общая точка пересборки 9-slice (порт uiPanel9SliceLayout) + EAsdaTipOwner
#include "UI/AsdaItemTooltipSubsystem.h" // ОБЩАЯ точка подсказок предмета (второй копии билдеров нет)
#include "Engine/Engine.h"               // GEngine->GameViewport: размер вьюпорта для проверки центра
#include "Engine/GameViewportClient.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "Quest/AsdaQuestMarkerSubsystem.h"
#include "Quest/AsdaQuestLogSubsystem.h"   // ОБЩАЯ точка `BuildRewardLines` — разбор RewardTable.BIN (второй копии нет)
#include "UI/AsdaIconCellGrid.h"   // общая точка: прозрачные клик-кнопки поверх JUI-ячеек (строки выбора награды)
#include "UI/AsdaInventorySubsystem.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaCraftWindowSubsystem.h"   // ★ 30009/30088/30010 — окно создания снаряжения 76000 (ремесло, волна В2)
#include "UI/AsdaCraftMakeWindowSubsystem.h"   // ★ хвост смерти entity_char.c:16910 — сброс окна изготовления 77500 (В8)
#include "UI/AsdaHuntReportSubsystem.h"         // ★ волна 2-Б: 30068 — окно «Доклад об охоте» 56450
#include "UI/AsdaTownWarSubsystem.h"            // ★ волна 2-Г: 30076 — окно налога города 79300
#include "TimerManager.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "UI/AsdaDisassemblySubsystem.h"    // ★ волна 2-А: окно разборки 32000 видно ⇒ разговор молча не открывается
#include "Text/AsdaLang.h"                  // [ПОРТ] 09.10.2026 (мультиязычность): заголовок/кнопки/реплики сценария на языке сессии
#include "AsdaNpcDialogSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaNpcDialogSubsystem*.cpp (LogAsdaNpcDlg, чистые хелперы)
#include "Quest/AsdaBoardQuestSubsystem.h"   // ★ 28.10.2026: попап 540 «режим доски» (`uiQuestWin_openReward`), сброс ждущего сдачи
#include "AsdaNpcDialogSubsystemInternal.h"   // ★ R3-c 12.11.2026: общее для частей AsdaNpcDialogSubsystem_Popups*.cpp

// Окно ПРИЁМА задания (JUI 750 `QUEST_Confirm_Win`) — показывается на ответ 5044, `resultCode == 0`.
//   ★ Это НЕ 540: другой скин (`Popup_Win`, тёмная рамка), другой источник данных
//   (`RewardTable.BIN` по паре (questNo, stage)) и другая кнопка (780 «ОК» — только закрыть).
static const TCHAR* kAcWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Quest/WBP_QUEST_Confirm_Win.WBP_QUEST_Confirm_Win_C");

static constexpr int32 kLocOkButton     = 2288;       // «ОК» — подпись кнопки 780 окна 750

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ОКНО ПРИЁМА ЗАДАНИЯ `QUEST_Confirm_Win` (JUI 750) — порт `CQuest_PopulateRewardWindow@0xC66A20`
//
//  ЧТО ДЕЛАЕТ ЭТАЛОН (`quest.c:4226…4410`), построчно:
//    1. `SetVisible(confirmWindow, 1)` + `CClass_MapInsert(g_pWindowMgr, confirmWindow)` — показать
//       и поднять на передний план (окно разговора NPC при этом ОСТАЁТСЯ открытым);
//    2. `questRewardPanelResize` — высота окна: 354×214 / 354×250 / 354×390 (таблица ниже);
//    3. `SetText(confirmTextTop, Str)` — строка 20197 «Получено задание [ss1].»;
//    4. цикл `i = 0…3` по ГАРАНТИРОВАННЫМ плиткам: вид 0 деньги, 1 опыт, 2 предмет;
//       `if (тип < 0) SetVisible(плитка, 0);` — пустая ПРЯЧЕТСЯ, соседние НЕ сдвигаются;
//    5. цикл `i = 4…7` по плиткам «на выбор» (только вид 2), заодно копится `hasExtraReward`;
//    6. `SetVisible(pConfirmTextT2, hasExtraReward)` — заголовок «Дополнительная награда».
//
//  Имя награды эталон обрезает `textTruncateWithEllipsis(g_pCommunityWnd, имя, 104)` под ширину
//  подписи 110 px. В порту обрезкой занимается сам Slate (`NoImage_Clip` → `ETextOverflowPolicy`),
//  своей копии измерителя здесь не заводим.
//
//  ★ КНОПКА В ОКНЕ ОДНА — `QUEST_Confirm_OK` (780). Соседние `Btn_LBase`/`Btn_RBase` и значок
//    `WkBtn_IconCheck` в JUI объявлены с `ID = ""` / `IDNum = 4294967295` и имеют скин
//    `Type = "StaticWindow"` — это КАРТИНКИ подложки кнопочной полосы, а не кнопки. Обработчик 780
//    (`questPanel_HandleUICommand@0xC5C3B0`, `quest.c:1704`) делает ровно одно: прячет окно.
//
//  ★ ПОДСКАЗОК ПО НАВЕДЕНИЮ ЗДЕСЬ НЕТ. Контролы 752…779 в эталоне встречаются ТОЛЬКО в
//    инициализации (`quest.c:1433…1454`) — ни один обработчик событий их не адресует, в отличие от
//    строк попапа 540. Выдумывать подсказку не будем (закон «запрет выдуманного UI»).
// ════════════════════════════════════════════════════════════════════════════════════════════════
bool UAsdaNpcDialogSubsystem::EnsureQuestAcceptWindow()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	if (AcWin && WM->IsWindowLive(AcWin)) { return true; }
	AcRelays.Reset();   // ★ 31.10.2026: окно 750 пересоздаётся — реле прежнего дерева мертвы, якорь только этого окна

	AcFrame.Reset(); AcDrag = nullptr; AcTopText = nullptr;
	AcT1Grp = nullptr; AcT2Grp = nullptr; AcT1Title = nullptr; AcT2Title = nullptr;
	AcT1Back.Reset(); AcT1Name.Reset(); AcT1Icon.Reset(); AcT1Qty.Reset();
	AcT2Back.Reset(); AcT2Name.Reset(); AcT2Icon.Reset(); AcT2Qty.Reset();
	AcOkBut = nullptr; AcBtnGrp = nullptr; AcOkText = nullptr;

	// Z выше попапа наград 540: эталон поднимает окно 750 на самый верх (`CClass_MapInsert`).
	AcWin = WM->OpenWindowByPath(kAcWinId, kAcWbp, kDlgZ + 9, ESlateVisibility::SelfHitTestInvisible);
	if (!AcWin)
	{
		UE_LOG(LogAsdaNpcDlg, Error, TEXT("[NPC-диалог] WBP_QUEST_Confirm_Win не загрузился (%s)"), kAcWbp);
		return false;
	}
	WM->SetWindowVisible(kAcWinId, false);

	for (int32 i = 0; i < 9; ++i)
	{
		AcFrame.Add(Cast<UImage>(MqFindExact(AcWin, *FString::Printf(TEXT("QUEST_Confirm_Win_0_%d"), i))));
	}
	AcDrag    = MqFindExact(AcWin, TEXT("QUEST_Confirm_Win_0_DRAG"));
	AcTopText = Cast<UTextBlock>(MqFindExact(AcWin, TEXT("QUEST_Confirm_Text_Top")));
	AcT1Grp   = MqFindExact(AcWin, TEXT("QUEST_Confirm_Text_T1_grp"));
	AcT2Grp   = MqFindExact(AcWin, TEXT("QUEST_Confirm_Text_T2_grp"));
	AcT1Title = Cast<UTextBlock>(MqFindExact(AcWin, TEXT("QUEST_Confirm_Text_T1")));
	AcT2Title = Cast<UTextBlock>(MqFindExact(AcWin, TEXT("QUEST_Confirm_Text_T2")));
	for (int32 i = 0; i < 4; ++i)
	{
		AcT1Back.Add(MqFindExact(AcWin, *FString::Printf(TEXT("QUEST_Confirm_Text_T1_G%d_grp"), i + 1)));
		AcT1Name.Add(Cast<UTextBlock>(MqFindExact(AcWin, *FString::Printf(TEXT("QUEST_Confirm_Text_T1_TE%d"), i + 1))));
		// Картинка иконки — `…_MARK<k>_<n>_0`, где `<n>` даёт импортёр; ищем по ПРЕФИКСУ
		//   (общая точка `AsdaUi::FindNodeByPrefix`, ею же берутся иконки окна 540).
		AcT1Icon.Add(Cast<UImage>(AsdaUi::FindNodeByPrefix(AcWin, *FString::Printf(TEXT("QUEST_Confirm_Text_T1_MARK%d_"), i + 1))));
		AcT1Qty.Add(Cast<UTextBlock>(MqFindExact(AcWin, *FString::Printf(TEXT("QUEST_Confirm_Text_T1_MARK%d"), i + 1))));
		AcT2Back.Add(MqFindExact(AcWin, *FString::Printf(TEXT("QUEST_Confirm_Text_T2_G%d_grp"), i + 1)));
		AcT2Name.Add(Cast<UTextBlock>(MqFindExact(AcWin, *FString::Printf(TEXT("QUEST_Confirm_Text_T2_TE%d"), i + 1))));
		AcT2Icon.Add(Cast<UImage>(AsdaUi::FindNodeByPrefix(AcWin, *FString::Printf(TEXT("QUEST_Confirm_Text_T2_MARK%d_"), i + 1))));
		AcT2Qty.Add(Cast<UTextBlock>(MqFindExact(AcWin, *FString::Printf(TEXT("QUEST_Confirm_Text_T2_MARK%d"), i + 1))));
	}
	AcOkBut  = Cast<UButton>(MqFindExact(AcWin, TEXT("QUEST_Confirm_OK")));
	AcOkText = Cast<UTextBlock>(MqFindExact(AcWin, TEXT("QUEST_Confirm_OK_Text")));
	AcBtnGrp = MqFindExact(AcWin, TEXT("Btn_CBase_grp"));

	if (AcOkBut)
	{
		AsdaUi::BindButton(this, AcOkBut, 780, [this](int32)
		{
			// ★ Волна В4 рыбалки (08.10.2026): 780 — кнопка окна `CQuest`, событие 66561 → при рыбалке поглощено (g:285554).
			if (AsdaQuestEpisodeFishingGate(GetGameInstance(), 780)) { return; }
			HideQuestAcceptWindow();   // эталон: `SetVisible(окно, 0)`
		}, PopupRelayAnchor(AcRelays));   // ★ U20: реле клика — общая точка
	}
	// Статические подписи ставит КОД (в `.JUI` там корейские рыбы) — эталон делает это один раз в
	//   инициализации (`quest.c:1424/1430/1459`).
	if (UAsdaTextSubsystem* TS = GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>())
	{
		if (AcT1Title) { AcT1Title->SetText(FText::FromString(TS->GetTextById(kLocRewardBasic))); }
		if (AcT2Title) { AcT2Title->SetText(FText::FromString(TS->GetTextById(kLocRewardSelect))); }
		if (AcOkText)  { AcOkText->SetText(FText::FromString(TS->GetTextById(kLocOkButton))); }
	}
	// ★ ОБВОДКА. В `CQuest_PopulateRewardWindow` `CClass_SetField926(…, 0x800000)` зовётся ровно
	//   ВОСЕМЬ раз — контролам 762/764/766/768 и 772/774/776/778 (`quest.c:1442` и `1453`), то есть
	//   ТОЛЬКО подписям количества поверх иконок. Строке 751, заголовкам 760/770 и подписям
	//   761…777 обводка НЕ ставится. Снимаем в рантайме через ОБЩУЮ точку (её же зовёт окно 540),
	//   потому что обводку запекает импортёр JUI и переимпорт вернул бы её (грабля
	//   `tools-no-import-side-effects`).
	{
			// ★★★ УБРАНО 2026-08-27: обводка расставлена по данным оригинала В САМИХ АССЕТАХ
			//   (шаг `Tools/asda_ui_text_style926.py`, таблица `Tools/data/text_style926.tsv` —
			//   реверс поля 926). ★ Прежний ручной разбор «оставить обводку у подписей количества»
			//   ПОЛНОСТЬЮ СОВПАЛ с данными: всем этим контролам обводка положена безусловно.
			//   Рантайм-простановка стала избыточной, а после отключения ошибочного правила
			//   импортёра — и вредной (перезаписала бы правильный ассет).
		(void)AcWin;
	}

	WM->RegisterWindowCloser(kAcWinId, FSimpleDelegate::CreateUObject(this, &UAsdaNpcDialogSubsystem::HideQuestAcceptWindow));

	int32 HaveFrame = 0;
	for (const TObjectPtr<UImage>& Im : AcFrame) { if (Im) { ++HaveFrame; } }
	int32 HaveTiles = 0;
	for (int32 i = 0; i < 4; ++i)
	{
		if (AcT1Back[i] && AcT1Name[i] && AcT1Icon[i]) { ++HaveTiles; }
		if (AcT2Back[i] && AcT2Name[i] && AcT2Icon[i]) { ++HaveTiles; }
	}
	UE_LOG(LogAsdaNpcDlg, Log,
		TEXT("[NPC-диалог] QUEST_Confirm_Win (750) разобран: рамка %d/9, ручка=%d, строка 751=%d, ")
		TEXT("заголовки 760/770=%d/%d, плиток собрано %d/8, кнопка 780=%d"),
		HaveFrame, AcDrag ? 1 : 0, AcTopText ? 1 : 0, AcT1Title ? 1 : 0, AcT2Title ? 1 : 0,
		HaveTiles, AcOkBut ? 1 : 0);
	return true;
}

void UAsdaNpcDialogSubsystem::HideQuestAcceptWindow()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kAcWinId, false);
	}
}

// Порт `questRewardPanelResize@0xC676E0` (`quest.c:4415`). Ширина всегда 354, высота — одна из трёх;
//   после смены размера эталон зовёт `uiPanel9SliceLayout` (у нас — `UAsdaTipPanel::Resize9Slice`).
//   Кнопочная полоса привязана к НИЗУ окна (`AlignYPos = "8 0 0"`, `Origin = "112 −38"`), поэтому
//   её надо сдвинуть на ту же дельту — импортёр запёк положение для высоты 390.
void UAsdaNpcDialogSubsystem::RelayoutQuestAcceptWindow(int32 Height)
{
	UCanvasPanelSlot* DragSlot = AcDrag ? AsdaUi::SlotOf(AcDrag) : nullptr;
	if (!DragSlot) { return; }
	int32 HaveFrame = 0;
	for (const TObjectPtr<UImage>& Im : AcFrame) { if (Im && AsdaUi::SlotOf(Im)) { ++HaveFrame; } }
	if (HaveFrame != 9)
	{
		UE_LOG(LogAsdaNpcDlg, Warning,
			TEXT("[NPC-диалог/приём] рамка 750 неполная (планов %d из 9) — высота окна НЕ пересчитана"), HaveFrame);
		return;
	}
	const FMargin Cur = DragSlot->GetOffsets();
	const float L = Cur.Left, T = Cur.Top, W = Cur.Right, HCur = Cur.Bottom;
	if (FMath::IsNearlyEqual(HCur, (float)Height)) { return; }

	UAsdaTipPanel::Resize9Slice(AcFrame, FVector2D(W, (float)Height), FVector2D(L, T));
	DragSlot->SetOffsets(FMargin(L, T, W, (float)Height));

	const float Shift = (float)Height - HCur;
	if (UCanvasPanelSlot* S = AcBtnGrp ? AsdaUi::SlotOf(AcBtnGrp) : nullptr)
	{
		const FMargin O = S->GetOffsets();
		S->SetOffsets(FMargin(O.Left, O.Top + Shift, O.Right, O.Bottom));
	}
	UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог/приём] раскладка 750: высота %.0f → %d (Δ кнопки %.0f)"),
		HCur, Height, Shift);
}

// Порт одной итерации циклов `i = 0…3` / `i = 4…7` из `CQuest_PopulateRewardWindow`.
void UAsdaNpcDialogSubsystem::ApplyAcceptRewardTile(bool bChoicePanel, int32 TileIdx, const FAsdaQuestRewardLine& Line)
{
	TArray<TObjectPtr<UWidget>>&    Backs = bChoicePanel ? AcT2Back : AcT1Back;
	TArray<TObjectPtr<UTextBlock>>& Names = bChoicePanel ? AcT2Name : AcT1Name;
	TArray<TObjectPtr<UImage>>&     Icons = bChoicePanel ? AcT2Icon : AcT1Icon;
	TArray<TObjectPtr<UTextBlock>>& Qtys  = bChoicePanel ? AcT2Qty  : AcT1Qty;
	if (!Backs.IsValidIndex(TileIdx)) { return; }

	// `if (тип < 0) SetVisible(плитка, 0);` — пустая ячейка ПРЯЧЕТСЯ, соседние не сдвигаются.
	if (Line.Kind < 0)
	{
		SetVis(Backs[TileIdx], false);
		return;
	}

	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT  = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaTextSubsystem*      TS  = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;

	FString TileName, QtyText, FlatIcon;
	int32   IconItem = -1;
	switch (Line.Kind)
	{
	case 0:   // ДЕНЬГИ — имя и иконка берутся у ПРЕДМЕТА 20551 «Золото» (эталон, ветка `else`)
		IconItem = kMoneyItemCode;
		TileName = FString::Printf(TEXT("%d %s"), Line.Amount, IT ? *IT->GetItemName(kMoneyItemCode) : TEXT(""));
		break;
	case 1:   // ОПЫТ — «%d <нотис 6010>» + ПЛОСКАЯ текстура `exp2` (в эталоне зашита строкой)
		FlatIcon = kIconExp;
		TileName = FString::Printf(TEXT("%d %s"), Line.Amount, TS ? *TS->GetTextById(kLocRewardExp) : TEXT(""));
		break;
	default:  // 2 — ПРЕДМЕТ / САМОЦВЕТ / особый предмет: имя из таблиц, количество — на иконке
		IconItem = Line.ItemCode;
		TileName = IT ? IT->GetItemName(Line.ItemCode) : FString();
		if (TileName.IsEmpty()) { TileName = UAsdaTextSubsystem::FormatPrintf(FAsdaText::Loc(990448), { FString::FromInt(Line.ItemCode) }); }   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): «код %d»
		QtyText = FString::Printf(TEXT("%d"), Line.Count);   // эталон печатает количество КАК ЕСТЬ
		break;
	}

	if (Names.IsValidIndex(TileIdx) && Names[TileIdx])
	{
		Names[TileIdx]->SetText(FText::FromString(TileName));
		SetVis(Names[TileIdx], true);
	}
	if (Icons.IsValidIndex(TileIdx) && Icons[TileIdx])
	{
		// Обе ветки — ОБЩИЕ точки порта, своих копий здесь нет (дедуп Д2, та же пара, что в 540):
		//   атласная иконка предмета — `UAsdaInventorySubsystem::ApplyIconToImage`,
		//   плоская текстура (`exp2`) — `AsdaUi::ApplyFlatJuiIcon`.
		bool bDrawn = false;
		if (IconItem > 0 && Inv) { bDrawn = Inv->ApplyIconToImage(Icons[TileIdx], IconItem, 32.f); }
		else                     { bDrawn = AsdaUi::ApplyFlatJuiIcon(Icons[TileIdx], FlatIcon, 32.f); }
		if (!bDrawn) { AsdaUi::ClearImageBrush(Icons[TileIdx]); }   // дедуп Д8
		SetVis(Icons[TileIdx], bDrawn);
	}
	if (Qtys.IsValidIndex(TileIdx) && Qtys[TileIdx])
	{
		Qtys[TileIdx]->SetText(FText::FromString(QtyText));
		SetVis(Qtys[TileIdx], !QtyText.IsEmpty());
	}
	SetVis(Backs[TileIdx], true);
}

// Числовой дамп окна 750 — «проверять числами, а не на глаз». Печатает РЕАЛЬНУЮ (эффективную)
//   видимость каждой плитки, текущую высоту рамки и подписи, то есть ровно то, что видит игрок.
FString UAsdaNpcDialogSubsystem::DumpQuestAcceptWindow() const
{
	if (!AcWin) { return TEXT("[750] окно не создано"); }
	const UCanvasPanelSlot* DragSlot = AcDrag ? AsdaUi::SlotOf(AcDrag) : nullptr;
	const FMargin M = DragSlot ? DragSlot->GetOffsets() : FMargin();
	FString Out = FString::Printf(
		TEXT("[750] окно видимо=%d, рамка (%.0f,%.0f) %.0fx%.0f, строка 751=«%s»"),
		IsEffectivelyVisible(AcWin) ? 1 : 0, M.Left, M.Top, M.Right, M.Bottom,
		AcTopText ? *AcTopText->GetText().ToString() : TEXT("нет"));
	auto Row = [&Out](const TCHAR* Tag, const TArray<TObjectPtr<UWidget>>& Backs,
		const TArray<TObjectPtr<UTextBlock>>& Names, const TArray<TObjectPtr<UTextBlock>>& Qtys,
		const TArray<TObjectPtr<UImage>>& Icons)
	{
		for (int32 i = 0; i < Backs.Num(); ++i)
		{
			const bool bVis = Backs[i] && IsEffectivelyVisible(Backs[i]);
			Out += FString::Printf(TEXT("\n  %s%d вид=%d текст=«%s» кол-во=«%s» иконка=%d"),
				Tag, i + 1, bVis ? 1 : 0,
				Names.IsValidIndex(i) && Names[i] ? *Names[i]->GetText().ToString() : TEXT(""),
				Qtys.IsValidIndex(i) && Qtys[i] ? *Qtys[i]->GetText().ToString() : TEXT(""),
				(Icons.IsValidIndex(i) && Icons[i] && IsEffectivelyVisible(Icons[i])) ? 1 : 0);
		}
	};
	Out += FString::Printf(TEXT("\n  заголовок 760 «%s» вид=%d; заголовок 770 «%s» вид=%d"),
		AcT1Title ? *AcT1Title->GetText().ToString() : TEXT(""), (AcT1Grp && IsEffectivelyVisible(AcT1Grp)) ? 1 : 0,
		AcT2Title ? *AcT2Title->GetText().ToString() : TEXT(""), (AcT2Grp && IsEffectivelyVisible(AcT2Grp)) ? 1 : 0);
	Row(TEXT("T1_G"), AcT1Back, AcT1Name, AcT1Qty, AcT1Icon);
	Row(TEXT("T2_G"), AcT2Back, AcT2Name, AcT2Qty, AcT2Icon);
	Out += FString::Printf(TEXT("\n  кнопка 780 «%s» вид=%d"),
		AcOkText ? *AcOkText->GetText().ToString() : TEXT(""),
		(AcOkBut && IsEffectivelyVisible(AcOkBut)) ? 1 : 0);
	return Out;
}

bool UAsdaNpcDialogSubsystem::ShowQuestAcceptWindow(int32 EpisodeId, int32 QuestNo, int32 Stage)
{
	if (!EnsureQuestAcceptWindow()) { return false; }
	UGameInstance* GI = GetGameInstance();

	// ── набор наград: ОБЩАЯ точка `UAsdaQuestLogSubsystem::BuildRewardLines` (порт
	//    `questBuildRewardSet@0xC663A0`). Своего разбора `RewardTable.BIN` здесь нет.
	TArray<FAsdaQuestRewardLine> Basic, Choice;
	const bool bHaveSet = UAsdaQuestLogSubsystem::BuildRewardLines(QuestNo, Stage, Basic, Choice);
	if (!bHaveSet)
	{
		// Эталон при промахе набора окно ВСЁ РАВНО показывает (`CQuest_PopulateRewardWindow` не
		//   спрашивает `rewardSetValid`) — просто все плитки прячутся по `тип < 0`. Молчать нельзя.
		UE_LOG(LogAsdaNpcDlg, Warning,
			TEXT("[NPC-диалог/приём] в RewardTable нет пары (квест %d, шаг %d) и (квест %d, шаг %d) — ")
			TEXT("окно 750 будет показано БЕЗ наград"), QuestNo, Stage, QuestNo, Stage + 1);
		Basic.SetNum(4);
	}

	// ── строка 751: нотис 20197 «Получено задание [ss1].», слот 0 = название эпизода ──
	//    Резолвер названия в порту ОДИН — `UAsdaQuestMarkerSubsystem` (им же пользуются облачко
	//    2406, окно 540 и нотис при сдаче); своей копии поиска по таблице эпизодов не заводим.
	{
		UAsdaQuestMarkerSubsystem* QM = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
		FString EpTitle = QM ? QM->EpisodeTitleByEpisodeId(EpisodeId) : FString();
		// ★ 28.10.2026 ДОСКА ОБЪЯВЛЕНИЙ: эталон (`CQuest_RecvPacket`, quest.c:7716-7731) при промахе эпизода берёт имя задания доски
		//   (`mapObjFindEntryById(g_pQuestCraftWnd, тело+11)` → запись `+12`); нет и её — красная строка «Quest Error» в чат,
		//   ONEBUT 6192 (ключ (0, 24)) и ОКНА НЕТ. Порт применяет эту ветку к номерам доски 20000…30000 (у прочих — прежний
		//   второй заход по ключу квеста ниже).
		if (EpTitle.IsEmpty() && QuestNo >= 20000 && QuestNo <= 30000)
		{
			UAsdaBoardQuestSubsystem* Board = GI ? GI->GetSubsystem<UAsdaBoardQuestSubsystem>() : nullptr;
			if (Board && Board->FindQuest(QuestNo))
			{
				EpTitle = UAsdaBoardQuestSubsystem::QuestName(QuestNo);
			}
			else
			{
				AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, TEXT("Quest Error"), FColor(0xFFFF0000u), 10000.f);
				AsdaNotice::MsgBoxLoc(GI, EAsdaMsgCategory::Quest, 6192, AsdaLoc::kBtnOk, 24);
				UE_LOG(LogAsdaNpcDlg, Warning, TEXT("[NPC-диалог/приём] 5044: задания доски %d нет в таблице — «Quest Error», окна 750 нет"), QuestNo);
				return false;
			}
		}
		if (EpTitle.IsEmpty() && QM && QuestNo >= 0)
		{
			// Эталонный второй заход: `mapObjFindEntryById(g_pQuestCraftWnd, тело+11)` — поиск по
			//   ключу квеста; у нас обе таблицы это одна и та же `DT_EpisodeTable` (поле `+236`).
			EpTitle = QM->EpisodeTitleByQuestKey(QuestNo);
			UE_LOG(LogAsdaNpcDlg, Warning,
				TEXT("[NPC-диалог/приём] названия эпизода %d в DT_EpisodeTable нет; по ключу квеста %d — «%s»"),
				EpisodeId, QuestNo, *EpTitle);
		}
		if (AcTopText && GI)
		{
			if (UAsdaTextSubsystem* TS = GI->GetSubsystem<UAsdaTextSubsystem>())
			{
				FString Line = TS->FormatById(kLocRewardHeader, { EpTitle }, {});
				if (Line.IsEmpty()) { Line = FAsdaLang::MissingTag(kLocRewardHeader); }   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка
				AcTopText->SetText(FText::FromString(Line));
			}
		}
	}

	// ── плитки ──
	for (int32 i = 0; i < 4; ++i)
	{
		ApplyAcceptRewardTile(/*bChoicePanel*/ false, i, Basic[i]);
		ApplyAcceptRewardTile(/*bChoicePanel*/ true,  i, Choice.IsValidIndex(i) ? Choice[i] : FAsdaQuestRewardLine());
	}
	// `SetVisible(pConfirmTextT2, hasExtraReward)` — заголовок 770. Вся группа T2 в WBP держит и
	//   заголовок, и четыре плитки; при пустом выборе она уезжает за низ укороченного окна, поэтому
	//   гасим её целиком — плитки внутри уже погашены отдельно (эффект тот же, что у эталона).
	SetVis(AcT2Grp, Choice.Num() > 0);
	SetVis(AcT1Grp, true);
	if (AcT2Title) { SetVis(AcT2Title, Choice.Num() > 0); }

	// ── высота окна: порт `questRewardPanelResize@0xC676E0` ──
	//    `тип[4] == 2` (есть награда на выбор) → 390; иначе `тип[2] == 2` (есть предмет в ячейке 2)
	//    → 250; иначе 214. Ячейки 2 и 3 — это ВТОРОЙ РЯД плиток (в `.JUI` они стоят на y = 71
	//    против y = 25 у первого), то есть эталонное правило означает «нужен ли второй ряд».
	//
	//    ★ ЗАПИСАНО ЧЕРЕЗ ЗАНЯТОСТЬ ВТОРОГО РЯДА, а не через `тип[2] == 2`, по двум причинам:
	//      (1) после схлопывания пустых ячеек (`CollapseRewardCells`) во втором ряду может
	//          оказаться не только предмет — правило «есть ли что рисовать во втором ряду»
	//          не зависит от того, какой вид туда переехал; и наоборот, ряд может ОПУСТЕТЬ,
	//          и тогда окно обязано стать ниже — это тот же самый признак;
	//      (2) ★ ДЕФЕКТ ОРИГИНАЛА (исправлен и помечен, закон `fix-original-defects`): при
	//          `ItemCode1 == -1`, `ItemCode2 > 0` эталон занимает ТОЛЬКО ячейку 3, `тип[2] == 2`
	//          у него ложно, окно остаётся 214 — и реальная награда оказывается ниже нижнего края
	//          окна, то есть НЕ ВИДНА. В данных это три пары: квесты 77/79/81 шаг 2
	//          (предметы 7743/7744/7745). Считаем по обеим ячейкам — награда видна.
	const bool bHasChoice = (Choice.Num() > 0);
	const bool bSecondRow = (Basic[2].Kind >= 0) || (Basic[3].Kind >= 0);
	const int32 Height = bHasChoice ? 390 : (bSecondRow ? 250 : 214);
	RelayoutQuestAcceptWindow(Height);

	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		// В `.JUI` окно объявлено `AlignXPos = "16 42 50"` / `AlignYPos = "32 42 50"` — режимы 16/32
		//   означают «контрол ЦЕНТРИРУЕТСЯ на точке». Центрируем ОБЩЕЙ точкой и уже ПОСЛЕ пересчёта
		//   высоты, поэтому окно любой из трёх высот стоит по центру на любом разрешении.
		// ★ 08.10.2026 [ОРИГИНАЛ] (window_default_pos.md §6 п.5): «16 42 50 / 32 42 50» — центр окна в ТОЧКЕ 42 % ширины и
		//   50 % высоты экрана (`uiCalcAnchoredScreenPos@0x12DFBB0`: x = trunc(42 %·W + Origin.x) − (w>>1), Origin 0), а не центр
		//   экрана: прежнее центрирование сдвигало окно на +82 px при 1024 (+154 при 1920). Высота — после пересчёта выше, как
		//   у эталона (пересчёт каждый кадр по живому размеру). Откат: `asda.UI.JuiDefaultPos 0` — прежний центр экрана.
		if (UAsdaWindowSubsystem::UseOriginalDefaultPos())
		{
			WM->PlaceWindowKeepingSavedOffset(kAcWinId, NAME_None, AcDrag, [](const FVector2D& V, const FVector2D& F)
			{
				return FVector2D(FMath::TruncToDouble(0.42 * V.X) - (FMath::TruncToInt32(F.X) >> 1),
					FMath::TruncToDouble(0.50 * V.Y) - (FMath::TruncToInt32(F.Y) >> 1));
			});
		}
		else { WM->CenterWindowOnScreen(AcWin, AcDrag); }
		WM->SetWindowVisible(kAcWinId, true);
		WM->BringWindowToFront(kAcWinId);   // эталон `CClass_MapInsert(g_pWindowMgr, confirmWindow)`
	}

	// ★ После схлопывания нулевых ячеек номер плитки роль награды НЕ задаёт, поэтому лог печатает
	//   пары «вид→значение» по местам, а не подписи «деньги/опыт/предмет» у фиксированных индексов.
	int32 ShownBasic = 0;
	FString Tiles;
	for (int32 i = 0; i < 4; ++i)
	{
		if (Basic[i].Kind >= 0) { ++ShownBasic; }
		static const TCHAR* KindName[3] = { TEXT("деньги"), TEXT("опыт"), TEXT("предмет") };
		Tiles += (i > 0) ? TEXT(", ") : TEXT("");
		Tiles += (Basic[i].Kind < 0 || Basic[i].Kind > 2)
			? FString::Printf(TEXT("G%d пусто"), i + 1)
			: FString::Printf(TEXT("G%d %s=%d"), i + 1, KindName[Basic[i].Kind],
				(Basic[i].Kind == 2) ? Basic[i].ItemCode : Basic[i].Amount);
	}
	UE_LOG(LogAsdaNpcDlg, Log,
		TEXT("[NPC-диалог/приём] окно 750 показано: эпизод %d, квест %d шаг %d, набор=%d, ")
		TEXT("плиток базовых %d/4 (%s), на выбор %d ⇒ высота %d"),
		EpisodeId, QuestNo, Stage, bHaveSet ? 1 : 0, ShownBasic, *Tiles, Choice.Num(), Height);
	return true;
}
