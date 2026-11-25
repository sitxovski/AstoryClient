// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ФЛАГА ПЕТЛИ ЗВУКА (08.11.2026, пачка 18 баг 3): `asda.SelfTest.SoundLoop [delay=N] [quit]`.
//   Баг: в «Битве армий» звук захвата базы `S_c_get_motion` играет один раз. Эталон: `CCampWar_OnObjectEffectPacket@0xCA5910`
//   зовёт `Audio_Play3DSound(…, "S_c_get_motion.wav", 1)` — флаг 1 = FSOUND_LOOP_NORMAL (`CSound_LoadSample@0xFA2580`), стоп —
//   `sub_9349C7` в 6724 sub 0. Разбор и аудит всех петель: Reports/batch18/sound_loop/REPORT.md.
//   Путь игрока: 6726 (точка) и 6724 sub 1 / sub 0 через ШТАТНЫЙ диспетчер `DebugFeedPacket` → `UAsdaCampWarBattleSubsystem::On6724`.
//   Остальные три петли эталона — тем же вызовом API, что у их владельцев (бустер `PlaySound3D(…, true)`, гача и разборка
//   `PlaySound2DManaged`). Проверка — журнал петель менеджера звука («старт / повтор №N / стоп <имя>»): повтор = процент
//   проигрывания перешёл за целый круг. Контроль: `asda.Sound.LoopFlag 0` — прежнее поведение (звук один раз, «стоп … кругов 0»).
//   Нужен звук: прогон БЕЗ `-nosound` (`asda_selftest_batch.py --sound`); без устройства — ОТКАЗ с причиной.
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "UI/AsdaCampWarBattleSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWave.h"
#include "AudioDevice.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSoundLoopTest, Log, All);

