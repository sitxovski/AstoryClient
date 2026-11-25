// Copyright (c) 2026 @sitxovski (Discord)
// Реализация окон 43500 «Карта развития питомца», 43400 «сетка кандидатов» и 43450 «подсказка».
//   Весь реверс с адресами — в шапке `UI/AsdaPetEvolutionSubsystem.h` и в
//   `Reports/pet_windows_re_report.md` §2, §4.3, §4.5, §6.6, §8.1.
#include "UI/AsdaPetEvolutionSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "UI/AsdaJuiWindowHandle.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaPetCageSubsystem.h"
#include "Pet/AsdaPetSubsystem.h"
#include "Net/NetOpcodes.h"
#include "Login/AsdaButtonRelay.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "AsdaPetEvolutionSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaPetEvolutionSubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaPetEvo);   // ★ R2: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

static const TCHAR* kEvoWbp  = TEXT("/Game/AsdaAssets/UI/Player_UI/Pet/WBP_Pet_Evolution_Win.WBP_Pet_Evolution_Win_C");
static const TCHAR* kGridWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Pet/WBP_Pet_Inven_Win.WBP_Pet_Inven_Win_C");
static const TCHAR* kTipWbp  = TEXT("/Game/AsdaAssets/UI/Player_UI/Pet/WBP_Pet_Inven_Tooltip.WBP_Pet_Inven_Tooltip_C");

// ★ ДЕФЕКТ ПД6 ОРИГИНАЛА (помечаем исправление, закон `fix-original-defects`).
//   У двух узлов `Slide_Ani` нижнего ряда (строки 4 и 5, позиции 277,404 и 399,404) в `.JUI`
//   отсутствует `IDNum`, поэтому `FindControlByID(43914)` / `(43915)` возвращают 0 и оригинал
//   НЕ МОЖЕТ управлять этими стрелками (падения нет из-за `if (ptr)`, ui_core.c:89840).
//   В порту стрелки адресуются по группе строки, и по умолчанию дефект ИСПРАВЛЕН.
//   Ключ 1 возвращает дословное поведение оригинала: строки 4 и 5 стрелок не получают.
bool AsdaRelayAnchorSplitOn();   // ключ `asda.UI.RelayAnchorSplit` (UI/AsdaUiHelpers.cpp) — свой GC-якорь реле у каждого окна

// ★ ОТСТУПЛЕНИЕ ПОРТА, помеченное ключом (ревизия волны 5): эталонный `CControl_InstantiateChild`
//   с `recurseChildren = 0` (ui_core.c:109622, :109698) клонирует ТОЛЬКО сам контрол, поэтому
//   картинка «LV» (43530 `Text_LV`, эталоном не биндится вовсе — отчёт §9) в строках 1..6
//   оригинала ОТСУТСТВУЕТ. Наш глубокий клон поддерева её приносит, и число под иконкой читается
//   как «4 LV», а не как «4». Ключ 1 возвращает дословное поведение.
static ASDA_DEV_CVAR(int32, CVarEvoCounterLvLiteral,
	TEXT("asda.Pet.EvoCounterLvLiteral"), 0,
	TEXT("0 — картинка «LV» есть у всех десяти строк счётчика (отступление порта, см. шапку .h); ")
	TEXT("1 — дословно как в оригинале: «LV» только у строки 0, у клонов она гасится."),
	ECVF_Default);

// Порог распознавания ДВОЙНОГО щелчка (событие 13, см. В1 в шапке `.h`) — ★ U1 (В2-A дедупа №2, 11.10.2026): общий
//   `FAsdaDblClick` с порогом Windows `GetDoubleClickTime` (было — литерал 0,5 с «системного по умолчанию»; на машине с другим
//   порогом расходилось с оригиналом). Прежнее значение — `LegacySec` поля `GridDbl` (ключ `asda.UI.DblClickLegacy 1`).
// ══════════════════════════ МЕСТНЫЕ ПОМОЩНИКИ РАСКЛАДКИ ═══════════════════════════════════════
// ★★★ РАСТЯНУТЬ ПО ШИРИНЕ РАМКУ 3×3, СОБРАННУЮ ИЗ ДЕВЯТИ ПЛАНОВ.
//   В `AsdaUi` есть близнец по ВЫСОТЕ (`StretchFramePlanesHeight`), по ширине — нет, а окно 43500
//   меняет именно ширину (390 ↔ 776, ui_core.c:90379/90391). Планы идут построчно: столбцы 0 и 2 —
//   углы и бордюры, их ширина из скина неизменна; столбец 1 получает остаток, столбец 2 едет вправо.
//   `Base` хранит АВТОРСКИЕ смещения (ловушка «считать от текущего» уже стоила бага складу —
//   см. `AsdaUi::ShiftSlotXFromBase`).
static void StretchFrameWidth(TMap<TWeakObjectPtr<UWidget>, FMargin>& Base,
	const TArray<TObjectPtr<UImage>>& Planes, float NewW)
{
	if (Planes.Num() < 9 || NewW <= 0.f) { return; }
	FMargin B0, B2;
	if (!AsdaUi::BaseOffsetsOf(Base, Planes[0], B0) || !AsdaUi::BaseOffsetsOf(Base, Planes[2], B2)) { return; }
	const float LeftW  = B0.Right;                 // FMargin у POINT-слота: Left/Top/Right=W/Bottom=H
	const float RightW = B2.Right;
	const float MidW   = FMath::Max(1.f, NewW - LeftW - RightW);
	// ★ 09.11.2026 (пачка 19, баг 4; `asda.UI.FrameKeepOverlap`): средняя колонка в ассете заходит под углы на 1 (шаги швов
	//   пост-импорта). Прежде ширина ставилась «встык» от сдвинутого влево начала — колонка кончалась на 1 px раньше правой, и в
	//   широком режиме 776 справа от окна была видна линия мира. Теперь перекрытие из авторских смещений сохраняется.
	const bool bKeep = AsdaUi::FrameKeepOverlapOn();
	const float FlushL = B0.Left + LeftW;          // край левой колонки (встык)
	const float NewR   = FlushL + MidW;            // начало правой колонки после растяжки
	for (int32 p = 0; p < 9; ++p)
	{
		FMargin B;
		if (!AsdaUi::BaseOffsetsOf(Base, Planes[p], B)) { continue; }
		const int32 Col = p % 3;
		if (Col == 0) { AsdaUi::SetSlotRect(Planes[p], FVector2D(B.Left, B.Top), FVector2D(B.Right, B.Bottom)); }
		else if (Col == 1)
		{
			const float L = bKeep ? FMath::Max(0.f, FlushL - B.Left) : 0.f;               // заход под левую колонку
			const float R = bKeep ? FMath::Max(0.f, (B.Left + B.Right) - B2.Left) : 0.f;  // заход под правую
			if (bKeep) { AsdaUi::SetSlotRect(Planes[p], FVector2D(FlushL - L, B.Top), FVector2D(MidW + L + R, B.Bottom)); }
			else       { AsdaUi::SetSlotRect(Planes[p], FVector2D(B.Left, B.Top), FVector2D(MidW, B.Bottom)); }
		}
		else { AsdaUi::SetSlotRect(Planes[p], FVector2D(bKeep ? NewR + (B.Left - B2.Left) : FlushL + MidW, B.Top), FVector2D(B.Right, B.Bottom)); }
	}
}

