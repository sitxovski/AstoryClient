// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ: ДЕЙСТВИЯ КЛАНА БЕЗ КЛАНА (пачка 16, баг 7, 08.11.2026). [ИСПРАВЛЕНИЕ] D8 — шапка `AsdaClanSubsystem.h`.
//
// ЖАЛОБА. Игрок не в клане нажимает «Распустить клан» у NPC и получает «Only the clan leader can do it» (loc 12431
//   «Привилегия только главы клана.»). Эталон: `uiRepairWindowToggle_54D0` (ui_core.c:16154) объединяет «клана нет» и
//   «не глава» в один 12431; `uiInventoryRepairDialogToggle` (ui_core.c:16188) — тот же 12431 при «клана нет»;
//   приглашение 4108 без клана сервер отвергает кодом 4 (`ClanMemberInvite_RequestUser`, GameServer.c:735) → 12417.
//
// ЧТО ПРОВЕРЯЕТ (путь игрока — тем же кодом, что мышь):
//   1. NPC 107 (Элла) → кнопка 0 (`OnDialogButton(581)`, код 30016) без клана → окна роспуска НЕТ, окно-сообщение
//      = loc 991210 «Ты не состоишь в клане.» / «You are not in a clan.»; строка не пустая и не равна 12431.
//   2. Контроль ключа `asda.Clan.NoClanNotice 0` — то же нажатие даёт 12431 (1:1 оригинал).
//   3. Кнопка 3 (`OnDialogButton(584)`, код 30027 «Расширить клан») без клана → 991210, окна 14450 нет.
//   4. Приём 4109 код 4 (штатный `DebugFeedPacket` → `HandlePacket` → меню игрока) без клана → окно 991210.
//   5. Контроль: в клане, звание 2 (не глава) — 4109 код 4 → 12417, кнопка 30016 → 12431 (прежние строки «только глава»).
//   6. Контроль ключа `asda.Clan.InviteResultMsgBox 0` — отказ 4109 строкой журнала (окна нет), нотис тот же 991210.
//   Кадры — `Reports/batch16/clan/<язык>_<кадр>.png`. Клан, звание, «Друг», ключи и очередь окон возвращаются.
//
// Консоль: `asda.Clan.NoClanSelfTest [delay=N] [quit]`. Только офлайн. Строки — «САМОТЕСТ-БЕЗКЛАНА», итог — «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "Npc/AsdaNpcDialogSubsystem.h"        // окно разговора 570: OpenForNpcId / OnDialogButton / DumpWindow
#include "UI/AsdaClanSubsystem.h"              // IsDisbandOpen / IsIncreaseOpen / NoClanLocOr
#include "UI/AsdaPlayerMenuSubsystem.h"        // GetLastNoticeLoc — след нотиса 4109
#include "UI/AsdaWindowSubsystem.h"            // очередь окон-сообщений
#include "World/AsdaWorldSubsystem.h"          // GetSelfClanId — восстановить свой клан
#include "UI/AsdaNotice.h"                     // AsdaNotice::Loc
#include "Text/AsdaLang.h"                     // FAsdaLang::IsRu — ожидаемый текст по языку сессии
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaNoClanTest, Log, All);

namespace AsdaNoClanTestLocal
{
	static const TCHAR* kWho = TEXT("asda.Clan.NoClanSelfTest");   // имя самотеста (сторож ключей)
	constexpr int32 kLocNotInClan = 991210;   // ожидаемая новая строка (оракул задан явно, не из боевого кода)
	constexpr int32 kLocCaptain   = 12431;    // эталон гейтов 30016/30027
	constexpr int32 kLocInvite4   = 12417;    // эталон 4109 код 4
	constexpr int32 kBtnDisband   = 581;      // кнопка меню 0 Эллы (`NPC_SHOP_BUT00`) = 30016
	constexpr int32 kBtnIncrease  = 584;      // кнопка меню 3 Эллы (`NPC_SHOP_BUT03`) = 30027
}

struct FAsdaNoClanSelfTest
{
	int32 Ok = 0;     // пройдено
	int32 Fail = 0;   // провалено
	TArray<TFunction<void(UWorld*)>> Steps;   // шаги, по одному за тик (окно-сообщение успевает отрисоваться к кадру)
	bool bSetupFailed = false;                // нет подсистем — шаги не выполняются
	FString Lang;                             // ru / en — префикс кадров
	FString ShotDir;                          // папка кадров
	int32 ClanWas = -1;                       // свой клан до прогона
	FString ClanNameWas;                      // имя своего клана до прогона
	uint8 RankWas = 0;                        // своё звание до прогона
	uint8 SoulWas = 0;                        // признак «Друг» до прогона

