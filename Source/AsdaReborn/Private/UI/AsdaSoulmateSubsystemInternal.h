// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSoulmateSubsystemInternal.h — ★ R2 10.11.2026 (разрез AsdaSoulmateSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaSoulmateSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaSoulmateSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaSoulmateSubsystem.h"
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "Net/NetOpcodes.h"   // OP_SOULMATE_UPDATE_INFO / OP_SOULMATE_WND_APPLY_INFO_PACKET
#include "AsdaBytes.h"
#include "Misc/ScopeExit.h"   // ★ 26.10.2026: ON_SCOPE_EXIT — выход из защиты повторного входа панели 6500
#include "UI/AsdaWindowSubsystem.h"
// GetSelfCharName — общая точка «своё имя» (ветка 5203 код 2)   ★ R3: комментарий снятого включения "World/AsdaWorldSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaSoulAppleSubsystem.h"   // ★ ревизия 1-Б С-1: яблочная часть inventoryHidePanelsAndRefresh
// ★ 23.10.2026: панель занятости 79900 — под панелью Друга 6500   ★ R3: комментарий снятого включения "UI/AsdaMinimapSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaSoulmateSkillSubsystem.h"
#include "UI/AsdaSoulRouletteSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "Login/AsdaLoginData.h"
#include "UI/AsdaConstellationSubsystem.h"   // SignName / MatchIndex / SelfSign — влечение знаков
#include "UI/AsdaPartySubsystem.h"           // 8265 «Вступить в отряд» — гейты живут ТАМ
#include "UI/AsdaUiHelpers.h"                // ParsePlaneName / ApplyTabState / FormatPrintfArgs
// ★ ОБЩАЯ ТОЧКА работы с узлами окна 6400 (`namespace AsdaCommunityUi`): `Find`/`FindLabel`/
//   `SetTxt`/`SetButtonCaption`/`SetVisByPrefix`/`ShownVisibilityFor`. Заводить здесь свои копии
//   поиска по `<idc>`/`<idc>_grp`/`<idc>_Text` нельзя — закон `port-shared-points`.
#include "UI/AsdaCommunityFriendSubsystem.h"
#include "UI/AsdaCommunitySubsystem.h"   // ★ 24.10.2026 (sm-offline): окно 6400 открыто и активна вкладка «Друг» — гейт автозапроса 2244
#include "Text/AsdaLang.h"               // ★ 24.10.2026 (sm-offline): формат «Последнего выхода» 6522 по языку
// DT_smcharexp — таблица опыта Дружбы   ★ R3: комментарий снятого включения "Items/AsdaItemTableSubsystem.h" (файл им не пользовался) — сохранён
#include "Items/AsdaItemDbRows.h"            // FAsdaSmcharexpRow
#include "World/AsdaCoord.h"                  // ★ волна 1-А: мир → тайл (`helper_scaleIfNonzero`/`ratioIfFieldSet`) для 9104
// ★ волна 1-А: гейт показа 8200 — элитный магазин 6000 (`helper_getField2075(g_pShopWnd)`)   ★ R3: комментарий снятого включения "UI/AsdaPremiumShopSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaCraftSubsystem.h"           // ★ волна 1-А: гейт показа 8200 — окна ремесла (`g_pItemMainWnd`/`g_pItemQtyMakeDlg`)
#include "Engine/DataTable.h"
#include "Containers/Ticker.h"   // ★ 23.10.2026: отложенная (на кадр) пересборка отметки Couple после сброса сессии
#include "Components/Button.h"
#include "Components/CheckBox.h"   // UCheckBox — галки блока «доступ к персонажу» 6543..6546
#include "Components/TextBlock.h"
#include "Components/EditableText.h"
#include "Components/MultiLineEditableText.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "HAL/IConsoleManager.h"
#include "Framework/Application/SlateApplication.h"   // ★ 10.10.2026: снятие фокуса с поля 8302 (`cFindSoulmateWinHide`)
#include "Engine/GameInstance.h"                    // ★ 10.10.2026: таймер следующего кадра (фокус поля 8302)
#include "TimerManager.h"                           // FTimerManager::SetTimerForNextTick
#include "AsdaSelf.h"                      // ★ Дедуп №2, В1-B (U5a): «это я?» — `AsdaSelf::IsSelfUserIdx`
#include "UI/AsdaJuiWindowHandle.h"        // ★ Дедуп №2, В1-B (U22): каркас `EnsureLazy` окон 6500/8200/8300/8400/9100

// Категория журнала частей AsdaSoulmateSubsystem*.cpp: определение — AsdaSoulmateSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaSoulmate, Log, All );

// Ключ CVarSoulmatePopFrom5220: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarSoulmatePopFrom5220;
#else
extern TAsdaConstCVar<int32> CVarSoulmatePopFrom5220;
#endif

// Ключ CVarSoulmateBarFullFromBack: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarSoulmateBarFullFromBack;
#else
extern TAsdaConstCVar<int32> CVarSoulmateBarFullFromBack;
#endif

