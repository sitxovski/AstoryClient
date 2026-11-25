// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPremiumBuyVariantSelfTest.cpp — САМОТЕСТ ВЫБОРА КЛАССА И УРОВНЯ В ОКНЕ ПОКУПКИ 6000 (06.11.2026; Reports/premium_circle_bike/REPORT_VARIANTS.md).
//
//   `asda.SelfTest.PremiumBuyVariant [out=<папка>] [quit]` — офлайн (`-AsdaNoNet`), НАСТОЯЩИЕ щелчки Slate (`SimulateClickAt`) по живой геометрии:
//     A покупка: карточка 61740 «Купить» → панель 6296 (уровни 1/40/60) → комбо → уровень 40 → цена и код 61755 → «Купить» → 1559 → ОК → 5070 с кодом 61755 → панель закрыта;
//     B класс: карточка другого класса набора 36 (62023): выбран её класс, смена класса в комбо меняет код;
//     C подарок: карточка 61740 «Подарок» → панель → уровень 60 → код 61770 → модалка подарка с этим кодом;
//     D карточка без вариантов (492): сразу модалка 1559, панели нет.
//   Кадры `asda_variant_*.png` (копия в `out=`). Итог `ВАРИАНТЫ-6000 ИТОГ: ok=N fail=M`.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
#include "Test/AsdaSelfTestKit.h"
#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "Text/AsdaLang.h"
#include "UI/AsdaInventorySubsystem.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaCashGiftSubsystem.h"
#include "UI/AsdaPremiumShopSubsystem.h"
#include "UI/AsdaPurchaseBoxSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Components/Widget.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaBuyVariantTest, Log, All);

namespace AsdaBuyVariantTest
{
	static const TCHAR* kCmd    = TEXT("asda.SelfTest.PremiumBuyVariant");
	static const TCHAR* kPrefix = TEXT("ВАРИАНТЫ-6000");

	// Где карточка: вкладка, подраздел, страница, номер.
	struct FWhere { int32 Cat = -1; int32 Sub = -1; int32 Page = -1; int32 Card = -1; };

	// Состояние прогона: шаги с задержкой на тикере (щелчкам нужен живой тик).
	struct FRun : public TSharedFromThis<FRun>
	{
		TWeakObjectPtr<UGameInstance> GI;
		bool bQuit = false;
		FString OutDir;
		int32 Ok = 0, Fail = 0;
		TMap<int32, FWhere> Where;
		TArray<FString> Shots;
		TArray<TPair<float, TFunction<void(FRun&)>>> Steps;
		int32 Next = 0;
		double DueAt = 0.0;

