// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ВОЛНЫ В6 РЫБАЛКИ (08.10.2026): строка «Навык рыбалки» 997 окна персонажа 900. [ПОРТ]
//   Отчёт — `Reports/owner_1008/fishing_impl_W3fix_W6.md`. Реверс — `fishing_parts/ui_book.md` §12.
//
// ЧТО ПРОВЕРЯЕТ (окно открыто настоящим `OpenWindow`, страница «Параметры»; пакеты — штатный `DebugFeedPacket` → `HandlePacket`
//   → делегат → очередь автомата рыбалки В1 → его Tick → сигнал `OnFishSkillChanged` → окно):
//   A. ★ W3fix2 (09.10.2026): умолчание ключа `asda.CharInfo.FishingZero` = 0 (A0); до навыка — эталонный формат и с навыком 0:
//      `0(Max.70) <FC=FFFFFF00>+ 0</F>` (A1); контроль отката ключом 1 (вариант 23.08) — голый «0» (A2).
//   B. 6164 (навык 123) → `123(Max.70) <FC=FFFFFF00>+ 0</F>`, узел — URichTextBlock (тег разобран, как у соседних строк),
//      геометрия живая (`GetCachedGeometry`).
//   C. 6172 код 2 (навык 124) → строка обновлена без перерисовки окна (`netOnItemUpgradeResult` пишет 997 сам).
//   D. Удочка в экип-слоте 9 (`itemCalcEnchantBonus@0xFC8010`): контроль — смена удочки БЕЗ перерисовки строку не меняет;
//      4064 (перерисовка `UI_FillEquipDetailPanel`) → бонус: 69657 (link 0, grp 10) + опция [3] (96, 7) = 17, опция [2]
//      (96, 50) НЕ считается (эталон берёт только 3 и 4); 69630 (link 1) → 0; 69659 (link 2, grp 100) → 100.
//      Числа удочек — `RawAssets/Data/ItemDB/item_list.csv` (подтип 10, вариант 1), не из кода.
//   Состояние (ключ, уровень, экип-слоты 9/10, базовые статы, вес, навык) восстанавливается в конце.
//
// Консоль: `asda.CharInfo.FishSkillSelfTest [delay=N] [quit]`. Только офлайн. Строки — «САМОТЕСТ-997», итог — «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "UI/AsdaCharaInfoSubsystem.h"
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "UI/AsdaInventorySubsystem.h"       // экип-слоты 9/10 (контейнер 3)
// FAsdaFmtText   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "UI/AsdaUiHelpers.h"                // WidgetAbsRect / IsShown
#include "World/AsdaFishingSubsystem.h"      // навык (только чтение)
#include "AsdaNetSubsystem.h"
// RefuseDebugWhenOnline — офлайн-гейт самотестов   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaBytes.h"
#include "Components/RichTextBlock.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "UI/AsdaItemRecordWriter.h"   // ★ U17 (В1 дедупа №2): синтетическая запись предмета 57 б

DEFINE_LOG_CATEGORY_STATIC(LogAsdaFishSkillTest, Log, All);

