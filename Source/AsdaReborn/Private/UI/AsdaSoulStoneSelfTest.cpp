// Copyright (c) 2026 @sitxovski (Discord)
// САМОТЕСТ «КАМЕНЬ ДУШИ» — `asda.Soul.StoneSelfTest [en]`. Только офлайн (`-AsdaNoNet`). Двойной щелчок — штатный `UseByDoubleClick`,
//   входящие — штатный приём `DebugFeedPacket`. Сценарии: проверки 8152/8153/8154, окно 8157, ворота 5029, значок, окно 9125, 5425,
//   5419, все коды 5420, 5423, 5421, 5422, 5424, чужой слот 0, вход в мир, конец слота, контрольная пара `asda.Soul.StoneBind 1/0`.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "AsdaOutgoingGate.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaWindowSubsystem.h"
#include "TimerManager.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaSoulSlotSubsystem.h"
#include "UI/AsdaSoulStoneSubsystem.h"
#include "UI/AsdaSoulmateSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "Components/Image.h"
#include "World/AsdaCoord.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaWorldSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSoulStoneTest, Log, All);

namespace AsdaSoulStoneTestLocal
{
	const TCHAR* const kCmd = TEXT("asda.Soul.StoneSelfTest");   // имя команды
	constexpr int32 kCell = 3;                                   // ячейка камня
	constexpr int32 kBond = 5;                                   // номер пары
	const TCHAR* const kFriend = TEXT("Pal");                    // имя Друга

	// Один шаг сценария.
	struct FStep
	{
		FString Name;                 // имя шага
		float TimeoutSec = 3.f;       // потолок
		TFunction<void()> Start;      // начало
		TFunction<bool()> Poll;       // true — шаг завершён
		TFunction<void()> Check;      // проверки
	};

