// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТЫ «БРОНЯ ПОСЛЕ РАЗРЫВА НА ВЫБОРЕ ПЕРСОНАЖА» (06.11.2026, волна relog_armor). [ПОРТ]
//   Баг игроков: разрыв на экране выбора → новый вход → персонаж в мире без брони, вещи на сервере целы.
//   Причина и правка — `UAsdaWorldSubsystem::TrySpawnLocalPlayer`, `UAsdaInventorySubsystem::ApplyPendingBatches`,
//   `AAsdaLoginGameMode::OnLoginKick`. Офлайн (`-AsdaNoNet`), пакеты — штатным `HandlePacket` (`DebugFeedPacket`).
//   1) `asda.SelfTest.RelogArmorWorld [shot=<папка>] [quit]` (карта мира). Запись char-list с пустым экипом (как после
//      `SendLogin` без 1005/1007) и 1027 с комплектом, ключ `asda.Char.SelfWornFromContainer` 0 и 1:
//      A) персонаж уже стоит, потом разбирается 1027; B) 1027 разобран, потом спавн (портал, смена канала).
//      Контейнер 3 цел в обоих значениях (только вид); при 0 — голая модель, при 1 — части на модели. Кадры `relog_*`.
//   2) `asda.SelfTest.RelogKickScene [shot=<папка>] [quit]` (карта логина). Настоящие 1007+1006 → перелёт и шкатулка, кик 4101:
//      ключ `asda.Login.KickReloadsScene` 0 — экран выбора остаётся; 1 — ОК окна перезагружает сцену, фазы и окно выбора сняты,
//      в том числе при кике во время перелёта (случай журнала 27.10). Кадры `kick_*`.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Blueprint/UserWidget.h"
#include "Characters/AsdaCharAssembly.h"
#include "Components/Button.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Login/AsdaLoginGameMode.h"
#include "Misc/Paths.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UnrealClient.h"   // FScreenshotRequest — кадры
#include "Kismet/GameplayStatics.h"   // ★ R5-fix6-c (13.11.2026): перезагрузка сцены входа перед прогоном (тот же вызов, что у игры)
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaWorldSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaRelogArmorTest, Log, All);

// Друг сцены логина: фазы сцены и окно ввода (закрытые поля; своей логики здесь нет).
struct FAsdaRelogArmorTestAccess
{
	static bool Flight(const AAsdaLoginGameMode* GM) { return GM->bFlightActive; }
	static bool Opening(const AAsdaLoginGameMode* GM) { return GM->bCasketOpening; }
	static bool Opened(const AAsdaLoginGameMode* GM) { return GM->bCasketOpened; }
	static UUserWidget* Login(const AAsdaLoginGameMode* GM) { return GM->LoginWidget; }
};

namespace AsdaRelogArmorTest
{
	static const TCHAR* kWorldCmd = TEXT("asda.SelfTest.RelogArmorWorld");   // самотест мира
	static const TCHAR* kSceneCmd = TEXT("asda.SelfTest.RelogKickScene");    // самотест сцены логина
	static const TCHAR* kWornKey = TEXT("asda.Char.SelfWornFromContainer");  // ключ правки мира
	static const TCHAR* kKickKey = TEXT("asda.Login.KickReloadsScene");      // ключ правки логина
	constexpr int32 kMe = 4321;                                              // свой userNum (4003)
	// Комплект жалобы 31.10: слот → код (шлем, грудь, штаны, обувь, перчатки, оружие).
	static const int32 kGear[6][2] = { { 0, 74394 }, { 1, 74395 }, { 2, 74397 }, { 3, 74398 }, { 4, 74396 }, { 9, 62309 } };

	// Шаг сценария: true — шаг завершён, false — ждёт следующего вызова. Аргумент — секунды от начала шага.
	using FStep = TFunction<bool(float)>;

	// Поставить ключ консоли (возврат — вручную в конце: область ключей самотеста привязана к миру, а сцена перезагружается).
	static void SetKey(const TCHAR* Key, int32 V)
	{
		if (IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(Key)) { C->Set(V, ECVF_SetByConsole); }
	}

