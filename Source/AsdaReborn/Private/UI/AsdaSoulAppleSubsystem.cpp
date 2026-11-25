// Copyright (c) 2026 @sitxovski (Discord)
// ═══════════════════════════════════════════════════════════════════════════════════════════════
// Порт «Яблока Дружбы» (панель 6689…6699 окна соулмейта 6500). Реверс, поток всех веток, таблица
//   нотисов и оговорки — в шапке `Public/UI/AsdaSoulAppleSubsystem.h`. [ПОРТ] волна 1-Б, 06.10.2026.
#include "UI/AsdaSoulAppleSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaUiHelpers.h"   // ★ U3 (дедуп №2, В2-A): AsdaUi::SetShownDecor / SetShownNoHit

#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "AsdaOutgoingGate.h"                 // RefuseDebugWhenOnline — офлайн-гейт отладочной подачи 6049/6051
#include "UI/AsdaAnimatedPlane.h"             // кадровая анимация плана: прогоны и длительность из скина
#include "UI/AsdaItemTooltipSubsystem.h"      // общая плашка g_ItemTooltip (ShowTextTip / HideTooltipFor)
#include "UI/AsdaTipPanel.h"                  // EAsdaTipOwner::SoulApple, FAsdaTipMetrics
#include "UI/AsdaNotice.h"                    // LocSlot / ChatColored / PlankLoc
#include "UI/AsdaSoulRouletteSubsystem.h"     // Spin() — 6575 «Золотого яблока»
#include "UI/AsdaWindowSubsystem.h"           // FindWindow("SOULMATE_POP_WIN")
#include "UI/AsdaSkillUseSubsystem.h"         // ★ ревизия М-1: HasSoulmate() = gap_878[47] != −1 (гейт подсказки)
#include "UI/AsdaEffectsSubsystem.h"          // SpawnItemUseFx — эффект «съел яблоко» на персонаже
#include "Resources/AsdaSoundSubsystem.h"     // PlaySound3D — S_c_SmExpGiftApple.wav
#include "World/AsdaWorldSubsystem.h"         // FindPlayerActorByCharId / GetUserNumByCharId
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "AsdaSelf.h"                      // ★ Дедуп №2, В1-B (U5a): «это я?» — `AsdaSelf::IsSelfUserIdx`
#include "UI/AsdaNoticeColors.h"   // ★ R5-c (12.11.2026): AsdaNoticeColor::Gold — золотой цвет нотисов (общая точка)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSoulApple, Log, All);

const FName UAsdaSoulAppleSubsystem::kPopWindowId(TEXT("SOULMATE_POP_WIN"));

namespace
{
	// ★ Имена узлов — по правилу импортёра JUI (`jui-importer-widget-naming`): группа с детьми — `{idc}_grp`,
	//   плоскость контрола — `{idc}_{индекс}_{план}` (индекс плывёт от правки разметки → ищем РАЗБОРОМ имени).
	//   Сверено с дампом ассета `Reports/owner_1006/_wave1B_wbp_dump.txt` (строки 111-128).
	const TCHAR* kIdc[(int32)EAsdaSoulAppleNode::Count] =
	{
		TEXT("Soulmate_AppleBase"),    // группа: ищется как `Soulmate_AppleBase_grp`
		TEXT("Soulmate_Gear"),
		TEXT("Soulmate_AniGear"),
		TEXT("Soulmate_Apple"),
		TEXT("Soulmate_GoldenApple"),
		TEXT("Soulmate_AppleEffect"),
		TEXT("Soulmate_ShakeApple"),
		TEXT("Soulmate_AppEff1"),
		TEXT("Soulmate_AppEff2"),
		TEXT("Soulmate_AppEff3"),
	};

	// ★ Длительность прогона из СКИНА — страховка на случай, если у плана в сборке нет данных
	//   (`RE/docs/JuiSkin.ini`: Ani_Gear_Soulmate 4 кадра / 200 мс; Soulmate_ShakeApple 10 / 300;
	//   Apple_Make1 16 / 1000; Apple_Make2 16 / 1000; Apple_Eat 12 / 800). Основной путь — живой план.
	struct FSkinAnim { int32 TotalFrame; float IntervalMs; };
	FSkinAnim SkinOf(EAsdaSoulAppleNode N)
	{
		switch (N)
		{
		case EAsdaSoulAppleNode::AniGear: return { 4, 200.f };
		case EAsdaSoulAppleNode::Shake:   return { 10, 300.f };
		case EAsdaSoulAppleNode::Eff1:    return { 16, 1000.f };
		case EAsdaSoulAppleNode::Eff2:    return { 16, 1000.f };
		case EAsdaSoulAppleNode::Eff3:    return { 12, 800.f };
		case EAsdaSoulAppleNode::Golden:  return { 2, 400.f };
		default:                          return { 0, 0.f };
		}
	}

	// ★ Секунды ОДНОГО прогона ровно по формуле тика плана (`ui_core.c:108838-108860`, порт —
	//   `UAsdaAnimatedPlane::TickAnim`): кадр = floor(t / FrameInterval × (TotalFrame − 1)), прогон кончен,
	//   когда кадр > TotalFrame − 1, то есть при t ≥ FrameInterval × TotalFrame / (TotalFrame − 1).
	double PassSeconds(int32 TotalFrame, float IntervalMs)
	{
		if (IntervalMs <= 0.f) { return 0.0; }
		const double K = (TotalFrame > 1) ? (double)TotalFrame / (double)(TotalFrame - 1) : 1.0;
		return (double)IntervalMs * K / 1000.0;
	}

