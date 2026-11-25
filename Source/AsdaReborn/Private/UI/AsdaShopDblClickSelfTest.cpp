// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «В КОРЗИНУ — ДВОЙНЫМ ЩЕЛЧКОМ» — `asda.Shop.SelfTestDblClick [delay=N] [quit]` (требование владельца 10.10.2026).
//
// Проверяет ПУТЁМ ИГРОКА (настоящие события Slate `SimulateClickAt` по живой геометрии, закон
//   `diagnostic-must-use-real-path`) три магазина, у которых нет своего самотеста кликов:
//   · магазин NPC 1300 (покупка): одиночный щелчок по витрине только выбирает, двойной кладёт РОВНО один раз,
//     одиночный по корзине только выбирает, двойной возвращает, бросок перетаскиванием кладёт сразу, ключ отката;
//   · премиум-магазин: одиночный по иконке карточки не кладёт, двойной кладёт ровно одну строку;
//   · магазин снаряжения 6800: одиночный только подсвечивает, двойной — примерка (= строка «Списка снаряжения»).
//   Лавку игрока (сторона гостя) проверяет её собственный самотест `asda.Trade.SelfTestStoreL2` (фаза «путём игрока»).
//   ★ Ревизия 10.10.2026 (МЕЛК-1/МЕЛК-2): пара рвётся — цепочки быстрых щелчков «товар → мимо → товар»,
//   «товар → другой товар → товар», «товар → смена списка → товар» (магазин NPC) и «карточка → пересборка страницы →
//   карточка» (премиум) НЕ кладут; четвёртый щелчок цепочки по тому же товару — контроль: пара после разрыва снова работает.
//   ★ Решение владельца 10.10.2026: магазин NPC в режиме ПРОДАЖИ — ОДИНОЧНЫЙ ПКМ по предмету кладёт в корзину продажи
//   (эталонный 0xF → vtbl+36 `npcShop_onItemSlotClick`), ключ `asda.Shop.DblClickToCart` на него не влияет; повтор по той же
//   ячейке — ничего (запись ушла в корзину целиком); предмет без записи в таблицах — молча; ПКМ по корзине — вернуть;
//   у покупки ПКМ ничего не делает (слот +36 пуст).
//   ★ Ревизия shopdbl2 Р-1: сумка поверх ячейки продажи — ПКМ и двойной ЛКМ по ней товар магазина НЕ кладут; контроль —
//   сумка в стороне, тот же ПКМ кладёт 1. ★ Р-2: на отложенном старте — повторная проверка офлайна (`RefuseDeferredStartWhenOnline`).
//   ★ Ревизия shop_offline_fix МЕЛК-3: «нажал на ячейке магазина — отпустил над сумкой»: ПКМ с отпусканием над сумкой вне
//   рамки магазина — корзина 0; ЛКМ в 2,5 px без порога — одиночный щелчок, корзина 0; контроль — то же нажатие ПКМ,
//   отпускание внутри ячейки, кладёт 1.
//   ★ Дедуп №2, В2-A (U1, 11.10.2026): раздел U1 первым шагом — таблица «пара щелчков через 0,15 / 0,30 / 0,45 / 0,60 с
//   сработала?» по распознавателям сумки, разборки, склада, сброса предмета, развития питомца и мини-статуса (зона и бафф):
//   «до» — ключ отката `asda.UI.DblClickLegacy 1` (прежние пороги окон), «после» — 0 (порог Windows `GetDoubleClickTime`).
//   Строка итога: «САМОТЕСТ-ДВОЙНОЙ ИТОГ: ok=N fail=M». Только офлайн.

#if !UE_BUILD_SHIPPING
#include "UI/AsdaShopDblClick.h"

#include "CoreMinimal.h"
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "UI/AsdaNpcShopSubsystem.h"
#include "UI/AsdaPremiumShopSubsystem.h"
#include "UI/AsdaEquipShopSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaDisassemblySubsystem.h"      // ★ U1 (В2-A): распознаватель окна разборки (таблица порогов)
#include "UI/AsdaWarehouseSubsystem.h"        // ★ U1 (В2-A): распознаватель склада
#include "UI/AsdaItemResetSubsystem.h"        // ★ U1 (В2-A): распознаватель сброса предмета 20200
#include "UI/AsdaPetEvolutionSubsystem.h"     // ★ U1 (В2-A): распознаватель сетки 43400
#include "UI/AsdaMiniStateSubsystem.h"        // ★ U1 (В2-A): распознаватели мини-статуса
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaNotice.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Npc/AsdaNpcDialogTypes.h"
#include "World/AsdaPlayerController.h"
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "Components/Widget.h"
#include "Components/PanelWidget.h"         // Р-1: корень окна сумки (сдвиг слотов)
#include "Components/CanvasPanelSlot.h"     // Р-1: слоты корневого канваса сумки
#include "Blueprint/UserWidget.h"           // Р-1: виджет окна из реестра (`FindWindow`)
#include "Containers/Ticker.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "GameFramework/InputSettings.h"   // ★ U1 (В2-A): прежний порог сумки/разборки — для оракула таблицы

DEFINE_LOG_CATEGORY_STATIC(LogAsdaShopDblTest, Log, All);

// ── Общий помощник самотестов: настоящий двойной щелчок Slate (два клика подряд, второй — сразу после завершения первого) ──
//   Состояние «второй клик ждёт» живёт в тикере; окно двойного щелчка Windows (0,5 с) перекрывает ~6 кадров с запасом.
void AsdaSimDoubleClickAt(UAsdaWindowSubsystem* WM, const FVector2D& Pos, bool bRight)
{
	if (!WM) { return; }
	WM->SimulateClickAt(Pos, /*HoldFrames*/ 2, bRight);
	TWeakObjectPtr<UAsdaWindowSubsystem> WW(WM);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, Pos, bRight](float) -> bool
	{
		UAsdaWindowSubsystem* W = WW.Get();
		if (!W) { return false; }
		if (W->IsSimClickPending()) { return true; }    // первый клик ещё не отпущен
		W->SimulateClickAt(Pos, /*HoldFrames*/ 2, bRight);
		return false;
	}));
}

// ── Общий помощник самотестов: цепочка щелчков подряд (контракт — в `AsdaShopDblClick.h`) ──
void AsdaSimClickChain(UAsdaWindowSubsystem* WM, TArray<FAsdaSimChainLink> Links, TSharedPtr<FAsdaSimChainResult> Out)
{
	TSharedRef<FAsdaSimChainResult> R = Out.IsValid() ? Out.ToSharedRef() : MakeShared<FAsdaSimChainResult>();
	R->bDone = false;
	R->ClickDoneSec.Reset();
	if (!WM || Links.Num() == 0) { R->bDone = true; return; }
	// Состояние прогона цепочки (живёт в тикере).
	struct FState
	{
		TArray<FAsdaSimChainLink> Links;   // звенья
		int32 Idx = 0;                     // следующее звено
		int32 Settle = 0;                  // кадров выдержки после отпускания (окно опрашивает мышь в своём Tick)
		bool bClickOut = false;            // щелчок заказан и ещё не отмечен в итоге
		double T0 = -1.0;                  // момент заказа первого щелчка
	};
	TSharedRef<FState> St = MakeShared<FState>();
	St->Links = MoveTemp(Links);
	TWeakObjectPtr<UAsdaWindowSubsystem> WW(WM);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, St, R](float) -> bool
	{
		UAsdaWindowSubsystem* W = WW.Get();
		if (!W) { R->bDone = true; return false; }
		if (W->IsSimClickPending()) { return true; }   // текущий щелчок ещё не отпущен
		const double Now = FPlatformTime::Seconds();
		if (St->bClickOut) { St->bClickOut = false; R->ClickDoneSec.Add(Now - St->T0); St->Settle = 0; }
		const bool bEnd = St->Idx >= St->Links.Num();
		const bool bAction = !bEnd && (bool)St->Links[St->Idx].Action;
		// Перед действием и перед концом цепочки — 2 кадра: окно должно успеть опросить отпускание последнего щелчка.
		if ((bEnd || bAction) && St->Settle < 2) { ++St->Settle; return true; }
		if (bEnd) { R->bDone = true; return false; }
		FAsdaSimChainLink& L = St->Links[St->Idx++];
		St->Settle = 0;
		if (bAction) { L.Action(); return true; }
		if (St->T0 < 0.0) { St->T0 = Now; }
		W->SimulateClickAt(L.Pos, /*HoldFrames*/ 2, L.bRight);
		St->bClickOut = true;
		return true;
	}));
}

bool AsdaSimGapPoint(const UWidget* A, const UWidget* B, FVector2D& Out)
{
	FVector2D AMn, AMx, BMn, BMx;
	if (!A || !B || !AsdaUi::WidgetAbsRect(A, AMn, AMx) || !AsdaUi::WidgetAbsRect(B, BMn, BMx)) { return false; }
	const float GapX = FMath::Max(BMn.X - AMx.X, AMn.X - BMx.X);   // > 0 — узлы разнесены по горизонтали
	const float GapY = FMath::Max(BMn.Y - AMx.Y, AMn.Y - BMx.Y);   // > 0 — по вертикали
	if (GapX >= 2.f && GapX >= GapY)
	{
		Out.X = (BMn.X > AMx.X) ? (AMx.X + BMn.X) * 0.5f : (BMx.X + AMn.X) * 0.5f;
		Out.Y = (AMn.Y + AMx.Y) * 0.5f;
		return true;
	}
	if (GapY >= 2.f)
	{
		Out.Y = (BMn.Y > AMx.Y) ? (AMx.Y + BMn.Y) * 0.5f : (BMx.Y + AMn.Y) * 0.5f;
		Out.X = (AMn.X + AMx.X) * 0.5f;
		return true;
	}
	return false;
}

