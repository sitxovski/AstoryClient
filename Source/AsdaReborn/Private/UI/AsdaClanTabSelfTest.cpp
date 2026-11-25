// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ ВКЛАДКИ «КЛАН» ОКНА СООБЩЕСТВА 6400 (01.11.2026, сессия clan-window): `asda.Clan.TabSelfTest [shots=<папка>] [quit]`. [ПОРТ]
//   Путь игрока: окно 6400 на вкладке 2 (`OpenOnTab(2)`), НАСТОЯЩИЕ щелчки Slate по кнопкам (`SimulateClickAt`), набор
//   символьными событиями Slate, ответы «Да»/«Нет» — щелчком по кнопкам окна-вопроса TWOBUT. Приём — `DebugFeedPacket` →
//   `HandlePacket` → делегат. Исходящие перехватываются (`BeginSendCapture`) — в сеть не уходит ничего. Офлайн (-AsdaNoNet).
//   Между шагами — ПОЛНАЯ сборка мусора: кнопки, чьи реле держатся только слабо, после неё молча умирают (так ломались окна 02.09).
//   Этапы:
//     0  вход: 4135 своей карточки (звание 1 в [23]) → своё звание = 1 до любого списка (эталон `charUpdateVisibleCharsFromPacket`);
//     1  вкладка 2 → 4122 + 4124; 4123/4125/4126 → инфо-панель и шесть строк состава; пагинация «вперёд/в конец/назад/в начало»;
//     2  «Информация» → 6511; 6512 → строка объявления; заметка: набор + «Сохранить» → 6518;
//     3  «Предводитель» (глава): страницы 1…5 открываются, страница 0 — нотис 17107; «Права»: галочки по маскам 4123;
//        «Сохранить» → 6508 (маски званий 2 и 3); 6509 → нотис 17145;
//     4  «Ранги»: строки без главы; смена звания в списке → вопрос 17168 → «Да» → 6521; «Исключить» → 17166 → «Да» → 4114;
//        6522 код 2 → 17172; 6523 → звание в строке и нотис 17170; 4115 → строка исчезла;
//     5  «Объявления»: набор + «Написать» → 6513;
//     6  «Передача полномочий»: строки заместителей; выбор строки + кнопка → 17204 → 17205 → 6526; 6528 код 1 → 17210, подрежим 0;
//        6529 у адресата → вопрос 17206 → «Да» → 6530 (1);
//     7  «Герб»: ползунки и сетки, «Сохранить» → 17231/17232 → проверка предмета → 17233 → 6524 (53 б); 6525/6527;
//     8  «Выйти из клана» главой → 12432 → «Да» → нотис 12433, 4116 не уходит.
//   Итог: «КЛАН-ВКЛАДКА ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Login/AsdaLoginData.h"
#include "Pet/AsdaPetSubsystem.h"
#include "UI/AsdaCommunitySubsystem.h"
#include "UI/AsdaCommunityClanSubsystem.h"
#include "UI/AsdaCommunityFriendSubsystem.h"   // AsdaCommunityUi::Find / FindLabel
#include "UI/AsdaNotice.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaJuiComboBox.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "World/AsdaWorldSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/EditableText.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "TimerManager.h"
#include "UObject/UObjectGlobals.h"
#include "UnrealClient.h"   // FScreenshotRequest — кадры `shots=<папка>`

DEFINE_LOG_CATEGORY_STATIC(LogAsdaClanTabTest, Log, All);

extern int32 AsdaEntryClanIndex(const UGameInstance* GI);   // индекс клана токенов входа (AsdaNetSubsystem_Session.cpp)

namespace AsdaClanTabTest
{
	static const TCHAR* kCmd    = TEXT("asda.Clan.TabSelfTest");   // имя команды
	static const TCHAR* kTwoBut = TEXT("TWOBUT_MSGBOX");           // окно-вопрос 1520
	static const TCHAR* kOneBut = TEXT("ONEBUT_MSGBOX");           // окно-нотис 1500
	static constexpr int32 kMe     = 1001;   // свой charId (`char+0x41C`) на прогон
	static constexpr int32 kMyUser = 1001;   // свой userIndex записи клана = свой charId (эталон сверяет 6523/4133 с `g_LocalPlayer+1052`)
	static constexpr int32 kClan   = 77;     // индекс клана

