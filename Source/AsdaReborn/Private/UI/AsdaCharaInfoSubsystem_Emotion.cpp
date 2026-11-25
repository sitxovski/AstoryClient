// Copyright (c) 2026 @sitxovski (Discord)
// AsdaCharaInfoSubsystem_Emotion.cpp — ★ R3-c 12.11.2026 (разрез AsdaCharaInfoSubsystem.cpp, дословный перенос): эмоции: таблица, проигрывание, подсказки, курсор, перетаскивание на быстрые слоты.
//   Общее для частей — в AsdaCharaInfoSubsystem_Private.h; шапка и реверс — в AsdaCharaInfoSubsystem.cpp.
#include "UI/AsdaCharaInfoSubsystem.h"

#include "World/AsdaTargetHotkeys.h"   // ★ 25.10.2026: функция 2/4 «атаковать» — Input_HandleTargetInteractClick

#include "AsdaCharaInfoSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaCharaInfoSubsystem*.cpp

// ★ 28.10.2026 [ПОРТ] (решение владельца): плывущему — только ход и предметы (тело — AsdaPlayerCharacter_Move.cpp).
bool AsdaSwimActionLocked(const class AAsdaPlayerCharacter* Ch);

namespace CharInfoLoc {
	static constexpr int32 kStatTipFirst = 1401;    // 1401…1413 — описания тех же строк

	static constexpr int32 kOnVehicle    = 2040;    // отказ действия верхом
	static constexpr int32 kNoSoulmate   = 6035;    // «нужен соулмейт» (жонглирование/стойка/ухаживание)
}

// ═══════════════════════════ ЭМОЦИИ ═════════════════════════════════════════════════════════════
// Порт `UI_DispatchTargetContextMenu@0xC532D0` (мисномер эталона; по сути `Emote_Execute`).
//   Категория 0 — эмоции: локально играем клип и шлём серверу 4024 с кодом действия.
//   Категории 1 (отряд) и 2 (функции) — это НЕ анимации, а команды к другим системам.
// Таблица эталона: индекс в лотке «обычных» → (состояние анимации, код действия для сервера).
struct FAsdaEmoteRow { int32 AnimState; int32 ActionCode; bool bNeedsSoulmate; };
static const FAsdaEmoteRow kEmoteRows[12] =
{
	{ 33, 100, false },  // 0  act_happy
	{ 34, 101, false },  // 1  act_angry
	{ 35, 102, false },  // 2  act_cry
	{ 36, 103, false },  // 3  act_thank
	{ 37, 104, false },  // 4  act_hallo
	{ 38, 105, false },  // 5  act_comeon
	{ 39, 106, false },  // 6  act_fight
	{ 41, 112, false },  // 7  act_dance01
	{ 42, 113, false },  // 8  act_dance02
	{ 45, 116, true  },  // 9  act_juggling  — нужен соулмейт
	{ 46, 117, true  },  // 10 act_upperarm  — нужен соулмейт
	{ 44, 115, true  },  // 11 act_courtship — нужен соулмейт
};

