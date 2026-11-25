// Copyright (c) 2026 @sitxovski (Discord)
// AsdaTargetFrameSubsystem.cpp — панель цели `PVP_TARGET` (idnum 40).
//   Модель, эталонные адреса, четыре исправленных дефекта оригинала и оба вскрытых миснейма —
//   в шапке `AsdaTargetFrameSubsystem.h`. Отчёт: `Reports/target_frame_port.md`.

#include "UI/AsdaTargetFrameSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaAnimatedPlane.h"
#include "AsdaNetSubsystem.h"
#include "Npc/AsdaMobActor.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "World/AsdaClanFlag.h"
#include "World/AsdaNameplateComponent.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaWorldSubsystem.h"
#include "Engine/Engine.h"        // GEngine->DeferredCommands — «quit» после самотеста
#include "TimerManager.h"         // отложенный снимок геометрии (до отрисовки она нулевая)

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"          // TActorIterator — консольный подбор ближайшего моба
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "AsdaSelf.h"                      // ★ Дедуп №2, В1-B (U5a): «это я?» — `AsdaSelf::IsSelfUserIdx`
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): разбор аргументов самотеста (`quit` не путается с числом)
#include "World/AsdaCombatTrace.h"  // ★ 18.10.2026: журнал боя — показ/скрытие панели цели и каждое изменение отображаемого HP цели
#include "UI/AsdaTutorialEvents.h"   // ★ 10.11.2026 обучение: флаги шагов гида 1950 (TUTORIAL.md §2.3)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaTarget, Log, All);

namespace
{
	// Окно и его WBP. Имя окна = имя окна в .JUI — по нему `UAsdaWindowSubsystem` находит запись
	//   в `DA_JuiWindowMeta` (ручки перетаскивания и т. п.).
	static const TCHAR* kTargetWindowId = TEXT("PVP_TARGET");
	static const TCHAR* kTargetWbp =
		TEXT("/Game/AsdaAssets/UI/Player_UI/HUD/WBP_PVP_TARGET.WBP_PVP_TARGET_C");

	// ── КЛЮЧИ ОТКАТА (каждое отступление от эталона имеет свой; см. шапку .h) ───────────────────
	static ASDA_DEV_CVAR(int32, CVarLegacyEleBase,
		TEXT("asda.Target.LegacyEleBase"), 0,
		TEXT("Панель цели, Д1: 1 = как в оригинале — подложку стихии PVP_ELE_Base не показывать ")
		TEXT("(в .JUI у неё Visible=0, метки — её дети, значит стихийная метка не видна никогда). ")
		TEXT("0 (по умолчанию) = показывать подложку вместе с меткой."),
		ECVF_Default);

	static ASDA_DEV_CVAR(int32, CVarLegacyBeingAttack,
		TEXT("asda.Target.LegacyBeingAttack"), 0,
		TEXT("Панель цели, Д2/Д3: 1 = как в оригинале — рамку «цель в бою» у мобов не зажигать ")
		TEXT("(гейт ent+474 не имеет писателя во всём бинаре) и мигать только при «моб бьёт первым». ")
		TEXT("0 (по умолчанию) = зажигать по боевому состоянию цели и мигать, пока рамка видна."),
		ECVF_Default);

	static ASDA_DEV_CVAR(int32, CVarLegacyLevelColor,
		TEXT("asda.Target.LegacyLevelColor"), 0,
		TEXT("Панель цели, Д4: 1 = как в оригинале — цвет по разнице уровней ставить только тексту ")
		TEXT("имени и скрытому PVP_LVTXT (спрайты цифр остаются белыми). 0 (по умолчанию) = красить ")
		TEXT("и спрайты уровня тоже."),
		ECVF_Default);

	static ASDA_DEV_CVAR(int32, CVarLegacyElementMark,
		TEXT("asda.Target.LegacyElementMark"), 0,
		TEXT("Панель цели: 1 = как в оригинале — у цели-ИГРОКА метка стихии показывается всегда ")
		TEXT("(эталонный цикл выбора максимума стартует с idx=0 и условие `idx >= 0` истинно даже ")
		TEXT("при всех нулях ⇒ вечный «Огонь»). 0 (по умолчанию) = при неизвестных/нулевых стихиях ")
		TEXT("метка гасится."),
		ECVF_Default);

	static ASDA_DEV_CVAR(int32, CVarCenterByPanel,
		TEXT("asda.Target.CenterByPanel"), 1,
		TEXT("Панель цели: 1 (по умолчанию) = центрировать по ширине базовой панели PVP_TARGET00 ")
		TEXT("(эталонная арифметика halfScreenWidth − halfTargetWidth; читателя у этих полей в ")
		TEXT("оригинале НЕТ). 0 = оставить анкер из .JUI (доля 0.4 ширины экрана)."),
		ECVF_Default);

	static ASDA_DEV_CVAR(int32, CVarShowTargetMp,
		TEXT("asda.Target.ShowMp"), 0,
		TEXT("Панель цели: 1 = заполнять полосу MP цели-игрока из витальных значений и убирать ")
		TEXT("перекрывающий слой PVP_MPBAR_HIDE. 0 (по умолчанию) = как в оригинале: обновления ")
		TEXT("PVP_MPBAR в эталоне НЕТ ни одного, слой HIDE закрывает полосу всегда."),
		ECVF_Default);

	// Показать/скрыть узел (общая мелочь; SelfHitTestInvisible — панель кликов не ловит).
	// ★ Волна 1 (14.09.2026, В3): правило видимости — общая точка `AsdaUi::SetShownDecor` (режим тот же).
	FORCEINLINE void SetVis(UWidget* W, bool bOn)
	{
		AsdaUi::SetShownDecor(W, bOn);
	}

	// Строка «имя [класс]» для дампа.
	FORCEINLINE FString NodeTag(const UWidget* W)
	{
		return W ? FString::Printf(TEXT("%s"), *W->GetName()) : FString(TEXT("НЕТ"));
	}
}

// ══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════

void UAsdaTargetFrameSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// ★ 06.10.2026 (ревизия): гашение эмблемы цели по событиям клана — см. объявление OnClanResetNet.
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	if (UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>())
	{
		Net->OnClanReset.AddUObject(this, &UAsdaTargetFrameSubsystem::OnClanResetNet);
		Net->OnClanBaseInfoPush.AddUObject(this, &UAsdaTargetFrameSubsystem::OnClanBaseInfoPushNet);
	}
	UE_LOG(LogAsdaTarget, Log, TEXT("[панель цели] подсистема поднята (окно PVP_TARGET 40)"));
}

void UAsdaTargetFrameSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnClanReset.RemoveAll(this);
			Net->OnClanBaseInfoPush.RemoveAll(this);
		}
	}
	ResetWidgets();
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaTargetFrameSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Цель — актор умершего мира, а `TargetReqUserNum` — НОМЕР СЕССИИ: держать их нельзя
	// ни через разрыв, ни через смену карты.
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-B; тот же дефект, что у чата — `wave2_A.md` п.0): окно 40 (PVP_TARGET) ЗАКРЫВАЕТСЯ В РЕЕСТРЕ.
	//   Было: сброс только забывал указатель, окно оставалось живым в реестре; при сбросе без смены мира (разрыв связи)
	//   следующая сборка получала от `OpenWindowByPath` ТОТ ЖЕ виджет и строила узлы/привязки второй раз поверх старого
	//   дерева (`BindControls` создаёт цифры уровня `NewObject<UImage>(окно, «PVP_LvDigit_*»)` с ФИКСИРОВАННЫМИ именами — второй вызов в том же окне пересоздаёт объект поверх живого узла канвы). Стало: окно снимается с реестра и вьюпорта (тихо), следующая сборка создаёт НОВЫЙ экземпляр — как
	//   после травела. Ключа отката нет: прежнее поведение — порча дерева.
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->CloseWindowSilently(FName(kTargetWindowId));
	}
	ResetWidgets();
	bTargetIsMob = false;
	bFrameShown = false;
	LastHpCur = -1; LastHpMax = -1; LastLevel = -1;
	TargetReqCharId = -1; TargetReqUserNum = -1;
	SmoothHpMax = 0; SmoothSegments = 0;
	bCentered = false;
	(void)Reason;
}

void UAsdaTargetFrameSubsystem::ResetWidgets()
{
	Win = nullptr;
	BaseGroup = nullptr; BaseImage = nullptr;
	RingNormal = RingNamed = RingElite = RingBoss = nullptr;
	RingNormalAni = RingNamedAni = RingEliteAni = RingBossAni = nullptr;
	LvBaseGroup = nullptr; LvText = nullptr;
	DigitProto.Reset(); DigitSlot.Reset();
	for (int32 i = 0; i < 10; ++i) { bDigitBrushValid[i] = false; }
	NameText = nullptr;
	HpBar.Reset();
	HpBar.SetNum(4);   // эталонный массив gap_818[4] всегда четырёхэлементный
	MpBar = FAsdaMiniBarFill();
	MpBarHide = nullptr; HpBuff = nullptr; MpBuff = nullptr;
	Portrait = nullptr; BeingAttack = nullptr; BatFirstMark = nullptr;
	EleBaseGroup = nullptr; EleMark.Reset();
	ClanGroup = nullptr; ClanMark = nullptr;
	EnemyStateGroup = nullptr; EnemySlots.Reset();
	bControlsReady = false;
	bFrameShown = false;
	bBeingAttackOn = false;
	CurTarget = nullptr; CurUserIdx = -1; CurRenderId = -1; CurSegments = 0; bTargetIsMob = false;
	LastHpCur = LastHpMax = LastLevel = -1;
	LastName.Reset();
}

bool UAsdaTargetFrameSubsystem::EnsureWidgets()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }

	UUserWidget* W = WM->FindWindow(FName(kTargetWindowId));
	if (W && !WM->IsWindowLive(W)) { W = nullptr; ResetWidgets(); }   // окно пережило травел — переоткрыть
	if (!W)
	{
		// В .JUI у окна 40 `Visible = "0"`: оно существует всегда и лишь показывается/прячется
		//   (эталон дёргает SetVisible, а не создаёт окно на каждый выбор цели).
		W = WM->OpenWindowByPath(FName(kTargetWindowId), kTargetWbp, kTargetFrameZ,
			ESlateVisibility::SelfHitTestInvisible);
		if (!W) { return false; }
		WM->SetWindowVisible(FName(kTargetWindowId), false);
		bFrameShown = false;
	}
	if (bControlsReady && Win == W) { return true; }

	ResetWidgets();
	Win = W;
	BindControls();
	return bControlsReady;
}

// Узел-план по имени контрола JUI: `<Idc>_<номер>_<план>`. Разбор ТОЧНЫЙ (не «contains»): иначе
//   `PVP_HPBAR` подхватил бы `PVP_HPBAR_Y_30_0`, а `PVP_MARK0` — `PVP_MARK05`.
UWidget* UAsdaTargetFrameSubsystem::FindPlane(const TCHAR* Idc, int32 PlaneIdx) const
{
	if (!Win || !Win->WidgetTree) { return nullptr; }
	const FString Prefix = FString(Idc) + TEXT("_");
	const FString Suffix = FString::Printf(TEXT("_%d"), PlaneIdx);
	UWidget* Found = nullptr;
	Win->WidgetTree->ForEachWidget([&](UWidget* Node)
	{
		if (Found || !Node) { return; }
		const FString N = Node->GetName();
		if (!N.StartsWith(Prefix, ESearchCase::CaseSensitive)) { return; }
		if (!N.EndsWith(Suffix, ESearchCase::CaseSensitive)) { return; }
		// Между префиксом и суффиксом обязан остаться ТОЛЬКО номер узла импортёра (цифры).
		const FString Mid = N.Mid(Prefix.Len(), N.Len() - Prefix.Len() - Suffix.Len());
		if (Mid.IsEmpty()) { return; }
		for (TCHAR C : Mid) { if (!FChar::IsDigit(C)) { return; } }
		Found = Node;
	});
	return Found;
}

UImage* UAsdaTargetFrameSubsystem::FirstImageIn(UWidget* Panel)
{
	UPanelWidget* P = Cast<UPanelWidget>(Panel);
	if (!P) { return nullptr; }
	for (int32 i = 0; i < P->GetChildrenCount(); ++i)
	{
		if (UImage* Img = Cast<UImage>(P->GetChildAt(i))) { return Img; }
	}
	return nullptr;
}

// Полная ширина заливки снимается ОДИН раз (грабля EXP-бара и полосы каста: после первого клипа
//   в 0 её уже не восстановить). Ширина берётся с ПОДЛОЖКИ, если она есть: у скинов с
//   ProgressType импортёр переносит текстурную Rect-пропорцию в слот заливки, и заливка на 100 %
//   оказывается уже подложки (разбор — в `AsdaCastBarSubsystem.cpp`).
void UAsdaTargetFrameSubsystem::SetupBar(FAsdaMiniBarFill& Bar, UImage* Fill, UImage* Back, float FallbackW)
{
	Bar.Fill = Fill;
	Bar.Back = Back;
	FVector2D Pos, Size;
	if (Back && UAsdaMiniStateSubsystem::GetSlotRect(Back, Pos, Size) && Size.X > 0.f)
	{
		Bar.BaseLeft = Pos.X; Bar.FullW = Size.X;
	}
	else if (Fill && UAsdaMiniStateSubsystem::GetSlotRect(Fill, Pos, Size) && Size.X > 0.f)
	{
		Bar.BaseLeft = Pos.X; Bar.FullW = Size.X;
	}
	if (Bar.FullW <= 0.f) { Bar.FullW = FallbackW; }
}

