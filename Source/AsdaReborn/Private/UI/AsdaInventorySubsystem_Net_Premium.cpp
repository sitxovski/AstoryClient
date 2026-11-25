// Copyright (c) 2026 @sitxovski (Discord)
// AsdaInventorySubsystem_Net_Premium.cpp — ★ R3-c 12.11.2026 (разрез AsdaInventorySubsystem_Net.cpp, дословный перенос): премиум-предметы и ответ функции предмета.
//   Общее для частей — в AsdaInventorySubsystem_Net_Private.h; шапка и реверс — в AsdaInventorySubsystem_Net.cpp.
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "UI/AsdaSkillTreeResetSubsystem.h"   // ★ 25.10.2026: ключ диалогов сброса навыков (гейт 196/197)
#include "AsdaPacketWriter.h"   // ★ U6 (В1 дедупа №2): 15 байт слотов-приёмников 6096 через общий писатель
#include "UI/AsdaSelfStoreSubsystem.h"   // билет премиум-лавки (5451 подтип 65) → 6300 (волна Л1 торговли)
#include "UI/AsdaPetCageSubsystem.h"      // ★ кнопка 454 — окно питомца 43000 (волна 2)
#include "UI/AsdaDisassemblySubsystem.h"    // ★ кнопка 453 — окно разборки снаряжения 32000 (волна 2-А, 06.10.2026)
#include "UI/AsdaSowelSubsystem.h"         // ★ ветка САМОЦВЕТОВ правой панели (430…437 / 441…446 / 495 / 499)
// ★ обмен по таблице событий (двойной клик по предмету subType 202 → 6098)   ★ R3: комментарий снятого включения "Npc/AsdaNpcDialogSubsystem.h" (файл им не пользовался) — сохранён
#include "Text/AsdaTextSubsystem.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "AsdaInventorySubsystem_Net_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaInventorySubsystem_Net*.cpp

// ★ 28.10.2026 [ПОРТ] (решение владельца): плывущему — только ход и предметы (тело — AsdaPlayerCharacter_Move.cpp).
bool AsdaSwimActionLocked(const class AAsdaPlayerCharacter* Ch);

