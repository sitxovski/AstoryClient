// Copyright (c) 2026 @sitxovski (Discord)
// AsdaNetSubsystem_RecvCombatState.cpp — часть `AsdaNetSubsystem.cpp`: приёмник пакетов: ответ на замах 4029/4027, транспорт соседа, облик-алиас, звания/статы; экипировка соседей, HP/MP, возрождение 4055/4056, уровень/опыт.
//   ★ R2 рефакторинга (10.11.2026): определения перенесены из `AsdaNetSubsystem.cpp` ДОСЛОВНО (сверка `Tools/asda_split_check.py`);
//   общие помощники и объявления — `AsdaNetSubsystemRecvInternal.h`. Шапка системы и ссылки на эталон — в `AsdaNetSubsystem.cpp` и заголовке класса.

#include "AsdaNetSubsystem.h"
#include "UI/AsdaCommunityClanSubsystem.h"   // ★ 01.11.2026 (clan-window): приём клановых пакетов панели «Предводитель»
#include "AsdaWeightWire.h"   // ★ 29.10.2026 (вес ×2): AsdaWeightFromWire — вес по проводу u16 (1017, 5003, 4999, 6125, …)
#include "World/AsdaAttackRhythmSubsystem.h"   // ★ 15.10.2026: серверные часы обычной атаки (4026/4028/5256)
#include "AsdaTimedSend.h"                     // ★ 15.10.2026: поток точной отправки (стендовая постановка 4028)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "HAL/IConsoleManager.h"
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "World/AsdaWorldSubsystem.h"
#include "UI/AsdaSoulStoneSubsystem.h"   // ★ 06.11.2026: 5420, 5421, 5424, 5425
#include "UI/AsdaSoulmateSkillSubsystem.h"
#include "UI/AsdaEffectsSubsystem.h"   // ★ гейт продолжения боя: обездвиживающие состояния
#include "AsdaConnection.h"
#include "Net/NetOpcodes.h"
#include "Misc/SecureHash.h"
#include "Login/AsdaLoginData.h"
#include "AsdaBytes.h"   // D4b: единые читалки little-endian вместо лямбд/открытых выражений в каждом хендлере
#include "AsdaPacketWriter.h"   // ★ волна 3 п.10: единый сборщик тел исходящих пакетов (вместо uint8 P[N] + WrI16)

