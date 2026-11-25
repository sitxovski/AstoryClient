// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ПАНЕЛЬ ОТРЯДА 2200 НЕ ПРОПАДАЕТ» (22.10.2026, баг-репорт игроков «Team HUD occasionally disappears»):
//   `asda.SelfTest.PartyHud [quit]`. Офлайн (`-AsdaNoNet`), на карте мира, путём игрока: пакеты подаются ШТАТНЫМ приёмом
//   (`UAsdaNetSubsystem::DebugFeedPacket` → `HandlePacket`, тот же разбор, что у живого сокета), панель проверяется по
//   живому Slate (окно во вьюпорте своего мира, обёртка вьюпорта, `GetCachedGeometry` строк, текст имён).
//   Разбор причины и эталона — `Reports/party_hud_2026-10-22/REPORT.md`.
//   Этапы:
//     0) отряд из трёх (я глава + 2): 5093 код 1 + 5107 → панель видна, две строки с именами и ненулевой геометрией;
//     A) 4056 о СОСЕДЕ (чужое воскрешение/прибытие в секторе): реестр окон не опустошается, панель на месте,
//        свой опыт не перезаписан опытом соседа (эталон `GameNet_OnLocalPlayerMapEntry@0xF43390`, ветка чужого — без UI);
//     B) 4056 о СЕБЕ (воскрешение в городе/на месте той же карты — сервер состав 5107 НЕ переприсылает): панель вернулась
//        с теми же участниками;
//     C) смена карты: сброс «смена карты» (приёмник отряда `ResetSessionState(MapChanged)`) + снос окон, как при травеле и `RevealWorld` по 4003
//        новой карты: панель вернулась, свёрнутый вид сохранён (эталон окно не пересоздаёт — состояние живёт);
//     D) Alt+Z: окно, восстановленное при скрытом интерфейсе, скрыто вместе со всеми, показ Alt+Z возвращает его;
//     E) роспуск 5103: панель скрыта и после сноса окон НЕ восстанавливается (лечится только живой отряд);
//     F) ключ отката `asda.Party.RosterSurvivesWipe 0`: прежнее поведение — после сноса панель не возвращается.
//   `legacy` — оба исправления выключены ключами отката: воспроизведение бага (A/B/C/D обязаны дать FAIL).
//   Итог: «ОТРЯД-HUD ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Login/AsdaLoginData.h"              // EncodeCp1251 — имена в теле 5107
#include "UI/AsdaPartySubsystem.h"
#include "AsdaSessionReset.h"                // EAsdaSessionResetReason — сброс «смена карты»
#include "UI/AsdaUiVisibilitySubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "Test/AsdaSelfTestKit.h"
#include "AsdaCVarSave.h"                    // ключи отката на время прогона (возврат — Finish)
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPartyHudTest, Log, All);