void UAsdaTargetFrameSubsystem::BindControls()
{
	if (!Win) { return; }

	// ── База панели (из её ширины эталон считает центр окна: halfTargetWidth = width/2) ─────────
	BaseGroup = AsdaUi::FindNodeExact(Win, TEXT("PVP_TARGET00_grp"));
	BaseImage = Cast<UImage>(FindPlane(TEXT("PVP_TARGET00"), 0));

	// ── Кольца ранга и их анимированные оверлеи (декор: idnum −1, логики в эталоне нет) ─────────
	RingNormal    = FindPlane(TEXT("Target_Gear_Normal"), 0);
	RingNamed     = FindPlane(TEXT("PVP_TARGETNAMED"), 0);
	RingElite     = FindPlane(TEXT("PVP_TARGETElite"), 0);
	RingBoss      = FindPlane(TEXT("PVP_TARGETBOSS"), 0);
	RingNormalAni = FindPlane(TEXT("Target_Gear_NormalAni"), 0);
	RingNamedAni  = FindPlane(TEXT("PVP_TARGETNamedAni"), 0);
	RingEliteAni  = FindPlane(TEXT("PVP_TARGETEliteAni"), 0);
	RingBossAni   = FindPlane(TEXT("PVP_TARGETBossAni"), 0);

	// ── Уровень: якорь, скрытый текст и 10 спрайтов-прототипов ──────────────────────────────────
	LvBaseGroup = AsdaUi::FindNodeExact(Win, TEXT("PVP_LVTxtBase_grp"));
	LvText      = Cast<UTextBlock>(AsdaUi::FindNodeExact(Win, TEXT("PVP_LVTXT")));
	SetVis(LvText, false);   // эталон: `CControl_SetVisible(pPvpLvTxt, 0)` в инициализаторе (74858)
	DigitProto.Reset();
	for (int32 d = 0; d < 10; ++d)
	{
		UImage* G = Cast<UImage>(FindPlane(*FString::Printf(TEXT("PVP_Count%d"), d), 0));
		DigitProto.Add(G);
		if (G)
		{
			DigitBrush[d] = G->GetBrush();
			bDigitBrushValid[d] = (DigitBrush[d].GetResourceObject() != nullptr);
			SetVis(G, false);   // прототипы не показываем — рисуют три рантайм-слота
		}
	}
	// Геометрия прототипа = позиция/размер `PVP_Count0` (эталон берёт их у клона-прототипа
	//   `pExpDigitProto`, созданного от Count0 с родителем PVP_LVTxtBase).
	{
		FVector2D P, S;
		if (DigitProto.IsValidIndex(0) && DigitProto[0] &&
			UAsdaMiniStateSubsystem::GetSlotRect(DigitProto[0], P, S))
		{
			DigitProtoX = P.X; DigitProtoY = P.Y;
			if (S.X > 0.f) { DigitProtoW = S.X; }
		}
		if (LvBaseGroup && UAsdaMiniStateSubsystem::GetSlotRect(LvBaseGroup, P, S) && S.X > 0.f)
		{
			LvBaseW = S.X;
		}
	}
	// Три рантайм-слота цифр (сотни/десятки/единицы) в канвасе якоря уровня.
	DigitSlot.Reset();
	if (UCanvasPanel* LvCanvas = Cast<UCanvasPanel>(LvBaseGroup))
	{
		// Имена даём осмысленные: клоны видны в дереве окна и в дампе `asda.Target.Nodes`,
		//   а безымянные `Image_1/2/3` там ничего не объясняют (закон ui-editable-in-ue).
		static const TCHAR* kDigitSlotNames[3] = {
			TEXT("PVP_LvDigit_Hundreds"), TEXT("PVP_LvDigit_Tens"), TEXT("PVP_LvDigit_Units") };
		for (int32 i = 0; i < 3; ++i)
		{
			UImage* C = NewObject<UImage>(Win, UImage::StaticClass(), FName(kDigitSlotNames[i]));
			C->SetVisibility(ESlateVisibility::Collapsed);
			if (UCanvasPanelSlot* CS = LvCanvas->AddChildToCanvas(C))
			{
				CS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
				CS->SetAlignment(FVector2D(0.f, 0.f));
				CS->SetOffsets(FMargin(DigitProtoX, DigitProtoY, DigitProtoW, 32.f));
			}
			DigitSlot.Add(C);
		}
	}
	else
	{
		UE_LOG(LogAsdaTarget, Warning, TEXT("[панель цели] PVP_LVTxtBase_grp не найден или не CanvasPanel — ")
			TEXT("спрайтовые цифры уровня работать не будут. Ассет перегенерировать НЕЛЬЗЯ ")
			TEXT("(законы no-invented-ui / ui-editable-in-ue), проверять импорт JUI."));
	}

	// ── Имя цели ────────────────────────────────────────────────────────────────────────────────
	NameText = Cast<UTextBlock>(AsdaUi::FindNodeExact(Win, TEXT("PVP_TARGETNAME")));

	// ── Полосы HP/MP. Порядок массива = эталонный `gap_818[4]`: 0 красная, 1 жёлтая, 2 зелёная,
	//    3 фиолетовая (индекс сегмента растёт вместе с запасом «жизней»). Паспортная ширина 157 px
	//    берётся из .JUI (контрол PVP_HPBAR Size «157 18»).
	SetupBar(HpBar[0], Cast<UImage>(FindPlane(TEXT("PVP_HPBAR"),   0)), nullptr, 157.f);
	// У жёлтой полосы скин BoostYellow_Bar двуплановый: план 0 — подложка, план 1 — заливка.
	SetupBar(HpBar[1], Cast<UImage>(FindPlane(TEXT("PVP_HPBAR_Y"), 1)),
	                   Cast<UImage>(FindPlane(TEXT("PVP_HPBAR_Y"), 0)), 157.f);
	SetupBar(HpBar[2], Cast<UImage>(FindPlane(TEXT("PVP_HPBAR_G"), 0)), nullptr, 157.f);
	SetupBar(HpBar[3], Cast<UImage>(FindPlane(TEXT("PVP_HPBAR_P"), 0)), nullptr, 157.f);
	SetupBar(MpBar,    Cast<UImage>(FindPlane(TEXT("PVP_MPBAR"),   0)), nullptr, 157.f);
	MpBarHide = FindPlane(TEXT("PVP_MPBAR_HIDE"), 0);
	HpBuff    = FindPlane(TEXT("PVP_HPBuff"), 0);
	MpBuff    = FindPlane(TEXT("PVP_MPBuff"), 0);

	// ── Портрет и рамка «цель в бою» ────────────────────────────────────────────────────────────
	Portrait     = Cast<UImage>(FindPlane(TEXT("PVP_IMAGE"), 0));
	BeingAttack  = Cast<UImage>(FindPlane(TEXT("PVP_BEING_ATTACK"), 0));
	BatFirstMark = FindPlane(TEXT("PVP_BAT_FIRST_MARK"), 0);

	// ── Стихийные метки ─────────────────────────────────────────────────────────────────────────
	EleBaseGroup = AsdaUi::FindNodeExact(Win, TEXT("PVP_ELE_Base_grp"));
	EleMark.Reset();
	for (int32 i = 0; i < 6; ++i)
	{
		EleMark.Add(FindPlane(*FString::Printf(TEXT("PVP_MARK%02d"), i), 0));
	}

	// ── Эмблема клана ───────────────────────────────────────────────────────────────────────────
	ClanGroup = AsdaUi::FindNodeExact(Win, TEXT("PVP_ClanMarkBase_grp"));
	ClanMark  = Cast<UImage>(FindPlane(TEXT("PVP_ClanMark"), 0));

	// ── 28 иконок состояний врага ───────────────────────────────────────────────────────────────
	EnemyStateGroup = AsdaUi::FindNodeExact(Win, TEXT("ENERMY_STATE_CONTROLWIN_grp"));
	EnemySlots.Reset();
	EnemySlots.SetNum(28);
	for (int32 i = 0; i < 28; ++i)
	{
		FAsdaEnemyStateSlot& S = EnemySlots[i];
		S.Group = AsdaUi::FindNodeExact(Win, *FString::Printf(TEXT("ENERMY_STATE_ICON%02d_grp"), i));
		S.Icon  = FirstImageIn(S.Group);   // номер плана у иконок «плавает» (56, 58, 60…) — берём первую картинку
		S.Stack = Cast<UTextBlock>(AsdaUi::FindNodeExact(Win, *FString::Printf(TEXT("ENERMY_STATE_ICON%02d"), i)));
		S.Cool  = FindPlane(*FString::Printf(TEXT("ENERMY_STATE_Cool%02d"), i), 0);
		SetVis(S.Cool, false);   // декоративный оверлей: idnum −1, вызовов в эталоне НЕТ
	}

	// Стартовое состояние = как после `hudResetCursorItemSlot`: всё погашено.
	ClearEnemyStateIcons();
	SetRing(EAsdaTargetRing::None);
	SetElementMark(-1);
	SetVis(BatFirstMark, false);
	SetVis(BeingAttack, false);
	SetVis(ClanGroup, false);
	for (FAsdaMiniBarFill& B : HpBar) { SetVis(B.Fill, false); SetVis(B.Back, false); }
	SetVis(MpBar.Fill, false);
	// Слой, закрывающий MP: в эталоне его показывают у мобов и НИКОГДА не прячут обратно —
	//   обновлений `PVP_MPBAR` в срезе нет ни одного, полоса MP цели не рисуется вовсе.
	SetVis(MpBarHide, CVarShowTargetMp.GetValueOnAnyThread() == 0);
	SetVis(HpBuff, false);   // ЧАСТИЧНО: контрол резолвится, рантайм-логики в эталоне нет
	SetVis(MpBuff, false);   // декоративный

	bControlsReady = true;

	int32 FoundStates = 0;
	for (const FAsdaEnemyStateSlot& S : EnemySlots) { if (S.Icon) { ++FoundStates; } }
	UE_LOG(LogAsdaTarget, Log,
		TEXT("[панель цели] контролы связаны: база=%s фон=%s | кольца N/Nm/E/B=%s/%s/%s/%s | ")
		TEXT("якорь уровня=%s цифр-прототипов=%d слотов цифр=%d | имя=%s | HP-полос=%d (ширина %.0f, левый край %.0f) | ")
		TEXT("MP=%s скрыватель=%s | портрет=%s рамка-бой=%s первый-удар=%s | стихий=%d подложка=%s | ")
		TEXT("клан=%s/%s | иконок состояний=%d/28"),
		*NodeTag(BaseGroup), *NodeTag(BaseImage),
		*NodeTag(RingNormal), *NodeTag(RingNamed), *NodeTag(RingElite), *NodeTag(RingBoss),
		*NodeTag(LvBaseGroup), DigitProto.Num(), DigitSlot.Num(), *NodeTag(NameText),
		(HpBar[0].IsValid() ? 1 : 0) + (HpBar[1].IsValid() ? 1 : 0) + (HpBar[2].IsValid() ? 1 : 0) + (HpBar[3].IsValid() ? 1 : 0),
		HpBar[0].FullW, HpBar[0].BaseLeft,
		*NodeTag(MpBar.Fill), *NodeTag(MpBarHide),
		*NodeTag(Portrait), *NodeTag(BeingAttack), *NodeTag(BatFirstMark),
		EleMark.Num(), *NodeTag(EleBaseGroup),
		*NodeTag(ClanGroup), *NodeTag(ClanMark), FoundStates);

	ApplyCentering();
}

// ★ Центрирование по ширине базовой панели. Эталон снимает для этого два числа
//   (`halfScreenWidth`, `halfTargetWidth`, ui_core.c:74903/74906 — структура прямо называет их
//   «центрирование target frame»), но НИ ОДНОГО ЧИТАТЕЛЯ у них нет: проверено вторым методом по
//   полному экспорту IDA. Значит в оригинале рамка стоит по анкеру .JUI (доля 0.4 ширины экрана)
//   и на широких экранах уезжает влево от центра. По просьбе владельца включаем ту самую
//   арифметику: анкер базовой группы = 0.5, сдвиг = −ширина/2 ⇒ панель ровно по центру.
void UAsdaTargetFrameSubsystem::ApplyCentering()
{
	if (!BaseGroup || !Win) { return; }
	UCanvasPanel* Root = Cast<UCanvasPanel>(Win->GetRootWidget());
	if (!Root) { return; }

	// ★ Исходную геометрию базы снимаем ОДИН раз, до любого сдвига. Иначе после первого
	//   центрирования `GetSlotRect` вернёт уже сдвинутый Left, дельта посчитается нулевой и
	//   откат ключа `asda.Target.CenterByPanel 0` не вернёт панель на место (живой прогон это
	//   и показал: «сдвиг узлов −0»).
	if (CenterDelta == 0.f && !bCentered)
	{
		FVector2D BasePos, BaseSize;
		if (!UAsdaMiniStateSubsystem::GetSlotRect(BaseGroup, BasePos, BaseSize) || BaseSize.X <= 0.f) { return; }
		JuiBaseLeft = BasePos.X;
		// halfTargetWidth эталона = ширина PVP_TARGET00 / 2. При анкере 0.5 позиция отсчитывается
		//   от середины экрана ⇒ база обязана начинаться в −halfW.
		CenterDelta = -BaseSize.X * 0.5f - JuiBaseLeft;
	}

	const bool bWant = CVarCenterByPanel.GetValueOnAnyThread() != 0;
	if (bWant == bCentered) { return; }

	// Двигаем ВСЕ прямые узлы корневого канваса одним общим правилом: у окна PVP_TARGET все они
	//   сидят на одном и том же анкере .JUI (доля 0.4), поэтому одинаковый сдвиг + смена анкера
	//   сохраняют взаимную раскладку. Иначе портрет/кольца/иконки состояний уехали бы от базы.
	// ★ Дедуп №2, В3-S10 (U15): НЕ переведено на `AsdaUi::ShiftCanvasSlot` / `ShiftRootChildren` намеренно — это не сдвиг
	//   на дельту, а ПЕРЕВЕС анкора X (0.4 ↔ 0.5) с компенсацией отступа: ось X каждого ребёнка принудительно становится
	//   POINT. Общая точка схему анкора не меняет, поэтому дала бы другую раскладку. Эталона у центрирования нет (поля
	//   `halfScreenWidth/halfTargetWidth` без читателя — см. выше), правило — решение владельца; у WBP_PVP_TARGET все дети
	//   корня POINT на 0.4 (перепись `asda.UI.AnchorCensus`), так что SPAN-оси здесь не теряются.
	const float Shift = bWant ? CenterDelta : -CenterDelta;
	const float NewAnchorX = bWant ? 0.5f : 0.4f;   // 0.4 = AlignXPos "16 40 15" из .JUI
	int32 Moved = 0;
	for (int32 i = 0; i < Root->GetChildrenCount(); ++i)
	{
		UWidget* Child = Root->GetChildAt(i);
		UCanvasPanelSlot* S = Child ? Cast<UCanvasPanelSlot>(Child->Slot) : nullptr;
		if (!S) { continue; }
		FAnchors A = S->GetAnchors();
		FMargin  O = S->GetOffsets();
		O.Left += Shift;
		A.Minimum.X = NewAnchorX; A.Maximum.X = NewAnchorX;
		S->SetAnchors(A);
		S->SetOffsets(O);
		++Moved;
	}
	bCentered = bWant;
	UE_LOG(LogAsdaTarget, Log, TEXT("[панель цели] центрирование=%s: левый край базы в .JUI %.0f, ")
		TEXT("сдвиг %.0f, анкер X %.2f, узлов передвинуто %d ")
		TEXT("(эталон: halfScreenWidth − halfTargetWidth; читателя у этих полей в оригинале НЕТ)"),
		bWant ? TEXT("по панели PVP_TARGET00") : TEXT("анкер .JUI 0.4"),
		JuiBaseLeft, Shift, NewAnchorX, Moved);
}

void UAsdaTargetFrameSubsystem::ShowFrame(bool bShow)
{
	if (bShow == bFrameShown) { return; }
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	bFrameShown = bShow;
	WM->SetWindowVisible(FName(kTargetWindowId), bShow);
	ASDA_CTRACE(EAsdaCT::Target, TEXT("панель цели %s (окно PVP_TARGET 40): моб=%s rid=%d userIdx=%d"),
		bShow ? TEXT("ПОКАЗАНА") : TEXT("СКРЫТА"), bTargetIsMob ? TEXT("да") : TEXT("нет"), CurRenderId, CurUserIdx);
	UE_LOG(LogAsdaTarget, Verbose, TEXT("[панель цели] окно %s"), bShow ? TEXT("показано") : TEXT("скрыто"));
}

