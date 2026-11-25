// Copyright (c) 2026 @sitxovski (Discord)
// AsdaInventorySubsystem_Net_UseResult.cpp — ★ R3-c 12.11.2026 (разрез AsdaInventorySubsystem_Net.cpp, дословный перенос): эффекты применения, ответ применения/отказ, лопата и автокопание.
//   Общее для частей — в AsdaInventorySubsystem_Net_Private.h; шапка и реверс — в AsdaInventorySubsystem_Net.cpp.
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaSoulStoneSubsystem.h"   // ★ 06.11.2026: ворота Камня души
#include "UI/AsdaSowelSubsystem.h"         // ★ ветка САМОЦВЕТОВ правой панели (430…437 / 441…446 / 495 / 499)
#include "Items/AsdaItemUseStateGate.h"      // ★ 05.11.2026: гейт 7007 «ограничивающее состояние» (повтор автолопаты)
// ★ обмен по таблице событий (двойной клик по предмету subType 202 → 6098)   ★ R3: комментарий снятого включения "Npc/AsdaNpcDialogSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaPremiumShopSubsystem.h"   // ★ 29.09.2026: `g_pPartyTargetObj` = элитный магазин (гейты №15/№15b лопат)
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "AsdaInventorySubsystem_Net_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaInventorySubsystem_Net*.cpp

// ★★★ ВИЗУАЛ И ЗВУК ПРИМЕНЕНИЯ РАСХОДНИКА — ТАБЛИЦА ИЗ КОДА ЭТАЛОНА, НЕ ИЗ ДАННЫХ.
//   `switch (ItemRecord->linkCode)` в `OnRecv_UseItemResult@0xD0B1E0` (item_inventory.c:39095-39157).
//   ⚠ Имени эффекта/звука в записи предмета НЕТ и быть не может: скан всех 14 601 764 байт
//   `item_list.dec.bin` (58 878 × 248) даёт 0 вхождений `.jeb` / `.wav` / `eff_` и 0 ASCII-строк
//   длиной ≥ 6 в хвосте записи. В оригинале визуал ЗАШИТ ПО ВИДУ (`linkCode`, он же серверное `+12`),
//   поэтому и здесь он таблицей в коде — снятой построчно с эталона, без единой выдумки
//   (законы `never-invent-only-original`, `no-invented-ui`).
//   `default:` в эталоне — `return`, то есть ничего: у 10 554 расходников с `linkCode == 0`
//   (коробки, материалы, жетоны, лопаты, свитки) визуала НЕТ, и добавлять его нельзя.
struct FAsdaItemUseFx
{
	const TCHAR* Jeb;     // база .jeb (без расширения) — Effect_SpawnByName(…, "<Jeb>.jeb", …)
	int32        SoundId; // id из таблицы звуков (CSoundMgr_PlaySound3D); −1 = звука в эталоне нет
};
static bool AsdaItemUseFxByLinkCode(int32 LinkCode, FAsdaItemUseFx& Out)
{
	switch (LinkCode)
	{
	case 5:                       // case 5: только эффект, звука эталон НЕ играет
		Out = { TEXT("eff_0_othr_99_elptn01-a_00"), -1 };  return true;
	case 7:                       // мгновенное HP
	case 200:                     // 0xC8 — HP-регенерация по времени (та же ветка switch)
		Out = { TEXT("eff_0_othr_99_hpptn01-a_00"), 95 };  return true;
	case 8:                       // мгновенное MP
	case 201:                     // 0xC9 — MP-регенерация по времени
		Out = { TEXT("eff_0_othr_99_mpptn01-a_00"), 96 };  return true;
	default:
		return false;             // эталон: `default: return;`
	}
}

