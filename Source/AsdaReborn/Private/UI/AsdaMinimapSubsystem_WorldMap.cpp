// Copyright (c) 2026 @sitxovski (Discord)
// AsdaMinimapSubsystem_WorldMap.cpp — ★ R3-c 12.11.2026 (разрез AsdaMinimapSubsystem.cpp, дословный перенос): карта мира: построение, списки, выбор области, моя позиция, переход по карте, картинки.
//   Общее для частей — в AsdaMinimapSubsystem_Private.h; шапка и реверс — в AsdaMinimapSubsystem.cpp.
#include "UI/AsdaMinimapSubsystem.h"



#include "AsdaMinimapSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaMinimapSubsystem*.cpp

// ★★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА (по просьбе юзера, 2026-08-01): радар-эффект окна карты мира
//    (контрол 1731 MaxMap_RaderEffect, скин Effect_Rader, анимированная плоскость
//    MaxMap_RaderEffect_39_0) ВЫКЛЮЧЕН ПО УМОЛЧАНИЮ.
//    В ОРИГИНАЛЕ эффект ЕСТЬ: MiniMap_OpenBigMap@0xD66D40 зовёт sub_92E658(g_pMiniMap, 1) —
//    эффект включается при открытии большой карты и гаснет при выборе области в списке 1723
//    (MiniMap_BigMapWindowProc: SetVisible(this+53, 0)). Код НЕ удалён — управляется этим CVar:
//    `Asda.WorldMap.RadarEffect 1` возвращает поведение оригинала.
//    (★ R3-c чистка 12.11.2026: перенесено из AsdaMinimapSubsystem.cpp — нужно только этой части, static как до разреза.)
static int32 GAsdaWorldMapRadarEffect = 0;
static ASDA_DEV_CVAR_REF(CVarAsdaWorldMapRadarEffect,
	TEXT("Asda.WorldMap.RadarEffect"),
	GAsdaWorldMapRadarEffect,
	TEXT("Радар-эффект окна карты мира (1731 MaxMap_RaderEffect). 1 — как в оригинале (включается ")
	TEXT("при открытии карты), 0 (по умолчанию) — выключен по просьбе пользователя."),
	ECVF_Default);

// ★★ Волна В4 рыбалки (08.10.2026): гейт «идёт рыбалка» окна карты. Эталон `MiniMap_BigMapWindowProc@0xD65210`, событие 0xB
//   (вызов 0xD654DD, g:374375): `if (Input_IsTextInputFocused(g_pChatInputWnd)) return 1;` — МИСНЕЙМ, это fishState ≠ 0 —
//   стоит ПЕРВЫМ, до разбора контрола: 1714 (клик по полю мини-карты), 1729 (клик «идти сюда» большой карты),
//   17500 (кнопка каналов) и строки каналов 0x445F…0x446D — молча, без нотиса.
// ★ 26.10.2026 (navattack): выход из атаки по приказу автопути игрока — единая точка в AsdaPlayerController_Move.cpp (разбор там).
bool AsdaExitCombatForAutoPath(AAsdaPlayerController* PC, const TCHAR* Why);

// ★ [ОРИГИНАЛ] 09.11.2026 (пачка 20, баг 1): плиты строк раскрытых списков 1721/1723 — как рендер списка эталона (`ui_core.c:112969`):
//   под курсором квад 0x96F0F0F0 на всю строку, выбранный пункт — белая плита `_Cur` и чёрный текст. Кисть плит — сплошной белый квад
//   (шаг пост-импорта asda_fix_worldmap_ui.py, правка (7)). 0 — прежнее: подсветка цветом кадра наведения тела комбо FF225777, без
//   плиты выбранного пункта.
static ASDA_DEV_CVAR(int32, CVarAsdaWorldMapListPlateEtalon,
	TEXT("asda.WorldMap.ListPlateEtalon"), 1,
	TEXT("1 — плиты строк списков карты мира как в оригинале (наведение 0x96F0F0F0 на всю строку, выбранный пункт белым с чёрным текстом); ")
	TEXT("0 — прежнее (наведение FF225777, выбранный пункт не выделяется)."),
	ECVF_Default);

static const TCHAR* kMapImgDir  = AsdaAssetPaths::kMapImagePrefix;