	// План контрола по разобранному имени (`AsdaUi::ParsePlaneName`) — подстрока здесь опасна:
	//   `Soulmate_Apple` — префикс и `Soulmate_AppleBase`, и `Soulmate_AppleEffect`.
	UImage* PlaneOfIdc(const UUserWidget* Host, const FString& Idc, int32 PlaneNo)
	{
		UWidgetTree* Tree = Host ? Host->WidgetTree : nullptr;
		if (!Tree) { return nullptr; }
		UImage* Found = nullptr;
		Tree->ForEachWidget([&Found, &Idc, PlaneNo](UWidget* W)
		{
			if (Found) { return; }
			UImage* Img = Cast<UImage>(W);
			if (!Img) { return; }
			FString ParsedIdc; int32 JuiIdx = 0, Plane = 0;
			if (!AsdaUi::ParsePlaneName(Img->GetName(), ParsedIdc, JuiIdx, Plane)) { return; }
			if (Plane == PlaneNo && ParsedIdc.Equals(Idc, ESearchCase::IgnoreCase)) { Found = Img; }
		});
		return Found;
	}

	const FColor kTipYellow = AsdaNoticeColor::Gold;   // −11514 — цвет 6051 (плашка и чат)   ★ R5-c (12.11.2026): значение — общая точка `AsdaNoticeColor::Gold` (было своё `FColor(0xFF, 0xD3, 0x06, 0xFF)`, те же байты).

	// ★ [ОРИГИНАЛ] Ревизия 1-Б, С-1: `inventoryHidePanelsAndRefresh@0xE65510` при ЖИВОЙ паре прячет группу 6691,
	//   а вернуть её может только `uiDlgShowExpandState` — то есть пакет 6049 с процентом 0 и следом ненулевой.
	//   После любого «закрыть всё» (гача, автобег, улучшение, продажа, переход по карте региона) панель яблока
	//   у живой пары пропадает до сброса яблока — дефект оригинала. Исправляем: группа возвращается в прежнюю
	//   видимость, как только окно 6500 снова на экране.
	ASDA_DEV_CVAR(int32, CVarFixCloseAllHide,
		TEXT("asda.Soul.Apple.FixCloseAllHide"), 1,
		TEXT("1 (по умолчанию) = после «закрыть всё»/снятия галки 6546 при живой паре панель «Яблока Дружбы» (6691)\n")
		TEXT("возвращается вместе с окном 6500.\n")
		TEXT("0 = как в оригинале: панель пропадает до сброса яблока (6049 с процентом 0 и следующий ненулевой)."),
		ECVF_Default);
}

// ═══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ════════════════════════════════════════════════

void UAsdaSoulAppleSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	Nodes.SetNum((int32)EAsdaSoulAppleNode::Count);
	// Вид по умолчанию = конструктор CSoulmateWnd (`social.c:6640-6648`): все флаги 0,
	//   `uiControl_ScaleCenterChildren(self, 50)`; видимость — как в .JUI (AppleBase 1, Apple 1, прочие 0).
	ResetSessionState(EAsdaSessionResetReason::LeftWorld);
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка сети только КЛАДЁМ, разбор — в Tick.
		HApple = Net->OnSoulApplePacket.AddWeakLambda(this, [this](int32 Op, const TArray<uint8>& B)
		{
			PendingIn.Push(TPair<int32, TArray<uint8>>(Op, B));
		});
	}
	UE_LOG(LogAsdaSoulApple, Log, TEXT("[яблоко] подсистема поднята: 6049/6051 → панель 6689…6699 окна 6500"));
}

void UAsdaSoulAppleSubsystem::Deinitialize()
{
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnSoulApplePacket.Remove(HApple);
	}
	Super::Deinitialize();
}

void UAsdaSoulAppleSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	PendingIn.Clear();
	Mouse.Reset();
	if (bTipShown)
	{
		if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
		{
			Tip->HideTooltipFor(EAsdaTipOwner::SoulApple);
		}
		bTipShown = false;
	}
	if (!AsdaIsFullSessionReset(Reason))
	{
		// Смена карты: состояние яблока у сервера то же, виджет 6500 будет новым — зеркало применится при привязке.
		bBound = false;
		return;
	}
	bShown = bStageGear = bStage34 = bStage67 = false;
	AppleExp = 0;
	bResetPending = false;
	EffStep = 0;
	bEffChain = false;
	bGoldenFlag = false;
	bBaseRestorePending = false;
	bScaleActive = false;
	ScaleCur = ScaleStep = 0.f; ScaleIntervalMs = ScaleMaxSteps = ScaleSteps = 0; ScaleNextDue = 0.0;
	AniGearEndS = ShakeEndS = EffPassEndS = 0.0;
	for (bool& V : Vis) { V = false; }
	Vis[(int32)EAsdaSoulAppleNode::AppleBase] = true;   // .JUI Visible="1"
	Vis[(int32)EAsdaSoulAppleNode::Apple]     = true;   // .JUI Visible="1"
	ApplePct = 50.f;                                    // конструктор: ScaleCenterChildren(50)
	bBound = false;                                     // применить к виджету заново
	Rx6049 = Rx6051 = Tx6575 = 0;
}

void UAsdaSoulAppleSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.SoulApple");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	if (TArray<TPair<int32, TArray<uint8>>> Batch; PendingIn.Drain(Batch))   // ★ U7 (В1 дедупа №2): общая очередь `TAsdaNetQueue` (замок + Drain) вместо своего массива
	{
		for (const TPair<int32, TArray<uint8>>& It : Batch)
		{
			if (It.Key == 6049)      { OnGrowth6049(It.Value); }
			else if (It.Key == 6051) { OnExpGift6051(It.Value); }
		}
	}
	EnsureBound();
	// ★ [ОРИГИНАЛ] Ревизия С-1 (ключ asda.Soul.Apple.FixCloseAllHide): окно 6500 снова на экране — вернуть 6691.
	if (bBaseRestorePending)
	{
		UAsdaWindowSubsystem* WMr = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (bBound && WMr && WMr->IsWindowVisible(kPopWindowId))
		{
			bBaseRestorePending = false;
			SetVis(EAsdaSoulAppleNode::AppleBase, bBaseVisBeforeHide);
			UE_LOG(LogAsdaSoulApple, Log, TEXT("[яблоко] окно 6500 снова видно — группа 6691 возвращена (%s) [исправление дефекта оригинала]"),
				bBaseVisBeforeHide ? TEXT("видна") : TEXT("скрыта, как была"));
		}
	}
	TickAnims();
	TickPointer();
}

