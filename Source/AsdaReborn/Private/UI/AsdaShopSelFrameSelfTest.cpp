// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ПАЧКИ 16, БАГ 6 (08.11.2026): белая рамка выбранного товара в магазинах.
//
// ЧТО ПРОВЕРЯЕТ (путь игрока: щелчки `SimulateClickAt` по ЖИВОЙ геометрии иконок, закон `diagnostic-must-use-real-path`):
//   П. Премиум-магазин 6000 — рамка 6273 `SHELLITEM_POP00_SEL_*` (порт `uiTabPanel_MoveSelectionHighlight@0xC27030`):
//      П1 после открытия рамки нет; П2 щелчок по иконке карточки 0 — рамка видна и стоит вокруг карточки 0 со смещением `.JUI`
//      (−2, −3); П3 щелчок по карточке 4 — рамка переехала на карточку 4; П4 кнопка «Купить» карточки 1 (`tradeSelectSlot`) — рамка
//      на карточке 1; П5 «Сброс» 6037 рамку не снимает (оригинал `case 0x1795`); П6 смена вкладки — рамка гаснет; П7 ключ отката
//      `asda.PremiumMall.SelFrame 0` — щелчок рамку не ставит.
//   Б. Магазин бустеров 15300 — рамка 15496 `BOOSTER_ITEM_SEL_*` после щелчка по иконке ячейки 0 видна вокруг ячейки (−2, −2).
//      ★ 12.11.2026: Б3 закрыть и снова открыть — рамки нет (ключ `asda.BoosterShop.SelFrameResetOnOpen` 1, дефект оригинала
//      исправлен); Б4 то же с ключом 0 — рамка остаётся, как в оригинале.
//   С. Магазин снаряжения 6800 — рамка ячейки `SHELLITEM2_POP00_SEL_*` после одиночного щелчка по иконке карточки 0 видна.
//   Кадры — `Reports/batch16/shop_outline/<язык>_<кадр>.png`.
//
// Консоль: `asda.Shop.SelFrameSelfTest [delay=N] [quit]`. Только офлайн. Строки — «САМОТЕСТ-РАМКА», итог — «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "UI/AsdaPremiumShopSubsystem.h"
#include "UI/AsdaBoosterShopSubsystem.h"
#include "UI/AsdaBoosterPanelSubsystem.h"
#include "UI/AsdaEquipShopSubsystem.h"
#include "UI/AsdaPurchaseBoxSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Widget.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSelFrameTest, Log, All);

namespace AsdaSelFrameTestLocal
{
	static const TCHAR* kWho = TEXT("asda.Shop.SelFrameSelfTest");   // имя самотеста (сторож ключей)

	// Живые узлы мира самотеста, имя которых начинается с `Prefix` (у узла есть родитель — он в дереве открытого окна).
	static void FindLive(UWorld* W, const FString& Prefix, TArray<UWidget*>& Out)
	{
		Out.Reset();
		for (TObjectIterator<UWidget> It; It; ++It)
		{
			UWidget* X = *It;
			if (X && X->GetWorld() == W && X->GetParent() && X->GetName().StartsWith(Prefix)) { Out.Add(X); }
		}
		Out.Sort([](const UWidget& A, const UWidget& B) { return A.GetName() < B.GetName(); });
	}
	static UWidget* FindLiveExact(UWorld* W, const FString& Name)
	{
		for (TObjectIterator<UWidget> It; It; ++It)
		{
			UWidget* X = *It;
			if (X && X->GetWorld() == W && X->GetParent() && X->GetName() == Name) { return X; }
		}
		return nullptr;
	}
	// Центр живой геометрии узла (абсолютные координаты Slate — система `SimulateClickAt`).
	using AsdaSelfTestInput::CenterOf;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.
	// Сколько планов рамки видно.
	static int32 VisibleCount(const TArray<UWidget*>& Planes)
	{
		int32 N = 0;
		for (const UWidget* X : Planes) { N += (X && X->IsVisible()) ? 1 : 0; }
		return N;
	}
}