bool UAsdaCharaInfoSubsystem::PlayEmotion(int32 Code)
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return false; }
	const int32 Group = EmotionGroupOf(Code);
	const int32 Idx   = EmotionIndexOf(Code);

	UAsdaNetSubsystem* Net    = GI->GetSubsystem<UAsdaNetSubsystem>();
	UAsdaWorldSubsystem* World = GI->GetSubsystem<UAsdaWorldSubsystem>();
	AAsdaPlayerCharacter* Ch = (World && Net)
		? Cast<AAsdaPlayerCharacter>(World->ActorByUserIdx(AsdaSelf::GetUserIdxOr(GI, INDEX_NONE))) : nullptr;   // ★ волна 1 (E2)

	// ★ 28.10.2026 [ПОРТ] (решение владельца): плывущий не играет эмоций и не садится — МОЛЧА (вставать можно: сидя
	//   в воду не попасть). Ключ `asda.Swim.ActionGate`.
	if (AsdaSwimActionLocked(Ch))
	{
		UE_LOG(LogAsdaCharInfo, Log, TEXT("[эмоции] код %d отклонён: персонаж плывёт"), Code);
		return false;
	}
	// ── Общий гейт эталона: верхом запрещено ВСЁ (нотис 2040) ─────────────────────────────────
	if (Ch && Ch->IsMounted())
	{
		Notice(CharInfoLoc::kOnVehicle);
		return false;
	}

	if (Group == 0)
	{
		if (Idx < 0 || Idx >= 12) { return false; }
		const FAsdaEmoteRow& R = kEmoteRows[Idx];
		// ★ Гейт эталона: обычные эмоции идут ТОЛЬКО из состояния 0 (стоя). На бегу эталонная
		//   ветка «сначала остановиться» мертва (дефект оригинала, п. 6) — эмоция просто не играет.
		// ★ 24.10.2026 (выход из сидения 1:1): гейт эталона — состояние действия == 0 (дизасм 0xC53349: `Net_GetCurrentMapType`,
		//   МИСНЕЙМ), поэтому и в «встаёт» (9, у порта `AnimState` уже Idle) эмоции нет. Ключ `asda.Char.SitExitEtalon 0` — прежнее.
		// ★ 05.11.2026 [ОРИГИНАЛ] (`World/AsdaSkillCastLock`): и во время своего навыка (25/24/14/13/27) — у порта `AnimState` при клипе
		//   навыка остаётся Idle, и эмоция ложилась поверх клипа. Молча, как эталон.
		if (AsdaSkillCastLock::EmoteBlocked(GI)) { return false; }
		if (Ch && (Ch->AnimState != EAsdaPlayerState::Idle || (AAsdaPlayerCharacter::IsSitExitEtalon() && Ch->GetSitPhase() != 0)))
		{
			UE_LOG(LogAsdaCharInfo, Log,
				TEXT("[эмоции] код %d отклонён: персонаж не в покое (эталон в этом случае молчит)"), Code);
			return false;
		}
		// ★ Три «парные» эмоции требуют соулмейта — иначе нотис 6035.
		UAsdaHotbarSubsystem* Hb = GI->GetSubsystem<UAsdaHotbarSubsystem>();
		if (R.bNeedsSoulmate && (!Hb || !Hb->HasSoulmate()))
		{
			Notice(CharInfoLoc::kNoSoulmate);
			return false;
		}
		// Локально играем сразу (эталон: `CharFx_AutoSabzil_State` до отправки пакета).
		const float ClipLen = Ch ? Ch->PlayEmotionClip(R.AnimState) : 0.f;   // длина клипа эмоции, 0 — не заиграл
		// И шлём серверу — он разошлёт 4025 всему сектору (включая нас; повторное проигрывание
		//   безвредно: клип просто перезапустится с нулевого кадра, как и в эталоне).
		//   ★ 27.09.2026 (pkt4024_facing.md): тело +5/+9 — НАПРАВЛЕНИЕ взгляда, не позиция. Эталон
		//   `UI_DispatchTargetContextMenu` (ui_core.c:32350-32575) у КАЖДОГО из 13 кодов берёт
		//   `thunk_copyVec3Field5(&effectSlots[112].gap_298[12])` — поле курса (`charComputeFacingAngleFromDir`).
		if (Net && Ch)
		{
			const FVector2D Face = Ch->GetFacingDirGame();   // единичный курс (x, z)
			Net->SendCharStatePos4024(R.ActionCode, (float)Face.X, (float)Face.Y);
		}
		// ★★★ 29.10.2026 [ОРИГИНАЛ] (баг «у жонглирования нет мячей»): реквизит СВОЕГО игрока. Эталон спавнит его прямо здесь,
		//   после `Send_CharStatePos` (ui_core.c:32470-32480 мяч, 32515-32560 цветы); 4025 отправителю НЕ приходит, поэтому
		//   ветка соседа (`OnCharAction`) своему его не даст никогда. Единая точка — `UAsdaEffectsSubsystem::SpawnEmoteProps`.
		//   Ключ `asda.Emote.Props 0` — прежний порт (у себя реквизита нет).
		if (Ch && ClipLen > 0.f && UAsdaEffectsSubsystem::IsEmotePropsEtalon())
		{
			if (UAsdaEffectsSubsystem* Fx = GI->GetSubsystem<UAsdaEffectsSubsystem>())
			{
				Fx->SpawnEmoteProps(AsdaSelf::GetUserIdxOr(GI, INDEX_NONE), R.AnimState);
			}
		}
		UE_LOG(LogAsdaCharInfo, Log, TEXT("[эмоции] код %d → состояние %d, серверный код %d"),
			Code, R.AnimState, R.ActionCode);
		return true;
	}

	if (Group == 1 && AsdaTargetHotkeys::IsPartyActionsEnabled())
	{
		// ★ 25.10.2026 [ОРИГИНАЛ] кат. 1 (ui_core.c:32105-32157): отряд — `UAsdaPartySubsystem::DispatchPartyMenu`. Цель «пригласить» —
		//   выбранный ПЕРСОНАЖ (`!g_nInputControlState`; моб/NPC → −1, ветки нет).
		UAsdaPartySubsystem* Party = GI->GetSubsystem<UAsdaPartySubsystem>();
		if (!Party) { return false; }
		AAsdaPlayerController* PC = Ch ? Cast<AAsdaPlayerController>(Ch->GetController()) : nullptr;
		const AAsdaPlayerCharacter* Tgt = PC ? Cast<AAsdaPlayerCharacter>(PC->TargetActor.Get()) : nullptr;
		int32 TU = -1; FString TName;
		if (Tgt && World)
		{
			TU = World->FindUserIdxByActor(Tgt);
			int32 ClassId = 0, Lvl = 0, Clan = -1;
			World->GetNetPlayerInfo(TU, TName, ClassId, Lvl, Clan);
		}
		return Party->DispatchPartyMenu(Idx, TU, TName, TEXT("функции 1/x (эмоция 10x / клавиши 48–50)"));
	}
	if (Group == 1)
	{
		// Отряд: 0 пригласить, 1 выйти, 2 исключить, 3 (эталон только печатает «вы не лидер»).
		//   Подсистемы отряда в порту ещё нет — честно фиксируем в логе, не выдумывая пакеты.
		UE_LOG(LogAsdaCharInfo, Warning,
			TEXT("[эмоции] действие отряда %d: подсистема отряда в порт ещё не портирована"), Idx);
		return false;
	}

	if (Group == 2)
	{
		// Функции. Порт делает ровно те, для которых системы уже есть.
		switch (Idx)
		{
		case 0:   // «сесть/встать» — эталон `charToggleSitStand@0xC54A70`
			// ★ 27.09.2026 (pkt4024_facing.md §6): вызов эталона `UI_DispatchTargetContextMenu` case 2/0 (ui_core.c:32162)
			//   → `charToggleSitStand` целиком: стоп 4008 на ходу, гейты «мёртв/занят/садится-встаёт», 4024, иконки.
			//   Единая точка — `AAsdaPlayerController::ToggleSitStand`. Ключ 0 — прежняя ветка ниже.
			if (AAsdaPlayerController::IsSitToggleEtalon())
			{
				AAsdaPlayerController* PC = Ch ? Cast<AAsdaPlayerController>(Ch->GetController()) : nullptr;
				if (!PC) { return false; }
				PC->ToggleSitStand(TEXT("окно действий: функции → сесть/встать"));
				return true;
			}
			if (Ch && Ch->AnimState != EAsdaPlayerState::Dead)
			{
				const bool bSit = (Ch->AnimState != EAsdaPlayerState::Sit);
				Ch->SetAnimState(bSit ? EAsdaPlayerState::Sit : EAsdaPlayerState::Idle);
				// ★ 27.09.2026 (pkt4024_facing.md): +5/+9 — НАПРАВЛЕНИЕ взгляда (`charToggleSitStand@0xC54A70`,
				//   entity_char.c:45248/45268: `effectSlots[112].gap_298[12]` и для 109, и для 107), не позиция.
				if (Net && Ch)
				{
					const FVector2D Face = Ch->GetFacingDirGame();   // единичный курс (x, z)
					Net->SendCharStatePos4024(bSit ? 107 : 109, (float)Face.X, (float)Face.Y);
				}
				return true;
			}
			return false;
		case 1:   // «поднять предмет» — эталон `UI_DispatchTargetContextMenu` кат. 2 case 1 (ui_core.c:32164-32165)
			// ★ 09.10.2026 (автолопата): `itemPickupGroundItem(thisPtr)` — общая точка порта
			//   `AAsdaPlayerController::PickupGroundItemEtalon` (её же зовёт цикл автолопаты); второй копии нет.
		{
			AAsdaPlayerController* PC = Ch ? Cast<AAsdaPlayerController>(Ch->GetController()) : nullptr;
			if (!PC) { return false; }
			PC->PickupGroundItemEtalon(TEXT("окно действий: функции → поднять"));
			return true;
		}
		// ★★★ 25.10.2026 [ОРИГИНАЛ] «атаковать» (код 204) — эталон кат. 2 case 4 (ui_core.c:32201-32203): `Input_HandleTargetInteractClick(
		//   thisPtr, result)`. Сюда же — клавиша 59 «ACTION ATTACK» (обёртка `0xE3DAF0`). Тело — `AsdaTargetHotkeys::AttackSelectedTarget`.
		//   Ключ отката `asda.Keys.TargetActions 0` — прежнее «функция не портирована».
		// ★★★ 25.10.2026 [ОРИГИНАЛ] 205 «осмотр» — кат. 2 case 5 (ui_core.c:32204-32207): `if (!g_nInputControlState)
		//   otherCharaWnd_SetTargetUser(…, g_nTargetUserIdx)` — цель-персонаж есть в реестре → 9419 «запрос в полёте» или 5478
		//   (`antiCheat_sendItemFlagDigest`) и окно 4200 ждёт ответ. Общая точка с пунктом меню 2006 — `RequestCharInfo`.
		case 5:
		{
			if (!AsdaTargetHotkeys::IsPartyActionsEnabled()) { break; }
			AAsdaPlayerController* PC = Ch ? Cast<AAsdaPlayerController>(Ch->GetController()) : nullptr;
			const AAsdaPlayerCharacter* Tgt = PC ? Cast<AAsdaPlayerCharacter>(PC->TargetActor.Get()) : nullptr;
			UAsdaPlayerMenuSubsystem* PM = GI->GetSubsystem<UAsdaPlayerMenuSubsystem>();
			if (!Tgt || !World || !PM || !Net) { return false; }   // цель не персонаж — ветки нет
			const bool bSelf = (Tgt == Ch);
			const int32 TU = bSelf ? AsdaSelf::GetUserIdxOr(GI, -1) : World->FindUserIdxByActor(Tgt);
			if (TU < 0) { return false; }                           // `CharManager_FindByUserIdx` → null: молча
			const int32 Cid = bSelf ? Net->SelectedCharId : World->GetNetPlayerCharId(TU);
			FString TName; int32 ClassId = 0, Lvl = 0, Clan = -1;
			if (bSelf) { TName = World->GetSelfCharName(); } else { World->GetNetPlayerInfo(TU, TName, ClassId, Lvl, Clan); }
			return PM->RequestCharInfo(TU, Cid, TName, TEXT("функции 2/5 (эмоция 205 / клавиша 61)"));
		}
		// ★★★ 25.10.2026 [ОРИГИНАЛ] 206 «общая цель» — кат. 2 case 6 (ui_core.c:32208-32285): цель помеченного соотрядника.
		case 6:
			if (!AsdaTargetHotkeys::IsPartyActionsEnabled()) { break; }
			if (UAsdaPartySubsystem* Party = GI->GetSubsystem<UAsdaPartySubsystem>())
			{
				return Party->TakeSharedTarget(TEXT("функции 2/6 (эмоция 206 / клавиша 63)"));
			}
			return false;
		case 4:
		{
			if (!AsdaTargetHotkeys::IsEnabled()) { break; }
			AAsdaPlayerController* PC = Ch ? Cast<AAsdaPlayerController>(Ch->GetController()) : nullptr;
			return PC && AsdaTargetHotkeys::AttackSelectedTarget(PC, TEXT("функции 2/4 (эмоция 204 / клавиша 59)"));
		}
		case 7:   // «личная лавка» (код 207) — эталон `UI_DispatchTargetContextMenu` кат. 2 case 7 (ui_core.c:32293-32296)
			// ★ 29.09.2026 (волна Л1 торговли): `if (!sub_934E86(g_pSellWindow)) netSendPlayerInfo6300(g_pSellWindow)` —
			//   окно подготовки 22000 открыто ⇒ молча. Гейты самой отправки (элитный магазин / ремесло — молча, уровень < 10 —
			//   107080) — внутри `UAsdaSelfStoreSubsystem::SendReady6300`. Сюда же приходят хоткей 65 и доп-меню cmd 2.
			if (UAsdaSelfStoreSubsystem* SS = GI->GetSubsystem<UAsdaSelfStoreSubsystem>())
			{
				return SS->DispatchPrivateShop(TEXT("функции 2/7 (эмоция 207 / хоткей 65 / доп-меню 2)"));
			}
			return false;
		// ★ 29.09.2026 (волна О1 торговли): 202 «обмен предметами» / 203 «обмен личными вещами» — эталон
		//   `UI_DispatchTargetContextMenu` case 2/2 и 2/3 (ui_core.c:32166-32198): `!g_nInputControlState`, цель — выбранный
		//   ПЕРСОНАЖ, не я → `CExchange_SendRequest` (тип 0) / `CExchange_SendAcceptRequest` (тип 1, МИСНЕЙМ) +
		//   `sub_931B23(g_pQuickSlotBar)`. Семи гейтов меню 2004/2005 и гейта «окно открыто» эталон здесь НЕ проверяет.
		case 2:
		case 3:
		{
			UAsdaExchangeP2PSubsystem* P2P = GI->GetSubsystem<UAsdaExchangeP2PSubsystem>();
			return P2P && P2P->SendRequestFromSelectedTarget(/*bPersonal*/ Idx == 3);
		}
		// ★★★ 17.10.2026 (синхронизация боя, Ф4): 208 «режим боя» — эталон `UI_DispatchTargetContextMenu` кат. 2 case 8
		//   (ui_core.c:32297–32320): режим включён (`[160] == 1`) — снять локально, нотис 108132, `[88] = 0`, 4206, иконка гаснет;
		//   выключен — 4206 с текущим `[88]` (включит сервер, ответ — 4206 команда 1). Тело и гейты — `UAsdaNetSubsystem::TogglePvpMode`.
		case 8:
			return Net && Net->TogglePvpMode(TEXT("функции 2/8 (эмоция 208)"));
		default:
			UE_LOG(LogAsdaCharInfo, Warning,
				TEXT("[эмоции] функция %d: соответствующая система в порт ещё не портирована"), Idx);
			return false;
		}
	}
	return false;
}

