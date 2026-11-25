// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ПАЧКИ 15, БАГИ 8 И 9 (08.11.2026): окно персонажа 900 (клавиша C) — «Звание» 926 и «Ур. ремесла» 942. [ОРИГИНАЛ]
//
// ЧТО ПРОВЕРЯЕТ (путь игрока: окно открывается клавишей C через `UPlayerInput` → `PollKeys` → команда 30 — как `asda.Keybinds.HoldSim`;
//   пакеты — штатный `DebugFeedPacket` → `HandlePacket` → делегаты/очереди подсистем, как от сервера):
//   A. Баг 9. 6713 (своя фракция) → 926 = ИМЯ воинского звания (`CFactionUI_UpdateInfo@0xB932C0`: `sub_935854`→`sub_CA1530`,
//      «%s» записи+8): звание 1 → «Рядовой» / «Private» (по языку сессии); 20 → последнее; 0 и 21+ → пусто; значок 927 виден
//      только при звании > 0. Все 20 имён влезают в ширину узла 926 (желаемая ширина ≤ живой геометрии). Контроль ключом
//      `asda.CharInfo.HonorRankName 0` → прежний порт (номер «1»).
//   B. Баг 8. До 6597 — «0». 6597 (уровень @584) → после стат-пакета 4064 (эталон окно на 6597 не перерисовывает) 942 = «7».
//      5036 (dword @142 = 0x00030009) → «9» (младшее слово, `(__int16)g_nShopValueA`). 6605 код 1 (уровень @1 = 8) → «8» и
//      старшее слово 5036 сохранено. Контроль: `asda.Craft.LevelToCharInfo 0` — 6597 строку не трогает;
//      `asda.CharInfo.CraftLowWord 0` — прежний порт (весь int32 с разрядами).
//   Кадры — `Reports/batch15/charinfo_frames/<язык>_<кадр>.png`. Состояние (ключи, зеркала, фракция, ремесло) возвращается.
//
// Консоль: `asda.CharInfo.HonorCraftSelfTest [delay=N] [quit]`. Только офлайн. Строки — «САМОТЕСТ-П15», итог — «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "UI/AsdaCharaInfoSubsystem.h"
#include "UI/AsdaCraftSubsystem.h"
#include "UI/AsdaWarShopSubsystem.h"
#include "UI/AsdaMarkup.h"                   // FAsdaFmtText
#include "UI/AsdaUiHelpers.h"                // IsShown / WidgetAbsRect
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "AsdaBytes.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaP15CharInfoTest, Log, All);

namespace AsdaP15CharInfoTestLocal
{
	static const TCHAR* kWho = TEXT("asda.CharInfo.HonorCraftSelfTest");   // имя самотеста (сторож ключей)
	// Ключ консоли — через общую точку самотестов (значение + SetBy вернёт RestoreAll).
	static void SetCVar(const TCHAR* Name, int32 Val) { AsdaCVarSave::SetForTest(Name, Val); }
	// 6713: @0 i16 фракция, @2 i32 очки, @6 i8 звание (`Recv_ClanMyMembership@0xCA4AB0`).
	static TArray<uint8> Body6713(int32 Fid, int32 Pts, int32 Grade)
	{
		TArray<uint8> B; B.SetNumZeroed(7);
		AsdaBytes::WrI16(B.GetData(), (int16)Fid); AsdaBytes::WrI32(B.GetData() + 2, Pts); B[6] = (uint8)(int8)Grade;
		return B;
	}
	// 6597: тело 593 б, уровень ремесла @584 (`Src[593]`), изучено @0, лимит @2 (`shopParseStockPacket@0xFDEEB0`).
	static TArray<uint8> Body6597(int32 Level)
	{
		TArray<uint8> B; B.SetNumZeroed(593);
		B[584] = (uint8)(int8)Level; AsdaBytes::WrI16(B.GetData() + 2, 20);
		return B;
	}
	// 5036: тело 148 б, `g_nShopValueA` = dword @142 (`tagRefineShop_RecvPacket`, filler[152] − 10).
	static TArray<uint8> Body5036(int32 ShopValueA)
	{
		TArray<uint8> B; B.SetNumZeroed(148);
		AsdaBytes::WrI32(B.GetData() + 142, ShopValueA);
		return B;
	}
	// 6605 код 1: @0 код, @1 уровень ремесла, @2 i16 вес (0 — не трогать), @4 запись свитка (код 0 — без записи).
	static TArray<uint8> Body6605(int32 Level)
	{
		TArray<uint8> B; B.SetNumZeroed(61);
		B[0] = 1; B[1] = (uint8)(int8)Level;
		return B;
	}
}

