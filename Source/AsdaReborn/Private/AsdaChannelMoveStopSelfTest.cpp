// Copyright (c) 2026 @sitxovski (Discord)
// ★★ САМОТЕСТ «СМЕНА КАНАЛА ОСТАНАВЛИВАЕТ ХОД» (08.11.2026, пачка 16, баг 2: «при смене канала персонаж продолжает идти»). [ИСПРАВЛЕНИЕ]
//
// Разбор и правка — у `AsdaSelfMoveHeldForChannel` (UI/AsdaMinimapSubsystem.cpp, ключ `asda.Minimap.ChannelMoveStop`).
// Путь игрока: кнопка 17500 над мини-картой → строка «2 CH» (OnClicked, тот же делегат, что зовёт Slate) → `SelectChannel` → 6084.
//   Отправки перехвачены (в сеть ничего не уходит), «в игре» включено на время прогона. Клавиши — `InputKey` (симуляция нажатия
//   через ввод контроллера и раскладку), клик по земле — `RequestMoveTo` (общая точка клика по земле и мини-карте).
//
// ЧТО ПРОВЕРЯЕТ:
//   K. Идёт кликом по земле → клик «2 CH»: 4008 ушёл РАНЬШЕ 6084, персонаж стоит, маршрута нет, общая точка остановки взведена;
//      через 0,5 с позиция та же; опоздавший грант 4007 на прежний 4006 персонажа не уводит.
//   W. Под удержанием зажата W — хода и 4006 нет.
//   E. Удержание ожидания ответа истекло (сервер промолчал) — W снова ведёт персонажа (замка навсегда нет).
//   A. Межкартовый автопуть взведён → клик «2 CH»: заявка снята, персонаж стоит.
//   M. Верхом (транспорт 522) кликом по земле → клик «2 CH»: стоп, 4008 раньше 6084.
//   C. Контроль ключом asda.Minimap.ChannelMoveStop 0 — персонаж идёт дальше, 4008 нет (воспроизведение бага).
//   T. Ответ 6015 (переход идёт, `bTransferInProgress`) — зажатая W персонажа не ведёт, 4006 нет.
//   Кадры — Reports/batch16/channel/<ru|en>_*.png.
//
// Консоль: `asda.SelfTest.ChannelMoveStop [delay=N] [quit]`. Только офлайн. Строки — «КАНАЛ-СТОП», итог — «КАНАЛ-СТОП ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Test/AsdaSelfTestWorldScope.h"
#include "Text/AsdaLang.h"
#include "UI/AsdaMinimapSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"   // R5-fix6: снятие окон-сообщений предыдущих самотестов пачки
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaChannelStopTest, Log, All);

// UI/AsdaMinimapSubsystem.cpp — удержание хода сменой канала (снять ожидание ответа между разделами).
void AsdaClearChannelMoveHold();

namespace AsdaChannelStopTest
{
	static const TCHAR* const kCmd = TEXT("asda.SelfTest.ChannelMoveStop");
	static const TCHAR* const kPrefix = TEXT("КАНАЛ-СТОП");
	static constexpr uint16 kDebugSelfNum = 41;   // свой номер офлайн, если 4003 не приходил
	static constexpr int32 kMountCode = 522;      // транспорт (subType 15)
	static constexpr float kRunSpeed = 0.35f;     // пешком (юнит/мс)

	// 6015 (26 б): IP[16], порт +16, клетка +18/+20, карта +22, канал +24, параметр +25 (как у asda.SelfTest.ChannelMove).
	static TArray<uint8> Body6015(int32 Map, uint8 Channel)
	{
		TArray<uint8> B; B.SetNumZeroed(26);
		const char* Ip = "@@ASTORY_SERVER_WAN_IP@@";
		for (int32 i = 0; Ip[i]; ++i) { B[i] = (uint8)Ip[i]; }
		B[16] = 0xF1; B[17] = 0x3C;
		B[18] = 0x89; B[19] = 0x01; B[20] = 0x8D; B[21] = 0x01;
		B[22] = (uint8)(Map & 0xFF); B[23] = (uint8)(Map >> 8);
		B[24] = Channel; B[25] = 0;
		return B;
	}
}