// ★★★ R5-fix8 (14.11.2026) [НАШЕ ЛУЧШЕ ОРИГИНАЛА — ОСОЗНАННОЕ РАСХОЖДЕНИЕ, слово владельца 14.11: «если наша реализация или
//   решение по функциональности лучше — оставлять наше»]: ЦИФРЫ ДЕЛЬТЫ HP/MP. Поведение порта НЕ меняется; оригинал — под ключами.
//   Сверено двумя методами: декомпил и дизассемблер образа из дампа (`Reports/refactor_2026-11/r5/fix/a/scan_vitals_digit.py`,
//   вывод `st8/scan_vitals_digit.txt`).
//   • Мана, пакет 4059 (`pkt_OnMpSkillFxUpdate@0xF440E0`, entity_char.c:53081): вызовов `FloatingText_Spawn@0x9A8CF0` в функции
//     НЕТ ни одного — оригинал цифру над головой не рисует вовсе. Порт рисовал цифру дельты при типах 4 и 6.
//   • Здоровье, пакет 4058 (`netHandleHpMpUpdate@0xF43D00`, network.c:42760-42766): три вызова `FloatingText_Spawn` (типы 1, 4, 6),
//     перед каждым `sprintf("%d", дельта)` без сравнения дельты с нулём — при дельте 0 рисуется «0». Порт ноль не показывал.
//   Порт оставлен как был: цифра маны игроку полезна (видно, сколько маны восстановлено/снято), а «0» над головой при
//   нулевой дельте — шум. Значения ключей «как оригинал» (MpDigit 0, HpZeroDigit 1) оставлены для сверки и самотеста.
static ASDA_DEV_CVAR(int32, CVarAsdaVitalsMpDigit,
	TEXT("asda.Vitals.MpDigit"), 1,
	TEXT("[НАШЕ ЛУЧШЕ ОРИГИНАЛА, слово владельца 14.11] 1 (по умолчанию) = порт: цифра дельты маны над головой при типах обновления ")
	TEXT("4 и 6 пакета 4059; 0 = как оригинал: у пакета маны цифра не рисуется."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarAsdaVitalsHpZeroDigit,
	TEXT("asda.Vitals.HpZeroDigit"), 0,
	TEXT("[НАШЕ ЛУЧШЕ ОРИГИНАЛА, слово владельца 14.11] 0 (по умолчанию) = порт: при дельте 0 пакета здоровья 4058 цифры нет; ")
	TEXT("1 = как оригинал: при типах 1/4/6 рисуется и «0»."),
	ECVF_Default);
// Проверка раскладки тела перед отправкой (хвост волны 3 п.10); определение и разбор — у `IsSendLinkUp`.
static bool AsdaPacketLayoutBroken(const FAsdaPacketWriter& W, uint16 Opcode, const TCHAR* Sender);
#include "Login/AsdaText.h"                 // имена класса/созвездия из loc-таблиц
#include "Items/AsdaItemTableSubsystem.h"   // раскладка 0x3EF по itemType
#include "World/AsdaLoadManagerSubsystem.h"  // 6004 успех → BeginMapLoad (Load Manager)
#include "Text/AsdaTextSubsystem.h"          // loc-строки форматов чата (20030/1187/1109/8200/…)
#include "UI/AsdaChatWindowSubsystem.h"      // RankColor / SlotColor — таблицы каналов и рангов
#include "UI/AsdaInventorySubsystem.h"        // Дд6: FAsdaItemInstance::kRecordSize (57 б) — длина записи предмета
#include "UI/AsdaMessageSubsystem.h"    // 5055 отказ портала: нотисы 1096/1097 в системный чат
#include "UI/AsdaNotice.h"              // ★ 27.09: отказы 5055/6201 — плашка 221 и пара «плашка + журнал» (общие точки)
#include "UI/AsdaChatSubsystem.h"       // ★ 27.09: ColGold() = эталонный −11514 отказов перехода
#include "UI/AsdaHotbarSubsystem.h"     // сохранение раскладки панели перед переходом (hotbar_SendAllLayouts)
#include "UI/AsdaMinimapSubsystem.h"
#include "World/AsdaPossessGate.h"      // ★ гейт действий при одержимости (SoulState==2), см. шапку заголовка
#include "AsdaOutgoingGate.h"               // ★ единый шлюз исходящих пакетов (E1, 14.09.2026)
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (итог и `quit` диагностики asda.Gm.TestEntryHide)
#include "UI/AsdaEntryWindows.h"               // ★ 22.10.2026: окна при загрузке карты — список эталона
#include "UI/AsdaCampWarSubsystem.h"           // ★ 30.10.2026: 5055 код 1 → «Битва армий» (49530, сброс при уходе с карты кампвара)
#include "UI/AsdaCampWarBattleSubsystem.h"     // ★ 30.10.2026: боевая фаза — 4055/6013 при включённом HUD боя
#include "AsdaSelf.h"                       // ★ «это я?» — единая точка своего номера/уровня (E2, 14.09.2026)
#include "World/AsdaPlayerController.h"
#include "World/AsdaCoord.h"                 // ★ единый перевод UE↔игра (Аудит №3 B1) — для SendStop4008AtActor
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"                 // ★ С-Р1 ред. 1: GEngine->AddOnScreenDebugMessage — отказ второго барьера на экране
#include "Net/ChannelCipher.h"                 // ★ XyT: живой ключ ПРИЁМА (профиль + подстановка)
#include "HAL/PlatformFileManager.h"           // ★ XyT: метка времени файла ключа
#include "Misc/FileHelper.h"                   // ★ XyT: чтение файла ключа
#include "Misc/Parse.h"                        // ★ XyT: FParse::HexDigit
#include "Misc/ConfigCacheIni.h"               // ★ GM: список операторских префиксов имени из DefaultGame.ini
#include "World/AsdaKingCrownSubsystem.h"      // ★ 27.09.2026: приём 6126 «корона владельца добычи рейд-босса»
#include "AsdaLogSeal.h"                     // ★ 19.10.2026: логин учётки → .alog.meta журнала сессии (SetSessionAccount)
#include "AsdaAntiCheatSubsystem.h"           // ★ EAC Э1 (16.10.2026): клиентский Easy Anti-Cheat (модуль AsdaAntiCheat)
#include "UI/AsdaDisconnectPopupSubsystem.h"  // ★ EAC ред. 3: попап 8000 с текстом EAC (991xxx), когда строка утверждена
#include "World/AsdaCombatTrace.h"            // ★ 18.10.2026: журнал боя — этапы ОТПР/ПРИЁМ/ОТКАЗ боевых пакетов
#include "World/AsdaMobSkillSubsystem.h"      // ★ 30.10.2026: навыки мобов 8011/8012/8013
#include "UI/AsdaArrowAmmoSubsystem.h"        // ★ 31.10.2026: 5030 — расход стрел слота 10
#include "AsdaNetSubsystemInternal.h"   // ★ 22.10.2026 разнос: общее для частей AsdaNetSubsystem*.cpp (LogAsdaNet, чистые хелперы)
#include "UI/AsdaTutorialEvents.h"   // ★ 10.11.2026 обучение: флаги шагов гида 1950 (TUTORIAL.md §2.3)
#include "AsdaNetSubsystemRecvInternal.h"   // ★ R2: общие помощники и объявления частей AsdaNetSubsystem*.cpp (после включений пролога)

// ★ 18.10.2026 (жалоба «после 5-го уровня квесты не появляются, после перезапуска — появляются»): блок персонажа
//   тела 5042 (профессия/класс/подкласс/очки, body[10..14]) применяет СЕТЬ до рассылки `OnQuestTurnInResult` и шлёт
//   `OnSelfJobChanged`. 1 — так (квестовые метки и диалог NPC сразу видят новый класс); 0 — прежний путь: поля пишет
//   баннер из своего обработчика 5042 (порядок относительно диалога не определён), события нет.
// ★★★ 05.11.2026 [ОРИГИНАЛ] ВОСКРЕШЕНИЕ СОСЕДА ПО 4056. Эталон `GameNet_OnLocalPlayerMapEntry@0xF43390`, ветка чужого
//   (game_main.c:13059-13085): HP (dword @6) и MP (i16 @10) соседа, перенос на тайл (@2/@4, `player_setPositionAndTarget`),
//   состояние: @20 == 200 (прибыл мёртвым) → 29, иначе 0 и у игрока (`categoryFlag == 32`) `CChar_SetState(…, 0)` — встаёт из
//   трупа; затем столб `cmchar01` (если не GM-скрыт). Порт делал только столб: сосед после воскрешения лежал трупом там, где
//   умер, до следующего появления в секторе. 1 — как эталон; 0 — как было.
static ASDA_DEV_CVAR(int32, CVarAsdaPeerRevive4056,
	TEXT("asda.Peer.Revive4056"), 1,
	TEXT("[ОРИГИНАЛ] 1 — 4056 о соседе ставит его HP/MP, переносит на тайл воскрешения и поднимает из позы трупа (при «прибыл "
	     "мёртвым» — кладёт трупом); 0 — как было (только столб света, сосед остаётся лежать)."),
	ECVF_Default);
// Аксессор ключа для других .cpp (имя ключа вне журналов не светим).
bool AsdaPeerRevive4056On() { return CVarAsdaPeerRevive4056.GetValueOnGameThread() != 0; }

// ★ Воспроизводить ли эталонный дефект наполнения очереди уронов (см. разбор у места чтения 4029).
//   0 = берём непустой префикс (наше исправление), 1 = эталон: при нуле очередь пуста и серия рвётся.
static ASDA_DEV_CVAR(int32, CVarLegacyQueueAllOrNothing,
	TEXT("asda.Combat.LegacyQueueAllOrNothing"), 0,
	TEXT("1 = эталонное «всё или ничего» в очереди уронов 4029 (с дефектом); 0 = непустой префикс."),
	ECVF_Default);

// ★★★ 22.10.2026 (баг игроков «Team HUD occasionally disappears»): 4056 о СОСЕДЕ — не «я в мире».
//   Эталон `GameNet_OnLocalPlayerMapEntry@0xF43390` (game_main.c:12871) делит 4056 на две ветки по `effectSlots[112].field_40`
//   (свой персонаж): своя — опыт (`UI_UpdateExpBar`), HP/MP, позиция, закрытие окон торговли/склада/лавки; чужая (game_main.c:13036)
//   — ТОЛЬКО звук 2D, HP/MP, позиция и столб света: ни опыта, ни окон. `netHandler_EnterWorld@0xF43050` (network.c:42639) 4055
//   не своего персонажа вовсе снимает (`CharManager_RemoveNode`, return 0). Порт же на ЛЮБОЙ 4055/4056 писал `SelfExp` опытом
//   из пакета и рассылал `OnWorldReady` → `UAsdaWorldSubsystem::RevealWorld` → `UAsdaWindowSubsystem::CloseAll`: воскрешение или
//   прибытие любого игрока в секторе закрывало все окна игрока (панель отряда 2200 после этого не возвращалась — сервер состав
//   5107 не переприсылает), сбивало полосу опыта и слало лишний 11004 (элитный ящик). Живые журналы: Player17 19.10 18:41:08
//   (4056 uidx=3 при своём 1 → RevealWorld, «сосед прибыл/воскрес»), sitxovski 20.10 00:23:51, Player51 19.10 20:51:52 и 22:11:57.
//   [ИСПРАВЛЕНИЕ ПОРТА] опыт и «я в мире» — только для своего userIdx (пока свой не известен по 4003 — как прежде, это страховка
//   первого входа). Откат: `asda.Net.WorldReadySelfOnly 0`.
static ASDA_DEV_CVAR(int32, CVarAsdaNetWorldReadySelfOnly,
	TEXT("asda.Net.WorldReadySelfOnly"), 1,
	TEXT("1 = 4055/4056 о другом игроке не пишут свой опыт и не рассылают «я в мире» (RevealWorld/закрытие окон) — как эталон; ")
	TEXT("0 = как прежде: любой 4055/4056 = «я в мире» и опыт из пакета."),
	ECVF_Default);

// ★ R2 (10.11.2026): приёмник области «CombatState» — ответ на замах 4029/4027, транспорт соседа, облик-алиас, звания/статы; экипировка соседей, HP/MP, возрождение 4055/4056, уровень/опыт.
//   Группы `case` перенесены из `UAsdaNetSubsystem::HandlePacket` ДОСЛОВНО и в исходном порядке; `bHandled` = false только
//   в служебной ветке `default:` в конце — опкод не из этой области, диспетчер идёт дальше.
void UAsdaNetSubsystem::HandlePacket_CombatState(const Asda::ParsedPacket& Pkt, bool& bHandled)
{
	bHandled = true;
	switch ((NetOpcode)Pkt.Opcode)
	{
	case OP_CAST_STATE: // ★ 4029 0xFBD — Recv_CastState@0xF41620: ОТВЕТ НА ЗАМАХ 4028 (живой сервер шлёт
	{                   //   именно его, а не 4027). @0 i16 caster, @2 i16 targetRid, @8 i32[4] очередь
	                    //   уронов (эталон addEntriesBounded4(caster, a2+2)), @24 u8 castSubType (0=обычная).
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 25, (int32)Pkt.Opcode))
		{
			auto I32 = [&B](int32 O) { return AsdaBytes::RdI32(&B[O]); };
			// ★ Аудит №3 (A7, находка 2): эталон `Recv_CastState@0xF41620` читает ОБА поля ЗНАКОВЫМИ —
			//   `CharManager_FindByUserIdx(…, *(__int16 *)a2)` и `Entity_FindByRenderId(…, *((__int16 *)a2 + 1))`,
			//   ровно как соседний 4027 (`Recv_CombatAnimState@0xF3EEB0`, `*(__int16 *)(pkt+1)` / `(pkt+3)`).
			//   Порт читал их без знака — расхождение с эталоном на значениях ≥ 0x8000.
			const int32 Caster = AsdaBytes::RdI16(&B[0]);
			const int32 Rid    = AsdaBytes::RdI16(&B[2]);
			const int32 TgtHp  = I32(4);   // ★ ОСТАТОЧНЫЙ HP ЦЕЛИ (эталон: a2[1]<=0 → пометка трупа)
			const uint8 Sub    = B[24];
			// ★★★ ИСПРАВЛЕНИЕ ТЯЖЁЛОГО ДЕФЕКТА ОРИГИНАЛА — «ВСЁ ИЛИ НИЧЕГО» В НАПОЛНЕНИИ ОЧЕРЕДИ.
			//   ⚠ Прежний комментарий здесь ВРАЛ: «ноль обрывает очередь (эталон addEntriesBounded4)».
			//   Эталон делает НЕ ЭТО. `addEntriesBounded4@0xA24180` (network.c:8317) дословно:
			//       for (i = 0; i < 4; ++i) {
			//           if ( !array[i] ) return 0;              // ← первый ноль: ВЫХОД, вернув 0
			//           list_push_back(hidden, array[i]); }     // значения уже в скрытом списке
			//       for (j = 0; j < 4; ++j) {                   // ← ЭТОТ цикл и наполняет видимую
			//           if ( count >= 4 || !array[j] ) return 0;//   очередь, но до него не дошли
			//           queue[count++] = list_front(hidden); list_pop_front(hidden); }
			//       return 1;
			//   Последствий два, и оба тяжёлые:
			//   1) ПЕРМАНЕНТНЫЙ СДВИГ ОЧЕРЕДИ. Значения из первого цикла остаются в скрытом списке
			//      навсегда (никто его не чистит на этом пути). Следующий полный пакет дотолкает
			//      свои четыре, а второй цикл снимет с ГОЛОВЫ — то есть чужие, застрявшие. Дальше
			//      цифры урона едут относительно ударов до конца сессии.
			//   2) ОБРЫВ АВТОАТАКИ. Возврат 0 гасит весь блок вызывающего — `Recv_CastState` case 0,
			//      network.c:41190-41199: под `if (v11)` лежит и `Net_SendActionPacket` (заявка на
			//      следующий замах), и сброс боевого состояния. Пакет с нулём = серия встала.
			//      Это сильный кандидат на «то атакует, то не атакует» В САМОМ ОРИГИНАЛЕ.
			//   Берём непустой ПРЕФИКС: он и есть то, что сервер реально прислал (сервер расходует
			//   один элемент за замах и добивает хвост нулями). Скрытого списка у нас нет вовсе,
			//   так что и сдвигаться нечему.
			//   Ключ отката к эталонному «всё или ничего»: `asda.Combat.LegacyQueueAllOrNothing 1`.
			TArray<int32> Dmg;
			{
				const bool bLegacy = CVarLegacyQueueAllOrNothing.GetValueOnGameThread() != 0;
				bool bHole = false;
				for (int32 k = 0; k < 4; ++k)
				{
					const int32 V = I32(8 + 4 * k);
					if (V == 0) { bHole = true; break; }
					Dmg.Add(V);
				}
				if (bLegacy && bHole)
				{
					// Эталон 1:1: очередь не наполняется вовсе, и серия обрывается.
					UE_LOG(LogAsdaNet, Log,
						TEXT("4029: очередь отброшена целиком (эталонное «всё или ничего», ")
						TEXT("отброшено %d знач.) — asda.Combat.LegacyQueueAllOrNothing"), Dmg.Num());
					Dmg.Reset();
				}
			}
			FString Q; for (int32 V : Dmg) { Q += FString::Printf(TEXT("%d "), V); }
			UE_LOG(LogAsdaNet, Log, TEXT("4029: caster=%d tgt=%d hp=%d sub=%d удары=%d [%s]"), Caster, Rid, TgtHp, Sub, Dmg.Num(), *Q);
#if ASDA_COMBAT_TRACE
			// Участник нашего боя (до строки): сосед бьёт моба нашего боя → его события — на уровне 1.
			if (!IsSelfUserIdx(Caster) && Caster >= 0 && AsdaCombatTrace::IsEngaged(Rid)) { AsdaCombatTrace::NoteEngagedUser(Caster); }
			// Канал общий (все соседи сектора): свои — всегда уровень 1; соседи — если моб наш или сосед — участник.
			const bool bOursCt29 = IsSelfUserIdx(Caster) || AsdaCombatTrace::IsEngaged(Rid) || AsdaCombatTrace::IsUserRelevant(Caster);
			if (AsdaCombatTrace::IsOn(bOursCt29 ? 1 : 2))
			{
				// Журнал боя: все 4 сырых значения очереди @8 (с нулями), принятый непустой префикс и итог — одной строкой.
				TArray<int32> RawQ; for (int32 k = 0; k < 4; ++k) { RawQ.Add(I32(8 + 4 * k)); }
				const bool bSelfCaster = IsSelfUserIdx(Caster);
				ASDA_CTRACE_IF(bOursCt29, Sub != 0 ? EAsdaCT::Refuse : (bSelfCaster ? EAsdaCT::Recv : EAsdaCT::Peer),
					TEXT("ПРИЁМ 4029 атакующий=uidx %d%s цель rid=%d код=%d%s hp=%d очередь=%s принято %d → %s"),
					Caster, bSelfCaster ? TEXT(" (Я)") : TEXT(" (сосед)"), Rid, (int32)Sub, Sub == 0 ? TEXT("(обычный)") : TEXT("(ОТКАЗ)"),
					TgtHp, *AsdaCtQueue(RawQ), Dmg.Num(),
					Sub == 0 ? TEXT("OnCombatAnim(1)") : TEXT("OnCombatRefused"));
			}
#endif
			if (Sub == 0)
			{
				CombatAnimSrcOpcode = 4029;   // ★ 17.10.2026 (Ф0): подписчик различает 4029 и 4027 (очередь соседа, см. объявление)
				OnCombatAnim.Broadcast(/*Case=*/1, Caster, Rid, Dmg, TgtHp);
				CombatAnimSrcOpcode = 0;
				// ★★★ 2026-09-02: ВТОРОЙ канал цифр урона — 4 × i16 с @+25 и 4 × i32 с @+33.
				//   Эталон наполняет обе очереди (`sub_F40670@0xF40670`, зовётся через thunk
				//   0x93A291) ровно там, где очередь уронов ПРИНЯТА — `network.c:41583`, `:41592`,
				//   `:41608`, `:41628`, `:41754`, `:41760`, все под `if (addEntriesBounded4(...))`.
				//   Поэтому и мы читаем канал только при непустой очереди: при пустой эталон
				//   `sub_F40670` не звал бы вовсе. Раскладка и доказательства обеими сторонами —
				//   в объявлении `FAsdaCombatSideDamage` (.h).
				if (Dmg.Num() > 0) { ParseCombatSideDamage(B, /*OffI16=*/25, /*OffI32=*/33, 4029, Caster, Rid); }
			}
			else
			{
				// case 1..7 = отказы/нотисы (эталон: сброс атакующего в idle). Гасим свой боевой цикл.
				// ★ 17.10.2026 (Ф1, ревизия Ф0 п.2/п.3): цель, её HP и очередь пакета — подписчику (код 2 у соседа, см. объявление).
				CombatRefusedRid = Rid; CombatRefusedTargetHp = TgtHp; CombatRefusedQueue = Dmg;
				OnCombatRefused.Broadcast(Caster, Sub, 4029);
				CombatRefusedRid = -1; CombatRefusedTargetHp = -1; CombatRefusedQueue.Reset();
			}
		}
		break;
	}
	case OP_COMBAT_ANIM_STATE: // 4027 0xFBB — Recv_CombatAnimState@0xF3EEB0: взвод атаки игрока (ответ на 4026)
	{
		// @0 u8 case | @1 i16 attackerUserIdx | @3 i16 targetRid | @5/@7 i16 живая клетка цели (x, z) | @9 i32 HP |
		//   @13 i32[4] очередь ударов (0=конец).
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 5, (int32)Pkt.Opcode))
		{
			const uint8 Case  = B[0];
			const int32 AtkU  = AsdaBytes::RdI16(&B[1]);
			const int32 Rid   = AsdaBytes::RdI16(&B[3]);
			TArray<int32> Dmg;
			for (int32 k = 0; k < 4 && (size_t)(13 + 4 * k + 4) <= B.size(); ++k)
			{
				const int32 O = 13 + 4 * k;
				const int32 V = AsdaBytes::RdI32(&B[O]);
				// ⚠ Тот же дефект наполнения, что и у 4029: эталон при нуле НЕ берёт ничего и
				//   вдобавок рвёт серию (разбор — у чтения 4029 ниже). Берём непустой префикс.
				if (V == 0) { break; }
				Dmg.Add(V);
			}
			// HP цели @+9 (эталон: для уже мёртвой цели пишет fadeStartTime; для живой — остаточный HP).
			int32 TgtHp = -1;
			if (NetNeedLen(B, 13, (int32)Pkt.Opcode)) { TgtHp = AsdaBytes::RdI32(&B[9]); }
			FString Q; for (int32 V : Dmg) { Q += FString::Printf(TEXT("%d "), V); }
			UE_LOG(LogAsdaNet, Log, TEXT("4027: case=%d atk=%d tgt=%d hp=%d удары=%d [%s]"), Case, AtkU, Rid, TgtHp, Dmg.Num(), *Q);
#if ASDA_COMBAT_TRACE
			// Участник нашего боя (до строки): сосед взводит атаку по мобу нашего боя → его события — на уровне 1.
			if (!IsSelfUserIdx(AtkU) && AtkU >= 0 && AsdaCombatTrace::IsEngaged(Rid)) { AsdaCombatTrace::NoteEngagedUser(AtkU); }
			// Канал общий (все соседи сектора): свои — всегда уровень 1; соседи — если моб наш или сосед — участник.
			const bool bOursCt27 = IsSelfUserIdx(AtkU) || AsdaCombatTrace::IsEngaged(Rid) || AsdaCombatTrace::IsUserRelevant(AtkU);
			if (AsdaCombatTrace::IsOn(bOursCt27 ? 1 : 2))
			{
				// Журнал боя: сырые 4 значения очереди @13 (сколько есть в теле), клетка цели @5/@7, итог.
				TArray<int32> RawQ;
				for (int32 k = 0; k < 4 && (size_t)(13 + 4 * k + 4) <= B.size(); ++k) { RawQ.Add(AsdaBytes::RdI32(&B[13 + 4 * k])); }
				const bool bSelfAtk = IsSelfUserIdx(AtkU);
				ASDA_CTRACE_IF(bOursCt27, Case != 1 ? EAsdaCT::Refuse : (bSelfAtk ? EAsdaCT::Recv : EAsdaCT::Peer),
					TEXT("ПРИЁМ 4027 атакующий=uidx %d%s цель rid=%d код=%d%s клетка цели=(%d,%d) hp=%d очередь=%s принято %d → %s"),
					AtkU, bSelfAtk ? TEXT(" (Я)") : TEXT(" (сосед)"), Rid, (int32)Case, Case == 1 ? TEXT("(взвод атаки)") : TEXT("(ОТКАЗ)"),
					B.size() >= 9 ? AsdaBytes::RdI16(&B[5]) : -1, B.size() >= 9 ? AsdaBytes::RdI16(&B[7]) : -1,
					TgtHp, *AsdaCtQueue(RawQ), Dmg.Num(), Case == 1 ? TEXT("OnCombatAnim(1)") : TEXT("OnCombatRefused"));
			}
#endif
			if (Case == 1)
			{
				// ★ 15.10.2026 (волна mobpos_swing): живая клетка цели @5/@7 — до разбора боя (см. `OnCombatAnimTargetCell`).
				if (NetNeedLen(B, 9, (int32)Pkt.Opcode))
				{
					OnCombatAnimTargetCell.Broadcast(AtkU, Rid, FIntPoint(AsdaBytes::RdI16(&B[5]), AsdaBytes::RdI16(&B[7])));
				}
				CombatAnimSrcOpcode = 4027;   // ★ 17.10.2026 (Ф0): 4027 case 1 — сброс очереди соседа по эталону (см. объявление)
				OnCombatAnim.Broadcast(Case, AtkU, Rid, Dmg, TgtHp);
				CombatAnimSrcOpcode = 0;
				// ★★★ 2026-09-02: ТОТ ЖЕ второй канал цифр урона, что и у 4029, но со сдвигом 4 —
				//   4 × i16 с @+29 и 4 × i32 с @+37. Сдвиг оттого, что у 4027 перед полями стоит
				//   лишний байт `case` и i16-пара. Эталон зовёт тот же `sub_F40670` тем же thunk'ом:
				//   `network.c:41193`, `:41233`, `:41262` — `sub_93A291(…, pPacket + 29, pPacket + 37)`,
				//   все три под `if (addEntriesBounded4(...))`. Разбор один на оба опкода.
				if (Dmg.Num() > 0) { ParseCombatSideDamage(B, /*OffI16=*/29, /*OffI32=*/37, 4027, AtkU, Rid); }
			}
			else { OnCombatRefused.Broadcast(AtkU, Case, 4027); }   // коды 0/2/4/7/8/9/11/12 → сброс в idle
		}
		break;
	}
	case OP_SPAWN_VISIBLE_PLAYER_FROM_PKT: // 6155 0x180B — СОСЕД СЕЛ НА ТРАНСПОРТ (имя константы — миснейм)
	{
		// ★★★ 16.09.2026, НОВЫЙ ОБРАБОТЧИК. До этого 6155 был мёртвой константой: в порте не было ни
		//   одного вхождения кроме объявления. Это дыра того же класса, что чинил 5418, только в
		//   реальном времени: 5418 говорит «сосед УЖЕ на транспорте, когда мы вошли в его зону»,
		//   а 6155 — «сосед садится на транспорт прямо сейчас, у нас на глазах».
		//
		//   ЭТАЛОН `spawnVisiblePlayerFromPkt@0xF3E550` (имя — МИСНЕЙМ, никакого спавна там нет):
		//   создаёт ПУСТОЙ узел 0x250 и заполняет ровно четыре поля —
		//     node+0 = i16 тело+0 | node+442 = i32 тело+2 | node+446 = i32 тело+6 | node+450 = u16 тело+10
		//   после чего кладёт узел в ту же очередь, что и 5418. Тело — 12 байт.
		//   442 = 292 + 10*15 ⇒ это РОВНО слот 15 блока функциональных предметов, то есть ТРАНСПОРТ.
		//
		//   ★ ОТЛИЧИЕ ОТ 5418: пакет ЧАСТИЧНЫЙ (один слот), а не снапшот. Поэтому при пустом коде мы
		//   НЕ шлём «снять транспорт»: в 5418 это законно, потому что там авторитетный полный снимок,
		//   здесь — нет. Снятие приходит своим пакетом 5454.
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 12, (int32)Pkt.Opcode))
		{
			const int32 Uidx = (int32)AsdaBytes::RdI16(&B[0]);   // ЗНАКОВО: ключ тот же, что у 4007/4062/5418
			const int32 Code = AsdaBytes::RdI32(&B[2]);
			const int32 Dur  = AsdaBytes::RdI32(&B[6]);
			const int32 Val  = (int32)AsdaBytes::RdU16(&B[10]);  // процент скорости; саму скорость несёт 6071
			if (Uidx >= 0 && Code >= 0)
			{
				FAsdaEffectMsg M; M.UserIdx = Uidx; M.Opcode = (int32)Pkt.Opcode;
				M.Kind        = EAsdaEffectMsgKind::ItemApply;
				M.ItemCode    = Code;
				M.DurationSec = Dur;
				M.Byte9       = 99;
				M.Id          = 15;   // ★ слот транспорта известен точно: пакет заполняет ровно его (node+442/446/450)
				OnEffectMsg.Broadcast(M);
				UE_LOG(LogAsdaNet, Log, TEXT("←6155 сосед сел на транспорт: userIdx=%d itemCode=%d dur=%d процент=%d"),
					Uidx, Code, Dur, Val);
				OnMountState.Broadcast(Uidx, Code, true);
			}
		}
		break;
	}
	case OP_RECV_PACKET_VISIBLE_PLAYER_LIST: // 5418 0x152A — современный алиас 4065 (тот же хендлер в эталоне)
	case OP_RECV_VISIBLE_PLAYER_LIST:        // 4065 0xFE1 — СНАПШОТ СОСТОЯНИЙ соседа (батч, 592 б/запись)
	{
		// ★★★ 16.09.2026, ПЕРЕПИСАНО ЦЕЛИКОМ (КРИТ). Прежняя версия читала запись как «wire-копию
		//   снапшота 0x303» — тайлы, charId, имя, пол, созвездие, причёску, экипировку и вес — и пушила
		//   результат в `PendingRemotePlayers`. НИ ОДНОГО из этих полей в теле 5418/4065 НЕТ: там лежат
		//   ТРИ БЛОКА СОСТОЯНИЙ, и всё, кроме `userIdx@0`, было мусором. Из мусорных байтов ещё и
		//   выставлялся максимальный вес своего персонажа.
		//   Источник ошибки — `remote_player_display_re.md` §2: там склеены ДВЕ РАЗНЫЕ очереди клиента —
		//   staging-запись 0x303 (771 б, CharManager+36, туда идёт 5417/4005) и узел 0x250 (592 б,
		//   CharManager+60, туда идёт ЭТОТ пакет). Эталон `Recv_VisiblePlayerList@0xF3DE50` не разбирает
		//   запись вовсе: `new(0x250)` → `qmemcpy(node, body, 0x250)` → в очередь; разбирает её потом
		//   потребитель `cChar_restoreSkillEffectsFromRecord@0x9CE420` (game.c:51984-52029).
		//
		//   РАСКЛАДКА (проверена построчно по потребителю):
		//     +0          i16 userNum (пусто = -1)
		//     +2  + 5*i   28 состояний ОТ НАВЫКОВ: u8 стаки | i16 длительность, сек (-1 = вечно) | i16 StateID
		//                 терминатор — StateID == -1 (эталон делает break)
		//     +142 + 5*j  30 СОУЛ-МЕЙТ-состояний, индекс j = НОМЕР СЛОТА (не уплотняется):
		//                 i16 длительность@+1 | i16 SoulMateSkillID@+3; применяется именно j, id — гейт
		//     +292 + 10*k 30 ФУНКЦИОНАЛЬНЫХ ПРЕДМЕТОВ, индекс k = тип-слот, ★ k == 15 = ТРАНСПОРТ:
		//                 i32 itemCode@+0 (пусто = -1) | i32 длительность@+4 | u16 величина@+8
		//                 (у маунта — процент скорости; эталон её в ЭТОЙ ветке НЕ читает: скорость
		//                 приходит отдельным пакетом 6071)
		//
		//   ★ ОБЩИЙ ЛИМИТ 28 (дефект оригинала, воспроизводим 1:1): во втором цикле эталон проверяет
		//   `i < 28` и делает `++i`, где `i` — счётчик ПЕРВОГО цикла. Если состояния навыков заняли
		//   лимит, соул-мейт-состояния не применятся вовсе.
		//
		//   Спавн соседей этот пакет НЕ триггерит — им занимается 5417/4005 (см. выше): там и
		//   `PendingRemotePlayers.Push`, и снятие запроса 5416.
		if (!NetNeedLen(Pkt.Body, 592, (int32)Pkt.Opcode)) { break; }   // ★ размер — ДО взятия data(); N8 (В2-A, 11.10.2026): + Warning-строка
		const uint8* Buf = Pkt.Body.data();
		int32 Records = 0;
		for (int32 Off = 0; Off + 592 <= (int32)Pkt.Body.size(); Off += 592)
		{
			const uint8* B = Buf + Off;
			const int32 Uidx = (int32)AsdaBytes::RdI16(&B[0]);   // ЗНАКОВО: ключ тот же, что у 4007/4062
			if (Uidx < 0) { continue; }                          // пустая запись
			++Records;
			int32 Applied = 0;   // ★ ОБЩИЙ счётчик обоих блоков состояний — как `i` у эталона

			// ── Блок 1: состояния от навыков (28 × 5 б, уплотнён, терминатор StateID == -1) ──────────
			int32 T0 = 0;
			for (; Applied < 28; ++Applied)
			{
				const int32 P = 2 + 5 * Applied;
				const int32 Id = AsdaBytes::RdI16(&B[P + 3]);
				if (Id == -1) { break; }                         // эталонный терминатор
				// Гейт эталона живёт в `cChar_restartSkillEffectByType@0xA2AD60`: id ∈ [0, 350) и id != 177.
				if (Id < 0 || Id >= 0x15E || Id == 177) { continue; }
				FAsdaEffectMsg M; M.UserIdx = Uidx; M.Opcode = (int32)Pkt.Opcode;
				M.Kind        = EAsdaEffectMsgKind::SkillApply;
				M.KindByte    = (int32)B[P];                     // стаки баффа
				M.DurationSec = AsdaBytes::RdI16(&B[P + 1]);
				M.Id          = Id;
				OnEffectMsg.Broadcast(M);
				++T0;
			}

			// ── Блок 2: соул-мейт-состояния (30 × 5 б, индекс = слот, НЕ уплотняется) ────────────────
			int32 T1 = 0;
			for (int32 j = 0; j < 30; ++j)
			{
				const int32 P   = 142 + 5 * j;
				const int32 Dur = AsdaBytes::RdI16(&B[P + 1]);
				const int32 Sid = AsdaBytes::RdI16(&B[P + 3]);
				// Эталон: `dur >= 0 && id >= 0 && i < 28` — и `++i` по ОБЩЕМУ счётчику.
				if (Dur < 0 || Sid < 0 || Applied >= 28) { continue; }
				FAsdaEffectMsg M; M.UserIdx = Uidx; M.Opcode = (int32)Pkt.Opcode;
				M.Kind        = EAsdaEffectMsgKind::SoulApply;
				M.Id          = j;                               // ★ применяется НОМЕР СЛОТА, не SoulMateSkillID
				M.DurationSec = Dur;
				M.Byte9       = 99;                              // как в 6154: уровень не передаётся
				OnEffectMsg.Broadcast(M);
				++Applied;
				++T1;
			}

			// ── Блок 3: функциональные предметы (30 × 10 б, индекс = тип-слот, k == 15 — транспорт) ──
			int32 T2 = 0;
			bool bMountSlotFilled = false;
			for (int32 K = 0; K < 30; ++K)
			{
				const int32 P    = 292 + 10 * K;
				const int32 Code = AsdaBytes::RdI32(&B[P]);
				if (Code < 0) { continue; }                      // эталонный гейт: пусто = -1
				const int32 Dur  = AsdaBytes::RdI32(&B[P + 4]);
				FAsdaEffectMsg M; M.UserIdx = Uidx; M.Opcode = (int32)Pkt.Opcode;
				M.Kind        = EAsdaEffectMsgKind::ItemApply;
				M.ItemCode    = Code;
				M.DurationSec = Dur;
				M.Byte9       = 99;
				// ★ НОМЕР СЛОТА едет в сообщении: у эталона ключ записи — именно k
				//   (`cCharStatusAddTypeMount(char, k, …)` → `charStatusAddType2(char+638, k, …)`),
				//   и таблица `special_itemlist` в этой ветке не участвует вовсе. Без этого потребитель
				//   выводил слот заново по itemCode и при промахе таблицы МОЛЧА терял состояние.
				M.Id          = K;
				OnEffectMsg.Broadcast(M);
				++T2;
				// ★ ТРАНСПОРТ — строго слот 15: у эталона `cCharStatusAddTypeMount` цепляет меш
				//   транспорта именно при `k == 15`. Здесь индекс слота известен точно, поэтому
				//   отсев по SubType (как в 6154) не нужен.
				if (K == 15)
				{
					bMountSlotFilled = true;
					UE_LOG(LogAsdaNet, Log, TEXT("[транспорт] из снапшота %u: userIdx=%d itemCode=%d dur=%d процент=%u"),
						Pkt.Opcode, Uidx, Code, Dur, (uint32)AsdaBytes::RdU16(&B[P + 8]));
					OnMountState.Broadcast(Uidx, Code, true);
				}
			}
			// Снапшот авторитетен: пустой слот 15 означает «точно НЕ на транспорте» — снимаем стейл-кэш
			//   (высадка вне нашего AoI не приносит 5454). Так же поступает обработчик 6154.
			//   ★ ЭТО НАШЕ РАСШИРЕНИЕ: эталон в этой ветке только ДОБАВЛЯЕТ и никогда не снимает.
			//   Поэтому — только для СОСЕДЕЙ: ответ сервера на наш же запрос 5416 содержит запись про
			//   нас, и она сняла бы наш собственный транспорт.
			if (!bMountSlotFilled && !AsdaSelf::IsSelfUserIdx(this, Uidx)) { OnMountState.Broadcast(Uidx, 0, false); }

			UE_LOG(LogAsdaNet, Log, TEXT("←%u снапшот состояний: userIdx=%d навыки=%d соулмейт=%d предметы=%d"),
				Pkt.Opcode, Uidx, T0, T1, T2);
		}
		// ★ `OnEnvBatch` здесь НЕ шлём: этот обработчик очередей спавна больше не наполняет
		//   (раньше слал, пока ошибочно пушил мусор в PendingRemotePlayers). Спавн — дело 4005/5417.
		(void)Records;
		break;
	}

	case OP_CHAR_TITLE_RECV_TITLE_SLOT_UPDATE:   // 6068 0x17B4 — титул/ранг игрока (14б)
	{
		const uint8* B = Pkt.Body.data();
		if (!NetNeedLen(Pkt.Body, 14, (int32)Pkt.Opcode)) { break; }   // N8 (дедуп №2, В2-A, 11.10.2026): была молчаливая проверка — теперь с Warning-строкой `NetNeedLen`
		const int32 Uidx  = (int32)AsdaBytes::RdU16(&B[0]);
		const int32 Rank  = AsdaBytes::RdI32(&B[6]);
		const int32 Tid0  = AsdaBytes::RdI16(&B[10]);
		const int32 Tid1  = AsdaBytes::RdI16(&B[12]);
		UE_LOG(LogAsdaNet, Log, TEXT("Титул 6068: userIdx=%d title0=%d title1=%d ранг=%d"), Uidx, Tid0, Tid1, Rank);
		OnCharTitle.Broadcast(Uidx, Tid0, Tid1, Rank);
		break;
	}
	// ═══ ★★★ СЕМЬЯ «ЗВАНИЯ / СЛАВА» (Fame). Разбор тел — у объявлений делегатов в .h и в
	//     `Reports/title_system.md`. Здесь ТОЛЬКО копия тела потребителю: закон
	//     `no-reentrant-net-callbacks` (окно званий кладёт её в очередь и разбирает в Tick).
	case OP_UI_TITLE_ON_JOB_CHANGE_RESULT:        // 6065 0x17B1 — звание получено/потеряно
	{
		const uint8* B = Pkt.Body.data();
		if (!NetNeedLen(Pkt.Body, 49, (int32)Pkt.Opcode)) { break; }   // N8 (дедуп №2, В2-A, 11.10.2026): была молчаливая проверка — теперь с Warning-строкой `NetNeedLen`
		UE_LOG(LogAsdaNet, Log, TEXT("Звание 6065: userIdx=%d charId=%d titleId=%d снято=%d очки=%d"),
			(int32)AsdaBytes::RdI16(&B[0]), AsdaBytes::RdI32(&B[2]),
			(int32)AsdaBytes::RdI16(&B[6]), (int32)B[8], AsdaBytes::RdI32(&B[45]));
		OnTitleGranted.Broadcast(TArray<uint8>(B, (int32)Pkt.Body.size()));
		break;
	}
	// 6066 0x17B2 — маска ПОЛУЧЕННЫХ званий. 42 б у необновлённого сервера, 74 б у обновлённого
	// (P21: биты 256…511 дописаны в хвост). Длина сверяется «не меньше» — тело уходит целиком.
	case OP_NET_ON_CHAR_EQUIP_UPDATE:
	{
		const uint8* B = Pkt.Body.data();
		if (!NetNeedLen(Pkt.Body, 42, (int32)Pkt.Opcode)) { break; }   // N8 (дедуп №2, В2-A, 11.10.2026): была молчаливая проверка — теперь с Warning-строкой `NetNeedLen`
		UE_LOG(LogAsdaNet, Log, TEXT("Звания 6066: маска «получено» userIdx=%d charId=%d очки=%d тело=%d б"),
			(int32)AsdaBytes::RdI16(&B[0]), AsdaBytes::RdI32(&B[2]), AsdaBytes::RdI32(&B[6]),
			(int32)Pkt.Body.size());
		OnTitleMaskOwned.Broadcast(TArray<uint8>(B, (int32)Pkt.Body.size()));
		break;
	}
	case OP_QUEST_TRACK_BIT_TOGGLE_BY_USER_IDX:   // 6070 0x17B6 — звание удалено (13 б)
	{
		const uint8* B = Pkt.Body.data();
		if (!NetNeedLen(Pkt.Body, 13, (int32)Pkt.Opcode)) { break; }   // N8 (дедуп №2, В2-A, 11.10.2026): была молчаливая проверка — теперь с Warning-строкой `NetNeedLen`
		UE_LOG(LogAsdaNet, Log, TEXT("Звание 6070: userIdx=%d код=%d titleId=%d очки=%d"),
			(int32)AsdaBytes::RdI16(&B[0]), (int32)B[6],
			(int32)AsdaBytes::RdI16(&B[7]), AsdaBytes::RdI32(&B[9]));
		OnTitleDeleted.Broadcast(TArray<uint8>(B, (int32)Pkt.Body.size()));
		break;
	}
	case OP_NET_HANDLER_ON_GUILD_TITLE_RESULT:    // 6085 0x17C5 — СЕКРЕТ звания открыт (8 б)
	{
		const uint8* B = Pkt.Body.data();
		if (!NetNeedLen(Pkt.Body, 8, (int32)Pkt.Opcode)) { break; }   // N8 (дедуп №2, В2-A, 11.10.2026): была молчаливая проверка — теперь с Warning-строкой `NetNeedLen`
		UE_LOG(LogAsdaNet, Log, TEXT("Звание 6085: «найдено» charId=%d userIdx=%d titleId=%d"),
			AsdaBytes::RdI32(&B[0]), (int32)AsdaBytes::RdI16(&B[4]), (int32)AsdaBytes::RdI16(&B[6]));
		OnTitleFound.Broadcast(TArray<uint8>(B, (int32)Pkt.Body.size()));
		break;
	}
	// 6086 0x17C6 — маска «НАЙДЕНО». 38 б у необновлённого сервера, 70 б у обновлённого (P21).
	case OP_NET_HANDLER_ON_CHAR_DATA32:
	{
		const uint8* B = Pkt.Body.data();
		if (!NetNeedLen(Pkt.Body, 38, (int32)Pkt.Opcode)) { break; }   // N8 (дедуп №2, В2-A, 11.10.2026): была молчаливая проверка — теперь с Warning-строкой `NetNeedLen`
		UE_LOG(LogAsdaNet, Log, TEXT("Звания 6086: маска «найдено» charId=%d userIdx=%d тело=%d б"),
			AsdaBytes::RdI32(&B[0]), (int32)AsdaBytes::RdI16(&B[4]), (int32)Pkt.Body.size());
		OnTitleMaskFound.Broadcast(TArray<uint8>(B, (int32)Pkt.Body.size()));
		break;
	}
	case OP_TITLE_STAT_BATCH_2255:                // 2255 0x8CF — блоки статов полученных званий
	{
		const uint8* B = Pkt.Body.data();
		if (!NetNeedLen(Pkt.Body, 6, (int32)Pkt.Opcode)) { break; }   // N8 (дедуп №2, В2-A, 11.10.2026): была молчаливая проверка — теперь с Warning-строкой `NetNeedLen`
		UE_LOG(LogAsdaNet, Log, TEXT("Звания 2255: блоки статов userIdx=%d charId=%d записей=%d"),
			(int32)AsdaBytes::RdI16(&B[0]), AsdaBytes::RdI32(&B[2]),
			((int32)Pkt.Body.size() - 6) / 46);
		OnTitleStatBatch.Broadcast(TArray<uint8>(B, (int32)Pkt.Body.size()));
		break;
	}
	case OP_CHAR_NET_UPDATE_FIELD_REFRESH_PLATE:   // 6078 0x17BE — только РАНГ (бейдж 1668), без титулов
	{
		// Эталон charNet_UpdateFieldRefreshPlate@0xE9C0F0: [0..1] userIdx, [2..5] charId, [6..9] rankValue.
		const uint8* B = Pkt.Body.data();
		if (!NetNeedLen(Pkt.Body, 10, (int32)Pkt.Opcode)) { break; }   // N8 (дедуп №2, В2-A, 11.10.2026): была молчаливая проверка — теперь с Warning-строкой `NetNeedLen`
		// ★ Аудит №3 (знаковость): эталон `charNet_UpdateFieldRefreshPlate@0xE9C0F0` — `__int16 *a2`,
		//   `CharManager_FindByUserIdx(mgr, *a2)`; ранг рядом = `*(_DWORD *)(a2 + 3)`.
		const int32 Uidx = AsdaBytes::RdI16(&B[0]);
		const int32 Rank = AsdaBytes::RdI32(&B[6]);
		UE_LOG(LogAsdaNet, Log, TEXT("Ранг 6078: userIdx=%d rank=%d"), Uidx, Rank);
		OnCharRank.Broadcast(Uidx, Rank);
		break;
	}
	// ══════ ★ ОТВЕТЫ КОНТЕКСТНОГО МЕНЮ ПО ИГРОКУ (окно TRANTOOL_WIN 2000) ══════════════════════
	//   Разбор каждого тела — `Public/UI/AsdaPlayerMenuSubsystem.h` §3 и отчёт
	//   `Reports/player_context_menu.md`. Здесь только приём и передача потребителю — поля разбирают
	//   сами потребители (обмен `UAsdaExchangeP2PSubsystem`, окно 4200 `UAsdaOtherCharaSubsystem`,
	//   ответ Дружбы 5201 — `UAsdaSoulmateSubsystem::OnPlayerMenuResult`; ★ поправка волны 1-А, 06.10.2026).
	case OP_CHAR_FULL_SPAWN:   // 4021 0xFB5 — НАБОР части экипа чара (Recv_CharFullSpawn@0xF3E970)
	case OP_CHAR_SPAWN_SHORT:  // 4023 0xFB7 — СБРОС части (Recv_CharSpawnShort@0xF3ECE0, itemId:=0)
	{
		// Wire: +0 u16 userIdx, +2 u16 partType, +4 dword itemId, +8 u8 colorOpt (spawn_despawn.md §2).
		const uint8* B = Pkt.Body.data();
		if (!NetNeedLen(Pkt.Body, 9, (int32)Pkt.Opcode)) { break; }   // N8 (дедуп №2, В2-A, 11.10.2026): была молчаливая проверка — теперь с Warning-строкой `NetNeedLen`
		// ★ Аудит №3 (знаковость): эталоны `Recv_CharFullSpawn@0xF3E970` и `Recv_CharSpawnShort@0xF3ECE0`
		//   принимают тело как `__int16 *a2` и читают ОБА поля ЗНАКОВО: `*a2` (userIdx) и `a2[1]`
		//   (partType — он же индекс экип-слота в `tagCHARACTER_SetPartsModelByType`).
		const int32 Uidx = AsdaBytes::RdI16(&B[0]);
		const int32 Part = AsdaBytes::RdI16(&B[2]);
		const int32 Item = (Pkt.Opcode == (uint16)OP_CHAR_SPAWN_SHORT) ? 0
			: AsdaBytes::RdI32(&B[4]);
		// ★ B[8] = УРОВЕНЬ ЗАТОЧКИ (эталон network.c:40631 пишет его в инстанс+0x18), не «цвет».
		const uint8 Grade = B[8];
		UE_LOG(LogAsdaNet, Log, TEXT("Часть экипа %u: userIdx=%d partType=%d itemId=%d заточка=%d"),
			Pkt.Opcode, Uidx, Part, Item, (int32)Grade);
		// ★ Своё оружие/щит на лету (смена/покупка/заточка в мире): дальность-тип атаки и свечение.
		// ★★★ ИСПРАВЛЕНО 2026-09-02 (К-6, вторая копия подпорки; дефект ПОРТА, провод не затронут).
		//   БЫЛО: `bWeaponSlot = (Part == 9 || Part == 6)`, `bShieldSlot = (Part == 7)` с
		//   пояснением «принимаем ОБА энума, чтобы не зависеть от того, какой шлёт наш сервер».
		//   Второго энума не существует — это выдумка порта, и комментарий врал вдобавок про
		//   диспетчер. Доказано с обеих сторон:
		//     • сервер `CharPutOnEquip@0x4CD730` (`GameServer.c:81808-81872`) кладёт в поле +2 тела
		//       4021/4023 НОМЕР ЭКИП-СЛОТА (`a3` ← байт +26 записи предмета) и сам пропускает
		//       через `switch` только набор {0,1,2,3,4,8,9};
		//     • эталонный приёмник `Recv_CharFullSpawn@0xF3E970` (`network.c:40637`) отдаёт это поле
		//       в `tagCHARACTER_SetPartsModelByType@0x9E18D0` (`entity_char.c:10955`), и её `switch`
		//       принимает тот же набор: 0 шлем, 1 грудь, 2 штаны, 3 обувь, 4 перчатки, 8 щит,
		//       9 оружие; значения 6 и 7 уходят в `default: result = 0`, то есть игнорируются.
		//   СТАЛО: единственный энум порта `EAsdaEquipSlot` (тот же, что применён в исправленном
		//   разборе `AsdaWorldSubsystem::OnCharPartUpdate`) — второй энум не заводим.
		const bool bWeaponSlot = (Part == (int32)EAsdaEquipSlot::Weapon);   // 9
		const bool bShieldSlot = (Part == (int32)EAsdaEquipSlot::Shield);   // 8
		if (IsSelfUserIdx((int32)Uidx) && (bWeaponSlot || bShieldSlot))
		{
			if (bWeaponSlot) { SelfWeaponItemId = Item; SelfWeaponGrade = (int32)Grade; }
			else             { SelfShieldItemId = Item; SelfShieldGrade = (int32)Grade; }
			UE_LOG(LogAsdaNet, Log, TEXT("Своё %s (смена %u): itemId=%d, заточка=%d"),
				bWeaponSlot ? TEXT("оружие") : TEXT("щит"), Pkt.Opcode, Item, (int32)Grade);
			OnSelfEquipGrade.Broadcast();
		}
		OnCharPartUpdate.Broadcast(Uidx, Part, Item, Grade);
		break;
	}
	// ★ 6539 0x198B — ОТВЕТ НА ЗАПРОС HP/MP ЦЕЛИ (мы шлём 6538 при выборе игрока целью).
	//   Эталон netRecvCharStateUpdate@0xE82A70 (network.c:30676-30709): memcpy 0x11 (17) байт,
	//     [0]    u8  subcode — применяется ТОЛЬКО при 1 или 3 (иные значения игнорируются);
	//     [1..4] i32 charId  — ключ CharManager_FindByCharId (НЕ userIdx!);
	//     [5..8] i32 maxHp;  [9..12] i32 curHp;  [13..14] i16 maxMp;  [15..16] i16 curMp;
	//   хвост — TargetNameplate_Update: полоса HP плашки ЦЕЛИ. Порт слал 6538 и НЕ читал ответ —
	//   у чужих игроков HP не появлялся вовсе.
	case OP_NET_RECV_CHAR_STATE_UPDATE:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 17, (int32)Pkt.Opcode))
		{
			auto U16 = [&B](int32 O) { return (int32)AsdaBytes::RdU16(&B[O]); };
			auto I32 = [&B](int32 O) { return AsdaBytes::RdI32(&B[O]); };
			const uint8 Sub = B[0];
			if (Sub == 1 || Sub == 3)
			{
				// ★ Аудит №3 (знаковость): эталон `netRecvCharStateUpdate` пишет ОБА поля MP через
				//   `*(_WORD *)&v5[13]` и `*(_WORD *)&v5[15]` — то есть БЕЗ знака (HP рядом — `_DWORD`,
				//   charId — `*(int *)&v5[1]`). Порт читал MP знаково.
				const int32 CharId = I32(1), MaxHp = I32(5), CurHp = I32(9), MaxMp = U16(13), CurMp = U16(15);
				UE_LOG(LogAsdaNet, Log, TEXT("[HP] 6539 (ответ на 6538) charId=%d: HP %d/%d, MP %d/%d (subcode %u)"),
					CharId, CurHp, MaxHp, CurMp, MaxMp, Sub);
				ASDA_CTRACE(EAsdaCT::Recv, TEXT("ПРИЁМ 6539 HP/MP цели-игрока (ответ на 6538): charId=%d HP %d/%d MP %d/%d подкод=%d"),
					CharId, CurHp, MaxHp, CurMp, MaxMp, (int32)Sub);
				OnCharStateUpdate.Broadcast(CharId, MaxHp, CurHp, MaxMp, CurMp);
			}
			else
			{
				UE_LOG(LogAsdaNet, Log, TEXT("[HP] 6539: subcode=%u — эталон применяет только 1 и 3, пропуск"), Sub);
				ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ПРИЁМ 6539 подкод=%d — цель не найдена/не применяется (эталон берёт только 1 и 3)"), (int32)Sub);
			}
		}
		break;
	}
	// ★ 4060 0xFDC — СТАТ-БЛОК СВОЕГО ПЕРСОНАЖА (RecvPacket_CharStatBlockA@0xB93E30,
	//   network.c:10188-10202): эталон делает memcpy 0x44 (68) байт тела ПРЯМО в CCharacter начиная
	//   с поля maxHp, затем hudRefreshFromCharacterStats@0xE796A0 (полная пересборка HUD). Раскладка
	//   структуры (live.dmp, локальный чар @0x27D30898): maxHp d@+1124, maxMp w@+1128,
	//   curHp d@+1130, curMp w@+1134 ⇒ в теле пакета: maxHp d@0, maxMp w@4, curHp d@6, curMp w@10.
	//   Живые байты 2026-08-04: [25 0B 00 00][A2 00][92 05 00 00][51 00] = 2853 / 162 / 1426 / 81 —
	//   ровно значения char-list. Пакет приходит на входе в мир и при смене статов, БЕЗ userIdx
	//   (эталон пишет всегда локальному игроку).
	case OP_RECV_PACKET_CHAR_STAT_BLOCK_A:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 12, (int32)Pkt.Opcode))
		{
			auto I16 = [&B](int32 O) { return AsdaBytes::RdI16(&B[O]); };
			auto I32 = [&B](int32 O) { return AsdaBytes::RdI32(&B[O]); };
			const int32 MaxHp = I32(0), MaxMp = I16(4), CurHp = I32(6), CurMp = I16(10);
			UE_LOG(LogAsdaNet, Log, TEXT("[HP] 4060 стат-блок self: HP %d/%d, MP %d/%d (эталон: memcpy 0x44 в CCharacter+1124 + hudRefreshFromCharacterStats)"),
				CurHp, MaxHp, CurMp, MaxMp);
			ASDA_CTRACE(EAsdaCT::Recv, TEXT("ПРИЁМ 4060 стат-блок своего персонажа: HP %d/%d MP %d/%d"), CurHp, MaxHp, CurMp, MaxMp);
			SetVitalsHp((int32)SelfUserNum, CurHp, MaxHp, TEXT("4060 стат-блок"), /*bServerSnapshot=*/true);
			SetVitalsMp((int32)SelfUserNum, CurMp, MaxMp, TEXT("4060 стат-блок"), /*bServerSnapshot=*/true);
		}
		// ★★★ ОСТАЛЬНОЙ БОЕВОЙ БЛОК. Эталон копирует ВСЕ 68 байт, а не только HP/MP: сразу за
		//   HP/MP в нём лежат физ./маг. атака (+12/+14/+16/+18 от начала блока = CCharacter+1136…
		//   +1142). Именно эти слова читает отражение урона подтипа 5
		//   (`cclassApplyReflectSkillDamage@0xF8A970`) — до этой правки порт их выбрасывал, хотя
		//   они приходили в том же теле.
		if (NetNeedLen(B, 20, (int32)Pkt.Opcode))
		{
			// Ключ карты — свой userNum из 4003. Если 4060 обогнал 4003, ключа ещё нет: пишем след,
			//   а не молча теряем (сервер шлёт 4060 при каждом изменении статов — следующий догонит).
			if (bSelfUserNumKnown) { SetCombatStatsFromBlock((int32)SelfUserNum, B.data(), (int32)B.size(), TEXT("4060")); }
			else { UE_LOG(LogAsdaNet, Warning, TEXT("[статы] 4060 пришёл ДО 4003 (SelfUserNum=0) — боевой блок не к чему привязать")); }
		}
		// ★★★ ВЕСЬ ОСТАЛЬНОЙ БЛОК — защита, маг. защита, шанс блока и 12 стихийных чисел. Эталон
		//   копирует все 68 байт и тут же зовёт `UI_FillEquipDetailPanel` (окно 900 §3.4 отчёта).
		//   Блок ВСЕГДА про своего персонажа — userIdx в теле нет.
		SetSelfStatBlockFrom68(B.data(), (int32)B.size(), TEXT("4060"));
		break;
	}
	// ★ 4064 0xFE0 — БАЗОВЫЕ СТАТЫ СВОЕГО ПЕРСОНАЖА (`RecvPacket_CharStatBlockB@0xB93F10`,
	//   network.c:10206): `memcpy(CCharacter+1192, тело, 0x24)` + `UI_FillEquipDetailPanel`.
	//   Тело — 18 слов: 6 базовых, 6 прибавок, 6 не читаемых окном (см. отчёт §3.3).
	//   Сервер: `sub_52C230@0x52C230` → `sub_4C28F0(…, 4064, 36, a2, v6, a2, 1)` (GameServer.c:113912).
	case OP_RECV_PACKET_CHAR_STAT_BLOCK_B:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 36, (int32)Pkt.Opcode)) { SetSelfBaseStatsFrom36(B.data(), (int32)B.size(), TEXT("4064")); }
		break;
	}
	// ★ 5036 0x13AC — ПАКЕТ ЛАВКИ ЗАТОЧКИ (`Recv_RefineShop@0xE01930`, network.c:27710):
	//   `memcpy(this->filler+10, тело, 0x94)`, а `tagRefineShop_RecvPacket@0xE019E0` (item_enhance.c:5807)
	//   берёт оттуда `filler[152]` в глобал `g_nShopValueA` — это ОЧКИ РЕМЕСЛА, которые окно
	//   характеристик показывает в `STATUS_ST12` (`shopUiWnd_setValueText@0xB94C50`).
	//   `filler[152]` = тело+142 (dword). Само окно заточки в порт ещё не портировано — здесь берём
	//   ТОЛЬКО это число, чтобы строка «Ур. ремесла» не оставалась нулём.
	case OP_REFINE_SHOP:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 146, (int32)Pkt.Opcode))
		{
			SelfCraftSkill = AsdaBytes::RdI32(&B[142]);
			UE_LOG(LogAsdaNet, Log, TEXT("[статы] 5036: очки ремесла = %d (эталон g_nShopValueA ← filler[152])"),
				SelfCraftSkill);
			OnSelfStatsChanged.Broadcast();
		}
		break;
	}
	// ★★★ 4025 0xFB9 — ДЕЙСТВИЕ/ЭМОЦИЯ ЛЮБОГО ПЕРСОНАЖА (`Recv_CharSpawnFx@0xF3AF00`, network.c:39532).
	//   Тело 13 б, ровно то же, что клиент шлёт в 4024: userIdx u16@0, actionType u16@2, флаг шлема u8@4,
	//   X f32@5, Z f32@9 (НАПРАВЛЕНИЕ взгляда, не позиция — `rendererSetField5Vec3`, game.c:551525). Сервер
	//   (`sub_546730`, S:124795) рассылает 4025 соседям в секторе через `SendPacket_ClippingSectorInOtherUser` —
	//   ОТПРАВИТЕЛЮ НЕ ПРИХОДИТ (`if (v14 != a3)` @0x41EA80; ★ волна В5 рыбалки 08.10.2026: прежнее «включая самого
	//   отправителя» было неверно, client_net.md §3.1). Разбор actionType (idle/движение/эмоции 100…117/рыбалка
	//   122…129/GM-невидимость) делает мир — здесь только раскладываем поля.
	case OP_CHAR_SPAWN_FX:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 13, (int32)Pkt.Opcode)) { break; }
		const int32 Uidx = (int32)AsdaBytes::RdU16(&B[0]);
		const int32 Act  = (int32)AsdaBytes::RdU16(&B[2]);
		float X = AsdaBytes::RdF32(B.data() + 5);   // ★ N6: `AsdaBytes`
		float Z = AsdaBytes::RdF32(B.data() + 9);
		UE_LOG(LogAsdaNet, Log, TEXT("4025 действие: userIdx=%d actionType=%d поз=(%.0f,%.0f)"), Uidx, Act, X, Z);
		ASDA_CTRACE_V(EAsdaCT::Recv, TEXT("ПРИЁМ 4025 действие персонажа: uidx=%d действие=%d курс=(%.3f,%.3f)"), Uidx, Act, X, Z);
		OnCharAction.Broadcast(Uidx, Act, X, Z);
		// ★★★ 25.10.2026 [ОРИГИНАЛ] действие 111 — СМЕНА ПОКАЗА ШЛЕМА (`Recv_CharSpawnFx`, network.c:39837-39865): байт @4 ≠ 0 → показ.
		//   Сервер шлёт его соседям, когда 4070 меняет `CUser+0x4A6` (`sub_52A0C0`); в остальных действиях @4 эталон не читает.
		if (Act == 111)
		{
			const bool bShow = (B[4] != 0);
			UE_LOG(LogAsdaNet, Log, TEXT("4025 действие 111: userIdx=%d показ шлема=%d"), Uidx, bShow ? 1 : 0);
			OnHelmetShowSync.Broadcast(Uidx, bShow);
		}
		break;
	}
	// ★ 10.10.2026 (копание): 5430 0x1536 — ИТОГ КОПКИ (`network.c:1530-1592`; `OP_MINING_RESULT`, до 10.10.2026 — миснейм `OP_EFFECT_SPAWN_BY_NAME` по вызову
	//   внутри обработчика). Тело 3 б: итог u8@0 (1 — удачно), userNum i16@1. Разбор у объявления `OnMiningResult`; нотис 9003/9004,
	//   эффект и звук итога — `UAsdaWorldSubsystem::OnMiningResult`. До 10.10.2026 обработчика не было вовсе — итог копки в системный
	//   чат не попадал, эффекта и звука итога не было (жалоба владельца, живой тест 10.10).
	case OP_MINING_RESULT:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 3, (int32)Pkt.Opcode)) { break; }
		const int32 Result = (int32)B[0];
		const int32 Uidx   = (int32)AsdaBytes::RdU16(&B[1]);
		UE_LOG(LogAsdaNet, Log, TEXT("5430 итог копки: %s (код %d), userIdx=%d"), Result == 1 ? TEXT("удачно") : TEXT("не удалась"), Result, Uidx);
		OnMiningResult.Broadcast(Result, Uidx);
		break;
	}
	// ★ 5426 0x1532 — ЛЕЧЕНИЕ (Recv_PotionBuff@0xF54B40, network.c:47241+). Тело (a1 = i16*):
	//     userIdx i16@0, НОВЫЙ HP i32@6.
	//   Эталон применяет ТОЛЬКО при `newHp >= curHp` (это ветка лечения: зелье/умение соулмейта 0x24)
	//   и пишет `*(&iMaxMp+1)` = curHp (0x46A), тут же зовя UI_UpdateHPBar. MP пакет НЕ трогает.
	//   Сопутствующие эффекты/звук/чат (loc 9579) — система соулмейта, здесь не дублируем.
	case OP_POTION_BUFF:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 10, (int32)Pkt.Opcode))
		{
			// ★ Аудит №3 (знаковость): эталон `bool __cdecl Recv_PotionBuff(__int16 *a1)` —
			//   `CharManager_FindByUserIdx(mgr, *a1)`; новый HP рядом = `*(_DWORD *)(a1 + 3)`.
			const int32 Uidx  = AsdaBytes::RdI16(&B[0]);
			const int32 NewHp = AsdaBytes::RdI32(&B[6]);
			const FAsdaVitals* Prev = VitalsByUserIdx.Find(Uidx);
			const bool bHeal = !Prev || Prev->HpCur < 0 || NewHp >= Prev->HpCur;
			UE_LOG(LogAsdaNet, Log, TEXT("[HP] 5426 лечение: userIdx=%d HP %d→%d (%s)"),
				Uidx, Prev ? Prev->HpCur : -1, NewHp, bHeal ? TEXT("применяем") : TEXT("эталон игнорирует: меньше текущего"));
			ASDA_CTRACE(bHeal ? EAsdaCT::Recv : EAsdaCT::Refuse, TEXT("ПРИЁМ 5426 лечение: uidx=%d%s HP %d→%d (%s)"),
				Uidx, IsSelfUserIdx(Uidx) ? TEXT(" (Я)") : TEXT(""), Prev ? Prev->HpCur : -1, NewHp,
				bHeal ? TEXT("применяем") : TEXT("отброшено: меньше текущего, эталон игнорирует"));
			if (bHeal) { SetVitalsHp(Uidx, NewHp, -1, TEXT("5426 лечение"), /*bServerSnapshot=*/true); }
		}
		break;
	}
	// ★ 6580 0x19B4 — РЕЗУЛЬТАТ РУЛЕТКИ (Recv_RouletteResult, диспетч network.c:924, тело = pkt+9).
	//   Ветка `pkt[5] == 3` («Roul_HPMP.jeb», network.c:30167-30179) — приз в виде ОЗ/ОМ:
	//     charId dword@+1; если это МОЙ charId →
	//       *(dword*)(LocalPlayer+1130) = *(dword*)(pkt+67)   // curHp  (0x46A)
	//       *(word *)(LocalPlayer+1134) = *(word *)(pkt+71)   // curMp  (0x46E)
	//       UI_UpdateHPBar(curHp, maxHp);  UI_UpdateMPBar(curMp, maxMp);
	//   Для ЧУЖОГО чара пишутся те же поля его CCharacter. Это ещё один живой источник ОЗ/ОМ
	//   помимо 4058/4059/4060/4055/4056/6539 — заводим, чтобы полосы не «застревали» после приза.
	case OP_ROULETTE_RESULT:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 73, (int32)Pkt.Opcode) && B[5] == 3)
		{
			const int32 CharId = AsdaBytes::RdI32(&B[1]);
			const int32 CurHp  = AsdaBytes::RdI32(&B[67]);
			const int32 CurMp  = AsdaBytes::RdI16(&B[71]);
			const bool bSelf = (SelectedCharId != 0 && CharId == SelectedCharId);
			UE_LOG(LogAsdaNet, Log, TEXT("6580 рулетка (приз ОЗ/ОМ): charId=%d (свой=%s) HP=%d MP=%d"),
				CharId, bSelf ? TEXT("да") : TEXT("нет"), CurHp, CurMp);
			ASDA_CTRACE(EAsdaCT::Recv, TEXT("ПРИЁМ 6580 рулетка, приз ОЗ/ОМ: charId=%d%s HP→%d MP→%d"), CharId, bSelf ? TEXT(" (Я)") : TEXT(""), CurHp, CurMp);
			if (bSelf)
			{
				SetVitalsHp((int32)SelfUserNum, CurHp, -1, TEXT("6580 рулетка"), /*bServerSnapshot=*/true);
				SetVitalsMp((int32)SelfUserNum, CurMp, -1, TEXT("6580 рулетка"), /*bServerSnapshot=*/true);
			}
			else
			{
				OnCharStateUpdate.Broadcast(CharId, -1, CurHp, -1, CurMp);   // разрешение charId→userIdx в мир-подсистеме
			}
		}
		// ★ Тело 6580 целиком уходит подписчику рулетки (раскладка 73 б — у объявления
		//   `OnRoulettePrize`): вид приза, запись предмета 57 б, опыт, ОЗ/ОМ. Вещаем ЗДЕСЬ,
		//   в уже существующем `case`, — второй `case` на 6580 дал бы «duplicate case».
		OnRoulettePrize.Broadcast(NetBodyToArray(Pkt.Body));
		break;
	}
	// ★★★ 4058 0xFDA — СЕРВЕРНЫЙ СНАПШОТ HP ЛЮБОГО ИГРОКА (netHandleHpMpUpdate@0xF43D00,
	//   network.c:42728-42817). Раскладка подтверждена и кодом эталона, и живыми байтами 2026-08-04
	//   (регенерация вне боя, шаг 71 HP каждые 5 с, потолок = maxHp@2). См. NetOpcodes.h.
	case OP_NET_HANDLE_HP_MP_UPDATE:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() >= 17)
		{
			auto I16 = [&B](int32 O) { return AsdaBytes::RdI16(&B[O]); };
			auto I32 = [&B](int32 O) { return AsdaBytes::RdI32(&B[O]); };
			// ★ Аудит №3 (знаковость): эталон `netHandleHpMpUpdate@0xF43D00` — `__int16 *a2`,
			//   `CharManager_FindByUserIdx(mgr, *a2)`; дельта тоже знаковая (`a2[3] - curHp`).
			const int32 Uidx   = I16(0);
			const int32 MaxHp  = I32(2);
			const int32 CurHp  = I32(6);
			const uint8 UpdT   = B[10];
			const int32 SkillId= I16(11);
			const int32 SecHp  = I32(13);
			// Дельта для всплывающей цифры — РАЗНИЦА С ПРЕЖНИМ значением (эталон: hpMpDelta =
			//   a2[3] − CCharacter.curHp, i16). Считаем ДО записи нового значения.
			const FAsdaVitals* Prev = VitalsByUserIdx.Find(Uidx);
			const int32 Delta = (Prev && Prev->HpCur >= 0) ? (CurHp - Prev->HpCur) : 0;
#if ASDA_COMBAT_TRACE
			// Журнал боя (ДО записи — указатель Prev ещё действителен): урон/цифра (тип 1/4/6), свой HP, наш игрок — событие;
			//   прочее (реген соседей раз в 5 с) — только уровень 2. Событие по ЧУЖОМУ игроку (не я, не цель PvP-цикла,
			//   не участник нашего боя) — тоже уровень 2 (канал общий: 4058 приходит по всем игрокам сектора).
			//   Прежнее сравнение с CurrentCycleTarget() (rid МОБА; у PvP-цикла теперь −1) заменено на IsUserRelevant.
			{
				const bool bSelfV = IsSelfUserIdx(Uidx);
				const bool bOursV = bSelfV || AsdaCombatTrace::IsUserRelevant(Uidx);
				const bool bEvent = bOursV || Delta < 0 || UpdT == 1 || UpdT == 4 || UpdT == 6;
				if (bEvent)
				{
					ASDA_CTRACE_IF(bOursV, EAsdaCT::Recv, TEXT("ПРИЁМ 4058 HP игрока uidx=%d%s: %d→%d / %d (дельта %+d) тип=%d%s навык=%d второй=%d"),
						Uidx, bSelfV ? TEXT(" (Я)") : TEXT(""), Prev ? Prev->HpCur : -1, CurHp, MaxHp, Delta, (int32)UpdT,
						(UpdT == 1 || UpdT == 4 || UpdT == 6) ? TEXT("(с цифрой)") : TEXT("(тихо)"), SkillId, SecHp);
				}
				else
				{
					ASDA_CTRACE_V(EAsdaCT::Recv, TEXT("ПРИЁМ 4058 HP соседа uidx=%d: %d→%d / %d (дельта %+d) тип=%d"),
						Uidx, Prev ? Prev->HpCur : -1, CurHp, MaxHp, Delta, (int32)UpdT);
				}
			}
#endif
			SetVitalsHp(Uidx, CurHp, MaxHp, TEXT("4058"), /*bServerSnapshot=*/true);
			// ★ «Отстающий» HP для слоя MINISTATE_HPBuff: эталон пишет его в `gap_4C0[12]` и
			//   ОБНУЛЯЕТ, если он равен текущему (network.c:42804-42808) — тогда слой нулевой ширины.
			// ★★★ ДОРАЗБОР 2026-08-24 (было помечено «не разобрано»). Эталон гасит второе значение
			//   ТРЕМЯ правилами, и два из них жили только в покадровом `HUD_PlayerBuffIcons_Update`
			//   (`ui_core.c:76246`/`:76259`): «текущее >= максимума» и «второе <= текущего».
			//   Ключ к ним — поле `&iMaxMp + 1`, которое в срезе значилось неразобранным: это
			//   ТЕКУЩЕЕ здоровье. Доказательство — `combat_pvp.c:7780-7787` вычитает из него урон
			//   и ЭТИМ ЖЕ значением зовёт `UI_UpdateHPBar_thunk(g_pHUD, значение, maxHp)`.
			//   Применяем их сразу при приёме: у эталона тот же результат наступил бы кадром позже.
			//   Без этого слой не гас никогда (владелец: «по истечении действия зелья не исчезает»).
			{
				FAsdaVitals& VV = VitalsByUserIdx.FindOrAdd(Uidx);
				const bool bDone = (SecHp <= CurHp) || (MaxHp > 0 && CurHp >= MaxHp);
				VV.HpSecond = bDone ? 0 : SecHp;
				OnVitalsChanged.Broadcast(Uidx, VV);
			}
			// Эталон рисует цифру только для updateType 1/4/6 (иначе LABEL_17 — тихая запись).
			if (UpdT == 1 || UpdT == 4 || UpdT == 6)
			{
				// ★ R5-fix8 (14.11.2026) [НАШЕ ЛУЧШЕ ОРИГИНАЛА]: оригинал рисует цифру и при дельте 0; порт — нет (осознанно, слово
				//   владельца 14.11). Оригинал — значение 1 ключа `asda.Vitals.HpZeroDigit` (разбор у ключа).
				bVitalsFxZeroDigit = CVarAsdaVitalsHpZeroDigit.GetValueOnGameThread() != 0;
				OnVitalsFx.Broadcast(Uidx, Delta, UpdT, SkillId);
				bVitalsFxZeroDigit = false;
			}
			// ★ R5-fix6 (13.11.2026) [ОРИГИНАЛ]: тип 4/6 — ещё и эффект мгновенного состояния (network.c:42767-42808); от дельты не зависит.
			if (UpdT == 4 || UpdT == 6) { OnVitalsStateFx.Broadcast(Uidx, UpdT, SkillId, /*bMp=*/false); }
			UE_LOG(LogAsdaNet, Log, TEXT("[HP] 4058 userIdx=%d (свой=%s): HP %d→%d / %d (дельта %+d), тип=%u skill=%d second=%d%s"),
				Uidx, AsdaSelf::IsSelfUserIdx(this, Uidx) ? TEXT("да") : TEXT("нет"),
				Prev ? Prev->HpCur : -1, CurHp, MaxHp, Delta, UpdT, SkillId, SecHp,
				(SecHp == CurHp) ? TEXT(" (== cur → эталон обнуляет)") : TEXT(""));
		}
		else
		{
			UE_LOG(LogAsdaNet, Warning, TEXT("[HP] 4058: тело %d б < 17 — пакет короче раскладки эталона, пропуск"), (int32)Pkt.Body.size());
		}
		break;
	}
	// ★ 4059 0xFDB — СНАПШОТ MP (pkt_OnMpSkillFxUpdate@0xF440E0, entity_char.c:53081). Эталон берёт
	//   ТОЛЬКО curMp@4 (a1[2]) и максимум из CCharacter.iMaxMp; поле @2 он не читает. Живые байты:
	//   @2 == MpMax из char-list (162) — используем его ТОЛЬКО как фолбэк, если максимум неизвестен.
	case OP_PKT_ON_MP_SKILL_FX_UPDATE:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 11, (int32)Pkt.Opcode))
		{
			auto I16 = [&B](int32 O) { return AsdaBytes::RdI16(&B[O]); };
			// ★ Аудит №3 (знаковость): эталон объявлен как
			//   `char __cdecl pkt_OnMpSkillFxUpdate(__int16 *a1)` — `*a1` читается ЗНАКОВО.
			const int32 Uidx    = I16(0);
			const int32 PktMax  = I16(2);
			const int32 CurMp   = I16(4);
			const uint8 UpdT    = B[6];
			const int32 SkillId = I16(7);
			const int32 SecMp   = I16(9);
			const FAsdaVitals* Prev = VitalsByUserIdx.Find(Uidx);
			const int32 Delta = (Prev && Prev->MpCur >= 0) ? (CurMp - Prev->MpCur) : 0;
			const int32 KnownMax = (Prev && Prev->MpMax > 0) ? Prev->MpMax : PktMax;
#if ASDA_COMBAT_TRACE
			// Журнал боя (ДО записи — указатель Prev ещё действителен): расход/цифра MP навыка (тип 4/6) и своё MP — событие;
			//   событие по чужому игроку (канал общий) — уровень 2 (UID), своё — всегда 1.
			if (IsSelfUserIdx(Uidx) || UpdT == 4 || UpdT == 6)
			{
				ASDA_CTRACE_IF(IsSelfUserIdx(Uidx) || AsdaCombatTrace::IsUserRelevant(Uidx), EAsdaCT::Recv, TEXT("ПРИЁМ 4059 MP uidx=%d%s: %d→%d / %d (дельта %+d) тип=%d навык=%d второй=%d"),
					Uidx, IsSelfUserIdx(Uidx) ? TEXT(" (Я)") : TEXT(""), Prev ? Prev->MpCur : -1, CurMp, KnownMax, Delta, (int32)UpdT, SkillId, SecMp);
			}
			else
			{
				ASDA_CTRACE_V(EAsdaCT::Recv, TEXT("ПРИЁМ 4059 MP соседа uidx=%d: %d→%d / %d тип=%d"), Uidx, Prev ? Prev->MpCur : -1, CurMp, KnownMax, (int32)UpdT);
			}
#endif
			SetVitalsMp(Uidx, CurMp, KnownMax, TEXT("4059"), /*bServerSnapshot=*/true);
			// ★ «Отстающий» MP для слоя MINISTATE_MPBuff (эталон `gap_4C0[16]`, симметрично 4058).
			//   ★★★ Те же три правила гашения, что и у HP (см. 4058). Текущая мана у эталона —
			//   `SHIWORD(iCurrentHp)`: `pkt_OnMpSkillFxUpdate@0xF440E0` (`entity_char.c:53093`)
			//   пишет туда значение и ИМ ЖЕ зовёт `UI_UpdateMPBar`.
			{
				FAsdaVitals& VV = VitalsByUserIdx.FindOrAdd(Uidx);
				const bool bDone = (SecMp <= CurMp) || (KnownMax > 0 && CurMp >= KnownMax);
				VV.MpSecond = bDone ? 0 : SecMp;
				OnVitalsChanged.Broadcast(Uidx, VV);
			}
			// ★ R5-fix8 (14.11.2026) [НАШЕ ЛУЧШЕ ОРИГИНАЛА]: у пакета маны оригинал цифру НЕ рисует; порт рисует (осознанно, слово
			//   владельца 14.11). Оригинал — значение 0 ключа `asda.Vitals.MpDigit` (разбор у ключа).
			if ((UpdT == 4 || UpdT == 6) && CVarAsdaVitalsMpDigit.GetValueOnGameThread() != 0) { OnVitalsFx.Broadcast(Uidx, Delta, UpdT, SkillId); }
			// ★ R5-fix6 (13.11.2026) [ОРИГИНАЛ]: тип 4/6 — эффект мгновенного состояния (entity_char.c:53102-53138).
			if (UpdT == 4 || UpdT == 6) { OnVitalsStateFx.Broadcast(Uidx, UpdT, SkillId, /*bMp=*/true); }
			UE_LOG(LogAsdaNet, Verbose, TEXT("[MP] 4059 userIdx=%d: MP %d→%d / %d (пакет @2=%d, дельта %+d), тип=%u skill=%d second=%d"),
				Uidx, Prev ? Prev->MpCur : -1, CurMp, KnownMax, PktMax, Delta, UpdT, SkillId, SecMp);
		}
		break;
	}
	case OP_NET_HANDLER_ENTER_WORLD:      // 4055 0xFD7 — ответ на 4054 (респаун/смена карты): позиция+HP/MP/EXP
	case OP_INS_DUNGEON_APPLY_PICK_INFO:  // 4056 0xFD8 — ★ ЖИВОЙ ТЕСТ 2026-07-22: при ПЕРВОМ входе сервер шлёт
	{                                     //   именно 4056 («прибытие»), не 4055. Оба = «игрок в мире».
		// ★ HUD: опыт self из пакета входа (эталон netHandler_EnterWorld: exp i64 @+35, HP@+29, MP@+33;
		//   GameNet_OnLocalPlayerMapEntry (4056): exp i64 @+12, HP dword@+6, MP i16@+10) → EXP-бар.
		const std::vector<uint8>& B = Pkt.Body;
		const int32 ExpOff = (Pkt.Opcode == (uint16)OP_NET_HANDLER_ENTER_WORLD) ? 35 : 12;
		// ★★★ 22.10.2026: «это про меня?» — опыт и «я в мире» только своему userIdx (разбор у `CVarAsdaNetWorldReadySelfOnly`).
		//   Свой userIdx ещё не известен (4003 не приходил) — пакет считаем своим: так прежняя страховка снятия загрузки цела.
		const bool bEntryAboutMe = CVarAsdaNetWorldReadySelfOnly.GetValueOnGameThread() == 0
			|| !AsdaSelf::IsUserIdxKnown(this)
			|| (NetNeedLen(B, 2, (int32)Pkt.Opcode) && AsdaSelf::IsSelfUserIdx(this, AsdaBytes::RdI16(&B[0])));
		if (bEntryAboutMe && NetNeedLen(B, ExpOff + 8, (int32)Pkt.Opcode))
		{
			int64 E = 0; FMemory::Memcpy(&E, B.data() + ExpOff, 8);
			if (E >= 0)
			{
				SelfExp = E;
				UE_LOG(LogAsdaNet, Log, TEXT("%u: exp self = %lld"), Pkt.Opcode, (long long)E);
				OnSelfExp.Broadcast((int32)SelfLevel, SelfExp, /*gained=*/0, /*bFullExp=*/false, /*srcRid=*/-1);
			}
		}
		// ★ 30.10.2026 (боевая фаза «Битвы армий»): `netHandler_EnterWorld@0xF43050` (только свой) — при +0x2388 == 1
		//   Toggle(0,1) + ResetState + ResetPositions (разбор отложен в Tick подсистемы).
		if (bEntryAboutMe && Pkt.Opcode == (uint16)OP_NET_HANDLER_ENTER_WORLD)
		{
			if (UAsdaCampWarBattleSubsystem* CwB = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCampWarBattleSubsystem>() : nullptr)
			{
				CwB->OnLeaveByServerMove(TEXT("4055 воскрешение со сменой карты"));
			}
		}
		if (bEntryAboutMe)
		{
			UE_LOG(LogAsdaNet, Log, TEXT("%u: сервер подтвердил вход (%d б) — игрок В МИРЕ"), Pkt.Opcode, (int32)Pkt.Body.size());
			LastWorldReadyOpcode = (int32)Pkt.Opcode;   // ★ 22.10.2026: ветка эталона для списка окон (AsdaEntryWindows)
			bLastWorldReadyDeadArrival = (Pkt.Opcode == (uint16)OP_INS_DUNGEON_APPLY_PICK_INFO)
				&& NetNeedLen(B, 22, (int32)Pkt.Opcode) && (AsdaBytes::RdI16(&B[20]) == 200);
			//   Та же формула, что у ветки воскрешения ниже: `NewMap >= 0 && NewMap != EnterMapId`.
			bLastWorldReadyMapChange = (Pkt.Opcode == (uint16)OP_NET_HANDLER_ENTER_WORLD)
				&& B.size() > 2 && ((int32)B[2] != EnterMapId);
			OnWorldReady.Broadcast();   // B6: снять загрузочный экран, игровое состояние
		}
		else
		{
			UE_LOG(LogAsdaNet, Log, TEXT("%u: прибытие/воскрешение ДРУГОГО игрока (%d б) — «я в мире» и опыт не трогаем (эталон, ветка чужого)"),
				Pkt.Opcode, (int32)Pkt.Body.size());
		}
		// ★ ЭФФЕКТ ПОЯВЛЕНИЯ (эталон GameNet_OnLocalPlayerMapEntry@0xF43390): userIdx u16@+0.
		//   4056 приходит и о ЧУЖОМ игроке (сервер шлёт всем в секторе при его воскрешении/прибытии) —
		//   эталон играет cmchar01+S_c_entry на нём же. 4056 wire (22 б): uidx@0, tileX@2, tileY@4,
		//   HP i32@6, MP i16@10, exp i64@12, state i16@20 (200 = прибыл мёртвым, эталон pPacket[10]).
		if (NetNeedLen(B, 2, (int32)Pkt.Opcode))
		{
			// ★ Аудит №3 (знаковость): эталон `GameNet_OnLocalPlayerMapEntry(__int16 *pPacket)` читает
			//   `*pPacket` (userIdx), `pPacket[1]`/`pPacket[2]` (тайлы) и `pPacket[10] == 200` ЗНАКОВО.
			const int32 EntryUidx = AsdaBytes::RdI16(&B[0]);
			const bool bDeadArrival = (Pkt.Opcode == (uint16)OP_INS_DUNGEON_APPLY_PICK_INFO)
				&& NetNeedLen(B, 22, (int32)Pkt.Opcode) && (AsdaBytes::RdI16(&B[20]) == 200);
			UE_LOG(LogAsdaNet, Log, TEXT("%u: прибытие userIdx=%d%s → эффект появления"),
				Pkt.Opcode, EntryUidx, bDeadArrival ? TEXT(" (МЁРТВЫМ)") : TEXT(""));
			ASDA_CTRACE(EAsdaCT::Recv, TEXT("ПРИЁМ %d прибытие/воскрешение: uidx=%d%s%s (у меня мёртв=%s)"), (int32)Pkt.Opcode, EntryUidx,
				AsdaSelf::IsSelfUserIdx(this, EntryUidx) ? TEXT(" (Я)") : TEXT(""), bDeadArrival ? TEXT(" — прибыл МЁРТВЫМ (state 200)") : TEXT(""),
				bSelfDead ? TEXT("да") : TEXT("нет"));
			// ★ Снятие «мёртв»: ЛЮБАЯ ветка воскрешения (CharRegenVillage 4055 / CharRegenVillageMove /
			//   CharRegenSelfPosition 4056) обнуляет на сервере CUser+79 и CUser+21732 (sub_4CB070(...,0)).
			//   state@20 == 200 = прибытие мёртвым (смена карты трупом) — состояние сохраняем.
			if (AsdaSelf::IsSelfUserIdx(this, EntryUidx) && !bDeadArrival && bSelfDead)   // E2
			{
				bSelfDead = false;
				bSelfSpawnDead = false;      // ★ снимаем и «вошёл мёртвым», иначе следующий спавн повторит позу трупа
				bSelfDeathNoClip = false;
				UE_LOG(LogAsdaNet, Log, TEXT("%u: воскрешение подтверждено сервером — действия разблокированы"), Pkt.Opcode);
				ASDA_CTRACE(EAsdaCT::Death, TEXT("СВОЁ воскрешение подтверждено сервером (%d): состояние смерти снято → OnCharRevived"), (int32)Pkt.Opcode);
				OnCharRevived.Broadcast();
			}
			// ★ 05.11.2026 [ОРИГИНАЛ] (`asda.Peer.Revive4056`): ветка ЧУЖОГО `GameNet_OnLocalPlayerMapEntry@0xF43390` (game_main.c:13059)
			//   ставит соседу HP (dword @6) и MP (i16 @10) — здесь, в хранилище; тайл и позу берёт мир из `AsdaPeerEntry4056`.
			GAsdaPeer4056 = FAsdaPeer4056{};
			if (Pkt.Opcode == (uint16)OP_INS_DUNGEON_APPLY_PICK_INFO && !AsdaSelf::IsSelfUserIdx(this, EntryUidx) && NetNeedLen(B, 12, (int32)Pkt.Opcode))
			{
				GAsdaPeer4056 = FAsdaPeer4056{ EntryUidx, AsdaBytes::RdI16(&B[2]), AsdaBytes::RdI16(&B[4]),
					AsdaBytes::RdI32(&B[6]), AsdaBytes::RdI16(&B[10]), bDeadArrival, true };
				if (AsdaPeerRevive4056On())
				{
					SetVitalsHp(EntryUidx, GAsdaPeer4056.Hp, -1, TEXT("4056 соседа"), /*bServerSnapshot=*/true);
					SetVitalsMp(EntryUidx, GAsdaPeer4056.Mp, -1, TEXT("4056 соседа"), /*bServerSnapshot=*/true);
				}
			}
			OnMapEntryFx.Broadcast(EntryUidx, bDeadArrival);
			GAsdaPeer4056.bValid = false;   // разбор только на время рассылки
			if (Pkt.Opcode == (uint16)OP_INS_DUNGEON_APPLY_PICK_INFO) { OnMapEntry4056.Broadcast(EntryUidx, bDeadArrival); }   // ★ 07.10.2026: только 4056

			// ★★★ ПОЗИЦИЯ ВОСКРЕШЕНИЯ — ЕЁ ЗАДАЁТ СЕРВЕР, А НЕ КЛИЕНТ (живой тест: после ОК персонаж
			//   вставал НА МЕСТЕ СМЕРТИ). «Город привязки» — переменная: сервер берёт её из CUser
			//   (`gap_552E+27259` карта сейва, `+27260/+27262` тайл) и присылает НОВЫЕ КООРДИНАТЫ:
			//     • `CharRegenVillageMove@0x52AB80` (сейв на ТЕКУЩЕЙ карте) — 4056, 22 б:
			//         a6[0]=userNum, a6[1]=tileX (CUser+21734), a6[2]=tileY (+21736) → ТЕЛЕПОРТ;
			//     • `CharRegenVillage@0x52A780` (сейв на ДРУГОЙ карте) — 4055, 43 б:
			//         userNum i16@0, mapId u8@2, …, tileX i16@25, tileY i16@27, HP@29, MP@33, exp@35
			//         → СМЕНА КАРТЫ с загрузкой уровня.
			//   То же 4056 шлёт `CharRegenSelfPosition` (воскрешение на месте) — там тайл прежний,
			//   телепорт получается нулевой, поэтому ветка едина для всех вариантов воскрешения.
			if (AsdaSelf::IsSelfUserIdx(this, EntryUidx))   // E2
			{
				int32 NewMap = -1, TX = -1, TZ = -1, Hp = -1, Mp = -1;
				FString PortIp; int32 PortPort = 0;
				if (Pkt.Opcode == (uint16)OP_INS_DUNGEON_APPLY_PICK_INFO && NetNeedLen(B, 12, (int32)Pkt.Opcode))
				{
					// 4056 (22 б): uidx@0, tileX@2, tileY@4, HP i32@6, MP i16@10 — та же карта.
					TX = AsdaBytes::RdI16(&B[2]);
					TZ = AsdaBytes::RdI16(&B[4]);
					Hp = AsdaBytes::RdI32(&B[6]);
					Mp = AsdaBytes::RdI16(&B[10]);
				}
				else if (Pkt.Opcode == (uint16)OP_NET_HANDLER_ENTER_WORLD && NetNeedLen(B, 35, (int32)Pkt.Opcode))
				{
					// 4055 (43 б, живые байты 2026-08-03): uidx@0, mapId u8@2, IP str@3..22,
					//   port u16@23, tileX i16@25, tileY i16@27, HP i32@29, MP i16@33, exp i64@35.
					NewMap = (int32)B[2];
					// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, N7): было `FString(ANSI_TO_TCHAR(IpBuf))` БЕЗ обрезки
					//   пробелов (единственная из 6 копий) — стало общая точка `AsdaBytes::AnsiFieldTrim` с Trim.
					//   Эталон netHandler_EnterWorld@0xF43050 (network.c:42639) копирует 22 б (IP+порт) как есть,
					//   а строка уходит в CNetwork_Connect@0x12AA840 → inet_addr(cp) (network.c:51871); явной обрезки
					//   в эталоне нет — точечный IP пробелов не содержит, а сервер добивает поле нулями, так что для
					//   живых данных результат тот же; обрезка лишь страхует наш Connect по FString от хвоста.
					PortIp = AsdaBytes::AnsiFieldTrim(B.data() + 3, 20);
					PortPort = (int32)AsdaBytes::RdU16(&B[23]);
					TX = AsdaBytes::RdI16(&B[25]);
					TZ = AsdaBytes::RdI16(&B[27]);
					Hp = AsdaBytes::RdI32(&B[29]);
					Mp = AsdaBytes::RdI16(&B[33]);
				}
				// ★ HP/MP от сервера. Формула сервера SetCurrentRegenHpValue@0x43C8A0: обычное воскрешение
				//   (a4 <= 0, включая город/на месте/уровень≤20/GM и код 39) = maxHP/2 = РОВНО 50 %;
				//   навык 189 = процент уровня навыка; коды 938/1014 = процент из скилл-таблицы.
				//   Живые байты: HP@29 = 1426 при максимуме 2853 = 49.98 % — сервер даёт ПОЛОВИНУ.
				if (Hp >= 0)
				{
					UE_LOG(LogAsdaNet, Warning, TEXT("[воскрешение] HP=%d MP=%d, источник=пакет %u (формула сервера: обычное=50%% max)"),
						Hp, Mp, Pkt.Opcode);
					ASDA_CTRACE(EAsdaCT::Recv, TEXT("ПРИЁМ %d своё HP/MP при прибытии: HP=%d MP=%d тайл=(%d,%d) карта=%d"), (int32)Pkt.Opcode, Hp, Mp, TX, TZ, NewMap);
					// ★ Единый источник истины: 4055/4056 — такой же серверный снапшот, как 4058.
					//   Максимумы пакет не несёт → сохраняем известные (char-list / последний 4058).
					SetVitalsHp((int32)SelfUserNum, Hp, -1, (Pkt.Opcode == (uint16)OP_NET_HANDLER_ENTER_WORLD) ? TEXT("4055") : TEXT("4056"), /*bServerSnapshot=*/true);
					if (Mp >= 0)
					{
						SetVitalsMp((int32)SelfUserNum, Mp, -1, (Pkt.Opcode == (uint16)OP_NET_HANDLER_ENTER_WORLD) ? TEXT("4055") : TEXT("4056"), /*bServerSnapshot=*/true);
					}
					OnSelfVitals.Broadcast(Hp, Mp);
				}
				if (TX >= 0 && TZ >= 0)
				{
					const bool bMapChange = (NewMap >= 0 && NewMap != EnterMapId);
					UE_LOG(LogAsdaNet, Warning,
						TEXT("[воскрешение] ответ сервера (%u) байты=[mapId=%d ip=%s:%d тайл=(%d,%d) HP=%d MP=%d] → %s"),
						Pkt.Opcode, NewMap, PortIp.IsEmpty() ? TEXT("-") : *PortIp, PortPort, TX, TZ, Hp, Mp,
						bMapChange ? TEXT("СМЕНА КАРТЫ (портал-хендофф)") : TEXT("телепорт в пределах карты"));
					SelfTileX = TX; SelfTileZ = TZ;
					if (bMapChange)
					{
						// ★★★ ПОРТАЛ-ХЕНДОФФ 1:1 (эталон game_main.c LoadProgress 0→3): сервер в 4055 даёт
						//   IP:port канала целевой карты и тайл респауна. Клиент ОБЯЗАН переподключиться и
						//   после загрузки уровня послать 5058 CharPortalLogonReq (НЕ обычный 4001!) — иначе
						//   сервер остаётся на СТАРОЙ карте: продолжает слать её мобов, а повторный 4001
						//   спавнит по старой позиции CUser (живой тест: мобы rein на alpen + спавн вне
						//   города (400,311) вместо (125,390)).
						EnterMapId = NewMap;
						bPortalPending = true;
						PortalTileX = TX; PortalTileZ = TZ;
						ApplyGameServerFromPacket(PortIp, PortPort, TEXT("4055"));   // ★ 16.10.2026: непубличный → домен, порт из пакета
						// ★★★ РЕКОННЕКТ ТОЛЬКО ОТЛОЖЕННО. Синхронный EnterGameWorld() отсюда = краш
						//   (2026-08-04): мы внутри колбэка пакета, а тот вызван из FAsdaConnection::
						//   DrainFrames(), который идёт по приёмному буферу. Disconnect+Connect сбрасывает
						//   RecvBuffer (ArrayNum=0), но сокет СНОВА не nullptr — старый гард не срабатывал,
						//   и финальный RemoveAt(0, 289) падал на массиве размера 0.
						//   Правильный порядок — тот же, что уже применён для смены канала 6015:
						//   поднять bPendingEnterWorld, попросить прекратить разбор, а сам реконнект
						//   выполнить в UAsdaNetSubsystem::Tick ПОСЛЕ выхода из Conn->Tick().
						bInGameConnected = false;
						bPendingEnterWorld = true;
						if (Conn) { Conn->RequestStopDrain(); }
						UE_LOG(LogAsdaNet, Warning,
							TEXT("[портал] запрошен реконнект (отложен до конца тика): %s:%d, карта=%d, после загрузки → 5058 тайл=(%d,%d)"),
							*GameServerIP, GameServerPort, NewMap, TX, TZ);
					}
					else
					{
						OnSelfWarp.Broadcast(-1, TX, TZ);
					}
				}
			}
		}
		break;
	}
	case OP_RECV_PACKET_LEVEL_UP:   // 4036 0xFC4 — level-up (эталон Recv_LevelUp@0xF424C0):
	{                               //   userIdx i16@0, level u8@2, exp i64@+3, скилл-очки i16@+11, статы @+13
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 11, (int32)Pkt.Opcode)) { break; }   // N8 (дедуп №2, В2-A, 11.10.2026): была молчаливая проверка — теперь с Warning-строкой `NetNeedLen`
		const uint16 Uidx = (uint16)AsdaBytes::RdU16(&B[0]);
		const uint8  Lvl  = B[2];
		int64 E = 0; FMemory::Memcpy(&E, B.data() + 3, 8);
		if (IsSelfUserIdx((int32)Uidx))
		{
			SelfLevel = Lvl;
			if (E >= 0) { SelfExp = E; }
			UE_LOG(LogAsdaNet, Log, TEXT("4036: LEVEL UP self → %d (exp=%lld)"), (int32)Lvl, (long long)E);
			ASDA_CTRACE(EAsdaCT::Reward, TEXT("ПРИЁМ 4036 НОВЫЙ УРОВЕНЬ (Я): уровень=%d опыт=%lld"), (int32)Lvl, (long long)E);
			// ★ 19.10.2026 (ревизия P75): свободные очки навыков из тела +11 (i16) — эталон в ветке своего
			//   персонажа зовёт `skillUiUpdatePointText(g_pSkillWnd, *(i16*)(a1+11))` (network.c, Recv_LevelUp@0xF424C0).
			//   Порт поле не читал: счётчик окна навыков стоял до следующего пакета с очками (на каждом уровне,
			//   а после зелья 558 — на всех 58 уровнях сразу).
			if (B.size() >= 13)
			{
				SelfSkillPoints = (int32)AsdaBytes::RdI16(&B[11]);
				UE_LOG(LogAsdaNet, Log, TEXT("4036: свободные очки навыков → %d"), SelfSkillPoints);
				OnSelfSkillPoints.Broadcast(SelfSkillPoints);
			}
			OnSelfLevelUp.Broadcast();
			OnSelfExp.Broadcast((int32)SelfLevel, SelfExp, /*gained=*/0, /*bFullExp=*/false, /*srcRid=*/-1);
		}
		else
		{
			// Чужой level-up: эталон играет S_c_levelup + эффект lvup01 у актора — фаза мировых эффектов.
			UE_LOG(LogAsdaNet, Log, TEXT("4036: level-up соседа userIdx=%u → %d"), Uidx, (int32)Lvl);
			ASDA_CTRACE_UID((int32)Uidx, EAsdaCT::Recv, TEXT("ПРИЁМ 4036 новый уровень соседа uidx=%d → %d"), (int32)Uidx, (int32)Lvl);
			// ★ Эталон тут же пишет уровень в `CCharacter+1082` соседа — у нас это реестр
			//   `NetPlayers` мир-подсистемы, её и уведомляем (иначе панель цели покажет старый уровень).
			OnRemoteLevel.Broadcast((int32)Uidx, (int32)Lvl);
		}
		// ★★★ БОЕВОЙ БЛОК ИЗ ТЕЛА 4036 — ЕДИНСТВЕННЫЙ канал, дающий статы ЧУЖОГО персонажа.
		//   Эталон `Recv_LevelUp@0xF424C0` (game.c:554533-554534) делает ДВА memcpy в найденный по
		//   userIdx `CCharacter`:  `+1192 ← тело+13` (0x24 — базовые очки) и `+1124 ← тело+49`
		//   (0x44 — боевой блок). Тело 117 б = 49 + 68 ровно (`GameServer.c:79611`).
		//   Сервер шлёт этот же пакет соседям по сектору (`sub_52E6D0` =
		//   `SendPacket_ClippingSectorInOtherUser`, `GameServer.c:79612`), поэтому ветка работает
		//   и для чужого userIdx — гейта «только свой» у эталона тут нет.
		if (NetNeedLen(B, 117, (int32)Pkt.Opcode))
		{
			SetCombatStatsFromBlock((int32)Uidx, B.data() + 49, (int32)B.size() - 49, TEXT("4036"));
			// HP/MP из того же блока: level-up у оригинала восстанавливает их до нового максимума,
			//   и это тот же серверный снимок, что 4060 (смещения внутри блока идентичны).
			const int32 MaxHp = AsdaBytes::RdI32(&B[49]);
			const int32 MaxMp = AsdaBytes::RdI16(&B[53]);
			const int32 CurHp = AsdaBytes::RdI32(&B[55]);
			const int32 CurMp = AsdaBytes::RdI16(&B[59]);
			ASDA_CTRACE_UID((int32)Uidx, EAsdaCT::Recv, TEXT("ПРИЁМ 4036 стат-блок uidx=%d: HP %d/%d MP %d/%d (восстановлено до максимума)"), (int32)Uidx, CurHp, MaxHp, CurMp, MaxMp);
			SetVitalsHp((int32)Uidx, CurHp, MaxHp, TEXT("4036 стат-блок"), /*bServerSnapshot=*/true);
			SetVitalsMp((int32)Uidx, CurMp, MaxMp, TEXT("4036 стат-блок"), /*bServerSnapshot=*/true);
			// ★ Для СВОЕГО персонажа 4036 несёт ровно те же два блока, что 4060 и 4064
			//   (`Recv_LevelUp@0xF424C0`: `+1192 ← тело+13` 0x24 и `+1124 ← тело+49` 0x44).
			//   Окно характеристик обязано обновиться на level-up — иначе после уровня в нём
			//   висят старые числа до следующего 4060.
			if (IsSelfUserIdx((int32)Uidx))
			{
				SetSelfBaseStatsFrom36(B.data() + 13, (int32)B.size() - 13, TEXT("4036"));
				SetSelfStatBlockFrom68(B.data() + 49, (int32)B.size() - 49, TEXT("4036"));
			}
		}
		break;
	}
	case OP_NET_HANDLE_HP_EXP_UPDATE:   // 4038 0xFC6 — НАЧИСЛЕНИЕ ОПЫТА
	{
		// ★★★ ПОЛНАЯ раскладка тела (19 б). Подтверждена ДВУМЯ независимыми способами:
		//   • сервер `Recv_BattleServerFromCharParamExp_1@0x4CBA90` (GameServer.c:80904) собирает
		//     тело из `{bool a5; __int64 newExp; __int64 expGain; __int16 a3;}` = 1+8+8+2 = 19 и
		//     шлёт `sub_4C28F0(…, 4038, 19, …)`;
		//   • живые тела из лога порта (см. `Reports/exp_gain_ui.md` §4.2).
		//     off +0 u8  flag  — серверный `a5`: 1 = НАГРАДНОЙ («полный») опыт, 0 = обычный
		//     off +1 i64 exp   — новый суммарный опыт персонажа
		//     off +9 i64 gain  — ПРИБАВКА (было прочитано как i32 — большие награды обрезались)
		//     off +17 i16 a3   — −1 = надпись над собой, ≥0 = renderId сущности (убитый моб)
		//   Эталон `netHandleHpExpUpdate@0xF427A0` ветвится ровно по (flag, a3) — см. отчёт §1.2.
		//   Показ (всплывающая надпись + строки чата) делает HUD-подсистема: сеть только разбирает.
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 19, (int32)Pkt.Opcode)) { break; }
		const uint8 Flag = B[0];
		int64 E = 0;    FMemory::Memcpy(&E, B.data() + 1, 8);
		int64 Gain = 0; FMemory::Memcpy(&Gain, B.data() + 9, 8);
		const int32 SrcRid = (int32)(int16)AsdaBytes::RdU16(&B[17]);
		if (E >= 0) { SelfExp = E; }   // эталон пишет exp в персонажа БЕЗУСЛОВНО, до всякого показа
		UE_LOG(LogAsdaNet, Log, TEXT("4038: опыт %lld (+%lld), flag=%d (%s), источник rid=%d (%s)"),
			(long long)E, (long long)Gain, (int32)Flag,
			(Flag == 1) ? TEXT("наградной") : TEXT("обычный"),
			SrcRid, (SrcRid < 0) ? TEXT("над собой") : TEXT("над сущностью"));
		ASDA_CTRACE(EAsdaCT::Reward, TEXT("ПРИЁМ 4038 опыт: +%lld → всего %lld, %s, источник %s rid=%d"),
			(long long)Gain, (long long)E, (Flag == 1) ? TEXT("наградной") : TEXT("обычный"),
			(SrcRid < 0) ? TEXT("над собой") : TEXT("убитый моб"), SrcRid);
		// ★ 17.10.2026 (синхронизация боя, Ф1) [ПОРТ]: опыт за убийство моба (надпись над ним и строки чата) — вместе с показом
		//   его СМЕРТИ. Сервер шлёт 4038 сразу, а смерть на экране лента попаданий держит до смертельного попадания (до 4.5 с):
		//   надпись над живым мобом. Значение опыта (`SelfExp`) уже записано выше — держится только показ. Ключ отката —
		//   `asda.Combat.HitFrameAllChannels 0` (лента выключена — держать нечем).
		{
			const int32 Lv = (int32)SelfLevel; const int64 Ex = SelfExp; const bool bFull = (Flag == 1);   // снимок аргументов показа
			TWeakObjectPtr<UAsdaNetSubsystem> WeakNet(this);                                           // выпуск позже — подсистема могла уйти
			UAsdaWorldSubsystem* WSx = (SrcRid >= 0 && GetGameInstance()) ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
			if (WSx && WSx->HoldUntilMobDeathShown(SrcRid, [WeakNet, Lv, Ex, Gain, bFull, SrcRid]()
				{
					if (UAsdaNetSubsystem* N = WeakNet.Get()) { N->OnSelfExp.Broadcast(Lv, Ex, Gain, bFull, SrcRid); }
				}, TEXT("опыт 4038"), /*bReleaseOnReset=*/true))   // ★ Ф2 (ревизия Ф1 п.10): при сбросе лент опыт не теряется
			{
				ASDA_CTRACE(EAsdaCT::Reward, TEXT("опыт 4038 +%lld ПРИДЕРЖАН до показа смерти моба rid=%d (лента попаданий)"), (long long)Gain, SrcRid);
				break;
			}
		}
		OnSelfExp.Broadcast((int32)SelfLevel, SelfExp, Gain, Flag == 1, SrcRid);
		break;
	}
	// ═══════════════════ ★ ЧАТ: приём строк в окно CHAT_NEW (2500) ══════════════════════════════
	// Реверс форматов — 2026-08-05 по RE/etalon/network.c (см. шапку FAsdaChatLineIn).
	// ЗДЕСЬ ТОЛЬКО РАЗБОР И ФОРМАТИРОВАНИЕ. Фильтры оригинала (игнор-лист `strSetContains`,
	// мат-фильтр `Chat_MatchBannedWord` → loc 9005, анти-спам по «.com»/«.c0m») — ЭТАП 2/3:
	// им нужны CurseTable.BIN и набор игнора из меню игрока (CHAT_NEW_TABX 80000).
	default: bHandled = false; break;
	}
}
