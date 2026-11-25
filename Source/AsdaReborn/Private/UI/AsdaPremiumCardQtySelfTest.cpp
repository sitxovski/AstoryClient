// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPremiumCardQtySelfTest.cpp — САМОТЕСТ ПОДПИСЕЙ ВИТРИНЫ 6000 (06.11.2026, решение владельца; Reports/premium_circle_bike/REPORT_LABELS.md).
//
//   `asda.SelfTest.PremiumCardQty [out=<папка>] [quit]` — офлайн (`-AsdaNoNet`), путём игрока: окно 6000, все вкладки, подразделы и страницы.
//     • каждая карточка из ведомости RawAssets/Data/NPC/PremiumShopBin_card_qty.json показывает имя с количеством (RU « x3», EN «*3»);
//     • карточка вне ведомости количества не показывает;
//     • имена коробок 76444–76447, 76452, 76453 = содержимому;
//     • на странице с карточкой 387 (самая длинная подпись): корзина 387/492/466, модалка 1559 и нотис покупки 5071 в журнале чата;
//     • подарок = покупка себе: код 5074 каждой карточки = ведомости, окно подарка и модалка 1559 карточки 387 показывают количество;
//     • кадры `asda_cardqty.png` (карточка и корзина), `asda_cardqty_1559.png`, `asda_cardqty_notice.png`, `asda_cardqty_gift.png`, `asda_cardqty_gift1559.png`.
//   Итог `КАРТОЧКИ-ШТУКИ ИТОГ: ok=N fail=M`.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
#include "Test/AsdaSelfTestTree.h"   // папка проекта в дереве (ASDA_ROOT) — ведомости-оракулы и для собранной игры
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Text/AsdaLang.h"
#include "UI/AsdaCashGiftSubsystem.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaPremiumShopSubsystem.h"
#include "UI/AsdaPurchaseBoxSubsystem.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCardQtyTest, Log, All);

namespace AsdaCardQtyTest
{
	static const TCHAR* kCmd    = TEXT("asda.SelfTest.PremiumCardQty");
	static const TCHAR* kPrefix = TEXT("КАРТОЧКИ-ШТУКИ");
	static const int32 kShotCode = 387;   // «Пустой камень улучшения (9+2)» — самая длинная подпись (32 знака на RU), кадры её страницы

	// Ожидаемая подпись карточки: имя спецпредмета + количество в принятом для языка виде.
	static FString Want(const FString& Name, int32 Qty)
	{
		return FAsdaLang::IsRu() ? FString::Printf(TEXT("%s x%d"), *Name, Qty) : FString::Printf(TEXT("%s*%d"), *Name, Qty);
	}

