// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ ОТКАТОВ НАВЫКОВ (25.10.2026, волна «все откаты точные»): `asda.SelfTest.SkillCooldowns [quit]`. Офлайн
//   (`-AsdaNoNet`), на карте мира. Реальный путь: выученные навыки — кадром 5251 (34-б записи с откатом +4 от сервера),
//   применение — кадром 5271 (результат 1, свой userIdx) через общий разбор входящих, состояние 135 — конструктором записи
//   эффекта (как 5453), конец отката от сервера — кадром 5274, портал — настоящий `OpenLevel`, восстановление — 5465.
//   Эталон — `skillStartCooldown@0xE579E0` / `skillStartGroupCooldown@0xE57860` (шапка в AsdaSkillUseSubsystem.cpp).
//   Проверяются:
//     A) длительность = запись выученного +4 в мс, без округления до секунд (прежнее — числом, ключ SkillEtalon 0);
//     B) снижение состоянием 135 (уровень · s1PerLevel + s1Base навыка записи) на всю группу `.sif +0xA7`, пол 1000 мс;
//     C) пара 930/1027 (если обе записи есть в skill_info);
//     D) панель быстрого доступа и книга навыков горят тем же остатком;
//     E) 5274 снимает откат (ключ Skill5274Sync; 0 — как эталон, не снимает);
//     F) портал: остаток идёт по реальным часам; 5465 — снимок (навыки вне пакета сняты);
//     G) ★ ревизия wave_1025b 1.1 — порядок прихода в одном кадре (ключ `asda.Cooldown.ArrivalOrder`): 5465→5271 — взвод остаётся,
//        5271→5465 — снимок его стирает, 5465→5274 — откат снят и не возвращается (прежнее, ключ 0, — возвращался), 5271→5274 — не взведён;
//     H) ★ ревизия 2.1 — откат 0 мс (запись +4 = 0, снижений нет) идущий откат группы не снимает (эталон дописывает пустую запись).
//   Итог: «ОТКАТЫ-НАВЫКИ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaSelf.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaSkillWindowSubsystem.h"
#include "UI/AsdaSkillDatabase.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/Ticker.h"
#include "Internationalization/Regex.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSkCdTest, Log, All);