// То же для полосы из ТРЁХ планов (верхняя планка 43589: LBar | CBar | RBar).
static void StretchBarWidth(TMap<TWeakObjectPtr<UWidget>, FMargin>& Base,
	const TArray<TObjectPtr<UImage>>& Planes, float NewMidW)
{
	if (Planes.Num() < 3 || NewMidW <= 0.f) { return; }
	FMargin B0, B1, B2;
	if (!AsdaUi::BaseOffsetsOf(Base, Planes[0], B0)) { return; }
	if (!AsdaUi::BaseOffsetsOf(Base, Planes[1], B1)) { return; }
	if (!AsdaUi::BaseOffsetsOf(Base, Planes[2], B2)) { return; }
	// ★ 09.11.2026 (`asda.UI.FrameKeepOverlap`): перекрытие середины с краями из ассета сохраняется (см. `StretchFrameWidth`).
	if (AsdaUi::FrameKeepOverlapOn())
	{
		const float FlushL = B0.Left + B0.Right;
		const float L = FMath::Max(0.f, FlushL - B1.Left);
		const float R = FMath::Max(0.f, (B1.Left + B1.Right) - B2.Left);
		AsdaUi::SetSlotRect(Planes[1], FVector2D(FlushL - L, B1.Top), FVector2D(NewMidW + L + R, B1.Bottom));
		AsdaUi::SetSlotRect(Planes[2], FVector2D(FlushL + NewMidW, B2.Top), FVector2D(B2.Right, B2.Bottom));
		return;
	}
	AsdaUi::SetSlotRect(Planes[1], FVector2D(B1.Left, B1.Top), FVector2D(NewMidW, B1.Bottom));
	AsdaUi::SetSlotRect(Planes[2], FVector2D(B1.Left + NewMidW, B2.Top), FVector2D(B2.Right, B2.Bottom));
}

// Поставить узлу ШИРИНУ, сохранив позицию и высоту (эталонное `(*(vtbl+20))(ctrl, W, −1)`).
static void SetSlotWidth(TMap<TWeakObjectPtr<UWidget>, FMargin>& Base, UWidget* W, float NewW)
{
	FMargin B;
	if (!W || !AsdaUi::BaseOffsetsOf(Base, W, B)) { return; }
	AsdaUi::SetSlotRect(W, FVector2D(B.Left, B.Top), FVector2D(NewW, B.Bottom));
}

// Поставить узлу X, сохранив остальное (эталонное `CControl_SetField74_75(ctrl, X, −1)`).
static void SetSlotX(TMap<TWeakObjectPtr<UWidget>, FMargin>& Base, UWidget* W, float BaseX, float NewX)
{
	FMargin B;
	if (!W || !AsdaUi::BaseOffsetsOf(Base, W, B)) { return; }
	AsdaUi::SetSlotPos(W, FVector2D(B.Left + (NewX - BaseX), B.Top));
}

// ★★★ ПОИСК УЗЛА ВНУТРИ ГРУППЫ СТРОКИ, А НЕ ПО ВСЕМУ ОКНУ.
//   ЗАЧЕМ. Импортёр .JUI вшивает в имя узла его ПОРЯДКОВЫЙ НОМЕР в дереве (`…_56_0`, `…_105_0`),
//   поэтому общего «по всему окну» префикса у однотипных узлов разных строк не существует, а
//   первый же поиск по короткому префиксу ловит НЕ ТО: `Pet_Evolution_TOP_Mark_` матчит сначала
//   группу `…_grp`, а `Gfont_Small_Count_9_` не матчит `Gfont_Small_Count_9_70_0`, если добавить
//   к префиксу цифру. Живой прогон 21.09 это и показал: иконок стадий 0/7, шаблонов цифр 9/10.
//   ЛЕЧЕНИЕ. Ищем среди ПРЯМЫХ ДЕТЕЙ конкретной группы: внутри одной группы имя однозначно, и
//   переимпорт ассета (который поменяет только числа) ничего не сломает.
template <typename T>
static T* ChildByPrefix(UPanelWidget* Grp, const FString& Prefix)
{
	if (!Grp) { return nullptr; }
	for (int32 i = 0; i < Grp->GetChildrenCount(); ++i)
	{
		UWidget* C = Grp->GetChildAt(i);
		if (!C) { continue; }
		if (T* Typed = Cast<T>(C))
		{
			if (C->GetName().StartsWith(Prefix)) { return Typed; }
		}
	}
	return nullptr;
}

// Иконка строки: прямой ребёнок-`UImage` группы, названный `<База>_<число>_0`.
//   `<База>` — имя группы без суффикса `_grp`.
static UImage* RowIcon(UPanelWidget* Grp)
{
	if (!Grp) { return nullptr; }
	FString Base = Grp->GetName();
	if (Base.EndsWith(TEXT("_grp"))) { Base.LeftChopInline(4); }
	for (int32 i = 0; i < Grp->GetChildrenCount(); ++i)
	{
		UImage* Img = Cast<UImage>(Grp->GetChildAt(i));
		if (!Img) { continue; }
		const FString N = Img->GetName();
		if (!N.StartsWith(Base + TEXT("_")) || !N.EndsWith(TEXT("_0"))) { continue; }
		// Между базой и `_0` обязаны стоять только цифры — иначе это соседний узел строки.
		const FString Mid = N.Mid(Base.Len() + 1, N.Len() - Base.Len() - 3);
		if (Mid.IsEmpty() || !Mid.IsNumeric()) { continue; }
		return Img;
	}
	return nullptr;
}

// ═════════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════
void UAsdaPetEvolutionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());

	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		// Событие 0x15 (`uiSlotWindowProc`, ui_core.c:90177-90179) — закрыть окно по Esc.
		WM->RegisterEscCloser(kEvoWindowId, FSimpleDelegate::CreateUObject(this, &UAsdaPetEvolutionSubsystem::CloseEvolution));
	}
	if (UAsdaPetSubsystem* P = GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только КЛАДЁМ событие в очередь.
		HPetEvent = P->OnPetEvent.AddLambda([this](int32 Op, int32 Res, int32 Uid)
		{
			PetEvents.Add(TTuple<int32, int32, int32>(Op, Res, Uid));
		});
	}
	Stages.SetNum(kStages);
	Mats.SetNum(kMaterials);
	Cells.SetNum(kGridCells);
	TplGlyphs.Init(nullptr, kGlyphs);
	UE_LOG(LogAsdaPetEvo, Log, TEXT("[питомец-развитие] подсистема поднята (окна 43500/43400/43450)"));
}

void UAsdaPetEvolutionSubsystem::Deinitialize()
{
	if (UAsdaPetSubsystem* P = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPetSubsystem>() : nullptr)
	{
		P->OnPetEvent.Remove(HPetEvent);
	}
	ResetTipWidget();
	ResetGridWidget();
	ResetEvoWidget();
	Super::Deinitialize();
}

void UAsdaPetEvolutionSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	CloseEvolution();
	TargetUid = -1;
	CurStage = -1;
	CurCol = 0;
	for (FAsdaPetUnionRow& M : Mats) { M.PickedUid = -1; M.ReqLevel = -1; M.ReqPetIndex = -1; }
	for (FAsdaPetGridCell& C : Cells) { C.PetUid = -1; }
	// ★ «Запрос в полёте» снимается ТОЛЬКО приходом 6113 (npc_mob_pet.c:7650) — а после
	//   пересоздания сокета ответа не будет вовсе, и без этого сброса окно осталось бы немым
	//   (закон `session-reset-drops-early-data`).
	SendLatch.Disarm();
	bConfirmArmed = false;
	bConfirmOpen = false;
	SendLeft = 0.f;
	bWidePage = false;
	TipForUid = -1;
	HoverKey = -1;
	// ★ Стенд — тоже вещь СЕССИИ: перехват сети нельзя оставить висеть после пересоздания сокета.
	if (bStandArmed)
	{
		if (UAsdaPetSubsystem* P = Pets()) { P->EndSendCapture(); }
		bStandArmed = false;
	}
	bStandDone = false;
	StandLeft = 0.f;
	StandExpectUnion = -1;
	StandExpectCharId = -1;
	StandExpectMat[0] = StandExpectMat[1] = StandExpectMat[2] = -1;
	PetEvents.Reset();
	Mouse.Reset();
	Press.Clear();
	GridDbl.Reset();
	(void)Reason;
}

void UAsdaPetEvolutionSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.PetEvolution");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	DrainPetEvents();
	TrackWindowMove();   // окно могли подвинуть мышью — раскладка обязана поехать следом

	// ★ Стенд отправки: перехват снимается ЗДЕСЬ, а не из колбэка — трогать лямбду сети изнутри
	//   неё самой нельзя. И обязательно снимается по таймауту: пока он взведён, в сеть не уходит
	//   НИЧЕГО (см. `StartSendStand`).
	if (bStandArmed)
	{
		StandLeft -= DeltaTime;
		if (bStandDone || StandLeft <= 0.f)
		{
			if (!bStandDone)
			{
				UE_LOG(LogAsdaPetEvo, Error,
					TEXT("[питомец-развитие] СТЕНД 6112: тело НЕ СНЯТО за отведённое время — пакет из очереди не ушёл"));
			}
			if (UAsdaPetSubsystem* P = Pets()) { P->EndSendCapture(); }
			bStandArmed = false;
			bStandDone = false;
		}
	}

	// ★★★ ВОЛНА 8 (пункт 1 ревизии): ТАЙМАУТ ЗАСОВА «6112 В ПОЛЁТЕ» — до последней волны он
	//   снимался только приходом 6113 и сбросом сессии. Крутится ВСЕГДА, в том числе при
	//   закрытом окне: засов не должен пережить закрытие.
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, S2): общая точка `FAsdaReplyLatch` — срок стенными часами, ключ читается на тике (было `Tick(DeltaTime)` — игровой тик).
	if (SendLatch.TickTimeout(AsdaPetUi::SendTimeoutSec()))
	{
		UE_LOG(LogAsdaPetEvo, Warning,
			TEXT("[питомец-развитие] ответа 6113 нет %.0f с — засов «запрос в полёте» снят, кнопка 43876 снова жива ")
			TEXT("(ключ asda.Pet.SendTimeoutSec)"), AsdaPetUi::SendTimeoutSec());
	}

	Mouse.Update();
	if (!bVisible || !bEvoReady) { return; }

	// Клоны счётчиков строятся ЛЕНИВО: слоты канваса честны только у разложенного окна.
	if (!bClonesBuilt) { bClonesBuilt = BuildCountClones(); if (bClonesBuilt) { RefreshAll(); } }

	// ★★★ ОТЛОЖЕННАЯ ОТПРАВКА 6112 — порт `uiPetDialogTickAutoLearn@0xEDFB90` (ui_core.c:90223):
	//   `bFlag535 == 1 && m_pSelSlot == 0 && timeGetTime() − t > 0xFA0` ⇒ `skillTree_SendLearnRequest`.
	if (bConfirmArmed && !SendLatch.IsArmed())
	{
		SendLeft -= DeltaTime;
		if (SendLeft <= 0.f) { SendUnion(); }
	}

	TickMouse();
}

// ═══════════════════════════════════ КАРКАС ТРЁХ ОКОН ═════════════════════════════════════════
bool UAsdaPetEvolutionSubsystem::EnsureEvo()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kEvoWindowId, kEvoWbp, kEvoZ).Ensure(
		GetGameInstance(), Win, bEvoReady,
		[this](UAsdaWindowSubsystem&) { ResetEvoWidget(); },
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupEvoControls();
			FAsdaJuiWindowHandle::RegisterFrameZone(WM, kEvoWindowId, this, Win, bVisible,
				TEXT("Pet_Evolution_Win_0_%d"), 9);
		},
		[]()
		{
			UE_LOG(LogAsdaPetEvo, Error, TEXT("[питомец-развитие] WBP_Pet_Evolution_Win не загрузился (%s)"), kEvoWbp);
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && bEvoReady;
}

bool UAsdaPetEvolutionSubsystem::EnsureGrid()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kGridWindowId, kGridWbp, kGridZ).Ensure(
		GetGameInstance(), Grid, bGridReady,
		[this](UAsdaWindowSubsystem&) { ResetGridWidget(); },
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupGridControls();
			FAsdaJuiWindowHandle::RegisterFrameZone(WM, kGridWindowId, this, Grid, bGridShown,
				TEXT("Pet_Inven_Win_0_%d"), 9);
		},
		[]()
		{
			UE_LOG(LogAsdaPetEvo, Error, TEXT("[питомец-развитие] WBP_Pet_Inven_Win не загрузился (%s)"), kGridWbp);
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && bGridReady;
}

#if !UE_BUILD_SHIPPING
// ★ 31.10.2026 самотест GC-якорей (`asda.SelfTest.RelayAnchors`) — разбор у объявления.
UButton* UAsdaPetEvolutionSubsystem::DebugRecreateEvoKeepGrid()
{
	if (!EnsureEvo() || !EnsureGrid()) { return nullptr; }
	if (UAsdaWindowSubsystem* WM = Windows()) { WM->CloseWindow(kEvoWindowId); }   // 43500 больше не живо
	EnsureEvo();   // штатный `Ensure`: окно не живо ⇒ `ResetEvoWidget` (сброс его якоря) + разметка заново
	return BtnGridX;
}
#endif

