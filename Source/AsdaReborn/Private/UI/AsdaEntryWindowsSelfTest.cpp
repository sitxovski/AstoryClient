// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ОКНА ПРИ СВОЁМ ВХОДЕ/ВОСКРЕШЕНИИ И СМЕНЕ КАРТЫ» (22.10.2026): какие окна эталон закрывает своим 4055/4056 и
//   загрузкой карты, а какие держит. Разбор — итог агента party-hud (координатору), эталон:
//     4056 своё  — `GameNet_OnLocalPlayerMapEntry@0xF43390` (game_main.c:12871-13034);
//     4055 своё  — `netHandler_EnterWorld@0xF43050` (network.c:42639-42690) + загрузка карты (game_main.c:3540-3725);
//   окна инвентаря/персонажа/навыков/квестов/сообщества в этих списках НЕТ — у оригинала они остаются открытыми.
//   Команды (офлайн, `-AsdaNoNet`, карта мира):
//     `asda.SelfTest.EntryWindows [legacy] [quit]` — воскрешение 4056 на той же карте и 4055 без смены карты;
//     `asda.SelfTest.EntryWindowsTravel [legacy] [quit]` — НАСТОЯЩИЙ травел UE на ту же карту (виджеты сносит движок),
//        затем вход 4003 (`Asda.World.FakeSelfSpawn`, та же точка, что SpawnSelf) — окна, которые эталон держит, на месте.
//   `legacy` — исправление выключено ключом `asda.UI.EntryWindowsEtalon 0` (прежнее `CloseAll`): самотест обязан дать FAIL.
//   Шаги идут на тикере ядра (`FTSTicker`) — он переживает травел, таймеры мира — нет.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaCharaInfoSubsystem.h"
#include "UI/AsdaSkillWindowSubsystem.h"
#include "UI/AsdaCommunitySubsystem.h"
#include "Quest/AsdaQuestLogSubsystem.h"
#include "UI/AsdaBoosterShopSubsystem.h"
#include "UI/AsdaPremiumShopSubsystem.h"
#include "UI/AsdaHairShopSubsystem.h"
#include "UI/AsdaDisassemblySubsystem.h"     // слот 6: разборка 32000
#include "UI/AsdaPetCageSubsystem.h"         // слот 9: клетка питомца 43000
#include "UI/AsdaItemUpgradeSubsystem.h"     // слот 12: улучшение снаряжения 20000
#include "UI/AsdaPartySubsystem.h"           // висящая заявка в отряд (автоотказ 5092 при загрузке)
#include "UI/AsdaSoulmateSkillSubsystem.h"   // вопрос зова друга (1, 38) → 5237 код 2
#include "Login/AsdaLoginData.h"             // EncodeCp1251 — имя в теле 5091
#include "World/AsdaLoadManagerSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaEntryWinTest, Log, All);

namespace AsdaEntryWinTest
{
	static constexpr uint16 kMyUserNum = 321;   // свой userNum на прогон (как у соседних самотестов)

	// ★ R5-fix-b 13.11.2026 (заключительный заход, четвёртая запись): исходное состояние прогона (окна и «свой») — снимок до первого шага, возврат после последнего.
	//   Прежде прогон оставлял окно разборки 32000, сумку и userNum 321 — сосед по пачке `Trade.SelfTestGates` падал
	//   на гейте «рюкзак» (35/4, одиночно 39/0; `r5/fix/c/REPORT6.md` §8).
	struct FRun;
	struct FState;
	static void CaptureStart(FRun& R);
	static void RestoreEnd(FRun& R);

	// Шаги на тикере ядра: (задержка, действие). Мир берётся заново на каждом шаге — травел его меняет.
	struct FRun : public TSharedFromThis<FRun>
	{
		TWeakObjectPtr<UGameInstance> GI;
		FString Cmd;
		bool bQuit = false;
		int32 Ok = 0, Fail = 0;
		TArray<TPair<float, TFunction<void(FRun&)>>> Steps;
		int32 Next = 0;
		double DueAt = 0.0;
		FTSTicker::FDelegateHandle H;
		TSharedPtr<FState> Before;                  // ★ R5-fix-b 13.11.2026 (заключительный заход, четвёртая запись): окна до прогона
		TSharedPtr<FAsdaNetSelfScope> NetScope;     // «свой» до прогона (номер, известность, charId, уровень, вес, смерть)
		int32 ClassWas = -1;                        // класс своего до прогона (`SetSelf` ставит 1, если не было)
		bool bClassSaved = false;                   // класс запомнен