	// Член клана для записи 131 б (4125/4133).
	struct FMem { int32 User; uint8 CharIdx; const char* Name; uint8 Grade; uint8 Lv; uint8 Online; int32 Points; };

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                  // счёт
		TWeakObjectPtr<UWorld> W;                // мир прогона
		bool bQuit = false;                      // выйти после итога
		bool bCap = false;                       // перехват исходящих взведён
		TUniquePtr<FAsdaNetSelfScope> NetScope;  // свои charId/userNum — вернуть в конце
		TArray<TPair<int32, TArray<uint8>>> Sent;   // перехваченные исходящие (опкод, тело)
		FString ShotDir;                         // `shots=<папка>` — куда класть кадры
		FString MyName;                          // имя своего персонажа (из мира)
		TArray<FMem> Mems;                       // состав клана прогона
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaClanTabTest, Log, TEXT("КЛАН-ВКЛАДКА ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaClanTabTest, Error, TEXT("КЛАН-ВКЛАДКА FAIL %s"), *What); }
		}
		// Сколько перехвачено пакетов опкода; последнее тело — в Out.
		int32 SentCount(int32 Op, TArray<uint8>* Out = nullptr) const
		{
			int32 N = 0;
			for (const TPair<int32, TArray<uint8>>& P : Sent) { if (P.Key == Op) { ++N; if (Out) { *Out = P.Value; } } }
			return N;
		}
	};
	using FCtxRef = TSharedRef<FCtx>;

	using AsdaSelfTestCtx::GIOf;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	using AsdaSelfTestCtx::Sub;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	using AsdaSelfTestCtx::Feed;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия отличалась только тем, что подсистема сети бралась через `Sub<UAsdaNetSubsystem>` — то же, что `AsdaTestNet::Get`.
	using AsdaSelfTestCtx::ByFn::After;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия отличалась только именами параметров.
	// Полная сборка мусора: реле кнопок, держащиеся только слабо, после неё умирают — кнопка молчит.
	static void Gc() { CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true); }
	// Кадр экрана с интерфейсом (снимается в конце кадра — следующий шаг ставить таймером).
	static void Shot(const FCtxRef& C, const TCHAR* Name)
	{
		if (C->ShotDir.IsEmpty()) { return; }
		const FString Path = FPaths::Combine(C->ShotDir, FString(Name) + TEXT(".png"));
		UE_LOG(LogAsdaClanTabTest, Log, TEXT("КЛАН-ВКЛАДКА кадр → %s"), *Path);
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/true, /*bAddFilenameSuffix*/false);
	}
	static FString Loc(const FCtxRef& C, int32 Id) { return AsdaNotice::Loc(GIOf(C), Id); }
	static UUserWidget* Wnd(const FCtxRef& C) { UAsdaCommunitySubsystem* Cm = Sub<UAsdaCommunitySubsystem>(C); return Cm ? Cm->GetWindowWidget() : nullptr; }
	static FString Txt(const FCtxRef& C, const FString& Idc)
	{
		const UTextBlock* T = Wnd(C) ? AsdaCommunityUi::FindLabel(Wnd(C), Idc) : nullptr;
		return T ? T->GetText().ToString() : FString(TEXT("<нет узла>"));
	}
	static bool Shown(const FCtxRef& C, const FString& Idc)
	{
		UWidget* N = Wnd(C) ? AsdaCommunityUi::Find(Wnd(C), Idc) : nullptr;
		FVector2D Mn, Mx;
		return N && AsdaUi::IsShown(N) && AsdaUi::WidgetAbsRect(N, Mn, Mx) && (Mx - Mn).X > 1.0 && (Mx - Mn).Y > 1.0;
	}
	// Настоящий щелчок мыши по центру узла (события Slate через менеджер окон).
	static bool ClickW(const FCtxRef& C, UWidget* N, const FString& Tag)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		FVector2D Mn, Mx;
		const bool bGeo = WM && N && AsdaUi::IsShown(N) && AsdaUi::WidgetAbsRect(N, Mn, Mx) && (Mx - Mn).X > 1.0 && (Mx - Mn).Y > 1.0;
		C->Expect(bGeo, FString::Printf(TEXT("%s: узел на экране (щелчок)"), *Tag));
		if (bGeo) { WM->SimulateClickAt((Mn + Mx) * 0.5f, 2); }
		return bGeo;
	}
	static bool Click(const FCtxRef& C, const FString& Idc) { return ClickW(C, Wnd(C) ? AsdaCommunityUi::Find(Wnd(C), Idc) : nullptr, Idc); }
	static UWidget* MsgNode(const FCtxRef& C, const TCHAR* Win, const TCHAR* Name)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		UUserWidget* Wd = WM ? WM->FindWindow(FName(Win)) : nullptr;
		return Wd ? AsdaUi::FindNodeByName(Wd, Name, nullptr) : nullptr;
	}
	// Текст верхнего окна-сообщения (пусто — окна нет на экране).
	static FString MsgText(const FCtxRef& C, bool bTwoBut)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		UUserWidget* Wd = WM ? WM->FindWindow(FName(bTwoBut ? kTwoBut : kOneBut)) : nullptr;
		if (!Wd || !AsdaUi::IsShown(Wd)) { return FString(); }
		return UAsdaWindowSubsystem::ReadMsgBoxShownText(AsdaUi::FindNodeByName(Wd, bTwoBut ? TEXT("TWOBUT_TEXT") : TEXT("ONEBUT_TEXT"), nullptr));
	}
	static void Answer(const FCtxRef& C, bool bYes, const FString& Tag)
	{
		ClickW(C, MsgNode(C, kTwoBut, bYes ? TEXT("TWOBUT_BUT00") : TEXT("TWOBUT_BUT01")), Tag + (bYes ? TEXT(" «Да»") : TEXT(" «Нет»")));
	}
	static void CloseOneBut(const FCtxRef& C)
	{
		if (!MsgText(C, false).IsEmpty()) { ClickW(C, MsgNode(C, kOneBut, TEXT("ONEBUT_BUT00")), TEXT("нотис «ОК»")); }
	}
	// Текст loc без тегов цвета `<FC=…>…</F>` — для сверки с показанным окном.
	static bool MsgHas(const FCtxRef& C, bool bTwoBut, int32 LocId, const TCHAR* Part = nullptr)
	{
		const FString S = MsgText(C, bTwoBut);
		if (S.IsEmpty()) { return false; }
		if (Part) { return S.Contains(Part); }
		FString L = Loc(C, LocId);
		int32 Cut = INDEX_NONE;
		if (L.FindChar(TEXT('%'), Cut) || L.FindChar(TEXT('['), Cut)) { L = L.Left(Cut); }
		L.TrimStartAndEndInline();
		return L.IsEmpty() || S.Contains(L.Left(FMath::Min(8, L.Len())));
	}
	static void TypeChars(const FString& S)
	{
		if (!FSlateApplication::IsInitialized()) { return; }
		for (int32 i = 0; i < S.Len(); ++i) { FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(S[i], FModifierKeysState(), 0, false)); }
	}

	// ── Тела пакетов сервера ──
	static void PutStr(TArray<uint8>& B, int32 At, const char* S, int32 Max)
	{
		for (int32 i = 0; S[i] && i < Max - 1; ++i) { B[At + i] = (uint8)S[i]; }
	}
	static TArray<uint8> Rec131(const FMem& M)
	{
		TArray<uint8> B; B.SetNumZeroed(131);
		AsdaBytes::WrI32(B.GetData() + 0, M.User);
		B[4] = M.CharIdx;
		PutStr(B, 5, M.Name, 20);
		B[25] = 1; B[26] = M.Lv; B[27] = 1; B[28] = 0;
		B[33] = M.Grade;
		AsdaBytes::WrI32(B.GetData() + 34, M.Points);
		B[111] = 0; B[112] = 3; B[113] = M.Online;
		return B;
	}
	static TArray<uint8> Body4123(const FCtxRef& C)
	{
		TArray<uint8> B; B.SetNumZeroed(191);
		AsdaBytes::WrI16(B.GetData() + 0, kClan);
		PutStr(B, 2, "TestClan", 18);
		const FTCHARToUTF8 Nm(*C->MyName);
		PutStr(B, 20, Nm.Get(), 20);
		B[45] = 50; B[47] = 2;
		AsdaBytes::WrI32(B.GetData() + 49, 1234);   // осталось ОД
		AsdaBytes::WrI32(B.GetData() + 53, 5678);   // всего ОД
		AsdaBytes::WrI16(B.GetData() + 57, 0x0003); // звание 2: права 1,2
		AsdaBytes::WrI16(B.GetData() + 59, 0x0004); // звание 3: право 3
		return B;
	}
	static TArray<uint8> Body4135Self()
	{
		TArray<uint8> B; B.SetNumZeroed(142);
		AsdaBytes::WrI32(B.GetData() + 0, kMe);
		AsdaBytes::WrI16(B.GetData() + 4, kClan);
		PutStr(B, 6, "TestClan", 17);
		B[23] = 1;   // звание 1 = глава (`char+0x97B`)
		return B;
	}

	static void Finish(const FCtxRef& C)
	{
		UWorld* W = C->W.Get();
		if (C->bCap) { if (UAsdaPetSubsystem* Pet = Sub<UAsdaPetSubsystem>(C)) { Pet->EndSendCapture(); } }
		C->NetScope.Reset();
		// Вернуть окно 6400 к вкладке 0: выбранная вкладка живёт и при закрытом окне (флаг панели эталона), а соседние самотесты
		//   (гейт «Друг» — хвост 4109) рассчитывают, что вкладка «Клан» НЕ выбрана.
		if (UAsdaCommunitySubsystem* Cm = Sub<UAsdaCommunitySubsystem>(C)) { Cm->OpenOnTab(0); Cm->CloseWindow(); }
		AsdaSelfTestKit::Finish(W, kCmd, C->bQuit, LogAsdaClanTabTest, TEXT("КЛАН-ВКЛАДКА"), C->Ok, C->Fail);
	}

	// ── Последовательность шагов: (пауза перед шагом, шаг). Сборка мусора — отдельным шагом перед щелчками. ──
	struct FStep { float Delay; TFunction<void(FCtxRef)> Fn; };
	static TArray<FStep>& Steps() { static TArray<FStep> S; return S; }
	static void RunStep(FCtxRef C, int32 I)
	{
		if (!Steps().IsValidIndex(I)) { Finish(C); return; }
		After(C, Steps()[I].Delay, [I](FCtxRef C) { Steps()[I].Fn(C); RunStep(C, I + 1); });
	}
	static void Add(float Delay, TFunction<void(FCtxRef)> Fn) { Steps().Add({ Delay, MoveTemp(Fn) }); }
	static int32 Was(const FCtxRef& C, int32 Op) { return C->SentCount(Op); }
	static int32 Mark[16] = {};   // счётчики «до шага» по слотам
	static UWidget* Named(const FCtxRef& C, const FString& N) { return Wnd(C) ? Wnd(C)->GetWidgetFromName(FName(*N)) : nullptr; }
	// План `<Idc>_<jui>_<plane>` окна (как ищет подсистема).
	static UWidget* Plane(const FCtxRef& C, const FString& Idc, int32 P)
	{
		UUserWidget* H = Wnd(C);
		UWidget* Found = nullptr;
		if (H && H->WidgetTree)
		{
			H->WidgetTree->ForEachWidget([&](UWidget* W)
			{
				FString N; int32 J = 0, Pl = 0;
				if (!Found && W && AsdaUi::ParsePlaneName(W->GetName(), N, J, Pl) && N == Idc && Pl == P) { Found = W; }
			});
		}
		return Found;
	}
	static bool Checked(const FCtxRef& C, const FString& Idc)
	{
		const UCheckBox* B = Cast<UCheckBox>(Wnd(C) ? AsdaCommunityUi::Find(Wnd(C), Idc) : nullptr);
		return B && B->IsChecked();
	}
	static void HoverAt(UWidget* W)
	{
		FVector2D Mn, Mx;
		if (W && FSlateApplication::IsInitialized() && AsdaUi::WidgetAbsRect(W, Mn, Mx)) { FSlateApplication::Get().SetCursorPos((Mn + Mx) * 0.5f); }
	}
	static UAsdaCommunityClanSubsystem* Cl(const FCtxRef& C) { return Sub<UAsdaCommunityClanSubsystem>(C); }
	static int32 FindCertificateCode(const FCtxRef& C)
	{
		UAsdaItemTableSubsystem* T = Sub<UAsdaItemTableSubsystem>(C);
		if (!T) { return 0; }
		for (int32 Code = 1; Code <= 2000; ++Code)
		{
			FAsdaSpecialItemlistRow R;
			if (T->GetSpecialItem(Code, R) && R.SubType == 62) { return Code; }
		}
		for (int32 Code = 20000; Code <= 80000; ++Code)
		{
			const FAsdaItemRecord* R = T->GetItem(Code);
			if (R && R->LinkCode == 62) { return Code; }
		}
		return 0;
	}
	static TArray<uint8> Rec4133(uint8 Kind, const FMem& M)
	{
		TArray<uint8> B; B.SetNumZeroed(134);
		B[0] = Kind; AsdaBytes::WrI16(B.GetData() + 1, kClan);
		const TArray<uint8> R = Rec131(M);
		FMemory::Memcpy(B.GetData() + 3, R.GetData(), 131);
		return B;
	}
	static uint32 U16At(const TArray<uint8>& B, int32 O) { return (B.Num() >= O + 2) ? AsdaBytes::RdU16(B.GetData() + O) : 0xFFFFu; }
	static int32 I32At(const TArray<uint8>& B, int32 O) { return (B.Num() >= O + 4) ? AsdaBytes::RdI32(B.GetData() + O) : -999; }

	static void BuildSteps()
	{
		Steps().Reset();
		// ═══ 0: вход — своя клановая карточка ═══
		Add(0.3f, [](FCtxRef C) { Feed(C, 4135, Body4135Self()); });
		// ═══ 0а: вкладку «Клан» ещё НЕ открывали (ревизия, блокер): значок 13 от 6529 и исключение себя (4133) работают ═══
		Add(0.3f, [](FCtxRef C)
		{
			Mark[12] = Was(C, 4122);
			TArray<uint8> O; O.SetNumZeroed(2); AsdaBytes::WrI16(O.GetData(), kClan); Feed(C, 6529, O);
		});
		Add(0.4f, [](FCtxRef C)
		{
			C->Expect(Was(C, 4122) == Mark[12] + 1, TEXT("0а: 6529 при неизвестном блоке клана → 4122 (имя клана для 17207, отложенно)"));
			Feed(C, 4123, Body4123(C));
		});
		Add(0.3f, [](FCtxRef C)
		{
			UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(C);
			C->Expect(Hb && Hb->HandleNoticeIconClick(13), TEXT("0а: вкладку не открывали — значок 13 на панели, щелчок принят"));
		});
		Add(0.4f, [](FCtxRef C)
		{
			C->Expect(MsgHas(C, true, 17207, TEXT("TestClan")), FString::Printf(TEXT("0а: вопрос 17207 без открытой вкладки («%s»)"), *MsgText(C, true)));
			Mark[13] = Was(C, 6530);
			Answer(C, true, TEXT("0а: 17207"));
		});
		Add(0.4f, [](FCtxRef C)
		{
			TArray<uint8> B; const int32 N = C->SentCount(6530, &B);
			C->Expect(N == Mark[13] + 1 && B.Num() == 3 && B[0] == 1 && (int32)U16At(B, 1) == kClan, FString::Printf(TEXT("0а: «Да» → 6530 [1, клан] (%d б)"), B.Num()));
			Mark[14] = Was(C, 4138);
			Feed(C, 4133, Rec4133(2, C->Mems[0]));
		});
		Add(0.5f, [](FCtxRef C)
		{
			C->Expect(Was(C, 4138) == Mark[14] + 1, TEXT("0а: вкладку не открывали — 4133 вид 2 про меня → 4138 (тикер с подъёма подсистемы)"));
			C->Expect(MsgHas(C, false, 20052, TEXT("TestClan")), FString::Printf(TEXT("0а: нотис 20052 («%s»)"), *MsgText(C, false)));
			CloseOneBut(C);
		});
		Add(0.4f, [](FCtxRef C) { Feed(C, 4135, Body4135Self()); });   // снова в клане (как после повторного вступления) — дальше обычный путь
		Add(0.3f, [](FCtxRef C)
		{
			UAsdaNetSubsystem* N = Sub<UAsdaNetSubsystem>(C);
			const int32 G = N ? (int32)N->GetSelfClanGrade() : -1;
			C->Expect(G == 1, FString::Printf(TEXT("0: 4135 своей карточки → своё звание %d (ждём 1, charUpdateVisibleCharsFromPacket@0xBC4BB0)"), G));
			if (N && G != 1) { N->SelfClanRank = 1; }
			C->Expect(AsdaEntryClanIndex(GIOf(C)) == kClan, FString::Printf(TEXT("0: 4122/4124 входа несут свой клан (%d, ждём %d)"), AsdaEntryClanIndex(GIOf(C)), kClan));
			if (UAsdaCommunitySubsystem* Cm = Sub<UAsdaCommunitySubsystem>(C)) { Cm->OpenOnTab(2); }
		});
		// ═══ 1: состав и пагинация ═══
		Add(1.0f, [](FCtxRef C)
		{
			C->Expect(C->SentCount(4122) >= 1 && C->SentCount(4124) >= 1, FString::Printf(TEXT("1: вкладка 2 → 4122=%d, 4124=%d"), C->SentCount(4122), C->SentCount(4124)));
			Feed(C, 4123, Body4123(C));
			TArray<uint8> L; for (const FMem& M : C->Mems) { L.Append(Rec131(M)); }
			Feed(C, 4125, L); Feed(C, 4126, TArray<uint8>());
		});
		Add(0.5f, [](FCtxRef C)
		{
			C->Expect(Txt(C, TEXT("CMTY_CLAN_NAME_V")) == TEXT("TestClan"), FString::Printf(TEXT("1: имя клана 14003 = «%s»"), *Txt(C, TEXT("CMTY_CLAN_NAME_V"))));
			C->Expect(Txt(C, TEXT("CMTY_CLAN_MEMNAME2")) == TEXT("ViceOne"), FString::Printf(TEXT("1: строка 2 = «%s»"), *Txt(C, TEXT("CMTY_CLAN_MEMNAME2"))));
			C->Expect(Txt(C, TEXT("CMTY_CLAN_PAGE2")) == TEXT("2"), FString::Printf(TEXT("1: страниц 2 («%s»)"), *Txt(C, TEXT("CMTY_CLAN_PAGE2"))));
			Shot(C, TEXT("1_list"));
			Gc();
		});
		Add(0.8f, [](FCtxRef C) { Click(C, TEXT("CMTY_CLAN_NEXTBTN1")); });
		Add(0.4f, [](FCtxRef C)
		{
			C->Expect(Txt(C, TEXT("CMTY_CLAN_PAGE1")) == TEXT("2") && Txt(C, TEXT("CMTY_CLAN_MEMNAME1")) == TEXT("Seventh"),
				FString::Printf(TEXT("1: «вперёд» → стр. %s, строка 1 «%s»"), *Txt(C, TEXT("CMTY_CLAN_PAGE1")), *Txt(C, TEXT("CMTY_CLAN_MEMNAME1"))));
			Click(C, TEXT("CMTY_CLAN_PREBTN1"));
		});
		Add(0.4f, [](FCtxRef C) { C->Expect(Txt(C, TEXT("CMTY_CLAN_PAGE1")) == TEXT("1"), TEXT("1: «назад» → страница 1")); });
		// подсказка-заметка над строкой состава (событие 8 → `uiSlotTooltipFromList`), заметку приносит рассылка 6520
		Add(0.2f, [](FCtxRef C)
		{
			TArray<uint8> M; M.SetNumZeroed(71); AsdaBytes::WrI32(M.GetData(), 5001); M[4] = 0; PutStr(M, 7, "vice memo", 64);
			Feed(C, 6520, M);
			HoverAt(AsdaCommunityUi::Find(Wnd(C), TEXT("CMTY_CLAN_LIST2")));
		});
		Add(0.4f, [](FCtxRef C)
		{
			C->Expect(Shown(C, TEXT("CmtyClan_IntroTip")) && Txt(C, TEXT("CmtyClan_IntroTip")) == TEXT("vice memo"),
				FString::Printf(TEXT("1: 6520 + наведение на строку → подсказка 14900 «%s»"), *Txt(C, TEXT("CmtyClan_IntroTip"))));
			Shot(C, TEXT("1_memo_tip"));
			HoverAt(Named(C, TEXT("CMTY_CLAN_NAME_V")));
		});
		// 4133: вид 3 (вступил), вид 2 (другой исключён)
		Add(0.3f, [](FCtxRef C)
		{
			C->Expect(!Shown(C, TEXT("CmtyClan_IntroTip")), TEXT("1: курсор ушёл — подсказка скрыта"));
			Feed(C, 4133, Rec4133(3, { 5100, 0, "Joiner", 5, 10, 1, 0 }));
		});
		Add(0.3f, [](FCtxRef C)
		{
			C->Expect(Cl(C)->GetMemberCount() == 9, FString::Printf(TEXT("1: 4133 вид 3 → в составе 9 (%d)"), Cl(C)->GetMemberCount()));
			Feed(C, 4133, Rec4133(2, { 5100, 0, "Joiner", 5, 10, 1, 0 }));
		});
		Add(0.3f, [](FCtxRef C) { C->Expect(Cl(C)->GetMemberCount() == 8, FString::Printf(TEXT("1: 4133 вид 2 (другой) → запись долой (%d)"), Cl(C)->GetMemberCount())); });
		// ═══ 2: «Информация» ═══
		Add(0.2f, [](FCtxRef C) { Gc(); });
		Add(0.6f, [](FCtxRef C) { Mark[0] = Was(C, 6511); Click(C, TEXT("CMTY_CLAN_3BTN_OPT1")); });
		Add(0.4f, [](FCtxRef C)
		{
			C->Expect(Was(C, 6511) == Mark[0] + 1, TEXT("2: «Информация» → 6511"));
			C->Expect(Shown(C, TEXT("CMTY_CLAN_NAME_V")) && Shown(C, TEXT("CmtyClan_RemainPoints")) && Shown(C, TEXT("CMTY_CLAN_INFO_WIN")),
				TEXT("2: на «Информации» инфо-блок клана с рамкой 14001 виден (эталон гасит только список 14100)"));
			TArray<uint8> N; N.SetNumZeroed(546); N[0] = 1; N[1] = 2; AsdaBytes::WrI32(N.GetData() + 2, 31);
			PutStr(N, 6, "Writer", 20); PutStr(N, 26, "2026-11-01", 20); PutStr(N, 46, "Hello clan", 500);
			Feed(C, 6512, N);
		});
		Add(0.4f, [](FCtxRef C)
		{
			C->Expect(Txt(C, TEXT("CMTY_CLAN_NOTICE_TEXT")).Contains(TEXT("Hello clan")), FString::Printf(TEXT("2: 6512 → раскрытое объявление «%s»"), *Txt(C, TEXT("CMTY_CLAN_NOTICE_TEXT"))));
			Shot(C, TEXT("2_info"));
			// второе, новее: 31 уходит в строку, 32 — раскрыто
			TArray<uint8> N; N.SetNumZeroed(546); N[0] = 1; N[1] = 2; AsdaBytes::WrI32(N.GetData() + 2, 32);
			PutStr(N, 6, "Writer2", 20); PutStr(N, 26, "2026-11-02", 20); PutStr(N, 46, "Second", 500);
			Feed(C, 6512, N);
		});
		Add(0.3f, [](FCtxRef C)
		{
			C->Expect(Txt(C, TEXT("CMTY_CLAN_NOTICE_TEXT")).Contains(TEXT("Second")) && Txt(C, TEXT("CmtyClan_Notice01_T1")).Contains(TEXT("Hello")),
				FString::Printf(TEXT("2: раскрыто «%s», в строке «%s»"), *Txt(C, TEXT("CMTY_CLAN_NOTICE_TEXT")), *Txt(C, TEXT("CmtyClan_Notice01_T1"))));
			Gc();
		});
		// удаление строки (кнопка 14091) щелчком
		Add(0.5f, [](FCtxRef C) { Click(C, TEXT("CmtyClan_NoticeDel01")); });
		Add(0.4f, [](FCtxRef C)
		{
			C->Expect(MsgHas(C, true, 17182), FString::Printf(TEXT("2: «Очистить» строки → вопрос 17182 («%s»)"), *MsgText(C, true)));
			Mark[15] = Was(C, 6515);
			Answer(C, true, TEXT("2: 17182 строка"));
		});
		Add(0.4f, [](FCtxRef C)
		{
			TArray<uint8> B; const int32 N = C->SentCount(6515, &B);
			C->Expect(N == Mark[15] + 1 && B.Num() == 10 && I32At(B, 0) == kMe && (int32)U16At(B, 4) == kClan && I32At(B, 6) == 31,
				FString::Printf(TEXT("2: «Да» → 6515 (%d б): клан %u, ключ %d"), B.Num(), U16At(B, 4), I32At(B, 6)));
			TArray<uint8> R; R.Add(1); Feed(C, 6516, R);
			TArray<uint8> G; G.SetNumZeroed(4); AsdaBytes::WrI32(G.GetData(), 31); Feed(C, 6517, G);
		});
		Add(0.3f, [](FCtxRef C)
		{
			C->Expect(Txt(C, TEXT("CmtyClan_Notice01_T1")).IsEmpty() && Txt(C, TEXT("CMTY_CLAN_NOTICE_TEXT")).Contains(TEXT("Second")),
				TEXT("2: 6517 → строка исчезла, раскрытое на месте"));
			// удаление раскрытого (кнопка 14090) щелчком
			Click(C, TEXT("CmtyClan_NoticeDel00"));
		});
		Add(0.4f, [](FCtxRef C) { Mark[15] = Was(C, 6515); Answer(C, true, TEXT("2: 17182 раскрытое")); });
		Add(0.4f, [](FCtxRef C)
		{
			TArray<uint8> B; const int32 N = C->SentCount(6515, &B);
			C->Expect(N == Mark[15] + 1 && I32At(B, 6) == 32, FString::Printf(TEXT("2: «Очистить» раскрытого → 6515, ключ %d"), I32At(B, 6)));
			TArray<uint8> G; G.SetNumZeroed(4); AsdaBytes::WrI32(G.GetData(), 32); Feed(C, 6517, G);
		});
		Add(0.3f, [](FCtxRef C)
		{
			C->Expect(Txt(C, TEXT("CMTY_CLAN_NOTICE_TEXT")).IsEmpty(), FString::Printf(TEXT("2: 6517 (объявление удалено) → панель пуста («%s»)"), *Txt(C, TEXT("CMTY_CLAN_NOTICE_TEXT"))));
			// прочитанное объявление переживает пересоздание окна: новое окно рисует его без 6512
			TArray<uint8> N; N.SetNumZeroed(546); N[0] = 1; N[1] = 2; AsdaBytes::WrI32(N.GetData() + 2, 40);
			PutStr(N, 6, "Writer3", 20); PutStr(N, 26, "2026-11-03", 20); PutStr(N, 46, "Survives", 500);
			Feed(C, 6512, N);
			if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C)) { WM->CloseWindow(FName(TEXT("COMMUNITY_WIN"))); }
		});
		Add(0.5f, [](FCtxRef C) { if (UAsdaCommunitySubsystem* Cm = Sub<UAsdaCommunitySubsystem>(C)) { Cm->OpenOnTab(2); } });
		Add(1.0f, [](FCtxRef C)
		{
			Feed(C, 4123, Body4123(C));
			TArray<uint8> L; for (const FMem& M : C->Mems) { L.Append(Rec131(M)); }
			Feed(C, 4125, L); Feed(C, 4126, TArray<uint8>());
			Click(C, TEXT("CMTY_CLAN_3BTN_OPT1"));
		});
		Add(0.5f, [](FCtxRef C)
		{
			C->Expect(Txt(C, TEXT("CMTY_CLAN_NOTICE_TEXT")).Contains(TEXT("Survives")), FString::Printf(TEXT("2: новое окно рисует прочитанное объявление («%s»)"), *Txt(C, TEXT("CMTY_CLAN_NOTICE_TEXT"))));
			TArray<uint8> G; G.SetNumZeroed(4); AsdaBytes::WrI32(G.GetData(), 40); Feed(C, 6517, G);
		});
		Add(0.3f, [](FCtxRef C) { Click(C, TEXT("Clan_MyIntroTxt")); });
		Add(0.3f, [](FCtxRef C) { TypeChars(TEXT("memo1")); Mark[1] = Was(C, 6518); });
		Add(0.2f, [](FCtxRef C) { Click(C, TEXT("Clan_MyIntroSave")); });
		Add(0.4f, [](FCtxRef C)
		{
			TArray<uint8> B; const int32 N = C->SentCount(6518, &B);
			C->Expect(N == Mark[1] + 1 && B.Num() == 70 && FMemory::Memcmp(B.GetData() + 6, "memo1", 5) == 0, FString::Printf(TEXT("2: заметка → 6518 (%d б)"), B.Num()));
		});
		// ═══ 3: «Предводитель» ═══
		Add(0.2f, [](FCtxRef C) { Gc(); });
		Add(0.6f, [](FCtxRef C) { Click(C, TEXT("CMTY_CLAN_3BTN_OPT2")); });
		Add(0.5f, [](FCtxRef C)
		{
			C->Expect(MsgText(C, false).IsEmpty() && Shown(C, TEXT("CmtyClan_Sub2_ChangeBtn")),
				FString::Printf(TEXT("3: вход в «Предводителя» → сразу страница «Права», без нотиса 17107 (нотис «%s»)"), *MsgText(C, false)));
			C->Expect(!Shown(C, TEXT("CmtyClan_SubMenuBtn_OPT0")) && !Shown(C, TEXT("CmtyClan_SubMenuTxt00")),
				TEXT("3: пункта «Кандидаты» нет (решение владельца)"));
			{
				FVector2D A0 = FVector2D::ZeroVector, A1 = FVector2D::ZeroVector, B0 = FVector2D::ZeroVector, B1 = FVector2D::ZeroVector;   // ★ R5-fix-c (13.11.2026): начальные значения — C4701 (при отказе геометрии числа уходили в строку проверки неинициализированными)
				UWidget* O1 = AsdaCommunityUi::Find(Wnd(C), TEXT("CmtyClan_SubMenuBtn_OPT1"));
				UWidget* G = Named(C, TEXT("CmtyClan_SubMenuBtn_grp"));
				const bool bTop = O1 && G && AsdaUi::WidgetAbsRect(O1, A0, A1) && AsdaUi::WidgetAbsRect(G, B0, B1) && FMath::Abs(A0.Y - B0.Y) < 1.5;
				C->Expect(bTop, FString::Printf(TEXT("3: «Права» поднят на место первого пункта (верх %.1f, верх полосы %.1f)"), A0.Y, B0.Y));
			}
			C->Expect(Shown(C, TEXT("CMTY_CLAN_NAME_V")) && Shown(C, TEXT("CMTY_CLAN_CHIEF")) && !Shown(C, TEXT("CMTY_CLAN_MEMNAME1")),
				TEXT("3: на «Предводителе» инфо-блок виден, список членов скрыт"));
			CloseOneBut(C);
		});
		// стр. 1 «Права»
		Add(0.4f, [](FCtxRef C) { Click(C, TEXT("CmtyClan_SubMenuBtn_OPT1")); });
		Add(0.4f, [](FCtxRef C)
		{
			C->Expect(Checked(C, TEXT("CmtyClan_PosFunc_Btn1_1")) && Checked(C, TEXT("CmtyClan_PosFunc_Btn1_2")) && !Checked(C, TEXT("CmtyClan_PosFunc_Btn1_3"))
				&& !Checked(C, TEXT("CmtyClan_PosFunc_Btn2_1")) && !Checked(C, TEXT("CmtyClan_PosFunc_Btn2_2")) && Checked(C, TEXT("CmtyClan_PosFunc_Btn2_3")),
				TEXT("3.1: галочки по маскам 4123 (звание 2 = 3, звание 3 = 4)"));
			Shot(C, TEXT("3_1_rights"));
			Click(C, TEXT("CmtyClan_PosFunc_Btn2_1"));
		});
		Add(0.3f, [](FCtxRef C) { C->Expect(Checked(C, TEXT("CmtyClan_PosFunc_Btn2_1")), TEXT("3.1: щелчок по галочке — взведена")); Click(C, TEXT("CmtyClan_Sub2_CancelBtn")); });
		Add(0.3f, [](FCtxRef C) { C->Expect(!Checked(C, TEXT("CmtyClan_PosFunc_Btn2_1")), TEXT("3.1: «Отмена» 35591 → галочка из блока (снята)")); Click(C, TEXT("CmtyClan_PosFunc_Btn1_3")); });
		Add(0.3f, [](FCtxRef C) { Mark[2] = Was(C, 6508); Click(C, TEXT("CmtyClan_Sub2_ChangeBtn")); });
		Add(0.4f, [](FCtxRef C)
		{
			TArray<uint8> B; const int32 N = C->SentCount(6508, &B);
			C->Expect(N == Mark[2] + 1 && B.Num() == 10 && (int32)U16At(B, 0) == kClan && U16At(B, 2) == 7 && U16At(B, 4) == 4 && U16At(B, 6) == 0 && U16At(B, 8) == 0,
				FString::Printf(TEXT("3.1: «Сохранить» → 6508 (%d б): клан %u, маски %u/%u/%u/%u"), B.Num(), U16At(B, 0), U16At(B, 2), U16At(B, 4), U16At(B, 6), U16At(B, 8)));
			TArray<uint8> A; A.SetNumZeroed(3); A[0] = 1; AsdaBytes::WrI16(A.GetData() + 1, 6508); Feed(C, 6509, A);
		});
		Add(0.4f, [](FCtxRef C)
		{
			C->Expect(MsgHas(C, false, 17145), FString::Printf(TEXT("3.1: 6509 → нотис 17145 («%s»)"), *MsgText(C, false)));
			CloseOneBut(C);
			TArray<uint8> B = Body4123(C); AsdaBytes::WrI16(B.GetData() + 57, 7); B.SetNum(193); AsdaBytes::WrI16(B.GetData() + 191, 6508);
			Feed(C, 6527, B);
		});
		Add(0.3f, [](FCtxRef C) { Click(C, TEXT("CmtyClan_SubMenuBtn_OPT2")); });
		Add(0.4f, [](FCtxRef C) { Click(C, TEXT("CmtyClan_SubMenuBtn_OPT1")); });
		Add(0.4f, [](FCtxRef C)
		{
			C->Expect(Checked(C, TEXT("CmtyClan_PosFunc_Btn1_3")), TEXT("3.1: права сохранены после смены страницы (6527)"));
			Click(C, TEXT("CMTY_CLAN_3BTN_OPT0"));
		});
		Add(0.4f, [](FCtxRef C) { Click(C, TEXT("CMTY_CLAN_3BTN_OPT2")); });
		Add(0.4f, [](FCtxRef C) { C->Expect(Checked(C, TEXT("CmtyClan_PosFunc_Btn1_3")), TEXT("3.1: права сохранены после смены подрежима")); });
		// стр. 2 «Ранги»
		Add(0.3f, [](FCtxRef C) { Gc(); });
		Add(0.6f, [](FCtxRef C) { Click(C, TEXT("CmtyClan_SubMenuBtn_OPT2")); });
		Add(0.4f, [](FCtxRef C)
		{
			C->Expect(Txt(C, TEXT("CmtyClan_Sub3_Name0")) == TEXT("ViceOne") && Txt(C, TEXT("CmtyClan_Sub3_Name4")) == TEXT("Rookie"),
				FString::Printf(TEXT("3.2: строки без главы: 0 «%s», 4 «%s»"), *Txt(C, TEXT("CmtyClan_Sub3_Name0")), *Txt(C, TEXT("CmtyClan_Sub3_Name4"))));
			UAsdaJuiComboBox* Cb = Cl(C)->DebugGradeCombo(2);
			C->Expect(Cb && Cb->GetSelectedIndex() == 2, FString::Printf(TEXT("3.2: список звания строки 2 (Valued, звание 3) — пункт %d (ждём 2)"), Cb ? Cb->GetSelectedIndex() : -9));
			Shot(C, TEXT("3_2_grades"));
			HoverAt(Plane(C, TEXT("CmtyClan_Sub3_Mem1"), 0));
		});
		Add(0.4f, [](FCtxRef C)
		{
			C->Expect(Shown(C, TEXT("CmtyClan_IntroTip")), FString::Printf(TEXT("3.2: наведение на строку → подсказка 17169 «%s»"), *Txt(C, TEXT("CmtyClan_IntroTip"))));
			ClickW(C, Plane(C, TEXT("CmtyClan_Sub3_Combo2"), 0), TEXT("3.2: тело списка звания строки 2"));
		});
		Add(0.4f, [](FCtxRef C)
		{
			UAsdaJuiComboBox* Cb = Cl(C)->DebugGradeCombo(2);
			C->Expect(Cb && Cb->IsOpen(), TEXT("3.2: список звания раскрыт"));
			Shot(C, TEXT("3_2_combo_open"));
			Mark[3] = Was(C, 6521);
			ClickW(C, Named(C, TEXT("CmtyClan_Sub3_Combo2_Row3")), TEXT("3.2: пункт 3 «Заместитель»"));
		});
		Add(0.5f, [](FCtxRef C) { C->Expect(MsgHas(C, true, 17167), FString::Printf(TEXT("3.2: повышение → вопрос 17167 («%s»)"), *MsgText(C, true))); Answer(C, true, TEXT("3.2: 17167")); });
		Add(0.4f, [](FCtxRef C)
		{
			TArray<uint8> B; const int32 N = C->SentCount(6521, &B);
			C->Expect(N == Mark[3] + 1 && B.Num() == 31 && U16At(B, 4) == 2 && I32At(B, 6) == 5003 && FMemory::Memcmp(B.GetData() + 11, "Valued", 6) == 0,
				FString::Printf(TEXT("3.2: «Да» → 6521 (%d б), звание %u, цель %d"), B.Num(), U16At(B, 4), I32At(B, 6)));
			TArray<uint8> R; R.Add(2); Feed(C, 6522, R);
		});
		Add(0.4f, [](FCtxRef C)
		{
			C->Expect(MsgHas(C, false, 17172), FString::Printf(TEXT("3.2: 6522 код 2 → нотис 17172 («%s»)"), *MsgText(C, false)));
			CloseOneBut(C);
		});
		Add(0.4f, [](FCtxRef C)
		{
			TArray<uint8> G; G.SetNumZeroed(9); AsdaBytes::WrI32(G.GetData(), 5003); G[4] = 0; AsdaBytes::WrI16(G.GetData() + 5, kClan); AsdaBytes::WrI16(G.GetData() + 7, 2);
			Mark[4] = Was(C, 4124);
			Feed(C, 6523, G);
		});
		Add(0.5f, [](FCtxRef C)
		{
			C->Expect(MsgHas(C, false, 17170, TEXT("Valued")), FString::Printf(TEXT("3.2: 6523 → нотис 17170 («%s»)"), *MsgText(C, false)));
			C->Expect(Was(C, 4124) == Mark[4] + 1, TEXT("3.2: 6523 → 4124 (заново список, отложенно)"));
			CloseOneBut(C);
		});
		Add(0.4f, [](FCtxRef C) { Gc(); });
		Add(0.6f, [](FCtxRef C) { Click(C, TEXT("CmtyClan_Sub3_Expulsion4")); });
		Add(0.4f, [](FCtxRef C)
		{
			C->Expect(MsgHas(C, true, 17166, TEXT("Rookie")), FString::Printf(TEXT("3.2: «Изгнать» → вопрос 17166 («%s»)"), *MsgText(C, true)));
			Mark[5] = Was(C, 4114);
			Answer(C, true, TEXT("3.2: 17166"));
		});
		Add(0.4f, [](FCtxRef C)
		{
			TArray<uint8> B; const int32 N = C->SentCount(4114, &B);
			C->Expect(N == Mark[5] + 1 && B.Num() == 25 && I32At(B, 0) == 5005 && FMemory::Memcmp(B.GetData() + 5, "Rookie", 6) == 0,
				FString::Printf(TEXT("3.2: «Да» → 4114 (%d б), цель %d"), B.Num(), I32At(B, 0)));
			TArray<uint8> R; R.SetNumZeroed(26); R[0] = 1; AsdaBytes::WrI32(R.GetData() + 1, 5005); R[5] = 0; PutStr(R, 6, "Rookie", 20);
			Feed(C, 4115, R);
		});
		Add(0.4f, [](FCtxRef C)
		{
			C->Expect(MsgHas(C, false, 12423), FString::Printf(TEXT("3.2: 4115 код 1 → нотис 12423 («%s»)"), *MsgText(C, false)));
			C->Expect(Txt(C, TEXT("CmtyClan_Sub3_Name4")) != TEXT("Rookie"), FString::Printf(TEXT("3.2: строка исключённого исчезла (строка 4 «%s»)"), *Txt(C, TEXT("CmtyClan_Sub3_Name4"))));
			CloseOneBut(C);
		});
		// ★ 04.11: глава НЕ первым в списке (как на живой БД — `UPClanMemberinfo` без ORDER BY) → в «Рангах» все не-главы
		Add(0.3f, [](FCtxRef C)
		{
			TArray<uint8> L;
			const FMem Order[] = { { 6001, 0, "FirstOne", 5, 11, 1, 10 }, C->Mems[0], { 6002, 0, "SecondOne", 4, 12, 0, 20 }, { 6003, 1, "ThirdOne", 3, 13, 1, 30 } };
			for (const FMem& M : Order) { L.Append(Rec131(M)); }
			Feed(C, 4124 + 1, L); // 4125
		});
		Add(0.3f, [](FCtxRef C)
		{
			// В составе сейчас прежние записи + новые; проверяем, что «FirstOne» (запись не на месте главы) видна в «Рангах».
			bool bFound = false;
			for (int32 R = 0; R < 7; ++R) { if (Txt(C, FString::Printf(TEXT("CmtyClan_Sub3_Name%d"), R)) == TEXT("FirstOne")) { bFound = true; } }
			if (!bFound) { Click(C, TEXT("CMTY_CLAN_NEXTBTN1")); }
		});
		Add(0.3f, [](FCtxRef C)
		{
			bool bFound = false;
			for (int32 R = 0; R < 7; ++R) { if (Txt(C, FString::Printf(TEXT("CmtyClan_Sub3_Name%d"), R)) == TEXT("FirstOne")) { bFound = true; } }
			C->Expect(bFound, TEXT("3.2: член, стоящий в списке раньше главы, виден в «Рангах»"));
			Shot(C, TEXT("3_2_grades_order"));
		});
		// Чистый случай игрока: клан из 3 (рядовой первым, глава, ещё один) → в «Рангах» ровно 2 и оба не-главы.
		Add(0.3f, [](FCtxRef C)
		{
			Cl(C)->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
			TArray<uint8> L;
			const FMem Three[] = { { 7001, 0, "Alpha", 5, 20, 1, 1 }, C->Mems[0], { 7002, 0, "Gamma", 4, 21, 0, 2 } };
			for (const FMem& M : Three) { L.Append(Rec131(M)); }
			Feed(C, 4123, Body4123(C)); Feed(C, 4125, L); Feed(C, 4126, TArray<uint8>());
		});
		Add(0.4f, [](FCtxRef C) { Click(C, TEXT("CMTY_CLAN_3BTN_OPT2")); });   // сброс сессии вернул подрежим 0 — снова «Предводитель»
		Add(0.5f, [](FCtxRef C) { CloseOneBut(C); Click(C, TEXT("CmtyClan_SubMenuBtn_OPT2")); });
		Add(0.4f, [](FCtxRef C)
		{
			C->Expect(Txt(C, TEXT("CmtyClan_Sub3_Name0")) == TEXT("Alpha") && Txt(C, TEXT("CmtyClan_Sub3_Name1")) == TEXT("Gamma") && Txt(C, TEXT("CmtyClan_Sub3_Name2")).IsEmpty(),
				FString::Printf(TEXT("3.2: клан из 3 (глава вторым) → «Ранги»: «%s», «%s», «%s» (ждём Alpha, Gamma, пусто)"),
					*Txt(C, TEXT("CmtyClan_Sub3_Name0")), *Txt(C, TEXT("CmtyClan_Sub3_Name1")), *Txt(C, TEXT("CmtyClan_Sub3_Name2"))));
			C->Expect(Txt(C, TEXT("CMTY_CLAN_PAGE2")) == TEXT("1"), TEXT("3.2: клан из 3 — одна страница"));
			Shot(C, TEXT("3_2_grades_three"));
		});
		Add(0.4f, [](FCtxRef C)
		{
			// вернуть состав прогона в том виде, в каком его оставили шаги выше: Valued — заместитель (6523), Rookie — изгнан (4115)
			Cl(C)->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
			TArray<uint8> L;
			for (FMem M : C->Mems)
			{
				if (FCStringAnsi::Strcmp(M.Name, "Rookie") == 0) { continue; }
				if (FCStringAnsi::Strcmp(M.Name, "Valued") == 0) { M.Grade = 2; }
				L.Append(Rec131(M));
			}
			Feed(C, 4123, Body4123(C)); Feed(C, 4125, L); Feed(C, 4126, TArray<uint8>());
		});
		Add(0.4f, [](FCtxRef C) { Click(C, TEXT("CMTY_CLAN_3BTN_OPT2")); });   // сброс сессии вернул подрежим 0 — снова «Предводитель»
		Add(0.5f, [](FCtxRef C) { CloseOneBut(C); Click(C, TEXT("CmtyClan_SubMenuBtn_OPT2")); });
		// стр. 3 «Объявления»
		Add(0.4f, [](FCtxRef C) { Click(C, TEXT("CmtyClan_SubMenuBtn_OPT3")); });
		Add(0.4f, [](FCtxRef C) { Click(C, TEXT("CMTY_CLAN_NOITICE_WRITE")); });
		Add(0.3f, [](FCtxRef C) { TypeChars(TEXT("notice1")); Mark[6] = Was(C, 6513); });
		Add(0.2f, [](FCtxRef C) { Click(C, TEXT("CMTY_CLAN_NOITICE_OK")); });
		Add(0.4f, [](FCtxRef C)
		{
			TArray<uint8> B; const int32 N = C->SentCount(6513, &B);
			C->Expect(N == Mark[6] + 1 && B.Num() == 550 && FMemory::Memcmp(B.GetData() + 50, "notice1", 7) == 0, FString::Printf(TEXT("3.3: «Написать» → 6513 (%d б)"), B.Num()));
			Shot(C, TEXT("3_3_notice"));
		});
		// стр. 4 «Передача полномочий»
		Add(0.3f, [](FCtxRef C) { Gc(); });
		Add(0.6f, [](FCtxRef C) { Click(C, TEXT("CmtyClan_SubMenuBtn_OPT4")); });
		Add(0.4f, [](FCtxRef C)
		{
			C->Expect(Txt(C, TEXT("CmtyClan_Sub5_Name0")) == TEXT("ViceOne") && Txt(C, TEXT("CmtyClan_Sub5_Name2")) == TEXT("Valued"),
				FString::Printf(TEXT("3.4: заместители «%s», «%s», «%s»"), *Txt(C, TEXT("CmtyClan_Sub5_Name0")), *Txt(C, TEXT("CmtyClan_Sub5_Name1")), *Txt(C, TEXT("CmtyClan_Sub5_Name2"))));
			ClickW(C, Plane(C, TEXT("CmtyClan_Sub5_Mem1"), 0), TEXT("3.4: строка 1 (ViceTwo, не в игре)"));
		});
		Add(0.4f, [](FCtxRef C) { C->Expect(AsdaUi::IsShown(Plane(C, TEXT("CmtyClan_Sub5_Sel"), 0)), TEXT("3.4: рамка выбора 36020 видна")); Click(C, TEXT("CmtyClan_Sub5_ConBtn")); });
		Add(0.4f, [](FCtxRef C) { C->Expect(MsgHas(C, false, 17209, TEXT("ViceTwo")), FString::Printf(TEXT("3.4: не в игре → нотис 17209 («%s»)"), *MsgText(C, false))); CloseOneBut(C); });
		Add(0.4f, [](FCtxRef C) { ClickW(C, Plane(C, TEXT("CmtyClan_Sub5_Mem0"), 0), TEXT("3.4: строка 0 (ViceOne, в игре)")); });
		Add(0.4f, [](FCtxRef C)
		{
			FAnchors A1, A2; FVector2D R = FVector2D::ZeroVector, F = FVector2D::ZeroVector;   // ★ R5-fix-c (13.11.2026): начальные значения — C4701 (при отказе геометрии числа уходили в строку проверки неинициализированными)
			const bool bGeo = AsdaUi::CanvasRootPlacement(Plane(C, TEXT("CmtyClan_Sub5_Mem0"), 0), A1, R) && AsdaUi::CanvasRootPlacement(Plane(C, TEXT("CmtyClan_Sub5_Sel"), 0), A2, F);
			C->Expect(bGeo && FMath::IsNearlyEqual(F.X, R.X - 1.f, 0.5f) && FMath::IsNearlyEqual(F.Y, R.Y - 1.f, 0.5f),
				FString::Printf(TEXT("3.4: рамка 36020 на строке 0: рамка (%.0f,%.0f), строка (%.0f,%.0f)"), F.X, F.Y, R.X, R.Y));
			Shot(C, TEXT("3_4_master")); Click(C, TEXT("CmtyClan_Sub5_ConBtn"));
		});
		Add(0.4f, [](FCtxRef C) { C->Expect(MsgHas(C, true, 17204, TEXT("ViceOne")), FString::Printf(TEXT("3.4: вопрос 17204 («%s»)"), *MsgText(C, true))); Answer(C, true, TEXT("3.4: 17204")); });
		Add(0.5f, [](FCtxRef C) { C->Expect(MsgHas(C, true, 17205, TEXT("ViceOne")), FString::Printf(TEXT("3.4: вопрос 17205 («%s»)"), *MsgText(C, true))); Mark[7] = Was(C, 6526); Answer(C, true, TEXT("3.4: 17205")); });
		Add(0.4f, [](FCtxRef C)
		{
			TArray<uint8> B; const int32 N = C->SentCount(6526, &B);
			C->Expect(N == Mark[7] + 1 && B.Num() == 31 && I32At(B, 0) == kMe && (int32)U16At(B, 4) == kClan && I32At(B, 6) == 5001,
				FString::Printf(TEXT("3.4: «Да» → 6526 (%d б), цель %d"), B.Num(), I32At(B, 6)));
			C->Expect(MsgHas(C, false, 17208, TEXT("ViceOne")), FString::Printf(TEXT("3.4: нотис 17208 («%s»)"), *MsgText(C, false)));
			CloseOneBut(C);
		});
		Add(0.4f, [](FCtxRef C)
		{
			TArray<uint8> R; R.SetNumZeroed(143); R[0] = 5; Feed(C, 6528, R);
		});
		Add(0.4f, [](FCtxRef C) { C->Expect(MsgHas(C, false, 17211), FString::Printf(TEXT("3.4: 6528 код 5 → нотис 17211 («%s»)"), *MsgText(C, false))); CloseOneBut(C); });
		// значок 13 — предложение стать главой (как у адресата)
		Add(0.4f, [](FCtxRef C) { TArray<uint8> O; O.SetNumZeroed(2); AsdaBytes::WrI16(O.GetData(), kClan); Feed(C, 6529, O); });
		Add(0.4f, [](FCtxRef C)
		{
			UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(C);
			C->Expect(Hb && Hb->HandleNoticeIconClick(13), TEXT("3.4: 6529 → значок 13 на панели, щелчок принят"));
		});
		Add(0.4f, [](FCtxRef C) { C->Expect(MsgHas(C, true, 17207, TEXT("TestClan")), FString::Printf(TEXT("3.4: вопрос 17207 («%s»)"), *MsgText(C, true))); Mark[8] = Was(C, 6530); Answer(C, false, TEXT("3.4: 17207")); });
		Add(0.4f, [](FCtxRef C)
		{
			TArray<uint8> B; const int32 N = C->SentCount(6530, &B);
			C->Expect(N == Mark[8] + 1 && B.Num() == 3 && B[0] == 2 && (int32)U16At(B, 1) == kClan, FString::Printf(TEXT("3.4: «Нет» → 6530 [2, клан] (%d б)"), B.Num()));
		});
		// стр. 5 «Герб»
		Add(0.3f, [](FCtxRef C) { Gc(); });
		Add(0.6f, [](FCtxRef C) { Click(C, TEXT("CmtyClan_SubMenuBtn_OPT5")); });
		Add(0.4f, [](FCtxRef C)
		{
			C->Expect(AsdaUi::IsShown(Plane(C, TEXT("CmtyClan_InsgIcon04"), 0)) && AsdaUi::IsShown(Plane(C, TEXT("CmtyClan_BorderIcon04"), 0)),
				TEXT("3.5: сетки символов и рамок нарисованы"));
			C->Expect(Cl(C)->DebugEmblemState().StartsWith(TEXT("фон 255,255,255")), FString::Printf(TEXT("3.5: герба нет → сброс («%s»)"), *Cl(C)->DebugEmblemState()));
			Shot(C, TEXT("3_5_emblem"));
			ClickW(C, Plane(C, TEXT("CmtyClan_InsgIcon03"), 0), TEXT("3.5: символ, ячейка 3"));
		});
		Add(0.3f, [](FCtxRef C) { ClickW(C, Plane(C, TEXT("CmtyClan_BG_Cr"), 1), TEXT("3.5: «−» красного фона")); });
		Add(0.3f, [](FCtxRef C) { ClickW(C, Plane(C, TEXT("CmtyClan_BorderIcon02"), 0), TEXT("3.5: рамка, ячейка 2")); });
		Add(0.3f, [](FCtxRef C)
		{
			HoverAt(Plane(C, TEXT("CmtyClan_InsgIcon07"), 0));
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
			C->Expect(WM && WM->HandleMouseWheel(-1), TEXT("3.5: колесо над сеткой символов съедено окном"));
		});
		Add(0.3f, [](FCtxRef C)
		{
			const FString S = Cl(C)->DebugEmblemState();
			C->Expect(S.Contains(TEXT("фон 249,255,255")) && S.Contains(TEXT("символ ячейка 3 ряд 1")) && S.Contains(TEXT("рамка ячейка 2 ряд 0")),
				FString::Printf(TEXT("3.5: ползунок/ячейки/колесо → «%s»"), *S));
			Shot(C, TEXT("3_5_emblem_edit"));
			Click(C, TEXT("CmtyClan_Sub6_SaveBtn"));
		});
		Add(0.4f, [](FCtxRef C) { C->Expect(MsgHas(C, true, 17231), FString::Printf(TEXT("3.5: «Сохранить» → вопрос 17231 («%s»)"), *MsgText(C, true))); Answer(C, true, TEXT("3.5: 17231")); });
		Add(0.4f, [](FCtxRef C) { C->Expect(MsgHas(C, false, 17234), FString::Printf(TEXT("3.5: сертификата нет → нотис 17234 («%s»)"), *MsgText(C, false))); CloseOneBut(C); });
		Add(0.3f, [](FCtxRef C)
		{
			const int32 Code = FindCertificateCode(C);
			UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(C);
			const bool bPut = Code > 0 && Inv && Inv->DebugPutItem(1, 5, Code, 1);
			C->Expect(bPut, FString::Printf(TEXT("3.5: сертификат герба (код %d, атрибут 62) положен в сумку, ячейка 5"), Code));
			Click(C, TEXT("CmtyClan_Sub6_SaveBtn"));
		});
		Add(0.4f, [](FCtxRef C) { Answer(C, true, TEXT("3.5: 17231")); });
		Add(0.4f, [](FCtxRef C) { C->Expect(MsgHas(C, true, 17233), FString::Printf(TEXT("3.5: вопрос 17233 («%s»)"), *MsgText(C, true))); Mark[9] = Was(C, 6524); Answer(C, true, TEXT("3.5: 17233")); });
		Add(0.4f, [](FCtxRef C)
		{
			TArray<uint8> B; const int32 N = C->SentCount(6524, &B);
			C->Expect(N == Mark[9] + 1 && B.Num() == 53 && (int32)U16At(B, 0) == kClan && I32At(B, 2) == kMe && U16At(B, 10) == 5 && B[12] == 1
				&& I32At(B, 13 + 24) == 8 && I32At(B, 13) == 2 && (uint32)I32At(B, 13 + 16) == 0xFFF9FFFFu,
				FString::Printf(TEXT("3.5: «Да» → 6524 (%d б): символ %d, рамка %d, фон %08X, слот %u"), B.Num(), I32At(B, 37), I32At(B, 13), (uint32)I32At(B, 29), U16At(B, 10)));
			TArray<uint8> R; R.SetNumZeroed(62); R[0] = 1; AsdaBytes::WrI32(R.GetData() + 1, kMe);
			R[5 + 4] = 1; AsdaBytes::WrI16(R.GetData() + 5 + 5, 5);   // пустая запись: код 0, контейнер 1, слот 5
			Feed(C, 6525, R);
		});
		Add(0.5f, [](FCtxRef C)
		{
			C->Expect(MsgHas(C, false, 17235), FString::Printf(TEXT("3.5: 6525 → нотис 17235 («%s»)"), *MsgText(C, false)));
			UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(C);
			C->Expect(Inv && Inv->GetCellItemCode(1, 5) == 0, TEXT("3.5: 6525 → сертификат списан (ячейка 5 пуста)"));
			CloseOneBut(C);
		});
		Add(0.4f, [](FCtxRef C)
		{
			TArray<uint8> B = Body4123(C); B[65] = 1;
			AsdaBytes::WrI32(B.GetData() + 66 + 0, 7); AsdaBytes::WrI32(B.GetData() + 66 + 8, (int32)0xFF102030u);
			AsdaBytes::WrI32(B.GetData() + 66 + 16, (int32)0xFF405060u); AsdaBytes::WrI32(B.GetData() + 66 + 24, 20); AsdaBytes::WrI32(B.GetData() + 66 + 32, (int32)0xFF708090u);
			B.SetNum(193); AsdaBytes::WrI16(B.GetData() + 191, 6525);
			Feed(C, 6527, B);
		});
		Add(0.4f, [](FCtxRef C)
		{
			const FString S = Cl(C)->DebugEmblemState();
			C->Expect(S.Contains(TEXT("фон 64,80,96")) && S.Contains(TEXT("символ 112,128,144")) && S.Contains(TEXT("рамка 16,32,48"))
				&& S.Contains(TEXT("символ ячейка 5 ряд 3")) && S.Contains(TEXT("рамка ячейка 7 ряд 0")), FString::Printf(TEXT("3.5: 6527 с гербом → редактор из блока «%s»"), *S));
			Shot(C, TEXT("3_5_emblem_block"));
			Click(C, TEXT("CmtyClan_Sub6_SaveBtn"));
		});
		Add(0.4f, [](FCtxRef C) { C->Expect(MsgHas(C, true, 17232), FString::Printf(TEXT("3.5: герб есть → вопрос 17232 («%s»)"), *MsgText(C, true))); Answer(C, false, TEXT("3.5: 17232")); });
		// 6528 код 1 — смена главы прошла: подрежим «Состав», страница панели 3
		Add(0.4f, [](FCtxRef C) { TArray<uint8> R; R.SetNumZeroed(143); R[0] = 1; Feed(C, 6528, R); });
		Add(0.4f, [](FCtxRef C)
		{
			C->Expect(MsgHas(C, false, 17210), FString::Printf(TEXT("3.4: 6528 код 1 → нотис 17210 («%s»)"), *MsgText(C, false)));
			C->Expect(Shown(C, TEXT("CMTY_CLAN_MEMNAME1")), TEXT("3.4: 6528 код 1 → подрежим «Состав»"));
			CloseOneBut(C);
		});
		// ═══ 8: «Выйти из клана» главой ═══
		Add(0.3f, [](FCtxRef C) { Gc(); });
		Add(0.6f, [](FCtxRef C) { Click(C, TEXT("CMTY_CLAN_3BTN_OPT1")); });
		Add(0.5f, [](FCtxRef C) { Click(C, TEXT("CMTY_CLAN_SEC_BTN")); });
		Add(0.4f, [](FCtxRef C)
		{
			C->Expect(MsgHas(C, true, 12432), FString::Printf(TEXT("8: «Выйти из клана» → вопрос 12432 («%s»)"), *MsgText(C, true)));
			Shot(C, TEXT("8_leave_ask"));
			Mark[10] = Was(C, 4116);
			Answer(C, true, TEXT("8: 12432"));
		});
		Add(0.4f, [](FCtxRef C)
		{
			C->Expect(Was(C, 4116) == Mark[10], TEXT("8: глава — 4116 не уходит"));
			C->Expect(MsgHas(C, false, 12433), FString::Printf(TEXT("8: нотис 12433 («%s»)"), *MsgText(C, false)));
			CloseOneBut(C);
		});
		// ═══ 9: меня исключили (4133 вид 2 про меня) ═══
		Add(0.3f, [](FCtxRef C) { Mark[11] = Was(C, 4138); Feed(C, 4133, Rec4133(2, C->Mems[0])); });
		Add(0.5f, [](FCtxRef C)
		{
			C->Expect(MsgHas(C, false, 20052, TEXT("TestClan")), FString::Printf(TEXT("9: 4133 вид 2 про меня → нотис 20052 («%s»)"), *MsgText(C, false)));
			C->Expect(Was(C, 4138) == Mark[11] + 1, TEXT("9: → 4138 (отложенно)"));
			UAsdaCommunitySubsystem* Cm = Sub<UAsdaCommunitySubsystem>(C);
			C->Expect(Cm && !Cm->IsWindowOpen(), TEXT("9: окно 6400 закрыто (`communityUI_Close`)"));
			CloseOneBut(C);
		});
	}

	// ── вход ──
	static void S0(FCtxRef C)
	{
		BuildSteps();
		RunStep(C, 0);
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(A);
		// Свой персонаж нужен окну мира — офлайн его нет: штатный фейковый спавн и повтор через 2 с.
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			FString Extra;
			for (const FString& P : KA.Pos) { if (P.StartsWith(TEXT("shots="), ESearchCase::IgnoreCase)) { Extra += TEXT(" ") + P; } }
			const FString Again = FString::Printf(TEXT("%s selfready%s%s"), kCmd, *Extra, KA.bQuit ? TEXT(" quit") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaCommunitySubsystem* Cm = GI ? GI->GetSubsystem<UAsdaCommunitySubsystem>() : nullptr;
		UAsdaCommunityClanSubsystem* Cl = GI ? GI->GetSubsystem<UAsdaCommunityClanSubsystem>() : nullptr;
		UAsdaPetSubsystem* Pet = GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr;
		const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!Net || !Cm || !Cl || !Pet || !WS) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем")); return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W; C->bQuit = KA.bQuit;
		for (const FString& P : KA.Pos) { if (P.StartsWith(TEXT("shots="), ESearchCase::IgnoreCase)) { C->ShotDir = P.Mid(6); } }
		C->MyName = WS->GetSelfCharName();
		static FTCHARToUTF8* MyNameUtf8 = nullptr;   // имя своего персонажа для записи 4125 (живёт до конца процесса)
		delete MyNameUtf8; MyNameUtf8 = new FTCHARToUTF8(*C->MyName);
		C->Mems = {
			{ kMyUser, 0, MyNameUtf8->Get(), 1, 60, 1, 900 },
			{ 5001, 0, "ViceOne",  2, 55, 1, 800 },
			{ 5002, 1, "ViceTwo",  2, 50, 0, 700 },
			{ 5003, 0, "Valued",   3, 45, 1, 600 },
			{ 5004, 2, "Regular",  4, 40, 0, 500 },
			{ 5005, 0, "Rookie",   5, 30, 1, 400 },
			{ 5006, 1, "Seventh",  5, 20, 0, 300 },
			{ 5007, 0, "Eighth",   4, 25, 1, 200 },
		};
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		C->NetScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		Net->SelectedCharId = kMe; Net->SelfClanRank = 0;   // перезаход: звания ещё нет, его приносит 4135
		TWeakPtr<FCtx> WC(C);
		C->bCap = Pet->BeginSendCapture([WC](int32 Op, const TArray<uint8>& Body, bool, TCHAR)
		{
			TSharedPtr<FCtx> P = WC.Pin();
			if (P) { P->Sent.Add(TPair<int32, TArray<uint8>>(Op, Body)); }
		}, 0);
		C->Expect(C->bCap, TEXT("подготовка: перехват исходящих взведён (в сеть ничего не уходит)"));
		Cl->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		Cm->CloseWindow();
		After(C, 0.3f, &S0);
	}
}

// Консольная команда самотеста.
static FAutoConsoleCommandWithWorldAndArgs GAsdaClanTabSelfTest(
	TEXT("asda.Clan.TabSelfTest"),
	TEXT("Самотест вкладки «Клан» окна 6400 путём игрока: 4135 → звание; состав 4123/4125/4126 и пагинация; «Информация» 6511/6512, ")
	TEXT("заметка 6518; «Предводитель» — шесть страниц; выход главы 12432/12433. Сборка мусора между шагами. Офлайн. `shots=<папка>`, `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W) { AsdaClanTabTest::Run(A, W); }));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
