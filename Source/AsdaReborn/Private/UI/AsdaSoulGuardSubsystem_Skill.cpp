// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSoulGuardSubsystem_Skill.cpp — ★ R3-c 12.11.2026 (разрез AsdaSoulGuardSubsystem.cpp, дословный перенос): сеть и навыки стража: 6156/6157/6159/6160, очередь движений, применение навыка, нотисы, Tick.
//   Общее для частей — в AsdaSoulGuardSubsystem_Private.h; шапка и реверс — в AsdaSoulGuardSubsystem.cpp.
#include "UI/AsdaSoulGuardSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"   // ★ U13 (В1 дедупа №2): AsdaNotice::ConsoleEcho — эхо консольных команд
#include "AsdaSelf.h"   // ★ волна 1: «это я?» / мой номер / мой уровень — общая точка ядра (E2, E4)
#include "UI/AsdaUiHelpers.h"   // ★ Аудит №4 (B1): AsdaUi::SlotOf/SlotRect — общая точка слота канваса

#include "UI/AsdaMinimapSubsystem.h"    // ★ ширина окна мини-карты для авто-якоря (эталон GetSize(g_pMiniMapWnd))
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaSkillUseSubsystem.h"   // ★ ОБЩИЙ диспетчер применения: ряды Хранителя ходят через него
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaWorldSubsystem.h"   // ★ 3D-компаньон хранителя: наличие → высота плашки, каст → мотион
#include "World/AsdaSoulGuardianActor.h" // ★ П7: мотион 5 по ОБЫЧНОМУ навыку владельца (вариант 0)
// ★ цель-МОБ как точка вылета монеты (FindNetMobByRid отдаёт AAsdaMobActor*)   ★ R3: комментарий снятого включения "Npc/AsdaMobActor.h" (файл им не пользовался) — сохранён
#include "AsdaNetSubsystem.h"

#include "Components/Image.h"
#include "Engine/DataTable.h"
#include "Kismet/GameplayStatics.h"
#include "Framework/Application/SlateApplication.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "AsdaSoulGuardSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaSoulGuardSubsystem*.cpp

// ─────────────────────────────── сеть ───────────────────────────────────────────────────────────
// `recvSkillSlotState@0xEF6B10` (network.c:33977) 1:1.
void UAsdaSoulGuardSubsystem::OnSlotState6157(int32 UserIdx, int32 AddFlag, int32 InSkillType, int32 SkillIndex,
                                              int32 C0, int32 C1, int32 C2)
{
	EnsureControls();
	++NetCnt6157; LastCoinAddFlag = AddFlag;
	// Эталон userIdx НЕ проверяет — пакет 6157 сервер шлёт только владельцу (sub_4C28F0 ... a2 = uid).
	//   Логируем расхождение, но обрабатываем: так же поступает клиент.
	const int32 Self = SelfUserIdx();
	if (Self >= 0 && UserIdx != Self)
	{
		UE_LOG(LogAsdaSoul, Verbose, TEXT("[хранитель] 6157 про userIdx=%d (мой %d) — обрабатываем, как эталон"),
			UserIdx, Self);
	}
	// Самый первый пакет: если все три счётчика были нулями, значения ставятся СРАЗУ.
	if (Coin[0] == 0 && Coin[1] == 0 && Coin[2] == 0)
	{
		Coin[0] = C0; Coin[1] = C1; Coin[2] = C2;
		OnCoinsChanged.Broadcast(C0, C1, C2);
	}
	if (AddFlag == 0) { UpdateArrowIndicators(C0, C1, C2); }
	if (AddFlag == 1)
	{
		// sub_92DC3F(obj, SkillIndex) = аксессор SkillCoinTable по индексу (bounds-check, результат
		//   в этом месте эталон отбрасывает) — воспроизводим как диагностику.
		int32 K0 = 0, K1 = 0, K2 = 0;
		if (GetSkillCoins(SkillIndex, K0, K1, K2))
		{
			UE_LOG(LogAsdaSoul, Verbose, TEXT("[хранитель] скилл %d по таблице даёт %d/%d/%d"), SkillIndex, K0, K1, K2);
		}
		SpawnFlyingCoin(C0, C1, C2, SkillIndex);
		LoadRowSkills(InSkillType);
	}
	if (AddFlag == 2) { UpdateArrowIndicators(C0, C1, C2); }
	RefreshIcons();
	RefreshOrHide();
	// ★ Эмулятор: сервер после КАЖДОГО изменения монет зовёт sub_48B210 (пересчёт уровня → 6156 →
	//   наложение/снятие пассивного баффа). При живом сервере это делает он, а мы только слушаем.
	if (bSimulateServer && AddFlag != 1) { SimRecomputeGuard(); }
}

