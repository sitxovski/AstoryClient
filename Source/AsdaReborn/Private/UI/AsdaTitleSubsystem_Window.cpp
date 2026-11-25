// Copyright (c) 2026 @sitxovski (Discord)
// AsdaTitleSubsystem_Window.cpp — ★ R3-c 12.11.2026 (разрез AsdaTitleSubsystem.cpp, дословный перенос): окно званий: зона, контролы, значок-уведомление, список, шапка, панель сведений, мышь, кнопки, подсказка.
//   Общее для частей — в AsdaTitleSubsystem_Private.h; шапка и реверс — в AsdaTitleSubsystem.cpp.
#include "UI/AsdaTitleSubsystem.h"
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaUiHelpers.h"   // ★ U20: AsdaUi::BindButton
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "AsdaNetSubsystem.h"
#include "AsdaDataTableCache.h"
#include "Items/AsdaGradeColor.h"
#include "Login/AsdaButtonRelay.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaEffectsSubsystem.h"   // ★ SpawnTitleFx — два эффекта звания на персонаже (attach-6)
#include "UI/AsdaHudSubsystem.h"       // ★ ShowTitleBanner — баннер «звание получено» (тип 2)
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaMarkup.h"          // UAsdaRichText::ApplyFormatted — единственный показ тегов <FC=..>
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaNameplateComponent.h"   // ★ D8: читаем ФАКТИЧЕСКИЕ поля плашки в проверке снятия
#include "World/AsdaWorldSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "UI/AsdaCraftSubsystem.h"   // ★ волна В3 ремесла: единый предикат «окно ремесла открыто» (`IsAnyCraftWindowOpen`)
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "Login/AsdaLoginData.h"   // [ПОРТ] 08.10.2026: общий кодек провода (гибрид RU+EN+AR)
#include "AsdaSelf.h"                      // ★ Дедуп №2, В1-B (U5a): «это я?» — `AsdaSelf::IsSelfUserIdx`
#include "AsdaTitleSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaTitleSubsystem*.cpp

namespace {
	const TCHAR* kNoticeWbp =
		TEXT("/Game/AsdaAssets/UI/Player_UI/HUD/WBP_Title_Notice_BG.WBP_Title_Notice_BG_C");

	// ── Цвета, снятые дизассемблером (sRGB ARGB) ──
	constexpr uint32 kColStateOwned = 0xFFFFF200;   // −3584    «Получено»
	constexpr uint32 kColStateFound = 0xFFC8C8C8;   // −3618616 «Найдено» и «Преф.»
	constexpr uint32 kColWhite      = 0xFFFFFFFF;   // −1       «Оконч.»
	constexpr uint32 kColStatPlus   = 0xFF5A96FA;   // −10853126 положительный бонус
	constexpr uint32 kColStatMinus  = 0xFFFF0F0F;   // −61681    отрицательный бонус

	// ── Ключи отката дефектов оригинала (см. шапку заголовка) ──
	ASDA_DEV_CVAR(int32, CVarShowRankDelta,
		TEXT("asda.Title.ShowRankDelta"), 0,
		TEXT("D1. 0 (по умолчанию) = как в оригинале: метки дельты ранга/очков/% ПУСТЫ (эталон\n")
		TEXT("печатает в них пустую строку вместо посчитанной). 1 = печатать посчитанное."),
		ECVF_Default);

// (D2 снят 27.09.2026: ключ `asda.Title.OwnerMaskFromTarget` удалён — эталон проверяет СВОЮ маску, см. шапку заголовка.)
	ASDA_DEV_CVAR(int32, CVarSilentApplyDeny,
		TEXT("asda.Title.SilentApplyDeny"), 0,
		TEXT("D4. 1 = как в оригинале: «Применить» на неполученном звании молчит.\n")
		TEXT("0 (по умолчанию) = показываем нотис 2426, который в оригинале не используется нигде."),
		ECVF_Default);

	ASDA_DEV_CVAR(int32, CVarRawCategoryLoc,
		TEXT("asda.Title.RawCategoryLoc"), 0,
		TEXT("D6. 1 = как в оригинале: колонка «Категория» берёт строку 1460+значение даже когда\n")
		TEXT("значение выходит за 0…5 (получается чужой текст «Преф.»/«Оконч.»).\n")
		TEXT("0 (по умолчанию) = такая колонка пуста, факт пишется в лог."),
		ECVF_Default);

	// ★ D7 — знаменатель «Всего собрано %».
	ASDA_DEV_CVAR(int32, CVarTotalRateByCount,
		TEXT("asda.Title.TotalRateByCount"), 0,
		TEXT("D7. 0 (по умолчанию) = как в оригинале: знаменатель «Всего собрано %» —\n")
		TEXT("МАКСИМАЛЬНЫЙ id таблицы (g_nDataRecIdMax), а не число записей. С маской 512 бит\n")
		TEXT("числитель теперь может дойти до ЧИСЛА ЗАПИСЕЙ, и при полной коллекции процент\n")
		TEXT("выходит чуть больше 100 (280/279 = 100,36 %) — это и есть дефект оригинала.\n")
		TEXT("1 = знаменатель = число записей таблицы (процент никогда не превысит 100)."),
		ECVF_Default);

	// ★★★ ДВА ЭФФЕКТА ЗВАНИЯ НА ПЕРСОНАЖЕ (2026-08-30). Ключ ОТКАТА, по умолчанию ВКЛЮЧЕНО —
	//   это восстановление эталонного поведения, а не отступление.
	ASDA_DEV_CVAR(int32, CVarCharFx,
		TEXT("asda.Title.CharFx"), 1,
		TEXT("Эффект звания НА ПЕРСОНАЖЕ (эталон, attach-6, звука нет).\n")
		TEXT("1 (по умолчанию) = как в оригинале: 6085 «найдено» → ex_mark_003.jeb (восклицательный\n")
		TEXT("знак, ~2.1 с), 6065 «получено» → 0018.jeb (шапка выпускника, 5.0 с). Оба спавнятся\n")
		TEXT("ДО гейта «свой игрок», то есть видны и над чужим персонажем.\n")
		TEXT("0 = не спавнить (диагностика: отделить эффект от баннера и значка HUD)."),
		ECVF_Default);

	// Имена .jeb — ДОСЛОВНО с эталона, менять нельзя (закон never-invent-only-original).
	const TCHAR* kJebFound   = TEXT("ex_mark_003");   // 6085, network.c:31468
	const TCHAR* kJebGranted = TEXT("0018");          // 6065, ui_core.c:82321

	/** Найти виджет по точному имени. ★ Через общую точку `AsdaUi::FindNodeExact` (`fix_helpers_dedup.md`). */
	template <typename T>
	T* FindW(UUserWidget* Host, const FString& Name)
	{
		return Cast<T>(AsdaUi::FindNodeExact(Host, *Name));
	}

	/** Найти ОДИН виджет, чьё имя начинается с префикса (планы импортёра `idc_<seq>_<plane>`). */
	UWidget* FindByPrefix(UUserWidget* Host, const FString& Prefix)
	{
		if (!Host || !Host->WidgetTree) { return nullptr; }
		TArray<UWidget*> All;
		Host->WidgetTree->GetAllWidgets(All);
		for (UWidget* W : All)
		{
			if (W && W->GetName().StartsWith(Prefix, ESearchCase::CaseSensitive)) { return W; }
		}
		return nullptr;
	}

	/** Собрать TextBlock'и поддерева в порядке обхода (DFS). */
	void CollectTextsDfs(UWidget* W, TArray<UTextBlock*>& Out)
	{
		if (!W) { return; }
		if (UTextBlock* Tb = Cast<UTextBlock>(W)) { Out.Add(Tb); }
		if (UPanelWidget* P = Cast<UPanelWidget>(W))
		{
			for (int32 i = 0; i < P->GetChildrenCount(); ++i) { CollectTextsDfs(P->GetChildAt(i), Out); }
		}
	}

	// ★ Волна 1 (14.09.2026, В3): правило видимости — общая точка `AsdaUi::SetShownDecor` (режим тот же).
	void SetVis(UWidget* W, bool bOn)
	{
		AsdaUi::SetShownDecor(W, bOn);
	}

	/** Обернуть текст inline-тегом цвета — родной способ оригинала (`"<FC=%08x>%s</F>"`). */
	FString Colored(uint32 Argb, const FString& S)
	{
		return FString::Printf(TEXT("<FC=%08X>%s</F>"), Argb, *S);
	}
}

void UAsdaTitleSubsystem::RegisterZone()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// Рамка окна — девять планов `Title_Win_0_0…8` (скин `Win_Mg`), прямоугольник по ЖИВОЙ геометрии.
	// ★ Волна 3: зона по планам рамки — общая точка `FAsdaJuiWindowHandle::RegisterFrameZone` (`!bOpen || !Widget` → нет зоны).
	FAsdaJuiWindowHandle::RegisterFrameZone(*WM, kWindowId, this, Widget, bOpen, TEXT("Title_Win_0_%d"), 9);
	// Колесо: эталон отдаёт его полосе 18735 (`uiControlSetScrollPage(…, 1, 1)` — один ряд за щелчок).
	WM->RegisterWheelHandler(kWindowId, FAsdaUiWheelHandler::CreateWeakLambda(this,
		[this](int32 Notches) -> bool
		{
			if (!bOpen || !bControlsReady) { return false; }
			const int32 MaxTop = FMath::Max(0, Order.Num() - kRows);
			const int32 New = FMath::Clamp(ScrollTop - Notches, 0, MaxTop);
			if (New == ScrollTop) { return true; }
			ScrollTop = New;
			RefreshList();
			LayoutScrollBar();
			return true;
		}));
}

