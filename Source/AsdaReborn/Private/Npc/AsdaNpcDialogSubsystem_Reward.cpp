// Copyright (c) 2026 @sitxovski (Discord)
// AsdaNpcDialogSubsystem_Reward.cpp — ★ R3-c 12.11.2026 (разрез AsdaNpcDialogSubsystem_Popups.cpp, дословный перенос): окно награды: ячейки, раскладка, показ, выбор награды, подсказки, Tick, дампы.
//   Общее для частей — в AsdaNpcDialogSubsystemInternal.h; шапка и реверс — в AsdaNpcDialogSubsystem_Popups.cpp.
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "AsdaDataTableCache.h"
#include "Quest/AsdaQuestTypes.h"
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
#include "UI/AsdaCraftSubsystem.h"             // ★ ключ К-9 `asda.Craft.FixDeathRestoreOrder` (В8)
#include "UI/AsdaHuntReportSubsystem.h"         // ★ волна 2-Б: 30068 — окно «Доклад об охоте» 56450
#include "UI/AsdaTownWarSubsystem.h"            // ★ волна 2-Г: 30076 — окно налога города 79300
#include "TimerManager.h"
#include "EngineUtils.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "UI/AsdaDisassemblySubsystem.h"    // ★ волна 2-А: окно разборки 32000 видно ⇒ разговор молча не открывается
#include "Text/AsdaLangRows.h"              // [ПОРТ] 09.10.2026 (мультиязычность): перевод копии страницы сценария
#include "AsdaNpcDialogSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaNpcDialogSubsystem*.cpp (LogAsdaNpcDlg, чистые хелперы)
#include "Quest/AsdaBoardQuestSubsystem.h"   // ★ 28.10.2026: попап 540 «режим доски» (`uiQuestWin_openReward`), сброс ждущего сдачи
#include "AsdaNpcDialogSubsystemInternal.h"   // ★ R3-c 12.11.2026: общее для частей AsdaNpcDialogSubsystem_Popups*.cpp

// Э6: попап наград эпизода (JUI 540).
// Окно попапа.
static const TCHAR* kRwWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Quest/WBP_EPISODEPOP_WIN.WBP_EPISODEPOP_WIN_C");

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ПОДСКАЗКА `QUEST_INFO_WIN` (599)
// ════════════════════════════════════════════════════════════════════════════════════════════════
// ════════════════════════════════════════════════════════════════════════════════════════════════
//  Э6. ПОПАП НАГРАД `EPISODEPOP_WIN` (540)
//
//  ★★★ СЕМАНТИКА 13 ПОЛЕЙ БЛОКА ЦЕЛИ — ПОДТВЕРЖДЕНА ДВУМЯ МЕТОДАМИ (заход 2026-08-20).
//
//  МЕТОД 1 — КОД. Три эталонные функции читают ОДИН И ТОТ ЖЕ набор смещений блока цели:
//    `questStageBuildStatFlags@0xD7E950` (сколько категорий занято),
//    `itemSlotSetupEquipSowel@0xD7F130` (что кладётся в слот) и
//    `QuestReward_SetSlotDisplay@0xD80230` (как слот рисуется). Сопоставление даёт таблицу:
//
//    вид | поле блока цели | что это                 | как рисует эталон
//    ----+-----------------+-------------------------+--------------------------------------------
//      0 | +16  i32        | ДЕНЬГИ                  | «N <имя предмета 20551>» + иконка предмета 20551
//      1 | +20  i32        | ОПЫТ                    | «N <нотис 6010>» + иконка `exp2`
//      2 | +28  i32        | ХАРИЗМА                 | «N <нотис 6015>» + иконка `charisma-icon`
//      3 | +32  i32        | КОД ЭПИЗОДА             | имя эпизода + его иконка (`CQuest_Episode_FindById`)
//      4 | +36  i32        | —                       | СЛОТ НЕ СОЗДАЁТСЯ ВОВСЕ (`i != 4`), см. D16
//      5 | +40  i32        | —                       | слот создаётся, но `default:` — пустая строка (D16)
//      6 | +44  i32        | —                       | то же
//      7 | +48  u16        | —                       | то же
//      8 | +52  i32 (+56 u16 количество) | ПРЕДМЕТ/САМОЦВЕТ | имя+иконка предмета либо самоцвета, «×N»
//      9 | +60  i32 (+64 u16) | то же                | то же
//     10 | +68  i32 (+72 u16) | то же                | то же
//     11 | +76  i32 (+80 u16) | то же                | то же
//     12 | +84  i32 (+88 u16) | то же                | то же
//
//  МЕТОД 2 — ДАННЫЕ (`Content/AsdaAssets/Data/NPC/NpcNsrPage.json`, 4902 блока цели, из них 492 с
//  наградой). Значения ПОДТВЕРЖДАЮТ роль каждого поля:
//      +16  > 0 у 396 блоков: 2000, 9000, 34500, 46000, 92000 — порядок величин «деньги за квест»;
//      +20  > 0 у 472: 1000, 4000, 6000, 23250, 31000, 62000 — «опыт» (примерно 0.67 от денег);
//      +28, +32, +40, +44, +48 — не встречаются НИ РАЗУ (харизма/эпизод в этой сборке не выдаются);
//      +36  > 0 у 27: 4, 15, 24, 26 — маленькие числа, и они НИКОГДА не видны (вид 4 пропущен);
//      +52  > 0 у 44: 30985, 30978, 21387, 31100 — коды предметов, количество (+56) = 1;
//      +68  > 0 у 41: 7767, 20589, 7760 — коды предметов, количество (+72) = 1;
//      +76  > 0 у  2: 23239, 73360, количество (+80) = 1;
//      +60, +84 — не встречаются.
//  Расхождений между методами нет ⇒ семантика подтверждена, окно строится по данным, а не «на глаз».
//
//  ★ ВТОРОЙ БЛОК — ВЫБИРАЕМАЯ награда (`questRewardSlotParse@0xD81460`), смещения +92…+132.
//    В данных заполнен ровно один вид — 10 (`+112` код предмета, `+116` количество): 57 блоков,
//    коды 41788…41809, количество 1. Остальные шесть видов пусты. Панель `EPISODESELECT_WIN` (553)
//    именно ими и наполняется, выбор уходит на сервер кодом `ObjActive[j]`.
// ════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ НОТИСЫ ПОПАПА — ПЕРЕСВЕРЕНО ПОСТРОЧНО 2026-08-22 (прежнее прочтение было ошибочным).
//   Прежде в порту стояло «оба заголовка 542 и 555 эталон заполняет одним нотисом 6004
//   (`gap_75C[15260]`, `wnd[4286]`)». Арифметика смещений это опровергает: `self[4258]` — это
//   байт 4258*4 = 17032, а `gap_75C` начинается с 0x75C = 1884, значит `gap_75C[15148]` = `self[4258]`
//   = окно 540, `[15152]` = `self[4259]` = заголовок 568, а `[15260]` = `self[4286]` =
//   **`EPISODEPOP_ENDBUT` (567)**. То есть 6004 — это ПОДПИСЬ КНОПКИ, а не заголовок панели.
//   Заголовки эталон ставит в конце строителя (`ui_core.c:59692…59697`):
//       542 `EPISODE_BASICTITLE`  ← нотис 12003 («Обычная награда»)
//       555 `EPISODE_SELECTTITLE` ← нотис 12004 («Дополнительная награда»)
//       554 `EPISODEPOP_TEXT02`   ← нотис 12005 («Выбери предмет, который тебе больше понравится.»)
//   Отсюда и «Готово» на месте подзаголовка панели на кадре владельца.
//   6010 — «опыт», 6015 — «харизма» (подписи слотов) — эти два прочтения подтвердились.
static constexpr int32 kLocRewardEndBut  = 6004;

static constexpr int32 kLocRewardSelHint = 12005;
// ★★★ ЗАМЕЧАНИЕ ВЛАДЕЛЬЦА 2026-08-22: «Текст не помещается для выборной награды. Уменьши до
//   "Выбери предмет"». Нотис 12005 в русской локализации — «Выбери предмет, который тебе больше
//   понравится.»: в поле 554 (280 px, и то с вылетом за панель) он не помещается ни при каком
//   раскладе, см. измерения в `Reports/quest_reward_window_fix2.md` §1.
//   ★ Текст берём ИЗ ТАБЛИЦ ИГРЫ, а не зашиваем в код (закон `no-invented-ui`): нотис **13212** —
//   это ровно «Выбери предмет», строка САМОЙ ИГРЫ, и она уже используется оригиналом как
//   инструкция над списком предметов (`shop_trade.c:10683`, `:10741`:
//   `UI_FormatLocalizedNotice(ctx, 13212, 0, 1)` → `CControl_SetText(m_pField3, …)`).
//   Заводить новую строку не понадобилось.
static constexpr int32 kLocRewardSelHintShort = 13212;

static constexpr int32 kLocRewardCha   = 6015;

static const TCHAR* kIconCha = TEXT("charisma-icon");
// Шесть слотов каждой панели: имена контролов JUI (01…05 + 12 у базовой, 06…10 + 11 у выбора).
static const TCHAR* kRwBasicIdx[6] = { TEXT("01"), TEXT("02"), TEXT("03"), TEXT("04"), TEXT("05"), TEXT("12") };
static const TCHAR* kRwSelIdx[6]   = { TEXT("06"), TEXT("07"), TEXT("08"), TEXT("09"), TEXT("10"), TEXT("11") };

int32 UAsdaNpcDialogSubsystem::BuildRewardStatFlags(const FAsdaNsrStepObj& Obj, bool OutFlags[13])
{
	// Дословно `questStageBuildStatFlags`: 12 полей проверяются как `i32 > 0`, поле вида 7 — как u16 != 0.
	const int32 V[13] = { Obj.A16, Obj.A20, Obj.A28, Obj.A32, Obj.A36, Obj.A40, Obj.A44,
	                      Obj.A48, Obj.A52, Obj.A60, Obj.A68, Obj.A76, Obj.A84 };
	int32 N = 0;
	for (int32 i = 0; i < 13; ++i)
	{
		OutFlags[i] = (i == 7) ? (V[i] != 0) : (V[i] > 0);
		if (OutFlags[i]) { ++N; }
	}
	return N;
}

bool UAsdaNpcDialogSubsystem::CraftCheckSlotsValid(const bool Flags[13])
{
	// `craftCheckSlotsValid@0xD83CC0`: истина, только если заданы виды 8 и/или 9 и БОЛЬШЕ НИЧЕГО.
	const bool bItem89 = Flags[8] || Flags[9];
	int32 Others = 0;
	for (int32 i = 0; i < 13; ++i) { if (Flags[i] && i != 8 && i != 9) { ++Others; } }
	return Others == 0 && bItem89;
}

void UAsdaNpcDialogSubsystem::FillRewardSlotFromObj(const FAsdaNsrStepObj& Obj, int32 Kind, FAsdaRewardSlot& Out) const
{
	Out = FAsdaRewardSlot();
	Out.Kind = Kind;
	// Виды 0…7 — СКАЛЯР (`this[8*a8+4103] = поле`), виды 8…12 — ПАРА «код + количество»
	//   (`this[8*a8+4104] = код`, `this[8*a8+4105] = u16 количество`).
	switch (Kind)
	{
	case 0:  Out.Amount = Obj.A16; break;
	case 1:  Out.Amount = Obj.A20; break;
	case 2:  Out.Amount = Obj.A28; break;
	case 3:  Out.Amount = Obj.A32; break;
	case 4:  Out.Amount = Obj.A36; break;
	case 5:  Out.Amount = Obj.A40; break;
	case 6:  Out.Amount = Obj.A44; break;
	case 7:  Out.Amount = Obj.A48; break;
	case 8:  Out.ItemId = Obj.A52; Out.Count = Obj.C56; break;
	case 9:  Out.ItemId = Obj.A60; Out.Count = Obj.C64; break;
	case 10: Out.ItemId = Obj.A68; Out.Count = Obj.C72; break;
	case 11: Out.ItemId = Obj.A76; Out.Count = Obj.C80; break;
	case 12: Out.ItemId = Obj.A84; Out.Count = Obj.C88; break;
	default: break;
	}
	// `CItemManager_GetItemRecord` не нашёл, а `GetSowelRecord` нашёл ⇒ флаг «самоцвет» (`+14732`).
	if (Out.ItemId > 0)
	{
		if (const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr)
		{
			Out.bSowel = (IT->GetItem(Out.ItemId) == nullptr) && IT->IsSowelCode(Out.ItemId);
		}
	}
}