// ★★★ 5029 `OP_ON_RECV_USE_ITEM_RESULT` — `OnRecv_UseItemResult@0xD0B1E0` (item_inventory.c:38976).
//
// РАСКЛАДКА ТЕЛА (24 б). Авторитет — СБОРКА ПАКЕТА НА СЕРВЕРЕ (`CharSpendItemUse@0x4D8C40`,
//   GameServer.c:86176-86186; база кадра — локальная `a5` по `ebp-0x24`, смещения посчитаны по
//   объявлениям локалов v46…v54):
//     +0  u8  код результата 0…14
//     +1  i16 userNum ЦЕЛИ  (`v46 = a2`)      ← ⚠ именно userNum, НЕ charId ([[charid-vs-usernum]])
//     +3  i32 charId        (`v47`)            — клиент оригинала его НЕ читает
//     +7  i32 itemCode      (`v48 = Src`)
//     +11 i16 slotIndex     (`v49 = v27`)
//     +13 i16 0             (`v50 = 0`, резерв)
//     +15 u8  containerType (`v51 = v26`)
//     +16 i32 новое количество в стопке (`v52 = v28`)
//     +20 i16 вес СТОПКИ    (`v53 = v29`)
//     +22 i16 вес ПЕРСОНАЖА (`v54 = v41 = текущий вес − вес единицы`)
//   ★ СНЯТО ЛОЖНОЕ «ИСПРАВЛЕНИЕ ДЕФЕКТА». Прежде здесь утверждалось, что клиент читает вес по
//   байтам 18-19 (`HUD_UpdateMiniWeightBar(g_pHUD, SHIWORD(packet[2]))`) и что мини-полоса веса в
//   оригинале показывает мусор. ЭТО НЕВЕРНО: `SHIWORD(packet[2])` — артефакт печати Hex-Rays.
//   В самом бинаре при базе кадра `packet = ebp-0x120`:
//       0xD0B4AB  movsx eax, word ptr [ebp-0x10A]   ; 0x120−0x10A = +22  → HUD_UpdateMiniWeightBar
//       0xD0B5D5  movsx eax, word ptr [ebp-0x10A]   ; +22, вторая ветка
//   (рядом `mov cx, [ebp-0x10c]` = +20 → `invSlot+0xE`, вес стопки — индексация согласована).
//   Оригинал читает вес персонажа ПРАВИЛЬНО, дефекта нет. Значение +22 ниже верно и менялось не
//   ради «починки», а просто совпадает с оригиналом.
//
// ★★★ ПАКЕТ РАССЫЛАЕТСЯ СОСЕДЯМ. На успешном пути сервер зовёт `sub_52E630@0x52E630` →
//   `SendPacket_ClippingSectorInAllUser@0x41E4F0` (GameServer.c:86188) — 9 секторов, ВКЛЮЧАЯ свой,
//   опкод и тело те же. Рассылка подавляется (`v22 = 0`) для `+27 ∈ {15, 16, неактивный 32}` и
//   `+12 == 110` (корм питомца) — тогда уходит только автору (`sub_4C28F0`).
//   ⚠ ДЕФЕКТ ОРИГИНАЛА, ИСПРАВЛЯЕМ: клиент эталона на входе в ветку успеха делает
//   `if (!targetChar || !targetChar[47565]) return;`, а поле `+0x2E734` (`selectionFlag`) не пусто
//   ТОЛЬКО У ЛОКАЛЬНОГО ИГРОКА — доказано вторым методом: `charNamePlate_UpdateAllFactions`
//   (entity_char.c:9991) обновляет плашку фракции ровно при `!selectionFlag`, то есть у ЧУЖИХ,
//   а `network.c:46558` по `selectionFlag` отделяет «показать подсказку себе» от «просто выставить
//   состояние другому». Итог: сервер зелья соседей рассылает, а оригинальный клиент их МОЛЧА
//   ВЫБРАСЫВАЕТ. Мы для чужого userNum выполняем РОВНО визуальную часть (эффект + звук по
//   `linkCode`) и НЕ трогаем свой инвентарь/вес/откат/нотисы — иначе чужой пакет затирал бы нашу
//   ячейку с тем же номером (именно от этого и защищал оригинальный `return`).
void UAsdaInventorySubsystem::ApplyUseResult(const TArray<uint8>& Body24)
{
	if (Body24.Num() < 24) { return; }
	const uint8* P = Body24.GetData();
	const int32 Result    = P[0];
	const int32 UserNum   = RdI16(P + 1);
	const int32 ItemCode  = RdI32(P + 7);
	const int32 SlotIndex = RdI16(P + 11);
	const int32 ContType  = P[15];
	const int32 NewAmount = RdI32(P + 16);
	const int32 NewStackW = RdI16(P + 20);
	const int32 NewCharW  = AsdaWeightFromWire(RdI16(P + 22));   // ★ 29.10.2026 (вес ×2): вес персонажа u16

	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	// ★ Волна 1 (14.09.2026, исправление E2): свой номер — `AsdaSelf::GetUserIdxOr` (−1 до пакета 4003). Раньше до 4003
	//   здесь стоял 0, и «своим» считался только пакет userNum 0, вопреки правилу ниже. Ключ отката ядра на выборку номера не действует.
	const int32 SelfNum = Net ? AsdaSelf::GetUserIdxOr(GI, INDEX_NONE) : -1;
	// Свой пакет или соседский. Отказы сервер шлёт ТОЛЬКО автору (`sub_4C28F0(…, a2, …)`), поэтому
	//   при неизвестном своём userNum (до 4003) считаем пакет своим — как и вёл себя порт раньше.
	//   ⚠ Сравниваем 16-битные ЗНАЧЕНИЯ: в пакете поле знаковое (`__int16 v46`), а `SelfUserNum` —
	//   `uint16`; без маски userNum ≥ 32768 никогда бы не совпал сам с собой.
	const bool bSelf = (SelfNum < 0) || ((UserNum & 0xFFFF) == (SelfNum & 0xFFFF));

	if (bSelf) { bActionPending = false; }

	if (Result != 1)
	{
		ApplyUseRefusal(Result, ItemCode);
		return;
	}

	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(ItemCode) : nullptr;
	if (!Rec)
	{
		// Эталон: `if (!invSlot || !ItemRecord || !targetChar || !targetChar[47565]) return;`
		UE_LOG(LogAsdaInven, Warning,
			TEXT("[инвентарь] 5029: предмета %d нет в таблице — эталон в этом месте выходит из функции"),
			ItemCode);
		return;
	}

	// ── 1. ВИЗУАЛ И ЗВУК: и себе, и соседям, по ОДНОМУ пути (как у навыков, [[skill-aoe-and-broadcast]])
	FAsdaItemUseFx Fx;
	if (AsdaItemUseFxByLinkCode(Rec->LinkCode, Fx))
	{
		if (UAsdaEffectsSubsystem* Eff = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr)
		{
			Eff->SpawnItemUseFx(UserNum, Fx.Jeb, Fx.SoundId);
		}
	}

	// ── 2. СОСТОЯНИЯ 265 / 266 (регенерация HP/MP по времени) — только ЛОКАЛЬНОМУ игроку.
	//   Эталон: `if (targetChar[47565] && linkCode == 200) cCharStatusAddTypeMount(targetChar, 265, 27, itemCode)`
	//   (то же для 201 → 266, 24). ★ Имя `cCharStatusAddTypeMount@0xA2AF10` ВРЁТ: ветка «маунт»
	//   внутри срабатывает только при `statusId == 15`, а по факту это
	//   `charStatusAddType2(char + 638, id, durationSec, itemCode)` — то есть 265/266 это ИД состояния,
	//   а 27/24 — ДЛИТЕЛЬНОСТЬ В СЕКУНДАХ, а вовсе не «подтип» (entity_char.c:25584).
	//   Иконка в HUD обновляется тем же `HUD_PlayerBuffIcons_Update` = делегатом `OnEffectsChanged`.
	if (bSelf && (Rec->LinkCode == 200 || Rec->LinkCode == 201))
	{
		if (UAsdaEffectsSubsystem* Eff = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr)
		{
			const int32 StateId  = (Rec->LinkCode == 200) ? 265 : 266;
			const int32 DurSec   = (Rec->LinkCode == 200) ? 27  : 24;
			Eff->AddType2(UserNum, StateId, DurSec, ItemCode);
			UE_LOG(LogAsdaInven, Log,
				TEXT("[инвентарь] 5029: предмет %d повесил состояние %d на %d с (регенерация %s)"),
				ItemCode, StateId, DurSec, (StateId == 265) ? TEXT("ЖИЗНИ") : TEXT("МАНЫ"));
		}
	}

	if (!bSelf)
	{
		// Дальше — работа со СВОИМ инвентарём, весом, откатом и чатом. Для соседа её делать нельзя.
		UE_LOG(LogAsdaInven, Log,
			TEXT("[инвентарь] 5029 СОСЕДА (userNum=%d): предмет %d — показан только эффект/звук, ")
			TEXT("свой инвентарь не тронут"), UserNum, ItemCode);
		return;
	}

	// ── 2b. ★★★ КАМЕНЬ ДУШИ (`itemSubType == 32`). Порт `OnRecv_UseItemResult@0xD0B1E0`,
	//   `item_inventory.c:39044-39061` + `:39159-39168`. Ветка стоит СТРОГО ДО нотиса 20069:
	//   метка `LABEL_17` эталона — это как раз нотис, и переход `if (subType != 32) goto LABEL_17`
	//   заводит туда все прочие подтипы.
	//
	//   Развилка — по `inventoryHasEquippedItemOfType32@0xD25980` (`:43319-43340`): скан 60 ячеек
	//   контейнера расходников (`&g_InventoryArray2 + 57*i`) на запись с `itemSubType == 32` и
	//   БАЙТОМ +50 записи == 3 («камень активирован»).
	//     • есть активированный ⇒ ПОТРЕБЛЕНИЕ: эффект + звук на ПАРТНЁРЕ-соулмейте, дальше общий путь;
	//     • нет ⇒ АКТИВАЦИЯ: слот помечается, вес обновляется, и эталон выходит СРАЗУ —
	//       ни нотиса, ни эффекта, ни отката (`invIsBagSlotActive@0xD25C20`).
	//   ⚠ Ни в одной ветке откат НЕ взводится: `hotbarPushItemCooldown` эталон зовёт только при
	//   подтипах 15/16 и linkCode 7/8/200/201 (`ui_core.c:71963-71966`).
	if (Rec->ItemSubType == 32)
	{
		bool bHaveActivated = false;
		if (const TArray<FAsdaItemInstance>* SpendC = Container(2))
		{
			for (const FAsdaItemInstance& It : *SpendC)
			{
				if (It.IsEmpty() || It.Raw.Num() <= 50) { continue; }
				const FAsdaItemRecord* R2 = IT->GetItem(It.ItemCode);
				if (R2 && R2->ItemSubType == 32 && It.Raw[50] == 3) { bHaveActivated = true; break; }
			}
		}
		if (bHaveActivated)
		{
			if (AsdaSoulStoneOn()) { if (UAsdaSoulStoneSubsystem* Stone = GI ? GI->GetSubsystem<UAsdaSoulStoneSubsystem>() : nullptr) { Stone->ResetBlock(); } }
			// ПОТРЕБЛЕНИЕ. Эталон: `sub_92E847` сбрасывает блок полей окна соулмейта (636…655),
			//   затем берёт userNum ПАРТНЁРА (`Field142C->gap_8B4[4]`, ставится в `social.c:7314`)
			//   и вешает эффект + звук ИМЕННО НА НЕГО (кость 0, режим привязки 5 = позиция персонажа).
			int32 PartnerNum = -1;
			if (UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
			{
				PartnerNum = WS->GetSoulPartnerUserIdx(UserNum);
			}
			if (PartnerNum >= 0)
			{
				if (UAsdaEffectsSubsystem* Eff = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr)
				{
					Eff->SpawnItemUseFx(PartnerNum, TEXT("eff_1_char_99_expmovg-a_01"),
						/*SoundId*/ -1, TEXT("S_c_SmExpGift"));
				}
				UE_LOG(LogAsdaInven, Log,
					TEXT("[камень души] предмет %d ПОТРАЧЕН: эффект expmovg-a_01 + S_c_SmExpGift на партнёре userNum=%d"),
					ItemCode, PartnerNum);
			}
			else
			{
				// Эталон при `!CharManager_FindByUserIdx(партнёр)` просто пропускает визуал и идёт дальше.
				UE_LOG(LogAsdaInven, Log,
					TEXT("[камень души] предмет %d ПОТРАЧЕН, но партнёра-соулмейта в мире нет — ")
					TEXT("эталон в этом случае визуал тоже пропускает"), ItemCode);
			}
			// Дальше — общий путь LABEL_17 (нотис 20069, списание, вес).
		}
		else
		{
			// ★ 06.11.2026 [ОРИГИНАЛ] Ворота `invIsBagSlotActive@0xD25C20` (item_inventory.c:39159): без них эталон молча выходит.
			UAsdaSoulStoneSubsystem* Stone = GI ? GI->GetSubsystem<UAsdaSoulStoneSubsystem>() : nullptr;
			if (AsdaSoulStoneOn() && Stone && !Stone->IsBagSlotActive())
			{
				UE_LOG(LogAsdaInven, Log, TEXT("[камень души] 5029: предмет %d, ворота invIsBagSlotActive закрыты — как эталон, без действий"), ItemCode);
				return;
			}
			// АКТИВАЦИЯ. Эталон: `inventorySlotSelectAndCopy` ставит байту +50 значение 3,
			//   обновляет ячейку и мини-полосу веса, `uiProgressShowZero(g_pSoulmateWnd)` —
			//   и ВЫХОДИТ: ни нотиса 20069, ни эффекта, ни отката.
			if (TArray<FAsdaItemInstance>* SpendC = Container(2))
			{
				if (SpendC->IsValidIndex(SlotIndex))
				{
					FAsdaItemInstance& Cell = (*SpendC)[SlotIndex];
					if (Cell.Raw.Num() > 50) { Cell.Raw[50] = 3; }
					if (NewAmount > 0) { Cell.SetAmount(NewAmount); Cell.SetStackWeight(NewStackW); }
					NotifyCell(2, SlotIndex);
				}
			}
			if (Net) { Net->SetSelfWeight(NewCharW, -1, TEXT("5029 активация камня души")); }
			if (bWindowOpen) { RefreshGrid(); }
			if (AsdaSoulStoneOn() && Stone) { Stone->OnStoneActivated(SlotIndex); }
			UE_LOG(LogAsdaInven, Log,
				TEXT("[камень души] предмет %d АКТИВИРОВАН в ячейке 2:%d (байт +50 = 3); ")
				TEXT("эталон здесь выходит без нотиса, эффекта и отката"), ItemCode, SlotIndex);
			return;
		}
	}

	// ── 3. ЧАТ-НОТИС 20069 «Ты используешь предмет: [ss1].» — эталон печатает его при
	//   `targetChar[47565] == 1` (локальный игрок) на ЛЮБОМ успешном применении: метка `LABEL_17`
	//   стоит ПОСЛЕ ветки Камня души, и переход `if (itemSubType != 32) goto LABEL_17` заводит сюда
	//   все прочие подтипы (item_inventory.c:39044/39062-39070). Имя подставляется через
	//   `CObject_SetSlotString(ctx, 0, name, 0)` — это и есть слот `[ss1]`.
	//   Категория `Loot` выбрана не наугад: эталонный цвет этой строки −7930543 = 0xFF86FD51 —
	//   (проверено: 2^32 − 7930543 = 4287036753 = 0xFF86FD51; бинарь `0xD0B528: push 0xff86fd51`.
	//    Раньше здесь стояло 0xFF88FD51 — опечатка, само число −7930543 верное)
	//   светло-зелёный, ровно как у категории `Loot` в таблице маршрутизации (у отказов эталон
	//   даёт −11514 = 0xFFFFD306, золотой — это `ItemRefused`).
	if (UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
	{
		Msg->PostLocFmt(EAsdaMsgCategory::Loot, 20069, { IT->GetItemName(Rec->ItemCode) }, {});   // ★ [ПОРТ] 09.10.2026 (мультиязычность): имя — геттером (язык сессии)
	}

	// ── 4. ОБНОВЛЕНИЕ ЯЧЕЙКИ.
	//   ⚠ ДЕФЕКТ ОРИГИНАЛА, ИСПРАВЛЯЕМ: эталон адресует ячейку ЖЁСТКО через
	//   `&g_InventoryArray2 + 57*slotIndex` (контейнер 2), а контейнер из пакета лишь ЗАПИСЫВАЕТ в
	//   поле записи слота (`*((_BYTE*)invSlot + 4) = HIBYTE(packet[1])`). Сервер же присылает тот
	//   контейнер, который мы сами указали в 5028, — берём его, а на расхождение пишем в лог.
	const int32 UseCont = (ContType == 1 || ContType == 2) ? ContType : 2;
	if (UseCont != 2)
	{
		UE_LOG(LogAsdaInven, Log,
			TEXT("[инвентарь] 5029: сервер вернул контейнер %d (эталон здесь всегда пишет во 2-й — ")
			TEXT("дефект оригинала); применяем к контейнеру %d"), ContType, UseCont);
	}
	if (TArray<FAsdaItemInstance>* Cont = Container(UseCont))
	{
		if (Cont->IsValidIndex(SlotIndex))
		{
			if (NewAmount > 0)
			{
				(*Cont)[SlotIndex].SetAmount(NewAmount);
				(*Cont)[SlotIndex].SetStackWeight(NewStackW);   // эталон: `invSlot[7] = WORD2(packet[2])`
				NotifyCell(UseCont, SlotIndex);
			}
			else
			{
				// Эталон: `OnRecv_RemoveSlot_byPtr` — стопка кончилась, слот чистится целиком.
				ClearCell(UseCont, SlotIndex);
			}
		}
	}

	// ── 5. ВЕС ПЕРСОНАЖА (мини-полоса HUD). См. разбор дефекта в шапке: берём +22, а не байты 18-19.
	if (Net) { Net->SetSelfWeight(NewCharW, -1, TEXT("5029 применение предмета")); }

	// ── 6. ОТКАТ. Эталон зовёт `hotbarPushItemCooldown(g_pSkillWnd, itemCode, containerType, slotIndex)`
	//   в трёх местах: при `itemSubType == 15`, при `== 16` и внутри веток `linkCode` 7/200 и 8/201.
	//   Гейт внутри самой функции — ровно объединение этих условий, поэтому здесь достаточно одного
	//   вызова: подсистема панели сама решит, ставить откат или нет (и на какую группу).
	if (UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr)
	{
		Hb->PushItemCooldown(ItemCode, UseCont, SlotIndex);
		// Эталон: `CClass_BuildItemRecord(g_pQuickSlotBar, 2, invSlot)` — иконка/счётчик слота панели.
		Hb->RebuildSlots();
		Hb->RefreshPage();
	}
	if (bWindowOpen) { RefreshGrid(); }
	UE_LOG(LogAsdaInven, Log,
		TEXT("[инвентарь] 5029: предмет %d (linkCode %d, подтип %d) использован, в ячейке %d:%d осталось %d, ")
		TEXT("вес стопки %d, вес персонажа %d"),
		ItemCode, Rec->LinkCode, Rec->ItemSubType, UseCont, SlotIndex, NewAmount, NewStackW, NewCharW);
}

// ★★★ ТАБЛИЦА ОТКАЗОВ 5029 — коды результата 0…14, ровно как их разбирает `switch` эталона
//   (item_inventory.c:39035-39218). Все тексты — из игровых данных (DT_GameText), своих не пишем.
//   ★ ПОПРАВКА ВОЛНЫ 8: коды 12/13/14 эталон выводит через `CControl_SetTooltipText(g_pChatWnd, …)`,
//   а 0/3…10 — через `CChat_AddTokenizedLines(g_pChatWnd, …, −11514, 10000)`. Это РАЗНЫЕ ОКНА:
//   первое — плашка `WARMESSAGE_WIN` 221, второе — журнал чата. До волны 8 оба канала были
//   сведены в один, и три строки уходили не туда; теперь 12/13/14 идут через `NoticePlank`.
void UAsdaInventorySubsystem::ApplyUseRefusal(int32 Result, int32 ItemCode)
{
	// ★ 09.10.2026 (автолопата): [ИСПРАВЛЕНИЕ ОРИГИНАЛА] отказ сервера на повтор инструмента цикла (код ≠ 11, например 9 «в
	//   городе» / 0 / 3): эталон таймер не трогает — он остаётся просроченным, и повтор 5028 уходит после каждого ответа с тем же
	//   нотисом, бесконечно. Порт сбрасывает цикл; ключ `asda.Shovel.AutoLoopFix 0` — как оригинал.
	if (Result != 11 && IsAutoLoopFixEnabled() && AutoDig.bActive && AutoDig.ItemCode == ItemCode)
	{
		AutoDigReset(*FString::Printf(TEXT("[ПОРТ] отказ 5029 код %d на повтор"), Result));
	}
	switch (Result)
	{
	case 0:  Notice(2020);         break;
	case 3:  Notice(9565);         break;
	case 4:  Notice(1214);         break;
	case 5:  Notice(2046);         break;
	case 6:  Notice(6953);         break;
	case 7:  Notice(6954);         break;
	case 8:  Notice(6955);         break;
	case 9:  Notice(9000);         break;
	case 10: Notice(9001);         break;
	// ★★★ ВОЛНА 8 (пункт 5 ревизии питомцев): КОДЫ 12/13/14 — ДРУГОЙ КАНАЛ.
	//   Эталон шлёт их через `CControl_SetTooltipText(g_pChatWnd, …)` (item_inventory.c:39205-39217),
	//   а это плашка `WARMESSAGE_WIN` 221, не строка чата. Ревизия назвала два пет-нотиса
	//   (106000/106001); третий (7007) лежит В ТОЙ ЖЕ ветке эталона и переведён вместе с ними
	//   (закон `no-scope-cutting`: чиним всю ветку, а не две строки из трёх).
	case 12: NoticePlank(7007);    break;
	case 13: NoticePlank(106000);  break;
	case 14: NoticePlank(106001);  break;
	case 11:
		// ★★★ Код 11 (0xB) — НЕ отказ, а РАЗРЕШЕНИЕ КОПАТЬ. Эталон (`item_inventory.c:39202-39204`):
		//   `case 0xB: Item_UseShovelingTool(this, *(int*)((char*)packet + 7)); return;`
		//   — аргумент это itemCode из +7, а не индекс слота.
		ApplyShovelingTool(ItemCode);
		break;
	default:
		// Эталон: `default: return;` — молча. Пишем в лог, чтобы неизвестный код не терялся.
		UE_LOG(LogAsdaInven, Log,
			TEXT("[инвентарь] 5029 result=%d (предмет %d) — эталон такой код игнорирует молча"),
			Result, ItemCode);
		break;
	}
}

// ★★★ КОПАНИЕ — порт `Item_UseShovelingTool@0xD0BFE0` (`item_inventory.c:39236-39465`).
//   Приходит кодом результата 11 канала 5029. Сервер на этом коде предмет НЕ списывает — списание
//   придёт отдельным 5029 с кодом 1 позже, по таймеру (`inventory_SendUseItemRequest@0xD13EA0`).
//
//   ДВА ГЕЙТА ОТКАЗА (оба — чат-нотис и `return 0`):
//     1) свободных ячеек контейнера расходников (60 шт) меньше одной ⇒ нотис **7505**
//        (`:39316-39328`, плюс `resetActiveAndCounter(g_pActionTimer)`);
//     2) вес персонажа ≥ 90 % от максимума ⇒ нотис **9006** (`:39330-39339`).
//        ⚠ Имена `curDurability`/`durabilityPercent` в декомпиле ВРУТ: `gap_43B[15]` и `gap_43B[13]`
//        — это ТЕКУЩИЙ и МАКСИМАЛЬНЫЙ ВЕС (в них же пишет `HUD_UpdateMiniWeightBar@0xE78FA0`).
//
//   ДАЛЬШЕ (только при `Net_GetCurrentMapType(localPlayer) == 0`; миснейм — это СОСТОЯНИЕ ДЕЙСТВИЯ `+47551` == 0, закон
//   net-getcurrentmaptype-is-misname; иначе вся ветка молча не исполняется — ★ ревизия six_items СРЕДН-2, гейт ниже):
//     • `player[1].bActionBusy = 1`;
//     • наружу уходит **4008** `Net_SendActionPacket(userIdx, x, y, z)` (`network.c:5027`);
//     • наружу уходит **4024** `Send_CharStatePos(target, x, y, z, 110)` — код действия 110 для
//       подтипа 33 и **114** для подтипа 34 (`network.c:5085`);
//     • состояние персонажа **40** (лопата) / **43** (авто-лопата);
//     • на кость `8_a1` вешается меш инструмента `ch4_98_Shoveling.jbm` / `ch4_98_AutoShovel_01.jbm`;
//     • для подтипа 34 — `timerStart(g_pActionTimer, 13000, itemCode)` (13 с) и `timer[8] = 34`.
void UAsdaInventorySubsystem::ApplyShovelingTool(int32 ItemCode)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(ItemCode) : nullptr;

	// ── Гейт 1: свободные ячейки контейнера расходников ──
	int32 Free = 0;
	if (const TArray<FAsdaItemInstance>* SpendC = Container(2))
	{
		for (const FAsdaItemInstance& It : *SpendC) { if (It.IsEmpty()) { ++Free; } }
	}
	if (Free < 1)
	{
		Notice(7505);
		UE_LOG(LogAsdaInven, Log,
			TEXT("[копание] предмет %d: свободных ячеек расходников 0 — нотис 7505 (эталон здесь же ")
			TEXT("зовёт resetActiveAndCounter и выходит)"), ItemCode);
		AutoDigReset(TEXT("7505 нет места (item_inventory.c:39326)"));   // ★ 09.10.2026: эталонный сброс цикла
		return;
	}
	// ── Гейт 2: вес ≥ 90 % ──
	const int32 CurW = Net ? Net->SelfWeightCur : -1;
	const int32 MaxW = Net ? Net->SelfWeightMax : -1;
	if (MaxW > 0 && ((double)CurW / (double)MaxW) * 100.0 >= 90.0)
	{
		Notice(9006);
		UE_LOG(LogAsdaInven, Log,
			TEXT("[копание] предмет %d: вес %d/%d = %.1f%% ≥ 90%% — нотис 9006"),
			ItemCode, CurW, MaxW, MaxW > 0 ? (double)CurW * 100.0 / (double)MaxW : 0.0);
		// ★ 09.10.2026 (автолопата): [ИСПРАВЛЕНИЕ ОРИГИНАЛА] эталон здесь цикл НЕ сбрасывает (`return 0` без
		//   `resetActiveAndCounter`, :39330-39339): таймер остаётся просроченным, повтор 5028 уходит после каждого ответа, а сервер
		//   на каждый 5028 снова шлёт 5429 (лишняя добыча) — бесконечно. Порт сбрасывает; ключ `asda.Shovel.AutoLoopFix 0` — как оригинал.
		if (IsAutoLoopFixEnabled() && AutoDig.bActive && AutoDig.ItemCode == ItemCode)
		{
			AutoDigReset(TEXT("[ПОРТ] 9006 вес ≥ 90 %"));
		}
		return;
	}

	const int32 Sub = Rec ? Rec->ItemSubType : -1;
	if (Sub != 33 && Sub != 34)
	{
		UE_LOG(LogAsdaInven, Log,
			TEXT("[копание] предмет %d подтипа %d — у эталона ветки нет, работа инструмента не начинается"),
			ItemCode, Sub);
		return;
	}

	// ★ Ревизия six_items СРЕДН-2 (09.10.2026): ГЕЙТ СОСТОЯНИЯ ДЕЙСТВИЯ. Эталон (item_inventory.c:39347 / ветка 34 — :39405):
	//   `if (!Net_GetCurrentMapType(LocalPlayer))` — вся ветка (занят, 4008, 4024, 40/43, меш, таймер) идёт только при
	//   состоянии действия 0; иначе — молча ничего (ни нотиса, ни лога у эталона). Состояние своего — общая точка
	//   `AAsdaPlayerController::KbEtalonActionState` (ход 1, сидение 7/8/9, рыбалка 60…67, лопата 40/43, эмоция, навык, бой,
	//   смерть, ★ В3 S7: и 68 «добыча» — прежде проверялось здесь признаком персонажа). Ключа отката нет: прежний порт
	//   (копание из любого состояния гасило рыбалку/ход/сидение) — дефект порта, не поведение оригинала.
	{
		UWorld* GW = GI ? GI->GetWorld() : nullptr;
		const AAsdaPlayerController* GPC = GW ? Cast<AAsdaPlayerController>(GW->GetFirstPlayerController()) : nullptr;
		const int32 ActSt = GPC ? GPC->KbEtalonActionState() : 0;   // эталонное `+47551` своего
		const int32 St = ActSt;   // ★ В3 S7: 68 уже в общей точке (прежде дописывалось здесь: `ActSt == 0 && 68 → 68`)
		if (St != 0)
		{
			UE_LOG(LogAsdaInven, Log, TEXT("[копание] предмет %d: состояние %d ≠ 0 — ветка эталона не исполняется (молча, как оригинал)"),
				ItemCode, St);
			// ★ 09.10.2026 (автолопата): [ИСПРАВЛЕНИЕ ОРИГИНАЛА] ответ на повтор пришёл не в покое (40/43 ещё идёт — у порта при
			//   `asda.Shovel.ClipEnd 0`): эталон молчит, таймер остаётся просроченным и повтор 5028 уходит после каждого ответа
			//   (сервер каждый раз шлёт 5429). Порт сбрасывает цикл; ключ `asda.Shovel.AutoLoopFix 0` — как оригинал.
			if (IsAutoLoopFixEnabled() && AutoDig.bActive && AutoDig.ItemCode == ItemCode)
			{
				AutoDigReset(TEXT("[ПОРТ] ответ 11 не в покое"));
			}
			return;
		}
	}

	// ── Остановка движения: 4008. Эталонный `Net_SendActionPacket` шлёт именно этот опкод. ──
	//   Его гейт — `g_bActionPending` (сервер считает нас идущими после 4006 до 4008; network.c:5016-5018): без незакрытого
	//   4006 эталон 4008 не шлёт — так же и `SendStop4008` порта (возврат false).
	bool bSent4008 = false;   // ушёл ли 4008 (для журнала)
	if (Net)
	{
		const AActor* StopAt = nullptr;   // чья позиция уйдёт в 4008 (свой персонаж)
		if (UWorld* W = GI->GetWorld())
		{
			if (APlayerController* PC = W->GetFirstPlayerController()) { StopAt = PC->GetPawn(); }
		}
		// ★ 26.09.2026: раньше сюда шли сырые `GetActorLocation().X/.Y` движка; тело 4008 требует ИГРОВЫЕ
		//   (gameX, gameZ) — перевод внутри `SendStop4008AtActor` через `AsdaCoord::UeToGame2D`.
		bSent4008 = Net->SendStop4008AtActor(StopAt);
	}

	// ★ Ревизия six_items (09.10.2026): **4024** `Send_CharStatePos(свой userNum, …, 110|114)` (item_inventory.c:39363/39409) —
	//   эталон шлёт его ПОСЛЕ 4008 и до состояния 40/43. Отправитель 4024 в порту есть с 27.09.2026 (`SendCharStatePos4024`,
	//   курс — общая точка `GetFacingDirGame`; прежний комментарий «отправителя нет» устарел). Сервер пишет код в своё состояние
	//   действия и раздаёт соседям 4025 110/114 (у них — 40/43). Конец — 5428 по концу клипа (`OnShovelClipEnd`).
	//   Ключ отката `asda.Shovel.NetSync 0` (прежний порт: 4024 не шлётся).
	AAsdaPlayerCharacter* SelfCh = nullptr;   // свой персонаж
	if (UWorld* SW = GI ? GI->GetWorld() : nullptr)
	{
		if (APlayerController* SPC = SW->GetFirstPlayerController()) { SelfCh = Cast<AAsdaPlayerCharacter>(SPC->GetPawn()); }
	}
	const int32 ActCode = (Sub == 33) ? 110 : 114;   // код действия 4024
	bool bSent4024 = false;                          // ушёл ли 4024
	if (Net && SelfCh && AAsdaPlayerCharacter::IsShovelNetSync())
	{
		const FVector2D Face = SelfCh->GetFacingDirGame();
		bSent4024 = Net->SendCharStatePos4024(ActCode, (float)Face.X, (float)Face.Y);
	}
	// ★ Решение владельца 09.10.2026 (п. 5): `CharFx_AutoSabzil_State(LocalPlayer, …, 40|43)` (item_inventory.c:39364/39410) —
	//   состояние лопаты: `bActionBusy = 1`, оружие за спину; конец — по концу клипа копания (six_items СРЕДН-1); клип, удары,
	//   меш инструмента и эффекты ставит персонаж (★ 10.10.2026, копание).
	// ★ 10.10.2026 (копание): источник «свой» (стартовый эффект `c-shovel-a_01` у обоих инструментов) и прежний режим таймера
	//   (`sub_928DED(g_pActionTimer)` читается ДО `set_o8` ниже — по нему эталон решает, менять ли меш инструмента).
	if (SelfCh) { SelfCh->EnterEtalonShovelState(Sub == 33 ? 40 : 43, EAsdaShovelEntry::Self, AutoDig.Mode); }
	// ★ 09.10.2026 (автолопата): ТАЙМЕР ЦИКЛА. Ветка 33 (:39397-39398): `resetActiveAndCounter` + `set_o8(33)` — ручная лопата
	//   гасит цикл. Ветка 34 (:39457-39458): `timerStart(g_pActionTimer, 13000, itemCode)` + `set_o8(34)`. Режим (+0x20) у эталона
	//   выбирает меш на кости `8_a1` (★ 10.10.2026: прежний режим уходит персонажу в `EnterEtalonShovelState` выше). Ключ `asda.Shovel.AutoLoop 0` —
	//   прежний порт: цикла нет (таймер не стартует).
	if (Sub == 33)
	{
		AutoDigReset(TEXT("ручная лопата 33 (item_inventory.c:39397)"));
		AutoDig.Mode = 33;
	}
	else if (IsAutoLoopEnabled())
	{
		AutoDigStart(13000, ItemCode);
		AutoDig.Mode = 34;
	}

	// ★ 10.10.2026 (копание): клип копания, удары по порогам (звук + эффект), меш инструмента на `8_a1`, стартовый эффект, конец
	//   по треку клипа → 5428 — `AAsdaPlayerCharacter::EnterEtalonShovelState`/`TickShovel`; итог сервера 5430 (нотис 9003/9004,
	//   эффект и звук итога) — `UAsdaWorldSubsystem::OnMiningResult`. Таймер авто-лопаты 13 с и цикл — `AutoDigStart`/`TickAutoDig`.
	UE_LOG(LogAsdaInven, Log,
		TEXT("[копание] предмет %d подтип %d: гейты пройдены (свободно ячеек %d, вес %d/%d), 4008 %s, 4024 %d %s; ")
		TEXT("состояние %d (клип, удары, инструмент «%s» — у персонажа)%s"),
		ItemCode, Sub, Free, CurW, MaxW,
		bSent4008 ? TEXT("отправлен") : TEXT("не нужен (хода не было — гейт g_bActionPending)"), ActCode,
		bSent4024 ? TEXT("отправлен") : TEXT("НЕ отправлен (нет связи / ключ asda.Shovel.NetSync 0)"),
		(Sub == 33) ? 40 : 43,
		(Sub == 33) ? TEXT("ch4_98_Shoveling.jbm") : TEXT("ch4_98_AutoShovel_01.jbm"),
		(Sub == 34) ? TEXT(", таймер 13000 мс") : TEXT(""));
}

// ═══════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ ЦИКЛ АВТОЛОПАТЫ (09.10.2026, ревизия six_fixes п. 6 / СРЕДН-2; требование владельца: копает → заканчивает копку →
//     поднимает добычу с земли → снова копает, пока игрок не прервёт). Реверс — `Reports/owner_1009/autoshovel.md`.
//   СТАРТ: 5029 код 11 по 11/34 → `ApplyShovelingTool` → `AutoDigStart(13000)`; слот/контейнер — гейт 11/34 в `UseConsumable`.
//   КАДР (game_main.c:2271-2307, `TickAutoDig`): `updateTimeoutLevel` → 1 в каждом кадре, пока таймер активен и прошло ≥ 13 с;
//     тогда при `!get_o16` (подбор 5012 не в пути) — `itemPickupGroundItem` + уровень +1; при уровне ≥ 3 — запись по коду
//     инструмента: 11/34 → `inventory_SendUseItemRequest(слот)`, иначе сброс. Ответ 5029 код 11 → снова `timerStart` (уровень 0).
//     Значит, за цикл — до трёх попыток подбора (по одной на кадр без подбора в пути) либо меньше, если уровень дорос до 3
//     по просрочке (> 1,5 с); пока ответа на повтор нет, подбор продолжается каждый кадр (как у эталона).
//   ОСТАНОВКА (эталон): состояние своего ∉ {0, 40, 43} (покадрово, entity_char.c:8064-8074), 7505, ручная лопата 33, 9001 в
//     повторе, смена карты/сброс сессии, нет записи / не 11/34. [ПОРТ] (`asda.Shovel.AutoLoopFix`): 9006, 2040 и 9002 в повторе,
//     код инструмента в слоте ≠ коду таймера, отказ 5029 на повтор, ответ 11 не в покое.
// ═══════════════════════════════════════════════════════════════════════════════════════════════
// Ключ отката цикла автолопаты.
static ASDA_DEV_CVAR(int32, CVarAsdaShovelAutoLoop,
	TEXT("asda.Shovel.AutoLoop"), 1,
	TEXT("Авто-лопата (11/34): 1 (по умолчанию) = как оригинал — цикл «копка → через 13 с подбор добычи рядом (5012) → повтор 5028», ")
	TEXT("пока игрок не прервёт (ход, бой, смерть, сидение, навык, эмоция, рыбалка, 7505, ручная лопата, 9001, смена карты); ")
	TEXT("0 = прежний порт — одна копка, цикла нет."),
	ECVF_Default);
// Ключ отката исправлений дефектов оригинала в цикле.
static ASDA_DEV_CVAR(int32, CVarAsdaShovelAutoLoopFix,
	TEXT("asda.Shovel.AutoLoopFix"), 1,
	TEXT("[ПОРТ] Исправления дефектов оригинала в цикле авто-лопаты: 1 (по умолчанию) = цикл сбрасывается при 9006 (вес ≥ 90 %), ")
	TEXT("2040 (транспорт) и 9002 в повторе, при чужом коде в слоте инструмента, при отказе 5029 на повтор и при ответе 11 не в покое; ")
	TEXT("0 = как оригинал — таймер остаётся просроченным и повтор (с нотисом / лишней добычей) идёт бесконечно."),
	ECVF_Default);

bool UAsdaInventorySubsystem::IsAutoLoopEnabled() { return CVarAsdaShovelAutoLoop.GetValueOnGameThread() != 0; }
bool UAsdaInventorySubsystem::IsAutoLoopFixEnabled() { return CVarAsdaShovelAutoLoopFix.GetValueOnGameThread() != 0; }

void UAsdaInventorySubsystem::AutoDigStart(int32 DurMs, int32 ItemCode)
{
	// `timerStart@0xD0C820`: +0x18 = 1, +0x04/+0x0C = timeGetTime, +0x10 = 0, +0x14 = длительность, +0x1C = код, +0x2C = 0.
	AutoDig.bActive = true;
	AutoDig.StartS = FPlatformTime::Seconds();
	AutoDig.DurationMs = DurMs;
	AutoDig.ItemCode = ItemCode;
	AutoDig.Level = 0;
	UE_LOG(LogAsdaInven, Log, TEXT("[автолопата] старт таймера: предмет %d, %d мс, слот %d контейнер %d (повторов в цикле было %d)"),
		ItemCode, DurMs, AutoDig.Slot, AutoDig.Cont, AutoDig.Repeats);
}

void UAsdaInventorySubsystem::AutoDigReset(const TCHAR* Why)
{
	if (!AutoDig.bActive && AutoDig.ItemCode == 0) { return; }   // нечего снимать (журнал не засоряем)
	UE_LOG(LogAsdaInven, Log, TEXT("[автолопата] AUTOSABZIL : END — %s (предмет %d, уровень %d, повторов %d)"),
		Why ? Why : TEXT("?"), AutoDig.ItemCode, AutoDig.Level, AutoDig.Repeats);
	// `resetActiveAndCounter@0x966850`: только +0x18 и +0x1C (имя IDA обманчиво — «счётчик» +0x2C не трогается).
	AutoDig.bActive = false;
	AutoDig.ItemCode = 0;
	AutoDig.Repeats = 0;
}

int32 UAsdaInventorySubsystem::ShovelUseGate() const
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return 0; }
	// 2040: `listContainsByTypeId(состояния своего, 15, 2)` — на транспорте.
	if (const UAsdaEffectsSubsystem* Eff = GI->GetSubsystem<UAsdaEffectsSubsystem>())
	{
		if (Eff->FindByTypeSub(AsdaSelf::GetUserIdxOr(GI, INDEX_NONE), /*Id*/ 15, /*Type*/ 2)) { return 2040; }
	}
	// 9002: `Net_GetCurrentMapType(LP) == 40 || == 43` (миснейм: состояние действия +47551) — уже копает.
	UWorld* W = GI->GetWorld();
	const AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
	const int32 St = PC ? PC->KbEtalonActionState() : 0;   // эталонное +47551 своего
	if (St == 40 || St == 43) { return 9002; }
	// 9001: `g_pPartyTargetObj` (миснейм — элитный магазин открыт) || `sub_922D5D(g_pGachaScene)` (сцена премиум-гачи).
	const UAsdaPremiumShopSubsystem* EliteShop = GI->GetSubsystem<UAsdaPremiumShopSubsystem>();
	const UAsdaPremiumGachaSubsystem* Gacha = GI->GetSubsystem<UAsdaPremiumGachaSubsystem>();
	if ((EliteShop && EliteShop->IsOpen()) || (Gacha && Gacha->IsSceneOpen())) { return 9001; }
	return 0;
}

