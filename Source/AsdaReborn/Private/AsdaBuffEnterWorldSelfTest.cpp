// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ ВОССТАНОВЛЕНИЯ СВОИХ СОСТОЯНИЙ ПРИ ВХОДЕ В МИР (25.10.2026): `asda.SelfTest.BuffEnterWorld [quit]`.
//   Баг игроков: расходные зелья-баффы (опыт, атака, ОЗ, защита) на 1 час пропадали после перезапуска игры. Сервер остаток хранит
//   (`Joy1.dbo.CharSkillItem`) и отдаёт его в теле 1021 (+368, 30 × 10 б; состояния навыков — +32, 28 × 12 б), порт эти байты
//   не читал. Офлайн (`-AsdaNoNet`), на карте мира. Реальный путь (закон diagnostic-must-use-real-path): кадр 1021 подаётся в
//   общий разбор входящих (`DebugFeedPacket`) — он пишет снимок и рассылает полный сброс сессии «вход в мир»; затем, как
//   обработчик 4003, ставится свой номер и зовётся ТОТ ЖЕ обработчик делегата `OnSelfSpawn` (`OnSelfSpawnCarry`) — сам 4003
//   не подаётся, чтобы не пересоздавать павна посреди пакета. Проверяется:
//     A) ключ `asda.Buff.EnterWorldRestore 0` (прежний порт) — после входа список своих состояний ПУСТ (воспроизведение бага);
//     B) ключ 1: заявка взведена сбросом 1021 и снята 4003; четыре зелья легли в слоты = SubType своих строк `special_itemlist`
//        с остатком из снимка, состояние навыка — с остатком, транспорт (слот 15) — делегатом посадки; имя предмета для
//        подсказки — на языке сессии; повторная 4003 (портал) ничего не накладывает заново;
//     C) через 2,5 с: плашка STATE_ICON (мини-статус пересобирается тиком после сброса 1021) — иконка и таймер у каждого зелья
//        с флагом 2; остаток идёт вниз (тик раз в секунду, как эталон `buffListTickDecrement`);
//     D) разрыв между 1021 и 4003 снимает заявку — на новой 4003 ничего не ложится;
//     E) нулевые записи снимка (строки в БД нет): ключ 1 пропускает их ([ИСПРАВЛЕНИЕ], ревизия М-1 — шаг B7a), ключ 2 — как оригинал.
//   Итог: «ВХОД-БАФФЫ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaSessionReset.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaMiniStateSubsystem.h"
#include "Text/AsdaLang.h"
#include "Test/AsdaSelfTestKit.h"
#include "Test/AsdaSelfTestWorldScope.h"   // ★ 31.10.2026: свой павн мира — вернуть в конце (1021 обнуляет ссылку, FakeSelfSpawn спавнит нового)
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaBuffEnterTest, Log, All);

bool AsdaMountPortalReapplyOn();   // ключ `asda.Mount.PortalReapply` (World/AsdaWorldSubsystem_Peers.cpp) — аксессор