// ═══════════════════════════════ ПРИВЯЗКА К ОКНУ 6500 ══════════════════════════════════════════

void UAsdaSoulAppleSubsystem::EnsureBound()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	UUserWidget* W = WM ? WM->FindWindow(kPopWindowId) : nullptr;
	if (!W)
	{
		if (bBound) { UE_LOG(LogAsdaSoulApple, Log, TEXT("[яблоко] окно 6500 исчезло — жду нового")); }
		bBound = false;
		Wnd.Reset();
		return;
	}
	if (bBound && Wnd.Get() == W) { return; }
	BindNodes(W);
	ApplyMirror();
}

void UAsdaSoulAppleSubsystem::BindNodes(UUserWidget* W)
{
	Wnd = W;
	Nodes.SetNum((int32)EAsdaSoulAppleNode::Count);
	int32 Found = 0;
	for (int32 i = 0; i < (int32)EAsdaSoulAppleNode::Count; ++i)
	{
		UWidget* N = nullptr;
		if (i == (int32)EAsdaSoulAppleNode::AppleBase)
		{
			N = AsdaUi::FindNodeExact(W, TEXT("Soulmate_AppleBase_grp"));
		}
		else
		{
			N = PlaneOfIdc(W, kIdc[i], 0);
		}
		Nodes[i] = N;
		if (N) { ++Found; }
		else
		{
			UE_LOG(LogAsdaSoulApple, Warning, TEXT("[яблоко] узел «%s» в окне 6500 не найден"), kIdc[i]);
		}
	}
	// Эталон: `m_AppleBaseSize = GetSize(m_pApple)`, `m_AppleBasePos = GetPos(m_pApple)` — снимаются ОДИН раз
	//   при привязке (social.c:6586-6597). У порта виджет пересоздаётся — снимаем с нового (там исходные значения ассета).
	AppleBasePos = FVector2D::ZeroVector;
	AppleBaseSize = FVector2D::ZeroVector;
	if (const UCanvasPanelSlot* S = AsdaUi::SlotOf(Nodes[(int32)EAsdaSoulAppleNode::Apple]))
	{
		const FMargin O = S->GetOffsets();
		AppleBasePos  = FVector2D(O.Left, O.Top);
		AppleBaseSize = FVector2D(O.Right, O.Bottom);
	}
	// ★ Ревизия М-3: якорь окна рулетки 6699 `Soulmate_Glass` (у него дети ⇒ группа `_grp`).
	GlassAnchor = AsdaUi::FindNodeExact(W, TEXT("Soulmate_Glass_grp"));
	bBound = (Found == (int32)EAsdaSoulAppleNode::Count) && AppleBaseSize.X > 0.f;
	UE_LOG(LogAsdaSoulApple, Log, TEXT("[яблоко] окно 6500 разобрано: узлов %d/%d, 6692 @(%.0f,%.0f) %.0f×%.0f"),
		Found, (int32)EAsdaSoulAppleNode::Count, AppleBasePos.X, AppleBasePos.Y, AppleBaseSize.X, AppleBaseSize.Y);
}

UWidget* UAsdaSoulAppleSubsystem::GetNodeWidget(EAsdaSoulAppleNode N) const
{
	const int32 i = (int32)N;
	return (bBound && Nodes.IsValidIndex(i)) ? Nodes[i].Get() : nullptr;
}

// Применить зеркало к (новому) виджету: видимость, масштаб, бесконечные анимации видимых эффектов.
void UAsdaSoulAppleSubsystem::ApplyMirror()
{
	if (!bBound) { return; }
	for (int32 i = 0; i < (int32)EAsdaSoulAppleNode::Count; ++i)
	{
		if (UWidget* N = Nodes[i])
		{
			// U3 (дедуп №2, В2-A, 11.10.2026): правило «показать без хит-теста» — общая `AsdaUi::SetShownNoHit` (вынос, поведение то же); группа AppleBase — декор с детьми (`SetShownDecor`).
			const bool bGroup = (i == (int32)EAsdaSoulAppleNode::AppleBase);
			if (bGroup) { AsdaUi::SetShownDecor(N, Vis[i]); } else { AsdaUi::SetShownNoHit(N, Vis[i]); }
		}
	}
	ApplyScale(ApplePct, /*bIntegerPct*/ !bScaleActive);
}

// ═══════════════════════════════ ВИЗУАЛЬНЫЕ ОПЕРАЦИИ ЭТАЛОНА ═══════════════════════════════════

void UAsdaSoulAppleSubsystem::SetVis(EAsdaSoulAppleNode N, bool bShow)
{
	const int32 i = (int32)N;
	Vis[i] = bShow;
	if (UWidget* W = GetNodeWidget(N))
	{
		// U3 (дедуп №2, В2-A, 11.10.2026): правило «показать без хит-теста» — общая `AsdaUi::SetShownNoHit` (вынос, поведение то же); группа AppleBase — декор с детьми (`SetShownDecor`).
		const bool bGroup = (N == EAsdaSoulAppleNode::AppleBase);
		if (bGroup) { AsdaUi::SetShownDecor(W, bShow); } else { AsdaUi::SetShownNoHit(W, bShow); }
	}
}

void UAsdaSoulAppleSubsystem::ScaleCenter(int32 Pct)
{
	// `uiControl_ScaleCenterChildren@0xE6E1A0`: v6 = pct·W/100, v5 = pct·H/100 (целые), позиция =
	//   base + (base − v)/2 — у 6692 И у 6695 одной и той же базой (база снята с 6692).
	ApplePct = (float)Pct;
	ApplyScale(ApplePct, /*bIntegerPct*/ true);
}

