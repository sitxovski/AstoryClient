// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «СНЯТИЕ ПРЕДЕЛА ПИТОМЦА» `asda.Pet.SelfTestLimitBreak` (07.11.2026, баг игрока «окно лимита не видит зелье»). [ПОРТ]
//   Путь игрока без сети: настоящий 6106 через приёмник → ПКМ в окне 43000 → кнопка 43055 → окно 44200 → кнопка 44232 →
//   ответ на нотис или вопрос 71 → перехват тела 6148 → настоящий ответ 6149 через приёмник питомцев.
//   Ожидания заданы числами сервера (`RecvClient_PetMaxLevelUp@0x45A2F0`: +0 uid, +4 ячейка, +6 контейнер, +7 ячейка талисмана),
//   а не вызовом искателя окна: прежний стенд брал ожидание из него самого и дефекта не видел.
//   T  данные: 23237 имеет linkCode 207, 399 — подтип 207, 404 — подтип 209; имена и нотис 109937 на языке сессии.
//   1  ключ `asda.Pet.LimitBreakPotionLinkField 0`: воспроизведение дефекта, 6148 не уходит;
//   2  зелье в ячейке 40: уходит контейнер 2, ячейка 40, ячейка очищается ответом;
//   3  зелье и талисман в ячейке 42 сумки ×2; 4  только талисман: 6148 нет;
//   5  только свиток 399: контейнер 1; 6  ключ `asda.Pet.LimitBreakScroll 0`: 6148 нет; 7  зелье и свиток: тратится зелье;
//   8  уровень ниже предела: 6148 нет; 9  уровень 10: окно не открывается; 10  питомец призван: окно не открывается.
// Консоль: `asda.Pet.SelfTestLimitBreak [delay=N] [quit]`. Только офлайн (`-AsdaNoNet`).
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestKit.h"              // каркас: аргументы, отказ, ключи, итог, выход
#include "Test/AsdaSelfTestRunGate.h"   // FAsdaSelfTestRunGate — отложенный старт   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaNetSubsystem.h"                  // DebugFeedPacket, SelectedCharId
#include "AsdaBytes.h"                         // поля тел 6106/6148/6149
#include "Login/AsdaLoginData.h"               // EncodeCp1251 — кличка в телах
#include "Items/AsdaItemTableSubsystem.h"      // записи предметов и спецпредметов
#include "Pet/AsdaPetSubsystem.h"
#include "Text/AsdaLang.h"                     // язык сессии
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaNotice.h"                     // текст нотиса по номеру
#include "UI/AsdaPetCageSubsystem.h"
#include "UI/AsdaPetGrowthSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPetLimitTest, Log, All);

namespace AsdaPetLimitTestLocal
{
	static const TCHAR* kCmd = TEXT("asda.Pet.SelfTestLimitBreak");
	static const TCHAR* kPfx = TEXT("САМОТЕСТ-ПРЕДЕЛ-ПИТОМЦА");
	constexpr int32 kUid = 990088;           // uid питомца стенда
	constexpr int32 kType = 14;              // порода стенда
	constexpr int32 kTestCharId = 77088;     // свой charId, если офлайн его нет
	constexpr int32 kStandCell = 0;          // ячейка окна 43000 с питомцем стенда
	constexpr int32 kPotion = 23237;         // зелье: item_table col 8 = 207, продают NPC 3, 4, 8, 139
	constexpr int32 kScroll = 399;           // свиток: Function_Table подтип 207
	constexpr int32 kCharm = 404;            // талисман: Function_Table подтип 209, витрина премиум-лавки
	constexpr int32 kBody6148 = 9;           // длина тела запроса
	constexpr int32 kBody6149 = 110;         // длина тела ответа
	constexpr double kWaitSec = 1.0;         // потолок ожидания модального окна и перехвата
	constexpr double kQuietSec = 0.4;        // сколько ждать, что 6148 не ушёл

	// Предмет, который кладётся в ячейку на время случая.
	struct FPut { int32 Container; int32 Cell; int32 Code; };