// Порт `UI_SetSitStandButtonIcon@0xC54CF0` (ui_core.c:32599): список «функций» (`this+51`), контрол `this[i+20]`.
void UAsdaCharaInfoSubsystem::SetSitStandButtonIcon(int32 ActionState)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
	if (!Hb) { return; }
	for (FAsdaEmotionSlotUi& S : EmotionSlots)
	{
		if (S.Group != 2 || !S.Icon) { continue; }
		FString Tag; bool bFull = false;
		if (S.IndexInGroup >= Hb->EmotionCountInGroup(2) || !Hb->ResolveEmotionIcon(S.Code, Tag, bFull)) { continue; }
		if (Tag != TEXT("act_updown") && Tag != TEXT("act_standup")) { continue; }
		const FString Name = (ActionState == 8 || ActionState == 7) ? FString(TEXT("act_standup")) : FString(TEXT("act_updown"));
		AsdaUi::ApplyFlatJuiIcon(S.Icon.Get(), Name, 32.f);   // эталон `DrawTextureRect(…, 0, 0, 32, 32)`
		SitStandIconShown = Name;
		UE_LOG(LogAsdaCharInfo, Log, TEXT("[эмоции] иконка «сесть/встать»: %s (состояние %d)"), *Name, ActionState);
		return;   // эталон рисует первую найденную и выходит
	}
}

