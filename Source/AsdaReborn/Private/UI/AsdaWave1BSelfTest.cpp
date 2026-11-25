// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ВОЛНЫ 1-Б (06.10.2026, отчёт `Reports/owner_1006/wave1B_party_roulette.md`). [ПОРТ]
//
// ★ ЧТО ПРОВЕРЯЕТ (всё — штатным путём: приём через `UAsdaNetSubsystem::DebugFeedPacket` → `HandlePacket`,
//   клики — настоящими событиями Slate по ЖИВОЙ геометрии `UAsdaWindowSubsystem::SimulateClickAt`, отправка —
//   перехватом исходящих `UAsdaPetSubsystem::BeginSendCapture`, в сеть не уходит ничего):
//   P. ОТРЯД: 5093 код 1 + 5107 → я глава, отряд 42, панель 2200 видна.
//   M. МИНИ-КАРТА (5104/5109): до координат отметок нет; 5109 ×3 (я / 2002 на моей карте / 3003 на чужой) +
//      5104 ×2 (чужой отряд 43 — пропуск; свой 42 — тайл 2002 обновлён). Список `EAsdaMapMark::Party` = 2
//      (без себя), нарисован на мини-карте ровно 1 квад `miniPartyPos`, и он смещён от значка игрока в сторону
//      сдвига тайла (+X, +Z). Контроль: 3003 (чужая карта) не нарисован; после 5103 отметок нет.
//   D. ОКНО РОСПУСКА 2256: клик по 2260 у главы → окно, текст loc 1170, пакета нет; «ОК» → 5098 режим 1
//      (partyIndex 42); снова 2260 → «Отмена» → 5098 режим 0. Контрольные пары: рядовой (5102 → 2002) — 2260
//      шлёт режим 0 сразу и окна нет, `ShowDisbandConfirm` → false; ключ `asda.Party.FixDisbandConfirm 0` у
//      главы — режим 0 сразу, окна нет.
//   G. ГЕЙТ «ДУШИ»: 4070 в таблице и режется при SoulState 1; контроль — 4071 не режется, SoulState 0 не режет.
//   R. РУЛЕТКА 67000: 6576 код 1 (сектор 2, следующая −1) ОТКРЫВАЕТ окно (прежде — нет); узлы иконок/стрелок —
//      планы, не группы; колесо встаёт на 180°, через анимацию «забрать» уходит 6579 (−1) и окно гаснет.
//      Контроль: 6576 код 10 окно не открывает; кнопка 67002 без «созрело» ничего не делает.
//   A. ЯБЛОКО ДРУЖБЫ (окно 6500): 6049 чужой — пропуск; 10 % → раскрытие, рост 49→50, шестерня + «трясти» →
//      возврат; подсказка 12183 при наведении; 50 % → рост 50→80 и 70 %; 100 % → цепочка 6696→6697→6698 и
//      ЗОЛОТОЕ 6693; клик по нему → 6575 (charId); 6576 снова показывает спрятанное окно 67000; клик 67002 при
//      «созрело» → локальное вращение без пакета, флаг снят.
//   ★ РЕВИЗИЯ 1-Б (М-4): + 6051 (код 2 — без сброса; свой userNum и чужой charId — персонажа нет, без сброса; съел я — сброс),
//     6049 ≥ 100 при НЕраскрытой панели (только раскрыть, без цепочки и флага), подсказка без соулмейта (М-1),
//     окно 2256 гаснет при 5103 (М-2), окно 67000 у якоря 6699 (М-3; контроль — без окна 6500 позиция не трогается),
//     «скрыть панели» (С-1): при паре 6691 прячется и возвращается с окном 6500 (контроль — ключ 0 не возвращает),
//     разрыв 5207 и уход Друга 5226 — сброс панели без возврата.
//   Состояние (charId/userNum/SoulState, отряд, яблоко, окна, ключ) восстанавливается в конце.
//
// Консоль: `asda.UI.SelfTestWave1B [delay=N] [quit]`. Только офлайн (`FAsdaOutgoingGate::RefuseDebugWhenOnline`).
//   Строки проверок — «САМОТЕСТ-1Б», итог — «САМОТЕСТ-1Б ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestSeq.h"   // ★ R5-fix-b 13.11.2026 (заключительный заход, четвёртая запись): общая очередь шагов «по событию»

#if !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "AsdaNetSubsystem.h"
#include "AsdaOutgoingGate.h"                 // RefuseDebugWhenOnline, Block, EAsdaGate
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "Login/AsdaLoginData.h"              // EncodeCp1251 — имена в теле 5107
#include "Pet/AsdaPetSubsystem.h"             // BeginSendCapture / EndSendCapture — перехват исходящих
#include "UI/AsdaPartySubsystem.h"
#include "UI/AsdaMinimapSubsystem.h"
#include "UI/AsdaSoulRouletteSubsystem.h"
#include "UI/AsdaSoulAppleSubsystem.h"
#include "UI/AsdaSoulmateSubsystem.h"         // ShowPopCardFromSnapshot — поднять окно 6500 штатно; HasSoulmatePair
#include "UI/AsdaSkillUseSubsystem.h"         // ★ ревизия М-1: HasSoulmate (gap_878[47])
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaUiHelpers.h"
#include "World/AsdaPossessGate.h"            // AsdaSoul::IsGatedOpcode
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaWorldSubsystem.h"         // DebugSpawnSelfAt — офлайн-павн (штатная точка OnSelfSpawn)
#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaWave1BTest, Log, All);

namespace AsdaWave1BTestLocal
{
	// Счётчики прогона.
	struct FRun
	{
		int32 Ok = 0, Fail = 0;   // пройдено / провалено
		// Одна проверка: печать и счёт.
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaWave1BTest, Log, TEXT("САМОТЕСТ-1Б ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaWave1BTest, Error, TEXT("САМОТЕСТ-1Б FAIL %s"), *What); }
		}
	};

	// Перехваченная отправка: опкод и тело (payload без конверта).
	using FSent = TArray<TPair<int32, TArray<uint8>>>;

	// Контекст многоэтапного прогона (живёт между таймерами).
	struct FCtx
	{
		FRun R;                                   // счётчики
		TWeakObjectPtr<UWorld> W;                 // мир прогона
		TSharedPtr<AsdaSelfTestSeq::FSeq> Seq;    // ★ R5-fix-b 13.11.2026 (заключительный заход, четвёртая запись): очередь ворот (`AsdaSelfTestSeq::When`)
		bool bQuit = false;                       // выйти после последнего этапа
		int32 CharIdWas = 0;                      // свой charId до прогона
		uint16 UserNumWas = 0;                    // свой userNum до прогона
		bool bKnownWas = false;                   // «userNum известен» до прогона
		uint8 SoulWas = 0;                        // свой SoulState до прогона
		int32 MapId = -1;                         // карта мини-карты на момент прогона
		FIntPoint Tile = FIntPoint::ZeroValue;    // тайл игрока
		TSharedPtr<FSent> Sent;                   // перехваченные исходящие (общие для этапов)
		bool bCapture = false;                    // перехват взведён этим прогоном
		int32 Tx6575Base = 0;                     // счётчик 6575 яблока до клика
	};

	static constexpr int32 kMe = 1001, kMate = 2002, kFar = 3003;   // charId участников
	static constexpr uint16 kMyUserNum = 321;                        // мой userNum на прогон
	static constexpr int32 kParty = 42;                              // мой partyIndex