// ── ★ КАРТА МИРА: разложить подписи и связать контролы (MiniMap_BuildUI@0xD63220, часть 1710) ─────
void UAsdaMinimapSubsystem::BuildWorldMapUI()
{
	if (!BigMapWidget) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* TS = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	auto Txt = [&](int32 Id) { return TS ? TS->GetTextById(Id) : FString(); };
	auto SetLabel = [&](const TCHAR* Name, int32 Id)
	{
		if (UTextBlock* T = Cast<UTextBlock>(BigMapWidget->GetWidgetFromName(Name)))
		{
			T->SetText(FText::FromString(Txt(Id)));
		}
	};
	// Эталон MiniMap_BuildUI: 1730 ← 9698, 1720 ← 9600, 1722 ← 9610, 1724 ← 9699, 1725..1728 ← 15203..15206.
	SetLabel(TEXT("MaxMap_Guide"),        9698);
	SetLabel(TEXT("MaxMap_ContTxt"),      9600);
	SetLabel(TEXT("MaxMap_AreaTxt"),      9610);
	SetLabel(TEXT("MaxMap_MyPosBtn_Text"), 9699);
	SetLabel(TEXT("MaxMap_Regend1"),     15203);
	SetLabel(TEXT("MaxMap_Regend2"),     15204);
	SetLabel(TEXT("MaxMap_Regend3"),     15205);
	SetLabel(TEXT("MaxMap_Regend4"),     15206);

	// 1724 MaxMap_MyPosBtn — «Где я?».
	if (UButton* B = Cast<UButton>(BigMapWidget->GetWidgetFromName(TEXT("MaxMap_MyPosBtn"))))
	{
		B->OnClicked.Clear();
		B->OnClicked.AddDynamic(this, &UAsdaMinimapSubsystem::OnMyPosClicked);
	}
	// ── 1721/1723: ШТАТНЫЕ JUI-комбобоксы (скин Bk_ComboBox: ES0 тело + ES1 стрелка + подпись),
	//    раскрывающийся список собран из скина DarkGrey_List (Tools/asda_fix_worldmap_ui.py).
	//    Самодельные UMG-ComboBoxString в ассете свёрнуты — здесь их не трогаем.
	if (UButton* CB = Cast<UButton>(BigMapWidget->GetWidgetFromName(TEXT("MaxMap_Continent_Click"))))
	{
		CB->OnClicked.Clear();
		CB->OnClicked.AddDynamic(this, &UAsdaMinimapSubsystem::OnContinentBoxClicked);
	}
	if (UButton* CB = Cast<UButton>(BigMapWidget->GetWidgetFromName(TEXT("MaxMap_Area_Click"))))
	{
		CB->OnClicked.Clear();
		CB->OnClicked.AddDynamic(this, &UAsdaMinimapSubsystem::OnAreaBoxClicked);
	}
	// Строки обоих списков: клик = выбор, наведение = подсветка (кадр SS1 скина комбобокса).
	ComboRelays.Reset();
	for (int32 Which = 0; Which < 2; ++Which)
	{
		const bool bCont = (Which == 0);
		const TCHAR* Base = bCont ? TEXT("MaxMap_Continent") : TEXT("MaxMap_Area");
		const int32 MaxRows = bCont ? 2 : 10;
		for (int32 i = 0; i < MaxRows; ++i)
		{
			UButton* RB = Cast<UButton>(BigMapWidget->GetWidgetFromName(
				*FString::Printf(TEXT("%s_Row%d_Click"), Base, i)));
			if (!RB) { continue; }
			UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
			R->Index = i;
			R->Callback = [this, bCont](int32 Row)
			{
				if (bCont) { SelectContinent(Row); } else { SelectArea(Row); }
				SetComboListOpen(bCont, false);
				ReturnFocusToViewport();
			};
			R->HoverCallback   = [this, bCont, i]() { SetComboHover(bCont, i); };
			R->UnhoverCallback = [this, bCont]()    { SetComboHover(bCont, -1); };
			RB->OnClicked.Clear();     RB->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
			RB->OnHovered.Clear();     RB->OnHovered.AddDynamic(R, &UAsdaButtonRelay::OnHovered);
			RB->OnUnhovered.Clear();   RB->OnUnhovered.AddDynamic(R, &UAsdaButtonRelay::OnUnhovered);
			AsdaUi::AnchorRelay(ComboRelays, R, RB);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
		}
	}
	// 1736/1735 «Карта мира» — легаси: обработчика нет, группа в JUI Visible=0. Держим скрытой.
	for (const TCHAR* N : { TEXT("MaxMap_Worldmap_grp"), TEXT("MaxMap_Worldmap_22"), TEXT("MaxMap_Worldmap_btn") })
	{
		if (UWidget* W = BigMapWidget->GetWidgetFromName(N)) { W->SetVisibility(ESlateVisibility::Collapsed); }
	}
	bWorldMapBuilt = true;
	FillAreaCombo(SelContinent);
	RefreshComboList(true);
	CenterBigMapWindow();
	UE_LOG(LogAsdaMinimap, Log, TEXT("[карта мира] окно 1710 разложено: подписи 9600/9610/9698/9699 + легенда 15203..15206"));
}

// ── ★ Окно по центру экрана (MiniMap_BuildUI@0xD63220, построчно) ────────────────────────────────
//   maxMapPosX = ШиринаЭкрана/2 − ШиринаОкна/2;  maxMapPosY = ВысотаЭкрана/2 − ВысотаОкна/2;
//   CControl_SetField74_75(окно, maxMapPosX, maxMapPosY) — ОДИН раз при сборке UI, затем позиция
//   живёт своей жизнью (перетаскивание за 1713 её меняет и она сохраняется между открытиями).
void UAsdaMinimapSubsystem::CenterBigMapWindow()
{
	if (bWindowPlaced || !BigMapWidget) { return; }
	// ★ ИСПРАВЛЕНИЕ (дедуп №2, В2-B, U11): размер экрана — в ЕДИНИЦАХ UI (`AsdaUi::ViewportSizeUnits`), как и оффсеты, которые
	//   двигает `ShiftBigMapWindow`. Было `UWidgetLayoutLibrary::GetViewportSize` (пиксели, «DPI проекта = 1.0»): при масштабе
	//   UI ≠ 1 окно уезжало от центра (масштаб 1,5: угол (167,125) ед. = (250,188) px, правый край за экраном). При масштабе 1
	//   (сейчас у проекта) — то же число.
	const FVector2D VP = AsdaUi::ViewportSizeUnits(BigMapWidget->GetWorld());
	if (VP.X <= 0.f || VP.Y <= 0.f) { return; }                                 // вьюпорт ещё не готов
	// Текущее место окна = JUI Origin узла 1710 (59, 40) при размере 690×519.
	const FVector2D Cur(59.f, 40.f);
	// ★ Дедуп №2, В1-B (U16, 11.10.2026): формула центра — общая точка `AsdaUi::CenteredOrigin`.
	const FVector2D Want = AsdaUi::CenteredOrigin(VP, FVector2D(kBigWinW, kBigWinH));
	ShiftBigMapWindow(Want - Cur);
	bWindowPlaced = true;
	UE_LOG(LogAsdaMinimap, Log, TEXT("[карта мира] окно отцентрировано: экран=%.0fx%.0f → (%.0f,%.0f)"),
		VP.X, VP.Y, Want.X, Want.Y);
}

