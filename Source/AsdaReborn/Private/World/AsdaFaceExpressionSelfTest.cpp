// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ ВЫРАЖЕНИЯ ЛИЦА (07.11.2026): `asda.SelfTest.FaceExpr [quit]`. [ОРИГИНАЛ] Механизм — шапка `World/AsdaPlayerCharacter_Face.cpp`.
//   Офлайн (`-AsdaNoNet`), карта мира. Вход игрока: эмоции — `PlayEmotion` окна действий, соседи — пакеты 4005 / 4025.
//     0) 44 копии атласа лица есть у обоих полов;
//     A) покой: глаза закрываются через 3000 мс на 150 мс, стоит текстура `_e1`;
//     B) все 12 эмоций из боевого покоя (злое лицо): вход в эмоцию даёт обычное лицо, на эмоции нет моргания;
//     C) атака — злое лицо; выход в боевой покой — злое до конца прохода клипа, потом обычное;
//     D) сидя глаза моргают;
//     E) соседи муж и жен: эмоция 4025/100 — обычное лицо, замах — злое своего пола, покой — моргание;
//     F) ключ `asda.Char.FaceExpression 0` — обычное лицо, моргания нет;
//     G) кадры трёх выражений: `Reports/emote_face/Face_<режим>.png`.
//   Итог: «ЛИЦО ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "AsdaTestPackets.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "AsdaBytes.h"
#include "World/AsdaCoord.h"
#include "Resources/AsdaAssetPaths.h"
#include "UI/AsdaCharaInfoSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "Test/AsdaSelfTestKit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaFaceTest, Log, All);