namespace AsdaPartyHudTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.PartyHud");   // имя команды (итог, ключи консоли)
	static const FName  kRosterId(TEXT("PARTY_WIN"));             // окно 2200 в реестре окон
	static constexpr int32  kMe = 1001, kMate = 2002, kThird = 3003; // charId участников
	static constexpr uint16 kMyUserNum = 321;                        // мой userNum на прогон
	static constexpr int32  kNeighbor = 77;                          // userIdx соседа (чужой 4056)
	static constexpr int32  kParty = 42;                             // partyIndex
	static constexpr int64  kNeighborExp = 987654321;               // опыт соседа в его 4056 — своим стать не должен

	// Состояние прогона (живёт на таймерах мира).
	struct FCtx
	{
		TWeakObjectPtr<UWorld> W;     // мир прогона
		bool bQuit = false;           // `quit` после итога
		int32 Ok = 0, Fail = 0;       // счётчики проверок
		int32 CharIdWas = 0;          // свой charId до прогона
		uint16 UserNumWas = 0;        // свой userNum до прогона
		bool bKnownWas = false;       // признак «свой userNum известен» до прогона
		int64 ExpWas = -1;            // свой опыт до прогона
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaPartyHudTest, Log,   TEXT("ОТРЯД-HUD ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaPartyHudTest, Error, TEXT("ОТРЯД-HUD FAIL %s"), *What); }
		}
	};
	using FCtxRef = TSharedRef<FCtx>;

	using AsdaSelfTestCtx::GIOf;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	using AsdaSelfTestCtx::Sub;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// Подать тело штатным приёмом (тот же разбор, что у живого сокета).
	static void Feed(const FCtxRef& C, int32 Op, const TArray<uint8>& B)
	{
		if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C)) { Net->DebugFeedPacket(Op, B); }
	}

	// 4056 (22 б, раскладка `GameNet_OnLocalPlayerMapEntry`): uidx@0, тайл X@2, Y@4, HP i32@6, MP i16@10, exp i64@12, state i16@20.
	static TArray<uint8> Body4056(int32 Uidx, int32 Tx, int32 Tz, int64 Exp)
	{
		TArray<uint8> B; B.SetNumZeroed(22);
		AsdaBytes::WrI16(&B[0], Uidx); AsdaBytes::WrI16(&B[2], Tx); AsdaBytes::WrI16(&B[4], Tz);
		AsdaBytes::WrI32(&B[6], 500); AsdaBytes::WrI16(&B[10], 100);
		FMemory::Memcpy(&B[12], &Exp, 8);
		AsdaBytes::WrI16(&B[20], 0);
		return B;
	}

	// Снимок панели 2200 так, как её видит игрок.
	struct FSnap
	{
		bool bLive = false;      // окно во вьюпорте СВОЕГО мира (`IsWindowLive`)
		bool bVisible = false;   // собственная видимость окна в реестре
		bool bHost = false;      // обёртка вьюпорта видима (Alt+Z её прячет)
		int32 Rows = 0;          // строк участников с ненулевой геометрией после отрисовки
		FString Names;           // имена в показанных строках
		bool bCollapsedView = false;   // показан свёрнутый заголовок `Party_WinH_grp`, тело `Party_WinN_grp` скрыто
		int32 Registered = 0;    // окон в реестре
	};
	static FSnap Snap(const FCtxRef& C)
	{
		FSnap S;
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		if (!WM) { return S; }
		S.Registered = WM->GetRegisteredWindowIds().Num();
		UUserWidget* Win = WM->FindWindow(kRosterId);
		S.bLive = Win && WM->IsWindowLive(Win);
		S.bVisible = WM->IsWindowVisible(kRosterId);
		S.bHost = Win && UAsdaUiVisibilitySubsystem::IsViewportHostVisible(Win);
		if (!S.bLive) { return S; }
		for (int32 R = 0; R < 6; ++R)
		{
			UWidget* Root = AsdaUi::FindNodeByPrefix(Win, *FString::Printf(TEXT("PARTY_WIN%02d_grp"), R));
			FVector2D Mn, Mx;
			if (!Root || !Root->IsVisible() || !AsdaUi::WidgetAbsRect(Root, Mn, Mx) || (Mx - Mn).X < 2.f || (Mx - Mn).Y < 2.f) { continue; }
			++S.Rows;
			if (const UTextBlock* T = Cast<UTextBlock>(AsdaUi::FindNodeByPrefix(Win, *FString::Printf(TEXT("PARTY_NAME%02d"), R))))
			{
				S.Names += (S.Names.IsEmpty() ? TEXT("") : TEXT(",")) + T->GetText().ToString();
			}
		}
		const UWidget* Hdr = AsdaUi::FindNodeByPrefix(Win, TEXT("Party_WinH_grp"));
		const UWidget* Body = AsdaUi::FindNodeByPrefix(Win, TEXT("Party_WinN_grp"));
		S.bCollapsedView = Hdr && Hdr->IsVisible() && Body && !Body->IsVisible();
		return S;
	}
	static FString Str(const FSnap& S)
	{
		return FString::Printf(TEXT("живо=%d видимо=%d обёртка=%d строк=%d имена=[%s] свёрнуто=%d окон в реестре=%d"),
			S.bLive, S.bVisible, S.bHost, S.Rows, *S.Names, S.bCollapsedView, S.Registered);
	}
	// Панель на экране с обоими соотрядниками.
	static bool Shown(const FSnap& S)
	{
		return S.bLive && S.bVisible && S.bHost && S.Rows == 2 && S.Names.Contains(TEXT("Member1")) && S.Names.Contains(TEXT("Member2"));
	}

	using AsdaSelfTestCtx::ByStage::After;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// Снос окон, как при травеле на новую карту и `RevealWorld` по 4003 новой карты (`UAsdaWindowSubsystem::CloseAll`).
	static void WipeWindows(const FCtxRef& C) { if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C)) { WM->CloseAll(); } }

	static void SA(FCtxRef C);  static void SA2(FCtxRef C);
	static void SB(FCtxRef C);  static void SC(FCtxRef C);  static void SC2(FCtxRef C);
	static void SD(FCtxRef C);  static void SD2(FCtxRef C); static void SE(FCtxRef C);  static void SE2(FCtxRef C);
	static void SF(FCtxRef C);  static void SF2(FCtxRef C); static void SF3(FCtxRef C); static void SF4(FCtxRef C);
	static void Finish(FCtxRef C);

	// Отряд из трёх: 5093 код 1 (я создал отряд) + 5107 полный состав — штатным приёмом.
	static void MakeParty(const FCtxRef& C)
	{
		TArray<uint8> B; B.SetNumZeroed(15);
		B[0] = 1; AsdaBytes::WrI16(&B[1], kMyUserNum); AsdaBytes::WrI32(&B[3], kMe);
		AsdaBytes::WrI16(&B[7], 322); AsdaBytes::WrI32(&B[9], kMate); AsdaBytes::WrI16(&B[13], kParty);
		Feed(C, 5093, B);
		TArray<uint8> R; R.SetNumZeroed(144);
		const int32 Ids[6] = { kMe, kMate, kThird, -1, -1, -1 };
		for (int32 i = 0; i < 6; ++i)
		{
			AsdaBytes::WrI32(&R[4 * i], Ids[i]);
			if (Ids[i] != -1) { FAsdaLoginData::EncodeCp1251(FString::Printf(TEXT("Member%d"), i), &R[24 + 20 * i], 19); }
		}
		Feed(C, 5107, R);
	}

	// ═══ 0. Свои идентификаторы и отряд ═══
	static void S0(FCtxRef C)
	{
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
		UAsdaPartySubsystem* Party = Sub<UAsdaPartySubsystem>(C);
		if (!Net || !Party) { C->Check(false, TEXT("0 подсистемы сети/отряда")); Finish(C); return; }
		C->CharIdWas = Net->SelectedCharId; C->UserNumWas = Net->SelfUserNum; C->bKnownWas = Net->bSelfUserNumKnown; C->ExpWas = Net->SelfExp;
		Net->SelectedCharId = kMe; Net->SetSelfUserNumForDebug(kMyUserNum);
		MakeParty(C);
		After(C, 0.6f, &SA);
	}

	// ═══ A. 4056 о соседе ═══
	static void SA(FCtxRef C)
	{
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
		UAsdaPartySubsystem* Party = Sub<UAsdaPartySubsystem>(C);
		const FSnap S0 = Snap(C);
		C->Check(Party && Party->IsInParty() && Party->GetMembers().Num() == 3 && Shown(S0),
			FString::Printf(TEXT("0 отряд из трёх, панель 2200 на экране: %s"), *Str(S0)));
		const int64 ExpBefore = Net ? Net->SelfExp : -1;
		Feed(C, 4056, Body4056(kNeighbor, 100, 100, kNeighborExp));
		const FSnap S1 = Snap(C);   // тот же кадр: снос окон (если он есть) уже случился внутри приёма
		C->Check(S1.Registered >= S0.Registered && S1.bLive,
			FString::Printf(TEXT("A1 4056 соседа: реестр окон не опустошён (%d → %d), панель жива — эталон ветки чужого UI не трогает"),
				S0.Registered, S1.Registered));
		C->Check(Net && Net->SelfExp == ExpBefore,
			FString::Printf(TEXT("A2 4056 соседа: свой опыт не перезаписан (%lld → %lld, у соседа %lld) — эталон пишет опыт только в своей ветке"),
				(long long)ExpBefore, Net ? (long long)Net->SelfExp : -1LL, (long long)kNeighborExp));
		After(C, 0.6f, &SA2);
	}
	static void SA2(FCtxRef C)
	{
		const FSnap S = Snap(C);
		C->Check(Shown(S), FString::Printf(TEXT("A3 через 0.6 с после 4056 соседа панель на экране с теми же участниками: %s"), *Str(S)));
		// ═══ B. 4056 о себе: воскрешение на той же карте ═══
		Feed(C, 4056, Body4056(kMyUserNum, 256, 256, 1000));
		After(C, 0.8f, &SB);
	}
	static void SB(FCtxRef C)
	{
		const FSnap S = Snap(C);
		C->Check(Shown(S), FString::Printf(TEXT("B1 своё воскрешение (4056, та же карта, 5107 не приходит): панель 2200 на экране с теми же участниками: %s"), *Str(S)));
		// ═══ C. Смена карты: свернуть панель, сброс «смена карты», снос окон ═══
		if (UAsdaPartySubsystem* Party = Sub<UAsdaPartySubsystem>(C)) { Party->ToggleRosterCollapsed(); }
		After(C, 0.3f, &SC);
	}
	static void SC(FCtxRef C)
	{
		const FSnap S = Snap(C);
		C->Check(S.bCollapsedView, FString::Printf(TEXT("C0 панель свёрнута кнопкой: %s"), *Str(S)));
		// Сброс «смена карты» — приёмник самой подсистемы отряда (общая рассылка `asda.Session.ResetMap` обнулила бы и
		//   ссылку мира на свой павн — следующим самотестам пакета он нужен); затем снос окон, как травел и 4003 новой карты.
		if (UAsdaPartySubsystem* Party = Sub<UAsdaPartySubsystem>(C)) { Party->ResetSessionState(EAsdaSessionResetReason::MapChanged); }
		WipeWindows(C);
		After(C, 0.8f, &SC2);
	}
	static void SC2(FCtxRef C)
	{
		const FSnap S = Snap(C);
		UAsdaPartySubsystem* Party = Sub<UAsdaPartySubsystem>(C);
		C->Check(S.bLive && S.bVisible && S.bHost && S.bCollapsedView && Party && Party->GetMembers().Num() == 3,
			FString::Printf(TEXT("C1 смена карты (сброс + снос окон): панель вернулась СВЁРНУТОЙ, состав 3: %s"), *Str(S)));
		if (Party) { Party->ToggleRosterCollapsed(); }
		After(C, 0.4f, &SD);
	}

	// ═══ D. Alt+Z ═══
	static void SD(FCtxRef C)
	{
		const FSnap S = Snap(C);
		C->Check(Shown(S), FString::Printf(TEXT("C2 развёрнута обратно, строки и имена на месте: %s"), *Str(S)));
		if (UAsdaUiVisibilitySubsystem* Vis = Sub<UAsdaUiVisibilitySubsystem>(C)) { Vis->SetUiShown(false, TEXT("самотест PartyHud")); }
		WipeWindows(C);
		After(C, 0.6f, &SD2);
	}
	static void SD2(FCtxRef C)
	{
		const FSnap S = Snap(C);
		C->Check(S.bLive && S.bVisible && !S.bHost,
			FString::Printf(TEXT("D1 восстановлена при скрытом интерфейсе (Alt+Z) — скрыта вместе со всеми: %s"), *Str(S)));
		if (UAsdaUiVisibilitySubsystem* Vis = Sub<UAsdaUiVisibilitySubsystem>(C)) { Vis->SetUiShown(true, TEXT("самотест PartyHud")); }
		After(C, 0.4f, &SE);
	}

	// ═══ E. Роспуск 5103 ═══
	static void SE(FCtxRef C)
	{
		const FSnap S = Snap(C);
		C->Check(Shown(S), FString::Printf(TEXT("D2 интерфейс показан — панель видна: %s"), *Str(S)));
		TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], kParty);
		Feed(C, 5103, B);
		After(C, 0.3f, &SE2);
	}
	static void SE2(FCtxRef C)
	{
		UAsdaPartySubsystem* Party = Sub<UAsdaPartySubsystem>(C);
		const FSnap S = Snap(C);
		C->Check(Party && !Party->IsInParty() && !(S.bLive && S.bVisible),
			FString::Printf(TEXT("E1 роспуск 5103: не в отряде, панель скрыта: %s"), *Str(S)));
		WipeWindows(C);
		After(C, 0.6f, &SF);
	}

	// ═══ F. Ключ отката ═══
	static void SF(FCtxRef C)
	{
		const FSnap S = Snap(C);
		C->Check(!(S.bLive && S.bVisible), FString::Printf(TEXT("E2 вне отряда снос окон панель НЕ возвращает: %s"), *Str(S)));
		AsdaCVarSave::SetForTest(TEXT("asda.Party.RosterSurvivesWipe"), 0);
		MakeParty(C);
		After(C, 0.6f, &SF2);
	}
	static void SF2(FCtxRef C)
	{
		const FSnap S = Snap(C);
		C->Check(Shown(S), FString::Printf(TEXT("F0 снова отряд из трёх (ключ 0) — панель по 5107: %s"), *Str(S)));
		WipeWindows(C);
		After(C, 0.6f, &SF3);
	}
	static void SF3(FCtxRef C)
	{
		const FSnap S = Snap(C);
		C->Check(!S.bLive, FString::Printf(TEXT("F1 ключ asda.Party.RosterSurvivesWipe 0 — прежнее поведение: после сноса панели нет до нового 5107: %s"), *Str(S)));
		AsdaCVarSave::SetForTest(TEXT("asda.Party.RosterSurvivesWipe"), 1);
		After(C, 0.6f, &SF4);
	}
	static void SF4(FCtxRef C)
	{
		const FSnap S = Snap(C);
		C->Check(Shown(S), FString::Printf(TEXT("F2 ключ 1 — панель вернулась: %s"), *Str(S)));
		TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], kParty);
		Feed(C, 5103, B);   // уборка: отряд распущен
		After(C, 0.3f, &Finish);
	}

	static void Finish(FCtxRef C)
	{
		if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C))
		{
			Net->SelectedCharId = C->CharIdWas; Net->SelfUserNum = C->UserNumWas; Net->bSelfUserNumKnown = C->bKnownWas; Net->SelfExp = C->ExpWas;
		}
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaPartyHudTest, TEXT("ОТРЯД-HUD"), C->Ok, C->Fail);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestPartyHud(
	TEXT("asda.SelfTest.PartyHud"),
	TEXT("Самотест панели отряда 2200: 4056 соседа/своё воскрешение/смена карты/Alt+Z/роспуск/ключ отката — панель на экране "
	     "с теми же участниками (живой Slate, геометрия строк). Офлайн, на карте мира. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaPartyHudTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		// Свой персонаж нужен приёмнику 4056 (телепорт своего павна) — офлайн его нет: штатный фейковый спавн и повтор через 2 с.
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s selfready%s%s"), kCmd, KA.Has(TEXT("legacy")) ? TEXT(" legacy") : TEXT(""), KA.bQuit ? TEXT(" quit") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W; C->bQuit = KA.bQuit;
		AsdaCVarSave::ArmWorldGuard(W, kCmd);   // мир ушёл раньше итога — ключи всё равно вернутся
		// `legacy` — оба исправления выключены ключами отката (дословно прежний код): прогон ОБЯЗАН дать FAIL в A/B/C/D —
		//   так самотест доказывает, что воспроизводит баг игроков.
		if (KA.Has(TEXT("legacy")))
		{
			UE_LOG(LogAsdaPartyHudTest, Warning, TEXT("ОТРЯД-HUD режим legacy: asda.Party.RosterSurvivesWipe 0 + asda.Net.WorldReadySelfOnly 0 (прежнее поведение)"));
			AsdaCVarSave::SetForTest(TEXT("asda.Party.RosterSurvivesWipe"), 0);
			AsdaCVarSave::SetForTest(TEXT("asda.Net.WorldReadySelfOnly"), 0);
		}
		S0(C);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
