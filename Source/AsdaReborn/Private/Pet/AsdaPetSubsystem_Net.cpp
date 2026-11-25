// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPetSubsystem_Net.cpp — ★ R3-c 12.11.2026 (разрез AsdaPetSubsystem.cpp, дословный перенос): приём пакетов 61xx и запросы: вызов, инкубация, кормление, отпуск, слияние, имя, слоты, снятие предела.
//   Общее для частей — в AsdaPetSubsystem_Private.h; шапка и реверс — в AsdaPetSubsystem.cpp.
#include "Pet/AsdaPetSubsystem.h"
#include "AsdaWeightWire.h"   // ★ 29.10.2026 (вес ×2): AsdaWeightFromWire — вес персонажа по проводу u16
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaSelf.h"   // ★ В2-A дедупа №2: общая точка «это я?» (класс/уровень/charId/userNum)

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Login/AsdaLoginData.h"     // Decode/EncodeCp1251 — единственный кодек cp1251 порта
#include "Net/NetOpcodes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"                      // TActorIterator — поиск модели своего питомца (баг 6 пачки 20)
#include "World/AsdaWorldSubsystem.h"         // LocalPlayerActor — свой персонаж
#include "World/AsdaPetActor.h"               // модель питомца в мире
#include "Engine/DataTable.h"
#include "UI/AsdaNotice.h"            // ★ волна 8: общая точка нотиса AsdaPetUi::NoticeMsgBox               // ★ волна 8: обход `petexptable` в AsdaPetUi::ExpRow
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaPetSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaPetSubsystem*.cpp

namespace {
	// ★★★ ВОЛНА 8 (пункт 3 ревизии): КЛЮЧ ОТКАТА К ДЕФЕКТУ ПД9 В ПРИЁМЕ 6113.
	//   0 (по умолчанию) — ИСПРАВЛЕНО: съеденный материал ищется по МЛАДШИМ 16 битам uid,
	//   то есть находится и при uid >= 32768. 1 — ДОСЛОВНО КАК ОРИГИНАЛ: сравнение ПОЛНОГО
	//   32-битного uid со знакорасширенным i16 полем тела (npc_mob_pet.c:7677 → :5549), при котором
	//   питомец с большим uid остаётся в списке фантомом. Решение о режиме — за владельцем.
	static ASDA_DEV_CVAR(int32, CVarPetLegacyUnionUidCompare,
		TEXT("asda.Pet.LegacyUnionUidCompare"), 0,
		TEXT("Сверка съеденных материалов в ответе 6113: 0 — по младшим 16 битам uid ")
		TEXT("(ИСПРАВЛЕНИЕ дефекта ПД9: фантомов не остаётся); 1 — дословно как оригинал: ")
		TEXT("полный uid против знакорасширенного i16 (uid >= 32768 не удаляется)."));

	// ★ [ИСПРАВЛЕНИЕ] 09.11.2026, пачка 20 баг 6 «после удаления питомца его модель остаётся». Опрос эталона раз в 500 мс
	//   (`entity_char.c:7778-7780`): призванного (`g_pActiveBuffEntry` = `wnd[155]`) нет — модель питомца у своего персонажа
	//   снимается (`mapDecoModelRemoveById`). Приёмник 6111 зануляет `wnd[155]` (`item_enhance.c:4763-4764`), 6107 — тоже, то есть
	//   модель уходит через ≤ 500 мс после ответа. Порт снимал модель только по 6120 −1 через очередь (до 2 с), а без 6120
	//   (своего 6120 −1 сервер шлёт лишь при выбросе ПРИЗВАННОГО, `sub_454920`) модель стояла до следующей карты.
	//   1 — снимать модель своего питомца в кадре ответа, погасившего призванного; 0 — прежнее поведение.
	static ASDA_DEV_CVAR(int32, CVarPetDropModelOnRelease,
		TEXT("asda.Pet.DropModelOnRelease"), 1,
		TEXT("1 — 6111 (выброшен) и 6107 (спрятался от голода) по призванному сразу снимают модель питомца у своего персонажа; ")
		TEXT("0 — модель уходит только по 6120 −1 (очередь до 2 с)."));

	// Снять модель своего питомца в мире (разбор — у `CVarPetDropModelOnRelease`). Возвращает, сколько моделей снято.
	static int32 DropOwnPetModel(UGameInstance* GI, const TCHAR* Why)
	{
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		AActor* Me = WS ? WS->LocalPlayerActor.Get() : nullptr;
		UWorld* W = GI ? GI->GetWorld() : nullptr;
		if (!Me || !W) { return 0; }
		int32 N = 0;
		for (TActorIterator<AAsdaPetActor> It(W); It; ++It)
		{
			if (IsValid(*It) && It->OwnerChar.Get() == Me) { It->Destroy(); ++N; }
		}
		UE_LOG(LogAsdaPet, Log, TEXT("[питомцы] %s: модель своего питомца снята (%d)"), Why, N);
		return N;
	}
}

// ══════════════════════════════ ОБЩИЕ КУСКИ РАЗБОРА/СБОРКИ ════════════════════════════════════
void UAsdaPetSubsystem::ReadPetRecord(const TArray<uint8>& B, int32 Off, FAsdaPetSlot& Out)
{
	// PetRecord = 41 байт, раскладка из ctor `sub_45B710` (GameServer.c:36961) и приёмника списка.
	const uint8* P = B.GetData() + Off;
	Out.PetIndex = AsdaBytes::RdI16(P + 0);                       // +0  u16 (−1 = слот пуст)
	Out.Name     = FAsdaLoginData::DecodeCp1251(P + 2, kNameBytes); // +2  char[16]
	Out.Fullness = (int32)P[18];                                  // +18 u8
	Out.Level    = (int32)P[19];                                  // +19 u8
	Out.MaxLevel = (int32)P[20];                                  // +20 u8  (клиентский CPetSlot+0x6C)
	Out.Exp      = (int32)AsdaBytes::RdU16(P + 21);               // +21 u16
	// +23 + 6j — три пары {u16 тип, i32 значение}. Массив фиксированной длины: только Init.
	Out.Abilities.Init(FAsdaPetAbility(), 3);
	for (int32 j = 0; j < 3; ++j)
	{
		Out.Abilities[j].Type  = (int32)AsdaBytes::RdU16(P + 23 + 6 * j);
		Out.Abilities[j].Value = AsdaBytes::RdI32(P + 25 + 6 * j);
	}
}

void UAsdaPetSubsystem::WritePetRecord(TArray<uint8>& B, int32 Off, const FAsdaPetSlot& In)
{
	uint8* P = B.GetData() + Off;
	AsdaBytes::WrI16(P + 0, In.PetIndex);
	FAsdaLoginData::EncodeCp1251(In.Name, P + 2, kNameBytes);
	P[18] = (uint8)In.Fullness;
	P[19] = (uint8)In.Level;
	P[20] = (uint8)In.MaxLevel;
	AsdaBytes::WrI16(P + 21, In.Exp);
	for (int32 j = 0; j < 3; ++j)
	{
		const FAsdaPetAbility& A = In.Abilities.IsValidIndex(j) ? In.Abilities[j] : FAsdaPetAbility();
		AsdaBytes::WrI16(P + 23 + 6 * j, A.Type);
		AsdaBytes::WrI32(P + 25 + 6 * j, A.Value);
	}
}

// ══════════════════════════════════ ПРИЁМ: ДИСПЕТЧЕР ══════════════════════════════════════════
void UAsdaPetSubsystem::OnPetPacket(int32 Opcode, const TArray<uint8>& Body)
{
	RxByOpcode.FindOrAdd(Opcode) += 1;
	++RxSinceReset;
	// ★ Волна 2: накопитель события. Приёмник помечает «пакет мой» через `MarkEvent`, и ровно
	//   один `OnPetEvent` уходит ПОСЛЕ приёмника — так подписчик (окна) видит уже применённое
	//   состояние, а не половину. Чужой/битый пакет метки не ставит и события не даёт.
	EventOpcode = -1;
	EventResult = 0;
	EventPetUid = -1;
	switch (Opcode)
	{
		case OP_PET_CALL_RESULT:        OnCallResult6101(Body);      break;
		case OP_PET_INCUBATE_RESULT:    OnIncubateResult6103(Body);  break;
		case OP_PET_CONDITION_UPDATE:   OnConditionUpdate6104(Body); break;
		case OP_PET_EXP_UPDATE:         OnExpUpdate6105(Body);       break;
		case OP_PET_LIST:               OnPetList6106(Body);         break;
		case OP_PET_HUNGER_RECALL:      OnHungerRecall6107(Body);    break;
		case OP_PET_FEED_RESULT:        OnFeedResult6109(Body);      break;
		case OP_PET_RELEASE_RESULT:     OnReleaseResult6111(Body);   break;
		case OP_PET_UNION_RESULT:       OnUnionResult6113(Body);     break;
		case OP_PET_NAME_CHANGE_RESULT: OnRenameResult6119(Body);    break;
		case OP_PET_SLOT_UP_RESULT:     OnSlotUpResult6122(Body);    break;
		case OP_PET_SLOT_COUNT:         OnSlotCount6123(Body);       break;
		case OP_PET_LIMITBREAK_RESULT:  OnLimitBreakResult6149(Body);break;
		default:
			UE_LOG(LogAsdaPet, Warning, TEXT("[питомцы] неизвестный опкод %d в пет-делегате"), Opcode);
			break;
	}
	// Эталон в КАЖДОМ приёмнике заканчивает парой «перерисовать 43000 + перерисовать 43200».
	//   У нас это одно событие — окно решает само (закон `loc-per-handler-not-by-family`).
	if (EventOpcode == Opcode)
	{
		OnPetEvent.Broadcast(EventOpcode, EventResult, EventPetUid);
	}
}

