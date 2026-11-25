// Copyright (c) 2026 @sitxovski (Discord)
// AsdaCharaInfoSubsystem_Pages.cpp — ★ R3-c 12.11.2026 (разрез AsdaCharaInfoSubsystem.cpp, дословный перенос): страницы (основная/данные/рыбалка/стихии/эмоции), полосы HP/MP/вес/опыт, события своего персонажа.
//   Общее для частей — в AsdaCharaInfoSubsystem_Private.h; шапка и реверс — в AsdaCharaInfoSubsystem.cpp.
#include "UI/AsdaCharaInfoSubsystem.h"

#include "Items/AsdaItemTableSubsystem.h" // В6 рыбалки: справочник удочки (подтип/вариант/linkCode/linkedGroupCode)
#include "AsdaBytes.h"                     // В6 рыбалки: опции записи 57 б (RdI16)

#include "AsdaCharaInfoSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaCharaInfoSubsystem*.cpp

namespace CharInfoLoc {
	static constexpr int32 kTitleEmpty   = 2433;    // «Выбрать звание»
	static constexpr int32 kJobFirst     = 12556;   // 12556 + класс/подкласс — название профессии
	static constexpr int32 kFactionFirst = 10191;   // 10191 + фракция — Свет/Тьма/Хаос
}

// ★★★ [ОРИГИНАЛ] 08.11.2026 (пачка 15, баг 9 «Honor rank показывает 1 вместо названия»). Эталон
//   `CFactionUI_UpdateInfo@0xB932C0` пишет в 926 `CHARAINFO_CAMPWAR-T3-S` ИМЯ воинского звания:
//   `sub_935854(g_pCampWarClan, звание)` → `sub_CA1530@0xCA1530` (запись 40 б таблицы `CampWar_HonorTable.BIN`,
//   имя = запись+8; номер вне 0…N → пусто). Порт печатал номер «%d». Таблица в порту — `DT_WarRank`
//   (оверлей языка `WarRank`), читатель — `UAsdaWarShopSubsystem::RankName` (второй потребитель того же `sub_935854`).
static ASDA_DEV_CVAR(int32, CVarCharInfoHonorRankName,
	TEXT("asda.CharInfo.HonorRankName"), 1,
	TEXT("1 = как оригинал: «Звание» (926) — название воинского звания из таблицы званий; 0 = прежний порт — номер звания."),
	ECVF_Default);

// ★★★ [ОРИГИНАЛ] 08.11.2026 (пачка 15, баг 8). «Ур. ремесла» 942 эталон печатает `"%d", (__int16)g_nShopValueA`
//   (`shopUiWnd_setValueText@0xB94C50`): только МЛАДШЕЕ слово — в нём уровень ремесла (6597/6603/6605, ключ
//   `asda.Craft.LevelToCharInfo`), старшее слово пишет 5036 целым dword. Порт печатал весь int32 с разрядами.
static ASDA_DEV_CVAR(int32, CVarCharInfoCraftLowWord,
	TEXT("asda.CharInfo.CraftLowWord"), 1,
	TEXT("1 = как оригинал: «Ур. ремесла» — младшее слово очков ремесла (int16, «%d»); 0 = прежний порт — весь int32 с разрядами."),
	ECVF_Default);

// Текст 926 «Звание»: имя звания (эталон `sub_935854`→`sub_CA1530`, «%s» записи+8) или номер (ключ 0, прежний порт).
static FString HonorRankText(UGameInstance* GI, int32 Grade)
{
	if (CVarCharInfoHonorRankName.GetValueOnGameThread() == 0)
	{
		return Grade > 0 ? FString::Printf(TEXT("%d"), Grade) : FString();
	}
	const UAsdaWarShopSubsystem* Shop = GI ? GI->GetSubsystem<UAsdaWarShopSubsystem>() : nullptr;
	return Shop ? Shop->RankName(Grade) : FString();   // номер вне таблицы / 0 → пусто, как `sub_CA1530`
}