// ══════════════════════════════ ТИК ════════════════════════════════════════════════════════════

void UAsdaTargetFrameSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.TargetFrame");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }
	// ★ Гейт «панель живёт только в мире» здесь НЕ нужен и вреден: цель физически не может
	//   существовать вне игрового мира, а требование `WS->LocalPlayerActor` ломало живую проверку
	//   без сервера (павн из char-select там не создаётся, и панель не поднималась НИ РАЗУ —
	//   ровно такое ложное «нет» и ловит закон false-negatives-method). Единственное условие —
	//   у контроллера есть выбранная цель; нет цели ⇒ окно спрятано и работы ноль.
	UWorld* World = GI->GetWorld();
	AAsdaPlayerController* PC = World ? Cast<AAsdaPlayerController>(World->GetFirstPlayerController()) : nullptr;
	AActor* Target = PC ? PC->TargetActor.Get() : nullptr;

	// ★ 06.10.2026 (ревизия): накопленные события клана — `sub_92132C(g_pHUD, 0)`: гасим только базу эмблемы,
	//   кисть не трогаем. Следующая пересборка панели (`ForceRefresh`/смена цели) нарисует эмблему заново по
	//   данным цели — как `HUD_UpdateTargetFrame` эталона.
	if (PendingClanMarkHide > 0)
	{
		PendingClanMarkHide = 0;
		SetVis(ClanGroup, false);
		UE_LOG(LogAsdaTarget, Log, TEXT("[панель цели] эмблема клана цели погашена событием клана (4107/4117/4132/4133)"));
	}

	// ★ Журнал боя: причина, по которой панель сейчас будет снята (пишется только при снятии, не каждый кадр).
#if ASDA_COMBAT_TRACE
	const TCHAR* TrHideWhy = Target ? TEXT("") : TEXT("у контроллера нет цели (TargetActor пуст)");
#endif
	// Мёртвая цель: эталон снимает рамку тем же путём, что и снятие цели
	//   (`hudClearCursorItemIfSlot` → `hudResetCursorItemSlot`, ui_core.c:77100).
	if (const AAsdaMobActor* M = Cast<AAsdaMobActor>(Target))
	{
#if ASDA_COMBAT_TRACE
		if (M->IsDead()) { TrHideWhy = TEXT("цель-моб мертва (IsDead — реальная смерть, не «обречён»)"); }
#endif
		// ★★★ ИСПРАВЛЕНО 2026-08-25 (владелец: «как только персонаж начал взводить оружие — выделение
		//   и плашка пропали»). Здесь стояло `|| bServerDead`, а эта метка — «обречён» (бит 0x20
		//   оригинала) — ставится СРАЗУ по приходу боевого пакета с нулевым здоровьем, то есть за
		//   полторы секунды до кадра удара. Метка нужна боевой логике, чтобы НЕ начинать новый замах
		//   по уже засчитанной цели, но гасить по ней ВИЗУАЛ нельзя: в оригинале рамка и подсветка
		//   живут, пока цель не умрёт по-настоящему. Условие — только реальная смерть.
		if (M->IsDead()) { Target = nullptr; }
	}
	else if (Target)
	{
		// ★ Панель цели в эталоне поднимают ровно два входа: `hudShowMobTargetInfo` (моб) и
		//   `hud_setTarget` (персонаж). НПС, лут и прочие акторы, по которым у нас тоже ставится
		//   `TargetActor` (кольцо под ногами — общий механизм), рамку показывать НЕ должны.
		UAsdaWorldSubsystem* WSg = GI->GetSubsystem<UAsdaWorldSubsystem>();
#if ASDA_COMBAT_TRACE
		if (!WSg || WSg->FindUserIdxByActor(Target) < 0) { TrHideWhy = TEXT("цель — не моб и не игрок из реестра (НПС/лут/прочее): панель не положена"); }
#endif
		if (!WSg || WSg->FindUserIdxByActor(Target) < 0) { Target = nullptr; }
	}

	if (!Target)
	{
		if (CurTarget.IsValid() || bFrameShown)
		{
			ASDA_CTRACE(EAsdaCT::Target, TEXT("панель цели СНИМАЕТСЯ: %s | прежняя цель моб=%s rid=%d userIdx=%d, последнее HP %d/%d"),
				TrHideWhy, bTargetIsMob ? TEXT("да") : TEXT("нет"), CurRenderId, CurUserIdx, LastHpCur, LastHpMax);
			// hudResetCursorItemSlot: спрятать окно, обнулить кэш цели.
			ShowFrame(false);
			CurTarget = nullptr; CurUserIdx = -1; CurRenderId = -1; CurSegments = 0; bTargetIsMob = false;
			LastHpCur = LastHpMax = LastLevel = -1; LastName.Reset();
			HpSmooth.Reset(); MpSmooth.Reset(); SmoothHpMax = 0; SmoothSegments = 0;
			bBeingAttackOn = false;
			// Эталон `hudResetCursorItemSlot` (ui_core.c:76065-76066) обнуляет кэш запроса 6538.
			TargetReqCharId = -1; TargetReqUserNum = -1;
			ClearEnemyStateIcons();
		}
		StateTip.Hide(GI, EAsdaTipOwner::TargetState);   // ★ 04.11.2026: панель снята — подсказка её иконки гаснет (эталон 0xA)
		return;
	}

	if (!EnsureWidgets()) { return; }

	if (CurTarget.Get() != Target)
	{
		RebuildForTarget(Target);
	}
	else
	{
		RefreshVitals();
		RefreshEnemyStateIcons(Target);
	}
	TickTargetBars(DeltaTime);   // ★ плавный ход полос цели (после RefreshVitals — по свежей цели)
	TickBeingAttackBlink();
	TickStateIconTips();   // ★ 04.11.2026: подсказки иконок состояний цели (эталон msg 8/9/0xA по 6703…6730)
}

// ★★★ 04.11.2026 (баг владельца «у цели не видно описаний баффов/дебаффов») [ОРИГИНАЛ]. Эталон `HUD_CommandHandler` (ui_core.c:76782):
//     msg 8, контрол 0x1A2F…0x1A4A (6703…6730) → g_nTargetBuffIconIdx = id % 0x1A2F; `HUD_TargetBuffIcon_OnClick(hud, i)`:
//       if (targetBuffId[i] > −1) { type == 0 → uiHelpEntryLayoutAndShow(id) (stateinfotip: имя + описание);
//                                   type == 1 → uiHelpEntryLayoutAndShowAlt(id) (soulstateinfotip) }
//     msg 9 → `Tooltip_LayoutNearCursor`; msg 0xA → `CControl_KillFocus(&g_ItemTooltip)`, индекс −1.
//   Та же плашка, что у своих иконок мини-статуса (0x3C…0x58) и у иконок отряда, — `UAsdaMiniStateSubsystem::ShowSharedTip`.
//   Гейт события 8 эталона — `!helper_getField1(g_pUIManager)` (предмет «в руке») — `bDragGate` общего помощника.
//   Порт не имел ни одного обработчика наведения на панели цели: слоты рисовались, подсказки не было ни у кого (свой/отряд/чужой/моб).
void UAsdaTargetFrameSubsystem::TickStateIconTips()
{
	UGameInstance* GI = GetGameInstance();
	int32 Slot = -1;
	if (bFrameShown && !AsdaUi::IsModalDispatchBlocked(GI))
	{
		for (int32 i = 0; i < EnemySlots.Num(); ++i)
		{
			const FAsdaEnemyStateSlot& S = EnemySlots[i];
			if (S.BuffId < 0 || !S.Icon) { continue; }
			const ESlateVisibility V = S.Icon->GetVisibility();
			if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { continue; }
			if (AsdaUi::IsCursorOverWidget(S.Icon)) { Slot = i; break; }
		}
	}
	if (Slot < 0) { StateTip.Hide(GI, EAsdaTipOwner::TargetState); return; }
	const int32 Id = EnemySlots[Slot].BuffId, Type = EnemySlots[Slot].BuffType;
	// Ключ = контрол + id: смена состояния в том же слоте под курсором пересобирает плашку (эталон держит её по индексу,
	//   а `HUD_TargetBuffIcons_Refresh` гасит её при изменении кэша — `hud[697]`, ui_core.c:77004-77008).
	StateTip.Update(GI, EAsdaTipOwner::TargetState, (6703 + Slot) * 1000 + Id, [GI, Id, Type]() -> bool
	{
		UAsdaEffectsSubsystem* Fx = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
		UAsdaMiniStateSubsystem* Mini = GI ? GI->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr;
		if (!Fx || !Mini) { return false; }
		FString Title, Atlas, Body; int32 Cell = 0;
		const bool bOk = (Type == 1) ? Fx->GetSoulStateInfo(Id, Title, Atlas, Cell, Body)   // uiHelpEntryLayoutAndShowAlt
		                             : (Type == 0 ? Fx->GetStateInfo(Id, Title, Atlas, Cell, Body) : false);   // uiHelpEntryLayoutAndShow
		if (!bOk) { return false; }
		Mini->ShowSharedTip(Title, Body, EAsdaTipOwner::TargetState);
		return true;
	});
}

void UAsdaTargetFrameSubsystem::ForceRefresh()
{
	CurTarget = nullptr;   // следующий тик пересоберёт панель с нуля
	LastHpCur = LastHpMax = LastLevel = -1;
	LastName.Reset();
	// Ключ `asda.Target.CenterByPanel` меняют между прогонами — раскладку надо переприменить здесь,
	//   иначе смена ключа не даст никакого видимого эффекта до перезахода в мир.
	if (bControlsReady) { ApplyCentering(); }
}

// ══════════════════════════════ ПЕРЕСБОР ПОД ЦЕЛЬ ══════════════════════════════════════════════

void UAsdaTargetFrameSubsystem::RebuildForTarget(AActor* Target)
{
	ASDA_CTRACE(EAsdaCT::Target, TEXT("панель цели: СМЕНА цели — пересборка (прежняя rid=%d userIdx=%d HP %d/%d → новая '%s')"),
		CurRenderId, CurUserIdx, LastHpCur, LastHpMax, Target ? *Target->GetName() : TEXT("нет"));
	CurTarget = Target;
	CurUserIdx = -1; CurRenderId = -1; CurSegments = 0; bTargetIsMob = false;
	LastHpCur = LastHpMax = LastLevel = -1;
	LastName.Reset();
	bBeingAttackOn = false;
	BlinkAlpha = 255.f;

	// Общая «шапка» обоих эталонных путей (`hud_setTarget` :75935-75957 и
	//   `HUD_UpdateTargetFrame` :77515-77546): гасим first-attack и рамку боя, сбрасываем её альфу,
	//   гасим шесть стихийных меток, показываем базовую панель, прячем эмблему клана.
	SetVis(BatFirstMark, false);
	SetVis(BeingAttack, false);
	if (BeingAttack) { BeingAttack->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 1.f)); }
	SetElementMark(-1);
	SetVis(BaseGroup, true);
	SetVis(ClanGroup, false);
	ClearEnemyStateIcons();

	UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;

	if (AAsdaMobActor* Mob = Cast<AAsdaMobActor>(Target))
	{
		bTargetIsMob = true;
		CurRenderId = Mob->NetRenderId;
		FillForMob(Mob);
	}
	else
	{
		bTargetIsMob = false;
		const int32 Uidx = WS ? WS->FindUserIdxByActor(Target) : -1;
		CurUserIdx = Uidx;
		FillForPlayer(Target, Uidx);
	}

	SetClanMark(Target);
	RefreshEnemyStateIcons(Target);
	ShowFrame(true);
}

// ── Цель-МОБ: эталон `hudShowMobTargetInfo @0xE7AB50` (ui_core.c:75701) ─────────────────────────
void UAsdaTargetFrameSubsystem::FillForMob(AAsdaMobActor* Mob)
{
	if (!Mob) { return; }
	// ★ 10.11.2026 [ОРИГИНАЛ] обучение: `hudShowMobTargetInfo@0xE7AB50` (ui_core.c:75733) — байт 85 при виде сущности
	//   +468 ∈ {0,1,2}; панель цели порта для мобов и есть эта ветка (NPC и персонажи идут другими заполнителями).
	AsdaTutorial::Mark(GetGameInstance(), EAsdaTutorialFlag::MobTargetInfo);

	// (1) Сегментированный HP. `ent+18700` = «жизни» типа (Mob list.BIN +0x10C).
	CurSegments = Mob->MobLives;
	const int32 MaxHp = (Mob->MaxHp > 0) ? Mob->MaxHp : Mob->MobTypeMaxHp;
	HpSmooth.Reset();                                  // ★ новая цель — полоса без «наливания»
	ASDA_CTRACE(EAsdaCT::HpTarget, TEXT("панель цели МОБ rid=%d '%s': стартовое HP %d/%d (%.1f%%), сегментов %d, max из %s, мёртв=%s обречён=%s"),
		Mob->NetRenderId, *Mob->MobName, Mob->CurHp, MaxHp, MaxHp > 0 ? 100.f * (float)Mob->CurHp / (float)MaxHp : 0.f,
		CurSegments, (Mob->MaxHp > 0) ? TEXT("актора (MaxHp)") : TEXT("типа (MobTypeMaxHp)"),
		Mob->IsDead() ? TEXT("да") : TEXT("нет"), Mob->bServerDead ? TEXT("да") : TEXT("нет"));
	ApplyTargetHp(Mob->CurHp, MaxHp, CurSegments);
	LastHpCur = Mob->CurHp; LastHpMax = MaxHp;

	// (2) MP у мобов закрыт чёрным слоем `PVP_MPBAR_HIDE` (эталон :75759).
	SetVis(MpBar.Fill, false);
	SetVis(MpBarHide, true);

	// (3) «Бьёт первым» — байт типа `Mob list.BIN +0x100` → `ent+6977` (эталон :75742).
	const bool bFirst = (Mob->MobAttackFirst == 1);
	SetVis(BatFirstMark, bFirst);

	// (4) Рамка «цель в бою». Эталон гейтит её словом `ent+474`, у которого во ВСЁМ бинаре нет
	//     писателя (Д2) ⇒ у мобов она не загоралась никогда. Восстанавливаем по боевому состоянию.
	const bool bLegacyBA = CVarLegacyBeingAttack.GetValueOnAnyThread() != 0;
	bBeingAttackOn = bLegacyBA
		? false                                              // 1:1 эталон: ent+474 всегда 0
		: (Mob->IsCombatStance() || Mob->GetChaseTargetUserIdx() >= 0);
	SetVis(BeingAttack, bBeingAttackOn);

	// (5) Стихийная метка — ИНДЕКС из `ent+6978` (Mob list.BIN +0xF8; в порту это поле зовётся
	//     `MobRank` — миснейм, см. шапку .h). Эталон: `if (ent[6978] < 6) показать метку[idx]`.
	SetElementMark((Mob->MobRank >= 0 && Mob->MobRank < 6) ? Mob->MobRank : -1);

	// (6) Базовая панель.
	SetVis(BaseGroup, true);

	// (7) Портрет типа: атлас `Mob list.BIN +0xD4`, ячейка +0xF4, сетка 46×46 по 11 колонок.
	if (!Mob->MobPortraitTex.IsEmpty())
	{
		SetPortraitFromAtlas(Mob->MobPortraitTex, Mob->MobPortraitCell, kEmblemPx, kEmblemCols);
	}
	else
	{
		ClearPortrait();
		UE_LOG(LogAsdaTarget, Warning, TEXT("[панель цели] у моба id=%d в DT_MobStats пустая колонка ")
			TEXT("PortraitTex — перегенерировать таблицу (Tools/asda_build_mob_stats_dt.py)"), Mob->MobId);
	}

	// (8) Кольцо ранга и цвет имени — оба по байту агрессии `ent+6979` (Mob list.BIN +0xFC).
	const int32 Aggr = Mob->MobAggression;
	SetRing(RingFromAggression(Aggr));
	const int32 SelfLvl = UAsdaNameplateComponent::ResolveSelfLevel(Mob->GetWorld());
	const int32 Diff = SelfLvl - Mob->MobLevel;
	SetNameAndColor(Mob->MobName, TargetNameColor(Diff, Aggr));

	// (9) Уровень спрайтами (эталон: `HUD_TargetLevelDigits(hud, *(ent+476))`).
	SetLevelDigits(Mob->MobLevel);
	LastLevel = Mob->MobLevel;

	UE_LOG(LogAsdaTarget, Log,
		TEXT("[панель цели] МОБ id=%d rid=%d '%s': уровень %d (мой %d, разница %d), агрессия/ранг %d → кольцо %d, ")
		TEXT("стихия %d, HP %d/%d сегментов %d, бьёт первым=%s, портрет %s#%d"),
		Mob->MobId, Mob->NetRenderId, *Mob->MobName, Mob->MobLevel, SelfLvl, Diff,
		Aggr, (int32)RingFromAggression(Aggr), Mob->MobRank, Mob->CurHp, MaxHp, CurSegments,
		bFirst ? TEXT("да") : TEXT("нет"), *Mob->MobPortraitTex, Mob->MobPortraitCell);
}

