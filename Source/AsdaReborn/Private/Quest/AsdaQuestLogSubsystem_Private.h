// Copyright (c) 2026 @sitxovski (Discord)
// AsdaQuestLogSubsystem_Private.h — ★ R3-c 12.11.2026 (разрез AsdaQuestLogSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaQuestLogSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaQuestLogSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "Quest/AsdaQuestLogSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI

#include "Quest/AsdaQuestMarkerSubsystem.h"
#include "Npc/AsdaNpcDialogTypes.h"          // FAsdaEpisodeRow, FAsdaQuestRewardTableRow
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaSystemMenuSubsystem.h"
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaTipPanel.h"   // ★ общая точка Resize9Slice (порт uiPanel9SliceLayout)
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaMinimapSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaNameplateComponent.h"
#include "Npc/AsdaNpcDialogSubsystem.h"   // ★ 19.10.2026: награды завершения 5052 — `ApplyQuestCompletionRewards5052`

#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "World/AsdaFishingSubsystem.h"     // ★ 08.10.2026 (рыбалка В4): гейт «идёт рыбалка» кнопок окна заданий (событие 66561)
#include "Text/AsdaLangRows.h"               // [ПОРТ] 09.10.2026 (мультиязычность): тексты книги задания / эпизода / рейда на языке сессии
#include "Quest/AsdaBoardQuestSubsystem.h"   // ★ 28.10.2026: задания доски объявлений 20000…30000 — тип, награды, готовность, имя
#include "UI/AsdaNotice.h"                   // ★ 31.10.2026: нотис провала отказа от задания (5007) — канал эталонного 6956
#include "UI/AsdaTutorialEvents.h"   // ★ 10.11.2026 обучение: флаги шагов гида 1950 (TUTORIAL.md §2.3)

// Категория журнала частей AsdaQuestLogSubsystem*.cpp: определение — AsdaQuestLogSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaQuestLog, Log, All );

// AsdaQuestEpisodeFishingGate: определение — в домашней части (R2).
bool AsdaQuestEpisodeFishingGate(const UGameInstance* GI, int32 CtrlId);

// GAsdaQuestEtalonZeroMoney: определение — в домашней части (R2).
extern int32 GAsdaQuestEtalonZeroMoney;

// GAsdaQuestCollapseRewardCells: определение — в домашней части (R2).
extern int32 GAsdaQuestCollapseRewardCells;

// GAsdaQuestRightEdgeKeepsDrag: определение — в домашней части (R2).
extern int32 GAsdaQuestRightEdgeKeepsDrag;

// GAsdaQuestLogAbandonAck: определение — в домашней части (R2).
extern int32 GAsdaQuestLogAbandonAck;

// GAsdaQuestLogSlotMove: определение — в домашней части (R2).
extern int32 GAsdaQuestLogSlotMove;

// GQuestRightEdgeBase: определение — в домашней части (R2).
extern TMap<TWeakObjectPtr<UWidget> , FMargin> GQuestRightEdgeBase;

// ── Таблицы ─────────────────────────────────────────────────────────────────────────────────────
static const TCHAR* kEpisodeDT = TEXT("/Game/AsdaAssets/Data/NPC/DT_EpisodeTable.DT_EpisodeTable");
static const TCHAR* kBookDT    = TEXT("/Game/AsdaAssets/Data/Quest/DT_QuestBook.DT_QuestBook");
static const TCHAR* kGuideDT   = TEXT("/Game/AsdaAssets/Data/Quest/DT_QuestGuide.DT_QuestGuide");
static const TCHAR* kRewardDT  = TEXT("/Game/AsdaAssets/Data/NPC/DT_QuestRewardTable.DT_QuestRewardTable");

static const TCHAR* kRaidDT    = TEXT("/Game/AsdaAssets/Data/Quest/DT_RaidQuestData.DT_RaidQuestData");

namespace QLogLang {
// BookTitle: определение — в домашней части (R2).
FString BookTitle(const FAsdaQuestBookRow* B);
// BookDesc: определение — в домашней части (R2).
FString BookDesc(const FAsdaQuestBookRow* B);
// BookStep: определение — в домашней части (R2).
FString BookStep(const FAsdaQuestBookRow* B, int32 Stage);
// EpTitle: определение — в домашней части (R2).
FString EpTitle(const FAsdaEpisodeRow* Ep);
// RaidName: определение — в домашней части (R2).
FString RaidName(const FAsdaRaidQuestRow* R);
}

