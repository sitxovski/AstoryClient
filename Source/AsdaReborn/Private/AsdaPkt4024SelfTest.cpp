// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ТЕЛА ПАКЕТА 4024 «действие/эмоция» (27.09.2026, отчёт `Reports/owner_0927/pkt4024_facing.md`). [ПОРТ]
//
// ★ ЗАЧЕМ. Эталон `Send_CharStatePos@0x9F9EF0` кладёт в тело +5/+9 не позицию, а НАПРАВЛЕНИЕ ВЗГЛЯДА: у всех
//   отправителей источник — `effectSlots[112].gap_298+12` (= `CCharacter+47718*4`), поле, которое пишется только
//   вместе с `charComputeFacingAngleFromDir`. Порт клал позицию у эмоций и «сесть/встать» (`UAsdaCharaInfoSubsystem`)
//   и у танцпола Alpen (`AAsdaPlayerCharacter::TickAutoDanceZone`). Исправлено общей точкой
//   `AAsdaPlayerCharacter::GetFacingDirGame` (её же берёт вставание клавишей, `AsdaPlayerKeyboardEtalon.cpp`).
//
// ★ КАК ПРОВЕРЯЕТ. Зовёт НАСТОЯЩИЕ входы (не копии): `UAsdaCharaInfoSubsystem::PlayEmotion` (эмоции 0..8 и функция
//   «сесть/встать»), клавишу W сидя (штатный `UPlayerInput`), тик танцпола (персонаж ставится в зону пола Alpen).
//   Перед каждой пробой персонаж поворачивается штатным `FaceGameDir(D)` на известный игровой курс D; тело
//   перехватывается узко (`ArmSendCapture(…, 4024)`) — в сеть ничего. Ожидание: код действия как у эталона, тело
//   (+5, +9) = D с точностью 0.01, |тело| = 1.
//   ★ Контрольные пары (закон pixel-metric-needs-control-pairs):
//     (а) каждый вход прогоняется при ДВУХ курсах D1 и D2 (D2 ⟂ D1) на одной и той же точке — тело обязано
//         следовать за курсом, а позиция в эти две пробы одинакова;
//     (б) мутация «как было до правки» — прямой `SendCharStatePos4024(код, позиция)` — проверка ОБЯЗАНА её отличить.
//
// Консоль: `asda.Net.SelfTest4024 [delay=N] [quit]`. Только офлайн (`FAsdaOutgoingGate::RefuseDebugWhenOnline`).
//   Строки проверок — «САМОТЕСТ-4024», итог — «САМОТЕСТ-4024 ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING
#include "AsdaBytes.h"   // ★ N6 (В1 дедупа №2): единые читалки little-endian

#include "AsdaNetSubsystem.h"
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "Test/AsdaSelfTestRunGate.h"   // RefuseDebugWhenOnline — офлайн-гейт самотестов   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "World/AsdaCoord.h"                   // игра ↔ UE (точка танцпола)
#include "World/AsdaPlayerCharacter.h"         // FaceGameDir / GetFacingDirGame / SetAnimState / StopEmotionClip
#include "UI/AsdaCharaInfoSubsystem.h"         // PlayEmotion — вход эмоций и «сесть/встать»
#include "UI/AsdaHotbarSubsystem.h"            // SitStandIconName — иконка «сесть/встать» хотбара
#include "UI/AsdaWindowSubsystem.h"            // ★ 24.10.2026: окно под курсором перед «кликом по миру» (шаг F, диагностика)
#include "UI/AsdaUiVisibilitySubsystem.h"      // ★ 24.10.2026: Alt+Z на время «клика по миру» (шаг F) — хит-тест окон молчит
#include "UI/AsdaEquipShopSubsystem.h"         // ★ 24.10.2026 (H4): вход в магазин снаряжения сидя
#include "UI/AsdaCraftWindowSubsystem.h"       // ★ 24.10.2026 (H5): вход в ремесло сидя
#include "Framework/Application/SlateApplication.h"   // ★ 24.10.2026: позиция курсора в журнал шага F
#include "World/AsdaPlayerController.h"        // ToggleSitStand, ExecuteCommand(55), OnLeftClick, bAutoAttack
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPkt4024Test, Log, All);

// Друг `UAsdaNetSubsystem` (объявлен в заголовке): нужен узкий перехватчик отправки.
struct FAsdaPkt4024SelfTest
{
	// Точка входа консольной команды.
	static void Run(const TArray<FString>& Args, UWorld* W);
	// ★ Точка входа самотеста «сесть/встать» `asda.Net.SelfTestSitStand` (27.09.2026, §6 отчёта).
	static void RunSitStand(const TArray<FString>& Args, UWorld* W);
};

namespace AsdaPkt4024TestLocal
{
	// Один перехваченный 4024.
	struct FCap
	{
		uint16 UserNum = 0;                     // тело +0
		int32  Act = 0;                         // тело +2 — код действия
		FVector2D V = FVector2D::ZeroVector;    // тело +5/+9
		int32  Len = 0;                         // длина кадра (29)
	};
	// Общее состояние прогона (живёт в лямбдах таймеров).
	struct FState
	{
		TArray<FCap> Caps;                      // перехваты
		uint32 Gen = 0;                         // поколение СВОЕГО перехватчика
		int32 Ok = 0, Fail = 0;                 // счёт
		FVector2D Home = FVector2D::ZeroVector; // игровая точка персонажа на старте
		int32 SavedMapId = -1;                  // `EnterMapId` до прогона
		int32 SavedKbKey = 1;                   // `asda.Move.KeyboardEtalon` до прогона
		bool bSavedInGame = false;              // `bInGameConnected` до прогона
		int32 Mark = 0;                         // перехватов на начало пробы
	};

	// Курсы контрольной пары (игровые x, z; единичные, D2 ⟂ D1, оба не вдоль осей).
	static const FVector2D kD1(0.6, -0.8);
	static const FVector2D kD2(0.8, 0.6);

	// ★ N6 (В1 дедупа №2): формула чтения — `AsdaBytes`; прежние имена и типы — переходники.
	static float RdF(const uint8* P) { return AsdaBytes::RdF32(P); }
	static uint16 RdU16(const uint8* P) { return (uint16)AsdaBytes::RdU16(P); }

	// Свой персонаж (офлайн-спавн).
	static AAsdaPlayerCharacter* SelfCh(UWorld* W)
	{
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		return PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
	}
	// Нажать/отпустить клавишу штатным путём `UPlayerInput` (как `asda.Keybinds.HoldSim`).
	using AsdaSelfTestInput::KeyEv;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.
	// Привести персонажа в покой без эмоции и повернуть на курс D.
	static void Prepare(AAsdaPlayerCharacter* C, const FVector2D& D)
	{
		if (!C) { return; }
		C->StopEmotionClip();
		if (C->AnimState != EAsdaPlayerState::Idle) { C->SetAnimState(EAsdaPlayerState::Idle); }
		C->FaceGameDir(D);
	}
	// Итог одной пробы. bExpectFacing = true — тело обязано нести курс D (исправленные места);
	//   false — мутация «позиция», проверка обязана её отличить (иначе самотест слеп).
	static void Judge(FState& S, const TCHAR* Label, int32 ExpectAct, const FVector2D& D, const FVector2D& Pos, bool bExpectFacing)
	{
		const int32 N = S.Caps.Num() - S.Mark;
		auto Line = [](const FString& T) { UE_LOG(LogAsdaPkt4024Test, Log, TEXT("%s"), *T); };
		if (N != 1)
		{
			++S.Fail;
			Line(FString::Printf(TEXT("САМОТЕСТ-4024 ПРОВАЛ [%s]: перехвачено пакетов 4024 %d (ожидался ровно 1)"), Label, N));
			return;
		}
		const FCap& C = S.Caps.Last();
		const bool bAct    = (ExpectAct < 0) ? (C.Act == 112 || C.Act == 113) : (C.Act == ExpectAct);
		const bool bUnit   = FMath::IsNearlyEqual((float)C.V.Size(), 1.f, 0.01f);
		const bool bFacing = bUnit && FVector2D::Distance(C.V, D) < 0.01;
		const bool bPos    = FVector2D::Distance(C.V, Pos) < 1.0;
		const bool bHdr    = (C.Len == 29);
		const bool bPass   = bExpectFacing ? (bAct && bFacing && !bPos && bHdr) : (!bFacing && bPos);
		if (bPass) { ++S.Ok; } else { ++S.Fail; }
		Line(FString::Printf(
			TEXT("САМОТЕСТ-4024 %s [%s]: код %d (ждали %s), тело (%.4f, %.4f) |тело|=%.4f, курс D=(%.2f, %.2f), позиция (%.0f, %.0f), len=%d | %s"),
			bPass ? TEXT("OK") : TEXT("ПРОВАЛ"), Label, C.Act,
			ExpectAct < 0 ? TEXT("112/113") : *FString::FromInt(ExpectAct),
			C.V.X, C.V.Y, C.V.Size(), D.X, D.Y, Pos.X, Pos.Y, C.Len,
			bExpectFacing ? (bFacing ? TEXT("НАПРАВЛЕНИЕ взгляда") : (bPos ? TEXT("ПОЗИЦИЯ вместо курса") : TEXT("не курс D")))
			              : (bFacing ? TEXT("мутация НЕ отличена — самотест слеп") : TEXT("мутация (позиция) отличена, как и должно"))));
	}
}