// ── Цель-ИГРОК: эталон `hud_setTarget @0xE7B470` + `HUD_UpdateTargetFrame` (ui_core.c:77497) ────
void UAsdaTargetFrameSubsystem::FillForPlayer(AActor* Player, int32 UserIdx)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	UAsdaNetSubsystem*   Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;

	FString Name; int32 ClassId = 0, Level = 0, ClanId = -1;
	bool bKnown = WS && WS->GetNetPlayerInfo(UserIdx, Name, ClassId, Level, ClanId);
	if (!bKnown && Net && UserIdx >= 0 && AsdaSelf::IsSelfUserIdx(Net, UserIdx))
	{
		// Своя цель бывает при само-выборе (лечение себя): свои поля живут в NetSubsystem.
		Name = WS ? WS->GetSelfCharName() : FString();
		// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U5d): класс — общая точка `AsdaSelf::GetClassId` (байт +0x438, фолбэк — запись char-select rec+24) (было 0 до 4005);
		//   ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U5c): уровень — общая точка `AsdaSelf::GetLevel` (до 4005/4036 — уровень выбранного персонажа, а не 0).
		ClassId = FMath::Max(AsdaSelf::GetClassId(Net), 0);
		Level = AsdaSelf::GetLevel(Net);
		bKnown = true;
	}
	if (!bKnown)
	{
		// Имя из плашки — последний источник (актор есть, а в реестре его нет: NPC, питомец и т. п.).
		if (const UAsdaNameplateComponent* NP = Player ? Player->FindComponentByClass<UAsdaNameplateComponent>() : nullptr)
		{
			Name = NP->DisplayName;
		}
	}

	// ★★★ (0) ЗАПРОС HP/MP ЦЕЛИ (6538) — ЕДИНСТВЕННЫЙ канал жизни чужого игрока.
	//   Эталон `hud_setTarget` (ui_core.c:75922-75932): если charId новой цели ОТЛИЧАЕТСЯ от кэша
	//   и цель — не я сам (`!*(_DWORD *)(target + 190260)`, флаг «локальный персонаж»), уходит 6538
	//   с charId/userNum новой цели и charId/userNum ПРЕЖНЕЙ; затем кэш переписывается.
	//   ⚠ Без этого пакета сервер не вписывает нас в список наблюдателей цели и НЕ шлёт 6539 —
	//     `GetVitals(UserIdx)` пуст, и полоса рисуется пустой (ровно тот баг, что был на скрине).
	const int32 TgtCharId = WS ? WS->GetNetPlayerCharId(UserIdx) : 0;
	const bool  bSelfTarget = (Net && UserIdx >= 0 && AsdaSelf::IsSelfUserIdx(Net, UserIdx));
	if (Net && UserIdx >= 0 && !bSelfTarget && TargetReqCharId != TgtCharId)
	{
		if (TgtCharId == 0)
		{
			// Молча слать запрос с нулевым charId нельзя: сервер по нему цель не найдёт и ответит
			//   подкодом 2 — это выглядело бы как «HP просто не приходит». Пишем, что именно пусто.
			UE_LOG(LogAsdaTarget, Warning,
				TEXT("[панель цели] у игрока userNum=%d в реестре НЕТ charId — запрос 6538 не отправлен, ")
				TEXT("HP цели не придёт (charId кладут снапшоты 4005/4065)"), UserIdx);
			ASDA_CTRACE(EAsdaCT::Refuse, TEXT("6538 НЕ отправлен: у цели-игрока userIdx=%d нет charId в реестре (ждём 4005/4065) — HP цели не придёт"),
				UserIdx);
		}
		else
		{
			ASDA_CTRACE(EAsdaCT::Send, TEXT("6538 запрос HP/MP цели: charId=%d userNum=%d (прежняя charId=%d userNum=%d) — ответ 6539"),
				TgtCharId, UserIdx, TargetReqCharId, TargetReqUserNum);
			Net->SendTargetInfoReq6538(TgtCharId, UserIdx, TargetReqCharId, TargetReqUserNum);
			TargetReqCharId = TgtCharId; TargetReqUserNum = UserIdx;
		}
	}
	else
	{
		// Причина — в порядке условия выше: сначала сеть (без неё bSelfTarget=false, а кэш charId мог совпасть случайно).
		ASDA_CTRACE(EAsdaCT::Target, TEXT("6538 не нужен: userIdx=%d %s"), UserIdx,
			!Net ? TEXT("— нет сети")
			: (bSelfTarget ? TEXT("— цель я сам (само-выбор)")
			: (UserIdx < 0 ? TEXT("— userIdx неизвестен")
			: TEXT("— запрос по этой charId уже отправлен"))));
	}

	// (1) Цель-игрок: у мобов нет «жизней», бар односегментный (эталон :77609-77623).
	CurSegments = 0;
	int32 CurHp = -1, MaxHp = 0;
	if (Net && UserIdx >= 0)
	{
		if (const UAsdaNetSubsystem::FAsdaVitals* V = Net->GetVitals(UserIdx))
		{
			CurHp = V->HpCur; MaxHp = V->HpMax;
		}
	}
	HpSmooth.Reset();                                  // ★ новая цель — полоса без «наливания»
	ASDA_CTRACE(EAsdaCT::HpTarget, TEXT("панель цели ИГРОК userIdx=%d '%s': стартовое HP %d/%d (%.1f%%), источник «%s»%s"),
		UserIdx, *Name, CurHp, MaxHp, (MaxHp > 0 && CurHp >= 0) ? 100.f * (float)CurHp / (float)MaxHp : 0.f,
		(Net && UserIdx >= 0 && Net->GetVitals(UserIdx)) ? *Net->GetVitals(UserIdx)->LastHpSrc : TEXT("нет витальных"),
		CurHp < 0 ? TEXT(" — HP ещё не приходило (ждём 6539/4058), полоса пустая") : TEXT(""));
	ApplyTargetHp(CurHp, MaxHp, 0);
	LastHpCur = CurHp; LastHpMax = MaxHp;

	// (2) MP цели: обновлений `PVP_MPBAR` в эталоне НЕТ ни одного (контрол только резолвится).
	const bool bShowMp = CVarShowTargetMp.GetValueOnAnyThread() != 0;
	if (bShowMp && Net && UserIdx >= 0)
	{
		const UAsdaNetSubsystem::FAsdaVitals* V = Net->GetVitals(UserIdx);
		const float Frac = (V && V->MpMax > 0) ? FMath::Clamp((float)V->MpCur / (float)V->MpMax, 0.f, 1.f) : 0.f;
		SetVis(MpBar.Fill, true);
		MpSmooth.SetTarget(Frac);                      // ★ догоняет в TickTargetBars
		UAsdaMiniStateSubsystem::ClipFill(MpBar, MpSmooth.Shown);
	}
	else
	{
		SetVis(MpBar.Fill, false);
		MpSmooth.Reset();
	}
	SetVis(MpBarHide, !bShowMp);

	// (3) У игрока first-attack не бывает, кольцо — только Normal (эталон :75957-75960 и :77543-77546).
	SetVis(BatFirstMark, false);
	SetRing(EAsdaTargetRing::Normal);

	// (4) Рамка «цель в бою»: эталон `objStateAllowsRender(target)` — «состояние цели допускает
	//     показ И у цели есть своя цель (renderId +208468 находится в менеджере)». В порту боевого
	//     состояния ЧУЖОГО игрока нет ни одним пакетом ⇒ канал заведён, значение пока «нет»
	//     (реестр отложенного, запись PT-1).
	bBeingAttackOn = false;
	SetVis(BeingAttack, false);

	// (5) Стихия цели-игрока — МАКСИМУМ из шести слов `target+1168..+1178`. Чужому персонажу они
	//     приезжают единственным каналом 4036 (боевой блок по userIdx).
	int32 BestIdx = -1, BestVal = 0;
	bool bEleKnown = false;
	if (Net && UserIdx >= 0)
	{
		if (const UAsdaNetSubsystem::FAsdaCombatStats* CS = Net->GetCombatStats(UserIdx))
		{
			bEleKnown = CS->bEleValid;
			for (int32 i = 0; i < 6; ++i)
			{
				if (CS->Ele[i] > BestVal) { BestVal = CS->Ele[i]; BestIdx = i; }
			}
		}
	}
	// Эталонный цикл стартует с `bestElemIdx = 0` и проверяет `if (bestElemIdx >= 0)` — условие
	//   истинно даже когда все шесть нулевые, поэтому оригинал ВСЕГДА зажигает «Огонь». Это дефект;
	//   по умолчанию гасим метку, ключ отката возвращает эталонное поведение.
	if (CVarLegacyElementMark.GetValueOnAnyThread() != 0) { BestIdx = FMath::Max(BestIdx, 0); }
	SetElementMark(BestIdx);

	// (6) Эмблема класса/PvP-ранга из атласа `char_emblems` (46×46, сетка 11 колонок).
	const int32 EmblemType = EmblemTypeFromClassId(ClassId);
	if (EmblemType >= 0) { SetPortraitFromAtlas(TEXT("char_emblems"), EmblemType, kEmblemPx, kEmblemCols); }
	else                 { ClearPortrait(); }

	// (7) Имя и уровень. Эталон обоим контролам ставит БЕЛЫЙ (−1) и лесенку разницы уровней
	//     цели-игроку НЕ применяет.
	SetNameAndColor(Name, FColor::White);
	SetLevelDigits(Level);
	LastLevel = Level;

	UE_LOG(LogAsdaTarget, Log,
		TEXT("[панель цели] ИГРОК userNum=%d '%s': уровень %d, класс %d → эмблема %d, HP %d/%d, ")
		TEXT("клан %d, стихии %s (лучшая %d, значение %d)"),
		UserIdx, *Name, Level, ClassId, EmblemType, CurHp, MaxHp, ClanId,
		bEleKnown ? TEXT("известны") : TEXT("НЕ приходили (нужен 4036)"), BestIdx, BestVal);
}

// ── Покадровое обновление значений текущей цели ─────────────────────────────────────────────────
void UAsdaTargetFrameSubsystem::RefreshVitals()
{
	AActor* T = CurTarget.Get();
	if (!T) { return; }

	int32 CurHp = -1, MaxHp = 0, Level = LastLevel;
	if (const AAsdaMobActor* Mob = Cast<AAsdaMobActor>(T))
	{
		CurHp = Mob->CurHp;
		MaxHp = (Mob->MaxHp > 0) ? Mob->MaxHp : Mob->MobTypeMaxHp;
		Level = Mob->MobLevel;
		// Рамка «цель в бою» пересчитывается каждый кадр: моб мог войти/выйти из погони.
		if (CVarLegacyBeingAttack.GetValueOnAnyThread() == 0)
		{
			const bool bNow = Mob->IsCombatStance() || Mob->GetChaseTargetUserIdx() >= 0;
			if (bNow != bBeingAttackOn)
			{
				ASDA_CTRACE(EAsdaCT::Target, TEXT("панель цели rid=%d '%s': рамка «цель в бою» %s (стойка=%s, гонится за userIdx=%d)"),
					Mob->NetRenderId, *Mob->MobName, bNow ? TEXT("ЗАЖЖЕНА") : TEXT("погашена"),
					Mob->IsCombatStance() ? TEXT("да") : TEXT("нет"), Mob->GetChaseTargetUserIdx());
				bBeingAttackOn = bNow;
				SetVis(BeingAttack, bNow);
				if (!bNow && BeingAttack) { BeingAttack->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 1.f)); }
			}
		}
	}
	else if (CurUserIdx >= 0)
	{
		UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		const UAsdaNetSubsystem::FAsdaVitals* V = Net ? Net->GetVitals(CurUserIdx) : nullptr;
		if (V)
		{
			CurHp = V->HpCur; MaxHp = V->HpMax;
		}
		// ★ MP цели тоже приезжает ПОЗЖЕ выбора (тем же 6539), поэтому его нельзя ставить только
		//   при пересборке панели: с ключом `asda.Target.ShowMp 1` полоса иначе застыла бы пустой.
		if (CVarShowTargetMp.GetValueOnAnyThread() != 0 && MpBar.IsValid())
		{
			const float Frac = (V && V->MpMax > 0) ? FMath::Clamp((float)V->MpCur / (float)V->MpMax, 0.f, 1.f) : 0.f;
			MpSmooth.SetTarget(Frac);
		}
		UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		FString Nm; int32 Cls = 0, Lv = 0, Cl = -1;
		if (WS && WS->GetNetPlayerInfo(CurUserIdx, Nm, Cls, Lv, Cl))
		{
			Level = Lv;
			// Эталон перерисовывает имя ТОЛЬКО при изменении (:77585-77593) — так же и здесь.
			if (Nm != LastName) { SetNameAndColor(Nm, FColor::White); }
		}
	}

	if (CurHp != LastHpCur || MaxHp != LastHpMax)
	{
#if ASDA_COMBAT_TRACE
		{
			// Источник значения: у моба — CurHp актора (пишут боевые пакеты/накопитель), у игрока — витальные (канал LastHpSrc).
			const AAsdaMobActor* TrMob = Cast<AAsdaMobActor>(T);
			const UAsdaNetSubsystem* TrNet = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			const UAsdaNetSubsystem::FAsdaVitals* TrV = (!TrMob && TrNet && CurUserIdx >= 0) ? TrNet->GetVitals(CurUserIdx) : nullptr;
			ASDA_CTRACE(EAsdaCT::HpTarget, TEXT("HP ЦЕЛИ %s %s'%s': %d→%d (max %d→%d) = %.1f%% Δ=%d, источник: %s%s"),
				TrMob ? TEXT("моб") : TEXT("игрок"),
				*FString::Printf(TEXT("%s=%d "), TrMob ? TEXT("rid") : TEXT("userIdx"), TrMob ? TrMob->NetRenderId : CurUserIdx),
				TrMob ? *TrMob->MobName : *LastName,
				LastHpCur, CurHp, LastHpMax, MaxHp, (MaxHp > 0 && CurHp >= 0) ? 100.f * (float)CurHp / (float)MaxHp : 0.f,
				(LastHpCur >= 0 && CurHp >= 0) ? CurHp - LastHpCur : 0,
				TrMob ? TEXT("CurHp актора") : (TrV ? *TrV->LastHpSrc : TEXT("витальных нет")),
				(TrMob && TrMob->bServerDead) ? TEXT(" [обречён: сервер засчитал смерть, ждём кадр удара]") : TEXT(""));
		}
#endif
		LastHpCur = CurHp; LastHpMax = MaxHp;
		ApplyTargetHp(CurHp, MaxHp, CurSegments);
	}
	if (Level != LastLevel) { LastLevel = Level; SetLevelDigits(Level); }
}

