// Copyright (c) 2026 @sitxovski (Discord)
// AsdaMiniStateSubsystem_Debug.cpp — ★ R3-c 12.11.2026 (разрез AsdaMiniStateSubsystem.cpp, дословный перенос): дампы и дев-команды.
//   Общее для частей — в AsdaMiniStateSubsystem_Private.h; шапка и реверс — в AsdaMiniStateSubsystem.cpp.
#include "UI/AsdaMiniStateSubsystem.h"


// inline-теги <FC=…>…</F> в тексте тултипа 110922   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "AsdaMiniStateSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaMiniStateSubsystem*.cpp

// ══════════════════════════════ ДАМП ═══════════════════════════════════════════════════════════
FString UAsdaMiniStateSubsystem::DumpControls() const
{
	auto Vis = [](const UWidget* W) -> const TCHAR*
	{
		if (!W) { return TEXT("НЕТ"); }
		switch (W->GetVisibility())
		{
		case ESlateVisibility::Visible:              return TEXT("Visible");
		case ESlateVisibility::Collapsed:            return TEXT("Collapsed");
		case ESlateVisibility::Hidden:               return TEXT("Hidden");
		case ESlateVisibility::HitTestInvisible:     return TEXT("HitTestInv");
		case ESlateVisibility::SelfHitTestInvisible: return TEXT("SelfHitTestInv");
		default:                                     return TEXT("?");
		}
	};
	auto Rect = [](const UWidget* W) -> FString
	{
		FVector2D P, S;
		if (!GetSlotRect(W, P, S)) { return TEXT("(слота нет)"); }
		return FString::Printf(TEXT("(%.0f,%.0f %.0fx%.0f)"), P.X, P.Y, S.X, S.Y);
	};
	auto BarLine = [&](const TCHAR* Tag, const FAsdaMiniBarFill& B) -> FString
	{
		return FString::Printf(TEXT("  %s: ES0=%s%s  ES1=%s%s полн.ширина=%.0f\n"),
			Tag, Vis(B.Back), *Rect(B.Back), Vis(B.Fill), *Rect(B.Fill), B.FullW);
	};

	FString S;
	S += FString::Printf(TEXT("=== MINISTATE_WIN (этап 1) === окно=%s контролы=%s\n"),
		MiniWidget ? TEXT("создано") : TEXT("НЕТ"), bControlsReady ? TEXT("готовы") : TEXT("НЕ ГОТОВЫ"));
	S += BarLine(TEXT("HP  (3)  "), HpBar);
	S += BarLine(TEXT("MP  (4)  "), MpBar);
	S += BarLine(TEXT("вес (6)  "), WeightBar);
	S += BarLine(TEXT("вес2(95) "), WeightBar2);
	S += BarLine(TEXT("вес3(96) "), WeightBar3);
	S += BarLine(TEXT("бонус(165, ссылок в срезе нет)"), BonusBar);
	S += FString::Printf(TEXT("  HPBuff(37)=%s%s  MPBuff(89)=%s%s (ширина = pct×%.2f, слои всегда видимы)\n"),
		Vis(HpGhost), *Rect(HpGhost), Vis(MpGhost), *Rect(MpGhost), GhostWidthScale);
	S += FString::Printf(TEXT("  HPTEXT(32)='%s'  MPTEXT(33)='%s'  CharName(5)='%s'\n"),
		HpText ? *HpText->GetText().ToString() : TEXT("—"),
		MpText ? *MpText->GetText().ToString() : TEXT("—"),
		CharNameText ? *CharNameText->GetText().ToString() : TEXT("—"));
	{
		FString D;
		for (int32 i = 0; i < WeiDigitOnes.Num(); ++i)
		{
			if (WeiDigitOnes[i] && WeiDigitOnes[i]->GetVisibility() != ESlateVisibility::Collapsed) { D += FString::Printf(TEXT("Е%d "), i); }
			if (WeiDigitTens.IsValidIndex(i) && WeiDigitTens[i] && WeiDigitTens[i]->GetVisibility() != ESlateVisibility::Collapsed) { D += FString::Printf(TEXT("Д%d "), i); }
			if (WeiDigitHundreds.IsValidIndex(i) && WeiDigitHundreds[i] && WeiDigitHundreds[i]->GetVisibility() != ESlateVisibility::Collapsed) { D += FString::Printf(TEXT("С%d "), i); }
		}
		S += FString::Printf(TEXT("  цифры веса (130..139 + 2 ряда клонов): видимы [%s], Y=%.0f, %%-глиф=%s\n"),
			D.IsEmpty() ? TEXT("нет") : *D, WeiDigitY, Vis(WeiPercentGlyph));
	}
	{
		FString D;
		for (int32 i = 0; i < LvDigitOnes.Num(); ++i)
		{
			if (LvDigitOnes[i] && LvDigitOnes[i]->GetVisibility() != ESlateVisibility::Collapsed) { D += FString::Printf(TEXT("Е%d%s "), i, *Rect(LvDigitOnes[i])); }
			if (LvDigitTens.IsValidIndex(i) && LvDigitTens[i] && LvDigitTens[i]->GetVisibility() != ESlateVisibility::Collapsed) { D += FString::Printf(TEXT("Д%d%s "), i, *Rect(LvDigitTens[i])); }
			if (LvDigitHundreds.IsValidIndex(i) && LvDigitHundreds[i] && LvDigitHundreds[i]->GetVisibility() != ESlateVisibility::Collapsed) { D += FString::Printf(TEXT("С%d%s "), i, *Rect(LvDigitHundreds[i])); }
		}
		S += FString::Printf(TEXT("  цифры уровня (10..19 + 2 ряда клонов): уровень=%d, видимы [%s]; прототип=(%.0f,%.0f) глиф=%.0f LVWIN=%.0f\n"),
			LastLevelApplied, D.IsEmpty() ? TEXT("нет") : *D, LvProtoPos.X, LvProtoPos.Y, LvDigitW, LvWinW);
	}
	{
		static const TCHAR* kCls[4] = { TEXT("Tenderfoot(28)"), TEXT("Warrior(29)"), TEXT("Archer(30)"), TEXT("Wizard(31)") };
		FString D;
		for (int32 i = 0; i < ClassIcons.Num() && i < 4; ++i) { D += FString::Printf(TEXT("%s=%s "), kCls[i], Vis(ClassIcons[i])); }
		S += FString::Printf(TEXT("  иконки класса: %s\n"), *D);
	}
	{
		FString D;
		for (int32 i = 0; i < ElementIcons.Num(); ++i) { D += FString::Printf(TEXT("%d=%s "), i, Vis(ElementIcons[i])); }
		S += FString::Printf(TEXT("  стихии (20..25): %s; родитель ELE_Base=%s ★ в ОРИГИНАЛЕ поддерево не рисуется (Visible=0, IDNum=-1)\n"),
			*D, Vis(ElementBase));
	}
	S += FString::Printf(TEXT("  PvP(5300)=%s  pluscpack(5110)=%s [показ по 6585]  Partyer(9)=%s%s [глава отряда, 5102]\n"),
		Vis(PvpModeIcon), Vis(PlusPackIcon), Vis(PartyerIcon), *Rect(PartyerIcon));
	S += FString::Printf(TEXT("  ClanMarkBase(270)=%s%s  ClanMark(271)=%s%s кисть=%s  кампвар=%d флаг клана=%d\n"),
		Vis(ClanMarkBase), *Rect(ClanMarkBase), Vis(ClanMarkImg), *Rect(ClanMarkImg),
		(ClanMarkImg && ClanMarkImg->GetBrush().GetResourceObject()) ? *ClanMarkImg->GetBrush().GetResourceObject()->GetName() : TEXT("нет"),
		IsCampWarMap() ? 1 : 0, IsSelfClanFlagOn() ? 1 : 0);
	S += FString::Printf(TEXT("  Gear1(7)=%s  AniGear1(8)=%s [стартер анимации в срезе не найден]  зона клика(97)=%s%s\n"),
		Vis(Gear1Group), Vis(AniGear1Group), Vis(ClickZone), *Rect(ClickZone));
	S += FString::Printf(TEXT("  вес: флаг70=%d флаг90=%d мигание=%s; крыло(6585)=%d остаток=%d с (%d дн.) активен=%d; тултип=%s\n"),
		bWeightWarn70Shown ? 1 : 0, bWeightWarn90Shown ? 1 : 0,
		(WeightBlinkNextS >= 0.0) ? TEXT("вкл (250 мс)") : TEXT("выкл"),
		WingId, WingRemainSec, WingRemainSec / 86400, bWingActive ? 1 : 0,
		bTipShown ? TEXT("показан") : TEXT("скрыт"));
	// Тексты: выравнивание и слоты (доказательство п.2 «цифры HP/MP не отцентрованы»).
	auto TxtLine = [&](const TCHAR* Tag, const UTextBlock* T, int32 Mask) -> FString
	{
		if (!T) { return FString::Printf(TEXT("  %s: НЕТ\n"), Tag); }
		const TCHAR* J = (Mask & 0x2) ? TEXT("право") : (Mask & 0x1) ? TEXT("центр") : TEXT("лево");
		const TCHAR* V = (Mask & 0x8) ? TEXT("низ")   : (Mask & 0x4) ? TEXT("центр") : TEXT("верх");
		return FString::Printf(TEXT("  %s: EXS TextAlign=%d (гор.=%s верт.=%s) слот=%s\n"), Tag, Mask, J, V, *Rect(T));
	};
	S += TxtLine(TEXT("HPTEXT(32) "), HpText, 5);
	S += TxtLine(TEXT("MPTEXT(33) "), MpText, 5);
	S += TxtLine(TEXT("CharName(5)"), CharNameText, 5);
	if (const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		S += FString::Printf(TEXT("  данные: userNum=%u level=%u class=%d вес=%d/%d (канал «%s»)\n"),
			Net->SelfUserNum, (uint32)Net->SelfLevel, Net->SelfClassId,
			Net->SelfWeightCur, Net->SelfWeightMax, *Net->SelfWeightSrc);
	}
	return S;
}

