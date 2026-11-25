// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ВЛАДЕЛЕЦ ГОРОДА ПОСЛЕ ВОЙНЫ — ФЛАГИ НА ПУТИ ИГРОКА» (08.11.2026, пачка 17, баг 1): `asda.SelfTest.CityWarOwner [quit]`.
//   Жалоба владельца: «выиграл войну за город за Свет, а город не покрылся флагами победителя».
//   Полный поток оригинала (разбор — `Reports/batch17/city_war/REPORT.md`):
//     • сервер шлёт 6743 (274 б) ВСЕМ в мире при ответе чат-сервера 0xE27 (`sub_46AE10` → `Send_RoseWarOwnerInfo@0x46B060`)
//       и КАЖДОМУ входящему на карту (`CharMake@0x543A10`: кэш, иначе запрос CSC_ROSEWAR_OWNERINFO_REQ → тот же ответ);
//     • бой (карта 19) владельца НЕ меняет: `Game_Chat_RoseWarMapClose` добавляет городу очко победы фракции
//       (WinPoint1..3), владелец пересчитывается чат-сервером при недельном подсчёте (живой журнал: пн 02.11 08:00:00);
//     • клиент: `uniconWar_UpdateFactionStateUI@0x9C8B90` пишет состояние трёх городов, `uniconWarApplyTex0/1@0x9C8F30/0x9C90C0`
//       при ОТРИСОВКЕ подменяют unicon.TEX / unicon03.TEX текстурой фракции (карта 3 → запись 0, 0 → 1, 7 → 2).
//   Порт: `UAsdaWorldSubsystem::ApplyCityWarTextures` (подписан на `OnCityOwnerInfo`, вызывается из `RevealWorld`).
//   Самотест идёт ШТАТНЫМ путём: 6743 подаётся через `DebugFeedPacket` → `HandlePacket` (тот же разбор, что у сети).
//   Для КАЖДОГО из трёх городов (Силарис 0, Альпия 3, Пламения 7):
//     1) ВХОД ПОСЛЕ ВОЙНЫ: номер карты уже новый, 6743 (город за Светом) приходит ДО готовности мира (как ответ на вход
//        во время загрузки) → мир поднялся → вызов `RevealWorld` (`ApplyCityWarTextures`) → все флаги/площадки = Свет;
//     2) СМЕНА НА ГЛАЗАХ: 6743 «город за Тьмой» (Свет проиграл) → «Хаос» → «не занят (−1)» → снова «Свет» (Свет выиграл);
//        после каждого — сверка КАЖДОГО слота и кадр. Остальные два города в пакете всегда держат ДРУГИХ хозяев —
//        так ловится путаница слотов (миснейм контролов, см. память unicon-war-city-ownership).
//   Кадры: `Reports/batch17/city_war/<ru|en>_<город>_<шаг>_<хозяин>_{wide,close}.png` (своя камера над городом,
//   интерфейс в кадр не попадает). Только офлайн (-AsdaNoNet). Итог: «ВЛАДЕЛЕЦ-ГОРОДА ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "Resources/AsdaMaterialNames.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "UI/AsdaChatSubsystem.h"   // DebugCountSystemLines — строка 111920 в системном чате
#include "UI/AsdaNotice.h"          // AsdaNotice::Loc — имя города (603/600/707)
#include "Camera/CameraActor.h"
#include "Components/MeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCityOwnerTest, Log, All);

namespace AsdaCityWarOwnerSelfTest
{
	// Имя команды (строка итога, отказ, возврат ключей).
	static const TCHAR* kCmd = TEXT("asda.SelfTest.CityWarOwner");
	// Ожидаемые имена подменных текстур по коду фракции (порядок `CMap_LoadUniconWarTextures@0x9C88E0`).
	static const TCHAR* kFlag[4]  = { TEXT("uniconLightFlag"), TEXT("uniconDarkFlag"), TEXT("uniconChaosFlag"), TEXT("uniconNoConqureFlag") };
	static const TCHAR* kFloor[4] = { TEXT("uniconLightFloor"), TEXT("uniconDarkFloor"), TEXT("uniconChaosFloor"), TEXT("uniconNotConqureFloor") };
	// Подписи хозяина в именах кадров.
	static const TCHAR* kFacTag[4] = { TEXT("light"), TEXT("dark"), TEXT("chaos"), TEXT("none") };