// Текст 942 «Ур. ремесла» по зеркалу `g_nShopValueA` (−1 = ещё не приходило → «0»).
static FString CraftSkillText(int32 ShopValueA)
{
	if (ShopValueA < 0) { return FString(TEXT("0")); }
	if (CVarCharInfoCraftLowWord.GetValueOnGameThread() == 0) { return AsdaUi::FormatThousands(ShopValueA); }
	return FString::Printf(TEXT("%d"), (int32)(int16)(ShopValueA & 0xFFFF));   // `sprintf("%d", (__int16)g_nShopValueA)`
}

// ★★★ «НАВЫК РЫБАЛКИ» (997 `STATUS_fishing2`) — ЭТАЛОННЫЙ ФОРМАТ (волна В6 рыбалки, 08.10.2026).
//   ★ Прежний вывод «Д13 — у 997 нет писателя, в оригинале строка всегда пустая» (`char_info_fix2.md` §5.4) ОПРОВЕРГНУТ
//   вторым методом — сканом бинаря по абсолютному адресу `g_pStatValueLabel` 0x2BDFE04 (= `g_pCharInfoWnd` + 0x20C,
//   `mov [ecx+0x20c], eax` после `FindControlByID(0x3E5)` @0xB92699): `Reports/owner_1008/fishing_parts/ui_book.md` §12.
//   Поиск по `this+131` ссылку не находил — поле пишется через глобальный указатель. Писателей ТРИ, формат у всех один —
//   `"%d(Max.%d) <FC=FFFFFF00>+ %d</F>"` = навык, `(u8)g_LocalPlayer+1082 (уровень) × 10.0`, `itemCalcEnchantBonus()`:
//     • `uiUpdateStatValueLabel@0xFCAAF0` — пакет 6164 (g:606445-606468);
//     • `netOnItemUpgradeResult@0xFCCF50` (миснейм; результат поимки 6172) — g:607472-607484;
//     • `CItemUpgrade_RefreshStatPreview@0xB94480` (миснейм; из `UI_FillEquipDetailPanel@0xB93A60`) — КАЖДАЯ перерисовка
//       окна персонажа, навык из `+226096` (g:217422-217427).
//   Навык хранит автомат рыбалки В1 (`UAsdaFishingSubsystem::GetFishingSkill`, сигнал `OnFishSkillChanged` — 6164/6172/сброс).
// Ключ `asda.CharInfo.FishingZero` — ОТКАТ к варианту 23.08.2026 (требование владельца «когда навык 0 — пусть пишет 0, а не пустоту»):
//   0 (умолч., ★ W3fix2 09.10.2026) — как оригинал: эталонный формат ВСЕГДА; до прихода 6164/6172 навык 0 → `0(Max.N) + B`.
//     Ревизия `fishing_impl_W3fix_W6_REVIEW.md` (МЕЛК-10): у эталона строка до 6164 НЕ пустая — поле навыка по умолчанию 0, а
//     перерисовка окна (`CItemUpgrade_RefreshStatPreview`, g:217422-217427) пишет формат всегда. Требование владельца 23.08
//     («0, а не пустота») исходило из ложного Д13 «в оригинале строка пуста» и эталонным форматом удовлетворено — отступление
//     по умолчанию не нужно.
//   1 — [ПОРТ] прежний вариант: пока навык в сессии не пришёл, строка — голый «0»; после — эталонный формат.
static ASDA_DEV_CVAR(int32, CVarCharInfoFishingZero,
	TEXT("asda.CharInfo.FishingZero"), 0,
	TEXT("«Навык рыбалки» 997: 0 = как оригинал (умолчание) — эталонный формат «навык(Max.уровень×10) + бонус удочки» всегда, ")
	TEXT("до прихода навыка (6164/6172) навык 0; 1 = [ПОРТ] откат к варианту 23.08 — до прихода навыка голый «0»."),
	ECVF_Default);

