// Copyright (c) 2026 @sitxovski (Discord)
// AsdaNpcDialogSubsystemInternal.h — ★ 23.10.2026 (разнос AsdaNpcDialogSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaNpcDialogSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала LogAsdaNpcDlg (один экземпляр — DEFINE_LOG_CATEGORY в
//   AsdaNpcDialogSubsystem.cpp) и чистые static-константы/хелперы без состояния — текст байт в байт, у каждой части своя копия.
//   Подключать ПОСЛЕ #include пролога.
#pragma once

#include "CoreMinimal.h"
#include "Npc/AsdaNpcDialogSubsystem.h"

// Категория журнала разговора с NPC: объявление; определение — AsdaNpcDialogSubsystem.cpp (до разноса там был DEFINE_LOG_CATEGORY_STATIC).
DECLARE_LOG_CATEGORY_EXTERN(LogAsdaNpcDlg, Log, All);

static constexpr int32 kDlgZ = 120;   // над HUD, ниже модальных диалогов подтверждения
static const TCHAR* kDtScript = TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcQuestScript.DT_NpcQuestScript");
static const TCHAR* kDtPage   = TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcNsrPage.DT_NpcNsrPage");
// ★ волна 2 п. 7 (14.09.2026): каталог портретов (прежний kPortraitDir) — правило AsdaAssetPaths::NpcPortraitObject.
// Реплики `./Data/NPC/Episode/Talk/<nsrName>.bin` (тег "TAK", 208 б на запись, текст с +5).
//   Их читает ТОЛЬКО ветка опкода 122 (`npcTalkGetEpisodeRecordOff@0xD94D40`).
static const TCHAR* kDtTalk   = TEXT("/Game/AsdaAssets/Text/Dialogue/DT_Talk.DT_Talk");
static const FName  kMqWinId(TEXT("MULTYQUEST_WIN"));
static const FName  kQiWinId(TEXT("QUEST_INFO_WIN"));
static const FName  kRwWinId(TEXT("EPISODEPOP_WIN"));
static const FName  kAcWinId(TEXT("QUEST_Confirm_Win"));

static const TCHAR* kDtWarpList  = TEXT("/Game/AsdaAssets/Data/NPC/DT_WarpList.DT_WarpList");
static const TCHAR* kDtNpcSnq    = TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcSnq.DT_NpcSnq");
static const TCHAR* kDtSnqIct    = TEXT("/Game/AsdaAssets/Data/NPC/DT_SnqIct.DT_SnqIct");
static const TCHAR* kDtCallMob   = TEXT("/Game/AsdaAssets/Data/DT_CallMob.DT_CallMob");
static const TCHAR* kDtEventItem = TEXT("/Game/AsdaAssets/Data/NPC/DT_EventItem.DT_EventItem");
// ★★ 27.09.2026 (itemsfix, СРЕДН-4): СЕРВЕРНАЯ таблица обменов (снимок `EventItem_Table.txt` живого сервера,
//   строка = позиция, как адресует сервер). Только для сторожа `IsEventExchangeServerSafe`.
static const TCHAR* kDtEventItemServer = TEXT("/Game/AsdaAssets/Data/NPC/DT_EventItemServer.DT_EventItemServer");
static constexpr int32 kLocCompleteBusy = 6524;
static constexpr int32 kLocRewardHeader = 20197;

namespace
{
	UAsdaNpcDialogSubsystem* DlgSub(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
	}

	// Эффективная видимость: у свёрнутого РОДИТЕЛЯ ребёнок не рисуется, сколько его ни показывай.
	//   (Закон `false-negatives-method`: «имя есть в дампе» ≠ «контрол виден».)
	//   ⚠ Цепочка `GetParent()` обрывается на КОРНЕВОЙ канве и НЕ доходит до самого `UUserWidget` —
	//   а скрывают окно именно у него (`SetWindowVisible`). Без последнего шага дамп рапортовал
	//   «видим» у контролов закрытого окна.
	bool IsEffectivelyVisible(const UWidget* W)
	{
		for (const UWidget* P = W; P; P = P->GetParent())
		{
			const ESlateVisibility V = P->GetVisibility();
			if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { return false; }
		}
		if (const UUserWidget* Host = W ? W->GetTypedOuter<UUserWidget>() : nullptr)
		{
			const ESlateVisibility V = Host->GetVisibility();
			if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { return false; }
		}
		return true;
	}
}