	// Один город прогона: уровень, номер карты (как `CurrentMapId`), слот записи 6743, метка кадров.
	struct FCity { const TCHAR* Level; int32 MapId; int32 Slot; const TCHAR* Tag; };
	static const FCity kCities[3] =
	{
		{ TEXT("/Game/Map/silalis/silalis"), 0, 1, TEXT("silalis") },
		{ TEXT("/Game/Map/alpen/alpen"),     3, 0, TEXT("alpen") },
		{ TEXT("/Game/Map/flammio/flammio"), 7, 2, TEXT("flammio") },
	};
	// Хозяева в пакете по шагам «на глазах» (−1 сервер шлёт для ничьего города; клиент читает его как 3).
	static const int32 kLiveOwners[4] = { 1, 2, -1, 0 };

	// Фазы на тиках ядра (переживают смену уровня).
	enum class EPhase : uint8 { WaitWorld, Enter, Live, Done };

	// Состояние прогона.
	struct FRun
	{
		TWeakObjectPtr<UGameInstance> GI;     // экземпляр игры (переживает переход)
		bool bQuit = false;                   // выйти после итога
		int32 Ok = 0, Fail = 0;               // счёт проверок
		int32 City = 0;                       // индекс в kCities
		int32 LiveStep = 0;                   // шаг в kLiveOwners
		EPhase Phase = EPhase::WaitWorld;     // фаза
		double PhaseStart = 0.0;              // начало фазы
		int32 SubStep = 0;                    // 0 = применить и сверить, 1 = кадр общий, 2 = кадр близкий, 3 = дальше
		TWeakObjectPtr<UWorld> OldWorld;      // мир до перехода
		TWeakObjectPtr<ACameraActor> Cam;     // своя камера кадров
		FString Lang;                         // ru | en
		FString ShotDir;                      // папка кадров
		FString ShotBase;                     // имя кадра без суффикса
		int32 ExpectFaction = 0;              // ожидаемый хозяин (0..3) текущего шага
	};
	static TSharedPtr<FRun> GRun;

	// Записать результат проверки.
	static void Expect(FRun& R, bool b, const FString& What)
	{
		if (b) { ++R.Ok; UE_LOG(LogAsdaCityOwnerTest, Log, TEXT("ВЛАДЕЛЕЦ-ГОРОДА ok   %s"), *What); }
		else { ++R.Fail; UE_LOG(LogAsdaCityOwnerTest, Error, TEXT("ВЛАДЕЛЕЦ-ГОРОДА FAIL %s"), *What); }
	}

	// Тело 6743 (274 б) 1:1 с `Send_RoseWarOwnerInfo`: [0] код 1, три записи по 91 б, @0 i16 фракция, @2 налог,
	//   @6 i16 клан (−1 — клана нет; флага клана нет, эмблема остаётся родной).
	static TArray<uint8> Body6743(const int32 Owners[3])
	{
		TArray<uint8> B;
		B.SetNumZeroed(274);
		B[0] = 1;
		for (int32 i = 0; i < 3; ++i)
		{
			uint8* P = B.GetData() + 1 + 91 * i;
			const int16 F = (int16)Owners[i];
			FMemory::Memcpy(P + 0, &F, 2);
			P[2] = 10;
			const int16 NoClan = -1;
			FMemory::Memcpy(P + 6, &NoClan, 2);
		}
		return B;
	}

	// Подать 6743 штатным приёмом: город `Slot` за `Owner`, два других — за РАЗНЫМИ иными фракциями (ловит путаницу слотов).
	static void Feed(UAsdaNetSubsystem* Net, int32 Slot, int32 Owner)
	{
		const int32 Norm = (Owner < 0) ? 3 : Owner;
		int32 Owners[3];
		int32 Other = 0;
		for (int32 i = 0; i < 3; ++i)
		{
			if (i == Slot) { Owners[i] = Owner; continue; }
			while (Other == Norm) { ++Other; }
			Owners[i] = Other % 4;
			++Other;
		}
		Net->DebugFeedPacket(6743, Body6743(Owners));
	}