	// Ведомость штук: код → количество.
	static TMap<int32, int32> LoadQty()
	{
		TMap<int32, int32> Out;
		FString Text;
		const FString Path = FPaths::Combine(AsdaSelfTestTree::ProjectDir(), TEXT("RawAssets/Data/NPC/PremiumShopBin_card_qty.json"));
		if (!FFileHelper::LoadFileToString(Text, *Path)) { return Out; }
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Rd = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Rd, Root) || !Root.IsValid()) { return Out; }
		const TSharedPtr<FJsonObject>* Q = nullptr;
		if (!Root->TryGetObjectField(TEXT("qty"), Q) || !Q) { return Out; }
		for (const auto& P : (*Q)->Values) { Out.Add(FCString::Atoi(*P.Key), (int32)P.Value->AsNumber()); }
		return Out;
	}

	// Ведомость кодов подарка: код карточки → код выдачи.
	static TMap<int32, int32> LoadGift()
	{
		TMap<int32, int32> Out;
		FString Text;
		const FString Path = FPaths::Combine(AsdaSelfTestTree::ProjectDir(), TEXT("RawAssets/Data/NPC/PremiumShopBin_card_qty.json"));
		if (!FFileHelper::LoadFileToString(Text, *Path)) { return Out; }
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Rd = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Rd, Root) || !Root.IsValid()) { return Out; }
		const TSharedPtr<FJsonObject>* Q = nullptr;
		if (!Root->TryGetObjectField(TEXT("gift"), Q) || !Q) { return Out; }
		for (const auto& P : (*Q)->Values) { Out.Add(FCString::Atoi(*P.Key), (int32)P.Value->AsNumber()); }
		return Out;
	}

	// Где карточка: вкладка, подраздел, страница.
	struct FWhere { int32 Cat = -1; int32 Sub = -1; int32 Page = -1; };

	// Переход на страницу витрины тем же порядком вызовов, что у игрока.
	static void Go(UAsdaPremiumShopSubsystem* P, const FWhere& Wh)
	{
		P->SetCategory(Wh.Cat);
		if (P->GetCategory() == 3) { P->SetEquipSub(Wh.Sub); } else { P->SetToolSub(Wh.Sub); }
		P->SetPage(Wh.Page);
	}

	// Состояние прогона: шаги с задержкой на тикере (кадру нужен живой тик).
	struct FRun : public TSharedFromThis<FRun>
	{
		TWeakObjectPtr<UGameInstance> GI;
		bool bQuit = false;
		FString OutDir;
		int32 Ok = 0, Fail = 0;
		FWhere ShotPage;
		int32 ShotCard = -1;              // номер карточки kShotCode на её странице
		TArray<FString> Shots;            // кадры прогона (копируются в out=)
		TArray<TPair<float, TFunction<void(FRun&)>>> Steps;
		int32 Next = 0;
		double DueAt = 0.0;

		UWorld* W() const { return GI.IsValid() ? GI->GetWorld() : nullptr; }
		template <class T> T* Sub() const { return GI.IsValid() ? GI->GetSubsystem<T>() : nullptr; }
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaCardQtyTest, Log,   TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaCardQtyTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
		void Add(float Delay, TFunction<void(FRun&)> Fn) { Steps.Emplace(Delay, MoveTemp(Fn)); }
		void Shot(const TCHAR* Name)
		{
			Shots.Add(Name);
			FScreenshotRequest::RequestScreenshot(Name, /*bShowUI*/ true, /*bAddUniqueSuffix*/ false);
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
				AsdaSelfTestKit::Finish(W(), kCmd, bQuit, LogAsdaCardQtyTest, kPrefix, Ok, Fail);
				return false;
			}
			DueAt = FPlatformTime::Seconds() + Steps[Next].Key;
			return true;
		}
	};

	// Обход витрины и проверка подписей; запоминает страницу для кадра.
	static void CheckCards(FRun& R)
	{
		UAsdaPremiumShopSubsystem* P = R.Sub<UAsdaPremiumShopSubsystem>();
		const UAsdaItemTableSubsystem* IT = R.Sub<UAsdaItemTableSubsystem>();
		const TMap<int32, int32> Qty = LoadQty();
		R.Check(Qty.Num() == 26, FString::Printf(TEXT("ведомость штук: %d карточек (ждём 26)"), Qty.Num()));
		if (!P || !IT) { R.Check(false, TEXT("нет подсистем магазина и таблиц")); return; }

		// Имена коробок = содержимому.
		const bool bRu = FAsdaLang::IsRu();
		struct FBox { int32 Code; const TCHAR* Ru; const TCHAR* En; };
		static const FBox kBoxes[] = {
			{ 76444, TEXT("Эликсир здоровья (100) x2"), TEXT("HP Elixir(100)*2") },
			{ 76445, TEXT("Эликсир здоровья (100) x3"), TEXT("HP Elixir(100)*3") },
			{ 76446, TEXT("Эликсир здоровья (100) x4"), TEXT("HP Elixir(100)*4") },
			{ 76447, TEXT("Эликсир здоровья (100) x5"), TEXT("HP Elixir(100)*5") },
			{ 76452, TEXT("Пустой камень улучшения (9+2)"), TEXT("Empty Upgrade Stone(9+2)") },
			{ 76453, TEXT("Пустой камень улучшения (9+2) x2"), TEXT("Empty Upgrade Stone(9+2)*2") },
		};
		for (const FBox& B : kBoxes)
		{
			const FString Name = IT->GetItemName(B.Code);
			const FString Exp = bRu ? B.Ru : B.En;
			R.Check(Name == Exp, FString::Printf(TEXT("коробка %d: имя «%s» (ждём «%s»)"), B.Code, *Name, *Exp));
		}

		// Все вкладки, подразделы и страницы: то же, что видит игрок на карточке.
		TSet<int32> Seen;
		int32 Plain = 0, PlainBad = 0;
		P->OpenWindow();   // таблица витрины читается при первом открытии
		// Подарок = покупка себе: код позиции 5074 у всех карточек из ведомости.
		const TMap<int32, int32> Gift = LoadGift();
		int32 GiftBad = 0;
		for (const auto& G : Gift) { GiftBad += P->GiftWireCode(G.Key) == G.Value ? 0 : 1; }
		R.Check(Gift.Num() == 177 && GiftBad == 0, FString::Printf(TEXT("код подарка: карточек %d (ждём 177), расхождений %d"), Gift.Num(), GiftBad));
		R.Check(P->GiftWireCode(533) == 79939 && P->GiftWireCode(387) == 76453, FString::Printf(TEXT("код подарка: 533 → %d, 387 → %d"), P->GiftWireCode(533), P->GiftWireCode(387)));
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
						if (Code <= 0) { continue; }
						const FString Shown = P->DebugCardText(Card);
						if (const int32* Q = Qty.Find(Code))
						{
							if (Seen.Contains(Code)) { continue; }
							Seen.Add(Code);
							const FString Exp = Want(IT->GetItemName(Code), *Q);
							R.Check(Shown == Exp, FString::Printf(TEXT("карточка %d: «%s» (ждём «%s»)"), Code, *Shown, *Exp));
							if (Code == kShotCode) { R.ShotPage.Cat = Cat; R.ShotPage.Sub = SubI; R.ShotPage.Page = Pg; R.ShotCard = Card; }
						}
						else
						{
							++Plain;
							if (Shown != IT->GetItemName(Code)) { ++PlainBad; UE_LOG(LogAsdaCardQtyTest, Warning, TEXT("%s карточка %d: «%s» ≠ имени предмета"), kPrefix, Code, *Shown); }
						}
					}
				}
			}
		}
		R.Check(Seen.Num() == Qty.Num(), FString::Printf(TEXT("найдено карточек со штуками %d из %d"), Seen.Num(), Qty.Num()));
		R.Check(Plain > 0 && PlainBad == 0, FString::Printf(TEXT("карточек без штук %d, из них с чужой подписью %d"), Plain, PlainBad));
		if (R.ShotPage.Cat < 0) { R.Check(false, TEXT("карточка 387 не найдена")); return; }
		Go(P, R.ShotPage);
		// Корзина: подпись строки та же, что на карточке.
		const TArray<int32> Cart = { 387, 492, 466 };
		P->DebugFillCart(Cart);
		for (int32 i = 0; i < Cart.Num(); ++i)
		{
			const FString Exp = Want(IT->GetItemName(Cart[i]), Qty.FindRef(Cart[i]));
			const FString Shown = P->DebugCartRowText(i);
			R.Check(Shown == Exp, FString::Printf(TEXT("корзина, строка %d: «%s» (ждём «%s»)"), i, *Shown, *Exp));
		}
	}

	// Модалка 1559: кнопка покупки карточки 387, имя товара как на карточке.
	static void CheckModal(FRun& R)
	{
		UAsdaPremiumShopSubsystem* P = R.Sub<UAsdaPremiumShopSubsystem>();
		UAsdaPurchaseBoxSubsystem* Box = R.Sub<UAsdaPurchaseBoxSubsystem>();
		const UAsdaItemTableSubsystem* IT = R.Sub<UAsdaItemTableSubsystem>();
		if (!P || !Box || !IT) { R.Check(false, TEXT("нет подсистем покупки")); return; }
		P->DebugFillCart({});
		P->DebugSetGxp(999999);
		P->DebugSetCapture(true);
		P->OnCardBuyClicked(R.ShotCard);
		const FString Exp = Want(IT->GetItemName(kShotCode), 2);
		R.Check(Box->IsOpen() && Box->DebugShownName() == Exp,
			FString::Printf(TEXT("модалка 1559 %s: «%s» (ждём «%s»)"), Box->IsOpen() ? TEXT("открыта") : TEXT("НЕ открыта"), *Box->DebugShownName(), *Exp));
	}

	// Окно подарка карточки 387: заголовок с количеством.
	static void CheckGiftPanel(FRun& R)
	{
		UAsdaPremiumShopSubsystem* P = R.Sub<UAsdaPremiumShopSubsystem>();
		const UAsdaItemTableSubsystem* IT = R.Sub<UAsdaItemTableSubsystem>();
		if (!P || !IT) { return; }
		P->OnCardGiftClicked(R.ShotCard);
		const FString Exp = Want(IT->GetItemName(kShotCode), 2);
		R.Check(P->IsGiftPanelShown() && P->DebugGiftTitle(true) == Exp, FString::Printf(TEXT("окно подарка: «%s» (ждём «%s»)"), *P->DebugGiftTitle(true), *Exp));
	}

	// Модалка 1559 подарка: имя получателя подтверждено, подпись с количеством; отмена, 5074 не уходит.
	static void CheckGiftModal(FRun& R)
	{
		UAsdaPremiumShopSubsystem* P = R.Sub<UAsdaPremiumShopSubsystem>();
		UAsdaPurchaseBoxSubsystem* Box = R.Sub<UAsdaPurchaseBoxSubsystem>();
		UAsdaCashGiftSubsystem* G = R.Sub<UAsdaCashGiftSubsystem>();
		const UAsdaItemTableSubsystem* IT = R.Sub<UAsdaItemTableSubsystem>();
		if (!P || !Box || !G || !IT) { R.Check(false, TEXT("нет подсистем подарка")); return; }
		const uint8 Route[UAsdaCashGiftSubsystem::kRouteBytes] = {};
		G->DebugSetConfirmed(TEXT("Получатель"), Route);
		P->DebugSetGiftName(TEXT("Получатель"));
		P->OnGiftGiveClicked(true);
		const FString Exp = Want(IT->GetItemName(kShotCode), 2);
		R.Check(Box->IsOpen() && Box->DebugShownName() == Exp, FString::Printf(TEXT("модалка подарка: «%s» (ждём «%s»)"), *Box->DebugShownName(), *Exp));
	}

	// Закрыть окно подарка и модалку.
	static void CloseGift(FRun& R)
	{
		if (UAsdaPurchaseBoxSubsystem* Box = R.Sub<UAsdaPurchaseBoxSubsystem>()) { if (Box->IsOpen()) { Box->OnCancelClicked(); } }
		if (UAsdaCashGiftSubsystem* G = R.Sub<UAsdaCashGiftSubsystem>()) { G->DebugClearFlags(); }
		if (UAsdaPremiumShopSubsystem* P = R.Sub<UAsdaPremiumShopSubsystem>()) { P->CloseGiftPanels(); }
	}

	// ОК в модалке → 5070 перехвачен → настоящий обработчик ответа 5071 → нотис покупки в журнале чата.
	static void CheckNotice(FRun& R)
	{
		UAsdaPremiumShopSubsystem* P = R.Sub<UAsdaPremiumShopSubsystem>();
		UAsdaPurchaseBoxSubsystem* Box = R.Sub<UAsdaPurchaseBoxSubsystem>();
		const UAsdaChatSubsystem* Chat = R.Sub<UAsdaChatSubsystem>();
		const UAsdaItemTableSubsystem* IT = R.Sub<UAsdaItemTableSubsystem>();
		if (!P || !Box || !Chat || !IT) { R.Check(false, TEXT("нет подсистем нотиса")); return; }
		if (Box->IsOpen()) { Box->OnConfirmClicked(); }
		R.Check(P->DebugSentBodies().Num() == 1, FString::Printf(TEXT("5070 перехвачено пакетов %d (ждём 1)"), P->DebugSentBodies().Num()));
		TArray<uint8> B;   // ответ 5071 код 1 (та же форма, что в `asda.PremiumMall.BoxAddSelfTest`)
		B.SetNumZeroed(407);
		B[0] = 1;
		B[1] = 1;
		B[2] = 0xA0; B[3] = 0xBB; B[4] = 0x0D;   // баланс 900000
		B[12] = 1;
		P->DebugFeedBuyResult(B);
		const FString Exp = Want(IT->GetItemName(kShotCode), 2);
		R.Check(Chat->DebugLastSystemLineIndex(Exp) >= 0, FString::Printf(TEXT("нотис покупки в журнале чата содержит «%s»"), *Exp));
		P->DebugSetCapture(false);
		P->DebugClearPending();
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestPremiumCardQty(
	TEXT("asda.SelfTest.PremiumCardQty"),
	TEXT("Самотест подписей витрины 6000: количество на 26 карточках, имена коробок 76444–76447/76452/76453, кадр страницы. `out=<папка>`, `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaCardQtyTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA, 60.f)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет GameInstance")); return; }
		TSharedRef<FRun> R = MakeShared<FRun>();
		R->GI = GI;
		R->bQuit = KA.bQuit;
		for (const FString& A : KA.Pos) { if (A.StartsWith(TEXT("out="))) { R->OutDir = A.Mid(4); } }
		R->Add(0.1f, [](FRun& Rn) { CheckCards(Rn); });
		R->Add(1.5f, [](FRun& Rn) { Rn.Shot(TEXT("asda_cardqty")); });
		R->Add(1.5f, [](FRun& Rn) { CheckModal(Rn); });
		R->Add(1.5f, [](FRun& Rn) { Rn.Shot(TEXT("asda_cardqty_1559")); });
		R->Add(1.5f, [](FRun& Rn) { CheckNotice(Rn); });
		R->Add(1.5f, [](FRun& Rn) { Rn.Shot(TEXT("asda_cardqty_notice")); });
		R->Add(1.0f, [](FRun& Rn) { CheckGiftPanel(Rn); });
		R->Add(1.5f, [](FRun& Rn) { Rn.Shot(TEXT("asda_cardqty_gift")); });
		R->Add(1.0f, [](FRun& Rn) { CheckGiftModal(Rn); });
		R->Add(1.5f, [](FRun& Rn) { Rn.Shot(TEXT("asda_cardqty_gift1559")); });
		R->Add(1.5f, [](FRun& Rn) { CloseGift(Rn); });
		R->Add(2.0f, [](FRun& Rn)
		{
			for (const FString& Name : Rn.Shots)
			{
				const FString Src = FPaths::ScreenShotDir() / (Name + TEXT(".png"));
				if (Rn.OutDir.IsEmpty()) { continue; }
				IFileManager::Get().MakeDirectory(*Rn.OutDir, true);
				Rn.Check(IFileManager::Get().Copy(*(Rn.OutDir / (Name + TEXT(".png"))), *Src) == COPY_OK, FString::Printf(TEXT("кадр %s → %s"), *Src, *Rn.OutDir));
			}
			if (UAsdaPremiumShopSubsystem* P = Rn.Sub<UAsdaPremiumShopSubsystem>()) { P->CloseWindow(); }
		});
		R->Start();
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