void UAsdaTitleSubsystem::SetupControls()
{
	bControlsReady = false;
	if (!Widget || !Widget->WidgetTree) { return; }
	RootCanvas = Cast<UCanvasPanel>(Widget->GetRootWidget());

	// ── Кнопки ──
	BtnClose   = FindW<UButton>(Widget, TEXT("Title_Win_CloseBtn"));    // 18703
	BtnApply   = FindW<UButton>(Widget, TEXT("Title_List_AppBtn"));     // 18737
	BtnDelete  = FindW<UButton>(Widget, TEXT("Title_List_DelBtn"));     // 18738
	BtnOk      = FindW<UButton>(Widget, TEXT("Title_BtnOk"));           // 18727
	BtnCancel  = FindW<UButton>(Widget, TEXT("Title_BtnCancel"));       // 18728
	BtnRelease = FindW<UButton>(Widget, TEXT("Title_ReleaseBtn"));      // 18712
	// ── 18713: переключатель `TabBar_W108_3Btn` — импортёр развернул его в ТРИ кнопки `_OPT0…2`,
	//    ТРИ плана-картинки `_212_0…2` и три подписи `_Text` / `_Text2` / `_Text3`.
	//    Кадровые состояния (покой / наведение / выбрано) живут В СКИНЕ (закон `jui-caption-states`),
	//    в код они попадают через `FButtonStyle` кнопки — их и раскладывает `AsdaUi::ApplyTabState`.
	TabButtons.Reset(); TabPlanes.Reset(); TabCaptions.Reset();
	for (int32 i = 0; i < 3; ++i)
	{
		TabButtons.Add(FindW<UButton>(Widget, FString::Printf(TEXT("Title_ClassSelectBtn_OPT%d"), i)));
		TabPlanes.Add(Cast<UImage>(FindByPrefix(Widget, FString::Printf(TEXT("Title_ClassSelectBtn_212_%d"), i))));
		TabCaptions.Add(FindW<UTextBlock>(Widget, i == 0
			? FString(TEXT("Title_ClassSelectBtn_Text"))
			: FString::Printf(TEXT("Title_ClassSelectBtn_Text%d"), i + 1)));
	}
	TabPlaneIdle.Reset(); TabBtnIdle.Reset(); TabCaptionIdle.Reset();

	// ── Шапка славы ──
	TxtRanking     = FindW<UTextBlock>(Widget, TEXT("Title_Ranking"));              // 18704
	TxtTotalPoint  = FindW<UTextBlock>(Widget, TEXT("Title_TotalPoint"));           // 18705
	TxtTotalRate   = FindW<UTextBlock>(Widget, TEXT("Title_TotalGathRate"));        // 18706
	TxtRankDelta   = FindW<UTextBlock>(Widget, TEXT("Title_Ranking_Change"));       // 188480
	TxtPointDelta  = FindW<UTextBlock>(Widget, TEXT("Title_TotalPoint_Change"));    // 188510
	TxtRateDelta   = FindW<UTextBlock>(Widget, TEXT("Title_TotalGathRate_Change")); // 188540
	DeltaArrows.Reset();
	for (const TCHAR* P : { TEXT("Title_Ranking_UpArr_"),        TEXT("Title_Ranking_DnArr_"),
	                        TEXT("Title_TotalPoint_UpArr_"),     TEXT("Title_TotalPoint_DnArr_"),
	                        TEXT("Title_TotalGathRate_UpArr_"),  TEXT("Title_TotalGathRate_DnArr_") })
	{
		DeltaArrows.Add(FindByPrefix(Widget, P));
	}

	CaptureRich(RsFirstTitle,  FindW<UTextBlock>(Widget, TEXT("Title_1stTitle_Name")));   // 18708
	CaptureRich(RsSecondTitle, FindW<UTextBlock>(Widget, TEXT("Title_2ndTitle_Name")));   // 18710
	TxtCharName    = FindW<UTextBlock>(Widget, TEXT("Title_Title_CharName"));  // 18711
	TxtTabRate     = FindW<UTextBlock>(Widget, TEXT("Title_ClassGathRate"));   // 18714
	TxtLeftTimeCap = FindW<UTextBlock>(Widget, TEXT("Title_LeftTime_Txt"));    // 18725
	TxtLeftTime    = FindW<UTextBlock>(Widget, TEXT("Title_LeftTime"));        // 18726

	// ── Инфо-панель ──
	TxtInfoCap       = FindW<UTextBlock>(Widget, TEXT("Title_InfoTitleTxt"));    // 18715
	CaptureRich(RsInfoName, FindW<UTextBlock>(Widget, TEXT("Title_Info_Name")));  // 18716
	TxtInfoGrade     = FindW<UTextBlock>(Widget, TEXT("Title_Info_Grade"));      // 18717
	TxtInfoPoint     = FindW<UTextBlock>(Widget, TEXT("Title_Info_Point"));      // 18718
	TxtInfoLocation  = FindW<UTextBlock>(Widget, TEXT("Title_Info_Location"));   // 18724
	TxtInfoRandom    = FindW<UTextBlock>(Widget, TEXT("Title_Info_Random"));     // 188580
	TxtInfoType      = FindW<UTextBlock>(Widget, TEXT("Title_Info_Type"));       // 188570
	TxtInfoTypeName  = FindW<UTextBlock>(Widget, TEXT("Title_Info_Type_Name"));  // 188590
	TxtInfoCondition = FindW<UTextBlock>(Widget, TEXT("Title_Info_Condition"));  // 18723
	// ★ 18723 `NoImage_Clip` 178×77 — авторский слот рассчитан на ПЯТЬ строк по 14 px, и эталон
	//   печатает в него длинное предложение (`TextContent` в `.JUI`). Ширину переноса фиксируем
	//   ПО СЛОТУ ИЗ АССЕТА (закон `ui-autowrap-canvas-gotcha`: автоперенос в канвасе берёт ширину
	//   из кэша прошлого кадра и даёт дёрганье), обрезку по символам не включаем.
	if (TxtInfoCondition)
	{
		const FVector2D Sz = SlotSize(TxtInfoCondition);
		TxtInfoCondition->SetAutoWrapText(false);
		if (Sz.X > 1.f) { TxtInfoCondition->SetWrapTextAt((float)Sz.X); }
		UE_LOG(LogAsdaTitle, Log, TEXT("[звания] 18723 «условие получения»: слот %.0fx%.0f, перенос по %.0f px"),
			Sz.X, Sz.Y, Sz.X);
	}
	RsInfoOption.Reset(); RsInfoOption.SetNum(kOptionSlots);
	for (int32 i = 0; i < kOptionSlots; ++i)
	{
		CaptureRich(RsInfoOption[i], FindW<UTextBlock>(Widget, FString::Printf(TEXT("Title_Info_Option%d"), i + 1)));
	}

	// ── Заголовки колонок (кликабельны: сортировка) ──
	ColumnCaptions.Reset(); ColumnZones.Reset();
	const TCHAR* ColIdc[7] = { TEXT("Title_TNum"), TEXT("Title_TCategory"), TEXT("Title_TTitleName"),
	                           TEXT("Title_TTitlePosition"), TEXT("Title_TGrade"), TEXT("Title_TPoint"),
	                           TEXT("Title_TState") };
	for (int32 i = 0; i < 7; ++i)
	{
		ColumnCaptions.Add(FindW<UTextBlock>(Widget, FString(ColIdc[i]) + TEXT("_Text")));
		ColumnZones.Add(FindByPrefix(Widget, FString(ColIdc[i]) + TEXT("_")));
	}

	// ── 12 строк списка ──
	//   ★ ЗАКОН `jui-uneven-node-layout` + `jui-flattened-hierarchy-geometry`: имена ячеек колонки
	//     «Позиц.» импортёр выдал НЕОДНОРОДНО (`Title_TTitlePosition01`, `…03`, `…04`, `…04_1`, …),
	//     поэтому семь текстов ряда берём ОБХОДОМ ПОДДЕРЕВА в порядке DFS — он совпадает с
	//     порядком колонок (№ · Категория · Имя · Позиц. · Ранг · Очки · Состояние).
	Rows.Reset(); Rows.SetNum(kRows);
	RowPlanes.Reset();
	int32 CellsOk = 0;
	for (int32 R = 0; R < kRows; ++R)
	{
		const FString NN = FString::Printf(TEXT("%02d"), R + 1);
		FRowWidgets& RW = Rows[R];
		RW.Group = Widget->GetWidgetFromName(FName(*FString::Printf(TEXT("Title_ListBox%s_grp"), *NN)));
		if (RW.Group)
		{
			TArray<UTextBlock*> Texts;
			CollectTextsDfs(RW.Group, Texts);
			for (int32 C = 0; C < 7 && C < Texts.Num(); ++C) { CaptureRich(RW.Cells[C], Texts[C]); ++CellsOk; }
			if (Texts.Num() != 7)
			{
				UE_LOG(LogAsdaTitle, Warning,
					TEXT("[звания] ряд %d: текстов в поддереве %d вместо 7 — колонки могут разъехаться"),
					R + 1, Texts.Num());
			}
		}
		RW.Sel  = FindByPrefix(Widget, FString::Printf(TEXT("Title_ListBox_Sel%s_"), *NN));
		RW.Find = FindByPrefix(Widget, FString::Printf(TEXT("Title_List_Find%s_"),  *NN));
		RW.Lose = FindByPrefix(Widget, FString::Printf(TEXT("Title_List_Lose%s_"),  *NN));
		// ⚠ у рядов 11 и 12 импортёр назвал плашку `Title_NoticeBar011_…` / `…012_…` (лишний ноль).
		RW.Bar  = FindByPrefix(Widget, FString::Printf(TEXT("Title_NoticeBar%s_"), *NN));
		if (!RW.Bar) { RW.Bar = FindByPrefix(Widget, FString::Printf(TEXT("Title_NoticeBar0%s_"), *NN)); }
		for (UWidget* W : { RW.Sel, RW.Find, RW.Lose, RW.Bar }) { if (W) { RowPlanes.Add(W); } }
	}

	// 18736 — зелёная рамка выбора (8 планов `Title_ListSelected_187_0…7`).
	SelectedFrame.Reset();
	for (int32 i = 0; i < 8; ++i)
	{
		if (UWidget* W = FindByPrefix(Widget, FString::Printf(TEXT("Title_ListSelected_187_%d"), i)))
		{
			SelectedFrame.Add(W);
		}
	}

	// 9 планов рамки окна — для зоны интерфейса и дампов.
	FramePlanes.Reset();
	for (int32 i = 0; i < 9; ++i)
	{
		if (UWidget* W = FindByPrefix(Widget, FString::Printf(TEXT("Title_Win_0_%d"), i))) { FramePlanes.Add(W); }
	}

	// ── Полоса прокрутки 18735: в .JUI её контрола НЕТ, оригинал создаёт её в рантайме из скина
	//    колёсного окна (`ScrollBarFileNameVert = "Nomal_Scr"`), поэтому строим общей точкой.
	if (RootCanvas) { ScrollBar.Build(Widget, RootCanvas, TEXT("Title_Wheel"), /*ZTrack*/ 30); }

	// ── Привязка кликов ──
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, MoveTemp(Fn), Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnClose,   18703, [this](int32) { CloseTitleWindow(); });
	Bind(BtnApply,   18737, [this](int32) { OnApplyClicked(); });
	Bind(BtnDelete,  18738, [this](int32) { OnDeleteClicked(); });
	Bind(BtnOk,      18727, [this](int32) { OnOkClicked(); });
	Bind(BtnCancel,  18728, [this](int32) { OnCancelClicked(); });
	Bind(BtnRelease, 18712, [this](int32) { OnReleaseClicked(); });
	for (int32 i = 0; i < TabButtons.Num(); ++i)
	{
		Bind(TabButtons[i], 18713, [this, i](int32) { SelectTab(i); });
	}

	// ── Статичные подписи (эталон ставит их в `CUITitle_InitControls`) ──
	auto SetLoc = [this](UTextBlock* Tb, int32 Id) { if (Tb) { SetTxt(Tb, Loc(Id)); } };
	SetLoc(FindW<UTextBlock>(Widget, TEXT("Title_ReleaseBtn_Text")),      TLoc::kRelease);
	SetLoc(FindW<UTextBlock>(Widget, TEXT("Title_List_AppBtn_Text")),     TLoc::kBtnApply);
	SetLoc(FindW<UTextBlock>(Widget, TEXT("Title_List_DelBtn_Text")),     TLoc::kBtnDelete);
	SetLoc(FindW<UTextBlock>(Widget, TEXT("Title_BtnOk_Text")),           TLoc::kBtnOk);
	SetLoc(FindW<UTextBlock>(Widget, TEXT("Title_BtnCancel_Text")),       TLoc::kBtnCancel);
	// Подписи вкладок ставит `RefreshTabState` — там же, где кадр и цвет, ОДНИМ проходом
	//   (иначе между установкой текста и установкой кадра проскакивает лишнее состояние).
	for (int32 i = 0; i < TabCaptions.Num(); ++i) { SetLoc(TabCaptions[i], TLoc::kTab0 + i); }
	SetLoc(TxtInfoCap,     TLoc::kInfoCap);
	SetLoc(TxtLeftTimeCap, TLoc::kLeftTimeCap);
	SetLoc(TxtLeftTime,    TLoc::kLeftTimeNone);
	const int32 ColLoc[7] = { TLoc::kColNum, TLoc::kColCategory, TLoc::kColName, TLoc::kColPosition,
	                          TLoc::kColGrade, TLoc::kColPoint, TLoc::kColState };
	for (int32 i = 0; i < 7; ++i) { SetLoc(ColumnCaptions[i], ColLoc[i]); }

	for (const TObjectPtr<UWidget>& W : SelectedFrame) { SetVis(W, false); }
	for (const TObjectPtr<UWidget>& W : RowPlanes)     { SetVis(W, false); }

	bControlsReady = (Rows.Num() == kRows && Rows[0].Group != nullptr);
	UE_LOG(LogAsdaTitle, Log,
		TEXT("[звания] контролы окна 18700 разобраны: кнопок %d/6, вкладок %d/3, ячеек списка %d/%d, ")
		TEXT("бонусов %d/6, колонок %d/7, планов рамки %d/9, рамка выбора %d/8, полоса=%s"),
		(BtnClose ? 1 : 0) + (BtnApply ? 1 : 0) + (BtnDelete ? 1 : 0) + (BtnOk ? 1 : 0)
			+ (BtnCancel ? 1 : 0) + (BtnRelease ? 1 : 0),
		TabButtons[0] && TabButtons[1] && TabButtons[2] ? 3 : 0,
		CellsOk, kRows * 7,
		RsInfoOption.Num(), ColumnCaptions.Num(), FramePlanes.Num(), SelectedFrame.Num(),
		ScrollBar.IsBuilt() ? TEXT("да") : TEXT("НЕТ"));
}