struct FAsdaChannelStopSelfTest
{
	struct FSent { FVector2D Cur, Dst; };
	struct FRun
	{
		TWeakObjectPtr<UWorld> W;
		TWeakObjectPtr<AAsdaPlayerController> PC;
		TWeakObjectPtr<AAsdaPlayerCharacter> Me;
		bool bQuit = false;
		int32 Ok = 0, Fail = 0;
		TUniquePtr<FAsdaWorldSelfScope> WorldScope;
		TUniquePtr<FAsdaCVarScope> Keys;
		uint32 CapGen = 0;
		TArray<uint16> Ops;          // порядок перехваченных 4006/4008/6084
		TArray<FSent> Sent4006;      // точки перехваченных 4006
		int32 WaitTries = 0;
		int32 Self = 0;
		FString Lang, ShotDir;
		FVector2D Origin = FVector2D::ZeroVector, Hold = FVector2D::ZeroVector;
		// Снимок сети.
		int32 Map = -1, ChCount = 0, Ch = 0, Port = 0, TX = -1, TZ = -1, PTX = -1, PTZ = -1;
		FString Ip;
		bool bTransfer = false, bPending = false, bPortal = false, bInGame = false;
		uint8 Param = 0xFF;
		float Speed0 = 0.35f;        // скорость сервера до прогона
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaChannelStopTest, Log, TEXT("КАНАЛ-СТОП ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaChannelStopTest, Error, TEXT("КАНАЛ-СТОП FAIL %s"), *What); }
		}
		int32 Count(uint16 Op) const { int32 N = 0; for (uint16 O : Ops) { N += (O == Op); } return N; }
		int32 IndexOf(uint16 Op) const { return Ops.IndexOfByKey(Op); }
	};
	using FR = TSharedRef<FRun>;

	static UAsdaNetSubsystem* NetOf(const FRun& R) { UWorld* W = R.W.Get(); return W ? W->GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr; }
	static UAsdaMinimapSubsystem* MiniOf(const FRun& R) { UWorld* W = R.W.Get(); return W ? W->GetGameInstance()->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr; }

