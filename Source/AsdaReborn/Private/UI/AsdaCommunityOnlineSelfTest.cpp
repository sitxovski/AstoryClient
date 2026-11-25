// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ: ОНЛАЙН-СТАТУС ВО ВКЛАДКАХ «КЛАН» И «ДРУГ» ОКНА СООБЩЕСТВА 6400 (пачка 17, баг 4, 08.11.2026). [ПОРТ]
//
// ЖАЛОБА. Во вкладке «Клан» игрок, который в сети, не подсвечен зелёным; подозрение — список не обновляется. То же проверить у «Друг»
//   и «Друг (Soulmate)» (последняя — отдельный самотест `asda.Soul.SelfTestOfflineInfo`, 5220/5226).
//
// ПОТОК ОРИГИНАЛА (разбор — отчёт сессии, коротко):
//   Клан. Список 4125 (записи 131 б, флаг «в игре» [113] = `CharPos.State` из хранимки DB `UPClanMemberinfo`, канал [111], карта [112]),
//     конец 4126. Обновление одной записи — 4133 вид 1 (`Recv_AuthRelogin@0xBCB760` → `party_UpdateMemberRecord`, затем
//     `Recv_ServerChannelList` — перерисовка). Сервер шлёт 4133 вид 1 при входе в мир (`sub_405260(…, 4)`), смене карты (5) — флаг 1,
//     при выходе (6) — флаг 0 и дата. Строка: флаг 1 → «<FC=FF93FF93>В игре</F> - Nch Карта» (loc 12227, зелёный) + значок
//     `CMTY_CLAN_MEMON{n}_On`; флаг 0 → «Не в игре» (loc 12228, серый), значок погашен.
//   Друг. Список 4157 (записи 31 б, флаг [10] == 1), обновление одной записи 4160 (i16 0 + запись 31 б; у оригинала ветки нет —
//     дефект D-F4, у порта ключ `asda.Community.Friend.Handle4160`). Строка: в сети → ONNAME + ONINFO + `CMTY_FRND_STAT{n}On`,
//     иначе OFFNAME + `CMTY_FRND_STAT{n}Off`.
//
// ЧТО ПРОВЕРЯЕТ (тем же разбором пакетов, что в игре — `DebugFeedPacket` → `HandlePacket`; окно открыто вкладкой, как кнопкой):
//   К1. Первое открытие вкладки «Клан»: 4125 со своей записью (в игре), членом в игре и членом не в игре → строки 1/2 зелёные
//       со значком, строка 3 серая без значка.
//   К2. Окно открыто: член 3 вошёл (4133 вид 1, флаг 1) → строка 3 зелёная со значком; член 2 вышел (вид 1, флаг 0) → серая.
//   К3. Повторное открытие вкладки: свежий список 4125 заменяет прежний, статусы — из него.
//   Д1. Вкладка «Друг»: 4157 (друг 1 в сети, друг 2 нет) → строка 1 «в сети», строка 2 «вне сети».
//   Д2. Окно открыто: 4160 друг 2 в сети → строка 2 «в сети»; 4160 друг 1 вне сети → строка 1 «вне сети».
//   Кадры — `Reports/batch17/community/<язык>_<кадр>.png`. Клан, окно и ключи возвращаются.
//
// Консоль: `asda.Community.OnlineSelfTest [delay=N] [quit]`. Только офлайн. Строки — «САМОТЕСТ-ОНЛАЙН», итог — «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaCommunitySubsystem.h"
#include "UI/AsdaCommunityClanSubsystem.h"
#include "UI/AsdaCommunityFriendSubsystem.h"   // AsdaCommunityUi::Find / FindLabel
#include "UI/AsdaNotice.h"
#include "UI/AsdaUiHelpers.h"
#include "World/AsdaWorldSubsystem.h"
#include "Text/AsdaLang.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaOnlineTest, Log, All);

namespace AsdaOnlineTestLocal
{
	static const TCHAR* kWho = TEXT("asda.Community.OnlineSelfTest");   // имя самотеста (сторож ключей)
	constexpr int32 kClan = 7;              // тестовый клан
	constexpr int32 kLocInGame  = 12227;    // «В игре» (зелёный тег FF93FF93)
	constexpr int32 kLocOutGame = 12228;    // «Не в игре»