FString UAsdaCharaInfoSubsystem::GetEmotionIconName(int32 Code) const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
	FString Tex; bool bFull = false;
	if (Hb && Hb->ResolveEmotionIcon(Code, Tex, bFull)) { return Tex; }
	return FString();
}

// ═══════════════════════════ ПОДСКАЗКИ И КУРСОР ═════════════════════════════════════════════════
// Эталон msg 8 по 12310…12322: заголовок = та же строка, что подпись (12033+i), тело = 1401+i,
//   цвет заголовка по группе строк (красный атаки, синий защита, зелёный статы, жёлтый ремёсла).
void UAsdaCharaInfoSubsystem::ShowStatTooltip(int32 RowIndex)
{
	if (RowIndex < 0 || RowIndex > 12) { return; }
	UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	if (!Tip) { return; }
	const TCHAR* Color =
		(RowIndex <= 1)  ? TEXT("FFFF0000") :   // 12310…12311 — атака / маг. атака
		(RowIndex <= 4)  ? TEXT("FF0080FF") :   // 12312…12314 — защита / маг. защита / блок
		(RowIndex <= 10) ? TEXT("FF00FF64") :   // 12315…12320 — базовые статы
		                   TEXT("FFFFFF00");    // 12321…12322 — ремёсла
	const int32 TitleLoc = (RowIndex == 11) ? CharInfoLoc::kCraftLabel : (CharInfoLoc::kStatFirst + RowIndex);
	FAsdaTipMetrics M; M.FontPx = 8; M.MaxW = 218.f;
	Tip->ShowTextTip(EAsdaTipOwner::CharaInfo,
		FString::Printf(TEXT("<FC=%s>%s</F>"), Color, *Loc(TitleLoc)),
		LocTagged(CharInfoLoc::kStatTipFirst + RowIndex), M);
	bTipShown = true;
}

