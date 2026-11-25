// Copyright (c) 2026 @sitxovski (Discord)
// AsdaCharaInfoSubsystem_Debug.cpp — ★ R3-c 12.11.2026 (разрез AsdaCharaInfoSubsystem.cpp, дословный перенос): тексты/формат/локализация, нотисы, дампы и дев-команды.
//   Общее для частей — в AsdaCharaInfoSubsystem_Private.h; шапка и реверс — в AsdaCharaInfoSubsystem.cpp.
#include "UI/AsdaCharaInfoSubsystem.h"


#include "Widgets/Input/SButton.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "AsdaCharaInfoSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaCharaInfoSubsystem*.cpp

// ═══════════════════════════ УТИЛИТЫ ════════════════════════════════════════════════════════════
void UAsdaCharaInfoSubsystem::SetText(UTextBlock* T, const FString& S)
{
	if (!T) { return; }
	T->SetText(FText::FromString(S));
}

// ★ Снять стиль строки из JUI ДО первой подмены на rich (обоснование — в шапке `FAsdaFmtText`).
//   25.09.2026: тело переехало в общую точку разметки `UAsdaRichText::CaptureFmt` (окна 900/20000/50300).
void UAsdaCharaInfoSubsystem::CaptureFmt(FAsdaFmtText& F, UTextBlock* T)
{
	UAsdaRichText::CaptureFmt(F, T);   // в окне 900 перенос всегда false (строки 15 px)
}

// ★★★ ПОКАЗАТЬ СЫРОЙ ТЕКСТ С ТЕГАМИ. Эталон кладёт в контрол строку С разметкой
//   (`"<база> <FC=FFFF8000>+ N</F>"`), а разбирает её РЕНДЕР текста (`textParseColorTag@0x12C8700`).
//   В порту разбор один — `UAsdaRichText::ApplyFormatted`; зовём его ВНУТРИ этого метода и
//   ОБЯЗАТЕЛЬНО переприсваиваем `F.Node`: функция подменяет `UTextBlock` на `URichTextBlock`
//   в том же canvas-слоте и возвращает НОВЫЙ узел (закон `minopio_window.md`).
//   25.09.2026: тело переехало в общую точку разметки `UAsdaRichText::SetFmt` (окна 900/20000/50300).
void UAsdaCharaInfoSubsystem::SetTextFmt(FAsdaFmtText& F, const FString& Raw)
{
	UAsdaRichText::SetFmt(F, Raw);
}

// Эталонный формат «min ~ max» (`sprintf("%s ~ %s", …)`); одиночное число — просто число.
FString UAsdaCharaInfoSubsystem::RangeText(int32 Lo, int32 Hi)
{
	if (Lo == Hi) { return AsdaUi::FormatThousands(Lo); }
	return FString::Printf(TEXT("%s ~ %s"), *AsdaUi::FormatThousands(Lo), *AsdaUi::FormatThousands(Hi));
}

// Эталонный формат «база <FC=FFFF8000>+ N</F>» / «база <FC=FFFF8000>- N</F>» / «база».
FString UAsdaCharaInfoSubsystem::BaseBonusText(int32 Base, int32 Bonus)
{
	const FString B = AsdaUi::FormatThousands(Base);
	if (Bonus > 0) { return FString::Printf(TEXT("%s <FC=FFFF8000>+ %d</F>"), *B, Bonus); }
	if (Bonus < 0) { return FString::Printf(TEXT("%s <FC=FFFF8000>- %d</F>"), *B, -Bonus); }
	return B;
}

FString UAsdaCharaInfoSubsystem::Loc(int32 Id) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` — теги срезаны, маркеры как в таблице (как было).
	return AsdaNotice::Loc(GetGameInstance(), Id);
}

FString UAsdaCharaInfoSubsystem::LocTagged(int32 Id) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::LocTagged` (поведение то же).
	return AsdaNotice::LocTagged(GetGameInstance(), Id);
}

