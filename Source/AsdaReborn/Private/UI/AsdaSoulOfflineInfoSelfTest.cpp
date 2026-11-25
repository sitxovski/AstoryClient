// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «СВЕДЕНИЯ О ДРУГЕ ВНЕ ИГРЫ + ОКНО ПРЕЖНИХ ДРУЗЕЙ 6640» (24.10.2026, агент sm-offline): `asda.Soul.SelfTestOfflineInfo [quit]`.
//   Задача владельца 24.10: «во вкладке Друг при Друге не в сети — вся информация: имя, уровень, профессия, локация, последний выход».
//   Сервер: P79-SQL (`AstoryServer/Docs/DBManager/soulmate-offline-friend-p79.md`) — ответ 2243 на 2244 несёт строки активной пары
//   SmLvl 254 (персонаж) и 253 (карта/канал/время выхода UTC). Всё — штатным путём: приём `UAsdaNetSubsystem::DebugFeedPacket` →
//   `HandlePacket` → делегат; исходящие — перехват `BeginSendCapture` (в сеть не уходит ничего); кнопки — `UButton::OnClicked`;
//   окно — `UAsdaCommunitySubsystem::OpenOnTab(0)`. Офлайн (-AsdaNoNet), на карте мира.
//   Этапы:
//     1  5220 «Друг вне игры» + показ вкладки → ровно один автозапрос 2244; поля пусты до ответа;
//     2  ответ 2243 (2 прежних Друга + строки P79): 6520 имя, 6521 «33 Ур.», 6556 профессия (loc 12556+25), 6523 «  2 кан. <карта>»,
//        6522 местное время выхода (формат по языку); окно 6640 НЕ поднято (это автозапрос); повторный показ вкладки — без 2244;
//     3  кнопка 6584 → 2244 → 2243: окно 6640 — заголовок 12171, группы «Душа: 2»/«Душа: 1»/«Душа: 0», имена/уровни/профессии
//        (loc 12300+), подписи 12014/11029/11030, строки P79 в сетке не видны, скрытые узлы Name4..6/N*Time погашены; 6642 закрывает;
//     4  5110 (Друг в игре) — 6522 пуст; 5226 — поля пусты сразу, через OfflineFriendAfterLeaveSec — новый 2244, ответ обновил поля;
//     5  предел частоты: 5226 в пределах 30 с — 2244 не ушёл, стоит один отложенный; закрытие окна его снимает;
//     6  стоковый ответ (без строк P79): поля пусты, «Вне игры»; повторный показ вкладки — без 2244;
//     7  чужой Smidx в строках 254/253 — не берётся;
//     8  своя одержимость (5220 @32 = 2) — автозапроса нет, поля пусты;
//     9  ключи отката: OfflineFriendInfo 0 — автозапроса нет, строки P79 не читаются; OldSoulGrid 0 — кнопка шлёт 2244, окно 6640 не встаёт;
//    10  ★ ревизия 24.10 (б): ответ после 5226 с прежним выходом (раньше 5226) — поля пусты, ОДИН повтор через предел частоты, свежий
//        ответ принят, опрос прекращён; время «как в прошлом ответе» — тоже повтор; второй прежний ответ принят (без бесконечного опроса);
//        ключ OfflineLeaveRetry 0 — прежний порт (принят сразу, повтора нет);
//    11  ★ ревизия 24.10 (а): ожидание ответа на кнопку 6584 снимается сменой карты (ответ после неё 6640 не поднимает) и истекает
//        по сроку годности; ключ OldSoulGridReplyTtlSec 0 — прежний порт (ожидание пережило смену карты, 6640 встало); в срок — встаёт.
//   Итог: «ДРУГ-ВНЕ-ИГРЫ ИТОГ: ok=N fail=M».
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
#include "Text/AsdaLang.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaCommunitySubsystem.h"
#include "UI/AsdaCommunityFriendSubsystem.h"   // AsdaCommunityUi::FindLabel
#include "UI/AsdaMinimapSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaSoulmateSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSoulOfflineTest, Log, All);

namespace AsdaSoulOfflineTest
{
	static const TCHAR* kCmd = TEXT("asda.Soul.SelfTestOfflineInfo");   // имя команды
	static constexpr int32  kMe = 1001;          // свой charId на прогон
	static constexpr uint16 kMyUserNum = 321;    // свой userNum
	static constexpr int32  kMateU = 777;        // userNum Друга
	static constexpr int32  kMateCid = 900777;   // charId Друга
	static constexpr int32  kSlot = 5;           // Smidx пары
	static constexpr uint8  kFriendLv = 3;       // уровень Дружбы (свой блок +12)

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                  // счёт
		TWeakObjectPtr<UWorld> W;                // мир прогона
		bool bQuit = false;                      // выйти после итога
		TUniquePtr<FAsdaNetSelfScope> NetScope;  // свои charId/userNum — вернуть в конце
		int32 ChannelWas = 0;                    // свой канал до прогона
		bool bCap = false;                       // перехват исходящих взведён этим прогоном
		int32 Sent2244 = 0;                      // перехваченных 2244
		int32 Mark = 0;                          // счётчик 2244 на начало шага
		int32 MapId = 3;                         // карта Друга (для «Локации»)
		FDateTime Stamp;                         // ★ 24.10.2026: «свежее» время выхода (UTC, до секунды) этапов 4 и 10
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaSoulOfflineTest, Log, TEXT("ДРУГ-ВНЕ-ИГРЫ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaSoulOfflineTest, Error, TEXT("ДРУГ-ВНЕ-ИГРЫ FAIL %s"), *What); }
		}
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Строка ответа 2243 (27 б).
	struct FRow { int32 Smidx; uint8 SmLvl, Lvl, Job; const TCHAR* Name; };