// ═════════════════════════════ ЗНАЧОК-УВЕДОМЛЕНИЕ HUD ═════════════════════════════════════════
void UAsdaTitleSubsystem::SetupNoticeWidget()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM || (WM->IsWindowLive(NoticeWidget) && NoticeBg)) { return; }
	NoticeWidget = WM->OpenWindowByPath(kNoticeId, kNoticeWbp, /*Z*/ 300, ESlateVisibility::SelfHitTestInvisible);
	if (!NoticeWidget)
	{
		UE_LOG(LogAsdaTitle, Error, TEXT("[звания] WBP_Title_Notice_BG не загрузился (%s)"), kNoticeWbp);
		return;
	}
	NoticeBg   = FindByPrefix(NoticeWidget, TEXT("Title_Notice_BG_"));
	NoticeLose = FindByPrefix(NoticeWidget, TEXT("Title_Notice_Lose_"));
	NoticeFind = FindByPrefix(NoticeWidget, TEXT("Title_Notice_Find_"));
	SetVis(NoticeBg, false); SetVis(NoticeLose, false); SetVis(NoticeFind, false);
	UE_LOG(LogAsdaTitle, Log, TEXT("[звания] значок 50000 поднят: BG=%s Lose=%s Find=%s"),
		NoticeBg ? TEXT("да") : TEXT("НЕТ"), NoticeLose ? TEXT("да") : TEXT("НЕТ"),
		NoticeFind ? TEXT("да") : TEXT("НЕТ"));
}

void UAsdaTitleSubsystem::ShowNoticeIcon(int32 IconKind, int32 TipKind)
{
	SetupNoticeWidget();
	NoticeIconKind = IconKind;
	NoticeTipKind  = TipKind;
	bNoticeActive  = true;
	UE_LOG(LogAsdaTitle, Log, TEXT("[звания] значок уведомления зажжён: иконка=%s подсказка=%d"),
		IconKind == 1 ? TEXT("найдено") : TEXT("потеряно"), TipKind);
}

bool UAsdaTitleSubsystem::IsNoticeIconShown() const
{
	// `sub_D97230`: видимость контрола 50000. Фон виден ровно при `+880` (см. `UpdateBlink`) — окно значка должно жить.
	return bNoticeActive && NoticeWidget && NoticeBg && AsdaUi::IsShown(NoticeBg);
}

void UAsdaTitleSubsystem::HideNoticeIconForCloseAll()
{
	if (!bNoticeActive) { return; }
	bNoticeActive = false;                                           // `set_o370`: `[880] = 0`
	SetVis(NoticeBg, false); SetVis(NoticeLose, false); SetVis(NoticeFind, false);
	HideHoverTip();
	UE_LOG(LogAsdaTitle, Log, TEXT("[звания] значок 50000 погашен «закрыть всё» (sub_924419 = set_o370)"));
}

void UAsdaTitleSubsystem::ShowNoticeIconFromSnapshot()
{
	SetupNoticeWidget();
	bNoticeActive = true;                                            // `set_o370_7470`: `[880] = 1`
	UpdateBlink();                                                   // перерисовка сразу (эталон — ближайшим тиком мигания)
	UE_LOG(LogAsdaTitle, Log, TEXT("[звания] значок 50000 возвращён снимком окон (sub_934373 = set_o370_7470), вид=%d"), NoticeIconKind);
}

