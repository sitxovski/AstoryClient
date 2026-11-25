// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ ЭКРАННОГО ЗАТЕМНЕНИЯ СМЕНЫ КАРТЫ (31.10.2026, баг 5): `asda.Load.FadeSelfTest [map=<id>] [noshots] [quit]`. [ИСПРАВЛЕНИЕ]
//   Жалоба игроков и владельца: загрузочный экран появляется и исчезает обрывом; после загрузки на секунду видно небо без земли.
//   Разбор и ключи — шапка затемнения в `AsdaLoadManagerSubsystem.cpp`.
//   Путь игрока (закон diagnostic-must-use-real-path): смена карты — `UAsdaLoadManagerSubsystem::BeginMapLoad` (тот же вызов,
//   что отложенная загрузка после 6004 / 4055 / портала), травел L_Loading → целевая карта; онлайн-ожидание 4003 эмулируется
//   крюком `bDebugHoldScreenAsOnline`, сам 4003 — `UAsdaWorldSubsystem::DebugSpawnSelfAt` (та же точка `OnSelfSpawn`);
//   своё 4056 — штатный приём `UAsdaNetSubsystem::DebugFeedPacket`. Шаги — по тикеру ядра (таймеры мира умирают с миром).
//   Кадры PNG по времени — `Reports/loading_fade/shots/<язык>_<ключи>_NN_<этап>_<мс>_a<альфа>.png` (UI в кадре).
//   Этапы:  1 мир с персонажем;  2 BeginMapLoad: режим 3 — картинка проявляется, альфа растёт, экран считается висящим;
//           3 целевая карта: экран пережил травел и висит до 4003 (ключ KeepScreenOnTravel 1);  4 эмуляция 4003: экран снят
//           логически, картинка держится до готовности (камера у персонажа), затем режим 4 — альфа монотонно падает до 0;
//           5 своё 4056 — режим 0: чёрный от 1 до 0;  6 (ревизия 31.10) переход и уход на экран входа во время ожидания 4003 —
//           на логине экрана загрузки нет;  7 вход с логина и обрыв (OnGameDisconnected, причина 255) во время ожидания 4003 —
//           экран снят сразу, попап обрыва открыт.  `pic=logo` — фон Start_logo на всех переходах (сверка кисти с M_LoadingBG).  Ключи 0 (`--pre "asda.Load.FadeSec 0" --pre "asda.Load.KeepScreenOnTravel 0"`) —
//           ожидания прежнего поведения: затемнений нет, экран снят травелом (доказательство причины «неба без земли»).
//   Итог: «ЗАТЕМНЕНИЕ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaWorldKind.h"
#include "World/AsdaWorldSubsystem.h"
#include "UI/AsdaDisconnectPopupSubsystem.h"
#include "Resources/AsdaMaterialNames.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaLoadFadeTest, Log, All);

namespace AsdaLoadFadeTest
{
	static const TCHAR* kCmd = TEXT("asda.Load.FadeSelfTest");   // имя команды
	static constexpr uint16 kMyUserNum = 321;   // свой userNum на прогон
	static constexpr int32 kTileX = 256;        // тайл спавна своего персонажа (как у самотеста окклюзии камеры)
	static constexpr int32 kTileY = 256;
	using EFade = UAsdaLoadManagerSubsystem::EAsdaScreenFade;