	static void After(const FR& R, float Sec, TFunction<void(const FR&)> Fn)
	{
		UWorld* W = R->W.Get();
		if (!W) { return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([R, Fn = MoveTemp(Fn)]() { Fn(R); }), Sec, false);
	}
	static UUserWidget* FindMini(const UWorld* W)
	{
		for (TObjectIterator<UUserWidget> It; It; ++It)
		{
			UUserWidget* U = *It;
			if (U && U->GetWorld() == W && U->IsInViewport() && U->WidgetTree && U->WidgetTree->FindWidget(FName(TEXT("Change_Channel_Btn")))) { return U; }
		}
		return nullptr;
	}
	static bool Click(const FRun& R, const TCHAR* Name)
	{
		UUserWidget* M = FindMini(R.W.Get());
		UButton* B = (M && M->WidgetTree) ? Cast<UButton>(M->WidgetTree->FindWidget(FName(Name))) : nullptr;
		if (!B) { return false; }
		B->OnClicked.Broadcast();
		return true;
	}
	// Путь игрока: кнопка 17500 раскрывает панель, клик по строке «2 CH».
	static bool ClickChannel2(const FRun& R)
	{
		if (UAsdaMinimapSubsystem* Mini = MiniOf(R)) { Mini->SetChannelPanelOpen(false); }
		const bool bBtn = Click(R, TEXT("Change_Channel_Btn"));
		const bool bRow = Click(R, TEXT("Change_Channel01_Click"));
		return bBtn && bRow;
	}
	static void Key(const FRun& R, const FKey& K, bool bDown)
	{
		if (AAsdaPlayerController* PC = R.PC.Get())
		{
			static_cast<APlayerController*>(PC)->InputKey(FInputKeyEventArgs::CreateSimulated(K, bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f));
		}
	}
	static void Shot(const FRun& R, const TCHAR* Name)
	{
		const FString File = FPaths::Combine(R.ShotDir, FString::Printf(TEXT("%s_%s.png"), *R.Lang, Name));
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaChannelStopTest, Log, TEXT("КАНАЛ-СТОП кадр: %s"), *File);
	}
	// Грант сервера на последний перехваченный 4006 (тот же делегат, что пакет 4007).
	static void Grant(const FRun& R)
	{
		UAsdaNetSubsystem* Net = NetOf(R);
		if (!Net || !R.Sent4006.Num()) { return; }
		const FSent X = R.Sent4006.Last();
		Net->OnEntityMove.Broadcast(R.Self, 1, X.Cur, X.Dst, Net->LastServerSpeed, false, 2, -1);
	}
	// Начать ход кликом по земле (общая точка клика); если ход не начался локально — грант сервера; в крайнем случае ход «в точку»
	//   напрямую (как самотест высадки) — проверяется не путь старта, а остановка.
	static void StartWalk(const FR& R, const FVector2D& Dst, TFunction<void(const FR&)> Then)
	{
		AAsdaPlayerController* PC = R->PC.Get();
		if (!PC) { Then(R); return; }
		const int32 N0 = R->Sent4006.Num();
		PC->RequestMoveTo(Dst);
		After(R, 0.15f, [N0, Dst, Then](const FR& R)
		{
			AAsdaPlayerCharacter* Me = R->Me.Get();
			UAsdaNetSubsystem* Net = NetOf(*R);
			if (Me && !Me->IsMoving() && R->Sent4006.Num() > N0) { Grant(*R); }
			if (Me && Net && !Me->IsMoving())
			{
				UE_LOG(LogAsdaChannelStopTest, Warning, TEXT("КАНАЛ-СТОП клик по земле хода не дал (4006 %d) — ход «в точку» напрямую"), R->Sent4006.Num() - N0);
				Me->NetMoveFromActor(Dst, Net->LastServerSpeed);
				Net->bActionPending = true;
			}
			After(R, 0.25f, Then);
		});
	}

	static void Start(UWorld* W, bool bQuit);
	static void WaitMini(const FR& R);
	static void Begin(const FR& R);
	static void StepK(const FR& R);
	static void StepW(const FR& R);
	static void StepE(const FR& R);
	static void StepA(const FR& R);
	static void StepM(const FR& R);
	static void StepC(const FR& R);
	static void StepT(const FR& R);
	static void Finish(const FR& R);
};

void FAsdaChannelStopSelfTest::Start(UWorld* W, bool bQuit)
{
	FR R = MakeShared<FRun>();
	R->W = W; R->bQuit = bQuit;
	UAsdaNetSubsystem* Net = NetOf(*R);
	if (!Net) { AsdaSelfTestKit::Refused(W, AsdaChannelStopTest::kCmd, bQuit, TEXT("нет сетевой подсистемы")); return; }
	if (Net->IsConnected()) { AsdaSelfTestKit::Refused(W, AsdaChannelStopTest::kCmd, bQuit, TEXT("живое соединение")); return; }
	if (AsdaSelfTestKit::NeedSelfSpawn(W) && GEngine)
	{
		GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
		GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
	}
	WaitMini(R);
}

void FAsdaChannelStopSelfTest::WaitMini(const FR& R)
{
	UWorld* W = R->W.Get();
	if (!W) { return; }
	AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W->GetFirstPlayerController());
	if (!FindMini(W) || !PC || !Cast<AAsdaPlayerCharacter>(PC->GetPawn()))
	{
		if (++R->WaitTries > 40) { AsdaSelfTestKit::Refused(W, AsdaChannelStopTest::kCmd, R->bQuit, TEXT("нет окна мини-карты или своего персонажа за 20 с")); return; }
		After(R, 0.5f, [](const FR& R) { WaitMini(R); });
		return;
	}
	Begin(R);
}