// ★★★ ДВА ЭФФЕКТА ЗВАНИЯ НА ПЕРСОНАЖЕ. Общая точка 6065 и 6085 — в эталоне это ДВА ДОСЛОВНЫХ
//   БЛИЗНЕЦА (`ui_core.c:82313-82322` и `network.c:31456-31468`), различающихся ровно ИМЕНЕМ .jeb:
//     …_FindByUserIdx(g_CharManager, userIdx) → если персонаж найден:
//       genderBoneIdx = List_FindIndexByName(model, gender == 1 ? "M" : "G");   // ★ см. ниже
//       pos = CObj_GetPosition(char->gap_1B0);
//       Effect_SpawnByName(g_pEffectMgr, "<имя>.jeb", 1, pos.x, pos.y, pos.z,
//                          char->effectSlots[100].field_4, char->posVec, 0, 6, 0, 1.0f);
//     …и ТОЛЬКО ПОСЛЕ этого — `if (self->userIdx == charId) { …окно, маска, значок… }`.
//
// ★ ДЕФЕКТ ОРИГИНАЛА (мёртвый код, не переносим): `genderBoneIdx` вычисляется В ОБОИХ хендлерах
//   («M» у мужчин, «G» у женщин) и НИ РАЗУ не используется — в `Effect_SpawnByName` он не передаётся.
//   То есть привязки к кости у этих двух эффектов нет НИ В ОРИГИНАЛЕ; автор её задумывал, но не
//   довёл. Мы повторяем фактическое поведение (attach-6, без кости), а не задуманное.
// ★ ЗВУКА НЕТ (доказано двумя методами — см. заголовок `UAsdaEffectsSubsystem::SpawnTitleFx`).
void UAsdaTitleSubsystem::SpawnTitleEffect(int32 UserIdx, bool bFound)
{
	if (CVarCharFx.GetValueOnGameThread() == 0) { return; }          // ключ отката
	if (UserIdx < 0) { return; }
	UAsdaEffectsSubsystem* Fx =
		GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	if (!Fx) { return; }
	Fx->SpawnTitleFx(UserIdx, bFound ? FString(kJebFound) : FString(kJebGranted));
}

// Порт `uiQuickbarUpdateBlink@0xE9A090`: мигание маркеров рядов (500 мс) и значка HUD (1000 мс).
void UAsdaTitleSubsystem::UpdateBlink()
{
	const double Now = FPlatformTime::Seconds();
	if (Now - LastBlinkS >= 0.5) { LastBlinkS = Now; bBlinkPhase = !bBlinkPhase; }

	if (bOpen && bControlsReady)
	{
		for (int32 R = 0; R < Rows.Num(); ++R)
		{
			const int32 Id = RowTitleId.IsValidIndex(R) ? RowTitleId[R] : -1;
			const bool bNew  = (Id >= 0) && FlagNew.Contains(Id);
			const bool bLost = (Id >= 0) && FlagLost.Contains(Id);
			SetVis(Rows[R].Find, bNew  && !bBlinkPhase);
			SetVis(Rows[R].Lose, bLost && !bBlinkPhase);
			SetVis(Rows[R].Bar, (bNew || bLost) && !bBlinkPhase);
		}
	}

	if (!NoticeWidget) { return; }
	if (!bNoticeActive)
	{
		SetVis(NoticeBg, false); SetVis(NoticeLose, false); SetVis(NoticeFind, false);
		return;
	}
	// ★★★ ФОН ЗНАЧКА ОБЯЗАН ЛОВИТЬ МЫШЬ (2026-08-30, замечание владельца «иконка ничего не делает»).
	//   `SetVis` ставит `SelfHitTestInvisible` — при нём САМ виджет мышь не ловит, а значит клик по
	//   значку не попадал никуда ([[selfhittest-vs-hittest-invisible]]). Фону нужен именно `Visible`;
	//   двум глифам-состояниям — нет, они остаются `SelfHitTestInvisible` и клик не съедают.
	if (NoticeBg) { NoticeBg->SetVisibility(ESlateVisibility::Visible); }
	// Эталон: `+223 == 0` → мигает «потеряно» (50001), `== 1` → мигает «найдено» (50002).
	SetVis(NoticeLose, NoticeIconKind == 0 && !bBlinkPhase);
	SetVis(NoticeFind, NoticeIconKind == 1 && !bBlinkPhase);
}

// ★★★ КЛИК И НАВЕДЕНИЕ НА ЗНАЧОК HUD — работает при ЗАКРЫТОМ окне, поэтому живёт отдельно от
//   `TickMouse` (та гейтится `bOpen`). Эталон — `questLog_OnUiEvent`:
//     case 0x8 (наведение): `tutorialPopupLayoutSmall(&g_ItemTooltip, loc(20504), loc(20500 + вид))`
//                           для `a2 == 50000` при взведённом `+880`;
//     case 0xB (клик):      `if (a2 == 50000 && +880 && !sub_91E005()) { questLogWnd_Open(); KillFocus(tip); }`
//                           `if (a2 == 50000 || 50001 || 50002) sub_93E3CD(this);`  ← гашение значка
//   ★ Порядок эталона важен и воспроизведён дословно: сперва ОТКРЫТИЕ (оно гейтится модальным
//     окном — `sub_E9C0A0` = «видим модальный msgbox»), потом БЕЗУСЛОВНОЕ гашение значка. То есть
//     даже если открыть окно не дали, значок всё равно гаснет.
//   ★ `questLogWnd_Open` в эталоне — ТОГГЛ (`if (окно видимо) return uiTitleHide(this)`), ровно как
//     наш `OpenTitleWindow()`. Своей ветки «обновить вместо закрытия» НЕ заводим: при открытом окне
//     список и так перестраивают сами обработчики 6065/6085.
//   ★ МАРКЕРЫ СТРОК ЗДЕСЬ НЕ ТРОГАЕМ. В эталоне байты `+16`/`+17` записи звания гасит ТОЛЬКО клик
//     ПО СТРОКЕ (`uiQuickbarSelectSlot@0xE99EE0`), а не клик по значку. Гасить их здесь означало бы
//     стереть ровно то мигание нового звания, ради которого окно и открывают.
void UAsdaTitleSubsystem::TickNoticeIcon()
{
	if (!bNoticeActive || !NoticeBg) { return; }
	if (!AsdaUi::IsCursorOverWidget(NoticeBg)) { return; }

	// Наведение: подсказка «шапка 20504 + текст 20500…20503 по виду значка» (эталон case 0x8).
	ShowHoverTip(TLoc::kNoticeCap, TLoc::kNoticeNew + FMath::Clamp(NoticeTipKind, 0, 3));
	if (!Mouse.bLmbDown) { return; }
	OnNoticeIconClicked();
}

// ★ ДЕЙСТВИЕ клика по значку, отдельно от ПОПАДАНИЯ по нему. Разделено намеренно: так
//   консольная проверка (`asda.Title.Click notice`) жмёт ТЕ ЖЕ ветки, что и мышь, и второй
//   реализации не заводится (закон `diagnostic-must-use-real-path`).
void UAsdaTitleSubsystem::OnNoticeIconClicked()
{
	if (!bNoticeActive) { return; }
	// Гейт эталона `!sub_91E005()`: при видимом модальном msgbox окно не открывается.
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	const bool bModal = WM && (WM->IsWindowVisible(FName(TEXT("ONEBUT_MSGBOX")))
	                        || WM->IsWindowVisible(FName(TEXT("TWOBUT_MSGBOX"))));
	if (!bModal) { OpenTitleWindow(); }

	// Гашение значка — БЕЗУСЛОВНОЕ, как в эталоне (`sub_93E3CD` вне гейта открытия).
	bNoticeActive = false;
	SetVis(NoticeBg, false); SetVis(NoticeLose, false); SetVis(NoticeFind, false);
	HideHoverTip();
	UE_LOG(LogAsdaTitle, Log,
		TEXT("[звания] клик по значку 50000: окно %s, значок ПОГАШЕН (маркеры строк не трогаем)"),
		bModal ? TEXT("НЕ открыто — виден модальный msgbox (гейт эталона)")
		       : (bOpen ? TEXT("ОТКРЫТО") : TEXT("ЗАКРЫТО тогглом, как в эталоне")));
}

// ═════════════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ════════════════════════════════════════════
bool UAsdaTitleSubsystem::OpenTitleWindow()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }

	// ★ ГЕЙТЫ `questLogWnd_Open` (quest.c:8526-8533): `helper_getField2075(g_pShopWnd)` · окно ремесла 76000 ·
	//   окно изготовления 77500 — все МОЛЧА.
	//   ★ Поправка волны В3 ремесла (ревизия В0–В2 С-1): второй и третий гейты — `tileInteract_isControlVisible(
	//   g_pItemMainWnd)` и `isBoundControlVisible(g_pItemQtyMakeDlg)`; прежний порт искал несуществующее окно
	//   `ITEM_MAIN_WIN` (гейт не срабатывал никогда) и нумпад `NUMPAD_WIN` (ложно блокировал звания при открытом
	//   нумпаде). Теперь — единый предикат ремесла.
	//   ⚠ Первый гейт оставлен как был (`NPCSHOP_WIN`); `helper_getField2075(g_pShopWnd)` в разборах ремесла и NPC —
	//   ЭЛИТНЫЙ магазин (поле 2075), а не магазин NPC: перепроверить отдельной задачей (вне объёма волны).
	static const FName kShop(TEXT("NPCSHOP_WIN"));
	if (WM->IsWindowVisible(kShop))
	{
		UE_LOG(LogAsdaTitle, Log, TEXT("[звания] открытие отклонено: открыто окно «%s» (гейт эталона)"), *kShop.ToString());
		return false;
	}
	if (UAsdaCraftSubsystem::IsAnyCraftWindowOpen(GI))
	{
		UE_LOG(LogAsdaTitle, Log, TEXT("[звания] открытие отклонено молча: открыто окно ремесла 76000/77500 (quest.c:8530/8533)"));
		return false;
	}

	if (!EnsureWidget()) { return false; }

	// Тоггл: открытое окно закрываем (эталон `if (win->visible == 1) return uiTitleHide(this)`).
	if (bOpen)
	{
		CloseTitleWindow();
		return true;
	}

	// Эталон: при открытии сбрасывает выбор, скролл и сортирует по колонке «Состояние».
	bSortDesc = false;
	SortColumn = 188350;
	SelectedRow = -1;
	ScrollTop = 0;

	if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
	{
		// Эталон берёт ранг и очки прямо из полей персонажа; у нас они приходят пакетами
		//   6068 (ранг) и 6066/6065/6070 (очки) — сюда только копируем текущее.
		(void)Net;
	}

	bOpen = true;
	WM->SetWindowVisible(kWindowId, true);
	WM->BringWindowToFront(kWindowId);

	RefreshSelfTitles();
	ClearInfoPanel();
	SelPrefix = WornPrefix;
	SelSuffix = WornSuffix;
	RebuildOrder();
	RefreshList();
	RefreshHeader();
	RefreshTabState();
	LayoutScrollBar();
	for (const TObjectPtr<UWidget>& W : SelectedFrame) { SetVis(W, false); }

	UE_LOG(LogAsdaTitle, Log,
		TEXT("[звания] окно 18700 открыто: вкладка=%d, званий во вкладке %d, получено %d, найдено %d, ")
		TEXT("надето (приставка=%d, окончание=%d), очки=%d, ранг=%d"),
		Tab, Order.Num(),
		[this] { int32 N = 0; for (const FAsdaTitleDef& D : Defs) { if (IsOwned(D.Id)) { ++N; } } return N; }(),
		[this] { int32 N = 0; for (const FAsdaTitleDef& D : Defs) { if (IsFound(D.Id)) { ++N; } } return N; }(),
		WornPrefix, WornSuffix, FamePoints, FameRank);
	return true;
}