bool UAsdaNpcDialogSubsystem::ParseSelectRewardSlot(const FAsdaNsrStepObj& Obj, int32 PickCode, FAsdaRewardSlot& Out) const
{
	// Дословно `questRewardSlotParse@0xD81460`: семь проверок подряд, каждая ПЕРЕЗАПИСЫВАЕТ вид и
	//   значение; в конце требуется, чтобы сработала РОВНО ОДНА (`if (v12 == 1) return 1;`).
	Out = FAsdaRewardSlot();
	Out.PickCode = PickCode;
	int32 Hits = 0;
	if (Obj.S92  > 0)  { Out.Kind = 2;  Out.Amount = Obj.S92;  ++Hits; }
	if (Obj.S96  > 0)  { Out.Kind = 3;  Out.Amount = Obj.S96;  ++Hits; }
	if (Obj.S100 > 0)  { Out.Kind = 5;  Out.Amount = Obj.S100; ++Hits; }
	if (Obj.S108 != 0) { Out.Kind = 6;  Out.Amount = Obj.S104; ++Hits; }   // гейт — u16 +108, значение — i32 +104
	if (Obj.S112 > 0)  { Out.Kind = 10; Out.ItemId = Obj.S112; Out.Count = Obj.S116; ++Hits; }
	if (Obj.S120 > 0)  { Out.Kind = 11; Out.ItemId = Obj.S120; Out.Count = Obj.S124; ++Hits; }
	if (Obj.S128 > 0)  { Out.Kind = 12; Out.ItemId = Obj.S128; Out.Count = Obj.S132; ++Hits; }
	if (Hits != 1) { Out = FAsdaRewardSlot(); return false; }
	Out.PickCode = PickCode;
	if (Out.ItemId > 0)
	{
		if (const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr)
		{
			Out.bSowel = (IT->GetItem(Out.ItemId) == nullptr) && IT->IsSowelCode(Out.ItemId);
		}
	}
	return true;
}

// Порт `QuestReward_SetSlotDisplay@0xD80230` (что написать/нарисовать) + `uiQuickSlotPanelBSetRow@0xD960E0`
//   (куда именно положить: имя → TXTnn, иконка → ITEMICONnn, количество → текст ПОВЕРХ иконки,
//   после чего показываются и иконка, и подложка слота).
void UAsdaNpcDialogSubsystem::ApplyRewardSlotDisplay(bool bSelectPanel, int32 SlotIdx, const FAsdaRewardSlot& Slot)
{
	TArray<TObjectPtr<UWidget>>&    Backs = bSelectPanel ? RwSelBack : RwBasicBack;
	TArray<TObjectPtr<UTextBlock>>& Names = bSelectPanel ? RwSelName : RwBasicName;
	TArray<TObjectPtr<UImage>>&     Icons = bSelectPanel ? RwSelIcon : RwBasicIcon;
	TArray<TObjectPtr<UTextBlock>>& Qtys  = bSelectPanel ? RwSelQty  : RwBasicQty;
	if (!Backs.IsValidIndex(SlotIdx)) { return; }

	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaTextSubsystem*      TS = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;

	FString SlotName;      // `nameText_2`
	FString QtyText;       // `qtyText_2` (пусто, если количество <= 0 — эталон)
	FString FlatIcon;      // прямое имя текстуры (виды 1 и 2)
	int32   IconItem = -1; // код предмета/самоцвета для атласной иконки

	switch (Slot.Kind)
	{
	case 0:   // ДЕНЬГИ — эталон берёт имя и иконку у предмета 20551
		IconItem = kMoneyItemCode;
		SlotName = FString::Printf(TEXT("%d %s"), Slot.Amount, IT ? *IT->GetItemName(kMoneyItemCode) : TEXT(""));
		break;
	case 1:   // ОПЫТ
		FlatIcon = kIconExp;
		SlotName = FString::Printf(TEXT("%d %s"), Slot.Amount, TS ? *TS->GetTextById(kLocRewardExp) : TEXT(""));
		break;
	case 2:   // ХАРИЗМА
		FlatIcon = kIconCha;
		SlotName = FString::Printf(TEXT("%d %s"), Slot.Amount, TS ? *TS->GetTextById(kLocRewardCha) : TEXT(""));
		break;
	case 3:   // КОД ЭПИЗОДА — имя из таблицы эпизодов. Резолвер в порту ОДИН
		//   (`UAsdaQuestMarkerSubsystem::EpisodeTitleByQuestKey`, им же пользуется облачко 2406);
		//   своей копии поиска по Episode_Table здесь НЕ заводим.
	{
		UAsdaQuestMarkerSubsystem* QM = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
		SlotName = QM ? QM->EpisodeTitleByQuestKey(Slot.Amount) : FString();
		if (SlotName.IsEmpty()) { SlotName = UAsdaTextSubsystem::FormatPrintf(FAsdaText::Loc(990447), { FString::FromInt(Slot.Amount) }); }   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): «эпизод %d»
		break;
	}
	case 8: case 9: case 10: case 11: case 12:   // ПРЕДМЕТ либо САМОЦВЕТ
		IconItem = Slot.ItemId;
		SlotName = IT ? IT->GetItemName(Slot.ItemId) : FString();
		if (SlotName.IsEmpty()) { SlotName = UAsdaTextSubsystem::FormatPrintf(FAsdaText::Loc(990448), { FString::FromInt(Slot.ItemId) }); }   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): «код %d»
		if (Slot.Count > 0) { QtyText = FString::Printf(TEXT("%d"), Slot.Count); }
		break;
	default:
		// ★ ДЕФЕКТ ОРИГИНАЛА D16 (см. шапку раздела): виды 5, 6 и 7 доходят до `default:` в
		//   `QuestReward_SetSlotDisplay` и рисуют ПУСТУЮ строку — слот занят, а показать нечего.
		//   Воспроизводим как есть (в данных этой сборки эти виды не встречаются ни разу), но
		//   говорим вслух, чтобы «пустой слот» не выглядел ошибкой порта.
		UE_LOG(LogAsdaNpcDlg, Warning,
			TEXT("[NPC-диалог/награда] слот %d: вид %d у эталона не имеет ветки отрисовки ")
			TEXT("(ДЕФЕКТ ОРИГИНАЛА D16) — строка остаётся пустой"), SlotIdx, Slot.Kind);
		break;
	}

	if (Names.IsValidIndex(SlotIdx) && Names[SlotIdx]) { Names[SlotIdx]->SetText(FText::FromString(SlotName)); SetVis(Names[SlotIdx], true); }
	if (Icons.IsValidIndex(SlotIdx) && Icons[SlotIdx])
	{
		UImage* Img = Icons[SlotIdx];
		// Обе ветки — ОБЩИЕ точки порта, своих копий здесь нет (дедуп Д2):
		//   атласная иконка предмета — `UAsdaInventorySubsystem::ApplyIconToImage`,
		//   плоская текстура (`exp2`/харизма, `TexturePath_Format`) — `AsdaUi::ApplyFlatJuiIcon`.
		bool bDrawn = false;
		if (IconItem > 0 && Inv) { bDrawn = Inv->ApplyIconToImage(Img, IconItem, 32.f); }
		else { bDrawn = AsdaUi::ApplyFlatJuiIcon(Img, FlatIcon, 32.f); }
		SetVis(Img, bDrawn);
	}
	if (Qtys.IsValidIndex(SlotIdx) && Qtys[SlotIdx])
	{
		Qtys[SlotIdx]->SetText(FText::FromString(QtyText));
		SetVis(Qtys[SlotIdx], !QtyText.IsEmpty());
	}
	SetVis(Backs[SlotIdx], true);
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ОБВОДКА ТЕКСТА В ПОПАПЕ 540 — СТРОГО ПО ЭТАЛОНУ (замечание владельца 2026-08-22)
//
//  Механизм (разобран 2026-08-20, `Reports/npc_visual_fix.md` §1.2): обводку в оригинале даёт
//  поле 926 контрола — `CClass_SetField926(ctrl, 0x800000)`, вызов В КОДЕ. Поле 925 (`TextOutLine`
//  из `JuiSkin.ini`), по которому обводку запекает наш импортёр JUI, — это флаги ВЫВОДА
//  (перенос/обрезка), а не контур.
//
//  Построчная сверка строителя окна (`RE/etalon/ui_core.c:59374…59700`): внутри окна 540
//  `SetField926(…, 0x800000)` зовётся РОВНО ДЕСЯТЬ раз, и только для подписей количества поверх
//  иконок наград:
//      59399 → 544 ITEMICON01   59408 → 546 ITEMICON02   59417 → 548 ITEMICON03
//      59426 → 550 ITEMICON04   59435 → 552 ITEMICON05   59456 → 557 ITEMICON06
//      59465 → 559 ITEMICON07   59474 → 561 ITEMICON08   59483 → 563 ITEMICON09
//      59492 → 565 ITEMICON10
//  ⚠ В прошлом отчёте в этот список ошибочно попал 555 (`EPISODE_SELECTTITLE`, `ui_core.c:59445`) —
//    ему `SetField926` НЕ ставится. Ошибка исправлена, список выше сверен построчно.
//  Всем остальным контролам окна (заголовок задания 568, подзаголовки 542/554/555, подписи наград
//  589…598 и 2402/2405, подпись кнопки 567) эталон обводки НЕ даёт — там она наша, от импортёра.
//  ★ `ITEMICON11` (2404) и `ITEMICON12` (2401) — такие же подписи количества, но `SetField926` им
//    эталон не зовёт. Воспроизводим как есть: это расхождение ОРИГИНАЛА, а не наше.
//
//  Снимаем В РАНТАЙМЕ, а не в ассете: обводку запекает импортёр (`Tools/asda_jui_generic_umg.py`),
//  и после любого переимпорта она вернулась бы (грабля `tools-no-import-side-effects`).
//  Чтобы правка была двусторонней (и снимала лишнее, и оставляла положенное), проходим ВСЕ
//  текстовые блоки окна и выставляем контур по списку, а не «снимаем у тех, кого вспомнили».
// ════════════════════════════════════════════════════════════════════════════════════════════════
void UAsdaNpcDialogSubsystem::ApplyRewardOutline()
{
	if (!RwWin || !RwWin->WidgetTree) { return; }
	// Десять контролов, которым обводка положена кодом оригинала (см. таблицу выше).
	static const TCHAR* kOutlined[10] = {
		TEXT("EPISODEPOP_ITEMICON01"), TEXT("EPISODEPOP_ITEMICON02"), TEXT("EPISODEPOP_ITEMICON03"),
		TEXT("EPISODEPOP_ITEMICON04"), TEXT("EPISODEPOP_ITEMICON05"), TEXT("EPISODEPOP_ITEMICON06"),
		TEXT("EPISODEPOP_ITEMICON07"), TEXT("EPISODEPOP_ITEMICON08"), TEXT("EPISODEPOP_ITEMICON09"),
		TEXT("EPISODEPOP_ITEMICON10") };
	// ★ ДЕДУП Д3: своего обхода дерева текстов здесь нет — он на порт ОДИН
	//   (`AsdaUi::ApplyTextOutlineTree`, им же пользуется журнал заданий 700). Двусторонность
	//   («снять лишнее и оставить положенное») даёт сама общая точка: `DefaultPx = 0` всем,
	//   `ExceptPx = 1` — перечисленным.
	// ★★★ УБРАНО 2026-08-27: обводка расставлена по данным оригинала В САМИХ АССЕТАХ
	//   (шаг `Tools/asda_ui_text_style926.py`, таблица `Tools/data/text_style926.tsv` —
	//   реверс поля 926). ★ Прежний ручной разбор «оставить обводку у подписей количества»
	//   ПОЛНОСТЬЮ СОВПАЛ с данными: всем этим контролам обводка положена безусловно.
	//   Рантайм-простановка стала избыточной, а после отключения ошибочного правила
	//   импортёра — и вредной (перезаписала бы правильный ассет).
	(void)RwWin;   // окно используется соседними ветками; здесь делать нечего
}

