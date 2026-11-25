// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ ВОЛНЫ 2-В (06.10.2026): `asda.UI.SelfTestWave2C [delay=N] [quit]` — дуэль (CDuelMgr, окно 1200),
//   своя плашка (TARGETMARK_MAN 1620), баннер смены профессии 18500. Отчёт `Reports/owner_1006/wave2C_duel_mark_banner.md` §5.
//   Входы ТОЛЬКО штатные:
//     • пакеты — `UAsdaNetSubsystem::DebugFeedPacket` → `HandlePacket` → делегаты → подсистемы (тот же разбор, что у сервера);
//     • отправка 4303 — БОЕВОЙ `SendDuelAnswer4303` под узким перехватом (`ArmSendCapture(…, 4303)`), в сеть не уходит;
//     • клики — `UAsdaHotbarSubsystem::HandleNoticeIconClick(14)` (тело клика мыши после хит-теста) и
//       `UAsdaPartySubsystem::OnJoinOk/OnJoinCancel` (их зовут реле кнопок 2248/2249);
//     • время — ручные часы подсистем (`DebugAdvance` = тот же шаг, что у их Tick);
//     • геометрия — `GetCachedGeometry` ПОСЛЕ отрисовки (закон ui-check-game-path-geometry).
//   Контрольные пары у каждой ветки: я ↔ не я, код ↔ другой код, ключ [ПОРТ] 1 ↔ 0, близко ↔ далеко, жив ↔ мёртв.
//   Сервер не нужен: команда отказывает при живом соединении (`RefuseDebugWhenOnline`).
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#if !UE_BUILD_SHIPPING   // ★ античит А2 (12.10.2026): самотесты и стенды в Shipping не входят
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaSelf.h"
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaSessionReset.h"
#include "UI/AsdaDuelSubsystem.h"
#include "UI/AsdaChangeJobBannerSubsystem.h"
#include "UI/AsdaTargetMarkManSubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaPartySubsystem.h"
#include "UI/AsdaHudSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaNotice.h"
#include "World/AsdaWorldSubsystem.h"
#include "Test/AsdaSelfTestWorldScope.h"   // ★ 01.11.2026: FAsdaWorldSelfScope — изоляция пакета
#include "World/AsdaNameplateComponent.h"
#include "World/AsdaCoord.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"   // OpenLevel — настоящая смена карты (ревизия К-1)
#include "Containers/Ticker.h"         // продолжение самотеста после OpenLevel (таймеры прежнего мира гибнут)
#include "AsdaCVarSave.h"               // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaWave2CTest, Log, All);

struct FAsdaWave2CSelfTest
{
	static int32 Ok;                // пройдено проверок
	static int32 Fail;              // провалено проверок
	static int32 SavedCharId;       // свой charId до прогона
	static int32 SavedJob, SavedClass, SavedSubJob, SavedSkillPts;   // поля профессии до прогона
	static constexpr int32 kSelfCharId  = 910001;   // свой charId на время прогона
	static constexpr int32 kOtherCharId = 910002;   // charId вызвавшего (в мире его нет)
	static constexpr int32 kOtherUser   = 4321;     // userNum вызвавшего (в мире его нет)

	// Учёт проверки и строка лога.
	static void Check(bool bOk, const FString& What)
	{
		if (bOk) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaWave2CTest, Log, TEXT("[волна2В-самотест] %s — %s"), bOk ? TEXT("ОК") : TEXT("ПРОВАЛ"), *What);
	}
	// Запись little-endian и имени cp1251 (ASCII) в тело.
	static void PutI16(TArray<uint8>& B, int32 At, int32 V) { B[At] = (uint8)(V & 0xFF); B[At + 1] = (uint8)((V >> 8) & 0xFF); }
	static void PutI32(TArray<uint8>& B, int32 At, int32 V) { for (int32 i = 0; i < 4; ++i) { B[At + i] = (uint8)(((uint32)V >> (8 * i)) & 0xFF); } }
	static void PutStr(TArray<uint8>& B, int32 At, const char* S) { for (int32 i = 0; S[i] && i < 19; ++i) { B[At + i] = (uint8)S[i]; } }
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): через `AsdaCVarSave` — исходные значение и SetBy запоминаются при первом касании, `Finish` их
	//   возвращает (прежде ключи не возвращались вовсе: Plate.SelfHideOnDeath оставался 0, остальные — 1 с SetByCode).
	static void SetCVar(const TCHAR* Name, int32 V) { AsdaCVarSave::SetForTest(Name, V); }

	// 4302 (34 б): вызвавший @0/@2/@6, цель @26, группа @32.
	static TArray<uint8> Make4302(int32 CalleeUser, int32 Pvp)
	{
		TArray<uint8> B; B.SetNumZeroed(34);
		PutI16(B, 0, kOtherUser); PutI32(B, 2, kOtherCharId); PutStr(B, 6, "Challenger");
		PutI16(B, 26, CalleeUser); PutI32(B, 28, kSelfCharId); PutI16(B, 32, Pvp);
		return B;
	}
	// 4303 (33 б): код @0, userNum @1, charId @3, имя @7, группа @27, центр @29/@31.
	static TArray<uint8> Make4303(int32 Code, int32 User, int32 Pvp)
	{
		TArray<uint8> B; B.SetNumZeroed(33);
		B[0] = (uint8)Code; PutI16(B, 1, User); PutI32(B, 3, kSelfCharId); PutStr(B, 7, "Rival");
		PutI16(B, 27, Pvp); PutI16(B, 29, 256); PutI16(B, 31, 256);
		return B;
	}
	// 4305 (14 б): группа @0, charId A @2, charId B @6, тайл @10/@12.
	static TArray<uint8> Make4305(int32 Pvp, int32 CharA, int32 CharB, int32 TileX, int32 TileY)
	{
		TArray<uint8> B; B.SetNumZeroed(14);
		PutI16(B, 0, Pvp); PutI32(B, 2, CharA); PutI32(B, 6, CharB); PutI16(B, 10, TileX); PutI16(B, 12, TileY);
		return B;
	}
	// 4304 (55 б): группа @0, победитель @2/@4, проигравший @8/@10, тип @14, имена @15/@35.
	static TArray<uint8> Make4304(int32 Pvp, int32 WinU, int32 WinC, int32 LoseU, int32 LoseC, int32 Type, const char* WinN, const char* LoseN)
	{
		TArray<uint8> B; B.SetNumZeroed(55);
		PutI16(B, 0, Pvp); PutI16(B, 2, WinU); PutI32(B, 4, WinC); PutI16(B, 8, LoseU); PutI32(B, 10, LoseC);
		B[14] = (uint8)Type; PutStr(B, 15, WinN); PutStr(B, 35, LoseN);
		return B;
	}
	// 5042 (501 б): код шага @0, квест @1, стадия @3, тип @5, страница @6, награда @8, профессия @10..@14.
	static TArray<uint8> Make5042(int32 Step, int32 Job, int32 Cls, int32 Sub, int32 Pts)
	{
		TArray<uint8> B; B.SetNumZeroed(501);
		B[0] = (uint8)Step; PutI16(B, 1, 9001); PutI16(B, 3, 1); B[5] = 1; PutI16(B, 6, -1);
		B[10] = (uint8)Job; B[11] = (uint8)Cls; B[12] = (uint8)Sub; PutI16(B, 13, Pts);
		return B;
	}
	// Сколько раз встречается loc в следе.
	static int32 CountIn(const TArray<int32>& T, int32 Id) { int32 N = 0; for (int32 X : T) { N += (X == Id) ? 1 : 0; } return N; }

	static void RunSync(UWorld* W);
	static void RunGeometry(UWorld* W);
	static void Finish(UWorld* W);
	static void RunAfterOpenLevel(UWorld* W, TWeakObjectPtr<UUserWidget> OldOverlay, TWeakObjectPtr<UUserWidget> OldBanner);
};
// ★ 01.11.2026: снимок мира своего персонажа на время прогона (взят на старте, возвращается перед OpenLevel).
static TUniquePtr<FAsdaWorldSelfScope> GAsdaWave2CWorldScope;
int32 FAsdaWave2CSelfTest::Ok = 0;
int32 FAsdaWave2CSelfTest::Fail = 0;
int32 FAsdaWave2CSelfTest::SavedCharId = 0;
int32 FAsdaWave2CSelfTest::SavedJob = -1;
int32 FAsdaWave2CSelfTest::SavedClass = -1;
int32 FAsdaWave2CSelfTest::SavedSubJob = -1;
int32 FAsdaWave2CSelfTest::SavedSkillPts = -1;