void FAsdaChannelStopSelfTest::Begin(const FR& R)
{
	UWorld* W = R->W.Get();
	UAsdaNetSubsystem* Net = NetOf(*R);
	AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W->GetFirstPlayerController());
	AAsdaPlayerCharacter* Me = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
	if (!Net || !PC || !Me) { return; }
	R->PC = PC; R->Me = Me;
	R->Lang = FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en");
	R->ShotDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/batch16/channel")));
	R->WorldScope = MakeUnique<FAsdaWorldSelfScope>(W, AsdaChannelStopTest::kCmd);
	R->Keys = MakeUnique<FAsdaCVarScope>(W, AsdaChannelStopTest::kCmd);
	R->Keys->Set(TEXT("asda.Minimap.ChannelGates"), 1);
	R->Keys->Set(TEXT("asda.Minimap.ChannelMoveStop"), 1);
	R->Keys->Set(TEXT("asda.Minimap.ChannelMoveStopSec"), 30.f);   // разделы K/W не зависят от подвисаний кадров (скриншот); E ставит свой срок
	R->Keys->Set(TEXT("asda.Net.ChannelTransferPortalLogon"), 1);
	R->Map = Net->EnterMapId; R->ChCount = Net->ChannelCount; R->Ch = Net->CurrentChannel; R->Ip = Net->GameServerIP; R->Port = Net->GameServerPort;
	R->TX = Net->SelfTileX; R->TZ = Net->SelfTileZ; R->PTX = Net->PortalTileX; R->PTZ = Net->PortalTileZ; R->Param = Net->PortalChannelParam;
	R->bTransfer = Net->bTransferInProgress; R->bPending = Net->bPendingEnterWorld; R->bPortal = Net->bPortalPending; R->bInGame = Net->bInGameConnected;
	R->Speed0 = Net->LastServerSpeed;
	TWeakPtr<FRun> WR = R;
	R->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WR](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
	{
		TSharedPtr<FRun> S = WR.Pin();
		if (!S) { return; }
		if (Op == 4006 || Op == 4008 || Op == 6084) { S->Ops.Add(Op); }
		if (Op == 4006 && P && Len >= 37)
		{
			FSent X;
			X.Cur = FVector2D(AsdaBytes::RdF32(P + 16 + 5), AsdaBytes::RdF32(P + 16 + 9));
			X.Dst = FVector2D(AsdaBytes::RdF32(P + 16 + 13), AsdaBytes::RdF32(P + 16 + 17));
			S->Sent4006.Add(X);
		}
	}, 0, AsdaChannelStopTest::kCmd);
	if (R->CapGen == 0) { R->Expect(false, TEXT("перехват отправок занят другим прогоном")); Finish(R); return; }
	if (!Net->IsSelfUserNumKnown()) { Net->SetSelfUserNumForDebug(AsdaChannelStopTest::kDebugSelfNum); }
	R->Self = (int32)Net->SelfUserNum;
	Net->bInGameConnected = true;   // путь «в игре» (отправки перехвачены)
	Net->LastServerSpeed = AsdaChannelStopTest::kRunSpeed;
	Net->ChannelCount = 3; Net->CurrentChannel = 0; Net->EnterMapId = 3;
	Me->StopMove(); Me->SetAnimState(EAsdaPlayerState::Idle);
	R->Origin = Me->GetGamePosXZ();
	AsdaClearChannelMoveHold();
	// ★ R5-fix6 (13.11.2026): своё исходное состояние — окна-сообщения предыдущих самотестов пачки сняты. Оставшееся модальное окно
	//   (после `asda.SelfTest.CashGift` висел WBP_ONEBUT_MSGBOX) держит ввод у себя: зажатая W до мира не доходит, и шаг E2 краснел
	//   («идёт 0, сдвиг 0.0, 4006 0») — только в пачке, на RU; на EN и в одиночку зелёный. Проверки и допуски прежние.
	if (UGameInstance* GIc = W->GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WMc = GIc->GetSubsystem<UAsdaWindowSubsystem>())
		{
			const bool bWasModal = WMc->IsAnyModalOpen();
			WMc->ClearMsgBoxQueue(AsdaChannelStopTest::kCmd);
			UE_LOG(LogAsdaChannelStopTest, Log, TEXT("КАНАЛ-СТОП исходное состояние: модальное окно до прогона %s, после снятия очереди сообщений %s"),
				bWasModal ? TEXT("БЫЛО (осталось от предыдущего самотеста)") : TEXT("нет"), WMc->IsAnyModalOpen() ? TEXT("ещё есть") : TEXT("нет"));
		}
	}
	StepK(R);
}