// ═══════════════════════════ НАПОЛНЕНИЕ ═════════════════════════════════════════════════════════
void UAsdaCharaInfoSubsystem::RefreshAll()
{
	if (!Win) { return; }
	RefreshBasicPage();
	RefreshDataPage();
	RefreshElementPage();
	RefreshEmotionTab();
	SwitchTabMode(CurTab);
	SelectStatPage(CurStatPage);
}

// Порт `UI_FillEquipDetailPanel@0xB93A60` (левая часть) + `uiSetThousandsText` + `uiEquipPanelSetGradeText`.
void UAsdaCharaInfoSubsystem::RefreshBasicPage()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaWorldSubsystem* World = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;

	if (World) { SetText(NameText.Get(), World->GetSelfCharName()); }
	if (Net)
	{
		SetText(LevelText.Get(), AsdaUi::FormatThousands((int32)Net->SelfLevel));
		// Профессия: loc `12556 + подкласс`, а если подкласса нет — `12556 + класс` (эталон).
		const int32 Cls = ResolveSelfClassId();
		if (Cls >= 0) { SetText(JobText.Get(), Loc(CharInfoLoc::kJobFirst + Cls)); }
		// Знак зодиака (эталон: `cfgFindRecordByKey65` по `CCharacter+0x45F`; в порту это
		//   `SelfConstellation`, 0xFF = неизвестен).
		if (Net->SelfConstellation != 0xFF)
		{
			// ★ 15.09 (dt_gc_hold, хвост п.2): DT_Constellation — сильно у подсистемы знаков на сессию (цепочка FAsdaText слабая).
			if (const UAsdaConstellationSubsystem* Cons = GI ? GI->GetSubsystem<UAsdaConstellationSubsystem>() : nullptr) { Cons->HoldGameTables(); }
			SetText(ConsText.Get(), FAsdaCharRecord::ConstellationName(Net->SelfConstellation));
		}
		// Полосы — из тех же значений, что кормят мини-статус (второго источника нет).
		// ★ Волна 1 (E2): свой номер — `AsdaSelf::GetUserIdxOr` (до 4003 полосы не читают запись ЧУЖОГО userNum 0).
		if (const UAsdaNetSubsystem::FAsdaVitals* V = Net->GetVitals(AsdaSelf::GetUserIdxOr(GetGameInstance(), INDEX_NONE)))
		{
			ApplyHp(V->HpCur, V->HpMax);
			ApplyMp(V->MpCur, V->MpMax);
		}
		ApplyWeight(Net->SelfWeightCur, Net->SelfWeightMax);
		ApplyExp((int32)Net->SelfLevel, Net->SelfExp);
	}
	// Деньги персонажа (эталонный `STATUS_ST0` ← `CCharacter+0x44C`); в порту золото держит мир.
	if (World && World->SelfGold >= 0)
	{
		SetText(CashValue.Get(), AsdaUi::FormatThousands((int32)FMath::Clamp<int64>(World->SelfGold, 0, MAX_int32)));
	}
	// Фракция — по последним данным пакета 6713.
	OnSelfFaction(LastFactionId, LastFactionPoints, LastFactionGrade);
	OnSelfTitleChanged();
}

