// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «СВЕДЕНИЯ ДРУГА / SHARE / ТОЧКА НА КАРТЕ» (23.10.2026, агент sm-info): `asda.Soul.SelfTestInfoShareMark [quit]`. [ПОРТ]
//   Баги владельца 23.10: «Can't see soulmate info while he or she is offline», «"share" flag is dropping», розовая точка Друга
//   на мини-карте и карте мира. Всё — штатным путём: приём через `UAsdaNetSubsystem::DebugFeedPacket` → `HandlePacket` → делегаты;
//   клик галки — `UCheckBox::OnCheckStateChanged` (тот же делегат, что шлёт Slate); исходящие — перехват `BeginSendCapture`
//   (в сеть не уходит ничего); окно сообщества — `UAsdaCommunitySubsystem::OpenOnTab(0)`. Офлайн (-AsdaNoNet), на карте мира.
//   Этапы:
//     1  до своей 5220: клик галки 6543 — 5221 НЕ уходит, галка вернулась (asda.Soulmate.ShareSendNeedsServer);
//     2  5220 «Друг вне игры»: 6548 = loc 8054, имя/локация пусты, уровень Дружбы 6557 и доля 6558 — из своего блока,
//        строки share — имена из 5220 (не из списка персонажей), галки 6543/6545/6546 горят, строка 2 (пустое имя) спрятана;
//        отметки Друга нет; клик «снять 6545» → 5221 {6546=1, 6543=1, 6544=0, 6545=0} — остальные флаги сохранены;
//     3  5110 + 5220 «в игре»: 6548 = 8053, имя, 6523 «Локация» (карта/канал), отметка Couple на мини-карте (1 квад) в тайле записи;
//        5111 — тайл обновлён; большая карта — 1 квад;
//     4  5227: другой канал — отметки нет, 6523 «  2 кан. …»; свой канал — отметка вернулась;
//     5  5110 с другой картой — на мини-карте не рисуется; своя карта — снова;
//     6  сброс сессии MapChanged (подсистема + мини-карта) — отметка вернулась на следующем кадре;
//     7  5226 «Друг уходит»: 6548 = 8054, имя/локация пусты, уровень Дружбы на месте, отметки нет;
//     8  своя одержимость (5220 @32 = 2): статус пуст, галки погашены, клик — 5221 не уходит;
//     9  ключи отката: MapMark 0 — отметки нет; LocationText 0 — 6523 прежний (класс); OfflineInfo 0 — 5226 даёт прежнее «В игре».
//   Итог: «СВЕДЕНИЯ-ДРУГА ИТОГ: ok=N fail=M».
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
#include "Items/AsdaItemTableSubsystem.h"   // DT_smcharexp — опыт Дружбы для доли 50 %
#include "Items/AsdaItemDbRows.h"
#include "Engine/DataTable.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaCommunitySubsystem.h"
#include "UI/AsdaCommunityFriendSubsystem.h"   // AsdaCommunityUi::FindLabel
#include "UI/AsdaMinimapSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaSoulmateSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "Blueprint/UserWidget.h"
#include "Components/CheckBox.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSoulInfoTest, Log, All);

