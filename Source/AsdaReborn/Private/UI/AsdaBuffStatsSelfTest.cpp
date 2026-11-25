// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «БАФФ АТАКИ/ЗАЩИТЫ → ОКНО ХАРАКТЕРИСТИК» (04.11.2026, жалоба игрока «Enhanced Concentration Witcheryer (598)
//   не меняет атаку и защиту»). [ПОРТ] Только офлайн. Команда `asda.CharInfo.SelfTestBuffStats [delay=N] [quit]`.
//   Путь игрока: окно 900 открыто штатным `OpenWindow` (страница «Параметры»), пакеты — штатный `DebugFeedPacket` (= `HandlePacket`
//   сокета) с ЖИВЫМИ телами журнала игрока 04.11 10:59:10 (5272 состояния 21/41 с флагом навыка 598 и 4060 с пересчитанным блоком).
//   Эталон: сервер на наложение/снятие состояния 21/41 зовёт обработчик `sub_488840`/`sub_4888C0` (пересчёт `SetTotalAttackMin/Max`,
//   защиты) и шлёт 4060 владельцу (GameServer.c:46910-46930 → `sub_52C170`); клиент кладёт блок как есть (`RecvPacket_CharStatBlockA`)
//   и перерисовывает окно (`UI_FillEquipDetailPanel`, ui_core.c:11296-11318) — своих расчётов по таблице состояний у клиента нет.
//   Шаги:
//     1) база 4060: атака 365~470, защита 2513~2751 — строки «Атака»/«Защита» окна;
//     2) 5272 состояние 21 (+25 %, флаг 598) + 4060 атака 510~657 — строка «Атака» обновилась при открытом окне;
//     3) 5272 состояние 41 (−50 %) + 4060 защита 1257~1376 — строка «Защита» обновилась;
//     4) окно закрыто и открыто заново — показывает последний блок;
//     5) 5273 снятие 21 и 41 + 4060 базы — строки вернулись.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "UI/AsdaCharaInfoSubsystem.h"
#include "UI/AsdaMarkup.h"                   // FAsdaFmtText
#include "UI/AsdaUiHelpers.h"                // FormatThousands — формат окна
#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "AsdaBytes.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaBuffStatsTest, Log, All);

namespace AsdaBuffStatsTestLocal
{
	constexpr int32 kSelfFallback = 4322;   // свой userNum, если не известен
	const TCHAR* const kCmd = TEXT("asda.CharInfo.SelfTestBuffStats");   // имя команды

	// 4060 (68 б): +0 maxHp, +4 maxMp, +6 curHp, +10 curMp, +12/+14 атака, +16/+18 маг. атака, +20 маг. защита, +22/+24 защита.
	static TArray<uint8> Body4060(int32 AtkMin, int32 AtkMax, int32 DefMin, int32 DefMax)
	{
		TArray<uint8> B; B.SetNumZeroed(68);
		AsdaBytes::WrI32(&B[0], 115241); AsdaBytes::WrI16(&B[4], 592); AsdaBytes::WrI32(&B[6], 101493); AsdaBytes::WrI16(&B[10], 490);
		AsdaBytes::WrI16(&B[12], AtkMin); AsdaBytes::WrI16(&B[14], AtkMax); AsdaBytes::WrI16(&B[16], 194); AsdaBytes::WrI16(&B[18], 194);
		AsdaBytes::WrI16(&B[22], DefMin); AsdaBytes::WrI16(&B[24], DefMax);
		return B;
	}
	// 5272 (живое тело 04.11 10:59:10): @0 userIdx, @2/@4 состояние, @6 флаг = навык 598, @8 стек, @10 вид 2, @11 остаток 1200 с,
	//   @13 1, @14 величина (%).
	static TArray<uint8> Body5272(int32 Self, int32 State, int32 Value)
	{
		TArray<uint8> B; B.SetNumZeroed(27);
		AsdaBytes::WrI16(&B[0], Self); AsdaBytes::WrI16(&B[2], State); AsdaBytes::WrI16(&B[4], State); AsdaBytes::WrI16(&B[6], 598);
		B[8] = 1; B[10] = 2; AsdaBytes::WrI16(&B[11], 1200); B[13] = 1; AsdaBytes::WrI16(&B[14], Value);
		return B;
	}
	// 5273 (живое тело 01.11 12:38:29): @0 userIdx, @2 состояние.
	static TArray<uint8> Body5273(int32 Self, int32 State)
	{
		TArray<uint8> B; B.SetNumZeroed(9); AsdaBytes::WrI16(&B[0], Self); AsdaBytes::WrI16(&B[2], State); return B;
	}
	// Сырая строка строки окна 931 + i (StatFmt[i]) — через отражение: заголовок окна не трогаем.
	static FString StatRaw(const UAsdaCharaInfoSubsystem* CI, int32 i)
	{
		const FArrayProperty* P = CI ? CastField<FArrayProperty>(UAsdaCharaInfoSubsystem::StaticClass()->FindPropertyByName(TEXT("StatFmt"))) : nullptr;
		if (!P) { return TEXT("<нет StatFmt>"); }
		FScriptArrayHelper H(P, P->ContainerPtrToValuePtr<void>(CI));
		if (!H.IsValidIndex(i)) { return TEXT("<нет строки>"); }
		return reinterpret_cast<const FAsdaFmtText*>(H.GetRawPtr(i))->LastRaw;
	}
}

