// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ГЕЙТА 7007 «ОГРАНИЧИВАЮЩЕЕ СОСТОЯНИЕ» (05.11.2026). [ОРИГИНАЛ]
//   Состояния своего персонажа накладываются НАСТОЯЩИМИ пакетами 5272 и снимаются пакетами 5268 через штатный приёмник
//   (`UAsdaNetSubsystem::DebugFeedPacket`); отправки 5028/5000 перехватываются (`AsdaSelfTestKit::ArmCapture`, в сеть ничего).
//   Входы — те же точки, что зовут жесты игрока: окно сумки (`UseFromBagWindow` — двойной щелчок по ячейке), панель
//   быстрого доступа (`UseByDoubleClick(2, …)` — мост хотбара `B.UseByDoubleClick`), надевание (`EquipItem` — мост хотбара
//   `B.UseFromBag` и перенос на слот).
//     A. Для КАЖДОГО из восьми состояний 0,1,2,9,3,14,4,8: расходник из окна сумки и с панели — отказ, плашка 7007 цветом
//        −11514, 5028 не ушёл; надевание (окно сумки / панель / перенос) — 5000 УШЁЛ (у оригинала гейта там нет).
//     B. Состояние не из списка (5 «Отравление» и т. п.) — расходник применяется.
//     C. Снятие 5268 — снова применяется.  D. Истечение по времени (5272 на 2 с, ждём 4,6 с) — снова применяется.
//     E. Коды 1…2000 гейт пропускает; ключ `asda.Item.StateLockGate 0` — прежнее поведение (5028 уходит).
// Консоль: `asda.SelfTest.ItemStateLock [quit]`. Офлайн (`-AsdaNoNet`), карта мира. Итог — «СОСТОЯНИЕ-7007 ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Items/AsdaItemUseStateGate.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaNoticeColors.h"
#include "Text/AsdaLang.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaStateLockTest, Log, All);