namespace AsdaShopDblTestLocal
{
	// Центр виджета в абсолютных координатах Slate (те же, что у `SimulateClickAt`).
	static bool CenterOf(const UWidget* W, FVector2D& Out)
	{
		FVector2D Mn, Mx;
		if (!W || !AsdaUi::WidgetAbsRect(W, Mn, Mx)) { return false; }
		Out = (Mn + Mx) * 0.5f;
		return true;
	}

	// Пауза, после которой следующий щелчок по тому же узлу уже НЕ второй (окно двойного + запас).
	static float GapSec() { return (float)FAsdaShopDblClick::WindowSec() + 0.35f; }
}

// Состояние одного прогона: счётчики и очередь шагов. Шаг возвращает паузу (с) до следующего.
struct FAsdaShopDblSelfTest
{
	int32 Ok = 0;                                    // пройденные проверки
	int32 Fail = 0;                                  // проваленные проверки
	TArray<TFunction<float(UWorld*)>> Steps;         // шаги «путём игрока»
	int32 GoldWas = 0;                               // деньги до теста (вернуть в конце)
	int32 NpcId = -1;                                // выбранный NPC с ассортиментом
	int32 Cell = -1;                                 // ячейка витрины с товаром без нумпада
	int32 Cell2 = -1;                                // вторая такая ячейка (бросок / ключ отката)
	TSharedPtr<FAsdaSimChainResult> Chain;           // итог текущей цепочки щелчков (планировщик ждёт её конца)
	//   ★ R5-fix-b 13.11.2026 (заключительный заход): случай-цепочка доказывает «НЕ кладёт» только если щелчок перед снимком отпущен ВНУТРИ окна двойного
	//   (0,5 с). Под нагрузкой (три игры, ~15 к/с) четыре щелчка по 4 кадра не укладывались (0,595 / 0,521 с) — опыт не
	//   состоялся, а проверка падала. Такой случай повторяется заново (до kChainTries попыток, пауза — окно двойного + 0,35 с);
	//   проверка и её условия те же, в счёт идёт последняя попытка.
	static constexpr int32 kChainTries = 4;          // попыток случая-цепочки
	int32 ChainTries = 0;                            // сделано повторов текущего случая
	int32 Rewind = 0;                                // планировщику: вернуться на столько шагов назад (повтор случая)
	int32 SnapCount = -1;                            // счётчик корзины в момент снимка (после третьего щелчка цепочки)
	int32 SellAbs = -1;                              // индекс предмета X в витрине продажи
	int32 SellAbsNoRec = -1;                         // индекс предмета без записи в таблицах
	TArray<int32> BagCells;                          // ячейки сумки (контейнер 2), занятые тестом — очистить в конце
	static constexpr int32 kNoRecordCode = 1999999999;   // код вне всех таблиц (эталонный `slotKind` 0)
	bool bInvWasOpen = false;                        // Р-1: была ли сумка открыта до случая «сумка поверх» (вернуть как было)
	FVector2D CoverPt = FVector2D::ZeroVector;       // Р-1: точка — центр ячейки продажи, накрытый сумкой
	TArray<TPair<TWeakObjectPtr<UCanvasPanelSlot>, FMargin>> Shifted;   // Р-1: сдвинутые слоты сумки и прежние отступы (откат)
	FVector2D EdgeCell = FVector2D::ZeroVector;      // МЕЛК-3: центр ячейки продажи, у которого стоит край сумки
	FVector2D EdgePress = FVector2D::ZeroVector;     // МЕЛК-3: точка нажатия — ячейка продажи у самого края сумки (ловит магазин)
	FVector2D EdgeRelease = FVector2D::ZeroVector;   // МЕЛК-3: точка отпускания в 2,5 px от нажатия — до нажатия её ловит сумка (порог 4 px не пройден)
	FVector2D BagOut = FVector2D(-1.f, -1.f);        // МЕЛК-3: точка над сумкой вне рамки магазина (её ловит сумка и после подъёма магазина)
	bool bEdgeOpenedBag = false;                     // МЕЛК-3: сумку открыл сам случай «нажал на магазине — отпустил над сумкой» (закрыть после)

	// Р-1: сдвинуть окно (все слоты детей корневого канваса) на абсолютную дельту — тот же приём, что `MoveCover`
	//   самотеста подъёма (`AsdaRaiseClickSelfTest.cpp`): масштаб из живой геометрии корня, растянутые слоты — обоими краями.
	void ShiftWindow(UAsdaWindowSubsystem* WM, FName Id, FVector2D AbsDelta)
	{
		UUserWidget* Wd = WM ? WM->FindWindow(Id) : nullptr;
		UPanelWidget* Root = Wd ? Cast<UPanelWidget>(Wd->GetRootWidget()) : nullptr;
		if (!Root) { return; }
		float Scale = (float)Root->GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale();   // абс. единиц на единицу канвы
		if (Scale <= KINDA_SMALL_NUMBER) { Scale = 1.f; }
		const FVector2D Local = AbsDelta / Scale;   // дельта в единицах канвы
		for (int32 i = 0; i < Root->GetChildrenCount(); ++i)
		{
			UWidget* Ch = Root->GetChildAt(i);
			UCanvasPanelSlot* Sl = Ch ? Cast<UCanvasPanelSlot>(Ch->Slot) : nullptr;
			if (!Sl) { continue; }
			const FAnchors An = Sl->GetAnchors();
			FMargin M = Sl->GetOffsets();
			Shifted.Emplace(Sl, M);
			M.Left += Local.X;
			if (!FMath::IsNearlyEqual(An.Minimum.X, An.Maximum.X)) { M.Right -= Local.X; }
			M.Top += Local.Y;
			if (!FMath::IsNearlyEqual(An.Minimum.Y, An.Maximum.Y)) { M.Bottom -= Local.Y; }
			Sl->SetOffsets(M);
		}
	}
	// Р-1: откатить сдвиг (в обратном порядке — первая запись хранит исходную позицию).
	void UndoShift()
	{
		for (int32 i = Shifted.Num() - 1; i >= 0; --i)
		{
			if (UCanvasPanelSlot* Sl = Shifted[i].Key.Get()) { Sl->SetOffsets(Shifted[i].Value); }
		}
		Shifted.Reset();
	}

