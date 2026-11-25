// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «НАДПИСИ НАД ГОЛОВОЙ ПОСЛЕ ПЕРЕПОДКЛЮЧЕНИЯ» — `asda.Overhead.SelfTestRelog [shotdir=<папка>] [quit]`
//   (05.11.2026, баг игроков «после обрыва и повторного входа над чужими персонажами нет имени»). Только офлайн (`-AsdaNoNet`).
//
// ПУТЬ ИГРОКА (ничего не подменяется, кроме самого сокета):
//   • соседи — настоящие тела 4005 (151 б) в штатный приёмник `DebugFeedPacket` → очередь → спаун мира → плашка;
//   • свой персонаж — настоящий 4003 (9 б: userNum, тайл, состояние, шлем) → `OnSelfSpawn` → спаун павна;
//   • моб — запись 4013 штатному приёмнику; выбран целью (имя + полоса HP, как клик ЛКМ);
//   • обрыв — та же рассылка сброса, что делает закрытие игрового сокета (`UAsdaSessionResetSubsystem::Broadcast(Disconnected)`),
//     затем НАСТОЯЩИЙ травел на экран входа (`/Game/Map/Login`, как кнопка ОК окна обрыва), вход персонажа (рассылка
//     `EnteringWorld`, как 1021) и НАСТОЯЩИЙ травел на карту; пакеты входа приходят в порядке живого журнала, в том числе
//     4005 соседа РАНЬШЕ своего 4003;
//   • смена канала — травел на ту же карту (сброс `MapChanged`), затем 4003 и 4005.
//   На каждом круге (до обрыва / после обрыва и входа / после смены канала) проверяется ЖИВОЙ виджет каждой плашки: виджет в
//   вьюпорте, видим, непрозрачность 1, геометрия Slate ненулевая, контрол имени — ожидаемый текст и видим; у цели-моба — имя и
//   полоса HP. Перечисляются ВСЕ персонажи мира (сироты вне реестра тоже) — с причиной, если плашка не видна.
//   Строки — «НАДПИСИ-ПЕРЕЗАХОД», итог — «НАДПИСИ-ПЕРЕЗАХОД ИТОГ: ok=… fail=…». Кадры `relog_<круг>.png` в `shotdir=`.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "World/AsdaOverheadOcclusion.h"
#include "World/AsdaNameplateComponent.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaCoord.h"
#include "AsdaNetSubsystem.h"
#include "AsdaSessionReset.h"
#include "AsdaTestPackets.h"
#include "AsdaBytes.h"
#include "Login/AsdaLoginData.h"
#include "Test/AsdaSelfTestKit.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaRelogPlateTest, Log, All);

namespace AsdaRelogPlateLocal
{
	static const TCHAR* kCmd = TEXT("asda.Overhead.SelfTestRelog");
	static const TCHAR* kPrefix = TEXT("НАДПИСИ-ПЕРЕЗАХОД");
	static const TCHAR* kMap = TEXT("/Game/Map/alpen/alpen");   // карта прогона (alpen = id 3)
	constexpr int32 kSelfCharId = 424250;   // свой charId на прогон
	constexpr int32 kSelfUserNum = 7;       // свой userNum на прогоне
	constexpr int32 kMobRid = 29301;        // renderId моба-цели
	constexpr int32 kMobType = 1;           // тип моба (волк alpen)

	// Сосед прогона: userNum, имя (RU и EN — плашка обязана показать оба), класс/уровень.
	struct FPeer { int32 UserNum; const TCHAR* Name; uint8 Class; uint8 Level; float Fwd; float Side; };
	static const FPeer kPeers[] = {
		{ 3, TEXT("Aria"),   2, 45, 700.f, -260.f },   // имя со снимка жалобы
		{ 0, TEXT("Пахарь"), 1, 30, 650.f,  260.f },   // userNum 0 — законный номер сервера (живой журнал 01.11: LUFFY uidx=0)
		{ 9, TEXT("Kai"),    3, 12, 900.f,    0.f },
	};