// K. Клик по земле → идёт → клик «2 CH».
void FAsdaChannelStopSelfTest::StepK(const FR& R)
{
	StartWalk(R, R->Origin + FVector2D(900.f, 0.f), [](const FR& R)
	{
		AAsdaPlayerCharacter* Me = R->Me.Get(); AAsdaPlayerController* PC = R->PC.Get();
		if (!Me || !PC) { Finish(R); return; }
		R->Expect(Me->IsMoving(), TEXT("K0 клик по земле: персонаж идёт"));
		Shot(*R, TEXT("k0_walking"));
		R->Ops.Reset();
		const bool bClick = ClickChannel2(*R);
		const int32 I8 = R->IndexOf(4008), I6 = R->IndexOf(6084);
		R->Expect(bClick && I6 >= 0 && I8 >= 0 && I8 < I6,
			FString::Printf(TEXT("K1 клик 17500 → «2 CH»: 4008 ушёл раньше 6084 (4008 №%d, 6084 №%d, клик=%d)"), I8, I6, (int32)bClick));
		R->Expect(!Me->IsMoving() && !PC->HasRoute() && PC->IsStunHardStopped(),
			FString::Printf(TEXT("K2 персонаж стоит (%d), маршрута нет (%d), ход держится общей точкой (%d)"),
				(int32)!Me->IsMoving(), (int32)!PC->HasRoute(), (int32)PC->IsStunHardStopped()));
		R->Hold = Me->GetGamePosXZ();
		After(R, 0.5f, [](const FR& R)
		{
			AAsdaPlayerCharacter* Me = R->Me.Get();
			const float D = Me ? FVector2D::Distance(Me->GetGamePosXZ(), R->Hold) : -1.f;
			R->Expect(Me && !Me->IsMoving() && D < 2.f, FString::Printf(TEXT("K3 через 0,5 с персонаж на месте (сдвиг %.1f)"), D));
			Shot(*R, TEXT("k3_stopped_after_click"));
			Grant(*R);   // опоздавший грант на прежний 4006
			After(R, 0.3f, [](const FR& R)
			{
				AAsdaPlayerCharacter* Me = R->Me.Get();
				const float D = Me ? FVector2D::Distance(Me->GetGamePosXZ(), R->Hold) : -1.f;
				R->Expect(Me && !Me->IsMoving() && D < 2.f, FString::Printf(TEXT("K4 опоздавший грант 4007 не уводит (сдвиг %.1f)"), D));
				StepW(R);
			});
		});
	});
}

// W. Под удержанием зажата W.
void FAsdaChannelStopSelfTest::StepW(const FR& R)
{
	const int32 N0 = R->Count(4006);
	Key(*R, EKeys::W, true);
	After(R, 0.4f, [N0](const FR& R)
	{
		AAsdaPlayerCharacter* Me = R->Me.Get();
		const float D = Me ? FVector2D::Distance(Me->GetGamePosXZ(), R->Hold) : -1.f;
		R->Expect(Me && !Me->IsMoving() && D < 2.f && R->Count(4006) == N0,
			FString::Printf(TEXT("W1 зажата W во время смены канала: хода нет (сдвиг %.1f), новых 4006 %d"), D, R->Count(4006) - N0));
		Key(*R, EKeys::W, false);
		// Повторный запрос с коротким сроком ожидания (0,3 с) — сервер «молчит», ждём истечения.
		R->Keys->Set(TEXT("asda.Minimap.ChannelMoveStopSec"), 0.3f);
		AsdaClearChannelMoveHold();
		ClickChannel2(*R);
		if (AAsdaPlayerCharacter* Me2 = R->Me.Get()) { R->Hold = Me2->GetGamePosXZ(); }
		After(R, 0.8f, [](const FR& R) { StepE(R); });
	});
}

