// Copyright (c) 2026 @sitxovski (Discord)
// AsdaTitleSubsystem_Debug.cpp — ★ R3-c 12.11.2026 (разрез AsdaTitleSubsystem.cpp, дословный перенос): дампы и дев-команды.
//   Общее для частей — в AsdaTitleSubsystem_Private.h; шапка и реверс — в AsdaTitleSubsystem.cpp.
#include "UI/AsdaTitleSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaUiHelpers.h"   // ★ U20: AsdaUi::BindButton
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "UI/AsdaHudSubsystem.h"       // ★ ShowTitleBanner — баннер «звание получено» (тип 2)
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaMarkup.h"          // UAsdaRichText::ApplyFormatted — единственный показ тегов <FC=..>
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaNameplateComponent.h"   // ★ D8: читаем ФАКТИЧЕСКИЕ поля плашки в проверке снятия

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaTitleSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaTitleSubsystem*.cpp

// ═════════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════════
FString UAsdaTitleSubsystem::DumpControls() const
{
	FString S = TEXT("=== КОНТРОЛЫ ОКНА ЗВАНИЙ 18700 ===\n");
	auto Line = [&S](const TCHAR* Idc, int32 IdNum, const UWidget* W)
	{
		S += FString::Printf(TEXT("  %-28s %-7d %s\n"), Idc, IdNum, W ? TEXT("есть") : TEXT("НЕТ"));
	};
	Line(TEXT("Title_Win (рамка, планов)"), 18700, FramePlanes.Num() == 9 ? (UWidget*)Widget : nullptr);
	Line(TEXT("Title_Win_CloseBtn"),   18703, BtnClose);
	Line(TEXT("Title_Ranking"),        18704, TxtRanking);
	Line(TEXT("Title_TotalPoint"),     18705, TxtTotalPoint);
	Line(TEXT("Title_TotalGathRate"),  18706, TxtTotalRate);
	Line(TEXT("Title_1stTitle_Name"),  18708, RsFirstTitle.Node);
	Line(TEXT("Title_2ndTitle_Name"),  18710, RsSecondTitle.Node);
	Line(TEXT("Title_Title_CharName"), 18711, TxtCharName);
	Line(TEXT("Title_ReleaseBtn"),     18712, BtnRelease);
	Line(TEXT("Title_ClassSelectBtn0"),18713, TabButtons.IsValidIndex(0) ? TabButtons[0].Get() : nullptr);
	Line(TEXT("Title_ClassSelectBtn1"),18713, TabButtons.IsValidIndex(1) ? TabButtons[1].Get() : nullptr);
	Line(TEXT("Title_ClassSelectBtn2"),18713, TabButtons.IsValidIndex(2) ? TabButtons[2].Get() : nullptr);
	Line(TEXT("Title_ClassGathRate"),  18714, TxtTabRate);
	Line(TEXT("Title_InfoTitleTxt"),   18715, TxtInfoCap);
	Line(TEXT("Title_Info_Name"),      18716, RsInfoName.Node);
	Line(TEXT("Title_Info_Grade"),     18717, TxtInfoGrade);
	Line(TEXT("Title_Info_Point"),     18718, TxtInfoPoint);
	Line(TEXT("Title_Info_Condition"), 18723, TxtInfoCondition);
	Line(TEXT("Title_Info_Location"),  18724, TxtInfoLocation);
	Line(TEXT("Title_LeftTime_Txt"),   18725, TxtLeftTimeCap);
	Line(TEXT("Title_LeftTime"),       18726, TxtLeftTime);
	Line(TEXT("Title_BtnOk"),          18727, BtnOk);
	Line(TEXT("Title_BtnCancel"),      18728, BtnCancel);
	Line(TEXT("Title_List_AppBtn"),    18737, BtnApply);
	Line(TEXT("Title_List_DelBtn"),    18738, BtnDelete);
	Line(TEXT("Title_Info_Type"),     188570, TxtInfoType);
	Line(TEXT("Title_Info_Random"),   188580, TxtInfoRandom);
	Line(TEXT("Title_Info_Type_Name"),188590, TxtInfoTypeName);
	for (int32 i = 0; i < RsInfoOption.Num(); ++i)
	{
		Line(*FString::Printf(TEXT("Title_Info_Option%d"), i + 1), 18840 + i, RsInfoOption[i].Node);
	}
	const int32 ColId[7] = { 18729, 18730, 18731, 18732, 18733, 18734, 188350 };
	for (int32 i = 0; i < ColumnCaptions.Num(); ++i)
	{
		Line(*FString::Printf(TEXT("колонка %d (подпись)"), i), ColId[i], ColumnCaptions[i]);
	}
	for (int32 R = 0; R < Rows.Num(); ++R)
	{
		int32 Ok = 0; for (int32 C = 0; C < 7; ++C) { if (Rows[R].Cells[C].Node) { ++Ok; } }
		S += FString::Printf(TEXT("  ряд %02d: контейнер=%s ячеек=%d/7 Sel=%s Find=%s Lose=%s Bar=%s\n"),
			R + 1, Rows[R].Group ? TEXT("да") : TEXT("НЕТ"), Ok,
			Rows[R].Sel ? TEXT("да") : TEXT("НЕТ"), Rows[R].Find ? TEXT("да") : TEXT("НЕТ"),
			Rows[R].Lose ? TEXT("да") : TEXT("НЕТ"), Rows[R].Bar ? TEXT("да") : TEXT("НЕТ"));
	}
	S += FString::Printf(TEXT("  рамка выбора 18736: планов %d/8; полоса 18735: %s\n"),
		SelectedFrame.Num(), ScrollBar.IsBuilt() ? TEXT("построена") : TEXT("НЕТ"));
	S += FString::Printf(TEXT("  значок 50000/50001/50002: %s / %s / %s\n"),
		NoticeBg ? TEXT("есть") : TEXT("НЕТ"), NoticeLose ? TEXT("есть") : TEXT("НЕТ"),
		NoticeFind ? TEXT("есть") : TEXT("НЕТ"));
	// ★★★ ЖИВАЯ ГЕОМЕТРИЯ И ВИДИМОСТЬ ФОНА ЗНАЧКА (2026-08-30). Ради этого дампа он и добавлен:
	//   «иконка ничего не делает» = фон был `SelfHitTestInvisible` и мышь не ловил. Печатаем ФАКТ
	//   ПОСЛЕ ОТРИСОВКИ (`GetCachedGeometry`, закон `ui-check-game-path-geometry`), а не намерение:
	//   `Visible` + ненулевой прямоугольник = клик физически может попасть.
	if (NoticeBg)
	{
		const FGeometry& G = NoticeBg->GetCachedGeometry();
		const FVector2D P = G.GetAbsolutePosition();
		const FVector2D Sz = G.GetLocalSize() * G.Scale;
		const ESlateVisibility V = NoticeBg->GetVisibility();
		S += FString::Printf(
			TEXT("  фон значка 50000: видимость=%s (ловит мышь: %s) прямоугольник=(%.0f,%.0f) %.0fx%.0f\n"),
			V == ESlateVisibility::Visible ? TEXT("Visible")
				: V == ESlateVisibility::SelfHitTestInvisible ? TEXT("SelfHitTestInvisible")
				: V == ESlateVisibility::HitTestInvisible ? TEXT("HitTestInvisible")
				: V == ESlateVisibility::Collapsed ? TEXT("Collapsed") : TEXT("Hidden"),
			V == ESlateVisibility::Visible ? TEXT("ДА") : TEXT("нет"),
			P.X, P.Y, Sz.X, Sz.Y);
	}
	// ★ ВСЁ ДЕРЕВО ОКНА ЗНАЧКА С ЖИВОЙ ГЕОМЕТРИЕЙ. Нужно потому, что нулевой прямоугольник у фона
	//   означает «кто-то ВЫШЕ по цепочке схлопнут», а по одному узлу этого не увидеть
	//   ([[jui-cell-chain-nesting]], [[jui-flattened-hierarchy-geometry]]).
	if (NoticeWidget && NoticeWidget->WidgetTree)
	{
		TArray<UWidget*> All;
		NoticeWidget->WidgetTree->GetAllWidgets(All);
		S += FString::Printf(TEXT("  дерево окна значка (%d узлов):\n"), All.Num());
		for (UWidget* W : All)
		{
			if (!W) { continue; }
			const FGeometry& G = W->GetCachedGeometry();
			const FVector2D P = G.GetAbsolutePosition();
			const FVector2D Sz = G.GetLocalSize() * G.Scale;
			S += FString::Printf(TEXT("    %-40s вид=%d рект=(%.0f,%.0f) %.0fx%.0f\n"),
				*W->GetName(), (int32)W->GetVisibility(), P.X, P.Y, Sz.X, Sz.Y);
		}
	}
	return S;
}