// Девять планов рамки окна + прозрачная ручка перетаскивания той же геометрии.
//   Имена — из живого дампа виджетов (`headless__probe_npc_dialog_widgets.log`):
//   `EPISODEPOP_WIN_0_0…0_8` (сетка 3×3: колонки 64/320/64, строки 64/64/64 = 448×192) и
//   `EPISODEPOP_WIN_0_DRAG` (140,179,448,192) — ручка окна (`EVS EVT=6 RET=20003`, JUI 540).
void UAsdaNpcDialogSubsystem::CacheRewardFrame()
{
	RwFrame.Reset();
	RwDrag = nullptr;
	RwEndButText = nullptr;
	if (!RwWin) { return; }
	for (int32 i = 0; i < 9; ++i)
	{
		RwFrame.Add(Cast<UImage>(MqFindExact(RwWin, *FString::Printf(TEXT("EPISODEPOP_WIN_0_%d"), i))));
	}
	RwDrag       = MqFindExact(RwWin, TEXT("EPISODEPOP_WIN_0_DRAG"));
	RwEndButText = MqFindExact(RwWin, TEXT("EPISODEPOP_ENDBUT_Text"));
	int32 Have = 0;
	for (const TObjectPtr<UImage>& Im : RwFrame) { if (Im) { ++Have; } }
	UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог/награда] рамка 540: планов %d/9, ручка=%d, подпись кнопки=%d"),
		Have, RwDrag ? 1 : 0, RwEndButText ? 1 : 0);
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  Порт `uiWindow_relayoutByState@0xD941A0` (`RE/etalon/ui_core.c:60202…60355`).
//
//  Эталон меняет ТРИ вещи и ничего больше:
//    1) ВЫСОТУ рамки 540 — `sub_930E85(wnd[4258], -1, H)` + `uiPanel9SliceLayout`;
//    2) ПОЛОЖЕНИЕ панели выбора 553 — `SetField74_75(wnd[4272], baseX, baseY + Δ)`, где база —
//       позиция панели 541 (`Class_GetVec2_74(wnd[4260])`, то есть JUI-Origin «59 70»);
//    3) видимость панелей/заголовков (это в порту уже делает `ShowRewardPopup`).
//
//  Таблица высот и сдвигов (nB — гарантированных слотов, nS — выбираемых):
//    без панели выбора        : nB 1-2 → 192,  nB 3-4 → 256,  nB 5-6 → 320
//    панель выбора, nB == 0   : nS 1-2 → 192,  nS 3-4 → 256,  nS 5-6 → 320,  Δ = 0
//    панель выбора, nB 1-2    : Δ=100, nS 1-2 → 320, nS ≥ 3 → 384
//    панель выбора, nB 3-4    : Δ=135, nS 1-4 → 384, nS ≥ 5 → 448
//    панель выбора, nB 5-6    : Δ=180, nS 1-2 → 384, nS 3-4 → 448, nS ≥ 5 → 512
//
//  ★ ПОЗИЦИЮ САМОГО ОКНА эталон здесь НЕ трогает — она задана один раз в строителе и окно растёт
//    ВНИЗ. Мы считаем всё от ТЕКУЩЕГО положения ручки, а центрирование делаем ПОСЛЕ (см. ниже).
//  ★ Кнопка 567 в `.JUI` объявлена `AlignYPos = "8 0 0"` — «отступ от НИЗА окна» (Origin «152 -51»).
//    Импортёр запёк результат для высоты 192; при смене высоты кнопку надо сдвинуть на ту же дельту,
//    иначе она осталась бы висеть посреди выросшего окна.
// ════════════════════════════════════════════════════════════════════════════════════════════════
void UAsdaNpcDialogSubsystem::RelayoutRewardWindow(int32 NumBasic, int32 NumSelect, bool bSelectPanel)
{
	UCanvasPanelSlot* DragSlot = RwDrag ? AsdaUi::SlotOf(RwDrag) : nullptr;
	if (!DragSlot) { return; }
	// Рамка должна быть полной: `Resize9Slice` при неполной девятке молча выходит, и тогда ручка
	//   выросла бы, а картинки нет. Лучше не менять НИЧЕГО и сказать об этом вслух.
	int32 HaveFrame = 0;
	for (const TObjectPtr<UImage>& Im : RwFrame) { if (Im && AsdaUi::SlotOf(Im)) { ++HaveFrame; } }
	if (HaveFrame != 9)
	{
		UE_LOG(LogAsdaNpcDlg, Warning,
			TEXT("[NPC-диалог/награда] рамка 540 неполная (планов %d из 9) — высота окна НЕ пересчитана"), HaveFrame);
		return;
	}
	const FMargin Cur = DragSlot->GetOffsets();
	const float FrameL = Cur.Left, FrameT = Cur.Top, FrameW = Cur.Right, HCur = Cur.Bottom;

	// ★ ВАЖНАЯ ТОНКОСТЬ 1:1: у эталона КАЖДАЯ ветка смены высоты стоит под условием «слотов НЕ ноль»
	//   (`if (*(wnd+16800) && …)`, `if (nS) {…}`). При нулевом счётчике высота НЕ трогается вовсе —
	//   остаётся такой, какой была с прошлого показа. Воспроизводим ровно это: `H = HCur`.
	int32 H = (int32)HCur;
	int32 Delta = 0;
	if (!bSelectPanel)                       // ветка `a2 == 0`: только гарантированная панель
	{
		if (NumBasic > 0) { H = (NumBasic <= 2) ? 192 : (NumBasic <= 4 ? 256 : 320); }
	}
	else if (NumBasic == 0)                  // ветка `a2 != 0`, `*(wnd+16800) == 0`
	{
		if (NumSelect > 0) { H = (NumSelect <= 2) ? 192 : (NumSelect <= 4 ? 256 : 320); }
		Delta = 0;                           // панель выбора встаёт НА МЕСТО панели 541
	}
	else if (NumBasic <= 2)
	{
		Delta = 100;
		if (NumSelect > 0) { H = (NumSelect <= 2) ? 320 : 384; }
	}
	else if (NumBasic <= 4)
	{
		Delta = 135;
		if (NumSelect > 0) { H = (NumSelect <= 4) ? 384 : 448; }
	}
	else                                     // 5…6 (больше шести эталон отсекает раньше)
	{
		Delta = 180;
		if (NumSelect > 0) { H = (NumSelect <= 2) ? 384 : (NumSelect <= 4 ? 448 : 512); }
	}

	// 1) рамка: девятиплановая пересборка — ОБЩАЯ ТОЧКА `UAsdaTipPanel::Resize9Slice`
	//    (порт `uiPanel9SliceLayout`), своей копии математики здесь нет.
	UAsdaTipPanel::Resize9Slice(RwFrame, FVector2D(FrameW, (float)H), FVector2D(FrameL, FrameT));
	DragSlot->SetOffsets(FMargin(FrameL, FrameT, FrameW, (float)H));

	// 2) панель выбора 553 — на «позицию панели 541 плюс Δ». В WBP группа панели 541 лежит по
	//    JUI-Origin «59 70» от рамки, поэтому цель = рамка + 70 + Δ (X панели эталон не меняет).
	if (UCanvasPanelSlot* SelSlot = RwSelectGrp ? AsdaUi::SlotOf(RwSelectGrp) : nullptr)
	{
		const FMargin S = SelSlot->GetOffsets();
		SelSlot->SetOffsets(FMargin(S.Left, FrameT + 70.f + (float)Delta, S.Right, S.Bottom));
	}

	// 3) кнопка «Подтвердить» 567 — привязана к НИЗУ окна (`AlignYPos = "8 0 0"`).
	const float Shift = (float)H - HCur;
	if (!FMath::IsNearlyZero(Shift))
	{
		auto ShiftTop = [Shift](UWidget* W)
		{
			if (UCanvasPanelSlot* S = W ? AsdaUi::SlotOf(W) : nullptr)
			{
				const FMargin O = S->GetOffsets();
				S->SetOffsets(FMargin(O.Left, O.Top + Shift, O.Right, O.Bottom));
			}
		};
		ShiftTop(RwEndBut);
		ShiftTop(RwEndButText);
	}
	UE_LOG(LogAsdaNpcDlg, Log,
		TEXT("[NPC-диалог/награда] раскладка 540: гарантированных %d, выбираемых %d, панель выбора=%d ⇒ высота %d, Δпанели %d"),
		NumBasic, NumSelect, bSelectPanel ? 1 : 0, H, Delta);
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ★ ДЕФЕКТ ЛОКАЛИЗАЦИИ (исправляем и помечаем, закон `fix-original-defects`).
//  В `.JUI` подзаголовок панели выбора 555 объявлен шириной 100 px, а подсказка 554 начинается с
//  x = 102 — то есть они ПЕРЕКРЫВАЮТСЯ уже в данных на 6 px. Автору это сходило с рук, потому что
//  корейский нотис 12004 («추가 보상») занимает четыре знака. Русский «Дополнительная награда»
//  в наш 8-px шрифт в 100 px не влезает и наезжает на подсказку.
//  Чиним МИНИМАЛЬНО и по ФАКТУ: измеряем реальную ширину подзаголовка тем же измерителем Slate,
//  что и высоту строки (общая точка `AsdaUi::MeasureTextWidth`), и, ЕСЛИ подсказка начинается
//  раньше конца подзаголовка, сдвигаем её вправо, оставляя её ПРАВЫЙ край на месте. Если текст
//  влез (короткая локализация, корейский оригинал) — не двигаем ничего.
// ════════════════════════════════════════════════════════════════════════════════════════════════
void UAsdaNpcDialogSubsystem::FitSelectHintAfterTitle()
{
	if (!RwSelectTitle || !RwSelectHint || !RwSelectGrp) { return; }
	UCanvasPanelSlot* HintSlot = AsdaUi::SlotOf(RwSelectHint);
	UCanvasPanelSlot* TitleSlot = AsdaUi::SlotOf(RwSelectTitle);
	if (!HintSlot || !TitleSlot) { return; }

	// Левый край подзаголовка В КООРДИНАТАХ ПАНЕЛИ: у него есть своя группа-обёртка (со стрелкой),
	//   поэтому смещения складываем вверх по дереву до самой панели 553.
	float TitleLeft = 0.f;
	for (UWidget* Cur = RwSelectTitle; Cur && Cur != RwSelectGrp; Cur = Cur->GetParent())
	{
		if (UCanvasPanelSlot* S = AsdaUi::SlotOf(Cur)) { TitleLeft += S->GetOffsets().Left; }
	}
	const FSlateFontInfo Fnt = RwSelectTitle->GetFont();
	const FString Title = RwSelectTitle->GetText().ToString();
	const float TitleW = FMath::Max(AsdaUi::MeasureTextWidth(Title, Fnt, TitleSlot->GetOffsets().Right),
		TitleSlot->GetOffsets().Right);
	const FMargin H = HintSlot->GetOffsets();
	const float RightEdge = H.Left + H.Right;                 // правый край подсказки — держим на месте
	const float WantLeft = TitleLeft + TitleW + 4.f;          // 4 px — просвет между подзаголовком и подсказкой
	const float NewLeft = FMath::Max(WantLeft, H.Left);       // влезло — оставляем как есть
	const float NewW = FMath::Max(RightEdge - NewLeft, 40.f);
	if (NewLeft > H.Left + 0.5f) { HintSlot->SetOffsets(FMargin(NewLeft, H.Top, NewW, H.Bottom)); }

	// ★ ЧИСЛОВОЕ ДОКАЗАТЕЛЬСТВО, а не «на глаз» (замечание владельца №1). Меряем ОБЕ строки тем же
	//   измерителем Slate и печатаем, сколько места осталось подсказке и сколько ей нужно.
	const FString Hint = RwSelectHint->GetText().ToString();
	const float HintW = AsdaUi::MeasureTextWidth(Hint, RwSelectHint->GetFont(), NewW);
	UE_LOG(LogAsdaNpcDlg, Log,
		TEXT("[NPC-диалог/награда] панель 553: подзаголовок «%s» = %.0f px (поле %.0f, x %.0f); ")
		TEXT("подсказка «%s» = %.0f px, поле x %.0f→%.0f ширина %.0f→%.0f ⇒ %s (запас %.0f px)"),
		*Title, TitleW, TitleSlot->GetOffsets().Right, TitleLeft,
		*Hint, HintW, H.Left, NewLeft, H.Right, NewW,
		(HintW <= NewW + 0.5f) ? TEXT("ПОМЕЩАЕТСЯ") : TEXT("НЕ ПОМЕЩАЕТСЯ"), NewW - HintW);
	if (HintW > NewW + 0.5f)
	{
		UE_LOG(LogAsdaNpcDlg, Warning,
			TEXT("[NPC-диалог/награда] подсказка 554 шире своего поля на %.0f px — текст будет обрезан"),
			HintW - NewW);
	}
	// ★ Для сравнения — ЭТАЛОННЫЙ длинный нотис 12005 тем же измерителем: это и есть числовой
	//   ответ на вопрос «почему укоротили», а не «показалось».
	if (const UAsdaTextSubsystem* TS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr)
	{
		const FString Long = TS->GetTextById(kLocRewardSelHint);
		const float LongW = AsdaUi::MeasureTextWidth(Long, RwSelectHint->GetFont(), 0.f);
		UE_LOG(LogAsdaNpcDlg, Log,
			TEXT("[NPC-диалог/награда] для сравнения, эталонный нотис %d «%s» = %.0f px — это на %.0f px шире поля %.0f"),
			kLocRewardSelHint, *Long, LongW, LongW - NewW, NewW);
	}
}

bool UAsdaNpcDialogSubsystem::EnsureRewardWindow()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	if (RwWin && WM->IsWindowLive(RwWin)) { return true; }
	RwRelays.Reset();   // ★ 31.10.2026: попап 540 пересоздаётся — реле прежнего дерева мертвы, якорь только этого окна
	RwBasicGrp = nullptr; RwSelectGrp = nullptr;
	RwBasicTitle = nullptr; RwSelectTitle = nullptr; RwSelectHint = nullptr; RwEpisodeTitle = nullptr;
	RwEndBut = nullptr;
	RwBasicBack.Reset(); RwBasicName.Reset(); RwBasicIcon.Reset(); RwBasicQty.Reset();
	RwSelBack.Reset(); RwSelName.Reset(); RwSelIcon.Reset(); RwSelQty.Reset(); RwSelCursor.Reset();

	RwWin = WM->OpenWindowByPath(kRwWinId, kRwWbp, kDlgZ + 7, ESlateVisibility::SelfHitTestInvisible);
	if (!RwWin)
	{
		UE_LOG(LogAsdaNpcDlg, Error, TEXT("[NPC-диалог] WBP_EPISODEPOP_WIN не загрузился (%s)"), kRwWbp);
		return false;
	}
	WM->SetWindowVisible(kRwWinId, false);

	// ★ Имена взяты ИЗ ЖИВОГО ДАМПА виджетов (`Tools/_probe_npc_dialog_widgets.py`, лог
	//   `headless__probe_npc_dialog_widgets.log`), а не угаданы: подпись слота зовётся
	//   `EPISODEPOP_TXTnn`, картинка иконки — `EPISODEPOP_ITEMICONnn_<k>_0`, а количество лежит в
	//   ОДНОИМЁННОМ с иконкой TextBlock-е `EPISODEPOP_ITEMICONnn` (импортёр так разводит текст и план).
	RwBasicGrp     = MqFindExact(RwWin, TEXT("EPISODEBASIC_WIN_grp"));
	RwSelectGrp    = MqFindExact(RwWin, TEXT("EPISODESELECT_WIN_grp"));
	RwBasicTitle   = Cast<UTextBlock>(MqFindExact(RwWin, TEXT("EPISODE_BASICTITLE")));
	RwSelectTitle  = Cast<UTextBlock>(MqFindExact(RwWin, TEXT("EPISODE_SELECTTITLE")));
	RwSelectHint   = Cast<UTextBlock>(MqFindExact(RwWin, TEXT("EPISODEPOP_TEXT02")));
	RwEpisodeTitle = Cast<UTextBlock>(MqFindExact(RwWin, TEXT("EPISODEPOP_TITLE00")));
	RwEndBut       = Cast<UButton>(MqFindExact(RwWin, TEXT("EPISODEPOP_ENDBUT")));

	for (int32 i = 0; i < 6; ++i)
	{
		RwBasicBack.Add(MqFindExact(RwWin, *FString::Printf(TEXT("EPISODEPOP_ITEMBACK%s_grp"), kRwBasicIdx[i])));
		RwBasicName.Add(Cast<UTextBlock>(MqFindExact(RwWin, *FString::Printf(TEXT("EPISODEPOP_TXT%s"), kRwBasicIdx[i]))));
		RwBasicIcon.Add(Cast<UImage>(AsdaUi::FindNodeByPrefix(RwWin, *FString::Printf(TEXT("EPISODEPOP_ITEMICON%s_"), kRwBasicIdx[i]))));
		RwBasicQty.Add(Cast<UTextBlock>(MqFindExact(RwWin, *FString::Printf(TEXT("EPISODEPOP_ITEMICON%s"), kRwBasicIdx[i]))));
		RwSelBack.Add(MqFindExact(RwWin, *FString::Printf(TEXT("EPISODEPOP_ITEMBACK%s_grp"), kRwSelIdx[i])));
		RwSelName.Add(Cast<UTextBlock>(MqFindExact(RwWin, *FString::Printf(TEXT("EPISODEPOP_TXT%s"), kRwSelIdx[i]))));
		RwSelIcon.Add(Cast<UImage>(AsdaUi::FindNodeByPrefix(RwWin, *FString::Printf(TEXT("EPISODEPOP_ITEMICON%s_"), kRwSelIdx[i]))));
		RwSelQty.Add(Cast<UTextBlock>(MqFindExact(RwWin, *FString::Printf(TEXT("EPISODEPOP_ITEMICON%s"), kRwSelIdx[i]))));
	}
	// ★★★ КУРСОР ВЫБОРА 566 — ДЕВЯТКА С ПУСТЫМ ЦЕНТРОМ (исправлено 2026-08-22, замечание №2).
	//   Скин `Box_Orange3Stroke` — `ResizeWindow` из девяти планов (`ES = 0…8`, сетка 3×3 по 3 px);
	//   у центрального `PlaneAlwaysInvisible = "1"`, и импортёр его не создаёт
	//   (`asda_jui_generic_umg.py:366` отбрасывает невидимые планы), поэтому в WBP их ВОСЕМЬ:
	//     `_44_0`(0,17,3,3) `_44_1`(3,17,162,3) `_44_2`(165,17,3,3)
	//     `_44_3`(0,20,3,44)                    `_44_4`(165,20,3,44)
	//     `_44_5`(0,64,3,3) `_44_6`(3,64,162,3) `_44_7`(165,64,3,3)
	//   Порядок — построчный по сетке с пропуском центра, поэтому раскладываем их в массив
	//   0,1,2, 3,(nullptr),4, 5,6,7 — ровно тот вид, которого ждёт `UAsdaTipPanel::Resize9Slice`.
	for (int32 k = 0; k < 8; ++k)
	{
		if (k == 4) { RwSelCursor.Add(nullptr); }        // место невидимого центрального плана
		RwSelCursor.Add(Cast<UImage>(MqFindExact(RwWin, *FString::Printf(TEXT("EPISODEPOP_SELECTCUR_44_%d"), k))));
	}
	if (RwEndBut)
	{
		AsdaUi::BindButton(this, RwEndBut, 567, [this](int32 Id) { OnDialogButton(Id); }, PopupRelayAnchor(RwRelays));   // ★ U20: реле клика — общая точка
	}
	// ★★★ ДОБАВЛЕНО 2026-08-22. Строки панели ВЫБОРА не были кликабельны ВООБЩЕ: `OnRewardSelect`
	//   существовал, но звать его было некому, поэтому в режиме панелей 1 (20 сдач из 439)
	//   «выбранная ячейка» навсегда оставалась −1 и кнопка 567 отвечала бы «награда не выбрана».
	//   Эталон обрабатывает эти клики ОТДЕЛЬНОЙ функцией `itemTabWnd_onTabButtonCommand@0xD94DE0`
	//   (не `craftWnd_onButtonClick`, которая в режиме попапа знает только 567):
	//       556/557 → 0, 558/559 → 1, 560/561 → 2, 562/563 → 3, 564/565 → 4, 2403/2404 → 5.
	//   Клик-оверлей берём у ОБЩЕЙ точки `UAsdaIconCellGrid` (прозрачные кнопки поверх JUI-ячеек);
	//   иконки строк рисует `ApplyRewardSlotDisplay`, поэтому `Refresh()` у сетки не зовётся, а
	//   провайдер кода отдаёт 0. Размер клик-зоны — родной из `.JUI`: `Size = "160 42"`.
	{
		TArray<FName> SelCells;
		for (int32 i = 0; i < 6; ++i)
		{
			SelCells.Add(FName(*FString::Printf(TEXT("EPISODEPOP_ITEMBACK%s_grp"), kRwSelIdx[i])));
		}
		RwSelGrid = NewObject<UAsdaIconCellGrid>(this);
		RwSelGrid->SetCellSizePx(160.f, 42.f);
		RwSelGrid->SetupNamed(RwWin, SelCells, /*bMale*/true,
			[](int32) { return 0; },
			[this](int32 Cell) { ClickRewardSelectCell(Cell); });
	}
	ApplyRewardOutline();
	CacheRewardFrame();
	WM->RegisterWindowCloser(kRwWinId, FSimpleDelegate::CreateUObject(this, &UAsdaNpcDialogSubsystem::HideRewardPopup));
	UE_LOG(LogAsdaNpcDlg, Log,
		TEXT("[NPC-диалог] EPISODEPOP_WIN разобран: базовых слотов %d, слотов выбора %d, заголовки=%d/%d/%d, кнопка 567=%d"),
		RwBasicBack.Num(), RwSelBack.Num(),
		RwBasicTitle ? 1 : 0, RwSelectTitle ? 1 : 0, RwEpisodeTitle ? 1 : 0, RwEndBut ? 1 : 0);
	return true;
}

