// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWindowSubsystem_ZOrder.cpp — ★ R2 10.11.2026 (разрез AsdaWindowSubsystem.cpp, дословный перенос): зоны UI, topmost, Z-порядок, владельцы окон, BringWindowToFront.
//   Общее для частей — в AsdaWindowSubsystemInternal.h; шапка и реверс — в AsdaWindowSubsystem.cpp.
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiVisibilitySubsystem.h"   // ★ 21.10.2026: Alt+Z — флаг «интерфейс скрыт» (владелец)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "UI/AsdaLocIds.h"   // Дд2/Дд3: нотисы кнопок ОБЩИХ окон (ONEBUT/TWOBUT/SHOUTING) — одно место
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/EditableText.h"   // поле ввода 1583 диалога SHOUTING_WIN
#include "Components/CheckBox.h"         // ★ приёмка: галки под ручкой (INVEN_NEW_WIN 83503)
#include "Components/EditableTextBox.h"  // ★ приёмка: поля ввода под ручкой
#include "Components/MultiLineEditableText.h"  // ★ приёмка: многострочный ввод
#include "Components/ComboBoxString.h"   // ★ приёмка: выпадающие списки
#include "Components/Slider.h"           // ★ приёмка: ползунки тянут мышь сами
#include "Components/ScrollBox.h"        // ★ приёмка: прокрутка тянет мышь сама
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"   // DPI-скейл вьюпорта: абсолютная дельта курсора → юниты канвы
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Image.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#if !UE_BUILD_SHIPPING   // ★ 31.10.2026: журнал синтетического клика — дев-диагностика, в Shipping не входит
#include "Widgets/Input/SButton.h"         // ★ 31.10.2026: состояние кнопки под синтетическим кликом (журнал `[симклик]`)
#include "Types/ReflectionMetadata.h"   // ★ 31.10.2026: имя виджета под синтетическим кликом (журнал `[симклик]`)
#endif
#include "Fonts/FontMeasure.h"   // метрики строки для asda.UI.DumpTextMetrics   // фокус на игровой вьюпорт при базовом GameAndUI
#include "Misc/ConfigCacheIni.h"   // раскладка окон между сессиями: секция [AsdaWindowLayout] в ini игры
#include "Login/AsdaLoginData.h"   // FAsdaLoginData::Loc — подписи кнопок ТОЛЬКО из таблицы DT_GameText
// игровые loc-подписи (2288 «ОК» / 2289 «Отмена») для TWOBUT_MSGBOX   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaUiHelpers.h"         // D1b: общая `AsdaUi::WidgetAbsRect` (хит-тест окна и рамки)
// D2: фолбэк однокнопочного msgbox, когда WBP не загрузился   ★ R3: комментарий снятого включения "UI/AsdaMessageSubsystem.h" (файл им не пользовался) — сохранён
// шаг 13 `uiCloseAllWindows`: блокировка/разблокировка хотбара   ★ R3: комментарий снятого включения "UI/AsdaHotbarSubsystem.h" (файл им не пользовался) — сохранён
// шаги 23-24: сворачивание трея и запрет его кнопок   ★ R3: комментарий снятого включения "UI/AsdaSystemMenuSubsystem.h" (файл им не пользовался) — сохранён
#include "Resources/AsdaSoundSubsystem.h" // звуки открытия/закрытия окна количества (5 / 6)
// ★ 27.09.2026: панель 1530 для `DropTopMsgBoxBeforeNotice`   ★ R3: комментарий снятого включения "UI/AsdaItemTooltipSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaPurchaseBoxSubsystem.h"  // ★ 27.09.2026: панель 1559 для `DropTopMsgBoxBeforeNotice`
#include "Styling/SlateStyleRegistry.h"   // ★ гашение синей рамки фокуса: подмена кисти FocusRectangle
#include "Styling/SlateStyle.h"
#include "Styling/AppStyle.h"
#include "Brushes/SlateNoResource.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "Login/AsdaText.h"                 // FAsdaText::Loc — строки диалога `asda.UI.ConfirmPreview` (07.10.2026)
#include "Login/AsdaLoginGameMode.h"          // ★ 08.10.2026: подъём при показе не трогает экран логина
#include "World/AsdaWorldKind.h"               // ★ F7 (дедуп №2, В3): общий признак «сцена логина»
#include "GameFramework/GameModeBase.h"     // GetAuthGameMode — проверка экрана логина
#include "UnrealClient.h"                   // С-1 (07.10.2026): FViewport::ViewportResizedEvent — смена размера вьюпорта
#include "Blueprint/GameViewportSubsystem.h" // М-1 (07.10.2026): слот окна во вьюпорте — полноэкранный ли он
#include "Widgets/SOverlay.h"               // ★ 08.10.2026: подъём окна БЕЗ снятия с вьюпорта (слот оверлея вьюпорта)
#include "UI/AsdaNotice.h"                  // ★ 08.10.2026 (ревизия В3 рыбалки): нотис 6835 строгого нумпада — LocRaw / ChatColored
// ★ 08.10.2026: ColGold — цвет −11514 нотиса 6835   ★ R3: комментарий снятого включения "UI/AsdaChatSubsystem.h" (файл им не пользовался) — сохранён
// ★ 09.10.2026: UAsdaRichText::ApplyFormatted — теги <FC=…> в теле окон-сообщений   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "Components/RichTextBlock.h"      // ★ 09.10.2026: тело окна-сообщения после первой подмены — URichTextBlock
#include "AsdaWindowSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWindowSubsystem*.cpp (LogAsdaWindow, чистые хелперы)
#include "AsdaWindowSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaWindowSubsystem*.cpp

