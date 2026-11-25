// Copyright (c) 2026 @sitxovski (Discord)
// AsdaMiniStateSubsystem_Vitals.cpp — ★ R3-c 12.11.2026 (разрез AsdaMiniStateSubsystem.cpp, дословный перенос): HP/MP/вес/уровень, PvP, пакеты, отряд, клан-метка, смерть, крылья, RefreshAll.
//   Общее для частей — в AsdaMiniStateSubsystem_Private.h; шапка и реверс — в AsdaMiniStateSubsystem.cpp.
#include "UI/AsdaMiniStateSubsystem.h"


// inline-теги <FC=…>…</F> в тексте тултипа 110922   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "AsdaMiniStateSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaMiniStateSubsystem*.cpp

// loc-идентификаторы уведомлений о перегрузе (эталон HUD_UpdateMiniWeightBar, ui_core.c:75178/75191).
static constexpr int32 kLocWeight70 = 2358;
static constexpr int32 kLocWeight90 = 2359;

// ★ 06.10.2026 [ПОРТ] ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА: 6542 «флаг клана ВЫКЛ» эталон на портрете не гасит —
//   `partyMemberUpdateHpState` (social.c:3640-3650) только ПОКАЗЫВАЕТ базу, если флаг был выключен, а
//   гасит её лишь вход на карту. При «выкл → выкл» база даже зажигается с пустым рисунком.
//   1 = 6542 с выключенным флагом гасит эмблему; 0 = как в оригинале.
static ASDA_DEV_CVAR(int32, CVarClanMarkHideOnFlagOff,
	TEXT("asda.Mini.ClanMarkHideOnFlagOff"),
	1,
	TEXT("Эмблема клана на портрете при 6542 «флаг выключен»: 1 = погасить (исправление дефекта оригинала); "
	     "0 = как в оригинале (база остаётся/зажигается до следующего входа на карту)."),
	ECVF_Default);

// Карты кампвара в ветке входа на карту (`getField3036(g_pLocalPlayer) == 19 || == 23`, game_main.c:3910).
//   По реестру карт это ровно те две карты, у которых байт +69 = 1 (battlefield, mine).
static constexpr int32 kCampWarMapA = 19;
static constexpr int32 kCampWarMapB = 23;
// Коды событий очереди эмблемы (см. PendingClanMark в .h).
static constexpr uint8 kClanEvSnapshot = 1;   // снимок клана своего (4135/6153)
static constexpr uint8 kClanEvFlag     = 2;   // 6542 своего клана
static constexpr uint8 kClanEvHide     = 3;   // 4107/4117/4133 код успеха, 4132 код 2

// ══════════════════════════════ ПОКАЗАТЕЛИ ═════════════════════════════════════════════════════
// UI_UpdateHPBar@0xE78CF0 1:1, включая обе точки раннего выхода.
void UAsdaMiniStateSubsystem::ApplyHp(int32 Cur, int32 Max)
{
	if (!bControlsReady) { return; }
	if (Max <= 0)
	{
		ASDA_CTRACE(EAsdaCT::HpSelf, TEXT("свой HP в HUD НЕ обновлён: max=%d ≤ 0 (cur=%d) — эталонный ранний выход UI_UpdateHPBar"), Max, Cur);
		return;
	}                            // эталон: if (a3 <= 0) return 0;
	const double Pct = (double)Cur / (double)Max * 100.0;
	if (Pct < 0.0 || Cur > Max)
	{
		ASDA_CTRACE(EAsdaCT::HpSelf, TEXT("свой HP в HUD НЕ обновлён: %d/%d (%s) — эталон игнорирует значение целиком, полоса остаётся «%s»"),
			Cur, Max, Cur > Max ? TEXT("cur > max") : TEXT("cur < 0"), HpText ? *HpText->GetText().ToString() : TEXT("нет текста"));
		return;
	}              // эталон: пакет игнорируется ЦЕЛИКОМ
	HpSmooth.SetTarget((float)(Pct / 100.0));         // ★ заливка догоняет в TickVitalBars
	ClipFill(HpBar, HpSmooth.Shown);
	if (HpText) { HpText->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), Cur, Max))); }
}

// HUD_UpdateMPBar@0xE78E50 1:1.
void UAsdaMiniStateSubsystem::ApplyMp(int32 Cur, int32 Max)
{
	if (!bControlsReady) { return; }
	if (Max <= 0)
	{
		ASDA_CTRACE(EAsdaCT::HpSelf, TEXT("свой MP в HUD НЕ обновлён: max=%d ≤ 0 (cur=%d) — эталонный ранний выход HUD_UpdateMPBar"), Max, Cur);
		return;
	}
	const double Pct = (double)Cur / (double)Max * 100.0;
	if (Pct < 0.0 || Cur > Max)
	{
		ASDA_CTRACE(EAsdaCT::HpSelf, TEXT("свой MP в HUD НЕ обновлён: %d/%d (%s) — эталон игнорирует значение целиком"),
			Cur, Max, Cur > Max ? TEXT("cur > max") : TEXT("cur < 0"));
		return;
	}
	MpSmooth.SetTarget((float)(Pct / 100.0));         // ★ заливка догоняет в TickVitalBars
	ClipFill(MpBar, MpSmooth.Shown);
	if (MpText) { MpText->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), Cur, Max))); }
}

// ★ Догоняющий ход полос жизни и маны. Значения приходят пакетами скачком, а заливка идёт к ним
//   плавно за `asda.UI.BarSmoothSec`. Отступление от оригинала (там перерисовка мгновенная),
//   ключ отката — та же настройка в 0.
void UAsdaMiniStateSubsystem::TickVitalBars(float DeltaSeconds)
{
	if (!bControlsReady) { return; }
	const float Sec = FAsdaBarSmoother::SmoothSeconds();
#if ASDA_COMBAT_TRACE
	const float TrHpWas = HpSmooth.Shown;
	const float TrMpWas = MpSmooth.Shown;
#endif
	if (HpSmooth.Advance(DeltaSeconds, Sec))
	{
		ClipFill(HpBar, HpSmooth.Shown);
		// Покадровый ход своей полосы — только уровень 2; приход к цели — одна строка уровня 1.
		ASDA_CTRACE_V(EAsdaCT::HpSelf, TEXT("своя полоса HP кадр: %.3f→%.3f (цель %.3f, dt %.3f)"), TrHpWas, HpSmooth.Shown, HpSmooth.Target, DeltaSeconds);
#if ASDA_COMBAT_TRACE
		if (HpSmooth.Shown == HpSmooth.Target && TrHpWas >= 0.f && TrHpWas != HpSmooth.Target)
		{
			ASDA_CTRACE(EAsdaCT::HpSelf, TEXT("своя полоса HP ДОЕХАЛА: %.3f (текст «%s»)"), HpSmooth.Shown, HpText ? *HpText->GetText().ToString() : TEXT("нет"));
		}
#endif
	}
	if (MpSmooth.Advance(DeltaSeconds, Sec))
	{
		ClipFill(MpBar, MpSmooth.Shown);
		ASDA_CTRACE_V(EAsdaCT::HpSelf, TEXT("своя полоса MP кадр: %.3f→%.3f (цель %.3f, dt %.3f)"), TrMpWas, MpSmooth.Shown, MpSmooth.Target, DeltaSeconds);
	}
}