// E. Сервер промолчал — удержание снято, W снова ведёт.
void FAsdaChannelStopSelfTest::StepE(const FR& R)
{
	R->Keys->Set(TEXT("asda.Minimap.ChannelMoveStopSec"), 30.f);
	AAsdaPlayerController* PC = R->PC.Get();
	R->Expect(PC && !PC->IsStunHardStopped(), TEXT("E1 ответа нет за asda.Minimap.ChannelMoveStopSec — ход снова разрешён (замка навсегда нет)"));
	const int32 N0 = R->Count(4006);
	Key(*R, EKeys::W, true);
	// ★ 11.11.2026 (D4): 0.4 → 1.2 с. Под нагрузкой (две игры и сборка) за 0.4 с ход не успевал начаться — в эталоне 7.6.0
	//   провал «идёт 0, сдвиг 0.0, 4006 0», на повторе зелёный. Смысл проверки тот же: W после снятия удержания ведёт.
	After(R, 1.2f, [N0](const FR& R)
	{
		AAsdaPlayerCharacter* Me = R->Me.Get();
		const float D = Me ? FVector2D::Distance(Me->GetGamePosXZ(), R->Hold) : -1.f;
		R->Expect(Me && (Me->IsMoving() || D > 5.f) && R->Count(4006) > N0,
			FString::Printf(TEXT("E2 W после удержания ведёт персонажа (идёт %d, сдвиг %.1f, 4006 %d)"), Me ? (int32)Me->IsMoving() : -1, D, R->Count(4006) - N0));
		Key(*R, EKeys::W, false);
		After(R, 0.4f, [](const FR& R)
		{
			if (AAsdaPlayerCharacter* Me = R->Me.Get()) { Me->StopMove(); }
			StepA(R);
		});
	});
}

// A. Автопуть.
void FAsdaChannelStopSelfTest::StepA(const FR& R)
{
	UAsdaMinimapSubsystem* Mini = MiniOf(*R);
	AAsdaPlayerCharacter* Me = R->Me.Get();
	if (!Mini || !Me) { Finish(R); return; }
	const FIntPoint T = FIntPoint(FMath::FloorToInt(Me->GetGamePosXZ().X / 100.f) + 12, FMath::FloorToInt(Me->GetGamePosXZ().Y / 100.f));
	const bool bBegun = Mini->BeginMapTravel(3, T.X, T.Y);
	After(R, 0.5f, [bBegun](const FR& R)
	{
		UAsdaMinimapSubsystem* Mini = MiniOf(*R);
		AAsdaPlayerCharacter* Me = R->Me.Get();
		if (!Mini || !Me) { Finish(R); return; }
		const bool bWas = Mini->IsTravelRequested();
		const bool bWalk = Me->IsMoving();
		R->Expect(bBegun && bWas, FString::Printf(TEXT("A0 автопуть взведён (заявка %d, идёт %d)"), (int32)bWas, (int32)bWalk));
		ClickChannel2(*R);
		R->Expect(!Mini->IsTravelRequested() && !Me->IsMoving(),
			FString::Printf(TEXT("A1 клик «2 CH»: заявка автопути снята (%d), персонаж стоит (%d)"), (int32)!Mini->IsTravelRequested(), (int32)!Me->IsMoving()));
		R->Hold = Me->GetGamePosXZ();
		After(R, 0.5f, [](const FR& R)
		{
			AAsdaPlayerCharacter* Me = R->Me.Get();
			const float D = Me ? FVector2D::Distance(Me->GetGamePosXZ(), R->Hold) : -1.f;
			R->Expect(Me && !Me->IsMoving() && D < 2.f, FString::Printf(TEXT("A2 через 0,5 с автопуть не повёл (сдвиг %.1f)"), D));
			AsdaClearChannelMoveHold();
			StepM(R);
		});
	});
}