// ★★ [ИСПРАВЛЕНИЕ] 24.10.2026: формат нотиса отката 5451 код 8/0x0C (см. ApplyFunctionResult).
//   1 = «<8114 без хвостового двоеточия>: N <6824>», остаток неизвестен — 9558; 0 = прежняя склейка «<8114>N <6824>» / голое 8114.
// ★ 08.11.2026 [ИСПРАВЛЕНИЕ] пачка 19, баг 9: зелье уровня (Plus Package, подтип 120: 14, 475, 476, 489, 490, 536, 537, 538, 544) при действующем
//   прежнем не применяется. В оригинале запрета нет ни у клиента (`itemUseDispatch@0xA105A0`, ветка Flag102 шлёт 5450 сразу), ни у сервера
//   (`Char_FunctionItem_Use@0x4ED780` case 0x78), ни у базы: `UPPlusPackageitemUse` тратит предмет и ПЕРЕЗАПИСЫВАЕТ CharPlusPackage (срок с нуля,
//   30 дней можно затереть 7-дневным). Нотиса в оригинале нет: строка 12849 «действует эффект того же типа». Авторитет — сервер (патч предложен отдельно).
static ASDA_DEV_CVAR(int32, CVarPlusPackOnce,
	TEXT("asda.Item.PlusPackOnce"), 1,
	TEXT("1 = зелье уровня (подтип 120) не применяется, пока действует прежнее (нотис 12849); 0 = как оригинал, 5450 уходит всегда."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarCooldownNoticeFormat,
	TEXT("asda.Inven.CooldownNoticeFormat"),
	1,
	TEXT("Нотис отката 5451 код 8/0x0C: 1 = «Откат: N сек.» / «Cooling time remains: N Seconds», остаток неизвестен — "
	     "фраза 9558 «Время до перезарядки.»; 0 = прежняя склейка шаблона 8114 с числом (в EN без разделителя) и голое 8114."),
	ECVF_Default);

// Шаблон 8114 без хвостовых пробелов и двоеточия: RU «Откат: » → «Откат», EN «Cooling time remains» — без изменений.
static FString TrimCooldownTemplate(const FString& Tmpl)
{
	FString S = Tmpl;
	S.TrimEndInline();
	while (S.EndsWith(TEXT(":")) || S.EndsWith(TEXT("："))) { S.LeftChopInline(1); S.TrimEndInline(); }
	return S;
}

// ★ 08.11.2026 [ОРИГИНАЛ] (пачка 16, баг 1): нотис срока транспорта на успешный 5451 (подтип 15) — разбор в `ApplyFunctionResult`.
static ASDA_DEV_CVAR(int32, CVarMountDaysNotice,
	TEXT("asda.Mount.DaysNotice"), 1,
	TEXT("[ОРИГИНАЛ 08.11.2026] 1 = после посадки на транспорт с ограниченным сроком плашка 20230 «Срок действия этого ТС - N дн.» "
	     "(меньше суток — 13227), как оригинал; 0 = прежний порт (без нотиса)."),
	ECVF_Default);
static bool AsdaMountDaysNoticeOn() { return CVarMountDaysNotice.GetValueOnGameThread() != 0; }

// ★ 08.11.2026 [ОРИГИНАЛ] (пачка 16, баг 3): ключ `asda.Mount.BusyGate` — определение в `AsdaInventorySubsystem_Use.cpp`.
bool AsdaPremiumBusyGateOn();

// ───────────────────────────── C. ПРЕМИУМ / ФУНКЦИОНАЛЬНЫЕ ПРЕДМЕТЫ ────────────────────────────
//
// ★★★ ДИСПЕТЧЕР ПРИМЕНЕНИЯ — порт `itemUseDispatch@0xA105A0` / `itemUseDispatch_C20@0xA10C20`
//   (item_inventory.c:3231-3394 и :3395-3520). Ветвление 1:1:
//     нет записи `special_itemlist`                       → выход;
//     предмет уже активен (`charContainsRecordById`)       → выход МОЛЧА, без пакета;
//     `Type103 != 0`: только `Type103 == 3 && Flag101 != 2` → 5450;
//     иначе `Flag101 == 2`: подтип 15 (транспорт) / 25 (спутник) — ТОГГЛ (см. ниже), прочие молча;
//     иначе `Flag102 == 1`                                  → 5450 (в т.ч. со строкой @55).
//   ⚠ `Type103` во ВСЕХ 558 строках данных равен 0 — ветка `== 3` в этой сборке недостижима.
//   Это НЕ смещение колонки: раскладка проверена вторым методом — `Flag101 == 2` даёт ровно 219
//   строк, и множество `SubType ∈ {15, 25}` — ровно те же 219 (214 + 5), совпадение полное.
bool UAsdaInventorySubsystem::UsePremiumItem(int32 ContainerId, int32 CellIndex, const FString& Message)
{
	const FAsdaItemInstance* Src = GetCell(ContainerId, CellIndex);
	if (!Src || Src->IsEmpty()) { return false; }
	// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (`asda.Duel.LoserLock`): пока свой персонаж доигрывает жест поражения дуэли (51) — предметы не
	//   применяются, МОЛЧА (у оригинала гейта нет — жест обрывался; разбор у `AsdaDuelLoserLocked`).
	{
		const UAsdaWorldSubsystem* DlW = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (DlW && AsdaDuelLoserLocked(Cast<AAsdaPlayerCharacter>(DlW->LocalPlayerActor.Get())))
		{
			UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] %s: жест поражения дуэли — предмет не применён"), ANSI_TO_TCHAR(__FUNCTION__));
			return false;
		}
	}
	// ★ ГЕЙТ КОНТЕЙНЕРА (добавлен при перепроверке). Премиум-канал работает ТОЛЬКО с сумкой:
	//   • клиент — `CItemManager_UseInventoryItem@0xA0FDF0` (`item_inventory.c:2929`) адресует ячейку
	//     жёстко как `&g_InventoryBag + 57 * slot`, контейнер параметром вообще не приходит;
	//   • сервер — `Char_FunctionItem_Use@0x4ED780` резолвит `ItemPos` вызовом
	//     `sub_4D61E0((CUser*)this, arg0, 1, v88[3], a5, nullptr)` с КОНСТАНТОЙ 1 (сумка).
	//   Если сюда попадёт ячейка контейнера 2, сервер применит предмет из СУМКИ с тем же номером
	//   ячейки — то есть спишет чужой предмет. Поэтому отказываем явно, а не «на авось».
	if (ContainerId != 1)
	{
		UE_LOG(LogAsdaInven, Warning,
			TEXT("[премиум] предмет %d в контейнере %d, ячейка %d: канал 5450 работает только с сумкой ")
			TEXT("(эталон `item_inventory.c:2929`, сервер `sub_4D61E0(..., 1, ...)`) — пакет не отправляем"),
			Src->ItemCode, ContainerId, CellIndex);
		return false;
	}
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!Net || !IT) { return false; }
	const int32 Code = Src->ItemCode;
	FAsdaSpecialItemlistRow Sp;
	if (!IT->GetSpecialItem(Code, Sp))
	{
		UE_LOG(LogAsdaInven, Log, TEXT("[премиум] предмет %d: записи special_itemlist нет — эталон здесь выходит"), Code);
		return false;
	}
	// Байты 101/102/103 отдельных колонок в строке не имеют — читаем из сырой записи общей точкой.
	const int32 Flag101 = AsdaBytes::HexByteAt(Sp.DataHex, 101);
	const int32 Flag102 = AsdaBytes::HexByteAt(Sp.DataHex, 102);
	const int32 Type103 = AsdaBytes::HexByteAt(Sp.DataHex, 103);
	const int32 SubType = Sp.SubType;

	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U5b): свой userNum — `AsdaSelf::GetUserIdxOr`; до 4003 (номер неизвестен) пакет НЕ шлём (было: 0 — номер чужого игрока, сервер выдаёт его штатно); ключ отката `asda.Self.StrictUnknownNum 0` — сырой номер.
	const int32 SelfNum = AsdaSelf::GetUserIdxOr(Net, -1);
	if (SelfNum < 0)
	{
		UE_LOG(LogAsdaInven, Warning, TEXT("[премиум] предмет %d: 5450 не отправлен — свой userNum ещё неизвестен (4003 не пришёл)"), Code);
		return false;
	}
	TArray<int32> Targets;
	Targets.Add(SelfNum);   // эталон: элемент 0 списка целей — свой userNum
	// ★ 28.10.2026 [ПОРТ] (решение владельца): транспорт (подтип 15) плывущему не вызвать — МОЛЧА. Верхом персонаж не «плывёт»
	//   (`IsSwimming` у всадника ложно), поэтому «слезть» в воде остаётся. Ключ `asda.Swim.ActionGate`.
	if (SubType == 15)
	{
		const UAsdaWorldSubsystem* SwW = GI->GetSubsystem<UAsdaWorldSubsystem>();
		if (SwW && AsdaSwimActionLocked(Cast<AAsdaPlayerCharacter>(SwW->LocalPlayerActor.Get())))
		{
			UE_LOG(LogAsdaInven, Log, TEXT("[премиум] предмет %d (транспорт): персонаж плывёт — 5450 не отправлен"), Code);
			return false;
		}
	}

	auto Send5450 = [&](const TCHAR* Why) -> bool
	{
		const bool bOk = Net->SendFunctionItemUse5450(SelfNum, Code, CellIndex,
			/*PosX*/ -1, /*PosZ*/ -1, /*ClickByte*/ 0, /*Value*/ -1, Targets, Message);
		UE_LOG(LogAsdaInven, Log, TEXT("[премиум] предмет %d (подтип %d): %s → 5450 %s"),
			Code, SubType, Why, bOk ? TEXT("отправлен") : TEXT("НЕ отправлен"));
		if (bOk) { bActionPending = true; ActionSentAtS = FPlatformTime::Seconds(); }
		return bOk;
	};

	// Транспорт (15) и спутник (25) — ТОГГЛ: состояние уже висит ⇒ 5455 «снять», иначе 5450.
	//   Признак «висит» берём из общего списка состояний (`AddType2`, тип 2) — того же, куда его
	//   кладёт приём 5453; своей копии не заводим ([[port-shared-points]]).
	if (Flag101 == 2 && (SubType == 15 || SubType == 25))
	{
		const FAsdaEffectEntry* Active = nullptr;
		if (UAsdaEffectsSubsystem* Eff = GI->GetSubsystem<UAsdaEffectsSubsystem>())
		{
			Active = Eff->FindByTypeSub(SelfNum, SubType, 2);
		}
		// ★★★ ДОБАВЛЕНО ПРИ ПОВТОРНОЙ СВЕРКЕ (второй заход, 2026-08-13) — ТРОТТЛ И КОД ТРАНСПОРТА.
		//   Тело `itemUseDispatch@0xA105A0` (`item_inventory.c:3299-3350`) для подтипа 15 (транспорт):
		//       если состояние (15, 2) ЕСТЬ:
		//           if (timeGetTime() − g_dwPlayerActionTimer > 0xC8) {        // 200 мс
		//               g_dwPlayerActionTimer = timeGetTime();
		//               netSendSimpleCmd_5455(localPlayer2[1].mountItemId);    // ★ КОД АКТИВНОГО МАУНТА
		//               charRemoveBuffBySkillId(localPlayer2->gap_0, 0xF); }   // снять баф 15 локально
		//       иначе:
		//           if (timeGetTime() − g_dwPlayerActionTimer > 0xBB8) { … }   // 3000 мс, затем 5450
		//   Для подтипа 25 (спутник) троттла НЕТ ни в одной ветке — это проверено по тому же телу.
		//   Раньше порт (а) не имел троттла вовсе и (б) слал в 5455 код НАЖАТОГО предмета, а не
		//   код НАДЕТОГО транспорта: если в сумке лежат два разных маунта, слезть вторым было
		//   нельзя — сервер не нашёл бы активной записи по чужому коду.
		const double NowS = FPlatformTime::Seconds();
		const bool bThrottled15 = (SubType == 15);
		if (Active)
		{
			if (bThrottled15 && PremiumActionAtS > 0.0 && (NowS - PremiumActionAtS) * 1000.0 <= 200.0)
			{
				UE_LOG(LogAsdaInven, Log,
					TEXT("[премиум] подтип 15: «слезть» отклонено троттлом 200 мс (эталон `itemUseDispatch@0xA105A0`)"));
				return false;
			}
			// Код берём из САМОЙ записи состояния — это и есть `localPlayer2[1].mountItemId`
			//   (его туда кладёт приём 5453, `AddType2(..., ItemCode)`); нет — падаем на нажатый.
			const int32 SendCode = (Active->ItemCode > 0) ? Active->ItemCode : Code;
			if (bThrottled15) { PremiumActionAtS = NowS; }
			const bool bOk = Net->SendFunctionItemToggle5455(SendCode);
			if (bOk && SubType == 15)
			{
				// Эталон: `charRemoveBuffBySkillId(localPlayer2->gap_0, 0xF)` — локально снять баф 15
				//   не дожидаясь пакета (сервер подтвердит своим 5454).
				if (UAsdaEffectsSubsystem* Eff = GI->GetSubsystem<UAsdaEffectsSubsystem>())
				{
					Eff->RemoveBySkillState(SelfNum, /*Id*/ 15, /*Type*/ 2);
				}
			}
			UE_LOG(LogAsdaInven, Log,
				TEXT("[премиум] предмет %d (подтип %d) уже активен → 5455 кодом %d %s"),
				Code, SubType, SendCode, bOk ? TEXT("отправлен") : TEXT("НЕ отправлен"));
			return bOk;
		}
		if (bThrottled15 && PremiumActionAtS > 0.0 && (NowS - PremiumActionAtS) * 1000.0 <= 3000.0)
		{
			UE_LOG(LogAsdaInven, Log,
				TEXT("[премиум] подтип 15: «сесть» отклонено троттлом 3000 мс (эталон `itemUseDispatch@0xA105A0`)"));
			return false;
		}
		if (bThrottled15) { PremiumActionAtS = NowS; }
		// ★ 08.11.2026 [ОРИГИНАЛ] (пачка 16, баг 3, ключ `asda.Mount.BusyGate`): гейт состояния самого тумблера, который порт
		//   пропустил (`itemUseDispatch@0xA105A0`, AsdaStory_game.c:75359 и :75384). «Сесть» (15) — только если состояние действия
		//   своего (+47551) не 7/8/9 (садится/сидит/встаёт) и не 40/43 (копает), и таймер автолопаты (`sub_9353AE` = `get_o6`
		//   +0x18 «активен», thunk_map.tsv) не взведён; спутник (25) — те же 40/43 и лопата. Отказ МОЛЧА; троттл 3 с эталон
		//   уже взвёл выше (таймер ставится до проверки состояния).
		if (AsdaPremiumBusyGateOn())
		{
			const UWorld* SgW = GI->GetWorld();
			const AAsdaPlayerController* SgPC = SgW ? Cast<AAsdaPlayerController>(SgW->GetFirstPlayerController()) : nullptr;
			const int32 St = SgPC ? SgPC->KbEtalonActionState() : 0;   // эталонное +47551 своего
			const bool bStateBad = (St == 40 || St == 43) || (SubType == 15 && (St == 7 || St == 8 || St == 9));
			if (bStateBad || AutoDig.bActive)
			{
				UE_LOG(LogAsdaInven, Log, TEXT("[премиум] предмет %d (подтип %d): состояние действия %d, автолопата %d — 5450 не отправлен (молча, как оригинал)"),
					Code, SubType, St, AutoDig.bActive ? 1 : 0);
				return false;
			}
		}
		return Send5450(TEXT("включение"));
	}
	if (Type103 == 3 && Flag101 != 2) { return Send5450(TEXT("одноразовое применение (Type103==3)")); }
	// ★★★ ЧЕСТНО О НЕПОРТИРОВАННОМ (найдено при повторной сверке, второй заход 2026-08-13).
	//   Ветка `Flag102 == 1` эталона (`item_inventory.c:3357-3384`) содержит ЕЩЁ ДВА гейта,
	//   которых здесь нет, — оба опираются на поля персонажа, которых в порту не существует:
	//     1) `itemCode == 196 || itemCode == 197`: требуется `localPlayer3->gap_43B[10]`,
	//        иначе нотис **12848** и выход БЕЗ пакета;
	//     2) `itemTable2Rec->subType == 48`: требуется
	//        `effectSlots[112].field_8 == 6 || == 29 || (effectSlots[112].field_C & 6) != 0`,
	//        иначе нотис **12856** и выход БЕЗ пакета.
	//   Это НЕ упрощение, а отсутствующий источник данных: оба поля — состояние персонажа, которое
	//   порт пока не ведёт. Авторитет всё равно у сервера: он на те же случаи отвечает 5451 своими
	//   кодами отказа (таблица `kLoc` в `ApplyFunctionResult`), и нотис игрок увидит.
	//   Там же не портирован анти-дубль эталона: перед КАЖДОЙ отправкой 5450 он делает
	//   `sub_91ED2A(g_pItemMgr, code)` (положить код в список «запрос ушёл»), а на входе —
	//   `if (… && !charContainsRecordById(g_pItemMgr, code))` (`entity_char.c:52318` — линейный
	//   поиск записи с первым dword == code). Точку УДАЛЕНИЯ из этого списка в срезе найти не
	//   удалось, поэтому вслепую воспроизводить его нельзя: ошибка заперла бы канал навсегда.
	// ★ 25.10.2026: гейт 1) ПОРТИРОВАН — поле есть: `gap_43B[10]` = `CCharacter+0x445` = профессия `SelfJob` (блок 1017 +41).
	//   Профессии нет (новичок, 0) ⇒ плашка 221 с нотисом 12848 (−11514, 10 с) и выход без пакета (item_inventory.c:3359-3366).
	//   −1 (поле ещё не пришло) не гейтим — решит сервер (5451 код 15 → тот же 12848).
	if (Flag102 == 1 && (Code == 196 || Code == 197) && AsdaSkillResetDialogsOn() && Net->SelfJob == 0)
	{
		AsdaNotice::PlankLoc(GI, EAsdaMsgCategory::System, 12848, AsdaNoticeColor::Gold, 10000.f, 5450);
		UE_LOG(LogAsdaInven, Log, TEXT("[премиум] предмет %d: профессии нет (новичок) — нотис 12848, 5450 не отправлен (как эталон)"), Code);
		return false;
	}
	if (Flag102 == 1 && SubType == 120 && CVarPlusPackOnce.GetValueOnGameThread() != 0)
	{
		const UAsdaMiniStateSubsystem* Mini = GI->GetSubsystem<UAsdaMiniStateSubsystem>();
		if (Mini && Mini->IsPlusPackActive())
		{
			AsdaNotice::PlankLoc(GI, EAsdaMsgCategory::System, 12849, AsdaNoticeColor::Gold, 10000.f, 5450);
			UE_LOG(LogAsdaInven, Log, TEXT("[премиум] предмет %d: зелье уровня уже действует — нотис 12849, 5450 не отправлен (asda.Item.PlusPackOnce)"), Code);
			return false;
		}
	}
	if (Flag102 == 1)                 { return Send5450(TEXT("Flag102==1")); }

	UE_LOG(LogAsdaInven, Log,
		TEXT("[премиум] предмет %d (подтип %d, флаги 101=%d 102=%d 103=%d) не проходит ни одну ветку эталона — ")
		TEXT("такие подтипы (90/97/98/99/104/210/211) обслуживаются собственными окнами"),
		Code, SubType, Flag101, Flag102, Type103);
	return false;
}

