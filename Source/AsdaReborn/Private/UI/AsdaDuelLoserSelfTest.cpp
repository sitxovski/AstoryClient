// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ 31.10.2026: `asda.UI.SelfTestDuelLoser [delay=N] [shots] [quit]` — жест поражения дуэли и флаги дуэли (путь игрока, офлайн).
//   Разбор — `Reports/duel/DUEL_FLOW.md`. Входы штатные: пакеты 4305/4304 — `UAsdaNetSubsystem::DebugFeedPacket` (тот же разбор,
//   что у сервера); ход — `AAsdaPlayerController::RequestMoveTo` (вход клика по земле); эмоция — окно действий `PlayEmotion`; сесть —
//   `ToggleSitStand`; навык — окно навыков `UseFromSkillWindow`; предмет — сумка `UseConsumable`; отправки — под перехватом (в сеть не идут).
//   Проверяет:
//     F1 4305 → флаг pvp_flag_land в центре арены (клип по кругу), флаги pvp_flag_back за спиной обоих участников (кость "2", видимы);
//     L1 4304 (проигравший — я) → флаги сняты, поза 51 (клип anim_99), [ИСПРАВЛЕНИЕ `asda.Duel.LoserLock`]: ход, эмоция, сесть, навык,
//        предмет не уходят до конца жеста; L2 после конца клипа (4.267 с) — снова можно (контроль: 4006 уходит);
//     N1 сосед проиграл (4304, проигравший — сосед): у него поза 51 и клип anim_99, у меня блокировки нет;
//     K0 контроль `asda.Duel.LoserLock 0`: жест прерывается ходом (как оригинал).
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING
#include "Test/AsdaSelfTestSelfReady.h"   // ★ 01.11.2026: изоляция пакета — свой персонаж заводится и без ссылки мира/номера

#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaDuelSubsystem.h"
#include "UI/AsdaCharaInfoSubsystem.h"
#include "UI/AsdaSkillUseSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaCoord.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/OutputDevice.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaDuelLoserTest, Log, All);

namespace AsdaDuelLoserTestLocal
{
	static const TCHAR* kCmd = TEXT("asda.UI.SelfTestDuelLoser");   // имя команды
	static constexpr int32 kSelfCharId = 940001;   // свой charId на время прогона
	static constexpr int32 kSelfNum = 91;          // свой userNum
	static constexpr int32 kPeerNum = 4411;        // userNum соседа (фейковый сосед → charId 900000 + userNum)
	static constexpr int32 kPeerCharId = 900000 + kPeerNum;

