// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ПЕРЕХОД В ШАХТУ И ОБРАТНО» (09.11.2026, пачка 20, задача владельца «make possible to travel to Mine»; серверная часть —
//   P120, AstoryServer/Docs/GameServer/mine-travel-p120.md; отчёт Reports/batch20/mine/REPORT.md): `asda.Mine.TravelSelfTest [noshots] [quit]`.
//   Путь игрока (закон diagnostic-must-use-real-path), офлайн (-AsdaNoNet):
//     1. Альпия (карта 3) — свой персонаж у NPC 181 «Шахта» (118,310): окно разговора `OpenForNpcId(181)` (тот же код, что щелчок мышью),
//        кнопка меню 0 видна, код 30077, подпись из таблицы по языку; нажатие `OnDialogButton(581)` (кнопка NPC_SHOP_BUT00, как мышь) под
//        перехватом исходящих → РОВНО ОДИН 6746, тело 4 б = свой charId (эталон game_main.c:10304 → antiCheat_SendHeartbeat6746).
//     2. Ответ сервера P120 — 5055 код 1 (53 б, IP/порт Total_PortalConfig[23], карта 23, прибытие (261,41), +51/+52 = 0xFF как sub_40DE70)
//        через ШТАТНЫЙ приёмник `DebugFeedPacket` → переход взведён: EnterMapId 23, тайл прибытия, HUD кампвара подавлен (эталон: карта 23).
//        Офлайн реконнекта нет — уровень грузится штатным `BeginMapLoad(23)` (тот же вызов, что после 5058), признаки перехода снимаются
//        `ClearPortalTransferState` (как после входа на новом соединении).
//     3. Шахта: уровень /Game/Map/mine загружен, номер карты 23, мир виден (доля чёрных < 25 %), кадр с HUD и мини-картой.
//     4. Смерть в шахте: 4053 → меню смерти — ТОЛЬКО опция 8 (эталон Recv_CharDeath_Apply: карта 23 → 8), выбор → 4054 режим 8 (его P120
//        G 0x52A755 на сервере превращает в воскрешение в городе); воскрешение 4056 → меню закрыто.
//     5. Страж шахты 202: окно, кнопка 0 видна, код 30078; нажатие → РОВНО ОДИН 6749, тело 6 б = charId + армия (эталон 30078 →
//        netSendReqSelfId_op6749). Ответ 5055 код 1 на карту 3, прибытие (115,320) — первая пара MapInfo Альпии у NPC 181.
//     6. Альпия снова: уровень загружен, мир виден, кадр.
//   Кадры — Reports/batch20/mine/shots/<ru|en>_<этап>.png. Итог: «ШАХТА ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "UI/AsdaAfterDeathSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaWorldKind.h"
#include "World/AsdaWorldSubsystem.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Widgets/SWindow.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMineTravelTest, Log, All);

namespace AsdaMineTravelTest
{
	static const TCHAR* kCmd = TEXT("asda.Mine.TravelSelfTest");   // имя команды
	static constexpr uint16 kMyUserNum = 321;                        // свой userNum на прогон
	static constexpr int32 kTestCharId = 75001;                      // свой charId, если вход не дал своего
	static constexpr int32 kAlpen = 3, kMine = 23;                   // карты
	static const FIntPoint kAtNpc181(119, 312);                      // у NPC 181 (118,310) — в пределах 3 клеток P120
	static const FIntPoint kMineArrival(261, 41);                    // STARTPOS Total_PortalConfig[23] (P120), 3 клетки от стража 202 (261,38)
	static const FIntPoint kAlpenReturn(115, 320);                   // первая пара MapInfo Альпии

	// Перехваченная отправка.
	struct FSent { int32 Op = 0; TArray<uint8> Body; };

