// Copyright (c) 2026 @sitxovski (Discord)
// ★★ САМОТЕСТ «ПОДАРОК АДМИНИСТРАТОРУ» (08.11.2026, пачка 20 баг 9 — решение владельца): `asda.CashGift.GmRefuseSelfTest [quit]`.
//   Офлайн (-AsdaNoNet). Проверяет ключ `asda.CashGift.RefuseGm` в общей точке `UAsdaCashGiftSubsystem` под перехватом отправок:
//   A. настоящие отправители: 5072 из 6000 и 15300 на «GM_Test» / «gm_test» / « Gm_Admin » — пакета нет, окно 1500 с loc 991211
//      (текст по языку сессии: RU «Администраторы не принимают подарки :(», EN «Admins are not accepting gifts :(»); обычное имя
//      «Получатель», «GMTest», «XGM_» — 5072 уходит; ключ 0 — «GM_Test» уходит (как оригинал);
//      5074: подтверждённое 5073 имя «GM_Test» (6000) и поле 15519 «GM_Test» (15300) — пакета нет; обычное имя — уходит;
//   B. путь игрока: окно 6000 — «Подарок» карточки → панель 6296 (и 6333, если есть карточка спецпредмета) → имя → «Проверить»;
//      окно 15300 — «Подарок» ячейки → поле 15519 → «ОК» 15520. Кадры `Reports/batch20/gift_gm/<ru|en>_*.png`.
//   Итог: «ПОДАРОК-GM ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Login/AsdaLoginData.h"
#include "Text/AsdaLang.h"
#include "UI/AsdaCashGiftSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaPremiumShopSubsystem.h"
#include "UI/AsdaBoosterShopSubsystem.h"
#include "UI/AsdaPurchaseBoxSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaGiftGmTest, Log, All);

namespace AsdaGiftGmTest
{
	static const TCHAR* kCmd = TEXT("asda.CashGift.GmRefuseSelfTest");   // имя команды
	static const TCHAR* kSelfName = TEXT("ДарительГМ");                  // своё имя офлайн-спавна
	static const TCHAR* kRcpt = TEXT("Получатель");                      // обычный получатель
	static const TCHAR* kGm = TEXT("GM_Test");                           // администратор
	constexpr int32 kLocGm = 991211;                                     // «Администраторы не принимают подарки :(»

	// Перехваченная отправка.
	struct FSent { int32 Op = 0; TArray<uint8> Body; };

	// Контекст прогона.
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                    // пройдено / провалено
		TWeakObjectPtr<UWorld> W;                  // мир прогона
		bool bQuit = false;                        // выйти после итога
		uint32 CapGen = 0;                         // поколение перехвата
		TArray<FSent> Sent;                        // перехваченные отправки
		TUniquePtr<FAsdaNetSelfScope> Self;        // «свой персонаж» сети — вернуть в конце
		FString Lang;                              // ru / en — префикс кадров
		FString Want;                              // ожидаемый текст окна по языку
		int32 FnCard = -1, FnCat = -1;             // карточка спецпредмета 6000 (панель 6333)

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaGiftGmTest, Log,   TEXT("ПОДАРОК-GM ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaGiftGmTest, Error, TEXT("ПОДАРОК-GM FAIL %s"), *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
		int32 Count(int32 Op) const { int32 N = 0; for (const FSent& S : Sent) { N += (S.Op == Op) ? 1 : 0; } return N; }
		// Окно 1500 наверху очереди показывает текст loc 991211 языка сессии.
		bool GmBoxShown() const
		{
			const UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>();
			return WM && WM->GetMsgBoxQueueNum() > 0 && WM->GetMsgBoxQueueTopText() == Want;
		}
		void Shot(const TCHAR* Name) const
		{
			const FString File = FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/batch20/gift_gm"), FString::Printf(TEXT("%s_%s.png"), *Lang, Name));
			FScreenshotRequest::RequestScreenshot(File, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
			UE_LOG(LogAsdaGiftGmTest, Log, TEXT("ПОДАРОК-GM кадр: %s"), *FPaths::ConvertRelativePathToFull(File));
		}
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Тело 5073 (34 б): код, маршрут, имя (как в `asda.SelfTest.CashGift`).
	static TArray<uint8> Body5073(uint8 Code, const FString& Name)
	{
		TArray<uint8> B; B.SetNumZeroed(34);
		B[0] = Code; B[1] = 3; B[2] = 1; B[3] = 2; B[4] = 4; B[5] = 5; B[6] = 77;
		AsdaBytes::WrI32(&B[7], 0x01020304); B[11] = 2; AsdaBytes::WrI16(&B[12], 0x1234);
		FAsdaLoginData::EncodeCp1251(Name, &B[14], 20);
		return B;
	}
	static void Feed(const FCtxRef& C, int32 Op, const TArray<uint8>& Body)
	{
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { Net->DebugFeedPacket(Op, Body); }
		if (UAsdaCashGiftSubsystem* G = C->Sub<UAsdaCashGiftSubsystem>()) { G->DebugPump(); }
	}
	static void CloseMsgBoxes(const FCtxRef& C)
	{
		if (UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>()) { WM->ClearMsgBoxQueue(kCmd); }
	}
	static int32 PickItem(const UAsdaItemTableSubsystem* IT)
	{
		for (const FAsdaItemRecord& R : IT->GetAllItems()) { if (R.ItemCode > 0 && R.Price > 0 && R.ItemType >= 0 && R.ItemType <= 9) { return R.ItemCode; } }
		return 0;
	}

	static void Finish(const FCtxRef& C)
	{
		CloseMsgBoxes(C);
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { if (C->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, C->CapGen); C->CapGen = 0; } }
		if (UAsdaCashGiftSubsystem* G = C->Sub<UAsdaCashGiftSubsystem>()) { G->ResetSessionState(EAsdaSessionResetReason::Disconnected); }
		if (C->Self) { C->Self->Restore(); C->Self.Reset(); }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaGiftGmTest, TEXT("ПОДАРОК-GM"), C->Ok, C->Fail);
	}