bool UAsdaPetEvolutionSubsystem::EnsureTip()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kTipWindowId, kTipWbp, kTipZ).Ensure(
		GetGameInstance(), Tip, bTipReady,
		[this](UAsdaWindowSubsystem&) { ResetTipWidget(); },
		[this](UAsdaWindowSubsystem&) { SetupTipControls(); },
		[]()
		{
			UE_LOG(LogAsdaPetEvo, Error, TEXT("[питомец-развитие] WBP_Pet_Inven_Tooltip не загрузился (%s)"), kTipWbp);
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && bTipReady;
}

void UAsdaPetEvolutionSubsystem::ResetEvoWidget()
{
	if (UAsdaWindowSubsystem* WM = Windows())
	{
		WM->UnregisterUiZone(kEvoWindowId);
		WM->CloseWindow(kEvoWindowId);
	}
	Win = nullptr;
	TxtTitle = TxtOk = TxtEnd = TxtInfo = TxtResult = nullptr;
	BtnX = BtnOk = BtnEnd = BtnExpand = BtnCollapse = nullptr;
	PanelRight = ResultAniGrp = ResultAni = Ctrl43725 = Ctrl43728 = BtnBaseGrp = BarGrp = TitleGrp = nullptr;
	ImgResult3d = GfontResult = ImgInfoBg = TitleDrag = TplLead1 = nullptr;
	TplCountGrp = nullptr;
	WinPlanes.Reset();
	BarPlanes.Reset();
	RedBoxPlanes.Reset();
	TplGlyphs.Init(nullptr, kGlyphs);
	Stages.Reset();
	Stages.SetNum(kStages);
	Mats.Reset();
	Mats.SetNum(kMaterials);
	Relays.Reset();
	LayoutBase.Reset();
	bLayoutCaptured = false;
	bClonesBuilt = false;
	bEvoReady = false;
	bVisible = false;
	RedBoxBase = FVector2D(-1.0, -1.0);
	Stage0IconBase = FVector2D(-1.0, -1.0);
}

void UAsdaPetEvolutionSubsystem::ResetGridWidget()
{
	if (UAsdaWindowSubsystem* WM = Windows())
	{
		WM->UnregisterUiZone(kGridWindowId);
		WM->CloseWindow(kGridWindowId);
	}
	Grid = nullptr;
	TxtGridTitle = nullptr;
	BtnGridX = nullptr;
	Cells.Reset();
	Cells.SetNum(kGridCells);
	GridRelays.Reset();   // ★ 31.10.2026: якорь крестика сетки живёт и умирает вместе с сеткой (`asda.UI.RelayAnchorSplit`)
	bGridReady = false;
	bGridShown = false;
}

void UAsdaPetEvolutionSubsystem::ResetTipWidget()
{
	if (UAsdaWindowSubsystem* WM = Windows())
	{
		WM->CloseWindow(kTipWindowId);
	}
	Tip = nullptr;
	TxtTipName = TxtTipLabel = TxtTipStage = TxtTipPct = nullptr;
	TipBarFill = nullptr;
	bTipReady = false;
	bTipShown = false;
	TipForUid = -1;
}

// ★★★ РАЗБОР КОНТРОЛОВ 43500. Порядок и состав — `uiPetDialogInit@0xEDC7C0` (ui_core.c:89789-89907),
//   номер за номером. Имена виджетов — из ЖИВОГО структурного дампа
//   `Reports/pet/ui_dump/43500_Pet_Evolution_Win.md` и `wbp_raw/WBP_Pet_Evolution_Win.txt`.
void UAsdaPetEvolutionSubsystem::SetupEvoControls()
{
	bEvoReady = false;
	if (!Win || !Win->WidgetTree) { return; }

	// ── Девять планов рамки окна (их же тянет `ApplyPage`) ──
	WinPlanes.Init(nullptr, 9);
	for (int32 p = 0; p < 9; ++p)
	{
		WinPlanes[p] = Cast<UImage>(AsdaUi::FindNodeExact(Win, *FString::Printf(TEXT("Pet_Evolution_Win_0_%d"), p)));
	}
	// ── Три плана верхней планки 43589 и её группа ──
	BarPlanes.Init(nullptr, 3);
	BarPlanes[0] = Cast<UImage>(AsdaUi::FindNodeExact(Win, TEXT("WinSm_LBar_2_0")));
	BarPlanes[1] = Cast<UImage>(AsdaUi::FindNodeExact(Win, TEXT("Pet_Evolution_Topbar_1_0")));
	BarPlanes[2] = Cast<UImage>(AsdaUi::FindNodeExact(Win, TEXT("WinSm_RBar_4_0")));
	BarGrp = AsdaUi::FindNodeExact(Win, TEXT("Pet_Evolution_Topbar_grp"));

	// ── 43501 заголовок и его группа (в ней же «крестик» 43889) ──
	TxtTitle  = Cast<UTextBlock>(AsdaUi::FindNodeExact(Win, TEXT("Pet_Evolution_Top_TextMove")));
	TitleDrag = Cast<UImage>(AsdaUi::FindNodeExact(Win, TEXT("Pet_Evolution_Top_TextMove_138_DRAG")));
	TitleGrp  = AsdaUi::FindNodeExact(Win, TEXT("Pet_Evolution_Top_TextMove_grp"));
	BtnX      = Cast<UButton>(AsdaUi::FindNodeExact(Win, TEXT("Pet_Evolution_X")));

	// ── 43877 подсказка (канал C) ──
	TxtInfo   = Cast<UTextBlock>(AsdaUi::FindNodeExact(Win, TEXT("Pet_Evolution_Tooltip_Text")));
	ImgInfoBg = Cast<UImage>(AsdaUi::FindNodeExact(Win, TEXT("Pet_Evolution_Tooltip_Text_140_HIT")));

	// ── Кнопки 43876 / 43879 и группа 43888 ──
	BtnOk      = Cast<UButton>(AsdaUi::FindNodeExact(Win, TEXT("Pet_Evolution_OK_2")));
	TxtOk      = Cast<UTextBlock>(AsdaUi::FindNodeExact(Win, TEXT("Pet_Evolution_OK_2_Text")));
	BtnEnd     = Cast<UButton>(AsdaUi::FindNodeExact(Win, TEXT("Pet_Evolution_End")));
	TxtEnd     = Cast<UTextBlock>(AsdaUi::FindNodeExact(Win, TEXT("Pet_Evolution_End_Text")));
	BtnBaseGrp = AsdaUi::FindNodeExact(Win, TEXT("Btn_CBase_grp"));

	// ── Стрелки страниц 43582/43583 и правая панель 43580 ──
	BtnExpand   = Cast<UButton>(AsdaUi::FindNodeExact(Win, TEXT("SIDEBUT")));
	BtnCollapse = Cast<UButton>(AsdaUi::FindNodeExact(Win, TEXT("CLOSEBUT")));
	PanelRight  = AsdaUi::FindNodeExact(Win, TEXT("Pet_Evolution_R_grp"));

	// ── Блок результата: 43883 анимация, 43576 превью (волна 6-7), 43742 имя, 43740 «готово» ──
	ResultAniGrp = AsdaUi::FindNodeExact(Win, TEXT("Pet_Evolution_R_Mark_Ani1_grp"));
	ResultAni    = AsdaUi::FindNodeExact(Win, TEXT("Pet_Evolution_R_Mark_Ani1_10_0"));
	ImgResult3d  = Cast<UImage>(AsdaUi::FindNodeExact(Win, TEXT("Pet_Evolution_R_Mark_3d_8_0")));
	TxtResult    = Cast<UTextBlock>(AsdaUi::FindNodeExact(Win, TEXT("Pet_Evolution_R_Text1")));
	GfontResult  = Cast<UImage>(AsdaUi::FindNodeExact(Win, TEXT("Pet_Evolution_Gfont_3_9_0")));

	// ── 43725/43728: эталон их привязывает, ЧИТАТЕЛЕЙ в срезе нет (см. шапку) ──
	Ctrl43725 = AsdaUi::FindNodeByPrefix(Win, TEXT("Pet_Evolution_R_Mark_Ani2"));
	Ctrl43728 = AsdaUi::FindNodeExact(Win, TEXT("Pet_Evolution_R_TOP_Text"));

	// ── 43885 красная рамка выбора: восемь планов ──
	RedBoxPlanes.Init(nullptr, 8);
	for (int32 p = 0; p < 8; ++p)
	{
		RedBoxPlanes[p] = Cast<UImage>(AsdaUi::FindNodeExact(Win, *FString::Printf(TEXT("Pet_Evolution_L_Redlinebox_134_%d"), p)));
	}

	// ── ШАБЛОНЫ СЧЁТЧИКА. Эталон их ПРЯЧЕТ (ui_core.c:89820, :89824) ──
	TplCountGrp = AsdaUi::FindNodeExact(Win, TEXT("Pet_Evolution_Count_1_grp"));
	UPanelWidget* TplCountPanel = Cast<UPanelWidget>(TplCountGrp);
	UPanelWidget* TplDigitPanel = Cast<UPanelWidget>(AsdaUi::FindNodeExact(Win, TEXT("Pet_Evolution_Count_1_2_grp")));
	// 43529 — ведущая «1»: прямой ребёнок КОНТЕЙНЕРА СЧЁТЧИКА (а десять цифр лежат этажом ниже).
	TplLead1 = ChildByPrefix<UImage>(TplCountPanel, TEXT("Gfont_Small_Count_1_"));
	TplGlyphs.Init(nullptr, kGlyphs);
	// Порядок эталона: `m_apTplCtrl[m] = FindControlByID(m + 43532)`, а 43532 — скин `Count_9`,
	//   43541 — `Count_0` (живой дамп, строки 61-70). Имя узла содержит имя СКИНА, поэтому
	//   шаблон m — это `Gfont_Small_Count_(9−m)_*`, и искать его надо ВНУТРИ контейнера цифр.
	for (int32 m = 0; m < kGlyphs; ++m)
	{
		TplGlyphs[m] = ChildByPrefix<UImage>(TplDigitPanel, FString::Printf(TEXT("Gfont_Small_Count_%d_"), 9 - m));
	}
	AsdaUi::SetShownDecor(TplLead1, false);
	for (int32 m = 0; m < kGlyphs; ++m) { AsdaUi::SetShownDecor(TplGlyphs[m], false); }

	// ── СЕМЬ СТРОК ЭВОЛЮЦИИ. `for (j = 0; j < 7; ++j)` (ui_core.c:89827-89844) ──
	Stages.Reset();
	Stages.SetNum(kStages);
	for (int32 j = 0; j < kStages; ++j)
	{
		FAsdaPetEvoRow& R = Stages[j];
		const FString GrpName = (j == 0) ? FString(TEXT("Pet_Evolution_TOP_Mark_grp"))
		                                 : FString::Printf(TEXT("Pet_Evolution_L_Mark%d_grp"), j);
		R.Group = Cast<UPanelWidget>(AsdaUi::FindNodeExact(Win, *GrpName));
		// Иконка строки и оба глифа — ПРЯМЫЕ ДЕТИ группы строки (по всему окну искать нельзя,
		//   см. комментарий к `ChildByPrefix`).
		R.Icon  = RowIcon(R.Group);
		R.Label = Cast<UTextBlock>(AsdaUi::FindNodeExact(Win, *FString::Printf(TEXT("Pet_Evolution_L_Text%d"), j + 1)));
		R.GfontMain = ChildByPrefix<UImage>(R.Group, FString::Printf(TEXT("Pet_Evolution_Gfont_1_%d_"), j));
		R.GfontEvo  = ChildByPrefix<UImage>(R.Group, FString::Printf(TEXT("Pet_Evolution_Gfont_2_%d_"), j));
		// ★ ДЕФЕКТ ПД6: стрелку берём НЕ по номеру 43910+j (у строк 4 и 5 номера в `.JUI` нет),
		//   а как единственный `UAsdaAnimatedPlane` внутри группы строки. Строка 0 стрелки не имеет
		//   и в оригинале (`if (j)`, ui_core.c:89838).
		if (j > 0 && R.Group)
		{
			for (int32 c = 0; c < R.Group->GetChildrenCount(); ++c)
			{
				UWidget* Ch = R.Group->GetChildAt(c);
				if (Ch && Ch->GetClass()->GetName().Contains(TEXT("AsdaAnimatedPlane"))) { R.SlideAni = Ch; break; }
			}
		}
		AsdaUi::SetShownDecor(R.GfontMain, false);
		AsdaUi::SetShownDecor(R.GfontEvo, false);
		AsdaUi::SetShownDecor(R.SlideAni, false);
	}

	// ── ТРИ СТРОКИ ЮНИОНА. `for (m = 0; m < 3; ++m)` (ui_core.c:89849-89866) ──
	Mats.Reset();
	Mats.SetNum(kMaterials);
	for (int32 m = 0; m < kMaterials; ++m)
	{
		FAsdaPetUnionRow& R = Mats[m];
		R.Group = Cast<UPanelWidget>(AsdaUi::FindNodeExact(Win, *FString::Printf(TEXT("Pet_Evolution_R_Mark%d_grp"), m + 1)));
		R.Icon  = RowIcon(R.Group);
		// 43743+m — заголовки `R_Text2/3/4`; 43746+m — имена `R_Text5/6/7`; 43749+m — «готовность»
		//   `R_Text8/9/10`; 43755+m — глиф `Gfont_4/5/6`.
		R.Title = Cast<UTextBlock>(AsdaUi::FindNodeExact(Win, *FString::Printf(TEXT("Pet_Evolution_R_Text%d"), m + 2)));
		R.Name  = Cast<UTextBlock>(AsdaUi::FindNodeExact(Win, *FString::Printf(TEXT("Pet_Evolution_R_Text%d"), m + 5)));
		R.Ready = Cast<UTextBlock>(AsdaUi::FindNodeExact(Win, *FString::Printf(TEXT("Pet_Evolution_R_Text%d"), m + 8)));
		R.GfontReady = ChildByPrefix<UImage>(R.Group, FString::Printf(TEXT("Pet_Evolution_Gfont_%d_"), m + 4));
		AsdaUi::SetShownDecor(R.GfontReady, false);
		// Подпись строки — loc 109964+m, цикл `m + 109964` (ui_core.c:89851-89853).
		if (R.Title)
		{
			const FString S = Loc(PetEvoLoc::kMatTitle0 + m);
			if (!S.IsEmpty()) { R.Title->SetText(FText::FromString(S)); }
		}
	}

	// ── Кнопки. Событие 66561 (`uiSlotWindowProc`, ui_core.c:90149-90174) ──
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, MoveTemp(Fn), Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnOk,       43876, [this](int32) { OnOkClicked(); });
	Bind(BtnEnd,      43879, [this](int32) { OnCloseClicked(); });
	Bind(BtnX,        43889, [this](int32) { OnCloseClicked(); });
	Bind(BtnExpand,   43582, [this](int32) { OnPageArrow(true); });
	Bind(BtnCollapse, 43583, [this](int32) { OnPageArrow(false); });

	// ── Подписи init. КАЖДАЯ — по своей строке эталона ──
	auto SetLoc = [this](UTextBlock* T, int32 Id)
	{
		if (!T) { return; }
		const FString S = Loc(Id);
		if (!S.IsEmpty()) { T->SetText(FText::FromString(S)); }
	};
	SetLoc(TxtTitle, PetEvoLoc::kTitle);    // :89813  43501
	SetLoc(TxtOk,    PetEvoLoc::kBtnOk);    // :89869  43876
	SetLoc(TxtEnd,   PetEvoLoc::kBtnEnd);   // :89872  43879
	// 43877 при init очищается (`CControl_SetText(m_pInfoText, &Src)`, :89867).
	if (TxtInfo) { TxtInfo->SetText(FText::GetEmpty()); }

	CaptureLayoutBase();
	ApplyPage(false);

	// *** ЭТАЛОННАЯ РАМКА ТЕКСТА — ВСЕМУ ДЕРЕВУ ОКНА (24.09.2026, следствие ревизии, СРЕДН-6).
	//   ЗАЧЕМ ЗДЕСЬ. Жёлтая подсказка 43877 обрывалась на полуслове у правого края: порт отдавал
	//   Slate одну неограниченную строку. У эталона узел рисует `ctrlRenderLabelQuad`
	//   (`ui_core.c:110604-110757`), перенос включён атрибутом скина (поле 925 = `TextOutLine`,
	//   `:110742-110746`), а у скина `NoImage`, на котором сидят все тексты этого окна,
	//   `TextOutLine="1"` (`JuiSkin.ini:29835-29838`).
	//   ★ ПЕРЕНОС НА НЕСКОЛЬКО СТРОК — ЭТО ЭТАЛОН, А НЕ «КРАСИВЕЕ ОРИГИНАЛА»: контрол 43877 в .JUI
	//     имеет размер 368x75 (дамп окна, узел 140), то есть высоту под ПЯТЬ строк по 15 px. Одна
	//     строка в нём — наша потеря, а не замысел автора.
	//   Разбор целиком и выбор «здесь, а не в импортёре» — у `AsdaUi::ApplyJuiTextBoundsToTree`.
	{
		// Признак скина — тот же, что у 43000: `NoImage`/`NoImage_icon`/`Black`, TextOutLine="1".
		const int32 NBounds = AsdaUi::ApplyJuiTextBoundsToTree(Win, /*bSkinClipsText*/ true);
		UE_LOG(LogAsdaPetEvo, Log,
			TEXT("[питомец-развитие] эталонная рамка текста поставлена узлам: %d"), NBounds);
	}

	bEvoReady = (Stages.Num() == kStages && Stages[0].Icon != nullptr && BtnOk != nullptr && BtnEnd != nullptr);
	int32 IconsFound = 0, LabelsFound = 0, ArrowsFound = 0;
	for (const FAsdaPetEvoRow& R : Stages)
	{
		IconsFound += (R.Icon != nullptr) ? 1 : 0;
		LabelsFound += (R.Label != nullptr) ? 1 : 0;
		ArrowsFound += (R.SlideAni != nullptr) ? 1 : 0;
	}
	UE_LOG(LogAsdaPetEvo, Log,
		TEXT("[питомец-развитие] 43500 разобрано: иконок стадий %d/7, подписей %d/7, стрелок %d/6, ")
		TEXT("слотов материалов %d/3, шаблон 43529=%s, цифровых шаблонов %d/10"),
		IconsFound, LabelsFound, ArrowsFound,
		(Mats[0].Icon ? 1 : 0) + (Mats[1].Icon ? 1 : 0) + (Mats[2].Icon ? 1 : 0),
		TplLead1 ? TEXT("есть") : TEXT("НЕТ"),
		[this]() { int32 N = 0; for (const TObjectPtr<UImage>& G : TplGlyphs) { N += G ? 1 : 0; } return N; }());
}