// M. Верхом.
void FAsdaChannelStopSelfTest::StepM(const FR& R)
{
	UAsdaNetSubsystem* Net = NetOf(*R);
	AAsdaPlayerCharacter* Me = R->Me.Get();
	if (!Net || !Me) { Finish(R); return; }
	Net->OnMountState.Broadcast(R->Self, AsdaChannelStopTest::kMountCode, true);
	Net->LastServerSpeed = 0.56f;
	StartWalk(R, Me->GetGamePosXZ() + FVector2D(-900.f, 0.f), [](const FR& R)
	{
		AAsdaPlayerCharacter* Me = R->Me.Get();
		if (!Me) { Finish(R); return; }
		R->Expect(Me->IsMounted() && Me->IsMoving(), FString::Printf(TEXT("M0 верхом (%d) кликом по земле идёт (%d)"), (int32)Me->IsMounted(), (int32)Me->IsMoving()));
		Shot(*R, TEXT("m0_riding"));
		R->Ops.Reset();
		ClickChannel2(*R);
		const int32 I8 = R->IndexOf(4008), I6 = R->IndexOf(6084);
		R->Expect(!Me->IsMoving() && I8 >= 0 && I6 > I8, FString::Printf(TEXT("M1 клик «2 CH» верхом: стоит (%d), 4008 №%d раньше 6084 №%d"), (int32)!Me->IsMoving(), I8, I6));
		R->Hold = Me->GetGamePosXZ();
		After(R, 0.5f, [](const FR& R)
		{
			AAsdaPlayerCharacter* Me = R->Me.Get();
			const float D = Me ? FVector2D::Distance(Me->GetGamePosXZ(), R->Hold) : -1.f;
			R->Expect(Me && !Me->IsMoving() && D < 2.f, FString::Printf(TEXT("M2 через 0,5 с верхом на месте (сдвиг %.1f)"), D));
			Shot(*R, TEXT("m2_riding_stopped"));
			if (Me) { Me->ClearMount(); }
			if (UAsdaNetSubsystem* Net = NetOf(*R)) { Net->LastServerSpeed = AsdaChannelStopTest::kRunSpeed; }
			AsdaClearChannelMoveHold();
			StepC(R);
		});
	});
}

// C. Контроль ключом 0 — прежнее поведение (баг).
void FAsdaChannelStopSelfTest::StepC(const FR& R)
{
	R->Keys->Set(TEXT("asda.Minimap.ChannelMoveStop"), 0);
	AAsdaPlayerCharacter* Me = R->Me.Get();
	if (!Me) { Finish(R); return; }
	StartWalk(R, Me->GetGamePosXZ() + FVector2D(900.f, 0.f), [](const FR& R)
	{
		AAsdaPlayerCharacter* Me = R->Me.Get();
		if (!Me) { Finish(R); return; }
		R->Ops.Reset();
		ClickChannel2(*R);
		R->Hold = Me->GetGamePosXZ();
		const bool bNo4008 = R->Count(4008) == 0 && R->Count(6084) == 1;
		After(R, 0.4f, [bNo4008](const FR& R)
		{
			AAsdaPlayerCharacter* Me = R->Me.Get();
			const float D = Me ? FVector2D::Distance(Me->GetGamePosXZ(), R->Hold) : -1.f;
			R->Expect(Me && Me->IsMoving() && D > 20.f && bNo4008,
				FString::Printf(TEXT("C1 контроль ключом asda.Minimap.ChannelMoveStop 0: после «2 CH» персонаж идёт дальше (сдвиг %.1f), 4008 нет — баг воспроизведён"), D));
			Shot(*R, TEXT("c1_control_key0_keeps_walking"));
			if (Me) { Me->StopMove(); }
			if (UAsdaNetSubsystem* Net = NetOf(*R)) { Net->bActionPending = false; }
			R->Keys->Set(TEXT("asda.Minimap.ChannelMoveStop"), 1);
			StepT(R);
		});
	});
}

// T. Переход 6015 идёт — W не ведёт.
void FAsdaChannelStopSelfTest::StepT(const FR& R)
{
	UAsdaNetSubsystem* Net = NetOf(*R);
	AAsdaPlayerCharacter* Me = R->Me.Get();
	if (!Net || !Me) { Finish(R); return; }
	AsdaClearChannelMoveHold();
	Net->DebugFeedPacket(6015, AsdaChannelStopTest::Body6015(3, 1));
	R->Expect(Net->bTransferInProgress, TEXT("T0 6015: переход идёт (гейт перехода взведён)"));
	R->Hold = Me->GetGamePosXZ();
	const int32 N0 = R->Count(4006);
	Key(*R, EKeys::W, true);
	After(R, 0.4f, [N0](const FR& R)
	{
		AAsdaPlayerCharacter* Me = R->Me.Get();
		const float D = Me ? FVector2D::Distance(Me->GetGamePosXZ(), R->Hold) : -1.f;
		R->Expect(Me && !Me->IsMoving() && D < 2.f && R->Count(4006) == N0,
			FString::Printf(TEXT("T1 во время перехода зажатая W не ведёт персонажа (сдвиг %.1f, 4006 %d)"), D, R->Count(4006) - N0));
		Shot(*R, TEXT("t1_transfer_w_held"));
		Key(*R, EKeys::W, false);
		After(R, 0.2f, [](const FR& R) { Finish(R); });
	});
}