// Живой прямоугольник кнопки 988 + симулированный клик в его центр.
void UAsdaCharaInfoSubsystem::DebugClickBlog()
{
	FVector2D Mn = FVector2D::ZeroVector, Mx = FVector2D::ZeroVector;   // ★ R3-c чистка 12.11.2026: C4701 — без живого прямоугольника в журнал идут нули, не мусор
	const bool bRect = BlogButton && AsdaUi::WidgetAbsRect(BlogButton.Get(), Mn, Mx);
	const bool bRowVis = AsdaUi::IsShown(BlogRow);   // ★ волна В6 ремесла (МЕЛК-8): общая точка видимости узла
	UE_LOG(LogAsdaCharInfo, Log,
		TEXT("[характеристики] кнопка 988 «Мой блог»: узел=%s строка 989 видима=%d подпись=«%s» ")
		TEXT("живой прямоугольник=%s (%.0f,%.0f)-(%.0f,%.0f)"),
		BlogButton ? TEXT("есть") : TEXT("НЕТ"), bRowVis ? 1 : 0,
		BlogCaption ? *BlogCaption->GetText().ToString() : TEXT("-"),
		bRect ? TEXT("да") : TEXT("НЕТ"), Mn.X, Mn.Y, Mx.X, Mx.Y);
	if (bRect)
	{
		// Кнопка на экране — жмём НАСТОЯЩИМ кликом через общий симклик оконной подсистемы.
		if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
		{
			WM->SimulateClickAt((Mn + Mx) * 0.5, /*HoldFrames*/ 3);
		}
		return;
	}
	// ⚠ Прямоугольник пуст — значит узел за границей вьюпорта (в headless-прогоне PIE-вьюпорт
	//   меньше окна 900 и его нижняя часть обрезается). Тогда дёргаем ТОТ ЖЕ делегат кнопки,
	//   что и мышь (`UButton::OnClicked`), чтобы путь обработчика всё-таки был проверен.
	UE_LOG(LogAsdaCharInfo, Warning,
		TEXT("[характеристики] 988: живой прямоугольник пуст (узел за краем вьюпорта) — ")
		TEXT("жмём через собственный делегат кнопки OnClicked"));
	if (BlogButton) { BlogButton->OnClicked.Broadcast(); }
}

void UAsdaCharaInfoSubsystem::Notice(int32 LocId)
{
	// ★ Волна 1 (14.09.2026, В2): маршрут — фасад `AsdaNotice` (категория та же, поведение то же).
	if (AsdaNotice::Chat(GetGameInstance(), EAsdaMsgCategory::Tip, LocId)) { return; }
	UE_LOG(LogAsdaCharInfo, Log, TEXT("[характеристики] нотис %d: %s"), LocId, *Loc(LocId));
}

// Класс/подкласс: эталон берёт `CCharacter+0x438` (класс) и `+0x439` (подкласс) — в порту это
//   `SelfClassId` (тот же байт). Профессия = loc `12556 + значение`.
int32 UAsdaCharaInfoSubsystem::ResolveSelfClassId() const
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return -1; }
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U5d): класс — общая точка `AsdaSelf::GetClassId` (байт +0x438, фолбэк — запись char-select rec+24).
	//   Было: фолбэк на `SelfJob` — это ПРОФЕССИЯ (+0x445), другой байт.
	return AsdaSelf::GetClassId(Net);
}

