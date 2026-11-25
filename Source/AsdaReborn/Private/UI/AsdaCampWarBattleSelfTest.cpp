// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «БИТВЫ АРМИЙ» — БОЕВАЯ ФАЗА (30.10.2026): `asda.UI.SelfTestCampWarBattle [delay=N] [shots] [quit]` — HUD боя на карте 19,
//   окна 49600/49900/49910/49940/49510/49800/49399/49400, кнопка 17520 миникарты, приёмы 6715…6742, отправки 6717/6731/6734/6186.
//   Разбор — `Reports/faction_select/BATTLE_SPEC.md`, FLOW.md §8. Входы ТОЛЬКО штатные:
//     • вход на поле боя — 5055 код 1 (`UAsdaCampWarSubsystem::OnPortalTransfer5055` — та же точка, что зовёт сетевой слой; офлайн
//       сам переход не выполняется: номер текущей карты ставится как после загрузки — `Asda.Load.CurrentMap 19`);
//     • пакеты — `DebugFeedPacket` (проход через ШТАТНЫЙ диспетчер `HandlePacket` с длинами по серверу);
//     • клики — НАСТОЯЩИЕ события Slate по живой геометрии (`SimulateClickAt`, в том числе ПКМ): кнопки окон, строки списка,
//       «Да»/«Нет» двухкнопочного окна, кнопка 17520 миникарты;
//     • отправки — боевые отправители под перехватом (в сеть не уходит ничего).
//   Объект точки захвата — рантайм-копия NPC (renderId = номер точки), как её создаёт сетевой спавн.
//   Контрольные пары: в отряде ↔ нет (5098 при входе), свой захват ↔ чужой без персонажа, «Да» ↔ «Нет» (6731), сумка пуста ↔ есть
//   предмет (6186), ключ исправления 0 ↔ 1 (Д-Б4 «Пригласить»), код 6728 в диапазоне ↔ вне, точка есть ↔ нет (6725).
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "AsdaCampWarBattleInternal.h"
#include "UI/AsdaCampWarBattleSubsystem.h"
#include "UI/AsdaCampWarSubsystem.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaChatWindowSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaPartySubsystem.h"
#include "UI/AsdaSystemMenuSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Npc/AsdaNpcActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"   // модель объекта точки в самотесте (тон на настоящих слотах материала)
#include "Materials/MaterialInterface.h"   // Tex слотов точки после замены на мастер тона
#include "World/AsdaFloatingTextSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaWalkGrid.h"            // ★ 31.10: вода поля битвы закрыта
#include "World/AsdaPlayerCharacter.h"
#include "UI/AsdaAfterDeathSubsystem.h"      // ★ 31.10: окно смерти на карте 19
#include "Login/AsdaLoginData.h"              // ★ 31.10: разбор тела 6718 / сборка 6719   // ★ 30.10.2026: поза 47 / жест 51
#include "Animation/AnimSingleNodeInstance.h"
#include "Blueprint/UserWidget.h"
#include "Components/Image.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "Framework/Application/SlateApplication.h"   // ★ 31.10: сетка клика — путь Slate под точкой
#include "Types/ReflectionMetadata.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"
#include "UnrealClient.h"          // FScreenshotRequest — снимки окон (аргумент `shots`)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCampWarBattleTest, Log, All);

namespace AsdaCampWarBattleTestLocal
{
	using namespace AsdaCwBattle;
	static const TCHAR* kCmd = TEXT("asda.UI.SelfTestCampWarBattle");   // имя команды

	// Счётчики прогона.
	struct FRun
	{
		int32 Ok = 0, Fail = 0;   // пройдено / провалено
		// Одна проверка: печать и счёт.
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaCampWarBattleTest, Log, TEXT("[бой-самотест] ОК — %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaCampWarBattleTest, Error, TEXT("[бой-самотест] ПРОВАЛ — %s"), *What); }
		}
	};