void UAsdaSoulAppleSubsystem::ApplyScale(float Pct, bool bIntegerPct)
{
	if (!bBound || AppleBaseSize.X <= 0.f) { return; }
	int32 W, H;
	if (bIntegerPct)
	{
		// Целочисленная ветка `uiControl_ScaleCenterChildren`: (pct * W) / 100 с усечением.
		const int32 P = FMath::RoundToInt(Pct);
		W = P * (int32)AppleBaseSize.X / 100;
		H = P * (int32)AppleBaseSize.Y / 100;
	}
	else
	{
		// Ветка анимации плана (`ui_core.c:108300`): scaled = (uint64)(W × cur / 100.0).
		W = (int32)((double)AppleBaseSize.X * (double)Pct / 100.0);
		H = (int32)((double)AppleBaseSize.Y * (double)Pct / 100.0);
	}
	const int32 X = (int32)AppleBasePos.X + ((int32)AppleBaseSize.X - W) / 2;
	const int32 Y = (int32)AppleBasePos.Y + ((int32)AppleBaseSize.Y - H) / 2;
	for (EAsdaSoulAppleNode N : { EAsdaSoulAppleNode::Apple, EAsdaSoulAppleNode::Shake })
	{
		if (UCanvasPanelSlot* S = AsdaUi::SlotOf(GetNodeWidget(N)))
		{
			S->SetOffsets(FMargin((float)X, (float)Y, (float)W, (float)H));
		}
	}
}

void UAsdaSoulAppleSubsystem::StartGrow(float StartPct, int32 IntervalMs, float StepPct, int32 MaxSteps)
{
	// `uiContainerConfigItemAndShow@0x12E11C0` + тик плана (`ui_core.c:108271-108310`): первый шаг — СРАЗУ
	//   (`start = now − interval`), далее каждые `interval` мс: если шагов уже `MaxSteps` — конец и событие 24
	//   (a7 = 3), иначе +шаг и пересчёт размера по центру. Контрол показывается, если был скрыт.
	bScaleActive    = true;
	ScaleCur        = StartPct;
	ScaleStep       = StepPct;
	ScaleIntervalMs = IntervalMs;
	ScaleMaxSteps   = MaxSteps;
	ScaleSteps      = 0;
	ScaleNextDue    = FPlatformTime::Seconds();
	if (!Vis[(int32)EAsdaSoulAppleNode::Apple]) { SetVis(EAsdaSoulAppleNode::Apple, true); }
}

double UAsdaSoulAppleSubsystem::ShowAnimLoops(EAsdaSoulAppleNode N, int32 Loops)
{
	// `ctrlShowWithItemFlags@0x12E1130`: Animate = 1, EFrameStop = 1, лимит прогонов = Loops, старт сейчас, показать.
	SetVis(N, true);
	const FSkinAnim Skin = SkinOf(N);
	int32 Tf = Skin.TotalFrame; float Fi = Skin.IntervalMs;
	if (UAsdaAnimatedPlane* P = Cast<UAsdaAnimatedPlane>(GetNodeWidget(N)))
	{
		P->bLoop = false;
		P->MaxLoops = Loops;
		P->RestartAnim();
		if (P->FrameIntervalMs > 0.f && P->TotalFrame > 0) { Tf = P->TotalFrame; Fi = P->FrameIntervalMs; }
	}
	return PassSeconds(Tf, Fi) * Loops;
}

double UAsdaSoulAppleSubsystem::ShowAnimRestart(EAsdaSoulAppleNode N)
{
	// `CControl_Show@0x12E1350`: Animate = 1, «перезапуск» = 1 (кадр с начала), EFrameStop НЕ ставится —
	//   анимация крутится, пока контрол не спрячут. Возвращаем длительность ОДНОГО прогона (для цепочки).
	SetVis(N, true);
	const FSkinAnim Skin = SkinOf(N);
	int32 Tf = Skin.TotalFrame; float Fi = Skin.IntervalMs;
	if (UAsdaAnimatedPlane* P = Cast<UAsdaAnimatedPlane>(GetNodeWidget(N)))
	{
		P->bLoop = true;
		P->MaxLoops = 0;
		P->RestartAnim();
		if (P->FrameIntervalMs > 0.f && P->TotalFrame > 0) { Tf = P->TotalFrame; Fi = P->FrameIntervalMs; }
	}
	return PassSeconds(Tf, Fi);
}

void UAsdaSoulAppleSubsystem::CloseHide(EAsdaSoulAppleNode N)
{
	// `CControl_CloseHide@0x12E1420`: Animate = 0, лимит 0, спрятать (если был виден).
	if (UAsdaAnimatedPlane* P = Cast<UAsdaAnimatedPlane>(GetNodeWidget(N))) { P->StopAnim(); }
	SetVis(N, false);
}

void UAsdaSoulAppleSubsystem::ShowExpandState()
{
	// `uiDlgShowExpandState@0xE6DD20` (game.c:475644-475656).
	SetVis(EAsdaSoulAppleNode::AppleBase, true);
	SetVis(EAsdaSoulAppleNode::Gear, true);
	ScaleCenter(50);
	SetVis(EAsdaSoulAppleNode::Apple, true);
	SetVis(EAsdaSoulAppleNode::Golden, false);
	SetVis(EAsdaSoulAppleNode::AppleEffect, false);
	SetVis(EAsdaSoulAppleNode::Shake, false);
	SetVis(EAsdaSoulAppleNode::Eff1, false);
	SetVis(EAsdaSoulAppleNode::Eff2, false);
	SetVis(EAsdaSoulAppleNode::Eff3, false);
	// `uiContainerConfigItemAndShow(apple, base…, 49.0f, 50.0f, 50, 1.0f, 1)`: старт 49 %, шаг +1 % через 50 мс, 1 шаг.
	StartGrow(49.f, 50, 1.f, 1);
}

void UAsdaSoulAppleSubsystem::ToggleShowAlt()
{
	// `uiToggleShowAlt@0xE6E0E0`: 6692 скрыть, 6695 показать и 3 прогона анимации.
	SetVis(EAsdaSoulAppleNode::Apple, false);
	ShakeEndS = FPlatformTime::Seconds() + ShowAnimLoops(EAsdaSoulAppleNode::Shake, 3);
}

