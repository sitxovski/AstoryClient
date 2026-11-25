// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWindowSubsystem_Layout.cpp — ★ R2 10.11.2026 (разрез AsdaWindowSubsystem.cpp, дословный перенос): раскладка и удержание в экране: сохранённые смещения/размеры, рецепты перетаскивания, прямоугольники слотов, вне экрана/захват, ClampWindowBySlotsNow/IntoViewport, ресайз, отладка раскладки.
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
#include "UI/AsdaMessageSubsystem.h"  // D2: фолбэк однокнопочного msgbox, когда WBP не загрузился
#include "UI/AsdaHotbarSubsystem.h"      // шаг 13 `uiCloseAllWindows`: блокировка/разблокировка хотбара
// шаги 23-24: сворачивание трея и запрет его кнопок   ★ R3: комментарий снятого включения "UI/AsdaSystemMenuSubsystem.h" (файл им не пользовался) — сохранён
#include "Resources/AsdaSoundSubsystem.h" // звуки открытия/закрытия окна количества (5 / 6)
#include "UI/AsdaItemTooltipSubsystem.h"  // ★ 27.09.2026: панель 1530 для `DropTopMsgBoxBeforeNotice`
#include "UI/AsdaPurchaseBoxSubsystem.h"  // ★ 27.09.2026: панель 1559 для `DropTopMsgBoxBeforeNotice`
#include "Styling/SlateStyleRegistry.h"   // ★ гашение синей рамки фокуса: подмена кисти FocusRectangle
#include "Styling/SlateStyle.h"
#include "Styling/AppStyle.h"
#include "Brushes/SlateNoResource.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Login/AsdaText.h"                 // FAsdaText::Loc — строки диалога `asda.UI.ConfirmPreview` (07.10.2026)
#include "Login/AsdaLoginGameMode.h"          // ★ 08.10.2026: подъём при показе не трогает экран логина
#include "World/AsdaWorldKind.h"               // ★ F7 (дедуп №2, В3): общий признак «сцена логина»
#include "GameFramework/GameModeBase.h"     // GetAuthGameMode — проверка экрана логина
#include "UnrealClient.h"                   // С-1 (07.10.2026): FViewport::ViewportResizedEvent — смена размера вьюпорта
#include "Blueprint/GameViewportSubsystem.h" // М-1 (07.10.2026): слот окна во вьюпорте — полноэкранный ли он
#include "Widgets/SOverlay.h"               // ★ 08.10.2026: подъём окна БЕЗ снятия с вьюпорта (слот оверлея вьюпорта)
#include "UI/AsdaNotice.h"                  // ★ 08.10.2026 (ревизия В3 рыбалки): нотис 6835 строгого нумпада — LocRaw / ChatColored
// ★ 08.10.2026: ColGold — цвет −11514 нотиса 6835   ★ R3: комментарий снятого включения "UI/AsdaChatSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaMarkup.h"                  // ★ 09.10.2026: UAsdaRichText::ApplyFormatted — теги <FC=…> в теле окон-сообщений
#include "Components/RichTextBlock.h"      // ★ 09.10.2026: тело окна-сообщения после первой подмены — URichTextBlock
#include "AsdaWindowSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWindowSubsystem*.cpp (LogAsdaWindow, чистые хелперы)
#include "AsdaWindowSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaWindowSubsystem*.cpp

// Пара «число,число» → FVector2D. false, если строка не разбирается (битый ini правим молча —
//   пользователь мог отредактировать файл руками, ронять игру из-за этого нельзя).
static bool AsdaParseVec2(const FString& S, FVector2D& Out)
{
	FString A, B;
	if (!S.Split(TEXT(","), &A, &B)) { return false; }
	A.TrimStartAndEndInline(); B.TrimStartAndEndInline();
	if (!A.IsNumeric() || !B.IsNumeric()) { return false; }
	Out = FVector2D(FCString::Atof(*A), FCString::Atof(*B));
	return Out.ContainsNaN() ? false : true;
}

static FString AsdaVec2ToString(const FVector2D& V)
{
	return FString::Printf(TEXT("%.3f,%.3f"), V.X, V.Y);
}

void UAsdaWindowSubsystem::LoadAllLayouts() const
{
	if (bLayoutsLoaded) { return; }
	UAsdaWindowSubsystem* Self = const_cast<UAsdaWindowSubsystem*>(this);
	Self->bLayoutsLoaded = true;
	if (!GConfig) { return; }
	TArray<FString> Lines;
	if (!GConfig->GetSection(kLayoutSection, Lines, GGameUserSettingsIni)) { return; }
	int32 NumPos = 0, NumSize = 0, NumBad = 0;
	for (const FString& Line : Lines)
	{
		FString Key, Val;
		if (!Line.Split(TEXT("="), &Key, &Val)) { ++NumBad; continue; }
		Key.TrimStartAndEndInline(); Val.TrimStartAndEndInline();
		FString Kind, Rest;
		if (!Key.Split(TEXT("."), &Kind, &Rest)) { ++NumBad; continue; }
		if (Kind == TEXT("Pos") || Kind == TEXT("PosExt"))
		{
			FString WinStr, TgtStr;
			if (!Rest.Split(TEXT("."), &WinStr, &TgtStr)) { ++NumBad; continue; }
			FVector2D Off;
			if (!AsdaParseVec2(Val, Off)) { ++NumBad; continue; }
			FAsdaWindowSavedLayout& L = Self->SavedLayouts.FindOrAdd(FName(*WinStr));
			L.Offsets.Add(FName(*TgtStr), Off);
			if (Kind == TEXT("PosExt")) { L.ExternalKeys.Add(FName(*TgtStr)); }
			++NumPos;
		}
		else if (Kind == TEXT("Size"))
		{
			FVector2D Sz;
			if (!AsdaParseVec2(Val, Sz)) { ++NumBad; continue; }
			FAsdaWindowSavedLayout& L = Self->SavedLayouts.FindOrAdd(FName(*Rest));
			L.Size = Sz; L.bHasSize = true;
			++NumSize;
		}
		else if (Kind == TEXT("Flag"))
		{
			Self->SavedLayouts.FindOrAdd(FName(*Rest)).bSizeFlag = Val.ToBool() || Val == TEXT("1");
		}
		else { ++NumBad; }
	}
	UE_LOG(LogAsdaWindow, Log,
		TEXT("[раскладка окон] прочитано из «%s»: окон %d, позиций %d, размеров %d%s"),
		*GGameUserSettingsIni, Self->SavedLayouts.Num(), NumPos, NumSize,
		NumBad > 0 ? *FString::Printf(TEXT(", НЕРАЗОБРАННЫХ строк %d (правились руками?)"), NumBad) : TEXT(""));
}

void UAsdaWindowSubsystem::FlushWindowLayout()
{
	if (!bLayoutDirty || !GConfig) { return; }
	bLayoutDirty = false;
	// Секция переписывается ЦЕЛИКОМ: так удаление окна из раскладки (сброс) не оставляет
	//   осиротевших ключей, а порядок строк остаётся стабильным между запусками.
	GConfig->EmptySection(kLayoutSection, GGameUserSettingsIni);
	int32 NumKeys = 0;
	for (const TPair<FName, FAsdaWindowSavedLayout>& Kv : SavedLayouts)
	{
		const FString Win = Kv.Key.ToString();
		for (const TPair<FName, FVector2D>& Off : Kv.Value.Offsets)
		{
			if (Off.Value.IsNearlyZero()) { continue; }   // нулевой сдвиг = дефолт, в файле не нужен
			const TCHAR* Prefix = Kv.Value.ExternalKeys.Contains(Off.Key) ? TEXT("PosExt") : TEXT("Pos");
			GConfig->SetString(kLayoutSection, *FString::Printf(TEXT("%s.%s.%s"), Prefix, *Win, *Off.Key.ToString()),
				*AsdaVec2ToString(Off.Value), GGameUserSettingsIni);
			++NumKeys;
		}
		if (Kv.Value.bHasSize)
		{
			GConfig->SetString(kLayoutSection, *FString::Printf(TEXT("Size.%s"), *Win),
				*AsdaVec2ToString(Kv.Value.Size), GGameUserSettingsIni);
			GConfig->SetString(kLayoutSection, *FString::Printf(TEXT("Flag.%s"), *Win),
				Kv.Value.bSizeFlag ? TEXT("1") : TEXT("0"), GGameUserSettingsIni);
			NumKeys += 2;
		}
	}
	GConfig->Flush(false, GGameUserSettingsIni);
	UE_LOG(LogAsdaWindow, Verbose, TEXT("[раскладка окон] записано ключей %d в «%s»"),
		NumKeys, *GGameUserSettingsIni);
}

int32 UAsdaWindowSubsystem::DebugFillAllLayouts(FVector2D Off)
{
	const UAsdaJuiWindowMetaAsset* Meta = GetJuiMeta();
	if (!Meta) { return 0; }
	LoadAllLayouts();
	int32 N = 0;
	for (const FAsdaJuiWindowMeta& M : Meta->Windows)
	{
		for (const FAsdaJuiDragHandle& H : M.DragHandles)
		{
			const FName Win = H.TargetWindowId.IsNone() ? M.WindowId : H.TargetWindowId;
			if (Win.IsNone() || H.TargetControl.IsNone()) { continue; }
			FAsdaWindowSavedLayout& L = SavedLayouts.FindOrAdd(Win);
			if (L.ExternalKeys.Contains(H.TargetControl)) { continue; }   // ключ владельца окна — не наш
			if (!L.Offsets.Contains(H.TargetControl)) { ++N; }
			L.Offsets.Add(H.TargetControl, Off);
		}
	}
	bLayoutDirty = true;
	FlushWindowLayout();
	UE_LOG(LogAsdaWindow, Log, TEXT("[раскладка окон] самотест: всем %d ключам меты записан сдвиг (%.0f,%.0f)"), N, Off.X, Off.Y);
	return N;
}

bool UAsdaWindowSubsystem::IsWindowLayoutSaveable(FName WindowId) const
{
	const FAsdaWindowEntry* E = Windows.Find(WindowId);
	if (!E) { return false; }
	// Модальные окна появляются по месту (центр экрана) — их позиция не пользовательская.
	if (E->bModal || E->ZOrder >= kModalZ) { return false; }
	// ПРИЗНАК ИЗ ДАННЫХ: ручка перетаскивания объявлена в .JUI. `DragHandles` уже учитывает
	//   список-исключение `GDragDisabledWindows` (чат 2500 / журнал 2570) — там он пуст.
	return E->DragHandles.Num() > 0;
}

bool UAsdaWindowSubsystem::HasSavedWindowOffset(FName WindowId) const
{
	LoadAllLayouts();
	const FAsdaWindowSavedLayout* L = SavedLayouts.Find(WindowId);
	if (!L) { return false; }
	for (const TPair<FName, FVector2D>& Kv : L->Offsets)
	{
		if (!Kv.Value.IsNearlyZero()) { return true; }
	}
	return false;
}

bool UAsdaWindowSubsystem::GetSavedWindowOffset(FName WindowId, FName TargetKey, FVector2D& OutOffset) const
{
	LoadAllLayouts();
	const FAsdaWindowSavedLayout* L = SavedLayouts.Find(WindowId);
	if (!L || L->Offsets.Num() == 0) { return false; }
	if (TargetKey.IsNone())
	{
		// Ключ не задан — отдаём первую цель. У 120 из 123 окон-с-ручкой цель ровно одна (корень окна).
		for (const TPair<FName, FVector2D>& Kv : L->Offsets) { OutOffset = Kv.Value; return true; }
		return false;
	}
	if (const FVector2D* F = L->Offsets.Find(TargetKey)) { OutOffset = *F; return true; }
	return false;
}

bool UAsdaWindowSubsystem::GetAppliedWindowOffset(FName WindowId, FName TargetKey, FVector2D& OutOffset) const
{
	const FAsdaWindowEntry* E = Windows.Find(WindowId);
	if (!E) { return false; }
	if (TargetKey.IsNone())
	{
		// Ключ не задан — отдаём первую цель, СИММЕТРИЧНО `GetSavedWindowOffset`. Так спрашивают
		//   владельцы, которые считают позицию окна сами и обязаны знать, на сколько окно уже
		//   сдвинуто пользователем (трекер заданий 1800: он каждый кадр переписывает верх окна и
		//   без этой поправки затирал бы перетаскивание). У 120 из 123 окон-с-ручкой цель одна.
		for (const TPair<FName, FVector2D>& Kv : E->AppliedOffsets) { OutOffset = Kv.Value; return true; }
		return false;
	}
	if (const FVector2D* F = E->AppliedOffsets.Find(TargetKey)) { OutOffset = *F; return true; }
	return false;
}

