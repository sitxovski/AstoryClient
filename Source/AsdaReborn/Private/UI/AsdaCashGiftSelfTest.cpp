// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ПОДАРОК ИЗ КЭШ-МАГАЗИНА» P101_CashGift (01.11.2026): `asda.SelfTest.CashGift [delay=N] [quit]`. Офлайн (-AsdaNoNet).
//   Что проверяет (реверс — `Reports/cash_gift/re/client.md`, проект — `PROJECT_P101.md` §5):
//   A. отправка 5072/5074 НАСТОЯЩИМИ отправителями под перехватом: опкод, длина тела (20 / 187), паддинг, конверт 'S' («с секретом»),
//      тело побайтно против раскладки §2 (имя cp1251 + NUL; режим, таблица, коды, пары опций {−1, 0}, маршрут +176…+186);
//      гейты: выключатель 0, 12919 (окно обмена), [2078]/[2079] «в полёте», «себе» 12946, мало GXP 6778, имя ≠ подтверждённому 12939;
//   B. приём 5073 (коды 0…5 и неизвестный 6): строка по таблице §4, имя и маршрут сохранены только кодом 1, [2078] снят;
//      5075 (0/1/4): строка, [2079] снят, баланс кода 1 — общей точкой 5467;
//   C. 5078 (1 и 7 позиций): записи в сумку по ячейке +5 (контейнер 1), запись поверх занятой ячейки, вес u16 (P97, > 32767),
//      значок 3 + клик → 6886; Д-К2 (при своей отправке пакет принимается; ключ 0 — выброшен);
//   D. путь игрока: окно 6000 — кнопка «Подарок» карточки, панель 6296 и 6333 (геометрия после отрисовки), «Проверить» → 5072,
//      5073 → «Подарок» → модалка 1559 → ОК → 5074, 5075 → панели закрыты; Д-К3 (поле 6345); окно 15300 — кнопка «Подарок»,
//      15520 → 5072, OK 15527 → 1559 → 5074 с количеством; тексты кнопок по языку сессии.
//   Состояние (сумка, ключи, перехват, «свой персонаж») возвращается. Итог: «ПОДАРОК-P101 ИТОГ: ok=N fail=M».
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
#include "UI/AsdaCashGiftSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaPremiumShopSubsystem.h"
#include "UI/AsdaBoosterShopSubsystem.h"
#include "UI/AsdaPurchaseBoxSubsystem.h"
#include "UI/AsdaTradeStateSubsystem.h"
#include "UI/AsdaEquipShopSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCashGiftTest, Log, All);