	// Текстура Tex материала; Native — у исходного (не динамического) родителя.
	static FString TexName(UMaterialInterface* MI, bool bNative)
	{
		if (bNative)
		{
			while (UMaterialInstanceDynamic* D = Cast<UMaterialInstanceDynamic>(MI)) { MI = D->Parent; }
		}
		UTexture* T = nullptr;
		if (!MI || !MI->GetTextureParameterValue(FName(AsdaMat::kParamTex), T) || !T) { return FString(); }
		return T->GetName();
	}

	// Обойти все слоты мира с РОДНОЙ UNICON/UNICON03 и сверить текущую текстуру с фракцией. Заодно — рамки флагов для камеры.
	static void CheckWorld(FRun& R, UWorld* W, int32 Faction, const FString& Tag, FBox& OutAll, FBoxSphereBounds& OutBiggest)
	{
		int32 N[2] = { 0, 0 }, Good[2] = { 0, 0 };
		FString Bad;
		OutAll = FBox(ForceInit);
		OutBiggest = FBoxSphereBounds(ForceInit);
		for (TActorIterator<AActor> It(W); It; ++It)
		{
			TArray<UMeshComponent*> Meshes;
			It->GetComponents<UMeshComponent>(Meshes);
			for (UMeshComponent* MC : Meshes)
			{
				for (int32 i = 0; MC && i < MC->GetNumMaterials(); ++i)
				{
					UMaterialInterface* MI = MC->GetMaterial(i);
					const FString Native = TexName(MI, true).ToUpper();
					const int32 K = Native == TEXT("UNICON") ? 0 : Native == TEXT("UNICON03") ? 1 : -1;
					if (K < 0) { continue; }
					++N[K];
					const FString Cur = TexName(MI, false);
					const bool b = Cur.Equals(K == 0 ? kFlag[Faction] : kFloor[Faction]);
					if (b) { ++Good[K]; }
					else if (Bad.Len() < 400) { Bad += FString::Printf(TEXT(" %s/%s=%s"), *It->GetName(), *Native, *Cur); }
					if (K == 0)
					{
						const FBoxSphereBounds Bn = MC->Bounds;
						OutAll += Bn.GetBox();
						if (Bn.SphereRadius > OutBiggest.SphereRadius) { OutBiggest = Bn; }
					}
				}
			}
		}
		Expect(R, N[0] > 0 && Good[0] == N[0], FString::Printf(TEXT("%s: флаги UNICON → %s: %d из %d"), *Tag, kFlag[Faction], Good[0], N[0]));
		Expect(R, N[1] > 0 && Good[1] == N[1], FString::Printf(TEXT("%s: площадки UNICON03 → %s: %d из %d"), *Tag, kFloor[Faction], Good[1], N[1]));
		if (!Bad.IsEmpty()) { UE_LOG(LogAsdaCityOwnerTest, Warning, TEXT("ВЛАДЕЛЕЦ-ГОРОДА %s: не подменены:%s"), *Tag, *Bad); }
	}

