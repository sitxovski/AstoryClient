// Copyright (c) 2026 @sitxovski (Discord)
// Порт тем справочной подсказки `UI_BuildHelpTooltipText@0xEB11A0` (разбор — шапка `AsdaHelpTopics.h`). [ОРИГИНАЛ]
#include "UI/AsdaHelpTopics.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR — дев-ключи вне Shipping
#include "UI/AsdaMiniStateSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"   // FAsdaDragGhost::IsAnyActive — гейт «предмет в руке»
#include "Engine/GameInstance.h"

// ★ [ИСПРАВЛЕНИЕ] Тема 1 (кнопки листания панели быстрого доступа): эталон берёт тело 12158, если локаль сборки не корейская
//   (`CObj_GetField14(&g_hMainWnd) != 0` — у нашей сборки-эталона 2/6, `validateStringByLocale`), иначе 9102 «Клавиши F1-F5».
//   Строки 12158 нет ни в русских, ни в английских данных игры — оригинал показывал пустое тело. Подставляем 9102 той же
//   функции (листание по умолчанию и правда F1…F5, `system/Shortcutkey.ini` «PAGE 1…5»).
static ASDA_DEV_CVAR(int32, CVarHelpTopic1Fallback,
	TEXT("asda.UI.HelpTopic1Fallback"),
	1,
	TEXT("1 = справка кнопок листания панели быстрого доступа: если строки 12158 нет в данных, тело — 9102 «Клавиши F1-F5»; "
	     "0 = как оригинал (пустое тело)."),
	ECVF_Default);

namespace AsdaHelpTopics
{
	bool Build(const UGameInstance* GI, int32 Topic, FString& OutTitle, FString& OutBody)
	{
		OutTitle.Reset();
		OutBody.Reset();
		switch (Topic)
		{
		case 0:
			// ui_core.c:83322 — заголовок 9143 «Слот оружия», тела нет (плашка: ширина заголовка + 27, высота 20).
			OutTitle = AsdaNotice::Loc(GI, 9143);
			return true;
		case 1:
			// ui_core.c:83329 — заголовок 9144 «Кнопка 'Быстрый выбор'», тело 12158 (не корейская локаль) / 9102 (150×50).
			OutTitle = AsdaNotice::Loc(GI, 9144);
			OutBody = AsdaNotice::Loc(GI, 12158);
			if (OutBody.IsEmpty() && CVarHelpTopic1Fallback.GetValueOnGameThread() != 0) { OutBody = AsdaNotice::Loc(GI, 9102); }
			return true;
		// ── Мини-карта 1700 (`uiBuildHelpTooltipByMsgId@0xD65ED0`, ui_core.c:54944; тексты ui_core.c:83429…83547; плашка 150×50) ──
		case 12: OutTitle = AsdaNotice::Loc(GI, 9155); OutBody = AsdaNotice::Loc(GI, 9125); return true;   // 1708, окно компактное: «Увеличение х2»
		case 13: OutTitle = AsdaNotice::Loc(GI, 9156); OutBody = AsdaNotice::Loc(GI, 9126); return true;   // 1706, зум выключен: «Увеличить»
		case 14: OutTitle = AsdaNotice::Loc(GI, 9157); OutBody = AsdaNotice::Loc(GI, 9128); return true;   // 1704: «Закрыть мини-карту»
		case 15: OutTitle = AsdaNotice::Loc(GI, 9158); OutBody = AsdaNotice::Loc(GI, 9129); return true;   // 1707: «Вся карта ('m')»
		case 25: OutTitle = AsdaNotice::Loc(GI, 9156); OutBody = AsdaNotice::Loc(GI, 9127); return true;   // 1706, зум включён: «Свернуть»
		case 26: OutTitle = AsdaNotice::Loc(GI, 9166); OutBody = AsdaNotice::Loc(GI, 9167); return true;   // 1708, окно развёрнуто: «Обычный размер»
		case 27: OutBody = AsdaNotice::Loc(GI, 9169); return true;                                          // 1719 индикатор (180×50, без заголовка)
		// ── Панель отряда 2200 (`uiShowHelpTooltipByControlId@0xB9B9C0`, ui_core.c:11514; тексты ui_core.c:83477…83508) ──
		case 18: OutTitle = AsdaNotice::Loc(GI, 9161); return true;   // 2262: «Кнопка 'Выгнать'» (100×20)
		case 19: OutTitle = AsdaNotice::Loc(GI, 9162); return true;   // 2260: «Кнопка 'Уйти'»
		case 20: OutTitle = AsdaNotice::Loc(GI, 9163); return true;   // 2279: «Кнопка 'Закрыть'»
		case 21: OutTitle = AsdaNotice::Loc(GI, 9164); return true;   // 2209+5k значок главы: «Глава отряда»
		case 22:                                                      // 2210+5k имя: «Член отряда» + две строки мыши (180×90)
			OutTitle = AsdaNotice::Loc(GI, 9165);
			OutBody = AsdaNotice::Loc(GI, 9140) + TEXT("\n") + AsdaNotice::Loc(GI, 9141);
			return true;
		default:
			return false;
		}
	}