// Разбор 43400: тридцать ячеек 43401+ii (ui_core.c:89881-89885), заголовок 43431, «крестик» 43439.
void UAsdaPetEvolutionSubsystem::SetupGridControls()
{
	bGridReady = false;
	if (!Grid || !Grid->WidgetTree) { return; }

	Cells.Reset();
	Cells.SetNum(kGridCells);
	for (int32 i = 0; i < kGridCells; ++i)
	{
		// Имя иконки — `Pet_Inven_MARK_<n>_<idx>_0`; подчёркивание после номера разводит 1 и 10.
		Cells[i].Icon = Cast<UImage>(AsdaUi::FindNodeByPrefix(Grid, *FString::Printf(TEXT("Pet_Inven_MARK_%d_"), i + 1)));
		Cells[i].PetUid = -1;
	}
	TxtGridTitle = Cast<UTextBlock>(AsdaUi::FindNodeExact(Grid, TEXT("Pet_Inven_Top_Text")));
	BtnGridX     = Cast<UButton>(AsdaUi::FindNodeExact(Grid, TEXT("Pet_Inven_Win_X")));

	// 43431 ← loc 109962 (ui_core.c:89880).
	if (TxtGridTitle)
	{
		const FString S = Loc(PetEvoLoc::kGridTitle);
		if (!S.IsEmpty()) { TxtGridTitle->SetText(FText::FromString(S)); }
	}
	// ★ 43439 — это СБРОС/ЗАКРЫТИЕ СЕТКИ (`sub_9257FB`), а НЕ закрытие диалога 43500
	//   (ui_core.c:90161; отмечено и в отчёте §2.1).
	if (BtnGridX)
	{
		// ★ 31.10.2026: якорь — сетки, а не окна 43500 (его сбрасывает `ResetEvoWidget` при живой сетке; `asda.UI.RelayAnchorSplit`).
		AsdaUi::BindButton(this, BtnGridX, 43439, [this](int32) { CloseGrid(); }, AsdaRelayAnchorSplitOn() ? GridRelays : Relays);   // ★ U20: реле клика — общая точка
	}

	int32 Found = 0;
	for (const FAsdaPetGridCell& C : Cells) { Found += C.Icon ? 1 : 0; }
	bGridReady = (Found == kGridCells);
	UE_LOG(LogAsdaPetEvo, Log, TEXT("[питомец-развитие] 43400 разобрано: ячеек %d/30, 43431=%s, 43439=%s"),
		Found, TxtGridTitle ? TEXT("есть") : TEXT("НЕТ"), BtnGridX ? TEXT("есть") : TEXT("НЕТ"));
}