	// Один случай сценария.
	struct FCase
	{
		const TCHAR* Name;                   // подпись в журнале
		TArray<FPut> Items;                  // содержимое контейнеров
		int32 Level;                         // уровень питомца
		int32 MaxLevel;                      // предел питомца
		bool bActive;                        // питомец призван
		int32 KeyLink;                       // ключ asda.Pet.LimitBreakPotionLinkField
		int32 KeyScroll;                     // ключ asda.Pet.LimitBreakScroll
		bool bOpens;                         // окно 44200 должно открыться
		bool bSends;                         // 6148 должен уйти
		int32 Bag;                           // ожидаемый контейнер расходника
		int32 Slot;                          // ожидаемая ячейка расходника
		int32 Premium;                       // ожидаемая ячейка талисмана или −1
	};

	// Шаг случая.
	enum class EPhase : uint8 { WaitModal, WaitSend, WaitQuiet, WaitReply, Done };

	// Состояние прогона.
	struct FState
	{
		TWeakObjectPtr<UWorld> W;              // мир прогона
		bool bQuit = false;                    // выйти по окончании
		int32 Ok = 0, Fail = 0;                // счётчики проверок
		TUniquePtr<FAsdaCVarScope> Cv;         // ключи на время прогона (восстанавливаются)
		FTSTicker::FDelegateHandle Tick;       // тикер сценария
		bool bCapture = false;                 // перехват исходящих взведён этим прогоном
		TArray<TArray<uint8>> Sent;            // перехваченные тела 6148
		TArray<FCase> Cases;                   // сценарий
		int32 Index = 0;                       // текущий случай
		EPhase Phase = EPhase::Done;           // текущий шаг случая
		double T0 = 0.0;                       // начало шага