// Привести учёт к факту: и сохранённое, и «применённое» становятся равны Total (см. шапку в .h).
void UAsdaWindowSubsystem::SyncSavedWindowOffset(FName WindowId, FName TargetKey, FVector2D TotalOffset)
{
	if (WindowId.IsNone()) { return; }
	// ★★★ ПОПРАВКА РЕВИЗИИ 25.09.2026 (КРИТ-4): `NAME_None` БОЛЬШЕ НЕ «ТИХИЙ ВЫХОД».
	//   БОЛЕЗНЬ. Строка была `if (WindowId.IsNone() || TargetKey.IsNone()) return;`, а оба
	//   ЧИТАТЕЛЯ пары — `GetSavedWindowOffset` (:603-608) и `GetAppliedWindowOffset` (:623-631) —
	//   `NAME_None` трактуют как «первая (обычно единственная) цель». Асимметрия стоила молча
	//   пропущенной защиты: `UAsdaPetCageSubsystem::PlaceChildBesideCage` звала синхронизацию
	//   именно с `NAME_None`, и в логах живых прогонов не было НИ ОДНОЙ строки
	//   «база учёта синхронизирована владельцем», хотя постановка окна отрабатывала дважды.
	//   ⇒ риск, ради которого функцию и звали (драг-дельты лягут на неверную базу, в ini уедет
	//   мусор), оставался открытым.
	//   ЛЕЧЕНИЕ. Разрешаем ключ так же, как его разрешают читатели, и ДОБАВЛЯЕМ ВТОРОЙ ИСТОЧНИК:
	//   если сохранённых целей ещё нет (первая постановка окна), берём ту цель, в которую будет
	//   копить перетаскивание — `DragHandles[0].TargetControl` (`AccumulateOffset`, :1360-1361).
	//   Без этого база записалась бы под ключом, которого драг никогда не коснётся.
	//   Не нашли ни там, ни там — ЖАЛУЕМСЯ В ЛОГ, а не выходим молча (закон `false-negatives-method`).
	if (TargetKey.IsNone())
	{
		LoadAllLayouts();
		if (const FAsdaWindowSavedLayout* L0 = SavedLayouts.Find(WindowId))
		{
			for (const TPair<FName, FVector2D>& Kv : L0->Offsets) { TargetKey = Kv.Key; break; }
		}
		if (TargetKey.IsNone())
		{
			if (const FAsdaWindowEntry* E0 = Windows.Find(WindowId))
			{
				for (const FAsdaJuiDragHandle& H : E0->DragHandles)
				{
					if (!H.TargetControl.IsNone()) { TargetKey = H.TargetControl; break; }
				}
			}
		}
		if (TargetKey.IsNone())
		{
			UE_LOG(LogAsdaWindow, Warning,
				TEXT("[раскладка окон] «%s»: базу учёта синхронизировать НЕ ПОД ЧЕМ — ни сохранённых ")
				TEXT("целей, ни ручки перетаскивания. Сдвиг (%.1f,%.1f) НЕ записан; следующее ")
				TEXT("перетаскивание ляжет на неверную базу."),
				*WindowId.ToString(), TotalOffset.X, TotalOffset.Y);
			return;
		}
	}
	LoadAllLayouts();
	FAsdaWindowSavedLayout& L = SavedLayouts.FindOrAdd(WindowId);
	L.Offsets.Add(TargetKey, TotalOffset);
	L.ExternalKeys.Remove(TargetKey);          // ключ обычный: восстанавливает общий механизм
	if (FAsdaWindowEntry* E = Windows.Find(WindowId))
	{
		E->AppliedOffsets.Add(TargetKey, TotalOffset);   // база для дальнейших драг-дельт
	}
	bLayoutDirty = true;
	FlushWindowLayout();
	UE_LOG(LogAsdaWindow, Log, TEXT("[раскладка окон] «%s».«%s»: база учёта синхронизирована владельцем ← (%.1f,%.1f)"),
		*WindowId.ToString(), *TargetKey.ToString(), TotalOffset.X, TotalOffset.Y);
}

void UAsdaWindowSubsystem::SetSavedWindowOffset(FName WindowId, FName TargetKey, FVector2D Offset)
{
	if (WindowId.IsNone()) { return; }
	LoadAllLayouts();
	const FName Key = TargetKey.IsNone() ? WindowId : TargetKey;
	FAsdaWindowSavedLayout& L = SavedLayouts.FindOrAdd(WindowId);
	L.Offsets.Add(Key, Offset);
	// ★ Ключ помечается «применяет владелец»: общий механизм его НЕ двигает и НЕ клампит — иначе
	//   сдвиг лёг бы дважды (реестром и владельцем) или был бы затёрт формулой владельца.
	L.ExternalKeys.Add(Key);
	bLayoutDirty = true;
	FlushWindowLayout();
	UE_LOG(LogAsdaWindow, Log, TEXT("[раскладка окон] «%s».«%s» ← (%.1f,%.1f) (записал владелец окна)"),
		*WindowId.ToString(), *Key.ToString(), Offset.X, Offset.Y);
}

void UAsdaWindowSubsystem::SaveWindowSize(FName WindowId, FVector2D BaseSize, bool bFlag)
{
	if (WindowId.IsNone() || BaseSize.X <= 0.f || BaseSize.Y <= 0.f) { return; }
	LoadAllLayouts();
	FAsdaWindowSavedLayout& L = SavedLayouts.FindOrAdd(WindowId);
	if (L.bHasSize && L.Size.Equals(BaseSize, 0.5f) && L.bSizeFlag == bFlag) { return; }
	L.Size = BaseSize; L.bHasSize = true; L.bSizeFlag = bFlag;
	bLayoutDirty = true;
	FlushWindowLayout();
	UE_LOG(LogAsdaWindow, Log, TEXT("[раскладка окон] размер «%s» ← %.0fx%.0f (признак=%d)"),
		*WindowId.ToString(), BaseSize.X, BaseSize.Y, bFlag ? 1 : 0);
}

bool UAsdaWindowSubsystem::GetSavedWindowSize(FName WindowId, FVector2D& OutBaseSize, bool* OutFlag) const
{
	LoadAllLayouts();
	const FAsdaWindowSavedLayout* L = SavedLayouts.Find(WindowId);
	if (!L || !L->bHasSize) { return false; }
	OutBaseSize = L->Size;
	if (OutFlag) { *OutFlag = L->bSizeFlag; }
	return true;
}

// Рецепт «что двигать» берётся ИЗ МЕТЫ .JUI, а не из кода. Перебираем ВСЕ окна меты, потому что
//   цель может принадлежать ДРУГОМУ окну, чем ручка (данные оригинала: `Gear_book_movetext` 80302
//   в `Gear_book_WIN` двигает `Inven_Strength_BOX3` 80200 внутри `INVEN_WIN`).
bool UAsdaWindowSubsystem::FindDragRecipe(FName TargetWindowId, FName TargetKey,
	bool& bOutRoot, TArray<FName>& OutWidgets) const
{
	const UAsdaJuiWindowMetaAsset* Meta = GetJuiMeta();
	if (!Meta) { return false; }
	for (const FAsdaJuiWindowMeta& M : Meta->Windows)
	{
		for (const FAsdaJuiDragHandle& H : M.DragHandles)
		{
			const FName Win = H.TargetWindowId.IsNone() ? M.WindowId : H.TargetWindowId;
			if (Win != TargetWindowId || H.TargetControl != TargetKey) { continue; }
			bOutRoot = H.bTargetIsWindowRoot;
			OutWidgets = H.TargetWidgets;
			return true;
		}
	}
	return false;
}

void UAsdaWindowSubsystem::ShiftTargetByKey(FName WindowId, FName TargetKey, FVector2D Delta)
{
	if (Delta.IsNearlyZero()) { return; }
	const FAsdaWindowEntry* E = Windows.Find(WindowId);
	if (!E || !E->Widget) { return; }
	bool bRoot = false;
	TArray<FName> Widgets;
	if (!FindDragRecipe(WindowId, TargetKey, bRoot, Widgets))
	{
		UE_LOG(LogAsdaWindow, Warning,
			TEXT("[раскладка окон] «%s».«%s»: рецепта сдвига в мете .JUI нет — сохранённая позиция ")
			TEXT("не применена (окно переименовано в данных или мета не перегенерирована)"),
			*WindowId.ToString(), *TargetKey.ToString());
		return;
	}
	if (bRoot)
	{
		if (UPanelWidget* RootCv = Cast<UPanelWidget>(E->Widget->GetRootWidget()))
		{
			for (int32 i = 0; i < RootCv->GetChildrenCount(); ++i) { ShiftCanvasSlot(RootCv->GetChildAt(i), Delta); }
		}
		return;
	}
	for (const FName& N : Widgets) { ShiftCanvasSlot(E->Widget->GetWidgetFromName(N), Delta); }
}

void UAsdaWindowSubsystem::AccumulateOffset(FName TargetWindowId, FName TargetKey, FVector2D Delta)
{
	if (TargetWindowId.IsNone() || TargetKey.IsNone() || Delta.IsNearlyZero()) { return; }
	LoadAllLayouts();
	FAsdaWindowEntry* TE = Windows.Find(TargetWindowId);
	if (!TE) { return; }
	FVector2D& Acc = TE->AppliedOffsets.FindOrAdd(TargetKey);
	Acc += Delta;
	SavedLayouts.FindOrAdd(TargetWindowId).Offsets.Add(TargetKey, Acc);
	bLayoutDirty = true;
}

// Применить сохранённые сдвиги окна. ИДЕМПОТЕНТНО: двигаем на разницу «сохранено − уже применено»,
//   поэтому повторный вызов ничего не ломает, а порядок относительно чужих сдвигов не важен
//   (сдвиги складываются: центрирование карты мира `CenterBigMapWindow` даёт тот же результат
//   до и после нас).
void UAsdaWindowSubsystem::ApplySavedLayout(FName WindowId)
{
	LoadAllLayouts();
	const FAsdaWindowSavedLayout* L = SavedLayouts.Find(WindowId);
	if (!L || L->Offsets.Num() == 0) { return; }
	FAsdaWindowEntry* E = Windows.Find(WindowId);
	if (!E || !E->Widget) { return; }
	// Модалка позицию не восстанавливает (появляется по месту), запись из старого ini игнорируем.
	if (E->bModal || E->ZOrder >= kModalZ) { return; }
	int32 Applied = 0;
	for (const TPair<FName, FVector2D>& Kv : L->Offsets)
	{
		// Ключи владельца окна пропускаем: их применяет он сам (см. ExternalKeys).
		if (L->ExternalKeys.Contains(Kv.Key)) { continue; }
		const FVector2D Cur = E->AppliedOffsets.FindRef(Kv.Key);
		const FVector2D Delta = Kv.Value - Cur;
		if (Delta.IsNearlyZero()) { continue; }
		ShiftTargetByKey(WindowId, Kv.Key, Delta);
		E->AppliedOffsets.Add(Kv.Key, Kv.Value);
		E->ClampPendingKeys.Add(Kv.Key);   // М-2: заявка на кламп — поключево (см. FAsdaWindowEntry)
		++Applied;
	}
	if (Applied > 0)
	{
		ClampWindowBySlotsNow(WindowId);   // М-1: в экран ДО первой отрисовки — по слотам (живой геометрии ещё нет)
		LiveClampNotBeforeFrame = FMath::Max(LiveClampNotBeforeFrame, GFrameCounter + 1);   // слоты сдвинуты — геометрия старая
		PendingClamp.Add(WindowId);   // живая проверка — когда появится геометрия (см. TickLayoutRestore)
		UE_LOG(LogAsdaWindow, Log, TEXT("[раскладка окон] «%s»: восстановлено сдвигов %d из ini"),
			*WindowId.ToString(), Applied);
	}
}

// ★★★ ПРЯМОУГОЛЬНИК РЕБЁНКА КАНВАСА В ЛОКАЛЬНЫХ КООРДИНАТАХ РОДИТЕЛЯ — ОДНА ФОРМУЛА НА ОБА ПУТИ БЕЗ ЖИВОЙ ГЕОМЕТРИИ
//   (`SlotAbsRectViaParent` — от живого родителя, `SlotRectInRootLocal` — по цепочке слотов до корня, М-1 07.10.2026).
//   Формула `SConstraintCanvas::ArrangeLayeredChildren`: точечный анкор — Right/Bottom = размер, alignment сдвигает на
//   −Size·Alignment; растянутый — от обоих краёв, alignment не применяется. PSize — локальный размер родителя.
//   false — получился не положительный размер.
static bool CanvasChildLocalRect(const UCanvasPanelSlot* S, const FVector2D& PSize, FVector2D& OutPos, FVector2D& OutSize)
{
	const FAnchors An = S->GetAnchors();
	const FMargin O = S->GetOffsets();
	const FVector2D Al = S->GetAlignment();
	// По X: точечный анкор — Right = ширина, alignment сдвигает; растянутый — от обоих краёв.
	if (FMath::IsNearlyEqual(An.Minimum.X, An.Maximum.X))
	{
		OutSize.X = O.Right;
		OutPos.X = An.Minimum.X * PSize.X + O.Left - Al.X * OutSize.X;
	}
	else
	{
		OutPos.X = An.Minimum.X * PSize.X + O.Left;
		OutSize.X = An.Maximum.X * PSize.X - O.Right - OutPos.X;
	}
	// По Y — то же правило.
	if (FMath::IsNearlyEqual(An.Minimum.Y, An.Maximum.Y))
	{
		OutSize.Y = O.Bottom;
		OutPos.Y = An.Minimum.Y * PSize.Y + O.Top - Al.Y * OutSize.Y;
	}
	else
	{
		OutPos.Y = An.Minimum.Y * PSize.Y + O.Top;
		OutSize.Y = An.Maximum.Y * PSize.Y - O.Bottom - OutPos.Y;
	}
	return OutSize.X > 0.0 && OutSize.Y > 0.0;
}