// ★ Волна 2: пометка «пакет мой» — см. объявление в `.h`.
void UAsdaPetSubsystem::MarkEvent(int32 Opcode, int32 Result, int32 PetUniqueId)
{
	EventOpcode = Opcode;
	EventResult = Result;
	EventPetUid = PetUniqueId;
}

// ── 6101 (9 б) — ответ на призыв/отзыв ────────────────────────────────────────────────────────
void UAsdaPetSubsystem::OnCallResult6101(const TArray<uint8>& B)
{
	// Эталон снимает признак «в полёте» ПЕРВЫМ делом и БЕЗУСЛОВНО (item_enhance.c:4484),
	//   до сверки charId. Повторяем 1:1: иначе чужой/битый пакет залипил бы кнопку.
	// ★★★ ВОЛНА 9, ПОПРАВКА РЕВИЗИИ: ЧЕТВЁРТОЕ ИЗ ЧЕТЫРЁХ МЕСТ ГОНКИ — ЛЕЧИТСЯ СВЕРКОЙ ЭХА.
	//   Прежняя запись «номера запроса в телах 6100/6101 НЕТ» была ЛОЖНЫМ «нет» и противоречила
	//   собственной раскладке в шапке этого же файла: 6101 несёт `petUid i32@5`
	//   (`GameServer.c:34457`, `:34476`, `:34492`). Разошлось эхо ⇒ ответ на ПРЕДЫДУЩИЙ 6100.
	//   ⚠ ЧЕСТНЫЙ ОСТАТОК: повтор запроса ПО ТОМУ ЖЕ питомцу даёт то же эхо и неразличим.
	//   ⚠ Длину проверяем ДО чтения эха: тело короче 9 б поля `+5` не содержит.
	const int32 EchoPet = (B.Num() >= 9) ? AsdaBytes::RdI32(&B[5]) : INDEX_NONE;
	if (CallLatch.IsForeignEcho(EchoPet))
	{
		UE_LOG(LogAsdaPet, Warning,
			TEXT("[питомцы] 6101 относится к ДРУГОМУ запросу: эхо petUid=%d не совпало с ")
			TEXT("отправленным %d — это ответ на ПРЕДЫДУЩИЙ 6100. Засов не снимаем."),
			EchoPet, CallLatch.EchoKey);
		return;
	}
	if (CallLatch.ConsumeTimedOut())
	{
		UE_LOG(LogAsdaPet, Warning,
			TEXT("[питомцы] 6101 пришёл ПОСЛЕ таймаута, но эхо petUid=%d СОВПАЛО — ответ наш. ")
			TEXT("Неразличим лишь повтор запроса ПО ТОМУ ЖЕ питомцу."), EchoPet);
	}
	ClearCallRequestInFlight(nullptr);   // ★ волна 8: с признаком снимается и таймаут
	if (B.Num() < 9) { return; }
	// ★★★ ВОЛНА 8 (пункт 4 сквозной ревизии): ПОРОГ ПРЕДУПРЕЖДЕНИЯ ИСПРАВЛЕН С 18 НА 29.
	//   ЧЕМ ПЛОХ БЫЛ 18. Ответ 6101 уходит С ПАДДИНГОМ (`a8 == 1` у сборщика кадра), а
	//   длина паддинга есть `dword_68B2534 % 20`, то есть 0…19 байт (GameServer.c:137824-137826).
	//   ЗНАЧИТ ЗАКОННАЯ ДЛИНА ТЕЛА 6101 — от 9 до 28 байт. Порог 18 ловил НОРМАЛЬНЫЕ
	//   пакеты в 11 случаях из 20 (паддинг 9…19) и печатал страшное слово «дефект» на ровном месте.
	//   ЧТО ЛОВИТСЯ ПО ДЛИНЕ НА САМОМ ДЕЛЕ:
	//     • ПД2 (GameServer.c:35532 шлёт под 6101 тело 6109, 89 б) — ДА: 89 > 28 при любом паддинге;
	//     • ПД1 (GameServer.c:34566 шлёт под 6101 тело 6103) — НЕТ: оно короткое и целиком
	//       помещается в законный диапазон 9…28, то есть по ДЛИНЕ он неотличим от своего
	//       и здесь НЕ ДИАГНОСТИРУЕТСЯ. Называть его в сообщении значило бы врать.
	//   ★ Порог выбран НЕ на глаз: 9 (своё тело) + 20 (максимум паддинга не более 19,
	//   то есть первая НЕЗАКОННАЯ длина — 29).
	static constexpr int32 kCallResultBody     = 9;    // своё тело 6101
	static constexpr int32 kMaxFramePadding    = 19;   // GameServer.c:137824 — `% 20`, то есть 0…19
	if (B.Num() > kCallResultBody + kMaxFramePadding)
	{
		UE_LOG(LogAsdaPet, Warning,
			TEXT("[питомцы] 6101 пришёл длиной %d б — больше законного максимума %d ")
			TEXT("(своё тело %d + паддинг до %d, GameServer.c:137824). Это ДЕФЕКТ СЕРВЕРА ПД2: ")
			TEXT("GameServer.c:35532 шлёт под опкодом 6101 тело 6109 (89 б). Читаем свои %d б. ")
			TEXT("★ ПД1 (тело 6103 под 6101, GameServer.c:34566) по ДЛИНЕ НЕОТЛИЧИМ от законного ")
			TEXT("пакета и здесь не ловится."),
			B.Num(), kCallResultBody + kMaxFramePadding, kCallResultBody, kMaxFramePadding, kCallResultBody);
	}
	const int32 CharId = AsdaBytes::RdI32(&B[0]);
	if (!IsForSelf(CharId, OP_PET_CALL_RESULT)) { return; }
	const int32 Result = (int32)B[4];
	const int32 Uid    = AsdaBytes::RdI32(&B[5]);
	LastCallResult = Result;
	// ★ Волна 2: метка события — окно перерисуется и покажет нотис СВОЕЙ ветки эталона.
	MarkEvent(OP_PET_CALL_RESULT, Result, Uid);

	if (Result != 1)
	{
		// Отказы 2 (питомец не найден) и 3 (сытость 0). Эталон состояние НЕ трогает — только
		//   перерисовывает окно (item_enhance.c:4529-4532). Нотис волны 2.
		UE_LOG(LogAsdaPet, Log, TEXT("[питомцы] 6101 отказ: код=%d pet=%d"), Result, Uid);
		return;
	}

	// Успех — ТУМБЛЕР, и решает его КЛИЕНТ по своему текущему признаку (item_enhance.c:4497-4527):
	//   у совпавшего слота признак переворачивается, у всех прочих гасится.
	bool bNowActive = false;
	for (FAsdaPetSlot& S : Slots)
	{
		if (S.UniqueId != Uid) { S.bIsActive = false; continue; }
		S.bIsActive = !S.bIsActive;
		bNowActive = S.bIsActive;
	}
	ActiveUniqueId = bNowActive ? Uid : -1;
	UE_LOG(LogAsdaPet, Log, TEXT("[питомцы] 6101 успех: pet=%d теперь %s"),
		Uid, bNowActive ? TEXT("ПРИЗВАН") : TEXT("отозван"));
}