	// Одна проверка.
	void Expect(bool bCond, const FString& Name, const FString& Detail)
	{
		if (bCond) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaShopDblTest, Log, TEXT("САМОТЕСТ-ДВОЙНОЙ %s: %s — %s"), bCond ? TEXT("ok") : TEXT("ПРОВАЛ"), *Name, *Detail);
	}

	// Подобрать NPC и ячейки витрины, у товара которых эталон НЕ спрашивает количество (иначе вместо
	//   корзины откроется нумпад): `FlagWord176 != 0` либо не торгуемый штучно (`TradableFlag == 0`).
	bool PickNpc(UGameInstance* GI)
	{
		UDataTable* DT = LoadObject<UDataTable>(nullptr, TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcShopList.DT_NpcShopList"));
		UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		if (!DT || !IT) { return false; }
		for (const TPair<FName, uint8*>& P : DT->GetRowMap())
		{
			const FAsdaNpcShopRow* Row = reinterpret_cast<const FAsdaNpcShopRow*>(P.Value);
			if (!Row) { continue; }
			int32 A = -1, B = -1;
			for (int32 i = 0; i < Row->ItemCodes.Num() && i < 36 && B < 0; ++i)
			{
				const FAsdaItemRecord* R = IT->GetItem(Row->ItemCodes[i]);
				if (!R || R->Price <= 0 || IT->IsSowelCode(Row->ItemCodes[i])) { continue; }
				if (R->FlagWord176 == 0 && R->TradableFlag != 0) { continue; }   // спросит количество
				if (A < 0) { A = i; } else { B = i; }
			}
			if (B >= 0) { NpcId = Row->NpcId; Cell = A; Cell2 = B; return true; }
		}
		return false;
	}

	// ★ Ревизия 10.10.2026 (МЕЛК-1/МЕЛК-2). Случай «пара рвётся»: Prepare — подготовка (корзина пуста), MakeLinks — цепочка
	//   «товар → <разрыв> → товар → снимок → товар» (разрыв — щелчок мимо / по другому товару / действие), Count — счётчик корзины.
	//   Ждём: снимок 0 (НЕ положило), в конце 1 (четвёртый щелчок с третьим — двойной: контроль, что щелчки вообще доходят),
	//   и третий щелчок отпущен раньше, чем истекло окно двойного от первого (иначе «не кладёт» ничего не доказывает).
	void AddChainCase(const FString& Name,
		TFunction<bool(UWorld*)> Prepare,
		TFunction<bool(UWorld*, TArray<FAsdaSimChainLink>&)> MakeLinks,
		TFunction<int32(UWorld*)> Count)
	{
		Steps.Add([this, Name, Prepare, MakeLinks](UWorld* W) -> float
		{
			UAsdaWindowSubsystem* WM = W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
			TArray<FAsdaSimChainLink> Links;
			SnapCount = -1;
			Chain.Reset();
			if (!WM || !Prepare(W) || !MakeLinks(W, Links))
			{
				Expect(false, Name, TEXT("подготовка: нет окна/живой геометрии/промежутка между ячейками"));
				return 0.1f;
			}
			Chain = MakeShared<FAsdaSimChainResult>();
			AsdaSimClickChain(WM, MoveTemp(Links), Chain);
			return 0.05f;
		});
		Steps.Add([this, Name, Count](UWorld* W) -> float
		{
			if (!Chain.IsValid()) { return 0.05f; }   // подготовка уже засчитана провалом
			const int32 Final = Count(W);
			const double Win = FAsdaShopDblClick::WindowSec();
			const TArray<double>& T = Chain->ClickDoneSec;
			// Щелчок перед снимком — предпоследний (у разрыва-щелчка их 4, у разрыва-действия 3).
			const double Span = (T.Num() >= 3) ? T[T.Num() - 2] : -1.0;
			const bool bFast = Span >= 0.0 && Span <= Win;
			// ★ R5-fix-b 13.11.2026 (заключительный заход, третья запись), ревизия REVIEW6 С-1: повтор — только при чистом снимке (корзина 0): товар до последнего щелчка — дефект при любой скорости.
			if (Chain->bDone && !bFast && SnapCount == 0 && ChainTries < kChainTries - 1)   // ★ R5-fix-b 13.11.2026 (заключительный заход): опыт не состоялся — случай заново
			{
				++ChainTries;
				UE_LOG(LogAsdaShopDblTest, Log, TEXT("САМОТЕСТ-ДВОЙНОЙ повтор %d/%d: %s — щелчок перед снимком отпущен через %.3f с при окне %.3f с (кадры медленные), случай заново"),
					ChainTries, kChainTries - 1, *Name, Span, Win);
				Chain.Reset();
				Rewind = 2;
				return AsdaShopDblTestLocal::GapSec();
			}
			ChainTries = 0;
			Expect(Chain->bDone && bFast && SnapCount == 0 && Final == 1, Name,
				FString::Printf(TEXT("до последнего щелчка корзина=%d (ждём 0), после него=%d (ждём 1); щелчок перед снимком отпущен через %.3f с от 1-го при окне %.3f с, щелчков %d"),
					SnapCount, Final, Span, Win, T.Num()));
			Chain.Reset();
			return 0.05f;
		});
		Steps.Add([](UWorld*) -> float { return AsdaShopDblTestLocal::GapSec(); });   // пауза: следующий случай начинается с «чистой» пары
	}

	// ★ U1 (В2-A дедупа №2): таблица порогов двойного щелчка по окнам — «до» (ключ отката) и «после» (порог Windows).
	//   Оракул независимый: «сработал» ⇔ Δt ≤ порога строки (прежний порог окна — числом из отчёта аудита U1, а не из кода окна).
	void RunU1Table(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI) { Expect(false, TEXT("U1: подготовка"), TEXT("нет GameInstance")); return; }
		struct FRow { const TCHAR* Name; FAsdaDblClick Dbl; double OldSec; const TCHAR* OldWhat; };
		TArray<FRow> Rows;
		const double InputDbl = (double)GetDefault<UInputSettings>()->DoubleClickTime;   // прежний порог сумки и разборки (0,2 с из DefaultInput.ini)
		if (UAsdaInventorySubsystem* S = GI->GetSubsystem<UAsdaInventorySubsystem>())     { Rows.Add({ TEXT("сумка (ячейка)"), S->DebugDblClickProbe(), InputDbl, TEXT("UInputSettings") }); }
		if (UAsdaDisassemblySubsystem* S = GI->GetSubsystem<UAsdaDisassemblySubsystem>()) { Rows.Add({ TEXT("разборка 32004"), S->DebugDblClickProbe(), InputDbl, TEXT("UInputSettings") }); }
		if (UAsdaWarehouseSubsystem* S = GI->GetSubsystem<UAsdaWarehouseSubsystem>())     { Rows.Add({ TEXT("склад (ячейка)"), S->DebugDblClickProbe(), 0.35, TEXT("литерал") }); }
		if (UAsdaItemResetSubsystem* S = GI->GetSubsystem<UAsdaItemResetSubsystem>())     { Rows.Add({ TEXT("сброс 20200"), S->DebugDblClickProbe(), 0.35, TEXT("kDoubleClickSec") }); }
		if (UAsdaPetEvolutionSubsystem* S = GI->GetSubsystem<UAsdaPetEvolutionSubsystem>()) { Rows.Add({ TEXT("питомец 43400"), S->DebugDblClickProbe(), 0.5, TEXT("литерал") }); }
		if (UAsdaMiniStateSubsystem* S = GI->GetSubsystem<UAsdaMiniStateSubsystem>())
		{
			Rows.Add({ TEXT("мини-статус (зона)"), S->DebugDblClickProbe(0), 0.5, TEXT("DoubleClickSec, ИГРОВОЕ время") });
			Rows.Add({ TEXT("мини-статус (бафф)"), S->DebugDblClickProbe(1), 0.5, TEXT("DoubleClickSec, ИГРОВОЕ время") });
		}
		Expect(Rows.Num() == 7, TEXT("U1: распознаватели всех окон найдены"), FString::Printf(TEXT("%d из 7"), Rows.Num()));
		const double Win = FAsdaDblClick::WindowSec();
		static const double kDt[4] = { 0.15, 0.30, 0.45, 0.60 };
		UE_LOG(LogAsdaShopDblTest, Log, TEXT("САМОТЕСТ-ДВОЙНОЙ U1 ТАБЛИЦА: порог Windows %.3f с; пары щелчков через 0,15 / 0,30 / 0,45 / 0,60 с; «+» — двойной сработал, «−» — нет"), Win);
		for (const FRow& R : Rows)
		{
			FString Before, After;
			bool bBeforeOk = true, bAfterOk = true;
			AsdaCVarSave::SetForTest(TEXT("asda.UI.DblClickLegacy"), 1);
			for (const double Dt : kDt)
			{
				const bool b = R.Dbl.ProbePair(Dt);
				Before += b ? TEXT("+") : TEXT("−");
				bBeforeOk &= (b == (Dt <= R.OldSec + 1e-9));
			}
			AsdaCVarSave::SetForTest(TEXT("asda.UI.DblClickLegacy"), 0);
			for (const double Dt : kDt)
			{
				const bool b = R.Dbl.ProbePair(Dt);
				After += b ? TEXT("+") : TEXT("−");
				bAfterOk &= (b == (Dt <= Win + 1e-9));
			}
			UE_LOG(LogAsdaShopDblTest, Log, TEXT("САМОТЕСТ-ДВОЙНОЙ U1 | %s | было %.2f с (%s) | до: %s | после: %s"),
				R.Name, R.OldSec, R.OldWhat, *Before, *After);
			Expect(bBeforeOk, FString::Printf(TEXT("U1 %s: ключ отката 1 — прежний порог %.2f с"), R.Name, R.OldSec), Before);
			Expect(bAfterOk, FString::Printf(TEXT("U1 %s: порог Windows %.3f с"), R.Name, Win), After);
		}
		AsdaCVarSave::SetForTest(TEXT("asda.UI.DblClickLegacy"), 0);   // исходное значение и SetBy вернёт финальный `RestoreAll`
	}

	void Build()
	{
		using namespace AsdaShopDblTestLocal;
		// ═══ 0. U1 (В2-A дедупа №2): таблица порогов двойного щелчка по окнам ═══
		Steps.Add([this](UWorld* W) -> float { RunU1Table(W); return 0.05f; });
		// ═══ 1. Магазин NPC 1300 — покупка ═══
		Steps.Add([this](UWorld* W) -> float
		{
			UGameInstance* GI = W->GetGameInstance();
			UAsdaNpcShopSubsystem* S = GI->GetSubsystem<UAsdaNpcShopSubsystem>();
			UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>();
			if (!S || !Inv || !PickNpc(GI))
			{
				Expect(false, TEXT("магазин NPC: подготовка"), TEXT("нет подсистем либо NPC с товаром без нумпада"));
				return 0.1f;
			}
			GoldWas = Inv->GetGold();
			Inv->SetGoldDisplay(100000000);
			S->OpenShop(5, NpcId);
			Expect(S->IsOpen() && S->DebugItemCode(Cell) > 0, TEXT("магазин NPC: открыт (покупка)"),
				FString::Printf(TEXT("NPC %d, ячейки %d/%d, коды %d/%d"), NpcId, Cell, Cell2, S->DebugItemCode(Cell), S->DebugItemCode(Cell2)));
			return 0.6f;
		});
		auto ClickNpc = [this](UWorld* W, bool bCart, int32 C, bool bDouble) -> float
		{
			UGameInstance* GI = W->GetGameInstance();
			UAsdaNpcShopSubsystem* S = GI->GetSubsystem<UAsdaNpcShopSubsystem>();
			UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
			FVector2D P;
			if (!S || !WM || !CenterOf(bCart ? S->DebugCartIcon(C) : S->DebugGridIcon(C), P))
			{
				Expect(false, TEXT("магазин NPC: клик"), TEXT("нет живой геометрии ячейки"));
				return 0.1f;
			}
			if (bDouble) { AsdaSimDoubleClickAt(WM, P, false); } else { WM->SimulateClickAt(P, 2, false); }
			return GapSec();
		};
		// 1а. Одиночный по витрине → только выбор.
		Steps.Add([ClickNpc, this](UWorld* W) { return ClickNpc(W, false, Cell, false); });
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaNpcShopSubsystem* S = W->GetGameInstance()->GetSubsystem<UAsdaNpcShopSubsystem>();
			Expect(S && S->DebugCartFilled() == 0 && S->DebugSelGridCell() == Cell,
				TEXT("магазин NPC: одиночный щелчок по витрине — выбор (курсор), в корзину НЕ кладёт (эталон 0xB)"),
				FString::Printf(TEXT("корзина=%d выбрана=%d"), S ? S->DebugCartFilled() : -1, S ? S->DebugSelGridCell() : -2));
			return 0.05f;
		});
		// 1б. Двойной по витрине → ровно одна позиция.
		Steps.Add([ClickNpc, this](UWorld* W) { return ClickNpc(W, false, Cell, true); });
		Steps.Add([this](UWorld* W) -> float
		{
			UGameInstance* GI = W->GetGameInstance();
			UAsdaNpcShopSubsystem* S = GI->GetSubsystem<UAsdaNpcShopSubsystem>();
			UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
			Expect(S && S->DebugCartFilled() == 1 && WM && !WM->IsNumPadOpen(),
				TEXT("магазин NPC: двойной щелчок по витрине — в корзину РОВНО один раз (эталон 0xD → vtbl+44)"),
				FString::Printf(TEXT("корзина=%d нумпад=%d"), S ? S->DebugCartFilled() : -1, (WM && WM->IsNumPadOpen()) ? 1 : 0));
			return 0.05f;
		});
		// 1в. Ещё один одиночный (после паузы) — корзина не растёт.
		Steps.Add([ClickNpc, this](UWorld* W) { return ClickNpc(W, false, Cell, false); });
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaNpcShopSubsystem* S = W->GetGameInstance()->GetSubsystem<UAsdaNpcShopSubsystem>();
			Expect(S && S->DebugCartFilled() == 1, TEXT("магазин NPC: одиночный после паузы — корзина не растёт"),
				FString::Printf(TEXT("корзина=%d"), S ? S->DebugCartFilled() : -1));
			return 0.05f;
		});
		// 1г. Одиночный по корзине → только выбор; двойной → возврат.
		Steps.Add([ClickNpc](UWorld* W) { return ClickNpc(W, true, 0, false); });
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaNpcShopSubsystem* S = W->GetGameInstance()->GetSubsystem<UAsdaNpcShopSubsystem>();
			Expect(S && S->DebugCartFilled() == 1 && S->DebugSelCartCell() == 0,
				TEXT("магазин NPC: одиночный по корзине — только выбор (эталон 0xB), позиция на месте"),
				FString::Printf(TEXT("корзина=%d выбрана=%d"), S ? S->DebugCartFilled() : -1, S ? S->DebugSelCartCell() : -2));
			return 0.05f;
		});
		Steps.Add([ClickNpc](UWorld* W) { return ClickNpc(W, true, 0, true); });
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaNpcShopSubsystem* S = W->GetGameInstance()->GetSubsystem<UAsdaNpcShopSubsystem>();
			Expect(S && S->DebugCartFilled() == 0, TEXT("магазин NPC: двойной по корзине — позиция возвращена (эталон 0xD → vtbl+48)"),
				FString::Printf(TEXT("корзина=%d"), S ? S->DebugCartFilled() : -1));
			return 0.05f;
		});
		// 1д. Бросок перетаскиванием витрина → корзина кладёт сразу (эталонное событие 7 не тронуто).
		Steps.Add([this](UWorld* W) -> float
		{
			UGameInstance* GI = W->GetGameInstance();
			UAsdaNpcShopSubsystem* S = GI->GetSubsystem<UAsdaNpcShopSubsystem>();
			UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
			FVector2D A, B;
			if (!S || !WM || !CenterOf(S->DebugGridIcon(Cell2), A) || !CenterOf(S->DebugCartIcon(0), B))
			{
				Expect(false, TEXT("магазин NPC: перетаскивание"), TEXT("нет живой геометрии"));
				return 0.1f;
			}
			WM->SimulateDrag(A, B, 20);
			return 0.8f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaNpcShopSubsystem* S = W->GetGameInstance()->GetSubsystem<UAsdaNpcShopSubsystem>();
			Expect(S && S->DebugCartFilled() == 1, TEXT("магазин NPC: перетаскивание витрина → корзина кладёт сразу (не сломано)"),
				FString::Printf(TEXT("корзина=%d"), S ? S->DebugCartFilled() : -1));
			AsdaCVarSave::SetForTest(TEXT("asda.Shop.DblClickToCart"), 0);
			return 0.05f;
		});
		// 1е. Ключ отката: `asda.Shop.DblClickToCart 0` — одиночный кладёт, как до правки.
		Steps.Add([ClickNpc, this](UWorld* W) { return ClickNpc(W, false, Cell, false); });
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaNpcShopSubsystem* S = W->GetGameInstance()->GetSubsystem<UAsdaNpcShopSubsystem>();
			Expect(S && S->DebugCartFilled() == 2, TEXT("магазин NPC: ключ отката asda.Shop.DblClickToCart 0 — одиночный кладёт"),
				FString::Printf(TEXT("корзина=%d"), S ? S->DebugCartFilled() : -1));
			AsdaCVarSave::SetForTest(TEXT("asda.Shop.DblClickToCart"), 1);
			return 0.3f;
		});
		// 1ж–1и. ★ Ревизия 10.10.2026: пара рвётся (МЕЛК-1 — щелчок мимо ячейки, МЕЛК-2 — список пересобран). Перед каждым
		//   случаем витрина открывается заново (корзина пуста), поэтому «в корзине 1» однозначно значит «положило один раз».
		auto NpcReopen = [this](UWorld* W) -> bool
		{
			UAsdaNpcShopSubsystem* S = W->GetGameInstance()->GetSubsystem<UAsdaNpcShopSubsystem>();
			if (!S) { return false; }
			S->OpenShop(5, NpcId);
			return S->IsOpen() && S->DebugCartFilled() == 0;
		};
		auto NpcCart = [](UWorld* W) -> int32
		{
			UAsdaNpcShopSubsystem* S = W->GetGameInstance()->GetSubsystem<UAsdaNpcShopSubsystem>();
			return S ? S->DebugCartFilled() : -1;
		};
		// Цепочка «ячейка → <разрыв> → ячейка → снимок → ячейка»; разрыв — щелчок в точке BreakPos либо действие BreakAction.
		auto NpcLinks = [this, NpcCart](UWorld* W, TArray<FAsdaSimChainLink>& L, const FVector2D* BreakPos, TFunction<void()> BreakAction) -> bool
		{
			UAsdaNpcShopSubsystem* S = W->GetGameInstance()->GetSubsystem<UAsdaNpcShopSubsystem>();
			FVector2D P;
			if (!S || !AsdaShopDblTestLocal::CenterOf(S->DebugGridIcon(Cell), P)) { return false; }
			FAsdaSimChainLink Item; Item.Pos = P;
			FAsdaSimChainLink Brk;
			if (BreakPos) { Brk.Pos = *BreakPos; } else { Brk.Action = BreakAction; }
			FAsdaSimChainLink Snap; Snap.Action = [this, NpcCart, W]() { SnapCount = NpcCart(W); };
			L = { Item, Brk, Item, Snap, Item };
			return true;
		};
		// 1ж. «товар → мимо → товар»: нажатие в промежутке между соседними ячейками витрины (фон окна).
		AddChainCase(TEXT("магазин NPC: «товар → щелчок мимо ячейки → товар» быстрее окна двойного — НЕ кладёт (МЕЛК-1); 4-й щелчок — двойной, кладёт (контроль)"),
			NpcReopen,
			[this, NpcLinks](UWorld* W, TArray<FAsdaSimChainLink>& L) -> bool
			{
				UAsdaNpcShopSubsystem* S = W->GetGameInstance()->GetSubsystem<UAsdaNpcShopSubsystem>();
				if (!S) { return false; }
				const int32 Nb = ((Cell % 6) < 5) ? Cell + 1 : Cell - 1;   // соседняя ячейка той же строки (6 в строке)
				FVector2D Miss;
				const TCHAR* How = TEXT("промежуток соседних ячеек");
				if (!AsdaSimGapPoint(S->DebugGridIcon(Cell), S->DebugGridIcon(Nb), Miss))
				{
					// Ячейки витрины стоят вплотную (32 px с шагом 32) — тогда фон окна слева от первой ячейки той же строки.
					FVector2D Mn, Mx;
					if (!AsdaUi::WidgetAbsRect(S->DebugGridIcon(Cell - (Cell % 6)), Mn, Mx)) { return false; }
					Miss = FVector2D(Mn.X - 6.f, (Mn.Y + Mx.Y) * 0.5f);
					How = TEXT("фон окна в 6 px левее первой ячейки строки");
				}
				UE_LOG(LogAsdaShopDblTest, Log, TEXT("САМОТЕСТ-ДВОЙНОЙ: точка «мимо» (%.0f,%.0f) — %s (ячейка %d)"), Miss.X, Miss.Y, How, Cell);
				return NpcLinks(W, L, &Miss, nullptr);
			},
			NpcCart);
		// 1з. «товар → другой товар → товар».
		AddChainCase(TEXT("магазин NPC: «товар → другой товар → товар» — НЕ кладёт ни один; 4-й щелчок — двойной, кладёт (контроль)"),
			NpcReopen,
			[this, NpcLinks](UWorld* W, TArray<FAsdaSimChainLink>& L) -> bool
			{
				UAsdaNpcShopSubsystem* S = W->GetGameInstance()->GetSubsystem<UAsdaNpcShopSubsystem>();
				FVector2D Other;
				if (!S || !AsdaShopDblTestLocal::CenterOf(S->DebugGridIcon(Cell2), Other)) { return false; }
				return NpcLinks(W, L, &Other, nullptr);
			},
			NpcCart);
		// 1и. «товар → смена списка → товар»: витрина пересобрана между щелчками (`OpenShop` поверх открытой — тот же путь,
		//   что новое открытие магазина NPC: список, страницы, корзина заново).
		AddChainCase(TEXT("магазин NPC: «товар → список пересобран → товар» — НЕ кладёт (МЕЛК-2); 4-й щелчок — двойной, кладёт (контроль)"),
			NpcReopen,
			[this, NpcLinks](UWorld* W, TArray<FAsdaSimChainLink>& L) -> bool
			{
				UAsdaNpcShopSubsystem* S = W->GetGameInstance()->GetSubsystem<UAsdaNpcShopSubsystem>();
				if (!S) { return false; }
				TWeakObjectPtr<UAsdaNpcShopSubsystem> WS(S);
				const int32 Npc = NpcId;
				return NpcLinks(W, L, nullptr, [WS, Npc]() { if (WS.IsValid()) { WS->OpenShop(5, Npc); } });
			},
			NpcCart);
		// 1к. ★ Решение владельца 10.10.2026: ПКМ в магазине NPC. Эталон: событие 0xF окна 1300 → vtbl+36; у покупки слот ПУСТ.
		auto RClickNpc = [this](UWorld* W, bool bCart, int32 C) -> float
		{
			UGameInstance* GI = W->GetGameInstance();
			UAsdaNpcShopSubsystem* S = GI->GetSubsystem<UAsdaNpcShopSubsystem>();
			UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
			FVector2D P;
			if (!S || !WM || !AsdaShopDblTestLocal::CenterOf(bCart ? S->DebugCartIcon(C) : S->DebugGridIcon(C), P))
			{
				Expect(false, TEXT("магазин NPC: ПКМ"), FString::Printf(TEXT("нет живой геометрии ячейки %d (корзина=%d)"), C, bCart ? 1 : 0));
				return 0.1f;
			}
			WM->SimulateClickAt(P, 2, /*bRight*/ true);
			return AsdaShopDblTestLocal::GapSec();
		};
		auto CartIs = [this](UWorld* W, int32 Want, const TCHAR* Name)
		{
			UAsdaNpcShopSubsystem* S = W->GetGameInstance()->GetSubsystem<UAsdaNpcShopSubsystem>();
			Expect(S && S->DebugCartFilled() == Want, Name, FString::Printf(TEXT("корзина=%d (ждём %d)"), S ? S->DebugCartFilled() : -1, Want));
		};
		Steps.Add([this, NpcReopen](UWorld* W) -> float { NpcReopen(W); return 0.3f; });
		Steps.Add([this, RClickNpc](UWorld* W) { return RClickNpc(W, false, Cell); });
		Steps.Add([CartIs](UWorld* W) -> float { CartIs(W, 0, TEXT("магазин NPC, ПОКУПКА: одиночный ПКМ по товару — ничего (эталон: слот vtbl+36 пуст `0xE231F0`)")); return 0.05f; });
		// Подготовка продажи: предмет X (товар витрины без нумпада) и предмет без записи — в свободные ячейки сумки, витрина типа 6.
		Steps.Add([this](UWorld* W) -> float
		{
			UGameInstance* GI = W->GetGameInstance();
			UAsdaNpcShopSubsystem* S = GI->GetSubsystem<UAsdaNpcShopSubsystem>();
			UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>();
			const int32 X = S ? S->DebugItemCode(Cell) : 0;
			BagCells.Reset();
			for (int32 i = 0; Inv && i < UAsdaInventorySubsystem::kSpendSlots && BagCells.Num() < 2; ++i)
			{
				const FAsdaItemInstance* It = Inv->GetCell(2, i);
				if (!It || It->ItemCode <= 0) { BagCells.Add(i); }
			}
			if (!S || !Inv || X <= 0 || BagCells.Num() < 2)
			{
				Expect(false, TEXT("магазин NPC, ПРОДАЖА: подготовка"), TEXT("нет подсистем / товара / двух свободных ячеек сумки"));
				return 0.1f;
			}
			Inv->DebugPutItem(2, BagCells[0], X, 1);
			Inv->DebugPutItem(2, BagCells[1], kNoRecordCode, 1);
			S->CloseShop();
			S->OpenShop(6, NpcId);
			SellAbs = -1; SellAbsNoRec = -1;
			for (int32 a = 0; a < 36; ++a)
			{
				if (S->DebugItemCode(a) == X && SellAbs < 0) { SellAbs = a; }
				if (S->DebugItemCode(a) == kNoRecordCode && SellAbsNoRec < 0) { SellAbsNoRec = a; }
			}
			Expect(S->IsOpen() && SellAbs >= 0 && SellAbsNoRec >= 0 && S->DebugCartFilled() == 0, TEXT("магазин NPC, ПРОДАЖА: открыт (тип 6), предметы сумки на витрине"),
				FString::Printf(TEXT("код X=%d → индекс %d; код без записи → индекс %d; ячейки сумки %d/%d"), X, SellAbs, SellAbsNoRec, BagCells[0], BagCells[1]));
			if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>()) { WM->ClearMsgBoxQueue(TEXT("самотест ПКМ продажи")); }
			return 0.6f;
		});
		// ЛКМ одиночный — по-прежнему только выбор.
		Steps.Add([this, ClickNpc](UWorld* W) { return SellAbs >= 0 ? ClickNpc(W, false, SellAbs, false) : 0.05f; });
		Steps.Add([CartIs](UWorld* W) -> float { CartIs(W, 0, TEXT("магазин NPC, ПРОДАЖА: одиночный ЛКМ по предмету — только выбор, в корзину НЕ кладёт")); return 0.05f; });
		// Одиночный ПКМ — в корзину продажи ровно один раз.
		Steps.Add([this, RClickNpc](UWorld* W) { return SellAbs >= 0 ? RClickNpc(W, false, SellAbs) : 0.05f; });
		Steps.Add([this](UWorld* W) -> float
		{
			UGameInstance* GI = W->GetGameInstance();
			UAsdaNpcShopSubsystem* S = GI->GetSubsystem<UAsdaNpcShopSubsystem>();
			UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
			Expect(S && S->DebugCartFilled() == 1 && WM && !WM->IsNumPadOpen() && WM->GetMsgBoxQueueNum() == 0,
				TEXT("магазин NPC, ПРОДАЖА: ОДИНОЧНЫЙ ПКМ по предмету сумки — в корзину продажи РОВНО один раз (эталон 0xF → vtbl+36 `npcShop_onItemSlotClick`)"),
				FString::Printf(TEXT("корзина=%d нумпад=%d окон-сообщений=%d"), S ? S->DebugCartFilled() : -1, (WM && WM->IsNumPadOpen()) ? 1 : 0, WM ? WM->GetMsgBoxQueueNum() : -1));
			return 0.05f;
		});
		// Повтор ПКМ по той же ячейке: эталон переносит в корзину ВСЮ запись и обнуляет её (`memset(entry, 0, 0x14)`,
		//   npc_mob_pet.c:7406) — ячейка витрины пуста, повтор ничего не делает и окна не даёт. (Отказ 2312 у эталона — только
		//   докладка в ту же пару при НЕПУСТОЙ записи, npc_mob_pet.c:7341-7352; у ячеек сумки ключ пары = слот, запись после
		//   переноса пуста, так что ПКМ туда не доходит.)
		Steps.Add([this, RClickNpc](UWorld* W) { return SellAbs >= 0 ? RClickNpc(W, false, SellAbs) : 0.05f; });
		Steps.Add([this](UWorld* W) -> float
		{
			UGameInstance* GI = W->GetGameInstance();
			UAsdaNpcShopSubsystem* S = GI->GetSubsystem<UAsdaNpcShopSubsystem>();
			UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
			Expect(S && S->DebugCartFilled() == 1 && S->DebugItemCode(SellAbs) <= 0 && WM && WM->GetMsgBoxQueueNum() == 0,
				TEXT("магазин NPC, ПРОДАЖА: повторный ПКМ по той же ячейке — запись ушла в корзину целиком (эталон обнуляет её), ничего не кладёт, окна нет"),
				FString::Printf(TEXT("корзина=%d код ячейки=%d окон-сообщений=%d"), S ? S->DebugCartFilled() : -1, S ? S->DebugItemCode(SellAbs) : -1, WM ? WM->GetMsgBoxQueueNum() : -1));
			if (WM) { WM->ClearMsgBoxQueue(TEXT("самотест ПКМ продажи")); }
			return 0.3f;
		});
		// Предмет без записи в таблицах (`slotKind` 0) — эталон молча ничего не делает.
		Steps.Add([this, RClickNpc](UWorld* W) { return SellAbsNoRec >= 0 ? RClickNpc(W, false, SellAbsNoRec) : 0.05f; });
		Steps.Add([this](UWorld* W) -> float
		{
			UGameInstance* GI = W->GetGameInstance();
			UAsdaNpcShopSubsystem* S = GI->GetSubsystem<UAsdaNpcShopSubsystem>();
			UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
			Expect(S && S->DebugCartFilled() == 1 && WM && WM->GetMsgBoxQueueNum() == 0 && !WM->IsNumPadOpen(),
				TEXT("магазин NPC, ПРОДАЖА: ПКМ по предмету без записи в таблицах (эталонный slotKind 0) — молча, в корзину не кладёт"),
				FString::Printf(TEXT("корзина=%d окон-сообщений=%d"), S ? S->DebugCartFilled() : -1, WM ? WM->GetMsgBoxQueueNum() : -1));
			return 0.05f;
		});
		// ПКМ по корзине — вернуть (ветка 0x550…0x554 → внешний vtbl+48).
		Steps.Add([RClickNpc](UWorld* W) { return RClickNpc(W, true, 0); });
		Steps.Add([CartIs](UWorld* W) -> float { CartIs(W, 0, TEXT("магазин NPC, ПРОДАЖА: одиночный ПКМ по ячейке корзины — позиция возвращена (эталон 0xF → +36 → +48)")); return 0.05f; });
		// ★ Ревизия shopdbl2 Р-1 (10.10.2026): окно ВЫШЕ магазина перехватывает мышь. Сумку поднимаем над магазином и сдвигаем
		//   так, чтобы её верхняя полоса (без ячеек) легла на ячейку продажи; ПКМ и двойной ЛКМ по этой точке — корзина 0.
		//   Контроль: сумка убрана с ячейки — тот же ПКМ кладёт 1 (затем возвращаем позицию, чтобы дальше всё шло как было).
		Steps.Add([this](UWorld* W) -> float
		{
			UGameInstance* GI = W->GetGameInstance();
			UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
			UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>();
			bInvWasOpen = Inv && Inv->IsWindowOpen();
			if (Inv && !bInvWasOpen) { Inv->OpenWindow(); }
			if (WM) { WM->BringWindowToFront(FName(TEXT("INVEN_WIN"))); }
			return 0.3f;   // кадры на раскладку сумки (живая геометрия)
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UGameInstance* GI = W->GetGameInstance();
			UAsdaNpcShopSubsystem* S = GI->GetSubsystem<UAsdaNpcShopSubsystem>();
			UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
			FVector2D P, Mn, Mx;
			if (!S || !WM || SellAbs < 0 || !AsdaShopDblTestLocal::CenterOf(S->DebugGridIcon(SellAbs), P)
				|| !WM->GetUiZoneRect(FName(TEXT("INVEN_WIN")), Mn, Mx))
			{
				Expect(false, TEXT("магазин NPC, Р-1: сумка поверх ячейки"), TEXT("нет геометрии ячейки продажи или рамки сумки"));
				return 0.05f;
			}
			CoverPt = P;
			// Точка верхней полосы сумки (заголовок, ячеек нет: сетка начинается с y=320 окна) → на центр ячейки продажи.
			const FVector2D Q((Mn.X + Mx.X) * 0.5f, Mn.Y + 10.f);
			ShiftWindow(WM, FName(TEXT("INVEN_WIN")), P - Q);
			return 0.3f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaWindowSubsystem* WM = W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
			const FName Catch = WM ? WM->FindWindowCatchingAt(CoverPt) : NAME_None;
			Expect(Catch == FName(TEXT("INVEN_WIN")), TEXT("магазин NPC, Р-1: сумка лежит поверх ячейки продажи (точку ловит сумка)"),
				FString::Printf(TEXT("ловит=%s точка=(%.0f,%.0f)"), *Catch.ToString(), CoverPt.X, CoverPt.Y));
			if (WM) { WM->SimulateClickAt(CoverPt, 2, /*bRight*/ true); }
			return AsdaShopDblTestLocal::GapSec();
		});
		Steps.Add([CartIs](UWorld* W) -> float { CartIs(W, 0, TEXT("магазин NPC, Р-1: ПКМ по сумке, лежащей поверх ячейки продажи, — товар магазина НЕ кладётся (мышь у верхнего окна)")); return 0.05f; });
		Steps.Add([this](UWorld* W) -> float
		{
			if (UAsdaWindowSubsystem* WM = W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>()) { AsdaSimDoubleClickAt(WM, CoverPt, /*bRight*/ false); }
			return AsdaShopDblTestLocal::GapSec() + 0.2f;
		});
		Steps.Add([CartIs](UWorld* W) -> float { CartIs(W, 0, TEXT("магазин NPC, Р-1: двойной ЛКМ по сумке поверх ячейки продажи — товар магазина НЕ кладётся")); return 0.05f; });
		// Контроль: сумку вернули на место; если она и там накрывает ячейку — закрываем её (точку должен ловить магазин).
		Steps.Add([this](UWorld* W) -> float
		{
			UGameInstance* GI = W->GetGameInstance();
			UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
			UndoShift();
			if (WM && WM->FindWindowCatchingAt(CoverPt) != FName(TEXT("NPCSHOP_WIN")))
			{
				if (UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>()) { Inv->CloseWindow(); }
			}
			return 0.3f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaWindowSubsystem* WM = W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
			const FName Catch = WM ? WM->FindWindowCatchingAt(CoverPt) : NAME_None;
			Expect(Catch == FName(TEXT("NPCSHOP_WIN")), TEXT("магазин NPC, Р-1 (контроль): сумка убрана с ячейки — точку ловит магазин"),
				FString::Printf(TEXT("ловит=%s"), *Catch.ToString()));
			if (WM) { WM->SimulateClickAt(CoverPt, 2, /*bRight*/ true); }
			return AsdaShopDblTestLocal::GapSec();
		});
		Steps.Add([CartIs](UWorld* W) -> float { CartIs(W, 1, TEXT("магазин NPC, Р-1 (контроль): сумка в стороне — тот же ПКМ кладёт РОВНО один раз")); return 0.05f; });
		// Возврат позиции из корзины и сумки в исходное состояние: дальше идёт проверка ключа отката с корзины 0.
		Steps.Add([RClickNpc](UWorld* W) { return RClickNpc(W, true, 0); });
		// ★ МЕЛК-3 (ревизия shop_offline_fix, 10.10.2026): «нажал на ячейке магазина — отпустил над сумкой». Сумка лежит выше
		//   магазина, её левый край — у центра ячейки продажи (граница «магазин | сумка» — по живому хит-тесту окон).
		//   ⚠ Нажатие по магазину поднимает его наверх: в оригинале сразу (ui_core.c:104163-104170), в порту — на отпускании
		//   (`PendingRaiseWindowId`, до опроса мыши магазином). Поэтому точка, которую до нажатия ловила сумка, к отпусканию уже
		//   под магазином. Случаи:
		//   (а) ПКМ: нажатие в ячейке, отпускание над сумкой ВНЕ рамки магазина — другой контрол, щелчка нет (эталон
		//       `CControl_OnEvent15@0x12DC530`: нажатие и отпускание на ОДНОМ контроле) ⇒ корзина 0;
		//   (б) ЛКМ: захват в ячейке, отпускание в 2,5 px над бывшей сумкой (порог `kDragStartPixels` = 4 px не пройден) —
		//       одиночный щелчок по ячейке, только выбор (эталон 0xB), не двойной ⇒ корзина 0;
		//   (в) контроль пары к (а): то же нажатие ПКМ, отпускание в 2,5 px внутри той же ячейки ⇒ щелчок, корзина 1.
		//       (а) и (в) отличаются только точкой отпускания — это и доказывает, что решает контрол под отпусканием.
		//   Короткий ПКМ «в 2,5 px над сумкой» нарочно не проверяется: исход зависит от того, поднимет ли нажатие магазин
		//   (подъёма нет, пока видна подсказка, — эталон +0x1B0); оба исхода согласны с эталоном, однозначной проверки нет.
		Steps.Add([this](UWorld* W) -> float
		{
			UGameInstance* GI = W->GetGameInstance();
			UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
			UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>();
			bEdgeOpenedBag = Inv && !Inv->IsWindowOpen();
			if (bEdgeOpenedBag) { Inv->OpenWindow(); }
			if (WM) { WM->BringWindowToFront(FName(TEXT("INVEN_WIN"))); }
			return 0.3f;   // кадры на раскладку сумки (живая геометрия)
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UGameInstance* GI = W->GetGameInstance();
			UAsdaNpcShopSubsystem* S = GI->GetSubsystem<UAsdaNpcShopSubsystem>();
			UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
			FVector2D P, Mn, Mx;
			if (!S || !WM || SellAbs < 0 || !AsdaShopDblTestLocal::CenterOf(S->DebugGridIcon(SellAbs), P)
				|| !WM->GetUiZoneRect(FName(TEXT("INVEN_WIN")), Mn, Mx))
			{
				Expect(false, TEXT("магазин NPC, МЕЛК-3: сумка у края ячейки"), TEXT("нет геометрии ячейки продажи или рамки сумки"));
				return 0.05f;
			}
			EdgeCell = P;
			// Левый край верхней полосы сумки (без ячеек) — в 6 px справа от центра ячейки (ячейка 32 px — край внутри неё).
			//   Точную границу ищет следующий шаг по хит-тесту окон: рамка зоны и область, где окно ловит мышь, расходятся
			//   на пару пикселей (первый прогон: зона с 206,5, ловит уже с ≤ 206).
			ShiftWindow(WM, FName(TEXT("INVEN_WIN")), FVector2D(P.X + 6.f - Mn.X, P.Y - (Mn.Y + 10.f)));
			return 0.3f;
		});
		// Геометрия случаев: граница, точки нажатия/отпускания, точка над сумкой вне рамки магазина.
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaWindowSubsystem* WM = W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
			// Граница: первая точка (шаг 0,5 px) слева направо по строке центра ячейки, которую ловит сумка, при том что точку
			//   левее ловит магазин. Нажатие — 1,5 px левее границы, отпускание — 1 px правее (2,5 px < 4).
			float Edge = -1.f;   // абсолютная X границы; −1 — не найдена
			for (float X = EdgeCell.X - 14.f; WM && X <= EdgeCell.X + 14.f && Edge < 0.f; X += 0.5f)
			{
				if (WM->FindWindowCatchingAt(FVector2D(X, EdgeCell.Y)) == FName(TEXT("INVEN_WIN"))
					&& WM->FindWindowCatchingAt(FVector2D(X - 0.5f, EdgeCell.Y)) == FName(TEXT("NPCSHOP_WIN"))) { Edge = X; }
			}
			EdgePress = FVector2D(Edge - 1.5f, EdgeCell.Y);
			EdgeRelease = FVector2D(Edge + 1.f, EdgeCell.Y);
			Expect(Edge > 0.f, TEXT("магазин NPC, МЕЛК-3: найдена граница «магазин | сумка» в пределах ячейки"),
				FString::Printf(TEXT("центр ячейки (%.1f,%.1f), граница x=%.1f"), EdgeCell.X, EdgeCell.Y, Edge));
			// Точка над сумкой вне рамки магазина (сетка 8 px по рамке сумки): её ловит сумка и после подъёма магазина.
			FVector2D BMn, BMx, SMn, SMx;
			BagOut = FVector2D(-1.f, -1.f);
			if (WM && WM->GetUiZoneRect(FName(TEXT("INVEN_WIN")), BMn, BMx) && WM->GetUiZoneRect(FName(TEXT("NPCSHOP_WIN")), SMn, SMx))
			{
				for (float Y = BMn.Y + 4.f; Y < BMx.Y - 4.f && BagOut.X < 0.f; Y += 8.f)
				{
					for (float X = BMn.X + 4.f; X < BMx.X - 4.f && BagOut.X < 0.f; X += 8.f)
					{
						const bool bInShop = X >= SMn.X - 2.f && X <= SMx.X + 2.f && Y >= SMn.Y - 2.f && Y <= SMx.Y + 2.f;
						if (!bInShop && WM->FindWindowCatchingAt(FVector2D(X, Y)) == FName(TEXT("INVEN_WIN"))) { BagOut = FVector2D(X, Y); }
					}
				}
			}
			const FName AtPress = WM ? WM->FindWindowCatchingAt(EdgePress) : NAME_None;
			const FName AtRelease = WM ? WM->FindWindowCatchingAt(EdgeRelease) : NAME_None;
			Expect(AtPress == FName(TEXT("NPCSHOP_WIN")) && AtRelease == FName(TEXT("INVEN_WIN")) && BagOut.X >= 0.f,
				TEXT("магазин NPC, МЕЛК-3: нажатие ловит магазин; в 2,5 px и вне рамки магазина — сумка"),
				FString::Printf(TEXT("нажатие (%.1f,%.1f) ловит=%s; в 2,5 px (%.1f,%.1f) ловит=%s; вне магазина (%.0f,%.0f)"),
					EdgePress.X, EdgePress.Y, *AtPress.ToString(), EdgeRelease.X, EdgeRelease.Y, *AtRelease.ToString(), BagOut.X, BagOut.Y));
			return 0.05f;
		});
		// (а) ПКМ: нажатие в ячейке, отпускание над сумкой вне рамки магазина.
		Steps.Add([this](UWorld* W) -> float
		{
			if (UAsdaWindowSubsystem* WM = W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>())
			{
				if (BagOut.X >= 0.f) { WM->SimulateDrag(EdgePress, BagOut, 12, /*bRight*/ true); }
			}
			return AsdaShopDblTestLocal::GapSec();
		});
		Steps.Add([this, CartIs](UWorld* W) -> float
		{
			UAsdaWindowSubsystem* WM = W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
			const FName AtUp = WM ? WM->FindWindowCatchingAt(BagOut) : NAME_None;
			Expect(AtUp == FName(TEXT("INVEN_WIN")), TEXT("магазин NPC, МЕЛК-3 (а): точку отпускания и после подъёма магазина ловит сумка"),
				FString::Printf(TEXT("ловит=%s"), *AtUp.ToString()));
			CartIs(W, 0, TEXT("магазин NPC, МЕЛК-3 (а): ПКМ нажат на ячейке магазина, отпущен над сумкой — щелчка нет (эталон: нажатие и отпускание на одном контроле)"));
			if (WM) { WM->BringWindowToFront(FName(TEXT("INVEN_WIN"))); }   // сумка снова выше магазина — для (б)
			return 0.3f;
		});
		// (б) ЛКМ: захват в ячейке, отпускание в 2,5 px (порог не пройден) над местом, где до нажатия была сумка.
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaWindowSubsystem* WM = W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
			const FName AtRelease = WM ? WM->FindWindowCatchingAt(EdgeRelease) : NAME_None;
			Expect(AtRelease == FName(TEXT("INVEN_WIN")), TEXT("магазин NPC, МЕЛК-3 (б): до нажатия точку отпускания ловит сумка"),
				FString::Printf(TEXT("ловит=%s"), *AtRelease.ToString()));
			if (WM) { WM->SimulateDrag(EdgePress, EdgeRelease, 4, /*bRight*/ false); }
			return AsdaShopDblTestLocal::GapSec() + 0.2f;
		});
		Steps.Add([this, CartIs](UWorld* W) -> float
		{
			CartIs(W, 0, TEXT("магазин NPC, МЕЛК-3 (б): ЛКМ нажат на ячейке, отпущен в 2,5 px без порога — одиночный щелчок (выбор), в корзину не кладёт"));
			if (UAsdaWindowSubsystem* WM = W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>()) { WM->BringWindowToFront(FName(TEXT("INVEN_WIN"))); }
			return 0.3f;
		});
		// (в) Контроль пары к (а): то же нажатие ПКМ, отпускание в 2,5 px ВЛЕВО — внутри той же ячейки (её ловит магазин при
		//   любом z-порядке) ⇒ щелчок, корзина 1. От (а) отличается только точкой отпускания.
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaWindowSubsystem* WM = W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
			const FVector2D Back(EdgePress.X - 2.5f, EdgePress.Y);   // отпускание внутри ячейки
			const FName AtBack = WM ? WM->FindWindowCatchingAt(Back) : NAME_None;
			Expect(AtBack == FName(TEXT("NPCSHOP_WIN")), TEXT("магазин NPC, МЕЛК-3 (в, контроль): точку отпускания в 2,5 px внутри ячейки ловит магазин"),
				FString::Printf(TEXT("ловит=%s"), *AtBack.ToString()));
			if (WM) { WM->SimulateDrag(EdgePress, Back, 4, /*bRight*/ true); }
			return AsdaShopDblTestLocal::GapSec();
		});
		Steps.Add([CartIs](UWorld* W) -> float { CartIs(W, 1, TEXT("магазин NPC, МЕЛК-3 (в, контроль): ПКМ нажат и отпущен в той же ячейке (сдвиг 2,5 px) — кладёт РОВНО один раз")); return 0.05f; });
		Steps.Add([this](UWorld* W) -> float
		{
			UndoShift();
			if (bEdgeOpenedBag)
			{
				if (UAsdaInventorySubsystem* Inv = W->GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>()) { Inv->CloseWindow(); }
			}
			return 0.3f;
		});
		Steps.Add([RClickNpc](UWorld* W) { return RClickNpc(W, true, 0); });
		Steps.Add([this, CartIs](UWorld* W) -> float
		{
			CartIs(W, 0, TEXT("магазин NPC, Р-1 (контроль): позиция возвращена ПКМ по корзине"));
			if (UAsdaInventorySubsystem* Inv = W->GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>())
			{
				if (!bInvWasOpen && Inv->IsWindowOpen()) { Inv->CloseWindow(); }
			}
			return 0.2f;
		});
		// Ключ отката `asda.Shop.DblClickToCart 0` ПКМ не меняет — одиночный кладёт так же.
		Steps.Add([this, RClickNpc](UWorld* W) -> float
		{
			AsdaCVarSave::SetForTest(TEXT("asda.Shop.DblClickToCart"), 0);
			return SellAbs >= 0 ? RClickNpc(W, false, SellAbs) : 0.05f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaNpcShopSubsystem* S = W->GetGameInstance()->GetSubsystem<UAsdaNpcShopSubsystem>();
			AsdaCVarSave::SetForTest(TEXT("asda.Shop.DblClickToCart"), 1);
			Expect(S && S->DebugCartFilled() == 1, TEXT("магазин NPC, ПРОДАЖА: ключ asda.Shop.DblClickToCart 0 на ПКМ не влияет — одиночный ПКМ кладёт один раз"),
				FString::Printf(TEXT("корзина=%d"), S ? S->DebugCartFilled() : -1));
			// Уборка: сумка — как была.
			if (UAsdaInventorySubsystem* Inv = W->GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>())
			{
				for (const int32 c : BagCells) { Inv->DebugPutItem(2, c, 0, 0); }
			}
			BagCells.Reset();
			return 0.05f;
		});
		Steps.Add([](UWorld* W) -> float
		{
			if (UAsdaNpcShopSubsystem* S = W->GetGameInstance()->GetSubsystem<UAsdaNpcShopSubsystem>()) { S->CloseShop(); }
			return 0.3f;
		});

		// ═══ 2. Премиум-магазин ═══
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaPremiumShopSubsystem* P = W->GetGameInstance()->GetSubsystem<UAsdaPremiumShopSubsystem>();
			if (P) { P->OpenWindow(false); }
			Expect(P && P->IsOpen() && P->DebugCardCode(0) > 0, TEXT("премиум-магазин: открыт, карточка 0 с товаром"),
				FString::Printf(TEXT("открыт=%d код0=%d"), (P && P->IsOpen()) ? 1 : 0, P ? P->DebugCardCode(0) : 0));
			return 0.8f;
		});
		auto ClickCard = [this](UWorld* W, bool bPremium, bool bDouble) -> float
		{
			UGameInstance* GI = W->GetGameInstance();
			UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
			const UWidget* Icon = nullptr;
			if (bPremium) { if (UAsdaPremiumShopSubsystem* P = GI->GetSubsystem<UAsdaPremiumShopSubsystem>()) { Icon = P->DebugCardIcon(0); } }
			else          { if (UAsdaEquipShopSubsystem* E = GI->GetSubsystem<UAsdaEquipShopSubsystem>()) { Icon = E->DebugCardIcon(0); } }
			FVector2D C;
			if (!WM || !CenterOf(Icon, C))
			{
				Expect(false, bPremium ? TEXT("премиум-магазин: клик") : TEXT("магазин снаряжения: клик"), TEXT("нет живой геометрии карточки 0"));
				return 0.1f;
			}
			if (bDouble) { AsdaSimDoubleClickAt(WM, C, false); } else { WM->SimulateClickAt(C, 2, false); }
			return AsdaShopDblTestLocal::GapSec();
		};
		Steps.Add([ClickCard](UWorld* W) { return ClickCard(W, true, false); });
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaPremiumShopSubsystem* P = W->GetGameInstance()->GetSubsystem<UAsdaPremiumShopSubsystem>();
			Expect(P && P->DebugCartFilled() == 0 && P->DebugSelCard() == 0,
				TEXT("премиум-магазин: одиночный щелчок по иконке — выбор, в корзину НЕ кладёт [ПОРТ]"),
				FString::Printf(TEXT("корзина=%d выбрана=%d"), P ? P->DebugCartFilled() : -1, P ? P->DebugSelCard() : -2));
			return 0.05f;
		});
		Steps.Add([ClickCard](UWorld* W) { return ClickCard(W, true, true); });
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaPremiumShopSubsystem* P = W->GetGameInstance()->GetSubsystem<UAsdaPremiumShopSubsystem>();
			Expect(P && P->DebugCartFilled() == 1, TEXT("премиум-магазин: двойной щелчок — в корзину РОВНО одна строка"),
				FString::Printf(TEXT("корзина=%d"), P ? P->DebugCartFilled() : -1));
			return AsdaShopDblTestLocal::GapSec();
		});
		// 2а. ★ Ревизия 10.10.2026 (МЕЛК-2): «карточка → страница пересобрана → карточка» — НЕ кладёт. Пересборка — повторный
		//   вход в ту же категорию (`SetCategory`, как щелчок по вкладке: `BuildPages` + `BuildPage`). Корзина перед случаем — «Сброс» 6037.
		AddChainCase(TEXT("премиум-магазин: «карточка → страница пересобрана → карточка» — НЕ кладёт (МЕЛК-2); 4-й щелчок — двойной, кладёт (контроль)"),
			[](UWorld* W) -> bool
			{
				UAsdaPremiumShopSubsystem* P = W->GetGameInstance()->GetSubsystem<UAsdaPremiumShopSubsystem>();
				if (!P || !P->IsOpen()) { return false; }
				P->OnResetClicked();
				return P->DebugCartFilled() == 0 && P->DebugCardCode(0) > 0;
			},
			[this](UWorld* W, TArray<FAsdaSimChainLink>& L) -> bool
			{
				UAsdaPremiumShopSubsystem* P = W->GetGameInstance()->GetSubsystem<UAsdaPremiumShopSubsystem>();
				FVector2D C;
				if (!P || !AsdaShopDblTestLocal::CenterOf(P->DebugCardIcon(0), C)) { return false; }
				TWeakObjectPtr<UAsdaPremiumShopSubsystem> WP(P);
				FAsdaSimChainLink Item; Item.Pos = C;
				FAsdaSimChainLink Brk; Brk.Action = [WP]() { if (WP.IsValid()) { WP->SetCategory(WP->GetCategory()); } };
				FAsdaSimChainLink Snap; Snap.Action = [this, WP]() { SnapCount = WP.IsValid() ? WP->DebugCartFilled() : -1; };
				L = { Item, Brk, Item, Snap, Item };
				return true;
			},
			[](UWorld* W) -> int32
			{
				UAsdaPremiumShopSubsystem* P = W->GetGameInstance()->GetSubsystem<UAsdaPremiumShopSubsystem>();
				return P ? P->DebugCartFilled() : -1;
			});
		Steps.Add([](UWorld* W) -> float
		{
			if (UAsdaPremiumShopSubsystem* P = W->GetGameInstance()->GetSubsystem<UAsdaPremiumShopSubsystem>()) { P->CloseWindow(); }
			return 0.3f;
		});

		// ═══ 3. Магазин снаряжения 6800 ═══
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaEquipShopSubsystem* E = W->GetGameInstance()->GetSubsystem<UAsdaEquipShopSubsystem>();
			if (E) { E->OpenWindow(); }
			Expect(E && E->IsOpen() && E->DebugCardCode(0) > 0, TEXT("магазин снаряжения: открыт, карточка 0 с товаром"),
				FString::Printf(TEXT("открыт=%d код0=%d"), (E && E->IsOpen()) ? 1 : 0, E ? E->DebugCardCode(0) : 0));
			return 0.8f;
		});
		Steps.Add([ClickCard](UWorld* W) { return ClickCard(W, false, false); });
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaEquipShopSubsystem* E = W->GetGameInstance()->GetSubsystem<UAsdaEquipShopSubsystem>();
			Expect(E && E->DebugWearFilled() == 0 && E->DebugSelCard() == 0,
				TEXT("магазин снаряжения: одиночный щелчок — рамка выбора, в «Список снаряжения» НЕ кладёт [ПОРТ]"),
				FString::Printf(TEXT("список=%d выбрана=%d"), E ? E->DebugWearFilled() : -1, E ? E->DebugSelCard() : -2));
			return 0.05f;
		});
		Steps.Add([ClickCard](UWorld* W) { return ClickCard(W, false, true); });
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaEquipShopSubsystem* E = W->GetGameInstance()->GetSubsystem<UAsdaEquipShopSubsystem>();
			Expect(E && E->DebugWearFilled() == 1, TEXT("магазин снаряжения: двойной щелчок — примерка, в список РОВНО одна строка"),
				FString::Printf(TEXT("список=%d"), E ? E->DebugWearFilled() : -1));
			if (E) { E->CloseWindow(); }
			return 0.3f;
		});
	}
};