	bool Show(UGameInstance* GI, int32 Topic, EAsdaTipOwner Owner)
	{
		UAsdaMiniStateSubsystem* Mini = GI ? GI->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr;
		FString Title, Body;
		if (!Mini || !Build(GI, Topic, Title, Body) || (Title.IsEmpty() && Body.IsEmpty())) { return false; }
		Mini->ShowSharedTip(Title, Body, Owner);
		return true;
	}

	bool ShowText(UGameInstance* GI, int32 TitleLoc, int32 BodyLoc, EAsdaTipOwner Owner)
	{
		UAsdaMiniStateSubsystem* Mini = GI ? GI->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr;
		const FString Title = TitleLoc > 0 ? AsdaNotice::Loc(GI, TitleLoc) : FString();
		const FString Body = BodyLoc > 0 ? AsdaNotice::Loc(GI, BodyLoc) : FString();
		if (!Mini || (Title.IsEmpty() && Body.IsEmpty())) { return false; }
		Mini->ShowSharedTip(Title, Body, Owner);
		return true;
	}
}

void FAsdaHelpTipHover::Update(UGameInstance* GI, EAsdaTipOwner Owner, int32 Key, TFunctionRef<bool()> Show, bool bDragGate)
{
	if (Key == HoverKey) { return; }   // событие 9: тот же контрол — плашку ведёт тик тултипа
	if (ShownKey >= 0) { Hide(GI, Owner); }   // событие 0xA прежнему
	HoverKey = Key;
	if (Key < 0 || (bDragGate && FAsdaDragGhost::IsAnyActive())) { return; }
	if (Show()) { ShownKey = Key; }      // событие 8 новому
}

void FAsdaHelpTipHover::UpdateTopic(UGameInstance* GI, EAsdaTipOwner Owner, int32 Key, int32 Topic)
{
	Update(GI, Owner, Key, [GI, Owner, Topic]() { return Topic >= 0 && AsdaHelpTopics::Show(GI, Topic, Owner); });
}

void FAsdaHelpTipHover::UpdateText(UGameInstance* GI, EAsdaTipOwner Owner, int32 Key, int32 TitleLoc, int32 BodyLoc)
{
	Update(GI, Owner, Key, [GI, Owner, TitleLoc, BodyLoc]() { return AsdaHelpTopics::ShowText(GI, TitleLoc, BodyLoc, Owner); });
}

void FAsdaHelpTipHover::Hide(UGameInstance* GI, EAsdaTipOwner Owner)
{
	if (ShownKey >= 0)
	{
		// ★ Гасит ТОЛЬКО владелец (закон `ui-shared-tip-ownership`).
		if (UAsdaMiniStateSubsystem* Mini = GI ? GI->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr) { Mini->HideSharedTip(Owner); }
	}
	ShownKey = -1;
	HoverKey = -1;
}