// ══════════════════════════════ ПОЛОСЫ HP ══════════════════════════════════════════════════════

// Одиночный бар: `SetRange(0, maxHp)` + `SetValue(curHp)` (эталон :77617-77623 и ветка `else`
//   сегментного апдейта). У UMG «range+value» = клип ширины плана-заливки.
// ★★ ЦИФРЫ ЗДОРОВЬЯ ПОВЕРХ ПОЛОСЫ ЦЕЛИ (просьба владельца 2026-08-25).
//   ⚠ В ОРИГИНАЛЕ ИХ НЕТ: у окна `PVP_TARGET` нет ни контрола под такую подпись, ни кода, который
//   бы её заполнял (у полос игрока числа есть, у панели цели — не предусмотрены). Поэтому подпись
//   создаётся нашим виджетом и кладётся поверх полосы `PVP_HPBAR` — её место в .JUI «56 20»,
//   размер «157 18», отсюда и координаты. Ключ отката: `asda.Target.HpNumbers 0`.
static ASDA_DEV_CVAR(int32, CVarAsdaTargetHpNumbers,
	TEXT("asda.Target.HpNumbers"), 1,
	TEXT("1 = показывать цифры здоровья цели поверх полосы (текущее/максимум); 0 = как в оригинале, без них."),
	ECVF_Default);

void UAsdaTargetFrameSubsystem::EnsureHpNumbers()
{
	if (HpNumbers || !Win || !Win->WidgetTree) { return; }
	// Родитель — тот же канвас, в котором лежит сама полоса: так подпись поедет вместе с окном.
	UWidget* BarW = HpBar.IsValidIndex(0) ? Cast<UWidget>(HpBar[0].Fill) : nullptr;
	UPanelWidget* Parent = BarW ? BarW->GetParent() : nullptr;
	UCanvasPanel* Canvas = Cast<UCanvasPanel>(Parent);
	if (!Canvas) { return; }

	UTextBlock* T = Win->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("AsdaTargetHpNumbers"));
	// Шрифт и цвет — как у подписи имени цели, чтобы панель выглядела единой.
	if (NameText) { T->SetFont(NameText->GetFont()); }
	FSlateFontInfo F = T->GetFont();
	F.Size = 10;
	T->SetFont(F);
	T->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	T->SetJustification(ETextJustify::Center);
	T->SetVisibility(ESlateVisibility::HitTestInvisible);   // подпись не должна перехватывать мышь

	if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(Canvas->AddChild(T)))
	{
		// Полоса PVP_HPBAR: Origin «56 20», Size «157 18» — кладём подпись ровно на неё.
		CS->SetAutoSize(false);
		CS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
		CS->SetAlignment(FVector2D(0.f, 0.f));
		CS->SetPosition(FVector2D(56.f, 20.f));
		CS->SetSize(FVector2D(157.f, 18.f));
		CS->SetZOrder(50);   // поверх всех слоёв полосы
	}
	HpNumbers = T;
}

void UAsdaTargetFrameSubsystem::UpdateHpNumbers(int32 CurHp, int32 MaxHp)
{
	const bool bWant = (CVarAsdaTargetHpNumbers.GetValueOnGameThread() != 0) && CurHp >= 0 && MaxHp > 0;
	if (bWant) { EnsureHpNumbers(); }
	if (!HpNumbers) { return; }
	if (!bWant)
	{
		HpNumbers->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	HpNumbers->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), FMath::Max(0, CurHp), MaxHp)));
	HpNumbers->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UAsdaTargetFrameSubsystem::SetSimpleHp(int32 CurHp, int32 MaxHp)
{
	UpdateHpNumbers(CurHp, MaxHp);
	for (int32 i = 1; i < HpBar.Num(); ++i) { SetVis(HpBar[i].Fill, false); SetVis(HpBar[i].Back, false); }
	if (!HpBar.IsValidIndex(0) || !HpBar[0].IsValid()) { return; }
	// Значения ещё не приходили (HpCur == −1) — полосу не трогаем, как эталон при maxHp == 0.
	if (CurHp < 0 || MaxHp <= 0) { SetVis(HpBar[0].Fill, false); return; }
	SetVis(HpBar[0].Fill, true);
	UAsdaMiniStateSubsystem::ClipFill(HpBar[0], (float)CurHp / (float)MaxHp);
}

// ★ Сегментированная полоса — дословный порт `hudUpdateSegmentedHpBar @0xE7A860` (ui_core.c:75641).
//   Эталон построчно:
//       if (maxHp < curHp) return 0;                       // защита от рассинхрона
//       для всех 4: SetValue(100.0); SetVisible(0);
//       if (segments) {
//           segSize = maxHp / segments;
//           v8 = segments;  if (maxHp != curHp) v8 = (int)(curHp / segSize);
//           if (maxHp <= curHp) --v8;                      // полный HP ⇒ последний сегмент
//           v7 = v8 - 1;  v6 = (int)(curHp - v8*segSize);
//           if (curHp >= segSize) { bar[v7]: Range(0,100) Value(100) Visible }   // «полный» ниже
//           bar[v8]: Range(0, segSize), Value(maxHp==curHp ? segSize : v6), Visible
//       } else { bar[0]: Visible, Range(0,maxHp), Value(curHp) }
//   Порядок массива: 0 красная, 1 жёлтая, 2 зелёная, 3 фиолетовая — по мере расходования «жизней»
//   индекс уменьшается, поэтому босс «слезает» с фиолетовой на красную.
void UAsdaTargetFrameSubsystem::UpdateSegmentedHp(int32 CurHp, int32 MaxHp, int32 Segments)
{
	if (MaxHp < CurHp) { return; }                       // эталонный ранний выход
	UpdateHpNumbers(CurHp, MaxHp);                       // цифры одинаковы для обеих форм полосы
	for (FAsdaMiniBarFill& B : HpBar) { SetVis(B.Fill, false); SetVis(B.Back, false); }
	if (Segments <= 0)
	{
		SetSimpleHp(CurHp, MaxHp);
		return;
	}
	if (MaxHp <= 0 || CurHp < 0) { return; }

	const float SegSize = (float)MaxHp / (float)Segments;
	if (SegSize <= 0.f) { return; }
	int32 Idx = Segments;
	if (MaxHp != CurHp) { Idx = (int32)((float)CurHp / SegSize); }
	if (MaxHp <= CurHp) { --Idx; }
	Idx = FMath::Clamp(Idx, 0, 3);
	const int32 Below = Idx - 1;
	const float Rest = (float)CurHp - (float)Idx * SegSize;

	if ((float)CurHp >= SegSize && HpBar.IsValidIndex(Below) && Below >= 0 && HpBar[Below].IsValid())
	{
		SetVis(HpBar[Below].Back, true);
		SetVis(HpBar[Below].Fill, true);
		UAsdaMiniStateSubsystem::ClipFill(HpBar[Below], 1.f);   // Range(0,100) Value(100) = полная
	}
	if (HpBar.IsValidIndex(Idx) && HpBar[Idx].IsValid())
	{
		const float Val = (MaxHp == CurHp) ? SegSize : FMath::Max(0.f, Rest);
		SetVis(HpBar[Idx].Back, true);
		SetVis(HpBar[Idx].Fill, true);
		UAsdaMiniStateSubsystem::ClipFill(HpBar[Idx], Val / SegSize);
	}
}

// ★ Слой плавности над эталонными рисовальщиками полос.
//   ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА (заявка владельца): в игре 2008 полоса цели прыгает мгновенно.
//   Ключ отката — `asda.UI.BarSmoothSec 0`. Сглаживается ТОЛЬКО картинка: и логика сегментов, и
//   цифры уровня, и переходы «жизней» считаются от настоящих значений, просто на кадр позже.
void UAsdaTargetFrameSubsystem::ApplyTargetHp(int32 CurHp, int32 MaxHp, int32 Segments)
{
	SmoothHpMax = MaxHp;
	SmoothSegments = Segments;
	if (MaxHp <= 0 || CurHp < 0)
	{
		ASDA_CTRACE(EAsdaCT::HpTarget, TEXT("полоса HP цели НЕ рисуется: HP неизвестно (cur=%d max=%d, сегментов %d) — заливка погашена, сглаживание сброшено"),
			CurHp, MaxHp, Segments);
		HpSmooth.Reset();
		if (Segments > 0) { UpdateSegmentedHp(CurHp, MaxHp, Segments); }
		else              { SetSimpleHp(CurHp, MaxHp); }
		return;
	}
#if ASDA_COMBAT_TRACE
	const float TrWasShown = HpSmooth.Shown;
#endif
	HpSmooth.SetTarget((float)CurHp / (float)MaxHp);
	ASDA_CTRACE(EAsdaCT::HpTarget, TEXT("полоса HP цели: цель %.3f (%d/%d, сегментов %d), сейчас нарисовано %.3f → %s"),
		HpSmooth.Target, CurHp, MaxHp, Segments, TrWasShown,
		TrWasShown < 0.f ? TEXT("сразу (первый показ)")
		: (FAsdaBarSmoother::SmoothSeconds() <= 0.f ? TEXT("сразу (сглаживание выкл)") : TEXT("плавно (TickTargetBars)")));
	DrawTargetHpShown();
}

void UAsdaTargetFrameSubsystem::DrawTargetHpShown()
{
	if (SmoothHpMax <= 0) { return; }
	// Округляем ВВЕРХ: пока показанная доля не дошла до нуля, полоса не должна схлопываться в 0.
	const int32 Shown = FMath::Clamp(
		FMath::CeilToInt(FMath::Clamp(HpSmooth.Shown, 0.f, 1.f) * (float)SmoothHpMax), 0, SmoothHpMax);
	if (SmoothSegments > 0) { UpdateSegmentedHp(Shown, SmoothHpMax, SmoothSegments); }
	else                    { SetSimpleHp(Shown, SmoothHpMax); }
}

void UAsdaTargetFrameSubsystem::TickTargetBars(float DeltaSeconds)
{
	const float Sec = FAsdaBarSmoother::SmoothSeconds();
#if ASDA_COMBAT_TRACE
	const float TrHpWas = HpSmooth.Shown;
#endif
	if (HpSmooth.Advance(DeltaSeconds, Sec))
	{
		DrawTargetHpShown();
		// Покадровый ход полосы — только на уровне 2; приход к цели — одной строкой на уровне 1.
		ASDA_CTRACE_V(EAsdaCT::HpTarget, TEXT("полоса HP цели кадр: %.3f→%.3f (цель %.3f, dt %.3f, сглаж. %.2f с)"),
			TrHpWas, HpSmooth.Shown, HpSmooth.Target, DeltaSeconds, Sec);
#if ASDA_COMBAT_TRACE
		if (HpSmooth.Shown == HpSmooth.Target && TrHpWas >= 0.f && TrHpWas != HpSmooth.Target)
		{
			ASDA_CTRACE(EAsdaCT::HpTarget, TEXT("полоса HP цели ДОЕХАЛА: %.3f (≈%d/%d)"),
				HpSmooth.Shown, FMath::CeilToInt(HpSmooth.Shown * (float)SmoothHpMax), SmoothHpMax);
		}
#endif
	}
	if (MpSmooth.Advance(DeltaSeconds, Sec) && MpBar.IsValid())
	{
		UAsdaMiniStateSubsystem::ClipFill(MpBar, MpSmooth.Shown);
	}
}

// ══════════════════════════════ УРОВЕНЬ СПРАЙТАМИ ══════════════════════════════════════════════

// Порт `HUD_TargetLevelDigits @0xE80C50` (ui_core.c:77630). Эталон раскладывает уровень на
//   сотни/десятки/единицы и считает X по ширине якоря `PVP_LVTxtBase` (обозначим её W) и по
//   позиции/ширине прототипа цифры (protoX, protoW):
//     три разряда:  X(сотни) = protoX − 6; X(десятки) = protoX + W/3 − 6; X(единицы) = protoX + 2W/3 − 6
//     два разряда:  X(единицы) = protoX + W/2 − 2;  X(десятки) = protoX + W/2 − protoW + 2
//     один разряд:  X(единицы) = protoX + W/2 − protoW/2 − 2
//   Y у всех = protoY. Показываются только задействованные разряды.
void UAsdaTargetFrameSubsystem::SetLevelDigits(int32 Level)
{
	SetVis(LvBaseGroup, true);   // эталон: `SetVisible(pPvpLvTxtBase, 1)` первым делом
	if (DigitSlot.Num() != 3) { return; }
	for (UImage* S : DigitSlot) { SetVis(S, false); }
	if (Level < 0) { return; }

	const int32 Hundreds = Level / 100;
	const int32 Tens     = (Level % 100) / 10;
	const int32 Units    = Level % 10;
	const float W = LvBaseW;
	const float PX = DigitProtoX, PY = DigitProtoY, PW = DigitProtoW;

	auto Place = [&](int32 SlotIdx, int32 Digit, float X)
	{
		UImage* Img = DigitSlot[SlotIdx];
		if (!Img || !bDigitBrushValid[Digit]) { return; }
		Img->SetBrush(DigitBrush[Digit]);
		if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(Img))
		{
			CS->SetOffsets(FMargin(X, PY, PW, 32.f));
		}
		SetVis(Img, true);
	};

	if (Hundreds > 0)
	{
		Place(0, Hundreds, PX - 6.f);
		Place(1, Tens,     PX + W / 3.f - 6.f);
		Place(2, Units,    PX + 2.f * W / 3.f - 6.f);
	}
	else if (Tens > 0)
	{
		Place(1, Tens,  PX + W / 2.f - PW + 2.f);
		Place(2, Units, PX + W / 2.f - 2.f);
	}
	else
	{
		Place(2, Units, PX + W / 2.f - PW / 2.f - 2.f);
	}
}

