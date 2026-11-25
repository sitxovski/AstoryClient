// Copyright (c) 2026 @sitxovski (Discord)
// AsdaCharaInfoSubsystem_Private.h — ★ R3-c 12.11.2026 (разрез AsdaCharaInfoSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaCharaInfoSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaCharaInfoSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaCharaInfoSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaExchangeP2PSubsystem.h"   // ★ 29.09.2026 (волна О1): функции 202/203 — заявка обмена
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 30.09.2026 (волна Л5 торговли): гейт «лавка открыта» 22000/23000 → 107022
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "AsdaSelf.h"   // ★ волна 1: «это я?» / мой номер / мой уровень — общая точка ядра (E2, E4)
#include "UI/AsdaEffectsSubsystem.h"   // ★ 29.10.2026: реквизит эмоций своего игрока (SpawnEmoteProps)
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "UI/AsdaTitleSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaHudSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaMarkup.h"   // ★ UAsdaRichText::ApplyFormatted — ЕДИНАЯ точка тегов <FC=…>
#include "UI/AsdaCommunityFriendSubsystem.h"   // AsdaCommunityUi::ShownVisibilityFor — общая точка видимости узла
#include "UI/AsdaConstellationSubsystem.h"      // HoldGameTables — удержание DT_Constellation (15.09.2026, dt_gc_hold)
#include "UI/AsdaSelfStoreSubsystem.h"          // функция 2/7 «личная лавка» (волна Л1 торговли, 29.09.2026)
#include "Resources/AsdaSoundSubsystem.h"
#include "AsdaNetSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "UI/AsdaPartySubsystem.h"     // ★ 25.10.2026: функции 1/x (отряд) и 2/6 (общая цель)
#include "UI/AsdaPlayerMenuSubsystem.h" // ★ 25.10.2026: функция 2/5 «осмотр» — общий запрос 5478 с пунктом меню 2006
#include "World/AsdaFishingSubsystem.h"   // В6 рыбалки: навык (GetFishingSkill / IsFishingSkillKnown / OnFishSkillChanged) — только чтение
#include "UI/AsdaInventorySubsystem.h"    // В6 рыбалки: надетая удочка (контейнер 3, экип-слот 9)
#include "Login/AsdaButtonRelay.h"
#include "Login/AsdaLoginData.h"   // [ПОРТ] 08.10.2026: лимит «о себе» в байтах провода (TruncateToWireLimit)

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/EditableText.h"   // поле ввода 8302 окна «Обо мне»
#include "Components/RichTextBlock.h"   // дамп: после ApplyFormatted строка становится URichTextBlock
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "UI/AsdaCraftSubsystem.h"   // ★ волна В3 ремесла: единый предикат «окно ремесла открыто» (`IsAnyCraftWindowOpen`)
#include "UI/AsdaWarShopSubsystem.h"   // ★ 08.11.2026 (пачка 15, баг 9): имя воинского звания `RankName` (эталон `sub_935854`)
#include "World/AsdaSkillCastLock.h"   // ★ 05.11.2026: эмоция во время своего навыка не играет (эталон: только состояние 0)
#include "UI/AsdaCloseGear.h"   // ★ 08.11.2026: шестерёнка у крестика — общая точка (asda.UI.CloseGearHover)

// Категория журнала частей AsdaCharaInfoSubsystem*.cpp: определение — AsdaCharaInfoSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaCharInfo, Log, All );

static const FName  kCharInfoWinId(TEXT("CHARAINFO_WIN"));

namespace CharInfoLoc {
	static constexpr int32 kRowFirst     = 12023;   // 12023…12028 — подписи 12300…12305

	static constexpr int32 kStatFirst    = 12033;   // 12033…12045 — подписи 12310…12322

	static constexpr int32 kCraftLabel   = 5501;    // «Ур. ремесла» — переписывает 12044

	static constexpr int32 kTitleTip     = 2432;    // подсказка кнопки звания
}
