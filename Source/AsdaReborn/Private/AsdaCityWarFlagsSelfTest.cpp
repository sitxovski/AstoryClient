// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ФЛАГИ, ПЛОЩАДКИ И ЭМБЛЕМЫ ГОРОДОВ ПО 6743» (28.10.2026): `asda.SelfTest.CityWarFlags [quit]`.
//   Эталон: `uniconWarApplyTex0/1` + `UI_LoadClanIcon` подменяют при отрисовке unicon.TEX / unicon03.TEX / unicon_clan.TEX
//   на карте 3/0/7 текстурой по фракции-хозяину города из 6743. Порт: `UAsdaWorldSubsystem::ApplyCityWarTextures`.
//   Реальный путь (закон diagnostic-must-use-real-path), без сети — 6743 подаётся штатным разбором (`SimulateCityOwnerInfo`):
//     1) Силарис (карта 0), хозяин Тьма + флаг клана: КАЖДЫЙ слот с родной UNICON/UNICON03 несёт uniconDarkFlag/Floor,
//        каждый UNICON_CLAN — испечённую эмблему;
//     2) смена хозяина на Хаос — перекраска тех же слотов;
//     3) ГОНКА портала: 6743 приходит, когда номер карты уже новый (7), а мир ещё старый; затем мир Пламении готов —
//        вызов из RevealWorld обязан найти флаги НОВОЙ карты (раньше скан защёлкивался на умирающем мире).
//   Запускать на /Game/Map/silalis/silalis, только офлайн (-AsdaNoNet). Итог: «ФЛАГИ-ГОРОДА ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "Resources/AsdaMaterialNames.h"
#include "Test/AsdaSelfTestKit.h"
#include "Components/MeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCityWarTest, Log, All);

namespace AsdaCityWarFlagsSelfTest
{
	// Имя команды (строка итога, отказ, возврат ключей).
	static const TCHAR* kCmd = TEXT("asda.SelfTest.CityWarFlags");
	// Ожидаемые имена подменных текстур по коду фракции (порядок `CMap_LoadUniconWarTextures@0x9C88E0`).
	static const TCHAR* kFlag[4]  = { TEXT("uniconLightFlag"), TEXT("uniconDarkFlag"), TEXT("uniconChaosFlag"), TEXT("uniconNoConqureFlag") };
	static const TCHAR* kFloor[4] = { TEXT("uniconLightFloor"), TEXT("uniconDarkFloor"), TEXT("uniconChaosFloor"), TEXT("uniconNotConqureFloor") };

	// Состояние прогона (переживает смену уровня — живёт на тикере ядра).
	struct FRun
	{
		TWeakObjectPtr<UGameInstance> GI;   // экземпляр игры (переживает переход)
		bool bQuit = false;                 // выйти после итога
		int32 Ok = 0, Fail = 0;             // счёт проверок
		double PhaseStart = 0.0;            // начало ожидания нового мира
		TWeakObjectPtr<UWorld> OldWorld;    // мир Силариса до перехода
	};
	static TSharedPtr<FRun> GRun;