// HUD_RefreshHPMPGauges_Frame@0xE79CD0 (только часть, относящаяся к мини-статусу):
//   ширина слоя = процент «отстающего» значения × 1.25, слой всегда видим.
// ★★★ «ОТСТАЮЩИЙ» СЛОЙ — то, что игрок видит как коричневую часть полосы при восстановлении.
//   Эталон `HUD_RefreshHPMPGauges_Frame@0xE79CD0` (ui_core.c:75432) считает его так:
//       hpScale      = 100.0 / maxHp;
//       hpAuxPercent = ВТОРОЕ значение * hpScale;          // куда восстановит зелье/навык
//       if ( hpPercent < hpAuxPercent ) hpPercent = hpAuxPercent;   // ★ БЕРЁТСЯ МАКСИМУМ
//       SetWidth(g_pHpGaugeBarB, hpPercent);               // слой рисуется по максимуму
//   То же для маны через `mpAuxPercent`.
//
//   ⚠ ИСПРАВЛЕНО 24.08.2026. Порт брал ТОЛЬКО второе значение. Пока оно больше текущего —
//   разница видна, но как только восстановление догоняло (или второе оказывалось меньше),
//   слой становился КОРОЧЕ основной полосы и пропадал за ней. Именно из-за максимума в эталоне
//   слой никогда не уходит под основную полосу: он всегда либо вровень, либо выступает — и
//   ровно выступающая часть и есть та самая коричневая полоска «сколько ещё восстановится».
void UAsdaMiniStateSubsystem::ApplyVitalGhosts(int32 HpSecond, int32 HpMax, int32 MpSecond, int32 MpMax)
{
	if (!bControlsReady) { return; }
	// ★ 06.10.2026 (ревизия МЕЛК-3): пока персонаж мёртв, эталон каждый кадр держит слои в нуле
	//   (ui_core.c:75482-75497). Пакет со «вторым» HP/MP может прийти ПОСЛЕ тика мини-статуса —
	//   гасим и здесь, на приёме, чтобы слой не мелькнул на один кадр.
	if (IsSelfDeadExact()) { HpSecond = 0; MpSecond = 0; }
	auto Apply = [this](UImage* Ghost, int32 Val, int32 /*Cur*/, int32 Max)
	{
		if (!Ghost) { return; }
		float W = 0.f;
		if (Max > 0)
		{
			// ★★★ 1:1 `HUD_RefreshHPMPGauges_Frame@0xE79CD0` (`ui_core.c:75447-75455`): слой буффа
			//   рисуется САМИМ вторым значением, зажатым в [0, максимум] — БЕЗ максимума с текущим.
			//   ⛔ Максимум `if (percent < aux) percent = aux` в эталоне относится к ДРУГОМУ контролу
			//   (`g_pHpGaugeBarB`), не к этому. Прежняя правка перепутала их: при обнулённом втором
			//   значении слой показывал ТЕКУЩЕЕ здоровье и оставался во всю полосу навсегда
			//   (владелец 2026-08-24: «по истечении действия зелья полоса должна исчезать»).
			const int32 Shown = FMath::Clamp(Val, 0, Max);
			W = (100.f / (float)Max * (float)Shown) * GhostWidthScale;
		}
		if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(Ghost))
		{
			FMargin O = CS->GetOffsets();
			// Журнал боя: «призрачный» слой восстановления — только при смене ширины.
#if ASDA_COMBAT_TRACE
			if (!FMath::IsNearlyEqual(O.Right, FMath::Max(W, 0.f), 0.05f))
			{
				ASDA_CTRACE(EAsdaCT::HpSelf, TEXT("свой «призрачный» слой %s (второе значение): %d из max %d → ширина %.1f→%.1f%s"),
					Ghost == HpGhost ? TEXT("HP (HPBuff 37)") : TEXT("MP (MPBuff 89)"), Val, Max, O.Right, FMath::Max(W, 0.f),
					IsSelfDeadExact() ? TEXT(" [мёртв: слой в нуле]") : TEXT(""));
			}
#endif
			O.Right = FMath::Max(W, 0.f);
			CS->SetOffsets(O);
		}
		SetVis(Ghost, true);   // эталон: SetVisible(1) каждый кадр, независимо от значения
	};
	// Текущие значения берём из того же единого источника, что и основные полосы.
	int32 CurHp = 0, CurMp = 0;
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		if (const UAsdaNetSubsystem::FAsdaVitals* V = Net->GetVitals(AsdaSelf::GetUserIdxOr(GetGameInstance(), INDEX_NONE)))   // ★ волна 1 (E2)
		{
			CurHp = V->HpCur; CurMp = V->MpCur;
		}
	}
	Apply(HpGhost, HpSecond, CurHp, HpMax);
	Apply(MpGhost, MpSecond, CurMp, MpMax);
}