	// Состояние прогона.
	struct FState
	{
		TWeakObjectPtr<UWorld> World;           // мир
		TWeakObjectPtr<UAsdaNetSubsystem> Net;  // сеть
		TArray<FStep> Steps;                    // шаги
		int32 StepIdx = 0;                      // текущий шаг
		bool bStepStarted = false;              // шаг начат
		double StepStartS = 0.0;                // начало шага
		FTimerHandle Tick;                      // таймер исполнителя
		bool bQuit = false;                     // выйти после прогона
		FAsdaSelfTestRunGate Gate{ kCmd, false };   // офлайн-гейт
		int32 Ok = 0, Fail = 0;                 // счётчики
		TUniquePtr<FAsdaNetSelfScope> SelfScope;    // свой номер вернётся
		int32 Self = -1;                        // свой номер
		int32 Peer = 7822;                      // Друг в мире
		int32 Code = 0;                         // код камня
		bool bEn = false;                       // прогон на английском

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		UAsdaSoulSlotSubsystem* SS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaSoulSlotSubsystem>() : nullptr; }
		UAsdaSoulStoneSubsystem* St() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaSoulStoneSubsystem>() : nullptr; }
		UAsdaSoulmateSubsystem* Sm() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr; }
		UAsdaInventorySubsystem* Inv() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaInventorySubsystem>() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		double El() const { return FPlatformTime::Seconds() - StepStartS; }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaSoulStoneTest, Log, TEXT("САМОТЕСТ-КАМЕНЬ %s %s: %s"), bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		// 5229 или 5230: i16 userIdx, i16 слот, i16 остаток.
		void Slot(int32 Op, int32 User, int32 Id, int32 Dur = 600)
		{
			TArray<uint8> B; B.SetNumZeroed(6);
			AsdaBytes::WrI16(&B[0], (int16)User); AsdaBytes::WrI16(&B[2], (int16)Id); AsdaBytes::WrI16(&B[4], (int16)Dur);
			if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(Op, B); }
		}
		// 5306 по номеру персонажа.
		void ClearAll(int32 User)
		{
			const TArray<uint8> Hdr = { 0x89 };
			if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(5306, Hdr, (uint16)User); }
		}
		// Успешный 5029 по камню.
		void Use5029()
		{
			TArray<uint8> B; B.SetNumZeroed(24);
			B[0] = 1;
			AsdaBytes::WrI16(&B[1], (int16)Self);
			AsdaBytes::WrI32(&B[7], Code);
			AsdaBytes::WrI16(&B[11], (int16)kCell);
			B[15] = 2;
			AsdaBytes::WrI32(&B[16], 1);
			if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(5029, B); }
			if (UAsdaInventorySubsystem* I = Inv()) { I->DebugPumpPendingPackets(); }
		}
		// Байт +50 камня.
		int32 Byte50() const
		{
			const UAsdaInventorySubsystem* I = Inv();
			const FAsdaItemInstance* It = I ? I->GetCell(2, kCell) : nullptr;
			return (It && It->Raw.Num() > 50) ? It->Raw[50] : -1;
		}
		// Положить камень; bActive — сразу активированным.
		void Put(bool bActive)
		{
			UAsdaInventorySubsystem* I = Inv();
			if (!I) { return; }
			I->DebugPutItem(2, kCell, Code, 1);
			if (FAsdaItemInstance* It = I->GetCellMutable(2, kCell)) { It->Raw[50] = bActive ? 3 : 0; }
		}
		// Двойной щелчок игрока по камню.
		void Click() { if (UAsdaInventorySubsystem* I = Inv()) { I->UseByDoubleClick(2, kCell); } }
		// Журнал показанного через пробел.
		FString Shown() const
		{
			const UAsdaSoulStoneSubsystem* S = St();
			return S ? FString::Join(S->DebugLog(), TEXT(" ")) : FString();
		}
		// Журнал равен перечисленному (порядок важен).
		bool Is(const TArray<FString>& Want) const
		{
			const UAsdaSoulStoneSubsystem* S = St();
			return S && S->DebugLog() == Want;
		}
		// Чистая исходная точка: Друг вне игры, камень не активирован, слот 0 не идёт.
		void Base(bool bActive = false)
		{
			ClearAll(Self);
			if (UAsdaSoulmateSubsystem* M = Sm()) { M->DebugSetChatBond(kBond, /*bPair*/ false, kFriend); M->DebugSetRecPartner(Peer); }
			Put(bActive);
			if (UAsdaSoulStoneSubsystem* S = St()) { S->ResetBlock(); S->DebugClearLog(); }
		}
		// Друг в игре или нет.
		void Online(bool bOn) { if (UAsdaSoulmateSubsystem* M = Sm()) { M->DebugSetChatBond(kBond, bOn, kFriend); } }
		// 5420 с кодом.
		void Status(uint8 C)
		{
			TArray<uint8> B; B.SetNumZeroed(14);
			B[0] = C;
			for (int32 i = 0; i < 11; ++i) { B[3 + i] = (uint8)(0x30 + i); }
			if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(5420, B); }
		}
		// 5421 от просителя.
		void Ask(int32 From)
		{
			TArray<uint8> B; B.SetNumZeroed(13);
			AsdaBytes::WrI16(&B[0], (int16)From);
			for (int32 i = 0; i < 11; ++i) { B[2 + i] = (uint8)(0x40 + i); }
			if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(5421, B); }
		}
		// 5425: потолок и накопленное.
		void Bar(int64 Max, int64 Cur)
		{
			TArray<uint8> B; B.SetNumZeroed(22);
			FMemory::Memcpy(&B[6], &Max, 8);
			FMemory::Memcpy(&B[14], &Cur, 8);
			if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(5425, B); }
		}
	};
}

