// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPremiumPostSubsystem_Debug.cpp — ★ R3-c 12.11.2026 (разрез AsdaPremiumPostSubsystem.cpp, дословный перенос): дампы, симуляции, дев-команды GPost*, самотест строк.
//   Общее для частей — в AsdaPremiumPostSubsystem_Private.h; шапка и реверс — в AsdaPremiumPostSubsystem.cpp.
#include "UI/AsdaPremiumPostSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"   // ★ U13 (дедуп №2, В2-A): ConsoleEcho — общее эхо консольных команд

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Framework/Application/SlateApplication.h"   // живой хит-тест: кого Slate реально видит под точкой
#include "Types/ReflectionMetadata.h"                 // имя UMG-виджета по SWidget в дампе хит-теста
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "UI/AsdaAnimatedPlane.h"
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaPremiumGachaSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "AsdaCVarSave.h"                    // ★ 25.10.2026: самотест строк ящика — ключ с возвратом
#include "Test/AsdaSelfTestKit.h"            // ★ 25.10.2026: каркас самотеста (quit, итог)
#include "TimerManager.h"
#include "Engine/World.h"
#include "AsdaPremiumPostSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaPremiumPostSubsystem*.cpp

// ═══════════════════════════════ ДИАГНОСТИКА ═══════════════════════════════════════════════════
FString UAsdaPremiumPostSubsystem::DumpControls() const
{
	FString S;
	S += FString::Printf(TEXT("=== Primium_Item_post_WIN (69000): окно %s, контролы %s ===\n"),
		Win ? TEXT("создано") : TEXT("НЕТ"), bReady ? TEXT("разобраны") : TEXT("НЕ разобраны"));
	S += FString::Printf(TEXT("69001 заголовок : %s\n"), TitleText ? *TitleText->GetName() : TEXT("НЕТ"));
	S += FString::Printf(TEXT("69002 примечание: %s\n"), NoteText ? *NoteText->GetName() : TEXT("НЕТ"));
	S += FString::Printf(TEXT("69079 рамка     : %s\n"), SelFrame ? *SelFrame->GetName() : TEXT("НЕТ"));
	S += FString::Printf(TEXT("69081 радио     : %d элементов\n"), RadioImages.Num());
	for (int32 R = 0; R < RowGroups.Num(); ++R)
	{
		S += FString::Printf(TEXT("строка %d: группа %s, подсветка %s, иконки"), R + 1,
			RowGroups[R] ? *RowGroups[R]->GetName() : TEXT("НЕТ"),
			RowHighlights.IsValidIndex(R) && RowHighlights[R] ? *RowHighlights[R]->GetName() : TEXT("НЕТ"));
		for (int32 C = 0; C < kCols; ++C)
		{
			const int32 Idx = R * kCols + C;
			S += FString::Printf(TEXT(" %s"),
				IconImages.IsValidIndex(Idx) && IconImages[Idx] ? *IconImages[Idx]->GetName() : TEXT("—"));
		}
		S += TEXT("\n");
	}
	return S;
}

FString UAsdaPremiumPostSubsystem::DumpModel() const
{
	FString S;
	S += FString::Printf(TEXT("=== элитный ящик: окно %s, запрос в полёте: %s, выбрана строка %d ===\n"),
		IsWindowVisible() ? TEXT("видно") : TEXT("скрыто"), bRequestInFlight ? TEXT("да") : TEXT("нет"), SelectedRow);
	for (int32 R = 0; R < Rows.Num(); ++R)
	{
		S += FString::Printf(TEXT("[%d]%s Order=%d ItemIndex=%d Count=%d BuyType=%d | ячейки:"),
			R, R == SelectedRow ? TEXT("*") : TEXT(" "), Rows[R].Order, Rows[R].ItemIndex, Rows[R].Count, Rows[R].BuyType);
		for (int32 C = 0; C < kCols; ++C)
		{
			const int32 Idx = R * kCols + C;
			S += FString::Printf(TEXT(" %d"), CellCodes.IsValidIndex(Idx) ? CellCodes[Idx] : -1);
		}
		S += TEXT("\n");
	}
	return S;
}

void UAsdaPremiumPostSubsystem::SimulateList(const TArray<uint8>& Body)
{
	UE_LOG(LogAsdaPost, Log, TEXT("[элитный ящик] эмуляция 11005: тело %d б"), Body.Num());
	ApplyListBody(Body);
}

void UAsdaPremiumPostSubsystem::SimulateWorldReady()
{
	OnWorldReady();
	WorldEntryQueryDelay = 0.f;   // в живой проверке ждать секунду незачем — путь тот же
}

