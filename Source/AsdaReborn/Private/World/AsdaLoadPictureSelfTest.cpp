// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «КАКАЯ КАРТИНКА ЗАГРУЗКИ У КАКОЙ КАРТЫ» (10.11.2026, баг тестера «у Ruined City of Inferion нет экрана загрузки»):
//   `asda.Load.PictureSelfTest [maps=0,1,… | maps=0+1+… в -ExecCmds] [noshots] [quit]` (по умолчанию все карты 0–23).
//   Путь игрока (закон diagnostic-must-use-real-path): смена карты — `UAsdaLoadManagerSubsystem::BeginMapLoad` (тот же вызов, что
//   4055/5055/6201/6013/портал) → проявление картинки → L_Loading → целевая карта (офлайн экран снимается сам). У карты без UE-уровня
//   (BeginMapLoad её не грузит) — `ShowLoadingScreen`, тот же выбор `PickLoadingTexture`, затем `HideLoadingScreen`.
//   Картинку берём `GetPictureTexPath()` после того, как экран висит ≥ 1.7 с (проявление 1.5 с закончено), и снимаем кадр с UI:
//   `Reports/loading_picture/shots/<язык>_map<NN>_<имя картинки>.png`.
//   Ожидания (эталон `CMap_LoadLoadingScreen@0x975650`, `RE/etalon/world_map.c:196-218`: картинка из папки карты, пусто →
//   `map\load\start_logo.TEX`): у карты есть своя картинка — путь в `/Textures/Maps/<папка карты>/load/`; 18 Inferion —
//   `limg_Inferion00`, 19 battlefield — `limg_battlefield00` (донорские сцены в рамке эталона, 10.11.2026); 20 eventfield,
//   21 fieldofhonor, 22 quizstage — `Start_logo` (своих картинок нет ни в эталоне, ни в порте).
//   Итог: «КАРТИНКА ЗАГРУЗКИ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaWorldKind.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"   // FPackageName::ObjectPathToObjectName (ревизия 10.11)
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaLoadPicTest, Log, All);

namespace AsdaLoadPicTest
{
	static const TCHAR* kCmd = TEXT("asda.Load.PictureSelfTest");   // имя команды
	static constexpr double kShowSec = 1.7;    // экран висит столько до снимка (проявление 1.5 с)
	static constexpr double kArriveSec = 120.0; // потолок ожидания целевой карты
	using EFade = UAsdaLoadManagerSubsystem::EAsdaScreenFade;

	// Контекст прогона (живёт между шагами тикера, переживает смену мира).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                     // счёт
		bool bQuit = false;                         // выйти после итога
		bool bShots = true;                         // снимать кадры
		TArray<int32> Maps;                         // карты прогона
		int32 Idx = 0;                              // текущая карта (индекс в Maps)
		int32 Stage = 0;                            // 0 старт карты, 1 ждём экран, 2 ждём прибытия, 3 пауза
		bool bDirect = false;                       // у карты нет уровня — ShowLoadingScreen напрямую
		double T0 = 0.0;                            // начало этапа
		TWeakObjectPtr<UGameInstance> GI;           // игра
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaLoadPicTest, Log, TEXT("КАРТИНКА ЗАГРУЗКИ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaLoadPicTest, Error, TEXT("КАРТИНКА ЗАГРУЗКИ FAIL %s"), *What); }
		}
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Ожидаемая картинка карты: пустая строка — «своя из папки карты», иначе — точное имя ассета.
	static FString ExpectedName(int32 MapId)
	{
		switch (MapId)
		{
		case 18: return TEXT("limg_Inferion00");
		case 19: return TEXT("limg_battlefield00");
		case 20: case 21: case 22: return TEXT("Start_logo");
		default: return FString();
		}
	}

	static void Finish(const FCtxRef& C)
	{
		UGameInstance* GI = C->GI.Get();
		AsdaSelfTestKit::Finish(GI ? GI->GetWorld() : nullptr, kCmd, C->bQuit, LogAsdaLoadPicTest, TEXT("КАРТИНКА ЗАГРУЗКИ"), C->Ok, C->Fail);
	}