// ── 6103 (18 б) — ответ инкубатора ────────────────────────────────────────────────────────────
void UAsdaPetSubsystem::OnIncubateResult6103(const TArray<uint8>& B)
{
	if (B.Num() < 18) { return; }
	// ★★★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА, ПОМЕЧЕНО (волна 8, пункт 9 ревизии; закон
	//   `fix-original-defects`). СВЕРКИ charId У ЭТАЛОНА ЗДЕСЬ НЕТ ВООБЩЕ:
	//   `CPetWin_OnResult@0xDC2CA0` начинается с `memcpy(packet, pPacket + 9, 0x12u)` и сразу
	//   применяет тело (npc_mob_pet.c:5060-5078) — поле +0 он не читает ни разу.
	//   Зачем сверка всё же стоит: 6103 адресный (сервер шлёт его ОДНОМУ
	//   пользователю, GameServer.c:34771), и на живом сервере разницы нет; но чужой
	//   либо поддельный пакет у эталона почистил бы НАШИ ячейки сумки. До волны 8
	//   это отступление стояло БЕЗ ПОМЕТКИ, то есть читалось как дословность.
	const int32 CharId = AsdaBytes::RdI32(&B[0]);
	if (!IsForSelf(CharId, OP_PET_INCUBATE_RESULT)) { return; }
	LastIncubateResult = (int32)B[4];
	// ★ ВОЛНА 3: сохраняем ВСЁ тело — номера ячеек нужны окну 42000, чтобы очистить инвентарь
	//   ровно так, как это делает эталон (npc_mob_pet.c:5098-5110 и :5138-:5150).
	//   `RdI16` отдаёт знаковое: умолчание тела 0xFFFF читается как −1 («поля нет»).
	LastIncubateEcho.Result       = LastIncubateResult;
	LastIncubateEcho.CatalystBag  = (int32)B[5];
	LastIncubateEcho.CatalystSlot = AsdaBytes::RdI16(&B[6]);
	LastIncubateEcho.EggBag       = (int32)B[8];
	LastIncubateEcho.EggSlot      = AsdaBytes::RdI16(&B[9]);
	LastIncubateEcho.BoosterBag   = (int32)B[11];
	LastIncubateEcho.BoosterSlot  = AsdaBytes::RdI16(&B[12]);
	LastIncubateEcho.WeightCur    = AsdaWeightFromWire(AsdaBytes::RdI32(&B[14]));   // ★ 29.10.2026: вес u16
	// ★ Волна 2: метка события — окно перерисуется и покажет нотис СВОЕЙ ветки эталона.
	MarkEvent(OP_PET_INCUBATE_RESULT, LastIncubateResult, -1);
	// Сам питомец придёт отдельным 6106 (GameServer.c:34771-34788: сервер шлёт 6103, затем,
	//   если код 1, добавляет слот и шлёт список). Здесь — только код и вес для HUD.
	UE_LOG(LogAsdaPet, Log,
		TEXT("[питомцы] 6103 инкубатор: код=%d катализатор=(%d,%d) яйцо=(%d,%d) бустер=(%d,%d) вес=%d"),
		LastIncubateResult, (int32)B[5], AsdaBytes::RdI16(&B[6]), (int32)B[8], AsdaBytes::RdI16(&B[9]),
		(int32)B[11], AsdaBytes::RdI16(&B[12]), AsdaBytes::RdI32(&B[14]));
}

// ── 6104 (29 б) — сытость и три бонуса ────────────────────────────────────────────────────────
void UAsdaPetSubsystem::OnConditionUpdate6104(const TArray<uint8>& B)
{
	if (B.Num() < 29) { return; }
	// ★ Здесь тело начинается с userNum (u16), charId лежит со +2 (GameServer.c:35204-35205).
	const int32 CharId = AsdaBytes::RdI32(&B[2]);
	if (!IsForSelf(CharId, OP_PET_CONDITION_UPDATE)) { return; }
	const int32 Uid = AsdaBytes::RdI32(&B[6]);
	// ★ Волна 2: метка события — окно перерисуется и покажет нотис СВОЕЙ ветки эталона.
	MarkEvent(OP_PET_CONDITION_UPDATE, 0, Uid);
	FAsdaPetSlot* S = FindMutable(Uid);
	if (!S)
	{
		UE_LOG(LogAsdaPet, Warning, TEXT("[питомцы] 6104 про неизвестного pet=%d — пропускаю"), Uid);
		return;
	}
	S->Fullness = (int32)B[10];
	S->Abilities.Init(FAsdaPetAbility(), 3);
	for (int32 j = 0; j < 3; ++j)
	{
		S->Abilities[j].Type  = (int32)AsdaBytes::RdU16(&B[11 + 6 * j]);
		S->Abilities[j].Value = AsdaBytes::RdI32(&B[13 + 6 * j]);
	}
	UE_LOG(LogAsdaPet, Verbose, TEXT("[питомцы] 6104: pet=%d сытость=%d бонусы=%d/%d/%d"),
		Uid, S->Fullness, S->Abilities[0].Value, S->Abilities[1].Value, S->Abilities[2].Value);
}

// ── 6105 (15 б) — опыт (и повышение уровня) ───────────────────────────────────────────────────
void UAsdaPetSubsystem::OnExpUpdate6105(const TArray<uint8>& B)
{
	if (B.Num() < 15) { return; }
	const int32 CharId = AsdaBytes::RdI32(&B[2]);
	if (!IsForSelf(CharId, OP_PET_EXP_UPDATE)) { return; }
	const int32 Uid = AsdaBytes::RdI32(&B[6]);
	// ★ Волна 2: метка события — окно перерисуется и покажет нотис СВОЕЙ ветки эталона.
	MarkEvent(OP_PET_EXP_UPDATE, 0, Uid);
	FAsdaPetSlot* S = FindMutable(Uid);
	if (!S) { return; }
	S->Exp = (int32)AsdaBytes::RdU16(&B[10]);
	// ★ Ветка повышения уровня. Клиент её читает (item_enhance.c:4695-4701): при ненулевом байте
	//   +12 берёт новый уровень из u16 +13. ★ СЕРВЕР ЭТОЙ СБОРКИ ЕЁ СТАВИТ: в ветке роста уровня
	//   `sub_456D90` пишет `v31 = 1; v32 = *(u8*)(активныйСлот + 23)` (GameServer.c:35331-35332),
	//   а `v31`/`v32` — это байты +12 и +13 буфера 6105 (`v27` на `ebp-0x24`, `v31` на `ebp-0x18`,
	//   `v32` на `ebp-0x17`; отправка :35354). Прежний комментарий «сервер признак не ставит» был
	//   неверен (ревизия 2026-09-21).
	if (B[12] != 0)
	{
		S->Level = (int32)AsdaBytes::RdU16(&B[13]);
	}
	UE_LOG(LogAsdaPet, Verbose, TEXT("[питомцы] 6105: pet=%d опыт=%d уровень=%d%s"),
		Uid, S->Exp, S->Level, B[12] ? TEXT(" (повышение)") : TEXT(""));
}

// ── 6106 (5 + 46·N) — список питомцев ─────────────────────────────────────────────────────────
void UAsdaPetSubsystem::OnPetList6106(const TArray<uint8>& B)
{
	// ★ ТОЧНАЯ проверка размера, без допуска на паддинг: этот пакет сервер шлёт с `a7 = 0`
	//   (GameServer.c:34788, 35458, 35470), а эталон валидирует `(кадр − 15) % 46 == 0`
	//   (network.c:25628-25630). Кадр = 10 + тело ⇒ в терминах порта `(тело − 5) % 46 == 0`.
	//   Закон `packet-size-check-no-padding`: лишний байт здесь = сдвиг всех записей.
	if (B.Num() < kListHeaderSize || ((B.Num() - kListHeaderSize) % kListRecordSize) != 0)
	{
		UE_LOG(LogAsdaPet, Error,
			TEXT("[питомцы] 6106: тело %d б не бьётся формулой 5 + 46·N — пакет отброшен целиком"), B.Num());
		return;
	}
	const int32 CharId = AsdaBytes::RdI32(&B[0]);
	if (!IsForSelf(CharId, OP_PET_LIST)) { return; }
	// ★ Волна 2: метка события — окно перерисуется и покажет нотис СВОЕЙ ветки эталона.
	MarkEvent(OP_PET_LIST, 0, -1);
	const int32 Flag  = (int32)B[4];   // см. шапку: клиент его игнорирует, до нас он всегда 0
	const int32 Count = (B.Num() - kListHeaderSize) / kListRecordSize;

	for (int32 i = 0; i < Count; ++i)
	{
		const int32 Off = kListHeaderSize + i * kListRecordSize;
		FAsdaPetSlot S;
		S.UniqueId = AsdaBytes::RdI32(&B[Off]);              // запись +0
		ReadPetRecord(B, Off + 4, S);                        // запись +4, 41 байт
		S.bIsActive = (B[Off + 45] != 0);                    // запись +45
		if (!S.IsFilled())
		{
			// Пустой слот сервер в список не кладёт, но если положит — игнорируем: у такого
			//   слота нет ключа, по которому окно могло бы его показать.
			continue;
		}
		AddOrUpdate(S);
		if (S.bIsActive) { ActiveUniqueId = S.UniqueId; }
	}
	UE_LOG(LogAsdaPet, Log,
		TEXT("[питомцы] 6106: пачка %d записей (флаг=%d), всего в списке %d, призван=%d"),
		Count, Flag, Slots.Num(), ActiveUniqueId);
}

// ── 6107 (8 б) — сытость упала в 0, сервер отозвал питомца ────────────────────────────────────
void UAsdaPetSubsystem::OnHungerRecall6107(const TArray<uint8>& B)
{
	if (B.Num() < 8) { return; }
	const int32 CharId = AsdaBytes::RdI32(&B[0]);
	if (!IsForSelf(CharId, OP_PET_HUNGER_RECALL)) { return; }
	const int32 Uid = AsdaBytes::RdI32(&B[4]);
	// ★ Волна 2: метка события — окно перерисуется и покажет нотис СВОЕЙ ветки эталона.
	MarkEvent(OP_PET_HUNGER_RECALL, 0, Uid);
	if (FAsdaPetSlot* S = FindMutable(Uid))
	{
		S->bIsActive = false;                   // эталон: `*(_BYTE*)matchedEntry = 0`
	}
	if (ActiveUniqueId == Uid)
	{
		ActiveUniqueId = -1;   // эталон: `self[155] = nullptr`
		if (CVarPetDropModelOnRelease.GetValueOnGameThread() != 0) { DropOwnPetModel(GetGameInstance(), TEXT("6107 спрятался от голода")); }
	}
	UE_LOG(LogAsdaPet, Log, TEXT("[питомцы] 6107: pet=%d спрятался от голода"), Uid);
}

