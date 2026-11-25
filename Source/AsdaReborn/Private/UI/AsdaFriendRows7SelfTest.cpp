// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ: 7 ПРИЯТЕЛЕЙ НА СТРАНИЦУ ВО ВКЛАДКЕ «ДРУГ» ОКНА СООБЩЕСТВА 6400 (пачка 17, баг 7, 08.11.2026). [ИСПРАВЛЕНИЕ]
//
// РЕШЕНИЕ ВЛАДЕЛЬЦА «friend-list — make 7». Оригинал держит 6 строк (`social.c:5179-5194`, `uiTradeListRefreshPage` ui_core.c:44839),
//   7-я строка разметки мёртвая — под 6-й пустое место. Ассет: шаг пост-импорта `asda_ui_friend_row7` (группа `CMTY_FRND_LIST7_grp`
//   видима). Код: `UAsdaCommunityFriendSubsystem` считает по `FriendRows()` (ключ `asda.Community.FriendRows7`, 0 = 6 строк).
//
// ЧТО ПРОВЕРЯЕТ (путь игрока: окно открыто вкладкой, список — штатный разбор 4157, нажатия — настоящим щелчком мыши Slate
//   `UAsdaWindowSubsystem::SimulateClickAt` в центр узла):
//   N = 6, 7, 8, 14 приятелей: на странице 1 строки 1…min(N,7) с именами по порядку, остальные пусты; 7-я строка на экране и
//     ниже 6-й на шаг строк (57); счётчик страниц = ceil(N/7) (6→1, 7→1, 8→2, 14→2).
//   N = 8: «вперёд» → страница 2: строка 1 = 8-й приятель, строки 2…7 пусты; «назад» → страница 1.
//   N = 14: щелчок по 7-й строке → рамка выбора на ней; «Шёпот» → адресат = 7-й приятель; «Удалить» → вопрос 20002 с именем
//     7-го, «Да» → 4154 с его charId (перехват, в сеть не уходит); «в конец» → страница 2: щелчок по 7-й строке → 14-й,
//     шёпот 14-му, удаление 14-го (4154 с его charId).
//   Контроль ключа 0: 14 приятелей → 6 строк, 3 страницы, 7-я группа скрыта, щелчок по 6-й строке → 6-й приятель.
//   Кадры — `Reports/batch17/community/<язык>_rows7_*.png`. Окно, список и ключи возвращаются.
//
// Консоль: `asda.Community.FriendRows7SelfTest [delay=N] [quit]`. Только офлайн. Строки — «САМОТЕСТ-ДРУЗЬЯ7», итог — «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaChatWindowSubsystem.h"        // DebugWhisperTarget — адресат шёпота после кнопки 14509
#include "UI/AsdaCommunitySubsystem.h"
#include "UI/AsdaCommunityFriendSubsystem.h"   // AsdaCommunityUi::Find / FindLabel
#include "UI/AsdaNotice.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"            // SimulateClickAt, окно-вопрос TWOBUT_MSGBOX
#include "Text/AsdaLang.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaFriendRows7Test, Log, All);

namespace AsdaFriendRows7TestLocal
{
	static const TCHAR* kWho = TEXT("asda.Community.FriendRows7SelfTest");   // имя самотеста (сторож ключей)
	static const TCHAR* kTwoBut = TEXT("TWOBUT_MSGBOX");                     // окно-вопрос 1520
	constexpr int32 kCharIdBase = 9100;                                      // charId приятеля i = 9100 + i (i с 1)
	constexpr float kRowStep = 57.f;                                         // шаг строк разметки (14520…14570: T 29, 86 … 314, 371)

	// Имя приятеля i (1-based): F01…F14.
	static FString NameOf(int32 I) { return FString::Printf(TEXT("F%02d"), I); }
	// Тело 4157: N записей по 31 б (раскладка `FAsdaFriendRecord`), все вне сети.
	static TArray<uint8> Body4157(int32 N)
	{
		TArray<uint8> B;
		for (int32 i = 1; i <= N; ++i)
		{
			TArray<uint8> R; R.SetNumZeroed(31);
			AsdaBytes::WrI32(R.GetData(), kCharIdBase + i); R[4] = 1; R[7] = 20; R[8] = 1; R[10] = 0;
			const FTCHARToUTF8 Nm(*NameOf(i));
			for (int32 k = 0; k < Nm.Length() && k < 19; ++k) { R[11 + k] = (uint8)Nm.Get()[k]; }
			B.Append(R);
		}
		return B;
	}
}