FString UAsdaCharaInfoSubsystem::DumpState() const
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	FString S = FString::Printf(TEXT("[характеристики] окно %s, вкладка %d, страница %d\n"),
		IsWindowOpen() ? TEXT("ОТКРЫТО") : TEXT("закрыто"), CurTab, CurStatPage);
	if (Net)
	{
		const UAsdaNetSubsystem::FAsdaSelfStatBlock& B = Net->SelfStatBlock;
		const UAsdaNetSubsystem::FAsdaSelfBaseStats& P = Net->SelfBaseStats;
		S += FString::Printf(TEXT("  блок 68 б: %s | атака %d~%d, маг. атака %d~%d, защита %d~%d, ")
			TEXT("маг. защита %d, блок %d~%d\n"),
			B.bValid ? TEXT("есть") : TEXT("НЕ ПРИХОДИЛ"),
			B.PhysAtkMin, B.PhysAtkMax, B.MagAtkMin, B.MagAtkMax, B.DefMin, B.DefMax,
			B.MagDefense, B.BlockMin, B.BlockMax);
		S += FString::Printf(TEXT("  стихии атк: %d %d %d %d %d %d | защ: %d %d %d %d %d %d\n"),
			B.EleAtk[0], B.EleAtk[1], B.EleAtk[2], B.EleAtk[3], B.EleAtk[4], B.EleAtk[5],
			B.EleDef[0], B.EleDef[1], B.EleDef[2], B.EleDef[3], B.EleDef[4], B.EleDef[5]);
		S += FString::Printf(TEXT("  блок 36 б: %s | база %d %d %d %d %d %d | прибавка %d %d %d %d %d %d\n"),
			P.bValid ? TEXT("есть") : TEXT("НЕ ПРИХОДИЛ"),
			P.Base[0], P.Base[1], P.Base[2], P.Base[3], P.Base[4], P.Base[5],
			P.Bonus[0], P.Bonus[1], P.Bonus[2], P.Bonus[3], P.Bonus[4], P.Bonus[5]);
		S += FString::Printf(TEXT("  ОЗ %d/%d  ОМ %d/%d  вес %d/%d  уровень %d  ремесло %d\n"),
			LastHpCur, LastHpMax, LastMpCur, LastMpMax, Net->SelfWeightCur, Net->SelfWeightMax,
			(int32)Net->SelfLevel, Net->SelfCraftSkill);
	}
	// Текст строк с тегами — уже РАЗОБРАННЫЙ (в узле лежит `URichTextBlock`): так видно, что
	//   разметка не всплывает игроку (пункт 1 владельца).
	for (int32 i = 5; i < 11 && StatFmt.IsValidIndex(i); ++i)
	{
		if (URichTextBlock* R = Cast<URichTextBlock>(StatFmt[i].Node.Get()))
		{
			// `GetText` у rich-контрола возвращает UE-разметку `<a c=..>`, а игроку видны только
			//   глифы. Печатаем ОБЕ строки: сырьё эталона и то, во что оно превратилось.
			S += FString::Printf(TEXT("  строка ST%d: RICH | сырьё «%s» | разбор «%s» | видно «%s»\n"),
				i + 1, *StatFmt[i].LastRaw, *R->GetText().ToString(),
				*FAsdaMarkup::StripTags(StatFmt[i].LastRaw));
		}
		else if (UTextBlock* T = Cast<UTextBlock>(StatFmt[i].Node.Get()))
		{
			S += FString::Printf(TEXT("  строка ST%d: ПЛОСКИЙ «%s» — ТЕГИ НЕ РАЗОБРАНЫ\n"),
				i + 1, *T->GetText().ToString());
		}
	}
	// Фаза «сесть/сидит/встать» (эталонные номера состояний 7/8/9) — пункт 3 владельца.
	{
		UGameInstance* GI2 = GetGameInstance();
		UAsdaNetSubsystem* N2 = GI2 ? GI2->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaWorldSubsystem* Wd = GI2 ? GI2->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (N2 && Wd)
		{
			if (AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(Wd->ActorByUserIdx(AsdaSelf::GetUserIdxOr(GI2, INDEX_NONE))))
			{
				S += FString::Printf(TEXT("  поза: состояние=%d фаза сидения=%d\n"),
					(int32)Ch->AnimState, Ch->GetSitPhase());
			}
		}
	}
	S += DumpIntroState() + TEXT("\n");
	return S;
}

