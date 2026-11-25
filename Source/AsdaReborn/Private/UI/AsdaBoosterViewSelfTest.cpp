// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ОКНО «ВИД» БУСТЕРА ИЗ ВСЕХ ВХОДОВ» (31.10.2026): `asda.SelfTest.BoosterView [quit]`. [ПОРТ]
//   Задача владельца: окно «Вид» 15600 (`BOOSTER_PREV_WIN`) открывалось только из магазина «Усилители» (и барабана); в оригинале
//   его открывают ещё премиум-магазин 6000 (клик по карточке бустера, `itemOnUseDispatch@0xC267E0`) и окно состава набора 26500
//   (клик по бустеру в составе, `invItemUseOrBooster@0xDB36D0`). Поток — `Reports/booster_view/FLOW.md`.
//   Путь игрока (закон `ui-check-game-path-geometry`): живая геометрия, настоящие события мыши Slate (`SimulateClickAt`),
//   офлайн (-AsdaNoNet). Разделы:
//     Z. ключ `asda.Booster.PrevPaths 0` (прежний порт): клик по карточке бустера — окна «Вид» НЕТ (шаг 0 задачи);
//     P. премиум-магазин: «Снаряжение → Воин → Бустер», клик по иконке карточки → «Вид» с составом этого бустера, окно поверх
//        витрины (точка центра — зона «Вида»), подсказка ячейки — владелец BoosterPrev, имя на языке сессии; клик по набору
//        внутри «Вида» → окно 26500 (превью), пристыковано: 26500 у правого края так, что «Вид» вплотную справа;
//        крестик 26500 гасит и «Вид», подсказки нет;
//     K. окно состава набора: коробка в сумке → двойной щелчок → 26500 (вскрытие) → клик по бустеру в составе → «Вид»,
//        пристыкован справа; щелчок по ячейкам такого «Вида» мёртв (эталонный флаг +697 = 0); крестик 26500 гасит оба;
//     S. страница из премиум-магазина (если на полке есть бустер на две страницы; в данных 31.10 — нет, раздел пропускается);
//     T. страница через окно 26500: смена страницы не двигает окно, крестик 15648 гасит только «Вид», новое открытие —
//        снова 1-я страница (ключ `asda.Booster.PrevOpenReset`).
//   Сумка, ключи и окна возвращаются в конце. Итог: «ВИД-БУСТЕРА ИТОГ: ok=N fail=M». Карта мира (`--map world`).
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaBoosterPanelSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaPackagePanelSubsystem.h"
#include "UI/AsdaPremiumShopSubsystem.h"
#include "UI/AsdaShopDblClick.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaBoosterViewTest, Log, All);