// ══════════════════════════════ КОЛЬЦА, МЕТКИ, ИМЯ ═════════════════════════════════════════════

EAsdaTargetRing UAsdaTargetFrameSubsystem::RingFromAggression(int32 Aggression)
{
	// `switch (ent+6979)` из `hudShowMobTargetInfo`: 1 и 5 → Normal, 2 → Named, 3 → Boss, 4 → Elite,
	//   `default` → ни одного кольца (все четыре остаются погашенными).
	switch (Aggression)
	{
	case 1: case 5: return EAsdaTargetRing::Normal;
	case 2:         return EAsdaTargetRing::Named;
	case 3:         return EAsdaTargetRing::Boss;
	case 4:         return EAsdaTargetRing::Elite;
	default:        return EAsdaTargetRing::None;
	}
}

// ★★★ ДУБЛИРУЮЩИЕ КОЛЬЦА РАНГА (правка по просьбе владельца 2026-08-25). У каждого ранга в .JUI
//   ЛЕЖИТ ПАРА: неподвижная шестерёнка (`Target_Gear_Normal`, `PVP_TARGETNAMED`, `PVP_TARGETElite`,
//   `PVP_TARGETBOSS`) и анимированная поверх неё (те же имена с `Ani`). Порт показывал ОБЕ, и
//   неподвижная просвечивала из-под анимированной — владелец видел это как дубль.
//   Теперь видна только анимированная. Ключ возврата: `asda.Target.StaticRings 1`.
static ASDA_DEV_CVAR(int32, CVarAsdaTargetStaticRings,
	TEXT("asda.Target.StaticRings"), 0,
	TEXT("1 = показывать и НЕПОДВИЖНЫЕ шестерёнки ранга в панели цели (дубль под анимированными); 0 = только анимированные."),
	ECVF_Default);

void UAsdaTargetFrameSubsystem::SetRing(EAsdaTargetRing Ring)
{
	const bool bStatic = CVarAsdaTargetStaticRings.GetValueOnGameThread() != 0;
	SetVis(RingNormal, bStatic && Ring == EAsdaTargetRing::Normal);
	SetVis(RingNamed,  bStatic && Ring == EAsdaTargetRing::Named);
	SetVis(RingElite,  bStatic && Ring == EAsdaTargetRing::Elite);
	SetVis(RingBoss,   bStatic && Ring == EAsdaTargetRing::Boss);
	// Анимированные оверлеи колец: idnum −1, вызовов в эталоне нет, но в .JUI они лежат ровно
	//   поверх своих колец — именно их и показываем.
	SetVis(RingNormalAni, Ring == EAsdaTargetRing::Normal);
	SetVis(RingNamedAni,  Ring == EAsdaTargetRing::Named);
	SetVis(RingEliteAni,  Ring == EAsdaTargetRing::Elite);
	SetVis(RingBossAni,   Ring == EAsdaTargetRing::Boss);
	for (UWidget* A : { RingNormalAni.Get(), RingNamedAni.Get(), RingEliteAni.Get(), RingBossAni.Get() })
	{
		if (UAsdaAnimatedPlane* P = Cast<UAsdaAnimatedPlane>(A))
		{
			if (P->GetVisibility() != ESlateVisibility::Collapsed) { P->RestartAnim(); }
		}
	}
}

void UAsdaTargetFrameSubsystem::SetElementMark(int32 Idx)
{
	for (int32 i = 0; i < EleMark.Num(); ++i) { SetVis(EleMark[i], i == Idx); }
	// Д1: подложка `PVP_ELE_Base` в .JUI скрыта, а метки — её дети. Показываем подложку вместе
	//   с меткой, иначе метки не видно (ключ отката asda.Target.LegacyEleBase 1).
	const bool bLegacy = CVarLegacyEleBase.GetValueOnAnyThread() != 0;
	SetVis(EleBaseGroup, !bLegacy && Idx >= 0);
}

// Эталонная лесенка ОКНА ЦЕЛИ. Верхняя ступень FFAAAAAA (−5592406) — не путать с FF808080
//   над-головной плашки (`DT_MobNameColors`, там своя таблица).
FColor UAsdaTargetFrameSubsystem::TargetNameColor(int32 LevelDiff, int32 Aggression)
{
	if (Aggression == 2) { return FColor(0xE2, 0x4A, 0xCB, 0xFF); }   // −1946933
	if (Aggression == 3) { return FColor(0xC3, 0xA8, 0x68, 0xFF); }   // −3954584
	// Ветки 1/5 (Normal) и 4 (Elite) идут по лесенке; прочие агрессии эталон не красит вовсе —
	//   контрол остаётся белым после сброса в `hud_setTarget`.
	if (Aggression != 1 && Aggression != 4 && Aggression != 5) { return FColor::White; }
	if (LevelDiff >= 7)  { return FColor(0xAA, 0xAA, 0xAA, 0xFF); }   // −5592406
	if (LevelDiff <= -6) { return FColor(0xFF, 0x00, 0x00, 0xFF); }   // −65536
	if (LevelDiff >= -5 && LevelDiff <= -2) { return FColor(0xFF, 0xA2, 0x00, 0xFF); }   // −24064
	if (LevelDiff >= -1 && LevelDiff <=  1) { return FColor(0xFF, 0xFF, 0xFF, 0xFF); }   // −1
	if (LevelDiff >=  2 && LevelDiff <=  6) { return FColor(0x00, 0xFF, 0x00, 0xFF); }   // −16711936
	return FColor::White;
}

void UAsdaTargetFrameSubsystem::SetNameAndColor(const FString& Name, const FColor& Srgb)
{
	LastName = Name;
	// Цвета клиента заданы в sRGB — переводим (закон ui-srgb-gamma-rule).
	const FLinearColor Lin = FLinearColor::FromSRGBColor(Srgb);
	if (NameText)
	{
		NameText->SetText(FText::FromString(Name));
		NameText->SetColorAndOpacity(FSlateColor(Lin));
	}
	if (LvText) { LvText->SetColorAndOpacity(FSlateColor(Lin)); }   // эталон красит и его (скрытый)
	// Д4: спрайты уровня эталон не красит (они и не текст) — по умолчанию красим, чтобы цвет
	//   уровня был виден так же, как цвет имени. Откат: asda.Target.LegacyLevelColor 1.
	if (CVarLegacyLevelColor.GetValueOnAnyThread() == 0)
	{
		for (UImage* S : DigitSlot) { if (S) { S->SetColorAndOpacity(Lin); } }
	}
	else
	{
		for (UImage* S : DigitSlot) { if (S) { S->SetColorAndOpacity(FLinearColor::White); } }
	}
}

// ══════════════════════════════ ПОРТРЕТ И ЭМБЛЕМА КЛАНА ════════════════════════════════════════

int32 UAsdaTargetFrameSubsystem::EmblemTypeFromClassId(int32 ClassId)
{
	// Дословно `HUD_UpdateTargetFrame` (ui_core.c:77548-77570), байт `target+1080`:
	//   0 → 3;  1..7 → 0;  12..18 → 1;  23..29 → 2;  всё остальное → −1 (эмблемы нет).
	if (ClassId == 0) { return 3; }
	if (ClassId >= 1  && ClassId <= 7)  { return 0; }
	if (ClassId >= 12 && ClassId <= 18) { return 1; }
	if (ClassId >= 23 && ClassId <= 29) { return 2; }
	return -1;
}

void UAsdaTargetFrameSubsystem::SetPortraitFromAtlas(const FString& AtlasName, int32 Cell, int32 CellPx, int32 Cols)
{
	if (!Portrait || AtlasName.IsEmpty()) { return; }
	// Ячейка → пиксели: `gridIndexToXY_11col@0xEBFFE0` (X = 46·(i%11), Y = 46·(i/11)).
	// ★ Дедуп №2, В1-B (U4a, 11.10.2026): построчная копия общей точки `AsdaUi::ApplyAtlasCellIcon` заменена вызовом
	//   (тот же загрузчик `LoadUITexture`, логический размер атласа, кисть Cs×Cs); false = атлас не найден.
	if (!AsdaUi::ApplyAtlasCellIcon(Portrait, AtlasName, Cell, CellPx, Cols))
	{
		UE_LOG(LogAsdaTarget, Warning, TEXT("[панель цели] атлас портрета '%s' не найден в /Game/AsdaAssets/Textures/JUI"),
			*AtlasName);
		ClearPortrait();
		return;
	}
	SetVis(Portrait, true);
}

void UAsdaTargetFrameSubsystem::ClearPortrait()
{
	// Эталон в этом случае рисует «пустую» текстуру клиента 32×32 (`CClass_GetSubObjectAtFixedOff`);
	//   у нас достаточно погасить контрол — под ним лежит подложка панели.
	SetVis(Portrait, false);
}

// Эмблема клана цели: RT 128×64, из него берётся под-прямоугольник (23,12)+64×42 — ровно как в
//   эталоне `CControl_DrawTextureRect(pPvpClanMark, flagTex, 23, 12, 64, 42)` (ui_core.c:75982).
//   Видимость базы включает `sub_92132C(hud, 1)` = `SetVisible(pPvpClanMarkBase, on)`.
void UAsdaTargetFrameSubsystem::SetClanMark(AActor* Target)
{
	if (!ClanGroup || !ClanMark) { return; }
	const UAsdaNameplateComponent* NP = Target ? Target->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
	if (!NP || !NP->bClanFlagOn || !NP->ClanFlag.IsValid())
	{
		SetVis(ClanGroup, false);
		SetVis(ClanMark, false);
		return;
	}
	// ★ 06.10.2026 (ревизия, вне задачи): ВЕТКА КАМПВАРА (ui_core.c:75970-75976) — при байте +69 текущей карты
	//   эталон рисует не флаг, а ячейку 64×64 `CampWar_MarkImage` по лагерю ЦЕЛИ (`target+209016`; в порту —
	//   `UAsdaNameplateComponent::CampId`, его пишут 6713/6722). Общая точка с портретом — `UAsdaClanFlag`.
	//   Ветка `else if (+69)` без объекта флага у эталона мертва (внутри снова требует объект) — не переносится.
	const bool bCamp = UAsdaClanFlag::IsCampWarMap(this);   // карта кампвара
	if (bCamp)
	{
		UAsdaClanFlag::ApplyCampMark(ClanMark, NP->CampId, 32.f);   // [ПОРТ] лагерь вне 0..2 → пустая кисть
	}
	else
	{
		// ★ 06.10.2026: кисть — общая точка `UAsdaClanFlag::MakePlateBrush` (её же берёт портрет своего
		//   персонажа, `UAsdaMiniStateSubsystem::DrawPlayerPortraitMark`); поведение прежнее.
		FSlateBrush B;   // кисть выреза эмблемы 32×32
		if (!UAsdaClanFlag::MakePlateBrush(this, NP->ClanFlag, 32.f, B)) { SetVis(ClanGroup, false); SetVis(ClanMark, false); return; }
		ClanMark->SetBrush(B);
	}
	SetVis(ClanMark, true);
	SetVis(ClanGroup, true);
	UE_LOG(LogAsdaTarget, Verbose, TEXT("[панель цели] эмблема клана %d нарисована (%s)"),
		NP->ClanId, bCamp ? TEXT("кампвар: ячейка лагеря цели") : TEXT("RT 128×64, вырез 23,12,64×42"));
}

// ★ 06.10.2026 (ревизия): 4107/4117 код 1 и 4133 код 2 — только СВОЙ сброс (сеть шлёт OnClanReset и о чужих по 4133).
void UAsdaTargetFrameSubsystem::OnClanResetNet(int32 CharId, const FString& CharName)
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const bool bById   = Net && Net->SelectedCharId != 0 && CharId == Net->SelectedCharId;
	const bool bByName = WS && !CharName.IsEmpty() && WS->GetSelfCharName().Equals(CharName);
	if (bById || bByName) { ++PendingClanMarkHide; }
}

// ★ 06.10.2026 (ревизия): 4132 код 2 — `uiTradeWindowResetState` (ui_core.c:17213-17214).
void UAsdaTargetFrameSubsystem::OnClanBaseInfoPushNet(int32 Opcode, const TArray<uint8>& Body)
{
	if (Opcode == 4132 && Body.Num() >= 1 && Body[0] == 2) { ++PendingClanMarkHide; }
}

// ══════════════════════════════ ИКОНКИ СОСТОЯНИЙ ЦЕЛИ ══════════════════════════════════════════

void UAsdaTargetFrameSubsystem::ClearEnemyStateIcons()
{
	for (FAsdaEnemyStateSlot& S : EnemySlots)
	{
		S.BuffId = -1; S.BuffType = 0; S.StackNum = 0;
		SetVis(S.Icon, false);
		if (S.Stack) { S.Stack->SetText(FText::GetEmpty()); }
		SetVis(S.Cool, false);
	}
}

// Порт `HUD_TargetBuffIcons_Update @0xE7F1F0` (цель-игрок, список `target+2556`) и
//   `HUD_TargetBuffIcons_Refresh @0xE7EA10` (цель-моб, список `target+18580`). Общее у обоих:
//     • сперва ВСЕ 28 слотов гасятся (пустая текстура + пустой текст);
//     • затем список обходится по порядку наложения, `type == 2` (предметные усилители) у игрока
//       ПРОПУСКАЕТСЯ, у моба пропускается `type == 1`;
//     • иконка = ячейка 24×24 атласа из `stateinfotip` (`IconAtlas_IndexToUV` = сетка 10 колонок);
//     • число стека 2..30 рисуется поверх иконки жёлтым (`<FC=ffFFFF00>N</F>`).
//   ★ ОТСТУПЛЕНИЕ ПО ФОРМЕ (не по результату), как у мини-статуса: число стека выводится прямым
//     цветом обычного `UTextBlock`, а не разметкой — вид тот же, но нет подмены виджета на
//     `URichTextBlock` каждый кадр.
#if !UE_BUILD_SHIPPING
// Видна ли иконка слота (самотест: значок яда/крови под полосой HP цели — путём игрока, по виджету окна цели).
bool UAsdaTargetFrameSubsystem::DebugEnemyStateIconShown(int32 Slot) const
{
	if (!EnemySlots.IsValidIndex(Slot) || !EnemySlots[Slot].Icon) { return false; }
	const ESlateVisibility V = EnemySlots[Slot].Icon->GetVisibility();
	return V != ESlateVisibility::Collapsed && V != ESlateVisibility::Hidden;
}
#endif