		// Записать проверку в журнал и счётчик.
		void Expect(bool b, const FString& What)
		{
			b ? ++Ok : ++Fail;
			UE_LOG(LogAsdaPetLimitTest, Warning, TEXT("%s %s %s"), kPfx, b ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), *What);
		}
		// Перейти к шагу случая.
		void Begin(EPhase P) { Phase = P; T0 = FPlatformTime::Seconds(); }
	};

	// Сеть игры.
	static UAsdaNetSubsystem* NetOf(UWorld* W) { return AsdaSelfTestKit::NetOf(W); }
	// Подсистема игры по классу.
	template <class T> static T* SubOf(UWorld* W) { return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<T>() : nullptr; }

	// Настоящее тело 6106: charId, флаг 0, одна запись 46 б (uid, PetRecord 41 б, «призван» +45).
	static void Feed6106(UWorld* W, int32 Level, int32 MaxLevel, bool bActive)
	{
		UAsdaNetSubsystem* Net = NetOf(W);
		TArray<uint8> B; B.SetNumZeroed(5 + 46);
		AsdaBytes::WrI32(B.GetData(), Net->SelectedCharId);
		uint8* R = B.GetData() + 5;
		AsdaBytes::WrI32(R, kUid);
		uint8* P = R + 4;
		AsdaBytes::WrI16(P + 0, kType);
		FAsdaLoginData::EncodeCp1251(TEXT("Bunny"), P + 2, 16);
		P[18] = 90; P[19] = (uint8)Level; P[20] = (uint8)MaxLevel;   // сытость, уровень, предел
		R[45] = bActive ? 1 : 0;
		Net->DebugFeedPacket(6106, B);
	}

	// Настоящее тело 6149 (110 б): uid@0, уровень@23, предел@24, ячейка расходника@45, контейнер@47, код результата@48.
	static void Feed6149(UWorld* W, const FCase& C)
	{
		UAsdaPetSubsystem* P = SubOf<UAsdaPetSubsystem>(W);
		if (!P) { return; }
		TArray<uint8> B; B.SetNumZeroed(kBody6149);
		AsdaBytes::WrI32(B.GetData(), kUid);
		AsdaBytes::WrI16(B.GetData() + 4, kType);
		FAsdaLoginData::EncodeCp1251(TEXT("Bunny"), B.GetData() + 6, 16);
		B[22] = 90;
		B[23] = (uint8)C.Level;
		B[24] = (uint8)(C.MaxLevel + 1);
		AsdaBytes::WrI16(B.GetData() + 45, (int16)C.Slot);
		B[47] = (uint8)C.Bag;
		B[48] = 1;
		P->OnPetPacket(6149, B);
	}

	// Закрыть все модальные окна кнопкой «ОК»; сколько закрыто.
	static int32 PressAllOk(UWorld* W)
	{
		UAsdaWindowSubsystem* WM = SubOf<UAsdaWindowSubsystem>(W);
		int32 N = 0;
		while (WM && WM->IsAnyModalOpen() && N < 6)
		{
			WM->AnswerMsgBox(/*bTwoBut*/false, /*bYes*/true);
			++N;
		}
		return N;
	}

	// Очистить ячейки всех случаев.
	static void ClearCells(TSharedRef<FState> S, UWorld* W)
	{
		UAsdaInventorySubsystem* IV = SubOf<UAsdaInventorySubsystem>(W);
		if (!IV) { return; }
		for (const FCase& C : S->Cases)
		{
			for (const FPut& I : C.Items) { IV->ClearCell(I.Container, I.Cell); }
		}
	}

	// Есть ли в строке кириллица.
	using AsdaSelfTestInput::HasCyrillic;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия отличалась только именами параметров.

	// Текст показан на языке сессии: не пуст, кириллица только в русской сессии.
	static bool LangOk(const FString& T)
	{
		return !T.IsEmpty() && HasCyrillic(T) == FAsdaLang::IsRu();
	}

	// Проверка T: коды таблиц и тексты.
	static void CheckData(TSharedRef<FState> S, UWorld* W)
	{
		UAsdaItemTableSubsystem* IT = SubOf<UAsdaItemTableSubsystem>(W);
		if (!IT) { S->Expect(false, TEXT("T нет таблицы предметов")); return; }
		const FAsdaItemRecord* Rec = IT->GetItem(kPotion);
		S->Expect(Rec && Rec->LinkCode == UAsdaPetGrowthSubsystem::kPotionLinkCode,
			FString::Printf(TEXT("T 23237: linkCode %d (сервер: item_table col 8 = 207)"), Rec ? Rec->LinkCode : -1));
		int32 Potions = 0;
		for (const FAsdaItemRecord& R : IT->GetAllItems()) { Potions += (R.LinkCode == UAsdaPetGrowthSubsystem::kPotionLinkCode) ? 1 : 0; }
		S->Expect(Potions == 1, FString::Printf(TEXT("T записей с linkCode 207: %d (ждали 1, как в item_table)"), Potions));
		FAsdaSpecialItemlistRow Scroll, Charm;
		const bool bScroll = IT->GetSpecialItem(kScroll, Scroll);
		const bool bCharm = IT->GetSpecialItem(kCharm, Charm);
		S->Expect(bScroll && Scroll.SubType == 207, FString::Printf(TEXT("T 399: подтип %d (Function_Table 207)"), bScroll ? Scroll.SubType : -1));
		S->Expect(bCharm && Charm.SubType == UAsdaPetGrowthSubsystem::kTalismanSubType,
			FString::Printf(TEXT("T 404: подтип %d (Function_Table 209)"), bCharm ? Charm.SubType : -1));
		const FString PotionName = IT->GetItemName(kPotion);
		const FString Notice = AsdaNotice::Loc(W->GetGameInstance(), 109937);
		S->Expect(LangOk(PotionName), FString::Printf(TEXT("T имя 23237 на языке сессии: [%s]"), *PotionName));
		S->Expect(LangOk(Scroll.Name), FString::Printf(TEXT("T имя 399 на языке сессии: [%s]"), *Scroll.Name));
		S->Expect(LangOk(Charm.Name), FString::Printf(TEXT("T имя 404 на языке сессии: [%s]"), *Charm.Name));
		S->Expect(LangOk(Notice), FString::Printf(TEXT("T нотис 109937 на языке сессии: [%s]"), *Notice));
	}

	// Перехват исходящих подсистемы питомцев: тела 6148 складываются в состояние.
	static bool ArmCapture(TSharedRef<FState> S, UWorld* W)
	{
		UAsdaNetSubsystem* Net = NetOf(W);
		UAsdaPetSubsystem* Pets = SubOf<UAsdaPetSubsystem>(W);
		if (!Net || !Pets) { return false; }
		TWeakPtr<FState> WS2 = S;
		return Pets->BeginSendCapture([WS2](int32 Op, const TArray<uint8>& Body, bool, TCHAR)
		{
			if (TSharedPtr<FState> St = WS2.Pin())
			{
				if (Op == 6148) { St->Sent.Add(Body); }
			}
		}, Net->SelectedCharId == 0 ? kTestCharId : 0);
	}

	// Завершение: убрать предметы и окна, снять перехват, вернуть ключи.
	static void Finish(TSharedRef<FState> S)
	{
		S->Tick.Reset();
		UWorld* W = S->W.Get();
		ClearCells(S, W);
		PressAllOk(W);
		if (UAsdaPetGrowthSubsystem* G = SubOf<UAsdaPetGrowthSubsystem>(W)) { G->CloseGrowth(); }
		if (UAsdaPetCageSubsystem* C = SubOf<UAsdaPetCageSubsystem>(W)) { C->CloseCage(); }
		if (S->bCapture) { if (UAsdaPetSubsystem* P = SubOf<UAsdaPetSubsystem>(W)) { P->EndSendCapture(); } }
		if (S->Cv) { S->Cv->Restore(); }
		AsdaSelfTestKit::Finish(W, kCmd, S->bQuit, LogAsdaPetLimitTest, kPfx, S->Ok, S->Fail, TEXT(""), true);
	}

	// Начать текущий случай: список питомцев, предметы, кнопки игрока.
	static void StartCase(TSharedRef<FState> S, UWorld* W)
	{
		const FCase& C = S->Cases[S->Index];
		UAsdaInventorySubsystem* IV = SubOf<UAsdaInventorySubsystem>(W);
		UAsdaPetCageSubsystem* Cage = SubOf<UAsdaPetCageSubsystem>(W);
		UAsdaPetGrowthSubsystem* G = SubOf<UAsdaPetGrowthSubsystem>(W);
		S->Cv->Set(TEXT("asda.Pet.LimitBreakPotionLinkField"), C.KeyLink);
		S->Cv->Set(TEXT("asda.Pet.LimitBreakScroll"), C.KeyScroll);
		ClearCells(S, W);
		S->Sent.Reset();
		Feed6106(W, C.Level, C.MaxLevel, C.bActive);
		for (const FPut& I : C.Items)
		{
			if (!IV->DebugPutItem(I.Container, I.Cell, I.Code, 1))
			{
				S->Expect(false, FString::Printf(TEXT("%s: предмет %d не лёг в (%d,%d)"), C.Name, I.Code, I.Container, I.Cell));
			}
		}
		if (!Cage->IsCageOpen()) { Cage->OpenCage(); }
		Cage->OnSlotRightClicked(kStandCell);
		Cage->OnPanelGrowth();
		S->Expect(G->IsGrowthOpen() == C.bOpens, FString::Printf(TEXT("%s: окно 44200 %s"), C.Name, G->IsGrowthOpen() ? TEXT("открыто") : TEXT("не открыто")));
		if (G->IsGrowthOpen()) { G->OnOkClicked(); }
		S->Begin(EPhase::WaitModal);
	}

	// Закрыть окна случая и перейти к следующему; false — сценарий окончен.
	static bool NextCase(TSharedRef<FState> S, UWorld* W)
	{
		PressAllOk(W);
		if (UAsdaPetGrowthSubsystem* G = SubOf<UAsdaPetGrowthSubsystem>(W)) { G->CloseGrowth(); }
		if (UAsdaPetCageSubsystem* C = SubOf<UAsdaPetCageSubsystem>(W)) { C->CloseCage(); }
		if (++S->Index >= S->Cases.Num()) { Finish(S); return false; }
		StartCase(S, W);
		return true;
	}

	// Один тик сценария; false — конец.
	static bool Step(TSharedRef<FState> S)
	{
		UWorld* W = S->W.Get();
		UAsdaWindowSubsystem* WM = SubOf<UAsdaWindowSubsystem>(W);
		UAsdaInventorySubsystem* IV = SubOf<UAsdaInventorySubsystem>(W);
		if (!W || !WM || !IV) { Finish(S); return false; }
		const FCase& C = S->Cases[S->Index];
		const double Dt = FPlatformTime::Seconds() - S->T0;
		switch (S->Phase)
		{
		case EPhase::WaitModal:
			if (!WM->IsAnyModalOpen() && Dt < kWaitSec) { return true; }
			S->Expect(WM->IsAnyModalOpen(), FString::Printf(TEXT("%s: показано окно сообщения"), C.Name));
			WM->AnswerMsgBox(/*bTwoBut*/C.bSends, /*bYes*/true);
			S->Begin(C.bSends ? EPhase::WaitSend : EPhase::WaitQuiet);
			return true;

		case EPhase::WaitSend:
			if (S->Sent.Num() == 0 && Dt < kWaitSec) { return true; }
			if (S->Sent.Num() == 1 && S->Sent[0].Num() == kBody6148)
			{
				const TArray<uint8>& B = S->Sent[0];
				const int32 Uid = AsdaBytes::RdI32(B.GetData());
				const int32 Slot = AsdaBytes::RdI16(B.GetData() + 4);
				const int32 Bag = B[6];
				const int32 Premium = AsdaBytes::RdI16(B.GetData() + 7);
				S->Expect(Uid == kUid && Slot == C.Slot && Bag == C.Bag && Premium == C.Premium,
					FString::Printf(TEXT("%s: 6148 uid %d, контейнер %d, ячейка %d, талисман %d (ждали %d, %d, %d)"),
						C.Name, Uid, Bag, Slot, Premium, C.Bag, C.Slot, C.Premium));
			}
			else
			{
				S->Expect(false, FString::Printf(TEXT("%s: ушло %d×6148, длина %d (ждали 1×%d)"), C.Name, S->Sent.Num(), S->Sent.Num() ? S->Sent[0].Num() : 0, kBody6148));
				return NextCase(S, W);
			}
			Feed6149(W, C);
			S->Begin(EPhase::WaitReply);
			return true;

		case EPhase::WaitQuiet:
			if (Dt < kQuietSec) { return true; }
			S->Expect(S->Sent.Num() == 0, FString::Printf(TEXT("%s: 6148 не ушёл (%d)"), C.Name, S->Sent.Num()));
			return NextCase(S, W);

		case EPhase::WaitReply:
			{
				const FAsdaItemInstance* It = IV->GetCell(C.Bag, C.Slot);
				const bool bGone = !It || It->IsEmpty();
				if (!bGone && Dt < kWaitSec) { return true; }
				S->Expect(bGone, FString::Printf(TEXT("%s: ответ 6149 очистил (%d,%d)"), C.Name, C.Bag, C.Slot));
			}
			return NextCase(S, W);

		default:
			Finish(S);
			return false;
		}
	}

	// Сценарий.
	static TArray<FCase> BuildCases()
	{
		const FPut Potion40 { 2, 40, kPotion };
		const FPut Potion7 { 2, 7, kPotion };
		const FPut Charm42 { 1, 42, kCharm };
		const FPut Charm21 { 1, 21, kCharm };
		const FPut Scroll5 { 1, 5, kScroll };
		TArray<FCase> L;
		L.Add({ TEXT("1 старое сравнение, зелье в сумке"), { Potion40 }, 3, 3, false, 0, 0, true, false, 0, 0, -1 });
		L.Add({ TEXT("2 зелье в ячейке 40"), { Potion40 }, 3, 3, false, 1, 1, true, true, 2, 40, -1 });
		L.Add({ TEXT("3 зелье и талисман в ячейке 42"), { Potion40, Charm42 }, 3, 3, false, 1, 1, true, true, 2, 40, 42 });
		L.Add({ TEXT("4 только талисман"), { Charm21 }, 3, 3, false, 1, 1, true, false, 0, 0, -1 });
		L.Add({ TEXT("5 только свиток 399"), { Scroll5 }, 3, 3, false, 1, 1, true, true, 1, 5, -1 });
		L.Add({ TEXT("6 свиток при выключенном ключе"), { Scroll5 }, 3, 3, false, 1, 0, true, false, 0, 0, -1 });
		L.Add({ TEXT("7 зелье и свиток"), { Potion7, Scroll5 }, 3, 3, false, 1, 1, true, true, 2, 7, -1 });
		// ★ 07.11.2026: коробка 79940 кладёт две штуки в разные ячейки контейнера 2, окно берёт последнюю.
		L.Add({ TEXT("7а две штуки из коробки 79940"), { FPut{ 2, 41, kPotion }, FPut{ 2, 42, kPotion } }, 3, 3, false, 1, 1, true, true, 2, 42, -1 });
		L.Add({ TEXT("8 уровень ниже предела"), { Potion40 }, 2, 3, false, 1, 1, true, false, 0, 0, -1 });
		L.Add({ TEXT("9 уровень 10"), { Potion40 }, 10, 10, false, 1, 1, false, false, 0, 0, -1 });
		L.Add({ TEXT("10 питомец призван"), { Potion40 }, 3, 3, true, 1, 1, false, false, 0, 0, -1 });
		return L;
	}
}

