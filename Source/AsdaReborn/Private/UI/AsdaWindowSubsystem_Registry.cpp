// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWindowSubsystem_Registry.cpp — ★ R2 10.11.2026 (разрез AsdaWindowSubsystem.cpp, дословный перенос): отладка подписей/ручек/зон, колесо, закрытие окон NPC, реестр окон Open/Register/Close.
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
#include "UI/AsdaHotbarSubsystem.h"      // шаг 13 `uiCloseAllWindows`: блокировка/разблокировка хотбара
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

// ★★★ 26.10.2026 [ИСПРАВЛЕНИЕ] (волна silaris): `OpenWindow` не возвращает виджет, умерший с прежним миром (см. там).
static ASDA_DEV_CVAR(int32, CVarWindowReopenOrphan,
	TEXT("asda.Window.ReopenOrphan"), 1,
	TEXT("1 (по умолчанию) — OpenWindow снимает запись реестра, чей виджет умер со сменой мира (вне вьюпорта, без родителя),\n")
	TEXT("и создаёт окно заново (иначе «поднять окно заново» на экране загрузки L_Loading крутится без конца — зависание\n")
	TEXT("панели Друга 6500 на портале); 0 — прежнее: вернуть мёртвый виджет из реестра."),
	ECVF_Default);
// Аксессор ключа (Shipping: константа 1; имя ключа в рабочий код не попадает).
static bool AsdaWindowReopenOrphanOn() { return CVarWindowReopenOrphan.GetValueOnGameThread() != 0; }

// sRGB-hex «AARRGGBB» из линейного цвета — чтобы строку из лога можно было СРАВНИТЬ глазами
//   со строкой скина `JuiSkin.ini` (там цвета в sRGB). Обратная к `AsdaUi::SrgbHexToLinear`.
static FString AsdaLinearToSrgbHex(const FLinearColor& C)
{
	// ★ Обратная кривая — общая точка `AsdaUi::LinearToSrgbChannel` (раньше была выписана здесь
	//   копией; сведено, `fix_helpers_dedup.md`).
	auto Ch = [](float V)
	{
		return (int32)FMath::RoundToInt(AsdaUi::LinearToSrgbChannel(FMath::Clamp(V, 0.f, 1.f)) * 255.f);
	};
	return FString::Printf(TEXT("%02X%02X%02X%02X"),
		(int32)FMath::RoundToInt(FMath::Clamp(C.A, 0.f, 1.f) * 255.f), Ch(C.R), Ch(C.G), Ch(C.B));
}

FString UAsdaWindowSubsystem::DebugCaptionStates(FName WindowId) const
{
	FString Out = TEXT("  СОСТОЯНИЯ ПОДПИСИ КНОПОК (кадры скина 0/1/2 → цвет и смещение текста)");
	int32 Shown = 0;
	for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
	{
		if (!WindowId.IsNone() && Kv.Key != WindowId) { continue; }
		UUserWidget* Win = Kv.Value.Widget;
		if (!Win || !Win->WidgetTree) { continue; }
		Out += FString::Printf(TEXT("\n    окно %-22s реле=%d"), *Kv.Key.ToString(), Kv.Value.CaptionRelays.Num());
		Win->WidgetTree->ForEachWidget([&](UWidget* W)
		{
			UButton* B = Cast<UButton>(W);
			if (!B) { return; }
			const FButtonStyle& St = B->GetStyle();
			if (!St.NormalForeground.IsColorSpecified()) { return; }
			UTextBlock* Cap = Cast<UTextBlock>(Win->GetWidgetFromName(FName(*(B->GetName() + TEXT("_Text")))));
			FVector2D CapPos(0, 0);
			FString CurHex = TEXT("—");
			FString CapVis = TEXT("нет подписи");
			if (Cap)
			{
				CurHex = AsdaLinearToSrgbHex(Cap->GetColorAndOpacity().GetSpecifiedColor());
				if (const UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(Cap->Slot)) { CapPos = CS->GetPosition(); }
				CapVis = Cap->GetVisibility() == ESlateVisibility::SelfHitTestInvisible
					? TEXT("SelfHitTestInvisible") : *UEnum::GetValueAsString(Cap->GetVisibility());
			}
			Out += FString::Printf(
				TEXT("\n      %-22s скин N=%s H=%s P=%s сдвиг(0)=(%.0f,%.0f) сдвиг(2)=(%.0f,%.0f) | сейчас цвет=%s поз=(%.1f,%.1f) подпись=%s | наведена=%s нажата=%s"),
				*B->GetName(),
				*AsdaLinearToSrgbHex(St.NormalForeground.GetSpecifiedColor()),
				*AsdaLinearToSrgbHex(St.HoveredForeground.GetSpecifiedColor()),
				*AsdaLinearToSrgbHex(St.PressedForeground.GetSpecifiedColor()),
				St.NormalPadding.Left, St.NormalPadding.Top, St.PressedPadding.Left, St.PressedPadding.Top,
				*CurHex, CapPos.X, CapPos.Y, *CapVis,
				B->IsHovered() ? TEXT("да") : TEXT("нет"), B->IsPressed() ? TEXT("да") : TEXT("нет"));
			++Shown;
		});
	}
	if (Shown == 0)
	{
		Out += TEXT("\n    кнопок со состояниями подписи не найдено (окно не переимпортировано либо скин без состояний)");
	}
	return Out;
}

