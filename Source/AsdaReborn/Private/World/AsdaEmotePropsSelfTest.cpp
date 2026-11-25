// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ РЕКВИЗИТА ЭМОЦИЙ (29.10.2026): `asda.SelfTest.EmoteProps [quit]`. [ОРИГИНАЛ]
//   Жалоба игроков: «Juggling emote is missing the balls». Реверс — у объявления `UAsdaEffectsSubsystem::SpawnEmoteProps`.
//   Офлайн (`-AsdaNoNet`), карта мира. Реальный путь (закон diagnostic-must-use-real-path): свой игрок — `PlayEmotion`
//   (тот же вход, что окно действий / клавиши / панель / чат), соседи — пакеты 4005 / 4025 в общий разбор входящих
//   (`DebugFeedPacket`), соулмейт — 5220 (без него парные эмоции закрыты нотисом 6035).
//     0) пять BP реквизита собраны (`BP_Fx_m_ball`, `m_flower01/02`, `f_flower01/02`);
//     A) свой: жонглирование → ОДИН `m_ball`, attach-6 (мировая матрица персонажа, без кости);
//     B) соседи муж/жен: 4025/116 → по одному `m_ball` (у обоих полов один файл); 4025/115 → ДВА цветка своего пола на
//        кости `_8` (кость есть в скелете); прочие эмоции (100) — реквизита нет;
//     C) прерывание: сосед 4025/1 (ход) → его реквизит гаснет на следующем тике; конец клипа (1.6 с) — гаснет и свой;
//     D) свой ухаживание → два цветка своего пола; ключ `asda.Emote.Props 0` (прежний порт) — у себя реквизита нет;
//     E) кадры: своё жонглирование крупно, 3 момента клипа (`Reports/emote_props/EmoteProps_juggling_<k>.png`) + рамка мячей в логе.
//   Итог: «РЕКВИЗИТ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "AsdaTestPackets.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "AsdaBytes.h"
#include "AsdaEffectActor.h"
#include "UI/AsdaCharaInfoSubsystem.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "Test/AsdaSelfTestKit.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaEmotePropsTest, Log, All);