// Разбор 43450 (ui_core.c:89887-89897): 43452 имя, 43453 «Рост», 43454 стадия, 43451 полоса, 43455 %.
void UAsdaPetEvolutionSubsystem::SetupTipControls()
{
	bTipReady = false;
	if (!Tip || !Tip->WidgetTree) { return; }

	TxtTipName  = Cast<UTextBlock>(AsdaUi::FindNodeExact(Tip, TEXT("Pet_Inven_Tooltip_T1_Text")));
	TxtTipLabel = Cast<UTextBlock>(AsdaUi::FindNodeExact(Tip, TEXT("Pet_Inven_Tooltip_T2_Text")));
	TxtTipStage = Cast<UTextBlock>(AsdaUi::FindNodeExact(Tip, TEXT("Pet_Inven_Tooltip_T3_Text")));
	TxtTipPct   = Cast<UTextBlock>(AsdaUi::FindNodeExact(Tip, TEXT("Pet_Inven_Tooltip_Text")));
	// `Green_Bar_3_0` — дорожка, `Green_Bar_3_1` — ЗАЛИВКА (живой дамп: дорожка 281..582,
	//   заливка 281..482 при 2/3 прогресса).
	TipBarFill  = Cast<UImage>(AsdaUi::FindNodeExact(Tip, TEXT("Green_Bar_3_1")));

	// 43453 ← loc 109963 (ui_core.c:89891).
	if (TxtTipLabel)
	{
		const FString S = Loc(PetEvoLoc::kTipGrowth);
		if (!S.IsEmpty()) { TxtTipLabel->SetText(FText::FromString(S)); }
	}
	bTipReady = (TxtTipName != nullptr && TxtTipStage != nullptr && TipBarFill != nullptr);
	UE_LOG(LogAsdaPetEvo, Log, TEXT("[питомец-развитие] 43450 разобрано: 43452=%s 43453=%s 43454=%s 43451=%s 43455=%s"),
		TxtTipName ? TEXT("есть") : TEXT("НЕТ"), TxtTipLabel ? TEXT("есть") : TEXT("НЕТ"),
		TxtTipStage ? TEXT("есть") : TEXT("НЕТ"), TipBarFill ? TEXT("есть") : TEXT("НЕТ"),
		TxtTipPct ? TEXT("есть") : TEXT("НЕТ"));
}

// ══════════════════════ РАЗМНОЖЕНИЕ ШАБЛОНОВ СЧЁТЧИКА (см. шапку `.h`) ════════════════════════
bool UAsdaPetEvolutionSubsystem::CloneCounter(UPanelWidget* Dst, UImage* Anchor, const TCHAR* Tag,
	TObjectPtr<UWidget>& OutRoot, TObjectPtr<UImage>& OutLead1, TArray<TObjectPtr<UImage>>& OutGlyphs)
{
	OutGlyphs.Init(nullptr, kGlyphs);
	if (!Win || !Dst || !Anchor || !TplCountGrp) { return false; }

	// Авторское смещение счётчика от иконки строки 0 — константы `kCountDx/kCountDy` (сырой дамп).
	FVector2D AnchorPos, AnchorSize;
	if (!AsdaUi::SlotRect(Anchor, AnchorPos, AnchorSize)) { return false; }

	// ★ ЭТАЛОН КЛОНИРУЕТ ПОВЕРХНОСТНО — тринадцать вызовов `CControl_InstantiateChild` на строку
	//   с `recurseChildren = 0` (тело: ui_core.c:109622, рекурсия под 4-м аргументом :109698).
	//   Один глубокий клон поддерева `43520` даёт ТО ЖЕ множество 13 узлов, потому что в `.uasset`
	//   они лежат вложенно; подробный разбор эквивалентности — в шапке `.h`.
	const FString Prefix = FString::Printf(TEXT("EvoCount_%s_"), Tag);
	UWidget* Root = AsdaUi::CloneWidgetSubtree(Win, TplCountGrp, Dst,
		[&Prefix](const UWidget* Src) { return Prefix + (Src ? Src->GetName() : FString()); });
	if (!Root) { return false; }
	OutRoot = Root;
	AsdaUi::SetSlotPos(Root, FVector2D(AnchorPos.X + (double)kCountDx, AnchorPos.Y + (double)kCountDy));

	// В клоне ищем ведущую «1» и десять цифровых глифов — ПО ДЕТЯМ соответствующих групп клона,
	//   а не по префиксу на всё окно (см. комментарий к `ChildByPrefix`: имена несут порядковый
	//   номер узла, и короткий префикс либо ловит не тот узел, либо не ловит `…_9_70_0`).
	UPanelWidget* CloneRootPanel = Cast<UPanelWidget>(Root);
	UPanelWidget* CloneDigits = nullptr;
	if (CloneRootPanel)
	{
		for (int32 i = 0; i < CloneRootPanel->GetChildrenCount(); ++i)
		{
			UWidget* C = CloneRootPanel->GetChildAt(i);
			if (C && C->GetName().StartsWith(Prefix + TEXT("Pet_Evolution_Count_1_2_grp")))
			{
				CloneDigits = Cast<UPanelWidget>(C);
				break;
			}
		}
	}
	// ★ Ключ `asda.Pet.EvoCounterLvLiteral 1` гасит картинку «LV» у КЛОНОВ (у строки 0 она
	//   авторская и остаётся): эталон при `recurseChildren = 0` её в строки 1..6 не переносит.
	if (CVarEvoCounterLvLiteral.GetValueOnGameThread() != 0)
	{
		AsdaUi::SetShownDecor(ChildByPrefix<UImage>(CloneRootPanel, Prefix + TEXT("Pet_Evolution_Count_lv_")), false);
	}
	OutLead1 = ChildByPrefix<UImage>(CloneRootPanel, Prefix + TEXT("Gfont_Small_Count_1_"));
	for (int32 m = 0; m < kGlyphs; ++m)
	{
		// Шаблон m ↔ скин `Count_(9−m)`; в имени узла стоит имя скина.
		OutGlyphs[m] = ChildByPrefix<UImage>(CloneDigits, FString::Printf(TEXT("%sGfont_Small_Count_%d_"), *Prefix, 9 - m));
	}
	// Эталон гасит клоны сразу после создания (пролог перерисовки, skill_buff.c:37787-37790).
	HideCounter(OutLead1, OutGlyphs);
	return OutLead1 != nullptr;
}

