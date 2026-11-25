// Copyright (c) 2026 @sitxovski (Discord)
// AsdaMinimapSubsystem_Private.h — ★ R3-c 12.11.2026 (разрез AsdaMinimapSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaMinimapSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaMinimapSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaMinimapSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaWarehouseSubsystem.h"   // ★ 30.09.2026 (ревизия Л4/Л5, СРЕДН-1): свиток возврата склада (+0x5734) — гейт 10305
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "UI/AsdaUiHelpers.h"   // ★ D1: единая sRGB→linear конверсия (дедуп 2026-08-08)
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaMiniStateSubsystem.h"   // ★ 31.10.2026: общая плашка подсказки значка цели задания     // ★ 30.10.2026: EAsdaTipOwner::Minimap — владелец справки кнопок
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 30.09.2026 (волна Л5 торговли): гейты «лавка открыта» (17500 → 107022, автопуть молча)
#include "World/AsdaMapRegistry.h"   // ★ волна 2 (14.09.2026): реестр карт — генерируемый заголовок из Tools/data/asda_map_registry.json (области карты мира, кроп городов, континент, имя ассета)

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiVisibilitySubsystem.h"   // ★ 06.11.2026: Alt+Z — скрытая мини-карта курсор не меняет
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"   // ★★★ ЕДИНЫЙ маршрутизатор сообщений (нотис 1013 «Недоступная область»)
#include "Resources/AsdaSoundSubsystem.h"     // карта мира: звуки открытия 5 / закрытия 6
#include "Resources/AsdaTextureSubsystem.h"   // ★ Аудит №3 (A8/B8): маркеры и подложки — через менеджер
#include "AsdaNetSubsystem.h"                 // каналы: g_nCurrentChannel/g_nChannelCount + 6084
#include "UI/AsdaIconCellGrid.h"              // FindCell — общий поиск узла по префиксу idc
#include "Login/AsdaButtonRelay.h"            // клик/ховер 15 строк каналов одним обработчиком
#include "Quest/AsdaQuestMarkerSubsystem.h"    // ★ NextPortalStep — ЕДИНЫЙ расчёт шага через порталы
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaWalkGrid.h"
#include "World/AsdaGmOriginalSubsystem.h"   // ★ 09.11.2026 GM-телепорт Ctrl+ЛКМ по карте (штатная 6010)
#include "World/AsdaMapTable.h"


#include "Blueprint/WidgetTree.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Engine/Texture2D.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "UI/AsdaCraftSubsystem.h"   // ★ волна В3 ремесла: единый предикат «окно ремесла открыто» (`IsAnyCraftWindowOpen`)
#include "World/AsdaFishingSubsystem.h"   // ★ 08.10.2026 (рыбалка В4): гейт «идёт рыбалка» кнопок окна карты
#include "Text/AsdaLang.h"                 // [ПОРТ] 09.10.2026 (мультиязычность): запасное имя карты на языке сессии
#include "UI/AsdaSoulmateSubsystem.h"      // ★ 23.10.2026: занятость — под панелью Друга 6500 (IsPopCardShown / виджет)
#include "UI/AsdaPartySubsystem.h"         // ★ 26.10.2026: гейт смены канала «в отряде» (98004)
#include "UI/AsdaNotice.h"                 // ★ 26.10.2026: нотисы отказа смены канала (чат + окно-сообщение)
#include "UI/AsdaCampWarBattleSubsystem.h"   // ★ 30.10.2026: 17520 по HUD боя «Битвы армий» и клик → 10047 → 6731
#include "UI/AsdaTutorialEvents.h"   // ★ 10.11.2026 обучение: флаги шагов гида 1950 (TUTORIAL.md §2.3)

// Категория журнала частей AsdaMinimapSubsystem*.cpp: определение — AsdaMinimapSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaMinimap, Log, All );

static bool AsdaMinimapFishingGate(const UGameInstance* GI, const TCHAR* Ctrl)
{
	if (!UAsdaFishingSubsystem::IsFishingNow(GI)) { return false; }   // ★ В3 S7 (N14): одна точка
	UE_LOG(LogAsdaMinimap, Log, TEXT("[миникарта] %s: идёт рыбалка — событие поглощено (g:374375)"), Ctrl);
	return true;
}

// AsdaDestMarkClearOn: определение — в домашней части (★ R3-c 12.11.2026).
bool AsdaDestMarkClearOn();

// Ключ CVarOccupationResetNodes: определение — в домашней части (★ R3-c 12.11.2026, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarOccupationResetNodes;
#else
extern TAsdaConstCVar<int32> CVarOccupationResetNodes;
#endif

// ── Хелперы слота CanvasPanel ────────────────────────────────────────────────────────────────────
static UCanvasPanelSlot* SlotOf(UWidget* W) { return W ? AsdaUi::SlotOf(W) : nullptr; }

// Локальный пиксель клика внутри окна карты. Проект рисует UI при DPI = 1.0 (jui-align-anchors),
//   поэтому viewport-координаты мыши и слейтовые пиксели совпадают 1:1.
static bool ClickLocalPx(UObject* WCO, UWidget* View, FVector2D& OutLocal)
{
	if (!View) { return false; }
	const UWorld* W = WCO ? WCO->GetWorld() : nullptr;
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	if (!PC) { return false; }
	float MX = 0.f, MY = 0.f;
	if (!PC->GetMousePosition(MX, MY)) { return false; }
	const FGeometry& Geo = View->GetCachedGeometry();
	FVector2D Origin, ViewportSize;
	USlateBlueprintLibrary::LocalToViewport(WCO, Geo, FVector2D::ZeroVector, Origin, ViewportSize);
	OutLocal = FVector2D(MX, MY) - Origin;
	return true;
}

// ── ★★★ УГОЛ КОНУСА ОБЗОРА — ОБЩАЯ ТОЧКА `AsdaUi::CameraConeYaw` ────────────────────────────────
//   Тело переехало в `AsdaUiHelpers.cpp` (2026-08-22): за тем же углом пришёл экранный трекер
//   заданий 1800 (`Reports/quest_tracker.md` §4.1), и второй копии быть не должно — именно
//   копия когда-то и дала баг «конус стоит намертво». Здесь остаётся короткий локальный псевдоним,
//   чтобы не менять три места вызова ниже.
static FORCEINLINE float AsdaCameraConeYaw(const AAsdaPlayerCharacter* P) { return AsdaUi::CameraConeYaw(P); }
