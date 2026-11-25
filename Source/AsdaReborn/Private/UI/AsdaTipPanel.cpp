// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaTipPanel — реализация. Разбор эталона — в шапке AsdaTipPanel.h.
#include "UI/AsdaTipPanel.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMarkup.h"
#include "UI/AsdaUiHelpers.h"

#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/RichTextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "HAL/IConsoleManager.h"
#include "UObject/Class.h"        // StaticEnum<EAsdaTipOwner> для диагностических строк

DEFINE_LOG_CATEGORY_STATIC(LogAsdaTip, Log, All);

// ★ ОТКАТ (2026-08-09): 0 — высоту плашки в режиме ByContent брать по ФАКТИЧЕСКОМУ desired size
//   текстового виджета (после того как текст уже уложен с реальным переносом); 1 — по прежней
//   оценке «строк × высота строки», где строки считались нашим измерителем.
//   Оценка расходилась с рендером (иной шрифт базового стиля), из-за чего тело вылезало за рамку
//   и наезжало на «элитный» подвал.
static ASDA_DEV_CVAR(int32, CVarTipLegacyEstimateHeight,
	TEXT("asda.ItemTip.LegacyEstimateHeight"), 0,
	TEXT("1 = высота плашки по оценке числа строк (прежнее поведение), 0 = по фактической высоте текста."),
	ECVF_Default);

// ★★★ РАМКА ПОДСКАЗКИ ПО СОДЕРЖИМОМУ (требование владельца 2026-08-23: «тултипы имеют фиксированную
//   рамку, а должна быть динамическая под контент; на кадре высота слишком большая под пустоту,
//   ширина должна быть под контент»).
//
// ЧТО ДЕЛАЛ ЭТАЛОН (`tutorialPopupLayoutSmall@0xEB6CE0`, item_inventory.c:61991 — формула ОДНА на
//   все «простые» подсказки, `tutorialPopupLayoutLarge` отличается только константой подвала):
//       w = clamp(textFontMeasureMarkupString(ТЕЛО), 90, 218) + 38
//       n = textFontCountWrappedLines_Active(ТЕЛО, w − 12)
//       h = интервал·(n+1) + глиф·(n+2) + 12
//   ⇒ у ОРИГИНАЛА ширина считается ТОЛЬКО по телу и НЕ МЕНЬШЕ 90 + 38 = 128 px, а высота — не
//   меньше 3·2 + 12·3 + 12 = 54 px, СКОЛЬКО БЫ ни было текста. Именно это владелец и видит как
//   «маленький текст в непропорционально большой рамке»: у подсказки без тела (эмоция, строка
//   меню, кнопка панели) тело пустое, ширину задаёт нижний кламп 90, а высота считается по
//   «пустой» строке. То есть это ДЕФЕКТ ОРИГИНАЛА, а не наша регрессия.
//
// ЧТО ДЕЛАЕМ (закон `fix-original-defects` — дефекты оригинала исправляем и помечаем):
//   1) в измерение ширины входит и ЗАГОЛОВОК: у половины подсказок порта весь текст лежит именно
//      в нём (эмоции, простые строки), и рамка, посчитанная по пустому телу, к нему не относилась;
//   2) нижний кламп ширины снимается (0 вместо 90) — рамка облегает содержимое;
//   3) верхний кламп/жёсткая ширина остаются: они задают ШИРИНУ ПЕРЕНОСА, и трогать их нельзя;
//      жёсткая ширина (`FixedWidth`, у тултипа предмета 250) становится ПОТОЛКОМ, а не догмой —
//      сузить можно только до самой длинной строки, поэтому НИ ОДНА строка не переносится иначе,
//      чем переносилась бы при 250;
//   4) высота: пустое тело больше не занимает строку — плашка кончается сразу под заголовком;
//      пустой заголовок — тело поднимается к верхней кромке.
//   КЛЮЧ ОТКАТА: `asda.ItemTip.HugContent 0` возвращает эталонные клампы 1:1.
static ASDA_DEV_CVAR(int32, CVarTipHugContent,
	TEXT("asda.ItemTip.HugContent"), 1,
	TEXT("1 = рамка подсказки облегает содержимое (заголовок учитывается, нижний кламп ширины снят, ")
	TEXT("пустое тело не занимает строку); 0 = эталонные клампы `tutorialPopupLayoutSmall` (мин. 128x54)."),
	ECVF_Default);

// ★ Волна 1 (14.09.2026, В3): правило видимости — общая точка `AsdaUi::SetShownDecor` (режим тот же).
static void TipSetVis(UWidget* W, bool bVisible)
{
	AsdaUi::SetShownDecor(W, bVisible);
}

