// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ТРАНСПОРТ ПОСЛЕ НАСТОЯЩЕЙ СМЕНЫ КАРТЫ» (05.11.2026): `asda.SelfTest.MountTravel [out=<папка>] [quit]`.
//   Отчёт игрока: «сменил карту верхом (портал, переход между зонами) — на новой карте персонаж без модели транспорта, а
//   состояние "верхом" и скорость остались». Соседний `asda.SelfTest.PossessMountPortal` эмулирует травел (уничтожение павна +
//   рассылка `MapChanged`); здесь — НАСТОЯЩИЙ травел UE путём игрока: `UAsdaLoadManagerSubsystem::BeginMapLoad` (уровень
//   L_Loading → async-загрузка целевой карты → `OpenLevel`), движок сносит мир, павн и виджеты, `PostLoadMapWithWorld` рассылает
//   сброс. Пакеты — штатным разбором входящих (`DebugFeedPacket`): 4003 (свой спавн), 5453 (посадка), 6071 (скорость),
//   4005 + 6154 (сосед), 1021 (снимок входа). Офлайн (`-AsdaNoNet`), кадры `asda_mounttravel_<шаг>.png` (копия в `out=`).
//   Ветки:
//     A) старт на карте мира: вход, посадка 5453 + 6071 0.56, сосед верхом (4005 + 6154) → кадр «до»;
//     P1) портал с НОВЫМ номером сессии: после травела 4003 → свой павн верхом, меш транспорта виден, скорость 0.56;
//         сосед новой карты: 4005 без транспорта — пеший (кэш прошлой карты не сажает), 6154 со слотом — верхом → кадр «после»;
//     P2) переход с ТЕМ ЖЕ номером (смена зоны на том же канал-сервере) → верхом;
//     P3) свой 4005 и свой 6154 без записи транспорта не снимают свой транспорт (`asda.Mount.SnapshotKeepsSelf`; у эталона
//         снимки только добавляют), а пустой 6154 соседа по-прежнему его спешивает; ключ 0 — свой пустой 6154 спешивал;
//     P4) ключ `asda.Mount.PortalReapply 0` → пеший после травела (воспроизведение исходного бага), ключ 1 обратно;
//     R) перезаход: 1021 со слотом 15 (маунт Fable) → травел → 4003 → верхом;
//     S) высадка 5454 после травела снимает транспорт (сквозной путь не сломан).
//   Итог: «ТРАНСПОРТ-ТРАВЕЛ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "AsdaTestPackets.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaWorldKind.h"
#include "Test/AsdaSelfTestKit.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMountTravelTest, Log, All);

namespace AsdaMountTravelTest
{
	static const TCHAR* kCmd    = TEXT("asda.SelfTest.MountTravel");   // имя команды
	static const TCHAR* kPrefix = TEXT("ТРАНСПОРТ-ТРАВЕЛ");            // префикс строк журнала
	static const TCHAR* kKey    = TEXT("asda.Mount.PortalReapply");    // ключ правки 30.10
	constexpr int32 kMapId   = 3;      // карта прогона (Альпия; есть в реестре карт и уровень UE)
	constexpr int32 kMeA     = 4321;   // свой номер сессии на старте
	constexpr int32 kMeB     = 4400;   // свой номер после портала
	constexpr int32 kPeer    = 77;     // номер соседа
	constexpr int32 kMount   = 522;    // оригинальный транспорт (special_itemlist SubType 15)
	constexpr int32 kMountF  = 559;    // маунт Fable (тоже SubType 15)
	constexpr float kSpeed   = 0.56f;  // скорость верхом (6071)

	// Шаг прогона: задержка перед ним, затем либо действие, либо ожидание условия (до таймаута).
	struct FStep
	{
		float Delay = 0.f;                         // пауза перед шагом, с
		float TimeoutS = 0.f;                      // > 0 — шаг-ожидание: ждать `Until` не дольше
		TFunction<bool(struct FRun&)> Until;       // условие ожидания
		TFunction<void(struct FRun&)> Fn;          // действие (или реакция на исход ожидания)
		FString Name;                              // имя шага для журнала
	};