bool UAsdaWindowSubsystem::DebugGetDragHandleRect(FName WindowId, int32 Index, FVector2D& OutMin, FVector2D& OutMax) const
{
	const FAsdaWindowEntry* E = Windows.Find(WindowId);
	if (!E || !E->Widget || !E->DragHandles.IsValidIndex(Index)) { return false; }
	const UWidget* HW = E->Widget->GetWidgetFromName(E->DragHandles[Index].HandleWidget);
	return HW && WidgetAbsRect(HW, OutMin, OutMax) && !(OutMax - OutMin).IsNearlyZero();
}

FString UAsdaWindowSubsystem::DebugDragHandles() const
{
	FString S = FString::Printf(TEXT("  ПЕРЕТАСКИВАНИЕ: открытых окон %d, тянем сейчас: %s"),
		Windows.Num(), DragWindowId.IsNone() ? TEXT("нет") : *DragWindowId.ToString());
	const UAsdaJuiWindowMetaAsset* Meta = GetJuiMeta();
	S += FString::Printf(TEXT("\n    мета JUI: %s"),
		Meta ? *FString::Printf(TEXT("окон %d"), Meta->Windows.Num()) : TEXT("НЕ ЗАГРУЖЕНА"));
	const FVector2D Cur = FSlateApplication::IsInitialized()
		? FSlateApplication::Get().GetCursorPos() : FVector2D::ZeroVector;
	for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
	{
		const FAsdaWindowEntry& E = Kv.Value;
		S += FString::Printf(TEXT("\n    %-22s z=%-5d мета=%s ручек=%d NoSelect=%d сдвиг=(%.0f,%.0f)%s"),
			*Kv.Key.ToString(), E.ZOrder, E.bHasMeta ? TEXT("да") : TEXT("НЕТ"),
			E.DragHandles.Num(), E.NoHitWidgets.Num(), E.DragOffset.X, E.DragOffset.Y,
			GDragDisabledWindows().Contains(Kv.Key) ? TEXT(" [drag запрещён юзером]") : TEXT(""));
		for (const FAsdaJuiDragHandle& H : E.DragHandles)
		{
			UWidget* HW = E.Widget ? E.Widget->GetWidgetFromName(H.HandleWidget) : nullptr;
			FVector2D Mn(0, 0), Mx(0, 0);
			const bool bR = HW && WidgetAbsRect(HW, Mn, Mx);
			const bool bOver = bR && Cur.X >= Mn.X && Cur.X <= Mx.X && Cur.Y >= Mn.Y && Cur.Y <= Mx.Y;
			S += FString::Printf(TEXT("\n        ручка %-30s %s (%.0f,%.0f)-(%.0f,%.0f) курсор=%s цель=%s%s%s%s"),
				*H.HandleWidget.ToString(), HW ? TEXT("") : TEXT("[ВИДЖЕТА НЕТ] "),
				Mn.X, Mn.Y, Mx.X, Mx.Y, bOver ? TEXT("ДА") : TEXT("нет"),
				*H.TargetControl.ToString(),
				H.bTargetIsWindowRoot ? TEXT(" (всё окно)") : TEXT(""),
				H.bOriginalBugFixed ? TEXT(" [починен баг оригинала]") : TEXT(""),
				H.bAddedByPort ? TEXT(" [НАША ДОБАВКА, asda.UI.AddedDragHandles]") : TEXT(""));
		}
	}
	return S;
}