namespace AsdaEmotePropsTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.EmoteProps");   // имя команды
	static const TCHAR* kKey = TEXT("asda.Emote.Props");           // ключ отката
	constexpr int32 kMe = 4321;        // свой userNum
	constexpr int32 kPeerMJ = 60;      // сосед муж — жонглирование
	constexpr int32 kPeerFJ = 61;      // соседка — жонглирование
	constexpr int32 kPeerMC = 62;      // сосед муж — ухаживание
	constexpr int32 kPeerFC = 63;      // соседка — ухаживание
	constexpr int32 kPeerHappy = 64;   // сосед — эмоция без реквизита (100 «радость»)

	// Состояние прогона, живущее между этапами таймера.
	struct FRun
	{
		int32 Ok = 0, Fail = 0;                        // счётчики проверок
		FAsdaNetSelfScope SelfScope;                   // возврат своего номера/сети после прогона
		TUniquePtr<FAsdaCVarScope> CVars;              // возврат ключей после прогона
		FAsdaSelfTestArgs Args;                        // аргументы команды (`quit`)
		// Записать итог проверки.
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaEmotePropsTest, Log,   TEXT("РЕКВИЗИТ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaEmotePropsTest, Error, TEXT("РЕКВИЗИТ FAIL %s"), *What); }
		}
	};

	// Живые эффект-акторы `Jeb` на носителе `Host` (не уничтожаемые). `bCharWorld`/`Bone` — для проверки режима привязки.
	struct FFxCount { int32 N = 0; int32 CharWorld = 0; int32 OnBone = 0; };
	static FFxCount CountFx(UWorld* W, const AActor* Host, const TCHAR* Jeb, FName Bone = NAME_None)
	{
		FFxCount C;
		if (!W || !Host) { return C; }
		for (TActorIterator<AAsdaEffectActor> It(W); It; ++It)
		{
			const AAsdaEffectActor* A = *It;
			if (!IsValid(A) || A->IsActorBeingDestroyed() || A->FollowActor.Get() != Host) { continue; }
			if (!A->EffectName.Equals(Jeb, ESearchCase::IgnoreCase)) { continue; }
			++C.N;
			if (A->bFollowCharWorld && !A->bFollowBoneFull) { ++C.CharWorld; }
			if (A->bFollowBoneFull && !Bone.IsNone() && A->FollowBone == Bone) { ++C.OnBone; }
		}
		return C;
	}

	// Все живые эффекты реквизита на носителе (любой из пяти файлов).
	static int32 CountAnyProp(UWorld* W, const AActor* Host)
	{
		return CountFx(W, Host, TEXT("m_ball")).N + CountFx(W, Host, TEXT("m_flower01")).N + CountFx(W, Host, TEXT("m_flower02")).N
			+ CountFx(W, Host, TEXT("f_flower01")).N + CountFx(W, Host, TEXT("f_flower02")).N;
	}

	// Тело 4003 (9 б): userNum, тайлы, состояние, байт показа шлема.
	using AsdaTestPackets::Body4003;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPackets.h`) — копия была дословной.

	// Тело 4005 (151 б): сосед заданного пола без снаряжения.
	static TArray<uint8> Body4005(int32 Uidx, uint8 Sex, float GameX, float GameZ)
	{
		TArray<uint8> B; B.SetNumZeroed(151);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)Uidx);
		B[6] = 1;
		AsdaBytes::WrI32(B.GetData() + 11, 990000 + Uidx);   // charId — заведомо не свой
		const char* Name = "EmoteTest";
		FMemory::Memcpy(B.GetData() + 15, Name, FCStringAnsi::Strlen(Name));
		B[37] = Sex; B[38] = 1; B[40] = 10;                   // пол, класс, уровень
		FMemory::Memcpy(B.GetData() + 56, &GameX, 4);
		FMemory::Memcpy(B.GetData() + 60, &GameZ, 4);
		return B;
	}

	// Тело 4025 (13 б): userIdx, код действия, курс (1, 0).
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

	// 5220 своей записи (101 б): @0 charId, @4 userIdx, @6 номер пары (>0 — соулмейт есть), @23 userIdx партнёра.
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

	// Отложить шаг и дождаться ещё одного кадра: первый кадр после тяжёлого синхронного этапа длинный, таймер срабатывает в нём
	//   ДО тика подсистем (FTickableGameObject) — проверка «погашено на следующем тике» должна идти кадром позже.
	static void AfterTick(UWorld* W, float Sec, TFunction<void()> Fn)
	{
		After(W, Sec, [W, Fn = MoveTemp(Fn)]() mutable { After(W, 0.05f, MoveTemp(Fn)); });
	}

	// Закрыть прогон: снять ключи/сеть, итог, выход по `quit`.
	static void Done(UWorld* W, const TSharedPtr<FRun>& R)
	{
		if (R->CVars) { R->CVars->Restore(); }
		R->SelfScope.Restore();
		AsdaSelfTestKit::Finish(W, kCmd, R->Args.bQuit, LogAsdaEmotePropsTest, TEXT("РЕКВИЗИТ"), R->Ok, R->Fail);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestEmoteProps(
	TEXT("asda.SelfTest.EmoteProps"),
	TEXT("Самотест реквизита эмоций: мячи жонглирования (M_Ball, attach-6) и цветы ухаживания (кость _8) у своего игрока и у соседей "
	     "обоих полов (4025/116, 4025/115), снятие при ходе и по концу клипа, ключ asda.Emote.Props 0, кадр. Офлайн, на карте мира. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaEmotePropsTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? AsdaTestNet::Get(GI) : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaCharaInfoSubsystem* CI = GI ? GI->GetSubsystem<UAsdaCharaInfoSubsystem>() : nullptr;
		if (!Net || !WS || !CI || !GI->GetSubsystem<UAsdaEffectsSubsystem>() || !IConsoleManager::Get().FindConsoleVariable(kKey))
		{
			AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем сети/мира/окна действий/эффектов или ключа отката"));
			return;
		}
		TSharedPtr<FRun> R = MakeShared<FRun>();
		R->Args = KA;
		R->SelfScope.Capture(Net, kCmd);
		R->CVars = MakeUnique<FAsdaCVarScope>(W, kCmd);
		R->CVars->Set(kKey, 1);

		// ═══ 0. BP реквизита собраны ═══
		const TCHAR* Jebs[5] = { TEXT("m_ball"), TEXT("m_flower01"), TEXT("m_flower02"), TEXT("f_flower01"), TEXT("f_flower02") };
		for (const TCHAR* J : Jebs)
		{
			R->Check(AAsdaEffectActor::ResolveJebClass(J) != nullptr, FString::Printf(TEXT("0 BP_Fx_%s собран"), J));
		}

		// ═══ Свой персонаж + соулмейт ═══
		GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
		AsdaTestNet::DebugFeedPacket(Net, 4003, Body4003(kMe));
		AAsdaPlayerCharacter* Me = Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get());
		if (AsdaTestNet::SelectedCharId(Net) == 0) { AsdaTestNet::SelectedCharId(Net) = 777001; }   // офлайн charId нет — свой на время прогона
		AsdaTestNet::DebugFeedPacket(Net, 5220, Body5220(AsdaTestNet::SelectedCharId(Net), 5));
		R->Check(Me != nullptr, FString::Printf(TEXT("A0 свой персонаж %s (пол %d)"), Me ? TEXT("есть") : TEXT("НЕТ"), Me ? (int32)Me->Sex : -1));
		if (!Me) { Done(W, R); return; }

		// ═══ A. Свой: жонглирование ═══
		const bool bOkJ = CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(0, 9));
		const FFxCount MeBall = CountFx(W, Me, TEXT("m_ball"));
		R->Check(bOkJ && Me->GetBusyActionCode() == 45, FString::Printf(TEXT("A1 PlayEmotion(жонглирование): принят %d, состояние %d (ждём 45)"),
			bOkJ ? 1 : 0, Me->GetBusyActionCode()));
		R->Check(MeBall.N == 1 && MeBall.CharWorld == 1, FString::Printf(TEXT("A2 у себя мячей m_ball: %d (ждём 1), из них attach-6 %d (ждём 1)"),
			MeBall.N, MeBall.CharWorld));

		// ═══ B. Соседи обоих полов ═══
		const FVector2D P0(25650.f, 25650.f);   // тайл 256 → 256*100+50
		AsdaTestNet::DebugFeedPacket(Net, 4005, Body4005(kPeerMJ, 1, P0.X + 200.f, P0.Y));
		AsdaTestNet::DebugFeedPacket(Net, 4005, Body4005(kPeerFJ, 2, P0.X - 200.f, P0.Y));
		AsdaTestNet::DebugFeedPacket(Net, 4005, Body4005(kPeerMC, 1, P0.X, P0.Y + 200.f));
		AsdaTestNet::DebugFeedPacket(Net, 4005, Body4005(kPeerFC, 2, P0.X, P0.Y - 200.f));
		AsdaTestNet::DebugFeedPacket(Net, 4005, Body4005(kPeerHappy, 1, P0.X + 300.f, P0.Y + 300.f));
		AAsdaPlayerCharacter* MJ = Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(kPeerMJ));
		AAsdaPlayerCharacter* FJ = Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(kPeerFJ));
		AAsdaPlayerCharacter* MC = Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(kPeerMC));
		AAsdaPlayerCharacter* FC = Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(kPeerFC));
		AAsdaPlayerCharacter* HP = Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(kPeerHappy));
		R->Check(MJ && FJ && MC && FC && HP, TEXT("B0 пять соседей заспавнены (4005)"));
		if (!(MJ && FJ && MC && FC && HP)) { Done(W, R); return; }
		R->Check(MJ->Sex == 1 && FJ->Sex == 2 && MC->Sex == 1 && FC->Sex == 2,
			FString::Printf(TEXT("B0a пол соседей: %d %d %d %d (ждём 1 2 1 2)"), (int32)MJ->Sex, (int32)FJ->Sex, (int32)MC->Sex, (int32)FC->Sex));

		AsdaTestNet::DebugFeedPacket(Net, 4025, Body4025(kPeerMJ, 116));
		AsdaTestNet::DebugFeedPacket(Net, 4025, Body4025(kPeerFJ, 116));
		const FFxCount BM = CountFx(W, MJ, TEXT("m_ball")), BF = CountFx(W, FJ, TEXT("m_ball"));
		R->Check(BM.N == 1 && BM.CharWorld == 1 && MJ->GetBusyActionCode() == 45,
			FString::Printf(TEXT("B1 сосед муж 4025/116: m_ball %d (1), attach-6 %d (1), состояние %d (45)"), BM.N, BM.CharWorld, MJ->GetBusyActionCode()));
		R->Check(BF.N == 1 && BF.CharWorld == 1 && FJ->GetBusyActionCode() == 45,
			FString::Printf(TEXT("B2 соседка 4025/116: m_ball %d (1), attach-6 %d (1), состояние %d (45)"), BF.N, BF.CharWorld, FJ->GetBusyActionCode()));

		const FName B8(TEXT("Null_8"));   // эталонная `_8` — в UE `Null_8` (импорт переименовал «_N»)
		AsdaTestNet::DebugFeedPacket(Net, 4025, Body4025(kPeerMC, 115));
		AsdaTestNet::DebugFeedPacket(Net, 4025, Body4025(kPeerFC, 115));
		const FFxCount M1 = CountFx(W, MC, TEXT("m_flower01"), B8), M2 = CountFx(W, MC, TEXT("m_flower02"), B8);
		const FFxCount F1 = CountFx(W, FC, TEXT("f_flower01"), B8), F2 = CountFx(W, FC, TEXT("f_flower02"), B8);
		R->Check(M1.N == 1 && M2.N == 1 && M1.OnBone == 1 && M2.OnBone == 1 && CountAnyProp(W, MC) == 2,
			FString::Printf(TEXT("B3 сосед муж 4025/115: m_flower01 %d, m_flower02 %d (по 1, на _8: %d/%d), всего реквизита %d (ждём 2)"),
				M1.N, M2.N, M1.OnBone, M2.OnBone, CountAnyProp(W, MC)));
		R->Check(F1.N == 1 && F2.N == 1 && F1.OnBone == 1 && F2.OnBone == 1 && CountAnyProp(W, FC) == 2,
			FString::Printf(TEXT("B4 соседка 4025/115: f_flower01 %d, f_flower02 %d (по 1, на _8: %d/%d), всего реквизита %d (ждём 2)"),
				F1.N, F2.N, F1.OnBone, F2.OnBone, CountAnyProp(W, FC)));
		{
			const USkeletalMeshComponent* SkM = MC->BaseSkel;
			const USkeletalMeshComponent* SkF = FC->BaseSkel;
			R->Check(SkM && SkF && SkM->DoesSocketExist(B8) && SkF->DoesSocketExist(B8), TEXT("B5 кость _8 (в UE Null_8) есть в скелете обоих полов"));
		}
		AsdaTestNet::DebugFeedPacket(Net, 4025, Body4025(kPeerHappy, 100));
		R->Check(HP->GetBusyActionCode() == 33 && CountAnyProp(W, HP) == 0,
			FString::Printf(TEXT("B6 эмоция без реквизита (4025/100): состояние %d (33), реквизита %d (0)"), HP->GetBusyActionCode(), CountAnyProp(W, HP)));

		// ═══ C. Прерывание ходом: сосед муж-жонглёр уходит в ход 4025/1 ═══
		AsdaTestNet::DebugFeedPacket(Net, 4025, Body4025(kPeerMJ, 1));
		TWeakObjectPtr<AAsdaPlayerCharacter> WMe(Me), WMJ(MJ), WFJ(FJ), WMC(MC);
		AfterTick(W, 0.25f, [W, R, WMe, WMJ, WFJ, WMC]()
		{
			R->Check(WMJ.IsValid() && CountFx(W, WMJ.Get(), TEXT("m_ball")).N == 0,
				FString::Printf(TEXT("C1 сосед ушёл в ход (4025/1): его m_ball погашен (%d, ждём 0)"), WMJ.IsValid() ? CountFx(W, WMJ.Get(), TEXT("m_ball")).N : -1));
			R->Check(WFJ.IsValid() && CountFx(W, WFJ.Get(), TEXT("m_ball")).N == 1,
				TEXT("C2 соседка без хода — её m_ball ещё жонглирует (клип 1.67 с)"));
			R->Check(WMe.IsValid() && CountFx(W, WMe.Get(), TEXT("m_ball")).N == 1, TEXT("C3 свой m_ball жив до конца клипа"));
			// Конец клипа жонглирования (1.6 с) — свой мяч гаснет по уходу из 45 (эталон: состояние 0 по концу прохода).
			After(W, 1.75f, [W, R, WMe, WMC]()
			{
				AAsdaPlayerCharacter* Me2 = WMe.Get();
				R->Check(Me2 && Me2->GetBusyActionCode() != 45 && CountFx(W, Me2, TEXT("m_ball")).N == 0,
					FString::Printf(TEXT("C4 конец клипа: своё состояние %d (не 45), m_ball %d (ждём 0)"),
						Me2 ? Me2->GetBusyActionCode() : -1, Me2 ? CountFx(W, Me2, TEXT("m_ball")).N : -1));
				R->Check(WMC.IsValid() && CountAnyProp(W, WMC.Get()) == 2, TEXT("C5 ухаживание соседа (6.9 с) — цветы ещё на месте"));
				if (!Me2) { Done(W, R); return; }

				// ═══ D. Свой: ухаживание и ключ отката ═══
				UGameInstance* GI2 = W->GetGameInstance();
				UAsdaCharaInfoSubsystem* CI2 = GI2 ? GI2->GetSubsystem<UAsdaCharaInfoSubsystem>() : nullptr;
				const bool bMale = (Me2->Sex == 1);
				const bool bOkC = CI2 && CI2->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(0, 11));
				const FName B8b(TEXT("Null_8"));
				const FFxCount C1 = CountFx(W, Me2, bMale ? TEXT("m_flower01") : TEXT("f_flower01"), B8b);
				const FFxCount C2 = CountFx(W, Me2, bMale ? TEXT("m_flower02") : TEXT("f_flower02"), B8b);
				R->Check(bOkC && Me2->GetBusyActionCode() == 44 && C1.OnBone == 1 && C2.OnBone == 1 && CountAnyProp(W, Me2) == 2,
					FString::Printf(TEXT("D1 своё ухаживание (пол %d): состояние %d (44), цветов %d+%d на _8 (1+1), всего %d (2)"),
						(int32)Me2->Sex, Me2->GetBusyActionCode(), C1.OnBone, C2.OnBone, CountAnyProp(W, Me2)));
				// Прерывание своего — ход (тот же путь, что клик по земле: `SetAnimState(Move)` → `StopEmotionClip`).
				Me2->SetAnimState(EAsdaPlayerState::Move);
				Me2->SetAnimState(EAsdaPlayerState::Idle);
				AfterTick(W, 0.25f, [W, R, WMe]()
				{
					AAsdaPlayerCharacter* Me3 = WMe.Get();
					R->Check(Me3 && CountAnyProp(W, Me3) == 0, FString::Printf(TEXT("D2 свой ход прервал ухаживание: реквизита %d (ждём 0)"),
						Me3 ? CountAnyProp(W, Me3) : -1));
					if (!Me3) { Done(W, R); return; }
					UAsdaCharaInfoSubsystem* CI3 = W->GetGameInstance()->GetSubsystem<UAsdaCharaInfoSubsystem>();
					R->CVars->Set(kKey, 0);
					const bool bOkL = CI3->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(0, 9));
					R->Check(bOkL && Me3->GetBusyActionCode() == 45 && CountFx(W, Me3, TEXT("m_ball")).N == 0,
						FString::Printf(TEXT("D3 ключ asda.Emote.Props 0 (прежний порт): своё жонглирование без мячей (%d)"), CountFx(W, Me3, TEXT("m_ball")).N));
					R->CVars->Set(kKey, 1);

					// ═══ E. Кадры: своё жонглирование крупно (стрела камеры 170 — нижний предел зума), соседи скрыты на время кадра ═══
					After(W, 1.9f, [W, R, WMe]()
					{
						AAsdaPlayerCharacter* Me4 = WMe.Get();
						if (!Me4) { Done(W, R); return; }
						UGameInstance* GI4 = W->GetGameInstance();
						UAsdaWorldSubsystem* WS4 = GI4->GetSubsystem<UAsdaWorldSubsystem>();
						for (int32 U : { kPeerMJ, kPeerFJ, kPeerMC, kPeerFC, kPeerHappy })
						{
							if (AActor* P = WS4->ActorByUserIdx(U)) { P->SetActorHiddenInGame(true); }
						}
						TWeakObjectPtr<USpringArmComponent> Arm = Me4->FindComponentByClass<USpringArmComponent>();
						const float ArmWas = Arm.IsValid() ? Arm->TargetArmLength : 0.f;
						if (Arm.IsValid()) { Arm->TargetArmLength = 170.f; }
						const bool bOkS = GI4->GetSubsystem<UAsdaCharaInfoSubsystem>()->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(0, 9));
						R->Check(bOkS && CountFx(W, Me4, TEXT("m_ball")).N == 1, TEXT("E1 жонглирование для кадра запущено, мячи есть"));
						// Кадр k: снимок + положение геометрии мячей относительно персонажа (мировые вершины эффекта → рамка компонентов).
						TSharedRef<TFunction<void(int32)>> Shot = MakeShared<TFunction<void(int32)>>();
						*Shot = [W, R, WMe, Arm, ArmWas, Shot](int32 K)
						{
							AAsdaPlayerCharacter* Me5 = WMe.Get();
							if (!Me5) { Done(W, R); return; }
							for (TActorIterator<AAsdaEffectActor> It(W); It; ++It)
							{
								if (It->FollowActor.Get() != Me5 || !It->EffectName.Equals(TEXT("m_ball"), ESearchCase::IgnoreCase)) { continue; }
								const FBox Bx = It->GetComponentsBoundingBox(/*bNonColliding*/true);
								const FVector L = Me5->GetActorLocation();
								const FVector H = Me5->BaseSkel ? Me5->BaseSkel->GetSocketLocation(FName(TEXT("8_a1"))) : L;
								UE_LOG(LogAsdaEmotePropsTest, Log, TEXT("РЕКВИЗИТ кадр %d: рамка мячей центр %s размер %s | персонаж %s | кисть 8_a1 %s"),
									K, *Bx.GetCenter().ToCompactString(), *Bx.GetSize().ToCompactString(), *L.ToCompactString(), *H.ToCompactString());
							}
							// Абсолютный путь: пакетный прогон уводит Saved в песочницу и удаляет её после итога.
							const FString Png = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()
								/ FString::Printf(TEXT("../Reports/emote_props/EmoteProps_juggling_%d.png"), K));
							FScreenshotRequest::RequestScreenshot(Png, /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
							UE_LOG(LogAsdaEmotePropsTest, Log, TEXT("РЕКВИЗИТ кадр → %s"), *Png);
							if (K < 3) { After(W, 0.4f, [Shot, K]() { (*Shot)(K + 1); }); return; }
							if (Arm.IsValid()) { Arm->TargetArmLength = ArmWas; }
							After(W, 1.5f, [W, R, Shot]() { *Shot = nullptr; Done(W, R); });   // обнулить — разорвать кольцевую ссылку
						};
						After(W, 0.3f, [Shot]() { (*Shot)(1); });
					});
				});
			});
		});
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