	// Проверка выбранной картинки и кадр.
	static void Judge(const FCtxRef& C, UAsdaLoadManagerSubsystem* LM, int32 MapId)
	{
		const FString Path = LM->GetPictureTexPath();
		const FString Name = FPackageName::ObjectPathToObjectName(Path);
		const FString Want = ExpectedName(MapId);
		// папка карты в Content: имя из реестра, пробел → «_» (как AsdaMapRegistry::ToAssetName), регистр не важен
		const FString Dir = LM->GetMapDirName(MapId).Replace(TEXT(" "), TEXT("_"));
		const bool bOwnFolder = Path.Contains(FString::Printf(TEXT("/Textures/Maps/%s/load/"), *Dir), ESearchCase::IgnoreCase);
		const bool bOk = Want.IsEmpty() ? (bOwnFolder && !Name.Equals(TEXT("Start_logo"), ESearchCase::IgnoreCase))
		                                : Name.Equals(Want, ESearchCase::IgnoreCase);
		C->Expect(bOk, FString::Printf(TEXT("карта %d ('%s'): картинка %s (ждали %s)%s"), MapId, *LM->GetMapDirName(MapId),
			Path.IsEmpty() ? TEXT("<нет>") : *Path, Want.IsEmpty() ? *FString::Printf(TEXT("свою из Maps/%s/load"), *Dir) : *Want,
			C->bDirect ? TEXT(" [уровня нет — ShowLoadingScreen]") : TEXT("")));
		if (!C->bShots) { return; }
		const FString OutDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/loading_picture/shots")));
		IFileManager::Get().MakeDirectory(*OutDir, true);
		const FString File = FString::Printf(TEXT("%s_map%02d_%s.png"), FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en"), MapId,
			Name.IsEmpty() ? TEXT("none") : *Name);
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutDir, File), /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
	}

	// Шаг тикера: true — тикать дальше.
	static bool Step(const FCtxRef& C)
	{
		UGameInstance* GI = C->GI.Get();
		if (!GI) { return false; }
		UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>();
		if (!LM) { return false; }
		UWorld* W = GI->GetWorld();
		const double Now = FPlatformTime::Seconds();
		const double Dt = Now - C->T0;
		if (C->Idx >= C->Maps.Num()) { Finish(C); return false; }
		const int32 MapId = C->Maps[C->Idx];
		switch (C->Stage)
		{
		case 0:   // старт перехода на карту
		{
			if (LM->IsLoadingScreenVisible() || (W && AsdaWorldKind::IsLoadingMap(W))) { return true; }   // прошлый переход ещё не снят
			const FAsdaMapRow* Row = LM->GetMapRow(MapId);
			C->bDirect = !Row || Row->Level.IsNull();
			UE_LOG(LogAsdaLoadPicTest, Log, TEXT("КАРТИНКА ЗАГРУЗКИ карта %d: %s"), MapId, C->bDirect ? TEXT("ShowLoadingScreen (уровня нет)") : TEXT("BeginMapLoad"));
			if (C->bDirect) { LM->ShowLoadingScreen(MapId); } else { LM->BeginMapLoad(MapId); }
			C->Stage = 1; C->T0 = Now;
			return true;
		}
		case 1:   // экран висит ≥ kShowSec и картинка выбрана → проверка и кадр
		{
			if (Dt < kShowSec) { return true; }
			if (!LM->IsLoadingScreenVisible() && Dt < 10.0) { return true; }
			// ★ 10.11.2026 (повторный прогон): проявление (режим 3) стартует не сразу после BeginMapLoad (≈0.8 с) — на 1.7 с кадр
			//   ловил середину перехода поверх застывшего кадра прошлой карты. Снимаем, когда картинка НЕПРОЗРАЧНА — Loading
			//   (ShowLoadingScreen ставит его и при прямом показе; None при живом экране = картинки нет, не «непрозрачна»).
			const EFade F = LM->GetScreenFadeState();
			if (F != EFade::Loading && Dt < 10.0) { return true; }
			// ★ ревизия-2: потолок вышел, а картинка так и не стала непрозрачной — кадр без гарантии, отдельный FAIL
			if (F != EFade::Loading)
			{
				C->Expect(false, FString::Printf(TEXT("карта %d: экран не стал непрозрачным за 10 с (состояние затемнения %d) — кадр без гарантии"),
					MapId, static_cast<int32>(F)));
			}
		}
			Judge(C, LM, MapId);
			C->Stage = 2; C->T0 = Now;
			return true;
		case 2:   // ждём конца перехода (кадр уже запрошен — даём ему кадр-два)
			if (Dt < 0.5) { return true; }
			if (C->bDirect) { LM->HideLoadingScreen(); C->Stage = 3; C->T0 = Now; return true; }
			if (W && !AsdaWorldKind::IsLoadingMap(W) && W->HasBegunPlay() && !LM->IsLoadingScreenVisible() && LM->CurrentMapId == MapId)
			{
				C->Stage = 3; C->T0 = Now; return true;
			}
			if (Dt > kArriveSec)
			{
				C->Expect(false, FString::Printf(TEXT("карта %d: переход не закончился за %.0f с"), MapId, kArriveSec));
				Finish(C); return false;
			}
			return true;
		case 3:   // пауза на осадку перед следующей картой
			if (Dt < 1.0) { return true; }
			++C->Idx; C->Stage = 0; C->T0 = Now;
			return true;
		default:
			return false;
		}
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(A);
		// 24 перехода по ≈ 15–40 с — окно второго барьера с запасом
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA, 3600.f)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI || !GI->GetSubsystem<UAsdaLoadManagerSubsystem>()) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистемы загрузки")); return; }
		FCtxRef C = MakeShared<FCtx>();
		C->GI = GI; C->bQuit = KA.bQuit; C->bShots = !KA.Has(TEXT("noshots"));
		for (const FString& P : KA.Pos)
		{
			if (P.StartsWith(TEXT("maps=")))
			{
				// ★ ревизия-2: `-ExecCmds` режет строку по запятым — там список через «+» (maps=18+19); кавычки снимаем
				TArray<FString> Parts; P.Mid(5).Replace(TEXT("\""), TEXT("")).Replace(TEXT("+"), TEXT(",")).ParseIntoArray(Parts, TEXT(","));
				for (const FString& S : Parts) { C->Maps.Add(FCString::Atoi(*S)); }
			}
		}
		if (C->Maps.Num() == 0) { for (int32 i = 0; i <= 23; ++i) { C->Maps.Add(i); } }
		if (C->bShots)   // ★ ревизия-2: старые кадры этого языка по картам прогона убираем — в папке не должно остаться чужих
		{
			const FString OutDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/loading_picture/shots")));
			for (const int32 M : C->Maps)
			{
				TArray<FString> Old;
				IFileManager::Get().FindFiles(Old, *FPaths::Combine(OutDir, FString::Printf(TEXT("%s_map%02d_*.png"), FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en"), M)), true, false);
				for (const FString& F : Old) { IFileManager::Get().Delete(*FPaths::Combine(OutDir, F)); }
			}
		}
		UE_LOG(LogAsdaLoadPicTest, Log, TEXT("КАРТИНКА ЗАГРУЗКИ старт: карт %d, язык %s"), C->Maps.Num(), FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en"));
		C->T0 = FPlatformTime::Seconds();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([C](float) { return Step(C); }), 0.f);
	}
}

// Консольная команда самотеста.
static FAutoConsoleCommandWithWorldAndArgs GAsdaLoadPictureSelfTest(
	TEXT("asda.Load.PictureSelfTest"),
	TEXT("Самотест картинки загрузки по картам (10.11.2026, Inferion): BeginMapLoad на каждую карту → какая картинка выбрана "
	     "(своя из Maps/<папка>/load или Start_logo) + кадр в Reports/loading_picture/shots. Офлайн. [maps=0,1,… (в -ExecCmds через +: maps=18+19)] [noshots] `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W) { AsdaLoadPicTest::Run(A, W); }));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