	// Перехваченная отправка: опкод и payload (без конверта).
	using FSent = TArray<TPair<int32, TArray<uint8>>>;

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		FRun R;                                   // счётчики
		TWeakObjectPtr<UWorld> W;                 // мир прогона
		float Delay = 0.f;                        // сдвиг старта
		bool bQuit = false;                       // выйти после итога
		FString ShotDir;                          // папка снимков (`shots`)
		TSharedPtr<FSent> Sent;                   // перехваченные исходящие
		uint32 CapGen = 0;                        // поколение перехвата
		TSharedPtr<FAsdaNetSelfScope> SelfScope;  // свой charId/номер (RAII)
		TSharedPtr<FAsdaCVarScope> Keys;          // ключи на время прогона (RAII)
		int32 SavedCamp = -2;                     // своя армия до прогона
		int32 SavedMap = 0;                       // карта до прогона
		TArray<TWeakObjectPtr<AActor>> Spawned;   // объекты точек, созданные прогоном
		int32 ItemCode = 0;                       // существующий код предмета (для сумки и награды)
		int32 Live0 = 0;                          // живых всплывающих надписей до 6730
		bool bBagEmpty = true;                    // сумка была пуста до K9
		bool bItemAdded = false;                  // прогон положил предмет в сумку (ячейка 0) — снять в конце
		int32 SavedGold = -1;                     // золото до 6733
		int32 SavedChatState = -1;                // режим ввода чата до прогона
		// ★ 31.10.2026 (баги игроков: чат армии, окно смерти на карте 19) — счётчики «до» и сохранённая карта входа.
		int32 N6718 = 0;                          // отправок 6718 до шага S
		int32 NSentAll = 0;                       // всех отправок до шага S3 (канал 0 в бою — пакета нет)
		int32 N5327 = 0;                          // строк 5327 в ленте до шага S4
		int32 N6719 = 0;                          // строк эха 6719 до шага S5
		int32 N4054 = 0;                          // отправок 4054 до шага окна смерти
		int32 SavedEnterMap = -1;                 // `Net->EnterMapId` до шага T
		float T = 0.f;                            // время следующего шага
	};

	static constexpr int32 kSelfCharId = 930404;   // свой charId на время прогона
	static constexpr int32 kSelfNum = 77;          // свой userNum
	static constexpr int32 kMateCharId = 930405;   // соратник по скваду

	using AsdaSelfTestCtx::GIOf;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	using AsdaSelfTestCtx::Sub;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	static FString L(const TSharedRef<FCtx>& C, int32 Id) { return AsdaNotice::Loc(GIOf(C), Id); }
	static UAsdaCampWarBattleSubsystem* B(const TSharedRef<FCtx>& C) { return Sub<UAsdaCampWarBattleSubsystem>(C); }
	using AsdaSelfTestCtx::Feed;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия отличалась только тем, что подсистема сети бралась через `Sub<UAsdaNetSubsystem>` — то же, что `AsdaTestNet::Get`.

	// Следующий шаг через Step секунд после предыдущего.
	using AsdaSelfTestCtx::Then;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	// Настоящий клик Slate по центру живой геометрии узла.
	static bool ClickNode(const TSharedRef<FCtx>& C, UWidget* Node, const FString& What, bool bRight = false)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		FVector2D Mn, Mx;
		const bool bGeo = WM && Node && AsdaUi::WidgetAbsRect(Node, Mn, Mx);
		C->R.Expect(bGeo, FString::Printf(TEXT("клик «%s»: живая геометрия есть"), *What));
		if (bGeo) { WM->SimulateClickAt((Mn + Mx) * 0.5f, /*HoldFrames*/ 3, bRight); }
		return bGeo;
	}
	// Узел окна по имени.
	static UWidget* WinNode(const TSharedRef<FCtx>& C, const TCHAR* Win, const TCHAR* Node)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		UUserWidget* W = WM ? WM->FindWindow(FName(Win)) : nullptr;
		return W ? W->GetWidgetFromName(FName(Node)) : nullptr;
	}
	// Сколько перехвачено опкода Op; OutLast — payload последнего.
	using AsdaSelfTestCtx::CountOp;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	// Снять окна-сообщения.
	static void Dismiss(const TSharedRef<FCtx>& C) { if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C)) { WM->ClearMsgBoxQueue(TEXT("самотест боя: шаг")); } }
	// ★ 31.10.2026 (жалоба игроков «верх экрана над персонажем не кликается» на карте 19): СЕТКА 32×24 ПО ВЬЮПОРТУ — кто ловит
	//   нажатие в каждой ячейке: шлюз «курсор над окном» (`IsScreenPointOverAnyUiWindow` — им контроллер решает «клик в мир или в
	//   окно», геометрия `GetCachedGeometry`) и лист пути Slate под точкой (`LocateWindowUnderMouse`). Карта — в журнал.
	struct FUiScan
	{
		int32 Cells = 0;                 // ячеек всего
		int32 Blocked = 0;               // ячеек, где шлюз говорит «окно»
		int32 WorldBandBlocked = 0;      // из них в полосе мира (середина экрана — вне угловых окон HUD)
		int32 CenterBlocked = 0;         // из них над персонажем: столбцы 10…21, строки 1…11 (верх-центр экрана)
		int32 CenterBlockedCw = 0;       // из них — окнами/зонами боя (CAMPWAR_*, CAPWAR_*)
		int32 CwCells = 0;               // всего ячеек, закрытых окнами/зонами боя
		TMap<FString, int32> ByZone;     // окно/зона шлюза → число ячеек
		TMap<FString, int32> BySlate;    // лист Slate → число ячеек
	};
	static FUiScan ScanUi(const TSharedRef<FCtx>& C, const TCHAR* Tag)
	{
		FUiScan Out;
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		if (!WM || !GEngine || !GEngine->GameViewport || !GEngine->GameViewport->GetGameViewportWidget().IsValid()) { return Out; }
		const FGeometry G = GEngine->GameViewport->GetGameViewportWidget()->GetCachedGeometry();
		const FVector2D Sz = G.GetLocalSize();
		if (Sz.X < 64.f || Sz.Y < 64.f) { return Out; }
		constexpr int32 NX = 32, NY = 24;
		TArray<TSharedRef<SWindow>> Wins;
		FSlateApplication::Get().GetAllVisibleWindowsOrdered(Wins);
		TMap<FString, TCHAR> Letter;
		FString Map;
		for (int32 Y = 0; Y < NY; ++Y)
		{
			FString Row;
			for (int32 X = 0; X < NX; ++X)
			{
				const FVector2D Abs = G.LocalToAbsolute(FVector2D((X + 0.5f) * Sz.X / NX, (Y + 0.5f) * Sz.Y / NY));
				FName Zone;
				const bool bUi = WM->IsScreenPointOverAnyUiWindow(Abs, Zone);
				++Out.Cells;
				const FWidgetPath Path = FSlateApplication::Get().LocateWindowUnderMouse(Abs, Wins, /*bIgnoreEnabledStatus*/ true);
				FString Leaf = TEXT("-");
				if (Path.IsValid() && Path.Widgets.Num() > 0)
				{
					const TSharedRef<SWidget> Lw = Path.Widgets.Last().Widget;
					Leaf = Lw->GetTypeAsString() + TEXT(" ") + FReflectionMetaData::GetWidgetDebugInfo(&Lw.Get());
				}
				Out.BySlate.FindOrAdd(Leaf) += 1;
				if (!bUi) { Row += TEXT('.'); continue; }
				++Out.Blocked;
				const bool bBand = (Y >= 2 && Y <= 17 && X >= 7 && X <= 24);
				if (bBand) { ++Out.WorldBandBlocked; }
				const bool bCw = Zone.ToString().StartsWith(TEXT("CAMPWAR")) || Zone.ToString().StartsWith(TEXT("CAPWAR"));
				if (bCw) { ++Out.CwCells; }
				if (Y >= 1 && Y <= 11 && X >= 10 && X <= 21) { ++Out.CenterBlocked; if (bCw) { ++Out.CenterBlockedCw; } }
				const FString Z = Zone.ToString();
				Out.ByZone.FindOrAdd(Z) += 1;
				if (!Letter.Contains(Z)) { Letter.Add(Z, (TCHAR)(TEXT('A') + Letter.Num() % 26)); }
				Row += Letter[Z];
			}
			Map += TEXT("\n    ") + Row;
		}
		FString Legend;
		for (const TPair<FString, TCHAR>& L : Letter) { Legend += FString::Printf(TEXT(" %c=%s(%d)"), L.Value, *L.Key, Out.ByZone[L.Key]); }
		UE_LOG(LogAsdaCampWarBattleTest, Log, TEXT("[бой-самотест] СЕТКА КЛИКА «%s» (вьюпорт %.0f×%.0f): окно в %d из %d ячеек, в полосе мира %d; %s%s"),
			Tag, Sz.X, Sz.Y, Out.Blocked, Out.Cells, Out.WorldBandBlocked, *Legend, *Map);
		UE_LOG(LogAsdaCampWarBattleTest, Log, TEXT("[бой-самотест] СЕТКА КЛИКА «%s»: %s"), Tag, *WM->DebugUiZones());
		FString Sl;
		for (const TPair<FString, int32>& L : Out.BySlate) { Sl += FString::Printf(TEXT("\n    %4d × %s"), L.Value, *L.Key); }
		UE_LOG(LogAsdaCampWarBattleTest, Log, TEXT("[бой-самотест] СЕТКА КЛИКА «%s»: листья Slate под точками:%s"), Tag, *Sl);
		return Out;
	}
	// Снимок экрана с интерфейсом (только с `shots`).
	static void Shot(const TSharedRef<FCtx>& C, const TCHAR* Name)
	{
		if (C->ShotDir.IsEmpty()) { return; }
		FScreenshotRequest::RequestScreenshot(C->ShotDir / FString::Printf(TEXT("%s.png"), Name), /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
	}
	// Видим ли узел окна (по имени).
	static bool NodeShown(const TSharedRef<FCtx>& C, const TCHAR* Win, const TCHAR* Node) { return AsdaUi::IsShown(WinNode(C, Win, Node)); }

	// ── Тела пакетов (раскладки — BATTLE_SPEC §4) ──
	static TArray<uint8> Make6726(int32 Id, int32 Owner, int32 State, int32 Bonus)
	{
		TArray<uint8> P; P.SetNumZeroed(11);
		AsdaBytes::WrI16(P.GetData(), Id); AsdaBytes::WrI16(P.GetData() + 2, 10 + Id); AsdaBytes::WrI16(P.GetData() + 4, 20 + Id);
		AsdaBytes::WrI16(P.GetData() + 6, Owner); P[8] = (uint8)State; AsdaBytes::WrI16(P.GetData() + 9, Bonus);
		return P;
	}
	static TArray<uint8> Make6724(int32 Id, int32 Sub, int32 CharId)
	{
		TArray<uint8> P; P.SetNumZeroed(7); AsdaBytes::WrI16(P.GetData(), Id); P[2] = (uint8)Sub; AsdaBytes::WrI32(P.GetData() + 3, CharId); return P;
	}
	static TArray<uint8> Make6728(int32 Code, int32 N) { TArray<uint8> P; P.SetNumZeroed(3); P[0] = (uint8)Code; AsdaBytes::WrI16(P.GetData() + 1, N); return P; }
	static TArray<uint8> Make3I32(int32 A, int32 Bv, int32 Cv)
	{
		TArray<uint8> P; P.SetNumZeroed(12); AsdaBytes::WrI32(P.GetData(), A); AsdaBytes::WrI32(P.GetData() + 4, Bv); AsdaBytes::WrI32(P.GetData() + 8, Cv); return P;
	}
	static TArray<uint8> Make3I16(int32 A, int32 Bv, int32 Cv)
	{
		TArray<uint8> P; P.SetNumZeroed(6); AsdaBytes::WrI16(P.GetData(), A); AsdaBytes::WrI16(P.GetData() + 2, Bv); AsdaBytes::WrI16(P.GetData() + 4, Cv); return P;
	}
	static TArray<uint8> Make6713(int32 Fid) { TArray<uint8> P; P.SetNumZeroed(7); AsdaBytes::WrI16(P.GetData(), Fid); return P; }
	static TArray<uint8> Make6701()
	{
		// Расписание 19:00–19:20, регион 0, всё прочее нули (раскладка `Recv_ClanInfo`).
		TArray<uint8> P; P.SetNumZeroed(44);
		AsdaBytes::WrI16(P.GetData() + 2, 19); AsdaBytes::WrI16(P.GetData() + 6, 19); AsdaBytes::WrI16(P.GetData() + 8, 20);
		AsdaBytes::WrI16(P.GetData() + 10, 1);
		return P;
	}
	static TArray<uint8> Make6732(int32 Winner, int32 Type1Code, int32 ItemCode)
	{
		TArray<uint8> P; P.SetNumZeroed(65);
		AsdaBytes::WrI16(P.GetData(), Winner);
		AsdaBytes::WrI16(P.GetData() + 2, 0); AsdaBytes::WrI16(P.GetData() + 4, 1); AsdaBytes::WrI16(P.GetData() + 6, 2);
		const int32 A[3] = { 100, 50, 0 }, Bk[3] = { 100, 50, 0 };
		for (int32 I = 0; I < 3; ++I) { AsdaBytes::WrI32(P.GetData() + 8 + 4 * I, A[I]); AsdaBytes::WrI32(P.GetData() + 20 + 4 * I, Bk[I]); }
		AsdaBytes::WrI32(P.GetData() + 32, 777); AsdaBytes::WrI16(P.GetData() + 36, 12);
		P[38] = 0; P[39] = 1; P[40] = 4;
		AsdaBytes::WrI32(P.GetData() + 41, 5000); AsdaBytes::WrI32(P.GetData() + 45, Type1Code); AsdaBytes::WrI32(P.GetData() + 49, ItemCode);
		AsdaBytes::WrI32(P.GetData() + 61, 2);
		return P;
	}
	// Запись сквада 119 б: [0] слот, далее запись 118 б (+0 занят, +1 userNum, +3 charId, +8 капитан, +9 звание, +10 ур., +12/+13 проф.,
	//   +55 отряд, +57 имя, +77 глава, +97 клан).
	static void AddMember(TArray<uint8>& P, int32 Slot, int32 UserNum, int32 CharId, int32 Captain, int32 Grade, int32 Lv, int32 Squad, const char* Name)
	{
		const int32 Off = P.Num();
		P.AddZeroed(119);
		uint8* D = P.GetData() + Off;
		D[0] = (uint8)Slot;
		uint8* Rc = D + 1;
		Rc[0] = 1; AsdaBytes::WrI16(Rc + 1, UserNum); AsdaBytes::WrI32(Rc + 3, CharId); Rc[8] = (uint8)Captain; Rc[9] = (uint8)Grade;
		AsdaBytes::WrI16(Rc + 10, Lv); Rc[12] = 2; Rc[13] = 0; AsdaBytes::WrI16(Rc + 55, Squad);
		FCStringAnsi::Strncpy((char*)Rc + 57, Name, 20);
		FCStringAnsi::Strncpy((char*)Rc + 77, "Leader", 20);
		FCStringAnsi::Strncpy((char*)Rc + 97, "Clan", 20);
	}
	static TArray<uint8> Make6721(int32 In, int32 CharId, int32 Slot, const char* Name)
	{
		TArray<uint8> P; P.SetNumZeroed(26); P[0] = (uint8)In; AsdaBytes::WrI32(P.GetData() + 1, CharId); P[5] = (uint8)Slot;
		FCStringAnsi::Strncpy((char*)P.GetData() + 6, Name, 20);
		return P;
	}
	// Объект точки захвата — рантайм-копия NPC с renderId = номер точки (как у сетевого спавна 4067).
	static AAsdaNpcActor* SpawnPoint(const TSharedRef<FCtx>& C, int32 Id, const FVector& At)
	{
		UWorld* W = C->W.Get();
		if (!W) { return nullptr; }
		AAsdaNpcActor* A = W->SpawnActorDeferred<AAsdaNpcActor>(AAsdaNpcActor::StaticClass(), FTransform(At));
		if (!A) { return nullptr; }
		A->bNetSpawned = true;
		A->NetRenderId = Id;
		A->FinishSpawning(FTransform(At));
		// Модель объекта точки (NPC 188…197 — `mdel_obscuranpotal-a_00`, слоты — MIC мастера `M_AsdaObjectLit`): тон проверяется на
		//   настоящих слотах материала. Масштаб 0.01 — модели конвертера glTF в метрах (как у мобов, `AsdaWorldSubsystem.cpp`).
		if (A->Mesh)
		{
			if (USkeletalMesh* Sk = LoadObject<USkeletalMesh>(nullptr,
				TEXT("/Game/AsdaAssets/Models/NPC/mdel_obscuranpotal-a_00/SkeletalMeshes/mdel_obscuranpotal-a_00.mdel_obscuranpotal-a_00")))
			{
				A->Mesh->SetSkeletalMesh(Sk);
				A->SetActorScale3D(FVector(0.01f));
			}
		}
		C->Spawned.Add(A);
		return A;
	}

	static void Finish(TSharedRef<FCtx> C);

	// Тон на меше объекта точки (`ObjTint` динамических инстансов отдельного мастера `M_AsdaObjectLit_CampTint`) ≈ T.
	static bool MeshTintIs(const TSharedRef<FCtx>& C, int32 Id, const FVector& T, FVector* Out = nullptr, int32* OutSlots = nullptr)
	{
		FVector V = FVector::ZeroVector;
		if (!B(C) || !B(C)->DebugGetMeshTint(Id, V, OutSlots)) { return false; }
		if (Out) { *Out = V; }
		return V.Equals(T, 0.1f);
	}
	// Свой персонаж (тот, кого находит подсистема боя по charId).
	static AAsdaPlayerCharacter* SelfCh(const TSharedRef<FCtx>& C)
	{
		UAsdaWorldSubsystem* WS = Sub<UAsdaWorldSubsystem>(C);
		return WS ? Cast<AAsdaPlayerCharacter>(WS->ActorByCharId(kSelfCharId)) : nullptr;
	}
	// Клип персонажа: имя кончается на `anim_<Idx>`; петля и длина — через выходные параметры.
	static bool ClipIs(const AAsdaPlayerCharacter* Ch, int32 Idx, bool* OutLoop = nullptr, float* OutLen = nullptr)
	{
		const UAnimSingleNodeInstance* SN = (Ch && Ch->BaseSkel) ? Ch->BaseSkel->GetSingleNodeInstance() : nullptr;
		const UAnimSequenceBase* A = SN ? Cast<UAnimSequenceBase>(SN->GetAnimationAsset()) : nullptr;
		if (!A) { return false; }
		if (OutLoop) { *OutLoop = SN->IsLooping(); }
		if (OutLen) { *OutLen = A->GetPlayLength(); }
		return A->GetName().EndsWith(FString::Printf(TEXT("anim_%d"), Idx));
	}
	// Эффект живой и это BP нужного .jeb.
	static bool FxIs(const AActor* A, const TCHAR* Jeb) { return IsValid(A) && A->GetClass()->GetName().Contains(Jeb, ESearchCase::IgnoreCase); }

	// ═══════════════════════════ ПРОГОН ═══════════════════════════
	static void Run(TSharedRef<FCtx> C)
	{
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
		UAsdaCampWarSubsystem* CW = Sub<UAsdaCampWarSubsystem>(C);
		UAsdaCampWarBattleSubsystem* Bt = B(C);
		UAsdaWorldSubsystem* WS = Sub<UAsdaWorldSubsystem>(C);
		UAsdaLoadManagerSubsystem* LM = Sub<UAsdaLoadManagerSubsystem>(C);
		FRun& R = C->R;
		if (!Net || !CW || !Bt || !WS || !LM) { R.Expect(false, TEXT("подсистемы сети/кампвара/боя/мира/загрузки есть")); Finish(C); return; }
		C->SelfScope = MakeShared<FAsdaNetSelfScope>(Net, kCmd);
		C->Keys = MakeShared<FAsdaCVarScope>(C->W.Get(), kCmd);
		C->Keys->Set(TEXT("asda.CampWar.Battle"), 1);
		C->Keys->Set(TEXT("asda.Death.ConfirmGaugeMs"), 0);   // ★ 31.10: полоса вопроса «Желаешь воскреснуть?» не держит кнопку (шаг T)
		C->SavedCamp = WS->GetSelfCampId();
		if (UAsdaChatWindowSubsystem* Chat = Sub<UAsdaChatWindowSubsystem>(C)) { C->SavedChatState = Chat->GetInputState(); }
		C->SavedMap = LM->CurrentMapId;
		Net->SelectedCharId = kSelfCharId;
		Net->SelfUserNum = (uint16)kSelfNum;
		Net->bSelfUserNumKnown = true;
		C->Sent = MakeShared<FSent>();
		TSharedPtr<FSent> S = C->Sent;
		C->CapGen = AsdaSelfTestKit::ArmCapture(Net, [S](uint16 Op, const uint8* Body, int32 Len, bool, TCHAR)
		{
			TArray<uint8> Bd; Bd.Append(Body, Len); S->Add(TPair<int32, TArray<uint8>>((int32)Op, MoveTemp(Bd)));
		}, 0, kCmd);
		if (!C->CapGen) { R.Expect(false, TEXT("перехват отправок взведён")); Finish(C); return; }
		// Существующий код предмета (сумка 6186 и награда 6732 типа 4).
		if (UAsdaItemTableSubsystem* IT = Sub<UAsdaItemTableSubsystem>(C))
		{
			for (int32 Code = 57001; Code < 200000 && !C->ItemCode; ++Code) { if (IT->GetItem(Code)) { C->ItemCode = Code; } }
		}
		CW->ResetSessionState(EAsdaSessionResetReason::EnteringWorld);
		Bt->ResetSessionState(EAsdaSessionResetReason::EnteringWorld);
		Bt->ClearTrace();
		Feed(C, 6713, Make6713(0));   // своя армия 0 (общая точка 6713)
		Feed(C, 6701, Make6701());    // расписание 19:00–19:20
		// A. Вход на поле боя: 5055 с города (карта 3) на карту 19, игрок в отряде (контроль: 5098 должен уйти).
		LM->CurrentMapId = 3;
		if (UAsdaPartySubsystem* Party = Sub<UAsdaPartySubsystem>(C)) { Party->DebugSetPartyState(true, false, 5, 0); }
		CW->OnPortalTransfer5055(/*bHudOn*/ true, /*TargetMapId*/ 19);

		Then(C, 0.4f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			Sub<UAsdaLoadManagerSubsystem>(C)->CurrentMapId = 19;   // загрузка карты 19 (офлайн — номер, как после травела)
			R.Expect(Bt->IsHudOn(), TEXT("A1: 5055 (HUD разрешён) → `campWarWnd_ToggleVisible(1)`: HUD боя включён (+0x2388)"));
			R.Expect(Bt->IsWinShown(kWinPoint) && Bt->IsWinShown(kWinMap) && Bt->IsWinShown(kWinTime), TEXT("A1: окна 49600, 49900, 49940 видны"));
			R.Expect(Bt->IsShown(49500) && Bt->IsShown(49902) && Bt->IsShown(49904) && !Bt->IsShown(49903), TEXT("A1: 49500/49902/49904 видны, 49903 скрыт"));
			R.Expect(!Bt->IsWinShown(kWinHold) && !Bt->IsWinShown(kWinAtk), TEXT("A1: 49510 и 49399 скрыты"));
			TArray<uint8> P;
			R.Expect(CountOp(C, 5098, &P) == 1, TEXT("A2: вход на кампварную карту в отряде → 5098 «выйти из отряда» (a3 = +69 карты-цели)"));
			if (UAsdaChatWindowSubsystem* Chat = Sub<UAsdaChatWindowSubsystem>(C))
			{
				R.Expect(Chat->GetInputState() == 9 && Chat->IsChannelPrefixMode(), TEXT("A3: чат — режим 9 (сквад-чат 6718), флаг +0x6A0"));
			}
			if (UAsdaSystemMenuSubsystem* Sys = Sub<UAsdaSystemMenuSubsystem>(C)) { R.Expect(!Sys->IsExpanded(), TEXT("A3: системное меню спрятано (`CUIWindow_ShowHide(…, 0)`)")); }
			R.Expect(Bt->GetText(49941) == L(C, 12510) && Bt->GetText(49501) == L(C, 10052), TEXT("A4: 49941 = loc 12510, 49501 = loc 10052"));
			R.Expect(Bt->GetVisibleDigit(0, 0) == 0 && Bt->GetVisibleDigit(0, 5) == 0, TEXT("A4: 49600 — 6 разрядов «000000» (ведущие нули)"));
			R.Expect(Bt->IsShown(49721) && Bt->IsShown(49730) && !Bt->IsShown(49731), TEXT("A4: 3Point_m1 виден, BB виден, BB2 скрыт (рантайм эталона)"));
			// ★ 31.10.2026 (Б, решение владельца): на поле битвы (карта 19) в воду не входить — маска `SwimBits` карты пуста, бывшие
			//   открытые тайлы воды 28.10 снова блок при `asda.Swim.OpenWater 1`; контроль — у alpen открытая вода осталась.
			{
				const UAsdaWalkGrid* Bf = LoadObject<UAsdaWalkGrid>(nullptr, TEXT("/Game/Map/battlefield/DA_WalkGrid.DA_WalkGrid"));
				const UAsdaWalkGrid* Al = LoadObject<UAsdaWalkGrid>(nullptr, TEXT("/Game/Map/alpen/DA_WalkGrid.DA_WalkGrid"));
				bool bClosed = Bf && Bf->SwimBits.Num() == 0;
				static const FIntPoint kWas[] = { FIntPoint(427, 261), FIntPoint(155, 190), FIntPoint(187, 357) };   // открыты 28.10
				for (const FIntPoint& T : kWas) { bClosed = bClosed && !Bf->IsTileSwimOpen(T.X, T.Y) && !Bf->IsTileWalkable(T.X, T.Y); }
				R.Expect(bClosed, FString::Printf(TEXT("A5: вода поля битвы закрыта — SwimBits %d б, тайлы воды 28.10 (427,261)/(155,190)/(187,357) — блок"),
					Bf ? Bf->SwimBits.Num() : -1));
				R.Expect(Al && Al->SwimBits.Num() > 0, TEXT("A5: контроль — у alpen открытая вода осталась (SwimBits не пуст)"));
			}
			if (UAsdaPartySubsystem* Party = Sub<UAsdaPartySubsystem>(C)) { Party->DebugSetPartyState(false, false, -1, 0); }
			// B. Точки: 6726 ×10 — 0: армия 0 state 2 бонус 10; 1: армия 1 state 1; 6: армия 1 state 2; прочие нейтральны.
			for (int32 I = 0; I < 10; ++I)
			{
				const int32 Owner = (I == 0) ? 0 : (I == 1 || I == 6) ? 1 : -1;
				const int32 St = (I == 0 || I == 6) ? 2 : (I == 1) ? 1 : 0;
				Feed(C, 6726, Make6726(I, Owner, St, (I == 0) ? 10 : 0));
			}
			Feed(C, 6726, Make6726(12, 0, 0, 0));   // контроль: номер ≥ 10 — выход
			// Объекты точек 3 и 4 рядом со своим персонажем.
			AActor* Self = Sub<UAsdaWorldSubsystem>(C)->LocalPlayerActor.Get();
			const FVector At = Self ? Self->GetActorLocation() : FVector::ZeroVector;
			// ★ 31.10: у точек настоящая модель (крупная) — дальше от персонажа, чтобы поза захвата на снимке не закрывалась.
			SpawnPoint(C, 3, At + FVector(1200, 0, 0));
			SpawnPoint(C, 4, At + FVector(1200, 900, 0));
			Shot(C, TEXT("campwar_battle_hud"));
			{
				const FUiScan Sc = ScanUi(C, TEXT("campwar_battle_hud"));
				C->R.Expect(Sc.Cells > 0 && Sc.CenterBlockedCw == 0 && Sc.CwCells <= 90, FString::Printf(TEXT("ЭКРАН (campwar_battle_hud): над персонажем (верх-центр 12×11) окна боя клик не глотают — закрыто ими %d (прочими окнами HUD %d); окна/зоны боя занимают %d из %d ячеек (только свои прямоугольники) [ИСПРАВЛЕНИЕ]"), Sc.CenterBlockedCw, Sc.CenterBlocked - Sc.CenterBlockedCw, Sc.CwCells, Sc.Cells));
			}
		});
		Then(C, 0.4f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			R.Expect(Bt->GetSlotCount() == 10, FString::Printf(TEXT("B1: 6726 ×10 → точек %d (ждём 10; повтор/≥10 не раздувают)"), Bt->GetSlotCount()));
			const FAsdaCwPointSlot& P0 = Bt->GetSlot(0);
			R.Expect(P0.Id == 0 && P0.State == 2 && P0.Owner == 0 && P0.Raw24 == 10 && P0.CellX == 10 && P0.CellY == 20,
				TEXT("B1: слот 0 — state 2, армия 0, бонус 10, клетка (10,20)"));
			R.Expect(P0.TimerMs >= 60000 && P0.TimerMs < 61000, FString::Printf(TEXT("B1: таймер точки 0 = бонус·6000 (+dt) = %d мс"), P0.TimerMs));
			FVector2D UV;
			R.Expect(Bt->GetMapIconCell(0, UV) == 2 && FMath::IsNearlyEqual(UV.X, 0.25f) && FMath::IsNearlyZero(UV.Y), TEXT("B2: значок точки 0 — ячейка (0.25, 0)"));
			R.Expect(Bt->GetMapIconCell(1, UV) == 1 && FMath::IsNearlyZero(UV.X) && FMath::IsNearlyEqual(UV.Y, 0.25f), TEXT("B2: значок точки 1 — ячейка (0, 32·1/128)"));
			R.Expect(Bt->GetMapIconCell(5, UV) == 0 && FMath::IsNearlyEqual(UV.Y, 0.75f), TEXT("B2: нейтральная точка 5 — ячейка (0, 0.75)"));
			R.Expect(Bt->IsMapLayerDrawn(), TEXT("B3: слой рисунка карты 49900 рисуется (HUD + 49904)"));
			R.Expect(Bt->IsShown(TimerMainKey(0)) && Bt->IsShown(TimerMainKey(1)) && Bt->IsShown(TimerMainKey(6)) && !Bt->IsShown(TimerMainKey(5)),
				TEXT("B4: таймеры — 0 (дом. Света), 1 (state 1), 6 (дом. Тьмы) видны; 5 скрыт"));
			const UImage* G2 = Cast<UImage>(Bt->GetNode(TimerGlyphKey(0, 2)));
			const UObject* Res = G2 ? G2->GetBrush().GetResourceObject() : nullptr;
			R.Expect(Res && Res->GetName().Contains(TEXT("greenbufftime"), ESearchCase::IgnoreCase), TEXT("B4: таймер точки 0 (state 2) — GreenBufftime.tex"));
			// C. Свёртка карты кликами 49902 / 49903.
			ClickNode(C, Bt->GetNode(49902), TEXT("49902 свернуть карту"));
		});
		Then(C, 0.4f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			C->R.Expect(!Bt->IsShown(49902) && Bt->IsShown(49903) && !Bt->IsShown(49904) && !Bt->IsMapLayerDrawn() && !Bt->IsShown(TimerMainKey(0)),
				TEXT("C1: 49902 → карта свёрнута (49903 виден, 49904 скрыт, рисунка и таймеров нет)"));
			ClickNode(C, Bt->GetNode(49903), TEXT("49903 развернуть карту"));
		});
		Then(C, 0.4f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			R.Expect(Bt->IsShown(49902) && !Bt->IsShown(49903) && Bt->IsMapLayerDrawn(), TEXT("C2: 49903 → карта развёрнута"));
			// D. Захват точки 3 своим персонажем: 6724 sub 1.
			Feed(C, 6724, Make6724(3, 1, kSelfCharId));
			Feed(C, 6724, Make6724(4, 1, 99999999));   // контроль: захватчика нет в мире — ничего
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			R.Expect(Bt->GetMySlot() == 3 && Bt->GetSlot(3).Capturer == 0, TEXT("D1: 6724 sub 1 (я) → +0x23D4 = 3, захватчик — армия 0"));
			R.Expect(Bt->GetSlot(3).Anim == 1 && Bt->IsWinShown(kWinHold), TEXT("D1: тик: шаг 3 → 1, полоса 49510 видна (моя)"));
			R.Expect(Bt->GetSlot(4).Anim == 0 && Bt->GetSlot(4).Capturer == -1, TEXT("D1: контроль — чужой без персонажа: точка 4 не тронута"));
			R.Expect(Bt->GetText(49522) == L(C, 10007), TEXT("D1: подпись 49522 = loc 10007 «Идет захват...»"));
			R.Expect(FxIs(Bt->GetHandFx(kSelfCharId), TEXT("camp_hand_01")), TEXT("D1: эффект Camp_hand_01.jeb создан у захватчика (char+0x33080)"));
			{
				AAsdaPlayerCharacter* Me = SelfCh(C);
				bool bLoop = false; float Len = 0.f;
				const bool bClip = ClipIs(Me, AAsdaPlayerCharacter::kClipOccupy, &bLoop, &Len);
				R.Expect(Me && Me->GetCampPoseState() == 47 && Me->IsEtalonActionBusy() && bClip && bLoop && FMath::IsNearlyEqual(Len, 1.f, 0.05f),
					FString::Printf(TEXT("D1: поза 47 — клип anim_98 (occupa) в цикле, %.3f с ≈ 1.000, «занят» (состояние %d, клип '%s')"),
						Len, Me ? Me->GetBusyActionCode() : -1, Me ? *Me->GetPlayingClipName() : TEXT("—")));
			}
			Bt->DebugAdvanceTick(5000.f);
			const float V = Bt->GetBarValue(49521);
			R.Expect(V >= 5000.f && V < 6000.f, FString::Printf(TEXT("D2: +5000 мс — полоса %.0f мс (0…10000)"), V));
			const FVector Tn = Bt->GetObjTint(3);
			R.Expect(FMath::IsNearlyEqual(Tn.X, 0.5f, 0.1f) && FMath::IsNearlyEqual(Tn.Y, 0.5f, 0.1f) && FMath::IsNearlyEqual(Tn.Z, 0.5f, 0.1f),
				FString::Printf(TEXT("D2: тон объекта тускнеет к армии 0 (%.2f, %.2f, %.2f) ≈ (0.5, 0.5, 0.5)"), Tn.X, Tn.Y, Tn.Z));
			FVector Mt = FVector::ZeroVector;
			int32 NSlots = 0;
			const bool bMt = MeshTintIs(C, 3, Tn, &Mt, &NSlots);   // до Printf: порядок вычисления аргументов не задан
			R.Expect(bMt, FString::Printf(TEXT("D2: тон лёг на меш точки — ObjTint мастера тона (%.2f, %.2f, %.2f), слотов %d"),
				Mt.X, Mt.Y, Mt.Z, NSlots));
			{
				// Динамические инстансы мастера тона унесли текстуры исходных MIC (Tex / TexN NormalStudio) — объект не «плоский».
				AAsdaNpcActor* Pa = Bt->DebugFindPointActor(3);
				int32 NTex = 0, NAll = 0;
				if (Pa && Pa->Mesh)
				{
					for (int32 I = 0; I < Pa->Mesh->GetNumMaterials(); ++I)
					{
						UMaterialInterface* Mi = Pa->Mesh->GetMaterial(I);
						if (!Mi) { continue; }
						++NAll;
						UTexture* T = nullptr;
						if (Mi->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("Tex")), T) && T) { ++NTex; }
					}
				}
				R.Expect(NAll == 3 && NTex == NAll && NSlots == NAll,
					FString::Printf(TEXT("D2: у всех слотов точки свой Tex после замены на мастер тона (%d из %d, с тоном %d)"), NTex, NAll, NSlots));
			}
			Shot(C, TEXT("campwar_battle_capture"));
			{
				const FUiScan Sc = ScanUi(C, TEXT("campwar_battle_capture"));
				C->R.Expect(Sc.Cells > 0 && Sc.CenterBlockedCw == 0 && Sc.CwCells <= 90, FString::Printf(TEXT("ЭКРАН (campwar_battle_capture): над персонажем (верх-центр 12×11) окна боя клик не глотают — закрыто ими %d (прочими окнами HUD %d); окна/зоны боя занимают %d из %d ячеек (только свои прямоугольники) [ИСПРАВЛЕНИЕ]"), Sc.CenterBlockedCw, Sc.CenterBlocked - Sc.CenterBlockedCw, Sc.CwCells, Sc.Cells));
			}
			// sub 0 — прерван.
			Feed(C, 6724, Make6724(3, 0, kSelfCharId));
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			R.Expect(Bt->GetMySlot() == -1 && !Bt->IsWinShown(kWinHold) && Bt->GetSlot(3).Anim == 0, TEXT("D3: 6724 sub 0 → +0x23D4 = −1, 49510 скрыт, шаг 2 → 0"));
			R.Expect(!Bt->GetHandFx(kSelfCharId), TEXT("D3: эффект Camp_hand_01 погашен (`CEffectMgr_StopByOwner`)"));
			R.Expect(MeshTintIs(C, 3, FVector::OneVector), TEXT("D3: тон точки (нейтральная) — белый, на меше (1, 1, 1)"));
			{
				AAsdaPlayerCharacter* Me = SelfCh(C);
				R.Expect(Me && Me->GetCampPoseState() == 0 && !Me->IsEtalonActionBusy() && !ClipIs(Me, AAsdaPlayerCharacter::kClipOccupy),
					FString::Printf(TEXT("D3: 6724 sub 0 при состоянии 47 → покой, поза снята (клип '%s')"), Me ? *Me->GetPlayingClipName() : TEXT("—")));
			}
			// Захват до конца по времени.
			Feed(C, 6724, Make6724(3, 1, kSelfCharId));
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			Bt->DebugAdvanceTick(10001.f);
			C->R.Expect(Bt->GetSlot(3).Anim == 0 && Bt->GetMySlot() == -1 && !Bt->IsWinShown(kWinHold), TEXT("D4: ≥ 10000 мс — захват окончен (шаг 0, полоса скрыта)"));
			// Поза 47 не снимается концом таймера захвата — только 6724 sub 0 или другой записью состояния (BATTLE_GAPS_RE §2.4).
			AAsdaPlayerCharacter* Me = SelfCh(C);
			C->R.Expect(Me && Me->GetCampPoseState() == 47 && ClipIs(Me, AAsdaPlayerCharacter::kClipOccupy),
				TEXT("D4: конец таймера захвата позу 47 не снимает (перехода 47 → 2 у эталона нет)"));
			// Жест 51 (та же точка порта, что у 4304 / 4025 121 / спауна 121) перебивает 47: клип anim_99 один раз, затем покой.
			const float L51 = Me ? Me->EnterCampPose(51, TEXT("самотест боя: жест 51")) : 0.f;
			bool bLoop = true; float Len = 0.f;
			const bool bClip = ClipIs(Me, AAsdaPlayerCharacter::kClipLoser, &bLoop, &Len);
			C->R.Expect(Me && Me->GetCampPoseState() == 51 && bClip && !bLoop && FMath::IsNearlyEqual(L51, 4.267f, 0.05f),
				FString::Printf(TEXT("D4a: состояние 51 перебивает 47 — клип anim_99 (loser) один раз, %.3f с ≈ 4.267"), L51));
		});
		Then(C, 4.6f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			AAsdaPlayerCharacter* Me = SelfCh(C);
			C->R.Expect(Me && Me->GetCampPoseState() == 0 && !Me->IsEtalonActionBusy() && !ClipIs(Me, AAsdaPlayerCharacter::kClipLoser),
				FString::Printf(TEXT("D4b: конец клипа 51 → состояние 0, покой (клип '%s')"), Me ? *Me->GetPlayingClipName() : TEXT("—")));
			Bt->ClearTrace();
			Feed(C, 6724, Make6724(3, 3, kSelfCharId));
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			R.Expect(Bt->GetNoticeTrace().Contains(10182) && Bt->GetSlot(3).Anim == 0, TEXT("D5: 6724 sub 3 → окно 10182 «Позиция уже занята.», шаг 2 → 0"));
			Dismiss(C);
			// E. 6725: точка 3 → армия 0 state 1, затем state 2 бонус 20; точки 42 нет — пропуск.
			TArray<uint8> P = Make6726(3, 0, 1, 0);
			Feed(C, 6725, P);
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			R.Expect(Bt->GetSlot(3).State == 1 && Bt->GetSlot(3).Owner == 0 && Bt->GetSlot(3).TimerMs < 1000, TEXT("E1: 6725 state 1 → «временно», таймер с 0"));
			R.Expect(Bt->IsShown(TimerMainKey(3)), TEXT("E1: таймер точки 3 (state 1) виден"));
			R.Expect(Bt->GetSlot(3).LightKind == 1 && FxIs(Bt->GetSlot(3).Light.Get(), TEXT("camp_light_01")), TEXT("E1: свет точки Camp_Light_01.jeb (армия 0, state 1) создан"));
			FVector Mt = FVector::ZeroVector;
			const bool bMt = MeshTintIs(C, 3, FVector(0, 0, 0.5f), &Mt);   // до Printf: порядок вычисления аргументов не задан
			R.Expect(bMt, FString::Printf(TEXT("E1: тон точки — половинный цвет армии 0 на меше (%.2f, %.2f, %.2f)"), Mt.X, Mt.Y, Mt.Z));
			Shot(C, TEXT("campwar_battle_light"));
			Feed(C, 6725, Make6726(3, 0, 2, 20));
			Feed(C, 6725, Make6726(42, 1, 2, 0));
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			R.Expect(Bt->GetSlot(3).State == 2 && Bt->GetSlot(3).TimerMs >= 120000, FString::Printf(TEXT("E2: 6725 state 2 → таймер = 20·6000 (%d мс)"), Bt->GetSlot(3).TimerMs));
			R.Expect(!Bt->IsShown(TimerMainKey(3)), TEXT("E2: state 2 у не-«домашней» точки 3 — таймер скрыт"));
			R.Expect(Bt->GetSlot(3).LightKind == 2 && FxIs(Bt->GetSlot(3).Light.Get(), TEXT("camp_light_02")), TEXT("E2: свет сменился на Camp_Light_02.jeb (state 2)"));
			R.Expect(MeshTintIs(C, 3, FVector(0, 0, 1.f)), TEXT("E2: тон точки — полный цвет армии 0 на меше (0, 0, 1)"));
			R.Expect(Bt->GetSlotCount() == 10, TEXT("E3: контроль — 6725 по точке 42 пропущен"));
			// F. 6728 коды 0…6 (чат 10171+код), код 11 — вне диапазона.
			Bt->ClearTrace();
			for (int32 Code = 0; Code <= 6; ++Code) { Feed(C, 6728, Make6728(Code, 4)); }
			Feed(C, 6728, Make6728(11, 0));
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			const TArray<int32> Want = { 10171, 10172, 10173, 10174, 10175, 10176, 10177 };
			R.Expect(Bt->GetNoticeTrace() == Want, FString::Printf(TEXT("F1: 6728 коды 0…6 → чат 10171…10177 (%d строк), код 11 — молча"), Bt->GetNoticeTrace().Num()));
			const FString Want0 = L(C, 10177).Replace(TEXT("[sd1]"), TEXT("5"));
			R.Expect(Bt->GetLastNoticeText() == Want0, FString::Printf(TEXT("F1: [sd1] = N + 1: «%s»"), *Bt->GetLastNoticeText()));
			FColor Col;
			if (UAsdaChatSubsystem* Chat = Sub<UAsdaChatSubsystem>(C))
			{
				R.Expect(Chat->DebugFindLastSystemLineColor(Want0, Col) && Col == Argb(0xFFFF0080u), TEXT("F1: цвет строки 0xFFFF0080"));
			}
			// Код 7 N = 3 — «START 3 min».
			Feed(C, 6728, Make6728(7, 3));
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			R.Expect(Bt->IsWinShown(kWinFont) && Bt->IsShown(49913) && Bt->IsShown(49920) && Bt->IsShown(49917) && !Bt->IsShown(49911) && !Bt->IsShown(49916),
				TEXT("F2: код 7 N 3 → 49910: 49913 + 49920 + цифра 49917 (3)"));
			Shot(C, TEXT("campwar_battle_start3")); ScanUi(C, TEXT("campwar_battle_start3"));
			Bt->DebugAdvanceTick(1500.f);
			const float A = Bt->GetNodeAlpha(49913);
			R.Expect(A > 0.3f && A < 0.6f, FString::Printf(TEXT("F2: затухание 3000 мс — на 1500 мс альфа %.2f"), A));
			Bt->DebugAdvanceTick(1600.f);
			R.Expect(!Bt->IsShown(49913) && !Bt->IsShown(49920) && !Bt->IsShown(49917) && FMath::IsNearlyEqual(Bt->GetNodeAlpha(49913), 1.f),
				TEXT("F2: событие 0x18 по 49913 → 49913/49920/цифры спрятаны, альфа 255"));
			Feed(C, 6728, Make6728(8, 0));
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			R.Expect(Bt->IsShown(49911) && Bt->IsShown(49912), TEXT("F3: код 8 → «CampWar START» (49911 + 49912)"));
			R.Expect(Bt->GetTimerMin() == 15 && Bt->GetTimerSec() == 0, FString::Printf(TEXT("F3: код 8 → таймер (20−0) − 5 = 15:00 (%d:%d)"), Bt->GetTimerMin(), Bt->GetTimerSec()));
			Bt->DebugAdvanceTick(3001.f);
			R.Expect(!Bt->IsWinShown(kWinFont), TEXT("F3: конец затухания 49911 → окно 49910 спрятано"));
			Feed(C, 6728, Make6728(9, 2));
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			R.Expect(Bt->IsShown(49921) && Bt->IsShown(49920) && Bt->IsShown(49916) && Bt->GetTimerMin() == 2, TEXT("F4: код 9 N 2 → «End 2 min» + таймер 2:00"));
			Feed(C, 6728, Make6728(10, 0));
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			C->R.Expect(Bt->IsWinShown(kWinFont) && !Bt->IsShown(49921) && !Bt->IsShown(49911) && !Bt->IsShown(49913), TEXT("F5: код 10 → только корень 49910"));
			C->R.Expect(FxIs(Bt->GetStartFx(), TEXT("camp_start")), TEXT("F5: код 10 → Camp_start.jeb на точке возрождения армии 0 (+0x23E0)"));
			Shot(C, TEXT("campwar_battle_fx")); ScanUi(C, TEXT("campwar_battle_fx"));
			// G. Очки 6729, численность 6742.
			Feed(C, 6729, Make3I32(1234, 56, 999999));
			Feed(C, 6742, Make3I16(12, 7, 30));
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			const int32 W0[6] = { 0, 0, 1, 2, 3, 4 };
			bool bOk = true;
			for (int32 J = 0; J < 6; ++J) { bOk = bOk && Bt->GetVisibleDigit(0, J) == W0[J]; }
			R.Expect(Bt->GetScore(0) == 1234 && bOk, TEXT("G1: 6729 → армия 0 «001234» (6 цифр, ведущие нули)"));
			// G1b. Наборы цифр армий 1/2 (копии `countpack` в ассете) — «000056», «999999».
			const int32 W1[6] = { 0, 0, 0, 0, 5, 6 };
			bool b1 = true, b2 = true;
			for (int32 J = 0; J < 6; ++J) { b1 = b1 && Bt->GetVisibleDigit(1, J) == W1[J]; b2 = b2 && Bt->GetVisibleDigit(2, J) == 9; }
			R.Expect(b1 && b2, TEXT("G1: армии 1/2 — «000056» и «999999» в своих наборах цифр"));
			// G1c. Живая геометрия трёх наборов (путь игрока, `GetCachedGeometry`): видимы, не пересекаются, стоят у своих знаков.
			FVector2D Mn[3], Mx[3];
			bool bGeo = true;
			for (int32 F = 0; F < 3; ++F)
			{
				bool bAny = false;
				Mn[F] = FVector2D(FLT_MAX, FLT_MAX); Mx[F] = FVector2D(-FLT_MAX, -FLT_MAX);
				for (int32 J = 0; J < 6; ++J)
				{
					FVector2D A, Bv;
					if (UWidget* G = Bt->GetNode(DigitPosKey(F, J)); G && AsdaUi::IsShown(G) && AsdaUi::WidgetAbsRect(G, A, Bv))
					{
						bAny = true; Mn[F] = FVector2D::Min(Mn[F], A); Mx[F] = FVector2D::Max(Mx[F], Bv);
					}
				}
				FVector2D MA = FVector2D::ZeroVector, MB = FVector2D::ZeroVector;   // ★ R5-fix-b 13.11.2026: ноль — C4701 (на ветке без присвоения значение уходит в текст проверки)
				UWidget* Mark = Bt->GetNode(49601 + F);
				const bool bMark = Mark && AsdaUi::WidgetAbsRect(Mark, MA, MB);
				const float Cy = (Mn[F].Y + Mx[F].Y) * 0.5f, MCy = (MA.Y + MB.Y) * 0.5f;
				const float Scale = bMark ? FMath::Max(0.01f, (MB.Y - MA.Y) / 32.f) : 1.f;
				const bool bNear = bAny && bMark && Mn[F].X >= MB.X - 4.f * Scale && FMath::Abs(Cy - MCy) < 12.f * Scale;
				R.Expect(bAny && bNear, FString::Printf(TEXT("G1c: армия %d — набор цифр виден (%.0f,%.0f)-(%.0f,%.0f), справа от знака (%.0f,%.0f)-(%.0f,%.0f)"),
					F, Mn[F].X, Mn[F].Y, Mx[F].X, Mx[F].Y, MA.X, MA.Y, MB.X, MB.Y));
				bGeo = bGeo && bAny;
			}
			auto Cross = [&](int32 I, int32 J) { return Mn[I].X < Mx[J].X && Mn[J].X < Mx[I].X && Mn[I].Y < Mx[J].Y && Mn[J].Y < Mx[I].Y; };
			R.Expect(bGeo && !Cross(0, 1) && !Cross(0, 2) && !Cross(1, 2), TEXT("G1c: три набора цифр не пересекаются"));
			R.Expect(Bt->GetText(49722) == TEXT("12/30") && Bt->GetText(49724) == TEXT("7/30") && Bt->GetText(49726) == TEXT("30/30"),
				FString::Printf(TEXT("G2: 6742 → «12/30» «7/30» «30/30» («%s»)"), *Bt->GetText(49722)));
			Shot(C, TEXT("campwar_battle_points"));
			{
				const FUiScan Sc = ScanUi(C, TEXT("campwar_battle_points"));
				C->R.Expect(Sc.Cells > 0 && Sc.CenterBlockedCw == 0 && Sc.CwCells <= 90, FString::Printf(TEXT("ЭКРАН (campwar_battle_points): над персонажем (верх-центр 12×11) окна боя клик не глотают — закрыто ими %d (прочими окнами HUD %d); окна/зоны боя занимают %d из %d ячеек (только свои прямоугольники) [ИСПРАВЛЕНИЕ]"), Sc.CenterBlockedCw, Sc.CenterBlocked - Sc.CenterBlockedCw, Sc.CwCells, Sc.Cells));
			}
			// H. 6736: сервер 19:05:30, конец 19:20 → 14:30 → сразу шаг → 14:29.
			TArray<uint8> P = Make3I16(19, 5, 30);
			Feed(C, 6736, P);
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			R.Expect(Sub<UAsdaCampWarSubsystem>(C)->IsBattleTimerOn(), TEXT("H1: 6736 → +0x2334 = 1"));
			const FString T = Bt->GetText(49942);
			R.Expect(T == TEXT("14:29") || T == TEXT("14:28"), FString::Printf(TEXT("H1: 49942 «%s» (ждём 14:29 сразу, тик раз в 1 с)"), *T));
		});
		Then(C, 1.4f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			const FString T = Bt->GetText(49942);
			R.Expect(T == TEXT("14:28") || T == TEXT("14:27"), FString::Printf(TEXT("H2: таймер боя идёт раз в секунду («%s»)"), *T));
			// I. 6730: убийца — я (+25), затем чужой — контроль.
			Bt->ClearTrace();
			C->Live0 = Sub<UAsdaFloatingTextSubsystem>(C) ? Sub<UAsdaFloatingTextSubsystem>(C)->DebugLiveCount() : 0;
			TArray<uint8> P; P.SetNumZeroed(10); AsdaBytes::WrI16(P.GetData(), kSelfNum); AsdaBytes::WrI32(P.GetData() + 2, kSelfCharId); AsdaBytes::WrI32(P.GetData() + 6, 25);
			Feed(C, 6730, P);
			TArray<uint8> P2 = P; AsdaBytes::WrI16(P2.GetData(), kSelfNum + 1);
			Feed(C, 6730, P2);
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			const FString Want = AsdaUi::FormatPrintfArgs(L(C, 108118), { TEXT("25") });
			R.Expect(Bt->GetNoticeTrace().Num() == 1 && Bt->GetLastNoticeText() == Want, FString::Printf(TEXT("I1: 6730 (я) → чат «%s»; чужой — молча"), *Bt->GetLastNoticeText()));
			const int32 Live1 = Sub<UAsdaFloatingTextSubsystem>(C) ? Sub<UAsdaFloatingTextSubsystem>(C)->DebugLiveCount() : 0;
			R.Expect(Live1 >= C->Live0 + 2, FString::Printf(TEXT("I1: всплыли «25» и «POINT» (надписей %d → %d)"), C->Live0, Live1));
			// J. Итог 6732: победитель 0 (моя армия).
			Feed(C, 6732, Make6732(0, 3, C->ItemCode));
		});
		Then(C, 0.7f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			R.Expect(Bt->IsWinShown(kWinEnd), TEXT("J1: 6732 → окно итога 49800"));
			R.Expect(Bt->GetText(49801) == L(C, 10121) && Bt->GetText(49805) == L(C, 10191) && Bt->GetText(49806) == L(C, 10122),
				TEXT("J1: 49801 = 10121, 49805 = 10191 (армия 0), 49806 = 10122 «Поздравляем…» (победитель = моя армия)"));
			R.Expect(Bt->GetText(49812) == TEXT("66.67%") && Bt->GetText(49813) == TEXT("200") && Bt->GetText(49822) == TEXT("33.33%"),
				FString::Printf(TEXT("J1: строки — «%s» «%s» / «%s»"), *Bt->GetText(49812), *Bt->GetText(49813), *Bt->GetText(49822)));
			const float Fr = Bt->GetBarValue(49811), Bk = Bt->GetBarValue(49814);
			R.Expect(FMath::IsNearlyEqual(Fr, 33.33f, 0.5f) && FMath::IsNearlyEqual(Bk, 66.67f, 0.5f), FString::Printf(TEXT("J1: полосы A %.1f%% / A+B %.1f%%"), Fr, Bk));
			R.Expect(Bt->GetText(49842) == TEXT("777") && Bt->GetText(49844) == TEXT("12"), TEXT("J1: слава 777, монеты 12"));
			R.Expect(Bt->GetText(49851) == FString::Printf(TEXT("5000 %s"), *L(C, 6010)), FString::Printf(TEXT("J1: награда 0 — «%s»"), *Bt->GetText(49851)));
			const UAsdaItemTableSubsystem* IT = Sub<UAsdaItemTableSubsystem>(C);
			const bool bHas20551 = IT && IT->GetItem(20551);
			R.Expect(bHas20551 ? Bt->GetText(49861) == FString::Printf(TEXT("3 %s"), *IT->GetItemName(20551)) : Bt->GetText(49861).IsEmpty(),
				FString::Printf(TEXT("J1: награда 1 (предмет 20551 %s) — «%s»"), bHas20551 ? TEXT("есть") : TEXT("нет в таблице"), *Bt->GetText(49861)));
			R.Expect(!C->ItemCode || (!Bt->GetText(49871).IsEmpty() && Bt->GetText(49870 + kAlt) == TEXT("2")), FString::Printf(TEXT("J1: награда 2 (тип 4) — «%s», ×«%s»"),
				*Bt->GetText(49871), *Bt->GetText(49870 + kAlt)));
			R.Expect(!Bt->IsShown(49882), TEXT("J1: 49882 «Выйти из боя» спрятана до 6733"));
			Shot(C, TEXT("campwar_battle_end")); ScanUi(C, TEXT("campwar_battle_end"));
			if (UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(C)) { C->SavedGold = Inv->GetGold(); }
			TArray<uint8> P; P.SetNumZeroed(235); P[0] = 1; AsdaBytes::WrI16(P.GetData() + 1, 1234); AsdaBytes::WrI32(P.GetData() + 3, 54321);
			Feed(C, 6733, P);
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			R.Expect(Bt->IsShown(49882), TEXT("J2: 6733 → 49882 видна"));
			if (UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(C)) { R.Expect(Inv->GetGold() == 54321, TEXT("J2: 6733 [0]==1 → золото 54321")); }
			Feed(C, 6732, Make6732(-1, 0, 0));
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			R.Expect(Bt->GetText(49806) == L(C, 10129) && Bt->GetText(49812) == TEXT("0.00%") && Bt->IsShown(49882), TEXT("J3: победитель −1 → `Reset`: 10129, 0.00%, 49882 видна"));
			Feed(C, 6732, Make6732(1, 3, C->ItemCode));
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			C->R.Expect(Bt->GetText(49806) == L(C, 10123) && !Bt->IsShown(49882), TEXT("J4: победитель 1 (не моя армия) → 10123, 49882 спрятана"));
			// «Выйти из боя» (49880 внутри 49882) появляется только после 6733 — как у эталона.
			TArray<uint8> P; P.SetNumZeroed(235);
			Feed(C, 6733, P);
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			C->R.Expect(Bt->IsShown(49882) && Bt->IsShown(49880), TEXT("J4: 6733 ([0] ≠ 1) → 49882 видна, золото не тронуто"));
			ClickNode(C, Bt->GetNode(49880), TEXT("49880 «Выйти из боя» (итог)"));
		});
		Then(C, 0.4f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			TArray<uint8> P;
			R.Expect(!Bt->IsWinShown(kWinEnd) && CountOp(C, 6731, &P) == 1 && P.Num() == 8 && AsdaBytes::RdI32(P.GetData()) == kSelfCharId
				&& AsdaBytes::RdI16(P.GetData() + 4) == 0, TEXT("J5: 49880 → 49800 скрыт, на карте 19 — 6731 (charId, армия 0)"));
			// 6731 сбросил модель — HUD остаётся, но очки обнулены (`CCampWar_ResetState`).
			R.Expect(Bt->IsHudOn() && Bt->GetScore(0) == 0 && Bt->GetSlotCount() == 0, TEXT("J5: 6731 → ResetState (очки 0, точек 0), HUD не тронут"));
			// K. «Рейд»: 49500 → 6717.
			ClickNode(C, Bt->GetNode(49500 + kAlt), TEXT("49500 «Рейд»"));
		});
		Then(C, 0.4f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			TArray<uint8> P;
			R.Expect(CountOp(C, 6717, &P) == 1 && P.Num() == 6 && AsdaBytes::RdI32(P.GetData()) == kSelfCharId && AsdaBytes::RdI16(P.GetData() + 4) == 0
				&& Bt->IsWaitingList(), TEXT("K1: 49500 → 6717 (charId, армия 0), +0x235A = 1"));
			TArray<uint8> L6715;
			AddMember(L6715, 0, kSelfNum, kSelfCharId, 3, 2, 45, -1, "SelfName");
			AddMember(L6715, 1, 88, kMateCharId, 0, 0, 33, 4, "Mate");
			AddMember(L6715, 2, 89, kMateCharId + 1, 3, 9, 50, 2, "Third");
			Feed(C, 6715, L6715);
			Feed(C, 6716, TArray<uint8>());
		});
		Then(C, 0.5f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			R.Expect(Bt->GetMyIndex() == 0 && !Bt->IsWaitingList(), TEXT("K2: 6715 → своя запись: +0xB7E = 0; 6716 → флаг снят"));
			R.Expect(Bt->IsWinShown(kWinAtk) && Bt->GetText(49000) == L(C, 10051) && Bt->GetText(49010) == L(C, 10052), TEXT("K2: 6716 → 49399 «Список групп», шапка 10052…"));
			R.Expect(Bt->GetText(RowKey(1, 2)) == TEXT("Mate") && Bt->GetText(RowKey(1, 4)) == TEXT("33") && Bt->GetText(RowKey(1, 8)) == L(C, 10064)
				&& Bt->GetText(RowKey(0, 8)) == L(C, 10065) && Bt->GetText(RowKey(1, 5)) == L(C, 12558) && Bt->GetText(RowKey(3, 2)).IsEmpty(),
				FString::Printf(TEXT("K2: строки — «%s» «%s» «%s» / «%s» / «%s» / «%s»"), *Bt->GetText(RowKey(1, 2)), *Bt->GetText(RowKey(1, 4)),
					*Bt->GetText(RowKey(1, 8)), *Bt->GetText(RowKey(0, 8)), *Bt->GetText(RowKey(1, 5)), *Bt->GetText(RowKey(3, 2))));
			Shot(C, TEXT("campwar_battle_raid")); ScanUi(C, TEXT("campwar_battle_raid"));
			ClickNode(C, Bt->GetNode(RowKey(0, 0)), TEXT("строка 0 (своя)"));
		});
		Then(C, 0.4f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			C->R.Expect(Bt->GetListSel() == -1, TEXT("K3: своя строка не выбирается (контроль)"));
			ClickNode(C, Bt->GetNode(RowKey(1, 0)), TEXT("строка 1"));
		});
		Then(C, 0.4f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			C->R.Expect(Bt->GetListSel() == 1 && Bt->IsShown(FrameKey(0)), TEXT("K3: ЛКМ по строке 1 → +0x2318 = 1, рамка 49330"));
			ClickNode(C, Bt->GetNode(RowKey(1, 0)), TEXT("строка 1 ПКМ"), /*bRight*/ true);
		});
		Then(C, 0.4f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			R.Expect(Bt->IsWinShown(kWinPop) && Bt->GetText(49410 + kAlt) == L(C, 10061) && Bt->GetText(49430 + kAlt) == L(C, 111930) && Bt->IsShown(49430),
				TEXT("K4: ПКМ → попап 49400 у курсора; 49430 «Исключить» (111930) виден"));
			// ★ 07.11.2026: настоящее нажатие мимо попапа (`asda.UI.ClickAwayMenu`) гасит 49400; ПКМ по строке открывает его снова.
			FSlateApplication& App = FSlateApplication::Get();
			TSet<FKey> Down;
			Down.Add(EKeys::LeftMouseButton);
			const FVector2D Away(4.0, 4.0);
			App.ProcessMouseButtonDownEvent(nullptr, FPointerEvent(0, Away, Away, Down, EKeys::LeftMouseButton, 0.f, FModifierKeysState()));
			App.ProcessMouseButtonUpEvent(FPointerEvent(0, Away, Away, TSet<FKey>(), EKeys::LeftMouseButton, 0.f, FModifierKeysState()));
			R.Expect(!Bt->IsWinShown(kWinPop), TEXT("K4а: щелчок мимо гасит попап 49400"));
			ClickNode(C, Bt->GetNode(RowKey(1, 0)), TEXT("строка 1 ПКМ снова"), /*bRight*/ true);
		});
		Then(C, 0.4f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			C->R.Expect(Bt->IsWinShown(kWinPop), TEXT("K4б: ПКМ по строке открывает попап снова"));
			ClickNode(C, Bt->GetNode(49420), TEXT("49420 «Назначить капитана»"));
		});
		Then(C, 0.4f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			TArray<uint8> P;
			R.Expect(CountOp(C, 6734, &P) == 1 && P.Num() == 14 && AsdaBytes::RdI16(P.GetData()) == 0 && AsdaBytes::RdI32(P.GetData() + 2) == kSelfCharId
				&& AsdaBytes::RdI16(P.GetData() + 6) == 0 && AsdaBytes::RdI32(P.GetData() + 8) == kMateCharId && AsdaBytes::RdI16(P.GetData() + 12) == 1
				&& !Bt->IsWinShown(kWinPop), TEXT("K5: 49420 → 6734 (армия, charId, мой индекс 0, charId цели, слот 1), попап скрыт"));
			// 6735 код 1: капитан — член 1.
			TArray<uint8> P35; P35.SetNumZeroed(7); P35[0] = 1; AsdaBytes::WrI32(P35.GetData() + 1, kMateCharId); AsdaBytes::WrI16(P35.GetData() + 5, 1);
			Bt->ClearTrace();
			Feed(C, 6735, P35);
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			R.Expect(Bt->GetNoticeTrace().Contains(10072) && Bt->GetLastNoticeText().Contains(TEXT("Mate")) && Bt->GetMember(1).Captain() == 0
				&& Bt->GetMember(2).Captain() == 3, FString::Printf(TEXT("K6: 6735 код 1 → 10072 с именем «%s», капитан — член 1"), *Bt->GetLastNoticeText()));
			Dismiss(C);
			ClickNode(C, Bt->GetNode(RowKey(1, 0)), TEXT("строка 1 ПКМ"), true);
		});
		Then(C, 0.4f, [C]()
		{
			ClickNode(C, B(C)->GetNode(49430), TEXT("49430 «Исключить»"));
		});
		Then(C, 0.5f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			const FString Want = AsdaUi::FormatPrintfArgs(L(C, 111937), { TEXT("Mate") });
			R.Expect(Bt->GetNoticeTrace().Contains(111937) && Bt->GetLastNoticeText() == Want, FString::Printf(TEXT("K7: 49430 → TWOBUT «%s»"), *Bt->GetLastNoticeText()));
			ClickNode(C, WinNode(C, TEXT("TWOBUT_MSGBOX"), TEXT("TWOBUT_BUT00")), TEXT("«Да» TWOBUT 87"));
		});
		Then(C, 0.4f, [C]()
		{
			FRun& R = C->R;
			UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(C);
			C->bBagEmpty = true;
			for (int32 J = 0; Inv && J < FMath::Min(Inv->GetContainerSize(2), 60); ++J) { const FAsdaItemInstance* It = Inv->GetCell(2, J); if (It && !It->IsEmpty()) { C->bBagEmpty = false; } }
			R.Expect(CountOp(C, 6186) == (C->bBagEmpty ? 0 : 1), FString::Printf(TEXT("K8: «Да» → 6186 только при предмете в сумке (сумка %s, ушло %d)"),
				C->bBagEmpty ? TEXT("пуста") : TEXT("не пуста"), CountOp(C, 6186)));
			// Предмет в сумку (контейнер 2, ячейка 0) и повтор.
			if (Inv && C->ItemCode && C->bBagEmpty)
			{
				C->bItemAdded = true;
				TArray<uint8> Rec; Rec.SetNumZeroed(57); AsdaBytes::WrI32(Rec.GetData(), C->ItemCode); Rec[4] = 2; AsdaBytes::WrI16(Rec.GetData() + 5, 0);
				AsdaBytes::WrI32(Rec.GetData() + 9, 1);
				Inv->ApplyExternalRecord(Rec, UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
			}
			ClickNode(C, B(C)->GetNode(RowKey(1, 0)), TEXT("строка 1 ПКМ"), true);
		});
		Then(C, 0.4f, [C]() { ClickNode(C, B(C)->GetNode(49430), TEXT("49430 «Исключить» (сумка не пуста)")); });
		Then(C, 0.5f, [C]() { ClickNode(C, WinNode(C, TEXT("TWOBUT_MSGBOX"), TEXT("TWOBUT_BUT00")), TEXT("«Да» TWOBUT 87")); });
		Then(C, 0.4f, [C]()
		{
			FRun& R = C->R;
			TArray<uint8> P;
			R.Expect(!C->ItemCode || (CountOp(C, 6186, &P) == (C->bBagEmpty ? 1 : 2) && P.Num() == 10 && AsdaBytes::RdI32(P.GetData()) == kSelfCharId && AsdaBytes::RdI16(P.GetData() + 4) == 1
				&& AsdaBytes::RdI32(P.GetData() + 6) == kMateCharId), TEXT("K9: «Да» с предметом в сумке → 6186 (charId, слот 1, charId цели)"));
		});
		// ★★★ 31.10.2026 — БАГИ ИГРОКОВ «БИТВЫ АРМИЙ» (`Reports/campwar_bugs/RE_SPEC.md`).
		// S. Чат армии: режим 9 (вкладка «Армия») — Enter → 6718 в серверной раскладке (108 б); префикс «[Армия] » срезается;
		//   каналы 0/1/2 в бою — окно 10222 без пакета; «/n» в бою — красная строка 5327; приём 6719 — строка ленты.
		Then(C, 0.4f, [C]()
		{
			FRun& R = C->R;
			UAsdaChatWindowSubsystem* Chat = Sub<UAsdaChatWindowSubsystem>(C);
			if (!Chat) { R.Expect(false, TEXT("S: подсистема окна чата есть")); return; }
			Chat->SetInputState(9);
			C->N6718 = CountOp(C, 6718);
			Chat->DebugSubmit(TEXT("Армия, к точке 3!"));
		});
		Then(C, 0.3f, [C]()
		{
			FRun& R = C->R;
			UAsdaChatWindowSubsystem* Chat = Sub<UAsdaChatWindowSubsystem>(C);
			TArray<uint8> P;
			const int32 N = CountOp(C, 6718, &P);
			const FString Name = P.Num() >= 28 ? FAsdaLoginData::DecodeCp1251(P.GetData() + 8, 20) : FString();
			const FString Txt = P.Num() >= 108 ? FAsdaLoginData::DecodeWireText(P.GetData() + 28, 80) : FString();
			R.Expect(N == C->N6718 + 1 && P.Num() == 108 && AsdaBytes::RdI32(P.GetData()) == kSelfCharId && AsdaBytes::RdI16(P.GetData() + 4) == 0
				&& AsdaBytes::RdI16(P.GetData() + 6) == B(C)->GetMyIndex() && P[107] == 0 && Txt == TEXT("Армия, к точке 3!"),
				FString::Printf(TEXT("S1: режим 9 → 6718: тело %d б (108 — серверная раскладка), charId, армия 0, индекс %d, имя «%s», текст «%s»"),
					P.Num(), B(C)->GetMyIndex(), *Name, *Txt));
			// Префикс приглашения режима 9 (loc 5337) срезается, как у эталона.
			Chat->DebugSubmit(L(C, 5337) + TEXT("ура"));
		});
		Then(C, 0.3f, [C]()
		{
			FRun& R = C->R;
			UAsdaChatWindowSubsystem* Chat = Sub<UAsdaChatWindowSubsystem>(C);
			TArray<uint8> P;
			CountOp(C, 6718, &P);
			const FString Txt = P.Num() >= 108 ? FAsdaLoginData::DecodeWireText(P.GetData() + 28, 80) : FString();
			R.Expect(CountOp(C, 6718) == C->N6718 + 2 && Txt == TEXT("ура"), FString::Printf(TEXT("S2: ведущий «%s» (loc 5337) срезан — текст «%s»"), *L(C, 5337), *Txt));
			// Каналы 0/1/2 в бою — окно 10222, пакета нет.
			C->NSentAll = C->Sent ? C->Sent->Num() : 0;
			Chat->SetInputState(0);
			Chat->DebugSubmit(TEXT("всем привет"));
		});
		Then(C, 0.3f, [C]()
		{
			FRun& R = C->R;
			UAsdaChatWindowSubsystem* Chat = Sub<UAsdaChatWindowSubsystem>(C);
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
			R.Expect(WM && WM->IsMsgBoxQueued(0, 10222) && C->Sent && C->Sent->Num() == C->NSentAll,
				TEXT("S3: канал 0 в бою → однокнопочное окно 10222 «Во время боя чат будет отключен.», пакета нет"));
			Dismiss(C);
			// «/n» в бою — режим не меняется, красная строка 5327.
			C->N5327 = Sub<UAsdaChatSubsystem>(C) ? Sub<UAsdaChatSubsystem>(C)->DebugCountSystemLines(L(C, 5327)) : 0;
			Chat->SetInputState(9);
			Chat->DebugSubmit(TEXT("/n"));
		});
		Then(C, 0.3f, [C]()
		{
			FRun& R = C->R;
			UAsdaChatWindowSubsystem* Chat = Sub<UAsdaChatWindowSubsystem>(C);
			UAsdaChatSubsystem* Sys = Sub<UAsdaChatSubsystem>(C);
			R.Expect(Chat->GetInputState() == 9 && Sys && Sys->DebugCountSystemLines(L(C, 5327)) == C->N5327 + 1,
				TEXT("S4: «/n» в бою → режим остаётся 9, красная строка loc 5327 «общий чат недоступен»"));
			// Приём 6719 (эхо сервера / товарищ): [0] флаг, [1..20] имя, [21..] текст.
			TArray<uint8> P; P.SetNumZeroed(101); P[0] = 1;
			FAsdaLoginData::EncodeCp1251(TEXT("Mate"), P.GetData() + 1, 19);
			FAsdaLoginData::EncodeWireText(TEXT("держим третью"), P.GetData() + 21, 79);
			C->N6719 = Chat->DebugCountLogLines(TEXT("держим третью"));
			Feed(C, 6719, P);
		});
		Then(C, 0.3f, [C]()
		{
			FRun& R = C->R;
			UAsdaChatWindowSubsystem* Chat = Sub<UAsdaChatWindowSubsystem>(C);
			R.Expect(Chat->DebugCountLogLines(TEXT("держим третью")) == C->N6719 + 1 && Chat->DebugCountLogLines(TEXT("Mate")) > 0,
				TEXT("S5: приём 6719 → строка ленты «[Группа] [Mate]… : держим третью» (категория 6)"));
			// T. Окно смерти на карте 19: армия 0 сейчас вторая (очки 1234 / 56 / 999999) → отсчёт 20 с, блокировка 10 с.
			UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
			C->SavedEnterMap = Net->EnterMapId;
			Net->EnterMapId = 19;
			Feed(C, 6729, Make3I32(1234, 56, 999999));
			Net->bSelfDead = false;
			Net->EnterSelfDeadState(TEXT("самотест боя: смерть на карте 19"), /*bAlreadyDead*/ false);
		});
		Then(C, 0.6f, [C]()
		{
			FRun& R = C->R;
			UAsdaAfterDeathSubsystem* Ad = Sub<UAsdaAfterDeathSubsystem>(C);
			if (!Ad) { R.Expect(false, TEXT("T: подсистема окна смерти есть")); return; }
			R.Expect(Ad->IsDeathMenuOpen() && Ad->DebugOptionCount() == 2 && Ad->DebugOptionType(0) == 7 && Ad->DebugOptionType(1) == 8 && Ad->DebugAutoOption() == 8,
				TEXT("T1: смерть на 19 → опции 7 «ближайший лагерь» и 8 «база», авто — 8"));
			R.Expect(FMath::IsNearlyEqual(Ad->DebugRespawnSec(), 20.f, 1.f) && FMath::IsNearlyEqual(Ad->DebugClickLockSec(), 10.f, 1.f),
				FString::Printf(TEXT("T2: армия 0 — вторая по очкам → отсчёт %.1f ≈ 20 с, блокировка 7/8 %.1f ≈ 10 с (половина) [ИСПРАВЛЕНИЕ]"),
					Ad->DebugRespawnSec(), Ad->DebugClickLockSec()));
			const FString L0 = Ad->DebugSlotLabel(0), L1 = Ad->DebugSlotLabel(1);
			const FString Want1 = L(C, 2386).Replace(TEXT("[sd1]"), *FString::FromInt(FMath::FloorToInt(Ad->DebugRespawnSec())));
			// ★ 08.11.2026 [ИСПРАВЛЕНИЕ] (`asda.Death.CampWarLockCountdown`): на 7 — остаток блокировки в формате loc 2418.
			const int32 Lock = FMath::CeilToInt(Ad->DebugClickLockSec());
			const FString Want0 = L(C, 2385) + TEXT(" ") + L(C, 2418).Replace(TEXT("[sd1]"), *FString::FromInt(Lock / 60)).Replace(TEXT("[sd2]"), *FString::FromInt(Lock % 60));
			R.Expect(L0 == Want0 && L1 == Want1 && !L1.Contains(L(C, 20193).Left(10)),
				FString::Printf(TEXT("T3: подписи — «%s» (2385 + остаток блокировки 2418) и «%s» (2386 с секундами, не «в городе» 20192/20193)"), *L0, *L1));
			Shot(C, TEXT("campwar_death_lock"));
			C->N4054 = CountOp(C, 4054);
			ClickNode(C, Ad->DebugSlotButton(0), TEXT("опция 7 во время блокировки"));
		});
		Then(C, 0.4f, [C]()
		{
			FRun& R = C->R;
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
			R.Expect(!(WM && WM->IsMsgBoxQueued(1, 0x4000)) && CountOp(C, 4054) == C->N4054,
				TEXT("T4: клик 7 во время блокировки — молча игнор (вопроса 2382 нет, 4054 нет)"));
			// Прошла блокировка (10 с) — обе кнопки доступны.
			UAsdaAfterDeathSubsystem* Ad = Sub<UAsdaAfterDeathSubsystem>(C);
			// ★ 08.11.2026: остаток идёт по секундам — минус 3 с, подпись 7 показывает на 3 меньше.
			const int32 Was = FMath::CeilToInt(Ad->DebugClickLockSec());
			Ad->Tick(3.0f);
			const int32 Now = FMath::CeilToInt(Ad->DebugClickLockSec());
			const FString Want3 = L(C, 2385) + TEXT(" ") + L(C, 2418).Replace(TEXT("[sd1]"), *FString::FromInt(Now / 60)).Replace(TEXT("[sd2]"), *FString::FromInt(Now % 60));
			R.Expect(Now == Was - 3 && Ad->DebugSlotLabel(0) == Want3, FString::Printf(TEXT("T4a: через 3 с на 7 «%s»"), *Ad->DebugSlotLabel(0)));
			Ad->Tick(7.5f);
			R.Expect(Ad->DebugClickLockSec() <= 0.f && Ad->DebugSlotLabel(0) == L(C, 2385),
				FString::Printf(TEXT("T4b: блокировка кончилась — на 7 снова «%s» без времени"), *Ad->DebugSlotLabel(0)));
			Shot(C, TEXT("campwar_death_unlocked"));
			ClickNode(C, Ad->DebugSlotButton(0), TEXT("опция 7 после блокировки"));
		});
		Then(C, 0.4f, [C]()
		{
			FRun& R = C->R;
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
			R.Expect(WM && WM->IsMsgBoxQueued(1, 0x4000), TEXT("T5: после блокировки клик 7 → вопрос loc 2382 «Желаешь воскреснуть?»"));
			ClickNode(C, WinNode(C, TEXT("TWOBUT_MSGBOX"), TEXT("TWOBUT_BUT00")), TEXT("«Да» вопроса 2382"));
		});
		Then(C, 0.4f, [C]()
		{
			FRun& R = C->R;
			TArray<uint8> P;
			const int32 N = CountOp(C, 4054, &P);
			R.Expect(N == C->N4054 + 1 && P.Num() == 1 && P[0] == 7, FString::Printf(TEXT("T6: «Да» → 4054 режим %d (7 — ближайший лагерь, ответ 4056 — режим войны сохраняется)"),
				P.Num() ? (int32)P[0] : -1));
			UAsdaAfterDeathSubsystem* Ad = Sub<UAsdaAfterDeathSubsystem>(C);
			const FString L1 = Ad->DebugSlotLabel(1);
			R.Expect(L1 == L(C, 2386).Replace(TEXT("[sd1]"), *FString::FromInt(FMath::FloorToInt(Ad->DebugRespawnSec()))),
				FString::Printf(TEXT("T7: отсчёт идёт — «%s»"), *L1));
			// Отсчёт до нуля → авто-опция 8 «база».
			C->N4054 = N;
			Ad->Tick(Ad->DebugRespawnSec() + 0.5f);
		});
		Then(C, 0.3f, [C]()
		{
			FRun& R = C->R;
			TArray<uint8> P;
			R.Expect(CountOp(C, 4054, &P) == C->N4054 + 1 && P.Num() == 1 && P[0] == 8, TEXT("T8: отсчёт истёк → авто-воскрешение на базе: 4054 режим 8"));
			UAsdaAfterDeathSubsystem* Ad = Sub<UAsdaAfterDeathSubsystem>(C);
			Ad->DebugCloseMenuOnRevive();
			// Армия 0 последняя: очки поровну, численность меньше всех → отсчёт 10 с, блокировка 5 с.
			Feed(C, 6729, Make3I32(5, 5, 5));      // боевые пакеты разбираются в тике — смерть следующим шагом
			Feed(C, 6742, Make3I16(10, 20, 30));
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
			Net->bSelfDead = false;
			Net->EnterSelfDeadState(TEXT("самотест боя: смерть на карте 19 (армия последняя)"), false);
		});
		Then(C, 0.4f, [C]()
		{
			FRun& R = C->R;
			UAsdaAfterDeathSubsystem* Ad = Sub<UAsdaAfterDeathSubsystem>(C);
			R.Expect(FMath::IsNearlyEqual(Ad->DebugRespawnSec(), 10.f, 1.f) && FMath::IsNearlyEqual(Ad->DebugClickLockSec(), 5.f, 1.f),
				FString::Printf(TEXT("T9: очки равны, численность армии 0 меньше всех → отсчёт %.1f ≈ 10 с, блокировка %.1f ≈ 5 с"),
					Ad->DebugRespawnSec(), Ad->DebugClickLockSec()));
			Ad->DebugCloseMenuOnRevive();
			// U. «Вошли мёртвым» / рассинхрон 6001 на карте 19 — меню 7/8, окна 2370 нет; режим 0 не уходит.
			UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
			Net->bSelfDead = false;
			Net->EnterSelfDeadState(TEXT("самотест боя: рассинхрон 6001 на карте 19"), /*bAlreadyDead*/ true);
		});
		Then(C, 0.4f, [C]()
		{
			FRun& R = C->R;
			UAsdaAfterDeathSubsystem* Ad = Sub<UAsdaAfterDeathSubsystem>(C);
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
			R.Expect(!Ad->DebugHasModal() && !(WM && WM->IsMsgBoxQueued(0, 2370)) && Ad->DebugOptionCount() == 2 && Ad->DebugOptionType(0) == 7,
				TEXT("U1: вошли мёртвым на 19 → меню 7/8, окна 2370 нет [ИСПРАВЛЕНИЕ] (режим 0 снял бы режим войны)"));
			C->N4054 = CountOp(C, 4054);
			Ad->SelectOption(4);   // прочий путь (консоль) — режим 0 на карте 19 не уходит
			R.Expect(CountOp(C, 4054) == C->N4054, TEXT("U2: опция 4 (режим 0) на карте 19 не отправляется"));
			Ad->DebugCloseMenuOnRevive();
			// U3. Обычная карта: «вошли мёртвым» → окно 2370; свой 4053 следом закрывает его и строит меню выбора.
			UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
			Net->EnterMapId = 3;
			Net->bSelfDead = false;
			Net->EnterSelfDeadState(TEXT("самотест боя: рассинхрон 6001 на карте 3"), true);
		});
		Then(C, 0.4f, [C]()
		{
			FRun& R = C->R;
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
			R.Expect(WM && WM->IsMsgBoxQueued(0, 2370), TEXT("U3: обычная карта — вошли мёртвым → окно 2370 (как оригинал)"));
			TArray<uint8> P; P.SetNumZeroed(4); AsdaBytes::WrI16(P.GetData(), kSelfNum); AsdaBytes::WrI16(P.GetData() + 2, -1);
			Feed(C, 4053, P);
		});
		Then(C, 0.6f, [C]()
		{
			FRun& R = C->R;
			UAsdaAfterDeathSubsystem* Ad = Sub<UAsdaAfterDeathSubsystem>(C);
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
			R.Expect(!(WM && WM->IsMsgBoxQueued(0, 2370)) && Ad->IsDeathMenuOpen() && Ad->DebugOptionType(0) == 4,
				TEXT("U4: свой 4053 закрыл висящее окно 2370 и построил меню выбора (опция 4) [ИСПРАВЛЕНИЕ]"));
			Ad->DebugCloseMenuOnRevive();
			UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
			Net->bSelfDead = false; Net->bSelfDeathNoClip = false;
			Net->EnterMapId = C->SavedEnterMap;
			if (AAsdaPlayerCharacter* Me = SelfCh(C)) { Me->SetAnimState(EAsdaPlayerState::Idle); }
			Dismiss(C);
		});
		Then(C, 0.3f, [C]()
		{
			// L. 49410 «Пригласить»: цель в отряде — Д-Б4(а): ключ 0 → пустая строка, ключ 1 → 1047.
			if (UAsdaPartySubsystem* Party = Sub<UAsdaPartySubsystem>(C)) { Party->DebugSetPartyState(false, false, -1, 0); }
			ClickNode(C, B(C)->GetNode(RowKey(1, 0)), TEXT("строка 1 ПКМ"), true);
		});
		Then(C, 0.4f, [C]() { B(C)->ClearTrace(); ClickNode(C, B(C)->GetNode(49410), TEXT("49410 «Пригласить в отряд»")); });
		Then(C, 0.4f, [C]()
		{
			FRun& R = C->R;
			UAsdaPartySubsystem* Party = Sub<UAsdaPartySubsystem>(C);
			R.Expect(Party && Party->IsInviteWindowOpen() && !B(C)->IsWinShown(kWinPop), TEXT("L1: не в отряде → окно приглашения 2250 открыто (`partyUi_ShowInviteRequest`)"));
			if (Party) { Party->CloseInviteWindow(); }
			// M. 6721: вошёл новичок в слот 5 (окно 49399 открыто → снова 6717), затем вышел (Д-Б8: имя в 10071).
			B(C)->ClearTrace();
			Feed(C, 6721, Make6721(1, 930500, 5, "Newbie"));
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			R.Expect(Bt->GetNoticeTrace().Num() == 1 && Bt->GetNoticeTrace()[0] == 10070 && Bt->GetLastNoticeText().Contains(TEXT("Newbie")),
				FString::Printf(TEXT("M1: 6721 [0]=1 → чат 10070 с именем («%s»)"), *Bt->GetLastNoticeText()));
			// Повторный 6717 (49399 открыт) уже очистил 50 записей до ответа — у вышедшего имени нет (1:1 эталон).
			Feed(C, 6721, Make6721(0, 930500, 5, "Newbie"));
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			R.Expect(Bt->GetNoticeTrace().Num() == 2 && Bt->GetNoticeTrace()[1] == 10071, TEXT("M1: 6721 [0]=0 → чат 10071"));
			R.Expect(CountOp(C, 6717) == 3, FString::Printf(TEXT("M1: 49399 открыт → каждый 6721 снова шлёт 6717 (%d)"), CountOp(C, 6717)));
			ClickNode(C, Bt->GetNode(49001), TEXT("X 49001"));
		});
		Then(C, 0.4f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			R.Expect(!Bt->IsWinShown(kWinAtk) && !Bt->IsWinShown(kWinPop), TEXT("M2: X 49001 → 49399 и 49400 спрятаны"));
			// N. 17520 «Выйти из боя» на миникарте: «Нет» — ничего, «Да» — 6731.
			UWidget* Out = WinNode(C, TEXT("MINIMAP_WIN"), TEXT("CAMPWAR_Out_Button"));
			R.Expect(AsdaUi::IsShown(Out), TEXT("N0: 17520 видна при HUD боя (`uiDlgSetDetailVisible`)"));
			ClickNode(C, Out, TEXT("17520 «Выйти из боя»"));
		});
		Then(C, 0.5f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			R.Expect(Bt->GetNoticeTrace().Num() > 0 && Bt->GetNoticeTrace().Last() == 10047 && Bt->GetLastNoticeText() == L(C, 10047), TEXT("N1: 17520 → TWOBUT 10047"));
			Shot(C, TEXT("campwar_battle_10047"));
			ClickNode(C, WinNode(C, TEXT("TWOBUT_MSGBOX"), TEXT("TWOBUT_BUT01")), TEXT("«Нет» TWOBUT 10047"));
		});
		Then(C, 0.4f, [C]()
		{
			C->R.Expect(CountOp(C, 6731) == 1, TEXT("N2: «Нет» → 6731 не ушёл (контроль)"));
			ClickNode(C, WinNode(C, TEXT("MINIMAP_WIN"), TEXT("CAMPWAR_Out_Button")), TEXT("17520 «Выйти из боя»"));
		});
		Then(C, 0.5f, [C]() { ClickNode(C, WinNode(C, TEXT("TWOBUT_MSGBOX"), TEXT("TWOBUT_BUT00")), TEXT("«Да» TWOBUT 10047")); });
		Then(C, 0.4f, [C]()
		{
			FRun& R = C->R;
			R.Expect(CountOp(C, 6731) == 2, TEXT("N3: «Да» → 6731"));
			// O. 6741 (контрол HUD); P. 6737 — сброс: HUD снят, чат и меню вернулись.
			TArray<uint8> P41; P41.SetNumZeroed(7); P41[0] = 1;
			Feed(C, 6741, P41);
			Feed(C, 6737, TArray<uint8>());
		});
		Then(C, 0.4f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			R.Expect(Bt->GetHudFlag6741() == 1, TEXT("O1: 6741 → модель +0x9B4 = 1"));
			R.Expect(!Bt->IsHudOn() && !Bt->IsWinShown(kWinPoint) && !Bt->IsWinShown(kWinMap) && !Bt->IsWinShown(kWinTime), TEXT("P1: 6737 → HUD снят, окна спрятаны"));
			if (UAsdaChatWindowSubsystem* Chat = Sub<UAsdaChatWindowSubsystem>(C)) { R.Expect(Chat->GetInputState() != 9 && !Chat->IsChannelPrefixMode(), TEXT("P1: чат вернул режим")); }
			if (UAsdaSystemMenuSubsystem* Sys = Sub<UAsdaSystemMenuSubsystem>(C)) { R.Expect(Sys->IsExpanded(), TEXT("P1: системное меню снова видно")); }
			R.Expect(!AsdaUi::IsShown(WinNode(C, TEXT("MINIMAP_WIN"), TEXT("CAMPWAR_Out_Button"))), TEXT("P1: 17520 спрятана"));
			// O2. 6739 — оператор/наблюдатель.
			TArray<uint8> P39; P39.SetNumZeroed(9); AsdaBytes::WrI32(P39.GetData(), kSelfCharId); P39[4] = 7; AsdaBytes::WrI16(P39.GetData() + 5, 21); AsdaBytes::WrI16(P39.GetData() + 7, 30);
			Feed(C, 6739, P39);
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarBattleSubsystem* Bt = B(C);
			FRun& R = C->R;
			R.Expect(Bt->GetMyIndex() == 7 && Bt->GetSelfArmy() == -1 && Sub<UAsdaCampWarSubsystem>(C)->GetEndH() == 21 && Bt->IsHudOn(),
				TEXT("O2: 6739 → оператор: армия −1, +0xB7E = 7, конец 21:30, HUD включён"));
			Feed(C, 6713, Make6713(0));
			// Q. Снова вход и уход с карты 19 обычным порталом (бит 0x80 → HUD выкл) — ResetState и токены 20000 → FF.
			Sub<UAsdaLoadManagerSubsystem>(C)->CurrentMapId = 3;
			Sub<UAsdaCampWarSubsystem>(C)->OnPortalTransfer5055(true, 19);
		});
		Then(C, 0.3f, [C]()
		{
			FRun& R = C->R;
			R.Expect(B(C)->IsHudOn(), TEXT("Q1: повторный вход — HUD включён"));
			Sub<UAsdaLoadManagerSubsystem>(C)->CurrentMapId = 19;
			Feed(C, 6729, Make3I32(5, 6, 7));
			UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
			Net->CampWarToken1 = 0x11; Net->CampWarToken2 = 0x22;
			Sub<UAsdaCampWarSubsystem>(C)->OnPortalTransfer5055(false, 3);
		});
		Then(C, 0.3f, [C]()
		{
			FRun& R = C->R;
			UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
			R.Expect(!B(C)->IsHudOn() && B(C)->GetScore(0) == 0 && Net->CampWarToken1 == 0xFF && Net->CampWarToken2 == 0xFF,
				TEXT("Q2: 5055 с карты 19 (HUD подавлен) → HUD снят, ResetState: очки 0, токены 20000 → FF FF"));
			// R. 4055 при включённом HUD → Toggle(0,1) + ResetState.
			Sub<UAsdaLoadManagerSubsystem>(C)->CurrentMapId = 3;
			Sub<UAsdaCampWarSubsystem>(C)->OnPortalTransfer5055(true, 19);
		});
		Then(C, 0.3f, [C]()
		{
			B(C)->OnLeaveByServerMove(TEXT("самотест: 4055"));
		});
		Then(C, 0.3f, [C]()
		{
			C->R.Expect(!B(C)->IsHudOn(), TEXT("R1: 4055/6013 при HUD → `campWarWnd_ToggleVisible(0, 1)` + ResetState"));
			Finish(C);
		});
	}

	static void Finish(TSharedRef<FCtx> C)
	{
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
		if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C)) { WM->ClearMsgBoxQueue(TEXT("самотест боя: конец")); }
		if (UAsdaCampWarBattleSubsystem* Bt = B(C)) { Bt->ResetSessionState(EAsdaSessionResetReason::EnteringWorld); Bt->ClearTrace(); }
		if (UAsdaCampWarSubsystem* CW = Sub<UAsdaCampWarSubsystem>(C)) { CW->ResetSessionState(EAsdaSessionResetReason::EnteringWorld); }
		if (UAsdaPartySubsystem* Party = Sub<UAsdaPartySubsystem>(C)) { if (Party->IsInviteWindowOpen()) { Party->CloseInviteWindow(); } Party->DebugSetPartyState(false, false, -1, 0); }
		for (const TWeakObjectPtr<AActor>& A : C->Spawned) { if (A.IsValid()) { A->Destroy(); } }
		if (UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(C))
		{
			if (C->bItemAdded) { Inv->ClearCell(2, 0); }
			if (C->SavedGold >= 0) { Inv->SetGoldDisplay(C->SavedGold); }
		}
		if (UAsdaLoadManagerSubsystem* LM = Sub<UAsdaLoadManagerSubsystem>(C)) { LM->CurrentMapId = C->SavedMap; }
		if (Net)
		{
			Net->OnSelfFaction.Broadcast(C->SavedCamp, 0, 0);
			if (C->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, C->CapGen); }
		}
		if (UAsdaChatWindowSubsystem* Chat = Sub<UAsdaChatWindowSubsystem>(C))
		{
			Chat->SetChannelPrefixMode(false);
			if (C->SavedChatState >= 0) { Chat->SetInputState(C->SavedChatState); }
		}
		if (C->Keys) { C->Keys->Restore(); }
		if (C->SelfScope) { C->SelfScope->Restore(); }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaCampWarBattleTest, TEXT("[бой-самотест]"), C->R.Ok, C->R.Fail);
	}

	// asda.UI.SelfTestCampWarBattle [delay=N] [shots] [quit] — офлайн: спавн своего павна (если его нет), через 4 с — прогон.
	static FAutoConsoleCommandWithWorldAndArgs GAsdaCampWarBattleSelfTest(
		TEXT("asda.UI.SelfTestCampWarBattle"),
		TEXT("asda.UI.SelfTestCampWarBattle [delay=N] [shots] [quit] — ОФЛАЙН-самотест боевой фазы «Битвы армий»: 5055 → HUD боя, точки 6725/6726, захват 6724 "
		     "с полосой 49510, 6728 (чат и 49910), 6729/6742, таймер 6736, 6730, итог 6732/6733 (49800), «Рейд» 6715/6716/6721/6735 с кликами (6717/6734/6186), "
		     "17520 → 10047 → 6731, 6739/6741/6737, уход с карты 19"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			if (!W) { return; }
			TSharedRef<FCtx> C = MakeShared<FCtx>();
			C->W = W;
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
			C->bQuit = KitArgs.bQuit;
			C->Delay = KitArgs.Delay;
			if (KitArgs.Has(TEXT("shots")))
			{
				C->ShotDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports/faction_select/shots"));
			}
			static const uint16 kGuardOps[] = { 6717, 6731, 6734, 6186, 6723, 5098 };
			if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
			auto Start = [C]()
			{
				UWorld* Wd = C->W.Get();
				if (!Wd || !GEngine) { return; }
				if (AsdaSelfTestKit::RefuseDeferred(Wd, kCmd, C->bQuit)) { return; }
				APlayerController* P = Wd->GetFirstPlayerController();
				if (!P || !P->GetPawn())
				{
					GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
					GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256 CampWarBattle"));
				}
				FTimerHandle H;
				Wd->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]() { Run(C); }), 4.f, false);
			};
			if (C->Delay <= 0.f) { Start(); return; }
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Start), C->Delay, false);
		}));
}

#endif // !UE_BUILD_SHIPPING