namespace AsdaSoundLoopTestLocal
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.SoundLoop");   // имя команды
	static constexpr int32 kSelfCharId = 930404;                  // свой charId на время прогона (как у самотеста боя)
	static constexpr int32 kPoint = 3;                            // номер точки захвата

	// Счётчики прогона.
	struct FRun
	{
		int32 Ok = 0, Fail = 0;   // пройдено / провалено
		// Одна проверка: печать и счёт.
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaSoundLoopTest, Log, TEXT("[петля-самотест] ОК — %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaSoundLoopTest, Error, TEXT("[петля-самотест] ПРОВАЛ — %s"), *What); }
		}
	};

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		FRun R;                                       // счётчики
		TWeakObjectPtr<UWorld> W;                     // мир прогона
		bool bQuit = false;                           // выйти после итога
		float Delay = 0.f;                            // сдвиг старта
		float T = 0.f;                                // время следующего шага
		float Wait = 0.f;                             // ожидание «≥ 2 круга» самого длинного звука, с
		int32 SavedMap = 0;                           // карта до прогона
		TSharedPtr<FAsdaNetSelfScope> SelfScope;      // свой charId (RAII)
		TSharedPtr<FAsdaCVarScope> Keys;              // ключи на время прогона (RAII)
		TArray<TWeakObjectPtr<UAudioComponent>> Own;  // петли, заведённые самотестом (бустер, гача, разборка)
	};

	using AsdaSelfTestCtx::GIOf;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	using AsdaSelfTestCtx::Sub;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	using AsdaSelfTestCtx::Feed;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия отличалась только тем, что подсистема сети бралась через `Sub<UAsdaNetSubsystem>` — то же, что `AsdaTestNet::Get`.

	// Следующий шаг через Step секунд после предыдущего.
	using AsdaSelfTestCtx::Then;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// 6726 (11 б): точка, клетка, армия-владелец, state, бонус.
	static TArray<uint8> Make6726(int32 Id, int32 Owner, int32 State)
	{
		TArray<uint8> P; P.SetNumZeroed(11);
		AsdaBytes::WrI16(P.GetData(), Id); AsdaBytes::WrI16(P.GetData() + 6, Owner); P[8] = (uint8)State;
		return P;
	}
	// 6724 (7 б): точка, sub, charId захватчика.
	static TArray<uint8> Make6724(int32 Id, int32 SubCode, int32 CharId)
	{
		TArray<uint8> P; P.SetNumZeroed(7);
		AsdaBytes::WrI16(P.GetData(), Id); P[2] = (uint8)SubCode; AsdaBytes::WrI32(P.GetData() + 3, CharId);
		return P;
	}

	// Длительность волны из Content/AsdaAssets/Sound (0 — нет ассета).
	static float WaveLen(const TCHAR* Name)
	{
		const FString Path = FString::Printf(TEXT("/Game/AsdaAssets/Sound/%s.%s"), Name, Name);
		const USoundWave* Wv = LoadObject<USoundWave>(nullptr, *Path);
		return Wv ? Wv->Duration : 0.f;
	}
	// Строк журнала, начинающихся с `Prefix` и содержащих `Name`.
	static int32 Count(const TArray<FString>& J, const TCHAR* Prefix, const TCHAR* Name)
	{
		int32 N = 0;
		for (const FString& L : J) { if (L.StartsWith(Prefix) && L.Contains(FString(Name) + TEXT(" "))) { ++N; } }
		return N;
	}
	static bool Has(const TArray<FString>& J, const FString& Line)
	{
		for (const FString& L : J) { if (L.StartsWith(Line)) { return true; } }
		return false;
	}
	static void DumpJournal(const TArray<FString>& J, const TCHAR* Stage)
	{
		UE_LOG(LogAsdaSoundLoopTest, Log, TEXT("[петля-самотест] журнал (%s): %d строк"), Stage, J.Num());
		for (const FString& L : J) { UE_LOG(LogAsdaSoundLoopTest, Log, TEXT("[петля-самотест]   %s"), *L); }
	}

	static void Finish(TSharedRef<FCtx> C)
	{
		for (const TWeakObjectPtr<UAudioComponent>& A : C->Own) { if (A.IsValid()) { A->Stop(); } }
		C->Own.Reset();
		Feed(C, 6724, Make6724(kPoint, 0, kSelfCharId));   // захват снят при любом выходе
		AsdaSoundLoopJournalStop();
		if (UAsdaCampWarBattleSubsystem* Bt = Sub<UAsdaCampWarBattleSubsystem>(C)) { Bt->ResetSessionState(EAsdaSessionResetReason::EnteringWorld); }
		if (UAsdaLoadManagerSubsystem* LM = Sub<UAsdaLoadManagerSubsystem>(C)) { LM->CurrentMapId = C->SavedMap; }
		if (C->Keys) { C->Keys->Restore(); }
		if (C->SelfScope) { C->SelfScope->Restore(); }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaSoundLoopTest, TEXT("[петля-самотест]"), C->R.Ok, C->R.Fail);
	}

	static void Run(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
		UAsdaSoundSubsystem* Snd = Sub<UAsdaSoundSubsystem>(C);
		UAsdaCampWarBattleSubsystem* Bt = Sub<UAsdaCampWarBattleSubsystem>(C);
		UAsdaLoadManagerSubsystem* LM = Sub<UAsdaLoadManagerSubsystem>(C);
		if (!W || !Net || !Snd || !Bt || !LM) { AsdaSelfTestKit::Refused(W, kCmd, C->bQuit, TEXT("нет мира/подсистем сети, звука, боя, загрузки")); return; }
		if (!W->GetAudioDeviceRaw()) { AsdaSelfTestKit::Refused(W, kCmd, C->bQuit, TEXT("нет звукового устройства (-nosound) — запуск с --sound")); return; }
		C->SelfScope = MakeShared<FAsdaNetSelfScope>(Net, kCmd);
		C->Keys = MakeShared<FAsdaCVarScope>(W, kCmd);
		C->Keys->Set(TEXT("asda.CampWar.Battle"), 1);
		C->Keys->Set(TEXT("asda.Sound.LoopFlag"), 1);
		C->Keys->Set(TEXT("asda.Gacha.IdleLoop2D"), 1);
		C->SavedMap = LM->CurrentMapId;
		LM->CurrentMapId = 19;   // поле «Битвы армий» (офлайн — номер, как после травела)
		Net->SelectedCharId = kSelfCharId;
		Bt->ResetSessionState(EAsdaSessionResetReason::EnteringWorld);
		Feed(C, 6726, Make6726(kPoint, -1, 0));   // точка 3 нейтральная

		// Ждём не меньше 2.3 круга самого длинного звука (захват, бустер, гача, разборка).
		const TCHAR* Names[] = { TEXT("S_c_get_motion"), TEXT("s_item_boosting01"), TEXT("S_e_sm_ro_ready_00"), TEXT("S_e_disjoint_process") };
		float MaxLen = 0.f;
		for (const TCHAR* N : Names)
		{
			const float L = WaveLen(N);
			C->R.Expect(L > 0.f, FString::Printf(TEXT("ассет %s есть, круг %.2f с"), N, L));
			MaxLen = FMath::Max(MaxLen, L);
		}
		C->Wait = FMath::Max(1.f, MaxLen * 2.3f + 0.5f);

		// A. Флаг петли по эталону: захват (путь игрока) + три петли владельцев.
		AsdaSoundLoopJournalStart();
		Feed(C, 6724, Make6724(kPoint, 1, kSelfCharId));
		FVector Loc = FVector::ZeroVector;
		if (APlayerController* PC = W->GetFirstPlayerController()) { if (APawn* P = PC->GetPawn()) { Loc = P->GetActorLocation(); } }
		C->Own.Add(Snd->PlaySound3D(TEXT("s_item_boosting01"), Loc, 100.f, 10000.f, /*bLoop*/ true));   // = AsdaBoosterPanelSubsystem::PlayLoopStart
		C->Own.Add(Snd->PlaySound2DManaged(TEXT("S_e_sm_ro_ready_00")));                                 // = AsdaPremiumGachaSubsystem::StartIdleLoop
		C->Own.Add(Snd->PlaySound2DManaged(TEXT("S_e_disjoint_process")));                               // = AsdaDisassemblySubsystem::StartLoop

		Then(C, C->Wait, [C]()
		{
			const TArray<FString> J = AsdaSoundLoopJournalPeek();
			DumpJournal(J, TEXT("A: флаг петли 1, через ≥ 2.3 круга"));
			const TCHAR* Names[] = { TEXT("S_c_get_motion"), TEXT("s_item_boosting01"), TEXT("S_e_sm_ro_ready_00"), TEXT("S_e_disjoint_process") };
			for (const TCHAR* N : Names)
			{
				const int32 Starts = Count(J, TEXT("старт"), N), Reps = Count(J, TEXT("повтор"), N), Stops = Count(J, TEXT("стоп"), N);
				C->R.Expect(Starts == 1 && Has(J, FString::Printf(TEXT("старт %s (круг"), N)) && Reps >= 2 && Stops == 0,
					FString::Printf(TEXT("A1: %s — старт 1, повторов %d (ждём ≥ 2), стопов %d (ждём 0): петля идёт, пока не остановят"), N, Reps, Stops));
			}
			C->R.Expect(Sub<UAsdaCampWarBattleSubsystem>(C)->GetMySlot() == kPoint, TEXT("A0: 6724 sub 1 (я) → захват идёт, +0x23D4 = 3"));
			int32 Playing = 0;
			for (const TWeakObjectPtr<UAudioComponent>& A : C->Own) { if (A.IsValid() && A->IsPlaying()) { ++Playing; } }
			C->R.Expect(Playing == 3, FString::Printf(TEXT("A1: петли бустера/гачи/разборки звучат (%d из 3)"), Playing));
			// Стоп: 6724 sub 0 — `sub_9349C7(g_pSoundMgr, "S_c_get_motion.wav")`; владельцы гасят свои петли сами.
			Feed(C, 6724, Make6724(kPoint, 0, kSelfCharId));
			for (const TWeakObjectPtr<UAudioComponent>& A : C->Own) { if (A.IsValid()) { A->Stop(); } }
		});
		Then(C, 0.5f, [C]()
		{
			const TArray<FString> J = AsdaSoundLoopJournalStop();
			DumpJournal(J, TEXT("A: после стопа"));
			C->R.Expect(Sub<UAsdaCampWarBattleSubsystem>(C)->GetMySlot() == -1, TEXT("A2: 6724 sub 0 → захват окончен, +0x23D4 = −1"));
			const TCHAR* Names[] = { TEXT("S_c_get_motion"), TEXT("s_item_boosting01"), TEXT("S_e_sm_ro_ready_00"), TEXT("S_e_disjoint_process") };
			for (const TCHAR* N : Names)
			{
				C->R.Expect(Count(J, TEXT("стоп"), N) == 1, FString::Printf(TEXT("A2: %s — ровно один стоп после остановки"), N));
			}
			C->Own.Reset();
			// B. Контроль: прежнее поведение (`asda.Sound.LoopFlag 0`) — звук захвата один раз, сам кончается.
			C->Keys->Set(TEXT("asda.Sound.LoopFlag"), 0);
			AsdaSoundLoopJournalStart();
			Feed(C, 6724, Make6724(kPoint, 1, kSelfCharId));
		});
		Then(C, FMath::Max(1.f, WaveLen(TEXT("S_c_get_motion")) * 2.3f + 0.5f), [C]()
		{
			const TArray<FString> J = AsdaSoundLoopJournalStop();
			DumpJournal(J, TEXT("B: контроль, ключ 0"));
			const TCHAR* N = TEXT("S_c_get_motion");
			C->R.Expect(Has(J, TEXT("старт S_c_get_motion (круг")) && Count(J, TEXT("повтор"), N) == 0 && Has(J, TEXT("стоп S_c_get_motion (кругов 0)")),
				TEXT("B1: контроль asda.Sound.LoopFlag 0 — звук захвата играет ОДИН раз и кончается сам, захват ещё идёт (прежний баг воспроизведён)"));
			C->R.Expect(Sub<UAsdaCampWarBattleSubsystem>(C)->GetMySlot() == kPoint, TEXT("B1: захват при этом не окончен (+0x23D4 = 3)"));
			Finish(C);
		});
	}

	// asda.SelfTest.SoundLoop [delay=N] [quit] — офлайн, нужен звук (без -nosound).
	static FAutoConsoleCommandWithWorldAndArgs GAsdaSoundLoopSelfTest(
		TEXT("asda.SelfTest.SoundLoop"),
		TEXT("asda.SelfTest.SoundLoop [delay=N] [quit] — ОФЛАЙН-самотест флага петли звука (FMOD LOOP_NORMAL): захват точки «Битвы армий» "
		     "6724 sub 1 → S_c_get_motion повторяется, 6724 sub 0 → стоп; петли бустера, гачи, разборки; контроль asda.Sound.LoopFlag 0. Нужен звук (--sound)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			if (!W) { return; }
			TSharedRef<FCtx> C = MakeShared<FCtx>();
			C->W = W;
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
			C->bQuit = KitArgs.bQuit;
			C->Delay = KitArgs.Delay;
			if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs)) { return; }
			auto Start = [C]()
			{
				UWorld* Wd = C->W.Get();
				if (!Wd || !GEngine) { return; }
				if (AsdaSelfTestKit::RefuseDeferred(Wd, kCmd, C->bQuit)) { return; }
				APlayerController* P = Wd->GetFirstPlayerController();
				if (!P || !P->GetPawn())
				{
					GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
					GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256 SoundLoop"));
				}
				FTimerHandle H;
				Wd->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]() { Run(C); }), 4.f, false);
			};
			if (C->Delay <= 0.f) { Start(); return; }
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Start), C->Delay, false);
		}));
}

#endif // !UE_BUILD_SHIPPING