// ── 6109 (89 б) — ответ на кормление ──────────────────────────────────────────────────────────
void UAsdaPetSubsystem::OnFeedResult6109(const TArray<uint8>& B)
{
	if (B.Num() < 89) { return; }
	// ★★★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА, ПОМЕЧЕНО (волна 8, пункт 9 ревизии; закон
	//   `fix-original-defects`). ПОРЯДОК У ЭТАЛОНА ДРУГОЙ: `netOnRefineResultPacket@0xDC9710`
	//   СНАЧАЛА печатает нотис 101102 «не по вкусу» при коде 5 (network.c:25897-25904)
	//   и только потом сверяет charId. Значит у оригинала ЧУЖОЙ пакет мог напечатать
	//   нотис нашему игроку. Порт сверяет ПЕРВЫМ и чужое отбрасывает совсем — то есть
	//   окно 43000 не увидит события и нотиса не покажет. Разбор последствий и ключ
	//   `asda.Pet.FeedNoticeDouble` — в `AsdaPetCageSubsystem.cpp`, ветка `OnFeedResult6109`.
	//   На живом сервере разницы нет: 6109 адресный (GameServer.c:35470 шлёт одному).
	const int32 CharId = AsdaBytes::RdI32(&B[0]);
	if (!IsForSelf(CharId, OP_PET_FEED_RESULT)) { return; }
	const int32 Uid = AsdaBytes::RdI32(&B[4]);
	LastFeedResult = (int32)B[65];
	// ★ ВОЛНА 6: снимок тела для окна 43000. Поля и доказательства смещений — у объявления
	//   `FAsdaPetFeedEcho`. Снимается ДО разбора кода и на ЛЮБОМ коде: эталон читает эти байты
	//   только в ветке 1, но окно по `Result` само решает, что применять, а снимок обязан быть
	//   определён и на отказах (иначе в нём осталось бы тело ПРЕДЫДУЩЕГО кормления).
	LastFeedEcho = FAsdaPetFeedEcho();
	LastFeedEcho.Result    = LastFeedResult;                 // +65 network.c:25908
	LastFeedEcho.Fullness  = (int32)B[66];                   // +66 :25921 `*(itemNode+76)`
	LastFeedEcho.WeightCur = AsdaWeightFromWire(AsdaBytes::RdI32(&B[67]));   // ★ 29.10.2026: вес u16;       // +67 :25931 HUD_UpdateMiniWeightBar
	LastFeedEcho.ItemCode  = AsdaBytes::RdI32(&B[8]);        // +8  :25944 Item+0
	LastFeedEcho.ItemBag   = (int32)B[12];                   // +12 :25934/:25937 Item+4
	LastFeedEcho.ItemSlot  = AsdaBytes::RdI16(&B[13]);       // +13 :25935/:25938 Item+5
	LastFeedEcho.ItemQty   = AsdaBytes::RdI32(&B[17]);       // +17 :25932/:25941 Item+9
	// ★★★ ВОЛНА 8 (БЛОКЕР-2): ЗАПИСЬ ПРЕДМЕТА ЦЕЛИКОМ, 57 б с +8. Ровно тот кусок, который эталон
	//   кладёт в ячейку одним `memcpy(…, &refineResult[2], 0x39u)` (network.c:25951). Длина тела
	//   проверена выше (>= 89), значит +8+57 = 65 <= 89 — выход за буфер невозможен.
	LastFeedEcho.ItemRec57.SetNumUninitialized(57);
	FMemory::Memcpy(LastFeedEcho.ItemRec57.GetData(), B.GetData() + 8, 57);
	// ★ Волна 2: метка события — окно перерисуется и покажет нотис СВОЕЙ ветки эталона.
	MarkEvent(OP_PET_FEED_RESULT, LastFeedResult, Uid);
	if (LastFeedResult != 1)
	{
		UE_LOG(LogAsdaPet, Log, TEXT("[питомцы] 6109 отказ: код=%d pet=%d"), LastFeedResult, Uid);
		return;
	}
	// Успех: эталон обновляет сытость и три бонуса совпавшего питомца (network.c:25918-25931).
	if (FAsdaPetSlot* S = FindMutable(Uid))
	{
		S->Fullness = (int32)B[66];
		S->Abilities.Init(FAsdaPetAbility(), 3);
		for (int32 j = 0; j < 3; ++j)
		{
			S->Abilities[j].Type  = (int32)AsdaBytes::RdU16(&B[71 + 6 * j]);
			S->Abilities[j].Value = AsdaBytes::RdI32(&B[73 + 6 * j]);
		}
		UE_LOG(LogAsdaPet, Log, TEXT("[питомцы] 6109 успех: pet=%d сытость=%d вес=%d корм=(код %d, сумка %d, ячейка %d, штук %d)"),
			Uid, S->Fullness, LastFeedEcho.WeightCur,
			LastFeedEcho.ItemCode, LastFeedEcho.ItemBag, LastFeedEcho.ItemSlot, LastFeedEcho.ItemQty);
	}
	else
	{
		// ★ Эталон обходит СВОЙ список и, не найдя uid, просто выходит из цикла (network.c:25914-25930) —
		//   вес и инвентарь он при этом применяет ВСЁ РАВНО (:25931 идёт после цикла). Окно
		//   волны 6 делает так же: оно читает снимок, а не запись питомца.
		UE_LOG(LogAsdaPet, Warning,
			TEXT("[питомцы] 6109 успех про НЕИЗВЕСТНОГО pet=%d — запись не обновлена, вес и сумка применятся (эталон так же)"), Uid);
	}
}

// ── 6111 (9 б) — ответ на «выбросить» ─────────────────────────────────────────────────────────
void UAsdaPetSubsystem::OnReleaseResult6111(const TArray<uint8>& B)
{
	if (B.Num() < 9) { return; }
	const int32 CharId = AsdaBytes::RdI32(&B[0]);
	if (!IsForSelf(CharId, OP_PET_RELEASE_RESULT)) { return; }
	const int32 Uid = AsdaBytes::RdI32(&B[4]);
	LastReleaseResult = (int32)B[8];
	// ★ Волна 2: метка события — окно перерисуется и покажет нотис СВОЕЙ ветки эталона.
	MarkEvent(OP_PET_RELEASE_RESULT, LastReleaseResult, Uid);
	if (LastReleaseResult != 1)
	{
		UE_LOG(LogAsdaPet, Log, TEXT("[питомцы] 6111 отказ: код=%d pet=%d"), LastReleaseResult, Uid);
		return;
	}
	// Эталон (item_enhance.c:4749-4771): снимает выбор, гасит «активного», если это был он,
	//   и УДАЛЯЕТ запись из списка.
	SelectedUniqueId = -1;
	if (ActiveUniqueId == Uid)
	{
		ActiveUniqueId = -1;
		if (CVarPetDropModelOnRelease.GetValueOnGameThread() != 0) { DropOwnPetModel(GetGameInstance(), TEXT("6111 выброшен призванный")); }
	}
	const int32 Removed = Slots.RemoveAll([Uid](const FAsdaPetSlot& S) { return S.UniqueId == Uid; });
	// ★★ 27.09 (СРЕДН-1 ревизии P31/P32): серверный патч P31 досылает 6111 на КАЖДЫЙ съеденный
	//   материал ДО 6113. Запоминаем ПОЛНЫЙ uid, чтобы 6113 не искал этот материал повторно по младшим
	//   16 битам и не стёр питомца-двойника (uid отличается на 65536·k). Без P31 во время юниона 6111
	//   не приходит, список пуст — 6113 работает как раньше. Потолок — страховка от роста.
	if (UnionReleasedUids.Num() < kMaxUnionReleasedUids) { UnionReleasedUids.Add(Uid); }
	UE_LOG(LogAsdaPet, Log, TEXT("[питомцы] 6111 успех: pet=%d удалён (записей убрано %d), осталось %d"),
		Uid, Removed, Slots.Num());
}