// `charHandleSlotKeyPacket@0xEF6D00` + применение в `charVisibilityStateRefresh@0xEF72C0`.
// ★★★ Ключ пакета — charId (`CUser+372`), сверяем с `SelectedCharId` (char-select). Раньше здесь
//   сверялся сессионный userIdx — на живом сервере он НИКОГДА не совпал бы, и плашка/уровень
//   хранителя своего игрока не обновлялись бы (для соседей запись просто ложилась в карту).
void UAsdaSoulGuardSubsystem::OnSoulGuardIndex6156(int32 CharId, int32 SoulGuardIdx)
{
	EnsureControls();
	++NetCnt6156;
	SoulGuardIndexByUser.Add(CharId, SoulGuardIdx);
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const int32 SelfChar = Net ? Net->SelectedCharId : 0;
	if (CharId < 0 || SelfChar == 0 || CharId == SelfChar)
	{
		SoulGuardIndex = SoulGuardIdx;
		const FAsdaSoulGuarderRow* R = FindGuarderByIndex(SoulGuardIdx);
		UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] призван индекс %d%s"),
			SoulGuardIdx, R ? *FString::Printf(TEXT(" («%s», тип %d, ряд %d, бафф %d, скилл %d)"),
				*R->Name, R->SkillType, R->RowIndex + 1, R->BuffSkillId, R->GuardianSkillId) : TEXT(""));
		OnGuardIndexChanged.Broadcast(SoulGuardIdx);
		return;
	}
	// ★ ОТСТУПЛЕНИЕ (bPeerBanners): надпись «Душа Хранит N» над СОСЕДОМ. Уровень берём из той же
	//   записи SoulGuarderTable (+8 RowIndex), что и своя плашка — ни одного нового источника.
	if (!bPeerBanners) { return; }
	const FAsdaSoulGuarderRow* PR = FindGuarderByIndex(SoulGuardIdx);
	if (!PR) { return; }                       // −1/нет записи = хранитель снят → надписи нет (как у себя)
	UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	AActor* Peer = WS ? WS->FindPlayerActorByCharId(CharId) : nullptr;
	if (Peer) { ShowPeerBanner(Peer, CharId, -1, PR->RowIndex, /*bGuard=*/true); }
}