void FAsdaPkt4024SelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaPkt4024TestLocal;
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { UE_LOG(LogAsdaPkt4024Test, Warning, TEXT("САМОТЕСТ-4024: нет подсистемы сети")); return; }
	bool bQuit = false;
	float Delay = 0.f;
	for (const FString& A : Args)
	{
		if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
	}
	Delay = AsdaSelfTestKit::DelayArgOr(Args, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
	// Только офлайн; окно защиты на время прохода. 4006/4008 — на случай хода после вставания клавишей.
	static const uint16 kGuardOps[] = { 4024, 4006, 4008 };
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
	if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Net.SelfTest4024"), KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }

	TSharedRef<FState> S = MakeShared<FState>();
	TWeakObjectPtr<UAsdaNetSubsystem> WNet(Net);
	TWeakObjectPtr<UWorld> WW(W);
	constexpr float kLead = 2.f;   // столько даём офлайн-спавну своего павна
	// ★ Р-2 (10.10.2026): офлайн проверяется повторно в момент ОТЛОЖЕННОГО старта — первым сработавшим таймером;
	//   при отказе все шаги прогона пропускаются (итог отказа пишет `RefuseDeferredStartWhenOnline`).
	// ★ 11.10.2026 (В3 S1): своя копия флага (FGate) заменена общим `FAsdaSelfTestRunGate` — поведение то же.
	TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(TEXT("asda.Net.SelfTest4024"), bQuit);   // общий флаг отказа всех шагов прогона
	auto GateRefused = [Gate, WW]() { return !Gate->Pass(WW.Get()); };   // true — прогон отменён; проверка один раз, первым сработавшим шагом
	auto At = [W, Net, Delay, kLead, GateRefused](float T, TFunction<void()> Fn)
	{
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Net, [GateRefused, Fn = MoveTemp(Fn)]()
		{
			if (GateRefused()) { return; }   // Р-2: офлайн повторно — на отложенном старте
			Fn();
		}), T + Delay + kLead, false);
	};

	// ── t=−kLead: офлайн-спавн своего павна штатными крюками (как у `asda.Net.SelfTestStop4008`) ──
	{
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Net, [WW, GateRefused]()
		{
			UWorld* Wd = WW.Get();
			if (Wd && GateRefused()) { return; }   // Р-2: офлайн повторно — на отложенном старте
			if (Wd && !SelfCh(Wd) && GEngine)
			{
				GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
		}), FMath::Max(0.01f, Delay), false);
	}

	// ── t=0.1: подготовка — узкий перехват 4024, запомнить точку и ключи ──
	At(0.1f, [S, WNet, WW]()
	{
		UAsdaNetSubsystem* N = WNet.Get(); AAsdaPlayerCharacter* C = SelfCh(WW.Get());
		if (!N || !C) { ++S->Fail; UE_LOG(LogAsdaPkt4024Test, Log, TEXT("САМОТЕСТ-4024 ПРОВАЛ [подготовка]: нет персонажа")); return; }
		TWeakPtr<FState> WS = S;
		S->Gen = AsdaSelfTestKit::ArmCapture(N, [WS](uint16 Op, const uint8* Pl, int32 Len, bool /*bPad*/, TCHAR /*Env*/)
		{
			TSharedPtr<FState> St = WS.Pin();
			if (!St || Op != 4024 || !Pl || Len < 29) { return; }
			FCap Cp;
			Cp.Len = Len; Cp.UserNum = RdU16(Pl + 16); Cp.Act = (int32)RdU16(Pl + 18);
			Cp.V = FVector2D(RdF(Pl + 21), RdF(Pl + 25));
			St->Caps.Add(Cp);
		}, (uint16)4024, TEXT("asda.Net.SelfTest4024"));
		S->Home = C->GetGamePosXZ();
		S->SavedMapId = N->EnterMapId;
		S->bSavedInGame = N->bInGameConnected;
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Move.KeyboardEtalon"))) { S->SavedKbKey = V->GetInt(); }
		UE_LOG(LogAsdaPkt4024Test, Log, TEXT("САМОТЕСТ-4024 подготовка: перехват 4024 поколение %u, персонаж в игре (%.0f, %.0f), userNum известен=%d"),
			S->Gen, S->Home.X, S->Home.Y, N->IsSelfUserNumKnown() ? 1 : 0);
	});

	// ── t=0.3: контроль (б) — мутация «как было до правки»: позиция прямо в тело ──
	At(0.3f, [S, WNet, WW]()
	{
		UAsdaNetSubsystem* N = WNet.Get(); AAsdaPlayerCharacter* C = SelfCh(WW.Get());
		if (!N || !C) { ++S->Fail; return; }
		Prepare(C, kD1);
		const FVector2D P = C->GetGamePosXZ();
		S->Mark = S->Caps.Num();
		N->SendCharStatePos4024(100, (float)P.X, (float)P.Y);
		Judge(*S, TEXT("мутация: позиция вместо курса"), 100, kD1, P, false);
	});

	// ── t=0.5: эмоции 0..8 окна персонажа (`PlayEmotion` → 4024 100…106/112/113), каждая при D1 и D2 ──
	At(0.5f, [S, WNet, WW]()
	{
		UAsdaNetSubsystem* N = WNet.Get(); UWorld* Wd = WW.Get(); AAsdaPlayerCharacter* C = SelfCh(Wd);
		UAsdaCharaInfoSubsystem* CI = (Wd && Wd->GetGameInstance()) ? Wd->GetGameInstance()->GetSubsystem<UAsdaCharaInfoSubsystem>() : nullptr;
		if (!N || !C || !CI) { ++S->Fail; UE_LOG(LogAsdaPkt4024Test, Log, TEXT("САМОТЕСТ-4024 ПРОВАЛ [эмоции]: нет подсистемы")); return; }
		static const int32 kCodes[9] = { 100, 101, 102, 103, 104, 105, 106, 112, 113 };   // ui_core.c:32357…32453
		for (int32 i = 0; i < 9; ++i)
		{
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				const FVector2D D = Pass ? kD2 : kD1;
				Prepare(C, D);
				S->Mark = S->Caps.Num();
				CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(0, i));
				Judge(*S, *FString::Printf(TEXT("эмоция %d, курс D%d"), i, Pass + 1), kCodes[i], D, C->GetGamePosXZ(), true);
			}
		}
		Prepare(C, kD1);
	});

	// ── t=1.0: «сесть/встать» кнопкой функций (`PlayEmotion(группа 2, 0)` → 107 при D1, затем 109 при D2) ──
	At(1.0f, [S, WNet, WW]()
	{
		UAsdaNetSubsystem* N = WNet.Get(); UWorld* Wd = WW.Get(); AAsdaPlayerCharacter* C = SelfCh(Wd);
		UAsdaCharaInfoSubsystem* CI = (Wd && Wd->GetGameInstance()) ? Wd->GetGameInstance()->GetSubsystem<UAsdaCharaInfoSubsystem>() : nullptr;
		if (!N || !C || !CI) { ++S->Fail; UE_LOG(LogAsdaPkt4024Test, Log, TEXT("САМОТЕСТ-4024 ПРОВАЛ [сесть/встать]: нет подсистемы")); return; }
		Prepare(C, kD1);
		S->Mark = S->Caps.Num();
		CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(2, 0));
		Judge(*S, TEXT("кнопка «сесть», курс D1"), 107, kD1, C->GetGamePosXZ(), true);
	});
	// ── t=4.0: «встать» — после фазы 8 (сидит): при 7 (садится) эталон позы не меняет (`charActionStateIs7to9`) ──
	At(4.0f, [S, WNet, WW]()
	{
		UAsdaNetSubsystem* N = WNet.Get(); UWorld* Wd = WW.Get(); AAsdaPlayerCharacter* C = SelfCh(Wd);
		UAsdaCharaInfoSubsystem* CI = (Wd && Wd->GetGameInstance()) ? Wd->GetGameInstance()->GetSubsystem<UAsdaCharaInfoSubsystem>() : nullptr;
		if (!N || !C || !CI) { ++S->Fail; return; }
		C->FaceGameDir(kD2);   // сидя — только поворот меша, состояние «сидит» не трогаем
		S->Mark = S->Caps.Num();
		CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(2, 0));
		Judge(*S, TEXT("кнопка «встать», курс D2"), 109, kD2, C->GetGamePosXZ(), true);
		Prepare(C, kD1);
	});

	// ── t=4.5 … 7.6: вставание клавишей W (схема эталона `asda.Move.KeyboardEtalon 1`) при курсе D2 ──
	At(4.5f, [S, WW]()
	{
		AAsdaPlayerCharacter* C = SelfCh(WW.Get());
		if (!C) { return; }
		AsdaCVarSave::SetForTest(TEXT("asda.Move.KeyboardEtalon"), 1);
		Prepare(C, kD2);
		C->SetAnimState(EAsdaPlayerState::Sit);   // сесть: фаза 8 через ~2.6 с (как KB11a)
	});
	At(7.3f, [S, WNet, WW]()
	{
		UAsdaNetSubsystem* N = WNet.Get(); AAsdaPlayerCharacter* C = SelfCh(WW.Get());
		if (!N || !C) { ++S->Fail; return; }
		C->FaceGameDir(kD2);
		S->Mark = S->Caps.Num();
		N->bInGameConnected = true;   // ветка вставания шлёт только «в мире»; перехват узкий, сокета нет
		UE_LOG(LogAsdaPkt4024Test, Log, TEXT("САМОТЕСТ-4024 клавиша W сидя: фаза %d (ждём 8)"), C->GetSitPhase());
		KeyEv(WW.Get(), EKeys::W, true);
	});
	At(7.6f, [S, WNet, WW]()
	{
		UAsdaNetSubsystem* N = WNet.Get(); AAsdaPlayerCharacter* C = SelfCh(WW.Get());
		KeyEv(WW.Get(), EKeys::W, false);
		if (N) { N->bInGameConnected = S->bSavedInGame; }
		if (!C) { ++S->Fail; return; }
		// Вставание — единственный 4024 окна; ход после него (4006) сюда не попадает: перехват узкий.
		if (S->Caps.Num() - S->Mark > 1) { S->Caps.SetNum(S->Mark + 1); }
		Judge(*S, TEXT("клавиша W сидя (вставание), курс D2"), 109, kD2, C->GetGamePosXZ(), true);
		AsdaCVarSave::SetForTest(TEXT("asda.Move.KeyboardEtalon"), S->SavedKbKey);
		C->NetSnap(S->Home);
		Prepare(C, kD1);
	});

	// ── t=8.0 … 11.5: танцпол Alpen (тик `TickAutoDanceZone` → 4024 112/113), курс D1, затем D2 ──
	//   Зона — прямоугольник `.rdata` эталона 15136..16758 × 36847..38172 (игровые x, z); центр.
	const FVector2D kFloor(15947.0, 37509.5);
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const float T0 = 8.0f + Pass * 2.0f;
		const FVector2D D = Pass ? kD2 : kD1;
		At(T0, [S, WNet, WW, D, kFloor]()
		{
			UAsdaNetSubsystem* N = WNet.Get(); AAsdaPlayerCharacter* C = SelfCh(WW.Get());
			if (!N || !C) { return; }
			N->EnterMapId = 3;               // зона работает только на Alpen (`getField3036 == 3`)
			C->NetSnap(kFloor);              // покой в центре пола
			Prepare(C, D);
			S->Mark = S->Caps.Num();
		});
		At(T0 + 1.5f, [S, WNet, WW, D, Pass]()
		{
			UAsdaNetSubsystem* N = WNet.Get(); AAsdaPlayerCharacter* C = SelfCh(WW.Get());
			if (!N || !C) { ++S->Fail; UE_LOG(LogAsdaPkt4024Test, Log, TEXT("САМОТЕСТ-4024 ПРОВАЛ [танцпол]: нет персонажа")); return; }
			// Первый танец после входа; следующих за 1.5 с не бывает (кулдаун = длина клипа).
			if (S->Caps.Num() - S->Mark > 1) { S->Caps.SetNum(S->Mark + 1); }
			Judge(*S, *FString::Printf(TEXT("танцпол Alpen, курс D%d"), Pass + 1), -1, D, C->GetGamePosXZ(), true);
			C->NetSnap(S->Home);             // выход из зоны снимает кулдаун (как у штатного ухода с пола)
			Prepare(C, kD1);
			N->EnterMapId = S->SavedMapId;
		});
	}

	// ── t=12.5: снять перехват, итог ──
	At(12.5f, [S, WNet, WW, bQuit]()
	{
		UAsdaNetSubsystem* N = WNet.Get(); UWorld* Wd = WW.Get();
		if (N) { AsdaSelfTestKit::DisarmCapture(N, S->Gen); N->EnterMapId = S->SavedMapId; N->bInGameConnected = S->bSavedInGame; }
		// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключ Move.KeyboardEtalon — к исходному значению И приоритету SetBy. Прежде его возвращал только шаг
		//   7.6 (прежним значением, но с SetByConsole), а при раннем выходе шага (нет персонажа) ключ оставался 1.
		AsdaCVarSave::RestoreAll(TEXT("asda.Net.SelfTest4024"));
		UE_LOG(LogAsdaPkt4024Test, Log, TEXT("САМОТЕСТ-4024 ИТОГ: ok=%d fail=%d (перехвачено 4024: %d)"), S->Ok, S->Fail, S->Caps.Num());
		AsdaSelfTestKit::Quit(Wd, bQuit);
	});
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTest4024(
	TEXT("asda.Net.SelfTest4024"),
	TEXT("Самотест тела 4024: курс взгляда вместо позиции — эмоции 0..8, «сесть/встать», вставание клавишей W, танцпол Alpen; "
	     "каждая проба при двух курсах + мутация «позиция». Узкий перехват, сеть не нужна (только офлайн). `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Net.SelfTest4024"));   // ★ С1 (ревизия wave0_REVIEW.md, 11.10.2026): мир ушёл до итога — ключи вернёт сторож
		FAsdaPkt4024SelfTest::Run(A, W);
	}));