	// Подсистемы.
	struct FSubs
	{
		UGameInstance* GI = nullptr; UAsdaNetSubsystem* Net = nullptr; UAsdaNpcDialogSubsystem* Dlg = nullptr;
		UAsdaClanSubsystem* Clan = nullptr; UAsdaPlayerMenuSubsystem* PM = nullptr; UAsdaWindowSubsystem* WM = nullptr;
		UAsdaWorldSubsystem* WS = nullptr;
		bool Ok() const { return GI && Net && Dlg && Clan && PM && WM && WS; }
	};
	static FSubs Get(UWorld* W)
	{
		FSubs S; S.GI = W ? W->GetGameInstance() : nullptr;
		if (!S.GI) { return S; }
		S.Net = S.GI->GetSubsystem<UAsdaNetSubsystem>(); S.Dlg = S.GI->GetSubsystem<UAsdaNpcDialogSubsystem>();
		S.Clan = S.GI->GetSubsystem<UAsdaClanSubsystem>(); S.PM = S.GI->GetSubsystem<UAsdaPlayerMenuSubsystem>();
		S.WM = S.GI->GetSubsystem<UAsdaWindowSubsystem>(); S.WS = S.GI->GetSubsystem<UAsdaWorldSubsystem>();
		return S;
	}

	// Одна проверка.
	void Expect(bool b, const FString& What)
	{
		if (b) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaNoClanTest, Log, TEXT("САМОТЕСТ-БЕЗКЛАНА %s: %s"), b ? TEXT("ok") : TEXT("ПРОВАЛ"), *What);
	}
	// Кадр в файл.
	void Shot(const TCHAR* Name)
	{
		const FString File = FPaths::Combine(ShotDir, FString::Printf(TEXT("%s_%s.png"), *Lang, Name));
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaNoClanTest, Log, TEXT("САМОТЕСТ-БЕЗКЛАНА кадр: %s"), *File);
	}
	// Свой клан: −1 = клана нет (тем же делегатом, что приём 4105/4123 — как `asda.Clan.BtnSelfTest`), звание — зеркало 0x97B.
	static void SetClan(const FSubs& S, int32 ClanId, const FString& Name, uint8 Rank)
	{
		S.Net->OnClanInfo.Broadcast(-1, ClanId, Name, FString());
		S.Net->SelfClanRank = Rank;
	}
	// Тело 4109 (27 б, как у `asda.Clan.FriendGateSelfTest`): @0 код.
	static void Feed4109(const FSubs& S, uint8 Code)
	{
		TArray<uint8> B; B.SetNumZeroed(27); B[0] = Code;
		S.Net->DebugFeedPacket(4109, B);
	}
	// Нажатие кнопки меню Эллы (окно разговора открывается при необходимости).
	void Press(const FSubs& S, int32 Btn)
	{
		if (!S.Dlg->IsDialogOpen()) { S.Dlg->OpenForNpcId(107); }
		S.Dlg->OnDialogButton(Btn);
	}
	FString Top(const FSubs& S) const { return S.WM->GetMsgBoxQueueTopText(); }

	void Setup(UWorld* W)
	{
		FSubs S = Get(W);
		if (!S.Ok()) { Expect(false, TEXT("нет подсистем (сеть/диалог/клан/меню/окна/мир)")); bSetupFailed = true; return; }
		Lang = FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en");
		ShotDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/batch16/clan")));
		ClanWas = S.WS->GetSelfClanId(); ClanNameWas = S.Net->SelfClanName;
		RankWas = S.Net->SelfClanRank; SoulWas = S.Net->SelfSoulSilhouette;
		AsdaCVarSave::SetForTest(TEXT("asda.Clan.NoClanNotice"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Clan.InviteResultMsgBox"), 1);
		S.Net->SelfSoulSilhouette = 0;
		SetClan(S, -1, FString(), 0);
		S.WM->ClearMsgBoxQueue(TEXT("самотест без клана: старт"));
	}

	void BuildSteps()
	{
		using namespace AsdaNoClanTestLocal;
		// 0. Строки и разметка Эллы.
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W);
			const FString Want = FAsdaLang::IsRu() ? TEXT("Ты не состоишь в клане.") : TEXT("You are not in a clan.");
			const FString Got = AsdaNotice::Loc(S.GI, kLocNotInClan);
			Expect(Got == Want, FString::Printf(TEXT("loc %d = «%s» (ждём «%s»)"), kLocNotInClan, *Got, *Want));
			Expect(Got != AsdaNotice::Loc(S.GI, kLocCaptain), FString::Printf(TEXT("991210 ≠ 12431 «%s»"), *AsdaNotice::Loc(S.GI, kLocCaptain)));
			Expect(S.WS->GetSelfClanId() == -1, FString::Printf(TEXT("свой клан снят: %d (ждём −1)"), S.WS->GetSelfClanId()));
			Expect(S.Dlg->OpenForNpcId(107), TEXT("окно разговора с NPC 107 открыто"));
			const FString D = S.Dlg->DumpWindow();
			Expect(D.Contains(TEXT("код=30016")) && D.Contains(TEXT("код=30027")),
				TEXT("в меню Эллы есть кнопки 30016 (роспуск) и 30027 (расширение)"));
		});
		// 1. Роспуск без клана → 991210.
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W);
			Press(S, kBtnDisband);
			Expect(!S.Clan->IsDisbandOpen(), TEXT("1 без клана: окно роспуска 14470 НЕ открылось"));
			Expect(Top(S) == AsdaNotice::Loc(S.GI, kLocNotInClan),
				FString::Printf(TEXT("1 без клана, «Распустить клан» → окно «%s» (ждём 991210)"), *Top(S)));
			Shot(TEXT("1_disband_noclan"));
		});
		// 2. Контроль: ключ 0 → 12431.
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W);
			S.WM->ClearMsgBoxQueue(TEXT("самотест без клана: шаг 2"));
			AsdaCVarSave::SetForTest(TEXT("asda.Clan.NoClanNotice"), 0);
			Press(S, kBtnDisband);
			Expect(Top(S) == AsdaNotice::Loc(S.GI, kLocCaptain),
				FString::Printf(TEXT("2 контроль ключ 0: окно «%s» (ждём 12431 — как в оригинале)"), *Top(S)));
			AsdaCVarSave::SetForTest(TEXT("asda.Clan.NoClanNotice"), 1);
			Shot(TEXT("2_disband_noclan_key0"));
		});
		// 3. Расширение без клана → 991210.
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W);
			S.WM->ClearMsgBoxQueue(TEXT("самотест без клана: шаг 3"));
			Press(S, kBtnIncrease);
			Expect(!S.Clan->IsIncreaseOpen(), TEXT("3 без клана: окно 14450 НЕ открылось"));
			Expect(Top(S) == AsdaNotice::Loc(S.GI, kLocNotInClan),
				FString::Printf(TEXT("3 без клана, «Расширить клан» → окно «%s» (ждём 991210)"), *Top(S)));
			Shot(TEXT("3_increase_noclan"));
		});
		// 4. Приглашение: 4109 код 4 без клана → окно 991210.
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W);
			S.WM->ClearMsgBoxQueue(TEXT("самотест без клана: шаг 4"));
			if (S.Dlg->IsDialogOpen()) { S.Dlg->Close(); }
			Feed4109(S, 4);
			Expect(S.PM->GetLastNoticeLoc() == kLocNotInClan && Top(S) == AsdaNotice::Loc(S.GI, kLocNotInClan),
				FString::Printf(TEXT("4 без клана, 4109 код 4 → нотис %d окном «%s» (ждём 991210)"), S.PM->GetLastNoticeLoc(), *Top(S)));
			Shot(TEXT("4_invite_noclan"));
		});
		// 5. Контроль: в клане, не глава → прежние строки «только глава».
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W);
			S.WM->ClearMsgBoxQueue(TEXT("самотест без клана: шаг 5"));
			SetClan(S, 7, TEXT("SelfTest"), 2);
			Feed4109(S, 4);
			Expect(S.PM->GetLastNoticeLoc() == kLocInvite4 && Top(S) == AsdaNotice::Loc(S.GI, kLocInvite4),
				FString::Printf(TEXT("5 контроль в клане (звание 2): 4109 код 4 → нотис %d «%s» (ждём 12417)"), S.PM->GetLastNoticeLoc(), *Top(S)));
			S.WM->ClearMsgBoxQueue(TEXT("самотест без клана: шаг 5б"));
			Press(S, kBtnDisband);
			Expect(!S.Clan->IsDisbandOpen() && Top(S) == AsdaNotice::Loc(S.GI, kLocCaptain),
				FString::Printf(TEXT("5 контроль в клане (звание 2): «Распустить клан» → «%s» (ждём 12431)"), *Top(S)));
			Shot(TEXT("5_member_disband"));
		});
		// 6. Контроль канала: ключ InviteResultMsgBox 0 → строка журнала, окна нет.
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W);
			S.WM->ClearMsgBoxQueue(TEXT("самотест без клана: шаг 6"));
			if (S.Dlg->IsDialogOpen()) { S.Dlg->Close(); }
			SetClan(S, -1, FString(), 0);
			AsdaCVarSave::SetForTest(TEXT("asda.Clan.InviteResultMsgBox"), 0);
			const int32 Seq = S.PM->GetNoticeSeq();
			Feed4109(S, 4);
			Expect(S.WM->GetMsgBoxQueueNum() == 0 && S.PM->GetNoticeSeq() == Seq + 1 && S.PM->GetLastNoticeLoc() == kLocNotInClan,
				FString::Printf(TEXT("6 контроль ключ InviteResultMsgBox 0: окон %d (ждём 0), нотис %d журналом (ждём 991210)"),
					S.WM->GetMsgBoxQueueNum(), S.PM->GetLastNoticeLoc()));
			AsdaCVarSave::SetForTest(TEXT("asda.Clan.InviteResultMsgBox"), 1);
		});
	}

	void Finish(UWorld* W)
	{
		FSubs S = Get(W);
		AsdaCVarSave::RestoreAll(AsdaNoClanTestLocal::kWho);
		if (!S.Ok()) { return; }
		if (S.Dlg->IsDialogOpen()) { S.Dlg->Close(); }
		S.Clan->CloseDisbandWindow();
		S.Clan->CloseIncreaseWindow();
		SetClan(S, ClanWas, ClanNameWas, RankWas);
		S.Net->SelfClanName = ClanNameWas;
		S.Net->SelfSoulSilhouette = SoulWas;
		S.WM->ClearMsgBoxQueue(TEXT("самотест без клана: конец"));
	}
};

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GAsdaNoClanSelfTest(
		TEXT("asda.Clan.NoClanSelfTest"),
		TEXT("asda.Clan.NoClanSelfTest [delay=N] [quit] — ОФЛАЙН-самотест пачки 16 (баг 7): действия клана без клана — роспуск/расширение у NPC 107 ")
		TEXT("и отказ приглашения 4109 код 4 дают «Ты не состоишь в клане.» (991210), контроли ключей и «не глава», кадры RU/EN."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const TCHAR* Who = AsdaNoClanTestLocal::kWho;
			AsdaCVarSave::ArmWorldGuard(W, Who);
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
			if (AsdaSelfTestKit::RefuseStart(W, Who, KitArgs)) { return; }
			const bool bQuit = KitArgs.bQuit;
			TWeakObjectPtr<UWorld> WW(W);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit, Who](float) -> bool
			{
				UWorld* W2 = WW.Get();
				if (!W2) { return false; }
				if (AsdaSelfTestKit::RefuseDeferred(W2, Who, bQuit)) { return false; }
				TSharedRef<FAsdaNoClanSelfTest> T = MakeShared<FAsdaNoClanSelfTest>();
				T->Setup(W2);
				T->BuildSteps();
				if (T->bSetupFailed) { T->Steps.Reset(); }
				TSharedRef<int32> Idx = MakeShared<int32>(0);
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, T, Idx, bQuit, Who](float) -> bool
				{
					UWorld* W3 = WW.Get();
					if (!W3) { AsdaCVarSave::RestoreAll(Who); return false; }
					if (*Idx < T->Steps.Num()) { T->Steps[(*Idx)++](W3); return true; }
					T->Finish(W3);
					UE_LOG(LogAsdaNoClanTest, Log, TEXT("САМОТЕСТ-БЕЗКЛАНА ИТОГ: ok=%d fail=%d"), T->Ok, T->Fail);
					AsdaSelfTestKit::Quit(W3, bQuit);
					return false;
				}), 0.8f);
				return false;
			}), KitArgs.Delay);
			UE_LOG(LogAsdaNoClanTest, Log, TEXT("САМОТЕСТ-БЕЗКЛАНА: запуск через %.1f с"), KitArgs.Delay);
		}));
}

#endif // !UE_BUILD_SHIPPING