// Запуск самотеста (консольная команда).
struct FAsdaPetLimitBreakSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaPetLimitTestLocal;
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (!W) { AsdaSelfTestKit::Refused(nullptr, kCmd, A.bQuit, TEXT("нет мира")); return; }
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, A)) { return; }
		TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(kCmd, A.bQuit);
		TWeakObjectPtr<UWorld> WW(W);
		const bool bQuit = A.bQuit;
		FTimerHandle H0;
		W->GetTimerManager().SetTimer(H0, FTimerDelegate::CreateLambda([WW, Gate]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd)) { return; }
			if (AsdaSelfTestKit::NeedSelfSpawn(Wd) && GEngine)
			{
				GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
		}), FMath::Max(0.01f, A.Delay), false);
		FTimerHandle H1;
		W->GetTimerManager().SetTimer(H1, FTimerDelegate::CreateLambda([WW, Gate, bQuit]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd)) { return; }
			if (!NetOf(Wd) || !SubOf<UAsdaPetCageSubsystem>(Wd) || !SubOf<UAsdaPetGrowthSubsystem>(Wd) || !SubOf<UAsdaInventorySubsystem>(Wd))
			{
				AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("нет сети / окон питомцев / инвентаря"));
				return;
			}
			TSharedRef<FState> S = MakeShared<FState>();
			S->W = Wd; S->bQuit = bQuit;
			S->Cases = BuildCases();
			S->Cv = MakeUnique<FAsdaCVarScope>(Wd, kCmd);
			S->bCapture = ArmCapture(S, Wd);
			if (!S->bCapture) { AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("перехват исходящих питомцев не взведён (занят)")); return; }
			CheckData(S, Wd);
			StartCase(S, Wd);
			S->Tick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float) -> bool { return Step(S); }));
		}), A.Delay + 2.f, false);
	}
};

// Консольная команда самотеста.
static FAutoConsoleCommandWithWorldAndArgs GAsdaPetLimitBreakSelfTest(
	TEXT("asda.Pet.SelfTestLimitBreak"),
	TEXT("Самотест снятия предела питомца (07.11.2026): зелье 23237, свиток 399, талисман 404, гейты окна 44200, тело 6148, ответ 6149. ")
	TEXT("Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W) { FAsdaPetLimitBreakSelfTest::Run(A, W); }));

#endif // !UE_BUILD_SHIPPING