// ── 6113 (56 б) — ответ эволюции/юниона ───────────────────────────────────────────────────────
void UAsdaPetSubsystem::OnUnionResult6113(const TArray<uint8>& B)
{
	if (B.Num() < 56) { return; }
	// ⚠ Сверка «моё ли» идёт ПЕРВОЙ: иначе чужой отказ записался бы в `LastUnionResult`, а волна 2
	//   показала бы по нему нотис. Эталон тоже проверяет charId раньше, чем трогает состояние
	//   (npc_mob_pet.c:7656).
	const int32 CharId = AsdaBytes::RdI32(&B[0]);
	if (!IsForSelf(CharId, OP_PET_UNION_RESULT)) { return; }
	LastUnionResult = (int32)B[4];
	// ★★★ ВОЛНА 9: ЭХО ЗАПРОСА — младшие 16 бит uid трёх материалов (+50/+52/+54). Сохраняем
	//   ДО любой ветки отказа: сервер кладёт эхо и в отказах тоже (`GameServer.c:35986`,
	//   ветки кодов 3/4/7), значит по нему ответ на ПРЕДЫДУЩИЙ 6112 отличается от текущего.
	//   Читает окно 43500 (`GetLastUnionMatLow16`) для сверки в `FAsdaReplyLatch::IsForeignEcho`.
	for (int32 i = 0; i < 3; ++i)
	{
		LastUnionMatLow16[i] = (int32)AsdaBytes::RdU16(&B[50 + 2 * i]);
	}
	// ★ Волна 2: метка события — окно перерисуется и покажет нотис СВОЕЙ ветки эталона.
	//   ⚠ По 6113 нотиса ОТКАЗА в оригинале НЕТ вовсе: весь разбор лежит внутри `if (result == 1)`
	//   (npc_mob_pet.c:7654). Событие нужно, чтобы окно 43000 обновило список после юниона.
	MarkEvent(OP_PET_UNION_RESULT, LastUnionResult, AsdaBytes::RdI32(&B[5]));
	if (LastUnionResult != 1)
	{
		UE_LOG(LogAsdaPet, Log, TEXT("[питомцы] 6113 отказ: код=%d (3 материалы, 4 рецепт, 7 результат)"),
			LastUnionResult);
		LastUnionMissing = 0;
		UnionReleasedUids.Reset();   // ★ 27.09 (СРЕДН-1): юнион закончен — список «удалено 6111» больше не нужен
		return;
	}

	// Новый питомец: uid +5, PetRecord +9. Признак «призван» у него 0 (эталон: `LOBYTE(v7[0]) = 0`,
	//   npc_mob_pet.c:7663).
	FAsdaPetSlot New;
	New.UniqueId = AsdaBytes::RdI32(&B[5]);
	ReadPetRecord(B, 9, New);
	New.bIsActive = false;
	AddOrUpdate(New);

	// ★★★ +50/+52/+54 — МЛАДШИЕ 16 БИТ `uniqueId` ТРЁХ СЪЕДЕННЫХ МАТЕРИАЛОВ, и их надо УДАЛИТЬ
	//   из списка (ревизия 2026-09-21, перепроверено построчно). Прежняя трактовка «породы» и
	//   «удаление сделает сервер» были ошибочны:
	//     • ЧТО В ПОЛЕ: сервер пишет `*(_WORD*)&v14[2*i + 41] = v10[i + 1]` (GameServer.c:36090),
	//       а ровно этот же `v10[i+1]` он тут же сравнивает КАК DWORD с `PetSlot+0 UniqueIndex`
	//       (GameServer.c:36100). Значит это uid, урезанный до 16 бит при записи в тело.
	//     • КТО УДАЛЯЕТ: эталон, сам. `petUI_OnPetDataPacket` прогоняет три значения через
	//       `petDataFindInvItemById` (npc_mob_pet.c:7677), а та ищет запись по `CPetSlot+0x04`
	//       (uid) и стирает её `mapEraseNode` (npc_mob_pet.c:5549-5553).
	//     • ПОЧЕМУ НЕ СЕРВЕР: после юниона он шлёт ТОЛЬКО 6113 (`sub_4590E0`, GameServer.c:36115);
	//       список 6106 в этой ветке не отправляется, чистку тридцати слотов он делает у себя
	//       (`sub_455D90`, :36104). Без локального удаления в окне 43000 остались бы ТРИ
	//       ФАНТОМНЫХ ПИТОМЦА до следующего входа в мир.
	// ★★★ ДЕФЕКТ ОРИГИНАЛА ПД9 И ЕГО ЛЕЧЕНИЕ (ВОЛНА 8, пункт 3 сквозной ревизии;
	//   закон `fix-original-defects` — исправляем И ПОМЕЧАЕМ, а не молча).
	//   ЧТО ДЕЛАЕТ ЭТАЛОН, ДОСЛОВНО. `petDataFindInvItemById(g_bDeviceReloadPending,
	//   *((__int16*)&v8[12] + i + 1))` (npc_mob_pet.c:7677) читает поле как ЗНАКОВЫЙ i16,
	//   аргумент расширяется со знаком до i32, и внутри сравнение идёт с ПОЛНЫМ
	//   32-битным uid: `if (*(_DWORD*)(v8 + 4) == a2)` (npc_mob_pet.c:5549).
	//   ⇒ питомец с `uniqueId >= 32768` НИКОГДА не совпадёт с полем тела и в оригинале
	//   НЕ УДАЛЯЕТСЯ: в окне 43000 остаётся ФАНТОМ съеденного материала до следующего
	//   входа в мир (сервер у себя его уже стёр, `sub_455D90`, GameServer.c:36104).
	//   ЧТО ДЕЛАЛ ПОРТ ДО ВОЛНЫ 8 И ЧЕМ ЭТО БЫЛО ПЛОХО. Порт маскировал СВОЙ uid
	//   (`(S.UniqueId & 0xFFFF) == Low16`) под комментарием «Сопоставляем так же» — то есть
	//   ТИХО ЛЕЧИЛ дефект, называя это дословностью. Без пометки такое расхождение
	//   нельзя: оно меняет состав списка питомцев игрока.
	//   ВТОРАЯ БЕДА: `RemoveAll` сносил ВСЕХ, у кого совпали младшие 16 бит. Эталон
	//   удаляет РОВНО ОДНУ запись и сразу выходит (`mapEraseNode(...); return 1;`,
	//   npc_mob_pet.c:5551-5552). Два питомца с uid 1234 и 67770 дают одинаковые младшие 16 бит,
	//   и порт стёр бы ОБОИХ — игрок потерял бы питомца, которого не отдавал.
	//   КАК СДЕЛАНО СЕЙЧАС:
	//     • удаляется НЕ БОЛЕЕ ОДНОЙ записи на каждый из трёх материалов — как у эталона;
	//     • ПО УМОЛЧАНИЮ дефект ПОЧИНЕН: сопоставление по младшим 16 битам, то есть
	//       фантомов не остаётся и при uid >= 32768;
	//     • КЛЮЧ ОТКАТА `asda.Pet.LegacyUnionUidCompare 1` — дословное поведение оригинала:
	//       сравнение ПОЛНОГО 32-битного uid со ЗНАКОРАСШИРЕННЫМ i16 полем.
	//   На живом сервере разница видна только у питомцев с uid >= 32768; починить само
	//   поле нельзя — оно 16-битное НА ПРОВОДЕ (GameServer.c:36090), это дело серверного патча.
	const bool bLegacyUid = (CVarPetLegacyUnionUidCompare.GetValueOnGameThread() != 0);
	int32 Removed = 0;
	int32 AlreadyBy6111 = 0;
	LastUnionMissing = 0;      // ★ 27.09: счётчик Warning «не найден» этого 6113 (для самопроверки)   // ★ 27.09: из них уже удалены предшествующим 6111 (серверный патч P31)
	for (int32 k = 0; k < 3; ++k)
	{
		const int32 Low16   = (int32)AsdaBytes::RdU16(&B[50 + 2 * k]);
		const int32 Signed16 = (int32)(int16)(uint16)Low16;        // знакорасширение эталона
		if (Low16 == 0xFFFF) { continue; }                         // ctor кладёт −1 = «материала нет»
		// ★★★ 27.09 (ревизия P31/P32, СРЕДН-1) — [ПРОТОКОЛ] СОВМЕСТИМОСТЬ С СЕРВЕРНЫМ ПАТЧЕМ P31.
		//   С P31 сервер ДО 6113 досылает 6111 на каждый съеденный материал, и `OnReleaseResult6111`
		//   уже удалил его по ПОЛНОМУ uid, запомнив в `UnionReleasedUids`. Искать такой материал
		//   в `Slots` ещё раз по младшим 16 битам нельзя: его там уже нет, и «первый совпавший» —
		//   ЧУЖОЙ питомец-двойник (uid ± 65536·k), который пропал бы из окна до перезахода;
		//   попутно сыпалось бы три ложных Warning «не найден». Поэтому: нашли в списке запись
		//   с теми же младшими 16 битами — вычёркиваем её (одна запись на один материал),
		//   считаем материал удалённым и идём к следующему БЕЗ поиска. Режим сверки (ключ
		//   `LegacyUnionUidCompare`) здесь не важен: материал уже убран по полному uid.
		//   Без P31 список пуст (6111 во время юниона не приходит) — дальше всё как раньше.
		{
			const int32 Rel = UnionReleasedUids.IndexOfByPredicate(
				[Low16](int32 R) { return (R & 0xFFFF) == Low16; });
			if (Rel != INDEX_NONE)
			{
				UnionReleasedUids.RemoveAtSwap(Rel);
				++Removed;
				++AlreadyBy6111;
				continue;
			}
		}
		// Поиск ПЕРВОЙ подходящей записи — тот же обход «до первого совпадения»,
		//   что у `petDataFindInvItemById` (обход `listPetInv` с `return 1` на первом же).
		int32 Hit = INDEX_NONE;
		for (int32 i = 0; i < Slots.Num(); ++i)
		{
			const FAsdaPetSlot& S = Slots[i];
			if (S.UniqueId == New.UniqueId) { continue; }          // только что рождённого не трогаем
			const bool bMatch = bLegacyUid ? (S.UniqueId == Signed16)
			                               : ((S.UniqueId & 0xFFFF) == Low16);
			if (bMatch) { Hit = i; break; }
		}
		if (Hit != INDEX_NONE)
		{
			const int32 GoneUid = Slots[Hit].UniqueId;
			Slots.RemoveAt(Hit);
			++Removed;
			if (ActiveUniqueId   == GoneUid) { ActiveUniqueId   = -1; }
			if (SelectedUniqueId == GoneUid) { SelectedUniqueId = -1; }
		}
		else
		{
			++LastUnionMissing;
			UE_LOG(LogAsdaPet, Warning,
				TEXT("[питомцы] 6113: материал с младшими 16 битами uid=%d в списке не найден ")
				TEXT("(режим сверки: %s; поле тела 16-битное — дефект ПД9, лечится только на сервере)"),
				Low16, bLegacyUid ? TEXT("дословная, знакорасширение") : TEXT("исправленная, младшие 16 бит"));
		}
	}
	UE_LOG(LogAsdaPet, Log,
		TEXT("[питомцы] 6113 успех: новый pet=%d порода=%d ур.=%d; съедены uid(16 бит) %d/%d/%d, ")
		TEXT("удалено записей %d (из них раньше, по 6111 от P31: %d), в списке %d"),
		New.UniqueId, New.PetIndex, New.Level,
		(int32)AsdaBytes::RdU16(&B[50]), (int32)AsdaBytes::RdU16(&B[52]), (int32)AsdaBytes::RdU16(&B[54]),
		Removed, AlreadyBy6111, Slots.Num());
	// ★ 27.09 (СРЕДН-1): юнион закончен. Остаток списка (6111 обычного выброса, пришедший, пока
	//   юнион был в полёте) к следующему юниону не относится.
	UnionReleasedUids.Reset();
}