// ══════════════════════════════ КОНСОЛЬ (проверка БЕЗ сервера) ═════════════════════════════════
static UAsdaMiniStateSubsystem* MiniOf(UWorld* W)
{
	return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr;
}

#if !UE_BUILD_SHIPPING   // ★ античит А2 (12.10.2026): отладка самотестов/дев-команд — только вне Shipping
// ★★★ ВЫВОД ДИАГНОСТИКИ НА ЭКРАН. Юзер запускает команды В ИГРЕ и файл лога не читает, поэтому
//   каждая строка уходит и в лог, и в системный журнал чата через единый маршрутизатор сообщений
//   (категория Tip = «подсказки, служебное»). Многострочный текст режем по '\n': одна длинная
//   запись и в консоли, и в журнале обрезается.
// ★ U13 (В1 дедупа №2): тело — общая точка `AsdaNotice::ConsoleEcho` (прежняя копия была дословной, менялась только категория лога).
static void MiniEcho(UWorld* W, const FString& Text)
{
	AsdaNotice::ConsoleEcho(W, Text, LogAsdaMini);
}
#endif // !UE_BUILD_SHIPPING

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaMiniDump(
	TEXT("asda.Mini.Dump"),
	TEXT("Мини-статус: дамп КАЖДОГО контрола этапа 1 (видимость/геометрия/значения)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaMiniStateSubsystem* M = MiniOf(W)) { MiniEcho(W, M->DumpControls()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaMiniBuffDump(
	TEXT("asda.Mini.BuffDump"),
	TEXT("Мини-статус: дамп 28 слотов иконок эффектов (id/typeFlag/стек/таймер) + ивент-иконок"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaMiniStateSubsystem* M = MiniOf(W)) { MiniEcho(W, M->DumpBuffSlots()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaMiniBuffRefresh(
	TEXT("asda.Mini.BuffRefresh"),
	TEXT("Мини-статус: перерисовать иконки эффектов и ивент-иконки из живой модели + дамп"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaMiniStateSubsystem* M = MiniOf(W);
		if (!M) { return; }
		M->RefreshBuffIcons(true);
		M->RefreshEventIcons();
		MiniEcho(W, M->DumpBuffSlots());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaMiniRefresh(
	TEXT("asda.Mini.Refresh"),
	TEXT("Мини-статус: пересчитать все показатели из живых данных (аналог hudRefreshFromCharacterStats)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaMiniStateSubsystem* M = MiniOf(W)) { M->RefreshAll(); MiniEcho(W, M->DumpControls()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaMiniHp(
	TEXT("asda.Mini.TestHp"),
	TEXT("asda.Mini.TestHp <cur> <max> — подставить HP в мини-статус (эталон UI_UpdateHPBar)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaMiniStateSubsystem* M = MiniOf(W);
		if (!M || A.Num() < 2) { MiniEcho(W, TEXT("использование: asda.Mini.TestHp <cur> <max>")); return; }
		M->ApplyHp(FCString::Atoi(*A[0]), FCString::Atoi(*A[1]));
		MiniEcho(W, M->DumpControls());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaMiniMp(
	TEXT("asda.Mini.TestMp"),
	TEXT("asda.Mini.TestMp <cur> <max> — подставить MP в мини-статус"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaMiniStateSubsystem* M = MiniOf(W);
		if (!M || A.Num() < 2) { MiniEcho(W, TEXT("использование: asda.Mini.TestMp <cur> <max>")); return; }
		M->ApplyMp(FCString::Atoi(*A[0]), FCString::Atoi(*A[1]));
		MiniEcho(W, M->DumpControls());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaMiniGhost(
	TEXT("asda.Mini.TestGhost"),
	TEXT("asda.Mini.TestGhost <hpSecond> <hpMax> <mpSecond> <mpMax> — «отстающие» слои HPBuff/MPBuff"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaMiniStateSubsystem* M = MiniOf(W);
		if (!M || A.Num() < 4) { MiniEcho(W, TEXT("использование: asda.Mini.TestGhost <hp2> <hpMax> <mp2> <mpMax>")); return; }
		M->ApplyVitalGhosts(FCString::Atoi(*A[0]), FCString::Atoi(*A[1]), FCString::Atoi(*A[2]), FCString::Atoi(*A[3]));
		MiniEcho(W, M->DumpControls());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaMiniWeight(
	TEXT("asda.Mini.TestWeight"),
	TEXT("asda.Mini.TestWeight <cur> <max> — вес: 3 полосы, пороги 70/90, мигание, цифры. "
	     "★ Базовый максимум в оригинале приходит ПОЛЕМ БД в блоке CharInfo (см. AsdaNetSubsystem.h)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: подменяет вес живой сессии (гейты клиента, например копание ≥ 90 %).
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Mini.TestWeight"))) { return; }
		UAsdaMiniStateSubsystem* M = MiniOf(W);
		if (!M || A.Num() < 2) { MiniEcho(W, TEXT("использование: asda.Mini.TestWeight <cur> <max>")); return; }
		if (UAsdaNetSubsystem* Net = W->GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->SetSelfWeight(FCString::Atoi(*A[0]), FCString::Atoi(*A[1]), TEXT("консоль"));
		}
		MiniEcho(W, M->DumpControls());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaMiniLevel(
	TEXT("asda.Mini.TestLevel"),
	TEXT("asda.Mini.TestLevel <уровень> — цифры уровня (1/2/3 разряда, раскладка HUD_UpdateLevelDigits)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaMiniStateSubsystem* M = MiniOf(W);
		if (!M || A.Num() < 1) { MiniEcho(W, TEXT("использование: asda.Mini.TestLevel <уровень>")); return; }
		M->ApplyLevel(FCString::Atoi(*A[0]));
		MiniEcho(W, M->DumpControls());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaMiniClass(
	TEXT("asda.Mini.TestClass"),
	TEXT("asda.Mini.TestClass <classId> [имя] — иконка класса по байту +0x438 "
	     "(0 Новичок, 1..7 воин, 12..18 лучник, 23..29 маг; 8..11/19..22/>29 — иконки НЕТ, так в оригинале)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaMiniStateSubsystem* M = MiniOf(W);
		if (!M || A.Num() < 1) { MiniEcho(W, TEXT("использование: asda.Mini.TestClass <classId> [имя]")); return; }
		FString Nm; for (int32 i = 1; i < A.Num(); ++i) { if (i > 1) Nm += TEXT(" "); Nm += A[i]; }
		M->ApplyNameClassElements(Nm, FCString::Atoi(*A[0]), TArray<int32>());
		MiniEcho(W, M->DumpControls());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaMiniElements(
	TEXT("asda.Mini.TestElements"),
	TEXT("asda.Mini.TestElements <огонь> <вода> <земля> <ветер> <свет> <тьма> — выбор иконки стихии. "
	     "★ В ОРИГИНАЛЕ поддерево ELE_Base скрыто и НЕ рисуется — команда показывает только выбор в дампе"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaMiniStateSubsystem* M = MiniOf(W);
		if (!M || A.Num() < 6) { MiniEcho(W, TEXT("использование: asda.Mini.TestElements f w e a l d")); return; }
		TArray<int32> E; for (int32 i = 0; i < 6; ++i) { E.Add(FCString::Atoi(*A[i])); }
		M->ApplyNameClassElements(FString(), -1, E);
		MiniEcho(W, M->DumpControls());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaMiniPvp(
	TEXT("asda.Mini.PvP"),
	TEXT("asda.Mini.PvP <0|1> — иконка режима PvP (5300); в игре ею управляет опкод 4206"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaMiniStateSubsystem* M = MiniOf(W);
		if (!M) { return; }
		M->SetPvpModeIcon(A.Num() > 0 ? (FCString::Atoi(*A[0]) != 0) : true);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaMiniPlusPack(
	TEXT("asda.Mini.PlusPack"),
	TEXT("asda.Mini.PlusPack <0|1> [wingId] [остаток_сек] — эмуляция пакета 6585: показать/скрыть иконку "
	     "Plus Package (5110) и подставить вид пакета. Наведи курсор на иконку — появится описание"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaMiniStateSubsystem* M = MiniOf(W);
		if (!M) { return; }
		const bool bOn   = (A.Num() > 0) ? (FCString::Atoi(*A[0]) != 0) : true;
		const int32 Wing = (A.Num() > 1) ? FCString::Atoi(*A[1]) : 2;
		const int32 Sec  = (A.Num() > 2) ? FCString::Atoi(*A[2]) : 28342920;   // 328 дн., как у тест-чара
		M->SimulateWingState(Wing, Sec, bOn);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// Полный список ВСЕХ видов Plus Package с их строками — доказательная команда «для каждого вида есть окно».
static FAutoConsoleCommandWithWorldAndArgs GAsdaMiniPlusPackList(
	TEXT("asda.Mini.PlusPackList"),
	TEXT("Все виды Plus Package из DT_PlusPackage: id, loc заголовка/тела и сами строки"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaMiniStateSubsystem* M = MiniOf(W)) { MiniEcho(W, M->DumpPlusPackages()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ═══════════ ДИНАМИКА ПОЛОС БЕЗ СЕРВЕРА: правим ЕДИНЫЙ источник, а не виджеты напрямую ═══════════
//   Команды бьют ровно в те же функции, что и сетевые обработчики (SetVitalsHp/SetVitalsMp/
//   ApplyVitalsDamage/SetSelfWeight), поэтому проверяют ВЕСЬ тракт: сеть → FAsdaVitals →
//   OnVitalsChanged → мини-статус → клип заливки. Плашка над головой обновится тем же сигналом.
static FAutoConsoleCommandWithWorldAndArgs GAsdaMiniHpDelta(
	TEXT("asda.Mini.HpDelta"),
	TEXT("asda.Mini.HpDelta <±N> — изменить ТЕКУЩИЙ HP своего персонажа через единый источник "
	     "(отрицательное = урон через ApplyVitalsDamage, как в бою 4015; положительное = снапшот, как 4058)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!Net || A.Num() < 1) { MiniEcho(W, TEXT("использование: asda.Mini.HpDelta <±N>")); return; }
		const int32 D = FCString::Atoi(*A[0]);
		const UAsdaNetSubsystem::FAsdaVitals* V = Net->GetVitals((int32)Net->SelfUserNum);
		if (!V || !V->HasHp()) { UE_LOG(LogAsdaMini, Warning, TEXT("[тест] HP ещё не приходил (нет снапшота) — сначала войди в мир")); return; }
		if (D < 0) { Net->ApplyVitalsDamage((int32)Net->SelfUserNum, -D, TEXT("консоль HpDelta")); }
		else       { Net->SetVitalsHp((int32)Net->SelfUserNum, FMath::Min(V->HpCur + D, V->HpMax), -1, TEXT("консоль HpDelta"), false); }
		MiniEcho(W, Net->DumpVitals());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaMiniMpDelta(
	TEXT("asda.Mini.MpDelta"),
	TEXT("asda.Mini.MpDelta <±N> — изменить ТЕКУЩИЙ MP своего персонажа через единый источник "
	     "(эквивалент серверного 4059 «MP после скилла»)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!Net || A.Num() < 1) { MiniEcho(W, TEXT("использование: asda.Mini.MpDelta <±N>")); return; }
		const UAsdaNetSubsystem::FAsdaVitals* V = Net->GetVitals((int32)Net->SelfUserNum);
		if (!V || V->MpCur < 0) { UE_LOG(LogAsdaMini, Warning, TEXT("[тест] MP ещё не приходил — сначала войди в мир")); return; }
		const int32 New = FMath::Clamp(V->MpCur + FCString::Atoi(*A[0]), 0, FMath::Max(V->MpMax, 1));
		Net->SetVitalsMp((int32)Net->SelfUserNum, New, -1, TEXT("консоль MpDelta"), false);
		MiniEcho(W, Net->DumpVitals());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaMiniWeightDelta(
	TEXT("asda.Mini.WeightDelta"),
	TEXT("asda.Mini.WeightDelta <±N> — изменить ТЕКУЩИЙ вес (эквивалент 5013 «подобрал предмет» / "
	     "выброса из инвентаря). Порог 70 %% и 90 %% с миганием проверяются прямо тут"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!Net || A.Num() < 1) { MiniEcho(W, TEXT("использование: asda.Mini.WeightDelta <±N>")); return; }
		if (Net->SelfWeightMax <= 0) { UE_LOG(LogAsdaMini, Warning, TEXT("[тест] максимум веса неизвестен — сначала выбери персонажа (пакет 1017)")); return; }
		const int32 New = FMath::Clamp(FMath::Max(Net->SelfWeightCur, 0) + FCString::Atoi(*A[0]), 0, Net->SelfWeightMax);
		Net->SetSelfWeight(New, -1, TEXT("консоль WeightDelta"));
	}));
#endif // ASDA_DEV_CONSOLE