// ═════════════════════════════════════════════════════════════════════════════════════════════════════════════
//  ★★★ САМОТЕСТ «СЕСТЬ/ВСТАТЬ» `asda.Net.SelfTestSitStand` (27.09.2026, отчёт pkt4024_facing.md §6). [ПОРТ]
//  Эталон `charToggleSitStand@0xC54A70`: на ходу — 4008 «стоп» ПЕРЕД 4024; мёртв — ничего; сидит (8) → 4024 109;
//  садится/встаёт (7/9) — позы не меняет; «занят» (`objStateAllowsRender`) — молча, без иконок; иначе 4024 107;
//  в конце иконка `act_standup` (7/8) / `act_updown`. Входы — НАСТОЯЩИЕ: кнопка окна действий (`PlayEmotion(200)`),
//  клавиша 55 (`ExecuteCommand`), клик по миру сидя (`OnLeftClick`). Перехват всего исходящего (в сеть ничего).
//  Контрольные пары: каждый сценарий при `asda.Char.SitToggleEtalon` 1 и 0; при 0 проверки ждут прежнее поведение
//  порта (противоположное там, где правка его меняет).
// ═════════════════════════════════════════════════════════════════════════════════════════════════════════════
namespace AsdaSitTestLocal
{
	using namespace AsdaPkt4024TestLocal;
	// Один перехваченный пакет 4008/4024/4006.
	struct FPk
	{
		uint16 Op = 0;                         // опкод
		int32 Act = 0;                         // 4024: код действия
		FVector2D V = FVector2D::ZeroVector;   // 4024: тело +5/+9
		double T = 0.0;                        // ★ 24.10.2026: `FPlatformTime::Seconds` перехвата (интервал 107 → 108)
	};
	// Состояние прогона.
	struct FSt
	{
		TArray<FPk> Caps;                      // перехваты по порядку
		uint32 Gen = 0;                        // поколение своего перехватчика
		int32 Ok = 0, Fail = 0;                // счёт
		int32 Key = 1;                         // текущее значение ключа
		bool bSavedInGame = false;             // `bInGameConnected` до прогона
		bool bOpenedCharaInfo = false;         // ★ 24.10.2026: окно действий открыл сам самотест — закрыть в конце
		FVector2D Home = FVector2D::ZeroVector;   // точка персонажа
		int32 Mark = 0;                        // перехватов на начало пробы
		FString TrayBefore;                    // иконка лотка окна действий до пробы
		int32 GMark = 0;                       // ★ 24.10.2026 (этап G, 108/0): перехватов на начало сценария (переживает `Reset`)
		// ★ 24.10.2026: число 4024 с кодом Act начиная с перехвата From.
		int32 CountActFrom(int32 From, int32 Act) const { int32 R = 0; for (int32 i = From; i < Caps.Num(); ++i) { if (Caps[i].Op == 4024 && Caps[i].Act == Act) { ++R; } } return R; }
		// ★ 24.10.2026: индекс первого 4024 с кодом Act начиная с перехвата From (−1 — не было).
		int32 IndexOfActFrom(int32 From, int32 Act) const { for (int32 i = From; i < Caps.Num(); ++i) { if (Caps[i].Op == 4024 && Caps[i].Act == Act) { return i; } } return -1; }
		// ★ 24.10.2026 (этап H): всего 4024 начиная с перехвата From.
		int32 Count4024From(int32 From) const { int32 R = 0; for (int32 i = From; i < Caps.Num(); ++i) { if (Caps[i].Op == 4024) { ++R; } } return R; }
		// Число перехватов опкода с отметки.
		int32 Count(uint16 Op) const { int32 R = 0; for (int32 i = Mark; i < Caps.Num(); ++i) { if (Caps[i].Op == Op) { ++R; } } return R; }
		// Индекс первого перехвата опкода с отметки (−1 — нет).
		int32 First(uint16 Op) const { for (int32 i = Mark; i < Caps.Num(); ++i) { if (Caps[i].Op == Op) { return i; } } return -1; }
		// Код действия первого 4024 с отметки (−1 — нет).
		int32 FirstAct() const { const int32 i = First(4024); return i >= 0 ? Caps[i].Act : -1; }
		// Одна проверка.
		void Expect(bool b, const TCHAR* Step, const FString& What)
		{
			if (b) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaPkt4024Test, Log, TEXT("САМОТЕСТ-СИДЕНИЕ %s [ключ=%d] %s: %s"), b ? TEXT("OK") : TEXT("ПРОВАЛ"), Key, Step, *What);
		}
	};
	// Контроллер своего персонажа.
	static AAsdaPlayerController* SelfPC(UWorld* W) { return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
	// Сброс: снять сидение без хвоста вставания (как KB-самотест), покой дома, без боя и маршрута.
	static void Reset(FSt& S, UWorld* W)
	{
		AAsdaPlayerCharacter* C = SelfCh(W); AAsdaPlayerController* PC = SelfPC(W);
		if (PC) { PC->CancelRoute(); PC->bAutoAttack = false; PC->bInCombatStance = false; PC->bCombatStarted = false; }
		if (C) { C->StopEmotionClip(); C->StopMove(); C->SetAnimState(EAsdaPlayerState::Move); C->NetSnap(S.Home); C->FaceGameDir(kD1); }
		S.Mark = S.Caps.Num();
	}
	// Имя иконки «сесть/встать» на хотбаре сейчас.
	static FString HotbarIcon(UWorld* W)
	{
		UAsdaHotbarSubsystem* Hb = (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
		return Hb ? Hb->SitStandIconName(TEXT("act_updown")) : FString(TEXT("?"));
	}
	// Имя, поставленное лотку «функции» окна действий (`UI_SetSitStandButtonIcon`); пусто — не ставилось.
	static FString TrayIcon(UWorld* W)
	{
		UAsdaCharaInfoSubsystem* CI = (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaCharaInfoSubsystem>() : nullptr;
		return CI ? CI->GetSitStandIconShown() : FString(TEXT("?"));
	}
	// Кнопка окна действий «функции → сесть/встать».
	static void PressButton(UWorld* W)
	{
		UAsdaCharaInfoSubsystem* CI = (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaCharaInfoSubsystem>() : nullptr;
		if (CI) { CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(2, 0)); }
	}
}

void FAsdaPkt4024SelfTest::RunSitStand(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaSitTestLocal;
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { UE_LOG(LogAsdaPkt4024Test, Warning, TEXT("САМОТЕСТ-СИДЕНИЕ: нет подсистемы сети")); return; }
	bool bQuit = false;
	float Delay = 0.f;
	for (const FString& A : Args)
	{
		if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
	}
	Delay = AsdaSelfTestKit::DelayArgOr(Args, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
	static const uint16 kGuardOps[] = { 4024, 4008, 4006 };
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
	if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Net.SelfTestSitStand"), KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }

	TSharedRef<FSt> S = MakeShared<FSt>();
	TWeakObjectPtr<UAsdaNetSubsystem> WNet(Net);
	TWeakObjectPtr<UWorld> WW(W);
	IConsoleVariable* KeyVar = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Char.SitToggleEtalon"));
	constexpr float kLead = 2.f;   // столько даём офлайн-спавну своего павна
	// ★ Р-2 (10.10.2026): офлайн проверяется повторно в момент ОТЛОЖЕННОГО старта — первым сработавшим таймером;
	//   при отказе все шаги прогона пропускаются (итог отказа пишет `RefuseDeferredStartWhenOnline`).
	// ★ 11.10.2026 (В3 S1): своя копия флага (FGate) заменена общим `FAsdaSelfTestRunGate` — поведение то же.
	TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(TEXT("asda.Net.SelfTestSitStand"), bQuit);   // общий флаг отказа всех шагов прогона
	auto GateRefused = [Gate, WW]() { return !Gate->Pass(WW.Get()); };   // true — прогон отменён; проверка один раз, первым сработавшим шагом
	auto At = [W, Net, Delay, kLead, GateRefused](float T, TFunction<void()> Fn)
	{
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Net, [GateRefused, Fn = MoveTemp(Fn)]()
		{
			if (GateRefused()) { return; }   // Р-2: офлайн повторно — на отложенном старте
			Fn();
		}), T + Delay + kLead, false);
	};
	{
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Net, [WW, GateRefused]()
		{
			UWorld* Wd = WW.Get();
			if (Wd && GateRefused()) { return; }   // Р-2: офлайн повторно — на отложенном старте
			if (Wd && !SelfCh(Wd) && GEngine)
			{
				GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
		}), FMath::Max(0.01f, Delay), false);
	}
	// ── Подготовка: перехват ВСЕГО исходящего, «в мире» (4008 через `StopMoveAndNotify` шлётся только в мире) ──
	At(0.1f, [S, WNet, WW, KeyVar]()
	{
		UAsdaNetSubsystem* N = WNet.Get(); AAsdaPlayerCharacter* C = SelfCh(WW.Get());
		if (!N || !C || !KeyVar) { ++S->Fail; UE_LOG(LogAsdaPkt4024Test, Log, TEXT("САМОТЕСТ-СИДЕНИЕ ПРОВАЛ [подготовка]: нет персонажа или ключа")); return; }
		TWeakPtr<FSt> WS = S;
		S->Gen = AsdaSelfTestKit::ArmCapture(N, [WS](uint16 Op, const uint8* Pl, int32 Len, bool, TCHAR)
		{
			TSharedPtr<FSt> St = WS.Pin();
			if (!St || !Pl || (Op != 4024 && Op != 4008 && Op != 4006)) { return; }
			FPk P; P.Op = Op; P.T = FPlatformTime::Seconds();
			if (Op == 4024 && Len >= 29) { P.Act = (int32)RdU16(Pl + 18); P.V = FVector2D(RdF(Pl + 21), RdF(Pl + 25)); }
			St->Caps.Add(P);
		}, (uint16)0, TEXT("asda.Net.SelfTest4024"));
		S->bSavedInGame = N->bInGameConnected;
		N->bInGameConnected = true;
		S->Home = C->GetGamePosXZ();
		// Окно действий на вкладке «Эмоции» — лотки строятся только у открытого окна (иконку «сесть/встать» видно там).
		UWorld* Wd = WW.Get();
		UAsdaCharaInfoSubsystem* CI = (Wd && Wd->GetGameInstance()) ? Wd->GetGameInstance()->GetSubsystem<UAsdaCharaInfoSubsystem>() : nullptr;
		if (CI && !CI->IsWindowOpen()) { CI->ToggleTab(1); S->bOpenedCharaInfo = CI->IsWindowOpen(); }
		S->Expect(CI && CI->IsWindowOpen(), TEXT("подготовка"), FString::Printf(TEXT("окно действий открыто: %d"), (CI && CI->IsWindowOpen()) ? 1 : 0));
	});

	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const int32 K = (Pass == 0) ? 1 : 0;
		const float B = 0.3f + Pass * 10.f;
		// A. На ходу — кнопка: эталон 4008, затем 4024 107; персонаж остановлен и садится (7).
		At(B + 0.0f, [S, WNet, WW, KeyVar, K]()
		{
			UWorld* Wd = WW.Get(); UAsdaNetSubsystem* N = WNet.Get(); AAsdaPlayerCharacter* C = SelfCh(Wd);
			if (!N || !C || !KeyVar) { ++S->Fail; return; }
			S->Key = K; AsdaCVarSave::SetForTest(TEXT("asda.Char.SitToggleEtalon"), K);
			Reset(*S, Wd);
			C->NetMove(S->Home, S->Home + kD1 * 800.0, 0.35f);   // локальный ход «в точку»
			N->bActionPending = true;                             // незакрытый 4006 — сервер ждёт 4008
			const bool bWasMoving = C->IsMoving();
			S->Mark = S->Caps.Num();
			S->TrayBefore = TrayIcon(Wd);
			PressButton(Wd);
			const int32 i8 = S->First(4008), i24 = S->First(4024);
			const bool bAct107 = (S->FirstAct() == 107);
			if (K) { S->Expect(bWasMoving && S->Count(4008) == 1 && S->Count(4024) == 1 && i8 < i24 && bAct107 && !C->IsMoving() && C->GetEtalonSitState() == 7,
				TEXT("A кнопка на ходу"), FString::Printf(TEXT("шёл=%d, 4008 %d шт. (#%d), 4024 %d шт. (#%d, код %d), идёт после=%d, сидячее %d — ждали 4008 → 4024/107, стоит, 7"),
					bWasMoving ? 1 : 0, S->Count(4008), i8, S->Count(4024), i24, S->FirstAct(), C->IsMoving() ? 1 : 0, C->GetEtalonSitState())); }
			else   { S->Expect(bWasMoving && S->Count(4008) == 0 && bAct107,
				TEXT("A кнопка на ходу"), FString::Printf(TEXT("прежний порт: 4008 %d шт. (ждали 0), 4024 код %d — контроль"), S->Count(4008), S->FirstAct())); }
			const FString Tr = TrayIcon(Wd);
			if (K) { S->Expect(Tr == TEXT("act_standup"), TEXT("A иконка окна действий"), FString::Printf(TEXT("%s (ждали act_standup)"), *Tr)); }
			else   { S->Expect(Tr == S->TrayBefore, TEXT("A иконка окна действий"), FString::Printf(TEXT("прежний порт: не менялась (%s → %s) — контроль"), *S->TrayBefore, *Tr)); }
			const FString Ic = HotbarIcon(Wd);
			if (K) { S->Expect(Ic == TEXT("act_standup"), TEXT("A иконка хотбара"), FString::Printf(TEXT("%s (ждали act_standup: садится)"), *Ic)); }
			else   { S->Expect(Ic == TEXT("act_updown"), TEXT("A иконка хотбара"), FString::Printf(TEXT("%s (прежний порт: имя из таблицы) — контроль"), *Ic)); }
			N->bActionPending = false;
		});
		// B. Занят (автоатака) — кнопка: эталон молча, без пакетов и без позы.
		At(B + 0.5f, [S, WW, K]()
		{
			UWorld* Wd = WW.Get(); AAsdaPlayerCharacter* C = SelfCh(Wd); AAsdaPlayerController* PC = SelfPC(Wd);
			if (!C || !PC) { ++S->Fail; return; }
			Reset(*S, Wd);
			PC->bAutoAttack = true;
			PressButton(Wd);
			const int32 N24 = S->Count(4024), N8 = S->Count(4008);
			if (K) { S->Expect(N24 == 0 && N8 == 0 && C->GetEtalonSitState() == 0, TEXT("B занят"), FString::Printf(TEXT("4024 %d, 4008 %d, сидячее %d — ждали молча 0/0/0"), N24, N8, C->GetEtalonSitState())); }
			else   { S->Expect(N24 == 1 && C->GetEtalonSitState() == 7, TEXT("B занят"), FString::Printf(TEXT("прежний порт: сел (%d) и 4024 %d — контроль"), C->GetEtalonSitState(), N24)); }
			PC->bAutoAttack = false;
		});
		// C. Садится (7) — кнопка: эталон позы не меняет, пакетов нет.
		At(B + 1.0f, [S, WW, K]()
		{
			UWorld* Wd = WW.Get(); AAsdaPlayerCharacter* C = SelfCh(Wd);
			if (!C) { ++S->Fail; return; }
			Reset(*S, Wd);
			C->SetAnimState(EAsdaPlayerState::Sit);
			const int32 Before = C->GetEtalonSitState();
			S->Mark = S->Caps.Num();
			PressButton(Wd);
			const int32 N24 = S->Count(4024);
			if (K) { S->Expect(Before == 7 && N24 == 0 && C->GetEtalonSitState() == 7, TEXT("C садится (7)"), FString::Printf(TEXT("до %d, 4024 %d, после %d — ждали 7/0/7"), Before, N24, C->GetEtalonSitState())); }
			else   { S->Expect(Before == 7 && N24 == 1 && S->FirstAct() == 109, TEXT("C садится (7)"), FString::Printf(TEXT("прежний порт: встать сразу (4024 %d, код %d) — контроль"), N24, S->FirstAct())); }
		});
		// D. Сидит (8) — иконка хотбара, затем кнопка: 4024 109 с курсом, фаза 9, иконка act_updown.
		At(B + 1.5f, [S, WW]()
		{
			UWorld* Wd = WW.Get(); AAsdaPlayerCharacter* C = SelfCh(Wd);
			if (!C) { ++S->Fail; return; }
			Reset(*S, Wd);
			C->SetAnimState(EAsdaPlayerState::Sit);
		});
		At(B + 4.4f, [S, WW, K]()
		{
			UWorld* Wd = WW.Get(); AAsdaPlayerCharacter* C = SelfCh(Wd);
			if (!C) { ++S->Fail; return; }
			const FString Ic8 = HotbarIcon(Wd);
			if (K) { S->Expect(C->GetEtalonSitState() == 8 && Ic8 == TEXT("act_standup"), TEXT("D сидит: иконка"), FString::Printf(TEXT("состояние %d, иконка %s — ждали 8/act_standup"), C->GetEtalonSitState(), *Ic8)); }
			else   { S->Expect(Ic8 == TEXT("act_updown"), TEXT("D сидит: иконка"), FString::Printf(TEXT("прежний порт: %s — контроль"), *Ic8)); }
			C->FaceGameDir(kD2);
			S->Mark = S->Caps.Num();
			S->TrayBefore = TrayIcon(Wd);
			PressButton(Wd);
			const FString TrD = TrayIcon(Wd);
			if (K) { S->Expect(S->TrayBefore == TEXT("act_standup") && TrD == TEXT("act_updown"), TEXT("D встаёт: иконка окна действий"), FString::Printf(TEXT("%s → %s (ждали act_standup → act_updown)"), *S->TrayBefore, *TrD)); }
			else   { S->Expect(TrD == S->TrayBefore, TEXT("D встаёт: иконка окна действий"), FString::Printf(TEXT("прежний порт: не менялась (%s → %s) — контроль"), *S->TrayBefore, *TrD)); }
			const int32 i24 = S->First(4024);
			const bool b109 = i24 >= 0 && S->Caps[i24].Act == 109 && FVector2D::Distance(S->Caps[i24].V, kD2) < 0.01;
			S->Expect(S->Count(4024) == 1 && b109 && C->GetEtalonSitState() == 9 && S->Count(4008) == 0, TEXT("D сидит: кнопка «встать»"),
				FString::Printf(TEXT("4024 %d шт., код %d, тело (%.3f, %.3f), фаза %d, 4008 %d — ждали 1/109/курс D2/9/0 (без пары: одинаково при обоих ключах)"),
					S->Count(4024), S->FirstAct(), i24 >= 0 ? S->Caps[i24].V.X : 0.0, i24 >= 0 ? S->Caps[i24].V.Y : 0.0, C->GetEtalonSitState(), S->Count(4008)));
			const FString Ic9 = HotbarIcon(Wd);
			S->Expect(Ic9 == TEXT("act_updown"), TEXT("D встаёт: иконка"), FString::Printf(TEXT("%s (ждали act_updown; без пары)"), *Ic9));
		});
		// E. Клавиша 55 (эталон `hotkeyDispatchAction` → `UI_DispatchTargetContextMenu(2, 0)`).
		At(B + 5.0f, [S, WW, K]()
		{
			UWorld* Wd = WW.Get(); AAsdaPlayerCharacter* C = SelfCh(Wd); AAsdaPlayerController* PC = SelfPC(Wd);
			if (!C || !PC) { ++S->Fail; return; }
			Reset(*S, Wd);
			PC->ExecuteCommand(55);
			const int32 N24 = S->Count(4024);
			if (K) { S->Expect(N24 == 1 && S->FirstAct() == 107 && C->GetEtalonSitState() == 7, TEXT("E клавиша 55"), FString::Printf(TEXT("4024 %d шт. код %d, сидячее %d — ждали 1/107/7"), N24, S->FirstAct(), C->GetEtalonSitState())); }
			else   { S->Expect(N24 == 0 && C->GetEtalonSitState() == 7, TEXT("E клавиша 55"), FString::Printf(TEXT("прежний порт: сел без пакета (4024 %d) — контроль"), N24)); }
		});
		// F. Клик по миру сидя (эталон input.c:170-174 → `charToggleSitStand`).
		At(B + 5.5f, [S, WW]()
		{
			UWorld* Wd = WW.Get(); AAsdaPlayerCharacter* C = SelfCh(Wd);
			if (!C) { ++S->Fail; return; }
			Reset(*S, Wd);
			C->SetAnimState(EAsdaPlayerState::Sit);
		});
		At(B + 8.4f, [S, WW, K]()
		{
			UWorld* Wd = WW.Get(); AAsdaPlayerCharacter* C = SelfCh(Wd); AAsdaPlayerController* PC = SelfPC(Wd);
			if (!C || !PC) { ++S->Fail; return; }
			const int32 Before = C->GetEtalonSitState();
			// ★ 24.10.2026 [ИСПРАВЛЕНИЕ САМОТЕСТА]: курсор офлайн-прогона (`-RenderOffscreen`) — это позиция мыши ОС над скрытым окном игры,
			//   разная от прогона к прогону (в журналах: (101,115) — провал, (0,115)/(77,0)/(101,0) — успех). Над окном (MINISTATE_WIN)
			//   `OnLeftClick` по праву выходит гейтом «курсор над окном» ДО ветки сидения — шаг проверял не «клик по миру». Как в шаге C2a
			//   самотеста 4007 (22.10): на время клика интерфейс скрыт (Alt+Z — хит-тест окон молчит), логика клика не трогается.
			UGameInstance* GIf = Wd ? Wd->GetGameInstance() : nullptr;
			UAsdaWindowSubsystem* WMf = GIf ? GIf->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
			UAsdaUiVisibilitySubsystem* Uvf = GIf ? GIf->GetSubsystem<UAsdaUiVisibilitySubsystem>() : nullptr;
			FName ZoneF;
			const bool bOverF = WMf && WMf->IsCursorOverAnyUiWindow(ZoneF);
			const FVector2D CurF = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetCursorPos() : FVector2D::ZeroVector;
			UE_LOG(LogAsdaPkt4024Test, Log, TEXT("САМОТЕСТ-СИДЕНИЕ F: курсор (%.0f,%.0f), над окном: %s"), CurF.X, CurF.Y, bOverF ? *ZoneF.ToString() : TEXT("нет"));
			const bool bUiWasShownF = !Uvf || Uvf->IsUiShown();
			if (Uvf) { Uvf->SetUiShown(false, TEXT("самотест сидения F: курсор офлайн-прогона не над окном")); }
			FName ZoneF2;
			if (WMf && WMf->IsCursorOverAnyUiWindow(ZoneF2)) { S->Expect(false, TEXT("F метод"), FString::Printf(TEXT("курсор всё ещё над окном «%s»"), *ZoneF2.ToString())); }
			S->Mark = S->Caps.Num();
			PC->OnLeftClick(false);
			if (Uvf && bUiWasShownF) { Uvf->SetUiShown(true, TEXT("самотест сидения F: возврат")); }
			const int32 N24 = S->Count(4024);
			if (K) { S->Expect(Before == 8 && N24 == 1 && S->FirstAct() == 109 && C->GetEtalonSitState() == 9 && S->Count(4006) == 0, TEXT("F клик по миру сидя"),
				FString::Printf(TEXT("до %d, 4024 %d шт. код %d, после %d, 4006 %d — ждали 8/1/109/9/0"), Before, N24, S->FirstAct(), C->GetEtalonSitState(), S->Count(4006))); }
			else   { S->Expect(Before == 8 && N24 == 0, TEXT("F клик по миру сидя"), FString::Printf(TEXT("прежний порт: встать пакетом не шлёт (4024 %d) — контроль"), N24)); }
		});
		At(B + 9.0f, [S, WW]() { Reset(*S, WW.Get()); });
	}

	// ═══ ★ 24.10.2026 — G. ЦИКЛ СИДЕНИЯ 108/0 (`charUpdateSabzilAnimAndSendPos@0x9F1310`, `Player_TickSendStatePosOnAnimEnd@0x9F1560`) ═══
	//   Ключ `asda.Char.SitStateSync`: 1 — по концу «садится» (1,5/2 с) ровно один 4024 код 108, по концу «встаёт» — 4024 код 0;
	//   0 — прежний порт (контроль: ни 108, ни 0). G1 сел → сидит → встал; G2 встал (поза лавки 109) ДО границы — 108 нет, конец 9 — 0;
	//   G3 посадку прервал ход — ни 108, ни 0; G4 поза лавки 107/109 — тот же цикл; G5 ключ 0 — контроль.
	auto GStart = [S, WW](int32 SyncKey)
	{
		S->Key = SyncKey;
		AsdaCVarSave::SetForTest(TEXT("asda.Char.SitToggleEtalon"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Char.SitStateSync"), SyncKey);
		Reset(*S, WW.Get());
		S->GMark = S->Caps.Num();
	};
	// G1. Сел (кнопка) → через 1,5–2 с один 108 → встал → по концу «встаёт» один 0.
	At(20.5f, [S, WW, GStart]()
	{
		UWorld* Wd = WW.Get(); AAsdaPlayerCharacter* C = SelfCh(Wd);
		if (!C) { ++S->Fail; return; }
		GStart(1);
		PressButton(Wd);
		S->Expect(S->CountActFrom(S->GMark, 107) == 1 && S->CountActFrom(S->GMark, 108) == 0 && C->GetSitPhase() == 7, TEXT("G1 сел"),
			FString::Printf(TEXT("107 %d шт., 108 %d шт. сразу, фаза %d — ждали 1/0/7"), S->CountActFrom(S->GMark, 107), S->CountActFrom(S->GMark, 108), C->GetSitPhase()));
	});
	At(21.2f, [S, WW]()
	{
		AAsdaPlayerCharacter* C = SelfCh(WW.Get());
		if (!C) { ++S->Fail; return; }
		S->Expect(S->CountActFrom(S->GMark, 108) == 0 && C->GetSitPhase() == 7, TEXT("G1 через 0,7 с"),
			FString::Printf(TEXT("108 %d шт., фаза %d — ждали 0/7 (граница 1,5/2 с ещё не пройдена)"), S->CountActFrom(S->GMark, 108), C->GetSitPhase()));
	});
	At(23.2f, [S, WW]()
	{
		UWorld* Wd = WW.Get(); AAsdaPlayerCharacter* C = SelfCh(Wd);
		if (!C) { ++S->Fail; return; }
		const int32 i107 = S->IndexOfActFrom(S->GMark, 107), i108 = S->IndexOfActFrom(S->GMark, 108);
		const double Dt = (i107 >= 0 && i108 >= 0) ? S->Caps[i108].T - S->Caps[i107].T : -1.0;
		const bool bFace = i108 >= 0 && FVector2D::Distance(S->Caps[i108].V, kD1) < 0.01;
		S->Expect(S->CountActFrom(S->GMark, 108) == 1 && Dt >= 1.4 && Dt <= 2.2 && bFace && C->GetSitPhase() == 8, TEXT("G1 сидит: 108"),
			FString::Printf(TEXT("108 %d шт., через %.2f с после 107, курс %s, фаза %d — ждали 1 шт., 1,5/2 с, курс D1, 8"),
				S->CountActFrom(S->GMark, 108), Dt, bFace ? TEXT("D1") : TEXT("не D1"), C->GetSitPhase()));
		C->FaceGameDir(kD2);
		PressButton(Wd);
		S->Expect(S->CountActFrom(S->GMark, 109) == 1 && C->GetSitPhase() == 9 && S->CountActFrom(S->GMark, 0) == 0, TEXT("G1 встаёт"),
			FString::Printf(TEXT("109 %d шт., фаза %d, 0 %d шт. — ждали 1/9/0"), S->CountActFrom(S->GMark, 109), C->GetSitPhase(), S->CountActFrom(S->GMark, 0)));
	});
	At(28.0f, [S, WW]()
	{
		AAsdaPlayerCharacter* C = SelfCh(WW.Get());
		if (!C) { ++S->Fail; return; }
		const int32 i0 = S->IndexOfActFrom(S->GMark, 0);
		const bool bFace = i0 >= 0 && FVector2D::Distance(S->Caps[i0].V, kD2) < 0.01;
		S->Expect(S->CountActFrom(S->GMark, 0) == 1 && bFace && C->GetSitPhase() == 0 && S->CountActFrom(S->GMark, 108) == 1, TEXT("G1 встал: 0"),
			FString::Printf(TEXT("0 %d шт. (курс %s), фаза %d, 108 за весь цикл %d — ждали 1 шт. с курсом D2, 0, 108 ровно один"),
				S->CountActFrom(S->GMark, 0), bFace ? TEXT("D2") : TEXT("не D2"), C->GetSitPhase(), S->CountActFrom(S->GMark, 108)));
	});
	// G2. Встал ДО границы (поза лавки 109 — без проверки «сидит») — 108 не уходит; конец «встаёт» — 0.
	At(28.5f, [S, WW, GStart]()
	{
		GStart(1);
		PressButton(WW.Get());
	});
	At(29.0f, [S, WW]()
	{
		AAsdaPlayerController* PC = SelfPC(WW.Get()); AAsdaPlayerCharacter* C = SelfCh(WW.Get());
		if (!PC || !C) { ++S->Fail; return; }
		const int32 Before = C->GetSitPhase();
		PC->SendStallStatePose(109, TEXT("самотест G2: встать до границы"));
		S->Expect(Before == 7 && C->GetSitPhase() == 9 && S->CountActFrom(S->GMark, 109) == 1, TEXT("G2 встал до границы"),
			FString::Printf(TEXT("фаза до %d, после %d, 109 %d шт. — ждали 7/9/1"), Before, C->GetSitPhase(), S->CountActFrom(S->GMark, 109)));
	});
	At(31.5f, [S]()
	{
		S->Expect(S->CountActFrom(S->GMark, 108) == 0, TEXT("G2 108 не ушёл"),
			FString::Printf(TEXT("108 %d шт. — ждали 0 (посадка прервана до 1,5/2 с)"), S->CountActFrom(S->GMark, 108)));
	});
	At(34.0f, [S, WW]()
	{
		AAsdaPlayerCharacter* C = SelfCh(WW.Get());
		S->Expect(S->CountActFrom(S->GMark, 0) == 1 && S->CountActFrom(S->GMark, 108) == 0 && C && C->GetSitPhase() == 0, TEXT("G2 конец «встаёт»: 0"),
			FString::Printf(TEXT("0 %d шт., 108 %d шт., фаза %d — ждали 1/0/0"), S->CountActFrom(S->GMark, 0), S->CountActFrom(S->GMark, 108), C ? C->GetSitPhase() : -1));
	});
	// G3. Посадку прервал ход (смена состояния до границы) — ни 108, ни 0.
	At(34.5f, [S, WW, GStart]()
	{
		GStart(1);
		PressButton(WW.Get());
	});
	At(35.0f, [S, WW]() { Reset(*S, WW.Get()); });
	At(37.5f, [S, WW]()
	{
		AAsdaPlayerCharacter* C = SelfCh(WW.Get());
		S->Expect(S->CountActFrom(S->GMark, 107) == 1 && S->CountActFrom(S->GMark, 108) == 0 && S->CountActFrom(S->GMark, 0) == 0 && C && C->GetSitPhase() == 0,
			TEXT("G3 посадку прервал ход"), FString::Printf(TEXT("107 %d, 108 %d, 0 %d, фаза %d — ждали 1/0/0/0"),
				S->CountActFrom(S->GMark, 107), S->CountActFrom(S->GMark, 108), S->CountActFrom(S->GMark, 0), C ? C->GetSitPhase() : -1));
	});
	// G4. Поза лавки 107 → тот же цикл: 108 через 1,5/2 с; 109 → по концу «встаёт» 0.
	At(38.0f, [S, WW, GStart]()
	{
		AAsdaPlayerController* PC = SelfPC(WW.Get());
		if (!PC) { ++S->Fail; return; }
		GStart(1);
		PC->SendStallStatePose(107, TEXT("самотест G4: лавка"));
	});
	At(40.7f, [S, WW]()
	{
		AAsdaPlayerController* PC = SelfPC(WW.Get()); AAsdaPlayerCharacter* C = SelfCh(WW.Get());
		if (!PC || !C) { ++S->Fail; return; }
		S->Expect(S->CountActFrom(S->GMark, 107) == 1 && S->CountActFrom(S->GMark, 108) == 1 && C->GetSitPhase() == 8, TEXT("G4 лавка 107: 108"),
			FString::Printf(TEXT("107 %d, 108 %d, фаза %d — ждали 1/1/8"), S->CountActFrom(S->GMark, 107), S->CountActFrom(S->GMark, 108), C->GetSitPhase()));
		PC->SendStallStatePose(109, TEXT("самотест G4: лавка закрыта"));
	});
	At(45.0f, [S, WW]()
	{
		AAsdaPlayerCharacter* C = SelfCh(WW.Get());
		S->Expect(S->CountActFrom(S->GMark, 109) == 1 && S->CountActFrom(S->GMark, 0) == 1 && S->CountActFrom(S->GMark, 108) == 1 && C && C->GetSitPhase() == 0,
			TEXT("G4 лавка 109: 0"), FString::Printf(TEXT("109 %d, 0 %d, 108 %d, фаза %d — ждали 1/1/1/0"),
				S->CountActFrom(S->GMark, 109), S->CountActFrom(S->GMark, 0), S->CountActFrom(S->GMark, 108), C ? C->GetSitPhase() : -1));
	});
	// G5. Контроль: ключ 0 — прежний порт, ни 108, ни 0 (107/109 по-прежнему уходят).
	At(45.5f, [S, WW, GStart]()
	{
		GStart(0);
		PressButton(WW.Get());
	});
	At(48.2f, [S, WW]()
	{
		UWorld* Wd = WW.Get(); AAsdaPlayerCharacter* C = SelfCh(Wd);
		if (!C) { ++S->Fail; return; }
		S->Expect(S->CountActFrom(S->GMark, 107) == 1 && S->CountActFrom(S->GMark, 108) == 0 && C->GetSitPhase() == 8, TEXT("G5 сидит"),
			FString::Printf(TEXT("прежний порт: 107 %d, 108 %d, фаза %d — ждали 1/0/8 (контроль)"), S->CountActFrom(S->GMark, 107), S->CountActFrom(S->GMark, 108), C->GetSitPhase()));
		PressButton(Wd);
	});
	At(52.5f, [S, WW]()
	{
		AAsdaPlayerCharacter* C = SelfCh(WW.Get());
		S->Expect(S->CountActFrom(S->GMark, 109) == 1 && S->CountActFrom(S->GMark, 0) == 0 && C && C->GetSitPhase() == 0, TEXT("G5 встал"),
			FString::Printf(TEXT("прежний порт: 109 %d, 0 %d, фаза %d — ждали 1/0/0 (контроль)"), S->CountActFrom(S->GMark, 109), S->CountActFrom(S->GMark, 0), C ? C->GetSitPhase() : -1));
		Reset(*S, WW.Get());
	});

	// ═══ ★ 24.10.2026 — H. ВЫХОД ИЗ СИДЕНИЯ 1:1 (ключ `asda.Char.SitExitEtalon`; разбор у `AAsdaPlayerCharacter::IsSitExitEtalon`) ═══
	//   H1 эмоция сидя (8) и в «встаёт» (9) — у эталона гейт «состояние == 0»: ни клипа, ни 4024;
	//   H2 эмоция поверх сидения (`CChar_SetState(…, 34)`, путь соседа по 4025) — сидение снято сразу, по концу клипа ПОКОЙ, кодов нет;
	//   H3 замах поверх сидения (`CChar_SetState(…, 5/15)`) — то же; H4 вход в магазин снаряжения сидя (`tradeWinConfirmSubmit`) —
	//   «встаёт» БЕЗ 109, по концу — 4024 0; H5 вход в ремесло сидя (`CharFx(0)`) — сразу покой, кодов нет (ни 109, ни 0);
	//   H6 контроль ключом 0 — прежний порт: по концу эмоции снова «сидит».
	auto HStart = [S, WW](int32 ExitKey)
	{
		S->Key = ExitKey;
		AsdaCVarSave::SetForTest(TEXT("asda.Char.SitToggleEtalon"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Char.SitStateSync"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Char.SitExitEtalon"), ExitKey);
		Reset(*S, WW.Get());
		S->GMark = S->Caps.Num();
	};
	auto Emote = [](UWorld* Wd, int32 Idx)
	{
		UAsdaCharaInfoSubsystem* CI = (Wd && Wd->GetGameInstance()) ? Wd->GetGameInstance()->GetSubsystem<UAsdaCharaInfoSubsystem>() : nullptr;
		if (CI) { CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(0, Idx)); }
	};
	// H1. Эмоция сидя и в «встаёт».
	At(53.0f, [S, WW, HStart]() { HStart(1); PressButton(WW.Get()); });
	At(55.6f, [S, WW, Emote]()
	{
		UWorld* Wd = WW.Get(); AAsdaPlayerCharacter* C = SelfCh(Wd);
		if (!C) { ++S->Fail; return; }
		S->Mark = S->Caps.Num();
		Emote(Wd, 0);
		S->Expect(S->Count(4024) == 0 && C->GetEtalonSitState() == 8 && !C->IsEmotionAnimActive(), TEXT("H1 эмоция сидя (8)"),
			FString::Printf(TEXT("4024 %d, состояние %d, клип эмоции %d — ждали 0/8/0 (гейт эталона «состояние 0»)"), S->Count(4024), C->GetEtalonSitState(), C->IsEmotionAnimActive() ? 1 : 0));
		PressButton(Wd);   // встать: 9
		S->Mark = S->Caps.Num();
		Emote(Wd, 0);
		S->Expect(S->Count(4024) == 0 && C->GetSitPhase() == 9 && !C->IsEmotionAnimActive(), TEXT("H1 эмоция в «встаёт» (9)"),
			FString::Printf(TEXT("4024 %d, фаза %d, клип эмоции %d — ждали 0/9/0 (прежний порт эмоцию разрешал)"), S->Count(4024), C->GetSitPhase(), C->IsEmotionAnimActive() ? 1 : 0));
	});
	// H2. Эмоция поверх сидения (путь соседа: `PlayEmotionClip` = `CChar_SetState(…, 34)`).
	At(58.5f, [S, WW, HStart]() { HStart(1); if (AAsdaPlayerCharacter* C = SelfCh(WW.Get())) { C->SetAnimState(EAsdaPlayerState::Sit); } });
	At(61.0f, [S, WW]()
	{
		AAsdaPlayerCharacter* C = SelfCh(WW.Get());
		if (!C) { ++S->Fail; return; }
		const int32 Before = C->GetEtalonSitState();
		S->GMark = S->Caps.Num();
		const float Len = C->PlayEmotionClip(34);
		S->Expect(Before == 8 && Len > 0.f && C->GetSitPhase() == 0 && C->GetEtalonSitState() == 0 && C->AnimState == EAsdaPlayerState::Idle && C->IsEmotionAnimActive(),
			TEXT("H2 эмоция поверх сидения"), FString::Printf(TEXT("до %d, клип %.2f с, фаза %d, сидячее %d, покой %d, эмоция %d — ждали 8/>0/0/0/1/1"),
				Before, Len, C->GetSitPhase(), C->GetEtalonSitState(), C->AnimState == EAsdaPlayerState::Idle ? 1 : 0, C->IsEmotionAnimActive() ? 1 : 0));
	});
	At(65.7f, [S, WW]()
	{
		AAsdaPlayerCharacter* C = SelfCh(WW.Get());
		if (!C) { ++S->Fail; return; }
		S->Expect(!C->IsEmotionAnimActive() && C->AnimState == EAsdaPlayerState::Idle && C->GetEtalonSitState() == 0 && S->Caps.Num() == S->GMark, TEXT("H2 конец эмоции"),
			FString::Printf(TEXT("эмоция %d, покой %d, сидячее %d, 4024 после эмоции %d — ждали 0/1/0/0 (стоит, кодов нет)"), C->IsEmotionAnimActive() ? 1 : 0,
				C->AnimState == EAsdaPlayerState::Idle ? 1 : 0, C->GetEtalonSitState(), S->Caps.Num() - S->GMark));
	});
	// H3. Замах поверх сидения (`StartAttackSwing` = `CChar_SetState(…, 5/15)`).
	At(66.0f, [S, WW, HStart]() { HStart(1); if (AAsdaPlayerCharacter* C = SelfCh(WW.Get())) { C->SetAnimState(EAsdaPlayerState::Sit); } });
	At(68.5f, [S, WW]()
	{
		AAsdaPlayerCharacter* C = SelfCh(WW.Get());
		if (!C) { ++S->Fail; return; }
		const int32 Before = C->GetEtalonSitState();
		S->GMark = S->Caps.Num();
		C->StartAttackSwing(C->EquippedWeaponSubType, 0.3f);
		S->Expect(Before == 8 && C->IsAttackAnimActive() && C->GetSitPhase() == 0 && C->AnimState == EAsdaPlayerState::Idle, TEXT("H3 замах поверх сидения"),
			FString::Printf(TEXT("до %d, замах %d, фаза %d, покой %d — ждали 8/1/0/1"), Before, C->IsAttackAnimActive() ? 1 : 0, C->GetSitPhase(), C->AnimState == EAsdaPlayerState::Idle ? 1 : 0));
	});
	At(73.4f, [S, WW]()
	{
		AAsdaPlayerCharacter* C = SelfCh(WW.Get());
		if (!C) { ++S->Fail; return; }
		// ★ 07.11.2026: проверка уточнена. Сидячих кодов (107/108/109/0) нет — это и есть смысл H3: запись 5 поверх сидения снимает его без «встаёт».
		//   Единственный 4024 здесь — 400, выход из действия 5: `CCharacter_SetAction` case 0 при прошлом действии 5 шлёт его (entity_char.c:16545-16556),
		//   порт — с правила 01.11 (`OnAttackClipEnd`). Прежнее «4024 == 0» написано 24.10, до этого правила, и считало 400 сидячим кодом.
		const int32 SitCodes = S->CountActFrom(S->GMark, 107) + S->CountActFrom(S->GMark, 108) + S->CountActFrom(S->GMark, 109) + S->CountActFrom(S->GMark, 0);
		S->Expect(!C->IsAttackAnimActive() && C->AnimState == EAsdaPlayerState::Idle && C->GetEtalonSitState() == 0 && SitCodes == 0
			&& S->CountActFrom(S->GMark, 400) == 1 && S->Count4024From(S->GMark) == 1, TEXT("H3 конец замаха"),
			FString::Printf(TEXT("замах %d, покой %d, сидячее %d, сидячих кодов %d, 400 %d шт., 4024 всего %d — ждали 0/1/0/0/1/1"), C->IsAttackAnimActive() ? 1 : 0, C->AnimState == EAsdaPlayerState::Idle ? 1 : 0,
				C->GetEtalonSitState(), SitCodes, S->CountActFrom(S->GMark, 400), S->Count4024From(S->GMark)));
		C->StopAttackAnim();   // уборка: цикл замаха не должен пережить шаг
	});
	// H4. Магазин снаряжения сидя: «встаёт» без 109, по концу — 0.
	At(74.5f, [S, WW, HStart]() { HStart(1); if (AAsdaPlayerCharacter* C = SelfCh(WW.Get())) { C->SetAnimState(EAsdaPlayerState::Sit); } });
	At(77.0f, [S, WW]()
	{
		UWorld* Wd = WW.Get(); AAsdaPlayerCharacter* C = SelfCh(Wd);
		UAsdaEquipShopSubsystem* Es = (Wd && Wd->GetGameInstance()) ? Wd->GetGameInstance()->GetSubsystem<UAsdaEquipShopSubsystem>() : nullptr;
		if (!C || !Es) { ++S->Fail; return; }
		const int32 Before = C->GetEtalonSitState();
		S->GMark = S->Caps.Num();
		Es->OpenWindow();
		S->Expect(Before == 8 && Es->IsOpen() && C->GetSitPhase() == 9 && S->CountActFrom(S->GMark, 109) == 0, TEXT("H4 магазин снаряжения сидя"),
			FString::Printf(TEXT("до %d, окно %d, фаза %d, 109 %d — ждали 8/1/9/0 (CharFx 9 без 109)"), Before, Es->IsOpen() ? 1 : 0, C->GetSitPhase(), S->CountActFrom(S->GMark, 109)));
	});
	At(79.5f, [S, WW]()
	{
		UWorld* Wd = WW.Get(); AAsdaPlayerCharacter* C = SelfCh(Wd);
		UAsdaEquipShopSubsystem* Es = (Wd && Wd->GetGameInstance()) ? Wd->GetGameInstance()->GetSubsystem<UAsdaEquipShopSubsystem>() : nullptr;
		S->Expect(C && C->GetSitPhase() == 0 && S->CountActFrom(S->GMark, 0) == 1 && S->Count4024From(S->GMark) == 1, TEXT("H4 конец «встаёт»"),
			FString::Printf(TEXT("фаза %d, 0 %d шт., 4024 всего %d — ждали 0/1/1"), C ? C->GetSitPhase() : -1, S->CountActFrom(S->GMark, 0), S->Count4024From(S->GMark)));
		if (Es && Es->IsOpen()) { Es->CloseWindow(); }
	});
	// H5. Ремесло сидя (`CharFx(0)`): сразу покой, кодов нет.
	At(80.0f, [S, WW, HStart]() { HStart(1); if (AAsdaPlayerCharacter* C = SelfCh(WW.Get())) { C->SetAnimState(EAsdaPlayerState::Sit); } });
	At(82.5f, [S, WW]()
	{
		UWorld* Wd = WW.Get(); AAsdaPlayerCharacter* C = SelfCh(Wd);
		UAsdaCraftWindowSubsystem* Cw = (Wd && Wd->GetGameInstance()) ? Wd->GetGameInstance()->GetSubsystem<UAsdaCraftWindowSubsystem>() : nullptr;
		if (!C || !Cw) { ++S->Fail; return; }
		const int32 Before = C->GetEtalonSitState();
		S->GMark = S->Caps.Num();
		const bool bOpened = Cw->GateAndToggle(TEXT("самотест H5"));
		S->Expect(Before == 8 && bOpened && Cw->IsOpen() && C->GetSitPhase() == 0 && C->AnimState == EAsdaPlayerState::Idle, TEXT("H5 ремесло сидя"),
			FString::Printf(TEXT("до %d, окно %d, фаза %d, покой %d — ждали 8/1/0/1 (CharFx 0: без «встаёт»)"), Before, Cw->IsOpen() ? 1 : 0, C->GetSitPhase(),
				C->AnimState == EAsdaPlayerState::Idle ? 1 : 0));
	});
	At(85.0f, [S, WW]()
	{
		UWorld* Wd = WW.Get();
		UAsdaCraftWindowSubsystem* Cw = (Wd && Wd->GetGameInstance()) ? Wd->GetGameInstance()->GetSubsystem<UAsdaCraftWindowSubsystem>() : nullptr;
		S->Expect(S->Count4024From(S->GMark) == 0, TEXT("H5 коды"), FString::Printf(TEXT("4024 после входа %d — ждали 0 (ни 109, ни 0)"), S->Count4024From(S->GMark)));
		if (Cw && Cw->IsOpen()) { Cw->OnCloseClicked(); }   // закрытие путём игрока (6. снимок окон вернётся)
	});
	// H6. Контроль ключом 0: прежний порт — по концу эмоции снова «сидит».
	At(85.5f, [S, WW, HStart]() { HStart(0); if (AAsdaPlayerCharacter* C = SelfCh(WW.Get())) { C->SetAnimState(EAsdaPlayerState::Sit); } });
	At(88.0f, [S, WW]() { if (AAsdaPlayerCharacter* C = SelfCh(WW.Get())) { C->PlayEmotionClip(34); } });
	At(92.7f, [S, WW]()
	{
		AAsdaPlayerCharacter* C = SelfCh(WW.Get());
		S->Expect(C && C->AnimState == EAsdaPlayerState::Sit && C->GetEtalonSitState() == 8, TEXT("H6 ключ 0"),
			FString::Printf(TEXT("прежний порт: по концу эмоции сидячее %d — ждали 8 (контроль)"), C ? C->GetEtalonSitState() : -1));
		Reset(*S, WW.Get());
	});

	// ═══ ★ 25.10.2026 — I. ОТВЕТЫ СЕРВЕРА СИДЯЩЕМУ (ревизия sit108: ВАЖНО-1, М-2, М-3) — штатный приём `DebugFeedPacket` ═══
	//   I1 свой 4009 при «садится» (7) и «сидит» (8) — только позиция (`Recv_EntityStop`): фаза цела, 108 уходит по порогу, кодов сверх нет;
	//   I2 свой 4007 код 5 сидя — сразу покой (`CharFx(0)`), позиция сервера, без «встаёт» и без 0; I3 «занят» (автоатака) —
	//   магазин снаряжения не открывается, стопа сцены нет; I4 контроль ключом 0 — 4009 поднимает сидящего (прежний порт).
	auto MoveBody = [](uint8 Res, int16 Uidx, const FVector2D& Cur)
	{
		TArray<uint8> B; B.SetNumZeroed(27);   // раскладка `Recv_EntityMove@0xF3C2C0` (как самотест 4007)
		B[0] = Res;
		FMemory::Memcpy(&B[1], &Uidx, 2);
		const float Cx = (float)Cur.X, Cz = (float)Cur.Y;
		FMemory::Memcpy(&B[5], &Cx, 4);  FMemory::Memcpy(&B[9], &Cz, 4);
		FMemory::Memcpy(&B[13], &Cx, 4); FMemory::Memcpy(&B[17], &Cz, 4);
		return B;
	};
	// I1. Свой 4009 сидя.
	At(93.5f, [S, WW, HStart]() { HStart(1); if (AAsdaPlayerCharacter* C = SelfCh(WW.Get())) { C->SetAnimState(EAsdaPlayerState::Sit); } });
	At(94.0f, [S, WNet, WW, MoveBody]()
	{
		UAsdaNetSubsystem* N = WNet.Get(); AAsdaPlayerCharacter* C = SelfCh(WW.Get());
		if (!N || !C) { ++S->Fail; return; }
		const int32 Before = C->GetSitPhase();
		N->DebugFeedPacket(4009, MoveBody(0, (int16)N->SelfUserNum, C->GetGamePosXZ()));
		S->Expect(Before == 7 && C->GetSitPhase() == 7 && C->AnimState == EAsdaPlayerState::Sit && S->Count4024From(S->GMark) == 0, TEXT("I1 4009 при «садится»"),
			FString::Printf(TEXT("фаза до %d, после %d, 4024 %d — ждали 7/7/0 (только позиция, эталон Recv_EntityStop)"), Before, C->GetSitPhase(), S->Count4024From(S->GMark)));
	});
	At(96.3f, [S, WNet, WW, MoveBody]()
	{
		UAsdaNetSubsystem* N = WNet.Get(); AAsdaPlayerCharacter* C = SelfCh(WW.Get());
		if (!N || !C) { ++S->Fail; return; }
		S->Expect(C->GetSitPhase() == 8 && S->CountActFrom(S->GMark, 108) == 1, TEXT("I1 108 после 4009"),
			FString::Printf(TEXT("фаза %d, 108 %d — ждали 8/1 (посадка не прервана)"), C->GetSitPhase(), S->CountActFrom(S->GMark, 108)));
		N->DebugFeedPacket(4009, MoveBody(0, (int16)N->SelfUserNum, C->GetGamePosXZ()));
		S->Expect(C->GetSitPhase() == 8 && C->GetEtalonSitState() == 8, TEXT("I1 4009 при «сидит»"), FString::Printf(TEXT("фаза %d — ждали 8"), C->GetSitPhase()));
	});
	At(98.0f, [S]()
	{
		S->Expect(S->Count4024From(S->GMark) == 1, TEXT("I1 коды"), FString::Printf(TEXT("4024 за сценарий %d — ждали 1 (только 108; ни «встаёт», ни 0)"), S->Count4024From(S->GMark)));
	});
	// I2. Свой 4007 код 5 сидя: сразу покой в точке сервера.
	At(98.2f, [S, WNet, WW, MoveBody]()
	{
		UAsdaNetSubsystem* N = WNet.Get(); AAsdaPlayerCharacter* C = SelfCh(WW.Get());
		if (!N || !C) { ++S->Fail; return; }
		S->GMark = S->Caps.Num();
		const FVector2D Dst = S->Home + FVector2D(60.0, 0.0);
		N->DebugFeedPacket(4007, MoveBody(5, (int16)N->SelfUserNum, Dst));
		S->Expect(C->GetSitPhase() == 0 && C->AnimState == EAsdaPlayerState::Idle && FVector2D::Distance(C->GetGamePosXZ(), Dst) < 5.0, TEXT("I2 4007/5 сидя"),
			FString::Printf(TEXT("фаза %d, покой %d, до точки сервера %.1f — ждали 0/1/<5 (CharFx 0: без «встаёт»)"), C->GetSitPhase(),
				C->AnimState == EAsdaPlayerState::Idle ? 1 : 0, FVector2D::Distance(C->GetGamePosXZ(), Dst)));
	});
	At(100.5f, [S]()
	{
		S->Expect(S->Count4024From(S->GMark) == 0, TEXT("I2 коды"), FString::Printf(TEXT("4024 после поправки %d — ждали 0 (ни 109, ни 0)"), S->Count4024From(S->GMark)));
	});
	// I3. «Занят» — сцена не открывается, стопа нет.
	At(100.7f, [S, WW, HStart]()
	{
		UWorld* Wd = WW.Get(); AAsdaPlayerController* PC = SelfPC(Wd);
		UAsdaEquipShopSubsystem* Es = (Wd && Wd->GetGameInstance()) ? Wd->GetGameInstance()->GetSubsystem<UAsdaEquipShopSubsystem>() : nullptr;
		if (!PC || !Es) { ++S->Fail; return; }
		HStart(1);
		PC->bAutoAttack = true;   // `IsLocalPlayerBusy` — эталонное «занят» (бой)
		Es->OpenWindow();
		S->Expect(!Es->IsOpen() && S->Caps.Num() == S->GMark, TEXT("I3 занят: магазин снаряжения"),
			FString::Printf(TEXT("окно %d, пакетов %d — ждали 0/0 (гейт objStateAllowsRender: нотис 6946, без стопа)"), Es->IsOpen() ? 1 : 0, S->Caps.Num() - S->GMark));
		PC->bAutoAttack = false;
		if (Es->IsOpen()) { Es->CloseWindow(); }
	});
	// I4. Контроль ключом 0: 4009 поднимает сидящего (прежний порт).
	At(101.0f, [S, WW, HStart]() { HStart(0); if (AAsdaPlayerCharacter* C = SelfCh(WW.Get())) { C->SetAnimState(EAsdaPlayerState::Sit); } });
	At(103.5f, [S, WNet, WW, MoveBody]()
	{
		UAsdaNetSubsystem* N = WNet.Get(); AAsdaPlayerCharacter* C = SelfCh(WW.Get());
		if (!N || !C) { ++S->Fail; return; }
		N->DebugFeedPacket(4009, MoveBody(0, (int16)N->SelfUserNum, C->GetGamePosXZ()));
		S->Expect(C->GetSitPhase() == 9, TEXT("I4 ключ 0"), FString::Printf(TEXT("прежний порт: 4009 → «встаёт», фаза %d — ждали 9 (контроль)"), C->GetSitPhase()));
	});
	At(106.0f, [S, WW]() { Reset(*S, WW.Get()); });

	At(106.5f, [S, WNet, WW, bQuit]()
	{
		UAsdaNetSubsystem* N = WNet.Get(); UWorld* Wd = WW.Get();
		if (N) { AsdaSelfTestKit::DisarmCapture(N, S->Gen); N->bInGameConnected = S->bSavedInGame; N->bActionPending = false; }
		// ★ 24.10.2026 (гигиена пакета): окно действий, открытое подготовкой, закрываем — следующий самотест пакета (Л5 «характеристики»)
		//   застал бы его открытым и проверял бы закрытие вместо открытия.
		if (S->bOpenedCharaInfo && Wd && Wd->GetGameInstance())
		{
			if (UAsdaCharaInfoSubsystem* CI = Wd->GetGameInstance()->GetSubsystem<UAsdaCharaInfoSubsystem>()) { if (CI->IsWindowOpen()) { CI->CloseWindow(); } }
		}
		AsdaCVarSave::RestoreAll(TEXT("asda.Net.SelfTestSitStand"));   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи Char.SitToggleEtalon и (★ 24.10) Char.SitStateSync / Char.SitExitEtalon — значение + SetBy
		UE_LOG(LogAsdaPkt4024Test, Log, TEXT("САМОТЕСТ-СИДЕНИЕ ИТОГ: ok=%d fail=%d"), S->Ok, S->Fail);
		AsdaSelfTestKit::Quit(Wd, bQuit);
	});
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestSitStand(
	TEXT("asda.Net.SelfTestSitStand"),
	TEXT("Самотест «сесть/встать» по эталону charToggleSitStand: на ходу 4008 перед 4024, «занят», садится (7), сидит (8) и иконка, "
	     "клавиша 55, клик по миру сидя — при asda.Char.SitToggleEtalon 1 и 0 (контроль); цикл 108 «сидит» / 0 «встал» (сел, встал до границы, "
	     "прерванная посадка, лавка, ключ asda.Char.SitStateSync 0); выход из сидения 1:1 (эмоция сидя/в «встаёт», эмоция и замах поверх сидения, "
	     "магазин снаряжения и ремесло сидя, ключ asda.Char.SitExitEtalon 0); ответы сервера сидящему (4009, 4007/5, «занят» у сцены). Полный перехват, только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Net.SelfTestSitStand"));   // ★ С1 (ревизия wave0_REVIEW.md, 11.10.2026): мир ушёл до итога — ключи вернёт сторож
		FAsdaPkt4024SelfTest::RunSitStand(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