// helpWndFillNoticeList@0xD66880 — перезаполнить список областей под выбранный континент.
void UAsdaMinimapSubsystem::FillAreaCombo(int32 Continent)
{
	RefreshComboList(false);
}

// ── ★ Раскрывающийся список штатного комбобокса ──────────────────────────────────────────────────
//   Строки = подписи из DT_GameText; лишние строки и их клик-кнопки сворачиваются, высота панели
//   подгоняется под число строк (эталон CControl_SetScrollPos(ctrl, N) — N видимых строк по 20 px).
void UAsdaMinimapSubsystem::RefreshComboList(bool bContinent)
{
	if (!BigMapWidget) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* TS = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	const TCHAR* Base = bContinent ? TEXT("MaxMap_Continent") : TEXT("MaxMap_Area");
	const int32 MaxRows = bContinent ? 2 : 10;

	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U10d): подпись пункта без строки была голым числом («9601», `%d`) и только при
	//   отсутствии подсистемы текста — стала единая заглушка «[#id]» и для пустой строки таблицы.
	auto ItemText = [TS](int32 Id) -> FString
	{
		const FString S = TS ? TS->GetTextById(Id) : FString();
		return S.IsEmpty() ? FAsdaLang::MissingTag(Id) : S;
	};
	TArray<FString> Items;
	if (bContinent)
	{
		Items.Add(ItemText(9601));
		Items.Add(ItemText(9602));
	}
	else
	{
		for (const FAsdaWorldMapArea& A : WorldMapAreas)
		{
			if (A.Continent != SelContinent) { continue; }
			Items.Add(ItemText(A.TextId));
		}
	}

	for (int32 i = 0; i < MaxRows; ++i)
	{
		const bool bUsed = Items.IsValidIndex(i);
		if (UTextBlock* T = Cast<UTextBlock>(BigMapWidget->GetWidgetFromName(*FString::Printf(TEXT("%s_Row%d"), Base, i))))
		{
			T->SetText(bUsed ? FText::FromString(Items[i]) : FText::GetEmpty());
			AsdaUi::SetShownDecor(T, bUsed);
		}
		if (UWidget* RB = BigMapWidget->GetWidgetFromName(*FString::Printf(TEXT("%s_Row%d_Click"), Base, i)))
		{
			AsdaUi::SetShownHit(RB, bUsed);
		}
	}
	// Высота панели и её фона = число строк × 20 (высота ячейки Bk_ComboBox из JuiSkin).
	const float H = (float)Items.Num() * kComboRowH;
	if (UWidget* L = BigMapWidget->GetWidgetFromName(*FString::Printf(TEXT("%s_List"), Base)))
	{
		if (UCanvasPanelSlot* S = AsdaUi::SlotOf(L))
		{
			FMargin M = S->GetOffsets(); M.Right = 160.f; M.Bottom = H; S->SetOffsets(M);
		}
	}
	if (UWidget* Bg = BigMapWidget->GetWidgetFromName(*FString::Printf(TEXT("%s_ListBg"), Base)))
	{
		if (UCanvasPanelSlot* S = AsdaUi::SlotOf(Bg))
		{
			FMargin M = S->GetOffsets(); M.Right = 160.f; M.Bottom = H; S->SetOffsets(M);
		}
	}
}

void UAsdaMinimapSubsystem::SetComboListOpen(bool bContinent, bool bOpen)
{
	if (!BigMapWidget) { return; }
	// В оригинале одновременно раскрыт только один список — второй закрывается.
	if (bOpen) { SetComboListOpen(!bContinent, false); }
	(bContinent ? bContListOpen : bAreaListOpen) = bOpen;
	const TCHAR* Base = bContinent ? TEXT("MaxMap_Continent") : TEXT("MaxMap_Area");
	if (bOpen) { RefreshComboList(bContinent); }
	if (UWidget* L = BigMapWidget->GetWidgetFromName(*FString::Printf(TEXT("%s_List"), Base)))
	{
		AsdaUi::SetShownDecor(L, bOpen);
	}
	SetComboHover(bContinent, -1);   // закрыто — плиты свёрнуты; раскрыто — плита выбранного пункта (эталон this+327)
}

