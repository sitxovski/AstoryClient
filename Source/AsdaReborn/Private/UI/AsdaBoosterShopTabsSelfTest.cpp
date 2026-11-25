// Copyright (c) 2026 @sitxovski (Discord)
// AsdaBoosterShopTabsSelfTest.cpp — самотест [ИСПРАВЛЕНИЯ] 31.10.2026 «вкладки магазина бустеров 15300 по составу бустеров»
//   (задача владельца у NPC Пре-Пре: вкладки «Воин/Лучник/Маг/Доспех/Инструмент» были не распределены — у всех 72 записей
//   витрины категория 12, а классы оружия эталон брал из подтипа самого бустера 66). Разбор — `PassesCategory` и ведомость
//   `Tools/data/booster_shop_category.csv`.
//
// СЦЕНАРИЙ `asda.PremiumMall.BoosterTabsSelfTest [quit]` (офлайн, путь игрока — те же входы, что мышь):
//   открыть окно 15300 (`OpenWindow`), по каждой вкладке `SetCategory(i)`: число карточек из диагностики окна (`SelfTest`,
//   тот же фильтр, что строит страницу) — ждём Все 72, Популярные 0, Воин 21, Лучник 21, Маг 21, Доспех 9, Инструмент 0;
//   на ПЕРВОЙ странице вкладки видимые тексты окна содержат имя «своего» бустера (на языке сессии) и не содержат «чужого»:
//   Воин — копьё стража правосудия 62727, Лучник — баллиста 62721, Маг — посох 62724, Доспех — набор брони 62784.
//   Ключ `asda.PremiumMall.BoosterCatByContents 0` — вкладки оружия снова пусты (как эталон). Окно закрывается.
//   Итог `[вкладки бустеров] ИТОГ: ok=N fail=M`.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"   // ASDA_DEV_CONSOLE — дев-команда вне Shipping
#include "UI/AsdaBoosterShopSubsystem.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaBoosterTabsTest, Log, All);

#if ASDA_DEV_CONSOLE   // дев-команда, в Shipping не регистрируется
namespace AsdaBoosterTabsTest
{
	// Видимые тексты окна магазина бустеров (узел и вся цепочка родителей видимы).
	static TArray<FString> VisibleTexts(UWorld* W)
	{
		TArray<FString> Out;
		for (TObjectIterator<UUserWidget> It; It; ++It)
		{
			UUserWidget* U = *It;
			if (!U || U->GetWorld() != W || !U->IsInViewport() || !U->WidgetTree || !U->GetClass()->GetName().Contains(TEXT("BOOSTER_SHOP"))) { continue; }
			TArray<UWidget*> All;
			U->WidgetTree->GetAllWidgets(All);
			for (UWidget* Wd : All)
			{
				const UTextBlock* Tb = Cast<UTextBlock>(Wd);
				if (!Tb) { continue; }
				bool bVis = true;
				for (const UWidget* Up = Tb; Up; Up = Up->GetParent()) { if (!Up->IsVisible()) { bVis = false; break; } }
				if (bVis) { Out.Add(Tb->GetText().ToString()); }
			}
		}
		return Out;
	}