// Порт `uiQuestRewardPanelPopulate@0xD93F90` (+ `questBuildRewardSlots@0xD7E4E0`).
bool UAsdaNpcDialogSubsystem::ShowRewardPopup(int32 StepIndex, int32 SubIndex)
{
	const FAsdaNsrPageRow* Page = FindPage(CurrentNsr, CurrentTalkId);
	if (!Page || !Page->Steps.IsValidIndex(StepIndex)) { return false; }
	const FAsdaNsrStep& Step = Page->Steps[StepIndex];
	if (!Step.Subs.IsValidIndex(SubIndex)) { return false; }
	const FAsdaNsrStepSub& Sub = Step.Subs[SubIndex];
	if (Sub.Objs.Num() == 0)
	{
		UE_LOG(LogAsdaNpcDlg, Warning, TEXT("[NPC-диалог/награда] у шага %d под-записи %d нет блоков цели — показывать нечего"),
			StepIndex, SubIndex);
		return false;
	}
	const FAsdaNsrStepObj& Obj = Sub.Objs[0];   // эталон строит панель по блоку цели 0

	// ═══ РЕЖИМ ПАНЕЛЕЙ (`sub_921250` → `binTableLookup3D@0xD7E3C0`, bin_tables.c:178) ═══
	//   ★ РАЗОБРАНО 2026-08-20 (была строка 62 реестра отложенного). Писателя байта `gap_28[32]`
	//   искали не там: он стоит В СТРОКЕ НЕПОСРЕДСТВЕННО ПЕРЕД чтением (ui_core.c:59180-59181) и
	//   работает с ТЕМ ЖЕ объектом. Тело `binTableLookup3D` целиком:
	//       A = u16 @ (база + 31452*кв + 5228*шаг + 16 + 1032*под + 280)
	//       B = u16 @ (база + 31452*кв + 5228*шаг + 16 + 1032*под + 346)
	//       A!=0 && B==0 → 0;  A!=0 && B!=0 → 1;  A==0 && B!=0 → 2;  A==0 && B==0 → НЕ пишется
	//   ⚠ Константы 280/346 — СВЁРНУТЫЕ декомпилятором: у соседних читателей той же таблицы между
	//   `1032*под` и константой стоит переменное слагаемое, и `+68` (начало под-записи внутри шага)
	//   печатается отдельно, а здесь переменного слагаемого нет, поэтому 68 сложилось с числом.
	//   Значит смещения ОТНОСИТЕЛЬНО ПОД-ЗАПИСИ равны 280−68 = 212 и 346−68 = 278, то есть
	//   блок цели 0 (+188) плюс 24 и плюс 90 — это `Kind24` и `Flag90`, ТЕ ЖЕ ДВА ПОЛЯ, что читает
	//   `questNpc_IsTargetAvailable@0xD894C0`. Новых полей в таблице не нужно.
	//   Свёртку подтверждает независимо `uiQuestRewardPanelPopulate` (ui_core.c:60186): он берёт
	//   код эпизода по смещению 260 = 68 + 192 = блок цели 0 + 4, и это уже проверенный `QuestId`.
	//   Сверка по данным (13 150 под-записей `.NSR`): пара (1,0) — 426 штук, из них 421 имеет
	//   гарантированную награду и НИ ОДНОЙ разбираемой выбираемой; пара (1,1) — 20 штук, у ВСЕХ
	//   есть и гарантированная, и 1…3 выбираемых; пара (0,0) — 371 штука без наград вовсе;
	//   пара (0,1) в данных этой сборки не встречается. ⇒ `Kind24` = «есть гарантированная»,
	//   `Flag90` = «есть выбираемая». Прежнее поведение порта («строим обе панели всегда») давало
	//   расхождение на двух под-записях режима 0 — теперь его нет.
	if (Obj.Kind24 != 0 && Obj.Flag90 == 0)      { RewardPanelMode = 0; }
	else if (Obj.Kind24 != 0 && Obj.Flag90 != 0) { RewardPanelMode = 1; }
	else if (Obj.Kind24 == 0 && Obj.Flag90 != 0) { RewardPanelMode = 2; }
	// иначе (оба нуля) — эталон поле НЕ трогает, остаётся значение с прошлого показа.
	const int32 Mode = RewardPanelMode;

	// `questBuildRewardSlots@0xD7E4E0` (quest.c:5243): режимы 0 и 1 строят ГАРАНТИРОВАННУЮ панель
	//   (`questStageBuildStatFlags` + `itemSlotSetupEquipSowelSlots`), режим 2 её НЕ строит вовсе —
	//   а значит и оба ранних отказа (`> 6` и «только виды 8/9») в режиме 2 не выполняются.
	RewardBasic.Reset();
	// ★ «Выбранная ячейка» (эталонное `gap_8[56]`) — ЗНАЧИМОЕ состояние: именно её отправляет
	//   кнопка 567. У режима 0 эталон ставит 0 (выбирать нечего — берём единственную), у режимов
	//   1 и 2 — −1 (пока игрок не ткнул в строку). Прежде порт ставил −1 ВСЕГДА, из-за чего даже с
	//   исправленной кнопкой режим 0 упирался бы в нотис 6173.
	int32 NewSelIndex = RewardSelIndex;
	if (Mode != 2)
	{
		// `questStageBuildStatFlags` → «занятых категорий > 6 ⇒ базовая панель НЕ строится».
		//   ⚠ Эталон при этом возвращает 0, то есть попап ВСЁ РАВНО ПОКАЗЫВАЕТСЯ (просто с пустой
		//     базовой панелью), и «выбранную ячейку» не трогает. Прежде порт здесь выходил с
		//     `false`, то есть попап не показывал вовсе — расхождение убрано.
		bool Flags[13] = {};
		const int32 Occupied = BuildRewardStatFlags(Obj, Flags);
		if (Occupied > 6)
		{
			UE_LOG(LogAsdaNpcDlg, Warning,
				TEXT("[NPC-диалог/награда] занятых категорий %d > 6 — эталон базовую панель не строит, ")
				TEXT("попап показывается пустым"), Occupied);
		}
		else
		{
			// `craftCheckSlotsValid`: если награда состоит ТОЛЬКО из видов 8/9, выбирать нечего —
			//   эталон попап НЕ показывает, а СРАЗУ ШЛЁТ СДАЧУ (`itemSlotSetupEquipSowelSlots`,
			//   ветка `else`: `gap_8[56] = 0`, сохранение тройки, `netSendCraftSlotRequest(…, 2, …)`).
			//   ★★★ ИСПРАВЛЕНО 2026-08-22: порт эту ветку ЗНАЛ, но пакет НЕ СЛАЛ — просто возвращал
			//   `false`. То есть часть сдач молча проваливалась ещё до попапа.
			if (Mode == 0 && CraftCheckSlotsValid(Flags))
			{
				UE_LOG(LogAsdaNpcDlg, Log,
					TEXT("[NPC-диалог/награда] награда только из видов 8/9 — попапа нет, сдача уходит сразу (5041)"));
				RewardSelIndex = 0;                       // `gap_8[56] = 0`
				SendQuestComplete(StepIndex, SubIndex, 0);
				return false;                             // эталон: `questBuildRewardSlots` вернул 1 → `npcDialogPanelReset`
			}

			// `itemSlotSetupEquipSowelSlots`: слоты по категориям, ★ вид 4 ПРОПУСКАЕТСЯ (`i != 4`).
			for (int32 i = 0; i < 13; ++i)
			{
				if (!Flags[i] || i == 4) { continue; }
				FAsdaRewardSlot S;
				FillRewardSlotFromObj(Obj, i, S);
				RewardBasic.Add(S);
			}
			// `questBuildRewardSlots`: режим 0 → `gap_8[56] = 0`; режим 1 → −1 (ниже, вместе с 2).
			if (Mode == 0) { NewSelIndex = 0; }
		}
	}
	if (Mode != 0) { NewSelIndex = -1; }              // режимы 1 и 2: `gap_8[56] = -1`
	// Список ВЫБИРАЕМЫХ наград: собирают его ТОЛЬКО режимы 1 и 2 — по блокам цели, у которых
	//   `ObjActive[j] > 0`, каждый через `questRewardSlotParse`; код выбора = сам `ObjActive[j]`.
	RewardSelect.Reset();
	if (Mode != 0)
	{
		for (int32 j = 0; j < Sub.Objs.Num() && RewardSelect.Num() < 6; ++j)
		{
			if (!Sub.ObjActive.IsValidIndex(j) || Sub.ObjActive[j] <= 0) { continue; }
			FAsdaRewardSlot S;
			if (ParseSelectRewardSlot(Sub.Objs[j], Sub.ObjActive[j], S)) { RewardSelect.Add(S); }
		}
	}

	if (!EnsureRewardWindow()) { return false; }

	// Погасить все слоты, затем зажечь занятые (эталон `uiClearEquipSlots` + два `memset`).
	for (int32 i = 0; i < 6; ++i)
	{
		if (RwBasicBack.IsValidIndex(i)) { SetVis(RwBasicBack[i], false); }
		if (RwSelBack.IsValidIndex(i))   { SetVis(RwSelBack[i], false); }
	}
	for (int32 i = 0; i < RewardBasic.Num() && i < 6; ++i)  { ApplyRewardSlotDisplay(false, i, RewardBasic[i]); }
	for (int32 i = 0; i < RewardSelect.Num() && i < 6; ++i) { ApplyRewardSlotDisplay(true, i, RewardSelect[i]); }

	// Подписи панелей и кнопки — см. блок «НОТИСЫ ПОПАПА» выше (сверено построчно 2026-08-22).
	if (UAsdaTextSubsystem* TS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr)
	{
		if (RwBasicTitle)   { RwBasicTitle->SetText(FText::FromString(TS->GetTextById(kLocRewardBasic))); }
		if (RwSelectTitle)  { RwSelectTitle->SetText(FText::FromString(TS->GetTextById(kLocRewardSelect))); }
		// ★ Подсказка 554: короткий нотис 13212 вместо длинного 12005 — см. блок «НОТИСЫ ПОПАПА».
		//   Длинный оставлен запасом на случай, если в таблице нет короткого (тогда лучше обрезанный
		//   текст, чем ПУСТОЕ поле): молча пустеть поле не должно.
		if (RwSelectHint)
		{
			FString Hint = TS->GetTextById(kLocRewardSelHintShort);
			if (Hint.IsEmpty())
			{
				Hint = TS->GetTextById(kLocRewardSelHint);
				UE_LOG(LogAsdaNpcDlg, Warning,
					TEXT("[NPC-диалог/награда] нотиса %d в таблице нет — подсказка 554 взята из длинного %d"),
					kLocRewardSelHintShort, kLocRewardSelHint);
			}
			RwSelectHint->SetText(FText::FromString(Hint));
		}
		// Подпись кнопки 567 — `Collection_AddString(wnd[4286], нотис 6004)`; эталон ставит её и в
		//   `uiQuestRewardPanelPopulate`, и в `uiWindow_relayoutByState`, то есть на каждом показе.
		if (UTextBlock* Cap = Cast<UTextBlock>(RwEndButText))
		{
			Cap->SetText(FText::FromString(TS->GetTextById(kLocRewardEndBut)));
		}
		FitSelectHintAfterTitle();
	}
	// Название эпизода (568): эталон резолвит его по коду эпизода этого шага — в порту это поле
	//   `Objs[0].QuestId`, тот же источник, что и у облачка 2406 (общая точка, своей копии нет).
	if (RwEpisodeTitle)
	{
		UAsdaQuestMarkerSubsystem* QM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
		FString EpTitle = QM ? QM->EpisodeTitleByQuestKey(Obj.QuestId) : FString();
		if (EpTitle.IsEmpty()) { EpTitle = UAsdaTextSubsystem::FormatPrintf(FAsdaText::Loc(990447), { FString::FromInt(Obj.QuestId) }); }   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): «эпизод %d»
		RwEpisodeTitle->SetText(FText::FromString(EpTitle));
	}
	// Панели показываются только когда в них есть слоты (порт `uiWindow_relayoutByState`: там по числу
	//   слотов меняется ВЫСОТА окна, в порту её держит сам UMG-канвас).
	SetVis(RwBasicGrp, RewardBasic.Num() > 0);
	SetVis(RwSelectGrp, RewardSelect.Num() > 0);
	// ★ ВЫСОТА ОКНА И МЕСТО ПАНЕЛИ ВЫБОРА — порт `uiWindow_relayoutByState` (см. комментарий у
	//   `RelayoutRewardWindow`). Прежде порт этого не делал вовсе: в комментарии стояло «высоту
	//   держит сам UMG-канвас», что неверно — рамка в WBP это девять КАРТИНОК фиксированных 448×192,
	//   и при трёх и более наградах содержимое вылезало за облачко.
	//   ★ Третий параметр — РОВНО тот `a2`, что эталон передаёт в `uiWindow_relayoutByState`:
	//     `uiQuestRewardPanelPopulate(this, this->gap_28[32], …)` (`ui_core.c:59181`), а `gap_28[32]` —
	//     это режим панелей из `binTableLookup3D`. То есть «панель выбора есть» = `Mode != 0`, а не
	//     «слоты выбора распарсились» (по данным сборки это совпадает: пар (0,1) нет ни одной).
	RelayoutRewardWindow(RewardBasic.Num(), RewardSelect.Num(), Mode != 0);
	// ★ ИСПРАВЛЕНО 2026-08-22: было `RewardSelect.Num() > 0 ? 0 : -1` — «выбрана первая строка
	//   панели выбора, иначе ничего». Эталон решает НЕ по числу разобранных строк, а по РЕЖИМУ
	//   ПАНЕЛЕЙ (`questBuildRewardSlots`): режим 0 → 0, режимы 1 и 2 → −1. Разница принципиальная:
	//   в режиме 0 панели выбора нет вовсе, и прежний код отдавал −1 ⇒ кнопка 567 отвечала бы
	//   «награда не выбрана» вместо отправки.
	//   Курсор выбора рисуется только по строкам панели выбора; в режиме 0 их нет — прячем его,
	//   а само значение ставим ПОСЛЕ (`OnRewardSelect` перетирает `RewardSelIndex` своим аргументом).
	OnRewardSelect(RewardSelect.IsValidIndex(NewSelIndex) ? NewSelIndex : -1);
	RewardSelIndex = NewSelIndex;
	if (RwEndBut) { SetVis(RwEndBut, true); }

	if (UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>())
	{
		// ★★★ ОКНО — РОВНО ПО ЦЕНТРУ ЭКРАНА (замечание владельца 2026-08-22).
		//   Эталон центрирует попап один раз в строителе (`ui_core.c:59516…59522`):
		//       posX = ширина/2 − w/2;  posY = высота/2 − h/2;
		//       SetField74_75(540, posX, posY − h/2);          ← ЛИШНЕЕ вычитание половины высоты
		//   Второе вычитание — ДЕФЕКТ ОРИГИНАЛА: соседнее облачко 2406 центрируется ТЕМ ЖЕ кодом,
		//   написанным на 20 строк ниже (`ui_core.c:60338…60342`), и там его НЕТ. Из-за него попап
		//   уезжает на половину высоты вверх (при JUI-высоте 192 — на 192 px, ровно «хз где»).
		//   Исправляем и помечаем (закон `fix-original-defects`); центр берём по РАМКЕ окна и уже
		//   ПОСЛЕ пересчёта высоты, поэтому окно любой высоты стоит по центру на любом разрешении.
		WM->CenterWindowOnScreen(RwWin, RwDrag);
		WM->SetWindowVisible(kRwWinId, true);
		WM->BringWindowToFront(kRwWinId);
	}
	// ★★★ [ИСПРАВЛЕНИЕ] 29.10.2026 (жалобы «выбор награды выдал не то / зоны сдвинуты», окна «Faith Follower», Элла «A Right
	//   To Create Clan»): `RelayoutRewardWindow` поставил панель выбора 553 на «541 + Δ» (Δ 0/100/135/180), а клик-зоны
	//   строк остались там, где панель запечена в WBP (JUI-Origin «59 260» = 541 + 190) — на 190 − Δ px НИЖЕ строк
	//   (при двух базовых наградах — на 90 px, то есть на два ряда; нижние зоны ложились на кнопку 567). Переставляем зоны
	//   на текущее место строк ПОСЛЕ раскладки и центровки; погашенные строки мышь не ловят. Откат: `asda.UI.CellGridSyncOverlay 0`.
	if (RwSelGrid) { RwSelGrid->SyncClickOverlay(/*bFollowCellVisibility*/true); }
	UE_LOG(LogAsdaNpcDlg, Log,
		TEXT("[NPC-диалог/награда] попап 540 показан: эпизод %d, режим %d (Kind24=%d Flag90=%d), ")
		TEXT("гарантированных слотов %d, выбираемых %d"),
		Obj.QuestId, Mode, Obj.Kind24, Obj.Flag90, RewardBasic.Num(), RewardSelect.Num());
	return true;
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  Порт `itemTabWnd_positionTabHighlight@0xD9CFC0` (`RE/etalon/item_inventory.c:53144…`).
//
//  ЧТО ДЕЛАЕТ ЭТАЛОН. По номеру строки берёт ПОЗИЦИЮ её подложки — `Class_GetVec2_74(self[4275])`
//  для строки 0, `[4277]`, `[4279]`, `[4281]`, `[4283]` для 1…4 (это `EPISODEPOP_ITEMBACK06…10`) —
//  и переносит на неё курсор 566: `CControl_SetField74_75(self[4285], x, y)` + `SetVisible(…, 1)`.
//  Размер курсора при этом НЕ меняется: он фиксирован скином, 168 × 50 против подложки 160 × 42,
//  а «вылет» на 4 px во все стороны даёт объявление в `.JUI`:
//      `Origin = "-4 21"`, `AlignXPos = "5 -4 -4"`, `AlignYPos = "5 -4 -4"`
//  (подложка стоит на `Origin = "0 25"`, то есть −4 по обеим осям и +8 к каждому измерению).
//  ⇒ Геометрически правило одно: РАМКА = ПРЯМОУГОЛЬНИК ПОДЛОЖКИ, РАЗДУТЫЙ НА 4 px В КАЖДУЮ СТОРОНУ.
//
//  ★★★ ДЕФЕКТ ПОРТА (замечание владельца №2: «рамка только сверху и слева»). Прежний код ставил
//  ВСЕМ ВОСЬМИ планам рамки ОДНУ И ТУ ЖЕ точку — левый верхний угол подложки, — не трогая их
//  размеров. Восемь кусков сваливались в одну точку: широкая верхняя полоса (162 × 3) рисовалась
//  от угла вправо и читалась как «верхняя грань», высокая левая (3 × 44) — вниз и читалась как
//  «левая грань», а правый столбец и нижняя строка ложились поверх них и в глаза не бросались.
//  Правой и нижней граней не было ВООБЩЕ — их планы стояли не у правого/нижнего края, а в углу.
//  Чиним не координатами «на глаз», а пересборкой девятки под нужный прямоугольник — ОБЩЕЙ ТОЧКОЙ
//  `UAsdaTipPanel::Resize9Slice` (порт `uiPanel9SliceLayout`), той же, что держит рамку окна 540.
//  Своей математики 9-slice здесь нет.
//
//  ★ ПОБОЧНО ЛЕЧИТСЯ ЗАПЕЧЁННОЕ СМЕЩЕНИЕ. Импортёр применил `-4` из `AlignXPos`/`AlignYPos` ДВАЖДЫ:
//  в WBP планы стоят от (0, 17), тогда как подложка `ITEMBACK06_grp` — (8, 25), то есть верный
//  угол рамки (8−4, 25−4) = (4, 21). Считая прямоугольник ОТ ПОДЛОЖКИ, мы больше не зависим от
//  того, что запёк импортёр, — и переимпорт JUI это не сломает.
// ════════════════════════════════════════════════════════════════════════════════════════════════
// ★ Клик по строке панели ВЫБОРА. Единственное тело — им пользуется и прозрачная кнопка
//   `UAsdaIconCellGrid` поверх строки, и консоль `asda.Npc.RewardSelect` живой проверки.
void UAsdaNpcDialogSubsystem::ClickRewardSelectCell(int32 Cell)
{
	// Строки, которых в этом показе нет (и весь режим 0, где панели выбора нет вовсе),
	//   выбирать нельзя: иначе в пакет уехал бы индекс несуществующей награды.
	//   ★ 28.10.2026: в «режиме доски» строки выбора стоят на ФИКСИРОВАННЫХ местах 0…3 (`CQuestWin_AddRewardItem`: строка =
	//   индекс награды записи, его же сервер ждёт байтом 6593) — пустое место (вид −1) тоже не выбирается.
	if (!RewardSelect.IsValidIndex(Cell) || RewardSelect[Cell].Kind < 0)
	{
		UE_LOG(LogAsdaNpcDlg, Log,
			TEXT("[NPC-диалог/награда] клик по пустой строке выбора %d (строк %d) — игнор"),
			Cell, RewardSelect.Num());
		return;
	}
	UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог/награда] выбрана награда №%d (код выбора %d)"),
		Cell, RewardSelect[Cell].PickCode);
	OnRewardSelect(Cell);
}