	// Контекст прогона (живёт между шагами тикера, переживает смену мира).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                     // счёт
		bool bQuit = false;                         // выйти после итога
		bool bShots = true;                         // снимать кадры
		bool bFadeOn = true;                        // asda.Load.FadeSec > 0
		bool bKeepOn = true;                        // asda.Load.KeepScreenOnTravel != 0
		int32 TargetMap = 3;                        // карта перехода
		TWeakObjectPtr<UGameInstance> GI;           // игра (общая для всех миров прогона)
		TUniquePtr<FAsdaNetSelfScope> NetScope;     // свой userNum — вернуть в конце
		double T0 = 0.0;                            // начало текущего этапа
		double LastShot = -1.0;                     // время последнего кадра
		int32 ShotNo = 0;                           // номер кадра
		int32 Stage = 0;                            // текущий этап
		float MaxAlphaIn = 0.f;                     // наибольшая альфа проявления
		int32 MidAlphaIn = 0;                       // замеров проявления с альфой строго между 0 и 1
		float PrevAlpha = 2.f;                      // прошлая альфа растворения/чёрного
		bool bMonotonic = true;                     // альфа растворения/чёрного не росла
		bool bSawWaitReady = false, bSawOut = false; // этапы растворения встречены
		bool bScreenGoneBefore4003 = false;         // экран пропадал с вьюпорта до 4003
		bool bBlackStartedFull = false;             // чёрный начат с альфы ≈ 1
		bool bLogoPic = false;                      // `pic=logo`: фон Start_logo на каждом переходе
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaLoadFadeTest, Log, TEXT("ЗАТЕМНЕНИЕ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaLoadFadeTest, Error, TEXT("ЗАТЕМНЕНИЕ FAIL %s"), *What); }
		}
	};
	using FCtxRef = TSharedRef<FCtx>;

	template <class T> static T* Sub(const FCtxRef& C) { UGameInstance* GI = C->GI.Get(); return GI ? GI->GetSubsystem<T>() : nullptr; }
	static int32 CVarI(const TCHAR* N) { const IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(N); return V ? V->GetInt() : 0; }
	static float CVarF(const TCHAR* N) { const IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(N); return V ? V->GetFloat() : 0.f; }
	static const TCHAR* StateName(EFade S)
	{
		switch (S)
		{
		case EFade::None: return TEXT("нет"); case EFade::PictureIn: return TEXT("проявление");
		case EFade::Loading: return TEXT("загрузка"); case EFade::WaitReady: return TEXT("ждём-мир");
		case EFade::PictureOut: return TEXT("растворение"); case EFade::BlackOut: return TEXT("чёрный");
		default: return TEXT("?");
		}
	}
	// Кадр с UI в PNG (абсолютный путь). Не чаще раза в MinGap с: запрос кадра одиночный.
	static void Shot(const FCtxRef& C, const TCHAR* Stage, double Now, double MinGap = 0.09)
	{
		UAsdaLoadManagerSubsystem* LM = Sub<UAsdaLoadManagerSubsystem>(C);
		const float A = LM ? LM->GetScreenFadeAlpha() : 0.f;
		const EFade S = LM ? LM->GetScreenFadeState() : EFade::None;
		UE_LOG(LogAsdaLoadFadeTest, Log, TEXT("ЗАТЕМНЕНИЕ кадр %02d [%s] t=%4.0f мс состояние=%s альфа=%.3f экран=%s во вьюпорте=%s"),
			C->ShotNo, Stage, (Now - C->T0) * 1000.0, StateName(S), A,
			(LM && LM->IsLoadingScreenVisible()) ? TEXT("висит") : TEXT("снят"), (LM && LM->IsLoadScreenOnViewport()) ? TEXT("да") : TEXT("нет"));
		if (!C->bShots || (C->LastShot >= 0.0 && Now - C->LastShot < MinGap)) { ++C->ShotNo; return; }
		const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/loading_fade/shots")));
		IFileManager::Get().MakeDirectory(*Dir, true);
		const FString Name = FString::Printf(TEXT("%s_%s_%02d_%s_%04d_a%03d.png"), FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en"),
			C->bFadeOn ? TEXT("fade") : TEXT("legacy"), C->ShotNo, Stage, (int32)((Now - C->T0) * 1000.0), (int32)(A * 100.f + 0.5f));
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, Name), /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		C->LastShot = Now;
		++C->ShotNo;
	}
	// Своё 4056 (22 б): uidx@0, тайлы @2/@4, HP i32@6, MP i16@10, опыт i64@12, состояние i16@20 (0 — живой).
	static TArray<uint8> Body4056()
	{
		TArray<uint8> B; B.SetNumZeroed(22);
		AsdaBytes::WrI16(&B[0], (int16)kMyUserNum); AsdaBytes::WrI16(&B[2], kTileX); AsdaBytes::WrI16(&B[4], kTileY);
		AsdaBytes::WrI32(&B[6], 100); AsdaBytes::WrI16(&B[10], 50);
		return B;
	}

	static void Finish(const FCtxRef& C)
	{
		if (UAsdaLoadManagerSubsystem* LM = Sub<UAsdaLoadManagerSubsystem>(C)) { LM->bDebugHoldScreenAsOnline = false; }
		C->NetScope.Reset();
		UGameInstance* GI = C->GI.Get();
		AsdaSelfTestKit::Finish(GI ? GI->GetWorld() : nullptr, kCmd, C->bQuit, LogAsdaLoadFadeTest, TEXT("ЗАТЕМНЕНИЕ"), C->Ok, C->Fail);
	}

	// Шаг тикера: true — тикать дальше.
	static bool Step(const FCtxRef& C)
	{
		UGameInstance* GI = C->GI.Get();
		if (!GI) { return false; }
		UWorld* W = GI->GetWorld();
		UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>();
		if (!LM) { return false; }
		const double Now = FPlatformTime::Seconds();
		const double Dt = Now - C->T0;
		switch (C->Stage)
		{
		case 0:   // мир с персонажем: 1.5 с на осадку (спавн, камера), кадр «до» — за 0.5 с до перехода (запрос кадра — на следующий кадр)
			if (Dt >= 1.0 && C->ShotNo == 0) { Shot(C, TEXT("world"), Now); }
			if (Dt < 1.5) { return true; }
			{
				const APlayerController* PC = GI->GetFirstLocalPlayerController();
				C->Expect(PC && PC->GetPawn(), TEXT("1: свой персонаж в мире (FakeSelfSpawn)"));
				LM->bDebugHoldScreenAsOnline = true;
				if (C->bLogoPic) { LM->SetNextLoadingTexture(AsdaLoadScreen::kStartLogoTex); }
				UE_LOG(LogAsdaLoadFadeTest, Log, TEXT("ЗАТЕМНЕНИЕ этап 2: BeginMapLoad(%d)"), C->TargetMap);
				LM->BeginMapLoad(C->TargetMap);
				C->Expect(LM->IsLoadingScreenVisible() == C->bFadeOn,
					C->bFadeOn ? TEXT("2: с началом перехода экран загрузки уже считается висящим (режим 3)")
					           : TEXT("2: ключ FadeSec 0 — до L_Loading экрана нет (как было)"));
				C->Stage = 1; C->T0 = Now; C->LastShot = -1.0;
			}
			return true;
		case 1:   // режим 3: кадры проявления, пока не встанет целевая карта
		{
			const EFade S = LM->GetScreenFadeState();
			const float A = LM->GetScreenFadeAlpha();
			if (S == EFade::PictureIn || S == EFade::Loading)
			{
				C->MaxAlphaIn = FMath::Max(C->MaxAlphaIn, A);
				if (S == EFade::PictureIn && A > 0.01f && A < 0.99f) { ++C->MidAlphaIn; }
			}
			if (Dt < 2.0) { Shot(C, TEXT("in"), Now, 0.2); }
			if (W && !AsdaWorldKind::IsLoadingMap(W) && W->HasBegunPlay() && Dt > 0.5)
			{
				if (C->bFadeOn)
				{
					C->Expect(C->MidAlphaIn >= 3 && C->MaxAlphaIn >= 0.99f,
						FString::Printf(TEXT("2: режим 3 — картинка проявилась плавно (промежуточных замеров %d, наибольшая альфа %.2f)"), C->MidAlphaIn, C->MaxAlphaIn));
				}
				C->Stage = 2; C->T0 = Now; C->LastShot = -1.0;
				UE_LOG(LogAsdaLoadFadeTest, Log, TEXT("ЗАТЕМНЕНИЕ этап 3: целевая карта %s — ждём «4003» 1.2 с"), *W->GetName());
			}
			if (Dt > 60.0) { C->Expect(false, TEXT("2: целевая карта не началась за 60 с")); Finish(C); return false; }
			return true;
		}
		case 2:   // целевая карта до «4003»: экран должен висеть (ключ 1) — ни кадра мира без персонажа
			if (!LM->IsLoadScreenOnViewport()) { C->bScreenGoneBefore4003 = true; }
			Shot(C, TEXT("pre4003"), Now, 0.25);
			if (Dt < 1.2) { return true; }
			if (C->bKeepOn)
			{
				C->Expect(LM->IsLoadingScreenVisible() && !C->bScreenGoneBefore4003,
					TEXT("3: экран загрузки пережил травел и висел на целевой карте до 4003 (нет кадров мира без персонажа)"));
			}
			else
			{
				C->Expect(C->bScreenGoneBefore4003,
					TEXT("3: ключ KeepScreenOnTravel 0 — экран снят травелом до 4003 (прежнее поведение: мир без персонажа = «небо без земли»)"));
			}
			if (UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>())
			{
				UE_LOG(LogAsdaLoadFadeTest, Log, TEXT("ЗАТЕМНЕНИЕ этап 4: эмуляция 4003 (DebugSpawnSelfAt %d,%d)"), kTileX, kTileY);
				WS->DebugSpawnSelfAt(kTileX, kTileY);
			}
			C->Stage = 3; C->T0 = Now; C->LastShot = -1.0; C->PrevAlpha = 2.f;
			return true;
		case 3:   // режим 4: растворение
		{
			const EFade S = LM->GetScreenFadeState();
			const float A = LM->GetScreenFadeAlpha();
			if (S == EFade::WaitReady) { C->bSawWaitReady = true; }
			if (S == EFade::PictureOut)
			{
				C->bSawOut = true;
				if (A > C->PrevAlpha + 1e-4f) { C->bMonotonic = false; }
				C->PrevAlpha = A;
			}
			Shot(C, TEXT("out"), Now, 0.1);
			// Потолок с запасом: кадры PNG подвешивают кадр, а шаг затемнения берёт не больше 0.1 с за кадр.
			const double Cap = CVarF(TEXT("asda.Load.FadeSec")) * 4.0 + CVarF(TEXT("asda.Load.RevealMaxSec")) + 3.0;
			if (S != EFade::None && Dt < Cap) { return true; }
			C->Expect(!LM->IsLoadingScreenVisible(), TEXT("4: после 4003 экран загрузки снят (IsLoadingScreenVisible = false)"));
			if (C->bFadeOn)
			{
				C->Expect(C->bSawWaitReady && C->bSawOut, TEXT("4: перед растворением ждали готовности мира, затем режим 4"));
				C->Expect(C->bMonotonic, TEXT("4: альфа растворения монотонно падала"));
				C->Expect(S == EFade::None && Dt >= CVarF(TEXT("asda.Load.FadeSec")) * 0.9,
					FString::Printf(TEXT("4: растворение завершено за %.2f с (не обрывом)"), Dt));
			}
			else
			{
				C->Expect(S == EFade::None && !C->bSawOut, TEXT("4: ключ FadeSec 0 — экран снят обрывом (как было)"));
			}
			{
				const APlayerController* PC = GI->GetFirstLocalPlayerController();
				const APawn* P = PC ? PC->GetPawn() : nullptr;
				C->Expect(P && PC->PlayerCameraManager && PC->PlayerCameraManager->GetViewTarget() == P,
					TEXT("4: камера смотрит на своего персонажа на целевой карте"));
			}
			// Своё 4056 — режим 0.
			if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C)) { Net->SetSelfUserNumForDebug(kMyUserNum); Net->DebugFeedPacket(4056, Body4056()); }
			C->bBlackStartedFull = LM->GetScreenFadeState() == EFade::BlackOut && LM->GetScreenFadeAlpha() > 0.99f;
			C->Stage = 4; C->T0 = Now; C->LastShot = -1.0; C->PrevAlpha = 2.f; C->bMonotonic = true;
			return true;
		}
		case 4:   // режим 0: чёрный рассеивается
		{
			const EFade S = LM->GetScreenFadeState();
			const float A = LM->GetScreenFadeAlpha();
			if (S == EFade::BlackOut) { if (A > C->PrevAlpha + 1e-4f) { C->bMonotonic = false; } C->PrevAlpha = A; }
			Shot(C, TEXT("black"), Now, 0.25);
			if (S != EFade::None && Dt < CVarF(TEXT("asda.Load.FadeSec")) * 4.0 + 2.0) { return true; }
			if (C->bFadeOn)
			{
				C->Expect(C->bBlackStartedFull, TEXT("5: своё 4056 — режим 0, чёрный начат с непрозрачного"));
				C->Expect(C->bMonotonic && S == EFade::None, FString::Printf(TEXT("5: чёрный рассеялся монотонно за %.2f с"), Dt));
			}
			else
			{
				C->Expect(!C->bBlackStartedFull && S == EFade::None, TEXT("5: ключ FadeSec 0 — чёрного нет (как было)"));
			}
			// Этап 6: новый переход, на целевой карте до «4003» — уход на экран входа (тот же OpenLevel, что ОК попапа обрыва).
			LM->bDebugHoldScreenAsOnline = true;
			if (C->bLogoPic) { LM->SetNextLoadingTexture(AsdaLoadScreen::kStartLogoTex); }
			UE_LOG(LogAsdaLoadFadeTest, Log, TEXT("ЗАТЕМНЕНИЕ этап 6: BeginMapLoad(%d), затем уход на экран входа до 4003"), C->TargetMap);
			LM->BeginMapLoad(C->TargetMap);
			C->Stage = 5; C->T0 = Now; C->LastShot = -1.0;
			return true;
		}
		case 5:   // ждём целевую карту (0.5 с), экран висит → OpenLevel Login
			if (W && !AsdaWorldKind::IsLoadingMap(W) && W->HasBegunPlay() && !AsdaWorldKind::IsLoginScene(W))
			{
				if (C->T0 > 0.0 && Dt > 3.0)
				{
					C->Expect(LM->IsLoadingScreenVisible() || !C->bKeepOn, TEXT("6: на целевой карте до 4003 экран загрузки висит"));
					UE_LOG(LogAsdaLoadFadeTest, Log, TEXT("ЗАТЕМНЕНИЕ этап 6: OpenLevel /Game/Map/Login при висящем экране"));
					UGameplayStatics::OpenLevel(W, FName(TEXT("/Game/Map/Login")));
					C->Stage = 6; C->T0 = Now; C->LastShot = -1.0;
				}
				return true;
			}
			if (Dt > 60.0) { C->Expect(false, TEXT("6: целевая карта не началась за 60 с")); Finish(C); return false; }
			return true;
		case 6:   // экран входа: экрана загрузки нет → вход с логина (штатный путь 6004) и обрыв до 4003
			if (!(W && AsdaWorldKind::IsLoginScene(W) && W->HasBegunPlay())) { if (Dt > 60.0) { C->Expect(false, TEXT("6: экран входа не открылся")); Finish(C); return false; } return true; }
			if (Dt < 1.5) { return true; }
			Shot(C, TEXT("login"), Now);
			C->Expect(!LM->IsLoadingScreenVisible() && !LM->IsLoadScreenOnViewport() && LM->GetScreenFadeState() == EFade::None,
				TEXT("6: уход на экран входа во время ожидания 4003 — экрана загрузки нет (не залип над логином)"));
			LM->bDebugHoldScreenAsOnline = true;
			if (C->bLogoPic) { LM->SetNextLoadingTexture(AsdaLoadScreen::kStartLogoTex); }
			UE_LOG(LogAsdaLoadFadeTest, Log, TEXT("ЗАТЕМНЕНИЕ этап 7: вход с логина BeginMapLoad(%d), затем обрыв до 4003"), C->TargetMap);
			LM->BeginMapLoad(C->TargetMap);
			C->Stage = 7; C->T0 = Now; C->LastShot = -1.0;
			return true;
		case 7:   // целевая карта: экран висит → обрыв
			if (W && !AsdaWorldKind::IsLoadingMap(W) && !AsdaWorldKind::IsLoginScene(W) && W->HasBegunPlay() && Dt > 3.0)
			{
				C->Expect(LM->IsLoadingScreenVisible() || !C->bKeepOn, TEXT("7: на целевой карте до 4003 экран загрузки висит"));
				if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C))
				{
					// Тот же делегат, что `HandleConnectionClosed` при FIN/RST сервера (причина 255 — 4101 не приходил).
					UE_LOG(LogAsdaLoadFadeTest, Log, TEXT("ЗАТЕМНЕНИЕ этап 7: обрыв игрового соединения (OnGameDisconnected, причина 255)"));
					// Причину пишем как `HandleConnectionClosed` (0xFF — без 4101): иначе в пакете осталась бы чужая от прошлого самотеста.
					Net->LastDisconnectReason = 0xFF;
					Net->bInGameConnected = true;
					Net->OnGameDisconnected.Broadcast();
					Net->bInGameConnected = false;
				}
				C->Stage = 8; C->T0 = Now; C->LastShot = -1.0;
				return true;
			}
			if (Dt > 60.0) { C->Expect(false, TEXT("7: целевая карта не началась за 60 с")); Finish(C); return false; }
			return true;
		case 8:   // через 0.5 с: экран снят, попап открыт
		{
			if (Dt < 0.5) { return true; }
			if (C->LastShot < 0.0) { Shot(C, TEXT("disconnect"), Now); }   // кадр с попапом — до его закрытия
			if (Dt < 1.0) { return true; }
			UAsdaDisconnectPopupSubsystem* Pop = Sub<UAsdaDisconnectPopupSubsystem>(C);
			const bool bPop = Pop && Pop->IsOpen();
			if (UAsdaLoadManagerSubsystem::IsDisconnectDropsScreenOn())
			{
				C->Expect(!LM->IsLoadingScreenVisible() && !LM->IsLoadScreenOnViewport() && bPop,
					TEXT("7: обрыв во время ожидания 4003 — экран загрузки снят сразу, попап обрыва открыт"));
			}
			else
			{
				C->Expect(LM->IsLoadingScreenVisible() && !bPop,
					TEXT("7: ключ DisconnectDropsScreen 0 — попап погашен гейтом загрузки, экран висит (как было)"));
				LM->AbortLoadingScreen(TEXT("самотест: уборка после ключа 0"));
			}
			if (Pop && bPop) { Pop->ClosePopup(); }
			Finish(C);
			return false;
		}
		default:
			return false;
		}
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!Net || !WS || !GI->GetSubsystem<UAsdaLoadManagerSubsystem>()) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем")); return; }
		FCtxRef C = MakeShared<FCtx>();
		C->GI = GI; C->bQuit = KA.bQuit; C->bShots = !KA.Has(TEXT("noshots"));
		for (const FString& P : KA.Pos)
		{
			if (P.StartsWith(TEXT("map="))) { C->TargetMap = FCString::Atoi(*P.Mid(4)); }
			if (P.Equals(TEXT("pic=logo"), ESearchCase::IgnoreCase)) { C->bLogoPic = true; }
		}
		C->bFadeOn = CVarF(TEXT("asda.Load.FadeSec")) > 0.f;
		C->bKeepOn = CVarI(TEXT("asda.Load.KeepScreenOnTravel")) != 0;
		C->NetScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		Net->SetSelfUserNumForDebug(kMyUserNum);
		UE_LOG(LogAsdaLoadFadeTest, Log, TEXT("ЗАТЕМНЕНИЕ старт: FadeSec=%.2f KeepScreenOnTravel=%d RevealFrames=%d карта перехода %d"),
			CVarF(TEXT("asda.Load.FadeSec")), CVarI(TEXT("asda.Load.KeepScreenOnTravel")), CVarI(TEXT("asda.Load.RevealFrames")), C->TargetMap);
		// Свой павн на текущей карте — штатными крюками (как у самотестов прыжка/плавания): номер карты нужен, иначе
		//   спавн «до загрузки уровня» откладывается до следующего PostLoadMap и сработал бы на целевой карте раньше «4003».
		UAsdaLoadManagerSubsystem* LM0 = GI->GetSubsystem<UAsdaLoadManagerSubsystem>();
		if (LM0->CurrentMapId < 0) { LM0->CurrentMapId = 3; }
		if (AsdaSelfTestKit::NeedSelfSpawn(W)) { WS->DebugSpawnSelfAt(kTileX, kTileY); }
		C->T0 = FPlatformTime::Seconds();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([C](float) { return Step(C); }), 0.f);
	}
}

// Консольная команда самотеста.
static FAutoConsoleCommandWithWorldAndArgs GAsdaLoadFadeSelfTest(
	TEXT("asda.Load.FadeSelfTest"),
	TEXT("Самотест экранного затемнения смены карты (баг 5, 31.10): BeginMapLoad → режим 3 → L_Loading → целевая карта (экран висит до 4003) → "
	     "эмуляция 4003 → готовность мира → режим 4; своё 4056 → режим 0. Кадры PNG в Reports/loading_fade/shots. Офлайн, на карте мира. "
	     "[map=<id>] [noshots] `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W) { AsdaLoadFadeTest::Run(A, W); }));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