void FAsdaWave2CSelfTest::RunSync(UWorld* W)
{
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaDuelSubsystem* Duel = GI ? GI->GetSubsystem<UAsdaDuelSubsystem>() : nullptr;
	UAsdaChangeJobBannerSubsystem* Job = GI ? GI->GetSubsystem<UAsdaChangeJobBannerSubsystem>() : nullptr;
	UAsdaTargetMarkManSubsystem* Mark = GI ? GI->GetSubsystem<UAsdaTargetMarkManSubsystem>() : nullptr;
	UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
	UAsdaPartySubsystem* Party = GI ? GI->GetSubsystem<UAsdaPartySubsystem>() : nullptr;
	UAsdaHudSubsystem* Hud = GI ? GI->GetSubsystem<UAsdaHudSubsystem>() : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	UAsdaSessionResetSubsystem* SR = GI ? GI->GetSubsystem<UAsdaSessionResetSubsystem>() : nullptr;
	Ok = 0; Fail = 0;
	if (!Net || !Duel || !Job || !Mark || !Hb || !Party || !Hud || !WM || !WS || !SR)
	{
		Check(false, TEXT("нет одной из подсистем (сеть/дуэль/баннер/плашка/хотбар/отряд/HUD/окна/мир/сброс)"));
		return;
	}
	auto Feed = [Net](int32 Op, const TArray<uint8>& B) { Net->DebugFeedPacket(Op, B); };   // штатный HandlePacket
	SavedCharId = Net->SelectedCharId;
	SavedJob = Net->SelfJob; SavedClass = Net->SelfClassId; SavedSubJob = Net->SelfSubJob; SavedSkillPts = Net->SelfSkillPoints;
	Net->SelectedCharId = kSelfCharId;
	const int32 Self = AsdaSelf::GetUserIdxOr(GI, 0);
	AActor* SelfActor = (IsValid(WS->LocalPlayerActor) ? WS->LocalPlayerActor.Get() : nullptr);   // ⚠ смена карты (§5) его уничтожает — берётся заново
	Check(SelfActor && Net->IsSelfUserIdx(Self) && !Net->IsSelfUserIdx(kOtherUser),
		FString::Printf(TEXT("свой павн есть, свой userNum %d опознаётся, %d — чужой"), Self, kOtherUser));
	if (!SelfActor) { return; }
	Duel->DebugSetManualClock(true);
	Job->DebugSetManualClock(true);
	Duel->DebugResetAll();
	Job->DebugReset();
	WM->ClearMsgBoxQueue(TEXT("самотест 2-В: старт"));

	// Узкий перехват 4303 — БОЕВОЙ отправитель, в сеть не уходит.
	TArray<TArray<uint8>> Sent;
	uint32 Gen = AsdaSelfTestKit::ArmCapture(Net, [&Sent](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
	{
		if (Op == 4303) { Sent.Add(TArray<uint8>(P, Len)); }
	}, 4303, TEXT("asda.UI.SelfTestWave2C"));

	// ═══ 1. ВЫЗОВ МНЕ (4302) → значок 14 → окно 2245 → ответ 4303 ═══
	{
		Feed(4302, Make4302(Self + 1, 7));   // контрольная пара: цель НЕ я
		const bool bIconOther = Hb->HasNoticeIcon(14);
		Check(Duel->GetTarget().CharId < 0 && bIconOther,
			FString::Printf(TEXT("4302 цель не я: поля вызова не тронуты (charId %d), значок 14 стоит (эталон ставит его без гейта): %d"),
				Duel->GetTarget().CharId, bIconOther ? 1 : 0));
		Hb->RemoveNoticeIcon(14);
		Duel->DebugResetAll();
		Feed(4302, Make4302(Self, 7));
		const UAsdaDuelSubsystem::FTarget& T = Duel->GetTarget();
		Check(T.CharId == kOtherCharId && T.UserIdx == kOtherUser && T.Name == TEXT("Challenger") && T.PvpIdx == 7,
			FString::Printf(TEXT("4302 цель я: вызвавший charId %d userNum %d «%s», группа %d"), T.CharId, T.UserIdx, *T.Name, T.PvpIdx));
		Check(Hb->HasNoticeIcon(14) && Duel->IsRequestActive(), TEXT("значок 14 на хотбаре, m_bRequestActive = 1"));
		Check(Hb->HandleNoticeIconClick(14) && !Hb->HasNoticeIcon(14), TEXT("клик по значку 14: обработан, значок снят"));
		const FString Txt = Party->GetJoinBoxText();
		Check(Party->IsJoinBoxOpen() && Duel->IsPromptShown() && Txt.Contains(TEXT("Challenger")) && !Txt.Contains(TEXT("%s")),
			FString::Printf(TEXT("окно 2245 открыто дуэлью, текст «%s» (имя подставлено, «%%s» не осталось)"), *Txt));
		Duel->DebugAdvance(10.f);
		Check(Party->IsJoinBoxOpen() && Sent.Num() == 0, TEXT("через 10 с окно ещё открыто, ответа нет"));
		Party->OnJoinOk();   // кнопка 2248
		Check(Sent.Num() == 1 && Sent[0].Num() >= 33 && Sent[0][0] == 1 && AsdaBytes::RdI16(&Sent[0][1]) == kOtherUser
			&& AsdaBytes::RdI32(&Sent[0][3]) == kOtherCharId && AsdaBytes::RdI16(&Sent[0][27]) == 7 && Sent[0][7] == 0,
			FString::Printf(TEXT("2248 → 4303: %d пакет(ов), ответ %d, userNum %d, charId %d, группа %d, имя нулями"),
				Sent.Num(), Sent.Num() ? Sent[0][0] : -1, Sent.Num() ? AsdaBytes::RdI16(&Sent[0][1]) : -1,
				Sent.Num() ? AsdaBytes::RdI32(&Sent[0][3]) : -1, Sent.Num() ? AsdaBytes::RdI16(&Sent[0][27]) : -1));
		Check(!Party->IsJoinBoxOpen() && !Duel->IsRequestActive() && Duel->GetTarget().CharId < 0, TEXT("после ответа окно закрыто, вызов сброшен"));
		// контрольная пара: «Отмена» → ответ 0
		Feed(4302, Make4302(Self, 8));
		Hb->HandleNoticeIconClick(14);
		Party->OnJoinCancel();   // кнопка 2249
		Check(Sent.Num() == 2 && Sent[1][0] == 0 && AsdaBytes::RdI16(&Sent[1][27]) == 8, TEXT("2249 → 4303 с ответом 0 (группа 8)"));
		// без вызова кнопка дуэлью не шлёт ничего
		Party->OnJoinOk();
		Check(Sent.Num() == 2, TEXT("2248 без вызова: 4303 не уходит (эталон: `if (m_nTargetSessionId >= 0)`)"));
		// WM_TIMER 31: 30 с от значка — сброс и снятие значка БЕЗ ответа
		Feed(4302, Make4302(Self, 9));
		Duel->DebugAdvance(29.9f);
		Check(Hb->HasNoticeIcon(14) && Duel->IsRequestActive(), TEXT("29,9 с: значок и вызов ещё живы"));
		Duel->DebugAdvance(0.2f);
		Check(!Hb->HasNoticeIcon(14) && !Duel->IsRequestActive() && Sent.Num() == 2, TEXT("30 с: WM_TIMER 31 — значок снят, вызов сброшен, 4303 НЕ ушёл"));
		// окно открыто 30 с: в эталоне раньше срабатывает WM_TIMER 31 (взведён раньше показа) — проверяем, что ответ 0 не потерян,
		//   когда окно показано сразу (одновременный срок): либо таймер 31 закрыл без ответа, либо ушёл 0 — но НЕ «1».
		Feed(4302, Make4302(Self, 10));
		Hb->HandleNoticeIconClick(14);
		Duel->DebugAdvance(30.1f);
		Check(!Party->IsJoinBoxOpen() && (Sent.Num() == 2 || (Sent.Num() == 3 && Sent[2][0] == 0)),
			FString::Printf(TEXT("окно без ответа 30 с — закрыто, принятия нет (отправлено %d)"), Sent.Num()));
	}

	// ═══ 2. 4303: КОДЫ ОТКАЗА И ПРИНЯТИЕ → ОТСЧЁТ 11 С ═══
	{
		WM->ClearMsgBoxQueue(TEXT("самотест 2-В"));
		Duel->DebugClearTrace();
		Feed(4303, Make4303(0, Self, 5));
		Check(Duel->DebugNoticeTrace().Num() == 1 && Duel->DebugNoticeTrace()[0] == 108102 && Duel->DebugLastText(108102).Contains(TEXT("Rival"))
			&& WM->GetMsgBoxQueueNum() > 0,
			FString::Printf(TEXT("код 0 → окно-сообщение 108102 «%s»"), *Duel->DebugLastText(108102)));
		const int32 Codes[7] = { 2, 3, 4, 5, 6, 7, 8 };
		const int32 Locs[7]  = { 108103, 108104, 108105, 108108, 108109, 108110, 108111 };
		bool bAll = true;
		for (int32 I = 0; I < 7; ++I)
		{
			Duel->DebugClearTrace();
			Feed(4303, Make4303(Codes[I], Self, 5));
			bAll &= (Duel->DebugNoticeTrace().Num() == 1 && Duel->DebugNoticeTrace()[0] == Locs[I]);
		}
		Check(bAll, TEXT("коды 2..8 → 108103/104/105/108/109/110/111 (по одному нотису)"));
		Duel->DebugClearTrace();
		Feed(4303, Make4303(9, Self, 5));
		const bool bFix9 = Duel->DebugNoticeTrace().Num() == 1 && Duel->DebugNoticeTrace()[0] == 108104;
		SetCVar(TEXT("asda.Duel.FixCode9"), 0);
		Duel->DebugClearTrace();
		Feed(4303, Make4303(9, Self, 5));
		Check(bFix9 && Duel->DebugNoticeTrace().Num() == 0, TEXT("код 9: [ПОРТ] ключ 1 → 108104; ключ 0 → молча, как оригинал (пара)"));
		SetCVar(TEXT("asda.Duel.FixCode9"), 1);
		WM->ClearMsgBoxQueue(TEXT("самотест 2-В"));
		// принятие про чужого (его нет в мире) — эталон молчит
		Duel->DebugClearTrace();
		Feed(4303, Make4303(1, kOtherUser, 5));
		Check(!Duel->IsCountdownRunning() && Duel->DebugNoticeTrace().Num() == 0, TEXT("код 1 про персонажа вне мира: отсчёта нет, нотиса нет (пара)"));
		Feed(4303, Make4303(1, Self, 5));
		Check(Duel->IsCountdownRunning() && Duel->GetDuelIndex(Self) == 5 && Duel->DebugNoticeTrace().Num() == 1
			&& Duel->DebugNoticeTrace()[0] == 108101 && Duel->DebugLastText(108101).Contains(TEXT("Rival")),
			FString::Printf(TEXT("код 1 про меня: отсчёт пошёл, [156] = 5, 108101 «%s»"), *Duel->DebugLastText(108101)));
		Duel->DebugAdvance(0.9f);
		Check(Duel->DebugShownOnes() < 0 && Duel->DebugShownTens() < 0, TEXT("0,9 с: цифр ещё нет (накопитель < 1000 мс)"));
		// ★ Первая цифра — когда накопитель ПРЕВЫСИТ 1000 мс: (11000 − 1050) / 1000 = 9. «10» при свежем накопителе не бывает.
		Duel->DebugAdvance(0.15f);
		Check(Duel->DebugShownTens() < 0 && Duel->DebugShownOnes() == 9 && Duel->GetLastCountdownShown() == 9
			&& Duel->DebugLastText(108116).Contains(TEXT("9")),
			FString::Printf(TEXT("1,05 с: в окне 1200 «9» (десятки %d, единицы %d), строка «%s»"),
				Duel->DebugShownTens(), Duel->DebugShownOnes(), *Duel->DebugLastText(108116)));
		for (int32 S = 0; S < 7; ++S) { Duel->DebugAdvance(1.01f); }
		Check(Duel->DebugShownTens() < 0 && Duel->DebugShownOnes() == 2 && Duel->GetLastCountdownShown() == 2,
			FString::Printf(TEXT("8,12 с: «2» (десятки %d, единицы %d)"), Duel->DebugShownTens(), Duel->DebugShownOnes()));
		Duel->DebugAdvance(1.01f);
		Check(Duel->DebugShownOnes() == 1 && Duel->IsCountdownRunning(), TEXT("9,13 с: «1»"));
		Duel->DebugAdvance(1.01f);
		Check(!Duel->IsCountdownRunning() && Duel->DebugIconShown(0) && !Duel->DebugIconShown(1) && Duel->DebugShownOnes() < 0
			&& CountIn(Duel->DebugNoticeTrace(), 108116) == 9 && CountIn(Duel->DebugNoticeTrace(), 108115) == 1,
			FString::Printf(TEXT("10,14 с: отсчёт кончился — START, цифры скрыты, 108116 ×%d, 108115 ×%d"),
				CountIn(Duel->DebugNoticeTrace(), 108116), CountIn(Duel->DebugNoticeTrace(), 108115)));
		Duel->DebugAdvance(1.5f);
		const float Op = Duel->DebugIconOpacity(0);
		Check(Op > 0.35f && Op < 0.75f, FString::Printf(TEXT("START гаснет за 3 с: через ~1,5 с прозрачность %.2f"), Op));
		Duel->DebugAdvance(2.0f);
		Check(!Duel->DebugIconShown(0) && !Duel->DebugOverlayShown(), TEXT("через 3 с START погас, окно 1200 свёрнуто"));
		// [ОРИГИНАЛ] `g_fCountdownTickAccum` не обнуляется между дуэлями: в конце остался ~1010 мс ⇒ второй отсчёт сразу «10».
		Duel->DebugClearTrace();
		Feed(4303, Make4303(1, Self, 6));
		Duel->DebugAdvance(0.016f);
		Check(Duel->DebugShownTens() == 1 && Duel->DebugShownOnes() == 0 && Duel->GetLastCountdownShown() == 10,
			FString::Printf(TEXT("второй отсчёт без сброса накопителя: сразу «10» (десятки %d, единицы %d) — путь десятков"),
				Duel->DebugShownTens(), Duel->DebugShownOnes()));
		Duel->DebugAdvance(11.f);
		Duel->DebugAdvance(3.5f);
	}

	// ═══ 3. АРЕНА 4305 И ПРЕДУПРЕЖДЕНИЯ ═══
	{
		const FIntPoint T = AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(SelfActor->GetActorLocation()));
		Duel->DebugClearTrace();
		Feed(4305, Make4305(11, kSelfCharId, kOtherCharId, T.X, T.Y));
		Check(Duel->GetArenaCount() == 1 && Duel->IsInArena(kSelfCharId) && !Duel->IsInArena(kOtherCharId),
			FString::Printf(TEXT("4305: арена есть (%d), я в арене, чужого в мире нет — его [92] не ставится"), Duel->GetArenaCount()));
		Duel->DebugAdvance(1.0f);
		Check(Duel->DebugNoticeTrace().Num() == 0, TEXT("в центре арены предупреждений нет (пара)"));
		Duel->DebugResetAll();
		Feed(4305, Make4305(12, kSelfCharId, -1, T.X + 25, T.Y));   // ~2500: дальше 2000, ближе 3000
		Duel->DebugAdvance(0.4f);
		const int32 Early = Duel->DebugNoticeTrace().Num();
		Duel->DebugAdvance(0.2f);
		Check(Early == 0 && CountIn(Duel->DebugNoticeTrace(), 108124) == 1, FString::Printf(TEXT("25 тайлов: через 0,6 с один 108124 (до 0,5 с — %d)"), Early));
		Duel->DebugResetAll();
		Feed(4305, Make4305(13, kSelfCharId, -1, T.X + 35, T.Y));   // ~3500: за границей поля
		for (int32 F = 0; F < 36; ++F) { Duel->DebugAdvance(1.f / 60.f); }
		const int32 Fixed = CountIn(Duel->DebugNoticeTrace(), 108125);
		SetCVar(TEXT("asda.Duel.FixWarnSpam"), 0);
		Duel->DebugResetAll();
		Feed(4305, Make4305(14, kSelfCharId, -1, T.X + 35, T.Y));
		for (int32 F = 0; F < 36; ++F) { Duel->DebugAdvance(1.f / 60.f); }
		const int32 Orig = CountIn(Duel->DebugNoticeTrace(), 108125);
		SetCVar(TEXT("asda.Duel.FixWarnSpam"), 1);
		Check(Fixed == 1 && Orig > 3, FString::Printf(TEXT("35 тайлов, 0,6 с: [ПОРТ] 108125 ×%d; оригинал (ключ 0) ×%d — каждый кадр (пара)"), Fixed, Orig));
		Duel->DebugResetAll();
	}

	// ═══ 4. ИТОГ 4304 ═══
	{
		const FIntPoint T = AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(SelfActor->GetActorLocation()));
		// победа своего: 108117, затем 108112 (победитель, проигравший), VICTORY, арена снята
		Feed(4305, Make4305(20, kSelfCharId, kOtherCharId, T.X, T.Y));
		Duel->DebugClearTrace();
		Feed(4304, Make4304(20, Self, kSelfCharId, kOtherUser, kOtherCharId, 2, "Hero", "Rival"));
		const TArray<int32>& Tr = Duel->DebugNoticeTrace();
		Check(Tr.Num() == 2 && Tr[0] == 108117 && Tr[1] == 108112 && Duel->DebugLastText(108112).StartsWith(TEXT("Hero"))
			&& Duel->DebugLastText(108112).Contains(TEXT("Rival")) && Duel->DebugIconShown(1) && Duel->GetArenaCount() == 0 && !Duel->IsInArena(kSelfCharId),
			FString::Printf(TEXT("победа (тип 2): след %s, «%s», VICTORY, арен %d"),
				*FString::JoinBy(Tr, TEXT(" "), [](int32 X) { return FString::FromInt(X); }), *Duel->DebugLastText(108112), Duel->GetArenaCount()));
		// повтор того же пакета (сервер шлёт проигравшему дважды): [ПОРТ] отброшен
		// ★ 06.11.2026 (баг 7, изоляция пакета): счётчик живёт всю сессию, в пакете его уже двигал итог дуэли другого теста — считаем прирост.
		const int32 DedupBefore = Duel->DebugDedupDropped();
		Feed(4304, Make4304(20, Self, kSelfCharId, kOtherUser, kOtherCharId, 2, "Hero", "Rival"));
		Check(Duel->DebugNoticeTrace().Num() == 2 && Duel->DebugDedupDropped() - DedupBefore == 1, TEXT("повтор 4304 отброшен (asda.Duel.DedupResult 1)"));
		SetCVar(TEXT("asda.Duel.DedupResult"), 0);
		Feed(4304, Make4304(20, Self, kSelfCharId, kOtherUser, kOtherCharId, 2, "Hero", "Rival"));
		Check(Duel->DebugNoticeTrace().Num() == 4, TEXT("ключ 0: повтор отработан ещё раз — как оригинал (пара)"));
		SetCVar(TEXT("asda.Duel.DedupResult"), 1);
		// поражение своего: 108117, 108122 (проигравший, победитель), LOSE
		Duel->DebugResetAll();
		Feed(4304, Make4304(21, kOtherUser, kOtherCharId, Self, kSelfCharId, 1, "Rival", "Hero"));
		const TArray<int32>& Tr2 = Duel->DebugNoticeTrace();
		Check(Tr2.Num() == 2 && Tr2[0] == 108117 && Tr2[1] == 108122 && Duel->DebugLastText(108122).StartsWith(TEXT("Hero"))
			&& Duel->DebugIconShown(2) && !Duel->DebugIconShown(1),
			FString::Printf(TEXT("поражение (тип 1): «%s», LOSE"), *Duel->DebugLastText(108122)));
		// сдача (тип 4): 108113 / 108123
		Duel->DebugResetAll();
		Feed(4304, Make4304(22, Self, kSelfCharId, kOtherUser, kOtherCharId, 4, "Hero", "Rival"));
		Check(CountIn(Duel->DebugNoticeTrace(), 108113) == 1 && CountIn(Duel->DebugNoticeTrace(), 108112) == 0 && Duel->DebugIconShown(1),
			TEXT("сдача (тип 4), я победитель: 108113, VICTORY"));
		Duel->DebugResetAll();
		Feed(4304, Make4304(23, kOtherUser, kOtherCharId, Self, kSelfCharId, 4, "Rival", "Hero"));
		Check(CountIn(Duel->DebugNoticeTrace(), 108123) == 1 && Duel->DebugIconShown(2), TEXT("сдача (тип 4), я проигравший: 108123, LOSE"));
		// зритель
		Duel->DebugResetAll();
		Feed(4304, Make4304(24, kOtherUser, kOtherCharId, kOtherUser + 1, kOtherCharId + 1, 0, "A1", "B1"));
		Check(Duel->DebugNoticeTrace().Num() == 1 && Duel->DebugNoticeTrace()[0] == 108112 && !Duel->DebugOverlayShown(),
			FString::Printf(TEXT("зритель: только 108112 «%s», без 108117 и без надписи"), *Duel->DebugLastText(108112)));
		Duel->DebugResetAll();
		Feed(4304, Make4304(25, kOtherUser, kOtherCharId, Self, kSelfCharId, 7, "X", "Y"));
		Check(Duel->DebugNoticeTrace().Num() == 0, TEXT("тип 7 (> 4): эталон ничего не делает (пара)"));
	}

	// ═══ 5. СМЕНА КАРТЫ (настоящий сброс) И ПОЛНЫЙ СБРОС ═══
	{
		const FIntPoint T = AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(SelfActor->GetActorLocation()));
		Duel->DebugResetAll();
		Feed(4302, Make4302(Self, 30));
		Feed(4305, Make4305(30, kSelfCharId, kOtherCharId, T.X, T.Y));
		SR->Broadcast(EAsdaSessionResetReason::MapChanged);
		Check(Duel->GetArenaCount() == 0 && !Duel->IsInArena(kSelfCharId) && Duel->IsRequestActive() && Duel->GetTarget().PvpIdx == 30,
			TEXT("смена карты: арены сняты, [92] сброшен, вызов мне сохранён (как эталон)"));
		Duel->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		Check(!Duel->IsRequestActive() && Duel->GetTarget().CharId < 0 && !Hb->HasNoticeIcon(14), TEXT("полный сброс: вызов и значок 14 сняты (пара)"));
		// Смена карты сносит акторы мира, в том числе свой павн, — вход на новую карту = новый спавн (как 4003).
		GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256 Wave2C"));
		SelfActor = (IsValid(WS->LocalPlayerActor) ? WS->LocalPlayerActor.Get() : nullptr);
		Check(SelfActor != nullptr, TEXT("после смены карты свой павн заспавнен заново"));
		if (!SelfActor) { AsdaSelfTestKit::DisarmCapture(Net, Gen); return; }
	}

	// ═══ 6. СВОЯ ПЛАШКА (1620): по умолчанию как чужие (ключ 0); ключ 1 — точный эталон ═══
	{
		auto Make4053 = [](int32 U) { TArray<uint8> B; B.SetNumZeroed(4); PutI16(B, 0, U); PutI16(B, 2, U + 100); return B; };
		auto Make4056 = [](int32 U, int32 State) { TArray<uint8> B; B.SetNumZeroed(22); PutI16(B, 0, U); PutI16(B, 2, 256); PutI16(B, 4, 256); PutI32(B, 6, 300); PutI16(B, 10, 50); PutI16(B, 20, State); return B; };
		UAsdaNameplateComponent* NP = Mark->FindSelfPlate();
		SetCVar(TEXT("asda.Plate.SelfHideOnDeath"), 0);
		Mark->Tick(0.f);
		Check(NP && !NP->bHideForLocalPlayer, FString::Printf(TEXT("жив: своя плашка видна: плашка %s, гейт %d"), NP ? TEXT("есть") : TEXT("НЕТ"), NP ? (int32)NP->bHideForLocalPlayer : -1));
		Feed(4053, Make4053(Self));
		Mark->Tick(0.f);
		Check(NP && !NP->bHideForLocalPlayer && Net->bSelfDead, TEXT("ключ 0 (по умолчанию): мёртв — своя плашка видна, как чужие (закон владельца)"));
		SetCVar(TEXT("asda.Plate.SelfHideOnDeath"), 1);
		Mark->Tick(0.f);
		Check(NP && NP->bHideForLocalPlayer && Mark->IsSelfPlateHiddenByDeath(), TEXT("ключ 1: смерть «на глазах» (4053) — скрыта (hudHideTargetNameplate) (пара)"));
		Feed(4056, Make4056(Self, 200));   // прибытие МЁРТВЫМ
		Mark->Tick(0.f);
		Check(NP && !NP->bHideForLocalPlayer && Net->bSelfDead, TEXT("ключ 1: 4056 прибытие мёртвым — показана (эталон показывает 1620 без гейта «мёртв»)"));
		Feed(4053, Make4053(Self + 1));
		Mark->Tick(0.f);
		Check(NP && !NP->bHideForLocalPlayer, TEXT("смерть ЧУЖОГО: своя плашка не трогается (пара)"));
		Feed(4056, Make4056(Self, 0));     // воскрешение
		Mark->Tick(0.f);
		Check(NP && !NP->bHideForLocalPlayer && !Net->bSelfDead, TEXT("4056 воскрешение: жив, плашка видна"));
		// ★ Повторная ревизия М-3: вход/рассинхрон УЖЕ мёртвым (6001 причина 1 → `EnterSelfDeadState(…, true)`) — эталон 1620 не прячет.
		{
			TArray<uint8> B6001; B6001.SetNumZeroed(3); PutI16(B6001, 0, 4007); B6001[2] = 1;
			Feed(6001, B6001);
			Mark->Tick(0.f);
			Check(Net->bSelfDead && Net->bSelfDeathNoClip && NP && !NP->bHideForLocalPlayer && !Mark->DebugHideLatch(),
				TEXT("ключ 1: вошли уже мёртвым (6001 причина 1, без клипа) — плашка НЕ скрыта (hudHide зовёт только переход в смерть)"));
			Feed(4056, Make4056(Self, 0));
			Mark->Tick(0.f);
		}
		// ★ Повторная ревизия М-2: полный сброс сессии снимает защёлку (`uiCharSelectInitControls` → `SetVisible(1620, 1)`).
		Feed(4053, Make4053(Self));
		Mark->Tick(0.f);
		const bool bHid = NP && NP->bHideForLocalPlayer && Mark->DebugHideLatch();
		Mark->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		Mark->Tick(0.f);
		Check(bHid && !Mark->DebugHideLatch() && NP && !NP->bHideForLocalPlayer,
			TEXT("ключ 1: смерть → скрыта; полный сброс сессии → защёлка снята, плашка видна (пара)"));
		Feed(4056, Make4056(Self, 0));
		Mark->Tick(0.f);
		SetCVar(TEXT("asda.Plate.SelfHideOnDeath"), 0);
		Mark->Tick(0.f);
	}

	// ═══ 6б. ОКНО 2245 — РЕГРЕССИЯ ОТРЯДА (ревизия С-3): кнопки 2248/2249 при заявке отряда ═══
	{
		TArray<TPair<int32, TArray<uint8>>> All;   // весь исходящий трафик (узкий перехват 4303 на время снят)
		AsdaSelfTestKit::DisarmCapture(Net, Gen);
		const uint32 GenAll = AsdaSelfTestKit::ArmCapture(Net, [&All](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			All.Add(TPair<int32, TArray<uint8>>((int32)Op, TArray<uint8>(P, Len)));
		}, 0, TEXT("asda.UI.SelfTestWave2C"));
		auto MakeReq = [](int32 ReqId) { TArray<uint8> B; B.SetNumZeroed(30); B[0] = 0; PutI16(B, 1, kOtherUser); PutI32(B, 3, kOtherCharId); PutStr(B, 7, "Leader"); B[27] = 0; PutI16(B, 28, ReqId); return B; };
		auto Count = [&All](int32 Op) { int32 N = 0; for (const auto& E : All) { N += (E.Key == Op) ? 1 : 0; } return N; };
		auto Last = [&All](int32 Op) -> const TArray<uint8>* { for (int32 I = All.Num() - 1; I >= 0; --I) { if (All[I].Key == Op) { return &All[I].Value; } } return nullptr; };
		Duel->DebugResetAll();
		Feed(5091, MakeReq(31)); Party->Tick(0.f);
		Party->OnJoinOk();
		const TArray<uint8>* A = Last(5092);
		Check(A && A->Num() >= 3 && (*A)[0] == 1 && AsdaBytes::RdI16(&(*A)[1]) == 31 && Count(4303) == 0,
			FString::Printf(TEXT("5091 → 2248: ушёл 5092 код %d заявка %d, 4303 нет (%d)"), A ? (int32)(*A)[0] : -1, A ? AsdaBytes::RdI16(&(*A)[1]) : -1, Count(4303)));
		Feed(5095, MakeReq(32)); Party->Tick(0.f);
		Party->OnJoinCancel();
		const TArray<uint8>* C = Last(5096);
		Check(C && (*C)[0] == 0 && AsdaBytes::RdI16(&(*C)[1]) == 32 && Count(4303) == 0, TEXT("5095 → 2249: ушёл 5096 код 0, 4303 нет"));
		Feed(5091, MakeReq(33)); Party->Tick(0.f);
		Feed(4302, Make4302(Self, 34));
		const int32 N5092 = Count(5092);
		Party->OnJoinOk();
		const TArray<uint8>* D = Last(4303);
		Check(Count(5092) == N5092 + 1 && D && (*D)[0] == 1 && AsdaBytes::RdI16(&(*D)[27]) == 34,
			TEXT("заявка отряда + вызов дуэли одновременно: 2248 отвечает ОБОИМ (5092 и 4303), как эталон"));
		const int32 Before = All.Num();
		Party->OnJoinOk(); Party->OnJoinCancel();
		Check(All.Num() == Before, TEXT("без заявки и без вызова: 2248/2249 ничего не шлют (пара)"));
		if (Hb->HasNoticeIcon(0)) { Hb->RemoveNoticeIcon(0); }
		if (Hb->HasNoticeIcon(8)) { Hb->RemoveNoticeIcon(8); }
		Hb->RemoveNoticeIcon(14);
		AsdaSelfTestKit::DisarmCapture(Net, GenAll);
		Gen = AsdaSelfTestKit::ArmCapture(Net, [&Sent](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			if (Op == 4303) { Sent.Add(TArray<uint8>(P, Len)); }
		}, 4303, TEXT("asda.UI.SelfTestWave2C"));
		Duel->DebugResetAll();
	}

	// ═══ 7. СМЕНА ПРОФЕССИИ (5042) И БАННЕР 18500 ═══
	{
		WM->ClearMsgBoxQueue(TEXT("самотест 2-В"));
		Job->DebugReset();
		const int32 JobWas = Net->SelfJob;
		Feed(5042, Make5042(3, 1, 1, 0, 5));   // код шага ≠ 0
		Check(Net->SelfJob == JobWas && Job->DebugPhase() == 0, TEXT("5042 с кодом шага 3: профессия не меняется, баннера нет (пара)"));
		Feed(5042, Make5042(0, 0, 1, 0, 5));   // body[10] = 0 — смены профессии нет
		Check(Net->SelfJob == JobWas && Job->DebugPhase() == 0, TEXT("5042 без флага профессии (body[10] = 0): ничего (пара)"));
		Feed(5042, Make5042(0, 1, 1, 0, 5));   // Fighter: класс 1 → надпись 0, группа 1
		float Bx = 0.f, Jx = 0.f;
		const bool bX = Job->DebugLabelX(Bx, Jx);
		Check(Net->SelfJob == 1 && Net->SelfClassId == 1 && Net->SelfSubJob == 0 && Net->SelfSkillPoints == 5 && Job->GetSelfJobGroup() == 1,
			FString::Printf(TEXT("5042 смена профессии: job %d, класс %d, подкласс %d, очки %d, группа %d"),
				Net->SelfJob, Net->SelfClassId, Net->SelfSubJob, Net->SelfSkillPoints, Job->GetSelfJobGroup()));
		Check(Job->DebugPhase() == 1 && Job->IsStartActive() && Job->DebugShownJob() == 0 && Job->DebugOffsetPx() < 0.f && bX
			&& FMath::IsNearlyEqual(Bx, 52.f, 0.6f) && FMath::IsNearlyEqual(Jx, 182.f, 0.6f),
			FString::Printf(TEXT("баннер: въезд, надпись Fighter, сдвиг %.0f px, Become.x %.0f (ждём 52), Job.x %.0f (ждём 182)"),
				Job->DebugOffsetPx(), Bx, Jx));
		// взаимоисключение: пока идёт 18500, баннер уровня 18550 не стартует
		Hud->OnSelfLevelUp();
		Check(!Hud->IsConBanner2Active(), TEXT("повышение уровня во время баннера 18500: 18550 не стартовал (startActive)"));
		Job->DebugAdvance(0.5f);
		Check(Job->DebugPhase() == 2 && FMath::IsNearlyZero(Job->DebugOffsetPx()), TEXT("0,5 с: баннер на якоре, показ (таймер 11 взведён)"));
		// Прибытие на 9-м кадре (0,15 с при H = 720: шаги 25+24+…+17 ≥ 180) ⇒ таймер 11 сработает в 2,15 с.
		Job->DebugAdvance(1.5f);
		Check(Job->DebugPhase() == 2 && Job->IsStartActive(), TEXT("2,0 с от старта: ещё показ (таймер 11 — в 2,15 с)"));
		Job->DebugAdvance(0.2f);
		Check(Job->DebugPhase() == 3 && !Job->IsStartActive(), TEXT("2,2 с: таймер 11 — угасание, startActive = 0"));
		Job->DebugAdvance(0.45f);
		const float Op = Job->DebugOpacity();
		Check(Op > 0.3f && Op < 0.75f, FString::Printf(TEXT("угасание 1 с: на середине прозрачность %.2f"), Op));
		Job->DebugAdvance(0.6f);
		Check(Job->DebugPhase() == 0 && Job->DebugShownJob() < 0 && Job->DebugNotice7006Count() == 1 && WM->GetMsgBoxQueueNum() > 0,
			FString::Printf(TEXT("таймер 12: баннер снят, первая профессия — 7006 ×%d (окно-сообщение)"), Job->DebugNotice7006Count()));
		WM->ClearMsgBoxQueue(TEXT("самотест 2-В"));
		// вторая профессия (Knight, класс 2 → надпись 1): 7006 нет
		Feed(5042, Make5042(0, 2, 2, 0, 3));
		Check(Job->DebugShownJob() == 1 && Job->GetSelfJobGroup() == 2, TEXT("класс 2 → надпись Knight, группа 2"));
		Job->DebugAdvance(5.f);
		Check(Job->DebugNotice7006Count() == 1, TEXT("вторая профессия: нотиса 7006 нет (пара)"));
		// лучник 3-й (класс 14 → надпись 5, группа 3), маг 1-й (класс 23 → надпись 6, группа 1)
		int32 G = 0;
		const int32 I14 = UAsdaChangeJobBannerSubsystem::JobBannerIdFromClass(14, G);
		const int32 G14 = G;
		const int32 I23 = UAsdaChangeJobBannerSubsystem::JobBannerIdFromClass(23, G);
		const int32 I4 = UAsdaChangeJobBannerSubsystem::JobBannerIdFromClass(4, G);
		Check(I14 == 5 && G14 == 3 && I23 == 6 && I4 == -1 && G == 0, TEXT("разбор класса: 14 → 5/3, 23 → 6/1, 4 (4-я профессия) → без баннера"));
		// 4-я профессия: поля меняются, баннера нет
		Feed(5042, Make5042(0, 3, 4, 1, 0));
		Check(Net->SelfClassId == 4 && Job->DebugPhase() == 0, TEXT("класс 4: поля обновлены, баннера нет (как оригинал)"));
		WM->ClearMsgBoxQueue(TEXT("самотест 2-В"));
		// обратное взаимоисключение (идёт 18550 → 18500 пропущен) — в конце геометрии: баннер уровня живёт по своим часам.
	}

	// ═══ 8. Сцена для геометрии: START и баннер на якоре (часы стоят — состояние держится до снимка) ═══
	{
		Job->DebugReset();
		Duel->DebugResetAll();
		Feed(4303, Make4303(1, Self, 40));
		Duel->DebugAdvance(11.1f);   // START показан, угасание только началось
		Feed(5042, Make5042(0, 1, 1, 0, 5));
		Job->DebugAdvance(0.6f);     // на якоре
		Check(Duel->DebugIconShown(0) && Job->DebugPhase() == 2, TEXT("сцена геометрии: START и баннер Fighter на экране"));
		WM->ClearMsgBoxQueue(TEXT("самотест 2-В"));
	}
	AsdaSelfTestKit::DisarmCapture(Net, Gen);
	UE_LOG(LogAsdaWave2CTest, Log, TEXT("[волна2В-самотест] синхронная часть: проверок %d, провалов %d"), Ok + Fail, Fail);
}