void UAsdaNpcDialogSubsystem::OnRewardSelect(int32 SlotIdx)
{
	RewardSelIndex = SlotIdx;
	// Курсор выбора (566) виден, только когда есть что выбирать (`SetVisible(…, 1)` эталона стоит
	//   в тех же ветках, что и перенос; в режиме 0 панели выбора нет вовсе).
	const bool bShow = RewardSelect.IsValidIndex(SlotIdx);
	for (UImage* Im : RwSelCursor) { SetVis(Im, bShow); }
	if (!bShow || !RwSelBack.IsValidIndex(SlotIdx) || !RwSelBack[SlotIdx]) { return; }
	UCanvasPanelSlot* Src = Cast<UCanvasPanelSlot>(RwSelBack[SlotIdx]->Slot);
	if (!Src) { return; }

	// Раздутие рамки над подложкой — из `.JUI`, а не подобранное: подложка `Origin = "0 25"`,
	//   `Size = "160 42"`; курсор `Origin = "-4 21"`, `Size = "168 50"`.
	static constexpr float kSelCurInflate = 4.f;
	const FMargin B = Src->GetOffsets();
	const FVector2D Origin(B.Left - kSelCurInflate, B.Top - kSelCurInflate);
	const FVector2D Size(B.Right + 2.f * kSelCurInflate, B.Bottom + 2.f * kSelCurInflate);
	UAsdaTipPanel::Resize9Slice(RwSelCursor, Size, Origin);

	UE_LOG(LogAsdaNpcDlg, Log,
		TEXT("[NPC-диалог/награда] рамка выбора 566 → строка %d: подложка (%.0f,%.0f,%.0f,%.0f) ⇒ рамка (%.0f,%.0f,%.0f,%.0f)"),
		SlotIdx, B.Left, B.Top, B.Right, B.Bottom, Origin.X, Origin.Y, Size.X, Size.Y);
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ★ 28.10.2026 ДОСКА ОБЪЯВЛЕНИЙ: ПОПАП 540 В «РЕЖИМЕ ДОСКИ» — порт `uiQuestWin_openReward@0xD93160` (ui_core.c:60109)
//  Эталон: `uiClearEquipSlots` → 8 вызовов `CQuestWin_AddRewardItem@0xD93390` (quest.c:7461): i 0 — золото `+368` (строка 0, ВСЕГДА,
//    имя/иконка предмета 20551), i 1 — опыт `+364` (строка 1, ВСЕГДА, `exp2` + 6010), i 2…5 — «на выбор» `+332+4k` (k = i−2;
//    при коде > −1 → строка k панели выбора, вид 11), i 6…7 — предметы `+372+4n` (n = i−6; при коде > −1 → строка n+2 обычной
//    панели). Затем `uiWindow_relayoutByState(выбор[0] < 0 ? 0 : 1)`, 4286 ← 6004, 4258 видима, 4259 ← имя задания, закрыть магазин
//    NPC, `this[437] = 1`. Кнопка 567 этого режима — `ConfirmRewardPopup` (ветки `uiCraftSlotClickSendReq` с `sub_936EB1(доска) > −1`).
//  [ПОРТ] выбор строки «на выбор» на показе = −1: эталон его не сбрасывает (`uiClearEquipSlots` выбор не трогает), и выбор,
//    оставшийся от прошлого попапа NPC, уехал бы байтом 6593 вместо 99 — порт гасит выбор при каждом показе и скрытии попапа.
//  Окно разговора 570 при этом НЕ открыто (вход — кнопка 75281 доски); видимость/высоту/центр — общие точки этого попапа.
// ════════════════════════════════════════════════════════════════════════════════════════════════
bool UAsdaNpcDialogSubsystem::ShowBoardRewardPopup(int32 QuestId, const FString& Title)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaBoardQuestSubsystem* Board = GI ? GI->GetSubsystem<UAsdaBoardQuestSubsystem>() : nullptr;
	const FAsdaRaidQuestRow* R = Board ? Board->FindQuest(QuestId) : nullptr;
	if (!R)
	{
		// `CQuestWin_AddRewardItem` без записи возвращает 0 — эталон выходит из `uiQuestWin_openReward`, попапа нет.
		UE_LOG(LogAsdaNpcDlg, Warning, TEXT("[NPC-диалог/награда] доска: записи задания %d нет — попап 540 не показан"), QuestId);
		return false;
	}
	if (!EnsureRewardWindow()) { return false; }
	// ★ Ревизия М-6: `npcShop_Close(g_pNpcShopWnd)` (ui_core.c:60148) — магазин NPC закрывается ТОЙ ЖЕ точкой закрытия, что у
	//   ухода от NPC (`UAsdaNpcShopSubsystem::CloseShop`).
	if (UAsdaNpcShopSubsystem* Shop = GI->GetSubsystem<UAsdaNpcShopSubsystem>()) { if (Shop->IsOpen()) { Shop->CloseShop(); } }
	const UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>();
	auto ItemSlot = [IT](int32 Id, int32 Count)
	{
		FAsdaRewardSlot S;
		S.Kind = 11; S.ItemId = Id; S.Count = Count;   // вид 11 — предмет/самоцвет (`gap_75C[32k + 14716] = 11`)
		S.bSowel = IT && IT->GetItem(Id) == nullptr && IT->IsSowelCode(Id);
		return S;
	};
	// Обычная панель: строки на ФИКСИРОВАННЫХ местах 0 золото, 1 опыт, 2/3 предметы (пустое место — вид −1, строка скрыта).
	RewardBasic.Reset(); RewardBasic.SetNum(4);
	int32 NumBasic = 0, NumSelect = 0;   // `gap_75C[14916]` / `[14917]`
	RewardBasic[0].Kind = 0; RewardBasic[0].Amount = R->Gold; ++NumBasic;
	RewardBasic[1].Kind = 1; RewardBasic[1].Amount = R->Exp;  ++NumBasic;
	for (int32 n = 0; n < 2; ++n)
	{
		const int32 Id = R->RewardItemIds.IsValidIndex(n) ? R->RewardItemIds[n] : -1;
		if (Id > -1) { RewardBasic[2 + n] = ItemSlot(Id, R->RewardItemCounts.IsValidIndex(n) ? R->RewardItemCounts[n] : 0); ++NumBasic; }
	}
	RewardSelect.Reset(); RewardSelect.SetNum(4);
	for (int32 k = 0; k < 4; ++k)
	{
		const int32 Id = R->ChoiceItemIds.IsValidIndex(k) ? R->ChoiceItemIds[k] : -1;
		if (Id > -1)
		{
			RewardSelect[k] = ItemSlot(Id, R->ChoiceItemCounts.IsValidIndex(k) ? R->ChoiceItemCounts[k] : 0);
			RewardSelect[k].PickCode = k;   // байт выбора 6593 = индекс награды записи
			++NumSelect;
		}
	}
	for (int32 i = 0; i < 6; ++i)
	{
		if (RwBasicBack.IsValidIndex(i)) { SetVis(RwBasicBack[i], false); }
		if (RwSelBack.IsValidIndex(i))   { SetVis(RwSelBack[i], false); }
	}
	for (int32 i = 0; i < RewardBasic.Num(); ++i)  { if (RewardBasic[i].Kind >= 0)  { ApplyRewardSlotDisplay(false, i, RewardBasic[i]); } }
	for (int32 i = 0; i < RewardSelect.Num(); ++i) { if (RewardSelect[i].Kind >= 0) { ApplyRewardSlotDisplay(true, i, RewardSelect[i]); } }
	// `uiWindow_relayoutByState(this, +332 < 0 ? 0 : 1)` — панель выбора по ПЕРВОЙ награде на выбор.
	const bool bSelectPanel = R->ChoiceItemIds.IsValidIndex(0) && R->ChoiceItemIds[0] >= 0;
	if (UAsdaTextSubsystem* TS = GI->GetSubsystem<UAsdaTextSubsystem>())
	{
		if (RwBasicTitle)  { RwBasicTitle->SetText(FText::FromString(TS->GetTextById(kLocRewardBasic))); }
		if (RwSelectTitle) { RwSelectTitle->SetText(FText::FromString(TS->GetTextById(kLocRewardSelect))); }
		if (RwSelectHint)
		{
			FString Hint = TS->GetTextById(kLocRewardSelHintShort);
			if (Hint.IsEmpty()) { Hint = TS->GetTextById(kLocRewardSelHint); }
			RwSelectHint->SetText(FText::FromString(Hint));
		}
		if (UTextBlock* Cap = Cast<UTextBlock>(RwEndButText)) { Cap->SetText(FText::FromString(TS->GetTextById(kLocRewardEndBut))); }   // 4286 ← 6004
		FitSelectHintAfterTitle();
	}
	if (RwEpisodeTitle) { RwEpisodeTitle->SetText(FText::FromString(Title)); }   // 4259 ← имя задания
	SetVis(RwBasicGrp, NumBasic > 0);
	SetVis(RwSelectGrp, bSelectPanel);
	RelayoutRewardWindow(NumBasic, NumSelect, bSelectPanel);
	OnRewardSelect(-1);
	RewardSelIndex = -1;
	if (RwEndBut) { SetVis(RwEndBut, true); }
	bRewardPopupMode = true;   // `this[437] = 1` — окно принимает только 567
	if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
	{
		WM->CenterWindowOnScreen(RwWin, RwDrag);   // общая точка этого попапа (центр по рамке, без лишнего «− h/2» эталона)
		WM->SetWindowVisible(kRwWinId, true);
		WM->BringWindowToFront(kRwWinId);          // `CClass_MapInsert(g_pWindowMgr, 4258)`
	}
	// ★★★ [ИСПРАВЛЕНИЕ] 29.10.2026: клик-зоны строк выбора — за панелью выбора, которую только что передвинула
	//   `RelayoutRewardWindow` (разбор — в `ShowRewardPopup`). Откат: `asda.UI.CellGridSyncOverlay 0`.
	if (RwSelGrid) { RwSelGrid->SyncClickOverlay(/*bFollowCellVisibility*/true); }
	UE_LOG(LogAsdaNpcDlg, Log,
		TEXT("[NPC-диалог/награда] попап 540 «режим доски»: задание %d «%s», обычных %d, на выбор %d, панель выбора=%d"),
		QuestId, *Title, NumBasic, NumSelect, bSelectPanel ? 1 : 0);
	return true;
}