	// cp1251-строка в тело (латиница — без перекодировки).
	static void PutStr(TArray<uint8>& B, int32 At, const char* S, int32 Max)
	{
		for (int32 i = 0; S[i] && i < Max - 1; ++i) { B[At + i] = (uint8)S[i]; }
	}
	// Запись члена клана 131 б (раскладка `FAsdaClanMemberRecord`).
	static TArray<uint8> Rec131(int32 User, uint8 CharIdx, const char* Name, uint8 Grade, uint8 Online, uint8 Ch, uint8 Map)
	{
		TArray<uint8> B; B.SetNumZeroed(131);
		AsdaBytes::WrI32(B.GetData(), User); B[4] = CharIdx;
		PutStr(B, 5, Name, 20);
		B[25] = 1; B[26] = 30; B[27] = 1; B[33] = Grade;
		B[111] = Ch; B[112] = Map; B[113] = Online;
		return B;
	}
	// 4133: [0] вид · [1..2] клан · [3..133] запись.
	static TArray<uint8> Body4133(uint8 Kind, const TArray<uint8>& Rec)
	{
		TArray<uint8> B; B.SetNumZeroed(3); B[0] = Kind; AsdaBytes::WrI16(B.GetData() + 1, (int16)kClan);
		B.Append(Rec);
		return B;
	}
	// Запись приятеля 31 б (раскладка `FAsdaFriendRecord`).
	static TArray<uint8> Rec31(int32 CharId, const char* Name, uint8 Online)
	{
		TArray<uint8> B; B.SetNumZeroed(31);
		AsdaBytes::WrI32(B.GetData(), CharId); B[4] = 1; B[5] = 0; B[6] = 3; B[7] = 25; B[8] = 1; B[10] = Online;
		PutStr(B, 11, Name, 20);
		return B;
	}
}

struct FAsdaOnlineSelfTest
{
	int32 Ok = 0;     // пройдено
	int32 Fail = 0;   // провалено
	TArray<TFunction<void(UWorld*)>> Steps;   // шаги, по одному за тик (перерисовка и геометрия успевают к кадру)
	bool bSetupFailed = false;                // нет подсистем — шаги не выполняются
	FString Lang;                             // ru / en — префикс кадров
	FString ShotDir;                          // папка кадров
	FString MyName;                           // имя своего персонажа (своя запись клана)
	int32 ClanWas = -1;                       // свой клан до прогона
	FString ClanNameWas;                      // имя своего клана до прогона
	uint8 RankWas = 0;                        // своё звание до прогона