// ★★★ ЖИВОЙ ХИТ-ТЕСТ ОКНА — жалоба владельца «иногда приходится нажимать по два раза».
//   Прежде проверить её было НЕЧЕМ: `DumpControls` печатает только ИМЕНА виджетов, а вопрос стоял
//   про геометрию и перекрытие. Здесь для каждого кликабельного контрола идут три факта подряд:
//     1) видимость виджета (свёрнутый родитель делает поддерево неотрисованным и некликабельным);
//     2) РЕАЛЬНЫЙ экранный прямоугольник (`AsdaUi::WidgetAbsRect` — общая точка, своей нет);
//     3) кого Slate ВОЗВРАЩАЕТ под центром этого прямоугольника (`LocateWindowUnderMouse`) — это
//        и есть ответ на «хит-тест перекрыт соседним виджетом».
//   Плюс z-порядок ВСЕХ открытых окон общей точкой `UAsdaWindowSubsystem::DebugDragHandles()`:
//   перевешивание окна между нажатием и отпусканием съедает клик целиком (разбор — у
//   `UAsdaWindowSubsystem::SetWindowZOrder`).
FString UAsdaPremiumPostSubsystem::DumpHitTest() const
{
	// Имя самого глубокого виджета, который Slate находит в точке `P` (пусто — не нашёл ничего).
	auto TopWidgetAt = [](const FVector2D& P) -> FString
	{
		if (!FSlateApplication::IsInitialized()) { return TEXT("Slate не поднят"); }
		FSlateApplication& App = FSlateApplication::Get();
		const FWidgetPath Path = App.LocateWindowUnderMouse(P, App.GetInteractiveTopLevelWindows(), false, 0);
		if (!Path.IsValid() || Path.Widgets.Num() == 0) { return TEXT("НИЧЕГО (клик уйдёт в мир)"); }
		// Хвост пути = самый глубокий хит-тестируемый виджет под точкой. Печатаем ТРИ последних:
		//   у UMG самый глубокий часто безымянный SBox/SOverlay, и имя даёт его родитель.
		FString Out;
		const int32 First = FMath::Max(0, Path.Widgets.Num() - 3);
		for (int32 i = Path.Widgets.Num() - 1; i >= First; --i)
		{
			const TSharedRef<SWidget>& Wg = Path.Widgets[i].Widget;
			const FString Dbg = FReflectionMetaData::GetWidgetDebugInfo(&Wg.Get());
			Out += FString::Printf(TEXT("%s%s[%s]"), Out.IsEmpty() ? TEXT("") : TEXT(" < "),
				*Wg->GetTypeAsString(), *Dbg);
		}
		return Out;
	};
	auto Line = [&TopWidgetAt](const TCHAR* Title, const UWidget* Wd) -> FString
	{
		if (!Wd) { return FString::Printf(TEXT("  %-28s ВИДЖЕТА НЕТ\n"), Title); }
		FVector2D Mn, Mx;
		const bool bRect = AsdaUi::WidgetAbsRect(Wd, Mn, Mx);
		if (!bRect)
		{
			return FString::Printf(TEXT("  %-28s %-34s видимость=%d | ЭКРАННОГО ПРЯМОУГОЛЬНИКА НЕТ (ни разу не отрисован)\n"),
				Title, *Wd->GetName(), (int32)Wd->GetVisibility());
		}
		const FVector2D C = (Mn + Mx) * 0.5f;
		return FString::Printf(TEXT("  %-28s %-34s видимость=%d | экран=(%.0f,%.0f)-(%.0f,%.0f) центр=(%.0f,%.0f) | под центром Slate видит: %s\n"),
			Title, *Wd->GetName(), (int32)Wd->GetVisibility(), Mn.X, Mn.Y, Mx.X, Mx.Y, C.X, C.Y,
			*TopWidgetAt(C));
	};

	FString S = FString::Printf(TEXT("=== элитный ящик: ХИТ-ТЕСТ (окно %s, контролы %s, выбрана строка %d) ===\n"),
		IsWindowVisible() ? TEXT("видно") : TEXT("СКРЫТО"), bReady ? TEXT("разобраны") : TEXT("НЕ разобраны"),
		SelectedRow);

	S += Line(TEXT("69082 «В рюкзак»"), Win ? Win->GetWidgetFromName(TEXT("Primium_Item_post_OK")) : nullptr);
	S += Line(TEXT("69083 «Закрыть»"),  Win ? Win->GetWidgetFromName(TEXT("Primium_Item_post_X")) : nullptr);
	S += Line(TEXT("69089 крестик"),    Win ? Win->GetWidgetFromName(TEXT("Primium_Item_post_XX")) : nullptr);
	S += Line(TEXT("69001 ручка окна"), Win ? Win->GetWidgetFromName(TEXT("Primium_Item_post_movetext")) : nullptr);

	S += TEXT("--- 69081 радиогруппа (та самая «галочка»): элемент строки, её группа и первая иконка ---\n");
	for (int32 R = 0; R < kRows; ++R)
	{
		S += Line(*FString::Printf(TEXT("радио строки %d"), R), RadioImages.IsValidIndex(R) ? RadioImages[R].Get() : nullptr);
		S += Line(*FString::Printf(TEXT("группа строки %d"), R), RowGroups.IsValidIndex(R) ? RowGroups[R].Get() : nullptr);
		S += Line(*FString::Printf(TEXT("иконка [%d][0]"), R), IconImages.IsValidIndex(R * kCols) ? IconImages[R * kCols].Get() : nullptr);
		// Прозрачная кнопка-оверлей ОБЩЕЙ сетки: именно она шлёт `OnIconClicked`.
		S += Line(*FString::Printf(TEXT("оверлей клика [%d][0]"), R),
			Win ? Win->GetWidgetFromName(FName(*FString::Printf(TEXT("CELL_HIT%02d"), R * kCols))) : nullptr);
	}

	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		S += TEXT("--- z-порядок окон (общая точка `DebugDragHandles`) ---\n");
		S += WM->DebugDragHandles();
		S += TEXT("\n");
	}
	return S;
}

// ★★★ ПРОБА «ЧТО ДЕЛАЕТ С КОНТРОЛАМИ ПЕРЕВЕШИВАНИЕ ОКНА». Кнопки окна — виджеты Slate, и у них
//   перевешивание отбирает захват мыши (разбор — у `UAsdaWindowSubsystem::SetWindowZOrder`). А вот
//   «галочка» 69081 ДО 2026-08-27 кликалась НЕ виджетом, а покадровым хит-тестом по
//   `GetCachedGeometry`; проба осталась как измеритель геометрии планов после перевешивания окна.
//   Ответ нужен фактом, а не рассуждением, поэтому меряем В ОДНОМ ВЫЗОВЕ: до подъёма → подъём →
//   сразу после. Печатается и z окна, чтобы было видно, что подъём реально состоялся.
FString UAsdaPremiumPostSubsystem::ProbeRaise()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return TEXT("реестра окон нет"); }
	auto Rect = [](const UWidget* Wd) -> FString
	{
		FVector2D Mn, Mx;
		if (!Wd) { return TEXT("виджета нет"); }
		if (!AsdaUi::WidgetAbsRect(Wd, Mn, Mx)) { return TEXT("ПРЯМОУГОЛЬНИКА НЕТ"); }
		return FString::Printf(TEXT("(%.0f,%.0f)-(%.0f,%.0f)"), Mn.X, Mn.Y, Mx.X, Mx.Y);
	};
	const UWidget* Radio = RadioImages.IsValidIndex(0) ? RadioImages[0].Get() : nullptr;
	const UWidget* Ok    = Win ? Win->GetWidgetFromName(TEXT("Primium_Item_post_OK")) : nullptr;
	FString S = FString::Printf(TEXT("=== проба подъёма окна 69000 ===\nдо   : z=%d | радио 0 %s | кнопка %s \n"),
		WM->GetWindowZOrder(FName(kWinId)), *Rect(Radio), *Rect(Ok));
	WM->BringWindowToFront(FName(kWinId));
	S += FString::Printf(TEXT("после: z=%d | радио 0 %s | кнопка %s \n"),
		WM->GetWindowZOrder(FName(kWinId)), *Rect(Radio), *Rect(Ok));
	return S;
}