// ═══════════════════════ РЕЕСТР ЗОН ИНТЕРФЕЙСА ═══════════════════════════════════════════════
// Единая точка правды «курсор над интерфейсом?» для AAsdaPlayerController::OnLeftClick.
// Подробное обоснование — в шапке объявления (AsdaWindowSubsystem.h).
void UAsdaWindowSubsystem::RegisterUiZone(FName ZoneId, FAsdaUiZoneRect RectProvider)
{
	if (ZoneId.IsNone() || !RectProvider.IsBound()) { return; }
	const bool bNew = (UiZones.Find(ZoneId) == nullptr);
	UiZones.Add(ZoneId, MoveTemp(RectProvider));
	if (bNew)
	{
		UE_LOG(LogAsdaWindow, Log, TEXT("[UI-зона] зарегистрирована «%s» (всего зон: %d)"),
			*ZoneId.ToString(), UiZones.Num());
	}
}

void UAsdaWindowSubsystem::UnregisterUiZone(FName ZoneId)
{
	if (UiZones.Remove(ZoneId) > 0)
	{
		UE_LOG(LogAsdaWindow, Log, TEXT("[UI-зона] снята «%s» (осталось: %d)"),
			*ZoneId.ToString(), UiZones.Num());
	}
}

// ═══════════ СЛОЙ «ПОВЕРХ ВСЕГО» (обоснование — в шапке объявления) ══════════════════════════
void UAsdaWindowSubsystem::PushTopmost(FName ClaimId, FName OwnerWindowId, FAsdaUiZoneRect RectProvider)
{
	if (ClaimId.IsNone() || OwnerWindowId.IsNone()) { return; }
	if (TopmostClaims.Contains(ClaimId)) { return; }          // идемпотентно

	FTopmostClaim Claim;
	Claim.Owner = OwnerWindowId;
	Claim.Rect = RectProvider;
	Claim.SavedZ = 0;
	if (const FAsdaWindowEntry* E = Windows.Find(OwnerWindowId)) { Claim.SavedZ = E->ZOrder; }
	TopmostClaims.Add(ClaimId, Claim);

	// ★ Поднимаем именно КОРНЕВОЙ виджет в вьюпорте — только этот z и решает спор двух окон.
	//   Каждый следующий слой встаёт на единицу выше предыдущего (стек всплывашек).
	SetWindowZOrder(OwnerWindowId, kTopmostZ + TopmostClaims.Num());
	// ★ И регистрируем зону перехвата мирового клика по САМОМУ слою: всплывашка выходит за корпус
	//   окна, её прямоугольник в зону окна не входит, без этого клик по списку падал бы в мир.
	if (RectProvider.IsBound()) { RegisterUiZone(ClaimId, MoveTemp(RectProvider)); }

	UE_LOG(LogAsdaWindow, Log,
		TEXT("[поверх-всего] слой «%s» поднят: окно «%s» z %d → %d (активных слоёв: %d)"),
		*ClaimId.ToString(), *OwnerWindowId.ToString(), Claim.SavedZ,
		kTopmostZ + TopmostClaims.Num(), TopmostClaims.Num());
}

void UAsdaWindowSubsystem::PopTopmost(FName ClaimId)
{
	FTopmostClaim Claim;
	if (!TopmostClaims.RemoveAndCopyValue(ClaimId, Claim)) { return; }
	UnregisterUiZone(ClaimId);
	SetWindowZOrder(Claim.Owner, Claim.SavedZ);
	UE_LOG(LogAsdaWindow, Log,
		TEXT("[поверх-всего] слой «%s» снят: окно «%s» z возвращён на %d (осталось слоёв: %d)"),
		*ClaimId.ToString(), *Claim.Owner.ToString(), Claim.SavedZ, TopmostClaims.Num());
}

bool UAsdaWindowSubsystem::IsTopmostActive(FName ClaimId) const
{
	return TopmostClaims.Contains(ClaimId);
}