bool UAsdaWindowSubsystem::IsCursorOverAnyUiWindow() const
{
	FName Dummy;
	return IsCursorOverAnyUiWindow(Dummy);
}

// ★★★ ПОПРАВКА ФИНАЛЬНОЙ ПРИЁМКИ (СРЕДН-5а): ДАМП ПЕЧАТАЛ НЕ ТО, ПО ЧЕМУ ИДЁТ РЕШЕНИЕ.
//   БЫЛО: перебирался ТОЛЬКО `UiZones` — ручной реестр зон, а само решение «курсор над UI»
//   берёт ЕЩЁ и ВСЕ ОТКРЫТЫЕ ОКНА реестра (`IsCursorOverAnyUiWindow`, шаг 2). Поэтому при ТРЁХ
//   открытых окнах дамп печатал «не зарегистрировано ни одной» — тот же ложный «нет», из-за
//   которого родился ложный диагноз по окну 44200 (закон `false-negatives-method`).
//   СТАЛО: печатаются ОБА источника ровно тем же порядком, каким их смотрит решение.
FString UAsdaWindowSubsystem::DebugUiZones() const
{
	const FVector2D Cur = FSlateApplication::IsInitialized()
		? FSlateApplication::Get().GetCursorPos() : FVector2D::ZeroVector;
	FString S = FString::Printf(TEXT("  ЗОНЫ ИНТЕРФЕЙСА, курсор (%.0f,%.0f):"
		"\n    — шаг 1, РУЧНОЙ реестр зон (%d) —"), Cur.X, Cur.Y, UiZones.Num());
	if (UiZones.Num() == 0) { S += TEXT("\n    (пусто)"); }
	for (const TPair<FName, FAsdaUiZoneRect>& Kv : UiZones)
	{
		FVector2D Mn, Mx;
		if (AsdaReadZoneRect(Kv.Value, Mn, Mx))   // ★ 31.10.2026: то же чтение, что у шлюза (`asda.UI.ZoneRectInit`)
		{
			const bool bOver = (Cur.X >= Mn.X && Cur.X <= Mx.X && Cur.Y >= Mn.Y && Cur.Y <= Mx.Y);
			S += FString::Printf(TEXT("\n    %-20s (%.0f,%.0f)-(%.0f,%.0f) курсор над ней=%s"),
				*Kv.Key.ToString(), Mn.X, Mn.Y, Mx.X, Mx.Y, bOver ? TEXT("ДА") : TEXT("нет"));
		}
		else
		{
			S += FString::Printf(TEXT("\n    %-20s прямоугольника нет (окно скрыто/не построено)"), *Kv.Key.ToString());
		}
	}
	// ★ ШАГ 2 — то, что решение смотрит ПОСЛЕ ручного реестра: ВСЕ открытые окна.
	S += FString::Printf(TEXT("\n    — шаг 2, ОТКРЫТЫЕ ОКНА РЕЕСТРА (%d) —"), Windows.Num());
	int32 Shown = 0;
	for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
	{
		const FAsdaWindowEntry& E = Kv.Value;
		if (!E.Widget || !E.Widget->IsInViewport()) { continue; }
		const ESlateVisibility V = E.Widget->GetVisibility();
		if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { continue; }
		FVector2D Mn, Mx;
		if (!WidgetAbsRect(E.Widget->GetRootWidget(), Mn, Mx)) { continue; }
		const bool bOver = (Cur.X >= Mn.X && Cur.X <= Mx.X && Cur.Y >= Mn.Y && Cur.Y <= Mx.Y);
		S += FString::Printf(TEXT("\n    %-20s z=%-5d (%.0f,%.0f)-(%.0f,%.0f) курсор над ним=%s%s"),
			*Kv.Key.ToString(), E.ZOrder, Mn.X, Mn.Y, Mx.X, Mx.Y,
			bOver ? TEXT("ДА") : TEXT("нет"), E.bModal ? TEXT(" [МОДАЛЬНОЕ]") : TEXT(""));
		++Shown;
	}
	if (Shown == 0) { S += TEXT("\n    (ни одного видимого)"); }
	return S;
}