// Вход команды.
struct FAsdaSoulStoneSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaSoulStoneSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaSoulStoneTestLocal;
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006 };
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, KitArgs.bQuit, TEXT("нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate.bQuit = KitArgs.bQuit;
	S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
	for (const FString& A : Args) { if (A.Equals(TEXT("en"), ESearchCase::IgnoreCase)) { S->bEn = true; } }

	auto Add = [S](const FString& Name, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.TimeoutSec = Timeout;
		St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};
	// Шаг без ожидания.
	auto Now = [Add, S](const FString& Name, TFunction<void()> Body)
	{
		Add(Name, 1.f, MoveTemp(Body), [S]() { return S->El() > 0.05; }, nullptr);
	};

	Add(TEXT("подготовка"), 6.f, [S, Net]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		UAsdaWorldSubsystem* WSs = S->WS();
		if (!C || !WSs) { S->Expect(false, TEXT("нет своего персонажа после FakeSelfSpawn")); return; }
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = 4321; Net->bSelfUserNumKnown = true; }
		Net->bSelfDead = false;
		S->Self = (int32)Net->SelfUserNum;
		const FVector2D Tile = AsdaCoord::TileCenterGame(AsdaCoord::GameToTile(C->GetGamePosXZ()) + FIntPoint(2, 0));
		WSs->DebugSpawnFakePeer(S->Peer, TEXT("Pal"), Tile);
		AsdaCVarSave::SetForTest(TEXT("asda.Soul.SlotRun"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Soul.StoneBind"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Soul.StoneSelfSlotOnly"), 1);
		const UGameInstance* G = S->GI();
		const UAsdaItemTableSubsystem* IT = G ? G->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		if (IT)
		{
			for (const FAsdaItemRecord& R : IT->GetAllItems())
			{
				if (R.ItemSubType == 32 && R.DurationOrEnchant <= 0 && R.ItemCode > 0) { S->Code = R.ItemCode; break; }
			}
		}
	}, [S]()
	{
		const UAsdaWorldSubsystem* Wd = S->WS();
		return S->El() > 0.6 && Wd && Wd->GetPlayerActorByUserNum(S->Peer) != nullptr;
	}, [S]()
	{
		S->Expect(S->St() && S->SS() && S->Sm() && S->Inv(), TEXT("подсистемы на месте"));
		S->Expect(S->Code > 0, FString::Printf(TEXT("камень из таблицы предметов: код %d"), S->Code));
	});

	Now(TEXT("строки"), [S]()
	{
		const UGameInstance* G = S->GI();
		static const int32 kIds[] = { 6035, 8101, 6955, 8131, 8132, 8133, 8134, 8135, 8136, 8140, 8150, 8152, 8153, 8154, 8157, 8172, 20160, 20161, 20162 };
		for (const int32 Id : kIds)
		{
			const FString T = AsdaNotice::Loc(G, Id);
			bool bCyr = false;
			for (const TCHAR Ch : T) { if (Ch >= 0x400 && Ch <= 0x4FF) { bCyr = true; break; } }
			S->Expect(!T.IsEmpty() && bCyr != S->bEn, FString::Printf(TEXT("строка %d: «%s»"), Id, *T));
		}
	});

	Now(TEXT("значок bind_soulstone"), [S]()
	{
		UImage* Img = NewObject<UImage>(S->GI());
		S->Expect(Img && AsdaUi::ApplyFlatJuiIcon(Img, TEXT("bind_soulstone"), 32.f), TEXT("текстура значка загружается"));
	});

	Now(TEXT("проверки 8152, 8153, 8154"), [S]()
	{
		UAsdaSoulStoneSubsystem* St = S->St();
		S->Base();
		S->Click();
		S->Expect(S->Is({ TEXT("ask:8157") }) && St->DebugPendingCell() == kCell, FString::Printf(TEXT("Друг вне игры, слота нет: окно 8157, эталон проверит слот после ответа [%s]"), *S->Shown()));
		St->OnActivateConfirm(true);
		S->Expect(S->Is({ TEXT("ask:8157"), TEXT("chat:8153") }), FString::Printf(TEXT("согласие без слота: 8153, 5028 нет [%s]"), *S->Shown()));
		S->Base();
		S->Online(true);
		S->Click();
		S->Expect(S->Is({ TEXT("chat:8153") }), FString::Printf(TEXT("Друг в игре, слота нет: 8153 [%s]"), *S->Shown()));
		S->Base();
		if (UAsdaSoulmateSubsystem* M = S->Sm()) { M->DebugSetChatBond(-1, false, FString()); }
		S->Click();
		S->Expect(S->Is({ TEXT("chat:8152"), TEXT("chat:8153") }), FString::Printf(TEXT("пары нет: 8152 и 8153 [%s]"), *S->Shown()));
		S->Base();
		S->Slot(5229, S->Self, 0);
		S->Online(true);
		St->DebugClearLog();
		S->Click();
		S->Expect(S->Is({}) && St->DebugPendingCell() < 0, FString::Printf(TEXT("Друг в игре, слот идёт: молча [%s]"), *S->Shown()));
	});

	Now(TEXT("окно 8157 и 5028"), [S]()
	{
		UAsdaSoulStoneSubsystem* St = S->St();
		S->Base();
		S->Expect(!St->DebugBound(), TEXT("до слота флаг +637 снят"));
		S->Slot(5229, S->Self, 0);
		S->Expect(S->SS()->GetSlotState(S->Self, 0) == 2, TEXT("слот 0 действует"));
		S->Expect(St->DebugBound() && St->DebugFlag640(), TEXT("старт слота поставил +637 и +640"));
		S->Click();
		S->Expect(S->Is({ TEXT("ask:8157") }) && St->DebugPendingCell() == kCell, FString::Printf(TEXT("окно 8157 [%s]"), *S->Shown()));
		St->OnActivateConfirm(false);
		S->Expect(S->Is({ TEXT("ask:8157") }), TEXT("отказ в окне: ничего не ушло"));
		St->DebugClearLog();
		S->Click();
		St->OnActivateConfirm(true);
		S->Expect(S->Is({ TEXT("ask:8157"), TEXT("send:5028") }), FString::Printf(TEXT("согласие: 5028 [%s]"), *S->Shown()));
		St->DebugClearLog();
		S->Click();
		S->ClearAll(S->Self);
		St->OnActivateConfirm(true);
		S->Expect(S->Is({ TEXT("ask:8157"), TEXT("chat:8153") }), FString::Printf(TEXT("слот кончился при открытом окне: 8153 [%s]"), *S->Shown()));
		S->Base();
		S->Slot(5229, S->Self, 0);
		S->Click();
		S->Inv()->DebugPutItem(2, kCell, 0, 0);
		St->DebugClearLog();
		St->OnActivateConfirm(true);
		S->Expect(S->Is({}), TEXT("камень убрали при открытом окне: 5028 нет"));
	});

	Now(TEXT("ворота 5029"), [S]()
	{
		UAsdaSoulStoneSubsystem* St = S->St();
		S->Base();
		S->Use5029();
		S->Expect(S->Byte50() == 0, FString::Printf(TEXT("без +637 камень не активируется: байт %d"), S->Byte50()));
		S->Base();
		S->Slot(5229, S->Self, 0);
		S->Online(true);
		S->Use5029();
		S->Expect(S->Byte50() == 0, TEXT("Друг в игре: ворота закрыты"));
		S->Online(false);
		S->Use5029();
		S->Expect(S->Byte50() == 3 && St->DebugCell() == kCell, FString::Printf(TEXT("ворота открыты: байт %d, ячейка %d"), S->Byte50(), St->DebugCell()));
		S->Expect(St->DebugGhostBound(), TEXT("узлы окна 9125 привязаны"));
		S->Expect(St->DebugGhostVisible() && St->DebugPercentText() == TEXT("0 %"), FString::Printf(TEXT("окно 9125 показано, «%s»"), *St->DebugPercentText()));
	});

	Now(TEXT("шкала 5425"), [S]()
	{
		UAsdaSoulStoneSubsystem* St = S->St();
		S->Bar(1000, 250);
		S->Expect(St->DebugPercentText() == TEXT("25.00 %"), FString::Printf(TEXT("четверть: «%s»"), *St->DebugPercentText()));
		S->Bar(0, 250);
		S->Expect(St->DebugPercentText() == TEXT("0.00 %"), FString::Printf(TEXT("нулевой потолок: «%s»"), *St->DebugPercentText()));
	});

	Now(TEXT("просьба 5419"), [S]()
	{
		UAsdaSoulStoneSubsystem* St = S->St();
		St->DebugClearLog();
		S->Click();
		S->Expect(S->Is({ TEXT("chat:8101") }), FString::Printf(TEXT("Друг вне игры: 8101 «Друга нет в игре» [%s]"), *S->Shown()));
		St->DebugClearLog();
		S->Online(true);
		S->Click();
		S->Expect(S->Is({ TEXT("send:5419"), TEXT("chat:8150") }), FString::Printf(TEXT("Друг в игре: 5419 и 8150 [%s]"), *S->Shown()));
		const TArray<uint8>& B = St->DebugLastBody();
		S->Expect(B.Num() == 11 && B[0] == kCell && B[1] == 0 && B[4] == 2 && B[5] == 1, TEXT("тело 5419: ячейка, контейнер, количество"));
	});

	Now(TEXT("все коды 5420"), [S]()
	{
		UAsdaSoulStoneSubsystem* St = S->St();
		static const int32 kMap[][2] = { { 0, 8132 }, { 1, 8133 }, { 2, 8134 }, { 4, 8135 }, { 5, 8136 }, { 6, 8140 } };
		for (const auto& M : kMap)
		{
			St->DebugClearLog();
			S->Status((uint8)M[0]);
			S->Expect(S->Is({ FString::Printf(TEXT("box:%d"), M[1]) }) && St->DebugBound(), FString::Printf(TEXT("код %d: окно %d, привязка цела [%s]"), M[0], M[1], *S->Shown()));
		}
		St->DebugClearLog();
		S->Status(9);
		S->Expect(S->Is({}), TEXT("неизвестный код молчит"));
		S->Status(3);
		S->Expect(S->Is({ TEXT("box:20161") }) && St->DebugSendArmed(), FString::Printf(TEXT("код 3: окно 20161 [%s]"), *S->Shown()));
		St->OnSendConfirmOk();
		S->Expect(S->Is({ TEXT("box:20161"), TEXT("send:5423"), TEXT("send:5028") }) && !St->DebugSendArmed(), FString::Printf(TEXT("ОК: 5423 и 5028 [%s]"), *S->Shown()));
		St->DebugClearLog();
		St->OnSendConfirmOk();
		S->Expect(S->Is({}), TEXT("повторный ОК ничего не шлёт"));
		S->Use5029();
		S->Expect(!St->DebugBound() && St->DebugCell() < 0, TEXT("камень потрачен: блок сброшен"));
	});

	Now(TEXT("вопрос Другу 5421 и ответ 5422"), [S]()
	{
		UAsdaSoulStoneSubsystem* St = S->St();
		S->Base();
		S->Online(true);
		S->Ask(1234);
		S->Expect(S->Is({}), TEXT("чужой проситель: окна нет"));
		S->Ask(S->Peer);
		S->Expect(S->Is({ TEXT("ask:20160") }) && St->DebugRequester() == S->Peer, FString::Printf(TEXT("окно 20160 + 8131 [%s]"), *S->Shown()));
		St->OnPartnerAskConfirm(true);
		S->Expect(S->Is({ TEXT("ask:20160"), TEXT("send:5422:1") }), FString::Printf(TEXT("согласие: 5422 режим 1 [%s]"), *S->Shown()));
		const TArray<uint8>& B = St->DebugLastBody();
		S->Expect(B.Num() == 14 && B[0] == 1 && AsdaBytes::RdI16(&B[1]) == S->Peer && B[3] == 0x40 && B[13] == 0x4A, TEXT("тело 5422: режим, проситель, 11 байт"));
		S->Expect(St->DebugRequester() == S->Peer, TEXT("после согласия проситель цел"));
		St->DebugClearLog();
		S->Ask(S->Peer);
		St->OnPartnerAskConfirm(false);
		S->Expect(S->Is({ TEXT("ask:20160"), TEXT("send:5422:2") }) && St->DebugRequester() == -1, FString::Printf(TEXT("отказ: 5422 режим 2, проситель стёрт [%s]"), *S->Shown()));
	});

	Now(TEXT("подарок 5424"), [S]()
	{
		UAsdaSoulStoneSubsystem* St = S->St();
		S->Base(true);
		S->Slot(5229, S->Self, 0);
		St->DebugClearLog();
		TArray<uint8> B; B.SetNumZeroed(8);
		const uint8 LevelWas = S->N()->SelfLevel;
		S->N()->SelfLevel = 10;
		const int64 Gift = 500;
		FMemory::Memcpy(B.GetData(), &Gift, 8);
		S->N()->DebugFeedPacket(5424, B);
		S->Expect(S->Is({ TEXT("chat:20162"), TEXT("fx") }), FString::Printf(TEXT("нотис и эффект [%s]"), *S->Shown()));
		S->N()->SelfLevel = LevelWas;
		S->Expect(!St->DebugGiftText().IsEmpty() && !St->DebugGiftText().Contains(TEXT("[sf")) && !St->DebugGiftText().Contains(TEXT("0.000000")), FString::Printf(TEXT("текст с долей от размаха уровня «%s»"), *St->DebugGiftText()));
		S->Expect(!St->DebugBound(), TEXT("блок сброшен"));
	});

	Now(TEXT("чужой слот 0"), [S]()
	{
		UAsdaSoulStoneSubsystem* St = S->St();
		S->Base();
		S->Slot(5229, S->Peer, 0);
		S->Expect(!St->DebugBound(), TEXT("чужой слот не ставит +637"));
		S->ClearAll(S->Peer);
		S->Base();
		AsdaCVarSave::SetForTest(TEXT("asda.Soul.StoneSelfSlotOnly"), 0);
		S->Slot(5229, S->Peer, 0);
		S->Expect(St->DebugBound(), TEXT("ключ 0: чужой слот ставит +637, как оригинал"));
		AsdaCVarSave::SetForTest(TEXT("asda.Soul.StoneSelfSlotOnly"), 1);
		S->ClearAll(S->Peer);
	});

	Now(TEXT("вход в мир и конец слота"), [S]()
	{
		UAsdaSoulStoneSubsystem* St = S->St();
		S->Base(true);
		S->Slot(5229, S->Self, 0);
		St->OnWorldReady();
		S->Expect(St->DebugBound() && St->DebugCell() == kCell, TEXT("слот идёт: привязка и ячейка найдены"));
		St->OnStoneActivated(kCell);
		S->Expect(St->DebugGhostVisible(), TEXT("окно 9125 показано"));
		S->ClearAll(S->Self);
		S->Expect(!St->DebugGhostVisible() && St->DebugPercentText().IsEmpty(), TEXT("конец слота гасит окно 9125"));
		St->OnWorldReady();
		S->Expect(!St->DebugBound() && St->DebugCell() < 0, TEXT("слота нет: блок сброшен"));
	});

	Now(TEXT("контроль: ключ 0"), [S]()
	{
		UAsdaSoulStoneSubsystem* St = S->St();
		S->Base();
		AsdaCVarSave::SetForTest(TEXT("asda.Soul.StoneBind"), 0);
		S->Slot(5229, S->Self, 0);
		S->Expect(!St->DebugBound(), TEXT("старт слота флаги не ставит"));
		S->Online(true);
		S->Click();
		S->Expect(S->Is({}) && St->DebugPendingCell() < 0, FString::Printf(TEXT("щелчок идёт мимо проверок [%s]"), *S->Shown()));
		S->Use5029();
		S->Expect(S->Byte50() == 3, TEXT("5029 активирует без ворот, как прежний порт"));
		S->Status(0);
		S->Bar(1000, 250);
		S->Expect(S->Is({}) && St->DebugPercentText().IsEmpty(), TEXT("5420 и 5425 не разбираются"));
		AsdaCVarSave::SetForTest(TEXT("asda.Soul.StoneBind"), 1);
		S->Base();
		S->Slot(5229, S->Self, 0);
		S->Click();
		S->Expect(S->Is({ TEXT("ask:8157") }), TEXT("ключ 1 снова проверяет"));
		S->St()->OnActivateConfirm(false);
	});

	Add(TEXT("итог"), 1.f, [S]()
	{
		S->ClearAll(S->Self);
		if (UAsdaInventorySubsystem* I = S->Inv()) { I->DebugPutItem(2, kCell, 0, 0); }
		if (UAsdaSoulmateSubsystem* M = S->Sm()) { M->DebugSetChatBond(-1, false, FString()); M->DebugSetRecPartner(0); }
		if (UAsdaSoulStoneSubsystem* St = S->St()) { St->ResetBlock(); St->ClearGhost(); }
		// ★ 06.11.2026: окна-сообщения прогона (ONEBUT/TWOBUT) держали ввод модалкой — в пакете следующий самотест не получал наведения и щелчков.
		if (UGameInstance* GIc = S->World.IsValid() ? S->World->GetGameInstance() : nullptr)
		{
			if (UAsdaWindowSubsystem* WMc = GIc->GetSubsystem<UAsdaWindowSubsystem>())
			{
				WMc->ClearMsgBoxQueue(kCmd);
				S->Expect(!WMc->IsAnyModalOpen(), TEXT("итог: модальных окон не осталось"));
			}
		}
		S->SelfScope.Reset();
		AsdaSelfTestKit::Finish(S->World.Get(), kCmd, false, LogAsdaSoulStoneTest, TEXT("САМОТЕСТ-КАМЕНЬ"), S->Ok, S->Fail);
	}, nullptr, nullptr);

	TWeakObjectPtr<UWorld> WW(W);
	{
		FTimerHandle H;
		TWeakPtr<FState> WSg = S;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Net, [WW, WSg]()
		{
			UWorld* Wd = WW.Get();
			const TSharedPtr<FState> Sg = WSg.Pin();
			if (Wd && Sg && !Sg->Gate.Pass(Wd)) { return; }
			APlayerController* P = Wd ? Wd->GetFirstPlayerController() : nullptr;
			if (Wd && (!P || !P->GetPawn()) && GEngine)
			{
				GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
		}), FMath::Max(0.01f, KitArgs.Delay), false);
	}
	TWeakPtr<FState> WSt = S;
	W->GetTimerManager().SetTimer(S->Tick, FTimerDelegate::CreateWeakLambda(Net, [WSt, WW]()
	{
		TSharedPtr<FState> St = WSt.Pin();
		UWorld* Wd = WW.Get();
		if (!St || !Wd) { return; }
		if (!St->Gate.Pass(Wd))
		{
			Wd->GetTimerManager().ClearTimer(St->Tick);
			return;
		}
		if (!St->Steps.IsValidIndex(St->StepIdx))
		{
			Wd->GetTimerManager().ClearTimer(St->Tick);
			AsdaSelfTestKit::Quit(Wd, St->bQuit);
			return;
		}
		const double NowS = FPlatformTime::Seconds();
		FStep& Stp = St->Steps[St->StepIdx];
		if (!St->bStepStarted)
		{
			St->bStepStarted = true;
			St->StepStartS = NowS;
			UE_LOG(LogAsdaSoulStoneTest, Log, TEXT("САМОТЕСТ-КАМЕНЬ шаг %s"), *Stp.Name);
			if (Stp.Start) { Stp.Start(); }
			return;
		}
		const bool bDone = !Stp.Poll || Stp.Poll();
		if (!bDone && NowS - St->StepStartS < Stp.TimeoutSec) { return; }
		if (!bDone) { St->Expect(false, FString::Printf(TEXT("шаг не завершился за %.1f с"), Stp.TimeoutSec)); }
		if (Stp.Check) { Stp.Check(); }
		++St->StepIdx;
		St->bStepStarted = false;
	}), 0.01f, true, KitArgs.Delay + 2.5f);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSoulStoneSelfTest(
	TEXT("asda.Soul.StoneSelfTest"),
	TEXT("Самотест Камня души: проверки, окно 8157, ворота 5029, окно 9125, 5419…5425, чужой слот, контрольная пара. Аргумент en — проверка английских строк."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Soul.StoneSelfTest"));
		FAsdaSoulStoneSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
