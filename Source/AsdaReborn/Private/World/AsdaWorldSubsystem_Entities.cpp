// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWorldSubsystem_Entities.cpp — ★ 23.10.2026 часть UAsdaWorldSubsystem, вынесенная из AsdaWorldSubsystem.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaWorldSubsystem.plan.json).
//   Действие/эмоция 4025, добыча, GM-скрытие, смерть и варп, витальные значения, отказ боя, HP моба, создание мобов, AOI, чужие игроки, темп атаки.
//   #include — те же, что у AsdaWorldSubsystem.cpp, в том же порядке; общее — AsdaWorldSubsystemInternal.h.
#include "World/AsdaAttackExit.h"   // ★ 01.11.2026: 4024/400 — выход своего из действия атаки (AsdaLeaveAttackAction)
#include "World/AsdaWorldSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "UI/AsdaNotice.h"              // ★ 27.09.2026: плашка 221 для отказов 4007 (канал обработчика `Recv_EntityMove`)
#include "HAL/IConsoleManager.h"        // ключ `asda.Move.RefusePlank`
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д8/Д9: ресурс только через менеджер
#include "AsdaNetSubsystem.h"
#include "AsdaSelf.h"   // ★ «это я?» — единая точка своего номера (E2, 14.09.2026)
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaAttackRhythmSubsystem.h"   // ★ 15.10.2026: ключ отката ритма (темп предметов), кадры ударов своего замаха
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaChatSubsystem.h"            // ★ системный чат CHAT_NEW_SYSTEM + оверлеи A/B — все лут-строки
#include "UI/AsdaMessageSubsystem.h"         // ★★★ ЕДИНЫЙ маршрутизатор сообщений (категория → окно по таблице)
#include "Framework/Application/SlateApplication.h"
#include "Npc/AsdaMobActor.h"
#include "World/AsdaCoord.h"                 // ★ Аудит №3 (B1/B2): единый перевод координат и тайловая сетка
#include "World/AsdaPlayerController.h"      // снятие таргета при смерти цели-моба (эталон SetActionState(10))
#include "World/AsdaFloatingTextSubsystem.h" // цифры урона (порт CFloatingText)
#include "Resources/AsdaGameUserSettings.h"  // ★ 17.10.2026 (Ф5): режим показа цифр урона `DamageTextMode`
#include "Skill/AsdaSkillFxSubsystem.h"      // ★★★ стык «урон ↔ визуал»: момент удара навыка
#include "World/AsdaFxLightSettings.h"         // ★ волна 3 п.16 (15.09.2026): AsdaWeaponGlow — имя свечения заточки (одна таблица баз)
#include "UI/AsdaSkillDatabase.h"            // ★ Аудит №3: поля записи навыка — только отсюда
#include "UI/AsdaEffectsSubsystem.h"         // ★ состояния мобов: 4017 бафф-стеки и бафф отражения
#include "Resources/AsdaSoundSubsystem.h"    // звуки ударов (S_c_*normal1, фазовые звуки мобов)
#include "Engine/StaticMesh.h"
#include "TimerManager.h"
#include "World/AsdaNameplateComponent.h"   // над-головные плашки (порт JUI-окон 1:1)
#include "AsdaDataTableCache.h"             // AsdaDt::Cached — объекты таблиц имён для сильного удержания (HoldEntityNameTables)
#include "World/AsdaSoulGuardianActor.h"     // ★ компаньон-хранитель «ангел» (6156, soul_guardian_companion_re.md)
#include "Misc/ConfigCacheIni.h"             // GConfig — опции показа ангела (FxConfig)
#include "AsdaEffectActor.h"                 // эффекты персонажей: крылья/кольца soulmate (follow-кость)
#include "ProceduralMeshComponent.h"         // asda.VerifyWeaponGlow: чтение мировых вершин секций эффекта
// loc-нотисы подбора (1011/1012/1030)   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
// UAsdaRichText::ApplyFormatted (единая точка текста)   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "Login/AsdaLoginData.h"             // DecodeCp1251 (имена монстров в cp1251)
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectGlobals.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "World/AsdaCombatTrace.h"           // ★ 18.10.2026: журнал боя (ASDA_CTRACE) — полный цикл действий клиента в бою
#include "AsdaWorldSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWorldSubsystem*.cpp (LogAsdaWorld, журнал боя, чистые хелперы)
#include "World/AsdaCrouchStand.h"        // ★ 05.11.2026: присед соседа стоя — 4025 132/133 и код появления 132
// ★ 22.10.2026: окна при своём 4055/4056 — список эталона вместо CloseAll   ★ R3-c: комментарий снятого включения "UI/AsdaEntryWindows.h" (части им не пользуются)
// ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)   ★ R3-c: комментарий снятого включения "Resources/AsdaAssetPaths.h" (части им не пользуются)
// ★ волна 2 (14.09.2026): реестр карт — генерируемый заголовок из Tools/data/asda_map_registry.json (путь DA_WalkGrid)   ★ R3-c: комментарий снятого включения "World/AsdaMapRegistry.h" (части им не пользуются)
// кости/attach-матрицы оружия «в руке ↔ за спиной»   ★ R3-c: комментарий снятого включения "Characters/AsdaCharAssembly.h" (части им не пользуются)
// HERO_NOTICE_WIN — только чужой редкий дроп (6131)   ★ R3-c: комментарий снятого включения "UI/AsdaHudSubsystem.h" (части им не пользуются)
// ★ снаряды дальнобойного оружия (projectile_re.md)   ★ R3-c: комментарий снятого включения "World/AsdaProjectileActor.h" (части им не пользуются)
// ★ сырая запись skill_info (.sif +0x48 = buff0 состояния отражения)   ★ R3-c: комментарий снятого включения "Items/AsdaItemDbRows.h" (части им не пользуются)
// ★ единые читалки сырой записи (DataHex)   ★ R3-c: комментарий снятого включения "AsdaBytes.h" (части им не пользуются)
// подошва цели: низ ВИДИМОЙ геометрии (TargetSoleZ)   ★ R3-c: комментарий снятого включения "Components/MeshComponent.h" (части им не пользуются)
// ★ F7 (дедуп №2, В3): общие признаки «логин» / «загрузка»   ★ R3-c: комментарий снятого включения "World/AsdaWorldKind.h" (части им не пользуются)
// FAsdaHeldTables — удержание таблиц спауна (15.09.2026, dt_gc_hold)   ★ R3-c: комментарий снятого включения "AsdaHeldTables.h" (части им не пользуются)
// ★ дедуп №2 В1-C (F5/F6): теги эффектов персонажа, имена одиночных эффект-BP   ★ R3-c: комментарий снятого включения "AsdaFxNames.h" (части им не пользуются)
// ★ дедуп №2 В1-C (F10/F16): пути мастеров, имена параметров, экранные ассеты   ★ R3-c: комментарий снятого включения "Resources/AsdaMaterialNames.h" (части им не пользуются)
// RT эмблемы клана — это UTexture-наследник   ★ R3-c: комментарий снятого включения "Engine/TextureRenderTarget2D.h" (части им не пользуются)
#include "AsdaWorldSubsystem_Entities_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaWorldSubsystem_Entities*.cpp

// ★ 01.11.2026 [ОРИГИНАЛ]: 4025 код 0/400 соседа снимает его замах и очередь (разбор — в `OnCharAction`).
static ASDA_DEV_CVAR(int32, CVarAsdaPeerIdleEndsAttack,
	TEXT("asda.Combat.PeerIdleEndsAttack"), 1,
	TEXT("[ОРИГИНАЛ] 1 (по умолчанию) = 4025 код 0/400 соседа (вышел из атаки в покой) снимает его замах и, без признака 8, очередь ударов ")
	TEXT("(CChar_SetState 0, network.c:39645). 0 = прежнее: только поза покоя, живой замах соседа продолжается."),
	ECVF_Default);

