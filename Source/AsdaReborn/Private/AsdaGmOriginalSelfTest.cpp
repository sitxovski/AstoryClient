// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ ВКЛАДКИ «КОМАНДЫ ОРИГИНАЛА» GM-МЕНЮ (19.10.2026): `asda.SelfTest.GmOriginal [quit]`.
//   Офлайн (`-AsdaNoNet`), на карте мира (`--map world` пакета). Реальный путь (закон diagnostic-must-use-real-path):
//     A) каждый отправитель `UAsdaNetSubsystem::SendGm*` под перехватом — тело ПОБАЙТНО против раскладки оригинала
//        (`Reports/gm_original_commands/group1..4_spec.md`; ожидаемые байты собраны здесь вручную, не боевым сборщиком),
//        вид конверта (S — токен+MD5, E — MD5 пустоты, T — только токен, N — пустое тело) и отказы без отправки;
//     B) вкладки меню: пункты вкладки «Команды оригинала» — ровно наши и в нашем порядке, на «Функциях порта» их нет;
//        окно меню открыто штатно (права — ответом 6037 на пробу), страницы листаются, все пункты видны ровно раз;
//     C) пункты меню через `ActivateFunctionById` → настоящие окна ввода 1580 / нумпад 1410 / подтверждение 1520 → тело;
//     D) ответы сервера — через `DebugFeedPacket` (тот же разбор входящих, что у живого кадра) → строки чата / нотисы.
//     E) приёмники группы 4 у цели: 6025 (2117), 6058 (сброс откатов), 6145 (отсчёт 1:1 по арифметике эталона, действия по
//        концу → 6147/6146), 6147 (зоны, стоп, 108141/108142), 6013 «от самого себя» — без перехода.
//   Итог: «GM-ОРИГ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "World/AsdaAdminSubsystem.h"
#include "World/AsdaGmOriginalSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaDisconnectPopupSubsystem.h"
#include "UI/AsdaSkillWindowSubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCoord.h"
#include "World/AsdaFloatingTextSubsystem.h"
#include "Engine/GameViewportClient.h"
#include "Login/AsdaText.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Blueprint/UserWidget.h"
#include "Components/EditableText.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Components/Button.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaGmOrigTest, Log, All);

namespace AsdaGmOrigSelfTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.GmOriginal");

	// Перехваченный исходящий кадр.
	struct FSent
	{
		uint16 Op = 0;          // опкод
		TArray<uint8> Body;     // тело, отданное конверту (payload для S, всё тело для E/T/N)
		TCHAR Env = 0;          // вид конверта
	};

	// Независимый сборщик ожидаемых байт (LE), чтобы не сверять боевой сборщик с самим собой.
	struct FExp
	{
		TArray<uint8> B;
		FExp& U8(uint32 V) { B.Add((uint8)V); return *this; }
		FExp& I16(int32 V) { B.Add((uint8)(V & 0xFF)); B.Add((uint8)((V >> 8) & 0xFF)); return *this; }
		FExp& I32(int32 V) { for (int32 i = 0; i < 4; ++i) { B.Add((uint8)(((uint32)V >> (8 * i)) & 0xFF)); } return *this; }
		FExp& F32(float V) { uint32 U; FMemory::Memcpy(&U, &V, 4); return I32((int32)U); }
		FExp& Str(const char* Ascii, int32 Field) { int32 n = 0; for (; Ascii[n] && n < Field; ++n) { B.Add((uint8)Ascii[n]); } for (; n < Field; ++n) { B.Add(0); } return *this; }
		FExp& Bytes(std::initializer_list<uint8> L) { for (uint8 V : L) { B.Add(V); } return *this; }
		FExp& Zeros(int32 N) { for (int32 i = 0; i < N; ++i) { B.Add(0); } return *this; }
	};

	static FString Hex(const TArray<uint8>& B)
	{
		FString S;
		for (uint8 V : B) { S += FString::Printf(TEXT("%02X"), V); }
		return S;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestGmOriginal(
	TEXT("asda.SelfTest.GmOriginal"),
	TEXT("Самотест вкладки «Команды оригинала» GM-меню: тела всех операторских команд побайтно, вкладки/страницы меню, пункты "
	     "через штатные окна ввода, ответы сервера. Офлайн, на карте мира. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaGmOrigSelfTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		// Свой персонаж нужен разделам C (шаблон своей клетки) и E (стоп в зоне 6147): офлайн его нет — штатный фейковый спавн
		//   (как в самотесте состояний действия) и повторный запуск команды через 2 с с меткой `selfready`.
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s selfready%s"), kCmd, KA.bQuit ? TEXT(" quit") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]()
			{
				if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); }
			}), 2.f, false);
			return;
		}
		static const uint16 kOwnOps[] = { 4209, 6007, 6010, 6012, 6014, 6018, 6022, 6024, 6026, 6028, 6029, 6032, 6033, 6035, 6036,
			6038, 6040, 6042, 6045, 6046, 6058, 6075, 6145, 6146, 6147 };
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA, 0.f, kOwnOps)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaAdminSubsystem*      Admin = GI ? GI->GetSubsystem<UAsdaAdminSubsystem>() : nullptr;
		UAsdaGmOriginalSubsystem* Gmo   = GI ? GI->GetSubsystem<UAsdaGmOriginalSubsystem>() : nullptr;
		UAsdaNetSubsystem*        Net   = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaWorldSubsystem*      WS    = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaWindowSubsystem*     WM    = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		AAsdaPlayerController*    PC    = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
		if (!Admin || !Gmo || !Net || !WS || !WM || !PC) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем или контроллера")); return; }

		int32 Ok = 0, Fail = 0;
		auto Check = [&Ok, &Fail](bool bPass, const FString& What)
		{
			if (bPass) { ++Ok;   UE_LOG(LogAsdaGmOrigTest, Log,   TEXT("GM-ОРИГ ok   %s"), *What); }
			else       { ++Fail; UE_LOG(LogAsdaGmOrigTest, Error, TEXT("GM-ОРИГ FAIL %s"), *What); }
		};

		FAsdaNetSelfScope SelfScope(Net, kCmd);
		const uint32 IdxWas = Net->GetUserIndexForDiagnostics();
		const bool bConfirmedWas = Net->IsOperatorConfirmedByServer();
		constexpr uint16 kMe = 17;          // свой userNum
		constexpr int32  kMyIdx = 2;        // свой UserIndex (оператор 0…5000)
		constexpr int32  kPeer = 23;        // userNum соседа
		constexpr int32  kPeerIdx = 900000 + kPeer;   // его charId = UserIndex (`DebugSpawnFakePeerState`)
		// Своё поле персонажа `CUser+0x174` (= `SelectedCharId`, ревизия С-2) НАМЕРЕННО отличается от UserIndex учётки:
		//   так самотест ловит подмену одного другим (у одержимого они и правда разные).
		constexpr int32  kMyChar = 777001;
		Net->SetSelfUserNumForDebug(kMe);
		Net->SetUserIndexForSelfTest(kMyIdx);
		Net->SelectedCharId = kMyChar;
		// ★ 09.11.2026: GM-меню открывается только персонажу «GM_» — имя ставим всегда (павн мог уже быть), в конце возвращаем.
		UAsdaWorldSubsystem* WSn = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		const FString NameWas = WSn ? WSn->GetSelfCharName() : FString();
		if (WSn) { WSn->SetSelfCharNameForSelfTest(TEXT("GM_SelfTest")); }

		TArray<FSent> Sent;
		const uint32 Gen = AsdaSelfTestKit::ArmCapture(Net, [&Sent](uint16 Op, const uint8* P, int32 Len, bool, TCHAR Env)
		{
			FSent S; S.Op = Op; S.Env = Env;
			if (P && Len > 0) { S.Body.Append(P, Len); }
			Sent.Add(MoveTemp(S));
		}, 0, kCmd);
		if (Gen == 0)
		{
			if (WSn) { WSn->SetSelfCharNameForSelfTest(NameWas); }
			Net->SetUserIndexForSelfTest(IdxWas);
			AsdaSelfTestKit::Finish(W, kCmd, KA.bQuit, LogAsdaGmOrigTest, TEXT("GM-ОРИГ"), 0, 1);
			return;
		}

		// Ровно один новый кадр с этим опкодом, конвертом и телом.
		auto ExpectOne = [&](int32 Before, uint16 Op, TCHAR Env, const TArray<uint8>& Want, const FString& What)
		{
			const bool bOne = Sent.Num() == Before + 1;
			const FSent* S = bOne ? &Sent.Last() : nullptr;
			const bool bPass = S && S->Op == Op && S->Env == Env && S->Body == Want;
			Check(bPass, FString::Printf(TEXT("%s: %u/%c %d б %s"), *What, (uint32)Op, Env, Want.Num(),
				bPass ? TEXT("совпало") : *FString::Printf(TEXT("ПОЛУЧЕНО %s %u/%c [%s], ждали [%s]"), bOne ? TEXT("1 кадр") : *FString::Printf(TEXT("%d кадров"), Sent.Num() - Before),
					S ? (uint32)S->Op : 0u, S ? S->Env : TEXT('-'), S ? *Hex(S->Body) : TEXT(""), *Hex(Want))));
		};
		auto ExpectNone = [&](int32 Before, const FString& What)
		{
			Check(Sent.Num() == Before, FString::Printf(TEXT("%s: пакета нет (%d)"), *What, Sent.Num() - Before));
		};
		int32 N0 = 0;

		// ═══ A. Отправители — побайтно ═══
		{
			const TArray<uint8> Name = FExp().Str("Target_1", 20).B;
			N0 = Sent.Num(); Net->SendGmSummon6012(TEXT("Target_1")); ExpectOne(N0, 6012, 'S', Name, TEXT("A1 6012 вызвать по имени (20 б cp1251, хвост нулями)"));
			N0 = Sent.Num(); Net->SendGmMeet6014(TEXT(" Target_1 ")); ExpectOne(N0, 6014, 'S', Name, TEXT("A2 6014 к игроку (пробелы по краям срезаны)"));
			N0 = Sent.Num(); Net->SendGmKick6018(TEXT("Target_1")); ExpectOne(N0, 6018, 'S', Name, TEXT("A3 6018 выкинуть"));
			N0 = Sent.Num(); Net->SendGmKick6018(TEXT("Цель")); ExpectOne(N0, 6018, 'S', FExp().Bytes({ 0xD6, 0xE5, 0xEB, 0xFC }).Zeros(16).B, TEXT("A4 имя кириллицей — cp1251"));
			N0 = Sent.Num();
			const bool bLong = Net->SendGmSummon6012(TEXT("ABCDEFGHIJKLMNOPQRST"));   // 20 байт > 19
			const bool bEmpty = Net->SendGmSummon6012(TEXT("   "));
			Check(!bLong && !bEmpty && Sent.Num() == N0, TEXT("A5 имя 20 байт и пустое — не отправлены"));

			N0 = Sent.Num(); Net->SendGmKill6022(kPeer, kPeerIdx); ExpectOne(N0, 6022, 'S', FExp().I16(kPeer).I32(kPeerIdx).B, TEXT("A6 6022 убить"));
			N0 = Sent.Num(); Net->SendGmMove6026(0, 120, 340, kPeer, kPeerIdx);
			ExpectOne(N0, 6026, 'S', FExp().U8(0).I16(120).I16(340).I16(kPeer).I32(kPeerIdx).B, TEXT("A7 6026 перенести"));
			const TArray<uint8> Md5Empty = FExp().Bytes({ 0xD4, 0x1D, 0x8C, 0xD9, 0x8F, 0x00, 0xB2, 0x04, 0xE9, 0x80, 0x09, 0x98, 0xEC, 0xF8, 0x42, 0x7E }).B;
			N0 = Sent.Num(); Net->SendGmLocalChat6028(); ExpectOne(N0, 6028, 'E', Md5Empty, TEXT("A8 6028 объявление сектору — MD5 пустоты"));
			N0 = Sent.Num(); Net->SendGmGlobalChat6029(); ExpectOne(N0, 6029, 'E', Md5Empty, TEXT("A9 6029 объявление серверу — MD5 пустоты"));
			N0 = Sent.Num(); Net->SendGmAdd6032(1, 50000000); ExpectOne(N0, 6032, 'S', FExp().U8(1).I32(50000000).B, TEXT("A10 6032 золото себе"));
			N0 = Sent.Num(); Net->SendGmMobInfo6033(0, 1234); ExpectOne(N0, 6033, 'S', FExp().U8(0).I32(1234).I16(kMe).B, TEXT("A11 6033 карточка монстра"));
			N0 = Sent.Num(); Net->SendGmInvincible6035();
			Check(Sent.Num() == N0 + 1 && Sent.Last().Op == 6035 && Sent.Last().Env == 'T' && Sent.Last().Body.Num() == 8, TEXT("A12 6035 неуязвимость — только токен 8 б"));
			N0 = Sent.Num(); Net->SendGmStats6036();
			Check(Sent.Num() == N0 + 1 && Sent.Last().Op == 6036 && Sent.Last().Env == 'T' && Sent.Last().Body.Num() == 8, TEXT("A13 6036 характеристики — только токен 8 б"));
			N0 = Sent.Num(); Net->SendGmEventSet6038(1, 2.f, -1, -1, 10, 19, 21, 0, 10, 20, 21, 30, 1, 100);
			ExpectOne(N0, 6038, 'S', FExp().I16(-1).I16(-1).I16(-1).U8(10).U8(19).U8(21).U8(0).U8(10).U8(20).U8(21).U8(30)
				.U8(1).F32(2.f).I32(-1).I32(-1).U8(1).U8(100).B, TEXT("A14 6038 ивент (29 б)"));
			N0 = Sent.Num(); Net->SendGmEventOff6040(1, 3, 7); ExpectOne(N0, 6040, 'S', FExp().I16(-1).I16(-1).I16(-1).U8(1).I32(3).I32(7).B, TEXT("A15 6040 выключить ивент"));
			N0 = Sent.Num(); Net->SendGmItemGive6042(kPeer, kPeerIdx, 4001, 5, true);
			ExpectOne(N0, 6042, 'S', FExp().I16(kPeer).I32(kPeerIdx).I32(4001).I32(5).U8(1).B, TEXT("A16 6042 выдать предмет на склад"));
			N0 = Sent.Num(); Net->SendGmMonsterPop6045(300, 1); ExpectOne(N0, 6045, 'S', FExp().I16(kMe).I16(300).U8(1).B, TEXT("A17 6045 вызвать группу"));
			N0 = Sent.Num(); Net->SendGmMonsterReturn6046(300); ExpectOne(N0, 6046, 'S', FExp().I16(kMe).I16(300).B, TEXT("A18 6046 убрать группу"));
			N0 = Sent.Num(); Net->SendGmCharValue6075(1, kPeer);
			ExpectOne(N0, 6075, 'S', FExp().I16(kMe).I32(kMyChar).U8(1).I16(kPeer).Zeros(15).B, TEXT("A19 6075 проценты против игрока"));
			// группа 4
			N0 = Sent.Num(); Net->SendGmCallMap4209(22); ExpectOne(N0, 4209, 'S', FExp().I16(0).I16(22).B, TEXT("A20 4209 собрать карту 22"));
			N0 = Sent.Num(); Net->SendGmClearGroundItems6007(); ExpectOne(N0, 6007, 'N', TArray<uint8>(), TEXT("A21 6007 предметы с земли — пустое тело без конверта"));
			N0 = Sent.Num(); Net->SendGmJump6010(120, 340); ExpectOne(N0, 6010, 'S', FExp().I16(120).I16(340).B, TEXT("A22 6010 прыжок в клетку"));
			N0 = Sent.Num(); Net->SendGmRevive6024(kPeer, kPeerIdx); ExpectOne(N0, 6024, 'S', FExp().I16(kPeer).I32(kPeerIdx).B, TEXT("A23 6024 воскресить/восполнить"));
			N0 = Sent.Num(); Net->SendGmCoolReset6058(kPeer); ExpectOne(N0, 6058, 'S', FExp().I16(kPeer).B, TEXT("A24 6058 сбросить откаты"));
			N0 = Sent.Num(); Net->SendGmCountDown6145(30, 1, 2); ExpectOne(N0, 6145, 'S', FExp().I32(30).U8(1).I16(2).B, TEXT("A25 6145 отсчёт 30 с, канал, перенос"));
			N0 = Sent.Num();
			const bool bZero = Net->SendGmCountDown6145(0, 0, 0);
			Check(!bZero && Sent.Num() == N0, TEXT("A26 6145 0 с — не отправлен (как отправитель 0xBB4D30)"));
			N0 = Sent.Num(); Net->SendGmMapBlock6147(1, -1, -1); ExpectOne(N0, 6147, 'S', FExp().U8(1).I32(-1).I32(-1).B, TEXT("A27 6147 блок карты"));
			{
				const int32 Six[6] = { 10, 20, 30, 40, 25, 35 };
				N0 = Sent.Num(); Net->SendClientTargetJump6146(1, -1, Six);
				ExpectOne(N0, 6146, 'S', FExp().U8(1).I16(-1).I32(10).I32(20).I32(30).I32(40).I32(25).I32(35).B, TEXT("A28 6146 режим 1 — перенос области"));
			}
			N0 = Sent.Num();
			const bool bNeg = Net->SendGmKill6022(-1, 0) || Net->SendGmRevive6024(-1, 0) || Net->SendGmCoolReset6058(-1) || Net->SendGmItemGive6042(kPeer, kPeerIdx, 0, 1, false);
			Check(!bNeg && Sent.Num() == N0, TEXT("A29 цель −1 / код предмета 0 — не отправлены"));
		}

		// ═══ B. Вкладки и страницы ═══
		static const TCHAR* kIds[] = { TEXT("GMO_SUMMON"), TEXT("GMO_MEET"), TEXT("GMO_KICK"), TEXT("GMO_KILL"), TEXT("GMO_MOVE"),
			TEXT("GMO_LOCALCHAT"), TEXT("GMO_GLOBALCHAT"), TEXT("GMO_ADD_EXP"), TEXT("GMO_ADD_GOLD"), TEXT("GMO_ADD_SKILL"),
			TEXT("GMO_MOBINFO"), TEXT("GMO_MOBKILL"), TEXT("GMO_MOBPOP"), TEXT("GMO_MOBRETURN"), TEXT("GMO_INVINCIBLE"), TEXT("GMO_STATS"),
			TEXT("GMO_EVENTSET"), TEXT("GMO_EVENTOFF"), TEXT("GMO_ITEMGIVE"), TEXT("GMO_CHARVALUE"), TEXT("GMO_CALLMAP"), TEXT("GMO_CLEARITEMS"),
			TEXT("GMO_JUMP"), TEXT("GMO_REVIVE"), TEXT("GMO_COOLRESET"), TEXT("GMO_COUNTDOWN"), TEXT("GMO_AREAJUMP"), TEXT("GMO_MAPBLOCK_ON"),
			TEXT("GMO_MAPBLOCK_OFF") };
		constexpr int32 kNumIds = UE_ARRAY_COUNT(kIds);
		{
			const TArray<FName> Orig = Admin->GetTabFunctionIds(EAsdaAdminTab::Original);
			bool bSame = Orig.Num() == kNumIds;
			for (int32 i = 0; bSame && i < kNumIds; ++i) { bSame = Orig[i] == FName(kIds[i]); }
			Check(bSame, FString::Printf(TEXT("B1 вкладка «Команды оригинала»: %d пунктов в порядке регистрации (есть %d)"), kNumIds, Orig.Num()));
			const TArray<FName> Port = Admin->GetTabFunctionIds(EAsdaAdminTab::Port);
			bool bNoneInPort = Port.Num() > 0;
			for (const FName& N : Port) { if (N.ToString().StartsWith(TEXT("GMO_"))) { bNoneInPort = false; } }
			Check(bNoneInPort, FString::Printf(TEXT("B2 на «Функциях порта» (%d пунктов) команд оригинала нет"), Port.Num()));

			// Права — штатно: ответ 6037 на пробу → признак сервера → меню.
			Net->ClearOperatorConfirmation();
			Admin->ResetSession();
			Gmo->DebugClearLines(); const int32 LinesBefore = 0;
			Net->DebugFeedPacket(6037, TArray<uint8>());
			Gmo->DebugFlush();
			Check(Gmo->DebugLastLines().Num() == LinesBefore && !Gmo->IsStatsPending(), TEXT("B3 проба прав 6037 без запроса — в чат не печатается"));
			Admin->RequestAdminMode(PC);
			Check(Admin->IsAuthorized() && Admin->IsMenuOpen(), TEXT("B4 права по 6037, меню открыто"));
			Admin->SetActiveTab(EAsdaAdminTab::Original);
			const int32 Pages = Admin->GetPageCount();
			TArray<FString> Seen;
			int32 MaxRows = 0;
			for (int32 Pg = 0; Pg < Pages; ++Pg)
			{
				Admin->SetPage(Pg);
				MaxRows = FMath::Max(MaxRows, Admin->DebugVisibleRowCount());
				for (int32 r = 0; r < Admin->DebugVisibleRowCount(); ++r) { Seen.Add(Admin->DebugRowText(r)); }
			}
			const int32 Per = MaxRows > 0 ? MaxRows : 1;
			Check(Admin->GetActiveTab() == EAsdaAdminTab::Original && Pages == (kNumIds + Per - 1) / Per && Seen.Num() == kNumIds,
				FString::Printf(TEXT("B5 вкладка оригинала: %d стр. по ≤%d строк, всего видно %d из %d"), Pages, MaxRows, Seen.Num(), kNumIds));
			Check(Seen.Num() == kNumIds && Seen[0].Contains(FAsdaText::Loc(990710)) && Seen.Last().Contains(FAsdaText::Loc(990816)),
				FString::Printf(TEXT("B6 подписи строк — loc: первая «%s», последняя «%s»"), Seen.Num() ? *Seen[0] : TEXT(""), Seen.Num() ? *Seen.Last() : TEXT("")));
			Admin->SetPage(Pages + 5);
			Check(Admin->GetPage() == Pages - 1, TEXT("B7 страница за концом — зажим на последней"));
			Admin->SetActiveTab(EAsdaAdminTab::Port);
			bool bPortClean = Admin->DebugVisibleRowCount() > 0 && Admin->GetPage() == 0;
			for (int32 r = 0; r < Admin->DebugVisibleRowCount(); ++r) { if (Admin->DebugRowText(r).Contains(FAsdaText::Loc(990710))) { bPortClean = false; } }
			Check(bPortClean, TEXT("B8 переключение на «Функции порта»: страница 0, пунктов оригинала нет"));
			// Клик по кнопке вкладки — рассылкой OnClicked самой кнопки; пересборка уходит в очередь следующего тика (ревизия С-3).
			UButton* TabOrig = Admin->DebugTabButton(1);
			if (TabOrig) { TabOrig->OnClicked.Broadcast(); }
			const bool bNotYet = Admin->GetActiveTab() == EAsdaAdminTab::Port;
			Admin->DebugRunDeferredMenuActions();   // «следующий тик» без повторного входа в тикер ядра
			Check(TabOrig && bNotYet && Admin->GetActiveTab() == EAsdaAdminTab::Original && Admin->DebugVisibleRowCount() > 0,
				TEXT("B9 клик по вкладке «Команды оригинала»: внутри клика меню не пересобирается, на следующем тике — вкладка оригинала"));
			UButton* NextBtn = Admin->GetPageCount() > 1 ? Admin->DebugTabButton((int32)EAsdaAdminTab::Count + 1) : nullptr;
			if (NextBtn) { NextBtn->OnClicked.Broadcast(); Admin->DebugRunDeferredMenuActions(); }
			Check(NextBtn && Admin->GetPage() == 1, TEXT("B10 клик «Вперёд >» — вторая страница"));
			Admin->SetPage(0);
			Admin->CloseMenuWindow();
		}

		// ═══ C. Пункты меню через штатные окна ═══
		auto TypeInput = [WM](const FString& Text) -> bool
		{
			UUserWidget* Dlg = WM->FindWindow(TEXT("SHOUTING_WIN"));
			UEditableText* E = Dlg ? Cast<UEditableText>(Dlg->GetWidgetFromName(TEXT("Shouting_InputBox"))) : nullptr;
			if (!E) { return false; }
			E->SetText(FText::FromString(Text));
			WM->CloseInputDialog(true);
			return true;
		};
		auto InputText = [WM]() -> FString
		{
			UUserWidget* Dlg = WM->FindWindow(TEXT("SHOUTING_WIN"));
			UEditableText* E = Dlg ? Cast<UEditableText>(Dlg->GetWidgetFromName(TEXT("Shouting_InputBox"))) : nullptr;
			return E ? E->GetText().ToString() : FString(TEXT("<нет окна>"));
		};
		auto TypeNum = [WM](int32 V) -> bool
		{
			UUserWidget* Dlg = WM->FindWindow(TEXT("NUMPAD_WIN"));
			UEditableText* E = Dlg ? Cast<UEditableText>(Dlg->GetWidgetFromName(TEXT("NUMPAD_NUMBOX"))) : nullptr;
			if (!E) { return false; }
			E->SetText(FText::FromString(FString::FromInt(V)));
			WM->CloseNumPad(true);
			return true;
		};
		auto Act = [&](const TCHAR* Id) { return Admin->ActivateFunctionById(FName(Id), PC); };
		const bool bTwo = true;
		{
			PC->ClearTarget();
			// По имени.
			N0 = Sent.Num(); Act(TEXT("GMO_SUMMON"));
			Check(WM->IsOpen(TEXT("SHOUTING_WIN")) && Sent.Num() == N0, TEXT("C1 «Вызвать к себе»: открыто окно ввода 1580, пакета ещё нет"));
			TypeInput(TEXT("Target_1"));
			ExpectOne(N0, 6012, 'S', FExp().Str("Target_1", 20).B, TEXT("C2 ввод имени → 6012"));
			N0 = Sent.Num(); Act(TEXT("GMO_KICK")); TypeInput(TEXT("Target_1"));
			Check(WM->IsOpen(TEXT("TWOBUT_MSGBOX")) && Sent.Num() == N0, TEXT("C3 «Выкинуть»: после имени — подтверждение 1520, пакета нет"));
			WM->AnswerMsgBox(bTwo, true);
			ExpectOne(N0, 6018, 'S', FExp().Str("Target_1", 20).B, TEXT("C4 «ОК» подтверждения → 6018"));
			N0 = Sent.Num(); Act(TEXT("GMO_MEET")); WM->CloseInputDialog(false);
			ExpectNone(N0, TEXT("C5 «Переместиться к игроку» — отмена ввода"));

			// Без цели — отказ до окна.
			N0 = Sent.Num(); Act(TEXT("GMO_KILL"));
			Check(!WM->IsOpen(TEXT("TWOBUT_MSGBOX")) && Sent.Num() == N0, TEXT("C6 «Убить» без цели — ни окна, ни пакета"));

			// Сосед-цель.
			WS->DebugSpawnFakePeer(kPeer, TEXT("GmoTarget"), FVector2D(25700.f, 25700.f));
			AActor* Peer = WS->ActorByUserIdx(kPeer);
			Check(Peer && WS->GetNetPlayerCharId(kPeer) == kPeerIdx, TEXT("C7 подготовка: сосед 23, charId 900023"));
			if (Peer)
			{
				PC->SetTarget(Peer);
				N0 = Sent.Num(); Act(TEXT("GMO_KILL")); WM->AnswerMsgBox(bTwo, true);
				ExpectOne(N0, 6022, 'S', FExp().I16(kPeer).I32(kPeerIdx).B, TEXT("C8 «Убить» цель → подтверждение → 6022"));
				N0 = Sent.Num(); Act(TEXT("GMO_MOVE")); TypeInput(TEXT("120 340"));
				ExpectOne(N0, 6026, 'S', FExp().U8(0).I16(120).I16(340).I16(kPeer).I32(kPeerIdx).B, TEXT("C9 «Перенести» цель → клетка → 6026"));
				N0 = Sent.Num(); Act(TEXT("GMO_MOVE")); TypeInput(TEXT("abc"));
				ExpectNone(N0, TEXT("C10 клетка «abc» — неверный ввод"));
				Check(Gmo->DebugLastLines().Num() > 0 && Gmo->DebugLastLines().Last().StartsWith(FAsdaText::Loc(990794)), TEXT("C11 строка «Неверный ввод» в чате"));
				N0 = Sent.Num(); Act(TEXT("GMO_ITEMGIVE")); TypeInput(TEXT("4001 5 склад"));
				ExpectOne(N0, 6042, 'S', FExp().I16(kPeer).I32(kPeerIdx).I32(4001).I32(5).U8(1).B, TEXT("C12 «Выдать предмет» цели «4001 5 склад» → 6042"));
				N0 = Sent.Num(); Act(TEXT("GMO_REVIVE"));
				ExpectOne(N0, 6024, 'S', FExp().I16(kPeer).I32(kPeerIdx).B, TEXT("C13 «Воскресить / восполнить» цель → 6024 сразу"));
				N0 = Sent.Num(); Act(TEXT("GMO_COOLRESET"));
				ExpectOne(N0, 6058, 'S', FExp().I16(kPeer).B, TEXT("C14 «Сбросить откаты» цели → 6058"));
				N0 = Sent.Num(); Act(TEXT("GMO_CHARVALUE"));
				ExpectOne(N0, 6075, 'S', FExp().I16(kMe).I32(kMyChar).U8(1).I16(kPeer).Zeros(15).B, TEXT("C15 «Боевые проценты» против игрока → 6075"));
				PC->ClearTarget();
			}
			// Себе без цели.
			N0 = Sent.Num(); Act(TEXT("GMO_COOLRESET"));
			ExpectOne(N0, 6058, 'S', FExp().I16(kMe).B, TEXT("C16 «Сбросить откаты» без цели — себе"));
			N0 = Sent.Num(); Act(TEXT("GMO_ITEMGIVE")); TypeInput(TEXT("4001"));
			ExpectOne(N0, 6042, 'S', FExp().I16(kMe).I32(kMyChar).I32(4001).I32(1).U8(0).B, TEXT("C17 «Выдать предмет» без цели — себе, 1 шт. в сумку"));
			N0 = Sent.Num(); Act(TEXT("GMO_ADD_GOLD"));
			Check(WM->IsNumPadOpen(), TEXT("C18 «Золото себе»: открыт нумпад 1410"));
			TypeNum(500);
			ExpectOne(N0, 6032, 'S', FExp().U8(1).I32(500).B, TEXT("C19 нумпад 500 → 6032"));
			N0 = Sent.Num(); Act(TEXT("GMO_JUMP"));
			const FString JumpInit = InputText();
			Check(!PC->GetPawn() || JumpInit.Contains(TEXT(" ")), FString::Printf(TEXT("C20 «Прыжок»: шаблон — своя клетка «%s»"), *JumpInit));
			TypeInput(TEXT("120 340"));
			ExpectOne(N0, 6010, 'S', FExp().I16(120).I16(340).B, TEXT("C21 «Прыжок в клетку» → 6010"));
			N0 = Sent.Num(); Act(TEXT("GMO_COUNTDOWN")); TypeInput(TEXT("30 1 2"));
			ExpectOne(N0, 6145, 'S', FExp().I32(30).U8(1).I16(2).B, TEXT("C22 «Обратный отсчёт» «30 1 2» → 6145"));
			N0 = Sent.Num(); Act(TEXT("GMO_COUNTDOWN")); TypeInput(TEXT("30 3"));
			ExpectNone(N0, TEXT("C23 отсчёт с охватом 3 — неверный ввод"));
			N0 = Sent.Num(); Act(TEXT("GMO_AREAJUMP")); TypeInput(TEXT("10 20 30 40 25 35"));
			Check(WM->IsOpen(TEXT("TWOBUT_MSGBOX")) && Sent.Num() == N0, TEXT("C24 «Перенести всех из области»: подтверждение, пакета нет"));
			WM->AnswerMsgBox(bTwo, true);
			ExpectOne(N0, 6146, 'S', FExp().U8(1).I16(-1).I32(10).I32(20).I32(30).I32(40).I32(25).I32(35).B, TEXT("C25 «ОК» → 6146 режим 1"));
			N0 = Sent.Num(); Act(TEXT("GMO_AREAJUMP")); TypeInput(TEXT("30 20 10 40 25 35"));
			ExpectNone(N0, TEXT("C26 область minX > maxX — неверный ввод"));
			N0 = Sent.Num(); Act(TEXT("GMO_MAPBLOCK_ON")); WM->AnswerMsgBox(bTwo, true);
			ExpectOne(N0, 6147, 'S', FExp().U8(1).I32(-1).I32(-1).B, TEXT("C27 «Блок карты: включить» → 6147 (1, −1, −1)"));
			N0 = Sent.Num(); Act(TEXT("GMO_MAPBLOCK_OFF")); WM->AnswerMsgBox(bTwo, false);
			ExpectNone(N0, TEXT("C28 «Блок карты: снять» — «Отмена»"));
			N0 = Sent.Num(); Act(TEXT("GMO_MAPBLOCK_OFF")); WM->AnswerMsgBox(bTwo, true);
			ExpectOne(N0, 6147, 'S', FExp().U8(0).I32(-1).I32(-1).B, TEXT("C29 «Блок карты: снять» → 6147 (0, −1, −1)"));
			N0 = Sent.Num(); Act(TEXT("GMO_CALLMAP")); TypeInput(TEXT("22")); WM->AnswerMsgBox(bTwo, true);
			ExpectOne(N0, 4209, 'S', FExp().I16(0).I16(22).B, TEXT("C30 «Собрать игроков карты» 22 → подтверждение → 4209"));
			N0 = Sent.Num(); Act(TEXT("GMO_CLEARITEMS")); WM->AnswerMsgBox(bTwo, true);
			ExpectOne(N0, 6007, 'N', TArray<uint8>(), TEXT("C31 «Убрать предметы с земли» → 6007 пустое тело"));
			N0 = Sent.Num(); Act(TEXT("GMO_LOCALCHAT"));
			Check(Sent.Num() == N0 + 1 && Sent.Last().Op == 6028, TEXT("C32 «Объявление сектору» → 6028 сразу"));
			N0 = Sent.Num(); Act(TEXT("GMO_EVENTSET"));
			const FString EvInit = InputText();
			UAsdaGmOriginalSubsystem::FEventSetArgs EA;
			Check(UAsdaGmOriginalSubsystem::ParseEventSet(EvInit, EA) && EA.Type == 1 && EA.Channel == -1 && EA.Map == -1,
				FString::Printf(TEXT("C33 шаблон ивента разбирается: «%s»"), *EvInit));
			TypeInput(TEXT("1 2.5 -1 -1 19.10 21:00 20.10 21:30 1 100"));
			ExpectOne(N0, 6038, 'S', FExp().I16(-1).I16(-1).I16(-1).U8(10).U8(19).U8(21).U8(0).U8(10).U8(20).U8(21).U8(30)
				.U8(1).F32(2.5f).I32(-1).I32(-1).U8(1).U8(100).B, TEXT("C34 «Включить ивент» → 6038"));
			N0 = Sent.Num(); Act(TEXT("GMO_EVENTSET")); TypeInput(TEXT("1 11 -1 -1 19.10 21:00 20.10 21:30 1 100"));
			ExpectNone(N0, TEXT("C35 множитель 11 > 10 — неверный ввод"));
			N0 = Sent.Num(); Act(TEXT("GMO_EVENTOFF")); TypeInput(TEXT("1 -1 -1"));
			ExpectOne(N0, 6040, 'S', FExp().I16(-1).I16(-1).I16(-1).U8(1).I32(-1).I32(-1).B, TEXT("C36 «Выключить ивент» → 6040"));
			N0 = Sent.Num(); Act(TEXT("GMO_STATS"));
			Check(Sent.Num() == N0 + 1 && Sent.Last().Op == 6036 && Gmo->IsStatsPending(), TEXT("C37 «Мои характеристики» → 6036, ждём 6037"));
			N0 = Sent.Num(); Act(TEXT("GMO_MOBINFO"));
			ExpectNone(N0, TEXT("C38 «Карточка монстра» без монстра-цели"));
			if (WM->IsOpen(TEXT("SHOUTING_WIN"))) { WM->CloseInputDialog(false); }
			if (WM->IsNumPadOpen()) { WM->DismissNumPad(kCmd); }
			if (WM->IsOpen(TEXT("TWOBUT_MSGBOX"))) { WM->AnswerMsgBox(bTwo, false); }
		}

		// ═══ D. Ответы сервера (разбор входящих → очередь → тик) ═══
		{
			auto Feed = [&](int32 Op, const TArray<uint8>& B) { Net->DebugFeedPacket(Op, B); Gmo->DebugFlush(); };
			auto LinesSince = [Gmo](int32 Before) { return Gmo->DebugLastLines().Num() - Before; };
			auto LastHas = [Gmo](const FString& S) { return Gmo->DebugLastLines().Num() > 0 && Gmo->DebugLastLines().Last().Contains(S); };
			Gmo->DebugClearLines(); int32 L0 = 0;
			// 6037 на свой запрос (C37): уровень/опыт/… + строка недостоверности.
			{
				TArray<uint8> B; B.SetNumZeroed(135);
				B[0] = 45; AsdaBytes::WrI32(B.GetData() + 11, 777);
				Feed(6037, B);
				Check(!Gmo->IsStatsPending() && LinesSince(L0) >= 10 && Gmo->DebugLastLines()[L0].Contains(TEXT("45")), TEXT("D1 6037 на запрос — характеристики в чат, ожидание снято"));
				Gmo->DebugClearLines(); L0 = 0;
				Feed(6037, B);
				Check(LinesSince(L0) == 0, TEXT("D2 повторный 6037 без запроса — в чат не печатается"));
			}
			Gmo->DebugClearLines(); L0 = 0;
			{
				TArray<uint8> B; B.SetNumZeroed(4); AsdaBytes::WrI32(B.GetData(), 5555);
				Feed(6023, B);
				Check(LinesSince(L0) == 0, TEXT("D3 6023 с чужим UserIndex — молча"));
				AsdaBytes::WrI32(B.GetData(), kMyChar);
				Feed(6023, B);
				Check(LinesSince(L0) == 1 && LastHas(FAsdaText::Loc(2116)), TEXT("D4 6023 со своим полем персонажа (SelectedCharId) — нотис 2116"));
			}
			Gmo->DebugClearLines(); L0 = 0;
			Feed(6027, TArray<uint8>());
			Check(LinesSince(L0) == 1 && LastHas(FAsdaText::Loc(2118)), TEXT("D5 6027 — нотис 2118"));
			Gmo->DebugClearLines(); L0 = 0;
			{
				TArray<uint8> B; B.Add(0);
				Feed(6043, B);
				Check(LinesSince(L0) == 1 && LastHas(TEXT("[ SUCCESS ITEM POP ]")), TEXT("D6 6043 код 0 — «[ SUCCESS ITEM POP ]»"));
				B[0] = 4; Gmo->DebugClearLines(); L0 = 0; Feed(6043, B);
				Check(LinesSince(L0) == 1 && LastHas(TEXT("[ WRONG ITEM CODE ]")), TEXT("D7 6043 код 4 — «[ WRONG ITEM CODE ]»"));
				B[0] = 9; Gmo->DebugClearLines(); L0 = 0; Feed(6043, B);
				Check(LinesSince(L0) == 0, TEXT("D8 6043 код 9 — строки нет (как эталон)"));
			}
			Gmo->DebugClearLines(); L0 = 0;
			{
				TArray<uint8> B; B.SetNumZeroed(24);
				B[6] = 1; AsdaBytes::WrI16(B.GetData() + 7, kPeer); AsdaBytes::WrI16(B.GetData() + 9, 123);
				Feed(6075, B);
				Check(LinesSince(L0) == 12 && Gmo->DebugLastLines()[L0] == TEXT("-------------------") && Gmo->DebugLastLines()[L0 + 3] == TEXT("TargetMinAttack = 123"),
					TEXT("D9 6075 — разделитель + 11 строк, MinAttack 123"));
			}
			Gmo->DebugClearLines(); L0 = 0;
			{
				// Коды — из живых таблиц: эталон печатает строку, только если запись нашлась (сначала предметы, затем самоцветы).
				const UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>();
				int32 ItemCode = 0;
				for (int32 c = 1; IT && c < 200000 && ItemCode == 0; ++c) { if (IT->GetItem(c)) { ItemCode = c; } }
				FAsdaSowellRow Sw;
				const int32 SowCode = (IT && IT->GetSowell(7001, Sw)) ? 7001 : 0;
				TArray<uint8> B; B.SetNumZeroed(395);
				B[82] = 50;
				AsdaBytes::WrI32(B.GetData() + 83, ItemCode); AsdaBytes::WrI32(B.GetData() + 87, 12500);
				AsdaBytes::WrI32(B.GetData() + 93, SowCode);  AsdaBytes::WrI32(B.GetData() + 97, 500);
				AsdaBytes::WrI32(B.GetData() + 103, 999999999);   // кода нет ни в одной таблице — строки нет
				Feed(6034, B);
				const TArray<FString>& Ln = Gmo->DebugLastLines();
				Check(ItemCode > 0 && SowCode > 0 && LinesSince(L0) == 6 && Ln[L0].Contains(TEXT("50(%)"))
					&& Ln[L0 + 2].StartsWith(FString::Printf(TEXT(" Item - index:%d "), ItemCode))
					&& Ln[L0 + 3].StartsWith(TEXT(" Sowel - index:7001 ")),
					FString::Printf(TEXT("D10 6034 — карточка: шанс 50 %%, предмет %d и самоцвет 7001 найдены по таблицам, неизвестный код молча"), ItemCode));
			}
			{
				const int32 K0 = Gmo->DebugKickCount();
				Gmo->DebugClearLines(); L0 = 0;
				Feed(6019, TArray<uint8>());
				Check(Gmo->DebugKickCount() == K0 + 1 && LastHas(FAsdaText::Loc(2113)), TEXT("D11 6019 — кик принят, строка 2113 (офлайн связь не закрывается)"));
				const UAsdaDisconnectPopupSubsystem* Pop = GI->GetSubsystem<UAsdaDisconnectPopupSubsystem>();
				Check(Pop && Pop->GetNextNotice() == 0, TEXT("D12 после кика текст 2113 не остался ждать следующего обрыва (ревизия С-1)"));
			}
		}

		// ═══ E. Приёмники группы 4 у цели / охвата ═══
		{
			auto Feed = [&](int32 Op, const TArray<uint8>& B) { Net->DebugFeedPacket(Op, B); Gmo->DebugFlush(); };
			auto Lines = [Gmo]() { return Gmo->DebugLastLines().Num(); };
			// 6025: [0..1] userNum, [2..3] младшее слово UserIndex.
			{
				TArray<uint8> B; B.SetNumZeroed(4);
				AsdaBytes::WrI16(B.GetData(), kMe); AsdaBytes::WrI16(B.GetData() + 2, (kMyChar + 1) & 0xFFFF);
				Gmo->DebugClearLines(); Feed(6025, B);
				Check(Lines() == 0, TEXT("E1 6025 со своим userNum, но чужим UserIndex — молча"));
				AsdaBytes::WrI16(B.GetData(), kPeer); AsdaBytes::WrI16(B.GetData() + 2, kMyChar & 0xFFFF);
				Gmo->DebugClearLines(); Feed(6025, B);
				Check(Lines() == 0, TEXT("E2 6025 чужой userNum — молча"));
				AsdaBytes::WrI16(B.GetData(), kMe);
				Gmo->DebugClearLines(); Feed(6025, B);
				Check(Lines() == 1 && Gmo->DebugLastLines()[0] == FAsdaText::Loc(2117),
					TEXT("E3 6025 свой (userNum + младшее слово UserIndex) — нотис 2117 [ИСПРАВЛЕНИЕ сравнения эталона]"));
			}
			// 6058: сброс откатов навыков.
			{
				UAsdaSkillWindowSubsystem* Sw = GI->GetSubsystem<UAsdaSkillWindowSubsystem>();
				UAsdaHotbarSubsystem* Hb = GI->GetSubsystem<UAsdaHotbarSubsystem>();
				if (Sw && Hb)
				{
					Sw->PushCooldownFromServer(501, 30);
					Hb->PushItemCooldownRaw(4001, 2, 0, 999, 30000);   // откат ПРЕДМЕТА: 6058 его не касается (вектор CHotbar+56)
					const bool bPre = Hb->QuerySkillCooldown(501).State == 1;
					TArray<uint8> B; B.SetNumZeroed(4);
					AsdaBytes::WrI16(B.GetData(), kPeer);
					Feed(6058, B);
					const bool bKept = Hb->QuerySkillCooldown(501).State == 1;
					AsdaBytes::WrI16(B.GetData(), kMe);
					Feed(6058, B);
					const bool bReset = Hb->QuerySkillCooldown(501).State == 0;
					const bool bItemKept = Hb->GetItemCooldownState(4001, 2, 0) != 3;
					Check(bItemKept, TEXT("E4b 6058: откат предмета 4001 остался (сбрасываются только навыки)"));
					Check(bPre && bKept && bReset, FString::Printf(TEXT("E4 6058: откат навыка 501 до — %s, по чужому userNum — %s, по своему — %s"),
						bPre ? TEXT("идёт") : TEXT("НЕТ"), bKept ? TEXT("остался") : TEXT("СНЯТ"), bReset ? TEXT("сброшен") : TEXT("ОСТАЛСЯ")));
				}
				else { Check(false, TEXT("E4 нет подсистем окна навыков / панели")); }
			}
			// 6145: отсчёт — арифметика `recallCountdown_Tick` 1:1.
			{
				auto Start = [&](int32 Sec, uint8 Scope, int32 Action)
				{
					TArray<uint8> B; B.SetNumZeroed(7);
					AsdaBytes::WrI32(B.GetData(), Sec); B[4] = Scope; AsdaBytes::WrI16(B.GetData() + 5, Action);
					Feed(6145, B);
				};
				Gmo->DebugClearLines();
				Start(10, 0, 0);
				Check(Gmo->IsCountDownRunning() && Lines() == 1 && Gmo->DebugLastLines()[0] == TEXT("Count Down Start Count: 10 Type: 0 ")
					&& FMath::IsNearlyEqual(Gmo->GetCountDownDurationMs(), 10000.f), TEXT("E5 6145 10 с: литерал эталона плашкой, отсчёт идёт"));
				Gmo->DebugClearLines();
				Gmo->DebugTickCountDown(1000.f);
				Check(Lines() == 0, TEXT("E6 через 1000 мс плашки нет (накопитель должен быть СТРОГО > 1000)"));
				UAsdaFloatingTextSubsystem* FT = GI->GetSubsystem<UAsdaFloatingTextSubsystem>();
				const int32 Live0 = FT ? FT->DebugLiveCount() : 0;
				Gmo->DebugTickCountDown(1.f);
				if (FT && GI->GetGameViewportClient())
				{
					Check(FT->DebugLiveCount() == Live0 + 1, TEXT("E7a вместе с плашкой — крупная цифра по центру (стиль 0x16, экранный спавн)"));
				}
				const FString Want8 = FAsdaText::Loc(108143).Replace(TEXT("%d"), TEXT("8"));
				Check(Lines() == 1 && Gmo->DebugLastLines()[0] == Want8, FString::Printf(TEXT("E7 через 1001 мс — «%s» (N−2, как эталон)"), *Want8));
				int32 Guard = 0;
				while (Gmo->IsCountDownRunning() && ++Guard < 1000) { Gmo->DebugTickCountDown(100.f); }
				const int32 Planks = Lines();
				Check(!Gmo->IsCountDownRunning() && Planks == 8 && Gmo->DebugLastLines().Last() == FAsdaText::Loc(108143).Replace(TEXT("%d"), TEXT("1")),
					FString::Printf(TEXT("E8 до конца: плашек 8…1 (%d), отсчёт остановлен, на 0 плашки нет"), Planks));
				// Действия по концу.
				int32 S0 = Sent.Num();
				Start(3, 1, 1);
				Guard = 0; while (Gmo->IsCountDownRunning() && ++Guard < 1000) { Gmo->DebugTickCountDown(100.f); }
				ExpectOne(S0, 6147, 'S', FExp().U8(1).I32(0).I32(0).B, TEXT("E9 конец отсчёта, действие 1 → 6147 (1, 0, 0)"));
				S0 = Sent.Num();
				Start(2, 2, 2);
				Guard = 0; while (Gmo->IsCountDownRunning() && ++Guard < 1000) { Gmo->DebugTickCountDown(100.f); }
				ExpectOne(S0, 6146, 'S', FExp().U8(1).I16(-1).I32(240).I32(214).I32(270).I32(228).I32(195).I32(189).B,
					TEXT("E10 конец отсчёта, действие 2 → 6146 (1, −1, 240,214,270,228, 195,189)"));
				// Повторный 6145 во время отсчёта — с нуля [ИСПРАВЛЕНИЕ: у эталона прошедшее не обнулялось и таймер замирал].
				Start(30, 0, 0);
				Gmo->DebugTickCountDown(5000.f);
				Start(3, 0, 0);
				Check(Gmo->IsCountDownRunning() && Gmo->GetCountDownElapsedMs() == 0.f, TEXT("E11 повторный 6145 (3 с после 5 с старого) — отсчёт с нуля, не замер"));
				Guard = 0; while (Gmo->IsCountDownRunning() && ++Guard < 1000) { Gmo->DebugTickCountDown(100.f); }
				Check(!Gmo->IsCountDownRunning(), TEXT("E12 повторный отсчёт доходит до конца"));
			}
			// 6147: блок карты.
			{
				Check(UAsdaGmOriginalSubsystem::MapBlockZoneAt(FVector2D(23000, 23000)) == 0 && UAsdaGmOriginalSubsystem::MapBlockZoneAt(FVector2D(26000, 23000)) == 1
					&& UAsdaGmOriginalSubsystem::MapBlockZoneAt(FVector2D(25500, 22000)) == 2 && UAsdaGmOriginalSubsystem::MapBlockZoneAt(FVector2D(22500, 22300)) == -1
					&& UAsdaGmOriginalSubsystem::MapBlockZoneAt(FVector2D(30000, 30000)) == -1, TEXT("E13 зоны 6147: 0/1/2 внутри, граница и вне — −1 (строгие неравенства)"));
				TArray<uint8> B; B.SetNumZeroed(9);
				B[0] = 0;
				Gmo->DebugClearLines(); Feed(6147, B);
				Check(!Gmo->IsMapBlockOn() && Lines() == 1 && Gmo->DebugLastLines()[0] == FAsdaText::Loc(108142), TEXT("E14 6147 [0]=0 — плашка 108142"));
				AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(PC->GetPawn());
				if (Ch)
				{
					const FVector Was = Ch->GetActorLocation();
					const bool bInZoneAlready = UAsdaGmOriginalSubsystem::MapBlockZoneAt(Ch->GetGamePosXZ()) >= 0;
					B[0] = 1;
					if (!bInZoneAlready)
					{
						Gmo->DebugClearLines(); Feed(6147, B);
						Check(!Gmo->IsMapBlockOn() && Lines() == 0, TEXT("E15 6147 [0]=1 вне зон — молча, блока нет"));
					}
					const FVector2D Ue = AsdaCoord::GameToUe2D(FVector2D(23000.f, 23000.f));
					Ch->SetActorLocation(FVector(Ue.X, Ue.Y, Was.Z), false, nullptr, ETeleportType::TeleportPhysics);
					PC->bMovingToPoint = true;   // «шёл к точке» — стоп обязан это снять
					Gmo->DebugClearLines(); Feed(6147, B);
					Check(!PC->bMovingToPoint, TEXT("E16a стоп на месте: ход к точке снят"));
					Check(Gmo->IsMapBlockOn() && Gmo->GetMapBlockZone() == 0 && Lines() == 1 && Gmo->DebugLastLines()[0] == FAsdaText::Loc(108141),
						FString::Printf(TEXT("E16 6147 [0]=1 в зоне 0 (позиция %s) — стоп, плашка 108141"), *Ch->GetGamePosXZ().ToString()));
					Ch->SetActorLocation(Was, false, nullptr, ETeleportType::TeleportPhysics);
					B[0] = 0; Feed(6147, B);
				}
				else { UE_LOG(LogAsdaGmOrigTest, Warning, TEXT("GM-ОРИГ E15/E16 пропущены: своего павна нет")); }
			}
			// 6013 «от себя» — переход не начинается [ИСПРАВЛЕНИЕ].
			{
				TArray<uint8> B; B.SetNumZeroed(82);
				AsdaBytes::WrI32(B.GetData(), kMyChar); AsdaBytes::WrI16(B.GetData() + 4, kMe);
				FMemory::Memcpy(B.GetData() + 10, "127.0.0.1", 9);
				const bool bWas = Net->bTransferInProgress;
				Net->DebugFeedPacket(6013, B);
				Check(!bWas && !Net->bTransferInProgress, TEXT("E17 6013 со своим UserIndex (сбор карты 4209 у самого оператора) — перехода нет"));
			}
		}

		// ═══ Уборка ═══
		{
			TArray<uint8> B; B.SetNumZeroed(2);
			AsdaBytes::WrI16(B.GetData(), (int16)kPeer);
			Net->DebugFeedPacket(4011, B);   // уход соседа штатным 4011
		}
		AsdaSelfTestKit::DisarmCapture(Net, Gen);
		Admin->CloseMenuWindow();
		Admin->ResetSession();
		if (WSn) { WSn->SetSelfCharNameForSelfTest(NameWas); }
		Net->SetUserIndexForSelfTest(IdxWas);
		Net->ClearOperatorConfirmation();
		if (bConfirmedWas) { Net->DebugFeedPacket(6037, TArray<uint8>()); }
		AsdaSelfTestKit::Finish(W, kCmd, KA.bQuit, LogAsdaGmOrigTest, TEXT("GM-ОРИГ"), Ok, Fail);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
