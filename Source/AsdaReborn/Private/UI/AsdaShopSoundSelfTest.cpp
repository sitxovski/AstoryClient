// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ ЗВУКОВ ТОРГОВЛИ (06.11.2026, баг игроков «покупка и продажа у NPC проходят без звука»): `asda.SelfTest.ShopSound [quit]`.
//   Офлайн (`-AsdaNoNet`, игра с `-nosound`): проверяется ЗАПРОС звука — журнал `AsdaSoundTapStart/Stop` пишет только звук, чей
//   ассет найден и дошёл до запуска. Ответы сервера приходят тем же делегатом, что у приёма пакетов (`OnShopBuyResult`/`OnShopSellResult`).
//   Разделы: Н) магазин NPC 1300: открытие 5, покупка и продажа — звук сделки, отказ 5021 — звук окна 25, отказ 5019 — без звука
//   (чат), закрытие 6, ключи 0; П) у каждого товара всех магазинов NPC есть звук сделки; Э) снаряжение 6800: открытие 5, кнопки 4,
//   вкладка без звука, закрытие 4 + 6; Е) премиум 6000: открытие 5, страницы 4, покупка 4, закрытие 6; К) нумпад (магазин, склад,
//   аукцион, обмен): 5 / 6. Итог: «ЗВУК-ТОРГОВЛИ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "Components/Button.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Login/AsdaButtonRelay.h"
#include "Npc/AsdaNpcDialogTypes.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaEquipShopSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaNpcShopSubsystem.h"
#include "UI/AsdaPremiumShopSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UObject/UObjectIterator.h"
#include "World/AsdaWorldSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaShopSoundTest, Log, All);

// Журнал запросов звука (AsdaSoundSubsystem.cpp, без правки заголовка).
ASDAREBORN_API void AsdaSoundTapStart();
ASDAREBORN_API TArray<FName> AsdaSoundTapStop();

namespace AsdaShopSoundTest
{
	const TCHAR* const kCmd = TEXT("asda.SelfTest.ShopSound");   // имя команды
	const TCHAR* const kPrefix = TEXT("ЗВУК-ТОРГОВЛИ");          // префикс строк журнала

	// Счётчики проверок.
	struct FRes { int32 Ok = 0, Fail = 0; };