namespace AsdaFishSkillTestLocal
{
	constexpr int32 kLevel = 7;          // свой уровень на время прогона → «Max.70»
	constexpr int32 kRodA = 69657;       // удочка: подтип 10, вариант 1, linkCode 0, linkedGroupCode 10 (item_list.csv)
	constexpr int32 kRodB = 69630;       // удочка: linkCode 1, linkedGroupCode 20 — бонус НЕ даёт (эталон: только link 0/2)
	constexpr int32 kRodC = 69659;       // удочка: linkCode 2, linkedGroupCode 100
	// Целое из ключа консоли.
	static int32 GetCVar(const TCHAR* Name) { IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name); return V ? V->GetInt() : 0; }
	// Поставить ключ консоли.
	static void SetCVar(const TCHAR* Name, int32 Val) { AsdaCVarSave::SetForTest(Name, Val); }
	// Запись 57 б: @0 код, @4 контейнер, @5 i16 слот, @7 i16 −1, @9 i32 кол-во; опции — пары (i16 код, i16 значение) с +29.
	//   Opts: X — номер пары (0…4), Y — код опции, Z — значение.
	static TArray<uint8> Rec57(int32 Code, int32 Cont, int32 Slot, int32 Amount, const TArray<FIntVector>& Opts = TArray<FIntVector>())
	{
		// ★ U17: запись ячейки + пары заточки.
		FAsdaItemRecordWriter Rw;
		Rw.Code(Code).Container(Cont).Slot(Slot).Second(-1).Amount(Amount);
		for (const FIntVector& O : Opts) { Rw.EnchantPair(O.X, O.Y, O.Z); }
		return Rw.ToArray();
	}
	// Ожидаемая сырая строка 997 (формат эталона g:606463).
	static FString Want(int32 Skill, int32 MaxV, int32 Bonus) { return FString::Printf(TEXT("%d(Max.%d) <FC=FFFFFF00>+ %d</F>"), Skill, MaxV, Bonus); }
}

struct FAsdaFishSkillSelfTest
{
	int32 Ok = 0;     // пройдено
	int32 Fail = 0;   // провалено
	TArray<TFunction<void(UWorld*)>> Steps;   // шаги (по одному за тик планировщика — автомат В1 разбирает очередь в своём Tick)
	int32 CVarWas = 1;                        // asda.CharInfo.FishingZero до прогона
	uint8 LevelWas = 0;                       // SelfLevel до прогона
	int32 WeightWas = -1;                     // вес до прогона
	bool bSkillKnownWas = false;              // навык был известен до прогона
	int32 SkillWas = 0;                       // навык до прогона
	TArray<uint8> Eq9Was, Eq10Was;            // экип-слоты 9/10 до прогона
	UAsdaNetSubsystem::FAsdaSelfBaseStats BaseWas;   // базовые статы до прогона (4064 их перепишет)
	bool bWinWas = false;                     // окно 900 было открыто
	bool bSetupFailed = false;                // ★ 11.10.2026 (В3 S1): окно 900 не открылось — шаги не выполняются (иначе SelectStatPage по пустым узлам роняла игру)