	// Тело 4005 (151 б) — раскладка приёмника `OP_RECV_PLAYER_APPEARANCE` (AsdaNetSubsystem.cpp): [0] u16 userNum, [6] шлем,
	//   [11] i32 charId, [15] имя cp1251 20 б, [37] пол, [38] класс, [39] подкласс, [40] уровень, [47] созвездие,
	//   [48..50] причёска/цвет/лицо, [56] f32 x, [60] f32 z, [96 + 5i] экип i32 + заточка.
	static TArray<uint8> Make4005(int32 UserNum, int32 CharId, const FString& Name, uint8 Class, uint8 Level, const FVector2D& PosGame)
	{
		TArray<uint8> B; B.SetNumZeroed(151);
		AsdaBytes::WrI16(B.GetData() + 0, UserNum);
		B[6] = 1;
		AsdaBytes::WrI32(B.GetData() + 11, CharId);
		FAsdaLoginData::EncodeCp1251(Name, B.GetData() + 15, 19);
		B[37] = 2; B[38] = Class; B[39] = 0; B[40] = Level; B[47] = 1; B[48] = 1; B[49] = 1; B[50] = 1;
		const float X = (float)PosGame.X, Z = (float)PosGame.Y;
		FMemory::Memcpy(B.GetData() + 56, &X, 4);
		FMemory::Memcpy(B.GetData() + 60, &Z, 4);
		return B;
	}

	// Тело 4003 (9 б): [0] userNum, [2] тайл X, [4] тайл Z, [6] состояние (0 — живой), [8] показ шлема.
	static TArray<uint8> Make4003(int32 UserNum, int32 TileX, int32 TileZ)
	{
		TArray<uint8> B; B.SetNumZeroed(9);
		AsdaBytes::WrI16(B.GetData() + 0, UserNum);
		AsdaBytes::WrI16(B.GetData() + 2, TileX);
		AsdaBytes::WrI16(B.GetData() + 4, TileZ);
		B[8] = 1;
		return B;
	}

	struct FRun : public TSharedFromThis<FRun>
	{
		TWeakObjectPtr<UGameInstance> GI;   // переживает травел
		bool bQuit = false;
		FString ShotDir;
		int32 Ok = 0, Fail = 0;
		int32 SavedCharId = 0;               // своё charId до прогона (вернуть)
		FAsdaNetSelfScope SelfGuard;         // ★ R5-fix6 (13.11.2026): «свой» сети до прогона — возврат сторожем (в сводке пачки было «свой изменён»)
		FIntPoint SelfTile = FIntPoint(256, 256);
		TArray<TPair<float, TFunction<void(FRun&)>>> Steps;
		int32 Next = 0;
		double DueAt = 0.0;

