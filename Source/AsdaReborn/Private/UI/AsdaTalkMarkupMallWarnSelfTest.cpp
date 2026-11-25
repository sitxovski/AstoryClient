// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «РАЗМЕТКА ЦВЕТА В РЕПЛИКАХ NPC + ПРЕДУПРЕЖДЕНИЕ МОЛЛА В ГОРОДЕ» (30.10.2026) — путь игрока (-game), только офлайн.
//   Баги игроков №3 и №4 волны bugs8:
//     • №3 «Sowel Shop Lua: Don't forget to load <FC=FFFF2222> sowel </F>!» — строки реплики NPC шли в обычный UTextBlock,
//       теги были видны буквами; составной тег `<FC=AARRGGBB,FB>` разбирался в неверный цвет (ключи `asda.NpcDlg.TalkMarkup`,
//       `asda.Text.CompoundTags`). Проверки: разбор составного тега (rich / сегменты / срез, и прежний разбор при ключе 0),
//       страница `lua_50` у NPC 1 (Lua) и приписка 111929 у NPC 50 — строка с тегами стала URichTextBlock с цветом, «FC=» не видно.
//     • №4 «Caution Message кеш-магазина в городе» — эталон (`uiInventoryConfirmAction@0xC4AD60`, ui_core.c:30542) показывает
//       97100 только вне города (бит 0x80 клетки). Проверки: открытие молла тумблером (хоткей 39 → `Toggle`) в городской клетке —
//       плашки нет; в полевой — есть; ключ `asda.PremiumMall.WarnOutsideTownOnly 0` — в городе снова есть (прежний порт).
//   Строки проверок — «САМОТЕСТ-РАЗМЕТКА-МОЛЛ», итог — «САМОТЕСТ-РАЗМЕТКА-МОЛЛ ИТОГ». Ключи: `delay=N`, `quit`.
#include "AsdaDevConsole.h"              // ASDA_DEV_CONSOLE — дев-команда вне Shipping

#if !UE_BUILD_SHIPPING   // самотесты и стенды в Shipping не входят
#include "UI/AsdaMarkup.h"
#include "Test/AsdaSelfTestKit.h"        // общий каркас самотестов (аргументы, отказ, итог, выход)
#include "AsdaCVarSave.h"                // ключи консоли самотеста — запомнить и вернуть
#include "Test/AsdaSelfTestRunGate.h"   // FAsdaSelfTestRunGate — офлайн повторно на отложенном старте   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "Npc/AsdaNpcDialogSubsystem.h"  // окно разговора 570: открытие, страница, дамп строк
#include "Npc/AsdaNpcActor.h"            // актор NPC — вход тем же путём, что клик мыши
#include "UI/AsdaPremiumMallSubsystem.h" // молл: тумблер хоткея 39
#include "UI/AsdaWindowSubsystem.h"      // окно-сообщение ONEBUT 1500 (плашка 97100)
#include "UI/AsdaNotice.h"               // AsdaNotice::Loc — ожидаемый текст 97100
#include "UI/AsdaUiHelpers.h"            // SrgbHexToLinear — ожидаемый цвет сегмента
#include "World/AsdaZoneSubsystem.h"     // признак «город» по клетке игрока
#include "World/AsdaWorldSubsystem.h"    // свой персонаж
#include "World/AsdaPlayerCharacter.h"   // NetSnap — перенос своего на клетку
#include "World/AsdaGroundTrace.h"       // AsdaGround::GetGrid — сетка клеток карты
#include "World/AsdaWalkGrid.h"          // IsTilePeaceZone / IsTileWalkable / IsTileOverWater
#include "World/AsdaCoord.h"             // GameToTile / TileCenterGame
#include "World/AsdaPlayerController.h"  // SetTarget — выделение NPC, как при клике
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"                 // TActorIterator
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"
#include "Components/Button.h"
#include "Blueprint/UserWidget.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaTalkMarkupTest, Log, All);

namespace
{
	const TCHAR* kCmd    = TEXT("asda.SelfTest.TalkMarkupMallWarn");   // имя команды (итог и возврат ключей)
	const TCHAR* kPrefix = TEXT("САМОТЕСТ-РАЗМЕТКА-МОЛЛ");             // префикс строк проверок
	const FName  kOneBut(TEXT("ONEBUT_MSGBOX"));                       // окно-сообщение 1500
	constexpr int32 kNpcLua     = 1;      // Lua (NSR `lua`, карта 3)
	constexpr int32 kTalkLua50  = 50;     // страница `lua_50` — «…<FC=FFFF2222> живые камни</F>!»
	constexpr int32 kNpcNotice  = 50;     // NPC с припиской 111929 (эталон npc_mob_pet.c:3436)
	constexpr int32 kLocWarnMob = 97100;  // «Осторожнее! …могут напасть монстры!»