// ── 6119 (25 б) — ответ переименования ────────────────────────────────────────────────────────
void UAsdaPetSubsystem::OnRenameResult6119(const TArray<uint8>& B)
{
	if (B.Num() < 25) { return; }
	const int32 CharId = AsdaBytes::RdI32(&B[0]);
	if (!IsForSelf(CharId, OP_PET_NAME_CHANGE_RESULT)) { return; }
	const int32 Uid = AsdaBytes::RdI32(&B[4]);
	const FString NewName = FAsdaLoginData::DecodeCp1251(B.GetData() + 8, kNameBytes);
	LastRenameResult = (int32)B[24];
	// ★ Волна 2: метка события — окно перерисуется и покажет нотис СВОЕЙ ветки эталона.
	MarkEvent(OP_PET_NAME_CHANGE_RESULT, LastRenameResult, Uid);
	if (LastRenameResult != 1)
	{
		// Коды 2 (брань), 3 (уже меняли), 4 (нет питомца). Состояние не меняется.
		// ⚠ Дефект ПД3 оригинала (три нотиса подряд) — вопрос волны 2, здесь его нет.
		UE_LOG(LogAsdaPet, Log, TEXT("[питомцы] 6119 отказ: код=%d pet=%d"), LastRenameResult, Uid);
		return;
	}
	if (FAsdaPetSlot* S = FindMutable(Uid))
	{
		// ★★★ ПРЕЖНЕЕ ИМЯ СОХРАНЯЕМ ДО ЗАПИСИ НОВОГО. Нотис успеха 101110 двухаргументный —
		//   «[ %s ]'s name has been changed to [ %s ]», и эталон подставляет в него СТАРОЕ имя
		//   (`matchedSlot->gap_C`) и НОВОЕ (`&refineResult[2]`) ПЕРЕД тем, как переписать слот
		//   (item_enhance.c:4983-4986). Окно узнаёт о пакете уже после применения состояния,
		//   поэтому старое имя ему может дать только модель.
		LastRenamePrevName = S->Name;
		S->Name = NewName;                 // эталон: sprintf(matchedSlot->gap_C, "%s", &пакет[8])
		SelectedUniqueId = Uid;            // эталон: v17[154] = matchedSlot
	}
	UE_LOG(LogAsdaPet, Log, TEXT("[питомцы] 6119 успех: pet=%d имя=[%s]"), Uid, *NewName);
}

// ── 6122 (13 б) — ответ расширения ячеек ──────────────────────────────────────────────────────
void UAsdaPetSubsystem::OnSlotUpResult6122(const TArray<uint8>& B)
{
	if (B.Num() < 13) { return; }
	const int32 CharId = AsdaBytes::RdI32(&B[0]);
	if (!IsForSelf(CharId, OP_PET_SLOT_UP_RESULT)) { return; }
	LastSlotUpResult = (int32)B[4];
	// ★ ВОЛНА 6: снимок тела для окна 43000 — вес и ячейка потраченного расширителя.
	//   Поля и доказательства смещений — у объявления `FAsdaPetSlotUpEcho`.
	LastSlotUpEcho = FAsdaPetSlotUpEcho();
	LastSlotUpEcho.Result    = LastSlotUpResult;             // +4 item_enhance.c:5030
	LastSlotUpEcho.NewCount  = (int32)B[5];                  // +5 :5036
	LastSlotUpEcho.ItemBag   = (int32)B[6];                  // +6 :5038/:5040
	LastSlotUpEcho.ItemSlot  = AsdaBytes::RdI16(&B[7]);      // +7 :5038/:5040
	LastSlotUpEcho.WeightCur = AsdaWeightFromWire(AsdaBytes::RdI32(&B[9]));   // ★ 29.10.2026: вес u16;      // +9 :5037 HUD_UpdateMiniWeightBar
	// ★ Волна 2: метка события — окно перерисуется и покажет нотис СВОЕЙ ветки эталона.
	MarkEvent(OP_PET_SLOT_UP_RESULT, LastSlotUpResult, -1);
	if (LastSlotUpResult == 1)
	{
		// Эталон пишет новое число ячеек в `CPetData+0x1E0` БЕЗ КЛАМПА (item_enhance.c:5036:
		//   `v8[120] = (CControl *)BYTE1(v7[1]);`). Диапазон 6..30 живёт ТОЛЬКО на сервере, и там
		//   это не кламп, а СБРОС В 6 при выходе за границы (GameServer.c:112326-112329,
		//   114470-114473). Свой кламп здесь был выдумкой поверх оригинала — убран.
		PetPossessionCount = (int32)B[5];
		UE_LOG(LogAsdaPet, Log, TEXT("[питомцы] 6122 успех: ячеек теперь %d, предмет=(%d,%d), вес=%d"),
			PetPossessionCount, (int32)B[6], AsdaBytes::RdI16(&B[7]), AsdaBytes::RdI32(&B[9]));
	}
	else
	{
		// 2 — нет расширителя, 3 — уже 30. Состояние не меняется.
		UE_LOG(LogAsdaPet, Log, TEXT("[питомцы] 6122 отказ: код=%d"), LastSlotUpResult);
	}
}

// ── 6123 (5 б) — число ячеек при входе в мир и при портале ────────────────────────────────────
void UAsdaPetSubsystem::OnSlotCount6123(const TArray<uint8>& B)
{
	if (B.Num() < 5) { return; }
	const int32 CharId = AsdaBytes::RdI32(&B[0]);
	if (!IsForSelf(CharId, OP_PET_SLOT_COUNT)) { return; }
	// ★ Волна 2: метка события — окно перерисуется и покажет нотис СВОЕЙ ветки эталона.
	MarkEvent(OP_PET_SLOT_COUNT, 0, -1);
	// ★ Эталон применяет число ТОЛЬКО если оно ненулевое (item_enhance.c:5089-5090:
	//   `if (LOBYTE(v4[1])) v5[120] = ...`). Повторяем 1:1: ноль оставляет прежнее значение.
	if (B[4] != 0)
	{
		// Без клампа — ровно как эталон (item_enhance.c:5089-5090), см. приём 6122.
		PetPossessionCount = (int32)B[4];
	}
	UE_LOG(LogAsdaPet, Log, TEXT("[питомцы] 6123: сервер сообщил %d ячеек, применено %d"),
		(int32)B[4], PetPossessionCount);
}