	struct FRun : public TSharedFromThis<FRun>
	{
		TWeakObjectPtr<UGameInstance> GI;          // GameInstance переживает травел
		bool bQuit = false;                        // выйти по итогу
		FString OutDir;                            // папка копий кадров
		int32 Ok = 0, Fail = 0;                    // счётчики
		TArray<FStep> Steps;                       // шаги
		int32 Next = 0;                            // следующий шаг
		double DueAt = 0.0;                        // когда исполнять
		double WaitStart = -1.0;                   // начало ожидания текущего шага
		FString PendingShot;                       // кадр, ждущий копирования
		bool bWaitTimedOut = false;                // последнее ожидание вышло по таймауту
		int32 KeyWas = 1;                          // исходное значение ключа
		uint16 SelfNumWas = 0;                     // свой номер до прогона
		bool bSelfNumKnownWas = false;             // номер был известен
		FAsdaNetSelfScope SelfGuard;               // ★ R5-fix6 (13.11.2026): «свой» сети до прогона — возврат сторожем (в сводке пачки было «свой изменён»)

		UWorld* W() const { return GI.IsValid() ? GI->GetWorld() : nullptr; }
		template <class T> T* Sub() const { return GI.IsValid() ? GI->GetSubsystem<T>() : nullptr; }
		UAsdaNetSubsystem* Net() const { return Sub<UAsdaNetSubsystem>(); }
		UAsdaWorldSubsystem* WS() const { return Sub<UAsdaWorldSubsystem>(); }