// ═══════ ★★★ СЕТЕВОЙ ТРАКТ ПРИМЕНЕНИЯ СКИЛЛА ХРАНИТЕЛЯ — СВОЙ И ЧУЖИЕ (2026-08-08) ═════════════
// Разбор по обеим сторонам:
//   C→S 6158 `CSoulGuarderManager::RecvChar_SoulGuarderSkillUse@0x48E4F0` (GameServer.c:56935):
//     гейты; при отказе → 6159 (8 б, `v26` = код) ТОЛЬКО кастеру (`sub_4C28F0`); при успехе →
//     4159 соседям (запись кулдауна, 32 б), при первом призыве 6156 по сектору, и запрос
//     8940 на BattleServer.
//   S→C после ответа BattleServer, `RecvBattle_SoulGuarderSkillUse@0x48F1B0` (:57268):
//     6161 (7 б) по СЕКТОРУ → `sub_48B570(uid, ряд, 5)` (СПИСАНИЕ 5 монет → 6157 AddFlag 0 владельцу)
//     → 6156 по сектору, если хранитель ещё не призван → 6159 (8 + 29·N) по СЕКТОРУ.
//     Ветка «долгого каста» (motionType 1/7) вместо 6159 шлёт 6160 (7 + 29·N).
//   Рассылка «по сектору» = `sub_52E630` → `SendPacket_ClippingSectorInAllUser` (:114720/:13299) —
//     кастер НЕ исключается, поэтому свой каст приходит тем же пакетом, что и чужой.
void UAsdaSoulGuardSubsystem::OnSkillCast6159(int32 Result, int32 CasterUserIdx, int32 SkillCode,
                                              int32 Flag, int32 HitCount)
{
	++NetCnt6159; LastCastResult = Result; LastCastSkill = SkillCode; LastCastUser = CasterUserIdx;
	const bool bMine = (CasterUserIdx == SelfUserIdx());
	// ★ Ряды Хранителя идут через ОБЩИЙ диспетчер, значит он держит `ActiveSkill`, а ответом на
	//   его заявку служит 6159/6161, а не 5257/5271. Снимаем активность сразу, иначе гейт 7
	//   («тот же навык уже применяется») держал бы ряд до истечения сторожа 2000 мс.
	if (bMine && IsGuardianSkillCode(SkillCode))
	{
		if (UAsdaSkillUseSubsystem* SU = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr)
		{
			SU->ClearActiveSkill(SkillCode, Result == 1 ? TEXT("6159 успех") : TEXT("6159 отказ"));
		}
	}
	if (Result != 1)
	{
		// Эталон (`Recv_SkillCast`, network.c:44958-44964): гасит поле каста ЛЮБОМУ кастеру
		//   (`pChar->effectSlots[100].field_4 = -1` стоит ДО гейта), а нотис `dword_15B5D78[result]`
		//   печатает ТОЛЬКО локальному игроку (`CClass_GetField47565`).
		if (UAsdaSkillUseSubsystem* SU = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr)
		{
			SU->CancelSkillVisualFor(CasterUserIdx, TEXT("отказ (6159)"));
		}
		if (bMine) { PostSkillFailNotice(Result, SkillCode); }
		UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] 6159 ОТКАЗ: код=%d скилл=%d кастер=%d%s"),
			Result, SkillCode, CasterUserIdx, bMine ? TEXT(" (мой)") : TEXT(""));
		return;
	}
	// ★★★ П7 (2026-08-11): КОМПАНЬОН РЕАГИРУЕТ И НА ОБЫЧНЫЕ НАВЫКИ ВЛАДЕЛЬЦА.
	//   Эталон `UpdateList_ModelAnim@0xFA7CE0` (`entity_char.c:61304-61332`): при СМЕНЕ состояния
	//   владельца на 0x18/0xE/0x1B/0xD (24/14/27/13) и заведённом слоте показа
	//   (`effectSlots[100].field_4 != -1`) выполняется
	//       if (effectSlots[slot].activeId > 0x4B1) { …SkillCoinTable[+16] → мотион 5/10/11… }
	//       else                                     Char_SetMotion(angel, 5, 0, 0);
	//   ⇒ ветка `else` покрывает ВСЕ обычные навыки (код ≤ 1201), и мотион там 5 БЕЗ эффекта слота.
	//   Порт же выходил здесь `return`, и пока игрок кастовал обычные навыки, хранитель стоял в idle.
	//   Пакет 5257 (принят, состояние 24) приходит тем же делегатом, что и 6159, — этим и пользуемся.
	if (!IsGuardianSkillCode(SkillCode))
	{
		QueueNormalSkillMotion(CasterUserIdx, SkillCode, TEXT("5257"));
		return;
	}
	QueueGuardianMotion(CasterUserIdx, SkillCode, TEXT("6159"));
	// ★★★ СВОЙ КАСТ = ПОЛНЫЙ ЦИКЛ ПОКАЗА (полоса применения + клип игрока + эффекты).
	//   6159 для хранителя — это то же, что 5257 для обычного навыка: эталон заводит слот показа
	//   и ставит владельцу состояние 24. Доказательство «из другого конца»: реакция компаньона
	//   (`soul_guardian_companion_re.md` §7.2) срабатывает ТОЛЬКО при состоянии владельца
	//   13/14/24/27 И `activeSlot != −1` — то есть в момент 6159 у владельца оба поля заведены.
	//   Без этого вызова полоса не зажигалась и игрок стоял без анимации (живой тест 2026-08-10).
	// ★★★ И ЧУЖОЙ КАСТ ХРАНИТЕЛЯ ТОЖЕ (закрытие дыры отчёта §28.8): 6159 приходит секторным
	//   броадкастом, эталонный `Recv_SkillCast@0xF4E370` ищет кастера через
	//   `CharManager_FindByUserIdx` и ставит показ ЛЮБОМУ найденному персонажу. Локальными внутри
	//   `OnGuardianCastAccepted` остаются полоса применения и наш диспетчер заявок.
	if (UAsdaSkillUseSubsystem* SU = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr)
	{
		SU->OnGuardianCastAccepted(CasterUserIdx, SkillCode, /*bIdle=*/false);
	}
	UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] 6159 УСПЕХ: скилл %d, кастер userIdx=%d%s, целей %d, флаг %d"),
		SkillCode, CasterUserIdx, bMine ? TEXT(" (мой)") : TEXT(" (СОСЕД)"), HitCount, Flag);

	// ★ ОТСТУПЛЕНИЕ: «Душа Взрыв N» в МОМЕНТ КАСТА. Эталон так НЕ делает — у него «Взрыв»
	//   показывается по достижению 5 монет в ряду (ui_core.c:92385-92412), а каст не показывает
	//   ничего. Своему игроку — только при bBurstOnCast (по умолчанию 0 = эталон);
	//   соседу — при bPeerBurstOnCast, потому что эталонного повода для него не существует
	//   в принципе (счётчики монет чужого игрока клиенту не приходят).
	int32 CastRow = -1;
	if (!RowByGuardianSkill(SkillCode, CastRow)) { return; }
	if (bMine)
	{
		if (!bBurstOnCast) { return; }
		if (bControlsReady && GetLocalChar())
		{
			BannerRow = -1;   // погасить текущую, чтобы SwitchTo на следующем кадре стартовал заново
			ActiveBurstSlot = -1;
			ShowBannerTriple(CastRow, /*bGuard=*/false, true);
			SetBannerTripleAlpha(CastRow, /*bGuard=*/false, 0.f);
			BannerRow = CastRow; bBannerIsGuard = false; BannerAlpha = 0.f; bBannerFadeOut = false;
		}
		return;
	}
	if (!bPeerBurstOnCast) { return; }
	UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (AActor* Peer = WS ? WS->ActorByUserIdx(CasterUserIdx) : nullptr)
	{
		ShowPeerBanner(Peer, 0, CasterUserIdx, CastRow, /*bGuard=*/false);
	}
}