// ── 6149 (110 б) — ответ лимит-брейка ─────────────────────────────────────────────────────────
void UAsdaPetSubsystem::OnLimitBreakResult6149(const TArray<uint8>& B)
{
	if (B.Num() < 110) { return; }
	// ★★★ БАЙТ 0 ЭТОГО ТЕЛА — `petUniqueId`, А НЕ `charId` (ревизия 2026-09-21, перепроверено
	//   построчно с обеих сторон). Доказательства:
	//     • СЕРВЕР: `recvDBPetMaxLevelUp` кладёт в тело `*(_DWORD*)v14 = <dword ответа БД +11>`
	//       (GameServer.c:36840), а туда он попал из тела 6148: запрос 2464 собирается на
	//       GameServer.c:36774 как `v35 = v25[0]`, где `v25[0]` = первое поле 6148 = petUid
	//       (`sub_45BAD0`, GameServer.c:37125 — ctor `petUid@0`). charId сервер в этом ответе НЕ
	//       передаёт: он сверяет его у себя (`if (*(void**)v12->gap_16B != v13[0].vftable) return`,
	//       GameServer.c:36845) и до клиента не доносит.
	//     • КЛИЕНТ: `pet_OnRecvItemOpResult` делает `itemRecord[1] = itemOpPayload[0]`
	//       (npc_mob_pet.c:7731) — то есть кладёт байт 0 в `CPetSlot+0x04`, а это поле `uniqueId`.
	//       Сверки charId в обработчике НЕТ ВООБЩЕ (сплошной поиск по телу :7691-7900 пуст).
	//   ⇒ звать здесь `IsForSelf` НЕЛЬЗЯ: на живом сервере `SelectedCharId != 0`, сравнение
	//     `petUid == charId` почти всегда ложно, и лимит-брейк не применялся бы НИКОГДА.
	//   ⇒ прежняя оговорка «тело 6149 не содержит uniqueId» и запасной путь «применить к
	//     выбранному» были ошибкой — убраны.
	const int32 Uid = AsdaBytes::RdI32(&B[0]);
	LastLimitBreakResult = (int32)B[48];
	// ★ ВОЛНА 4: снимок тела для окна 44200. Поля и доказательства смещений — у объявления
	//   `FAsdaPetLimitBreakEcho`. Снимается ДО разбора кода и на ЛЮБОМ коде: эталон читает
	//   те же байты в ветках 1..4, а окно по `Result` само решает, что применять.
	LastLimitBreakEcho = FAsdaPetLimitBreakEcho();
	LastLimitBreakEcho.Result      = LastLimitBreakResult;
	LastLimitBreakEcho.UsedSlot    = AsdaBytes::RdI16(&B[45]);   // npc_mob_pet.c:7770-7774
	LastLimitBreakEcho.UsedBag     = (int32)B[47];               // :7772 / :7774 (2 = расходники, 1 = сумка)
	LastLimitBreakEcho.WeightCur   = AsdaWeightFromWire(AsdaBytes::RdI32(&B[49]));   // ★ 29.10.2026: вес u16;   // :7777 HUD_UpdateMiniWeightBar
	LastLimitBreakEcho.RetItemCode = AsdaBytes::RdI32(&B[53]);   // :7834 / :7850 — ветвление нотисов
	LastLimitBreakEcho.RetItemBag  = (int32)B[57];               // :7781 / :7786 Item+4
	LastLimitBreakEcho.RetItemSlot = AsdaBytes::RdI16(&B[58]);   // :7781 / :7786 Item+5
	LastLimitBreakEcho.RetItemQty  = AsdaBytes::RdI32(&B[62]);   // :7778 / :7788 Item+9
	// ★★★ ВОЛНА 8 (БЛОКЕР-2): ЗАПИСЬ ПРЕДМЕТА ЦЕЛИКОМ, 57 б с +53. Ровно тот кусок, который эталон
	//   кладёт в ячейку одним `memcpy(…, (char*)&itemOpPayload[13] + 1, 0x39u)` (npc_mob_pet.c:7795).
	//   Длина тела проверена выше (>= 110), значит +53+57 = 110 <= 110 — запись укладывается
	//   в тело ровно встык, выход за буфер невозможен.
	LastLimitBreakEcho.RetItemRec57.SetNumUninitialized(57);
	FMemory::Memcpy(LastLimitBreakEcho.RetItemRec57.GetData(), B.GetData() + 53, 57);
	// ★ Волна 2: метка события — окно перерисуется и покажет нотис СВОЕЙ ветки эталона.
	MarkEvent(OP_PET_LIMITBREAK_RESULT, LastLimitBreakResult, Uid);
	// Исходы 1..4 меняют питомца; 0 и 5..16 — отказы, состояние не трогают
	//   (npc_mob_pet.c:7718-7760: обновление идёт только в ветках 1..4).
	if (LastLimitBreakResult >= 1 && LastLimitBreakResult <= 4)
	{
		FAsdaPetSlot Upd;
		Upd.UniqueId = Uid;
		ReadPetRecord(B, 4, Upd);                       // PetRecord обновлённого питомца
		// Эталон здесь зовёт `petDataSelectInvItemById` (npc_mob_pet.c:7745): та стирает запись с
		//   тем же uid и кладёт новую, то есть ровно наш `AddOrUpdate`. Признак «призван» эталон
		//   при этом обнуляет (`LOBYTE(itemRecord[0]) = 0`, :7730) — повторяем.
		const FAsdaPetSlot* Old = FindByUniqueId(Uid);
		const bool bWasActive = Old ? Old->bIsActive : false;
		Upd.bIsActive = false;
		AddOrUpdate(Upd);
		if (bWasActive && ActiveUniqueId == Uid)
		{
			// Питомец был призван, а запись пришла с погашенным признаком: у оригинала это
			//   недостижимо (окно 44200 не открывается при призванном питомце, гейт 109975
			//   ui_core.c:90614), но если сервер такое пришлёт — состояние обязано сойтись.
			ActiveUniqueId = -1;
			UE_LOG(LogAsdaPet, Warning,
				TEXT("[питомцы] 6149 пришёл на ПРИЗВАННОГО pet=%d — признак снят (у оригинала гейт 109975 этого не допускает)"), Uid);
		}
		UE_LOG(LogAsdaPet, Log,
			TEXT("[питомцы] 6149 исход=%d: pet=%d порода=%d ур.=%d предел=%d опыт=%d (зелье=(%d,%d), вес=%d)"),
			LastLimitBreakResult, Uid, Upd.PetIndex, Upd.Level, Upd.MaxLevel, Upd.Exp,
			(int32)B[47], AsdaBytes::RdI16(&B[45]), AsdaBytes::RdI32(&B[49]));
	}
	else
	{
		UE_LOG(LogAsdaPet, Log, TEXT("[питомцы] 6149 отказ: код=%d pet=%d"), LastLimitBreakResult, Uid);
	}
}

// ── 6120 — приходит через УЖЕ существующий приёмник сети (второго не заводим) ─────────────────
void UAsdaPetSubsystem::OnPeerPet6120(int32 CharId, int32 PetTypeId, FString PetName)
{
	if (CharId != SelfCharId() || SelfCharId() == 0) { return; }   // чужие — дело мира
	SelfPeerPetType = PetTypeId;
	SelfPeerPetName = PetName;
	// Сверка мира и модели: если сервер показал питомца, а у нас никто не «призван» (или наоборот),
	//   это рассинхрон, и его надо видеть в логе, а не ловить по пустому окну.
	const FAsdaPetSlot* Act = GetActivePet();
	const bool bWorldHas = (PetTypeId >= 0);
	if (bWorldHas != (Act != nullptr))
	{
		UE_LOG(LogAsdaPet, Warning,
			TEXT("[питомцы] рассинхрон 6120 и списка: в мире %s (тип=%d), в модели %s"),
			bWorldHas ? TEXT("ЕСТЬ") : TEXT("нет"), PetTypeId,
			Act ? TEXT("ЕСТЬ") : TEXT("нет"));
	}
	else if (Act && Act->PetIndex != PetTypeId)
	{
		UE_LOG(LogAsdaPet, Warning, TEXT("[питомцы] 6120: в мире порода %d, в модели %d"),
			PetTypeId, Act->PetIndex);
	}
}

// ═══════════════════════════════════ ОТПРАВКА ══════════════════════════════════════════════════
bool UAsdaPetSubsystem::EnqueueRequest(const FAsdaPetPendingRequest& Req)
{
	Pending.Add(Req);
	return true;
}

bool UAsdaPetSubsystem::SendRequest(const FAsdaPetPendingRequest& R)
{
	UAsdaNetSubsystem* N = Net();
	if (!N) { return false; }
	const int32 Me = N->SelectedCharId;
	bool bOk = false;
	switch (R.Opcode)
	{
		case OP_PET_CALL_REQ:
			bOk = N->SendPetCall6100(Me, R.PetUniqueId);
			break;
		case OP_PET_INCUBATE_REQ:
			bOk = N->SendPetIncubate6102(Me, R.Bag, R.Slot, R.Bag2, R.Slot2, R.Bag3, R.Slot3);
			break;
		case OP_PET_FEED_REQ:
			bOk = N->SendPetFeed6108(Me, R.PetUniqueId, R.Bag, R.Slot);
			break;
		case OP_PET_RELEASE_REQ:
			bOk = N->SendPetRelease6110(Me, R.PetUniqueId);
			break;
		case OP_PET_UNION_REQ:
			// ★ 27.09 (СРЕДН-1): новый юнион — список «удалено 6111» начинается с чистого листа, чтобы
			//   uid, выброшенные ДО этого запроса обычным 6110, не приняли за материалы этого 6113.
			UnionReleasedUids.Reset();
			bOk = N->SendPetUnion6112(Me, R.UnionIndex,
				R.Materials.IsValidIndex(0) ? R.Materials[0] : -1,
				R.Materials.IsValidIndex(1) ? R.Materials[1] : -1,
				R.Materials.IsValidIndex(2) ? R.Materials[2] : -1);
			break;
		case OP_PET_NAME_CHANGE_REQ:
			bOk = N->SendPetRename6118(Me, R.PetUniqueId, R.NewName);
			break;
		case OP_PET_SLOT_UP_REQ:
			bOk = N->SendPetSlotUp6121(Me, R.Bag, R.Slot);
			break;
		case OP_PET_LIMITBREAK_REQ:
			bOk = N->SendPetLimitBreak6148(R.PetUniqueId, R.Bag, R.Slot, R.PremiumSlot);
			break;
		default:
			UE_LOG(LogAsdaPet, Error, TEXT("[питомцы] в очереди неизвестный опкод %d"), R.Opcode);
			return false;
	}
	if (bOk) { TxByOpcode.FindOrAdd(R.Opcode) += 1; }
	return bOk;
}

