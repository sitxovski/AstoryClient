// Copyright (c) 2026 @sitxovski (Discord)
// AsdaNetSubsystemRecvInternal.h — общий внутренний заголовок частей `AsdaNetSubsystem*.cpp`.
//   ★ R2 рефакторинга (10.11.2026): сюда ДОСЛОВНО вынесены помощники, нужные нескольким частям (static/анонимное
//   пространство имён сохранены — у каждой части своя копия без состояния), и объявления определений, оставшихся
//   в одной части (ключи, категория журнала). Подключать ПОСЛЕ включений пролога части: своих #include здесь нет.
#pragma once

// ── Объявления определений, оставшихся в одной части (R2) ──
extern int32 GAsdaSkillSelfHitNoDamage;   // переменная ключа-ссылки `asda.Skill.SelfHitNoDamage` (определение без static — AsdaNetSubsystem.cpp)
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarAsdaNetPortalLogoutOnOldConn;    // asda.Net.PortalLogoutOnOldConn (определение без static — AsdaNetSubsystem_Connect.cpp)
extern TAutoConsoleVariable<FString> CVarAsdaGmOperatorNamePrefixes;    // asda.Gm.OperatorNamePrefixes (определение без static — AsdaNetSubsystem_Gm.cpp)
#else
extern TAsdaConstCVar<int32> CVarAsdaNetPortalLogoutOnOldConn;          // Shipping: константа на месте ключа
extern TAsdaConstCVar<FString> CVarAsdaGmOperatorNamePrefixes;          // Shipping: константа на месте ключа
#endif

#if ASDA_COMBAT_TRACE
// Журнал боя: одно значение урона из пакета словами. Таблица кодов — та же, что AsdaCtWorld::Hit (AsdaWorldSubsystem.cpp)
//   и памятка combat-result-codes: 0 — пусто/промах, −1 уклон, −2 промах, −3…−5 иммунитет, −6/−7 промах по состоянию;
//   бит 31 (0x80000000|урон, т.е. значение < −7) — КРИТ, урон = младшие 31 бит. Пример: «0x80000032→крит 50».
static FString AsdaCtDmg(int32 V)
{
	if (V == 0)  { return TEXT("0(пусто/промах)"); }
	if (V == -1) { return TEXT("-1(уклон)"); }
	if (V == -2) { return TEXT("-2(промах)"); }
	if (V >= -5 && V <= -3) { return FString::Printf(TEXT("%d(иммун)"), V); }
	if (V == -6 || V == -7) { return FString::Printf(TEXT("%d(промах)"), V); }
	if (V < -7)  { return FString::Printf(TEXT("0x%08X→крит %d"), (uint32)V, (int32)((uint32)V & 0x7FFFFFFFu)); }
	return FString::Printf(TEXT("%d"), V);
}
// Журнал боя: очередь ударов «[45,-2(промах),0x80000032→крит 50,0]».
static FString AsdaCtQueue(const TArray<int32>& Q)
{
	FString S = TEXT("[");
	for (int32 i = 0; i < Q.Num(); ++i) { if (i) { S += TEXT(","); } S += AsdaCtDmg(Q[i]); }
	return S + TEXT("]");
}
#endif
// Разбор 4056 о соседе — живёт только на время рассылки `OnMapEntryFx` (читает мир через `AsdaPeerEntry4056`).
struct FAsdaPeer4056
{
	int32 Uidx = -1;          // userIdx соседа
	int32 TileX = -1;         // тайл воскрешения X (@2)
	int32 TileZ = -1;         // тайл воскрешения Z (@4)
	int32 Hp = -1;            // HP (@6)
	int32 Mp = -1;            // MP (@10)
	bool bDeadArrival = false;// @20 == 200 — прибыл мёртвым
	bool bValid = false;      // разбор актуален
};