namespace AsdaFaceTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.FaceExpr");   // имя команды
	static const TCHAR* kKey = TEXT("asda.Char.FaceExpression"); // ключ отката
	constexpr int32 kMe = 4322;      // свой userNum
	constexpr int32 kPeerM = 70;     // сосед муж
	constexpr int32 kPeerF = 71;     // соседка

	// Состояние прогона между этапами.
	struct FRun
	{
		int32 Ok = 0, Fail = 0;                 // счётчики проверок
		FAsdaNetSelfScope SelfScope;            // возврат своего номера и сети
		TUniquePtr<FAsdaCVarScope> CVars;       // возврат ключей
		FAsdaSelfTestArgs Args;                 // аргументы команды
		// Записать итог проверки.
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaFaceTest, Log,   TEXT("ЛИЦО ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaFaceTest, Error, TEXT("ЛИЦО FAIL %s"), *What); }
		}
	};

	// Наблюдение за режимом лица: сколько длился закрытый глаз и когда он начался.
	struct FWatch
	{
		double T0 = 0.0;          // начало наблюдения
		double ClosedAt = -1.0;   // первое закрытие, сек от начала
		double OpenedAt = -1.0;   // открытие после него, сек от начала
		int32 Closed = 0;         // кадров с закрытыми глазами
		FString ClosedTex;        // текстура при закрытых глазах
	};

	// Тело 4003: свой персонаж.
	using AsdaTestPackets::Body4003;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPackets.h`) — копия была дословной.

	// Тело 4005: сосед заданного пола.
	static TArray<uint8> Body4005(int32 Uidx, uint8 Sex, float GameX, float GameZ)
	{
		TArray<uint8> B; B.SetNumZeroed(151);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)Uidx);
		B[6] = 1;
		AsdaBytes::WrI32(B.GetData() + 11, 991000 + Uidx);
		const char* Name = "FaceTest";
		FMemory::Memcpy(B.GetData() + 15, Name, FCStringAnsi::Strlen(Name));
		B[37] = Sex; B[38] = 1; B[40] = 10;
		FMemory::Memcpy(B.GetData() + 56, &GameX, 4);
		FMemory::Memcpy(B.GetData() + 60, &GameZ, 4);
		return B;
	}

	// Тело 4025: userIdx, код действия, курс.
	static TArray<uint8> Body4025(int32 Uidx, int32 Action)
	{
		TArray<uint8> B; B.SetNumZeroed(13);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)Uidx);
		AsdaBytes::WrI16(B.GetData() + 2, (int16)Action);
		const float FX = 1.f, FZ = 0.f;
		FMemory::Memcpy(B.GetData() + 5, &FX, 4);
		FMemory::Memcpy(B.GetData() + 9, &FZ, 4);
		return B;
	}

	// Тело 5220: своя запись с соулмейтом (без него парные эмоции закрыты).
	static TArray<uint8> Body5220(int32 CharId, int32 SoulIdx)
	{
		TArray<uint8> B; B.SetNumZeroed(101);
		AsdaBytes::WrI32(B.GetData() + 0, CharId);
		AsdaBytes::WrI16(B.GetData() + 4, (int16)kMe);
		AsdaBytes::WrI32(B.GetData() + 6, SoulIdx);
		AsdaBytes::WrI16(B.GetData() + 23, -1);
		return B;
	}

	// Отложить шаг на `Sec` секунд игрового времени.
	static void After(UWorld* W, float Sec, TFunction<void()> Fn)
	{
		if (!W) { return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(MoveTemp(Fn)), Sec, false);
	}

	// Опрашивать режим лица `Who` каждый кадр `Sec` секунд, затем вызвать `Then`.
	static void Watch(UWorld* W, TWeakObjectPtr<AAsdaPlayerCharacter> Who, float Sec, TFunction<void(const FWatch&)> Then, FString ClosedShot = FString())
	{
		TSharedRef<FWatch> Obs = MakeShared<FWatch>();
		Obs->T0 = W->GetTimeSeconds();
		TSharedRef<TFunction<void()>> Step = MakeShared<TFunction<void()>>();
		*Step = [W, Who, Sec, Obs, Step, Then, ClosedShot]()
		{
			const double T = W->GetTimeSeconds() - Obs->T0;
			if (const AAsdaPlayerCharacter* C = Who.Get())
			{
				if (C->GetFaceMode() == 1)
				{
					++Obs->Closed;
					if (Obs->ClosedAt < 0.0)
					{
						Obs->ClosedAt = T; Obs->ClosedTex = C->GetFaceTextureShown();
						if (!ClosedShot.IsEmpty()) { FScreenshotRequest::RequestScreenshot(ClosedShot, false, false); }
					}
				}
				else if (Obs->ClosedAt >= 0.0 && Obs->OpenedAt < 0.0) { Obs->OpenedAt = T; }
			}
			if (T >= Sec) { Then(*Obs); *Step = nullptr; return; }
			W->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([Step]() { if (*Step) { (*Step)(); } }));
		};
		(*Step)();
	}

	// Закрыть прогон.
	static void Done(UWorld* W, const TSharedPtr<FRun>& R)
	{
		if (R->CVars) { R->CVars->Restore(); }
		R->SelfScope.Restore();
		// ★ R5-fix8 (14.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: соседи теста убираются (пакет 4011) — раньше оставались следующему тесту той же
		//   игры (`asda.SelfTest.PartFx` получал соседку 71 вместо своего соседа-мужчины).
		if (UAsdaNetSubsystem* NetEnd = W ? AsdaTestNet::Get(W->GetGameInstance()) : nullptr)
		{
			for (const int32 U : { kPeerM, kPeerF })
			{
				TArray<uint8> B; B.SetNumZeroed(2);   // тело 4011: userIdx (i16, младший байт первым)
				B[0] = (uint8)(U & 0xFF); B[1] = (uint8)((U >> 8) & 0xFF);
				AsdaTestNet::DebugFeedPacket(NetEnd, 4011, B);
			}
		}
		AsdaSelfTestKit::Finish(W, kCmd, R->Args.bQuit, LogAsdaFaceTest, TEXT("ЛИЦО"), R->Ok, R->Fail);
	}

	// Этап G: кадры выражений, затем конец прогона.
	static void StageFrames(UWorld* W, TSharedPtr<FRun> R, TWeakObjectPtr<AAsdaPlayerCharacter> Me)
	{
		AAsdaPlayerCharacter* C = Me.Get();
		USpringArmComponent* Arm = C ? C->FindComponentByClass<USpringArmComponent>() : nullptr;
		if (!C || !Arm) { R->Check(false, TEXT("G нет персонажа или камеры для кадров")); Done(W, R); return; }
		Arm->TargetArmLength = 130.f;
		// Лицом к камере: стрела стоит за спиной персонажа.
		C->FaceGameDir((AsdaCoord::UeToGame2D(Arm->GetSocketLocation(USpringArmComponent::SocketName)) - AsdaCoord::UeToGame2D(C->GetActorLocation())).GetSafeNormal());
		C->SetCombatStance(true);
		After(W, 0.15f, [W, R, Me]()
		{
			FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports/emote_face/Face_angry.png")), false, false);
			After(W, 8.f, [W, R, Me]()
			{
				// В стойке короткий клип покоя обнуляет таймер на каждом проходе (как эталон), моргание — уже в мирном покое.
				if (AAsdaPlayerCharacter* C1 = Me.Get()) { C1->SetCombatStance(false); }
				Watch(W, Me, 6.5f, [W, R](const FWatch& O)
				{
					R->Check(O.Closed > 0, FString::Printf(TEXT("G кадр обычного лица и моргание перед кадром, закрытых кадров %d"), O.Closed));
					FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports/emote_face/Face_normal.png")), false, false);
					After(W, 1.f, [W, R]() { Done(W, R); });
				}, FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports/emote_face/Face_closed.png")));
			});
		});
	}

	// Этап F: ключ 0.
	static void StageKeyOff(UWorld* W, TSharedPtr<FRun> R, TWeakObjectPtr<AAsdaPlayerCharacter> Me)
	{
		R->CVars->Set(kKey, 0);
		Watch(W, Me, 3.6f, [W, R, Me](const FWatch& O)
		{
			const AAsdaPlayerCharacter* C = Me.Get();
			R->Check(O.Closed == 0 && C && C->GetFaceMode() == 0 && !C->GetFaceTextureShown().EndsWith(TEXT("_e1")) && !C->GetFaceTextureShown().EndsWith(TEXT("_e2")),
				FString::Printf(TEXT("F ключ 0: моргания нет (кадров %d), лицо %s"), O.Closed, C ? *C->GetFaceTextureShown() : TEXT("?")));
			R->CVars->Set(kKey, 1);
			StageFrames(W, R, Me);
		});
	}

	// Этап E: соседи обоих полов.
	static void StagePeers(UWorld* W, TSharedPtr<FRun> R, TWeakObjectPtr<AAsdaPlayerCharacter> Me)
	{
		UGameInstance* GI = W->GetGameInstance();
		UAsdaNetSubsystem* Net = AsdaTestNet::Get(GI);
		UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
		const FVector2D P0(25650.f, 25650.f);
		AsdaTestNet::DebugFeedPacket(Net, 4005, Body4005(kPeerM, 1, P0.X + 200.f, P0.Y));
		AsdaTestNet::DebugFeedPacket(Net, 4005, Body4005(kPeerF, 2, P0.X - 200.f, P0.Y));
		TWeakObjectPtr<AAsdaPlayerCharacter> M = Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(kPeerM));
		TWeakObjectPtr<AAsdaPlayerCharacter> F = Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(kPeerF));
		R->Check(M.IsValid() && F.IsValid(), TEXT("E0 соседи заспавнены"));
		if (!M.IsValid() || !F.IsValid()) { StageKeyOff(W, R, Me); return; }
		M->PlayAttackClip(0); F->PlayAttackClip(0);
		After(W, 0.15f, [W, R, Me, M, F, Net]()
		{
			for (const TWeakObjectPtr<AAsdaPlayerCharacter>& C : { M, F })
			{
				if (!C.IsValid()) { continue; }
				const FString Tex = C->GetFaceTextureShown();
				const TCHAR* Sex = C->Sex == 2 ? TEXT("ch1_f_") : TEXT("ch1_m_");
				R->Check(C->GetFaceMode() == 2 && Tex.EndsWith(TEXT("_e2")) && Tex.StartsWith(Sex), FString::Printf(TEXT("E1 сосед пол %d, замах: режим %d, лицо %s"), (int32)C->Sex, C->GetFaceMode(), *Tex));
				C->StopAttackAnim();
				C->SetCombatStance(false);
			}
			AsdaTestNet::DebugFeedPacket(Net, 4025, Body4025(kPeerM, 100));
			AsdaTestNet::DebugFeedPacket(Net, 4025, Body4025(kPeerF, 100));
			After(W, 0.15f, [W, R, Me, M, F]()
			{
				for (const TWeakObjectPtr<AAsdaPlayerCharacter>& C : { M, F })
				{
					if (C.IsValid()) { R->Check(C->GetBusyActionCode() == 33 && C->GetFaceMode() == 0, FString::Printf(TEXT("E2 сосед пол %d, эмоция 4025/100: состояние %d, режим %d"), (int32)C->Sex, C->GetBusyActionCode(), C->GetFaceMode())); }
				}
				if (F.IsValid()) { F->StopEmotionClip(); }
				Watch(W, F, 3.6f, [W, R, Me](const FWatch& O)
				{
					R->Check(O.Closed > 0 && O.ClosedTex.EndsWith(TEXT("_e1")) && O.ClosedTex.StartsWith(TEXT("ch1_f_")), FString::Printf(TEXT("E3 соседка в покое моргает: кадров %d, лицо %s"), O.Closed, *O.ClosedTex));
					StageKeyOff(W, R, Me);
				});
			});
		});
	}

	// Этап D: сидение.
	static void StageSit(UWorld* W, TSharedPtr<FRun> R, TWeakObjectPtr<AAsdaPlayerCharacter> Me)
	{
		if (AAsdaPlayerCharacter* C = Me.Get()) { C->SetCombatStance(false); C->SetAnimState(EAsdaPlayerState::Sit); }
		After(W, 3.f, [W, R, Me]()
		{
			const AAsdaPlayerCharacter* C = Me.Get();
			R->Check(C && C->GetFaceActionState() == 8, FString::Printf(TEXT("D1 сидит, состояние %d"), C ? C->GetFaceActionState() : -1));
			Watch(W, Me, 3.6f, [W, R, Me](const FWatch& O)
			{
				R->Check(O.Closed > 0, FString::Printf(TEXT("D2 сидя глаза моргают, кадров %d"), O.Closed));
				if (AAsdaPlayerCharacter* C2 = Me.Get()) { C2->SetAnimState(EAsdaPlayerState::Idle); }
				After(W, 2.5f, [W, R, Me]() { StagePeers(W, R, Me); });
			});
		});
	}

	// Этап C: атака и выход в боевой покой.
	static void StageAttack(UWorld* W, TSharedPtr<FRun> R, TWeakObjectPtr<AAsdaPlayerCharacter> Me)
	{
		AAsdaPlayerCharacter* C = Me.Get();
		if (!C) { Done(W, R); return; }
		C->SetCombatStance(false);
		After(W, 0.2f, [W, R, Me]()
		{
			AAsdaPlayerCharacter* C1 = Me.Get();
			if (!C1) { Done(W, R); return; }
			C1->PlayAttackClip(0);
			After(W, 0.15f, [W, R, Me]()
			{
				AAsdaPlayerCharacter* C2 = Me.Get();
				if (!C2) { Done(W, R); return; }
				R->Check(C2->GetFaceActionState() == 5 && C2->GetFaceMode() == 2 && C2->GetFaceTextureShown().EndsWith(TEXT("_e2")),
					FString::Printf(TEXT("C1 атака: состояние %d, режим %d, лицо %s"), C2->GetFaceActionState(), C2->GetFaceMode(), *C2->GetFaceTextureShown()));
				C2->StopAttackAnim();
				After(W, 0.1f, [W, R, Me]()
				{
					AAsdaPlayerCharacter* C3 = Me.Get();
					if (!C3) { Done(W, R); return; }
					R->Check(C3->IsCombatStance() && C3->GetFaceMode() == 2, FString::Printf(TEXT("C2 боевой покой: режим %d"), C3->GetFaceMode()));
					After(W, 6.f, [W, R, Me]()
					{
						const AAsdaPlayerCharacter* C4 = Me.Get();
						R->Check(C4 && C4->GetFaceMode() != 2, FString::Printf(TEXT("C3 после прохода клипа злого лица нет: режим %d"), C4 ? C4->GetFaceMode() : -1));
						StageSit(W, R, Me);
					});
				});
			});
		});
	}

	// Этап B: все эмоции по очереди, каждая из злого лица боевого покоя.
	static void StageEmote(UWorld* W, TSharedPtr<FRun> R, TWeakObjectPtr<AAsdaPlayerCharacter> Me, int32 Idx)
	{
		static const int32 kState[12] = { 33, 34, 35, 36, 37, 38, 39, 41, 42, 45, 46, 44 };   // состояния `kEmoteRows`
		if (Idx >= 12) { StageAttack(W, R, Me); return; }
		AAsdaPlayerCharacter* C = Me.Get();
		if (!C) { Done(W, R); return; }
		C->SetCombatStance(true);
		After(W, 0.1f, [W, R, Me, Idx]()
		{
			AAsdaPlayerCharacter* C1 = Me.Get();
			UAsdaCharaInfoSubsystem* CI = W->GetGameInstance()->GetSubsystem<UAsdaCharaInfoSubsystem>();
			if (!C1 || !CI) { Done(W, R); return; }
			R->Check(C1->GetFaceMode() == 2, FString::Printf(TEXT("B%d перед эмоцией злое лицо: режим %d"), Idx, C1->GetFaceMode()));
			const bool bOk = CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(0, Idx));
			After(W, 0.1f, [W, R, Me, Idx, bOk]()
			{
				const AAsdaPlayerCharacter* C2 = Me.Get();
				if (!C2) { Done(W, R); return; }
				const FString Tex = C2->GetFaceTextureShown();
				R->Check(bOk && C2->GetBusyActionCode() == kState[Idx] && C2->GetFaceMode() == 0 && !Tex.EndsWith(TEXT("_e2")),
					FString::Printf(TEXT("B%d эмоция принята %d: состояние %d (ждём %d), режим %d, лицо %s"), Idx, bOk ? 1 : 0, C2->GetBusyActionCode(), kState[Idx], C2->GetFaceMode(), *Tex));
				// Первая эмоция держится дольше порога моргания только если клип длинный; проверяем весь её срок.
				Watch(W, Me, 1.2f, [W, R, Me, Idx](const FWatch& O)
				{
					R->Check(O.Closed == 0, FString::Printf(TEXT("B%d на эмоции моргания нет, кадров %d"), Idx, O.Closed));
					if (AAsdaPlayerCharacter* C3 = Me.Get()) { C3->StopEmotionClip(); }
					After(W, 0.1f, [W, R, Me, Idx]() { StageEmote(W, R, Me, Idx + 1); });
				});
			});
		});
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestFaceExpr(
	TEXT("asda.SelfTest.FaceExpr"),
	TEXT("Самотест выражения лица: моргание, все эмоции, атака, боевой покой, сидение, соседи обоих полов, ключ asda.Char.FaceExpression 0, кадры. Офлайн, на карте мира."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaFaceTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? AsdaTestNet::Get(GI) : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!Net || !WS || !GI->GetSubsystem<UAsdaCharaInfoSubsystem>() || !IConsoleManager::Get().FindConsoleVariable(kKey))
		{
			AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем или ключа отката"));
			return;
		}
		TSharedPtr<FRun> R = MakeShared<FRun>();
		R->Args = KA;
		R->SelfScope.Capture(Net, kCmd);
		R->CVars = MakeUnique<FAsdaCVarScope>(W, kCmd);
		R->CVars->Set(kKey, 1);

		// ═══ 0. Копии атласа ═══
		int32 Found = 0;
		for (const bool bMale : { true, false })
		{
			for (int32 Idx = 0; Idx <= 10; ++Idx)
			{
				for (const TCHAR* Suf : { TEXT("_e1"), TEXT("_e2") })
				{
					Found += LoadObject<UTexture2D>(nullptr, *AsdaAssetPaths::CharTextureObject(AsdaAssetPaths::FaceTextureName(bMale, Idx) + Suf)) ? 1 : 0;
				}
			}
		}
		R->Check(Found == 44, FString::Printf(TEXT("0 копий атласа лица: %d (ждём 44)"), Found));

		// ═══ Свой персонаж и соулмейт ═══
		GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
		AsdaTestNet::DebugFeedPacket(Net, 4003, Body4003(kMe));
		AAsdaPlayerCharacter* Me = Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get());
		if (AsdaTestNet::SelectedCharId(Net) == 0) { AsdaTestNet::SelectedCharId(Net) = 777002; }
		AsdaTestNet::DebugFeedPacket(Net, 5220, Body5220(AsdaTestNet::SelectedCharId(Net), 5));
		R->Check(Me != nullptr, FString::Printf(TEXT("A0 свой персонаж, пол %d"), Me ? (int32)Me->Sex : -1));
		if (!Me) { Done(W, R); return; }
		TWeakObjectPtr<AAsdaPlayerCharacter> WMe = Me;

		// ═══ A. Моргание в покое ═══
		After(W, 0.3f, [W, R, WMe]()
		{
			Watch(W, WMe, 7.f, [W, R, WMe](const FWatch& O)
			{
				const double Dur = O.OpenedAt - O.ClosedAt;
				R->Check(O.ClosedAt > 0.0 && O.ClosedAt <= 3.2 && O.ClosedTex.EndsWith(TEXT("_e1")), FString::Printf(TEXT("A1 глаза закрылись на %.2f с наблюдения, лицо %s"), O.ClosedAt, *O.ClosedTex));
				R->Check(Dur >= 0.13 && Dur <= 0.3, FString::Printf(TEXT("A2 закрыты %.3f с (эталон 0.15)"), Dur));
				StageEmote(W, R, WMe, 0);
			});
		});
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