// HUD_UpdateMiniWeightBar@0xE78FA0 1:1 (3 полосы, пороги 70/90, мигание, цифры).
void UAsdaMiniStateSubsystem::ApplyWeight(int32 Cur, int32 Max)
{
	// ★ Доказательная строка (только лог, поведения не меняет): по ней в живом логе сразу видно
	//   и пришедшие числа, и посчитанный процент — именно она вскрыла подмену максимума
	//   пакетом 5456 (8842/210 = 4210 % вместо 8842/32000 = 27 %).
	UE_LOG(LogAsdaMini, Log, TEXT("[вес] %d/%d = %d %% (контролы %s)"),
		FMath::Max(Cur, 0), Max,
		(Max > 0) ? (int32)((float)FMath::Max(Cur, 0) / (float)Max * 100.f) : -1,
		bControlsReady ? TEXT("готовы") : TEXT("НЕ готовы"));
	if (!bControlsReady) { return; }
	if (Cur < 0) { Cur = 0; }                      // эталон: if (a2 <= 0) a2 = 0;
	if (Max <= 0)
	{
		// Эталон в этом случае ВЫХОДИТ, ничего не перерисовывая. Логируем один раз на изменение,
		//   чтобы было видно: базовый максимум веса сервер ещё не прислал (см. .h — он из БД).
		UE_LOG(LogAsdaMini, Verbose, TEXT("[вес] максимум неизвестен (%d) — полоса не обновляется (как в эталоне)"), Max);
		return;
	}
	const float PctF = (float)Cur / (float)Max * 100.f;
	ClipFill(WeightBar,  PctF / 100.f);
	// Обе доп. полосы получают то же значение и СНАЧАЛА гасятся (эталонный цикл на 2 элемента).
	ClipFill(WeightBar2, PctF / 100.f);
	ClipFill(WeightBar3, PctF / 100.f);
	SetVis(WeightBar2.Fill, false); SetVis(WeightBar2.Back, false);
	SetVis(WeightBar3.Fill, false); SetVis(WeightBar3.Back, false);

	int32 Pct = (int32)PctF;
	if (Pct < 0) { Pct = 0; }

	// ── Цифры процента (hudDrawThreeDigitNumber@0xE811F0) ──
	{
		for (int32 D = 0; D < 10; ++D)
		{
			SetVis(WeiDigitOnes.IsValidIndex(D) ? WeiDigitOnes[D] : nullptr, false);
			SetVis(WeiDigitTens.IsValidIndex(D) ? WeiDigitTens[D] : nullptr, false);
			SetVis(WeiDigitHundreds.IsValidIndex(D) ? WeiDigitHundreds[D] : nullptr, false);
		}
		// ★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА. Эталон считает `digitHundreds = a2 / 100` и БЕЗ
		//   всякой проверки берёт `pWeightDigit0[3 * digitHundreds]`. Массив рассчитан на цифры
		//   0…9, поэтому при проценте ≥ 1000 оригинал читает и пишет ЗА ПРЕДЕЛАМИ массива
		//   внутри `CHUD`. Контрол физически трёхразрядный, больше 999 показать нечем, поэтому
		//   для ОТРИСОВКИ ограничиваем 999. Полосы, пороги 70/90 и уведомления считаются от
		//   НЕОГРАНИЧЕННОГО `Pct` — там поведение остаётся 1:1 с эталоном.
		//   (Именно на этот дефект налетал баг «10 %»: 8842/210 = 4210 %, сотни = 42, ряд сотен
		//    не находился, и на экране оставались десятки+единицы «10».
		//    Разбор — `Reports/ministate_weight_fix.md`.)
		const int32 PctDraw = FMath::Min(Pct, 999);
		if (PctDraw != Pct)
		{
			UE_LOG(LogAsdaMini, Warning,
				TEXT("[вес] процент %d > 999 (%d/%d) — трёхразрядный контрол показывает 999. ")
				TEXT("Проверь источник максимума веса: он должен приходить из 1017 блок+44 / 4065, а не из 5456."),
				Pct, Cur, Max);
		}
		const int32 H = PctDraw / 100, T = (PctDraw / 10) % 10, O = PctDraw % 10;
		if (H != 0 && WeiDigitHundreds.IsValidIndex(H) && WeiDigitHundreds[H])
		{
			SetSlotPos(WeiDigitHundreds[H], FVector2D(0.f, WeiDigitY));     // X сотен = 0
			SetVis(WeiDigitHundreds[H], true);
		}
		if ((T != 0 || H != 0) && WeiDigitTens.IsValidIndex(T) && WeiDigitTens[T])
		{
			SetSlotPos(WeiDigitTens[T], FVector2D(5.f, WeiDigitY));         // X десятков = 5
			SetVis(WeiDigitTens[T], true);
		}
		if (WeiDigitOnes.IsValidIndex(O) && WeiDigitOnes[O])
		{
			SetSlotPos(WeiDigitOnes[O], FVector2D(10.f, WeiDigitY));        // X единиц = 10
			SetVis(WeiDigitOnes[O], true);
		}
	}

	// ── Пороги и уведомления (ui_core.c:75168-75194) ──
	// ★ 06.10.2026: 1:1 с ПЕРЕХОДОМ эталона. После нотиса 2358 эталон делает `goto LABEL_19` и ПРОПУСКАЕТ
	//   проверку 90 %: за один пакет приходит не больше одного нотиса, 2359 — только со СЛЕДУЮЩИМ пакетом.
	//   Проверка 90 % (LABEL_16) достижима лишь из «< 70» (там она ложна) и из «флаг 70 уже поднят».
	//   Прежде порт выводил 2358 и 2359 в одном кадре (`self_portrait_game.log`, строки 5540/5544).
	if (Pct < 70)
	{
		bWeightWarn70Shown = false;
		bWeightWarn90Shown = false;
	}
	else if (!bWeightWarn70Shown)
	{
		PostWeightNotice(kLocWeight70);
		bWeightWarn70Shown = true;
		// goto LABEL_19 — проверка 90 % в этом пакете не выполняется
	}
	else if (Pct >= 90 && !bWeightWarn90Shown)
	{
		PostWeightNotice(kLocWeight90);
		bWeightWarn90Shown = true;
	}

	// ── Видимость полос + таймер мигания ──
	if (Pct < 90)
	{
		SetVis(WeightBar.Fill, true); SetVis(WeightBar.Back, true);
		WeightBlinkNextS = -1.0;                         // эталон: KillTimer(hWnd, 25)
		bWeightBlinkOn = true;
		if (Pct >= 70) { SetVis(WeightBar2.Fill, true); SetVis(WeightBar2.Back, true); }
	}
	else
	{
		SetVis(WeightBar3.Fill, true); SetVis(WeightBar3.Back, true);
		SetVis(WeightBar.Fill, false); SetVis(WeightBar.Back, false);
		bWeightBlinkOn = true;
		UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
		WeightBlinkNextS = (W ? W->GetTimeSeconds() : 0.0) + (double)FMath::Max(OverloadBlinkSec, 0.05f);
	}
}