namespace
{
	// Разбор `delay=N`.
	// ★ R5-fix-c (13.11.2026): своя `ParseShopDblDelay` (дословная копия у двенадцати самотестов) убрана — разбор `delay=N` в общей точке
	//   `AsdaSelfTestKit::DelayArg` (`Test/AsdaSelfTestKit.h`), вызовы ниже идут в неё.

	FAutoConsoleCommandWithWorldAndArgs GAsdaShopSelfTestDblClick(
		TEXT("asda.Shop.SelfTestDblClick"),
		TEXT("asda.Shop.SelfTestDblClick [delay=N] [quit] — самотест требования 10.10.2026 «в корзину — двойным щелчком» путём игрока: "
		     "магазин NPC (выбор/в корзину/возврат/бросок/ключ отката), премиум-магазин, магазин снаряжения. Только офлайн."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Shop.SelfTestDblClick"), KitArgs)) { return; }
			const float Delay = AsdaSelfTestKit::DelayArg(Args);
			const bool bQuit = Args.Contains(TEXT("quit"));
			TWeakObjectPtr<UWorld> WW(W);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit](float) -> bool
			{
				UWorld* Wl = WW.Get();
				if (!Wl) { return false; }
				if (AsdaSelfTestKit::RefuseDeferred(Wl, TEXT("asda.Shop.SelfTestDblClick"), bQuit)) { return false; }   // Р-2: офлайн повторно — на отложенном старте
				const AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(Wl->GetFirstPlayerController());
				const bool bNeedSpawn = !PC || !PC->GetPawn();
				if (bNeedSpawn && GEngine)
				{
					GEngine->Exec(Wl, TEXT("Asda.Load.CurrentMap 3"));
					GEngine->Exec(Wl, TEXT("Asda.World.FakeSelfSpawn 256 256"));
				}
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit](float) -> bool
				{
					UWorld* W2 = WW.Get();
					if (!W2) { return false; }
					TSharedRef<FAsdaShopDblSelfTest> T = MakeShared<FAsdaShopDblSelfTest>();
					T->Build();
					TSharedRef<int32> Idx = MakeShared<int32>(0);
					TSharedRef<double> NextAt = MakeShared<double>(0.0);
					TSharedRef<int32> Quiet = MakeShared<int32>(3);   // ★ R5-fix8-b 14.11.2026 (третья запись, добавка координатора: ClickThrough): кадров после отпускания симввода
					FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, T, Idx, NextAt, Quiet, bQuit](float) -> bool
					{
						UWorld* W3 = WW.Get();
						if (!W3) { AsdaCVarSave::RestoreAll(TEXT("asda.Shop.SelfTestDblClick")); return false; }   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи — и при уходе мира
						UGameInstance* GI3 = W3->GetGameInstance();
						UAsdaWindowSubsystem* WM3 = GI3 ? GI3->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
						if (WM3 && WM3->IsSimClickPending()) { *Quiet = 0; return true; }   // идёт симклик — ждём его конца
						// ★ R5-fix8-b 14.11.2026 (третья запись, добавка координатора: ClickThrough): после отпускания — ещё 3 кадра (как в общей очереди шагов). Прежде при 15 к/с
						//   проверка «перетаскивание витрина → корзина» шла в том же кадре, что и отпускание: удержание 20 кадров (1,3 с) дольше
						//   паузы шага 0,8 с, и срок шага истекал раньше конца ввода. (★ R5-fix8-b 14.11.2026 (пятая запись, ревизия REVIEW8b), М-6: уточнена причина)
						if (*Quiet < 3) { ++*Quiet; return true; }
						if (T->Chain.IsValid() && !T->Chain->bDone) { return true; }   // идёт цепочка щелчков — ждём её конца
						if (FPlatformTime::Seconds() < *NextAt) { return true; }
						if (*Idx < T->Steps.Num())
						{
							const float Wait = T->Steps[(*Idx)++](W3);
							if (T->Rewind > 0) { *Idx -= T->Rewind; T->Rewind = 0; }   // ★ R5-fix-b 13.11.2026 (заключительный заход): повтор случая-цепочки
							*NextAt = FPlatformTime::Seconds() + Wait;
							return true;
						}
						if (UAsdaInventorySubsystem* Inv3 = GI3 ? GI3->GetSubsystem<UAsdaInventorySubsystem>() : nullptr) { Inv3->SetGoldDisplay(T->GoldWas); }
						// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключ Shop.DblClickToCart — к исходному значению И приоритету SetBy (прежде шаги
						//   возвращали константу 1 с SetByConsole).
						AsdaCVarSave::RestoreAll(TEXT("asda.Shop.SelfTestDblClick"));
						UE_LOG(LogAsdaShopDblTest, Log, TEXT("САМОТЕСТ-ДВОЙНОЙ ИТОГ: ok=%d fail=%d"), T->Ok, T->Fail);
						AsdaSelfTestKit::Quit(W3, bQuit);
						return false;
					}));
					return false;
				}), bNeedSpawn ? 2.f : 0.f);
				return false;
			}), Delay);
			UE_LOG(LogAsdaShopDblTest, Log, TEXT("САМОТЕСТ-ДВОЙНОЙ: запуск через %.1f с"), Delay);
		}));
}

#endif // !UE_BUILD_SHIPPING