bool UAsdaPetEvolutionSubsystem::BuildCountClones()
{
	if (!Win || !TplCountGrp) { return false; }
	// Слоты канваса читаются сразу после Build, но иконку строки 0 могли ещё не разложить —
	//   проверяем ненулевым размером (закон `ui-check-game-path-geometry`).
	FVector2D P, S;
	if (!Stages.IsValidIndex(0) || !Stages[0].Icon || !AsdaUi::SlotRect(Stages[0].Icon, P, S) || S.X <= 0.0)
	{
		return false;
	}
	Stage0IconBase = P;
	if (RedBoxPlanes.Num() == 8 && RedBoxPlanes[0])
	{
		FVector2D RP, RS;
		if (AsdaUi::SlotRect(RedBoxPlanes[0], RP, RS)) { RedBoxBase = RP; }
	}

	int32 Built = 0;
	for (int32 j = 0; j < kStages; ++j)
	{
		FAsdaPetEvoRow& R = Stages[j];
		if (CloneCounter(R.Group, R.Icon, *FString::Printf(TEXT("S%d"), j), R.CountClone, R.Lead1, R.Glyphs)) { ++Built; }
	}
	for (int32 m = 0; m < kMaterials; ++m)
	{
		FAsdaPetUnionRow& R = Mats[m];
		if (CloneCounter(R.Group, R.Icon, *FString::Printf(TEXT("M%d"), m), R.CountClone, R.Lead1, R.Glyphs)) { ++Built; }
	}
	UE_LOG(LogAsdaPetEvo, Log,
		TEXT("[питомец-развитие] шаблоны 43529 и 43532…43541 размножены: строк собрано %d из 10 ")
		TEXT("(7 эволюции + 3 юниона)"), Built);
	return Built > 0;
}

void UAsdaPetEvolutionSubsystem::HideCounter(UImage* Lead1, const TArray<TObjectPtr<UImage>>& Glyphs)
{
	AsdaUi::SetShownDecor(Lead1, false);
	for (const TObjectPtr<UImage>& G : Glyphs) { AsdaUi::SetShownDecor(G, false); }
}

// ★★★ ЦИФРОВОЙ СЧЁТЧИК ТРЕБУЕМОГО УРОВНЯ СТАДИИ, 1:1 (skill_buff.c:37806-37831, дубль
//   item_inventory.c:65128-65155). ★ Это НЕ «уровень питомца»: число берётся из `e[5k+4]`
//   (skill_buff.c:37798), а подпись рядом — 43530 `Text_LV`.
//   `itoa` → длина; при длине 1 все десять глифов сдвигаются на −4 по x, при 2 — на 0;
//   затем показывается глиф `abs(цифра − 9)` ТОЛЬКО для позиции 0, а позиция 1 рисуется
//   клоном 43529 (скин `Count_1`).
//
// ★★★ ДЕФЕКТ ОРИГИНАЛА ПД7 — ЛЮБОЕ ДВУЗНАЧНОЕ ЧИСЛО РИСУЕТСЯ КАК «X1» (закон
//   `fix-original-defects` требует пометки; находка ревизии волны 5, МЕЛКОЕ-6).
//   Клон 43529 стоит СЛЕВА (x = 383), десять глифов — СПРАВА (x = 392/394), то есть 43529
//   занимает позицию ВЕДУЩЕЙ цифры, а включается по ветке «цифр две» и всегда рисует свой скин
//   `Count_1`. «10» соберётся верно, «20» оригинал покажет как «10».
//   ИСПРАВИТЬ НЕЛЬЗЯ БЕЗ АССЕТА: в позиции ведущей цифры в `.JUI` есть единственный узел 43529 и
//   только со скином `Count_1`; второго набора глифов там нет, а дорисовывать разметку
//   запрещено (`no-ui-regeneration-without-permission`, `no-runtime-widget-fixes`).
//   Поэтому воспроизводим 1:1, а дефект занесён в `PORT_PENDING_ON_SYSTEMS.md`.
//   На поставляемых данных не стреляет: требуемые уровни рецептов однозначны, максимум — «10».
void UAsdaPetEvolutionSubsystem::DrawCounter(int32 Value, UImage* Lead1, const TArray<TObjectPtr<UImage>>& Glyphs)
{
	HideCounter(Lead1, Glyphs);
	if (Value < 0) { return; }
	const FString Buf = FString::FromInt(Value);
	const int32 Digits = Buf.Len();
	const double ShiftX = (Digits == 1) ? (double)kGlyphShiftOneDigit : 0.0;
	for (const TObjectPtr<UImage>& G : Glyphs)
	{
		if (!G) { continue; }
		FVector2D Pos, Size;
		if (AsdaUi::SlotRect(G, Pos, Size)) { AsdaUi::SetSlotPos(G, FVector2D(ShiftX, Pos.Y)); }
	}
	for (int32 i = 0; i < Digits; ++i)
	{
		const int32 D = FMath::Clamp((int32)(Buf[i] - TCHAR('0')), 0, 9);
		const int32 GlyphIdx = FMath::Abs(D - 9);
		if (i == 0 && Glyphs.IsValidIndex(GlyphIdx)) { AsdaUi::SetShownDecor(Glyphs[GlyphIdx], true); }
		if (i == 1) { AsdaUi::SetShownDecor(Lead1, true); }
	}
}

// ═══════════════════ ДВЕ СТРАНИЦЫ ОКНА (uiUpgradeDialogActivate / …Reset) ═════════════════════
// Разбор и доказательство прогоном — у объявления в `.h`.
// ★★★ ПОПРАВКА РЕВИЗИИ 25.09.2026 (СРЕДНЕЕ-7): СЛЕЖЕНИЕ ЗА ЛЮБЫМ СДВИГОМ ОКНА, А НЕ ТОЛЬКО ЗА НАШИМ.
//
//   БОЛЕЗНЬ. `OnWindowMovedExternally` звала ровно одна точка — авто-постановка рядом с клеткой.
//   Но 43500 ПЕРЕТАСКИВАЕМО: у него зарегистрирована зона и ручка (`RegisterFrameZone`,
//   `EnsureEvo`), и общий `TickDrag` оконной подсистемы двигает его слоты сам. После такого
//   перетаскивания снимок `LayoutBase` оставался на старом месте, и первое же переключение
//   страницы (`ApplyPage`) возвращало часть узлов на прежние координаты — окно разъезжалось
//   надвое. Ровно та болезнь, которую правка для авто-постановки и лечила, только вход другой.
//
//   ЛЕЧЕНИЕ. Один сторож на ВСЕ причины сдвига: каждый кадр сверяем левый верхний угол окна с
//   запомненным и разницу отдаём в ту же `OnWindowMovedExternally`. Источник сдвига значения не
//   имеет — мышь, наш код, восстановление из ini. Первый вызов только запоминает точку.
void UAsdaPetEvolutionSubsystem::TrackWindowMove()
{
	if (!Win || !bVisible) { bWindowOriginKnown = false; return; }
	FVector2D TL(0.0, 0.0);
	if (!FAsdaPopupMenu::GetTopLeft(Win, TL)) { return; }
	if (!bWindowOriginKnown)
	{
		LastWindowOrigin = TL;
		bWindowOriginKnown = true;
		return;
	}
	const FVector2D Delta = TL - LastWindowOrigin;
	LastWindowOrigin = TL;
	// Порог — полпикселя: дробная дельта копится в `TickDrag`, и гонять раскладку на 0.001 незачем.
	if (Delta.SizeSquared() < 0.25) { return; }
	OnWindowMovedExternally(Delta);
}