bool UAsdaNpcDialogSubsystem::DebugRewardPopupVisible() const
{
	const UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	return WM && RwWin && WM->IsWindowVisible(kRwWinId);
}

void UAsdaNpcDialogSubsystem::HideRewardPopup()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kRwWinId, false);
	}
	RewardBasic.Reset();
	RewardSelect.Reset();
	RewardSelIndex = -1;
	bRewardPopupMode = false;
	// ★ 28.10.2026 [ИСПРАВЛЕНИЕ ОРИГИНАЛА] (ключ `asda.Board.FixPendingReset`): попап ушёл без сдачи → «ждущее сдачи задание» доски
	//   (`this[10]` окна 75000) снимается. У эталона оно висит до 6593, и следующая 567 у NPC ушла бы 6593 вместо сдачи квеста NPC.
	//   После отправки 6593 доска уже сбросила его сама — тогда здесь ничего не происходит.
	if (UAsdaBoardQuestSubsystem* Board = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaBoardQuestSubsystem>() : nullptr)
	{
		Board->OnRewardPopupHidden();
	}
	// Окно ушло — снимаем СВОЮ подсказку (эталон msg 0xA `CControl_KillFocus(&g_ItemTooltip)`).
	//   Гасим только свою: закон `ui-shared-tip-ownership`.
	if (RwTipHover >= 0)
	{
		if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
		{
			Tip->HideTooltipFor(EAsdaTipOwner::QuestReward);
		}
		RwTipHover = -1;
	}
}