// ★ КРУГ 10: размер клиентского AoI-сектора — НАСТРАИВАЕМ (эталонная логика на неэталонных данных).
//   Эталон: сектор = 3000 юнитов = 30 тайлов (= серверному сектору оригинала); таймер [147]=10000мс
//   рефрешится повторными 4013. НАШ сервер расширен до сектора 256 тайлов (карта 2×2) и повторных
//   4013 дальним мобам НЕ шлёт (по логу 02.08: между бёрстами 0 движковых 4013) → эталонный 10с-таймер
//   гасил ВСЕХ мобов вне ±1×30 тайлов ровно через 10с после входа («убил одного — остальные испарились»
//   = совпадение по времени).
// ★ КРУГ 10б (АВТО-режим, задача юзера «AoI автоматический для клиента»): клиент НЕ должен требовать
//   ручной подгонки под конфиг сервера. Семантика CVar:
//     0  = АВТО (ДЕФОЛТ): сектор оценивается по наблюдаемым применённым 4013/4019 —
//          Чебышёв-дистанция |mobTile−playerTile|; эффективный = max(30, максимум ЗА СЕССИЮ КАРТЫ).
//          30 = эталонный минимум ⇒ на стоковом сервере (сектор 30) поведение 1:1 с оригиналом.
//     >0 = ручной размер сектора в тайлах (форс, напр. 256 = наш расширенный сервер);
//     <0 = AoI-деспаун мобов/лута выключен.
//   Деспаун-условие ОСТАЛОСЬ эталонным («таймер 10с истёк И |Δсектор|>1») — меняется только размер.
// ★ КРУГ 11 (баг юзера «зашёл в город, вернулся в поле — мобы пропали»): исходный 10б держал порог
//   СКОЛЬЗЯЩИМ окном 60 с и СЖИМАЛ его к 30 при пустом окне. Но расширенный сервер шлёт каждого моба
//   в 4013 ОДИН раз (за 9 мин сессии 02.08 — ни одной повторной 4013, 4019 только регены ~10 шт);
//   в городе мобов нет → наблюдений нет → окно пустело за 60 с → порог сжимался 8 тайл/с до 30 →
//   полевые мобы (10с-таймер у всех давно истёк) проходили |Δсектор|>1 → МАССОВЫЙ AoI-деспаун,
//   а сервер повторную 4013 не шлёт → безвозвратно до перезахода. Дефект двойной: (а) «нет
//   наблюдений» ≠ «маленький AoI» — сжимать порог без данных нельзя; (б) деспаун при сервере без
//   ре-трансляции = потеря навсегда. ФИКС: порог МОНОТОННЫЙ — максимум наблюдений за сессию карты,
//   сжатия нет вообще; сброс к 30 при входе в мир (4003, новая карта/перезаход). Сток-сервер (>30
//   никогда не наблюдается) — по-прежнему 1:1; сервер с настоящим средним AoI и ре-трансляцией по
//   пересечению секторов (Send_SectorInNewAddMonsterInfo) — деспаун дальше 2×AoI работает, мобы
//   возвращаются серверным ре-трансом. Окно 60 с оставлено ТОЛЬКО как диагностика (asda.Mobs.Dump).
#if ASDA_DEV_CONSOLE   // ★ R3-c чистка: в Shipping этот ключ — константа из AsdaWorldSubsystem_Entities_Private.h
ASDA_DEV_CVAR(int32, CVarAsdaAoiSectorTiles,
	TEXT("Asda.Mobs.AoiSectorTiles"), kAsdaAoiSectorTilesDefault,
	TEXT("Клиентский AoI-сектор в тайлах: 0=АВТО (оценка по пакетам, мин. 30 = эталон), >0=ручной размер, <0=AoI-деспаун мобов/лута выключен."),
	ECVF_Default);
#endif // ASDA_DEV_CONSOLE

// ══════════ ★★★ 4025 — ДЕЙСТВИЕ/ЭМОЦИЯ ПЕРСОНАЖА (`Recv_CharSpawnFx@0xF3AF00`, network.c:39532) ══
// Сервер (`CharEMotionSync@0x546730`, S:124795) рассылает пакет соседям в секторе через
//   `SendPacket_ClippingSectorInOtherUser` — ОТПРАВИТЕЛЮ НЕ ПРИХОДИТ (`if (v14 != a3)` @0x41EA80; ★ волна В5 рыбалки
//   08.10.2026: прежнее «включая отправителя» было неверно, client_net.md §3.1). Своё действие ставится локально.
// Таблица `actionType → состояние` снята с эталона дословно (`Reports/char_info_window.md` §7):
//   0/400 → 0 (покой) · 1/2 → 1 (движение) · 100…106 → 33…39 · 107/108/109 → сесть/сидит/встать ·
//   110 → 40 (копка, «сабзиль») · 111 → синхронизация показа шлема · 112/113 → 41/42 ·
//   114 → 43 (копка-b) · 115 → 44 + цветки · 116 → 45 + мяч · 117 → 46 ·
//   122…129 → 60…67 (рыбалка: `pkt[1] − 62`, case 10…17 после `−112`, game.c:551733; ★ В5: прежнее «121…128 → 59…66»
//   было неверно) · 130 → 68 («добыча», минимум для рыбалки — В45fix2) · 300/301 → GM-невидимость (см. ветку ниже).
//   ★ В45fix2: 109 → 9 («встаёт», `EnterStandUpState`), а не 0.
// ★ 10.10.2026 (копание): итог копки 5430 — разбор у объявления.
void UAsdaWorldSubsystem::OnMiningResult(int32 Result, int32 UserIdx)
{
	AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(ActorByUserIdx(UserIdx));
	if (!Ch)
	{
		// Эталон: `if (pUpgradeTargetChar) { … }` — без персонажа ничего (и нотиса своему нет).
		UE_LOG(LogAsdaWorld, Log, TEXT("[копание] 5430 userIdx=%d — актора в мире нет, итог не показан (как эталон)"), UserIdx);
		return;
	}
	const bool bOk = (Result == 1);   // `upgradeMsg[0] == 1`
	if (Ch->IsLocalPlayerCharacter())
	{
		// `UI_FormatLocalizedNotice(…, 9003|9004, 0, 1)` → `CChat_AddTokenizedLines(g_pChatWnd, txt, −11514, 10000, 0)`.
		const int32 LocId = bOk ? 9003 : 9004;
		const FString Txt = AsdaNotice::Loc(GetGameInstance(), LocId);
		const bool bPosted = AsdaNotice::ChatColored(GetGameInstance(), EAsdaMsgCategory::System,
			Txt.IsEmpty() ? FAsdaLang::MissingTag(LocId) : Txt, AsdaNoticeColor::Gold, 10000.f);
		UE_LOG(LogAsdaWorld, Log, TEXT("[копание] нотис %d: %s (в системный чат: %s)"), LocId, *Txt, bPosted ? TEXT("да") : TEXT("НЕТ маршрутизатора"));
	}
	Ch->OnMiningResult5430(bOk);
}