void FAsdaWave2CSelfTest::RunGeometry(UWorld* W)
{
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaDuelSubsystem* Duel = GI ? GI->GetSubsystem<UAsdaDuelSubsystem>() : nullptr;
	UAsdaChangeJobBannerSubsystem* Job = GI ? GI->GetSubsystem<UAsdaChangeJobBannerSubsystem>() : nullptr;
	if (!Duel || !Job) { Check(false, TEXT("геометрия: нет подсистем")); return; }
	auto Rect = [](const UWidget* Wd, FVector2D& P, FVector2D& S)
	{
		if (!Wd) { return false; }
		const FGeometry& G = Wd->GetCachedGeometry();
		P = FVector2D(G.GetAbsolutePosition()); S = FVector2D(G.GetAbsoluteSize());
		return S.X > 0.f && S.Y > 0.f;
	};
	UUserWidget* Ov = Duel->DebugOverlay();
	UUserWidget* Bn = Job->DebugWidget();
	UWidget* Root = Ov ? AsdaUi::FindNodeExact(Ov, TEXT("PVP_Gfont_Win_0_0")) : nullptr;
	UWidget* Start = Ov ? AsdaUi::FindNodeExact(Ov, TEXT("PVP_Gfont_1_1_0")) : nullptr;
	UWidget* Bar = Bn ? AsdaUi::FindNodeExact(Bn, TEXT("ChangeJob_Banner_0_0")) : nullptr;
	UWidget* Become = Bn ? AsdaUi::FindNodeExact(Bn, TEXT("Con_Banner_Become_11_0")) : nullptr;
	UWidget* Fighter = Bn ? AsdaUi::FindNodeExact(Bn, TEXT("Con_Banner_Fighter_12_0")) : nullptr;
	FVector2D RP, RS, SP, SS, BP, BS, EP, ES, FP, FS;
	const bool bR = Rect(Root, RP, RS), bS = Rect(Start, SP, SS), bB = Rect(Bar, BP, BS), bE = Rect(Become, EP, ES), bF = Rect(Fighter, FP, FS);
	// Масштаб: подложка 1200 — 2×2 в JUI.
	const float K = bR ? (float)RS.X / 2.f : 1.f;
	auto Near = [K](const FVector2D& A, const FVector2D& B) { return A.Equals(B, 1.5f * FMath::Max(K, 1.f)); };
	Check(bR && bS && Near(SP, RP + FVector2D(-46.f, 10.f) * K) && Near(SS, FVector2D(195.f, 49.f) * K),
		FString::Printf(TEXT("START 1201 на экране (%.1f,%.1f) %.1f×%.1f; ждём база+(−46,10) и 195×49 ×%.2f [JUI]"), SP.X, SP.Y, SS.X, SS.Y, K));
	Check(bB && bE && bF && Near(BS, FVector2D(320.f, 81.f) * K) && Near(EP - BP, FVector2D(52.f, 20.f) * K) && Near(FP - BP, FVector2D(182.f, 20.f) * K),
		FString::Printf(TEXT("баннер 18500: полоса %.1f×%.1f; Become −полоса (%.1f,%.1f) ждём (52,20); Fighter (%.1f,%.1f) ждём (182,20) ×%.2f"),
			BS.X, BS.Y, (EP - BP).X, (EP - BP).Y, (FP - BP).X, (FP - BP).Y, K));
	if (GEngine && W)
	{
		GEngine->Exec(W, TEXT("asda.UI.TreeGeom w2c_after WBP_PVP_Gfont_Win"));
		GEngine->Exec(W, TEXT("asda.UI.TreeGeom w2c_after WBP_ChangeJob_Banner"));
	}
	// Обратное взаимоисключение: идёт баннер 18550 (уровень) → баннер 18500 не стартует (`showActive`, ui_core.c:17931-17934).
	UAsdaHudSubsystem* Hud = GI ? GI->GetSubsystem<UAsdaHudSubsystem>() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (Hud && Net)
	{
		Job->DebugReset();
		Hud->OnSelfLevelUp();
		const bool bLv = Hud->IsConBanner2Active();
		Net->DebugFeedPacket(5042, Make5042(0, 1, 12, 0, 1));
		Check(bLv && Job->DebugPhase() == 0, TEXT("баннер уровня 18550 идёт → баннер профессии 18500 пропущен (showActive)"));
	}
}