bool UAsdaTipPanel::Bind(UGameInstance* GI, FName WindowId, const TCHAR* WbpPath, int32 ZOrder,
                         const TCHAR* TitleName, const TCHAR* BodyName, const TCHAR* PlanePrefix,
                         const TArray<FString>& HideOtherPrefixes)
{
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }

	// ★★★ ПОЧЕМУ ЗДЕСЬ ПРОВЕРКА «ОКНО ЕЩЁ ЖИВО», А НЕ ПРОСТО `if (Widget) return true;`
	//   Плашка тултипа в порту ОДНА и живёт в реестре `UAsdaWindowSubsystem` под id `ITEM_TIPWIN`.
	//   Любой `CloseAll()` (снос дерева окон; его делает, например,
	//   вход в сцену премиум-гачи — шаг 5 `premiumGacha_BeginRoll`, world_map.c:4690) вызывает
	//   `RemoveFromParent()` у ВСЕХ окон и чистит реестр. Наш `Widget` при этом остаётся ненулевым,
	//   прежний `if (Widget) return true;` считал привязку действительной, `Show()` честно проставлял
	//   текст и размер — а на экране не было НИЧЕГО и НИ ОДНОЙ ошибки в логе.
	//   Симптом владельца «тултипов у предметов нет» после входа в сцену гачи — ровно это.
	//   ⇒ окно, выброшенное из реестра, открываем заново и перепривязываем все узлы (включая подвал).
	if (Widget && WM->IsWindowLive(Widget)) { return true; }
	const bool bRebind = (Widget != nullptr);
	if (bRebind)
	{
		UE_LOG(LogAsdaTip, Log,
			TEXT("[тултип] окно '%s' было выброшено из реестра (CloseAll) — привязываемся заново"),
			*WindowId.ToString());
		Widget = nullptr; TitleW = nullptr; BodyW = nullptr;
		Planes.Reset(); FooterPlanes.Reset();
		PremiumIcon = nullptr; FooterText = nullptr;
		bShown = false;
		Owner = EAsdaTipOwner::None;
	}
	// Запоминаем параметры привязки — они нужны для повторной привязки после следующего `CloseAll`.
	BoundWindowId = WindowId;
	BoundWbp = WbpPath;
	BoundZ = ZOrder;
	BoundTitleName = TitleName;
	BoundBodyName = BodyName;
	BoundPlanePfx = PlanePrefix;
	BoundHidePfx = HideOtherPrefixes;

	// ★ Окно берётся из реестра оконной подсистемы по WindowId: второй вызов с тем же id вернёт
	//   ТОТ ЖЕ виджет — ровно как единственный `g_ItemTooltip` эталона.
	Widget = WM->OpenWindowByPath(WindowId, WbpPath, ZOrder, ESlateVisibility::SelfHitTestInvisible);
	if (!Widget || !Widget->WidgetTree)
	{
		UE_LOG(LogAsdaTip, Warning, TEXT("[тултип] окно '%s' не загрузилось (%s)"), *WindowId.ToString(), WbpPath);
		Widget = nullptr;
		return false;
	}
	Widget->SetVisibility(ESlateVisibility::Collapsed);

	TitleW = Widget->GetWidgetFromName(FName(TitleName));
	if (UTextBlock* T0 = Cast<UTextBlock>(TitleW)) { TitleFont = T0->GetFont(); }
	Planes.Reset();
	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);
	const FString PlanePfx(PlanePrefix);
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		if (UImage* Im = Cast<UImage>(W))
		{
			if (Im->GetName().StartsWith(PlanePfx)) { Planes.Add(Im); continue; }
		}
		for (const FString& P : HideOtherPrefixes)
		{
			if (W->GetName().StartsWith(P)) { W->SetVisibility(ESlateVisibility::Collapsed); break; }
		}
	}

	// ★ Узла тела в WBP НЕТ: в .JUI у ITEM_TIPCONTENS/ItemCompareTip_Contents Size="36 0",
	//   а генератор создаёт Text-контрол только при w>0 && h>0. Создаём его в рантайме.
	//   Рецепт 1:1 с чатом: ОБЫЧНЫЙ UTextBlock через WidgetTree → отдать в `ApplyFormatted`
	//   (ветка «Already» у готового Rich не назначает style set/декоратор — теги молча теряются).
	BodyW = Widget->GetWidgetFromName(FName(BodyName));
	if (!BodyW)
	{
		if (UCanvasPanel* Root = Cast<UCanvasPanel>(Widget->WidgetTree->RootWidget))
		{
			UTextBlock* C = Widget->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(BodyName));
			C->SetAutoWrapText(true);
			C->SetJustification(ETextJustify::Left);          // JUI TextAlign = 0 у тела (у заголовка 5 = центр)
			C->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			if (UTextBlock* T0 = Cast<UTextBlock>(TitleW)) { C->SetFont(T0->GetFont()); }
			if (UCanvasPanelSlot* CS = Root->AddChildToCanvas(C))
			{
				CS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
				CS->SetAlignment(FVector2D(0.f, 0.f));
				CS->SetOffsets(FMargin(kBodyInsetX, kBodyTop, 120.f, 60.f));
			}
			BodyW = C;
		}
	}
	UE_LOG(LogAsdaTip, Log, TEXT("[тултип] '%s' привязан%s: заголовок=%d тело=%d планов рамки=%d"),
		*WindowId.ToString(), bRebind ? TEXT(" ЗАНОВО") : TEXT(""),
		TitleW != nullptr, BodyW != nullptr, Planes.Num());
	// Подвал был привязан до `CloseAll` — восстанавливаем его теми же именами, иначе «элитная»
	//   нижняя плашка (5005/5006/5007) молча пропадёт навсегда после первого закрытия всех окон.
	if (bRebind && bFooterBound)
	{
		// ⚠ КОПИИ обязательны: `BindFooter` присваивает эти же поля из переданных указателей,
		//   а указатель в собственный буфер FString при присваивании — обращение к освобождённой памяти.
		const FString I = FooterIconIdc, P = FooterPlaneIdc, T = FooterTextIdc;
		BindFooter(*I, *P, *T);
	}
	return true;
}