namespace AsdaSoulInfoTest
{
	static const TCHAR* kCmd = TEXT("asda.Soul.SelfTestInfoShareMark");   // имя команды
	static constexpr int32  kMe = 1001;          // свой charId на прогон
	static constexpr uint16 kMyUserNum = 321;    // свой userNum
	static constexpr int32  kMateU = 777;        // userNum Друга
	static constexpr int32  kMateCid = 900777;   // charId Друга
	static constexpr int32  kSlot = 5;           // слот пары
	static constexpr uint8  kFriendLv = 3;       // уровень Дружбы (свой блок +12)

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                  // счёт
		TWeakObjectPtr<UWorld> W;                // мир прогона
		bool bQuit = false;                      // выйти после итога
		TUniquePtr<FAsdaNetSelfScope> NetScope;  // свои charId/userNum — вернуть в конце
		int32 ChannelWas = 0;                    // свой канал до прогона
		TArray<FAsdaCharRecord> CharListWas;     // список персонажей до прогона
		bool bCap = false;                       // перехват исходящих взведён этим прогоном
		TArray<TArray<uint8>> Sent5221;          // перехваченные тела 5221
		int32 MapId = -1;                        // карта мини-карты
		FIntPoint Tile = FIntPoint(100, 100);    // тайл Друга
		int32 RefusedWas = 0;                    // счётчик отказов 5221 до шага
		int64 Exp = 40;                          // опыт Дружбы: середина уровня kFriendLv по DT_smcharexp (доля 50 %)
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaSoulInfoTest, Log, TEXT("СВЕДЕНИЯ-ДРУГА ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaSoulInfoTest, Error, TEXT("СВЕДЕНИЯ-ДРУГА FAIL %s"), *What); }
		}
	};
	using FCtxRef = TSharedRef<FCtx>;

	using AsdaSelfTestCtx::GIOf;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	using AsdaSelfTestCtx::Sub;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	using AsdaSelfTestCtx::Feed;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия отличалась только тем, что подсистема сети бралась через `Sub<UAsdaNetSubsystem>` — то же, что `AsdaTestNet::Get`.
	using AsdaSelfTestCtx::ByStage::After;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	static FString Loc(const FCtxRef& C, int32 Id) { return AsdaNotice::Loc(GIOf(C), Id); }
	static UUserWidget* Wnd(const FCtxRef& C) { UAsdaCommunitySubsystem* Cm = Sub<UAsdaCommunitySubsystem>(C); return Cm ? Cm->GetWindowWidget() : nullptr; }
	static FString Txt(const FCtxRef& C, const TCHAR* Idc)
	{
		UUserWidget* W = Wnd(C);
		const UTextBlock* T = W ? AsdaCommunityUi::FindLabel(W, Idc) : nullptr;
		return T ? T->GetText().ToString() : FString(TEXT("<нет узла>"));
	}
	static UCheckBox* Chk(const FCtxRef& C, int32 I)
	{
		UUserWidget* W = Wnd(C);
		return W ? Cast<UCheckBox>(W->GetWidgetFromName(FName(*FString::Printf(TEXT("SOUL_CHECK%02d"), I)))) : nullptr;
	}
	// Клик игрока по галке: `UCheckBox` сам переключается и шлёт `OnCheckStateChanged` — повторяем ровно это.
	static void Click(const FCtxRef& C, int32 I)
	{
		if (UCheckBox* Cb = Chk(C, I))
		{
			const bool bNew = !Cb->IsChecked();
			Cb->SetIsChecked(bNew);
			Cb->OnCheckStateChanged.Broadcast(bNew);
		}
		else { C->Expect(false, FString::Printf(TEXT("нет галки SOUL_CHECK%02d"), I)); }
	}
	static bool Collapsed(const FCtxRef& C, int32 I) { UCheckBox* Cb = Chk(C, I); return Cb && Cb->GetVisibility() == ESlateVisibility::Collapsed; }
	static FString MapName(const FCtxRef& C, int32 Map)
	{
		UAsdaTextSubsystem* T = Sub<UAsdaTextSubsystem>(C);
		return T ? T->GetText(TEXT("Map_info"), Map, TEXT("name")) : FString();
	}

	// 5220 (одна запись 101 б): свой блок соулмейта.
	static TArray<uint8> Body5220(const FCtxRef& C, uint8 Login, uint8 Info, uint8 S1, uint8 S2, uint8 S3, uint8 Connect = 0)
	{
		TArray<uint8> B; B.SetNumZeroed(101);
		AsdaBytes::WrI32(&B[0], kMe); AsdaBytes::WrI16(&B[4], kMyUserNum);
		AsdaBytes::WrI32(&B[6], kSlot);
		AsdaBytes::WrI32(&B[10], (int32)C->Exp);   // опыт Дружбы (QWORD младшая половина)
		B[18] = kFriendLv;                         // уровень Дружбы
		AsdaBytes::WrI32(&B[19], kMateCid); AsdaBytes::WrI16(&B[23], Login ? kMateU : -1);
		B[25] = Login;
		B[28] = (uint8)FMath::Max(0, C->MapId);   // карта Друга
		B[29] = 0;                                 // канал Друга
		B[32] = Connect;                           // свой connecttype (2 — одержим)
		B[33] = Info; B[36] = S1; B[37] = S2; B[38] = S3;
		FAsdaLoginData::EncodeCp1251(TEXT("Alpha"), &B[39], 19);
		FAsdaLoginData::EncodeCp1251(TEXT("Gamma"), &B[79], 19);   // строка 2 (+53) — пустая: слот 11 без персонажа
		return B;
	}
	// 5110 (251 б): слот, Друг, карта/канал, тайл, уровень Дружбы, имя.
	static TArray<uint8> Body5110(const FCtxRef& C, int32 Map, FIntPoint Tile)
	{
		TArray<uint8> B; B.SetNumZeroed(251);
		AsdaBytes::WrI32(&B[0], kSlot); AsdaBytes::WrI32(&B[4], kMateCid); AsdaBytes::WrI16(&B[8], kMateU);
		B[10] = 0;                                  // канал Друга (gap_8BE[0])
		AsdaBytes::WrI32(&B[13], 1000); AsdaBytes::WrI32(&B[17], 500);
		AsdaBytes::WrI16(&B[21], 200);  AsdaBytes::WrI16(&B[23], 50);
		AsdaBytes::WrI16(&B[25], Tile.X); AsdaBytes::WrI16(&B[27], Tile.Y);
		B[29] = 42; B[38] = (uint8)Map; B[39] = 0; B[40] = 2; B[50] = kFriendLv;
		AsdaBytes::WrI32(&B[42], (int32)C->Exp);
		FAsdaLoginData::EncodeCp1251(TEXT("Mate"), &B[63], 19);
		return B;
	}
	// 5111 (50 б): тайл Друга (body[12]/[14] → rec[25]/[27]).
	static TArray<uint8> Body5111(const FCtxRef& C, FIntPoint Tile)
	{
		TArray<uint8> B; B.SetNumZeroed(50);
		AsdaBytes::WrI32(&B[0], 1000); AsdaBytes::WrI32(&B[4], 600);
		AsdaBytes::WrI16(&B[8], 200);  AsdaBytes::WrI16(&B[10], 60);
		AsdaBytes::WrI16(&B[12], Tile.X); AsdaBytes::WrI16(&B[14], Tile.Y);
		B[16] = 42; B[27] = 2; B[37] = kFriendLv;
		AsdaBytes::WrI32(&B[29], (int32)C->Exp);
		return B;
	}
	static TArray<uint8> Body5227(uint8 Chan, uint8 Map) { TArray<uint8> B; B.SetNumZeroed(2); B[0] = Chan; B[1] = Map; return B; }

	static bool MarkIs(const FCtxRef& C, FIntPoint Tile, int32 Map)
	{
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		int32 X = 0, Z = 0, M = 0;
		return Sm && Sm->GetCoupleMarkForCheck(X, Z, M) && X == Tile.X && Z == Tile.Y && M == Map;
	}
	static bool NoMark(const FCtxRef& C) { UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C); int32 X, Z, M; return Sm && !Sm->GetCoupleMarkForCheck(X, Z, M); }
	static int32 Drawn(const FCtxRef& C, bool bBig) { UAsdaMinimapSubsystem* MM = Sub<UAsdaMinimapSubsystem>(C); return MM ? MM->CountDrawnMarks(EAsdaMapMark::Couple, bBig) : -1; }
	static FString Last5221(const FCtxRef& C)
	{
		if (C->Sent5221.Num() == 0) { return TEXT("нет"); }
		const TArray<uint8>& B = C->Sent5221.Last();
		if (B.Num() < 4) { return TEXT("коротко"); }
		const int32 O = B.Num() - 4;
		return FString::Printf(TEXT("%d/%d/%d/%d"), B[O], B[O + 1], B[O + 2], B[O + 3]);
	}

	static void S1(FCtxRef C); static void S2(FCtxRef C); static void S2b(FCtxRef C); static void S3(FCtxRef C);
	static void S3b(FCtxRef C); static void S3c(FCtxRef C); static void S4(FCtxRef C); static void S4b(FCtxRef C);
	static void S5(FCtxRef C); static void S5b(FCtxRef C); static void S6(FCtxRef C); static void S6b(FCtxRef C);
	static void S7(FCtxRef C); static void S8(FCtxRef C); static void S8b(FCtxRef C); static void S9(FCtxRef C);
	static void S9b(FCtxRef C); static void Finish(FCtxRef C);

	// ═══ 1: до своей 5220 клик галки 5221 не шлёт ═══
	static void S1(FCtxRef C)
	{
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		C->Expect(Wnd(C) && Chk(C, 0), TEXT("подготовка: окно сообщества 6400, вкладка «Друг», галка 6543 есть"));
		if (!Sm || !Wnd(C)) { Finish(C); return; }
		C->RefusedWas = Sm->GetShareSendRefusedCount();
		const int32 Was = C->Sent5221.Num();
		Click(C, 0);
		C->Expect(C->Sent5221.Num() == Was && Sm->GetShareSendRefusedCount() == C->RefusedWas + 1 && Chk(C, 0) && !Chk(C, 0)->IsChecked(),
			TEXT("1: до своей 5220 клик 6543 — 5221 НЕ ушёл (состояние сервера неизвестно), галка вернулась"));
		Feed(C, 5220, Body5220(C, /*Login*/0, /*Info*/1, 1, 0, 1));
		After(C, 0.3f, &S2);
	}
	// ═══ 2: Друг вне игры ═══
	static void S2(FCtxRef C)
	{
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		const FString Off = Loc(C, 8054);
		const FString St = Txt(C, TEXT("SOUL_TXT01"));
		C->Expect(!Off.IsEmpty() && St == Off && (Off == TEXT("Вне игры") || Off == TEXT("Offline")),
			FString::Printf(TEXT("2: Друг вне игры — 6548 «%s» (loc 8054 «%s»)"), *St, *Off));
		C->Expect(Txt(C, TEXT("SOUL_TXT02")).IsEmpty() && Txt(C, TEXT("SOUL_TXT05")).TrimStartAndEnd().IsEmpty(),
			TEXT("2: имя 6520 и «Локация» 6523 пусты (сервер их без Друга не шлёт — как оригинал)"));
		const FString Lv = Txt(C, TEXT("SOUL_TXT12"));
		const FString Pct = Txt(C, TEXT("SOUL_TXT13"));
		C->Expect(Lv == FString::Printf(TEXT("%d %s "), (int32)kFriendLv, *Loc(C, 12215)) && Pct.StartsWith(TEXT("50.0")) && Pct.EndsWith(TEXT("%")) && Sm && Sm->GetFriendLevel() == kFriendLv,
			FString::Printf(TEXT("2: уровень Дружбы 6557 «%s» и доля 6558 «%s» — из своего блока (уровень %d)"), *Lv, *Pct, Sm ? Sm->GetFriendLevel() : -1));
		C->Expect(Txt(C, TEXT("SOUL_TXT07")) == TEXT("Alpha") && Txt(C, TEXT("SOUL_TXT09")) == TEXT("Gamma") && Collapsed(C, 1) && !Collapsed(C, 0) && !Collapsed(C, 2),
			FString::Printf(TEXT("2: строки share — имена из 5220 («%s»/«%s», не из списка персонажей), строка 2 без имени спрятана"),
				*Txt(C, TEXT("SOUL_TXT07")), *Txt(C, TEXT("SOUL_TXT09"))));
		C->Expect(Chk(C, 0) && Chk(C, 0)->IsChecked() && Chk(C, 2) && Chk(C, 2)->IsChecked() && Chk(C, 3) && Chk(C, 3)->IsChecked() && !Collapsed(C, 3),
			TEXT("2: галки 6543/6545/6546 горят по 5220 (@36/@38/@33)"));
		C->Expect(NoMark(C), TEXT("2: Друг вне игры — отметки на карте нет"));
		const int32 Was = C->Sent5221.Num();
		Click(C, 2);   // снять «поделиться» строки 3
		C->Expect(C->Sent5221.Num() == Was + 1 && Last5221(C) == TEXT("1/1/0/0"),
			FString::Printf(TEXT("2: снять 6545 → 5221 {6546,6543,6544,6545} = %s (ждём 1/1/0/0 — остальные флаги сохранены)"), *Last5221(C)));
		After(C, 0.1f, &S2b);
	}
	static void S2b(FCtxRef C)
	{
		Feed(C, 5110, Body5110(C, C->MapId, C->Tile));
		Feed(C, 5220, Body5220(C, /*Login*/1, 1, 1, 0, 0));
		After(C, 0.6f, &S3);
	}
	// ═══ 3: Друг в игре ═══
	static void S3(FCtxRef C)
	{
		const FString On = Loc(C, 8053);
		C->Expect(Txt(C, TEXT("SOUL_TXT01")) == On && Txt(C, TEXT("SOUL_TXT02")) == TEXT("Mate"),
			FString::Printf(TEXT("3: Друг в игре — 6548 «%s», имя «%s»"), *Txt(C, TEXT("SOUL_TXT01")), *Txt(C, TEXT("SOUL_TXT02"))));
		const FString LocTxt = Txt(C, TEXT("SOUL_TXT05"));
		const FString Want = FString::Printf(TEXT("  1 %s %s"), *Loc(C, 1187), *MapName(C, C->MapId));
		C->Expect(LocTxt == Want && !MapName(C, C->MapId).IsEmpty(), FString::Printf(TEXT("3: 6523 «Локация» = «%s» (ждём «%s»)"), *LocTxt, *Want));
		C->Expect(MarkIs(C, C->Tile, C->MapId), FString::Printf(TEXT("3: отметка Couple — тайл (%d,%d) карта %d"), C->Tile.X, C->Tile.Y, C->MapId));
		C->Expect(Drawn(C, false) == 1, FString::Printf(TEXT("3: на мини-карте нарисован %d квад miniCouplePos (ждём 1)"), Drawn(C, false)));
		C->Tile += FIntPoint(3, -2);
		Feed(C, 5111, Body5111(C, C->Tile));
		After(C, 0.4f, &S3b);
	}
	static void S3b(FCtxRef C)
	{
		C->Expect(MarkIs(C, C->Tile, C->MapId) && Drawn(C, false) == 1, FString::Printf(TEXT("3: 5111 — отметка в новом тайле (%d,%d)"), C->Tile.X, C->Tile.Y));
		if (UAsdaMinimapSubsystem* MM = Sub<UAsdaMinimapSubsystem>(C)) { MM->SetBigMapVisible(true); }
		After(C, 0.8f, &S3c);
	}
	static void S3c(FCtxRef C)
	{
		C->Expect(Drawn(C, true) == 1, FString::Printf(TEXT("3: карта мира 1710 — нарисован %d квад miniCouplePos (ждём 1)"), Drawn(C, true)));
		if (UAsdaMinimapSubsystem* MM = Sub<UAsdaMinimapSubsystem>(C)) { MM->SetBigMapVisible(false); }
		Feed(C, 5227, Body5227(1, (uint8)C->MapId));
		After(C, 0.4f, &S4);
	}
	// ═══ 4: 5227 — другой канал ═══
	static void S4(FCtxRef C)
	{
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		const FString Want = FString::Printf(TEXT("  2 %s %s"), *Loc(C, 1187), *MapName(C, C->MapId));
		C->Expect(NoMark(C) && Drawn(C, false) == 0 && Sm && Sm->GetPartnerChannelForCheck() == 1 && Txt(C, TEXT("SOUL_TXT05")) == Want,
			FString::Printf(TEXT("4: 5227 канал 2 — отметки нет (другой канал), 6523 «%s»"), *Txt(C, TEXT("SOUL_TXT05"))));
		Feed(C, 5227, Body5227(0, (uint8)C->MapId));
		After(C, 0.4f, &S4b);
	}
	static void S4b(FCtxRef C)
	{
		C->Expect(MarkIs(C, C->Tile, C->MapId) && Drawn(C, false) == 1, TEXT("4: 5227 свой канал — отметка вернулась"));
		Feed(C, 5110, Body5110(C, (C->MapId + 1) % 23, C->Tile));
		After(C, 0.4f, &S5);
	}
	// ═══ 5: другая карта ═══
	static void S5(FCtxRef C)
	{
		C->Expect(MarkIs(C, C->Tile, (C->MapId + 1) % 23) && Drawn(C, false) == 0, TEXT("5: Друг на другой карте — на мини-карте не рисуется"));
		Feed(C, 5110, Body5110(C, C->MapId, C->Tile));
		After(C, 0.4f, &S5b);
	}
	static void S5b(FCtxRef C)
	{
		C->Expect(MarkIs(C, C->Tile, C->MapId) && Drawn(C, false) == 1, TEXT("5: своя карта — снова рисуется"));
		// ═══ 6: сброс сессии MapChanged — подсистема, затем мини-карта (худший порядок: мини-карта чистит ВСЕ отметки последней).
		if (UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C)) { Sm->ResetSessionState(EAsdaSessionResetReason::MapChanged); }
		if (UAsdaMinimapSubsystem* MM = Sub<UAsdaMinimapSubsystem>(C)) { MM->ResetSessionState(EAsdaSessionResetReason::MapChanged); }
		C->Expect(NoMark(C), TEXT("6: сразу после сброса мини-карты отметок нет"));
		After(C, 0.5f, &S6);
	}
	static void S6(FCtxRef C)
	{
		C->Expect(MarkIs(C, C->Tile, C->MapId), TEXT("6: после смены карты отметка Друга восстановлена на следующем кадре"));
		After(C, 0.6f, &S6b);
	}
	static void S6b(FCtxRef C)
	{
		// Окно 6400 после MapChanged у порта пересоздаётся — открываем вкладку заново (путь игрока).
		if (UAsdaCommunitySubsystem* Cm = Sub<UAsdaCommunitySubsystem>(C)) { Cm->OpenOnTab(0); }
		Feed(C, 5226, TArray<uint8>());
		After(C, 0.6f, &S7);
	}
	// ═══ 7: 5226 «Друг уходит» ═══
	static void S7(FCtxRef C)
	{
		C->Expect(Txt(C, TEXT("SOUL_TXT01")) == Loc(C, 8054) && Txt(C, TEXT("SOUL_TXT02")).IsEmpty() && Txt(C, TEXT("SOUL_TXT05")).TrimStartAndEnd().IsEmpty(),
			FString::Printf(TEXT("7: 5226 — 6548 «%s», имя и локация пусты (было «В игре» и старые данные)"), *Txt(C, TEXT("SOUL_TXT01"))));
		C->Expect(Txt(C, TEXT("SOUL_TXT12")) == FString::Printf(TEXT("%d %s "), (int32)kFriendLv, *Loc(C, 12215)), TEXT("7: уровень Дружбы на месте"));
		C->Expect(NoMark(C) && Drawn(C, false) == 0, TEXT("7: отметки Друга нет"));
		Feed(C, 5220, Body5220(C, 0, 1, 1, 0, 0, /*Connect*/2));
		After(C, 0.4f, &S8);
	}
	// ═══ 8: своя одержимость ═══
	static void S8(FCtxRef C)
	{
		UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
		C->Expect(Sm && Sm->IsShareLocked() && Txt(C, TEXT("SOUL_TXT01")).IsEmpty() && Chk(C, 0) && !Chk(C, 0)->GetIsEnabled() && Chk(C, 3) && !Chk(C, 3)->IsChecked(),
			TEXT("8: я одержим (5220 @32 = 2) — статус пуст, галки погашены, 6546 снята"));
		const int32 Was = C->Sent5221.Num();
		C->RefusedWas = Sm ? Sm->GetShareSendRefusedCount() : 0;
		Click(C, 0);
		C->Expect(C->Sent5221.Num() == Was && Sm && Sm->GetShareSendRefusedCount() == C->RefusedWas + 1,
			TEXT("8: клик галки под одержимостью — 5221 НЕ ушёл (флаги хозяина тела)"));
		Feed(C, 5220, Body5220(C, 1, 1, 1, 0, 0));
		Feed(C, 5110, Body5110(C, C->MapId, C->Tile));
		After(C, 0.4f, &S8b);
	}
	// ═══ 9: ключи отката ═══
	static void S8b(FCtxRef C)
	{
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.MapMark"), 0);
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.LocationText"), 0);
		Feed(C, 5110, Body5110(C, C->MapId, C->Tile));
		After(C, 0.4f, &S9);
	}
	static void S9(FCtxRef C)
	{
		const FString Want = FString::Printf(TEXT("  1 %s %s"), *Loc(C, 1187), *MapName(C, C->MapId));
		C->Expect(NoMark(C), TEXT("9: ключ asda.Soulmate.MapMark 0 — отметки нет (прежний порт)"));
		C->Expect(Txt(C, TEXT("SOUL_TXT05")) != Want, FString::Printf(TEXT("9: ключ asda.Soulmate.LocationText 0 — 6523 прежний «%s»"), *Txt(C, TEXT("SOUL_TXT05"))));
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.MapMark"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.LocationText"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.OfflineInfo"), 0);
		Feed(C, 5226, TArray<uint8>());
		After(C, 0.4f, &S9b);
	}
	static void S9b(FCtxRef C)
	{
		C->Expect(Txt(C, TEXT("SOUL_TXT01")) == Loc(C, 8053), TEXT("9: ключ asda.Soulmate.OfflineInfo 0 — 5226 даёт прежнее «В игре» (доказывает откат)"));
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.OfflineInfo"), 1);
		Finish(C);
	}

	static void Finish(FCtxRef C)
	{
		UWorld* W = C->W.Get();
		{
			TArray<uint8> B = Body5220(C, 0, 0, 0, 0, 0);
			AsdaBytes::WrI32(&B[6], -1);
			Feed(C, 5220, B);   // «слота нет» — снять признак у подсистемы навыков (канал OnSoulmateState)
		}
		if (UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C)) { Sm->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
		if (UAsdaCommunitySubsystem* Cm = Sub<UAsdaCommunitySubsystem>(C)) { Cm->CloseWindow(); }
		if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C)) { Net->CurrentChannel = C->ChannelWas; Net->CharList = C->CharListWas; }
		if (C->bCap) { if (UAsdaPetSubsystem* Pet = Sub<UAsdaPetSubsystem>(C)) { Pet->EndSendCapture(); } }
		C->NetScope.Reset();
		AsdaSelfTestKit::Finish(W, kCmd, C->bQuit, LogAsdaSoulInfoTest, TEXT("СВЕДЕНИЯ-ДРУГА"), C->Ok, C->Fail);
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(A);
		// Свой персонаж нужен мини-карте (позиция, карта) — офлайн его нет: штатный фейковый спавн и повтор через 2 с.
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
		UAsdaSoulmateSubsystem* Sm = GI ? GI->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr;
		UAsdaCommunitySubsystem* Cm = GI ? GI->GetSubsystem<UAsdaCommunitySubsystem>() : nullptr;
		UAsdaMinimapSubsystem* MM = GI ? GI->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr;
		UAsdaPetSubsystem* Pet = GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr;
		if (!Net || !Sm || !Cm || !MM || !Pet) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем")); return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W; C->bQuit = KA.bQuit;
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		C->NetScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		Net->SelectedCharId = kMe; Net->SelfUserNum = kMyUserNum; Net->bSelfUserNumKnown = true;
		C->ChannelWas = Net->CurrentChannel; Net->CurrentChannel = 0;
		C->CharListWas = Net->CharList;
		// Список персонажей с ДРУГИМИ именами — доказывает, что строки share берутся из 5220.
		Net->CharList.SetNum(3);
		const TCHAR* Names[3] = { TEXT("CharA"), TEXT("CharB"), TEXT("CharC") };
		for (int32 i = 0; i < 3; ++i) { Net->CharList[i] = FAsdaCharRecord(); Net->CharList[i].Name = Names[i]; Net->CharList[i].Slot = (uint8)i; }
		for (const TCHAR* K : { TEXT("asda.Soulmate.PopFrom5220"), TEXT("asda.Soulmate.OfflineInfo"), TEXT("asda.Soulmate.LocationText"),
		                        TEXT("asda.Soulmate.MapMark"), TEXT("asda.Soulmate.ShareLockPossessed"), TEXT("asda.Soulmate.ShareSendNeedsServer"),
		                        TEXT("asda.Soulmate.ShareNamesFrom5220"), TEXT("asda.Soulmate.Recv5227"), TEXT("asda.Soulmate.ShareBlockVisible") })
		{
			AsdaCVarSave::SetForTest(K, 1);
		}
		TWeakPtr<FCtx> WC(C);
		C->bCap = Pet->BeginSendCapture([WC](int32 Op, const TArray<uint8>& Body, bool, TCHAR)
		{
			if (Op != 5221) { return; }
			if (TSharedPtr<FCtx> P = WC.Pin()) { P->Sent5221.Add(Body); }
		}, 0);
		C->Expect(C->bCap, TEXT("подготовка: перехват исходящих взведён (в сеть ничего не уходит)"));
		// Опыт Дружбы — середина уровня по таблице оригинала (доля = (B − (A − exp)) / B = 50 %), а не выдуманное число.
		if (UAsdaItemTableSubsystem* It = GI->GetSubsystem<UAsdaItemTableSubsystem>())
		{
			if (UDataTable* DT = It->GetDataTable(FName(TEXT("smcharexp"))))
			{
				if (const FAsdaSmcharexpRow* Row = DT->FindRow<FAsdaSmcharexpRow>(FName(*FString::FromInt(kFriendLv)), TEXT("selftest"), false))
				{
					C->Exp = (int64)Row->CumExp - (int64)Row->ExpToNext / 2;
				}
			}
		}
		C->Expect(C->Exp > 0 && C->Exp < MAX_int32, FString::Printf(TEXT("подготовка: опыт Дружбы %lld (середина уровня %d по DT_smcharexp)"), C->Exp, (int32)kFriendLv));
		C->MapId = MM->GetDrawnMapId();
		if (const UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>())
		{
			if (const AAsdaPlayerCharacter* P = Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor))
			{
				const FVector2D Pos = P->GetGamePosXZ();
				C->Tile = FIntPoint(FMath::FloorToInt(Pos.X / 100.f) + 4, FMath::FloorToInt(Pos.Y / 100.f) + 3);   // рядом с игроком (в окне мини-карты)
			}
		}
		C->Expect(C->MapId >= 0, FString::Printf(TEXT("подготовка: мини-карта рисует карту %d, Друг в тайле (%d,%d)"), C->MapId, C->Tile.X, C->Tile.Y));
		Sm->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		Cm->OpenOnTab(0);
		After(C, 1.0f, &S1);
	}
}

// Консольная команда самотеста.
static FAutoConsoleCommandWithWorldAndArgs GAsdaSoulSelfTestInfoShareMark(
	TEXT("asda.Soul.SelfTestInfoShareMark"),
	TEXT("Самотест вкладки соулмейта: сведения при Друге вне игры (5220/5226), флаги share (5221 только из состояния сервера, замок ")
	TEXT("одержимости), 6523 «Локация», 5227, розовая точка Друга на мини-карте и карте мира, ключи отката. Офлайн, на карте мира. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W) { AsdaSoulInfoTest::Run(A, W); }));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