// Порт `UI_FillEquipDetailPanel` (правая часть) + `CItemUpgrade_RefreshStatPreview@0xB94480`.
void UAsdaCharaInfoSubsystem::RefreshDataPage()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }

	// Поднять поддерево страницы (импортёр свернул его рекурсией от Visible="0" родителя).
	auto Show = [](UWidget* W) { if (W) { W->SetVisibility(ESlateVisibility::SelfHitTestInvisible); } };
	for (int32 i = 0; i < 12; ++i)
	{
		Show(StatLabels.IsValidIndex(i) ? StatLabels[i].Get() : nullptr);
		Show(StatFmt.IsValidIndex(i) ? StatFmt[i].Node.Get() : nullptr);
	}

	const UAsdaNetSubsystem::FAsdaSelfStatBlock& B = Net->SelfStatBlock;
	const UAsdaNetSubsystem::FAsdaSelfBaseStats& S = Net->SelfBaseStats;

	// ST1…ST5 — из 68-байтного блока (см. отчёт §3.2)
	// ★★★ ВСЕ значения пишем через `SetTextFmt` — ЕДИНУЮ точку разбора тегов (`char_info_fix2.md` §1).
	//   Теги реально несут только ST6…ST11 («<FC=FFFF8000>+ N</F>»), но точка записи в эталоне одна
	//   (`CControl_SetText` + разбор в рендере текста), и заводить вторую в порту нельзя.
	SetTextFmt(StatFmt[0], RangeText(B.PhysAtkMin, B.PhysAtkMax));                 // 931 Атака
	SetTextFmt(StatFmt[1], RangeText(B.MagAtkMin,  B.MagAtkMax));                  // 932 Маг. атака
	SetTextFmt(StatFmt[2], RangeText(B.DefMin,     B.DefMax));                     // 933 Защита
	SetTextFmt(StatFmt[3], AsdaUi::FormatThousands(B.MagDefense));                 // 934 Маг. защита
	SetTextFmt(StatFmt[4], RangeText(B.BlockMin,   B.BlockMax));                   // 935 Шанс блока
	// ST6…ST11 — из 36-байтного блока: «база +/- прибавка» (эталонный формат с тегом цвета)
	for (int32 i = 0; i < 6; ++i)
	{
		SetTextFmt(StatFmt[5 + i], BaseBonusText(S.Base[i], S.Bonus[i]));
	}
	// ST12 — очки ремесла (эталонный `g_nShopValueA`); −1 = ещё не приходили.
	SetTextFmt(StatFmt[11], CraftSkillText(Net->SelfCraftSkill));

	// Навык рыбалки 997: эталон пишет его в КАЖДОЙ перерисовке окна (`CItemUpgrade_RefreshStatPreview@0xB94480`,
	//   g:217422-217427; разбор — у `CVarCharInfoFishingZero`).
	Show(FishRow.Get()); Show(FishLabel.Get()); Show(FishFmt.Node ? FishFmt.Node.Get() : FishValue.Get());
	RefreshFishSkill();
}

// `itemCalcEnchantBonus@0xFC8010` (g:605509-605537, дизасм 0xFC8010…0xFC80E4): запись надетого `CCharacter+0x835` =
//   `+1588 + 57·9` = экип-слот 9 (оружие — удочка); нет записи/справочника — 0. Опции 3 и 4 (пары +29+4i: i16 код, i16
//   значение — `[rec + 4i + 0x1D]`/`[rec + 4i + 0x1F]`) с кодом 96 суммируются; удочка (подтип +0x84 == 10, вариант +0x90 == 1)
//   с `linkCode` (+0x60) 0 или 2 добавляет `linkedGroupCode` (+0x64).
int32 UAsdaCharaInfoSubsystem::RodFishBonus() const
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemInstance* It = Inv ? Inv->GetCell(3, 9) : nullptr;
	if (!It || !IT) { return 0; }
	const FAsdaItemRecord* R = IT->GetItem(It->ItemCode);   // `CItemManager_GetItemRecord(*rec)`
	if (!R) { return 0; }
	int32 Bonus = 0;
	if (It->Raw.Num() >= FAsdaItemInstance::kRecordSize)
	{
		for (int32 i = 3; i < 5; ++i)
		{
			if (AsdaBytes::RdI16(It->Raw.GetData() + 29 + 4 * i) == 96) { Bonus += AsdaBytes::RdI16(It->Raw.GetData() + 31 + 4 * i); }
		}
	}
	if (R->ItemSubType == 10 && R->ItemVariant == 1)
	{
		if (R->LinkCode == 0) { Bonus += R->LinkedGroupCode; }
		if (R->LinkCode == 2) { Bonus += R->LinkedGroupCode; }
	}
	return Bonus;
}