// ═══════════ ПОДСКАЗКА ПРЕДМЕТА ПО ИКОНКЕ НАГРАДЫ (порт msg 8/9/0xA окна 540) ═══════════════════
// ЭТАЛОН (`uiWindowMsgHandler@0xD91DB0`, ui_core.c:59737-59757):
//     case 8:   if (!helper_getField1(g_pUIManager)) uiShowEquipSlotTooltip2(this, ctrlId);
//     case 9:   Tooltip_LayoutNearCursor(&g_ItemTooltip);
//     case 0xA: CControl_KillFocus(&g_ItemTooltip);
//   `uiShowEquipSlotTooltip2@0xD925A0` принимает IDNum контрола и знает РОВНО двенадцать иконок:
//     544/546/548/550/552/2401 → слот 0…5 панели ГАРАНТИРОВАННОЙ награды (541),
//     557/559/561/563/565/2404 → слот 0…5 панели ВЫБОРА (553).
//   Гейт: подсказка есть ТОЛЬКО если вид слота (`Kind`) в диапазоне 8…12, то есть слот несёт
//   предмет или самоцвет. Слоты «деньги / опыт / харизма / эпизод» (виды 0…3) подсказки не имеют.
// ПОРТ. Сообщений окна у нас нет — курсор опрашивается покадрово (тот же приём, что у витрины
//   премиум-гачи). Развилку построителей держит ОБЩАЯ точка
//   `UAsdaItemTooltipSubsystem::ShowQuestRewardSlotTooltip`, второй копии билдеров тут нет.
//   Укладку у курсора (msg 9) делает сам показ (`LayoutAtCursor` внутри `ShowItemTooltip`).
void UAsdaNpcDialogSubsystem::TickRewardTooltips()
{
	// Попап не создан — тратить кадр не на что (тик крутится всю сессию, а окно живёт минуты).
	if (!RwWin && RwTipHover < 0) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!Tip || !WM) { return; }

	int32 Hit = -1;            // 0…5 — гарантированная награда, 6…11 — выбор
	int32 HitCode = 0, HitCount = 0;
	bool  bHitSelect = false;

	if (RwWin && WM->IsWindowVisible(kRwWinId))
	{
		// Гарантированная награда: иконки 544/546/548/550/552/2401.
		for (int32 i = 0; i < RwBasicIcon.Num() && Hit < 0; ++i)
		{
			if (!RewardBasic.IsValidIndex(i)) { break; }
			const FAsdaRewardSlot& S = RewardBasic[i];
			if (S.Kind < 8 || S.Kind > 12 || S.ItemId <= 0) { continue; }   // эталонный гейт «вид 8…12»
			if (AsdaUi::IsCursorOverWidget(RwBasicIcon[i]))
			{
				Hit = i; HitCode = S.ItemId; HitCount = S.Count; bHitSelect = false;
			}
		}
		// Панель выбора: иконки 557/559/561/563/565/2404.
		for (int32 i = 0; i < RwSelIcon.Num() && Hit < 0; ++i)
		{
			if (!RewardSelect.IsValidIndex(i)) { break; }
			const FAsdaRewardSlot& S = RewardSelect[i];
			if (S.Kind < 8 || S.Kind > 12 || S.ItemId <= 0) { continue; }
			if (AsdaUi::IsCursorOverWidget(RwSelIcon[i]))
			{
				Hit = 6 + i; HitCode = S.ItemId; HitCount = S.Count; bHitSelect = true;
			}
		}
	}

	if (Hit < 0)
	{
		if (RwTipHover >= 0) { Tip->HideTooltipFor(EAsdaTipOwner::QuestReward); RwTipHover = -1; }
		return;
	}
	if (Hit == RwTipHover) { return; }   // тот же контрол — эталон msg 8 повторно не шлёт
	RwTipHover = Hit;
	ShowRewardSlotTip(bHitSelect, bHitSelect ? Hit - 6 : Hit);
}

// ★ ЕДИНОЕ ТЕЛО ПОКАЗА подсказки слота награды: его зовёт и покадровый опрос курсора, и консольная
//   проверка `asda.Npc.RewardTip` (мышью в headless-PIE не навести, а заводить ВТОРОЙ путь показа
//   ради теста нельзя — тогда тест доказывал бы работу тестовой ветки, а не игровой).
bool UAsdaNpcDialogSubsystem::ShowRewardSlotTip(bool bSelectPanel, int32 SlotIdx)
{
	UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	const TArray<FAsdaRewardSlot>& Arr = bSelectPanel ? RewardSelect : RewardBasic;
	if (!Tip || !Arr.IsValidIndex(SlotIdx)) { return false; }
	const FAsdaRewardSlot& S = Arr[SlotIdx];
	if (S.Kind < 8 || S.Kind > 12 || S.ItemId <= 0)
	{
		// Эталонный гейт: подсказка есть только у слотов «предмет/самоцвет» (виды 8…12).
		UE_LOG(LogAsdaNpcDlg, Log,
			TEXT("[NPC-диалог/награда] панель %s слот %d: вид %d — подсказки у него НЕТ (эталон: только виды 8…12)"),
			bSelectPanel ? TEXT("ВЫБОРА") : TEXT("гарантированная"), SlotIdx, S.Kind);
		return false;
	}
	Tip->ShowQuestRewardSlotTooltip(S.ItemId, S.Count, bSelectPanel, EAsdaTipOwner::QuestReward);
	UE_LOG(LogAsdaNpcDlg, Log,
		TEXT("[NPC-диалог/награда] подсказка: панель %s слот %d, код %d ×%d, плашка на экране=%s"),
		bSelectPanel ? TEXT("ВЫБОРА") : TEXT("гарантированная"), SlotIdx, S.ItemId, S.Count,
		Tip->IsTooltipShown() ? TEXT("да") : TEXT("НЕТ"));
	return true;
}

void UAsdaNpcDialogSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.NpcDialog");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	// ★ Смерть своего персонажа → 5045 (см. подписку в `Initialize`).
	if (bLeaveOnDeathPending)
	{
		bLeaveOnDeathPending = false;
		// ★ Волна В6 ремесла (ревизия В5 МЕЛК-5): первая строка блока эталона — `uiWnd_Close(g_pQuestMgr)`
		//   (entity_char.c:16898): журнал квестов 700 закрывается своей точкой, без гейта «открыто» (звук 6 всегда).
		if (UAsdaQuestLogSubsystem* Ql = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaQuestLogSubsystem>() : nullptr)
		{
			Ql->CloseUnconditional();
		}
		// ★ Волна В8 ремесла (ревизия В7 МЕЛК-4, реестр W7-7): окна ремесла (entity_char.c:16908-16911) —
		//   `if (tileInteract_isControlVisible(g_pItemMainWnd)) uiWindowHideAndCleanup` → `if (isBoundControlVisible(
		//   g_pItemQtyMakeDlg)) itemQtyDlgReset`. Прежде оба окна ловили смерть СВОИМИ флагами в своих `Tick`, и порядок
		//   «5045 ↔ сброс 77500» задавал порядок регистрации тиков. Теперь весь блок идёт отсюда, детерминированно.
		//   ★ К-9 (дефект оригинала, ключ `asda.Craft.FixDeathRestoreOrder`, разбор — у ключа в AsdaCraftSubsystem.cpp):
		//   у эталона окна ремесла закрываются ПОСЛЕ 5045, и восстановление снимка (сумка) отказывает из-за видимого окна
		//   ремесла — снимок теряется. 1 (умолчание) = окна ремесла закрываются ДО 5045 (их порядок 76000 → 77500 сохранён);
		//   0 = строго порядок эталона 16900 → 16908 → 16910.
		//   ★ W8-2: при 1 закрытие 76000 (а с ним 77000) до 5045 не снимает снимок окна 77000 (К-7) — окна снимка возвращает
		//   5045 (смерть при открытом 77000 без перехода в 77500); при 0 — как эталон.
		const bool bCraftBefore5045 = UAsdaCraftSubsystem::IsDeathRestoreOrderFixEnabled();
		auto CraftDeathTail = [this, bCraftBefore5045]()
		{
			if (UAsdaCraftWindowSubsystem* W76 = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCraftWindowSubsystem>() : nullptr)
			{
				W76->CloseAfterSelfDeath(bCraftBefore5045);   // entity_char.c:16908-16909
			}
			if (UAsdaCraftMakeWindowSubsystem* Mk = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCraftMakeWindowSubsystem>() : nullptr)
			{
				Mk->ResetAfterSelfDeath();           // entity_char.c:16910-16911
			}
		};
		if (bCraftBefore5045) { CraftDeathTail(); }   // ★ К-9: отступление от порядка эталона (исправление дефекта)
		ResetAllWindowsAndLeave(TEXT("смерть своего персонажа (CCharacter_SetAction, entity_char.c:16900)"));
		CloseShopsAfterDeath();   // ★ волна В5 (ревизия В4 С-3): хвост эталона после 5045
		// ⚠ Следующая строка эталона — `hudHideTargetNameplate(&g_InventoryBag)` (entity_char.c:16907, ui_core.c:3537):
		//   скрыть окно 1620 `TARGETMARK_MAN` (плашка выбранной цели-игрока, поле `g_InventoryBag+0x148C`, привязка
		//   `uiCharSelectInitControls` ui_core.c:2799). В порту этого окна нет (рисуется только 1660) — подсистемы, через
		//   которую скрывать, нет; строка реестра В6.
		if (!bCraftBefore5045) { CraftDeathTail(); }  // порядок эталона: 16908-16911 после 5045
	}
	++RwTipTicks;
	TickRewardTooltips();
}

// ★ ЖИВАЯ ПРОВЕРКА (Э6). Подставляем тройку и идём ШТАТНЫМ путём — своей ветки показа нет.
void UAsdaNpcDialogSubsystem::ShowRewardPopupForCheck(const FString& NsrName, int32 TalkId, int32 StepIndex, int32 SubIndex)
{
	CurrentNsr = NsrName;
	CurrentTalkId = TalkId;
	SavedTalkId = TalkId; SavedStep = StepIndex; SavedSub = SubIndex;
	const bool bOk = ShowRewardPopup(StepIndex, SubIndex);
	UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог/награда] проверка «%s» talkId=%d шаг=%d под-запись=%d → %s"),
		*NsrName, TalkId, StepIndex, SubIndex, bOk ? TEXT("попап показан") : TEXT("попап НЕ показан (см. строку выше)"));
}