		UWorld* W() const { return GI.IsValid() ? GI->GetWorld() : nullptr; }
		template <class T> T* Sub() const { return GI.IsValid() ? GI->GetSubsystem<T>() : nullptr; }
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaBuyVariantTest, Log,   TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaBuyVariantTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
		void Add(float Delay, TFunction<void(FRun&)> Fn) { Steps.Emplace(Delay, MoveTemp(Fn)); }
		void Shot(const TCHAR* Name)
		{
			Shots.Add(Name);
			FScreenshotRequest::RequestScreenshot(Name, /*bShowUI*/ true, /*bAddUniqueSuffix*/ false);
		}
		// Настоящий щелчок по центру видимого узла.
		void Click(UWidget* N, const FString& Tag)
		{
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>();
			FVector2D Mn, Mx;
			const bool bGeo = WM && N && AsdaUi::IsShown(N) && AsdaUi::WidgetAbsRect(N, Mn, Mx) && (Mx - Mn).X > 1.0 && (Mx - Mn).Y > 1.0;
			Check(bGeo, FString::Printf(TEXT("%s: узел на экране"), *Tag));
			if (bGeo) { WM->SimulateClickAt((Mn + Mx) * 0.5f, 2); }
		}
		void Start()
		{
			DueAt = FPlatformTime::Seconds() + (Steps.Num() ? Steps[0].Key : 0.f);
			TSharedRef<FRun> Self = AsShared();
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self](float) { return Self->TickOnce(); }));
		}
		bool TickOnce()
		{
			if (FPlatformTime::Seconds() < DueAt) { return true; }
			if (Next >= Steps.Num()) { return false; }
			TFunction<void(FRun&)> Fn = Steps[Next].Value;
			++Next;
			Fn(*this);
			if (Next >= Steps.Num())
			{
				AsdaSelfTestKit::Finish(W(), kCmd, bQuit, LogAsdaBuyVariantTest, kPrefix, Ok, Fail);
				return false;
			}
			DueAt = FPlatformTime::Seconds() + Steps[Next].Key;
			return true;
		}
	};

	// Открыть страницу карточки.
	static void Go(FRun& R, int32 Code)
	{
		UAsdaPremiumShopSubsystem* P = R.Sub<UAsdaPremiumShopSubsystem>();
		const FWhere* Wh = R.Where.Find(Code);
		if (!P || !Wh) { R.Check(false, FString::Printf(TEXT("карточка %d не найдена на витрине"), Code)); return; }
		P->SetCategory(Wh->Cat);
		if (P->GetCategory() == 3) { P->SetEquipSub(Wh->Sub); } else { P->SetToolSub(Wh->Sub); }
		P->SetPage(Wh->Page);
	}

	// Обход витрины: страницы нужных карточек.
	static void Scan(FRun& R)
	{
		UAsdaPremiumShopSubsystem* P = R.Sub<UAsdaPremiumShopSubsystem>();
		if (!P) { R.Check(false, TEXT("нет подсистемы магазина")); return; }
		P->OpenWindow();
		P->DebugSetGxp(999999);
		const TSet<int32> Want = { 61740, 62023, 492, 79940 };
		for (int32 Cat = 0; Cat < 8; ++Cat)
		{
			for (int32 SubI = 0; SubI < 5; ++SubI)
			{
				P->SetCategory(Cat);
				if (P->GetCategory() == 3) { if (SubI > 2) { continue; } P->SetEquipSub(SubI); } else { P->SetToolSub(SubI); }
				for (int32 Pg = 0; Pg < 20; ++Pg)
				{
					P->SetPage(Pg);
					for (int32 Card = 0; Card < UAsdaPremiumShopSubsystem::kCards; ++Card)
					{
						const int32 Code = P->DebugCardCode(Card);
						if (Want.Contains(Code) && !R.Where.Contains(Code)) { R.Where.Add(Code, FWhere{ Cat, SubI, Pg, Card }); }
						if (Code == 79940 && Cat == 5) { R.Where.Add(Code, FWhere{ Cat, SubI, Pg, Card }); }   // вкладка «Питомец», а не «Горячая новинка!»
					}
				}
			}
		}
		R.Check(R.Where.Num() == Want.Num(), FString::Printf(TEXT("найдено карточек %d из %d"), R.Where.Num(), Want.Num()));
	}

	// Цена варианта на панели = цене таблицы.
	static void CheckPanel(FRun& R, int32 WantCode, const TCHAR* Tag)
	{
		UAsdaPremiumShopSubsystem* P = R.Sub<UAsdaPremiumShopSubsystem>();
		const UAsdaItemTableSubsystem* IT = R.Sub<UAsdaItemTableSubsystem>();
		const FAsdaItemRecord* Rec = IT ? IT->GetItem(WantCode) : nullptr;
		const FString Price = Rec ? AsdaUi::FormatThousands(Rec->Price) : FString();
		R.Check(P && Rec && P->GetGiftItemCode() == WantCode && P->DebugEquipPrice() == Price,
			FString::Printf(TEXT("%s: код панели %d (ждём %d), цена «%s» (ждём «%s»)"), Tag, P ? P->GetGiftItemCode() : -1, WantCode, P ? *P->DebugEquipPrice() : TEXT(""), *Price));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestPremiumBuyVariant(
	TEXT("asda.SelfTest.PremiumBuyVariant"),
	TEXT("Самотест выбора класса и уровня в окне покупки 6000 настоящими щелчками: покупка, смена класса, подарок, карточка без вариантов. `out=<папка>`, `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaBuyVariantTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA, 120.f)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет GameInstance")); return; }
		TSharedRef<FRun> R = MakeShared<FRun>();
		R->GI = GI;
		R->bQuit = KA.bQuit;
		for (const FString& A : KA.Pos) { if (A.StartsWith(TEXT("out="))) { R->OutDir = A.Mid(4); } }
		auto Shop = [](FRun& Rn) { return Rn.Sub<UAsdaPremiumShopSubsystem>(); };
		auto Box  = [](FRun& Rn) { return Rn.Sub<UAsdaPurchaseBoxSubsystem>(); };

		R->Add(0.1f, [](FRun& Rn) { Scan(Rn); });
		// ── A: покупка уровня 40 ──
		R->Add(0.5f, [](FRun& Rn) { Go(Rn, 61740); });
		R->Add(0.5f, [Shop](FRun& Rn) { Shop(Rn)->DebugSetCapture(true); Rn.Click(Shop(Rn)->DebugCardBuyWidget(Rn.Where.FindRef(61740).Card), TEXT("A «Купить» карточки 61740")); });
		R->Add(1.0f, [Shop, Box](FRun& Rn)
		{
			UAsdaPremiumShopSubsystem* P = Shop(Rn);
			Rn.Check(P->IsGiftPanelShown() && P->IsPanelBuy() && !Box(Rn)->IsOpen(), TEXT("A открыта панель 6296 в режиме покупки, модалки нет"));
			CheckPanel(Rn, 61740, TEXT("A первый уровень"));
			Rn.Shot(TEXT("asda_variant_panel"));
		});
		R->Add(1.5f, [Shop](FRun& Rn) { Rn.Click(Shop(Rn)->DebugNode(TEXT("SHELL_EQUIP_LV")), TEXT("A комбо уровня")); });
		R->Add(1.0f, [](FRun& Rn) { Rn.Shot(TEXT("asda_variant_combo")); });
		R->Add(1.5f, [Shop](FRun& Rn) { Rn.Click(Shop(Rn)->DebugNode(TEXT("SHELL_EQUIP_LV_Row1")), TEXT("A пункт уровня 40")); });
		R->Add(1.0f, [Shop](FRun& Rn) { CheckPanel(Rn, 61755, TEXT("A уровень 40")); Rn.Click(Shop(Rn)->DebugGiftGiveWidget(), TEXT("A «Купить» панели")); });
		R->Add(1.0f, [Box](FRun& Rn)
		{
			UAsdaPurchaseBoxSubsystem* B = Box(Rn);
			Rn.Check(B->IsOpen() && B->DebugRequest().ItemCode == 61755, FString::Printf(TEXT("A модалка 1559 с кодом %d (ждём 61755)"), B->DebugRequest().ItemCode));
			Rn.Shot(TEXT("asda_variant_1559"));
		});
		R->Add(1.5f, [Box](FRun& Rn) { Rn.Click(Box(Rn)->DebugOkWidget(), TEXT("A ОК модалки")); });
		R->Add(1.0f, [Shop](FRun& Rn)
		{
			UAsdaPremiumShopSubsystem* P = Shop(Rn);
			const TArray<TArray<uint8>>& S = P->DebugSentBodies();
			const int32 Got = S.Num() == 1 && S[0].Num() >= 7 ? *(const int32*)&S[0][3] : -1;
			Rn.Check(S.Num() == 1 && Got == 61755 && S[0][1] == 1, FString::Printf(TEXT("A 5070: пакетов %d, код %d (ждём 61755, таблица item_list)"), S.Num(), Got));
			Rn.Check(!P->IsGiftPanelShown(), TEXT("A панель закрыта после покупки"));
			P->DebugSetCapture(false);
			P->DebugClearPending();
		});
		// ── B: другой класс того же набора ──
		R->Add(0.5f, [](FRun& Rn) { Go(Rn, 62023); });
		R->Add(0.5f, [Shop](FRun& Rn) { Rn.Click(Shop(Rn)->DebugCardBuyWidget(Rn.Where.FindRef(62023).Card), TEXT("B «Купить» карточки 62023")); });
		R->Add(1.0f, [Shop](FRun& Rn)
		{
			CheckPanel(Rn, 62023, TEXT("B выбран класс карточки"));
			Rn.Shot(TEXT("asda_variant_class"));
		});
		R->Add(1.5f, [Shop](FRun& Rn) { Rn.Click(Shop(Rn)->DebugNode(TEXT("SHELL_EQUIP_JOB")), TEXT("B комбо класса")); });
		R->Add(1.0f, [Shop](FRun& Rn) { Rn.Click(Shop(Rn)->DebugNode(TEXT("SHELL_EQUIP_JOB_Row0")), TEXT("B пункт первого класса")); });
		R->Add(1.0f, [Shop](FRun& Rn)
		{
			UAsdaPremiumShopSubsystem* P = Shop(Rn);
			Rn.Check(P->GetGiftItemCode() == 62022, FString::Printf(TEXT("B смена класса: код %d (ждём 62022)"), P->GetGiftItemCode()));
			P->CloseGiftPanels();
		});
		// ── C: подарок, уровень 60 ──
		R->Add(0.5f, [](FRun& Rn) { Go(Rn, 61740); });
		R->Add(0.5f, [Shop](FRun& Rn) { Rn.Click(Shop(Rn)->DebugCardGiftWidget(Rn.Where.FindRef(61740).Card), TEXT("C «Подарок» карточки 61740")); });
		R->Add(1.0f, [Shop](FRun& Rn)
		{
			Rn.Check(Shop(Rn)->IsGiftPanelShown() && !Shop(Rn)->IsPanelBuy(), TEXT("C панель подарка"));
			Rn.Click(Shop(Rn)->DebugNode(TEXT("SHELL_EQUIP_LV")), TEXT("C комбо уровня"));
		});
		R->Add(1.0f, [Shop](FRun& Rn) { Rn.Click(Shop(Rn)->DebugNode(TEXT("SHELL_EQUIP_LV_Row2")), TEXT("C пункт уровня 60")); });
		R->Add(1.0f, [Shop](FRun& Rn)
		{
			CheckPanel(Rn, 61770, TEXT("C уровень 60"));
			const uint8 Route[UAsdaCashGiftSubsystem::kRouteBytes] = {};
			Rn.Sub<UAsdaCashGiftSubsystem>()->DebugSetConfirmed(TEXT("Получатель"), Route);
			Shop(Rn)->DebugSetGiftName(TEXT("Получатель"));
			Rn.Click(Shop(Rn)->DebugGiftGiveWidget(), TEXT("C «Подарок» панели"));
		});
		R->Add(1.0f, [Shop, Box](FRun& Rn)
		{
			UAsdaPurchaseBoxSubsystem* B = Box(Rn);
			Rn.Check(B->IsOpen() && B->DebugRequest().ItemCode == 61770 && B->DebugRequest().PanelMode == 1 && Shop(Rn)->GiftWireCode(61770) == 61770,
				FString::Printf(TEXT("C модалка подарка: код %d, код 5074 %d (ждём 61770)"), B->DebugRequest().ItemCode, Shop(Rn)->GiftWireCode(B->DebugRequest().ItemCode)));
			Rn.Shot(TEXT("asda_variant_gift"));
		});
		R->Add(1.5f, [Shop, Box](FRun& Rn)
		{
			if (Box(Rn)->IsOpen()) { Box(Rn)->OnCancelClicked(); }
			Rn.Sub<UAsdaCashGiftSubsystem>()->DebugClearFlags();
			Shop(Rn)->CloseGiftPanels();
		});
		// ── D: карточка без вариантов ──
		R->Add(0.5f, [](FRun& Rn) { Go(Rn, 492); });
		R->Add(0.5f, [Shop](FRun& Rn) { Rn.Click(Shop(Rn)->DebugCardBuyWidget(Rn.Where.FindRef(492).Card), TEXT("D «Купить» карточки 492")); });
		R->Add(1.0f, [Shop, Box](FRun& Rn)
		{
			Rn.Check(Box(Rn)->IsOpen() && !Shop(Rn)->IsGiftPanelShown(), TEXT("D без вариантов: сразу модалка 1559, панели нет"));
			if (Box(Rn)->IsOpen()) { Box(Rn)->OnCancelClicked(); }
		});
		// ── E: корзина из трёх строк, две с вариантами ──
		R->Add(0.5f, [](FRun& Rn) { Go(Rn, 61740); });
		R->Add(0.5f, [Shop](FRun& Rn)
		{
			Shop(Rn)->DebugClearPending();
			Shop(Rn)->DebugSetCapture(false);
			Shop(Rn)->DebugSetCapture(true);
			Shop(Rn)->DebugFillCart({ 61740, 62023, 492 });
			Rn.Click(Shop(Rn)->DebugBuySelWidget(), TEXT("E «Купить выбранное»"));
		});
		R->Add(1.0f, [Shop, Box](FRun& Rn)
		{
			Rn.Check(Shop(Rn)->IsGiftPanelShown() && Shop(Rn)->IsPanelBuy() && !Box(Rn)->IsOpen(), TEXT("E панель первой строки, модалки нет"));
			CheckPanel(Rn, 61740, TEXT("E первая строка"));
			Rn.Click(Shop(Rn)->DebugNode(TEXT("SHELL_EQUIP_LV")), TEXT("E комбо уровня"));
		});
		R->Add(1.0f, [Shop](FRun& Rn) { Rn.Click(Shop(Rn)->DebugNode(TEXT("SHELL_EQUIP_LV_Row1")), TEXT("E пункт уровня 40")); });
		R->Add(1.0f, [Shop](FRun& Rn)
		{
			CheckPanel(Rn, 61755, TEXT("E уровень 40"));
			Rn.Shot(TEXT("asda_variant_cart"));
		});
		R->Add(1.5f, [Shop](FRun& Rn) { Rn.Click(Shop(Rn)->DebugGiftGiveWidget(), TEXT("E «Купить» первой строки")); });
		R->Add(1.0f, [Shop, Box](FRun& Rn)
		{
			UAsdaPremiumShopSubsystem* P = Shop(Rn);
			Rn.Check(P->DebugCartRowCode(0) == 61755, FString::Printf(TEXT("E строка 0: код %d (ждём 61755)"), P->DebugCartRowCode(0)));
			Rn.Check(P->IsGiftPanelShown() && !Box(Rn)->IsOpen(), TEXT("E панель второй строки"));
			CheckPanel(Rn, 62023, TEXT("E вторая строка"));
			Rn.Click(P->DebugGiftGiveWidget(), TEXT("E «Купить» второй строки"));
		});
		R->Add(1.0f, [Shop, Box](FRun& Rn)
		{
			UAsdaPurchaseBoxSubsystem* B = Box(Rn);
			Rn.Check(!Shop(Rn)->IsGiftPanelShown() && B->IsOpen() && B->DebugRequest().ItemCode == 61755,
				FString::Printf(TEXT("E панели закрыты, модалка 1559 с кодом %d (ждём 61755)"), B->DebugRequest().ItemCode));
			Rn.Shot(TEXT("asda_variant_cart1559"));
		});
		R->Add(1.5f, [Box](FRun& Rn) { Rn.Click(Box(Rn)->DebugOkWidget(), TEXT("E ОК модалки")); });
		R->Add(1.0f, [Shop](FRun& Rn)
		{
			const TArray<TArray<uint8>>& S = Shop(Rn)->DebugSentBodies();
			const TArray<uint8> B = S.Num() > 0 ? S.Last() : TArray<uint8>();
			auto Code = [&B](int32 Off) { return B.Num() > Off + 3 ? (int32)(B[Off] | (B[Off + 1] << 8) | (B[Off + 2] << 16) | (B[Off + 3] << 24)) : 0; };
			Rn.Check(Code(3) == 61755 && Code(7) == 62023 && Code(11) == 492, FString::Printf(TEXT("E 5070: коды %d, %d, %d (ждём 61755, 62023, 492)"), Code(3), Code(7), Code(11)));
			Shop(Rn)->DebugClearPending();
			Shop(Rn)->DebugSetCapture(false);
		});
		// ── F: отмена панели прерывает очередь ──
		R->Add(0.5f, [Shop](FRun& Rn)
		{
			Shop(Rn)->DebugSetCapture(true);
			Shop(Rn)->DebugFillCart({ 61740 });
			Rn.Click(Shop(Rn)->DebugBuySelWidget(), TEXT("F «Купить выбранное»"));
		});
		R->Add(1.0f, [Shop](FRun& Rn) { Rn.Click(Shop(Rn)->DebugNode(TEXT("SHELL_EQUIP_CAN_BTN")), TEXT("F отмена панели")); });
		R->Add(1.0f, [Shop, Box](FRun& Rn)
		{
			UAsdaPremiumShopSubsystem* P = Shop(Rn);
			Rn.Check(!P->IsGiftPanelShown() && !Box(Rn)->IsOpen() && P->DebugCartRowCode(0) == 61740 && P->DebugSentBodies().Num() == 0,
				FString::Printf(TEXT("F панель закрыта, модалки нет, строка %d, пакетов %d"), P->DebugCartRowCode(0), P->DebugSentBodies().Num()));
			P->DebugFillCart({});
			P->DebugSetCapture(false);
		});
		// ── G: чужие предметы не входят в варианты набора 2257 ──
		R->Add(0.5f, [Shop](FRun& Rn)
		{
			Shop(Rn)->DebugFillCart({ 75816 });
			Rn.Click(Shop(Rn)->DebugBuySelWidget(), TEXT("G «Купить выбранное» с 75816"));
		});
		R->Add(1.0f, [Shop](FRun& Rn)
		{
			const FString Dump = Shop(Rn)->DumpGiftPanels();
			Rn.Check(Dump.Contains(TEXT("профессий 1, уровней 3")), TEXT("G набор 2257: одна профессия, три уровня"));
			Shop(Rn)->CloseGiftPanels();
			Shop(Rn)->DebugFillCart({});
		});
		// ── H: коробка зелья 79940 на вкладке «Питомец»: покупка, вскрытие ──
		R->Add(0.5f, [](FRun& Rn) { Go(Rn, 79940); });
		R->Add(1.0f, [Shop](FRun& Rn)
		{
			UAsdaPremiumShopSubsystem* P = Shop(Rn);
			const int32 Card = Rn.Where.FindRef(79940).Card;
			const FString Want = FAsdaLang::IsRu() ? TEXT("Зелье нового уровня питомца x2") : TEXT("Pet Limit breaking potion*2");
			Rn.Check(Rn.Where.FindRef(79940).Cat == 5 && P->DebugCardText(Card) == Want,
				FString::Printf(TEXT("H карточка вкладки %d: «%s» (ждём «%s»)"), Rn.Where.FindRef(79940).Cat, *P->DebugCardText(Card), *Want));
			Rn.Shot(TEXT("asda_potion_card"));
		});
		R->Add(1.5f, [Shop](FRun& Rn)
		{
			Shop(Rn)->DebugClearPending();
			Shop(Rn)->DebugSetCapture(false);
			Shop(Rn)->DebugSetCapture(true);
			Rn.Click(Shop(Rn)->DebugCardBuyWidget(Rn.Where.FindRef(79940).Card), TEXT("H «Купить» карточки 79940"));
		});
		R->Add(1.0f, [Shop, Box](FRun& Rn)
		{
			UAsdaPurchaseBoxSubsystem* B = Box(Rn);
			const FAsdaItemRecord* Rec = Rn.Sub<UAsdaItemTableSubsystem>()->GetItem(79940);
			Rn.Check(B->IsOpen() && !Shop(Rn)->IsGiftPanelShown() && B->DebugRequest().ItemCode == 79940 && Rec && Rec->Price == 490,
				FString::Printf(TEXT("H модалка 1559 с кодом %d, цена таблицы %d (ждём 79940, 490)"), B->DebugRequest().ItemCode, Rec ? Rec->Price : -1));
			Rn.Shot(TEXT("asda_potion_1559"));
		});
		R->Add(1.5f, [Box](FRun& Rn) { Rn.Click(Box(Rn)->DebugOkWidget(), TEXT("H ОК модалки")); });
		R->Add(1.0f, [Shop](FRun& Rn)
		{
			const TArray<TArray<uint8>>& S = Shop(Rn)->DebugSentBodies();
			const TArray<uint8> B = S.Num() > 0 ? S.Last() : TArray<uint8>();
			const int32 C0 = B.Num() > 6 ? AsdaBytes::RdI32(B.GetData() + 3) : 0;
			Rn.Check(S.Num() == 1 && C0 == 79940 && B[1] == 1, FString::Printf(TEXT("H 5070: пакетов %d, код %d, таблица %d"), S.Num(), C0, B.Num() > 1 ? (int32)B[1] : -1));
			Shop(Rn)->DebugClearPending();
			Shop(Rn)->DebugSetCapture(false);
		});
		R->Add(0.5f, [](FRun& Rn)
		{
			UAsdaInventorySubsystem* Inv = Rn.Sub<UAsdaInventorySubsystem>();
			UAsdaNetSubsystem* Net = Rn.Sub<UAsdaNetSubsystem>();
			const UAsdaItemTableSubsystem* IT = Rn.Sub<UAsdaItemTableSubsystem>();
			TArray<FAsdaPackageEntry> Ct;
			Rn.Check(IT->GetPackageContents(79940, Ct) && Ct.Num() == 2 && Ct[0].ItemCode == 23237 && Ct[1].ItemCode == 23237 && Ct[0].Amount == 1 && Ct[1].Amount == 1,
				FString::Printf(TEXT("H состав коробки: позиций %d"), Ct.Num()));
			const int32 BoxCell = Inv->FindFreeSlot(1);
			Rn.Check(BoxCell >= 0 && Inv->DebugPutItem(1, BoxCell, 79940, 1), FString::Printf(TEXT("H коробка в ячейке %d сумки"), BoxCell));
			TArray<uint8> S15; int32 Cnt = 0, ShS = 0, ShB = 0;
			const bool bRes = Inv->ReservePackageSlots(79940, S15, Cnt, ShS, ShB);
			const int32 C0 = S15.Num() >= 6 ? AsdaBytes::RdI16(S15.GetData() + 1) : -1, C1 = S15.Num() >= 6 ? AsdaBytes::RdI16(S15.GetData() + 4) : -1;
			Rn.Check(bRes && Cnt == 2 && ShS == 0 && ShB == 0 && S15[0] == 2 && S15[3] == 2 && C0 != C1,
				FString::Printf(TEXT("H ячейки вскрытия: позиций %d, контейнеры %d/%d, ячейки %d/%d"), Cnt, S15.Num() > 3 ? S15[0] : -1, S15.Num() > 3 ? S15[3] : -1, C0, C1));
			TSharedRef<TArray<uint8>> Body = MakeShared<TArray<uint8>>();
			FAsdaNetSelfScope Self(Net, kCmd);
			Net->SelfUserNum = 321; Net->bSelfUserNumKnown = true;
			const uint32 Gen = AsdaSelfTestKit::ArmCapture(Net, [Body](uint16, const uint8* D, int32 Len, bool, TCHAR) { Body->Reset(); if (D && Len > 0) { Body->Append(D, Len); } }, 6096, kCmd);
			const bool bSend = Inv->SendOpenPackage6096(79940, 1, BoxCell);
			AsdaSelfTestKit::DisarmCapture(Net, Gen);
			Rn.Check(bSend && Body->Num() >= 16 && (*Body)[6] == 2 && (*Body)[10] == 2 && (*Body)[13] == 2,
				FString::Printf(TEXT("H 6096: длина %d, позиций %d, контейнеры %d/%d"), Body->Num(), Body->Num() > 6 ? (int32)(*Body)[6] : -1, Body->Num() > 10 ? (int32)(*Body)[10] : -1, Body->Num() > 13 ? (int32)(*Body)[13] : -1));
			Inv->ClearCell(1, BoxCell);
		});
		R->Add(2.0f, [Shop](FRun& Rn)
		{
			for (const FString& Name : Rn.Shots)
			{
				if (Rn.OutDir.IsEmpty()) { continue; }
				const FString Src = FPaths::ScreenShotDir() / (Name + TEXT(".png"));
				IFileManager::Get().MakeDirectory(*Rn.OutDir, true);
				Rn.Check(IFileManager::Get().Copy(*(Rn.OutDir / (Name + TEXT(".png"))), *Src) == COPY_OK, FString::Printf(TEXT("кадр %s"), *Name));
			}
			Shop(Rn)->CloseWindow();
		});
		R->Start();
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