// ── ★ ПЛИТЫ СТРОК РАСКРЫТОГО СПИСКА (09.11.2026, пачка 20, баг 1) ─────────────────────────────────────
//   Эталон: `uiListGrid_layoutRows@0x12EBFC0` (ui_core.c:112605) кладёт пункту под курсором (this+326) и выбранному (this+327)
//   прямоугольник SetRect(clientLeft, rowTop, clientRight, rowTop + высота) — ВСЯ ширина плана 0 списка; рендер (ui_core.c:112969)
//   ставит TEXTUREFACTOR: под курсором 0x96F0F0F0, затем — если это же выбранный — ~item[+2760] = 0xFFFFFFFF (выбранный
//   побеждает). Текст выбранного — `(a4 & 0xFFFFFF) | 0xFF000000` при a4 = 0 ⇒ чёрный, остальных — белый (ui_core.c:113203-113230).
//   Ключ `asda.WorldMap.ListPlateEtalon` 0 — прежнее: одна плита цвета FF225777, выбранный пункт не выделяется.
void UAsdaMinimapSubsystem::SetComboHover(bool bContinent, int32 Row)
{
	if (!BigMapWidget) { return; }
	const TCHAR* Base = bContinent ? TEXT("MaxMap_Continent") : TEXT("MaxMap_Area");
	const bool bEtalon = CVarAsdaWorldMapListPlateEtalon.GetValueOnGameThread() != 0;
	const bool bListOpen = bContinent ? bContListOpen : bAreaListOpen;
	const int32 SelRow = (bEtalon && bListOpen) ? (bContinent ? SelContinent : SelArea) : -1;
	const int32 MaxRows = bContinent ? 2 : 10;
	// Плита строки: позиция по строке и цвет; −1 — свернуть.
	auto Plate = [this, Base](const TCHAR* Suffix, int32 R, uint32 Argb)
	{
		UWidget* W = BigMapWidget->GetWidgetFromName(*FString::Printf(TEXT("%s%s"), Base, Suffix));
		if (!W) { return; }
		if (R < 0) { W->SetVisibility(ESlateVisibility::Collapsed); return; }
		if (UCanvasPanelSlot* S = AsdaUi::SlotOf(W)) { S->SetOffsets(FMargin(0.f, (float)R * kComboRowH, 160.f, kComboRowH)); }
		if (UImage* I = Cast<UImage>(W)) { I->SetBrushTintColor(FSlateColor(AsdaUi::SrgbU32ToLinear(Argb))); }
		W->SetVisibility(ESlateVisibility::HitTestInvisible);
	};
	// Под курсором; у выбранного пункта плиту рисует `_Cur` (выбранный побеждает).
	Plate(TEXT("_Sel"), (Row >= 0 && Row != SelRow) ? Row : -1, bEtalon ? 0x96F0F0F0u : 0xFF225777u);
	Plate(TEXT("_Cur"), SelRow, 0xFFFFFFFFu);
	for (int32 i = 0; i < MaxRows; ++i)
	{
		if (UTextBlock* T = Cast<UTextBlock>(BigMapWidget->GetWidgetFromName(*FString::Printf(TEXT("%s_Row%d"), Base, i))))
		{
			T->SetColorAndOpacity(FSlateColor(i == SelRow ? FLinearColor::Black : FLinearColor::White));
		}
	}
}

void UAsdaMinimapSubsystem::OnContinentBoxClicked() { SetComboListOpen(true,  !bContListOpen); }
void UAsdaMinimapSubsystem::OnAreaBoxClicked()      { SetComboListOpen(false, !bAreaListOpen); }

// MiniMap_OpenBigMap@0xD66D40 — показать в окне подложку карты InMapId (город/поле).
void UAsdaMinimapSubsystem::OpenBigMapFor(int32 InMapId, bool bTown)
{
	DisplayMapId  = InMapId;
	bDisplayTown  = bTown;
	BigMapTex     = LoadBigMapImage(InMapId, bTown);
	// Радар-эффект 1731 (эталон sub_92E658(g_pMiniMap,1) в MiniMap_OpenBigMap) — показываем при открытии
	//   карты; гасит его выбор области (эталон MiniMap_BigMapWindowProc ctrl 1723: SetVisible(this+53, 0)).
	//   ★ По умолчанию ВЫКЛЮЧЕН (CVar Asda.WorldMap.RadarEffect, см. шапку файла) — просьба юзера.
	if (BigMapWidget)
	{
		if (UWidget* R = BigMapWidget->GetWidgetFromName(TEXT("MaxMap_RaderEffect_39_0")))
		{
			R->SetVisibility(GAsdaWorldMapRadarEffect != 0 ? ESlateVisibility::HitTestInvisible
			                                               : ESlateVisibility::Collapsed);
		}
	}
	// ★ [ОРИГИНАЛ] 30.10.2026: эталон здесь же строит метки квестов NPC показанной карты (`npcUi_BuildNpcActionList(…, 1)`) —
	//   пересчёт меток сразу, а не через секунду периодики (разбор — у ключа `asda.Map.OtherMapQuestMarks`).
	if (UAsdaQuestMarkerSubsystem* QM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr)
	{
		QM->RebuildMarks();
	}
	UE_LOG(LogAsdaMinimap, Log, TEXT("[карта мира] показана карта=%d вариант=%s подложка=%s"),
		InMapId, bTown ? TEXT("город") : TEXT("поле"), BigMapTex ? TEXT("есть") : TEXT("НЕТ"));
}

// worldMapDlg_SelectCurrentMap@0xD66A20 — выставить комбобоксы на СВОЮ карту и показать её.
void UAsdaMinimapSubsystem::SelectCurrentMapInCombos()
{
	// Эталон: континент 1, если g_nCurMapId >= 12 или == 7; иначе 0.
	SelContinent = AsdaMapRegistry::ContinentOf(MapId);   // ★ волна 2: континент карты — из реестра карт
	int32 Row = -1, Fallback = -1, i = 0;
	for (const FAsdaWorldMapArea& A : WorldMapAreas)
	{
		if (A.Continent != SelContinent) { continue; }
		if (A.MapId == MapId)
		{
			if (Fallback < 0) { Fallback = i; }
			if (A.bTown == bTownMode) { Row = i; break; }
		}
		++i;
	}
	if (Row < 0) { Row = Fallback; }
	SelArea = Row;

	if (BigMapWidget)
	{
		RefreshComboList(true);
		FillAreaCombo(SelContinent);
		SetComboListOpen(true, false);
		SetComboListOpen(false, false);
		if (UAsdaTextSubsystem* TS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr)
		{
			SetComboCaption(TEXT("MaxMap_Continent_Text"), TS->GetTextById(SelContinent == 1 ? 9602 : 9601));
			int32 k = 0;
			for (const FAsdaWorldMapArea& A : WorldMapAreas)
			{
				if (A.Continent != SelContinent) { continue; }
				if (k == Row) { SetComboCaption(TEXT("MaxMap_Area_Text"), TS->GetTextById(A.TextId)); break; }
				++k;
			}
		}
	}
	OpenBigMapFor(MapId, bTownMode);
}

