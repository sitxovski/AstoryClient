// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSkillWindowSubsystem_Controls.cpp — ★ R3-c 12.11.2026 (разрез AsdaSkillWindowSubsystem.cpp, дословный перенос): виджеты, открыть/закрыть, группы слотов, сбор виджетов, стили, монеты, кнопки, прокрутка.
//   Общее для частей — в AsdaSkillWindowSubsystem_Private.h; шапка и реверс — в AsdaSkillWindowSubsystem.cpp.
#include "UI/AsdaSkillWindowSubsystem.h"

#include "Login/AsdaButtonRelay.h"

#if PLATFORM_WINDOWS
// ★ Системный интервал двойного клика: оригинал получает событие 0xD от самой Windows
//   (`WM_LBUTTONDBLCLK`), то есть по значению «Панель управления → Мышь». В Slate такого запроса нет
//   (`FSlateApplication` умеет только маршрутизировать уже готовое событие), поэтому берём его у ОС
//   тем же вызовом, каким его берёт оконная процедура оригинала.
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/MinWindows.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif
#include "AsdaSkillWindowSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaSkillWindowSubsystem*.cpp

// WBP окна (JUI IDNum 12000). Импортирован ранее, НЕ перегенерируется.
static const TCHAR* kSkillWbp   = TEXT("/Game/AsdaAssets/UI/Player_UI/Character/WBP_SKILL_WIN.WBP_SKILL_WIN_C");

// ── Локализация: id из эталона (реверс — Reports/skill_window_port.md) ──
static constexpr int32 kLocNoJob            = 9421;    // «профессия ещё не выбрана» (окно не открывается)
static constexpr int32 kLocBusy             = 107022;  // «Ты в своей лавке» — открыта личная лавка (гейт волны Л5 торговли)

static constexpr int32 kLocJob4TipBody      = 111801;  // подсказка кнопки 4-й профессии (тело)

bool UAsdaSkillWindowSubsystem::EnsureWidget()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	if (WM->IsWindowLive(Win)) { return true; }
	ResetWidgets();
	// Корень SelfHitTestInvisible: растянутый на вьюпорт UUserWidget с Visible съедает клики по миру.
	Win = WM->OpenWindowByPath(kSkillWinId, kSkillWbp, WindowZ, ESlateVisibility::SelfHitTestInvisible);
	if (!Win)
	{
		UE_LOG(LogAsdaSkillWin, Error, TEXT("[навыки] WBP_SKILL_WIN не загрузился — окно показать нечем"));
		return false;
	}
	ScanWidgets();
	BindButtons();
	for (int32 i = 0; i < Slots.Num(); ++i) { BuildCoinChildren(i); }
	BuildScrollBar();
	LayoutScrollBar();
	// Монеты и подписи вкладок создаются уже после разбора — стили применяем повторно.
	ApplyTextStyles();

	if (!bZoneBound)
	{
		// Своя зона интерфейса: пока курсор над корпусом окна — мир кликов не получает.
		WM->RegisterUiZone(kSkillWinId, FAsdaUiZoneRect::CreateWeakLambda(this,
			[this](FVector2D& Mn, FVector2D& Mx) { return GetWindowRect(Mn, Mx); }));
		// Колесо мыши = прокрутка сетки (эталон msg 0x17 контрола 12291 SkillTree_Wheel).
		WM->RegisterWheelHandler(kSkillWinId, FAsdaUiWheelHandler::CreateUObject(this, &UAsdaSkillWindowSubsystem::OnWheel));
		bZoneBound = true;
	}
	// ★★★ ШАГ 20 ЭТАЛОННОГО `uiCloseAllWindows@0xD61960` (ui_core.c:54382): `uiPanel_Hide(g_pSkillWnd)`
	//   — `CControl_SetVisible(m_pSkillWin, 0)` + `uiPanel_SetTabVisible(g_pSystemMainWnd, 7, 0)` +
	//   гашение подсказки (ui_core.c:72114-72127). Ровно это делает наш `CloseWindow`.
	//   ⚠ Условия «если открыто» в эталоне НЕТ — вызов безусловный, вместе со звуком закрытия;
	//     наш `CloseWindow` тоже безусловный, поэтому расхождения нет.
	WM->RegisterWindowCloser(kSkillWinId,
		FSimpleDelegate::CreateUObject(this, &UAsdaSkillWindowSubsystem::CloseWindow));
	return true;
}

void UAsdaSkillWindowSubsystem::ResetWidgets()
{
	Win = nullptr;
	for (FAsdaSkillSlotWidgets& S : Slots) { S = FAsdaSkillSlotWidgets(); }
	Relays.Reset();
	TxtSkillPoint = nullptr; TxtPointBack = nullptr; TxtTreeCaption = nullptr; TxtTreeSpent = nullptr;
	TxtJob4Point = nullptr; TxtJob4Back = nullptr; Job4Frame = nullptr; Job4Button = nullptr;
	Job4ButtonText = nullptr; Job4PointFrame = nullptr; CloseButton = nullptr;
	GearStatic = nullptr; GearAnimated = nullptr;
	Tab3Buttons.Reset(); Tab3Texts.Reset(); Tab4Buttons.Reset(); Tab4Texts.Reset();
	Tab3Planes.Reset(); CedTimers.Reset();
	// ★ Обязательно: иначе `BuildScrollBar` увидит собранную полосу (`IsBuilt()`) от ПРОШЛОГО виджета
	//   окна и не соберёт её заново (та же грабля, что в инвентаре, — там полоса сбрасывается так же).
	WheelPanel = nullptr; ScrollHost = nullptr;
	ScrollBar = FAsdaScrollBarNomal();
	Tab3TextTemplate = nullptr; Tab4TextTemplate = nullptr;
	PreviewPanel = nullptr; PreviewTitle = nullptr;
	SoulRows.Reset(); TreeArt.Reset();
	CoinTemplates.Reset(); CoinTextTemplates.Reset();
	HoverSlot = -1;
	SelectedSlot = -1;
	// ★ Аудит №3 (B4): призрак живёт в дереве ЭТОГО виджета — забываем структуру целиком, иначе
	//   `Begin` увидит `UImage` из мёртвого дерева и не соберёт новый (та же причина, что у полосы).
	DragGhost = FAsdaDragGhost();
	DragArm.Disarm(); DragSrcSlot = -1; DragSkillCode = -1; bDragActive = false;
	SoulHoverRow = -1;
	bSoulHoverBig = false;
}

