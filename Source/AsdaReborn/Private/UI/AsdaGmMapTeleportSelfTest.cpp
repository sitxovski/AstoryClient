// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ GM-ТЕЛЕПОРТА CTRL+ЛКМ ПО КАРТАМ (09.11.2026, пачка 20): `asda.SelfTest.GmMapTeleport name=<имя> [shotdir=<папка>] [quit]`.
//   Офлайн (`-AsdaNoNet`), карта мира Alpen (3). Разбор — шапка `UAsdaGmOriginalSubsystem::TryMapClickTeleport`,
//   отчёт `Reports/batch20/gm_teleport/README.md`.
//   Путь игрока: Ctrl — настоящий ввод контроллера (`InputKey`, тот же `IsInputKeyDown`, что читает клик), клик — общая точка
//   `UAsdaMinimapSubsystem::HandleMapClick` (её зовут кнопки поля мини-карты 1714 и карты мира 1729) с пикселем, пересчитанным
//   из клетки той же арифметикой; карта мира открыта штатно, регион выбран теми же вызовами, что строки списков 1721/1723.
//   Отправки — под перехватом (`AsdaSelfTestKit::ArmCapture`), ответ сервера 6037 — `DebugFeedPacket` (тот же разбор входящих).
//   Имя на GM_ (без учёта регистра):
//     A1 без Ctrl — обычный клик, 6010/6036 нет;  A2 Ctrl, права не подтверждены → проба 6036, ответ 6037 → 6010 с клеткой;
//     A3 Ctrl по мини-карте → 6010 сразу, хода нет;  A4 карта мира, своя карта → 6010 с клеткой региона;
//     A5 карта мира, чужая карта → 6010 формы P123 (тело 8 б: X, Z, карта, метка A123; без паддинга), строка 991303, автопути нет;
//     A5b ключ `asda.Gm.MapTeleportOtherMap 0` → ничего, строка 991300;  A6 клетка вне карты → ничего, строка 991302;
//     A7 ключ `asda.Gm.MapClickTeleport 0` → клик обычный, 6010 нет.
//   Имя без GM_: B1 Ctrl по мини-карте (даже с правами от сервера) → 6010/6036 нет, клик обычный (ход);
//     B2 Ctrl по карте мира своей и чужой карты → 6010/6036 нет, чужая — обычная заявка автопути (как у оригинала).
//   Строки: текст loc 991300/991302 из таблицы текущего языка (не «[#id]»). Кадры: `<shotdir>/<lang>_<gm|player>_<шаг>.png`.
//   Итог: «GM-ТЕЛЕПОРТ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaMinimapSubsystem.h"
#include "World/AsdaGmOriginalSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaWalkGrid.h"
#include "Npc/AsdaNpcGmOnly.h"
#include "Login/AsdaText.h"
#include "Text/AsdaLang.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"   // FScreenshotRequest — кадры

DEFINE_LOG_CATEGORY_STATIC(LogAsdaGmMapTpTest, Log, All);