void UAsdaTargetFrameSubsystem::RefreshEnemyStateIcons(AActor* Target)
{
	if (EnemySlots.Num() != 28) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaEffectsSubsystem* Fx = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	if (!Fx) { return; }

	// ★ Признак берём у актора, а НЕ из `CurRenderId >= 0`: у запечённого превью-моба renderId
	//   равен −1, и вывод «renderId нет ⇒ это игрок» отправил бы запрос в список игроков.
	const bool bMob = (CurRenderId >= 0) && bTargetIsMob;
	const TArray<FAsdaEffectEntry>& List = bMob ? Fx->GetMobList(CurRenderId)
	                                            : (CurUserIdx >= 0 ? Fx->GetList(CurUserIdx)
	                                                               : Fx->GetList(INDEX_NONE));

	// Быстрый выход: набор не изменился — не трогаем виджеты (эталон тоже держит кэш id/типов).
	{
		int32 Live = 0;
		bool bSame = true;
		for (const FAsdaEffectEntry& E : List)
		{
			if (Live >= 28) { break; }
			if (!bMob && E.Type == 2) { continue; }
			if (bMob && E.Type != 0)  { continue; }
			if (EnemySlots[Live].BuffId != E.Id || EnemySlots[Live].BuffType != E.Type || EnemySlots[Live].StackNum != E.Stack) { bSame = false; break; }
			++Live;
		}
		if (bSame && (Live >= 28 || EnemySlots[Live].BuffId == -1)) { return; }
	}

	ClearEnemyStateIcons();
	int32 Slot = 0;
	for (const FAsdaEffectEntry& E : List)
	{
		if (Slot >= 28) { break; }                       // эталон: лишние состояния не показываются
		if (!bMob && E.Type == 2) { continue; }          // у игрока: `if (buff[20] != 2)`
		if (bMob && E.Type != 0)  { continue; }          // у моба рисует только ветка type 0 (ui_core.c:77040-77050)
		FString Name, Atlas, Text; int32 Cell = 0;
		// ★ 04.11.2026 (status_effect_expiry) [ОРИГИНАЛ]: у игрока-цели type 1 (соулмейт) — иконка из soulstateinfotip
		//   (`partyFindMemberSlotByIndex` — МИСНЕЙМ, ui_core.c:77236-77246), type 0 — stateinfotip (`mapFindEntryById`). Порт брал
		//   stateinfotip для обоих: соул-состояние цели рисовалось чужой иконкой (по номеру слота). Число стека — только у type 0.
		const bool bSoul = (!bMob && E.Type == 1);
		if (!(bSoul ? Fx->GetSoulStateInfo(E.Id, Name, Atlas, Cell, Text) : Fx->GetStateInfo(E.Id, Name, Atlas, Cell, Text))) { continue; }
		FAsdaEnemyStateSlot& S = EnemySlots[Slot];
		// ★ Дедуп №2, В1-B (U4a): клетка атласа состояний — общая точка `AsdaUi::ApplyAtlasCellIcon` (24 px, 10 колонок).
		if (!S.Icon || !AsdaUi::ApplyAtlasCellIcon(S.Icon, Atlas, Cell, kStateIconPx, kStateIconCols)) { continue; }
		SetVis(S.Icon, true);
		S.BuffId = E.Id; S.BuffType = E.Type; S.StackNum = E.Stack;
		// Число стека — только 2..30 (эталон: `if (stack >= 2u && stack <= 0x1Eu)`), жёлтым.
		if (S.Stack && !bSoul && E.Stack >= 2 && E.Stack <= 30)
		{
			S.Stack->SetText(FText::AsNumber(E.Stack));
			S.Stack->SetColorAndOpacity(FSlateColor(FLinearColor::FromSRGBColor(FColor(0xFF, 0xFF, 0x00, 0xFF))));
		}
		++Slot;
	}
	SetVis(EnemyStateGroup, true);
}

// ══════════════════════════════ МИГАНИЕ РАМКИ «ЦЕЛЬ В БОЮ» ═════════════════════════════════════

// Порт `hudRefreshControlColorFromCount @0xE7F9E0` (ui_core.c:77345-77356):
//     alpha = текущая альфа контрола;  alpha <= 0 ? SetColor4(255) : SetColor4(alpha − 13)
//   То есть пила 255 → 242 → … → ≤0 → 255, шаг 13 за вызов (вызов покадровый).
//   Гейт эталона — флаг `hud[690]` («моб бьёт первым»), из-за которого мигал невидимый контрол
//   (дефект Д3). У нас гейт — «рамка сейчас показана»; откат: asda.Target.LegacyBeingAttack 1.
void UAsdaTargetFrameSubsystem::TickBeingAttackBlink()
{
	if (!BeingAttack) { return; }
	const bool bLegacy = CVarLegacyBeingAttack.GetValueOnAnyThread() != 0;
	const bool bGate = bLegacy
		? (BatFirstMark && BatFirstMark->GetVisibility() != ESlateVisibility::Collapsed)
		: bBeingAttackOn;
	if (!bGate)
	{
		if (!FMath::IsNearlyEqual(BlinkAlpha, 255.f))
		{
			BlinkAlpha = 255.f;
			BeingAttack->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 1.f));
		}
		return;
	}
	BlinkAlpha -= kBlinkStep;
	if (BlinkAlpha <= 0.f) { BlinkAlpha = 255.f; }
	BeingAttack->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, BlinkAlpha / 255.f));
}

// ══════════════════════════════ ДАМП ═══════════════════════════════════════════════════════════

FString UAsdaTargetFrameSubsystem::DumpState() const
{
	FString S;
	S += TEXT("── ПАНЕЛЬ ЦЕЛИ (JUI PVP_TARGET, idnum 40; разбор Docs/UI/HUD/PVP_TARGET.md) ──\n");
	S += FString::Printf(TEXT("  окно: %s   контролы: %s   видимо: %s   центрирование: %s\n"),
		Win ? TEXT("создано") : TEXT("НЕТ"), bControlsReady ? TEXT("связаны") : TEXT("НЕТ"),
		bFrameShown ? TEXT("да") : TEXT("нет"),
		CVarCenterByPanel.GetValueOnAnyThread() ? TEXT("по панели PVP_TARGET00") : TEXT("анкер .JUI 0.4"));

	const AActor* T = CurTarget.Get();
	S += FString::Printf(TEXT("  цель: %s   userNum=%d   renderId=%d   сегментов=%d   HP=%d/%d   уровень=%d   имя='%s'\n"),
		T ? *T->GetName() : TEXT("нет"), CurUserIdx, CurRenderId, CurSegments,
		LastHpCur, LastHpMax, LastLevel, *LastName);

	S += TEXT("  ── СОСТАВ ОКНА (из ассета, ни одного выдуманного узла) ──\n");
	S += FString::Printf(TEXT("    база PVP_TARGET00      : группа=%s фон=%s\n"), *NodeTag(BaseGroup), *NodeTag(BaseImage));
	S += FString::Printf(TEXT("    кольца рангов          : Normal=%s Named=%s Elite=%s Boss=%s\n"),
		*NodeTag(RingNormal), *NodeTag(RingNamed), *NodeTag(RingElite), *NodeTag(RingBoss));
	S += FString::Printf(TEXT("    аним-оверлеи колец     : %s / %s / %s / %s (декор, idnum −1)\n"),
		*NodeTag(RingNormalAni), *NodeTag(RingNamedAni), *NodeTag(RingEliteAni), *NodeTag(RingBossAni));
	S += FString::Printf(TEXT("    уровень                : якорь=%s текст(скрыт)=%s прототипов=%d слотов=%d ")
		TEXT("(protoX=%.0f protoY=%.0f protoW=%.0f baseW=%.0f)\n"),
		*NodeTag(LvBaseGroup), *NodeTag(LvText), DigitProto.Num(), DigitSlot.Num(),
		DigitProtoX, DigitProtoY, DigitProtoW, LvBaseW);
	S += FString::Printf(TEXT("    имя                    : %s\n"), *NodeTag(NameText));
	for (int32 i = 0; i < HpBar.Num(); ++i)
	{
		S += FString::Printf(TEXT("    HP-полоса[%d]           : заливка=%s подложка=%s ширина=%.0f левый=%.0f\n"),
			i, *NodeTag(HpBar[i].Fill), *NodeTag(HpBar[i].Back), HpBar[i].FullW, HpBar[i].BaseLeft);
	}
	S += FString::Printf(TEXT("    MP                     : полоса=%s скрыватель=%s (эталон: обновлений MP цели НЕТ)\n"),
		*NodeTag(MpBar.Fill), *NodeTag(MpBarHide));
	S += FString::Printf(TEXT("    слои-оверлеи           : HPBuff=%s (ЧАСТИЧНО: логики в эталоне нет) MPBuff=%s (декор)\n"),
		*NodeTag(HpBuff), *NodeTag(MpBuff));
	S += FString::Printf(TEXT("    портрет / рамка боя    : %s / %s (показана=%s, альфа=%.0f) первый-удар=%s\n"),
		*NodeTag(Portrait), *NodeTag(BeingAttack), bBeingAttackOn ? TEXT("да") : TEXT("нет"),
		BlinkAlpha, *NodeTag(BatFirstMark));
	{
		FString Marks;
		for (int32 i = 0; i < EleMark.Num(); ++i)
		{
			Marks += FString::Printf(TEXT("%s%s"), i ? TEXT(" ") : TEXT(""), *NodeTag(EleMark[i]));
		}
		S += FString::Printf(TEXT("    стихии (подложка=%s)   : %s\n"), *NodeTag(EleBaseGroup), *Marks);
	}
	S += FString::Printf(TEXT("    эмблема клана          : база=%s метка=%s\n"), *NodeTag(ClanGroup), *NodeTag(ClanMark));
	{
		int32 Bound = 0, Shown = 0;
		FString Live;
		for (int32 i = 0; i < EnemySlots.Num(); ++i)
		{
			if (EnemySlots[i].Icon) { ++Bound; }
			if (EnemySlots[i].BuffId >= 0)
			{
				++Shown;
				Live += FString::Printf(TEXT(" [%d]=%d(x%d)"), i, EnemySlots[i].BuffId, EnemySlots[i].StackNum);
			}
		}
		S += FString::Printf(TEXT("    состояния врага        : контейнер=%s связано %d/28, занято %d:%s\n"),
			*NodeTag(EnemyStateGroup), Bound, Shown, Live.IsEmpty() ? TEXT(" —") : *Live);
	}
	S += TEXT("  ── КЛЮЧИ ОТКАТА (каждое отступление от эталона откатывается отдельно) ──\n");
	S += FString::Printf(TEXT("    asda.Target.LegacyEleBase=%d      Д1: подложка стихии (в .JUI Visible=0 ⇒ метка не видна)\n"),
		CVarLegacyEleBase.GetValueOnAnyThread());
	S += FString::Printf(TEXT("    asda.Target.LegacyBeingAttack=%d  Д2/Д3: гейт ent+474 без писателя + мигание невидимого\n"),
		CVarLegacyBeingAttack.GetValueOnAnyThread());
	S += FString::Printf(TEXT("    asda.Target.LegacyLevelColor=%d   Д4: цвет уровня уходил в скрытый PVP_LVTXT\n"),
		CVarLegacyLevelColor.GetValueOnAnyThread());
	S += FString::Printf(TEXT("    asda.Target.LegacyElementMark=%d  вечный «Огонь» у цели-игрока (bestIdx стартует с 0)\n"),
		CVarLegacyElementMark.GetValueOnAnyThread());
	S += FString::Printf(TEXT("    asda.Target.CenterByPanel=%d      центрирование по ширине базы (у эталона нет читателя)\n"),
		CVarCenterByPanel.GetValueOnAnyThread());
	S += FString::Printf(TEXT("    asda.Target.ShowMp=%d             полоса MP цели (в эталоне не обновляется ни разу)\n"),
		CVarShowTargetMp.GetValueOnAnyThread());
	return S;
}

// ══════════════════ САМОТЕСТ: ЗНАЧЕНИЕ СНИМАЕТСЯ С САМОЙ ШКАЛЫ ПОСЛЕ ОТРИСОВКИ ═════════════════
//   Почему не печатать `LastHpCur/LastHpMax`: они показывают, ЧТО КОД ПОЛОЖИЛ, а искали мы обрыв
//   именно между «значение есть» и «шкала его показывает». Поэтому берём `GetCachedGeometry()`
//   плана-заливки (её заполняет Slate ПОСЛЕ прохода раскладки) и считаем долю от полной ширины.
FString UAsdaTargetFrameSubsystem::SelfTest() const
{
	FString S = TEXT("=== asda.Target.SelfTest — ШКАЛА ЖИЗНИ ЦЕЛИ ПОСЛЕ ОТРИСОВКИ ===\n");
	const AActor* T = CurTarget.Get();
	S += FString::Printf(TEXT("  цель: %s   userNum=%d   моб=%s   окно видимо=%s\n"),
		T ? *T->GetName() : TEXT("НЕТ"), CurUserIdx, bTargetIsMob ? TEXT("да") : TEXT("нет"),
		bFrameShown ? TEXT("да") : TEXT("нет"));

	// Источник значений — единый реестр витальных значений (тот же, что кормит плашки и HUD).
	UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!bTargetIsMob && CurUserIdx >= 0)
	{
		const UAsdaNetSubsystem::FAsdaVitals* V = Net ? Net->GetVitals(CurUserIdx) : nullptr;
		S += FString::Printf(TEXT("  реестр витальных значений: %s   кэш 6538: charId=%d userNum=%d\n"),
			V ? *FString::Printf(TEXT("HP %d/%d, MP %d/%d, канал «%s»"), V->HpCur, V->HpMax, V->MpCur, V->MpMax, *V->LastHpSrc)
			  : TEXT("ЗАПИСИ НЕТ (6539 не приходил)"),
			TargetReqCharId, TargetReqUserNum);
	}

	for (int32 i = 0; i < HpBar.Num(); ++i)
	{
		const UImage* Fill = HpBar[i].Fill;
		if (!Fill) { S += FString::Printf(TEXT("  HP-полоса[%d]: узла нет\n"), i); continue; }
		const FGeometry& G = Fill->GetCachedGeometry();
		const FVector2D Sz = FVector2D(G.GetLocalSize());
		const FVector2D Pos = FVector2D(G.GetAbsolutePosition());
		const bool bVis = Fill->IsVisible();
		const float FracScreen = (HpBar[i].FullW > 0.f) ? (float)(Sz.X / HpBar[i].FullW) : -1.f;
		S += FString::Printf(
			TEXT("  HP-полоса[%d] %s: видима=%s  экран поз(%.0f,%.0f) разм(%.1f×%.1f)  ")
			TEXT("полная ширина=%.1f  ДОЛЯ ПО ЭКРАНУ=%.3f\n"),
			i, *NodeTag(Fill), bVis ? TEXT("да") : TEXT("нет"), Pos.X, Pos.Y, Sz.X, Sz.Y,
			HpBar[i].FullW, FracScreen);
	}
	if (HpNumbers)
	{
		S += FString::Printf(TEXT("  цифры над полосой: видимы=%s текст «%s»\n"),
			HpNumbers->IsVisible() ? TEXT("да") : TEXT("нет"), *HpNumbers->GetText().ToString());
	}
	if (MpBar.Fill)
	{
		const FVector2D MSz = FVector2D(MpBar.Fill->GetCachedGeometry().GetLocalSize());
		S += FString::Printf(TEXT("  MP-полоса %s: видима=%s разм(%.1f×%.1f) доля=%.3f  скрыватель видим=%s ")
			TEXT("(ключ asda.Target.ShowMp=%d)\n"),
			*NodeTag(MpBar.Fill), MpBar.Fill->IsVisible() ? TEXT("да") : TEXT("нет"), MSz.X, MSz.Y,
			MpBar.FullW > 0.f ? (float)(MSz.X / MpBar.FullW) : -1.f,
			(MpBarHide && MpBarHide->IsVisible()) ? TEXT("да") : TEXT("нет"),
			CVarShowTargetMp.GetValueOnAnyThread());
	}
	// Уровень: спрайты цифр — тоже фактическая геометрия, а не «что задумано».
	{
		FString D;
		for (int32 i = 0; i < DigitSlot.Num(); ++i)
		{
			const UImage* Im = DigitSlot[i];
			const FVector2D DSz = Im ? FVector2D(Im->GetCachedGeometry().GetLocalSize()) : FVector2D::ZeroVector;
			D += FString::Printf(TEXT(" [%d]%s(%.0f×%.0f)"), i,
				(Im && Im->IsVisible()) ? TEXT("вкл") : TEXT("выкл"), DSz.X, DSz.Y);
		}
		S += FString::Printf(TEXT("  уровень=%d, спрайты цифр:%s\n"), LastLevel, *D);
	}
	S += FString::Printf(TEXT("  имя «%s»: узел %s видим=%s\n"), *LastName, *NodeTag(NameText),
		(NameText && NameText->IsVisible()) ? TEXT("да") : TEXT("нет"));
	return S;
}