// ★★★ ЖИВАЯ ДИАГНОСТИКА ПЕРЕКЛЮЧАТЕЛЯ ПОД-ВКЛАДОК (дефект владельца: «кликается только третья»).
//   Печатает ВСЕ кнопки `CHARAINFO_STMENU_OPT*`, найденные в дереве (их там БОЛЬШЕ трёх — разметка
//   несёт ДВА одноимённых контрола 981), их слот-прямоугольники, живую геометрию и видимость,
//   а затем — САМОЕ ГЛАВНОЕ — спрашивает у Slate, КАКОЙ виджет реально лежит под центром каждой
//   кнопки (`FSlateApplication::LocateWidgetInWindow`). Если там не кнопка, значит клик крадёт
//   сосед сверху, и это видно одной строкой, а не догадками по дампу ассета.
FString UAsdaCharaInfoSubsystem::DumpTabProbe() const
{
	FString S = FString::Printf(TEXT("[под-вкладки] окно %s, страница %d, кнопок в дереве: %d, планов 2/3: %d/%d\n"),
		IsWindowOpen() ? TEXT("ОТКРЫТО") : TEXT("закрыто"), CurStatPage,
		StatTabButtons.Num(), StatTab2Planes.Num(), StatTab3Planes.Num());
	for (int32 i = 0; i < StatTabButtons.Num(); ++i)
	{
		const UButton* B = StatTabButtons[i].Get();
		if (!B) { S += FString::Printf(TEXT("  [%d] <нет>\n"), i); continue; }
		FVector2D P, Sz, Mn, Mx;
		const bool bSlot = AsdaUi::SlotRect(B, P, Sz);
		const bool bAbs  = AsdaUi::WidgetAbsRect(B, Mn, Mx);
		// ★ Адрес объекта печатается НЕ для красоты: именно он доказал, что «пятая кнопка» — это
		//   тот же виджет, что и первая (см. разбор у `AddUnique` в `ScanWidgets`).
		S += FString::Printf(TEXT("  [%d] %-28s объект=%p стр=%2d vis=%d слот=(%.0f,%.0f %.0fx%.0f) абс=(%.0f,%.0f %.0fx%.0f)%s\n"),
			i, *B->GetName(), (const void*)B, StatTabPageOf.IsValidIndex(i) ? StatTabPageOf[i] : -99,
			(int32)B->GetVisibility(),
			bSlot ? P.X : -1.f, bSlot ? P.Y : -1.f, bSlot ? Sz.X : -1.f, bSlot ? Sz.Y : -1.f,
			bAbs ? Mn.X : -1.f, bAbs ? Mn.Y : -1.f, bAbs ? (Mx.X - Mn.X) : -1.f, bAbs ? (Mx.Y - Mn.Y) : -1.f,
			B->GetIsEnabled() ? TEXT("") : TEXT(" [ВЫКЛЮЧЕНА]"));
	}
	// Кто лежит под центром каждой кнопки — прямой ответ Slate.
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication& App = FSlateApplication::Get();
		for (int32 i = 0; i < StatTabButtons.Num(); ++i)
		{
			const UButton* B = StatTabButtons[i].Get();
			if (!B || B->GetVisibility() == ESlateVisibility::Collapsed) { continue; }
			FVector2D Mn, Mx;
			if (!AsdaUi::WidgetAbsRect(B, Mn, Mx)) { continue; }
			const FVector2D C = (Mn + Mx) * 0.5f;
			FWidgetPath Path = App.LocateWindowUnderMouse(C, App.GetInteractiveTopLevelWindows());
			FString Leafs;
			for (int32 k = FMath::Max(0, Path.Widgets.Num() - 4); k < Path.Widgets.Num(); ++k)
			{
				Leafs += FString::Printf(TEXT(" / %s"), *Path.Widgets[k].Widget->GetTypeAsString());
			}
			S += FString::Printf(TEXT("  под центром [%d] (%.0f,%.0f) хвост пути:%s\n"), i, C.X, C.Y, *Leafs);
		}
	}
	return S;
}