namespace QLogUi {
// ── Имена узлов импортированного WBP (сняты дампом ассета, а не угаданы; закон
//    `jui-importer-widget-naming`: интерактив/текст = ТОЧНОЕ имя `idc`, план = `{idc}_{idx}_{plane}`).
	// Ячейки журнала 720…731 — ПЛАНЫ (иконка), поверх которых лежит текстовый счётчик `QUEST_INVENn`.
	static const TCHAR* SlotIcon[12] = {
		TEXT("QUEST_INVEN0_28_0"),  TEXT("QUEST_INVEN1_29_0"),  TEXT("QUEST_INVEN2_30_0"),
		TEXT("QUEST_INVEN3_31_0"),  TEXT("QUEST_INVEN4_32_0"),  TEXT("QUEST_INVEN5_33_0"),
		TEXT("QUEST_INVEN6_34_0"),  TEXT("QUEST_INVEN7_35_0"),  TEXT("QUEST_INVEN8_36_0"),
		TEXT("QUEST_INVEN9_37_0"),  TEXT("QUEST_INVEN10_38_0"), TEXT("QUEST_INVEN11_39_0"),
	};
	static const TCHAR* SlotSel = TEXT("QUEST_INVENSEL_40_0");            // 737
	// Лоток целей 715…719: план-иконка + текстовый счётчик.
	static const TCHAR* TrayIcon[5] = {
		TEXT("QUEST_TRAY0_56_0"), TEXT("QUEST_TRAY1_57_0"), TEXT("QUEST_TRAY2_58_0"),
		TEXT("QUEST_TRAY3_59_0"), TEXT("QUEST_TRAY4_60_0"),
	};
	static const TCHAR* TrayText[5] = {
		TEXT("QUEST_TRAY0"), TEXT("QUEST_TRAY1"), TEXT("QUEST_TRAY2"),
		TEXT("QUEST_TRAY3"), TEXT("QUEST_TRAY4"),
	};
	// Награды: базовая (1690…1693 иконки, 786…789 подписи) и «на выбор» (1694…1697 / 795…798).
	static const TCHAR* RewardIcon[4] = {
		TEXT("Quest_Display_RewardIcon_0_71_0"), TEXT("Quest_Display_RewardIcon_1_73_0"),
		TEXT("Quest_Display_RewardIcon_2_75_0"), TEXT("Quest_Display_RewardIcon_3_77_0"),
	};
	static const TCHAR* RewardText[4] = {
		TEXT("Quest_Display_RewardIcon_T0"), TEXT("Quest_Display_RewardIcon_T1"),
		TEXT("Quest_Display_RewardIcon_T2"), TEXT("Quest_Display_RewardIcon_T3"),
	};
	// ★ СЧЁТЧИК ПОВЕРХ иконки награды (IDNum 1690…1693) — отдельный текстовый узел рядом с планом
	//   иконки; ИМЕННО ему эталон ставит обводку (`quest.c:1476`), а не подписи `…_T0…T3`.
	static const TCHAR* RewardIconText[4] = {
		TEXT("Quest_Display_RewardIcon_0"), TEXT("Quest_Display_RewardIcon_1"),
		TEXT("Quest_Display_RewardIcon_2"), TEXT("Quest_Display_RewardIcon_3"),
	};
	// То же для наград на выбор (IDNum 1694…1697, `quest.c:1486`).
	static const TCHAR* ChoiceIconText[4] = {
		TEXT("Quest_Display_CRewardIcon_0"), TEXT("Quest_Display_CRewardIcon_1"),
		TEXT("Quest_Display_CRewardIcon_2"), TEXT("Quest_Display_CRewardIcon_3"),
	};
	static const TCHAR* ChoiceIcon[4] = {
		TEXT("Quest_Display_CRewardIcon_0_80_0"), TEXT("Quest_Display_CRewardIcon_1_82_0"),
		TEXT("Quest_Display_CRewardIcon_2_84_0"), TEXT("Quest_Display_CRewardIcon_3_86_0"),
	};
	static const TCHAR* ChoiceText[4] = {
		TEXT("Quest_Display_CRewardIcon_T0"), TEXT("Quest_Display_CRewardIcon_T1"),
		TEXT("Quest_Display_CRewardIcon_T2"), TEXT("Quest_Display_CRewardIcon_T3"),
	};
	static const TCHAR* RouteBtn[4]  = { TEXT("QUEST_btn_b1"), TEXT("QUEST_btn_b2"), TEXT("QUEST_btn_b3"), TEXT("QUEST_btn_b4") };
	static const TCHAR* RouteText[4] = { TEXT("QUEST_btn_b1_Text"), TEXT("QUEST_btn_b2_Text"), TEXT("QUEST_btn_b3_Text"), TEXT("QUEST_btn_b4_Text") };
	static const TCHAR* RouteGrp1    = TEXT("QUEST_btn_b1_grp");          // вместе с указателем 745