void UAsdaPetEvolutionSubsystem::OnWindowMovedExternally(FVector2D Delta)
{
	if (Delta.IsNearlyZero()) { return; }
	// 1) Снимок авторской раскладки — иначе его вернёт первое же переключение страницы.
	// ★★★ ПОПРАВКА 25.09.2026 (жалоба владельца «у окна развития нет шапки и крестика»).
	//   БОЛЕЗНЬ. Сдвиг получали ВСЕ записи снимка, а в нём лежат узлы РАЗНОЙ вложенности:
	//   `PlaceAt` двигает только прямых детей корневой канвы, а у вложенных (центр и правый край
	//   планки 43589 внутри `Pet_Evolution_Topbar_grp`; заголовок 43501, ручка `_DRAG` и крестик
	//   43889 внутри `Pet_Evolution_Top_TextMove_grp`) оффсеты ОТНОСИТЕЛЬНО группы — группа уже
	//   увезла их с собой. Добавка дельты сдвигала их ВТОРОЙ раз, и `ApplyPage` ставил их на
	//   «окно + дельта» от группы: при постановке рядом с клеткой (дельта ≈ −190,−96) шапка,
	//   заголовок и крестик уходили за верх экрана, оставался один левый край планки (его
	//   `ApplyPage` не трогает). При дельте живого прогона (−94,−66) они просто съезжали.
	//   ЛЕЧЕНИЕ. Дельта — только тем, чей родитель корневая канва (ровно тем, кого двигает `PlaceAt`).
	const UWidget* RootW = Win ? Win->GetRootWidget() : nullptr;
	for (TPair<TWeakObjectPtr<UWidget>, FMargin>& Kv : LayoutBase)
	{
		const UWidget* W = Kv.Key.Get();
		if (!W || !RootW || W->GetParent() != RootW) { continue; }
		// ★ ИСПРАВЛЕНИЕ (дедуп №2, В2-B, U15): база сдвигается по тому же SPAN-безопасному правилу, что и сами узлы
		//   (`FAsdaPopupMenu::PlaceAt` → `AsdaUi::ShiftRootChildren`); было `Left/Top += d`.
		const UCanvasPanelSlot* CS = AsdaUi::SlotOf(W);
		Kv.Value = CS ? AsdaUi::ShiftedOffsets(Kv.Value, CS->GetAnchors(), Delta)
		              : FMargin(Kv.Value.Left + (float)Delta.X, Kv.Value.Top + (float)Delta.Y, Kv.Value.Right, Kv.Value.Bottom);
	}
	// Сторож `TrackWindowMove` увидит этот же сдвиг на ближайшем тике — чтобы он не применил
	//   дельту ВТОРОЙ раз, сразу приводим его точку отсчёта к факту.
	if (Win)
	{
		FVector2D TLNow(0.0, 0.0);
		if (FAsdaPopupMenu::GetTopLeft(Win, TLNow)) { LastWindowOrigin = TLNow; bWindowOriginKnown = true; }
	}
	// 2) Спутники 43400 и 43450 — в оригинале это контролы ВНУТРИ 43500 и едут с ним сами.
	int32 Moved = 0;
	for (UUserWidget* Sat : { (UUserWidget*)Grid, (UUserWidget*)Tip })
	{
		FVector2D TL(0.0, 0.0);
		if (Sat && FAsdaPopupMenu::GetTopLeft(Sat, TL))
		{
			FAsdaPopupMenu::PlaceAt(Sat, TL + Delta);
			++Moved;
		}
	}
	UE_LOG(LogAsdaPetEvo, Log,
		TEXT("[питомец-развитие] окно 43500 подвинуто снаружи на (%.0f,%.0f): раскладка обновлена ")
		TEXT("(узлов %d), спутников 43400/43450 сдвинуто %d"),
		Delta.X, Delta.Y, LayoutBase.Num(), Moved);
}

void UAsdaPetEvolutionSubsystem::CaptureLayoutBase()
{
	if (bLayoutCaptured) { return; }
	FMargin Tmp;
	for (const TObjectPtr<UImage>& P : WinPlanes)  { if (P) { AsdaUi::BaseOffsetsOf(LayoutBase, P, Tmp); } }
	for (const TObjectPtr<UImage>& P : BarPlanes)  { if (P) { AsdaUi::BaseOffsetsOf(LayoutBase, P, Tmp); } }
	if (BarGrp)     { AsdaUi::BaseOffsetsOf(LayoutBase, BarGrp, Tmp); }
	if (TitleGrp)   { AsdaUi::BaseOffsetsOf(LayoutBase, TitleGrp, Tmp); }
	if (TxtTitle)   { AsdaUi::BaseOffsetsOf(LayoutBase, TxtTitle, Tmp); }
	if (TitleDrag)  { AsdaUi::BaseOffsetsOf(LayoutBase, TitleDrag, Tmp); }
	if (BtnX)       { AsdaUi::BaseOffsetsOf(LayoutBase, BtnX, Tmp); }
	if (TxtInfo)    { AsdaUi::BaseOffsetsOf(LayoutBase, TxtInfo, Tmp); }
	if (ImgInfoBg)  { AsdaUi::BaseOffsetsOf(LayoutBase, ImgInfoBg, Tmp); }
	if (BtnBaseGrp) { AsdaUi::BaseOffsetsOf(LayoutBase, BtnBaseGrp, Tmp); }
	bLayoutCaptured = true;
}

void UAsdaPetEvolutionSubsystem::ApplyPage(bool bWide)
{
	CaptureLayoutBase();
	bWidePage = bWide;

	const float WinW   = bWide ? (float)kWinWideW   : (float)kWinNarrowW;
	const float InfoW  = bWide ? (float)kInfoWideW  : (float)kInfoNarrowW;
	const float TitleW = bWide ? (float)kTitleWideW : (float)kTitleNarrowW;
	const float BarW   = bWide ? (float)kBarWideW   : (float)kBarNarrowW;

	// Окно 43500: `(*(vtbl+20))(m_pWnd, 776 | 390, −1)` (ui_core.c:90379 / :90391).
	StretchFrameWidth(LayoutBase, WinPlanes, WinW);
	// Планка 43589: 670 | 284 (ui_core.c:90382 / :90394). Группа планки — полная ширина окна.
	StretchBarWidth(LayoutBase, BarPlanes, BarW);
	SetSlotWidth(LayoutBase, BarGrp, WinW);
	// Заголовок 43501: 772 | 390 (ui_core.c:90381 / :90393). «Крестик» 43889 — его ребёнок в `.JUI`,
	//   поэтому в оригинале он едет вместе с заголовком; в расплющенной иерархии порта двигаем сами.
	SetSlotWidth(LayoutBase, TitleGrp, TitleW);
	SetSlotWidth(LayoutBase, TxtTitle, TitleW);
	SetSlotWidth(LayoutBase, TitleDrag, TitleW);
	SetSlotX(LayoutBase, BtnX, (float)kTitleNarrowW, TitleW);
	// Подсказка 43877: 752 | 368 (ui_core.c:90380 / :90392); подложка и текст тянутся вместе.
	// ★ Эталон задаёт ширину ОДНОМУ контролу 43877 (752 | 368). В расплющенной иерархии порта их
	//   два — подложка и текст, и в `.JUI` они авторски разной ширины (368 и 363). Поэтому тянем
	//   ОБА на одну и ту же ДЕЛЬТУ от их авторских ширин, а не подгоняем константой: прежняя
	//   `InfoW − 5` была выдумана (ревизия волны 5, МЕЛКОЕ-2).
	{
		const float InfoDelta = InfoW - (float)kInfoNarrowW;
		FMargin B;
		if (ImgInfoBg && AsdaUi::BaseOffsetsOf(LayoutBase, ImgInfoBg, B))
		{
			AsdaUi::SetSlotRect(ImgInfoBg, FVector2D(B.Left, B.Top), FVector2D(B.Right + InfoDelta, B.Bottom));
		}
		if (TxtInfo && AsdaUi::BaseOffsetsOf(LayoutBase, TxtInfo, B))
		{
			AsdaUi::SetSlotRect(TxtInfo, FVector2D(B.Left, B.Top), FVector2D(B.Right + InfoDelta, B.Bottom));
		}
	}
	// Группа кнопки 43888: x 341 | 144 (ui_core.c:90386 / :90398).
	SetSlotX(LayoutBase, BtnBaseGrp, (float)kBtnNarrowX, bWide ? (float)kBtnWideX : (float)kBtnNarrowX);

	// Правая панель 43580 и две стрелки (ui_core.c:90377, :90398-90400).
	AsdaUi::SetShownDecor(PanelRight, bWide);
	AsdaUi::SetShownHit(BtnExpand, !bWide);
	AsdaUi::SetShownHit(BtnCollapse, bWide);
}