// Эталон: CControl_SetTooltipText(g_pChatWnd, UI_FormatLocalizedNotice(loc), −21248, 30000).
//   −21248 = 0xFFFFAD00 (оранжевый), 30 000 мс. У нас единая точка вывода — маршрутизатор
//   сообщений; категория ItemRefused = «отказ предмета, прочность, ВЕС».
// ★ КАНАЛ — РЕШЕНИЕ ВЛАДЕЛЬЦА (2026-08-04, `Docs/UI/HUD/NOTICES_ALL.md` §0.0, строка «Отказ предмета,
//   прочность 0, перегруз (2358/2359 TTL 30000)»): эталонное окно 221 WARMESSAGE зарезервировано под военные
//   события, перегруз идёт в СИСТЕМНЫЙ ЧАТ + `error.wav`. Отступление записано, поэтому не возвращаем 221.
void UAsdaMiniStateSubsystem::PostWeightNotice(int32 LocId)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr;
	if (!Msg)
	{
		UE_LOG(LogAsdaMini, Warning, TEXT("[вес] уведомление loc %d: маршрутизатор сообщений недоступен"), LocId);
		return;
	}
	FAsdaMsgParams P;
	P.Category  = EAsdaMsgCategory::ItemRefused;
	P.LocId     = LocId;
	P.bUseColor = true;
	P.Color     = FColor(0xFF, 0xAD, 0x00, 0xFF);   // эталонный −21248 = 0xFFFFAD00
	P.TtlMs     = 30000.f;                          // эталонные 30 000 мс
	Msg->Post(P);
	NoticeTrace.Add(LocId);                         // ★ 06.10.2026: след для самотеста
	UE_LOG(LogAsdaMini, Log, TEXT("[вес] уведомление о перегрузе: loc %d"), LocId);
}

// ★ 06.10.2026: нотис режима боя 4206. Эталон (network.c:44123-44137, 44169-44184):
//   `CChat_AddTokenizedLines(g_pChatWnd, текст, −1, 10000, 0)` + `CControl_SetTooltipText(g_pChatWnd, текст, −1, 10000)` —
//   ПАРНЫЙ вызов «журнал + 221», белый (−1), 10 000 мс.
//   КАНАЛ — РЕШЕНИЕ ВЛАДЕЛЬЦА (2026-08-04, `NOTICES_ALL.md` §0.0): «Парные вызовы (журнал + 221) → только журнал»,
//   разметка колл-сайтов `Reports/warmsg_callsites.tsv:171-174` помечает 108132/108119/108120 как PAIR → «только журнал».
//   ★ Ревизия (МЕЛК-1): `CChat_AddTokenizedLines(…, −1, 10000, 0)` — последний аргумент track = 0 → буфер A, а НЕ B.
//   Строка §0.0 «PvP-дуэль → журнал + буфер B» относится к итогу дуэли (108112/108113/108122/108123), не к 4206.
//   ⇒ категория System (журнал 2572 + буфер A), цвет эталона — белый.
void UAsdaMiniStateSubsystem::PostPvpNotice(int32 LocId, bool bChatOnly)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr;
	NoticeTrace.Add(LocId);   // след для самотеста — «обработчик отправил»
	if (!Msg)
	{
		UE_LOG(LogAsdaMini, Warning, TEXT("[PvP] нотис loc %d: маршрутизатор сообщений недоступен"), LocId);
		return;
	}
	FAsdaMsgParams P;
	P.Category  = EAsdaMsgCategory::System;   // журнал + буфер A (track 0 эталона)
	P.LocId     = LocId;
	P.bUseColor = true;
	P.Color     = FColor::White;   // эталонный −1 = 0xFFFFFFFF
	P.TtlMs     = 10000.f;         // эталонные 10 000 мс
	// ★ 18.10.2026 (ревизия Ф4, мелочь 7): локальный отбой режима — эталон пишет только в чат (`CChat_AddTokenizedLines`,
	//   ui_core.c:32302), без подсказки `CControl_SetTooltipText` — приёмник задан явно, мимо таблицы маршрутов.
	if (bChatOnly) { P.Targets = (int32)EAsdaMsgTarget::SystemChat; }
	Msg->Post(P);
	UE_LOG(LogAsdaMini, Log, TEXT("[PvP] нотис режима боя: loc %d%s"), LocId, bChatOnly ? TEXT(" (локальный отбой — только чат)") : TEXT(""));
}

// HUD_UpdateLevelDigits@0xE7A160 1:1 (см. вывод формулы в шапке .h).
void UAsdaMiniStateSubsystem::ApplyLevel(int32 Level)
{
	if (!bControlsReady || Level < 0) { return; }
	for (int32 D = 0; D < 10; ++D)
	{
		SetVis(LvDigitOnes.IsValidIndex(D) ? LvDigitOnes[D] : nullptr, false);
		SetVis(LvDigitTens.IsValidIndex(D) ? LvDigitTens[D] : nullptr, false);
		SetVis(LvDigitHundreds.IsValidIndex(D) ? LvDigitHundreds[D] : nullptr, false);
	}
	const int32 H = Level / 100;
	const int32 T = (Level % 100) / 10;
	const int32 O = Level % 10;
	const int32 PX = (int32)LvProtoPos.X;
	const float PY = LvProtoPos.Y;
	const int32 LW = (int32)LvWinW;
	const int32 DW = (int32)LvDigitW;

	auto Show = [this, PY](TArray<TObjectPtr<UImage>>& Row, int32 D, int32 X)
	{
		if (!Row.IsValidIndex(D) || !Row[D]) { return; }
		SetSlotPos(Row[D], FVector2D((float)X, PY));
		SetVis(Row[D], true);
	};

	if (H != 0)
	{
		Show(LvDigitHundreds, H, PX - 6);
		Show(LvDigitTens,     T, PX + LW / 3 - 6);
		Show(LvDigitOnes,     O, PX + 2 * LW / 3 - 6);
	}
	else if (T != 0)
	{
		Show(LvDigitTens, T, PX + LW / 2 - DW + 2);
		Show(LvDigitOnes, O, PX + LW / 2 - 2);
	}
	else
	{
		Show(LvDigitOnes, O, PX + LW / 2 - DW / 2 - 2);
	}
	LastLevelApplied = Level;
	UE_LOG(LogAsdaMini, Log, TEXT("[мини-статус] уровень %d → разрядов %d (сотни=%d десятки=%d единицы=%d), база=(%d,%.0f) LVWIN=%d глиф=%d"),
		Level, (H != 0) ? 3 : (T != 0 ? 2 : 1), H, T, O, PX, PY, LW, DW);
}

