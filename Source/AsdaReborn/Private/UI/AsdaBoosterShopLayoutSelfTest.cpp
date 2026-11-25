// Copyright (c) 2026 @sitxovski (Discord)
// AsdaBoosterShopLayoutSelfTest.cpp — самотест [ПОРТ] 05.11.2026 «витрина бустер-шопа из данных редактора»
//   (задача владельца: интерактивный редактор витрины `Tools/BoosterShopEditor`; генерирует Bpremium/Fpremium.BIN,
//   ведомости категорий и вкладок → `DT_BoosterShopList` + `DT_BoosterShopTabs`).
//
// СЦЕНАРИЙ `asda.PremiumMall.BoosterLayoutSelfTest [file=<путь>] [quit]` (офлайн, путь игрока — те же входы, что мышь):
//   ожидание — файл редактора `Tools/BoosterShopEditor/out/expect_layout.json` (`bse_cli.py expect`): видимые вкладки
//   по порядку (фильтр, подписи RU/EN, коды карточек в порядке витрины) и для каждого кода имя RU/EN и цена.
//   Окно 15300 открывается `OpenWindow`; число видимых кнопок полосы = числу вкладок; у каждой кнопки подпись на языке
//   сессии, у скрытых кнопок полосы — не видна; клик по кнопке (`OnTabSlotClicked`, вход мыши) → по каждой странице
//   (`SetPage`, вход кнопок 15501/15503) видимые карточки совпадают с ожиданием по коду, имени и цене, лишних нет.
//   Через кадр — живая геометрия каждой видимой кнопки ненулевая (закон «UI путём игрока»). Окно закрывается.
//   Итог `[витрина бустеров] ИТОГ: ok=N fail=M`.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"   // ASDA_DEV_CONSOLE — дев-команда вне Shipping
#include "UI/AsdaBoosterShopSubsystem.h"
#include "UI/AsdaBoosterPanelSubsystem.h"   // окно «Вид» 15600 — сверка состава созданного бустера
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaBoosterLayoutTest, Log, All);

#if ASDA_DEV_CONSOLE   // дев-команда, в Shipping не регистрируется
namespace AsdaBoosterLayoutTest
{
	static const TCHAR* const kCmd = TEXT("asda.PremiumMall.BoosterLayoutSelfTest");

	// Одна ожидаемая вкладка: фильтр, подписи, коды карточек в порядке витрины.
	struct FTabWant
	{
		int32 Filter = 0;          // эталонный смысл вкладки 0…6
		FString CaptionRu;         // подпись RU (loc GameText)
		FString CaptionEn;         // подпись EN (DT_Loc_GameText_en)
		TArray<int32> Codes;       // коды карточек по порядку (все страницы подряд)
	};

	// Ожидание по одному коду: имена и цена так, как их видит игрок.
	struct FItemWant
	{
		FString NameRu;            // item_list.ItemName
		FString NameEn;            // DT_Loc_item_list_en (пусто — EN-строки нет, игра покажет RU)
		int32 Price = 0;           // item_list.Price
	};

	// Счётчики и журнал.
	struct FRun
	{
		int32 Ok = 0;              // пройдено проверок
		int32 Fail = 0;            // провалено проверок
		void Check(bool b, const FString& What)
		{
			(b ? Ok : Fail)++;
			UE_LOG(LogAsdaBoosterLayoutTest, Log, TEXT("[витрина бустеров] %s — %s"), b ? TEXT("ok  ") : TEXT("FAIL"), *What);
		}
	};