bool UAsdaWindowSubsystem::IsPointerBlockedByTopmost(FName MyWindowId) const
{
	if (TopmostClaims.Num() == 0 || !FSlateApplication::IsInitialized()) { return false; }
	const FVector2D Cur = FSlateApplication::Get().GetCursorPos();
	for (const TPair<FName, FTopmostClaim>& Kv : TopmostClaims)
	{
		if (Kv.Value.Owner == MyWindowId) { continue; }        // свой же слой мышь не блокирует
		FVector2D Mn, Mx;
		if (!AsdaReadZoneRect(Kv.Value.Rect, Mn, Mx)) { continue; }   // ★ 31.10.2026: чистый накопитель (`asda.UI.ZoneRectInit`)
		if (Cur.X >= Mn.X && Cur.X <= Mx.X && Cur.Y >= Mn.Y && Cur.Y <= Mx.Y) { return true; }
	}
	return false;
}

bool UAsdaWindowSubsystem::IsCursorOverHigherWindow(FName MyWindowId) const
{
	// Разбор — у объявления. Хит-тест окна тот же, что у `IsCursorOverWindow` и у подъёма в `TickDrag`.
	if (!FSlateApplication::IsInitialized()) { return false; }
	const FAsdaWindowEntry* Mine = Windows.Find(MyWindowId);
	if (!Mine) { return false; }
	const FVector2D Cur = FSlateApplication::Get().GetCursorPos();
	for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
	{
		if (Kv.Key == MyWindowId) { continue; }
		const FAsdaWindowEntry& E = Kv.Value;
		if (!E.Widget || E.ZOrder <= Mine->ZOrder || E.ZOrder >= kTopmostZ) { continue; }
		const ESlateVisibility WV = E.Widget->GetVisibility();
		if (WV == ESlateVisibility::Collapsed || WV == ESlateVisibility::Hidden || WV == ESlateVisibility::HitTestInvisible) { continue; }
		FName Hit;
		if (HitTestWidgetTree(E.Widget->GetRootWidget(), E.NoHitWidgets, Cur, Hit, AsdaDrawnBodyFor(Kv.Key, E))) { return true; }
	}
	return false;
}

FString UAsdaWindowSubsystem::DebugTopmost() const
{
	// ★ ПОПРАВКА ФИНАЛЬНОЙ ПРИЁМКИ (СРЕДН-5в): шапка обещала больше, чем показывала:
	//   здесь ТОЛЬКО `TopmostClaims` — ни модального блокера, ни окон из полосы `kTopmostZ`.
	//   Модальный гейт теперь печатается ОТДЕЛЬНОЙ строкой дампа (`asda.UI.DumpZones`),
	//   а окна полосы — в шаге 2 «ЗОН ИНТЕРФЕЙСА» со своим z. Заголовок сужен до правды.
	if (TopmostClaims.Num() == 0) { return TEXT("  ЗАЯВКИ PushTopmost: нет активных (модальный гейт — отдельной строкой)"); }
	FString S = FString::Printf(TEXT("  ЗАЯВКИ PushTopmost (%d):"), TopmostClaims.Num());
	for (const TPair<FName, FTopmostClaim>& Kv : TopmostClaims)
	{
		FVector2D Mn(0, 0), Mx(0, 0);
		const bool bR = AsdaReadZoneRect(Kv.Value.Rect, Mn, Mx);   // ★ 31.10.2026: то же чтение, что у шлюза
		int32 NowZ = -1;
		if (const FAsdaWindowEntry* E = Windows.Find(Kv.Value.Owner)) { NowZ = E->ZOrder; }
		S += FString::Printf(TEXT("\n    %-18s владелец «%s» z=%d (был %d) rect=%s(%.0f,%.0f)-(%.0f,%.0f)"),
			*Kv.Key.ToString(), *Kv.Value.Owner.ToString(), NowZ, Kv.Value.SavedZ,
			bR ? TEXT("") : TEXT("НЕТ "), Mn.X, Mn.Y, Mx.X, Mx.Y);
	}
	return S;
}

bool UAsdaWindowSubsystem::GetUiZoneRect(FName ZoneId, FVector2D& OutMin, FVector2D& OutMax) const
{
	const FAsdaUiZoneRect* D = UiZones.Find(ZoneId);
	if (!D || !D->IsBound()) { return false; }
	return AsdaReadZoneRect(*D, OutMin, OutMax);   // ★ 31.10.2026: чистый накопитель (`asda.UI.ZoneRectInit`)
}

bool UAsdaWindowSubsystem::IsCursorOverWindow(FName WindowId) const
{
	if (!FSlateApplication::IsInitialized()) { return false; }
	if (UAsdaUiVisibilitySubsystem::HiddenBlocksUiHitTest(this)) { return false; }   // ★ 21.10.2026 (Alt+Z): окна скрыты
	const FAsdaWindowEntry* E = Windows.Find(WindowId);
	if (!E || !E->Widget) { return false; }
	const ESlateVisibility WV = E->Widget->GetVisibility();
	if (WV == ESlateVisibility::Collapsed || WV == ESlateVisibility::Hidden) { return false; }
	FName Hit;
	return HitTestWidgetTree(E->Widget->GetRootWidget(), E->NoHitWidgets,
		FSlateApplication::Get().GetCursorPos(), Hit, AsdaDrawnBodyFor(WindowId, *E));
}