// ═══ ★★★ B5 (аудит №4 `system_dedup_audit_4.md`): ЕДИНАЯ ПРОВЕРКА ДЛИНЫ ТЕЛА ПАКЕТА ═══════════
//
// ЧТО БЫЛО. Проверок «тело достаточной длины» в этом файле 121 в двух несовместимых по последствиям
//   формах. Форма `if (тело < N) { UE_LOG(Warning); break; }` (44 места) КРИЧИТ о коротком пакете.
//   Форма `if (тело >= N) { …разбор… }` (77 мест, из них 74 без `else`) при коротком теле МОЛЧА
//   ничего не делает: «пакет пришёл битым» становится неотличимо от «пакет не приходил вовсе».
//   Это ровно тот класс ложного отрицания, против которого написан закон `false-negatives-method`.
//
// ОБРАЗЕЦ, К КОТОРОМУ ПРИВЕДЕНО, УЖЕ БЫЛ В ЭТОМ ЖЕ ФАЙЛЕ: `case 4015` (проверка полей отражения
//   @31/@33) и `case 5047` (охрана 688 б) — короткий пакет там пишет предупреждение и идёт дальше.
//
// ЧТО ДЕЛАЕТ. Возвращает true, если тела хватает; иначе ВСЕГДА пишет предупреждение с опкодом,
//   фактической и требуемой длиной — и возвращает false, то есть поток управления вызывающего
//   НЕ МЕНЯЕТСЯ (замена `B.size() >= N` на `NetNeedLen(B, N, Pkt.Opcode)` — выражение в выражение).
//   ⚠ Проверка НЕ ужесточается и НЕ ослабляется: сравнение то же самое, добавлен только голос.
static bool NetNeedLen(const std::vector<uint8>& Body, int32 Need, int32 Opcode, const TCHAR* What = nullptr)
{
	if ((int32)Body.size() >= Need) { return true; }
	UE_LOG(LogAsdaNet, Warning, TEXT("Пакет %d: тело %d б < %d б — %s пропущен (короткий/битый кадр)"),
		Opcode, (int32)Body.size(), Need, What ? What : TEXT("разбор"));
	return false;
}
// Та же проверка для уже скопированного в TArray тела (снапшот enter-world и т.п.).
static bool NetNeedLen(const TArray<uint8>& Body, int32 Need, int32 Opcode, const TCHAR* What = nullptr)
{
	if (Body.Num() >= Need) { return true; }
	UE_LOG(LogAsdaNet, Warning, TEXT("Пакет %d: тело %d б < %d б — %s пропущен (короткий/битый кадр)"),
		Opcode, Body.Num(), Need, What ? What : TEXT("разбор"));
	return false;
}

// ★ Копия тела пакета в TArray — тела семейств «Сообщество»/«Соулмейт» уходят подписчикам целиком
//   (записи переменной длины разбирает UI, у него же лежат таблицы loc). Отдельная точка нужна,
//   чтобы не плодить один и тот же трёхстрочник в полутора десятке приёмников.
//   ★ N8 (В1 дедупа №2): ручные копии «SetNum + Memcpy» / «Append» ЦЕЛОГО тела в обработчиках сведены сюда
//   (копии ПЕРВЫХ N байт — другая операция, они остаются у своих обработчиков).
static TArray<uint8> NetBodyToArray(const std::vector<uint8>& Body)
{
	TArray<uint8> Out;
	Out.SetNumUninitialized((int32)Body.size());
	if (!Body.empty()) { FMemory::Memcpy(Out.GetData(), Body.data(), Body.size()); }
	return Out;
}

// ★ [ПОРТ] Подтянуть список операторских префиксов имени из ini (тело — рядом с самой
//   консольной переменной, ниже по файлу; здесь только объявление, чтобы звать из Initialize).
static void AsdaGmLoadOperatorNamePrefixesFromIni();

// Тело загрузчика ini (объявление — перед `Initialize`). Значение из ini кладём с приоритетом
//   SetByProjectSetting: ручная правка из консоли (SetByConsole) его перебьёт, а перезапуск снова
//   возьмёт ini. Пустая/отсутствующая строка ini умолчание не трогает.
static void AsdaGmLoadOperatorNamePrefixesFromIni()
{
	// ★ античит А2 (12.10.2026, мера 6): в Shipping список префиксов — константа `GM_,ADM_`; из ini (в том числе из
	//   пользовательского `Saved/Config/…/Game.ini`) его не берём — иначе игрок «назначал» бы себя оператором для клиента.
#if ASDA_DEV_CONSOLE
	if (!GConfig) { return; }
	FString Prefixes;
	if (!GConfig->GetString(TEXT("AsdaGm"), TEXT("OperatorNamePrefixes"), Prefixes, GGameIni)) { return; }
	Prefixes.TrimStartAndEndInline();
	if (Prefixes.IsEmpty()) { return; }
	CVarAsdaGmOperatorNamePrefixes.AsVariable()->Set(*Prefixes, ECVF_SetByProjectSetting);
	UE_LOG(LogAsdaNet, Log,
		TEXT("[GM] операторские префиксы имени взяты из Config/DefaultGame.ini [AsdaGm]: «%s»"), *Prefixes);
#endif // ASDA_DEV_CONSOLE
}