	// Тело 1027 (651 б): код 1, 11 записей по 57 б с body+5, индекс = экип-слот. `Codes` — слот → код.
	static TArray<uint8> Body1027(const TMap<int32, int32>& Codes)
	{
		TArray<uint8> B; B.SetNumZeroed(651);
		B[0] = 1;
		B[632 + 15] = 1;   // блок опций персонажа: показ шлема (как в живом пакете, иначе шлем законно скрыт)
		for (const TPair<int32, int32>& P : Codes)
		{
			AsdaBytes::WrI32(B.GetData() + 5 + 57 * P.Key, P.Value);
		}
		return B;
	}

	// Состояние самотеста мира.
	struct FWorldRun
	{
		TWeakObjectPtr<UWorld> W;                     // мир
		bool bQuit = false;                           // `quit` в конце
		FString ShotDir;                              // папка кадров (пусто — без кадров)
		int32 Ok = 0, Fail = 0;                       // счётчики проверок
		TArray<FStep> Steps;                          // сценарий
		int32 Idx = 0;                                // текущий шаг
		double StepT0 = 0.0;                          // начало шага (время мира)
		FDelegateHandle H;                            // подписка на тик мира
		TUniquePtr<FAsdaNetSelfScope> NetScope;       // свой номер — вернуть после прогона
		TWeakObjectPtr<UAsdaNetSubsystem> Net;        // сеть
		TWeakObjectPtr<UAsdaWorldSubsystem> WS;       // мир
		TWeakObjectPtr<UAsdaItemTableSubsystem> Items;// таблицы предметов
		TWeakObjectPtr<UAsdaInventorySubsystem> Inv;  // инвентарь (контейнер 3 = надетое)
		TMap<int32, int32> OrigCells;                 // контейнер 3 до прогона: слот → код
		int32 OrigWorn = 1, OrigKick = 1;             // ключи до прогона
		float OrigArm = -1.f;                         // длина штанги камеры до кадров (−1 — не менялась)

		// Записать итог проверки.
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaRelogArmorTest, Log,   TEXT("БРОНЯ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaRelogArmorTest, Error, TEXT("БРОНЯ FAIL %s"), *What); }
		}
	};