void UAsdaSkillWindowSubsystem::OpenWindow()
{
	// ★ Волна В3 ремесла (ревизия В0–В2 С-1): `skillwin_OpenSkillWindow@0xE52DE0` (skill_buff.c:32749) — всё открытие
	//   под `... && !tileInteract_isControlVisible(g_pItemMainWnd) && !isBoundControlVisible(g_pItemQtyMakeDlg)` — МОЛЧА.
	if (UAsdaCraftSubsystem::IsAnyCraftWindowOpen(GetGameInstance()))
	{
		UE_LOG(LogAsdaSkillWin, Log, TEXT("[навыки] окно не открыто молча: открыто окно ремесла 76000/77500 (skill_buff.c:32749)"));
		return;
	}
	// ★ Волна Л5 торговли (30.09.2026): [ОРИГИНАЛ] `skillwin_OpenSkillWindow@0xE52DE0` (skill_buff.c:32751-32754) — после молчаливых
	//   гейтов `if (sub_934E86(g_pSellWindow) || sub_929DC4(g_bSomeFeatureEnabled))` → 107022 «Ты в своей лавке» в чат (−11514, 10 с),
	//   окно не открывается. Подготовка 22000 или витрина 23000 (и у хозяина, и у гостя) — общая точка `UAsdaTradeStateSubsystem`.
	if (UAsdaTradeStateSubsystem::IsSelfStoreOpen(GetGameInstance()))
	{
		UAsdaTradeStateSubsystem::ChatStoreGate(GetGameInstance(), kLocBusy, TEXT("skillwin_OpenSkillWindow"));
		return;
	}
	// ★ Гейт эталона `skillwin_OpenSkillWindow@0xE52DE0`: без выбранной профессии окно не открывается.
	const int32 Job = GetSelfJob();
	if (Job <= 0)
	{
		Notice(kLocNoJob);
		UE_LOG(LogAsdaSkillWin, Log, TEXT("[навыки] окно не открыто: профессия не выбрана (job=%d)"), Job);
		return;
	}
	if (GetSelfBand() == 0)
	{
		// [ПОРТ] (поправлено 26.10.2026 по Reports/class_34_39/RESEARCH.md §2.3; прежний комментарий «эталон сюда не доходит» был
		//   неверен): эталон `skillwin_OpenSkillWindow` (skill_buff.c:32756-32803) открывает окно при любом классе ≠ 0 — у ремесленных
		//   34…39 и меток 40+ (полоса 0) это три пустые вкладки и кнопка 4-й профессии с пустым деревом: навыков у них нет ни в одной
		//   таблице (клиент и сервер). Порт вместо пустого окна показывает нотис 9421 тем же однокнопочным окном, что и гейт «класс 0».
		//   Такой класс штатно недостижим (смена профессии на 34…39 не принимается сервером, `SetJobChangeProcess` GameServer.c:39106).
		Notice(kLocNoJob);   // 9421 — однокнопочный msgbox, как в эталоне
		UE_LOG(LogAsdaSkillWin, Warning, TEXT("[навыки] класс %d вне боевых полос — дерева нет"), Job);
		return;
	}
	if (!EnsureWidget()) { return; }

	// Подстраховка: если окно открыли раньше, чем сработала подписка на очки, берём число напрямую
	//   (блок char-select 1017 +39 = `CCharacter+0x443`, см. UAsdaNetSubsystem::SelfSkillPoints).
	if (SkillPoints < 0)
	{
		if (UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>())
		{
			if (Net->SelfSkillPoints >= 0) { SkillPoints = Net->SelfSkillPoints; }
		}
	}

	CurTree = 0;
	ScrollRow = 0;
	SelectedSlot = -1;
	RebuildAll();

	if (UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>())
	{
		WM->SetWindowVisible(kSkillWinId, true);
		WM->BringWindowToFront(kSkillWinId);
	}
	if (UAsdaSoundSubsystem* Snd = GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>())
	{
		Snd->PlaySound2DById(OpenSoundId);
	}
	UE_LOG(LogAsdaSkillWin, Log, TEXT("[навыки] окно открыто: класс=%d полоса=%d очков=%d записей=%d"),
		Job, GetSelfBand(), SkillPoints, Learned.Num());
}

void UAsdaSkillWindowSubsystem::CloseWindow()
{
	// Окно закрыли посреди перетаскивания — «руку» разжимаем (эталон теряет захват вместе с окном).
	if (bDragActive) { CancelSkillDragVisual(); DragSkillCode = -1; }
	DragArm.Disarm();
	HideOwnTooltip();
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kSkillWinId, false);
	}
	if (UAsdaSoundSubsystem* Snd = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		Snd->PlaySound2DById(CloseSoundId);
	}
}

void UAsdaSkillWindowSubsystem::ToggleWindow()
{
	if (IsWindowOpen()) { CloseWindow(); } else { OpenWindow(); }
}

// ══════════════════════════════ РАЗБОР ДЕРЕВА ВИДЖЕТОВ ═════════════════════════════════════════
// Имя вида «SKILLTREE_ICON07_grp» → 7. −1, если это не группа узла.
static int32 SlotIndexFromGroupName(const FString& Name)
{
	static const FString Pre = TEXT("SKILLTREE_ICON");
	static const FString Suf = TEXT("_grp");
	if (!Name.StartsWith(Pre) || !Name.EndsWith(Suf)) { return -1; }
	const FString Mid = Name.Mid(Pre.Len(), Name.Len() - Pre.Len() - Suf.Len());
	if (Mid.Len() != 2 || !FChar::IsDigit(Mid[0]) || !FChar::IsDigit(Mid[1])) { return -1; }
	const int32 Idx = FCString::Atoi(*Mid);
	return (Idx >= 0 && Idx < 45) ? Idx : -1;
}