	// Контекст прогона (переживает смену мира).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                   // счёт
		bool bQuit = false, bShots = true;        // выход / кадры
		int32 Stage = 0;                          // этап
		bool bDone = false;                       // итог напечатан — тикер снимается
		double T0 = 0.0;                          // начало этапа
		int32 WantMap = kAlpen;                   // карта, которую ждём
		FIntPoint Spawn = kAtNpc181;              // тайл спавна на ней
		TWeakObjectPtr<UGameInstance> GI;         // игра
		TUniquePtr<FAsdaNetSelfScope> NetScope;   // свой номер/charId — вернуть в конце
		FString GsIpWas; int32 GsPortWas = 0;     // адрес игрового сервера до прогона (5055 его меняет)
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaMineTravelTest, Log, TEXT("ШАХТА ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaMineTravelTest, Error, TEXT("ШАХТА FAIL %s"), *What); }
		}
	};
	using FCtxRef = TSharedRef<FCtx>;

	template <class T> static T* Sub(const FCtxRef& C) { UGameInstance* GI = C->GI.Get(); return GI ? GI->GetSubsystem<T>() : nullptr; }

	// Кадр окна с интерфейсом: доля почти чёрных пикселей средней полосы (мир) + PNG.
	static float Grab(const FCtxRef& C, const FString& Tag)
	{
		UGameViewportClient* GVC = GEngine ? GEngine->GameViewport : nullptr;
		TSharedPtr<SWindow> Win = GVC ? GVC->GetWindow() : nullptr;
		if (!Win.IsValid() || !FSlateApplication::IsInitialized()) { return -1.f; }
		TArray<FColor> Px; FIntVector Sz(0, 0, 0);
		if (!FSlateApplication::Get().TakeScreenshot(Win.ToSharedRef(), Px, Sz) || Px.Num() != Sz.X * Sz.Y || Sz.X <= 0) { return -1.f; }
		int64 N = 0, Black = 0;
		for (int32 Y = Sz.Y * 25 / 100; Y < Sz.Y * 60 / 100; ++Y)
		{
			for (int32 X = Sz.X * 30 / 100; X < Sz.X * 70 / 100; ++X)
			{
				const FColor& P = Px[Y * Sz.X + X];
				const float L = 0.2126f * P.R + 0.7152f * P.G + 0.0722f * P.B;
				++N; if (L < 12.f) { ++Black; }
			}
		}
		if (C->bShots)
		{
			for (FColor& P : Px) { P.A = 255; }
			IImageWrapperModule& IW = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
			TSharedPtr<IImageWrapper> Png = IW.CreateImageWrapper(EImageFormat::PNG);
			if (Png.IsValid() && Png->SetRaw(Px.GetData(), Px.Num() * sizeof(FColor), Sz.X, Sz.Y, ERGBFormat::BGRA, 8))
			{
				const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/batch20/mine/shots")));
				IFileManager::Get().MakeDirectory(*Dir, true);
				FFileHelper::SaveArrayToFile(Png->GetCompressed(), *FPaths::Combine(Dir, FString::Printf(TEXT("%s_%s.png"), FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en"), *Tag)));
			}
		}
		const float F = N ? (float)Black / (float)N : -1.f;
		UE_LOG(LogAsdaMineTravelTest, Log, TEXT("ШАХТА кадр %s: чёрных %.1f%%"), *Tag, F * 100.f);
		return F;
	}

	// Строка дампа окна 570 по тегу.
	static FString DumpLine(const FString& Dump, const FString& Tag)
	{
		TArray<FString> Lines;
		Dump.ParseIntoArrayLines(Lines);
		for (const FString& L : Lines) { if (L.TrimStart().StartsWith(Tag)) { return L.TrimStart(); } }
		return FString();
	}
	// Подпись кнопки из строки дампа «… подпись=«…» …».
	static FString Caption(const FString& Line)
	{
		const int32 A = Line.Find(TEXT("подпись=«"));
		if (A == INDEX_NONE) { return FString(); }
		const FString R = Line.Mid(A + 9);
		int32 B = INDEX_NONE;
		return R.FindChar(TEXT('»'), B) ? R.Left(B) : FString();
	}
	static bool HasCyrillic(const FString& S) { for (TCHAR Ch : S) { if (Ch >= 0x0400 && Ch <= 0x04FF) { return true; } } return false; }

	// Действие под перехватом исходящих (в сеть не уходит ничего).
	static TArray<FSent> Capture(const FCtxRef& C, TFunctionRef<void()> Act, bool& bOk)
	{
		TArray<FSent> Sent;
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
		const uint32 Gen = Net ? AsdaSelfTestKit::ArmCapture(Net, [&Sent](uint16 Op, const uint8* B, int32 N, bool, TCHAR)
		{
			FSent X; X.Op = Op; if (B && N > 0) { X.Body.Append(B, N); } Sent.Add(MoveTemp(X));
		}, 0, kCmd) : 0;
		bOk = Gen != 0;
		Act();
		if (bOk) { AsdaSelfTestKit::DisarmCapture(Net, Gen); }
		return Sent;
	}
	static int32 CountOp(const TArray<FSent>& S, int32 Op, const FSent** Last = nullptr)
	{
		int32 N = 0;
		for (const FSent& X : S) { if (X.Op == Op) { ++N; if (Last) { *Last = &X; } } }
		return N;
	}

	// 5055 код 1 — как строит сервер P120 (буфер sub_40DE70: +31/+33 = -1, +35…+50 = -1, +51/+52 = 0xFF).
	static TArray<uint8> Body5055(int32 Map, FIntPoint T)
	{
		TArray<uint8> B; B.SetNumZeroed(53);
		B[0] = 1;
		const char* Ip = "@@ASTORY_SERVER_WAN_IP@@";
		for (int32 i = 0; Ip[i]; ++i) { B[1 + i] = (uint8)Ip[i]; }
		AsdaBytes::WrI16(&B[21], (int16)15601);
		AsdaBytes::WrI16(&B[23], (int16)Map);
		AsdaBytes::WrI16(&B[25], (int16)T.X);
		AsdaBytes::WrI16(&B[27], (int16)T.Y);
		AsdaBytes::WrI16(&B[31], -1); AsdaBytes::WrI16(&B[33], -1);
		for (int32 i = 35; i < 51; ++i) { B[i] = 0xFF; }
		B[51] = 0xFF; B[52] = 0xFF;
		return B;
	}

	// Окно NPC → кнопка 0 → ровно один опкод WantOp; возврат — тело.
	static TArray<uint8> PressNpc(const FCtxRef& C, int32 NpcId, int32 WantCode, int32 WantOp, const TCHAR* RuCaption)
	{
		TArray<uint8> Out;
		UAsdaNpcDialogSubsystem* Dlg = Sub<UAsdaNpcDialogSubsystem>(C);
		if (!Dlg) { C->Expect(false, TEXT("нет подсистемы диалога NPC")); return Out; }
		C->Expect(Dlg->OpenForNpcId(NpcId), FString::Printf(TEXT("окно разговора с NPC %d открылось"), NpcId));
		const FString D = Dlg->DumpWindow();
		const FString B0 = DumpLine(D, TEXT("NPC_SHOP_BUT00"));
		const FString Cap = Caption(B0);
		C->Expect(B0.Contains(TEXT("видим=1")) && B0.Contains(FString::Printf(TEXT("код=%d"), WantCode)),
			FString::Printf(TEXT("NPC %d: кнопка 0 видна, код %d: %s"), NpcId, WantCode, *B0));
		C->Expect(FAsdaLang::IsRu() ? Cap == RuCaption : (!Cap.IsEmpty() && !HasCyrillic(Cap)),
			FString::Printf(TEXT("NPC %d: подпись кнопки «%s» (%s)"), NpcId, *Cap, FAsdaLang::IsRu() ? *FString::Printf(TEXT("ждём «%s»"), RuCaption) : TEXT("EN: латиница")));
		bool bCap = false;
		const TArray<FSent> Sent = Capture(C, [&]() { Dlg->OnDialogButton(581); }, bCap);
		const FSent* Last = nullptr;
		const int32 N = CountOp(Sent, WantOp, &Last);
		C->Expect(bCap && N == 1 && Sent.Num() == 1, FString::Printf(TEXT("NPC %d, кнопка 0 (581): ушёл ровно один %d (всего отправок %d)"), NpcId, WantOp, Sent.Num()));
		if (Last) { Out = Last->Body; }
		if (Dlg->IsDialogOpen()) { Dlg->Close(); }
		return Out;
	}

	// Подать 5055 и снять признаки перехода (офлайн реконнекта нет — дальше BeginMapLoad, как после входа на новом соединении).
	static void Feed5055AndLoad(const FCtxRef& C, int32 Map, FIntPoint T)
	{
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
		UAsdaLoadManagerSubsystem* LM = Sub<UAsdaLoadManagerSubsystem>(C);
		if (!Net || !LM) { return; }
		Net->DebugFeedPacket(5055, Body5055(Map, T));
		C->Expect(Net->EnterMapId == Map && Net->bPortalPending && Net->PortalTileX == T.X && Net->PortalTileZ == T.Y,
			FString::Printf(TEXT("5055 код 1 → переход взведён: карта %d (ждём %d), 5058 ожидается=%d, тайл прибытия (%d,%d)"),
				Net->EnterMapId, Map, Net->bPortalPending ? 1 : 0, Net->PortalTileX, Net->PortalTileZ));
		// Ожидание по эталону network.c:43118: подавлен, если бит 0x80 байта +51 ИЛИ карта 23. P120, как сток (RecvContinent_PortalMove,
		//   буфер sub_40DE70), кладёт в +51 0xFF — значит подавлен в обе стороны (как после кристалла у стока).
		C->Expect(Net->bCampWarHudSuppressed == (((0xFF & 0x80) != 0) || Map == kMine),
			FString::Printf(TEXT("HUD кампвара %s (эталон network.c:43118: бит 0x80 +51 ИЛИ карта 23)"), Net->bCampWarHudSuppressed ? TEXT("подавлен") : TEXT("разрешён")));
		Net->ClearPortalTransferState(TEXT("самотест шахты: офлайн, вход на новом соединении"));
		Net->bTransferInProgress = false;
		Net->bPendingEnterWorld = false;
		Net->EnterMapId = Map;
		C->WantMap = Map; C->Spawn = T;
		LM->BeginMapLoad(Map);
	}

	static void Finish(const FCtxRef& C)
	{
		if (C->bDone) { return; }
		C->bDone = true;
		if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C)) { Net->GameServerIP = C->GsIpWas; Net->GameServerPort = C->GsPortWas; }
		C->NetScope.Reset();
		UGameInstance* GI = C->GI.Get();
		AsdaSelfTestKit::Finish(GI ? GI->GetWorld() : nullptr, kCmd, C->bQuit, LogAsdaMineTravelTest, TEXT("ШАХТА"), C->Ok, C->Fail);
	}

	// Ждать загрузки C->WantMap и поставить своего персонажа; true — готово (этап двигается дальше).
	static bool WaitMap(const FCtxRef& C, double Dt)
	{
		UGameInstance* GI = C->GI.Get();
		UWorld* W = GI ? GI->GetWorld() : nullptr;
		UAsdaLoadManagerSubsystem* LM = Sub<UAsdaLoadManagerSubsystem>(C);
		if (W && !AsdaWorldKind::IsLoadingMap(W) && W->HasBegunPlay() && Dt > 1.0 && LM && !LM->IsLoadingScreenVisible())
		{
			LM->CurrentMapId = C->WantMap;
			const FString Pkg = W->GetOutermost() ? W->GetOutermost()->GetName() : FString();
			const FString Dir = LM->GetMapDirName(C->WantMap);
			C->Expect(Pkg.Contains(Dir), FString::Printf(TEXT("карта %d загружена: уровень %s (папка %s)"), C->WantMap, *Pkg, *Dir));
			if (UAsdaWorldSubsystem* WS = Sub<UAsdaWorldSubsystem>(C)) { WS->DebugSpawnSelfAt(C->Spawn.X, C->Spawn.Y); }
			return true;
		}
		if (Dt > 120.0) { C->Expect(false, FString::Printf(TEXT("карта %d не загрузилась за 120 с"), C->WantMap)); Finish(C); }
		return false;
	}

	static bool Step(const FCtxRef& C)
	{
		UGameInstance* GI = C->GI.Get();
		if (!GI || C->bDone) { return false; }
		UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>();
		UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>();
		const double Now = FPlatformTime::Seconds();
		const double Dt = Now - C->T0;
		auto Next = [&](int32 S) { C->Stage = S; C->T0 = Now; };
		switch (C->Stage)
		{
		case 0:   // Альпия: BeginMapLoad, если сейчас не она
			C->WantMap = kAlpen; C->Spawn = kAtNpc181;
			if (LM->CurrentMapId != kAlpen) { LM->BeginMapLoad(kAlpen); }
			Next(1); return true;
		case 1: if (WaitMap(C, Dt)) { Net->EnterMapId = kAlpen; Next(2); } return !C->bDone;
		case 2:   // 4 с на осадку → кадр → NPC 181 → 6746 → 5055 → загрузка шахты
		{
			if (Dt < 4.0) { return true; }
			const float B = Grab(C, TEXT("1_alpen_npc181"));
			C->Expect(B >= 0.f && B < 0.25f, FString::Printf(TEXT("Альпия у NPC 181: мир виден (чёрных %.1f%%)"), B * 100.f));
			const TArray<uint8> Body = PressNpc(C, 181, 30077, 6746, TEXT("Шахта"));
			C->Expect(Body.Num() == 4 && AsdaBytes::RdI32(Body.GetData()) == Net->SelectedCharId,
				FString::Printf(TEXT("6746: тело %d б, charId %d (свой %d) — его P120 сверяет с UserIndex сессии"), Body.Num(),
					Body.Num() >= 4 ? AsdaBytes::RdI32(Body.GetData()) : -1, Net->SelectedCharId));
			Feed5055AndLoad(C, kMine, kMineArrival);
			Next(3); return true;
		}
		case 3: if (WaitMap(C, Dt)) { Next(4); } return !C->bDone;
		case 4:   // шахта: кадр, смерть → опция 8
		{
			if (Dt < 4.0) { return true; }
			C->Expect(LM->CurrentMapId == kMine && Net->EnterMapId == kMine, FString::Printf(TEXT("в шахте: карта %d / %d"), LM->CurrentMapId, Net->EnterMapId));
			const float B = Grab(C, TEXT("2_mine_arrival"));
			C->Expect(B >= 0.f && B < 0.25f, FString::Printf(TEXT("шахта у точки прибытия (261,41): мир виден (чёрных %.1f%%)"), B * 100.f));
			TArray<uint8> D; D.SetNumZeroed(4); AsdaBytes::WrI16(&D[0], (int16)kMyUserNum); AsdaBytes::WrI16(&D[2], -1);
			Net->DebugFeedPacket(4053, D);
			Next(5); return true;
		}
		case 5:
		{
			if (Dt < 0.5) { return true; }
			UAsdaAfterDeathSubsystem* Ad = GI->GetSubsystem<UAsdaAfterDeathSubsystem>();
			C->Expect(Ad && Ad->IsDeathMenuOpen(), TEXT("смерть в шахте: меню смерти открыто по 4053"));
			C->Expect(Ad && Ad->DebugOptionCount() == 1 && Ad->DebugOptionType(0) == 8,
				FString::Printf(TEXT("меню смерти на карте 23: опций %d, первая %d (эталон: только 8 «база»)"), Ad ? Ad->DebugOptionCount() : -1, Ad ? Ad->DebugOptionType(0) : -1));
			Grab(C, TEXT("3_mine_death_menu"));
			bool bCap = false;
			const TArray<FSent> S = Capture(C, [&]() { if (Ad) { Ad->SelectOptionAt(0); } }, bCap);
			const FSent* L = nullptr;
			const int32 N = CountOp(S, 4054, &L);
			const int32 Mode = (L && L->Body.Num() > 0) ? (int32)L->Body.Last() : -1;
			C->Expect(bCap && N == 1 && Mode == 8, FString::Printf(TEXT("опция 8 → 4054 режим %d (ждём 8; сервер P120 G: на карте типа 6 — воскрешение в городе)"), Mode));
			TArray<uint8> R; R.SetNumZeroed(22);
			AsdaBytes::WrI16(&R[0], (int16)kMyUserNum); AsdaBytes::WrI16(&R[2], (int16)kMineArrival.X); AsdaBytes::WrI16(&R[4], (int16)kMineArrival.Y);
			AsdaBytes::WrI32(&R[6], 500); AsdaBytes::WrI16(&R[10], 100);
			Net->DebugFeedPacket(4056, R);
			Next(6); return true;
		}
		case 6:   // страж 202 → 6749 → 5055 → Альпия
		{
			if (Dt < 1.0) { return true; }
			UAsdaAfterDeathSubsystem* Ad = GI->GetSubsystem<UAsdaAfterDeathSubsystem>();
			C->Expect(Ad && !Ad->IsDeathMenuOpen(), TEXT("4056 → меню смерти закрыто"));
			const TArray<uint8> Body = PressNpc(C, 202, 30078, 6749, TEXT("В Альпию"));
			C->Expect(Body.Num() == 6 && AsdaBytes::RdI32(Body.GetData()) == Net->SelectedCharId,
				FString::Printf(TEXT("6749: тело %d б, charId %d (свой %d), армия %d"), Body.Num(),
					Body.Num() >= 4 ? AsdaBytes::RdI32(Body.GetData()) : -1, Net->SelectedCharId, Body.Num() >= 6 ? (int32)AsdaBytes::RdI16(Body.GetData() + 4) : -1));
			Feed5055AndLoad(C, kAlpen, kAlpenReturn);
			Next(7); return true;
		}
		case 7: if (WaitMap(C, Dt)) { Next(8); } return !C->bDone;
		case 8:
		{
			if (Dt < 4.0) { return true; }
			C->Expect(LM->CurrentMapId == kAlpen && Net->EnterMapId == kAlpen, FString::Printf(TEXT("снова Альпия: карта %d / %d"), LM->CurrentMapId, Net->EnterMapId));
			const float B = Grab(C, TEXT("4_alpen_return"));
			C->Expect(B >= 0.f && B < 0.25f, FString::Printf(TEXT("Альпия у точки возврата (115,320): мир виден (чёрных %.1f%%)"), B * 100.f));
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
		if (!Net || !GI->GetSubsystem<UAsdaWorldSubsystem>() || !GI->GetSubsystem<UAsdaLoadManagerSubsystem>() || !GI->GetSubsystem<UAsdaNpcDialogSubsystem>())
		{
			AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем")); return;
		}
		FCtxRef C = MakeShared<FCtx>();
		C->GI = GI; C->bQuit = KA.bQuit; C->bShots = !KA.Has(TEXT("noshots"));
		C->NetScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		C->GsIpWas = Net->GameServerIP; C->GsPortWas = Net->GameServerPort;
		Net->SetSelfUserNumForDebug(kMyUserNum);
		if (Net->SelectedCharId <= 0) { Net->SelectedCharId = kTestCharId; }
		if (UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>(); LM && LM->CurrentMapId < 0) { LM->CurrentMapId = kAlpen; }
		C->T0 = FPlatformTime::Seconds();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([C](float) { return Step(C); }), 0.f);
	}
}

// Консольная команда самотеста.
static FAutoConsoleCommandWithWorldAndArgs GAsdaMineTravelSelfTest(
	TEXT("asda.Mine.TravelSelfTest"),
	TEXT("Самотест перехода в шахту (карта 23) и обратно (09.11, P120): NPC 181 «Шахта» в Альпии → 6746 → 5055 → уровень шахты → смерть "
	     "(опция 8 → 4054 режим 8) → страж 202 «В Альпию» → 6749 → 5055 → Альпия; кадры в Reports/batch20/mine/shots. Офлайн. [noshots] `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W) { AsdaMineTravelTest::Run(A, W); }));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