bool UAsdaWindowSubsystem::IsCursorOverAnyUiWindow(FName& OutZone) const
{
	OutZone = NAME_None;
	if (!FSlateApplication::IsInitialized()) { return false; }
	return IsScreenPointOverAnyUiWindow(FSlateApplication::Get().GetCursorPos(), OutZone);
}

bool UAsdaWindowSubsystem::IsScreenPointOverAnyUiWindow(const FVector2D& Cur, FName& OutZone) const
{
	FName Widget;
	return IsScreenPointOverAnyUiWindowEx(Cur, OutZone, Widget);
}

bool UAsdaWindowSubsystem::IsScreenPointOverAnyUiWindowEx(const FVector2D& Cur, FName& OutZone, FName& OutWidget) const
{
	OutZone = NAME_None;
	OutWidget = NAME_None;
	// ★ 21.10.2026 (Alt+Z) [ИСПРАВЛЕНИЕ]: у оригинала невидимые окна ловили клики и подсказки — при скрытом интерфейсе клик идёт в мир.
	if (UAsdaUiVisibilitySubsystem::HiddenBlocksUiHitTest(this)) { return false; }
	// 1) Явно зарегистрированные зоны (всплывающие слои PushTopmost и прочее, чего нет в .JUI).
	for (const TPair<FName, FAsdaUiZoneRect>& Kv : UiZones)
	{
		if (!Kv.Value.IsBound()) { continue; }
		FVector2D Mn, Mx;
		if (!AsdaReadZoneRect(Kv.Value, Mn, Mx)) { continue; }   // ★ 31.10.2026: чистый накопитель (`asda.UI.ZoneRectInit`)
		if (Cur.X >= Mn.X && Cur.X <= Mx.X && Cur.Y >= Mn.Y && Cur.Y <= Mx.Y)
		{
			OutZone = Kv.Key;
			// ★ 31.10.2026 (волна hud_click) [ИСПРАВЛЕНИЕ ПОРТА]: ИМЯ — ПО Z. Ручные зоны проверялись раньше окон и без z: над «OK»
			//   окна подтверждения 1559 шлюз называл магазин 15300 ПОД ним (его зона). Решение «окно/мир» от этого не менялось,
			//   но имя врало журналам и проверкам. Окно зоны — одноимённое либо до точки («CAMPWAR_Point.49732»); окно реестра
			//   выше него, поймавшее точку, называется вместо зоны. Ключ — общий `asda.UI.HitTestPlanesOnly`.
			if (CVarHitTestPlanesOnly.GetValueOnGameThread() != 0)
			{
				FString Base = Kv.Key.ToString();
				int32 Dot = INDEX_NONE;
				if (Base.FindChar(TEXT('.'), Dot)) { Base.LeftInline(Dot); }
				const FAsdaWindowEntry* ZE = Windows.Find(FName(*Base));
				const int32 ZoneZ = ZE ? ZE->ZOrder : MIN_int32;
				int32 BestZ = ZoneZ;
				for (const TPair<FName, FAsdaWindowEntry>& W : Windows)
				{
					const FAsdaWindowEntry& E = W.Value;
					if (!E.Widget || E.ZOrder <= BestZ) { continue; }
					const ESlateVisibility WV = E.Widget->GetVisibility();
					if (WV == ESlateVisibility::Collapsed || WV == ESlateVisibility::Hidden) { continue; }
					FName Hit;
					if (HitTestWidgetTree(E.Widget->GetRootWidget(), E.NoHitWidgets, Cur, Hit, AsdaDrawnBodyFor(W.Key, E))) { BestZ = E.ZOrder; OutZone = W.Key; OutWidget = Hit; }
				}
			}
			return true;
		}
	}
	// 2) ★★★ ВСЕ ОТКРЫТЫЕ ОКНА РЕЕСТРА — автоматически, без ручной регистрации зон.
	//    Раньше зону регистрировали вручную, и это делали РОВНО 2 окна из ~260: клик проваливался
	//    в мир сквозь всё остальное. Теперь окно попадает под хит-тест самим фактом регистрации.
	//    Прямоугольники берутся по ЖИВОЙ геометрии виджетов, поэтому перетаскивание, растягивание,
	//    смена разрешения и DPI учитываются сами.
	for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
	{
		const FAsdaWindowEntry& E = Kv.Value;
		if (!E.Widget) { continue; }
		const ESlateVisibility WV = E.Widget->GetVisibility();
		if (WV == ESlateVisibility::Collapsed || WV == ESlateVisibility::Hidden) { continue; }
		FName Hit;
		if (HitTestWidgetTree(E.Widget->GetRootWidget(), E.NoHitWidgets, Cur, Hit, AsdaDrawnBodyFor(Kv.Key, E)))
		{
			OutZone = Kv.Key;
			OutWidget = Hit;
			return true;
		}
	}
	return false;
}