void UAsdaCharaInfoSubsystem::ShowTitleTooltip()
{
	UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	if (!Tip) { return; }
	FAsdaTipMetrics M; M.FontPx = 8; M.MaxW = 218.f;
	Tip->ShowTextTip(EAsdaTipOwner::CharaInfo, Loc(CharInfoLoc::kRowFirst + 1), LocTagged(CharInfoLoc::kTitleTip), M);
	bTipShown = true;
}

// Эталон: тип подсказки 3 → `emotionTip_lookupByCode` → текст = поле +0 записи (без заголовка).
void UAsdaCharaInfoSubsystem::ShowEmotionTooltip(int32 Code)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
	UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	if (!Hb || !Tip) { return; }
	FString Text;
	if (!Hb->ResolveEmotionTip(Code, Text) || Text.IsEmpty()) { return; }
	FAsdaTipMetrics M; M.FontPx = 8; M.MaxW = 218.f;
	Tip->ShowTextTip(EAsdaTipOwner::CharaInfo, Text, FString(), M);
	bTipShown = true;
}

void UAsdaCharaInfoSubsystem::HideOwnTooltip()
{
	if (!bTipShown) { return; }
	if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		// ★ Закон `ui-shared-tip-ownership`: гасим ТОЛЬКО свою плашку.
		Tip->HideTooltipFor(EAsdaTipOwner::CharaInfo);
	}
	bTipShown = false;
	HoverRow = -1; HoverEmotion = -1; HoverKind = 0;
}