void UAsdaSoulGuardSubsystem::OnSkillCastIdle6160(int32 CasterUserIdx, int32 SkillCode, int32 Flag, int32 HitCount)
{
	++NetCnt6160;
	// ★★★ П7: 5270 «долгий каст» ставит владельцу состояние **14** — оно тоже в наборе
	//   `case 0x18/0xE/0x1B/0xD` эталонного `UpdateList_ModelAnim`, значит компаньон обязан
	//   реагировать и на него (мотион 5 без эффекта слота).
	if (!IsGuardianSkillCode(SkillCode))
	{
		QueueNormalSkillMotion(CasterUserIdx, SkillCode, TEXT("5270"));
		return;
	}
	QueueGuardianMotion(CasterUserIdx, SkillCode, TEXT("6160"));
	// ★ 6160 = «долгий каст» (motionType 1/7) и полный аналог 5270: состояние 14, слот заведён,
	//   полоса горит, показ запущен (эталонный `Recv_SkillCastIdle` грузит клип ТЕМ ЖЕ массивом 0).
	// ★★★ Показ — любому кастеру (см. 6159 выше); полоса и диспетчер внутри — только своему.
	if (UAsdaSkillUseSubsystem* SU = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr)
	{
		SU->OnGuardianCastAccepted(CasterUserIdx, SkillCode, /*bIdle=*/true);
	}
	UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] 6160 начало каста: скилл %d, кастер userIdx=%d, целей %d, флаг %d"),
		SkillCode, CasterUserIdx, HitCount, Flag);
}

// 6161 (хранитель) / 5271 (обычные скиллы). Эталонный `recvOtherCharSkillUse` мотиона НЕ ставит —
//   он только гасит поле каста и при result != 1 печатает нотис. Значит боевой мотион компаньона
//   мы вешаем на 6159/6160, а здесь ничего не дублируем (иначе удар сыграл бы дважды).
void UAsdaSoulGuardSubsystem::OnSkillCastState(int32 Result, int32 UserIdx, int32 SkillId, int32 Flag, bool bGuardian)
{
	if (!bGuardian) { return; }
	++NetCnt6161;
	const bool bMine = (UserIdx == SelfUserIdx());
	if (Result != 1)
	{
		// ★★★ Показ гасится ЛЮБОМУ кастеру (`field_4 = -1` стоит снаружи гейта «свой игрок»,
		//   network.c:44634); нотис отказа — только своему.
		if (UAsdaSkillUseSubsystem* SU = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr)
		{
			SU->CancelSkillVisualFor(UserIdx, TEXT("отказ (6161)"));
		}
		if (bMine) { PostSkillFailNotice(Result, SkillId); }
		UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] 6161 ОТКАЗ: код=%d скилл=%d userIdx=%d%s"),
			Result, SkillId, UserIdx, bMine ? TEXT(" (мой)") : TEXT(""));
		return;
	}
	UE_LOG(LogAsdaSoul, Verbose, TEXT("[хранитель] 6161: каст скилла %d подтверждён (userIdx=%d, флаг=%d)"),
		SkillId, UserIdx, Flag);
}