// ★★★ ЖИВОЙ ДЕФЕКТ 2026-08-06: «окно настройки цвета каналов уходит на задний план при вызове».
//   ПРИЧИНА НАЙДЕНА ЗДЕСЬ. Палитра 2700 открывается кликом ПО ОБРАЗЦУ ЦВЕТА, который лежит
//   ВНУТРИ окна 2590. В том же кадре `TickDrag` разбирает фронт нажатия сверху вниз по z,
//   находит под курсором окно 2590 (у него есть ручка перетаскивания) и зовёт эту функцию.
//   Прежняя реализация считала «верх» по ВСЕМ окнам, включая только что открытую палитру
//   (z 830), и ставила редактору z 831 — то есть НАД его же дочерним окном. Визуально это
//   ровно то, что описал юзер: палитра «ушла назад, фокус вернулся на предыдущее окно».
//   ЛЕЧЕНИЕ (оно же — воспроизведение эталона `uiShowChildWindow`, ui_core.c:11605, который
//   регистрирует 2700 верхним окном): потомки исключаются из расчёта верха и после подъёма
//   владельца ПЕРЕСТАВЛЯЮТСЯ над ним.
int32 UAsdaWindowSubsystem::GetWindowZOrder(FName WindowId) const
{
	const FAsdaWindowEntry* E = Windows.Find(WindowId);
	return E ? E->ZOrder : -1;
}

// ★★★ Полоса «приоритетных» окон — требование владельца 2026-08-26 (коробки/бустеры всегда впереди).
//   Обоснование и порядок слоёв — у объявления `kPriorityZ` в заголовке.
void UAsdaWindowSubsystem::SetWindowPriority(FName WindowId, bool bPriority)
{
	FAsdaWindowEntry* E = Windows.Find(WindowId);
	if (!E || !E->Widget) { return; }
	const bool bIsPriority = (E->ZOrder >= kPriorityZ && E->ZOrder <= kPriorityZMax);
	if (bPriority == bIsPriority) { return; }        // уже в нужном состоянии — идемпотентно

	if (bPriority)
	{
		// Внутри полосы — по порядку появления: окно, открытое позже, ложится поверх предыдущего
		//   (два бустера подряд не спорят за один z).
		int32 Z = kPriorityZ;
		for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
		{
			if (Kv.Key == WindowId) { continue; }
			if (Kv.Value.ZOrder >= kPriorityZ && Kv.Value.ZOrder <= kPriorityZMax)
			{
				Z = FMath::Max(Z, Kv.Value.ZOrder + 1);
			}
		}
		Z = FMath::Min(Z, kPriorityZMax);
		SetWindowZOrder(WindowId, Z);
		UE_LOG(LogAsdaWindow, Log,
			TEXT("[окна] «%s» переведено в полосу приоритетных: z=%d (обычные окна выше %d не поднимаются)"),
			*WindowId.ToString(), Z, kPriorityZ - 1);
	}
	else
	{
		if (CVarAlwaysTopBand.GetValueOnGameThread() != 0)
		{
			// ★ Ревизия (СРЕДН-1): 8799 теперь над полосой AlwaysTop — возвращаем на верх СВОЕЙ полосы (обычной или AlwaysTop).
			SetWindowZOrder(WindowId, E->bAlwaysTopRuntime ? kAlwaysTopZ : kRaiseBandFloor);
			BringWindowToFront(WindowId);
		}
		else
		{
			SetWindowZOrder(WindowId, kPriorityZ - 1);
		}
		UE_LOG(LogAsdaWindow, Log, TEXT("[окна] «%s» возвращено в обычный слой"), *WindowId.ToString());
	}
	RaiseOwnedAbove(WindowId);   // дочерние окна обязаны остаться выше владельца
}

// ★★★ 31.10.2026 [ИСПРАВЛЕНИЕ ПОРТА] (волна hud_click, жалоба владельца «нажал „Купить“ в окне подтверждения покупки магазина
//   бустеров — клик прошёл сквозь окно»): ПОКАЗ ОКНА ПОЛОСЫ ПРИОРИТЕТНЫХ [8800, 8899] — НАВЕРХ ПОЛОСЫ.
//   ПРИЧИНА. Место в полосе выдаётся ОДИН раз — `SetWindowPriority` при создании виджета (окно подтверждения 1559, окно «Вид»
//   15600, панели бустеров), а явный подъём при показе (`BringWindowToFront`) полосу 8800+ не трогал вовсе. Окно, созданное
//   ПОЗЖЕ, навсегда оставалось выше созданного раньше: покупка → «Вид» → снова «Купить» — подтверждение 1559 открывалось ПОД окном
//   «Вид», и нажатие по нему получало окно сверху. Эталон держит 1559 модальным (`helper_setupFields(..., 1)`, item_enhance.c:2768) —
//   поверх всего, что открыто. Теперь показ окна полосы поднимает его над прочими окнами полосы (свои дочерние — не в счёт),
//   у потолка полоса сжимается с сохранением порядка. 1 (по умолчанию) — так; 0 — прежнее: место в полосе только при создании.
static ASDA_DEV_CVAR(int32, CVarPriorityBandRaise,
	TEXT("asda.UI.PriorityBandRaise"), 1,
	TEXT("1 (по умолчанию) — явный подъём окна полосы приоритетных (8800…8899: окно подтверждения покупки 1559, «Вид» 15600, ")
	TEXT("панели бустеров) ставит его над прочими окнами полосы; 0 — прежнее: место в полосе выдаётся один раз при создании ")
	TEXT("(окно, созданное позже, навсегда выше — подтверждение покупки открывалось под окном «Вид»)."),
	ECVF_Default);