	// Поставить свою камеру: bWide — над всеми флагами города, иначе — у самого крупного флага.
	static void AimCamera(FRun& R, UWorld* W, const FBox& All, const FBoxSphereBounds& Biggest, bool bWide)
	{
		if (!W || !All.IsValid) { return; }
		ACameraActor* Cam = R.Cam.Get();
		if (!Cam || Cam->GetWorld() != W)
		{
			FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Cam = W->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, SP);
			R.Cam = Cam;
		}
		if (!Cam) { return; }
		FVector Target; float Dist;
		if (bWide) { Target = All.GetCenter(); Dist = FMath::Max(All.GetExtent().Size() * 1.6f, 2000.f); }
		else       { Target = Biggest.Origin; Dist = FMath::Max(Biggest.SphereRadius * 4.0f, 600.f); }
		const FVector Dir = FVector(-1.f, -0.6f, bWide ? 1.1f : 0.35f).GetSafeNormal();
		const FVector Loc = Target + Dir * Dist;
		Cam->SetActorLocationAndRotation(Loc, (Target - Loc).Rotation());
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0)) { PC->SetViewTarget(Cam); }
	}

	// Кадр в файл (без интерфейса — видны только флаги).
	static void Shot(FRun& R, const TCHAR* Kind)
	{
		const FString File = FPaths::Combine(R.ShotDir, FString::Printf(TEXT("%s_%s.png"), *R.ShotBase, Kind));
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaCityOwnerTest, Log, TEXT("ВЛАДЕЛЕЦ-ГОРОДА кадр: %s"), *File);
	}

	// Начать переход на город R.City: номер карты сразу новый, 6743 «город за Светом» приходит ДО готовности мира.
	static void StartCity(FRun& R, UWorld* W)
	{
		UGameInstance* GI = R.GI.Get();
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
		const FCity& C = kCities[R.City];
		R.OldWorld = W;
		UGameplayStatics::OpenLevel(W, FName(C.Level), true, TEXT("game=/Script/AsdaReborn.AsdaWorldGameMode"));
		if (LM) { LM->CurrentMapId = C.MapId; }   // как `StartTargetLoad`: номер карты меняется раньше мира
		if (Net) { Feed(Net, C.Slot, 0); }        // ответ сервера на вход приходит, пока мир грузится
		R.Phase = EPhase::WaitWorld;
		R.PhaseStart = FPlatformTime::Seconds();
		R.SubStep = 0;
		R.LiveStep = 0;
	}

	// Тик прогона.
	static bool OnTick(float)
	{
		if (!GRun.IsValid()) { return false; }
		FRun& R = *GRun;
		UGameInstance* GI = R.GI.Get();
		UWorld* W = GI ? GI->GetWorld() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		const double Now = FPlatformTime::Seconds();
		const double Age = Now - R.PhaseStart;
		const FCity& C = kCities[FMath::Clamp(R.City, 0, 2)];
		auto Finish = [&R, W]()
		{
			if (ACameraActor* Cam = R.Cam.Get()) { Cam->Destroy(); }
			AsdaSelfTestKit::Finish(W, kCmd, R.bQuit, LogAsdaCityOwnerTest, TEXT("ВЛАДЕЛЕЦ-ГОРОДА"), R.Ok, R.Fail);
			GRun.Reset();
		};
		if (!Net || !WS) { Expect(R, false, TEXT("нет подсистем сети/мира")); Finish(); return false; }

		switch (R.Phase)
		{
		case EPhase::WaitWorld:
		{
			const bool bNew = W && W != R.OldWorld.Get() && W->GetMapName().Contains(C.Tag) && W->HasBegunPlay();
			if (!bNew)
			{
				if (Age < 120.0) { return true; }
				Expect(R, false, FString::Printf(TEXT("%s: за 120 с мир не поднялся"), C.Tag));
				Finish();
				return false;
			}
			if (Age < 4.0) { return true; }   // текстуры и стриминг
			R.Phase = EPhase::Enter;
			R.PhaseStart = Now;
			R.SubStep = 0;
			return true;
		}
		case EPhase::Enter:
		case EPhase::Live:
		{
			FBox All; FBoxSphereBounds Big;
			if (R.SubStep == 0)
			{
				FString Tag;
				if (R.Phase == EPhase::Enter)
				{
					// Тот же вызов, что делает `RevealWorld` при снятии загрузочного экрана (вход по 4003).
					WS->ApplyCityWarTextures();
					R.ExpectFaction = 0;
					Tag = FString::Printf(TEXT("%s вход после войны (6743 до готовности мира), Свет"), C.Tag);
					R.ShotBase = FString::Printf(TEXT("%s_%s_1_enter_%s"), *R.Lang, C.Tag, kFacTag[0]);
				}
				else
				{
					const int32 Owner = kLiveOwners[R.LiveStep];
					// Строка чата 111920 по городу (имя города — нотис 603/600/707): до и после приёма.
					const UAsdaChatSubsystem* Chat = GI->GetSubsystem<UAsdaChatSubsystem>();
					static const int32 kCityLoc[3] = { 603, 600, 707 };
					const FString CityName = AsdaNotice::Loc(GI, kCityLoc[C.Slot]).TrimStartAndEnd();
					const int32 Before = Chat ? Chat->DebugCountSystemLines(CityName) : -1;
					Feed(Net, C.Slot, Owner);   // смена на глазах: подписчик OnCityOwnerInfo перекрашивает сам
					R.ExpectFaction = Owner < 0 ? 3 : Owner;
					Tag = FString::Printf(TEXT("%s смена на глазах → %s"), C.Tag, kFlag[R.ExpectFaction]);
					R.ShotBase = FString::Printf(TEXT("%s_%s_%d_live_%s"), *R.Lang, C.Tag, 2 + R.LiveStep, kFacTag[R.ExpectFaction]);
					const int32 After = Chat ? Chat->DebugCountSystemLines(CityName) : -1;
					const int32 Want = (R.ExpectFaction == 3) ? 0 : 1;
					Expect(R, Chat && After - Before == Want,
						FString::Printf(TEXT("%s: строк чата 111920 про «%s» +%d (ждали +%d)"), *Tag, *CityName, After - Before, Want));
				}
				Expect(R, Net->GetCityOwnerFaction(C.Slot) == R.ExpectFaction,
					FString::Printf(TEXT("%s: принятый хозяин слота %d = %d (ждали %d)"), *Tag, C.Slot, Net->GetCityOwnerFaction(C.Slot), R.ExpectFaction));
				Expect(R, WS->GetCityWarSlotForCurrentMap() == C.Slot,
					FString::Printf(TEXT("%s: слот карты %d = %d"), *Tag, C.MapId, WS->GetCityWarSlotForCurrentMap()));
				CheckWorld(R, W, R.ExpectFaction, Tag, All, Big);
				AimCamera(R, W, All, Big, /*bWide*/ true);
				R.SubStep = 1; R.PhaseStart = Now;
				return true;
			}
			if (R.SubStep == 1) { if (Age < 1.5) { return true; } Shot(R, TEXT("wide")); R.SubStep = 2; R.PhaseStart = Now; return true; }
			if (R.SubStep == 2)
			{
				if (Age < 0.8) { return true; }
				CheckWorld(R, W, R.ExpectFaction, TEXT("(повтор перед близким кадром)"), All, Big);
				AimCamera(R, W, All, Big, /*bWide*/ false);
				R.SubStep = 3; R.PhaseStart = Now;
				return true;
			}
			if (R.SubStep == 3) { if (Age < 1.5) { return true; } Shot(R, TEXT("close")); R.SubStep = 4; R.PhaseStart = Now; return true; }
			if (Age < 0.8) { return true; }
			// Дальше: следующий шаг «на глазах», следующий город или конец.
			if (R.Phase == EPhase::Enter) { R.Phase = EPhase::Live; R.LiveStep = 0; }
			else if (++R.LiveStep >= UE_ARRAY_COUNT(kLiveOwners))
			{
				if (++R.City >= UE_ARRAY_COUNT(kCities)) { Finish(); return false; }
				StartCity(R, W);
				return true;
			}
			R.SubStep = 0; R.PhaseStart = Now;
			return true;
		}
		default: break;
		}
		Finish();
		return false;
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
		if (!Net || !LM) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет подсистемы сети или загрузки")); return; }
		if (GRun.IsValid()) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("самотест уже идёт")); return; }
		GRun = MakeShared<FRun>();
		FRun& R = *GRun;
		R.GI = GI; R.bQuit = Args.bQuit;
		R.Lang = FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en");
		R.ShotDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/batch17/city_war")));
		R.City = 0;
		StartCity(R, W);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&OnTick), 0.1f);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestCityWarOwner(
	TEXT("asda.SelfTest.CityWarOwner"),
	TEXT("Самотест: владелец города из 6743 на пути игрока — для Силариса, Альпии и Пламении вход на карту после войны "
	     "(6743 до готовности мира) и смена хозяина на глазах (Тьма → Хаос → ничей → Свет); сверка каждого флага/площадки "
	     "и кадры в Reports/batch17/city_war. Только офлайн. [quit]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCityWarOwnerSelfTest::Run(A, W);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
