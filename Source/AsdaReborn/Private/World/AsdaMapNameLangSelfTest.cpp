// Copyright (c) 2026 @sitxovski (Discord)
// ★★ САМОТЕСТ ЯЗЫКА НАЗВАНИЙ КАРТ (07.11.2026, баг владельца «название Mt. Flamion показывается по-русски вместо языка игры»).
// Запуск: `asda.SelfTest.MapNameLang [delay=N] [quit]` — игра на уровне Mountain_Flamion, офлайн, `-AsdaLang=ru|en`:
//   `asda_selftest_batch.py --lang en --map "/Game/Map/Mountain_Flamion/Mountain_Flamion?game=/Script/AsdaReborn.AsdaWorldGameMode" asda.SelfTest.MapNameLang`.
// ЧТО ПРОВЕРЯЕТ:
//   L1  каждая картинка названия карты из `DT_MapTable` (`NameTex`, `NameTexTown`, эталон `Map_SetNameTextureByID@0xDA3700`): ФАЙЛ, из
//       которого она реально загрузилась (`UPackage::GetLoadedPath`), и подмена движка (`FPackageLocalizationManager`). Имя пакета
//       не показатель: в `-game` файл L10N грузится под именем RU-пакета (Linker.cpp:694). EN — файл `/Game/L10N/en/…`, RU — `/Game/AsdaAssets/…`.
//       Исключения kNoEnSource: у картинки нет EN-версии в эталоне (инфо, не зачёт).
//   L2  три копии картинки Mt. Flamion (NPC/Image, Portrait, Texture) — тот же пакет языка сессии.
//   L3  настоящий показ баннера карты 12 (`UAsdaZoneSubsystem::ForceShowBanner`): кисть виджета `WBP_VILLAGE_NAME` держит картинку
//       пакета языка сессии, логический размер 512×64 (как у эталона; иначе баннер встанет не там).
//   L4  текстовые названия (GameText 804, 11815, 9621, 96212): EN — без кириллицы и с «Flamion», RU — кириллица.
//   L5  все карты 0…23: `Map_info` (мини-карта, большая карта, задания, родственная душа) и `GetMapDisplayName` («Кто?», друзья,
//       клан, загрузка) — в EN без кириллицы; в RU у карт с именем — кириллица (карта 23 в Map_info без имени, оригинал).
//   L6  настоящая подпись мини-карты `MINIMAP_NAME` на карте 12: EN «Mt. Flamion», RU «Гора Пламень».
// Строки — «САМОТЕСТ-НАЗВАНИЕ-КАРТЫ», итог — «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestRunGate.h"   // FAsdaSelfTestRunGate — гейт прогона самотеста   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "Resources/AsdaAssetPaths.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaUiHelpers.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaMapRegistry.h"
#include "World/AsdaZoneSubsystem.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Internationalization/PackageLocalizationManager.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMapNameLangTest, Log, All);

namespace AsdaMapNameLangTest
{
	static const TCHAR* const kCmd = TEXT("asda.SelfTest.MapNameLang");     // имя консольной команды
	static const TCHAR* const kPrefix = TEXT("САМОТЕСТ-НАЗВАНИЕ-КАРТЫ");     // префикс строк журнала
	static constexpr int32 kMapFlamion = 12;                                 // Mt. Flamion
	static const TCHAR* const kEnRoot = TEXT("/Game/L10N/en/");              // корень EN-копий пакетов
	static const TCHAR* const kRuRoot = TEXT("/Game/AsdaAssets/");           // корень RU-пакетов
	// Картинки без EN-версии в эталоне (`Data/NPC/Image/Texture.btz` EN-клиента файла не содержит): событийные карты 20–22.
	static const TCHAR* const kNoEnSource[] = { TEXT("name_eventfield"), TEXT("name_fieldofhonor"), TEXT("name_quizstage") };

	// Счётчики прогона.
	struct FState
	{
		int32 Ok = 0, Fail = 0;   // проверки
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaMapNameLangTest, Display, TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaMapNameLangTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
	};