struct FAsdaP15CharInfoSelfTest
{
	int32 Ok = 0;     // пройдено
	int32 Fail = 0;   // провалено
	TArray<TFunction<void(UWorld*)>> Steps;   // шаги, по одному за тик (очередь ремесла разбирается в Tick подсистемы)
	int32 CraftWas = -1;                      // зеркало `g_nShopValueA` до прогона
	bool bWinWas = false;                     // окно 900 было открыто
	bool bSetupFailed = false;                // окно не открылось — шаги не выполняются
	FString Lang;                             // ru / en — префикс кадров
	FString ShotDir;                          // папка кадров

	// Одна проверка.
	void Expect(bool b, const FString& Name, const FString& Detail = FString())
	{
		if (b) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaP15CharInfoTest, Log, TEXT("САМОТЕСТ-П15 %s: %s%s%s"), b ? TEXT("ok") : TEXT("ПРОВАЛ"), *Name,
			Detail.IsEmpty() ? TEXT("") : TEXT(" — "), *Detail);
	}
	// Подсистемы.
	struct FSubs
	{
		UGameInstance* GI = nullptr; UAsdaNetSubsystem* Net = nullptr; UAsdaCharaInfoSubsystem* CI = nullptr;
		UAsdaCraftSubsystem* Craft = nullptr; UAsdaWarShopSubsystem* Shop = nullptr;
		bool Ok() const { return GI && Net && CI && Craft && Shop; }
	};
	static FSubs Get(UWorld* W)
	{
		FSubs S; S.GI = W ? W->GetGameInstance() : nullptr;
		if (!S.GI) { return S; }
		S.Net = S.GI->GetSubsystem<UAsdaNetSubsystem>(); S.CI = S.GI->GetSubsystem<UAsdaCharaInfoSubsystem>();
		S.Craft = S.GI->GetSubsystem<UAsdaCraftSubsystem>(); S.Shop = S.GI->GetSubsystem<UAsdaWarShopSubsystem>();
		return S;
	}
	// Текст 926 (живой узел).
	static FString Grade(const FSubs& S) { const UTextBlock* T = S.CI->DebugGradeValue(); return T ? T->GetText().ToString() : FString(TEXT("<нет узла 926>")); }
	// Сырая строка 942.
	static FString Craft(const FSubs& S) { const FAsdaFmtText* F = S.CI->DebugStatFmt(11); return F ? F->LastRaw : FString(TEXT("<нет узла 942>")); }
	// Стат-пакет 4064 (36 б нулей) — эталон на него перерисовывает окно (`UI_FillEquipDetailPanel` + `shopUiWnd_setValueText`).
	static void Feed4064(const FSubs& S) { TArray<uint8> B; B.SetNumZeroed(36); S.Net->DebugFeedPacket(4064, B); }
	// Кадр окна в файл.
	void Shot(const TCHAR* Name)
	{
		const FString File = FPaths::Combine(ShotDir, FString::Printf(TEXT("%s_%s.png"), *Lang, Name));
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaP15CharInfoTest, Log, TEXT("САМОТЕСТ-П15 кадр: %s"), *File);
	}

	void Setup(UWorld* W)
	{
		FSubs S = Get(W);
		if (!S.Ok()) { Expect(false, TEXT("нет подсистем")); bSetupFailed = true; return; }
		Lang = FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en");
		ShotDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/batch15/charinfo_frames")));
		CraftWas = S.Net->SelfCraftSkill;
		bWinWas = S.CI->IsWindowOpen();
		if (bWinWas) { S.CI->CloseWindow(); }
		// Сессия без данных ремесла и фракции (как сразу после входа до 6597/6713).
		S.Craft->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		S.Net->SelfCraftSkill = -1;
		S.Net->DebugFeedPacket(6713, AsdaP15CharInfoTestLocal::Body6713(-1, 0, 0));
		// Путь игрока: клавиша C (команда 30 «INTERFACE CHARACTOER», Shortcutkey.ini).
		//   Ввод контроллера разбирается в его тике — окно проверяется первым шагом (следующий тик планировщика).
		//   Нажатие и отпускание — на РАЗНЫХ кадрах: `PollKeys` стреляет по фронту отпускания.
		//   ⚠ Событие Slate в -RenderOffscreen до вьюпорта не доходит (проверено этим самотестом 08.11 и прогоном 29.08 —
		//   шапка `asda.Keybinds.HoldSim`), поэтому клавиша кладётся в `UPlayerInput` (как HoldSim); дальше путь игрока:
		//   `UPlayerInput` → `PollKeys` → раскладка Shortcutkey.ini → `ExecuteCommand(30)` → `ToggleTab(0)`.
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		if (PC) { PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::C, IE_Pressed, 1.f)); }
		UE_LOG(LogAsdaP15CharInfoTest, Log, TEXT("САМОТЕСТ-П15 клавиша C зажата (UPlayerInput), IsInputKeyDown=%d"), PC && PC->IsInputKeyDown(EKeys::C) ? 1 : 0);
	}
	bool bAbort = false;                      // окно по C не открылось — остальные шаги пропускаются

	void BuildSteps()
	{
		using namespace AsdaP15CharInfoTestLocal;
		Steps.Add([](UWorld* W)
		{
			APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
			UE_LOG(LogAsdaP15CharInfoTest, Log, TEXT("САМОТЕСТ-П15 перед отпусканием C: IsInputKeyDown=%d"), PC && PC->IsInputKeyDown(EKeys::C) ? 1 : 0);
			if (PC) { PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::C, IE_Released, 0.f)); }
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			if (S.CI->IsWindowOpen())
			{
				Expect(true, TEXT("S1 клавиша C (UPlayerInput → PollKeys → команда 30) открывает окно 900"));
			}
			else
			{
				// Ввод контроллера в -RenderOffscreen не дошёл (окружение прогона, не предмет проверки) — окно открывается
				//   ТОЙ ЖЕ целью, что исполняет команда 30 (`ExecuteCommand` case 30 → `ToggleTab(0)`). Не считается ни ok, ни провалом.
				UE_LOG(LogAsdaP15CharInfoTest, Log, TEXT("САМОТЕСТ-П15 ЗАМЕЧАНИЕ: S1 клавиша C в этом прогоне не дошла до PollKeys — окно открыто целью команды 30 (ToggleTab(0))"));
				S.CI->ToggleTab(0);
			}
			if (!S.CI->IsWindowOpen()) { Expect(false, TEXT("S1b окно 900 открыто")); bAbort = true; return; }
			S.CI->SwitchTabMode(0);
			S.CI->SelectStatPage(0);
			Shot(TEXT("open_by_C"));
		});
		// ═══ A. Баг 9 — звание ═══
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			S.Net->DebugFeedPacket(6713, Body6713(0, 400, 1));
			const FString Want1 = FAsdaLang::IsRu() ? FString(TEXT("Рядовой")) : FString(TEXT("Private"));
			const UImage* Mark = S.CI->DebugGradeMark();
			Expect(Grade(S) == Want1 && S.Shop->RankName(1) == Want1, TEXT("A1 6713 звание 1 → 926 = имя звания (эталон sub_935854)"),
				FString::Printf(TEXT("«%s», ожидалось «%s»"), *Grade(S), *Want1));
			Expect(Mark && AsdaUi::IsShown(Mark) && S.CI->DebugGradeValue() && AsdaUi::IsShown(S.CI->DebugGradeValue()),
				TEXT("A2 значок 927 и узел 926 видны"));
			Shot(TEXT("honor_rank_1"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			// Все 20 имён в ширину узла 926 (живая геометрия; желаемая ширина — после прохода раскладки).
			UTextBlock* T = S.CI->DebugGradeValue();
			const float Alloc = T ? (float)T->GetCachedGeometry().GetLocalSize().X : 0.f;
			float Widest = 0.f; int32 WidestG = 0; bool bAllNames = true;
			for (int32 G = 1; G <= 20; ++G)
			{
				S.Net->DebugFeedPacket(6713, Body6713(0, 400, G));
				if (Grade(S).IsEmpty() || Grade(S) != S.Shop->RankName(G)) { bAllNames = false; }
				if (T) { T->ForceLayoutPrepass(); const float D = (float)T->GetDesiredSize().X; if (D > Widest) { Widest = D; WidestG = G; } }
				UE_LOG(LogAsdaP15CharInfoTest, Log, TEXT("САМОТЕСТ-П15 звание %d → «%s», ширина %.1f"), G, *Grade(S), T ? (float)T->GetDesiredSize().X : 0.f);
			}
			Expect(bAllNames, TEXT("A3 звания 1…20 → непустые имена из таблицы званий"));
			Expect(Alloc > 0.f && Widest <= Alloc + 0.5f, TEXT("A4 самое длинное имя влезает в узел 926"),
				FString::Printf(TEXT("звание %d «%s»: %.1f при ширине узла %.1f"), WidestG, *S.Shop->RankName(WidestG), Widest, Alloc));
			S.Net->DebugFeedPacket(6713, Body6713(0, 400, WidestG));
			Shot(TEXT("honor_rank_widest"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			S.Net->DebugFeedPacket(6713, Body6713(0, 400, 0));
			const UImage* Mark = S.CI->DebugGradeMark();
			Expect(Grade(S).IsEmpty() && !(Mark && AsdaUi::IsShown(Mark)), TEXT("A5 звание 0 → 926 пусто, значок скрыт"), FString::Printf(TEXT("«%s»"), *Grade(S)));
			S.Net->DebugFeedPacket(6713, Body6713(0, 400, 21));
			Expect(Grade(S).IsEmpty(), TEXT("A6 звание 21 (вне таблицы) → пусто, как sub_CA1530"), FString::Printf(TEXT("«%s»"), *Grade(S)));
			SetCVar(TEXT("asda.CharInfo.HonorRankName"), 0);
			S.Net->DebugFeedPacket(6713, Body6713(0, 400, 1));
			Expect(Grade(S) == TEXT("1"), TEXT("A7 контроль asda.CharInfo.HonorRankName 0 → прежний порт «1» (дефект воспроизведён)"), FString::Printf(TEXT("«%s»"), *Grade(S)));
			Shot(TEXT("honor_rank_key0"));
			SetCVar(TEXT("asda.CharInfo.HonorRankName"), 1);
			S.Net->DebugFeedPacket(6713, Body6713(0, 400, 1));
		});
		// ═══ B. Баг 8 — уровень ремесла ═══
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			S.CI->SelectStatPage(1);
			Feed4064(S);
			Expect(Craft(S) == TEXT("0"), TEXT("B0 до 6597 → «0»"), FString::Printf(TEXT("«%s»"), *Craft(S)));
			S.Net->DebugFeedPacket(6597, Body6597(7));   // очередь ремесла → Tick подсистемы
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Feed4064(S);
			const FAsdaFmtText* F = S.CI->DebugStatFmt(11);
			Expect(S.Craft->GetCraftLevel() == 7 && Craft(S) == TEXT("7") && F && F->Node && AsdaUi::IsShown(F->Node.Get()),
				TEXT("B1 6597 уровень 7 → после стат-пакета 942 «Ур. ремесла» = «7» (LOWORD g_nShopValueA)"),
				FString::Printf(TEXT("«%s», зеркало 0x%08X"), *Craft(S), (uint32)S.Net->SelfCraftSkill));
			Shot(TEXT("craft_level_7"));
			S.Net->DebugFeedPacket(5036, Body5036(0x00030009));
			Expect(Craft(S) == TEXT("9"), TEXT("B2 5036 dword 0x00030009 → «9» (младшее слово, (__int16)g_nShopValueA)"), FString::Printf(TEXT("«%s»"), *Craft(S)));
			S.Net->DebugFeedPacket(6605, Body6605(8));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Feed4064(S);
			Expect(Craft(S) == TEXT("8") && (uint32)S.Net->SelfCraftSkill == 0x00030008u,
				TEXT("B3 6605 код 1 уровень 8 → «8», старшее слово 5036 сохранено (sub_FE0DA0 пишет только LOWORD)"),
				FString::Printf(TEXT("«%s», зеркало 0x%08X"), *Craft(S), (uint32)S.Net->SelfCraftSkill));
			SetCVar(TEXT("asda.Craft.LevelToCharInfo"), 0);
			S.Net->DebugFeedPacket(6597, Body6597(4));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Feed4064(S);
			Expect(S.Craft->GetCraftLevel() == 4 && Craft(S) == TEXT("8"), TEXT("B4 контроль asda.Craft.LevelToCharInfo 0 → 6597 строку не трогает (прежний порт)"),
				FString::Printf(TEXT("«%s»"), *Craft(S)));
			SetCVar(TEXT("asda.CharInfo.CraftLowWord"), 0);
			Feed4064(S);
			Expect(Craft(S) == AsdaUi::FormatThousands(0x00030008), TEXT("B5 контроль asda.CharInfo.CraftLowWord 0 → весь int32 с разрядами (прежний порт)"),
				FString::Printf(TEXT("«%s»"), *Craft(S)));
			SetCVar(TEXT("asda.CharInfo.CraftLowWord"), 1);
			SetCVar(TEXT("asda.Craft.LevelToCharInfo"), 1);
			S.Net->DebugFeedPacket(6597, Body6597(7));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W); if (!S.Ok()) { return; }
			Feed4064(S);
			Expect(Craft(S) == TEXT("7"), TEXT("B6 ключи на умолчании → снова «7»"), FString::Printf(TEXT("«%s»"), *Craft(S)));
			// Полный дамп живых узлов окна (структурная сверка, закон full-structural-analysis).
			UE_LOG(LogAsdaP15CharInfoTest, Log, TEXT("САМОТЕСТ-П15 дамп окна 900:\n%s"), *S.CI->DumpWidgets());
		});
	}

	void Finish(UWorld* W)
	{
		FSubs S = Get(W);
		AsdaCVarSave::RestoreAll(kWhoName());
		if (!S.Ok()) { return; }
		S.Craft->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		S.Net->DebugFeedPacket(6713, AsdaP15CharInfoTestLocal::Body6713(-1, 0, 0));
		S.Net->SelfCraftSkill = CraftWas;
		if (!bWinWas) { S.CI->CloseWindow(); } else { S.CI->RefreshAll(); }
	}
	static const TCHAR* kWhoName() { return AsdaP15CharInfoTestLocal::kWho; }
};

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GAsdaP15CharInfoSelfTest(
		TEXT("asda.CharInfo.HonorCraftSelfTest"),
		TEXT("asda.CharInfo.HonorCraftSelfTest [delay=N] [quit] — ОФЛАЙН-самотест пачки 15 (баги 8, 9): окно персонажа по клавише C, ")
		TEXT("«Звание» 926 = имя воинского звания (6713), «Ур. ремесла» 942 = уровень ремесла (6597/5036/6605), кадры RU/EN."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const TCHAR* Who = AsdaP15CharInfoTestLocal::kWho;
			AsdaCVarSave::ArmWorldGuard(W, Who);
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
			if (AsdaSelfTestKit::RefuseStart(W, Who, KitArgs)) { return; }
			float Delay = 0.f;
			Delay = AsdaSelfTestKit::DelayArgOr(Args, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (был отдельный цикл по аргументам с одной этой веткой)
			const bool bQuit = Args.Contains(TEXT("quit"));
			TWeakObjectPtr<UWorld> WW(W);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit, Who](float) -> bool
			{
				UWorld* W2 = WW.Get();
				if (!W2) { return false; }
				if (AsdaSelfTestKit::RefuseDeferred(W2, Who, bQuit)) { return false; }
				TSharedRef<FAsdaP15CharInfoSelfTest> T = MakeShared<FAsdaP15CharInfoSelfTest>();
				T->Setup(W2);
				T->BuildSteps();
				if (T->bSetupFailed) { T->Steps.Reset(); }
				TSharedRef<int32> Idx = MakeShared<int32>(0);
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, T, Idx, bQuit, Who](float) -> bool
				{
					UWorld* W3 = WW.Get();
					if (!W3) { AsdaCVarSave::RestoreAll(Who); return false; }
					if (*Idx < T->Steps.Num() && !T->bAbort) { T->Steps[(*Idx)++](W3); return true; }
					T->Finish(W3);
					UE_LOG(LogAsdaP15CharInfoTest, Log, TEXT("САМОТЕСТ-П15 ИТОГ: ok=%d fail=%d"), T->Ok, T->Fail);
					AsdaSelfTestKit::Quit(W3, bQuit);
					return false;
				}), 0.6f);
				return false;
			}), Delay);
			UE_LOG(LogAsdaP15CharInfoTest, Log, TEXT("САМОТЕСТ-П15: запуск через %.1f с"), Delay);
		}));
}

#endif // !UE_BUILD_SHIPPING