// ★ Ревизия 2-В К-1: после НАСТОЯЩЕГО `OpenLevel` окна прежнего мира сняты движком — подсистемы обязаны создать их заново.
void FAsdaWave2CSelfTest::RunAfterOpenLevel(UWorld* W, TWeakObjectPtr<UUserWidget> OldOverlay, TWeakObjectPtr<UUserWidget> OldBanner)
{
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaDuelSubsystem* Duel = GI ? GI->GetSubsystem<UAsdaDuelSubsystem>() : nullptr;
	UAsdaChangeJobBannerSubsystem* Job = GI ? GI->GetSubsystem<UAsdaChangeJobBannerSubsystem>() : nullptr;
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!Net || !Duel || !Job || !WS) { Check(false, TEXT("OpenLevel: нет подсистем")); return; }
	UUserWidget* OldOv = OldOverlay.Get();
	UUserWidget* OldBn = OldBanner.Get();
	Check(OldOv && OldBn && !OldOv->IsInViewport() && !OldBn->IsInViewport(),
		FString::Printf(TEXT("OpenLevel: прежние окна 1200/18500 сняты движком (на экране %d/%d) — без пересоздания показ шёл бы в них (пара)"),
			OldOv ? (int32)OldOv->IsInViewport() : -1, OldBn ? (int32)OldBn->IsInViewport() : -1));
	const int32 Self = AsdaSelf::GetUserIdxOr(GI, 0);
	Check(IsValid(WS->LocalPlayerActor), TEXT("OpenLevel: свой павн в новом мире"));
	Duel->DebugResetAll();
	Net->DebugFeedPacket(4303, Make4303(1, Self, 50));
	Duel->DebugAdvance(11.1f);
	UUserWidget* NewOv = Duel->DebugOverlay();
	Check(NewOv && NewOv != OldOv && NewOv->IsInViewport() && NewOv->GetWorld() == W && Duel->DebugIconShown(0),
		TEXT("OpenLevel: отсчёт → START в НОВОМ окне 1200 (на экране, мир текущий)"));
	Job->DebugReset();
	Net->DebugFeedPacket(5042, Make5042(0, 1, 1, 0, 5));
	Job->DebugAdvance(0.6f);
	UUserWidget* NewBn = Job->DebugWidget();
	Check(NewBn && NewBn != OldBn && NewBn->IsInViewport() && NewBn->GetWorld() == W && Job->DebugPhase() == 2 && Job->DebugShownJob() == 0,
		TEXT("OpenLevel: баннер 18500 в НОВОМ окне (на экране, мир текущий, Fighter)"));
}