		// Записать итог проверки.
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaMountTravelTest, Log,   TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaMountTravelTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
		void Exec(const TCHAR* C) const { if (UWorld* Wd = W()) { if (GEngine) { GEngine->Exec(Wd, C); } } }
		void Act(float Delay, const TCHAR* Name, TFunction<void(FRun&)> Fn) { FStep S; S.Delay = Delay; S.Fn = MoveTemp(Fn); S.Name = Name; Steps.Add(MoveTemp(S)); }
		void Wait(float Delay, float Timeout, const TCHAR* Name, TFunction<bool(FRun&)> Until, TFunction<void(FRun&)> Fn)
		{
			FStep S; S.Delay = Delay; S.TimeoutS = Timeout; S.Until = MoveTemp(Until); S.Fn = MoveTemp(Fn); S.Name = Name; Steps.Add(MoveTemp(S));
		}
		// Кадр текущего вьюпорта (копия в out= на следующем шаге — файл пишется кадром позже).
		void Shot(const TCHAR* Name)
		{
			CopyShot();
			PendingShot = FString::Printf(TEXT("asda_mounttravel_%s"), Name);
			FScreenshotRequest::RequestScreenshot(PendingShot, /*bShowUI*/ true, /*bAddUniqueSuffix*/ false);
		}
		void CopyShot()
		{
			if (PendingShot.IsEmpty()) { return; }
			const FString Src = FPaths::ScreenShotDir() / (PendingShot + TEXT(".png"));
			if (!OutDir.IsEmpty())
			{
				IFileManager::Get().MakeDirectory(*OutDir, true);
				const bool bCopied = IFileManager::Get().Copy(*(OutDir / (PendingShot + TEXT(".png"))), *Src) == COPY_OK;
				UE_LOG(LogAsdaMountTravelTest, Log, TEXT("%s кадр %s → %s: %s"), kPrefix, *Src, *OutDir, bCopied ? TEXT("скопирован") : TEXT("НЕ скопирован"));
			}
			else { UE_LOG(LogAsdaMountTravelTest, Log, TEXT("%s кадр %s"), kPrefix, *Src); }
			PendingShot.Reset();
		}
		void Start()
		{
			DueAt = FPlatformTime::Seconds() + (Steps.Num() ? Steps[0].Delay : 0.f);
			TSharedRef<FRun> Self = AsShared();
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self](float) { return Self->TickOnce(); }));
		}
		void Finish()
		{
			CopyShot();
			// Вернуть ключ и свой номер (мир прогона уже не тот, что на старте, — сторож ключей мира тут не годится).
			if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(kKey)) { V->Set(KeyWas, ECVF_SetByConsole); }
			if (UAsdaNetSubsystem* N = Net())
			{
				N->OnMountState.Broadcast(kPeer, 0, false);
				if (bSelfNumKnownWas) { N->SetSelfUserNumForDebug(SelfNumWas); }
			}
			SelfGuard.Restore();   // ★ R5-fix6: уровень 10 и прочее «своё» — как до прогона
			AsdaSelfTestKit::Finish(W(), kCmd, bQuit, LogAsdaMountTravelTest, kPrefix, Ok, Fail);
		}
		bool TickOnce()
		{
			const double Now = FPlatformTime::Seconds();
			if (Now < DueAt) { return true; }
			if (Next >= Steps.Num()) { Finish(); return false; }
			FStep& S = Steps[Next];
			if (S.TimeoutS > 0.f)
			{
				if (WaitStart < 0.0) { WaitStart = Now; }
				const bool bMet = S.Until && S.Until(*this);
				const bool bOut = (Now - WaitStart) > S.TimeoutS;
				if (!bMet && !bOut) { DueAt = Now + 0.1; return true; }
				bWaitTimedOut = !bMet;
				UE_LOG(LogAsdaMountTravelTest, Log, TEXT("%s шаг «%s»: %s за %.1f с"), kPrefix, *S.Name, bMet ? TEXT("готово") : TEXT("ТАЙМАУТ"), Now - WaitStart);
				WaitStart = -1.0;
			}
			TFunction<void(FRun&)> Fn = S.Fn;
			++Next;
			if (Fn) { Fn(*this); }
			if (Next >= Steps.Num()) { Finish(); return false; }
			DueAt = FPlatformTime::Seconds() + Steps[Next].Delay;
			return true;
		}
	};

	// ── кадры пакетов ──
	using AsdaTestPackets::Body4003;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPackets.h`) — копия была дословной.
	// 5453: i16 userIdx, i32 код, i32 срок (посадка).
	static TArray<uint8> Body5453(int32 Uidx, int32 Code)
	{
		TArray<uint8> B; B.SetNumZeroed(10);
		AsdaBytes::WrI16(B.GetData(), (int16)Uidx); AsdaBytes::WrI32(B.GetData() + 2, Code); AsdaBytes::WrI32(B.GetData() + 6, -1);
		return B;
	}
	// 5454: i16 userIdx, i32 код (снятие).
	static TArray<uint8> Body5454(int32 Uidx, int32 Code)
	{
		TArray<uint8> B; B.SetNumZeroed(6);
		AsdaBytes::WrI16(B.GetData(), (int16)Uidx); AsdaBytes::WrI32(B.GetData() + 2, Code);
		return B;
	}
	// 6071: i16 userIdx, i32 id, f32 скорость.
	static TArray<uint8> Body6071(int32 Uidx, float Speed)
	{
		TArray<uint8> B; B.SetNumZeroed(10);
		AsdaBytes::WrI16(B.GetData(), (int16)Uidx); AsdaBytes::WrI32(B.GetData() + 2, 990000 + Uidx); FMemory::Memcpy(B.GetData() + 6, &Speed, 4);
		return B;
	}
	// 6154: u16 userIdx + записи по 12 б (type 2: код, срок −1, величина 80). Code 0 — пустой снимок (записей нет).
	static TArray<uint8> Body6154(int32 Uidx, int32 Code)
	{
		TArray<uint8> B; B.SetNumZeroed(Code > 0 ? 14 : 2);
		AsdaBytes::WrI16(B.GetData(), (int16)Uidx);
		if (Code > 0) { B[2] = 2; AsdaBytes::WrI32(B.GetData() + 4, Code); AsdaBytes::WrI32(B.GetData() + 8, -1); AsdaBytes::WrI16(B.GetData() + 12, 80); }
		return B;
	}
	// 4005 (151 б): игрок у позиции, слот экипировки 10 (транспорт) = MountCode.
	static TArray<uint8> Body4005(int32 Uidx, const char* Name, int32 MountCode, float GameX, float GameZ)
	{
		TArray<uint8> B; B.SetNumZeroed(151);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)Uidx);
		B[6] = 1;
		AsdaBytes::WrI32(B.GetData() + 11, 990000 + Uidx);
		FMemory::Memcpy(B.GetData() + 15, Name, FCStringAnsi::Strlen(Name));
		B[37] = 1; B[38] = 1; B[40] = 10;
		FMemory::Memcpy(B.GetData() + 56, &GameX, 4);
		FMemory::Memcpy(B.GetData() + 60, &GameZ, 4);
		for (int32 i = 0; i < 11; ++i) { AsdaBytes::WrI32(B.GetData() + 96 + 5 * i, -1); }
		if (MountCode > 0) { AsdaBytes::WrI32(B.GetData() + 96 + 5 * 10, MountCode); }
		return B;
	}
	// 1021 (686 б): снимок входа; слот 15 функциональных предметов = транспорт `Code`, прочие пусты (−1, 0, −1).
	static TArray<uint8> Body1021(int32 Code)
	{
		TArray<uint8> B; B.SetNumZeroed(686);
		for (int32 i = 0; i < 28; ++i) { AsdaBytes::WrI16(B.GetData() + 32 + 12 * i, -1); }
		for (int32 k = 0; k < 30; ++k)
		{
			uint8* P = B.GetData() + 368 + 10 * k;
			AsdaBytes::WrI32(P, k == 15 ? Code : -1); AsdaBytes::WrI32(P + 4, k == 15 ? 3600 : 0); AsdaBytes::WrI16(P + 8, k == 15 ? 80 : -1);
		}
		AsdaBytes::WrI16(B.GetData() + 26, (int16)kMapId);
		return B;
	}

	// ── состояние мира ──
	static AAsdaPlayerCharacter* SelfPawn(const FRun& R)
	{
		UAsdaWorldSubsystem* WS = R.WS();
		return (WS && IsValid(WS->LocalPlayerActor)) ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get()) : nullptr;
	}
	static AAsdaPlayerCharacter* PeerPawn(const FRun& R, int32 Uidx)
	{
		UAsdaWorldSubsystem* WS = R.WS();
		return WS ? Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(Uidx)) : nullptr;
	}
	// Модель транспорта ВИДНА: признак «верхом», компонент меша зарегистрирован, меш задан, не скрыт, в мире текущей карты.
	static FString MountVisual(const AAsdaPlayerCharacter* P, const UWorld* W)
	{
		if (!P) { return TEXT("павна нет"); }
		if (P->GetWorld() != W) { return TEXT("павн из прошлого мира"); }
		const USkeletalMeshComponent* M = P->MountMesh;
		return FString::Printf(TEXT("верхом %d, компонент %s, меш %s, зарегистрирован %d, виден %d, скрыт %d"),
			P->IsMounted() ? 1 : 0, M ? TEXT("есть") : TEXT("НЕТ"),
			(M && M->GetSkeletalMeshAsset()) ? *M->GetSkeletalMeshAsset()->GetName() : TEXT("нет"),
			(M && M->IsRegistered()) ? 1 : 0, (M && M->IsVisible()) ? 1 : 0, (M && M->bHiddenInGame) ? 1 : 0);
	}
	static bool MountShown(const AAsdaPlayerCharacter* P, const UWorld* W)
	{
		const USkeletalMeshComponent* M = P ? P->MountMesh.Get() : nullptr;
		return P && P->GetWorld() == W && P->IsMounted() && M && M->IsRegistered() && M->GetSkeletalMeshAsset() && M->IsVisible() && !M->bHiddenInGame;
	}

	// Настоящий травел путём игрока: менеджер загрузки (L_Loading → карта), ждать, пока целевая карта встанет.
	static void AddTravel(FRun& Run, const TCHAR* Tag)
	{
		Run.Act(0.3f, *FString::Printf(TEXT("травел %s"), Tag), [](FRun& R)
		{
			UWorld* Old = R.W();
			R.CopyShot();
			UE_LOG(LogAsdaMountTravelTest, Log, TEXT("%s травел: карта %d с мира «%s»"), kPrefix, kMapId, Old ? *Old->GetName() : TEXT("-"));
			if (UAsdaLoadManagerSubsystem* L = R.Sub<UAsdaLoadManagerSubsystem>()) { L->BeginMapLoad(kMapId); }
			else { R.Check(false, TEXT("нет менеджера загрузки")); }
		});
		// Сначала уход с прежнего мира на экран загрузки, затем целевая карта.
		Run.Wait(0.2f, 20.f, TEXT("экран загрузки"), [](FRun& R) { const UWorld* Wd = R.W(); return Wd && AsdaWorldKind::IsLoadingMap(Wd); }, [](FRun&) {});
		Run.Wait(0.2f, 60.f, TEXT("целевая карта"), [](FRun& R)
		{
			const UWorld* Wd = R.W();
			return Wd && !AsdaWorldKind::IsLoadingMap(Wd) && !AsdaWorldKind::IsLoginScene(Wd) && Wd->HasBegunPlay();
		}, [Tag = FString(Tag)](FRun& R)
		{
			R.Check(!R.bWaitTimedOut, FString::Printf(TEXT("%s: травел завершён, мир «%s»"), *Tag, R.W() ? *R.W()->GetName() : TEXT("-")));
			R.Exec(TEXT("Asda.Load.CurrentMap 3"));
		});
	}

	static void Build(FRun& Run)
	{
		// ═══ A. Старт: свой вход, посадка, скорость, сосед верхом ═══
		Run.Act(0.5f, TEXT("A"), [](FRun& R)
		{
			UAsdaNetSubsystem* N = R.Net();
			R.Exec(TEXT("Asda.Load.CurrentMap 3"));
			N->DebugFeedPacket(4003, Body4003(kMeA));
			AAsdaPlayerCharacter* Me = SelfPawn(R);
			R.Check(Me != nullptr, FString::Printf(TEXT("A1 4003: свой павн (номер %d)"), kMeA));
			N->DebugFeedPacket(5453, Body5453(kMeA, kMount));
			N->DebugFeedPacket(6071, Body6071(kMeA, kSpeed));
			R.Check(MountShown(Me, R.W()), FString::Printf(TEXT("A2 5453 [%d]: свой павн верхом, модель видна (%s)"), kMount, *MountVisual(Me, R.W())));
			// Стоя 6071 правит только кэш скорости своего хода (шаг не идёт) — разбор у приёма 6071.
			R.Check(FMath::IsNearlyEqual(N->LastServerSpeed, kSpeed, 0.001f), FString::Printf(TEXT("A3 6071 стоя: кэш скорости своего хода %.4f (ждём %.2f)"), N->LastServerSpeed, kSpeed));
			N->DebugFeedPacket(4005, Body4005(kPeer, "MountPeer", kMount, 25650.f, 25650.f));
			N->DebugFeedPacket(6154, Body6154(kPeer, kMount));
			AAsdaPlayerCharacter* P = PeerPawn(R, kPeer);
			R.Check(MountShown(P, R.W()), FString::Printf(TEXT("A4 сосед %d (4005 слот 10 + 6154): верхом, модель видна (%s)"), kPeer, *MountVisual(P, R.W())));
		});
		Run.Act(0.5f, TEXT("кадр A"), [](FRun& R) { R.Shot(TEXT("A_before")); });

		// ═══ P1. Портал с новым номером ═══
		AddTravel(Run, TEXT("P1"));
		Run.Act(0.5f, TEXT("P1"), [](FRun& R)
		{
			UAsdaNetSubsystem* N = R.Net();
			R.Check(SelfPawn(R) == nullptr, TEXT("P1a до 4003 нового мира своего павна нет (прежний снесён травелом)"));
			N->DebugFeedPacket(4003, Body4003(kMeB));
			AAsdaPlayerCharacter* Me = SelfPawn(R);
			R.Check(MountShown(Me, R.W()), FString::Printf(TEXT("P1b портал (номер %d → %d): свой павн верхом, модель видна (%s)"), kMeA, kMeB, *MountVisual(Me, R.W())));
			// Сервер при портале транспорт не снимает (`CharPortalMake` 6071 не шлёт) — скорость верхом остаётся, как у эталона.
			R.Check(FMath::IsNearlyEqual(N->LastServerSpeed, kSpeed, 0.001f), FString::Printf(TEXT("P1s после травела кэш скорости хода %.4f (верхом, ждём %.2f)"), N->LastServerSpeed, kSpeed));
			// Сосед новой карты с тем же номером, что сосед прошлой: появился пешим — кэш прошлой карты не должен его сажать.
			N->DebugFeedPacket(4005, Body4005(kPeer, "WalkPeer", 0, 25650.f, 25650.f));
			AAsdaPlayerCharacter* P = PeerPawn(R, kPeer);
			R.Check(P && !P->IsMounted(), FString::Printf(TEXT("P1c сосед новой карты (4005 без транспорта): пеший (%s)"), *MountVisual(P, R.W())));
			N->DebugFeedPacket(6154, Body6154(kPeer, kMountF));
			R.Check(MountShown(P, R.W()), FString::Printf(TEXT("P1d 6154 [%d]: сосед верхом, модель видна (%s)"), kMountF, *MountVisual(P, R.W())));
		});
		Run.Act(4.0f, TEXT("кадр P1"), [](FRun& R) { R.Shot(TEXT("P1_after")); });
		Run.Act(0.5f, TEXT("P1 стоит"), [](FRun& R)
		{
			AAsdaPlayerCharacter* Me = SelfPawn(R);
			R.Check(MountShown(Me, R.W()), FString::Printf(TEXT("P1e через 4.5 с после 4003 (экран загрузки снят) модель на месте (%s)"), *MountVisual(Me, R.W())));
		});

		// ═══ P2. Переход с тем же номером ═══
		AddTravel(Run, TEXT("P2"));
		Run.Act(0.5f, TEXT("P2"), [](FRun& R)
		{
			R.Net()->DebugFeedPacket(4003, Body4003(kMeB));
			AAsdaPlayerCharacter* Me = SelfPawn(R);
			R.Check(MountShown(Me, R.W()), FString::Printf(TEXT("P2 переход с тем же номером %d: верхом, модель видна (%s)"), kMeB, *MountVisual(Me, R.W())));
		});

		// ═══ P3. Свои 4005 / 6154 после портала ═══
		Run.Act(0.3f, TEXT("P3"), [](FRun& R)
		{
			UAsdaNetSubsystem* N = R.Net();
			N->DebugFeedPacket(4005, Body4005(kMeB, "Self", 0, 25650.f, 25650.f));
			AAsdaPlayerCharacter* Me = SelfPawn(R);
			R.Check(MountShown(Me, R.W()), FString::Printf(TEXT("P3a свой 4005 (слот 10 пуст): транспорт на месте (%s)"), *MountVisual(Me, R.W())));
			N->DebugFeedPacket(6154, Body6154(kMeB, 0));
			R.Check(MountShown(Me, R.W()), FString::Printf(TEXT("P3b свой 6154 без записей type 2: транспорт на месте (%s)"), *MountVisual(Me, R.W())));
			// Сосед с пустым по транспорту снимком по-прежнему спешивается (снятие стейл-кэша соседа не сломано).
			N->DebugFeedPacket(4005, Body4005(kPeer, "MountPeer", kMount, 25650.f, 25650.f));   // сосед этой карты верхом
			N->DebugFeedPacket(6154, Body6154(kPeer, 0));
			AAsdaPlayerCharacter* P = PeerPawn(R, kPeer);
			R.Check(P && !P->IsMounted(), FString::Printf(TEXT("P3c 6154 соседа без записей type 2: сосед спешен (%s)"), *MountVisual(P, R.W())));
			// Ключ 0 — прежнее поведение: свой пустой снимок спешивал (воспроизведение дыры), затем вернуть транспорт 5453.
			IConsoleVariable* K = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Mount.SnapshotKeepsSelf"));
			if (K) { K->Set(0, ECVF_SetByConsole); }
			N->DebugFeedPacket(6154, Body6154(kMeB, 0));
			R.Check(K && Me && !Me->IsMounted(), FString::Printf(TEXT("P3d ключ asda.Mount.SnapshotKeepsSelf 0: свой пустой 6154 спешил — дыра воспроизведена (%s)"), *MountVisual(Me, R.W())));
			if (K) { K->Set(1, ECVF_SetByConsole); }
			N->DebugFeedPacket(5453, Body5453(kMeB, kMount));
			R.Check(MountShown(Me, R.W()), FString::Printf(TEXT("P3e снова верхом (5453) (%s)"), *MountVisual(Me, R.W())));
		});
		Run.Act(0.3f, TEXT("кадр P3"), [](FRun& R) { R.Shot(TEXT("P3_self_snapshots")); });

		// ═══ P4. Ключ 0 — воспроизведение исходного бага ═══
		Run.Act(0.3f, TEXT("P4 ключ 0"), [](FRun& R) { if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(kKey)) { V->Set(0, ECVF_SetByConsole); } });
		AddTravel(Run, TEXT("P4"));
		Run.Act(0.5f, TEXT("P4"), [](FRun& R)
		{
			R.Net()->DebugFeedPacket(4003, Body4003(kMeA));
			AAsdaPlayerCharacter* Me = SelfPawn(R);
			R.Check(Me && !Me->IsMounted(), FString::Printf(TEXT("P4 ключ %s 0: после травела пеший — исходный баг воспроизведён (%s)"), kKey, *MountVisual(Me, R.W())));
			if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(kKey)) { V->Set(1, ECVF_SetByConsole); }
			// Состояние «верхом» в списке осталось — вернуть транспорт для следующих веток тем же путём, что сервер (5453).
			R.Net()->DebugFeedPacket(5453, Body5453(kMeA, kMount));
		});

		// ═══ R. Перезаход: 1021 со слотом 15 → травел → 4003 ═══
		Run.Act(0.3f, TEXT("R 1021"), [](FRun& R)
		{
			UAsdaNetSubsystem* N = R.Net();
			N->DebugFeedPacket(5454, Body5454(kMeA, kMount));   // прежний персонаж спешен
			N->DebugFeedPacket(1021, Body1021(kMountF));        // снимок нового входа: верхом на маунте Fable
		});
		AddTravel(Run, TEXT("R"));
		Run.Act(0.5f, TEXT("R"), [](FRun& R)
		{
			R.Net()->DebugFeedPacket(4003, Body4003(kMeB));
			AAsdaPlayerCharacter* Me = SelfPawn(R);
			R.Check(MountShown(Me, R.W()), FString::Printf(TEXT("R перезаход (1021 слот 15 = %d): верхом, модель видна (%s)"), kMountF, *MountVisual(Me, R.W())));
		});
		Run.Act(4.0f, TEXT("кадр R"), [](FRun& R) { R.Shot(TEXT("R_relog")); });

		// ═══ S. Высадка после травела ═══
		Run.Act(0.5f, TEXT("S"), [](FRun& R)
		{
			R.Net()->DebugFeedPacket(5454, Body5454(kMeB, kMountF));
			AAsdaPlayerCharacter* Me = SelfPawn(R);
			R.Check(Me && !Me->IsMounted(), FString::Printf(TEXT("S 5454 после травела: спешен (%s)"), *MountVisual(Me, R.W())));
		});
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestMountTravel(
	TEXT("asda.SelfTest.MountTravel"),
	TEXT("Самотест транспорта после НАСТОЯЩЕЙ смены карты (менеджер загрузки, OpenLevel): свой павн и сосед, портал с новым и тем же "
	     "номером, свои 4005/6154, ключ asda.Mount.PortalReapply 0, перезаход 1021. `out=<папка>` для кадров, `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaMountTravelTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!Net || !GI->GetSubsystem<UAsdaWorldSubsystem>() || !GI->GetSubsystem<UAsdaLoadManagerSubsystem>())
		{
			AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем сети/мира/загрузки"));
			return;
		}
		TSharedRef<FRun> R = MakeShared<FRun>();
		R->GI = GI;
		R->bQuit = KA.bQuit;
		for (const FString& A : KA.Pos) { if (A.StartsWith(TEXT("out="))) { R->OutDir = A.Mid(4); } }
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(kKey)) { R->KeyWas = V->GetInt(); V->Set(1, ECVF_SetByConsole); }
		R->SelfNumWas = Net->SelfUserNum;
		R->bSelfNumKnownWas = Net->IsSelfUserNumKnown();
		R->SelfGuard.Capture(Net, kCmd);
		Build(*R);
		UE_LOG(LogAsdaMountTravelTest, Log, TEXT("%s старт: шагов %d, карта %d, транспорт %d / %d"), kPrefix, R->Steps.Num(), kMapId, kMount, kMountF);
		R->Start();
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