void UAsdaTipPanel::BindFooter(const TCHAR* PremiumIconName, const TCHAR* FooterPlanePrefix, const TCHAR* FooterTextName)
{
	if (!Widget || !Widget->WidgetTree) { return; }
	// Запоминаем имена — после `CloseAll` `Bind` перепривяжет подвал сам (см. разбор там).
	FooterIconIdc  = PremiumIconName;
	FooterPlaneIdc = FooterPlanePrefix;
	FooterTextIdc  = FooterTextName;
	bFooterBound = true;
	// Имена узлов импортёра: интерактив/Text = точное имя idc, планы = «idc_<idx>_N».
	//   Поэтому ищем точное имя, а если его нет — по вхождению (общий хелпер AsdaUi::FindNodeByName).
	PremiumIcon = AsdaUi::FindNodeByName(Widget, PremiumIconName, PremiumIconName);
	FooterText  = AsdaUi::FindNodeByName(Widget, FooterTextName, FooterTextName);
	FooterPlanes.Reset();
	const FString Pfx(FooterPlanePrefix);
	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (UImage* Im = Cast<UImage>(W))
		{
			if (Im->GetName().StartsWith(Pfx)) { FooterPlanes.Add(Im); }
		}
	}
	UE_LOG(LogAsdaTip, Log, TEXT("[тултип] подвал: значок=%d планов=%d текст=%d"),
		PremiumIcon != nullptr, FooterPlanes.Num(), FooterText != nullptr);
}

void UAsdaTipPanel::Resize9Slice(const TArray<TObjectPtr<UImage>>& Nine, const FVector2D& NewSize,
	const FVector2D& Origin)
{
	if (Nine.Num() != 9) { return; }
	UCanvasPanelSlot* S[9];
	for (int32 i = 0; i < 9; ++i)
	{
		S[i] = Nine[i] ? AsdaUi::SlotOf(Nine[i]) : nullptr;
		// ★ Центр (4) необязателен: у скинов-обводок он объявлен `PlaneAlwaysInvisible = "1"` и
		//   импортёр его не создаёт. Остальные восемь обязательны — без любого из них рамка
		//   вышла бы разорванной, и лучше не трогать НИЧЕГО (см. комментарий в заголовке).
		if (!S[i] && i != 4) { return; }
	}
	const float LW = S[0]->GetOffsets().Right;                 // ширина левой колонки
	const float RW = S[2]->GetOffsets().Right;                 // ширина правой колонки
	const float TH = S[0]->GetOffsets().Bottom;                // высота верхней строки
	const float BH = S[6]->GetOffsets().Bottom;                // высота нижней строки
	const float CW = FMath::Max(NewSize.X - LW - RW, 1.f);
	const float CH = FMath::Max(NewSize.Y - TH - BH, 1.f);
	const float Xs[3] = { Origin.X, Origin.X + LW, Origin.X + LW + CW };
	const float Ws[3] = { LW, CW, RW };
	const float Ys[3] = { Origin.Y, Origin.Y + TH, Origin.Y + TH + CH };
	const float Hs[3] = { TH, CH, BH };
	// ★ 09.11.2026 (пачка 19, баг 4): общая точка — перекрытие кусков из ассета (шаги швов пост-импорта) сохраняется; прежде
	//   рамка подсказки и окон разговора с NPC ложилась встык, и на дробной позиции между кусками был просвет 1 px.
	AsdaUi::Relayout9KeepOverlap(S, Xs, Ws, Ys, Hs);
}