void UAsdaWindowSubsystem::BringWindowToFront(FName WindowId)
{
	FAsdaWindowEntry* E = Windows.Find(WindowId);
	if (!E || !E->Widget) { return; }
	// ★ 31.10.2026 (`asda.UI.PriorityBandRaise`, разбор — у объявления ключа): окно полосы приоритетных — наверх полосы.
	if (!E->bModal && E->ZOrder >= kPriorityZ && E->ZOrder <= kPriorityZMax && CVarPriorityBandRaise.GetValueOnGameThread() != 0)
	{
		auto TopOfBand = [this, WindowId]() -> int32
		{
			int32 Top = kPriorityZ - 1;
			for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
			{
				if (Kv.Key == WindowId || IsOwnedBy(Kv.Key, WindowId) || Kv.Value.bModal) { continue; }
				if (Kv.Value.ZOrder >= kPriorityZ && Kv.Value.ZOrder <= kPriorityZMax) { Top = FMath::Max(Top, Kv.Value.ZOrder); }
			}
			return Top;
		};
		int32 Top = TopOfBand();
		if (Top >= E->ZOrder)
		{
			if (Top + 1 > kPriorityZMax)
			{
				// Потолок полосы: переложить окна полосы подряд с kPriorityZ, порядок прежний (иначе подъём сошёлся бы в ничью).
				TArray<TPair<int32, FName>> Band;
				for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
				{
					if (!Kv.Value.bModal && Kv.Value.ZOrder >= kPriorityZ && Kv.Value.ZOrder <= kPriorityZMax) { Band.Emplace(Kv.Value.ZOrder, Kv.Key); }
				}
				Band.Sort([](const TPair<int32, FName>& A, const TPair<int32, FName>& B) { return A.Key < B.Key; });
				for (int32 i = 0; i < Band.Num(); ++i) { if (Band[i].Key != kPriorityZ + i) { SetWindowZOrder(Band[i].Value, kPriorityZ + i); } }
				E = Windows.Find(WindowId);
				if (!E || !E->Widget) { return; }
				Top = TopOfBand();
			}
			SetWindowZOrder(WindowId, FMath::Min(Top + 1, kPriorityZMax));
			UE_LOG(LogAsdaWindow, Log, TEXT("[окна] «%s» показано наверх полосы приоритетных: z=%d (asda.UI.PriorityBandRaise)"),
				*WindowId.ToString(), FMath::Min(Top + 1, kPriorityZMax));
		}
		RaiseOwnedAbove(WindowId);
		return;
	}
	// ★★★ Открытие поверх всех (08.10.2026): явный подъём (порт `CClass_MapInsert`) МЕЖДУ нажатием по телу другого окна и
	//   отпусканием — это окно, открытое щелчком: тело на отпускании встанет ПОД него (`RaisedDuringPress`, разбор в `TickDrag`).
	if (!PendingRaiseWindowId.IsNone() && PendingRaiseWindowId != WindowId && CVarOpenOnTop.GetValueOnGameThread() != 0)
	{
		RaisedDuringPress.AddUnique(WindowId);
	}
	// ★★★ РЕВИЗИЯ ПОДЪЁМА (СРЕДН-1, 07.10.2026): ДВЕ ПОЛОСЫ — обычная [700, 8600) и AlwaysTop [8600, 8699] (обоснование у kAlwaysTopZ).
	if (CVarAlwaysTopBand.GetValueOnGameThread() != 0)
	{
		// Модальные, приоритетные 8800+, всплывающие 9000+ — свои слои, подъём их не касается (как и прежде).
		if (E->bModal || E->ZOrder >= kPriorityZ) { RaiseOwnedAbove(WindowId); return; }
		if (E->bAlwaysTopRuntime)
		{
			// Показ AlwaysTop = в конец списка (верх полосы); уже верх — ничего.
			const int32 Want = NextAlwaysTopZ(WindowId);   // верх полосы БЕЗ самого окна + 1
			E = Windows.Find(WindowId);
			const bool bAlreadyTop = E && E->ZOrder >= kAlwaysTopZ && E->ZOrder <= kAlwaysTopZMax && E->ZOrder >= Want;
			if (E && !bAlreadyTop) { SetWindowZOrder(WindowId, Want); }
			RaiseOwnedAbove(WindowId);
			return;
		}
		// Дочернее окно владельца-AlwaysTop живёт в его полосе; 8700…8799 — всплывающие окна порта (меню по игроку): не трогаем.
		if (E->ZOrder > kAlwaysTopZMax || (E->ZOrder >= kAlwaysTopZ && HasAlwaysTopOwner(WindowId))) { RaiseOwnedAbove(WindowId); return; }
		// Обычное окно оказалось в полосе AlwaysTop (снят флаг, старое место) — его место наверху обычного слоя.
		const bool bForce = E->ZOrder >= kAlwaysTopZ;
		auto TopOfNormal = [this, WindowId]() -> int32
		{
			int32 Top = kRaiseBandFloor - 1;
			for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
			{
				if (Kv.Key == WindowId || IsOwnedBy(Kv.Key, WindowId) || Kv.Value.bModal) { continue; }
				if (Kv.Value.ZOrder >= kAlwaysTopZ || Kv.Value.bAlwaysTopRuntime) { continue; }   // AlwaysTop и выше — не «верх» обычного слоя
				Top = FMath::Max(Top, Kv.Value.ZOrder);
			}
			return Top;
		};
		int32 TopZ = TopOfNormal();
		if (bForce || E->ZOrder < TopZ)
		{
			if (TopZ + 1 >= kAlwaysTopZ - 1 && CVarZCompact.GetValueOnGameThread() != 0)
			{
				CompactRaiseBand(*FString::Printf(TEXT("подъём «%s» у потолка %d"), *WindowId.ToString(), kAlwaysTopZ - 1));
				E = Windows.Find(WindowId);
				if (!E || !E->Widget) { return; }
				TopZ = TopOfNormal();
			}
			if (bForce || E->ZOrder < TopZ) { SetWindowZOrder(WindowId, FMath::Min(FMath::Max(TopZ + 1, kRaiseBandFloor), kAlwaysTopZ - 1)); }
		}
		RaiseOwnedAbove(WindowId);
		return;
	}
	if (E->bAlwaysTop) { return; }   // прежняя редакция (ключ AlwaysTopBand 0): AlwaysTop из меты — неподвижный якорь
	// Верх ОБЫЧНОГО слоя: ниже полосы приоритетных окон (kPriorityZ), слоя всплывашек и всего выше.
	int32 TopZ = E->ZOrder;
	for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
	{
		if (Kv.Key == WindowId || Kv.Value.ZOrder >= kPriorityZ) { continue; }
		// ★ СВОИ ПОТОМКИ В РАСЧЁТ «ВЕРХА» НЕ ИДУТ — иначе владелец каждый раз перепрыгивал бы
		//   собственное дочернее окно и накрывал его.
		if (IsOwnedBy(Kv.Key, WindowId)) { continue; }
		TopZ = FMath::Max(TopZ, Kv.Value.ZOrder);
	}
	if (E->ZOrder < TopZ)
	{
		// ★★★ ВОЛНА 1-А (повторная ревизия 1А-5 п.3): у потолка — СЖАТИЕ полосы, иначе окна сходились на 8799 и подъём
		//   переставал работать (ничья по z ⇒ `E->ZOrder < TopZ` ложно). После сжатия «верх» считается заново.
		if (TopZ + 1 >= kPriorityZ - 1 && CVarZCompact.GetValueOnGameThread() != 0)
		{
			CompactRaiseBand(*FString::Printf(TEXT("подъём «%s» у потолка %d"), *WindowId.ToString(), kPriorityZ - 1));
			E = Windows.Find(WindowId);
			if (!E || !E->Widget) { return; }
			TopZ = E->ZOrder;
			for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
			{
				if (Kv.Key == WindowId || Kv.Value.ZOrder >= kPriorityZ || IsOwnedBy(Kv.Key, WindowId)) { continue; }
				TopZ = FMath::Max(TopZ, Kv.Value.ZOrder);
			}
		}
		// ★ ПОТОЛОК ОБЫЧНОГО СЛОЯ — под полосой приоритетных окон (kPriorityZ), а не под всплывашками:
		//   иначе обычное окно, поднятое кликом, перекрывало бы коробку/бустер (требование владельца).
		if (E->ZOrder < TopZ) { SetWindowZOrder(WindowId, FMath::Min(TopZ + 1, kPriorityZ - 1)); }
	}
	// Даже если владелец не двигался — потомки обязаны остаться выше него.
	RaiseOwnedAbove(WindowId);
}