// hudRefreshClassAndElementIcons@0xE79850 1:1.
void UAsdaMiniStateSubsystem::ApplyNameClassElements(const FString& CharName, int32 ClassId, const TArray<int32>& Elements)
{
	if (!bControlsReady) { return; }

	// ── Стихии: максимум по СТРОГОМУ «>» (при равенстве побеждает первый = Fire) ──
	if (Elements.Num() == 6)
	{
		int32 BestIdx = 0, BestVal = Elements[0];
		for (int32 i = 1; i < 6; ++i)
		{
			if (Elements[i] > BestVal) { BestVal = Elements[i]; BestIdx = i; }
		}
		for (int32 i = 0; i < 6; ++i) { SetVis(ElementIcons.IsValidIndex(i) ? ElementIcons[i] : nullptr, false); }
		SetVis(ElementIcons.IsValidIndex(BestIdx) ? ElementIcons[BestIdx] : nullptr, true);
		// ★ Родитель MINISTATE_ELE_Base остаётся СКРЫТЫМ — 1:1 с оригиналом: там он Visible="0",
		//   IDNum = −1 (недостижим для FindControlByID), а рендер не заходит в детей скрытого узла.
		//   То есть выбранная иконка выставляется, но НЕ РИСУЕТСЯ. Не «чиним» — воспроизводим.
		UE_LOG(LogAsdaMini, Verbose, TEXT("[мини-статус] стихия: выбран индекс %d (%d) — родитель ELE_Base скрыт, как в оригинале"),
			BestIdx, BestVal);
	}

	// ── Иконка класса по байту +0x438 ──
	if (ClassId >= 0)
	{
		for (int32 i = 0; i < 4; ++i) { SetVis(ClassIcons.IsValidIndex(i) ? ClassIcons[i] : nullptr, false); }
		int32 Which = -1;
		if (ClassId == 0)                            { Which = 0; }   // Tenderfoot (28)
		else if (ClassId >= 1  && ClassId <= 7)      { Which = 1; }   // Warrior    (29)
		else if (ClassId >= 12 && ClassId <= 18)     { Which = 2; }   // Archer     (30)
		else if (ClassId >= 23 && ClassId <= 29)     { Which = 3; }   // Wizard     (31)
		// 8..11, 19..22, >29 → НИ ОДНОЙ иконки. Это поведение оригинала, а не пропуск.
		if (Which >= 0) { SetVis(ClassIcons[Which], true); }
		else
		{
			UE_LOG(LogAsdaMini, Log, TEXT("[мини-статус] класс %d попал в «провал» диапазонов — иконки нет (как в оригинале)"), ClassId);
		}
	}

	if (CharNameText && !CharName.IsEmpty())
	{
		CharNameText->SetText(FText::FromString(CharName));
		LastNameApplied = CharName;   // ★ 06.10.2026: Tick сверяет с ним текущее имя
	}
}

void UAsdaMiniStateSubsystem::SetPvpModeIcon(bool bVisible)
{
	SetVis(PvpModeIcon, bVisible);
	UE_LOG(LogAsdaMini, Log, TEXT("[мини-статус] иконка PvP-режима (5300): %s"), bVisible ? TEXT("показана") : TEXT("скрыта"));
}

void UAsdaMiniStateSubsystem::SetPlusPackIcon(bool bVisible)
{
	SetVis(PlusPackIcon, bVisible);
	UE_LOG(LogAsdaMini, Log,
		TEXT("[мини-статус] иконка премиум-пакета (5110): %s ⚠ в ОРИГИНАЛЕ кода показа НЕТ (только резолв) — это ручная/консольная проверка"),
		bVisible ? TEXT("показана") : TEXT("скрыта"));
}

// ══════════════════════════════ ИСТОЧНИКИ ДАННЫХ ═══════════════════════════════════════════════
void UAsdaMiniStateSubsystem::OnVitalsChanged(int32 UserIdx, const UAsdaNetSubsystem::FAsdaVitals& V)
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || !AsdaSelf::IsSelfUserIdx(Net, UserIdx)) { return; }
	if (!bControlsReady)
	{
		ASDA_CTRACE_V(EAsdaCT::HpSelf, TEXT("своё HP %d/%d MP %d/%d (канал «%s») не показано: мини-статус ещё не собран"),
			V.HpCur, V.HpMax, V.MpCur, V.MpMax, *V.LastHpSrc);
		return;
	}
#if ASDA_COMBAT_TRACE
	{
		// Журнал боя: своё HP/MP «было → стало» — по тексту полос HUD (он хранит ровно показанное значение).
		const FString TrHpNew = FString::Printf(TEXT("%d/%d"), V.HpCur, V.HpMax);
		const FString TrMpNew = FString::Printf(TEXT("%d/%d"), V.MpCur, V.MpMax);
		const FString TrHpOld = HpText ? HpText->GetText().ToString() : FString();
		const FString TrMpOld = MpText ? MpText->GetText().ToString() : FString();
		if (TrHpOld != TrHpNew)
		{
			ASDA_CTRACE(EAsdaCT::HpSelf, TEXT("СВОЙ HP в HUD: «%s» → %s (%.1f%%), второе (призрачное) %d, канал «%s», мёртв=%s"),
				*TrHpOld, *TrHpNew, V.HpMax > 0 ? 100.f * (float)V.HpCur / (float)V.HpMax : 0.f, V.HpSecond, *V.LastHpSrc,
				IsSelfDeadExact() ? TEXT("да") : TEXT("нет"));
		}
		if (TrMpOld != TrMpNew)
		{
			ASDA_CTRACE(EAsdaCT::HpSelf, TEXT("СВОЙ MP в HUD: «%s» → %s (%.1f%%), второе (призрачное) %d"),
				*TrMpOld, *TrMpNew, V.MpMax > 0 ? 100.f * (float)V.MpCur / (float)V.MpMax : 0.f, V.MpSecond);
		}
	}
#endif
	ApplyHp(V.HpCur, V.HpMax);
	ApplyMp(V.MpCur, V.MpMax);
	ApplyVitalGhosts(V.HpSecond, V.HpMax, V.MpSecond, V.MpMax);
}

void UAsdaMiniStateSubsystem::OnSelfWeight(int32 Cur, int32 Max)
{
	// ★ 06.10.2026: значение, пришедшее сигналом, помечаем применённым. Иначе «догон» в Tick видел
	//   `SelfWeightCur != LastWeightCur` и применял ТОТ ЖЕ пакет второй раз в следующем кадре — эталон
	//   получил бы его один раз, а у нас второй проход давал нотис 2359 сразу после 2358.
	if (bControlsReady) { LastWeightCur = Cur; LastWeightMax = Max; }
	ApplyWeight(Cur, Max);
}

void UAsdaMiniStateSubsystem::OnSelfExpChanged(int32 Level, int64 /*Exp*/, int64 /*Gained*/,
                                               bool /*bFullExp*/, int32 /*SrcRenderId*/)
{
	// Эталон HUD_UpdateLevelDigits зовётся из Recv_LevelUp (4036) и hudRefreshFromCharacterStats;
	//   опыт мини-статус не рисует (полоса лежит в EXPBar_WIn — UAsdaHudSubsystem).
	if (Level > 0 && Level != LastLevelApplied) { ApplyLevel(Level); }
}