void UAsdaWorldSubsystem::OnCharAction(int32 UserIdx, int32 ActionType, float GameX, float GameZ)
{
	AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(ActorByUserIdx(UserIdx));
	if (!Ch)
	{
		// Эталон при `!CharManager_FindByUserIdx` просто выходит — сосед мог ещё не заспавниться.
		UE_LOG(LogAsdaWorld, Verbose, TEXT("[действие] 4025 userIdx=%d actionType=%d — актора в мире нет"),
			UserIdx, ActionType);
		return;
	}

	// ── состояния покоя/движения ───────────────────────────────────────────────────────────────
	if (ActionType == 0 || ActionType == 400)
	{
		// ★ 01.11.2026 (баг 9, ревизия 4024/400) [ОРИГИНАЛ]: 4025 код 0/400 соседа — `LABEL_11` → `CChar_SetState(…, 0, 0, 0)`
		//   (network.c:39645/39871): мирный покой ЗАМАХ СОСЕДА СНИМАЕТ (код 400 его клиент шлёт ровно при выходе из атаки 5 → 0,
		//   `CCharacter_SetAction` case 0, entity_char.c:16545-16556), а при `(field11C & 8) == 0` сбрасывает признак серии и очередь
		//   ударов (entity_char.c:19301-19305). Порт ставил только позу покоя, а её гейт замаха (`SetAnimState`) живой цикл атаки
		//   не трогал — сосед, вышедший из атаки, продолжал «махать» до следующего пакета. Ключ `asda.Combat.PeerIdleEndsAttack`
		//   (1; 0 — прежнее: только поза).
		if (CVarAsdaPeerIdleEndsAttack.GetValueOnGameThread() != 0 && !Ch->IsLocalPlayerCharacter())
		{
			const bool bSwing = Ch->IsAttackLoopAlive() || Ch->IsAttackAnimActive();   // замах соседа шёл
			if (bSwing) { Ch->StopAttackAnim(); }
			Ch->AttackFaceTarget = nullptr;
			const uint8* MobFlags = PeerSeriesFlags.Find(UserIdx);                   // признак серии по мобу (`field11C`)
			const bool bMobReset = !MobFlags || (*MobFlags & 8) == 0;                 // очередь по мобу сбрасывается
			if (bMobReset)
			{
				PeerSeriesFlags.Remove(UserIdx); PeerSeriesTargetRid.Remove(UserIdx); ++PeerSeriesGen.FindOrAdd(UserIdx);
				if (TArray<int32>* Buf = DmgBuffers.Find(UserIdx)) { Buf->Reset(); }
			}
			const FAsdaPvpSeries* Ps = PvpSeries.Find(UserIdx);                        // PvP-серия соседа
			const bool bPvpKept = Ps && (Ps->Flags & 8) != 0;                          // PvP-очередь сохранена (признак 8)
			if (Ps && !bPvpKept) { ResetPvpSeries(UserIdx, ActionType == 400 ? TEXT("4025/400: сосед вышел из атаки") : TEXT("4025/0: покой соседа")); }
			ASDA_CTRACE_UID(UserIdx, EAsdaCT::Peer, TEXT("сосед uidx=%d: 4025 код %d — покой (CChar_SetState 0): замах %s, очередь по мобу %s, PvP-очередь %s"),
				UserIdx, ActionType, bSwing ? TEXT("снят") : TEXT("не шёл"), bMobReset ? TEXT("сброшена") : TEXT("сохранена (признак 8)"),
				!Ps ? TEXT("не было") : (bPvpKept ? TEXT("сохранена (признак 8)") : TEXT("сброшена")));
		}
		Ch->StopEmotionClip();
		Ch->SetAnimState(EAsdaPlayerState::Idle);
		return;
	}
	if (ActionType == 1 || ActionType == 2)
	{
		Ch->StopEmotionClip();
		Ch->SetAnimState(EAsdaPlayerState::Move);
		return;
	}

	// ── сесть / сидит / встать (эталон: состояния 7 / 8 / 9) ───────────────────────────────────
	if (ActionType == 107 || ActionType == 108)
	{
		Ch->StopEmotionClip();
		Ch->SetAnimState(EAsdaPlayerState::Sit);
		return;
	}
	if (ActionType == 109)
	{
		// ★ В45fix2 (ревизия В4/В5 рыбалки, МЕЛК-3, 09.10.2026): 109 = `CChar_SetState(…, 9)` «встаёт» (g:551808-551809), а не 0:
		//   прежний `SetAnimState(Idle)` ставил хвост вставания только из сидения и у рыбачащего соседа сразу снимал удочку
		//   (у эталона она висит до конца клипа 9 — флаг снимается лишь в ≤ 2/5/6, g:607922). Разбор — `EnterStandUpState`.
		Ch->EnterStandUpState();
		return;
	}
	// ★ В45fix2 (ревизия В4/В5 рыбалки, МЕЛК-5, 09.10.2026): 130 = `CChar_SetState(…, 68)` «добыча» (g:551554-551555) —
	//   переписывает действие рыбалки 60…67 (запись безусловна, g:76456). Система добычи в порту не портирована, сделан
	//   минимум 1:1 (действие снято, «занят», флаг и удочка остаются) — разбор и [ПОРТ] у `EnterEtalonState68`.
	if (ActionType == 130)
	{
		Ch->EnterEtalonState68();
		return;
	}
	// ★ Решение владельца 09.10.2026 (п. 5): 110 → `CChar_SetState(…, 40)` лопата (network.c:39958), 114 → 43 авто-лопата
	//   (network.c:39716): «занят», оружие за спину. ★ 10.10.2026 (копание): клип, удары, инструмент и стартовый эффект
	//   (`c-shovel-a_01` / `c-shovel-b_01`, матрица соседа без привязки) — `EnterEtalonShovelState` с источником «4025».
	if (ActionType == 110 || ActionType == 114)
	{
		Ch->EnterEtalonShovelState(ActionType == 110 ? 40 : 43, EAsdaShovelEntry::Peer4025);
		return;
	}

	// ── ★ 23.10.2026: 131 — ПРЫЖОК (перенос из донора AsdaFableClassic) ─────────────────────────────────
	//   [ПОРТ] Эталон AsdaStory кода 131 не знает (у него прыжка нет). Донор `Asda2.exe`: приём 4025 → `0x7E5B80`, switch по
	//   действию, 0x83 → `0x7E63CE`: «прыгает» = 1, время слотов 24/37 = 0; клип выбирает машина состояний соседа по его
	//   покою/ходу (`0x4AE120`). Курс из пакета донор кладёт до switch для любого действия — здесь им не крутим: ход соседа
	//   и так ведёт курс (`FaceGameDir`), а у стоящего он равен своему. Старый сервер код рассылает как есть (`0x546730`).
	if (ActionType == AAsdaPlayerCharacter::kJumpActionCode)
	{
		Ch->StartJump(/*bFromNet*/true);
		return;
	}

	// ── ★ 05.11.2026: 132 / 133 — ПРИСЕД СТОЯ соседа (порт; разбор — `World/AsdaCrouchStand.h`) ──────────────────────────
	//   [ПОРТ] Эталон и донор этих кодов не шлют. Стоя присед в 4006 не передать (сервер отвергает ход в ту же клетку) — сосед шлёт
	//   4024 132 «присел» / 133 «встал», сервер рассылает 4025 как есть. В ходу присед по-прежнему несёт статус 4007.
	if (ActionType == AsdaCrouchStand::kStandOnCode || ActionType == AsdaCrouchStand::kStandOffCode)
	{
		if (AsdaCrouchStand::RelayOn()) { Ch->SetNetCrouch(ActionType == AsdaCrouchStand::kStandOnCode); }
		return;
	}

	// ── эмоции 100…106 и 112…117 ───────────────────────────────────────────────────────────────
	int32 EmoteState = -1;
	if (ActionType >= 100 && ActionType <= 106) { EmoteState = 33 + (ActionType - 100); }
	else if (ActionType == 112)                 { EmoteState = 41; }
	else if (ActionType == 113)                 { EmoteState = 42; }
	else if (ActionType == 115)                 { EmoteState = 44; }
	else if (ActionType == 116)                 { EmoteState = 45; }
	else if (ActionType == 117)                 { EmoteState = 46; }

	if (EmoteState >= 0)
	{
		const float Len = Ch->PlayEmotionClip(EmoteState);
		if (Len <= 0.f)
		{
			UE_LOG(LogAsdaWorld, Warning, TEXT("[эмоция] userIdx=%d actionType=%d: клип состояния %d не проигран"),
				UserIdx, ActionType, EmoteState);
		}
		// ★ Сопутствующие эффекты эталона (ui_core.c:32455-32545 у автора, network.c:39731-39812 у соседей).
		// ★★★ 29.10.2026: реквизит — единой точкой со своим игроком (`UAsdaEffectsSubsystem::SpawnEmoteProps`): мяч attach-6
		//   (мировая матрица, network.c:39813), ДВА цветка на `_8`, снятие при выходе из эмоции. Только при живом клипе: без него
		//   у порта эмоции нет вовсе (клип отклонён), и реквизит висел бы на стоящем персонаже. Ключ `asda.Emote.Props 0` —
		//   прежняя ветка ниже.
		UAsdaEffectsSubsystem* FxSub = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
		if (FxSub && UAsdaEffectsSubsystem::IsEmotePropsEtalon())
		{
			if (Len > 0.f) { FxSub->SpawnEmoteProps(UserIdx, EmoteState); }
		}
		else if (UAsdaEffectsSubsystem* Fx = FxSub)
		{
			if (ActionType == 116)
			{
				// [прежний порт] Жонглирование: один эффект `M_Ball.jeb` на пустышке. ⚠ 29.10.2026: прочтение «эффект вешается на
				//   пустышку» неверно — индекс кости эталон действительно выбрасывает, но режим 6 кость и не читает: это мировая
				//   матрица персонажа (`effectNodeFollowPlayerBone@0xF58850`, миснейм IDA). Канон — `SpawnEmoteProps`.
				Fx->SpawnDummyBoneFx(UserIdx, TEXT("M_Ball"), FString());
			}
			else if (ActionType == 115)
			{
				// [прежний порт] Ухаживание: ЧЕТЫРЕ эффекта цветов на кость `_8`; набор по полу модели. ⚠ 29.10.2026: у эталона
				//   их ДВА (`*_flower01` + `*_flower02`, network.c:39747/39761 или 39778/39792) — канон в `SpawnEmoteProps`.
				const bool bMale = (Ch->Sex == 1);
				const TCHAR* A = bMale ? TEXT("M_flower01") : TEXT("F_flower01");
				const TCHAR* B = bMale ? TEXT("M_flower02") : TEXT("F_flower02");
				Fx->SpawnDummyBoneFx(UserIdx, A, FString(), EAsdaFxAttachMode::BoneFull, FName(TEXT("_8")));
				Fx->SpawnDummyBoneFx(UserIdx, B, FString(), EAsdaFxAttachMode::BoneFull, FName(TEXT("_8")));
				Fx->SpawnDummyBoneFx(UserIdx, A, FString(), EAsdaFxAttachMode::BoneFull, FName(TEXT("_8")));
				Fx->SpawnDummyBoneFx(UserIdx, B, FString(), EAsdaFxAttachMode::BoneFull, FName(TEXT("_8")));
			}
		}
		return;
	}

	// ── ★★★ 300/301 — GM-НЕВИДИМОСТЬ ЧУЖОГО ПЕРСОНАЖА ─────────────────────────────────────────
	//   ⚠ ИСПРАВЛЕНИЕ МИСНЕЙМА (закон false-negatives-method): прежний комментарий здесь называл
	//   300/301 «смертью/воскрешением» — это НЕВЕРНО. В эталоне `Recv_CharSpawnFx@0xF3AF00`
	//   (network.c:39634-39643) ветка ровно такая:
	//       case 300: CChar_SetState(ch, 0);  sub_9210D4(ch, 1);   // поднять бит 0 флагов char+0x40C
	//                 if (цель == этот чар && inputState == 0) { uiPanelHideAll(); state = 11; }
	//       case 301:                          sub_9305E8(ch, 1);   // снять тот же бит
	//   `sub_9210D4/9305E8` — тонкие обёртки над `*(DWORD*)(char+0x40C) |= 1` / `&= ~1`
	//   (`sub_9CE2D0`/`sub_9D00A0`), то есть чисто ПОКАЗ. Смерть в этом же switch отдельной ветки
	//   не имеет вовсе и идёт своими пакетами 4053/4055-4056.
	//   Источники состояния (все три сведены сюда): 4025 — живое переключение соседа;
	//   4103 — оператор ВОШЁЛ в нашу зону уже скрытым; сам себя клиент прячет по 6006.
	if (ActionType == 300 || ActionType == 301)
	{
		const bool bHide = (ActionType == 300);
		if (bHide)
		{
			// Эталон перед скрытием ставит состояние покоя — иначе персонаж «застыл бы» в
			//   недоигранной эмоции и вынырнул из невидимости в ней же.
			Ch->StopEmotionClip();
			Ch->SetAnimState(EAsdaPlayerState::Idle);
		}
		SetCharHiddenByGm(UserIdx, bHide);
		return;
	}

	// ── ★★★ 122…129 — РЫБАЛКА (волна В5, 08.10.2026): действие 60…67 соседа ─────────────────────────────
	//   Эталон (`Recv_CharSpawnFx@0xF3AF00`, game.c:551524-551525, 551733): направление `(pkt+5, 0, pkt+9)` →
	//   `rendererSetField5Vec3` (поворот к воде — его шлёт отправитель из `charEmoteSabzilAction`), затем
	//   `CChar_SetState(чужой, stateId − 62)`: клип по полу, клип удочки, при ВХОДЕ в 65/66 — fish_end_01/02.
	//   Реально приходят 122/124/127/128 (60/62/65/66). Удочку/рыбу персонаж вставляет сам по полям 6173.
	if (ActionType >= 122 && ActionType <= 129)
	{
		const FVector2D Dir(GameX, GameZ);
		if (!Dir.IsNearlyZero()) { Ch->FaceGameDir(Dir.GetSafeNormal()); }
		Ch->PlayFishAction(ActionType - 62, /*bSelf*/false);
		return;
	}

	// ── ★ 30.10.2026 [ОРИГИНАЛ] 121 → `CChar_SetState(…, 51)` «проигравший PvP» (`Recv_CharSpawnFx`, network.c:39819: `pkt[1] − 112`
	//   case 9) — жест поражения трека 26 один раз, затем покой. Прежде код уходил в журнал «обрабатывается своим каналом».
	if (ActionType == 121)
	{
		Ch->EnterCampPose(51, TEXT("4025 действие 121"));
		return;
	}

	// ── остальное: каналы, у которых в порту есть СВОИ системы (или их ещё нет) ────────────────
	//   111 — показ шлема; 110/114 — «сабзиль» (копка). (130 — состояние 68 «добыча», ветка выше — В45fix2.)
	UE_LOG(LogAsdaWorld, Log,
		TEXT("[действие] 4025 userIdx=%d actionType=%d — не эмоция; обрабатывается своим каналом"),
		UserIdx, ActionType);
}