	// Текст, нарисованный сейчас в окне-сообщении ONEBUT (пусто — окна нет).
	FString OneButText(UAsdaWindowSubsystem* WM)
	{
		if (!WM->IsOpen(kOneBut)) { return FString(); }
		UUserWidget* Wd = WM->FindWindow(kOneBut);
		return Wd ? UAsdaWindowSubsystem::ReadMsgBoxShownText(Wd->GetWidgetFromName(TEXT("ONEBUT_TEXT"))) : FString();
	}

	// Диагностика сетки: размер, байты бит-пака города, число городских клеток.
	FString GridDiag(const UAsdaWalkGrid* Gd)
	{
		if (!Gd) { return TEXT("сетки нет"); }
		int32 Peace = 0, PeaceWalk = 0, PeaceWater = 0;
		for (int32 Z = 0; Z < Gd->Height; ++Z)
		{
			for (int32 X = 0; X < Gd->Width; ++X)
			{
				if (!Gd->IsTilePeaceZone(X, Z)) { continue; }
				++Peace;
				if (Gd->IsTileWalkable(X, Z)) { ++PeaceWalk; }
				if (Gd->IsTileOverWater(X, Z)) { ++PeaceWater; }
			}
		}
		return FString::Printf(TEXT("сетка %s %d×%d, городских клеток %d, из них проходимых %d, над водой %d"),
			*Gd->GetName(), Gd->Width, Gd->Height, Peace, PeaceWalk, PeaceWater);
	}

	// Ближайшая к игроку проходимая суша (не вода) с нужным признаком города.
	FIntPoint FindTile(const UAsdaWalkGrid* Gd, const FIntPoint& T0, bool bPeace)
	{
		if (!Gd) { return FIntPoint(-1, -1); }
		for (int32 R = 0; R <= 400; ++R)
		{
			for (int32 Dx = -R; Dx <= R; ++Dx)
			{
				for (int32 Dz = -R; Dz <= R; ++Dz)
				{
					if (FMath::Max(FMath::Abs(Dx), FMath::Abs(Dz)) != R) { continue; }
					const int32 X = T0.X + Dx, Z = T0.Y + Dz;
					if (Gd->IsTilePeaceZone(X, Z) == bPeace && Gd->IsTileWalkable(X, Z) && (bPeace || !Gd->IsTileOverWater(X, Z))) { return FIntPoint(X, Z); }
				}
			}
		}
		return FIntPoint(-1, -1);
	}
}