void UAsdaSoulAppleSubsystem::ToggleChildVisible()
{
	// `uiToggleChildVisible168_171@0xE6E140`: 6692 показать, 6695 спрятать (CloseHide).
	SetVis(EAsdaSoulAppleNode::Apple, true);
	CloseHide(EAsdaSoulAppleNode::Shake);
	ShakeEndS = 0.0;
}

void UAsdaSoulAppleSubsystem::WindowShowAltView()
{
	// `uiWindowShowAltView@0xE6DF20`: 6692 скрыть, цепочка = 1, шаг = 0, `CControl_Show(6696)`.
	SetVis(EAsdaSoulAppleNode::Apple, false);
	bEffChain = true;
	EffStep = 0;
	EffPassEndS = FPlatformTime::Seconds() + ShowAnimRestart(EAsdaSoulAppleNode::Eff1);
}

void UAsdaSoulAppleSubsystem::WindowResetView()
{
	// `uiWindow_ResetView@0xE6DF80`: цепочка = 0, шаг = 0, масштаб 50, 6692 видно, 6693/6696/6697/6698 скрыть.
	bEffChain = false;
	EffStep = 0;
	EffPassEndS = 0.0;
	ScaleCenter(50);
	SetVis(EAsdaSoulAppleNode::Apple, true);
	SetVis(EAsdaSoulAppleNode::Golden, false);
	SetVis(EAsdaSoulAppleNode::Eff1, false);
	SetVis(EAsdaSoulAppleNode::Eff2, false);
	SetVis(EAsdaSoulAppleNode::Eff3, false);
}

void UAsdaSoulAppleSubsystem::PanelResetAndKillFocus()
{
	// `uiPanelResetAndKillFocus@0xE6DE50`: масштаб 50, группа 6691 скрыть, 181…184 и 186 = 0, снять подсказку.
	ScaleCenter(50);
	SetVis(EAsdaSoulAppleNode::AppleBase, false);
	bShown = bStageGear = bStage34 = bStage67 = false;
	bResetPending = false;
	if (bTipShown)
	{
		if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
		{
			Tip->HideTooltipFor(EAsdaTipOwner::SoulApple);
		}
		bTipShown = false;
	}
}

void UAsdaSoulAppleSubsystem::OnGrowAnimEnd()
{
	// `SoulmateUI_HandleEvent` (game.c:473580-473600): `a3 == 6692 && a7 == 3`.
	if (!bStageGear)
	{
		bStageGear = true;
		SetVis(EAsdaSoulAppleNode::Gear, false);
		AniGearEndS = FPlatformTime::Seconds() + ShowAnimLoops(EAsdaSoulAppleNode::AniGear, 3);
		ToggleShowAlt();
	}
	else if (bStage34 && !bStage67)
	{
		ScaleCenter(70);
	}
	else if (bStage34 && bStage67)
	{
		ScaleCenter(100);
	}
	UE_LOG(LogAsdaSoulApple, Log, TEXT("[яблоко] рост доигран (событие 24, a7 = 3): 182=%d 183=%d 184=%d, масштаб %.0f %%"),
		bStageGear ? 1 : 0, bStage34 ? 1 : 0, bStage67 ? 1 : 0, ApplePct);
}

void UAsdaSoulAppleSubsystem::TickAnims()
{
	const double Now = FPlatformTime::Seconds();

	// ── рост яблока (план 6692) ──
	if (bScaleActive && Now >= ScaleNextDue)
	{
		ScaleNextDue += (double)ScaleIntervalMs / 1000.0;
		if (ScaleSteps >= ScaleMaxSteps)
		{
			bScaleActive = false;
			OnGrowAnimEnd();
		}
		else
		{
			++ScaleSteps;
			ScaleCur += ScaleStep;
			ApplePct = ScaleCur;
			ApplyScale(ScaleCur, /*bIntegerPct*/ false);
		}
	}

	// ── событие 24 шестерни 6690: `CControl_CloseHide(6690); SetVisible(6689, 1)` ──
	if (AniGearEndS > 0.0 && Now >= AniGearEndS)
	{
		AniGearEndS = 0.0;
		CloseHide(EAsdaSoulAppleNode::AniGear);
		SetVis(EAsdaSoulAppleNode::Gear, true);
	}

	// ── событие 24 «трясущегося» 6695: план сам прячется (EFrameStop), затем `uiToggleChildVisible168_171` ──
	if (ShakeEndS > 0.0 && Now >= ShakeEndS)
	{
		ShakeEndS = 0.0;
		ToggleChildVisible();
	}

	// ── цепочка «созрело» (`uiStepShowNextChild@0xE6E030` из главного цикла): эффект доиграл прогон → следующий ──
	if (bEffChain && EffStep < 2 && EffPassEndS > 0.0 && Now >= EffPassEndS)
	{
		++EffStep;
		const EAsdaSoulAppleNode Next = (EffStep == 1) ? EAsdaSoulAppleNode::Eff2 : EAsdaSoulAppleNode::Eff3;
		EffPassEndS = Now + ShowAnimRestart(Next);
		if (EffStep == 2)
		{
			SetVis(EAsdaSoulAppleNode::Golden, true);
			// Эталон показывает 6693 простым SetVisible; у её скина Animate = 1 (2 кадра / 400 мс) — крутится сама.
			if (UAsdaAnimatedPlane* P = Cast<UAsdaAnimatedPlane>(GetNodeWidget(EAsdaSoulAppleNode::Golden)))
			{
				P->bLoop = true; P->MaxLoops = 0; P->RestartAnim();
			}
			EffPassEndS = 0.0;
			UE_LOG(LogAsdaSoulApple, Log, TEXT("[яблоко] цепочка 6696 → 6697 → 6698 дошла: ЗОЛОТОЕ яблоко 6693 показано"));
		}
	}
}