struct FAsdaFriendRows7SelfTest
{
	int32 Ok = 0;     // пройдено
	int32 Fail = 0;   // провалено
	TArray<TFunction<void(UWorld*)>> Steps;   // шаги, по одному за тик (перерисовка и геометрия успевают к щелчку и кадру)
	bool bSetupFailed = false;                // нет подсистем — шаги не выполняются
	FString Lang;                             // ru / en — префикс кадров
	FString ShotDir;                          // папка кадров
	uint32 CapGen = 0;                        // поколение перехвата отправок (4154)
	TSharedRef<TArray<int32>> Del4154 = MakeShared<TArray<int32>>();   // charId из перехваченных 4154

	struct FSubs
	{
		UGameInstance* GI = nullptr; UAsdaNetSubsystem* Net = nullptr; UAsdaCommunitySubsystem* Cm = nullptr;
		UAsdaWindowSubsystem* WM = nullptr; UAsdaChatWindowSubsystem* Chat = nullptr;
		bool Ok() const { return GI && Net && Cm && WM && Chat; }
	};
	static FSubs Get(UWorld* W)
	{
		FSubs S; S.GI = W ? W->GetGameInstance() : nullptr;
		if (!S.GI) { return S; }
		S.Net = S.GI->GetSubsystem<UAsdaNetSubsystem>(); S.Cm = S.GI->GetSubsystem<UAsdaCommunitySubsystem>();
		S.WM = S.GI->GetSubsystem<UAsdaWindowSubsystem>(); S.Chat = S.GI->GetSubsystem<UAsdaChatWindowSubsystem>();
		return S;
	}
	void Expect(bool b, const FString& What)
	{
		if (b) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaFriendRows7Test, Log, TEXT("САМОТЕСТ-ДРУЗЬЯ7 %s: %s"), b ? TEXT("ok") : TEXT("ПРОВАЛ"), *What);
	}
	void Shot(const FString& Name)
	{
		const FString File = FPaths::Combine(ShotDir, FString::Printf(TEXT("%s_rows7_%s.png"), *Lang, *Name));
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaFriendRows7Test, Log, TEXT("САМОТЕСТ-ДРУЗЬЯ7 кадр: %s"), *File);
	}
	static UWidget* Node(const FSubs& S, const FString& Idc)
	{
		UUserWidget* W = S.Cm->GetWindowWidget();
		return W ? AsdaCommunityUi::Find(W, Idc) : nullptr;
	}
	static FString Txt(const FSubs& S, const FString& Idc)
	{
		UUserWidget* W = S.Cm->GetWindowWidget();
		const UTextBlock* T = W ? AsdaCommunityUi::FindLabel(W, Idc) : nullptr;
		return T ? T->GetText().ToString() : FString(TEXT("<нет узла>"));
	}
	// Прямоугольник узла на экране (false — не показан или нулевой).
	static bool Rect(UWidget* N, FVector2D& Mn, FVector2D& Mx)
	{
		return N && AsdaUi::IsShown(N) && AsdaUi::WidgetAbsRect(N, Mn, Mx) && (Mx - Mn).X > 1.0 && (Mx - Mn).Y > 1.0;
	}
	// Группа строки r (1-based) по точному имени узла.
	static UWidget* RowGrp(const FSubs& S, int32 R)
	{
		UUserWidget* W = S.Cm->GetWindowWidget();
		return W ? AsdaUi::FindNodeExact(W, *FString::Printf(TEXT("CMTY_FRND_LIST%d_grp"), R)) : nullptr;
	}
	// Настоящий щелчок мышью в центр узла.
	bool ClickW(const FSubs& S, UWidget* N, const FString& Tag)
	{
		FVector2D Mn, Mx;
		const bool bGeo = Rect(N, Mn, Mx);
		Expect(bGeo, FString::Printf(TEXT("%s: узел на экране (щелчок)"), *Tag));
		if (bGeo) { S.WM->SimulateClickAt((Mn + Mx) * 0.5f, 2); }
		return bGeo;
	}
	bool Click(const FSubs& S, const FString& Idc) { return ClickW(S, Node(S, Idc), Idc); }
	// Имя в строке r: в сети — ONNAME, иначе OFFNAME (все приятели теста вне сети).
	static FString RowName(const FSubs& S, int32 R) { return Txt(S, FString::Printf(TEXT("CMTY_FRND_OFFNAME%d"), R)); }
	// Страница r: строки 1…Rows содержат приятелей First…, остальные пусты.
	void ExpectPage(const FSubs& S, int32 Rows, int32 N, int32 Page, int32 Pages, const FString& Tag)
	{
		using namespace AsdaFriendRows7TestLocal;
		int32 Bad = 0;
		FString Seen;
		for (int32 r = 1; r <= Rows; ++r)
		{
			const int32 Abs = (Page - 1) * Rows + r;
			const FString Want = Abs <= N ? NameOf(Abs) : FString();
			const FString Got = RowName(S, r);
			// Заполненная строка видна ЦЕЛИКОМ: уровень «20» и статус «Выход/Log_off» на экране (не только полоса имени).
			FVector2D Mn, Mx;
			const bool bLv = Rect(Node(S, FString::Printf(TEXT("CMTY_FRND_LV%d"), r)), Mn, Mx) && Txt(S, FString::Printf(TEXT("CMTY_FRND_LV%d"), r)) == TEXT("20");
			const bool bSt = Rect(Node(S, FString::Printf(TEXT("CMTY_FRND_STAT%d"), r)), Mn, Mx);
			const bool bFull = Want.IsEmpty() || (bLv && bSt);
			Seen += FString::Printf(TEXT("%d:«%s»%s "), r, *Got, bFull ? TEXT("") : TEXT("(ур./статус не видны)"));
			if (Got != Want || !bFull) { ++Bad; }
		}
		const FString P1 = Txt(S, TEXT("CMTY_FRND_PAGE1")), P2 = Txt(S, TEXT("CMTY_FRND_PAGE2"));
		Expect(Bad == 0 && P1 == FString::FromInt(Page) && P2 == FString::FromInt(Pages),
			FString::Printf(TEXT("%s: строки %s| страница «%s/%s» (ждём %d/%d, неверных строк %d)"), *Tag, *Seen, *P1, *P2, Page, Pages, Bad));
	}
	// Геометрия 7-й строки: на экране, ниже 6-й на шаг строк.
	void ExpectRow7Geometry(const FSubs& S, bool bWantShown, const FString& Tag)
	{
		FVector2D A0, A1, B0, B1;
		const bool b6 = Rect(RowGrp(S, 6), A0, A1);
		const bool b7 = Rect(RowGrp(S, 7), B0, B1);
		if (bWantShown)
		{
			// Шаг в экранных пикселях = шаг разметки × масштаб окна (отношение высот строк к 53).
			const float Scale = (A1.Y - A0.Y) / 53.f;
			const float Step = B0.Y - A0.Y;
			Expect(b6 && b7 && FMath::Abs(Step - AsdaFriendRows7TestLocal::kRowStep * Scale) < 2.f && FMath::Abs(B0.X - A0.X) < 1.f,
				FString::Printf(TEXT("%s: 7-я строка на экране под 6-й (шаг %.1f, ждём %.1f; x %.1f/%.1f)"), *Tag, Step,
					AsdaFriendRows7TestLocal::kRowStep * Scale, A0.X, B0.X));
		}
		else
		{
			Expect(b6 && !b7, FString::Printf(TEXT("%s: 7-я строка скрыта (6-я на экране %d, 7-я %d)"), *Tag, b6 ? 1 : 0, b7 ? 1 : 0));
		}
	}
	// Выбранная строка: рамка выбора (`CMTY_FRND_SEL`) у строки r.
	bool SelAt(const FSubs& S, int32 R)
	{
		FVector2D R0, R1;
		if (!Rect(RowGrp(S, R), R0, R1)) { return false; }
		UUserWidget* W = S.Cm->GetWindowWidget();
		UWidget* Sel = W ? AsdaUi::FindNodeExact(W, TEXT("CMTY_FRND_SEL_645_0")) : nullptr;
		FVector2D S0, S1;
		return Rect(Sel, S0, S1) && FMath::Abs(S0.Y - R0.Y) < 6.f;
	}
	// Удалить выбранного — три шага (окно-вопрос и щелчок доходят на следующих кадрах): «Удалить» → вопрос с именем →
	//   «Да» → 4154 с charId.
	void AddDelete(int32 Abs, const FString& Tag)
	{
		using namespace AsdaFriendRows7TestLocal;
		Steps.Add([this](UWorld* W) { FSubs S = Get(W); Del4154->Reset(); Click(S, TEXT("CMTY_FRND_DELBTN")); });
		Steps.Add([this, Abs, Tag](UWorld* W)
		{
			FSubs S = Get(W);
			UUserWidget* Box = S.WM->FindWindow(FName(kTwoBut));
			const FString Q = (Box && AsdaUi::IsShown(Box)) ? UAsdaWindowSubsystem::ReadMsgBoxShownText(AsdaUi::FindNodeByName(Box, TEXT("TWOBUT_TEXT"), nullptr)) : FString();
			Expect(Q.Contains(NameOf(Abs)), FString::Printf(TEXT("%s: «Удалить» → вопрос «%s» (ждём имя %s)"), *Tag, *Q, *NameOf(Abs)));
			Shot(FString::Printf(TEXT("%s_delete_ask"), *Tag.Left(2)));
			ClickW(S, Box ? AsdaUi::FindNodeByName(Box, TEXT("TWOBUT_BUT00"), nullptr) : nullptr, Tag + TEXT(" «Да»"));
		});
		Steps.Add([this, Abs, Tag](UWorld* W)
		{
			Expect(Del4154->Num() == 1 && (*Del4154)[0] == kCharIdBase + Abs,
				FString::Printf(TEXT("%s: «Да» → 4154 ×%d, charId %d (ждём ×1, %d)"), *Tag, Del4154->Num(),
					Del4154->Num() ? (*Del4154)[0] : -1, kCharIdBase + Abs));
		});
	}

	void Setup(UWorld* W)
	{
		FSubs S = Get(W);
		if (!S.Ok()) { Expect(false, TEXT("нет подсистем (сеть/сообщество/окна/чат)")); bSetupFailed = true; return; }
		Lang = FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en");
		ShotDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/batch17/community")));
		AsdaCVarSave::SetForTest(TEXT("asda.Community.FriendRows7"), 1);
		TSharedRef<TArray<int32>> D = Del4154;
		CapGen = AsdaSelfTestKit::ArmCapture(S.Net, [D](uint16 Op, const uint8* Body, int32 Len, bool, TCHAR)
		{
			// Полезная нагрузка 4154 (без конверта): @0 i32 charId (`SendFriendDelete4154`).
			if (Op == 4154 && Len >= 4) { D->Add(AsdaBytes::RdI32(Body)); }
		}, /*весь трафик: только так `IsSendLinkUp` пропускает отправку офлайн*/0, AsdaFriendRows7TestLocal::kWho);
		Expect(CapGen != 0, TEXT("перехват 4154 взведён"));
		S.Cm->OpenOnTab(1);
	}

	// Шаги «показать N приятелей и проверить страницу 1».
	void AddList(int32 N, int32 Pages)
	{
		Steps.Add([this, N](UWorld* W) { FSubs S = Get(W); S.Net->DebugFeedPacket(4157, AsdaFriendRows7TestLocal::Body4157(N)); });
		Steps.Add([this, N, Pages](UWorld* W)
		{
			FSubs S = Get(W);
			const FString Tag = FString::Printf(TEXT("N=%d"), N);
			ExpectPage(S, 7, N, 1, Pages, Tag);
			ExpectRow7Geometry(S, true, Tag);
			Shot(FString::Printf(TEXT("n%02d_page1"), N));
		});
	}

	void BuildSteps()
	{
		Steps.Add([this](UWorld* W) { FSubs S = Get(W); Expect(S.Cm->IsWindowOpen() && S.Cm->GetActiveTab() == 1, TEXT("окно открыто на вкладке «Друг»")); });
		AddList(6, 1);
		AddList(7, 1);
		AddList(8, 2);
		// 8: листание.
		Steps.Add([this](UWorld* W) { FSubs S = Get(W); Click(S, TEXT("CMTY_FRND_NEXTBTN1")); });
		Steps.Add([this](UWorld* W) { FSubs S = Get(W); ExpectPage(S, 7, 8, 2, 2, TEXT("N=8 «вперёд»")); Shot(TEXT("n08_page2")); Click(S, TEXT("CMTY_FRND_PREBTN1")); });
		Steps.Add([this](UWorld* W) { FSubs S = Get(W); ExpectPage(S, 7, 8, 1, 2, TEXT("N=8 «назад»")); });
		AddList(14, 2);
		// 14: щелчок по 7-й строке на странице 1, шёпот, удаление.
		Steps.Add([this](UWorld* W) { FSubs S = Get(W); ClickW(S, RowGrp(S, 7), TEXT("N=14 стр.1 строка 7")); });
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W);
			Expect(SelAt(S, 7), TEXT("N=14 стр.1: рамка выбора на 7-й строке"));
			Shot(TEXT("n14_sel_row7"));
			Click(S, TEXT("CMTY_FRND_WHISPBTN"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W);
			Expect(S.Chat->DebugWhisperTarget() == AsdaFriendRows7TestLocal::NameOf(7),
				FString::Printf(TEXT("N=14 стр.1: «Шёпот» → адресат «%s» (ждём %s)"), *S.Chat->DebugWhisperTarget(), *AsdaFriendRows7TestLocal::NameOf(7)));
		});
		AddDelete(7, TEXT("p1 удаление 7-го"));
		// Страница 2: «в конец», щелчок по 7-й строке → 14-й.
		Steps.Add([this](UWorld* W) { FSubs S = Get(W); Click(S, TEXT("CMTY_FRND_ENDBTN1")); });
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W);
			ExpectPage(S, 7, 14, 2, 2, TEXT("N=14 «в конец»"));
			ClickW(S, RowGrp(S, 7), TEXT("N=14 стр.2 строка 7"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W);
			Expect(SelAt(S, 7), TEXT("N=14 стр.2: рамка выбора на 7-й строке"));
			Shot(TEXT("n14_page2_sel_row7"));
			Click(S, TEXT("CMTY_FRND_WHISPBTN"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W);
			Expect(S.Chat->DebugWhisperTarget() == AsdaFriendRows7TestLocal::NameOf(14),
				FString::Printf(TEXT("N=14 стр.2: «Шёпот» → адресат «%s» (ждём %s)"), *S.Chat->DebugWhisperTarget(), *AsdaFriendRows7TestLocal::NameOf(14)));
		});
		AddDelete(14, TEXT("p2 удаление 14-го"));
		// Контроль ключа 0: 6 строк оригинала.
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W);
			AsdaCVarSave::SetForTest(TEXT("asda.Community.FriendRows7"), 0);
			Click(S, TEXT("CMTY_FRND_STATBTN1"));   // в начало
			S.Net->DebugFeedPacket(4157, AsdaFriendRows7TestLocal::Body4157(14));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W);
			ExpectPage(S, 6, 14, 1, 3, TEXT("ключ 0, N=14"));
			ExpectRow7Geometry(S, false, TEXT("ключ 0"));
			Shot(TEXT("key0_n14"));
			ClickW(S, RowGrp(S, 6), TEXT("ключ 0 строка 6"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W);
			Click(S, TEXT("CMTY_FRND_WHISPBTN"));
		});
		Steps.Add([this](UWorld* W)
		{
			FSubs S = Get(W);
			Expect(SelAt(S, 6) && S.Chat->DebugWhisperTarget() == AsdaFriendRows7TestLocal::NameOf(6),
				FString::Printf(TEXT("ключ 0: строка 6 → шёпот «%s» (ждём %s)"), *S.Chat->DebugWhisperTarget(), *AsdaFriendRows7TestLocal::NameOf(6)));
			AsdaCVarSave::SetForTest(TEXT("asda.Community.FriendRows7"), 1);
		});
	}

	void Finish(UWorld* W)
	{
		FSubs S = Get(W);
		AsdaCVarSave::RestoreAll(AsdaFriendRows7TestLocal::kWho);
		if (!S.Ok()) { return; }
		if (CapGen) { AsdaSelfTestKit::DisarmCapture(S.Net, CapGen); }
		S.WM->ClearMsgBoxQueue(TEXT("самотест друзья7: конец"));
		S.Net->DebugFeedPacket(4157, TArray<uint8>());
		S.Chat->DebugSetWhisperTarget(FString());
		S.Cm->CloseWindow();
	}
};

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GAsdaFriendRows7SelfTest(
		TEXT("asda.Community.FriendRows7SelfTest"),
		TEXT("asda.Community.FriendRows7SelfTest [delay=N] [quit] — ОФЛАЙН-самотест пачки 17 (баг 7): 7 приятелей на страницу — 6/7/8/14, листание, ")
		TEXT("щелчок по 7-й строке на 1-й и 2-й странице, шёпот, удаление (4154 перехватом), контроль ключа asda.Community.FriendRows7 0; кадры RU/EN."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const TCHAR* Who = AsdaFriendRows7TestLocal::kWho;
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
				TSharedRef<FAsdaFriendRows7SelfTest> T = MakeShared<FAsdaFriendRows7SelfTest>();
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
					UE_LOG(LogAsdaFriendRows7Test, Log, TEXT("САМОТЕСТ-ДРУЗЬЯ7 ИТОГ: ok=%d fail=%d"), T->Ok, T->Fail);
					AsdaSelfTestKit::Quit(W3, bQuit);
					return false;
				}), 0.7f);
				return false;
			}), KitArgs.Delay);
			UE_LOG(LogAsdaFriendRows7Test, Log, TEXT("САМОТЕСТ-ДРУЗЬЯ7: запуск через %.1f с"), KitArgs.Delay);
		}));
}

#endif // !UE_BUILD_SHIPPING