// Текст строки 997: `sprintf("%d(Max.%d) <FC=FFFFFF00>+ %d</F>", навык, (unsigned __int64)((double)уровень * 10.0), бонус)`.
FString UAsdaCharaInfoSubsystem::FishSkillText() const
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaFishingSubsystem* Fish = GI ? GI->GetSubsystem<UAsdaFishingSubsystem>() : nullptr;
	const bool bKnown = Fish && Fish->IsFishingSkillKnown();
	if (!bKnown && CVarCharInfoFishingZero.GetValueOnGameThread() != 0)
	{
		return AsdaUi::FormatThousands(0);   // [ПОРТ] откат к варианту 23.08 (ключ 1): до прихода навыка — голый «0»
	}
	const int32 Skill = Fish ? Fish->GetFishingSkill() : 0;
	const int32 MaxV = (int32)(uint64)((double)(uint8)AsdaSelf::GetLevel(GI) * 10.0);
	return FString::Printf(TEXT("%d(Max.%d) <FC=FFFFFF00>+ %d</F>"), Skill, MaxV, RodFishBonus());
}

// Записать 997 (через единую точку тегов). Узла нет (окно ещё не собиралось) — нечего писать: соберётся с `RefreshDataPage`.
void UAsdaCharaInfoSubsystem::RefreshFishSkill()
{
	if (!FishFmt.Node) { return; }
	SetTextFmt(FishFmt, FishSkillText());
}

// 6164 / 6172 / сброс навыка (делегат автомата В1): эталон пишет 997 сразу (`uiUpdateStatValueLabel`, `netOnItemUpgradeResult`).
void UAsdaCharaInfoSubsystem::OnFishSkillChanged(int32 /*Skill*/)
{
	if (!Win) { return; }
	RefreshFishSkill();
}

// Порт `ItemStatPanel_UpdateBonusFields@0xB94D50` + `bagTab_ShowMaxCountTab@0xB95830`.
void UAsdaCharaInfoSubsystem::RefreshElementPage()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	const UAsdaNetSubsystem::FAsdaSelfStatBlock& B = Net->SelfStatBlock;

	auto Show = [](UWidget* W) { if (W) { W->SetVisibility(ESlateVisibility::SelfHitTestInvisible); } };
	Show(EleHeadAtk.Get()); Show(EleHeadDef.Get());

	// Формат эталона: «+N» при положительном, «-N» при отрицательном, «N» при нуле.
	auto Signed = [](int32 V) -> FString
	{
		if (V > 0) { return FString::Printf(TEXT("+%d"), V); }
		if (V < 0) { return FString::Printf(TEXT("-%d"), -V); }
		return TEXT("0");
	};
	for (int32 i = 0; i < 6; ++i)
	{
		Show(EleAtkLabels.IsValidIndex(i) ? EleAtkLabels[i].Get() : nullptr);
		Show(EleAtkValues.IsValidIndex(i) ? EleAtkValues[i].Get() : nullptr);
		Show(EleDefLabels.IsValidIndex(i) ? EleDefLabels[i].Get() : nullptr);
		Show(EleDefValues.IsValidIndex(i) ? EleDefValues[i].Get() : nullptr);
		SetText(EleAtkValues.IsValidIndex(i) ? EleAtkValues[i].Get() : nullptr, Signed(B.EleAtk[i]));
		SetText(EleDefValues.IsValidIndex(i) ? EleDefValues[i].Get() : nullptr, Signed(B.EleDef[i]));
	}
	// ★ Значок ДОМИНИРУЮЩЕЙ стихии: эталон показывает РОВНО ОДИН — с максимальной атакой.
	int32 Best = 0;
	for (int32 i = 1; i < 6; ++i) { if (B.EleAtk[i] > B.EleAtk[Best]) { Best = i; } }
	for (int32 i = 0; i < 6; ++i)
	{
		if (!EleDomIcons.IsValidIndex(i) || !EleDomIcons[i]) { continue; }
		AsdaUi::SetShownDecor(EleDomIcons[i], i == Best);
	}
}

