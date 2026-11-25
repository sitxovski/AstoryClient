// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ВСЕ ПУТИ ВЫЗОВА ПИТОМЦА» `asda.Pet.SelfTestCallPaths` (05.11.2026, баг игроков «игра зависает при вызове
//   питомца (не всегда)»). [ПОРТ]
//   Что проверяет — путь игрока, без сети, с НАСТОЯЩИМИ телами пакетов через приёмник сети (`DebugFeedPacket`) и с
//   НАСТОЯЩИМИ обработчиками окна 43000 / панели действий 43058 (`OnSlotRightClicked` → `OnPanelCall` / `OnPanelRecall`):
//     A  окно: ПКМ по ячейке → «Призыв» → ушёл ровно один 6100 с uid питомца; ответ 6101 (код 1) + свой 6120 → засов снят,
//        признак «призван», нотис 101111 показан и снимается «ОК», актор питомца появился (слив очереди 6120 ≤ 2.6 с);
//        самый долгий кадр от прихода ответа до появления — ниже порога зависания;
//     B  повтор: «Призыв» по уже призванному — оригинал молчит (6100 нет); «Отменить призыв» дважды подряд — уходит ОДИН
//        6100 (второй отброшен засовом «в полёте»); 6101 + 6120(−1) — актор убран;
//     C  ответ потерян: ключ `asda.Pet.SendTimeoutSec 1` — засов снимается сам, следующий «Призыв» уходит;
//     D  смена карты при запросе в полёте: сброс `MapChanged` снимает засов, поздний 6101 (эхо uid совпало) применяется,
//        новый павн (путь 4003) получает питомца из списка 6106;
//     E  верхом: посадка `SetMount` (запись маунта из таблицы) → «Призыв» → питомец у всадника;
//     F  смерть: `EnterSelfDeadState` → «Призыв» уходит (клиентского гейта у оригинала нет — сервер решает сам) →
//        ответ применяется без зависания; воскрешение 4056;
//   Каждый шаг замеряет самый долгий кадр (тикер ядра) — признак «зависания».
// Консоль: `asda.Pet.SelfTestCallPaths [delay=N] [quit]`. Только офлайн (`-AsdaNoNet`).
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestKit.h"              // каркас: аргументы, отказ, ключи, итог, выход
#include "Test/AsdaSelfTestRunGate.h"   // FAsdaSelfTestRunGate — отложенный старт   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaNetSubsystem.h"                  // DebugFeedPacket, перехват отправки, SelectedCharId, смерть
#include "AsdaSessionReset.h"                  // UAsdaSessionResetSubsystem — сброс «смена карты»
#include "AsdaBytes.h"                         // запись полей тел 6101/6106/6120/4056
#include "Login/AsdaLoginData.h"               // EncodeCp1251 — кличка в телах
#include "Pet/AsdaPetSubsystem.h"
#include "UI/AsdaPetCageSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaPetActor.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaWorldSubsystem.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"                       // TActorIterator — поиск актора питомца
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPetCallTest, Log, All);

namespace AsdaPetCallTestLocal
{
	static const TCHAR* kCmd = TEXT("asda.Pet.SelfTestCallPaths");
	static const TCHAR* kPfx = TEXT("САМОТЕСТ-ПИТОМЕЦ-ВЫЗОВ");
	constexpr int32 kUid = 990077;           // uid питомца стенда
	constexpr int32 kType = 14;              // порода стенда (Bunny — из живых журналов игроков)
	constexpr int32 kTestCharId = 77077;     // свой charId, если офлайн его нет (0 = «свой неизвестен» отбрасывает пакеты)
	constexpr double kSpawnWaitSec = 2.6;    // слив очереди 6120 — таймер 2 с (+ кадр)
	constexpr double kHangSec = 1.0;         // кадр длиннее — «зависание» в смысле жалобы игрока
	constexpr int32 kStandCell = 0;          // ячейка окна 43000 с питомцем стенда

	// Шаги сценария (каждый — ожидание своего условия).
	enum class EStep : uint8
	{
		A_Sent, A_Notice, A_Spawn, B_RecallSent, B_Gone, C_Timeout, C_Resent, D_Spawn, E_Clear, E_Spawn, F_Clear, F_Spawn, Done
	};