void UAsdaTitleSubsystem::CloseTitleWindow()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kWindowId, false);
	}
	HideHoverTip();
	bOpen = false;
	UE_LOG(LogAsdaTitle, Log, TEXT("[звания] окно 18700 закрыто"));
}

// ═════════════════════════════ СПИСОК ═════════════════════════════════════════════════════════
// Порт `questLog_SortCategoryByColumn@0xE9CC80`: пересобирает ПОРЯДОК ПОКАЗА по ключу колонки.
void UAsdaTitleSubsystem::RebuildOrder()
{
	Order.Reset();
	// ★ Счётчики «Собрано %» вкладки. Эталон (`QuestLog_PopulateCategoryList@0xE98C00`) наращивает
	//   `totalCount` ПОСЛЕ блока показа — то есть для КАЖДОЙ записи, прошедшей гейты
	//   Group(+84) / Enabled(+200) / ClassReq(+12), независимо от того, показана строка или нет.
	//   `completedCount` растёт только у полученных. Считаем их здесь: `Order` ниже отсеивает
	//   неполученные строки, поэтому по нему знаменатель считать НЕЛЬЗЯ (см. §«Собрано %»).
	TabTotalCount = 0;
	TabOwnedCount = 0;
	for (const FAsdaTitleDef& D : Defs)
	{
		if (D.Group != Tab || D.Enabled == 0) { continue; }
		// Гейт по классу (эталон: `if (rec+12 > 0)` — иначе строка видна всем).
		if (D.ClassReq > 0)
		{
			const int32 Cls = [this]() -> int32
			{
				const UAsdaNetSubsystem* N = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
				return N ? AsdaSelf::GetClassId(N) : 0;   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U5d): класс — общая точка `AsdaSelf::GetClassId` (байт +0x438, фолбэк — запись char-select rec+24) (было сырое −1 до 4005)
			}();
			bool bMatch = false;
			if (D.ClassReq == 1 && Cls > 0 && Cls <= 4)   { bMatch = true; }
			if (D.ClassReq == 2 && Cls >= 12 && Cls <= 15) { bMatch = true; }
			if (D.ClassReq == 3 && Cls >= 23 && Cls <= 26) { bMatch = true; }
			if (!bMatch) { continue; }
		}
		// ★ Знаменатель/числитель считаем ДО фильтра показа — ровно как эталонные
		//   `++totalCount` / `++completedCount`, которые стоят вне `if (showRow)`.
		++TabTotalCount;
		if (IsOwned(D.Id)) { ++TabOwnedCount; }
		// Вкладка «Секретные» (2) показывает ВСЁ; остальные — только полученное либо найденное.
		if (Tab != 2 && !IsOwned(D.Id) && !IsFound(D.Id)) { continue; }
		Order.Add(D.Id);
	}

	// Ключи сортировки — 1:1 из эталона (там они умножаются на 100000, у нас — сравнение полей).
	const int32 Col = SortColumn;
	Order.Sort([this, Col](int32 A, int32 B)
	{
		const FAsdaTitleDef* DA = FindDef(A);
		const FAsdaTitleDef* DB = FindDef(B);
		if (!DA || !DB) { return A < B; }
		auto State = [this](const FAsdaTitleDef& D) { return IsOwned(D.Id) ? 2 : (IsFound(D.Id) ? 1 : 0); };
		auto NameKey = [](const FAsdaTitleDef& D)
		{
			// Эталон складывает ПЕРВЫЕ 10 БАЙТ имени как знаковые char.
			int32 S = 0;
			// ★ [ПОРТ] 08.10.2026: было `StringCast<ANSICHAR>` — кириллица превращалась в '?', и сумма не совпадала с эталоном
			//   (у него байты cp1251). Ключ локальный (не провод), поэтому НАСТОЯЩИЙ cp1251.
			uint8 Ansi[10] = {};
			const int32 N = FAsdaLoginData::EncodeCp1251Strict(D.RawName, Ansi, 10);
			for (int32 i = 0; i < N; ++i) { S += (int32)(int8)Ansi[i]; }
			return S;
		};
		int64 KA = 0, KB = 0;
		switch (Col)
		{
			case 18729: KA = DA->Id;           KB = DB->Id;           break;
			case 18730: KA = DA->CategoryLoc;  KB = DB->CategoryLoc;  break;
			case 18731: KA = NameKey(*DA);     KB = NameKey(*DB);     break;
			case 18732: KA = DA->Position;     KB = DB->Position;     break;
			case 18733: KA = (int64)DA->Grade * 1000 + DA->Point;
			            KB = (int64)DB->Grade * 1000 + DB->Point;     break;
			case 18734: KA = DA->Point;        KB = DB->Point;        break;
			default:    KA = State(*DA);       KB = State(*DB);       break;   // 188350
		}
		if (KA != KB) { return bSortDesc ? (KA > KB) : (KA < KB); }
		return DA->Id < DB->Id;   // стабильность (у эталона — разведение ключей на ±1)
	});

	ScrollTop = FMath::Clamp(ScrollTop, 0, FMath::Max(0, Order.Num() - kRows));
}

// Порт `QuestLog_PopulateCategoryList@0xE98C00`.
void UAsdaTitleSubsystem::RefreshList()
{
	RowTitleId.Reset(); RowTitleId.SetNumZeroed(kRows);
	for (int32 i = 0; i < kRows; ++i) { RowTitleId[i] = -1; }
	if (!bControlsReady) { return; }

	// ★ ИСПРАВЛЕНО (дефект ПОРТА, не оригинала): знаменатель — ВСЕ звания вкладки
	//   (`TabTotalCount`, эталонный `totalCount`), а не число ПОКАЗАННЫХ строк.
	//   Порт считал по `Order`, а `Order` для вкладок 0/1 уже отфильтрован по
	//   «получено или найдено» ⇒ у любого игрока выходило ровно 100,00 %.
	TabRatePercent = (TabTotalCount > 0) ? (100.f * TabOwnedCount / TabTotalCount) : 0.f;

	for (int32 R = 0; R < kRows; ++R)
	{
		FRowWidgets& RW = Rows[R];
		const int32 OrderIdx = ScrollTop + R;
		if (!Order.IsValidIndex(OrderIdx))
		{
			for (int32 C = 0; C < 7; ++C) { SetRich(RW.Cells[C], FString()); }
			SetVis(RW.Sel, false); SetVis(RW.Find, false); SetVis(RW.Lose, false); SetVis(RW.Bar, false);
			continue;
		}
		const int32 Id = Order[OrderIdx];
		const FAsdaTitleDef* D = FindDef(Id);
		if (!D) { continue; }
		RowTitleId[R] = Id;

		const uint32 GradeArgb = FAsdaGradeColor::Raw(D->Grade).ToPackedARGB();

		// № — эталонный формат "%ld" по полю +4.
		SetRich(RW.Cells[0], FString::Printf(TEXT("%d"), D->SortNum));

		// Категория — нотис 1460+CategoryLoc. ★ D6: за пределами 1460…1465 строки НЕТ.
		if (D->CategoryLoc >= 0 && D->CategoryLoc <= (TLoc::kCategoryMax - TLoc::kCategoryBase))
		{
			SetRich(RW.Cells[1], Loc(TLoc::kCategoryBase + D->CategoryLoc));
		}
		else if (CVarRawCategoryLoc.GetValueOnGameThread() != 0)
		{
			SetRich(RW.Cells[1], Loc(TLoc::kCategoryBase + D->CategoryLoc));   // как в оригинале
		}
		else
		{
			SetRich(RW.Cells[1], FString());
			UE_LOG(LogAsdaTitle, Verbose,
				TEXT("[звания] D6: звание %d («%s») ссылается на строку категории %d, а их всего 0…5"),
				D->Id, *D->Name, D->CategoryLoc);
		}

		// Имя — цветом грейда.
		SetRich(RW.Cells[2], Colored(GradeArgb, D->Name));

		// Позиция — 1466 «Преф.» (серым) / 1467 «Оконч.» (белым): цвета сняты дизассемблером.
		SetRich(RW.Cells[3], Colored(D->Position == 0 ? kColStateFound : kColWhite,
			Loc(TLoc::kPositionBase + FMath::Clamp(D->Position, 0, 1))));

		// Грейд — нотис 1450+Grade цветом грейда.
		SetRich(RW.Cells[4], Colored(GradeArgb,
			Loc(TLoc::kGradeBase + FMath::Clamp(D->Grade, 0, TLoc::kGradeMax - TLoc::kGradeBase))));

		// Очки.
		SetRich(RW.Cells[5], FString::Printf(TEXT("%d"), D->Point));

		// Состояние: получено (жёлтым) / найдено (серым) / не получено (без тега).
		if (IsOwned(Id))      { SetRich(RW.Cells[6], Colored(kColStateOwned, Loc(TLoc::kStateOwned))); }
		else if (IsFound(Id)) { SetRich(RW.Cells[6], Colored(kColStateFound, Loc(TLoc::kStateFound))); }
		else                  { SetRich(RW.Cells[6], Loc(TLoc::kStateNone)); }

		SetVis(RW.Sel, R == SelectedRow);
	}

	// «Собрано %:» по вкладке — контрол 18714, нотис 2408 + "%.2f%%".
	if (TxtTabRate)
	{
		SetTxt(TxtTabRate, FString::Printf(TEXT("%s %.2f%%"), *Loc(TLoc::kTabRate), TabRatePercent));
	}
}