	// Свой павн.
	static AAsdaPlayerCharacter* MeOf(const FWorldRun& S)
	{
		const UAsdaWorldSubsystem* WS = S.WS.Get();
		return WS ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get()) : nullptr;
	}

	// Число надетых частей комплекта на модели (компоненты с меткой части и мешем предмета).
	static int32 PartsOnModel(const FWorldRun& S)
	{
		const AAsdaPlayerCharacter* Me = MeOf(S);
		UAsdaItemTableSubsystem* Items = S.Items.Get();
		if (!Me || !Items) { return 0; }
		const bool bMale = S.WS.Get() && S.WS->SelfSex != 2;
		TInlineComponentArray<UMeshComponent*> Comps(Me);
		int32 N = 0;
		for (const auto& G : kGear)
		{
			const FString Mesh = Items->GetItemMeshName(G[1], G[0] == 9 ? true : bMale);
			for (const UMeshComponent* C : Comps)
			{
				if (!IsValid(C) || !C->ComponentHasTag(UAsdaCharAssembly::WornPartTag)) { continue; }
				FString Name;
				if (const USkeletalMeshComponent* SK = Cast<USkeletalMeshComponent>(C)) { Name = GetNameSafe(SK->GetSkeletalMeshAsset()); }
				else if (const UStaticMeshComponent* SM = Cast<UStaticMeshComponent>(C)) { Name = GetNameSafe(SM->GetStaticMesh()); }
				if (Name.Equals(Mesh, ESearchCase::IgnoreCase)) { ++N; break; }
			}
		}
		return N;
	}

	// Сколько вещей комплекта лежит в контейнере 3.
	static int32 PartsInContainer(const FWorldRun& S)
	{
		const UAsdaInventorySubsystem* Inv = S.Inv.Get();
		int32 N = 0;
		for (const auto& G : kGear)
		{
			const FAsdaItemInstance* C = Inv ? Inv->GetCell(3, G[0]) : nullptr;
			N += (C && !C->IsEmpty() && C->ItemCode == G[1]) ? 1 : 0;
		}
		return N;
	}

	// Запись выбранного персонажа без экипа — как после `SendLogin` без свежих 1005/1007.
	static void EmptyCharList(FWorldRun& S)
	{
		UAsdaNetSubsystem* N = S.Net.Get();
		N->CharList.SetNum(3);
		FAsdaCharRecord& R = N->CharList[0];
		R.Slot = 0; R.RawSlotId = 10; R.Name = TEXT("RelogTest"); R.Sex = 1; R.Level = 40;
		R.Equip.Reset();
		N->LastSelectedRawSlot = 10;
	}

	// Раздеть модель: снимок без вещей и пересборка (начальное состояние спавна с пустой записью).
	static void Undress(FWorldRun& S)
	{
		UAsdaWorldSubsystem* WS = S.WS.Get();
		FAsdaWornParts& P = WS->SelfParts;
		P.Helmet = P.Chest = P.Pants = P.Shoes = P.Gloves = P.Weapon = P.Shield = 0;
		P.bHelmetShown = true;
		if (AAsdaPlayerCharacter* Me = MeOf(S)) { Me->InitAppearance(WS->SelfSex, WS->VisualSelfParts()); }
	}

	// Кадр своего персонажа крупным планом.
	static void Shot(FWorldRun& S, const FString& Name)
	{
		UWorld* W = S.W.Get();
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		AAsdaPlayerCharacter* Me = MeOf(S);
		if (PC && Me)
		{
			Me->SetActorRotation(FRotator(0.f, PC->GetControlRotation().Yaw + 160.f, 0.f));   // лицом к камере
			if (Me->CameraArm)
			{
				if (S.OrigArm < 0.f) { S.OrigArm = Me->CameraArm->TargetArmLength; }
				Me->CameraArm->TargetArmLength = 260.f;
				Me->CameraArm->bEnableCameraLag = false;
			}
		}
		if (S.ShotDir.IsEmpty()) { return; }
		const FString Path = FPaths::Combine(S.ShotDir, Name + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaRelogArmorTest, Log, TEXT("БРОНЯ кадр %s"), *Path);
	}

	// Конец прогона мира: контейнер 3, модель, ключи, номер.
	static void FinishWorld(const TSharedRef<FWorldRun>& SP)
	{
		FWorldRun& S = *SP;
		FWorldDelegates::OnWorldPostActorTick.Remove(S.H);
		if (UAsdaNetSubsystem* N = S.Net.Get()) { N->DebugFeedPacket(1027, Body1027(S.OrigCells)); }
		if (AAsdaPlayerCharacter* Me = MeOf(S))
		{
			if (S.OrigArm >= 0.f && Me->CameraArm) { Me->CameraArm->TargetArmLength = S.OrigArm; }
		}
		SetKey(kWornKey, S.OrigWorn);
		S.NetScope.Reset();
		AsdaSelfTestKit::Finish(S.W.Get(), kWorldCmd, S.bQuit, LogAsdaRelogArmorTest, TEXT("БРОНЯ"), S.Ok, S.Fail);
	}

	// Тик сценария мира.
	static void TickWorld(const TSharedRef<FWorldRun>& InSP, UWorld* InW)
	{
		const TSharedRef<FWorldRun> SP = InSP;
		FWorldRun& S = *SP;
		UWorld* W = S.W.Get();
		if (!W) { FWorldDelegates::OnWorldPostActorTick.Remove(S.H); return; }
		if (InW != W) { return; }
		while (S.Steps.IsValidIndex(S.Idx))
		{
			const float T = (float)(W->GetTimeSeconds() - S.StepT0);
			if (!S.Steps[S.Idx](T)) { return; }
			++S.Idx;
			S.StepT0 = W->GetTimeSeconds();
		}
		FinishWorld(SP);
	}

	// Подать 4003 своего персонажа.
	static void Feed4003(FWorldRun& S)
	{
		TArray<uint8> B; B.SetNumZeroed(9);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)kMe);
		AsdaBytes::WrI16(B.GetData() + 2, 256);
		AsdaBytes::WrI16(B.GetData() + 4, 256);
		B[8] = 1;
		S.Net->DebugFeedPacket(4003, B);
	}

	// Сценарий мира.
	static void BuildWorld(const TSharedRef<FWorldRun>& SP)
	{
		FWorldRun* S = &SP.Get();
		auto Do = [](TFunction<void()> Fn) { return FStep([Fn](float) { Fn(); return true; }); };
		auto Wait = [](float Sec) { return FStep([Sec](float T) { return T >= Sec; }); };
		TMap<int32, int32> Gear;
		for (const auto& G : kGear) { Gear.Add(G[0], G[1]); }

		S->Steps.Add(Do([S]()
		{
			GEngine->Exec(S->W.Get(), TEXT("Asda.Load.CurrentMap 3"));
			Feed4003(*S);
		}));
		S->Steps.Add(Wait(4.0f));   // эффект появления
		S->Steps.Add(Do([S]()
		{
			S->Check(MeOf(*S) != nullptr, TEXT("свой персонаж на карте"));
			if (const UAsdaInventorySubsystem* Inv = S->Inv.Get())
			{
				for (int32 Slot = 0; Slot < 11; ++Slot)
				{
					const FAsdaItemInstance* C = Inv->GetCell(3, Slot);
					if (C && !C->IsEmpty()) { S->OrigCells.Add(Slot, C->ItemCode); }
				}
			}
			EmptyCharList(*S);
		}));
		for (const int32 Key : { 0, 1 })
		{
			// A) персонаж уже стоит, потом разбирается 1027 (первый вход: 1027 разбирает первый кадр в мире).
			S->Steps.Add(Do([S, Key, Gear]()
			{
				SetKey(kWornKey, Key);
				Undress(*S);
				S->Net->DebugFeedPacket(1027, Body1027(Gear));
			}));
			S->Steps.Add(Wait(1.0f));
			S->Steps.Add(Do([S, Key]()
			{
				const int32 Model = PartsOnModel(*S), Cont = PartsInContainer(*S);
				S->Check(Cont == 6, FString::Printf(TEXT("A ключ %d: контейнер 3 цел, вещей %d из 6 (вещи не сняты — только вид)"), Key, Cont));
				S->Check(Model == (Key ? 6 : 0), FString::Printf(TEXT("A ключ %d: стоящий персонаж, разобран 1027: частей на модели %d (ждём %d)"), Key, Model, Key ? 6 : 0));
				Shot(*S, FString::Printf(TEXT("relog_A_key%d"), Key));
			}));
			S->Steps.Add(Wait(1.0f));
			// B) 1027 разобран, потом спавн с пустой записью char-list.
			S->Steps.Add(Do([S]()
			{
				if (AAsdaPlayerCharacter* Me = MeOf(*S)) { Me->Destroy(); }
				S->WS->LocalPlayerActor = nullptr;
				S->OrigArm = -1.f;
				EmptyCharList(*S);
				Feed4003(*S);
			}));
			S->Steps.Add(Wait(7.0f));
			S->Steps.Add(Do([S, Key]()
			{
				const int32 Model = PartsOnModel(*S);
				S->Check(MeOf(*S) != nullptr, FString::Printf(TEXT("B ключ %d: персонаж пересоздан"), Key));
				S->Check(Model == (Key ? 6 : 0), FString::Printf(TEXT("B ключ %d: спавн с пустой записью: частей на модели %d (ждём %d)"), Key, Model, Key ? 6 : 0));
				Shot(*S, FString::Printf(TEXT("relog_B_key%d"), Key));
			}));
			S->Steps.Add(Wait(1.0f));
		}
		// Совпавший 1027 не пересобирает модель: контрольный повтор при ключе 1.
		S->Steps.Add(Do([S, Gear]()
		{
			S->Net->DebugFeedPacket(1027, Body1027(Gear));
		}));
		S->Steps.Add(Wait(0.5f));
		S->Steps.Add(Do([S]()
		{
			S->Check(PartsOnModel(*S) == 6, TEXT("повтор того же 1027: модель одета"));
		}));
	}

	// Старт самотеста мира.
	static void StartWorld(const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kWorldCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaItemTableSubsystem* Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		IConsoleVariable* Key = IConsoleManager::Get().FindConsoleVariable(kWornKey);
		if (!Net || !WS || !Items || !Inv || !Key)
		{
			AsdaSelfTestKit::Refused(W, kWorldCmd, KA.bQuit, TEXT("нет подсистем или ключа отката"));
			return;
		}
		TSharedRef<FWorldRun> SP = MakeShared<FWorldRun>();
		SP->W = W; SP->bQuit = KA.bQuit; SP->Net = Net; SP->WS = WS; SP->Items = Items; SP->Inv = Inv;
		SP->OrigWorn = Key->GetInt();
		for (const FString& P : KA.Pos)
		{
			if (P.StartsWith(TEXT("shot="), ESearchCase::IgnoreCase))
			{
				SP->ShotDir = FPaths::ConvertRelativePathToFull(P.Mid(5));
				IFileManager::Get().MakeDirectory(*SP->ShotDir, /*Tree*/true);
			}
		}
		SP->NetScope = MakeUnique<FAsdaNetSelfScope>(Net, kWorldCmd);
		BuildWorld(SP);
		SP->StepT0 = W->GetTimeSeconds();
		SP->H = FWorldDelegates::OnWorldPostActorTick.AddLambda([SP](UWorld* InW, ELevelTick, float) { TickWorld(SP, InW); });
	}

	// ═══════════════════ Сцена логина ═══════════════════

	// Состояние самотеста сцены (живёт через перезагрузку уровня: тикер, а не тик мира).
	struct FSceneRun
	{
		TWeakObjectPtr<UAsdaNetSubsystem> Net;        // сеть (GameInstance переживает уровень)
		TWeakObjectPtr<AAsdaLoginGameMode> GM;        // текущая сцена логина
		bool bQuit = false;                           // `quit` в конце
		FString ShotDir;                              // папка кадров
		int32 Ok = 0, Fail = 0;                       // счётчики проверок
		TArray<FStep> Steps;                          // сценарий
		int32 Idx = 0;                                // текущий шаг
		double StepT0 = 0.0;                          // начало шага
		FTSTicker::FDelegateHandle H;                 // подписка на тикер
		int32 OrigKick = 1;                           // ключ до прогона

		// Записать итог проверки.
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaRelogArmorTest, Log,   TEXT("КИК ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaRelogArmorTest, Error, TEXT("КИК FAIL %s"), *What); }
		}
	};

	// Окно менеджера окон.
	static UUserWidget* Window(const FSceneRun& S, const TCHAR* Id)
	{
		UAsdaNetSubsystem* N = S.Net.Get();
		UGameInstance* GI = N ? N->GetGameInstance() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		return WM ? WM->FindWindow(FName(Id)) : nullptr;
	}

	// Окно выбора персонажа видно игроку.
	static bool CharSelectShown(const FSceneRun& S)
	{
		const UUserWidget* W = Window(S, TEXT("CHARSELECT_WIN"));
		return W && W->IsInViewport() && W->GetVisibility() != ESlateVisibility::Collapsed && W->GetVisibility() != ESlateVisibility::Hidden;
	}

	// Текущая сцена логина (после перезагрузки — новый режим игры).
	static AAsdaLoginGameMode* CurrentGM(const FSceneRun& S)
	{
		UAsdaNetSubsystem* N = S.Net.Get();
		UGameInstance* GI = N ? N->GetGameInstance() : nullptr;
		UWorld* W = GI ? GI->GetWorld() : nullptr;
		return W ? Cast<AAsdaLoginGameMode>(W->GetAuthGameMode()) : nullptr;
	}

	// Настоящий список персонажей: 1007 (экип, грудь комплекта) и 1006 (запись слота 0).
	static void FeedCharList(FSceneRun& S)
	{
		UAsdaNetSubsystem* N = S.Net.Get();
		TArray<uint8> E; E.SetNumZeroed(160);
		E[4] = 10;
		for (const auto& G : kGear) { AsdaBytes::WrI32(E.GetData() + 14 * G[0] + 6, G[1]); }
		N->DebugFeedPacket(1007, E);
		TArray<uint8> L; L.SetNumZeroed(193);
		L[4] = 10;
		const char* Name = "RelogTest";
		for (int32 i = 0; Name[i]; ++i) { L[5 + i] = (uint8)Name[i]; }
		L[4 + 22] = 1; L[4 + 23] = 1; L[4 + 25] = 40;
		N->DebugFeedPacket(1006, L);
	}

	// Кик 4101 код 1 (нет пинга) — как в журнале 27.10.
	static void FeedKick(FSceneRun& S)
	{
		S.Net->DebugFeedPacket(4101, TArray<uint8>({ 1, 0xD2, 0x07 }));
	}

	// Нажать ОК окна сообщения (делегат самой кнопки, его зовёт Slate).
	static bool ClickOk(FSceneRun& S)
	{
		UUserWidget* M = Window(S, TEXT("MESSAGE_WIN"));
		UButton* B = M ? Cast<UButton>(M->GetWidgetFromName(TEXT("MESSAGE_BUT"))) : nullptr;
		if (!B) { return false; }
		B->OnClicked.Broadcast();
		return true;
	}

	// Кадр сцены логина (с окнами).
	static void SceneShot(FSceneRun& S, const TCHAR* Name)
	{
		if (S.ShotDir.IsEmpty()) { return; }
		const FString Path = FPaths::Combine(S.ShotDir, FString(Name) + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaRelogArmorTest, Log, TEXT("КИК кадр %s"), *Path);
	}

	// Сцена после перезагрузки чистая: новый режим игры, фаз нет, окно выбора снято, окно входа на экране.
	static void CheckClean(FSceneRun& S, const AAsdaLoginGameMode* Old, const TCHAR* Tag)
	{
		const AAsdaLoginGameMode* GM = CurrentGM(S);
		S.Check(GM && GM != Old, FString::Printf(TEXT("%s: сцена логина перезагружена"), Tag));
		S.Check(GM && !FAsdaRelogArmorTestAccess::Flight(GM) && !FAsdaRelogArmorTestAccess::Opening(GM) && !FAsdaRelogArmorTestAccess::Opened(GM),
			FString::Printf(TEXT("%s: фазы перелёта и шкатулки сняты"), Tag));
		S.Check(!CharSelectShown(S), FString::Printf(TEXT("%s: окна выбора с «Играть» нет"), Tag));
		const UUserWidget* L = GM ? FAsdaRelogArmorTestAccess::Login(GM) : nullptr;
		S.Check(L && L->GetVisibility() != ESlateVisibility::Collapsed, FString::Printf(TEXT("%s: окно входа на экране"), Tag));
	}

	// Сценарий сцены.
	static void BuildScene(const TSharedRef<FSceneRun>& SP)
	{
		FSceneRun* S = &SP.Get();
		auto Do = [](TFunction<void()> Fn) { return FStep([Fn](float) { Fn(); return true; }); };
		auto Wait = [](float Sec) { return FStep([Sec](float T) { return T >= Sec; }); };
		// Ждать новую сцену: режим игры сменился и окно входа создано.
		TSharedRef<TWeakObjectPtr<AAsdaLoginGameMode>> Prev = MakeShared<TWeakObjectPtr<AAsdaLoginGameMode>>();
		auto WaitReload = [S, Prev](float T)
		{
			const AAsdaLoginGameMode* GM = CurrentGM(*S);
			return T > 25.f || (GM && GM != Prev->Get() && GM->HasActorBegunPlay() && FAsdaRelogArmorTestAccess::Login(GM) != nullptr && T > 2.f);
		};

		// ★ R5-fix6-c (13.11.2026): ИСХОДНОЕ СОСТОЯНИЕ СТАВИТ САМ ТЕСТ. В RU-пачке перед ним идёт `asda.Login.SelfTestSoulInfoTip` (на EN его
		//   нет): он открывает шкатулку и оставляет сцену на выборе персонажа. Тогда шкатулка уже «открыта», окно выбора
		//   скрыто, 1007+1006 его не показывают — первая проверка и контроль ключа 0 красные (15/2 на RU при 17/0 на EN и
		//   одиночно). Если сцена не в исходном состоянии — перезагружаем её тем же вызовом, что игра при разрыве на выборе.
		const TSharedRef<bool> bPreReload = MakeShared<bool>(false);
		S->Steps.Add(Do([S, Prev, bPreReload]()
		{
			AAsdaLoginGameMode* GM = CurrentGM(*S);
			if (GM && (FAsdaRelogArmorTestAccess::Flight(GM) || FAsdaRelogArmorTestAccess::Opening(GM) || FAsdaRelogArmorTestAccess::Opened(GM) || CharSelectShown(*S)))
			{
				*Prev = GM;
				*bPreReload = true;
				UE_LOG(LogAsdaRelogArmorTest, Log, TEXT("КИК инфо: сцена входа не в исходном состоянии (оставлена прежним самотестом) — перезагрузка перед прогоном"));
				UGameplayStatics::OpenLevel(GM->GetWorld(), FName(TEXT("/Game/Map/Login")));
			}
		}));
		S->Steps.Add(FStep([WaitReload, bPreReload](float T) { return !*bPreReload || WaitReload(T); }));

		// ── Экран выбора открыт, ключ 0: прежнее поведение (воспроизведение) ──
		S->Steps.Add(Do([S]() { SetKey(kKickKey, 0); FeedCharList(*S); }));
		S->Steps.Add(FStep([S](float T)
		{
			const AAsdaLoginGameMode* GM = CurrentGM(*S);
			return T > 25.f || (GM && FAsdaRelogArmorTestAccess::Opened(GM) && T > 1.f);
		}));
		// ★ 11.11.2026 (D4b): окно выбора после открытия шкатулки ждём условием (не меньше 1.5 с, потолок 20 с), а не фиксированные
		//   1.5 с — под нагрузкой оно появлялось позже, и проверка ниже (а за ней контроль ключа 0) падала через раз.
		S->Steps.Add(FStep([S](float T) { return T > 20.f || (T >= 1.5f && CharSelectShown(*S)); }));
		S->Steps.Add(Do([S]()
		{
			const AAsdaLoginGameMode* GM = CurrentGM(*S);
			S->Check(GM && FAsdaRelogArmorTestAccess::Opened(GM) && CharSelectShown(*S), TEXT("настоящие 1007+1006: шкатулка открыта, окно выбора показано"));
			SceneShot(*S, *FString::Printf(TEXT("kick_0_before_%s"), FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en")));
		}));
		S->Steps.Add(Wait(0.5f));
		S->Steps.Add(Do([S, Prev]()
		{
			*Prev = CurrentGM(*S);
			FeedKick(*S);
			S->Check(Window(*S, TEXT("MESSAGE_WIN")) != nullptr, TEXT("ключ 0: кик → окно сообщения"));
			S->Check(ClickOk(*S), TEXT("ключ 0: ОК нажат"));
		}));
		S->Steps.Add(Wait(1.5f));
		S->Steps.Add(Do([S, Prev]()
		{
			S->Check(CurrentGM(*S) == Prev->Get() && CharSelectShown(*S), TEXT("ключ 0 (воспроизведение): сцена та же, окно выбора с «Играть» осталось под окном входа"));
			SceneShot(*S, *FString::Printf(TEXT("kick_0_after_%s"), FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en")));
		}));
		S->Steps.Add(Wait(0.5f));
		// ── Экран выбора открыт, ключ 1 ──
		S->Steps.Add(Do([S, Prev]()
		{
			SetKey(kKickKey, 1);
			*Prev = CurrentGM(*S);
			FeedKick(*S);
			S->Check(Window(*S, TEXT("MESSAGE_WIN")) != nullptr, TEXT("ключ 1, экран выбора: кик → окно сообщения"));
			S->Check(CurrentGM(*S) == Prev->Get(), TEXT("ключ 1: до ОК сцена не перезагружается (текст разрыва читается)"));
			S->Check(ClickOk(*S), TEXT("ключ 1: ОК нажат"));
		}));
		S->Steps.Add(FStep(WaitReload));
		S->Steps.Add(Wait(6.0f));   // заставка загрузки сошла: кадр показывает то, что увидит игрок
		S->Steps.Add(Do([S, Prev]()
		{
			CheckClean(*S, Prev->Get(), TEXT("ключ 1, экран выбора"));
			SceneShot(*S, *FString::Printf(TEXT("kick_1_after_%s"), FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en")));
		}));
		S->Steps.Add(Wait(0.5f));
		// ── Кик во время перелёта, ключ 1 (журнал 27.10) ──
		S->Steps.Add(Do([S]() { FeedCharList(*S); }));
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S, Prev]()
		{
			const AAsdaLoginGameMode* GM = CurrentGM(*S);
			S->Check(GM && FAsdaRelogArmorTestAccess::Flight(GM), TEXT("перелёт к шкатулке идёт"));
			*Prev = CurrentGM(*S);
			FeedKick(*S);
			S->Check(ClickOk(*S), TEXT("кик в перелёте: ОК нажат"));
		}));
		S->Steps.Add(FStep(WaitReload));
		S->Steps.Add(Do([S, Prev]() { CheckClean(*S, Prev->Get(), TEXT("кик в перелёте")); }));
	}

	// Тик сценария сцены. false — снять тикер.
	static bool TickScene(const TSharedRef<FSceneRun>& SP)
	{
		FSceneRun& S = *SP;
		while (S.Steps.IsValidIndex(S.Idx))
		{
			const double Now = FPlatformTime::Seconds();
			if (!S.Steps[S.Idx]((float)(Now - S.StepT0))) { return true; }
			++S.Idx;
			S.StepT0 = FPlatformTime::Seconds();
		}
		SetKey(kKickKey, S.OrigKick);
		UAsdaNetSubsystem* N = S.Net.Get();
		UGameInstance* GI = N ? N->GetGameInstance() : nullptr;
		const FString Tail = FString::Printf(TEXT(" язык=%s"), FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en"));
		AsdaSelfTestKit::Finish(GI ? GI->GetWorld() : nullptr, kSceneCmd, S.bQuit, LogAsdaRelogArmorTest, TEXT("КИК"), S.Ok, S.Fail, *Tail);
		return false;
	}

	// Старт самотеста сцены.
	static void StartScene(const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kSceneCmd, KA)) { return; }
		AAsdaLoginGameMode* GM = W ? Cast<AAsdaLoginGameMode>(W->GetAuthGameMode()) : nullptr;
		UAsdaNetSubsystem* N = AsdaSelfTestKit::NetOf(W);
		IConsoleVariable* Key = IConsoleManager::Get().FindConsoleVariable(kKickKey);
		if (!GM || !N || !Key)
		{
			AsdaSelfTestKit::Refused(W, kSceneCmd, KA.bQuit, TEXT("нет сцены логина, сети или ключа (нужна --map login)"));
			return;
		}
		TSharedRef<FSceneRun> SP = MakeShared<FSceneRun>();
		SP->Net = N; SP->GM = GM; SP->bQuit = KA.bQuit; SP->OrigKick = Key->GetInt();
		for (const FString& P : KA.Pos)
		{
			if (P.StartsWith(TEXT("shot="), ESearchCase::IgnoreCase))
			{
				SP->ShotDir = FPaths::ConvertRelativePathToFull(P.Mid(5));
				IFileManager::Get().MakeDirectory(*SP->ShotDir, /*Tree*/true);
			}
		}
		BuildScene(SP);
		SP->StepT0 = FPlatformTime::Seconds();
		SP->H = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([SP](float) { return TickScene(SP); }));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaRelogArmorWorldCmd(
	TEXT("asda.SelfTest.RelogArmorWorld"),
	TEXT("Самотест: своя модель при пустой записи char-list и пришедшем 1027 (ключ asda.Char.SelfWornFromContainer 0/1, порядок "
	     "спавна и разбора 1027 — оба). Офлайн, карта мира. `shot=<папка>` — кадры, `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AsdaRelogArmorTest::StartWorld));

static FAutoConsoleCommandWithWorldAndArgs GAsdaRelogKickSceneCmd(
	TEXT("asda.SelfTest.RelogKickScene"),
	TEXT("Самотест: разрыв на выборе персонажа (кик 4101 на открытом экране и в перелёте, ключ asda.Login.KickReloadsScene 0/1). "
	     "Офлайн, карта логина. `shot=<папка>` — кадры, `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AsdaRelogArmorTest::StartScene));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