// Общая точка: код скилла → вариант боевого мотиона → ЗАЯВКА миру (применится в его тике).
//   ★ ЗАКОН no-reentrant-net-callbacks: мы внутри разбора кадров, актора трогать нельзя.
void UAsdaSoulGuardSubsystem::QueueGuardianMotion(int32 CasterUserIdx, int32 SkillCode, const TCHAR* Src)
{
	int32 Variant = -1;
	FAsdaSkillCoinRow CoinRow;
	if (FindSkillCoin(SkillCode, CoinRow)) { Variant = CoinRow.Field16; }
	if (Variant < 1 || Variant > 3)
	{
		// Формула таблицы (проверена на всех 54 id 1201..1254): вариант = ((id − 1201) % 3) + 1.
		//   Фолбэк на случай, если DataTable ещё не загружен.
		Variant = ((SkillCode - 1201) % 3) + 1;
		UE_LOG(LogAsdaSoul, Warning, TEXT("[хранитель] %s: SkillCoinTable[%d].Field16 недоступен — вариант по формуле: %d"),
			Src, SkillCode, Variant);
	}
	if (UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
	{
		WS->QueueSoulGuardianSkill(CasterUserIdx, Variant);
	}
}

// ★★★ П7: ОБЫЧНЫЙ навык владельца → компаньону мотион 5 БЕЗ эффекта слота.
//   Эталон `entity_char.c:61329-61331`: ветка `else` от `activeId > 0x4B1` — `Char_SetMotion(angel, 5, 0, 0)`.
//   `SkillCoinTable` здесь НЕ читается вовсе (эталон лезет в неё только при `activeId > 1201`),
//   поэтому вариант строго 0 — у `AAsdaSoulGuardianActor::NotifyGuardianSkill` это ветка `default`.
//   ⚠ Через `UAsdaWorldSubsystem::QueueSoulGuardianSkill` вариант 0 не проходит (там гейт 1..3 —
//   он верен для УДАРА хранителя), поэтому держим собственную очередь и разбираем её в `Tick`:
//   на момент прихода пакета компаньон мог быть ещё не создан (6156 приходит отдельно).
void UAsdaSoulGuardSubsystem::QueueNormalSkillMotion(int32 CasterUserIdx, int32 SkillCode, const TCHAR* Src)
{
	PendingNormalSkillMotion.Add(CasterUserIdx);
	UE_LOG(LogAsdaSoul, Verbose,
		TEXT("[хранитель] %s: обычный навык %d кастера userIdx=%d — компаньону мотион 5 (эталон "
		     "UpdateList_ModelAnim, ветка activeId <= 1201)"), Src, SkillCode, CasterUserIdx);
}

void UAsdaSoulGuardSubsystem::DrainNormalSkillMotions()
{
	if (PendingNormalSkillMotion.Num() == 0) { return; }
	TArray<int32> Batch = MoveTemp(PendingNormalSkillMotion);
	PendingNormalSkillMotion.Reset();
	UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!WS) { return; }
	for (int32 Uid : Batch)
	{
		AActor* A = WS->ActorByUserIdx(Uid);
		if (!A) { continue; }
		if (AAsdaSoulGuardianActor* G = WS->FindSoulGuardianByOwner(A))
		{
			G->NotifyGuardianSkill(/*Variant=*/0);   // мотион 5, эффекта слота нет
			UE_LOG(LogAsdaSoul, Log,
				TEXT("[хранитель-3D] мотион 5 по ОБЫЧНОМУ навыку владельца userIdx=%d (%s)"),
				Uid, *A->GetName());
		}
	}
}

// Нотис отказа: эталон `UI_FormatLocalizedNotice(ctx, dword_15B5D78[result], 0, 1)` + вывод в чат
//   цветом −11514 на 10 000 мс (network.c:44958-44964 / 44630-44636).
void UAsdaSoulGuardSubsystem::PostSkillFailNotice(int32 ResultCode, int32 SkillCode) const
{
	const int32 LocId = UAsdaNetSubsystem::SkillFailNoticeLocId(ResultCode);
	if (LocId == 0)
	{
		UE_LOG(LogAsdaSoul, Warning, TEXT("[хранитель] отказ каста, код %d (скилл %d): в эталонной таблице нотиса нет"),
			ResultCode, SkillCode);
		return;
	}
	UAsdaMessageSubsystem* Msg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr;
	if (!Msg) { return; }
	FAsdaMsgParams P;
	P.Category = EAsdaMsgCategory::SkillRefused;
	P.LocId    = LocId;
	P.TtlMs    = 10000.f;
	// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): RU-фолбэк убран — нет строки → «[#id]» (общая точка UAsdaMessageSubsystem).
	Msg->Post(P);
}

