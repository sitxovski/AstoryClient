// Copyright (c) 2026 @sitxovski (Discord)
// AsdaNetSubsystem_Gm.cpp — часть `AsdaNetSubsystem.cpp`: оператор/GM: признаки оператора, невидимость при входе, GM-пакеты 6005/6020/6036/6094, дев-команды.
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

// ★★★ ВТОРОЙ КАНАЛ ЦИФР УРОНА 4029/4027 — ОДИН разбор на оба опкода (закон дедупа общих точек).
//   Раскладка, потребитель эталона и доказательство со стороны сервера — в объявлении
//   `FAsdaCombatSideDamage` (`Public/AsdaNetSubsystem.h`). Здесь только чтение уже приходящих
//   байтов: отправка, формат, размер и порядок полей пакетов НЕ меняются.
//   ⚠ Обе очереди эталон наполняет ЦЕЛИКОМ, все четыре шага, без «обрыва на нуле»: `sub_F40670`
//     крутит `for (i = 0; i < 4; ++i)` безусловно. Поэтому правило «непустой префикс», введённое
//     для очереди уронов, здесь НЕ применяется — пустой шаг помечается признаком `bValid = false`
//     (сентинел `Raw == −1`), а не обрывает чтение.
void UAsdaNetSubsystem::ParseCombatSideDamage(const std::vector<uint8>& B, int32 OffI16, int32 OffI32,
	int32 Opcode, int32 AttackerUserIdx, int32 TargetRid)
{
	// Тело обязано вмещать обе четвёрки целиком: 8 б i16-очереди и 16 б i32-очереди.
	//   Живой сервер шлёт 4029 всегда 49 б и 4027 всегда 53 б, так что ветка «короче» —
	//   это чужой/битый пакет, а не законный вариант; молча додумывать нули нельзя.
	if (!NetNeedLen(B, OffI32 + 16, Opcode, TEXT("второй канал цифр урона"))) { return; }

	FAsdaCombatSideDamage S;
	S.Opcode          = Opcode;
	S.AttackerUserIdx = AttackerUserIdx;
	S.TargetRid       = TargetRid;

	FString Dump;
	for (int32 i = 0; i < 4; ++i)
	{
		FAsdaCombatSideHit& H = S.Steps[i];
		H.Raw   = AsdaBytes::RdI16(&B[OffI16 + 2 * i]);   // ЗНАКОВО: знак и есть флаг адресата
		H.Value = AsdaBytes::RdI32(&B[OffI32 + 4 * i]);
		// Эталон (`entity_char.c:26905`) сравнивает ровно с −1 и только потом смотрит знак.
		H.bValid      = (H.Raw != -1);
		H.bOnAttacker = H.bValid && (H.Raw < 0);
		// Младшие 15 бит — id навыка-источника (сервер: `a6 & 0x7FFF` как индекс строки навыка).
		H.SkillId     = H.bValid ? (H.Raw & 0x7FFF) : -1;
		if (H.bValid)
		{
			Dump += FString::Printf(TEXT("[%d] навык=%d знач=%d → %s  "),
				i, H.SkillId, H.Value, H.bOnAttacker ? TEXT("атакующий") : TEXT("цель"));
		}
	}
	if (!Dump.IsEmpty())
	{
		UE_LOG(LogAsdaNet, Log, TEXT("%d: второй канал цифр урона (@+%d/@+%d): %s"),
			Opcode, OffI16, OffI32, *Dump);
		// Зовётся из 4029/4027 по любому соседу: уровень 1 — свой удар, моб нашего боя или сосед — участник.
		ASDA_CTRACE_IF(IsSelfUserIdx(AttackerUserIdx) || AsdaCombatTrace::IsEngaged(TargetRid) || AsdaCombatTrace::IsUserRelevant(AttackerUserIdx),
			EAsdaCT::Recv, TEXT("ПРИЁМ %d второй канал цифр (навык-источник): атакующий uidx=%d цель rid=%d: %s"),
			Opcode, AttackerUserIdx, TargetRid, *Dump);
	}
	OnCombatSideDamage.Broadcast(S);
}