// ★ Живой разбор ПОКАЗАННОГО попапа 540 — доказательство трёх правок 2026-08-22.
//   Снимок показывает картинку, а этот дамп — ЧИСЛА: контур каждой подписи, текст заголовка
//   (и признак «есть непечатаемые/CJK-символы»), прямоугольник рамки против центра вьюпорта.
FString UAsdaNpcDialogSubsystem::DumpRewardWindow() const
{
	if (!RwWin || !RwWin->WidgetTree) { return TEXT("[награда] попап 540 не создан"); }
	FString Out = TEXT("=== попап наград EPISODEPOP_WIN (540) ===\n");
	// ★ ЖИВ ЛИ ПОКАДРОВЫЙ ОПРОС КУРСОРА. Подсказка по наведению держится ТОЛЬКО на нём
	//   (`TickRewardTooltips`), и если счётчик стоит на нуле — искать надо не в развилке
	//   построителей, а в том, почему подсистема не тикает.
	Out += FString::Printf(TEXT("опрос курсора: тиков %lld, под курсором сейчас %s\n"),
		(long long)RwTipTicks,
		RwTipHover < 0 ? TEXT("ничего")
		               : (RwTipHover < 6 ? *FString::Printf(TEXT("гарантированная %d"), RwTipHover)
		                                 : *FString::Printf(TEXT("выбор %d"), RwTipHover - 6)));

	// 1) ОБВОДКА. Ожидание: контур 1 px строго у `EPISODEPOP_ITEMICON01…10`, у всех прочих 0.
	int32 BadOn = 0, BadOff = 0;
	TArray<UTextBlock*> Texts;
	const_cast<UWidgetTree*>(ToRawPtr(RwWin->WidgetTree))->ForEachWidget([&](UWidget* W)
	{
		if (UTextBlock* Tb = Cast<UTextBlock>(W)) { Texts.Add(Tb); }
	});
	Texts.Sort([](const UTextBlock& A, const UTextBlock& B) { return A.GetName() < B.GetName(); });
	for (const UTextBlock* Tb : Texts)
	{
		const FString N = Tb->GetName();
		const int32 Px = Tb->GetFont().OutlineSettings.OutlineSize;
		const bool bWant = N.StartsWith(TEXT("EPISODEPOP_ITEMICON"), ESearchCase::CaseSensitive)
			&& N.Len() == 21 && N != TEXT("EPISODEPOP_ITEMICON11") && N != TEXT("EPISODEPOP_ITEMICON12");
		if (bWant && Px == 0) { ++BadOff; }
		if (!bWant && Px != 0) { ++BadOn; }
		Out += FString::Printf(TEXT("  %-30s обводка=%d %s текст=«%s»\n"), *N, Px,
			bWant ? TEXT("(эталон: ЕСТЬ)") : TEXT("(эталон: нет)"), *Tb->GetText().ToString());
	}
	Out += FString::Printf(TEXT("  ИТОГ обводки: подписей %d, лишних контуров %d, потерянных %d\n"),
		Texts.Num(), BadOn, BadOff);

	// 2) НАЗВАНИЕ ЗАДАНИЯ. Проверяем ещё и посимвольно: CJK в строке = кодировка снова сломана.
	if (RwEpisodeTitle)
	{
		const FString T = RwEpisodeTitle->GetText().ToString();
		int32 Cjk = 0;
		for (const TCHAR C : T)
		{
			if ((C >= 0xAC00 && C <= 0xD7A3) || (C >= 0x4E00 && C <= 0x9FFF)) { ++Cjk; }
		}
		Out += FString::Printf(TEXT("  заголовок 568: «%s» (символов %d, из них CJK %d — должно быть 0)\n"),
			*T, T.Len(), Cjk);
	}

	// 3) ЦЕНТР. ★ Сравнивать надо В ОДНОМ ПРОСТРАНСТВЕ: у Slate `GetAbsolutePosition()` — это
	//   координаты ВИРТУАЛЬНОГО РАБОЧЕГО СТОЛА, а `GameViewport->GetViewportSize()` — только размер.
	//   Поэтому опорой берём геометрию САМОГО окна-виджета (корневая канва растянута на вьюпорт),
	//   и обе величины оказываются в одних и тех же абсолютных единицах.
	if (const UWidget* Frame = RwDrag)
	{
		const FGeometry& GW = RwWin->GetCachedGeometry();
		const FGeometry& GF = Frame->GetCachedGeometry();
		const FVector2D WinPos = FVector2D(GW.GetAbsolutePosition()), WinSz = FVector2D(GW.GetAbsoluteSize());
		const FVector2D FrPos  = FVector2D(GF.GetAbsolutePosition()), FrSz  = FVector2D(GF.GetAbsoluteSize());
		const FVector2D ScreenC = WinPos + WinSz * 0.5f;
		const FVector2D FrameC  = FrPos + FrSz * 0.5f;
		Out += FString::Printf(
			TEXT("  вьюпорт: (%.0f,%.0f)+%.0fx%.0f, центр (%.1f,%.1f)\n")
			TEXT("  рамка 540: (%.0f,%.0f)+%.0fx%.0f, центр (%.1f,%.1f)\n")
			TEXT("  ОТКЛОНЕНИЕ ОТ ЦЕНТРА: (%.1f, %.1f) — должно быть (0.0, 0.0)\n"),
			WinPos.X, WinPos.Y, WinSz.X, WinSz.Y, ScreenC.X, ScreenC.Y,
			FrPos.X, FrPos.Y, FrSz.X, FrSz.Y, FrameC.X, FrameC.Y,
			FrameC.X - ScreenC.X, FrameC.Y - ScreenC.Y);
	}

	// 4) ПОДПИСИ ПАНЕЛИ ВЫБОРА (замечание владельца №1). Числа, а не «на глаз»: сколько занимает
	//    подзаголовок 555, сколько подсказка 554, и какое поле осталось у подсказки.
	if (RwSelectTitle && RwSelectHint)
	{
		const UCanvasPanelSlot* TS_ = AsdaUi::SlotOf(const_cast<UTextBlock*>(ToRawPtr(RwSelectTitle)));
		const UCanvasPanelSlot* HS_ = AsdaUi::SlotOf(const_cast<UTextBlock*>(ToRawPtr(RwSelectHint)));
		const FString T = RwSelectTitle->GetText().ToString();
		const FString H = RwSelectHint->GetText().ToString();
		const float TW = AsdaUi::MeasureTextWidth(T, RwSelectTitle->GetFont(), 0.f);
		const float HW = AsdaUi::MeasureTextWidth(H, RwSelectHint->GetFont(), 0.f);
		const float HField = HS_ ? HS_->GetOffsets().Right : 0.f;
		Out += FString::Printf(
			TEXT("  подзаголовок 555 «%s»: текст %.0f px, поле %.0f px\n")
			TEXT("  подсказка 554 «%s»: текст %.0f px, поле x=%.0f ширина %.0f ⇒ %s (запас %.0f px)\n"),
			*T, TW, TS_ ? TS_->GetOffsets().Right : 0.f,
			*H, HW, HS_ ? HS_->GetOffsets().Left : 0.f, HField,
			(HW <= HField + 0.5f) ? TEXT("ПОМЕЩАЕТСЯ") : TEXT("НЕ ПОМЕЩАЕТСЯ"), HField - HW);
	}

	// 5) РАМКА ВЫДЕЛЕНИЯ 566 (замечание владельца №2). Печатаем ВСЕ восемь планов и отдельно
	//    проверяем КАЖДУЮ ИЗ ЧЕТЫРЁХ ГРАНЕЙ: снимок не отличит «правая грань есть» от «правая
	//    грань лежит поверх левой», а числа отличат.
	if (RwSelCursor.Num() == 9)
	{
		FMargin R(0.f);
		bool bHaveR = false;
		if (RwSelBack.IsValidIndex(RewardSelIndex) && RwSelBack[RewardSelIndex])
		{
			if (const UCanvasPanelSlot* Bs = Cast<UCanvasPanelSlot>(RwSelBack[RewardSelIndex]->Slot))
			{
				const FMargin B = Bs->GetOffsets();
				R = FMargin(B.Left - 4.f, B.Top - 4.f, B.Right + 8.f, B.Bottom + 8.f);
				bHaveR = true;
			}
		}
		// ★ Рамка ПОКАЗАНА только когда есть что выбирать. В режиме панелей 0 строк выбора нет
		//   вовсе, рамка скрыта и НЕ переставляется — тогда её грани проверять не по чему, и
		//   написать «граней нет» было бы ложным отрицательным результатом.
		const bool bShown = RewardSelect.IsValidIndex(RewardSelIndex);
		Out += FString::Printf(TEXT("  рамка выбора 566: %s, выбрана строка %d, ожидаемый прямоугольник %s\n"),
			bShown ? TEXT("ПОКАЗАНА") : TEXT("скрыта (выбирать нечего)"),
			RewardSelIndex, bHaveR ? *FString::Printf(TEXT("(%.0f,%.0f)+%.0fx%.0f"), R.Left, R.Top, R.Right, R.Bottom)
			                       : TEXT("— (строка не выбрана)"));
		bHaveR = bHaveR && bShown;
		// Индексы сетки: 0 1 2 / 3 . 5 / 6 7 8; центра (4) в WBP нет — он невидим в скине.
		static const TCHAR* kCell[9] = { TEXT("верх-лево"), TEXT("верх-центр"), TEXT("верх-право"),
		                                 TEXT("серед-лево"), TEXT("центр(нет)"), TEXT("серед-право"),
		                                 TEXT("низ-лево"), TEXT("низ-центр"), TEXT("низ-право") };
		float MinX = TNumericLimits<float>::Max(), MinY = TNumericLimits<float>::Max();
		float MaxX = TNumericLimits<float>::Lowest(), MaxY = TNumericLimits<float>::Lowest();
		int32 Present = 0;
		for (int32 i = 0; i < 9; ++i)
		{
			const UCanvasPanelSlot* S = RwSelCursor[i] ? AsdaUi::SlotOf(const_cast<UImage*>(ToRawPtr(RwSelCursor[i]))) : nullptr;
			if (!S) { Out += FString::Printf(TEXT("    план %d %-12s — отсутствует\n"), i, kCell[i]); continue; }
			const FMargin O = S->GetOffsets();
			++Present;
			MinX = FMath::Min(MinX, O.Left);          MinY = FMath::Min(MinY, O.Top);
			MaxX = FMath::Max(MaxX, O.Left + O.Right); MaxY = FMath::Max(MaxY, O.Top + O.Bottom);
			Out += FString::Printf(TEXT("    план %d %-12s (%.0f,%.0f)+%.0fx%.0f вид=%d\n"),
				i, kCell[i], O.Left, O.Top, O.Right, O.Bottom, (int32)RwSelCursor[i]->GetVisibility());
		}
		if (Present > 0 && bHaveR)
		{
			// Грань замкнута, когда все три её плана ПРИЛЕГАЮТ к нужной линии прямоугольника
			//   и их отрезки вдоль этой линии стыкуются БЕЗ РАЗРЫВА, покрывая её целиком.
			//   bHoriz — грань горизонтальная (верх/низ); bFar — дальняя сторона (низ/право).
			auto Edge = [&](int32 A, int32 B, int32 C, bool bHoriz, bool bFar) -> bool
			{
				const int32 Idx[3] = { A, B, C };
				const float Line = bHoriz ? (bFar ? R.Top + R.Bottom : R.Top)
				                          : (bFar ? R.Left + R.Right : R.Left);
				const float Beg0 = bHoriz ? R.Left : R.Top;
				const float End0 = bHoriz ? R.Left + R.Right : R.Top + R.Bottom;
				float Reached = Beg0;
				for (int32 k = 0; k < 3; ++k)
				{
					const UCanvasPanelSlot* S = RwSelCursor[Idx[k]] ? AsdaUi::SlotOf(const_cast<UImage*>(ToRawPtr(RwSelCursor[Idx[k]]))) : nullptr;
					if (!S) { return false; }
					const FMargin O = S->GetOffsets();
					// поперёк грани: у ближней стороны совпадает начало, у дальней — конец
					const float Near = bHoriz ? O.Top : O.Left;
					const float Far  = Near + (bHoriz ? O.Bottom : O.Right);
					if (!FMath::IsNearlyEqual(bFar ? Far : Near, Line, 0.6f)) { return false; }
					// вдоль грани: покрытие без разрыва
					const float Beg = bHoriz ? O.Left : O.Top;
					const float End = Beg + (bHoriz ? O.Right : O.Bottom);
					if (Beg > Reached + 0.6f) { return false; }
					Reached = FMath::Max(Reached, End);
				}
				return Reached >= End0 - 0.6f;
			};
			const bool bTop = Edge(0, 1, 2, /*bHoriz*/true,  /*bFar*/false);
			const bool bBot = Edge(6, 7, 8, /*bHoriz*/true,  /*bFar*/true);
			const bool bLef = Edge(0, 3, 6, /*bHoriz*/false, /*bFar*/false);
			const bool bRig = Edge(2, 5, 8, /*bHoriz*/false, /*bFar*/true);
			Out += FString::Printf(
				TEXT("    габарит планов: (%.0f,%.0f)…(%.0f,%.0f)\n")
				TEXT("    ГРАНИ: верх=%s низ=%s лево=%s право=%s — все четыре должны быть ЕСТЬ\n"),
				MinX, MinY, MaxX, MaxY,
				bTop ? TEXT("ЕСТЬ") : TEXT("НЕТ"), bBot ? TEXT("ЕСТЬ") : TEXT("НЕТ"),
				bLef ? TEXT("ЕСТЬ") : TEXT("НЕТ"), bRig ? TEXT("ЕСТЬ") : TEXT("НЕТ"));
		}
	}
	return Out;
}

// ★ Полный обход таблицы страниц: где вообще есть награда и из чего она состоит.
FString UAsdaNpcDialogSubsystem::DumpRewardScan() const
{
	const UDataTable* Dt = AsdaDt::Cached(kDtPage);
	if (!Dt) { return TEXT("[награда] DT_NpcNsrPage не загружена"); }
	static const TCHAR* kKindName[13] = {
		TEXT("деньги"), TEXT("опыт"), TEXT("харизма"), TEXT("эпизод"), TEXT("вид4(не показывается)"),
		TEXT("вид5(пусто)"), TEXT("вид6(пусто)"), TEXT("вид7(пусто)"),
		TEXT("предмет8"), TEXT("предмет9"), TEXT("предмет10"), TEXT("предмет11"), TEXT("предмет12") };
	FString Out = TEXT("=== страницы с наградой (DT_NpcNsrPage) ===\n");
	int32 Total = 0;
	for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
	{
		const FAsdaNsrPageRow* R = reinterpret_cast<const FAsdaNsrPageRow*>(It.Value);
		if (!R) { continue; }
		for (int32 st = 0; st < R->Steps.Num(); ++st)
		{
			const FAsdaNsrStep& Step = R->Steps[st];
			for (int32 m = 0; m < Step.Subs.Num(); ++m)
			{
				const FAsdaNsrStepSub& Sub = Step.Subs[m];
				if (Sub.Objs.Num() == 0) { continue; }
				bool Flags[13] = {};
				const int32 N = BuildRewardStatFlags(Sub.Objs[0], Flags);
				if (N == 0) { continue; }
				FString Parts;
				for (int32 k = 0; k < 13; ++k)
				{
					if (!Flags[k]) { continue; }
					FAsdaRewardSlot S;
					FillRewardSlotFromObj(Sub.Objs[0], k, S);
					Parts += FString::Printf(TEXT("%s%s="), Parts.IsEmpty() ? TEXT("") : TEXT(", "), kKindName[k]);
					Parts += (S.ItemId > 0) ? FString::Printf(TEXT("код %d ×%d"), S.ItemId, S.Count)
					                        : FString::Printf(TEXT("%d"), S.Amount);
				}
				int32 SelCount = 0;
				for (int32 j = 0; j < Sub.Objs.Num(); ++j)
				{
					if (!Sub.ObjActive.IsValidIndex(j) || Sub.ObjActive[j] <= 0) { continue; }
					FAsdaRewardSlot S;
					if (ParseSelectRewardSlot(Sub.Objs[j], Sub.ObjActive[j], S)) { ++SelCount; }
				}
				// Режим панелей — та же формула, что в `ShowRewardPopup` (`binTableLookup3D@0xD7E3C0`);
				//   −1 = «эталон поле не пишет, остаётся значение с прошлого показа».
				const int32 Md = (Sub.Objs[0].Kind24 != 0)
					? (Sub.Objs[0].Flag90 != 0 ? 1 : 0)
					: (Sub.Objs[0].Flag90 != 0 ? 2 : -1);
				Out += FString::Printf(TEXT("%s «%s» talkId=%d шаг=%d под-запись=%d эпизод=%d режим=%d: %s%s\n"),
					*It.Key.ToString(), *R->NsrName, R->TalkId, st, m, Sub.Objs[0].QuestId, Md, *Parts,
					SelCount > 0 ? *FString::Printf(TEXT(" | выбираемых: %d"), SelCount) : TEXT(""));
				++Total;
			}
		}
	}
	Out += FString::Printf(TEXT("ИТОГО под-записей с наградой: %d\n"), Total);
	return Out;
}