void UAsdaSkillWindowSubsystem::BindSlotGroup(UCanvasPanel* Grp, int32 SlotIdx)
{
	if (!Grp || !Slots.IsValidIndex(SlotIdx)) { return; }
	FAsdaSkillSlotWidgets& S = Slots[SlotIdx];
	S.Group = Grp;

	const FString NN = FString::Printf(TEXT("%02d"), SlotIdx);
	const FString PreFrame = FString::Printf(TEXT("SKILLTREE_ICON%s_"), *NN);          // рамка слота
	const FString PreCool  = FString::Printf(TEXT("SKILLTREE_COOL%s"), *NN);
	const FString PreCed   = FString::Printf(TEXT("SKILLTREE_CED%s"), *NN);
	const FString PreDlv   = FString::Printf(TEXT("SKILLTREE_DLV%s"), *NN);
	const FString PreSelA  = FString::Printf(TEXT("SKILLTREE_SELECTABLE%s"), *NN);
	const FString PreSelD  = FString::Printf(TEXT("SKILLTREE_SELECTED%s"), *NN);

	for (int32 i = 0; i < Grp->GetChildrenCount(); ++i)
	{
		UWidget* W = Grp->GetChildAt(i);
		if (!W) { continue; }
		const FString N = W->GetName();
		if (N.StartsWith(PreSelA)) { if (UImage* Img = Cast<UImage>(W)) { S.Selectable = Img; } continue; }
		if (N.StartsWith(PreSelD)) { S.Selected = W; continue; }
		if (N.StartsWith(PreCool))
		{
			if (UTextBlock* T = Cast<UTextBlock>(W))
			{
				S.CoolText = T;
			}
			else
			{
				S.Cool = W;
				// Шторка узла — кадровый план `CoolTime_Effect` (118 кадров): гоним её той же
				//   механикой, что и шторки панели (`UAsdaHotbarSubsystem::DriveCurtain`).
				S.Curtain.Plane = Cast<UAsdaAnimatedPlane>(W);
				S.Curtain.ShownKey = -1; S.Curtain.ShownState = 0; S.Curtain.ShownEndS = 0.0;
				if (!S.Curtain.Plane)
				{
					UE_LOG(LogAsdaSkillWin, Warning,
						TEXT("[навыки] узел %d: контрол SKILLTREE_COOL%s не кадровый план (%s) — ")
						TEXT("шторку отката соберём по геометрии иконки"),
						SlotIdx, *NN, *W->GetClass()->GetName());
				}
			}
			continue;
		}
		if (N.StartsWith(PreCed)) { if (!Cast<UTextBlock>(W)) { S.Ced = W; } continue; }
		if (N.StartsWith(PreDlv)) { if (UTextBlock* T = Cast<UTextBlock>(W)) { S.Level = T; } continue; }
		if (!N.StartsWith(PreFrame)) { continue; }

		// ★★★ РАЗЛИЧЕНИЕ РАМКИ И ИКОНКИ. Импортёр (закон jui-importer-widget-naming) даёт картинкам имя
		//   `{idc}_{сквозной_индекс}_{план}`, поэтому в одной группе живут:
		//     рамка  `SKILLTREE_ICON19_197_0`   (idc = SKILLTREE_ICON19, JUI-индекс 197)
		//     иконка `SKILLTREE_ICON19_19_198_0`(idc = SKILLTREE_ICON19_19)
		//   ⚠ ПРЕФИКСНОЕ сравнение по «SKILLTREE_ICON19_19» ЛОВИТ И РАМКУ («197…» начинается на «19»).
		//   На слотах 19/20/21 (JUI-индексы 197/204/211) рамка биндилась как иконка: подложка
		//   исчезала, а картинка навыка рисовалась в прямоугольнике рамки 42×42 на (11,11) вместо
		//   32×32 на (16,16) — ровно «смещённые значки без подложки» из живого теста.
		//   Сравниваем ТОКЕН целиком: первый кусок после «SKILLTREE_ICON{NN}_» до следующего «_».
		FString Tail = N.Mid(PreFrame.Len());
		FString FirstToken = Tail;
		int32 Us = INDEX_NONE;
		if (Tail.FindChar(TEXT('_'), Us)) { FirstToken = Tail.Left(Us); }
		UImage* Img = Cast<UImage>(W);
		if (!Img) { continue; }
		if (FirstToken == NN) { if (!S.Icon)  { S.Icon  = Img; } }
		else                  { if (!S.Frame) { S.Frame = Img; } }
	}

	if (!S.Frame || !S.Icon)
	{
		UE_LOG(LogAsdaSkillWin, Warning, TEXT("[навыки] узел %02d: рамка=%s иконка=%s — разбор группы неполный"),
			SlotIdx, S.Frame ? TEXT("есть") : TEXT("НЕТ"), S.Icon ? TEXT("есть") : TEXT("НЕТ"));
	}
}