	using AsdaSelfTestCtx::GIOf;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	using AsdaSelfTestCtx::Sub;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	using AsdaSelfTestCtx::Feed;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия отличалась только тем, что подсистема сети бралась через `Sub<UAsdaNetSubsystem>` — то же, что `AsdaTestNet::Get`.
	using AsdaSelfTestCtx::ByStage::After;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	static FString Loc(const FCtxRef& C, int32 Id) { return AsdaNotice::Loc(GIOf(C), Id); }
	static UUserWidget* Wnd(const FCtxRef& C) { UAsdaCommunitySubsystem* Cm = Sub<UAsdaCommunitySubsystem>(C); return Cm ? Cm->GetWindowWidget() : nullptr; }
	static FString Txt(const FCtxRef& C, const FString& Idc)
	{
		UUserWidget* W = Wnd(C);
		const UTextBlock* T = W ? AsdaCommunityUi::FindLabel(W, Idc) : nullptr;
		return T ? T->GetText().ToString() : FString(TEXT("<нет узла>"));
	}
	static bool NodeCollapsed(const FCtxRef& C, const TCHAR* Name)
	{
		UUserWidget* W = Wnd(C);
		const UWidget* N = W ? AsdaUi::FindNodeExact(W, Name) : nullptr;
		return N && N->GetVisibility() == ESlateVisibility::Collapsed;
	}
	static void ClickBtn(const FCtxRef& C, const TCHAR* Idc)
	{
		UUserWidget* W = Wnd(C);
		if (UButton* B = W ? Cast<UButton>(W->GetWidgetFromName(FName(Idc))) : nullptr) { B->OnClicked.Broadcast(); }
		else { C->Expect(false, FString::Printf(TEXT("нет кнопки %s"), Idc)); }
	}
	static FString MapName(const FCtxRef& C, int32 Map)
	{
		UAsdaTextSubsystem* T = Sub<UAsdaTextSubsystem>(C);
		return T ? T->GetText(TEXT("Map_info"), Map, TEXT("name")) : FString();
	}
	// «Душа: N» — loc 12182 с числом в [sd1] (подстановка — независимо от подсистемы).
	static FString SoulTitle(const FCtxRef& C, int32 N) { return Loc(C, 12182).Replace(TEXT("[sd1]"), *FString::FromInt(N)); }
	// Ожидаемый текст 6522: UTC → местное время ПК, формат по языку (RU «дд.мм.гггг чч:мм», иначе «гггг-мм-дд чч:мм»).
	static FString WantLogout(int32 Y, int32 Mo, int32 D, int32 H, int32 Mi, int32 S)
	{
		const FDateTime L = FDateTime(Y, Mo, D, H, Mi, S) + (FDateTime::Now() - FDateTime::UtcNow());
		return FAsdaLang::IsRu()
			? FString::Printf(TEXT("%02d.%02d.%04d %02d:%02d"), L.GetDay(), L.GetMonth(), L.GetYear(), L.GetHour(), L.GetMinute())
			: FString::Printf(TEXT("%04d-%02d-%02d %02d:%02d"), L.GetYear(), L.GetMonth(), L.GetDay(), L.GetHour(), L.GetMinute());
	}

	// ★ 24.10.2026: время выхода «сейчас» (UTC, без долей секунды — строка P79 точна до секунды) и его запись «yyyyMMddHHmmss».
	static FDateTime UtcNowSec()
	{
		const FDateTime U = FDateTime::UtcNow();
		return FDateTime(U.GetYear(), U.GetMonth(), U.GetDay(), U.GetHour(), U.GetMinute(), U.GetSecond());
	}
	static FString StampOf(const FDateTime& U) { return U.ToString(TEXT("%Y%m%d%H%M%S")); }
	static FString WantLogoutAt(const FDateTime& U) { return WantLogout(U.GetYear(), U.GetMonth(), U.GetDay(), U.GetHour(), U.GetMinute(), U.GetSecond()); }