// Порт `uiCollectionRefreshInfo@0xE98640` + `StatPanel_RefreshDiffs@0xE9C7E0` (D1).
void UAsdaTitleSubsystem::RefreshHeader()
{
	UAsdaTextSubsystem* T = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	if (T)
	{
		{ UAsdaTextSubsystem::FNoticeSlots S; S.Num[0] = FameRank;
		  SetTxt(TxtRanking, UAsdaTextSubsystem::FormatNoticeText(LocTagged(TLoc::kRank), S)); }
		{ UAsdaTextSubsystem::FNoticeSlots S; S.Str[0] = AsdaUi::FormatThousands(FamePoints);
		  SetTxt(TxtTotalPoint, UAsdaTextSubsystem::FormatNoticeText(LocTagged(TLoc::kTotal), S)); }
	}

	// ★ D7 1:1: знаменатель — МАКСИМАЛЬНЫЙ id таблицы (`g_nDataRecIdMax`), а не число записей.
	//   Числитель в оригинале — биты 0…255 (цикл эталона `for (i = 0; i < 256; ++i)`); с маской
	//   512 бит он считает по ДЕЙСТВУЮЩЕЙ ширине, поэтому при полной коллекции процент выходит
	//   чуть больше 100 (280 бит / максимальный id 279 = 100,36 %) — ровно дефект оригинала,
	//   который до расширения был не виден, потому что бит 256…279 не существовало.
	//   Ключ `asda.Title.TotalRateByCount 1` ставит знаменателем число записей таблицы.
	int32 OwnedBits = 0;
	for (int32 i = 0; i < EffectiveMaskBits(); ++i) { if (MaskGet(MaskOwned, i)) { ++OwnedBits; } }
	const int32 Denom = (CVarTotalRateByCount.GetValueOnGameThread() != 0) ? Defs.Num() : MaxTitleId;
	TotalRatePercent = (Denom > 0) ? (100.f * OwnedBits / Denom) : 0.f;
	SetTxt(TxtTotalRate, FString::Printf(TEXT("%s %.2f%%"), *Loc(TLoc::kTotalRate), TotalRatePercent));

	// ★ D1: эталон печатает в три метки дельты ПУСТУЮ строку и прячет все шесть стрелок.
	const bool bShowDelta = CVarShowRankDelta.GetValueOnGameThread() != 0;
	const FString DeltaText = bShowDelta ? TEXT("-") : FString();
	SetTxt(TxtRankDelta, DeltaText);
	SetTxt(TxtPointDelta, DeltaText);
	SetTxt(TxtRateDelta, DeltaText);
	for (const TObjectPtr<UWidget>& W : DeltaArrows) { SetVis(W, false); }
}