// Порт `ActionUI_RefreshIcons@0xC52210`: три лотка, иконка = имя из `emotiontip.bin` + «.tex».
void UAsdaCharaInfoSubsystem::RefreshEmotionTab()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
	if (!Hb) { return; }

	int32 Filled = 0;
	for (FAsdaEmotionSlotUi& S : EmotionSlots)
	{
		if (S.Group < 0) { continue; }
		const bool bHasRecord = (S.IndexInGroup < Hb->EmotionCountInGroup(S.Group));
		if (S.Base) { S.Base->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
		if (!S.Icon) { continue; }
		FString Tex; bool bFull = false;
		if (!bHasRecord || !Hb->ResolveEmotionIcon(S.Code, Tex, bFull))
		{
			// Пустой слот лотка — у эталона их тоже больше, чем записей (дефект оригинала, п. 7).
			AsdaUi::ClearImageBrush(S.Icon.Get());
			S.Icon->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		AsdaUi::ApplyFlatJuiIcon(S.Icon.Get(), Tex, bFull ? 64.f : 32.f);
		S.Icon->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		++Filled;
	}
	// Курсоры выделения гасим — эталон показывает их только на выбранном слоте (`uiDlgUpdateDragIcon`).
	for (int32 g = 0; g < EmotionCursors.Num(); ++g)
	{
		if (EmotionCursors[g]) { EmotionCursors[g]->SetVisibility(ESlateVisibility::Collapsed); }
	}
	UE_LOG(LogAsdaCharInfo, Log, TEXT("[эмоции] лотки заполнены: иконок %d из %d слотов"),
		Filled, EmotionSlots.Num());
}

// ═══════════════════════════ ПОЛОСЫ ═════════════════════════════════════════════════════════════
// Порт `uiBar_SetCurMax_Slot33/34/36` и `uiBar_SetPercent_Slot35`: значение = процент, текст рядом.
static void ClipBar(const FAsdaMiniBarFill& Bar, float Frac)
{
	// ★ Волна 3: общая точка `FAsdaGaugeFill::Clip` (≡ `UAsdaMiniStateSubsystem::ClipFill`).
	if (!Bar.IsValid()) { return; }
	FAsdaGaugeFill::Clip(Bar.Fill, Bar.BaseLeft, Bar.FullW, Frac);
}

// ★ Догоняющий ход полос окна характеристик — тот же закон и та же настройка
//   `asda.UI.BarSmoothSec`, что у мини-статуса, плашек и панели цели.
void UAsdaCharaInfoSubsystem::TickVitalBars(float DeltaSeconds)
{
	const float Sec = FAsdaBarSmoother::SmoothSeconds();
	if (HpSmooth.Advance(DeltaSeconds, Sec)) { ClipBar(HpBar, HpSmooth.Shown); }
	if (MpSmooth.Advance(DeltaSeconds, Sec)) { ClipBar(MpBar, MpSmooth.Shown); }
}

void UAsdaCharaInfoSubsystem::ApplyHp(int32 Cur, int32 Max)
{
	LastHpCur = Cur; LastHpMax = Max;
	if (Max <= 0 || Cur < 0) { return; }
	HpSmooth.SetTarget((float)Cur / (float)Max);   // ★ заливка догоняет в TickVitalBars
	ClipBar(HpBar, HpSmooth.Shown);
	SetText(HpText.Get(), FString::Printf(TEXT("%s / %s"),
		*AsdaUi::FormatThousands(Cur), *AsdaUi::FormatThousands(Max)));
}

void UAsdaCharaInfoSubsystem::ApplyMp(int32 Cur, int32 Max)
{
	LastMpCur = Cur; LastMpMax = Max;
	if (Max <= 0 || Cur < 0) { return; }
	MpSmooth.SetTarget((float)Cur / (float)Max);   // ★ заливка догоняет в TickVitalBars
	ClipBar(MpBar, MpSmooth.Shown);
	SetText(MpText.Get(), FString::Printf(TEXT("%s / %s"),
		*AsdaUi::FormatThousands(Cur), *AsdaUi::FormatThousands(Max)));
}

void UAsdaCharaInfoSubsystem::ApplyWeight(int32 Cur, int32 Max)
{
	if (Max <= 0 || Cur < 0) { return; }   // эталон: `if (max <= 0) return 0` — полоса не трогается
	ClipBar(WeightBar, (float)Cur / (float)Max);
	SetText(WeightText.Get(), FString::Printf(TEXT("%s / %s"),
		*AsdaUi::FormatThousands(Cur), *AsdaUi::FormatThousands(Max)));
}

// Эталон пишет в 915 строку «%.2lf %%» — ПРОЦЕНТ, а не «текущий/следующий».
void UAsdaCharaInfoSubsystem::ApplyExp(int32 Level, int64 Exp)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaHudSubsystem* Hud = GI ? GI->GetSubsystem<UAsdaHudSubsystem>() : nullptr;
	if (!Hud) { return; }
	// ★ Долю опыта считает ОДНА подсистема на порт — HUD (у него таблица порогов `charexp`).
	//   Здесь берём готовое число: заводить второй расчёт того же значения нельзя (эталон в окне
	//   тоже не считает — `uiBar_SetPercent_Slot35` получает процент от `HUD_UpdateExpBar`).
	const float Frac = Hud->GetExpFraction();
	if (Frac < 0.f) { return; }
	ClipBar(ExpBar, Frac);
	SetText(ExpText.Get(), FString::Printf(TEXT("%.2f %%"), Frac * 100.f));
}