	// Перехват строк журнала (отказы гейтов).
	struct FLogCatch : public FOutputDevice
	{
		TArray<FString> Lines;   // строки с меткой
		virtual bool CanBeUsedOnAnyThread() const override { return true; }   // строки любого потока (вместе со строкой ниже)
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }   // ★ 05.11.2026: НЕбуферизованный список UE 5.8 — строка доходит в момент записи (иначе гонка с потоком логов: строка после RemoveOutputDevice/проверки)
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName&) override
		{
			if (!IsInGameThread()) { return; }   // ★ 05.11.2026: устройство синхронное (CanBeUsedOnMultipleThreads) — ловим только строки игрового потока: их пишет проверяемый код, читает самотест, замка у полей нет
			const FString S(V);
			if (S.Contains(TEXT("не применён")) || S.Contains(TEXT("жест поражения"))) { Lines.Add(S); }
		}
		// Есть ли строка с подстрокой.
		bool Has(const TCHAR* Sub) const
		{
			GLog->FlushThreadedLogs();
			for (const FString& L : Lines) { if (L.Contains(Sub)) { return true; } }
			return false;
		}
	};

	// Контекст прогона.
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                    // счётчики
		TWeakObjectPtr<UWorld> W;                  // мир
		float Delay = 0.f;                         // сдвиг старта
		bool bQuit = false;                        // выйти после итога
		FString ShotDir;                           // снимки (`shots`)
		float T = 0.f;                             // время следующего шага
		TSharedPtr<TArray<TPair<int32, TArray<uint8>>>> Sent;   // перехваченные отправки
		uint32 CapGen = 0;                         // поколение перехвата
		TSharedPtr<FAsdaNetSelfScope> SelfScope;   // свой персонаж (RAII)
		TSharedPtr<FAsdaCVarScope> Keys;           // ключи (RAII)
		bool bWasInGame = false;                   // `bInGameConnected` до прогона
		int32 Cell = -1;                           // ячейка сумки с пробным предметом
		// Проверка.
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaDuelLoserTest, Log, TEXT("[дуэль-самотест] ОК — %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaDuelLoserTest, Error, TEXT("[дуэль-самотест] ПРОВАЛ — %s"), *What); }
		}
	};
	using FCtxRef = TSharedRef<FCtx>;

	static UGameInstance* GIOf(const FCtxRef& C) { return C->W.IsValid() ? C->W->GetGameInstance() : nullptr; }
	template <class T> static T* Sub(const FCtxRef& C) { UGameInstance* GI = GIOf(C); return GI ? GI->GetSubsystem<T>() : nullptr; }
	static void Feed(const FCtxRef& C, int32 Op, const TArray<uint8>& P) { if (UAsdaNetSubsystem* N = Sub<UAsdaNetSubsystem>(C)) { N->DebugFeedPacket(Op, P); } }
	static int32 CountOp(const FCtxRef& C, int32 Op) { int32 N = 0; if (C->Sent) { for (const auto& P : *C->Sent) { if (P.Key == Op) { ++N; } } } return N; }
	static AAsdaPlayerController* PCOf(const FCtxRef& C) { return C->W.IsValid() ? Cast<AAsdaPlayerController>(C->W->GetFirstPlayerController()) : nullptr; }
	static AAsdaPlayerCharacter* Me(const FCtxRef& C) { UAsdaWorldSubsystem* WS = Sub<UAsdaWorldSubsystem>(C); return WS ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get()) : nullptr; }
	static AAsdaPlayerCharacter* Peer(const FCtxRef& C) { UAsdaWorldSubsystem* WS = Sub<UAsdaWorldSubsystem>(C); return WS ? Cast<AAsdaPlayerCharacter>(WS->FindPlayerActorByCharId(kPeerCharId)) : nullptr; }
	// Следующий шаг через Step с.
	using AsdaSelfTestCtx::Then;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	// Снимок (только `shots`).
	static void Shot(const FCtxRef& C, const TCHAR* Name)
	{
		if (C->ShotDir.IsEmpty()) { return; }
		FScreenshotRequest::RequestScreenshot(C->ShotDir / FString::Printf(TEXT("%s.png"), Name), true, false);
	}
	// Клип персонажа — `anim_<Idx>`.
	static bool ClipIs(const AAsdaPlayerCharacter* Ch, int32 Idx)
	{
		const UAnimSingleNodeInstance* SN = (Ch && Ch->BaseSkel) ? Ch->BaseSkel->GetSingleNodeInstance() : nullptr;
		const UAnimSequenceBase* A = SN ? Cast<UAnimSequenceBase>(SN->GetAnimationAsset()) : nullptr;
		return A && A->GetName().EndsWith(FString::Printf(TEXT("anim_%d"), Idx));
	}
	// Флаг играет свой клип по кругу.
	static bool FlagAnimLoops(const USkeletalMeshComponent* F)
	{
		const UAnimSingleNodeInstance* SN = F ? F->GetSingleNodeInstance() : nullptr;
		return SN && SN->GetAnimationAsset() && SN->IsLooping() && SN->IsPlaying();
	}
	// 4305 (14 б): группа @0, charId A @2, charId B @6, тайл @10/@12.
	static TArray<uint8> Make4305(int32 Pvp, int32 A, int32 B, FIntPoint Tile)
	{
		TArray<uint8> P; P.SetNumZeroed(14);
		AsdaBytes::WrI16(P.GetData(), Pvp); AsdaBytes::WrI32(P.GetData() + 2, A); AsdaBytes::WrI32(P.GetData() + 6, B);
		AsdaBytes::WrI16(P.GetData() + 10, Tile.X); AsdaBytes::WrI16(P.GetData() + 12, Tile.Y);
		return P;
	}
	// 4304 (55 б): группа @0, победитель userNum @2 / charId @4, проигравший @8 / @10, тип @14, имена @15/@35.
	static TArray<uint8> Make4304(int32 Pvp, int32 WinU, int32 WinC, int32 LoseU, int32 LoseC, int32 Type)
	{
		TArray<uint8> P; P.SetNumZeroed(55);
		AsdaBytes::WrI16(P.GetData(), Pvp); AsdaBytes::WrI16(P.GetData() + 2, WinU); AsdaBytes::WrI32(P.GetData() + 4, WinC);
		AsdaBytes::WrI16(P.GetData() + 8, LoseU); AsdaBytes::WrI32(P.GetData() + 10, LoseC); P[14] = (uint8)Type;
		const char* WN = "Winner"; const char* LN = "Loser";
		for (int32 i = 0; WN[i]; ++i) { P[15 + i] = (uint8)WN[i]; }
		for (int32 i = 0; LN[i]; ++i) { P[35 + i] = (uint8)LN[i]; }
		return P;
	}

	static void Finish(FCtxRef C);

	// Все входы игрока под блокировкой: вернуть, сколько из пяти «прошло» (0 — всё заблокировано), и подробности.
	static int32 TryInputs(const FCtxRef& C, FString& Out)
	{
		UAsdaWorldSubsystem* WS = Sub<UAsdaWorldSubsystem>(C);
		AAsdaPlayerController* P = PCOf(C);
		AAsdaPlayerCharacter* Ch = Me(C);
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
		if (!WS || !P || !Ch || !Net) { Out = TEXT("нет подсистем"); return 99; }
		FLogCatch Catch;
		GLog->AddOutputDevice(&Catch);
		int32 Passed = 0;
		// ход кликом по земле: 4006
		const int32 M0 = CountOp(C, 4006);
		Net->ClearMoveRequestInFlight(TEXT("самотест дуэли"));
		P->RequestMoveTo(Ch->GetGamePosXZ() + FVector2D(600.f, 0.f));
		const bool bMove = CountOp(C, 4006) > M0 || Ch->IsMoving();
		Passed += bMove ? 1 : 0;
		// эмоция «радость» (4024)
		const int32 E0 = CountOp(C, 4024);
		const bool bEmo = Sub<UAsdaCharaInfoSubsystem>(C) && Sub<UAsdaCharaInfoSubsystem>(C)->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(0, 0));
		const bool bEmoSent = CountOp(C, 4024) > E0;
		Passed += (bEmo || bEmoSent) ? 1 : 0;
		// сесть (4024 107)
		const int32 S0 = CountOp(C, 4024);
		P->ToggleSitStand(TEXT("самотест дуэли"));
		const bool bSit = CountOp(C, 4024) > S0;
		Passed += bSit ? 1 : 0;
		// навык из окна навыков (501)
		if (UAsdaSkillUseSubsystem* SU = Sub<UAsdaSkillUseSubsystem>(C)) { SU->UseFromSkillWindow(501); }
		const bool bSkillRefused = Catch.Has(TEXT("[навыки] 501 не применён"));
		Passed += bSkillRefused ? 0 : 1;
		// предмет из сумки
		bool bItemRefused = false;
		if (UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(C); Inv && C->Cell >= 0)
		{
			Inv->UseConsumable(1, C->Cell);
			bItemRefused = Catch.Has(TEXT("жест поражения дуэли — предмет не применён"));
		}
		Passed += bItemRefused ? 0 : 1;
		GLog->RemoveOutputDevice(&Catch);
		Out = FString::Printf(TEXT("ход %d, эмоция %d, сесть %d, навык %s, предмет %s"), bMove ? 1 : 0, (bEmo || bEmoSent) ? 1 : 0, bSit ? 1 : 0,
			bSkillRefused ? TEXT("отказ") : TEXT("пропущен"), bItemRefused ? TEXT("отказ") : TEXT("пропущен"));
		return Passed;
	}

	static void Run(FCtxRef C)
	{
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
		UAsdaWorldSubsystem* WS = Sub<UAsdaWorldSubsystem>(C);
		UAsdaDuelSubsystem* Du = Sub<UAsdaDuelSubsystem>(C);
		if (!Net || !WS || !Du || !Me(C)) { C->Expect(false, TEXT("подсистемы сети/мира/дуэли и свой персонаж есть")); Finish(C); return; }
		C->SelfScope = MakeShared<FAsdaNetSelfScope>(Net, kCmd);
		C->Keys = MakeShared<FAsdaCVarScope>(C->W.Get(), kCmd);
		C->Keys->Set(TEXT("asda.Duel.LoserLock"), 1);
		C->Keys->Set(TEXT("asda.Duel.Flags"), 1);
		C->Keys->Set(TEXT("asda.Duel.DedupResult"), 0);   // повторные 4304 в прогоне — разные сценарии
		Net->SelectedCharId = kSelfCharId;
		Net->SelfUserNum = (uint16)kSelfNum;
		Net->bSelfUserNumKnown = true;
		C->bWasInGame = Net->bInGameConnected;
		Net->bInGameConnected = true;   // отправители шлют только «в игре» — пакеты забирает перехват
		C->Sent = MakeShared<TArray<TPair<int32, TArray<uint8>>>>();
		auto S = C->Sent;
		C->CapGen = AsdaSelfTestKit::ArmCapture(Net, [S](uint16 Op, const uint8* Body, int32 Len, bool, TCHAR)
		{
			TArray<uint8> Bd; if (Body && Len > 0) { Bd.Append(Body, Len); } S->Add(TPair<int32, TArray<uint8>>((int32)Op, MoveTemp(Bd)));
		}, 0, kCmd);
		if (!C->CapGen) { C->Expect(false, TEXT("перехват отправок взведён")); Finish(C); return; }
		Du->ResetSessionState(EAsdaSessionResetReason::EnteringWorld);
		// Пробный предмет в сумке: первый расходник (тип 11), не лопата.
		if (UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(C))
		{
			int32 Code = 0;
			if (UAsdaItemTableSubsystem* IT = Sub<UAsdaItemTableSubsystem>(C))
			{
				for (const FAsdaItemRecord& Rec : IT->GetAllItems())
				{
					if (Rec.ItemType == 11 && Rec.ItemSubType != 33 && Rec.ItemSubType != 34 && Rec.ItemSubType != 15) { Code = Rec.ItemCode; break; }
				}
			}
			for (int32 K = 47; K >= 0 && C->Cell < 0; --K) { const FAsdaItemInstance* It = Inv->GetCell(1, K); if (It && It->IsEmpty()) { C->Cell = K; } }
			if (Code && C->Cell >= 0) { Inv->DebugPutItem(1, C->Cell, Code, 5); } else { C->Cell = -1; }
		}
		const FVector2D Near = Me(C)->GetGamePosXZ();
		WS->DebugSpawnFakePeer(kPeerNum, TEXT("Rival"), Near + FVector2D(0.f, 250.f));

		Then(C, 1.0f, [C]()
		{
			C->Expect(Peer(C) != nullptr, TEXT("сосед-соперник заспавнен (фейковый, путь снапшота 4065)"));
			// F. Арена 4305: я и сосед, центр — мой тайл.
			Feed(C, 4305, Make4305(21, kSelfCharId, kPeerCharId, AsdaCoord::GameToTile(Me(C)->GetGamePosXZ())));
		});
		Then(C, 0.6f, [C]()
		{
			UAsdaDuelSubsystem* Du = Sub<UAsdaDuelSubsystem>(C);
			ASkeletalMeshActor* Land = Cast<ASkeletalMeshActor>(Du->DebugCenterFlag(21));
			USkeletalMeshComponent* LandC = Land ? Land->GetSkeletalMeshComponent() : nullptr;
			if (LandC) { UE_LOG(LogAsdaDuelLoserTest, Log, TEXT("[дуэль-самотест] флаг центра: габарит %s, масштаб %s"), *LandC->Bounds.BoxExtent.ToString(), *LandC->GetComponentScale().ToString()); }
			C->Expect(LandC && LandC->Bounds.BoxExtent.GetMax() < 600.f, FString::Printf(TEXT("F1a: флаг центра в масштабе мира (габарит %s ≤ 600)"), LandC ? *LandC->Bounds.BoxExtent.ToString() : TEXT("—")));
			C->Expect(LandC && LandC->GetSkeletalMeshAsset() && LandC->GetSkeletalMeshAsset()->GetName() == TEXT("pvp_flag_land") && FlagAnimLoops(LandC),
				TEXT("F1: 4305 → флаг pvp_flag_land в центре арены, клип pvp_flag_land по кругу (тип 6, `clanFlagSpawnAt`)"));
			for (const int32 Cid : { kSelfCharId, kPeerCharId })
			{
				USkeletalMeshComponent* F = Du->DebugBackFlag(Cid);
				const bool bBone = F && F->GetAttachSocketName() == FName(TEXT("2"));
				C->Expect(F && F->GetSkeletalMeshAsset() && F->GetSkeletalMeshAsset()->GetName() == TEXT("pvp_flag_back") && bBone && F->IsVisible() && FlagAnimLoops(F),
					FString::Printf(TEXT("F2: флаг pvp_flag_back за спиной charId %d — кость '2', виден, клип Battle_Flag_00 по кругу (тип 5); габарит %s, масштаб %s"), Cid,
						F ? *F->Bounds.BoxExtent.ToString() : TEXT("—"), F ? *F->GetComponentScale().ToString() : TEXT("—")));
			}
			Shot(C, TEXT("duel_flags"));
		});
		// L. Итог 4304: проигравший — я (тип 2 «смерть»).
		Then(C, 0.6f, [C]()
		{
			Feed(C, 4304, Make4304(21, kPeerNum, kPeerCharId, kSelfNum, kSelfCharId, 2));
		});
		Then(C, 0.4f, [C]()
		{
			UAsdaDuelSubsystem* Du = Sub<UAsdaDuelSubsystem>(C);
			AAsdaPlayerCharacter* Ch = Me(C);
			C->Expect(!Du->DebugCenterFlag(21) && !Du->DebugBackFlag(kSelfCharId) && !Du->DebugBackFlag(kPeerCharId),
				TEXT("F3: 4304 → флаги сняты (`listEraseByObjId` pvp+10000 и charId+20000 ×2)"));
			C->Expect(Ch && Ch->GetCampPoseState() == 51 && ClipIs(Ch, AAsdaPlayerCharacter::kClipLoser),
				TEXT("L1: 4304 проигравший — я → поза 51, клип anim_99 (loser)"));
			FString D;
			const int32 Passed = TryInputs(C, D);
			C->Expect(Passed == 0 && Ch && Ch->GetCampPoseState() == 51 && PCOf(C)->IsStunHardStopped(),
				FString::Printf(TEXT("L2: во время жеста ничего не проходит [ИСПРАВЛЕНИЕ asda.Duel.LoserLock] — %s; поза 51 не прервана"), *D));
			Shot(C, TEXT("duel_loser_pose"));
		});
		// Конец клипа 4.267 с от 4304 — блокировка снята.
		Then(C, 4.4f, [C]()
		{
			AAsdaPlayerCharacter* Ch = Me(C);
			C->Expect(Ch && Ch->GetCampPoseState() == 0 && !PCOf(C)->IsStunHardStopped(), TEXT("L3: конец клипа (4.267 с) → поза снята, ход снова разрешён"));
			UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
			Net->ClearMoveRequestInFlight(TEXT("самотест дуэли"));
			const int32 M0 = CountOp(C, 4006);
			PCOf(C)->RequestMoveTo(Ch->GetGamePosXZ() + FVector2D(600.f, 0.f));
			C->Expect(CountOp(C, 4006) > M0, TEXT("L4: контроль — после жеста клик по земле шлёт 4006"));
			Ch->StopMove();
			Ch->SetAnimState(EAsdaPlayerState::Idle);
		});
		// N. Проиграл сосед: у него жест 51 целиком, у меня блокировки нет.
		Then(C, 0.6f, [C]()
		{
			Feed(C, 4304, Make4304(22, kSelfNum, kSelfCharId, kPeerNum, kPeerCharId, 2));
		});
		Then(C, 0.4f, [C]()
		{
			AAsdaPlayerCharacter* P = Peer(C);
			C->Expect(P && P->GetCampPoseState() == 51 && ClipIs(P, AAsdaPlayerCharacter::kClipLoser), TEXT("N1: проиграл сосед → у него поза 51, клип anim_99 (видно соседям)"));
			C->Expect(!PCOf(C)->IsStunHardStopped(), TEXT("N2: у победителя блокировки нет"));
		});
		Then(C, 2.0f, [C]()
		{
			AAsdaPlayerCharacter* P = Peer(C);
			C->Expect(P && P->GetCampPoseState() == 51, TEXT("N3: через 2 с жест соседа ещё идёт (клип 4.267 с)"));
		});
		Then(C, 2.6f, [C]()
		{
			AAsdaPlayerCharacter* P = Peer(C);
			C->Expect(P && P->GetCampPoseState() == 0, TEXT("N4: жест соседа доигран → покой"));
			// K0. Контроль ключа 0: жест прерывается ходом (как оригинал).
			C->Keys->Set(TEXT("asda.Duel.LoserLock"), 0);
			Feed(C, 4304, Make4304(23, kPeerNum, kPeerCharId, kSelfNum, kSelfCharId, 2));
		});
		Then(C, 0.4f, [C]()
		{
			AAsdaPlayerCharacter* Ch = Me(C);
			UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
			Net->ClearMoveRequestInFlight(TEXT("самотест дуэли"));
			const int32 M0 = CountOp(C, 4006);
			PCOf(C)->RequestMoveTo(Ch->GetGamePosXZ() + FVector2D(600.f, 0.f));
			C->Expect(CountOp(C, 4006) > M0, TEXT("K0: ключ asda.Duel.LoserLock 0 — клик во время жеста шлёт 4006 (оригинал жест не держит)"));
			Ch->StopMove();
			Ch->LeaveCampPose(51, TEXT("самотест"));
			Ch->SetAnimState(EAsdaPlayerState::Idle);
			Finish(C);
		});
	}

	static void Finish(FCtxRef C)
	{
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
		if (UAsdaDuelSubsystem* Du = Sub<UAsdaDuelSubsystem>(C)) { Du->ResetSessionState(EAsdaSessionResetReason::EnteringWorld); }
		if (UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(C); Inv && C->Cell >= 0) { Inv->DebugPutItem(1, C->Cell, 0, 0); }
		if (AAsdaPlayerCharacter* Ch = Me(C)) { Ch->LeaveCampPose(51, TEXT("самотест: конец")); }
		// ★ 01.11.2026 (изоляция пакета): сосед прогона уходит штатным 4011 (как `DespawnAll` самотеста PvP) — следующим не достаётся.
		if (Peer(C)) { TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(B.GetData(), (int16)kPeerNum); Feed(C, 4011, B); }
		if (Net)
		{
			Net->bInGameConnected = C->bWasInGame;
			if (C->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, C->CapGen); }
		}
		if (C->Keys) { C->Keys->Restore(); }
		if (C->SelfScope) { C->SelfScope->Restore(); }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaDuelLoserTest, TEXT("[дуэль-самотест]"), C->Ok, C->Fail);
	}

	// asda.UI.SelfTestDuelLoser [delay=N] [shots] [quit] — офлайн: спавн своего павна (если его нет), через 4 с — прогон.
	static FAutoConsoleCommandWithWorldAndArgs GAsdaDuelLoserSelfTest(
		TEXT("asda.UI.SelfTestDuelLoser"),
		TEXT("asda.UI.SelfTestDuelLoser [delay=N] [shots] [quit] — ОФЛАЙН-самотест дуэли: флаги 4305 (центр и за спинами), итог 4304 — жест поражения "
		     "51 доигрывается до конца (ход, эмоция, сесть, навык, предмет ждут), сосед видит жест целиком, контроль ключа asda.Duel.LoserLock 0"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			if (!W) { return; }
			FCtxRef C = MakeShared<FCtx>();
			C->W = W;
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
			C->bQuit = KitArgs.bQuit;
			C->Delay = KitArgs.Delay;
			if (KitArgs.Has(TEXT("shots"))) { C->ShotDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports/duel/shots")); }
			static const uint16 kGuardOps[] = { 4006, 4008, 4024, 5028, 4303 };
			if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
			auto Start = [C]()
			{
				UWorld* Wd = C->W.Get();
				if (!Wd || !GEngine) { return; }
				if (AsdaSelfTestKit::RefuseDeferred(Wd, kCmd, C->bQuit)) { return; }
				APlayerController* P = Wd->GetFirstPlayerController();
				if (!AsdaSelfTestSelfReady::IsReady(Wd))
				{
					if (!P || !P->GetPawn()) { GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3")); }   // ★ 01.11.2026: карта — только без павна
					GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256 DuelLoser"));
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