// `hudQuickSlot_TryUseSkill@0xEF4600` (ui_core.c:92674) 1:1.
bool UAsdaSoulGuardSubsystem::TryUseSkill(int32 Row)
{
	const double Now = FPlatformTime::Seconds();
	if ((Now - LastUseTickS) * 1000.0 <= (double)UseCooldownMs) { return false; }
	LastUseTickS = Now;
	if (Row < 0 || Row > 2) { return false; }

	if (!IsRowAvailable(Row))
	{
		PostNotAvailableNotice();
		return false;
	}
	if ((Now - LastMapCheckTickS) * 1000.0 <= (double)MapCheckCooldownMs) { return false; }
	LastMapCheckTickS = Now;

	const AAsdaPlayerCharacter* Ch = GetLocalChar();
	if (!Ch) { return false; }
	// ★★★ ПРАВИЛО «НЕТ ДАННЫХ → НЕ БЛОКИРУЕМ»: пока источник значения не подключён (−1), гейт
	//   обязан вести себя как «условие не выполнено». Иначе неизвестное поле молча запрещает игру.
	if (CurrentMapType < 0)
	{
		if (!bWarnedNoMapType)
		{
			bWarnedNoMapType = true;
			UE_LOG(LogAsdaSoul, Warning, TEXT("[хранитель] ГЕЙТ ПРОПУЩЕН: тип карты (Net_GetCurrentMapType) в порту не заполняется ")
				TEXT("— запрет по типам 13/14/15/24/25/27 НЕ применяется. Задать вручную: asda.Soul.MapType <n>"));
		}
	}
	else if (BannedMapTypes.Contains(CurrentMapType))
	{
		UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] тип карты %d запрещает скилл хранителя (эталон)"), CurrentMapType);
		return false;
	}
	// Эффект-блокировщик 547 (эталон CList_FindByTypeSub(char+2552, 547, 0)).
	if (const UAsdaEffectsSubsystem* Fx = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr)
	{
		if (Fx->FindByTypeSub(SelfUserIdx(), BlockingBuffId, 0))
		{
			UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] активен эффект %d — скилл хранителя запрещён (эталон)"), BlockingBuffId);
			return false;
		}
	}

	const int32 Code = GetRowSkillCode(Row);
	if (Code == -1)
	{
		UE_LOG(LogAsdaSoul, Warning, TEXT("[хранитель] у ряда %d нет скилла (сервер не прислал SkillType)"), Row + 1);
		return false;
	}
	// ★★★★★ ПРАВКА 2026-08-10 — ЗАЯВКУ СОБИРАЕТ ОБЩИЙ ДИСПЕТЧЕР, А НЕ ЭТА ПОДСИСТЕМА.
	//   Эталонный `hudQuickSlot_TryUseSkill@0xEF4600` (ui_core.c:92707-92711) после гейтов ряда
	//   зовёт ТОТ ЖЕ диспетчер, что и обычные навыки:
	//       if (skillinfo_GetByteFieldAt42(code) == 2) combatHandleSkillUse((int*)g_pSkillWnd, a2, code, 0);
	//       if (skillinfo_GetByteFieldAt42(code) == 1) skillUseOnTargetDispatch((int*)g_pSkillWnd, a2, code);
	//   Отдельного отправителя 6158 у эталона НЕТ: опкод выбирается внутри общих отправителей
	//   строкой `if (skillRecord->skillCode < 1201) 5256 else 6158`.
	//
	//   ★★★ ЧТО БЫЛО СЛОМАНО (живой лог 2026-08-10 07:33:41-49, шесть заявок подряд).
	//   Прежний код звал `SendSoulGuardSkillUse(...)`, где **тип цели ЖЁСТКО = 1** («персонаж»),
	//   а индекс брался из выбранной цели — то есть в записи ехало `rec[0] = 1` вместе с
	//   **renderId МОБА** (161, 223). Сервер `RecvChar_SoulGuarderSkillUse@0x48E4F0` ещё ДО switch
	//   по типу цели прогоняет PvP-предпроверку (GameServer.c:57017-57022):
	//       if (rec[0] == 1 && a2 != rec[1] && sub_5328F0(rec[1]) && !sub_556DB0(mgr, a2, rec[1], code))
	//           { v26 = 0; goto LABEL_106; }        // ← 6159 с кодом 0
	//   где `sub_5328F0(x) = (unsigned)x < 0x226` — это ПРОСТО диапазон 0..549, а не «игрок есть»,
	//   а `sub_556DB0` первым делом делает `sub_5324A0(mgr, rec[1])` и при отсутствии игрока с таким
	//   сессионным индексом возвращает 0. Итог: отказ 0 на КАЖДУЮ заявку.
	//   Живой пакет-доказательство: `6159 body=[00 FF FF 00 00 00 FF FF]` — код 0, кастер −1,
	//   скилл 0. Оба «пустых» поля значат, что отказ случился ДО строк `v27 = a2;` и
	//   `v24 = SoulGuarderSkillTable[...]`, то есть ровно в этой предпроверке (инициализатор
	//   `sub_4742F0` кладёт −1/−1, а `v24` инициализирован нулём).
	//
	//   ★★★ ЧТО ПРАВИЛЬНО. Живая серверная таблица
	//   `\\@@ASTORY_SERVER_HOSTNAME@@\h(@@ASTORY_SERVER_HOSTNAME@@_ns)\ASDA\Working_ASDA\v2\GameServer\SkillTable_Info\SoulGuarderSkillTable.txt`
	//   (54 строки; колонка 5 = поле `+46`, по которому и идёт серверный switch) даёт для
	//   **1228..1254 → targetSlotType 6** («на моба ИЛИ игрока»), castType 3, range 2..15,
	//   resourceType 1/3. Клиентский `skill_info.sif` совпадает бит-в-бит (kind 2, subType 2).
	//   ⇒ по мобу запись обязана быть `(тип 0, renderId)`, по игроку — `(тип 1, userNum)`.
	//   Именно это и собирает `SendUse`, поэтому дублировать сборку здесь нельзя.
	//   ⚠ Записи 1201..1227 — это АУРЫ (subType 5, slot 1), рядами не применяются.
	UAsdaSkillUseSubsystem* SU = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr;
	if (!SU)
	{
		UE_LOG(LogAsdaSoul, Warning, TEXT("[хранитель] нет UAsdaSkillUseSubsystem — скилл %d не применён"), Code);
		return false;
	}
	// ★ Ручное переопределение цели (`asda.Soul.Target <idx>`) осталось: оно проверяет ветку без мыши.
	if (OverrideTargetUserIdx >= 0)
	{
		UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] ручная цель asda.Soul.Target=%d — ОБЩИЙ диспетчер её не читает, ")
			TEXT("выбери цель мышью либо сбрось значение в −1"), OverrideTargetUserIdx);
	}
	const bool bSent = SU->UseGuardianSkill(Code);
	if (bSent) { ++NetSent6158; }
	UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] ряд %d: скилл %d, монет %d → общий диспетчер (6158) %s"),
		Row + 1, Code, GetCoin(Row), bSent ? TEXT("принял") : TEXT("ОТКЛОНИЛ (см. лог LogAsdaSkillUse)"));
	// ★ 3D-КОМПАНЬОН: боевой мотион. В ЭТАЛОНЕ он ставится НЕ здесь, а по возврату сервера:
	//   6159/6160 → `CChar_SetState(..., 24/14)` → `UpdateList_ModelAnim` следующим кадром читает
	//   `SkillCoinTable[activeId] +16` и зовёт `Char_SetMotion(angel, 5/10/11, spawnFx=1)`
	//   (entity_char.c:61304-61332). Значит при ЖИВОМ сервере предсказывать нельзя: сервер может
	//   отказать (нет монет, кулдаун, гейт карты), и удар сыграл бы «в пустоту».
	//   Предсказание оставлено ТОЛЬКО для офлайн-проверки (нет соединения) и ручного режима.
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bPredict = (PredictGuardMotionMode > 0)
		|| (PredictGuardMotionMode < 0 && !(Net && Net->IsConnected()));
	if (bPredict)
	{
		UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] мотион предсказан локально (сервера нет; режим %d)"),
			PredictGuardMotionMode);
		NotifyGuardianSkillUsed(Code);
	}
	return bSent;
}

