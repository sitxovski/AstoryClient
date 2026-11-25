// Copyright (c) 2026 @sitxovski (Discord)
// AsdaItemLevelGateSelfTest.cpp — самотест [ИСПРАВЛЕНИЯ] 29.10.2026 «минимальный уровень предмета при применении»
//   (баг игроков «Pig Pang Vehicle — This booster shall not be open till level 10»; шапка — Items/AsdaItemLevelGate.h).
//
// СЦЕНАРИЙ `asda.Item.LevelGateSelfTest [quit]` (офлайн, синхронно, ничего не пишет в ассеты и настройки):
//   1) гейт `AsdaItemLevelGate::RefuseBelowLevel` — тем же вызовом, что двойной клик по бустеру/коробке/расходнику:
//      бустер свинки-шарика 28229 (уровень 10) на 4/9 — отказ, на 10/11 — пропуск; коробка 73362 (уровень 40) на 39 — отказ,
//      на 40 — пропуск; зелье 20572 (уровень 1) на 1 — пропуск; уровень неизвестен (0) — пропуск (решает сервер P95);
//   2) простая подсказка `BuildTooltipSimple` (путь вкладки «Расходники» и карточек магазинов) у 73359 (itemType 11, уровень 40):
//      первая строка — «<Уровень> : 40 <минимум>», у зелья 20572 (уровень 1) строки уровня нет;
//   3) ключи: `asda.Item.UseLevelGate 0` — 28229 на 4 проходит (как эталон); `asda.ItemTip.LevelLine 0` — строки нет;
//   4) 75521 «Радужный жетон огурцоида» — предмет-титул (подтип 14, LinkCode 208), не коробка (DT_item_list поправлен 29.10).
//   Итог — `[уровень предмета] ИТОГ: ok=N fail=M` (ловит `asda.SelfTest.Batch`). Свой уровень (`SelfLevel`) возвращается.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"   // ASDA_DEV_CONSOLE — дев-команда вне Shipping
#include "Items/AsdaItemLevelGate.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "AsdaSelf.h"   // уровень своего персонажа (проверка «неизвестен»)
#include "AsdaDataTableCache.h"        // AsdaDt::RowByInt — витрина лавки NPC
#include "Npc/AsdaNpcDialogTypes.h"    // FAsdaNpcShopRow
#include "Test/AsdaSelfTestKit.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaItemLevelGateTest, Log, All);

