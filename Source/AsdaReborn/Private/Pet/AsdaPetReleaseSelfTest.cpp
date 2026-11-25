// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ВЫБРОС ПИТОМЦА» `asda.Pet.SelfTestRelease` (09.11.2026, пачка 20 баг 6: «после удаления питомца его 3D-модель
//   остаётся в игре»). [ПОРТ]
//   Путь игрока без сети: настоящий 6106 (три питомца, A призван) и свой 6120 через приёмник → модель A у своего персонажа →
//   окно 43000 → выбор ячейки (событие 0xB) → корзина 43051 → вопрос 55 «ОК» → перехват 6110 → ответ сервера в ЕГО порядке:
//   при выбросе призванного `sub_454920` рассылает 6120 −1, затем 6111 (`GameServer.c:35895-35927`); при выбросе непризванного — только 6111.
//   Случаи:
//     1  непризванный B при открытом окне: превью 43052 пустеет (у оригинала ветка `mapDecoModelRemoveById`, `entity_char.c:7801`),
//        модель призванного A на месте, запись B ушла;
//     2  призванный A при открытом окне: модель A снята ≤ 0.6 с после 6111 (у оригинала опрос 500 мс, `entity_char.c:7778-7780`),
//        плашка 43200 спрятана, превью пусто, запись ушла;
//     3  призванный C, окно закрыто до ответа: модель снята так же, плашка спрятана.
//   Кадры `asda_petrelease_c<случай>.png` (копия в `out=`). Итог `ВЫБРОС-ПИТОМЦА ИТОГ: ok=N fail=M`.
// Консоль: `asda.Pet.SelfTestRelease [out=<папка>] [quit]`. Только офлайн (`-AsdaNoNet`).
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestKit.h"              // аргументы, отказ, итог, выход
#include "AsdaNetSubsystem.h"                  // DebugFeedPacket, SelectedCharId
#include "AsdaBytes.h"                         // поля тел 6106/6111/6120
#include "Login/AsdaLoginData.h"               // EncodeCp1251 — клички
#include "Pet/AsdaPetSubsystem.h"
#include "UI/AsdaPetCageSubsystem.h"
#include "UI/AsdaPetPreviewActor.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaPetActor.h"
#include "World/AsdaWorldSubsystem.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"                      // FScreenshotRequest

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPetReleaseTest, Log, All);

namespace AsdaPetReleaseTestLocal
{
	static const TCHAR* kCmd = TEXT("asda.Pet.SelfTestRelease");
	static const TCHAR* kPfx = TEXT("ВЫБРОС-ПИТОМЦА");
	static const FName kStatusId(TEXT("Pet_Status_Win"));   // плашка 43200 в реестре окон
	constexpr int32 kTestCharId = 77111;       // свой charId, если офлайн его нет
	constexpr int32 kUidA = 991101;            // призванный в случаях 1–2
	constexpr int32 kUidB = 991102;            // непризванный, выбрасывается в случае 1
	constexpr int32 kUidC = 991103;            // призванный в случае 3
	constexpr int32 kTypeA = 14;               // порода A (Bunny)
	constexpr int32 kTypeB = 20;               // порода B (Dogi)
	constexpr int32 kTypeC = 13;               // порода C (Tiga)
	constexpr double kSpawnWaitSec = 2.8;      // слив очереди 6120 — 2 с (+ кадры)
	constexpr double kDropLimitSec = 0.6;      // потолок «6111 → модели нет»: опрос оригинала 500 мс + кадр

	// Шаг сценария.
	enum class EPhase : uint8 { Spawn, Pick, Confirm, Sent, Reply, Check, Done };

	// Состояние прогона.
	struct FState
	{
		TWeakObjectPtr<UGameInstance> GI;      // игра
		bool bQuit = false;                    // выйти по окончании
		int32 Ok = 0, Fail = 0;                // счётчики проверок
		FString OutDir;                        // папка для копий кадров
		FString PendingShot;                   // кадр, ждущий копии
		bool bCapture = false;                 // перехват взведён этим прогоном
		TArray<int32> Sent6110;                // uid из перехваченных тел 6110
		int32 Case = 1;                        // номер случая
		EPhase Phase = EPhase::Spawn;          // шаг
		double T0 = 0.0;                       // начало шага