void UAsdaWorldSubsystem::SetCharHiddenByGm(int32 UserIdx, bool bHidden)
{
	AActor* A = ActorByUserIdx(UserIdx);
	if (!A)
	{
		// Актора ещё/уже нет — оригинал в этом случае кладёт запись в отложенную очередь
		//   (`netHandlePersonaInteract` → `sub_91EF69`, разбор в `GameNet_OnUserSpawn@0x9CE750`).
		//   У нас роль этой очереди играет 4103: сервер шлёт его КАЖДОМУ, кто входит в зону
		//   оператора, поэтому состояние приедет заново вместе со спауном.
		UE_LOG(LogAsdaWorld, Log, TEXT("[GM-скрытие] userIdx=%d: актора в мире нет, состояние %s отложено"),
			UserIdx, bHidden ? TEXT("СКРЫТ") : TEXT("ВИДЕН"));
		if (bHidden) { GmHiddenUserIdx.Add(UserIdx); } else { GmHiddenUserIdx.Remove(UserIdx); }
		return;
	}

	if (bHidden) { GmHiddenUserIdx.Add(UserIdx); } else { GmHiddenUserIdx.Remove(UserIdx); }

	// ★★★ [ПОРТ] МЕТКА GM-СКРЫТИЯ на акторе (доводка P39, 27.09.2026): по ней эффекты, появившиеся на
	//   скрытом ПОЗЖЕ (бафф соседа, свечение при смене оружия, столб 4056…), гасят себя сами —
	//   `AAsdaEffectActor::Tick`, общая точка `kAsdaGmHiddenTagName` (AsdaEffectActor.h).
	{
		static const FName kGmTag(kAsdaGmHiddenTagName);
		if (bHidden) { A->Tags.AddUnique(kGmTag); } else { A->Tags.Remove(kGmTag); }
	}

	// ★★★ [ПОРТ] СВОЙ персонаж под hide — НЕ пропадает, а становится полупрозрачным.
	//   [ОРИГИНАЛ] эталон гасит бит показа и себе тоже (`localPlayerInputToggleByFlags@0xF49BD0`
	//   зовёт `CharManager_GetLocalPlayer()` → `sub_9210D4(v2,1)`, `network.c:44004`), то есть
	//   оператор перестаёт видеть собственного персонажа. Владелец потребовал видеть его
	//   полупрозрачным вместе с экипировкой и наложенными эффектами — это отступление от эталона,
	//   откат в поведение оригинала: `asda.Gm.HideTranslucent 0`.
	//   Соседей это не касается: их акторы гасятся целиком, как в оригинале.
	{
		UAsdaNetSubsystem* NetS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		const bool bSelf = NetS && NetS->IsSelfUserIdx(UserIdx);   // E2: та же строгая форма одной точкой
		AAsdaPlayerCharacter* SelfPc = bSelf ? Cast<AAsdaPlayerCharacter>(A) : nullptr;
		if (SelfPc)
		{
			SelfPc->SetGmHideTranslucent(bHidden);
			// Ключ выключен ⇒ ведём себя как оригинал: актор гасится целиком.
			const bool bFullHide = bHidden && !SelfPc->IsGmHideTranslucencyActive();
			A->SetActorHiddenInGame(bFullHide);
			AsdaSetAttachedFxHidden(A, bFullHide);
		}
		else
		{
			// ★ 24.10.2026: вторая причина скрытия соседа — невидимость навыка (α 0, UAsdaStealthSubsystem). Снятие GM-скрытия
			//   не проявляет того, кого прячет невидимость (и наоборот — подсистема смотрит метку GM).
			const AAsdaPlayerCharacter* PeerPc = Cast<AAsdaPlayerCharacter>(A);
			const bool bFinal = bHidden || (PeerPc && !PeerPc->IsStealthRendered());
			A->SetActorHiddenInGame(bFinal);
			AsdaSetAttachedFxHidden(A, bFinal);
		}
		// ★★★ [ПОРТ] ХРАНИТЕЛЬ ДУШИ — САМОСТОЯТЕЛЬНЫЙ АКТОР-КОМПАНЬОН, к персонажу не приаттачен,
		//   поэтому под предыдущие две строки он не попадает. Прятать его ОБЯЗАТЕЛЬНО и для себя,
		//   и для соседей: летающий над пустым местом хранитель выдаёт скрытого оператора вернее,
		//   чем сам персонаж. Прозрачным его НЕ делаем — он не «тело, экипировка и наложенные
		//   эффекты», а отдельная сущность (тот же охват, что у режима «душа»: эталон её не красит).
		//   Показывается обратно тем же вызовом с `bHidden == false`.
		if (AAsdaSoulGuardianActor* G = FindSoulGuardianByOwner(A))
		{
			G->SetActorHiddenInGame(bHidden);
			AsdaSetAttachedFxHidden(G, bHidden);
		}
	}

	// Эталон при скрытии снимает выбор, если скрылась НАША ТЕКУЩАЯ ЦЕЛЬ: панель цели иначе
	//   осталась бы висеть с HP несуществующего на экране персонажа.
	if (bHidden)
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GI->GetFirstLocalPlayerController(GI->GetWorld())))
			{
				if (PC->TargetActor.Get() == A) { PC->ClearTarget(); }
			}
		}
	}

	UE_LOG(LogAsdaWorld, Log, TEXT("[GM-скрытие] userIdx=%d -> %s"),
		UserIdx, bHidden ? TEXT("СКРЫТ") : TEXT("ВИДЕН"));
}