// ═══════════════════════════ ПОДПИСКИ ═══════════════════════════════════════════════════════════
void UAsdaCharaInfoSubsystem::OnVitalsChanged(int32 UserIdx, const UAsdaNetSubsystem::FAsdaVitals& V)
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || !AsdaSelf::IsSelfUserIdx(Net, UserIdx)) { return; }
	ApplyHp(V.HpCur, V.HpMax);
	ApplyMp(V.MpCur, V.MpMax);
}

void UAsdaCharaInfoSubsystem::OnSelfWeight(int32 Cur, int32 Max) { ApplyWeight(Cur, Max); }

void UAsdaCharaInfoSubsystem::OnSelfExpChanged(int32 Level, int64 Exp, int64 /*Gained*/, bool /*bFull*/, int32 /*Src*/)
{
	ApplyExp(Level, Exp);
	SetText(LevelText.Get(), AsdaUi::FormatThousands(Level));
}

// ★ 18.10.2026: ветка смены профессии 5042 зовёт `UI_FillEquipDetailPanel` целиком (network.c:21850) — у нас `RefreshAll`
//   (сам молчит, пока окна нет; при следующем открытии окно и так строится по живым полям).
void UAsdaCharaInfoSubsystem::OnSelfJobChanged(int32 /*OldClassId*/, int32 /*Job*/, int32 /*ClassId*/, int32 /*SubJob*/)
{
	RefreshAll();
}

// Эталон на КАЖДЫЙ стат-пакет зовёт `UI_FillEquipDetailPanel` — перерисовываем обе числовые страницы.
void UAsdaCharaInfoSubsystem::OnSelfStatsChanged()
{
	if (!Win) { return; }
	RefreshDataPage();
	RefreshElementPage();
}