// Вариант боевого мотиона хранителя по id скилла: поле +16 клиентской SkillCoinTable.
//   Эталон читает ровно это поле (sub_EF3FA0 → rec[+16]); у обычных скиллов там −1 → «просто мотион 5».
void UAsdaSoulGuardSubsystem::NotifyGuardianSkillUsed(int32 SkillId)
{
	int32 Variant = -1;
	FAsdaSkillCoinRow CoinRow;
	if (FindSkillCoin(SkillId, CoinRow)) { Variant = CoinRow.Field16; }
	UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!WS) { return; }
	// ★ Реестр компаньонов ключуется charId (ключ пакета 6156). Своего берём из char-select;
	//   если его ещё нет (офлайн-проверка), сработает фолбэк FindSoulGuardian по userIdx.
	const UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	const int32 SelfKey = (Net && Net->SelectedCharId != 0) ? Net->SelectedCharId : SelfUserIdx();
	WS->NotifySoulGuardianSkill(SelfKey, Variant);
}

void UAsdaSoulGuardSubsystem::PostNotAvailableNotice() const
{
	UAsdaMessageSubsystem* Msg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr;
	if (!Msg) { return; }
	FAsdaMsgParams P;
	P.Category = EAsdaMsgCategory::SkillRefused;
	P.LocId    = NotAvailableLocId;      // эталон 112503, белым, 10 000 мс
	P.TtlMs    = 10000.f;
	// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): RU-фолбэк убран — строка 112503 заведена в DT_GameText (ведомость gametext_overrides.json).
	Msg->Post(P);
}