	// 5220 (101 б): свой блок соулмейта.
	static TArray<uint8> Body5220(uint8 Login, uint8 Connect = 0)
	{
		TArray<uint8> B; B.SetNumZeroed(101);
		AsdaBytes::WrI32(&B[0], kMe); AsdaBytes::WrI16(&B[4], kMyUserNum);
		AsdaBytes::WrI32(&B[6], kSlot);
		B[18] = kFriendLv;
		AsdaBytes::WrI32(&B[19], kMateCid); AsdaBytes::WrI16(&B[23], Login ? kMateU : -1);
		B[25] = Login;
		B[32] = Connect;
		B[33] = 1;
		return B;
	}
	// 5110 (251 б): Друг в игре.
	static TArray<uint8> Body5110(int32 Map)
	{
		TArray<uint8> B; B.SetNumZeroed(251);
		AsdaBytes::WrI32(&B[0], kSlot); AsdaBytes::WrI32(&B[4], kMateCid); AsdaBytes::WrI16(&B[8], kMateU);
		B[29] = 42; B[38] = (uint8)Map; B[39] = 0; B[40] = 2; B[50] = kFriendLv;
		FAsdaLoginData::EncodeCp1251(TEXT("Mate"), &B[63], 19);
		return B;
	}
	// 2243 (243 б = 9 × 27): строки как пришли бы от DBManager (недостающие — нули, как у `memset` буфера строк).
	static TArray<uint8> Body2243(const TArray<FRow>& Rows)
	{
		TArray<uint8> B; B.SetNumZeroed(243);
		for (int32 i = 0; i < Rows.Num() && i < 9; ++i)
		{
			uint8* R = &B[i * 27];
			AsdaBytes::WrI32(R, Rows[i].Smidx);
			R[4] = Rows[i].SmLvl; R[5] = Rows[i].Lvl; R[6] = Rows[i].Job;
			FAsdaLoginData::EncodeCp1251(Rows[i].Name, R + 7, 20);
		}
		return B;
	}
	// Ответ P79: два прежних Друга (31 — 2 персонажа, 32 — 1) + строки активной пары.
	static TArray<uint8> P79Reply(const FCtxRef& C, int32 Smidx, uint8 Lvl, const TCHAR* Stamp)
	{
		return Body2243({
			{ 31, 2, 40, 12, TEXT("OldA1") }, { 31, 2, 7, 23, TEXT("OldA2") },
			{ 32, 1, 15, 1, TEXT("OldB1") },
			{ Smidx, 254, Lvl, 25, TEXT("MateOff") },
			{ Smidx, 253, (uint8)C->MapId, 1, Stamp } });
	}
	static UAsdaSoulmateSubsystem* Sm(const FCtxRef& C) { return Sub<UAsdaSoulmateSubsystem>(C); }
	static bool FieldsEmpty(const FCtxRef& C)
	{
		return Txt(C, TEXT("SOUL_TXT02")).IsEmpty() && Txt(C, TEXT("SOUL_TXT03")).IsEmpty() && Txt(C, TEXT("SOUL_TXT10")).IsEmpty()
			&& Txt(C, TEXT("SOUL_TXT05")).TrimStartAndEnd().IsEmpty() && Txt(C, TEXT("SOUL_TXT04")).IsEmpty();
	}
	// Новый «вход в мир» с Другом вне игры: сброс сессии, своя 5220, окно на вкладке «Друг».
	static void FreshOffline(const FCtxRef& C, uint8 Connect = 0)
	{
		if (UAsdaCommunitySubsystem* Cm = Sub<UAsdaCommunitySubsystem>(C)) { Cm->CloseWindow(); }
		if (UAsdaSoulmateSubsystem* S = Sm(C)) { S->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
		Feed(C, 5220, Body5220(/*Login*/0, Connect));
		C->Mark = C->Sent2244;
		if (UAsdaCommunitySubsystem* Cm = Sub<UAsdaCommunitySubsystem>(C)) { Cm->OpenOnTab(0); }
	}

	static void S1(FCtxRef C); static void S2(FCtxRef C); static void S3(FCtxRef C); static void S3b(FCtxRef C);
	static void S4(FCtxRef C); static void S4b(FCtxRef C); static void S4c(FCtxRef C); static void S4d(FCtxRef C);
	static void S5(FCtxRef C); static void S6(FCtxRef C); static void S6b(FCtxRef C); static void S7(FCtxRef C);
	static void S8(FCtxRef C); static void S9(FCtxRef C); static void S9b(FCtxRef C); static void S9c(FCtxRef C);
	static void S10(FCtxRef C); static void S10b(FCtxRef C); static void S10c(FCtxRef C); static void S10d(FCtxRef C);
	static void S10e(FCtxRef C); static void S10f(FCtxRef C); static void S10g(FCtxRef C); static void S10h(FCtxRef C);
	static void S10i(FCtxRef C); static void S11(FCtxRef C); static void S11b(FCtxRef C);
	static void Finish(FCtxRef C);

	// ═══ 1: Друг вне игры, показ вкладки → один автозапрос ═══
	static void S1(FCtxRef C)
	{
		C->Expect(Wnd(C) != nullptr && Sm(C), TEXT("подготовка: окно сообщества 6400 открыто на вкладке «Друг»"));
		if (!Wnd(C) || !Sm(C)) { Finish(C); return; }
		C->Expect(C->Sent2244 == C->Mark + 1 && Sm(C)->GetOfflineAutoReqSentCount() >= 1,
			FString::Printf(TEXT("1: показ вкладки при Друге вне игры — автозапрос 2244 ушёл ровно один раз (ушло %d)"), C->Sent2244 - C->Mark));
		C->Expect(Txt(C, TEXT("SOUL_TXT01")) == Loc(C, 8054) && FieldsEmpty(C),
			FString::Printf(TEXT("1: до ответа — 6548 «%s», поля пусты"), *Txt(C, TEXT("SOUL_TXT01"))));
		Feed(C, 2243, P79Reply(C, kSlot, 33, TEXT("20261021063015")));
		After(C, 0.3f, &S2);
	}
	// ═══ 2: ответ — пять полей ═══
	static void S2(FCtxRef C)
	{
		const FString Lv = FString::Printf(TEXT("%d %s "), 33, *Loc(C, 12215));
		const FString Loca = FString::Printf(TEXT("  2 %s %s"), *Loc(C, 1187), *MapName(C, C->MapId));
		const FString Out = WantLogout(2026, 10, 21, 6, 30, 15);
		C->Expect(Txt(C, TEXT("SOUL_TXT01")) == Loc(C, 8054), TEXT("2: 6548 по-прежнему «Вне игры»"));
		C->Expect(Txt(C, TEXT("SOUL_TXT02")) == TEXT("MateOff"), FString::Printf(TEXT("2: 6520 имя «%s»"), *Txt(C, TEXT("SOUL_TXT02"))));
		C->Expect(Txt(C, TEXT("SOUL_TXT03")) == Lv, FString::Printf(TEXT("2: 6521 уровень «%s» (ждём «%s»)"), *Txt(C, TEXT("SOUL_TXT03")), *Lv));
		C->Expect(!FAsdaCharRecord::ClassName(25).IsEmpty() && Txt(C, TEXT("SOUL_TXT10")) == FAsdaCharRecord::ClassName(25),
			FString::Printf(TEXT("2: 6556 профессия «%s» (loc 12556+25)"), *Txt(C, TEXT("SOUL_TXT10"))));
		C->Expect(!MapName(C, C->MapId).IsEmpty() && Txt(C, TEXT("SOUL_TXT05")) == Loca,
			FString::Printf(TEXT("2: 6523 «Локация» «%s» (ждём «%s»)"), *Txt(C, TEXT("SOUL_TXT05")), *Loca));
		C->Expect(Txt(C, TEXT("SOUL_TXT04")) == Out, FString::Printf(TEXT("2: 6522 «Последний выход» «%s» (ждём «%s», язык %s)"),
			*Txt(C, TEXT("SOUL_TXT04")), *Out, FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en")));
		C->Expect(Txt(C, TEXT("SOUL_SB07")) == Loc(C, 12103), FString::Printf(TEXT("2: подпись 6575 «%s» (loc 12103)"), *Txt(C, TEXT("SOUL_SB07"))));
		C->Expect(!Sm(C)->IsOldSoulGridShown(), TEXT("2: ответ на АВТОзапрос окно прежних Друзей 6640 не поднимает"));
		// Повторный показ вкладки — сведения свежие, 2244 не нужен.
		C->Mark = C->Sent2244;
		if (UAsdaCommunitySubsystem* Cm = Sub<UAsdaCommunitySubsystem>(C)) { Cm->SwitchTab(1); Cm->SwitchTab(0); }
		C->Expect(C->Sent2244 == C->Mark && Txt(C, TEXT("SOUL_TXT02")) == TEXT("MateOff"), TEXT("2: повторный показ вкладки — без 2244, поля на месте"));
		C->Mark = C->Sent2244;
		ClickBtn(C, TEXT("Soul_OldSoul_Btn"));
		After(C, 0.2f, &S3);
	}
	// ═══ 3: кнопка 6584 → окно 6640 ═══
	static void S3(FCtxRef C)
	{
		C->Expect(C->Sent2244 == C->Mark + 1, TEXT("3: кнопка 6584 — 2244 ушёл (без предела частоты: действие игрока)"));
		Feed(C, 2243, P79Reply(C, kSlot, 33, TEXT("20261021063015")));
		After(C, 0.3f, &S3b);
	}
	static void S3b(FCtxRef C)
	{
		C->Expect(Sm(C)->IsOldSoulGridShown(), TEXT("3: ответ на кнопку — окно 6640 показано"));
		C->Expect(Txt(C, TEXT("OldSoulmate_Title")) == Loc(C, 12171), FString::Printf(TEXT("3: заголовок 6641 «%s»"), *Txt(C, TEXT("OldSoulmate_Title"))));
		C->Expect(Txt(C, TEXT("OldSoulmate_N1Text02")) == Loc(C, 12014) && Txt(C, TEXT("OldSoulmate_N2Text03")) == Loc(C, 11029)
			&& Txt(C, TEXT("OldSoulmate_N3Text04")) == Loc(C, 11030), TEXT("3: подписи столбцов 12014/11029/11030"));
		C->Expect(Txt(C, TEXT("OldSoulmate_Name1")) == SoulTitle(C, 2) && Txt(C, TEXT("OldSoulmate_Name2")) == SoulTitle(C, 1)
			&& Txt(C, TEXT("OldSoulmate_Name3")) == SoulTitle(C, 0),
			FString::Printf(TEXT("3: группы «%s» / «%s» / «%s»"), *Txt(C, TEXT("OldSoulmate_Name1")), *Txt(C, TEXT("OldSoulmate_Name2")), *Txt(C, TEXT("OldSoulmate_Name3"))));
		C->Expect(Txt(C, TEXT("OldSoulmate_N1Txt05")) == TEXT("OldA1") && Txt(C, TEXT("OldSoulmate_N1Text06")) == TEXT("40")
			&& Txt(C, TEXT("OldSoulmate_N1Text07")) == Loc(C, 12312) && Txt(C, TEXT("OldSoulmate_N1Text08")) == TEXT("OldA2")
			&& Txt(C, TEXT("OldSoulmate_N1Text10")) == Loc(C, 12323),
			FString::Printf(TEXT("3: группа 1 — «%s» %s «%s», «%s»"), *Txt(C, TEXT("OldSoulmate_N1Txt05")), *Txt(C, TEXT("OldSoulmate_N1Text06")),
				*Txt(C, TEXT("OldSoulmate_N1Text07")), *Txt(C, TEXT("OldSoulmate_N1Text08"))));
		C->Expect(Txt(C, TEXT("OldSoulmate_N2Text05")) == TEXT("OldB1") && Txt(C, TEXT("OldSoulmate_N2Text06")) == TEXT("15") && Txt(C, TEXT("OldSoulmate_N2Text08")).IsEmpty(),
			TEXT("3: группа 2 — один персонаж прежнего Друга 32"));
		C->Expect(Txt(C, TEXT("OldSoulmate_N3Text05")).IsEmpty() && Txt(C, TEXT("OldSoulmate_N3Text08")).IsEmpty(),
			TEXT("3: строки P79 (текущий Друг) в окне прежних Друзей не видны — группа 3 пуста"));
		C->Expect(NodeCollapsed(C, TEXT("OldSoulmate_N1Name4")) && NodeCollapsed(C, TEXT("OldSoulmate_N3Job6")) && NodeCollapsed(C, TEXT("OldSoulmate_N2Time")),
			TEXT("3: узлы 7500.. (Name4..6/Lv/Job) и N*Time скрыты (второй цикл InitControls)"));
		C->Expect(Txt(C, TEXT("SOUL_TXT02")) == TEXT("MateOff"), TEXT("3: поля вкладки по ответу на кнопку тоже обновлены"));
		ClickBtn(C, TEXT("OldSoulmate_CloseBtn"));
		C->Expect(!Sm(C)->IsOldSoulGridShown(), TEXT("3: 6642 закрывает окно 6640"));
		// ═══ 4: Друг входит в игру ═══
		Feed(C, 5110, Body5110(C->MapId));
		Feed(C, 5220, Body5220(/*Login*/1));
		After(C, 0.3f, &S4);
	}
	static void S4(FCtxRef C)
	{
		C->Expect(Txt(C, TEXT("SOUL_TXT01")) == Loc(C, 8053) && Txt(C, TEXT("SOUL_TXT02")) == TEXT("Mate") && Txt(C, TEXT("SOUL_TXT04")).IsEmpty(),
			FString::Printf(TEXT("4: Друг в игре — 6548 «%s», имя «%s», 6522 пуст"), *Txt(C, TEXT("SOUL_TXT01")), *Txt(C, TEXT("SOUL_TXT02"))));
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.OfflineFriendReqIntervalSec"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.OfflineFriendAfterLeaveSec"), 0);
		After(C, 1.2f, &S4b);   // предел частоты (1 с) истёк
	}
	static void S4b(FCtxRef C)
	{
		C->Mark = C->Sent2244;
		Feed(C, 5226, TArray<uint8>());
		C->Expect(Txt(C, TEXT("SOUL_TXT01")) == Loc(C, 8054) && FieldsEmpty(C), TEXT("4: 5226 — «Вне игры», прежние сведения устарели — поля пусты"));
		After(C, 0.5f, &S4c);
	}
	static void S4c(FCtxRef C)
	{
		C->Expect(C->Sent2244 == C->Mark + 1 && !Sm(C)->IsOfflineReqScheduled(), FString::Printf(TEXT("4: после 5226 — один автозапрос 2244 (ушло %d)"), C->Sent2244 - C->Mark));
		C->Stamp = UtcNowSec();   // ★ 24.10.2026: БД записала выход — время не раньше 5226 (фиксированная дата ушла бы в повтор правки (б))
		Feed(C, 2243, P79Reply(C, kSlot, 34, *StampOf(C->Stamp)));
		After(C, 0.3f, &S4d);
	}
	static void S4d(FCtxRef C)
	{
		const FString Lv = FString::Printf(TEXT("%d %s "), 34, *Loc(C, 12215));
		C->Expect(Txt(C, TEXT("SOUL_TXT03")) == Lv && Txt(C, TEXT("SOUL_TXT04")) == WantLogoutAt(C->Stamp) && Sm(C)->GetOfflineLeaveCheckState() == 0,
			FString::Printf(TEXT("4: новый ответ — 6521 «%s», 6522 «%s»"), *Txt(C, TEXT("SOUL_TXT03")), *Txt(C, TEXT("SOUL_TXT04"))));
		// ═══ 5: предел частоты 30 с ═══
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.OfflineFriendReqIntervalSec"), 30);
		Feed(C, 5110, Body5110(C->MapId));
		C->Mark = C->Sent2244;
		Feed(C, 5226, TArray<uint8>());
		After(C, 0.4f, &S5);
	}
	static void S5(FCtxRef C)
	{
		C->Expect(C->Sent2244 == C->Mark && Sm(C)->IsOfflineReqScheduled(), TEXT("5: 5226 в пределах 30 с — 2244 не ушёл, стоит один отложенный запрос"));
		if (UAsdaCommunitySubsystem* Cm = Sub<UAsdaCommunitySubsystem>(C)) { Cm->CloseWindow(); }
		C->Expect(!Sm(C)->IsOfflineReqScheduled() && C->Sent2244 == C->Mark, TEXT("5: окно закрыто — отложенный запрос снят, 2244 не ушёл"));
		// ═══ 6: стоковый ответ ═══
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.OfflineFriendReqIntervalSec"), 0);
		FreshOffline(C);
		After(C, 0.4f, &S6);
	}
	static void S6(FCtxRef C)
	{
		C->Expect(C->Sent2244 == C->Mark + 1, TEXT("6: новый вход, показ вкладки — автозапрос 2244"));
		Feed(C, 2243, Body2243({ { 31, 2, 40, 12, TEXT("OldA1") } }));
		After(C, 0.3f, &S6b);
	}
	static void S6b(FCtxRef C)
	{
		const FAsdaOfflineFriend& F = Sm(C)->GetOfflineFriendForCheck();
		C->Expect(F.bReplied && !F.bValid && FieldsEmpty(C) && Txt(C, TEXT("SOUL_TXT01")) == Loc(C, 8054),
			TEXT("6: стоковый ответ (без строк P79) — «Вне игры», поля пусты, как оригинал"));
		C->Mark = C->Sent2244;
		if (UAsdaCommunitySubsystem* Cm = Sub<UAsdaCommunitySubsystem>(C)) { Cm->SwitchTab(1); Cm->SwitchTab(0); }
		C->Expect(C->Sent2244 == C->Mark, TEXT("6: повторный показ вкладки — 2244 не повторяется (ответ уже был)"));
		// ═══ 7: чужой Smidx ═══
		FreshOffline(C);
		Feed(C, 2243, P79Reply(C, kSlot + 1, 33, TEXT("20261021063015")));
		After(C, 0.3f, &S7);
	}
	static void S7(FCtxRef C)
	{
		C->Expect(!Sm(C)->GetOfflineFriendForCheck().bValid && FieldsEmpty(C), TEXT("7: строки 254/253 чужого Smidx не берутся"));
		// ═══ 8: своя одержимость ═══
		FreshOffline(C, /*Connect*/2);
		After(C, 0.3f, &S8);
	}
	static void S8(FCtxRef C)
	{
		C->Expect(C->Sent2244 == C->Mark && FieldsEmpty(C) && Txt(C, TEXT("SOUL_TXT01")).IsEmpty(),
			TEXT("8: я одержим — автозапроса нет, статус и поля пусты (как оригинал)"));
		// ═══ 9: ключи отката ═══
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.OfflineFriendInfo"), 0);
		FreshOffline(C);
		Feed(C, 2243, P79Reply(C, kSlot, 33, TEXT("20261021063015")));
		After(C, 0.3f, &S9);
	}
	static void S9(FCtxRef C)
	{
		C->Expect(C->Sent2244 == C->Mark && FieldsEmpty(C) && !Sm(C)->GetOfflineFriendForCheck().bReplied,
			TEXT("9: ключ asda.Soulmate.OfflineFriendInfo 0 — автозапроса нет, строки P79 не читаются, поля пусты (как оригинал)"));
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.OfflineFriendInfo"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.OldSoulGrid"), 0);
		C->Mark = C->Sent2244;
		ClickBtn(C, TEXT("Soul_OldSoul_Btn"));
		Feed(C, 2243, P79Reply(C, kSlot, 33, TEXT("20261021063015")));
		After(C, 0.3f, &S9b);
	}
	static void S9b(FCtxRef C)
	{
		C->Expect(C->Sent2244 == C->Mark + 1 && !Sm(C)->IsOldSoulGridShown(), TEXT("9: ключ asda.Soulmate.OldSoulGrid 0 — кнопка шлёт 2244, окно 6640 не встаёт (прежний порт)"));
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.OldSoulGrid"), 1);
		After(C, 0.1f, &S10);
	}
	// ═══ 10 (★ ревизия 24.10, (б)): ответ после 5226 с прежним выходом — один повтор, свежий принят; второй прежний принят ═══
	static void S10(FCtxRef C)
	{
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.OfflineFriendReqIntervalSec"), 1);   // предел частоты 1 с — повтор через ~1 с
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.OfflineFriendAfterLeaveSec"), 0);
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.OfflineLeaveRetry"), 1);
		FreshOffline(C);
		After(C, 1.5f, &S10b);
	}
	static void S10b(FCtxRef C)
	{
		C->Expect(C->Sent2244 == C->Mark + 1, FString::Printf(TEXT("10: новый вход — автозапрос 2244 (ушло %d)"), C->Sent2244 - C->Mark));
		Feed(C, 2243, P79Reply(C, kSlot, 33, TEXT("20261021063015")));
		C->Expect(Txt(C, TEXT("SOUL_TXT02")) == TEXT("MateOff") && Sm(C)->GetOfflineLeaveCheckState() == 0 && !Sm(C)->IsOfflineInfoStale(),
			TEXT("10: ответ без предшествующего 5226 принят сразу (сверять не с чем) — поля заполнены"));
		Feed(C, 5110, Body5110(C->MapId));   // Друг вошёл
		After(C, 1.2f, &S10c);               // предел частоты (1 с) истёк
	}
	static void S10c(FCtxRef C)
	{
		C->Mark = C->Sent2244;
		Feed(C, 5226, TArray<uint8>());
		C->Expect(C->Sent2244 == C->Mark + 1 && Sm(C)->GetOfflineLeaveCheckState() == 1,
			FString::Printf(TEXT("10: 5226 — автозапрос 2244 (ушло %d), ждём ответ не раньше 5226 (состояние проверки %d)"),
				C->Sent2244 - C->Mark, Sm(C)->GetOfflineLeaveCheckState()));
		Feed(C, 2243, P79Reply(C, kSlot, 33, TEXT("20261021063015")));   // БД ещё не записала выход — прежнее время
		C->Expect(FieldsEmpty(C) && Sm(C)->IsOfflineInfoStale() && Sm(C)->GetOfflineLeaveCheckState() == 2 && Sm(C)->IsOfflineReqScheduled()
			&& C->Sent2244 == C->Mark + 1,
			FString::Printf(TEXT("10: ответ с прежним выходом (раньше 5226) — поля пусты, устарело=%d, проверка %d, отложен один повтор=%d, 2244 пока %d"),
				Sm(C)->IsOfflineInfoStale() ? 1 : 0, Sm(C)->GetOfflineLeaveCheckState(), Sm(C)->IsOfflineReqScheduled() ? 1 : 0, C->Sent2244 - C->Mark));
		After(C, 1.5f, &S10d);
	}
	static void S10d(FCtxRef C)
	{
		C->Expect(C->Sent2244 == C->Mark + 2 && !Sm(C)->IsOfflineReqScheduled(),
			FString::Printf(TEXT("10: повторный 2244 ушёл через предел частоты (всего %d)"), C->Sent2244 - C->Mark));
		C->Stamp = UtcNowSec();
		Feed(C, 2243, P79Reply(C, kSlot, 35, *StampOf(C->Stamp)));   // БД записала выход
		const FString Lv = FString::Printf(TEXT("%d %s "), 35, *Loc(C, 12215));
		C->Expect(Txt(C, TEXT("SOUL_TXT03")) == Lv && Txt(C, TEXT("SOUL_TXT04")) == WantLogoutAt(C->Stamp) && !Sm(C)->IsOfflineInfoStale()
			&& Sm(C)->GetOfflineLeaveCheckState() == 0 && !Sm(C)->IsOfflineReqScheduled(),
			FString::Printf(TEXT("10: свежий ответ принят — 6521 «%s», 6522 «%s», проверка снята, повторов нет"),
				*Txt(C, TEXT("SOUL_TXT03")), *Txt(C, TEXT("SOUL_TXT04"))));
		After(C, 1.5f, &S10e);
	}
	static void S10e(FCtxRef C)
	{
		C->Expect(C->Sent2244 == C->Mark + 2 && !Sm(C)->IsOfflineReqScheduled(), TEXT("10: после свежего ответа 2244 больше не уходит (опроса нет)"));
		Feed(C, 5110, Body5110(C->MapId));
		After(C, 1.2f, &S10f);
	}
	// Второй признак «прежний»: время выхода как в прошлом ответе (недавнее — в пределах допуска часов, первым признаком не ловится).
	static void S10f(FCtxRef C)
	{
		C->Mark = C->Sent2244;
		Feed(C, 5226, TArray<uint8>());
		Feed(C, 2243, P79Reply(C, kSlot, 35, *StampOf(C->Stamp)));
		C->Expect(FieldsEmpty(C) && Sm(C)->GetOfflineLeaveCheckState() == 2 && Sm(C)->IsOfflineReqScheduled() && C->Sent2244 == C->Mark + 1,
			FString::Printf(TEXT("10: время выхода как в прошлом ответе — тоже прежний: поля пусты, один повтор отложен (проверка %d, 2244 %d)"),
				Sm(C)->GetOfflineLeaveCheckState(), C->Sent2244 - C->Mark));
		After(C, 1.5f, &S10g);
	}
	static void S10g(FCtxRef C)
	{
		C->Expect(C->Sent2244 == C->Mark + 2, FString::Printf(TEXT("10: повтор ушёл (всего %d)"), C->Sent2244 - C->Mark));
		Feed(C, 2243, P79Reply(C, kSlot, 35, *StampOf(C->Stamp)));   // повтор тоже прежний (у Друга, например, сбой записи)
		C->Expect(Txt(C, TEXT("SOUL_TXT02")) == TEXT("MateOff") && Txt(C, TEXT("SOUL_TXT04")) == WantLogoutAt(C->Stamp) && !Sm(C)->IsOfflineInfoStale()
			&& Sm(C)->GetOfflineLeaveCheckState() == 0 && !Sm(C)->IsOfflineReqScheduled(),
			TEXT("10: повтор тоже прежний — принят как есть (одна попытка, без бесконечного опроса)"));
		After(C, 1.5f, &S10h);
	}
	static void S10h(FCtxRef C)
	{
		C->Expect(C->Sent2244 == C->Mark + 2 && !Sm(C)->IsOfflineReqScheduled(), TEXT("10: после второго прежнего ответа 2244 больше не уходит"));
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.OfflineLeaveRetry"), 0);
		Feed(C, 5110, Body5110(C->MapId));
		After(C, 1.2f, &S10i);
	}
	static void S10i(FCtxRef C)
	{
		C->Mark = C->Sent2244;
		Feed(C, 5226, TArray<uint8>());
		Feed(C, 2243, P79Reply(C, kSlot, 33, TEXT("20261021063015")));
		C->Expect(Txt(C, TEXT("SOUL_TXT02")) == TEXT("MateOff") && Txt(C, TEXT("SOUL_TXT04")) == WantLogout(2026, 10, 21, 6, 30, 15)
			&& Sm(C)->GetOfflineLeaveCheckState() == 0 && !Sm(C)->IsOfflineReqScheduled() && C->Sent2244 == C->Mark + 1,
			TEXT("10: ключ asda.Soulmate.OfflineLeaveRetry 0 — прежний порт: прежний выход принят сразу, повтора нет (контроль)"));
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.OfflineLeaveRetry"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.OfflineFriendReqIntervalSec"), 0);
		After(C, 0.2f, &S11);
	}
	// ═══ 11 (★ ревизия 24.10, (а)): ожидание ответа на кнопку 6584 — сброс сменой карты и срок годности ═══
	static void S11(FCtxRef C)
	{
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.OldSoulGridReplyTtlSec"), 15.f);
		Sm(C)->CloseOldSoulGrid();
		C->Mark = C->Sent2244;
		ClickBtn(C, TEXT("Soul_OldSoul_Btn"));
		C->Expect(C->Sent2244 == C->Mark + 1 && Sm(C)->IsOldSoulGridShowPending(), TEXT("11: кнопка 6584 — 2244 ушёл, ждём ответ"));
		Sm(C)->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		C->Expect(!Sm(C)->IsOldSoulGridShowPending(), TEXT("11: смена карты — ожидание ответа на кнопку снято (ответ по прежнему соединению не придёт)"));
		Feed(C, 2243, P79Reply(C, kSlot, 33, TEXT("20261021063015")));   // ответ на следующий (авто)запрос по новому соединению
		C->Expect(!Sm(C)->IsOldSoulGridShown(), TEXT("11: 2243 после смены карты окно прежних Друзей 6640 НЕ поднимает"));
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.OldSoulGridReplyTtlSec"), 0.3f);
		ClickBtn(C, TEXT("Soul_OldSoul_Btn"));
		C->Expect(Sm(C)->IsOldSoulGridShowPending(), TEXT("11: кнопка 6584 (срок годности 0,3 с) — ждём ответ"));
		After(C, 0.6f, &S11b);   // ответ «потерялся» (ошибка БД) — срок истёк
	}
	static void S11b(FCtxRef C)
	{
		Feed(C, 2243, P79Reply(C, kSlot, 33, TEXT("20261021063015")));
		C->Expect(!Sm(C)->IsOldSoulGridShown() && !Sm(C)->IsOldSoulGridShowPending(),
			TEXT("11: 2243 позже срока годности — 6640 не поднято, ожидание снято"));
		// Контроль: ключ 0 — прежний порт, ожидание переживает смену карты и поднимает 6640 ответом на автозапрос.
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.OldSoulGridReplyTtlSec"), 0.f);
		ClickBtn(C, TEXT("Soul_OldSoul_Btn"));
		Sm(C)->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		const bool bKept = Sm(C)->IsOldSoulGridShowPending();
		Feed(C, 2243, P79Reply(C, kSlot, 33, TEXT("20261021063015")));
		C->Expect(bKept && Sm(C)->IsOldSoulGridShown(),
			TEXT("11: ключ asda.Soulmate.OldSoulGridReplyTtlSec 0 — прежний порт: ожидание пережило смену карты, 6640 встало (контроль)"));
		Sm(C)->CloseOldSoulGrid();
		// В срок при ключе по умолчанию — окно встаёт (правка не мешает штатному пути).
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.OldSoulGridReplyTtlSec"), 15.f);
		ClickBtn(C, TEXT("Soul_OldSoul_Btn"));
		Feed(C, 2243, P79Reply(C, kSlot, 33, TEXT("20261021063015")));
		C->Expect(Sm(C)->IsOldSoulGridShown() && !Sm(C)->IsOldSoulGridShowPending(), TEXT("11: ответ на кнопку в срок — 6640 показано (положительный контроль)"));
		Sm(C)->CloseOldSoulGrid();
		After(C, 0.1f, &S9c);
	}
	static void S9c(FCtxRef C) { Finish(C); }

	static void Finish(FCtxRef C)
	{
		UWorld* W = C->W.Get();
		{
			TArray<uint8> B = Body5220(0);
			AsdaBytes::WrI32(&B[6], -1);
			Feed(C, 5220, B);   // «слота нет» — снять признак пары у подсистем
		}
		if (UAsdaSoulmateSubsystem* S = Sm(C)) { S->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
		if (UAsdaCommunitySubsystem* Cm = Sub<UAsdaCommunitySubsystem>(C)) { Cm->CloseWindow(); }
		if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C)) { Net->CurrentChannel = C->ChannelWas; }
		if (C->bCap) { if (UAsdaPetSubsystem* Pet = Sub<UAsdaPetSubsystem>(C)) { Pet->EndSendCapture(); } }
		C->NetScope.Reset();
		AsdaSelfTestKit::Finish(W, kCmd, C->bQuit, LogAsdaSoulOfflineTest, TEXT("ДРУГ-ВНЕ-ИГРЫ"), C->Ok, C->Fail);
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(A);
		// Свой персонаж нужен окну мира и мини-карте — офлайн его нет: штатный фейковый спавн и повтор через 2 с.
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s selfready%s"), kCmd, KA.bQuit ? TEXT(" quit") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaSoulmateSubsystem* S = GI ? GI->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr;
		UAsdaCommunitySubsystem* Cm = GI ? GI->GetSubsystem<UAsdaCommunitySubsystem>() : nullptr;
		UAsdaPetSubsystem* Pet = GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr;
		if (!Net || !S || !Cm || !Pet) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем")); return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W; C->bQuit = KA.bQuit;
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		C->NetScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		Net->SelectedCharId = kMe; Net->SelfUserNum = kMyUserNum; Net->bSelfUserNumKnown = true;
		C->ChannelWas = Net->CurrentChannel; Net->CurrentChannel = 0;
		for (const TCHAR* K : { TEXT("asda.Soulmate.PopFrom5220"), TEXT("asda.Soulmate.OfflineInfo"), TEXT("asda.Soulmate.LocationText"),
		                        TEXT("asda.Soulmate.OfflineFriendInfo"), TEXT("asda.Soulmate.OldSoulGrid"), TEXT("asda.Soulmate.ShareBlockVisible"),
		                        TEXT("asda.Soulmate.OfflineLeaveRetry") })
		{
			AsdaCVarSave::SetForTest(K, 1);
		}
		// Первый шаг — «предела частоты нет» (прежние прогоны пакета могли слать 2244 только что); шаги 4–5 ставят свой предел.
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.OfflineFriendReqIntervalSec"), 0);
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.OfflineFriendAfterLeaveSec"), 5);
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.OldSoulGridReplyTtlSec"), 15.f);   // ★ 24.10.2026: срок годности ожидания кнопки 6584 — умолчание
		TWeakPtr<FCtx> WC(C);
		C->bCap = Pet->BeginSendCapture([WC](int32 Op, const TArray<uint8>&, bool, TCHAR)
		{
			if (Op != 2244) { return; }
			if (TSharedPtr<FCtx> P = WC.Pin()) { ++P->Sent2244; }
		}, 0);
		C->Expect(C->bCap, TEXT("подготовка: перехват исходящих взведён (в сеть ничего не уходит)"));
		if (const UAsdaMinimapSubsystem* MM = GI->GetSubsystem<UAsdaMinimapSubsystem>())
		{
			if (MM->GetDrawnMapId() >= 0) { C->MapId = MM->GetDrawnMapId(); }
		}
		FreshOffline(C);
		After(C, 1.0f, &S1);
	}
}

// Консольная команда самотеста.
static FAutoConsoleCommandWithWorldAndArgs GAsdaSoulSelfTestOfflineInfo(
	TEXT("asda.Soul.SelfTestOfflineInfo"),
	TEXT("Самотест вкладки «Друг»: сведения о Друге вне игры (автозапрос 2244 → строки P79 ответа 2243: имя, уровень, профессия, локация, ")
	TEXT("последний выход), предел частоты 30 с, окно прежних Друзей 6640 (кнопка 6584, закрытие 6642), ключи отката; ")
	TEXT("повтор при прежнем выходе после 5226 и срок/сброс ожидания кнопки 6584 (ревизия 24.10). Офлайн. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W) { AsdaSoulOfflineTest::Run(A, W); }));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