// Порт `CFactionUI_UpdateInfo@0xB932C0`.
void UAsdaCharaInfoSubsystem::OnSelfFaction(int32 FactionId, int32 Points, int32 Grade)
{
	LastFactionId = FactionId; LastFactionPoints = Points; LastFactionGrade = Grade;
	if (FactionId < 0 || FactionId > 2)
	{
		SetTextFmt(FactionFmt, FString());
		SetText(HonorValue.Get(), FString());
		SetText(GradeValue.Get(), FString());
		if (GradeMark) { GradeMark->SetVisibility(ESlateVisibility::Collapsed); }
		return;
	}
	// Цвета фракций эталона: 0 синий FF0000FF, 1 красный FFFF0000, 2 зелёный FF00FF00.
	static const TCHAR* kColor[3] = { TEXT("FF0000FF"), TEXT("FFFF0000"), TEXT("FF00FF00") };
	// ★ Строка НЕСЁТ ТЕГ — только через `SetTextFmt` (иначе видна сырая разметка, п. 1 владельца).
	SetTextFmt(FactionFmt, FString::Printf(TEXT("<FC=%s>%s</F>"),
		kColor[FactionId], *Loc(CharInfoLoc::kFactionFirst + FactionId)));
	SetText(HonorValue.Get(), FString::Printf(TEXT("%d"), Points));
	SetText(GradeValue.Get(), HonorRankText(GetGameInstance(), Grade));
	// Значок ранга: клетка `Grade-1` атласа `Faction_Grade.tex`, 24×24 (эталон `uiGrid_CellPosFromIndex`).
	if (GradeMark)
	{
		if (Grade > 0)
		{
			// ★ ИСПРАВЛЕНО 2026-08-26: `ApplyFlatJuiIcon` растягивала ВЕСЬ лист званий; эталон
			//   рисует ОДНУ клетку (`uiGrid_CellPosFromIndex@0xCA1C20`: сетка 8 колонок по 24 px,
			//   индекс = `Grade − 1`). Теперь через общую точку `ApplyJuiIconCell`.
			const int32 GradeIdx = Grade - 1;
			AsdaUi::ApplyJuiIconCell(GradeMark.Get(), TEXT("Faction_Grade"),
				24 * (GradeIdx % 8), 24 * (GradeIdx / 8), 24, 24.f);
			GradeMark->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		else
		{
			GradeMark->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

// Порт `uiRowSetTextOrLocalizedNotice@0xB95BB0`: длинное звание → две строки, иначе «Выбрать звание».
void UAsdaCharaInfoSubsystem::OnSelfTitleChanged()
{
	// ★ 2026-08-29: обе строки берём у подсистемы званий (она держит слоты, пришедшие в 6068).
	//   Эталон `uiRowSetTextOrLocalizedNotice@0xB95BB0`: если склейка «приставка окончание» короче
	//   двух символов — показать нотис «Выбрать звание», иначе обе строки как есть.
	// ★ Звание приходит С ТЕГОМ `<FC=%08x>` (пакет 6068, `NetHandler_UpdateCharGuildTitle`) —
	//   пишем через ту же единую точку, чтобы разметка не всплыла.
	FString Front, Second;
	if (const UAsdaTitleSubsystem* Titles = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UAsdaTitleSubsystem>() : nullptr)
	{
		Titles->GetWornTitleTexts(Front, Second);
	}
	if ((Front + Second).Len() < 2)
	{
		SetTextFmt(TitleFrontFmt, Loc(CharInfoLoc::kTitleEmpty));
		SetTextFmt(TitleSecondFmt, FString());
	}
	else
	{
		SetTextFmt(TitleFrontFmt, Front);
		SetTextFmt(TitleSecondFmt, Second);
	}
}

// ═══════════════════════════ КОМАНДЫ ════════════════════════════════════════════════════════════
void UAsdaCharaInfoSubsystem::OnMenuCommand(int32 ButtonIdNum, int32 /*TargetWindowId*/, bool bOpen)
{
	if (ButtonIdNum != kMenuButtonIdNum) { return; }
	// Эталон: 67073 («включили») → uiWindow_OpenIfAllowed, 67074 («выключили») → uiWindowClose.
	if (bOpen) { if (!IsWindowOpen()) { OpenWindow(); } }
	else       { if (IsWindowOpen())  { CloseWindow(); } }
}

void UAsdaCharaInfoSubsystem::OnGameCommand(int32 Cmd)
{
	if (Cmd == kCmdInterfaceChar)  { ToggleTab(0); return; }
	if (Cmd == kCmdInterfaceEmote) { ToggleTab(1); return; }
}