int32 UAsdaTipPanel::CountWrappedLines(const FString& Plain, const FSlateFontInfo& Fnt, float WrapW)
{
	TArray<FString> Paras;
	Plain.ParseIntoArray(Paras, TEXT("\n"), /*CullEmpty=*/false);
	if (Paras.Num() == 0) { return 1; }
	if (!FSlateApplication::IsInitialized() || WrapW <= 1.f) { return FMath::Max(Paras.Num(), 1); }

	const TSharedRef<FSlateFontMeasure> FM = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	int32 Total = 0;
	for (const FString& Para : Paras)
	{
		TArray<FString> Words;
		Para.ParseIntoArrayWS(Words);
		if (Words.Num() == 0) { ++Total; continue; }      // пустая строка тоже занимает строку
		int32 LinesHere = 1;
		FString Cur;
		for (const FString& W : Words)
		{
			const FString Try = Cur.IsEmpty() ? W : (Cur + TEXT(" ") + W);
			if (Cur.IsEmpty() || (float)FM->Measure(Try, Fnt, 1.f).X <= WrapW) { Cur = Try; }
			else { ++LinesHere; Cur = W; }
		}
		Total += LinesHere;
	}
	return FMath::Max(Total, 1);
}

void UAsdaTipPanel::Show(const FString& Title, uint32 TitleArgb, const FString& Body,
                         const FAsdaTipMetrics& M)
{
	if (!Widget) { return; }

	// Строки данных приходят с ЛИТЕРАЛЬНЫМ «\n» (два символа) — переводим так же, как ToRichMarkup.
	const FString BodyPlain = FAsdaMarkup::StripTags(Body).Replace(TEXT("\\n"), TEXT("\n"));

	const bool bHug = (CVarTipHugContent.GetValueOnGameThread() != 0);
	// Заголовок тоже содержимое: у подсказок без тела (эмоция, строка меню, кнопка панели) ВЕСЬ
	//   текст лежит именно в нём (разбор — в шапке `CVarTipHugContent`).
	const FString TitlePlain = FAsdaMarkup::StripTags(Title).Replace(TEXT("\\n"), TEXT("\n")).TrimStartAndEnd();

	float TextW = M.MinW, TitleTextW = 0.f, LineH = 16.f;
	FSlateFontInfo Fnt;
	if (FSlateApplication::IsInitialized())
	{
		const TSharedRef<FSlateFontMeasure> FM = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		// Меряем шрифтом ЗАГОЛОВКА (тело — Rich, у него шрифт из style set) и РОВНО тем кеглем,
		//   каким рисуем (<FS##>), иначе счёт строк расходится с рендером.
		Fnt = TitleFont;
		Fnt.Size = M.FontPx;
		LineH = AsdaUi::MeasureLineHeight(Fnt, 16.f);
		float MaxW = 0.f;
		FString Rest = BodyPlain;
		while (!Rest.IsEmpty())
		{
			FString L, R2;
			if (!Rest.Split(TEXT("\n"), &L, &R2)) { L = Rest; R2.Empty(); }
			MaxW = FMath::Max(MaxW, (float)FM->Measure(L.TrimStartAndEnd(), Fnt, 1.f).X);
			Rest = R2;
		}
		if (MaxW > 1.f) { TextW = MaxW; }
		if (!TitlePlain.IsEmpty())
		{
			// ⚠ Заголовок рисуется ПСЕВДО-жирным (эталон `CClass_SetField926(5001, 0x200000)`):
			//   второй проход того же глифа в `penX±1`, продвижение пера НЕ меняется
			//   (★ 2026-09-24, разбор — `AsdaUi::ApplyPseudoBold`; до этого тут стояла Bold-гарнитура,
			//   которая и мерилась, и рисовалась ШИРЕ эталона). Меряем обычным шрифтом и добавляем
			//   1 px — ровно ширину второго прохода, который вылезает за последний глиф.
			TitleTextW = (float)FM->Measure(TitlePlain, Fnt, 1.f).X + 1.f;
		}
	}
	const bool bBodyEmpty  = BodyPlain.TrimStartAndEnd().IsEmpty();
	const bool bTitleEmpty = TitlePlain.IsEmpty();

	// ── ШИРИНА ─────────────────────────────────────────────────────────────────────────────────
	//   Эталон: `clamp(ширинаТела, 90, 218) + 38`, либо жёсткие 250 у тултипа предмета.
	//   Режим «по содержимому»: в счёт идёт и заголовок, нижний кламп снят, а жёсткая ширина и
	//   верхний кламп работают ПОТОЛКОМ (они же — ширина переноса, см. шапку `CVarTipHugContent`).
	float PanelW;
	if (!bHug)
	{
		PanelW = (M.FixedWidth > 0.f) ? M.FixedWidth : (FMath::Clamp(TextW, M.MinW, M.MaxW) + M.WidthAdd);
	}
	else
	{
		const float Ceil    = (M.FixedWidth > 0.f) ? FMath::Max(M.FixedWidth - M.WidthAdd, 16.f) : M.MaxW;
		const float Content = FMath::Max(bBodyEmpty ? 0.f : TextW, TitleTextW);
		PanelW = FMath::Clamp(Content, 16.f, Ceil) + M.WidthAdd;
	}
	// Ширина переноса = ширина слота тела (JUI: отступ 8 слева и справа). Эталон считает строки по
	//   `ширинаПанели − 12`; у нас −16 (наш отступ), зато счёт согласован с фактическим рендером.
	const float BodyW_ = FMath::Max(PanelW - 2.f * kBodyInsetX, 16.f);
	const int32 Wrapped = CountWrappedLines(BodyPlain, Fnt, BodyW_);
	const int32 N = Wrapped + M.ExtraLines;   // эталонное «строк + 2»

	// ★★★ БАЗОВЫЕ СТИЛИ ОБОИХ КОНТРОЛОВ (2026-08-09).
	//   Раньше кегль и цвет подмешивались ТЕГАМИ («<FS08><FC=FFFFFFFF>» + реставрация после каждого
	//   `</F>`). Теги работают, но всё НЕформатированное (и всё после `</F>`, если реставрацию
	//   пропустить) рисовалось строкой `Default` таблицы DT_AsdaRichStyle — AsdaUIFontRT 10 px белым.
	//   Правильный способ — задать rich-контролу его СОБСТВЕННЫЙ базовый стиль: тогда `</F>`
	//   возвращает ровно к нему, а измеритель и рендер работают ОДНИМ шрифтом.
	//   Тело: JUI `ITEM_TIPCONTENS` — TextColor = FFFFFFFF, TextAlign = 0 (влево), перенос ЕСТЬ.
	//   Заголовок: JUI `ITEM_TIPTITLE` — TextAlign = 5 (центр), цвет ставит `CControl_SetField923`
	//   (цвет грейда), а `CClass_SetField926(5001, 0x200000)` = ЖИРНЫЙ.
	//   ⚠ Шрифт заголовка снят при `Bind`; если узла-заголовка в WBP не оказалось, шрифта у нас нет —
	//   тогда базовый стиль НЕ навязываем (пусть работает `Default` таблицы), иначе текст пропадёт.
	const bool bHaveFont = (Fnt.FontObject != nullptr);
	FSlateFontInfo BodyFont = Fnt;   BodyFont.Size = M.FontPx;
	FSlateFontInfo TitleFnt = Fnt;   TitleFnt.Size = M.FontPx;
	// ★ 2026-09-24: гарнитуру НЕ меняем — «жирный» заголовка даём вторым проходом по горизонтали
	//   цветом самого заголовка (см. ниже, после того как цвет посчитан).

	FTextBlockStyle BodyStyle;
	BodyStyle.SetFont(BodyFont);
	BodyStyle.SetColorAndOpacity(FSlateColor(FLinearColor::White));

	FTextBlockStyle TitleStyle;
	TitleStyle.SetFont(TitleFnt);
	const FLinearColor TitleCol = AsdaUi::SrgbU32ToLinear(TitleArgb);
	TitleStyle.SetColorAndOpacity(FSlateColor(TitleCol));
	{
		// ПСЕВДО-ЖИРНЫЙ заголовка (флаг 926 = 0x200000): второй проход глифа цветом заливки.
		FVector2D PbOff; FLinearColor PbCol;
		AsdaUi::PseudoBoldShadow(TitleCol, PbOff, PbCol);
		TitleStyle.SetShadowOffset(PbOff);
		TitleStyle.SetShadowColorAndOpacity(PbCol);
	}

	// ── ВЕРХ ТЕЛА ──────────────────────────────────────────────────────────────────────────────
	//   Эталон держит его константой (20 = заголовок y=2 h=18). В режиме «по содержимому» пустой
	//   заголовок больше не резервирует эти 20 px — тело поднимается к верхней кромке.
	const float BodyTop = (bHug && bTitleEmpty) ? kBodyBottom : kBodyTop;

	// ── ТЕЛО: сперва текст (чтобы узнать ФАКТИЧЕСКУЮ высоту), потом размер плашки ──
	float BodyDesiredH = 0.f;
	if (BodyW)
	{
		if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(BodyW))
		{
			CS->SetAutoSize(false);
			// Высота слота пока предварительная — уточним ниже, когда посчитаем PanelH.
			CS->SetOffsets(FMargin(kBodyInsetX, BodyTop, BodyW_, FMath::Max((float)Wrapped * LineH, 8.f)));
		}
		FAsdaRichApplyOpts Opts;
		Opts.BaseStyle   = bHaveFont ? &BodyStyle : nullptr;
		Opts.bWrap       = true;                      // тело МНОГОстрочное — перенос обязателен
		Opts.WrapAt      = BodyW_;
		Opts.bSetJustify = true;
		Opts.Justify     = ETextJustify::Left;        // JUI ITEM_TIPCONTENS TextAlign = 0
		if (UWidget* Rich = UAsdaRichText::ApplyFormatted(BodyW, Body, &Opts)) { BodyW = Rich; }
		else
		{
			UE_LOG(LogAsdaTip, Error, TEXT("[тултип] ApplyFormatted вернул nullptr для узла '%s'"), *BodyW->GetName());
		}
		TipSetVis(BodyW, true);
		// Фактическая высота уложенного текста (перенос уже применён). Пока виджет ни разу не
		//   рисовался, desired size = 0 — тогда остаётся оценка.
		if (BodyW)
		{
			BodyW->ForceLayoutPrepass();
			BodyDesiredH = (float)BodyW->GetDesiredSize().Y;
		}
	}

	float PanelH = 0.f;
	switch (M.HeightMode)
	{
	case EAsdaTipHeightMode::ItemLegacy:
		// ItemTooltip_Build: h = глиф·n + интервал·(n−1) + 15; окно ставится в h + 12.
		PanelH = M.LegacyLineHeight * (float)N + M.LegacyLineGap * (float)(N - 1) + 15.f + 12.f;
		break;
	case EAsdaTipHeightMode::BuffLegacy:
		PanelH = 16.f * (float)(Wrapped + 1) + 12.f;
		break;
	case EAsdaTipHeightMode::PlusPackLegacy:
		PanelH = M.LegacyLineGap * (float)(Wrapped + 1) + M.LegacyLineHeight * (float)(Wrapped + 2) + M.LegacyChromeH;
		break;
	case EAsdaTipHeightMode::ByContent:
	default:
	{
		// ★ Пустое тело в режиме «по содержимому» строки НЕ занимает: `CountWrappedLines` честно
		//   отдаёт 1 даже на пустой строке (так считает и эталон), и раньше эта «строка пустоты»
		//   добавляла к рамке целую высоту глифа — ровно то, что владелец увидел на кадре.
		if (bHug && bBodyEmpty)
		{
			PanelH = (bTitleEmpty ? 0.f : (2.f + LineH + 2.f)) + kBodyBottom;
			break;
		}
		const float Estimate = (float)Wrapped * LineH;
		const bool bLegacy = CVarTipLegacyEstimateHeight.GetValueOnGameThread() != 0;
		const float Content = (!bLegacy && BodyDesiredH > 1.f) ? FMath::Max(BodyDesiredH, LineH) : Estimate;
		PanelH = BodyTop + Content + kBodyBottom;
		break;
	}
	}

	Resize9Slice(Planes, FVector2D(PanelW, PanelH));
	LastSize = FVector2D(PanelW, PanelH);

	// Окончательная высота слота тела — по посчитанной плашке.
	if (BodyW)
	{
		if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(BodyW))
		{
			CS->SetOffsets(FMargin(kBodyInsetX, BodyTop, BodyW_, FMath::Max(PanelH - BodyTop - kBodyBottom, 8.f)));
		}
	}

	if (TitleW)
	{
		if (UCanvasPanelSlot* TS = AsdaUi::SlotOf(TitleW))
		{
			TS->SetOffsets(FMargin(2.f, 2.f, PanelW - 4.f, LineH + 2.f));
		}
		// Заголовок тоже rich: эталон пишет в 5001 строку С ТЕГАМИ («<FC=..>+15%</F> <FC=..>[Шедевр]</F> Имя»),
		//   а базовый цвет ставит `CControl_SetField923` (цвет грейда) — он и лежит в TitleStyle,
		//   поэтому `</F>` возвращает именно к нему (реставрация тегами больше не нужна).
		FAsdaRichApplyOpts TOpts;
		TOpts.BaseStyle   = bHaveFont ? &TitleStyle : nullptr;
		TOpts.bWrap       = false;                       // заголовок ОДНОстрочный
		TOpts.bSetJustify = true;
		TOpts.Justify     = ETextJustify::Center;        // JUI ITEM_TIPTITLE TextAlign = 5
		if (UWidget* RichT = UAsdaRichText::ApplyFormatted(TitleW, Title, &TOpts)) { TitleW = RichT; }
		else if (UTextBlock* T = Cast<UTextBlock>(TitleW))
		{
			T->SetAutoWrapText(false);
			T->SetJustification(ETextJustify::Center);
			T->SetText(FText::FromString(FAsdaMarkup::StripTags(Title)));
		}
		TipSetVis(TitleW, true);
	}

	// ── «ЭЛИТНЫЙ» ПОДВАЛ (эталон `Tooltip_LayoutNearCursor`, блок «если 5007 видим») ──
	if (M.bPremiumFooter && !M.FooterText.IsEmpty())
	{
		TipSetVis(PremiumIcon, true);
		if (UCanvasPanelSlot* PS = PremiumIcon ? AsdaUi::SlotOf(PremiumIcon) : nullptr)
		{
			const FMargin O = PS->GetOffsets();
			PS->SetOffsets(FMargin(7.f, 25.f, O.Right, O.Bottom));   // эталон SetField74_75(5007, 7, 25)
		}
		const float FootW = FMath::Max(PanelW - 12.f, 16.f);
		const FString FootPlain = FAsdaMarkup::StripTags(M.FooterText).Replace(TEXT("\\n"), TEXT("\n"));
		const int32 FootLines = CountWrappedLines(FootPlain, Fnt, FootW);
		// Эталон: h = глиф·n + интервал·(n−1).
		const float FootH = LineH * (float)FootLines + M.LegacyLineGap * (float)(FootLines - 1) + 2.f * kBodyBottom;
		if (FooterPlanes.Num() == 9)
		{
			Resize9Slice(FooterPlanes, FVector2D(PanelW, FootH));
			for (UImage* Im : FooterPlanes) { TipSetVis(Im, true); }
			if (UCanvasPanelSlot* FS = AsdaUi::SlotOf(FooterPlanes[0]))
			{
				// плашка ставится СРАЗУ ПОД панелью (эталон SetField74_75(5005, 0, height(5000)))
				const FMargin O = FS->GetOffsets();
				FS->SetOffsets(FMargin(O.Left, PanelH, O.Right, O.Bottom));
			}
		}
		if (FooterText)
		{
			if (UCanvasPanelSlot* TS = AsdaUi::SlotOf(FooterText))
			{
				TS->SetAutoSize(false);
				TS->SetOffsets(FMargin(7.f, PanelH + 7.f, FootW, FootH));
			}
			// Подвал 5006/5056 — тот же базовый стиль, что и тело (JUI TextColor = FFFFFFFF), перенос ЕСТЬ.
			FAsdaRichApplyOpts FOpts;
			FOpts.BaseStyle   = bHaveFont ? &BodyStyle : nullptr;
			FOpts.bWrap       = true;
			FOpts.WrapAt      = FootW;
			FOpts.bSetJustify = true;
			FOpts.Justify     = ETextJustify::Left;
			if (UWidget* RichF = UAsdaRichText::ApplyFormatted(FooterText, M.FooterText, &FOpts)) { FooterText = RichF; }
			TipSetVis(FooterText, true);
		}
		LastSize.Y = PanelH + FootH;   // кламп к экрану должен учитывать подвал
	}
	else
	{
		TipSetVis(PremiumIcon, false);
		for (UImage* Im : FooterPlanes) { TipSetVis(Im, false); }
		TipSetVis(FooterText, false);
	}

	Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
	bShown = true;
	UE_LOG(LogAsdaTip, Verbose,
		TEXT("[тултип] показ: панель %.0fx%.0f, заголовок %d симв., тело %d симв. (строк %d, кегль %d)"),
		LastSize.X, LastSize.Y, Title.Len(), Body.Len(), Wrapped, M.FontPx);
}