// ─────────────────────────────── тик ────────────────────────────────────────────────────────────
void UAsdaSoulGuardSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.SoulGuard");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	// ★ П7: разбор очереди «обычный навык владельца → мотион 5 компаньону». Стоит ДО гейта
	//   готовности контролов: реакция 3D-компаньона к окну 56000 отношения не имеет, а сам гейт
	//   в эталоне относится только к HUD-части (`byte+308`).
	DrainNormalSkillMotions();
	if (!EnsureControls()) { return; }
	// Порядок 1:1 с эталонным `cameraTarget_UpdateAbovePlayer@0xEF6790`.
	RefreshOrHide();
	UpdateBanner(DeltaTime);
	// ★★★ ПОЛЁТ МОНЕТ НЕ ГЕЙТИТСЯ ВИДИМОСТЬЮ ОКНА (симптом «несколько монет зависают до начисления
	//   новой»). Эталон `cameraTarget_UpdateAbovePlayer@0xEF6790` (`render_core.c:5811-5863`):
	//   `hudSkillPopup_RefreshOrHide(...)` — отдельно и ВЫШЕ, а точка прилёта и
	//   `coinDropMgr_UpdateFlyToPlayer(...)` стоят под гейтом `byte+308` (готовность HUD), к
	//   видимости окна 56000 отношения не имеющим.
	//   ЧТО ЛОМАЛОСЬ: счётчики применяет ТОЛЬКО прилёт монеты (`TickFlyingCoins` →
	//   `UpdateArrowIndicators`), а окно прячется, когда все три счётчика нули. Значит после распада
	//   монет до 0/0/0 окно гасло, тик полёта переставал вызываться — и всё, что было В ПОЛЁТЕ,
	//   ЗАМИРАЛО НАВСЕГДА. Разморозить могла только ветка «первый пакет» (все счётчики нули →
	//   значения ставятся сразу) или пакет распада/входа `AddFlag 0/2`, т.е. буквально «зависают до
	//   начисления новой». Тот же капкан давала портовая ветка `IsSelfDeadNow()` в `RefreshOrHide`.
	//   `UpdatePinnedPosition` теперь сам считает точку прилёта до выхода по `bWindowShown`.
	UpdatePinnedPosition();
	TickFlyingCoins(DeltaTime);
	TickPointer();   // наведение/клик по иконкам рядов + переключение чекбокса 56264

	// ★★★ СТРАХОВКА ОТ «ЗАЛИПШИХ» ВСПЫШЕК ШАРА (56331/56332/56333 — зелёный/синий/красный кружок).
	//   Эталон гасит их ТОЛЬКО событием 24 стрелки 56208 (`uiDialogMsgProc`, ui_core.c:92577-92582);
	//   ветка УБЫЛИ монет (`uiSlotState_UpdateArrowIndicators`, else) их не трогает вовсе — это в
	//   эталоне корректно, потому что стрелка гарантированно доигрывает. Если же у плана стрелки не
	//   окажется данных скина, событие не придёт, и кружок останется висеть поверх ряда — ровно тот
	//   симптом «применил скилл, а красная монета осталась». Дублируем гашение по времени.
	if (FlashHideAtS > 0.0)
	{
		const double Now = FPlatformTime::Seconds();
		if (Now >= FlashHideAtS)
		{
			FlashHideAtS = 0.0;
			SetVis(FlashRed, false); SetVis(FlashBlue, false); SetVis(FlashGreen, false);
		}
	}

	// ── эмулятор сервера (только когда включён вручную) ──
	if (bSimulateServer)
	{
		const double Now = FPlatformTime::Seconds();
		if (NextSimDecayS <= 0.0) { NextSimDecayS = Now + (double)DecayPeriodMs / 1000.0; }
		else if (Now >= NextSimDecayS)
		{
			NextSimDecayS = Now + (double)DecayPeriodMs / 1000.0;
			SimDecayTick();
		}
	}
	else { NextSimDecayS = 0.0; }
}