	static void Expect(FRes& R, bool b, const FString& What)
	{
		if (b) { ++R.Ok;   UE_LOG(LogAsdaShopSoundTest, Log,   TEXT("%s ok   %s"), kPrefix, *What); }
		else   { ++R.Fail; UE_LOG(LogAsdaShopSoundTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
	}

	// Какие звуки запросило действие.
	static TArray<FName> Tap(TFunctionRef<void()> Fn)
	{
		AsdaSoundTapStart();
		Fn();
		return AsdaSoundTapStop();
	}

	// Журнал строкой — для строк проверок.
	static FString Str(const TArray<FName>& L)
	{
		return L.Num() ? FString::JoinBy(L, TEXT(","), [](const FName& N) { return N.ToString(); }) : FString(TEXT("тишина"));
	}

	// Есть ли в журнале звук падения предмета (таблица `Sod_drop_*`).
	static bool HasDrop(const TArray<FName>& L)
	{
		return L.ContainsByPredicate([](const FName& N) { return N.ToString().StartsWith(TEXT("Sod_drop_")); });
	}

	// Нажать кнопку окна: реле владельца `Owner` с индексом `Id` (тот колбэк, что зовёт клик Slate).
	static bool Press(UObject* Owner, int32 Id)
	{
		for (TObjectIterator<UAsdaButtonRelay> It; It; ++It)
		{
			UAsdaButtonRelay* B = *It;
			if (IsValid(B) && B->Index == Id && B->GetOuter() == Owner && B->Callback && !B->HasDeadBinding())
			{
				B->Callback(B->Index);
				return true;
			}
		}
		return false;
	}

	// Тело ответа с кодом результата в первом байте.
	static TArray<uint8> Body(int32 Size, uint8 Code)
	{
		TArray<uint8> B;
		B.SetNumZeroed(Size);
		B[0] = Code;
		return B;
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNpcShopSubsystem* S = GI ? GI->GetSubsystem<UAsdaNpcShopSubsystem>() : nullptr;
		UAsdaEquipShopSubsystem* Eq = GI ? GI->GetSubsystem<UAsdaEquipShopSubsystem>() : nullptr;
		UAsdaPremiumShopSubsystem* Pr = GI ? GI->GetSubsystem<UAsdaPremiumShopSubsystem>() : nullptr;
		UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UDataTable* DT = LoadObject<UDataTable>(nullptr, TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcShopList.DT_NpcShopList"));
		if (!S || !Eq || !Pr || !Inv || !Net || !Snd || !WM || !WS || !DT)
		{
			AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет подсистем или таблицы магазинов"));
			return;
		}
		FRes R;
		const FName N4 = Snd->ResolveSoundName(4), N5 = Snd->ResolveSoundName(5), N6 = Snd->ResolveSoundName(6), N25 = Snd->ResolveSoundName(25);
		Expect(R, N4 == FName(TEXT("S_e_button05")) && N5 == FName(TEXT("S_e_button06")) && N6 == FName(TEXT("S_e_button07")) && N25 == FName(TEXT("S_danger")),
			FString::Printf(TEXT("таблица SFT: 4=%s 5=%s 6=%s 25=%s"), *N4.ToString(), *N5.ToString(), *N6.ToString(), *N25.ToString()));
		AsdaCVarSave::Remember(TEXT("asda.Shop.WindowSound"));
		AsdaCVarSave::Remember(TEXT("asda.Shop.TradeSound"));
		AsdaCVarSave::Remember(TEXT("asda.Shop.TradeSoundFallback"));
		AsdaCVarSave::Remember(TEXT("asda.EquipShop.ButtonSound"));
		AsdaCVarSave::Remember(TEXT("asda.PremiumShop.UiSound"));
		const int32 GoldWas = Inv->GetGold();
		Inv->SetGoldDisplay(100000000);

		// П) покрытие: у каждого товара всех магазинов NPC есть звук сделки.
		int32 NpcId = 0, Code = 0, Total = 0, Silent = 0, SilentCode = 0;
		FString SilentList;
		TSet<int32> Seen;
		for (const TPair<FName, uint8*>& P : DT->GetRowMap())
		{
			const FAsdaNpcShopRow* Row = reinterpret_cast<const FAsdaNpcShopRow*>(P.Value);
			if (!Row) { continue; }
			for (int32 C : Row->ItemCodes)
			{
				if (C <= 0 || Seen.Contains(C)) { continue; }
				Seen.Add(C);
				++Total;
				if (!SilentCode && GI->GetSubsystem<UAsdaItemTableSubsystem>()->GetDropSoundCat(C) == 255) { SilentCode = C; }
				const TArray<FName> L = Tap([&]() { S->PlayDealSound(C); });
				if (HasDrop(L))
				{
					if (!NpcId) { NpcId = Row->NpcId; Code = C; }
				}
				else
				{
					++Silent;
					if (SilentList.Len() < 240) { SilentList += FString::Printf(TEXT(" %d"), C); }
				}
			}
		}
		Expect(R, Total > 0 && Silent == 0, FString::Printf(TEXT("П: звук сделки есть у %d из %d товаров магазинов NPC; без звука:%s"), Total - Silent, Total, *SilentList));

		// Н) магазин NPC: покупка.
		if (NpcId > 0)
		{
			TArray<FName> L = Tap([&]() { S->OpenShop(5, NpcId); });
			Expect(R, S->IsOpen() && L.Contains(N5), FString::Printf(TEXT("Н: открытие магазина NPC %d — %s"), NpcId, *Str(L)));
			S->DebugPutCart(Code, 1);
			S->DebugConfirm();
			L = Tap([&]() { Net->OnShopBuyResult.Broadcast(Body(406, 1)); S->Tick(0.f); });
			Expect(R, HasDrop(L), FString::Printf(TEXT("Н: покупка, 5021 успех (товар %d) — %s"), Code, *Str(L)));
			Inv->SetGoldDisplay(100000000);
			S->DebugPutCart(Code, 1);
			S->DebugConfirm();
			L = Tap([&]() { Net->OnShopBuyResult.Broadcast(Body(406, 2)); S->Tick(0.f); });
			Expect(R, L.Contains(N25), FString::Printf(TEXT("Н: покупка, 5021 отказ 2 — окно с звуком 25: %s"), *Str(L)));
			WM->AnswerMsgBox(false, true);
			L = Tap([&]() { S->CloseShop(); });
			Expect(R, !S->IsOpen() && L.Contains(N6), FString::Printf(TEXT("Н: закрытие — %s"), *Str(L)));

			// Продажа.
			S->OpenShop(6, NpcId);
			S->DebugPutCart(Code, 1);
			S->DebugConfirm();
			L = Tap([&]() { Net->OnShopSellResult.Broadcast(Body(62, 1)); S->Tick(0.f); });
			Expect(R, HasDrop(L), FString::Printf(TEXT("Н: продажа, 5019 успех — %s"), *Str(L)));
			Inv->SetGoldDisplay(100000000);
			S->DebugPutCart(Code, 1);
			S->DebugConfirm();
			L = Tap([&]() { Net->OnShopSellResult.Broadcast(Body(62, 0)); S->Tick(0.f); });
			Expect(R, !HasDrop(L) && !L.Contains(N25), FString::Printf(TEXT("Н: продажа, 5019 отказ 0 — только чат, как в оригинале: %s"), *Str(L)));
			S->CloseShop();

			// Ключи 0 — прежнее поведение.
			AsdaCVarSave::SetForTest(TEXT("asda.Shop.WindowSound"), 0);
			AsdaCVarSave::SetForTest(TEXT("asda.Shop.TradeSound"), 0);
			L = Tap([&]()
			{
				S->OpenShop(5, NpcId);
				S->DebugPutCart(Code, 1);
				S->DebugConfirm();
				Net->OnShopBuyResult.Broadcast(Body(406, 1));
				S->Tick(0.f);
				S->CloseShop();
			});
			Expect(R, L.Num() == 0, FString::Printf(TEXT("Н: ключи 0 — магазин молчит (было до правки и у оригинала нет звука сделки): %s"), *Str(L)));
			AsdaCVarSave::SetForTest(TEXT("asda.Shop.WindowSound"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Shop.TradeSound"), 1);
		}
		else
		{
			Expect(R, false, TEXT("Н: нет ни одного товара со звуком — магазин NPC не проверен"));
		}

		// Ключ запасного звука 0 — расходник снова молчит (воспроизведение жалобы).
		if (SilentCode > 0)
		{
			AsdaCVarSave::SetForTest(TEXT("asda.Shop.TradeSoundFallback"), 0);
			const TArray<FName> L = Tap([&]() { S->PlayDealSound(SilentCode); });
			Expect(R, L.Num() == 0, FString::Printf(TEXT("П: ключ 0 — товар %d без категории молчит (жалоба игрока): %s"), SilentCode, *Str(L)));
			AsdaCVarSave::SetForTest(TEXT("asda.Shop.TradeSoundFallback"), 1);
		}
		Expect(R, SilentCode > 0, FString::Printf(TEXT("П: товаров без категории звука в магазинах: первый %d"), SilentCode));

		// Э) магазин снаряжения 6800.
		{
			TArray<FName> L = Tap([&]() { Eq->OpenWindow(); });
			Expect(R, Eq->IsOpen() && L.Contains(N5), FString::Printf(TEXT("Э: открытие 6800 — %s"), *Str(L)));
			for (int32 Id : { 6853, 6855, 6848, 6849 })
			{
				bool bPressed = false;
				L = Tap([&]() { bPressed = Press(Eq, Id); });
				Expect(R, bPressed && L.Contains(N4), FString::Printf(TEXT("Э: кнопка %d — %s"), Id, *Str(L)));
			}
			bool bTab = false;
			L = Tap([&]() { bTab = Press(Eq, 6856); });
			Expect(R, bTab && !L.Contains(N4), FString::Printf(TEXT("Э: вкладка 6856 без звука 4 — %s"), *Str(L)));
			AsdaCVarSave::SetForTest(TEXT("asda.EquipShop.ButtonSound"), 0);
			L = Tap([&]() { Press(Eq, 6853); });
			Expect(R, !L.Contains(N4), FString::Printf(TEXT("Э: ключ 0 — кнопка молчит: %s"), *Str(L)));
			AsdaCVarSave::SetForTest(TEXT("asda.EquipShop.ButtonSound"), 1);
			Eq->DebugArmBuy(Code);
			L = Tap([&]() { Net->OnShopBuyResult.Broadcast(Body(406, 1)); Eq->Tick(0.f); });
			Expect(R, HasDrop(L), FString::Printf(TEXT("Э: покупка, 5021 успех — %s"), *Str(L)));
			Inv->SetGoldDisplay(100000000);
			bool bClose = false;
			L = Tap([&]() { bClose = Press(Eq, 6801); });
			Expect(R, bClose && !Eq->IsOpen() && L.Contains(N4) && L.Contains(N6), FString::Printf(TEXT("Э: крестик 6801 — %s"), *Str(L)));
		}

		// Е) премиум-магазин 6000.
		{
			TArray<FName> L = Tap([&]() { Pr->OpenWindow(false); });
			Expect(R, Pr->IsOpen() && L.Contains(N5), FString::Printf(TEXT("Е: открытие 6000 — %s"), *Str(L)));
			for (int32 Id : { 6015, 6017, 6019, 6021 })
			{
				bool bPressed = false;
				L = Tap([&]() { bPressed = Press(Pr, Id); });
				Expect(R, bPressed && L.Contains(N4), FString::Printf(TEXT("Е: кнопка страницы %d — %s"), Id, *Str(L)));
			}
			UButton* Buy = Cast<UButton>(Pr->DebugCardBuyWidget(0));
			L = Tap([&]() { if (Buy) { Buy->OnClicked.Broadcast(); } });
			Expect(R, Buy && L.Contains(N4), FString::Printf(TEXT("Е: кнопка покупки карточки — %s"), *Str(L)));
			WM->AnswerMsgBox(false, false);
			L = Tap([&]() { Pr->CloseWindow(); });
			Expect(R, !Pr->IsOpen() && L.Contains(N6), FString::Printf(TEXT("Е: закрытие — %s"), *Str(L)));
			// Цикл «открыть-закрыть» с ключом 1: сверх звуков окна 6000 играют окна снимка (сумка и др.) — их ключ не касается.
			const TArray<FName> On = Tap([&]() { Pr->OpenWindow(false); Pr->CloseWindow(); });
			AsdaCVarSave::SetForTest(TEXT("asda.PremiumShop.UiSound"), 0);
			L = Tap([&]() { Pr->OpenWindow(false); Pr->CloseWindow(); });
			Expect(R, L.Num() == On.Num() - 2, FString::Printf(TEXT("Е: ключ 0 — окно 6000 своих 5 и 6 не играет: с ключом %s, без ключа %s"), *Str(On), *Str(L)));
			AsdaCVarSave::SetForTest(TEXT("asda.PremiumShop.UiSound"), 1);
		}

		// К) нумпад — общее окно количества магазина, склада, аукциона и обмена.
		{
			TArray<FName> L = Tap([&]() { WM->ShowNumPad(5, [](bool, int32) {}); });
			Expect(R, WM->IsNumPadOpen() && L.Contains(N5), FString::Printf(TEXT("К: нумпад открыт — %s"), *Str(L)));
			L = Tap([&]() { WM->CloseNumPad(false); });
			Expect(R, !WM->IsNumPadOpen() && L.Contains(N6), FString::Printf(TEXT("К: нумпад закрыт — %s"), *Str(L)));
		}

		Inv->SetGoldDisplay(GoldWas);
		AsdaSelfTestKit::Finish(W, kCmd, Args.bQuit, LogAsdaShopSoundTest, kPrefix, R.Ok, R.Fail);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestShopSound(
	TEXT("asda.SelfTest.ShopSound"),
	TEXT("Самотест звуков торговли: магазин NPC, снаряжение 6800, премиум 6000, нумпад; покрытие звука сделки всех товаров. Офлайн, карта мира. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaShopSoundTest::Run(A, W);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