// ═══════════════════════════════ МЫШЬ: ПОДСКАЗКА И КЛИК ════════════════════════════════════════

void UAsdaSoulAppleSubsystem::TickPointer()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	const bool bWndShown = bBound && WM && WM->IsWindowVisible(kPopWindowId) && Vis[(int32)EAsdaSoulAppleNode::AppleBase];
	if (!bWndShown)
	{
		if (bTipShown && Tip) { Tip->HideTooltipFor(EAsdaTipOwner::SoulApple); }
		bTipShown = false;
		bGoldenPressed = false;
		Mouse.Reset();
		return;
	}
	Mouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9): под открытым модальным нотисом эталон не отдаёт мышь никому ниже.
	const bool bModal = AsdaUi::IsModalDispatchBlocked(GI);
	if (Mouse.bLmbDown || Mouse.bLmbUp)
	{
		// След фронта ЛКМ над панелью — единственный способ понять, почему клик по 6693 не дошёл.
		UWidget* G = GetNodeWidget(EAsdaSoulAppleNode::Golden);
		UE_LOG(LogAsdaSoulApple, Log, TEXT("[яблоко] ЛКМ %s в (%.0f,%.0f): 6693 видно=%d под курсором=%d, модалка=%d"),
			Mouse.bLmbDown ? TEXT("нажата") : TEXT("отпущена"), Mouse.Cursor.X, Mouse.Cursor.Y,
			Vis[(int32)EAsdaSoulAppleNode::Golden] ? 1 : 0, (G && AsdaUi::IsCursorOverWidget(G)) ? 1 : 0, bModal ? 1 : 0);
	}
	if (bModal)
	{
		bGoldenPressed = false;
		Mouse.Reset();
		return;
	}
	// Мышь получают только контролы с NoSelect = 0: 6692, 6693, 6695 (у 6698 NoSelect = 1 — см. шапку).
	auto Over = [this](EAsdaSoulAppleNode N)
	{
		UWidget* W = GetNodeWidget(N);
		return Vis[(int32)N] && W && AsdaUi::IsCursorOverWidget(W);
	};
	const bool bOverGolden = Over(EAsdaSoulAppleNode::Golden);
	// ★ Ревизия М-1: эталон (case 8, game.c:473639) показывает подсказку только при
	//   `LocalPlayer->gap_878[47] != −1` (есть соулмейт) и `!helper_getField1(g_pUIManager)` (ничего не тащим).
	const UAsdaSkillUseSubsystem* SkU = GI ? GI->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr;
	const bool bTipAllowed = SkU && SkU->HasSoulmate() && !Mouse.bLmb;
	const bool bOverAny = bTipAllowed && (bOverGolden || Over(EAsdaSoulAppleNode::Apple) || Over(EAsdaSoulAppleNode::Shake));

	// msg 8 (наведение) → `uiTooltipSetTitleBody(&g_ItemTooltip, loc 12183 [ss1] = опыт с разделителями)`;
	//   msg 0xA (уход) → `CControl_KillFocus(&g_ItemTooltip)`.
	if (bOverAny && !bTipShown && Tip)
	{
		const FString Exp = AsdaUi::FormatThousands(AppleExp);
		const FString Title = AsdaNotice::LocSlot(GI, 12183, Exp);
		FAsdaTipMetrics M;   // эталонный клэмп 90…218 + 38 (`uiTooltipSetTitleBody@0xEB6700`)
		Tip->ShowTextTip(EAsdaTipOwner::SoulApple, Title, FString(), M);
		bTipShown = true;
	}
	else if (!bOverAny && bTipShown && Tip)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::SoulApple);
		bTipShown = false;
	}

	// msg 0xB (клик) по 6693 → 6575: нажали НА контроле и отпустили НА нём же (так JUI поднимает 0xB).
	//   ★ Прямоугольник снимаем В МОМЕНТ НАЖАТИЯ: на нажатие `UAsdaWindowSubsystem` перевешивает окно 6500
	//     поверх (z-порядок) — виджет перевставляется во вьюпорт, и кэш его геометрии на кадр отпускания
	//     обнулён (поймано самотестом: «под курсором=0» при отпускании ровно в той же точке).
	if (Mouse.bLmbDown)
	{
		bGoldenPressed = bOverGolden && AsdaUi::WidgetAbsRect(GetNodeWidget(EAsdaSoulAppleNode::Golden), GoldenPressMin, GoldenPressMax);
	}
	if (Mouse.bLmbUp)
	{
		const FVector2D Cur = Mouse.Cursor;
		const bool bInside = Cur.X >= GoldenPressMin.X && Cur.X <= GoldenPressMax.X && Cur.Y >= GoldenPressMin.Y && Cur.Y <= GoldenPressMax.Y;
		if (bGoldenPressed && bInside && Vis[(int32)EAsdaSoulAppleNode::Golden]) { ClickGoldenApple(); }
		bGoldenPressed = false;
	}
}

bool UAsdaSoulAppleSubsystem::ClickGoldenApple()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaSoulRouletteSubsystem* Ro = GI ? GI->GetSubsystem<UAsdaSoulRouletteSubsystem>() : nullptr;
	// ★ Эталон: `case 0xB: if (a3 == 6693) netSendAntiCheatRequest(win, 1);` — БЕЗ гейтов клиента
	//   (все семь проверок — на сервере, `Recv_UserSoulRoulettet@0x49A1C0`; отказ придёт 6576 кодом 10/11).
	const bool bOk = Ro && Ro->Spin();
	if (bOk) { ++Tx6575; }
	UE_LOG(LogAsdaSoulApple, Log, TEXT("[яблоко] клик по «Золотому яблоку» 6693 → 6575 %s"), bOk ? TEXT("отправлен") : TEXT("НЕ отправлен"));
	return bOk;
}

