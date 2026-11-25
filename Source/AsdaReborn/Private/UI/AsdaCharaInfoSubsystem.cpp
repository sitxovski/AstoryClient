// Copyright (c) 2026 @sitxovski (Discord)
// AsdaCharaInfoSubsystem.cpp — окно характеристик персонажа (900) + вкладка эмоций (1100).
// Полный реверс с доказательствами: `Reports/char_info_window.md`.
#include "UI/AsdaCharaInfoSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока

#include "UI/AsdaSystemMenuSubsystem.h"

#include "AsdaCharaInfoSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaCharaInfoSubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaCharInfo);   // ★ R3-c 12.11.2026: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

static const TCHAR* kCharInfoWbp =
	TEXT("/Game/AsdaAssets/UI/Player_UI/Character/WBP_CHARAINFO_WIN.WBP_CHARAINFO_WIN_C");

namespace CharInfoLoc {
	static constexpr int32 kIntroBtn     = 11698;   // «Обо мне»
	static constexpr int32 kBlogBtn      = 11752;   // «Мой блог»

	static constexpr int32 kCons         = 110631;  // «Знак зодиака»

	static constexpr int32 kPreCashLabel = 12959;   // «Деньги в подарок»
}

// ═══════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════════
void UAsdaCharaInfoSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	EmotionSlots.SetNum(30);   // 12 обычных + 6 отряда + 12 навыков (лотки эталона)
}

void UAsdaCharaInfoSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnVitalsChanged.RemoveAll(this);
			Net->OnSelfWeight.RemoveAll(this);
			Net->OnSelfExp.RemoveAll(this);
			Net->OnSelfStatsChanged.RemoveAll(this);
			Net->OnSelfFaction.RemoveAll(this);
			Net->OnSelfJobChanged.RemoveAll(this);   // ★ 18.10.2026
		}
		if (UAsdaFishingSubsystem* Fish = GI->GetSubsystem<UAsdaFishingSubsystem>())
		{
			Fish->OnFishSkillChanged.RemoveAll(this);   // В6 рыбалки
		}
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->UnregisterUiZone(kCharInfoWinId);
			WM->UnregisterWindowCloser(kCharInfoWinId);
		}
		if (UAsdaSystemMenuSubsystem* Menu = GI->GetSubsystem<UAsdaSystemMenuSubsystem>())
		{
			Menu->OnMenuCommand.RemoveDynamic(this, &UAsdaCharaInfoSubsystem::OnMenuCommand);
			Menu->UnregisterWindow(kMenuButtonIdNum);
		}
		if (AAsdaPlayerController* Pc = BoundController.Get())
		{
			Pc->OnGameCommand.RemoveDynamic(this, &UAsdaCharaInfoSubsystem::OnGameCommand);
		}
	}
	HideOwnTooltip();
	ResetWidgets();
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaCharaInfoSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	HideOwnTooltip();
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-B; тот же дефект, что у чата — `wave2_A.md` п.0): окно 4000 (CHARAINFO_WIN) ЗАКРЫВАЕТСЯ В РЕЕСТРЕ.
	//   Было: сброс только забывал указатель, окно оставалось живым в реестре; при сбросе без смены мира (разрыв связи)
	//   следующая сборка получала от `OpenWindowByPath` ТОТ ЖЕ виджет и строила узлы/привязки второй раз поверх старого
	//   дерева (второй набор реле 17 кнопок: привязок 59 → 106 → 153 после двух сбросов, `asda.UI.TreeCensus`). Стало: окно снимается с реестра и вьюпорта (тихо), следующая сборка создаёт НОВЫЙ экземпляр — как
	//   после травела. Ключа отката нет: прежнее поведение — порча дерева.
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->CloseWindowSilently(kCharInfoWinId);
	}
	ResetWidgets();
	CurStatPage = 0;
	bTipShown = false;
	HoverKind = 0;
	bRevealedData = false; bRevealedElement = false; bRevealedEmotion = false;
	bEmoteDragActive = false;
	EmoteDragArm.Disarm();
	// Кэш «последних показанных» значений: у нового персонажа они другие, а без сброса
	// окно не перерисовало бы совпавшие числа.
	LastHpCur = -1; LastHpMax = -1; LastMpCur = -1; LastMpMax = -1;
	LastFactionId = -1; LastFactionPoints = 0; LastFactionGrade = 0;
	(void)Reason;
}

void UAsdaCharaInfoSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.CharaInfo");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }

	// Ленивое связывание с сетью: подсистема поднимается раньше соединения.
	if (!bNetBound)
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnVitalsChanged.AddUObject(this, &UAsdaCharaInfoSubsystem::OnVitalsChanged);
			Net->OnSelfWeight.AddUObject(this, &UAsdaCharaInfoSubsystem::OnSelfWeight);
			Net->OnSelfExp.AddUObject(this, &UAsdaCharaInfoSubsystem::OnSelfExpChanged);
			Net->OnSelfStatsChanged.AddUObject(this, &UAsdaCharaInfoSubsystem::OnSelfStatsChanged);
			Net->OnSelfFaction.AddUObject(this, &UAsdaCharaInfoSubsystem::OnSelfFaction);
			Net->OnSelfJobChanged.AddUObject(this, &UAsdaCharaInfoSubsystem::OnSelfJobChanged);   // ★ 18.10.2026
			bNetBound = true;
			UE_LOG(LogAsdaCharInfo, Log, TEXT("[характеристики] подписка на сеть выполнена"));
		}
	}
	// ★ В6 рыбалки: навык рыбалки (997) — сигнал автомата В1 (6164 / 6172 / сброс навыка).
	if (!bFishBound)
	{
		if (UAsdaFishingSubsystem* Fish = GI->GetSubsystem<UAsdaFishingSubsystem>())
		{
			Fish->OnFishSkillChanged.AddUObject(this, &UAsdaCharaInfoSubsystem::OnFishSkillChanged);
			bFishBound = true;
		}
	}

	// ★★★ ОБЕ привязки к панели быстрого доступа. Ровно здесь была дыра у журнала заданий: подсистема
	//   регистрировала кнопку ТОЛЬКО для подсветки (`RegisterWindow`) и не подписывалась на саму
	//   команду (`OnMenuCommand`) — кнопка светилась, но окно не открывала.
	if (!bMenuBound)
	{
		if (UAsdaSystemMenuSubsystem* Menu = GI->GetSubsystem<UAsdaSystemMenuSubsystem>())
		{
			Menu->OnMenuCommand.AddDynamic(this, &UAsdaCharaInfoSubsystem::OnMenuCommand);   // КОМАНДА
			Menu->RegisterWindow(kMenuButtonIdNum);                                          // ПОДСВЕТКА
			bMenuBound = true;
			UE_LOG(LogAsdaCharInfo, Log, TEXT("[характеристики] кнопка меню %d: команда + подсветка"),
				kMenuButtonIdNum);
		}
	}

	TickVitalBars(DeltaTime);   // ★ плавный ход полос жизни и маны в окне характеристик

	// Клавиши C (команда 30) и E (команда 31) — контроллер появляется позже подсистемы.
	if (UWorld* W = GI->GetWorld())
	{
		AAsdaPlayerController* Pc = Cast<AAsdaPlayerController>(W->GetFirstPlayerController());
		if (Pc && Pc != BoundController.Get())
		{
			if (AAsdaPlayerController* Old = BoundController.Get())
			{
				Old->OnGameCommand.RemoveDynamic(this, &UAsdaCharaInfoSubsystem::OnGameCommand);
			}
			Pc->OnGameCommand.AddDynamic(this, &UAsdaCharaInfoSubsystem::OnGameCommand);
			BoundController = Pc;
			UE_LOG(LogAsdaCharInfo, Log, TEXT("[характеристики] подписка на клавиши %d/%d выполнена"),
				kCmdInterfaceChar, kCmdInterfaceEmote);
		}
	}

	// ★ Мост в панель быстрого доступа: слот типа «эмоция» должен УМЕТЬ сработать.
	//   До этой правки `Misc.PlayEmotion` не назначал никто — эмоции в хотбаре молчали.
	if (!bHotbarBridged)
	{
		if (UAsdaHotbarSubsystem* Hb = GI->GetSubsystem<UAsdaHotbarSubsystem>())
		{
			// ★ READ-MODIFY-WRITE: поля этого же моста заполняет ещё и `UAsdaExtraMenuSubsystem`
			//   (пункты доп-меню, слот типа 5). Свежая структура затёрла бы его лямбды.
			FAsdaHotbarMiscBridge B = Hb->GetMiscBridge();
			B.PlayEmotion        = [this](int32 Code) { PlayEmotion(Code); };
			B.GetEmotionIconName = [this](int32 Code) { return GetEmotionIconName(Code); };
			B.ShowEmotionTooltip = [this](int32 Code) { ShowEmotionTooltip(Code); };
			Hb->SetMiscBridge(B);
			bHotbarBridged = true;
			UE_LOG(LogAsdaCharInfo, Log, TEXT("[эмоции] мост в панель быстрого доступа поднят"));
		}
	}

	if (IsWindowOpen()) { TickPointer(); }
	else if (bTipShown)  { HideOwnTooltip(); }
}

// ═══════════════════════════ ОКНО ═══════════════════════════════════════════════════════════════
bool UAsdaCharaInfoSubsystem::IsWindowOpen() const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	// ★ Именно `IsWindowVisible`, а не `IsOpen`: закрытие окна его НЕ разрегистрирует (грабля окна навыков).
	return Win != nullptr && WM != nullptr && WM->IsWindowVisible(kCharInfoWinId);
}