void FAsdaChannelStopSelfTest::Finish(const FR& R)
{
	Key(*R, EKeys::W, false);
	AsdaClearChannelMoveHold();
	if (AAsdaPlayerCharacter* Me = R->Me.Get()) { Me->StopMove(); Me->ClearMount(); Me->SetAnimState(EAsdaPlayerState::Idle); }
	if (UAsdaMinimapSubsystem* Mini = MiniOf(*R)) { Mini->SetChannelPanelOpen(false); if (Mini->IsTravelRequested()) { Mini->CancelTravelRequest(); } }
	if (UAsdaNetSubsystem* Net = NetOf(*R))
	{
		if (R->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, R->CapGen); R->CapGen = 0; }
		Net->EnterMapId = R->Map; Net->ChannelCount = R->ChCount; Net->CurrentChannel = R->Ch; Net->GameServerIP = R->Ip; Net->GameServerPort = R->Port;
		Net->SelfTileX = R->TX; Net->SelfTileZ = R->TZ; Net->PortalTileX = R->PTX; Net->PortalTileZ = R->PTZ; Net->PortalChannelParam = R->Param;
		Net->bTransferInProgress = R->bTransfer; Net->bPendingEnterWorld = R->bPending; Net->bPortalPending = R->bPortal; Net->bInGameConnected = R->bInGame;
		Net->bActionPending = false;
		Net->ClearMoveRequestInFlight(TEXT("самотест ChannelMoveStop: конец"));
		Net->LastServerSpeed = R->Speed0;
		// R5-fix6 (13.11.2026): посадка шага M снимается тем же событием, что её поставило. Прежде в конце убиралась только модель
		//   (ClearMount), а запись списка транспорта оставалась, и транспорт возвращался следующему самотесту той же игры
		//   (asda.SelfTest.SwimBack после этого теста: персонаж в позе седла, «плывёт=0», 2/13).
		Net->OnMountState.Broadcast(R->Self, AsdaChannelStopTest::kMountCode, false);
	}
	if (R->WorldScope) { R->WorldScope->Restore(); }
	if (R->Keys) { R->Keys->Restore(); }
	AsdaSelfTestKit::Finish(R->W.Get(), AsdaChannelStopTest::kCmd, R->bQuit, LogAsdaChannelStopTest, AsdaChannelStopTest::kPrefix, R->Ok, R->Fail,
		*FString::Printf(TEXT(" (4006: %d, 4008: %d, 6084: %d)"), R->Count(4006), R->Count(4008), R->Count(6084)));
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestChannelMoveStop(
	TEXT("asda.SelfTest.ChannelMoveStop"),
	TEXT("Самотест [ИСПРАВЛЕНИЕ 08.11]: смена канала останавливает ход — клик по земле, W, автопуть, верхом, переход 6015, "
	     "истечение ожидания ответа, контроль ключом asda.Minimap.ChannelMoveStop 0. Офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, AsdaChannelStopTest::kCmd, A)) { return; }
		if (!W) { return; }
		if (A.Delay <= 0.f) { FAsdaChannelStopSelfTest::Start(W, A.bQuit); return; }
		TWeakObjectPtr<UWorld> WW = W;
		const bool bQuit = A.bQuit;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, bQuit]()
		{
			if (WW.Get() && AsdaSelfTestKit::RefuseDeferred(WW.Get(), AsdaChannelStopTest::kCmd, bQuit)) { return; }
			if (UWorld* Wd = WW.Get()) { FAsdaChannelStopSelfTest::Start(Wd, bQuit); }
		}), A.Delay, false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