void UAsdaInventorySubsystem::TickAutoDig()
{
	if (!AutoDig.bActive) { return; }
	UGameInstance* GI = GetGameInstance();
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
	const AAsdaPlayerCharacter* Ch = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!PC || !Ch || !Net) { return; }

	// ── 1. Покадровый сброс по состоянию своего (entity_char.c:8064-8074): `+47551 ∉ {40, 43}` и `≠ 0` → сброс.
	//   Ход 1, бой 5, смерть 6, сидение 7–9, навык/каст, эмоция, рыбалка 60…67 — из общей точки `KbEtalonActionState`;
	//   ★ В3 S7: и 68 «добыча» (прежде — признаком персонажа здесь).
	const int32 KbSt = PC->KbEtalonActionState();   // эталонное +47551 своего
	const int32 St = KbSt;   // ★ В3 S7: 68 уже в общей точке (прежде дописывалось здесь: `KbSt == 0 && 68 → 68`)
	if (St != 0 && St != 40 && St != 43)
	{
		AutoDigReset(*FString::Printf(TEXT("состояние своего %d (entity_char.c:8073)"), St));
		return;
	}

	// ── 2. `updateTimeoutLevel@0x966750`: до срока — 0; после — уровень просрочки 1/2/3 (> 500 / 1000 / 1500 мс) и 1.
	const double ElapsedMs = (FPlatformTime::Seconds() - AutoDig.StartS) * 1000.0;   // `elapsedMs`
	if (ElapsedMs < (double)AutoDig.DurationMs) { return; }
	const double OverMs = ElapsedMs - (double)AutoDig.DurationMs;   // просрочка
	if (OverMs > 1500.0 && AutoDig.Level < 3)      { AutoDig.Level = 3; }
	else if (OverMs > 1000.0 && AutoDig.Level < 2) { AutoDig.Level = 2; }
	else if (OverMs > 500.0 && AutoDig.Level < 1)  { AutoDig.Level = 1; }

	// ── 3. «AUTOSABZIL : IsCheck»: подбор не в пути (`!get_o16(g_pUIManager)`) → `itemPickupGroundItem` + `incRefField11`.
	//   ★ 09.10.2026 (МЕЛК-1): флаг читается со сторожем [ПОРТ] `asda.Loot.PickPendingGuardSec` (0 = эталон).
	if (!Net->IsItemPickPending())
	{
		PC->PickupGroundItemEtalon(TEXT("цикл автолопаты"));
		++AutoDig.Level;
	}

	// ── 4. Уровень ≥ 3 → запись по коду инструмента (+0x1C): 11/34 → повтор по слоту, иначе / записи нет → сброс.
	if (AutoDig.Level >= 3)
	{
		const UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>();
		const FAsdaItemRecord* Rec = IT ? IT->GetItem(AutoDig.ItemCode) : nullptr;
		if (Rec && Rec->ItemType == 11 && Rec->ItemSubType == 34)
		{
			SendAutoDigRepeat();
		}
		else
		{
			AutoDigReset(TEXT("AUTOSABZIL : END : GameProcess — записи инструмента нет / не 11/34 (game_main.c:2293-2305)"));
		}
	}
}

