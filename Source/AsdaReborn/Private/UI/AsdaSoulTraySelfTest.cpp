// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ ЛОТКА НАВЫКОВ ДРУЖБЫ (07.11.2026): `asda.Soulmate.TraySelfTest [en] [shot=<папка>] [quit]`. [ИСПРАВЛЕНИЕ]
//   Жалоба: сетка Soulmate Skills пустая. Разбор — у ключа `asda.Soul.TrayFlow` (AsdaSoulmateSkillSubsystem.cpp).
//   Офлайн (`-AsdaNoNet`), карта мира, путь игрока: своя запись 5220, блок 5228, окно 6400 на вкладке Друга, курсор и щелчки — живая геометрия
//   (`SetCursorPos`, `SimulateClickAt`, `SimulateDrag`).
//     A) ключ 0: картинок ячеек нет — дефект воспроизведён;
//     B) ключ 1, Друг вне игры, 5228 нет: 12 ячеек привязаны, навыки на месте, все серые (`-g`);
//     C) Друг в игре, 5228 зажигает навыки: цветные кисти; 5238 — ещё один;
//     D) наведение — подсказка лотка; уход — гаснет;
//     E) одиночный щелчок навык не применяет (откат не начат, нотисов нет); перетаскивание на панель — слот Type 4 с этим id;
//        30/36/41 не перетаскиваются; мимо панели — ничего;
//     F) откат навыка: двойной щелчок — нотис 2203.
//   Итог: «ЛОТОК-ДРУГА ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "Components/Image.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaCommunitySubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaSkillWindowSubsystem.h"
#include "UI/AsdaSoulmateSkillSubsystem.h"
#include "UI/AsdaSoulmateSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSoulTrayTest, Log, All);

namespace AsdaSoulTrayTest
{
	static const TCHAR* kCmd = TEXT("asda.Soulmate.TraySelfTest");   // имя команды
	static const TCHAR* kKey = TEXT("asda.Soul.TrayFlow");           // ключ отката
	constexpr int32 kMeChar = 75230;      // свой charId
	constexpr int32 kMateChar = 75214;    // charId Друга
	constexpr int32 kMe = 321;            // свой userNum
	constexpr int32 kSlot = 8;            // номер пары
	constexpr int32 kAbs = 1;             // слот панели под бросок

	// Шаг сценария: true — завершён. Аргумент — секунды от начала шага.
	using FStep = TFunction<bool(float)>;

	// Состояние прогона.
	struct FState
	{
		TWeakObjectPtr<UWorld> W;                        // мир
		bool bQuit = false;                              // `quit` в конце
		bool bEn = false;                                // английские строки
		FString ShotDir;                                 // папка кадров
		int32 Ok = 0, Fail = 0;                          // счётчики
		TArray<FStep> Steps;                             // сценарий
		int32 Idx = 0;                                   // текущий шаг
		double StepT0 = 0.0;                             // начало шага
		FDelegateHandle H;                               // подписка на тик мира
		TUniquePtr<FAsdaNetSelfScope> NetScope;          // свой номер/charId — вернуть
		TUniquePtr<FAsdaCVarScope> CVars;                // ключи — вернуть
		TWeakObjectPtr<UAsdaNetSubsystem> Net;           // сеть
		TWeakObjectPtr<UAsdaSoulmateSkillSubsystem> Sk;  // лоток
		TWeakObjectPtr<UAsdaHotbarSubsystem> Hb;         // панель
		TWeakObjectPtr<UAsdaWindowSubsystem> WM;         // окна
		TWeakObjectPtr<UAsdaItemTooltipSubsystem> Tip;   // плашка
		TWeakObjectPtr<UAsdaChatSubsystem> Chat;         // журнал 2572
		TWeakObjectPtr<UAsdaSkillWindowSubsystem> Sw;    // подсказка навыка
		TWeakObjectPtr<UAsdaCommunitySubsystem> Comm;    // окно 6400
		FAsdaHotbarSlot SavedSlot;                       // слот панели до прогона
		bool bSlotSaved = false;                         // слот снят
		int32 N2203 = 0;                                 // строк 2203 до шага