// ══════════════════ ★★★ КОЛЕСО МЫШИ НАД ИНТЕРФЕЙСОМ (см. шапку заголовка) ═══════════════════════
void UAsdaWindowSubsystem::RegisterWheelHandler(FName WindowId, FAsdaUiWheelHandler Handler)
{
	if (WindowId.IsNone() || !Handler.IsBound()) { return; }
	WheelHandlers.Add(WindowId, Handler);
	UE_LOG(LogAsdaWindow, Log, TEXT("[колесо] обработчик окна «%s» зарегистрирован (всего %d)"),
		*WindowId.ToString(), WheelHandlers.Num());
}

void UAsdaWindowSubsystem::UnregisterWheelHandler(FName WindowId)
{
	if (WheelHandlers.Remove(WindowId) > 0)
	{
		UE_LOG(LogAsdaWindow, Log, TEXT("[колесо] обработчик окна «%s» снят (осталось %d)"),
			*WindowId.ToString(), WheelHandlers.Num());
	}
}

bool UAsdaWindowSubsystem::HandleMouseWheel(int32 Notches)
{
	if (Notches == 0 || WheelHandlers.Num() == 0) { return false; }
	if (UAsdaUiVisibilitySubsystem::HiddenBlocksUiHitTest(this)) { return false; }   // ★ 21.10.2026 (Alt+Z): колесо — миру

	// Опрашиваем СВЕРХУ ВНИЗ по z — так же, как решается «какое окно под курсором».
	//   Окна без записи в реестре (обработчик пережил закрытие) получают z = -1 и идут последними.
	const bool bModal = IsAnyModalOpen();
	// modal_lock2 (МЕЛК-2): точка курсора — колесо корню вложенного модального контрола только над узлом.
	const FVector2D WheelPt = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetCursorPos() : FVector2D::ZeroVector;
	TArray<TPair<int32, FName>> Order;
	Order.Reserve(WheelHandlers.Num());
	for (const TPair<FName, FAsdaUiWheelHandler>& Kv : WheelHandlers)
	{
		const FAsdaWindowEntry* E = Windows.Find(Kv.Key);
		const int32 Z = E ? E->ZOrder : -1;
		// При открытой модалке нижние окна мышь не разбирают (то же правило, что в TickDrag).
		if (bModal && (!E || !PassesModalMouseGateAt(Kv.Key, *E, WheelPt))) { continue; }   // ★ 10.10.2026: общая точка гейта мыши модалки
		// ★ 07.11.2026 (`asda.UI.TopWindowPoll`): окно без своего колеса поверх списка раньше пропускало колесо в список под ним.
		if (E && E->Widget && IsWidgetBelowCatcher(E->Widget, WheelPt)) { continue; }
		Order.Emplace(Z, Kv.Key);
	}
	Order.Sort([](const TPair<int32, FName>& A, const TPair<int32, FName>& B) { return A.Key > B.Key; });

	for (const TPair<int32, FName>& It : Order)
	{
		const FAsdaUiWheelHandler* H = WheelHandlers.Find(It.Value);
		if (!H || !H->IsBound()) { continue; }
		if (H->Execute(Notches))
		{
			UE_LOG(LogAsdaWindow, Verbose, TEXT("[колесо] %+d щелчк(а/ов) съедено окном «%s» (z=%d)"),
				Notches, *It.Value.ToString(), It.Key);
			return true;
		}
	}
	return false;
}

// ═══════════ ОКНА NPC: ЗАКРЫТИЕ ПРИ УХОДЕ ИГРОКА (порт `UI_resetAllWindowsAndLeave_5045`) ══════
void UAsdaWindowSubsystem::RegisterNpcWindowCloser(FName WindowId, FAsdaNpcWindowCloser Closer)
{
	if (WindowId.IsNone() || !Closer.IsBound()) { return; }
	NpcWindowClosers.Add(WindowId, MoveTemp(Closer));
}

void UAsdaWindowSubsystem::UnregisterNpcWindowCloser(FName WindowId)
{
	NpcWindowClosers.Remove(WindowId);
}

int32 UAsdaWindowSubsystem::CloseNpcWindowsOnPlayerMove(const TCHAR* Why)
{
	// Копия ключей: закрыватель вправе снять свою же регистрацию (и вообще трогать реестр).
	TArray<FName> Ids;
	NpcWindowClosers.GetKeys(Ids);
	int32 Called = 0;
	for (const FName& Id : Ids)
	{
		if (const FAsdaNpcWindowCloser* C = NpcWindowClosers.Find(Id))
		{
			if (C->IsBound())
			{
				C->Execute();
				++Called;
			}
		}
	}
	if (Called > 0)
	{
		UE_LOG(LogAsdaWindow, Log,
			TEXT("[окна] игрок тронулся с места (%s) — закрыто окон NPC: %d"), Why, Called);
	}
	return Called;
}

