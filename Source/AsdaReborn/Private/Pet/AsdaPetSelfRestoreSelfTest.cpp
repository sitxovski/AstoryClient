// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «СВОЙ ПИТОМЕЦ ПОСЛЕ ВХОДА И ПОРТАЛА» `asda.Pet.SelfTestSelfRestore` (30.10.2026). [ПОРТ]
//   Дефект владельца (выпуск alpha-4.0.12): вошёл в мир с уже призванным SnowLion — панель «SnowLion, Зародыш(1)/(4)» горит,
//   а питомца рядом нет (`Reports/bugs_2026-10-29/pet31_missing_4012.webp`). Разбор и правка — `RestoreSelfPetFromList`
//   (`World/AsdaWorldSubsystem.h`), ключ `asda.Pet.SelfRestore`; отчёт — `Reports/pet31_snowlion/SELF_RESTORE.md`.
//
// ★ КАК (путь игрока, без сети): список питомцев приходит НАСТОЯЩИМ приёмником сети (`DebugFeedPacket(6106)`: одна запись,
//   признак «призван» = 1, как у сервера при входе), затем свой персонаж спавнится заново `Asda.World.FakeSelfSpawn` — тот же
//   `TrySpawnLocalPlayer`, что на входе в мир и после портала (4003). Своего 6120 нет — ровно как в живом журнале владельца.
//   Проверки по каждой из 32 пород `DT_PetModels`:
//     актор питомца есть, его хозяин — НОВЫЙ свой павн, порода совпадает, модель стоит, не скрыт, отрисован за последние
//     0.5 с (`WasRecentlyRendered`), стоит в 40–60 см от хозяина по плоскости, высота = эталонной формуле
//     (наземный — сетка под ним, парящий — Z хозяина + 20 + HoverHeight).
//   Затем: «6106 после спавна» (список пришёл, когда персонаж уже стоит) — питомец появляется на сливе очереди ≤ 2.5 с;
//   контроль: ключ `asda.Pet.SelfRestore 0` — после спавна питомца нет (дефект воспроизводится).
// Консоль: `asda.Pet.SelfTestSelfRestore [delay=N] [quit]`. Только офлайн (`-AsdaNoNet`).
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestKit.h"              // каркас: аргументы, отказ, ключи, итог, выход
#include "Test/AsdaSelfTestRunGate.h"   // FAsdaSelfTestRunGate — отложенный старт   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaNetSubsystem.h"                  // DebugFeedPacket, SelectedCharId, свой тайл
#include "AsdaBytes.h"                         // запись полей тела 6106
#include "AsdaDataTableCache.h"                // AsdaDt::Cached — DT_PetModels
#include "Login/AsdaLoginData.h"               // EncodeCp1251 — кличка в теле
#include "Pet/AsdaPetSubsystem.h"
#include "World/AsdaPetActor.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaGroundTrace.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"                       // TActorIterator — поиск актора питомца
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPetRestoreTest, Log, All);

namespace AsdaPetRestoreTestLocal
{
	static const TCHAR* kCmd = TEXT("asda.Pet.SelfTestSelfRestore");
	static const TCHAR* kPfx = TEXT("САМОТЕСТ-ПИТОМЕЦ-ВХОД");
	constexpr int32 kUid = 990031;          // uid записи стенда
	constexpr int32 kTestCharId = 77031;    // свой charId, если офлайн его нет (0 = «свой неизвестен»)
	constexpr double kCheckAfter = 0.6;     // проверка через это время после спавна (несколько кадров отрисовки)

	struct FRow { int32 Type; bool bHover; float Hover; };

	// Шаги сценария.
	enum class EStep : uint8 { PerType, WorldReady, LateList, Control, Done };

	struct FState
	{
		TWeakObjectPtr<UWorld> W;
		bool bQuit = false;
		int32 Ok = 0, Fail = 0;
		TUniquePtr<FAsdaCVarScope> Cv;
		FTSTicker::FDelegateHandle Tick;
		TArray<FRow> Rows;
		int32 SavedCharId = 0;
		EStep Step = EStep::PerType;
		int32 Idx = 0;             // порода в шаге PerType
		double T0 = 0.0;           // начало текущего ожидания
		double TPet = 0.0;         // шаг «6106 после спавна»: когда питомец появился (0 — ещё нет)
		int32 BadTypes = 0;
		FString BadList;