#if !UE_BUILD_SHIPPING   // ★ античит А2 (12.10.2026): отладка самотестов/дев-команд — только вне Shipping
// ★★★ НАСТОЯЩИЙ КЛИК МЫШЬЮ по кнопке под-вкладки — для живой проверки в PIE.
//   Не зовёт обработчик напрямую: ставит курсор в центр кнопки и прогоняет через Slate нажатие и
//   отпускание. Значит проверяется ИМЕННО хит-тест (кто получит клик), а не наша привязка.
void UAsdaCharaInfoSubsystem::ClickStatTabAt(int32 Index)
{
	if (!FSlateApplication::IsInitialized() || !StatTabButtons.IsValidIndex(Index))
	{
		UE_LOG(LogAsdaCharInfo, Warning, TEXT("[под-вкладки] клик %d невозможен (кнопок %d)"), Index, StatTabButtons.Num());
		return;
	}
	UButton* B = StatTabButtons[Index].Get();
	FVector2D Mn, Mx;
	if (!B || !AsdaUi::WidgetAbsRect(B, Mn, Mx))
	{
		UE_LOG(LogAsdaCharInfo, Warning, TEXT("[под-вкладки] у кнопки %d нет живой геометрии"), Index);
		return;
	}
	const FVector2D C = (Mn + Mx) * 0.5f;
	FSlateApplication& App = FSlateApplication::Get();
	const int32 Before = CurStatPage;

	// ШАГ 1 — ХИТ-ТЕСТ. Спрашиваем Slate, какой виджет лежит под центром кнопки, и сверяем его с
	//   САМОЙ кнопкой. Именно это и было сломано: две из трёх кнопок оказывались свёрнутыми, и под
	//   их местом лежал кто угодно, только не они.
	//   ⚠ Синтетический `ProcessMouseButtonDownEvent` тут НЕ годится: `SetCursorPos` в headless
	//   реальный курсор не двигает, а Slate внутри берёт положение именно у него — клик уходил
	//   мимо. Поэтому хит-тест проверяем путём, а сам клик подаём кнопке напрямую.
	const TSharedPtr<SWidget> Self = B->GetCachedWidget();
	FWidgetPath Path = App.LocateWindowUnderMouse(C, App.GetInteractiveTopLevelWindows());
	const TSharedPtr<SWidget> Leaf = (Path.Widgets.Num() > 0)
		? TSharedPtr<SWidget>(Path.Widgets.Last().Widget) : nullptr;
	bool bTopmost = false;
	for (int32 k = 0; k < Path.Widgets.Num(); ++k)
	{
		if (Self.IsValid() && Path.Widgets[k].Widget == Self) { bTopmost = true; break; }
	}

	// ШАГ 2 — КЛИК. `SButton::SimulateClick` прогоняет ровно ту же цепочку `Press → ExecuteOnClick`,
	//   что и настоящее нажатие, включая наш делегат `OnClicked`.
	if (Leaf.IsValid() && Leaf->GetType() == TEXT("SButton"))
	{
		StaticCastSharedPtr<SButton>(Leaf)->SimulateClick();
	}
	UE_LOG(LogAsdaCharInfo, Log,
		TEXT("[под-вкладки] клик по кнопке %d («%s») в (%.0f,%.0f): под курсором %s, кнопка в пути %s; страница %d → %d %s"),
		Index, *B->GetName(), C.X, C.Y,
		Leaf.IsValid() ? *Leaf->GetType().ToString() : TEXT("НИЧЕГО"),
		bTopmost ? TEXT("ЕСТЬ") : TEXT("НЕТ — её перекрыли"),
		Before, CurStatPage,
		(CurStatPage != Before) ? TEXT("— СРАБОТАЛО") : TEXT("— БЕЗ ЭФФЕКТА"));
}
#endif // !UE_BUILD_SHIPPING