// ══════════════════════════════ КОНСОЛЬ ════════════════════════════════════════════════════════

static UAsdaTargetFrameSubsystem* TargetFrameOf(UWorld* W)
{
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UAsdaTargetFrameSubsystem>() : nullptr;
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GTargetDump(
	TEXT("asda.Target.Dump"),
	TEXT("Панель цели: полный состав окна PVP_TARGET и текущее состояние (что за цель, HP, уровень, ключи отката)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaTargetFrameSubsystem* S = TargetFrameOf(W))
		{
			// Без цели тик окно не поднимает — поднимаем сами (скрытым), иначе дамп напечатал бы
			//   «окна нет» и это было бы ложное «нет» (закон false-negatives-method).
			S->EnsureForDump();
			UE_LOG(LogAsdaTarget, Display, TEXT("%s"), *S->DumpState());
		}
	}));
#endif // ASDA_DEV_CONSOLE

// ★★★ ЖИВАЯ ПРОВЕРКА ЦЕЛИ-ИГРОКА БЕЗ ВТОРОГО ИГРОКА (закон false-negatives-method: без этих трёх
//   команд «HP цели» проверялось бы только на живом сервере, и дефект 6538 снова остался бы виден
//   лишь глазами владельца). Каждая команда идёт ТЕМ ЖЕ путём, что и игра:
//     `asda.Target.Set <userNum>`   — `AAsdaPlayerController::SetTarget`, вход клика мышью;
//     `asda.Target.FeedHp …`        — собирает НАСТОЯЩЕЕ тело 6539 (17 б) и отдаёт его штатному
//                                     приёмнику `UAsdaNetSubsystem::DebugFeedPacket`; копии
//                                     разбора здесь нет;
//     `asda.Target.SelfTest`        — снимает значение С САМОЙ ШКАЛЫ после отрисовки.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GTargetFakePeer(
	TEXT("asda.Target.FakePeer"),
	TEXT("asda.Target.FakePeer <userNum> [имя] — поставить рядом соседа-игрока без сервера ")
	TEXT("(общая точка UAsdaWorldSubsystem::DebugSpawnFakePeer = путь снапшота 4005/4065)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: фальшивый сосед с charId 900000+N — выбор целью отправил бы 6538 с поддельным charId.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Target.FakePeer"))) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		if (!WS || !PC || A.Num() < 1) { UE_LOG(LogAsdaTarget, Warning, TEXT("asda.Target.FakePeer <userNum> [имя]")); return; }
		FVector2D Base(0.f, 0.f);
		if (const AAsdaPlayerCharacter* Me = Cast<AAsdaPlayerCharacter>(PC->GetPawn())) { Base = Me->GetGamePosXZ(); }
		WS->DebugSpawnFakePeer(FCString::Atoi(*A[0]), A.Num() > 1 ? A[1] : FString(), Base + FVector2D(200.f, 0.f));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GTargetSet(
	TEXT("asda.Target.Set"),
	TEXT("asda.Target.Set <userNum> — выбрать целью игрока с этим userNum (тот же вход, что у клика)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
		if (!WS || !PC || A.Num() < 1) { UE_LOG(LogAsdaTarget, Warning, TEXT("asda.Target.Set <userNum>")); return; }
		const int32 U = FCString::Atoi(*A[0]);
		AActor* Pl = WS->GetPlayerActorByUserNum(U);
		if (!Pl) { UE_LOG(LogAsdaTarget, Warning, TEXT("[панель цели] игрока userNum=%d в мире нет"), U); return; }
		PC->SetTarget(Pl);
		UE_LOG(LogAsdaTarget, Display, TEXT("[панель цели] цель = игрок userNum=%d «%s» (charId=%d)"),
			U, *Pl->GetName(), WS->GetNetPlayerCharId(U));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GTargetFeedHp(
	TEXT("asda.Target.FeedHp"),
	TEXT("asda.Target.FeedHp <userNum> <curHp> <maxHp> [curMp] [maxMp] [задержкаСек] — подать ")
	TEXT("НАСТОЯЩИЙ пакет 6539 (ответ сервера на наш 6538) по этому игроку: [0]=1, [1..4] charId, ")
	TEXT("[5..8] maxHp, [9..12] curHp, [13..14] maxMp, [15..16] curMp. Разбирает штатный приёмник. ")
	TEXT("Задержка нужна, чтобы проверить ПОВТОРНЫЙ приход HP по уже выбранной цели."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaNetSubsystem*   Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!WS || !Net || A.Num() < 3)
		{
			UE_LOG(LogAsdaTarget, Warning, TEXT("asda.Target.FeedHp <userNum> <curHp> <maxHp> [curMp] [maxMp]"));
			return;
		}
		const int32 U = FCString::Atoi(*A[0]);
		const int32 CurHp = FCString::Atoi(*A[1]);
		const int32 MaxHp = FCString::Atoi(*A[2]);
		const int32 CurMp = (A.Num() > 3) ? FCString::Atoi(*A[3]) : 0;
		const int32 MaxMp = (A.Num() > 4) ? FCString::Atoi(*A[4]) : 0;
		const float Delay = (A.Num() > 5) ? FCString::Atof(*A[5]) : 0.f;
		const int32 CharId = WS->GetNetPlayerCharId(U);
		if (CharId == 0)
		{
			UE_LOG(LogAsdaTarget, Warning, TEXT("[панель цели] у userNum=%d нет charId — 6539 адресовать некому"), U);
			return;
		}
		TArray<uint8> B; B.SetNumZeroed(17);
		B[0] = 1;                                                   // подкод (эталон применяет 1 и 3)
		FMemory::Memcpy(&B[1],  &CharId, 4);
		FMemory::Memcpy(&B[5],  &MaxHp,  4);
		FMemory::Memcpy(&B[9],  &CurHp,  4);
		const uint16 MaxMp16 = (uint16)FMath::Clamp(MaxMp, 0, 65535);
		const uint16 CurMp16 = (uint16)FMath::Clamp(CurMp, 0, 65535);
		FMemory::Memcpy(&B[13], &MaxMp16, 2);
		FMemory::Memcpy(&B[15], &CurMp16, 2);
		auto Feed = [Net, B]() { Net->DebugFeedPacket(6539, B); };
		if (Delay <= 0.f) { Feed(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Feed), Delay, false);
		UE_LOG(LogAsdaTarget, Display, TEXT("[панель цели] 6539 по userNum=%d уйдёт через %.1f с"), U, Delay);
	}));
#endif // ASDA_DEV_CONSOLE

// ★★★ ЗАДЕРЖКА ОБЯЗАТЕЛЬНА: `-ExecCmds` отрабатывает все команды в ОДНОМ кадре, ДО первой
//   отрисовки, и `GetCachedGeometry` тогда ещё нулевая — самотест напечатал бы «0×0» и соврал.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GTargetSelfTest(
	TEXT("asda.Target.SelfTest"),
	TEXT("asda.Target.SelfTest [задержкаСек] [quit] — ФАКТИЧЕСКАЯ доля заполнения шкалы жизни цели ")
	TEXT("после отрисовки (снимается с геометрии плана-заливки, а не из полей кода)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		const float Delay = FAsdaSelfTestArgs::Parse(A).Float(0, 0.f);   // ★ 11.10.2026 (В3 S1): разбор каркаса — `quit` первым аргументом не читается как задержка
		const bool bQuit = A.ContainsByPredicate([](const FString& S) { return S.Equals(TEXT("quit"), ESearchCase::IgnoreCase); });
		auto Run = [W, bQuit]()
		{
			if (UAsdaTargetFrameSubsystem* S = TargetFrameOf(W))
			{
				UE_LOG(LogAsdaTarget, Display, TEXT("%s"), *S->SelfTest());
			}
			else { UE_LOG(LogAsdaTarget, Warning, TEXT("подсистема панели цели не поднята")); }
			if (bQuit && GEngine) { GEngine->DeferredCommands.Add(TEXT("quit")); }
		};
		if (Delay <= 0.f || !W) { Run(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Run), Delay, false);
		UE_LOG(LogAsdaTarget, Display, TEXT("[панель цели] самотест снимет шкалу через %.1f с%s"),
			Delay, bQuit ? TEXT(" и закроет игру") : TEXT(""));
	}));
#endif // ASDA_DEV_CONSOLE

// ★ ПОЛНЫЙ обход дерева окна: каждый узел с классом, видимостью и живой экранной геометрией.
//   Нужен по закону full-structural-analysis-always: «контролы находятся по именам» доказывается
//   не списком ожиданий, а фактическим составом. Python-биндингов UMG в headless-прогоне нет
//   (`unreal.WidgetBlueprintLibrary` отсутствует), поэтому дерево печатает сам движок.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GTargetNodes(
	TEXT("asda.Target.Nodes"),
	TEXT("Панель цели: напечатать ВСЕ узлы окна PVP_TARGET (класс, видимость, экранная геометрия). ")
	TEXT("Аргумент `vis` — только фактически видимые."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaTargetFrameSubsystem* S = TargetFrameOf(W);
		if (!S) { return; }
		S->EnsureForDump();
		UAsdaWindowSubsystem* WM = W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
		UUserWidget* Win = WM ? WM->FindWindow(FName(TEXT("PVP_TARGET"))) : nullptr;
		if (!Win || !Win->WidgetTree)
		{
			UE_LOG(LogAsdaTarget, Warning, TEXT("[панель цели] окно не поднято — печатать нечего"));
			return;
		}
		const bool bOnlyVisible = A.Num() > 0 && A[0].Equals(TEXT("vis"), ESearchCase::IgnoreCase);
		int32 Total = 0, Visible = 0;
		UE_LOG(LogAsdaTarget, Display, TEXT("── УЗЛЫ ОКНА PVP_TARGET (%s) ──"),
			bOnlyVisible ? TEXT("только видимые") : TEXT("все"));
		Win->WidgetTree->ForEachWidget([&](UWidget* N)
		{
			if (!N) { return; }
			++Total;
			const bool bVis = AsdaUi::IsShown(N);   // ★ волна В6 ремесла (МЕЛК-8): общая точка видимости узла
			if (bVis) { ++Visible; }
			if (bOnlyVisible && !bVis) { return; }
			const FGeometry& G = N->GetCachedGeometry();
			const FVector2D Abs = G.GetAbsolutePosition();
			const FVector2D Sz  = G.GetLocalSize();
			FString Extra;
			if (const UImage* Img = Cast<UImage>(N))
			{
				const UObject* Res = Img->GetBrush().GetResourceObject();
				Extra = FString::Printf(TEXT(" браш=%s"), Res ? *Res->GetName() : TEXT("НЕТ"));
			}
			else if (const UTextBlock* Tb = Cast<UTextBlock>(N))
			{
				Extra = FString::Printf(TEXT(" текст='%s'"), *Tb->GetText().ToString());
			}
			UE_LOG(LogAsdaTarget, Display, TEXT("  %-34s %-20s vis=%-22s экран=(%.0f,%.0f) размер=(%.0f×%.0f)%s"),
				*N->GetName(), *N->GetClass()->GetName(),
				*UEnum::GetValueAsString(N->GetVisibility()), Abs.X, Abs.Y, Sz.X, Sz.Y, *Extra);
		});
		UE_LOG(LogAsdaTarget, Display, TEXT("── ИТОГО узлов: %d, фактически видимых: %d ──"), Total, Visible);
	}));
#endif // ASDA_DEV_CONSOLE

// ★ Выбрать целью БЛИЖАЙШЕГО моба уровня — единственный способ увидеть заполненную панель без
//   сервера. Это НЕ выдуманная цель: берётся реальный актор с реальными полями типа (уровень,
//   агрессия, стихия, портрет, «жизни»), просто выбор идёт не мышью, а командой.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GTargetPick(
	TEXT("asda.Target.Pick"),
	TEXT("Панель цели: выбрать целью ближайшего моба уровня — asda.Target.Pick [радиус, см = 100000]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (!W) { return; }
		AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W->GetFirstPlayerController());
		if (!PC) { UE_LOG(LogAsdaTarget, Warning, TEXT("[панель цели] контроллера нет")); return; }
		const float R = (A.Num() > 0) ? FCString::Atof(*A[0]) : 100000.f;
		const FVector From = PC->GetPawn() ? PC->GetPawn()->GetActorLocation() : FVector::ZeroVector;
		AAsdaMobActor* Best = nullptr;
		float BestD = R;
		for (TActorIterator<AAsdaMobActor> It(W); It; ++It)
		{
			AAsdaMobActor* M = *It;
			if (!IsValid(M) || M->IsDead() || M->bServerDead) { continue; }
			const float D = (float)FVector::Dist(From, M->GetActorLocation());
			if (D < BestD) { BestD = D; Best = M; }
		}
		if (!Best)
		{
			UE_LOG(LogAsdaTarget, Warning, TEXT("[панель цели] мобов в радиусе %.0f см не найдено"), R);
			return;
		}
		PC->SetTarget(Best);
		UE_LOG(LogAsdaTarget, Display,
			TEXT("[панель цели] целью выбран моб '%s' id=%d уровень=%d агрессия/ранг=%d стихия=%d ")
			TEXT("жизней=%d HP=%d/%d портрет=%s#%d (дистанция %.0f см)"),
			*Best->MobName, Best->MobId, Best->MobLevel, Best->MobAggression, Best->MobRank,
			Best->MobLives, Best->CurHp, Best->MaxHp, *Best->MobPortraitTex, Best->MobPortraitCell, BestD);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GTargetRefresh(
	TEXT("asda.Target.Refresh"),
	TEXT("Панель цели: пересобрать панель под текущую цель с нуля (после смены данных цели/ключей отката)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaTargetFrameSubsystem* S = TargetFrameOf(W))
		{
			S->ForceRefresh();
			UE_LOG(LogAsdaTarget, Display, TEXT("[панель цели] пересбор запрошен — применится ближайшим тиком"));
		}
	}));
#endif // ASDA_DEV_CONSOLE