struct FAsdaSelFrameSelfTest
{
	int32 Ok = 0;     // пройдено
	int32 Fail = 0;   // провалено
	TArray<TFunction<float(UWorld*)>> Steps;   // шаг возвращает паузу (с) до следующего
	FString Lang;                              // ru / en — префикс кадров
	FString ShotDir;                           // папка кадров

	void Expect(bool b, const FString& Name, const FString& Detail = FString())
	{
		if (b) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaSelFrameTest, Log, TEXT("САМОТЕСТ-РАМКА %s: %s%s%s"), b ? TEXT("ok") : TEXT("ПРОВАЛ"), *Name,
			Detail.IsEmpty() ? TEXT("") : TEXT(" — "), *Detail);
	}
	void Shot(const TCHAR* Name)
	{
		const FString File = FPaths::Combine(ShotDir, FString::Printf(TEXT("%s_%s.png"), *Lang, Name));
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaSelFrameTest, Log, TEXT("САМОТЕСТ-РАМКА кадр: %s"), *File);
	}
	static UGameInstance* GI(UWorld* W) { return W ? W->GetGameInstance() : nullptr; }
	template <class T> static T* Sub(UWorld* W) { UGameInstance* G = GI(W); return G ? G->GetSubsystem<T>() : nullptr; }

	// Щелчок ЛКМ по центру живой геометрии узла.
	bool Click(UWorld* W, const UWidget* X, const TCHAR* What)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
		FVector2D C;
		if (!WM || !AsdaSelFrameTestLocal::CenterOf(X, C))
		{
			Expect(false, FString::Printf(TEXT("щелчок: %s"), What), TEXT("нет живой геометрии узла"));
			return false;
		}
		WM->SimulateClickAt(C, 2, false);
		return true;
	}

	// Рамка 6273 стоит вокруг карточки `Card`: все 8 планов видны, угол — на (−2, −3) от угла карточки (смещение `.JUI`).
	void ExpectPremiumFrameOn(UWorld* W, int32 Card, const FString& Name)
	{
		using namespace AsdaSelFrameTestLocal;
		UAsdaPremiumShopSubsystem* P = Sub<UAsdaPremiumShopSubsystem>(W);
		TArray<UWidget*> Planes;
		FindLive(W, TEXT("SHELLITEM_POP00_SEL_"), Planes);
		UWidget* Root = P ? P->DebugNode(FString::Printf(TEXT("SHELLITEM_POP%02d_grp"), Card)) : nullptr;
		const UCanvasPanelSlot* FS = Planes.Num() > 0 ? AsdaUi::SlotOf(Planes[0]) : nullptr;
		const UCanvasPanelSlot* CS = Root ? AsdaUi::SlotOf(Root) : nullptr;
		const FVector2D D = (FS && CS) ? FS->GetPosition() - CS->GetPosition() : FVector2D(9999.f, 9999.f);
		FVector2D Mn(0.f, 0.f), Mx(0.f, 0.f), CMn(0.f, 0.f), CMx(0.f, 0.f);
		const bool bGeo = Planes.Num() > 0 && AsdaUi::WidgetAbsRect(Planes[0], Mn, Mx) && !(Mx - Mn).IsNearlyZero()
			&& Root && AsdaUi::WidgetAbsRect(Root, CMn, CMx);
		const int32 Vis = VisibleCount(Planes);
		Expect(P && P->DebugSelCard() == Card && Planes.Num() == 8 && Vis == 8 && FMath::IsNearlyEqual(D.X, -2.f, 0.5f)
			&& FMath::IsNearlyEqual(D.Y, -3.f, 0.5f) && bGeo,
			Name, FString::Printf(TEXT("выбрана=%d, планов %d, видно %d, смещение от карточки (%.1f, %.1f), угол рамки на экране (%.0f, %.0f), карточки (%.0f, %.0f)"),
				P ? P->DebugSelCard() : -2, Planes.Num(), Vis, D.X, D.Y, Mn.X, Mn.Y, CMn.X, CMn.Y));
	}
	void ExpectPremiumFrameHidden(UWorld* W, const FString& Name)
	{
		using namespace AsdaSelFrameTestLocal;
		TArray<UWidget*> Planes;
		FindLive(W, TEXT("SHELLITEM_POP00_SEL_"), Planes);
		const int32 Vis = VisibleCount(Planes);
		Expect(Planes.Num() == 8 && Vis == 0, Name, FString::Printf(TEXT("планов %d, видно %d"), Planes.Num(), Vis));
	}

	void BuildSteps()
	{
		using namespace AsdaSelFrameTestLocal;
		// ═══ П. Премиум-магазин 6000 ═══
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaPremiumShopSubsystem* P = Sub<UAsdaPremiumShopSubsystem>(W);
			if (P && P->IsOpen()) { P->CloseWindow(); }
			if (P) { P->OpenWindow(false); }
			Expect(P && P->IsOpen() && P->DebugCardCode(0) > 0 && P->DebugCardCode(4) > 0,
				TEXT("П0 премиум-магазин открыт, карточки 0 и 4 с товаром"),
				FString::Printf(TEXT("код0=%d код4=%d"), P ? P->DebugCardCode(0) : 0, P ? P->DebugCardCode(4) : 0));
			return 1.0f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			ExpectPremiumFrameHidden(W, TEXT("П1 после открытия рамки 6273 нет"));
			Shot(TEXT("premium_open"));
			UAsdaPremiumShopSubsystem* P = Sub<UAsdaPremiumShopSubsystem>(W);
			Click(W, P ? P->DebugCardIcon(0) : nullptr, TEXT("иконка карточки 0"));
			return 0.8f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			ExpectPremiumFrameOn(W, 0, TEXT("П2 щелчок по иконке карточки 0 — рамка вокруг карточки 0"));
			Shot(TEXT("premium_card0"));
			UAsdaPremiumShopSubsystem* P = Sub<UAsdaPremiumShopSubsystem>(W);
			Click(W, P ? P->DebugCardIcon(4) : nullptr, TEXT("иконка карточки 4"));
			return 0.8f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			ExpectPremiumFrameOn(W, 4, TEXT("П3 щелчок по карточке 4 — рамка переехала на карточку 4"));
			Shot(TEXT("premium_card4"));
			UAsdaPremiumShopSubsystem* P = Sub<UAsdaPremiumShopSubsystem>(W);
			const UWidget* Buy = P ? P->DebugCardBuyWidget(1) : nullptr;
			Click(W, Buy, TEXT("кнопка «Купить» карточки 1"));
			return 0.8f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			ExpectPremiumFrameOn(W, 1, TEXT("П4 кнопка «Купить» карточки 1 (tradeSelectSlot) — рамка на карточке 1"));
			Shot(TEXT("premium_buy1"));
			return 0.6f;   // кадр снимается на следующем кадре движка — окно закрываем отдельным шагом
		});
		Steps.Add([this](UWorld* W) -> float
		{
			// Модалку подтверждения 1559 / панель вариантов закрываем, покупку не подтверждаем.
			if (UAsdaPurchaseBoxSubsystem* Box = Sub<UAsdaPurchaseBoxSubsystem>(W)) { if (Box->IsOpen()) { Box->Close(); } }
			if (UAsdaPremiumShopSubsystem* P = Sub<UAsdaPremiumShopSubsystem>(W))
			{
				P->CloseWindow();
				P->OpenWindow(false);
			}
			return 0.8f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaPremiumShopSubsystem* P = Sub<UAsdaPremiumShopSubsystem>(W);
			Click(W, P ? P->DebugCardIcon(2) : nullptr, TEXT("иконка карточки 2"));
			return 0.8f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaPremiumShopSubsystem* P = Sub<UAsdaPremiumShopSubsystem>(W);
			if (P) { P->OnResetClicked(); }
			ExpectPremiumFrameOn(W, 2, TEXT("П5 «Сброс» 6037 рамку не снимает (оригинал case 0x1795)"));
			// Листание (оригинал гасит рамку: LABEL_57 `ida.c:266158`) — на странице 2, если она есть.
			if (P) { P->SetPage(1); }
			return 0.8f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			ExpectPremiumFrameHidden(W, TEXT("П6а листание на страницу 2 — рамка погасла"));
			UAsdaPremiumShopSubsystem* P = Sub<UAsdaPremiumShopSubsystem>(W);
			if (P) { P->SetPage(0); }
			Click(W, P ? P->DebugCardIcon(3) : nullptr, TEXT("иконка карточки 3"));
			return 0.8f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			ExpectPremiumFrameOn(W, 3, TEXT("П6б щелчок по карточке 3 после листания — рамка на карточке 3"));
			// Вкладка 2 («Расходные»): вкладка 1 «Новинка!» скрыта решением владельца и не открывается.
			if (UAsdaPremiumShopSubsystem* P = Sub<UAsdaPremiumShopSubsystem>(W)) { P->SetCategory(P->GetCategory() == 2 ? 0 : 2); }
			return 0.8f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			ExpectPremiumFrameHidden(W, TEXT("П6в смена вкладки — рамка погасла"));
			UAsdaPremiumShopSubsystem* P = Sub<UAsdaPremiumShopSubsystem>(W);
			Expect(P && P->DebugSelCard() == -1, TEXT("П6в смена вкладки — выбор сброшен"),
				FString::Printf(TEXT("выбрана=%d"), P ? P->DebugSelCard() : -2));
			Shot(TEXT("premium_tab_switch"));
			AsdaCVarSave::SetForTest(TEXT("asda.PremiumMall.SelFrame"), 0);
			Click(W, P ? P->DebugCardIcon(0) : nullptr, TEXT("иконка карточки 0 (ключ 0)"));
			return 0.8f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			ExpectPremiumFrameHidden(W, TEXT("П7 ключ отката asda.PremiumMall.SelFrame 0 — рамки нет (как до правки)"));
			AsdaCVarSave::RestoreAll(AsdaSelFrameTestLocal::kWho);
			if (UAsdaPremiumShopSubsystem* P = Sub<UAsdaPremiumShopSubsystem>(W)) { P->CloseWindow(); }
			return 0.5f;
		});

		// ═══ Б. Магазин бустеров 15300 ═══
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaBoosterShopSubsystem* B = Sub<UAsdaBoosterShopSubsystem>(W);
			if (B && B->IsOpen()) { B->CloseWindow(); }
			if (B) { B->OpenWindow(); }
			Expect(B && B->IsOpen(), TEXT("Б0 магазин бустеров открыт"));
			return 1.0f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			TArray<UWidget*> Planes;
			AsdaSelFrameTestLocal::FindLive(W, TEXT("BOOSTER_ITEM_SEL_"), Planes);
			Expect(Planes.Num() == 8 && AsdaSelFrameTestLocal::VisibleCount(Planes) == 0, TEXT("Б1 после открытия рамки 15496 нет"),
				FString::Printf(TEXT("планов %d, видно %d"), Planes.Num(), AsdaSelFrameTestLocal::VisibleCount(Planes)));
			TArray<UWidget*> Icons;   // иконка ячейки 0 — `BOOSTER_ITEM_ICON00_<n>_0` (15310), по ней ловит щелчок латч окна
			AsdaSelFrameTestLocal::FindLive(W, TEXT("BOOSTER_ITEM_ICON00_"), Icons);
			Click(W, Icons.Num() > 0 ? Icons[0] : nullptr, TEXT("иконка ячейки 0 магазина бустеров"));
			return 0.8f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			TArray<UWidget*> Planes;
			AsdaSelFrameTestLocal::FindLive(W, TEXT("BOOSTER_ITEM_SEL_"), Planes);
			UWidget* Cell = AsdaSelFrameTestLocal::FindLiveExact(W, TEXT("BOOSTER_ITEM00_grp"));
			const UCanvasPanelSlot* FS = Planes.Num() > 0 ? AsdaUi::SlotOf(Planes[0]) : nullptr;
			const UCanvasPanelSlot* CS = Cell ? AsdaUi::SlotOf(Cell) : nullptr;
			const FVector2D D = (FS && CS) ? FS->GetPosition() - CS->GetPosition() : FVector2D(9999.f, 9999.f);
			const int32 Vis = AsdaSelFrameTestLocal::VisibleCount(Planes);
			Expect(Planes.Num() == 8 && Vis == 8 && FMath::IsNearlyEqual(D.X, -2.f, 0.5f) && FMath::IsNearlyEqual(D.Y, -2.f, 0.5f),
				TEXT("Б2 щелчок по ячейке 0 — рамка 15496 вокруг ячейки 0"),
				FString::Printf(TEXT("планов %d, видно %d, смещение (%.1f, %.1f)"), Planes.Num(), Vis, D.X, D.Y));
			Shot(TEXT("booster_cell0"));
			// ★ 12.11.2026: повторное открытие — выбор сброшен, рамка обязана погаснуть (ключ по умолчанию 1).
			if (UAsdaBoosterPanelSubsystem* Pv = Sub<UAsdaBoosterPanelSubsystem>(W)) { Pv->ClosePrevWindow(); }
			if (UAsdaBoosterShopSubsystem* B = Sub<UAsdaBoosterShopSubsystem>(W)) { B->CloseWindow(); B->OpenWindow(); }
			return 1.0f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			TArray<UWidget*> Planes;
			AsdaSelFrameTestLocal::FindLive(W, TEXT("BOOSTER_ITEM_SEL_"), Planes);
			Expect(Planes.Num() == 8 && AsdaSelFrameTestLocal::VisibleCount(Planes) == 0,
				TEXT("Б3 повторное открытие — рамки 15496 нет (выбор сброшен)"),
				FString::Printf(TEXT("планов %d, видно %d"), Planes.Num(), AsdaSelFrameTestLocal::VisibleCount(Planes)));
			// Ключ 0 — как оригинал: рамку ставим щелчком, после повторного открытия она остаётся.
			AsdaCVarSave::SetForTest(TEXT("asda.BoosterShop.SelFrameResetOnOpen"), 0);
			TArray<UWidget*> Icons;
			AsdaSelFrameTestLocal::FindLive(W, TEXT("BOOSTER_ITEM_ICON00_"), Icons);
			Click(W, Icons.Num() > 0 ? Icons[0] : nullptr, TEXT("иконка ячейки 0 магазина бустеров (ключ 0)"));
			return 0.8f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			if (UAsdaBoosterPanelSubsystem* Pv = Sub<UAsdaBoosterPanelSubsystem>(W)) { Pv->ClosePrevWindow(); }
			if (UAsdaBoosterShopSubsystem* B = Sub<UAsdaBoosterShopSubsystem>(W)) { B->CloseWindow(); B->OpenWindow(); }
			return 1.0f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			TArray<UWidget*> Planes;
			AsdaSelFrameTestLocal::FindLive(W, TEXT("BOOSTER_ITEM_SEL_"), Planes);
			Expect(Planes.Num() == 8 && AsdaSelFrameTestLocal::VisibleCount(Planes) == 8,
				TEXT("Б4 ключ asda.BoosterShop.SelFrameResetOnOpen 0 — рамка после повторного открытия остаётся (как оригинал)"),
				FString::Printf(TEXT("планов %d, видно %d"), Planes.Num(), AsdaSelFrameTestLocal::VisibleCount(Planes)));
			AsdaCVarSave::RestoreAll(AsdaSelFrameTestLocal::kWho);
			return 0.3f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			if (UAsdaBoosterPanelSubsystem* Pv = Sub<UAsdaBoosterPanelSubsystem>(W)) { Pv->ClosePrevWindow(); }
			if (UAsdaBoosterShopSubsystem* B = Sub<UAsdaBoosterShopSubsystem>(W)) { B->CloseWindow(); }
			return 0.5f;
		});

		// ═══ С. Магазин снаряжения 6800 ═══
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaEquipShopSubsystem* E = Sub<UAsdaEquipShopSubsystem>(W);
			if (E && E->IsOpen()) { E->CloseWindow(); }
			if (E) { E->OpenWindow(); }
			Expect(E && E->IsOpen() && E->DebugCardCode(0) > 0, TEXT("С0 магазин снаряжения 6800 открыт, карточка 0 с товаром"),
				FString::Printf(TEXT("код0=%d"), E ? E->DebugCardCode(0) : 0));
			return 1.0f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaEquipShopSubsystem* E = Sub<UAsdaEquipShopSubsystem>(W);
			Click(W, E ? E->DebugCardIcon(0) : nullptr, TEXT("иконка карточки 0 магазина 6800"));
			return 0.8f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaEquipShopSubsystem* E = Sub<UAsdaEquipShopSubsystem>(W);
			TArray<UWidget*> Planes;
			AsdaSelFrameTestLocal::FindLive(W, TEXT("SHELLITEM2_POP00_SEL_"), Planes);
			const int32 Vis = AsdaSelFrameTestLocal::VisibleCount(Planes);
			Expect(E && E->DebugSelCard() == 0 && Planes.Num() > 0 && Vis == Planes.Num(),
				TEXT("С1 одиночный щелчок по карточке 0 — рамка ячейки видна"),
				FString::Printf(TEXT("выбрана=%d, планов %d, видно %d"), E ? E->DebugSelCard() : -2, Planes.Num(), Vis));
			Shot(TEXT("equip6800_card0"));
			return 0.6f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			if (UAsdaEquipShopSubsystem* E = Sub<UAsdaEquipShopSubsystem>(W)) { E->CloseWindow(); }
			return 0.5f;
		});
	}
};

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GAsdaSelFrameSelfTest(
		TEXT("asda.Shop.SelFrameSelfTest"),
		TEXT("asda.Shop.SelFrameSelfTest [delay=N] [quit] — ОФЛАЙН-самотест пачки 16 (баг 6): белая рамка выбранного товара ")
		TEXT("премиум-магазина 6000, магазина бустеров 15300 и магазина снаряжения 6800, кадры RU/EN."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const TCHAR* Who = AsdaSelFrameTestLocal::kWho;
			AsdaCVarSave::ArmWorldGuard(W, Who);
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
			if (AsdaSelfTestKit::RefuseStart(W, Who, KitArgs)) { return; }
			float Delay = 0.f;
			Delay = AsdaSelfTestKit::DelayArgOr(Args, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (был отдельный цикл по аргументам с одной этой веткой)
			const bool bQuit = Args.Contains(TEXT("quit"));
			TWeakObjectPtr<UWorld> WW(W);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit, Who](float) -> bool
			{
				UWorld* W2 = WW.Get();
				if (!W2) { return false; }
				if (AsdaSelfTestKit::RefuseDeferred(W2, Who, bQuit)) { return false; }
				TSharedRef<FAsdaSelFrameSelfTest> T = MakeShared<FAsdaSelFrameSelfTest>();
				T->Lang = FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en");
				T->ShotDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/batch16/shop_outline")));
				T->BuildSteps();
				TSharedRef<int32> Idx = MakeShared<int32>(0);
				TSharedRef<double> Next = MakeShared<double>(0.0);
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, T, Idx, Next, bQuit, Who](float) -> bool
				{
					UWorld* W3 = WW.Get();
					if (!W3) { AsdaCVarSave::RestoreAll(Who); return false; }
					const double Now = FPlatformTime::Seconds();
					if (Now < *Next) { return true; }
					// ★ 11.11.2026 (D4b): следующий шаг не начинается, пока заказанный прошлым шагом щелчок не доставлен (нажатие,
					//   удержание, отпускание идут по кадрам). Под нагрузкой фиксированной паузы шага не хватало: проверка читала
					//   состояние до щелчка, а следующий заказ щелчка затирал недоставленный. Потолок ожидания — 5 с сверх паузы.
					if (const UAsdaWindowSubsystem* ClickWm = W3->GetGameInstance() ? W3->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
					{
						if (ClickWm->IsSimClickPending() && Now < *Next + 5.0) { return true; }
					}
					if (*Idx < T->Steps.Num()) { *Next = Now + T->Steps[(*Idx)++](W3); return true; }
					AsdaCVarSave::RestoreAll(Who);
					UE_LOG(LogAsdaSelFrameTest, Log, TEXT("САМОТЕСТ-РАМКА ИТОГ: ok=%d fail=%d"), T->Ok, T->Fail);
					AsdaSelfTestKit::Quit(W3, bQuit);
					return false;
				}), 0.f);
				return false;
			}), Delay);
			UE_LOG(LogAsdaSelFrameTest, Log, TEXT("САМОТЕСТ-РАМКА: запуск через %.1f с"), Delay);
		}));
}

#endif // !UE_BUILD_SHIPPING