// Покадровый опрос курсора: строки статов (наведение → подсказка) и слоты эмоций (наведение +
//   клик = запуск). Кнопок у этих контролов в разметке нет — эталон тоже работает хит-тестом.
void UAsdaCharaInfoSubsystem::TickPointer()
{
	if (!Win) { return; }
	static FAsdaMouseFrame Mouse;
	Mouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ⚠ Защёлки сбрасываем: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия сошлись бы
	//   в клик, которого игрок не делал (здесь это ЗАПУСК эмоции либо укладка её в панель).
	//   Кадр мыши у окна — функциональная `static` (поля в классе нет), сбрасывается она так же.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		// Начатый захват снимаем ТОЛЬКО визуально: `EndEmotionDrag` выполнил бы действие (запуск
		//   или укладку), а под модалкой действия нет — есть отмена. Иконке возвращается цвет.
		if (bEmoteDragActive) { CancelEmotionDragVisual(EmoteDragArm.KeyA); }
		EmoteDragArm.Disarm();
		HideOwnTooltip();   // висящую подсказку статы/эмоции ГАСИМ (она же сбросит ключи наведения)
		Mouse.Reset();
		return;
	}

	// ── строки статов (страница 983) ───────────────────────────────────────────────────────────
	int32 NewRow = -1;
	if (CurTab == 0 && CurStatPage == 1)
	{
		for (int32 i = 0; i < 13; ++i)
		{
			UWidget* L = StatLabels.IsValidIndex(i) ? StatLabels[i].Get() : nullptr;
			UWidget* V = StatFmt.IsValidIndex(i) ? StatFmt[i].Node.Get() : nullptr;
			if ((L && AsdaUi::IsCursorOverWidget(L)) || (V && AsdaUi::IsCursorOverWidget(V))) { NewRow = i; break; }
		}
	}

	// ── слоты эмоций (вкладка 1) ───────────────────────────────────────────────────────────────
	int32 NewEmote = -1;
	if (CurTab == 1)
	{
		for (int32 i = 0; i < EmotionSlots.Num(); ++i)
		{
			UWidget* Ic = EmotionSlots[i].Icon.Get();
			if (Ic && Ic->GetVisibility() != ESlateVisibility::Collapsed && AsdaUi::IsCursorOverWidget(Ic))
			{
				NewEmote = i; break;
			}
		}
	}

	// ★ ГЕЙТ ЭТАЛОНА: пока идёт захват/перетаскивание, подсказка НЕ всплывает вовсе
	//   (`if (!helper_getField1(g_pUIManager))` перед веткой msg 8, `ui_core.c:32869`).
	//   Без него `HideOwnTooltip` сбрасывает `HoverEmotion` в −1, и уже следующий кадр показывал бы
	//   подсказку заново прямо под «призраком».
	if (bEmoteDragActive)
	{
		NewRow = -1; NewEmote = HoverEmotion;
	}
	else if (NewRow != HoverRow || NewEmote != HoverEmotion)
	{
		HideOwnTooltip();
		HoverRow = NewRow; HoverEmotion = NewEmote;
		if (NewRow >= 0)        { HoverKind = 1; ShowStatTooltip(NewRow); }
		else if (NewEmote >= 0) { HoverKind = 3; ShowEmotionTooltip(EmotionSlots[NewEmote].Code); }
		else                    { HoverKind = 0; }
	}

	// ── НАЖАТИЕ НА СЛОТ ЭМОЦИИ ─────────────────────────────────────────────────────────────────
	//   Эталон `EmoteWnd_WndProc` (`ui_core.c:32777`) разводит нажатие на ДВЕ разные ветки:
	//     msg 11 `opcodeRange_SelectList` — ВЫБОР слота (группа в `this[40]`, индекс в `this[41..43]`,
	//             курсор выделения через `uiDlgUpdateDragIcon`);
	//     msg 5  `hotbarHandleSlotKeyCommand` — НАЧАЛО ЗАХВАТА (гасит подсказку, взводит `this[55]`);
	//     msg 13 `Emote_Execute` — ЗАПУСК (это обычный клик без перетаскивания);
	//     msg 7  `hotbarAssignQuickSlotByControlId` — БРОСОК в панель быстрого доступа.
	//   ⇒ у нас: нажатие = выбор + ВЗВОД захвата; отпускание без сдвига = запуск; отпускание после
	//   сдвига = укладка в слот панели под курсором. Порог сдвига — общая точка `FAsdaDragArm`
	//   (закон `port-shared-points`), той же, что у книги навыков, инвентаря и самой панели.
	if (Mouse.bLmbDown && HoverEmotion >= 0 && EmotionSlots.IsValidIndex(HoverEmotion))
	{
		const FAsdaEmotionSlotUi& S = EmotionSlots[HoverEmotion];
		// Курсор выделения на выбранном слоте (эталон `uiDlgUpdateDragIcon`).
		for (int32 g = 0; g < EmotionCursors.Num(); ++g)
		{
			if (!EmotionCursors[g]) { continue; }
			AsdaUi::SetShownNoHit(EmotionCursors[g], g == S.Group);
			if (g == S.Group && S.Icon)
			{
				FVector2D P, Sz;
				if (AsdaUi::SlotRect(S.Icon.Get(), P, Sz)) { AsdaUi::SetSlotPos(EmotionCursors[g].Get(), P); }
			}
		}
		EmoteDragArm.Arm(HoverEmotion, -1, Mouse.Cursor);
	}

	// Порог пройден при зажатой кнопке — поднимаем «призрак» иконки (эталон создаёт окно-призрак
	//   в `CControl_OnLButtonDown` тем же кадром; порог — осознанное отступление, см. `FAsdaDragArm`).
	if (Mouse.bLmb && EmoteDragArm.ShouldStart(Mouse.Cursor))
	{
		BeginEmotionDrag(EmoteDragArm.KeyA);
	}
	if (bEmoteDragActive) { EmoteDragGhost.Tick(GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr); }

	// ── ОТПУСКАНИЕ ─────────────────────────────────────────────────────────────────────────────
	if (Mouse.bLmbUp)
	{
		const int32 Slot = EmoteDragArm.KeyA;
		const bool  bWasDrag = bEmoteDragActive;
		EndEmotionDrag(bWasDrag, Slot);
		EmoteDragArm.Disarm();
	}
}