#if ASDA_DEV_CONSOLE   // дев-команда, в Shipping не регистрируется
namespace AsdaItemLevelGateTest
{
	// Подменить целочисленный ключ консоли на время проверки; возвращает прежнее значение (−1 — ключа нет).
	static int32 SetCVar(const TCHAR* Name, int32 V)
	{
		IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(Name);
		if (!C) { return -1; }
		const int32 Was = C->GetInt();
		C->Set(V, ECVF_SetByConsole);
		return Was;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaItemLevelGateSelfTest(
	TEXT("asda.Item.LevelGateSelfTest"),
	TEXT("asda.Item.LevelGateSelfTest [quit] — [ИСПРАВЛЕНИЕ] 29.10.2026: минимальный уровень предмета при применении (5028/6096/5468) "
	     "и строка уровня в простой подсказке itemType 11. Только офлайн."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		using namespace AsdaItemLevelGateTest;
		static const TCHAR* const kCmd = TEXT("asda.Item.LevelGateSelfTest");
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (!W || AsdaSelfTestKit::RefuseStart(W, kCmd, Args)) { return; }
		UGameInstance* GI = W->GetGameInstance();
		UAsdaNetSubsystem* Net = GI ? AsdaTestNet::Get(GI) : nullptr;
		UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
		if (!Net || !IT || !Tip)
		{
			AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет подсистемы сети/таблиц/подсказки"));
			return;
		}
		int32 Ok = 0, Fail = 0;
		auto Check = [&Ok, &Fail](bool bCond, const FString& What)
		{
			(bCond ? Ok : Fail)++;
			UE_LOG(LogAsdaItemLevelGateTest, Log, TEXT("[уровень предмета] %s — %s"), bCond ? TEXT("ok  ") : TEXT("FAIL"), *What);
		};
		const uint8 WasLevel = AsdaTestNet::SelfLevel(Net);
		using AsdaItemLevelGate::EUse;
		auto Gate = [&](int32 Lv, int32 Code, EUse U) { AsdaTestNet::SelfLevel(Net) = (uint8)Lv; return AsdaItemLevelGate::RefuseBelowLevel(GI, Code, U); };

		// 1) гейт по уровню — данные из таблиц порта (уровни сверены с серверной item_table 29.10).
		const FAsdaItemRecord* Pig = IT->GetItem(28229);
		Check(Pig && Pig->RequiredLevel == 10, FString::Printf(TEXT("28229 «бустер свинки-шарика (3 дня)» RequiredLevel=%d (ждём 10)"), Pig ? Pig->RequiredLevel : -1));
		Check(Gate(4, 28229, EUse::Booster),   TEXT("28229 на 4-м уровне — отказ"));
		Check(Gate(9, 28229, EUse::Booster),   TEXT("28229 на 9-м уровне — отказ"));
		Check(!Gate(10, 28229, EUse::Booster), TEXT("28229 на 10-м уровне — пропуск"));
		Check(!Gate(11, 28229, EUse::Booster), TEXT("28229 на 11-м уровне — пропуск"));
		Check(Gate(39, 73362, EUse::Package),  TEXT("коробка 73362 (уровень 40) на 39-м — отказ"));
		Check(!Gate(40, 73362, EUse::Package), TEXT("коробка 73362 на 40-м — пропуск"));
		Check(!Gate(1, 20572, EUse::Consumable), TEXT("зелье 20572 (уровень 1) на 1-м — пропуск"));
		AsdaTestNet::SelfLevel(Net) = 0;
		const bool bUnknown = !AsdaItemLevelGate::RefuseBelowLevel(GI, 28229, EUse::Booster) || AsdaSelf::GetLevel(GI) > 0;
		Check(bUnknown, TEXT("уровень персонажа неизвестен — гейт пропускает (решает сервер)"));
		const int32 WasGate = SetCVar(TEXT("asda.Item.UseLevelGate"), 0);
		Check(!Gate(4, 28229, EUse::Booster), TEXT("asda.Item.UseLevelGate 0 — 28229 на 4-м проходит, как эталон"));
		SetCVar(TEXT("asda.Item.UseLevelGate"), WasGate < 0 ? 1 : WasGate);

		// 2) строка уровня в простой подсказке.
		AsdaTestNet::SelfLevel(Net) = 30;
		FAsdaTipMetrics M;
		const FAsdaItemTooltipText T1 = Tip->BuildTooltipSimple(73359, 1, 0, M);
		const bool bHasLvl = T1.bValid && T1.Body.Contains(TEXT(" : 40 "));
		Check(bHasLvl, FString::Printf(TEXT("подсказка 73359 (уровень 40) содержит строку уровня: «%s»"), *T1.Body.Left(120).Replace(TEXT("\n"), TEXT(" | "))));
		const FAsdaItemTooltipText T2 = Tip->BuildTooltipSimple(20572, 1, 0, M);
		Check(T2.bValid && !T2.Body.Contains(TEXT(" : 1 ")), TEXT("подсказка зелья 20572 (уровень 1) — без строки уровня"));
		const int32 WasLine = SetCVar(TEXT("asda.ItemTip.LevelLine"), 0);
		const FAsdaItemTooltipText T3 = Tip->BuildTooltipSimple(73359, 1, 0, M);
		Check(T3.bValid && !T3.Body.Contains(TEXT(" : 40 ")), TEXT("asda.ItemTip.LevelLine 0 — строки уровня нет, как эталон"));
		SetCVar(TEXT("asda.ItemTip.LevelLine"), WasLine < 0 ? 1 : WasLine);

		// 3) [ИСПРАВЛЕНИЕ] 29.10.2026: 75521 «Радужный жетон огурцоида» — знак охоты (предмет-титул, LinkCode 208), как у
		//    оригинального клиента и у сервера после hunt signs (07.10): НЕ коробка (раньше ведомость 25.09 делала его
		//    коробкой пакета 4476 по стоку сервера — двойной клик открывал панель 26500, сервер отвечал 6097 кодом 5).
		const FAsdaItemRecord* Tok = IT->GetItem(75521);
		Check(Tok && Tok->ItemSubType == 14 && Tok->DurationOrEnchant == 0 && Tok->LinkCode == 208,
			FString::Printf(TEXT("75521: подтип %d, пакет %d, LinkCode %d (ждём 14 / 0 / 208)"),
				Tok ? Tok->ItemSubType : -1, Tok ? Tok->DurationOrEnchant : -1, Tok ? Tok->LinkCode : -1));
		Check(!IT->IsPackageItem(75521), TEXT("75521 — не коробка (панель состава не открывается)"));
		Check(IT->IsPackageItem(76206), TEXT("76206 «Костюм оленя-ветерана» (тот же пакет 4476) — по-прежнему коробка"));

		// 5) [ИСПРАВЛЕНИЕ] 29.10.2026 (проверка владельца 4.0.11): описание 75521 — своё, а не заглушка «костюм Кролика» RU-таблицы;
		//    у настоящего костюма Кролика 75541 текст кролика остался. Путь — та же простая подсказка, что в игре (RU и EN).
		const FAsdaItemTooltipText Tk = Tip->BuildTooltipSimple(75521, 1, 0, M);
		Check(Tk.bValid && (Tk.Body.Contains(TEXT("огурцоид")) || Tk.Body.Contains(TEXT("Pickle"))) && !Tk.Body.Contains(TEXT("Кролик")),
			FString::Printf(TEXT("подсказка 75521 — о жетоне огурцоида: «%s»"), *Tk.Body.Replace(TEXT("\n"), TEXT(" | ")).Left(160)));
		const FAsdaItemTooltipText Tr = Tip->BuildTooltipSimple(75541, 1, 0, M);
		// ★ 11.11.2026 (D4, разбор красных эталона 7.6.0): 05.11.2026 (booster-loot, решение владельца 26.10, вариант Б — имя коробки
		//   по фактическому составу, Tools/_out/itemdb_overlay/item_name_overlay.csv) 75541 в русских данных стал «5 комб. камней
		//   ур. A (редкие)» с тем же описанием. Ожидание «текст кролика остался» устарело; проверяется, что описание у 75541 своё.
		Check(Tr.bValid && !Tr.Body.IsEmpty() && !Tr.Body.Contains(TEXT("Кролик")),
			FString::Printf(TEXT("подсказка 75541 — описание по составу, без заглушки костюма Кролика: «%s»"), *Tr.Body.Replace(TEXT("\n"), TEXT(" | ")).Left(160)));

		// 6) [ИСПРАВЛЕНИЕ] 29.10.2026 (баг «Blacksmith Vision Scroll in John's shop»): витрина лавки — только то, что продаёт
		//    сервер. Решение владельца: продажа включена на сервере (item_table кол.38) у 72727/72728 (Джон), 23088/79372 (Ян),
		//    75457/75458 (стражи 202–204); ивентовые (Бумибуми 131, «Персонаж события» 163/183, 61 товар) скрыты ведомостью
		//    Tools/data/npc_shop_hidden.csv.
		static const TCHAR* kShopDt = TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcShopList.DT_NpcShopList");
		const FAsdaNpcShopRow* John = AsdaDt::RowByInt<FAsdaNpcShopRow>(kShopDt, 4, TEXT("SelfTest"));
		Check(John && John->ItemCodes.Contains(72727) && John->ItemCodes.Contains(72728) && John->ItemCodes.Num() == 143,
			FString::Printf(TEXT("лавка Джона: товаров %d (ждём 143), свиток остроты зрения 7/8 на витрине"), John ? John->ItemCodes.Num() : -1));
		const FAsdaNpcShopRow* Yan = AsdaDt::RowByInt<FAsdaNpcShopRow>(kShopDt, 3, TEXT("SelfTest"));
		Check(Yan && Yan->ItemCodes.Contains(23088) && Yan->ItemCodes.Contains(79372) && Yan->ItemCodes.Num() == 144,
			FString::Printf(TEXT("лавка Яна: товаров %d (ждём 144), с 23088/79372"), Yan ? Yan->ItemCodes.Num() : -1));
		int32 Hidden = 0;
		for (const int32 Npc : { 131, 163, 183 })
		{
			const FAsdaNpcShopRow* R = AsdaDt::RowByInt<FAsdaNpcShopRow>(kShopDt, Npc, TEXT("SelfTest"));
			Hidden += (R && R->ItemCodes.Num() == 0) ? 1 : 0;
		}
		const FAsdaNpcShopRow* Guard = AsdaDt::RowByInt<FAsdaNpcShopRow>(kShopDt, 202, TEXT("SelfTest"));
		Check(Hidden == 3 && Guard && Guard->ItemCodes.Contains(75457) && Guard->ItemCodes.Contains(75458),
			FString::Printf(TEXT("ивентовые лавки 131/163/183 пусты: %d из 3; страж 202 продаёт бур и заряд"), Hidden));

		// 7) [ИСПРАВЛЕНИЕ] 31.10.2026: купоны СС 79374–79378 премиум-витрины — категория 3 («премиум», как у остальных 420 товаров
		//    витрины), без стопки. Сервер `CharChargeShopBuy@0x511DA0` продаёт через витрину только +36 ∈ {0,3} (было 1 → отказ);
		//    item_table на сервере поправлен так же (кол.23 = 0, кол.24 = 3). Контейнер по правилу клиента (тип ≤ 9 или кат. 3) —
		//    сумка, по правилу сервера (+7 == 4 && +36 == 1 ⇒ расходники, иначе сумка) — тоже сумка.
		int32 CouponsOk = 0;
		for (int32 C = 79374; C <= 79378; ++C)
		{
			const FAsdaItemRecord* R = IT->GetItem(C);
			if (R && R->CategoryFlag == 3 && R->TradableFlag == 0 && (R->ItemType <= 9 || R->CategoryFlag == 3)) { ++CouponsOk; }
		}
		Check(CouponsOk == 5, FString::Printf(TEXT("купоны СС 79374–79378: категория 3, без стопки, сумка — %d из 5"), CouponsOk));

		AsdaTestNet::SelfLevel(Net) = WasLevel;
		AsdaSelfTestKit::Finish(W, kCmd, Args.bQuit, LogAsdaItemLevelGateTest, TEXT("[уровень предмета]"), Ok, Fail);
	}));
#endif // ASDA_DEV_CONSOLE