	static const TCHAR* Title      = TEXT("QUEST_TITLE");                 // 703
	static const TCHAR* Display    = TEXT("QUEST_DISPLAYWIN");            // 714
	static const TCHAR* TabBarText = TEXT("Quest_Display_TabBar_Text");   // 742 (D7)
	static const TCHAR* TabBarImg  = TEXT("Quest_Display_TabBar_42_0");
	static const TCHAR* TrackBox   = TEXT("Quest_State_ChkBtn");          // 743
	static const TCHAR* TrackLabel = TEXT("Quest_StateTxt");              // 744
	static const TCHAR* RewardHdrA = TEXT("Quest_Display_TabWin2_T1_0");  // 785
	static const TCHAR* RewardHdrB = TEXT("Quest_Display_TabWin2_T2_0");  // 790 (D8)
	static const TCHAR* BtnClose   = TEXT("QUEST_TOPCLOSE");              // 702
	static const TCHAR* BtnTrash   = TEXT("QUEST_TRASHBUT");              // 713
	static const TCHAR* BtnExpand  = TEXT("QUEST_SIDEBUT");               // 739
	static const TCHAR* BtnCollaps = TEXT("QUEST_CLOSEBUT");              // 735
	static const TCHAR* RightPage  = TEXT("QUEST_RIGHISIDEBA_grp");       // 732
	static const TCHAR* ListHit    = TEXT("QUEST_CENTERLIN_110_HIT");     // 734 (место под список)
}

namespace QLoc {
// ── Локализация (нотисы) ────────────────────────────────────────────────────────────────────────
	static constexpr int32 kDescKindBase   = 3580;   // + вид квеста из `.QST` (+76): 0 «Обычное», 1 «Элитное»
	static constexpr int32 kDone           = 6163;   // «Готово» — приписка к названию выполненного
	static constexpr int32 kTabChapter     = 6101;   // «Глава» — подпись ярлыка вкладки 742
	static constexpr int32 kAbandonConfirm = 6174;   // «Ты действительно хочешь отказаться от задания?…»
	static constexpr int32 kAbandonFailed  = 6192;   // «Ошибка данных задания.» — сервер отказал в отказе (5007 результат 0)
	static constexpr int32 kAbandonBusy    = 8013;   // «Эта возможность доступна Друзьям.» — 5007 результат 2 (`sub_52EE40`), как у эталона
	static constexpr int32 kQuestDoneLine  = 20080;  // «Задание '[ss1]' выполнено»
	static constexpr int32 kRewardBase     = 12003;  // «Обычная награда»
	static constexpr int32 kRewardChoice   = 12004;  // «Дополнительная награда»
	static constexpr int32 kTrackLabel     = 12199;  // «Отображать информацию»
	static constexpr int32 kTrackLimit     = 7008;   // «Одновременно можно отслеживать до 6 заданий.»
	static constexpr int32 kOwnShop        = 107022; // «Ты в своей лавке»
	static constexpr int32 kExpWord        = 6010;   // «Опыт»
	static constexpr int32 kGoldItemCode   = 20551;  // код предмета «Золото» — им подписывают деньги
	static constexpr int32 kNoWalkArea     = 1013;   // «Недоступная область»
}

namespace QSnd {
// Звуки эталона (`csoundPlayName2DStore(g_pSoundMgr, N, 0)`): 5 открытие, 6 закрытие,
//   12 разворот/сворот страницы и выбор ячейки, 21 переключение слота, 13/14 прогресс квеста.
static constexpr int32 Open = 5, Close = 6, Page = 12, Slot = 21;
}

// ★ ДЕДУП Д1 (`Reports/quest_npc_dedup_audit.md`): точный поиск узла на порт ОДИН —
//   `AsdaUi::FindNodeExact`. Здесь только короткий локальный псевдоним (зовётся 27 раз).
static FORCEINLINE UWidget* Node(const UUserWidget* Host, const TCHAR* Exact)
{
	return AsdaUi::FindNodeExact(Host, Exact);
}