		// Записать итог проверки.
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaSoulTrayTest, Log,   TEXT("ЛОТОК-ДРУГА ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaSoulTrayTest, Error, TEXT("ЛОТОК-ДРУГА FAIL %s"), *What); }
		}
		// Кадр в папку `shot=`.
		void Shot(const TCHAR* Name) const
		{
			if (ShotDir.IsEmpty()) { return; }
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(ShotDir, FString(Name) + TEXT(".png")), /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		}
		// Сколько строк журнала с текстом нотиса.
		int32 Lines(int32 LocId) const
		{
			const UAsdaChatSubsystem* C = Chat.Get();
			return C ? C->DebugCountSystemLines(AsdaNotice::Loc(W.IsValid() ? W->GetGameInstance() : nullptr, LocId)) : -1;
		}
		// Индекс ячейки с навыком `Id`, −1 — нет.
		int32 CellOf(int32 Id) const
		{
			for (int32 i = 0; i < 12; ++i) { if (Sk->DebugTraySkill(i) == Id) { return i; } }
			return -1;
		}
		// Первая занятая ячейка, навык которой можно тащить.
		int32 DragCell() const
		{
			for (int32 i = 0; i < 12; ++i)
			{
				const int32 Id = Sk->DebugTraySkill(i);
				if (Id >= 0 && Id != 30 && Id != 36 && Id != 41) { return i; }
			}
			return -1;
		}
		// Центр ячейки на экране.
		bool CellCenter(int32 I, FVector2D& Out) const
		{
			const UImage* C = Sk->DebugTrayCell(I);
			if (!C) { return false; }
			FVector2D Mn, Mx;
			if (!AsdaUi::WidgetAbsRect(C, Mn, Mx)) { return false; }
			Out = (Mn + Mx) * 0.5;
			return true;
		}
		// Сколько занятых ячеек, картинка которых видима.
		int32 VisibleFilled() const
		{
			int32 N = 0;
			for (int32 i = 0; i < 12; ++i)
			{
				const UImage* C = Sk->DebugTrayCell(i);
				if (C && Sk->DebugTraySkill(i) >= 0 && C->GetVisibility() == ESlateVisibility::Visible) { ++N; }
			}
			return N;
		}
		// Имя ресурса кисти ячейки.
		FString BrushOf(int32 I) const
		{
			const UImage* C = Sk->DebugTrayCell(I);
			const UObject* R = C ? C->GetBrush().GetResourceObject() : nullptr;
			return R ? R->GetName() : FString();
		}
	};