	// Путь к файлу ожидания: `file=` из аргументов, иначе `<проект>/../Tools/BoosterShopEditor/out/expect_layout.json`.
	static FString ExpectPath(const TArray<FString>& Args)
	{
		for (const FString& A : Args)
		{
			if (A.StartsWith(TEXT("file="))) { return A.Mid(5); }
		}
		return FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("Tools"), TEXT("BoosterShopEditor"), TEXT("out"), TEXT("expect_layout.json")));
	}

	// Разбор файла ожидания. false — файла нет или он битый (текст причины в OutWhy).
	static bool LoadExpect(const FString& Path, TArray<FTabWant>& OutTabs, TMap<int32, FItemWant>& OutItems, FString& OutWhy)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path)) { OutWhy = TEXT("нет файла ") + Path; return false; }
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Rd = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Rd, Root) || !Root.IsValid()) { OutWhy = TEXT("JSON не разобран: ") + Path; return false; }
		const TArray<TSharedPtr<FJsonValue>>* Tabs = nullptr;
		const TSharedPtr<FJsonObject>* Items = nullptr;
		if (!Root->TryGetArrayField(TEXT("tabs"), Tabs) || !Root->TryGetObjectField(TEXT("items"), Items))
		{
			OutWhy = TEXT("в файле нет полей tabs/items");
			return false;
		}
		for (const TSharedPtr<FJsonValue>& V : *Tabs)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			if (!O.IsValid()) { continue; }
			FTabWant T;
			T.Filter = (int32)O->GetNumberField(TEXT("filter"));
			T.CaptionRu = O->GetStringField(TEXT("caption_ru"));
			T.CaptionEn = O->GetStringField(TEXT("caption_en"));
			for (const TSharedPtr<FJsonValue>& C : O->GetArrayField(TEXT("codes"))) { T.Codes.Add((int32)C->AsNumber()); }
			OutTabs.Add(MoveTemp(T));
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& It : (*Items)->Values)
		{
			const TSharedPtr<FJsonObject> O = It.Value.IsValid() ? It.Value->AsObject() : nullptr;
			if (!O.IsValid()) { continue; }
			FItemWant W;
			W.NameRu = O->GetStringField(TEXT("name_ru"));
			W.NameEn = O->GetStringField(TEXT("name_en"));
			W.Price = (int32)O->GetNumberField(TEXT("price"));
			OutItems.Add(FCString::Atoi(*It.Key), MoveTemp(W));
		}
		return OutTabs.Num() > 0;
	}

	// Ожидание окна «Вид» 15600 для одного бустера: 35 позиций строки состава (booster_list = GachaItemRateTable).
	struct FViewWant
	{
		int32 Code = 0;            // код бустера
		TArray<int32> Slots;       // 35 кодов позиций (0 — пусто): 0…4 «особые», 5…34 «обычные»
	};

	// Разбор `views` файла ожидания (поле необязательное: пусто — раздел «Вид» не проверяется).
	static void LoadViews(const FString& Path, TArray<FViewWant>& Out)
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *Path)) { return; }
		const TSharedRef<TJsonReader<>> Rd = TJsonReaderFactory<>::Create(Text);
		const TArray<TSharedPtr<FJsonValue>>* Views = nullptr;
		if (!FJsonSerializer::Deserialize(Rd, Root) || !Root.IsValid() || !Root->TryGetArrayField(TEXT("views"), Views)) { return; }
		for (const TSharedPtr<FJsonValue>& V : *Views)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			if (!O.IsValid()) { continue; }
			FViewWant W;
			W.Code = (int32)O->GetNumberField(TEXT("code"));
			for (const TSharedPtr<FJsonValue>& C : O->GetArrayField(TEXT("slots"))) { W.Slots.Add((int32)C->AsNumber()); }
			Out.Add(MoveTemp(W));
		}
	}

	// Только цифры строки (цена на карточке идёт с разделителями разрядов).
	static int32 DigitsOf(const FString& S)
	{
		FString D;
		for (const TCHAR Ch : S) { if (FChar::IsDigit(Ch)) { D.AppendChar(Ch); } }
		return D.IsEmpty() ? -1 : FCString::Atoi(*D);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaBoosterLayoutSelfTest(
	TEXT("asda.PremiumMall.BoosterLayoutSelfTest"),
	TEXT("asda.PremiumMall.BoosterLayoutSelfTest [file=<путь>] [quit] — [ПОРТ] 05.11.2026: витрина бустер-шопа 15300 из данных "
	     "редактора Tools/BoosterShopEditor (вкладки, порядок, имена, цены) путём игрока. Только офлайн."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		using namespace AsdaBoosterLayoutTest;
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (!W || AsdaSelfTestKit::RefuseStart(W, kCmd, Args)) { return; }
		UGameInstance* GI = W->GetGameInstance();
		UAsdaBoosterShopSubsystem* Bs = GI ? GI->GetSubsystem<UAsdaBoosterShopSubsystem>() : nullptr;
		if (!Bs || Bs->IsOpen())
		{
			AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет подсистемы или окно 15300 уже открыто игроком"));
			return;
		}
		TArray<FTabWant> Tabs;
		TMap<int32, FItemWant> Items;
		FString Why;
		const FString Path = ExpectPath(A);
		if (!LoadExpect(Path, Tabs, Items, Why))
		{
			AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, *Why);
			return;
		}
		const bool bRu = FAsdaLang::IsRu();
		TSharedRef<FRun> R = MakeShared<FRun>();
		UE_LOG(LogAsdaBoosterLayoutTest, Log, TEXT("[витрина бустеров] ожидание %s: вкладок %d, кодов %d, язык %s"),
			*Path, Tabs.Num(), Items.Num(), bRu ? TEXT("RU") : TEXT("EN"));

		Bs->OpenWindow();
		R->Check(Bs->IsOpen(), TEXT("окно 15300 открылось"));
		R->Check(Bs->DebugTabCount() == Tabs.Num(),
			FString::Printf(TEXT("видимых вкладок %d (ждём %d)"), Bs->DebugTabCount(), Tabs.Num()));
		// ── Полоса: подписи видимых кнопок и скрытость остальных ──
		for (int32 s = 0; s < UAsdaBoosterShopSubsystem::kCats; ++s)
		{
			FString Cap; bool bShown = false; FVector2D Sz;
			Bs->DebugTabState(s, Cap, bShown, Sz);
			if (Tabs.IsValidIndex(s))
			{
				const FTabWant& T = Tabs[s];
				const FString Want = bRu || T.CaptionEn.IsEmpty() ? T.CaptionRu : T.CaptionEn;
				R->Check(bShown && Bs->DebugTabFilter(s) == T.Filter && Cap == Want,
					FString::Printf(TEXT("кнопка %d: видна %d, фильтр %d (ждём %d), подпись «%s» (ждём «%s»)"),
						s, (int32)bShown, Bs->DebugTabFilter(s), T.Filter, *Cap, *Want));
			}
			else
			{
				R->Check(!bShown, FString::Printf(TEXT("кнопка %d скрыта (вкладка снята с витрины): видна %d"), s, (int32)bShown));
			}
		}
		// ── Каждая вкладка, каждая страница: карточки как у игрока ──
		const int32 PerPage = Bs->DebugCellsPerPage();
		for (int32 s = 0; s < Tabs.Num(); ++s)
		{
			const FTabWant& T = Tabs[s];
			Bs->OnTabSlotClicked(s);
			R->Check(Bs->DebugCategory() == T.Filter, FString::Printf(TEXT("клик по кнопке %d → фильтр %d (ждём %d)"),
				s, Bs->DebugCategory(), T.Filter));
			const int32 WantPages = FMath::Max(1, (T.Codes.Num() + PerPage - 1) / PerPage);
			R->Check(Bs->DebugMaxPage() == WantPages, FString::Printf(TEXT("вкладка %d: страниц %d (ждём %d при %d на странице)"),
				s, Bs->DebugMaxPage(), WantPages, PerPage));
			int32 Bad = 0, Seen = 0;
			FString FirstBad;
			for (int32 p = 1; p <= WantPages; ++p)
			{
				Bs->SetPage(p);
				for (int32 c = 0; c < UAsdaBoosterShopSubsystem::kCells; ++c)
				{
					const int32 Idx = (p - 1) * PerPage + c;
					int32 Code = 0; FString Name, Price; bool bShown = false;
					Bs->DebugCell(c, Code, Name, Price, bShown);
					const bool bWantCard = c < PerPage && T.Codes.IsValidIndex(Idx);
					if (!bWantCard)
					{
						if (bShown) { ++Bad; if (FirstBad.IsEmpty()) { FirstBad = FString::Printf(TEXT("стр.%d ячейка %d лишняя (%d)"), p, c, Code); } }
						continue;
					}
					++Seen;
					const int32 WantCode = T.Codes[Idx];
					const FItemWant* IW = Items.Find(WantCode);
					const FString WantName = IW ? ((bRu || IW->NameEn.IsEmpty()) ? IW->NameRu : IW->NameEn) : FString();
					const bool bGood = bShown && Code == WantCode && IW && Name == WantName && DigitsOf(Price) == IW->Price;
					if (!bGood)
					{
						++Bad;
						if (FirstBad.IsEmpty())
						{
							FirstBad = FString::Printf(TEXT("стр.%d ячейка %d: код %d «%s» цена «%s» видна %d; ждём %d «%s» %d"),
								p, c, Code, *Name, *Price, (int32)bShown, WantCode, *WantName, IW ? IW->Price : -1);
						}
					}
				}
			}
			R->Check(Bad == 0 && Seen == T.Codes.Num(), FString::Printf(TEXT("вкладка %d «%s»: карточек %d (ждём %d), расхождений %d%s%s"),
				s, *(bRu ? T.CaptionRu : T.CaptionEn), Seen, T.Codes.Num(), Bad, FirstBad.IsEmpty() ? TEXT("") : TEXT(" — "), *FirstBad));
		}
		// ── «Вид» 15600 созданных бустеров: щелчок по иконке карточки (вход мыши `OnCellIconClicked`) → 35 позиций ──
		TArray<FViewWant> Views;
		LoadViews(Path, Views);
		UAsdaBoosterPanelSubsystem* Bp = GI->GetSubsystem<UAsdaBoosterPanelSubsystem>();
		for (const FViewWant& V : Views)
		{
			// Где карточка: первая вкладка, в которой она есть, и её страница.
			int32 Slot = -1, Idx = -1;
			for (int32 s = 0; s < Tabs.Num() && Slot < 0; ++s)
			{
				Idx = Tabs[s].Codes.IndexOfByKey(V.Code);
				if (Idx >= 0) { Slot = s; }
			}
			if (Slot < 0 || !Bp || V.Slots.Num() != 35)
			{
				R->Check(false, FString::Printf(TEXT("«Вид» %d: карточки нет ни на одной вкладке ожидания или нет окна"), V.Code));
				continue;
			}
			Bs->OnTabSlotClicked(Slot);
			Bs->SetPage(Idx / PerPage + 1);
			int32 Cell = -1;
			for (int32 c = 0; c < UAsdaBoosterShopSubsystem::kCells && Cell < 0; ++c)
			{
				int32 Code = 0; FString N, P; bool bSh = false;
				Bs->DebugCell(c, Code, N, P, bSh);
				if (bSh && Code == V.Code) { Cell = c; }
			}
			if (Cell < 0)
			{
				R->Check(false, FString::Printf(TEXT("«Вид» %d: карточка не найдена на вкладке %d стр. %d"), V.Code, Slot, Idx / PerPage + 1));
				continue;
			}
			Bs->OnCellIconClicked(Cell);
			R->Check(Bp->IsPrevVisible() && Bp->GetPrevItemCode() == V.Code,
				FString::Printf(TEXT("«Вид» открыт щелчком по иконке карточки %d (окно %d, код %d)"), V.Code, (int32)Bp->IsPrevVisible(), Bp->GetPrevItemCode()));
			// Ожидание по эталонной раскладке окна: особые — позиции 0…4 как есть, обычные — непустые 5…34 подряд по 15.
			TArray<int32> Normal;
			for (int32 i = 5; i < 35; ++i) { if (V.Slots[i] > 0) { Normal.Add(V.Slots[i]); } }
			int32 Bad = 0;
			FString First;
			for (int32 i = 0; i < 5; ++i)
			{
				const int32 Want = V.Slots[i] > 0 ? V.Slots[i] : -1;
				if (Bp->DebugPrevSpecialCode(i) != Want) { ++Bad; if (First.IsEmpty()) { First = FString::Printf(TEXT("особая %d: %d ≠ %d"), i, Bp->DebugPrevSpecialCode(i), Want); } }
			}
			for (int32 Page = 0; Page <= (Normal.Num() > 15 ? 1 : 0); ++Page)
			{
				Bp->DebugSetPrevPage(Page);
				for (int32 c = 0; c < 15; ++c)
				{
					const int32 k = Page * 15 + c;
					const int32 Want = Normal.IsValidIndex(k) ? Normal[k] : -1;
					if (Bp->DebugPrevNormalCode(c) != Want)
					{
						++Bad;
						if (First.IsEmpty()) { First = FString::Printf(TEXT("обычная стр.%d ячейка %d: %d ≠ %d"), Page, c, Bp->DebugPrevNormalCode(c), Want); }
					}
				}
			}
			R->Check(Bad == 0, FString::Printf(TEXT("«Вид» %d: 5 особых + %d обычных = строке состава, расхождений %d%s%s"),
				V.Code, Normal.Num(), Bad, First.IsEmpty() ? TEXT("") : TEXT(" — "), *First));
			Bp->ClosePrevWindow();
		}

		// Первая вкладка — на место, затем кадр на раскладку и проверка живой геометрии кнопок.
		Bs->OnTabSlotClicked(0);
		Bs->SetPage(1);
		TWeakObjectPtr<UWorld> WW(W);
		TWeakObjectPtr<UAsdaBoosterShopSubsystem> WBs(Bs);
		const int32 NTabs = Tabs.Num();
		const bool bQuit = Args.bQuit;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, WBs, R, NTabs, bQuit](float) -> bool
		{
			UWorld* W2 = WW.Get();
			UAsdaBoosterShopSubsystem* B2 = WBs.Get();
			if (!W2) { return false; }
			if (B2)
			{
				for (int32 s = 0; s < NTabs; ++s)
				{
					FString Cap; bool bShown = false; FVector2D Sz;
					B2->DebugTabState(s, Cap, bShown, Sz);
					R->Check(bShown && Sz.X > 1.f && Sz.Y > 1.f,
						FString::Printf(TEXT("кнопка %d «%s»: живая геометрия %.0f×%.0f"), s, *Cap, Sz.X, Sz.Y));
				}
				B2->CloseWindow();
			}
			AsdaSelfTestKit::Finish(W2, kCmd, bQuit, LogAsdaBoosterLayoutTest, TEXT("[витрина бустеров]"), R->Ok, R->Fail);
			return false;
		}), 0.5f);
	}));
#endif // ASDA_DEV_CONSOLE