// ★ `NetBodyToArray` (копия тела пакета в TArray) перенесена выше, к `NetNeedLen` (N8 В1 дедупа №2): ею пользуются и
//   обработчики, стоящие в файле раньше этого места.

// ★★★ ОТКАЗЫ ПЕРЕХОДА — ОБЩАЯ ТОЧКА ДВУХ ОБРАБОТЧИКОВ (27.09.2026, `Reports/owner_0927/port_5055.md`).
//   Эталоны `Recv_PortalChannelChange@0xF45BD0` (5055, `network.c:42996-43081`) и `portalHandleChannelWarp@0xF464C0`
//   (6201, `world_map.c:8473-8499`) разбирают коды отказа ОДИНАКОВО — буква в букву, с одними и теми же id, цветом и
//   временем жизни. Отличие одно: у 5055 есть ещё диапазон 13…20 (битва армий), у 6201 его нет (`return 0`).
//   Канал — по обработчику (закон `loc-per-handler-not-by-family`), цвет −11514, 10 000 мс, звука нет:
//     код 0    → 1096 «Главный сервер отключен…»     — только плашка 221 (`CControl_SetTooltipText`);
//     код 2    → 1097 «Сервер переполнен.»           — только плашка 221;
//     код 8    → 13916 «Ты труп…»                    — плашка 221 + строка журнала (`CChat_AddTokenizedLines`);
//     код 12   → 2231 «Недостаточно золота.»         — плашка + журнал (стоковый отказ 5477 телепорта кристаллом);
//     13…20    → 7101 + (код − 13) (битва армий)     — плашка + журнал; ТОЛЬКО 5055.
//   Прочие коды здесь не разбираются (false): у 5055 это окна подземелий 3…7/9…11, у 6201 — молчаливый `return 0`.
//   `CControl_SetTooltipText` — миснейм IDA: это строка в плашке WARMESSAGE_WIN 221 (`AsdaNotice::Plank`).
//   ⚠ Прежняя редакция обоих обработчиков слала 0/2 (6201 — и 8/12) категорией `MoveRefused` (журнал + error.wav):
//   ни журнала у 0/2, ни звука у всех кодов эталон не даёт. Категория `System` — в её маршруте звука нет.
static bool PostPortalRefusalNotice(const UGameInstance* GI, int32 Opcode, uint8 Code, bool bRoseWarRange)
{
	int32 LocId = 0;          // id строки `error list.bin` / DT_GameText
	bool  bAlsoChat = true;   // true — пара «плашка + журнал», false — одна плашка (коды 0 и 2)
	switch (Code)
	{
	case 0x00: LocId = 1096;  bAlsoChat = false; break;
	case 0x02: LocId = 1097;  bAlsoChat = false; break;
	case 0x08: LocId = 13916; break;
	case 0x0C: LocId = 2231;  break;
	default:
		if (bRoseWarRange && Code >= 0x0D && Code <= 0x14) { LocId = 7101 + (Code - 0x0D); }   // `dungeonNoticeIdx + 7101`
		break;
	}
	if (LocId == 0) { return false; }

	const FColor Gold = UAsdaChatSubsystem::ColGold();   // −11514 = 0xFFFFD306
	if (bAlsoChat) { AsdaNotice::PlankAndChatLoc(GI, EAsdaMsgCategory::System, LocId, Gold, 10000.f, Opcode); }
	else           { AsdaNotice::PlankLoc(GI, EAsdaMsgCategory::System, LocId, Gold, 10000.f, Opcode); }
	UE_LOG(LogAsdaNet, Warning, TEXT("[портал] %d отказ, код=%u → нотис %d (%s)"),
		Opcode, (uint32)Code, LocId, bAlsoChat ? TEXT("плашка 221 + журнал") : TEXT("плашка 221"));
	return true;
}

// ── Объявление, которому нужен тип `FAsdaPeer4056` (выше) ──
extern FAsdaPeer4056 GAsdaPeer4056;   // последний чужой 4056 для входа соседа (определение без static — AsdaNetSubsystem.cpp)