// 4206 команда 1 — эталон network.c:44111-44138. Ветка иконки и нотиса стоит под `CClass_GetField47565`
//   (`selectionFlag`, char+190260 = «это локальный игрок»), поэтому только свой персонаж.
//   ★ 06.10.2026: нотисы по обработчику — подтип > 0: loc 108114, а при подтипе 2 — 108131; подтип 0: loc 108132.
void UAsdaMiniStateSubsystem::OnPvpMode(int32 UserIdx, bool bOn, int32 SubType)
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || !AsdaSelf::IsSelfUserIdx(Net, UserIdx)) { return; }
	SetPvpModeIcon(bOn);
	// ★ 18.10.2026 (мелочь 7): локальный отбой (`TogglePvpMode`) — только чат; ответ сервера 4206 — по таблице маршрутов (пара эталона).
	PostPvpNotice(bOn ? ((SubType == 2) ? 108131 : 108114) : 108132, /*bChatOnly=*/Net->bPvpModeLocalBroadcast);
}

// ★ 06.10.2026: 4206 команды 2/0 — эталон network.c:44166-44186: «пакет про меня» → loc 108119 / 108120.
void UAsdaMiniStateSubsystem::OnPvpModeRefused(int32 UserIdx, int32 Cmd)
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || !AsdaSelf::IsSelfUserIdx(Net, UserIdx)) { return; }
	if (Cmd == 2)      { PostPvpNotice(108119); }
	else if (Cmd == 0) { PostPvpNotice(108120); }
}

// Значок главы отряда (9) — эталон `sub_9CCD30`: SetVisible(CHUD+0x21C, a2 != 0).
void UAsdaMiniStateSubsystem::SetPartyerIcon(bool bVisible)
{
	bPartyerOn = bVisible;
	SetVis(PartyerIcon, bVisible);
	UE_LOG(LogAsdaMini, Log, TEXT("[мини-статус] значок главы отряда (9): %s"), bVisible ? TEXT("показан") : TEXT("скрыт"));
}

// ★ 06.10.2026: 5102 — `Recv_GM_PARTY_PARTYMASTER_CHANGE@0xF39DA0` (network.c:39160-39185) → `Party_SetMasterLocal`
//   (social.c:419-455): обходит состав отряда; у найденного члена с этим charId ставит признак главы и, если
//   это я, показывает значок, иначе гасит. Не нашёлся — значок НЕ трогается (1:1).
//   Сигнал даёт `UAsdaPartySubsystem` из своей очереди — состав 5107 того же кадра к этому моменту уже применён.
//   Нотисы 1061/1062 выводит отряд (второго вывода здесь нет).
void UAsdaMiniStateSubsystem::OnPartyMasterChanged(int32 CharId, bool bFound, bool bMe)
{
	if (!bFound)
	{
		UE_LOG(LogAsdaMini, Log, TEXT("[мини-статус] 5102: charId=%d нет в составе отряда — значок не трогаем (эталон)"), CharId);
		return;
	}
	SetPartyerIcon(bMe);
}

// ══════════════════════════════ ЭМБЛЕМА КЛАНА НА ПОРТРЕТЕ (270/271) ═════════════════════════════
void UAsdaMiniStateSubsystem::SetClanMarkBaseVisible(bool bVisible)
{
	// Эталон `CHUD_SetVisible@0xE81F50`: if (CHUD+0x9B0) SetVisible(…, a2 != 0). Рисунок 271 — ребёнок базы.
	bClanMarkBaseShown = bVisible;
	SetVis(ClanMarkBase, bVisible);
}

const UAsdaNameplateComponent* UAsdaMiniStateSubsystem::SelfPlate() const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const AActor* Me = WS ? WS->LocalPlayerActor.Get() : nullptr;
	return Me ? Me->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
}

bool UAsdaMiniStateSubsystem::IsCampWarMap() const
{
	// Эталон `*(_BYTE *)(g_pLocalEntity + 69) == 1`: g_pLocalEntity — запись реестра ТЕКУЩЕЙ карты, +69 = campwar/PK.
	//   Та же колонка, что у кнопки 17520 мини-карты (`FAsdaMapRow::CampwarPkFlag`).
	//   ★ Ревизия (вне задачи): общая точка с панелью цели — `UAsdaClanFlag::IsCampWarMap`.
	return UAsdaClanFlag::IsCampWarMap(this);
}

bool UAsdaMiniStateSubsystem::IsSelfClanFlagOn() const
{
	const UAsdaNameplateComponent* NP = SelfPlate();
	return NP && NP->ClanId >= 0 && NP->bClanFlagOn;
}