	// Тело 4003 (9 б).
	static TArray<uint8> Body4003()
	{
		TArray<uint8> B; B.SetNumZeroed(9);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)kMe);
		AsdaBytes::WrI16(B.GetData() + 2, 256);
		AsdaBytes::WrI16(B.GetData() + 4, 256);
		B[8] = 1;
		return B;
	}
	// Своя запись 5220 (101 б): @18 уровень Дружбы, @25 Друг в игре.
	static TArray<uint8> Body5220(bool bInGame)
	{
		TArray<uint8> B; B.SetNumZeroed(101);
		AsdaBytes::WrI32(B.GetData() + 0, kMeChar);
		AsdaBytes::WrI16(B.GetData() + 4, (int16)kMe);
		AsdaBytes::WrI32(B.GetData() + 6, kSlot);
		B[18] = 2;
		AsdaBytes::WrI32(B.GetData() + 19, kMateChar);
		AsdaBytes::WrI16(B.GetData() + 23, (int16)(bInGame ? 77 : -1));
		B[25] = bInGame ? 1 : 0;
		return B;
	}
	// 5228 (101 б): тип, байт k + 1 = навык k открыт.
	static TArray<uint8> Body5228(const TArray<int32>& Ids)
	{
		TArray<uint8> B; B.SetNumZeroed(101);
		B[0] = 1;
		for (const int32 Id : Ids) { B[Id + 1] = 1; }
		return B;
	}
	// 5207 код 1: разрыв (уборка).
	static TArray<uint8> Body5207()
	{
		TArray<uint8> B; B.SetNumZeroed(10);
		B[0] = 1;
		FMemory::Memcpy(B.GetData() + 5, "Pal", 3);
		return B;
	}

	// Конец прогона.
	static void Finish(const TSharedRef<FState>& SP)
	{
		FState& S = *SP;
		FWorldDelegates::OnWorldPostActorTick.Remove(S.H);
		if (S.Hb.IsValid() && S.bSlotSaved)
		{
			S.Hb->SetSlotContent(kAbs, (uint8)S.SavedSlot.Type, (uint8)S.SavedSlot.SubType, S.SavedSlot.Index, S.SavedSlot.Count, S.SavedSlot.Id);
		}
		if (S.Sk.IsValid()) { S.Sk->ClearAllCooldowns(); }
		if (S.Net.IsValid()) { S.Net->DebugFeedPacket(5207, Body5207()); }
		if (S.WM.IsValid()) { S.WM->ClearMsgBoxQueue(kCmd); }
		FSlateApplication::Get().SetCursorPos(FVector2D(2.0, 400.0));
		S.CVars.Reset();
		S.NetScope.Reset();
		AsdaSelfTestKit::Finish(S.W.Get(), kCmd, S.bQuit, LogAsdaSoulTrayTest, TEXT("ЛОТОК-ДРУГА"), S.Ok, S.Fail);
	}

	// Тик сценария.
	static void Tick(const TSharedRef<FState>& InSP, UWorld* InW)
	{
		const TSharedRef<FState> SP = InSP;   // своя ссылка: `Finish` снимает подписку вместе с копией в лямбде
		FState& S = *SP;
		UWorld* W = S.W.Get();
		if (!W) { FWorldDelegates::OnWorldPostActorTick.Remove(S.H); return; }
		if (InW != W) { return; }
		while (S.Steps.IsValidIndex(S.Idx))
		{
			const float T = (float)(W->GetTimeSeconds() - S.StepT0);
			if (!S.Steps[S.Idx](T)) { return; }
			++S.Idx;
			S.StepT0 = W->GetTimeSeconds();
		}
		Finish(SP);
	}

	// Сценарий.
	static void Build(const TSharedRef<FState>& SP)
	{
		FState* S = &SP.Get();
		auto Do = [](TFunction<void()> Fn) { return FStep([Fn](float) { Fn(); return true; }); };
		auto Wait = [](float Sec) { return FStep([Sec](float T) { return T >= Sec; }); };
		auto Reopen = [S]()
		{
			S->Comm->OpenOnTab(0);
		};

		// ═══ Вход и строки ═══
		S->Steps.Add(Do([S]()
		{
			const UGameInstance* GI = S->W->GetGameInstance();
			for (const int32 Id : { 2203, 8162 })
			{
				const FString T = AsdaNotice::Loc(GI, Id);
				bool bCyr = false;
				for (const TCHAR Ch : T) { if (Ch >= 0x400 && Ch <= 0x4FF) { bCyr = true; break; } }
				S->Check(!T.IsEmpty() && bCyr != S->bEn, FString::Printf(TEXT("строка %d: «%s»"), Id, *T));
			}
			GEngine->Exec(S->W.Get(), TEXT("Asda.Load.CurrentMap 3"));
			S->Net->bSelfUserNumKnown = false;
			S->Net->SelectedCharId = kMeChar;
			S->Net->DebugFeedPacket(5220, Body5220(false));
			S->Net->DebugFeedPacket(4003, Body4003());
		}));
		S->Steps.Add(Wait(4.5f));

		// ═══ A. Ключ 0: дефект ═══
		S->Steps.Add(Do([S, Reopen]() { S->CVars->Set(kKey, 0); Reopen(); }));
		S->Steps.Add(Wait(0.6f));
		S->Steps.Add(Do([S]()
		{
			S->Check(S->Sk->DebugTrayBound() == 0, FString::Printf(TEXT("A1 ключ 0: картинок ячеек %d (дефект: ждём 0)"), S->Sk->DebugTrayBound()));
			S->Shot(TEXT("tray_0_key0"));
			S->Comm->CloseWindow();
			S->CVars->Set(kKey, 1);
		}));
		S->Steps.Add(Wait(0.4f));

		// ═══ B. Ключ 1, Друг вне игры ═══
		S->Steps.Add(Do([S, Reopen]() { S->Sk->DebugReleaseTray(); Reopen(); }));
		S->Steps.Add(Wait(0.6f));
		S->Steps.Add(Do([S]()
		{
			const int32 Vis = S->VisibleFilled();
			S->Check(S->Sk->DebugTrayBound() == 12, FString::Printf(TEXT("B1 картинок ячеек %d/12"), S->Sk->DebugTrayBound()));
			S->Check(Vis > 0, FString::Printf(TEXT("B2 занятых видимых ячеек %d"), Vis));
			bool bGray = Vis > 0;
			FString L;
			for (int32 i = 0; i < 12; ++i)
			{
				if (S->Sk->DebugTraySkill(i) < 0) { continue; }
				const FString Br = S->BrushOf(i);
				L += FString::Printf(TEXT(" %d=%s"), S->Sk->DebugTraySkill(i), *Br);
				bGray &= !Br.IsEmpty() && !S->Sk->DebugIsLit(S->Sk->DebugTraySkill(i));
			}
			S->Check(bGray, FString::Printf(TEXT("B3 без 5228 все серые, кисти есть:%s"), *L));
			S->Shot(TEXT("tray_1_gray"));
		}));
		S->Steps.Add(Wait(0.3f));   // кадр снимается на следующем кадре игры: следующий шаг не должен успеть раньше

		// ═══ C. Друг в игре, 5228 и 5238 ═══
		S->Steps.Add(Do([S]()
		{
			TArray<int32> Lit;
			for (int32 i = 0; i < 12; ++i) { const int32 Id = S->Sk->DebugTraySkill(i); if (Id >= 0 && (i % 2) == 0) { Lit.Add(Id); } }
			S->Net->DebugFeedPacket(5220, Body5220(true));
			S->Net->DebugFeedPacket(5228, Body5228(Lit));
		}));
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]()
		{
			int32 NLit = 0, NBrush = 0, NOk = 0;
			for (int32 i = 0; i < 12; ++i)
			{
				const int32 Id = S->Sk->DebugTraySkill(i);
				if (Id < 0) { continue; }
				const bool bWant = (i % 2) == 0;
				NLit += S->Sk->DebugIsLit(Id) ? 1 : 0;
				NBrush += S->BrushOf(i).IsEmpty() ? 0 : 1;
				NOk += (S->Sk->DebugIsLit(Id) == bWant) ? 1 : 0;
			}
			S->Check(NLit > 0 && NOk == S->VisibleFilled() && NBrush == NOk, FString::Printf(TEXT("C1 5228: зажжено %d, по плану %d, кистей %d"), NLit, NOk, NBrush));
			S->Shot(TEXT("tray_2_lit"));
		}));
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]()
		{
			const int32 Id = S->Sk->DebugTraySkill(1);
			if (Id >= 0)
			{
				TArray<uint8> B; B.SetNumZeroed(2);
				AsdaBytes::WrI16(B.GetData(), (int16)Id);
				S->Net->DebugFeedPacket(5238, B);
				S->Check(S->Sk->DebugIsLit(Id), FString::Printf(TEXT("C2 5238: навык %d зажжён"), Id));
			}
		}));

		// ═══ D. Подсказка ═══
		S->Steps.Add(Do([S]()
		{
			FVector2D P;
			S->Check(S->CellCenter(0, P), TEXT("D0 центр ячейки 0"));
			FSlateApplication::Get().SetCursorPos(P);
		}));
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]()
		{
			S->Check(S->Tip->IsTooltipShown() && S->Tip->GetTipOwner() == EAsdaTipOwner::SoulTray, TEXT("D1 наведение: подсказка лотка показана"));
			S->Check(!S->Sw->DebugSoulTooltipTitle(30).Contains(TEXT("FFFF0000")), FString::Printf(TEXT("D1б навык 30 при уровне Дружбы 2 и Друге в игре: заголовок не красный: %s"), *S->Sw->DebugSoulTooltipTitle(30)));
			S->Shot(TEXT("tray_3_tip"));
		}));
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]() { FSlateApplication::Get().SetCursorPos(FVector2D(2.0, 400.0)); }));
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]() { S->Check(!S->Tip->IsTooltipShown() || S->Tip->GetTipOwner() != EAsdaTipOwner::SoulTray, TEXT("D2 уход: подсказка погашена")); }));

		// ═══ E. Одиночный щелчок и перетаскивание ═══
		S->Steps.Add(Do([S]()
		{
			S->SavedSlot = S->Hb->GetSlot(kAbs);
			S->bSlotSaved = true;
			S->Hb->SetSlotContent(kAbs, 0, 0, 0, 0, 0);
			FVector2D P;
			S->CellCenter(S->DragCell(), P);
			S->WM->SimulateClickAt(P, 3);
		}));
		S->Steps.Add(Wait(0.8f));   // дольше окна двойного щелчка
		S->Steps.Add(Do([S]()
		{
			const int32 Id = S->Sk->DebugTraySkill(S->DragCell());
			S->Check(S->Sk->GetCooldownRemainMs(Id) == 0 && S->Sk->DebugTx5231() == 0, TEXT("E1 одиночный щелчок навык не применил"));
			FVector2D From, Mn, Mx;
			S->CellCenter(S->DragCell(), From);
			S->Check(S->Hb->GetSlotIconAbsRect(kAbs, Mn, Mx), TEXT("E2 ячейка панели есть"));
			S->WM->SimulateDrag(From, (Mn + Mx) * 0.5, 20);
		}));
		S->Steps.Add(Wait(1.0f));
		S->Steps.Add(Do([S]()
		{
			const int32 Id = S->Sk->DebugTraySkill(S->DragCell());
			const FAsdaHotbarSlot& Sl = S->Hb->GetSlot(kAbs);
			S->Check(Sl.Type == (uint8)EAsdaHotbarSlotType::SkillAuto && Sl.Id == Id,
				FString::Printf(TEXT("E3 бросок на панель: тип %d, id %d (ждём 4, %d)"), Sl.Type, Sl.Id, Id));
			S->Shot(TEXT("tray_4_dropped"));
		}));
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]()
		{
			const int32 C30 = S->CellOf(30);
			FVector2D From, Mn, Mx;
			if (C30 >= 0 && S->CellCenter(C30, From) && S->Hb->GetSlotIconAbsRect(kAbs + 1, Mn, Mx))
			{
				S->Hb->SetSlotContent(kAbs + 1, 0, 0, 0, 0, 0);
				S->WM->SimulateDrag(From, (Mn + Mx) * 0.5, 20);
			}
			else { S->Check(C30 < 0, TEXT("E4 навык 30 в лотке есть, ячейка панели есть")); }
		}));
		S->Steps.Add(Wait(1.0f));
		S->Steps.Add(Do([S]()
		{
			S->Check(S->Hb->GetSlot(kAbs + 1).Type == 0, FString::Printf(TEXT("E5 навык 30 на панель не лёг (в лотке: ячейка %d)"), S->CellOf(30)));
		}));

		// ═══ F. Откат: двойной щелчок — 2203 ═══
		S->Steps.Add(Do([S]()
		{
			const int32 Id = S->Sk->DebugTraySkill(S->DragCell());
			S->Sk->StartCooldown(Id);
			S->N2203 = S->Lines(2203);
			FVector2D P;
			S->CellCenter(S->DragCell(), P);
			S->WM->SimulateClickAt(P, 2);
		}));
		S->Steps.Add(Wait(0.12f));
		S->Steps.Add(Do([S]()
		{
			FVector2D P;
			S->CellCenter(S->DragCell(), P);
			S->WM->SimulateClickAt(P, 2);
		}));
		S->Steps.Add(Wait(0.5f));
		S->Steps.Add(Do([S]()
		{
			S->Check(S->Lines(2203) == S->N2203 + 1, FString::Printf(TEXT("F1 двойной щелчок при откате: строк 2203 +%d (ждём 1)"), S->Lines(2203) - S->N2203));
			S->Comm->CloseWindow();
		}));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSoulTraySelfTest(
	TEXT("asda.Soulmate.TraySelfTest"),
	TEXT("Самотест лотка навыков Дружбы (ключ asda.Soul.TrayFlow): ячейки, 5228/5238, подсказка, щелчки, перетаскивание, откат. Офлайн, карта мира. `en`, `shot=<папка>`, `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaSoulTrayTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		TSharedRef<FState> SP = MakeShared<FState>();
		SP->W = W; SP->bQuit = KA.bQuit;
		if (GI)
		{
			SP->Net = GI->GetSubsystem<UAsdaNetSubsystem>();
			SP->Sk = GI->GetSubsystem<UAsdaSoulmateSkillSubsystem>();
			SP->Hb = GI->GetSubsystem<UAsdaHotbarSubsystem>();
			SP->WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
			SP->Tip = GI->GetSubsystem<UAsdaItemTooltipSubsystem>();
			SP->Chat = GI->GetSubsystem<UAsdaChatSubsystem>();
			SP->Comm = GI->GetSubsystem<UAsdaCommunitySubsystem>();
			SP->Sw = GI->GetSubsystem<UAsdaSkillWindowSubsystem>();
		}
		if (!SP->Net.IsValid() || !SP->Sk.IsValid() || !SP->Hb.IsValid() || !SP->WM.IsValid() || !SP->Tip.IsValid() || !SP->Chat.IsValid()
			|| !SP->Comm.IsValid() || !SP->Sw.IsValid() || !IConsoleManager::Get().FindConsoleVariable(kKey))
		{
			AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем или ключа отката"));
			return;
		}
		for (const FString& P : KA.Pos)
		{
			if (P.Equals(TEXT("en"), ESearchCase::IgnoreCase)) { SP->bEn = true; }
			else if (P.StartsWith(TEXT("shot="), ESearchCase::IgnoreCase))
			{
				SP->ShotDir = FPaths::ConvertRelativePathToFull(P.Mid(5));
				IFileManager::Get().MakeDirectory(*SP->ShotDir, /*Tree*/true);
			}
		}
		SP->NetScope = MakeUnique<FAsdaNetSelfScope>(SP->Net.Get(), kCmd);
		SP->CVars = MakeUnique<FAsdaCVarScope>(W, kCmd);
		Build(SP);
		SP->StepT0 = W->GetTimeSeconds();
		SP->H = FWorldDelegates::OnWorldPostActorTick.AddLambda([SP](UWorld* InW, ELevelTick, float) { Tick(SP, InW); });
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