FString UAsdaPremiumPostSubsystem::DumpNoticeState() const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	UAsdaHotbarSubsystem* Hot = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bPanel = WM && WM->IsOpen(FName(TEXT("QUICK_NEW_WIN")));
	return FString::Printf(
		TEXT("=== элитный ящик: иконка при входе в мир === иконка нужна: %s | иконка висит: %s | ")
		TEXT("панель QUICK_NEW_WIN поднята: %s | запрос 11004 отложен: %s (через %.1f с) | ")
		TEXT("запрос в полёте: %s | соединение с игровым сервером: %s"),
		bNoticeWanted ? TEXT("ДА") : TEXT("нет"),
		(Hot && Hot->HasNoticeIcon(kNoticeType)) ? TEXT("ДА") : TEXT("нет"),
		bPanel ? TEXT("да") : TEXT("НЕТ"),
		bWorldEntryQueryPending ? TEXT("ДА") : TEXT("нет"), WorldEntryQueryDelay,
		bRequestInFlight ? TEXT("да") : TEXT("нет"),
		(Net && Net->bInGameConnected) ? TEXT("есть") : TEXT("НЕТ"))
		+ FString::Printf(
			TEXT("\n★ нововведение «просмотрено»: %s | непросмотренных заказов: %d | просмотренных: %d"),
			bSeenGateEnabled ? TEXT("включено") : TEXT("ВЫКЛЮЧЕНО (поведение эталона)"),
			UnseenCount, SeenOrders.Num());
}

// ★★★ НАШЕ НОВОВВЕДЕНИЕ — живой дамп набора «просмотренных заказов». Заведён потому, что вопрос
//   «почему иконка (не) горит» без него отвечается только рассуждением: видно и текущее
//   содержимое ящика, и по каждой строке — считается она просмотренной или новой.
FString UAsdaPremiumPostSubsystem::DumpSeenState() const
{
	FString S = FString::Printf(
		TEXT("=== элитный ящик: ★ НОВОВВЕДЕНИЕ «просмотренные заказы» (в эталоне механизма нет) ===\n")
		TEXT("признак включён: %s | владелец набора CharIndex=%d | окно: %s\n")
		TEXT("непросмотренных заказов: %d | иконка нужна: %s | иконка висит: %s\n"),
		bSeenGateEnabled ? TEXT("ДА") : TEXT("нет (откат к эталону)"),
		SeenOwnerCharId,
		IsWindowVisible() ? TEXT("видно") : TEXT("скрыто"),
		UnseenCount,
		bNoticeWanted ? TEXT("ДА") : TEXT("нет"),
		[this]() -> const TCHAR*
		{
			UGameInstance* G = GetGameInstance();
			UAsdaHotbarSubsystem* H = G ? G->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
			return (H && H->HasNoticeIcon(kNoticeType)) ? TEXT("ДА") : TEXT("нет");
		}());

	S += FString::Printf(TEXT("--- текущее содержимое ящика (%d непустых строк) ---\n"), GetFilledRowCount());
	// Проверка «просмотрен ли» — той же мультимножественной логикой, что и в `ReconcileSeenOrders`,
	//   иначе дамп врал бы про дубли (закон `false-negatives-method`: доказывать вторым методом).
	TArray<FAsdaPremiumPostSeenKey> Pool = SeenOrders;
	for (int32 R = 0; R < Rows.Num(); ++R)
	{
		if (Rows[R].IsEmpty()) { continue; }
		const FAsdaPremiumPostSeenKey Key(Rows[R]);
		const int32 Idx = Pool.IndexOfByKey(Key);
		const bool bSeen = (Idx != INDEX_NONE);
		if (bSeen) { Pool.RemoveAtSwap(Idx); }
		S += FString::Printf(TEXT("  строка %d: %s -> %s\n"), R, *Key.ToText(),
			bSeen ? TEXT("ПРОСМОТРЕН") : TEXT("★ НОВЫЙ (иконка обязана гореть)"));
	}
	S += FString::Printf(TEXT("--- набор просмотренного (%d записей, потолок %d) ---\n"), SeenOrders.Num(), kRows);
	for (int32 I = 0; I < SeenOrders.Num(); ++I)
	{
		S += FString::Printf(TEXT("  [%d] %s\n"), I, *SeenOrders[I].ToText());
	}
	return S;
}

void UAsdaPremiumPostSubsystem::SimulateApply(int32 CharId, int32 ItemCode, int32 BuyType)
{
	// Кадр 11007 = 409 б: статус(1) + CharIndex(4) + 7 записей по 57 + вес(4) + BuyType(1).
	//   Заполняем ПЕРВУЮ запись (именно её эталон проверяет веткой `if (!j)`), остальные — пустые
	//   (`amount == -1` ⇒ «ячейку очистить», слот −1 ⇒ инвентарь их пропускает).
	TArray<uint8> B;
	B.SetNumZeroed(409);
	B[0] = 1;                                   // статус «успех»
	AsdaBytes::WrI32(&B[1], CharId);
	for (int32 j = 0; j < kRows; ++j)
	{
		uint8* Rec = &B[5 + 57 * j];
		AsdaBytes::WrI32(Rec + 9, -1);          // amount = −1: запись пустая
		*reinterpret_cast<int16*>(Rec + 5) = (int16)-1;
	}
	uint8* R0 = &B[5];
	AsdaBytes::WrI32(R0 + 0, ItemCode);
	R0[4] = 1;                                  // контейнер 1 (сумка) — внешнее условие эталона
	*reinterpret_cast<int16*>(R0 + 5) = (int16)0;   // слот 0
	AsdaBytes::WrI32(R0 + 9, 1);                // amount = 1
	AsdaBytes::WrI32(&B[404], 0);               // вес
	B[408] = (uint8)BuyType;
	UE_LOG(LogAsdaPost, Log, TEXT("[элитный ящик] эмуляция 11007: предмет %d, BuyType %d"), ItemCode, BuyType);
	ApplyResultBody(B);
}