FString UAsdaWindowSubsystem::DebugWheelHandlers() const
{
	if (WheelHandlers.Num() == 0) { return TEXT("  КОЛЕСО: обработчиков нет"); }
	FString S = FString::Printf(TEXT("  КОЛЕСО: обработчиков %d"), WheelHandlers.Num());
	for (const TPair<FName, FAsdaUiWheelHandler>& Kv : WheelHandlers)
	{
		const FAsdaWindowEntry* E = Windows.Find(Kv.Key);
		S += FString::Printf(TEXT("\n    %-24s z=%s привязан=%s"), *Kv.Key.ToString(),
			E ? *FString::FromInt(E->ZOrder) : TEXT("—"),
			Kv.Value.IsBound() ? TEXT("да") : TEXT("НЕТ"));
	}
	return S;
}

void UAsdaWindowSubsystem::SetWindowAnchorTop(FName WindowId, float BaseTopPx)
{
	if (WindowId.IsNone()) { return; }
	WindowAnchorTops.Add(WindowId, BaseTopPx);
}

bool UAsdaWindowSubsystem::GetWindowAnchorTop(FName WindowId, float& OutBaseTopPx) const
{
	if (const float* Found = WindowAnchorTops.Find(WindowId))
	{
		OutBaseTopPx = *Found;
		return true;
	}
	return false;
}