namespace AsdaStateLockTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.ItemStateLock");   // имя команды
	static const TCHAR* kTag = TEXT("СОСТОЯНИЕ-7007");                // префикс строк журнала
	static constexpr int32 kSpendCell = 40;   // расходники 40 — проверочный расходник
	static constexpr int32 kBagCell   = 20;   // сумка 20 — проверочная броня

	// Контекст прогона.
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                    // пройдено / провалено
		TWeakObjectPtr<UWorld> W;                  // мир
		bool bQuit = false;                        // выйти после итога
		uint32 CapGen = 0;                         // поколение перехвата
		TArray<int32> SentOps;                     // перехваченные опкоды
		int32 Potion = 0;                          // расходник (itemType 11) вне 1…2000
		int32 Armor = 0;                           // броня (тип 0…4)
		int32 Self = -1;                           // свой userIdx в списке состояний
		TArray<uint8> SavedSpend, SavedBag;        // ячейки до прогона
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaStateLockTest, Log,   TEXT("%s ok   %s"), kTag, *What); }
			else   { ++Fail; UE_LOG(LogAsdaStateLockTest, Error, TEXT("%s FAIL %s"), kTag, *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
		int32 Count(int32 Op) const { int32 N = 0; for (int32 O : SentOps) { N += (O == Op); } return N; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	using AsdaSelfTestCtx::At;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// 5272 «наложить состояние» своему персонажу: i16 userIdx, u16 id, raw: +6 i16 флаг, +9 стек, +10 вид, +11 i16 остаток, +13 байт9.
	static void FeedState(const FCtxRef& C, int32 Id, int32 DurSec)
	{
		TArray<uint8> B; B.SetNumZeroed(16);
		B[0] = (uint8)(C->Self & 0xFF); B[1] = (uint8)((C->Self >> 8) & 0xFF);
		B[2] = (uint8)Id; B[3] = 0;
		B[9] = 1; B[10] = 0;
		B[11] = (uint8)(DurSec & 0xFF); B[12] = (uint8)((DurSec >> 8) & 0xFF);
		C->Sub<UAsdaNetSubsystem>()->DebugFeedPacket(5272, B, 0);
	}
	// 5268 «снять состояние».
	static void FeedRemove(const FCtxRef& C, int32 Id)
	{
		TArray<uint8> B; B.SetNumZeroed(4);
		B[0] = (uint8)(C->Self & 0xFF); B[1] = (uint8)((C->Self >> 8) & 0xFF);
		B[2] = (uint8)Id;
		C->Sub<UAsdaNetSubsystem>()->DebugFeedPacket(5268, B, 0);
	}
	// Снять «ждём ответ на действие с предметом» штатными ответами: 5029 / 5001 с кодом, который оригинал игнорирует молча.
	static void ClearPending(const FCtxRef& C)
	{
		TArray<uint8> U; U.SetNumZeroed(24); U[0] = 99;
		C->Sub<UAsdaNetSubsystem>()->DebugFeedPacket(5029, U, 0);
		TArray<uint8> M; M.SetNumZeroed(24); M[0] = 99;
		C->Sub<UAsdaNetSubsystem>()->DebugFeedPacket(5001, M, 0);
	}
	static int32 PlankCount(const FCtxRef& C, FColor* OutColor)
	{
		const FString Txt = AsdaNotice::LocRaw(C->GI(), 7007, false);
		UAsdaChatSubsystem* Ch = C->Sub<UAsdaChatSubsystem>();
		return (Ch && !Txt.IsEmpty()) ? Ch->DebugCountWarLines(Txt, OutColor) : -1;
	}

	static bool PickData(const FCtxRef& C)
	{
		UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
		if (!IT) { return false; }
		static const TSet<int32> kSkipSub = { 8, 9, 12, 15, 16, 32, 33, 34, 38, 64, 106, 107, 200, 201, 202, 203, 204, 205, 206, 208, 209, 210 };   // 210 — рыба (окно сумки открывает книгу рыбака)
		for (int32 Code = 2001; Code <= 79999 && (!C->Potion || !C->Armor); ++Code)
		{
			const FAsdaItemRecord* R = IT->GetItem(Code);
			if (!R) { continue; }
			if (!C->Potion && R->ItemType == 11 && R->LinkCode > 0 && R->LinkCode != 101 && R->LinkCode != 102 && !kSkipSub.Contains(R->ItemSubType) && !IT->IsBoosterItem(Code)
				&& !(R->ItemSubType >= 0x2C && R->ItemSubType <= 0x35) && R->TooltipFlag200 != 1)
			{
				C->Potion = Code;
			}
			if (!C->Armor && R->ItemType >= 0 && R->ItemType <= 4 && R->TooltipFlag200 != 1 && R->CategoryFlag != 3) { C->Armor = Code; }
		}
		return C->Potion && C->Armor;
	}

	static void Finish(const FCtxRef& C)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		for (int32 Id : { 0, 1, 2, 9, 3, 14, 4, 8, 5 }) { FeedRemove(C, Id); }
		ClearPending(C);
		AsdaCVarSave::SetForTest(TEXT("asda.Item.StateLockGate"), 1);
		if (Inv)
		{
			if (C->SavedSpend.Num() >= 57) { Inv->ApplyExternalRecord(C->SavedSpend, UAsdaInventorySubsystem::EExternalRecordMode::AsIs); } else { Inv->ClearCell(2, kSpendCell); }
			if (C->SavedBag.Num() >= 57) { Inv->ApplyExternalRecord(C->SavedBag, UAsdaInventorySubsystem::EExternalRecordMode::AsIs); } else { Inv->ClearCell(1, kBagCell); }
		}
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { if (C->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, C->CapGen); C->CapGen = 0; } }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaStateLockTest, kTag, C->Ok, C->Fail);
	}

	// Одна проба по четырём путям, КАЖДЫЙ путь — своим шагом таймера: перед ним штатные ответы 5029/5001 снимают
	//   «ждём ответ на действие с предметом» (их разбор отложен до Tick инвентаря), затем — сам вход. bLocked — ждём отказ.
	struct FPath { const TCHAR* Name; int32 Cont; bool bBagWindow; bool bEquipDirect; int32 Op; bool bGated; };
	static const FPath kPaths[] = {
		{ TEXT("расходник из окна сумки"),              2, true,  false, 5028, true  },
		{ TEXT("расходник с панели быстрого доступа"),  2, false, false, 5028, true  },
		{ TEXT("надеть двойным щелчком из окна сумки"), 1, true,  false, 5000, false },
		{ TEXT("надеть с панели / переносом на слот"),  1, false, true,  5000, false },
	};
	static constexpr float kStep = 0.25f;                 // шаг между путями
	static constexpr float kProbeLen = 4 * kStep + 0.1f;  // длительность пробы

	static void Probe(const FCtxRef& C, float T0, const FString& Step, bool bLocked)
	{
		for (int32 i = 0; i < 4; ++i)
		{
			const FPath P = kPaths[i];
			At(C, T0 + i * kStep, [C]() { ClearPending(C); });
			At(C, T0 + i * kStep + 0.12f, [C, P, Step, bLocked]()
			{
				UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
				const int32 Cell = (P.Cont == 2) ? kSpendCell : kBagCell;
				const int32 N0 = C->Count(P.Op);
				const int32 R0 = AsdaItemUseStateGate::GetRefusalCount();
				FColor Col = FColor::Black;
				const int32 L0 = PlankCount(C, &Col);
				if (P.bEquipDirect) { Inv->EquipItem(1, Cell); }
				else if (P.bBagWindow) { Inv->UseFromBagWindow(P.Cont, Cell); }
				else { Inv->UseByDoubleClick(P.Cont, Cell); }
				const int32 N1 = C->Count(P.Op);
				const int32 R1 = AsdaItemUseStateGate::GetRefusalCount();
				const int32 L1 = PlankCount(C, &Col);
				if (bLocked && P.bGated)
				{
					C->Expect(N1 == N0 && R1 == R0 + 1 && L1 >= 1 && Col == AsdaNoticeColor::Gold,
						FString::Printf(TEXT("%s %s: ОТКАЗ — %d не ушёл (%d→%d), отказов 7007 %d→%d, на плашке 221 «%s» цветом −11514 (строк %d→%d)"),
							*Step, P.Name, P.Op, N0, N1, R0, R1, *AsdaNotice::LocRaw(C->GI(), 7007, false), L0, L1));
				}
				else
				{
					C->Expect(N1 == N0 + 1 && R1 == R0, FString::Printf(TEXT("%s %s: %d ушёл (%d→%d)%s"), *Step, P.Name, P.Op, N0, N1,
						bLocked ? TEXT(" — у оригинала здесь гейта нет") : TEXT("")));
				}
			});
		}
	}

	static void Run(const FCtxRef& C)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		UAsdaEffectsSubsystem* Fx = C->Sub<UAsdaEffectsSubsystem>();
		if (!Inv || !Net || !Fx || !C->Sub<UAsdaChatSubsystem>()) { C->Expect(false, TEXT("подсистемы есть")); Finish(C); return; }
		C->Self = Fx->SelfUserIdx();
		if (!PickData(C)) { C->Expect(false, TEXT("данные: расходник и броня")); Finish(C); return; }
		C->Expect(C->Self >= 0, FString::Printf(TEXT("данные: свой userIdx %d, расходник %d, броня %d, язык %s"),
			C->Self, C->Potion, C->Armor, FAsdaLang::Code(FAsdaLang::Current())));
		TWeakPtr<FCtx> WC(C);
		C->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WC](uint16 Op, const uint8*, int32, bool, TCHAR)
		{
			if (TSharedPtr<FCtx> X = WC.Pin()) { X->SentOps.Add((int32)Op); }
		}, 0, kCmd);
		if (!C->CapGen) { C->Expect(false, TEXT("перехват свободен")); Finish(C); return; }
		if (const FAsdaItemInstance* X = Inv->GetCell(2, kSpendCell)) { if (!X->IsEmpty()) { C->SavedSpend = X->Raw; } }
		if (const FAsdaItemInstance* X = Inv->GetCell(1, kBagCell)) { if (!X->IsEmpty()) { C->SavedBag = X->Raw; } }
		Inv->ApplyExternalRecord(FAsdaItemRecordWriter::Cell(C->Potion, 2, kSpendCell, 50), UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
		Inv->ApplyExternalRecord(FAsdaItemRecordWriter::Cell(C->Armor, 1, kBagCell, 1), UAsdaInventorySubsystem::EExternalRecordMode::AsIs);

		float T = 0.3f;
		Probe(C, T, TEXT("0 без состояний —"), false); T += kProbeLen;
		// A/C. Восемь состояний по одному: наложить 5272 → отказ → снять 5268 → снова можно.
		static const int32 kIds[] = { 0, 1, 2, 9, 3, 14, 4, 8 };
		for (int32 Id : kIds)
		{
			At(C, T, [C, Id]() { FeedState(C, Id, 600); });
			At(C, T + 0.1f, [C, Id]() { C->Expect(AsdaItemUseStateGate::IsSelfStateLocked(C->GI()), FString::Printf(TEXT("A%d 5272 состояние %d наложено — персонаж ограничен"), Id, Id)); });
			Probe(C, T + 0.15f, FString::Printf(TEXT("A%d"), Id), true);
			T += 0.15f + kProbeLen;
			At(C, T, [C, Id]() { FeedRemove(C, Id); });
			At(C, T + 0.1f, [C, Id]() { C->Expect(!AsdaItemUseStateGate::IsSelfStateLocked(C->GI()), FString::Printf(TEXT("C%d 5268 сняло состояние %d"), Id, Id)); });
			Probe(C, T + 0.15f, FString::Printf(TEXT("C%d после снятия —"), Id), false);
			T += 0.15f + kProbeLen;
		}
		// B. Состояние не из списка эталона.
		At(C, T, [C]() { FeedState(C, 5, 600); });
		At(C, T + 0.1f, [C]() { C->Expect(!AsdaItemUseStateGate::IsSelfStateLocked(C->GI()), TEXT("B состояние 5 (не из списка эталона) не ограничивает")); });
		Probe(C, T + 0.15f, TEXT("B состояние 5 —"), false);
		T += 0.15f + kProbeLen;
		At(C, T, [C]() { FeedRemove(C, 5); FeedState(C, 3, 2); });   // D: оглушение на 2 с
		Probe(C, T + 0.15f, TEXT("D1 оглушение на 2 с —"), true);
		// ★ 06.11.2026: остаток 2 уходит в −1 через 3 с (покадровый `TickDecrement`), снимает `UpdateExpired` раз в секунду, как эталон
		//   (`entity_char.c:7681`, `> 0x3E8`). Снятие приходится на 3…4 с, прежняя отметка 3,6 с попадала внутрь этого окна.
		At(C, T + 2.8f, [C]() { C->Expect(AsdaItemUseStateGate::IsSelfStateLocked(C->GI()), TEXT("D2 через 2,8 с состояние ещё действует")); });
		T += 4.6f;
		At(C, T, [C]() { C->Expect(!AsdaItemUseStateGate::IsSelfStateLocked(C->GI()), TEXT("D2 через 4,6 с состояние истекло само (таймер клиента)")); });
		Probe(C, T + 0.1f, TEXT("D3 после истечения —"), false);
		T += 0.1f + kProbeLen;
		// E. Коды 1…2000 и ключ отката.
		At(C, T, [C]() { FeedState(C, 8, 600); ClearPending(C); });
		At(C, T + 0.15f, [C]()
		{
			C->Expect(!AsdaItemUseStateGate::RefuseIfStateLocked(C->GI(), 500, TEXT("самотест")), TEXT("E1 код 500 (из 1…2000) гейт пропускает, как оригинал"));
			AsdaCVarSave::SetForTest(TEXT("asda.Item.StateLockGate"), 0);
			const int32 N0 = C->Count(5028);
			C->Sub<UAsdaInventorySubsystem>()->UseFromBagWindow(2, kSpendCell);
			C->Expect(C->Count(5028) == N0 + 1, TEXT("E2 КОНТРОЛЬ: ключ asda.Item.StateLockGate 0 — прежний порт, 5028 уходит под сном"));
			AsdaCVarSave::SetForTest(TEXT("asda.Item.StateLockGate"), 1);
			FeedRemove(C, 8);
		});
		T += 0.4f;
		At(C, T, [C]() { Finish(C); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestItemStateLock(
	TEXT("asda.SelfTest.ItemStateLock"),
	TEXT("Самотест гейта 7007: состояния 5272/5268 своего персонажа, расходник окна сумки и панели, надевание. Офлайн. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaStateLockTest;
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