bool UAsdaCharaInfoSubsystem::EnsureWidget()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	if (WM->IsWindowLive(Win)) { return true; }
	ResetWidgets();
	// Корень SelfHitTestInvisible: растянутый на вьюпорт UUserWidget с Visible съедает клики по миру.
	Win = WM->OpenWindowByPath(kCharInfoWinId, kCharInfoWbp, WindowZ, ESlateVisibility::SelfHitTestInvisible);
	if (!Win)
	{
		UE_LOG(LogAsdaCharInfo, Error, TEXT("[характеристики] WBP_CHARAINFO_WIN не загрузился"));
		return false;
	}
	ScanWidgets();
	BindButtons();
	ApplyStaticTexts();
	AsdaUi::BindButtonCaptionStates(Win, Relays);     // состояния кнопок живут в подписи
	AsdaUi::DisableButtonFocusRings(Win);
	ApplyOutlinePolicy();
	// ★★★ ЗОНА КУРСОРА — ТОЛЬКО КОРПУС ОКНА, А НЕ КОРЕНЬ (дефект владельца 2026-08-23:
	//   «при открытом окне характеристик блокируется управление камерой»).
	//   ПРИЧИНА БЫЛА ЗДЕСЬ. Прежний вариант отдавал прямоугольник `Win->GetRootWidget()`, а корень
	//   окна — `CanvasPanel`, РАСТЯНУТЫЙ НА ВЕСЬ ВЬЮПОРТ (сама рамка лежит в нём отрицательными
	//   смещениями, см. `Reports/_charinfo_wbp_dump.txt`: `CHARAINFO_WIN_0_0` = (−162,−324)).
	//   Значит зона = ВЕСЬ ЭКРАН, а `AAsdaPlayerController::OnRightPressed` при курсоре над зоной
	//   не стартует орбит-камеру (и `OnWheel` глотает зум, и ЛКМ не доходит до мира). Камера
	//   «залипала» на всё время, пока окно открыто, в любой точке экрана.
	//   ЛЕЧЕНИЕ — тот же прямоугольник, что считают журнал заданий 700 и инвентарь: объединение
	//   девяти планов рамки 9-slice. Обход вынесен в ОБЩУЮ точку `AsdaUi::FramePlanesAbsRect`
	//   (закон `port-shared-points`), своей копии не заводим.
	WM->RegisterUiZone(kCharInfoWinId, FAsdaUiZoneRect::CreateWeakLambda(this,
		[this](FVector2D& Min, FVector2D& Max) -> bool
		{
			if (!IsWindowOpen() || !Win) { return false; }
			return AsdaUi::FramePlanesAbsRect(Win, TEXT("CHARAINFO_WIN_0_%d"), 9, Min, Max);
		}));
	// Роспись в общем «закрыть все окна игрока» (эталон закрывает 900 вместе с прочими).
	WM->RegisterWindowCloser(kCharInfoWinId, FSimpleDelegate::CreateUObject(
		this, &UAsdaCharaInfoSubsystem::CloseWindow));
	// ★ 08.10.2026 [ОРИГИНАЛ] (отчёт window_default_pos.md §6 п.1): окно 900 стоит в дефолте .JUI «ц30/ц48», Origin −48,−34
	//   (145,44 при 1024×768) — оригинал его не центрирует. Прежнее центрирование к тому же шло ПОСЛЕ `ApplySavedLayout` и
	//   затирало сохранённый сдвиг игрока. Откат: `asda.UI.JuiDefaultPos 0` — прежний центр.
	if (!UAsdaWindowSubsystem::UseOriginalDefaultPos()) { WM->CenterWindowOnScreen(Win); }
	return true;
}

void UAsdaCharaInfoSubsystem::ResetWidgets()
{
	Win = nullptr;
	Relays.Reset();
	CloseButton = nullptr; GearStatic = nullptr; GearAnimated = nullptr;
	MainTabButtons.Reset(); MainTabPlanes.Reset();
	StatBack = nullptr; MotionBack = nullptr; MotionBase = nullptr;
	StatTabButtons.Reset(); StatTab2Planes.Reset(); StatTab3Planes.Reset();
	StatTab2Text = nullptr; StatTab3Text = nullptr; StatPages.Reset();
	NameText = nullptr; TitleButton = nullptr; TitleFront = nullptr; TitleSecond = nullptr;
	LevelText = nullptr; JobText = nullptr; ConsText = nullptr; ConsRow = nullptr;
	RowLabels.Reset(); BarLabels.Reset();
	FactionLabel = nullptr; FactionValue = nullptr; HonorLabel = nullptr; HonorValue = nullptr;
	GradeLabel = nullptr; GradeValue = nullptr; GradeMark = nullptr;
	IntroButton = nullptr; BlogButton = nullptr; BlogCaption = nullptr; BlogRow = nullptr; CashRow = nullptr;
	CashLabel = nullptr; CashValue = nullptr; PreCashLabel = nullptr; PreCashValue = nullptr;
	MacCharButton = nullptr; CashCheckButton = nullptr;
	HpBar = FAsdaMiniBarFill(); MpBar = FAsdaMiniBarFill();
	ExpBar = FAsdaMiniBarFill(); WeightBar = FAsdaMiniBarFill();
	HpBuff = nullptr; MpBuff = nullptr;
	HpText = nullptr; MpText = nullptr; ExpText = nullptr; WeightText = nullptr;
	StatLabels.Reset(); StatValues.Reset(); StatHits.Reset(); StatFmt.Reset();
	FactionFmt = FAsdaFmtText(); TitleFrontFmt = FAsdaFmtText(); TitleSecondFmt = FAsdaFmtText();
	FishLabel = nullptr; FishValue = nullptr; FishRow = nullptr; FishFmt = FAsdaFmtText();
	EleHeadAtk = nullptr; EleHeadDef = nullptr;
	EleDomIcons.Reset(); EleAtkLabels.Reset(); EleAtkValues.Reset();
	EleDefLabels.Reset(); EleDefValues.Reset();
	for (FAsdaEmotionSlotUi& S : EmotionSlots) { S = FAsdaEmotionSlotUi(); }
	EmotionHeads.Reset(); EmotionCursors.Reset(); EmotionGroups.Reset();
	HoverRow = -1; HoverEmotion = -1; HoverKind = 0;
	bRevealedData = false; bRevealedElement = false; bRevealedEmotion = false;
	StatTabCaptions.Reset(); StatTabPageOf.Reset();
	// ★ Призрак — РЕБЁНОК корневого канваса окна: окно пересоздано ⇒ структуру сбрасываем целиком,
	//   иначе следующий захват тянул бы указатель на узел мёртвого дерева (грабля из шапки `FAsdaDragGhost`).
	EmoteDragArm.Disarm();
	EmoteDragGhost = FAsdaDragGhost();
	bEmoteDragActive = false;
}