void UAsdaSkillWindowSubsystem::ScanWidgets()
{
	if (!Win || !Win->WidgetTree) { return; }
	TArray<UWidget*> All;
	Win->WidgetTree->GetAllWidgets(All);

	Tab3Buttons.SetNum(3); Tab4Buttons.SetNum(4); Tab3Planes.SetNum(3);
	CoinTemplates.SetNum(3); CoinTextTemplates.SetNum(3);
	SoulRows.SetNum(2);
	for (FAsdaSoulPreviewRow& R : SoulRows) { R.Tiers.SetNum(3); }
	TreeArt.SetNum(15);

	// ── Левая панель «Хранитель души» (56300). Импортёр НЕ дал именам idc: у контролов 56304/56308..56313
	//   в JUI пустой `idc`, поэтому обращаемся по СТРУКТУРЕ — через группы `soul_box1_grp` (строка 0)
	//   и `Box_Black_grp` (строка 1), внутри — по типу и размеру плана (126×190 портрет, 32×32 иконка).
	auto BindSoulRow = [this](UCanvasPanel* Box, int32 RowIdx, const TCHAR* TierPrefix)
	{
		if (!Box || !SoulRows.IsValidIndex(RowIdx)) { return; }
		FAsdaSoulPreviewRow& R = SoulRows[RowIdx];
		for (int32 i = 0; i < Box->GetChildrenCount(); ++i)
		{
			UWidget* C = Box->GetChildAt(i);
			if (!C) { continue; }
			// Якоря у импортированных планов точечные, поэтому Right/Bottom оффсетов = ширина/высота.
			const UCanvasPanelSlot* Cs = AsdaUi::SlotOf(C);
			const FMargin Off = Cs ? Cs->GetOffsets() : FMargin(0.f);
			const FVector2D Sz(Off.Right, Off.Bottom);
			if (UImage* Img = Cast<UImage>(C))
			{
				// 126×190 — портрет (56302/56308); 32×32 — иконка навыка хранителя (56304/56310).
				if (FMath::IsNearlyEqual(Sz.X, 126.f) && FMath::IsNearlyEqual(Sz.Y, 190.f)) { if (!R.Illust)   { R.Illust   = Img; } }
				else if (FMath::IsNearlyEqual(Sz.X, 32.f) && FMath::IsNearlyEqual(Sz.Y, 32.f)) { if (!R.NextIcon) { R.NextIcon = Img; } }
				continue;
			}
			if (UTextBlock* T = Cast<UTextBlock>(C))
			{
				// Подпись под портретом (56303/56309) — единственный текст шириной 126 ниже портрета.
				if (FMath::IsNearlyEqual(Sz.X, 126.f) && Sz.Y <= 20.f && !R.Name) { R.Name = T; }
				continue;
			}
			// Вложенная группа с плашками ступеней (JUI: `box`/`NoImage`; планы Text_1st/2nd/3rd).
			if (UCanvasPanel* Sub = Cast<UCanvasPanel>(C))
			{
				for (int32 k = 0; k < Sub->GetChildrenCount(); ++k)
				{
					UWidget* Lb = Sub->GetChildAt(k);
					if (!Lb || !Cast<UImage>(Lb)) { continue; }
					const FString LN = Lb->GetName();
					if (LN.StartsWith(FString(TierPrefix) + TEXT("1st"))) { R.Tiers[0] = Lb; }
					else if (LN.StartsWith(FString(TierPrefix) + TEXT("2nd"))) { R.Tiers[1] = Lb; }
					else if (LN.StartsWith(FString(TierPrefix) + TEXT("3nd")) ||
					         LN.StartsWith(FString(TierPrefix) + TEXT("3rd"))) { R.Tiers[2] = Lb; }
				}
			}
		}
	};

	for (UWidget* W : All)
	{
		if (UCanvasPanel* Cp = Cast<UCanvasPanel>(W))
		{
			// Строка 0: `soul_box1_grp` (планы SKILL_SOUL_1st/2nd/3nd). Строка 1: `Box_Black_grp` (Text_1st/2nd/3rd).
			if (Cp->GetName().StartsWith(TEXT("soul_box1_grp"))) { BindSoulRow(Cp, 0, TEXT("SKILL_SOUL_")); }
			else if (Cp->GetName().StartsWith(TEXT("Box_Black_grp"))) { BindSoulRow(Cp, 1, TEXT("Text_")); }
		}
	}

	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();

		if (UCanvasPanel* Cp = Cast<UCanvasPanel>(W))
		{
			const int32 Idx = SlotIndexFromGroupName(N);
			if (Idx >= 0) { BindSlotGroup(Cp, Idx); continue; }
		}

		// Именованные текстовые контролы (idnum в комментариях заголовка).
		if (UTextBlock* T = Cast<UTextBlock>(W))
		{
			if (N == TEXT("SKILL_POINTTXT"))        { TxtSkillPoint  = T; continue; }
			if (N == TEXT("SKILL_POINTBACK"))       { TxtPointBack   = T; continue; }
			if (N == TEXT("SKILL_TXT00"))           { TxtTreeCaption = T; continue; }
			if (N == TEXT("SKILL_POINT_TXT"))       { TxtTreeSpent   = T; continue; }
			if (N == TEXT("SKILL_POINTTXT2"))       { TxtJob4Point   = T; continue; }
			if (N == TEXT("SKILL_POINTBACK2"))      { TxtJob4Back    = T; continue; }
			if (N == TEXT("SOUL_SP_W_T_Text"))      { Job4ButtonText = T; continue; }
			if (N == TEXT("SKILL_SOUL_TEXT_p1"))    { PreviewTitle   = T; continue; }
			if (N == TEXT("SKILL_CHARA_BTN1_Text")) { Tab3TextTemplate = T; continue; }
			if (N == TEXT("SKILL_CHARA_BTN2_Text")) { Tab4TextTemplate = T; continue; }
			if (N == TEXT("SKILL_SOUL_LV1"))        { CoinTextTemplates[0] = T; continue; }
			if (N == TEXT("SKILL_SOUL_LV2"))        { CoinTextTemplates[1] = T; continue; }
			if (N == TEXT("SKILL_SOUL_LV3"))        { CoinTextTemplates[2] = T; continue; }
			continue;
		}

		if (UButton* B = Cast<UButton>(W))
		{
			if (N == TEXT("SKILL_WIN_CLOSEBTN")) { CloseButton = B; continue; }
			if (N == TEXT("SOUL_SP_W_T"))        { Job4Button  = B; continue; }
			if (N.StartsWith(TEXT("SKILL_CHARA_BTN1_OPT")))
			{
				const int32 K = FCString::Atoi(*N.RightChop(N.Len() - 1));
				if (Tab3Buttons.IsValidIndex(K)) { Tab3Buttons[K] = B; }
				continue;
			}
			if (N.StartsWith(TEXT("SKILL_CHARA_BTN2_OPT")))
			{
				const int32 K = FCString::Atoi(*N.RightChop(N.Len() - 1));
				if (Tab4Buttons.IsValidIndex(K)) { Tab4Buttons[K] = B; }
				continue;
			}
			continue;
		}

		if (UImage* Img = Cast<UImage>(W))
		{
			if (N.StartsWith(TEXT("Skill_Gear_")))       { GearStatic    = Img; continue; }
			// Планы опций селектора 12005: имена `SKILL_CHARA_BTN1_{juiIdx}_{опция}`.
			if (N.StartsWith(TEXT("SKILL_CHARA_BTN1_")))
			{
				TArray<FString> Tok; N.ParseIntoArray(Tok, TEXT("_"), true);
				if (Tok.Num() >= 2 && Tok.Last().IsNumeric() && Tok[Tok.Num() - 2].IsNumeric())
				{
					const int32 K = FCString::Atoi(*Tok.Last());
					if (Tab3Planes.IsValidIndex(K)) { Tab3Planes[K] = Img; }
				}
				continue;
			}
			if (N.StartsWith(TEXT("BALL_GREEN_SS_")))    { CoinTemplates[0] = Img; continue; }
			if (N.StartsWith(TEXT("BALL_BLUE_SS_")))     { CoinTemplates[1] = Img; continue; }
			if (N.StartsWith(TEXT("BALL_RED_SS_")))      { CoinTemplates[2] = Img; continue; }
			continue;
		}

		if (N.StartsWith(TEXT("Skill_AniGear")))     { GearAnimated   = W; continue; }
		if (N == TEXT("SOUL_SP_W_grp"))              { Job4Frame      = W; continue; }
		if (N == TEXT("SKILL_BLINE_2_grp"))          { Job4PointFrame = W; continue; }
		if (N == TEXT("SKILL_SOUL_MAIN_grp"))        { PreviewPanel   = W; continue; }
		if (N == TEXT("SkillTree_Wheel_grp"))        { WheelPanel = Cast<UCanvasPanel>(W); continue; }

		// Планы-подложки дерева (эталон `skillPanel_selectTreeByClass@0xE581A0`, таблица из 15 контролов).
		//   Имена из JUI: FB/AB/WB = Fighter/Archer/Wizard(Mage); 0..2 — вкладки 0..2, 4 — вкладка 4-й профессии.
		//   ⚠ FB3/AB3/WB3 (12263/12265/12267) в таблицу НЕ входят — ссылок в срезе нет.
		{
			// Индексы 0..8 = 3*(полоса−1) + вкладка — ровно как эталонное `v5` в selectTreeByClass.
			//   Индексы 9..14 (вкладка 4-й профессии) в срезе НЕ восстановлены: точный порядок хвоста
			//   `ctrlIdTable[15]` из бинаря не извлечён, а сами кандидаты FB3/FB4/AB3/AB4/WB3/WB4
			//   (12263..12268) в JUI ПУСТЫ — 5×5 `NoImage2` без единого потомка, импортёр их не создал.
			//   Поэтому под вкладкой 4-й профессии подложка не рисуется (см. RefreshTreeArt).
			static const TCHAR* kArtNames[15] = {
				TEXT("SKILLTREE_ARR_FB0_grp"), TEXT("SKILLTREE_ARR_FB1_grp"), TEXT("SKILLTREE_ARR_FB2_grp"),
				TEXT("SKILLTREE_ARR_AB0_grp"), TEXT("SKILLTREE_ARR_AB1_grp"), TEXT("SKILLTREE_ARR_AB2_grp"),
				TEXT("SKILLTREE_ARR_WB0_grp"), TEXT("SKILLTREE_ARR_WB1_grp"), TEXT("SKILLTREE_ARR_WB2_grp"),
				TEXT("SKILLTREE_ARR_N1_ARCH_grp"), TEXT("SKILLTREE_ARR_N2_MA_grp"), TEXT("SKILLTREE_ARR_N3_WAR_grp"),
				TEXT(""), TEXT(""), TEXT("") };
			bool bTaken = false;
			for (int32 k = 0; k < 15; ++k)
			{
				if (kArtNames[k][0] && N == kArtNames[k]) { TreeArt[k] = W; bTaken = true; break; }
			}
			if (bTaken) { continue; }
		}
	}

	int32 BoundSlots = 0, BoundFrames = 0;
	for (const FAsdaSkillSlotWidgets& S : Slots) { if (S.Group) { ++BoundSlots; } if (S.Frame) { ++BoundFrames; } }
	int32 SoulOk = 0;
	for (const FAsdaSoulPreviewRow& R : SoulRows) { if (R.Illust && R.Name && R.NextIcon) { ++SoulOk; } }
	int32 ArtOk = 0;
	for (const TObjectPtr<UWidget>& A : TreeArt) { if (A) { ++ArtOk; } }
	UE_LOG(LogAsdaSkillWin, Log,
		TEXT("[навыки] разбор окна: узлов %d/45 (рамок %d), вкладок 3=%d 4=%d, шаблонов цены %d, счётчик очков=%s, строк хранителя %d/2, подложек дерева %d/15"),
		BoundSlots, BoundFrames, Tab3Buttons.Num(), Tab4Buttons.Num(), CoinTemplates.Num(),
		TxtSkillPoint ? TEXT("есть") : TEXT("НЕТ"), SoulOk, ArtOk);

	BuildTabCaptions();
	ApplyTextStyles();
}