	static void After(const FCtxRef& C, float Sec, TFunction<void(const FCtxRef&)> Fn)
	{
		UWorld* W = C->W.Get();
		if (!W) { Finish(C); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C, Fn]() { Fn(C); }), Sec, false);
	}

	// Одна проверка 5072: имя → ожидание «ушёл / отказ с окном».
	static void Check5072(const FCtxRef& C, EAsdaGiftSource Src, const FString& Name, bool bWantSent, const TCHAR* Tag)
	{
		UAsdaCashGiftSubsystem* G = C->Sub<UAsdaCashGiftSubsystem>();
		CloseMsgBoxes(C);
		G->DebugClearFlags();
		C->Sent.Reset();
		const bool bOk = G->SendCheckName(Src, Name);
		const TCHAR* Where = Src == EAsdaGiftSource::Shop6000 ? TEXT("6000") : TEXT("15300");
		if (bWantSent)
		{
			C->Expect(bOk && C->Count(5072) == 1, FString::Printf(TEXT("%s 5072 из %s «%s» → ушёл"), Tag, Where, *Name));
		}
		else
		{
			C->Expect(!bOk && C->Count(5072) == 0 && G->DebugLastLoc() == kLocGm && C->GmBoxShown(),
				FString::Printf(TEXT("%s 5072 из %s «%s» → не ушёл, окно 1500 «%s» (loc %d)"), Tag, Where, *Name, *C->Want, G->DebugLastLoc()));
		}
	}

	// ═══ A: настоящие отправители ═══
	static void StageNet(const FCtxRef& C)
	{
		UAsdaCashGiftSubsystem* G = C->Sub<UAsdaCashGiftSubsystem>();
		UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
		if (!G || !IT) { C->Expect(false, TEXT("подсистемы подарка/таблиц есть")); Finish(C); return; }
		AsdaCVarSave::SetForTest(TEXT("asda.CashGift.Enable"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.CashGift.FixK5"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.CashGift.RefuseGm"), 1);
		const FString Txt = AsdaNotice::LocRaw(C->GI(), kLocGm);
		C->Expect(Txt == C->Want, FString::Printf(TEXT("A0 loc %d (%s) = «%s» (есть «%s»)"), kLocGm, *C->Lang, *C->Want, *Txt));

		for (const EAsdaGiftSource Src : { EAsdaGiftSource::Shop6000, EAsdaGiftSource::Booster15300 })
		{
			Check5072(C, Src, kGm, false, TEXT("A1"));
			Check5072(C, Src, TEXT("gm_test"), false, TEXT("A1 регистр"));
			Check5072(C, Src, TEXT(" Gm_Admin "), false, TEXT("A1 пробелы поля"));
			Check5072(C, Src, kRcpt, true, TEXT("A2 обычное имя"));
			Check5072(C, Src, TEXT("GMTest"), true, TEXT("A2 без «_»"));
			Check5072(C, Src, TEXT("XGM_"), true, TEXT("A2 «GM_» не в начале"));
		}
		AsdaCVarSave::SetForTest(TEXT("asda.CashGift.RefuseGm"), 0);
		Check5072(C, EAsdaGiftSource::Shop6000, kGm, true, TEXT("A3 ключ 0 (как оригинал)"));
		AsdaCVarSave::SetForTest(TEXT("asda.CashGift.RefuseGm"), 1);

		// A4 5074 — второй рубеж.
		const int32 Item = PickItem(IT);
		C->Expect(Item > 0, FString::Printf(TEXT("A4 код item_list %d"), Item));
		CloseMsgBoxes(C); G->DebugClearFlags(); C->Sent.Reset();
		Feed(C, 5073, Body5073(1, kGm));   // сервер подтвердил «GM_Test» (например, ключ был 0 при проверке)
		CloseMsgBoxes(C);
		const bool b6000 = G->SendPresent(EAsdaGiftSource::Shop6000, Item, 1, 99999999);
		C->Expect(G->GetConfirmedName() == kGm && !b6000 && C->Count(5074) == 0 && C->GmBoxShown(),
			TEXT("A4 5074 из 6000, подтверждённое имя «GM_Test» → не ушёл, окно 991211"));
		CloseMsgBoxes(C); G->DebugClearFlags();
		const bool b15300 = G->SendPresent(EAsdaGiftSource::Booster15300, Item, 1, 99999999, kGm);
		C->Expect(!b15300 && C->Count(5074) == 0 && C->GmBoxShown(), TEXT("A4 5074 из 15300, поле 15519 «GM_Test» → не ушёл, окно 991211"));
		CloseMsgBoxes(C); G->DebugClearFlags(); C->Sent.Reset();
		Feed(C, 5073, Body5073(1, kRcpt));
		CloseMsgBoxes(C);
		const bool bNorm = G->SendPresent(EAsdaGiftSource::Shop6000, Item, 1, 99999999);
		C->Expect(bNorm && C->Count(5074) == 1, TEXT("A5 5074 из 6000, обычное подтверждённое имя → ушёл"));
		CloseMsgBoxes(C); G->DebugClearFlags(); C->Sent.Reset();
		const bool bNorm2 = G->SendPresent(EAsdaGiftSource::Booster15300, Item, 1, 99999999, kRcpt);
		C->Expect(bNorm2 && C->Count(5074) == 1, TEXT("A5 5074 из 15300, обычное имя поля → ушёл"));
		CloseMsgBoxes(C);
		G->ResetSessionState(EAsdaSessionResetReason::Disconnected);
	}

	// Проверка панели 6000 на пути игрока (6296 или 6333): «GM_Test» → отказ и кадр; обычное имя → 5072.
	static void PanelCheck(const FCtxRef& C, const TCHAR* Panel, TFunction<void(const FCtxRef&)> Next)
	{
		UAsdaPremiumShopSubsystem* P = C->Sub<UAsdaPremiumShopSubsystem>();
		UAsdaCashGiftSubsystem* G = C->Sub<UAsdaCashGiftSubsystem>();
		if (!P || !G) { Finish(C); return; }
		C->Expect(P->IsGiftPanelShown(), FString::Printf(TEXT("B %s показана (путь игрока)"), Panel));
		CloseMsgBoxes(C); G->DebugClearFlags(); C->Sent.Reset();
		P->DebugSetGiftName(kGm);
		P->OnGiftCheckClicked();
		C->Expect(C->Count(5072) == 0 && C->GmBoxShown(), FString::Printf(TEXT("B %s «Проверить» «GM_Test» → 5072 нет, окно «%s»"), Panel, *C->Want));
		const FString Shot = FString::Printf(TEXT("%s_gm"), Panel);
		After(C, 0.6f, [Shot, Panel, Next](const FCtxRef& C2)
		{
			C2->Shot(*Shot);
			After(C2, 0.6f, [Panel, Next](const FCtxRef& C3)
			{
				UAsdaPremiumShopSubsystem* P3 = C3->Sub<UAsdaPremiumShopSubsystem>();
				UAsdaCashGiftSubsystem* G3 = C3->Sub<UAsdaCashGiftSubsystem>();
				if (!P3 || !G3) { Finish(C3); return; }
				CloseMsgBoxes(C3); G3->DebugClearFlags(); C3->Sent.Reset();
				P3->DebugSetGiftName(kRcpt);
				P3->OnGiftCheckClicked();
				C3->Expect(C3->Count(5072) == 1 && !C3->GmBoxShown(), FString::Printf(TEXT("B %s «Проверить» «%s» → 5072 ушёл"), Panel, kRcpt));
				G3->DebugClearFlags();
				Next(C3);
			});
		});
	}

	// ═══ B3: окно 15300 ═══
	static void StageBooster(const FCtxRef& C)
	{
		UAsdaBoosterShopSubsystem* Bs = C->Sub<UAsdaBoosterShopSubsystem>();
		if (!Bs) { C->Expect(false, TEXT("B подсистема 15300 есть")); Finish(C); return; }
		CloseMsgBoxes(C);
		Bs->OpenWindow();
		Bs->DebugSetGxp(99999999);
		After(C, 1.0f, [](const FCtxRef& C2)
		{
			UAsdaBoosterShopSubsystem* B = C2->Sub<UAsdaBoosterShopSubsystem>();
			UAsdaCashGiftSubsystem* G = C2->Sub<UAsdaCashGiftSubsystem>();
			if (!B || !G) { Finish(C2); return; }
			B->OnCellGiftClicked(0);
			After(C2, 0.8f, [](const FCtxRef& C3)
			{
				UAsdaBoosterShopSubsystem* B3 = C3->Sub<UAsdaBoosterShopSubsystem>();
				UAsdaCashGiftSubsystem* G3 = C3->Sub<UAsdaCashGiftSubsystem>();
				if (!B3 || !G3) { Finish(C3); return; }
				C3->Expect(B3->DebugPurMode() == 2, TEXT("B 15300 «Подарок» ячейки 0 → панель 15515 в режиме подарка"));
				CloseMsgBoxes(C3); G3->DebugClearFlags(); C3->Sent.Reset();
				B3->DebugSetGiftName(kGm);
				B3->OnPurConfirmClicked();
				C3->Expect(C3->Count(5072) == 0 && C3->GmBoxShown(), FString::Printf(TEXT("B 15300 «ОК» 15520 «GM_Test» → 5072 нет, окно «%s»"), *C3->Want));
				After(C3, 0.6f, [](const FCtxRef& C4)
				{
					C4->Shot(TEXT("15300_gm"));
					After(C4, 0.6f, [](const FCtxRef& C5)
					{
						UAsdaBoosterShopSubsystem* B5 = C5->Sub<UAsdaBoosterShopSubsystem>();
						UAsdaCashGiftSubsystem* G5 = C5->Sub<UAsdaCashGiftSubsystem>();
						if (!B5 || !G5) { Finish(C5); return; }
						CloseMsgBoxes(C5); G5->DebugClearFlags(); C5->Sent.Reset();
						B5->DebugSetGiftName(kRcpt);
						B5->OnPurConfirmClicked();
						C5->Expect(C5->Count(5072) == 1 && !C5->GmBoxShown(), FString::Printf(TEXT("B 15300 «ОК» 15520 «%s» → 5072 ушёл"), kRcpt));
						G5->DebugClearFlags();
						B5->CloseWindow();
						Finish(C5);
					});
				});
			});
		});
	}

	// ═══ B1/B2: окно 6000 ═══
	static void StageShop(const FCtxRef& C)
	{
		UAsdaPremiumShopSubsystem* Prem = C->Sub<UAsdaPremiumShopSubsystem>();
		UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
		if (!Prem || !IT) { C->Expect(false, TEXT("B подсистема окна 6000 есть")); Finish(C); return; }
		CloseMsgBoxes(C);
		Prem->OpenWindow(false);
		Prem->DebugSetGxp(99999999);
		int32 EqCard = -1, EqCat = -1;
		for (int32 Cat = 0; Cat < UAsdaPremiumShopSubsystem::kCatTabs && (EqCard < 0 || C->FnCard < 0); ++Cat)
		{
			Prem->SetCategory(Cat);
			for (int32 i = 0; i < UAsdaPremiumShopSubsystem::kCards; ++i)
			{
				const int32 Code = Prem->DebugCardCode(i);
				if (Code <= 0) { continue; }
				FAsdaSpecialItemlistRow Sp;
				if (EqCard < 0 && IT->GetItem(Code)) { EqCard = i; EqCat = Cat; }
				if (C->FnCard < 0 && !IT->GetItem(Code) && IT->GetSpecialItem(Code, Sp)) { C->FnCard = i; C->FnCat = Cat; }
			}
		}
		C->Expect(EqCard >= 0, FString::Printf(TEXT("B 6000: карточка item_list %d (вкладка %d), спецпредмета %d"), EqCard, EqCat, C->FnCard));
		if (EqCard < 0) { StageBooster(C); return; }
		Prem->SetCategory(EqCat);
		Prem->OnCardGiftClicked(EqCard);
		After(C, 1.2f, [](const FCtxRef& C2)
		{
			PanelCheck(C2, TEXT("6296"), [](const FCtxRef& C3)
			{
				UAsdaPremiumShopSubsystem* P = C3->Sub<UAsdaPremiumShopSubsystem>();
				if (!P) { Finish(C3); return; }
				P->CloseGiftPanels();
				if (C3->FnCard < 0)
				{
					UE_LOG(LogAsdaGiftGmTest, Warning, TEXT("ПОДАРОК-GM: карточки спецпредмета на витрине нет — панель 6333 не проверена"));
					P->CloseWindow();
					StageBooster(C3);
					return;
				}
				P->SetCategory(C3->FnCat);
				P->OnCardGiftClicked(C3->FnCard);
				After(C3, 1.2f, [](const FCtxRef& C4)
				{
					PanelCheck(C4, TEXT("6333"), [](const FCtxRef& C5)
					{
						if (UAsdaPremiumShopSubsystem* P5 = C5->Sub<UAsdaPremiumShopSubsystem>()) { P5->CloseGiftPanels(); P5->CloseWindow(); }
						StageBooster(C5);
					});
				});
			});
		});
	}

	static void Start(const FCtxRef& C)
	{
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		if (!Net) { C->Expect(false, TEXT("подсистема сети есть")); Finish(C); return; }
		C->Lang = FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en");
		C->Want = FAsdaLang::IsRu() ? FString(TEXT("Администраторы не принимают подарки :(")) : FString(TEXT("Admins are not accepting gifts :("));
		C->Self = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		C->CapGen = AsdaSelfTestKit::ArmCapture(Net, [C](uint16 Op, const uint8* P, int32 Len, bool /*bPad*/, TCHAR /*Env*/)
		{
			FSent& S = C->Sent.AddDefaulted_GetRef();
			S.Op = Op;
			if (P && Len > 0) { S.Body.Append(P, Len); }
		}, /*OnlyOpcode*/0, kCmd);
		C->Expect(C->CapGen != 0, TEXT("перехват отправок поставлен"));
		if (!C->CapGen) { Finish(C); return; }
		StageNet(C);
		StageShop(C);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaCashGiftGmRefuseSelfTest(
	TEXT("asda.CashGift.GmRefuseSelfTest"),
	TEXT("08.11.2026 пачка 20 баг 9: подарок персонажу «GM_…» не отправляется (5072/5074 из 6000 и 15300), окно loc 991211; "
	     "обычное имя уходит; путь игрока по панелям 6296/6333 и 15515, кадры в Reports/batch20/gift_gm. Офлайн. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaGiftGmTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W, kSelfName))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, *FString::Printf(TEXT("Asda.World.FakeSelfSpawn 256 256 %s"), kSelfName));
			const FString Again = FString::Printf(TEXT("%s selfready%s"), kCmd, KA.bQuit ? TEXT(" quit") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		static const uint16 kGuardOps[] = { 5072, 5074 };
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA, 0.f, MakeArrayView(kGuardOps))) { return; }
		if (!W) { return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KA.bQuit;
		Start(C);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