namespace AsdaSkCdTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.SkillCooldowns");
	constexpr int32 kHotSlot = 2;          // абсолютный слот панели под навык X

	// Состояние прогона (живёт между тиками и через смену мира).
	struct FState
	{
		TWeakObjectPtr<UGameInstance> GI;   // экземпляр игры
		bool   bQuit = false;               // `quit` в конце
		int32  Ok = 0, Fail = 0;            // счёт
		int32  Stage = 0;                   // шаг
		double StageT = 0.0;                // реальное время входа в шаг
		int32  Self = -1;                   // свой userIdx
		int32  X = -1;                      // навык узла книги без группы (база)
		int32  XMs = 0;                     // откат X, присланный «сервером» (+4), мс — не кратен секунде
		int32  C = -1;                      // навык с группой `.sif +0xA7`, содержащей его самого
		int32  K = -1;                      // навык записи состояния 135 (источник снижения)
		int32  KLevel = 3;                  // его уровень
		int32  V135 = 0;                    // ожидаемое снижение состоянием 135
		bool   b930 = false;                // есть 930 и 1027
		double TUseX = 0.0;                 // реальное время последнего применения X
		int32  M = -1;                      // член группы C, отличный от C (проверка H)
		double RM0 = 0.0, RC0 = 0.0;        // остатки M и C перед 5271 с откатом 0 мс
		double TMark = 0.0;                 // реальное время этого 5271
		UPTRINT OldWorld = 0;               // старый мир (ждём новый)
	};

	static void Check(FState& S, bool b, const FString& What)
	{
		if (b) { ++S.Ok;   UE_LOG(LogAsdaSkCdTest, Log,   TEXT("ОТКАТЫ-НАВЫКИ ok   %s"), *What); }
		else   { ++S.Fail; UE_LOG(LogAsdaSkCdTest, Error, TEXT("ОТКАТЫ-НАВЫКИ FAIL %s"), *What); }
	}

	static double Find(const FString& Dump, const FString& Pattern)
	{
		const FRegexPattern Rx(Pattern);
		FRegexMatcher M(Rx, Dump);
		return M.FindNext() ? FCString::Atod(*M.GetCaptureGroup(1)) : -1.0;
	}

	// 5251: 34-б записи {i16 код, u8 уровень, u8 «можно вкачивать», i32 откат мс, …, @10 i16 треб. уровень}.
	static void Feed5251(UAsdaNetSubsystem* Net, const TArray<FIntVector>& Rows)   // X = код, Y = уровень, Z = откат мс
	{
		TArray<uint8> B; B.SetNumZeroed(34 * Rows.Num());
		for (int32 i = 0; i < Rows.Num(); ++i)
		{
			uint8* P = B.GetData() + 34 * i;
			AsdaBytes::WrI16(P, Rows[i].X);
			P[2] = (uint8)Rows[i].Y;
			P[3] = 1;
			AsdaBytes::WrI32(P + 4, Rows[i].Z);
		}
		AsdaTestNet::DebugFeedPacket(Net, 5251, B);
	}

	// 5271 «каст кончился»: u8 результат, i16 userIdx, i16 навык, i16 флаг.
	static void Feed5271(UAsdaNetSubsystem* Net, int32 Self, int32 Skill)
	{
		TArray<uint8> B; B.SetNumZeroed(7);
		B[0] = 1;
		AsdaBytes::WrI16(&B[1], Self);
		AsdaBytes::WrI16(&B[3], Skill);
		AsdaBytes::WrI16(&B[5], 1);
		AsdaTestNet::DebugFeedPacket(Net, 5271, B);
	}

	static void Feed5274(UAsdaNetSubsystem* Net, int32 Skill)
	{
		TArray<uint8> B; B.SetNumZeroed(2);
		AsdaBytes::WrI16(B.GetData(), Skill);
		AsdaTestNet::DebugFeedPacket(Net, 5274, B);
	}

	// 5465: 360 б — записи {i32 навык, i16 остаток с}, терминатор −1 (вторая половина — откаты предметов, пустая).
	static void Feed5465(UAsdaNetSubsystem* Net, const TArray<FIntPoint>& Rows)   // X = навык, Y = остаток, с
	{
		TArray<uint8> B; B.SetNumZeroed(360);
		int32 Off = 0;
		for (const FIntPoint& R : Rows) { AsdaBytes::WrI32(B.GetData() + Off, R.X); AsdaBytes::WrI16(B.GetData() + Off + 4, R.Y); Off += 6; }
		AsdaBytes::WrI32(B.GetData() + Off, -1);
		AsdaBytes::WrI32(B.GetData() + 180, -1);
		AsdaTestNet::DebugFeedPacket(Net, 5465, B);
	}

	static void Finish(FState& S, UWorld* W)
	{
		if (UGameInstance* G = S.GI.Get())
		{
			if (UAsdaHotbarSubsystem* Hb = G->GetSubsystem<UAsdaHotbarSubsystem>()) { Hb->ClearSlot(kHotSlot); Hb->ClearSkillCooldowns(); }
			if (UAsdaSkillWindowSubsystem* Sw = G->GetSubsystem<UAsdaSkillWindowSubsystem>()) { Sw->ResetAllSkillCooldowns(); Sw->SetJobForTest(-1, -1); }
			if (UAsdaEffectsSubsystem* Fx = G->GetSubsystem<UAsdaEffectsSubsystem>()) { if (S.Self >= 0) { Fx->RemoveBySkillState(S.Self, 135, 0); } }
		}
		AsdaCVarSave::RestoreAll(kCmd);
		AsdaSelfTestKit::Finish(W, kCmd, S.bQuit, LogAsdaSkCdTest, TEXT("ОТКАТЫ-НАВЫКИ"), S.Ok, S.Fail);
	}

	static bool Step(TSharedRef<FState> SR)
	{
		FState& S = *SR;
		UGameInstance* GI = S.GI.Get();
		if (!GI) { AsdaCVarSave::RestoreAll(kCmd); return false; }
		UWorld* W = GI->GetWorld();
		UAsdaHotbarSubsystem* Hb = GI->GetSubsystem<UAsdaHotbarSubsystem>();
		UAsdaSkillWindowSubsystem* Sw = GI->GetSubsystem<UAsdaSkillWindowSubsystem>();
		UAsdaNetSubsystem* Net = AsdaTestNet::Get(GI);
		UAsdaEffectsSubsystem* Fx = GI->GetSubsystem<UAsdaEffectsSubsystem>();
		const UAsdaSkillDatabase* Db = Sw ? Sw->GetDatabase() : nullptr;
		const double Now = FPlatformTime::Seconds();
		const double InStage = Now - S.StageT;
		auto Next = [&S, Now]() { ++S.Stage; S.StageT = Now; };
		if (!Hb || !Sw || !Net || !Fx || !Db) { Check(S, false, TEXT("подсистемы")); Finish(S, W); return false; }
		auto Remain = [Sw](int32 Code) { return (double)Sw->GetCooldownRemainSec(Code); };
		auto Floor1000 = [](int32 A3, int32 V) { if (V) { A3 -= V; if (A3 <= 1000) { A3 = 1000; } } return A3; };

		switch (S.Stage)
		{
		case 0:   // вход в мир офлайн и окно книги (класс офлайн неизвестен — подмена класса 1, иначе дерево пустое)
			if (InStage < 3.0) { return true; }
			Sw->SetJobForTest(1, -1);
			Sw->OpenWindow();
			Next();
			return true;
		case 1:   // выбор навыков и 5251
			if (InStage < 1.0) { return true; }
			{
				S.Self = Fx->SelfUserIdx();
				Check(S, S.Self >= 0, FString::Printf(TEXT("0 свой userIdx %d"), S.Self));
				// X — навык дерева текущей полосы (вкладка 0: его узел в книге) с откатом и без группы.
				Sw->SetTree(0);
				TArray<int32> TreeCodes;
				Db->GetTreeSkills(Sw->GetSelfBand(), 0, TreeCodes);
				for (int32 Code : TreeCodes)
				{
					const FAsdaSkillDef* D = Code > 0 ? Db->Get(Code) : nullptr;
					if (S.X < 0 && D && D->CooldownMs >= 3000 && D->ChainSkillIds.Num() == 0) { S.X = Code; }
				}
				UE_LOG(LogAsdaSkCdTest, Log, TEXT("ОТКАТЫ-НАВЫКИ дерево 0 полосы %d: навыков %d"), Sw->GetSelfBand(), TreeCodes.Num());
				for (int32 Code = 500; Code < 1300; ++Code)
				{
					const FAsdaSkillDef* D = Db->Get(Code);
					if (!D) { continue; }
					if (S.C < 0 && D->CooldownMs >= 3000 && D->ChainSkillIds.Contains(Code) && D->ChainSkillIds.Num() >= 2 && Code != S.X) { S.C = Code; }
					const int32 V = S.KLevel * D->S1PerLevel + D->S1Base;
					if (S.K < 0 && V >= 200 && V <= 2000 && Code != S.X && Code != S.C && Code != 930 && Code != 1027) { S.K = Code; S.V135 = V; }
				}
				S.b930 = Db->Get(930) && Db->Get(1027);
				const FAsdaSkillDef* DX = Db->Get(S.X);
				S.XMs = DX ? DX->CooldownMs + 350 : 0;   // «сервер» прислал не кратное секунде (эталон кладёт как есть)
				Check(S, S.X > 0 && S.C > 0 && S.K > 0, FString::Printf(TEXT("0 навыки: X=%d (откат .sif %d мс), группа C=%d, состояние 135 от K=%d (−%d мс), 930/1027 %s"),
					S.X, DX ? DX->CooldownMs : 0, S.C, S.K, S.V135, S.b930 ? TEXT("есть") : TEXT("нет")));
				if (S.X <= 0 || S.C <= 0 || S.K <= 0) { Finish(S, W); return false; }
				TArray<FIntVector> Rows = { FIntVector(S.X, 1, S.XMs), FIntVector(S.C, 1, Db->Get(S.C)->CooldownMs), FIntVector(S.K, S.KLevel, Db->Get(S.K)->CooldownMs) };
				for (int32 Id : Db->Get(S.C)->ChainSkillIds) { if (Id != S.C && Db->Get(Id)) { Rows.Add(FIntVector(Id, 1, Db->Get(Id)->CooldownMs)); } }
				if (S.b930) { Rows.Add(FIntVector(930, 1, Db->Get(930)->CooldownMs)); Rows.Add(FIntVector(1027, 2, Db->Get(1027)->CooldownMs)); }
				Feed5251(Net, Rows);
				Hb->SetSlotContent(kHotSlot, (uint8)EAsdaHotbarSlotType::Skill, 0, 0, 0, S.X);
			}
			Next();
			return true;
		case 2:   // A: база
			if (InStage < 0.6) { return true; }
			{
				const FAsdaLearnedSkill* L = Sw->FindLearned(S.X);
				Check(S, L && L->CooldownMs == S.XMs, FString::Printf(TEXT("A1 5251: запись X +4 = %d мс (прислано %d)"), L ? L->CooldownMs : -1, S.XMs));
				// Прежнее поведение (ключ 0) — числом, затем эталон.
				AsdaCVarSave::SetForTest(TEXT("asda.Cooldown.SkillEtalon"), 0);
				Feed5271(Net, S.Self, S.X);
			}
			Next();
			return true;
		case 3:
			if (InStage < 0.3) { return true; }
			{
				const FAsdaSkillDef* DX = Db->Get(S.X);
				const double Old = FMath::Max(1000, (DX->CooldownMs / 1000) * 1000) / 1000.0;
				const double R = Remain(S.X);
				Check(S, R > 0.0 && R <= Old + 0.01 && R > Old - 0.6,
					FString::Printf(TEXT("A2 [прежнее, SkillEtalon 0] откат X %.2f с (≈ %.0f с: .sif, округлено до секунд), а сервер прислал %.3f с"), R, Old, S.XMs / 1000.0));
				AsdaCVarSave::SetForTest(TEXT("asda.Cooldown.SkillEtalon"), 1);
				Feed5271(Net, S.Self, S.X);
				S.TUseX = FPlatformTime::Seconds();
			}
			Next();
			return true;
		case 4:
			if (InStage < 0.3) { return true; }
			{
				const double Want = S.XMs / 1000.0 - (Now - S.TUseX);
				Check(S, FMath::Abs(Remain(S.X) - Want) < 0.08,
					FString::Printf(TEXT("A3 эталон: откат X %.3f с = запись +4 %.3f с минус прошедшее (без округления)"), Remain(S.X), S.XMs / 1000.0));
				// B: состояние 135 на себе → группа C.
				Fx->AddOrUpdateSkill(S.Self, 135, S.K, 1, 0, 300, 0);
				Feed5271(Net, S.Self, S.C);
				if (S.b930) { Feed5271(Net, S.Self, 930); }
			}
			Next();
			return true;
		case 5:
			if (InStage < 0.3) { return true; }
			{
				const int32 BaseC = Db->Get(S.C)->CooldownMs;
				const int32 WantMs = Floor1000(BaseC, S.V135);
				for (int32 Id : Db->Get(S.C)->ChainSkillIds)
				{
					if (!Db->Get(Id)) { continue; }
					const double R = Remain(Id);
					Check(S, FMath::Abs(R - WantMs / 1000.0) < 0.4,
						FString::Printf(TEXT("B1 группа %d: навык %d — %.2f с = (%d − %d) мс, пол 1000 (состояние 135 от %d ур. %d)"),
							S.C, Id, R, BaseC, S.V135, S.K, S.KLevel));
				}
				if (S.b930)
				{
					const FAsdaSkillDef* R930 = Db->Get(930); const FAsdaSkillDef* R1027 = Db->Get(1027);
					const int32 V930 = (int32)(int64)((double)R930->S1Base * ((double)R1027->MsBase + (double)(2 * R1027->MsPerLevel) / 100.0));
					const int32 Want930 = Floor1000(Floor1000(R930->CooldownMs, S.V135), V930);
					Check(S, FMath::Abs(Remain(930) - Want930 / 1000.0) < 0.4,
						FString::Printf(TEXT("C1 930: %.2f с = %d − 135:%d − 1027:%d мс (пол 1000 после каждого)"), Remain(930), R930->CooldownMs, S.V135, V930));
				}
			}
			Next();
			return true;
		case 6:   // D: панель и книга
			if (InStage < 0.7) { return true; }
			{
				const double Want = Remain(S.X);
				const double Rh = Find(Hb->DumpCooldownCurtains(), FString::Printf(TEXT("абс.слот %d\\s+тип 3 id %d\\s+состояние 1 осталось ([0-9.]+)"), kHotSlot, S.X));
				const double Rb = Find(Sw->DumpCooldownCurtains(), FString::Printf(TEXT("узел\\s*\\d+\\s+навык %d\\s+состояние 1 осталось ([0-9.]+)"), S.X));
				Check(S, Rh >= 0 && FMath::Abs(Rh - Want) < 0.3, FString::Printf(TEXT("D1 панель слот %d: шторка навыка X, осталось %.2f с (реестр %.2f)"), kHotSlot, Rh, Want));
				Check(S, Rb >= 0 && FMath::Abs(Rb - Want) < 0.3, FString::Printf(TEXT("D2 книга навыков: узел X горит, осталось %.2f с (реестр %.2f)"), Rb, Want));
				// E: 5274 — сервер отпустил X раньше (скидка, которой клиент не знает).
				AsdaCVarSave::SetForTest(TEXT("asda.Cooldown.Skill5274Sync"), 0);
				Feed5274(Net, S.X);
				Check(S, Remain(S.X) > 0.5, FString::Printf(TEXT("E1 [как эталон, Skill5274Sync 0] 5274 не снял откат X (осталось %.2f с)"), Remain(S.X)));
				AsdaCVarSave::SetForTest(TEXT("asda.Cooldown.Skill5274Sync"), 1);
				Feed5274(Net, S.X);
				Check(S, Remain(S.X) <= 0.0 && Hb->QuerySkillCooldown(S.X).State == 0, TEXT("E2 5274 снял откат X в книге и на панели"));
				// F: портал с идущим откатом. Откат X длиннее перехода (сам переход с загрузкой — до ~8 с): сервер обновил запись +4.
				S.XMs = 30350;
				Feed5251(Net, { FIntVector(S.X, 1, S.XMs) });
			}
			Next();
			return true;
		case 7:
			if (InStage < 0.5) { return true; }
			{
				const FAsdaLearnedSkill* L = Sw->FindLearned(S.X);
				Check(S, L && L->CooldownMs == S.XMs, FString::Printf(TEXT("F0 повторный 5251: запись X +4 = %d мс"), L ? L->CooldownMs : -1));
				Fx->RemoveBySkillState(S.Self, 135, 0);   // состояние 135 снято — дальше откат X без снижения
				Feed5271(Net, S.Self, S.X);
				S.TUseX = FPlatformTime::Seconds();
			}
			Next();
			return true;
		case 8:   // 5271 разбирается в тике диспетчера — портал только после взвода
			if (InStage < 0.4) { return true; }
			{
				Check(S, Remain(S.X) > 29.0, FString::Printf(TEXT("F0 перед порталом откат X %.2f с"), Remain(S.X)));
				S.OldWorld = (UPTRINT)W;
				UGameplayStatics::OpenLevel(W, FName(TEXT("alpen")), true, TEXT("game=/Script/AsdaReborn.AsdaWorldGameMode"));
			}
			Next();
			return true;
		case 9:
			if (!W || (UPTRINT)W == S.OldWorld || !W->HasBegunPlay())
			{
				if (InStage < 120.0) { return true; }
				Check(S, false, TEXT("F0 новый мир не поднялся"));
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
		case 10:
			if (InStage < 3.0) { return true; }
			{
				const double Want = S.XMs / 1000.0 - (Now - S.TUseX);
				Check(S, Want > 0.0 && FMath::Abs(Remain(S.X) - Want) < 0.15,
					FString::Printf(TEXT("F1 после портала до 5465: откат X %.2f с = %.2f с (реальные часы)"), Remain(S.X), Want));
				// 5465 после портала: сервер сохранил X = 4 с; группа C в снимке отсутствует.
				TArray<uint8> B; B.SetNumZeroed(360);
				AsdaBytes::WrI32(B.GetData(), S.X); AsdaBytes::WrI16(B.GetData() + 4, 4);
				AsdaBytes::WrI32(B.GetData() + 6, -1);
				AsdaBytes::WrI32(B.GetData() + 180, -1);
				AsdaTestNet::DebugFeedPacket(Net, 5465, B);
				Sw->OpenWindow();
			}
			Next();
			return true;
		case 11:
			if (InStage < 1.0) { return true; }
			{
				Check(S, FMath::Abs(Remain(S.X) - (4.0 - InStage)) < 0.3, FString::Printf(TEXT("F2 5465: откат X %.2f с (сервер 4 с минус %.2f)"), Remain(S.X), InStage));
				Check(S, Remain(S.C) <= 0.0, FString::Printf(TEXT("F3 5465 — снимок: группа C (вне пакета) снята (%.2f)"), Remain(S.C)));
				const double Want = Remain(S.X);
				const double Rh = Find(Hb->DumpCooldownCurtains(), FString::Printf(TEXT("абс.слот %d\\s+тип 3 id %d\\s+состояние 1 осталось ([0-9.]+)"), kHotSlot, S.X));
				const double Rb = Find(Sw->DumpCooldownCurtains(), FString::Printf(TEXT("узел\\s*\\d+\\s+навык %d\\s+состояние 1 осталось ([0-9.]+)"), S.X));
				Check(S, Rh >= 0 && FMath::Abs(Rh - Want) < 0.3, FString::Printf(TEXT("F4 после портала панель: %.2f с (реестр %.2f)"), Rh, Want));
				Check(S, Rb >= 0 && FMath::Abs(Rb - Want) < 0.3, FString::Printf(TEXT("F4 после портала книга: %.2f с (реестр %.2f)"), Rb, Want));
			}
			Next();
			return true;
		// ── G: порядок прихода в одном кадре (ревизия 1.1). Пакеты одного шага приходят до тиков книги и диспетчера. ──
		case 12:
			if (InStage < 0.2) { return true; }
			Feed5465(Net, { FIntPoint(S.X, 10) });
			Feed5271(Net, S.Self, S.C);
			Next();
			return true;
		case 13:
			if (InStage < 0.4) { return true; }
			{
				const double BaseC = Db->Get(S.C)->CooldownMs / 1000.0;
				Check(S, Remain(S.C) > BaseC - 0.8 && Remain(S.X) > 9.0 && Remain(S.X) <= 10.0,
					FString::Printf(TEXT("G1 5465 → 5271 в одном кадре: группа C взведена (%.2f с из %.2f), X из снимка %.2f с"), Remain(S.C), BaseC, Remain(S.X)));
				Feed5271(Net, S.Self, S.X);
				Feed5465(Net, { FIntPoint(S.C, 6) });
			}
			Next();
			return true;
		case 14:
			if (InStage < 0.4) { return true; }
			Check(S, Remain(S.X) <= 0.0 && FMath::Abs(Remain(S.C) - (6.0 - InStage)) < 0.4,
				FString::Printf(TEXT("G2 5271 → 5465 в одном кадре: снимок стёр взвод X (%.2f), C из снимка %.2f с"), Remain(S.X), Remain(S.C)));
			Feed5465(Net, { FIntPoint(S.X, 12) });
			Feed5274(Net, S.X);
			Check(S, Remain(S.X) <= 0.0, FString::Printf(TEXT("G3 5465 → 5274: откат X снят сразу (%.2f)"), Remain(S.X)));
			Next();
			return true;
		case 15:
			if (InStage < 0.4) { return true; }
			Check(S, Remain(S.X) <= 0.0, FString::Printf(TEXT("G3 5465 → 5274: после тиков откат X не вернулся (%.2f)"), Remain(S.X)));
			// Прежнее поведение (ключ 0): снимок ждал тика книги и возвращал снятый 5274 откат.
			AsdaCVarSave::SetForTest(TEXT("asda.Cooldown.ArrivalOrder"), 0);
			Feed5465(Net, { FIntPoint(S.X, 12) });
			Feed5274(Net, S.X);
			Next();
			return true;
		case 16:
			if (InStage < 0.4) { return true; }
			Check(S, Remain(S.X) > 11.0, FString::Printf(TEXT("G3' [прежнее, ArrivalOrder 0] 5465 → 5274: откат X вернулся тиком книги (%.2f с) — дефект виден"), Remain(S.X)));
			AsdaCVarSave::SetForTest(TEXT("asda.Cooldown.ArrivalOrder"), 1);
			Feed5271(Net, S.Self, S.X);
			Feed5274(Net, S.X);
			Next();
			return true;
		case 17:
			if (InStage < 0.4) { return true; }
			Check(S, Remain(S.X) <= 0.0 && Hb->QuerySkillCooldown(S.X).State == 0,
				FString::Printf(TEXT("G4 5271 → 5274 в одном кадре: откат X не взведён (%.2f)"), Remain(S.X)));
			// H: группа C в откате с полной длительностью.
			for (int32 Id : Db->Get(S.C)->ChainSkillIds) { if (S.M < 0 && Id >= 0 && Id != S.C && Db->Get(Id)) { S.M = Id; } }
			Feed5271(Net, S.Self, S.C);
			Next();
			return true;
		case 18:
			if (InStage < 0.4) { return true; }
			Check(S, S.M > 0 && Remain(S.M) > 1.5, FString::Printf(TEXT("H0 член группы M=%d в откате %.2f с"), S.M, S.M > 0 ? Remain(S.M) : -1.0));
			if (S.M <= 0) { Finish(S, W); return false; }
			Feed5251(Net, { FIntVector(S.C, 1, 0) });   // «сервер»: запись C +4 = 0 мс
			Next();
			return true;
		case 19:
			if (InStage < 0.4) { return true; }
			{
				const FAsdaLearnedSkill* L = Sw->FindLearned(S.C);
				Check(S, L && L->CooldownMs == 0, FString::Printf(TEXT("H1 5251: запись C +4 = %d мс"), L ? L->CooldownMs : -1));
				S.RM0 = Remain(S.M); S.RC0 = Remain(S.C); S.TMark = FPlatformTime::Seconds();
				Feed5271(Net, S.Self, S.C);
			}
			Next();
			return true;
		case 20:
			if (InStage < 0.4) { return true; }
			{
				const double Gone = Now - S.TMark;
				Check(S, Remain(S.M) > 0.0 && FMath::Abs(Remain(S.M) - (S.RM0 - Gone)) < 0.2,
					FString::Printf(TEXT("H2 откат 0 мс: член M идёт дальше %.2f с (было %.2f, прошло %.2f)"), Remain(S.M), S.RM0, Gone));
				Check(S, Remain(S.C) > 0.0 && FMath::Abs(Remain(S.C) - (S.RC0 - Gone)) < 0.2,
					FString::Printf(TEXT("H3 откат 0 мс: сам C идёт дальше %.2f с (было %.2f)"), Remain(S.C), S.RC0));
			}
			Finish(S, W);
			return false;
		default:
			Finish(S, W);
			return false;
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestSkillCooldowns(
	TEXT("asda.SelfTest.SkillCooldowns"),
	TEXT("Самотест откатов навыков: 5251 (+4 откат), 5271 применение, снижения 135 и 930/1027, группа .sif +0xA7, панель и книга, "
	     "5274, портал OpenLevel, 5465 снимок, порядок прихода 5465/5271/5274 в одном кадре, откат 0 мс. Офлайн, на карте мира. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaSkCdTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет GameInstance")); return; }
		for (const TCHAR* K : { TEXT("asda.Cooldown.RealClock"), TEXT("asda.Cooldown.CurtainTotal"), TEXT("asda.Cooldown.Snapshot5465"),
		                        TEXT("asda.Cooldown.SkillEtalon"), TEXT("asda.Cooldown.Skill5274Sync"), TEXT("asda.Cooldown.ArrivalOrder") })
		{
			AsdaCVarSave::SetForTest(K, 1);
		}
		if (GEngine)
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256 CdTest"));
		}
		TSharedRef<FState> SR = MakeShared<FState>();
		SR->GI = GI;
		SR->bQuit = KA.bQuit;
		SR->StageT = FPlatformTime::Seconds();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([SR](float) -> bool { return Step(SR); }), 0.1f);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
