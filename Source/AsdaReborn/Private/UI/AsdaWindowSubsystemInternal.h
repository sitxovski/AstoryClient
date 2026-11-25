// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWindowSubsystemInternal.h — ★ 23.10.2026 (разнос AsdaWindowSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaWindowSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала LogAsdaWindow (один экземпляр — DEFINE_LOG_CATEGORY в
//   AsdaWindowSubsystem.cpp) и чистые static-константы/хелперы без состояния — текст байт в байт, у каждой части своя копия.
//   Подключать ПОСЛЕ #include пролога.
#pragma once

#include "CoreMinimal.h"
#include "UI/AsdaUiHelpers.h"   // ★ R1: прямое включение (раньше приходило транзитивно)
#include "UI/AsdaWindowSubsystem.h"

// Категория журнала окон: объявление; определение — AsdaWindowSubsystem.cpp (до разноса там был DEFINE_LOG_CATEGORY_STATIC).
DECLARE_LOG_CATEGORY_EXTERN(LogAsdaWindow, Log, All);



// ★ Единственное определение «запись окна сейчас видна»: виджет есть и его видимость не
//   Collapsed/Hidden. Пользуются `IsWindowVisible` и `IsAnyModalOpen` — двух копий условия нет.
static bool AsdaWindowIsEntryVisible(const FAsdaWindowEntry& E)
{
	if (!E.Widget) { return false; }
	const ESlateVisibility V = E.Widget->GetVisibility();
	return V != ESlateVisibility::Collapsed && V != ESlateVisibility::Hidden;
}

// ── Прямоугольник виджета в АБСОЛЮТНЫХ (десктопных) координатах Slate — та же математика, что у
//    проверенного провайдера зоны чата: сравнивается с FSlateApplication::GetCursorPos().
// ★ D1b (аудит `code_dedup_audit_2.md`): тело переехало в `AsdaUi::WidgetAbsRect` (общая точка
//   с хит-тестом `AsdaUi::IsCursorOverWidget` и с рамкой окна выпендрёжа). Здесь — файловый
//   форвард, чтобы не переписывать десятки вызовов ниже.
static FORCEINLINE bool WidgetAbsRect(const UWidget* W, FVector2D& OutMin, FVector2D& OutMax)
{
	return AsdaUi::WidgetAbsRect(W, OutMin, OutMax);
}

// ★ 31.10.2026 (волна hud_click): окно-оверлей с `NoSelect="1"` у корня (`GNoSelectRootWindows` в AsdaWindowSubsystem_Raise.cpp:
//   нотисы, баннеры, метки цели, облачка речи, подсказки, полоса опыта) — сквозное по эталону; правило «видимое тело окна ловит»
//   (`asda.UI.HitTestDrawnBody`) к нему не применяется. Нужно самотесту `asda.UI.SelfTestClickThrough`, чтобы не дублировать список.
bool AsdaIsNoSelectRootWindow(FName WindowId);

// ═══ ★ R2 10.11.2026: общее для новых частей разреза AsdaWindowSubsystem.cpp (перенесено дословно) ═══
// Включения пролога AsdaWindowSubsystem.cpp — нужны перенесённым ниже помощникам во ВСЕХ частях, включая прежние.
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

// Ключ CVarZCompact: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarZCompact;
#else
extern TAsdaConstCVar<int32> CVarZCompact;
#endif

// Ключ CVarRaiseOnBodyClickAll: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarRaiseOnBodyClickAll;
#else
extern TAsdaConstCVar<int32> CVarRaiseOnBodyClickAll;
#endif

// Ключ CVarAlwaysTopBand: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarAlwaysTopBand;
#else
extern TAsdaConstCVar<int32> CVarAlwaysTopBand;
#endif

// Ключ CVarOpenOnTop: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarOpenOnTop;
#else
extern TAsdaConstCVar<int32> CVarOpenOnTop;
#endif

// Ключ CVarAddedDragHandles: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarAddedDragHandles;
#else
extern TAsdaConstCVar<int32> CVarAddedDragHandles;
#endif

// GDragDisabledWindows: определение — в домашней части (R2).
const TSet<FName>& GDragDisabledWindows();

// Ключ CVarHitTestPlanesOnly: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarHitTestPlanesOnly;
#else
extern TAsdaConstCVar<int32> CVarHitTestPlanesOnly;
#endif

// Ключ CVarHitTestDrawnBody: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarHitTestDrawnBody;
#else
extern TAsdaConstCVar<int32> CVarHitTestDrawnBody;
#endif