// ★★★ Своё состояние GM-невидимости: приходит ТОЛЬКО ответом 6006 (рассылка 4025 отправителя
//   пропускает — `SendPacket_ClippingSectorInOtherUser`, закон self-equip-not-broadcast).
//   `bAccepted == false` — сервер отказал; ничего не трогаем, состояние на сервере не изменилось.
void UAsdaWorldSubsystem::OnGmHideResult(bool bAccepted, bool bHidden)
{
	if (!bAccepted) { return; }
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || !Net->IsSelfUserNumKnown()) { return; }
	// ⚠ В ОРИГИНАЛЕ свой павн пропадает и с собственного экрана (бит поднимается на объекте
	//   локального игрока тем же `sub_9210D4`, что и у соседей). В порту по требованию владельца
	//   свой персонаж вместо этого становится полупрозрачным — развилка внутри `SetCharHiddenByGm`,
	//   откат `asda.Gm.HideTranslucent 0`.
	SetCharHiddenByGm((int32)Net->SelfUserNum, bHidden);
}

// ═══════════════ ★ СМЕРТЬ ИГРОКА: ПОЛНАЯ ПРЕЗЕНТАЦИЯ (4053) ═══════════════════════════════════
//   ЭТАЛОН. Recv_CharDeath_Apply@0xF42E50 (network.c:42579) на СВОЙ userIdx переводит персонажа в
//   состояние 6 (CharFx_AutoSabzil_State → CChar_SetState@0xA13D50 case 6, entity_char.c:16863):
//     • animSlotIdx = 7 и CObject_SetField13(pModelObj + 7*64, 0) — клип моушен-слота 7 («death01»)
//       с нулевой позиции, ОДИН РАЗ (луп не взводится: animPlayFlag не ставится);
//     • звук 3D CSoundMgr_PlaySound3D id 63 (gender==1, муж) / 64 (жен) в позиции персонажа;
//     • targetRenderId = -1 (цель снимается), bActionBusy = 0 (замах обрывается), percent64 = 100;
//     • charResetMountAndRebuildModel — ссаживание с транспорта; charClearAllSkillEffects — бафф-эффекты;
//     • закрытие торговых/сервисных окон; HUD-вкладки гасятся (uiPanel_DestroyChildControls).
//   Труп ИГРОКА, в отличие от моба, НЕ фейдится и не удаляется — тело лежит в позе последнего кадра
//   («уже мёртв»), пока не пройдёт воскрешение. Ввод режется эталонным isMovementActionState@0x95DBE0.
//   ⚠ ОБЕСЦВЕЧИВАНИЕ сцены (SetDeathDesaturation) — по ТЗ юзера; в декомпиле механизм не найден.
void UAsdaWorldSubsystem::OnPlayerCharDeath(int32 UserIdx, int32 KillerIdx)
{
	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	if (!Net) { return; }
	const bool bSelf = AsdaSelf::IsSelfUserIdx(Net, UserIdx);   // E2
	ASDA_CTRACE_IF(AsdaCtWorld::Users(UserIdx, KillerIdx), EAsdaCT::Death, TEXT("СМЕРТЬ персонажа uidx=%d%s (убийца uidx=%d)%s"), UserIdx, bSelf ? TEXT(" (Я)") : TEXT(" (сосед)"), KillerIdx,
		bSelf ? TEXT(" — свой замах прерывается, цель снимается") : TEXT(" — его серия, очередь и записи ленты снимаются"));
#if ASDA_COMBAT_TRACE
	if (!bSelf)
	{
		const TArray<int32>* CtBuf = DmgBuffers.Find(UserIdx);   // очередь соседа до сброса (журнал)
		ASDA_CTRACE_UID(UserIdx, EAsdaCT::Peer, TEXT("сосед uidx=%d умер: очередь ударов %s сбрасывается%s"), UserIdx, CtBuf ? *AsdaCtWorld::Queue(*CtBuf) : TEXT("[]"),
			(CVarAsdaObserverSync.GetValueOnGameThread() != 0) ? TEXT("") : TEXT(" (ключ ObserverSync 0 — НЕ сбрасывается)"));
	}
#endif
	// ★ Ревизия 17.10 (правка боя), п.6: своя смерть прерывает замах — окно «наш замах в полёте» сокращается до попадания,
	//   уже назначенного ответом сервера, или закрывается (придержанные HP/смерть цели применяются сразу).
	if (bSelf) { CutOurImpactWindow(TEXT("своя смерть")); }
	// ★ 17.10.2026 (Ф1, ревизия Ф0 п.1): смерть СОСЕДА — конец его серии (покой сбрасывает очередь и признак серии) и прерывание
	//   его записей ленты попаданий (назначенные ответом попадания доживают).
	else if (CVarAsdaObserverSync.GetValueOnGameThread() != 0)
	{
		PeerSeriesFlags.Remove(UserIdx); PeerSeriesTargetRid.Remove(UserIdx); ++PeerSeriesGen.FindOrAdd(UserIdx);
		if (TArray<int32>* Buf = DmgBuffers.Find(UserIdx)) { Buf->Reset(); }
		CutImpactEntries(UserIdx, TEXT("смерть атакующего"));
	}
	// ★ 17.10.2026 (Ф4) [ОРИГИНАЛ]: смерть персонажа (4053) — его PvP-серия кончается (покой сбрасывает очередь), и серии всех, кто
	//   бил его, — тоже (`CCharacter_SetActionState` case 0xA: у атакующих умершего состояние 5 → 0, подтип 20).
	ResetPvpSeries(UserIdx, TEXT("4053: смерть атакующего"));
	{
		TArray<int32> Hitters;   // атакующие, чья цель — умерший
		for (const TPair<int32, FAsdaPvpSeries>& E : PvpSeries) { if (E.Value.TargetUserIdx == UserIdx) { Hitters.Add(E.Key); } }
		for (const int32 H : Hitters)
		{
			if (AAsdaPlayerCharacter* HC = Cast<AAsdaPlayerCharacter>(ActorByUserIdx(H)); HC && !Net->IsSelfUserIdx(H)) { HC->StopAttackAnim(); }
			ResetPvpSeries(H, TEXT("4053: цель умерла"));
		}
	}

	// ★ «Вошли уже мёртвыми» / рассинхрон по 6001: клип падения НЕ проигрываем и звук гибели НЕ играем —
	//   гибель случилась в прошлой сессии. Ставим сразу позу трупа (последний кадр того же клипа).
	const bool bNoClip = bSelf && Net->bSelfDeathNoClip;

	// Свой павн берём НАПРЯМУЮ (LocalPlayerActor / павн контроллера) — реестр чужих здесь не при чём.
	AActor* A = bSelf ? LocalPlayerActor.Get() : ActorByUserIdx(UserIdx);
	AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(A);
	if (!Ch && bSelf)
	{
		if (APlayerController* PC0 = GetGameInstance()->GetFirstLocalPlayerController())
		{
			Ch = Cast<AAsdaPlayerCharacter>(PC0->GetPawn());   // страховка: павн контроллера
		}
	}
	if (!Ch)
	{
		// Павна ещё нет (например, 6001 прилетел до спавна) — состояние всё равно взведено в NetSubsystem,
		//   поза применится при спавне: TrySpawnLocalPlayer читает bSelfSpawnDead, а на рассинхрон
		//   повторный 6001 придёт снова. Меню смерти при этом уже открывается (оно павна не требует).
		UE_LOG(LogAsdaWorld, Warning, TEXT("[смерть] userIdx=%d: актора нет в мире — поза будет применена при спавне"), UserIdx);
		return;
	}

	// ★★★ ПОКАЗ НАВЫКА СНИМАЕТСЯ СМЕРТЬЮ — у СВОЕГО и у СОСЕДА одинаково. Эталон: `CChar_SetState`
	//   case 6 (entity_char.c:16863) зовёт `charClearAllSkillEffects`, а `recvOtherCharSkillUse@0xF4D7D0`
	//   при состоянии кастера 6 обнуляет слот показа (`field_4 = -1`, network.c:44615) — обе ветки БЕЗ
	//   гейта «локальный игрок». Стало значимо с §29: теперь показ идёт и у соседа, и без этой строки
	//   его эффекты каста висели бы над трупом, а припаркованные записи урона ждали бы кадра удара,
	//   которого уже не будет.
	if (UAsdaSkillFxSubsystem* Fx = GetGameInstance()->GetSubsystem<UAsdaSkillFxSubsystem>())
	{
		Fx->StopSkillVisual(Ch);
	}

	// Состояние 6: клип гибели (моушен-слот 7 = ассет 18 «death01») один раз, замирание последним кадром.
	Ch->StopAttackAnim();
	Ch->StopMove();
	if (bNoClip) { Ch->EnterDeadPoseInstant(); }
	else         { Ch->SetAnimState(EAsdaPlayerState::Dead); }
	UE_LOG(LogAsdaWorld, Warning, TEXT("[смерть] поза применена: актор '%s', состояние=%d, клип death01 (ассет 18), кадр=%s"),
		*Ch->GetName(), (int32)Ch->AnimState, bNoClip ? TEXT("последний (без падения)") : TEXT("с начала (падение)"));

	// Звук гибели: 63 муж / 64 жен, 3D в позиции тела (эталон CChar_SetState case 6).
	if (!bNoClip)
	{
		if (UAsdaSoundSubsystem* Snd = GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>())
		{
			Snd->PlaySound3DByIdAs(Ch->Sex == 2 ? 64 : 63, Ch->GetActorLocation(), EAsdaSoundClass::Voice, Ch);   // ★ 30.10: «голос»
		}
	}

	if (bSelf)
	{
		// Свой персонаж: снять цель, оборвать маршрут/автобой/подбор — эталон обнуляет targetRenderId
		//   и bActionBusy, а весь мировой ввод дальше глохнет по isMovementActionState.
		if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))
		{
			PC->CancelRoute();
			PC->ClearTarget();
			PC->bAutoAttack = false;
			PC->bCombatStarted = false;
			PC->ClearPendingLoot();
		}
		Ch->SetDeathDesaturation(true);
		UE_LOG(LogAsdaWorld, Warning,
			TEXT("[смерть] СВОЙ персонаж мёртв (убийца userIdx=%d): %s, сцена ч/б, ввод заблокирован"),
			KillerIdx, bNoClip ? TEXT("поза трупа БЕЗ клипа падения (вошли мёртвыми)") : TEXT("клип death01 → поза трупа"));
	}
	else
	{
		UE_LOG(LogAsdaWorld, Log, TEXT("[смерть] чужой игрок userIdx=%d убит (убийца %d): клип death01"), UserIdx, KillerIdx);
	}
}