		void Expect(bool b, const FString& What)
		{
			b ? ++Ok : ++Fail;
			UE_LOG(LogAsdaPetRestoreTest, Warning, TEXT("%s %s %s"), kPfx, b ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), *What);
		}
	};

	static UAsdaNetSubsystem* NetOf(UWorld* W) { return AsdaSelfTestKit::NetOf(W); }
	static UAsdaWorldSubsystem* WsOf(UWorld* W)
	{
		return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	}

	// Настоящее тело 6106: charId, флаг 0, одна запись 46 б (uid, PetRecord 41 б, «призван» +45).
	static void Feed6106(UWorld* W, int32 Type, bool bActive)
	{
		UAsdaNetSubsystem* Net = NetOf(W);
		TArray<uint8> B; B.SetNumZeroed(5 + 46);
		AsdaBytes::WrI32(B.GetData(), Net->SelectedCharId);
		uint8* R = B.GetData() + 5;
		AsdaBytes::WrI32(R, kUid);
		uint8* P = R + 4;
		AsdaBytes::WrI16(P + 0, Type);
		FAsdaLoginData::EncodeCp1251(TEXT("SnowLion"), P + 2, 16);
		P[18] = 90; P[19] = 1; P[20] = 4;   // сытость, уровень, предел
		R[45] = bActive ? 1 : 0;
		Net->DebugFeedPacket(6106, B);
	}

	// Свой персонаж заново — тот же путь, что вход в мир и портал (4003 → TrySpawnLocalPlayer).
	static void RespawnSelf(UWorld* W)
	{
		UAsdaNetSubsystem* Net = NetOf(W);
		if (GEngine) { GEngine->Exec(W, *FString::Printf(TEXT("Asda.World.FakeSelfSpawn %d %d"), Net->SelfTileX, Net->SelfTileZ)); }
	}

	// Актор питомца своего павна (nullptr — нет).
	static AAsdaPetActor* SelfPet(UWorld* W)
	{
		UAsdaWorldSubsystem* WS = WsOf(W);
		AActor* Me = WS ? WS->LocalPlayerActor.Get() : nullptr;
		if (!Me) { return nullptr; }
		for (TActorIterator<AAsdaPetActor> It(W); It; ++It)
		{
			if (IsValid(*It) && It->OwnerChar.Get() == Me) { return *It; }
		}
		return nullptr;
	}

	// Полная проверка питомца породы R. Пусто — всё в порядке, иначе — что не так.
	static FString CheckPet(UWorld* W, const FRow& R)
	{
		UAsdaWorldSubsystem* WS = WsOf(W);
		AActor* Me = WS ? WS->LocalPlayerActor.Get() : nullptr;
		AAsdaPetActor* P = SelfPet(W);
		if (!Me) { return TEXT("нет своего павна"); }
		if (!P) { return TEXT("актора питомца нет"); }
		FString Bad;
		// порода 0 («нет») в списке становится 15 — как у эталона (`petDataSelectInvItemById`: `if (!a4) a4 = 15;`, порт — `AddOrUpdate`)
		const int32 WantType = (R.Type == 0) ? 15 : R.Type;
		if (P->PetTypeId != WantType) { Bad += FString::Printf(TEXT(" порода %d (ждали %d)"), P->PetTypeId, WantType); }
		if (!P->Mesh || !P->Mesh->GetSkeletalMeshAsset()) { Bad += TEXT(" без модели"); }
		if (P->IsHidden() || (P->Mesh && !P->Mesh->IsVisible())) { Bad += TEXT(" скрыт"); }
		if (P->Mesh && !P->Mesh->WasRecentlyRendered(0.5f)) { Bad += TEXT(" не отрисован"); }
		const FVector L = P->GetActorLocation(), O = Me->GetActorLocation();
		const double D = FVector::Dist2D(L, O);
		if (D < 40.0 || D > 60.0) { Bad += FString::Printf(TEXT(" до хозяина %.1f"), D); }
		const float Want = R.bHover ? (float)(O.Z + P->FollowOffset.Z + R.Hover) : AsdaGround::GroundZ(W, L.X, L.Y, L.Z);
		if (FMath::Abs(L.Z - Want) > 0.5f) { Bad += FString::Printf(TEXT(" Z %.1f ≠ %.1f"), L.Z, Want); }
		UE_LOG(LogAsdaPetRestoreTest, Log, TEXT("%s порода %2d: хозяин %s, питомец %s, до хозяина %.1f, Z−хозяин %+.1f%s"), kPfx, R.Type,
			*O.ToCompactString(), *L.ToCompactString(), D, L.Z - O.Z, Bad.IsEmpty() ? TEXT("") : *(TEXT(" ✘") + Bad));
		return Bad;
	}

	static void Finish(TSharedRef<FState> S)
	{
		S->Tick.Reset();
		UWorld* W = S->W.Get();
		if (UAsdaNetSubsystem* Net = NetOf(W))
		{
			// убрать питомца стенда штатным путём (свой 6120 с −1) и вернуть свой charId
			TArray<uint8> B; B.SetNumZeroed(22);
			AsdaBytes::WrI32(B.GetData(), Net->SelectedCharId);
			AsdaBytes::WrI16(B.GetData() + 4, -1);
			Net->DebugFeedPacket(6120, B);
			Feed6106(W, 0, false);
			Net->SelectedCharId = S->SavedCharId;
		}
		if (S->Cv) { S->Cv->Restore(); }
		AsdaSelfTestKit::Finish(W, kCmd, S->bQuit, LogAsdaPetRestoreTest, kPfx, S->Ok, S->Fail, TEXT(""), true);
	}

	// Один тик сценария; false — конец.
	static bool Step(TSharedRef<FState> S)
	{
		UWorld* W = S->W.Get();
		if (!W || !NetOf(W) || !WsOf(W)) { Finish(S); return false; }
		const double Now = FPlatformTime::Seconds();
		switch (S->Step)
		{
		case EStep::PerType:
			if (Now - S->T0 < kCheckAfter) { return true; }
			{
				const FString Bad = CheckPet(W, S->Rows[S->Idx]);
				if (!Bad.IsEmpty()) { ++S->BadTypes; S->BadList += FString::Printf(TEXT(" %d(%s)"), S->Rows[S->Idx].Type, *Bad); }
			}
			if (++S->Idx < S->Rows.Num())
			{
				Feed6106(W, S->Rows[S->Idx].Type, true);   // «вход»: список до спавна, как у сервера
				RespawnSelf(W);
				S->T0 = FPlatformTime::Seconds();
				return true;
			}
			S->Expect(S->BadTypes == 0, FString::Printf(TEXT("вход/портал (6106 «призван» до спавна, 6120 нет): питомец у нового павна у всех %d пород — есть, виден, отрисован, на месте (не так: %d%s)"),
				S->Rows.Num(), S->BadTypes, *S->BadList));
			// Настоящий 4056 («мир готов» на той же карте): в игре `OnWorldReady` после 4055/4056 запускает таймер слива очереди
			//   6120 (2 с) — офлайн-спавн его не запускает. Тело — `CharRegenVillageMove@0x52AB80` (22 б, как у asda.SavePoint-стенда).
			{
				UAsdaNetSubsystem* Net = NetOf(W);
				TArray<uint8> B; B.SetNumZeroed(22);
				AsdaBytes::WrI16(&B[0], (int32)Net->SelfUserNum); AsdaBytes::WrI16(&B[2], Net->SelfTileX); AsdaBytes::WrI16(&B[4], Net->SelfTileZ);
				AsdaBytes::WrI32(&B[6], 1000); AsdaBytes::WrI16(&B[10], 100);
				Net->DebugFeedPacket(4056, B);
			}
			S->Step = EStep::WorldReady; S->T0 = FPlatformTime::Seconds();
			return true;
		case EStep::WorldReady:
			if (Now - S->T0 < 1.0) { return true; }
			// «6106 после спавна»: персонаж стоит без питомца (ключ 0 на время спавна), список приходит потом
			S->Cv->Set(TEXT("asda.Pet.SelfRestore"), 0);
			Feed6106(W, 0, false);   // никто не призван
			RespawnSelf(W);
			S->Cv->Set(TEXT("asda.Pet.SelfRestore"), 1);
			S->Expect(SelfPet(W) == nullptr, TEXT("перед «6106 после спавна»: у нового павна питомца нет"));
			Feed6106(W, 31, true);
			S->Step = EStep::LateList; S->T0 = FPlatformTime::Seconds();
			return true;
		case EStep::LateList:
			if (SelfPet(W) == nullptr && Now - S->T0 < 2.6) { return true; }
			// ★ 11.11.2026 (D4, разбор красных эталона): питомец появляется на этом же тике, а `CheckPet` требует
			//   `WasRecentlyRendered` — отрисованным он становится только через кадры. Проверка шла в тик появления и давала
			//   «не отрисован» (эталон 7.6.0: ok=4 fail=1). Срок появления меряется по-прежнему от прихода 6106, а сама
			//   проверка — через `kCheckAfter` после появления, как у пород основного цикла.
			if (SelfPet(W) != nullptr && S->TPet <= 0.0) { S->TPet = Now; }
			if (S->TPet > 0.0 && Now - S->TPet < kCheckAfter) { return true; }
			{
				const double Dt = (S->TPet > 0.0 ? S->TPet : Now) - S->T0;
				const FRow* R31 = S->Rows.FindByPredicate([](const FRow& R) { return R.Type == 31; });
				const FString Bad = R31 ? CheckPet(W, *R31) : FString(TEXT("нет породы 31 в DT_PetModels"));
				S->Expect(Bad.IsEmpty() && Dt <= 2.6, FString::Printf(TEXT("6106 пришёл после спавна: SnowLion появился через %.2f с на сливе очереди (≤ 2.6)%s"),
					Dt, Bad.IsEmpty() ? TEXT("") : *(TEXT(" —") + Bad)));
			}
			// контроль: ключ 0 — спавн с призванным в списке, питомца нет (дефект воспроизводится)
			S->Cv->Set(TEXT("asda.Pet.SelfRestore"), 0);
			RespawnSelf(W);
			S->Step = EStep::Control; S->T0 = FPlatformTime::Seconds();
			return true;
		case EStep::Control:
			if (Now - S->T0 < 2.6) { return true; }   // и слив очереди прошёл
			S->Expect(SelfPet(W) == nullptr, TEXT("контроль: asda.Pet.SelfRestore 0 — после спавна с «призван» в 6106 питомца нет (дефект воспроизводится)"));
			S->Cv->Set(TEXT("asda.Pet.SelfRestore"), 1);
			Finish(S);
			return false;
		default:
			Finish(S);
			return false;
		}
	}
}

struct FAsdaPetSelfRestoreSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaPetRestoreTestLocal;
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
			UAsdaWorldSubsystem* WS = WsOf(Wd);
			if (!Net || !WS || !WS->LocalPlayerActor) { AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("нет сети / мира / своего персонажа")); return; }
			TSharedRef<FState> S = MakeShared<FState>();
			S->W = Wd; S->bQuit = bQuit;
			S->Cv = MakeUnique<FAsdaCVarScope>(Wd, kCmd);
			S->Cv->Set(TEXT("asda.Pet.SelfRestore"), 1);
			S->SavedCharId = Net->SelectedCharId;
			if (Net->SelectedCharId == 0) { Net->SelectedCharId = kTestCharId; }   // «свой неизвестен» отбрасывает 6106
			if (const UDataTable* T = AsdaDt::Cached(AAsdaPetActor::kPetModelsPath))
			{
				for (const TPair<FName, uint8*>& It : T->GetRowMap())
				{
					const FAsdaPetModelRow* R = reinterpret_cast<const FAsdaPetModelRow*>(It.Value);
					if (R) { S->Rows.Add({R->PetTypeId, R->bHover, R->HoverHeight}); }
				}
				S->Rows.Sort([](const FRow& a, const FRow& b) { return a.Type < b.Type; });
			}
			if (S->Rows.Num() == 0) { AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("нет DT_PetModels")); Net->SelectedCharId = S->SavedCharId; return; }
			S->Expect(S->Rows.Num() == 32, FString::Printf(TEXT("пород в DT_PetModels: %d (ждали 32)"), S->Rows.Num()));
			Feed6106(Wd, S->Rows[0].Type, true);
			RespawnSelf(Wd);
			S->T0 = FPlatformTime::Seconds();
			S->Tick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float) -> bool { return Step(S); }));
		}), A.Delay + 2.f, false);
	}
};

static FAutoConsoleCommandWithWorldAndArgs GAsdaPetSelfRestoreSelfTest(
	TEXT("asda.Pet.SelfTestSelfRestore"),
	TEXT("Самотест «свой питомец после входа и портала» (30.10.2026): 6106 с призванным через приёмник сети + спавн своего персонажа ")
	TEXT("(путь 4003), 32 породы — есть/виден/на месте; 6106 после спавна; контроль asda.Pet.SelfRestore 0. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W) { FAsdaPetSelfRestoreSelfTest::Run(A, W); }));

#endif // !UE_BUILD_SHIPPING