// ★★★ ГАБАРИТЫ КУСКА ОКНА БЕЗ ЕГО ЖИВОЙ ГЕОМЕТРИИ (С-2 ревизии расширения окон, 07.10.2026).
//   Кусок рамки, целиком ушедший за кромку экрана, Slate не рисует — `GetCachedGeometry` у него пустая.
//   Его прямоугольник считаем по СЛОТУ канвы от ЖИВОЙ геометрии РОДИТЕЛЯ ровно той формулой, которой
//   раскладывает `SConstraintCanvas::ArrangeLayeredChildren` (анкоры, смещения, alignment; растянутые
//   анкоры — от обоих краёв). Родитель (обычно корневой канвас окна во весь экран) нарисован всегда.
//   false — слот не канвасный, авторазмер, родитель скрыт или сам без геометрии (тогда — прежний путь).
static bool SlotAbsRectViaParent(const UWidget* W, FVector2D& OutMin, FVector2D& OutMax)
{
	const UCanvasPanelSlot* S = AsdaUi::SlotOf(W);
	const UPanelWidget* Parent = W ? W->GetParent() : nullptr;
	if (!S || !Parent || S->GetAutoSize()) { return false; }
	// Вся цепочка предков видима: у скрытого предка геометрия протухшая (последний кадр показа).
	for (const UWidget* Q = Parent; Q; Q = Q->GetParent())
	{
		const ESlateVisibility QV = Q->GetVisibility();
		if (QV == ESlateVisibility::Collapsed || QV == ESlateVisibility::Hidden) { return false; }
	}
	const FGeometry PG = Parent->GetCachedGeometry();
	const FVector2D PSize(PG.GetLocalSize());            // локальный размер родителя, юниты канвы
	if (PSize.X <= 0.0 || PSize.Y <= 0.0) { return false; }
	FVector2D Pos, Size;                                  // прямоугольник куска в локальных координатах родителя
	if (!CanvasChildLocalRect(S, PSize, Pos, Size)) { return false; }
	OutMin = FVector2D(PG.LocalToAbsolute(Pos));
	OutMax = FVector2D(PG.LocalToAbsolute(Pos + Size));
	return true;
}

// ★★★ М-1 ревизии `widen_fixes2_REVIEW.md` (07.10.2026): ПРЯМОУГОЛЬНИК ВИДЖЕТА В ЛОКАЛЬНЫХ ЮНИТАХ КОРНЯ ОКНА — ПО ЦЕПОЧКЕ
//   СЛОТОВ КАНВАСА, БЕЗ ЖИВОЙ ГЕОМЕТРИИ. Нужен там, где Slate окно ещё ни разу не раскладывал (кадр создания) или где
//   его геометрия устарела (смена размера вьюпорта): корень окна во вьюпорте полноэкранный, его размер известен
//   (`RootSize` = вьюпорт / DPI), а дальше вниз — та же формула канваса. Render-трансформы не учитываются: в разметке
//   окон JUI их нет; если где-то есть, следующий живой кламп по геометрии поправит.
//   false — в цепочке не канвасный слот, авторазмер или СКРЫТЫЙ виджет (его кусок клампится при показе).
static bool SlotRectInRootLocal(const UWidget* W, const UWidget* Root, const FVector2D& RootSize,
	FVector2D& OutPos, FVector2D& OutSize)
{
	if (!W) { return false; }
	if (W == Root) { OutPos = FVector2D::ZeroVector; OutSize = RootSize; return true; }
	const ESlateVisibility V = W->GetVisibility();
	if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { return false; }
	const UCanvasPanelSlot* S = AsdaUi::SlotOf(W);
	const UPanelWidget* Parent = W->GetParent();
	if (!S || !Parent || S->GetAutoSize()) { return false; }
	FVector2D PPos, PSize;                                // прямоугольник родителя в юнитах корня
	if (!SlotRectInRootLocal(Parent, Root, RootSize, PPos, PSize)) { return false; }
	FVector2D Pos, Size;                                  // прямоугольник виджета в локальных координатах родителя
	if (!CanvasChildLocalRect(S, PSize, Pos, Size)) { return false; }
	OutPos = PPos + Pos;
	OutSize = Size;
	return true;
}

// Поправка, загоняющая прямоугольник [Mn..Mx] в экран [ViewMin..ViewMax] — ОДНА формула для клампа по слотам и по
//   живой геометрии. Сначала ПРАВЫЙ/НИЖНИЙ край внутрь, затем ЛЕВЫЙ/ВЕРХНИЙ. Порядок важен: если окно шире экрана (или
//   габариты раздуты растянутым контролом), побеждает левый-верхний угол — заголовок с ручкой остаётся доступным, и
//   окно всегда можно утащить обратно.
static FVector2D ClampAdjustIntoView(const FVector2D& Mn, const FVector2D& Mx, const FVector2D& ViewMin, const FVector2D& ViewMax)
{
	FVector2D Adj(0.f, 0.f);
	if (Mx.X > ViewMax.X) { Adj.X = ViewMax.X - Mx.X; }
	if (Mn.X + Adj.X < ViewMin.X) { Adj.X = ViewMin.X - Mn.X; }
	if (Mx.Y > ViewMax.Y) { Adj.Y = ViewMax.Y - Mx.Y; }
	if (Mn.Y + Adj.Y < ViewMin.Y) { Adj.Y = ViewMin.Y - Mn.Y; }
	return Adj;
}

// ★ 08.10.2026 (пакет 5, СРЕДН-2 ревизии пакета 4): шум арифметики позиций — смещения слотов хранятся в float (`FMargin`),
//   сдвиги в ini — с тремя знаками. Рамка, прижатая ровно к краю, на перезапуске мерилась на тысячные доли пикселя «за экраном»,
//   и правило «за экраном → дефолт» уводило окно (личный магазин у правого края). Это НЕ допуск правила владельца (допуск 0 px
//   остаётся): 1/100 пикселя на экране не видно, это граница погрешности вычислений, а не позиции.
static constexpr double kAsdaOffscreenNoisePx = 0.01;

// Пакет 5 (СРЕДН-2): поправку возврата при отпускании (`Back`, из `ClampAdjustIntoView`) довести до ЦЕЛОГО пикселя ВНУТРЬ экрана
//   [0..View]: по оси, прижатой к правому/нижнему краю, край рамки — не дальше `Floor(View)`, сдвиг ключа — вниз до целого
//   (`Floor`); по оси, прижатой к левому/верхнему краю (и у окна больше экрана), сдвиг — вверх до целого (`Ceil`). Ось без
//   поправки не трогается. OldOff — применённый сдвиг ключа до возврата. Возвращает false, если двигать нечего.
static bool AsdaRoundReleaseBackInside(const FVector2D& Mn, const FVector2D& Mx, const FVector2D& View, const FVector2D& OldOff,
	FVector2D& InOutBack)
{
	constexpr double kSnapEps = 1e-3;   // дробь меньше этой — шум float, а не доля пикселя
	for (int32 Ax = 0; Ax < 2; ++Ax)
	{
		double& B = InOutBack[Ax];
		if (FMath::Abs(B) < 0.01) { B = 0.0; continue; }               // ось не трогали
		const bool bLowEdge = Mn[Ax] + B <= kSnapEps;                   // прижато к левому/верхнему краю
		if (!bLowEdge) { B = FMath::Min(B, FMath::FloorToDouble(View[Ax]) - Mx[Ax]); }   // правый/нижний край ≤ Floor(View)
		const double NewOff = OldOff[Ax] + B;                           // сдвиг ключа после возврата
		const double Whole = bLowEdge ? FMath::CeilToDouble(NewOff - kSnapEps) : FMath::FloorToDouble(NewOff + kSnapEps);
		B = Whole - OldOff[Ax];
	}
	return !InOutBack.IsNearlyZero(0.01);
}

// ★★★ [ПОРТ] ПРАВИЛО ВЛАДЕЛЬЦА 08.10.2026 «ЗАЩИТА ОТ ОКНА ЗА ЭКРАНОМ» (уточнение владельца в тот же день): сохранённая позиция
//   выводит рамку окна (или под-окна со своим ключом) за экран хоть на пиксель (допуск 0) — окно возвращается в СВОЁ
//   ДЕФОЛТНОЕ положение: туда, где его ставит оригинал при первом показе (разметка .JUI = позиция WBP; у окон, которые
//   ставит код, — эта постановка). Сдвиг ключа просто снимается. Дефолт сам за экраном (данные оригинала: QUICK_WIN справа
//   10, CHAT_NEW_SERV снизу 30, под-окна OldSoulmate_Win и Inven_Strength_BOX3 — отчёт `Reports/owner_1008/window_default_pos.md`
//   §5) — дефолт прижимается к краю. Окна в экране не трогаются; в ini ничего не пишется (там остаётся позиция игрока).
//   Обоснование: оригинал позиций между сессиями не хранит вовсе, каждое окно рождается в дефолте (тот же отчёт §1.5).
//   Режимы (ключ отката `asda.UI.OffscreenToCenter`):
//     2 (умолчание) — дефолт окна, у дефолта за экраном — прижатие к краю;
//     1 — центр экрана по ВИДИМОЙ РАМКЕ окна (прежнее правило 08.10; HUD из `GOffscreenEdgeClampKeys` и окна больше экрана —
//         к краю); под-окно со своим ключом при этом ставится относительно корня (сдвиг снят), не центрируется само;
//     0 — прежнее прижатие к краю у всех.
static ASDA_DEV_CVAR(int32, CVarOffscreenToCenter,
	TEXT("asda.UI.OffscreenToCenter"), 2,
	TEXT("Окно, сохранённая позиция которого выводит рамку за экран: 2 — в дефолтное положение окна (уточнение владельца ")
	TEXT("08.10.2026; дефолт за экраном — прижать к краю); 1 — по центру экрана по видимой рамке; 0 — прежнее прижатие к краю."),
	ECVF_Default);

// Окна/ключи с фиксированным якорем на краю экрана (HUD): в режиме 1 (центр) за экраном — прижатие к краю, не центр.
//   «Окно» или «Окно.Ключ». В режиме 2 (дефолт) им, как и всем, возвращается дефолт — он у них и есть край экрана.
static const TSet<FName>& GOffscreenEdgeClampKeys()
{
	static const TSet<FName> S = {
		FName(TEXT("CHAT_NEW")), FName(TEXT("CHAT_NEW_SYSTEM")), FName(TEXT("CHAT_NEW_TABX")),   // чат и журнал (растягиваемые, у края)
		FName(TEXT("MINIMAP_WIN")), FName(TEXT("MINIMAPMIN_WIN")), FName(TEXT("OCCUPATION_BOX")), // мини-карта
		FName(TEXT("MINISTATE_WIN")), FName(TEXT("PVP_TARGET")),                                   // мини-статус, рамка цели
		FName(TEXT("QUICK_NEW_WIN")), FName(TEXT("SYSTEMMAIN_NEW_WIN")), FName(TEXT("EXPBar_WIn")), // хотбар, трей, полоса опыта
		FName(TEXT("SKILL_BAR_Win")), FName(TEXT("Quest_State_Win")),                              // полоса каста, трекер квестов
		FName(TEXT("CAMPWAR_Time_Win")), FName(TEXT("CAMPWAR_Time_Win.CAMPWAR_Button")) };         // таймер войны и его кнопка
	return S;
}

// Чем правило «за экраном» поправило ключ (для журнала и для под-окон своего корня).
enum class EAsdaOffscreenFix : uint8
{
	None,             // в экране — не трогали
	Edge,             // прижатие к краю (режим 0, HUD/окно больше экрана в режиме 1)
	Center,           // центр экрана (режим 1)
	Default,          // дефолт окна (режим 2; под-окно относительно корня)
	DefaultThenEdge,  // дефолт сам за экраном — прижат к краю
};

// Подпись поправки для журнала.
static const TCHAR* AsdaOffscreenFixName(EAsdaOffscreenFix F)
{
	switch (F)
	{
	case EAsdaOffscreenFix::Edge:            return TEXT("прижато к краю");
	case EAsdaOffscreenFix::Center:          return TEXT("ПО ЦЕНТРУ");
	case EAsdaOffscreenFix::Default:         return TEXT("В ДЕФОЛТ");
	case EAsdaOffscreenFix::DefaultThenEdge: return TEXT("В ДЕФОЛТ (дефолт за экраном — прижат к краю)");
	default:                                 return TEXT("—");
	}
}