// Подпись-«шапка» комбобокса (эталон cControlSetCaption): импортёр JUI держит её отдельным
//   TextBlock <idc>_Text под скином Bk_ComboBox, поэтому дублируем выбор и в него.
void UAsdaMinimapSubsystem::SetComboCaption(const TCHAR* TextWidget, const FString& S)
{
	if (!BigMapWidget) { return; }
	if (UTextBlock* T = Cast<UTextBlock>(BigMapWidget->GetWidgetFromName(TextWidget)))
	{
		T->SetText(FText::FromString(S));
	}
}

void UAsdaMinimapSubsystem::SelectContinent(int32 Continent)
{
	SelContinent = FMath::Clamp(Continent, 0, 1);
	SelArea = -1;
	FillAreaCombo(SelContinent);
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* TS = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	if (TS)
	{
		SetComboCaption(TEXT("MaxMap_Continent_Text"), TS->GetTextById(SelContinent == 1 ? 9602 : 9601));
		// Эталон MiniMap_BigMapWindowProc ctrl 1721: подпись списка областей ← 96111 / 96211.
		SetComboCaption(TEXT("MaxMap_Area_Text"), TS->GetTextById(SelContinent == 1 ? 96211 : 96111));
	}
	UE_LOG(LogAsdaMinimap, Log, TEXT("[карта мира] континент=%d"), SelContinent);
}

void UAsdaMinimapSubsystem::SelectArea(int32 AreaIndexInContinent)
{
	int32 i = 0;
	for (const FAsdaWorldMapArea& A : WorldMapAreas)
	{
		if (A.Continent != SelContinent) { continue; }
		if (i == AreaIndexInContinent)
		{
			SelArea = AreaIndexInContinent;
			OpenBigMapFor(A.MapId, A.bTown);
			if (UAsdaTextSubsystem* TS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr)
			{
				SetComboCaption(TEXT("MaxMap_Area_Text"), TS->GetTextById(A.TextId));
			}
			// Эталон: после выбора области радар-эффект гасится (SetVisible(this+53, 0)).
			if (BigMapWidget)
			{
				if (UWidget* R = BigMapWidget->GetWidgetFromName(TEXT("MaxMap_RaderEffect_39_0")))
				{
					R->SetVisibility(ESlateVisibility::Collapsed);
				}
			}
			return;
		}
		++i;
	}
}

// 1724 MaxMap_MyPosBtn: this[64] = −1; worldMapDlg_SelectCurrentMap.
void UAsdaMinimapSubsystem::ShowMyPosition()
{
	DisplayMapId = -1;
	SelectCurrentMapInCombos();
}

void UAsdaMinimapSubsystem::OnMyPosClicked() { ShowMyPosition(); }