// Рисует ли узел видимую картинку (текстура, ненулевая альфа кисти, цвета и прозрачности узла).
static bool AsdaDrawsVisiblePlane(const UWidget* W)
{
	if (!W || W->GetRenderOpacity() < 0.02f) { return false; }
	if (const UImage* Im = Cast<UImage>(W))
	{
		const FSlateBrush& B = Im->GetBrush();
		return B.DrawAs != ESlateBrushDrawType::NoDrawType && B.GetResourceObject()
			&& B.TintColor.GetSpecifiedColor().A > 0.02f && Im->GetColorAndOpacity().A > 0.02f;
	}
	if (const UBorder* Bd = Cast<UBorder>(W))
	{
		return Bd->Background.DrawAs != ESlateBrushDrawType::NoDrawType && Bd->Background.GetResourceObject() && Bd->GetBrushColor().A > 0.02f;
	}
	return false;
}

// Непустой ли текст узла.
static bool AsdaHasVisibleText(const UWidget* W)
{
	if (!W || W->GetRenderOpacity() < 0.02f) { return false; }
	if (const UTextBlock* Tb = Cast<UTextBlock>(W)) { return !Tb->GetText().IsEmptyOrWhitespace(); }
	if (const URichTextBlock* Rb = Cast<URichTextBlock>(W)) { return !Rb->GetText().IsEmptyOrWhitespace(); }
	return false;
}

// Рекурсия хит-теста; ClipMn/ClipMx — пересечение прямоугольников клипующих предков (без предков — вся плоскость);
//   bDrawnBody — правило `asda.UI.HitTestDrawnBody` для этого окна (ключ включён и корень окна не NoSelect).
static bool HitTestWidgetTreeClipped(UWidget* W, const TSet<FName>& NoHit, const FVector2D& Cur, FName& OutName,
	bool bPlanesOnly, FVector2D ClipMn, FVector2D ClipMx, bool bDrawnBody)
{
	if (!W) { return false; }
	const ESlateVisibility Vis = W->GetVisibility();
	if (Vis == ESlateVisibility::Collapsed || Vis == ESlateVisibility::Hidden) { return false; }
	FVector2D Mn, Mx;
	const bool bRect = WidgetAbsRect(W, Mn, Mx);
	if (UPanelWidget* P = Cast<UPanelWidget>(W))
	{
		FVector2D CMn = ClipMn, CMx = ClipMx;
		if (bPlanesOnly && bRect && (W->GetClipping() != EWidgetClipping::Inherit || W->IsA<UScrollBox>()))
		{
			CMn = FVector2D(FMath::Max(CMn.X, Mn.X), FMath::Max(CMn.Y, Mn.Y));
			CMx = FVector2D(FMath::Min(CMx.X, Mx.X), FMath::Min(CMx.Y, Mx.Y));
		}
		for (int32 i = P->GetChildrenCount() - 1; i >= 0; --i)
		{
			if (HitTestWidgetTreeClipped(P->GetChildAt(i), NoHit, Cur, OutName, bPlanesOnly, CMn, CMx, bDrawnBody)) { return true; }
		}
	}
	if (W->IsA<UCanvasPanel>()) { return false; }
	const bool bText = W->IsA<UTextBlock>() || W->IsA<URichTextBlock>();
	// (а) подпись — не план; видимый текст тела окна ловит (`asda.UI.HitTestDrawnBody`).
	if (bPlanesOnly && bText && !(bDrawnBody && AsdaHasVisibleText(W))) { return false; }
	// NoSelect пропускает клик; нарисованный план или видимый текст тела окна — ловит (`asda.UI.HitTestDrawnBody`).
	if (NoHit.Contains(W->GetFName()) && !(bDrawnBody && (bText ? AsdaHasVisibleText(W) : AsdaDrawsVisiblePlane(W)))) { return false; }
	if (!bRect) { return false; }
	if (bPlanesOnly && (Cur.X < ClipMn.X || Cur.X > ClipMx.X || Cur.Y < ClipMn.Y || Cur.Y > ClipMx.Y)) { return false; }   // (б)
	if (Cur.X >= Mn.X && Cur.X <= Mx.X && Cur.Y >= Mn.Y && Cur.Y <= Mx.Y)
	{
		OutName = W->GetFName();
		return true;
	}
	return false;
}