// ★★★ ОБРЕЗКА ОБЛАСТИ ПРОКРУТКИ — В АССЕТЕ (19.09.2026), рантайм-правка `ApplyScrollClip` УДАЛЕНА
//   (закон `no-runtime-widget-fixes`). В эталоне план-подложка дерева — ребёнок контрола 12291
//   `SkillTree_Wheel` типа `WheelWindow`, и колёсное окно рисует детей только внутри своего
//   прямоугольника (Origin (1,0), Size 400×477 внутри 12010). Раньше импортёр строил группу
//   `SkillTree_Wheel_grp` как union потомков (138,0,406,622) без обрезки, и код при каждом открытии
//   сужал её до (144,8,400,477), включал ClipToBounds и сдвигал ребёнка на (−6,−8). Теперь то же самое
//   записано в `WBP_SKILL_WIN` шагом `Tools/asda_ui_scroll_geometry_fix.py` (и исправленный импортёр
//   строит так сам), а повторный сдвиг кодом увёл бы подложку на (−6,−8) ещё раз.
//   ⚠ Узлы сетки обрезка не касается: `Skill_Icons` — брат колёсного окна, а не его ребёнок.
// ★★★ ЦВЕТ/ВЫРАВНИВАНИЕ ТЕКСТОВ — строго по блоку `EXS` каждого контрола (закон ui-text-color-exs).
//   Значения ниже вычитаны из `Reports/_skillwin_jui.json` (поля `TextColor` / `TextAlign`),
//   а НЕ из `CaptionInfo` (`cap`): у контролов с EXS цвет из `cap` игнорируется движком.
//   Для контролов БЕЗ блока EXS (12005/12006, 56303, 12401) действует цвет из `cap` — он и указан.
//   TextAlign: «4» = влево, «5» = по центру.
//   ★ Закон ui-srgb-gamma-rule: числа в JUI — sRGB, в UE кладём через FLinearColor::FromSRGBColor.
void UAsdaSkillWindowSubsystem::ApplyTextStyles()
{
	// ★★★ ОТКУДА БЕРЁТСЯ ОБВОДКА — восстановлено по бинарю (прошлый вывод был неполным).
	//
	//   1) Ключ скина `TextOutLine` — ЭТО НЕ ОБВОДКА. Он попадает в поле контрола 925, а рендер
	//      (`cControl_RenderLabel`, ui_core.c:110741) делает:
	//         flags = TextAlign; flags |= field926 | (field925 == 1 ? 0x2010 : 0x100);
	//      то есть 0x2010 = перенос по словам + клип, 0x100 = без клипа. У всех скинов `NoImage*`
	//      он равен 1, поэтому «обводка у всех» — ложный след.
	//
	//   2) НАСТОЯЩИЙ стиль текста — поле контрола **926**, выставляется `CClass_SetField926`.
	//      Глиф-рендер (font_text.c:6832+) читает его как styleFlags:
	//         0x200000 — рисовать глиф ДВАЖДЫ со сдвигом ±1 по X  ⇒ ЖИРНЫЙ (inline-тег `<FB>`);
	//         0x400000 — один глиф со сдвигом (+1,+1) цветом shadowColor ⇒ ТЕНЬ (тег `<FD>`);
	//         `dword_800000` — тот же флаг, что ставит тег `<FO>` ⇒ ОБВОДКА.
	//
	//   3) Кому что ставит ЭТО окно:
	//      • `CSkillWindow_InitControls` (skill_buff.c:32451/32498/32545) — всем 45 текстам уровня
	//        `SKILLTREE_DLV{NN}` (индекс записи слота 7): `SetField926(dword_800000)` ⇒ ОБВОДКА.
	//        Тем же флагом рисуются строки чата, серверные нотисы и плашки над головой — всё, что
	//        лежит поверх пёстрого фона. Отсюда чёрный контур у цифр уровня.
	//      • `skillPanelBindControls` (skill_buff.c:34230) — заголовку панели 56301 «Хранитель души»:
	//        `SetField926(0x200000)` ⇒ ЖИРНЫЙ, а НЕ обводка.
	//      • «Доступные очки навыка» (12262) и остальные тексты окна — `SetField926` им не зовётся
	//        вовсе ⇒ ни обводки, ни жирного.
	//   ⇒ Контур положен РОВНО цифрам уровня. Ниже он им и ставится, а у всех прочих снимается.
	auto Style = [](UTextBlock* T, uint32 ArgbSrgb, ETextJustify::Type Align, bool bOutline = false)
	{
		if (!T) { return; }
		{
			FSlateFontInfo F = T->GetFont();
			F.OutlineSettings.OutlineSize = bOutline ? 1 : 0;
			F.OutlineSettings.bSeparateFillAlpha = false;
			F.OutlineSettings.bApplyOutlineToDropShadows = false;
			F.OutlineSettings.OutlineColor = bOutline ? FLinearColor(0.f, 0.f, 0.f, 1.f) : FLinearColor::Transparent;
			T->SetFont(F);
		}
		const FColor C(uint8((ArgbSrgb >> 16) & 0xFF), uint8((ArgbSrgb >> 8) & 0xFF),
		               uint8(ArgbSrgb & 0xFF), uint8((ArgbSrgb >> 24) & 0xFF));
		FLinearColor L = FLinearColor::FromSRGBColor(C);
		L.A = C.A / 255.f;
		T->SetColorAndOpacity(FSlateColor(L));
		T->SetJustification(Align);
		T->SetAutoWrapText(false);   // ★ ui-autowrap-canvas-gotcha
	};

	Style(TxtPointBack,   0xFF403507, ETextJustify::Center);  // 12262 EXS TextColor FF403507, Align 5
	Style(TxtSkillPoint,  0xFF000000, ETextJustify::Center);  // 1092  EXS FF000000, Align 5
	Style(TxtJob4Back,    0xFF000000, ETextJustify::Center);  // 12411 EXS FF000000, Align 5
	Style(TxtJob4Point,   0xFF000000, ETextJustify::Left);    // 12413 EXS FF000000, Align 4
	Style(TxtTreeCaption, 0xFFC0C0C0, ETextJustify::Left);    // 12261 EXS FFC0C0C0, Align 4
	Style(TxtTreeSpent,   0xFFFFD21B, ETextJustify::Center);  // 12007 EXS FFFFD21B, Align 5
	Style(PreviewTitle,   0xFF000000, ETextJustify::Center);  // 56301 EXS FF000000, Align 5
	// 56303/56309 — блока EXS нет, цвет берётся из CaptionInfo (`cap` = ffe1e1e1).
	for (const FAsdaSoulPreviewRow& R : SoulRows) { Style(R.Name, 0xFFE1E1E1, ETextJustify::Center); }
	// 12401 SOUL_SP_W_T — EXS нет, `cap` = ff323232.
	Style(Job4ButtonText, 0xFF323232, ETextJustify::Center);
	// ★ Уровень в узле (12015 и остальные 44) — EXS FFFFFFFF, Align 5 + ЧЁРНАЯ ОБВОДКА
	//   (`SetField926(dword_800000)` = тег `<FO>`; см. разбор выше).
	for (const FAsdaSkillSlotWidgets& Sl : Slots) { Style(Sl.Level, 0xFFFFFFFF, ETextJustify::Center, /*bOutline*/ true); }
	// Числа цены (56321..56323) — EXS FFFFFFFF, Align 4.
	for (const FAsdaSkillSlotWidgets& Sl : Slots)
	{
		for (const TObjectPtr<UTextBlock>& T : Sl.CoinTexts) { Style(T, 0xFFFFFFFF, ETextJustify::Left); }
	}
	// ★★★ ПОДПИСИ ОПЦИЙ СЕЛЕКТОРОВ 12005/12006 ЗДЕСЬ БОЛЬШЕ НЕ КРАСЯТСЯ (правка 2026-08-27).
	//   ЧТО БЫЛО: обеим полосам ставился цвет `ffe2e2c7` из `cap` контрола — то есть оформление
	//   жило в C++ и вдобавок спорило с ассетом: у 12005 скин `Skill_W124_3Btn` СВЕТЛЫЙ, и светлая
	//   подпись на нём не читалась (строка `SKILL_CHARA_BTN1` таблицы `TABBARS` помечена
	//   «★сливается», владелец 2026-08-27 назначил ей тёмно-серый `ff4c4c4c`).
	//   КАК СТАЛО: цвет подписи приходит ИЗ АССЕТА — клоны берут его у образца
	//   `SKILL_CHARA_BTN{1,2}_Text`, которому его кладёт шаг `Tools/asda_ui_tabbar_captions.py`.
	//   В коде осталась только ПОДСВЕТКА АКТИВНОЙ вкладки — общая точка `AsdaUi::ApplyTabCaptionState`
	//   в `RebuildTabs` (закон `port-shared-points`). Выравнивание и перенос ставит `BuildTabCaptions`.

	// ★★★ СПЛОШНАЯ ЗАЧИСТКА ОБВОДКИ У ВСЕХ ОСТАЛЬНЫХ. Импортёр запёк непустой
	//   `FSlateFontInfo::OutlineSettings` во все текстовые блоки WBP; по данным (`SetField926`)
	//   контур положен ТОЛЬКО цифрам уровня, поэтому у всех прочих снимаем.
	//   ⚠ Цифры уровня из прохода ИСКЛЮЧЕНЫ явным списком — иначе этот же проход стирал бы контур,
	//   который мы только что им поставили (ровно это и случилось в прошлом заходе).
	if (Win && Win->WidgetTree)
	{
		TSet<const UWidget*> Keep;
		for (const FAsdaSkillSlotWidgets& Sl : Slots) { if (UTextBlock* L = Sl.Level.Get()) { Keep.Add(L); } }
		int32 Cleared = 0;
		Win->WidgetTree->ForEachWidget([&Cleared, &Keep](UWidget* W)
		{
			UTextBlock* T = Cast<UTextBlock>(W);
			if (!T || Keep.Contains(T)) { return; }
			FSlateFontInfo F = T->GetFont();
			if (F.OutlineSettings.OutlineSize == 0) { return; }
			F.OutlineSettings.OutlineSize = 0;
			F.OutlineSettings.bSeparateFillAlpha = false;
			F.OutlineSettings.OutlineColor = FLinearColor::Transparent;
			T->SetFont(F);
			++Cleared;
		});
		UE_LOG(LogAsdaSkillWin, Log,
			TEXT("[навыки] обводка: оставлена у %d цифр уровня (SetField926 = тег <FO>), снята у %d прочих текстов"),
			Keep.Num(), Cleared);
	}
}