	// Число карточек вкладки `Idx` из диагностики окна: строка «    <Idx> <имя>  <N> карточек».
	static int32 CountFromDump(const FString& Dump, int32 Idx)
	{
		TArray<FString> Lines;
		Dump.ParseIntoArrayLines(Lines);
		const FString Head = FString::Printf(TEXT("%d "), Idx);
		for (const FString& L : Lines)
		{
			const FString T = L.TrimStart();
			if (!T.StartsWith(Head) || !T.Contains(TEXT("карточек"))) { continue; }
			TArray<FString> Parts;
			T.ParseIntoArrayWS(Parts);
			for (int32 i = 1; i + 1 < Parts.Num(); ++i)
			{
				if (Parts[i + 1].StartsWith(TEXT("карточек")) && Parts[i].IsNumeric()) { return FCString::Atoi(*Parts[i]); }
			}
		}
		return -1;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaBoosterTabsSelfTest(
	TEXT("asda.PremiumMall.BoosterTabsSelfTest"),
	TEXT("asda.PremiumMall.BoosterTabsSelfTest [quit] — [ИСПРАВЛЕНИЕ] 31.10.2026: вкладки магазина бустеров 15300 по составу "
	     "бустеров (Воин/Лучник/Маг/Доспех), путь игрока, язык сессии. Только офлайн."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		using namespace AsdaBoosterTabsTest;
		static const TCHAR* const kCmd = TEXT("asda.PremiumMall.BoosterTabsSelfTest");
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (!W || AsdaSelfTestKit::RefuseStart(W, kCmd, Args)) { return; }
		UGameInstance* GI = W->GetGameInstance();
		UAsdaBoosterShopSubsystem* Bs = GI ? GI->GetSubsystem<UAsdaBoosterShopSubsystem>() : nullptr;
		const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		if (!Bs || !IT || Bs->IsOpen())
		{
			AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет подсистем или окно 15300 уже открыто игроком"));
			return;
		}
		int32 Ok = 0, Fail = 0;
		auto Check = [&Ok, &Fail](bool b, const FString& What)
		{
			(b ? Ok : Fail)++;
			UE_LOG(LogAsdaBoosterTabsTest, Log, TEXT("[вкладки бустеров] %s — %s"), b ? TEXT("ok  ") : TEXT("FAIL"), *What);
		};
		static const TCHAR* const Names[7] = { TEXT("Все"), TEXT("Популярные"), TEXT("Воин"), TEXT("Лучник"), TEXT("Маг"),
			TEXT("Доспех"), TEXT("Инструмент") };
		const int32 Want[7] = { 72, 0, 21, 21, 21, 9, 0 };
		// «Свой» бустер вкладки (первый в порядке витрины) и «чужой» — для проверки видимых карточек первой страницы.
		struct FMark { int32 Tab; int32 Own; int32 Foreign; };
		const FMark Marks[] = { { 2, 62727, 62721 }, { 3, 62721, 62724 }, { 4, 62724, 62727 }, { 5, 62784, 62727 } };

		Bs->OpenWindow();
		for (int32 i = 0; i < 7; ++i)
		{
			Bs->SetCategory(i);
			const int32 N = CountFromDump(Bs->SelfTest(), i);
			Check(N == Want[i], FString::Printf(TEXT("вкладка %d «%s»: карточек %d (ждём %d)"), i, Names[i], N, Want[i]));
		}
		for (const FMark& Mk : Marks)
		{
			Bs->SetCategory(Mk.Tab);
			const TArray<FString> T = VisibleTexts(W);
			const FString Own = IT->GetItemName(Mk.Own), Foreign = IT->GetItemName(Mk.Foreign);
			const bool bOwn = T.ContainsByPredicate([&Own](const FString& S) { return S.Contains(Own); });
			const bool bForeign = T.ContainsByPredicate([&Foreign](const FString& S) { return S.Contains(Foreign); });
			Check(bOwn && !bForeign, FString::Printf(TEXT("вкладка «%s», 1-я страница: видна «%s» — %d, нет «%s» — %d"),
				Names[Mk.Tab], *Own, (int32)bOwn, *Foreign, (int32)!bForeign));
		}
		// Ключ отката: класс по подтипу самого бустера (66) — вкладки оружия пусты, как в эталоне.
		IConsoleVariable* Key = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.PremiumMall.BoosterCatByContents"));
		if (Key)
		{
			const int32 Was = Key->GetInt();
			Key->Set(0, ECVF_SetByConsole);
			Bs->SetCategory(2);
			const int32 N0 = CountFromDump(Bs->SelfTest(), 2);
			Key->Set(Was, ECVF_SetByConsole);
			Check(N0 == 0, FString::Printf(TEXT("asda.PremiumMall.BoosterCatByContents 0 — «Воин» пуст, как эталон: %d"), N0));
		}
		Bs->SetCategory(0);
		Bs->CloseWindow();
		AsdaSelfTestKit::Finish(W, kCmd, Args.bQuit, LogAsdaBoosterTabsTest, TEXT("[вкладки бустеров]"), Ok, Fail);
	}));
#endif // ASDA_DEV_CONSOLE