namespace AsdaBoosterViewTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.BoosterView");   // имя команды
	static const TCHAR* kPfx = TEXT("ВИД-БУСТЕРА");                 // префикс строк и итога
	static const FName kPrevId(TEXT("BOOSTER_PREV_WIN"));            // окно «Вид» 15600
	static const FName kPkgId(TEXT("Package_Contents_Win"));         // окно состава набора 26500
	static const FName kShopId(TEXT("SHELLITEM_STORE_WIN"));         // премиум-магазин 6000

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                    // пройдено / провалено
		TWeakObjectPtr<UWorld> W;                  // мир прогона
		bool bQuit = false;                        // выйти после итога
		int32 Card = -1;                           // карточка премиум-магазина с бустером, в составе которого есть набор
		int32 Booster = 0;                         // её код
		int32 BoxInPrev = 0;                       // код набора в составе этого бустера
		FString BoxNode;                           // префикс узла ячейки «Вида» с этим набором
		int32 PageCard = -1;                       // карточка бустера с > 15 «обычными» (две страницы) или −1
		TArray<uint8> SavedBag0;                   // сумка, ячейка 0 до прогона
		int32 Box = 0;                             // коробка в сумке, в составе которой есть бустер
		int32 BoostInBox = 0;                      // код этого бустера
		int32 Box2 = 0;                            // коробка с бустером на две страницы (раздел T)
		int32 Boost2 = 0;                          // этот бустер
		FVector2D PrevMn = FVector2D::ZeroVector;  // левый-верхний угол рамки «Вида» до смены страницы
		FVector2D WorldPt = FVector2D::ZeroVector; // точка мира без интерфейса (увести курсор)

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaBoosterViewTest, Log,   TEXT("%s ok   %s"), kPfx, *What); }
			else   { ++Fail; UE_LOG(LogAsdaBoosterViewTest, Error, TEXT("%s FAIL %s"), kPfx, *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Шаг через Sec секунд от старта.
	using AsdaSelfTestCtx::At;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	// Живой прямоугольник узла (false — не отрисован / скрыт).
	static bool Rect(const UWidget* Wd, FVector2D& Mn, FVector2D& Mx)
	{
		return Wd && AsdaUi::IsShown(Wd) && AsdaUi::WidgetAbsRect(Wd, Mn, Mx) && (Mx - Mn).X >= 2.0 && (Mx - Mn).Y >= 2.0;
	}
	static bool Center(const UWidget* Wd, FVector2D& Out)
	{
		FVector2D Mn, Mx;
		if (!Rect(Wd, Mn, Mx)) { return false; }
		Out = (Mn + Mx) * 0.5f;
		return true;
	}
	// Рамка окна = объединение девяти планов `<Prefix>_0_0…_0_8` (живая геометрия).
	static bool FrameRect(const FCtxRef& C, FName WinId, const TCHAR* Prefix, FVector2D& Mn, FVector2D& Mx)
	{
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UUserWidget* Win = WM ? WM->FindWindow(WinId) : nullptr;
		if (!Win) { return false; }
		Mn = FVector2D(FLT_MAX, FLT_MAX); Mx = FVector2D(-FLT_MAX, -FLT_MAX);
		bool bAny = false;
		for (int32 i = 0; i < 9; ++i)
		{
			FVector2D A, B;
			if (!Rect(AsdaUi::FindNodeByName(Win, *FString::Printf(TEXT("%s_0_%d"), Prefix, i), nullptr), A, B)) { continue; }
			Mn = FVector2D(FMath::Min(Mn.X, A.X), FMath::Min(Mn.Y, A.Y));
			Mx = FVector2D(FMath::Max(Mx.X, B.X), FMath::Max(Mx.Y, B.Y));
			bAny = true;
		}
		return bAny;
	}
	// Курсор в точку + событие движения (наведение без щелчка).
	using AsdaSelfTestInput::Hover;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.
	static void Click(const FCtxRef& C, const FVector2D& P) { if (UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>()) { Hover(P); WM->SimulateClickAt(P, /*HoldFrames*/ 2); } }
	// Узел окна по началу имени.
	static UWidget* Node(const FCtxRef& C, FName WinId, const FString& Pfx)
	{
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UUserWidget* Win = WM ? WM->FindWindow(WinId) : nullptr;
		return Win ? AsdaUi::FindNodeByPrefix(Win, *Pfx) : nullptr;
	}
	static bool PrevShown(const FCtxRef& C)
	{
		const UAsdaBoosterPanelSubsystem* Bp = C->Sub<UAsdaBoosterPanelSubsystem>();
		const UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		return Bp && Bp->IsPrevVisible() && WM && WM->IsWindowVisible(kPrevId);
	}
	static bool TipOf(const FCtxRef& C, EAsdaTipOwner O)
	{
		const UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>();
		return T && T->IsTooltipShown() && T->GetTipOwner() == O;
	}
	static bool TipGone(const FCtxRef& C)
	{
		UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>();
		return T && (!T->IsTooltipShown() || (T->GetTipOwner() != EAsdaTipOwner::BoosterPrev && T->GetTipOwner() != EAsdaTipOwner::Package));
	}
	static FString TipState(const FCtxRef& C)
	{
		const UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>();
		return T ? FString::Printf(TEXT("показана=%d владелец=%d"), T->IsTooltipShown() ? 1 : 0, (int32)T->GetTipOwner()) : TEXT("нет подсистемы");
	}
	// Состояние «Вида» строкой.
	static FString PrevState(const FCtxRef& C)
	{
		const UAsdaBoosterPanelSubsystem* Bp = C->Sub<UAsdaBoosterPanelSubsystem>();
		return Bp ? FString::Printf(TEXT("видно=%d код=%d источник=%d ячейки=%d стр=%d"), Bp->IsPrevVisible() ? 1 : 0, Bp->GetPrevItemCode(),
			(int32)Bp->GetPrevSource(), Bp->ArePrevCellsActive() ? 1 : 0, Bp->GetPrevPage()) : TEXT("нет подсистемы");
	}
	// Окно с наибольшим z под точкой — то, что получит щелчок (шлюз «клик в мир или в окно»).
	static FName TopWindowAt(const FCtxRef& C, const FVector2D& P)
	{
		FName Zone = NAME_None;
		if (const UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>()) { WM->IsScreenPointOverAnyUiWindow(P, Zone); }
		return Zone;
	}
	// Правый край вьюпорта в абсолютных координатах Slate.
	static float ViewportRightAbs()
	{
		if (!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->GetGameViewportWidget().IsValid()) { return 0.f; }
		const FGeometry G = GEngine->GameViewport->GetGameViewportWidget()->GetCachedGeometry();
		return G.LocalToAbsolute(G.GetLocalSize()).X;
	}
	// Узел ячейки «Вида» с кодом `Code` на первой странице (та же раскладка, что `PopulatePrevWindow`).
	static FString PrevNodeOf(const TArray<int32>& Slots35, int32 Code)
	{
		for (int32 i = 0; i < 5 && i < Slots35.Num(); ++i) { if (Slots35[i] == Code) { return FString::Printf(TEXT("BOOSTER_PREV_SITEM%02d_"), i); } }
		int32 Seen = 0;
		for (int32 i = 5; i < Slots35.Num(); ++i)
		{
			if (Slots35[i] <= 0) { continue; }
			if (Slots35[i] == Code && Seen < 15) { return FString::Printf(TEXT("BOOSTER_PREV_ITEM%02d_"), 5 + Seen); }
			++Seen;
		}
		return FString();
	}
	// Проверка стыковки (`positionHoverTooltipWnd`): «Вид» вплотную справа от 26500, правый край «Вида» = край экрана,
	//   верх «Вида» = верх 26500 + (320 − 343) единиц канвы.
	static void ExpectDocked(const FCtxRef& C, const TCHAR* Tag)
	{
		FVector2D PkMn, PkMx, PvMn, PvMx;
		const bool bPk = FrameRect(C, kPkgId, TEXT("Package_Contents_Win"), PkMn, PkMx);
		const bool bPv = FrameRect(C, kPrevId, TEXT("BOOSTER_PREV_WIN"), PvMn, PvMx);
		const float Scale = bPv ? (PvMx.X - PvMn.X) / 360.f : 1.f;   // абсолютных пикселей на единицу канвы
		const float Right = ViewportRightAbs();
		const bool bJoin  = bPk && bPv && FMath::Abs(PvMn.X - PkMx.X) <= 3.f * Scale;
		const bool bEdge  = bPv && Right > 0.f && FMath::Abs(PvMx.X - Right) <= 3.f * Scale;
		const bool bTop   = bPk && bPv && FMath::Abs((PvMn.Y - PkMn.Y) - (320.f - 343.f) * Scale) <= 3.f * Scale;
		C->Expect(bJoin && bEdge && bTop, FString::Printf(
			TEXT("%s стыковка: 26500 (%.0f,%.0f)-(%.0f,%.0f), «Вид» (%.0f,%.0f)-(%.0f,%.0f), край экрана %.0f, масштаб %.2f — вплотную %d, у края %d, верх %d"),
			Tag, PkMn.X, PkMn.Y, PkMx.X, PkMx.Y, PvMn.X, PvMn.Y, PvMx.X, PvMx.Y, Right, Scale, bJoin ? 1 : 0, bEdge ? 1 : 0, bTop ? 1 : 0));
	}

	static void Finish(const FCtxRef& C)
	{
		UWorld* W = C->W.Get();
		if (UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>())
		{
			I->ApplyExternalRecord(C->SavedBag0.Num() == FAsdaItemRecordWriter::kSize ? C->SavedBag0 : FAsdaItemRecordWriter::ClearCell(1, 0),
				UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
			if (I->IsWindowOpen()) { I->CloseWindow(); }
		}
		if (UAsdaPackagePanelSubsystem* P = C->Sub<UAsdaPackagePanelSubsystem>()) { P->ClosePanel(); }
		if (UAsdaBoosterPanelSubsystem* B = C->Sub<UAsdaBoosterPanelSubsystem>()) { B->ClosePrevWindow(); }
		if (UAsdaPremiumShopSubsystem* S = C->Sub<UAsdaPremiumShopSubsystem>()) { if (S->IsOpen()) { S->CloseWindow(); } }
		if (UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>()) { T->HideTooltip(); }
		AsdaSelfTestKit::Finish(W, kCmd, C->bQuit, LogAsdaBoosterViewTest, kPfx, C->Ok, C->Fail);
	}

	// Открыть витрину «Снаряжение → Воин → Бустер» (те же переключатели, что щелчки по полосам 58200/58202/58203).
	static void OpenBoosterShelf(const FCtxRef& C)
	{
		UAsdaPremiumShopSubsystem* S = C->Sub<UAsdaPremiumShopSubsystem>();
		if (!S) { return; }
		if (!S->IsOpen()) { S->OpenWindow(false); }
		S->SetCategory(3);     // «Снаряжение»
		S->SetEquipSub(0);     // «Воин»
		S->SetEquipSub2(7);    // «Бустер» (Sub2 = 11)
	}

	static void Run(const FCtxRef& C)
	{
		UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		if (!IT || !WM || !Inv || !C->Sub<UAsdaPremiumShopSubsystem>() || !C->Sub<UAsdaBoosterPanelSubsystem>() || !C->Sub<UAsdaPackagePanelSubsystem>())
		{
			C->Expect(false, TEXT("подсистемы таблиц/окон/инвентаря/премиум-магазина/«Вида»/окна 26500 есть"));
			Finish(C);
			return;
		}
		// Коробка в сумке: первая из таблиц, в составе которой есть бустер с составом (`booster_list`).
		for (int32 Code = 20000; Code < 80000 && !C->Box; ++Code)
		{
			const FAsdaItemRecord* R = IT->GetItem(Code);
			if (!R || (R->ItemSubType != 200 && R->ItemSubType != 201) || R->DurationOrEnchant <= 0 || R->RequiredLevel > 1) { continue; }
			TArray<FAsdaPackageEntry> Pc;
			if (!IT->GetPackageContents(Code, Pc)) { continue; }
			for (const FAsdaPackageEntry& E : Pc)
			{
				const FAsdaItemRecord* B = IT->GetItem(E.ItemCode);
				TArray<int32> S35;
				if (B && (B->ItemSubType == 66 || B->ItemSubType == 67) && B->DurationOrEnchant > 0 && IT->GetBoosterSlotsRaw(E.ItemCode, S35))
				{
					C->Box = Code; C->BoostInBox = E.ItemCode; break;
				}
			}
		}
		C->Expect(C->Box > 0, FString::Printf(TEXT("данные: коробка %d с бустером %d в составе"), C->Box, C->BoostInBox));
		// Коробка с бустером, у которого > 15 «обычных» (две страницы «Вида»).
		for (int32 Code = 20000; Code < 80000 && !C->Box2; ++Code)
		{
			const FAsdaItemRecord* R = IT->GetItem(Code);
			if (!R || (R->ItemSubType != 200 && R->ItemSubType != 201) || R->DurationOrEnchant <= 0 || R->RequiredLevel > 1) { continue; }
			TArray<FAsdaPackageEntry> Pc;
			if (!IT->GetPackageContents(Code, Pc)) { continue; }
			for (const FAsdaPackageEntry& E : Pc)
			{
				const FAsdaItemRecord* B = IT->GetItem(E.ItemCode);
				TArray<int32> S35;
				if (!B || (B->ItemSubType != 66 && B->ItemSubType != 67) || B->DurationOrEnchant <= 0 || !IT->GetBoosterSlotsRaw(E.ItemCode, S35)) { continue; }
				int32 Normal = 0;
				for (int32 k = 5; k < S35.Num(); ++k) { if (S35[k] > 0) { ++Normal; } }
				if (Normal > 15) { C->Box2 = Code; C->Boost2 = E.ItemCode; break; }
			}
		}
		C->Expect(C->Box2 > 0, FString::Printf(TEXT("данные: коробка %d с бустером %d на две страницы"), C->Box2, C->Boost2));
		const FAsdaItemInstance* It0 = Inv->GetCell(1, 0);
		C->SavedBag0 = (It0 && !It0->IsEmpty()) ? It0->Raw : TArray<uint8>();
		// Точка мира без интерфейса.
		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->GetGameViewportWidget().IsValid())
		{
			const FGeometry G = GEngine->GameViewport->GetGameViewportWidget()->GetCachedGeometry();
			C->WorldPt = G.LocalToAbsolute(G.GetLocalSize() * FVector2D(0.5f, 0.22f));
		}

		// ═══ Z. Ключ PrevPaths 0 — прежний порт: из премиум-магазина «Вид» не открывается ═══
		At(C, 0.5f, [C]() { AsdaCVarSave::SetForTest(TEXT("asda.Booster.PrevPaths"), 0); OpenBoosterShelf(C); });
		At(C, 1.6f, [C]()
		{
			UAsdaPremiumShopSubsystem* S = C->Sub<UAsdaPremiumShopSubsystem>();
			UAsdaItemTableSubsystem* T = C->Sub<UAsdaItemTableSubsystem>();
			// Карточка бустера с набором в составе и карточка бустера на две страницы.
			for (int32 i = 0; S && T && i < UAsdaPremiumShopSubsystem::kCards; ++i)
			{
				const int32 Code = S->DebugCardCode(i);
				TArray<int32> S35;
				if (Code <= 0 || !T->GetBoosterSlotsRaw(Code, S35)) { continue; }
				int32 Normal = 0;
				for (int32 k = 5; k < S35.Num(); ++k) { if (S35[k] > 0) { ++Normal; } }
				if (Normal > 15 && C->PageCard < 0) { C->PageCard = i; }
				if (C->Card >= 0) { continue; }
				for (int32 k = 0; k < S35.Num(); ++k)
				{
					const FAsdaItemRecord* R = T->GetItem(S35[k]);
					if (R && (R->ItemSubType == 200 || R->ItemSubType == 201) && R->DurationOrEnchant > 0 && T->IsPackageItem(S35[k]))
					{
						const FString N = PrevNodeOf(S35, S35[k]);
						if (N.IsEmpty()) { continue; }
						C->Card = i; C->Booster = Code; C->BoxInPrev = S35[k]; C->BoxNode = N; break;
					}
				}
			}
			C->Expect(S && S->IsOpen() && C->Card >= 0, FString::Printf(TEXT("Z0 витрина «Снаряжение→Воин→Бустер»: карточка %d бустер %d (набор %d в ячейке %s); на две страницы — карточка %d"),
				C->Card, C->Booster, C->BoxInPrev, *C->BoxNode, C->PageCard));
			FVector2D P;
			if (S && C->Card >= 0 && Center(S->DebugCardIcon(C->Card), P)) { Click(C, P); }
		});
		At(C, 2.3f, [C]()
		{
			C->Expect(!PrevShown(C), FString::Printf(TEXT("Z1 ключ 0 (прежний порт): клик по карточке бустера — окна «Вид» нет (%s)"), *PrevState(C)));
			AsdaCVarSave::SetForTest(TEXT("asda.Booster.PrevPaths"), 1);
			Hover(C->WorldPt);
		});

		// ═══ P. Премиум-магазин → «Вид» ═══
		At(C, 2.8f, [C]()
		{
			FVector2D P;
			UAsdaPremiumShopSubsystem* S = C->Sub<UAsdaPremiumShopSubsystem>();
			if (S && C->Card >= 0 && Center(S->DebugCardIcon(C->Card), P)) { Click(C, P); }
		});
		At(C, 3.5f, [C]()
		{
			const UAsdaBoosterPanelSubsystem* Bp = C->Sub<UAsdaBoosterPanelSubsystem>();
			const bool bOk = PrevShown(C) && Bp->GetPrevItemCode() == C->Booster && Bp->GetPrevSource() == EAsdaPrevSource::PremiumShop
				&& Bp->ArePrevCellsActive() && Bp->GetPrevPage() == 0;
			C->Expect(bOk, FString::Printf(TEXT("P1 клик по карточке бустера %d → «Вид» с его составом, щелчок по ячейкам жив (%s)"), C->Booster, *PrevState(C)));
			// Имя бустера — на языке сессии (та же строка, что у таблицы предметов).
			const UTextBlock* Name = Cast<UTextBlock>(Node(C, kPrevId, TEXT("BOOSTER_PREV_NAME")));
			const FString Want = C->Sub<UAsdaItemTableSubsystem>()->GetItemName(C->Booster);
			C->Expect(Name && Name->GetText().ToString() == Want && !Want.IsEmpty(),
				FString::Printf(TEXT("P2 имя в окне: «%s» (ждём «%s»)"), Name ? *Name->GetText().ToString() : TEXT("—"), *Want));
			// Окно поверх витрины: щелчок в центр «Вида» получает «Вид», а не магазин и не мир.
			FVector2D Mn, Mx;
			const bool bFr = FrameRect(C, kPrevId, TEXT("BOOSTER_PREV_WIN"), Mn, Mx);
			const FName Top = bFr ? TopWindowAt(C, (Mn + Mx) * 0.5f) : NAME_None;
			C->Expect(bFr && Top == kPrevId, FString::Printf(TEXT("P3 центр «Вида» (%.0f,%.0f) — верхнее окно %s (ждём BOOSTER_PREV_WIN)"),
				(Mn.X + Mx.X) * 0.5f, (Mn.Y + Mx.Y) * 0.5f, *Top.ToString()));
			// Подсказка ячейки с набором.
			FVector2D P;
			Hover(C->WorldPt);
			if (Center(Node(C, kPrevId, C->BoxNode), P)) { At(C, 0.15f, [P]() { Hover(P); }); }
		});
		At(C, 4.1f, [C]()
		{
			C->Expect(TipOf(C, EAsdaTipOwner::BoosterPrev), FString::Printf(TEXT("P4 наведение на ячейку «Вида» — подсказка предмета, владелец BoosterPrev (%s)"), *TipState(C)));
			FVector2D P;
			if (Center(Node(C, kPrevId, C->BoxNode), P)) { Click(C, P); }   // щелчок по набору внутри «Вида»
		});
		At(C, 4.8f, [C]()
		{
			const UAsdaPackagePanelSubsystem* Pk = C->Sub<UAsdaPackagePanelSubsystem>();
			C->Expect(Pk && Pk->IsPanelOpen() && Pk->GetStampMode() == 0 && PrevShown(C),
				FString::Printf(TEXT("P5 щелчок по набору %d в «Виде» → окно 26500 (превью), «Вид» остался (панель=%d режим=%d; %s)"),
					C->BoxInPrev, Pk && Pk->IsPanelOpen() ? 1 : 0, Pk ? Pk->GetStampMode() : -1, *PrevState(C)));
			ExpectDocked(C, TEXT("P6"));
			FVector2D P;
			if (Center(Node(C, kPkgId, TEXT("Package_ClsBtn")), P)) { Click(C, P); }   // крестик 26506
		});
		At(C, 5.5f, [C]()
		{
			const UAsdaPackagePanelSubsystem* Pk = C->Sub<UAsdaPackagePanelSubsystem>();
			C->Expect(Pk && !Pk->IsPanelOpen() && !PrevShown(C), FString::Printf(TEXT("P7 крестик 26500 гасит и окно состава, и «Вид» (%s)"), *PrevState(C)));
			C->Expect(TipGone(C), FString::Printf(TEXT("P8 после закрытия — подсказки «Вида»/набора нет (%s)"), *TipState(C)));
		});

		// ═══ S. Страница: новое открытие — первая страница ═══
		At(C, 6.0f, [C]()
		{
			FVector2D P;
			UAsdaPremiumShopSubsystem* S = C->Sub<UAsdaPremiumShopSubsystem>();
			if (C->PageCard >= 0 && S && Center(S->DebugCardIcon(C->PageCard), P)) { Click(C, P); }
		});
		At(C, 6.7f, [C]()
		{
			if (C->PageCard < 0) { C->Expect(true, TEXT("S0 на витрине нет бустера на две страницы — раздел пропущен")); return; }
			FVector2D P;
			const bool bArrow = Center(Node(C, kPrevId, TEXT("BOOSTER_PREV_PAGE_OPT1")), P);
			C->Expect(PrevShown(C) && bArrow, FString::Printf(TEXT("S0 бустер на две страницы открыт, стрелка «>» на экране (%s)"), *PrevState(C)));
			if (bArrow) { Click(C, P); }
		});
		At(C, 7.3f, [C]()
		{
			if (C->PageCard < 0) { return; }
			const UAsdaBoosterPanelSubsystem* Bp = C->Sub<UAsdaBoosterPanelSubsystem>();
			C->Expect(Bp && Bp->GetPrevPage() == 1, FString::Printf(TEXT("S1 стрелка «>» — вторая страница (%s)"), *PrevState(C)));
			FVector2D P;
			if (Center(Node(C, kPrevId, TEXT("BOOSTER_PREV_WIN_CLOSE")), P)) { Click(C, P); }   // крестик 15648 «Вида»
		});
		At(C, 7.7f, [C]()
		{
			if (C->PageCard < 0) { return; }
			C->Expect(!PrevShown(C), FString::Printf(TEXT("S2 крестик 15648 закрыл «Вид» (%s)"), *PrevState(C)));
			FVector2D P;
			UAsdaPremiumShopSubsystem* S = C->Sub<UAsdaPremiumShopSubsystem>();
			Hover(C->WorldPt);
			if (S && Center(S->DebugCardIcon(C->PageCard), P)) { Click(C, P); }   // открыть заново той же карточкой
		});
		At(C, 8.4f, [C]()
		{
			if (C->PageCard >= 0)
			{
				const UAsdaBoosterPanelSubsystem* Bp = C->Sub<UAsdaBoosterPanelSubsystem>();
				C->Expect(PrevShown(C) && Bp->GetPrevPage() == 0, FString::Printf(TEXT("S3 новое открытие — снова первая страница (%s)"), *PrevState(C)));
			}
			// ★ 31.10.2026 (порядок пакета): магазин закрывается и при пропущенном разделе S — прежде ранний выход оставлял
			//   элитный магазин 6000 (AlwaysTop) поверх сумки и «Вида» на всю фазу K (K0/K4 «верхнее окно SHELLITEM_STORE_WIN»).
			if (UAsdaBoosterPanelSubsystem* B = C->Sub<UAsdaBoosterPanelSubsystem>()) { B->ClosePrevWindow(); }
			if (UAsdaPremiumShopSubsystem* S = C->Sub<UAsdaPremiumShopSubsystem>()) { S->CloseWindow(); }
		});

		// ═══ K. Окно состава набора (вскрытие из сумки) → «Вид» ═══
		At(C, 9.0f, [C]()
		{
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			if (!I || C->Box <= 0) { return; }
			I->ApplyExternalRecord(FAsdaItemRecordWriter::Cell(C->Box, 1, 0, 1), UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
			if (!I->IsWindowOpen()) { I->OpenWindow(); }
			I->SetTab(1);
		});
		At(C, 9.8f, [C]()
		{
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			FVector2D P;
			if (I && Center(I->DebugGetGridIcon(0), P)) { Hover(P); AsdaSimDoubleClickAt(C->Sub<UAsdaWindowSubsystem>(), P, false); }
		});
		At(C, 10.7f, [C]()
		{
			UAsdaPackagePanelSubsystem* Pk = C->Sub<UAsdaPackagePanelSubsystem>();
			C->Expect(Pk && Pk->IsPanelOpen() && Pk->GetStampMode() == 1,
				FString::Printf(TEXT("K0 двойной щелчок по коробке %d в сумке → окно 26500 (вскрытие) (панель=%d режим=%d)"),
					C->Box, Pk && Pk->IsPanelOpen() ? 1 : 0, Pk ? Pk->GetStampMode() : -1));
			// Ячейка с бустером.
			UWidget* Icon = nullptr;
			for (int32 r = 0; Pk && r < UAsdaPackagePanelSubsystem::kBlocks && !Icon; ++r)
			{
				for (int32 k = 0; k < UAsdaPackagePanelSubsystem::kBigCells && !Icon; ++k)   { if (Pk->DebugCellCode(r, true, k) == C->BoostInBox)  { Icon = Pk->DebugCellIcon(r, true, k); } }
				for (int32 k = 0; k < UAsdaPackagePanelSubsystem::kSmallCells && !Icon; ++k) { if (Pk->DebugCellCode(r, false, k) == C->BoostInBox) { Icon = Pk->DebugCellIcon(r, false, k); } }
			}
			FVector2D P;
			const bool bGeo = Center(Icon, P);
			C->Expect(bGeo, FString::Printf(TEXT("K1 ячейка бустера %d в составе на экране (%.0f,%.0f)"), C->BoostInBox, P.X, P.Y));
			if (bGeo) { Click(C, P); }
		});
		At(C, 11.4f, [C]()
		{
			const UAsdaBoosterPanelSubsystem* Bp = C->Sub<UAsdaBoosterPanelSubsystem>();
			C->Expect(PrevShown(C) && Bp->GetPrevItemCode() == C->BoostInBox && Bp->GetPrevSource() == EAsdaPrevSource::PackagePanel
				&& !Bp->ArePrevCellsActive() && Bp->GetPrevPage() == 0,
				FString::Printf(TEXT("K2 щелчок по бустеру %d в составе → «Вид» с его составом, щелчок по ячейкам мёртв (%s)"), C->BoostInBox, *PrevState(C)));
			ExpectDocked(C, TEXT("K3"));
			FVector2D Mn, Mx;
			const bool bFr = FrameRect(C, kPrevId, TEXT("BOOSTER_PREV_WIN"), Mn, Mx);
			const FName Top = bFr ? TopWindowAt(C, (Mn + Mx) * 0.5f) : NAME_None;
			C->Expect(bFr && Top == kPrevId, FString::Printf(TEXT("K4 центр «Вида» — верхнее окно %s"), *Top.ToString()));
			FVector2D P;
			Hover(C->WorldPt);
			if (Center(Node(C, kPrevId, TEXT("BOOSTER_PREV_SITEM00_")), P) || Center(Node(C, kPrevId, TEXT("BOOSTER_PREV_ITEM05_")), P))
			{
				At(C, 0.15f, [P]() { Hover(P); });
			}
		});
		At(C, 12.0f, [C]()
		{
			C->Expect(TipOf(C, EAsdaTipOwner::BoosterPrev), FString::Printf(TEXT("K5 наведение на ячейку «Вида» — подсказка предмета (%s)"), *TipState(C)));
			FVector2D P;
			if (Center(Node(C, kPkgId, TEXT("Package_ClsBtn")), P)) { Click(C, P); }
		});
		At(C, 12.7f, [C]()
		{
			const UAsdaPackagePanelSubsystem* Pk = C->Sub<UAsdaPackagePanelSubsystem>();
			C->Expect(Pk && !Pk->IsPanelOpen() && !PrevShown(C), FString::Printf(TEXT("K6 крестик 26500 гасит и «Вид» (%s)"), *PrevState(C)));
			C->Expect(TipGone(C), FString::Printf(TEXT("K7 подсказки «Вида» нет (%s)"), *TipState(C)));
		});
		// ═══ T. Страница «Вида»: смена страницы не двигает окно; новое открытие — первая страница ═══
		auto ClickBoostCell = [](const FCtxRef& Cx, int32 Code) -> bool
		{
			UAsdaPackagePanelSubsystem* Pk = Cx->Sub<UAsdaPackagePanelSubsystem>();
			UWidget* Icon = nullptr;
			for (int32 r = 0; Pk && r < UAsdaPackagePanelSubsystem::kBlocks && !Icon; ++r)
			{
				for (int32 k = 0; k < UAsdaPackagePanelSubsystem::kBigCells && !Icon; ++k)   { if (Pk->DebugCellCode(r, true, k) == Code)  { Icon = Pk->DebugCellIcon(r, true, k); } }
				for (int32 k = 0; k < UAsdaPackagePanelSubsystem::kSmallCells && !Icon; ++k) { if (Pk->DebugCellCode(r, false, k) == Code) { Icon = Pk->DebugCellIcon(r, false, k); } }
			}
			FVector2D P;
			if (!Center(Icon, P)) { return false; }
			Click(Cx, P);
			return true;
		};
		At(C, 13.1f, [C]()
		{
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			if (!I || C->Box2 <= 0) { return; }
			I->ApplyExternalRecord(FAsdaItemRecordWriter::Cell(C->Box2, 1, 0, 1), UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
			if (!I->IsWindowOpen()) { I->OpenWindow(); }
			I->SetTab(1);
		});
		At(C, 13.8f, [C]()
		{
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			FVector2D P;
			if (I && Center(I->DebugGetGridIcon(0), P)) { Hover(P); AsdaSimDoubleClickAt(C->Sub<UAsdaWindowSubsystem>(), P, false); }
		});
		At(C, 14.7f, [C, ClickBoostCell]()
		{
			const UAsdaPackagePanelSubsystem* Pk = C->Sub<UAsdaPackagePanelSubsystem>();
			const bool bCell = Pk && Pk->IsPanelOpen() && ClickBoostCell(C, C->Boost2);
			C->Expect(bCell, FString::Printf(TEXT("T0 коробка %d открыта, щелчок по бустеру %d"), C->Box2, C->Boost2));
		});
		At(C, 15.4f, [C]()
		{
			const UAsdaBoosterPanelSubsystem* Bp = C->Sub<UAsdaBoosterPanelSubsystem>();
			FVector2D Mx, P;
			FrameRect(C, kPrevId, TEXT("BOOSTER_PREV_WIN"), C->PrevMn, Mx);
			const bool bArrow = Center(Node(C, kPrevId, TEXT("BOOSTER_PREV_PAGE_OPT1")), P);
			C->Expect(PrevShown(C) && Bp->GetPrevItemCode() == C->Boost2 && Bp->GetPrevPage() == 0 && bArrow,
				FString::Printf(TEXT("T1 «Вид» бустера на две страницы: первая страница, стрелка «>» на экране (%s)"), *PrevState(C)));
			if (bArrow) { Click(C, P); }
		});
		At(C, 16.0f, [C]()
		{
			const UAsdaBoosterPanelSubsystem* Bp = C->Sub<UAsdaBoosterPanelSubsystem>();
			FVector2D Mn, Mx, P;
			const bool bFr = FrameRect(C, kPrevId, TEXT("BOOSTER_PREV_WIN"), Mn, Mx);
			C->Expect(Bp && Bp->GetPrevPage() == 1, FString::Printf(TEXT("T2 стрелка «>» — вторая страница (%s)"), *PrevState(C)));
			C->Expect(bFr && (Mn - C->PrevMn).Size() <= 1.0f, FString::Printf(TEXT("T3 смена страницы не двигает окно: было (%.0f,%.0f), стало (%.0f,%.0f)"),
				C->PrevMn.X, C->PrevMn.Y, Mn.X, Mn.Y));
			if (Center(Node(C, kPrevId, TEXT("BOOSTER_PREV_WIN_CLOSE")), P)) { Click(C, P); }   // крестик 15648
		});
		At(C, 16.6f, [C, ClickBoostCell]()
		{
			const UAsdaPackagePanelSubsystem* Pk = C->Sub<UAsdaPackagePanelSubsystem>();
			C->Expect(!PrevShown(C) && Pk && Pk->IsPanelOpen(), FString::Printf(TEXT("T4 крестик 15648 закрыл только «Вид», окно 26500 осталось (%s)"), *PrevState(C)));
			Hover(C->WorldPt);
			ClickBoostCell(C, C->Boost2);
		});
		At(C, 17.3f, [C]()
		{
			const UAsdaBoosterPanelSubsystem* Bp = C->Sub<UAsdaBoosterPanelSubsystem>();
			C->Expect(PrevShown(C) && Bp->GetPrevPage() == 0, FString::Printf(TEXT("T5 новое открытие — снова первая страница (%s)"), *PrevState(C)));
			FVector2D P;
			if (Center(Node(C, kPkgId, TEXT("Package_ClsBtn")), P)) { Click(C, P); }
		});
		At(C, 17.9f, [C]()
		{
			const UAsdaPackagePanelSubsystem* Pk = C->Sub<UAsdaPackagePanelSubsystem>();
			C->Expect(Pk && !Pk->IsPanelOpen() && !PrevShown(C), FString::Printf(TEXT("T6 крестик 26500 гасит оба окна (%s)"), *PrevState(C)));
		});
		At(C, 18.3f, [C]() { Finish(C); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestBoosterView(
	TEXT("asda.SelfTest.BoosterView"),
	TEXT("Самотест окна «Вид» бустера 15600 из всех входов оригинала: премиум-магазин (клик по карточке бустера), окно состава набора 26500 "
	     "(клик по бустеру в составе), набор внутри «Вида» → 26500, стыковка, закрытие, страница. Офлайн, карта мира. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaBoosterViewTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s selfready%s"), kCmd, KA.bQuit ? TEXT(" quit") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		if (!W) { return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KA.bQuit;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]()
		{
			if (C->W.IsValid() && AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			Run(C);
		}), FMath::Max(0.01f, KA.Delay), false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