// ═══════════════════════════ РАЗБОР ДЕРЕВА ВИДЖЕТОВ ═════════════════════════════════════════════
// ⚠ ИМЕНА ДУБЛИРУЮТСЯ. Импортёр выдал ДВЕ пары кнопок `CHARAINFO_STMENU_OPT0/OPT1` — по одной от
//   каждого из двух одноимённых контролов 981 (двухкнопочного видимого и трёхкнопочного отключённого).
//   `GetWidgetFromName` вернул бы только первую, поэтому дерево обходим сами и собираем ВСЕ.
void UAsdaCharaInfoSubsystem::ScanWidgets()
{
	if (!Win || !Win->WidgetTree) { return; }

	StatLabels.SetNum(14); StatValues.SetNum(14); StatHits.SetNum(14);
	RowLabels.SetNum(6); BarLabels.SetNum(4);
	EleDomIcons.SetNum(6); EleAtkLabels.SetNum(6); EleAtkValues.SetNum(6);
	EleDefLabels.SetNum(6); EleDefValues.SetNum(6);
	EmotionHeads.SetNum(3); EmotionCursors.SetNum(3); EmotionGroups.SetNum(3);

	// idc → планы (изображения), отсортированные по номеру плана
	TMap<FString, TArray<UImage*>> Planes;

	Win->WidgetTree->ForEachWidget([&](UWidget* W)
	{
		if (!W) { return; }
		const FString N = W->GetName();

		if (UImage* Im = Cast<UImage>(W))
		{
			FString Idc; int32 JuiIdx = -1, Plane = -1;
			if (AsdaUi::ParsePlaneName(N, Idc, JuiIdx, Plane))
			{
				Planes.FindOrAdd(Idc).Add(Im);
			}
			return;
		}
		if (UButton* B = Cast<UButton>(W))
		{
			if (N == TEXT("CHARAINFO_CLOSE"))      { CloseButton = B; return; }
			if (N == TEXT("STATUS_TITLE"))         { TitleButton = B; return; }
			if (N == TEXT("STATUS_MyIntroBtn"))    { IntroButton = B; return; }
			if (N == TEXT("CharInfo_Blog"))        { BlogButton = B; return; }
			if (N.StartsWith(TEXT("CHARAINFO_3BUT_OPT")))   { MainTabButtons.Add(B); return; }
			// ★★★ ТОЛЬКО `AddUnique` — И ЭТО НЕ ПЕРЕСТРАХОВКА (дефект владельца 2026-08-23:
			//   «кликается только третья под-вкладка»). ЖИВОЙ ЗАМЕР `asda.CharInfo.TabProbe`:
			//   в массив попадало ПЯТЬ записей — `OPT0, OPT1, OPT2, OPT0, OPT1`, причём последние
			//   две — ТЕ ЖЕ САМЫЕ объекты (имена совпадают побайтово, а имя UObject в одном
			//   `WidgetTree` уникально ⇒ это один и тот же виджет, обойдённый дважды: разметка
			//   несёт ДВА одноимённых контрола 981, и обход дерева видит общие узлы повторно).
			//   Последствие было ровно то, на что жалуется владелец:
			//     • `ApplyStaticTexts` раскладывал кнопки 0…2 по планам и делал их видимыми,
			//       а «лишние» 3 и 4 сворачивал — но это ОКАЗЫВАЛИСЬ ТЕ ЖЕ кнопки 0 и 1,
			//       поэтому первые две вкладки схлопывались (в живом дампе у них не было даже
			//       геометрии: `абс=(-1,-1)`), и на экране оставалась одна третья;
			//     • `BindButtons` вешал на кнопку 0 ДВА обработчика — «страница 0» и «страница 3»,
			//       то есть даже доживи она до клика, страница уехала бы не туда.
			//   ⚠ При `asda.CharInfo.EleTab 0` (эталонные две вкладки) дефект был ЕЩЁ ХУЖЕ:
			//   планов два, значит сворачивались кнопки 2, 3 и 4 = обе живые кнопки, и НИ ОДНА
			//   под-вкладка не кликалась вовсе.
			if (N.StartsWith(TEXT("CHARAINFO_STMENU_OPT"))) { StatTabButtons.AddUnique(B); return; }
			if (N == TEXT("STATUS_MAC_CHAR"))      { MacCharButton = B; return; }
			if (N == TEXT("Status_CashCheck_Btn")) { CashCheckButton = B; return; }
			return;
		}
		if (UTextBlock* T = Cast<UTextBlock>(W))
		{
			T->SetAutoWrapText(false);   // закон ui-autowrap-canvas-gotcha: в Canvas перенос ломает раскладку
			if (N == TEXT("CharInfo_Blog_Text")) { BlogCaption = T; return; }   // подпись 988 (loc 11752)
			if (N == TEXT("STATUS_NAME"))        { NameText = T; return; }
			if (N == TEXT("Status_FrontTitle"))  { TitleFront = T; return; }
			if (N == TEXT("Status_2ndTitle"))    { TitleSecond = T; return; }
			if (N == TEXT("STATUS_LEVEL"))       { LevelText = T; return; }
			if (N == TEXT("STATUS_JOB"))         { JobText = T; return; }
			if (N == TEXT("STATUS_cons"))        { ConsText = T; return; }
			if (N == TEXT("STATUS_THP"))         { HpText = T; return; }
			if (N == TEXT("STATUS_TMP"))         { MpText = T; return; }
			if (N == TEXT("STATUS_TEXP"))        { ExpText = T; return; }
			if (N == TEXT("STATUS_TWEIGHT"))     { WeightText = T; return; }
			if (N == TEXT("CHARAINFO_CAMPWAR_T1"))    { FactionLabel = T; return; }
			if (N == TEXT("CHARAINFO_CAMPWART1_S"))   { FactionValue = T; return; }
			if (N == TEXT("CHARAINFO_CAMPWAR_T2"))    { HonorLabel = T; return; }
			if (N == TEXT("CHARAINFO_CAMPWAR_T2_S"))  { HonorValue = T; return; }
			if (N == TEXT("CHARAINFO_CAMPWAR_T3"))    { GradeLabel = T; return; }
			if (N == TEXT("CHARAINFO_CAMPWAR_T3_S"))  { GradeValue = T; return; }
			if (N == TEXT("STATUS_SB0"))         { CashLabel = T; return; }
			if (N == TEXT("STATUS_ST0"))         { CashValue = T; return; }
			if (N == TEXT("STATUS_SB8"))         { EleHeadAtk = T; return; }
			if (N == TEXT("CharaInfo_StEle3") || N == TEXT("CharaInfo_StEle3_Text")) { EleHeadDef = T; return; }
			if (N == TEXT("STATUS_fishing1"))    { FishLabel = T; return; }
			if (N == TEXT("STATUS_fishing2"))    { FishValue = T; return; }
			if (N == TEXT("CHARAINFO_STMENU_Text"))   { StatTab3Text = T; return; }
			if (N == TEXT("CHARAINFO_STMENU_Text_1")) { StatTab2Text = T; return; }
			if (N == TEXT("STATUS_B04"))         { /* подпись зодиака */ return; }
			// Подписи строк 12300…12305 → RowLabels[0..5]
			if (N.StartsWith(TEXT("STATUS_B0")) && N.Len() == 10)
			{
				const int32 D = (int32)(N[9] - TCHAR('0'));
				if (D >= 0 && D <= 3) { RowLabels[D] = T; return; }        // B00…B03
				if (D == 5) { BarLabels[0] = T; return; }                  // B05 = ОЗ
				if (D == 6) { BarLabels[1] = T; return; }                  // B06 = ОМ
				if (D == 8) { BarLabels[2] = T; return; }                  // B08 = опыт
				if (D == 9) { BarLabels[3] = T; return; }                  // B09 = вес
				return;
			}
			// Стихии: подписи 12327…12332 (атака) и 12334…12339 (защита); значения 960…965 / 966…971.
			//   Имена в разметке нерегулярны, поэтому список задан явно (по IDNum из дампа JUI).
			static const TCHAR* kEleAtkLabel[6] = { TEXT("STATUS_ST9_01"), TEXT("STATUS_AT11"), TEXT("STATUS_AT13"),
			                                        TEXT("STATUS_AT15"),   TEXT("STATUS_AT17"), TEXT("STATUS_AT19_01") };
			static const TCHAR* kEleAtkValue[6] = { TEXT("STATUS_AT10"), TEXT("STATUS_AT12"), TEXT("STATUS_AT14"),
			                                        TEXT("STATUS_AT16"), TEXT("STATUS_AT18"), TEXT("STATUS_AT19") };
			static const TCHAR* kEleDefLabel[6] = { TEXT("STATUS_AT21"), TEXT("STATUS_AT23"), TEXT("STATUS_AT25"),
			                                        TEXT("STATUS_AT27"), TEXT("STATUS_AT29"), TEXT("STATUS_AT31") };
			static const TCHAR* kEleDefValue[6] = { TEXT("STATUS_AT22"), TEXT("STATUS_AT24"), TEXT("STATUS_AT26"),
			                                        TEXT("STATUS_AT28"), TEXT("STATUS_AT30"), TEXT("STATUS_AT32") };
			for (int32 i = 0; i < 6; ++i)
			{
				if (N == kEleAtkLabel[i]) { EleAtkLabels[i] = T; return; }
				if (N == kEleAtkValue[i]) { EleAtkValues[i] = T; return; }
				if (N == kEleDefLabel[i]) { EleDefLabels[i] = T; return; }
				if (N == kEleDefValue[i]) { EleDefValues[i] = T; return; }
			}
			// Значения статов STATUS_ST1…ST14 → StatValues[0..13]
			if (N.StartsWith(TEXT("STATUS_ST")) && !N.StartsWith(TEXT("STATUS_STTT")))
			{
				const FString Tail = N.Mid(9);
				if (Tail.IsNumeric())
				{
					const int32 Num = FCString::Atoi(*Tail);
					if (Num >= 1 && Num <= 14) { StatValues[Num - 1] = T; }
				}
				return;
			}
			// Подписи статов STATUS_STTT01…STTT14 → StatLabels[0..13]
			if (N.StartsWith(TEXT("STATUS_STTT")))
			{
				const FString Tail = N.Mid(11);
				if (Tail.IsNumeric())
				{
					const int32 Num = FCString::Atoi(*Tail);
					if (Num >= 1 && Num <= 14) { StatLabels[Num - 1] = T; }
				}
				return;
			}
			// Заголовки лотков эмоций
			if (N == TEXT("IMOTION_B00")) { EmotionHeads[0] = T; return; }
			if (N == TEXT("IMOTION_B01")) { EmotionHeads[1] = T; return; }
			if (N == TEXT("IMOTION_B02")) { EmotionHeads[2] = T; return; }
			// Подписи слотов эмоций (эталон текст в них не пишет — гасим)
			for (int32 g = 0; g < 3; ++g)
			{
				static const TCHAR* kPfx[3] = { TEXT("IMOTION_NOMAL"), TEXT("IMOTION_PARTY"), TEXT("IMOTION_ABILITY") };
				if (N.StartsWith(kPfx[g]))
				{
					const FString Tail = N.Mid(FCString::Strlen(kPfx[g]));
					if (Tail.IsNumeric())
					{
						const int32 Idx = FCString::Atoi(*Tail);
						const int32 Base = (g == 0) ? 0 : (g == 1 ? 12 : 18);
						if (EmotionSlots.IsValidIndex(Base + Idx)) { EmotionSlots[Base + Idx].Label = T; }
					}
					return;
				}
			}
			return;
		}
		// Группы-канвасы
		if (N == TEXT("CHARAINFO_STBACK_grp"))        { StatBack = W; return; }
		if (N == TEXT("CHARAINFO_IMOTIONBACK_grp"))   { MotionBack = W; return; }
		if (N == TEXT("CHARAINFO_IMOTIONBase_grp"))   { MotionBase = W; return; }
		if (N == TEXT("CHARAINFO_STBASIC_grp"))       { StatPages.SetNum(3); StatPages[0] = W; return; }
		if (N == TEXT("CHARAINFO_STDATA_grp"))        { StatPages.SetNum(3); StatPages[1] = W; return; }
		if (N == TEXT("CHARAINFO_STELE_grp"))         { StatPages.SetNum(3); StatPages[2] = W; return; }
		if (N == TEXT("STATUS_B04_grp"))              { ConsRow = W; return; }
		if (N == TEXT("CharInfo_Blog_Base_grp"))      { BlogRow = W; return; }
		if (N == TEXT("STATUS_Line01_grp"))           { CashRow = W; return; }
		if (N == TEXT("CharaInfo_StData4_grp"))       { FishRow = W; return; }
		if (N == TEXT("Status_PreCashTxt_grp"))       { PreCashLabel = W; return; }
		if (N == TEXT("Status_PreCash_grp"))          { PreCashValue = W; return; }
		if (N == TEXT("IMOTION_B00_s_grp"))           { EmotionGroups[0] = W; return; }
		if (N == TEXT("IMOTION_B01_s_grp"))           { EmotionGroups[1] = W; return; }
		if (N == TEXT("IMOTION_B02_s_grp"))           { EmotionGroups[2] = W; return; }
	});

	// ── планы по idc ────────────────────────────────────────────────────────────────────────────
	auto FirstPlane = [&Planes](const TCHAR* Idc) -> UImage*
	{
		if (TArray<UImage*>* A = Planes.Find(FString(Idc))) { return A->Num() ? (*A)[0] : nullptr; }
		return nullptr;
	};
	auto MakeBar = [&Planes](const TCHAR* Idc, UTextBlock* /*Txt*/) -> FAsdaMiniBarFill
	{
		FAsdaMiniBarFill B;
		TArray<UImage*>* A = Planes.Find(FString(Idc));
		if (!A || A->Num() == 0) { return B; }
		// Полная ширина ВСЕГДА у подложки ES0, если она есть (правило мини-статуса): у ProgressBar-скинов
		//   с двумя планами импортёр масштабирует ES1 по дизайн-проценту, а не на всю длину.
		B.Back = (A->Num() >= 2) ? (*A)[0] : nullptr;
		B.Fill = (A->Num() >= 2) ? (*A)[1] : (*A)[0];
		FVector2D P, S;
		const UWidget* Ref = B.Back ? static_cast<const UWidget*>(B.Back.Get()) : static_cast<const UWidget*>(B.Fill.Get());
		if (AsdaUi::SlotRect(Ref, P, S)) { B.FullW = S.X; }
		FVector2D FP, FS;
		if (AsdaUi::SlotRect(B.Fill.Get(), FP, FS)) { B.BaseLeft = FP.X; }
		return B;
	};

	GearStatic   = FirstPlane(TEXT("CharaInfo_Gear"));
	GearAnimated = FirstPlane(TEXT("CharaInfo_AniGear"));
	HpBar     = MakeBar(TEXT("STATUS_PHP"), HpText);
	MpBar     = MakeBar(TEXT("STATUS_PMP"), MpText);
	ExpBar    = MakeBar(TEXT("STATUS_PEXP"), ExpText);
	WeightBar = MakeBar(TEXT("STATUS_PWEIGHT"), WeightText);
	HpBuff    = FirstPlane(TEXT("STATUS_HPBuff"));
	MpBuff    = FirstPlane(TEXT("STATUS_MPBuff"));
	GradeMark = FirstPlane(TEXT("CHARAINFO_CAMPWAR_Mark"));

	for (int32 i = 0; i < 6; ++i)
	{
		EleDomIcons[i] = FirstPlane(*(FString(TEXT("CHARAINFO_STELE_ICON")) + FString::Printf(TEXT("%02d"), i)));
	}
	// Девять планов рамки окна и девять планов подложки страницы эмоций — для подгонки высоты.
	WinFramePlanes.Reset(); MotionBasePlanes.Reset();
	// ⚠ Порядок ОБЯЗАН быть 0…8 (его требует `UAsdaTipPanel::Resize9Slice`): сортируем по номеру
	//   плана из имени, а не полагаемся на порядок обхода дерева.
	auto SortPlanes = [](TArray<TObjectPtr<UImage>>& Arr)
	{
		Arr.Sort([](const UImage& A, const UImage& B)
		{
			FString Ia, Ib; int32 Ja = 0, Jb = 0, Pa = 0, Pb = 0;
			AsdaUi::ParsePlaneName(A.GetName(), Ia, Ja, Pa);
			AsdaUi::ParsePlaneName(B.GetName(), Ib, Jb, Pb);
			return Pa < Pb;
		});
	};
	if (TArray<UImage*>* A = Planes.Find(FString(TEXT("CHARAINFO_WIN"))))         { for (UImage* Im : *A) { WinFramePlanes.Add(Im); } }
	if (TArray<UImage*>* A = Planes.Find(FString(TEXT("CHARAINFO_IMOTIONBase")))) { for (UImage* Im : *A) { MotionBasePlanes.Add(Im); } }
	SortPlanes(WinFramePlanes); SortPlanes(MotionBasePlanes);

	// Планы двух вариантов переключателя под-вкладок (2 кнопки — `_162_*`, 3 кнопки — `_161_*`).
	if (TArray<UImage*>* A = Planes.Find(FString(TEXT("CHARAINFO_STMENU"))))
	{
		for (UImage* Im : *A)
		{
			FVector2D P, S;
			if (!AsdaUi::SlotRect(Im, P, S)) { continue; }
			if (FMath::IsNearlyEqual(S.X, 72.f, 2.f)) { StatTab3Planes.Add(Im); }
			else                                      { StatTab2Planes.Add(Im); }
		}
	}
	// Иконки и подложки слотов эмоций
	for (int32 g = 0; g < 3; ++g)
	{
		static const TCHAR* kIconPfx[3] = { TEXT("IMOTION_NOMAL"), TEXT("IMOTION_PARTY"), TEXT("IMOTION_ABILITY") };
		static const TCHAR* kBasePfx[3] = { TEXT("IMOTION_TRAY00_"), TEXT("IMOTION_TRAY01_"), TEXT("IMOTION_TRAY02_") };
		static const int32  kCount[3]   = { 12, 6, 12 };
		const int32 Base = (g == 0) ? 0 : (g == 1 ? 12 : 18);
		for (int32 i = 0; i < kCount[g]; ++i)
		{
			FAsdaEmotionSlotUi& S = EmotionSlots[Base + i];
			S.Group = g; S.IndexInGroup = i; S.Code = MakeEmotionCode(g, i);
			const FString Two = FString::Printf(TEXT("%02d"), i);
			S.Icon = FirstPlane(*(FString(kIconPfx[g]) + Two));
			S.Base = FirstPlane(*(FString(kBasePfx[g]) + Two));
		}
	}
	EmotionCursors[0] = FirstPlane(TEXT("IMOTION_NOMALCURSOR"));
	EmotionCursors[1] = FirstPlane(TEXT("IMOTION_PARTYCURSOR"));
	EmotionCursors[2] = FirstPlane(TEXT("IMOTION_ABILITYCURSOR"));

	// ★★★ СТИЛЬ СТРОК С ТЕГАМИ СНИМАЕМ ЗДЕСЬ — пока это ещё `UTextBlock` из JUI. После первого
	//   `UAsdaRichText::ApplyFormatted` строка станет `URichTextBlock`, и кегль/цвет/выравнивание,
	//   положенные импортёром (кегль 8, цвет из блока `EXS TextColor`), уже не прочитать.
	//   Тегами в окне 900 пахнут ровно три места (реверс — `char_info_fix2.md` §1.1):
	//   936…941 базовые статы, 922 фракция, 919/920 звание. Кэш узлов заводим на ВСЕ 14 строк
	//   значений, потому что через него же идут видимость и хит-тест подсказки.
	StatFmt.SetNum(14);
	for (int32 i = 0; i < 14; ++i) { CaptureFmt(StatFmt[i], StatValues.IsValidIndex(i) ? StatValues[i].Get() : nullptr); }
	CaptureFmt(FactionFmt,     FactionValue.Get());
	CaptureFmt(TitleFrontFmt,  TitleFront.Get());
	CaptureFmt(TitleSecondFmt, TitleSecond.Get());
	CaptureFmt(FishFmt,        FishValue.Get());   // 997 — строка с тегом `<FC=FFFFFF00>` (В6 рыбалки)

	UE_LOG(LogAsdaCharInfo, Log,
		TEXT("[характеристики] дерево разобрано: страниц=%d, значений статов=%d, слотов эмоций=%d, ")
		TEXT("кнопок главных вкладок=%d, кнопок под-вкладок=%d"),
		StatPages.Num(), StatValues.Num(), EmotionSlots.Num(), MainTabButtons.Num(), StatTabButtons.Num());
}

