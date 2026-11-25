// Copyright (c) 2026 @sitxovski (Discord)
// asda.ViewRange.SelfTest — дальность видимости: только графика, пакеты 4013/4015/4005 работают как обычно. [ПОРТ]
//   Офлайн: все мобы остаются акторами, за радиусом скрыты, положение и HP скрытых обновляются, туман закрывает дальность,
//   ползунок меняет всё на лету, защищённые не пропадают, откат возвращает карту. Запуск: `asda.ViewRange.SelfTest quit`.
//   `frames` — кадры фейда; `shots=<метка> [tile=x:y]` — кадры тумана при ползунке 50, 100 и 200 %.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "AsdaTestPackets.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Containers/Ticker.h"
#include "Data/AsdaMobStatsRow.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Npc/AsdaMobActor.h"
#include "Npc/AsdaNpcActor.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaSoulmateSubsystem.h"
#include "UI/AsdaSystemOptionSubsystem.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"
#include "World/AsdaCoord.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaPetActor.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaSoulGuardianActor.h"
#include "World/AsdaViewRangeSubsystem.h"
#include "World/AsdaWorldSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaViewRangeTest, Log, All);

namespace AsdaViewRangeTest
{
	static const TCHAR* const kCmd = TEXT("asda.ViewRange.SelfTest");   // имя команды
	static constexpr int32 kX = 240, kY = 240;        // своя клетка: радиусы влезают в 512 тайлов
	static constexpr int32 kMyUserNum = 7100;         // свой номер на время прогона
	static constexpr int32 kPeerNear = 7101;          // сосед в радиусе игроков
	static constexpr int32 kPeerFar = 7102;           // сосед дальше радиуса игроков
	static constexpr int32 kPeerParty = 7103;         // дальний сосед из отряда
	static constexpr int32 kPeerClan = 7104;          // дальний сосед из своего клана
	static constexpr int32 kPeerSoul = 7105;          // дальний соулмейт
	static constexpr int32 kPeerPet = 7106;           // дальний сосед с питомцем и хранителем
	static constexpr int32 kRidNear0 = 21000;         // 6 мобов рядом
	static constexpr int32 kRidFar0 = 21100;          // 10 мобов на 100 тайлов к востоку
	static constexpr int32 kRidTarget = 21200;        // моб-цель на 80 тайлах
	static constexpr int32 kRidBuff = 21300;          // дальний моб с усилением
	static constexpr int32 kRidBoss = 21301;          // дальний босс
	static constexpr int32 kRidFight = 21302;         // моб в бою на 80 тайлах
	static constexpr int32 kSgIdx = 3;                // индекс хранителя
	static constexpr int32 kMobType = 298;            // Pawn