	// Записать результат проверки.
	static void Expect(FRun& R, bool b, const FString& What)
	{
		if (b) { ++R.Ok; UE_LOG(LogAsdaCityWarTest, Log, TEXT("ФЛАГИ-ГОРОДА ok   %s"), *What); }
		else { ++R.Fail; UE_LOG(LogAsdaCityWarTest, Error, TEXT("ФЛАГИ-ГОРОДА FAIL %s"), *What); }
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

	// Обойти все слоты мира с родной текстурой города и сверить текущую. Tag — префикс строк журнала.
	static void CheckWorld(FRun& R, UWorld* W, int32 Faction, const TCHAR* Tag)
	{
		int32 N[3] = { 0, 0, 0 }, Good[3] = { 0, 0, 0 };
		FString Bad;
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
					const int32 K = Native == TEXT("UNICON") ? 0 : Native == TEXT("UNICON03") ? 1 : Native == TEXT("UNICON_CLAN") ? 2 : -1;
					if (K < 0) { continue; }
					++N[K];
					const FString Cur = TexName(MI, false);
					const bool b = (K == 0) ? Cur.Equals(kFlag[Faction]) : (K == 1) ? Cur.Equals(kFloor[Faction])
						: (!Cur.IsEmpty() && !Cur.Equals(TEXT("UNICON_CLAN"), ESearchCase::IgnoreCase));
					if (b) { ++Good[K]; }
					else if (Bad.Len() < 400) { Bad += FString::Printf(TEXT(" %s/%s=%s"), *It->GetName(), *Native, *Cur); }
				}
			}
		}
		Expect(R, N[0] > 0 && Good[0] == N[0], FString::Printf(TEXT("%s: флаги UNICON → %s: %d из %d"), Tag, kFlag[Faction], Good[0], N[0]));
		Expect(R, N[1] > 0 && Good[1] == N[1], FString::Printf(TEXT("%s: площадки UNICON03 → %s: %d из %d"), Tag, kFloor[Faction], Good[1], N[1]));
		Expect(R, N[2] > 0 && Good[2] == N[2], FString::Printf(TEXT("%s: эмблемы UNICON_CLAN → испечённый флаг клана: %d из %d"), Tag, Good[2], N[2]));
		if (!Bad.IsEmpty()) { UE_LOG(LogAsdaCityWarTest, Warning, TEXT("ФЛАГИ-ГОРОДА %s: не подменены:%s"), Tag, *Bad); }
	}

	// Ожидание мира Пламении и проверка 3 (на тиках ядра — переживают смену уровня).
	static bool OnTick(float)
	{
		if (!GRun.IsValid()) { return false; }
		FRun& R = *GRun;
		UGameInstance* GI = R.GI.Get();
		UWorld* W = GI ? GI->GetWorld() : nullptr;
		const double Now = FPlatformTime::Seconds();
		const bool bNew = W && W != R.OldWorld.Get() && W->GetMapName().Contains(TEXT("flammio")) && W->HasBegunPlay();
		if (!bNew)
		{
			if (Now - R.PhaseStart < 90.0) { return true; }
			Expect(R, false, TEXT("3: за 90 с мир Пламении не поднялся"));
		}
		else
		{
			if (Now - R.PhaseStart < 2.0) { return true; }
			// Тот же вызов, что делает `RevealWorld` при снятии загрузочного экрана.
			if (UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>()) { WS->ApplyCityWarTextures(); }
			CheckWorld(R, W, 0, TEXT("3 (6743 во время перехода → Пламения, Свет)"));
		}
		AsdaSelfTestKit::Finish(W, kCmd, R.bQuit, LogAsdaCityWarTest, TEXT("ФЛАГИ-ГОРОДА"), R.Ok, R.Fail);
		GRun.Reset();
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
		if (!W->GetMapName().Contains(TEXT("silalis"))) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("запускать на /Game/Map/silalis/silalis")); return; }
		if (GRun.IsValid()) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("самотест уже идёт")); return; }
		GRun = MakeShared<FRun>();
		FRun& R = *GRun;
		R.GI = GI; R.bQuit = Args.bQuit; R.OldWorld = W;

		// 1) Силарис, хозяин Тьма, у клана-хозяина есть флаг.
		LM->CurrentMapId = 0;
		Net->SimulateCityOwnerInfo({ 3, 1, 3 }, { 10, 10, 10 });
		Net->SimulateCityClanFlag(1, 0, (int32)0xFFFFFFFF, (int32)0xFF3060C0, 0, (int32)0xFFFFFFFF);
		CheckWorld(R, W, 1, TEXT("1 (Силарис, Тьма)"));
		// 2) Смена хозяина.
		Net->SimulateCityOwnerInfo({ 3, 2, 3 }, { 10, 10, 10 });
		Net->SimulateCityClanFlag(1, 0, (int32)0xFFFFFFFF, (int32)0xFF3060C0, 0, (int32)0xFFFFFFFF);
		CheckWorld(R, W, 2, TEXT("2 (Силарис, Хаос)"));
		// 3) Гонка портала: переход запрошен, номер карты уже новый (как в `StartTargetLoad`), мир ещё старый — приходит 6743.
		UGameplayStatics::OpenLevel(W, FName(TEXT("/Game/Map/flammio/flammio")));
		LM->CurrentMapId = 7;
		Net->SimulateCityOwnerInfo({ 3, 2, 0 }, { 10, 10, 10 });
		Net->SimulateCityClanFlag(2, 0, (int32)0xFFFFFFFF, (int32)0xFF30C060, 0, (int32)0xFFFFFFFF);
		R.PhaseStart = FPlatformTime::Seconds();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&OnTick), 0.2f);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestCityWarFlags(
	TEXT("asda.SelfTest.CityWarFlags"),
	TEXT("Самотест: флаги/площадки/эмблемы городов подменяются по фракции-хозяину из 6743 (карты 0/3/7), в т. ч. когда 6743 "
	     "пришёл во время перехода между картами. Запускать на /Game/Map/silalis/silalis, только офлайн. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCityWarFlagsSelfTest::Run(A, W);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