// ★★★ Ревизия С-1: порт хвоста `inventoryHidePanelsAndRefresh@0xE65510` (game.c:472590-472608) — часть,
//   относящаяся к яблоку. Пары нет ⇒ `uiPanelResetAndKillFocus` + `uiWindow_ResetView`; затем БЕЗУСЛОВНО
//   спрятать группу 6691. Зовёт `UAsdaSoulmateSubsystem::HidePanelsAndRefresh` (все места эталона).
void UAsdaSoulAppleSubsystem::OnHidePanelsAndRefresh(bool bHasPair, const TCHAR* Why)
{
	const bool bWasVis = Vis[(int32)EAsdaSoulAppleNode::AppleBase];
	if (!bHasPair)
	{
		PanelResetAndKillFocus();
		WindowResetView();
	}
	SetVis(EAsdaSoulAppleNode::AppleBase, false);
	// [ОРИГИНАЛ] При живой паре — запомнить и вернуть с окном 6500 (ключ asda.Soul.Apple.FixCloseAllHide).
	if (bHasPair && CVarFixCloseAllHide.GetValueOnGameThread() != 0)
	{
		if (!bBaseRestorePending) { bBaseVisBeforeHide = bWasVis; }
		bBaseRestorePending = true;
	}
	else
	{
		bBaseRestorePending = false;
	}
	UE_LOG(LogAsdaSoulApple, Log, TEXT("[яблоко] скрыть панели (%s): пара=%d → %s; группа 6691 спрятана%s"), Why, bHasPair ? 1 : 0,
		bHasPair ? TEXT("состояние яблока сохранено") : TEXT("сброс панели и вида"),
		bBaseRestorePending ? TEXT(", вернётся с окном 6500") : TEXT(""));
}

bool UAsdaSoulAppleSubsystem::ConsumeGoldenFlag()
{
	if (!bGoldenFlag) { return false; }
	bGoldenFlag = false;
	return true;
}

// ═══════════════════════════════ СЕТЬ ══════════════════════════════════════════════════════════

void UAsdaSoulAppleSubsystem::OnGrowth6049(const TArray<uint8>& B)
{
	// `net_OnDuelStageNotice@0xE6CF50` (game.c:475428): memcpy(v3, Src + 9, 7).
	if (B.Num() < 7) { UE_LOG(LogAsdaSoulApple, Warning, TEXT("[яблоко] ←6049 тело %d б < 7 — отброшено"), B.Num()); return; }
	++Rx6049;
	UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const int32 U   = (int32)(uint16)AsdaBytes::RdI16(&B[0]);
	const int32 Exp = AsdaBytes::RdI32(&B[2]);
	const int32 Pct = (int32)(int8)B[6];   // `SLOBYTE(v3[3])` — знаковый байт
	if (!Net || !AsdaSelf::IsSelfUserIdx(Net, U))
	{
		UE_LOG(LogAsdaSoulApple, Log, TEXT("[яблоко] ←6049 не мне (userNum %d) — пропуск, как эталон"), U);
		return;
	}
	AppleExp = Exp;   // v4[185]
	const TCHAR* Branch = TEXT("");
	if (Pct == 0)
	{
		bResetPending = true;    // v4[186] = 1
		WindowResetView();
		bShown = false;          // v4[181] = 0
		Branch = TEXT("0 → сброс вида");
	}
	else if (!bShown)
	{
		bShown = true;
		ShowExpandState();
		Branch = TEXT("раскрыть панель");
	}
	else if (Pct >= 1 && Pct <= 33)
	{
		ToggleShowAlt();
		Branch = TEXT("1…33 → трясти");
	}
	else if (Pct >= 34 && Pct <= 66)
	{
		if (bStage34) { ScaleCenter(70); ToggleShowAlt(); Branch = TEXT("34…66 → 70 % + трясти"); }
		else
		{
			bStage34 = true;
			StartGrow(50.f, 100, 2.f, 15);   // (…, 50.0f, 80.0f, 100, 2.0f, 15)
			SetVis(EAsdaSoulAppleNode::Gear, false);
			AniGearEndS = FPlatformTime::Seconds() + ShowAnimLoops(EAsdaSoulAppleNode::AniGear, 3);
			Branch = TEXT("34…66 впервые → рост 50→80 % + шестерня");
		}
	}
	else if (Pct >= 67 && Pct <= 99)
	{
		if (bStage67) { ScaleCenter(100); ToggleShowAlt(); Branch = TEXT("67…99 → 100 % + трясти"); }
		else
		{
			bStage67 = true;
			StartGrow(70.f, 100, 2.f, 20);   // (…, 70.0f, 110.0f, 100, 2.0f, 20)
			SetVis(EAsdaSoulAppleNode::Gear, false);
			AniGearEndS = FPlatformTime::Seconds() + ShowAnimLoops(EAsdaSoulAppleNode::AniGear, 3);
			Branch = TEXT("67…99 впервые → рост 70→110 % + шестерня");
		}
	}
	else if (Pct >= 100)
	{
		bStageGear = bStage34 = bStage67 = false;   // v4[182..184] = 0
		WindowShowAltView();
		bGoldenFlag = true;                         // v4[495] = 1
		Branch = TEXT("≥ 100 → созрело: цепочка эффектов, флаг 495");
	}
	else
	{
		// Отрицательный байт: ни одна ветка эталона его не ловит (все условия на `SLOBYTE >= 1`).
		Branch = TEXT("отрицательный процент — эталон ничего не делает");
	}
	UE_LOG(LogAsdaSoulApple, Log, TEXT("[яблоко] ←6049 опыт %d, процент %d: %s"), Exp, Pct, Branch);
}