	// Текстура названия из папки Root (тот же путь загрузки, что `UAsdaZoneSubsystem::LoadNameTexture`).
	static UTexture2D* LoadFrom(const TCHAR* Root, const FString& Name)
	{
		const FString Asset = AsdaMapRegistry::ToAssetName(Name);
		return LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), Root, *Asset, *Asset), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	// Файл, из которого реально загружена текстура, и подмена пакета движком в одну строку; true — файл языка сессии.
	static bool SourceOk(const UTexture2D* T, FString& Out)
	{
		if (!T) { Out = TEXT("—"); return false; }
		const FString File = T->GetPackage()->GetLoadedPath().GetPackageNameOrFallback();
		const FName Loc = FPackageLocalizationManager::Get().FindLocalizedPackageName(T->GetPackage()->GetFName());
		Out = FString::Printf(TEXT("файл %s, подмена %s"), *File, Loc.IsNone() ? TEXT("нет") : *Loc.ToString());
		return FAsdaLang::IsRu() ? (File.StartsWith(kRuRoot) && Loc.IsNone()) : (File.StartsWith(kEnRoot) && !Loc.IsNone());
	}

	// В строке есть кириллица.
	using AsdaSelfTestInput::HasCyrillic;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.

	// L1: картинка Name карты MapId.
	static void CheckName(FState& S, int32 MapId, const FString& Name)
	{
		const FString Base = FPaths::GetBaseFilename(AsdaMapRegistry::ToAssetName(Name));
		UTexture2D* T = LoadFrom(AsdaAssetPaths::kNpcImageRoot, FPaths::GetBaseFilename(Name));
		if (!T) { T = LoadFrom(AsdaAssetPaths::kNpcPortraitRoot, FPaths::GetBaseFilename(Name)); }
		FString Pkg;
		const bool bSrc = SourceOk(T, Pkg);
		bool bNoEn = false;
		for (const TCHAR* N : kNoEnSource) { bNoEn |= Base.Equals(N, ESearchCase::IgnoreCase); }
		if (bNoEn && !FAsdaLang::IsRu())
		{
			UE_LOG(LogAsdaMapNameLangTest, Display, TEXT("%s инфо карта %d «%s»: EN-версии в эталоне нет, пакет %s"), kPrefix, MapId, *Base, *Pkg);
			return;
		}
		S.Expect(bSrc, FString::Printf(TEXT("L1 карта %d «%s»: %s (язык %s)"), MapId, *Base, *Pkg, FAsdaLang::Code(FAsdaLang::Current())));
	}

	static void Run(const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (!W) { AsdaSelfTestKit::Refused(nullptr, kCmd, A.bQuit, TEXT("нет мира")); return; }
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, A)) { return; }
		TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(kCmd, A.bQuit);
		TSharedRef<FState> S = MakeShared<FState>();
		TWeakObjectPtr<UWorld> WW(W);
		const bool bQuit = A.bQuit;
		FTimerHandle H0;
		W->GetTimerManager().SetTimer(H0, FTimerDelegate::CreateLambda([WW, Gate]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd) || !GEngine) { return; }
			GEngine->Exec(Wd, *FString::Printf(TEXT("Asda.Load.CurrentMap %d"), kMapFlamion));
			GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 480 76"));   // свой павн — мини-карта обновляет подпись только с ним
		}), FMath::Max(0.01f, A.Delay), false);

		FTimerHandle H1;
		W->GetTimerManager().SetTimer(H1, FTimerDelegate::CreateLambda([WW, Gate, S, bQuit]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd)) { return; }
			UGameInstance* GI = Wd->GetGameInstance();
			UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
			UAsdaZoneSubsystem* Zone = GI ? GI->GetSubsystem<UAsdaZoneSubsystem>() : nullptr;
			if (!LM || !Zone) { AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("нет подсистем карты")); return; }

			// ── L1: все картинки названий ──
			for (int32 Id = 0; Id <= 23; ++Id)
			{
				const FAsdaMapRow* Row = LM->GetMapRow(Id);
				if (!Row) { continue; }
				if (!Row->NameTex.IsEmpty()) { CheckName(*S, Id, Row->NameTex); }
				if (!Row->NameTexTown.IsEmpty()) { CheckName(*S, Id, Row->NameTexTown); }
			}

			// ── L2: три копии картинки Mt. Flamion ──
			for (const TCHAR* Root : { TEXT("/Game/AsdaAssets/Textures/NPC/Image"), TEXT("/Game/AsdaAssets/Textures/NPC/Portrait"), TEXT("/Game/AsdaAssets/Textures/NPC/Texture") })
			{
				UTexture2D* T = LoadFrom(Root, TEXT("name_mountain_flamion"));
				FString Pkg;
				const bool bSrc = SourceOk(T, Pkg);
				S->Expect(bSrc, FString::Printf(TEXT("L2 %s: %s"), Root, *Pkg));
			}

			// ── L3: настоящий показ баннера ──
			Zone->ForceShowBanner(false);
			UTexture2D* Shown = nullptr;
			for (TObjectIterator<UImage> It; It; ++It)
			{
				if (It->GetWorld() != Wd) { continue; }
				UTexture2D* T = Cast<UTexture2D>(It->GetBrush().GetResourceObject());
				if (T && T->GetName().Equals(TEXT("name_mountain_flamion"), ESearchCase::IgnoreCase)) { Shown = T; break; }
			}
			FString ShownPkg;
			const bool bShownOk = SourceOk(Shown, ShownPkg);
			S->Expect(bShownOk, FString::Printf(TEXT("L3 баннер карты 12: кисть держит текстуру, %s"), *ShownPkg));
			const FIntPoint Log = Shown ? AsdaUi::LogicalAtlasSize(Shown) : FIntPoint::ZeroValue;
			S->Expect(Log == FIntPoint(512, 64), FString::Printf(TEXT("L3 логический размер баннера %dx%d (эталон 512x64), текстура %dx%d"),
				Log.X, Log.Y, Shown ? Shown->GetSizeX() : 0, Shown ? Shown->GetSizeY() : 0));

			// ── L4: текстовые названия ──
			for (const int32 Id : { 804, 11815, 9621, 96212 })
			{
				const FString Txt = AsdaNotice::Loc(GI, Id);
				const bool bOk = FAsdaLang::IsRu() ? HasCyrillic(Txt) : (!HasCyrillic(Txt) && Txt.Contains(TEXT("Flamion")));
				S->Expect(bOk, FString::Printf(TEXT("L4 строка %d: «%s»"), Id, *Txt));
			}
			// ── L5: текстовые названия всех карт ──
			UAsdaTextSubsystem* TS = GI->GetSubsystem<UAsdaTextSubsystem>();
			for (int32 Id = 0; Id <= 23; ++Id)
			{
				if (!LM->GetMapRow(Id)) { continue; }
				const FString Info = TS ? TS->GetText(TEXT("Map_info"), Id, TEXT("name")) : FString();
				const FString Disp = LM->GetMapDisplayName(Id).ToString();
				const bool bOk = FAsdaLang::IsRu() ? (Id == 23 || (HasCyrillic(Info) && HasCyrillic(Disp)))
					: (!HasCyrillic(Info) && !HasCyrillic(Disp) && !Disp.IsEmpty());
				S->Expect(bOk, FString::Printf(TEXT("L5 карта %d: Map_info «%s», имя карты «%s»"), Id, *Info, *Disp));
			}

			// ── L6: настоящая подпись мини-карты ──
			FString Mini;
			for (TObjectIterator<UTextBlock> It; It; ++It)
			{
				if (It->GetWorld() == Wd && It->GetName() == TEXT("MINIMAP_NAME")) { Mini = It->GetText().ToString(); break; }
			}
			S->Expect(Mini == (FAsdaLang::IsRu() ? TEXT("Гора Пламень") : TEXT("Mt. Flamion")), FString::Printf(TEXT("L6 подпись мини-карты: «%s»"), *Mini));
			AsdaSelfTestKit::Finish(Wd, kCmd, bQuit, LogAsdaMapNameLangTest, kPrefix, S->Ok, S->Fail, TEXT(""));
		}), A.Delay + 3.f, false);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaMapNameLangSelfTest(
	TEXT("asda.SelfTest.MapNameLang"),
	TEXT("Самотест языка названий карт: картинки DT_MapTable из пакета языка сессии, баннер Mt. Flamion, строки GameText. "
	     "`delay=N` `quit`; игра — на уровне Mountain_Flamion, `-AsdaLang=ru|en`. Только офлайн."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaMapNameLangTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