// ★★★ ВОЛНА 8 (пункт 1 ревизии): ЕДИНАЯ ТОЧКА СНЯТИЯ ЗАСОВА «6100 В ПОЛЁТЕ».
//   Путей сюда пять: приход 6101, отказ очереди, сброс сессии, закрытие и
//   открытие окна 43000, таймаут. Правило «снять таймер вместе с признаком»
//   живёт ТОЛЬКО здесь (закон `port-shared-points`).
void UAsdaPetSubsystem::ClearCallRequestInFlight(const TCHAR* Why)
{
	if (!CallLatch.IsArmed()) { return; }
	CallLatch.Disarm();
	if (Why)
	{
		UE_LOG(LogAsdaPet, Warning,
			TEXT("[питомцы] засов «6100 в полёте» снят: %s — кнопки 43065/43066 снова живы"), Why);
	}
}

bool UAsdaPetSubsystem::RequestCallPet(int32 PetUniqueId)
{
	// Гейт ОРИГИНАЛА и единственный: повторный запрос, пока не пришёл 6101 (item_enhance.c:4349).
	if (CallLatch.IsArmed())
	{
		UE_LOG(LogAsdaPet, Log, TEXT("[питомцы] 6100 не шлю: предыдущий запрос ещё в полёте"));
		return false;
	}
	// Эталон ставит признак ДО отправки (:4351). ★ ВОЛНА 8: вместе с признаком взводится таймаут ожидания ответа 6101
	//   (★ S2, В2-A: срок — от момента взвода стенными часами, ключ `asda.Pet.SendTimeoutSec` читается на тике).
	// ★★★ ВОЛНА 9: ключ эха. Ответ 6101 несёт petUid в `+5..8`
	//   (`GameServer.c:34457`, `:34476`, `:34492`) — по нему поздний ответ на ПРЕДЫДУЩИЙ 6100
	//   отличается от ответа на текущий. Ключ переживает снятие засова намеренно.
	CallLatch.Arm(OP_PET_CALL_REQ, AsdaPetUi::EchoKeyPet(PetUniqueId));
	FAsdaPetPendingRequest R;
	R.Opcode = OP_PET_CALL_REQ;
	R.PetUniqueId = PetUniqueId;
	return EnqueueRequest(R);
}

bool UAsdaPetSubsystem::RequestIncubate(int32 CatalystBag, int32 CatalystSlot, int32 EggBag, int32 EggSlot,
                                        int32 BoosterBag, int32 BoosterSlot)
{
	FAsdaPetPendingRequest R;
	R.Opcode = OP_PET_INCUBATE_REQ;
	R.Bag = CatalystBag;  R.Slot = CatalystSlot;   // предмет 1 — subType 107
	R.Bag2 = EggBag;      R.Slot2 = EggSlot;       // предмет 2 — subType 109
	R.Bag3 = BoosterBag;  R.Slot3 = BoosterSlot;   // предмет 3 — необязателен
	return EnqueueRequest(R);
}

bool UAsdaPetSubsystem::RequestFeed(int32 ItemBag, int32 ItemSlot, bool bFromWindowSlot)
{
	// ★★★ ДВА ПУТИ ОРИГИНАЛА (network.c:25729-25757), гейты РАЗНЫЕ. Раскладка — в объявлении.
	//   Общее начало эталона: `if (itemSlot)` — без предмета не делается ничего.
	if (ItemSlot < 0)
	{
		UE_LOG(LogAsdaPet, Log, TEXT("[питомцы] 6108 не шлю: предмет не задан (эталон: `if (itemSlot)`)"));
		return false;
	}

	int32 PetUniqueId = -1;
	if (!bFromWindowSlot)
	{
		// Путь «из сумки»: нужен ПРИЗВАННЫЙ, иначе нотис 101105 (network.c:25862).
		if (!HasActivePet())
		{
			bFeedNeedsActivePetNotice = true;
			UE_LOG(LogAsdaPet, Log,
				TEXT("[питомцы] 6108 (из сумки) не шлю: нет призванного питомца — нотис 101105"));
			return false;
		}
		PetUniqueId = ActiveUniqueId;
	}
	else
	{
		// Путь «кнопкой окна 43000»: нужен ВЫБРАННЫЙ; при его отсутствии оригинал молчит.
		if (SelectedUniqueId == -1 || !FindByUniqueId(SelectedUniqueId))
		{
			UE_LOG(LogAsdaPet, Log,
				TEXT("[питомцы] 6108 (кнопка окна) не шлю: питомец не выбран (оригинал молчит, нотиса нет)"));
			return false;
		}
		PetUniqueId = SelectedUniqueId;
	}

	FAsdaPetPendingRequest R;
	R.Opcode = OP_PET_FEED_REQ;
	R.PetUniqueId = PetUniqueId;
	R.Bag = ItemBag;
	R.Slot = ItemSlot;
	return EnqueueRequest(R);
}

bool UAsdaPetSubsystem::ConsumeFeedNeedsActivePetNotice()
{
	const bool b = bFeedNeedsActivePetNotice;
	bFeedNeedsActivePetNotice = false;
	return b;
}

bool UAsdaPetSubsystem::RequestRelease(int32 PetUniqueId)
{
	FAsdaPetPendingRequest R;
	R.Opcode = OP_PET_RELEASE_REQ;
	R.PetUniqueId = PetUniqueId;
	return EnqueueRequest(R);
}

bool UAsdaPetSubsystem::RequestUnion(int32 UnionIndex, int32 Mat0, int32 Mat1, int32 Mat2)
{
	FAsdaPetPendingRequest R;
	R.Opcode = OP_PET_UNION_REQ;
	R.UnionIndex = UnionIndex;
	R.Materials.Init(-1, 3);       // фиксированная длина: только Init
	R.Materials[0] = Mat0;
	R.Materials[1] = Mat1;
	R.Materials[2] = Mat2;
	return EnqueueRequest(R);
}

bool UAsdaPetSubsystem::RequestRename(int32 PetUniqueId, const FString& NewName)
{
	FAsdaPetPendingRequest R;
	R.Opcode = OP_PET_NAME_CHANGE_REQ;
	R.PetUniqueId = PetUniqueId;
	// Поле ввода 43071 ограничено 14 символами (`ctrlEditSetTextLength(14)`, item_enhance.c:4094).
	// ★ [ПОРТ] 08.10.2026: лимит в БАЙТАХ провода (EM_LIMITTEXT оригинала — байты; поле 6118 — char[16] без нуля); маркер
	//   cp1256 бесплатен, поэтому арабская кличка — те же 14 букв.
	R.NewName = FAsdaLoginData::TruncateToWireLimit(NewName, kNameMaxChars, 16);
	return EnqueueRequest(R);
}

bool UAsdaPetSubsystem::RequestSlotUp(int32 ItemBag, int32 ItemSlot)
{
	// ★ ВОЛНА 6: гейт эталона. `netSendVerifiedPacket6121@0xDCB360` начинается с `if (srcCmd)`
	//   (network.c:26181) — без записи предмета не делается ничего. Питомец при этом НЕ нужен:
	//   пакет несёт только charId и пару «сумка/ячейка» (:26188-26190), сервер сам ищет предмет
	//   и сам решает (`Recv_PetPossessionCountUp_Req`, GameServer.c:36397). Нотиса у этого гейта
	//   в оригинале нет — отказ приходит кодами 2/3 ответа 6122.
	if (ItemSlot < 0)
	{
		UE_LOG(LogAsdaPet, Log, TEXT("[питомцы] 6121 не шлю: предмет не задан (эталон: `if (srcCmd)`)"));
		return false;
	}
	FAsdaPetPendingRequest R;
	R.Opcode = OP_PET_SLOT_UP_REQ;
	R.Bag = ItemBag;
	R.Slot = ItemSlot;
	return EnqueueRequest(R);
}

bool UAsdaPetSubsystem::RequestLimitBreak(int32 PetUniqueId, int32 ItemBag, int32 ItemSlot, int32 PremiumSlot)
{
	FAsdaPetPendingRequest R;
	R.Opcode = OP_PET_LIMITBREAK_REQ;
	R.PetUniqueId = PetUniqueId;
	R.Bag = ItemBag;
	R.Slot = ItemSlot;
	R.PremiumSlot = PremiumSlot;
	return EnqueueRequest(R);
}