// ★★★ ПЕРЕНОС ПЕРСОНАЖА ПО КОМАНДЕ СЕРВЕРА (4056 tile / 4055 map+tile).
//   Живой тест: после ОК в модалке персонаж вставал НА МЕСТЕ СМЕРТИ — координаты «города привязки»
//   приходили в пакете, но клиент их не применял. Теперь:
//     MapId < 0  → телепорт в пределах текущей карты (NetSnap: позиция + стоп движения);
//     MapId >= 0 → полноценная СМЕНА КАРТЫ через Load Manager (тот же путь, что обычный переход).
void UAsdaWorldSubsystem::OnSelfWarp(int32 MapId, int32 TileX, int32 TileZ)
{
	const FVector2D DstGame = AsdaCoord::TileCenterGame(TileX, TileZ);   // ★ Аудит №3 (B2)
	if (MapId >= 0)
	{
		UE_LOG(LogAsdaWorld, Warning, TEXT("[воскрешение] смена карты: mapId=%d тайл=(%d,%d) → загрузка уровня (отложенно)"),
			MapId, TileX, TileZ);
		// ★ Тот же отложенный паттерн: мы внутри разбора пакета — травел уничтожит мир и объекты,
		//   чьи методы ещё ниже по стеку. Запускаем следующим кадром через таймер 0.
		if (UWorld* W = GetGameInstance()->GetWorld())
		{
			FTimerHandle H;
			const int32 M = MapId;
			W->GetTimerManager().SetTimerForNextTick([this, M]()
			{
				if (UAsdaLoadManagerSubsystem* LM = GetGameInstance()->GetSubsystem<UAsdaLoadManagerSubsystem>())
				{
					LM->BeginMapLoad(M);
				}
			});
		}
		return;
	}
	AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(LocalPlayerActor);
	if (!Ch)
	{
		if (APlayerController* PC0 = GetGameInstance()->GetFirstLocalPlayerController())
		{
			Ch = Cast<AAsdaPlayerCharacter>(PC0->GetPawn());
		}
	}
	if (!Ch) { UE_LOG(LogAsdaWorld, Warning, TEXT("[воскрешение] телепорт: павна нет")); return; }
	const FVector2D Was = Ch->GetGamePosXZ();
	if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))
	{
		PC->CancelRoute();
	}
	// ★ 22.10.2026 (ревизия плавности, ВАЖНО-Б): перенос сервером 4056 (воскрешение, короткий телепорт) — ЧИСТЫЙ снап: без хвоста
	//   поправки (тело и камера не «проезжают» путь) и с мгновенным курсом.
	Ch->NetSnapHard(DstGame);
	UE_LOG(LogAsdaWorld, Warning, TEXT("[воскрешение] телепорт в пределах карты: (%.0f,%.0f) → тайл(%d,%d)=(%.0f,%.0f)"),
		Was.X, Was.Y, TileX, TileZ, DstGame.X, DstGame.Y);
	ASDA_CTRACE(EAsdaCT::State, TEXT("перенос сервером (воскрешение/телепорт 4056): тайл (%d,%d), маршрут отменён"), TileX, TileZ);
}

// ★ HP/MP self из пакета воскрешения/входа (4055/4056). Само значение УЖЕ легло в единый источник
//   истины (UAsdaNetSubsystem::SetVitalsHp в обработчике пакета) и оттуда пришло в плашку через
//   OnVitalsChanged — здесь только диагностика. Прямая запись в плашку убрана: она и была вторым
//   параллельным путём, из-за которого «плашка = 0, а урон идёт» (см. ApplyVitalsToNameplate).
void UAsdaWorldSubsystem::OnSelfVitals(int32 Hp, int32 Mp)
{
	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	const UAsdaNetSubsystem::FAsdaVitals* V = Net ? Net->GetVitals((int32)Net->SelfUserNum) : nullptr;
	const int32 Max = (V && V->HpMax > 0) ? V->HpMax : 0;
	UE_LOG(LogAsdaWorld, Warning, TEXT("[воскрешение] HP=%d MP=%d, максимум=%d (%.0f%%), источник=пакет входа/воскрешения"),
		Hp, Mp, Max, (Max > 0) ? (100.f * (float)Hp / (float)Max) : -1.f);
	ASDA_CTRACE(EAsdaCT::HpSelf, TEXT("своё HP=%d/%d MP=%d из пакета входа/воскрешения (4055/4056) — уже в едином источнике HP"), Hp, Max, Mp);
}

// ══════════════ ★★★ ВИТАЛЬНЫЕ ЗНАЧЕНИЯ → ПЛАШКА (единственный путь) ═══════════════════════════
// ЗАКОН [[self-nameplate-same-as-others]]: на своей плашке показываем ТО ЖЕ, что на чужих. Полоса
//   HP своей плашки видна всегда (просьба юзера), у чужих — только когда игрок выбран целью
//   (эталон: окно плашки цели). Значения ОБОИХ случаев берутся только отсюда.
void UAsdaWorldSubsystem::ApplyVitalsToNameplate(int32 UserIdx)
{
	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	if (!Net) { return; }
	const UAsdaNetSubsystem::FAsdaVitals* V = Net->GetVitals(UserIdx);
	if (!V || !V->HasHp()) { return; }
	AActor* A = ActorByUserIdx(UserIdx);
	if (!A) { return; }
	UAsdaNameplateComponent* NP = A->FindComponentByClass<UAsdaNameplateComponent>();
	if (!NP) { return; }
	// ★ Второе («восстановительное») здоровье — в подложку плашки (расширение сверх оригинала,
	//   просьба владельца 2026-08-24). У чужих оно не приходит и остаётся нулевым — подложки нет.
	const int32 Second = FMath::Max(0, V->HpSecond);
	if (NP->CurHp == V->HpCur && NP->MaxHp == V->HpMax && NP->HpSecond == Second) { return; }
	NP->SetHpSecond(Second);
	NP->SetHp(V->HpCur, V->HpMax, 0);
}