// ═══════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ (живая проверка) ════════════════════════════════
static UAsdaCharaInfoSubsystem* CiGet(UWorld* W)
{
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UAsdaCharaInfoSubsystem>() : nullptr;
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCiOpen(
	TEXT("asda.CharInfo.Open"), TEXT("Открыть окно характеристик (CHARAINFO_WIN 900)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{ if (UAsdaCharaInfoSubsystem* S = CiGet(W)) { S->OpenWindow(); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCiClose(
	TEXT("asda.CharInfo.Close"), TEXT("Закрыть окно характеристик."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{ if (UAsdaCharaInfoSubsystem* S = CiGet(W)) { S->CloseWindow(); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCiTab(
	TEXT("asda.CharInfo.Tab"), TEXT("asda.CharInfo.Tab <0|1> — тумблер вкладки: 0 статы (клавиша C), 1 эмоции (E)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{ if (UAsdaCharaInfoSubsystem* S = CiGet(W)) { S->ToggleTab(A.Num() > 0 ? FCString::Atoi(*A[0]) : 0); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCiPage(
	TEXT("asda.CharInfo.Page"), TEXT("asda.CharInfo.Page <0..2> — под-страница: 0 Информация, 1 Параметры, 2 Характеристики."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{ if (UAsdaCharaInfoSubsystem* S = CiGet(W)) { S->SelectStatPage(A.Num() > 0 ? FCString::Atoi(*A[0]) : 0); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCiRefresh(
	TEXT("asda.CharInfo.Refresh"), TEXT("Перерисовать всё содержимое окна характеристик."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{ if (UAsdaCharaInfoSubsystem* S = CiGet(W)) { S->RefreshAll(); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCiEmoteDrop(
	TEXT("asda.CharInfo.EmoteDrop"),
	TEXT("asda.CharInfo.EmoteDrop <код эмоции> [абс.слот] — уложить эмоцию в слот панели (порт msg 7 окна эмоций). ")
	TEXT("Без второго аргумента слот берётся хит-тестом под курсором."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (UAsdaCharaInfoSubsystem* S = CiGet(W))
		{
			const int32 Code = A.Num() > 0 ? FCString::Atoi(*A[0]) : 0;
			const int32 Abs  = A.Num() > 1 ? FCString::Atoi(*A[1]) : -1;
			S->DropEmotionToHotbar(Code, Abs);
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCiTabProbe(
	TEXT("asda.CharInfo.TabProbe"), TEXT("Диагностика переключателя под-вкладок 981: все кнопки, их геометрия и кто под ними по мнению Slate."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{ if (UAsdaCharaInfoSubsystem* S = CiGet(W)) { UE_LOG(LogAsdaCharInfo, Log, TEXT("\n%s"), *S->DumpTabProbe()); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCiTabClick(
	TEXT("asda.CharInfo.TabClick"), TEXT("asda.CharInfo.TabClick <индекс кнопки> — НАСТОЯЩИЙ клик мышью по кнопке под-вкладки (проверка хит-теста)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: синтетический клик по кнопке под точкой — перекрытое окно получило бы нажатие своей боевой кнопки.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.CharInfo.TabClick"))) { return; }
		if (UAsdaCharaInfoSubsystem* S = CiGet(W)) { S->ClickStatTabAt(A.Num() > 0 ? FCString::Atoi(*A[0]) : 0); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCiDump(
	TEXT("asda.CharInfo.Dump"), TEXT("Состояние окна характеристик и оба серверных стат-блока."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{ if (UAsdaCharaInfoSubsystem* S = CiGet(W)) { UE_LOG(LogAsdaCharInfo, Log, TEXT("\n%s"), *S->DumpState()); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCiWidgets(
	TEXT("asda.CharInfo.Widgets"),
	TEXT("Живые виджеты окна 900: класс узла (rich/плоский), видимость, текст, цвет подписей под-вкладок."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{ if (UAsdaCharaInfoSubsystem* S = CiGet(W)) { UE_LOG(LogAsdaCharInfo, Log, TEXT("\n%s"), *S->DumpWidgets()); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★ ЖМЁМ САМУ КНОПКУ 988 НАСТОЯЩИМ КЛИКОМ (закон «диагностика обязана жать саму кнопку»):
//   считаем её ЖИВОЙ прямоугольник и заказываем симулированный клик в его центр через
//   `UAsdaWindowSubsystem::SimulateClickAt` — то есть проверяем и видимость, и хит-тест.
static FAutoConsoleCommandWithWorldAndArgs GCiBlogClick(
	TEXT("asda.CharInfo.BlogClick"),
	TEXT("Настоящий клик по кнопке 988 «Мой блог» окна 900 (по живой геометрии)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: синтетический клик по экранным координатам — может нажать боевую кнопку чужого окна.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.CharInfo.BlogClick"))) { return; }
		if (UAsdaCharaInfoSubsystem* S = CiGet(W)) { S->DebugClickBlog(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCiIntro(
	TEXT("asda.CharInfo.Intro"), TEXT("Открыть окно «Обо мне» (My_Introduction_Win 8300) — то же, что кнопка 987."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{ if (UAsdaCharaInfoSubsystem* S = CiGet(W)) { S->OpenIntroWindow(); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCiIntroSet(
	TEXT("asda.CharInfo.IntroSet"),
	TEXT("asda.CharInfo.IntroSet <текст> — положить текст в поле 8302 и отправить пакет 6080 (кнопка «ОК»)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (UAsdaCharaInfoSubsystem* S = CiGet(W))
		{
			S->SetIntroFieldText(FString::Join(A, TEXT(" ")));
			S->SubmitIntroText();
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCiIntroDump(
	TEXT("asda.CharInfo.IntroDump"), TEXT("Состояние окна «Обо мне» 8300."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{ if (UAsdaCharaInfoSubsystem* S = CiGet(W)) { UE_LOG(LogAsdaCharInfo, Log, TEXT("%s"), *S->DumpIntroState()); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCiEmote(
	TEXT("asda.CharInfo.Emote"),
	TEXT("asda.CharInfo.Emote <код> — проиграть эмоцию (0..11 обычные, 100..105 отряд, 200..211 функции)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{ if (UAsdaCharaInfoSubsystem* S = CiGet(W)) { S->PlayEmotion(A.Num() > 0 ? FCString::Atoi(*A[0]) : 0); } }));
#endif // ASDA_DEV_CONSOLE