struct FAsdaBuffStatsSelfTest
{
	int32 Ok = 0, Fail = 0;
	int32 Self = -1;
	bool bWinWas = false;
	TArray<TFunction<void(UWorld*)>> Steps;

	// Записать исход проверки.
	void Expect(bool b, const FString& What, const FString& Got)
	{
		if (b) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaBuffStatsTest, Log, TEXT("САМОТЕСТ-БАФФ-СТАТЫ %s %s — %s"), b ? TEXT("OK") : TEXT("ПРОВАЛ"), *What, *Got);
	}
	// Ожидаемая строка «Атака»/«Защита» — тем же форматом окна, что эталон (`String_FormatThousands` «мин ~ макс»).
	static FString Range(int32 A, int32 B) { return FString::Printf(TEXT("%s ~ %s"), *AsdaUi::FormatThousands(A), *AsdaUi::FormatThousands(B)); }

	void Build()
	{
		using namespace AsdaBuffStatsTestLocal;
		auto Subs = [](UWorld* W, UAsdaNetSubsystem*& N, UAsdaCharaInfoSubsystem*& C)
		{
			UGameInstance* G = W ? W->GetGameInstance() : nullptr;
			N = G ? AsdaTestNet::Get(G) : nullptr; C = G ? G->GetSubsystem<UAsdaCharaInfoSubsystem>() : nullptr;
			return N && C;
		};
		Steps.Add([this, Subs](UWorld* W)
		{
			UAsdaNetSubsystem* N; UAsdaCharaInfoSubsystem* C; if (!Subs(W, N, C)) { Expect(false, TEXT("подготовка"), TEXT("нет подсистем")); return; }
			if (!AsdaTestNet::bSelfUserNumKnown(N)) { AsdaTestNet::SelfUserNum(N) = kSelfFallback; AsdaTestNet::bSelfUserNumKnown(N) = true; }
			Self = (int32)AsdaTestNet::SelfUserNum(N);
			bWinWas = C->IsWindowOpen();
			C->OpenWindow(); C->SwitchTabMode(0); C->SelectStatPage(1);
			AsdaTestNet::DebugFeedPacket(N, 4060, Body4060(365, 470, 2513, 2751));
			Expect(C->IsWindowOpen(), TEXT("окно 900 открыто штатным OpenWindow"), TEXT(""));
		});
		Steps.Add([this, Subs](UWorld* W)
		{
			UAsdaNetSubsystem* N; UAsdaCharaInfoSubsystem* C; if (!Subs(W, N, C)) { return; }
			Expect(StatRaw(C, 0) == Range(365, 470) && StatRaw(C, 2) == Range(2513, 2751), TEXT("1 база: «Атака» 365 ~ 470, «Защита» 2 513 ~ 2 751"),
				FString::Printf(TEXT("атака «%s», защита «%s»"), *StatRaw(C, 0), *StatRaw(C, 2)));
			AsdaTestNet::DebugFeedPacket(N, 5272, Body5272(Self, 21, 25));
			AsdaTestNet::DebugFeedPacket(N, 4060, Body4060(510, 657, 2513, 2751));
		});
		Steps.Add([this, Subs](UWorld* W)
		{
			UAsdaNetSubsystem* N; UAsdaCharaInfoSubsystem* C; if (!Subs(W, N, C)) { return; }
			Expect(StatRaw(C, 0) == Range(510, 657), TEXT("2 состояние 21 (+25 %) + 4060: «Атака» обновилась при открытом окне"),
				FString::Printf(TEXT("«%s»"), *StatRaw(C, 0)));
			AsdaTestNet::DebugFeedPacket(N, 5272, Body5272(Self, 41, 50));
			AsdaTestNet::DebugFeedPacket(N, 4060, Body4060(510, 657, 1257, 1376));
		});
		Steps.Add([this, Subs](UWorld* W)
		{
			UAsdaNetSubsystem* N; UAsdaCharaInfoSubsystem* C; if (!Subs(W, N, C)) { return; }
			Expect(StatRaw(C, 2) == Range(1257, 1376), TEXT("3 состояние 41 (−50 %) + 4060: «Защита» обновилась"),
				FString::Printf(TEXT("«%s»"), *StatRaw(C, 2)));
			C->CloseWindow();
		});
		Steps.Add([this, Subs](UWorld* W)
		{
			UAsdaNetSubsystem* N; UAsdaCharaInfoSubsystem* C; if (!Subs(W, N, C)) { return; }
			C->OpenWindow(); C->SwitchTabMode(0); C->SelectStatPage(1);
		});
		Steps.Add([this, Subs](UWorld* W)
		{
			UAsdaNetSubsystem* N; UAsdaCharaInfoSubsystem* C; if (!Subs(W, N, C)) { return; }
			Expect(StatRaw(C, 0) == Range(510, 657) && StatRaw(C, 2) == Range(1257, 1376), TEXT("4 окно открыто заново — последний блок"),
				FString::Printf(TEXT("атака «%s», защита «%s»"), *StatRaw(C, 0), *StatRaw(C, 2)));
			AsdaTestNet::DebugFeedPacket(N, 5273, Body5273(Self, 21));
			AsdaTestNet::DebugFeedPacket(N, 5273, Body5273(Self, 41));
			AsdaTestNet::DebugFeedPacket(N, 4060, Body4060(365, 470, 2513, 2751));
		});
		Steps.Add([this, Subs](UWorld* W)
		{
			UAsdaNetSubsystem* N; UAsdaCharaInfoSubsystem* C; if (!Subs(W, N, C)) { return; }
			Expect(StatRaw(C, 0) == Range(365, 470) && StatRaw(C, 2) == Range(2513, 2751), TEXT("5 снятие 21/41 + 4060: строки вернулись"),
				FString::Printf(TEXT("атака «%s», защита «%s»"), *StatRaw(C, 0), *StatRaw(C, 2)));
			if (!bWinWas) { C->CloseWindow(); }
		});
	}
};

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestBuffStats(
	AsdaBuffStatsTestLocal::kCmd,
	TEXT("Самотест: бафф атаки/защиты (598: состояния 21/41) → 4060 → строки «Атака»/«Защита» окна 900 при открытом окне, после "
	     "переоткрытия и после снятия (04.11). Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaBuffStatsTestLocal;
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs)) { return; }
		const bool bQuit = KitArgs.bQuit;
		TWeakObjectPtr<UWorld> WW(W);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit](float) -> bool
		{
			UWorld* W2 = WW.Get();
			if (!W2) { return false; }
			if (AsdaSelfTestKit::RefuseDeferred(W2, kCmd, bQuit)) { return false; }
			TSharedRef<FAsdaBuffStatsSelfTest> T = MakeShared<FAsdaBuffStatsSelfTest>();
			T->Build();
			TSharedRef<int32> Idx = MakeShared<int32>(0);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, T, Idx, bQuit](float) -> bool
			{
				UWorld* W3 = WW.Get();
				if (!W3) { AsdaCVarSave::RestoreAll(kCmd); return false; }
				if (*Idx < T->Steps.Num()) { T->Steps[(*Idx)++](W3); return true; }
				UE_LOG(LogAsdaBuffStatsTest, Log, TEXT("САМОТЕСТ-БАФФ-СТАТЫ ИТОГ: ok=%d fail=%d"), T->Ok, T->Fail);
				AsdaCVarSave::RestoreAll(kCmd);
				AsdaSelfTestKit::Quit(W3, bQuit);
				return false;
			}), 0.4f);
			return false;
		}), FMath::Max(0.01f, KitArgs.Delay));
	}));

#endif // !UE_BUILD_SHIPPING