namespace AsdaCashGiftTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.CashGift");   // имя команды
	static const TCHAR* kSelfName = TEXT("ДарительП101");       // имя своего персонажа офлайн-спавна
	static const TCHAR* kRcpt = TEXT("Получатель");             // имя получателя

	// Перехваченная отправка.
	struct FSent { int32 Op = 0; TArray<uint8> Body; bool bPad = false; TCHAR Env = 0; };

	// Контекст прогона (живёт до последнего шага по таймерам).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                    // пройдено / провалено
		TWeakObjectPtr<UWorld> W;                  // мир прогона
		bool bQuit = false;                        // выйти после итога
		uint32 CapGen = 0;                         // поколение перехвата
		TArray<FSent> Sent;                        // перехваченные отправки
		TUniquePtr<FAsdaNetSelfScope> Self;        // «свой персонаж» сети — вернуть в конце
		TArray<TPair<int32, TArray<uint8>>> SavedCells;   // ячейки сумки, которые трогал тест (−1 код — была пустой)
		int32 EqCard = -1, FnCard = -1;            // карточки 6000: item_list / special_itemlist
		int32 EqCat = -1, FnCat = -1;              // их вкладки 58200
		int32 LastBalance = -1;                    // баланс, пришедший общей точкой 5467
		FDelegateHandle HBal;                      // подписка на 5467

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaCashGiftTest, Log,   TEXT("ПОДАРОК-P101 ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaCashGiftTest, Error, TEXT("ПОДАРОК-P101 FAIL %s"), *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
		// Последняя перехваченная отправка опкода Op (nullptr — не было).
		const FSent* Last(int32 Op) const { for (int32 i = Sent.Num() - 1; i >= 0; --i) { if (Sent[i].Op == Op) { return &Sent[i]; } } return nullptr; }
		int32 Count(int32 Op) const { int32 N = 0; for (const FSent& S : Sent) { N += (S.Op == Op) ? 1 : 0; } return N; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Тело 5073 (34 б): код, маршрут (Channel 3, Portal 1, Gsidx 2, Threadidx 4, Job 5, Lvl 77, Userindex 0x01020304, Charidx 2, Usernum 0x1234), имя.
	static TArray<uint8> Body5073(uint8 Code, const FString& Name)
	{
		TArray<uint8> B; B.SetNumZeroed(34);
		B[0] = Code; B[1] = 3; B[2] = 1; B[3] = 2; B[4] = 4; B[5] = 5; B[6] = 77;
		AsdaBytes::WrI32(&B[7], 0x01020304); B[11] = 2; AsdaBytes::WrI16(&B[12], 0x1234);
		FAsdaLoginData::EncodeCp1251(Name, &B[14], 20);
		return B;
	}
	// Тело 5075 (6 б).
	static TArray<uint8> Body5075(uint8 Code, int32 Balance)
	{
		TArray<uint8> B; B.SetNumZeroed(6); B[0] = Code; B[1] = 9; AsdaBytes::WrI32(&B[2], Balance); return B;
	}
	// Тело 5077 (61 б): код.
	static TArray<uint8> Body5077(uint8 Code)
	{
		TArray<uint8> B; B.SetNumZeroed(61); B[0] = Code; return B;
	}
	// Подать пакет через штатный разбор сети и сразу разобрать очередь подарка (то, что сделает Tick).
	static void Feed(const FCtxRef& C, int32 Op, const TArray<uint8>& Body)
	{
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { Net->DebugFeedPacket(Op, Body); }
		if (UAsdaCashGiftSubsystem* G = C->Sub<UAsdaCashGiftSubsystem>()) { G->DebugPump(); }
	}
	// Код i-й записи item_list / special_itemlist (для подставных подарков).
	static int32 PickItem(const UAsdaItemTableSubsystem* IT, int32 Skip)
	{
		int32 N = 0;
		// Снаряжение (слот 0…9) с ценой — настоящий товар `item_list`, не валюта 2055x.
		for (const FAsdaItemRecord& R : IT->GetAllItems()) { if (R.ItemCode > 0 && R.Price > 0 && R.ItemType >= 0 && R.ItemType <= 9) { if (N++ == Skip) { return R.ItemCode; } } }
		return 0;
	}
	static int32 PickSpecial(const UAsdaItemTableSubsystem* IT, int32 Skip)
	{
		int32 N = 0;
		for (int32 c = 1; c <= 700; ++c)
		{
			FAsdaSpecialItemlistRow Sp;
			if (IT->IsSpecialItemCode(c) && IT->GetSpecialItem(c, Sp)) { if (N++ == Skip) { return c; } }
		}
		return 0;
	}

	static void CloseMsgBoxes(const FCtxRef& C);

	static void Finish(const FCtxRef& C)
	{
		// ★ R5-fix-b 13.11.2026 (заключительный заход, шестая запись): убрать за собой окна-сообщения 1500 — иначе модалка доставалась следующему тесту пачки
		//   (`SelfTest.ChannelMoveStop` 16/1 на RU, `r5/fix/a/REPORT6.md` п. 2).
		CloseMsgBoxes(C);
		if (UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>())
		{
			for (const TPair<int32, TArray<uint8>>& P : C->SavedCells)
			{
				if (P.Value.Num() >= FAsdaItemInstance::kRecordSize) { Inv->ApplyExternalRecord(P.Value, UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
				else { Inv->ClearCell(1, P.Key); }
			}
		}
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>())
		{
			if (C->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, C->CapGen); C->CapGen = 0; }
			Net->OnPremiumBalance.Remove(C->HBal);
		}
		if (UAsdaCashGiftSubsystem* G = C->Sub<UAsdaCashGiftSubsystem>()) { G->ResetSessionState(EAsdaSessionResetReason::Disconnected); }
		if (UAsdaHotbarSubsystem* Hb = C->Sub<UAsdaHotbarSubsystem>()) { Hb->RemoveNoticeIcon(3); }
		if (C->Self) { C->Self->Restore(); C->Self.Reset(); }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaCashGiftTest, TEXT("ПОДАРОК-P101"), C->Ok, C->Fail);
	}

	// Строка дампа, начинающаяся (после отступа) с Prefix; пусто — нет.
	static FString LineOf(const FString& Dump, const TCHAR* Prefix)
	{
		TArray<FString> Lines; Dump.ParseIntoArrayLines(Lines);
		for (const FString& L : Lines) { if (L.TrimStart().StartsWith(Prefix)) { return L; } }
		return FString();
	}

	// Закрыть окна-сообщения 1500, оставшиеся от прошлых шагов: игрок закрыл бы их сам, а открытая модалка по эталону
	//   глушит кнопки окна 6000 (LABEL_53 → 12933).
	static void CloseMsgBoxes(const FCtxRef& C)
	{
		if (UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>()) { WM->ClearMsgBoxQueue(kCmd); }
	}

	// Через Sec секунд — следующий шаг (геометрия появляется только после отрисовки кадра).
	static void After(const FCtxRef& C, float Sec, TFunction<void(const FCtxRef&)> Fn)
	{
		UWorld* W = C->W.Get();
		if (!W) { Finish(C); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C, Fn]() { Fn(C); }), Sec, false);
	}

	// ═══ A + B + C: сеть и приём ═══
	static void StageNet(const FCtxRef& C)
	{
		UAsdaCashGiftSubsystem* G = C->Sub<UAsdaCashGiftSubsystem>();
		UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		UAsdaTradeStateSubsystem* TS = C->Sub<UAsdaTradeStateSubsystem>();
		if (!G || !IT || !Inv || !Net) { C->Expect(false, TEXT("подсистемы подарка/таблиц/сумки/сети есть")); Finish(C); return; }
		const int32 ItemA = PickItem(IT, 0), ItemB = PickItem(IT, 1), SpA = PickSpecial(IT, 0);
		C->Expect(ItemA > 0 && ItemB > 0 && SpA > 0, FString::Printf(TEXT("A коды: item_list %d/%d, special %d"), ItemA, ItemB, SpA));
		const FAsdaItemRecord* RecA = IT->GetItem(ItemA);
		AsdaCVarSave::SetForTest(TEXT("asda.CashGift.FixK2"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.CashGift.FixK3"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.CashGift.FixK5"), 1);

		// A0 выключатель 0 — ничего не уходит.
		AsdaCVarSave::SetForTest(TEXT("asda.CashGift.Enable"), 0);
		C->Sent.Reset();
		const bool bOff = G->SendCheckName(EAsdaGiftSource::Shop6000, kRcpt);
		C->Expect(!bOff && C->Sent.Num() == 0, TEXT("A0 asda.CashGift.Enable 0: 5072 не уходит"));
		AsdaCVarSave::SetForTest(TEXT("asda.CashGift.Enable"), 1);

		// A1 5072 из 6000: 20 б, паддинг, конверт «с секретом», тело = cp1251 + NUL.
		G->ResetSessionState(EAsdaSessionResetReason::Disconnected);
		const bool b1 = G->SendCheckName(EAsdaGiftSource::Shop6000, kRcpt);
		const FSent* S1 = C->Last(5072);
		uint8 Want[20]; FMemory::Memzero(Want, 20); FAsdaLoginData::EncodeCp1251(kRcpt, Want, 20);
		C->Expect(b1 && S1 && S1->Body.Num() == 20 && S1->bPad && S1->Env == TEXT('S') && FMemory::Memcmp(S1->Body.GetData(), Want, 20) == 0,
			FString::Printf(TEXT("A1 5072: ушёл=%d тело=%d б пад=%d конверт=%c имя побайтно"), b1 ? 1 : 0, S1 ? S1->Body.Num() : -1, S1 && S1->bPad ? 1 : 0, S1 ? S1->Env : TEXT('-')));
		// A2 [2078] — второй раз молча.
		const int32 N5072 = C->Count(5072);
		G->SendCheckName(EAsdaGiftSource::Shop6000, kRcpt);
		C->Expect(G->IsWaitingCheck() && C->Count(5072) == N5072, TEXT("A2 [2078] взведён: повторная проверка молча"));
		// A3 окно обмена → 12919.
		G->DebugClearFlags();
		if (TS) { TS->SetExchangeWindowOpen(true, kCmd); }
		const bool b3 = G->SendCheckName(EAsdaGiftSource::Shop6000, kRcpt);
		C->Expect(!b3 && G->DebugLastLoc() == 12919 && C->Count(5072) == N5072, FString::Printf(TEXT("A3 окно обмена 2008: 12919 (loc %d), не ушёл"), G->DebugLastLoc()));
		if (TS) { TS->SetExchangeWindowOpen(false, kCmd); }
		// A3б (ревизия С-3) рюкзак открыт (`sub_93248D(g_pInventoryWnd)`) → 12919.
		G->DebugClearFlags();
		Inv->OpenWindow();
		const bool b3b = Inv->IsWindowOpen() && G->SendCheckName(EAsdaGiftSource::Shop6000, kRcpt);
		C->Expect(Inv->IsWindowOpen() && !b3b && G->DebugLastLoc() == 12919 && C->Count(5072) == N5072,
			FString::Printf(TEXT("A3б С-3 рюкзак открыт: 12919 (loc %d), не ушёл"), G->DebugLastLoc()));
		Inv->CloseWindow();
		// A3в (ревизия С-3) персонаж занят / магазин 6800 — предикаты общей точки (состояние офлайн: свободен, 6800 закрыт).
		C->Expect(AsdaShopCanInteract(G), TEXT("A3в общая точка `AsdaShopCanInteract`: всё закрыто, персонаж свободен → можно"));
		// A4 15300 (Д-К5): «себе» → 12946.
		const bool b4 = G->SendCheckName(EAsdaGiftSource::Booster15300, kSelfName);
		C->Expect(!b4 && G->DebugLastLoc() == 12946, FString::Printf(TEXT("A4 Д-К5 15300 «себе»: 12946 (loc %d)"), G->DebugLastLoc()));
		// A4б Д-К5 ключ 0 — как оригинал: у 15300 ни «себе», ни флага [2078].
		AsdaCVarSave::SetForTest(TEXT("asda.CashGift.FixK5"), 0);
		G->DebugClearFlags();
		const int32 N4 = C->Count(5072);
		const bool b4b = G->SendCheckName(EAsdaGiftSource::Booster15300, kSelfName);
		C->Expect(b4b && C->Count(5072) == N4 + 1 && !G->IsWaitingCheck(), TEXT("A4б Д-К5 (ключ 0): 15300 шлёт и «себе», [2078] не ставит (оригинал)"));
		AsdaCVarSave::SetForTest(TEXT("asda.CashGift.FixK5"), 1);
		G->DebugClearFlags();

		// ═══ B: 5073 ═══
		const int32 Codes[] = { 0, 2, 3, 4, 5 };
		const int32 Locs[]  = { 6876, 20040, 6879, 6880, 12946 };
		for (int32 i = 0; i < 5; ++i)
		{
			Feed(C, 5073, Body5073(1, kRcpt));                     // сначала подтвердить
			Feed(C, 5073, Body5073((uint8)Codes[i], kRcpt));
			C->Expect(G->DebugLastLoc() == Locs[i] && G->GetConfirmedName().IsEmpty() && !G->IsWaitingCheck(),
				FString::Printf(TEXT("B 5073 код %d → loc %d (есть %d), имя стёрто"), Codes[i], Locs[i], G->DebugLastLoc()));
		}
		Feed(C, 5073, Body5073(1, kRcpt));
		uint8 R13[13]; G->DebugGetRoute(R13);
		C->Expect(G->DebugLastLoc() == 20070 && G->GetConfirmedName() == kRcpt && R13[0] == 3 && R13[1] == 1 && R13[2] == 2 && R13[3] == 4
			&& R13[4] == 5 && AsdaBytes::RdI32(&R13[6]) == 0x01020304 && R13[10] == 2 && AsdaBytes::RdI16(&R13[11]) == 0x1234,
			FString::Printf(TEXT("B 5073 код 1 → 20070 «%s», имя «%s», маршрут сохранён"), *AsdaNotice::LocRaw(C->GI(), 20070), *G->GetConfirmedName()));
		const int32 LocBefore = G->DebugLastLoc();
		Feed(C, 5073, Body5073(6, kRcpt));
		C->Expect(G->DebugLastLoc() == LocBefore && G->GetConfirmedName() == kRcpt, TEXT("B 5073 код 6 — молча (`default: return`), имя цело"));

		// ═══ A5: 5074 из 6000 — тело побайтно ═══
		C->Sent.Reset();
		const int32 PriceA = RecA ? RecA->Price : 0;
		const bool bLow = G->SendPresent(EAsdaGiftSource::Shop6000, ItemA, 1, PriceA - 1);
		C->Expect(!bLow && G->DebugLastLoc() == 6778 && C->Count(5074) == 0, FString::Printf(TEXT("A5 мало GXP (%d < %d): 6778, не ушёл"), PriceA - 1, PriceA));
		const bool b5 = G->SendPresent(EAsdaGiftSource::Shop6000, ItemA, 1, PriceA);
		const FSent* S5 = C->Last(5074);
		bool bBody = S5 && S5->Body.Num() == 187;
		if (bBody)
		{
			const uint8* P = S5->Body.GetData();
			bBody = P[0] == 1 && P[1] == 1 && AsdaBytes::RdI32(P + 8) == ItemA;
			for (int32 k = 1; k < 7; ++k) { bBody = bBody && P[1 + k] == 0 && AsdaBytes::RdI32(P + 8 + 4 * k) == -1; }
			for (int32 j = 0; j < 35; ++j) { bBody = bBody && AsdaBytes::RdI16(P + 36 + 4 * j) == -1 && AsdaBytes::RdI16(P + 38 + 4 * j) == 0; }
			bBody = bBody && P[176] == 3 && P[177] == 1 && P[178] == 2 && P[179] == 4 && AsdaBytes::RdI32(P + 180) == 0x01020304
				&& P[184] == 2 && AsdaBytes::RdI16(P + 185) == 0x1234;
		}
		C->Expect(b5 && bBody && S5->bPad && S5->Env == TEXT('S') && G->IsWaitingPresent() && G->IsPresentSent(),
			FString::Printf(TEXT("A5 5074 из 6000: 187 б, +0=1 +1=1 +8=%d, 6×(0/−1), 35 пар {−1,0}, маршрут +176…+186; [2079] и [2072] взведены"), ItemA));
		G->SendPresent(EAsdaGiftSource::Shop6000, ItemA, 1, PriceA);
		C->Expect(C->Count(5074) == 1, TEXT("A6 [2079]: повторный подарок молча"));
		// A6б (ревизия С-1) Д-К2: 5078 во время своей отправки не сбрасывает её — имя, [2072] целы; 5075 код 1 → 20071 с именем.
		TArray<int32> FreeS;
		for (int32 s2 = 0; s2 < Inv->GetContainerSize(1) && FreeS.Num() < 1; ++s2) { if (Inv->GetCellItemCode(1, s2) <= 0) { FreeS.Add(s2); } }
		if (FreeS.Num() == 1)
		{
			C->SavedCells.Emplace(FreeS[0], TArray<uint8>());
			TArray<uint8> G1; G1.SetNumZeroed(421);
			FAsdaLoginData::EncodeCp1251(TEXT("Отправитель"), G1.GetData(), 20);
			const TArray<uint8> R1 = FAsdaItemRecordWriter().Code(ItemB).Container(1).Slot(FreeS[0]).Second(-1).Amount(1).ToArray();
			FMemory::Memcpy(&G1[20], R1.GetData(), 57);
			Feed(C, 5078, G1);
			C->Expect(Inv->GetCellItemCode(1, FreeS[0]) == ItemB && G->IsPresentSent() && G->GetConfirmedName() == kRcpt,
				TEXT("A6б С-1 5078 при [2072]: вещь положена, имя получателя и [2072] сохранены"));
			Feed(C, 5075, Body5075(1, 777));
			C->Expect(G->DebugLastLoc() == 20071 && G->DebugLastText().Contains(kRcpt),
				FString::Printf(TEXT("A6б С-1 затем 5075 код 1 → 20071 с именем: «%s»"), *G->DebugLastText()));
			Inv->ClearCell(1, FreeS[0]);
		}
		// A6в (ревизия С-1) 5077 при [2072] → 20071 с именем; 5077 без [2072] — молча.
		Feed(C, 5073, Body5073(1, kRcpt));
		G->DebugClearFlags();
		G->SendPresent(EAsdaGiftSource::Shop6000, ItemA, 1, PriceA);
		Feed(C, 5077, Body5077(1));
		C->Expect(G->DebugLastLoc() == 20071 && G->DebugLastText().Contains(kRcpt), FString::Printf(TEXT("A6в 5077 код 1 при [2072] → 20071 «%s»"), *G->DebugLastText()));
		const int32 L77 = G->DebugLastLoc();
		Feed(C, 5077, Body5077(2));
		C->Expect(G->DebugLastLoc() == L77, TEXT("A6в 5077 без [2072] — молча (как эталон)"));
		// A6г выключатель 0 — 5074 не уходит.
		Feed(C, 5073, Body5073(1, kRcpt));
		G->DebugClearFlags();
		AsdaCVarSave::SetForTest(TEXT("asda.CashGift.Enable"), 0);
		const int32 N74 = C->Count(5074);
		const bool bOff4 = G->SendPresent(EAsdaGiftSource::Shop6000, ItemA, 1, PriceA);
		C->Expect(!bOff4 && C->Count(5074) == N74, TEXT("A6г asda.CashGift.Enable 0: 5074 не уходит"));
		AsdaCVarSave::SetForTest(TEXT("asda.CashGift.Enable"), 1);
		// A6д (ревизия Н-9) 15300 с количеством 0 — пакет без позиций (как эталон).
		const bool bQ0 = G->SendPresent(EAsdaGiftSource::Booster15300, SpA, 0, 99999999, kRcpt);
		const FSent* SQ0 = C->Last(5074);
		C->Expect(bQ0 && SQ0 && AsdaBytes::RdI32(&SQ0->Body[8]) == -1 && SQ0->Body[1] == 0, TEXT("A6д 15300 количество 0 → 5074 без позиций (+8 = −1)"));
		G->DebugClearFlags();
		Feed(C, 5073, Body5073(1, kRcpt));

		// ═══ B: 5075 ═══
		C->HBal = Net->OnPremiumBalance.AddLambda([C](uint8 St, int32 Bal) { if (St == 1) { C->LastBalance = Bal; } });
		Feed(C, 5075, Body5075(1, 4321));
		C->Expect(G->DebugLastLoc() == 20071 && !G->IsWaitingPresent() && C->LastBalance == 4321 && G->GetConfirmedName().IsEmpty(),
			FString::Printf(TEXT("B 5075 код 1 → 20071 «%s», баланс 4321 общей точкой 5467 (%d), [2079] снят, сброс окна"),
				*AsdaNotice::LocSlot(C->GI(), 20071, kRcpt), C->LastBalance));
		Feed(C, 5075, Body5075(0, 0));
		C->Expect(G->DebugLastLoc() == 6891, FString::Printf(TEXT("B 5075 код 0 → 6891 (%d)"), G->DebugLastLoc()));
		Feed(C, 5075, Body5075(4, 0));
		C->Expect(G->DebugLastLoc() == 1216, FString::Printf(TEXT("B 5075 код 4 → 1216 «%s»"), *AsdaNotice::LocRaw(C->GI(), 1216)));

		// ═══ A7: 5074 из 15300 — 7 позиций спецпредмета; имя поля ≠ подтверждённому → 12939 ═══
		Feed(C, 5073, Body5073(1, kRcpt));
		C->Sent.Reset();
		const bool b7bad = G->SendPresent(EAsdaGiftSource::Booster15300, SpA, 7, 99999999, TEXT("Чужой"));
		C->Expect(!b7bad && G->DebugLastLoc() == 12939 && C->Count(5074) == 0, TEXT("A7 15300 имя поля ≠ подтверждённому: 12939, не ушёл"));
		const bool b7 = G->SendPresent(EAsdaGiftSource::Booster15300, SpA, 7, 99999999, kRcpt);
		const FSent* S7 = C->Last(5074);
		bool b7Body = S7 && S7->Body.Num() == 187;
		for (int32 k = 0; b7Body && k < 7; ++k) { b7Body = S7->Body[1 + k] == 0 && AsdaBytes::RdI32(&S7->Body[8 + 4 * k]) == SpA; }
		C->Expect(b7 && b7Body, FString::Printf(TEXT("A7 5074 из 15300: 7 позиций special (+1…+7 = 0, коды %d)"), SpA));
		G->DebugClearFlags();

		// ═══ C: 5078 ═══
		TArray<int32> Free;
		for (int32 s = 0; s < Inv->GetContainerSize(1) && Free.Num() < 8; ++s) { if (Inv->GetCellItemCode(1, s) <= 0) { Free.Add(s); } }
		C->Expect(Free.Num() >= 8, FString::Printf(TEXT("C свободных ячеек сумки %d (нужно 8)"), Free.Num()));
		if (Free.Num() < 8) { Finish(C); return; }
		for (const int32 s : Free) { C->SavedCells.Emplace(s, TArray<uint8>()); }
		auto Gift421 = [&](const TArray<int32>& Codes, const TArray<int32>& Slots, int32 Weight)
		{
			TArray<uint8> B; B.SetNumZeroed(421);
			FAsdaLoginData::EncodeCp1251(TEXT("Отправитель"), B.GetData(), 20);
			for (int32 i = 0; i < Codes.Num() && i < 7; ++i)
			{
				const TArray<uint8> R = FAsdaItemRecordWriter().Code(Codes[i]).Container(2).Slot(Slots[i]).Second(-1).Amount(1 + i).ToArray();
				FMemory::Memcpy(&B[20 + 57 * i], R.GetData(), 57);
			}
			AsdaBytes::WrI16(&B[419], Weight);
			return B;
		};
		UAsdaHotbarSubsystem* Hb = C->Sub<UAsdaHotbarSubsystem>();
		if (Hb) { Hb->RemoveNoticeIcon(3); }
		// C1 одна позиция, вес 40000 (u16 > 32767).
		Feed(C, 5078, Gift421({ ItemA }, { Free[0] }, 40000));
		C->Expect(Inv->GetCellItemCode(1, Free[0]) == ItemA && G->DebugLastPlaced() == 1 && Net->SelfWeightCur == 40000 && G->DebugLastLoc() == 5060,
			FString::Printf(TEXT("C1 5078 1 позиция: ячейка %d = %d (Plate 2 в записи — всё равно сумка), вес %d (ждём 40000, P97 u16), 5060"),
				Free[0], Inv->GetCellItemCode(1, Free[0]), Net->SelfWeightCur));
		C->Expect(Hb && Hb->HasNoticeIcon(3), TEXT("C1 значок 3 `mes_present` висит"));
		if (Hb) { Hb->HandleNoticeIconClick(3); }
		C->Expect(G->DebugLastLoc() == 6886 && Hb && !Hb->HasNoticeIcon(3), FString::Printf(TEXT("C1 клик по значку 3 → 6886 «%s», значок снят"), *AsdaNotice::LocRaw(C->GI(), 6886)));
		// C2 семь позиций (item_list и special вперемешку), вторая запись — поверх занятой ячейки Free[0] (запись по +5, слияния нет).
		TArray<int32> Codes7 = { ItemB, SpA, ItemA, SpA, ItemB, ItemA, SpA };
		TArray<int32> Slots7 = { Free[0], Free[1], Free[2], Free[3], Free[4], Free[5], Free[6] };
		Feed(C, 5078, Gift421(Codes7, Slots7, 1234));
		bool bAll = G->DebugLastPlaced() == 7;
		for (int32 i = 0; i < 7; ++i) { bAll = bAll && Inv->GetCellItemCode(1, Slots7[i]) == Codes7[i]; }
		const FAsdaItemInstance* Cell1 = Inv->GetCell(1, Free[1]);
		C->Expect(bAll && Cell1 && Cell1->Amount == 2 && Net->SelfWeightCur == 1234,
			FString::Printf(TEXT("C2 5078 7 позиций: все по своим ячейкам (+5), ячейка %d перезаписана кодом %d, количество из записи (2), вес 1234"),
				Free[0], ItemB));
		// C3 Д-К2: при своей отправке ([2072]) пакет принимается; ключ 0 — выброшен (как оригинал).
		G->DebugSetSent(true);
		Feed(C, 5078, Gift421({ ItemA }, { Free[7] }, 0));
		C->Expect(Inv->GetCellItemCode(1, Free[7]) == ItemA, TEXT("C3 Д-К2 (ключ 1): 5078 при [2072] принят"));
		Inv->ClearCell(1, Free[7]);
		AsdaCVarSave::SetForTest(TEXT("asda.CashGift.FixK2"), 0);
		G->DebugSetSent(true);
		Feed(C, 5078, Gift421({ ItemA }, { Free[7] }, 0));
		C->Expect(Inv->GetCellItemCode(1, Free[7]) <= 0 && G->DebugLastPlaced() == 0, TEXT("C3 Д-К2 (ключ 0): 5078 при [2072] выброшен (оригинал)"));
		AsdaCVarSave::SetForTest(TEXT("asda.CashGift.FixK2"), 1);
		G->DebugClearFlags();
		if (Hb) { Hb->RemoveNoticeIcon(3); }
	}

	// ═══ D: путь игрока — окно 6000 ═══
	static void StageShop6000(const FCtxRef& C)
	{
		UAsdaPremiumShopSubsystem* Prem = C->Sub<UAsdaPremiumShopSubsystem>();
		UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
		if (!Prem || !IT) { C->Expect(false, TEXT("D подсистема окна 6000 есть")); Finish(C); return; }
		CloseMsgBoxes(C);
		Prem->OpenWindow(false);
		Prem->DebugSetGxp(99999999);
		// Карточки item_list и special_itemlist — перебор категорий витрины (как клики игрока по вкладкам 58200).
		for (int32 Cat = 0; Cat < UAsdaPremiumShopSubsystem::kCatTabs && (C->EqCard < 0 || C->FnCard < 0); ++Cat)
		{
			Prem->SetCategory(Cat);
			for (int32 i = 0; i < UAsdaPremiumShopSubsystem::kCards; ++i)
			{
				const int32 Code = Prem->DebugCardCode(i);
				if (Code <= 0) { continue; }
				FAsdaSpecialItemlistRow Sp;
				if (C->EqCard < 0 && IT->GetItem(Code)) { C->EqCard = i; C->EqCat = Cat; }
				if (C->FnCard < 0 && !IT->GetItem(Code) && IT->GetSpecialItem(Code, Sp)) { C->FnCard = i; C->FnCat = Cat; }
			}
			if (C->EqCard >= 0 && C->FnCard < 0) { break; }   // спецпредметы — на другой вкладке; ищем ниже отдельно
		}
		C->Expect(C->EqCard >= 0, FString::Printf(TEXT("D 6000: карточка item_list %d"), C->EqCard));
		if (C->EqCard < 0) { Finish(C); return; }
		// D0 (ревизия С-3) гейт 5070 — та же общая точка: окно обмена открыто → чат 12919, модалки и 5070 нет.
		Prem->SetCategory(C->EqCat);
		if (UAsdaTradeStateSubsystem* TS0 = C->Sub<UAsdaTradeStateSubsystem>())
		{
			C->Sent.Reset();
			TS0->SetExchangeWindowOpen(true, kCmd);
			Prem->OnCardBuyClicked(C->EqCard);
			UAsdaPurchaseBoxSubsystem* Bx0 = C->Sub<UAsdaPurchaseBoxSubsystem>();
			C->Expect(Bx0 && !Bx0->IsOpen() && C->Count(5070) == 0, TEXT("D0 С-3 покупка 5070 при открытом обмене: отказ общей точкой (чат 12919), модалки нет"));
			TS0->SetExchangeWindowOpen(false, kCmd);
		}
		// D0а (повтор вторым методом, 02.11): 5070 при открытом рюкзаке — эталон отказывает (`+0x210` = `m_bOpened` окна 400:
		//   пишут только `uiOpenInventoryWindow` = 1 и `sowelWindowClose` = 0), при закрытом — модалка 1559 открывается.
		if (UAsdaInventorySubsystem* Inv0 = C->Sub<UAsdaInventorySubsystem>())
		{
			UAsdaPurchaseBoxSubsystem* Bx1 = C->Sub<UAsdaPurchaseBoxSubsystem>();
			Inv0->OpenWindow();
			Prem->OnCardBuyClicked(C->EqCard);
			const bool bRefused = Inv0->IsWindowOpen() && Bx1 && !Bx1->IsOpen() && !Prem->IsGiftPanelShown();
			Inv0->CloseWindow();
			Prem->OnCardBuyClicked(C->EqCard);
			if (Prem->IsPanelBuy()) { Prem->OnGiftGiveClicked(false); }   // ★ 06.11.2026: карточка с вариантами идёт в 1559 через панель 6296
			const bool bAllowed = Bx1 && Bx1->IsOpen();
			if (Bx1 && Bx1->IsOpen()) { Bx1->OnCancelClicked(); }
			Prem->CloseGiftPanels();
			C->Expect(bRefused && bAllowed && C->Count(5070) == 0, TEXT("D0а 5070: рюкзак открыт → отказ 12919 (как эталон); рюкзак закрыт → модалка 1559"));
		}
		// D0б выключатель 0 — кнопки «Подарок» карточек скрыты.
		AsdaCVarSave::SetForTest(TEXT("asda.CashGift.Enable"), 0);
		Prem->SetCategory(C->EqCat);
		const bool bHidden = LineOf(Prem->DumpGiftPanels(), TEXT("Подарок-карточки-0")).Contains(TEXT("vis=0"));
		AsdaCVarSave::SetForTest(TEXT("asda.CashGift.Enable"), 1);
		Prem->SetCategory(C->EqCat);
		C->Expect(bHidden || Prem->DebugCardCode(0) <= 0, TEXT("D0б asda.CashGift.Enable 0: кнопка «Подарок» карточки скрыта"));
		Prem->SetCategory(C->EqCat);
		Prem->OnCardGiftClicked(C->EqCard);
		After(C, 1.2f, [](const FCtxRef& C2)
		{
			UAsdaPremiumShopSubsystem* P = C2->Sub<UAsdaPremiumShopSubsystem>();
			UAsdaCashGiftSubsystem* G = C2->Sub<UAsdaCashGiftSubsystem>();
			UAsdaPurchaseBoxSubsystem* Box = C2->Sub<UAsdaPurchaseBoxSubsystem>();
			if (!P || !G || !Box) { Finish(C2); return; }
			const FString Dump = P->DumpGiftPanels();
			UE_LOG(LogAsdaCashGiftTest, Log, TEXT("ПОДАРОК-P101 геометрия 6296 (путь игрока):\n%s"), *Dump);
			const FString Btn = AsdaNotice::Loc(C2->GI(), 6874);
			C2->Expect(P->IsGiftPanelShown() && Dump.Contains(TEXT("корень SHELLITEM_EQUIP_WIN_grp vis=1")) && Dump.Contains(TEXT("поле-имени SHELL_EQUIP_NINPUT vis=1"))
				&& Dump.Contains(TEXT("Проверить SHELL_EQUIP_NCONFIRM vis=1")) && Dump.Contains(FString::Printf(TEXT("текст=«%s»"), *Btn)),
				FString::Printf(TEXT("D1 панель 6296 показана: поле 6311, «Проверить» 6312, «Подарок» 6319 = loc 6874 «%s»"), *Btn));
			const FString RootLine = LineOf(Dump, TEXT("корень SHELLITEM_EQUIP_WIN_grp"));
			const FString FieldLine = LineOf(Dump, TEXT("поле-имени SHELL_EQUIP_NINPUT"));
			C2->Expect(!RootLine.IsEmpty() && !RootLine.Contains(TEXT(" 0x")) && !FieldLine.Contains(TEXT(" 0x")) && !FieldLine.Contains(TEXT("x0)")),
				FString::Printf(TEXT("D1 корень 6296 и поле 6311 имеют экранный размер: «%s» / «%s»"), *RootLine.TrimStartAndEnd(), *FieldLine.TrimStartAndEnd()));
			if (P->DebugCardCode(0) > 0)
			{
				C2->Expect(LineOf(Dump, TEXT("Подарок-карточки-0")).Contains(TEXT("vis=1")), TEXT("D1 кнопка «Подарок» карточки 0 видна (ключ подарка 1)"));
			}
			// D2 «Проверить» со своим именем → 12946, поле 6311 очищено.
			C2->Sent.Reset();
			P->DebugSetGiftName(kSelfName);
			P->OnGiftCheckClicked();
			C2->Expect(C2->Count(5072) == 0 && LineOf(P->DumpGiftPanels(), TEXT("поле-имени SHELL_EQUIP_NINPUT")).EndsWith(TEXT("текст=«»")),
				TEXT("D2 6312 «себе» → чат 12946, поле 6311 очищено, 5072 не ушёл"));
			// D3 имя получателя → 5072; 5073 код 1; «Подарок» 6319 → 1559 → ОК → 5074 с кодом панели.
			C2->Sent.Reset();
			P->DebugSetGiftName(kRcpt);
			P->OnGiftCheckClicked();
			const FSent* S = C2->Last(5072);
			C2->Expect(S && S->Body.Num() == 20, TEXT("D3 6312 → 5072 (20 б)"));
			Feed(C2, 5073, Body5073(1, kRcpt));
			CloseMsgBoxes(C2);   // ONEBUT 20070 — игрок нажал «ОК»
			P->OnGiftGiveClicked(false);
			C2->Expect(Box->IsOpen(), TEXT("D3 6319 при подтверждённом имени → модалка 1559"));
			Box->OnConfirmClicked();
			const FSent* S4 = C2->Last(5074);
			C2->Expect(S4 && S4->Body.Num() == 187 && AsdaBytes::RdI32(&S4->Body[8]) == P->GetGiftItemCode() && S4->Body[1] == 1,
				FString::Printf(TEXT("D3 ОК 1561 → 5074, код панели %d (+1 = item_list)"), P->GetGiftItemCode()));
			Feed(C2, 5075, Body5075(1, 1000));
			C2->Expect(!P->IsGiftPanelShown(), TEXT("D3 5075 код 1 → сброс окна: панели 6296/6333 закрыты"));
			CloseMsgBoxes(C2);   // ONEBUT 20071
			// D4 спецпредмет → 6333; Д-К3 «себе» чистит 6345.
			int32 Fn = C2->FnCard;
			if (Fn >= 0) { P->SetCategory(C2->FnCat); }
			UAsdaItemTableSubsystem* IT2 = C2->Sub<UAsdaItemTableSubsystem>();
			for (int32 Cat = 0; Fn < 0 && Cat < UAsdaPremiumShopSubsystem::kCatTabs; ++Cat)
			{
				P->SetCategory(Cat);
				for (int32 i = 0; i < UAsdaPremiumShopSubsystem::kCards && Fn < 0; ++i)
				{
					const int32 Code = P->DebugCardCode(i);
					FAsdaSpecialItemlistRow Sp;
					if (Code > 0 && IT2 && !IT2->GetItem(Code) && IT2->GetSpecialItem(Code, Sp)) { Fn = i; C2->FnCat = Cat; }
				}
			}
			C2->FnCard = Fn;
			C2->Expect(Fn >= 0, FString::Printf(TEXT("D4 6000: карточка special_itemlist %d"), Fn));
			if (Fn < 0) { Finish(C2); return; }
			P->OnCardGiftClicked(Fn);
			After(C2, 1.2f, [](const FCtxRef& C3)
			{
				UAsdaPremiumShopSubsystem* P3 = C3->Sub<UAsdaPremiumShopSubsystem>();
				UAsdaCashGiftSubsystem* G3 = C3->Sub<UAsdaCashGiftSubsystem>();
				if (!P3 || !G3) { Finish(C3); return; }
				const FString Dump = P3->DumpGiftPanels();
				UE_LOG(LogAsdaCashGiftTest, Log, TEXT("ПОДАРОК-P101 геометрия 6333 (путь игрока):\n%s"), *Dump);
				C3->Expect(Dump.Contains(TEXT("корень SHELLITEM_FUNC_WIN_grp vis=1")) && Dump.Contains(TEXT("поле-имени SHELL_FUNC_NINPUT vis=1"))
					&& Dump.Contains(TEXT("Проверить SHELL_FUNC_NCONFIRM vis=1")) && Dump.Contains(TEXT("иконка SHELL_FUNC_ICON_968_0 vis=1")),
					TEXT("D4 панель 6333 показана: поле 6345, «Проверить» 6346, иконка"));
				C3->Expect(Dump.Contains(TEXT("Подарок-карточки-0")) && !Dump.Contains(TEXT("Подарок-карточки-0: НЕТ УЗЛА")),
					TEXT("D4 кнопка «Подарок» карточки найдена"));
				C3->Sent.Reset();
				P3->DebugSetGiftName(kSelfName);
				P3->OnGiftCheckClicked();
				const FString D2 = P3->DumpGiftPanels();
				C3->Expect(C3->Count(5072) == 0 && LineOf(D2, TEXT("поле-имени SHELL_FUNC_NINPUT")).EndsWith(TEXT("текст=«»")),
					TEXT("D4 Д-К3 (ключ 1): «себе» в 6345 → чат 12946, поле 6345 очищено, 5072 не ушёл"));
				AsdaCVarSave::SetForTest(TEXT("asda.CashGift.FixK3"), 0);
				P3->DebugSetGiftName(kSelfName);
				P3->OnGiftCheckClicked();
				C3->Expect(LineOf(P3->DumpGiftPanels(), TEXT("поле-имени SHELL_FUNC_NINPUT")).Contains(kSelfName),
					TEXT("D4 Д-К3 (ключ 0): как оригинал — чистится 6311, имя в 6345 осталось"));
				AsdaCVarSave::SetForTest(TEXT("asda.CashGift.FixK3"), 1);
				// D4б «Проверить» 6346 → 5072, 5073 код 1, «Подарок» 6353 → 1559 → ОК → 5074 с кодом спецпредмета (+1 = 0).
				P3->DebugSetGiftName(kRcpt);
				P3->OnGiftCheckClicked();
				C3->Expect(C3->Count(5072) == 1, TEXT("D4б 6346 → 5072"));
				Feed(C3, 5073, Body5073(1, kRcpt));
				CloseMsgBoxes(C3);
				if (UAsdaPurchaseBoxSubsystem* Bx = C3->Sub<UAsdaPurchaseBoxSubsystem>())
				{
					P3->OnGiftGiveClicked(true);
					const bool bOpenBox = Bx->IsOpen();
					Bx->OnConfirmClicked();
					const FSent* S6 = C3->Last(5074);
					// ★ 06.11.2026 (решение владельца): подарок = покупка себе — в теле код выдачи карточки; если он другой, это коробка item_list (+1 = 1).
					const int32 Wire = P3->GiftWireCode(P3->GetGiftItemCode());
					const uint8 WantTab = Wire != P3->GetGiftItemCode() ? 1 : 0;
					C3->Expect(bOpenBox && S6 && AsdaBytes::RdI32(&S6->Body[8]) == Wire && S6->Body[1] == WantTab,
						FString::Printf(TEXT("D4б 6353 → 1559 → ОК → 5074, карточка %d, код %d (+1 = %d)"), P3->GetGiftItemCode(), Wire, (int32)WantTab));
				}
				G3->DebugClearFlags();
				P3->CloseGiftPanels();
				C3->Expect(!P3->IsGiftPanelShown(), TEXT("D4 «Отмена» 6356 → панели закрыты"));
				P3->CloseWindow();
				// ═══ D5: окно 15300 ═══
				UAsdaBoosterShopSubsystem* Bs = C3->Sub<UAsdaBoosterShopSubsystem>();
				if (!Bs) { C3->Expect(false, TEXT("D5 подсистема 15300 есть")); Finish(C3); return; }
				Bs->OpenWindow();
				Bs->DebugSetGxp(99999999);
				After(C3, 1.0f, [](const FCtxRef& C4)
				{
					UAsdaBoosterShopSubsystem* B4 = C4->Sub<UAsdaBoosterShopSubsystem>();
					UAsdaCashGiftSubsystem* G4 = C4->Sub<UAsdaCashGiftSubsystem>();
					UAsdaPurchaseBoxSubsystem* Box4 = C4->Sub<UAsdaPurchaseBoxSubsystem>();
					if (!B4 || !G4 || !Box4) { Finish(C4); return; }
					B4->OnCellGiftClicked(0);
					After(C4, 0.8f, [](const FCtxRef& C5)
					{
						UAsdaBoosterShopSubsystem* B5 = C5->Sub<UAsdaBoosterShopSubsystem>();
						UAsdaCashGiftSubsystem* G5 = C5->Sub<UAsdaCashGiftSubsystem>();
						UAsdaPurchaseBoxSubsystem* Box5 = C5->Sub<UAsdaPurchaseBoxSubsystem>();
						if (!B5 || !G5 || !Box5) { Finish(C5); return; }
						const FString Geo = B5->DebugGiftGeometry();
						UE_LOG(LogAsdaCashGiftTest, Log, TEXT("ПОДАРОК-P101 геометрия 15300 (путь игрока):\n%s"), *Geo);
						C5->Expect(B5->DebugPurMode() == 2 && Geo.Contains(TEXT("Подарок-ячейки-0 BOOSTER_ITEM_PRESENT00 vis=1")) && Geo.Contains(TEXT("vis=1")),
							TEXT("D5 15300: кнопка «Подарок» ячейки 0 видна, панель в режиме 2"));
						C5->Sent.Reset();
						G5->DebugClearFlags();
						B5->DebugSetGiftName(kRcpt);
						B5->OnPurConfirmClicked();
						C5->Expect(C5->Count(5072) == 1 && G5->IsWaitingCheck(), TEXT("D5 15520 → 5072; Д-К5: [2078] взведён"));
						// D5а (ревизия С-2) сервер промолчал: «Отмена» 15530 снимает [2078] окна 15300 — проверка снова уходит.
						B5->OnPurCancelClicked();
						C5->Expect(!G5->IsWaitingCheck(), TEXT("D5а С-2 сервер молчит → «Отмена» 15530 снимает [2078]"));
						B5->OnCellGiftClicked(0);
						B5->DebugSetGiftName(kRcpt);
						B5->OnPurConfirmClicked();
						C5->Expect(C5->Count(5072) == 2, TEXT("D5а после сброса панели 15520 снова шлёт 5072"));
						// D5б (ревизия Н-1) поле 15519 — не больше 20 символов.
						B5->DebugSetGiftName(TEXT("АБВГДЕЁЖЗИЙКЛМНОПРСТУФХ"));
						C5->Expect(LineOf(B5->DebugGiftGeometry(), TEXT("поле-15519")).Contains(TEXT("«АБВГДЕЁЖЗИЙКЛМНОПРСТ»")), TEXT("D5б поле 15519: LimitText 20"));
						B5->DebugSetGiftName(kRcpt);
						// D5в Д-К1 ключ 0 — модалка 15300 в режиме «купить» (PanelMode 0), как оригинал.
						Feed(C5, 5073, Body5073(1, kRcpt));
						AsdaCVarSave::SetForTest(TEXT("asda.CashGift.FixK1"), 0);
						B5->OnPurOkClicked();
						C5->Expect(Box5->IsOpen() && Box5->DebugRequest().PanelMode == 0, TEXT("D5в Д-К1 (ключ 0): 1559 из 15300 — режим покупки (12923), как оригинал"));
						Box5->OnCancelClicked();
						AsdaCVarSave::SetForTest(TEXT("asda.CashGift.FixK1"), 1);
						Feed(C5, 5073, Body5073(1, kRcpt));
						B5->SetQuantity(3);
						B5->OnPurOkClicked();
						C5->Expect(Box5->IsOpen() && Box5->DebugRequest().PanelMode == 1 && Box5->DebugRequest().RecipientName == kRcpt,
							TEXT("D5 15527 в режиме подарка → модалка 1559, Д-К1 (ключ 1): «Кому» = подтверждённое имя"));
						Box5->OnConfirmClicked();
						const FSent* S = C5->Last(5074);
						int32 N = 0;
						if (S) { for (int32 k = 0; k < 7; ++k) { N += AsdaBytes::RdI32(&S->Body[8 + 4 * k]) > 0 ? 1 : 0; } }
						C5->Expect(S && N == 3 && B5->DebugPurMode() == 0 && G5->IsWaitingPresent(),
							FString::Printf(TEXT("D5 ОК 1561 → 5074 с 3 позициями (%d), панель сброшена, Д-К5: [2079] взведён"), N));
						// D5г (ревизия С-2) сервер промолчал на 5074: закрытие окна 15300 снимает [2079]/[2072].
						B5->CloseWindow();
						C5->Expect(!G5->IsWaitingPresent() && !G5->IsPresentSent(), TEXT("D5г С-2 закрытие окна 15300 снимает [2079]/[2072]"));
						// D5д выключатель 0 — кнопка 15307 скрыта.
						AsdaCVarSave::SetForTest(TEXT("asda.CashGift.Enable"), 0);
						B5->OpenWindow();
						After(C5, 0.6f, [](const FCtxRef& C6)
						{
							UAsdaBoosterShopSubsystem* B6 = C6->Sub<UAsdaBoosterShopSubsystem>();
							if (B6)
							{
								C6->Expect(LineOf(B6->DebugGiftGeometry(), TEXT("Подарок-ячейки-0")).Contains(TEXT("vis=0")), TEXT("D5д asda.CashGift.Enable 0: кнопка 15307 скрыта"));
								B6->CloseWindow();
							}
							AsdaCVarSave::SetForTest(TEXT("asda.CashGift.Enable"), 1);
							Finish(C6);
						});
					});
				});
			});
		});
	}

	static void Start(const FCtxRef& C)
	{
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		if (!Net) { C->Expect(false, TEXT("подсистема сети есть")); Finish(C); return; }
		C->Self = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		C->CapGen = AsdaSelfTestKit::ArmCapture(Net, [C](uint16 Op, const uint8* P, int32 Len, bool bPad, TCHAR Env)
		{
			FSent& S = C->Sent.AddDefaulted_GetRef();
			S.Op = Op; S.bPad = bPad; S.Env = Env;
			if (P && Len > 0) { S.Body.Append(P, Len); }
		}, /*OnlyOpcode*/0, kCmd);
		C->Expect(C->CapGen != 0, TEXT("перехват отправок поставлен"));
		if (!C->CapGen) { Finish(C); return; }
		const UAsdaWorldSubsystem* WS = C->Sub<UAsdaWorldSubsystem>();
		C->Expect(WS && WS->GetSelfCharName() == kSelfName, FString::Printf(TEXT("своё имя офлайн-спавна «%s»"), WS ? *WS->GetSelfCharName() : TEXT("-")));
		StageNet(C);
		if (C->Fail > 0 && C->Ok == 0) { Finish(C); return; }
		StageShop6000(C);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestCashGift(
	TEXT("asda.SelfTest.CashGift"),
	TEXT("P101_CashGift 01.11.2026: подарок из кэш-магазина — 5072/5074 побайтно под перехватом и гейты, приём 5073/5075/5078 "
	     "(строки, маршрут, сумка, вес u16, значок 3 → 6886), Д-К2/Д-К3/Д-К5, путь игрока по окнам 6000 (6296/6333) и 15300. Офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaCashGiftTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		// Сумка, хотбар и окна живут при своём персонаже в мире — офлайн его нет: штатный фейковый спавн с именем и повтор через 2 с.
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
		static const uint16 kGuardOps[] = { 5072, 5074 };   // опкоды самотеста в окне отказа «онлайн»
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA, 0.f, MakeArrayView(kGuardOps))) { return; }
		if (!W) { return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KA.bQuit;
		if (KA.Delay > 0.f)
		{
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]() { Start(C); }), KA.Delay, false);
		}
		else
		{
			Start(C);
		}
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