// Эталон `CSkillWindow_InitControls@0xE50DB0` клонирует в КАЖДЫЙ узел шаблоны 56320 → 56325..56327 →
//   56321..56323. WBP содержит только сами шаблоны, поэтому экземпляры создаём в рантайме — 1:1.
void UAsdaSkillWindowSubsystem::BuildCoinChildren(int32 SlotIdx)
{
	if (!Slots.IsValidIndex(SlotIdx)) { return; }
	FAsdaSkillSlotWidgets& S = Slots[SlotIdx];
	if (!S.Group || !Win || !Win->WidgetTree) { return; }
	if (S.CoinIcons.Num() == 3) { return; }

	// ★★★ ТОЧКА ОТСЧЁТА. Эталон ставит шаблон-контейнер на (39,2) ОТНОСИТЕЛЬНО КОНТРОЛА СЛОТА
	//   (`SKILLTREE_ICON{NN}`, 42×42) и клонирует его как ЕГО ребёнка. Импортёр же обернул слот в группу
	//   `SKILLTREE_ICON{NN}_grp` (64×64), где рамка слота стоит на (11,11) — под план `SKILLTREE_CED{NN}`
	//   с JUI-origin (−11,−11). Если класть монеты в группу по (39,2), они уезжают на 11 px влево-вверх
	//   и ложатся ПОВЕРХ иконки навыка (в живом тесте — «по два-три значка на узел внахлёст»).
	//   Берём фактическую позицию рамки из её слота на канве.
	FVector2D SlotOrigin(11.f, 11.f);
	if (S.Frame)
	{
		if (const UCanvasPanelSlot* Fs = AsdaUi::SlotOf(S.Frame))
		{
			const FMargin Off = Fs->GetOffsets();
			SlotOrigin = FVector2D(Off.Left, Off.Top);
		}
	}
	const FVector2D Base = SlotOrigin + CoinOrigin;

	S.CoinIcons.SetNum(3);
	S.CoinTexts.SetNum(3);
	for (int32 i = 0; i < 3; ++i)
	{
		UImage* SrcImg = CoinTemplates.IsValidIndex(i) ? CoinTemplates[i].Get() : nullptr;
		UTextBlock* SrcTxt = CoinTextTemplates.IsValidIndex(i) ? CoinTextTemplates[i].Get() : nullptr;
		if (!SrcImg || !SrcTxt) { continue; }

		UImage* Ball = Win->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),
			FName(*FString::Printf(TEXT("SkillCoinBall_%02d_%d"), SlotIdx, i)));
		Ball->SetBrush(SrcImg->GetBrush());
		Ball->SetVisibility(ESlateVisibility::Collapsed);
		if (UCanvasPanelSlot* Cs = S.Group->AddChildToCanvas(Ball))
		{
			Cs->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
			Cs->SetAutoSize(false);
			Cs->SetOffsets(FMargin(Base.X, Base.Y + CoinRowStep * i, CoinBallSize.X, CoinBallSize.Y));
			Cs->SetZOrder(5);
		}

		UTextBlock* Num = Win->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("SkillCoinText_%02d_%d"), SlotIdx, i)));
		Num->SetFont(SrcTxt->GetFont());
		Num->SetColorAndOpacity(SrcTxt->GetColorAndOpacity());
		// JUI шаблонов 56321..56323: TextAlign = "4" (влево/вверх) — берём выравнивание из данных.
		Num->SetJustification(ETextJustify::Left);
		// ★ Закон ui-autowrap-canvas-gotcha: в Canvas перенос выключаем — иначе строка схлопывается.
		Num->SetAutoWrapText(false);
		Num->SetVisibility(ESlateVisibility::Collapsed);
		if (UCanvasPanelSlot* Cs = S.Group->AddChildToCanvas(Num))
		{
			Cs->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
			Cs->SetAutoSize(false);
			Cs->SetOffsets(FMargin(Base.X + CoinTextOffset.X,
			                       Base.Y + CoinRowStep * i + CoinTextOffset.Y,
			                       CoinTextSize.X, CoinTextSize.Y));
			Cs->SetZOrder(6);
		}

		S.CoinIcons[i] = Ball;
		S.CoinTexts[i] = Num;
	}
}