int32 UAsdaPremiumPostSubsystem::SimulateIconClick(int32 Cell)
{
	if (IconGrid) { IconGrid->SimulateClick(Cell); }
	else { UE_LOG(LogAsdaPost, Warning, TEXT("[элитный ящик] сетки иконок нет — окно не открывалось")); }
	return SelectedRow;
}

void UAsdaPremiumPostSubsystem::SimulateCheckboxClick(int32 Row)
{
	UButton* B = Win ? Cast<UButton>(Win->GetWidgetFromName(FName(*FString::Printf(TEXT("Primium_Item_post_BOX_SEL00_OPT%d"), Row)))) : nullptr;
	if (B) { B->OnClicked.Broadcast(); }
	else { UE_LOG(LogAsdaPost, Warning, TEXT("[элитный ящик] кнопки галочки строки %d нет — окно не открывалось"), Row); }
}

namespace {
// ═══════════════════════════════ КОНСОЛЬ ═══════════════════════════════════════════════════════
	UAsdaPremiumPostSubsystem* PostSys(UWorld* W)
	{
		return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaPremiumPostSubsystem>() : nullptr;
	}
}

// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U13): было `AddOnScreenDebugMessage` (12 с поверх экрана; в Shipping оверлей пуст) — стало общая точка `AsdaNotice::ConsoleEcho`: каждая строка в журнал (Display), непустая — в ленту Tip.
#if !UE_BUILD_SHIPPING   // ★ античит А2 (12.10.2026): отладка самотестов/дев-команд — только вне Shipping
namespace {
	void PostEcho(UWorld* W, const FString& S) { AsdaNotice::ConsoleEcho(W, S, LogAsdaPost); }
}
#endif // !UE_BUILD_SHIPPING

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPostOpen(
	TEXT("asda.Post.Open"),
	TEXT("asda.Post.Open [0|1] — открыть элитный почтовый ящик (69000). Аргумент 1 (по умолчанию) — "
	     "открыть ВМЕСТЕ с инвентарём (требование владельца), 0 — как в эталоне (инвентарь закрыть)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaPremiumPostSubsystem* P = PostSys(W);
		if (!P) { PostEcho(W, TEXT("подсистемы элитного ящика нет")); return; }
		const bool bBoth = (A.Num() < 1) || (FCString::Atoi(*A[0]) != 0);
		PostEcho(W, FString::Printf(TEXT("asda.Post.Open: %s"), P->OpenAndSync(bBoth) ? TEXT("OK") : TEXT("ОТКАЗ")));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPostClose(
	TEXT("asda.Post.Close"), TEXT("asda.Post.Close — закрыть окно элитного ящика."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaPremiumPostSubsystem* P = PostSys(W)) { P->CloseWindow(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPostTake(
	TEXT("asda.Post.Take"),
	TEXT("asda.Post.Take [строка 0..6] — забрать заказ (11006). Без аргумента — текущую выбранную строку."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaPremiumPostSubsystem* P = PostSys(W);
		if (!P) { return; }
		if (A.Num() >= 1) { P->SelectRow(FCString::Atoi(*A[0])); }
		P->TakeSelected();
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPostDump(
	TEXT("asda.Post.Dump"),
	TEXT("asda.Post.Dump [controls|model] — дамп контролов окна 69000 либо модели строк."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaPremiumPostSubsystem* P = PostSys(W);
		if (!P) { PostEcho(W, TEXT("подсистемы элитного ящика нет")); return; }
		const bool bCtl = (A.Num() >= 1) && A[0].Equals(TEXT("controls"), ESearchCase::IgnoreCase);
		PostEcho(W, bCtl ? P->DumpControls() : P->DumpModel());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPostSim(
	TEXT("asda.Post.Sim"),
	TEXT("asda.Post.Sim <charId> <ItemIndex:Count:BuyType[:Order]> [...] — эмуляция 11005 без сервера. "
	     "Пример: asda.Post.Sim 12345 5047:3:1 5100:1:3. Проверяет и кламп N > 7, и кламп Count > 7. "
	     "★ Четвёртое поле Order (по умолчанию — номер записи) нужно живой проверке нововведения "
	     "«просмотренные заказы»: им задаётся, тот же это заказ или новый."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельные строки ящика — Post.Take отправил бы 11006 с поддельными Order/ItemIndex.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Post.Sim"))) { return; }
		UAsdaPremiumPostSubsystem* P = PostSys(W);
		if (!P || A.Num() < 1) { PostEcho(W, TEXT("использование: asda.Post.Sim <charId> <item:count:type> …")); return; }
		TArray<uint8> Body;
		Body.SetNumZeroed(4);
		AsdaBytes::WrI32(&Body[0], FCString::Atoi(*A[0]));
		for (int32 i = 1; i < A.Num(); ++i)
		{
			TArray<FString> Parts;
			A[i].ParseIntoArray(Parts, TEXT(":"), true);
			if (Parts.Num() < 1) { continue; }
			const int32 Base = Body.Num();
			Body.SetNumZeroed(Base + 10);
			// ★ Четвёртое поле `order` добавлено ради живой проверки нововведения «просмотрено»:
			//   именно `Order` вместе с остальными тремя полями образует ключ заказа, и без
			//   возможности его задать сценарий «пришёл НОВЫЙ заказ» не отличить от «тот же».
			//   Без него — прежнее поведение (порядковый номер записи), обратная совместимость.
			AsdaBytes::WrI32(&Body[Base + 0], Parts.Num() > 3 ? FCString::Atoi(*Parts[3]) : i);  // Order
			AsdaBytes::WrI32(&Body[Base + 4], FCString::Atoi(*Parts[0]));                // ItemIndex
			Body[Base + 8] = (uint8)(Parts.Num() > 1 ? FCString::Atoi(*Parts[1]) : 1);   // Count
			Body[Base + 9] = (uint8)(Parts.Num() > 2 ? FCString::Atoi(*Parts[2]) : 1);   // BuyType
		}
		P->SimulateList(Body);
		PostEcho(W, P->DumpModel());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPostSimApply(
	TEXT("asda.Post.SimApply"),
	TEXT("asda.Post.SimApply <charId> <itemCode> [BuyType=3] — эмуляция 11007 (результат забора). "
	     "Живая проверка гейта окна 68500: у предмета с вариантами оно откроется, у предмета без "
	     "вариантов — нет (замечание владельца №1)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный успех 11007 кладёт предмет в сумку и шлёт 11004; открывает путь 6581 по поддельной записи.
		static const uint16 kGuardOps[] = { 11004 };
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Post.SimApply"), 5.f, MakeArrayView(kGuardOps))) { return; }
		UAsdaPremiumPostSubsystem* P = PostSys(W);
		if (!P || A.Num() < 2) { PostEcho(W, TEXT("использование: asda.Post.SimApply <charId> <itemCode> [BuyType]")); return; }
		P->SimulateApply(FCString::Atoi(*A[0]), FCString::Atoi(*A[1]),
			A.Num() > 2 ? FCString::Atoi(*A[2]) : 3);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPostHideUntilChoice(
	TEXT("asda.Post.HideUntilChoice"),
	TEXT("asda.Post.HideUntilChoice [0|1] — ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА по замечанию владельца: 1 — приз "
	     "с выбором «профессия × уровень» не показывать в инвентаре, пока выбор не подтверждён "
	     "(ПО УМОЛЧАНИЮ, решение владельца 2026-08-19); 0 — как в оригинале, предмет виден сразу. "
	     "Без аргумента — показать состояние."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaPremiumPostSubsystem* P = PostSys(W);
		if (!P) { PostEcho(W, TEXT("подсистемы элитного ящика нет")); return; }
		if (A.Num() >= 1) { P->SetDeferItemUntilChoice(FCString::Atoi(*A[0]) != 0); }
		PostEcho(W, FString::Printf(TEXT("не показывать приз до выбора варианта: %s (эталон — «нет»)"),
			P->IsDeferItemUntilChoice() ? TEXT("ДА") : TEXT("нет")));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GPostProbeRaise(
	TEXT("asda.Post.ProbeRaise"),
	TEXT("asda.Post.ProbeRaise — поднять окно 69000 наверх и тут же сравнить геометрию «галочки» и "
	     "кнопки ДО и ПОСЛЕ. Проверка, ломает ли перевешивание окна покадровый хит-тест радиогруппы."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		UAsdaPremiumPostSubsystem* P = PostSys(W);
		if (!P) { PostEcho(W, TEXT("подсистемы элитного ящика нет")); return; }
		PostEcho(W, P->ProbeRaise());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GPostHitDump(
	TEXT("asda.Post.HitDump"),
	TEXT("asda.Post.HitDump — живой хит-тест окна 69000: экранные прямоугольники кнопки «Положить "
	     "в рюкзак», радиогруппы («галочки»), строк и оверлеев клика по иконкам + кого Slate реально "
	     "видит под центром каждого + z-порядок всех окон. Заведена по жалобе «приходится нажимать "
	     "по два раза»."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		UAsdaPremiumPostSubsystem* P = PostSys(W);
		if (!P) { PostEcho(W, TEXT("подсистемы элитного ящика нет")); return; }
		PostEcho(W, P->DumpHitTest());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GPostNoticeState(
	TEXT("asda.Post.NoticeState"),
	TEXT("asda.Post.NoticeState — состояние механизма «иконка при входе в мир» (замечание владельца №6): "
	     "нужна ли иконка, висит ли она, поднята ли панель квик-бара, ждёт ли отправки 11004."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		UAsdaPremiumPostSubsystem* P = PostSys(W);
		if (!P) { PostEcho(W, TEXT("подсистемы элитного ящика нет")); return; }
		PostEcho(W, P->DumpNoticeState());
	}));
#endif // ASDA_DEV_CONSOLE

// ═══ ★★★ КОМАНДЫ НАШЕГО НОВОВВЕДЕНИЯ «ПРОСМОТРЕННЫЕ ЗАКАЗЫ» (в эталоне механизма нет) ═══
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GPostSeenDump(
	TEXT("asda.Post.SeenDump"),
	TEXT("asda.Post.SeenDump — ★ НОВОВВЕДЕНИЕ (не эталон): набор просмотренных заказов и разметка "
	     "каждой строки ящика «ПРОСМОТРЕН / НОВЫЙ». Отвечает фактом на вопрос, почему иконка типа 15 "
	     "горит или не горит."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		UAsdaPremiumPostSubsystem* P = PostSys(W);
		if (!P) { PostEcho(W, TEXT("подсистемы элитного ящика нет")); return; }
		PostEcho(W, P->DumpSeenState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GPostSeenReset(
	TEXT("asda.Post.SeenReset"),
	TEXT("asda.Post.SeenReset — ★ НОВОВВЕДЕНИЕ: забыть все просмотренные заказы (как при перезапуске "
	     "клиента). Следующий 11005 снова зажжёт иконку."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		UAsdaPremiumPostSubsystem* P = PostSys(W);
		if (!P) { PostEcho(W, TEXT("подсистемы элитного ящика нет")); return; }
		P->ResetSeenOrders(TEXT("команда asda.Post.SeenReset"));
		PostEcho(W, P->DumpSeenState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPostSeenGate(
	TEXT("asda.Post.SeenGate"),
	TEXT("asda.Post.SeenGate [0|1] — ★ НОВОВВЕДЕНИЕ ПО ТРЕБОВАНИЮ ВЛАДЕЛЬЦА: 1 (по умолчанию) — "
	     "просмотренный ящик больше не моргает иконкой, она возвращается только на НОВЫЙ заказ; "
	     "0 — поведение ЭТАЛОНА: иконка загорается на каждый непустой список 11005. "
	     "Без аргумента — показать состояние."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaPremiumPostSubsystem* P = PostSys(W);
		if (!P) { PostEcho(W, TEXT("подсистемы элитного ящика нет")); return; }
		if (A.Num() >= 1) { P->SetSeenGateEnabled(FCString::Atoi(*A[0]) != 0); }
		PostEcho(W, FString::Printf(
			TEXT("asda.Post.SeenGate = %d (1 = наше нововведение; 0 = как в эталоне — иконка на каждый список)"),
			P->IsSeenGateEnabled() ? 1 : 0));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GPostSimWorldReady(
	TEXT("asda.Post.SimWorldReady"),
	TEXT("asda.Post.SimWorldReady — прогнать тот же путь, что делегат OnWorldReady (4055/4056): "
	     "живая проверка запроса состояния ящика при входе в мир и при смене карты."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: эмуляция входа в мир шлёт 11004.
		static const uint16 kGuardOps[] = { 11004 };
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Post.SimWorldReady"), 5.f, MakeArrayView(kGuardOps))) { return; }
		UAsdaPremiumPostSubsystem* P = PostSys(W);
		if (!P) { return; }
		P->SimulateWorldReady();
		PostEcho(W, P->DumpNoticeState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPostLegacy(
	TEXT("asda.Post.LegacyCloseInven"),
	TEXT("asda.Post.LegacyCloseInven <0|1> — 1 = поведение ОРИГИНАЛА: открытие ящика по иконке "
	     "уведомления ЗАКРЫВАЕТ инвентарь. 0 (по умолчанию) = требование владельца: открываются оба окна."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaPremiumPostSubsystem* P = PostSys(W);
		if (!P) { return; }
		if (A.Num() >= 1) { P->SetLegacyCloseInventory(FCString::Atoi(*A[0]) != 0); }
		PostEcho(W, FString::Printf(TEXT("asda.Post.LegacyCloseInven = %d (1 = как в оригинале: инвентарь закрывается)"),
			P->IsLegacyCloseInventory() ? 1 : 0));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPostClick(
	TEXT("asda.Post.ClickIcon"),
	TEXT("asda.Post.ClickIcon <ячейка 0..48> — прогнать клик по иконке ТЕМ ЖЕ путём, что и кнопка-оверлей. "
	     "Живая проверка исправления дефекта A: в оригинале клик по иконке выбор строки НЕ менял."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaPremiumPostSubsystem* P = PostSys(W);
		if (!P || A.Num() < 1) { PostEcho(W, TEXT("использование: asda.Post.ClickIcon <0..48>")); return; }
		const int32 Cell = FCString::Atoi(*A[0]);
		const int32 Before = P->GetSelectedRow();
		const int32 After = P->SimulateIconClick(Cell);
		PostEcho(W, FString::Printf(
			TEXT("asda.Post.ClickIcon %d (строка %d, колонка %d): выбор %d -> %d"),
			Cell, Cell / UAsdaPremiumPostSubsystem::kCols, Cell % UAsdaPremiumPostSubsystem::kCols, Before, After));
	}));
#endif // ASDA_DEV_CONSOLE

// ★★★ 25.10.2026 САМОТЕСТ «СТРОКА ЯЩИКА = ЗАКАЗ» ([ИСПРАВЛЕНИЕ] `asda.EliteMail.MultiItemRows`; разбор — у объявления ключа).
//   Без сети: 11005 подаётся в настоящий разбор (`SimulateList`), 11006 перехватывается (`DebugSetTakeCapture`), 11007 —
//   в настоящий `ApplyResultBody`; следующую запись заказа шлёт настоящий Tick (шаги по таймеру 0,3 с). Случаи:
//     L  список из 5 заказов: 1 штука; 3 × 75701 (корзина 19.10); смешанный 75701+63103+71188; 7 × 62728 (полная строка);
//        старая запись 75702 Count 3 — 5 строк, ячейки по записям, коды смешанного заказа по местам;
//     T1 забор корзины из 3: три 11006 подряд (каждый после 11007 код 1), по одной записи с Count 1, запуск окончен;
//     T2 смешанная: 11006 с кодами 75701 → 63103 → 71188;
//     T3 мест не хватает (2 свободных в сумке 1 при 7 штуках): 11006 НЕ уходит;
//     T4 отказ сервера на первой записи строки из 7: дальше не шлётся, запуск окончен;
//     T5 ключ 0 — как оригинал: запись = строка, 7 строк из первых 7 записей.
//   Итог `САМОТЕСТ-ЯЩИК-СТРОКИ ИТОГ: ok=N fail=M`; ключ, перехват и подстановка мест возвращаются, модель очищается.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPostRowsSelfTest(
	TEXT("asda.EliteMail.RowsSelfTest"),
	TEXT("asda.EliteMail.RowsSelfTest [quit] — строка премиум-почты = заказ: разбор 11005 (до 49 записей), ячейки, забор строки "
	     "целиком (по 11006 на запись), проверка места, отказ, ключ 0 — как оригинал"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		static const TCHAR* kCmd = TEXT("asda.EliteMail.RowsSelfTest");
		static const TCHAR* kKey = TEXT("asda.EliteMail.MultiItemRows");
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args, 60.f)) { return; }
		UAsdaPremiumPostSubsystem* P = PostSys(W);
		if (!W || !P) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет мира или подсистемы ящика")); return; }
		if (P->IsWindowVisible()) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("окно ящика открыто игроком")); return; }
		struct FRun
		{
			int32 Ok = 0;                          // пройдено проверок
			int32 Fail = 0;                        // провалено проверок
			int32 Step = 0;                        // следующий шаг
			bool  bDone = false;                   // итог подведён — таймер ждёт снятия
			FTimerHandle H;                        // таймер шагов
			TArray<TFunction<void()>> Steps;       // шаги
		};
		TSharedRef<FRun> R = MakeShared<FRun>();
		TWeakObjectPtr<UWorld> WW(W);
		TWeakObjectPtr<UAsdaPremiumPostSubsystem> WP(P);
		const bool bQuit = Args.bQuit;
		auto Check = [R](bool bOk, const FString& What)
		{
			if (bOk) { ++R->Ok; } else { ++R->Fail; }
			UE_LOG(LogAsdaPost, Display, TEXT("САМОТЕСТ-ЯЩИК-СТРОКИ %s %s"), bOk ? TEXT("OK  ") : TEXT("FAIL"), *What);
		};
		// Тело 11005: CharIndex 0 (офлайн — сверка с выбранным персонажем пропускается) + записи (Order, Code, Count, Type).
		struct FR { int32 Order; int32 Code; int32 Count; };
		auto List = [](const TArray<FR>& Recs)
		{
			TArray<uint8> B;
			B.SetNumZeroed(4 + 10 * Recs.Num());
			for (int32 i = 0; i < Recs.Num(); ++i)
			{
				AsdaBytes::WrI32(&B[4 + 10 * i], Recs[i].Order);
				AsdaBytes::WrI32(&B[8 + 10 * i], Recs[i].Code);
				B[12 + 10 * i] = (uint8)Recs[i].Count;
				B[13 + 10 * i] = 1;
			}
			return B;
		};
		auto Reply = [](uint8 Status)
		{
			TArray<uint8> B;
			B.SetNumZeroed(409);
			B[0] = Status;
			B[408] = 1;
			return B;
		};
		auto Sent = [WP](int32 i, int32& Order, int32& Code, int32& Cnt)
		{
			UAsdaPremiumPostSubsystem* S = WP.Get();
			Order = Code = Cnt = -9;
			if (!S || !S->DebugTakeSent().IsValidIndex(i)) { return false; }
			const TArray<uint8>& B = S->DebugTakeSent()[i];
			Order = AsdaBytes::RdI32(&B[0]); Code = AsdaBytes::RdI32(&B[4]); Cnt = B[8];
			return true;
		};
		const TArray<FR> Five = {
			{ 100, 75701, 1 },
			{ 200, 75701, 1 }, { 200, 75701, 1 }, { 200, 75701, 1 },
			{ 300, 63103, 1 }, { 300, 71188, 1 }, { 300, 75701, 1 },
			{ 400, 62728, 1 }, { 400, 62728, 1 }, { 400, 62728, 1 }, { 400, 62728, 1 }, { 400, 62728, 1 }, { 400, 62728, 1 }, { 400, 62728, 1 },
			{ 500, 75702, 3 } };
		// ★ r2 (25.10, баг «купил 7 — в строке 6»): заказ 600 — 7 РАЗНЫХ позиций (коробки покупки владельца 10:36), порядок как у
		//   UPCashitemList (Orderidx, Code).
		TArray<FR> Six = Five;
		for (int32 C : { 76368, 76392, 76394, 76402, 76416, 79698, 79703 }) { Six.Add({ 600, C, 1 }); }
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		AsdaCVarSave::SetForTest(kKey, 1);
		P->DebugSetTakeCapture(true);
		P->DebugSetFreeCells(30, 60);

		// ── L: разбор ──
		R->Steps.Add([=]()
		{
			UAsdaPremiumPostSubsystem* S = WP.Get(); if (!S) { return; }
			S->SimulateList(List(Five));
			Check(S->GetFilledRowCount() == 5, FString::Printf(TEXT("L 15 записей пяти заказов → строк %d (5)"), S->GetFilledRowCount()));
			Check(S->DebugRowRecCount(1) == 3 && S->DebugRowCellCount(1) == 3,
				FString::Printf(TEXT("L корзина 3 × 75701 — одна строка: записей %d, ячеек %d"), S->DebugRowRecCount(1), S->DebugRowCellCount(1)));
			Check(S->DebugCellCode(2 * 7 + 0) == 63103 && S->DebugCellCode(2 * 7 + 1) == 71188 && S->DebugCellCode(2 * 7 + 2) == 75701,
				FString::Printf(TEXT("L смешанный заказ: ячейки %d / %d / %d"), S->DebugCellCode(14), S->DebugCellCode(15), S->DebugCellCode(16)));
			Check(S->DebugRowCellCount(3) == 7 && S->DebugRowCellCount(4) == 3 && S->DebugRowCellCount(0) == 1,
				FString::Printf(TEXT("L 7 штук — полная строка (%d), старая запись Count 3 — 3 ячейки (%d), одиночная — 1 (%d)"),
					S->DebugRowCellCount(3), S->DebugRowCellCount(4), S->DebugRowCellCount(0)));
		});
		// ── T1: забор корзины из 3 ──
		R->Steps.Add([=]()
		{
			UAsdaPremiumPostSubsystem* S = WP.Get(); if (!S) { return; }
			S->SelectRow(1);
			S->TakeSelected();
			int32 O = 0, C = 0, N = 0;
			const bool bSent = Sent(0, O, C, N);
			Check(bSent && O == 200 && C == 75701 && N == 1 && S->DebugTakeRunning() && S->DebugTakeSent().Num() == 1,
				FString::Printf(TEXT("T1 первая запись заказа 200: 11006 Order %d код %d ×%d"), O, C, N));
			S->DebugFeedApply(Reply(1));
		});
		R->Steps.Add([=]()
		{
			UAsdaPremiumPostSubsystem* S = WP.Get(); if (!S) { return; }
			int32 O = 0, C = 0, N = 0;
			Check(Sent(1, O, C, N) && O == 200 && C == 75701 && S->DebugTakeSent().Num() == 2,
				FString::Printf(TEXT("T1 после 11007 код 1 Tick послал вторую запись (11006 всего %d)"), S->DebugTakeSent().Num()));
			S->DebugFeedApply(Reply(1));
		});
		R->Steps.Add([=]()
		{
			UAsdaPremiumPostSubsystem* S = WP.Get(); if (!S) { return; }
			Check(S->DebugTakeSent().Num() == 3, FString::Printf(TEXT("T1 третья запись (11006 всего %d)"), S->DebugTakeSent().Num()));
			S->DebugFeedApply(Reply(1));
		});
		R->Steps.Add([=]()
		{
			UAsdaPremiumPostSubsystem* S = WP.Get(); if (!S) { return; }
			Check(!S->DebugTakeRunning() && S->DebugTakeSent().Num() == 3,
				FString::Printf(TEXT("T1 три 11007 — забор строки окончен, лишних 11006 нет (%d)"), S->DebugTakeSent().Num()));
			// T2: смешанная строка.
			S->DebugSetTakeCapture(true);
			S->SimulateList(List(Five));
			S->SelectRow(2);
			S->TakeSelected();
			S->DebugFeedApply(Reply(1));
		});
		R->Steps.Add([=]()
		{
			UAsdaPremiumPostSubsystem* S = WP.Get(); if (!S) { return; }
			S->DebugFeedApply(Reply(1));
		});
		R->Steps.Add([=]()
		{
			UAsdaPremiumPostSubsystem* S = WP.Get(); if (!S) { return; }
			int32 O0 = 0, C0 = 0, N0 = 0, O1 = 0, C1 = 0, N1 = 0, O2 = 0, C2 = 0, N2 = 0;
			const bool b = Sent(0, O0, C0, N0) && Sent(1, O1, C1, N1) && Sent(2, O2, C2, N2);
			Check(b && O0 == 300 && O1 == 300 && O2 == 300 && C0 == 63103 && C1 == 71188 && C2 == 75701,
				FString::Printf(TEXT("T2 смешанная строка: 11006 коды %d → %d → %d одного заказа"), C0, C1, C2));
			S->DebugFeedApply(Reply(1));
		});
		// ── T6 (r2): строка из 7 разных — одна строка, 7 ячеек, забор 7 записями ──
		R->Steps.Add([=]()
		{
			UAsdaPremiumPostSubsystem* S = WP.Get(); if (!S) { return; }
			S->DebugSetTakeCapture(true);
			S->SimulateList(List(Six));
			bool bCodes = S->DebugRowCellCount(5) == 7;
			const int32 Want[7] = { 76368, 76392, 76394, 76402, 76416, 79698, 79703 };
			for (int32 c = 0; c < 7; ++c) { bCodes &= S->DebugCellCode(5 * 7 + c) == Want[c]; }
			Check(S->GetFilledRowCount() == 6 && S->DebugRowRecCount(5) == 7 && bCodes,
				FString::Printf(TEXT("T6 7 разных позиций одного заказа — ОДНА строка, 7 ячеек по кодам (строк %d, записей %d, ячеек %d)"),
					S->GetFilledRowCount(), S->DebugRowRecCount(5), S->DebugRowCellCount(5)));
			S->SelectRow(5);
			S->TakeSelected();
			S->DebugFeedApply(Reply(1));
		});
		for (int32 k = 0; k < 6; ++k)
		{
			R->Steps.Add([=]()
			{
				UAsdaPremiumPostSubsystem* S = WP.Get(); if (!S) { return; }
				if (S->DebugTakeRunning()) { S->DebugFeedApply(Reply(1)); }
			});
		}
		R->Steps.Add([=]()
		{
			UAsdaPremiumPostSubsystem* S = WP.Get(); if (!S) { return; }
			bool bSeq = S->DebugTakeSent().Num() == 7;
			const int32 Want[7] = { 76368, 76392, 76394, 76402, 76416, 79698, 79703 };
			for (int32 i = 0; bSeq && i < 7; ++i) { int32 O = 0, C = 0, N = 0; bSeq &= Sent(i, O, C, N) && O == 600 && C == Want[i] && N == 1; }
			Check(bSeq && !S->DebugTakeRunning(),
				FString::Printf(TEXT("T6 забор строки из 7 разных: 7 × 11006 одного заказа по порядку ячеек (отправлено %d)"), S->DebugTakeSent().Num()));
		});
		// ── T3: мест не хватает ──
		R->Steps.Add([=]()
		{
			UAsdaPremiumPostSubsystem* S = WP.Get(); if (!S) { return; }
			Check(!S->DebugTakeRunning(), TEXT("T2 смешанная строка взята целиком"));
			S->DebugSetTakeCapture(true);
			S->DebugSetFreeCells(2, 60);
			S->SimulateList(List(Five));
			S->SelectRow(3);
			S->TakeSelected();
			Check(S->DebugTakeSent().Num() == 0 && !S->DebugTakeRunning() && !S->DebugInFlight(),
				FString::Printf(TEXT("T3 7 штук при 2 свободных ячейках: 11006 не ушёл (%d), строка осталась"), S->DebugTakeSent().Num()));
			// T4: отказ на первой записи.
			S->DebugSetFreeCells(30, 60);
			S->TakeSelected();
			S->DebugFeedApply(Reply(3));
		});
		R->Steps.Add([=]()
		{
			UAsdaPremiumPostSubsystem* S = WP.Get(); if (!S) { return; }
			Check(S->DebugTakeSent().Num() == 1 && !S->DebugTakeRunning(),
				FString::Printf(TEXT("T4 отказ 3 на первой записи: дальше не шлётся (11006 всего %d), забор окончен"), S->DebugTakeSent().Num()));
			// T5: ключ 0 — как оригинал.
			AsdaCVarSave::SetForTest(kKey, 0);
			S->SimulateList(List(Five));
			Check(S->GetFilledRowCount() == 7 && S->DebugRowRecCount(1) == 0 && S->DebugRowCellCount(1) == 1,
				FString::Printf(TEXT("T5 ключ 0: запись = строка — строк %d (7), у строки 1 ячеек %d"), S->GetFilledRowCount(), S->DebugRowCellCount(1)));
		});

		W->GetTimerManager().SetTimer(R->H, FTimerDelegate::CreateLambda([R, WW, WP, bQuit, List]()
		{
			if (R->bDone) { return; }
			if (R->Step < R->Steps.Num()) { R->Steps[R->Step++](); return; }
			R->bDone = true;
			if (UAsdaPremiumPostSubsystem* S = WP.Get())
			{
				S->DebugSetTakeCapture(false);
				S->DebugSetFreeCells(-1, -1);
				S->SimulateList(List({}));   // модель пуста (как до самотеста)
			}
			TArray<TFunction<void()>> Done = MoveTemp(R->Steps);
			Done.Reset();
			if (UWorld* Wd = WW.Get())
			{
				FTimerHandle H = R->H;
				TWeakObjectPtr<UWorld> W2 = WW;
				Wd->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([W2, H]() mutable
				{
					if (UWorld* W3 = W2.Get()) { W3->GetTimerManager().ClearTimer(H); }
				}));
			}
			AsdaSelfTestKit::Finish(WW.Get(), kCmd, bQuit, LogAsdaPost, TEXT("САМОТЕСТ-ЯЩИК-СТРОКИ"), R->Ok, R->Fail);
		}), 0.3f, true);
	}));
#endif // ASDA_DEV_CONSOLE