		UWorld* W() const { return GI.IsValid() ? GI->GetWorld() : nullptr; }
		template <class T> T* Sub() const { return GI.IsValid() ? GI->GetSubsystem<T>() : nullptr; }
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaEntryWinTest, Log,   TEXT("ОКНА-ВХОД ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaEntryWinTest, Error, TEXT("ОКНА-ВХОД FAIL %s"), *What); }
		}
		void Add(float Delay, TFunction<void(FRun&)> Fn) { Steps.Emplace(Delay, MoveTemp(Fn)); }
		void Start()
		{
			CaptureStart(*this);   // ★ R5-fix-b 13.11.2026 (заключительный заход, четвёртая запись)
			DueAt = FPlatformTime::Seconds() + (Steps.Num() ? Steps[0].Key : 0.f);
			TSharedRef<FRun> Self = AsShared();
			H = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self](float) { return Self->TickOnce(); }));
		}
		bool TickOnce()
		{
			if (FPlatformTime::Seconds() < DueAt) { return true; }
			if (Next >= Steps.Num()) { return false; }
			TFunction<void(FRun&)> Fn = Steps[Next].Value;
			++Next;
			Fn(*this);
			if (Next >= Steps.Num())
			{
				RestoreEnd(*this);   // ★ R5-fix-b 13.11.2026 (заключительный заход, четвёртая запись): убрать за собой до итога
				AsdaSelfTestKit::Finish(W(), *Cmd, bQuit, LogAsdaEntryWinTest, TEXT("ОКНА-ВХОД"), Ok, Fail);
				return false;
			}
			DueAt = FPlatformTime::Seconds() + Steps[Next].Key;
			return true;
		}
	};

	// Окно на экране так, как его видит игрок: флаг владельца + живой виджет СВОЕГО мира во вьюпорте + видимость в реестре.
	static bool Live(FRun& R, FName Id)
	{
		UAsdaWindowSubsystem* WM = R.Sub<UAsdaWindowSubsystem>();
		UUserWidget* Wd = WM ? WM->FindWindow(Id) : nullptr;
		return Wd && WM->IsWindowLive(Wd) && WM->IsWindowVisible(Id);
	}
	struct FState
	{
		bool Inv = false, Chara = false, Skill = false, Quest = false, Comm = false;      // эталон держит
		bool Dis = false, Cage = false, Upg = false;                                       // эталон держит (слоты 6/9/12)
		bool Booster = false, Elite = false, Hair = false;                                 // эталон закрывает
		FString Flags;   // флаги «открыто» владельцев и живость виджетов по отдельности (диагностика)
		FString Str() const
		{
			return FString::Printf(TEXT("инвентарь=%d персонаж=%d навыки=%d квесты=%d сообщество=%d разборка32000=%d клетка43000=%d улучшение20000=%d | бустеры15300=%d элитный6000=%d парикмахерская=%d [%s]"),
				Inv, Chara, Skill, Quest, Comm, Dis, Cage, Upg, Booster, Elite, Hair, *Flags);
		}
	};
	static FState Snap(FRun& R)
	{
		FState S;
		const UAsdaInventorySubsystem* Inv = R.Sub<UAsdaInventorySubsystem>();
		const UAsdaCharaInfoSubsystem* Ci = R.Sub<UAsdaCharaInfoSubsystem>();
		const UAsdaSkillWindowSubsystem* Sk = R.Sub<UAsdaSkillWindowSubsystem>();
		const UAsdaQuestLogSubsystem* Ql = R.Sub<UAsdaQuestLogSubsystem>();
		const UAsdaCommunitySubsystem* Cm = R.Sub<UAsdaCommunitySubsystem>();
		const UAsdaBoosterShopSubsystem* Bs = R.Sub<UAsdaBoosterShopSubsystem>();
		const UAsdaPremiumShopSubsystem* Ps = R.Sub<UAsdaPremiumShopSubsystem>();
		const UAsdaHairShopSubsystem* Hs = R.Sub<UAsdaHairShopSubsystem>();
		S.Inv   = Inv && Inv->IsWindowOpen() && Live(R, FName(TEXT("INVEN_WIN")));
		S.Chara = Ci && Ci->IsWindowOpen() && Live(R, FName(TEXT("CHARAINFO_WIN")));
		S.Skill = Sk && Sk->IsWindowOpen() && Live(R, FName(TEXT("SKILL_WIN")));
		S.Quest = Ql && Ql->IsOpen() && Live(R, FName(TEXT("QUEST_WIN")));
		S.Comm  = Cm && Cm->IsWindowOpen() && Live(R, FName(TEXT("COMMUNITY_WIN")));
		const UAsdaDisassemblySubsystem* Ds = R.Sub<UAsdaDisassemblySubsystem>();
		const UAsdaPetCageSubsystem* Pc = R.Sub<UAsdaPetCageSubsystem>();
		const UAsdaItemUpgradeSubsystem* Up = R.Sub<UAsdaItemUpgradeSubsystem>();
		S.Dis  = Ds && Ds->IsOpen() && Live(R, FName(TEXT("Disassembly_Win")));
		S.Cage = Pc && Pc->IsCageOpen();   // своего id у окна 43000 в реестре нет — по флагу владельца
		S.Upg  = Up && Up->IsOpen() && Live(R, FName(TEXT("Item_Upgrade_Win")));
		S.Booster = (Bs && Bs->IsOpen()) || Live(R, FName(TEXT("BOOSTER_SHOP_WIN")));
		S.Elite   = (Ps && Ps->IsOpen()) || Live(R, FName(TEXT("SHELLITEM_STORE_WIN")));
		S.Hair    = (Hs && Hs->IsSessionOpen()) || Live(R, FName(TEXT("HAIRSHOP_WIN")));
		S.Flags = FString::Printf(TEXT("флаг/живо: инв %d/%d перс %d/%d нав %d/%d кв %d/%d сооб %d/%d"),
			Inv && Inv->IsWindowOpen(), Live(R, FName(TEXT("INVEN_WIN"))), Ci && Ci->IsWindowOpen(), Live(R, FName(TEXT("CHARAINFO_WIN"))),
			Sk && Sk->IsWindowOpen(), Live(R, FName(TEXT("SKILL_WIN"))), Ql && Ql->IsOpen(), Live(R, FName(TEXT("QUEST_WIN"))),
			Cm && Cm->IsWindowOpen(), Live(R, FName(TEXT("COMMUNITY_WIN"))));
		return S;
	}
	static bool KeptAll(const FState& S) { return S.Inv && S.Chara && S.Skill && S.Quest && S.Comm && S.Dis && S.Cage && S.Upg; }
	static bool ClosedAll(const FState& S) { return !S.Booster && !S.Elite && !S.Hair; }

	// Открыть держимые окна игровыми функциями. Окно с «открыто» при мёртвом виджете (наследие прежнего прогона пакета в
	//   режиме `legacy`, где `CloseAll` снёс виджет мимо владельца) сперва закрывается владельцем — иначе «открыть» молчит.
	static void OpenKept(FRun& R)
	{
		if (UAsdaInventorySubsystem* Inv = R.Sub<UAsdaInventorySubsystem>())
		{ if (Inv->IsWindowOpen() && !Live(R, FName(TEXT("INVEN_WIN")))) { Inv->CloseWindow(); } if (!Inv->IsWindowOpen()) { Inv->OpenWindow(); } }
		if (UAsdaCharaInfoSubsystem* Ci = R.Sub<UAsdaCharaInfoSubsystem>())
		{ if (Ci->IsWindowOpen() && !Live(R, FName(TEXT("CHARAINFO_WIN")))) { Ci->CloseWindow(); } if (!Ci->IsWindowOpen()) { Ci->OpenWindow(); } }
		if (UAsdaSkillWindowSubsystem* Sk = R.Sub<UAsdaSkillWindowSubsystem>())
		{ if (Sk->IsWindowOpen() && !Live(R, FName(TEXT("SKILL_WIN")))) { Sk->CloseWindow(); } if (!Sk->IsWindowOpen()) { Sk->OpenWindow(); } }
		if (UAsdaQuestLogSubsystem* Ql = R.Sub<UAsdaQuestLogSubsystem>())
		{ if (Ql->IsOpen() && !Live(R, FName(TEXT("QUEST_WIN")))) { Ql->Close(); } if (!Ql->IsOpen()) { Ql->Open(); } }
		if (UAsdaCommunitySubsystem* Cm = R.Sub<UAsdaCommunitySubsystem>())
		{ if (Cm->IsWindowOpen() && !Live(R, FName(TEXT("COMMUNITY_WIN")))) { Cm->CloseWindow(); } if (!Cm->IsWindowOpen()) { Cm->OpenOnTab(0); /* ★ R5-fix6-c (13.11.2026): вкладку 0 ставит сам тест штатным путём (хоткей 35), а не берёт оставшуюся от соседа пачки */ } }
		// Слоты 6/9/12 — тумблеры владельцев: открываем только закрытое.
		if (UAsdaDisassemblySubsystem* Ds = R.Sub<UAsdaDisassemblySubsystem>())
		{ if (Ds->IsOpen() && !Live(R, FName(TEXT("Disassembly_Win")))) { Ds->CloseWindow(); } if (!Ds->IsOpen()) { Ds->Toggle(TEXT("самотест EntryWindows")); } }
		if (UAsdaPetCageSubsystem* Pc = R.Sub<UAsdaPetCageSubsystem>()) { if (!Pc->IsCageOpen()) { Pc->OpenCage(); } }
		if (UAsdaItemUpgradeSubsystem* Up = R.Sub<UAsdaItemUpgradeSubsystem>())
		{ if (Up->IsOpen() && !Live(R, FName(TEXT("Item_Upgrade_Win")))) { Up->ToggleWindow(); } if (!Up->IsOpen()) { Up->OpenWindow(); } }
	}
	// Закрываемые эталоном окна. Элитный магазин 6000 открывается ОТДЕЛЬНО: его вход сам зовёт `uiCloseAllWindows`
	//   (`charBeginAutoMove`, entity_char.c:45056-45059 → `CloseAllPlayerWindows`) и прятал бы держимые окна.
	static void OpenClosable(FRun& R, bool bElite)
	{
		if (bElite)
		{
			// Вход 6000 отклоняют открытые разборка/улучшение/клетка (гейты входа магазина) — закрыть их владельцами,
			//   держимые окна вернёт снимок `uiCaptureOpenWindowsState`/восстановление при закрытии магазина.
			if (UAsdaDisassemblySubsystem* Ds = R.Sub<UAsdaDisassemblySubsystem>()) { if (Ds->IsOpen()) { Ds->CloseWindow(); } }
			if (UAsdaItemUpgradeSubsystem* Up = R.Sub<UAsdaItemUpgradeSubsystem>()) { if (Up->IsOpen()) { Up->ToggleWindow(); } }
			if (UAsdaPetCageSubsystem* Pc = R.Sub<UAsdaPetCageSubsystem>())         { if (Pc->IsCageOpen()) { Pc->CloseCage(); } }
			if (UAsdaPremiumShopSubsystem* Ps = R.Sub<UAsdaPremiumShopSubsystem>()) { if (!Ps->IsOpen()) { Ps->OpenWindow(); } }
			return;
		}
		if (UAsdaBoosterShopSubsystem* Bs = R.Sub<UAsdaBoosterShopSubsystem>()) { if (!Bs->IsOpen()) { Bs->OpenWindow(); } }
		if (UAsdaHairShopSubsystem* Hs = R.Sub<UAsdaHairShopSubsystem>())       { if (!Hs->IsSessionOpen()) { Hs->OpenWindow(); } }
	}
	// ★ R5-fix-b 13.11.2026 (заключительный заход, четвёртая запись): снимок до первого шага — какие окна открыты, «свой» и класс.
	static void CaptureStart(FRun& R)
	{
		R.Before = MakeShared<FState>(Snap(R));
		if (UAsdaNetSubsystem* Net = R.Sub<UAsdaNetSubsystem>())
		{
			R.NetScope = MakeShared<FAsdaNetSelfScope>(Net, *R.Cmd);
			R.ClassWas = Net->SelfClassId;
			R.bClassSaved = true;
		}
	}
	// ★ R5-fix-b 13.11.2026 (заключительный заход, четвёртая запись): после последнего шага — окна, которых до прогона не было, закрыть владельцами; держимые, которые были, —
	//   вернуть (сообщество — только закрыть: вкладку при открытии ставит сам тест); затем класс и «свой» (`FAsdaNetSelfScope`).
	static void RestoreEnd(FRun& R)
	{
		if (R.Before.IsValid())
		{
			const FState B = *R.Before;
			// ★ R5-fix8-b 14.11.2026 (вторая запись, ревизия REVIEW8) (МЕЛК-4): «вернуть держимое» смотрит живое состояние владельца (после закрытия зависимых окон).
			// ★ R5-fix-b 13.11.2026 (заключительный заход, пятая запись): сначала окна, которые при закрытии возвращают сумку (разборка 32000 по эталону открывает её
			//   снова — «сумка снова открыта [222]»), и магазины; потом держимые. Иначе сумка оставалась открытой.
			if (UAsdaDisassemblySubsystem* Ds = R.Sub<UAsdaDisassemblySubsystem>()) { if (!B.Dis && Ds->IsOpen()) { Ds->CloseWindow(); } else if (B.Dis && !Ds->IsOpen()) { Ds->Toggle(TEXT("самотест EntryWindows: возврат")); } }
			if (UAsdaPetCageSubsystem* Pc = R.Sub<UAsdaPetCageSubsystem>()) { if (!B.Cage && Pc->IsCageOpen()) { Pc->CloseCage(); } else if (B.Cage && !Pc->IsCageOpen()) { Pc->OpenCage(); } }
			if (UAsdaItemUpgradeSubsystem* Up = R.Sub<UAsdaItemUpgradeSubsystem>()) { if (!B.Upg && Up->IsOpen()) { Up->ToggleWindow(); } else if (B.Upg && !Up->IsOpen()) { Up->OpenWindow(); } }
			if (UAsdaBoosterShopSubsystem* Bs = R.Sub<UAsdaBoosterShopSubsystem>()) { if (!B.Booster && Bs->IsOpen()) { Bs->CloseWindow(); } }
			if (UAsdaPremiumShopSubsystem* Ps = R.Sub<UAsdaPremiumShopSubsystem>()) { if (!B.Elite && Ps->IsOpen()) { Ps->CloseWindow(); } }
			if (UAsdaHairShopSubsystem* Hs = R.Sub<UAsdaHairShopSubsystem>()) { if (!B.Hair && Hs->IsSessionOpen()) { Hs->CloseWindow(); } }
			if (UAsdaInventorySubsystem* Inv = R.Sub<UAsdaInventorySubsystem>()) { if (!B.Inv && Inv->IsWindowOpen()) { Inv->CloseWindow(); } else if (B.Inv && !Inv->IsWindowOpen()) { Inv->OpenWindow(); } }
			if (UAsdaCharaInfoSubsystem* Ci = R.Sub<UAsdaCharaInfoSubsystem>()) { if (!B.Chara && Ci->IsWindowOpen()) { Ci->CloseWindow(); } else if (B.Chara && !Ci->IsWindowOpen()) { Ci->OpenWindow(); } }
			if (UAsdaSkillWindowSubsystem* Sk = R.Sub<UAsdaSkillWindowSubsystem>()) { if (!B.Skill && Sk->IsWindowOpen()) { Sk->CloseWindow(); } else if (B.Skill && !Sk->IsWindowOpen()) { Sk->OpenWindow(); } }
			if (UAsdaQuestLogSubsystem* Ql = R.Sub<UAsdaQuestLogSubsystem>()) { if (!B.Quest && Ql->IsOpen()) { Ql->Close(); } else if (B.Quest && !Ql->IsOpen()) { Ql->Open(); } }
			if (UAsdaCommunitySubsystem* Cm = R.Sub<UAsdaCommunitySubsystem>()) { if (!B.Comm && Cm->IsWindowOpen()) { Cm->CloseWindow(); } }
			UE_LOG(LogAsdaEntryWinTest, Log, TEXT("ОКНА-ВХОД уборка: до прогона %s; после уборки %s"), *B.Str(), *Snap(R).Str());
			R.Before.Reset();
		}
		if (R.bClassSaved) { if (UAsdaNetSubsystem* Net = R.Sub<UAsdaNetSubsystem>()) { Net->SelfClassId = R.ClassWas; } R.bClassSaved = false; }
		R.NetScope.Reset();   // возврат «своего» (`FAsdaNetSelfScope::Restore`)
	}

	// Свой номер и класс (книга навыков без класса не открывается: гейт `skillwin_OpenSkillWindow` «профессия не выбрана»).
	static void SetSelf(FRun& R)
	{
		if (UAsdaNetSubsystem* Net = R.Sub<UAsdaNetSubsystem>())
		{
			Net->SetSelfUserNumForDebug(kMyUserNum);
			if (Net->SelfClassId <= 0) { Net->SelfClassId = 1; }
		}
	}
	// 4056 своё (22 б): uidx@0, тайл @2/@4, HP i32@6, MP i16@10, опыт @12, state @20 (0 — живым).
	static TArray<uint8> Body4056(int32 Tx, int32 Tz)
	{
		TArray<uint8> B; B.SetNumZeroed(22);
		AsdaBytes::WrI16(&B[0], kMyUserNum); AsdaBytes::WrI16(&B[2], Tx); AsdaBytes::WrI16(&B[4], Tz);
		AsdaBytes::WrI32(&B[6], 500); AsdaBytes::WrI16(&B[10], 100);
		return B;
	}
	// 4055 своё без смены карты (43 б): uidx@0, карта u8@2 (= текущая), IP@3..22 пусто, порт@23, тайл @25/@27, HP@29, MP@33, опыт@35.
	static TArray<uint8> Body4055SameMap(int32 MapId, int32 Tx, int32 Tz)
	{
		TArray<uint8> B; B.SetNumZeroed(43);
		AsdaBytes::WrI16(&B[0], kMyUserNum); B[2] = (uint8)MapId;
		AsdaBytes::WrI16(&B[25], Tx); AsdaBytes::WrI16(&B[27], Tz);
		AsdaBytes::WrI32(&B[29], 500); AsdaBytes::WrI16(&B[33], 100);
		return B;
	}
	static void Feed(FRun& R, int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Net = R.Sub<UAsdaNetSubsystem>()) { Net->DebugFeedPacket(Op, B); } }
	// 5055 код 1 (53 б, `Recv_PortalChannelChange`): @0 код, @1..20 IP (пусто — тот же), @21 порт, @23 карта u16, @25/@27 тайл.
	static TArray<uint8> Body5055(int32 MapId, int32 Tx, int32 Tz)
	{
		TArray<uint8> B; B.SetNumZeroed(53);
		B[0] = 1;
		AsdaBytes::WrI16(&B[23], MapId); AsdaBytes::WrI16(&B[25], Tx); AsdaBytes::WrI16(&B[27], Tz);
		B[51] = 0x00; B[52] = 0x00;
		return B;
	}
	// 5091 входящая заявка «новый отряд» (30 б): код@0, userNum@1, charId@3, имя@7, правило@27, reqId@28.
	static TArray<uint8> Body5091()
	{
		TArray<uint8> B; B.SetNumZeroed(30);
		B[0] = 0;   // код 0 = заявка (≠0 — событие/отказ, `Party_DispatchEventNotice`)
		AsdaBytes::WrI16(&B[1], 322); AsdaBytes::WrI32(&B[3], 2002);
		FAsdaLoginData::EncodeCp1251(TEXT("Inviter"), &B[7], 19);
		B[27] = 0; AsdaBytes::WrI16(&B[28], 42);
		return B;
	}
	// 5236 зов друга (42 б): @0 charId инициатора, @4 userNum, @7 карта, @9 канал, @36 userNum ЦЕЛИ (мой), @39 карта цели.
	static TArray<uint8> Body5236()
	{
		TArray<uint8> B; B.SetNumZeroed(42);
		AsdaBytes::WrI32(&B[0], 2002); AsdaBytes::WrI16(&B[4], 322); B[7] = 3; B[9] = 1;
		AsdaBytes::WrI16(&B[36], kMyUserNum); B[39] = 3; B[41] = 1;
		return B;
	}

	// Старт: свой павн (офлайн — штатный фейковый спавн), затем сценарий. true — запущено.
	static TSharedRef<FRun> Make(UWorld* W, const TCHAR* Cmd, const FAsdaSelfTestArgs& KA)
	{
		TSharedRef<FRun> R = MakeShared<FRun>();
		R->GI = W ? W->GetGameInstance() : nullptr;
		R->Cmd = Cmd; R->bQuit = KA.bQuit;
		AsdaCVarSave::ArmWorldGuard(W, Cmd);
		if (KA.Has(TEXT("legacy")))
		{
			UE_LOG(LogAsdaEntryWinTest, Warning, TEXT("ОКНА-ВХОД режим legacy: asda.UI.EntryWindowsEtalon 0 (прежнее поведение — CloseAll)"));
			AsdaCVarSave::SetForTest(TEXT("asda.UI.EntryWindowsEtalon"), 0);
		}
		return R;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestEntryWindows(
	TEXT("asda.SelfTest.EntryWindows"),
	TEXT("Самотест окон при своём 4056 (воскрешение на той же карте) и 4055 без смены карты: инвентарь/персонаж/навыки/квесты/"
	     "сообщество остаются, магазин бустеров 15300 / элитный 6000 / парикмахерская закрываются (эталон). `legacy`, `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaEntryWinTest;
		static const TCHAR* kCmd = TEXT("asda.SelfTest.EntryWindows");
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s selfready%s%s"), kCmd, KA.Has(TEXT("legacy")) ? TEXT(" legacy") : TEXT(""), KA.bQuit ? TEXT(" quit") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle Hd;
			W->GetTimerManager().SetTimer(Hd, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		TSharedRef<FRun> R = Make(W, kCmd, KA);
		R->Add(0.f, [](FRun& R) { SetSelf(R); OpenKept(R); OpenClosable(R, false); });
		R->Add(0.8f, [](FRun& R)
		{
			const FState S = Snap(R);
			R.Check(KeptAll(S) && S.Booster, FString::Printf(TEXT("0 открыты окна: %s"), *S.Str()));
			Feed(R, 4056, Body4056(256, 256));   // своё воскрешение на той же карте (сервер CharRegenVillageMove/SelfPosition)
		});
		R->Add(0.8f, [](FRun& R)
		{
			const FState S = Snap(R);
			R.Check(KeptAll(S), FString::Printf(TEXT("A1 своё 4056: инвентарь/персонаж/навыки/квесты/сообщество ОСТАЛИСЬ (эталон их не закрывает): %s"), *S.Str()));
			R.Check(ClosedAll(S), FString::Printf(TEXT("A2 своё 4056: бустеры 15300 / парикмахерская ЗАКРЫТЫ (uiStorageWindow_Close+uiCloseItemComposeDialog, uiLeaveAndResetAllWindows): %s"), *S.Str()));
			OpenClosable(R, true);   // элитный 6000 — отдельно
		});
		R->Add(0.8f, [](FRun& R)
		{
			const FState S = Snap(R);
			R.Check(S.Elite, FString::Printf(TEXT("A3 элитный магазин 6000 открыт: %s"), *S.Str()));
			Feed(R, 4056, Body4056(256, 256));
		});
		R->Add(0.8f, [](FRun& R)
		{
			const FState S = Snap(R);
			R.Check(!S.Elite, FString::Printf(TEXT("A4 своё 4056: элитный 6000 ЗАКРЫТ (uiInventoryResetAndLeave): %s"), *S.Str()));
			OpenKept(R); OpenClosable(R, false);
		});
		R->Add(0.8f, [](FRun& R)
		{
			const UAsdaNetSubsystem* Net = R.Sub<UAsdaNetSubsystem>();
			const UAsdaLoadManagerSubsystem* LM = R.Sub<UAsdaLoadManagerSubsystem>();
			const int32 Map = LM ? LM->CurrentMapId : (Net ? Net->EnterMapId : 3);
			Feed(R, 4055, Body4055SameMap(Net ? Net->EnterMapId : Map, 256, 256));   // 4055 той же карты — без хендоффа
		});
		R->Add(0.8f, [](FRun& R)
		{
			const FState S = Snap(R);
			R.Check(KeptAll(S), FString::Printf(TEXT("B1 своё 4055 без смены карты: окна, которые эталон держит, ОСТАЛИСЬ: %s"), *S.Str()));
			R.Check(ClosedAll(S), FString::Printf(TEXT("B2 своё 4055: окна из списка загрузки карты (шаг 11, game_main.c:3711-3724) ЗАКРЫТЫ: %s"), *S.Str()));
			OpenKept(R); OpenClosable(R, false);
		});
		R->Add(0.8f, [](FRun& R)
		{
			UAsdaNetSubsystem* Net = R.Sub<UAsdaNetSubsystem>();
			const FState S0 = Snap(R);
			R.Check(S0.Booster && S0.Hair, FString::Printf(TEXT("C0 перед порталом той же карты открыты 15300 и парикмахерская: %s"), *S0.Str()));
			// Портал 5055 на ТУ ЖЕ карту: эталон `Recv_PortalChannelChange` всё равно грузит карту (network.c:43075-43080).
			Feed(R, 5055, Body5055(Net ? Net->EnterMapId : 3, 256, 256));
		});
		R->Add(0.8f, [](FRun& R)
		{
			const FState S = Snap(R);
			R.Check(KeptAll(S), FString::Printf(TEXT("C1 портал 5055 на ту же карту: держимые окна ОСТАЛИСЬ: %s"), *S.Str()));
			R.Check(ClosedAll(S), FString::Printf(TEXT("C2 портал 5055 на ту же карту: 15300 / парикмахерская ЗАКРЫТЫ (шаг 11 загрузки): %s"), *S.Str()));
		});
		R->Start();
	}));

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestEntryWindowsTravel(
	TEXT("asda.SelfTest.EntryWindowsTravel"),
	TEXT("Самотест окон при НАСТОЯЩЕМ травеле на карту (виджеты сносит UE) и входе 4003: окна, которые эталон держит, на месте, "
	     "закрываемые загрузкой — закрыты. `legacy`, `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaEntryWinTest;
		static const TCHAR* kCmd = TEXT("asda.SelfTest.EntryWindowsTravel");
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s selfready%s%s"), kCmd, KA.Has(TEXT("legacy")) ? TEXT(" legacy") : TEXT(""), KA.bQuit ? TEXT(" quit") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle Hd;
			W->GetTimerManager().SetTimer(Hd, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		TSharedRef<FRun> R = Make(W, kCmd, KA);
		R->Add(0.f, [](FRun& R) { SetSelf(R); OpenKept(R); OpenClosable(R, false); });
		R->Add(0.8f, [](FRun& R)
		{
			const FState S = Snap(R);
			R.Check(KeptAll(S) && S.Booster, FString::Printf(TEXT("0 открыты окна до травела: %s"), *S.Str()));
			// Настоящий травел UE на ту же карту: движок сносит виджеты мира, `PostLoadMap` рассылает «смену карты».
			if (UWorld* Wd = R.W()) { UGameplayStatics::OpenLevel(Wd, FName(TEXT("/Game/Map/alpen/alpen")), true, TEXT("game=/Script/AsdaReborn.AsdaWorldGameMode")); }
		});
		R->Add(6.f, [](FRun& R)
		{
			// Вход на новую карту: 4003 SpawnSelf (офлайн — тот же путь `Asda.World.FakeSelfSpawn` → `RevealWorld`).
			if (UWorld* Wd = R.W()) { if (GEngine) { GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3")); GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256")); } }
			SetSelf(R);
		});
		R->Add(2.5f, [](FRun& R)
		{
			const FState S = Snap(R);
			R.Check(KeptAll(S), FString::Printf(TEXT("T1 травел + вход 4003: инвентарь/персонаж/навыки/квесты/сообщество снова на экране (у эталона окна не закрываются): %s"), *S.Str()));
			R.Check(ClosedAll(S), FString::Printf(TEXT("T2 травел: бустеры 15300 / элитный 6000 / парикмахерская закрыты (загрузка, шаг 11): %s"), *S.Str()));
		});
		R->Start();
	}));