	// Состояние прогона.
	struct FState
	{
		TWeakObjectPtr<UWorld> W;              // мир прогона
		bool bQuit = false;                    // выйти по окончании
		int32 Ok = 0, Fail = 0;                // счётчики проверок
		TUniquePtr<FAsdaCVarScope> Cv;         // ключи на время прогона (восстанавливаются)
		FTSTicker::FDelegateHandle Tick;       // тикер сценария
		bool bCapture = false;                 // перехват исходящих подсистемы питомцев взведён этим прогоном
		TArray<int32> Sent6100;                // uid из перехваченных тел 6100
		EStep Step = EStep::A_Sent;            // текущий шаг
		double T0 = 0.0;                       // начало ожидания шага
		float MaxDt = 0.f;                     // самый долгий кадр текущего шага
		bool bMounted = false;                 // посадка шага E удалась

		// Записать проверку в журнал и счётчик.
		void Expect(bool b, const FString& What)
		{
			b ? ++Ok : ++Fail;
			UE_LOG(LogAsdaPetCallTest, Warning, TEXT("%s %s %s"), kPfx, b ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), *What);
		}
		// Начать ожидание нового шага (сброс таймера и замера кадра).
		void Begin(EStep S) { Step = S; T0 = FPlatformTime::Seconds(); MaxDt = 0.f; }
	};

	// Сеть игры.
	static UAsdaNetSubsystem* NetOf(UWorld* W) { return AsdaSelfTestKit::NetOf(W); }
	// Подсистема игры по классу.
	template <class T> static T* SubOf(UWorld* W) { return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<T>() : nullptr; }

	// Настоящее тело 6106: charId, флаг 0, одна запись 46 б (uid, PetRecord 41 б, «призван» +45).
	static void Feed6106(UWorld* W, bool bActive)
	{
		UAsdaNetSubsystem* Net = NetOf(W);
		TArray<uint8> B; B.SetNumZeroed(5 + 46);
		AsdaBytes::WrI32(B.GetData(), Net->SelectedCharId);
		uint8* R = B.GetData() + 5;
		AsdaBytes::WrI32(R, kUid);
		uint8* P = R + 4;
		AsdaBytes::WrI16(P + 0, kType);
		FAsdaLoginData::EncodeCp1251(TEXT("Bunny"), P + 2, 16);
		P[18] = 90; P[19] = 1; P[20] = 4;   // сытость, уровень, предел
		R[45] = bActive ? 1 : 0;
		Net->DebugFeedPacket(6106, B);
	}

	// Настоящее тело 6101 (9 б): charId@0, код@4, uid@5.
	static void Feed6101(UWorld* W, int32 Code)
	{
		UAsdaNetSubsystem* Net = NetOf(W);
		TArray<uint8> B; B.SetNumZeroed(9);
		AsdaBytes::WrI32(B.GetData(), Net->SelectedCharId);
		B[4] = (uint8)Code;
		AsdaBytes::WrI32(B.GetData() + 5, kUid);
		Net->DebugFeedPacket(6101, B);
	}

	// Настоящее тело своего 6120 (22 б): charId@0, порода u16@4 (−1 = убрать), кличка@6.
	static void Feed6120(UWorld* W, bool bShow)
	{
		UAsdaNetSubsystem* Net = NetOf(W);
		TArray<uint8> B; B.SetNumZeroed(22);
		AsdaBytes::WrI32(B.GetData(), Net->SelectedCharId);
		AsdaBytes::WrI16(B.GetData() + 4, bShow ? kType : -1);
		if (bShow) { FAsdaLoginData::EncodeCp1251(TEXT("Bunny"), B.GetData() + 6, 16); }
		Net->DebugFeedPacket(6120, B);
	}

	// Настоящий 4056 («прибытие»/воскрешение на месте; снимает смерть, запускает слив очереди 6120).
	static void Feed4056(UWorld* W)
	{
		UAsdaNetSubsystem* Net = NetOf(W);
		TArray<uint8> B; B.SetNumZeroed(22);
		AsdaBytes::WrI16(&B[0], (int32)Net->SelfUserNum); AsdaBytes::WrI16(&B[2], Net->SelfTileX); AsdaBytes::WrI16(&B[4], Net->SelfTileZ);
		AsdaBytes::WrI32(&B[6], 1000); AsdaBytes::WrI16(&B[10], 100);
		Net->DebugFeedPacket(4056, B);
	}

	// Актор питомца своего павна (nullptr — нет).
	static AAsdaPetActor* SelfPet(UWorld* W)
	{
		UAsdaWorldSubsystem* WS = SubOf<UAsdaWorldSubsystem>(W);
		AActor* Me = WS ? WS->LocalPlayerActor.Get() : nullptr;
		if (!Me) { return nullptr; }
		for (TActorIterator<AAsdaPetActor> It(W); It; ++It)
		{
			if (IsValid(*It) && It->OwnerChar.Get() == Me) { return *It; }
		}
		return nullptr;
	}

	// Слот питомца стенда в модели.
	static const FAsdaPetSlot* StandSlot(UWorld* W)
	{
		UAsdaPetSubsystem* P = SubOf<UAsdaPetSubsystem>(W);
		return P ? P->FindByUniqueId(kUid) : nullptr;
	}

	// Игрок: ПКМ по ячейке питомца в 43000 и кнопка панели 43058 («Призыв» или «Отменить призыв»).
	static void ClickPanel(UWorld* W, bool bCall)
	{
		UAsdaPetCageSubsystem* C = SubOf<UAsdaPetCageSubsystem>(W);
		if (!C) { return; }
		if (!C->IsCageOpen()) { C->OpenCage(); }
		C->OnSlotRightClicked(kStandCell);
		bCall ? C->OnPanelCall() : C->OnPanelRecall();
	}

	// Игрок нажимает «ОК» на верхнем нотисе; true — нотис был.
	static bool PressOk(UWorld* W)
	{
		UAsdaWindowSubsystem* WM = SubOf<UAsdaWindowSubsystem>(W);
		if (!WM || !WM->IsAnyModalOpen()) { return false; }
		WM->AnswerMsgBox(/*bTwoBut*/false, /*bYes*/true);
		return true;
	}

	// Свой павн (персонаж игрока).
	static AAsdaPlayerCharacter* SelfPawn(UWorld* W)
	{
		UAsdaWorldSubsystem* WS = SubOf<UAsdaWorldSubsystem>(W);
		return WS ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get()) : nullptr;
	}

	// Перехват исходящих подсистемы питомцев (весь трафик; офлайн в сеть и так ничего не уходит): ловим тела 6100
	//   `charId i32@0 · petUid i32@4`. Если свой charId неизвестен (офлайн) — подменяется на стендовый (иначе приёмник
	//   отбрасывает 6106/6101/6120 как чужие); `EndSendCapture` вернёт настоящий. true — взведён.
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
				if (Op == 6100 && Body.Num() >= 8) { St->Sent6100.Add(AsdaBytes::RdI32(Body.GetData() + 4)); }
			}
		}, Net->SelectedCharId == 0 ? kTestCharId : 0);
	}

	// Завершение: убрать питомца стенда штатным путём, снять перехват, вернуть ключи и charId.
	static void Finish(TSharedRef<FState> S)
	{
		S->Tick.Reset();
		UWorld* W = S->W.Get();
		if (UAsdaNetSubsystem* Net = NetOf(W))
		{
			if (AAsdaPlayerCharacter* Me = SelfPawn(W)) { if (Me->IsMounted()) { Me->ClearMount(); } }
			if (Net->bSelfDead) { Feed4056(W); }
			Feed6120(W, false);
			for (int32 k = 0; k < 4 && PressOk(W); ++k) {}
			if (UAsdaPetCageSubsystem* C = SubOf<UAsdaPetCageSubsystem>(W)) { C->CloseCage(); }
			// снять перехват и вернуть настоящий charId (их подменял `BeginSendCapture`)
			if (S->bCapture) { if (UAsdaPetSubsystem* P = SubOf<UAsdaPetSubsystem>(W)) { P->EndSendCapture(); } }
		}
		if (S->Cv) { S->Cv->Restore(); }
		AsdaSelfTestKit::Finish(W, kCmd, S->bQuit, LogAsdaPetCallTest, kPfx, S->Ok, S->Fail, TEXT(""), true);
	}

	// Проверка «появился ли питомец» с замером кадра; true — шаг закончен (проверка записана).
	static bool WaitSpawn(TSharedRef<FState> S, UWorld* W, const TCHAR* What)
	{
		AAsdaPetActor* P = SelfPet(W);
		const double Dt = FPlatformTime::Seconds() - S->T0;
		if (!P && Dt < kSpawnWaitSec) { return false; }
		const bool bModel = P && P->Mesh && P->Mesh->GetSkeletalMeshAsset() && P->PetTypeId == kType;
		S->Expect(bModel && S->MaxDt < kHangSec, FString::Printf(TEXT("%s: питомец %s через %.2f с; самый долгий кадр %.0f мс (порог зависания %.0f мс)"),
			What, P ? (bModel ? TEXT("появился с моделью") : TEXT("появился БЕЗ модели/не той породы")) : TEXT("НЕ появился"),
			Dt, S->MaxDt * 1000.f, kHangSec * 1000.0));
		return true;
	}

	// Один тик сценария; false — конец.
	static bool Step(TSharedRef<FState> S, float DeltaTime)
	{
		UWorld* W = S->W.Get();
		UAsdaNetSubsystem* Net = NetOf(W);
		UAsdaPetSubsystem* Pets = SubOf<UAsdaPetSubsystem>(W);
		if (!W || !Net || !Pets) { Finish(S); return false; }
		S->MaxDt = FMath::Max(S->MaxDt, DeltaTime);
		const double Now = FPlatformTime::Seconds();
		switch (S->Step)
		{
		case EStep::A_Sent:
			// 6100 уходит из тика подсистемы питомцев (вне разбора кадров) — ждём перехват.
			if (S->Sent6100.Num() == 0 && Now - S->T0 < 1.0) { return true; }
			S->Expect(S->Sent6100.Num() == 1 && S->Sent6100[0] == kUid && Pets->IsCallRequestInFlight(),
				FString::Printf(TEXT("A окно: ПКМ → «Призыв» — ушёл %d×6100 (uid %d), засов «в полёте» %s"),
					S->Sent6100.Num(), S->Sent6100.Num() ? S->Sent6100[0] : -1, Pets->IsCallRequestInFlight() ? TEXT("взведён") : TEXT("НЕТ")));
			// Ответ сервера в его порядке (живой журнал 28.10): 6120 → 4060/4064 → 6101.
			Feed6120(W, true);
			Feed6101(W, 1);
			{
				const FAsdaPetSlot* Sl = StandSlot(W);
				S->Expect(!Pets->IsCallRequestInFlight() && Sl && Sl->bIsActive,
					FString::Printf(TEXT("A ответ 6101 код 1: засов %s, признак «призван» %s"),
						Pets->IsCallRequestInFlight() ? TEXT("ВИСИТ") : TEXT("снят"), (Sl && Sl->bIsActive) ? TEXT("стоит") : TEXT("НЕТ")));
			}
			S->Begin(EStep::A_Notice);
			return true;

		case EStep::A_Notice:
			// Нотис 101111 очередь окон-сообщений рисует на следующем кадре — ждём его (≤ 1 с), игрок жмёт «ОК».
			{
				UAsdaWindowSubsystem* WM = SubOf<UAsdaWindowSubsystem>(W);
				const bool bModal = WM && WM->IsAnyModalOpen();
				if (!bModal && Now - S->T0 < 1.0) { return true; }
				const bool bPressed = PressOk(W);
				S->Expect(bModal && bPressed && !(WM && WM->IsAnyModalOpen()),
					TEXT("A нотис 101111 показан, «ОК» снимает модальность — ввод мира вернулся (ничего не держит ввод)"));
			}
			S->Begin(EStep::A_Spawn);
			return true;

		case EStep::A_Spawn:
			if (!WaitSpawn(S, W, TEXT("A окно → 6101+6120"))) { return true; }
			// B: «Призыв» по уже призванному — оригинал не делает ничего.
			S->Sent6100.Reset();
			ClickPanel(W, /*bCall*/true);
			ClickPanel(W, /*bCall*/false);   // «Отменить призыв»
			ClickPanel(W, /*bCall*/false);   // повтор, пока первый в полёте
			S->Begin(EStep::B_RecallSent);
			return true;

		case EStep::B_RecallSent:
			if (Now - S->T0 < 0.5) { return true; }
			S->Expect(S->Sent6100.Num() == 1, FString::Printf(
				TEXT("B «Призыв» по призванному — молча; «Отменить призыв» ×2 подряд — ушёл %d×6100 (ждали 1: второй отброшен засовом)"),
				S->Sent6100.Num()));
			Feed6120(W, false);
			Feed6101(W, 1);
			for (int32 k = 0; k < 4 && PressOk(W); ++k) {}
			S->Begin(EStep::B_Gone);
			return true;

		case EStep::B_Gone:
			if (SelfPet(W) && Now - S->T0 < kSpawnWaitSec) { return true; }
			{
				const FAsdaPetSlot* Sl = StandSlot(W);
				S->Expect(!SelfPet(W) && Sl && !Sl->bIsActive && S->MaxDt < kHangSec,
					FString::Printf(TEXT("B отзыв 6101 + 6120(−1): актор %s, признак «призван» %s, самый долгий кадр %.0f мс"),
						SelfPet(W) ? TEXT("ОСТАЛСЯ") : TEXT("убран"), (Sl && Sl->bIsActive) ? TEXT("ОСТАЛСЯ") : TEXT("снят"), S->MaxDt * 1000.f));
			}
			// C: ответ потерян — засов обязан сняться сам.
			S->Cv->Set(TEXT("asda.Pet.SendTimeoutSec"), 1);
			S->Sent6100.Reset();
			ClickPanel(W, /*bCall*/true);
			S->Begin(EStep::C_Timeout);
			return true;

		case EStep::C_Timeout:
			if (Pets->IsCallRequestInFlight() && Now - S->T0 < 3.0) { return true; }
			S->Expect(!Pets->IsCallRequestInFlight() && S->Sent6100.Num() == 1,
				FString::Printf(TEXT("C ответа 6101 нет: засов снят сам через %.1f с (ключ asda.Pet.SendTimeoutSec 1), 6100 ушло %d"),
					Now - S->T0, S->Sent6100.Num()));
			S->Cv->Set(TEXT("asda.Pet.SendTimeoutSec"), 20);
			S->Sent6100.Reset();
			ClickPanel(W, /*bCall*/true);
			S->Begin(EStep::C_Resent);
			return true;

		case EStep::C_Resent:
			if (S->Sent6100.Num() == 0 && Now - S->T0 < 1.0) { return true; }
			S->Expect(S->Sent6100.Num() == 1 && Pets->IsCallRequestInFlight(), TEXT("C после таймаута «Призыв» снова уходит (кнопка не залипла)"));
			// D: смена карты, пока запрос в полёте.
			if (UAsdaSessionResetSubsystem* SR = SubOf<UAsdaSessionResetSubsystem>(W)) { SR->Broadcast(EAsdaSessionResetReason::MapChanged); }
			S->Expect(!Pets->IsCallRequestInFlight(), TEXT("D сброс «смена карты» при 6100 в полёте: засов снят"));
			// Сброс сессии снимает и перехват стенда (вместе с подменой charId) — взводим заново, как после входа на карту.
			S->bCapture = ArmCapture(S, W);
			// Сервер после портала шлёт список (питомец призван) и свой павн встаёт заново (путь 4003); поздний 6101 той же
			//   просьбы приходит уже на новой карте — эхо uid совпадает, ответ применяется.
			Feed6106(W, /*bActive*/true);
			if (GEngine) { GEngine->Exec(W, *FString::Printf(TEXT("Asda.World.FakeSelfSpawn %d %d"), Net->SelfTileX, Net->SelfTileZ)); }
			Feed4056(W);
			S->Begin(EStep::D_Spawn);
			return true;

		case EStep::D_Spawn:
			if (!WaitSpawn(S, W, TEXT("D смена карты → 6106 «призван» + новый павн"))) { return true; }
			for (int32 k = 0; k < 4 && PressOk(W); ++k) {}
			// E: верхом. Снять питомца (6120 −1 + список «никто не призван») и дождаться, пока актор уйдёт.
			// Отзыв настоящим путём сервера: 6120 (−1) + 6101 код 1 (тумблер снимает «призван»). Один список 6106 «не призван»
			//   признак не снимает — у эталона так же (`petDataSelectInvItemById` только ставит `self[155]`, не гасит).
			Feed6120(W, false);
			Feed6101(W, 1);
			for (int32 k = 0; k < 4 && PressOk(W); ++k) {}
			S->Begin(EStep::E_Clear);
			return true;

		case EStep::E_Clear:
			if (SelfPet(W) && Now - S->T0 < kSpawnWaitSec) { return true; }
			S->Expect(!SelfPet(W), TEXT("E перед посадкой питомец убран (6120 −1)"));
			if (AAsdaPlayerCharacter* Me = SelfPawn(W))
			{
				UAsdaWorldSubsystem* WS = SubOf<UAsdaWorldSubsystem>(W);
				for (int32 Code = 1; Code <= 4000 && WS && !Me->IsMounted(); ++Code)
				{
					FString Model, Set; float H = 0.f;
					if (WS->LookupMount(Code, Model, Set, H)) { Me->SetMount(Model, Set, H); }
				}
				S->bMounted = Me->IsMounted();
			}
			S->Expect(S->bMounted, TEXT("E посадка на маунт (запись из таблицы маунтов)"));
			S->Sent6100.Reset();
			ClickPanel(W, /*bCall*/true);
			Feed6120(W, true);
			Feed6101(W, 1);
			for (int32 k = 0; k < 4 && PressOk(W); ++k) {}
			S->Begin(EStep::E_Spawn);
			return true;

		case EStep::E_Spawn:
			if (!WaitSpawn(S, W, TEXT("E верхом: «Призыв» → 6101+6120"))) { return true; }
			S->Expect(S->Sent6100.Num() == 1, FString::Printf(TEXT("E верхом ушёл %d×6100"), S->Sent6100.Num()));
			if (AAsdaPlayerCharacter* Me = SelfPawn(W)) { Me->ClearMount(); }
			// F: смерть. Снять питомца, дождаться ухода актора и умереть настоящей точкой входа в смерть.
			// Отзыв настоящим путём сервера: 6120 (−1) + 6101 код 1 (тумблер снимает «призван»). Один список 6106 «не призван»
			//   признак не снимает — у эталона так же (`petDataSelectInvItemById` только ставит `self[155]`, не гасит).
			Feed6120(W, false);
			Feed6101(W, 1);
			for (int32 k = 0; k < 4 && PressOk(W); ++k) {}
			S->Begin(EStep::F_Clear);
			return true;

		case EStep::F_Clear:
			if (SelfPet(W) && Now - S->T0 < kSpawnWaitSec) { return true; }
			S->Expect(!SelfPet(W), TEXT("F перед смертью питомец убран (6120 −1)"));
			Net->EnterSelfDeadState(TEXT("самотест вызова питомца"), /*bAlreadyDead*/true);
			S->Sent6100.Reset();
			ClickPanel(W, /*bCall*/true);
			Feed6120(W, true);
			Feed6101(W, 1);
			for (int32 k = 0; k < 4 && PressOk(W); ++k) {}
			S->Begin(EStep::F_Spawn);
			return true;

		case EStep::F_Spawn:
			if (!WaitSpawn(S, W, TEXT("F мёртвым: «Призыв» → 6101+6120"))) { return true; }
			S->Expect(S->Sent6100.Num() == 1 && Net->bSelfDead,
				FString::Printf(TEXT("F мёртвым ушёл %d×6100 (клиентского гейта смерти у оригинала нет)"), S->Sent6100.Num()));
			Feed4056(W);
			S->Expect(!Net->bSelfDead, TEXT("F воскрешение 4056 снимает смерть"));
			Finish(S);
			return false;

		default:
			Finish(S);
			return false;
		}
	}
}