	// Одна проверка.
	void Expect(bool b, const FString& Name, const FString& Detail = FString())
	{
		if (b) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaFishSkillTest, Log, TEXT("САМОТЕСТ-997 %s: %s%s%s"), b ? TEXT("ok") : TEXT("ПРОВАЛ"), *Name,
			Detail.IsEmpty() ? TEXT("") : TEXT(" — "), *Detail);
	}
	// Подсистемы.
	struct FSubs
	{
		UGameInstance* GI = nullptr; UAsdaNetSubsystem* Net = nullptr; UAsdaCharaInfoSubsystem* CI = nullptr;
		UAsdaInventorySubsystem* Inv = nullptr; UAsdaFishingSubsystem* Fish = nullptr;
		bool Ok() const { return GI && Net && CI && Inv && Fish; }
	};
	static FSubs Get(UWorld* W)
	{
		FSubs S; S.GI = W ? W->GetGameInstance() : nullptr;
		if (!S.GI) { return S; }
		S.Net = S.GI->GetSubsystem<UAsdaNetSubsystem>(); S.CI = S.GI->GetSubsystem<UAsdaCharaInfoSubsystem>();
		S.Inv = S.GI->GetSubsystem<UAsdaInventorySubsystem>(); S.Fish = S.GI->GetSubsystem<UAsdaFishingSubsystem>();
		return S;
	}
	// Сырая строка 997 (последняя запись через единую точку тегов).
	static FString Raw(const FSubs& S) { return S.CI->DebugFishSkillFmt().LastRaw; }
	// Положить запись в экип-слот (контейнер 3) — пустой массив очищает слот.
	static void PutEquip(const FSubs& S, int32 Slot, const TArray<uint8>& R)
	{
		if (FAsdaItemInstance* It = S.Inv->GetCellMutable(3, Slot))
		{
			if (R.Num() >= 57) { It->SetRaw(R.GetData(), R.Num()); } else { It->Clear(); }
		}
	}
	// 4064 (36 б нулей) — штатный стат-пакет: эталон на него перерисовывает окно (`UI_FillEquipDetailPanel`).
	static void Feed4064(const FSubs& S) { TArray<uint8> B; B.SetNumZeroed(36); S.Net->DebugFeedPacket(4064, B); }

	void Setup(UWorld* W)
	{
		using namespace AsdaFishSkillTestLocal;
		FSubs S = Get(W);
		if (!S.Ok()) { Expect(false, TEXT("нет подсистем")); return; }
		CVarWas = GetCVar(TEXT("asda.CharInfo.FishingZero"));
		LevelWas = S.Net->SelfLevel; WeightWas = S.Net->SelfWeightCur; BaseWas = S.Net->SelfBaseStats;
		bSkillKnownWas = S.Fish->IsFishingSkillKnown(); SkillWas = S.Fish->GetFishingSkill();
		if (const FAsdaItemInstance* It = S.Inv->GetCell(3, 9)) { Eq9Was = It->Raw; }
		if (const FAsdaItemInstance* It = S.Inv->GetCell(3, 10)) { Eq10Was = It->Raw; }
		bWinWas = S.CI->IsWindowOpen();
		// Сессия без навыка (выход в выбор персонажа обнуляет навык автомата В1 — `bSkillKnown = false`).
		S.Fish->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		SetCVar(TEXT("asda.CharInfo.FishingZero"), 0);
		S.Net->SelfLevel = kLevel;
		PutEquip(S, 9, TArray<uint8>());
		S.CI->OpenWindow();
		// ★ 11.10.2026 (В3 S1, загрязнение пакета): окно могло не открыться (гейт `uiWindow_OpenIfAllowed` — игровые окна убраны
		//   сценой прошлого самотеста). Тогда узлы страницы не собраны и `SelectStatPage` упала бы на пустом массиве — честный провал.
		if (!S.CI->IsWindowOpen())
		{
			Expect(false, TEXT("подготовка: окно 900 открыто"), TEXT("OpenWindow отклонён (игровые окна убраны сценой?) — шаги пропущены"));
			bSetupFailed = true;
			return;
		}
		S.CI->SwitchTabMode(0);
		S.CI->SelectStatPage(1);
	}

	void BuildSteps()
	{
		using namespace AsdaFishSkillTestLocal;
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			const UWidget* N = S.CI->DebugFishSkillFmt().Node;
			// A0: умолчание ключа (значение конструктора `TAutoConsoleVariable`, `IConsoleVariable::GetDefaultValue`) — 0.
			IConsoleVariable* Zv = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.CharInfo.FishingZero"));
			const FString Def = Zv ? Zv->GetDefaultValue() : FString(TEXT("<нет ключа>"));
			Expect(Def == TEXT("0"), TEXT("A0 W3fix2: умолчание asda.CharInfo.FishingZero = 0 (эталонный формат всегда)"),
				FString::Printf(TEXT("умолчание «%s», значение до прогона %d"), *Def, CVarWas));
			Expect(S.CI->IsWindowOpen() && N && AsdaUi::IsShown(N) && !S.Fish->IsFishingSkillKnown() && Raw(S) == Want(0, 70, 0),
				TEXT("A1 до навыка, FishingZero 0 (умолч.) → эталонный формат с навыком 0 (у оригинала строка до 6164 не пустая)"),
				FString::Printf(TEXT("«%s»"), *Raw(S)));
			SetCVar(TEXT("asda.CharInfo.FishingZero"), 1);
			S.CI->RefreshAll();
			Expect(Raw(S) == TEXT("0"), TEXT("A2 контроль отката FishingZero 1 (вариант 23.08) → голый «0» до навыка"),
				FString::Printf(TEXT("«%s»"), *Raw(S)));
			SetCVar(TEXT("asda.CharInfo.FishingZero"), 0);
			S.CI->RefreshAll();
			TArray<uint8> B; B.SetNumZeroed(4); AsdaBytes::WrI32(B.GetData(), 123);
			S.Net->DebugFeedPacket(6164, B);   // → очередь В1 → её Tick → OnFishSkillChanged
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			const UWidget* N = S.CI->DebugFishSkillFmt().Node;
			FVector2D Mn(0.0, 0.0), Mx(0.0, 0.0);
			const bool bGeo = N && AsdaUi::WidgetAbsRect(N, Mn, Mx) && !(Mx - Mn).IsNearlyZero();
			Expect(S.Fish->GetFishingSkill() == 123 && Raw(S) == Want(123, 70, 0), TEXT("B1 6164 (навык 123) → «123(Max.70) <FC=FFFFFF00>+ 0</F>»"),
				FString::Printf(TEXT("«%s»"), *Raw(S)));
			Expect(N && N->IsA<URichTextBlock>() && AsdaUi::IsShown(N) && bGeo,
				TEXT("B2 строка — URichTextBlock (тег <FC> разобран, как у соседних строк), живая геометрия"),
				FString::Printf(TEXT("%s (%.0f,%.0f)-(%.0f,%.0f)"), N ? *N->GetClass()->GetName() : TEXT("нет"), Mn.X, Mn.Y, Mx.X, Mx.Y));
			// C. 6172 код 2: навык 124; запись наживки = текущее содержимое слота 10 (инвентарь не меняется).
			TArray<uint8> B; B.SetNumZeroed(121);
			B[0] = 2; AsdaBytes::WrI16(B.GetData() + 1, 124); AsdaBytes::WrI16(B.GetData() + 3, (int16)FMath::Max(0, WeightWas));
			const TArray<uint8> Bait = (Eq10Was.Num() >= 57) ? Eq10Was : Rec57(0, 3, 10, -1);
			FMemory::Memcpy(B.GetData() + 7, Bait.GetData(), 57);
			S.Net->DebugFeedPacket(6172, B);
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Expect(S.Fish->GetFishingSkill() == 124 && Raw(S) == Want(124, 70, 0), TEXT("C1 6172 (навык 124) → строка обновлена сразу (`netOnItemUpgradeResult`)"),
				FString::Printf(TEXT("«%s»"), *Raw(S)));
			// D. Удочка 69657 + опции: [2] (96, 50) — не считается, [3] (96, 7) — считается.
			PutEquip(S, 9, Rec57(kRodA, 3, 9, 1, { FIntVector(2, 96, 50), FIntVector(3, 96, 7) }));
			Expect(Raw(S) == Want(124, 70, 0), TEXT("D0 контроль: смена удочки без перерисовки окна строку не меняет (эталон пишет 997 только в 3 местах)"),
				FString::Printf(TEXT("«%s»"), *Raw(S)));
			Feed4064(S);
			Expect(Raw(S) == Want(124, 70, 17), TEXT("D1 4064 → перерисовка: удочка 69657 (link 0, +10) + опция [3] (96, +7) = 17; опция [2] не считается"),
				FString::Printf(TEXT("«%s»"), *Raw(S)));
			PutEquip(S, 9, Rec57(kRodB, 3, 9, 1));
			Feed4064(S);
			Expect(Raw(S) == Want(124, 70, 0), TEXT("D2 контроль: удочка 69630 (linkCode 1) бонуса не даёт"), FString::Printf(TEXT("«%s»"), *Raw(S)));
			PutEquip(S, 9, Rec57(kRodC, 3, 9, 1, { FIntVector(4, 96, 3), FIntVector(1, 96, 40) }));
			Feed4064(S);
			Expect(Raw(S) == Want(124, 70, 103), TEXT("D3 удочка 69659 (linkCode 2, +100) + опция [4] (96, +3) = 103; опция [1] не считается"),
				FString::Printf(TEXT("«%s»"), *Raw(S)));
			PutEquip(S, 9, TArray<uint8>());
			Feed4064(S);
			Expect(Raw(S) == Want(124, 70, 0), TEXT("D4 удочки нет → бонус 0, формат эталона"),
				FString::Printf(TEXT("«%s»"), *Raw(S)));
		});
	}

	// Восстановление.
	void Finish(UWorld* W)
	{
		FSubs S = Get(W);
		if (!S.Ok()) { AsdaCVarSave::RestoreAll(TEXT("asda.CharInfo.FishSkillSelfTest")); return; }   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключ — и на этом выходе
		PutEquip(S, 9, Eq9Was);
		PutEquip(S, 10, Eq10Was);
		S.Net->SelfBaseStats = BaseWas;
		S.Net->SelfLevel = LevelWas;
		S.Net->SetSelfWeight(WeightWas, -1, TEXT("самотест 997: восстановление"));
		AsdaCVarSave::RestoreAll(TEXT("asda.CharInfo.FishSkillSelfTest"));   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключ CharInfo.FishingZero — значение + SetBy (прежде — CVarWas с SetByConsole)
		S.Fish->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		if (bSkillKnownWas)
		{
			TArray<uint8> B; B.SetNumZeroed(4); AsdaBytes::WrI32(B.GetData(), SkillWas);
			S.Net->DebugFeedPacket(6164, B);
		}
		if (!bWinWas) { S.CI->CloseWindow(); } else { S.CI->RefreshAll(); }
	}
};

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GAsdaFishSkillSelfTest(
		TEXT("asda.CharInfo.FishSkillSelfTest"),
		TEXT("asda.CharInfo.FishSkillSelfTest [delay=N] [quit] — ОФЛАЙН-самотест строки «Навык рыбалки» 997 (В6 рыбалки): «0» до навыка, ")
		TEXT("6164/6172 → эталонный формат «%d(Max.%d) <FC=FFFFFF00>+ %d</F>», бонус надетой удочки (`itemCalcEnchantBonus`) по 4064"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.CharInfo.FishSkillSelfTest"));   // ★ С1 (ревизия wave0_REVIEW.md, 11.10.2026): мир ушёл до итога — ключи вернёт сторож
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.CharInfo.FishSkillSelfTest"), KitArgs)) { return; }
			float Delay = 0.f;
			Delay = AsdaSelfTestKit::DelayArgOr(Args, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (был отдельный цикл по аргументам с одной этой веткой)
			const bool bQuit = Args.Contains(TEXT("quit"));
			TWeakObjectPtr<UWorld> WW(W);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit](float) -> bool
			{
				UWorld* W2 = WW.Get();
				if (!W2) { return false; }
				if (AsdaSelfTestKit::RefuseDeferred(W2, TEXT("asda.CharInfo.FishSkillSelfTest"), bQuit)) { return false; }   // Р-2: офлайн повторно — на отложенном старте
				TSharedRef<FAsdaFishSkillSelfTest> T = MakeShared<FAsdaFishSkillSelfTest>();
				T->Setup(W2);
				T->BuildSteps();
				if (T->bSetupFailed) { T->Steps.Reset(); }   // ★ В3 S1: подготовка не удалась — сразу к итогу
				TSharedRef<int32> Idx = MakeShared<int32>(0);
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, T, Idx, bQuit](float) -> bool
				{
					UWorld* W3 = WW.Get();
					if (!W3) { AsdaCVarSave::RestoreAll(TEXT("asda.CharInfo.FishSkillSelfTest")); return false; }   // ★ С1 (ревизия wave0_REVIEW.md, 11.10.2026): мир ушёл до итога — ключи вернуть и на этом выходе
					if (*Idx < T->Steps.Num()) { T->Steps[(*Idx)++](W3); return true; }
					T->Finish(W3);
					UE_LOG(LogAsdaFishSkillTest, Log, TEXT("САМОТЕСТ-997 ИТОГ: ok=%d fail=%d"), T->Ok, T->Fail);
					AsdaSelfTestKit::Quit(W3, bQuit);
					return false;
				}), 0.4f);
				return false;
			}), Delay);
			UE_LOG(LogAsdaFishSkillTest, Log, TEXT("САМОТЕСТ-997: запуск через %.1f с"), Delay);
		}));
}

#endif // !UE_BUILD_SHIPPING