// ★ После работы с выпадающим списком фокус ОБЯЗАН вернуться во вьюпорт: сфокусированный Slate-виджет
//   глотает клавиатуру, и персонаж перестаёт ходить на WASD (та же природа, что регрессия 2026-08-01).
void UAsdaMinimapSubsystem::ReturnFocusToViewport()
{
	if (APlayerController* PC = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr)
	{
		if (ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (UGameViewportClient* VP = LP->ViewportClient) { VP->SetIgnoreInput(false); }
		}
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void UAsdaMinimapSubsystem::CancelTravelRequest()
{
	bTravelActive = false; TravelMapId = -1;
	TravelStepTile = FIntPoint(-1, -1); TravelStepMapId = -1;
	TravelLastTile = FIntPoint(-1, -1); TravelStuckCount = 0;
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ★★★ МЕЖКАРТОВЫЙ АВТОПУТЬ — порт `beginMoveToTile@0xFBE140` + блока `game_main.c:1788…1895`
//  Разбор эталона целиком — `Reports/quest_tracker_fix.md` §Д3.
// ════════════════════════════════════════════════════════════════════════════════════════════════
bool UAsdaMinimapSubsystem::BeginMapTravel(int32 InMapId, int32 TileX, int32 TileY)
{
	// ★ Волна Л5 торговли (30.09.2026): [ОРИГИНАЛ] `beginMoveToTile@0xFBE140` (world_map.c:10813-10819) — первым делом
	//   `!sub_934E86(g_pSellWindow) && !sub_929DC4(g_bSomeFeatureEnabled)`, иначе МОЛЧА ничего (заявка не взводится).
	if (UAsdaTradeStateSubsystem::IsSelfStoreOpen(GetGameInstance()))
	{
		UE_LOG(LogAsdaMinimap, Log, TEXT("[автопуть] заявка отклонена молча: открыта личная лавка 22000/23000 (beginMoveToTile)"));
		return false;
	}
	// ★ Ревизия Л4/Л5 (СРЕДН-1): [ОРИГИНАЛ] третье условие того же гейта (world_map.c:10819) — `!CObject_GetField5581(g_pWarehouseWnd)`:
	//   свиток возврата склада активен ⇒ МОЛЧА.
	if (const UAsdaWarehouseSubsystem* Ware = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWarehouseSubsystem>() : nullptr)
	{
		if (Ware->IsRecallActive())
		{
			UE_LOG(LogAsdaMinimap, Log, TEXT("[автопуть] заявка отклонена молча: свиток возврата склада активен (beginMoveToTile)"));
			return false;
		}
	}
	// Эталонные ограничители `beginMoveToTile`: клетка ≤ 512 по обеим осям, карта задана.
	if (InMapId < 0 || TileX < 0 || TileY < 0 || TileX > 512 || TileY > 512) { return false; }

	bTravelActive = true;
	TravelMapId   = InMapId;
	TravelTile    = FIntPoint(TileX, TileY);
	// Шаг ещё не считан — первый же тик посчитает его и выдаст приказ движения.
	TravelStepTile   = FIntPoint(-1, -1);
	TravelStepMapId  = -1;
	TravelLastTile   = FIntPoint(-1, -1);
	TravelStuckCount = 0;
	TravelNextTryTime = 0.0;
	OnMapTravelRequested.Broadcast(TravelMapId, TileX, TileY);
	UE_LOG(LogAsdaMinimap, Log, TEXT("[автопуть] заявка: карта=%d клетка=(%d,%d) (эталон beginMoveToTile)"),
		TravelMapId, TileX, TileY);
	// ★★★ 26.10.2026 (navattack) [ИСПРАВЛЕНИЕ]: заявку взводит игрок (кнопка маршрута журнала на чужую карту, клик по большой карте
	//   чужой карты) — выход из атаки сразу, как у приказа по своей карте (разбор у `AsdaExitCombatForAutoPath`,
	//   AsdaPlayerController_Move.cpp). У эталона заявку в состоянии атаки снимает главный тик (LABEL_111) — поход не начинался вовсе.
	if (UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		AsdaExitCombatForAutoPath(Cast<AAsdaPlayerController>(W->GetFirstPlayerController()), TEXT("межкартовый автопуть"));
	}
	return true;
}

void UAsdaMinimapSubsystem::TickMapTravel()
{
	if (!bTravelActive) { return; }

	UGameInstance* GI = GetGameInstance();
	// ★ Пока идёт смена карты, поход замирает: игрок ещё не опознан на новом соединении, и шаги
	//   всё равно не дошли бы до сервера. Заявка при этом ЖИВА — после входа в мир поход
	//   продолжится сам, уже с новой карты (шаг пересчитается по графу переходов).
	if (const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		if (Net->bTransferInProgress) { return; }
	}
	UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	AAsdaPlayerCharacter* P = nullptr;
	const UAsdaWalkGrid* G = GetGrid(P);
	if (!LM || LM->CurrentMapId < 0 || !P || !G) { return; }      // карта ещё грузится — просто ждём
	AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(UGameplayStatics::GetPlayerController(P, 0));
	if (!PC) { return; }
	// ★ 22.10.2026 [ИСПРАВЛЕНИЕ] под «станом» (`asda.Move.StunHardStop`) автопуть ЖДЁТ: шагов не выдаём и попытку «не сдвинулся» не
	//   считаем (у эталона следование слало 4006 под станом → result 6, и заявка доживала до снятия). После снятия — следующий шаг.
	if (PC->IsStunHardStopped()) { TravelNextTryTime = FPlatformTime::Seconds() + kTravelRetrySec; return; }

	const int32 CurMap = LM->CurrentMapId;
	const FVector2D Pos = P->GetGamePosXZ();
	const FIntPoint CurTile(FMath::FloorToInt(Pos.X / 100.f), FMath::FloorToInt(Pos.Y / 100.f));

	// ── Прибыли? (эталон: позиция игрока в клетках совпала с g_nMapTransferTile*) ──
	if (CurMap == TravelMapId && CurTile == TravelTile)
	{
		UE_LOG(LogAsdaMinimap, Log, TEXT("[автопуть] прибыли: карта=%d клетка=(%d,%d)"),
			CurMap, CurTile.X, CurTile.Y);
		SetDestinationTile(0, 0, false);
		CancelTravelRequest();
		return;
	}

	// Пока персонаж идёт — не мешаем. Эталон переспрашивает маршрут ровно тогда, когда игрок
	//   доехал до клетки текущего шага (`g_bPlayerTilePosValid`).
	const bool bMapChanged = (CurMap != TravelStepMapId);
	if (!bMapChanged && PC->HasRoute()) { TravelStuckCount = 0; TravelLastTile = CurTile; return; }

	const double Now = FPlatformTime::Seconds();
	if (!bMapChanged && Now < TravelNextTryTime) { return; }
	TravelNextTryTime = Now + kTravelRetrySec;

	// ── Защита от зависания (эталонные g_AutoWalkRetryCount/g_AutoWalkFailCount) ──
	if (!bMapChanged && CurTile == TravelLastTile)
	{
		if (++TravelStuckCount >= kTravelStuckLimit)
		{
			UE_LOG(LogAsdaMinimap, Warning,
				TEXT("[автопуть] персонаж не сдвинулся с (%d,%d) за %d попыток — заявка снята (эталон g_AutoWalkFailCount>=5)"),
				CurTile.X, CurTile.Y, kTravelStuckLimit);
			SetDestinationTile(0, 0, false);
			CancelTravelRequest();
			return;
		}
	}
	else
	{
		TravelStuckCount = 0;
	}
	TravelLastTile = CurTile;

	// ── Клетка следующего шага ──
	//   своя карта == целевая → КОНЕЧНАЯ клетка; иначе → клетка ПОРТАЛА на следующую карту
	//   маршрута. Второй расчёт не пишем: спрашиваем ту же точку, что и экранный трекер.
	FIntPoint Step = TravelTile;
	if (CurMap != TravelMapId)
	{
		int32 PX = 0, PY = 0;
		// ★ Передаём сетку и позицию игрока: тогда цель — ближайшая ПРОХОДИМАЯ клетка зоны
		//   перехода, а не её центр (центр может стоять в стене — см. Alpen → Rein).
		const FIntPoint MeTile(FMath::FloorToInt(Pos.X / 100.f), FMath::FloorToInt(Pos.Y / 100.f));
		FIntPoint Approach(0, 0);
		if (!UAsdaQuestMarkerSubsystem::NextPortalStep(CurMap, TravelMapId, PX, PY, G, &MeTile, &Approach))
		{
			// ЭТАЛОН здесь молчит: `pathfindGetRouteNodeCoords` возвращает 0, координаты остаются
			//   мусором со стека, и от прогулки в случайную точку спасает только проверка
			//   диапазона. Мы отказываемся молча и осознанно — мусор не читаем.
			// ★ 27.09.2026: причина уточняется — цель на карте, закрытой на сервере (зеркало OpenMapInfo в реестре).
			UE_LOG(LogAsdaMinimap, Warning,
				TEXT("[автопуть] пути с карты %d на карту %d в графе порталов нет%s — заявка снята"),
				CurMap, TravelMapId,
				AsdaMapRegistry::IsMapOpenOnServer(TravelMapId) ? TEXT("") : TEXT(" (карта закрыта на сервере, OpenMapInfo Use=0)"));
			SetDestinationTile(0, 0, false);
			CancelTravelRequest();
			return;
		}
		Step = FIntPoint(PX, PY);
		// ★ ВХОД В ПОРТАЛ ПО ПРЯМОЙ (решение владельца 24.08.2026). Сперва выходим на клетку
		//   подвода — она снаружи зоны, ровно напротив цели. Уже стоя на ней, шагаем внутрь:
		//   отрезок получается прямым и перпендикулярным кромке зоны. Прежде цель ставилась
		//   сразу внутрь, персонаж приходил в угол зоны с произвольной стороны и замирал.
		if (Approach != Step && MeTile != Approach)
		{
			Step = Approach;
		}
	}

	// ── Клетка непроходима → эталонный поиск ближайшей проходимой по расширяющейся спирали ──
	//   (`game_main.c:1806…1860`: шаг угла 0.1 рад, радиус +200 за оборот, максимум 500 проб).
	if (!G->IsTileWalkable(Step.X, Step.Y))
	{
		const FVector2D Base(100.f * Step.X + 50.f, 100.f * Step.Y + 50.f);
		float Ang = 0.f, Rad = 200.f;
		bool bFound = false;
		for (int32 i = 0; i < 500 && !bFound; ++i)
		{
			Ang += 0.1f;
			if (Ang > 6.28318f) { Ang = 0.f; Rad += 200.f; }
			const FVector2D Q = Base + FVector2D(FMath::Sin(Ang), FMath::Cos(Ang)) * Rad;
			if (Q.X <= 0.f || Q.Y <= 0.f || Q.X >= 51100.f || Q.Y >= 51100.f) { continue; }
			const FIntPoint T(FMath::FloorToInt(Q.X / 100.f), FMath::FloorToInt(Q.Y / 100.f));
			if (G->IsTileWalkable(T.X, T.Y)) { Step = T; bFound = true; }
		}
		if (!bFound)
		{
			UE_LOG(LogAsdaMinimap, Warning,
				TEXT("[автопуть] клетка шага (%d,%d) непроходима и обход по спирали ничего не нашёл — заявка снята"),
				Step.X, Step.Y);
			SetDestinationTile(0, 0, false);
			CancelTravelRequest();
			return;
		}
	}

	TravelStepTile  = Step;
	TravelStepMapId = CurMap;
	SetDestinationTile(Step.X, Step.Y, true);
	// ★ Помечаем приказ как ШАГ ПУТЕШЕСТВИЯ: иначе общая точка «игрок начал двигаться» приняла бы
	//   его за волю игрока и отменила бы путешествие на первом же шаге.
	PC->BeginTravelStep();
	PC->MoveToGamePoint(FVector2D(100.f * Step.X + 50.f, 100.f * Step.Y + 50.f));
	UE_LOG(LogAsdaMinimap, Log,
		TEXT("[автопуть] карта %d → %d: шаг к %s (%d,%d), мы в (%d,%d)"),
		CurMap, TravelMapId, (CurMap == TravelMapId) ? TEXT("цели") : TEXT("порталу"),
		Step.X, Step.Y, CurTile.X, CurTile.Y);
}

FString UAsdaMinimapSubsystem::DescribeMapTravel() const
{
	if (!bTravelActive) { return TEXT("Межкартовый автопуть: заявки нет.\n"); }

	UGameInstance* GI = GetGameInstance();
	const UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	const int32 CurMap = LM ? LM->CurrentMapId : -1;

	FString S = FString::Printf(
		TEXT("Межкартовый автопуть: цель карта=%d клетка=(%d,%d); мы на карте %d; ")
		TEXT("текущий шаг (%d,%d) посчитан на карте %d; проб на месте %d из %d.\n"),
		TravelMapId, TravelTile.X, TravelTile.Y, CurMap,
		TravelStepTile.X, TravelStepTile.Y, TravelStepMapId, TravelStuckCount, kTravelStuckLimit);

	// ★ Куда поведёт СЛЕДУЮЩИЙ шаг — считаем ТОЙ ЖЕ функцией, что и исполнитель, и печатаем
	//   числами. Нужно потому, что сам исполнитель молчит, пока нет павна игрока (в headless-PIE
	//   его не спавнят), — а решение «к какому порталу идти» проверяемо и без павна.
	if (CurMap < 0)
	{
		S += TEXT("   шаг не считается: номер текущей карты неизвестен (−1)\n");
	}
	else if (CurMap == TravelMapId)
	{
		S += FString::Printf(TEXT("   мы уже на целевой карте — шаг ведёт прямо в клетку (%d,%d)\n"),
			TravelTile.X, TravelTile.Y);
	}
	else
	{
		int32 PX = 0, PY = 0;
		if (UAsdaQuestMarkerSubsystem::NextPortalStep(CurMap, TravelMapId, PX, PY))
		{
			S += FString::Printf(TEXT("   шаг ведёт к ПОРТАЛУ %d→? в клетке (%d,%d) (граф kPortalGraph)\n"),
				CurMap, PX, PY);
		}
		else
		{
			S += FString::Printf(TEXT("   пути с карты %d на карту %d в графе порталов НЕТ%s\n"),
				CurMap, TravelMapId,
				AsdaMapRegistry::IsMapOpenOnServer(TravelMapId) ? TEXT("") : TEXT(" (карта закрыта на сервере)"));
		}
	}

	// Почему исполнитель может стоять: без павна и сетки проходимости шагать некому.
	AAsdaPlayerCharacter* P = nullptr;
	if (!GetGrid(P) || !P)
	{
		S += TEXT("   ★ исполнитель ЖДЁТ: павна игрока/сетки проходимости в мире нет\n");
	}
	return S;
}

// Пикселей на тайл в поле 512×512. Эталон для клика/наведения берёт ровно mapTexSize/mapDrawW = 512/512 = 1
//   (все карты клиента 512×512 тайлов). Для СВОЕЙ карты используем реальную ширину сетки — так формула
//   остаётся верной, даже если карта окажется другого размера; для ЧУЖОЙ сетки у клиента нет → 512/512.
float UAsdaMinimapSubsystem::BigMapTileToPx() const
{
	AAsdaPlayerCharacter* P = nullptr;
	const UAsdaWalkGrid* G = GetGrid(P);
	if (G && G->Width > 0 && DisplayMapId == MapId) { return kBigMapPx / (float)G->Width; }
	return 1.f;
}

// Подложка большой карты по mapId (<dir>_big / <dir>_town_big).
// ★ 30.10.2026 [ИСПРАВЛЕНИЕ] — разбор у места использования (`LoadBigMapImage`): нет `<dir>_big` → картинка мини-карты.
static ASDA_DEV_CVAR(int32, CVarAsdaMapBigImageFromMini,
	TEXT("asda.Map.BigImageFromMini"), 1,
	TEXT("1 = у карты без картинки большой карты (<dir>_big) подложкой служит картинка её мини-карты; 0 = пустое поле."),
	ECVF_Default);
static bool AsdaBigImageFromMiniOn() { return CVarAsdaMapBigImageFromMini.GetValueOnGameThread() != 0; }

UTexture2D* UAsdaMinimapSubsystem::LoadBigMapImage(int32 InMapId, bool bTown) const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	const FAsdaMapRow* Row = LM ? LM->GetMapRow(InMapId) : nullptr;
	if (!Row) { return nullptr; }
	const FString Dir = AsdaMapRegistry::ToAssetName(Row->DirName);   // ★ волна 2: одна точка приведения имени
	// ★ Аудит №3 (A8/B8): подложки карт — через менеджер текстур (кэш + refCount).
	auto Load = [](const FString& Base) -> UTexture2D*
	{
		UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();
		return TexMgr ? TexMgr->LoadTextureCached(FString(kMapImgDir) + Base) : nullptr;
	};
	FVector2D O; float S;
	UTexture2D* T = (bTown && TownCrop(InMapId, O, S)) ? Load(Dir + TEXT("_town_big")) : nullptr;
	if (!T) { T = Load(Dir + TEXT("_big")); }
	// ★★★ [ИСПРАВЛЕНИЕ] 30.10.2026 (баг владельца: «у карт t_… белый фон»). У турнирных карт 10/11 (`t_palace_of_queen`,
	//   `t_castle_of_chess`) в клиенте есть только картинка мини-карты `<dir>.TEX`, `<dir>_big.TEX` нет ни в одной сборке
	//   (в оригинале эти карты закрыты: записи 10/11 реестра пусты, окно их не показывало). Большая карта — та же картинка
	//   карты 512×512 с подписями; без неё окно рисовало пустое (белое) поле. Берём картинку мини-карты ЭТОЙ ЖЕ карты —
	//   геометрия совпадает (обе покрывают карту целиком). Откат: `asda.Map.BigImageFromMini 0`.
	if (!T && AsdaBigImageFromMiniOn())
	{
		T = Load(Dir);
		if (T)
		{
			UE_LOG(LogAsdaMinimap, Log, TEXT("[карта мира] у карты %d нет %s_big — подложка из картинки мини-карты %s"),
				InMapId, *Dir, *Dir);
		}
	}
	if (!T)
	{
		UE_LOG(LogAsdaMinimap, Warning, TEXT("[карта мира] нет подложки %s%s_big"), kMapImgDir, *Dir);
	}
	return T;
}

// ── Текстуры карты ───────────────────────────────────────────────────────────────────────────────
void UAsdaMinimapSubsystem::LoadMapTextures()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	const FAsdaMapRow* Row = LM ? LM->GetMapRow(MapId) : nullptr;
	if (!Row) { MiniMapTex = nullptr; BigMapTex = nullptr; return; }

	// Имя ассета = имя файла оригинала без .TEX; пробелы каталога заменены на «_» при импорте.
	const FString Dir = AsdaMapRegistry::ToAssetName(Row->DirName);   // ★ волна 2: одна точка приведения имени
	// ★ Аудит №3 (A8/B8): подложки карт — через менеджер текстур (кэш + refCount).
	auto Load = [](const FString& Base) -> UTexture2D*
	{
		UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();
		return TexMgr ? TexMgr->LoadTextureCached(FString(kMapImgDir) + Base) : nullptr;
	};
	// В клиенте регистр имён пляшет (Rein_big, Silalis, QuizStage_big…). Ассеты импортированы с
	//   оригинальным регистром, а менеджер вдобавок повторяет попытку в нижнем регистре.
	MiniMapTex = Load(Dir);
	// Подложка БОЛЬШОЙ карты зависит от того, какая карта показана в окне 1710 (может быть чужая —
	//   выбранная в списке «Область»). Своя карта / окно ещё не открывалось → текущая карта и её зона.
	if (DisplayMapId < 0 || DisplayMapId == MapId)
	{
		DisplayMapId = MapId;
		bDisplayTown = bTownMode;
	}
	BigMapTex = LoadBigMapImage(DisplayMapId, bDisplayTown);
	if (!MiniMapTex)
	{
		UE_LOG(LogAsdaMinimap, Warning, TEXT("[миникарта] нет картинки карты %s%s"), kMapImgDir, *Dir);
	}
}