// Запуск самотеста (консольная команда).
struct FAsdaPetCallPathsSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaPetCallTestLocal;
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
			UAsdaNetSubsystem* Net = NetOf(Wd);
			UAsdaWorldSubsystem* WS = SubOf<UAsdaWorldSubsystem>(Wd);
			if (!Net || !WS || !WS->LocalPlayerActor || !SubOf<UAsdaPetCageSubsystem>(Wd))
			{
				AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("нет сети / мира / своего персонажа / окна питомцев"));
				return;
			}
			TSharedRef<FState> S = MakeShared<FState>();
			S->W = Wd; S->bQuit = bQuit;
			S->Cv = MakeUnique<FAsdaCVarScope>(Wd, kCmd);
			S->Cv->Set(TEXT("asda.Pet.SendTimeoutSec"), 20);
			S->bCapture = ArmCapture(S, Wd);
			if (!S->bCapture) { AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("перехват исходящих питомцев не взведён (занят)")); return; }
			// Настоящий 4056 («мир готов»): в игре `OnWorldReady` запускает таймер слива очереди 6120 (2 с); офлайн-спавн — нет.
			Feed4056(Wd);
			Feed6106(Wd, /*bActive*/false);
			ClickPanel(Wd, /*bCall*/true);
			S->Begin(EStep::A_Sent);
			S->Tick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float Dt) -> bool { return Step(S, Dt); }));
		}), A.Delay + 2.f, false);
	}
};

// Консольная команда самотеста.
static FAutoConsoleCommandWithWorldAndArgs GAsdaPetCallPathsSelfTest(
	TEXT("asda.Pet.SelfTestCallPaths"),
	TEXT("Самотест всех путей вызова питомца (05.11.2026, баг «зависает при вызове»): окно/панель, повтор, потеря ответа, ")
	TEXT("смена карты, верхом, мёртвым — настоящие 6106/6101/6120/4056 через приёмник сети, перехват 6100, замер самого долгого кадра. ")
	TEXT("Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W) { FAsdaPetCallPathsSelfTest::Run(A, W); }));

#endif // !UE_BUILD_SHIPPING