void UAsdaWorldSubsystem::OnVitalsChanged(int32 UserIdx, const UAsdaNetSubsystem::FAsdaVitals& V)
{
#if ASDA_COMBAT_TRACE
	// ★ Журнал боя: HP плашки ДО применения — для «было → стало» (свой — канал HUD, чужой — плашка над головой).
	{
		const AActor* CtA = ActorByUserIdx(UserIdx);
		const UAsdaNameplateComponent* CtNp = CtA ? CtA->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
		const int32 CtWas = CtNp ? CtNp->CurHp : -1;   // HP на плашке до применения (−1 — плашки нет)
		const UAsdaNetSubsystem* CtNet = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
		const bool bCtSelf = AsdaSelf::IsSelfUserIdx(CtNet, UserIdx);
		if (CtWas != V.HpCur)
		{
			ASDA_CTRACE_IF(bCtSelf || AsdaCombatTrace::IsUserRelevant(UserIdx), bCtSelf ? EAsdaCT::HpSelf : EAsdaCT::HpPlate, TEXT("%s uidx=%d: HP %d → %d / %d (%+d), MP %d/%d, канал «%s»%s"),
				bCtSelf ? TEXT("СВОЁ HP") : TEXT("HP игрока"), UserIdx, CtWas, V.HpCur, V.HpMax, (CtWas >= 0) ? V.HpCur - CtWas : 0, V.MpCur, V.MpMax,
				*V.LastHpSrc, bCtSelf ? TEXT(" → HUD MINISTATE + своя плашка") : TEXT(" → плашка"));
		}
		else
		{
			ASDA_CTRACE_V(bCtSelf ? EAsdaCT::HpSelf : EAsdaCT::HpPlate, TEXT("uidx=%d: витальные значения обновлены без смены HP (%d/%d, MP %d/%d, канал «%s»)"),
				UserIdx, V.HpCur, V.HpMax, V.MpCur, V.MpMax, *V.LastHpSrc);
		}
	}
#endif
	ApplyVitalsToNameplate(UserIdx);
	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	AActor* A = ActorByUserIdx(UserIdx);
	const UAsdaNameplateComponent* NP = A ? A->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
	UE_LOG(LogAsdaWorld, Log, TEXT("[HP] %s userIdx=%d: %d/%d (%.0f%%) канал=%s | плашка=%s | HUD=%s"),
		AsdaSelf::IsSelfUserIdx(Net, UserIdx) ? TEXT("свой") : TEXT("чужой"),
		UserIdx, V.HpCur, V.HpMax, (V.HpMax > 0) ? (100.f * (float)V.HpCur / (float)V.HpMax) : -1.f,
		*V.LastHpSrc,
		NP ? (NP->bShowHp ? *FString::Printf(TEXT("%d/%d видима"), NP->CurHp, NP->MaxHp)
		                  : *FString::Printf(TEXT("%d/%d скрыта"), NP->CurHp, NP->MaxHp))
		   : TEXT("актора нет"),
		AsdaSelf::IsSelfUserIdx(Net, UserIdx) ? TEXT("MINISTATE_WIN (свой канал: UAsdaMiniStateSubsystem::ApplyHp)") : TEXT("—"));
}

// ★ 6539 — витальные значения по charId (эталон CharManager_FindByCharId). Наш реестр чужих игроков
//   держит charId в FAsdaNetPlayerEntry, свой charId = Net->SelectedCharId.
void UAsdaWorldSubsystem::OnCharStateUpdate(int32 CharId, int32 MaxHp, int32 CurHp, int32 MaxMp, int32 CurMp)
{
	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	if (!Net || CharId == 0) { return; }
	int32 Uidx = -1;
	if (Net->SelectedCharId != 0 && CharId == Net->SelectedCharId) { Uidx = (int32)Net->SelfUserNum; }
	else
	{
		for (const TPair<int32, FAsdaNetPlayerEntry>& P : NetPlayers)
		{
			if (P.Value.CharId == CharId) { Uidx = P.Key; break; }
		}
	}
	if (Uidx < 0)
	{
		UE_LOG(LogAsdaWorld, Log, TEXT("[HP] 6539 charId=%d: игрока нет в реестре (вне AoI) — HP %d/%d не к кому применить"),
			CharId, CurHp, MaxHp);
		ASDA_CTRACE(EAsdaCT::HpTarget, TEXT("6539 charId=%d: HP %d/%d НЕ применён — игрока нет в реестре (вне видимости)"), CharId, CurHp, MaxHp);
		return;
	}
	ASDA_CTRACE(EAsdaCT::HpTarget, TEXT("6539 HP/MP цели-игрока charId=%d → uidx=%d: HP %d/%d MP %d/%d (в единый источник HP)"), CharId, Uidx, CurHp, MaxHp, CurMp, MaxMp);
	Net->SetVitalsHp(Uidx, CurHp, MaxHp, TEXT("6539 цель"), /*bServerSnapshot=*/true);
	Net->SetVitalsMp(Uidx, CurMp, MaxMp, TEXT("6539 цель"), /*bServerSnapshot=*/true);
}

// ★ Всплывающая цифра дельты HP/MP (эталон netHandleHpMpUpdate case 1/4/6: FloatingText_Spawn стиль
//   0x12 над сущностью) + бафф-FX по skillId. Стиль 0x12 — тот же атлас цифр, что у боевых чисел.
void UAsdaWorldSubsystem::OnVitalsFx(int32 UserIdx, int32 Delta, uint8 UpdateType, int32 SkillId)
{
	AActor* A = ActorByUserIdx(UserIdx);
	// ★ R5-fix8 (14.11.2026) [НАШЕ ЛУЧШЕ ОРИГИНАЛА — осознанное расхождение, слово владельца 14.11]: порт при дельте 0 цифру не
	//   рисует. У пакета здоровья 4058 оригинал рисует цифру и при дельте 0 — «0»
	//   (`netHandleHpMpUpdate@0xF43D00`: `sprintf("%d", дельта)` без проверки, типы 1/4/6; подтверждено дизассемблером образа —
	//   между разбором типа и вызовом `FloatingText_Spawn@0x9A8CF0` сравнений дельты нет). Признак «пакет 4058 — ноль рисовать»
	//   ставит приём (`UAsdaNetSubsystem::bVitalsFxZeroDigit`) только при значении 1 ключа `asda.Vitals.HpZeroDigit` (умолчание 0).
	const UAsdaNetSubsystem* NetZ = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!A || (Delta == 0 && !(NetZ && NetZ->bVitalsFxZeroDigit))) { return; }
	++DebugVitalsDigits;
	if (UAsdaFloatingTextSubsystem* FT = GetGameInstance()->GetSubsystem<UAsdaFloatingTextSubsystem>())
	{
		FT->SpawnOverActor(0x12, FString::FromInt(Delta), A);
	}
	UE_LOG(LogAsdaWorld, Log, TEXT("[HP] цифра дельты %+d над userIdx=%d (тип %u, skillId=%d, стиль 0x12)"),
		Delta, UserIdx, UpdateType, SkillId);
	ASDA_CTRACE_UID(UserIdx, EAsdaCT::DmgText, TEXT("цифра дельты HP/MP «%+d» стиль 0x12 над игроком uidx=%d (тип обновления %d, навык %d)"), Delta, UserIdx, (int32)UpdateType, SkillId);
}

// 4055/4056 подтверждение воскрешения: цвет вернуть, поза покоя, управление разблокировать.
//   (Сам «столб света» + fade-in делает OnMapEntryFx по 4056 — его не трогаем.)
void UAsdaWorldSubsystem::OnPlayerCharRevived()
{
	AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(LocalPlayerActor);
	if (!Ch) { return; }
	Ch->SetDeathDesaturation(false);
	if (Ch->AnimState == EAsdaPlayerState::Dead) { Ch->SetAnimState(EAsdaPlayerState::Idle); }
	UE_LOG(LogAsdaWorld, Log, TEXT("[смерть] воскрешение: цвет возвращён, поза покоя, управление разблокировано"));
	ASDA_CTRACE(EAsdaCT::State, TEXT("ВОСКРЕШЕНИЕ своего персонажа подтверждено: цвет возвращён, поза покоя, ввод разблокирован"));
}