// Порт `uiDrawPlayerPortrait@0xE81FF0` (ui_core.c:77886-77912).
void UAsdaMiniStateSubsystem::DrawPlayerPortraitMark()
{
	if (!bControlsReady) { return; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (IsCampWarMap())
	{
		// Ветка кампвара: `uiMakeAxisVec2(g_pCampWarClan, uv, *(g_LocalPlayer + 209016))` — лагерь 0/1/2 →
		//   (0,0)/(64,0)/(0,64), затем DrawTextureRect(271, "CampWar_MarkImage.tex", u, v, 64, 64) и SetVisible(270, 1).
		const int32 Camp = WS ? WS->GetSelfCampId() : -1;   // −2 пакета 6713 не было, −1 лагеря нет
		// Ячейка лагеря — общая точка `UAsdaClanFlag::ApplyCampMark` (её же берёт панель цели).
		//   [ПОРТ] лагерь вне 0..2 (эталон читает мусор стека) → пустая кисть; база всё равно показывается, как в эталоне.
		const bool bCell = UAsdaClanFlag::ApplyCampMark(ClanMarkImg, Camp, 32.f);
		SetClanMarkBaseVisible(true);
		UE_LOG(LogAsdaMini, Log, TEXT("[мини-статус] эмблема портрета: КАМПВАР, лагерь %d → ячейка атласа CampWar_MarkImage %s"),
			Camp, bCell ? TEXT("поставлена") : TEXT("НЕТ (пустая кисть)"));
		return;
	}
	// Ветка флага: DrawTextureRect(271, флаг, 23, 12, 64, 42); clanId == −1 → SetVisible(270, 0).
	const UAsdaNameplateComponent* NP = SelfPlate();
	FSlateBrush B;   // кисть испечённого флага
	if (NP && NP->ClanFlag.IsValid() && UAsdaClanFlag::MakePlateBrush(this, NP->ClanFlag, 32.f, B))
	{
		if (ClanMarkImg) { ClanMarkImg->SetBrush(B); }
	}
	else
	{
		AsdaUi::ClearImageBrush(ClanMarkImg);
	}
	if (!NP || NP->ClanId < 0)
	{
		SetClanMarkBaseVisible(false);
	}
	UE_LOG(LogAsdaMini, Log, TEXT("[мини-статус] эмблема портрета: флаг клана %d (дизайн %s), база %s"),
		NP ? NP->ClanId : -1, (NP && NP->ClanFlag.IsValid()) ? TEXT("есть") : TEXT("нет"),
		bClanMarkBaseShown ? TEXT("видна") : TEXT("скрыта"));
}

// Вход на карту — эталон game_main.c:3876 и 3910-3949 (порядок сохранён):
//   1) если карта кампвара (+69) — `uiDrawPlayerPortrait` (ветка кампвара сама показывает базу);
//   2) если карта не 19/23 — флаг клана включён ? (`CHUD_SetVisible(1)` + рисунок) : `CHUD_SetVisible(0)`.
void UAsdaMiniStateSubsystem::ApplyClanMarkOnMapEntry()
{
	if (!bControlsReady) { return; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	const int32 MapId = LM ? LM->CurrentMapId : -1;
	if (IsCampWarMap()) { DrawPlayerPortraitMark(); }
	if (MapId != kCampWarMapA && MapId != kCampWarMapB)
	{
		if (IsSelfClanFlagOn()) { SetClanMarkBaseVisible(true); DrawPlayerPortraitMark(); }
		else                    { SetClanMarkBaseVisible(false); }
	}
	bLastSelfFlagOn = IsSelfClanFlagOn();
	PendingClanMark.Reset(); PendingClanMarkWhy.Reset();   // вход на карту перекрывает накопленное
	UE_LOG(LogAsdaMini, Log, TEXT("[мини-статус] эмблема портрета: вход на карту %d (кампвар=%d) → база %s"),
		MapId, IsCampWarMap() ? 1 : 0, bClanMarkBaseShown ? TEXT("видна") : TEXT("скрыта"));
}

// ── Колбэки сети: только кладём событие (закон no-reentrant-net-callbacks) ──
void UAsdaMiniStateSubsystem::OnClanFlagNet(int32 ClanId, bool bFlagOn, const TArray<uint8>& /*Flag40*/)
{
	// Эталон обходит ВСЕ сущности с clanId; портрет трогает только свой (`j+190260 == 1`).
	const UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!WS || WS->GetSelfClanId() < 0 || WS->GetSelfClanId() != ClanId) { return; }
	PendingClanMark.Add(kClanEvFlag);
	PendingClanMarkWhy.Add(FString::Printf(TEXT("6542 флаг %s"), bFlagOn ? TEXT("вкл") : TEXT("выкл")));
}

void UAsdaMiniStateSubsystem::OnClanSnapshotNet(int32 CharId, int32 /*ClanId*/, const FString& /*ClanName*/,
	const FString& /*ClanRank*/, bool /*bFlagOn*/, const TArray<uint8>& /*Flag40*/)
{
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!AsdaSelf::IsSelfCharId(Net, CharId)) { return; }   // ★ N2 (В2-A): общая точка, политика «неизвестен ⇒ не мой» (как было)
	PendingClanMark.Add(kClanEvSnapshot);
	PendingClanMarkWhy.Add(TEXT("снимок клана своего (4135/6153)"));
}

void UAsdaMiniStateSubsystem::OnClanResetNet(int32 CharId, const FString& CharName)
{
	// Свой: charId выбранного персонажа либо имя своего (мир сверяет так же, эталон 4133 — userIdx + имя).
	UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const bool bById   = Net && Net->SelectedCharId != 0 && CharId == Net->SelectedCharId;
	const bool bByName = WS && !CharName.IsEmpty() && WS->GetSelfCharName().Equals(CharName);
	if (!bById && !bByName) { return; }
	PendingClanMark.Add(kClanEvHide);
	PendingClanMarkWhy.Add(TEXT("клан сброшен (4107/4117/4133)"));
}

void UAsdaMiniStateSubsystem::OnClanBaseInfoPushNet(int32 Opcode, const TArray<uint8>& Body)
{
	// 4132 код 2 — `uiTradeWindowResetState@0xBCB530` (ui_core.c:17191-17215): сброс блока окна сообщества
	//   и `CHUD_SetVisible(0)`. Данные персонажа (флаг) при этом НЕ меняются — 1:1.
	if (Opcode != 4132 || Body.Num() < 1 || Body[0] != 2) { return; }
	PendingClanMark.Add(kClanEvHide);
	PendingClanMarkWhy.Add(TEXT("4132 код 2"));
}

void UAsdaMiniStateSubsystem::ProcessPendingClanMark()
{
	if (!bControlsReady || PendingClanMark.Num() == 0) { return; }
	for (int32 i = 0; i < PendingClanMark.Num(); ++i)
	{
		const uint8 Ev = PendingClanMark[i];
		const FString& Why = PendingClanMarkWhy.IsValidIndex(i) ? PendingClanMarkWhy[i] : FString();
		const bool bNow = IsSelfClanFlagOn();   // флаг ПОСЛЕ обновления миром
		if (Ev == kClanEvHide)
		{
			SetClanMarkBaseVisible(false);
		}
		else if (Ev == kClanEvSnapshot)
		{
			// entity_char.c:44687-44697: свой + флаг → CHUD_SetVisible(1) + рисунок; без флага → CHUD_SetVisible(0).
			if (bNow) { SetClanMarkBaseVisible(true); DrawPlayerPortraitMark(); }
			else      { SetClanMarkBaseVisible(false); }
		}
		else if (Ev == kClanEvFlag)
		{
			// social.c:3640-3650: если флаг БЫЛ выключен — CHUD_SetVisible(1) + рисунок; при включённом флаге
			//   ставит [148]=[152]=1 (social.c:3656-3657), и `CharFx_UpdateKingCrown` (effects_particles.c:3620-3634)
			//   перепекает флаг, показывает базу и рисует его (это не шаг загрузки game_main.c:3185-3203).
			if (bNow)
			{
				SetClanMarkBaseVisible(true); DrawPlayerPortraitMark();
			}
			else if (CVarClanMarkHideOnFlagOff.GetValueOnGameThread() != 0)
			{
				SetClanMarkBaseVisible(false);   // [ПОРТ] исправление дефекта оригинала
			}
			else if (!bLastSelfFlagOn)
			{
				SetClanMarkBaseVisible(true); DrawPlayerPortraitMark();   // [ОРИГИНАЛ] «выкл → выкл» зажигает базу
			}
		}
		bLastSelfFlagOn = bNow;
		UE_LOG(LogAsdaMini, Log, TEXT("[мини-статус] эмблема портрета: %s → база %s (флаг %s)"),
			*Why, bClanMarkBaseShown ? TEXT("видна") : TEXT("скрыта"), bNow ? TEXT("вкл") : TEXT("выкл"));
	}
	PendingClanMark.Reset(); PendingClanMarkWhy.Reset();
}