void UAsdaTipPanel::Hide()
{
	if (Widget) { Widget->SetVisibility(ESlateVisibility::Collapsed); }
	bShown = false;
	Owner  = EAsdaTipOwner::None;
	SourceWindow = NAME_None;   // ★ 31.10.2026: скрытой плашке следить не за чем
}

bool UAsdaTipPanel::HideFor(EAsdaTipOwner InOwner)
{
	// ★★★ Скрыть может ТОЛЬКО владелец. Разбор — в шапке EAsdaTipOwner (AsdaTipPanel.h).
	if (!bShown || Owner != InOwner) { return false; }
	Hide();
	return true;
}

void UAsdaTipPanel::SetViewportPos(const FVector2D& Pos)
{
	LastPos = Pos;
	if (Widget) { Widget->SetPositionInViewport(Pos, false); }
}

void UAsdaTipPanel::LayoutAtCursor(float ExtraW)
{
	if (!Widget) { return; }
	UGameInstance* GI = Widget->GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;

	// ★★★ ИСПРАВЛЕНО 2026-08-08 (дефект «тултип мигает в левом-верхнем углу»).
	//   БЫЛО: курсор переводился в ЛОКАЛЬНЫЕ координаты САМОЙ ЭТОЙ ПЛАШКИ
	//   (`AsdaUi::ScreenToCanvasLocal(Widget, ...)`), а результат уходил в `SetPositionInViewport`,
	//   который ждёт координаты ВЬЮПОРТА. Пока плашка ни разу не рисовалась, геометрии не было и
	//   фолбэк случайно давал верный ответ; со второго кадра геометрия появлялась, и получалось
	//   P(n+1) = Курсор − P(n) + 32 — двухтактный «маятник» между позицией у курсора и углом (0,0).
	//   Кадр у курсора / кадр в углу, и так каждый кадр: именно это юзер видел как «мелькает
	//   вверху-слева». Плюс `DPI` брался из геометрии свёрнутого виджета (там он не обновляется).
	//   СТАЛО: единая точка `AsdaUi::CursorInViewportUnits` (пиксели вьюпорта / UI-масштаб) —
	//   ровно та система координат, в которой работает SetPositionInViewport(pos, false).
	FVector2D Cursor = FVector2D::ZeroVector;
	AsdaUi::CursorInViewportUnits(World, Cursor);
	const FVector2D Screen = AsdaUi::ViewportSizeUnits(World);     // экран в тех же единицах

	// Эталон `Tooltip_LayoutNearCursor`: левый-верх = курсор + 32/32.
	float X = (float)Cursor.X + 32.f;
	float Y = (float)Cursor.Y + 32.f;
	const float W = LastSize.X + ExtraW;   // эталон прибавляет ширину окна сравнения ДО клампа
	const float H = LastSize.Y;

	// Эталон `uiClampPopupToScreen`: отступ 5 px от каждого края.
	if (X + W > Screen.X) { X = Screen.X - W - 5.f; }
	if (X < 0.f)          { X = 5.f; }
	if (Y < 0.f)          { Y = 5.f; }
	if (Y + H + 5.f > Screen.Y)
	{
		Y = Screen.Y - H - 5.f;
		X = (float)Cursor.X + 5.f;                                  // эталон пересчитывает X от курсора
		if (X + W > Screen.X) { X = Screen.X - W - 5.f; }
	}
	SetViewportPos(FVector2D(X, Y));
	UE_LOG(LogAsdaTip, VeryVerbose,
		TEXT("[тултип] укладка: курсор(%.0f,%.0f) экран(%.0fx%.0f) панель(%.0fx%.0f +%0.f) → (%.0f,%.0f)"),
		Cursor.X, Cursor.Y, Screen.X, Screen.Y, LastSize.X, LastSize.Y, ExtraW, X, Y);
}