// Отказ атаки (коды эталона: 2=в движении, 4=скилл-лок, 7=оружие/патроны, 8=бафф-слот, 9=клетка карты).
//   ★ WARMESSAGE: эталон показывает строку на КАЖДЫЙ отказ (только self — гейт
//   charPtr->effectSlots[112].field_40). Карты «код→loc» у 4027 и 4029 разные:
//   4027 Recv_CombatAnimState (network.c:40918-41043, ВСЕ красные −65536):
//     2→2320, 3→2204, 5→1074, 6→1082, 7→2036, 8→2038, 9→1300, 11→112117, 12→112118, 20→1032;
//   4029 Recv_CastState (network.c:42079-42281): 1→1024 (золотой), 3→1095 (золотой),
//     4→2036, 5→1074, 6→1082, 7→2036 (красные). Все — SOLO (без дубля в сисчат), TTL 10 000 мс.
void UAsdaWorldSubsystem::OnCombatRefused(int32 CasterUserIdx, uint8 Code, uint16 SrcOpcode)
{
	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	ASDA_CTRACE_IF((Net && Net->IsSelfUserIdx(CasterUserIdx)) || AsdaCtWorld::Ours(Net ? Net->CombatRefusedRid : -1, CasterUserIdx), (Net && Net->IsSelfUserIdx(CasterUserIdx)) ? EAsdaCT::Refuse : EAsdaCT::Peer,
		TEXT("ОТКАЗ боя %d код %d атакующему uidx=%d%s (цель пакета rid=%d, HP в пакете %d, очередь %s)"),
		(int32)SrcOpcode, (int32)Code, CasterUserIdx, (Net && Net->IsSelfUserIdx(CasterUserIdx)) ? TEXT(" (я)") : TEXT(" (сосед)"),
		Net ? Net->CombatRefusedRid : -1, Net ? Net->CombatRefusedTargetHp : -1, Net ? *AsdaCtWorld::Queue(Net->CombatRefusedQueue) : TEXT("[]"));
	// ★★★ 27.09.2026, Р3 (бит 2 «запрос хода в пути»): эталон снимает его и в отказах боя — 4027 коды 6/7/8/9/20
	//   (`Recv_CombatAnimState`, 0xF3F517…0xF3F7B1), 4029 коды 4/6/7 (`Recv_CastState`, 0xF41F77…0xF4211A): внутри
	//   ветки «атакующий найден» (`CharManager_FindByUserIdx`), БЕЗ проверки «это я» — стоит рядом с нотисом, но
	//   вне его гейта `field_40`. Подход к цели шёл пакетом 4006, и отказ боя освобождает ввод, не дожидаясь 4007.
	//   (Те же снятия у 4202 коды 6..9 и 4204 коды 4/6/7 — у порта этих приёмников нет, реестр отложенного.)
	if (Net)
	{
		const bool bClears = (SrcOpcode == 4027 && (Code == 6 || Code == 7 || Code == 8 || Code == 9 || Code == 20))
			|| (SrcOpcode == 4029 && (Code == 4 || Code == 6 || Code == 7));
		const bool bFound = Net->IsSelfUserIdx(CasterUserIdx) || NetPlayers.Contains(CasterUserIdx);
		if (bClears && bFound) { Net->ClearMoveRequestInFlight(TEXT("отказ боя 4027/4029")); }
		// ★ 18.10.2026 (ревизия Ф4, ВАЖНО 1): отказ боя с мобом — атакующий шёл к мобу, его PvP-серия снимается.
		if (SrcOpcode == 4027 || SrcOpcode == 4029) { ResetPvpSeriesOnMobFight(CasterUserIdx, TEXT("отказ боя с мобом 4027/4029")); }
	}
	// ★ 17.10.2026 (синхронизация боя, Ф0) [ОРИГИНАЛ]: отказ 4029 СОСЕДА (приходит в сектор) — конец его серии, как эталон
	//   (`StopPeerAttackSeries`). Нотисы — только свои (гейт `field_40`), поэтому ниже для соседа по-прежнему выход.
	if (Net && !Net->IsSelfUserIdx(CasterUserIdx) && SrcOpcode == 4029 && CVarAsdaObserverSync.GetValueOnGameThread() != 0)
	{
		StopPeerAttackSeries(CasterUserIdx, Code);
	}
	if (!Net || !Net->IsSelfUserIdx(CasterUserIdx)) { return; }
	// ★ Ревизия 25.10 (фантомный урон, М-1): отказ сервера — ответ на свою заявку удара по мобу; удар не принят, очередь не
	//   расходовалась (отказ 4028 самим клиентом — не ответ сервера, заявки не было).
	if (SrcOpcode == 4027 || SrcOpcode == 4029)
	{
		if (UAsdaAttackRhythmSubsystem* RhR = GetGameInstance()->GetSubsystem<UAsdaAttackRhythmSubsystem>())
		{
			RhR->ResolveMobReply(/*bConsumed=*/false, TEXT("отказ сервера"), Net->CombatRefusedRid);   // ★ R5-fix8: цель отказа
		}
	}
	UE_LOG(LogAsdaWorld, Warning, TEXT("[net] атака отклонена сервером (%u код %d) — выходим из боя"), SrcOpcode, Code);
	// ★ Ревизия 17.10, п.6: удар не принят — нашего попадания по нему не будет; окно «замах в полёте» сокращается до уже
	//   назначенного прежним ответом попадания или закрывается (придержанные HP/смерть применяются сразу).
	CutOurImpactWindow(TEXT("отказ сервера"));
	// ★ РЕШЕНИЕ ЮЗЕРА: категория CombatRefused → СИСТЕМНЫЙ ЧАТ (в 221 остаются только военные).
	//   Цвет эталона сохранён построчно (золотой у 4029 sub 1/3, красный у остальных) через перекрытие.
	if (UAsdaMessageSubsystem* Msg = GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>())
	{
		int32 Loc = 0;
		bool bGold = false;
		if (SrcOpcode == 4027)
		{
			switch (Code)
			{
			case 2:  Loc = 2320;   break;   // перегруз ≥90 % — атака невозможна
			case 3:  Loc = 2204;   break;   // «Необходимо зарядить баллисту»
			case 5:  Loc = 1074;   break;   // «Этот объект не может быть целью»
			case 6:  Loc = 1082;   break;   // «…не можешь нанести вред монстру»
			case 7:  Loc = 2036;   break;   // «Нет стрел»
			case 8:  Loc = 2038;   break;   // «Во время езды невозможно атаковать»
			case 9:  Loc = 1300;   break;   // «Атака отсюда невозможна»
			case 11: Loc = 112117; break;   // оружие подтип 10 вариант 1 — удочка (GameServer.c:78403); строка добавлена 29.10.2026
			case 12: Loc = 112118; break;   // оружие подтип 10 вариант 2 — бур (GameServer.c:78409)
			case 20: Loc = 1032;   break;   // разница уровней >12
			default: break;                 // остальные коды эталон молча сбрасывает в idle
			}
		}
		else // 4029
		{
			switch (Code)
			{
			case 1:  Loc = 1024; bGold = true; break;   // «Цель вне досягаемости»
			case 3:  Loc = 1095; bGold = true; break;   // «Атака прервана из-за сбоя»
			case 4:  Loc = 2036; break;
			case 5:  Loc = 1074; break;
			case 6:  Loc = 1082; break;
			case 7:  Loc = 2036; break;
			default: break;                             // case 2 у эталона без сообщения
			}
		}
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("свой отказ %d код %d: нотис %d%s"), (int32)SrcOpcode, (int32)Code, Loc,
			(Loc > 0) ? (bGold ? TEXT(" (золотой) в системный чат") : TEXT(" (красный) в системный чат")) : TEXT(" — строки у эталона нет"));
		if (Loc > 0)
		{
			FAsdaMsgParams P;
			P.Category = EAsdaMsgCategory::CombatRefused;
			P.LocId = Loc;
			P.bUseColor = true;
			P.Color = bGold ? UAsdaChatSubsystem::ColGold() : UAsdaChatSubsystem::ColRed();
			P.SrcOpcode = SrcOpcode;
			Msg->Post(P);
		}
	}
	if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))
	{
		AsdaLeaveAttackAction(PC, TEXT("отказ сервера боя"));   // ★ 01.11.2026: 5 → 0 — 4024/400 до снятия флагов (эталон `CCharacter_SetAction` case 0)
		if (AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(LocalPlayerActor)) { Ch->StopAttackAnim(); }
		PC->bAutoAttack = false;
		PC->bCombatStarted = false;
		ASDA_CTRACE(EAsdaCT::Exit, TEXT("отказ сервера %d код %d — свой замах остановлен, автоатака и бой сняты (цель остаётся выбранной)"), (int32)SrcOpcode, (int32)Code);
	}
}