void UAsdaSkillWindowSubsystem::BindButtons()
{
	Relays.Reset();
	auto Bind = [this](UButton* B, TFunction<void(int32)> OnClick, int32 Index)
	{
		if (!B) { return; }
		AsdaUi::BindButton(this, B, Index, MoveTemp(OnClick), Relays);   // ★ U20: реле клика — общая точка
	};

	// Закрытие окна (эталон: msg 0x15 корня 12000 + hover-переключение шестерён на 12009).
	if (CloseButton)
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Callback        = [this](int32) { CloseWindow(); };
		R->HoverCallback   = [this]()
		{
			if (AsdaCloseGear::IsCommonOn()) { return; }   // ★ 08.11.2026: ведёт общая точка UAsdaCloseGearSubsystem
			if (GearStatic)   { GearStatic->SetVisibility(ESlateVisibility::Collapsed); }
			if (GearAnimated) { GearAnimated->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
		};
		R->UnhoverCallback = [this]()
		{
			if (AsdaCloseGear::IsCommonOn()) { return; }
			if (GearStatic)   { GearStatic->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
			if (GearAnimated) { GearAnimated->SetVisibility(ESlateVisibility::Collapsed); }
		};
		CloseButton->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		CloseButton->OnHovered.AddDynamic(R, &UAsdaButtonRelay::OnHovered);
		CloseButton->OnUnhovered.AddDynamic(R, &UAsdaButtonRelay::OnUnhovered);
		AsdaUi::AnchorRelay(Relays, R, CloseButton);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}

	for (int32 i = 0; i < Tab3Buttons.Num(); ++i)
	{
		Bind(Tab3Buttons[i].Get(), [this](int32 Idx) { SetTree(Idx); }, i);
	}
	for (int32 i = 0; i < Tab4Buttons.Num(); ++i)
	{
		Bind(Tab4Buttons[i].Get(), [this](int32 Idx) { SetTree(Idx); }, i);
	}
	// Кнопка 4-й профессии (эталон 12401 → `skillPanelShowTree`: переключение на дерево 3).
	if (Job4Button)
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Callback        = [this](int32) { SetTree(3); };
		R->HoverCallback   = [this]()
		{
			if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>())
			{
				FAsdaTipMetrics M; M.FontPx = TipFontPx; M.MaxW = (float)TipMaxWidth;
				Tip->ShowTextTip(EAsdaTipOwner::SkillWin, LocRaw(kLocJob4TipTitle), LocRaw(kLocJob4TipBody), M);
				bTipShown = true;
			}
		};
		R->UnhoverCallback = [this]() { HideOwnTooltip(); };
		Job4Button->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		Job4Button->OnHovered.AddDynamic(R, &UAsdaButtonRelay::OnHovered);
		Job4Button->OnUnhovered.AddDynamic(R, &UAsdaButtonRelay::OnUnhovered);
		AsdaUi::AnchorRelay(Relays, R, Job4Button);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}
}