// Порт `uiFillLocalPlayerInfo@0xE98840`: две надетые строки звания и имя персонажа.
void UAsdaTitleSubsystem::RefreshSelfTitles()
{
	auto TitleText = [this](int32 Id) -> FString
	{
		const FAsdaTitleDef* D = FindDef(Id);
		if (!D) { return FString(); }
		return Colored(FAsdaGradeColor::Raw(D->Grade).ToPackedARGB(), D->Name);
	};
	SetRich(RsFirstTitle,  TitleText(SelPrefix));
	SetRich(RsSecondTitle, TitleText(SelSuffix));

	if (const UAsdaWorldSubsystem* World = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
	{
		SetTxt(TxtCharName, World->GetSelfCharName());
	}
}

// Обе НАДЕТЫЕ строки звания с тегом цвета грейда (потребители — окно 900 и плашка над головой).
void UAsdaTitleSubsystem::GetWornTitleTexts(FString& OutPrefix, FString& OutSuffix) const
{
	auto Make = [this](int32 Id) -> FString
	{
		const FAsdaTitleDef* D = (Id >= 0) ? FindDef(Id) : nullptr;
		return D ? Colored(FAsdaGradeColor::Raw(D->Grade).ToPackedARGB(), D->Name) : FString();
	};
	OutPrefix = Make(WornPrefix);
	OutSuffix = Make(WornSuffix);
}

// Порт `questUiWnd_clearTextFields@0xE99A40`.
void UAsdaTitleSubsystem::ClearInfoPanel()
{
	SetRich(RsInfoName, FString());   SetTxt(TxtInfoGrade, FString());
	SetTxt(TxtInfoPoint, FString());  SetTxt(TxtInfoRandom, FString());
	SetTxt(TxtInfoType, FString());   SetTxt(TxtInfoTypeName, FString());
	SetTxt(TxtInfoLocation, FString());
	for (FAsdaTitleRichSlot& S : RsInfoOption) { SetRich(S, FString()); }
	SetTxt(TxtInfoCondition, FString());
}

// Порт `ItemTooltip_RefreshStats@0xE99360`: инфо-панель выбранного звания.
void UAsdaTitleSubsystem::RefreshInfoPanel()
{
	const int32 Id = RowTitleId.IsValidIndex(SelectedRow) ? RowTitleId[SelectedRow] : -1;
	const FAsdaTitleDef* D = (Id >= 0) ? FindDef(Id) : nullptr;
	if (!D) { ClearInfoPanel(); return; }

	// Эталон: подробности видны, только если звание ПОЛУЧЕНО либо это вкладка «Секретные».
	if (!IsOwned(Id) && Tab != 2)
	{
		ClearInfoPanel();
		SetTxt(TxtInfoCondition, D->Desc);   // условие получения эталон печатает ВСЕГДА
		return;
	}

	const uint32 GradeArgb = FAsdaGradeColor::Raw(D->Grade).ToPackedARGB();
	SetRich(RsInfoName, Colored(GradeArgb, D->Name));

	UAsdaTextSubsystem::FNoticeSlots S;
	S.Str[0] = Loc(TLoc::kGradeBase + FMath::Clamp(D->Grade, 0, TLoc::kGradeMax - TLoc::kGradeBase));
	SetTxt(TxtInfoGrade, UAsdaTextSubsystem::FormatNoticeText(LocTagged(TLoc::kInfoGrade), S));

	S.Clear(); S.Num[0] = D->Point;
	SetTxt(TxtInfoPoint, UAsdaTextSubsystem::FormatNoticeText(LocTagged(TLoc::kInfoPoint), S));

	S.Clear(); S.Str[0] = Loc(TLoc::kPositionBase + FMath::Clamp(D->Position, 0, 1));
	SetTxt(TxtInfoLocation, UAsdaTextSubsystem::FormatNoticeText(LocTagged(TLoc::kInfoPosition), S));

	// Шесть бонусов: ТИП — из `title_option` по `OptionTypeId`, ЗНАЧЕНИЕ — от сервера (2255/6065).
	const TArray<int16>* Stats = TitleStats.Find(Id);
	for (int32 Slot = 0; Slot < kOptionSlots; ++Slot)
	{
		if (!RsInfoOption.IsValidIndex(Slot) || !RsInfoOption[Slot].Node) { continue; }
		const FString* StatName = OptionNames.Find(D->OptionTypeId[Slot]);
		const bool bHaveStat = Stats && Stats->IsValidIndex(3 * Slot + 1) && (*Stats)[3 * Slot] != -1;
		if (!StatName || StatName->IsEmpty() || !bHaveStat)
		{
			SetRich(RsInfoOption[Slot], FString());
			continue;
		}
		const int32 Value = (int32)(*Stats)[3 * Slot + 1];
		const FString Suffix = (D->IsPercent[Slot] > 0) ? TEXT("%") : TEXT("");
		SetRich(RsInfoOption[Slot], Colored(Value > 0 ? kColStatPlus : kColStatMinus,
			FString::Printf(TEXT("%s : %d%s"), **StatName, Value, *Suffix)));
	}

	SetTxt(TxtInfoCondition, D->Desc);
}

void UAsdaTitleSubsystem::LayoutScrollBar()
{
	if (!ScrollBar.IsBuilt()) { return; }
	if (!bOpen) { ScrollBar.Hide(); return; }

	// ★★★ ГДЕ СТОИТ ПОЛОСА. Контрола 18735 в WBP НЕТ (импортёр колёсные окна не переносит), но его
	//   прямоугольник задан в `.JUI`: `Title_Wheel` — ребёнок `Title_ListBoxBase`, `Origin = "0 10"`,
	//   `Size = "517 275"`, `VertScrollPosType = "1"` (полоса СПРАВА). Оригинал вешает её
	//   `uiControl_AttachVertScrollbar@0x12F3160` ребёнком колёсного окна, начало = правый край.
	//   Начало родителя берём ИЗ АССЕТА — суммой canvas-слотов вверх по дереву (иерархия JUI
	//   расплющена, закон `jui-flattened-hierarchy-geometry`), а не константой.
	//   ⚠ БЫЛО: жёсткое `(543, 239)` — полоса оказывалась левее и выше области списка (жалоба
	//   владельца «полоса посередине-левее»).
	static constexpr float kWheelDx =   0.f;   // .JUI Title_Wheel Origin X
	static constexpr float kWheelDy =  10.f;   // .JUI Title_Wheel Origin Y
	static constexpr float kWheelW  = 517.f;   // .JUI Title_Wheel Size W
	static constexpr float kWheelH  = 275.f;   // .JUI Title_Wheel Size H

	UWidget* Host = Widget ? Widget->GetWidgetFromName(TEXT("Title_ListBoxBase_grp")) : nullptr;
	// ★ Начало родителя — сумма canvas-слотов вверх по дереву (иерархия JUI расплющена), с
	//   проверкой по ЖИВОЙ ГЕОМЕТРИИ, переведённой в координаты КОРНЕВОЙ канвы (планы полосы лежат
	//   именно в ней). Замер: оба пути дают (14,149) ⇒ правый край области рядов
	//   14 + 517 = 531 — там полоса и стоит. Ряды `Title_ListBoxNN_grp` идут от x = 15 шириной 516,
	//   то есть их правый край тоже 531: полоса прижата к нему, как `VertScrollPosType = "1"`.
	FVector2D Base = Host ? OffsetInRoot(Host) : FVector2D(46.0, 260.0);
	FVector2D Mn, Mx;
	if (RootCanvas && Host && AsdaUi::WidgetAbsRect(Host, Mn, Mx))
	{
		const FGeometry& RootG = RootCanvas->GetCachedGeometry();
		if (RootG.GetLocalSize().X > 1.f)
		{
			Base = FVector2D(RootG.AbsoluteToLocal(FVector2f((float)Mn.X, (float)Mn.Y)));
		}
	}
	const FVector2D Origin(Base.X + kWheelDx + kWheelW, Base.Y + kWheelDy);

	const int32 MaxRow = FMath::Max(0, Order.Num() - kRows);
	ScrollBarOrigin = Origin;
	ScrollBarHeight = kWheelH;
	// ★ Волна 14 (МЕЛК-1): целые аргументы эталона `uiControlSetScrollRange(12, totalCount)`
	//   (`QuestLog_PopulateCategoryList@0xE98C00`, quest.c:8730) — длину считает `EtalonThumbLen`.
	ScrollBar.Layout(Origin, kWheelH, /*Scale*/ 1.f, kRows, Order.Num(), MaxRow, ScrollTop);
}

// ★★★ КАДРЫ ВКЛАДОК 18713 (закон `jui-caption-states`). Состояния переключателя `TabBar_W108_3Btn`
//   заданы В СКИНЕ покадрово (кадр 0 покой, 1 наведение, 2 выбрано) — и плашкой, и цветом подписи.
//   Импортёр положил кадры в `FButtonStyle` кнопок `_OPT0…2`; раскладывает их общая точка
//   `AsdaUi::ApplyTabState` ОДНИМ проходом (плашка + гашение кисти активной кнопки + цвет подписи),
//   иначе между двумя вызовами кнопка успевает перерисовать вкладку своей кистью.
//   Наведение при этом остаётся за самой кнопкой (её `Hovered` = кадр 1) — у НЕАКТИВНЫХ вкладок
//   стиль возвращается ровно тот, что лежал в ассете.
void UAsdaTitleSubsystem::RefreshTabState()
{
	if (TabCaptions.Num() == 0) { return; }
	AsdaUi::ApplyTabState(TabPlanes, TabCaptions, TabButtons, Tab,
		TabPlaneIdle, TabBtnIdle, TabCaptionIdle);
}

// ═════════════════════════════ ВВОД ═══════════════════════════════════════════════════════════
int32 UAsdaTitleSubsystem::RowUnderCursor() const
{
	for (int32 R = 0; R < Rows.Num(); ++R)
	{
		// ★ ЗАКОН `jui-cell-chain-nesting`: попадание считаем по ЖИВОЙ геометрии контейнера ряда,
		//   а не по слоту из ассета — иерархия JUI расплющена и слот врёт.
		if (Rows[R].Group && AsdaUi::IsCursorOverWidget(Rows[R].Group)) { return R; }
	}
	return -1;
}

int32 UAsdaTitleSubsystem::ColumnHeaderUnderCursor() const
{
	static const int32 kIdNums[7] = { 18729, 18730, 18731, 18732, 18733, 18734, 188350 };
	for (int32 C = 0; C < ColumnZones.Num() && C < 7; ++C)
	{
		if (ColumnZones[C] && AsdaUi::IsCursorOverWidget(ColumnZones[C])) { return kIdNums[C]; }
	}
	return -1;
}

void UAsdaTitleSubsystem::TickMouse()
{
	// ⚠ `Mouse.Update()` ЗДЕСЬ БОЛЬШЕ НЕТ — он переехал в `Tick` (см. комментарий там). Второй
	//   вызов за кадр СЪЕЛ БЫ ФРОНТ: `Update` переписывает латч, и `bLmbDown` во второй раз уже 0.
	// ── Полоса прокрутки ──
	if (ScrollBar.IsBuilt())
	{
		int32 V = 0;
		const int32 MaxRow = FMath::Max(0, Order.Num() - kRows);
		const EAsdaScrollAction Act = ScrollBar.Tick(Mouse.bLmb, Mouse.bLmbDown, MaxRow, V);
		if (Act == EAsdaScrollAction::ScrollBy || Act == EAsdaScrollAction::SetRow)
		{
			const int32 New = FMath::Clamp(
				(Act == EAsdaScrollAction::ScrollBy) ? ScrollTop + V : V, 0, MaxRow);
			if (New != ScrollTop) { ScrollTop = New; RefreshList(); }
			LayoutScrollBar();
			return;
		}
		if (ScrollBar.IsCursorOver()) { return; }
	}

	// ── Наведение на ряд: показать/скрыть подсветку (эталон `uiTabPageSelect`) ──
	const int32 Hover = RowUnderCursor();
	if (Hover != HoverRow)
	{
		HoverRow = Hover;
		for (int32 R = 0; R < Rows.Num(); ++R) { SetVis(Rows[R].Sel, R == SelectedRow || R == Hover); }
	}

	// ── Подсказки кнопок (эталон case 8 → `tutorialPopupLayoutSmall`, case 0xA → скрыть) ──
	int32 TipTitle = -1, TipBody = -1;
	if (BtnApply  && AsdaUi::IsCursorOverWidget(BtnApply))  { TipTitle = TLoc::kTipApplyCap;  TipBody = TLoc::kTipApply; }
	else if (BtnDelete && AsdaUi::IsCursorOverWidget(BtnDelete)) { TipTitle = TLoc::kTipDeleteCap; TipBody = TLoc::kTipDelete; }
	else if (BtnOk     && AsdaUi::IsCursorOverWidget(BtnOk))     { TipTitle = TLoc::kTipOkCap;     TipBody = TLoc::kTipOk; }
	else if (BtnCancel && AsdaUi::IsCursorOverWidget(BtnCancel)) { TipTitle = TLoc::kTipCancelCap; TipBody = TLoc::kTipCancel; }
	if (TipBody >= 0) { ShowHoverTip(TipTitle, TipBody); } else { HideHoverTip(); }

	if (!Mouse.bLmbDown) { return; }

	// ── Клик по заголовку колонки — сортировка ──
	const int32 Col = ColumnHeaderUnderCursor();
	if (Col >= 0) { SortByColumn(Col); return; }

	// ── Клик по ряду ──
	if (Hover >= 0) { SelectRow(Hover); }
}

// ═════════════════════════════ ДЕЙСТВИЯ ═══════════════════════════════════════════════════════
// Порт `uiQuickbarSelectSlot@0xE99EE0`: повторный клик по выбранному ряду снимает выбор.
void UAsdaTitleSubsystem::SelectRow(int32 Row)
{
	if (!bControlsReady || !Rows.IsValidIndex(Row)) { return; }
	if (SelectedRow == Row)
	{
		SelectedRow = -1;
		for (const TObjectPtr<UWidget>& W : SelectedFrame) { SetVis(W, false); }
		SetVis(Rows[Row].Sel, false);
		ClearInfoPanel();
		UE_LOG(LogAsdaTitle, Log, TEXT("[звания] выбор ряда %d СНЯТ"), Row);
		return;
	}
	SelectedRow = Row;
	// Клик гасит мигание строки (эталон обнуляет байты +12/+13 записи).
	const int32 Id = RowTitleId.IsValidIndex(Row) ? RowTitleId[Row] : -1;
	if (Id >= 0) { FlagNew.Remove(Id); FlagLost.Remove(Id); }
	for (int32 R = 0; R < Rows.Num(); ++R) { SetVis(Rows[R].Sel, R == Row); }
	RefreshInfoPanel();
	const FAsdaTitleDef* D = (Id >= 0) ? FindDef(Id) : nullptr;
	UE_LOG(LogAsdaTitle, Log, TEXT("[звания] выбран ряд %d: id=%d «%s» получено=%d"),
		Row, Id, D ? *D->Name : TEXT("?"), Id >= 0 && IsOwned(Id) ? 1 : 0);
}

// Порт ветки события 66817 (смена сегмента 18713).
void UAsdaTitleSubsystem::SelectTab(int32 InTab)
{
	if (InTab < 0 || InTab > 2) { return; }
	Tab = InTab;
	SelectedRow = -1;
	ScrollTop = 0;
	for (const TObjectPtr<UWidget>& W : SelectedFrame) { SetVis(W, false); }
	ClearInfoPanel();
	RebuildOrder();
	RefreshList();
	RefreshTabState();
	LayoutScrollBar();
	UE_LOG(LogAsdaTitle, Log, TEXT("[звания] вкладка %d («%s»): званий %d"),
		Tab, *Loc(TLoc::kTab0 + Tab), Order.Num());
}

void UAsdaTitleSubsystem::SortByColumn(int32 ColumnIdNum)
{
	bSortDesc = !bSortDesc;
	SortColumn = ColumnIdNum;
	RebuildOrder();
	RefreshList();
	LayoutScrollBar();
	UE_LOG(LogAsdaTitle, Log, TEXT("[звания] сортировка по колонке %d, порядок %s"),
		ColumnIdNum, bSortDesc ? TEXT("убывающий") : TEXT("возрастающий"));
}

// Порт `uiSetSelectedTargetNameText@0xE98A50` — кнопка 18737 «Применить».
void UAsdaTitleSubsystem::OnApplyClicked()
{
	const int32 Id = RowTitleId.IsValidIndex(SelectedRow) ? RowTitleId[SelectedRow] : -1;
	if (Id < 0) { UE_LOG(LogAsdaTitle, Log, TEXT("[звания] «Применить»: ряд не выбран")); return; }
	const FAsdaTitleDef* D = FindDef(Id);
	if (!D) { return; }

	// [ОРИГИНАЛ] `j_charTestStateFlagA(Target_GetSelectedUserIdx(LocalPlayer), id)` — геттер отдаёт СВОЙ userNum
	//   (`Char_GetHoveredUserIdx@0x966490`), то есть проверяется своя маска «получено» (D2 снят 27.09.2026).
	const bool bOwned = IsOwned(Id);   // звание получено своим персонажем
	if (!bOwned)
	{
		// ★ D4: эталон здесь МОЛЧИТ, хотя строка 2426 существует и нигде не используется.
		if (CVarSilentApplyDeny.GetValueOnGameThread() == 0) { NoticeMsgBox(TLoc::kNotEarned); }
		UE_LOG(LogAsdaTitle, Log, TEXT("[звания] «Применить» отклонено: звание %d не получено"), Id);
		return;
	}

	if (D->Position == 0) { SelPrefix = Id; } else { SelSuffix = Id; }
	RefreshSelfTitles();
	UE_LOG(LogAsdaTitle, Log, TEXT("[звания] «Применить»: id=%d «%s» → слот %s (выбор: %d/%d)"),
		Id, *D->Name, D->Position == 0 ? TEXT("приставка") : TEXT("окончание"), SelPrefix, SelSuffix);
}

// Порт `questItem_ValidateSelection@0xE99C30` — кнопка 18738 «Очистить».
void UAsdaTitleSubsystem::OnDeleteClicked()
{
	const int32 Id = RowTitleId.IsValidIndex(SelectedRow) ? RowTitleId[SelectedRow] : -1;
	if (Id < 0) { UE_LOG(LogAsdaTitle, Log, TEXT("[звания] «Очистить»: ряд не выбран")); return; }
	const FAsdaTitleDef* D = FindDef(Id);
	if (!D) { return; }

	// Группа «Соревнование» (Group == 1) не удаляется — нотис 2428.
	if (D->Group == 1) { NoticeMsgBox(TLoc::kCompetition, /*эталон (0, 2428)*/TLoc::kCompetition); return; }
	// Надетое сейчас звание удалить нельзя — нотис 2430 (проверяются ОБА слота, и выбор, и надетое).
	if (Id == SelPrefix || Id == SelSuffix || Id == WornPrefix || Id == WornSuffix)
	{
		NoticeMsgBox(TLoc::kDelWorn, /*эталон (0, 2430)*/TLoc::kDelWorn);
		return;
	}
	if (!IsOwned(Id)) { NoticeMsgBox(TLoc::kDelNotOwned, /*эталон (0, 2429)*/TLoc::kDelNotOwned); return; }

	PendingDeleteId = Id;
	// Эталон: `CItem_BuildTooltipText(g_pItemTooltipUI, 1, 2427, …)` = двухкнопочный TWOBUT_MSGBOX,
	//   и «Да» уходит в `netSendReq6069` (`ui_core.c:57074`, код диалога 0x97B = 2427).
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		// ★ Редакция 1 (ревизия С-1): ветка 0x97B эталона — «закрытие (:57071) → 6069 → закрытие (LABEL_300)»:
		//   первое закрытие — ДО отправки, второе делает общая точка после колбэка.
		AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Loc(TLoc::kConfirmDel), [this](bool bYes)
		{
			if (!bYes || PendingDeleteId < 0) { PendingDeleteId = -1; return; }
			if (UAsdaWindowSubsystem* WM2 = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
			{
				WM2->RepeatMsgBoxClose(1, TEXT("0x97B: CGameUI_CloseWindow до netSendReq6069 (ui_core.c:57071)"));
			}
			if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
			{
				Net->SendTitleDelete6069(PendingDeleteId);
			}
			UE_LOG(LogAsdaTitle, Log, TEXT("[звания] подтверждено удаление id=%d → 6069"), PendingDeleteId);
			PendingDeleteId = -1;
			// ★ Волна В6 ремесла (ревизия В5 МЕЛК-2): закрытие вида дважды (:57071 и `LABEL_300` :57675) — первое стоит
			//   в начале колбэка (редакция 1: до 6069, как эталон), второе — общая точка после ответа.
		}, 0.f, 2288, 2289, /*EtalonNoticeId (1, 2427), quest.c:8799*/2427, /*EtalonKind*/1);
	}
}