// ★★★ ВКЛАДКИ ПУТЁМ ИГРОКА (2026-08-30). Снимаем не «что я поставил», а ФАКТ ПОСЛЕ ОТРИСОВКИ:
//   абсолютный экранный прямоугольник (`GetCachedGeometry`), видимость, кисть плана и цвет
//   подписи. Слот из ассета для этого не годится (закон `ui-check-game-path-geometry`), а
//   PIE-дамп по вкладкам уже один раз соврал. Кадр 0 скина `TabBar_W108_3Btn` лежит в
//   `ne_wheeltaget2` (белый прямоугольник × тинт C0C0C0 = СЕРЫЙ фон), кадр 2 — в
//   `ne_wheeltaget1` (ЧЁРНАЯ заливка в БЕЛОЙ рамке): различить их можно и по текстуре, и по UV.
void UAsdaTitleSubsystem::DumpTabsOnScreen(const TCHAR* Tag) const
{
	UE_LOG(LogAsdaTitle, Log, TEXT("=== ВКЛАДКИ 18713 НА ЭКРАНЕ [%s]: активна %d ==="), Tag, Tab);
	if (!Widget) { UE_LOG(LogAsdaTitle, Log, TEXT("  виджета нет")); return; }
	auto Geom = [](const UWidget* W) -> FString
	{
		if (!W) { return TEXT("НЕТ УЗЛА"); }
		const FGeometry& G = W->GetCachedGeometry();
		return FString::Printf(TEXT("экран(%.0f,%.0f %.0fx%.0f) vis=%s"),
			G.GetAbsolutePosition().X, G.GetAbsolutePosition().Y,
			G.GetLocalSize().X, G.GetLocalSize().Y,
			*StaticEnum<ESlateVisibility>()->GetNameStringByValue((int64)W->GetVisibility()));
	};
	for (int32 i = 0; i < TabPlanes.Num(); ++i)
	{
		const UImage* Img = TabPlanes.IsValidIndex(i) ? TabPlanes[i].Get() : nullptr;
		const UTextBlock* Tb = TabCaptions.IsValidIndex(i) ? TabCaptions[i].Get() : nullptr;
		const UButton* Bt = TabButtons.IsValidIndex(i) ? TabButtons[i].Get() : nullptr;
		UE_LOG(LogAsdaTitle, Log, TEXT("  вкладка %d%s"), i, (i == Tab) ? TEXT("  ← АКТИВНА") : TEXT(""));
		if (Img)
		{
			const FSlateBrush& B = Img->GetBrush();
			const UObject* Res = B.GetResourceObject();
			const FBox2f Uv = B.GetUVRegion();
			UE_LOG(LogAsdaTitle, Log,
				TEXT("      план   %s кисть[%s %s uv %.3f,%.3f-%.3f,%.3f тинт %s]"),
				*Geom(Img),
				B.DrawAs == ESlateBrushDrawType::NoDrawType ? TEXT("НЕ РИСУЕТ") : TEXT("Image"),
				Res ? *Res->GetName() : TEXT("-"),
				Uv.Min.X, Uv.Min.Y, Uv.Max.X, Uv.Max.Y,
				*B.TintColor.GetSpecifiedColor().ToFColor(true).ToHex());
		}
		else { UE_LOG(LogAsdaTitle, Log, TEXT("      план   НЕТ УЗЛА")); }
		if (Bt)
		{
			// Кнопка лежит ПОВЕРХ плана: если её кадр 0 что-то рисует, он перекрывает план.
			const FSlateBrush& N = Bt->GetStyle().Normal;
			const UObject* Res = N.GetResourceObject();
			UE_LOG(LogAsdaTitle, Log, TEXT("      кнопка %s кадр0[%s %s]"), *Geom(Bt),
				N.DrawAs == ESlateBrushDrawType::NoDrawType ? TEXT("НЕ РИСУЕТ") : TEXT("Image"),
				Res ? *Res->GetName() : TEXT("-"));
		}
		if (Tb)
		{
			const FLinearColor C = Tb->GetColorAndOpacity().GetSpecifiedColor();
			UE_LOG(LogAsdaTitle, Log, TEXT("      текст  %s «%s» цвет=%s"),
				*Geom(Tb), *Tb->GetText().ToString(), *C.ToFColor(true).ToHex());
		}
	}

	// ★★★ ПОЛНЫЙ СТРУКТУРНЫЙ РАЗБОР ПОЛОСЫ В РАНТАЙМЕ (закон `full-structural-analysis-always`).
	//   Точечный дамп выше печатает ТОЛЬКО те три узла, которые нашла сама подсистема, — и потому
	//   не способен увидеть две вещи, из-за которых вкладки 1 и 2 могут остаться плоскими:
	//     1) ДВОЙНИКИ ИМЁН: контрол, объявленный в данных дважды, даёт ДВА виджета с ОДНИМ именем;
	//        `FindByPrefix` берёт первый попавшийся, а рисуется второй (закон `false-negatives-method`);
	//     2) НЕОДНОРОДНАЯ РАСКЛАДКА: у вкладок 1 и 2 в ассете может не быть узлов, которые есть у
	//        вкладки 0 (закон `jui-uneven-node-layout`).
	//   Поэтому здесь обходится ВЕСЬ `WidgetTree` окна и печатается КАЖДЫЙ узел с префиксом
	//   `Title_ClassSelectBtn`: класс, родитель, порядок среди детей (он решает перекрытие,
	//   закон `umg-canvas-draw-order`), экранный прямоугольник, видимость, кисть и признак
	//   «этот ли экземпляр держит подсистема».
	if (!Widget->WidgetTree) { return; }
	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);
	UE_LOG(LogAsdaTitle, Log, TEXT("  --- ВСЕ УЗЛЫ ПОЛОСЫ Title_ClassSelectBtn в дереве окна ---"));
	int32 Found = 0;
	for (UWidget* Wg : All)
	{
		if (!Wg || !Wg->GetName().StartsWith(TEXT("Title_ClassSelectBtn"))) { continue; }
		++Found;

		// Держит ли подсистема ИМЕННО ЭТОТ экземпляр? Если узел с нужным именем есть, а признака
		//   «наш» нет — значит рисуется двойник, и вся правка вида уходит «в другой виджет».
		FString Mine;
		for (int32 i = 0; i < TabPlanes.Num(); ++i)   { if (TabPlanes[i].Get()   == Wg) { Mine = FString::Printf(TEXT(" ←план%d"),   i); } }
		for (int32 i = 0; i < TabButtons.Num(); ++i)  { if (TabButtons[i].Get()  == Wg) { Mine = FString::Printf(TEXT(" ←кнопка%d"), i); } }
		for (int32 i = 0; i < TabCaptions.Num(); ++i) { if (TabCaptions[i].Get() == Wg) { Mine = FString::Printf(TEXT(" ←текст%d"),  i); } }

		// Родитель и порядковый номер среди его детей — порядок рисования внутри канваса.
		UPanelWidget* Par = Wg->GetParent();
		const int32 Idx = Par ? Par->GetChildIndex(Wg) : -1;
		const int32 Cnt = Par ? Par->GetChildrenCount() : 0;

		// Кисть — только у картинок; у кнопок печатаем кадр 0 (он перекрывает план, если рисует).
		FString Br;
		if (const UImage* Im = Cast<UImage>(Wg))
		{
			const FSlateBrush& B = Im->GetBrush();
			const FBox2f Uv = B.GetUVRegion();
			Br = FString::Printf(TEXT(" кисть[%s %s uv %.3f,%.3f-%.3f,%.3f тинт %s] цвет=%s"),
				B.DrawAs == ESlateBrushDrawType::NoDrawType ? TEXT("НЕ РИСУЕТ") : TEXT("Image"),
				B.GetResourceObject() ? *B.GetResourceObject()->GetName() : TEXT("-"),
				Uv.Min.X, Uv.Min.Y, Uv.Max.X, Uv.Max.Y,
				*B.TintColor.GetSpecifiedColor().ToFColor(true).ToHex(),
				*Im->GetColorAndOpacity().ToFColor(true).ToHex());
		}

		UE_LOG(LogAsdaTitle, Log, TEXT("    %-34s %-14s родитель=%s [%d/%d] %s%s%s"),
			*Wg->GetName(), *Wg->GetClass()->GetName(),
			Par ? *Par->GetName() : TEXT("<нет>"), Idx, Cnt,
			*Geom(Wg), *Br, *Mine);
	}
	UE_LOG(LogAsdaTitle, Log, TEXT("  --- узлов полосы в дереве: %d ---"), Found);
}