void FAsdaWave2CSelfTest::Finish(UWorld* W)
{
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи прогона (Duel.FixCode9/FixWarnSpam/DedupResult, Plate.SelfHideOnDeath) — к исходному
	//   значению И приоритету SetBy.
	AsdaCVarSave::RestoreAll(TEXT("asda.UI.SelfTestWave2C"));
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaDuelSubsystem* Duel = GI ? GI->GetSubsystem<UAsdaDuelSubsystem>() : nullptr;
	UAsdaChangeJobBannerSubsystem* Job = GI ? GI->GetSubsystem<UAsdaChangeJobBannerSubsystem>() : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (Duel) { Duel->DebugResetAll(); Duel->DebugSetManualClock(false); }
	if (Job) { Job->DebugReset(); Job->DebugSetManualClock(false); }
	if (Net)
	{
		Net->SelectedCharId = SavedCharId;
		Net->SelfJob = SavedJob; Net->SelfClassId = SavedClass; Net->SelfSubJob = SavedSubJob; Net->SelfSkillPoints = SavedSkillPts;
	}
	if (WM) { WM->ClearMsgBoxQueue(TEXT("самотест 2-В: конец")); }
	AsdaSelfTestKit::Summary(LogAsdaWave2CTest, TEXT("[волна2В-самотест]"), Ok, Fail, TEXT(""), false);   // ★ 11.10.2026 (В3 S1): единый итог (было «проверок N, провалов M»)
}