// ★★★ 5451 — РЕЗУЛЬТАТ 5450. Порт `netRecvItemBuffApply@0xF23C60` (network.c:36086-36830).
void UAsdaInventorySubsystem::ApplyFunctionResult(const TArray<uint8>& Body68)
{
	if (Body68.Num() < 68) { return; }
	const uint8* P = Body68.GetData();
	const int32 Result  = P[0];
	const int32 UserNum = RdI16(P + 1);
	const uint8* Rec57  = P + 3;
	const int32 Code    = RdI32(Rec57);
	// ★ [ИСПРАВЛЕНИЕ] 29.10.2026 (вес ×2): текущий и максимум — u16 (максимум до 60000–64000, сервер P96).
	const int32 CurW    = AsdaWeightFromWire(RdI16(P + 60));
	const int32 MaxW    = AsdaWeightFromWire(RdI16(P + 66));
	bActionPending = false;

	UGameInstance* GI = GetGameInstance();
	if (Result != 1)
	{
		// Таблица нотисов 1:1 со `switch (packetBody[0])` эталона. Код 0x18 («NoMessege») ветки
		//   не имеет — эталон молчит; коды 0x16/0x17 дополнительно ПЕРЕОТПРАВЛЯЮТ 5455.
		static const TMap<int32, int32> kLoc = {
			{ 0x00, 12837 }, { 0x02, 12838 }, { 0x03, 12839 }, { 0x04, 12840 }, { 0x05, 12841 },
			{ 0x06, 12842 }, { 0x07, 12843 }, { 0x08, 8114  }, { 0x09, 12844 }, { 0x0A, 12845 },
			{ 0x0B, 12846 }, { 0x0C, 8114  }, { 0x0D, 12830 }, { 0x0E, 12847 }, { 0x0F, 12848 },
			{ 0x10, 12849 }, { 0x11, 12850 }, { 0x12, 12851 }, { 0x13, 12852 }, { 0x14, 12165 },
			{ 0x15, 13223 }, { 0x16, 13224 }, { 0x17, 13228 }, { 0x19, 2254  }, { 0x1A, 12857 },
			{ 0x1B, 107090 }, { 0x1C, 107091 },
		};
		// ★★★ ИСПРАВЛЕН ДЕФЕКТ ОРИГИНАЛА (закон `fix-original-defects`): коды 0x08 и 0x0C —
		//   ОТКАЗ ПО ОТКАТУ (серверная таблица `user+244801`, 600 записей, per-item). Эталон
		//   (`netRecvItemBuffApply@0xF23C60`, network.c:36644-36678) выводит по ним нотис 8114
		//   БЕЗ ЕДИНОЙ ПОДСТАНОВКИ: `UI_FormatLocalizedNotice(g_pLocNoticeCtx, 8114, 0, 1)`.
		//   А текст 8114 в данных — незакрытый шаблон: «Откат: » (GameText.csv, строка 1354).
		//   ⇒ В ОРИГИНАЛЕ игрок видит в чате «Откат: » и НИ ОДНОЙ цифры. Это и есть жалоба
		//   «игра не показывает откат премиум-предметов, транспорта и прочего из снаряжения»
		//   (живой лог 14.08 13:05:02.719: `[сисчат A] Откат: `, предмет 123 = транспорт).
		//   Дописываем остаток из нашего реестра откатов — того же, что рисует шторку на панели.
		if (Result == 0x08 || Result == 0x0C)
		{
			float Remain = 0.f;
			if (UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr)
			{
				Remain = Hb->GetItemCooldownRemainSec(Code, /*ContainerType*/ -1, /*SlotIndex*/ -1);
			}
			// Шаблон берём из таблицы игры (8114), единицу — из неё же (6824 «сек.», та же строка,
			//   которой окно навыков подписывает кулдаун узла — `kLocSecUnit`).
			// ★★ [ИСПРАВЛЕНИЕ] 24.10.2026 (жалоба владельца «откат:» у маунтов fable): прежняя склейка «шаблон + число»
			//   в EN давала «Cooling time remains30 Seconds» (оригинальная EN-строка 8114 — законченная фраза без
			//   двоеточия), а при неизвестном остатке в RU висело голое «Откат: ». Теперь: шаблон без хвостовых
			//   пробелов и двоеточия + «: N » + единица — «Откат: 30 сек.» / «Cooling time remains: 30 Seconds»;
			//   остаток неизвестен (сервер отказал кодом 8/0x0C, а своего отката клиент не знает) — законченная фраза
			//   оригинала 9558 «Время до перезарядки.» / «Cool Time Remaining.». Ключ = 0 — прежняя склейка.
			const FString Tmpl = LocText(8114);
			FString Text;
			if (CVarCooldownNoticeFormat.GetValueOnGameThread() == 0)
			{
				Text = (Remain > 0.f) ? FString::Printf(TEXT("%s%d %s"), *Tmpl, FMath::CeilToInt(Remain), *LocText(6824)) : Tmpl;
			}
			else
			{
				Text = (Remain > 0.f)
					? FString::Printf(TEXT("%s: %d %s"), *TrimCooldownTemplate(Tmpl), FMath::CeilToInt(Remain), *LocText(6824))
					: LocText(9558);
			}
			// ★ Ревизия Л1 торговли (СРЕДН-3, 29.09.2026): канал эталона — ПЛАШКА 221 `CControl_SetTooltipText(g_pChatWnd,
			//   текст, −11514, 10000)` (network.c:36644-36678), строки чата нет. Раньше текст уходил в чат (`PostText`).
			AsdaNotice::Plank(GI, EAsdaMsgCategory::ItemRefused, Text, AsdaNoticeColor::Gold, 10000.f, 8114);
			UE_LOG(LogAsdaInven, Log,
				TEXT("[премиум] 5451 result=%d ОТКАТ предмета %d: остаток %.1f с → «%s»"),
				Result, Code, Remain, *Text);
			return;
		}
		// ★★★ Ревизия Л1 торговли (СРЕДН-3, 29.09.2026) — КАНАЛ 1:1 с эталоном (network.c:36212-36820): ВСЕ 26 веток отказа —
		//   ПЛАШКА 221 `CControl_SetTooltipText(g_pChatWnd, loc, −11514 = 0xFFFFD306, 10000)`, строки чата нет (прежний порт —
		//   `Notice` = строка чата). Исключения эталона:
		//     • 0x19 (2254): плашка + `CChat_AddTokenizedLines(…, −11514, 10000, 0)` + msgbox `CItem_BuildTooltipText(…, 0, −1, …)`;
		//     • 0x1A (12857): только если уровень вне [min, max] предмета (`item_list` +0x50/+0x54, иначе `special_itemlist`
		//       +0x48/+0x4C), со слотами `CClass_SetSlotPair` 0/1 = min/max. Маркеров `[sd1]/[sd2]` в строке НЕТ ни в одном
		//       клиенте (EN: «…from level 1 to 23.», RU: «…1-23 уровней.») — общий форматтер возвращает её без изменений,
		//       как оригинал; уровень в диапазоне — молча;
		//     • коды 0x1B/0x1C (билет лавки) не требуют живого персонажа — у нас гейта «персонаж есть» нет вовсе.
		if (Result == 0x19)
		{
			NoticePlank(2254, TEXT(""));
			AsdaNotice::ChatColored(GI, EAsdaMsgCategory::ItemRefused, LocText(2254, TEXT("")), AsdaNoticeColor::Gold, 10000.f);
			AsdaNotice::MsgBoxLoc(GI, EAsdaMsgCategory::ItemRefused, 2254);
		}
		else if (Result == 0x1A)
		{
			const UAsdaItemTableSubsystem* ITl = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
			const int32 Lv = AsdaSelf::GetLevel(GI);
			int32 LvMin = 0, LvMax = 0; bool bOut = false;
			if (const FAsdaItemRecord* R = ITl ? ITl->GetItem(Code) : nullptr)
			{
				if (Lv < R->RequiredLevel || Lv > R->RequiredLevelMax) { LvMin = R->RequiredLevel; LvMax = R->RequiredLevelMax; bOut = true; }
			}
			else
			{
				FAsdaSpecialItemlistRow SpL;
				if (ITl && ITl->GetSpecialItem(Code, SpL) && (Lv < SpL.ItemLevel || Lv > SpL.Field76)) { LvMin = SpL.ItemLevel; LvMax = SpL.Field76; bOut = true; }
			}
			if (bOut)
			{
				UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
				FString S;
				if (Txt) { Txt->NoticeSetInt(0, LvMin); Txt->NoticeSetInt(1, LvMax); S = Txt->FormatNotice(12857); }
				if (S.IsEmpty()) { S = LocText(12857, TEXT("")); }
				AsdaNotice::Plank(GI, EAsdaMsgCategory::ItemRefused, S, AsdaNoticeColor::Gold, 10000.f, 12857);
			}
			UE_LOG(LogAsdaInven, Log, TEXT("[премиум] 5451 result=0x1A: уровень %d, диапазон [%d..%d], вне=%d"), Lv, LvMin, LvMax, bOut ? 1 : 0);
		}
		else if (const int32* Loc = kLoc.Find(Result))
		{
			NoticePlank(*Loc);
		}
		else
		{
			UE_LOG(LogAsdaInven, Log, TEXT("[премиум] 5451 result=%d — у эталона ветки нет (молча)"), Result);
		}
		if ((Result == 0x16 || Result == 0x17) && GI)
		{
			// ★ Волна Л2 торговли (30.09.2026), реестр Л1-7 — 1:1 с эталоном `netRecvItemBuffApply` (network.c:36757-36782):
			//   • 0x16 («срок аренды ТС закончился», 13224): `OnRecv_RemoveSlot(g_pInventoryWnd, тело + 3)` — слот записи (+4
			//     контейнер 1/2, +5 слот) очищается ВСЕГДА; затем, ТОЛЬКО если висит состояние транспорта (15, 2)
			//     (`listContainsByTypeId(localPlayer + 638, 15, 2)`), — `netSendSimpleCmd_5455(localPlayer[52176])` = код
			//     АКТИВНОГО маунта и `charRemoveBuffBySkillId(localPlayer, 15)`;
			//   • 0x17 («в подземельях кататься нельзя», 13228): 5455 и снятие бафа 15 — тоже только при транспорте, слот не трогается.
			//   Прежний порт слал 5455 кодом из пакета ВСЕГДА (и на 0x16 слот не чистил). Код маунта — из записи состояния
			//   (`AddType2(…, ItemCode)` приёма 5453) — та же общая точка, что тумблер транспорта в `UsePremiumItem`.
			UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>();
			if (Result == 0x16)
			{
				const int32 RecCont = Rec57[4];
				const int32 RecSlot = RdI16(Rec57 + 5);
				if (RecCont == 1 || RecCont == 2) { ClearCell(RecCont, RecSlot); }
				UE_LOG(LogAsdaInven, Log, TEXT("[премиум] 5451 0x16: слот %d:%d очищен (OnRecv_RemoveSlot)"), RecCont, RecSlot);
			}
			const int32 SelfNum = Net ? AsdaSelf::GetUserIdxOr(GI, INDEX_NONE) : INDEX_NONE;
			UAsdaEffectsSubsystem* Eff = GI->GetSubsystem<UAsdaEffectsSubsystem>();
			const FAsdaEffectEntry* Mount = (Eff && SelfNum != INDEX_NONE) ? Eff->FindByTypeSub(SelfNum, 15, 2) : nullptr;
			if (Mount && Net)
			{
				const int32 MountCode = Mount->ItemCode > 0 ? Mount->ItemCode : Code;
				const bool bOk = Net->SendFunctionItemToggle5455(MountCode);
				Eff->RemoveBySkillState(SelfNum, /*Id*/ 15, /*Type*/ 2);
				UE_LOG(LogAsdaInven, Log, TEXT("[премиум] 5451 0x%02X: транспорт активен → 5455 кодом маунта %d %s, баф 15 снят"),
					Result, MountCode, bOk ? TEXT("отправлен") : TEXT("НЕ отправлен"));
			}
			else
			{
				UE_LOG(LogAsdaInven, Log, TEXT("[премиум] 5451 0x%02X: транспорта нет — 5455 не шлётся (эталон)"), Result);
			}
		}
		return;
	}

	// ── УСПЕХ. Эталон: запись предмета кладётся в СУМКУ по своему слоту, затем `switch (rec->subType − 1)`.
	PutRecord(Rec57, /*ForcedContainer*/ 0);

	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	FAsdaSpecialItemlistRow Sp;
	const bool bSp = IT && IT->GetSpecialItem(Code, Sp);

	// ── ★★★ СПИСАНИЕ ПРЕДМЕТА. Порт хвоста `netParseInventorySlotUpdate@0xF28B20`
	//   (network.c:36919-36936) — общей функции, которую эталон зовёт ВНУТРИ каждой ветки
	//   `switch (Table2Record->subType − 1)` этого же пакета. Правило — байт записи шаблона +0x57:
	//       1 ⇒ `uiInventoryRemoveItemAndRebuild` + `OnRecv_RemoveSlot` — слот чистится ВСЕГДА;
	//       2 ⇒ то же, но `OnRecv_RemoveSlot` только при `*(int*)(узел+13) <= 0`, где узел+13 —
	//           поле записи +9 (КОЛИЧЕСТВО): `netParseInventorySlotUpdate` копирует
	//           `*(DWORD*)(v8+13) = *(DWORD*)(a2+12)`, а a2+12 = тело+12 = запись+9;
	//       иное ⇒ слот не трогаем (3 = транспорт, 4 = спутник, 0 = прочее).
	//
	//   ★ ЭТО И БЫЛ ДЕФЕКТ «применил предмет — предмет не исчез» (живой прогон 14.08).
	//   Доказательство из лога `Saved/Logs/AsdaReborn.log` (08:01:59.008):
	//       RECV op=5451 body=[01 00 00 | 7A 01 00 00 | 01 | 0A 00 | 00 00 | 00 00 00 00 | …]
	//                          рез=1 userNum=0   код=378  конт=1  слот=10   КОЛИЧЕСТВО = 0
	//   Сервер предмет уже списал (`FuncLog_20260814.txt`: `[USE] Success..! … iCode:378 ItemPos:10`)
	//   и вернул запись с количеством 0, а порт клал её в ячейку как есть. `IsEmpty()` смотрит
	//   ТОЛЬКО на `ItemCode`, поэтому ячейка осталась «занята» кодом 378 с количеством 0 — что и
	//   видно строкой наведения `контейнер 1 ячейка 10 · код=378 кол-во=0`. На повторное применение
	//   сервер ответил 5451 result=2 «Такого инструмента нет» (08:02:06.904).
	//   Отдельного пакета удаления слота у этого канала НЕТ (проверено по логу: между 5451 и
	//   следующим действием пришли только 4015/6001/1001).
	if (bSp)
	{
		const int32 ConsumeMode = UAsdaItemTableSubsystem::SpecialItemConsumeMode(Sp);
		const int32 RecAmount   = RdI32(Rec57 + 9);
		const int32 RecCont     = Rec57[4];
		const int32 RecSlot     = RdI16(Rec57 + 5);
		const bool  bWipe       = (ConsumeMode == 1) || (ConsumeMode == 2 && RecAmount <= 0);
		if (bWipe)
		{
			ClearCell(RecCont, RecSlot);
			UE_LOG(LogAsdaInven, Log,
				TEXT("[премиум] предмет %d СПИСАН: ячейка %d:%d очищена (режим +0x57 = %d, количество в ответе %d)"),
				Code, RecCont, RecSlot, ConsumeMode, RecAmount);
		}
		else
		{
			UE_LOG(LogAsdaInven, Log,
				TEXT("[премиум] предмет %d НЕ списывается: режим +0x57 = %d (%s), в ячейке %d:%d осталось %d"),
				Code, ConsumeMode,
				(ConsumeMode == 3) ? TEXT("транспорт") : (ConsumeMode == 4) ? TEXT("спутник") : TEXT("прочее"),
				RecCont, RecSlot, RecAmount);
		}
	}
	// ★ СВИТОК ВОЗВРАТА СКЛАДА (`special_itemlist.SubType == 112`; коды предметов
	//   382/383/384/406/456/460/514) — ТРЕТИЙ вход в склад, помимо кнопок кладовщика 30011/30012
	//   и кнопок внутри самого окна. Эталон `netRecvItemBuffApply@0xF23C60` (`network.c:36580`),
	//   вложенный `case 111` (нумерация вложенного switch сдвинута на 1 относительно `SubType`)
	//   зовёт `uiWindowOpen_EA20(g_pWarehouseWnd)`: показывается МАЛЕНЬКОЕ окно 5500 «Удалённый
	//   склад» с кнопками «Забрать»/«Хранить», и взводится признак `isOpen` (+0x5734), который
	//   блокирует движение, разговор с NPC, обмен, хотбар и операции инвентаря (нотисы 10305…10309).
	//   ⚠ Сервер отдельного согласия не спрашивает: сюда мы попадаем уже по успешному 5451.
	if (bSp && Sp.SubType == 112)
	{
		if (UAsdaWarehouseSubsystem* Ware = GI ? GI->GetSubsystem<UAsdaWarehouseSubsystem>() : nullptr)
		{
			Ware->OpenRecallPanel();
			UE_LOG(LogAsdaInven, Log,
				TEXT("[склад] 5451 подтип 112: свиток возврата склада (предмет %d) — открыто окно 5500"), Code);
		}
	}
	// ★★★ БИЛЕТ ПРЕМИУМ-ЛАВКИ (`special_itemlist.SubType == 65`), волна Л1 торговли 29.09.2026. Эталон
	//   `netRecvItemBuffApply@0xF23C60` вложенный `case 64` (network.c:36570-36577): запись билета (код @+3, контейнер @+7,
	//   слот @+8 тела) → `CSellWindow+0x38A/+0x38E/+0x38F`, премиум `+0x389 = 1`; «переоткрытие» (`g_bSellWindowActive`)
	//   → сначала 6305; затем `netSendPlayerInfo6300(g_pSellWindow)`. Всё это — у владельца лавки (мы уже в Tick инвентаря).
	if (bSp && Sp.SubType == 65)
	{
		if (UAsdaSelfStoreSubsystem* SS = GI ? GI->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr)
		{
			SS->OnTicketUsed(Rec57);
		}
	}
	if (bSp && Sp.SubType == 51)
	{
		// `case 50` вложенного switch — ЕДИНСТВЕННАЯ ветка, где эталон правит вес:
		//   `char->gap_43B[13] = HIWORD(packetBody[16])` (максимум) и `HUD_UpdateMiniWeightBar(SLOWORD(packetBody[15]))`.
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->SetSelfWeight(FMath::Max(CurW, 0), MaxW, TEXT("5451 подтип 51"));
		}
	}
	// ★ ОТКАТ ПРЕМИУМ-ПРЕДМЕТА. В эталоне он взводится не здесь напрямую, а через виртуальный хвост
	//   класса эффекта: `sub_9339C8(узел, Table2Record->cooldownBase)` → `sub_92C3BC` → `sub_925BE8`
	//   → `hotbarPushItemCooldown(g_pSkillWnd, узел+4 (itemCode), узел+8 (контейнер), узел+9 (слот))`.
	//   Источник длительности — `SpecialItemRecord.CooldownBase` (+0x60, миллисекунды).
	// ★ ИСПРАВЛЕНО (перепроверка): группа у премиум-предметов ЕСТЬ — это `LinkGroupId` (+0x75).
	//   Прежний комментарий «групп у премиум-предметов на клиенте НЕТ, гасятся только слоты с тем же
	//   кодом» ОПРОВЕРГНУТ телом `itemColorTypeMatch@0xDF67C0` (`item_inventory.c:55522-55552`):
	//   третий аргумент (контейнер) там не используется ВООБЩЕ, а сравниваются именно группы, причём
	//   ПЕРЕКРЁСТНО между таблицами — `ItemRecord->gap_CC` (+0xCC) сопоставляется с
	//   `Table2Record->linkGroupId[0]` (+0x75) как одно и то же пространство номеров.
	//   По данным (`special_itemlist`, 558 строк) группа ненулевая у 411 записей: 5 → 214 (весь
	//   транспорт), 6 → 106, 3 → 50, 1 → 28, 7 → 8, 4 → 5. Группа 1 ОБЩАЯ с `item_list`
	//   (26 зелий здоровья), то есть в оригинале зелье гасит и премиум-предметы той же группы.
	// ★★★ УТОЧНЁН МЕХАНИЗМ (перепроверка 14.08 по бинарю, снято прежнее «через виртуальный хвост
	//   sub_9339C8»). Цепочка в эталоне такая:
	//     `sub_9339C8` = thunk на `sub_F28DD0@0xF28DD0`: `узел[16] = 1000 * cooldownBase` — это
	//       ДЛИТЕЛЬНОСТЬ САМОГО ЭФФЕКТА (её потом уменьшает тик `узел[16] -= dt`), к откату НЕ ОТНОСИТСЯ;
	//     откат взводит СПАВН эффекта: каждый `CharEffect_Spawn*` / `Recv_MountVehicleRide@0xF2BCF0`
	//       заканчивается вызовом `sub_92C3BC` = thunk на `sub_F28E30@0xF28E30`:
	//           `узел[17] = 2; sub_925BE8(узел)` → `sub_F28D90@0xF28D90` →
	//           `hotbarPushItemCooldown(g_pSkillWnd, узел+4 код, узел+8 контейнер, узел+9 ячейка)`.
	//   Длительность `hotbarPushItemCooldown@0xE48FD0` берёт САМ из `SpecialItemRecord.cooldownBase`
	//   (+0x60) и трактует её как МИЛЛИСЕКУНДЫ (`durationF = (float)cooldownMs`).
	// ★ ГЕЙТ ЭТАЛОНА ПО ПОДТИПУ (`switch (Table2Record->subType − 1)`, network.c:36236-36600):
	//   откат взводят ВСЕ обработанные кейсы, КРОМЕ 19, 20, 64, 111, 119 (= подтипы 20, 21, 65,
	//   112, 120: там нет ни `sub_9339C8`, ни спавна) и кроме `default` (необработанный подтип).
	//   ⚠ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ: мы взводим откат для ЛЮБОГО подтипа с `CooldownBase > 0`, потому
	//   что сервер держит откат по своей per-item таблице (`user+244801`, отказ кодом 8) НЕЗАВИСИМО
	//   от подтипа — доказано живым логом 14.08 (`FuncLog_20260814.txt`, 16:05:02
	//   `[REQ_FAIL] … Code:123, Result:8`). Сузить гейт до эталонного значило бы вернуть «жму —
	//   отказ без причины» ровно на тех подтипах, где оригинал молчит.
	if (UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr)
	{
		if (bSp && Sp.CooldownBase > 0)
		{
			Hb->PushItemCooldownRaw(Code, Rec57[4], RdI16(Rec57 + 5), Sp.LinkGroupId, Sp.CooldownBase);
		}
		Hb->RebuildSlots();
		Hb->RefreshPage();
	}
	// ★★★ ВИЗУАЛ. Эталонный `switch (Table2Record->subType − 1)` внутри `case 1` — это 32 кейса
	//   (`network.c:36236-36592`), а НЕ семь. Раньше порт визуал на 5451 не показывал вовсе: эффект
	//   премиум-предмета случайно «проявлялся» только через широковещательный 5453, где работает
	//   7-кейсовая таблица `charBuff_ApplyPMBuffFromPacket@0xF26110`. Из-за этого подтипы 19, 25,
	//   41…52 (восстановление ЖИЗНЕЙ/МАНЫ/всего, разрушение брони, сброс навыков) не показывали
	//   ничего — дефект живого прогона «эффекта применения премиум зелий не вижу»
	//   (лог 13.08: коды 37 подтип 52 и 500 подтип 19 — ни одной строки `[предмет-визуал]`).
	if (bSp)
	{
		if (UAsdaEffectsSubsystem* Fx = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr)
		{
			Fx->SpawnPmItemUseFx(UserNum, Sp.SubType);
		}
	}
	// ★ 08.11.2026 [ОРИГИНАЛ] (пачка 16, баг 1 «двойной щелчок по транспорту не показывает, сколько дней осталось»).
	//   Эталон `netRecvItemBuffApply@0xF23C60`, успех, `case 14` (подтип 15, после `Recv_MountVehicleRide`): при живом своём
	//   персонаже и `часы = *(int*)(тело + 62) > 0` — больше 24 ч: дни = часы / 24, остаток часов > 0 ⇒ +1, слот [sd1] = дни,
	//   loc 20230 «Срок действия этого ТС - [sd1] дн.»; иначе loc 13227 «Сегодня заканчивается срок аренды ТС.». Канал —
	//   `CControl_SetTooltipText(g_pChatWnd, …, −11514, 10000)` = плашка `AsdaNotice::Plank` (золото, 10 с, сверху по центру).
	//   Поле +62 сервер заполняет только для предметов режима 3 (транспорт): `v65` ответа `[USE] Success` (GameServer.c:92758);
	//   бессрочный транспорт присылает ≤ 0 — нотиса нет, как у оригинала. Порт эту ветку пропустил. Ключ 0 — прежний порт.
	if (bSp && Sp.SubType == 15 && AsdaMountDaysNoticeOn())
	{
		const int32 Hours = RdI32(P + 62);
		if (Hours > 0)
		{
			FString S;
			int32 LocId = 13227;
			if (Hours > 24)
			{
				const int32 Days = Hours / 24 + ((Hours % 24) > 0 ? 1 : 0);
				LocId = 20230;
				if (UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr)
				{
					Txt->NoticeSetInt(0, Days);
					S = Txt->FormatNotice(20230);
				}
				if (S.IsEmpty()) { S = LocText(20230, TEXT("")).Replace(TEXT("[sd1]"), *FString::FromInt(Days)); }
			}
			else
			{
				S = LocText(13227, TEXT(""));
			}
			if (!S.IsEmpty())
			{
				AsdaNotice::Plank(GI, EAsdaMsgCategory::System, S, AsdaNoticeColor::Gold, 10000.f, LocId);
			}
			UE_LOG(LogAsdaInven, Log, TEXT("[транспорт] 5451: предмет %d, осталось %d ч → нотис %d «%s»"), Code, Hours, LocId, *S);
		}
	}
	if (bWindowOpen) { RefreshGrid(); }
	UE_LOG(LogAsdaInven, Log,
		TEXT("[премиум] 5451 успех: предмет %d (подтип %d, откат %d мс) применён игроком userNum=%d"),
		Code, bSp ? Sp.SubType : -1, bSp ? Sp.CooldownBase : 0, UserNum);
}