		// Записать проверку.
		void Expect(bool b, const FString& What)
		{
			b ? ++Ok : ++Fail;
			UE_LOG(LogAsdaPetReleaseTest, Warning, TEXT("%s %s %s"), kPfx, b ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), *What);
		}
		// Перейти к шагу.
		void Begin(EPhase P) { Phase = P; T0 = FPlatformTime::Seconds(); }
		// Текущий мир.
		UWorld* W() const { return GI.IsValid() ? GI->GetWorld() : nullptr; }
		// Подсистема игры.
		template <class T> T* Sub() const { return GI.IsValid() ? GI->GetSubsystem<T>() : nullptr; }
	};

	// Uid выбрасываемого в случае.
	static int32 CaseUid(int32 Case) { return Case == 1 ? kUidB : (Case == 2 ? kUidA : kUidC); }
	// Порода выбрасываемого в случае.
	static int32 CaseType(int32 Case) { return Case == 1 ? kTypeB : (Case == 2 ? kTypeA : kTypeC); }
	// Выбрасывается ли призванный.
	static bool CaseActive(int32 Case) { return Case != 1; }

	// Запись 46 б тела 6106: uid, PetRecord 41 б (порода, кличка, сытость, уровень, предел), «призван» +45.
	static void PutRecord(uint8* R, int32 Uid, int32 Type, const TCHAR* Name, bool bActive)
	{
		AsdaBytes::WrI32(R, Uid);
		uint8* P = R + 4;
		AsdaBytes::WrI16(P + 0, Type);
		FAsdaLoginData::EncodeCp1251(Name, P + 2, 16);
		P[18] = 90; P[19] = 3; P[20] = 5;
		R[45] = bActive ? 1 : 0;
	}

	// Настоящий 6106 со списком записей (charId, флаг 0, N × 46 б).
	static void Feed6106(const FState& S, const TArray<TTuple<int32, int32, const TCHAR*, bool>>& L)
	{
		UAsdaNetSubsystem* Net = S.Sub<UAsdaNetSubsystem>();
		TArray<uint8> B; B.SetNumZeroed(5 + 46 * L.Num());
		AsdaBytes::WrI32(B.GetData(), Net->SelectedCharId);
		for (int32 i = 0; i < L.Num(); ++i) { PutRecord(B.GetData() + 5 + 46 * i, L[i].Get<0>(), L[i].Get<1>(), L[i].Get<2>(), L[i].Get<3>()); }
		Net->DebugFeedPacket(6106, B);
	}

	// Настоящий свой 6120 (22 б): charId@0, порода@4 (−1 — убрать), кличка@6.
	static void Feed6120(const FState& S, int32 Type, const TCHAR* Name)
	{
		UAsdaNetSubsystem* Net = S.Sub<UAsdaNetSubsystem>();
		TArray<uint8> B; B.SetNumZeroed(22);
		AsdaBytes::WrI32(B.GetData(), Net->SelectedCharId);
		AsdaBytes::WrI16(B.GetData() + 4, Type);
		if (Type >= 0) { FAsdaLoginData::EncodeCp1251(Name, B.GetData() + 6, 16); }
		Net->DebugFeedPacket(6120, B);
	}

	// Настоящий 6111 (9 б): charId@0, uid@4, код@8.
	static void Feed6111(const FState& S, int32 Uid)
	{
		UAsdaNetSubsystem* Net = S.Sub<UAsdaNetSubsystem>();
		TArray<uint8> B; B.SetNumZeroed(9);
		AsdaBytes::WrI32(B.GetData(), Net->SelectedCharId);
		AsdaBytes::WrI32(B.GetData() + 4, Uid);
		B[8] = 1;
		Net->DebugFeedPacket(6111, B);
	}

	// Настоящий 4056 («прибытие» своего персонажа): в игре `OnWorldReady` запускает по нему таймер слива очереди 6120 (2 с).
	static void Feed4056(const FState& S)
	{
		UAsdaNetSubsystem* Net = S.Sub<UAsdaNetSubsystem>();
		TArray<uint8> B; B.SetNumZeroed(22);
		AsdaBytes::WrI16(&B[0], (int32)Net->SelfUserNum); AsdaBytes::WrI16(&B[2], Net->SelfTileX); AsdaBytes::WrI16(&B[4], Net->SelfTileZ);
		AsdaBytes::WrI32(&B[6], 1000); AsdaBytes::WrI16(&B[10], 100);
		Net->DebugFeedPacket(4056, B);
	}

	// Модель своего питомца в мире (nullptr — нет).
	static AAsdaPetActor* SelfPet(const FState& S)
	{
		UAsdaWorldSubsystem* WS = S.Sub<UAsdaWorldSubsystem>();
		AActor* Me = WS ? WS->LocalPlayerActor.Get() : nullptr;
		UWorld* W = S.W();
		if (!Me || !W) { return nullptr; }
		for (TActorIterator<AAsdaPetActor> It(W); It; ++It)
		{
			if (IsValid(*It) && It->OwnerChar.Get() == Me) { return *It; }
		}
		return nullptr;
	}

	// Порода в превью 43052 (−1 — пусто или превью не поднято).
	static int32 PreviewType(const FState& S)
	{
		UWorld* W = S.W();
		if (!W) { return -1; }
		for (TActorIterator<AAsdaPetPreviewActor> It(W); It; ++It)
		{
			if (IsValid(*It)) { return It->IsPetModelAlive() ? It->GetPetTypeId() : -1; }
		}
		return -1;
	}

	// Ячейка окна 43000 с питомцем uid (−1 — нет).
	static int32 CellOf(const FState& S, int32 Uid)
	{
		const UAsdaPetSubsystem* P = S.Sub<UAsdaPetSubsystem>();
		if (!P) { return -1; }
		const TArray<FAsdaPetSlot>& L = P->GetSlots();
		for (int32 i = 0; i < L.Num(); ++i) { if (L[i].UniqueId == Uid) { return i; } }
		return -1;
	}

	// Кадр с копией в папку отчёта (копия — на следующем шаге, когда файл записан).
	static void Shot(FState& S, const FString& Name)
	{
		S.PendingShot = Name;
		FScreenshotRequest::RequestScreenshot(Name, /*bShowUI*/ true, /*bAddUniqueSuffix*/ false);
	}
	// Скопировать прошлый кадр.
	static void CopyShot(FState& S)
	{
		if (S.PendingShot.IsEmpty() || S.OutDir.IsEmpty()) { return; }
		const FString Src = FPaths::ScreenShotDir() / (S.PendingShot + TEXT(".png"));
		IFileManager::Get().MakeDirectory(*S.OutDir, true);
		const bool bCopied = IFileManager::Get().Copy(*(S.OutDir / (S.PendingShot + TEXT(".png"))), *Src) == COPY_OK;
		UE_LOG(LogAsdaPetReleaseTest, Log, TEXT("%s кадр %s: %s"), kPfx, *S.PendingShot, bCopied ? TEXT("скопирован") : TEXT("НЕ скопирован"));
		S.PendingShot.Reset();
	}

	// Перехват исходящих: тела 6110 в состояние; подмена charId офлайн.
	static bool ArmCapture(const TSharedRef<FState>& S)
	{
		UAsdaNetSubsystem* Net = S->Sub<UAsdaNetSubsystem>();
		UAsdaPetSubsystem* Pets = S->Sub<UAsdaPetSubsystem>();
		if (!Net || !Pets) { return false; }
		TWeakPtr<FState> WS = S;
		return Pets->BeginSendCapture([WS](int32 Op, const TArray<uint8>& Body, bool, TCHAR)
		{
			if (TSharedPtr<FState> St = WS.Pin())
			{
				if (Op == 6110 && Body.Num() >= 8) { St->Sent6110.Add(AsdaBytes::RdI32(Body.GetData() + 4)); }
			}
		}, Net->SelectedCharId == 0 ? kTestCharId : 0);
	}

	// Завершение: окна закрыть, модель убрать штатным 6120 −1, перехват снять, итог.
	static void Finish(FState& S)
	{
		CopyShot(S);
		if (UAsdaWindowSubsystem* WM = S.Sub<UAsdaWindowSubsystem>()) { for (int32 k = 0; k < 4 && WM->IsAnyModalOpen(); ++k) { WM->AnswerMsgBox(false, true); } }
		if (UAsdaPetCageSubsystem* C = S.Sub<UAsdaPetCageSubsystem>()) { C->CloseCage(); }
		if (S.bCapture) { if (UAsdaPetSubsystem* P = S.Sub<UAsdaPetSubsystem>()) { P->EndSendCapture(); } }
		AsdaSelfTestKit::Finish(S.W(), kCmd, S.bQuit, LogAsdaPetReleaseTest, kPfx, S.Ok, S.Fail, TEXT(""), true);
		S.Phase = EPhase::Done;
	}

	// Подготовка случая: список, свой 6120 призванного, окно.
	static void PrepareCase(FState& S)
	{
		S.Sent6110.Reset();
		if (S.Case == 1)
		{
			Feed6106(S, { MakeTuple(kUidA, kTypeA, TEXT("Bunny"), true), MakeTuple(kUidB, kTypeB, TEXT("Dogi"), false) });
			Feed6120(S, kTypeA, TEXT("Bunny"));
		}
		else if (S.Case == 3)
		{
			Feed6106(S, { MakeTuple(kUidC, kTypeC, TEXT("Tiga"), true) });
			Feed6120(S, kTypeC, TEXT("Tiga"));
		}
		S.Begin(EPhase::Spawn);
	}

	// Один кадр сценария; false — конец.
	static bool Step(const TSharedRef<FState>& SR)
	{
		FState& S = *SR;
		const double Dt = FPlatformTime::Seconds() - S.T0;
		UAsdaPetCageSubsystem* Cage = S.Sub<UAsdaPetCageSubsystem>();
		UAsdaPetSubsystem* Pets = S.Sub<UAsdaPetSubsystem>();
		UAsdaWindowSubsystem* WM = S.Sub<UAsdaWindowSubsystem>();
		if (!Cage || !Pets || !WM) { Finish(S); return false; }
		const int32 Uid = CaseUid(S.Case);
		const FString Tag = FString::Printf(TEXT("случай %d (%s, окно %s)"), S.Case,
			CaseActive(S.Case) ? TEXT("призванный") : TEXT("непризванный"), S.Case == 3 ? TEXT("закрыто до ответа") : TEXT("открыто"));
		switch (S.Phase)
		{
		case EPhase::Spawn:
			{
				AAsdaPetActor* P = SelfPet(S);
				const int32 WantType = S.Case == 3 ? kTypeC : kTypeA;
				if ((!P || P->PetTypeId != WantType) && Dt < kSpawnWaitSec) { return true; }
				S.Expect(P && P->PetTypeId == WantType, FString::Printf(TEXT("%s: до выброса модель призванного (порода %d) у своего персонажа %s"),
					*Tag, WantType, P ? TEXT("стоит") : TEXT("НЕТ")));
				// Игрок: окно 43000, выбор ячейки выбрасываемого.
				if (!Cage->IsCageOpen()) { Cage->OpenCage(); }
				Cage->OnSlotLeftClicked(CellOf(S, Uid));
				S.Begin(EPhase::Pick);
				return true;
			}

		case EPhase::Pick:
			// Превью рисуется тиком окна — ждём, пока в нём встанет выбранный.
			if (PreviewType(S) != CaseType(S.Case) && Dt < 1.5) { return true; }
			S.Expect(PreviewType(S) == CaseType(S.Case), FString::Printf(TEXT("%s: выбран, превью 43052 показывает породу %d"), *Tag, PreviewType(S)));
			Cage->OnTrashClicked();   // корзина 43051 → вопрос 55
			S.Begin(EPhase::Confirm);
			return true;

		case EPhase::Confirm:
			if (!WM->IsAnyModalOpen() && Dt < 1.0) { return true; }
			S.Expect(WM->IsAnyModalOpen(), FString::Printf(TEXT("%s: вопрос 55 показан"), *Tag));
			WM->AnswerMsgBox(/*bTwoBut*/ true, /*bYes*/ true);
			S.Begin(EPhase::Sent);
			return true;

		case EPhase::Sent:
			if (S.Sent6110.Num() == 0 && Dt < 1.0) { return true; }
			S.Expect(S.Sent6110.Num() == 1 && S.Sent6110[0] == Uid, FString::Printf(TEXT("%s: ушёл %d×6110, uid %d (ждали %d)"),
				*Tag, S.Sent6110.Num(), S.Sent6110.Num() ? S.Sent6110[0] : -1, Uid));
			if (S.Case == 3) { Cage->CloseCage(); }
			// Ответ сервера в его порядке.
			if (CaseActive(S.Case)) { Feed6120(S, -1, TEXT("")); }
			Feed6111(S, Uid);
			S.Begin(EPhase::Reply);
			return true;

		case EPhase::Reply:
			{
				const bool bModelGone = CaseActive(S.Case) ? (SelfPet(S) == nullptr) : true;
				const bool bPreviewEmpty = S.Case == 3 || PreviewType(S) == -1;
				if ((!bModelGone || !bPreviewEmpty) && Dt < kDropLimitSec) { return true; }
				if (CaseActive(S.Case))
				{
					S.Expect(bModelGone, FString::Printf(TEXT("%s: модель питомца у своего персонажа %s через %.2f с после 6111 (потолок %.1f с)"),
						*Tag, bModelGone ? TEXT("снята") : TEXT("ОСТАЛАСЬ"), Dt, kDropLimitSec));
				}
				else
				{
					AAsdaPetActor* P = SelfPet(S);
					S.Expect(P && P->PetTypeId == kTypeA, TEXT("случай 1: модель призванного A не тронута выбросом B"));
				}
				if (S.Case != 3)
				{
					S.Expect(bPreviewEmpty, FString::Printf(TEXT("%s: превью 43052 %s (порода %d)"), *Tag, bPreviewEmpty ? TEXT("пусто") : TEXT("ПОКАЗЫВАЕТ выброшенного"), PreviewType(S)));
				}
				S.Expect(CellOf(S, Uid) < 0, FString::Printf(TEXT("%s: запись выброшенного ушла из списка"), *Tag));
				if (CaseActive(S.Case))
				{
					S.Expect(!Cage->IsStatusPanelShown() && !WM->IsWindowVisible(kStatusId), FString::Printf(TEXT("%s: плашка 43200 спрятана"), *Tag));
				}
				if (S.Case == 3 && !Cage->IsCageOpen()) { Cage->OpenCage(); }   // кадр с окном: в нём не должно быть выброшенного
				Shot(S, FString::Printf(TEXT("asda_petrelease_c%d"), S.Case));
				S.Begin(EPhase::Check);
				return true;
			}

		case EPhase::Check:
			if (Dt < 0.5) { return true; }
			CopyShot(S);
			// Модель не должна вернуться (слив очереди 6120 и восстановление из списка) — ждём больше периода очереди.
			if (Dt < 2.6) { return true; }
			if (CaseActive(S.Case)) { S.Expect(SelfPet(S) == nullptr, FString::Printf(TEXT("%s: через 2.6 с модель не вернулась"), *Tag)); }
			Cage->CloseCage();
			if (++S.Case > 3) { Finish(S); return false; }
			PrepareCase(S);
			return true;

		default:
			return false;
		}
	}
}