// asda.UI.SelfTestWave2C [delay=N] [quit] — офлайн: спавн своего павна (если его нет), через 4 с синхронная часть,
//   ещё через 0,8 с (кадры отрисованы) геометрия, возврат состояния, выход по `quit`.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaWave2CSelfTest(
	TEXT("asda.UI.SelfTestWave2C"),
	TEXT("asda.UI.SelfTestWave2C [delay=N] [quit] — ОФЛАЙН-самотест волны 2-В: дуэль 4302/4303/4304/4305 (окна 2245 и 1200, ")
	TEXT("отсчёт, арена, итог), своя плашка 1620 (смерть/воскрешение), смена профессии 5042 и баннер 18500; пакеты штатным HandlePacket"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.UI.SelfTestWave2C"), KitArgs)) { return; }
		if (!W) { return; }
		float Delay = 0.f;
		bool bQuit = false;
		for (const FString& Arg : A)
		{
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
		}
		Delay = AsdaSelfTestKit::DelayArgOr(A, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
		TWeakObjectPtr<UWorld> WW = W;
		auto Start = [WW, bQuit]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !GEngine) { return; }
			if (AsdaSelfTestKit::RefuseDeferred(Wd, TEXT("asda.UI.SelfTestWave2C"), bQuit)) { return; }   // Р-2: офлайн повторно — на отложенном старте
			APlayerController* P = Wd->GetFirstPlayerController();
			// ★ 01.11.2026 (изоляция пакета): прошлый самотест мог вернуть «свой номер неизвестен» или обнулить ссылку мира на свой павн
			//   (`FAsdaNetSelfScope` DuelLoser, сбросы сессии) — тогда свой персонаж заводится заново, как без павна.
			const UAsdaNetSubsystem* NetS = Wd->GetGameInstance() ? Wd->GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			const UAsdaWorldSubsystem* WSs = Wd->GetGameInstance() ? Wd->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
			const bool bSelfOk = P && P->GetPawn() && NetS && NetS->IsSelfUserNumKnown() && WSs && IsValid(WSs->LocalPlayerActor);
			// Снимок мира своего персонажа на время прогона (до `OpenLevel`): лишние павны/питомцы прогона следующим не достаются.
			GAsdaWave2CWorldScope = MakeUnique<FAsdaWorldSelfScope>(Wd, TEXT("asda.UI.SelfTestWave2C"));
			if (!bSelfOk)
			{
				if (!P || !P->GetPawn()) { GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3")); }
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256 Wave2C"));
			}
			FTimerHandle H1;
			Wd->GetTimerManager().SetTimer(H1, FTimerDelegate::CreateLambda([WW, bQuit]()
			{
				UWorld* W2 = WW.Get();
				if (!W2) { return; }
				FAsdaWave2CSelfTest::RunSync(W2);
				FTimerHandle H2;
				W2->GetTimerManager().SetTimer(H2, FTimerDelegate::CreateLambda([WW, bQuit]()
				{
					UWorld* W3 = WW.Get();
					if (!W3)
					{
						AsdaCVarSave::RestoreAll(TEXT("asda.UI.SelfTestWave2C"));   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи — и при уходе мира
						return;
					}
					FAsdaWave2CSelfTest::RunGeometry(W3);
					// ★ Ревизия К-1: настоящий OpenLevel (тот же вызов, что `UAsdaLoadManagerSubsystem::FinishLoadAndOpen`),
					//   продолжение — тикером ядра: таймеры прежнего мира гибнут вместе с ним.
					UGameInstance* GI3 = W3->GetGameInstance();
					UAsdaDuelSubsystem* D3 = GI3 ? GI3->GetSubsystem<UAsdaDuelSubsystem>() : nullptr;
					UAsdaChangeJobBannerSubsystem* J3 = GI3 ? GI3->GetSubsystem<UAsdaChangeJobBannerSubsystem>() : nullptr;
					TWeakObjectPtr<UUserWidget> OldOv = D3 ? D3->DebugOverlay() : nullptr;
					TWeakObjectPtr<UUserWidget> OldBn = J3 ? J3->DebugWidget() : nullptr;
					TWeakObjectPtr<UGameInstance> WGI = GI3;
					const UPTRINT OldAddr = (UPTRINT)W3;
					const double T0 = FPlatformTime::Seconds();
					TSharedRef<int32> Stage = MakeShared<int32>(0);
					TSharedRef<double> StageT = MakeShared<double>(0.0);
					GAsdaWave2CWorldScope.Reset();   // ★ 01.11.2026: снимок мира вернуть ДО смены уровня
					UGameplayStatics::OpenLevel(W3, FName(TEXT("alpen")), true, TEXT("game=/Script/AsdaReborn.AsdaWorldGameMode"));
					FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
						[WGI, OldAddr, T0, Stage, StageT, OldOv, OldBn, bQuit](float) -> bool
					{
						UGameInstance* G = WGI.Get();
						if (!G)
						{
							AsdaCVarSave::RestoreAll(TEXT("asda.UI.SelfTestWave2C"));   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи — и при уходе экземпляра игры
							return false;
						}
						UWorld* NW = G->GetWorld();
						const double Now = FPlatformTime::Seconds();
						if (!NW || (UPTRINT)NW == OldAddr || !NW->HasBegunPlay())
						{
							if (Now - T0 < 120.0) { return true; }
							FAsdaWave2CSelfTest::Check(false, TEXT("OpenLevel: новый мир не поднялся за 120 с"));
							if (NW) { FAsdaWave2CSelfTest::Finish(NW); AsdaSelfTestKit::Quit(NW, bQuit); }
							else { AsdaCVarSave::RestoreAll(TEXT("asda.UI.SelfTestWave2C")); }   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи — и без нового мира
							return false;
						}
						if (*Stage == 0)
						{
							*Stage = 1; *StageT = Now;
							if (GEngine)
							{
								GEngine->Exec(NW, TEXT("Asda.Load.CurrentMap 3"));
								GEngine->Exec(NW, TEXT("Asda.World.FakeSelfSpawn 256 256 Wave2C"));
							}
							return true;
						}
						if (Now - *StageT < 3.0) { return true; }
						FAsdaWave2CSelfTest::RunAfterOpenLevel(NW, OldOv, OldBn);
						FAsdaWave2CSelfTest::Finish(NW);
						AsdaSelfTestKit::Quit(NW, bQuit);
						return false;
					}), 0.5f);
				}), 0.8f, false);
			}), 4.f, false);
		};
		if (Delay <= 0.f) { Start(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Start), Delay, false);
	}));
#endif // ASDA_DEV_CONSOLE

#endif // !UE_BUILD_SHIPPING