void UAsdaSoulAppleSubsystem::OnExpGift6051(const TArray<uint8>& B)
{
	// `Net_OnExpGiftNotice@0xE6D940` (game.c:475533): memcpy(gift, pkt + 9, 14).
	if (B.Num() < 7) { UE_LOG(LogAsdaSoulApple, Warning, TEXT("[яблоко] ←6051 тело %d б < 7 — отброшено"), B.Num()); return; }
	++Rx6051;
	UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const int32 Code = (int32)(int8)B[0];
	if (Code == 2)
	{
		// `CControl_SetTooltipText(g_pChatWnd, loc 8167, −11514, 10000)` — плашка 221 (миснейм IDA).
		AsdaNotice::PlankLoc(GI, EAsdaMsgCategory::Social, 8167, kTipYellow, 10000.f, 6051);
		UE_LOG(LogAsdaSoulApple, Log, TEXT("[яблоко] ←6051 код 2 → плашка 8167"));
		return;
	}
	if (Code == 3)
	{
		AsdaNotice::PlankLoc(GI, EAsdaMsgCategory::Social, 8170, kTipYellow, 10000.f, 6051);
		UE_LOG(LogAsdaSoulApple, Log, TEXT("[яблоко] ←6051 код 3 → плашка 8170"));
		return;
	}
	const int32 U   = (int32)AsdaBytes::RdI16(&B[1]);
	const int32 Cid = AsdaBytes::RdI32(&B[3]);
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	bool bSkipEffect = false;   // эталон: ветка «userNum == мой», персонажа нет — выход без эффекта
	if (Net && AsdaSelf::IsSelfUserIdx(Net, U))
	{
		const bool bMe = (Net->SelectedCharId != 0 && Cid == Net->SelectedCharId);
		bResetPending = true;   // v19[186] = 1 (в обеих ветках)
		AsdaNotice::ChatColored(GI, EAsdaMsgCategory::Social, AsdaNotice::Loc(GI, bMe ? 12184 : 12185), kTipYellow, 10000.f);
		AActor* Eater = WS ? WS->FindPlayerActorByCharId(Cid) : nullptr;
		if (Eater)
		{
			if (UAsdaSoundSubsystem* Snd = GI->GetSubsystem<UAsdaSoundSubsystem>())
			{
				Snd->PlaySound3D(FName(TEXT("S_c_SmExpGiftApple.wav")), Eater->GetActorLocation());
			}
			WindowResetView();
		}
		else
		{
			bSkipEffect = true;   // `if (v3) { … LABEL_11 }` — без персонажа эталон дальше не идёт
		}
		UE_LOG(LogAsdaSoulApple, Log, TEXT("[яблоко] ←6051 съедено: charId %d (%s) → чат %d"), Cid, bMe ? TEXT("я") : TEXT("друг"), bMe ? 12184 : 12185);
	}
	if (bSkipEffect) { return; }
	// LABEL_11: эффект на съевшем, если его userNum совпал с полем пакета.
	const int32 EaterUserNum = WS ? WS->GetUserNumByCharId(Cid) : -1;
	if (EaterUserNum >= 0 && EaterUserNum == U)
	{
		if (UAsdaEffectsSubsystem* Eff = GI->GetSubsystem<UAsdaEffectsSubsystem>())
		{
			Eff->SpawnItemUseFx(U, TEXT("eff_1_char_99_expmovg-a_01"), /*SoundId*/ -1, TEXT("S_c_SmExpGift"));
		}
	}
}

// ═══════════════════════════════ ДАМП ══════════════════════════════════════════════════════════

FString UAsdaSoulAppleSubsystem::DumpState() const
{
	FString S = TEXT("══ ЯБЛОКО ДРУЖБЫ (6500, 6689…6699) ══\n");
	S += FString::Printf(TEXT("  окно 6500 привязано: %s; 6692 база @(%.0f,%.0f) %.0f×%.0f; масштаб %.1f %%%s\n"),
		bBound ? TEXT("да") : TEXT("нет"), AppleBasePos.X, AppleBasePos.Y, AppleBaseSize.X, AppleBaseSize.Y,
		ApplePct, bScaleActive ? TEXT(" (идёт рост)") : TEXT(""));
	S += FString::Printf(TEXT("  флаги: 181=%d 182=%d 183=%d 184=%d 186=%d 187=%d цепочка=%d 495=%d опыт=%d\n"),
		bShown, bStageGear, bStage34, bStage67, bResetPending, EffStep, bEffChain, bGoldenFlag, AppleExp);
	for (int32 i = 0; i < (int32)EAsdaSoulAppleNode::Count; ++i)
	{
		const UWidget* W = (Nodes.IsValidIndex(i) ? Nodes[i].Get() : nullptr);
		S += FString::Printf(TEXT("    %-22s зеркало=%d виджет=%s\n"), kIdc[i], Vis[i] ? 1 : 0,
			W ? (W->GetVisibility() == ESlateVisibility::Collapsed ? TEXT("скрыт") : TEXT("виден")) : TEXT("НЕТ"));
	}
	S += FString::Printf(TEXT("  пакеты: принято 6049=%d 6051=%d · отправлено 6575=%d\n"), Rx6049, Rx6051, Tx6575);
	return S;
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaSoulAppleDump(
	TEXT("asda.Soul.Apple.Dump"),
	TEXT("asda.Soul.Apple.Dump — состояние «Яблока Дружбы»: узлы 6689…6699, зеркало, флаги, счётчики."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (UAsdaSoulAppleSubsystem* S = GI ? GI->GetSubsystem<UAsdaSoulAppleSubsystem>() : nullptr)
		{
			UE_LOG(LogAsdaSoulApple, Log, TEXT("\n%s"), *S->DumpState());
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaSoulAppleFeed(
	TEXT("asda.Soul.Apple.Feed"),
	TEXT("asda.Soul.Apple.Feed <процент 0…100> [опыт] — ОФЛАЙН: тело 6049 своим userNum через штатный HandlePacket."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Soul.Apple.Feed"))) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!Net) { return; }
		TArray<uint8> B; B.SetNumZeroed(7);
		AsdaBytes::WrI16(&B[0], (int32)Net->SelfUserNum);
		AsdaBytes::WrI32(&B[2], A.Num() > 1 ? FCString::Atoi(*A[1]) : 12345);
		B[6] = (uint8)(A.Num() > 0 ? FCString::Atoi(*A[0]) : 100);
		Net->DebugFeedPacket(6049, B);
	}));
#endif // ASDA_DEV_CONSOLE