// Ключ CVarSoulmateOfflineInfo: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarSoulmateOfflineInfo;
#else
extern TAsdaConstCVar<int32> CVarSoulmateOfflineInfo;
#endif
// Ключ CVarSoulmateLocationText: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarSoulmateLocationText;
#else
extern TAsdaConstCVar<int32> CVarSoulmateLocationText;
#endif

// Ключ CVarSoulmateOfflineFriendInfo: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarSoulmateOfflineFriendInfo;
#else
extern TAsdaConstCVar<int32> CVarSoulmateOfflineFriendInfo;
#endif

// Ключ CVarSoulmateOfflineAfterLeave: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<float> CVarSoulmateOfflineAfterLeave;
#else
extern TAsdaConstCVar<float> CVarSoulmateOfflineAfterLeave;
#endif
// Ключ CVarSoulmateOldSoulGridReplyTtl: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<float> CVarSoulmateOldSoulGridReplyTtl;
#else
extern TAsdaConstCVar<float> CVarSoulmateOldSoulGridReplyTtl;
#endif
// Ключ CVarSoulmateOfflineLeaveRetry: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarSoulmateOfflineLeaveRetry;
#else
extern TAsdaConstCVar<int32> CVarSoulmateOfflineLeaveRetry;
#endif

// Ключ CVarSoulmateSelfEchoGuard: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarSoulmateSelfEchoGuard;
#else
extern TAsdaConstCVar<int32> CVarSoulmateSelfEchoGuard;
#endif

namespace {
	// ★★★ ПЛАН КОНТРОЛА ПО ЯКОРНОМУ ИМЕНИ. Импортёр JUI даёт точное имя `idc` только интерактиву
	//   и Text-контролам, а планам — `{idc}_{индексОбхода}_{номерПлана}` (закон
	//   `jui-importer-widget-naming`). Индекс обхода сдвигается от любой правки разметки, поэтому
	//   ищем ПО РАЗОБРАННОМУ ИМЕНИ (`AsdaUi::ParsePlaneName`), а не по подстроке: так `SOUL_GAUGE01`
	//   не поймает `SOUL_GAUGE01x`, а `Find_SMate_ListInt01` — не поймает `..._MARK1`.
	UImage* PlaneOf(const UUserWidget* Host, const FString& Idc, int32 PlaneNo)
	{
		UWidgetTree* Tree = Host ? Host->WidgetTree : nullptr;
		if (!Tree) { return nullptr; }
		UImage* Found = nullptr;
		Tree->ForEachWidget([&Found, &Idc, PlaneNo](UWidget* W)
		{
			if (Found || !W) { return; }
			UImage* Img = Cast<UImage>(W);
			if (!Img) { return; }
			FString ParsedIdc; int32 JuiIdx = 0, Plane = 0;
			if (!AsdaUi::ParsePlaneName(Img->GetName(), ParsedIdc, JuiIdx, Plane)) { return; }
			if (Plane == PlaneNo && ParsedIdc.Equals(Idc, ESearchCase::IgnoreCase)) { Found = Img; }
		});
		return Found;
	}

	// ★ ПОЛОСА ИЗ ДВУХ ПЛАНОВ (закон `ui-progressbar-animate-existing`): план ES0 — подложка,
	//   ES1 — заливка, которую клипуем по ширине. Полную ширину снимаем ОДИН раз здесь: после
	//   первого клипа в ноль её было бы уже не прочитать (грабля EXP-бара мини-статуса).
	bool BindBarFill(const UUserWidget* Host, const TCHAR* Idc, FAsdaMiniBarFill& Out)
	{
		const FString Name(Idc);
		Out.Back = PlaneOf(Host, Name, 0);
		Out.Fill = PlaneOf(Host, Name, 1);
		if (!Out.Fill)
		{
			// Скин с ОДНИМ планом: заливка и есть этот план (подложку рисует родитель).
			Out.Fill = Out.Back;
			Out.Back = nullptr;
		}
		FVector2D Pos, Size;
		if (Out.Fill && UAsdaMiniStateSubsystem::GetSlotRect(Out.Fill, Pos, Size))
		{
			Out.BaseLeft = Pos.X;
			Out.FullW    = Size.X;
		}
		// ★★★ 23.10.2026 [ИСПРАВЛЕНИЕ порта] (самотест панели Друга): ширина плана заливки ES1 в ассете — ДИЗАЙН-процент
		//   разметки (6508/6509: 55.3 из 83, 6504: 60.5 из 121), а не «полная полоса»: HP/MP Друга упирались в 2/3, опыт
		//   Дружбы — в половину. 100 % = ширина подложки ES0 — то же правило, что у полос мини-статуса (`MakeBar`).
		//   Ключ `asda.Soulmate.BarFullFromBack 0` — прежнее (по ширине заливки).
		FVector2D BPos, BSize;
		if (CVarSoulmateBarFullFromBack.GetValueOnGameThread() != 0 && Out.Back
			&& UAsdaMiniStateSubsystem::GetSlotRect(Out.Back, BPos, BSize) && BSize.X > 0.f)
		{
			Out.FullW = BSize.X;
		}
		return Out.IsValid();
	}
}