// ★★★ ЗАХВАТ ЭМОЦИИ ИЗ ЛОТКА (эталон msg 5 `hotbarHandleSlotKeyCommand@0xC552F0`).
//   Эталон на этом шаге делает ровно три вещи: гасит подсказку (`CControl_KillFocus(&g_ItemTooltip)`),
//   сбрасывает флаг «подсказка активна» (`this[57] = 0`) и взводит признак захвата (`this[55] = 1`),
//   после чего возвращает код 2 — по нему общий `CControl_OnLButtonDown@0x12DB870` создаёт
//   окно-призрак. У нас призрак — общая точка `FAsdaDragGhost` (та же, что у книги навыков).
void UAsdaCharaInfoSubsystem::BeginEmotionDrag(int32 SlotIdx)
{
	if (!EmotionSlots.IsValidIndex(SlotIdx) || !Win) { return; }
	EmoteDragArm.MarkStarted();
	bEmoteDragActive = true;
	HideOwnTooltip();                                   // эталон: `CControl_KillFocus(&g_ItemTooltip)`
	UImage* Src = EmotionSlots[SlotIdx].Icon.Get();
	if (Src)
	{
		if (UCanvasPanel* Root = Cast<UCanvasPanel>(Win->GetRootWidget()))
		{
			EmoteDragGhost.Begin(Win, Root, Src->GetBrush(), FVector2D(32.f, 32.f),
				kEmoteDragGhostOpacity, FVector2D(-16.f, -16.f), TEXT("EMOTE_DragGhost"),
				GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr);
		}
		Src->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, kEmoteDragSourceOpacity));
	}
	UE_LOG(LogAsdaCharInfo, Log, TEXT("[эмоция→панель] захват эмоции %d из слота %02d"),
		EmotionSlots[SlotIdx].Code, SlotIdx);
}