// Порт ветки 18727 `questLogWnd_HandleMessage`.
void UAsdaTitleSubsystem::OnOkClicked()
{
	if (CooldownMs > 0)
	{
		// ★ D5: текст 2425 обещает 10 минут, а кулдаун — 60 секунд (и у клиента, и у сервера).
		NoticeMsgBox(TLoc::kCooldown, /*эталон (0, 2425)*/TLoc::kCooldown);
		UE_LOG(LogAsdaTitle, Log, TEXT("[звания] «ОК» отклонено: кулдаун %d мс"), CooldownMs);
		return;
	}
	// Эталон шлёт пакет ТОЛЬКО если выбор отличается от надетого (сравнение двух пар слотов).
	if (SelPrefix == WornPrefix && SelSuffix == WornSuffix)
	{
		UE_LOG(LogAsdaTitle, Log, TEXT("[звания] «ОК»: выбор не изменился, пакет не шлём — просто закрываем"));
		CloseTitleWindow();
		return;
	}
	CooldownMs = kCooldownMs;
	LastCooldownTickS = FPlatformTime::Seconds();
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->SendTitleApply6067(SelPrefix, SelSuffix);
	}
	CloseTitleWindow();
}

void UAsdaTitleSubsystem::OnCancelClicked()
{
	CloseTitleWindow();
}

// Порт ветки 18712: обнулить ОБА слота выбора и очистить обе подписи.
void UAsdaTitleSubsystem::OnReleaseClicked()
{
	SelPrefix = -1;
	SelSuffix = -1;
	SetRich(RsFirstTitle, FString());
	SetRich(RsSecondTitle, FString());
	UE_LOG(LogAsdaTitle, Log, TEXT("[звания] «Убрать указанное звание»: оба слота выбора = -1"));
}

// ═════════════════════════════ НОТИСЫ ═════════════════════════════════════════════════════════
FString UAsdaTitleSubsystem::Loc(int32 Id) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` — теги срезаны, маркеры как в таблице (как было).
	return AsdaNotice::Loc(GetGameInstance(), Id);
}

FString UAsdaTitleSubsystem::LocTagged(int32 Id) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::LocTagged` (поведение то же).
	return AsdaNotice::LocTagged(GetGameInstance(), Id);
}

void UAsdaTitleSubsystem::NoticeMsgBox(int32 LocId, int32 EtalonNoticeId) const
{
	// ★ Волна 1 (14.09.2026, В2): единый маршрут `AsdaNotice::MsgBox` — окно 1500, без оконной подсистемы —
	//   строка в ленту той же категорией. Категория/подпись кнопки — прежние.
	//   ★ E5: раньше без оконной подсистемы нотис терялся молча — теперь уходит в ленту.
	AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::ItemRefused, Loc(LocId), AsdaLoc::kBtnOk, EtalonNoticeId);
}

void UAsdaTitleSubsystem::ShowHoverTip(int32 TitleLocId, int32 BodyLocId)
{
	UAsdaItemTooltipSubsystem* Tt =
		GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	if (!Tt) { return; }
	FAsdaTipMetrics M;
	M.WidthAdd = 27.f;      // как у `tutorialPopupLayoutSmall`
	M.ExtraLines = 1;
	Tt->ShowTextTip(EAsdaTipOwner::TitleWin, Loc(TitleLocId), Loc(BodyLocId), M);
	bTipShown = true;
}

void UAsdaTitleSubsystem::HideHoverTip()
{
	if (!bTipShown) { return; }
	// ★ ЗАКОН `ui-shared-tip-ownership`: гасит ТОЛЬКО владелец.
	if (UAsdaItemTooltipSubsystem* Tt =
		GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tt->HideTooltipFor(EAsdaTipOwner::TitleWin);
	}
	bTipShown = false;
}