UUserWidget* UAsdaWindowSubsystem::OpenWindow(FName WindowId, TSubclassOf<UUserWidget> WidgetClass,
	int32 ZOrder, ESlateVisibility Visibility, bool bModal)
{
	if (WindowId.IsNone() || !WidgetClass)
	{
		return nullptr;
	}
	// Уже открыто — вернуть существующее (аналог FindControlByID перед созданием).
	if (FAsdaWindowEntry* Found = Windows.Find(WindowId))
	{
		// ★★★ 26.10.2026 [ИСПРАВЛЕНИЕ] (волна silaris, `asda.Window.ReopenOrphan`): запись реестра, чей виджет умер вместе
		//   с прежним миром (снят с вьюпорта при разборке мира и ни во что не вложен), — не «открытое окно». Реестр чистит
		//   только `CloseAll` при входе в мир (`RevealWorld`), а на промежуточном уровне `L_Loading` запись висит: прежде
		//   здесь возвращался мёртвый виджет, `IsWindowLive` на нём ложно, и «поднять заново» (`EnsureLazy`) крутилось без
		//   конца — панель Друга 6500 вешала игру на каждом портале/телепорте (жалоба Player17, Player52).
		//   1 (по умолчанию) — мёртвую запись снять и создать окно заново; 0 — прежнее: вернуть то, что лежит в реестре.
		if (Found->Widget && AsdaWindowReopenOrphanOn() && !IsWindowLive(Found->Widget) && Found->Widget->GetParent() == nullptr)
		{
			UE_LOG(LogAsdaWindow, Log, TEXT("[реестр] «%s»: запись мёртвого виджета (вне вьюпорта, мир сменился) снята — окно создаётся заново"),
				*WindowId.ToString());
			ClearClampWatch(*Found);   // подписки «ждём показа» живут не дольше окна (как в CloseWindow)
			UnwatchShow(*Found);       // подписка подъёма при показе — тоже
			Found->Widget->RemoveFromParent();
			Windows.Remove(WindowId);
			PendingShowRaise.Remove(WindowId);
			PendingClamp.Remove(WindowId);
			ClampWokenWindows.Remove(WindowId);
		}
		else if (Found->Widget) { return Found->Widget; }
	}

	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World) { return nullptr; }

	UUserWidget* W = CreateWidget<UUserWidget>(World, WidgetClass);
	if (!W)
	{
		UE_LOG(LogAsdaWindow, Warning, TEXT("OpenWindow '%s': виджет не создан"), *WindowId.ToString());
		return nullptr;
	}
	// ★★★ Модальное окно ВСЕГДА в зарезервированной полосе z (см. kModalZ в заголовке): запрошенный
	//   вызывающим z игнорируется, иначе обычное окно с большим z накрывает предупреждение.
	int32 EffZ = bModal ? AllocateModalZ() : ZOrder;
	if (bModal && EffZ != ZOrder)
	{
		UE_LOG(LogAsdaWindow, Log, TEXT("[модаль] «%s»: z %d → %d (зарезервированная полоса модалок)"),
			*WindowId.ToString(), ZOrder, EffZ);
	}
	// ★★★ Ревизия подъёма (КРИТ-1 + СРЕДН-1): окно с рабочим AlwaysTop при показе ложится наверх полосы AlwaysTop — эталон держит
	//   AlwaysTop в конце списка (`mapClearMatchingEntries`, ui_core.c:105126). Приоритетные/всплывающие/модальные z не трогаются.
	const bool bAlwaysTopRt = ResolveAlwaysTop(WindowId);
	if (bAlwaysTopRt && !bModal && EffZ < kPriorityZ && CVarAlwaysTopBand.GetValueOnGameThread() != 0)
	{
		const int32 AtZ = NextAlwaysTopZ(WindowId);
		UE_LOG(LogAsdaWindow, Log, TEXT("[z-порядок] «%s»: AlwaysTop — z %d → %d (полоса AlwaysTop)"), *WindowId.ToString(), EffZ, AtZ);
		EffZ = AtZ;
	}
	// Запись реестра заводится ДО показа окна: в неё кладутся мостики состояний подписи.
	FAsdaWindowEntry Entry;
	Entry.Widget = W;
	Entry.ZOrder = EffZ;
	Entry.bModal = bModal;
	// ★★ ДО добавления во вьюпорт: именно там строится Slate-дерево, а `SButton` читает
	//   `IsFocusable` только в `Construct`. После этой строки флаг уже не подействует.
	ApplyButtonStateData(WindowId, W, Entry);
	W->AddToViewport(EffZ);
	W->SetVisibility(Visibility);
	// ★ 21.10.2026 (Alt+Z): окно реестра прячется вместе с интерфейсом (открытое при скрытом — сразу скрыто, как у оригинала).
	UAsdaUiVisibilitySubsystem::TrackFor(this, W);

	// ★★★ Мета .JUI (ручки перетаскивания + вырезы NoSelect) — ОДНА точка на все окна, текущие и будущие.
	AttachJuiMeta(WindowId, Entry);
	Entry.bAlwaysTopRuntime = bAlwaysTopRt;
	Windows.Add(WindowId, Entry);
	// ★ 08.10.2026 (пакет 2): целые пиксели у окон с процентным якорем, как у оригинала (до постановок и сдвигов игрока).
	SnapPercentAnchors(WindowId);
	// ★★★ 08.10.2026 (пакет 2, решение владельца): модальное окно — в позиции ОРИГИНАЛА (таблица у `PlaceModalByOriginal`);
	//   прежнее глобальное правило «модалка по центру» осталось для окон вне таблицы. Нужна запись реестра — поэтому после Add.
	if (bModal) { PlaceModalByOriginal(WindowId); }
	// ★★★ Открытие поверх всех (правило владельца 08.10.2026): подписка на показ корня; открытое видимым — сразу в очередь подъёма.
	if (FAsdaWindowEntry* Added = Windows.Find(WindowId))
	{
		WatchShow(WindowId, *Added);
		if (Added->bShownForRaise)
		{
			const bool bHeld = FSlateApplication::IsInitialized() && FSlateApplication::Get().GetPressedMouseButtons().Num() > 0;
			if (bHeld) { EnqueueShowRaise(WindowId, TEXT("открытие при зажатой кнопке мыши")); } else { RaiseOnShowNow(WindowId, TEXT("открытие")); }
		}
	}
	// ★★★ Позиция из ini — ТУТ ЖЕ, ОДНОЙ ТОЧКОЙ НА ВСЕ ОКНА (текущие и будущие): окно получает
	//   свою сохранённую раскладку самим фактом регистрации, как и ручки перетаскивания.
	ApplySavedLayout(WindowId);
	RefreshModalInput();   // модальное окно → gate ввода
	return W;
}