namespace AsdaBuffEnterTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.BuffEnterWorld");   // имя команды
	static const TCHAR* kKey = TEXT("asda.Buff.EnterWorldRestore");    // ключ отката правки
	constexpr int32 kBodyLen  = 686;   // тело 1021 (LoginManager `sub_415A40(1021, 686, …)`)
	constexpr int32 kSkillOff = 32;    // 28 записей состояний навыков по 12 б
	constexpr int32 kItemOff  = 368;   // 30 слотов функциональных предметов по 10 б
	constexpr int32 kZeroItemSlot = 20; // слот, оставленный в снимке нулевым (М-1); у зелий прогона другие слоты

	// Одно зелье прогона: код, ожидаемый слот (SubType строки), остаток.
	struct FPotion
	{
		int32 Code = 0;          // код `special_itemlist`
		const TCHAR* What = TEXT("");   // ярлык для лога
		int32 Slot = -1;         // SubType строки = индекс слота
		int32 Flag116 = 0;       // 2 — иконка на плашке рисуется
		int32 Remain = 0;        // остаток в снимке, сек
	};

	// Состояние прогона между шагами по таймеру.
	struct FRun
	{
		TWeakObjectPtr<UWorld> W;                    // мир прогона
		TWeakObjectPtr<UAsdaNetSubsystem> Net;       // сеть
		TWeakObjectPtr<UAsdaEffectsSubsystem> Fx;    // эффекты
		bool bQuit = false;                          // выйти по итогу
		int32 Me = 4321;                             // свой номер прогона
		int32 Ok = 0, Fail = 0;                      // счётчики
		TArray<FPotion> Potions;                     // зелья
		int32 MountCode = -1;                        // транспорт (слот 15)
		int32 SkillState = -1;                       // состояние навыка с иконкой
		TArray<uint8> Body;                          // тело 1021
		// что вернуть сети
		EAsdaNetState StateWas = EAsdaNetState::Idle;
		int32 EnterMapIdWas = -1;
		TArray<uint8> SnapshotWas;
		FString IpWas; int32 PortWas = 0;
		TUniquePtr<FAsdaNetSelfScope> SelfScope;     // свой номер/charId/уровень
		TUniquePtr<FAsdaWorldSelfScope> WorldScope;  // ★ 31.10.2026: свой павн мира — вернуть в конце (1021 обнуляет ссылку, FakeSelfSpawn спавнит нового)
		TUniquePtr<FAsdaCVarScope> CVars;            // ключ отката
		FTimerHandle Timer;                          // шаг C
		int32 RemainAtB = 0;                         // остаток зелья опыта на шаге B

		// Записать итог проверки.
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaBuffEnterTest, Log,   TEXT("ВХОД-БАФФЫ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaBuffEnterTest, Error, TEXT("ВХОД-БАФФЫ FAIL %s"), *What); }
		}
	};

	// Тело 1021: адрес сервера пуст (сеть оставит прежний), все записи пустые (как отдаёт сервер: id −1; код −1, остаток 0, −1).
	static TArray<uint8> MakeEmptyBody()
	{
		TArray<uint8> B; B.SetNumZeroed(kBodyLen);
		for (int32 i = 0; i < 28; ++i) { AsdaBytes::WrI16(B.GetData() + kSkillOff + 12 * i, -1); }
		for (int32 k = 0; k < 30; ++k)
		{
			uint8* R = B.GetData() + kItemOff + 10 * k;
			AsdaBytes::WrI32(R, -1); AsdaBytes::WrI32(R + 4, 0); AsdaBytes::WrI16(R + 8, -1);
		}
		return B;
	}

	// Подать 1021 штатным разбором и тут же снять отложенный реконнект (офлайн — коннектиться некуда); окно второго барьера
	//   самотестов 1021 закрывает (`ClearDebugSendWindow`) — взводим заново. false — появилось соединение (отказ).
	static bool Feed1021(FRun& R)
	{
		UAsdaNetSubsystem* Net = R.Net.Get();
		UWorld* W = R.W.Get();
		if (!Net || !W) { return false; }
		Net->DebugFeedPacket(1021, R.Body);
		Net->bPendingEnterWorld = false;
		FAsdaSelfTestArgs A; A.bQuit = false;
		return !AsdaSelfTestKit::RefuseStart(W, kCmd, A);
	}

	// То, что делает обработчик 4003: номер → рассылка `OnSelfSpawn`. Приёмник эффектов зовётся всегда; приёмник мира (спавн своего
	//   павна — без него мини-статус с плашкой STATE_ICON не собирается) — штатным офлайн-крюком `Asda.World.FakeSelfSpawn`, он
	//   зовёт тот же `UAsdaWorldSubsystem::OnSelfSpawn`.
	static void Simulate4003(FRun& R, bool bWorld)
	{
		if (UAsdaNetSubsystem* Net = R.Net.Get()) { Net->SetSelfUserNumForDebug((uint16)R.Me); }
		if (bWorld && GEngine && R.W.IsValid())
		{
			GEngine->Exec(R.W.Get(), TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(R.W.Get(), TEXT("Asda.World.FakeSelfSpawn 256 256"));
		}
		if (UAsdaEffectsSubsystem* Fx = R.Fx.Get()) { Fx->OnSelfSpawnCarry(); }
	}

	// Вернуть сеть и ключи, выйти.
	static void Finish(const TSharedRef<FRun>& R)
	{
		if (UAsdaNetSubsystem* Net = R->Net.Get())
		{
			Net->OnMountState.Broadcast(R->Me, 0, false);   // снять транспорт, если мир его повесил
			Net->State = R->StateWas;
			Net->EnterMapId = R->EnterMapIdWas;
			Net->EnterWorldSnapshot = R->SnapshotWas;
			Net->bPendingEnterWorld = false;
		}
		if (UAsdaEffectsSubsystem* Fx = R->Fx.Get()) { Fx->ClearCharEffects(R->Me); }
		R->SelfScope.Reset();
		R->WorldScope.Reset();
		R->CVars.Reset();
		AsdaSelfTestKit::Finish(R->W.Get(), kCmd, R->bQuit, LogAsdaBuffEnterTest, TEXT("ВХОД-БАФФЫ"), R->Ok, R->Fail);
	}

	// Шаг C (через 2,5 с) и D.
	static void StepC(const TSharedRef<FRun>& R)
	{
		UAsdaEffectsSubsystem* Fx = R->Fx.Get();
		if (!Fx || !R->Net.IsValid()) { R->Check(false, TEXT("C подсистемы исчезли")); Finish(R); return; }
		// Плашка STATE_ICON (мини-статус пересобрался тиком после сброса 1021): иконка и таймер у зелий с флагом 2 (без флага эталон иконку не рисует).
		UGameInstance* GI = R->W.IsValid() ? R->W->GetGameInstance() : nullptr;
		UAsdaMiniStateSubsystem* Mini = GI ? GI->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr;
		if (Mini && Mini->GetMiniWidget())
		{
			Mini->RefreshBuffIcons(/*bWithTimers=*/true);
			const FString Dump = Mini->DumpBuffSlots();
			TArray<FString> Lines; Dump.ParseIntoArrayLines(Lines);
			for (const FPotion& P : R->Potions)
			{
				const FString Key = FString::Printf(TEXT("id=%d typeFlag=2"), P.Code);
				const FString* L = Lines.FindByPredicate([&Key](const FString& S) { return S.Contains(Key); });
				if (P.Flag116 == 2)
				{
					const bool bIcon = L && L->Contains(TEXT("иконка=видима"));
					const bool bTimer = L && !L->Contains(TEXT("глифов таймера видимо=0"));
					R->Check(bIcon && bTimer, FString::Printf(TEXT("C2 плашка: зелье %d — иконка %s, таймер %s"), P.Code,
						bIcon ? TEXT("видна") : TEXT("НЕТ"), bTimer ? TEXT("виден") : TEXT("НЕТ")));
				}
				else
				{
					R->Check(L == nullptr, FString::Printf(TEXT("C2 плашка: зелье %d без флага 2 — иконки нет, как у эталона"), P.Code));
				}
			}
		}
		else { R->Check(false, TEXT("C2 мини-статус (плашка STATE_ICON) не готов на этой карте — иконки не проверены")); }

		const FPotion& Exp = R->Potions[0];
		const FAsdaEffectEntry* E = Fx->FindByTypeSub(R->Me, Exp.Slot, 2);
		const int32 Now = E ? E->RemainSec : -1;
		R->Check(E && Now <= R->RemainAtB - 2 && Now >= R->RemainAtB - 4,
			FString::Printf(TEXT("C1 через 2,5 с остаток зелья %d идёт вниз: %d → %d (эталон buffListTickDecrement, раз в секунду)"), Exp.Code, R->RemainAtB, Now));

		// ═══ D. Разрыв между 1021 и 4003 ═══
		if (!Feed1021(*R)) { R->Check(false, TEXT("D отказ: появилось соединение")); Finish(R); return; }
		R->Check(Fx->IsEnterWorldRestorePending(), TEXT("D1 новый 1021 взвёл заявку"));
		Fx->ResetSessionState(EAsdaSessionResetReason::Disconnected);
		R->Check(!Fx->IsEnterWorldRestorePending(), TEXT("D2 разрыв до 4003 снял заявку"));
		Simulate4003(*R, /*bWorld=*/false);
		R->Check(Fx->GetList(R->Me).Num() == 0, FString::Printf(TEXT("D3 4003 после разрыва ничего не наложила (записей %d)"), Fx->GetList(R->Me).Num()));

		// ═══ E. Ключ 2 — строго как оригинал: нулевые записи (строки CharStatus/CharSkillItem нет) тоже накладываются ═══
		R->CVars->Set(kKey, 2);
		if (!Feed1021(*R)) { R->Check(false, TEXT("E отказ: появилось соединение")); Finish(R); return; }
		Simulate4003(*R, /*bWorld=*/false);
		const FAsdaEffectEntry* Z0 = Fx->FindByTypeSub(R->Me, 0, 0);
		const FAsdaEffectEntry* Z2 = Fx->FindByTypeSub(R->Me, kZeroItemSlot, 2);
		R->Check(Z0 && Z0->RemainSec == 0 && Z2 && Z2->ItemCode == 0 && Fx->GetList(R->Me).Num() == 8,
			FString::Printf(TEXT("E1 ключ 2 (оригинал 1:1): нулевая запись навыка → состояние 0 %s, нулевой слот %d → предмет 0 %s, записей %d (ждём 8)"),
				Z0 ? TEXT("наложено") : TEXT("НЕТ"), kZeroItemSlot, Z2 ? TEXT("наложен") : TEXT("НЕТ"), Fx->GetList(R->Me).Num()));
		R->CVars->Set(kKey, 1);
		Finish(R);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestBuffEnterWorld(
	TEXT("asda.SelfTest.BuffEnterWorld"),
	TEXT("Самотест восстановления своих состояний при входе в мир (1021 +32/+368 → 4003): зелья опыта/атаки/ОЗ/защиты, состояние "
	     "навыка, транспорт, плашка иконок и таймеров, тик остатка, ключ отката asda.Buff.EnterWorldRestore. Офлайн, на карте мира. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaBuffEnterTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaEffectsSubsystem* Fx = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
		if (!Net || !Fx || !IConsoleManager::Get().FindConsoleVariable(kKey))
		{
			AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем сети/эффектов или ключа отката"));
			return;
		}
		TSharedRef<FRun> R = MakeShared<FRun>();
		R->W = W; R->Net = Net; R->Fx = Fx; R->bQuit = KA.bQuit;
		R->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		R->WorldScope = MakeUnique<FAsdaWorldSelfScope>(W, kCmd);
		R->CVars = MakeUnique<FAsdaCVarScope>(W, kCmd);
		R->Me = Net->IsSelfUserNumKnown() ? (int32)Net->SelfUserNum : 4321;
		R->StateWas = Net->State; R->EnterMapIdWas = Net->EnterMapId; R->SnapshotWas = Net->EnterWorldSnapshot;

		// ═══ 0. Подготовка: зелья из таблицы (слот = SubType строки), транспорт, состояние навыка с иконкой ═══
		struct FWant { int32 Code; const TCHAR* What; int32 Remain; };
		const FWant Wants[] = {
			{ 59, TEXT("опыт (1 час)"),       3227 },   // остаток как у живой строки CharSkillItem 75204
			{ 60, TEXT("физ. атака"),         3070 },
			{  9, TEXT("ОЗ +20%"),            1800 },
			{ 66, TEXT("защита"),             2500 } };
		for (const FWant& Wn : Wants)
		{
			FPotion P; P.Code = Wn.Code; P.What = Wn.What; P.Remain = Wn.Remain;
			FString Icon; int32 Cell = 0;
			const bool bRow = Fx->GetTable2(Wn.Code, P.Slot, P.Flag116, Icon, Cell);
			R->Check(bRow && P.Slot >= 0 && P.Slot < 30 && P.Slot != 15,
				FString::Printf(TEXT("0 зелье %d «%s»: строка special_itemlist есть, слот (SubType) %d, флаг иконки %d"), Wn.Code, Wn.What, P.Slot, P.Flag116));
			if (bRow && P.Slot >= 0 && P.Slot < 30) { R->Potions.Add(P); }
		}
		for (int32 c = 1; c < 1000 && R->MountCode < 0; ++c)
		{
			int32 St = -1, F = 0, Cl = 0; FString Ic;
			if (Fx->GetTable2(c, St, F, Ic, Cl) && St == 15) { R->MountCode = c; }
		}
		for (int32 s = 1; s < 350 && R->SkillState < 0; ++s)
		{
			FString N, A, T; int32 Cl = 0;
			if (s != 177 && Fx->GetStateInfo(s, N, A, Cl, T) && Fx->GetSkillMeta(s).bValid) { R->SkillState = s; }
		}
		TSet<int32> Slots; for (const FPotion& P : R->Potions) { Slots.Add(P.Slot); }
		R->Check(R->Potions.Num() == 4 && Slots.Num() == 4 && R->MountCode > 0 && R->SkillState > 0,
			FString::Printf(TEXT("0 набор: зелий %d в %d разных слотах, транспорт %d, состояние навыка %d"), R->Potions.Num(), Slots.Num(), R->MountCode, R->SkillState));
		if (R->Potions.Num() != 4 || Slots.Num() != 4 || R->MountCode <= 0 || R->SkillState <= 0) { Finish(R); return; }

		R->Body = MakeEmptyBody();
		for (const FPotion& P : R->Potions)
		{
			uint8* Rec = R->Body.GetData() + kItemOff + 10 * P.Slot;
			AsdaBytes::WrI32(Rec, P.Code); AsdaBytes::WrI32(Rec + 4, P.Remain); AsdaBytes::WrI16(Rec + 8, 50);
		}
		{
			uint8* Rec = R->Body.GetData() + kItemOff + 10 * 15;
			AsdaBytes::WrI32(Rec, R->MountCode); AsdaBytes::WrI32(Rec + 4, 3000); AsdaBytes::WrI16(Rec + 8, 100);
		}
		{
			// Запись `CharStatus`: Skillidx @0, Mskillidx @2, Sub @4, Count @5, Type @6, Time @7, Lvl @9, Evalue @10.
			// ★ М-1: запись 0 навыков и слот kZeroItemSlot — НУЛЕВЫЕ (так приходит 1021, когда строки в БД нет: LoginManager копирует
			//   обнулённый буфер безусловно). Ключ 1 их пропускает, ключ 2 (оригинал) накладывает — шаг E.
			FMemory::Memzero(R->Body.GetData() + kSkillOff, 12);
			FMemory::Memzero(R->Body.GetData() + kItemOff + 10 * kZeroItemSlot, 10);
			uint8* Rec = R->Body.GetData() + kSkillOff + 12 * 3;   // не первая — эталон пустые записи пропускает, а не обрывает
			AsdaBytes::WrI16(Rec, R->SkillState); AsdaBytes::WrI16(Rec + 2, 0); Rec[4] = 0; Rec[5] = 1; Rec[6] = 0;
			AsdaBytes::WrI16(Rec + 7, 1500); Rec[9] = 3; AsdaBytes::WrI16(Rec + 10, 0);
		}

		// ═══ A. Ключ 0 — прежний порт ═══
		R->CVars->Set(kKey, 0);
		if (!Feed1021(*R)) { R->Check(false, TEXT("A отказ: появилось соединение")); Finish(R); return; }
		R->Check(!Fx->IsEnterWorldRestorePending(), TEXT("A1 ключ 0: заявки нет"));
		Simulate4003(*R, /*bWorld=*/false);
		R->Check(Fx->GetList(R->Me).Num() == 0,
			FString::Printf(TEXT("A2 ключ 0 (прежний порт): после входа в мир своих состояний %d — так зелья и пропадали"), Fx->GetList(R->Me).Num()));

		// ═══ B. Ключ 1 ═══
		R->CVars->Set(kKey, 1);
		int32 MountCalls = 0, MountCodeSeen = -1;
		const FDelegateHandle HM = Net->OnMountState.AddLambda([&MountCalls, &MountCodeSeen, Me = R->Me](int32 U, int32 Code, bool bOn)
		{
			if (U == Me && bOn) { ++MountCalls; MountCodeSeen = Code; }
		});
		if (!Feed1021(*R)) { Net->OnMountState.Remove(HM); R->Check(false, TEXT("B отказ: появилось соединение")); Finish(R); return; }
		R->Check(Fx->IsEnterWorldRestorePending() && Fx->GetList(R->Me).Num() == 0,
			TEXT("B1 1021: полный сброс прошёл (список пуст), заявка восстановления взведена"));
		R->Check(Net->EnterWorldSnapshot.Num() == kBodyLen, FString::Printf(TEXT("B2 сеть сохранила снимок 1021 (%d б)"), Net->EnterWorldSnapshot.Num()));
		Simulate4003(*R, /*bWorld=*/true);
		Net->OnMountState.Remove(HM);
		R->Check(!Fx->IsEnterWorldRestorePending(), TEXT("B3 4003 сняла заявку"));
		for (const FPotion& P : R->Potions)
		{
			const FAsdaEffectEntry* E = Fx->FindByTypeSub(R->Me, P.Slot, 2);
			R->Check(E && E->ItemCode == P.Code && E->RemainSec == P.Remain,
				FString::Printf(TEXT("B4 зелье %d «%s»: запись type 2 слот %d, код %d, остаток %d с (ждём %d)"),
					P.Code, P.What, P.Slot, E ? E->ItemCode : -1, E ? E->RemainSec : -1, P.Remain));
		}
		{
			const FAsdaEffectEntry* E = Fx->FindByTypeSub(R->Me, R->SkillState, 0);
			R->Check(E && E->RemainSec == 1500 && E->Stack == 1 && E->Byte9 == 3,
				FString::Printf(TEXT("B5 состояние навыка %d: остаток %d (1500), стек %d (1), уровень %d (3)"),
					R->SkillState, E ? E->RemainSec : -1, E ? E->Stack : -1, E ? E->Byte9 : -1));
			const FAsdaEffectEntry* M = Fx->FindByTypeSub(R->Me, 15, 2);
			// ★ 31.10.2026: посадок своего при входе из выбора — ДВЕ, как у эталона: `char_restoreSkillEffectsAndMounts` в состоянии 0xA
			//   (game_main.c:2813 → `cCharStatusAddTypeMount`, entity_char.c:25569) и `charReapplyActiveSkillEffects` на шаге 12
			//   загрузчика 0xB (game_main.c:3249 → снова `cCharStatusAddTypeMount` для type 2). У порта вторую даёт повтор из своего
			//   списка (`asda.Mount.PortalReapply`, AsdaEffectsSubsystem.cpp); мир повтор отсекает по факту посадки актора. Ключ 0 — одна.
			const int32 MountWant = AsdaMountPortalReapplyOn() ? 2 : 1;
			R->Check(M && M->ItemCode == R->MountCode && MountCalls == MountWant && MountCodeSeen == R->MountCode,
				FString::Printf(TEXT("B6 транспорт %d: запись слота 15 есть, делегат посадки своего — %d раз (ждём %d: восстановление + повтор шага 12) (код %d)"),
					R->MountCode, MountCalls, MountWant, MountCodeSeen));
		}
		R->Check(Fx->GetList(R->Me).Num() == 6, FString::Printf(TEXT("B7 всего своих записей %d (4 зелья + транспорт + навык)"), Fx->GetList(R->Me).Num()));
		R->Check(!Fx->FindByTypeSub(R->Me, 0, 0) && !Fx->FindByTypeSub(R->Me, kZeroItemSlot, 2),
			FString::Printf(TEXT("B7a [ИСПРАВЛЕНИЕ] М-1: нулевая запись навыка и нулевой слот %d пропущены (состояния 0 и предмета 0 нет)"), kZeroItemSlot));

		// Имя предмета для подсказки иконки — на языке сессии.
		{
			const bool bEn = FAsdaLang::Current() == EAsdaLang::En;
			for (const FPotion& P : R->Potions)
			{
				FString Name, Text;
				Fx->GetItemTooltipText(P.Code, Name, Text);
				bool bCyr = false;
				for (TCHAR Ch : Name) { if (Ch >= 0x0400 && Ch <= 0x04FF) { bCyr = true; break; } }
				R->Check(!Name.IsEmpty() && (bEn ? !bCyr : bCyr),
					FString::Printf(TEXT("B8 подсказка зелья %d (%s): «%s»"), P.Code, FAsdaLang::Code(FAsdaLang::Current()), *Name));
			}
		}

		// Повторная 4003 (портал/смена канала) снимок НЕ переприменяет: остаток, сбитый «тиком», не возвращается к снимку.
		{
			FAsdaEffectEntry* E = Fx->FindByTypeSub(R->Me, R->Potions[0].Slot, 2);
			if (E) { E->RemainSec -= 100; }
			Simulate4003(*R, /*bWorld=*/false);
			const FAsdaEffectEntry* E2 = Fx->FindByTypeSub(R->Me, R->Potions[0].Slot, 2);
			R->Check(E2 && E2->RemainSec == R->Potions[0].Remain - 100 && Fx->GetList(R->Me).Num() == 6,
				FString::Printf(TEXT("B9 повторная 4003 (портал): ничего не наложено заново (остаток %d, записей %d)"),
					E2 ? E2->RemainSec : -1, Fx->GetList(R->Me).Num()));
			R->RemainAtB = E2 ? E2->RemainSec : 0;
		}

		// ═══ C. Тик остатка — через 2,5 с ═══
		W->GetTimerManager().SetTimer(R->Timer, FTimerDelegate::CreateLambda([R]() { StepC(R); }), 2.5f, false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