// Поправка окна/под-окна в экран по правилу выше. Mn/Mx — рамка ключа, KeyOffset — его применённый сдвиг (в тех же
//   единицах, что Mn/Mx); bRootKey — ключ двигает весь корпус окна; bRootFixed — корень этого окна правилом уже поставлен.
static FVector2D OffscreenAdjust(FName WindowId, FName Key, bool bRootKey, const FVector2D& Mn, const FVector2D& Mx,
	const FVector2D& ViewMin, const FVector2D& ViewMax, const FVector2D& KeyOffset, bool bRootFixed, EAsdaOffscreenFix& OutFix)
{
	OutFix = EAsdaOffscreenFix::None;
	// Допуск 0 px; сравнение — с запасом на шум вычислений (`kAsdaOffscreenNoisePx`, пакет 5, СРЕДН-2).
	const double Ns = kAsdaOffscreenNoisePx;
	const bool bOut = Mn.X < ViewMin.X - Ns || Mn.Y < ViewMin.Y - Ns || Mx.X > ViewMax.X + Ns || Mx.Y > ViewMax.Y + Ns;
	if (!bOut) { return FVector2D::ZeroVector; }
	const int32 Mode = CVarOffscreenToCenter.GetValueOnGameThread();
	if (Mode == 0) { OutFix = EAsdaOffscreenFix::Edge; return ClampAdjustIntoView(Mn, Mx, ViewMin, ViewMax); }
	// Дефолт: режим 2 — всем; режим 1 — под-окну, чей корень уже поставлен (относительно корня, СРЕДН-2 ревизии).
	if (Mode == 2 || (!bRootKey && bRootFixed))
	{
		const FVector2D Back = -KeyOffset;                                          // снять сдвиг ключа = дефолт
		const FVector2D Edge = ClampAdjustIntoView(Mn + Back, Mx + Back, ViewMin, ViewMax);
		OutFix = Edge.IsNearlyZero(0.01) ? EAsdaOffscreenFix::Default : EAsdaOffscreenFix::DefaultThenEdge;
		return Back + Edge;
	}
	const bool bFits = (Mx.X - Mn.X) <= (ViewMax.X - ViewMin.X) && (Mx.Y - Mn.Y) <= (ViewMax.Y - ViewMin.Y);
	const bool bEdge = GOffscreenEdgeClampKeys().Contains(WindowId)
		|| GOffscreenEdgeClampKeys().Contains(FName(*FString::Printf(TEXT("%s.%s"), *WindowId.ToString(), *Key.ToString())));
	if (bEdge || !bFits) { OutFix = EAsdaOffscreenFix::Edge; return ClampAdjustIntoView(Mn, Mx, ViewMin, ViewMax); }
	OutFix = EAsdaOffscreenFix::Center;
	// Целые пиксели (ревизия М-3): округляется ИТОГОВЫЙ левый-верх рамки, а не разность — иначе дробная база слота
	//   (StreetStall_Win 288.5) оставалась дробной.
	const FVector2D Want = ((ViewMin + ViewMax) - (Mx - Mn)) * 0.5;
	return FVector2D(FMath::RoundToDouble(Want.X) - Mn.X, FMath::RoundToDouble(Want.Y) - Mn.Y);
}

// ═══ ★★★ 04.11.2026 (жалоба игрока через владельца, малые экраны): ОКНО МОЖНО ДЕРЖАТЬ ЧАСТИЧНО ЗА КРАЕМ ═══════════════
//   БЫЛО: окно, отпущенное хоть на пиксель за краем, прижималось обратно (`asda.UI.DragReleaseClamp`, пакет 4 от 08.10),
//   а при открытии/перезапуске/смене разрешения рамка за экраном (допуск 0) уводила окно в дефолт. Игрок на 1024×768 не мог
//   отодвинуть окно настроек графики, чтобы видеть персонажа.
//   ОРИГИНАЛ (ui_core.c:114010 `uiControl_RepositionByDelta@0x12F1C30`, :113885 событие 0x20003): pos += курсор − курсор_пред,
//   клампа нет нигде (ни в перетаскивании, ни в сеттере `CControl_SetField74_75@0x97D500`, ни при смене разрешения), позиции
//   между сессиями не хранятся. `SetCapture` клиент не зовёт ни разу (во всём эталоне нет) — движения мыши приходят, только пока
//   курсор в окне игры (game_main.c:5734 `GetCursorPos`+`ScreenToClient`), поэтому схваченная точка ручки всегда остаётся на
//   экране: окно можно увести частично, но за ручку его всегда можно взять обратно.
//   СТАЛО [ПОРТ, ключ `asda.UI.OffscreenKeepGrab`]: окно считается «потерянным», только если на экране не осталось куска ручки
//   перетаскивания (заголовка) не меньше `asda.UI.OffscreenMinGrabPx` по каждой оси (у ручки меньше порога — вся ручка).
//   Отпускание: окно остаётся, где отпустили; лишь если ручка ушла дальше порога, окно подтягивается на минимум, чтобы
//   ручка стала досягаемой. Открытие/перезапуск/смена разрешения: досягаемое окно не трогается; недосягаемое — прежняя
//   защита владельца 08.10 (дефолт окна, `asda.UI.OffscreenToCenter`). Ключ 0 — прежнее поведение целиком (рамка в экране).
static ASDA_DEV_CVAR(int32, CVarOffscreenKeepGrab,
	TEXT("asda.UI.OffscreenKeepGrab"), 1,
	TEXT("1 — окно можно оставить частично за краем экрана, пока на экране кусок ручки перетаскивания не меньше ")
	TEXT("asda.UI.OffscreenMinGrabPx (04.11.2026, как оригинал: клампа нет, схваченная точка остаётся на экране); ")
	TEXT("0 — прежнее правило 08.10: рамка окна целиком в экране (отпускание прижимает, открытие уводит в дефолт)."),
	ECVF_Default);