// ★ GM-команда «пересчитать рейтинги СЕЙЧАС» — пара к серверному патчу P25.
//   Конверт тот же, что у всех операторских команд оригинала: `SendSecured`
//   (тело = [0..7] анти-реплей секрет + [8..23] MD5(payload) + [24..] payload),
//   потому что сервер сверяет подпись с признаком a4 = 1 (`sub_523F00`).
//   Payload 8 байт: +0 что пересчитать, +1..7 резерв (нули).
bool UAsdaNetSubsystem::SendGmRatingRecalc6020(uint8 What)
{
	if (What > 3) { What = 0; }
	FAsdaPacketWriter W(8);
	W.U8(0, What);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_GM_RATING_RECALC, TEXT("SendGmRatingRecalc6020"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_GM_RATING_RECALC, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6020 GM-пересчёт рейтингов: что=%d (42 б) -> %s"),
		(int32)What, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★★★ [ОРИГИНАЛ] GM-блок учётки цели — опкод 6094. Раскладка и серверный поток — у объявления в заголовке и в
//   `RE/docs/client/logic/gm_block_6094.md`. [ПРОТОКОЛ] Тело байт в байт как у `net_sendCharFieldSyncOpcode6094@0xA13680`
//   (network.c:8206-8210: `LOWORD(pkt[0]) = свой userNum; HIWORD(pkt[0]) = userNum цели; pkt[1] = fieldValue`).
//   ★ Верхняя граница номера — `sub_5328F0` сервера (`< 0x226`): за ней `sub_5324A0` вернёт nullptr, UserIndex станет −1
//     и DBManager запишет «UPHackBlock Error» — такой пакет слать бессмысленно.
bool UAsdaNetSubsystem::SendGmBlock6094(int32 TargetUserNum, int32 Hours)
{
	static constexpr int32 kServerUserSlots = 0x226;   // `sub_5328F0`: допустимые userNum 0…549
	if (!bSelfUserNumKnown)
	{
		UE_LOG(LogAsdaNet, Warning, TEXT("→6094 GM-блок: свой userNum ещё не известен (4003 не было) — не отправлен"));
		return false;
	}
	if (TargetUserNum < 0 || TargetUserNum >= kServerUserSlots)
	{
		UE_LOG(LogAsdaNet, Warning, TEXT("→6094 GM-блок: userNum цели %d вне 0…%d — не отправлен"), TargetUserNum, kServerUserSlots - 1);
		return false;
	}
	if (IsSelfUserIdx(TargetUserNum))
	{
		UE_LOG(LogAsdaNet, Warning, TEXT("→6094 GM-блок: цель = собственный персонаж (userNum %d) — не отправлен"), TargetUserNum);
		return false;
	}
	if (Hours <= 0)
	{
		UE_LOG(LogAsdaNet, Warning, TEXT("→6094 GM-блок: срок %d ч ≤ 0 — не отправлен (бессрочный блок −1 — только SQL владельца)"), Hours);
		return false;
	}
	FAsdaPacketWriter W(8);
	W.U16(0, SelfUserNum);               // [0..1] свой userNum — сервер не читает
	W.U16(2, (uint32)TargetUserNum);     // [2..3] userNum цели → `sub_5324F0` → UserIndex учётки
	W.I32(4, Hours);                     // [4..7] срок (часы) → `EXEC UPHackBlock UserIndex, Hours`
	if (AsdaPacketLayoutBroken(W, (uint16)OP_NET_SEND_CHAR_FIELD_SYNC_OPCODE6094, TEXT("SendGmBlock6094"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_NET_SEND_CHAR_FIELD_SYNC_OPCODE6094, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6094 GM-блок учётки: цель userNum=%d срок=%d ч (тело 32 б) -> %s "
		"(ответа сервер не шлёт; след — GameServer\\Log\\Log_SpeedHack «GM Recv_CharBlock()», DBManager «UPHackBlock»)"),
		TargetUserNum, Hours, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══ ★★★ [ОРИГИНАЛ] СЕРВЕР ПРЯЧЕТ ОПЕРАТОРА САМ — ПРИ ВХОДЕ В МИР И ПРИ КАЖДОМ ПОРТАЛЕ ═══════
//   Полный разбор и перечень всех 17 ссылок на поле `CUser+0x3BC3B` — у объявления
//   `IsSelfOperatorAccount` в заголовке. Здесь — только формула и ключ отката.
//   Серверный гейт (`sub_52ECF0@0x52ECF0`, GameServer.c:115112) дословно:
//       if (a3 <= 0) { if (sub_5324F0(this,a2) >= 0 && sub_5324F0(this,a2) <= 5000) return 1; }
//   `sub_5324F0` = `*(DWORD*)(CUser+0x16B)` = UserIndex учётки — ровно то число, что сервер печатает
//   в `Log_Operator\OperatorLog_*.txt` вторым полем и что лежит в `dbo.Charinfo.UserIndex`.
//   У нас это `UserIndex` из ответа логина 1004 — приходит ЗАДОЛГО до входа в мир и сбросом сессии
//   не стирается (полный сброс чистит игровое состояние, но не реквизиты учётки).
static ASDA_DEV_CVAR(int32, CVarAsdaGmHideAutoOnEnter,
	TEXT("asda.Gm.HideAutoOnEnter"), 1,
	TEXT("1 = воспроизводить сервер: при входе в мир и при портале оператор (UserIndex 0..5000) "
	     "считается СКРЫТЫМ (CharMake/CharPortalMake пишут 300 безусловно, клиенту не сообщая); "
	     "0 = состояние вести только по эху 6006."),
	ECVF_Default);

// Верхняя граница операторского гейта сервера — литерал 5000 из `sub_52ECF0`.
static constexpr uint32 kAsdaOperatorUserIndexMax = 5000;

bool UAsdaNetSubsystem::IsSelfOperatorAccount() const
{
	// ★★★ 0 = «ответа логина 1004 ещё не было», а НЕ учётка №0. Различать обязательно: поле
	//   инициализировано нулём, и без этой проверки любой клиент, дошедший до 4003 в обход логина
	//   (диагностические прогоны, ручной коннект к игровому серверу), считал бы себя оператором и
	//   прятался при входе. У сервера такой двусмысленности нет: `sub_5324F0` возвращает -1, если
	//   слота нет, а у живого слота там настоящий UserIndex.
	//   [ПРОВЕРЕНО НА ЖИВОЙ БАЗЕ 10.09.2026] `Joy1.dbo.Charinfo`: MIN(UserIndex)=1, MAX=2, всего 5
	//   персонажей — учётки с индексом 0 не существует, и весь диапазон гейта 0…5000 занимают
	//   ровно две операторские учётки (1 = GM_sitxovski/sitx, 2 = GM_Rimy/ADM_sitxovski/sitxovskiTest).
	//   То есть на этом сервере правило «UserIndex ≤ 5000 ⇒ оператор» никого постороннего не задевает.
	if (UserIndex == 0) { return false; }
	// ★★★ ГРАНИЦА ДИАПАЗОНА — 5000 ВКЛЮЧИТЕЛЬНО, и это проверено по дизасму, а не по Hex-Rays:
	//   `sub_52ECF0` (GameServer.exe.asm:416835-416887) в обеих ветках делает
	//       cmp eax, 1388h / jg <отказ>
	//   то есть отказ только при idx > 5000; сам 5000 гейт ПРОХОДИТ (диапазон 0…5000, 5001 значение).
	//   Владелец формулировал правило как «всё, что до 5000». Разницу в один индекс не замалчиваю:
	//   выбран СЕРВЕРНЫЙ вариант (включительно), потому что клиент здесь не назначает права, а
	//   ПРЕДСКАЗЫВАЕТ, что уже сделал сервер, — при `< 5000` учётка ровно с индексом 5000 была бы
	//   спрятана сервером и показана нашим клиентом (расхождение картинки с реальностью).
	//   На живой базе спор чисто теоретический: `Joy1.dbo.Charinfo` MIN=1, MAX=2 (10.09.2026).
	// Серверное сравнение знаковое (`>= 0 && <= 5000`); у нас поле беззнаковое, отрицательных нет.
	return UserIndex <= kAsdaOperatorUserIndexMax;
}

// ═══ ★ [ПОРТ] ВТОРОЙ КРИТЕРИЙ ОПЕРАТОРА — ПРЕФИКС В ИМЕНИ ПЕРСОНАЖА ═══════════════════════════
//   Полное обоснование и выкладка живой базы — у объявлений в заголовке. Коротко: сервер смотрит
//   ТОЛЬКО индекс, но у логинов учёток (`Us_member.Us_id` = «sitxovski», «Rimy») префикса нет ни у
//   одного, поэтому сравнивать надо с ИМЕНЕМ ПЕРСОНАЖА (`Joy1.dbo.Charinfo.Charname`).
//
//   ★ Почему список префиксов НАСТРАИВАЕМЫЙ и почему умолчание именно `GM_,ADM_`: оба префикса
//   реально встречаются на живой базе среди персонажей операторских учёток («GM_sitxovski»,
//   «GM_Rimy» у индекса 1-2 и «ADM_sitxovski» у индекса 2), а стоковый сервер к именам не привязан
//   вовсе — значит соглашение об именовании принадлежит АДМИНИСТРАТОРУ сервера и может меняться
//   без пересборки клиента. Отсюда ini + консольный ключ, а не литерал в коде.
ASDA_DEV_CVAR(FString, CVarAsdaGmOperatorNamePrefixes,
	TEXT("asda.Gm.OperatorNamePrefixes"),
	TEXT("GM_,ADM_"),
	TEXT("Префиксы ИМЕНИ ПЕРСОНАЖА, по которым клиент считает себя оператором (через запятую, "
	     "регистр не важен). Умолчание GM_,ADM_. Правится без пересборки: этот ключ или секция "
	     "[AsdaGm] OperatorNamePrefixes= в Config/DefaultGame.ini."),
	ECVF_Default);

// ★ КЛЮЧ ОТКАТА критерия по имени: 0 = вести себя ровно как сервер (только индекс 0…5000).
static ASDA_DEV_CVAR(int32, CVarAsdaGmOperatorByCharName,
	TEXT("asda.Gm.OperatorByCharName"), 1,
	TEXT("1 = оператор = индекс учётки в диапазоне прав ИЛИ префикс имени персонажа (надстройка "
	     "порта); 0 = ОТКАТ к поведению сервера: только UserIndex 0..5000."),
	ECVF_Default);

FString UAsdaNetSubsystem::GetSelfCharNameForOperatorCheck() const
{
	// 1) ОБЩАЯ ТОЧКА «своё имя» — ею же пользуется вход в GM-меню (`AsdaAdminSubsystem.cpp:423`).
	//    Второго склада имени не заводим (закон `port-shared-points`).
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>())
		{
			const FString& FromWorld = WS->GetSelfCharName();
			if (!FromWorld.IsEmpty()) { return FromWorld; }
		}
	}
	// 2) ★★★ ОБЩАЯ ТОЧКА ЗАПАЗДЫВАЕТ ОТНОСИТЕЛЬНО 4003 — ДОКАЗАНО ЖИВЫМ ЛОГОМ, а не рассуждением.
	//    `Saved/Logs/AsdaReborn_4.log` (сессия 03.09.2026, учётка Rimy, персонаж ADM_sitxovski):
	//        23:44:44.218  Login OK (… UserIndex=2 …)                         ← ответ 1004
	//        23:44:45.485  char-list 0x3EE: слоты GM_Rimy/ADM_sitxovski/…     ← ИМЕНА УЖЕ ЕСТЬ
	//        23:47:01.850  SpawnSelf 4003: userNum=0 tile=(108,183)           ← наш обработчик
	//        23:47:01.935  Self заспавнен … плашка 'ADM_sitxovski'            ← ТОЛЬКО ЗДЕСЬ мир
	//                                                                           ставит SelfCharName
	//    То есть `UAsdaWorldSubsystem::SelfCharName` садится на 85 мс ПОЗЖЕ обработки 4003 (он
	//    пишется внутри `TrySpawnLocalPlayer`, а её запускает уже сам `OnSelfSpawn`), тогда как
	//    запись char-list лежит в этой подсистеме за 2 мин 16 с ДО входа. Поэтому берём имя из
	//    ПЕРВОИСТОЧНИКА — той самой записи, из которой мир его потом и копирует
	//    (`AsdaWorldSubsystem.cpp`: `SelfCharName = R.Name;` по тому же `LastSelectedRawSlot`).
	//    ★★★ ВТОРАЯ, ЕЩЁ БОЛЕЕ ВАЖНАЯ ПРИЧИНА — СМЕНА КАРТЫ (портал и смена канала).
	//    `UAsdaWorldSubsystem::ResetSessionState` делает `SelfCharName.Empty()` БЕЗУСЛОВНО, то
	//    есть и по причине `MapChanged` тоже, а рассылает её `UAsdaSessionResetSubsystem` из
	//    `PostLoadMapWithWorld` (`AsdaSessionReset.cpp`, ветка «травел с игровой карты»). Значит
	//    после КАЖДОГО портала общая точка снова пуста, и если бы критерий смотрел только на неё,
	//    он молчал бы не только на первом входе, но и при любой смене карты — ровно там, где
	//    сервер (`CharPortalMake`) заново записывает 300.
	//    ⚠ Сброс сессии запись char-list НЕ трогает: `UAsdaNetSubsystem::ResetSessionState` чистит
	//    игровые реестры и зеркала, а `CharList`, `LastSelectedRawSlot` и `UserIndex` — реквизиты
	//    учётки и выбора персонажа, они переживают и портал, и разрыв (закон
	//    `session-reset-drops-early-data`: фильтруем, а не чистим).
	// ★ 30.10.2026 [ИСПРАВЛЕНИЕ] (`asda.Possess.BodyIdentity`): запись — общей точкой `AsdaSelf::FindSelectedCharRecord`. При одержимости
	//   своя запись с тем же номером слота — ДРУГОЙ персонаж; имя тела — блок 1017 +4 (20 б cp1251, char+1056 оригинала).
	if (const FAsdaCharRecord* R = AsdaSelf::FindSelectedCharRecord(this)) { return R->Name; }
	if (SelfSoulState == 2 && CharSelectData.Num() >= 4 + 20) { return FAsdaLoginData::DecodeCp1251(CharSelectData.GetData() + 4, 20); }
	return FString();
}

bool UAsdaNetSubsystem::IsSelfOperatorByCharName() const
{
	if (CVarAsdaGmOperatorByCharName.GetValueOnGameThread() == 0) { return false; }
	const FString Name = GetSelfCharNameForOperatorCheck();
	if (Name.IsEmpty()) { return false; }
	TArray<FString> Prefixes;
	CVarAsdaGmOperatorNamePrefixes.GetValueOnGameThread().ParseIntoArray(Prefixes, TEXT(","), true);
	for (const FString& Raw : Prefixes)
	{
		const FString Prefix = Raw.TrimStartAndEnd();
		// Регистр не сверяем: имена персонажей на живой базе смешанные, а соглашение о префиксе —
		//   человеческое, не протокольное.
		if (!Prefix.IsEmpty() && Name.StartsWith(Prefix, ESearchCase::IgnoreCase)) { return true; }
	}
	return false;
}

bool UAsdaNetSubsystem::IsSelfOperator() const
{
	// ★ ИЛИ, а не И: каждое слагаемое закрывает свой промах, разбор — у объявления в заголовке.
	return IsSelfOperatorAccount() || IsSelfOperatorByCharName();
}

// ★★★ [ПОРТ] 18.10.2026 (решение владельца): скрытие оператора не включается само при входе/портале, а держит
//   ПОСЛЕДНЕЕ выбранное состояние. Оригинал запоминать нечем: сервер ставит 300 при каждом CharMake/CharPortalMake,
//   своего признака у клиента нет (отправитель 6005 `sub_9F7AE0` в рознице не вызывается).
static ASDA_DEV_CVAR(int32, CVarAsdaGmHideRestoreLast,
	TEXT("asda.Gm.HideRestoreLast"), 1,
	TEXT("1 = после входа/портала вернуть последнее состояние GM-скрытия (запомнено по эху 6006; без записи — ВИДЕН): ")
	TEXT("сервер прячет оператора сам, поэтому для «виден» клиент шлёт 6005(0); 0 = как сервер (всегда скрыт после появления)."),
	ECVF_Default);

int32 UAsdaNetSubsystem::LoadSavedGmHidden() const
{
	if (SelectedCharId == 0 || !GConfig) { return -1; }
	bool bHidden = false;
	if (!GConfig->GetBool(TEXT("AsdaGm"), *FString::Printf(TEXT("Hidden.%d"), SelectedCharId), bHidden, GGameUserSettingsIni)) { return -1; }
	return bHidden ? 1 : 0;
}

void UAsdaNetSubsystem::SaveGmHidden(bool bHidden) const
{
	// ★ Ревизия 18.10 (ВАЖНО): только живая игровая сессия — стенд/самотест подают 6006 с выдуманным charId (1 = персонаж
	//   владельца) и записали бы его состояние в настоящий GameUserSettings.
	if (SelectedCharId == 0 || !GConfig || !bInGameConnected) { return; }
	GConfig->SetBool(TEXT("AsdaGm"), *FString::Printf(TEXT("Hidden.%d"), SelectedCharId), bHidden, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void UAsdaNetSubsystem::TickGmRestoreVisible(float DeltaTime)
{
	if (!bGmRestoreVisiblePending) { return; }
	if (!bGmHidden) { bGmRestoreVisiblePending = false; return; }   // уже видимы (эхо 6006 пришло)
	GmRestoreRetrySec -= DeltaTime;
	if (GmRestoreRetrySec > 0.f) { return; }
	if (bTransferInProgress || !bInGameConnected || GmHideLatch.IsArmed()) { GmRestoreRetrySec = 0.25f; return; }
	// ★ Ревизия 18.10 (ВАЖНО): сервер отказывает 6005, пока персонаж идёт (`Send_HideCommand` → `sub_523ED0`); после
	//   портала автопуть сразу ведёт персонажа — ждём остановки, попытку не тратим.
	if (const UWorld* Wd = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		if (const AAsdaPlayerController* PCm = Cast<AAsdaPlayerController>(Wd->GetFirstPlayerController()))
		{
			if (PCm->IsMovingNow()) { GmRestoreRetrySec = 0.25f; return; }
		}
	}
	if (GmRestoreAttempts >= 10)
	{
		bGmRestoreVisiblePending = false;
		UE_LOG(LogAsdaNet, Warning, TEXT("[GM-скрытие] восстановить «виден» не удалось за 10 попыток — остаёмся СКРЫТЫ"));
		return;
	}
	++GmRestoreAttempts;
	GmRestoreRetrySec = 1.0f;
	bGmRestoreSending = true;
	const bool bSent = SendGmHide6005(false);
	bGmRestoreSending = false;
	if (bSent)
	{
		bGmRestoreVisiblePending = false;
		bGmRestoreInFlight = true;
		UE_LOG(LogAsdaNet, Log, TEXT("[GM-скрытие] восстановлено последнее состояние: → 6005 ПОКАЗАТЬ (попытка %d)"), GmRestoreAttempts);
	}
}

// ★★★ [ОРИГИНАЛ+ПОРТ] ЕДИНАЯ ТОЧКА «сервер уже спрятал меня при входе/портале».
//   Зовётся из обработчика 4003 (единственный self-пакет ОБОИХ путей — `CharMake` и
//   `CharPortalMake`) СТРОГО ДО `OnSelfSpawn`, и второй раз — доопределением по имени, если в
//   момент 4003 имени персонажа ещё не было.
void UAsdaNetSubsystem::ApplyGmHideOnEntry(const TCHAR* Why)
{
	bGmHideEntryNamePending = false;

	if (CVarAsdaGmHideAutoOnEnter.GetValueOnGameThread() == 0)
	{
		if (bGmHidden)
		{
			// Ключ выключен, а признак висел с прошлой карты — честно говорим, что больше его
			//   не ведём: серверное состояние на новой карте нам неизвестно.
			UE_LOG(LogAsdaNet, Warning,
				TEXT("[GM-скрытие] asda.Gm.HideAutoOnEnter 0 (%s): серверное состояние неизвестно, ")
				TEXT("наш признак СКРЫТ оставлен как был (сервер, вероятно, поставил 300 сам)"), Why);
		}
		return;
	}

	const bool    bByIndex = IsSelfOperatorAccount();
	const FString Name     = GetSelfCharNameForOperatorCheck();
	const bool    bByName  = IsSelfOperatorByCharName();

	if (!bByIndex && !bByName)
	{
		// ★ ДООПРЕДЕЛЕНИЕ, а не отказ: пустое имя означает «критерий по имени НЕ ПРОВЕРЕН».
		//   Молча считать это отрицательным ответом нельзя (закон `false-negatives-method`).
		if (Name.IsEmpty() && CVarAsdaGmOperatorByCharName.GetValueOnGameThread() != 0)
		{
			bGmHideEntryNamePending = true;
			UE_LOG(LogAsdaNet, Warning,
				TEXT("[GM-скрытие] %s: индекс UserIndex=%u гейт 0..%u НЕ проходит, а имя персонажа ещё ")
				TEXT("НЕ известно — критерий по имени будет пересчитан, как только имя придёт ")
				TEXT("(не позже создания павна в TrySpawnLocalPlayer)"),
				Why, UserIndex, (uint32)kAsdaOperatorUserIndexMax);
		}
		else
		{
			UE_LOG(LogAsdaNet, Log,
				TEXT("[GM-скрытие] %s: НЕ оператор (UserIndex=%u вне 0..%u, имя «%s» без операторского ")
				TEXT("префикса) — авто-скрытие не взводим"),
				Why, UserIndex, (uint32)kAsdaOperatorUserIndexMax, *Name);
		}
		return;
	}

	if (!bByIndex)
	{
		// ⚠ ЧЕСТНОЕ ПРЕДУПРЕЖДЕНИЕ: сработала ТОЛЬКО наша надстройка. Сервер имени не смотрит —
		//   значит он, скорее всего, НЕ прятал этого персонажа, и наша прозрачность разойдётся с
		//   тем, что видят остальные. Откат — `asda.Gm.OperatorByCharName 0`.
		UE_LOG(LogAsdaNet, Warning,
			TEXT("[GM-скрытие] %s: сработал ТОЛЬКО критерий по имени («%s»), а серверный гейт по ")
			TEXT("индексу (UserIndex=%u, диапазон 0..%u) НЕ пройден. Сервер имени не читает — он ")
			TEXT("этого персонажа, вероятно, НЕ прятал. Откат: asda.Gm.OperatorByCharName 0"),
			Why, *Name, UserIndex, (uint32)kAsdaOperatorUserIndexMax);
	}

	if (!bGmHidden)
	{
		bGmHidden = true;
		UE_LOG(LogAsdaNet, Log,
			TEXT("[GM-скрытие] %s: считаем себя СКРЫТЫМИ. Критерии: индекс %s (UserIndex=%u, гейт ")
			TEXT("0..%u) | имя «%s» %s. Сервер (CharMake/CharPortalMake) уже записал 300"),
			Why, bByIndex ? TEXT("ПРОЙДЕН") : TEXT("не пройден"), UserIndex,
			(uint32)kAsdaOperatorUserIndexMax, *Name,
			bByName ? TEXT("ПОДХОДИТ под операторский префикс") : TEXT("под префиксы не подходит"));
	}
	else
	{
		UE_LOG(LogAsdaNet, Log,
			TEXT("[GM-скрытие] %s: уже СКРЫТЫ, состояние подтверждено повторно (индекс %s, имя «%s» %s)"),
			Why, bByIndex ? TEXT("ПРОЙДЕН") : TEXT("не пройден"), *Name,
			bByName ? TEXT("ПОДХОДИТ") : TEXT("не подходит"));
	}
	GmHideLatch.Disarm();
	bGmHiddenFromEntry = true;   // подпись меню: состояние ВЫЧИСЛЕНО, а не подтверждено 6006
	OnGmHideResult.Broadcast(/*bAccepted*/true, /*bHidden*/true);

	// ★★★ [ПОРТ] 18.10.2026: последнее состояние. Только при пройденном СЕРВЕРНОМ гейте (по индексу) — иначе сервер не
	//   прятал и 6005 он отвергнет. Без записи — «виден» (владелец: флаг сам включаться не должен).
	bGmRestoreVisiblePending = false;
	bGmRestoreInFlight = false;
	if (bByIndex && CVarAsdaGmHideRestoreLast.GetValueOnGameThread() != 0)
	{
		const int32 Saved = LoadSavedGmHidden();
		if (Saved != 1)
		{
			bGmRestoreVisiblePending = true;
			GmRestoreRetrySec = 0.f;
			GmRestoreAttempts = 0;
		}
		UE_LOG(LogAsdaNet, Log, TEXT("[GM-скрытие] %s: запомнено %s (charId %d) → %s"), Why,
			Saved == 1 ? TEXT("СКРЫТ") : (Saved == 0 ? TEXT("ВИДЕН") : TEXT("нет записи")), SelectedCharId,
			Saved == 1 ? TEXT("остаёмся скрытыми") : TEXT("после входа шлём 6005 ПОКАЗАТЬ"));
	}
}

void UAsdaNetSubsystem::ReevaluateGmHideOnEntryIfNamePending()
{
	if (!bGmHideEntryNamePending) { return; }
	const FString Name = GetSelfCharNameForOperatorCheck();
	if (Name.IsEmpty())
	{
		UE_LOG(LogAsdaNet, Warning,
			TEXT("[GM-скрытие] доопределение: имя персонажа так и не стало известно — критерий по ")
			TEXT("имени остаётся непроверенным, ведём состояние только по эху 6006"));
		bGmHideEntryNamePending = false;
		return;
	}
	UE_LOG(LogAsdaNet, Log,
		TEXT("[GM-скрытие] доопределение: имя персонажа стало известно («%s») — пересчитываем критерий"), *Name);
	ApplyGmHideOnEntry(TEXT("доопределение по имени"));
}

// ★★★ [ОРИГИНАЛ] GM-«скрыться» — опкод 6005, `Send_HideCommand@0x54E970` (GameServer.c:128161).
//   [ПРОТОКОЛ] Шлём РОВНО то, что слал оригинальный клиент (`sub_9F7AE0`, снят дизассемблированием
//   `client\system\AsdaStory.exe` — в декомпиле этой функции НЕТ, там дыра 0x9F7A70…0x9F80BF):
//       memcpy(body+0 , secret, 8);            // анти-реплей токен (0x57A7A5 + счётчик, 30 бит-позиций)
//       memcpy(body+8 , md5   , 16);           // Net_Md5Digest16(g_NetMd5Salt, payload, 2, md5)
//       memcpy(body+24, payload, 2);           // payload[0] = 0, payload[1] = bHide
//       CNetwork_Send(field1C, body, 26, 0x1775, 1, 1, 1, 0, 0);
//   Это в точности наш `SendSecured` (тело 24 + Len, bPad = true — эталонный a6 = 1).
//   ★ Байт payload[0] сервер ПЕРЕТИРАЕТ сам (`v10[0] = 1` при успехе, `= 0` при отказе) и этим же
//   буфером отвечает опкодом 6006 — поэтому слать в нём что-либо осмысленное бесполезно.
//   ★ Состояние НЕ защёлкиваем: `bGmHidden` меняется только в обработчике 6006.
bool UAsdaNetSubsystem::SendGmHide6005(bool bHide)
{
	// ★ 18.10.2026 (ревизия, мелочь): ручной выбор GM (меню) главнее восстановления последнего состояния.
	if (!bGmRestoreSending) { bGmRestoreVisiblePending = false; bGmRestoreInFlight = false; }
	FAsdaPacketWriter W(2);
	W.U8(1, bHide ? 1 : 0);                            // @0 = 0, @1 = требуемое состояние
	if (AsdaPacketLayoutBroken(W, (uint16)OP_GM_HIDE_REQ, TEXT("SendGmHide6005"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_GM_HIDE_REQ, W.Data(), W.Num());
	if (bOk)
	{
		GmHideLatch.Arm((int32)OP_GM_HIDE_REQ);   // ★ S2 (В2-A): засов «6005 в полёте» — общая точка
	}
	UE_LOG(LogAsdaNet, Log, TEXT("→6005 GM-скрыться: требуем %s (тело 26 б) -> %s"),
		bHide ? TEXT("СКРЫТЬ") : TEXT("ПОКАЗАТЬ"), bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★ ac0r (12.10.2026, античит; ревизия А0/А2 СРЕД-2/СРЕД-3): ПРОБА ПРАВ ОПЕРАТОРА — опкод 6036 GetValue.
//   [ПРОТОКОЛ] Шлём РОВНО то, что слал отправитель оригинала `0xA0B550` (снят дизассемблированием IDB-сборки
//   `AsdaStory.exe`, вызовов у него в рознице нет — как у всех операторских отправителей):
//       body[0..7] = анти-реплей токен (0x57A7A5 + счётчик, 30 бит-позиций) — наш `FillAntiReplaySecret`;
//       CNetwork_Send(field1C, body, 8, 0x1794, 1, 1, 1, 0, 0);   // без MD5 и полезной нагрузки; a6 = 1 — пад
//   Сервер (`Recv_OperatorCommand_GetValue@0x551970`): гейт `sub_52ECF0` (под P47 ещё GM-флаг) → токен `sub_524390`
//   → только ЧТЕНИЕ полей CUser (уровень, опыт, параметр, предмет 20551, блоки характеристик) → 6037 на NpcServer
//   (`Process_PlayerInfoInBattle@0x457D80`, только чтение) → 6037 (135 б) ТОЛЬКО отправителю (`sub_551B60`).
//   Ничего не пишет и никому не рассылает — в отличие от 6005 (CUser+21700 = 300/301 + 4025 по карте).
//   Без прав ответа нет вовсе (ни у стока, ни под P47) — поэтому неотвеченная проба на сессию ОДНА.
bool UAsdaNetSubsystem::SendGmRightsProbe6036()
{
	if (bGmRightsProbeUnanswered)
	{
		UE_LOG(LogAsdaNet, Log, TEXT("→6036 проба прав: прошлая проба этой сессии без ответа — повторов нет до конца сессии"));
		return false;
	}
	const uint16 Op = (uint16)OP_GM_GET_VALUE_REQ;
	static constexpr bool bProbePad = true;   // эталонный a6 = 1
	bool bOk = false;
	if (IsStandCapture(Op))
	{
		const auto Cap = SendCaptureForStand;   // копия: перехватчик вправе снять себя изнутри (см. SendSecured)
		Cap(Op, nullptr, 0, bProbePad, TEXT('T'));
		bOk = true;
	}
	else
	{
		if (!Conn || !Conn->IsConnected()) { return false; }
		// Гейты — та же маска, что у 6005 (`SendSecured`): XyT → одержимость → «душа» → переход.
		if (FAsdaOutgoingGate::Block(this, Op, EAsdaGate::XyT | EAsdaGate::Possess | EAsdaGate::Soul | EAsdaGate::Transfer)) { return false; }
		uint8 Body[8];
		FillAntiReplaySecret(Body);   // [0..7] токен — всё тело
		bOk = TrackSend(ConnSendSecret(Op, Body, sizeof(Body), 0, 0, bProbePad));
	}
	if (bOk) { bGmRightsProbeUnanswered = true; }
	UE_LOG(LogAsdaNet, Log, TEXT("→6036 проба прав оператора (тело 8 б, только чтение на сервере) -> %s"), bOk ? TEXT("OK") : TEXT("FAIL"));
	return bOk;
}

// ★ ЖИВАЯ ПРОВЕРКА КРИТЕРИЯ «Я ОПЕРАТОР» БЕЗ СЕРВЕРА: `asda.Gm.WhoAmI`.
//   Ничего не меняет и никуда не шлёт — печатает ровно то, что вернут БОЕВЫЕ функции
//   (закон `diagnostic-must-use-real-path`: своей копии условия здесь нет).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaGmWhoAmI(
	TEXT("asda.Gm.WhoAmI"),
	TEXT("Показать операторский критерий: UserIndex из 1004, имя персонажа, оба слагаемых и итог"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
	{
		UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!Net) { UE_LOG(LogAsdaNet, Warning, TEXT("asda.Gm.WhoAmI: сетевой подсистемы нет")); return; }
		UE_LOG(LogAsdaNet, Warning,
			TEXT("[GM-критерий] UserIndex=%u (гейт сервера 0..5000 включительно: %s) | имя персонажа «%s» ")
			TEXT("(операторский префикс: %s; список «%s», ключ отката asda.Gm.OperatorByCharName) | ИТОГ: %s"),
			Net->GetUserIndexForDiagnostics(),
			Net->IsSelfOperatorAccount()   ? TEXT("ПРОЙДЕН") : TEXT("не пройден"),
			*Net->GetSelfCharNameForOperatorCheck(),
			Net->IsSelfOperatorByCharName() ? TEXT("ЕСТЬ")   : TEXT("нет"),
			*IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Gm.OperatorNamePrefixes"))->GetString(),
			Net->IsSelfOperator()           ? TEXT("ОПЕРАТОР") : TEXT("обычный игрок"));
	}));
#endif // ASDA_DEV_CONSOLE

// ★ ЖИВАЯ ПРОВЕРКА АВТО-СКРЫТИЯ ВХОДА БЕЗ СЕРВЕРА: `asda.Gm.TestEntryHide`.
//   Зовёт ТУ ЖЕ `ApplyGmHideOnEntry`, что и обработчик 4003, — то есть проходит боевой путь
//   целиком (критерий → признак → рассылка `OnGmHideResult` → прозрачность и гейты). Наружу
//   ничего не шлёт. ⚠ Диагностика: реальное состояние на сервере не меняется.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaGmTestEntryHide(
	TEXT("asda.Gm.TestEntryHide"),
	TEXT("Прогнать расчёт авто-скрытия входа (тот же код, что в обработчике 4003), без сервера"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		// ★ 11.10.2026 (В3 S1): `quit` и единый итог — для пакетного прогона (прежде команда не выходила: пакет стоял до
		//   таймаута и останавливался). Признак «я скрыт», взведённый расчётом офлайн (UserIndex 0 проходит гейт оператора),
		//   возвращается как был — иначе следующие самотесты шли бы «скрытым GM» (гейт атаки, прозрачность).
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!Net) { AsdaSelfTestKit::Refused(World, TEXT("asda.Gm.TestEntryHide"), KA.bQuit, TEXT("сетевой подсистемы нет")); return; }
		const bool bHiddenWas = Net->IsGmHidden();   // признак до диагностики
		Net->ApplyGmHideOnEntry(TEXT("ДИАГНОСТИКА asda.Gm.TestEntryHide"));
		const bool bHiddenNow = Net->IsGmHidden();   // результат расчёта
		// ★ 18.10.2026: восстановление последнего состояния («виден» → 6005) — только отчёт; диагностика наружу не шлёт.
		const bool bRestore = Net->IsGmRestoreVisiblePending();
		Net->CancelGmRestoreVisible();
		if (KA.bQuit && bHiddenNow != bHiddenWas) { Net->ForceGmHiddenForDiagnostics(bHiddenWas); }
		AsdaSelfTestKit::Finish(World, TEXT("asda.Gm.TestEntryHide"), KA.bQuit, LogAsdaNet, TEXT("[GM-скрытие] ДИАГНОСТИКА"), 1, 0,
			bHiddenNow ? (bRestore ? TEXT(" (расчёт: СКРЫТ, восстановление «виден» взведено)") : TEXT(" (расчёт: СКРЫТ, остаёмся скрытыми)"))
			           : TEXT(" (расчёт: не скрыт)"));
	}));
#endif // ASDA_DEV_CONSOLE
// ★ ЖИВАЯ ПРОВЕРКА БЕЗ СЕРВЕРА И БЕЗ ПАРОЛЯ GM-МЕНЮ: `asda.Gm.ForceHideState 1|0`.
//   Взводит РОВНО тот же подтверждённый признак, что и эхо 6006, и рассылает `OnGmHideResult` —
//   то есть проходит весь тракт: прозрачность тела и экипировки, притухание эффектов, гейт атаки
//   и навыков. Наружу НИЧЕГО не шлёт (закон `no-reentrant-net-callbacks` и «сервер не трогаем»).
//   ⚠ Это диагностика: реальное состояние на сервере команда не меняет, и первый же вход/портал
//   или ответ 6006 её перебьёт.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaGmForceHideState(
	TEXT("asda.Gm.ForceHideState"),
	TEXT("Проверка GM-скрытия без сервера: asda.Gm.ForceHideState 1|0 — взвести/снять признак «я скрыт»"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		const bool bOn = Args.Num() == 0 || Args[0] != TEXT("0");
		UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!Net) { UE_LOG(LogAsdaNet, Warning, TEXT("asda.Gm.ForceHideState: сетевой подсистемы нет")); return; }
		Net->ForceGmHiddenForDiagnostics(bOn);
	}));
#endif // ASDA_DEV_CONSOLE

// Диагностический сеттер к команде выше. Публичный, потому что лямбда живёт вне класса.
void UAsdaNetSubsystem::ForceGmHiddenForDiagnostics(bool bHidden)
{
	bGmHidden = bHidden;
	GmHideLatch.Disarm();
	bGmHiddenFromEntry = false;
	UE_LOG(LogAsdaNet, Warning, TEXT("[GM-скрытие] ДИАГНОСТИКА: признак принудительно = %s (на сервер ничего не ушло)"),
		bHidden ? TEXT("СКРЫТ") : TEXT("ВИДИМ"));
	OnGmHideResult.Broadcast(/*bAccepted*/true, /*bHidden*/bHidden);
}