// Порт `inventory_SendUseItemRequest@0xD13EA0` (item_inventory.c:40964-41120).
void UAsdaInventorySubsystem::SendAutoDigRepeat()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!Net || !IT) { return; }
	// Контейнер: `get_oA == 1` → сумка (`g_InventoryBag`), иначе расходники (`g_InventoryArray2`).
	const int32 Cont = (AutoDig.Cont == 1) ? 1 : 2;   // контейнер повтора
	const FAsdaItemInstance* Cell = GetCell(Cont, AutoDig.Slot);   // слот инструмента
	if (!Cell)
	{
		// Слота нет (номер вне контейнера — старт не через `UseConsumable`): у эталона `slotEntry` всегда не null — читается мусор.
		if (IsAutoLoopFixEnabled()) { AutoDigReset(*FString::Printf(TEXT("[ПОРТ] слота %d:%d нет"), Cont, AutoDig.Slot)); }
		return;
	}
	// Гейты «ответ на действие с предметом ещё не пришёл» (`sub_93723F` = `bActionPending`) и «мёртв» (`!= 6`) — молча.
	if (bActionPending) { return; }
	UWorld* W = GI->GetWorld();
	const AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
	if (PC && PC->KbEtalonActionState() == 6) { return; }
	// Запись по коду В СЛОТЕ (у эталона нет записи — молча, таймер висит просроченным).
	const FAsdaItemRecord* Rec = Cell->IsEmpty() ? nullptr : IT->GetItem(Cell->ItemCode);
	// [ИСПРАВЛЕНИЕ ОРИГИНАЛА] эталон сверяет запись по коду ТАЙМЕРА (game_main.c:2286), а применяет СЛОТ: инструмент пропал
	//   (сломан, продан, перенесён) — таймер висит вечно; в слоте другой 11-й предмет — применится он. Порт сверяет код в слоте.
	if (IsAutoLoopFixEnabled() && (!Rec || Cell->ItemCode != AutoDig.ItemCode))
	{
		AutoDigReset(*FString::Printf(TEXT("[ПОРТ] в слоте %d:%d предмет %d, а не инструмент цикла %d"),
			Cont, AutoDig.Slot, Cell->ItemCode, AutoDig.ItemCode));
		return;
	}
	if (!Rec || Rec->ItemType != 11) { return; }
	const int32 Gate = ShovelUseGate();   // 2040 / 9002 / 9001 — та же точка, что у `UseConsumable`
	if (Gate == 2040)
	{
		Notice(2040);   // ★ 09.10.2026 (МЕЛК-2); [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка
		// [ИСПРАВЛЕНИЕ ОРИГИНАЛА] эталон без сброса: таймер просрочен, нотис повторяется КАЖДЫЙ кадр (5028 не уходит).
		if (IsAutoLoopFixEnabled()) { AutoDigReset(TEXT("[ПОРТ] 2040 транспорт")); }
		return;
	}
	if (Gate == 9002)
	{
		Notice(9002);
		// [ИСПРАВЛЕНИЕ ОРИГИНАЛА] у эталона недостижимо (43 = 9,034 с < 13 с), но при затянувшемся 40/43 нотис шёл бы каждый кадр.
		if (IsAutoLoopFixEnabled()) { AutoDigReset(TEXT("[ПОРТ] 9002 в повторе")); }
		return;
	}
	if (Gate == 9001)
	{
		Notice(9001);
		AutoDigReset(TEXT("9001 элитный магазин / сцена гачи (item_inventory.c:41042)"));
		return;
	}
	// ★ [ОРИГИНАЛ] 05.11.2026: 7007 — `listProbeEntryTypesSetState` + код вне 1…2000 (item_inventory.c:41046-41050): плашка,
	//   5028 не уходит, таймер НЕ сбрасывается — после снятия состояния повтор продолжается сам (как эталон). Общая точка
	//   `AsdaItemUseStateGate` (ключ `asda.Item.StateLockGate`); сервер на тот же случай отвечает 5029 кодом 12.
	if (AsdaItemUseStateGate::RefuseIfStateLocked(GI, Cell->ItemCode, TEXT("повтор автолопаты (inventory_SendUseItemRequest)"))) { return; }
	const bool bOk = Net->SendSpendItemUse5028(Cont, AutoDig.Slot, 0);
	if (bOk)
	{
		bActionPending = true; ActionSentAtS = FPlatformTime::Seconds(); ++AutoDig.Repeats;
		UE_LOG(LogAsdaInven, Log, TEXT("[автолопата] повтор №%d: 5028 контейнер %d слот %d (предмет %d, уровень %d) ушёл"),
			AutoDig.Repeats, Cont, AutoDig.Slot, AutoDig.ItemCode, AutoDig.Level);
	}
	else
	{
		// Нет связи: эталон шлёт каждый кадр без проверки — у порта повтор тоже каждый кадр, журнал — только Verbose.
		UE_LOG(LogAsdaInven, Verbose, TEXT("[автолопата] повтор: 5028 не ушёл (нет связи)"));
	}
}