// Порог досягаемости ручки в пикселях экрана: по горизонтали и по вертикали (у ручки меньше порога — её полный размер).
//   По вертикали меньше: полоса заголовка обычно ~20 px, и порог 24 запретил бы уводить окно за верхний край вовсе.
static ASDA_DEV_CVAR(int32, CVarOffscreenMinGrabPx,
	TEXT("asda.UI.OffscreenMinGrabPx"), 24,
	TEXT("Сколько пикселей ручки перетаскивания (заголовка) окна по ГОРИЗОНТАЛИ должно остаться на экране, чтобы окно ")
	TEXT("считалось досягаемым (asda.UI.OffscreenKeepGrab 1). Ручка уже порога — нужна целиком."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarOffscreenMinGrabPxY,
	TEXT("asda.UI.OffscreenMinGrabPxY"), 8,
	TEXT("То же по ВЕРТИКАЛИ (asda.UI.OffscreenKeepGrab 1). Ручка ниже порога — нужна целиком."),
	ECVF_Default);

// Включено ли правило «досягаемая ручка» (ключ выше).
static bool AsdaOffscreenKeepGrabOn() { return CVarOffscreenKeepGrab.GetValueOnGameThread() != 0; }

// Нужный кусок ручки по осям: порог (в единицах прямоугольников; PxToUnits — делитель пикселей), но не больше самой ручки.
static FVector2D AsdaGrabNeed(const FVector2D& HMn, const FVector2D& HMx, double PxToUnits)
{
	const double U = FMath::Max(PxToUnits, (double)KINDA_SMALL_NUMBER);
	const double Px = FMath::Max(1, CVarOffscreenMinGrabPx.GetValueOnGameThread()) / U;
	const double Py = FMath::Max(1, CVarOffscreenMinGrabPxY.GetValueOnGameThread()) / U;
	return FVector2D(FMath::Min(Px, HMx.X - HMn.X), FMath::Min(Py, HMx.Y - HMn.Y));
}

// Ручка [HMn..HMx] досягаема: её видимый кусок в экране [ViewMin..ViewMax] не меньше нужного по обеим осям.
static bool AsdaGrabReachable(const FVector2D& HMn, const FVector2D& HMx, const FVector2D& ViewMin, const FVector2D& ViewMax, double PxToUnits)
{
	const FVector2D Need = AsdaGrabNeed(HMn, HMx, PxToUnits);
	const double Iw = FMath::Min(HMx.X, ViewMax.X) - FMath::Max(HMn.X, ViewMin.X);
	const double Ih = FMath::Min(HMx.Y, ViewMax.Y) - FMath::Max(HMn.Y, ViewMin.Y);
	return Iw >= Need.X - kAsdaOffscreenNoisePx && Ih >= Need.Y - kAsdaOffscreenNoisePx;
}

// Минимальный сдвиг, делающий ручку досягаемой (ось, где куска хватает, не трогается).
static FVector2D AsdaGrabAdjust(const FVector2D& HMn, const FVector2D& HMx, const FVector2D& ViewMin, const FVector2D& ViewMax, double PxToUnits)
{
	const FVector2D Need = AsdaGrabNeed(HMn, HMx, PxToUnits);
	FVector2D Adj(0.0, 0.0);
	for (int32 Ax = 0; Ax < 2; ++Ax)
	{
		const double I = FMath::Min(HMx[Ax], ViewMax[Ax]) - FMath::Max(HMn[Ax], ViewMin[Ax]);
		if (I >= Need[Ax] - kAsdaOffscreenNoisePx) { continue; }
		// Ручка ушла за ближний (левый/верхний) край — правый край ручки на Need от него; иначе левый край ручки на Need от дальнего.
		if (HMn[Ax] < ViewMin[Ax]) { Adj[Ax] = (ViewMin[Ax] + Need[Ax]) - HMx[Ax]; }
		else { Adj[Ax] = (ViewMax[Ax] - Need[Ax]) - HMn[Ax]; }
	}
	return Adj;
}

// ★ 04.11.2026: ручка, наиболее видимая на экране, из ВСЕХ ручек, чья цель — (WindowId, Key) (ручка бывает в чужом окне).
//   bSlots — по цепочке слотов в юнитах корня (экран 0..ViewMax, до первой отрисовки), иначе живая геометрия (абсолютные
//   пиксели; без неё — слот от живого родителя). Ручка-корень окна (во весь экран) меряется рамкой ключа FrameMn/FrameMx.
//   false — у ключа нет видимой ручки с размером (тогда работает прежнее правило «рамка в экране»).
static bool AsdaFindGrabRect(const TMap<FName, FAsdaWindowEntry>& Windows, FName WindowId, FName Key, bool bSlots,
	const FVector2D& ViewMin, const FVector2D& ViewMax, const FVector2D& FrameMn, const FVector2D& FrameMx,
	FVector2D& OutMn, FVector2D& OutMx)
{
	const bool bAddedOn = CVarAddedDragHandles.GetValueOnGameThread() != 0;
	double BestArea = -1.0;
	for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
	{
		const FAsdaWindowEntry& HE = Kv.Value;
		if (!HE.Widget) { continue; }
		const UWidget* HRoot = HE.Widget->GetRootWidget();
		for (const FAsdaJuiDragHandle& H : HE.DragHandles)
		{
			const FName Tw = H.TargetWindowId.IsNone() ? Kv.Key : H.TargetWindowId;   // окно-цель ручки
			if (Tw != WindowId || H.TargetControl != Key) { continue; }
			if (H.bAddedByPort && !bAddedOn) { continue; }                              // выключенная ручка-добавка не держит окно
			const UWidget* HW = HE.Widget->GetWidgetFromName(H.HandleWidget);
			if (!HW) { continue; }
			const ESlateVisibility HV = HW->GetVisibility();
			if (HV == ESlateVisibility::Collapsed || HV == ESlateVisibility::Hidden) { continue; }
			FVector2D A, B;
			if (HW == HRoot) { A = FrameMn; B = FrameMx; }                              // ручка = всё окно
			else if (bSlots)
			{
				FVector2D Pos, Size;
				if (!SlotRectInRootLocal(HW, HRoot, ViewMax, Pos, Size)) { continue; }
				A = Pos; B = Pos + Size;
			}
			else if (!WidgetAbsRect(HW, A, B) && !SlotAbsRectViaParent(HW, A, B)) { continue; }
			if (B.X - A.X <= 0.0 || B.Y - A.Y <= 0.0) { continue; }
			// Ручка размером с экран (корневой контрол-рамка без скина) — меряем рамкой ключа.
			if (B.X - A.X >= (ViewMax.X - ViewMin.X) - 0.5 && B.Y - A.Y >= (ViewMax.Y - ViewMin.Y) - 0.5) { A = FrameMn; B = FrameMx; }
			const double Iw = FMath::Max(0.0, FMath::Min(B.X, ViewMax.X) - FMath::Max(A.X, ViewMin.X));
			const double Ih = FMath::Max(0.0, FMath::Min(B.Y, ViewMax.Y) - FMath::Max(A.Y, ViewMin.Y));
			if (Iw * Ih > BestArea) { BestArea = Iw * Ih; OutMn = A; OutMx = B; }
		}
	}
	return BestArea >= 0.0;
}

// ★ Ревизия ui_bugs_REVIEW СРЕДН-3 (08.10.2026): ВИДИМАЯ РАМКА ОКНА — планы корневого контрола `<Ключ>_<idx>_<n>` (рамка
//   из 9 кусков у INVEN_WIN 116…344 × 56…646). Габарит «все дети корня» до первой отрисовки включает панели, которые код
//   окна гасит позже (сумка мерилась 722 px вместо 228), — окно, стоящее в экране, считалось ушедшим, а центр уезжал.
//   Рамка есть не у всех окон (корень-точка без скина) — тогда false и прежний габарит. bSlots = true — по слотам в юнитах
//   корня (`SlotRectInRootLocal`), иначе — экранный прямоугольник по слоту от живого родителя (`SlotAbsRectViaParent`).
static bool AsdaRootFrameRect(UUserWidget* Win, FName RootKey, const FVector2D& RootSize, bool bSlots, FVector2D& OutMn, FVector2D& OutMx)
{
	UPanelWidget* RootCv = Win ? Cast<UPanelWidget>(Win->GetRootWidget()) : nullptr;
	if (!RootCv) { return false; }
	const FString Idc = RootKey.ToString();
	bool bHave = false;
	OutMn = FVector2D(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
	OutMx = -OutMn;
	for (int32 i = 0; i < RootCv->GetChildrenCount(); ++i)
	{
		UWidget* Ch = RootCv->GetChildAt(i);
		FString P; int32 J = 0, Pl = 0;
		// План корневого контрола `<Ключ>_<idx>_<n>` или его хит-зона `<Ключ>_<idx>_HIT` (у окон без скина корня, EXTRACT_WIN).
		const FString Nm = Ch ? Ch->GetName() : FString();
		const bool bHit = Nm.StartsWith(Idc + TEXT("_")) && Nm.EndsWith(TEXT("_HIT"));
		if (!Ch || (!bHit && (!AsdaUi::ParsePlaneName(Nm, P, J, Pl) || P != Idc))) { continue; }
		const ESlateVisibility V = Ch->GetVisibility();
		if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { continue; }
		FVector2D A, B;
		if (bSlots)
		{
			FVector2D Pos, Size;
			if (!SlotRectInRootLocal(Ch, RootCv, RootSize, Pos, Size)) { continue; }
			A = Pos; B = Pos + Size;
		}
		else if (!SlotAbsRectViaParent(Ch, A, B)) { continue; }
		OutMn = FVector2D(FMath::Min(OutMn.X, A.X), FMath::Min(OutMn.Y, A.Y));
		OutMx = FVector2D(FMath::Max(OutMx.X, B.X), FMath::Max(OutMx.Y, B.Y));
		bHave = true;
	}
	return bHave;
}

// Ключ, ждущий клампа, и его рецепт из меты .JUI (что именно двигает этот сдвиг). Общий для клампа по слотам и по геометрии.
struct FAsdaClampRecipe
{
	FName Key;                 // ключ сдвига (цель ручки)
	bool bRoot = false;        // сдвиг двигает весь корпус окна
	TArray<FName> Widgets;     // иначе — эти виджеты под-окна
};

// ═══ ★★★ С-1 ревизии `widen_fixes_REVIEW.md` (07.10.2026): ПОД-ОКНО СО СВОИМ КЛЮЧОМ — НЕ ЧАСТЬ КОРПУСА ═══
//   БЫЛО (регресс правки С-2): в габариты корня шли ВСЕ дети корневого канваса, в том числе группы
//   `*_grp` под-окон, у которых есть собственная сохранённая позиция (чат личного магазина, кнопка
//   таймера войны, старый соулмейт в сообществе, под-окна инвентаря и магазинов — 11 окон). Ушедшее за
//   кромку под-окно раздувало габариты корня, и кламп уводил ЦЕЛОЕ окно: личный магазин −579.5,
//   таймер войны −892 (`widenfix_review_logs/c6_subtarget.log`).
//   СТАЛО: виджеты, которые двигает СОБСТВЕННЫЙ (не корневой) ключ этого окна, в габариты корня не
//   входят — под-окно клампится само, своим ключом. Вынесено в функцию 07.10.2026 (М-1): им пользуются оба клампа.
TSet<FName> UAsdaWindowSubsystem::CollectOwnKeyWidgets(FName WindowId, const FAsdaWindowEntry& E) const
{
	TSet<FName> Out;
	for (const TPair<FName, FVector2D>& Kv : E.AppliedOffsets)
	{
		bool bSubRoot = false;
		TArray<FName> SubWidgets;
		if (FindDragRecipe(WindowId, Kv.Key, bSubRoot, SubWidgets) && !bSubRoot) { Out.Append(SubWidgets); }
	}
	return Out;
}

// Разбор — у объявления (М-1). Работает в ЛОКАЛЬНЫХ юнитах корня окна: экран = (0,0)..(вьюпорт / DPI).
// ★★★ [ПОРТ] КЛЮЧ ОТКАТА «ВОЗВРАТ В ЭКРАН ПРИ ОТПУСКАНИИ ПЕРЕТАСКИВАНИЯ» (пакет 4, 08.10.2026, требование владельца).
//   1 — окно, отпущенное частично за краем, прижимается к краю, в ini пишется позиция после возврата; 0 — как прежде
//   (окно остаётся, где отпустили, кламп — только при следующем открытии или смене разрешения).
static ASDA_DEV_CVAR(int32, CVarDragReleaseClamp,
	TEXT("asda.UI.DragReleaseClamp"), 1,
	TEXT("1 — окно, отпущенное перетаскиванием частично за краем экрана, возвращается в экран (прижатие к краю), в ini — ")
	TEXT("позиция после возврата (08.10.2026); 0 — прежнее поведение: остаётся, где отпустили."),
	ECVF_Default);

void UAsdaWindowSubsystem::ClampWindowBySlotsNow(FName WindowId, FName DragReleaseKey)
{
	const bool bRelease = !DragReleaseKey.IsNone();                  // режим «отпустили перетаскивание» (пакет 4)
	FAsdaWindowEntry* E = Windows.Find(WindowId);
	if (!E || !E->Widget || E->AppliedOffsets.Num() == 0) { return; }
	if (!bRelease && E->ClampPendingKeys.Num() == 0) { return; }
	if (!bRelease && (E->bModal || E->ZOrder >= kModalZ)) { return; } // модалка позицию не восстанавливает
	if (!E->Widget->IsInViewport()) { return; }                         // не полноэкранный слот вьюпорта — только живой кламп
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	UGameViewportClient* GVC = World ? World->GetGameViewport() : nullptr;
	if (!GVC) { return; }
	// Слот окна во вьюпорте обязан быть полноэкранным (так его ставит `AddToViewport`); иначе корень ≠ экран.
	if (const UGameViewportSubsystem* VS = UGameViewportSubsystem::Get())
	{
		const FGameViewportWidgetSlot VSlot = VS->GetWidgetSlot(E->Widget);
		const bool bFull = VSlot.Anchors.Minimum.IsNearlyZero() && VSlot.Anchors.Maximum.Equals(FVector2D(1.0, 1.0))
			&& FMath::IsNearlyZero(VSlot.Offsets.Left) && FMath::IsNearlyZero(VSlot.Offsets.Top)
			&& FMath::IsNearlyZero(VSlot.Offsets.Right) && FMath::IsNearlyZero(VSlot.Offsets.Bottom);
		if (!bFull) { return; }
	}
	UWidget* RootW = E->Widget->GetRootWidget();
	UPanelWidget* RootCv = Cast<UPanelWidget>(RootW);
	if (!RootCv) { return; }
	FVector2D ViewPx;                                                    // размер вьюпорта в пикселях
	GVC->GetViewportSize(ViewPx);
	if (ViewPx.X <= 0.0 || ViewPx.Y <= 0.0) { return; }
	float Scale = UWidgetLayoutLibrary::GetViewportScale(GVC);           // без покадрового кэша: размер мог смениться в этом кадре
	if (Scale <= KINDA_SMALL_NUMBER) { Scale = 1.f; }
	const FVector2D ViewSize = ViewPx / Scale;                           // экран в юнитах канваса

	LoadAllLayouts();
	const FAsdaWindowSavedLayout* L = SavedLayouts.Find(WindowId);
	TArray<FAsdaClampRecipe> Recipes;
	TArray<FName> Keys;                                                  // разбираемые ключи: заявка либо отпущенный ключ
	if (bRelease) { Keys.Add(DragReleaseKey); } else { Keys = E->ClampPendingKeys.Array(); }
	// ★ 08.10.2026 (пакет 5, МЕЛК-1 ревизии пакета 4): отпущен КОРНЕВОЙ ключ — сдвиг корня увёз и под-окна со своей позицией
	//   (чат личного магазина `StreetStall_ChatBox`), а габариты корня их не видят (С-1). Как у прежнего клампа: сначала корень,
	//   потом под-окна — каждое своим ключом. Ключи под-окон: применённые сдвиги этого окна и цели ручек этого окна (под-окно,
	//   которое игрок ещё не двигал, тоже уехало с корнем; ключ ему заводится этой поправкой).
	bool bReleaseRoot = false;                                           // отпущенный ключ двигает весь корпус окна
	if (bRelease)
	{
		TArray<FName> RootWidgetsUnused;                                 // виджеты рецепта (для корня не нужны)
		FindDragRecipe(WindowId, DragReleaseKey, bReleaseRoot, RootWidgetsUnused);
	}
	if (bReleaseRoot)
	{
		TArray<FName> SubKeys;                                           // кандидаты в под-окна со своей позицией
		E->AppliedOffsets.GetKeys(SubKeys);
		for (const FAsdaJuiDragHandle& H : E->DragHandles)
		{
			if (!H.TargetWindowId.IsNone() && H.TargetWindowId != WindowId) { continue; }   // ручка двигает чужое окно
			if (!H.TargetControl.IsNone()) { SubKeys.AddUnique(H.TargetControl); }
		}
		for (const FName& K : SubKeys) { Keys.AddUnique(K); }
	}
	for (const FName& Key : Keys)
	{
		if (L && L->ExternalKeys.Contains(Key)) { continue; }             // ключ владельца окна: его не клампим
		FAsdaClampRecipe R;
		R.Key = Key;
		// Под-окно без применённого сдвига (пакет 5, МЕЛК-1) — в режиме «отпускание» допускается: сдвиг считается от нуля.
		const bool bKeyKnown = E->AppliedOffsets.Contains(Key) || (bReleaseRoot && Key != DragReleaseKey);
		if (!bKeyKnown || !FindDragRecipe(WindowId, Key, R.bRoot, R.Widgets)) { continue; }
		if (bRelease && Key != DragReleaseKey && R.bRoot) { continue; }  // второй корневой ключ — не под-окно
		if (!R.bRoot && R.Widgets.IsEmpty()) { continue; }               // М-3: рецепт без виджетов — двигать нечего
		Recipes.Add(MoveTemp(R));
	}
	// Сначала корень: его сдвиг двигает и под-окна (все дети корневого канваса), а слоты читаются заново — сразу свежие.
	Recipes.StableSort([](const FAsdaClampRecipe& A, const FAsdaClampRecipe& B) { return A.bRoot && !B.bRoot; });
	TSet<FName> OwnKeyWidgets = CollectOwnKeyWidgets(WindowId, *E);
	// Пакет 5 (МЕЛК-1): под-окна, разбираемые своим ключом, — не часть корпуса и тогда, когда сдвига у них ещё нет.
	for (const FAsdaClampRecipe& R : Recipes) { if (!R.bRoot) { OwnKeyWidgets.Append(R.Widgets); } }

	bool bShifted = false;                                               // хоть один ключ сдвинут
	for (const FAsdaClampRecipe& R : Recipes)
	{
		TArray<UWidget*> Parts;                                          // что двигает этот ключ
		if (R.bRoot)
		{
			for (int32 i = 0; i < RootCv->GetChildrenCount(); ++i)
			{
				UWidget* Ch = RootCv->GetChildAt(i);
				if (Ch && !OwnKeyWidgets.Contains(Ch->GetFName())) { Parts.Add(Ch); }
			}
		}
		else
		{
			for (const FName& N : R.Widgets) { Parts.Add(E->Widget->GetWidgetFromName(N)); }
		}
		const float Big = TNumericLimits<float>::Max();
		FVector2D Mn(Big, Big), Mx(-Big, -Big);                          // габариты в юнитах корня
		bool bHave = false;
		for (UWidget* P : Parts)
		{
			FVector2D Pos, Size;
			if (!SlotRectInRootLocal(P, RootW, ViewSize, Pos, Size)) { continue; }   // скрыт / не канвас — клампится позже
			Mn = FVector2D(FMath::Min(Mn.X, Pos.X), FMath::Min(Mn.Y, Pos.Y));
			Mx = FVector2D(FMath::Max(Mx.X, Pos.X + Size.X), FMath::Max(Mx.Y, Pos.Y + Size.Y));
			bHave = true;
		}
		if (!bHave) { continue; }
		// СРЕДН-3: корень — по ВИДИМОЙ РАМКЕ (планы корневого контрола), если она есть.
		FVector2D FMn, FMx;
		if (R.bRoot && AsdaRootFrameRect(E->Widget, R.Key, ViewSize, /*bSlots*/true, FMn, FMx)) { Mn = FMn; Mx = FMx; }
		// ★ 04.11.2026 (`asda.UI.OffscreenKeepGrab`): ручка ключа досягаема — окно частично за краем НЕ трогаем (как оригинал).
		FVector2D GMn, GMx;                                              // ручка ключа (юниты корня)
		const bool bKeepGrab = AsdaOffscreenKeepGrabOn()
			&& AsdaFindGrabRect(Windows, WindowId, R.Key, /*bSlots*/true, FVector2D::ZeroVector, ViewSize, Mn, Mx, GMn, GMx);
		if (bKeepGrab && AsdaGrabReachable(GMn, GMx, FVector2D::ZeroVector, ViewSize, Scale)) { continue; }
		if (bRelease)
		{
			// Отпустили за краем: прижать к краю (игрок сам выбрал место — дефолт/центр тут не к месту) и запомнить
			//   ИМЕННО эту позицию: на следующем запуске окно встанет туда же, правило «за экраном → дефолт» не сработает.
			// ★ 04.11.2026: при `asda.UI.OffscreenKeepGrab 1` — не прижатие всей рамки, а минимальный подтяг ручки до досягаемости;
			//   сдвиг ключа — до целого пикселя в сторону экрана (как `AsdaRoundReleaseBackInside`).
			FVector2D Back;
			if (bKeepGrab)
			{
				Back = AsdaGrabAdjust(GMn, GMx, FVector2D::ZeroVector, ViewSize, Scale);
				const FVector2D OldOff = E->AppliedOffsets.FindRef(R.Key);
				for (int32 Ax = 0; Ax < 2; ++Ax)
				{
					if (FMath::Abs(Back[Ax]) < 0.01) { Back[Ax] = 0.0; continue; }
					const double NewOff = OldOff[Ax] + Back[Ax];
					Back[Ax] = (Back[Ax] > 0.0 ? FMath::CeilToDouble(NewOff - 1e-3) : FMath::FloorToDouble(NewOff + 1e-3)) - OldOff[Ax];
				}
				if (Back.IsNearlyZero(0.01)) { continue; }
			}
			else
			{
				Back = ClampAdjustIntoView(Mn, Mx, FVector2D::ZeroVector, ViewSize);
				if (!AsdaRoundReleaseBackInside(Mn, Mx, ViewSize, E->AppliedOffsets.FindRef(R.Key), Back)) { continue; }
			}
			ShiftTargetByKey(WindowId, R.Key, Back);
			const FVector2D NewOff = E->AppliedOffsets.FindRef(R.Key) + Back;   // сдвиг ключа после возврата
			E->AppliedOffsets.Add(R.Key, NewOff);
			SavedLayouts.FindOrAdd(WindowId).Offsets.Add(R.Key, NewOff);
			bLayoutDirty = true;
			bShifted = true;
			// Пакет 5 (МЕЛК-2): `DragOffset` окна-владельца ручки — с учётом возврата (диагностика `asda.UI.DumpDrag`).
			if (R.Key == DragReleaseKey)
			{
				if (FAsdaWindowEntry* Owner = Windows.Find(DragWindowId.IsNone() ? WindowId : DragWindowId)) { Owner->DragOffset += Back; }
			}
			UE_LOG(LogAsdaWindow, Log,
				TEXT("[перетаскивание] «%s».«%s»: отпущено за краем (rect %.3f,%.3f..%.3f,%.3f при экране 0,0..%.0f,%.0f) — ")
				TEXT("возвращено в экран на (%.3f,%.3f), в ini — позиция после возврата (%.3f,%.3f) [ПОРТ, asda.UI.DragReleaseClamp]"),
				*WindowId.ToString(), *R.Key.ToString(), Mn.X * Scale, Mn.Y * Scale, Mx.X * Scale, Mx.Y * Scale,
				ViewPx.X, ViewPx.Y, Back.X, Back.Y, NewOff.X, NewOff.Y);
			continue;
		}
		EAsdaOffscreenFix Fix = EAsdaOffscreenFix::None;
		const FVector2D Local = OffscreenAdjust(WindowId, R.Key, R.bRoot, Mn, Mx, FVector2D::ZeroVector, ViewSize,
			E->AppliedOffsets.FindRef(R.Key), E->bRootOffscreenFixed, Fix);
		if (Local.IsNearlyZero(0.01)) { continue; }
		if (R.bRoot && Fix != EAsdaOffscreenFix::Edge) { E->bRootOffscreenFixed = true; }   // под-окна — относительно корня
		UE_LOG(LogAsdaWindow, Log, TEXT("[раскладка окон] «%s».«%s»: за экраном — %s (правило владельца 08.10, asda.UI.OffscreenToCenter=%d, по слотам)"),
			*WindowId.ToString(), *R.Key.ToString(), AsdaOffscreenFixName(Fix), CVarOffscreenToCenter.GetValueOnGameThread());
		ShiftTargetByKey(WindowId, R.Key, Local);
		E->AppliedOffsets.Add(R.Key, E->AppliedOffsets.FindRef(R.Key) + Local);
		bShifted = true;
		// Формат строки — как у живого клампа (разбор `an_clamp.py`); прямоугольник в пикселях экрана от его угла.
		UE_LOG(LogAsdaWindow, Log,
			TEXT("[раскладка окон] «%s».«%s»: окно уходило за край (rect %.0f,%.0f..%.0f,%.0f при экране ")
			TEXT("0,0..%.0f,%.0f, по слотам до первой отрисовки) — возвращено на (%.1f,%.1f). В ini сохранена ИСХОДНАЯ позиция."),
			*WindowId.ToString(), *R.Key.ToString(), Mn.X * Scale, Mn.Y * Scale, Mx.X * Scale, Mx.Y * Scale,
			ViewPx.X, ViewPx.Y, Local.X, Local.Y);
	}
	// Слоты сдвинуты — геометрия прошлой раскладки устарела: живая проверка не раньше следующего кадра.
	//   Только при сдвиге: окно, стоящее в заявке без движения, не должно откладывать живой кламп остальных.
	if (bShifted) { LiveClampNotBeforeFrame = FMath::Max(LiveClampNotBeforeFrame, GFrameCounter + 1); }
}

// Разбор — у объявления (пакет 4). Цель — окно/ключ, которые копил `AccumulateOffset` (у ручки бывает чужая цель).
void UAsdaWindowSubsystem::ClampDragOnRelease()
{
	const bool bMoved = bDragMoved;
	bDragMoved = false;
	if (!bMoved || CVarDragReleaseClamp.GetValueOnGameThread() == 0 || DragWindowId.IsNone()) { return; }
	const FAsdaWindowEntry* E = Windows.Find(DragWindowId);
	if (!E || !E->DragHandles.IsValidIndex(DragHandleIndex)) { return; }
	const FAsdaJuiDragHandle& H = E->DragHandles[DragHandleIndex];
	const FName Target = H.TargetWindowId.IsNone() ? DragWindowId : H.TargetWindowId;   // окно-цель сдвига
	const FName Key = H.TargetControl;                                                   // ключ сдвига
	if (Key.IsNone()) { return; }
	ClampWindowBySlotsNow(Target, Key);
}

// ★★★ КЛАМП В ЭКРАН ПО ЖИВОЙ ГЕОМЕТРИИ. Сдвиг сохраняется в пикселях канваса, поэтому после смены
//   разрешения (или переезда на другой монитор) он может увести окно за край. Считается по ЖИВОЙ
//   геометрии тех виджетов, которые этот сдвиг и двигает, — поэтому запускается не в кадре
//   создания окна, а когда Slate уже посчитал геометрию. С 07.10.2026 (М-1) перед ним в кадре создания
//   уже отработал кламп по слотам (`ClampWindowBySlotsNow`), так что здесь обычно только проверка.
//   ★ Поправка клампа НЕ пишется в ini: в файле остаётся то, что выбрал пользователь, и на большом
//   экране окно вернётся ровно туда, куда он его ставил. Меняется только `AppliedOffsets`.
//   true — заявку снимаем: все ключи разобраны ИЛИ оставшиеся ждут показа скрытого куска по подписке (М-2).
bool UAsdaWindowSubsystem::ClampWindowIntoViewport(FName WindowId)
{
	FAsdaWindowEntry* E = Windows.Find(WindowId);
	if (!E || !E->Widget) { return true; }                                  // окно закрылось — снимаем
	if (E->AppliedOffsets.Num() == 0) { E->ClampPendingKeys.Reset(); return true; }   // нечего клампить
	if (E->ClampPendingKeys.Num() == 0) { return true; }                    // все ключи уже разобраны

	// ★ М-2 (07.10.2026): скрытые куски — не покадровый перебор, а подписка на их ПОКАЗ (FieldNotify `Visibility`).
	//   Окно уходит из `PendingClamp`; событие показа (`OnClampWatchVisibility`) вернёт его в заявку.
	auto ParkUntilShown = [this, WindowId, E](const TArray<UWidget*>& Hidden) -> bool
	{
		ClearClampWatch(*E);
		for (UWidget* H : Hidden)
		{
			if (!H) { continue; }
			const FDelegateHandle Hd = H->AddFieldValueChangedDelegate(UWidget::FFieldNotificationClassDescriptor::Visibility,
				INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateUObject(this, &UAsdaWindowSubsystem::OnClampWatchVisibility, WindowId));
			E->ClampWatch.Emplace(H, Hd);
		}
		if (E->ClampWatch.Num() == 0) { return false; }                      // подписаться не на что — остаёмся в опросе
		UE_LOG(LogAsdaWindow, Verbose, TEXT("[раскладка окон] «%s»: ключей %d ждут показа %d скрытых кусков — по событию, без опроса"),
			*WindowId.ToString(), E->ClampPendingKeys.Num(), E->ClampWatch.Num());
		return true;
	};

	const ESlateVisibility WV = E->Widget->GetVisibility();
	if (WV == ESlateVisibility::Collapsed || WV == ESlateVisibility::Hidden)
	{
		TArray<UWidget*> Self;                                              // само окно — единственный скрытый кусок
		Self.Add(E->Widget);
		return ParkUntilShown(Self);                                        // всё окно скрыто — ждём его показа
	}

	// Прямоугольник вьюпорта в АБСОЛЮТНЫХ координатах = геометрия САМОГО окна-виджета: он добавлен
	//   в вьюпорт полноэкранным слотом, поэтому его rect и есть экран. Берём именно его, а не
	//   корневой канвас: у самодельных оверлеев корень может быть не полноэкранным. Абсолютные
	//   координаты учитывают оконный режим (игра не обязана стоять в углу рабочего стола).
	UWidget* RootW = E->Widget->GetRootWidget();
	if (!RootW) { return true; }
	FVector2D ViewMin, ViewMax;
	if (!WidgetAbsRect(E->Widget, ViewMin, ViewMax)) { return false; }      // геометрии ещё нет

	float Scale = 1.f;
	if (const UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		Scale = UWidgetLayoutLibrary::GetViewportScale(W);
	}
	if (Scale <= KINDA_SMALL_NUMBER) { Scale = 1.f; }

	TArray<FAsdaClampRecipe> Recipes;
	for (const FName& Key : E->ClampPendingKeys.Array())
	{
		FAsdaClampRecipe R;
		R.Key = Key;
		// Ключа больше нет среди применённых (сброс раскладки) или рецепта нет в мете — клампить нечего.
		if (!E->AppliedOffsets.Contains(Key) || !FindDragRecipe(WindowId, Key, R.bRoot, R.Widgets))
		{
			E->ClampPendingKeys.Remove(Key);
			continue;
		}
		// ★ М-3 ревизии `widen_fixes2_REVIEW.md` (07.10.2026): рецепт без виджетов (мета `Gear_book_WIN →
		//   INVEN_WIN.Inven_Strength_BOX3`) двигать нечего — ключ снимаем, иначе заявка висела бы до закрытия окна.
		if (!R.bRoot && R.Widgets.IsEmpty())
		{
			UE_LOG(LogAsdaWindow, Log, TEXT("[раскладка окон] «%s».«%s»: в рецепте меты нет виджетов — клампить нечего, ключ снят"),
				*WindowId.ToString(), *Key.ToString());
			E->ClampPendingKeys.Remove(Key);
			continue;
		}
		Recipes.Add(MoveTemp(R));
	}
	// ★ М-2/С-1 (07.10.2026): СНАЧАЛА корень, ПОТОМ под-окна. Сдвиг корня двигает и под-окна (все дети
	//   корневого канваса), а их живая геометрия до следующей раскладки Slate остаётся старой — поэтому
	//   после сдвига корня под-окна в этом кадре не меряем, а ждём кадр (см. `bRootMoved` ниже).
	Recipes.StableSort([](const FAsdaClampRecipe& A, const FAsdaClampRecipe& B) { return A.bRoot && !B.bRoot; });

	// Под-окна со своим ключом — не часть корпуса (С-1 прошлой ревизии, разбор у `CollectOwnKeyWidgets`).
	TSet<FName> OwnKeyWidgets;
	if (Recipes.Num() > 0 && Recipes[0].bRoot) { OwnKeyWidgets = CollectOwnKeyWidgets(WindowId, *E); }

	bool bRootMoved = false;     // корень сдвинут в этом вызове — геометрия под-окон устарела до следующего кадра
	bool bDeferred = false;      // часть ключей отложена на следующий кадр (после сдвига корня) — заявка остаётся в опросе
	TArray<UWidget*> HiddenParts;   // скрытые куски ключей, ждущих показа (М-2: подписка на их показ)
	for (const FAsdaClampRecipe& R : Recipes)
	{
		if (bRootMoved && !R.bRoot) { bDeferred = true; break; }   // под-окна — в следующем кадре, по свежей геометрии
		const FName& Key = R.Key;
		// Габариты ИМЕННО ТОГО, что двигает этот сдвиг (весь корпус окна либо виджеты под-окна).
		TArray<UWidget*> Parts;
		int32 NumOwnKeyExcluded = 0;   // детей корня, исключённых как под-окна со своим ключом (С-1)
		if (R.bRoot)
		{
			if (UPanelWidget* RootCv = Cast<UPanelWidget>(RootW))
			{
				for (int32 i = 0; i < RootCv->GetChildrenCount(); ++i)
				{
					UWidget* Ch = RootCv->GetChildAt(i);
					if (Ch && OwnKeyWidgets.Contains(Ch->GetFName())) { ++NumOwnKeyExcluded; continue; }
					Parts.Add(Ch);
				}
			}
		}
		else
		{
			for (const FName& N : R.Widgets) { Parts.Add(E->Widget->GetWidgetFromName(N)); }
		}
		const float Big = TNumericLimits<float>::Max();
		FVector2D Mn(Big, Big), Mx(-Big, -Big);
		bool bHave = false;
		int32 NumBySlot = 0;   // кусков, габариты которых взяты по слоту (живой геометрии у них нет)
		// Куски без живой геометрии, чей слотовый прямоугольник ВНУТРИ экрана: в габариты идут, только
		//   если живой геометрии нет ни у одного куска (М-1, см. запасной путь ниже).
		TArray<TPair<FVector2D, FVector2D>> SlotRectsInside;
		// Куски без живой геометрии, у которых и слот от родителя не посчитался (последний запасной путь).
		TArray<UWidget*> NoParentRectParts;
		TArray<UWidget*> KeyHidden;   // скрытые куски этого ключа
		for (UWidget* P : Parts)
		{
			if (!P) { continue; }
			const ESlateVisibility PV = P->GetVisibility();
			if (PV == ESlateVisibility::Collapsed || PV == ESlateVisibility::Hidden) { KeyHidden.Add(P); continue; }
			FVector2D A, B;
			if (!WidgetAbsRect(P, A, B))
			{
				// ═══ ★★★ С-2 (07.10.2026): КУСОК БЕЗ ЖИВОЙ ГЕОМЕТРИИ — ПО СЛОТУ ОТ РОДИТЕЛЯ ═══════════
				//   БЫЛО: такой кусок просто выпадал из bbox, а запасной путь «слоты канвы» ниже включался,
				//   только если живой геометрии нет НИ У ОДНОГО куска. Окно, ушедшее за край ЧАСТИЧНО,
				//   клампилось по оставшимся кускам: у 8200 и 8500 на 1024×768 правый/нижний кусок рамки
				//   (5 и 4 px) так и оставался за экраном (ревизия `widen_windows_REVIEW.md`, С-2:
				//   «rect 690,350..1430,893» = 740×543 вместо 745×548).
				//   СТАЛО: у каждого куска своя мера — живая геометрия, а без неё слот от живого родителя.
				//   ⚠ Слотовый прямоугольник берём, только если он хоть частью ЗА экраном: только так
				//     кусок и теряет геометрию. Кусок, не нарисованный по иной причине, внутри экрана
				//     габариты окна не раздувает (прежнее поведение).
				if (!SlotAbsRectViaParent(P, A, B)) { NoParentRectParts.Add(P); continue; }
				const bool bOutside = A.X < ViewMin.X || A.Y < ViewMin.Y || B.X > ViewMax.X || B.Y > ViewMax.Y;
				if (!bOutside) { SlotRectsInside.Emplace(A, B); continue; }
				++NumBySlot;
			}
			Mn = FVector2D(FMath::Min(Mn.X, A.X), FMath::Min(Mn.Y, A.Y));
			Mx = FVector2D(FMath::Max(Mx.X, B.X), FMath::Max(Mx.Y, B.Y));
			bHave = true;
		}
		if (NumOwnKeyExcluded > 0)
		{
			UE_LOG(LogAsdaWindow, Log,
				TEXT("[раскладка окон] «%s».«%s»: под-окон со своим ключом вне габаритов корня: %d (клампятся сами)"),
				*WindowId.ToString(), *Key.ToString(), NumOwnKeyExcluded);
		}
		if (NumBySlot > 0)
		{
			UE_LOG(LogAsdaWindow, Log,
				TEXT("[раскладка окон] «%s».«%s»: у %d кусков нет живой геометрии (за кромкой) — их габариты ")
				TEXT("взяты по слоту от родителя; итог %.0f,%.0f..%.0f,%.0f"),
				*WindowId.ToString(), *Key.ToString(), NumBySlot, Mn.X, Mn.Y, Mx.X, Mx.Y);
		}

		// ═══ ★★★ ЗАПАСНОЙ ИСТОЧНИК ГАБАРИТОВ — СЛОТЫ КАНВЫ (КРИТ 25.09.2026) ═══════════════════
		//   БОЛЕЗНЬ, НАЙДЕННАЯ ЖИВЫМ ПРОГОНОМ. Кламп существует ровно для случая «окно уехало за
		//   кромку», но считал габариты ТОЛЬКО по ЖИВОЙ геометрии Slate — а у виджета, целиком
		//   оказавшегося за экраном, живой геометрии НЕТ ВОВСЕ (его не рисуют). Значит в
		//   единственном случае, ради которого кламп и написан, он молча не срабатывал: заявка
		//   вечно висела в `PendingClamp` («ждут клампа в экран (нет живой геометрии): 1»), а
		//   окно оставалось недосягаемым — ни крестика, ни ручки перетаскивания.
		//   ВОСПРОИЗВЕДЕНО: сдвиг (1400,900), сохранённый на 2560×1440, открыт на 1280×720 —
		//   `asda.UI.DumpWindowPos` показывал «применено сейчас (1400,900)» БЕЗ пометки о клампе,
		//   а зона окна — «прямоугольника нет». Лог прогона
		//   `Reports/pet/petinfo/game_offscreen_n_30_wide2narrow.log`.
		//   ⚠ Это болезнь ОБЩЕГО механизма, а не одного окна: так же уехало бы любое из окон с
		//   памятью позиции (43000, 43500, 44200, 43300 и все прочие 125 перетаскиваемых).
		//
		//   ЛЕЧЕНИЕ. Нет живой геометрии — берём СЛОТЫ канвы: они известны всегда, независимо от того,
		//   рисуется виджет или нет.
		//   ★ М-1 ревизии `widen_fixes_REVIEW.md` (07.10.2026): прежняя формула `ViewMin + AsdaUi::SlotRect × Scale`
		//     не знала АНКОРОВ и РОДИТЕЛЯ. Кнопка таймера войны `CAMPWAR_Button` (анкор справа) получала
		//     −100…0, «возвращалась» на +100 и окончательно уходила за экран. Теперь сначала слот от живого
		//     родителя (`SlotAbsRectViaParent`, та же формула, что у `SConstraintCanvas`) — и БЕЗ условия
		//     «за кромкой»: живой геометрии нет ни у одного куска, мерить больше нечем. Прежняя формула
		//     осталась только для кусков, у которых и родитель без геометрии.
		//   ★ Живая геометрия ОСТАЁТСЯ ГЛАВНОЙ: она учитывает оконный режим и фактические
		//     преобразования. Слоты — только запасной путь, когда живой нет.
		if (!bHave)
		{
			for (const TPair<FVector2D, FVector2D>& Rc : SlotRectsInside)
			{
				Mn = FVector2D(FMath::Min(Mn.X, Rc.Key.X), FMath::Min(Mn.Y, Rc.Key.Y));
				Mx = FVector2D(FMath::Max(Mx.X, Rc.Value.X), FMath::Max(Mx.Y, Rc.Value.Y));
				bHave = true;
			}
			for (UWidget* P : NoParentRectParts)
			{
				FVector2D SlotPos, SlotSize;
				if (!AsdaUi::SlotRect(P, SlotPos, SlotSize)) { continue; }
				if (SlotSize.X <= 0.0 || SlotSize.Y <= 0.0) { continue; }
				const FVector2D A = ViewMin + SlotPos * Scale;
				const FVector2D B = A + SlotSize * Scale;
				Mn = FVector2D(FMath::Min(Mn.X, A.X), FMath::Min(Mn.Y, A.Y));
				Mx = FVector2D(FMath::Max(Mx.X, B.X), FMath::Max(Mx.Y, B.Y));
				bHave = true;
			}
			if (bHave)
			{
				UE_LOG(LogAsdaWindow, Log,
					TEXT("[раскладка окон] «%s».«%s»: живой геометрии нет (окно целиком за кромкой) — ")
					TEXT("габариты взяты у слотов канвы (от родителя %d, без родителя %d): %.0f,%.0f..%.0f,%.0f"),
					*WindowId.ToString(), *Key.ToString(), SlotRectsInside.Num(), NoParentRectParts.Num(),
					Mn.X, Mn.Y, Mx.X, Mx.Y);
			}
		}

		if (!bHave)
		{
			// ★ М-2: габаритов нет, потому что куски скрыты, — ключ ждёт ПОКАЗА под-окна (подписка ниже).
			if (KeyHidden.Num() > 0) { HiddenParts.Append(KeyHidden); continue; }
			// ★ М-3: не скрыто и не меряется ничем (кусков нет в дереве) — ключ снимаем, а не держим вечно.
			UE_LOG(LogAsdaWindow, Log, TEXT("[раскладка окон] «%s».«%s»: виджетов рецепта нет в дереве окна — клампить нечего, ключ снят"),
				*WindowId.ToString(), *Key.ToString());
			E->ClampPendingKeys.Remove(Key);
			continue;
		}
		E->ClampPendingKeys.Remove(Key);

		// СРЕДН-3: корень — по ВИДИМОЙ РАМКЕ (планы корневого контрола; слот от живого родителя — мера и за кромкой).
		FVector2D FMn, FMx;
		if (R.bRoot && AsdaRootFrameRect(E->Widget, Key, FVector2D::ZeroVector, /*bSlots*/false, FMn, FMx)) { Mn = FMn; Mx = FMx; }
		// ★ 04.11.2026 (`asda.UI.OffscreenKeepGrab`): ручка досягаема — окно частично за краем остаётся (живая геометрия, пиксели).
		{
			FVector2D GMn, GMx;
			if (AsdaOffscreenKeepGrabOn() && AsdaFindGrabRect(Windows, WindowId, Key, /*bSlots*/false, ViewMin, ViewMax, Mn, Mx, GMn, GMx)
				&& AsdaGrabReachable(GMn, GMx, ViewMin, ViewMax, 1.0))
			{
				continue;
			}
		}
		EAsdaOffscreenFix Fix = EAsdaOffscreenFix::None;
		const FVector2D Adj = OffscreenAdjust(WindowId, Key, R.bRoot, Mn, Mx, ViewMin, ViewMax,
			E->AppliedOffsets.FindRef(Key) * Scale, E->bRootOffscreenFixed, Fix);
		// Допуск 0.01 px: после клампа по слотам (М-1) живая мера совпадает с ним до долей пикселя — их не двигаем.
		if (Adj.IsNearlyZero(0.01)) { continue; }
		if (R.bRoot && Fix != EAsdaOffscreenFix::Edge) { E->bRootOffscreenFixed = true; }   // под-окна — относительно корня
		UE_LOG(LogAsdaWindow, Log, TEXT("[раскладка окон] «%s».«%s»: за экраном — %s (правило владельца 08.10, asda.UI.OffscreenToCenter=%d, живая геометрия)"),
			*WindowId.ToString(), *Key.ToString(), AsdaOffscreenFixName(Fix), CVarOffscreenToCenter.GetValueOnGameThread());

		const FVector2D Local = Adj / Scale;
		ShiftTargetByKey(WindowId, Key, Local);
		E->AppliedOffsets.Add(Key, E->AppliedOffsets.FindRef(Key) + Local);
		if (R.bRoot) { bRootMoved = true; }
		UE_LOG(LogAsdaWindow, Log,
			TEXT("[раскладка окон] «%s».«%s»: окно уходило за край (rect %.0f,%.0f..%.0f,%.0f при экране ")
			TEXT("%.0f,%.0f..%.0f,%.0f) — возвращено на (%.1f,%.1f). В ini сохранена ИСХОДНАЯ позиция."),
			*WindowId.ToString(), *Key.ToString(), Mn.X, Mn.Y, Mx.X, Mx.Y,
			ViewMin.X, ViewMin.Y, ViewMax.X, ViewMax.Y, Local.X, Local.Y);
	}
	if (E->ClampPendingKeys.Num() == 0) { ClearClampWatch(*E); return true; }   // все ключи разобраны
	if (bDeferred) { return false; }                                            // под-окна — следующим кадром
	// Остались только ключи скрытых кусков (М-2): из опроса — в подписку на их показ.
	return ParkUntilShown(HiddenParts);
}

// ★ М-2 (07.10.2026): событие FieldNotify `Visibility` скрытого куска. Приходит ИЗНУТРИ чужого `SetVisibility` —
//   поэтому здесь только отметка; заявку и снятие подписок разбирает тик (`TickLayoutRestore`).
void UAsdaWindowSubsystem::OnClampWatchVisibility(UObject* Obj, UE::FieldNotification::FFieldId /*Field*/, FName WindowId)
{
	const UWidget* W = Cast<UWidget>(Obj);
	if (!W) { return; }
	const ESlateVisibility V = W->GetVisibility();
	if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { return; }   // спрятали — ждём дальше
	ClampWokenWindows.Add(WindowId);
}

void UAsdaWindowSubsystem::ClearClampWatch(FAsdaWindowEntry& E)
{
	for (const TPair<TWeakObjectPtr<UWidget>, FDelegateHandle>& Wt : E.ClampWatch)
	{
		if (UWidget* W = Wt.Key.Get())
		{
			W->RemoveFieldValueChangedDelegate(UWidget::FFieldNotificationClassDescriptor::Visibility, Wt.Value);
		}
	}
	E.ClampWatch.Reset();
}

// ★ С-1 (07.10.2026): колбэк движка приходит изнутри `FSceneViewport::UpdateViewportRHI` — окна здесь не трогаем,
//   только флаг и кадр; разбор — `ProcessViewportResize` в тике (закон `no-reentrant-net-callbacks`).
void UAsdaWindowSubsystem::OnViewportResized(FViewport* Viewport, uint32 /*Unused*/)
{
	const UGameInstance* GI = GetGameInstance();
	const UGameViewportClient* GVC = GI ? GI->GetGameViewportClient() : nullptr;
	if (!Viewport || !GVC || Viewport != GVC->Viewport) { return; }   // чужой вьюпорт (окна редактора и т. п.)
	bViewportResizePending = true;
	ViewportResizeFrame = GFrameCounter;
}

// Разбор — у объявления (С-1).
void UAsdaWindowSubsystem::ProcessViewportResize()
{
	// Два кадра тишины после события: Slate успевает разложить окно игры под новый размер, а серия событий
	//   (игрок тянет край окна) разбирается один раз — по последнему.
	if (GFrameCounter < ViewportResizeFrame + 2) { return; }
	bViewportResizePending = false;
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	UGameViewportClient* GVC = World ? World->GetGameViewport() : nullptr;
	if (!GVC) { return; }
	FVector2D Px;                                                        // новый размер вьюпорта, пиксели
	GVC->GetViewportSize(Px);
	const FIntPoint NowPx(FMath::RoundToInt32(Px.X), FMath::RoundToInt32(Px.Y));
	if (NowPx.X <= 0 || NowPx.Y <= 0 || NowPx == LastViewportPx) { return; }   // размер не изменился
	const FIntPoint OldPx = LastViewportPx;
	LastViewportPx = NowPx;

	LoadAllLayouts();
	// ★ 08.10.2026 (пакет 2): целые пиксели «ц%» пересчитать под новый экран (формула оригинала зависит от размера экрана).
	{
		TArray<FName> All;
		Windows.GetKeys(All);
		for (const FName& Id : All) { SnapPercentAnchors(Id); }
	}
	// ★ 08.10.2026 (пакет 3, МЕЛК-1): окна, поставленные кодом оригинала (1530/1559 — левый-верх 1500, 1410 — у курсора/центр),
	//   стоят абсолютно и за экраном сами не следуют — повторить их постановку под новый экран. Постановщики копируются
	//   заранее: вызов может переписать `RePlacer` своей же записи (1530 заново заявляет себя).
	//   ★ 08.10.2026 (пакет 4, МЕЛК-2 ревизии пакета 3): только ВИДИМЫЕ окна. Скрытые 1530 (после `SowelWinClose`) и 1559
	//   (между покупками) переставлялись впустую и шумели в журнале; при показе их ставит само открытие (1530 — `PlaceModalByOriginal`,
	//   1559 — `UAsdaPurchaseBoxSubsystem::Open`, 1410 — `ShowNumPad`), так что пропуск ничего не теряет.
	//   ★ 08.10.2026 (пакет 5, СРЕДН-1 ревизии пакета 4): утверждение «при показе их ставит само открытие» неверно для 1530 —
	//   его повторные показы (`ShowSowelStonesWindow`) только меняют видимость. Поэтому скрытому окну с постановщиком ставится
	//   флаг `bRePlaceOnShow`: постановщик выполнится на фронте ближайшего показа (`OnShowWatchVisibility`) — одной точкой для
	//   всех окон с постановщиком, текущих и будущих. Окно, которое ставит своё открытие (1559, 1410), снимает флаг само
	//   (`SetWindowRePlacer`).
	int32 NumReplaced = 0;   // окон, переставленных постановкой оригинала
	int32 NumDeferred = 0;   // скрытых окон, чья постановка отложена до показа
	{
		TArray<TFunction<void()>> Placers;   // копии постановщиков открытых и видимых окон
		for (TPair<FName, FAsdaWindowEntry>& Kv : Windows)
		{
			if (!Kv.Value.Widget || !Kv.Value.RePlacer) { continue; }
			if (AsdaWindowIsEntryVisible(Kv.Value)) { Placers.Add(Kv.Value.RePlacer); }
			else { Kv.Value.bRePlaceOnShow = true; ++NumDeferred; }
		}
		for (const TFunction<void()>& P : Placers) { P(); ++NumReplaced; }
	}
	TArray<FName> Ids;                                                  // окна, которым нужен кламп заново
	int32 NumRestored = 0;                                               // ключей, возвращённых к сохранённой позиции
	for (TPair<FName, FAsdaWindowEntry>& Kv : Windows)
	{
		FAsdaWindowEntry& E = Kv.Value;
		if (!E.Widget || E.AppliedOffsets.Num() == 0) { continue; }
		if (E.bModal || E.ZOrder >= kModalZ) { continue; }               // модалка позицию не восстанавливает
		Ids.Add(Kv.Key);
	}
	for (const FName& Id : Ids)
	{
		FAsdaWindowEntry* E = Windows.Find(Id);
		if (!E) { continue; }
		const FAsdaWindowSavedLayout* L = SavedLayouts.Find(Id);
		TArray<FName> Keys;
		E->AppliedOffsets.GetKeys(Keys);
		for (const FName& Key : Keys)
		{
			if (L && L->ExternalKeys.Contains(Key)) { continue; }         // ключ владельца окна: его не трогаем
			// Применённое = сохранённое + поправка прошлого клампа. Снимаем поправку: кламп считается заново под новый
			//   экран, а на большом экране окно встаёт туда, где его оставил игрок (как `ApplySavedLayout`).
			const FVector2D Cur = E->AppliedOffsets.FindRef(Key);
			const FVector2D* Saved = L ? L->Offsets.Find(Key) : nullptr;
			if (Saved && !(*Saved - Cur).IsNearlyZero())
			{
				ShiftTargetByKey(Id, Key, *Saved - Cur);
				E->AppliedOffsets.Add(Key, *Saved);
				++NumRestored;
			}
			E->ClampPendingKeys.Add(Key);
		}
		ClearClampWatch(*E);                                             // заявка заново: прежние подписки не нужны
		ClampWindowBySlotsNow(Id);                                       // в экран — уже в этом кадре, до отрисовки
		PendingClamp.Add(Id);                                            // живая проверка — по свежей геометрии
	}
	// Геометрия прошлой раскладки устарела (новый размер + сдвиги): живая проверка — через кадр.
	LiveClampNotBeforeFrame = FMath::Max(LiveClampNotBeforeFrame, GFrameCounter + 2);
	UE_LOG(LogAsdaWindow, Log,
		TEXT("[раскладка окон] размер вьюпорта %dx%d → %dx%d: окон на кламп заново %d, ключей возвращено к позиции из ini %d, переставлено постановкой оригинала %d, отложено до показа %d"),
		OldPx.X, OldPx.Y, NowPx.X, NowPx.Y, Ids.Num(), NumRestored, NumReplaced, NumDeferred);
}

// Разбор — у объявления (М-2). Подписки снимаются здесь, а не в колбэке FieldNotify (он приходит изнутри чужого `SetVisibility`).
void UAsdaWindowSubsystem::ProcessClampWoken()
{
	if (ClampWokenWindows.Num() == 0) { return; }
	const TArray<FName> Woken = ClampWokenWindows.Array();
	ClampWokenWindows.Reset();
	for (const FName& Id : Woken)
	{
		FAsdaWindowEntry* E = Windows.Find(Id);
		if (!E) { continue; }
		ClearClampWatch(*E);
		if (E->ClampPendingKeys.Num() == 0) { continue; }
		ClampWindowBySlotsNow(Id);   // показанный кусок — в экран до отрисовки (М-1 для кадра показа)
		PendingClamp.Add(Id);        // живая проверка — следующим кадром
	}
}

void UAsdaWindowSubsystem::OnSlatePreTick(float /*DeltaTime*/)
{
	// ★★★ Открытие поверх всех (08.10.2026): показы этого кадра — наверх своей полосы ДО отрисовки (кадра «под окном» нет).
	if (PendingShowRaise.Num() > 0) { FlushShowRaise(TEXT("тик Slate")); }
	ProcessClampWoken();
	// Показ, пришедший без подписки (окно ещё стояло в опросе, а игровой код в этом же кадре показал следующее
	//   под-окно), тоже клампим по слотам ДО отрисовки. Пустая заявка стоит ноль; без сдвига кламп по слотам
	//   живую проверку не откладывает.
	if (PendingClamp.Num() == 0) { return; }
	const TArray<FName> Pending = PendingClamp.Array();
	for (const FName& Id : Pending) { ClampWindowBySlotsNow(Id); }
}

void UAsdaWindowSubsystem::TickLayoutRestore()
{
	if (bViewportResizePending) { ProcessViewportResize(); }
	ProcessClampWoken();   // показ после тика Slate прошлого кадра (обычно уже разобран в `OnSlatePreTick`)
	if (PendingClamp.Num() == 0) { return; }
	if (GFrameCounter < LiveClampNotBeforeFrame) { return; }            // геометрия после сдвига слотов ещё старая
	TArray<FName> Pending = PendingClamp.Array();
	for (const FName& Id : Pending)
	{
		if (ClampWindowIntoViewport(Id)) { PendingClamp.Remove(Id); }
	}
}

void UAsdaWindowSubsystem::ResetWindowLayout(FName WindowId)
{
	LoadAllLayouts();
	TArray<FName> Targets;
	if (WindowId.IsNone()) { SavedLayouts.GetKeys(Targets); }
	else { Targets.Add(WindowId); }

	int32 NumMoved = 0;
	for (const FName& Id : Targets)
	{
		// Открытое окно возвращаем на место .JUI прямо сейчас: снимаем ФАКТИЧЕСКИ применённый сдвиг
		//   (он мог отличаться от сохранённого на величину клампа).
		if (FAsdaWindowEntry* E = Windows.Find(Id))
		{
			TArray<FName> Keys;
			E->AppliedOffsets.GetKeys(Keys);
			for (const FName& Key : Keys)
			{
				const FVector2D Cur = E->AppliedOffsets.FindRef(Key);
				if (!Cur.IsNearlyZero()) { ShiftTargetByKey(Id, Key, -Cur); ++NumMoved; }
			}
			E->AppliedOffsets.Reset();
			E->ClampPendingKeys.Reset();   // сдвигов нет — клампить нечего
			ClearClampWatch(*E);           // М-2: и ждать показа нечего
			E->DragOffset = FVector2D::ZeroVector;
		}
		SavedLayouts.Remove(Id);
		PendingClamp.Remove(Id);
	}
	bLayoutDirty = true;
	FlushWindowLayout();
	UE_LOG(LogAsdaWindow, Display,
		TEXT("[раскладка окон] СБРОС: окон %d, возвращено на место прямо сейчас %d. ")
		TEXT("★ Размеры окон с ресайзом (чат 2500 / журнал 2570) тоже сброшены — они возьмут ")
		TEXT("свой дефолт при следующей постройке окна."),
		Targets.Num(), NumMoved);
}

FString UAsdaWindowSubsystem::DebugWindowLayout() const
{
	LoadAllLayouts();
	FString S = FString::Printf(
		TEXT("  РАСКЛАДКА ОКОН МЕЖДУ СЕССИЯМИ\n    файл: %s\n    секция: [%s]\n    записей: %d%s"),
		*GGameUserSettingsIni, kLayoutSection, SavedLayouts.Num(),
		bLayoutDirty ? TEXT("  (есть НЕзаписанные изменения)") : TEXT(""));
	if (SavedLayouts.Num() == 0)
	{
		S += TEXT("\n    (пусто — ни одно окно ещё не двигали и не растягивали)");
	}
	for (const TPair<FName, FAsdaWindowSavedLayout>& Kv : SavedLayouts)
	{
		const FAsdaWindowEntry* E = Windows.Find(Kv.Key);
		S += FString::Printf(TEXT("\n    %-24s %s"), *Kv.Key.ToString(),
			E ? TEXT("[открыто]") : TEXT("[закрыто]"));
		for (const TPair<FName, FVector2D>& Off : Kv.Value.Offsets)
		{
			if (Kv.Value.ExternalKeys.Contains(Off.Key))
			{
				S += FString::Printf(TEXT("\n        поз. %-26s в ini (%.1f,%.1f)  [применяет сам владелец окна]"),
					*Off.Key.ToString(), Off.Value.X, Off.Value.Y);
				continue;
			}
			const FVector2D Now = E ? E->AppliedOffsets.FindRef(Off.Key) : FVector2D::ZeroVector;
			S += FString::Printf(TEXT("\n        поз. %-26s в ini (%.1f,%.1f)  применено сейчас (%.1f,%.1f)%s"),
				*Off.Key.ToString(), Off.Value.X, Off.Value.Y, Now.X, Now.Y,
				(E && !Now.Equals(Off.Value, 0.5f)) ? TEXT("  ← поправлено клампом в экран") : TEXT(""));
		}
		if (Kv.Value.bHasSize)
		{
			S += FString::Printf(TEXT("\n        размер %.0fx%.0f базовых px, признак раскладки=%d"),
				Kv.Value.Size.X, Kv.Value.Size.Y, Kv.Value.bSizeFlag ? 1 : 0);
		}
	}
	// Второй раздел: КТО вообще сохраняется — чтобы «моё окно не запоминается» отвечалось за секунду.
	S += TEXT("\n    ОТКРЫТЫЕ ОКНА И ИХ ПРИЗНАК СОХРАНЯЕМОСТИ (ручка в данных .JUI + не модальное):");
	for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
	{
		const bool bSave = IsWindowLayoutSaveable(Kv.Key);
		S += FString::Printf(TEXT("\n        %-24s %s"), *Kv.Key.ToString(),
			bSave ? TEXT("сохраняется")
			      : (Kv.Value.bModal || Kv.Value.ZOrder >= kModalZ
			         ? TEXT("НЕТ — модальное (появляется по месту)")
			         : (GDragDisabledWindows().Contains(Kv.Key)
			            ? TEXT("НЕТ — перетаскивание запрещено юзером (сохраняется только размер)")
			            : TEXT("НЕТ — ручки перетаскивания в .JUI нет (окно неподвижно)"))));
	}
	if (PendingClamp.Num() > 0)
	{
		S += FString::Printf(TEXT("\n    ждут клампа в экран (нет живой геометрии): %d"), PendingClamp.Num());
	}
	// М-2 (07.10.2026): окна, ключи которых ждут показа скрытого куска, — по событию, без покадрового опроса.
	int32 NumParked = 0;
	for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows) { if (Kv.Value.ClampWatch.Num() > 0) { ++NumParked; } }
	if (NumParked > 0)
	{
		S += FString::Printf(TEXT("\n    ждут показа скрытого куска (подписка на показ, без опроса): %d"), NumParked);
	}
	return S;
}