// ═══ ★ R3-c 12.11.2026: общее для новых частей разреза AsdaNpcDialogSubsystem_Popups.cpp (перенесено дословно) ═══
// Включения пролога AsdaNpcDialogSubsystem_Popups.cpp — нужны перенесённым ниже помощникам во ВСЕХ частях, включая прежние.
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
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "AsdaDataTableCache.h"
#include "World/AsdaEntityText.h"   // ★ волна 3 п.14: имя NPC для заголовка диалога — одна точка
#include "Login/AsdaButtonRelay.h"
#include "Login/AsdaGameText.h"        // FAsdaTextRow — строка DT_Talk (поля npc / line / text)
#include "Npc/AsdaNpcActor.h"
#include "Quest/AsdaQuestTypes.h"
#include "Quest/AsdaQuestGuideTarget.h"   // ★ 31.10.2026: ключ asda.Npc.CellsBeyond10 (ячейки NPC сверх 10, патч сервера N22)
#include "Resources/AsdaTextureSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "Login/AsdaText.h"                 // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): FAsdaText::Loc — подписи отказов по id
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaNotice.h"            // ★ 27.09 (clan_btn_6099_fix): MsgBoxLoc — окна нехватки/отказа обмена, как у эталона
#include "UI/AsdaCommunityFriendSubsystem.h"   // AsdaCommunityUi::ShownVisibilityFor — общая точка видимости узла
#include "UI/AsdaBoosterPanelSubsystem.h"   // ★ гейт 13101: «открыто окно бустера» (эталонное поле +0x3F8)
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 29.09.2026: гейт 1 `NPC_OpenWindow` — окно обмена 2008 открыто (`CExchange+0x2C`)
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
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "Quest/AsdaQuestMarkerSubsystem.h"
#include "Quest/AsdaNsrScriptPage.h"       // ★ 04.11.2026: строка облачка без страницы — одно правило с метками
#include "Quest/AsdaQuestLogSubsystem.h"   // ОБЩАЯ точка `BuildRewardLines` — разбор RewardTable.BIN (второй копии нет)
#include "Data/AsdaEffectMobTableRows.h"
#include "AsdaBytes.h"              // ★ ДЕДУП Д4: чтение little-endian из тела пакета — одна точка на порт
#include "Resources/AsdaSoundSubsystem.h"
#include "UI/AsdaIconCellGrid.h"   // общая точка: прозрачные клик-кнопки поверх JUI-ячеек (строки выбора награды)
#include "UI/AsdaInventorySubsystem.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaCraftWindowSubsystem.h"   // ★ 30009/30088/30010 — окно создания снаряжения 76000 (ремесло, волна В2)
#include "UI/AsdaCraftMakeWindowSubsystem.h"   // ★ хвост смерти entity_char.c:16910 — сброс окна изготовления 77500 (В8)
#include "UI/AsdaCraftSubsystem.h"             // ★ ключ К-9 `asda.Craft.FixDeathRestoreOrder` (В8)
#include "UI/AsdaHuntReportSubsystem.h"         // ★ волна 2-Б: 30068 — окно «Доклад об охоте» 56450
#include "UI/AsdaTownWarSubsystem.h"            // ★ волна 2-Г: 30076 — окно налога города 79300
#include "Npc/AsdaDonationSubsystem.h"         // ★ волна 2-Б: 30051 — пожертвование (нумпад режима 6 → 6132)
#include "TimerManager.h"
#include "EngineUtils.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "HAL/IConsoleManager.h"            // ★ 27.09 itemsfix: asda.EventExchange.ServerGuard
#include "UI/AsdaDisassemblySubsystem.h"    // ★ волна 2-А: окно разборки 32000 видно ⇒ разговор молча не открывается
#include "Text/AsdaLang.h"                  // [ПОРТ] 09.10.2026 (мультиязычность): заголовок/кнопки/реплики сценария на языке сессии
#include "Text/AsdaLangRows.h"              // [ПОРТ] 09.10.2026 (мультиязычность): перевод копии страницы сценария
#include "AsdaNpcDialogSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaNpcDialogSubsystem*.cpp (LogAsdaNpcDlg, чистые хелперы)
#include "Quest/AsdaBoardQuestSubsystem.h"   // ★ 28.10.2026: попап 540 «режим доски» (`uiQuestWin_openReward`), сброс ждущего сдачи

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ОБЛАЧКО ВЫБОРА КВЕСТА `MULTYQUEST_WIN` (2406)
// ════════════════════════════════════════════════════════════════════════════════════════════════
// Поиск узла по ТОЧНОМУ имени (без подстрок): имена берутся из живого дампа WBP.
//   ★ ДЕДУП Д1 (`Reports/quest_npc_dedup_audit.md`): своей копии обхода дерева здесь больше
//   нет — точный поиск на порт ОДИН, `AsdaUi::FindNodeExact`. Короткий локальный псевдоним
//   оставлен, чтобы не переписывать двадцать мест вызова и не терять смысл имени.
static FORCEINLINE UWidget* MqFindExact(const UUserWidget* Host, const TCHAR* Name)
{
	return AsdaUi::FindNodeExact(Host, Name);
}

static constexpr int32 kLocRewardBasic   = 12003;
static constexpr int32 kLocRewardSelect  = 12004;

static constexpr int32 kLocRewardExp   = 6010;

// `QuestReward_SetSlotDisplay` case 0: иконка и имя денег берутся у ПРЕДМЕТА 20551.
static constexpr int32 kMoneyItemCode  = 20551;
// Атласы иконок «опыт» и «харизма» — прямые имена текстур из эталона (`sprintf(iconPath, "exp2")`).
static const TCHAR* kIconExp = TEXT("exp2");