	using AsdaSelfTestCtx::GIOf;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	using AsdaSelfTestCtx::Sub;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// Подать тело через штатный приём (`HandlePacket`).
	static void Feed(const TSharedRef<FCtx>& C, int32 Op, const TArray<uint8>& B)
	{
		if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C)) { Net->DebugFeedPacket(Op, B); }
	}

	// 5109: уровень/HP/MP + тайл + карта одного участника (раскладка `Recv_GM_PARTY_UPDATE_PARAM`).
	static TArray<uint8> Body5109(int32 Cid, int32 Tx, int32 Tz, int32 Map)
	{
		TArray<uint8> B; B.SetNumZeroed(24);
		AsdaBytes::WrI32(&B[0], Cid); B[4] = 10; B[5] = 1; B[6] = 0;
		AsdaBytes::WrI32(&B[7], 100); AsdaBytes::WrI32(&B[11], 80);
		AsdaBytes::WrI16(&B[15], 60); AsdaBytes::WrI16(&B[17], 30);
		AsdaBytes::WrI16(&B[19], Tx); AsdaBytes::WrI16(&B[21], Tz); B[23] = (uint8)Map;
		return B;
	}

	// 5104: позиция соотрядника (раскладка сервера `sub_451A90`).
	static TArray<uint8> Body5104(int32 PartyIdx, int32 Cid, int32 Tx, int32 Tz)
	{
		TArray<uint8> B; B.SetNumZeroed(12);
		AsdaBytes::WrI16(&B[0], PartyIdx); AsdaBytes::WrI32(&B[2], Cid); AsdaBytes::WrI16(&B[6], 77);
		AsdaBytes::WrI16(&B[8], Tx); AsdaBytes::WrI16(&B[10], Tz);
		return B;
	}

	// Сколько перехвачено опкода Op; OutLast — тело последнего.
	static int32 CountOp(const FSent& S, int32 Op, TArray<uint8>* OutLast = nullptr)
	{
		int32 N = 0;
		for (const TPair<int32, TArray<uint8>>& P : S) { if (P.Key == Op) { ++N; if (OutLast) { *OutLast = P.Value; } } }
		return N;
	}

	// Центр живой геометрии узла окна (`GetCachedGeometry`); false — не отрисован.
	static bool CenterOf(UWidget* W, FVector2D& Out)
	{
		FVector2D Mn, Mx;
		if (!W || !AsdaUi::WidgetAbsRect(W, Mn, Mx)) { return false; }
		Out = (Mn + Mx) * 0.5f;
		return true;
	}

	// Настоящий клик Slate по центру узла окна WinId (по имени) либо по самому виджету.
	static bool ClickNode(const TSharedRef<FCtx>& C, UWidget* Node, const TCHAR* What)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		FVector2D P = FVector2D::ZeroVector;
		const bool bGeo = WM && CenterOf(Node, P);
		C->R.Expect(bGeo, FString::Printf(TEXT("клик «%s»: живая геометрия есть (центр %.0f,%.0f)"), What, bGeo ? P.X : 0.0, bGeo ? P.Y : 0.0));
		if (bGeo) { WM->SimulateClickAt(P, /*HoldFrames*/ 3); }
		return bGeo;
	}
	static UWidget* WinNode(const TSharedRef<FCtx>& C, const TCHAR* WinId, const TCHAR* Name)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		UUserWidget* W = WM ? WM->FindWindow(FName(WinId)) : nullptr;
		return W ? W->GetWidgetFromName(FName(Name)) : nullptr;
	}

	using AsdaSelfTestCtx::ByStage::After;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	static void Finish(TSharedRef<FCtx> C);
	static void SParty(TSharedRef<FCtx> C);
	static void SMarks(TSharedRef<FCtx> C);
	static void SDisbandOpen(TSharedRef<FCtx> C);
	static void SDisbandOk(TSharedRef<FCtx> C);
	static void SDisbandOpen2(TSharedRef<FCtx> C);
	static void SDisbandCancel(TSharedRef<FCtx> C);
	static void SNonLeader(TSharedRef<FCtx> C);
	static void SNonLeaderCheck(TSharedRef<FCtx> C);
	static void SKeyOff(TSharedRef<FCtx> C);
	static void SKeyOffCheck(TSharedRef<FCtx> C);
	static void SDestroyed(TSharedRef<FCtx> C);
	static void SRouletteSpun(TSharedRef<FCtx> C);
	static void SAppleBound(TSharedRef<FCtx> C);
	static void SAppleExpand(TSharedRef<FCtx> C);
	static void SAppleBack(TSharedRef<FCtx> C);
	static void SAppleTip(TSharedRef<FCtx> C);
	static void SAppleGrow(TSharedRef<FCtx> C);
	static void SAppleGrown(TSharedRef<FCtx> C);
	static void SAppleRipe(TSharedRef<FCtx> C);
	static void SAppleGolden(TSharedRef<FCtx> C);
	static void SGoldenClicked(TSharedRef<FCtx> C);
	static void SSpinBtnClicked(TSharedRef<FCtx> C);
	static void SGift(TSharedRef<FCtx> C);
	static void SPairHidden(TSharedRef<FCtx> C);
	static void SPairRestored(TSharedRef<FCtx> C);
	static void SKeyZeroRestored(TSharedRef<FCtx> C);
	static void SBreakDone(TSharedRef<FCtx> C);
	static void SLeftDone(TSharedRef<FCtx> C);
	static void SAppleTipNoMate(TSharedRef<FCtx> C);

	// Взвести общий перехват исходящих (на всю длину этапов отряда и рулетки).
	static void ArmCapture(TSharedRef<FCtx> C)
	{
		if (C->bCapture) { return; }
		UAsdaPetSubsystem* Pet = Sub<UAsdaPetSubsystem>(C);
		C->Sent = MakeShared<FSent>();
		TSharedPtr<FSent> S = C->Sent;
		C->bCapture = Pet && Pet->BeginSendCapture([S](int32 Op, const TArray<uint8>& Body, bool, TCHAR)
		{
			S->Add(TPair<int32, TArray<uint8>>(Op, Body));
		}, /*TestCharId*/ 0);
		C->R.Expect(C->bCapture, TEXT("перехват исходящих взведён"));
	}

	static void Run2(TSharedRef<FCtx> C);

	// ════════════════ 0. Офлайн своего павна нет — штатный спавн (та же точка, что 4003), мини-карте нужен павн и сетка ════════════════
	static void Run(TSharedRef<FCtx> C)
	{
		UAsdaWorldSubsystem* WS = Sub<UAsdaWorldSubsystem>(C);
		// Как в самотесте отправителей charId (`AsdaCharIdSendersSelfTest.cpp`, этап K): офлайн у загрузчика нет
		//   текущей карты, поэтому сперва объявляем её (alpen = 3), затем штатный спавн своего павна.
		if (WS && !::IsValid(WS->LocalPlayerActor.Get()) && GEngine && C->W.Get())
		{
			GEngine->Exec(C->W.Get(), TEXT("Asda.Load.CurrentMap 3"));
			WS->DebugSpawnSelfAt(256, 256, TEXT("СамТест1Б"));
		}
		After(C, 1.5f, &Run2);
	}

	// ════════════════ 0б. Свои идентификаторы, отряд (5093 + 5107) ════════════════
	static void Run2(TSharedRef<FCtx> C)
	{
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
		UAsdaPartySubsystem* Party = Sub<UAsdaPartySubsystem>(C);
		if (!Net || !Party) { C->R.Expect(false, TEXT("нет подсистем сети/отряда")); Finish(C); return; }
		C->CharIdWas = Net->SelectedCharId; C->UserNumWas = Net->SelfUserNum;
		C->bKnownWas = Net->bSelfUserNumKnown; C->SoulWas = Net->SelfSoulState;
		Net->SelectedCharId = kMe; Net->SelfUserNum = kMyUserNum; Net->bSelfUserNumKnown = true;
		if (UAsdaMinimapSubsystem* MM = Sub<UAsdaMinimapSubsystem>(C)) { C->MapId = MM->GetDrawnMapId(); }
		const UAsdaWorldSubsystem* WS = Sub<UAsdaWorldSubsystem>(C);
		if (const AAsdaPlayerCharacter* P = WS ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor) : nullptr)
		{
			const FVector2D Pos = P->GetGamePosXZ();
			C->Tile = FIntPoint(FMath::FloorToInt(Pos.X / 100.f), FMath::FloorToInt(Pos.Y / 100.f));
		}
		C->R.Expect(C->MapId >= 0, FString::Printf(TEXT("мини-карта рисует карту %d, игрок в тайле (%d,%d)"), C->MapId, C->Tile.X, C->Tile.Y));
		ArmCapture(C);

		// 5093 код 1: отряд 42 создан, запросил я (charId 1001).
		TArray<uint8> B; B.SetNumZeroed(15);
		B[0] = 1; AsdaBytes::WrI16(&B[1], kMyUserNum); AsdaBytes::WrI32(&B[3], kMe);
		AsdaBytes::WrI16(&B[7], 322); AsdaBytes::WrI32(&B[9], kMate); AsdaBytes::WrI16(&B[13], kParty);
		Feed(C, 5093, B);
		// 5107: полный состав, слот 0 — глава (я).
		TArray<uint8> R; R.SetNumZeroed(144);
		const int32 Ids[6] = { kMe, kMate, kFar, -1, -1, -1 };
		for (int32 i = 0; i < 6; ++i)
		{
			AsdaBytes::WrI32(&R[4 * i], Ids[i]);
			if (Ids[i] != -1) { FAsdaLoginData::EncodeCp1251(FString::Printf(TEXT("Member%d"), i), &R[24 + 20 * i], 19); }
		}
		Feed(C, 5107, R);
		After(C, 0.4f, &SParty);
	}

	// ════════════════ P + M(до координат) ════════════════
	static void SParty(TSharedRef<FCtx> C)
	{
		UAsdaPartySubsystem* Party = Sub<UAsdaPartySubsystem>(C);
		UAsdaMinimapSubsystem* MM = Sub<UAsdaMinimapSubsystem>(C);
		FRun& R = C->R;
		if (!Party || !MM) { R.Expect(false, TEXT("нет подсистем отряда/мини-карты")); Finish(C); return; }
		R.Expect(Party->IsInParty() && Party->IsPartyLeader() && Party->GetPartyIndex() == kParty,
			FString::Printf(TEXT("P: в отряде=%d глава=%d partyIndex=%d (ждём 1/1/%d)"), Party->IsInParty(), Party->IsPartyLeader(), Party->GetPartyIndex(), kParty));
		R.Expect(Party->GetMembers().Num() == 3 && Party->IsRosterVisible(), FString::Printf(TEXT("P: состав %d (ждём 3), панель 2200 видна=%d"), Party->GetMembers().Num(), Party->IsRosterVisible()));
		const TArray<FAsdaMapMark>* M0 = MM->FindMarks(EAsdaMapMark::Party);
		R.Expect(M0 && M0->Num() == 0, FString::Printf(TEXT("M: до 5104/5109 отметок отряда нет (список %s, %d) — исправление «тайл (0,0) карты 0»"),
			M0 ? TEXT("есть") : TEXT("нет"), M0 ? M0->Num() : -1));
		// 5109: я (своя карта — должен быть ИСКЛЮЧЁН), 2002 (своя карта), 3003 (чужая карта).
		Feed(C, 5109, Body5109(kMe,   C->Tile.X, C->Tile.Y, C->MapId));
		Feed(C, 5109, Body5109(kMate, C->Tile.X + 3, C->Tile.Y + 2, C->MapId));
		Feed(C, 5109, Body5109(kFar,  C->Tile.X + 1, C->Tile.Y + 1, (C->MapId + 1) % 23));
		// 5104: чужой отряд 43 — пропуск (контроль); свой 42 — новый тайл 2002.
		Feed(C, 5104, Body5104(43, kMate, C->Tile.X - 9, C->Tile.Y - 9));
		Feed(C, 5104, Body5104(kParty, kMate, C->Tile.X + 5, C->Tile.Y + 4));
		After(C, 0.6f, &SMarks);
	}

	// ════════════════ M: список и отрисовка ════════════════
	static void SMarks(TSharedRef<FCtx> C)
	{
		UAsdaMinimapSubsystem* MM = Sub<UAsdaMinimapSubsystem>(C);
		UAsdaPartySubsystem* Party = Sub<UAsdaPartySubsystem>(C);
		FRun& R = C->R;
		const TArray<FAsdaMapMark>* M = MM ? MM->FindMarks(EAsdaMapMark::Party) : nullptr;
		int32 NMate = 0, NFar = 0, NOther = 0;   // отметки по карте/тайлу
		if (M)
		{
			for (const FAsdaMapMark& K : *M)
			{
				if (K.MapId == C->MapId && K.TileX == C->Tile.X + 5 && K.TileY == C->Tile.Y + 4) { ++NMate; }
				else if (K.MapId == (C->MapId + 1) % 23) { ++NFar; }
				else { ++NOther; }
			}
		}
		R.Expect(M && M->Num() == 2 && NMate == 1 && NFar == 1 && NOther == 0,
			FString::Printf(TEXT("M: отметок отряда %d (ждём 2): 2002 в тайле 5104 на моей карте=%d, 3003 на чужой=%d, прочих=%d (себя и отряд 43 нет)"),
				M ? M->Num() : -1, NMate, NFar, NOther));
		TArray<UImage*> Party_, Self_;
		const int32 Drawn = MM ? MM->CountDrawnMarks(EAsdaMapMark::Party, false, &Party_) : -1;
		if (MM) { MM->CountDrawnMarks(EAsdaMapMark::Player, false, &Self_); }
		R.Expect(Drawn == 1, FString::Printf(TEXT("M: на мини-карте нарисован %d квад miniPartyPos (ждём 1 — 3003 с чужой карты не рисуется)"), Drawn));
		// ★ Направление сверяем по РАСКЛАДКЕ (слоты одной канвы маркеров): у значка игрока рендер-поворот на курс,
		//   и `GetCachedGeometry` повёрнутого виджета даёт угол ПОВЁРНУТОЙ рамки — по ней «центр» не считается
		//   (первый прогон: −13 px при верных слотах +2.5/+2.0). Живая геометрия — у самой отметки (не повёрнута).
		FVector2D Pm = FVector2D::ZeroVector;
		const UCanvasPanelSlot* SlP = Party_.Num() == 1 ? AsdaUi::SlotOf(Party_[0]) : nullptr;
		const UCanvasPanelSlot* SlS = Self_.Num() == 1 ? AsdaUi::SlotOf(Self_[0]) : nullptr;
		const bool bG = SlP && SlS && CenterOf(Party_[0], Pm);
		const float Dx = bG ? SlP->GetOffsets().Left - SlS->GetOffsets().Left : 0.f;
		const float Dy = bG ? SlP->GetOffsets().Top - SlS->GetOffsets().Top : 0.f;
		R.Expect(bG && Dx > 0.f && Dy > 0.f,
			FString::Printf(TEXT("M: отметка отрисована (центр %.0f,%.0f) и смещена от значка игрока на (%+.1f,%+.1f) px — в сторону тайла (+5,+4)"),
				Pm.X, Pm.Y, Dx, Dy));
		// Диагностика соответствия «тайл → пиксель» (печать слотов и формулы мини-карты) — для разбора при сбое.
		if (MM)
		{
			const UAsdaWorldSubsystem* WS = Sub<UAsdaWorldSubsystem>(C);
			const AAsdaPlayerCharacter* Pc = WS ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor) : nullptr;
			const FVector2D Pos = Pc ? Pc->GetGamePosXZ() : FVector2D::ZeroVector;
			auto SlotStr = [](const UImage* I)
			{
				const UCanvasPanelSlot* S = I ? AsdaUi::SlotOf(I) : nullptr;
				return S ? FString::Printf(TEXT("(%.1f,%.1f %.0fx%.0f)"), S->GetOffsets().Left, S->GetOffsets().Top, S->GetOffsets().Right, S->GetOffsets().Bottom) : FString(TEXT("—"));
			};
			UE_LOG(LogAsdaWave1BTest, Log, TEXT("САМОТЕСТ-1Б инфо: %s"), *MM->DescribeState());
			UE_LOG(LogAsdaWave1BTest, Log, TEXT("САМОТЕСТ-1Б инфо: игрок %s; отметка тайла %s; слот игрока %s, слот отметки %s"),
				*MM->DescribeProbe(Pos.X, Pos.Y), *MM->DescribeProbe(100.f * (C->Tile.X + 5) + 50.f, 100.f * (C->Tile.Y + 4) + 50.f),
				*SlotStr(Self_.Num() ? Self_[0] : nullptr), *SlotStr(Party_.Num() ? Party_[0] : nullptr));
		}
		if (bG)
		{
			FVector2D Lmn, Lmx;
			const bool bL = AsdaUi::WidgetAbsRect(MM->GetMarkLayer(false), Lmn, Lmx);
			R.Expect(bL && Pm.X >= Lmn.X && Pm.Y >= Lmn.Y && Pm.X <= Lmx.X && Pm.Y <= Lmx.Y,
				TEXT("M: квад отметки внутри слоя маркеров мини-карты"));
		}
		// ── D: у главы клик по 2260 «Выход» → окно 2256, пакета нет ──
		C->Sent->Reset();
		ClickNode(C, WinNode(C, TEXT("PARTY_WIN"), TEXT("PARTY_EXITBUT")), TEXT("2260 Выход (глава)"));
		After(C, 0.6f, &SDisbandOpen);
	}

	static FString DisbandText(const TSharedRef<FCtx>& C)
	{
		const UTextBlock* T = Cast<UTextBlock>(WinNode(C, TEXT("PARTYDISPERSION"), TEXT("PARTYDISPERSION_MESSAGE")));
		return T ? T->GetText().ToString() : FString(TEXT("<нет узла>"));
	}

	static void SDisbandOpen(TSharedRef<FCtx> C)
	{
		UAsdaPartySubsystem* Party = Sub<UAsdaPartySubsystem>(C);
		FRun& R = C->R;
		const FString Exp = AsdaNotice::Loc(GIOf(C), 1170);
		R.Expect(Party && Party->IsDisbandOpen(), TEXT("D: у главы 2260 открыл окно роспуска 2256"));
		R.Expect(!Exp.IsEmpty() && DisbandText(C) == Exp, FString::Printf(TEXT("D: текст 2257 = loc 1170 «%s» (в окне «%s»)"), *Exp, *DisbandText(C)));
		R.Expect(CountOp(*C->Sent, 5098) == 0, FString::Printf(TEXT("D: при показе окна 5098 НЕ ушёл (×%d)"), CountOp(*C->Sent, 5098)));
		ClickNode(C, WinNode(C, TEXT("PARTYDISPERSION"), TEXT("PARTYDISPERSION_OK")), TEXT("2258 ОК"));
		After(C, 0.6f, &SDisbandOk);
	}

	static void SDisbandOk(TSharedRef<FCtx> C)
	{
		UAsdaPartySubsystem* Party = Sub<UAsdaPartySubsystem>(C);
		FRun& R = C->R;
		TArray<uint8> L;
		const int32 N = CountOp(*C->Sent, 5098, &L);
		R.Expect(N == 1 && L.Num() >= 3 && AsdaBytes::RdI16(&L[0]) == kParty && L[2] == 1,
			FString::Printf(TEXT("D: «ОК» → 5098 ×%d, partyIndex %d, режим %d (ждём ×1, %d, 1 = роспуск)"),
				N, L.Num() >= 2 ? AsdaBytes::RdI16(&L[0]) : -1, L.Num() >= 3 ? (int32)L[2] : -1, kParty));
		R.Expect(Party && !Party->IsDisbandOpen(), TEXT("D: после «ОК» окно 2256 спрятано"));
		C->Sent->Reset();
		ClickNode(C, WinNode(C, TEXT("PARTY_WIN"), TEXT("PARTY_EXITBUT")), TEXT("2260 Выход (глава, второй раз)"));
		After(C, 0.6f, &SDisbandOpen2);
	}

	static void SDisbandOpen2(TSharedRef<FCtx> C)
	{
		UAsdaPartySubsystem* Party = Sub<UAsdaPartySubsystem>(C);
		C->R.Expect(Party && Party->IsDisbandOpen(), TEXT("D: окно 2256 открыто снова"));
		ClickNode(C, WinNode(C, TEXT("PARTYDISPERSION"), TEXT("PARTYDISPERSION_CANCEL")), TEXT("2259 Отмена"));
		After(C, 0.6f, &SDisbandCancel);
	}

	static void SDisbandCancel(TSharedRef<FCtx> C)
	{
		UAsdaPartySubsystem* Party = Sub<UAsdaPartySubsystem>(C);
		FRun& R = C->R;
		TArray<uint8> L;
		const int32 N = CountOp(*C->Sent, 5098, &L);
		R.Expect(N == 1 && L.Num() >= 3 && L[2] == 0,
			FString::Printf(TEXT("D: «Отмена» → 5098 ×%d режим %d (ждём ×1, 0 = просто выход, как обработчик эталона)"), N, L.Num() >= 3 ? (int32)L[2] : -1));
		R.Expect(Party && !Party->IsDisbandOpen(), TEXT("D: после «Отмены» окно 2256 спрятано"));
		// Контроль: глава — другой (5102 → 2002).
		TArray<uint8> B; B.SetNumZeroed(4); AsdaBytes::WrI32(&B[0], kMate);
		Feed(C, 5102, B);
		After(C, 0.3f, &SNonLeader);
	}

	static void SNonLeader(TSharedRef<FCtx> C)
	{
		UAsdaPartySubsystem* Party = Sub<UAsdaPartySubsystem>(C);
		FRun& R = C->R;
		R.Expect(Party && !Party->IsPartyLeader(), TEXT("D-контроль: после 5102 я не глава"));
		R.Expect(Party && !Party->ShowDisbandConfirm(), TEXT("D-контроль: не главе функция показа 0xB99C30 отказывает (гейт Party_IsMeBoss)"));
		C->Sent->Reset();
		ClickNode(C, WinNode(C, TEXT("PARTY_WIN"), TEXT("PARTY_EXITBUT")), TEXT("2260 Выход (рядовой)"));
		After(C, 0.6f, &SNonLeaderCheck);
	}

	static void SNonLeaderCheck(TSharedRef<FCtx> C)
	{
		UAsdaPartySubsystem* Party = Sub<UAsdaPartySubsystem>(C);
		FRun& R = C->R;
		TArray<uint8> L;
		const int32 N = CountOp(*C->Sent, 5098, &L);
		R.Expect(N == 1 && L.Num() >= 3 && L[2] == 0 && Party && !Party->IsDisbandOpen(),
			FString::Printf(TEXT("D-контроль: рядовой — 2260 сразу шлёт 5098 ×%d режим %d, окна нет (как оригинал)"), N, L.Num() >= 3 ? (int32)L[2] : -1));
		// Вернуть главенство и выключить ключ.
		TArray<uint8> B; B.SetNumZeroed(4); AsdaBytes::WrI32(&B[0], kMe);
		Feed(C, 5102, B);
		AsdaCVarSave::SetForTest(TEXT("asda.Party.FixDisbandConfirm"), 0);
		After(C, 0.3f, &SKeyOff);
	}

	static void SKeyOff(TSharedRef<FCtx> C)
	{
		UAsdaPartySubsystem* Party = Sub<UAsdaPartySubsystem>(C);
		C->R.Expect(Party && Party->IsPartyLeader(), TEXT("D-контроль: после 5102 я снова глава"));
		C->Sent->Reset();
		ClickNode(C, WinNode(C, TEXT("PARTY_WIN"), TEXT("PARTY_EXITBUT")), TEXT("2260 Выход (глава, ключ 0)"));
		After(C, 0.6f, &SKeyOffCheck);
	}

	static void SKeyOffCheck(TSharedRef<FCtx> C)
	{
		UAsdaPartySubsystem* Party = Sub<UAsdaPartySubsystem>(C);
		FRun& R = C->R;
		TArray<uint8> L;
		const int32 N = CountOp(*C->Sent, 5098, &L);
		R.Expect(N == 1 && L.Num() >= 3 && L[2] == 0 && Party && !Party->IsDisbandOpen(),
			FString::Printf(TEXT("D-контроль: ключ FixDisbandConfirm 0 — у главы 5098 ×%d режим %d сразу, окна нет (оригинал)"), N, L.Num() >= 3 ? (int32)L[2] : -1));
		AsdaCVarSave::SetForTest(TEXT("asda.Party.FixDisbandConfirm"), 1);
		// ★ Ревизия М-2: окно 2256 открыто у главы — 5103 обязан его погасить.
		R.Expect(Party && Party->ShowDisbandConfirm() && Party->IsDisbandOpen(), TEXT("D/М-2: перед 5103 окно 2256 открыто у главы"));
		// 5103: отряд 42 распущен.
		TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], kParty);
		Feed(C, 5103, B);
		After(C, 0.4f, &SDestroyed);
	}

	// ════════════════ M-контроль после 5103, G (гейт души), R (рулетка) ════════════════
	static void SDestroyed(TSharedRef<FCtx> C)
	{
		UAsdaPartySubsystem* Party = Sub<UAsdaPartySubsystem>(C);
		UAsdaMinimapSubsystem* MM = Sub<UAsdaMinimapSubsystem>(C);
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
		UAsdaSoulRouletteSubsystem* Ro = Sub<UAsdaSoulRouletteSubsystem>(C);
		FRun& R = C->R;
		const TArray<FAsdaMapMark>* M = MM ? MM->FindMarks(EAsdaMapMark::Party) : nullptr;
		R.Expect(Party && !Party->IsInParty() && (!M || M->Num() == 0),
			FString::Printf(TEXT("M-контроль: после 5103 не в отряде, отметок отряда %d"), M ? M->Num() : 0));
		R.Expect(MM && MM->CountDrawnMarks(EAsdaMapMark::Party, false) == 0, TEXT("M-контроль: на мини-карте квадов отряда 0"));
		R.Expect(Party && !Party->IsDisbandOpen(), TEXT("D/М-2: после 5103 окно роспуска 2256 спрятано"));

		// ── G: гейт «души» ──
		if (Net)
		{
			R.Expect(AsdaSoul::IsGatedOpcode(4070), TEXT("G: 4070 есть в таблице гейта «души» (sub_52EEA0, case 0xFE6)"));
			Net->SetSelfSoulState(1);
			R.Expect(FAsdaOutgoingGate::Block(Net, 4070, EAsdaGate::Soul), TEXT("G: SoulState 1 — 4070 режется"));
			R.Expect(!FAsdaOutgoingGate::Block(Net, 4071, EAsdaGate::Soul), TEXT("G-контроль: SoulState 1 — 4071 (нет в диспетчере с гейтом) не режется"));
			R.Expect(FAsdaOutgoingGate::Block(Net, 4054, EAsdaGate::Soul), TEXT("G-контроль: соседняя строка 4054 по-прежнему режется"));
			Net->SetSelfSoulState(0);
			R.Expect(!FAsdaOutgoingGate::Block(Net, 4070, EAsdaGate::Soul), TEXT("G-контроль: SoulState 0 — 4070 не режется"));
			Net->SetSelfSoulState(C->SoulWas);
		}

		// ── R: 6576 открывает окно 67000 ──
		if (!Ro) { R.Expect(false, TEXT("R: нет подсистемы рулетки")); Finish(C); return; }
		Ro->CloseWindow();
		R.Expect(!Ro->IsWindowShown(), TEXT("R: до ответа окно 67000 не показано"));
		Ro->OnSpinButton67002();
		R.Expect(Ro->GetSpinState() == EAsdaRouletteSpin::Idle, TEXT("R-контроль: 67002 без «созрело» колесо не трогает"));
		{ TArray<uint8> B; B.SetNumZeroed(3); B[0] = 10; Feed(C, 6576, B); }
		R.Expect(!Ro->IsWindowShown(), TEXT("R-контроль: 6576 код 10 (отказ) окно не открывает"));
		C->Sent->Reset();
		{ TArray<uint8> B; B.SetNumZeroed(3); B[0] = 1; B[1] = 2; B[2] = 0xFF; Feed(C, 6576, B); }
		R.Expect(Ro->IsWindowShown(), TEXT("R: 6576 код 1 ОТКРЫЛ окно 67000 (эталон roulettePanelHandleNetEvent → UpdateRewardSlots(…,1,1))"));
		int32 BadNodes = 0;   // узлы, найденные не тем типом/не той группой
		for (int32 s = 0; s < 4; ++s)
		{
			const UWidget* G = Ro->GetGfontNode(s);
			const UWidget* Pl = Ro->GetPlateNode(s);
			const UWidget* Pa = Ro->GetPlateAniNode(s);
			if (!G || !G->IsA<UImage>()) { ++BadNodes; }
			if (!Pl || !Pl->IsA<UImage>() || Pl->GetName().Contains(TEXT("_ani"))) { ++BadNodes; }
			if (!Pa || !Pa->GetName().EndsWith(TEXT("_ani_grp"))) { ++BadNodes; }
		}
		R.Expect(BadNodes == 0, FString::Printf(TEXT("R: узлы 67011/67003/67007 четырёх секторов — планы и группы по разметке (ошибок %d)"), BadNodes));
		R.Expect(Ro->GetSpinButton() != nullptr, TEXT("R: кнопка 67002 GOLD_roullete_btn привязана"));
		R.Expect(Ro->GetAnchorTarget().IsZero(), TEXT("R/М-3 контроль: окна 6500 ещё нет — к якорю 6699 окно 67000 не двигалось"));
		R.Expect(Ro->GetSpinState() != EAsdaRouletteSpin::Idle, TEXT("R: колесо крутится"));
		// ★ R5-fix6-c (13.11.2026): ждём СОБЫТИЯ «6579 ушёл» (колесо встало и слой «забрать» отыграл), потолок 30 с, затем 0,3 с — а не
		//   фиксированные 6,5 с. Шаг колеса у порта, как у эталона, при кадре длиннее 1/60 с не растягивается
		//   (`TickSpin`: множитель кадра ≥ 1 не применяется), поэтому под нагрузкой оборот идёт дольше: в красных прогонах
		//   колесо вставало через 7,5 с, 6579 уходил через 9,6 с — проверка в 6,5 с видела 171° и 6579 ×0.
		//   ★ R5-fix-b 13.11.2026 (заключительный заход, четвёртая запись): ворота общей очереди `AsdaSelfTestSeq::When` (потолок 30 с), затем 0,3 с — прежний `Until`.
		AsdaSelfTestSeq::When(C, 0.01f, [C]() { return CountOp(*C->Sent, 6579) >= 1; }, 30.f,
			[C]() { AsdaSelfTestSeq::At(C, 0.3f, [C]() { SRouletteSpun(C); }); });
	}

	static void SRouletteSpun(TSharedRef<FCtx> C)
	{
		UAsdaSoulRouletteSubsystem* Ro = Sub<UAsdaSoulRouletteSubsystem>(C);
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		FRun& R = C->R;
		TArray<uint8> L;
		const int32 N6579 = CountOp(*C->Sent, 6579, &L);
		R.Expect(Ro && FMath::IsNearlyEqual(Ro->GetArrowAngle(), 180.f, 0.5f) && Ro->GetSpinState() == EAsdaRouletteSpin::Idle,
			FString::Printf(TEXT("R: колесо встало на %.1f° (ждём 180 = сектор 2)"), Ro ? Ro->GetArrowAngle() : -1.f));
		R.Expect(N6579 == 1 && L.Num() >= 4 && AsdaBytes::RdI32(&L[0]) == -1,
			FString::Printf(TEXT("R: по анимации «забрать» ушёл 6579 ×%d, тело %d (ждём ×1, −1)"), N6579, L.Num() >= 4 ? AsdaBytes::RdI32(&L[0]) : 0));
		R.Expect(Ro && !Ro->IsWindowShown(), TEXT("R: после «забрать» окно 67000 спрятано (UpdateRewardSlots(…,0,0))"));

		// ── A: окно 6500 штатно (как восстановление слота 11) + показать для геометрии ──
		if (Sm) { Sm->ShowPopCardFromSnapshot(); }
		if (WM) { WM->SetWindowVisible(UAsdaSoulAppleSubsystem::kPopWindowId, true); }
		After(C, 0.4f, &SAppleBound);
	}

	static UWidget* AppleNode(const TSharedRef<FCtx>& C, EAsdaSoulAppleNode N)
	{
		UAsdaSoulAppleSubsystem* A = Sub<UAsdaSoulAppleSubsystem>(C);
		return A ? A->GetNodeWidget(N) : nullptr;
	}
	static bool LiveShown(const TSharedRef<FCtx>& C, EAsdaSoulAppleNode N)
	{
		const UWidget* W = AppleNode(C, N);
		return W && W->GetVisibility() != ESlateVisibility::Collapsed;
	}
	static TArray<uint8> Body6049(uint16 U, int32 Exp, int32 Pct)
	{
		TArray<uint8> B; B.SetNumZeroed(7);
		AsdaBytes::WrI16(&B[0], (int32)U); AsdaBytes::WrI32(&B[2], Exp); B[6] = (uint8)Pct;
		return B;
	}

	static void SAppleBound(TSharedRef<FCtx> C)
	{
		UAsdaSoulAppleSubsystem* A = Sub<UAsdaSoulAppleSubsystem>(C);
		FRun& R = C->R;
		if (!A) { R.Expect(false, TEXT("A: нет подсистемы яблока")); Finish(C); return; }
		R.Expect(A->IsBound(), TEXT("A: панель яблока привязана к окну 6500 (10 узлов 6689…6699)"));
		R.Expect(A->IsNodeShown(EAsdaSoulAppleNode::Apple) && !A->IsNodeShown(EAsdaSoulAppleNode::Golden) && FMath::IsNearlyEqual(A->GetApplePct(), 50.f),
			FString::Printf(TEXT("A: исходно яблоко 6692 видно, 6693 нет, масштаб %.0f %% (конструктор: 50)"), A->GetApplePct()));
		Feed(C, 6049, Body6049(kMyUserNum + 1, 777, 10));
		// ★ Ревизия М-4: первый пакет сразу 100 % — панель НЕ раскрыта (181 = 0), эталон только раскрывает.
		Feed(C, 6049, Body6049(kMyUserNum, 12345, 100));
		After(C, 0.3f, &SAppleExpand);
	}

	static void SAppleExpand(TSharedRef<FCtx> C)
	{
		UAsdaSoulAppleSubsystem* A = Sub<UAsdaSoulAppleSubsystem>(C);
		FRun& R = C->R;
		R.Expect(A->GetAppleExp() == 12345, FString::Printf(TEXT("A-контроль: 6049 чужого userNum пропущен, опыт = %d (ждём 12345 от своего)"), A->GetAppleExp()));
		R.Expect(LiveShown(C, EAsdaSoulAppleNode::AppleBase) && !LiveShown(C, EAsdaSoulAppleNode::Gear) && LiveShown(C, EAsdaSoulAppleNode::AniGear)
			&& LiveShown(C, EAsdaSoulAppleNode::Shake) && !LiveShown(C, EAsdaSoulAppleNode::Apple),
			TEXT("A: первый ненулевой (100 %) → раскрыто, рост 49→50 доигран: шестерня 6690 крутится, 6695 «трясётся», 6692 спрятано"));
		R.Expect(!A->IsEffChain() && !A->IsGoldenFlag() && !LiveShown(C, EAsdaSoulAppleNode::Golden),
			TEXT("A/М-4: 100 % при нераскрытой панели — ТОЛЬКО раскрытие: цепочки нет, флага 495 нет, 6693 нет"));
		After(C, 1.2f, &SAppleBack);
	}

	static void SAppleBack(TSharedRef<FCtx> C)
	{
		FRun& R = C->R;
		R.Expect(LiveShown(C, EAsdaSoulAppleNode::Apple) && !LiveShown(C, EAsdaSoulAppleNode::Shake)
			&& LiveShown(C, EAsdaSoulAppleNode::Gear) && !LiveShown(C, EAsdaSoulAppleNode::AniGear),
			TEXT("A: события 24 — 6695 доиграл 3 прогона → 6692 снова; 6690 доиграла → 6689"));
		FVector2D P;
		if (CenterOf(AppleNode(C, EAsdaSoulAppleNode::Apple), P)) { FSlateApplication::Get().SetCursorPos(P); }
		else { R.Expect(false, TEXT("A: у 6692 нет живой геометрии")); }
		After(C, 0.3f, &SAppleTipNoMate);
	}

	// ★ Ревизия М-1: соулмейта нет (gap_878[47] == −1) — подсказки нет; затем 6152 «свой соулмейт-индекс 5» — есть.
	static void SAppleTipNoMate(TSharedRef<FCtx> C)
	{
		UAsdaItemTooltipSubsystem* Tip = Sub<UAsdaItemTooltipSubsystem>(C);
		const UAsdaSkillUseSubsystem* SkU = Sub<UAsdaSkillUseSubsystem>(C);
		FRun& R = C->R;
		R.Expect(SkU && !SkU->HasSoulmate() && Tip && Tip->GetTipOwner() != EAsdaTipOwner::SoulApple,
			TEXT("A/М-1 контроль: без соулмейта наведение на 6692 подсказку НЕ даёт"));
		TArray<uint8> B; B.SetNumZeroed(12);
		AsdaBytes::WrI32(&B[0], kMe); AsdaBytes::WrI32(&B[4], 5); AsdaBytes::WrI32(&B[8], kMate);
		Feed(C, 6152, B);
		After(C, 0.3f, &SAppleTip);
	}

	static void SAppleTip(TSharedRef<FCtx> C)
	{
		UAsdaItemTooltipSubsystem* Tip = Sub<UAsdaItemTooltipSubsystem>(C);
		FRun& R = C->R;
		R.Expect(Tip && Tip->IsTooltipShown() && Tip->GetTipOwner() == EAsdaTipOwner::SoulApple,
			TEXT("A/М-1: с соулмейтом наведение на 6692 → подсказка loc 12183 (владелец SoulApple)"));
		FSlateApplication::Get().SetCursorPos(FVector2D(2.0, 2.0));
		Feed(C, 6049, Body6049(kMyUserNum, 23456, 50));
		After(C, 0.4f, &SAppleGrow);
	}

	static void SAppleGrow(TSharedRef<FCtx> C)
	{
		UAsdaSoulAppleSubsystem* A = Sub<UAsdaSoulAppleSubsystem>(C);
		UAsdaItemTooltipSubsystem* Tip = Sub<UAsdaItemTooltipSubsystem>(C);
		FRun& R = C->R;
		R.Expect(Tip && Tip->GetTipOwner() != EAsdaTipOwner::SoulApple, TEXT("A: курсор ушёл — подсказка яблока снята"));
		R.Expect(A->IsScaleAnimActive() && A->GetApplePct() > 50.f && A->GetApplePct() < 80.f,
			FString::Printf(TEXT("A: 50 %% впервые → идёт рост 50→80 (сейчас %.0f %%)"), A->GetApplePct()));
		After(C, 1.7f, &SAppleGrown);
	}

	static void SAppleGrown(TSharedRef<FCtx> C)
	{
		UAsdaSoulAppleSubsystem* A = Sub<UAsdaSoulAppleSubsystem>(C);
		FRun& R = C->R;
		const UWidget* Ap = AppleNode(C, EAsdaSoulAppleNode::Apple);
		FVector2D Mn, Mx;
		const bool bG = Ap && AsdaUi::WidgetAbsRect(Ap, Mn, Mx);
		R.Expect(!A->IsScaleAnimActive() && FMath::IsNearlyEqual(A->GetApplePct(), 70.f),
			FString::Printf(TEXT("A: рост доигран (событие 24, a7 = 3) → 70 %% (сейчас %.0f %%)"), A->GetApplePct()));
		R.Expect(bG, FString::Printf(TEXT("A: живой размер 6692 %.1f×%.1f px"), bG ? (Mx - Mn).X : 0.0, bG ? (Mx - Mn).Y : 0.0));
		Feed(C, 6049, Body6049(kMyUserNum, 34567, 100));
		After(C, 0.3f, &SAppleRipe);
	}

	static void SAppleRipe(TSharedRef<FCtx> C)
	{
		UAsdaSoulAppleSubsystem* A = Sub<UAsdaSoulAppleSubsystem>(C);
		FRun& R = C->R;
		R.Expect(A->IsEffChain() && A->GetEffStep() == 0 && LiveShown(C, EAsdaSoulAppleNode::Eff1) && !LiveShown(C, EAsdaSoulAppleNode::Apple)
			&& !LiveShown(C, EAsdaSoulAppleNode::Golden) && A->IsGoldenFlag(),
			TEXT("A: 100 % → «созрело»: 6692 спрятано, 6696 играет, 6693 ещё нет, флаг 495 взведён"));
		After(C, 2.6f, &SAppleGolden);
	}

	static void SAppleGolden(TSharedRef<FCtx> C)
	{
		UAsdaSoulAppleSubsystem* A = Sub<UAsdaSoulAppleSubsystem>(C);
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
		FRun& R = C->R;
		R.Expect(A->GetEffStep() == 2 && LiveShown(C, EAsdaSoulAppleNode::Eff2) && LiveShown(C, EAsdaSoulAppleNode::Eff3) && LiveShown(C, EAsdaSoulAppleNode::Golden),
			FString::Printf(TEXT("A: цепочка 6696 → 6697 → 6698 дошла (шаг %d), ЗОЛОТОЕ 6693 показано"), A->GetEffStep()));
		C->Sent->Reset();
		if (Net) { Net->SelectedCharId = kMe; }
		ClickNode(C, AppleNode(C, EAsdaSoulAppleNode::Golden), TEXT("6693 Золотое яблоко"));
		After(C, 0.5f, &SGoldenClicked);
	}

	static void SGoldenClicked(TSharedRef<FCtx> C)
	{
		UAsdaSoulRouletteSubsystem* Ro = Sub<UAsdaSoulRouletteSubsystem>(C);
		FRun& R = C->R;
		TArray<uint8> L;
		const int32 N = CountOp(*C->Sent, 6575, &L);
		R.Expect(N == 1 && L.Num() >= 4 && AsdaBytes::RdI32(&L[0]) == kMe,
			FString::Printf(TEXT("A: клик по 6693 → 6575 ×%d, charId %d (ждём ×1, %d)"), N, L.Num() >= 4 ? AsdaBytes::RdI32(&L[0]) : 0, kMe));
		// Ответ сервера 6576 код 1 → окно 67000 было спрятано («забрать») — показывается снова (ветка «есть, но спрятано»).
		{ TArray<uint8> B; B.SetNumZeroed(3); B[0] = 1; B[1] = 3; B[2] = 0xFF; Feed(C, 6576, B); }
		R.Expect(Ro && Ro->IsWindowShown(), TEXT("R: 6576 код 1 снова показал спрятанное окно 67000"));
		C->Sent->Reset();
		After(C, 0.3f, [](TSharedRef<FCtx> C2)
		{
			UAsdaSoulRouletteSubsystem* Ro2 = Sub<UAsdaSoulRouletteSubsystem>(C2);
			UAsdaSoulAppleSubsystem* A2 = Sub<UAsdaSoulAppleSubsystem>(C2);
			// ★ Ревизия М-3: подложка 67050 встала у якоря 6699: (якорь.X + 57 − 256 + 5, якорь.Y − 75) в единицах макета.
			FVector2D Gm = FVector2D::ZeroVector, Gx = Gm, Mm = Gm, Mx = Gm;
			UWidget* Glass = A2 ? A2->GetGlassAnchor() : nullptr;
			UWidget* Main = WinNode(C2, TEXT("GOLD_roullete_WIN"), TEXT("GOLD_roullete_main_1_0"));
			const bool bOk = Ro2 && !Ro2->IsAnchorPending() && Glass && Main && AsdaUi::WidgetAbsRect(Glass, Gm, Gx) && AsdaUi::WidgetAbsRect(Main, Mm, Mx);
			const float Sc = Main ? Main->GetCachedGeometry().Scale : 1.f;
			const FVector2D Want(Gm.X + (57.f - 256.f + 5.f) * Sc, Gm.Y - 75.f * Sc);
			C2->R.Expect(bOk && FVector2D::Distance(Mm, Want) < 2.0,
				FString::Printf(TEXT("R/М-3: окно 67000 у якоря 6699 — подложка в (%.0f,%.0f), ждём (%.0f,%.0f)"), Mm.X, Mm.Y, Want.X, Want.Y));
			ClickNode(C2, Ro2 ? Ro2->GetSpinButton() : nullptr, TEXT("67002 крутить (созрело)"));
			After(C2, 0.4f, &SSpinBtnClicked);
		});
	}

	static void SSpinBtnClicked(TSharedRef<FCtx> C)
	{
		UAsdaSoulAppleSubsystem* A = Sub<UAsdaSoulAppleSubsystem>(C);
		UAsdaSoulRouletteSubsystem* Ro = Sub<UAsdaSoulRouletteSubsystem>(C);
		FRun& R = C->R;
		R.Expect(A && !A->IsGoldenFlag(), TEXT("A: клик 67002 при «созрело» снял флаг 495"));
		R.Expect(CountOp(*C->Sent, 6575) == 0 && CountOp(*C->Sent, 6579) == 0,
			TEXT("A: клик 67002 — пакета нет (локальное вращение)"));
		R.Expect(Ro && Ro->GetSpinState() != EAsdaRouletteSpin::Idle, TEXT("A: колесо крутится после 67002"));
		// ── ★ М-4: 6051 «съедено» ──
		auto Gift = [&C](uint8 Code, int32 Cid)
		{
			TArray<uint8> B; B.SetNumZeroed(14);
			B[0] = Code; AsdaBytes::WrI16(&B[1], kMyUserNum); AsdaBytes::WrI32(&B[3], Cid);
			Feed(C, 6051, B);
		};
		Gift(2, kMe);       // код 2 — только плашка 8167
		Gift(0, 9999);      // свой userNum, charId неизвестен — персонажа нет, выход без сброса
		After(C, 0.3f, &SGift);
	}

	static void SGift(TSharedRef<FCtx> C)
	{
		FRun& R = C->R;
		R.Expect(LiveShown(C, EAsdaSoulAppleNode::Golden),
			TEXT("A/М-4 контроль: 6051 код 2 и «персонажа нет» вид НЕ сбрасывают — 6693 на месте"));
		TArray<uint8> B; B.SetNumZeroed(14);
		B[0] = 0; AsdaBytes::WrI16(&B[1], kMyUserNum); AsdaBytes::WrI32(&B[3], kMe);
		Feed(C, 6051, B);   // съел я — персонаж найден → звук и `uiWindow_ResetView`
		After(C, 0.3f, [](TSharedRef<FCtx> C2)
		{
			UAsdaSoulAppleSubsystem* A = Sub<UAsdaSoulAppleSubsystem>(C2);
			C2->R.Expect(A && !A->IsEffChain() && !LiveShown(C2, EAsdaSoulAppleNode::Golden) && LiveShown(C2, EAsdaSoulAppleNode::Apple)
				&& FMath::IsNearlyEqual(A->GetApplePct(), 50.f),
				TEXT("A/М-4: 6051 «съел я» → вид сброшен: 6693 спрятано, 6692 видно, 50 %"));
			// ── ★ С-1: пара есть (5203 код 1) → «закрыть всё» прячет 6691, состояние яблока сохраняется ──
			TArray<uint8> Bd; Bd.SetNumZeroed(9 + 20);
			Bd[0] = 1; AsdaBytes::WrI32(&Bd[1], 7); AsdaBytes::WrI32(&Bd[5], kMyUserNum);
			FAsdaLoginData::EncodeCp1251(TEXT("Mate"), &Bd[9], 19);
			Feed(C2, 5203, Bd);
			if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C2)) { WM->ClearMsgBoxQueue(TEXT("самотест 1-Б: нотис 5203")); }
			UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C2);
			C2->R.Expect(Sm && Sm->HasSoulmatePair(), TEXT("С-1: 5203 код 1 — пара есть (gap_8B4[6])"));
			if (Sm) { Sm->HidePopCardForCloseAll(); }
			After(C2, 0.3f, &SPairHidden);
		});
	}

	static void SPairHidden(TSharedRef<FCtx> C)
	{
		UAsdaSoulAppleSubsystem* A = Sub<UAsdaSoulAppleSubsystem>(C);
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		FRun& R = C->R;
		R.Expect(WM && !WM->IsWindowVisible(UAsdaSoulAppleSubsystem::kPopWindowId) && A && !A->IsNodeShown(EAsdaSoulAppleNode::AppleBase)
			&& A->IsBaseRestorePending() && A->IsNodeShown(EAsdaSoulAppleNode::Apple),
			TEXT("С-1: «закрыть всё» при паре — 6500 и группа 6691 спрятаны, яблоко не сброшено, возврат ждёт окно 6500"));
		if (WM) { WM->SetWindowVisible(UAsdaSoulAppleSubsystem::kPopWindowId, true); }   // как восстановление слота 11
		After(C, 0.3f, &SPairRestored);
	}

	static void SPairRestored(TSharedRef<FCtx> C)
	{
		UAsdaSoulAppleSubsystem* A = Sub<UAsdaSoulAppleSubsystem>(C);
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		FRun& R = C->R;
		R.Expect(A && A->IsNodeShown(EAsdaSoulAppleNode::AppleBase) && LiveShown(C, EAsdaSoulAppleNode::AppleBase) && !A->IsBaseRestorePending(),
			TEXT("С-1 [исправление оригинала]: окно 6500 снова видно — группа 6691 вернулась"));
		// Контроль: ключ 0 — как оригинал, панель с окном не возвращается.
		AsdaCVarSave::SetForTest(TEXT("asda.Soul.Apple.FixCloseAllHide"), 0);
		if (Sm) { Sm->HidePopCardForCloseAll(); }
		if (WM) { WM->SetWindowVisible(UAsdaSoulAppleSubsystem::kPopWindowId, true); }
		After(C, 0.3f, &SKeyZeroRestored);
	}

	static void SKeyZeroRestored(TSharedRef<FCtx> C)
	{
		UAsdaSoulAppleSubsystem* A = Sub<UAsdaSoulAppleSubsystem>(C);
		FRun& R = C->R;
		R.Expect(A && !A->IsNodeShown(EAsdaSoulAppleNode::AppleBase) && !A->IsBaseRestorePending(),
			TEXT("С-1 контроль: ключ FixCloseAllHide 0 — группа 6691 с окном 6500 НЕ вернулась (оригинал)"));
		AsdaCVarSave::SetForTest(TEXT("asda.Soul.Apple.FixCloseAllHide"), 1);
		// Разрыв пары 5207 код 1 → `inventoryHidePanelsAndRefresh` без пары: сброс панели и вида.
		TArray<uint8> B; B.SetNumZeroed(5 + 20); B[0] = 1;
		FAsdaLoginData::EncodeCp1251(TEXT("Mate"), &B[5], 19);
		Feed(C, 5207, B);
		After(C, 0.3f, &SBreakDone);
	}

	static void SBreakDone(TSharedRef<FCtx> C)
	{
		UAsdaSoulAppleSubsystem* A = Sub<UAsdaSoulAppleSubsystem>(C);
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		FRun& R = C->R;
		R.Expect(Sm && !Sm->HasSoulmatePair() && A && !A->IsNodeShown(EAsdaSoulAppleNode::AppleBase) && !A->IsBaseRestorePending()
			&& WM && !WM->IsWindowVisible(UAsdaSoulAppleSubsystem::kPopWindowId),
			TEXT("С-1: 5207 код 1 (разрыв) — пары нет, 6500 и 6691 спрятаны, возврата нет (сброс панели)"));
		// Уход Друга 5226: снова пара (5203), окно видно — 5226 снимает пару и гасит панели.
		TArray<uint8> Bd; Bd.SetNumZeroed(9 + 20);
		Bd[0] = 1; AsdaBytes::WrI32(&Bd[1], 7); AsdaBytes::WrI32(&Bd[5], kMyUserNum);
		Feed(C, 5203, Bd);
		if (WM) { WM->ClearMsgBoxQueue(TEXT("самотест 1-Б: нотис 5203")); WM->SetWindowVisible(UAsdaSoulAppleSubsystem::kPopWindowId, true); }
		Feed(C, 5226, TArray<uint8>());
		After(C, 0.3f, &SLeftDone);
	}

	static void SLeftDone(TSharedRef<FCtx> C)
	{
		UAsdaSoulAppleSubsystem* A = Sub<UAsdaSoulAppleSubsystem>(C);
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		C->R.Expect(Sm && !Sm->HasSoulmatePair() && A && !A->IsNodeShown(EAsdaSoulAppleNode::AppleBase) && !A->IsBaseRestorePending()
			&& WM && !WM->IsWindowVisible(UAsdaSoulAppleSubsystem::kPopWindowId),
			TEXT("С-1: 5226 «Друг уходит» — пара снята, окно 6500 и группа 6691 спрятаны"));
		Finish(C);
	}

	// Восстановление состояния, итог, выход.
	static void Finish(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = GIOf(C);
		if (C->bCapture) { if (UAsdaPetSubsystem* Pet = Sub<UAsdaPetSubsystem>(C)) { Pet->EndSendCapture(); } }
		if (UAsdaSoulRouletteSubsystem* Ro = Sub<UAsdaSoulRouletteSubsystem>(C)) { Ro->CloseWindow(); Ro->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
		if (UAsdaSoulAppleSubsystem* A = Sub<UAsdaSoulAppleSubsystem>(C)) { A->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
		if (UAsdaPartySubsystem* P = Sub<UAsdaPartySubsystem>(C)) { P->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
		if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C)) { WM->SetWindowVisible(UAsdaSoulAppleSubsystem::kPopWindowId, false); }
		// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи прогона (Party.FixDisbandConfirm, Soul.Apple.FixCloseAllHide) — к исходному значению И приоритету
		//   SetBy (прежде FixDisbandConfirm возвращался константой 1 с SetByConsole, FixCloseAllHide — шагом прогона).
		AsdaCVarSave::RestoreAll(TEXT("asda.UI.SelfTestWave1B"));
		if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C))
		{
			// ★ М-1: вернуть «соулмейта нет» (6152 со своим charId и индексом −1) — до восстановления charId.
			{ TArray<uint8> B; B.SetNumZeroed(12); AsdaBytes::WrI32(&B[0], kMe); AsdaBytes::WrI32(&B[4], -1); Net->DebugFeedPacket(6152, B); }
			Net->SelectedCharId = C->CharIdWas; Net->SelfUserNum = C->UserNumWas;
			Net->bSelfUserNumKnown = C->bKnownWas; Net->SetSelfSoulState(C->SoulWas);
		}
		UE_LOG(LogAsdaWave1BTest, Log, TEXT("САМОТЕСТ-1Б ИТОГ: ok=%d fail=%d"), C->R.Ok, C->R.Fail);
		if (C->bQuit && GEngine && W) { GEngine->Exec(W, TEXT("quit")); }
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaWave1BSelfTest(
	TEXT("asda.UI.SelfTestWave1B"),
	TEXT("asda.UI.SelfTestWave1B [delay=N] [quit] — ОФЛАЙН-самотест волны 1-Б: роспуск 2256, отметки отряда 5104/5109, гейт души 4070, рулетка 67000, «Яблоко Дружбы» 6049/6693/67002"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.UI.SelfTestWave1B"));   // ★ С1 (ревизия wave0_REVIEW.md, 11.10.2026): мир ушёл до итога — ключи вернёт сторож
		static const uint16 kGuard[] = { 5098, 6575, 6579 };
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.UI.SelfTestWave1B"), KitArgs, 0.f, MakeArrayView(kGuard))) { return; }
		if (!W) { return; }
		float Delay = 0.f;   // задержка запуска, с
		bool bQuit = false;  // выйти после прогона
		for (const FString& Arg : A)
		{
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
		}
		Delay = AsdaSelfTestKit::DelayArgOr(A, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
		TWeakObjectPtr<UWorld> WW = W;
		auto Go = [WW, bQuit]()
		{
			if (!WW.Get()) { return; }
			if (AsdaSelfTestKit::RefuseDeferred(WW.Get(), TEXT("asda.UI.SelfTestWave1B"), bQuit)) { return; }   // Р-2: офлайн повторно — на отложенном старте
			TSharedRef<AsdaWave1BTestLocal::FCtx> C = MakeShared<AsdaWave1BTestLocal::FCtx>();
			C->W = WW; C->bQuit = bQuit;
			AsdaWave1BTestLocal::Run(C);
		};
		if (Delay <= 0.f) { Go(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Go), Delay, false);
	}));

#endif // !UE_BUILD_SHIPPING