FString UAsdaTitleSubsystem::DumpState() const
{
	int32 Own = 0, Fnd = 0;
	for (int32 i = 0; i < EffectiveMaskBits(); ++i) { if (MaskGet(MaskOwned, i)) { ++Own; } if (MaskGet(MaskFound, i)) { ++Fnd; } }
	return FString::Printf(
		TEXT("=== СОСТОЯНИЕ ЗВАНИЙ ===\n")
		TEXT("  окно открыто=%d, контролы=%d\n")
		TEXT("  таблица: званий %d, максимальный id %d, статов в словаре %d\n")
		TEXT("  маски: получено %d бит, найдено %d бит; блоков статов %d\n")
		TEXT("  очки=%d ранг=%d; надето (приставка=%d, окончание=%d); выбор (%d, %d)\n")
		TEXT("  вкладка=%d, в списке %d, скролл=%d, выбранный ряд=%d, сортировка колонка=%d %s\n")
		TEXT("  кулдаун=%d мс; значок: активен=%d иконка=%d подсказка=%d\n")
		TEXT("  мигают: новых %d, потерянных %d; %% вкладки=%.2f (%d из %d), %% всего=%.2f\n"),
		bOpen ? 1 : 0, bControlsReady ? 1 : 0,
		Defs.Num(), MaxTitleId, OptionNames.Num(),
		Own, Fnd, TitleStats.Num(),
		FamePoints, FameRank, WornPrefix, WornSuffix, SelPrefix, SelSuffix,
		Tab, Order.Num(), ScrollTop, SelectedRow, SortColumn,
		bSortDesc ? TEXT("убыв.") : TEXT("возр."),
		CooldownMs, bNoticeActive ? 1 : 0, NoticeIconKind, NoticeTipKind,
		FlagNew.Num(), FlagLost.Num(), TabRatePercent, TabOwnedCount, TabTotalCount, TotalRatePercent);
}