// Объявить окно дочерним: сразу поставить его над владельцем и запомнить связь.
void UAsdaWindowSubsystem::SetWindowOwner(FName WindowId, FName OwnerWindowId)
{
	FAsdaWindowEntry* E = Windows.Find(WindowId);
	if (!E) { return; }
	if (WindowId == OwnerWindowId) { return; }                 // сам себе владельцем быть не может
	if (!OwnerWindowId.IsNone() && IsOwnedBy(OwnerWindowId, WindowId))
	{
		UE_LOG(LogAsdaWindow, Warning,
			TEXT("[окна] «%s» уже потомок «%s» — цикл владения не заводим"),
			*OwnerWindowId.ToString(), *WindowId.ToString());
		return;
	}
	E->OwnerWindowId = OwnerWindowId;
	if (!OwnerWindowId.IsNone()) { RaiseOwnedAbove(OwnerWindowId); }
}

bool UAsdaWindowSubsystem::IsOwnedBy(FName Candidate, FName Root) const
{
	FName Cur = Candidate;
	for (int32 Guard = 0; Guard < 16; ++Guard)      // лимит глубины = защита от битой цепочки
	{
		const FAsdaWindowEntry* E = Windows.Find(Cur);
		if (!E || E->OwnerWindowId.IsNone()) { return false; }
		if (E->OwnerWindowId == Root) { return true; }
		Cur = E->OwnerWindowId;
	}
	return false;
}

