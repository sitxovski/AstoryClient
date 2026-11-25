// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ СВЕЧЕНИЯ ЗАТОЧКИ КАК СОСТОЯНИЯ КАДРА (31.10.2026): `asda.SelfTest.WeaponGlowPersist [shot=<папка>] [quit]`. [ПОРТ]
//   Жалоба игроков: свечение и свет заточенного/инкрустированного оружия сами гаснут при фарме (Queen's Palace и др.) и после
//   смерти — до смены зоны, перезахода или переодевания. Эталон и разбор — у ключа `asda.WeaponGlow.Persist`
//   (AsdaWorldSubsystem_Visuals.cpp): `CCharacter_UpdateHeldItemEffect@0xA4C070` / `UpdateEquipAuraEffect@0xA4DD60` каждый кадр,
//   смерти в гейте нет, погасший эффект грузится заново.
//   Офлайн (`-AsdaNoNet`), карта мира. Путь игрока (закон diagnostic-must-use-real-path):
//     • свой — спавн пакетом 4003, оружие/щит с заточкой +7 — общая точка ответа 5001 (`ApplySelfEquipPart`), смерть — пакет 4053,
//       воскрешение — пакет 4056 (ветка «своё воскрешение» → `OnCharRevived`);
//     • сосед — 4005 (оружие и щит +7), смерть — 4053, уход — 4011.
//   Проверки — по эффект-акторам на носителе (теги `WeaponGlowFx`/`ShieldGlowFx`): число, скрытость, скелет носителя, кость,
//   вершины секций (эффект реально строит геометрию). Сценарии:
//     A) надето +7: свечение оружия и аура щита есть и видны;
//     B) смерть 4053 (ключ 1): на трупе свечение остаётся; воскрешение 4056 — на месте;
//     C) чужой снос эффекта → сверка строит заново; скрытие при видимом носителе → снова видно; носитель скрыт — эффект НЕ
//        «проявляется»; эффект, рождённый на скрытом носителе (`SpawnCharFx`), виден после показа носителя;
//     D) ключ 0 (прежний порт): смерть гасит свечение, после воскрешения его нет — баг воспроизведён; ключ 1 — возвращается само;
//     E) сосед +7: смерть — свечение на трупе остаётся;
//     F) (ревизия) пропала только аура щита — построена заново, свечение оружия НЕ перезапущено (тот же актор); магазин
//        снаряжения 6800 открыт (путь диалога NPC — `OpenWindow`) — свечения оружия и щита нет и сверка их не возвращает,
//        закрыт — построены заново; элитный магазин 6000 — гаснет только аура щита (эталон entity_char.c:28377, :28796-28799).
//   Кадры (`shot=`): жив, труп, после воскрешения, ключ 0 после воскрешения, после возврата ключа 1. Итог: «СВЕЧЕНИЕ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "AsdaTestPackets.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaEffectActor.h"
#include "AsdaFxNames.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"   // FAsdaSowellRow (раздел S)
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaEquipShopSubsystem.h"     // магазин снаряжения 6800 (гейт свечения эталона, ревизия Б3)
#include "UI/AsdaPremiumShopSubsystem.h"   // элитный магазин 6000 (гейт ауры щита)
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCoord.h"   // тайл воскрешения соседа (E3)
#include "Characters/AsdaCharAssembly.h"        // теги меша щита/оружия (H)
#include "Components/StaticMeshComponent.h"     // меш щита/оружия (H)
#include "Test/AsdaSelfTestKit.h"
#include "ProceduralMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "UnrealClient.h"   // FScreenshotRequest — кадры

DEFINE_LOG_CATEGORY_STATIC(LogAsdaGlowTest, Log, All);

// ★ 05.11.2026: число видимых обводок самоцвета у носителя (AsdaSowelGlow.cpp).
int32 AsdaSowelGlowCount(const AAsdaPlayerCharacter* Ch);