FString UAsdaTitleSubsystem::DumpList() const
{
	// ★ ЗАКОН: читаем С ВИДЖЕТОВ, а не из своих переменных — иначе дамп «подтверждает» непроехавшую правку.
	FString S = TEXT("=== 12 СТРОК СПИСКА (текст читается с виджетов) ===\n");
	for (int32 R = 0; R < Rows.Num(); ++R)
	{
		FString Cells;
		for (int32 C = 0; C < 7; ++C)
		{
			// ★ Читаем С УЗЛА, а он после первого показа уже `URichTextBlock` — иначе дамп
			//   «подтверждал» бы непроехавшую правку пустотой.
			Cells += TEXT("[") + NodeText(Rows[R].Cells[C].Node) + TEXT("] ");
		}
		S += FString::Printf(TEXT("  %02d id=%-4d %s\n"),
			R + 1, RowTitleId.IsValidIndex(R) ? RowTitleId[R] : -1, *Cells);
	}
	S += FString::Printf(TEXT("  подписи слотов: приставка=«%s» окончание=«%s» имя=«%s»\n"),
		*NodeText(RsFirstTitle.Node), *NodeText(RsSecondTitle.Node),
		TxtCharName ? *TxtCharName->GetText().ToString() : TEXT("-"));
	// Вкладки — состояние ЖИВЫХ виджетов: подпись и её цвет (кадр ставит `AsdaUi::ApplyTabState`).
	// ★ Вместе с подписью печатаем КИСТЬ ПЛАНА и кадр 2 из снятого стиля кнопки: без этого
	//   «активная вкладка светлая» приходится принимать на веру. Кадр 0 скина `TabBar_W108_3Btn`
	//   лежит в `ne_wheeltaget2` (белый прямоугольник × тинт C0C0C0 = СЕРЫЙ фон), кадр 2 —
	//   в `ne_wheeltaget1` (ЧЁРНАЯ заливка в БЕЛОЙ рамке). Различаются они и текстурой, и UV,
	//   поэтому в дампе печатаем и то, и другое.
	auto BrushInfo = [](const FSlateBrush& B) -> FString
	{
		const UObject* Res = B.GetResourceObject();
		const FBox2f Uv = B.GetUVRegion();
		return FString::Printf(TEXT("%s tex=%s uv=[(%.3f,%.3f)-(%.3f,%.3f)] тинт=%s"),
			B.DrawAs == ESlateBrushDrawType::NoDrawType ? TEXT("НЕ РИСУЕТ") : TEXT("Image"),
			Res ? *Res->GetName() : TEXT("<нет>"),
			Uv.Min.X, Uv.Min.Y, Uv.Max.X, Uv.Max.Y,
			*B.TintColor.GetSpecifiedColor().ToFColor(true).ToHex());
	};
	for (int32 i = 0; i < TabCaptions.Num(); ++i)
	{
		const FLinearColor C = TabCaptions[i] ? TabCaptions[i]->GetColorAndOpacity().GetSpecifiedColor()
		                                     : FLinearColor::Transparent;
		S += FString::Printf(TEXT("  вкладка %d%s: «%s» цвет=%s\n"), i, (i == Tab) ? TEXT(" (активна)") : TEXT(""),
			TabCaptions[i] ? *TabCaptions[i]->GetText().ToString() : TEXT("-"), *C.ToFColor(true).ToHex());
		S += FString::Printf(TEXT("      план:        %s\n"),
			TabPlanes.IsValidIndex(i) && TabPlanes[i] ? *BrushInfo(TabPlanes[i]->GetBrush()) : TEXT("<нет узла>"));
		S += FString::Printf(TEXT("      снят кадр 2: %s\n"),
			TabBtnIdle.IsValidIndex(i) ? *BrushInfo(TabBtnIdle[i].Pressed) : TEXT("<стиль не снят>"));
		S += FString::Printf(TEXT("      снят кадр 0: %s\n"),
			TabPlaneIdle.IsValidIndex(i) ? *BrushInfo(TabPlaneIdle[i]) : TEXT("<кисть не снята>"));
	}
	S += FString::Printf(TEXT("  полоса 18735: начало=(%.0f,%.0f) высота=%.0f ширина=%.0f; %s\n"),
		ScrollBarOrigin.X, ScrollBarOrigin.Y, ScrollBarHeight, AsdaNomalScr::kWidth,
		ScrollBar.IsBuilt() ? *ScrollBar.Dump() : TEXT("НЕ построена"));
	// Инфо-панель — тоже С ВИДЖЕТОВ: без этого «шесть бонусов» пришлось бы принимать на веру.
	auto Tx = [](const UTextBlock* Tb) { return Tb ? Tb->GetText().ToString() : FString(TEXT("-")); };
	S += FString::Printf(TEXT("  инфо-панель: имя=«%s» ранг=«%s» очки=«%s» место=«%s»\n"),
		*NodeText(RsInfoName.Node), *Tx(TxtInfoGrade), *Tx(TxtInfoPoint), *Tx(TxtInfoLocation));
	for (int32 i = 0; i < RsInfoOption.Num(); ++i)
	{
		S += FString::Printf(TEXT("    бонус %d: «%s»\n"), i + 1, *NodeText(RsInfoOption[i].Node));
	}
	S += FString::Printf(TEXT("    условие получения: «%s»\n"), *Tx(TxtInfoCondition));
	S += FString::Printf(TEXT("  кулдаун на экране: «%s»\n"), *Tx(TxtLeftTime));
	S += FString::Printf(TEXT("  шапка: «%s» · «%s» · «%s»; вкладка: «%s»\n"),
		TxtRanking ? *TxtRanking->GetText().ToString() : TEXT("-"),
		TxtTotalPoint ? *TxtTotalPoint->GetText().ToString() : TEXT("-"),
		TxtTotalRate ? *TxtTotalRate->GetText().ToString() : TEXT("-"),
		TxtTabRate ? *TxtTabRate->GetText().ToString() : TEXT("-"));
	return S;
}