void UAsdaSkillWindowSubsystem::OnCloseClicked()   { CloseWindow(); }
void UAsdaSkillWindowSubsystem::OnCloseHovered()   {}
void UAsdaSkillWindowSubsystem::OnCloseUnhovered() {}

// ══════════════════════════════ НАПОЛНЕНИЕ ═════════════════════════════════════════════════════
int32 UAsdaSkillWindowSubsystem::GetSelfJob() const
{
	if (JobOverride >= 0) { return JobOverride; }
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return -1; }
	// Эталон читает `CCharacter+0x438` (gap_438[0]); в порту это `SelfClassId` (см. AsdaNetSubsystem.h).
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U5d): класс — общая точка `AsdaSelf::GetClassId` (байт +0x438, фолбэк — запись char-select rec+24).
	//   Было: фолбэк на `SelfJob` (профессия +0x445).
	return AsdaSelf::GetClassId(Net);
}

int32 UAsdaSkillWindowSubsystem::GetSelfSubJob() const
{
	if (SubJobOverride >= 0) { return SubJobOverride; }
	// Подкласс = `CCharacter+0x439` (`gap_438[1]`). В порту приходит блоком char-select 1017, смещение +29.
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (Net && Net->SelfSubJob >= 0) { return Net->SelfSubJob; }
	// Запасной путь (подкласс ещё не пришёл — до 1017): основной класс. Подпись вкладки 4-й профессии тогда — по основному классу
	//   (персонаж с 4-й ступенью, выставленной прямо в основной класс БД, получает свою подпись).
	return GetSelfJob();
}

int32 UAsdaSkillWindowSubsystem::GetSelfBand() const
{
	return UAsdaSkillDatabase::BandFromJob(GetSelfJob());
}

void UAsdaSkillWindowSubsystem::SetTree(int32 TreeIdx)
{
	CurTree = FMath::Clamp(TreeIdx, 0, 3);
	ScrollRow = 0;
	SelectedSlot = -1;
	RebuildAll();
	UE_LOG(LogAsdaSkillWin, Log, TEXT("[навыки] вкладка %d (grade %d)"),
		CurTree, UAsdaSkillDatabase::GradeForBandTree(GetSelfBand(), CurTree));
}

int32 UAsdaSkillWindowSubsystem::MaxScrollRow() const
{
	// ★ Эталон задаёт колесу ФИКСИРОВАННЫЙ диапазон: `uiControlSetScrollRange(12291, 1, 4)` при
	//   инициализации (4 положения ⇒ ряды 0..3) и `(1, 1)` в `skillPanelShowTree` для вкладки
	//   4-й профессии (одно положение). По содержимому диапазон НЕ считается.
	return (CurTree == 3) ? 0 : 3;
}

void UAsdaSkillWindowSubsystem::SetScrollRow(int32 Row)
{
	const int32 New = FMath::Clamp(Row, 0, MaxScrollRow());
	if (New == ScrollRow) { return; }
	ScrollRow = New;
	HideOwnTooltip();
	RebuildGrid();
	// Эталон msg 0x17 (`skillWndOnUICommand`, контрол 12291) после пересборки сетки зовёт
	//   `skillPanel_selectTreeByClass(..., m_nSelectedOptionIdx)`: подложка дерева едет вверх,
	//   а панель «Хранитель души» переключает пару ступеней (RowIndex 0-1 → 1-2).
	RefreshTreeArt();
	RefreshPreviewPanel();
	LayoutScrollBar();
}

bool UAsdaSkillWindowSubsystem::OnWheel(int32 Notches)
{
	if (!IsWindowOpen()) { return false; }
	FVector2D Mn, Mx;
	if (!GetWindowRect(Mn, Mx)) { return false; }
	FVector2D C;
	if (!AsdaUi::CursorInViewportUnits(Win ? Win->GetWorld() : nullptr, C)) { return false; }
	if (C.X < Mn.X || C.X > Mx.X || C.Y < Mn.Y || C.Y > Mx.Y) { return false; }
	SetScrollRow(ScrollRow - Notches);
	return true;
}