// Консольная команда самотеста.
static FAutoConsoleCommandWithWorldAndArgs GAsdaPetReleaseSelfTest(
	TEXT("asda.Pet.SelfTestRelease"),
	TEXT("Самотест выброса питомца (09.11.2026): непризванный и призванный, окно открыто и закрыто — модель в мире, превью 43052, ")
	TEXT("плашка 43200, запись. `out=<папка>` для кадров, `quit`. Только офлайн."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaPetReleaseTestLocal;
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		FString Out;
		for (const FString& P : A.Pos) { if (P.StartsWith(TEXT("out="))) { Out = P.Mid(4); } }
		if (!W) { AsdaSelfTestKit::Refused(nullptr, kCmd, A.bQuit, TEXT("нет мира")); return; }
		if (!A.Has(TEXT("selfready")) && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			FString Again = FString::Printf(TEXT("%s selfready"), kCmd);
			if (!Out.IsEmpty()) { Again += TEXT(" out=") + Out; }
			if (A.bQuit) { Again += TEXT(" quit"); }
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, A)) { return; }
		TSharedRef<FState> S = MakeShared<FState>();
		S->GI = W->GetGameInstance();
		S->bQuit = A.bQuit;
		S->OutDir = Out;
		if (!S->Sub<UAsdaPetCageSubsystem>() || !S->Sub<UAsdaWindowSubsystem>()) { AsdaSelfTestKit::Refused(W, kCmd, A.bQuit, TEXT("нет окон питомцев")); return; }
		S->bCapture = ArmCapture(S);
		if (!S->bCapture) { AsdaSelfTestKit::Refused(W, kCmd, A.bQuit, TEXT("перехват исходящих питомцев не взведён (занят)")); return; }
		Feed4056(*S);
		PrepareCase(*S);
		UE_LOG(LogAsdaPetReleaseTest, Log, TEXT("%s старт: 3 случая"), kPfx);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float) { return Step(S); }));
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