UUserWidget* UAsdaWindowSubsystem::OpenWindowByPath(FName WindowId, const FString& WidgetClassPath,
	int32 ZOrder, ESlateVisibility Visibility, bool bModal)
{
	// "/Game/.../WBP_X[.WBP_X]" → "/Game/.../WBP_X.WBP_X_C" (класс Blueprint-generated). ★ волна 2 п. 7: правило в реестре путей.
	const FString Path = AsdaAssetPaths::WidgetClassPath(WidgetClassPath);
	UClass* C = LoadClass<UUserWidget>(nullptr, *Path);
	if (!C)
	{
		UE_LOG(LogAsdaWindow, Warning, TEXT("OpenWindow '%s': класс не загружен '%s'"), *WindowId.ToString(), *Path);
		return nullptr;
	}
	return OpenWindow(WindowId, C, ZOrder, Visibility, bModal);
}

void UAsdaWindowSubsystem::RegisterWindow(FName WindowId, UUserWidget* Widget, int32 ZOrder, bool bModal)
{
	if (WindowId.IsNone() || !Widget) { return; }
	FAsdaWindowEntry Entry;
	Entry.Widget = Widget;
	// ★★★ Та же полоса z, что и в OpenWindow: модальность и порядок отрисовки не должны расходиться.
	Entry.ZOrder = bModal ? AllocateModalZ() : ZOrder;
	Entry.bModal = bModal;
	AttachJuiMeta(WindowId, Entry);   // ★★★ см. OpenWindow: ручки/NoSelect берутся из данных .JUI
	Entry.bAlwaysTopRuntime = ResolveAlwaysTop(WindowId);   // ★ ревизия подъёма (КРИТ-1): рабочий AlwaysTop
	ApplyButtonStateData(WindowId, Widget, Entry);   // ★★★ см. OpenWindow: состояния подписи кнопок
	if (FAsdaWindowEntry* Old = Windows.Find(WindowId)) { UnwatchShow(*Old); }   // перерегистрация: старая подписка не нужна
	Windows.Add(WindowId, Entry);
	if (Widget->IsInViewport()) { UAsdaUiVisibilitySubsystem::TrackFor(this, Widget); }   // ★ 21.10.2026 (Alt+Z): и регистрация готового окна
	// ★★★ Открытие поверх всех (08.10.2026): см. OpenWindow; зарегистрированное уже показанным (во вьюпорте) — в очередь.
	if (FAsdaWindowEntry* Added = Windows.Find(WindowId))
	{
		WatchShow(WindowId, *Added);
		if (Added->bShownForRaise && Widget->IsInViewport())
		{
			const bool bHeld = FSlateApplication::IsInitialized() && FSlateApplication::Get().GetPressedMouseButtons().Num() > 0;
			if (bHeld) { EnqueueShowRaise(WindowId, TEXT("регистрация при зажатой кнопке мыши")); } else { RaiseOnShowNow(WindowId, TEXT("регистрация показанного")); }
		}
	}
	// ★ См. OpenWindow: AlwaysTop — наверх своей полосы (только уже показанное окно: перевешивание добавляет во вьюпорт).
	if (Entry.bAlwaysTopRuntime && !bModal && Entry.ZOrder < kPriorityZ && Widget->IsInViewport()
		&& CVarAlwaysTopBand.GetValueOnGameThread() != 0)
	{
		BringWindowToFront(WindowId);
	}
	SnapPercentAnchors(WindowId);     // ★ 08.10.2026 (пакет 2): целые пиксели «ц%», до сдвигов игрока — см. OpenWindow
	ApplySavedLayout(WindowId);       // ★★★ см. OpenWindow: позиция из ini — по факту регистрации
	if (bModal)
	{
		if (Widget->IsInViewport()) { Widget->RemoveFromParent(); }
		Widget->AddToViewport(Entry.ZOrder);
		UAsdaUiVisibilitySubsystem::TrackFor(this, Widget);   // ★ 21.10.2026 (Alt+Z): новая обёртка вьюпорта
		PlaceModalByOriginal(WindowId);   // ★ 08.10.2026 (пакет 2): позиция оригинала, вне таблицы — центр
	}
	RefreshModalInput();
}

UUserWidget* UAsdaWindowSubsystem::FindWindow(FName WindowId) const
{
	if (const FAsdaWindowEntry* Found = Windows.Find(WindowId))
	{
		return Found->Widget;
	}
	return nullptr;
}