	struct FSubs
	{
		UGameInstance* GI = nullptr; UAsdaNetSubsystem* Net = nullptr; UAsdaCommunitySubsystem* Cm = nullptr;
		UAsdaWorldSubsystem* WS = nullptr;
		bool Ok() const { return GI && Net && Cm && WS; }
	};
	static FSubs Get(UWorld* W)
	{
		FSubs S; S.GI = W ? W->GetGameInstance() : nullptr;
		if (!S.GI) { return S; }
		S.Net = S.GI->GetSubsystem<UAsdaNetSubsystem>(); S.Cm = S.GI->GetSubsystem<UAsdaCommunitySubsystem>();
		S.WS = S.GI->GetSubsystem<UAsdaWorldSubsystem>();
		return S;
	}
	void Expect(bool b, const FString& What)
	{
		if (b) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaOnlineTest, Log, TEXT("САМОТЕСТ-ОНЛАЙН %s: %s"), b ? TEXT("ok") : TEXT("ПРОВАЛ"), *What);
	}
	void Shot(const TCHAR* Name)
	{
		const FString File = FPaths::Combine(ShotDir, FString::Printf(TEXT("%s_%s.png"), *Lang, Name));
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaOnlineTest, Log, TEXT("САМОТЕСТ-ОНЛАЙН кадр: %s"), *File);
	}
	// Текст надписи окна (как видит игрок — `UTextBlock::GetText`).
	static FString Txt(const FSubs& S, const FString& Idc)
	{
		UUserWidget* W = S.Cm->GetWindowWidget();
		const UTextBlock* T = W ? AsdaCommunityUi::FindLabel(W, Idc) : nullptr;
		return T ? T->GetText().ToString() : FString(TEXT("<нет узла>"));
	}
	// Узел видим на экране (эффективная видимость + ненулевая геометрия).
	static bool Shown(const FSubs& S, const FString& Idc)
	{
		UUserWidget* W = S.Cm->GetWindowWidget();
		UWidget* N = W ? AsdaCommunityUi::Find(W, Idc) : nullptr;
		FVector2D Mn, Mx;
		return N && AsdaUi::IsShown(N) && AsdaUi::WidgetAbsRect(N, Mn, Mx) && (Mx - Mn).X > 1.0 && (Mx - Mn).Y > 1.0;
	}
	// Строка клана N «в игре»: текст начинается с «В игре» и значок виден.
	void ExpectClanRow(const FSubs& S, int32 N, bool bOnline, const TCHAR* What)
	{
		using namespace AsdaOnlineTestLocal;
		const FString T = Txt(S, FString::Printf(TEXT("CMTY_CLAN_MEMON%d"), N));
		const bool bIcon = Shown(S, FString::Printf(TEXT("CMTY_CLAN_MEMON%d_On"), N));
		const FString In = AsdaNotice::Loc(S.GI, kLocInGame), Out = AsdaNotice::Loc(S.GI, kLocOutGame);
		const bool bGood = bOnline ? (T.StartsWith(In) && bIcon) : (T == Out && !bIcon);
		Expect(bGood, FString::Printf(TEXT("%s: строка клана %d «%s», значок %d (ждём %s)"), What, N, *T, bIcon ? 1 : 0,
			bOnline ? *FString::Printf(TEXT("«%s - …», значок 1"), *In) : *FString::Printf(TEXT("«%s», значок 0"), *Out)));
	}
	// Строка друга N «в сети».
	void ExpectFriendRow(const FSubs& S, int32 N, bool bOnline, const FString& Name, const TCHAR* What)
	{
		const bool bOn = Shown(S, FString::Printf(TEXT("CMTY_FRND_STAT%dOn"), N));
		const bool bOff = Shown(S, FString::Printf(TEXT("CMTY_FRND_STAT%dOff"), N));
		const FString NameIdc = bOnline ? FString::Printf(TEXT("CMTY_FRND_ONNAME%d"), N) : FString::Printf(TEXT("CMTY_FRND_OFFNAME%d"), N);
		const FString Nm = Txt(S, NameIdc);
		const bool bGood = (bOnline ? (bOn && !bOff) : (!bOn && bOff)) && Nm == Name && Shown(S, NameIdc);
		Expect(bGood, FString::Printf(TEXT("%s: строка друга %d — On %d / Off %d, %s «%s» (ждём %s, «%s»)"), What, N,
			bOn ? 1 : 0, bOff ? 1 : 0, *NameIdc, *Nm, bOnline ? TEXT("в сети") : TEXT("вне сети"), *Name));
	}
	// Список клана: своя запись (в игре), Online (в игре), Offline (не в игре).
	TArray<uint8> ClanList(uint8 OnlineFlag2, uint8 OnlineFlag3) const
	{
		using namespace AsdaOnlineTestLocal;
		const FTCHARToUTF8 Me(*MyName);
		TArray<uint8> L = Rec131(1001, 0, Me.Get(), 1, 1, 0, 3);
		L.Append(Rec131(5001, 0, "Online", 2, OnlineFlag2, 1, 3));
		L.Append(Rec131(5002, 0, "Offline", 3, OnlineFlag3, 0, 3));
		return L;
	}

	void Setup(UWorld* W)
	{
		FSubs S = Get(W);
		if (!S.Ok()) { Expect(false, TEXT("нет подсистем (сеть/сообщество/мир)")); bSetupFailed = true; return; }
		Lang = FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en");
		ShotDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/batch17/community")));
		MyName = S.WS->GetSelfCharName();
		if (MyName.IsEmpty()) { MyName = TEXT("SelfTester"); }
		ClanWas = S.WS->GetSelfClanId(); ClanNameWas = S.Net->SelfClanName; RankWas = S.Net->SelfClanRank;
		AsdaCVarSave::SetForTest(TEXT("asda.Community.Friend.Handle4160"), 1);
		S.Net->OnClanInfo.Broadcast(-1, AsdaOnlineTestLocal::kClan, TEXT("TestClan"), FString());
		S.Net->SelfClanRank = 1;
	}

	void BuildSteps()
	{
		// К1. Первое открытие вкладки «Клан» + список.
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W);
			S.Cm->OpenOnTab(2);
			Expect(S.Cm->IsWindowOpen() && S.Cm->GetActiveTab() == 2, FString::Printf(TEXT("К1: окно открыто на вкладке «Клан» (вкладка %d)"), S.Cm->GetActiveTab()));
			S.Net->DebugFeedPacket(4125, ClanList(1, 0));
			S.Net->DebugFeedPacket(4126, TArray<uint8>());
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W);
			ExpectClanRow(S, 1, true,  TEXT("К1 своя запись"));
			ExpectClanRow(S, 2, true,  TEXT("К1 член в игре"));
			ExpectClanRow(S, 3, false, TEXT("К1 член не в игре"));
			Shot(TEXT("1_clan_first_open"));
		});
		// К2. Окно открыто: вход члена 3, выход члена 2.
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W);
			S.Net->DebugFeedPacket(4133, AsdaOnlineTestLocal::Body4133(1, AsdaOnlineTestLocal::Rec131(5002, 0, "Offline", 3, 1, 2, 3)));
			S.Net->DebugFeedPacket(4133, AsdaOnlineTestLocal::Body4133(1, AsdaOnlineTestLocal::Rec131(5001, 0, "Online", 2, 0, 0, 3)));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W);
			ExpectClanRow(S, 3, true,  TEXT("К2 вход члена (4133 вид 1, флаг 1)"));
			ExpectClanRow(S, 2, false, TEXT("К2 выход члена (4133 вид 1, флаг 0)"));
			ExpectClanRow(S, 1, true,  TEXT("К2 своя запись без изменений"));
			Shot(TEXT("2_clan_login_logout"));
		});
		// К3. Повторное открытие: свежий список.
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W);
			S.Cm->CloseWindow();
			S.Cm->OpenOnTab(2);
			S.Net->DebugFeedPacket(4125, ClanList(1, 1));
			S.Net->DebugFeedPacket(4126, TArray<uint8>());
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W);
			const UAsdaCommunityClanSubsystem* Cl = S.GI->GetSubsystem<UAsdaCommunityClanSubsystem>();
			Expect(Cl && Cl->GetMemberCount() == 3, FString::Printf(TEXT("К3 повторное открытие: в составе %d (ждём 3, без дублей)"), Cl ? Cl->GetMemberCount() : -1));
			ExpectClanRow(S, 2, true, TEXT("К3 повторное открытие, член 2 в игре"));
			ExpectClanRow(S, 3, true, TEXT("К3 повторное открытие, член 3 в игре"));
			Shot(TEXT("3_clan_reopen"));
		});
		// Д1. Вкладка «Друг» + список.
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W);
			S.Cm->SwitchTab(1);
			Expect(S.Cm->GetActiveTab() == 1, FString::Printf(TEXT("Д1: вкладка «Друг» (вкладка %d)"), S.Cm->GetActiveTab()));
			TArray<uint8> L = AsdaOnlineTestLocal::Rec31(9001, "FriendOn", 1);
			L.Append(AsdaOnlineTestLocal::Rec31(9002, "FriendOff", 0));
			S.Net->DebugFeedPacket(4157, L);
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W);
			ExpectFriendRow(S, 1, true,  TEXT("FriendOn"),  TEXT("Д1 друг в сети"));
			ExpectFriendRow(S, 2, false, TEXT("FriendOff"), TEXT("Д1 друг вне сети"));
			Shot(TEXT("4_friend_first_open"));
		});
		// Д2. Окно открыто: 4160 — друг 2 вошёл, друг 1 вышел.
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W);
			auto Body4160 = [](const TArray<uint8>& Rec) { TArray<uint8> B; B.SetNumZeroed(2); B.Append(Rec); return B; };
			S.Net->DebugFeedPacket(4160, Body4160(AsdaOnlineTestLocal::Rec31(9002, "FriendOff", 1)));
			S.Net->DebugFeedPacket(4160, Body4160(AsdaOnlineTestLocal::Rec31(9001, "FriendOn", 0)));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W);
			ExpectFriendRow(S, 2, true,  TEXT("FriendOff"), TEXT("Д2 4160 друг 2 вошёл"));
			ExpectFriendRow(S, 1, false, TEXT("FriendOn"),  TEXT("Д2 4160 друг 1 вышел"));
			Shot(TEXT("5_friend_update"));
		});
	}

	void Finish(UWorld* W)
	{
		FSubs S = Get(W);
		AsdaCVarSave::RestoreAll(AsdaOnlineTestLocal::kWho);
		if (!S.Ok()) { return; }
		S.Cm->CloseWindow();
		S.Net->DebugFeedPacket(4157, TArray<uint8>());
		S.Net->OnClanInfo.Broadcast(-1, ClanWas, ClanNameWas, FString());
		S.Net->SelfClanName = ClanNameWas;
		S.Net->SelfClanRank = RankWas;
	}
};

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GAsdaOnlineSelfTest(
		TEXT("asda.Community.OnlineSelfTest"),
		TEXT("asda.Community.OnlineSelfTest [delay=N] [quit] — ОФЛАЙН-самотест пачки 17 (баг 4): онлайн-статус во вкладках «Клан» (4125/4133) ")
		TEXT("и «Друг» (4157/4160) — первое открытие, вход/выход при открытом окне, своя запись; кадры RU/EN."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const TCHAR* Who = AsdaOnlineTestLocal::kWho;
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
				TSharedRef<FAsdaOnlineSelfTest> T = MakeShared<FAsdaOnlineSelfTest>();
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
					UE_LOG(LogAsdaOnlineTest, Log, TEXT("САМОТЕСТ-ОНЛАЙН ИТОГ: ok=%d fail=%d"), T->Ok, T->Fail);
					AsdaSelfTestKit::Quit(W3, bQuit);
					return false;
				}), 0.8f);
				return false;
			}), KitArgs.Delay);
			UE_LOG(LogAsdaOnlineTest, Log, TEXT("САМОТЕСТ-ОНЛАЙН: запуск через %.1f с"), KitArgs.Delay);
		}));
}

#endif // !UE_BUILD_SHIPPING