FString UAsdaTipPanel::DebugState() const
{
	if (!Widget)
	{
		return TEXT("плашка НЕ ПРИВЯЗАНА (окно WBP не загрузилось)");
	}
	const FGeometry& G = Widget->GetCachedGeometry();
	const UEnum* E = StaticEnum<EAsdaTipOwner>();
	return FString::Printf(
		TEXT("окно «%s» видимость=%d показана=%s владелец=%s размер=%.0fx%.0f позиция=(%.0f,%.0f) ")
		TEXT("геометрия: абс(%.0f,%.0f) размер(%.0fx%.0f) масштаб=%.2f | узлы: заголовок=%s тело=%s планов=%d"),
		*Widget->GetName(), (int32)Widget->GetVisibility(), bShown ? TEXT("да") : TEXT("нет"),
		E ? *E->GetNameStringByValue((int64)Owner) : TEXT("?"),
		LastSize.X, LastSize.Y, LastPos.X, LastPos.Y,
		G.GetAbsolutePosition().X, G.GetAbsolutePosition().Y, G.GetLocalSize().X, G.GetLocalSize().Y, G.Scale,
		TitleW ? *TitleW->GetName() : TEXT("НЕТ"), BodyW ? *BodyW->GetName() : TEXT("НЕТ"), Planes.Num());
}