static bool HitTestWidgetTree(UWidget* W, const TSet<FName>& NoHit, const FVector2D& Cur, FName& OutName, bool bDrawnBody = false)
{
	const double Big = TNumericLimits<double>::Max();
	return HitTestWidgetTreeClipped(W, NoHit, Cur, OutName, CVarHitTestPlanesOnly.GetValueOnGameThread() != 0,
		FVector2D(-Big, -Big), FVector2D(Big, Big), bDrawnBody);
}

// Ключ CVarZoneRectInit: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarZoneRectInit;
#else
extern TAsdaConstCVar<int32> CVarZoneRectInit;
#endif

// Прочитать прямоугольник зоны (делегат) по правилу `asda.UI.ZoneRectInit`.
static bool AsdaReadZoneRect(const FAsdaUiZoneRect& D, FVector2D& Mn, FVector2D& Mx)
{
	if (!D.IsBound()) { return false; }
	if (CVarZoneRectInit.GetValueOnGameThread() == 0) { return D.Execute(Mn, Mx); }
	Mn = FVector2D(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
	Mx = FVector2D(TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest());
	if (!D.Execute(Mn, Mx)) { return false; }
	return FMath::IsFinite(Mn.X) && FMath::IsFinite(Mn.Y) && FMath::IsFinite(Mx.X) && FMath::IsFinite(Mx.Y)
		&& Mn.X <= Mx.X && Mn.Y <= Mx.Y
		&& FMath::Abs(Mn.X) < 1.0e7 && FMath::Abs(Mn.Y) < 1.0e7 && FMath::Abs(Mx.X) < 1.0e7 && FMath::Abs(Mx.Y) < 1.0e7;
}

// ── ★★★ ЕСТЬ ЛИ ПОД КУРСОРОМ ЖИВОЙ КОНТРОЛ SLATE (поправка финальной приёмки волны 8) ─────────
//    ЗАЧЕМ. Немедленный подъём окна по нажатию на ручку перетаскивания перевешивает окно во
//    вьюпорте, а это рвёт захват мыши Slate (`FSlateUser::GetCaptorPath` → `Truncated` →
//    `ReleaseCapture`) и съедает клик по контролу, который живёт СВОИМ путём Slate, мимо нашего
//    покадрового опроса. Такие контролы под ручками существуют: 43439 `Pet_Inven_Win_X` лежит
//    внутри `Pet_Inven_Top_Text_68_DRAG` (доказано живым прогоном, см. место вызова).
//
//    ★ КАКИЕ ТИПЫ СЧИТАЮТСЯ И ПОЧЕМУ ИМЕННО ОНИ. Не «кнопка», а ЛЮБОЙ виджет с собственным
//    захватом мыши Slate:
//      • `UButton`            — крестики окон, кнопки панелей (самый частый случай);
//      • `UCheckBox`          — галки; в порту под полосой-ручкой лежит `INVEN_NEW_WIN` 83503;
//      • `UEditableText` и `UEditableTextBox`, `UMultiLineEditableText` — поля ввода: под ручками
//        лежат `CLAN_MAKING_WIN` 14402, `CLAN_JION` 14422, `SHOUTING_WIN` 1583,
//        `StreetStall_Win` 23403 (у них разрыв захвата стоит ещё и потери фокуса);
//      • `UComboBoxString`    — выпадающие списки, у них своё меню поверх;
//      • `USlider`, `UScrollBox` — тащат мышь сами, разрыв захвата обрывает протяжку.
//    ⚠ ОСТАЛЬНЫЕ контролы (иконки, планы, тексты) идут через наш покадровый опрос, а тот уже
//    защищён парой «нажал/отпустил» (`FAsdaPressLatch`) и переживает кадр без геометрии —
//    им откладывать подъём не нужно.
//    ⚠ РАДИОКНОПКИ сюда не нужны отдельным типом: импортёр делает из них `UButton`
//    (`Tools/asda_jui_generic_umg.py:520-531`), то есть они уже покрыты первым пунктом.
//    Скрытая цепочка и `Collapsed`/`Hidden` не считаются — как и в хит-тесте окна выше.
//
//    ★★★ ВОЛНА 9 (П8-11): ДВА ИЗ ЧЕТЫРЁХ НАЗВАННЫХ ИСТОЧНИКОВ ЛОЖНОГО «ДА» УСТРАНЕНЫ.
//    Прежняя редакция честно перечисляла, чего проверка не видит: `NoHitWidgets`,
//    `SelfHitTestInvisible`/`HitTestInvisible`, перекрытие другим виджетом и ВЫКЛЮЧЕННЫЙ РОДИТЕЛЬ.
//    Цена ложного «да» после поправки приёмки выросла (окно не потащится там, где должно), поэтому:
//      (1) `HitTestInvisible` — Slate НЕ отдаёт мышь ни самому узлу, НИ ЕГО ДЕТЯМ ⇒ обрываем
//          ветку целиком: капчурить там некому.
//          ★★★ ОТКУДА ЭТО ПРАВИЛО (поправка ревизии — прежняя формулировка вводила в заблуждение):
//          оно взято из СЕМАНТИКИ SLATE, а НЕ из эталона. У `uiControlHitTest@0x12DB470` аналога
//          «обрыва ветки» НЕТ вовсе: рекурсия в детей идёт ПЕРВОЙ и БЕЗУСЛОВНОЙ
//          (`ui_core.c:107160`), а признак `NoSelect` (байт `+295`, атрибут `.JUI` `"NoSelect"`,
//          разбор `ui_core.c:104979-104984`) проверяется ТОЛЬКО у самого узла (`:107177`).
//          Эталонному `NoSelect` соответствует `SelfHitTestInvisible`, и он обрабатывается
//          пунктом (2) ниже. ⚠ Эта функция вообще НЕ порт `uiControlHitTest` — она отвечает на
//          другой вопрос, «съест ли Slate это нажатие». Порт самого эталонного хит-теста —
//          `HitTestWidgetTree` (см. выше в этом файле), и он 1:1: дети первыми и безусловно,
//          `NoSelect` только у себя, ветка не рубится.
//          ⚠ ИНВАРИАНТ, КОТОРЫЙ НАДО ДЕРЖАТЬ: `HitTestInvisible` выдавать ТОЛЬКО листьям и
//          заведомо декоративным поддеревьям. Проверено: из 122 боевых присвоений в порту НИ У
//          ОДНОГО нет кликабельных потомков, а импортёр `asda_jui_generic_umg.py` и
//          `AsdaUi::ShownVisibilityFor` этот режим не выдают вовсе. Появится узел с
//          интерактивными детьми — ему нужен `SelfHitTestInvisible`, иначе дети умрут;
//      (2) `SelfHitTestInvisible` — мышь не берёт САМ узел, но дети берут ⇒ в детей идём,
//          а сам узел капчурящим НЕ считаем (именно этот случай и назван в П8-11 как
//          «кнопок с SelfHitTestInvisible сейчас нет, но при появлении проверку надо усилить»);
//      (3) выключенный РОДИТЕЛЬ гасит детей — тащим признак `bEnabledChain` вниз по рекурсии,
//          а не смотрим только на `GetIsEnabled()` самого узла.
//    ⚠ ЧЕГО ПРОВЕРКА ПО-ПРЕЖНЕМУ НЕ ВИДИТ (честно, чтобы следующая волна не искала заново):
//      • `NoHitWidgets` игрового вьюпорта — это движковый список, к дереву окна отношения не имеет;
//      • ПЕРЕКРЫТИЕ капчурящего узла ДРУГИМ, некапчурящим, но хит-тестируемым узлом того же окна.
//        Обход идёт по детям В ОБРАТНОМ ПОРЯДКЕ (верхние первыми) и возвращается на первом
//        попадании, то есть z-порядок учтён ВНУТРИ капчурящего набора; узел же вроде `UImage`
//        с полной видимостью, лежащий ПОВЕРХ кнопки, в оригинале невозможен (импортёр даёт планам
//        `SelfHitTestInvisible`, `AsdaUi::ShownVisibilityFor`), поэтому случай остаётся
//        теоретическим. Появятся такие планы — проверку придётся усилить снова.
//    Ключ отката прежний и общий: `asda.UI.DragOverControls 1`.
//
//    ★★★ ВСЕ ТРИ ПРАВИЛА ДОКАЗАНЫ ЖИВЫМ ОПЫТОМ (поправка ревизии; прежде — ни одного опыта).
//    Подопытная пара — единственная в порту, где крестик РЕАЛЬНО пересекается с ручкой:
//    `SYSTEM_MenuWIN_NEW`, ручка (1180,623)-(1354,663), крестик `SYSTEM_MENU_Xbtn`
//    (1348,627)-(1366,645); точка опыта (1351,636) лежит В ОБОИХ. Метрика — сдвиг окна после
//    настоящего драга Slate на −100 по X (`Reports/pet/v9/game_hittest_rules2.log`):
//        шаг 0  база                              → окно НЕ поехало   (контроль)
//        шаг 1  кнопке `SelfHitTestInvisible`     → ПОЕХАЛО (−100,0)  ⇒ правило (2)
//        шаг 2  кнопке обратно `Visible`          → НЕ поехало        (обратимо)
//        шаг 3  предку `HitTestInvisible`         → ПОЕХАЛО (−100,0)  ⇒ правило (1)
//        шаг 4  предку обратно `Visible`          → НЕ поехало        (обратимо)
//        шаг 5  предок ВЫКЛЮЧЕН                   → ПОЕХАЛО (−100,0)  ⇒ правило (3)
//    Чередование НЕТ/ДА/НЕТ/ДА/НЕТ/ДА и одинаковая величина сдвига — это и есть фальсифицируемость.
//    Состояние создавалось командами прогона `asda.UI.SetVis` / `asda.UI.SetEnabled`.
//    ⚠ ГРАБЛЯ ПЕРВОГО ЗАХОДА: предок был выбран ПО ПЕРЕСЕЧЕНИЮ ПРЯМОУГОЛЬНИКОВ
//    (`WinMd_CBar_grp` геометрически накрывает крестик) — и правила (1)/(3) «не сработали»,
//    потому что он крестику НЕ ПРЕДОК. Дамп дерева плоский, родство по нему не читается.
static bool FindSlateCapturingWidgetUnderCursor(UWidget* W, const FVector2D& Cur, bool bEnabledChain = true)
{
	if (!W) { return false; }
	const ESlateVisibility Vis = W->GetVisibility();
	if (Vis == ESlateVisibility::Collapsed || Vis == ESlateVisibility::Hidden) { return false; }
	// (1) Вся ветка вне хит-теста Slate — ни сам узел, ни дети мышь не получат.
	if (Vis == ESlateVisibility::HitTestInvisible) { return false; }

	// (3) Выключенность НАСЛЕДУЕТСЯ: выключенный родитель гасит всё поддерево.
	const bool bChain = bEnabledChain && W->GetIsEnabled();

	if (const UPanelWidget* P = Cast<UPanelWidget>(W))
	{
		for (int32 i = P->GetChildrenCount() - 1; i >= 0; --i)
		{
			if (FindSlateCapturingWidgetUnderCursor(P->GetChildAt(i), Cur, bChain)) { return true; }
		}
	}
	// (2) Сам узел мышь не берёт, хотя дети (разобранные выше) могли бы.
	if (Vis == ESlateVisibility::SelfHitTestInvisible) { return false; }

	const bool bCaptures =
		   W->IsA<UButton>()
		|| W->IsA<UCheckBox>()
		|| W->IsA<UEditableText>()
		|| W->IsA<UEditableTextBox>()
		|| W->IsA<UMultiLineEditableText>()
		|| W->IsA<UComboBoxString>()
		|| W->IsA<USlider>()
		|| W->IsA<UScrollBox>();
	if (!bCaptures) { return false; }
	if (!bChain) { return false; }
	FVector2D Mn, Mx;
	if (!WidgetAbsRect(W, Mn, Mx)) { return false; }
	return (Cur.X >= Mn.X && Cur.X <= Mx.X && Cur.Y >= Mn.Y && Cur.Y <= Mx.Y);
}

// ── Сдвиг слота на дельту. ★ Схема анкора НЕ МЕНЯЕТСЯ (закон jui-align-anchors): у POINT-оси
//    offsets = (позиция, размер) → двигаем позицию; у SPAN-оси offsets = (отступ слева, отступ
//    справа) → позиция сдвигается прибавлением слева И вычитанием справа, иначе окно не поедет,
//    а растянется. Прежний ShiftBigMapWindow этого не различал.
//   ★ Дедуп №2, В2-B (U15): тело переехало в общую точку `AsdaUi::ShiftCanvasSlot` — здесь форвард (поведение то же).
static void ShiftCanvasSlot(UWidget* W, const FVector2D& D)
{
	AsdaUi::ShiftCanvasSlot(W, D);
}

// GNoSelectRootWindows: определение — AsdaWindowSubsystem_Raise.cpp (R2).
const TSet<FName>& GNoSelectRootWindows();

// Правило «видимое тело окна ловит» (`asda.UI.HitTestDrawnBody`) для окна: ключ включён и корень окна не NoSelect (оверлей).
static bool AsdaDrawnBodyFor(FName WindowId, const FAsdaWindowEntry& E)
{
	return CVarHitTestDrawnBody.GetValueOnGameThread() != 0 && !GNoSelectRootWindows().Contains(WindowId) && !E.NoHitWidgets.Contains(WindowId);
}