void UAsdaWindowSubsystem::RaiseOwnedAbove(FName OwnerId)
{
	const FAsdaWindowEntry* Owner = Windows.Find(OwnerId);
	if (!Owner) { return; }
	const int32 OwnerZ = Owner->ZOrder;
	// Собираем детей заранее: SetWindowZOrder меняет карту (RemoveFromParent/AddToViewport).
	TArray<FName> Kids;
	for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
	{
		if (Kv.Value.OwnerWindowId == OwnerId) { Kids.Add(Kv.Key); }
	}
	// ★★★ ПОПРАВКА РЕВИЗИИ 25.09.2026 (СРЕДНЕЕ-1): У КАЖДОГО ПОТОМКА СВОЙ z, И ПОТОМКА НЕ ОПУСКАЮТ.
	//   БЫЛО: всем потомкам присваивался ОДИН И ТОТ ЖЕ `OwnerZ + 1`. Живой прогон это и показал:
	//   `Pet_Tooltips_Win` 745→733 и `Pet_Button_Win` 742→733 — одинаковый z у двух окон.
	//   Пока их спасал взаимоисключающий гейт 43058/43300 (под открытой панелью подсказка не
	//   всплывает), но это СЛУЧАЙНОСТЬ, а не правило: два одновременно видимых потомка спорили бы
	//   за порядок добавления во вьюпорт.
	//   ВТОРАЯ ПОЛОВИНА ПОПРАВКИ: `!=` заменено на «поднимаем, только если потомок НЕ ВЫШЕ».
	//   Прежний `if (K->ZOrder != Want)` СТАСКИВАЛ ВНИЗ потомка, уже поднятого собственным
	//   `BringWindowToFront` — например подсказку 43300, которую показ обязан класть поверх всего
	//   обычного слоя (порт `CClass_MapInsert`, `ui_core.c:90437`). Владелец от этого и страдал.
	//   Сортировка по текущему z — чтобы порядок между потомками сохранялся, а не зависел от
	//   обхода карты.
	Kids.Sort([this](const FName& A, const FName& B)
	{
		const FAsdaWindowEntry* EA = Windows.Find(A);
		const FAsdaWindowEntry* EB = Windows.Find(B);
		return (EA ? EA->ZOrder : 0) < (EB ? EB->ZOrder : 0);
	});
	int32 Step = 1;
	for (const FName& Kid : Kids)
	{
		FAsdaWindowEntry* K = Windows.Find(Kid);
		if (!K || !K->Widget) { continue; }
		// Модальный потомок живёт в полосе kModalZ и и так выше всего — не трогаем.
		if (K->bModal || K->ZOrder >= kModalZ) { RaiseOwnedAbove(Kid); continue; }
		// ! ПОПРАВКА РЕВИЗИИ (МЕЛКОЕ): потолок — ПОД ПОЛОСОЙ ПРИОРИТЕТНЫХ окон, а не под слоем
		//   всплывашек. Прежний `kTopmostZ - 1` позволял потомку владельца, стоящего у верхней
		//   границы обычного слоя (8799), провалиться В полосу приоритетных (8800…8899) и накрыть
		//   коробку или бустер. `BringWindowToFront` тем же числом и ограничен (`kPriorityZ - 1`).
		// ★ Ревизия (СРЕДН-1): потомок обычного окна не уходит в полосу AlwaysTop.
		//   Ревизия (МЕЛК-Р1): потомок владельца из полосы AlwaysTop не уходит выше 8699 (в полосу всплывающих 8700+).
		const bool bBand = CVarAlwaysTopBand.GetValueOnGameThread() != 0;
		const int32 KidCeil = (bBand && OwnerZ < kAlwaysTopZ) ? kAlwaysTopZ - 1
			: (bBand && OwnerZ <= kAlwaysTopZMax) ? kAlwaysTopZMax : kPriorityZ - 1;
		const int32 Want = FMath::Min(OwnerZ + Step, KidCeil);
		++Step;
		if (K->ZOrder < Want) { SetWindowZOrder(Kid, Want); }   // только ПОДНИМАЕМ, не опускаем
		RaiseOwnedAbove(Kid);      // внуки — ещё на ступень выше
	}
}