namespace AsdaGlowPersistTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.WeaponGlowPersist");   // имя команды
	static const TCHAR* kKey = TEXT("asda.WeaponGlow.Persist");           // ключ отката
	constexpr int32 kMe = 4321;        // свой userNum (4003)
	constexpr int32 kPeer = 61;        // сосед с заточенным оружием и щитом
	constexpr int32 kGrade = 7;        // ступень заточки (≥ 7 → состояние «_03»)

	// Шаг сценария: true — шаг завершён, false — ждёт следующего кадра. Аргумент — секунды от начала шага.
	using FStep = TFunction<bool(float)>;

	// Состояние прогона.
	struct FState
	{
		TWeakObjectPtr<UWorld> W;                     // мир
		bool bQuit = false;                           // `quit` в конце
		FString ShotDir;                              // папка кадров (пусто — без кадров)
		int32 Ok = 0, Fail = 0;                       // счётчики проверок
		TArray<FStep> Steps;                          // сценарий
		int32 Idx = 0;                                // текущий шаг
		double StepT0 = 0.0;                          // начало шага (время мира)
		FDelegateHandle H;                            // подписка на тик мира
		TUniquePtr<FAsdaNetSelfScope> NetScope;       // свой номер/смерть — вернуть после прогона
		TUniquePtr<FAsdaCVarScope> CVars;             // ключи — вернуть после прогона
		TWeakObjectPtr<UAsdaNetSubsystem> Net;        // сеть
		TWeakObjectPtr<UAsdaWorldSubsystem> WS;       // мир
		TWeakObjectPtr<UAsdaItemTableSubsystem> Items;// таблицы предметов
		TWeakObjectPtr<UAsdaInventorySubsystem> Inv;  // инвентарь (контейнер 3 = надетое)
		int32 Sword = 0;                              // оружие правой руки со свечением на +7
		int32 Shield = 0;                             // щит с аурой на +7
		FString SwordJeb;                             // ожидаемое имя .jeb свечения оружия
		int32 OrigShieldCell = 0, OrigWeaponCell = 0; // надетое в 3/8 и 3/9 до прогона (0 — пусто)
		bool bWornTouched = false;                    // сценарий успел надеть вещи
		bool bAbort = false;                          // прервать сценарий (нет данных)
		FRotator OrigSelfRot = FRotator::ZeroRotator; // поворот своего актёра до первого `Aim`
		bool bSelfRotSaved = false;                   // поворот снят
		TWeakObjectPtr<AAsdaEffectActor> KeepW;
		int32 OrigMapId = INT_MIN;                    // EnterMapId до раздела G (вернуть)
		int32 SowelCode = 0;                          // код самоцвета для раздела S
		int32 SowelClass = 0;                         // его класс (PartGrade)
		FVector2D PeerTarget = FVector2D::ZeroVector;  // точка воскрешения соседа (E3)
		TWeakObjectPtr<AAsdaPlayerCharacter> OldPawn; // павн до пересоздания (G4)       // свечение оружия до шага — тот же актор после (без перезапуска)

		// Записать итог проверки.
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaGlowTest, Log,   TEXT("СВЕЧЕНИЕ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaGlowTest, Error, TEXT("СВЕЧЕНИЕ FAIL %s"), *What); }
		}
	};

	// Свой павн.
	static AAsdaPlayerCharacter* MeOf(const FState& S)
	{
		const UAsdaWorldSubsystem* WS = S.WS.Get();
		return WS ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get()) : nullptr;
	}

	// Живые эффект-акторы носителя с тегом `Tag`.
	static TArray<AAsdaEffectActor*> GlowFx(const AActor* A, const TCHAR* Tag)
	{
		TArray<AAsdaEffectActor*> Out;
		if (!A) { return Out; }
		TArray<AActor*> Att;
		A->GetAttachedActors(Att);
		const FName T(Tag);
		for (AActor* X : Att)
		{
			AAsdaEffectActor* E = Cast<AAsdaEffectActor>(X);
			if (E && !E->IsPendingKillPending() && E->ActorHasTag(T)) { Out.Add(E); }
		}
		return Out;
	}

	// Описание свечения одного вида на носителе.
	struct FGlowInfo
	{
		int32 Count = 0;          // эффект-акторов с тегом
		bool bHidden = true;      // единственный скрыт
		bool bSkelOk = false;     // следует за BaseSkel носителя
		FName Bone;               // кость слежения
		int32 Verts = 0;          // вершин во всех секциях (эффект строит геометрию)
		FString Name;             // имя эффекта
	};
	static FGlowInfo Describe(const AAsdaPlayerCharacter* Ch, const TCHAR* Tag)
	{
		FGlowInfo I;
		const TArray<AAsdaEffectActor*> L = GlowFx(Ch, Tag);
		I.Count = L.Num();
		if (L.Num() != 1) { return I; }
		const AAsdaEffectActor* E = L[0];
		I.bHidden = E->IsHidden();
		I.bSkelOk = Ch && Ch->BaseSkel && E->FollowSkelComponent == Ch->BaseSkel;
		I.Bone = E->FollowBone;
		I.Name = E->EffectName;
		TInlineComponentArray<UProceduralMeshComponent*> PMs(E);
		for (const UProceduralMeshComponent* P : PMs)
		{
			for (int32 s = 0; P && s < P->GetNumSections(); ++s)
			{
				if (const FProcMeshSection* Sec = const_cast<UProceduralMeshComponent*>(P)->GetProcMeshSection(s)) { I.Verts += Sec->ProcVertexBuffer.Num(); }
			}
		}
		return I;
	}
	static FString Str(const FGlowInfo& I)
	{
		return FString::Printf(TEXT("актор(ов) %d, «%s», скрыт %d, скелет BaseSkel %d, кость %s, вершин %d"),
			I.Count, *I.Name, I.bHidden ? 1 : 0, I.bSkelOk ? 1 : 0, *I.Bone.ToString(), I.Verts);
	}
	// Ровно одно видимое свечение на BaseSkel носителя.
	static bool Alive(const FGlowInfo& I) { return I.Count == 1 && !I.bHidden && I.bSkelOk; }

	// Тело 4003 (9 б): userNum, тайлы, состояние, показ шлема.
	using AsdaTestPackets::Body4003;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPackets.h`) — копия была дословной.
	// Тело 4053 (4 б): умерший, убийца (−1 — нет).
	static TArray<uint8> Body4053(int32 Uidx)
	{
		TArray<uint8> B; B.SetNumZeroed(4);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)Uidx);
		AsdaBytes::WrI16(B.GetData() + 2, (int16)-1);
		return B;
	}
	// Тело 4056 (22 б): userIdx, тайлы, HP@6, MP@10; +20 ≠ 200 — обычное воскрешение (не «прибыл мёртвым»).
	static TArray<uint8> Body4056(int32 Uidx, const FVector2D& GamePos)
	{
		TArray<uint8> B; B.SetNumZeroed(22);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)Uidx);
		AsdaBytes::WrI16(B.GetData() + 2, (int16)FMath::Clamp(FMath::RoundToInt(GamePos.X / 100.f), 0, 32767));
		AsdaBytes::WrI16(B.GetData() + 4, (int16)FMath::Clamp(FMath::RoundToInt(GamePos.Y / 100.f), 0, 32767));
		AsdaBytes::WrI32(B.GetData() + 6, 100);
		AsdaBytes::WrI32(B.GetData() + 10, 100);
		return B;
	}
	// Тело 4055 (43 б): userIdx, карта @2 (та же — без перезагрузки уровня), тайлы @25/@27, HP @29, MP @33, опыт @35.
	static TArray<uint8> Body4055(int32 Uidx, int32 MapId, const FVector2D& GamePos)
	{
		TArray<uint8> B; B.SetNumZeroed(43);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)Uidx);
		B[2] = (uint8)MapId;
		AsdaBytes::WrI16(B.GetData() + 25, (int16)FMath::Clamp(FMath::RoundToInt(GamePos.X / 100.f), 0, 32767));
		AsdaBytes::WrI16(B.GetData() + 27, (int16)FMath::Clamp(FMath::RoundToInt(GamePos.Y / 100.f), 0, 32767));
		AsdaBytes::WrI32(B.GetData() + 29, 100);
		AsdaBytes::WrI16(B.GetData() + 33, 100);
		return B;
	}
	// Сколько живых эффект-акторов с тегом висит на носителе (крылья, кольцо соулмейта).
	static int32 CountTag(const AActor* A, const TCHAR* Tag)
	{
		return GlowFx(A, Tag).Num();
	}

	// Тело 4005 (151 б): сосед-мужчина, слоты экипа 8 (щит) и 9 (оружие) по 5 б (код i32 + заточка u8), позиция game XZ.
	static TArray<uint8> Body4005(int32 Uidx, int32 ShieldId, int32 WeaponId, int32 Grade, float GameX, float GameZ)
	{
		TArray<uint8> B; B.SetNumZeroed(151);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)Uidx);
		B[6] = 1;                                             // шлем показан
		AsdaBytes::WrI32(B.GetData() + 11, 980000 + Uidx);    // charId — заведомо не свой
		const char* Name = "GlowTest";
		FMemory::Memcpy(B.GetData() + 15, Name, FCStringAnsi::Strlen(Name));
		B[37] = 1; B[38] = 1; B[40] = 10;                     // пол, класс, уровень
		FMemory::Memcpy(B.GetData() + 56, &GameX, 4);
		FMemory::Memcpy(B.GetData() + 60, &GameZ, 4);
		AsdaBytes::WrI32(B.GetData() + 96 + 5 * 8, ShieldId); // слот 8 — щит
		B[96 + 5 * 8 + 4] = (uint8)Grade;
		AsdaBytes::WrI32(B.GetData() + 96 + 5 * 9, WeaponId); // слот 9 — оружие
		B[96 + 5 * 9 + 4] = (uint8)Grade;
		return B;
	}

	// Повернуть актёра лицом к камере правым боком (оружие — в правой руке).
	static void Aim(FState& S, AActor* Target)
	{
		UWorld* W = S.W.Get();
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		AAsdaPlayerCharacter* Me = MeOf(S);
		if (!PC || !Me || !Target) { return; }
		const float CamYaw = PC->GetControlRotation().Yaw;
		if (Target == Me && !S.bSelfRotSaved) { S.OrigSelfRot = Me->GetActorRotation(); S.bSelfRotSaved = true; }
		Target->SetActorRotation(FRotator(0.f, CamYaw + 180.f + 50.f, 0.f));
	}
	static void Shot(const FState& S, const TCHAR* Name)
	{
		if (S.ShotDir.IsEmpty()) { return; }
		const FString Path = FPaths::Combine(S.ShotDir, FString(Name) + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaGlowTest, Log, TEXT("СВЕЧЕНИЕ кадр %s"), *Path);
	}

	// Надетое своё: ячейка контейнера 3 (тот же путь, что ответ 5001) — для уборки.
	static void Wear(FState& S, int32 Slot, int32 ItemId)
	{
		UAsdaInventorySubsystem* Inv = S.Inv.Get();
		if (Inv && Inv->DebugPutItem(3, Slot, ItemId, 1)) { return; }
		if (UAsdaWorldSubsystem* WS = S.WS.Get()) { WS->ApplySelfEquipPart(Slot, ItemId, 0); }
	}

	// Надеть своему заточенный предмет путём игрока: инстанс в контейнере 3 (ячейка = слот) с заточкой в байте +24 — как его кладёт
	//   ответ 5001 — и общая точка модели/источника свечения `ApplySelfEquipPart` с той же заточкой.
	static void WearGraded(FState& S, int32 Slot, int32 ItemId, int32 Grade)
	{
		if (UAsdaInventorySubsystem* Inv = S.Inv.Get(); Inv && Inv->DebugPutItem(3, Slot, ItemId, 1))
		{
			if (FAsdaItemInstance* C = Inv->GetCellMutable(3, Slot))
			{
				C->Raw[24] = (uint8)Grade;
				C->SyncFromRaw();
			}
		}
		if (UAsdaWorldSubsystem* WS = S.WS.Get()) { WS->ApplySelfEquipPart(Slot, ItemId, Grade); }
	}

	// То же надевание, но с самоцветом `Sowel` в гнезде 0 (инстанс +16, i16) — так приходит запись предмета с вставленным совелом.
	static void WearSowel(FState& S, int32 Slot, int32 ItemId, int32 Grade, int32 Sowel)
	{
		if (UAsdaInventorySubsystem* Inv = S.Inv.Get(); Inv && Inv->DebugPutItem(3, Slot, ItemId, 1))
		{
			if (FAsdaItemInstance* C = Inv->GetCellMutable(3, Slot))
			{
				C->Raw[24] = (uint8)Grade;
				AsdaBytes::WrI16(C->Raw.GetData() + 16, (int16)Sowel);
				C->SyncFromRaw();
			}
		}
		if (UAsdaWorldSubsystem* WS = S.WS.Get()) { WS->ApplySelfEquipPart(Slot, ItemId, Grade); }
	}

	// Конец прогона: сосед уходит (4011), свой оживает (снимок сети), надетое и поворот возвращаются.
	static void Finish(const TSharedRef<FState>& SP)
	{
		FState& S = *SP;
		FWorldDelegates::OnWorldPostActorTick.Remove(S.H);
		if (UAsdaNetSubsystem* N = S.Net.Get())
		{
			TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(B.GetData(), (int16)kPeer);
			N->DebugFeedPacket(4011, B);
		}
		if (UGameInstance* GI = S.W.IsValid() ? S.W->GetGameInstance() : nullptr)
		{
			// Магазины, открытые шагами F, — закрыть (прерванный прогон не должен оставить окно).
			if (UAsdaEquipShopSubsystem* Eq = GI->GetSubsystem<UAsdaEquipShopSubsystem>(); Eq && Eq->IsOpen()) { Eq->CloseWindow(); }
			if (UAsdaPremiumShopSubsystem* Pr = GI->GetSubsystem<UAsdaPremiumShopSubsystem>(); Pr && Pr->IsOpen()) { Pr->CloseWindow(); }
		}
		if (AAsdaPlayerCharacter* Me = MeOf(S))
		{
			Me->SetActorHiddenInGame(false);
			if (Me->AnimState == EAsdaPlayerState::Dead) { Me->SetAnimState(EAsdaPlayerState::Idle); }
			if (S.bSelfRotSaved) { Me->SetActorRotation(S.OrigSelfRot); }
		}
		if (S.bWornTouched)
		{
			Wear(S, 9, S.OrigWeaponCell);
			Wear(S, 8, S.OrigShieldCell);
		}
		S.CVars.Reset();
		S.NetScope.Reset();
		if (UAsdaNetSubsystem* N = S.Net.Get())
		{
			if (MeOf(S) && !N->IsSelfUserNumKnown()) { N->SetSelfUserNumForDebug(N->SelfUserNum); }
			UE_LOG(LogAsdaGlowTest, Log, TEXT("СВЕЧЕНИЕ уборка: сосед %d ушёл, надетое 8=%d 9=%d, номер %d (известен %d)"),
				kPeer, S.OrigShieldCell, S.OrigWeaponCell, (int32)N->SelfUserNum, N->IsSelfUserNumKnown() ? 1 : 0);
		}
		AsdaSelfTestKit::Finish(S.W.Get(), kCmd, S.bQuit, LogAsdaGlowTest, TEXT("СВЕЧЕНИЕ"), S.Ok, S.Fail);
	}

	// Тик сценария.
	static void Tick(const TSharedRef<FState>& InSP, UWorld* InW)
	{
		const TSharedRef<FState> SP = InSP;   // своя ссылка: `Finish` снимает подписку вместе с копией в лямбде
		FState& S = *SP;
		UWorld* W = S.W.Get();
		if (!W) { FWorldDelegates::OnWorldPostActorTick.Remove(S.H); return; }
		if (InW != W) { return; }
		while (S.Steps.IsValidIndex(S.Idx) && !S.bAbort)
		{
			const float T = (float)(W->GetTimeSeconds() - S.StepT0);
			if (!S.Steps[S.Idx](T)) { return; }
			++S.Idx;
			S.StepT0 = W->GetTimeSeconds();
		}
		Finish(SP);
	}

	// Сценарий.
	static void Build(const TSharedRef<FState>& SP)
	{
		FState* S = &SP.Get();
		auto Do = [](TFunction<void()> Fn) { return FStep([Fn](float) { Fn(); return true; }); };
		auto Wait = [](float Sec) { return FStep([Sec](float T) { return T >= Sec; }); };
		const TCHAR* kW = AsdaCharFxTags::kWeaponGlow;
		const TCHAR* kS = AsdaCharFxTags::kShieldGlow;

		// ═══ A. Спавн и надевание +7 ═══
		S->Steps.Add(Do([S]()
		{
			UWorld* W = S->W.Get();
			if (const UAsdaInventorySubsystem* Inv = S->Inv.Get())
			{
				const FAsdaItemInstance* C8 = Inv->GetCell(3, 8);
				const FAsdaItemInstance* C9 = Inv->GetCell(3, 9);
				S->OrigShieldCell = (C8 && !C8->IsEmpty()) ? C8->ItemCode : 0;
				S->OrigWeaponCell = (C9 && !C9->IsEmpty()) ? C9->ItemCode : 0;
			}
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			S->Net->DebugFeedPacket(4003, Body4003(kMe));
		}));
		S->Steps.Add(Wait(4.0f));   // эффект появления (столб света) ≈3 с
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			UAsdaItemTableSubsystem* Items = S->Items.Get();
			UAsdaWorldSubsystem* WS = S->WS.Get();
			// Оружие правой руки (подтипы 0/1/3 — кость "8_a1") и щит, у которых по данным есть свечение на +7.
			for (const FAsdaItemRecord& R : Items->GetAllItems())
			{
				if (!S->Sword && R.ItemType == 9 && (R.ItemSubType == 0 || R.ItemSubType == 1 || R.ItemSubType == 3)
					&& !WS->BuildGlowJebName(R.ItemCode, kGrade, false).IsEmpty())
				{
					S->Sword = R.ItemCode;
				}
				if (!S->Shield && R.ItemType == 8 && !WS->BuildGlowJebName(R.ItemCode, kGrade, true).IsEmpty()) { S->Shield = R.ItemCode; }
				if (S->Sword && S->Shield) { break; }
			}
			S->SwordJeb = S->Sword ? WS->BuildGlowJebName(S->Sword, kGrade, false) : FString();
			S->Check(Me && S->Sword && S->Shield, FString::Printf(TEXT("A0 павн %s; оружие %d («%s»), щит %d («%s»)"),
				Me ? TEXT("есть") : TEXT("НЕТ"), S->Sword, *S->SwordJeb, S->Shield,
				S->Shield ? *WS->BuildGlowJebName(S->Shield, kGrade, true) : TEXT("")));
			if (!Me || !S->Sword || !S->Shield) { S->bAbort = true; return; }
			S->bWornTouched = true;
			WearGraded(*S, 9, S->Sword, kGrade);    // контейнер 3 + общая точка ответа 5001: модель + источник свечения
			WearGraded(*S, 8, S->Shield, kGrade);
			Aim(*S, Me);
		}));
		S->Steps.Add(Wait(0.8f));
		S->Steps.Add(Do([S, kW, kS]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			const FGlowInfo IW = Describe(Me, kW), IS = Describe(Me, kS);
			S->Check(Alive(IW) && IW.Name.Contains(S->SwordJeb.Replace(TEXT("-"), TEXT("_"))) && IW.Verts > 0,
				FString::Printf(TEXT("A1 надето +%d: свечение оружия (%s)"), kGrade, *Str(IW)));
			S->Check(Alive(IS) && IS.Verts > 0, FString::Printf(TEXT("A2 аура щита (%s)"), *Str(IS)));
			Shot(*S, TEXT("glow_1_alive"));
		}));

		// ═══ B. Смерть и воскрешение (ключ 1) ═══
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]() { S->Net->DebugFeedPacket(4053, Body4053(kMe)); }));
		S->Steps.Add(Wait(2.5f));   // клип падения death01
		S->Steps.Add(Do([S, kW, kS]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			const FGlowInfo IW = Describe(Me, kW), IS = Describe(Me, kS);
			S->Check(Me && Me->AnimState == EAsdaPlayerState::Dead && Alive(IW) && IW.Verts > 0 && Alive(IS),
				FString::Printf(TEXT("B1 смерть 4053 (труп %d): свечение остаётся — оружие (%s), щит (%s)"),
					Me && Me->AnimState == EAsdaPlayerState::Dead ? 1 : 0, *Str(IW), *Str(IS)));
			Aim(*S, Me);
		}));
		S->Steps.Add(Wait(0.5f));
		S->Steps.Add(Do([S]() { Shot(*S, TEXT("glow_2_dead")); }));
		S->Steps.Add(Wait(0.4f));
		S->Steps.Add(Do([S]() { if (AAsdaPlayerCharacter* Me = MeOf(*S)) { S->Net->DebugFeedPacket(4056, Body4056(kMe, Me->GetGamePosXZ())); } }));
		S->Steps.Add(Wait(1.2f));
		S->Steps.Add(Do([S, kW, kS]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			const FGlowInfo IW = Describe(Me, kW), IS = Describe(Me, kS);
			S->Check(Me && Me->AnimState != EAsdaPlayerState::Dead && !S->Net->bSelfDead && Alive(IW) && IW.Verts > 0 && Alive(IS),
				FString::Printf(TEXT("B2 воскрешение 4056 (мёртв %d): свечение на месте — оружие (%s), щит (%s)"),
					S->Net->bSelfDead ? 1 : 0, *Str(IW), *Str(IS)));
			Aim(*S, Me);
		}));
		S->Steps.Add(Wait(0.5f));
		S->Steps.Add(Do([S]() { Shot(*S, TEXT("glow_3_revived")); }));

		// ═══ C. Снос / залипшее скрытие / скрытый носитель ═══
		S->Steps.Add(Do([S, kW]()
		{
			for (AAsdaEffectActor* E : GlowFx(MeOf(*S), kW)) { E->Destroy(); }   // чужой снос (любой путь)
		}));
		S->Steps.Add(Wait(0.5f));
		S->Steps.Add(Do([S, kW, kS]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			const FGlowInfo IW = Describe(Me, kW);
			S->Check(Alive(IW) && IW.Name.Contains(S->SwordJeb.Replace(TEXT("-"), TEXT("_"))) && Describe(Me, kS).Count == 1,
				FString::Printf(TEXT("C1 снесённое свечение построено заново: %s; аура щита %d"), *Str(IW), Describe(Me, kS).Count));
			for (AAsdaEffectActor* E : GlowFx(Me, kW)) { E->SetActorHiddenInGame(true); }   // скрыто, носитель виден
		}));
		S->Steps.Add(Wait(0.5f));
		S->Steps.Add(Do([S, kW]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			const FGlowInfo IW = Describe(Me, kW);
			S->Check(Alive(IW), FString::Printf(TEXT("C2 скрытое при видимом носителе — снова видно: %s"), *Str(IW)));
			Me->SetActorHiddenInGame(true);   // носитель скрыт (так прячут транспорт/невидимость/GM): эффект скрываем вместе с ним
			for (AAsdaEffectActor* E : GlowFx(Me, kW)) { E->SetActorHiddenInGame(true); }
		}));
		S->Steps.Add(Wait(0.5f));
		S->Steps.Add(Do([S, kW]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			const FGlowInfo IW = Describe(Me, kW);
			S->Check(IW.Count == 1 && IW.bHidden, FString::Printf(TEXT("C3 носитель скрыт — сверка эффект НЕ проявляет: %s"), *Str(IW)));
			// Пересборка на скрытом носителе: `SpawnCharFx` рождает эффект скрытым — прежде так он и оставался.
			S->WS->ApplySelfEquipPart(9, S->Sword, kGrade);
			S->Check(Describe(Me, kW).bHidden, FString::Printf(TEXT("C4 рождён на скрытом носителе — скрыт: %s"), *Str(Describe(Me, kW))));
			Me->SetActorHiddenInGame(false);
		}));
		S->Steps.Add(Wait(0.5f));
		S->Steps.Add(Do([S, kW]()
		{
			const FGlowInfo IW = Describe(MeOf(*S), kW);
			S->Check(Alive(IW), FString::Printf(TEXT("C5 носитель показан — эффект виден: %s"), *Str(IW)));
		}));

		// ═══ D. Ключ 0 — прежний порт ═══
		S->Steps.Add(Do([S]() { S->CVars->Set(kKey, 0); S->Net->DebugFeedPacket(4053, Body4053(kMe)); }));
		S->Steps.Add(Wait(2.0f));
		S->Steps.Add(Do([S, kW]()
		{
			const FGlowInfo IW = Describe(MeOf(*S), kW);
			S->Check(IW.Count == 0, FString::Printf(TEXT("D1 ключ 0: смерть гасит свечение (%s)"), *Str(IW)));
			if (AAsdaPlayerCharacter* Me = MeOf(*S)) { S->Net->DebugFeedPacket(4056, Body4056(kMe, Me->GetGamePosXZ())); }
		}));
		S->Steps.Add(Wait(1.5f));
		S->Steps.Add(Do([S, kW]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			const FGlowInfo IW = Describe(Me, kW);
			S->Check(Me && Me->AnimState != EAsdaPlayerState::Dead && IW.Count == 0,
				FString::Printf(TEXT("D2 ключ 0: после воскрешения свечения нет — жалоба воспроизведена (%s)"), *Str(IW)));
			Aim(*S, Me);
		}));
		S->Steps.Add(Wait(0.5f));
		S->Steps.Add(Do([S]() { Shot(*S, TEXT("glow_4_key0_revived")); S->CVars->Set(kKey, 1); }));
		S->Steps.Add(Wait(0.6f));
		S->Steps.Add(Do([S, kW, kS]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			const FGlowInfo IW = Describe(Me, kW), IS = Describe(Me, kS);
			S->Check(Alive(IW) && IW.Verts > 0 && Alive(IS), FString::Printf(TEXT("D3 ключ 1: свечение вернулось без переодевания — оружие (%s), щит (%s)"),
				*Str(IW), *Str(IS)));
		}));
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]() { Shot(*S, TEXT("glow_5_key1_back")); }));

		// ═══ F. Одна часть / магазины (ревизия Б1, Б3) ═══
		S->Steps.Add(Do([S, kW, kS]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			const TArray<AAsdaEffectActor*> L = GlowFx(Me, kW);
			S->KeepW = L.Num() == 1 ? L[0] : nullptr;
			for (AAsdaEffectActor* E : GlowFx(Me, kS)) { E->Destroy(); }   // пропала только аура щита
		}));
		S->Steps.Add(Wait(0.5f));
		S->Steps.Add(Do([S, kW, kS]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			const TArray<AAsdaEffectActor*> L = GlowFx(Me, kW);
			S->Check(Alive(Describe(Me, kS)) && S->KeepW.IsValid() && L.Num() == 1 && L[0] == S->KeepW.Get(),
				FString::Printf(TEXT("F1 пропала аура щита — построена заново (%s), свечение оружия тот же актор %d (без перезапуска)"),
					*Str(Describe(Me, kS)), (L.Num() == 1 && L[0] == S->KeepW.Get()) ? 1 : 0));
			if (UAsdaEquipShopSubsystem* Eq = S->W.Get()->GetGameInstance()->GetSubsystem<UAsdaEquipShopSubsystem>()) { Eq->OpenWindow(); }
		}));
		S->Steps.Add(Wait(1.0f));
		S->Steps.Add(Do([S, kW, kS]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			UAsdaEquipShopSubsystem* Eq = S->W.Get()->GetGameInstance()->GetSubsystem<UAsdaEquipShopSubsystem>();
			const FGlowInfo IW = Describe(Me, kW), IS = Describe(Me, kS);
			S->Check(Eq && Eq->IsOpen() && IW.Count == 0 && IS.Count == 0,
				FString::Printf(TEXT("F2 магазин 6800 открыт %d (секунду): свечения нет и сверка его не вернула — оружие %d, щит %d"),
					Eq && Eq->IsOpen() ? 1 : 0, IW.Count, IS.Count));
			Shot(*S, TEXT("glow_6_equipshop_open"));
			if (Eq) { Eq->CloseWindow(); }
		}));
		S->Steps.Add(Wait(0.6f));
		S->Steps.Add(Do([S, kW, kS]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			const FGlowInfo IW = Describe(Me, kW), IS = Describe(Me, kS);
			S->Check(S->Net->SelfWeaponItemId == S->Sword && S->Net->SelfWeaponGrade == kGrade && S->Net->SelfShieldGrade == kGrade,
				FString::Printf(TEXT("F3a магазин закрыт: оружие вернулось на модель с заточкой из надетого (оружие %d +%d, щит +%d)"),
					S->Net->SelfWeaponItemId, S->Net->SelfWeaponGrade, S->Net->SelfShieldGrade));
			S->Check(Alive(IW) && IW.Verts > 0 && Alive(IS), FString::Printf(TEXT("F3 магазин закрыт: свечение построено заново — оружие (%s), щит (%s)"),
				*Str(IW), *Str(IS)));
			const TArray<AAsdaEffectActor*> L = GlowFx(Me, kW);
			S->KeepW = L.Num() == 1 ? L[0] : nullptr;
			if (UAsdaPremiumShopSubsystem* Pr = S->W.Get()->GetGameInstance()->GetSubsystem<UAsdaPremiumShopSubsystem>()) { Pr->OpenWindow(false); }
		}));
		S->Steps.Add(Wait(1.0f));
		S->Steps.Add(Do([S, kW, kS]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			UAsdaPremiumShopSubsystem* Pr = S->W.Get()->GetGameInstance()->GetSubsystem<UAsdaPremiumShopSubsystem>();
			const TArray<AAsdaEffectActor*> L = GlowFx(Me, kW);
			const FGlowInfo IS = Describe(Me, kS);
			S->Check(Pr && Pr->IsOpen() && IS.Count == 0 && L.Num() == 1 && L[0] == S->KeepW.Get(),
				FString::Printf(TEXT("F4 элитный магазин 6000 открыт %d: аура щита снята (%d), свечение оружия то же %d"),
					Pr && Pr->IsOpen() ? 1 : 0, IS.Count, (L.Num() == 1 && L[0] == S->KeepW.Get()) ? 1 : 0));
			if (Pr) { Pr->CloseWindow(); }
		}));
		S->Steps.Add(Wait(0.6f));
		S->Steps.Add(Do([S, kS]()
		{
			const FGlowInfo IS = Describe(MeOf(*S), kS);
			S->Check(Alive(IS), FString::Printf(TEXT("F5 элитный магазин закрыт: аура щита снова есть (%s)"), *Str(IS)));
		}));

		// ═══ H. Меш щита и оружия на всех ступенях заточки (05.11, жалоба «после улучшения щита свечение видно, а щита нет») ═══
		//   Свой: надевание путём игрока (контейнер 3 + 5001) со ступенью 0…12; сосед: 4021 со ступенью в байте +8. На каждой ступени —
		//   ровно один видимый зарегистрированный меш щита и оружия с загруженным мешем; при ступени ≥ 5 рядом свечение.
		S->Steps.Add(Do([S, kS]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			auto MeshInfo = [](const AActor* A, const FName& Tag, int32& OutN, int32& OutVis)
			{
				OutN = OutVis = 0;
				if (!A) { return; }
				TInlineComponentArray<UStaticMeshComponent*> Comps(A);
				for (const UStaticMeshComponent* C : Comps)
				{
					if (!IsValid(C) || !C->IsRegistered() || !C->ComponentHasTag(Tag) || !C->GetStaticMesh()) { continue; }
					++OutN;
					if (C->IsVisible() && !C->bHiddenInGame) { ++OutVis; }
				}
			};
			int32 Bad = 0; FString BadList;
			for (int32 G = 0; G <= 12; ++G)
			{
				WearGraded(*S, 8, S->Shield, G);
				WearGraded(*S, 9, S->Sword, G);
				int32 NS = 0, VS = 0, NW = 0, VW = 0;
				MeshInfo(Me, UAsdaCharAssembly::ShieldMeshTag, NS, VS);
				MeshInfo(Me, UAsdaCharAssembly::WeaponMeshTag, NW, VW);
				const int32 NGlow = GlowFx(Me, kS).Num();
				const bool bOk = NS == 1 && VS == 1 && NW == 1 && VW == 1 && (G < 5 || NGlow == 1);
				if (!bOk) { ++Bad; BadList += FString::Printf(TEXT(" +%d[щит %d/%d, оружие %d/%d, аура %d]"), G, VS, NS, VW, NW, NGlow); }
			}
			S->Check(Bad == 0, FString::Printf(TEXT("H1 свой: щит %d и оружие %d на ступенях 0…12 — меш виден везде (сбоев %d)%s"),
				S->Shield, S->Sword, Bad, *BadList));
			// H1b: ВСЕ модели щитов таблицы (по одному коду на меш своего пола), ступени 0, 5, 7, 10 — меш виден, материал не пуст.
			TSet<FString> Seen; int32 NMesh = 0, BadAll = 0; FString BadAllList;
			for (const FAsdaItemRecord& R : S->Items->GetAllItems())
			{
				if (R.ItemType != 8) { continue; }
				const FString Mesh = S->Items->GetItemMeshName(R.ItemCode, S->WS->SelfSex != 2);
				if (Mesh.IsEmpty() || Mesh.StartsWith(TEXT("itm1_")) || Seen.Contains(Mesh)) { continue; }   // без модели — как у эталона
				Seen.Add(Mesh); ++NMesh;
				for (const int32 G : { 0, 5, 7, 10 })
				{
					WearGraded(*S, 8, R.ItemCode, G);
					int32 NS = 0, VS = 0;
					MeshInfo(Me, UAsdaCharAssembly::ShieldMeshTag, NS, VS);
					bool bMat = false;
					TInlineComponentArray<UStaticMeshComponent*> Comps(Me);
					for (const UStaticMeshComponent* C : Comps)
					{
						if (IsValid(C) && C->ComponentHasTag(UAsdaCharAssembly::ShieldMeshTag)) { bMat = C->GetNumMaterials() > 0 && C->GetMaterial(0) != nullptr; }
					}
					if (!(NS == 1 && VS == 1 && bMat)) { ++BadAll; BadAllList += FString::Printf(TEXT(" %d+%d(%s)"), R.ItemCode, G, *Mesh); }
				}
			}
			S->Check(NMesh >= 15 && BadAll == 0, FString::Printf(TEXT("H1b все модели щитов (%d) × ступени 0/5/7/10: меш виден, материал есть (сбоев %d)%s"),
				NMesh, BadAll, *BadAllList));
			WearGraded(*S, 8, S->Shield, 7);
			Aim(*S, Me);
		}));
		S->Steps.Add(Wait(0.6f));
		S->Steps.Add(Do([S]() { Shot(*S, TEXT("shield_h1_self_plus12")); }));
		S->Steps.Add(Do([S]()
		{
			// Своё улучшение — заново надетый тот же щит с другой ступенью (ответ улучшения пишет ячейку сумки; модель меняет надевание).
			WearGraded(*S, 8, S->Shield, 7);
			WearGraded(*S, 9, S->Sword, 7);
		}));

		// ═══ S. Обводка самоцвета (itemSetupSowelGlow@0x9ED480): оружие — самоцвет в гнезде 0 и заточка >= 6, щит — так же. ═══
		S->Steps.Add(Do([S]()
		{
			const UAsdaItemTableSubsystem* IT = S->Items.Get();
			for (int32 Code = 1; Code < 200000 && S->SowelCode == 0; ++Code)
			{
				FAsdaSowellRow Row;
				if (IT->IsSowelCode(Code) && IT->GetSowell(Code, Row)) { S->SowelCode = Code; S->SowelClass = Row.PartGrade; }
			}
			AAsdaPlayerCharacter* Me = MeOf(*S);
			S->Check(S->SowelCode > 0, FString::Printf(TEXT("S0 самоцвет для проверки: %d (класс %d)"), S->SowelCode, S->SowelClass));
			S->CVars->Set(TEXT("asda.Char.SowelGlow"), 1);
			WearSowel(*S, 9, S->Sword, 5, S->SowelCode);
			S->Check(AsdaSowelGlowCount(Me) == 0, FString::Printf(TEXT("S1 оружие +5 с самоцветом: обводки нет (%d) — порог 6"), AsdaSowelGlowCount(Me)));
			WearSowel(*S, 9, S->Sword, 6, S->SowelCode);
			S->Check(AsdaSowelGlowCount(Me) == 1, FString::Printf(TEXT("S2 оружие +6 с самоцветом: обводка есть (%d)"), AsdaSowelGlowCount(Me)));
			WearSowel(*S, 8, S->Shield, 7, S->SowelCode);
			S->Check(AsdaSowelGlowCount(Me) == 2, FString::Printf(TEXT("S3 + щит +7 с самоцветом: обводок %d (оружие и щит)"), AsdaSowelGlowCount(Me)));
			Aim(*S, Me);
		}));
		S->Steps.Add(Wait(1.0f));
		S->Steps.Add(Do([S]() { Shot(*S, TEXT("sowel_1_on")); }));
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			WearSowel(*S, 9, S->Sword, 7, 0);   // самоцвет извлечён
			S->Check(AsdaSowelGlowCount(Me) == 1, FString::Printf(TEXT("S4 из оружия самоцвет вынут: осталась обводка щита (%d)"), AsdaSowelGlowCount(Me)));
			S->CVars->Set(TEXT("asda.Char.SowelGlow"), 0);
			WearSowel(*S, 9, S->Sword, 7, S->SowelCode);
			S->Check(AsdaSowelGlowCount(Me) == 0, FString::Printf(TEXT("S5 ключ asda.Char.SowelGlow 0 (как было): обводок нет (%d)"), AsdaSowelGlowCount(Me)));
			S->CVars->Set(TEXT("asda.Char.SowelGlow"), 1);
			WearSowel(*S, 9, S->Sword, 7, S->SowelCode);
			S->Check(AsdaSowelGlowCount(Me) == 2, FString::Printf(TEXT("S6 ключ 1: обводки снова %d"), AsdaSowelGlowCount(Me)));
		}));
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]()
		{
			// Убрать самоцветы: дальше тест идёт с обычными вещами.
			WearSowel(*S, 9, S->Sword, 7, 0);
			WearSowel(*S, 8, S->Shield, 7, 0);
		}));

		// ═══ G. Все пути воскрешения + прочие эффекты экипировки (05.11, повтор жалобы на alpha-6.1.0) ═══
		//   G1 крылья и кольцо соулмейта на себе; G2 смерть 4053; G3 воскрешение «в городе» на той же карте 4055; G4 смерть + новый
		//   павн (смена карты после смерти: павн создаётся заново пакетом 4003); G5 «душа» (SoulState 1); G6 оператор (тип 4) — сервер
		//   шлёт тот же 4056, что и обычное воскрешение (RecvCharRegen@0x52A570), проверено в B2.
		auto CheckAll = [S, kW, kS](const TCHAR* Step, bool bExpectWingRing)
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			const FGlowInfo IW = Describe(Me, kW), IS = Describe(Me, kS);
			const int32 NWing = CountTag(Me, AsdaCharFxTags::kWing), NRing = CountTag(Me, AsdaCharFxTags::kSoulRing);
			S->Check(Me && Alive(IW) && IW.Verts > 0 && Alive(IS) && (!bExpectWingRing || (NWing == 1 && NRing == 1)),
				FString::Printf(TEXT("%s: оружие (%s), щит (%s), крылья %d, кольцо %d"), Step, *Str(IW), *Str(IS), NWing, NRing));
		};
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]()
		{
			// Путь игрока: крылья — пакет 6585 (charId, wingId, срок, вкл.), кольцо соулмейта — 5220 (пара есть, soulIdx 5).
			if (S->Net->SelectedCharId == 0) { S->Net->SelectedCharId = 777001; }   // офлайн charId (вернёт FAsdaNetSelfScope)
			TArray<uint8> W; W.SetNumZeroed(11);
			AsdaBytes::WrI32(W.GetData(), (int32)S->Net->SelectedCharId); AsdaBytes::WrI16(W.GetData() + 4, 1); AsdaBytes::WrI32(W.GetData() + 6, 3600); W[10] = 1;
			S->Net->DebugFeedPacket(6585, W);
			TArray<uint8> R; R.SetNumZeroed(101);
			AsdaBytes::WrI32(R.GetData(), (int32)S->Net->SelectedCharId); AsdaBytes::WrI16(R.GetData() + 4, (int16)kMe);
			AsdaBytes::WrI32(R.GetData() + 6, 5); AsdaBytes::WrI16(R.GetData() + 6 + 17, -1);
			S->Net->DebugFeedPacket(5220, R);
			S->OrigMapId = S->Net->EnterMapId;
			if (S->Net->EnterMapId < 0 || S->Net->EnterMapId > 255) { S->Net->EnterMapId = 3; }   // 4055 «та же карта» — без перезагрузки уровня
		}));
		S->Steps.Add(Wait(0.6f));
		S->Steps.Add(Do([CheckAll]() { CheckAll(TEXT("G1 жив, крылья и кольцо надеты"), true); }));
		S->Steps.Add(Do([S]() { S->Net->DebugFeedPacket(4053, Body4053(kMe)); }));
		S->Steps.Add(Wait(2.2f));
		S->Steps.Add(Do([S, CheckAll]() { CheckAll(TEXT("G2 смерть 4053"), true); Aim(*S, MeOf(*S)); }));
		S->Steps.Add(Wait(0.4f));
		S->Steps.Add(Do([S]() { Shot(*S, TEXT("glow_7_dead_wings_ring")); }));
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]()
		{
			if (AAsdaPlayerCharacter* Me = MeOf(*S)) { S->Net->DebugFeedPacket(4055, Body4055(kMe, S->Net->EnterMapId, Me->GetGamePosXZ())); }
		}));
		S->Steps.Add(Wait(1.5f));
		S->Steps.Add(Do([S, CheckAll]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			S->Check(Me && Me->AnimState != EAsdaPlayerState::Dead && !S->Net->bSelfDead,
				FString::Printf(TEXT("G3a воскрешение 4055 (та же карта %d): мёртв %d"), S->Net->EnterMapId, S->Net->bSelfDead ? 1 : 0));
			CheckAll(TEXT("G3 после воскрешения 4055"), true);
			Aim(*S, Me);
		}));
		S->Steps.Add(Wait(0.4f));
		S->Steps.Add(Do([S]() { Shot(*S, TEXT("glow_8_revived_4055")); }));
		// G4: смерть и новый павн (так приходит воскрешение в городе на ДРУГОЙ карте: уровень грузится заново, павн — новый).
		S->Steps.Add(Do([S]() { S->Net->DebugFeedPacket(4053, Body4053(kMe)); }));
		S->Steps.Add(Wait(1.5f));
		S->Steps.Add(Do([S]()
		{
			S->OldPawn = MeOf(*S);
			if (AAsdaPlayerCharacter* Me = MeOf(*S)) { Me->Destroy(); }
			S->Net->bSelfDead = false;
			S->Net->DebugFeedPacket(4003, Body4003(kMe));
		}));
		S->Steps.Add(Wait(4.0f));   // столб появления
		S->Steps.Add(Do([S, CheckAll]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			S->Check(Me && Me != S->OldPawn.Get(), FString::Printf(TEXT("G4a новый павн после смерти (смена карты): %s"), Me ? *Me->GetName() : TEXT("НЕТ")));
			CheckAll(TEXT("G4 новый павн после смерти — свечение из надетого, крылья/кольцо из кэша 6585/5220"), true);
			Aim(*S, Me);
		}));
		S->Steps.Add(Wait(0.4f));
		S->Steps.Add(Do([S]() { Shot(*S, TEXT("glow_9_new_pawn")); }));
		// G5: «душа» (SoulState 1): эталон эффекты у души не трогает — свечение остаётся.
		S->Steps.Add(Do([S]() { S->Net->SetSelfSoulState(1); }));
		S->Steps.Add(Wait(1.5f));
		S->Steps.Add(Do([S, CheckAll]() { CheckAll(TEXT("G5 «душа» SoulState 1"), true); S->Net->SetSelfSoulState(0); }));
		S->Steps.Add(Wait(0.5f));
		S->Steps.Add(Do([S]()
		{
			if (S->OrigMapId != INT_MIN) { S->Net->EnterMapId = S->OrigMapId; }
			TArray<uint8> W; W.SetNumZeroed(11);   // крылья сняты (6585 выкл.), пары нет (5220 soulIdx 0)
			AsdaBytes::WrI32(W.GetData(), (int32)S->Net->SelectedCharId); AsdaBytes::WrI16(W.GetData() + 4, 1);
			S->Net->DebugFeedPacket(6585, W);
			TArray<uint8> R; R.SetNumZeroed(101);
			AsdaBytes::WrI32(R.GetData(), (int32)S->Net->SelectedCharId); AsdaBytes::WrI16(R.GetData() + 4, (int16)kMe); AsdaBytes::WrI16(R.GetData() + 6 + 17, -1);
			S->Net->DebugFeedPacket(5220, R);
		}));

		// ═══ E. Сосед ═══
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]()
		{
			const FVector2D P = MeOf(*S)->GetGamePosXZ();
			S->Net->DebugFeedPacket(4005, Body4005(kPeer, S->Shield, S->Sword, kGrade, P.X + 150.f, P.Y));
		}));
		S->Steps.Add(Wait(1.5f));
		S->Steps.Add(Do([S, kW]()
		{
			AAsdaPlayerCharacter* PA = Cast<AAsdaPlayerCharacter>(S->WS->ActorByUserIdx(kPeer));
			const FGlowInfo IW = Describe(PA, kW);
			S->Check(PA && Alive(IW), FString::Printf(TEXT("E1 сосед 4005 +%d: свечение оружия (%s)"), kGrade, *Str(IW)));
			S->Net->DebugFeedPacket(4053, Body4053(kPeer));
		}));
		S->Steps.Add(Wait(2.0f));
		S->Steps.Add(Do([S, kW]()
		{
			AAsdaPlayerCharacter* PA = Cast<AAsdaPlayerCharacter>(S->WS->ActorByUserIdx(kPeer));
			const FGlowInfo IW = Describe(PA, kW);
			S->Check(PA && PA->AnimState == EAsdaPlayerState::Dead && Alive(IW),
				FString::Printf(TEXT("E2 сосед умер 4053 (труп %d): свечение остаётся (%s)"), PA && PA->AnimState == EAsdaPlayerState::Dead ? 1 : 0, *Str(IW)));
		}));
		// E3: сосед воскрес (4056 по его userIdx — так сервер рассылает воскрешение по сектору). Эталон (game_main.c:13059):
		//   HP/MP, перенос на тайл, поза покоя; при @20 == 200 — прибыл мёртвым. Ключ `asda.Peer.Revive4056` 0 — как было.
		S->Steps.Add(Do([S]()
		{
			S->CVars->Set(TEXT("asda.Peer.Revive4056"), 0);
			if (AAsdaPlayerCharacter* PA = Cast<AAsdaPlayerCharacter>(S->WS->ActorByUserIdx(kPeer)))
			{
				S->Net->DebugFeedPacket(4056, Body4056(kPeer, PA->GetGamePosXZ()));
			}
		}));
		S->Steps.Add(Wait(1.2f));
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* PA = Cast<AAsdaPlayerCharacter>(S->WS->ActorByUserIdx(kPeer));
			S->Check(PA && PA->AnimState == EAsdaPlayerState::Dead,
				FString::Printf(TEXT("E3a ключ asda.Peer.Revive4056 0 (как было): сосед после 4056 лежит трупом (%d) — жалоба воспроизведена"),
					PA && PA->AnimState == EAsdaPlayerState::Dead ? 1 : 0));
			Aim(*S, PA);
		}));
		S->Steps.Add(Wait(0.5f));
		S->Steps.Add(Do([S]() { Shot(*S, TEXT("peer_1_key0_still_dead")); }));
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]()
		{
			S->CVars->Set(TEXT("asda.Peer.Revive4056"), 1);
			if (AAsdaPlayerCharacter* PA = Cast<AAsdaPlayerCharacter>(S->WS->ActorByUserIdx(kPeer)))
			{
				S->PeerTarget = PA->GetGamePosXZ() + FVector2D(300.f, 0.f);   // воскрешение «в городе» той же карты — на 3 тайла дальше
				TArray<uint8> B = Body4056(kPeer, S->PeerTarget);
				AsdaBytes::WrI32(B.GetData() + 6, 777);   // HP после воскрешения
				S->Net->DebugFeedPacket(4056, B);
			}
		}));
		S->Steps.Add(Wait(1.2f));
		S->Steps.Add(Do([S, kW, kS]()
		{
			AAsdaPlayerCharacter* PA = Cast<AAsdaPlayerCharacter>(S->WS->ActorByUserIdx(kPeer));
			const FGlowInfo IW = Describe(PA, kW), IS = Describe(PA, kS);
			const FVector2D Want = AsdaCoord::TileCenterGame(FMath::RoundToInt(S->PeerTarget.X / 100.f), FMath::RoundToInt(S->PeerTarget.Y / 100.f));
			const float Dist = PA ? (float)FVector2D::Distance(PA->GetGamePosXZ(), Want) : -1.f;
			const UAsdaNetSubsystem::FAsdaVitals* V = S->Net->VitalsByUserIdx.Find(kPeer);
			S->Check(PA && PA->AnimState != EAsdaPlayerState::Dead && Dist >= 0.f && Dist < 60.f && V && V->HpCur == 777,
				FString::Printf(TEXT("E3 сосед воскрес 4056 (ключ 1): поза %d (не труп), до тайла воскрешения %.0f, HP %d"),
					PA ? (int32)PA->AnimState : -1, Dist, V ? V->HpCur : -1));
			S->Check(PA && Alive(IW) && Alive(IS), FString::Printf(TEXT("E3b свечение у воскресшего соседа: оружие (%s), щит (%s)"), *Str(IW), *Str(IS)));
			Aim(*S, PA);
		}));
		S->Steps.Add(Wait(0.5f));
		S->Steps.Add(Do([S]() { Shot(*S, TEXT("peer_2_key1_revived")); }));
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]()
		{
			if (AAsdaPlayerCharacter* PA = Cast<AAsdaPlayerCharacter>(S->WS->ActorByUserIdx(kPeer)))
			{
				TArray<uint8> B = Body4056(kPeer, PA->GetGamePosXZ());
				AsdaBytes::WrI16(B.GetData() + 20, 200);   // прибыл мёртвым
				S->Net->DebugFeedPacket(4056, B);
			}
		}));
		S->Steps.Add(Wait(1.0f));
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* PA = Cast<AAsdaPlayerCharacter>(S->WS->ActorByUserIdx(kPeer));
			S->Check(PA && PA->AnimState == EAsdaPlayerState::Dead,
				FString::Printf(TEXT("E3c 4056 с @20 == 200 (прибыл мёртвым): сосед в позе трупа (%d)"), PA ? (int32)PA->AnimState : -1));
		}));
		// H2: сосед — 4021 (слот 8 щит / 9 оружие) со ступенью заточки 0…12 в байте +8: меш щита и оружия на месте.
		S->Steps.Add(Do([S, kS]()
		{
			AAsdaPlayerCharacter* PA = Cast<AAsdaPlayerCharacter>(S->WS->ActorByUserIdx(kPeer));
			auto Count = [](const AActor* A, const FName& Tag)
			{
				int32 N = 0;
				if (!A) { return N; }
				TInlineComponentArray<UStaticMeshComponent*> Comps(A);
				for (const UStaticMeshComponent* C : Comps)
				{
					if (IsValid(C) && C->IsRegistered() && C->ComponentHasTag(Tag) && C->GetStaticMesh() && C->IsVisible() && !C->bHiddenInGame) { ++N; }
				}
				return N;
			};
			int32 Bad = 0; FString BadList;
			for (int32 G = 0; G <= 12; ++G)
			{
				for (const int32 Slot : { 8, 9 })
				{
					TArray<uint8> B; B.SetNumZeroed(9);
					AsdaBytes::WrI16(B.GetData(), (int16)kPeer); AsdaBytes::WrI16(B.GetData() + 2, (int16)Slot);
					AsdaBytes::WrI32(B.GetData() + 4, Slot == 8 ? S->Shield : S->Sword); B[8] = (uint8)G;
					S->Net->DebugFeedPacket(4021, B);
				}
				const int32 NS = Count(PA, UAsdaCharAssembly::ShieldMeshTag), NW = Count(PA, UAsdaCharAssembly::WeaponMeshTag);
				const int32 NGlow = GlowFx(PA, kS).Num();
				if (!(NS == 1 && NW == 1 && (G < 5 || NGlow == 1))) { ++Bad; BadList += FString::Printf(TEXT(" +%d[щит %d, оружие %d, аура %d]"), G, NS, NW, NGlow); }
			}
			S->Check(PA && Bad == 0, FString::Printf(TEXT("H2 сосед 4021 ступени 0…12: меш щита и оружия виден везде (сбоев %d)%s"), Bad, *BadList));
		}));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestWeaponGlowPersist(
	TEXT("asda.SelfTest.WeaponGlowPersist"),
	TEXT("Самотест свечения заточки как состояния кадра (CCharacter_UpdateHeldItemEffect каждый кадр): +7 оружие и щит, смерть 4053 и "
	     "воскрешение 4056, снос и залипшее скрытие эффекта, скрытый носитель, ключ asda.WeaponGlow.Persist 0/1, сосед 4005/4053. "
	     "Офлайн, карта мира. `shot=<папка>` — кадры, `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaGlowPersistTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaItemTableSubsystem* Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		if (!Net || !WS || !Items || !Inv || !IConsoleManager::Get().FindConsoleVariable(kKey))
		{
			AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем сети/мира/предметов/инвентаря или ключа отката"));
			return;
		}
		TSharedRef<FState> SP = MakeShared<FState>();
		SP->W = W; SP->bQuit = KA.bQuit; SP->Net = Net; SP->WS = WS; SP->Items = Items; SP->Inv = Inv;
		for (const FString& P : KA.Pos)
		{
			if (P.StartsWith(TEXT("shot="), ESearchCase::IgnoreCase))
			{
				SP->ShotDir = FPaths::ConvertRelativePathToFull(P.Mid(5));
				IFileManager::Get().MakeDirectory(*SP->ShotDir, /*Tree*/true);
			}
		}
		SP->NetScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		SP->CVars = MakeUnique<FAsdaCVarScope>(W, kCmd);
		SP->CVars->Set(kKey, 1);
		SP->CVars->Set(TEXT("asda.WeaponGlow.ShopGate"), 1);   // гейт магазинов эталона (шаги F)
		Build(SP);
		SP->StepT0 = W->GetTimeSeconds();
		SP->H = FWorldDelegates::OnWorldPostActorTick.AddLambda([SP](UWorld* InW, ELevelTick, float) { Tick(SP, InW); });
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