		UWorld* W() const { return GI.IsValid() ? GI->GetWorld() : nullptr; }
		template <class T> T* Sub() const { return GI.IsValid() ? GI->GetSubsystem<T>() : nullptr; }
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaRelogPlateTest, Log,   TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaRelogPlateTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
		void Add(float Delay, TFunction<void(FRun&)> Fn) { Steps.Emplace(Delay, MoveTemp(Fn)); }
		void Exec(const TCHAR* C) { if (UWorld* Wd = W()) { if (GEngine) { GEngine->Exec(Wd, C); } } }
		void Start()
		{
			DueAt = FPlatformTime::Seconds() + (Steps.Num() ? Steps[0].Key : 0.f);
			TSharedRef<FRun> Self = AsShared();
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self](float) { return Self->TickOnce(); }));
		}
		bool TickOnce()
		{
			if (FPlatformTime::Seconds() < DueAt) { return true; }
			if (Next >= Steps.Num()) { return false; }
			TFunction<void(FRun&)> Fn = Steps[Next].Value;
			++Next;
			Fn(*this);
			if (Next >= Steps.Num())
			{
				if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>()) { Net->SelectedCharId = SavedCharId; }
				SelfGuard.Restore();   // ★ R5-fix6: номер 7 и прочее «своё» — как до прогона
				AsdaSelfTestKit::Finish(W(), kCmd, bQuit, LogAsdaRelogPlateTest, kPrefix, Ok, Fail);
				return false;
			}
			DueAt = FPlatformTime::Seconds() + Steps[Next].Key;
			return true;
		}
	};

	// Точка в игровых координатах перед камерой (от своего павна): вперёд Fwd, вбок Side.
	static FVector2D GameFront(FRun& R, float Fwd, float Side)
	{
		UWorld* Wd = R.W();
		APlayerController* PC = Wd ? Wd->GetFirstPlayerController() : nullptr;
		const UAsdaWorldSubsystem* WS = R.Sub<UAsdaWorldSubsystem>();
		const AActor* Self = WS ? WS->LocalPlayerActor.Get() : nullptr;
		if (!PC || !PC->PlayerCameraManager || !Self) { return FVector2D(25600.f + Fwd, 25600.f + Side); }
		const float Yaw = PC->PlayerCameraManager->GetCameraRotation().Yaw;
		const FVector P = Self->GetActorLocation() + FRotator(0.f, Yaw, 0.f).Vector() * Fwd + FRotator(0.f, Yaw + 90.f, 0.f).Vector() * Side;
		return AsdaCoord::UeToGame2D(P);
	}

	// Пакеты соседей (4005) — штатным приёмником.
	static void FeedPeers(FRun& R, int32 From, int32 To)
	{
		UAsdaNetSubsystem* Net = R.Sub<UAsdaNetSubsystem>();
		if (!Net) { return; }
		for (int32 i = From; i < To && i < UE_ARRAY_COUNT(kPeers); ++i)
		{
			const FPeer& P = kPeers[i];
			Net->DebugFeedPacket(4005, Make4005(P.UserNum, 910000 + P.UserNum, P.Name, P.Class, P.Level, GameFront(R, P.Fwd, P.Side)));
		}
	}

	// Свой вход — 4003 штатным приёмником (userNum, тайл).
	static void FeedSelf(FRun& R)
	{
		UAsdaNetSubsystem* Net = R.Sub<UAsdaNetSubsystem>();
		if (!Net) { return; }
		Net->SelectedCharId = kSelfCharId;
		Net->DebugFeedPacket(4003, Make4003(kSelfUserNum, R.SelfTile.X, R.SelfTile.Y));
	}

	// Моб — запись 4013 и выбор целью (как ЛКМ).
	static void FeedMobAndTarget(FRun& R)
	{
		UAsdaNetSubsystem* Net = R.Sub<UAsdaNetSubsystem>();
		if (!Net) { return; }
		const FVector2D G = GameFront(R, 600.f, 0.f);
		Net->DebugFeedPacket(4013, AsdaTestPackets::MobSpawn4013(kMobRid, kMobType, FIntPoint((int32)(G.X / 100.f), (int32)(G.Y / 100.f)), 1, 100));
	}
	static void TargetMob(FRun& R)
	{
		UWorld* Wd = R.W();
		UAsdaWorldSubsystem* WS = R.Sub<UAsdaWorldSubsystem>();
		AAsdaPlayerController* PC = Wd ? Cast<AAsdaPlayerController>(Wd->GetFirstPlayerController()) : nullptr;
		AActor* M = WS ? (AActor*)WS->FindNetMobByRid(kMobRid) : nullptr;
		if (PC && M) { PC->SetTarget(M); }
	}

	// Живое состояние одной плашки: что видит игрок и почему.
	struct FPlate
	{
		bool bWidget = false, bInViewport = false, bVisible = false, bNameVisible = false;
		float Opacity = 0.f;
		FVector2D Size = FVector2D::ZeroVector;
		FString NameText;
		FString Diag;
		bool Shown(const FString& Want) const
		{
			return bWidget && bInViewport && bVisible && Opacity > 0.99f && Size.X > 0.f && bNameVisible && NameText == Want;
		}
		FString Str() const
		{
			return FString::Printf(TEXT("виджет=%d вьюпорт=%d видим=%d α=%.2f размер=%.0f×%.0f имя «%s» видно=%d | %s"),
				bWidget, bInViewport, bVisible, Opacity, Size.X, Size.Y, *NameText, bNameVisible, *Diag);
		}
	};

	static FPlate PlateOf(const AActor* A, bool bMob)
	{
		FPlate S;
		const UAsdaNameplateComponent* NP = A ? A->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
		if (!NP) { S.Diag = TEXT("компонента плашки нет"); return S; }
		const UUserWidget* Wd = NP->DebugScreenWidget();
		FAsdaOccludeHit H;
		AsdaOverheadOcclusion::TraceNow(A->GetWorld(), NP->GetPlateWorldPoint(), A, &H);
		const FAsdaOccludeState& O = NP->DebugOccState();
		S.Diag = FString::Printf(TEXT("DisplayName «%s» всегда=%d скрыт=%d | перекрытие: замер=%d принято=%d α=%.2f, луч сейчас=%d %s"),
			*NP->DisplayName, NP->bAlwaysShowName ? 1 : 0, A->IsHidden() ? 1 : 0, O.bInit, O.bOccluded, O.Alpha, H.bOccluded ? 1 : 0, *H.What);
		if (!Wd) { return S; }
		S.bWidget = true;
		S.bInViewport = Wd->IsInViewport();
		const ESlateVisibility V = Wd->GetVisibility();
		S.bVisible = V != ESlateVisibility::Collapsed && V != ESlateVisibility::Hidden;
		S.Opacity = Wd->GetRenderOpacity();
		S.Size = Wd->GetCachedGeometry().GetLocalSize();
		if (const UTextBlock* T = Cast<UTextBlock>(Wd->GetWidgetFromName(FName(bMob ? TEXT("TARGETMARK_MONNAME") : TEXT("TARGETMARK_MANNAME1")))))
		{
			S.NameText = T->GetText().ToString();
			const ESlateVisibility TV = T->GetVisibility();
			S.bNameVisible = TV != ESlateVisibility::Collapsed && TV != ESlateVisibility::Hidden;
		}
		return S;
	}

	// Проверки круга: каждый сосед из реестра, цель-моб и перепись всех персонажей мира (сироты вне реестра — тоже).
	static void CheckRound(FRun& R, const TCHAR* Round)
	{
		UWorld* Wd = R.W();
		UAsdaWorldSubsystem* WS = R.Sub<UAsdaWorldSubsystem>();
		if (!Wd || !WS) { R.Check(false, FString::Printf(TEXT("[%s] нет мира"), Round)); return; }
		for (const FPeer& P : kPeers)
		{
			AActor* A = WS->ActorByUserIdx(P.UserNum);
			const bool bPeer = A && A != WS->LocalPlayerActor.Get();
			const FPlate S = bPeer ? PlateOf(A, false) : FPlate();
			R.Check(bPeer && S.Shown(P.Name), FString::Printf(TEXT("[%s] сосед userNum %d «%s»: %s"),
				Round, P.UserNum, P.Name, bPeer ? *S.Str() : TEXT("актора нет в реестре")));
		}
		AActor* M = (AActor*)WS->FindNetMobByRid(kMobRid);
		const FPlate SM = M ? PlateOf(M, true) : FPlate();
		const UAsdaNameplateComponent* MNP = M ? M->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
		R.Check(M && MNP && SM.Shown(MNP->DisplayName) && !MNP->DisplayName.IsEmpty() && MNP->bShowHp,
			FString::Printf(TEXT("[%s] цель-моб rid %d: %s, полоса HP=%d"), Round, kMobRid, M ? *SM.Str() : TEXT("моба нет"), MNP ? (int32)MNP->bShowHp : -1));
		// Перепись: все персонажи мира, их плашки и есть ли они в реестре.
		int32 NChars = 0, NOrphans = 0;
		for (TActorIterator<AAsdaPlayerCharacter> It(Wd); It; ++It)
		{
			AAsdaPlayerCharacter* Ch = *It;
			if (Ch == WS->LocalPlayerActor.Get()) { continue; }
			++NChars;
			const int32 U = WS->FindUserIdxByActor(Ch);
			if (U < 0) { ++NOrphans; }
			UE_LOG(LogAsdaRelogPlateTest, Log, TEXT("%s   [%s] персонаж %s реестр=%d скрыт=%d: %s"), kPrefix, Round, *Ch->GetName(), U,
				Ch->IsHidden() ? 1 : 0, *PlateOf(Ch, false).Str());
		}
		R.Check(NOrphans == 0 && NChars == UE_ARRAY_COUNT(kPeers), FString::Printf(TEXT("[%s] персонажей в мире %d (ждём %d), вне реестра %d"),
			Round, NChars, (int32)UE_ARRAY_COUNT(kPeers), NOrphans));
		if (!R.ShotDir.IsEmpty() && GEngine)
		{
			const FString Path = R.ShotDir / FString::Printf(TEXT("relog_%s"), Round);
			GEngine->Exec(Wd, *FString::Printf(TEXT("asda.UI.Shot %s"), *Path));
		}
	}

	// Подготовка мира после травела: id карты (сетка высот, AoI), свой 4003, затем соседи и моб — в порядке живого журнала.
	static void AddWorldEntry(TSharedRef<FRun> R, const TCHAR* Round, bool bPeerBeforeSelf)
	{
		R->Add(6.0f, [](FRun& Rn)
		{
			Rn.Exec(TEXT("Asda.Load.CurrentMap 3"));
			// Свой charId известен с ответа 1017 (выбор персонажа) — ДО любого пакета игрового сервера.
			if (UAsdaNetSubsystem* Net = Rn.Sub<UAsdaNetSubsystem>()) { Net->SelectedCharId = kSelfCharId; }
		});
		R->Add(0.5f, [bPeerBeforeSelf](FRun& Rn)
		{
			// Живой журнал перезахода: 4005 соседа может опередить свой 4003 (сосед с номером 3 — первым).
			if (bPeerBeforeSelf) { FeedPeers(Rn, 0, 1); }
			FeedSelf(Rn);
		});
		R->Add(1.5f, [bPeerBeforeSelf](FRun& Rn)
		{
			FeedPeers(Rn, bPeerBeforeSelf ? 1 : 0, UE_ARRAY_COUNT(kPeers));
			FeedMobAndTarget(Rn);
		});
		R->Add(1.0f, [](FRun& Rn) { TargetMob(Rn); });
		const FString RoundName(Round);
		R->Add(2.0f, [RoundName](FRun& Rn) { CheckRound(Rn, *RoundName); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaOverheadSelfTestRelog(
	TEXT("asda.Overhead.SelfTestRelog"),
	TEXT("asda.Overhead.SelfTestRelog [shotdir=<папка>] [quit] — ОФЛАЙН: плашки соседей (4005), своя 4003 и цели-моба (4013) до обрыва, ")
	TEXT("после обрыва → экран входа → вход и после смены канала (настоящие травелы UE); проверка живых виджетов имени"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		using namespace AsdaRelogPlateLocal;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет GameInstance")); return; }
		TSharedRef<FRun> R = MakeShared<FRun>();
		R->GI = GI;
		R->bQuit = KA.bQuit;
		for (const FString& Arg : KA.Pos) { if (Arg.StartsWith(TEXT("shotdir="))) { R->ShotDir = Arg.Mid(8).Replace(TEXT("\\"), TEXT("/")); } }
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>()) { R->SavedCharId = Net->SelectedCharId; R->SelfGuard.Capture(Net, kCmd); }

		// ── круг 1: обычный вход (сосед раньше своего 4003 — как в живом журнале) ──
		R->Add(0.2f, [](FRun& Rn)
		{
			if (UWorld* Wd = Rn.W()) { UGameplayStatics::OpenLevel(Wd, FName(kMap), true, TEXT("game=/Script/AsdaReborn.AsdaWorldGameMode")); }
		});
		AddWorldEntry(R, TEXT("1_вход"), true);
		// ── круг 2: обрыв → экран входа (ОК окна обрыва) → вход персонажа (1021) → карта ──
		R->Add(0.5f, [](FRun& Rn)
		{
			if (UAsdaSessionResetSubsystem* SR = Rn.Sub<UAsdaSessionResetSubsystem>()) { SR->Broadcast(EAsdaSessionResetReason::Disconnected); }
		});
		R->Add(1.0f, [](FRun& Rn)
		{
			if (UWorld* Wd = Rn.W()) { UGameplayStatics::OpenLevel(Wd, FName(TEXT("/Game/Map/Login"))); }
		});
		R->Add(8.0f, [](FRun& Rn)
		{
			if (UAsdaSessionResetSubsystem* SR = Rn.Sub<UAsdaSessionResetSubsystem>()) { SR->Broadcast(EAsdaSessionResetReason::EnteringWorld); }
			if (UWorld* Wd = Rn.W()) { UGameplayStatics::OpenLevel(Wd, FName(kMap), true, TEXT("game=/Script/AsdaReborn.AsdaWorldGameMode")); }
		});
		AddWorldEntry(R, TEXT("2_после_обрыва"), true);
		// ── круг 3: смена канала (та же карта заново, сброс MapChanged), обычный порядок 4003 → 4005 ──
		R->Add(0.5f, [](FRun& Rn)
		{
			if (UWorld* Wd = Rn.W()) { UGameplayStatics::OpenLevel(Wd, FName(kMap), true, TEXT("game=/Script/AsdaReborn.AsdaWorldGameMode")); }
		});
		AddWorldEntry(R, TEXT("3_смена_канала"), false);
		UE_LOG(LogAsdaRelogPlateTest, Log, TEXT("%s старт: 3 круга (вход / обрыв→логин→вход / смена канала), соседей %d, моб %d"),
			kPrefix, (int32)UE_ARRAY_COUNT(kPeers), kMobRid);
		R->Start();
	}));

#endif // !UE_BUILD_SHIPPING