void UAsdaCharaInfoSubsystem::BindButtons()
{
	auto Bind = [this](UButton* B, TFunction<void(int32)> OnClick, int32 Index,
	                   TFunction<void()> Hover = nullptr, TFunction<void()> Unhover = nullptr)
	{
		if (!B) { return; }
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Index = Index;
		R->Callback = MoveTemp(OnClick);
		B->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		if (Hover)
		{
			R->HoverCallback = MoveTemp(Hover);
			B->OnHovered.AddDynamic(R, &UAsdaButtonRelay::OnHovered);
		}
		if (Unhover)
		{
			R->UnhoverCallback = MoveTemp(Unhover);
			B->OnUnhovered.AddDynamic(R, &UAsdaButtonRelay::OnUnhovered);
		}
		AsdaUi::AnchorRelay(Relays, R, B);
	};

	// Крестик 980 + переключение шестерён при наведении (эталон msg 8 / 0xA по 980).
	Bind(CloseButton, [this](int32) { CloseWindow(); }, 0,
		[this]()
		{
			if (AsdaCloseGear::IsCommonOn()) { return; }   // ★ 08.11.2026: ведёт общая точка UAsdaCloseGearSubsystem
			if (GearStatic)   { GearStatic->SetVisibility(ESlateVisibility::Collapsed); }
			if (GearAnimated) { GearAnimated->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
		},
		[this]()
		{
			if (AsdaCloseGear::IsCommonOn()) { return; }
			if (GearStatic)   { GearStatic->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
			if (GearAnimated) { GearAnimated->SetVisibility(ESlateVisibility::Collapsed); }
		});

	// Главные вкладки 902 (эталон: msg 66817 с a2 != 981 → uiWindowSwitchTabMode).
	for (int32 i = 0; i < MainTabButtons.Num(); ++i)
	{
		Bind(MainTabButtons[i].Get(), [this](int32 Idx) { SwitchTabMode(Idx); }, i);
	}
	// Под-вкладки 981 (эталон: msg 66817 с a2 == 981 → uiTabSelectVisible).
	//   ⚠ Кнопок в дереве ЧЕТЫРЕ (две пары одноимённых, см. ScanWidgets) — индекс берём по геометрии.
	// Под-вкладки 981: номер страницы = ПОЗИЦИЯ кнопки в раскладке (см. ApplyStaticTexts).
	for (int32 i = 0; i < StatTabButtons.Num(); ++i)
	{
		Bind(StatTabButtons[i].Get(), [this](int32 Idx) { SelectStatPage(Idx); }, i);
	}

	// Кнопка звания 905: клик открывает окно званий, наведение — подсказка loc 2432.
	Bind(TitleButton, [this](int32)
		{
			HideOwnTooltip();
			// ★ 2026-08-29 (порт МЕНЮ ЗВАНИЙ, PORT_PENDING п.87 ЗАКРЫТ): эталонный
			//   `questLogWnd_Open(&g_pCharTitleMgr)` (`ui_core.c:74403`, case 0x389) открывает
			//   окно `Title_Win` 18700 — теперь оно есть.
			UAsdaTitleSubsystem* Titles = GetGameInstance()
				? GetGameInstance()->GetSubsystem<UAsdaTitleSubsystem>() : nullptr;
			if (Titles) { Titles->OpenTitleWindow(); }
			else        { Notice(CharInfoLoc::kTitleTip); }
		}, 0,
		[this]() { ShowTitleTooltip(); },
		[this]() { HideOwnTooltip(); });

	// Кнопка «Обо мне» 987 → эталонный `findSoulmateWinOpen@0xE72C20`: открывает ТОЛЬКО малое окно
	//   8300 `My_Introduction_Win` (не весь поиск соулмейта), предзаполняя поле сохранённым текстом.
	Bind(IntroButton, [this](int32) { OpenIntroWindow(); }, 0);
	// ★ 10.10.2026 (`Reports/owner_1010/overlaps_fixes.md`, живой скан EN): подпись кнопки 987 эталон ставит кодом —
	//   `CWidget_SetTextAll4(987, loc 11698)` (ui_core.c:10739-10743); у порта она не ставилась, и в EN оставался образец
	//   ассета «Написать о себе».
	if (Win && Win->WidgetTree)
	{
		if (UTextBlock* IntroCap = Cast<UTextBlock>(Win->WidgetTree->FindWidget(FName(TEXT("STATUS_MyIntroBtn_Text")))))
		{
			SetText(IntroCap, Loc(CharInfoLoc::kIntroBtn));
		}
	}

	// ★ Кнопка 988 «Мой блог». Эталон ведёт её в окно ВСТРОЕННОГО БРАУЗЕРА 19000
	//   (`CashCharge_InitWindow@0xB7D920`); URL строится по шаблону из бинаря с меткой
	//   `MBLOG_ASDA` и токеном, который приходит пакетом 6083 (`itemMallLoginBegin@0xF49A60`).
	//   Браузер мы НЕ портируем (решение по памятке `character-blog-system`), а сервис издателя
	//   `blog.mgame.com` мёртв — поэтому вместо тишины даём ЧЕСТНЫЙ отказ: нотис + запись в
	//   журнал с причиной. Подключение — одна строка, когда появится своя веб-точка.
	// Подпись 988 берём ИЗ ТАБЛИЦЫ (loc 11752), а не из `.JUI`: там она корейская.
	if (BlogCaption) { SetText(BlogCaption.Get(), Loc(CharInfoLoc::kBlogBtn)); }
	Bind(BlogButton, [this](int32)
		{
			HideOwnTooltip();
			UE_LOG(LogAsdaCharInfo, Warning,
				TEXT("[характеристики] «Мой блог» (988): окно встроенного браузера 19000 не портировано, ")
				TEXT("а сервис издателя blog.mgame.com мёртв — открывать нечего (см. character-blog-system)"));
			// Нотис 990100 «Эта часть игры ещё не перенесена: [ss1].» — тот же канал честного
			//   отказа, что у доп-меню; подставляем подпись кнопки (loc 11752 «Мой блог»).
			if (UAsdaMessageSubsystem* M = GetGameInstance()
					? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
			{
				M->PostLocFmt(EAsdaMsgCategory::Tip, 990100, { Loc(CharInfoLoc::kBlogBtn) }, {});
			}
		}, 0);
}
