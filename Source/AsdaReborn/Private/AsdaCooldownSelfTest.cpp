// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ ОТКАТОВ ПРЕДМЕТОВ (25.10.2026, жалоба владельца «отсчёт на иконке маунта fable не совпадает с реальным, после
//   перекладки дооткатывается»): `asda.SelfTest.Cooldowns [quit]`. Офлайн (`-AsdaNoNet`), на карте мира.
//   Реальный путь (закон diagnostic-must-use-real-path): применение — ответами 5451 (премиум/маунт) и 5029 (расходник) через
//   очередь инвентаря, восстановление — настоящим кадром 5465 через общий разбор входящих (`DebugFeedPacket`), портал —
//   настоящий `OpenLevel`. Шапка исправлений — `AsdaHotbarSubsystem.cpp`, «ОТКАТЫ: ЧАСЫ, ПРОГОН ШТОРКИ, СНИМОК 5465».
//   Проверяются:
//     A) взвод: маунт fable 609 (special_itemlist 30000 мс, группа 5) и зелье (item_list) — длительность и группа по таблицам,
//        групповой откат гасит стоковый маунт той же группы и не трогает чужую группу;
//     B) часы: рывок кадра (сон 2 с) — реестр (предметы и книга навыков) отсчитывает РЕАЛЬНОЕ время; мировое время за тот
//        же кадр ушло не больше чем на 0,4 с (кламп движка) — прежний дефект виден числом; при ключе RealClock 0 реестр стоит;
//     C) прогон шторки (общая `DriveCurtain` всех окон): посреди отката кадр 117 достигается ровно в остаток; при ключе
//        CurtainTotal 0 — прежнее раннее гашение (числом); догонка доигравшей шторки (ключ CurtainResync);
//     D) перекладка сумка→сумка и сумка→панель: шторка новой ячейки/слота горит с тем же остатком, что у реестра;
//     E) портал (`OpenLevel`): остаток пережил смену мира без 5465; затем 5465 — полный снимок (лишние записи сняты,
//        остаток = серверный, книга навыков тоже снимком);
//     F) вход в мир с остатком: полный сброс сессии → 5465 → остаток и стартовый кадр шторки от сервера.
//   Итог: «ОТКАТЫ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaSessionReset.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaSkillWindowSubsystem.h"
#include "UI/AsdaAnimatedPlane.h"
#include "UI/AsdaUiHelpers.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Kismet/GameplayStatics.h"   // OpenLevel — настоящая смена карты
#include "Containers/Ticker.h"        // продолжение после OpenLevel (таймеры прежнего мира гибнут)
#include "Internationalization/Regex.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCdTest, Log, All);

// Единые часы реестра откатов (AsdaHotbarSubsystem.cpp).
double AsdaCooldownNowS(const UObject* Ctx);