// ★ ВАЖНО-3 ревизии: путь «переход 5055 на ДРУГУЮ карту → окна загрузки». Висящая заявка в отряд (5091), вопрос зова друга
//   (5236 → TWOBUT (1, 38)) и магазин бустеров 15300; 5055 код 1 на другую карту подаётся ШТАТНЫМ приёмом. Проверяется:
//   автоотказ 5092 код 0 и 5237 код 2 ушли В ПРИЁМЕ пакета — до взвода перехода (`bTransferInProgress`/`bPendingEnterWorld`),
//   то есть прежнему соединению, как у эталона (шаг 0 загрузки идёт до закрытия сокета на шаге 3); 15300 закрыт; заявки и
//   вопроса 38 нет. Отложенный реконнект после проверки снимается (офлайн самотест на другую карту не уходит).
static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestEntryWindowsPortal(
	TEXT("asda.SelfTest.EntryWindowsPortal"),
	TEXT("Самотест перехода 5055 на другую карту: автоотказ заявки 5092 и ответ зова 5237 уходят прежнему соединению, 15300 "
	     "закрыт, вопрос 38 снят. `legacy`, `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaEntryWinTest;
		static const TCHAR* kCmd = TEXT("asda.SelfTest.EntryWindowsPortal");
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s selfready%s%s"), kCmd, KA.Has(TEXT("legacy")) ? TEXT(" legacy") : TEXT(""), KA.bQuit ? TEXT(" quit") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle Hd;
			W->GetTimerManager().SetTimer(Hd, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		TSharedRef<FRun> R = Make(W, kCmd, KA);
		R->Add(0.f, [](FRun& R)
		{
			SetSelf(R);
			Feed(R, 5091, Body5091());   // заявка «новый отряд» — висит, таймер 30 с
			Feed(R, 5236, Body5236());   // зов друга — вопрос (1, 38) на TWOBUT
			OpenClosable(R, false);      // 15300 (+ парикмахерская)
		});
		R->Add(0.8f, [](FRun& R)
		{
			UAsdaNetSubsystem* Net = R.Sub<UAsdaNetSubsystem>();
			UAsdaWindowSubsystem* WM = R.Sub<UAsdaWindowSubsystem>();
			const UAsdaSoulmateSkillSubsystem* Ss = R.Sub<UAsdaSoulmateSkillSubsystem>();
			const FState S0 = Snap(R);
			R.Check(Net && WM && WM->IsMsgBoxQueued(1, 38) && Ss && Ss->GetMateRecall().bPending && S0.Booster,
				FString::Printf(TEXT("0 висят: вопрос (1, 38)=%d, зов=%d, 15300=%d"), WM && WM->IsMsgBoxQueued(1, 38),
					Ss && Ss->GetMateRecall().bPending, S0.Booster));
			if (!Net || !WM) { return; }
			// Перехват всего исходящего: опкод, код @0 и «переход уже взведён?» в момент отправки.
			struct FSent { int32 Op; int32 Code; bool bTransfer; };
			TSharedRef<TArray<FSent>> Sent = MakeShared<TArray<FSent>>();
			TWeakObjectPtr<UAsdaNetSubsystem> WNet(Net);
			const uint32 Gen = AsdaSelfTestKit::ArmCapture(Net, [Sent, WNet](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
			{
				const UAsdaNetSubsystem* N = WNet.Get();
				Sent->Add({ (int32)Op, (P && Len > 0) ? (int32)P[0] : -1, N && (N->bTransferInProgress || N->bPendingEnterWorld) });
			}, 0, kCmd);
			// Состояние перехода — вернуть после проверки (реконнект офлайн не нужен).
			const int32 MapWas = Net->EnterMapId; const FString IpWas = Net->GameServerIP; const int32 PortWas = Net->GameServerPort;
			const bool bTrWas = Net->bTransferInProgress, bPendWas = Net->bPendingEnterWorld, bPortWas = Net->bPortalPending;
			const bool bConnWas = Net->bInGameConnected;
			const int32 PtxWas = Net->PortalTileX, PtzWas = Net->PortalTileZ, TxWas = Net->SelfTileX, TzWas = Net->SelfTileZ;
			Feed(R, 5055, Body5055(MapWas == 7 ? 8 : 7, 120, 130));   // ДРУГАЯ карта
			AsdaSelfTestKit::DisarmCapture(Net, Gen);
			Net->EnterMapId = MapWas; Net->GameServerIP = IpWas; Net->GameServerPort = PortWas;
			Net->bTransferInProgress = bTrWas; Net->bPendingEnterWorld = bPendWas; Net->bPortalPending = bPortWas;
			Net->bInGameConnected = bConnWas;
			Net->PortalTileX = PtxWas; Net->PortalTileZ = PtzWas; Net->SelfTileX = TxWas; Net->SelfTileZ = TzWas;
			if (WM) { WM->ConsumeMapLoadMsgBoxesClosed(); }   // загрузки не будет — флаг шага 0 снять
			const FSent* P5092 = Sent->FindByPredicate([](const FSent& S) { return S.Op == 5092; });
			const FSent* P5237 = Sent->FindByPredicate([](const FSent& S) { return S.Op == 5237; });
			R.Check(P5092 && P5092->Code == 0 && !P5092->bTransfer,
				FString::Printf(TEXT("A1 автоотказ заявки 5092 код 0 ушёл ДО взвода перехода (прежнему соединению): %s"),
					P5092 ? *FString::Printf(TEXT("код %d, переход взведён=%d"), P5092->Code, P5092->bTransfer) : TEXT("НЕ ОТПРАВЛЕН")));
			R.Check(P5237 && P5237->Code == 2 && !P5237->bTransfer,
				FString::Printf(TEXT("A2 ответ зова 5237 код 2 ушёл ДО взвода перехода: %s"),
					P5237 ? *FString::Printf(TEXT("код %d, переход взведён=%d"), P5237->Code, P5237->bTransfer) : TEXT("НЕ ОТПРАВЛЕН")));
			const UAsdaPartySubsystem* Party = R.Sub<UAsdaPartySubsystem>();
			const FState S = Snap(R);
			R.Check(!S.Booster && !S.Hair, FString::Printf(TEXT("A3 15300 / парикмахерская закрыты: %s"), *S.Str()));
			R.Check(!WM->IsMsgBoxQueued(1, 38) && Ss && !Ss->GetMateRecall().bPending,
				FString::Printf(TEXT("A4 вопрос (1, 38) снят, зов закрыт (в очереди=%d, зов=%d)"), WM->IsMsgBoxQueued(1, 38), Ss && Ss->GetMateRecall().bPending));
			R.Check(Party && !Party->IsJoinBoxOpen(), TEXT("A5 окно заявки 2245 закрыто"));
		});
		R->Start();
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