// ★★★ Аудит №4 (B4): гвард травела — обоснование трёх условий в шапке объявления.
bool UAsdaWindowSubsystem::IsWindowLive(const UUserWidget* Widget) const
{
	const UGameInstance* GI = GetGameInstance();
	if (!Widget || !GI) { return false; }
	return Widget->GetWorld() == GI->GetWorld() && Widget->IsInViewport();
}

bool UAsdaWindowSubsystem::IsOpen(FName WindowId) const
{
	const FAsdaWindowEntry* Found = Windows.Find(WindowId);
	return Found && Found->Widget != nullptr;
}

// ★ «Окно фактически видно» — ОДНО определение на весь порт. Раньше эта проверка была вписана
//   прямо в `IsAnyModalOpen`; вынесена сюда, чтобы у неё не завелось второй копии.
bool UAsdaWindowSubsystem::IsWindowVisible(FName WindowId) const
{
	const FAsdaWindowEntry* Found = Windows.Find(WindowId);
	return Found && AsdaWindowIsEntryVisible(*Found);
}

void UAsdaWindowSubsystem::SetWindowVisible(FName WindowId, bool bVisible, ESlateVisibility VisibleMode)
{
	if (const FAsdaWindowEntry* Found = Windows.Find(WindowId))
	{
		if (Found->Widget)
		{
			Found->Widget->SetVisibility(bVisible ? VisibleMode : ESlateVisibility::Collapsed);
		}
	}
	RefreshModalInput();   // видимость модального изменилась → пересчитать gate
}

void UAsdaWindowSubsystem::CloseWindow(FName WindowId)
{
	// ★★★ Очередь окон-сообщений (26.09): закрытие ONEBUT/TWOBUT чужим кодом снимает запись этого окна и закрывает
	//   вид через очередь (5 + возврат следующей записи); внутренний путь очереди снимает виджет «голым» (`bMsgBoxRawClose`).
	//   Звук 5 теперь играет `CloseMsgBoxKind` (порт `CGameUI_CloseWindow`), а не это место.
	if (!bMsgBoxRawClose && (WindowId == FName(TEXT("ONEBUT_MSGBOX")) || WindowId == FName(TEXT("TWOBUT_MSGBOX"))))
	{
		DismissMsgBoxWindow(WindowId == FName(TEXT("TWOBUT_MSGBOX")), bMsgBoxSilentClose);
		return;
	}
	// ★ 06.10.2026 (волна 2-Г): окно ввода имени 1552 — та же очередь (виды 59/60).
	if (!bMsgBoxRawClose && WindowId == FName(TEXT("GIFT_MSGBOX")))
	{
		DismissGiftMsgBox(bMsgBoxSilentClose);
		return;
	}
	if (FAsdaWindowEntry* Found = Windows.Find(WindowId))
	{
		ClearClampWatch(*Found);   // М-2: подписки «ждём показа» живут не дольше окна
		UnwatchShow(*Found);       // ★ 08.10.2026: подписка подъёма при показе — тоже
		if (Found->Widget) { Found->Widget->RemoveFromParent(); }
		Windows.Remove(WindowId);
	}
	PendingShowRaise.Remove(WindowId);   // ★ 08.10.2026: закрытое окно подниматься не должно
	PendingClamp.Remove(WindowId);
	ClampWokenWindows.Remove(WindowId);
	FlushWindowLayout();   // позиция закрываемого окна гарантированно на диске (если менялась)
	RefreshModalInput();   // модальное могло закрыться → снять/переставить gate
}

void UAsdaWindowSubsystem::CloseAll()
{
	for (TPair<FName, FAsdaWindowEntry>& Kv : Windows)
	{
		ClearClampWatch(Kv.Value);   // М-2: подписки «ждём показа» живут не дольше окна
		UnwatchShow(Kv.Value);       // ★ 08.10.2026: подписка подъёма при показе — тоже
		if (Kv.Value.Widget) { Kv.Value.Widget->RemoveFromParent(); }
	}
	Windows.Empty();
	PendingShowRaise.Reset();        // ★ 08.10.2026: очередь подъёма при показе живёт не дольше реестра
	PendingClamp.Reset();
	ClampWokenWindows.Reset();
	FlushWindowLayout();
	// ★★★ Очередь окон-сообщений (26.09): эталонные окна травел не сносит — запись, оставшаяся в очереди, снова на
	//   экране после входа в мир (`CGameUI_CloseWindow(5001/5002/0)` загрузки, game_main.c:3547-3549).
	RedisplayMsgBoxAfterCloseAll();
}