#if ASDA_DEV_CONSOLE   // дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdTalkMarkupMallWarnSelfTest(
	TEXT("asda.SelfTest.TalkMarkupMallWarn"),
	TEXT("Самотест багов bugs8 №3/№4: разметка <FC=..>/<FC=..,FB> в репликах NPC (lua_50, приписка 111929) и предупреждение 97100 "
	     "молла только вне города. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNpcDialogSubsystem* Dlg = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
		UAsdaPremiumMallSubsystem* Mall = GI ? GI->GetSubsystem<UAsdaPremiumMallSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UAsdaZoneSubsystem* Zone = GI ? GI->GetSubsystem<UAsdaZoneSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!Dlg || !Mall || !WM || !Zone || !WS) { AsdaSelfTestKit::Refused(W, kCmd, KitArgs.bQuit, TEXT("нет подсистем")); return; }
		AsdaCVarSave::ArmWorldGuard(W, kCmd);

		struct FStat { int32 Ok = 0; int32 Fail = 0; FIntPoint Town = FIntPoint(-1, -1); FIntPoint Field = FIntPoint(-1, -1); };
		TSharedRef<FStat> Stat = MakeShared<FStat>();
		TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(kCmd, KitArgs.bQuit);
		const float Delay = KitArgs.Delay;
		const bool bQuit = KitArgs.bQuit;
		auto At = [W, WM, Delay, Gate, bQuit](float T, TFunction<void()> Fn)
		{
			FTimerHandle H;
			TWeakObjectPtr<UWorld> WW(W);
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(WM, [Gate, WW, Fn = MoveTemp(Fn)]()
			{
				if (!Gate->Pass(WW.Get())) { return; }
				Fn();
			}), T + Delay, false);
		};
		auto Check = [Stat](bool bOk, const FString& What)
		{
			if (bOk) { ++Stat->Ok; } else { ++Stat->Fail; }
			UE_LOG(LogAsdaTalkMarkupTest, Warning, TEXT("%s %s %s"), kPrefix, bOk ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), *What);
		};
		// Свой персонаж: реестр мира, офлайн (без 4003) — павн контроллера (как `UAsdaFishingSubsystem::SelfChar`).
		auto Me = [WS, W]() -> AAsdaPlayerCharacter*
		{
			if (IsValid(WS->LocalPlayerActor)) { if (AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get())) { return Ch; } }
			APlayerController* PC = W->GetFirstPlayerController();
			if (AAsdaPlayerCharacter* Ch = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr) { return Ch; }
			for (TActorIterator<AAsdaPlayerCharacter> It(W); It; ++It) { if (*It && (*It)->IsPlayerControlled()) { return *It; } }
			return nullptr;
		};
		// Диагностика поиска своего персонажа (почему -1,-1): контроллер, павн, персонажи на уровне.
		auto MeDiag = [WS, W]() -> FString
		{
			APlayerController* PC = W->GetFirstPlayerController();
			APawn* Pn = PC ? PC->GetPawn() : nullptr;
			int32 N = 0; for (TActorIterator<AAsdaPlayerCharacter> It(W); It; ++It) { ++N; }
			return FString::Printf(TEXT("реестр=%s PC=%s павн=%s персонажей=%d"), IsValid(WS->LocalPlayerActor) ? *WS->LocalPlayerActor->GetClass()->GetName() : TEXT("нет"),
				PC ? *PC->GetClass()->GetName() : TEXT("нет"), Pn ? *Pn->GetClass()->GetName() : TEXT("нет"), N);
		};

		// Свой персонаж офлайн (без 4003 его нет — нет павна и сетки клеток): тот же путь, что у самотеста цепочки ESC.
		if (AsdaSelfTestKit::NeedSelfSpawn(W) && GEngine)
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			WS->DebugSpawnSelfAt(256, 256, TEXT("СамТест"));
		}

		// ── 1. Разбор составного тега `<FC=FFCC0055,FB>` (эталон `textParseColorTag`: подтеги внутри одних скобок) ──
		At(0.5f, [Check]()
		{
			AsdaCVarSave::SetForTest(TEXT("asda.Text.CompoundTags"), 1);
			const FString Raw = TEXT("Друг: <FC=FFCC0055,FB>'Друг'</F>.");
			const FString Rich = FAsdaMarkup::ToRichMarkup(Raw);
			Check(Rich.Contains(TEXT("c=\"CC0055\"")) && Rich.Contains(TEXT("b=\"1\"")) && !Rich.Contains(TEXT("FB")),
				FString::Printf(TEXT("rich: составной тег → цвет CC0055 + жирный: «%s»"), *Rich));
			Check(FAsdaMarkup::StripTags(Raw) == TEXT("Друг: 'Друг'."), FString::Printf(TEXT("срез: «%s»"), *FAsdaMarkup::StripTags(Raw)));
			TArray<FAsdaTextSegment> Segs;
			FAsdaMarkup::ToSegments(Raw, Segs);
			const FLinearColor Want = AsdaUi::SrgbHexToLinear(TEXT("CC0055"));
			Check(Segs.Num() == 3 && Segs[1].Text == TEXT("'Друг'") && Segs[1].Style == EAsdaTextStyle::Bold && Segs[1].Color.Equals(Want, 0.002f),
				FString::Printf(TEXT("сегменты: %d, второй «%s» жирный=%d цвет=%s"), Segs.Num(), Segs.IsValidIndex(1) ? *Segs[1].Text : TEXT(""),
					Segs.IsValidIndex(1) ? (int32)(Segs[1].Style == EAsdaTextStyle::Bold) : -1, Segs.IsValidIndex(1) ? *Segs[1].Color.ToString() : TEXT("")));
			// Одиночные теги не изменились.
			Check(FAsdaMarkup::ToRichMarkup(TEXT("<FC=FFFF2222> sowel </F>!")) == TEXT("<a c=\"FF2222\"> sowel </>!"),
				FString::Printf(TEXT("одиночный тег: «%s»"), *FAsdaMarkup::ToRichMarkup(TEXT("<FC=FFFF2222> sowel </F>!"))));
			// Ключ 0 — прежний разбор (документирует дефект: «FFCC0055,FB» → правые 6 hex «0055FB», без жирного).
			AsdaCVarSave::SetForTest(TEXT("asda.Text.CompoundTags"), 0);
			const FString Old = FAsdaMarkup::ToRichMarkup(Raw);
			Check(Old.Contains(TEXT("c=\"0055FB\"")) && !Old.Contains(TEXT("b=\"1\"")), FString::Printf(TEXT("ключ 0 — прежний разбор: «%s»"), *Old));
			AsdaCVarSave::SetForTest(TEXT("asda.Text.CompoundTags"), 1);
		});

		// ── 2. Реплика Lua `lua_50`: вход как кликом мыши, страница 50 ──
		At(1.0f, [W, Dlg, Check]()
		{
			AsdaCVarSave::SetForTest(TEXT("asda.NpcDlg.TalkMarkup"), 1);
			AAsdaNpcActor* Actor = nullptr;
			for (TActorIterator<AAsdaNpcActor> It(W); It; ++It) { if (*It && (*It)->NpcId == kNpcLua) { Actor = *It; break; } }
			bool bOpen = false;
			if (Actor)
			{
				if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W->GetFirstPlayerController())) { PC->SetTarget(Actor); }
				bOpen = Dlg->OpenForNpc(Actor);
			}
			else
			{
				bOpen = Dlg->OpenForNpcId(kNpcLua);
			}
			Check(bOpen && Dlg->IsDialogOpen(), FString::Printf(TEXT("окно разговора с Lua открыто (актор %s)"), Actor ? TEXT("найден") : TEXT("нет — вход по id")));
			Dlg->RenderPage(kTalkLua50);
		});
		At(1.6f, [W, Dlg, Check]()
		{
			const FString D = Dlg->DumpWindow();
			const bool bRich = D.Contains(TEXT("[rich] «")) && D.Contains(TEXT("c=\"FF2222\""));
			Check(bRich && !D.Contains(TEXT("FC=")) && !D.Contains(TEXT("</F>")),
				FString::Printf(TEXT("lua_50: строка с тегом — rich, цвет FF2222, «FC=»/«</F>» не видно (rich=%d)"), bRich ? 1 : 0));
			GEngine->Exec(W, TEXT("asda.UI.Shot asda_talk_markup_lua50"));
		});
		// Ключ 0 — прежний вывод (тег буквами) — подтверждает, что проверка ловит дефект.
		At(2.2f, [Dlg, Check]()
		{
			AsdaCVarSave::SetForTest(TEXT("asda.NpcDlg.TalkMarkup"), 0);
			Dlg->RenderPage(kTalkLua50);
			const FString D = Dlg->DumpWindow();
			Check(D.Contains(TEXT("<FC=FFFF2222>")) && !D.Contains(TEXT("[rich] «")), TEXT("ключ asda.NpcDlg.TalkMarkup 0: тег снова виден буквами (прежний порт)"));
			AsdaCVarSave::SetForTest(TEXT("asda.NpcDlg.TalkMarkup"), 1);
			Dlg->Close();
		});

		// ── 3. Приписка 111929 у NPC 50 (`sprintf("%s\n %s", текст, нотис)`, нотис с `<FC=FFFF0000>`) ──
		At(2.8f, [Dlg, Check]()
		{
			const bool bOpen = Dlg->OpenForNpcId(kNpcNotice);
			const FString D = Dlg->DumpWindow();
			Check(bOpen && D.Contains(TEXT("c=\"FF0000\"")) && !D.Contains(TEXT("FC=")),
				FString::Printf(TEXT("NPC %d: приписка 111929 — rich, красная, тег не виден (окно %s)"), kNpcNotice, bOpen ? TEXT("открыто") : TEXT("НЕ открыто")));
			Dlg->Close();
		});

		// ── 4. Молл в ГОРОДСКОЙ клетке: предупреждения 97100 нет ──
		At(5.9f, [WM, Mall, Me, MeDiag, Stat, Check]()
		{
			AsdaCVarSave::SetForTest(TEXT("asda.PremiumMall.OpenWarning"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.PremiumMall.WarnOutsideTownOnly"), 1);
			AAsdaPlayerCharacter* P = Me();
			const FIntPoint T0 = P ? AsdaCoord::GameToTile(P->GetGamePosXZ()) : FIntPoint(-1, -1);
			Stat->Town  = FindTile(AsdaGround::GetGrid(), T0, true);
			Stat->Field = FindTile(AsdaGround::GetGrid(), T0, false);
			Check(P && Stat->Town.X >= 0 && Stat->Field.X >= 0,
				FString::Printf(TEXT("клетки найдены: игрок %d,%d, город %d,%d, поле %d,%d (%s, сетка %s)"), T0.X, T0.Y, Stat->Town.X, Stat->Town.Y,
					Stat->Field.X, Stat->Field.Y, *MeDiag(), *GridDiag(AsdaGround::GetGrid())));
			if (P && Stat->Town.X >= 0) { P->NetSnap(AsdaCoord::TileCenterGame(Stat->Town)); }
			Mall->CloseMall();   // защёлка молла снята при любом состоянии адресата
			WM->ClearMsgBoxQueue(TEXT("самотест разметки/молла: город"));
		});
		At(6.4f, [GI = Zone->GetGameInstance(), WM, Mall, Zone, Check]()
		{
			Check(Zone->IsLocalPlayerInPeaceZoneNow(), TEXT("город: признак зоны клетки игрока = город (бит 0x80)"));
			Mall->CloseMall();   // защёлка молла снята при любом состоянии адресата
			Mall->Toggle();   // хоткей 39 (`OnGameCommand`) → `Toggle`
			const FString T = OneButText(WM);
			Check(T != AsdaNotice::Loc(GI, kLocWarnMob),
				FString::Printf(TEXT("город: молл открыт (%d), плашки 97100 нет (окно-сообщение: «%s»)"), Mall->IsOpen() ? 1 : 0, *T));
			GEngine->Exec(Zone->GetWorld(), TEXT("asda.UI.Shot asda_mall_town_nowarn"));
		});
		// Ключ 0 — прежний порт: в городе плашка есть.
		At(7.0f, [GI = Zone->GetGameInstance(), WM, Mall, Check]()
		{
			Mall->CloseMall();
			WM->ClearMsgBoxQueue(TEXT("самотест разметки/молла: город, ключ 0"));
			AsdaCVarSave::SetForTest(TEXT("asda.PremiumMall.WarnOutsideTownOnly"), 0);
			Mall->OpenMall(0);
			Check(OneButText(WM) == AsdaNotice::Loc(GI, kLocWarnMob), TEXT("ключ WarnOutsideTownOnly 0: в городе плашка 97100 снова есть (прежний порт)"));
			Mall->CloseMall();
			WM->ClearMsgBoxQueue(TEXT("самотест разметки/молла: город, ключ 0 снят"));
			AsdaCVarSave::SetForTest(TEXT("asda.PremiumMall.WarnOutsideTownOnly"), 1);
		});

		// ── 5. Молл в ПОЛЕВОЙ клетке: предупреждение 97100 есть (как оригинал) ──
		At(7.5f, [Me, Stat]()
		{
			if (AAsdaPlayerCharacter* P = Me(); P && Stat->Field.X >= 0) { P->NetSnap(AsdaCoord::TileCenterGame(Stat->Field)); }
		});
		At(8.0f, [GI = Zone->GetGameInstance(), WM, Mall, Zone, Check]()
		{
			Check(!Zone->IsLocalPlayerInPeaceZoneNow(), TEXT("поле: признак зоны клетки игрока = не город"));
			Mall->CloseMall();   // защёлка молла снята при любом состоянии адресата
			Mall->Toggle();   // хоткей 39 → `Toggle`
			const FString T = OneButText(WM);
			Check(T == AsdaNotice::Loc(GI, kLocWarnMob), FString::Printf(TEXT("поле: молл открыт (%d), плашка 97100 есть: «%s»"), Mall->IsOpen() ? 1 : 0, *T));
			GEngine->Exec(Zone->GetWorld(), TEXT("asda.UI.Shot asda_mall_field_warn"));
		});
		At(8.6f, [W, WM, Mall, Stat, bQuit]()
		{
			Mall->CloseMall();
			WM->ClearMsgBoxQueue(TEXT("самотест разметки/молла: конец"));
			AsdaSelfTestKit::Finish(W, kCmd, bQuit, LogAsdaTalkMarkupTest, kPrefix, Stat->Ok, Stat->Fail, TEXT(""), true);
		});
	}));
#endif // ASDA_DEV_CONSOLE

#endif // !UE_BUILD_SHIPPING