// ★ 06.10.2026 (ревизия МЕЛК-3): ИМЕННО смерть (состояние действия 6), а не «недееспособен» вообще.
//   `IsIncapacitated()` по шапке AsdaPlayerCharacter.h будет расширен нокдауном 7/8/9 — слои при нокдауне
//   эталон не гасит. Источники: поза павна `AnimState == Dead` или сетевое зеркало CUser+79 `bSelfDead`.
bool UAsdaMiniStateSubsystem::IsSelfDeadExact() const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (Net && Net->bSelfDead) { return true; }
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const AAsdaPlayerCharacter* Ch = WS ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get()) : nullptr;
	return Ch && Ch->AnimState == EAsdaPlayerState::Dead;
}

// ★ 06.10.2026: смерть — эталон ui_core.c:75482-75497 (каждый кадр при состоянии 6).
void UAsdaMiniStateSubsystem::ApplyDeathGaugeReset()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || !bControlsReady) { return; }
	const int32 Self = AsdaSelf::GetUserIdxOr(GI, INDEX_NONE);
	UAsdaNetSubsystem::FAsdaVitals* V = Net->VitalsByUserIdx.Find(Self);
	const bool bHadSecond = V && (V->HpSecond > 0 || V->MpSecond > 0);
	// «Второе» HP/MP персонажа = 0 (эталон: char+0x4CC/+0x4D0 и их пары-«аукс»).
	if (V) { V->HpSecond = 0; V->MpSecond = 0; }
	// Слои 37/89 нулевой ширины (эталон SetWidth(pHpBuff/pMpBuff, 0)); видимость не меняется.
	if (bHadSecond || !bDeathResetLogged)
	{
		ApplyVitalGhosts(0, V ? V->HpMax : 1, 0, V ? V->MpMax : 1);
	}
	if (!bDeathResetLogged)
	{
		ASDA_CTRACE(EAsdaCT::Death, TEXT("СВОЯ СМЕРТЬ в HUD: состояние 6 (bSelfDead=%s, поза Dead) — призрачные слои HP/MP и «второе» HP/MP обнулены; HP в HUD «%s»"),
			Net->bSelfDead ? TEXT("да") : TEXT("нет"), HpText ? *HpText->GetText().ToString() : TEXT("нет"));
		bDeathResetLogged = true;
		UE_LOG(LogAsdaMini, Log, TEXT("[мини-статус] персонаж мёртв (состояние 6) — слои HPBuff/MPBuff и «второе» HP/MP обнулены (эталон)"));
	}
}

// ★★★ 6585 — ЕДИНСТВЕННЫЙ драйвер иконки Plus Package (реверс 2026-08-06, ПОПРАВКА к прошлому кругу).
//   Эталон: приём `recvWingDataTableInsert@0xEE3170` (network.c:32934) кладёт 11-байтную запись в
//   очередь объекта `g_WingDataTable`; раз в 2000 мс её разбирает `charSitStatePoll@0xEE3250`
//   (entity_char.c:49945-50012, вызов game_main.c:1526):
//       pChar[1].gap_A42[15140] = флаг (rec+10)     ← «плюс-пакет активен»
//       pChar[1].gap_A42[15148] = wingId (rec+4)    ← id записи PlusPackageTable
//       если это ЛОКАЛЬНЫЙ игрок:  g_WingDataTable[0] = wingId, g_BuffRemainSeconds = rec+6 (u32)
//   и В НАЧАЛЕ той же функции (entity_char.c:49969-49979):
//       if (g_pAutoStateControl) { if (LocalPlayer.gap_A42[15140]) SetVisible(1); else SetVisible(0); }
//   ★ `g_pAutoStateControl` (VA 0x2C3BE14) — это И ЕСТЬ `pluscpack`: в live.dmp по указателю лежит
//     CControl с IDNum 5110 (@+0x16C), скином «pluspac» и idc «pluscpack». Прошлый вывод
//     «кода показа в срезе нет» был ошибочным (закон dead-code-markings-suspect).
void UAsdaMiniStateSubsystem::OnWingState(int32 /*CharId*/, int32 InWingId, int32 RemainSec, bool bOn)
{
	WingId = InWingId;
	WingRemainSec = RemainSec;
	bWingActive = bOn;
	SetVis(PlusPackIcon, bWingActive);
	UE_LOG(LogAsdaMini, Log,
		TEXT("[мини-статус] 6585: крыло=%d остаток=%d с (%d дн.) флаг=%d → иконка Plus Package %s"),
		WingId, WingRemainSec, WingRemainSec / 86400, bWingActive ? 1 : 0,
		bWingActive ? TEXT("ПОКАЗАНА") : TEXT("скрыта"));
}

void UAsdaMiniStateSubsystem::RefreshAll()
{
	if (!bControlsReady) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!Net) { return; }

	if (const UAsdaNetSubsystem::FAsdaVitals* V = Net->GetVitals(AsdaSelf::GetUserIdxOr(GetGameInstance(), INDEX_NONE)))   // ★ волна 1 (E2)
	{
		ApplyHp(V->HpCur, V->HpMax);
		ApplyMp(V->MpCur, V->MpMax);
		ApplyVitalGhosts(V->HpSecond, V->HpMax, V->MpSecond, V->MpMax);
	}
	LastWeightCur = Net->SelfWeightCur; LastWeightMax = Net->SelfWeightMax;
	ApplyWeight(Net->SelfWeightCur, Net->SelfWeightMax);
	const int32 Lvl = ResolveSelfLevel();
	if (Lvl > 0) { ApplyLevel(Lvl); }

	// Класс: блок 1017 (+28) / 4005-self (@+38); фолбэк — запись char-select (0x3EE rec+24).
	const int32 ClassId = ResolveSelfClass();
	LastClassApplied = ClassId;
	// Иконка Plus Package: состояние из последнего 6585 (эталон charSitStatePoll по флагу rec+10).
	SetVis(PlusPackIcon, bWingActive);
	const FString Name = WS ? WS->GetSelfCharName() : FString();
	// Стихии (6×i16 @ +0x4A8) в порт ещё не заведены ни одним пакетом → пустой массив = «не менять».
	ApplyNameClassElements(Name, ClassId, TArray<int32>());
	// ЭТАП 2: иконки активных эффектов и ивент-бонусов (с таймерами — как эталонный Tray_Update).
	RefreshBuffIcons(/*bWithTimers=*/true);
	RefreshEventIcons();
}