// Друг `UAsdaMinimapSubsystem`: пересчёт клетки в пиксель поля и сброс троттла клика.
struct FAsdaGmMapTeleportSelfTest
{
	// Клик по мини-карте в клетку (обратная арифметика `HandleMapClick`, ветка мини-карты). false — нет сетки/павна.
	static bool ClickMinimap(UAsdaMinimapSubsystem* MM, const FIntPoint& Tile)
	{
		if (!MM) { return false; }
		AAsdaPlayerCharacter* P = nullptr;
		const UAsdaWalkGrid* G = MM->GetGrid(P);
		if (!P || !G) { return false; }
		const FVector2D Pos = P->GetGamePosXZ();
		const float Scale = MM->WorldToTexelScale(G);
		const FVector2D Scroll = MM->ScrollTexels(FVector2D(Pos.X * Scale, Pos.Y * Scale));
		const FVector2D TileC((double)Tile.X + 0.5, (double)Tile.Y + 0.5);
		const FVector2D LocalPx = (TileC * (Scale * 100.f) - Scroll) * MM->MarkPxPerTexel();
		MM->LastClickTime = 0.0;
		MM->HandleMapClick(LocalPx, /*bBig=*/false);
		return true;
	}
	// Клик по карте мира в клетку показанного региона (обратная арифметика ветки карты мира, с городским кропом).
	static void ClickBigMap(UAsdaMinimapSubsystem* MM, const FVector2D& Tile)
	{
		FVector2D T = Tile;
		FVector2D O; float S;
		if (MM->bDisplayTown && MM->TownCrop(MM->DisplayMapId, O, S)) { T = (T - O) * S; }
		MM->LastClickTime = 0.0;
		MM->HandleMapClick(T * MM->BigMapTileToPx(), /*bBig=*/true);
	}
	// Клик по карте мира в пиксель поля без городского кропа (клетка 0 — край карты).
	static void ClickBigMapRawNoCrop(UAsdaMinimapSubsystem* MM, const FVector2D& Px)
	{
		const bool bTownWas = MM->bDisplayTown;
		MM->bDisplayTown = false;
		MM->LastClickTime = 0.0;
		MM->HandleMapClick(Px, /*bBig=*/true);
		MM->bDisplayTown = bTownWas;
	}
	// Метка цели хода (клик «идти» её ставит) — признак обычного клика.
	static bool DestActive(const UAsdaMinimapSubsystem* MM) { return MM && MM->bDestActive; }
	static void ClearDest(UAsdaMinimapSubsystem* MM) { if (MM) { MM->bDestActive = false; } }
	static int32 OwnMapId(const UAsdaMinimapSubsystem* MM) { return MM ? MM->MapId : -1; }
	// Ближайшая к Want проходимая клетка своей карты (обычный клик «идти» ставит цель только на проходимую).
	static FIntPoint NearWalkable(const UAsdaMinimapSubsystem* MM, const FIntPoint& Want)
	{
		AAsdaPlayerCharacter* P = nullptr;
		const UAsdaWalkGrid* G = MM ? MM->GetGrid(P) : nullptr;
		if (!G) { return Want; }
		for (int32 r = 0; r < 40; ++r)
		{
			for (int32 dz = -r; dz <= r; ++dz)
			{
				for (int32 dx = -r; dx <= r; ++dx)
				{
					if (FMath::Max(FMath::Abs(dx), FMath::Abs(dz)) != r) { continue; }
					if (G->IsTileWalkable(Want.X + dx, Want.Y + dz)) { return FIntPoint(Want.X + dx, Want.Y + dz); }
				}
			}
		}
		return Want;
	}
};