	// Состояние прогона.
	struct FCtx
	{
		UGameInstance* GI = nullptr;                  // экземпляр игры
		bool bQuit = false;                           // выйти после прогона
		int32 Ok = 0, Fail = 0;                       // счётчики проверок
		int32 Stage = 0;                              // номер шага
		double T0 = 0.0;                              // время начала шага
		TUniquePtr<FAsdaNetSelfScope> NetScope;       // свой персонаж вернётся после прогона
		TUniquePtr<FAsdaCVarScope> CVars;             // ключи вернутся после прогона
		TArray<TWeakObjectPtr<AAsdaNpcActor>> Npcs;   // NPC карты, принятые пакетом 4067
		TWeakObjectPtr<AAsdaNpcActor> QuestNpc;       // дальний NPC с квестовой меткой
		TWeakObjectPtr<AAsdaPetActor> Pet;            // питомец дальнего соседа
		int32 BuffId = -1;                            // усиление записи kRidBuff
		FVector FarPos0 = FVector::ZeroVector;        // положение скрытого моба до хода
		float Density100 = 0.f;                       // плотность тумана при 100 %
		int32 Shot = 0;                               // номер кадра
		double NextShot = 0.0;                        // время следующего кадра
		FString ShotTag;                              // метка кадров тумана
		FIntPoint ShotTile = FIntPoint(kX, kY);       // клетка кадров тумана
		float ShotYaw = -1000.f, ShotPitch = -1000.f; // камера кадров; -1000 — как есть
		double ShotWait = 5.0;                        // пауза перед кадром, с
		void Expect(bool b, const FString& What)      // записать проверку
		{
			if (b) { ++Ok; UE_LOG(LogAsdaViewRangeTest, Log, TEXT("ДАЛЬНОСТЬ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaViewRangeTest, Error, TEXT("ДАЛЬНОСТЬ FAIL %s"), *What); }
		}
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Тело 4005: сосед в игровых координатах.
	static TArray<uint8> Body4005(int32 Uidx, float GameX, float GameZ, const char* Name = "RangeTest")
	{
		TArray<uint8> B; B.SetNumZeroed(151);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)Uidx);
		B[6] = 1;
		AsdaBytes::WrI32(B.GetData() + 11, 990000 + Uidx);
		FMemory::Memcpy(B.GetData() + 15, Name, FCStringAnsi::Strlen(Name));
		B[37] = 1; B[38] = 1; B[40] = 10;
		FMemory::Memcpy(B.GetData() + 56, &GameX, 4);
		FMemory::Memcpy(B.GetData() + 60, &GameZ, 4);
		return B;
	}

	// Тело 4015 (37 б): действие моба rid.
	static TArray<uint8> Body4015(int32 Rid, uint8 Kind, const FIntPoint& Cur, const FIntPoint& Dst, int32 Hp)
	{
		TArray<uint8> B; B.SetNumZeroed(37);
		AsdaBytes::WrI16(B.GetData() + 0, -1);
		AsdaBytes::WrI16(B.GetData() + 2, (int16)kMobType);
		AsdaBytes::WrI16(B.GetData() + 4, (int16)Rid);
		B[6] = Kind;
		AsdaBytes::WrI16(B.GetData() + 7, (int16)Cur.X);
		AsdaBytes::WrI16(B.GetData() + 9, (int16)Cur.Y);
		AsdaBytes::WrI16(B.GetData() + 11, (int16)Dst.X);
		AsdaBytes::WrI16(B.GetData() + 13, (int16)Dst.Y);
		AsdaBytes::WrI16(B.GetData() + 15, 200);
		AsdaBytes::WrI32(B.GetData() + 25, Hp);
		return B;
	}

	// Подать запись 4013 штатным приёмом.
	static void FeedMob(UAsdaNetSubsystem* Net, int32 Rid, const FIntPoint& T, int32 Type = kMobType)
	{
		AsdaTestNet::DebugFeedPacket(Net, 4013, AsdaTestPackets::MobSpawn4013(Rid, Type, T, /*State*/1, /*Speed*/1666));
	}

	// Есть ли на мобе усиление прогона.
	static bool HasBuff(const FCtxRef& C, int32 Rid)
	{
		const UAsdaEffectsSubsystem* Fx = C->GI->GetSubsystem<UAsdaEffectsSubsystem>();
		if (!Fx) { return false; }
		for (const FAsdaEffectEntry& E : Fx->GetMobList(Rid)) { if (E.Type == 0 && E.Id == C->BuffId) { return true; } }
		return false;
	}

	// Сколько мобов диапазона существуют.
	static int32 Alive(const UAsdaWorldSubsystem* WS, int32 Rid0, int32 N)
	{
		int32 Cnt = 0;
		for (int32 i = 0; i < N; ++i) { if (WS->FindNetMobByRid(Rid0 + i)) { ++Cnt; } }
		return Cnt;
	}

	// Сколько мобов диапазона видимы.
	static int32 Shown(const UAsdaWorldSubsystem* WS, int32 Rid0, int32 N)
	{
		int32 Cnt = 0;
		for (int32 i = 0; i < N; ++i) { const AAsdaMobActor* M = WS->FindNetMobByRid(Rid0 + i); if (M && !M->IsHidden()) { ++Cnt; } }
		return Cnt;
	}

	// Непрозрачность тумана мира на дистанции L.
	static float FogOpacity(const UWorld* W, float L, float* OutDensity = nullptr)
	{
		float Best = 0.f;
		for (TObjectIterator<UExponentialHeightFogComponent> It; It; ++It)
		{
			if (It->IsTemplate() || It->GetWorld() != W) { continue; }
			if (OutDensity) { *OutDensity = It->FogDensity; }
			const float K = FMath::Square(UE_LN2) * It->FogDensity / 1000.f;
			Best = FMath::Max(Best, FMath::Min(It->FogMaxOpacity, 1.f - FMath::Exp(-K * FMath::Max(0.f, L - It->StartDistance))));
		}
		return Best;
	}

	// Кадр в Reports/view_distance_1106/frames.
	static void Shoot(const FCtxRef& C, const FString& Name)
	{
		++C->Shot;
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("@@ASTORY_DEV_ROOT_FWD@@/Reports/view_distance_1106/frames/%s.png"), *Name), /*bShowUI=*/false, /*bAddUniqueSuffix=*/false);
	}