// Снять ВИЗУАЛ захвата: погасить призрак и вернуть исходной иконке полную непрозрачность.
void UAsdaCharaInfoSubsystem::CancelEmotionDragVisual(int32 SlotIdx)
{
	if (bEmoteDragActive)
	{
		EmoteDragGhost.Cancel();
		bEmoteDragActive = false;
	}
	if (EmotionSlots.IsValidIndex(SlotIdx))
	{
		if (UImage* Ic = EmotionSlots[SlotIdx].Icon.Get()) { Ic->SetColorAndOpacity(FLinearColor::White); }
	}
}

// Полная отмена захвата БЕЗ укладки и БЕЗ запуска (эталон msg 6): окно закрылось или сменили вкладку.
void UAsdaCharaInfoSubsystem::CancelEmotionDrag()
{
	if (!bEmoteDragActive && EmoteDragArm.KeyA < 0) { return; }
	CancelEmotionDragVisual(EmoteDragArm.KeyA);
	EmoteDragArm.Disarm();
}

// ★★★ ОТПУСКАНИЕ. Без сдвига — обычный клик = ЗАПУСК (эталон msg 13 `Emote_Execute`).
//   Со сдвигом — БРОСОК (эталон msg 7 `hotbarAssignQuickSlotByControlId@0xC55450`): берётся IDNum
//   контрола под курсором и принимаются РОВНО два диапазона слотов панели — 73011…73022 (основной
//   ряд, колонки 0…11) и 73161…73172 (T-ряд, колонки 0…11). У нас «контрол под курсором» — это
//   хит-тест `UAsdaHotbarSubsystem::HitTestSlotUnderCursor` (та же общая точка, что у инвентаря и
//   книги навыков). ⚠ КОЛОНКА 12 (слот оружия) эмоцию НЕ принимает — верхняя граница эталонных
//   диапазонов на одну колонку короче, чем у предмета; ровно так же ведёт себя навык.
//   Промах мимо панели — эталон НЕ ДЕЛАЕТ НИЧЕГО (`if (a2)`), эмоция ниоткуда не снимается.
void UAsdaCharaInfoSubsystem::EndEmotionDrag(bool bWasDragging, int32 SlotIdx)
{
	// Призрак снимаем ДО хит-теста: иначе он сам оказался бы «контролом под курсором».
	CancelEmotionDragVisual(SlotIdx);
	if (SlotIdx < 0 || !EmotionSlots.IsValidIndex(SlotIdx)) { return; }
	const int32 Code = EmotionSlots[SlotIdx].Code;

	if (!bWasDragging)
	{
		PlayEmotion(Code);                              // эталон msg 13
		return;
	}
	DropEmotionToHotbar(Code);
}

// Единая точка укладки: ею пользуются и бросок мышью, и консольная проверка `asda.CharInfo.EmoteDrop`.
bool UAsdaCharaInfoSubsystem::DropEmotionToHotbar(int32 Code, int32 AbsSlot)
{
	UAsdaHotbarSubsystem* Hb = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
	if (!Hb)
	{
		UE_LOG(LogAsdaCharInfo, Warning, TEXT("[эмоция→панель] подсистема панели недоступна — бросок эмоции %d потерян"), Code);
		return false;
	}
	const int32 Abs = (AbsSlot >= 0) ? AbsSlot : Hb->HitTestSlotUnderCursor();
	if (Abs < 0)
	{
		UE_LOG(LogAsdaCharInfo, Log,
			TEXT("[эмоция→панель] бросок эмоции %d мимо панели — эталон в этом случае ничего не делает"), Code);
		return false;
	}
	// ⚠ Колонка 12 — слот оружия: эталонные диапазоны эмоции его НЕ включают (см. комментарий выше).
	if (Hb->ColumnOfAbsSlot(Abs) == 12)
	{
		UE_LOG(LogAsdaCharInfo, Log,
			TEXT("[эмоция→панель] слот оружия (колонка 12) эмоцию не принимает — отказ, как в эталоне"));
		return false;
	}
	// Запись 10 б эталона `hotbarSetQuickSlotItem@0xC55530`: тип 2, u16 на +2 = код группа·100 + индекс.
	//   `SetSlotContent` сам пересобирает иконку/подсказку и шлёт страницу пакетом 5067 —
	//   то же, что делает `Hotbar_DragDrop_SetSlot@0xDE9DF0` через `Hotbar_SendLayout`.
	const bool bOk = Hb->SetSlotContent(Abs, (uint8)EAsdaHotbarSlotType::Emotion, 0, Code, 0, 0);
	UE_LOG(LogAsdaCharInfo, Log, TEXT("[эмоция→панель] эмоция %d → слот %d панели: %s"),
		Code, Abs, bOk ? TEXT("уложена") : TEXT("ОТКАЗ"));
	return bOk;
}