namespace AsdaCdTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.Cooldowns");
	constexpr int32 kMount   = 609;     // маунт fable (P82 на сервере: 30000 мс)
	constexpr int32 kStockMt = 522;     // стоковый магазинный маунт той же группы 5 (образец fable-строк)
	constexpr int32 kOtherGr = 465;     // премиум-предмет другой группы (6)
	constexpr int32 kBagCont = 1;       // сумка снаряжения (ShellInven) — туда кладутся маунты
	constexpr int32 kPotCont = 2;       // расходники

	// Состояние прогона (живёт между тиками и через смену мира).
	struct FState
	{
		TWeakObjectPtr<UGameInstance> GI;    // экземпляр игры (переживает OpenLevel)
		bool   bQuit = false;                // `quit` в конце
		int32  Ok = 0, Fail = 0;             // счёт
		int32  Stage = 0;                    // шаг конечного автомата
		double StageT = 0.0;                 // реальное время входа в шаг
		double TUse = 0.0;                   // реальное время применения маунта (откат 30 с от него)
		double TotalS = 30.0;                // длительность отката маунта по таблице
		double RealT0 = 0.0, WorldT0 = 0.0;  // начало «рывка»
		double WorldBeforePortal = 0.0;      // мировое время старого мира перед порталом
		UPTRINT OldWorld = 0;                // адрес старого мира (дождаться нового)
		int32  Potion = 0;                   // код зелья с откатом
		int32  BagCell = 3;                  // текущая ячейка маунта в сумке
		double T5465 = 0.0;                  // реальное время подачи 5465 после портала
	};

	static void Check(FState& S, bool b, const FString& What)
	{
		if (b) { ++S.Ok;   UE_LOG(LogAsdaCdTest, Log,   TEXT("ОТКАТЫ ok   %s"), *What); }
		else   { ++S.Fail; UE_LOG(LogAsdaCdTest, Error, TEXT("ОТКАТЫ FAIL %s"), *What); }
	}

	// Время до кадра 117 у плана после перезапуска (кадр = floor(e/I·117) + start, AsdaAnimatedPlane::TickAnim).
	static double CurtainEndS(const UAsdaAnimatedPlane* P)
	{
		const int32 MaxIdx = P ? P->TotalFrame - 1 : 0;
		if (!P || MaxIdx <= 0) { return -1.0; }
		return (double)P->FrameIntervalMs * (double)(MaxIdx - P->StartFrame) / (double)MaxIdx / 1000.0;
	}

	// Тело 5465 (360 б): навыки и предметы — пары (X = код, Y = остаток сек), терминаторы −1.
	static TArray<uint8> Make5465(const TArray<FIntPoint>& Skills, const TArray<FIntPoint>& Items)
	{
		TArray<uint8> B; B.SetNumZeroed(360);
		auto Fill = [&B](int32 Base, const TArray<FIntPoint>& L)
		{
			int32 J = 0;
			for (; J < L.Num() && J < 30; ++J)
			{
				AsdaBytes::WrI32(B.GetData() + Base + 6 * J, L[J].X);
				AsdaBytes::WrI16(B.GetData() + Base + 6 * J + 4, L[J].Y);
			}
			if (J < 30) { AsdaBytes::WrI32(B.GetData() + Base + 6 * J, -1); }
		};
		Fill(0, Skills);
		Fill(180, Items);
		return B;
	}

	// Ответ 5451 «успех» на применение премиум-предмета (как `asda.Inven.Recv5451`): запись 57 б с кодом/контейнером/ячейкой.
	static void Feed5451(UAsdaInventorySubsystem* Inv, UAsdaNetSubsystem* Net, int32 Code, int32 Cont, int32 Cell)
	{
		TArray<uint8> B; B.SetNumZeroed(68);
		B[0] = 1;
		AsdaBytes::WrI16(&B[1], Net ? (int32)AsdaTestNet::SelfUserNum(Net) : 0);
		uint8* Rec = B.GetData() + 3;
		AsdaBytes::WrI32(Rec + 0, Code);
		Rec[4] = (uint8)Cont;
		AsdaBytes::WrI16(Rec + 5, Cell);
		AsdaBytes::WrI16(Rec + 7, -1);
		AsdaBytes::WrI32(Rec + 9, 1);
		Inv->SimulateFunctionResult(B);
		Inv->DebugPumpPendingPackets();
	}

	// Остаток шторки из дампа окна: строка «<Tag> <Номер> предмет <Код> состояние 1 осталось X с» (инвентарь)
	//   или «абс.слот <Номер> тип 1 id <Код> состояние 1 осталось X с» (панель). −1 — строки нет.
	static double DumpRemain(const FString& Dump, const FString& Pattern)
	{
		const FRegexPattern Rx(Pattern);
		FRegexMatcher M(Rx, Dump);
		if (M.FindNext()) { return FCString::Atod(*M.GetCaptureGroup(1)); }
		return -1.0;
	}

	static double Expected(const FState& S) { return S.TotalS - (FPlatformTime::Seconds() - S.TUse); }

	static void Finish(FState& S, UWorld* W)
	{
		if (UGameInstance* G = S.GI.Get())
		{
			if (UAsdaInventorySubsystem* Inv = G->GetSubsystem<UAsdaInventorySubsystem>())
			{
				Inv->DebugPutItem(kBagCont, S.BagCell, 0, 0);
				if (S.Potion > 0) { Inv->DebugPutItem(kPotCont, 5, 0, 0); }
			}
			if (UAsdaHotbarSubsystem* Hb = G->GetSubsystem<UAsdaHotbarSubsystem>()) { Hb->ClearAllCooldowns(); }
			if (UAsdaSkillWindowSubsystem* Sw = G->GetSubsystem<UAsdaSkillWindowSubsystem>()) { Sw->ResetAllSkillCooldowns(); }
		}
		AsdaSelfTestKit::Finish(W, kCmd, S.bQuit, LogAsdaCdTest, TEXT("ОТКАТЫ"), S.Ok, S.Fail);
	}

	// ══════════ СИНХРОННАЯ ЧАСТЬ: A, B (сон в кадре), C ══════════
	static bool RunSync(FState& S, UWorld* W)
	{
		UGameInstance* GI = S.GI.Get();
		UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
		UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		UAsdaSkillWindowSubsystem* Sw = GI ? GI->GetSubsystem<UAsdaSkillWindowSubsystem>() : nullptr;
		const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		UAsdaNetSubsystem* Net = GI ? AsdaTestNet::Get(GI) : nullptr;
		if (!Hb || !Inv || !Sw || !IT || !Net) { Check(S, false, TEXT("0 подсистемы")); return false; }

		// Ключи — на значения исправления (умолчание 1), демонстрации прежнего ниже ставят 0 и возвращают 1.
		for (const TCHAR* K : { TEXT("asda.Cooldown.RealClock"), TEXT("asda.Cooldown.CurtainTotal"),
		                        TEXT("asda.Cooldown.CurtainResync"), TEXT("asda.Cooldown.Snapshot5465") })
		{
			AsdaCVarSave::SetForTest(K, 1);
		}
		Hb->ClearAllCooldowns();
		Sw->ResetAllSkillCooldowns();

		// ── 0. Таблицы ──
		FAsdaSpecialItemlistRow Mt, Stock, Other;
		const bool bMt = IT->GetSpecialItem(kMount, Mt), bSt = IT->GetSpecialItem(kStockMt, Stock), bOt = IT->GetSpecialItem(kOtherGr, Other);
		Check(S, bMt && Mt.CooldownBase == 30000 && Mt.LinkGroupId == 5,
			FString::Printf(TEXT("A0 таблица: %d откат %d мс, группа %d (сервер P82 FuncLog: CoolTime:30000)"), kMount, Mt.CooldownBase, Mt.LinkGroupId));
		Check(S, bSt && bOt && Stock.LinkGroupId == 5 && Other.LinkGroupId != 5,
			FString::Printf(TEXT("A0 группы: %d → %d, %d → %d"), kStockMt, Stock.LinkGroupId, kOtherGr, Other.LinkGroupId));
		S.TotalS = Mt.CooldownBase / 1000.0;
		int32 PotGroup = 0, PotMs = 0;
		for (int32 C : { 20571 }) { if (IT->GetItemCooldown(C, PotGroup, PotMs)) { S.Potion = C; } }
		for (int32 C = 1; C < 80000 && S.Potion == 0; ++C) { if (IT->GetItemCooldown(C, PotGroup, PotMs) && PotGroup != 0) { S.Potion = C; } }
		int32 PotTwin = 0;
		for (int32 C = 1; C < 80000 && PotTwin == 0 && S.Potion > 0; ++C)
		{
			int32 G2 = 0, M2 = 0;
			if (C != S.Potion && IT->GetItemCooldown(C, G2, M2) && G2 == PotGroup) { PotTwin = C; }
		}
		Check(S, S.Potion > 0 && PotMs > 0, FString::Printf(TEXT("A0 зелье %d: откат %d мс, группа %d, близнец группы %d"), S.Potion, PotMs, PotGroup, PotTwin));

		// ── A. Взвод реальными ответами ──
		Inv->DebugPutItem(kBagCont, S.BagCell, kMount, 1);
		Feed5451(Inv, Net, kMount, kBagCont, S.BagCell);
		S.TUse = FPlatformTime::Seconds();
		FAsdaHotbarCooldown Q = Hb->QueryItemCooldown(kMount, kBagCont);
		Check(S, Q.State == 1 && FMath::Abs(Q.RemainMs - 30000.f) < 100.f && Q.IconFrame == 0,
			FString::Printf(TEXT("A1 5451 маунт %d: состояние %d, остаток %.0f мс (ждём 30000), кадр %d"), kMount, Q.State, Q.RemainMs, Q.IconFrame));
		const FAsdaHotbarCooldown QS = Hb->QueryItemCooldown(kStockMt, kBagCont), QO = Hb->QueryItemCooldown(kOtherGr, kBagCont);
		Check(S, QS.State == 1 && FMath::Abs(QS.RemainMs - Q.RemainMs) < 50.f && QO.State == 0,
			FString::Printf(TEXT("A2 группа 5: стоковый маунт %d в откате (%.0f мс), предмет группы %d — нет (%d)"), kStockMt, QS.RemainMs, Other.LinkGroupId, QO.State));
		if (S.Potion > 0 && GEngine)
		{
			Inv->DebugPutItem(kPotCont, 5, S.Potion, 4);
			GEngine->Exec(W, *FString::Printf(TEXT("asda.Inven.Recv5029 1 %d 5 %d 3"), S.Potion, kPotCont));
			Inv->DebugPumpPendingPackets();
			const FAsdaHotbarCooldown QP = Hb->QueryItemCooldown(S.Potion, kPotCont);
			const FAsdaHotbarCooldown QT = PotTwin > 0 ? Hb->QueryItemCooldown(PotTwin, kPotCont) : FAsdaHotbarCooldown();
			Check(S, QP.State == 1 && FMath::Abs(QP.RemainMs - (float)PotMs) < 100.f && (PotTwin == 0 || QT.State == 1),
				FString::Printf(TEXT("A3 5029 зелье %d: остаток %.0f мс (ждём %d); близнец группы %d: %d"), S.Potion, QP.RemainMs, PotMs, PotTwin, QT.State));
		}

		// ── B1. Рывок ВНУТРИ кадра: реестр на реальных часах идёт, мир стоит ──
		Sw->PushCooldownFromServer(502, 100);
		{
			const double R0 = Hb->QueryItemCooldown(kMount, kBagCont).RemainMs / 1000.0;
			const float  K0 = Sw->GetCooldownRemainSec(502);
			const double Wt0 = W->GetTimeSeconds(), Rt0 = FPlatformTime::Seconds();
			FPlatformProcess::Sleep(1.5f);
			const double Dt = FPlatformTime::Seconds() - Rt0;
			const double R1 = Hb->QueryItemCooldown(kMount, kBagCont).RemainMs / 1000.0;
			const float  K1 = Sw->GetCooldownRemainSec(502);
			Check(S, FMath::Abs((R0 - R1) - Dt) < 0.1,
				FString::Printf(TEXT("B1 сон %.2f с в кадре: остаток маунта %.2f → %.2f с (реальные часы)"), Dt, R0, R1));
			Check(S, FMath::Abs((K0 - K1) - Dt) < 0.1,
				FString::Printf(TEXT("B1 книга навыков: остаток 502 %.2f → %.2f с (реальные часы)"), K0, K1));
			Check(S, FMath::IsNearlyEqual(W->GetTimeSeconds(), Wt0), TEXT("B1 мировое время за кадр не сдвинулось (на нём реестр стоял бы)"));
		}
		{
			// Прежнее поведение — для числа в отчёте: ключ 0, тот же сон, остаток не меняется.
			AsdaCVarSave::SetForTest(TEXT("asda.Cooldown.RealClock"), 0);
			Hb->PushItemCooldownRaw(4001, kPotCont, 0, 999, 30000);
			const double R0 = Hb->QueryItemCooldown(4001, kPotCont).RemainMs / 1000.0;
			FPlatformProcess::Sleep(0.5f);
			const double R1 = Hb->QueryItemCooldown(4001, kPotCont).RemainMs / 1000.0;
			Check(S, FMath::IsNearlyEqual(R0, R1, 0.001),
				FString::Printf(TEXT("B2 [прежнее, RealClock 0] за 0,5 с реального времени остаток %.2f → %.2f с — стоит"), R0, R1));
			Hb->ClearItemCooldownByCode(4001);
			AsdaCVarSave::SetForTest(TEXT("asda.Cooldown.RealClock"), 1);
		}

		// ── C. Прогон шторки (общая точка всех окон) ──
		UAsdaAnimatedPlane* P = NewObject<UAsdaAnimatedPlane>(GetTransientPackage());
		P->TotalFrame = 118; P->FrameIntervalMs = 1000.f;
		FAsdaCooldownCurtain Cu; Cu.Plane = P;
		{
			// Посреди отката: остаток 10 с из 30 (как после 5465/перекладки) — стартовый кадр 78.
			Hb->PushItemCooldownRaw(4003, kPotCont, 0, 998, 30000);
			FAsdaHotbarCooldown Mid = Hb->QueryItemCooldown(4003, kPotCont);
			Mid.RemainMs = 10000.f; Mid.IconFrame = (int32)((1.0 - 10.0 / 30.0) * 117.0);
			Hb->DriveCurtain(Cu, 77, Mid, AsdaCooldownNowS(GI));
			const double EndNew = CurtainEndS(P);
			Check(S, FMath::Abs(EndNew - 10.0) < 0.02 && P->StartFrame == Mid.IconFrame,
				FString::Printf(TEXT("C1 шторка с кадра %d при остатке 10 с: кадр 117 через %.3f с (ждём 10,000)"), P->StartFrame, EndNew));
			AsdaCVarSave::SetForTest(TEXT("asda.Cooldown.CurtainTotal"), 0);
			Cu.ShownKey = -1;
			Hb->DriveCurtain(Cu, 77, Mid, AsdaCooldownNowS(GI));
			const double EndOld = CurtainEndS(P);
			Check(S, EndOld < 4.0,
				FString::Printf(TEXT("C2 [прежнее, CurtainTotal 0] та же шторка гасла через %.2f с вместо 10 — раньше сервера на %.2f с"), EndOld, 10.0 - EndOld));
			AsdaCVarSave::SetForTest(TEXT("asda.Cooldown.CurtainTotal"), 1);
			Cu.ShownKey = -1;
			// Свежий откат: кадр 0, конец — полная длительность (поведение эталона при свежем взводе).
			const FAsdaHotbarCooldown Fresh = Hb->QueryItemCooldown(kMount, kBagCont);
			Hb->DriveCurtain(Cu, 78, Fresh, AsdaCooldownNowS(GI));
			Check(S, FMath::Abs(CurtainEndS(P) - Fresh.RemainMs / 1000.0) < 0.02,
				FString::Printf(TEXT("C3 маунт: шторка с кадра %d, конец через %.3f с = остаток %.3f с"), P->StartFrame, CurtainEndS(P), Fresh.RemainMs / 1000.0));
			// Догонка: план «доиграл» (как при отставании часов), реестр ещё в откате — следующий вызов поднимает заново.
			P->StopAnim();
			Hb->DriveCurtain(Cu, 78, Hb->QueryItemCooldown(kMount, kBagCont), AsdaCooldownNowS(GI));
			Check(S, !P->IsAnimFinished(), TEXT("C4 доигравшая шторка при живом откате поднята заново (CurtainResync)"));
			AsdaCVarSave::SetForTest(TEXT("asda.Cooldown.CurtainResync"), 0);
			P->StopAnim();
			Hb->DriveCurtain(Cu, 78, Hb->QueryItemCooldown(kMount, kBagCont), AsdaCooldownNowS(GI));
			Check(S, P->IsAnimFinished(), TEXT("C5 [прежнее, CurtainResync 0] доигравшая шторка оставалась погашенной"));
			AsdaCVarSave::SetForTest(TEXT("asda.Cooldown.CurtainResync"), 1);
			Hb->ClearItemCooldownByCode(4003);
		}
		AsdaCVarSave::RestoreAll(kCmd);

		// ── Подготовка D: вход в мир офлайн (без него панель быстрого доступа не создаётся, а у шторок сумки нет образца
		//   плана — прогон 25.10 показал пустые дампы до портала), окно сумки (вкладка снаряжения), маунт на панели в слоте 0 ──
		if (GEngine)
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256 CdTest"));
		}
		Inv->OpenWindow();
		Inv->SetTab(1);
		const bool bSet = Hb->SetSlotContent(0, (uint8)EAsdaHotbarSlotType::Item, (uint8)kBagCont, S.BagCell, 1, kMount);
		Check(S, bSet, TEXT("D0 маунт вынесен на панель (слот 0, сумка ячейка 3)"));
		return true;
	}

	// ══════════ АСИНХРОННАЯ ЧАСТЬ ══════════
	static bool Step(TSharedRef<FState> SR)
	{
		FState& S = *SR;
		UGameInstance* GI = S.GI.Get();
		if (!GI) { AsdaCVarSave::RestoreAll(kCmd); return false; }
		UWorld* W = GI->GetWorld();
		UAsdaHotbarSubsystem* Hb = GI->GetSubsystem<UAsdaHotbarSubsystem>();
		UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>();
		UAsdaSkillWindowSubsystem* Sw = GI->GetSubsystem<UAsdaSkillWindowSubsystem>();
		UAsdaNetSubsystem* Net = AsdaTestNet::Get(GI);
		const double Now = FPlatformTime::Seconds();
		const double InStage = Now - S.StageT;
		auto Next = [&S, Now]() { ++S.Stage; S.StageT = Now; };
		if (!Hb || !Inv || !Sw || !Net) { Check(S, false, TEXT("подсистемы пропали")); Finish(S, W); return false; }

		switch (S.Stage)
		{
		case 0:   // B3: настоящий рывок кадра — 2 с сна, мир зажмёт кадр (до него — 3 с на подъём панели после входа)
			if (InStage < 3.0) { return true; }
			S.WorldT0 = W ? W->GetTimeSeconds() : 0.0;
			S.RealT0 = FPlatformTime::Seconds();
			FPlatformProcess::Sleep(2.0f);
			Next();
			return true;
		case 1:
			if (InStage < 0.3) { return true; }
			{
				const double DReal = Now - S.RealT0, DWorld = (W ? W->GetTimeSeconds() : 0.0) - S.WorldT0;
				Check(S, DReal > 2.0 && DWorld < DReal - 1.4,
					FString::Printf(TEXT("B3 рывок: реальное %.2f с, мировое %.2f с (кламп 0,4 с/кадр) — мир отстал на %.2f с"), DReal, DWorld, DReal - DWorld));
				const double R = Hb->QueryItemCooldown(kMount, kBagCont).RemainMs / 1000.0;
				Check(S, FMath::Abs(R - Expected(S)) < 0.1,
					FString::Printf(TEXT("B3 остаток маунта %.2f с = серверный %.2f с (на мировых часах было бы на %.2f с больше)"), R, Expected(S), DReal - DWorld));
				const float K = Sw->GetCooldownRemainSec(502);
				Check(S, FMath::Abs(K - (100.0 - (Now - S.TUse))) < 0.4,
					FString::Printf(TEXT("B3 книга навыков после рывка: 502 осталось %.2f с (ждём ≈ %.2f)"), K, 100.0 - (Now - S.TUse)));
			}
			Next();
			return true;
		case 2:   // D1: сумка и панель горят тем же остатком
			if (InStage < 1.0) { return true; }
			{
				const double Want = Expected(S);
				const double Ri = DumpRemain(Inv->DumpCooldownCurtains(), FString::Printf(TEXT("сумка ячейка %d\\s+предмет %d\\s+состояние 1 осталось ([0-9.]+)"), S.BagCell, kMount));
				const double Rh = DumpRemain(Hb->DumpCooldownCurtains(), FString::Printf(TEXT("абс.слот 0\\s+тип 1 id %d\\s+состояние 1 осталось ([0-9.]+)"), kMount));
				Check(S, Ri >= 0 && FMath::Abs(Ri - Want) < 0.3, FString::Printf(TEXT("D1 сумка ячейка %d: шторка, осталось %.2f с (сервер %.2f)"), S.BagCell, Ri, Want));
				Check(S, Rh >= 0 && FMath::Abs(Rh - Want) < 0.3, FString::Printf(TEXT("D1 панель слот 0: шторка, осталось %.2f с (сервер %.2f)"), Rh, Want));
				// Перекладка сумка→сумка (ячейка 3 → 9) и вынос того же маунта во второй слот панели.
				Inv->DebugPutItem(kBagCont, S.BagCell, 0, 0);
				S.BagCell = 9;
				Inv->DebugPutItem(kBagCont, S.BagCell, kMount, 1);
				Hb->SetSlotContent(0, (uint8)EAsdaHotbarSlotType::Item, (uint8)kBagCont, S.BagCell, 1, kMount);
				Hb->SetSlotContent(1, (uint8)EAsdaHotbarSlotType::Item, (uint8)kBagCont, S.BagCell, 1, kMount);
			}
			Next();
			return true;
		case 3:
			if (InStage < 0.8) { return true; }
			{
				const double Want = Expected(S);
				const FString Di = Inv->DumpCooldownCurtains(), Dh = Hb->DumpCooldownCurtains();
				const double Ri = DumpRemain(Di, FString::Printf(TEXT("сумка ячейка 9\\s+предмет %d\\s+состояние 1 осталось ([0-9.]+)"), kMount));
				const double Rold = DumpRemain(Di, FString::Printf(TEXT("сумка ячейка 3\\s+предмет %d\\s+состояние 1 осталось ([0-9.]+)"), kMount));
				const double R0 = DumpRemain(Dh, FString::Printf(TEXT("абс.слот 0\\s+тип 1 id %d\\s+состояние 1 осталось ([0-9.]+)"), kMount));
				const double R1 = DumpRemain(Dh, FString::Printf(TEXT("абс.слот 1\\s+тип 1 id %d\\s+состояние 1 осталось ([0-9.]+)"), kMount));
				Check(S, Ri >= 0 && FMath::Abs(Ri - Want) < 0.3 && Rold < 0,
					FString::Printf(TEXT("D2 сумка→сумка (3 → 9): новая ячейка %.2f с (сервер %.2f), старая погашена"), Ri, Want));
				Check(S, R0 >= 0 && R1 >= 0 && FMath::Abs(R0 - Want) < 0.3 && FMath::Abs(R1 - Want) < 0.3,
					FString::Printf(TEXT("D3 сумка→панель: слоты 0 и 1 — %.2f / %.2f с (сервер %.2f)"), R0, R1, Want));
				// E: портал. Перед ним — «лишняя» запись (у сервера её на портале нет) и откат навыка вне снимка.
				Hb->PushItemCooldownRaw(4001, kPotCont, 0, 999, 300000);
				S.WorldBeforePortal = W ? W->GetTimeSeconds() : 0.0;
				S.OldWorld = (UPTRINT)W;
				UGameplayStatics::OpenLevel(W, FName(TEXT("alpen")), true, TEXT("game=/Script/AsdaReborn.AsdaWorldGameMode"));
			}
			Next();
			return true;
		case 4:   // ждём новый мир
			if (!W || (UPTRINT)W == S.OldWorld || !W->HasBegunPlay())
			{
				if (InStage < 120.0) { return true; }
				Check(S, false, TEXT("E0 новый мир не поднялся за 120 с"));
				Finish(S, W);
				return false;
			}
			if (GEngine)
			{
				GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256 CdTest"));
			}
			Next();
			return true;
		case 5:
			if (InStage < 3.0) { return true; }
			{
				const double R = Hb->QueryItemCooldown(kMount, kBagCont).RemainMs / 1000.0;
				Check(S, W && W->GetTimeSeconds() < S.WorldBeforePortal,
					FString::Printf(TEXT("E1 портал: мировое время нового мира %.1f с < старого %.1f с (на нём записи уехали бы в будущее)"),
						W ? W->GetTimeSeconds() : -1.0, S.WorldBeforePortal));
				Check(S, FMath::Abs(R - Expected(S)) < 0.15,
					FString::Printf(TEXT("E2 после портала до 5465: остаток маунта %.2f с = серверный %.2f с"), R, Expected(S)));
				// 5465 после портала: навык 501 — 20 с, маунт — 12 с (как сохранил сервер). 502 и 4001 в снимке нет.
				AsdaTestNet::DebugFeedPacket(Net, 5465, Make5465({ FIntPoint(501, 20) }, { FIntPoint(kMount, 12) }));
				S.T5465 = FPlatformTime::Seconds();
				const FAsdaHotbarCooldown Q = Hb->QueryItemCooldown(kMount, kBagCont);
				Check(S, Q.State == 1 && FMath::Abs(Q.RemainMs - 12000.f) < 100.f && Q.IconFrame == (int32)((1.0 - 12.0 / 30.0) * 117.0),
					FString::Printf(TEXT("E3 5465: маунт %.0f мс (ждём 12000), стартовый кадр %d"), Q.RemainMs, Q.IconFrame));
				Check(S, Hb->QueryItemCooldown(4001, kPotCont).State == 0, TEXT("E4 5465 — снимок: запись предмета вне пакета снята (эталон skillHotbarRefreshLocked)"));
				S.TUse = S.T5465 - (S.TotalS - 12.0);   // дальше «серверный» остаток считаем от снимка
				Inv->OpenWindow();
				Inv->SetTab(1);
			}
			Next();
			return true;
		case 6:
			if (InStage < 1.0) { return true; }
			{
				const float K1 = Sw->GetCooldownRemainSec(501), K2 = Sw->GetCooldownRemainSec(502);
				Check(S, FMath::Abs(K1 - (20.0 - (Now - S.T5465))) < 0.3 && K2 <= 0.f,
					FString::Printf(TEXT("E5 книга навыков снимком: 501 осталось %.2f с, 502 (вне снимка) — %.2f"), K1, K2));
				const double Want = Expected(S);
				const double Ri = DumpRemain(Inv->DumpCooldownCurtains(), FString::Printf(TEXT("сумка ячейка 9\\s+предмет %d\\s+состояние 1 осталось ([0-9.]+)"), kMount));
				const double Rh = DumpRemain(Hb->DumpCooldownCurtains(), FString::Printf(TEXT("абс.слот 0\\s+тип 1 id %d\\s+состояние 1 осталось ([0-9.]+)"), kMount));
				Check(S, Ri >= 0 && FMath::Abs(Ri - Want) < 0.3, FString::Printf(TEXT("E6 после портала сумка: %.2f с (сервер %.2f)"), Ri, Want));
				Check(S, Rh >= 0 && FMath::Abs(Rh - Want) < 0.3, FString::Printf(TEXT("E6 после портала панель: %.2f с (сервер %.2f)"), Rh, Want));
				UAsdaAnimatedPlane* P = NewObject<UAsdaAnimatedPlane>(GetTransientPackage());
				P->TotalFrame = 118; P->FrameIntervalMs = 1000.f;
				FAsdaCooldownCurtain Cu; Cu.Plane = P;
				const FAsdaHotbarCooldown Q = Hb->QueryItemCooldown(kMount, kBagCont);
				Hb->DriveCurtain(Cu, 1, Q, AsdaCooldownNowS(GI));
				Check(S, FMath::Abs(CurtainEndS(P) - Q.RemainMs / 1000.0) < 0.02,
					FString::Printf(TEXT("E7 шторка восстановленного отката: с кадра %d, конец через %.3f с = остаток %.3f с"), P->StartFrame, CurtainEndS(P), Q.RemainMs / 1000.0));

				// F: вход в мир с остатком — полный сброс сессии, затем 5465 логона.
				Hb->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
				Check(S, Hb->QueryItemCooldown(kMount, kBagCont).State == 0, TEXT("F1 полный сброс сессии снял откаты"));
				AsdaTestNet::DebugFeedPacket(Net, 5465, Make5465(TArray<FIntPoint>(), { FIntPoint(kMount, 25) }));
				const FAsdaHotbarCooldown QF = Hb->QueryItemCooldown(kMount, kBagCont);
				Cu.ShownKey = -1;
				Hb->DriveCurtain(Cu, 1, QF, AsdaCooldownNowS(GI));
				Check(S, QF.State == 1 && FMath::Abs(QF.RemainMs - 25000.f) < 100.f && QF.IconFrame == (int32)((1.0 - 25.0 / 30.0) * 117.0)
					&& FMath::Abs(CurtainEndS(P) - QF.RemainMs / 1000.0) < 0.02,
					FString::Printf(TEXT("F2 вход с остатком 25 с: реестр %.0f мс, кадр %d, конец шторки через %.3f с"), QF.RemainMs, QF.IconFrame, CurtainEndS(P)));
			}
			Finish(S, W);
			return false;
		default:
			Finish(S, W);
			return false;
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestCooldowns(
	TEXT("asda.SelfTest.Cooldowns"),
	TEXT("Самотест откатов предметов: 5451 маунт fable 609 / 5029 зелье, рывок кадра (реальные часы), прогон шторки, "
	     "перекладка сумка/панель, портал OpenLevel, 5465 снимок, вход с остатком. Офлайн, на карте мира. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaCdTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет GameInstance")); return; }
		TSharedRef<FState> SR = MakeShared<FState>();
		SR->GI = GI;
		SR->bQuit = KA.bQuit;
		if (!RunSync(*SR, W)) { Finish(*SR, W); return; }
		SR->StageT = FPlatformTime::Seconds();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([SR](float) -> bool { return Step(SR); }), 0.1f);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