	// Поставить ползунок.
	static void Slider(const FCtxRef& C, float V)
	{
		if (C->CVars) { C->CVars->Set(TEXT("asda.Cull.Scale"), V); }
	}

	// Конец прогона.
	static bool Done(const FCtxRef& C, UWorld* W)
	{
		C->CVars.Reset();
		C->NetScope.Reset();
		AsdaSelfTestKit::Finish(W, kCmd, C->bQuit, LogAsdaViewRangeTest, TEXT("ДАЛЬНОСТЬ"), C->Ok, C->Fail);
		return false;
	}

	// Шаг прогона; false — конец.
	static bool Step(const FCtxRef& C)
	{
		UWorld* W = C->GI ? C->GI->GetWorld() : nullptr;
		UAsdaWorldSubsystem* WS = C->GI ? C->GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaNetSubsystem* Net = C->GI ? AsdaTestNet::Get(C->GI) : nullptr;
		UAsdaViewRangeSubsystem* VR = UAsdaViewRangeSubsystem::Get(W);
		if (!W || !WS || !Net || !VR) { return false; }
		const double Dt = FPlatformTime::Seconds() - C->T0;
		const auto Next = [&C]() { ++C->Stage; C->T0 = FPlatformTime::Seconds(); };
		const auto PeerShown = [WS](int32 U) { const AActor* A = WS->ActorByUserIdx(U); return A && !A->IsHidden(); };
		const float Tile = 100.f;
		switch (C->Stage)
		{
		case 0:   // наполнение
		{
			WS->DebugSpawnSelfAt(kX, kY);
			const bool bWas = AsdaTestNet::bInGameConnected(Net);
			AsdaTestNet::bInGameConnected(Net) = true;
			for (int32 i = 0; i < 6; ++i)  { FeedMob(Net, kRidNear0 + i, FIntPoint(kX + 3 + i, kY + 3)); }
			for (int32 i = 0; i < 10; ++i) { FeedMob(Net, kRidFar0 + i, FIntPoint(kX + 100, kY + i)); }
			FeedMob(Net, kRidTarget, FIntPoint(kX + 80, kY));
			FeedMob(Net, kRidFight, FIntPoint(kX + 80, kY + 2));
			if (const UAsdaEffectsSubsystem* Fx = C->GI->GetSubsystem<UAsdaEffectsSubsystem>())
			{
				for (int32 Id = 1; Id < 350 && C->BuffId < 0; ++Id)
				{
					const FAsdaSkillEffectMeta& Meta = Fx->GetSkillMeta(Id);
					if (Id != 177 && Meta.bValid && Meta.Buff0SubType == 1) { C->BuffId = Id; }
				}
			}
			TArray<uint8> Rec = AsdaTestPackets::MobSpawn4013(kRidBuff, kMobType, FIntPoint(kX + 100, kY + 20), 1, 1666);
			for (int32 i = 0; i < 28; ++i) { AsdaBytes::WrI16(&Rec[63 + 2 * i], i == 0 ? (int16)C->BuffId : (int16)-1); }
			AsdaTestNet::DebugFeedPacket(Net, 4013, Rec);
			for (int32 T = 1; T < 900; ++T)
			{
				const FAsdaMobStatsRow* Row = AAsdaMobActor::FindMobStatsRow(T);
				if (Row && Row->Aggression == 3) { FeedMob(Net, kRidBoss, FIntPoint(kX + 100, kY + 25), T); break; }
			}
			AsdaTestNet::bInGameConnected(Net) = bWas;
			AsdaTestNet::DebugFeedPacket(Net, 4005, Body4005(kPeerNear, (kX + 40) * Tile, kY * Tile));
			AsdaTestNet::DebugFeedPacket(Net, 4005, Body4005(kPeerFar, (kX + 130) * Tile, kY * Tile));
			AsdaTestNet::DebugFeedPacket(Net, 4005, Body4005(kPeerParty, (kX + 130) * Tile, (kY + 5) * Tile));
			AsdaTestNet::DebugFeedPacket(Net, 4005, Body4005(kPeerClan, (kX + 130) * Tile, (kY + 10) * Tile));
			AsdaTestNet::DebugFeedPacket(Net, 4005, Body4005(kPeerSoul, (kX + 130) * Tile, (kY + 15) * Tile, "RangeSoul"));
			AsdaTestNet::DebugFeedPacket(Net, 4005, Body4005(kPeerPet, (kX + 130) * Tile, (kY + 20) * Tile));
			WS->SetPartyRoster({ kPeerParty });
			VR->DebugSetClan(kPeerClan, 777);
			if (UAsdaSoulmateSubsystem* Soul = C->GI->GetSubsystem<UAsdaSoulmateSubsystem>()) { Soul->DebugSetChatBond(0, true, TEXT("RangeSoul")); }
			if (AActor* Owner = WS->ActorByUserIdx(kPeerPet))
			{
				FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				AAsdaPetActor* Pet = W->SpawnActor<AAsdaPetActor>(AAsdaPetActor::StaticClass(), Owner->GetActorLocation(), FRotator::ZeroRotator, SP);
				if (Pet) { Pet->InitPet(1, TEXT("RangePet"), Owner); }
				VR->DebugAddPet(990000 + kPeerPet, Pet);
				C->Pet = Pet;
			}
			if (UAsdaSystemOptionSubsystem* Opt = C->GI->GetSubsystem<UAsdaSystemOptionSubsystem>())
			{
				Opt->DebugConfirmCharacterDisplay(3, 3, true);
				Opt->DebugConfirmCharacterDisplay(3, 4, true);
			}
			WS->OnSoulGuardIndex(kPeerPet, kSgIdx);
			WS->TickSoulGuardians();
			int32 Idx = 0;
			for (TActorIterator<AAsdaNpcActor> It(W); It; ++It)
			{
				if (It->bNetSpawned) { continue; }
				TArray<uint8> Body; Body.SetNumZeroed(108);
				AsdaBytes::WrI16(Body.GetData() + 0, (int16)(22000 + Idx++));
				AsdaBytes::WrI16(Body.GetData() + 2, (int16)It->NpcId);
				AsdaBytes::WrI16(Body.GetData() + 8, (int16)It->TileX);
				AsdaBytes::WrI16(Body.GetData() + 10, (int16)It->TileZ);
				AsdaTestNet::DebugFeedPacket(Net, 4067, Body);
				C->Npcs.Add(*It);
			}
			Next();
			return true;
		}
		case 1:   // 100 %: дальность 9000 юн, мобы 60 тайлов, игроки 90
		{
			if (Dt < 5.0) { return true; }
			C->Expect(VR->MobTiles() == 60 && VR->PlayerTiles() == 90, FString::Printf(TEXT("100 %%: радиусы %d и %d"), VR->MobTiles(), VR->PlayerTiles()));
			const float Op = FogOpacity(W, 9000.f, &C->Density100);
			C->Expect(Op >= 0.98f, FString::Printf(TEXT("100 %%: туман на дальности %.3f, плотность %.4f"), Op, C->Density100));
			C->Expect(Shown(WS, kRidNear0, 6) == 6, TEXT("100 %: ближние мобы видны"));
			C->Expect(Alive(WS, kRidFar0, 10) == 10, TEXT("100 %: все дальние мобы остаются акторами"));
			C->Expect(Shown(WS, kRidFar0, 10) == 0, TEXT("100 %: дальние мобы скрыты"));
			C->Expect(PeerShown(kPeerNear), TEXT("100 %: игрок на 40 тайлах виден"));
			C->Expect(!PeerShown(kPeerFar) && WS->ActorByUserIdx(kPeerFar), TEXT("100 %: игрок на 130 тайлах скрыт, актор цел"));
			C->Expect(PeerShown(kPeerParty), TEXT("100 %: дальний игрок из отряда виден"));
			C->Expect(PeerShown(kPeerClan), TEXT("100 %: дальний игрок своего клана виден"));
			C->Expect(PeerShown(kPeerSoul), TEXT("100 %: дальний соулмейт виден"));
			C->Expect(!PeerShown(kPeerPet), TEXT("100 %: дальний игрок с питомцем скрыт"));
			C->Expect(C->Pet.IsValid() && C->Pet->IsHidden(), TEXT("100 %: его питомец скрыт"));
			{
				const AAsdaSoulGuardianActor* G = WS->FindSoulGuardian(kPeerPet);
				C->Expect(G && G->IsHidden(), TEXT("100 %: его хранитель скрыт"));
			}
			C->Expect(Shown(WS, kRidBoss, 1) == 1, TEXT("100 %: дальний босс виден"));
			C->Expect(HasBuff(C, kRidBuff), TEXT("100 %: усиление скрытого моба на месте"));
			{
				int32 Bad = 0, Far = 0;
				for (const TWeakObjectPtr<AAsdaNpcActor>& P : C->Npcs)
				{
					const AAsdaNpcActor* N = P.Get();
					if (!N || !N->bNetSpawned) { continue; }
					const int32 D = FMath::Max(FMath::Abs(N->TileX - kX), FMath::Abs(N->TileZ - kY));
					if (D > 75) { ++Far; if (!N->IsHidden()) { ++Bad; } else if (!C->QuestNpc.IsValid()) { C->QuestNpc = P; } }
					if (D < 55 && N->IsHidden()) { ++Bad; }
				}
				C->Expect(Far > 0 && Bad == 0, FString::Printf(TEXT("NPC: дальних %d скрыто, ошибок %d"), Far, Bad));
			}
			// Скрытый моб живёт как обычно: ход и HP из 4015.
			if (const AAsdaMobActor* M = WS->FindNetMobByRid(kRidFar0)) { C->FarPos0 = M->GetActorLocation(); }
			AsdaTestNet::DebugFeedPacket(Net, 4015, Body4015(kRidFar0, 2, FIntPoint(kX + 100, kY), FIntPoint(kX + 90, kY), 1500));
			AsdaTestNet::DebugFeedPacket(Net, 4015, Body4015(kRidFar0 + 1, 1, FIntPoint(kX + 100, kY + 1), FIntPoint(kX + 100, kY + 1), 77));
			Next();
			return true;
		}
		case 2:
			if (Dt < 3.0) { return true; }
			if (const AAsdaMobActor* M = WS->FindNetMobByRid(kRidFar0))
			{
				const float Moved = FVector::Dist2D(M->GetActorLocation(), C->FarPos0);
				C->Expect(Moved > 100.f && M->IsHidden(), FString::Printf(TEXT("скрытый моб идёт по 4015: сдвиг %.0f юн"), Moved));
			}
			else { C->Expect(false, TEXT("скрытый моб существует")); }
			if (const AAsdaMobActor* M = WS->FindNetMobByRid(kRidFar0 + 1)) { C->Expect(M->CurHp == 77, FString::Printf(TEXT("скрытый моб получил HP из 4015: %d"), M->CurHp)); }
			Slider(C, 2.f);
			Next();
			return true;
		case 3:   // 200 %: мобы 120, игроки 180
		{
			if (Dt < 5.0) { return true; }
			float D2 = 0.f;
			const float Op = FogOpacity(W, 18000.f, &D2);
			C->Expect(Op >= 0.98f && D2 < C->Density100, FString::Printf(TEXT("200 %%: туман на дальности %.3f, плотность %.4f ниже"), Op, D2));
			C->Expect(Shown(WS, kRidFar0, 10) == 10, FString::Printf(TEXT("200 %%: дальние показаны, %d"), Shown(WS, kRidFar0, 10)));
			C->Expect(PeerShown(kPeerFar), TEXT("200 %: игрок на 130 тайлах показан"));
			C->Expect(PeerShown(kPeerPet) && C->Pet.IsValid() && !C->Pet->IsHidden(), TEXT("200 %: игрок и питомец показаны"));
			{
				const AAsdaSoulGuardianActor* G = WS->FindSoulGuardian(kPeerPet);
				C->Expect(G && !G->IsHidden(), TEXT("200 %: хранитель показан"));
			}
			if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(C->GI->GetFirstLocalPlayerController(W)))
			{
				if (AAsdaMobActor* T = WS->FindNetMobByRid(kRidTarget)) { PC->SetTarget(T); }
			}
			if (AAsdaMobActor* Fight = WS->FindNetMobByRid(kRidFight)) { Fight->PlayAggroRoarOnce(); }
			Slider(C, 0.5f);
			Next();
			return true;
		}
		case 4:   // 50 %: мобы 30, игроки 45
			// Подсистема квестовых меток офлайн снимает чужую метку, поэтому тест держит её сам.
			if (AAsdaNpcActor* Q = C->QuestNpc.Get()) { Q->SetQuestMark(1); }
			if (Dt < 5.0) { return true; }
			C->Expect(VR->MobTiles() == 30, TEXT("50 %: радиус 30 тайлов"));
			C->Expect(Shown(WS, kRidFar0, 10) == 0 && Alive(WS, kRidFar0, 10) == 10, TEXT("50 %: дальние скрыты, акторы целы"));
			C->Expect(Shown(WS, kRidNear0, 6) == 6, TEXT("50 %: ближние на месте"));
			C->Expect(Shown(WS, kRidTarget, 1) == 1, TEXT("50 %: цель на 80 тайлах видна"));
			C->Expect(Shown(WS, kRidFight, 1) == 1, TEXT("50 %: моб в бою на 80 тайлах виден"));
			C->Expect(Shown(WS, kRidBoss, 1) == 1, TEXT("50 %: босс виден"));
			if (const AAsdaNpcActor* Q = C->QuestNpc.Get()) { C->Expect(!Q->IsHidden(), TEXT("50 %: NPC с квестовой меткой виден")); }
			if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(C->GI->GetFirstLocalPlayerController(W))) { PC->ClearTarget(); }
			Slider(C, 1.f);
			if (AActor* Self = WS->LocalPlayerActor.Get())
			{
				const FVector2D Ue = AsdaCoord::GameToUe2D(AsdaCoord::TileCenterGame(FIntPoint(kX + 100, kY)));
				Self->SetActorLocation(FVector(Ue.X, Ue.Y, Self->GetActorLocation().Z));
			}
			Next();
			return true;
		case 5:   // персонаж ушёл к дальним
			if (Dt < 5.0) { return true; }
			C->Expect(Shown(WS, kRidFar0, 10) == 10, FString::Printf(TEXT("смена позиции: мобы у новой точки видны, %d"), Shown(WS, kRidFar0, 10)));
			C->Expect(Shown(WS, kRidNear0, 6) == 0 && Alive(WS, kRidNear0, 6) == 6, TEXT("смена позиции: прежние скрыты, акторы целы"));
			C->Expect(HasBuff(C, kRidBuff), TEXT("смена позиции: усиление на месте"));
			if (C->CVars) { C->CVars->Set(TEXT("asda.ViewRange.Enable"), 0); }
			Next();
			return true;
		case 6:   // откат
		{
			if (Dt < 4.0) { return true; }
			C->Expect(VR->HiddenMobCount() == 0 && Shown(WS, kRidNear0, 6) == 6, TEXT("откат: все мобы видны"));
			C->Expect(VR->HiddenPlayerCount() == 0 && PeerShown(kPeerFar), TEXT("откат: скрытых игроков нет"));
			C->Expect(VR->HiddenNpcCount() == 0, TEXT("откат: скрытых NPC нет"));
			float D0 = 0.f;
			FogOpacity(W, 9000.f, &D0);
			C->Expect(D0 < C->Density100, FString::Printf(TEXT("откат: плотность тумана карты %.4f"), D0));
			if (AAsdaNpcActor* Q = C->QuestNpc.Get()) { Q->SetQuestMark(0); }
			WS->SetPartyRoster({});
			VR->DebugSetClan(-1, -1);
			if (UAsdaSoulmateSubsystem* Soul = C->GI->GetSubsystem<UAsdaSoulmateSubsystem>()) { Soul->DebugSetChatBond(-1, false, FString()); }
			if (AAsdaPetActor* Pet = C->Pet.Get()) { Pet->Destroy(); }
			return Done(C, W);
		}
		case 10:   // `frames`: кольцо мобов на 8 тайлах
		{
			WS->DebugSpawnSelfAt(kX, kY);
			const bool bWas = AsdaTestNet::bInGameConnected(Net);
			AsdaTestNet::bInGameConnected(Net) = true;
			static const int32 Ring[8][2] = { {8,0},{-8,0},{0,8},{0,-8},{8,8},{-8,8},{8,-8},{-8,-8} };
			for (int32 i = 0; i < 8; ++i) { FeedMob(Net, kRidNear0 + i, FIntPoint(kX + Ring[i][0], kY + Ring[i][1])); }
			AsdaTestNet::bInGameConnected(Net) = bWas;
			Next();
			return true;
		}
		case 11:
			if (Dt < 4.0) { return true; }
			Shoot(C, TEXT("a_before"));
			C->CVars->Set(TEXT("asda.ViewRange.Hysteresis"), 0);
			C->CVars->Set(TEXT("asda.ViewRange.FogOpacity"), 0.5f);
			C->CVars->Set(TEXT("asda.ViewRange.BaseUnits"), 1000.f);
			C->NextShot = 0.0;
			Next();
			return true;
		case 12:
			if (Dt < C->NextShot) { return true; }
			Shoot(C, FString::Printf(TEXT("b_out_%02d"), C->Shot));
			C->NextShot = Dt + 0.25;
			if (Dt < 3.0) { return true; }
			C->Expect(Shown(WS, kRidNear0, 8) == 0 && Alive(WS, kRidNear0, 8) == 8, TEXT("кадры: мобы скрыты, акторы целы"));
			C->CVars->Set(TEXT("asda.ViewRange.BaseUnits"), 9000.f);
			C->NextShot = 0.0;
			Next();
			return true;
		case 13:
			if (Dt < C->NextShot) { return true; }
			Shoot(C, FString::Printf(TEXT("c_in_%02d"), C->Shot));
			C->NextShot = Dt + 0.25;
			if (Dt < 3.0) { return true; }
			C->Expect(Shown(WS, kRidNear0, 8) == 8, TEXT("кадры: мобы вернулись"));
			return Done(C, W);
		case 20:   // `shots`: 50, 100, 200 %
			WS->DebugSpawnSelfAt(C->ShotTile.X, C->ShotTile.Y);
			Slider(C, 0.5f);
			Next();
			return true;
		case 21: case 22: case 23: case 24:
		{
			if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(C->GI->GetFirstLocalPlayerController(W)))
			{
				if (C->ShotYaw > -999.f) { PC->SetOrbitAnglesForBench(C->ShotYaw, C->ShotPitch); }
			}
			if (Dt < C->ShotWait) { return true; }
			// Четвёртый кадр снова на 50 %: отличает прогрев после загрузки от действия ползунка.
			static const TCHAR* const Names[4] = { TEXT("050"), TEXT("100"), TEXT("200"), TEXT("050b") };
			const int32 I = C->Stage - 21;
			Shoot(C, FString::Printf(TEXT("fog_%s_%s"), *C->ShotTag, Names[I]));
			if (I == 0) { Slider(C, 1.f); }
			if (I == 1) { Slider(C, 2.f); }
			if (I == 2) { Slider(C, 0.5f); }
			if (I == 3) { C->Ok += 1; C->Stage = 25; C->T0 = FPlatformTime::Seconds(); return true; }
			Next();
			return true;
		}
		case 25:
			if (Dt < 1.0) { return true; }
			return Done(C, W);
		default:
			return false;
		}
	}

	// Старт: отказ при живом соединении, снимок ключей и своего персонажа.
	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA, 120.f)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? AsdaTestNet::Get(GI) : nullptr;
		if (!Net || !GI->GetSubsystem<UAsdaWorldSubsystem>()) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем")); return; }
		FCtxRef C = MakeShared<FCtx>();
		C->GI = GI; C->bQuit = KA.bQuit;
		C->NetScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		C->CVars = MakeUnique<FAsdaCVarScope>(W, kCmd);
		C->CVars->Set(TEXT("asda.Cull.Scale"), 1.f);
		for (const FString& P : KA.Pos)
		{
			if (P.StartsWith(TEXT("shots="))) { C->ShotTag = P.Mid(6); }
			if (P.StartsWith(TEXT("yaw=")))   { C->ShotYaw = FCString::Atof(*P.Mid(4)); }
			if (P.StartsWith(TEXT("pitch="))) { C->ShotPitch = FCString::Atof(*P.Mid(6)); }
			if (P.StartsWith(TEXT("wait=")))  { C->ShotWait = FCString::Atof(*P.Mid(5)); }
			if (P.StartsWith(TEXT("mapid=")))
			{
				if (UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>()) { LM->CurrentMapId = FCString::Atoi(*P.Mid(6)); }
			}
			if (P.StartsWith(TEXT("tile=")))
			{
				FString X, Y;
				if (P.Mid(5).Split(TEXT(":"), &X, &Y)) { C->ShotTile = FIntPoint(FCString::Atoi(*X), FCString::Atoi(*Y)); }
			}
		}
		// Кадры тумана сохраняют ключи запуска: сверка «с правкой и без» идёт через --pre.
		if (C->ShotTag.IsEmpty()) { C->CVars->Set(TEXT("asda.ViewRange.Enable"), 1); C->CVars->Set(TEXT("asda.ViewRange.BaseUnits"), 9000.f); }
		AsdaTestNet::SetSelfUserNumForDebug(Net, kMyUserNum);
		// Номер текущей карты: без него спавн «до загрузки уровня» отложился бы до следующей загрузки.
		if (UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>()) { if (LM->CurrentMapId < 0) { LM->CurrentMapId = 3; } }
		if (KA.Has(TEXT("frames"))) { C->Stage = 10; }
		if (!C->ShotTag.IsEmpty()) { C->Stage = 20; }
		C->T0 = FPlatformTime::Seconds();
		UE_LOG(LogAsdaViewRangeTest, Log, TEXT("ДАЛЬНОСТЬ старт: мир %s, язык %s"), *W->GetMapName(), FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en"));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([C](float) { return Step(C); }), 0.f);
	}
}

// Консольная команда самотеста.
static FAutoConsoleCommandWithWorldAndArgs GAsdaViewRangeSelfTest(
	TEXT("asda.ViewRange.SelfTest"),
	TEXT("Дальность видимости: скрытие без смены сети, туман, ползунок, защиты, откат. `frames`, `shots=<метка> [tile=x:y]`. Офлайн."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AsdaViewRangeTest::Run));

#endif