namespace AsdaGmMapTpTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.GmMapTeleport");

	struct FSent { uint16 Op = 0; TArray<uint8> Body; bool bPad = true; };   // bPad — кадр добивается паддингом

	struct FRun
	{
		TWeakObjectPtr<UWorld> World;
		bool bQuit = false;
		bool bGm = false;                                  // имя на GM_
		FString Name;                                      // имя своего персонажа
		FString ShotDir;                                   // папка кадров
		FString Lang;                                      // ru / en
		int32 Ok = 0, Fail = 0;
		uint32 CapGen = 0;                                 // поколение перехвата
		TArray<FSent> Sent;                                // перехваченные отправки
		TArray<TFunction<double()>> Steps;                 // шаги: вернуть паузу до следующего, с
		int32 Next = 0;
		FTimerHandle Timer;
		int32 Mark = 0;                                    // индекс `Sent` на начало шага
		int32 GmClicksAtMark = 0;                          // счётчик кликов GM-телепорта на начало шага
		int32 LinesAtMark = 0;                             // строк GM-подсистемы на начало шага
		FIntPoint Self = FIntPoint(256, 256);              // своя клетка спавна

		UGameInstance* GI() const { return World.IsValid() ? World->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
		APlayerController* PC() const { return World.IsValid() ? World->GetFirstPlayerController() : nullptr; }
	};
	using FRef = TSharedRef<FRun>;

	static void Check(FRun& R, bool b, const FString& What)
	{
		const TCHAR* Who = R.bGm ? TEXT("GM") : TEXT("игрок");
		if (b) { ++R.Ok;   UE_LOG(LogAsdaGmMapTpTest, Log,   TEXT("GM-ТЕЛЕПОРТ ok   [%s] %s"), Who, *What); }
		else   { ++R.Fail; UE_LOG(LogAsdaGmMapTpTest, Error, TEXT("GM-ТЕЛЕПОРТ FAIL [%s] %s"), Who, *What); }
	}

	static void Ctrl(FRun& R, bool bDown)
	{
		if (APlayerController* P = R.PC())
		{
			P->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftControl, bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f));
		}
	}

	// Ctrl виден контроллеру (тот же опрос, что у клика).
	static bool CtrlSeen(const FRun& R)
	{
		const APlayerController* P = R.PC();
		return P && (P->IsInputKeyDown(EKeys::LeftControl) || P->IsInputKeyDown(EKeys::RightControl));
	}

	// Начало шага: запомнить отправки, счётчик и строки.
	static void MarkNow(FRun& R)
	{
		R.Mark = R.Sent.Num();
		const UAsdaMinimapSubsystem* MM = R.Sub<UAsdaMinimapSubsystem>();
		R.GmClicksAtMark = MM ? MM->GetGmTeleportClickCount() : 0;
		const UAsdaGmOriginalSubsystem* Gm = R.Sub<UAsdaGmOriginalSubsystem>();
		R.LinesAtMark = Gm ? Gm->DebugLastLines().Num() : 0;
	}
	// Сколько кадров опкода ушло с начала шага; последний — в Out.
	static int32 SentSince(const FRun& R, uint16 Op, TArray<uint8>* Out = nullptr)
	{
		int32 N = 0;
		for (int32 i = R.Mark; i < R.Sent.Num(); ++i)
		{
			if (R.Sent[i].Op != Op) { continue; }
			++N;
			if (Out) { *Out = R.Sent[i].Body; }
		}
		return N;
	}
	static int32 GmClicksSince(const FRun& R)
	{
		const UAsdaMinimapSubsystem* MM = R.Sub<UAsdaMinimapSubsystem>();
		return (MM ? MM->GetGmTeleportClickCount() : 0) - R.GmClicksAtMark;
	}
	// Есть ли с начала шага строка с этим текстом.
	static bool LineSince(const FRun& R, const FString& Text)
	{
		const UAsdaGmOriginalSubsystem* Gm = R.Sub<UAsdaGmOriginalSubsystem>();
		if (!Gm) { return false; }
		const TArray<FString>& L = Gm->DebugLastLines();
		for (int32 i = FMath::Max(0, FMath::Min(R.LinesAtMark, L.Num())); i < L.Num(); ++i) { if (L[i].Contains(Text)) { return true; } }
		return false;
	}
	// Тело 6010: i16 X, i16 Z (`group4_spec.md` §3.1).
	static bool Body6010(const TArray<uint8>& B, int32 X, int32 Z)
	{
		return B.Num() == 4 && (int16)(B[0] | (B[1] << 8)) == X && (int16)(B[2] | (B[3] << 8)) == Z;
	}
	static FString Hex(const TArray<uint8>& B)
	{
		FString S; for (uint8 V : B) { S += FString::Printf(TEXT("%02X"), V); } return S;
	}
	static void Shot(FRun& R, const TCHAR* Step)
	{
		if (R.ShotDir.IsEmpty()) { return; }
		const FString File = FPaths::Combine(R.ShotDir, FString::Printf(TEXT("%s_%s_%s.png"), *R.Lang, R.bGm ? TEXT("gm") : TEXT("player"), Step));
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
	}
	// Индекс области списка «Область» (в своём континенте) с картой Want (bOther — любая карта, кроме Want). false — нет.
	static bool FindArea(const UAsdaMinimapSubsystem* MM, int32 Want, bool bOther, int32& OutCont, int32& OutIdx, int32& OutMap)
	{
		const TArray<FAsdaWorldMapArea>& Areas = MM->GetWorldMapAreas();
		for (int32 a = 0; a < Areas.Num(); ++a)
		{
			const bool bHit = bOther ? (Areas[a].MapId != Want && Areas[a].MapId >= 0) : (Areas[a].MapId == Want);
			if (!bHit) { continue; }
			int32 InCont = 0;
			for (int32 i = 0; i < a; ++i) { if (Areas[i].Continent == Areas[a].Continent) { ++InCont; } }
			OutCont = Areas[a].Continent; OutIdx = InCont; OutMap = Areas[a].MapId;
			return true;
		}
		return false;
	}
	static bool SelectRegion(FRun& R, int32 Want, bool bOther, int32& OutMap)
	{
		UAsdaMinimapSubsystem* MM = R.Sub<UAsdaMinimapSubsystem>();
		if (!MM) { return false; }
		if (!MM->IsBigMapOpen()) { MM->ToggleBigMap(); }
		int32 C = 0, I = 0;
		if (!FindArea(MM, Want, bOther, C, I, OutMap)) { return false; }
		MM->SelectContinent(C);
		MM->SelectArea(I);
		return MM->GetDisplayedMapId() == OutMap;
	}

	static void Pump(FRef R)
	{
		UWorld* W = R->World.Get();
		if (!W) { return; }
		if (R->Next >= R->Steps.Num())
		{
			Ctrl(*R, false);
			AsdaSelfTestKit::DisarmCapture(R->Sub<UAsdaNetSubsystem>(), R->CapGen);
			AsdaSelfTestKit::Finish(W, kCmd, R->bQuit, LogAsdaGmMapTpTest, TEXT("GM-ТЕЛЕПОРТ"), R->Ok, R->Fail,
				*FString::Printf(TEXT(" (имя %s, язык %s)"), *R->Name, *R->Lang));
			return;
		}
		const double Delay = R->Steps[R->Next++]();
		W->GetTimerManager().SetTimer(R->Timer, FTimerDelegate::CreateLambda([R]() { Pump(R); }), FMath::Max(0.05, Delay), false);
	}

	// Шаги GM_.
	static void AddGmSteps(FRef R)
	{
		FRun& S = *R;
		const UAsdaMinimapSubsystem* M0 = S.Sub<UAsdaMinimapSubsystem>();
		const FIntPoint T1 = FAsdaGmMapTeleportSelfTest::NearWalkable(M0, FIntPoint(S.Self.X + 6, S.Self.Y - 4));
		const FIntPoint T2(S.Self.X - 5, S.Self.Y + 3), T3(S.Self.X + 12, S.Self.Y + 9);
		// A1 без Ctrl: обычный клик.
		S.Steps.Add([R, T1]() -> double
		{
			Ctrl(*R, false); MarkNow(*R);
			UAsdaMinimapSubsystem* MM = R->Sub<UAsdaMinimapSubsystem>();
			FAsdaGmMapTeleportSelfTest::ClearDest(MM);
			FAsdaGmMapTeleportSelfTest::ClickMinimap(MM, T1);
			return 0.3;
		});
		S.Steps.Add([R]() -> double
		{
			Check(*R, SentSince(*R, 6010) == 0 && SentSince(*R, 6036) == 0 && GmClicksSince(*R) == 0
				&& FAsdaGmMapTeleportSelfTest::DestActive(R->Sub<UAsdaMinimapSubsystem>()),
				FString::Printf(TEXT("A1 клик по мини-карте без Ctrl — обычный ход (цель хода есть), 6010=%d 6036=%d"),
					SentSince(*R, 6010), SentSince(*R, 6036)));
			return 0.05;
		});
		// A2 Ctrl, права не подтверждены → проба 6036 → ответ 6037 → 6010.
		S.Steps.Add([R, T1]() -> double
		{
			UAsdaNetSubsystem* Net = R->Sub<UAsdaNetSubsystem>();
			if (Net) { Net->ClearOperatorConfirmation(); }
			Ctrl(*R, true);   // Ctrl зажат ДО щелчка, как у игрока (состояние клавиши контроллер видит со следующего кадра)
			return 0.2;
		});
		S.Steps.Add([R, T1]() -> double
		{
			Check(*R, CtrlSeen(*R), TEXT("A2-0 Ctrl зажат и виден контроллеру"));
			MarkNow(*R);
			FAsdaGmMapTeleportSelfTest::ClickMinimap(R->Sub<UAsdaMinimapSubsystem>(), T1);
			return 0.3;
		});
		S.Steps.Add([R]() -> double
		{
			const UAsdaGmOriginalSubsystem* Gm = R->Sub<UAsdaGmOriginalSubsystem>();
			Check(*R, SentSince(*R, 6036) == 1 && SentSince(*R, 6010) == 0 && Gm && Gm->IsMapTpPending(),
				FString::Printf(TEXT("A2a Ctrl+клик без подтверждённых прав: проба 6036 ушла (%d), 6010 ещё нет (%d), клик ждёт ответа"),
					SentSince(*R, 6036), SentSince(*R, 6010)));
			if (UAsdaNetSubsystem* Net = R->Sub<UAsdaNetSubsystem>()) { Net->DebugFeedPacket(6037, TArray<uint8>()); }
			return 0.3;
		});
		S.Steps.Add([R, T1]() -> double
		{
			TArray<uint8> B;
			const int32 N = SentSince(*R, 6010, &B);
			Check(*R, N == 1 && Body6010(B, T1.X, T1.Y), FString::Printf(TEXT("A2b ответ 6037 → 6010 ушёл (%d), тело %s = клетка (%d,%d)"),
				N, *Hex(B), T1.X, T1.Y));
			return 0.05;
		});
		// A3 Ctrl по мини-карте с правами → 6010 сразу, хода нет.
		S.Steps.Add([R, T2]() -> double
		{
			MarkNow(*R);
			UAsdaMinimapSubsystem* MM = R->Sub<UAsdaMinimapSubsystem>();
			FAsdaGmMapTeleportSelfTest::ClearDest(MM);
			FAsdaGmMapTeleportSelfTest::ClickMinimap(MM, T2);
			TArray<uint8> B;
			const int32 N = SentSince(*R, 6010, &B);
			Check(*R, N == 1 && Body6010(B, T2.X, T2.Y) && SentSince(*R, 6036) == 0 && !FAsdaGmMapTeleportSelfTest::DestActive(MM)
				&& GmClicksSince(*R) == 1,
				FString::Printf(TEXT("A3 Ctrl+клик по мини-карте: 6010 сразу (%d), тело %s = (%d,%d), хода нет"), N, *Hex(B), T2.X, T2.Y));
			Shot(*R, TEXT("a3_minimap"));
			return 0.6;
		});
		// A4 карта мира, своя карта.
		S.Steps.Add([R, T3]() -> double
		{
			UAsdaMinimapSubsystem* MM = R->Sub<UAsdaMinimapSubsystem>();
			int32 Map = -1;
			const bool bSel = SelectRegion(*R, FAsdaGmMapTeleportSelfTest::OwnMapId(MM), false, Map);
			MarkNow(*R);
			FAsdaGmMapTeleportSelfTest::ClickBigMap(MM, FVector2D(T3.X + 0.5, T3.Y + 0.5));
			TArray<uint8> B;
			const int32 N = SentSince(*R, 6010, &B);
			Check(*R, bSel && N == 1 && Body6010(B, T3.X, T3.Y) && !MM->IsTravelRequested(),
				FString::Printf(TEXT("A4 карта мира, своя карта %d (выбрана %d): 6010 (%d), тело %s = (%d,%d), автопути нет"),
					FAsdaGmMapTeleportSelfTest::OwnMapId(MM), bSel ? 1 : 0, N, *Hex(B), T3.X, T3.Y));
			Shot(*R, TEXT("a4_worldmap_own"));
			return 0.6;
		});
		// A5 карта мира, чужая карта → 6010 формы P123: тело 8 б (X, Z, карта, метка 0xA123), без паддинга (кадр ровно 42 б).
		S.Steps.Add([R]() -> double
		{
			UAsdaMinimapSubsystem* MM = R->Sub<UAsdaMinimapSubsystem>();
			int32 Map = -1;
			const bool bSel = SelectRegion(*R, FAsdaGmMapTeleportSelfTest::OwnMapId(MM), true, Map);
			MM->CancelTravelRequest();
			MarkNow(*R);
			FAsdaGmMapTeleportSelfTest::ClickBigMap(MM, FVector2D(200.5, 200.5));
			TArray<uint8> B;
			const int32 N = SentSince(*R, 6010, &B);
			const bool bPad = R->Sent.Num() > 0 && R->Sent.Last().Op == 6010 && R->Sent.Last().bPad;
			const bool bBody = B.Num() == 8 && (int16)(B[0] | (B[1] << 8)) == 200 && (int16)(B[2] | (B[3] << 8)) == 200
				&& (int16)(B[4] | (B[5] << 8)) == Map && (uint16)(B[6] | (B[7] << 8)) == 0xA123;
			const FString L = FAsdaText::Loc(991303).Replace(TEXT("[ss1]"), *FString::FromInt(Map)).Replace(TEXT("[ss2]"), TEXT("200"))
				.Replace(TEXT("[ss3]"), TEXT("200"));
			Check(*R, bSel && N == 1 && bBody && !bPad && SentSince(*R, 6036) == 0 && GmClicksSince(*R) == 1 && !MM->IsTravelRequested()
				&& LineSince(*R, L) && !L.StartsWith(TEXT("[#")),
				FString::Printf(TEXT("A5 карта мира, чужая карта %d: 6010 формы P123 (%d), тело %s = (200,200) карта %d метка A123, паддинг %s, "
					"автопути нет, строка «%s»"), Map, N, *Hex(B), Map, bPad ? TEXT("ЕСТЬ") : TEXT("нет"), *L));
			Shot(*R, TEXT("a5_worldmap_other"));
			return 0.6;
		});
		// A5b ключ asda.Gm.MapTeleportOtherMap 0 → на чужую карту ничего не уходит, строка 991300.
		S.Steps.Add([R]() -> double
		{
			UAsdaMinimapSubsystem* MM = R->Sub<UAsdaMinimapSubsystem>();
			AsdaCVarSave::SetForTest(TEXT("asda.Gm.MapTeleportOtherMap"), 0);
			MM->CancelTravelRequest();
			MarkNow(*R);
			FAsdaGmMapTeleportSelfTest::ClickBigMap(MM, FVector2D(200.5, 200.5));
			const FString L = FAsdaText::Loc(991300);
			Check(*R, SentSince(*R, 6010) == 0 && SentSince(*R, 6036) == 0 && GmClicksSince(*R) == 1 && !MM->IsTravelRequested()
				&& LineSince(*R, L) && !L.StartsWith(TEXT("[#")),
				FString::Printf(TEXT("A5b asda.Gm.MapTeleportOtherMap 0: чужая карта — 6010 нет (%d), строка «%s»"), SentSince(*R, 6010), *L));
			AsdaCVarSave::SetForTest(TEXT("asda.Gm.MapTeleportOtherMap"), 1);
			return 0.3;
		});
		// A6 клетка вне карты (клетка 0 — сервер отказал бы `IsCheckPosInMaxPos`).
		S.Steps.Add([R]() -> double
		{
			UAsdaMinimapSubsystem* MM = R->Sub<UAsdaMinimapSubsystem>();
			int32 Map = -1;
			SelectRegion(*R, FAsdaGmMapTeleportSelfTest::OwnMapId(MM), false, Map);
			MarkNow(*R);
			// Пиксель (0.2, 0.2) — клетка 0 (у городского кропа тоже: клетки (x,y) = кроп + px/S ≥ кропа; берём поле без кропа).
			FAsdaGmMapTeleportSelfTest::ClickBigMapRawNoCrop(MM, FVector2D(0.2, 0.2));
			const FString L = FAsdaText::Loc(991302).Replace(TEXT("[ss1]"), TEXT("0")).Replace(TEXT("[ss2]"), TEXT("0"));
			Check(*R, SentSince(*R, 6010) == 0 && GmClicksSince(*R) == 1 && LineSince(*R, L) && !L.StartsWith(TEXT("[#")),
				FString::Printf(TEXT("A6 клетка (0,0) вне карты: 6010 нет (%d), строка «%s»"), SentSince(*R, 6010), *L));
			MM->ToggleBigMap();   // закрыть карту мира
			return 0.3;
		});
		// A7 ключ выключен → обычный клик.
		S.Steps.Add([R, T1]() -> double
		{
			AsdaCVarSave::SetForTest(TEXT("asda.Gm.MapClickTeleport"), 0);
			MarkNow(*R);
			UAsdaMinimapSubsystem* MM = R->Sub<UAsdaMinimapSubsystem>();
			FAsdaGmMapTeleportSelfTest::ClearDest(MM);
			FAsdaGmMapTeleportSelfTest::ClickMinimap(MM, T1);
			Check(*R, SentSince(*R, 6010) == 0 && GmClicksSince(*R) == 0 && FAsdaGmMapTeleportSelfTest::DestActive(MM),
				FString::Printf(TEXT("A7 asda.Gm.MapClickTeleport 0: Ctrl+клик — обычный ход, 6010 нет (%d)"), SentSince(*R, 6010)));
			AsdaCVarSave::SetForTest(TEXT("asda.Gm.MapClickTeleport"), 1);
			return 0.2;
		});
	}

	// Шаги обычного игрока.
	static void AddPlayerSteps(FRef R)
	{
		FRun& S = *R;
		const FIntPoint T1 = FAsdaGmMapTeleportSelfTest::NearWalkable(S.Sub<UAsdaMinimapSubsystem>(), FIntPoint(S.Self.X + 6, S.Self.Y - 4));
		const FIntPoint T3(S.Self.X + 12, S.Self.Y + 9);
		// B1 мини-карта, даже с подтверждёнными правами (ответ 6037).
		S.Steps.Add([R]() -> double
		{
			if (UAsdaNetSubsystem* Net = R->Sub<UAsdaNetSubsystem>()) { Net->ClearOperatorConfirmation(); Net->DebugFeedPacket(6037, TArray<uint8>()); }
			Ctrl(*R, true);   // Ctrl зажат ДО щелчка
			return 0.3;
		});
		S.Steps.Add([R, T1]() -> double
		{
			Check(*R, CtrlSeen(*R), TEXT("B1-0 Ctrl зажат и виден контроллеру"));
			MarkNow(*R);
			UAsdaMinimapSubsystem* MM = R->Sub<UAsdaMinimapSubsystem>();
			FAsdaGmMapTeleportSelfTest::ClearDest(MM);
			FAsdaGmMapTeleportSelfTest::ClickMinimap(MM, T1);
			return 0.4;
		});
		S.Steps.Add([R]() -> double
		{
			UAsdaMinimapSubsystem* MM = R->Sub<UAsdaMinimapSubsystem>();
			const UAsdaNetSubsystem* Net = R->Sub<UAsdaNetSubsystem>();
			Check(*R, !AsdaNpcGmOnly::SelfIsGm(R->GI()) && SentSince(*R, 6010) == 0 && SentSince(*R, 6036) == 0 && GmClicksSince(*R) == 0
				&& FAsdaGmMapTeleportSelfTest::DestActive(MM),
				FString::Printf(TEXT("B1 не GM_ (права от сервера=%d): Ctrl+клик по мини-карте — 6010=%d 6036=%d, обычный ход"),
					Net && Net->IsOperatorConfirmedByServer() ? 1 : 0, SentSince(*R, 6010), SentSince(*R, 6036)));
			Shot(*R, TEXT("b1_minimap"));
			return 0.6;
		});
		// B2 карта мира: своя и чужая карта.
		S.Steps.Add([R, T3]() -> double
		{
			UAsdaMinimapSubsystem* MM = R->Sub<UAsdaMinimapSubsystem>();
			int32 Map = -1;
			const bool bOwn = SelectRegion(*R, FAsdaGmMapTeleportSelfTest::OwnMapId(MM), false, Map);
			MarkNow(*R);
			FAsdaGmMapTeleportSelfTest::ClickBigMap(MM, FVector2D(T3.X + 0.5, T3.Y + 0.5));
			int32 OtherMap = -1;
			const bool bOther = SelectRegion(*R, FAsdaGmMapTeleportSelfTest::OwnMapId(MM), true, OtherMap);
			MM->CancelTravelRequest();
			FAsdaGmMapTeleportSelfTest::ClickBigMap(MM, FVector2D(200.5, 200.5));
			Check(*R, bOwn && bOther && SentSince(*R, 6010) == 0 && SentSince(*R, 6036) == 0 && GmClicksSince(*R) == 0 && MM->IsTravelRequested(),
				FString::Printf(TEXT("B2 не GM_: Ctrl+клик по карте мира (своя и чужая %d) — 6010=%d 6036=%d, чужая — обычная заявка автопути (%d)"),
					OtherMap, SentSince(*R, 6010), SentSince(*R, 6036), MM->IsTravelRequested() ? 1 : 0));
			Shot(*R, TEXT("b2_worldmap_other"));
			return 0.6;
		});
		S.Steps.Add([R]() -> double
		{
			UAsdaMinimapSubsystem* MM = R->Sub<UAsdaMinimapSubsystem>();
			if (MM) { MM->CancelTravelRequest(); if (MM->IsBigMapOpen()) { MM->ToggleBigMap(); } }
			return 0.1;
		});
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestGmMapTeleport(
	TEXT("asda.SelfTest.GmMapTeleport"),
	TEXT("Самотест GM-телепорта Ctrl+ЛКМ по мини-карте и карте мира: GM_ — 6010 (своя карта, с пробой прав 6036/6037), чужая карта и "
	     "клетка вне карты — ничего не уходит; не GM_ — ничего не уходит, клик обычный. Офлайн. `name=<имя>` `shotdir=<папка>` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaGmMapTpTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		FString Name = TEXT("GM_Tester"), ShotDir;
		for (const FString& A : Args)
		{
			if (A.StartsWith(TEXT("name="), ESearchCase::IgnoreCase)) { Name = A.Mid(5); }
			if (A.StartsWith(TEXT("shotdir="), ESearchCase::IgnoreCase)) { ShotDir = A.Mid(8); }
		}
		// Свой персонаж с нужным именем: штатный офлайн-спавн на Alpen (3) и повторный запуск через 2 с.
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W, *Name))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, *FString::Printf(TEXT("Asda.World.FakeSelfSpawn 256 256 %s"), *Name));
			FString Again = FString::Printf(TEXT("%s name=%s selfready"), kCmd, *Name);
			if (!ShotDir.IsEmpty()) { Again += FString::Printf(TEXT(" shotdir=%s"), *ShotDir); }
			if (KA.bQuit) { Again += TEXT(" quit"); }
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]()
			{
				if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); }
			}), 2.f, false);
			return;
		}
		static const uint16 kOwnOps[] = { 6010, 6036 };
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA, 0.f, kOwnOps)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaMinimapSubsystem* MM = GI ? GI->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr;
		if (!Net || !MM || !GI->GetSubsystem<UAsdaGmOriginalSubsystem>() || !W->GetFirstPlayerController())
		{
			AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем или контроллера"));
			return;
		}
		FRef R = MakeShared<FRun>();
		R->World = W; R->bQuit = KA.bQuit; R->Name = Name; R->ShotDir = ShotDir;
		R->bGm = Name.StartsWith(TEXT("GM_"), ESearchCase::IgnoreCase);
		R->Lang = FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en");
		TWeakPtr<FRun> WR = R;
		R->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WR](uint16 Op, const uint8* P, int32 Len, bool bPad, TCHAR)
		{
			if (TSharedPtr<FRun> S = WR.Pin()) { FSent X; X.Op = Op; X.bPad = bPad; if (P && Len > 0) { X.Body.Append(P, Len); } S->Sent.Add(MoveTemp(X)); }
		}, 0, kCmd);
		if (R->CapGen == 0) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("перехват отправок занят")); return; }
		// Старт: своё имя и карта.
		R->Steps.Add([R]() -> double
		{
			UAsdaMinimapSubsystem* M = R->Sub<UAsdaMinimapSubsystem>();
			Check(*R, AsdaNpcGmOnly::SelfIsGm(R->GI()) == R->bGm && FAsdaGmMapTeleportSelfTest::OwnMapId(M) == 3,
				FString::Printf(TEXT("старт: имя «%s» (%s), своя карта %d"), *R->Name, R->bGm ? TEXT("GM_") : TEXT("не GM_"),
					FAsdaGmMapTeleportSelfTest::OwnMapId(M)));
			return 0.1;
		});
		if (R->bGm) { AddGmSteps(R); } else { AddPlayerSteps(R); }
		Pump(R);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
