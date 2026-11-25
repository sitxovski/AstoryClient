// Copyright (c) 2026 @sitxovski (Discord)
// AsdaChatWindowSubsystem_Input.cpp — ★ 23.10.2026 часть UAsdaChatWindowSubsystem, вынесенная из AsdaChatWindowSubsystem.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaChatWindowSubsystem.plan.json).
//   Диагностика окна, этап 2 — ввод (поле 2609, приглашение, разбор /команд, отправка, гейт канала, антифлуд), кулдаун-полоса 2618/2619.
//   #include — те же, что у AsdaChatWindowSubsystem.cpp, в том же порядке; общее — AsdaChatWindowSubsystemInternal.h.
#include "UI/AsdaChatWindowSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "AsdaSelf.h"   // ★ В2-A дедупа №2: общая точка «это я?» (класс/уровень/charId/userNum)
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "Login/AsdaLoginData.h"   // [ПОРТ] 08.10.2026: WireTextByteLen/TruncateToWireBytes (лимит ввода в байтах провода)
#include "AsdaDataTableCache.h"   // ★ Аудит №4 (B2): AsdaDt::Cached — одна ленивая загрузка DataTable на порт
#include "UI/AsdaUiHelpers.h"   // ★ D1/D2/D5: единые UI-хелперы (дедуп 2026-08-08)
#include "AsdaNetSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaCharaInfoSubsystem.h"   // ★ эмоции слэш-командой (порт ChatInput_ParseCommand)
#include "UI/AsdaJuiSlot.h"
#include "Text/AsdaTextSubsystem.h"
#include "Resources/AsdaTextureSubsystem.h"   // ★ Аудит №3 (A8/B8): атласы JUI — только через менеджер
// UAsdaRichText::ApplyFormatted — теги <FS>/<FD>/<FC=..>   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "World/AsdaWorldSubsystem.h"
#include "UI/AsdaCampWarBattleSubsystem.h"   // ★ 31.10.2026: чат армии 6718 (`SendArmyChat6718`)
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/TextWidgetTypes.h"   // UTextLayoutWidget — общий предок UTextBlock и URichTextBlock
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/ComboBoxString.h"
#include "Components/RichTextBlock.h"
#include "Components/EditableText.h"
#include "Components/CheckBox.h"   // 2607 CHAT_NEW_Hbtn — CheckButton скина Chat_new_ck3
#include "GameFramework/PlayerController.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"   // замер высоты строки для точной центровки подписи 2608
#include "Styling/CoreStyle.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "UI/AsdaNotice.h"                 // ★ Дедуп №2, В1-B (U10c): `AsdaNotice::Loc` вместо прямого `FAsdaLoginData::Loc`
#include "UI/AsdaPartySubsystem.h"         // ★ 12.10.2026 [ОРИГИНАЛ]: чат отряда — `Party_IsInParty` и `g_nPartyInviteTargetIdx` (= PartyIndex)
#include "UI/AsdaSoulmateSubsystem.h"      // ★ 12.10.2026 [ОРИГИНАЛ]: канал «Друг» — слот/пара/имя партнёра (`g_pSoulmateWnd`)
#include "AsdaChatWindowSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaChatWindowSubsystem*.cpp (LogAsdaChatWin, чистые хелперы)


// ★ 12.10.2026 [ОРИГИНАЛ] ЧАТ ОТРЯДА ОЖИВЛЁН (отчёт `Reports/owner_1012/party_chat.md`). Ключ отката: 1 — прежняя
//   заглушка порта «отряд не портирован» (режим 5/41 ВСЕГДА отказывает loc 6928, 5105 не уходит никогда).
static ASDA_DEV_CVAR(int32, CVarPartyChatLegacy,
	TEXT("asda.Chat.PartyChatLegacy"), 0,
	TEXT("Чат отряда (режим 5/41): 0 — как оригинал (в отряде → 5105 с индексом отряда, вне отряда → loc 6928); ")
	TEXT("1 — прежняя заглушка порта (всегда отказ loc 6928)."),
	ECVF_Default);

// ★ 12.10.2026 [ОРИГИНАЛ] ПОРЯДОК ПРОВЕРОК КАНАЛОВ (отчёт `Reports/owner_1012/party_chat.md`, раздел «Порядок проверок каналов»).
//   0 — как оригинал `chatInput_OnCommandKey` case 2609 (chat.c:3851-3975): гейт канала (и кулдаун 0/1/2) ДО антифлуда; клан и
//   армия антифлуд не проходят вовсе; «Друг» — настоящий гейт соулмейта и 5088 вида 1 на имя партнёра; шёпот без адресата — loc
//   6943 строкой ленты. 1 — прежний порядок порта (антифлуд первым для всех каналов, «Друг» — заглушка 6929, шёпот — 6942 в журнал).
//   Отряд (режим 5/41) — под своим ключом `asda.Chat.PartyChatLegacy`.
static ASDA_DEV_CVAR(int32, CVarChannelGateLegacy,
	TEXT("asda.Chat.ChannelGateLegacy"), 0,
	TEXT("Порядок проверок каналов чата: 0 — как оригинал (гейт канала до антифлуда, клан/армия без антифлуда, гейт «Друга», ")
	TEXT("шёпот без адресата → 6943); 1 — прежний порядок порта (антифлуд первым, «Друг» — заглушка 6929, шёпот → 6942)."),
	ECVF_Default);

// ══════════════════════════════ ДИАГНОСТИКА ═══════════════════════════════════════════════════

bool UAsdaChatWindowSubsystem::DebugLinesScroll(float& OutOffset, float& OutEnd, FVector2D& OutCenter) const
{
	FVector2D Mn, Mx;   // прямоугольник ленты
	if (!LinesBox || !AsdaUi::IsShown(LinesBox) || !AsdaUi::WidgetAbsRect(LinesBox, Mn, Mx) || (Mx - Mn).IsNearlyZero()) { return false; }
	OutOffset = LinesBox->GetScrollOffset();
	OutEnd = LinesBox->GetScrollOffsetOfEnd();
	OutCenter = (Mn + Mx) * 0.5f;
	return true;
}

FString UAsdaChatWindowSubsystem::DebugState() const
{
	FString S = FString::Printf(
		TEXT("[чат-окно CHAT_NEW 2500] окно=%s лента=%s строк=%d/%d префикс-канала(осада)=%s высота=%.0f (y %.0f…%.0f)"),
		ChatWidget ? TEXT("есть") : TEXT("НЕТ"),
		LinesBox ? TEXT("есть") : TEXT("НЕТ"),
		LogHistory.Num(), kMaxLines,
		bChannelPrefixMode ? TEXT("ВКЛ") : TEXT("выкл"),
		BaseHeight(), BaseTop(), BaseTop() + BaseHeight());
	// ★ Ширина, вкладки и память кнопки 2605 — всё, что добавлено 2026-08-06 (Д21).
	S += FString::Printf(
		TEXT("\n  ширина=%.0f (мин %.0f, макс %.0f, надбавка DW=%.0f) | вкладок влезает %d из %d, ")
		TEXT("первая видимая %d, стрелки %s | кнопка 2605 помнит %.0fx%.0f (%s)"),
		BaseWidth(), kWinMinW, kWinMaxW, FMath::Max(0.f, BaseWidth() - kWinW),
		VisibleTabCount(), ChatTabs.Num(), TabScroll,
		(VisibleTabCount() < ChatTabs.Num()) ? TEXT("показаны") : TEXT("СКРЫТЫ"),
		SavedW, SavedH, bHasSavedSize ? TEXT("есть") : TEXT("нет"));
	// Сколько строк по каждому слоту канала — видно, правильно ли раскладываются категории.
	int32 PerSlot[10] = { 0 };
	int32 Service = 0;
	int32 Announce = 0;      // ★ строки-дубли оповещений (bNoNick) — среди них есть и слот 4
	for (const FChatLogLine& L : LogHistory)
	{
		if (L.Slot >= 0 && L.Slot <= 9) { ++PerSlot[L.Slot]; } else { ++Service; }
		if (L.bNoNick) { ++Announce; }
	}
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	for (int32 i = 0; i < 10; ++i)
	{
		if (PerSlot[i] == 0) { continue; }
		const FColor C = ChanColor(i);   // ДЕЙСТВУЮЩИЙ цвет (палитра 2700 могла его сменить)
		const FString Nm = UiChannelText(kChannels[i].LocId);   // ★ с переименованием «Все каналы»
		S += FString::Printf(TEXT("\n  слот %d (loc %d «%s», префикс «[%s]») цвет FF%02X%02X%02X — строк %d"),
			i, kChannels[i].LocId, *Nm, *SlotShortName(i), C.R, C.G, C.B, PerSlot[i]);
	}
	if (Service > 0) { S += FString::Printf(TEXT("\n  служебных (без слота) — строк %d"), Service); }
	// ★ Дубли оповещений (приёмник ChatRibbon): ник в них не разбирается (bNoNick), цвет базовый,
	//   inline-теги цвета грейда сохранены. Слот 4 = эталонный слот анонса редкой добычи.
	if (Announce > 0)
	{
		S += FString::Printf(TEXT("\n  дублей оповещений (анонсы/нотисы, без разбора ника) — строк %d"), Announce);
	}
	return S;
}

// ★★★ ГРАБЛЯ «ФИОЛЕТОВЫЙ ТЕКСТ» — ТРЕТИЙ РАЗ (окно смерти, справка «?», теперь подпись 2619).
//   ПРИЧИНА. У JUI-контрола ДВА разных поля цвета:
//     CaptionInfo = "0 0 ff400040 5"   ← служебная «подпись контрола» редактора JUI;
//     EXS … TextColor = "FFFFFFFF" …   ← НАСТОЯЩИЙ цвет текста, который рисует игра.
//   Импортёр берёт CaptionInfo, и у ВСЕХ текстовых контролов (скин NoImage) там лежит один и тот
//   же `ff400040` — тёмно-фиолетовый. Поэтому «фиолетовый текст» появляется не в одном окне,
//   а везде, где текст пришёл из импорта и цвет не переопределили руками.
//   ЛЕЧИМ ОДНИМ ПРОХОДОМ ПО ВСЕМУ ОКНУ, а не по одной подписи.
//
//   ПОЛНЫЙ СКАН ОКНА 2500 (gamemain_jui.ini, строки 92901…93946): 46 контролов, из них
//   15 имеют `EXS TextColor`, и у ВСЕХ 15 он НЕ совпадает с CaptionInfo:
//
//     IDNum | ID                        | CaptionInfo | EXS TextColor
//     ------+---------------------------+-------------+---------------
//      2534 | CHAT_NEW_TextMove         | ff400040    | FFFFFFFF
//      2520 | CHAT_NEW_C-List           | ff400040    | FFFFFFFF
//      2581 | (справка, строка)         | ff400040    | FFC0C0C0
//      2583 | (справка, строка)         | ff400040    | FFC0C0C0
//      2582 | (справка, заголовок)      | ff400040    | FFFF8040
//      2586 | (справка, строка)         | ff400040    | FFFFFF80
//      2584 | (справка, строка)         | ff400040    | FF80FF00
//      2585 | (справка, строка)         | ff400040    | FF80FF00
//      2588 | (справка, строка)         | ff400040    | FFFFFFFF
//      ---- | SizeWin                   | ff400040    | FFFFFFFF
//      2535 | CHAT_NEW_Bar_TEXT         | ff400040    | FFFFFFFF
//      2533 | CHAT_NEW_Edit             | ffe9d6bc    | FFC6C6C6
//    ★ 2619 | CHAT_NEW_Bar_TEXT2        | ff400040    | FFFFFFFF   ← подпись кулдауна, БЫЛА ФИОЛЕТОВОЙ
//      2609 | CHAT_NEW_Edit2            | ffffffff    | FFC6C6C6
//      ---- | CHAT_NEW_SERV_Resize_Cur  | ff400040    | FFFFFFFF
//
//   Контролы 2581…2588 (справка) — узлы ассета `CHAT_NEW_Que_btn_tooltip_TXT00…06` (у них в JUI нет ID;
//   узлы создали шаги пост-импорта), текст и цвет из этой же таблицы им ставит BuildQueTipTexts
//   (дополнение 3.4, с 15.09 — в сами узлы, без параллельных виджетов). Здесь красим то,
//   что реально пришло из импорта.
void UAsdaChatWindowSubsystem::ApplyEtalonTextColors()
{
	if (!ChatWidget) { return; }
	struct FRow { const TCHAR* Node; const TCHAR* Hex; const TCHAR* Id; };
	static const FRow kRows[] =
	{
		{ TEXT("CHAT_NEW_TextMove"),  TEXT("FFFFFFFF"), TEXT("2534") },
		{ TEXT("CHAT_NEW_C_List"),    TEXT("FFFFFFFF"), TEXT("2520") },
		{ TEXT("SizeWin"),            TEXT("FFFFFFFF"), TEXT("—")    },
		{ TEXT("CHAT_NEW_Bar_TEXT"),  TEXT("FFFFFFFF"), TEXT("2535") },
		{ TEXT("CHAT_NEW_Bar_TEXT2"), TEXT("FFFFFFFF"), TEXT("2619") },
		{ TEXT("CHAT_NEW_Edit"),      TEXT("FFC6C6C6"), TEXT("2533") },
		{ TEXT("CHAT_NEW_Edit2"),     TEXT("FFC6C6C6"), TEXT("2609") },
	};
	int32 Fixed = 0, Missing = 0;
	for (const FRow& R : kRows)
	{
		UWidget* W = ChatWidget->GetWidgetFromName(FName(R.Node));
		if (!W) { ++Missing; continue; }
		const FLinearColor C = AsdaSrgbHexToLinear(R.Hex);
		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			const FLinearColor Was = Tb->GetColorAndOpacity().GetSpecifiedColor();
			Tb->SetColorAndOpacity(FSlateColor(C));
			UE_LOG(LogAsdaChatWin, Log,
				TEXT("[чат-цвета] %s (%s): было (%.3f,%.3f,%.3f,%.3f) → EXS TextColor %s = (%.3f,%.3f,%.3f,%.3f)"),
				R.Node, R.Id, Was.R, Was.G, Was.B, Was.A, R.Hex, C.R, C.G, C.B, C.A);
			++Fixed;
		}
		else if (UEditableText* Ed = Cast<UEditableText>(W))
		{
			// У UEditableText цвет живёт в стиле — отдельного сеттера нет.
			FEditableTextStyle St = Ed->WidgetStyle;
			const FLinearColor Was = St.ColorAndOpacity.GetSpecifiedColor();
			St.ColorAndOpacity = FSlateColor(C);
			Ed->SetWidgetStyle(St);
			UE_LOG(LogAsdaChatWin, Log,
				TEXT("[чат-цвета] %s (%s): было (%.3f,%.3f,%.3f,%.3f) → EXS TextColor %s = (%.3f,%.3f,%.3f,%.3f)"),
				R.Node, R.Id, Was.R, Was.G, Was.B, Was.A, R.Hex, C.R, C.G, C.B, C.A);
			++Fixed;
		}
	}
	UE_LOG(LogAsdaChatWin, Log,
		TEXT("[чат-цвета] проход по окну 2500: покрашено %d узлов из таблицы EXS TextColor, не найдено %d"),
		Fixed, Missing);
}

// ★ РАНТАЙМ-ДОКАЗАТЕЛЬСТВО по трём живым дефектам 2026-08-05 (asda.ChatWin.DumpCombo):
//   порядок отрисовки списка каналов, состав/индексация пунктов, состояние кулдаун-полосы.
FString UAsdaChatWindowSubsystem::DebugCombo() const
{
	auto Geo = [](const UWidget* W) -> FString
	{
		if (!W) { return TEXT("НЕТ УЗЛА"); }
		const FGeometry& G = W->GetCachedGeometry();
		const FVector2D Sz = G.GetLocalSize() * G.Scale;
		const FVector2D P = G.GetAbsolutePosition();
		return FString::Printf(TEXT("(%.0f,%.0f) %.0fx%.0f vis=%d"), P.X, P.Y, Sz.X, Sz.Y, (int32)W->GetVisibility());
	};

	FString S = TEXT("=========== СПИСОК КАНАЛОВ 2608 + КУЛДАУН-ПОЛОСА 2618 ===========");
	S += FString::Printf(TEXT("\n  список: %s, пунктов %d, выбран %d, режим ввода %d"),
		bComboOpen ? TEXT("РАСКРЫТ") : TEXT("свёрнут"), ComboRows.Num(), ComboIndex, InputState);
	FVector2D Mn, Mx;
	if (GetComboListScreenRect(Mn, Mx))
	{
		S += FString::Printf(TEXT("\n  rect списка (экранный): (%.0f,%.0f)-(%.0f,%.0f)"), Mn.X, Mn.Y, Mx.X, Mx.Y);
	}
	else { S += TEXT("\n  rect списка: НЕТ (список свёрнут либо геометрия ещё не посчитана)"); }
	S += FString::Printf(TEXT("\n  панель списка: %s"), *Geo(ComboList));
	for (int32 i = 0; i < ComboRows.Num(); ++i)
	{
		const int32 Slot = ComboSlots.IsValidIndex(i) ? ComboSlots[i] : -1;
		const int32 Loc  = ComboLoc.IsValidIndex(i)   ? ComboLoc[i]   : -1;
		S += FString::Printf(TEXT("\n    пункт %d → слот %d (loc %d) %s%s"),
			i, Slot, Loc, *Geo(ComboRows[i]), (i == ComboIndex) ? TEXT("  ← ВЫБРАН") : TEXT(""));
	}

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			S += TEXT("\n") + WM->DebugTopmost();
			S += TEXT("\n") + WM->DebugUiZones();
		}
	}

	// ★★★ ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА: три независимых таймера (в игре один общий stateFlags4).
	S += TEXT("\n  КУЛДАУН ★ отступление: свой таймер на каждый широкий канал (в игре — один общий)");
	for (int32 i = 0; i < kWideChannelCount; ++i)
	{
		S += FString::Printf(TEXT("\n    канал %d «%s»: осталось %.1f из %.1f с, причина «%s»%s"),
			i, *WideChannelName(i), ChannelCooldownLeft[i], ChannelCooldownTotal[i],
			*ChannelCooldownReason[i],
			(i == InputState) ? TEXT("  ← ТЕКУЩИЙ (его и показывает полоса)") : TEXT(""));
	}
	S += FString::Printf(TEXT("\n    общий (антифлуд/мат): осталось %.1f из %.1f с, причина «%s»"),
		CooldownLeft, CooldownTotal, *CooldownReason);
	{
		const bool bWideNow = (InputState >= 0 && InputState < kWideChannelCount);
		const bool bVisible = (CooldownLeft > 0.f) || (bWideNow && ChannelCooldownLeft[InputState] > 0.f);
		S += FString::Printf(TEXT("\n    полоса видна=%s"), bVisible ? TEXT("ДА") : TEXT("нет"));
	}
	if (ChatWidget)
	{
		S += FString::Printf(TEXT("\n    ES0 подложка 2618: %s"), *Geo(ChatWidget->GetWidgetFromName(TEXT("CHAT_NEW_Bar2_32_0"))));
		S += FString::Printf(TEXT("\n    ES1 заливка  2618: %s"), *Geo(ChatWidget->GetWidgetFromName(TEXT("CHAT_NEW_Bar2_32_1"))));
		S += FString::Printf(TEXT("\n    подпись      2619: %s"), *Geo(FloodText));
		S += FString::Printf(TEXT("\n    поле ввода   2609: %s"), *Geo(InputEdit));
	}
	return S;
}

void UAsdaChatWindowSubsystem::DumpWidgets() const
{
	UE_LOG(LogAsdaChatWin, Log, TEXT("================ ДАМП ОКНА CHAT_NEW (2500) ================"));
	if (!ChatWidget || !ChatWidget->WidgetTree)
	{
		UE_LOG(LogAsdaChatWin, Log, TEXT("  окно не построено"));
		return;
	}
	UE_LOG(LogAsdaChatWin, Log,
		TEXT("  ОКНО: L=%.1f T=%.1f H=%.1f, низ %.1f (%s). Перетаскивания НЕТ — только растягивание вверх [%.0f…%.0f] и кнопка 2605"),
		WinL, WinT, WinH, WinT + WinH,
		bLayoutToggled ? (bExpanded ? TEXT("развёрнуто") : TEXT("свёрнуто")) : TEXT("исходная высота JUI"),
		kHCollapsed, kHExpanded);
	UE_LOG(LogAsdaChatWin, Log,
		TEXT("  колонки: имя | класс | наш слот | ЭТАЛОН rect (базовые px) | Δ | видим сейчас | состояние"));

	// Эталонная таблица для ТЕКУЩЕГО положения окна — с ней и сверяем каждый узел.
	TArray<FChatLayoutEntry> Table;
	BuildLayoutTable(WinL, WinT, WinH, WinW, QueTipOffset, Table);

	int32 NumMismatch = 0, NumCollapsed = 0;   // расхождения геометрии; свёрнутые сейчас узлы
	TArray<UWidget*> All;
	ChatWidget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();

		FString Slot = TEXT("(слот не CanvasPanelSlot)");
		FVector2D OurPos(0.f, 0.f), OurSize(0.f, 0.f);
		bool bHaveSlot = false;
		if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(W))
		{
			const FAnchors A = CS->GetAnchors();
			const FMargin O = CS->GetOffsets();
			const bool bSpan = !FMath::IsNearlyEqual(A.Minimum.X, A.Maximum.X, 0.0001f)
			                || !FMath::IsNearlyEqual(A.Minimum.Y, A.Maximum.Y, 0.0001f);
			Slot = FString::Printf(TEXT("анкор [%.4f..%.4f]x[%.4f..%.4f] %s off(L%.1f T%.1f R%.1f B%.1f) z=%d"),
				A.Minimum.X, A.Maximum.X, A.Minimum.Y, A.Maximum.Y,
				bSpan ? TEXT("SPAN") : TEXT("POINT"), O.Left, O.Top, O.Right, O.Bottom, CS->GetZOrder());
			// Наш прямоугольник в базовых px: POINT-анкор + оффсет (fx·1024, fy·768 обратно).
			OurPos  = FVector2D(O.Left + A.Minimum.X * AsdaJui::kBaseW, O.Top + A.Minimum.Y * AsdaJui::kBaseH);
			OurSize = FVector2D(O.Right, O.Bottom);
			bHaveSlot = !bSpan;
		}

		// ЭТАЛОННЫЙ прямоугольник (если узел есть в таблице раскладки).
		FString EtalonStr = TEXT("эталон: (нет в таблице — декорация/9-slice, позиция от группы)");
		FString DeltaStr  = TEXT("Δ —");
		const FChatLayoutEntry* Ent = Table.FindByPredicate(
			[&N](const FChatLayoutEntry& E) { return N.Equals(E.Name); });
		if (Ent)
		{
			EtalonStr = FString::Printf(TEXT("ЭТАЛОН (%.1f,%.1f,%.1fx%.1f)%s"),
				Ent->X, Ent->Y, Ent->W, Ent->H, Ent->bLocal ? TEXT(" [локально в группе]") : TEXT(""));
			if (bHaveSlot)
			{
				const float dX = OurPos.X - Ent->X, dY = OurPos.Y - Ent->Y;
				const float dW = OurSize.X - Ent->W, dH = OurSize.Y - Ent->H;
				const bool bOk = FMath::IsNearlyZero(dX, 0.6f) && FMath::IsNearlyZero(dY, 0.6f)
				              && FMath::IsNearlyZero(dW, 0.6f) && FMath::IsNearlyZero(dH, 0.6f);
				if (!bOk) { ++NumMismatch; }
				DeltaStr = FString::Printf(TEXT("Δ(%+.1f,%+.1f,%+.1f,%+.1f)%s"),
					dX, dY, dW, dH, bOk ? TEXT(" СОВПАДАЕТ") : TEXT("  ★ РАСХОЖДЕНИЕ"));
			}
		}

		// ★ Перенесено в ассет (15.09, разрешение владельца): таблицы ChatNodeState больше нет — исходная
		//   видимость узла берётся из WBP (причины по эталону — Tools/asda_ui_chat_markup_visibility.py).
		const bool bNowVisible = AsdaUi::IsShown(W);   // ★ волна В6 ремесла (МЕЛК-8): общая точка видимости узла
		if (W->GetVisibility() == ESlateVisibility::Collapsed) { ++NumCollapsed; }
		const FString StateStr = bNowVisible
			? FString(TEXT("показан (исходно — ассет)"))
			: FString(TEXT("свёрнут/скрыт (исходно — ассет, по событию — код)"));

		const FGeometry& G = W->GetCachedGeometry();
		const FVector2D P = G.GetAbsolutePosition();
		const FVector2D Sz = G.GetLocalSize() * G.Scale;
		UE_LOG(LogAsdaChatWin, Log,
			TEXT("  %-34s %-18s %s | %s | %s | видим сейчас=%s | %s | экран (%.0f,%.0f) разм (%.0f,%.0f)"),
			*N, *W->GetClass()->GetName(), *Slot, *EtalonStr, *DeltaStr,
			bNowVisible ? TEXT("ДА") : TEXT("нет"), *StateStr, P.X, P.Y, Sz.X, Sz.Y);
	}
	UE_LOG(LogAsdaChatWin, Log,
		TEXT("  ИТОГ АУДИТА: расхождений геометрии %d, свёрнуто сейчас %d (исходная видимость — из ассета)"),
		NumMismatch, NumCollapsed);

	// ★ СВЕРКА СХЕМ (грабля №6): наш слот ленты обязан совпасть со слотом плана импортёра.
	if (LinesBox)
	{
		if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(LinesBox))
		{
			const FAnchors A = CS->GetAnchors();
			const bool bLocalFill = FMath::IsNearlyEqual(A.Minimum.X, 0.f) && FMath::IsNearlyEqual(A.Maximum.X, 1.f)
			                     && FMath::IsNearlyEqual(A.Minimum.Y, 0.f) && FMath::IsNearlyEqual(A.Maximum.Y, 1.f);
			const bool bPointOk = FMath::IsNearlyEqual(A.Minimum.X, AsdaJui::kFxLeft, 0.001f)
			                   && FMath::IsNearlyEqual(A.Minimum.Y, AsdaJui::kFyBottom, 0.001f)
			                   && FMath::IsNearlyEqual(A.Maximum.X, AsdaJui::kFxLeft, 0.001f)
			                   && FMath::IsNearlyEqual(A.Maximum.Y, AsdaJui::kFyBottom, 0.001f);
			UE_LOG(LogAsdaChatWin, Log,
				TEXT("  СВЕРКА слота ленты: %s"),
				bLocalFill ? TEXT("вложена в родную группу (локальная заливка 0..1) — ВЕРНО")
				           : (bPointOk ? TEXT("POINT [0,1] как у планов импортёра — СОВПАДАЮТ (верно)")
				                       : TEXT("★ РАСХОДЯТСЯ С ИМПОРТЁРОМ — ЭТО БАГ")));
		}
	}

	// Прямоугольник зоны + общий реестр зон интерфейса.
	FVector2D Mn, Mx;
	if (GetWindowScreenRect(Mn, Mx))
	{
		const bool bOver = IsCursorOverWindow();
		UE_LOG(LogAsdaChatWin, Log,
			TEXT("  ПРЯМОУГОЛЬНИК ОКНА (абс. экран): (%.0f,%.0f)-(%.0f,%.0f) | курсор над окном=%s")
			TEXT(" → AAsdaPlayerController::OnLeftClick мировой клик %s"),
			Mn.X, Mn.Y, Mx.X, Mx.Y, bOver ? TEXT("ДА") : TEXT("нет"),
			bOver ? TEXT("БЛОКИРУЕТ") : TEXT("пропускает"));
	}
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			UE_LOG(LogAsdaChatWin, Log, TEXT("%s"), *WM->DebugUiZones());
		}
	}
	UE_LOG(LogAsdaChatWin, Log, TEXT("%s"), *DebugState());
	UE_LOG(LogAsdaChatWin, Log, TEXT("==========================================================="));
}

// ═══════════════════════ ЭТАП 2 — ВВОД (поле 2609, приглашение, антифлуд) ═════════════════════

// Поле ввода 2609 уже есть в WBP (`CHAT_NEW_Edit2`, UEditableText). Настраиваем его 1:1:
//   `ctrlEditSetTextLength(chatNewEdit, 68)` (chat.c:826) — лимит 68 символов;
//   цвет текста 0xCCFFFFFF (chat.c:831-833);
//   приглашение режима — отдельная подпись слева от поля, текст loc 5316 + inputState
//   (`uiQuestInput_UpdatePromptByState`). В оригинале приглашение рисуется тем же контролом,
//   у нас — отдельным TextBlock, а поле сдвигается вправо на его ширину: так текст игрока
//   нельзя случайно стереть вместе с приглашением.
void UAsdaChatWindowSubsystem::BuildInputWidgets()
{
	if (!ChatWidget || !ChatWidget->WidgetTree) { return; }
	UCanvasPanel* Root = Cast<UCanvasPanel>(ChatWidget->WidgetTree->RootWidget);
	if (!Root) { return; }

	InputEdit = Cast<UEditableText>(ChatWidget->GetWidgetFromName(TEXT("CHAT_NEW_Edit2")));
	if (InputEdit)
	{
		FSlateFontInfo F = TipFont;
		F.Size = (int32)kFontPx;
		InputEdit->SetFont(F);
		// Цвет текста поля 0xCCFFFFFF (chat.c:831-833), sRGB → linear. У UEditableText цвет
		//   живёт в стиле, отдельного сеттера нет — правим стиль целиком.
		// ★★★ ЭТО НЕ КОСТЫЛЬ ПОД ИМПОРТЁР — ПРОВЕРЕНО 2026-08-27 (класс С2 аудита
		//   `Reports/audit_runtime_style.md` предлагал убрать эти строки как «цвет, который
		//   должен приезжать из `EXS TextColor`»; ПРЕДЛОЖЕНИЕ ОТКЛОНЕНО).
		//   В данных у `CHAT_NEW_Edit2` лежит `EXS TextColor = FFC6C6C6`, и эталон его читает
		//   (`CEditBox_LoadFromXml @0x12F95A0` → поле this+3236). Но СРАЗУ ПОСЛЕ загрузки окна
		//   эталон ПЕРЕБИВАЕТ его сам: `chat.c:826-833` берёт указатель на то же поле и кладёт
		//   `0xCCFFFFFF` (там же, строкой выше, `ctrlEditSetTextLength(chatNewEdit, 68)`).
		//   Значения РАЗНЫЕ (`FFC6C6C6` ≠ `CCFFFFFF`), значит запечь данные и удалить код нельзя:
		//   поле чата стало бы непрозрачно-серым вместо полупрозрачно-белого.
		//   Импортёр при этом ПОЧИНЕН: он теперь кладёт в ассет цвет из `EXS TextColor` (все 66
		//   полей ввода), а этот вызов остаётся законным ВТОРЫМ слоем — ровно как в оригинале.
		FEditableTextStyle St = InputEdit->WidgetStyle;
		St.ColorAndOpacity = FSlateColor(AsdaSrgbHexToLinear(TEXT("CCFFFFFF")));
		St.Font = F;
		InputEdit->SetWidgetStyle(St);
		InputEdit->SetIsPassword(false);
		// Горизонталь — КАК В ОРИГИНАЛЕ, ВЛЕВО (скин Black_EditBox, CaptionAlign = 0).
		InputEdit->SetJustification(ETextJustify::Left);
		// ★ Перенесено в ассет (15.09, разрешение владельца): поле 2609 показано в WBP (эталон SetVisible(1)
		//   chat.c:827; шаг Tools/asda_ui_chat_markup_visibility.py). Показ при открытии строки ввода
		//   (OpenInput) — событие, остаётся в коде.
		InputEdit->OnTextCommitted.AddDynamic(this, &UAsdaChatWindowSubsystem::OnInputCommitted);
		// ★ ЖЁСТКИЙ СТОП НАБОРА — аналог EM_LIMITTEXT, который эталон ставит настоящему Win32-полю
		//   (`ctrlEditSetTextLength` → SendMessageA(hEdit, 0xC5, лимит, 0), ui_core.c:116693).
		//   У UEditableText лимита нет, поэтому режем прямо в обработчике изменения текста:
		//   набрать больше GetInputMaxChars() физически нельзя, как и в оригинале.
		InputEdit->OnTextChanged.AddDynamic(this, &UAsdaChatWindowSubsystem::OnInputChanged);
		UE_LOG(LogAsdaChatWin, Log,
			TEXT("[чат-ввод] поле 2609: лимит %d символов (эталон %d; переключается asda.Chat.InputMaxChars ")
			TEXT("— >80 требует серверного патча P14). ★ Канал армии (режим 9, опкод 6718) всегда %d"),
			GetInputMaxChars(), kInputMaxCharsEtalon, kInputMaxCharsArmy);
	}

	// Приглашение режима (loc 5316 + state).
	// ★ 04.11.2026 (смена карты): окно чата пересоздаётся при OpenLevel, а гард `if (!InputPrefix)`
	//   оставлял указатель на узел СНЕСЁННОГО окна — в новом окне узел не создавался вовсе
	//   (самотест `asda.SelfTest.IconsAfterTravel`, обход подсистем). Берём узел из дерева ТЕКУЩЕГО окна.
	InputPrefix = Cast<UTextBlock>(ChatWidget->WidgetTree->FindWidget(FName(TEXT("CHAT_NEW_InputPrefix"))));
	if (!InputPrefix)
	{
		if (UTextBlock* Tb = ChatWidget->WidgetTree->ConstructWidget<UTextBlock>(
				UTextBlock::StaticClass(), FName(TEXT("CHAT_NEW_InputPrefix"))))
		{
			Tb->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			Tb->SetAutoWrapText(false);
			FSlateFontInfo F = TipFont;
			F.Size = (int32)kFontPx;
			Tb->SetFont(F);
			Tb->SetColorAndOpacity(FSlateColor(AsdaSrgbHexToLinear(TEXT("FFFFD800"))));  // как слот «Шепот»
			if (UCanvasPanelSlot* CS = Root->AddChildToCanvas(Tb))
			{
				AsdaJui::SetChatSlot(CS, kEditX + 3.f, kEditY + 4.f, kEditW - 6.f, 14.f);
				CS->SetZOrder(75);
			}
			InputPrefix = Tb;
		}
	}

	// Подпись полосы антифлуда 2619 — текст loc 5326 (chat.c:851).
	FloodText = Cast<UTextBlock>(ChatWidget->GetWidgetFromName(TEXT("CHAT_NEW_Bar_TEXT2")));
	if (FloodText)
	{
		FSlateFontInfo F = TipFont;
		F.Size = (int32)kFontPx;
		FloodText->SetFont(F);
		FloodText->SetAutoWrapText(false);
		FloodText->SetJustification(ETextJustify::Center);
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAsdaTextSubsystem* Txt = GI->GetSubsystem<UAsdaTextSubsystem>())
			{
				FloodText->SetText(FText::FromString(Txt->GetTextById(5326)));
			}
		}
	}
	// Заливка полосы 2618 — второй план скина Red_Bar_R (`_32_1`), ширину клипаем по остатку.
	FloodBarFill = ChatWidget->GetWidgetFromName(TEXT("CHAT_NEW_Bar2_32_1"));

	ApplyEtalonTextColors();   // ★ цвета ВСЕХ подписей окна — из EXS TextColor, а не из CaptionInfo
	SetInputState(InputState);
	CloseInput();
}

void UAsdaChatWindowSubsystem::SetInputState(int32 State, const FString& WhisperTo)
{
	InputState = State;
	if (!WhisperTo.IsEmpty()) { WhisperTarget = WhisperTo; }
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	// ★ ПРИГЛАШЕНИЕ — НЕ чистая формула «5316 + state»: на 8 и 9 она ломается.
	//   Эталон (chat.c:3949 и 3966) сравнивает текст поля с loc **5325** для состояния 8
	//   («[Друг]», а не 5324) и с loc **5337** для состояния 9 («[Армия]»). Держим таблицей.
	static const int32 kPromptLoc[10] = { 5316, 5317, 5318, 5319, 5320, 5321, 5322, 5323, 5325, 5337 };
	const int32 PromptId = (State >= 0 && State <= 9) ? kPromptLoc[State] : (5316 + State);
	FString P = UiChannelText(PromptId);   // ★ с переименованием «По всем каналам» → «Все каналы»
	// ★ Скобки у названий каналов снимаем (требование юзера).
	P.ReplaceInline(TEXT("["), TEXT(""), ESearchCase::CaseSensitive);
	P.ReplaceInline(TEXT("]"), TEXT(""), ESearchCase::CaseSensitive);
	P.TrimStartAndEndInline();
	if (State == 7 && !WhisperTarget.IsEmpty())
	{
		// loc 5323 — «[Шепчемся с персонажем %s]»: подставляем имя, как эталон.
		if (P.Contains(TEXT("%s"))) { P = P.Replace(TEXT("%s"), *WhisperTarget); }
		else { P += FString::Printf(TEXT(" %s"), *WhisperTarget); }
	}
	// ★ ОТСТУПЛЕНИЕ (требование юзера 2026-08-05): ПРИГЛАШЕНИЕ РЕЖИМА В ПОЛЕ НЕ ПОКАЗЫВАЕМ.
	//   В оригинале оно есть и лежит В САМОМ поле (`uiQuestInput_UpdatePromptByState` ставит
	//   текст loc 5316+state через CControl_SetText, а проверка `strcmp(editText, приглашение)`
	//   отсекает отправку пустой строки) — но вместе с плашкой комбобокса это давало дубль.
	//   Само значение приглашения продолжаем вычислять — оно уходит в лог и в дампы.
	if (InputPrefix) { InputPrefix->SetVisibility(ESlateVisibility::Collapsed); }
	if (InputEdit)
	{
		if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(InputEdit))
		{
			// Поле лежит в группе SizeWin — координаты локальные (origin группы 1,588).
			// ★ ЦЕНТРОВКА ПО ВЕРТИКАЛИ (требование юзера): UEditableText вертикального
			//   выравнивания не имеет — центрируем САМИМ слотом: высота строки кегля 8
			//   ≈ 14 px в поле 20 px ⇒ смещаем на (20 − 14) / 2 = 3 px вниз и даём высоту 14.
			// ★ ШИРИНА — С НАДБАВКОЙ ГОРИЗОНТАЛЬНОГО РЕСАЙЗА, иначе смена канала ПОСЛЕ растягивания
			//   схлопывала поле обратно на авторские 336 px (эта строка выполняется позже раскладки).
			CS->SetOffsets(FMargin(kEditX - 1.f, 138.f + 3.f,
				kEditW + FMath::Max(0.f, BaseWidth() - kWinW), 14.f));
		}
		// ★★★ ЛИМИТ ЗАВИСИТ ОТ РЕЖИМА — обрезаем УЖЕ НАБРАННОЕ при переключении.
		//   Единственное исключение: КАНАЛ АРМИИ (режим 9, опкод 6718) держится на 80 байтах —
		//   у него ФИКСИРОВАННАЯ длина пакета (252, текст ровно 200 б) и MD5-подпись по 0xE4,
		//   а пересылка 6719 идёт пакетом ФИКСИРОВАННОЙ длины 101 с записью `char[80]`.
		//   Серверный патч P14 (лимит 160) этот канал СОЗНАТЕЛЬНО не трогает, поэтому ограничение
		//   доводит клиент (§7.5 п.2 реверса chat_context_menu_and_limits.md).
		const int32 MaxLen = GetActiveInputMaxChars();
		const FString Cur = InputEdit->GetText().ToString();
		// ★ [ПОРТ] 08.10.2026: лимит в БАЙТАХ ПРОВОДА (как EM_LIMITTEXT оригинала по ANSI-буферу): cp1251 — символ = байт,
		//   арабский/смешанный (UTF-8) — 2 байта на букву. Обрезка по границе символа (`TruncateToWireBytes`).
		if (!IsWithinActiveInputLimit(Cur))   // С3 08.10.2026: в режиме 6 с «Друг» — и префикс+текст ≤ 160 б
		{
			InputEdit->SetText(FText::FromString(ClampToActiveInputLimit(Cur)));
			UE_LOG(LogAsdaChatWin, Warning,
				TEXT("[чат-ввод] смена режима на %d: набранный текст обрезан с %d до %d символов%s"),
				State, Cur.Len(), MaxLen,
				(State == 9) ? TEXT(" (канал армии 6718 остаётся на 80 — фикс. длина пакета + MD5)") : TEXT(""));
		}
	}
	// Комбобокс держим синхронным. ★ РАНЬШЕ здесь стояла жёсткая эталонная таблица
	//   `channelOrder[6] = {4,3,8,6,5,7}` — она знает только про ШЕСТЬ эталонных пунктов, поэтому
	//   переход в «Поиск отряда» (1), «Торговлю» (2) или «Армию» (9) подпись комбобокса НЕ обновлял.
	//   Ищем по ФАКТИЧЕСКОЙ таблице списка — она же и есть наш channelOrder, и её состав меняется
	//   (из неё убран слот 0). Состояние, которого в списке нет (например 0 после его удаления),
	//   просто не двигает выделение — подпись остаётся на прежнем пункте, что честно: пункта нет.
	const int32 Found = ComboSlots.IndexOfByKey(State);
	if (Found != INDEX_NONE && ComboIndex != Found)
	{
		ComboIndex = Found;
		RefreshChannelCombo(-1);
	}
	else if (Found == INDEX_NONE)
	{
		UE_LOG(LogAsdaChatWin, Log,
			TEXT("[чат-каналы] режим %d в списке отправки отсутствует (для слота 0 это норма: пункт «По всем каналам» убран решением юзера) — выделение не двигаем"),
			State);
	}
	SelectedChannel = State;
	// ★ Эталон при смене режима гасит полосу и показывает её заново только в ветке «широких»
	//   каналов (chat.c:1785-1796). У нас та же точка вызова, но ★ ОТСТУПЛЕНИЕ: полоса берёт
	//   таймер ИМЕННО НОВОГО канала (в игре счётчик один общий) — см. ApplyCooldownBar.
	ApplyCooldownBar();
	UE_LOG(LogAsdaChatWin, Log,
		TEXT("[чат-ввод] режим %d — приглашение «%s» | таймеры каналов: 0=%.0f 1=%.0f 2=%.0f"),
		State, *P, ChannelCooldownLeft[0], ChannelCooldownLeft[1], ChannelCooldownLeft[2]);
}

// ★ Аналог EM_LIMITTEXT: набрать больше лимита физически нельзя (эталон ставит лимит самому
//   Win32-полю класса «edit», ui_core.c:116681-116694). Обрезаем и возвращаем каретку в конец.
void UAsdaChatWindowSubsystem::OnInputChanged(const FText& Text)
{
	if (!InputEdit) { return; }
	// ★ Лимит зависит от РЕЖИМА: канал армии (9, опкод 6718 с MD5 и фикс. длиной) — 80, прочие — общий.
	const int32 MaxLen = GetActiveInputMaxChars();
	const FString S = Text.ToString();
	// ★ [ПОРТ] 08.10.2026: считаем БАЙТЫ ПРОВОДА (`WireTextByteLen`), а не символы: у оригинала EM_LIMITTEXT по ANSI-буферу,
	//   и сервер режет текст по байтам (strlen ≤ 80/160). Русский/английский — как раньше (cp1251: 1 байт), арабский — 2 байта.
	//   С3 08.10.2026: в режиме 6 с «Друг» — ещё и префикс+текст ≤ 160 б провода (`IsWithinActiveInputLimit`).
	if (IsWithinActiveInputLimit(S)) { return; }
	InputEdit->SetText(FText::FromString(ClampToActiveInputLimit(S)));
	UE_LOG(LogAsdaChatWin, Verbose,
		TEXT("[чат-ввод] набор остановлен на лимите %d символов (режим %d; asda.Chat.InputMaxChars=%d)"),
		MaxLen, InputState, GetInputMaxChars());
}

void UAsdaChatWindowSubsystem::OpenInput(int32 State)
{
	if (State >= 0) { SetInputState(State); }
	bInputActive = true;
	CloseFocusDropFrames = 0;
	InputFocusGraceFrames = kInputFocusGraceFrames;   // фокус приедет не мгновенно — см. ReconcileInputFocus
	if (InputEdit)
	{
		InputEdit->SetVisibility(ESlateVisibility::Visible);
		InputEdit->SetKeyboardFocus();
	}
	UE_LOG(LogAsdaChatWin, Log, TEXT("[чат-ввод] строка открыта, режим %d"), InputState);
}

// ★★★ ПОРТ `uiChatInputShowWithText@0xBA3E80` (`ui_core.c:12037`) — ЕДИНАЯ ТОЧКА «ОТКРЫТЬ ВВОД С
//   ГОТОВЫМ ТЕКСТОМ». Эталон:
//       if ( sub_934E7C(chatNewEdit) )                 // поле ввода УЖЕ В ФОКУСЕ
//           CControl_SetFocus(chatNewEdit, 0); uiPanel_ShowAndSelectActive(this, 0);  // свернуть
//       else
//           CControl_SetText(chatNewEdit, Str); CControl_SetFocus(chatNewEdit, 1);
//           uiPanel_ShowAndSelectActive(this, 1); CControl_SetVisible(chatWin, 1);
//   ★ Что такое `sub_934E7C` — доказано ВТОРЫМ МЕТОДОМ по двум независимым местам: в окне обмена
//     `item_enhance.c:2279` («не в фокусе → SetFocus(1), SetText, SetFocus(0)») и в логине
//     `login_charselect.c:1416` (Tab: «поле логина в фокусе → перевести фокус на список»). Это
//     «контрол в фокусе», то есть ровно наш `bInputActive` после сверки с фактом (ReconcileInputFocus).
//   Смысл ветки «уже в фокусе»: НЕ ЗАТИРАТЬ уже набранный игроком текст.
void UAsdaChatWindowSubsystem::OpenInputWithText(const FString& Text)
{
	if (IsInputActive())
	{
		// Эталонная ветка «поле уже в фокусе»: ввод сворачивается, текст не подставляется.
		UE_LOG(LogAsdaChatWin, Log,
			TEXT("[чат-ввод] строка уже в фокусе — эталон её СВОРАЧИВАЕТ и текст не подставляет "
			     "(`uiChatInputShowWithText`, ветка sub_934E7C=истина)"));
		CloseInput();
		return;
	}
	OpenInput(-1);                       // общая точка открытия: флаг, видимость, фокус
	if (InputEdit)
	{
		InputEdit->SetText(FText::FromString(Text));
		InputEdit->SetKeyboardFocus();   // каретка встаёт в конец: выделения эталон не делает
	}
	UE_LOG(LogAsdaChatWin, Log, TEXT("[чат-ввод] строка открыта с готовым текстом «%s»"), *Text);
}

// ★★★ ШЁПОТ, ВЫБРАННЫЙ МЫШЬЮ — порт `CChatList_OnSelectWhisper@0xDBB7C0` (`chat.c:6280`).
//   Эталон кладёт в поле ввода ЛИТЕРАЛ `sprintf(buf, "/w %s ", имя)` — это НЕ строка локализации
//   (проверено: в теле функции нет ни одного `UI_FormatLocalizedNotice`), и НЕ строка в ленте чата
//   (`uiChatLogAddLine`/`CChat_AddTokenizedLines` в теле тоже нет). Дальше эталон гасит легаси-
//   подложки (`uiPanel_SetFocusChildren(chat, 0)`) и зовёт `uiChatInputShowWithText`.
//   Приглашение loc 5323 «[Шепчемся с персонажем %s] » в оригинале появляется В ТОМ ЖЕ ПОЛЕ, но
//   ПОЗЖЕ — когда разбор слэш-команды по пробелу переведёт ввод в состояние 7. У нас приглашение
//   в поле не показывается (действующее решение владельца 2026-08-05: дубль подписи комбобокса),
//   поэтому ВИДИМАЯ обратная связь — это ровно подставленный `/w имя `, как в оригинале.
//   Состояние 7 ставим сразу (действующее поведение порта): подпись комбобокса 2608 показывает
//   «ШЕПОТ» (loc 5335), а адресат уходит в пакет 5088 из SendCurrentChannel.
void UAsdaChatWindowSubsystem::BeginWhisperTo(const FString& Name)
{
	if (Name.IsEmpty())
	{
		UE_LOG(LogAsdaChatWin, Warning, TEXT("[чат-ввод] шёпот: имя адресата пусто — эталон молча выходит"));
		return;
	}
	if (IsInputActive())
	{
		// Ветка эталона «поле уже в фокусе» — целиком в OpenInputWithText, состояние не трогаем.
		OpenInputWithText(FString());
		return;
	}
	SetInputState(7, Name);
	OpenInputWithText(FString::Printf(TEXT("/w %s "), *Name));
	UE_LOG(LogAsdaChatWin, Log,
		TEXT("[чат-ввод] шёпот «%s»: в поле 2609 подставлено «/w %s » (эталон CChatList_OnSelectWhisper)"),
		*Name, *Name);
}

// ★★★ ОБЩАЯ ТОЧКА ПРАВДЫ ДЛЯ ФЛАГА «ИДЁТ НАБОР В ЧАТЕ».
//   ЭТАЛОН: `Input_IsTextInputFocused@0x986670` (input.c:891) возвращает ПЕРВОЕ ПОЛЕ самого окна
//   ввода `g_pChatInputWnd` — то есть признак «окно в фокусе», который ведёт менеджер окон и
//   СНИМАЕТ, как только фокус ушёл (клик мимо поля, закрытие окна-владельца). У нас же это был
//   ЛАТЧ `bInputActive`: его ставил `OpenInput`, а снимали только Esc (`OnInputCommitted`
//   `OnCleared`) и пересборка окна. Путь «кнопка „Шёпот“ в окне поиска отряда → OpenInput → игрок
//   закрыл окно/кликнул мимо, не нажав Esc» оставлял латч взведённым НАВСЕГДА.
//   Цена дефекта (лог 2026-08-30 18:08:36 → 18:12:30): гейт `IsTextInputFocused` стоит в трёх
//   местах — системное меню (кнопки 73401…73409), хотбар и диспетчер клавиш контроллера, —
//   поэтому после залипания НЕ ОТКРЫВАЛОСЬ НИ ОДНО окно ни кнопкой, ни клавишей, и не работало
//   движение WASD. Класс дефекта — `no-state-latch-before-shown`.
//   ЧИНИМ: раз в кадр сверяем латч с фактическим фокусом Slate у поля 2609. Фокуса нет дольше
//   отсрочки — латч снимаем. Текст в поле НЕ трём и режим ввода игроку НЕ переставляем: фокус уже
//   у мира/другого виджета, а `CloseInput` дополнительно дёргал бы `SetInputMode` и мог отобрать
//   фокус у виджета, по которому игрок только что кликнул.
// ★★★ 06.11.2026 [ОРИГИНАЛ] ФОКУС ПОЛЯ = НАБОР (`asda.Chat.FocusIsTyping`; найдено самотестом `asda.SelfTest.DeathChat`).
//   У эталона состояние одно: «поле в фокусе» (`sub_934E7C`). Enter в поле отправляет и снимает фокус (`chatInput_OnCommandKey`
//   case 2609 → `uiChatInputToggle` ui_core.c:11989: `CControl_SetFocus(поле, 0)`); щелчок в поле — это и есть открытая строка.
//   В порту флаг и фокус расходились в ОБЕ стороны:
//   1) ПОСЛЕ ОТПРАВКИ: `SubmitInput` просит фокус `SetKeyboardFocus` (отложенная операция локального игрока), следом коммит
//      `OnCleared` зовёт `CloseInput`, а отложенный фокус приезжает ПОСЛЕ него. Флаг снят, фокус в поле ⇒ буквы идут и в поле,
//      и в раскладку; R и Ctrl+W печатаются в поле вместо команды, Enter не доходит до строки чата.
//   2) ЩЕЛЧОК В ПОЛЕ: флаг не ставился вовсе ⇒ клавиши при открытом поле срабатывали как горячие.
//   Лечение: несколько кадров после закрытия фокус поля возвращаем миру; позже фокус поля = набор. 0 — прежний порт.
static ASDA_DEV_CVAR(int32, CVarChatFocusIsTyping,
	TEXT("asda.Chat.FocusIsTyping"), 1,
	TEXT("[ОРИГИНАЛ] Строка чата открыта ровно тогда, когда поле в фокусе: после отправки фокус уходит миру, щелчок в поле открывает набор. ")
	TEXT("0 — прежний порт: фокус оставался в поле после отправки, щелчок флага набора не ставил."));

// Закрытая строка: сверка фокуса поля с флагом (разбор у ключа выше).
void UAsdaChatWindowSubsystem::ReconcileClosedInputFocus()
{
	if (CVarChatFocusIsTyping.GetValueOnGameThread() == 0 || !InputEdit) { CloseFocusDropFrames = 0; return; }
	const TSharedPtr<SWidget> Sw = InputEdit->GetCachedWidget();
	const bool bFocused = Sw.IsValid() && Sw->HasAnyUserFocusOrFocusedDescendants();
	if (CloseFocusDropFrames > 0)
	{
		--CloseFocusDropFrames;
		if (bFocused && FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().SetAllUserFocusToGameViewport();
			UE_LOG(LogAsdaChatWin, Log, TEXT("[чат-ввод] строка закрыта — запоздалый фокус поля 2609 возвращён миру"));
		}
		return;
	}
	if (!bFocused) { return; }
	bInputActive = true;
	InputFocusGraceFrames = kInputFocusGraceFrames;
	UE_LOG(LogAsdaChatWin, Log, TEXT("[чат-ввод] поле 2609 получило фокус щелчком — набор открыт, режим %d"), InputState);
}

void UAsdaChatWindowSubsystem::ReconcileInputFocus()
{
	if (!bInputActive) { InputFocusGraceFrames = 0; ReconcileClosedInputFocus(); return; }

	// Поля ввода нет (окно пересобирается) — держать флаг не на чем.
	if (!InputEdit)
	{
		bInputActive = false;
		InputFocusGraceFrames = 0;
		UE_LOG(LogAsdaChatWin, Warning,
			TEXT("[чат-ввод] флаг набора снят: поля 2609 не существует (окно пересобирается)"));
		return;
	}

	// Фокус считаем по ЖИВОМУ Slate-виджету: у `UEditableText` клавиатурный фокус уходит в
	//   потомка (SEditableText), поэтому спрашиваем «фокус на мне ИЛИ на потомке».
	bool bFocused = false;
	if (const TSharedPtr<SWidget> Sw = InputEdit->GetCachedWidget())
	{
		bFocused = Sw->HasAnyUserFocusOrFocusedDescendants();
	}
	if (bFocused) { InputFocusGraceFrames = kInputFocusGraceFrames; return; }
	if (InputFocusGraceFrames > 0) { --InputFocusGraceFrames; return; }

	bInputActive = false;
	UE_LOG(LogAsdaChatWin, Log,
		TEXT("[чат-ввод] фокус ушёл из поля 2609 — флаг набора снят (режим %d). ")
		TEXT("Без этого гейт «чат-ввод в фокусе» глушил бы все окна и клавиши"), InputState);
}

// Дамп для консоли: латч против факта. Печатает ровно то, по чему дефект и ловится.
FString UAsdaChatWindowSubsystem::DebugInputState() const
{
	bool bFocused = false;
	bool bHasWidget = false;
	if (InputEdit)
	{
		if (const TSharedPtr<SWidget> Sw = InputEdit->GetCachedWidget())
		{
			bHasWidget = true;
			bFocused = Sw->HasAnyUserFocusOrFocusedDescendants();
		}
	}
	// ★ Текст поля печатаем ОБЯЗАТЕЛЬНО: именно он — видимая обратная связь при выборе шёпота
	//   («/w имя » эталона). Без него «не видно» доказать нечем.
	const FString Cur = InputEdit ? InputEdit->GetText().ToString() : FString();
	return FString::Printf(
		TEXT("[чат-ввод] флаг набора=%d | поле 2609: есть=%d живой виджет=%d фокус=%d текст«%s» | ")
		TEXT("отсрочка=%d | режим=%d | адресат шёпота «%s»"),
		bInputActive ? 1 : 0, InputEdit ? 1 : 0, bHasWidget ? 1 : 0, bFocused ? 1 : 0, *Cur,
		InputFocusGraceFrames, InputState, *WhisperTarget);
}

// ★ 25.10.2026 [ИСПРАВЛЕНИЕ] (найдено самотестом приседа `asda.SelfTest.CrouchMode`, шаг R9): `FInputModeGameAndUI` без виджета
//   фокус НЕ переносит (`FInputModeDataBase::SetFocusAndLocking` ставит фокус только виджету, если он задан) — после закрытия
//   строки чата фокус клавиатуры оставался в её `SEditableText`. Буквы до вьюпорта всплывают (ход W/S/A/D шёл), но общий гейт
//   игрового ввода `AAsdaPlayerCharacter::IsJumpInputAllowed` («фокус в поле ввода») держал закрытыми прыжок (Пробел) и присед
//   (Ctrl) до первого клика по миру. 1 — фокус возвращается вьюпорту игры (только если он всё ещё в строке чата: чужое поле,
//   получившее фокус, не трогаем); 0 — прежнее.
static ASDA_DEV_CVAR(int32, CVarAsdaChatCloseFocusWorld,
	TEXT("asda.Chat.CloseFocusWorld"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 — закрытие строки чата возвращает фокус клавиатуры миру (прыжок и присед сразу работают); 0 — фокус остаётся в поле."),
	ECVF_Default);

void UAsdaChatWindowSubsystem::CloseInput()
{
	bInputActive = false;
	InputFocusGraceFrames = 0;
	CloseFocusDropFrames = kCloseFocusDropFrames;   // ★ 06.11.2026: хвост закрытия — `ReconcileClosedInputFocus`
	if (InputEdit)
	{
		InputEdit->SetText(FText::GetEmpty());
		if (APlayerController* PC = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr)
		{
			// Вернуть фокус миру: иначе WASD продолжает уходить в поле.
			FInputModeGameAndUI M;
			M.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			M.SetHideCursorDuringCapture(false);
			PC->SetInputMode(M);
			// ★ 25.10.2026: режим ввода сам фокус не переносит (разбор у ключа выше) — переносим явно, пока он в строке чата.
			if (CVarAsdaChatCloseFocusWorld.GetValueOnGameThread() != 0 && FSlateApplication::IsInitialized() && InputEdit->HasKeyboardFocus())
			{
				FSlateApplication::Get().SetAllUserFocusToGameViewport();
			}
		}
	}
}

// Ключ `asda.Chat.WhisperAnswer` (объявлен в AsdaChatWindowSubsystem.cpp): true — «R» как оригинал.
bool AsdaChatWhisperAnswerOn();

// ★ [ИСПРАВЛЕНИЕ] 05.11.2026: «R» с шептавшим кладёт в поле «/w имя » (видимый адресат вместо скрытого приглашения 5323);
//   разбор — в `AnswerLastWhisper` ниже. 1 — подставлять (по умолчанию); 0 — прежнее: режим 7, поле пустое.
static ASDA_DEV_CVAR(int32, CVarChatWhisperAnswerPrefill,
	TEXT("asda.Chat.WhisperAnswerPrefill"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Клавиша «R» после входящего шёпота: 1 — строка открывается с «/w имя » (адресат виден, как при выборе шёпота мышью); ")
	TEXT("0 — прежнее: режим шёпота, поле пустое."),
	ECVF_Default);

// ★ 08.11.2026 [ИСПРАВЛЕНИЕ] (пачка 15, баг владельца): «R», когда отвечать некому. Эталон (`ChatInput_ParseWhisper` case 1,
//   chat.c:2875-2880) при пустом chat+2552 пишет в ленту loc 6942 «Неправильно указано имя пользователя.» / «Incorrect user name.»
//   и очищает поле. Имени игрок не вводил, поэтому строка ложная. 1 — R молчит: строки нет, поле и режим не трогаются.
//   0 — как эталон: строка 6942 и пустое поле.
static ASDA_DEV_CVAR(int32, CVarChatWhisperAnswerNoTargetSilent,
	TEXT("asda.Chat.WhisperAnswerNoTargetSilent"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Клавиша «R», когда шёпота не было: 1 — ничего не происходит (нет строки «Неправильно указано имя пользователя.», ")
	TEXT("поле не трогается); 0 — как оригинал: строка loc 6942 в ленту и очистка поля."),
	ECVF_Default);

// CHAT ANSWER (69) — быстрый ответ последнему шептавшему. Справка loc 5406: «нажми 'R' и введи сообщение».
// ★★★ [ОРИГИНАЛ] 25.10.2026 — порт `ChatInput_ParseWhisper(chat, 1)` (клавиша 69: input.c:4407-4409; тело chat.c:2873-2900):
//   • последнего шептавшего нет (`strlen(chat+2552) <= 0`) → loc **6942** строкой ЛЕНТЫ `Chat_AddChatLine(chat, 5, …, −10240)`
//     (кат. 5 = слот «Шепот», 0xFFFFD800) и ОЧИСТКА поля (`CControl_SetText(edit, "")`); режим не меняется, ввод не открывается;
//   • есть → приглашение loc 5323 «Шепчемся с %s», `chat+420 = 7`, адресат `chat+2532 = chat+2552`, фокус в поле.
//   Шептавший запоминается при приёме 5089 (AsdaChatWindowSubsystem.cpp, `NoteWhisperFrom`). Ключ 0 — прежнее поведение порта:
//   без шёпота ввод открывался в текущем режиме, в журнал — предупреждение.
void UAsdaChatWindowSubsystem::AnswerLastWhisper()
{
	if (LastWhisperFrom.IsEmpty())
	{
		if (!AsdaChatWhisperAnswerOn())
		{
			UE_LOG(LogAsdaChatWin, Warning, TEXT("[чат-ввод] CHAT ANSWER: шёпота ещё не было"));
			OpenInput(-1);
			return;
		}
		if (CVarChatWhisperAnswerNoTargetSilent.GetValueOnGameThread() != 0)
		{
			UE_LOG(LogAsdaChatWin, Log, TEXT("[чат-ввод] CHAT ANSWER: шептавшего нет — ничего не делаем (asda.Chat.WhisperAnswerNoTargetSilent=1)"));
			return;
		}
		UAsdaTextSubsystem* Txt = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
		const FString S = Txt ? Txt->GetTextById(6942) : FString();
		AddChatLineRaw(5, S.IsEmpty() ? FAsdaLang::MissingTag(6942) : S, FColor(0xFF, 0xD8, 0x00, 0xFF));   // −10240
		if (InputEdit) { InputEdit->SetText(FText::GetEmpty()); }
		UE_LOG(LogAsdaChatWin, Log, TEXT("[чат-ввод] CHAT ANSWER: шептавшего нет — loc 6942 в ленту (кат. 5), поле очищено"));
		return;
	}
	// ★ [ИСПРАВЛЕНИЕ] 05.11.2026 (баг игроков «R не подставляет /w имя»): ИМЯ АДРЕСАТА ВИДНО В ПОЛЕ. Эталон в этой ветке пишет В САМО
	//   поле приглашение loc 5323 «[Шепчемся с персонажем %s]» (`CControl_SetText(chat+24, messageText)`, chat.c:2893), и игрок видит,
	//   кому ответит. У порта приглашения в поле нет (решение владельца 2026-08-05, см. `SetInputState`), поэтому строка открывалась
	//   ПУСТОЙ: режим 7 и адресат стояли, но игрок не видел ни имени, ни признака шёпота и считал, что R не работает. 1 — поле
	//   получает литерал «/w имя » тем же путём, что выбор шёпота мышью (`BeginWhisperTo` = `CChatList_OnSelectWhisper@0xDBB7C0`,
	//   chat.c:6280: `sprintf(buf, "/w %s ", имя)` — литерал, одинаковый в RU и EN); Enter разбирает его в тот же 5088 вида 0 на
	//   шептавшего. 0 — прежнее: режим 7 и пустое поле.
	if (CVarChatWhisperAnswerPrefill.GetValueOnGameThread() != 0)
	{
		BeginWhisperTo(LastWhisperFrom);
		UE_LOG(LogAsdaChatWin, Log, TEXT("[чат-ввод] CHAT ANSWER: шёпот последнему шептавшему «%s», в поле «/w %s »"), *LastWhisperFrom, *LastWhisperFrom);
		return;
	}
	SetInputState(7, LastWhisperFrom);
	OpenInput(7);
	UE_LOG(LogAsdaChatWin, Log, TEXT("[чат-ввод] CHAT ANSWER: шёпот последнему шептавшему «%s»"), *LastWhisperFrom);
}

void UAsdaChatWindowSubsystem::ToggleInput()
{
	if (bInputActive) { CloseInput(); } else { OpenInput(-1); }
}

void UAsdaChatWindowSubsystem::OnInputCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		SubmitInput(Text.ToString());
	}
	else if (CommitMethod == ETextCommit::OnCleared)   // Esc
	{
		CloseInput();
	}
}

// ★★★ СЛЭШ-КОМАНДЫ — 1:1 `chatParseSlashCommand @0xBA11F0` (chat.c:1441-1790).
//   Эталон вызывает её ПО ПРОБЕЛУ (msgCode 67591, wparam == 32, ui_core.c:12558) и сверяет
//   начало строки через `str_StartsWith` с ТРЕМЯ вариантами: локализованным именем канала,
//   коротким алиасом («A », «J », «B » …) и оригинальным корейским написанием (байт-строки
//   `byte_13DA890..8DC` — их содержимого в срезе НЕТ, поэтому воспроизводим первые два).
//
//   loc-имя  алиас   → uiQuestInput_UpdatePromptByState(chat, N)      chat.c
//     5328    A      → 0  «[По всем каналам]»                          1616
//     5329    J      → 1  «[Поиск отряда]»                             1625
//     5330    B      → 2  «[Торговля]»                                 1634
//     5331    Y      → 3  «[Глобальный чат]»                           1643
//     5332    N      → 4  «[Общий]»  (при режиме осады вместо этого loc 5327)  1651
//     5333    P      → 5  «[Отряд]»                                    1667
//     5334    C      → 6  «[Клан]»                                     1675
//     5335    —      → 7  «[Шепчемся с …]», имя разбирается sscanf      1683
//      —      W / w  → 7  то же, имя следующим токеном                 1720
//     5336    S      → 8                                               1712
//     5312    R      → 9                                               1746
//   ★ ИСПРАВЛЕНИЕ моей же прежней догадки: «R» — это НЕ «ответ на шёпот», а переход в
//     состояние 9 (loc 5312 / приглашение loc 5337). Ответ на шёпот — отдельная команда
//     клавиатуры CHAT ANSWER (69), которая ставит состояние 7 с последним отправителем.
//
// ★★★ СУДЬБА «/A» ПОСЛЕ УДАЛЕНИЯ ПУНКТА «По всем каналам» ИЗ СПИСКА (решение юзера 2026-08-05).
//   ОСТАВЛЯЕМ РАБОТАТЬ, БЕЗ ОТКАЗА. Это не выдумка, а прямое следствие эталона:
//     • в ОРИГИНАЛЕ комбобокс 2608 широких каналов не содержит ВООБЩЕ (chat.c:719-731 — там
//       только шесть пунктов + «Прочие»), а слоты 0/1/2 доступны ИСКЛЮЧИТЕЛЬНО слэш-командами
//       A/J/B (chat.c:1616/1625/1634). То есть «нет пункта в списке, но команда работает» —
//       это и есть ЭТАЛОННОЕ состояние, а не поломка;
//     • убрав пункт слота 0, мы вернули список БЛИЖЕ к эталону, а не создали новую сущность;
//     • отказ («команда недоступна») пришлось бы ВЫДУМАТЬ: ни такой ветки, ни loc-строки под
//       неё в эталоне нет — запрещено правилом «не выдумывать».
//   Практическое следствие: «/A» переключит ввод в слот 0, приглашение сменится, но отправка
//   уйдёт в 6560 с каналом 0, который серверный патч обслуживать НЕ будет ⇒ сообщение никуда
//   не попадёт. Это честно логируется в SendCurrentChannel, а подпись комбобокса на несуществующий
//   пункт не переключается (см. SetInputState).
bool UAsdaChatWindowSubsystem::ParseSlashCommand(FString& InOut)
{
	FString S = InOut;
	S.TrimStartInline();
	if (S.IsEmpty() || S[0] != TEXT('/')) { return false; }

	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;

	struct FCmd { int32 LocId; const TCHAR* Short; int32 State; };
	static const FCmd kCmds[] =
	{
		{ 5328, TEXT("a"), 0 }, { 5329, TEXT("j"), 1 }, { 5330, TEXT("b"), 2 },
		{ 5331, TEXT("y"), 3 }, { 5332, TEXT("n"), 4 }, { 5333, TEXT("p"), 5 },
		{ 5334, TEXT("c"), 6 }, { 5336, TEXT("s"), 8 }, { 5312, TEXT("r"), 9 },
	};
	// Слово команды — до первого пробела (эталон сверяет ПРЕФИКС строки).
	FString Word, Rest;
	if (!S.RightChop(1).Split(TEXT(" "), &Word, &Rest)) { Word = S.RightChop(1); Rest.Empty(); }
	const FString W = Word.ToLower();

	// Шёпот: «/W имя текст» и локализованное имя канала loc 5335 (эталон chat.c:1683/1720).
	const FString LocWhisper = Txt ? Txt->GetTextById(5335).ToLower() : FString();
	if (W == TEXT("w") || W == TEXT("whisper") || (!LocWhisper.IsEmpty() && W == LocWhisper))
	{
		FString Name, Tail;
		if (Rest.Split(TEXT(" "), &Name, &Tail)) { SetInputState(7, Name); InOut = Tail; }
		else { SetInputState(7, Rest); InOut.Empty(); }
		return true;
	}
	for (const FCmd& C : kCmds)
	{
		const FString LocName = Txt ? Txt->GetTextById(C.LocId).ToLower() : FString();
		if (W == C.Short || (!LocName.IsEmpty() && W == LocName))
		{
			// ★ 31.10.2026 [ОРИГИНАЛ] `ChatInput_ParseCommand` (chat.c:1651-1659): «Общий» в бою (флаг +0x6A0) — режим не меняется,
			//   красная строка loc 5327 «Во время битвы армий общий чат недоступен.» (−65536, 10 с); команда разобрана — ничего не уходит.
			if (C.State == 4 && bChannelPrefixMode)
			{
				const FString M = Txt ? Txt->GetTextById(5327) : FString();
				AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, M.IsEmpty() ? FAsdaLang::MissingTag(5327) : M, FColor(255, 0, 0, 255), 10000.f);
				InOut.Empty();
				return true;
			}
			SetInputState(C.State);
			InOut = Rest;
			return true;
		}
	}
	// ★★★ ЭМОЦИИ СЛЭШ-КОМАНДОЙ — порт `ChatInput_ParseCommand@0xBAED10` (chat.c:3985-4640).
	//   Эталон сравнивает введённое после «/» с ДЕВЯТЬЮ нотисами и переводит персонажа в
	//   соответствующее состояние, отправляя серверу код действия.
	//     12145 → 33/100 · 12146 → 34/101 · 12147 → 35/102 · 12148 → 36/103 · 12149 → 37/104 ·
	//     12150 → 38/105 · 12151 → 39/106 · 12163 → 41/112 · 12164 → 42/113
	//   ★ ИСПРАВЛЕН ДЕФЕКТ ОРИГИНАЛА Э4: эталон приводит ввод к ВЕРХНЕМУ регистру, а сравнивает
	//     точным `strcmp` со строкой нотиса как есть («Haha», «Dance 1») — в любой латинской
	//     локализации совпадение невозможно и команды не работают. Здесь сравнение
	//     регистронезависимое (обе стороны в нижний), поэтому команды работают и по-русски.
	//   ★ ИСПРАВЛЕН ДЕФЕКТ ОРИГИНАЛА Э3: «Dance 2» ставит состояние 42, а не 41 (эталон играл
	//     себе `dance01`, хотя соседям уходил код `dance02`).
	{
		struct FEmoteCmd { int32 LocId; int32 EmoteIndex; };
		static const FEmoteCmd kEmotes[] =
		{
			{ 12145, 0 }, { 12146, 1 }, { 12147, 2 }, { 12148, 3 }, { 12149, 4 },
			{ 12150, 5 }, { 12151, 6 }, { 12163, 7 }, { 12164, 8 },
		};
		// Эталон сверяет ВСЮ строку после «/», а не только первое слово («Dance 1» с пробелом).
		const FString Full = S.RightChop(1).TrimStartAndEnd().ToLower();
		for (const FEmoteCmd& E : kEmotes)
		{
			const FString Name = Txt ? Txt->GetTextById(E.LocId).TrimStartAndEnd().ToLower() : FString();
			if (Name.IsEmpty() || Full != Name) { continue; }
			if (UAsdaCharaInfoSubsystem* CI = GI ? GI->GetSubsystem<UAsdaCharaInfoSubsystem>() : nullptr)
			{
				CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(0, E.EmoteIndex));
			}
			InOut.Empty();
			return true;
		}
	}

	// «/?» — эталон печатает 10 строк справки loc 5371..5380 (chat.c:4656, 1609).
	if (W == TEXT("?"))
	{
		for (int32 L = 5371; L <= 5380; ++L)
		{
			const FString Line = Txt ? Txt->GetTextById(L) : FString();
			if (!Line.IsEmpty()) { AddChatLineRaw(6, Line, FColor(255, 255, 255, 255)); }
		}
		InOut.Empty();
		return true;
	}
	return false;
}

// ★★★ ОТПРАВКА ПО ТЕКУЩЕМУ РЕЖИМУ — раскладки тел см. AsdaNetSubsystem.h (реверс 2026-08-05).
//   ★ ЭХО: клиент СВОЮ строку в ленту НЕ добавляет — её присылает сервер (подтверждено:
//   во всех успешных ветках отправки после CNetwork_Send идёт только очистка поля, а
//   обработчики приёма специально сравнивают отправителя с локальным игроком и всё равно
//   рисуют строку — network.c:10888/10567/10389). Поэтому здесь никакого AddChatLine.
bool UAsdaChatWindowSubsystem::SendCurrentChannel(const FString& Text)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!Net)
	{
		UE_LOG(LogAsdaChatWin, Warning, TEXT("[чат] отправка НЕВОЗМОЖНА: сетевой подсистемы нет"));
		return false;
	}

	// ★ Проверка доступности канала ДО отправки — там же, где в эталоне.
	FString Deny; bool bTop = false; int32 DenyFeedCat = -1;
	if (!ChannelAllowed(InputState, Deny, bTop, &DenyFeedCat))
	{
		// ★ 12.10.2026 [ОРИГИНАЛ]: часть отказов эталон печатает НЕ в журнал, а строкой ЛЕНТЫ (`Chat_AddChatLine`): шёпот без
		//   адресата — кат. 5 (chat.c:2907-2909), «Друг» без пары/имени — кат. 101 (chat.c:2297-2299/2365-2368). Цвет строки ленты
		//   берётся из канала (`AddChatLineRaw` перетирает переданный, как эталон), поэтому переданный — только запасной.
		// ★ 31.10.2026 [ОРИГИНАЛ]: каналы 0/1/2 в бою — ОДНОКНОПОЧНОЕ ОКНО (`CItem_BuildTooltipText(g_pItemTooltipUI, 0, 10222, …)`,
		//   chat.c / G:226348-226352), не строка журнала; пакета нет, поле очищено (`SubmitInput`).
		if (DenyFeedCat == kDenyMsgBox)
		{
			AsdaNotice::MsgBox(GI, EAsdaMsgCategory::War, Deny, AsdaLoc::kBtnOk, 10222);
			UE_LOG(LogAsdaChatWin, Log, TEXT("[чат] режим %d в бою: окно 10222 «%s», пакета нет (как оригинал)"), InputState, *Deny);
			return false;
		}
		if (DenyFeedCat >= 0)
		{
			AddChatLineRaw(DenyFeedCat, Deny, FColor(255, 0, 0, 255));
			UE_LOG(LogAsdaChatWin, Warning, TEXT("[чат] отправка ОТКЛОНЕНА: режим=%d «%s» (строка ленты, кат. %d)"), InputState, *Deny, DenyFeedCat);
			return false;
		}
		// Отказ — КРАСНОЙ строкой на 10 с в СИСТЕМНЫЙ ЖУРНАЛ (CChat_AddTokenizedLines,
		//   цвет −65536 = 0xFFFF0000, TTL 10000, дорожка 0), как в оригинале.
		// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U2): было прямое `UAsdaChatSubsystem::AddSystemMessage(…, A, 10000)` мимо
		//   маршрутизатора (не попадало в счётчики и `asda.Msg.Verbose`) — стало `AsdaNotice::ChatColored` (журнал +
		//   буфер A, тот же цвет и TTL; категория System — без звука, как прямой вызов).
		AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, Deny, FColor(255, 0, 0, 255), 10000.f);
		UE_LOG(LogAsdaChatWin, Warning, TEXT("[чат] отправка ОТКЛОНЕНА: режим=%d «%s»"), InputState, *Deny);
		return false;
	}

	const FString SelfName = WS ? WS->GetSelfCharName() : FString();
	bool bOk = false;
	int32 Opcode = 0;

	switch (InputState)
	{
	case 0: case 1: case 2:
		// ★★★ ЖИВОЙ ТЕСТ 2026-08-05: «в По всем каналам / Поиск отряда / Торговля сообщения не уходят».
		//   ВЕРДИКТ ПО СЕРВЕРУ (доказательство, а не догадка): в `RE/ida/server_work/GameServer.c`
		//   обработчика опкода **6560 (0x19A0) НЕТ ВООБЩЕ** — в диспетчере есть непрерывный ряд
		//   `case 0x1964 … 0x199B` и `case 0x19AF … 0x19D0`, а 0x19A0 в этом промежутке отсутствует
		//   (grep «0x19A0» по всему файлу — 0 совпадений). Пакет уходит в сокет и молча теряется.
		//   ★★★ ПОПЫТКА ОБОЙТИ ЭТО ЧЕРЕЗ 5086 ПРОВАЛИЛАСЬ И ОТКАЧЕНА (живой тест 2026-08-05).
		//   Слали 5086 с `Body[2] = InputState`, рассчитывая, что +2 — «номер канала». В ленте
		//   получилось: «Торговля» → **[Сервер]**, «Поиск отряда» → **[кан.]**, «По всем каналам»
		//   → без подписи. Юзер сверил с ОРИГИНАЛЬНЫМ клиентом — тот показывает ТО ЖЕ САМОЕ,
		//   то есть приёмная сторона исправна, а врал ОТПРАВИТЕЛЬ.
		//   РАЗБОР: байт +2 пакета 5086 — это ОБЛАСТЬ ОХВАТА глобального объявления, а не канал:
		//     `Recv_ChatMessage` (network.c:10983-10995): 0 → "[Имя]: текст";
		//       1 → GetLocalizedString(1187) = «кан.»;  2 → GetLocalizedString(1109) = «Сервер»;
		//       > 2 → строка не формируется вовсе.
		//     Эталонный ОТПРАВИТЕЛЬ 5086 (chat.c:3053) кладёт в +2 **ВСЕГДА 0** (`msgHeader[2] = 0`).
		//   ⇒ числа, которое дало бы «Торговля»/«Поиск отряда», в 5086 НЕ СУЩЕСТВУЕТ.
		//   ⇒ `kWideChannelsVia5086 = false`: шлём эталонный 6560, где номер канала = InputState 1:1
		//   (`channelHeader[0] = channelType`, chat.c:2529), а ответ приходит опкодом **6561**
		//   (`Recv_WhisperOrSystemMessage`, network.c:10823) с подписью формата loc 20030 и цветом
		//   слота канала. Приём 6561 у нас уже реализован 1:1. Чтобы круг замкнулся, НУЖЕН
		//   СЕРВЕРНЫЙ ПАТЧ: в GameServer.exe нет НИ 6560, НИ 6561 (grep 6560/0x19A0/6561/0x19A1 —
		//   по 0 совпадений). ТЗ — chat_main_window.md, Д15.
		if (kWideChannelsVia5086)
		{
			Opcode = 5086; bOk = Net->SendChatNormal5086(Text, SelfName, (uint8)InputState);
		}
		else
		{
			// ★ Слот 0 убран из списка отправки (решение юзера) и серверным патчем
			//   `chat_wide_channels_6560` обслуживаться НЕ будет — достижим только «/A».
			//   Пакет всё равно шлём эталонно (клиент 1:1), но предупреждаем явно.
			if (InputState == 0)
			{
				UE_LOG(LogAsdaChatWin, Warning,
					TEXT("[чат] режим 0 «По всем каналам»: пункт убран из списка отправки, серверный патч ")
					TEXT("его не обслуживает — пакет 6560 канал 0 уйдёт, но никому не придёт. Достижим только «/A»"));
			}
			Opcode = 6560; bOk = Net->SendChatChannel6560((uint8)InputState, Text);
		}
		break;
	case 3:
		Opcode = 5086; bOk = Net->SendChatNormal5086(Text, SelfName); break;
	case 4:
		Opcode = 5084; bOk = Net->SendChatShout5084(Text); break;
	case 5: case 41:
		{
			// ★ 12.10.2026 [ОРИГИНАЛ]: поле @0 пакета 5105 — НЕ свой userNum, а `g_nPartyInviteTargetIdx` = ИНДЕКС МОЕГО ОТРЯДА:
			//   `ChatInput_SendParty@0xBA8F50` (chat.c:3230/3244) → `Party_SendChat(g_nPartyInviteTargetIdx, msg)` (social.c:9569);
			//   глобал пишут 5093 код 1 (@13, network.c:38859) и 5092 (social.c:9492) — у порта это `PartyIndex` (его же шлёт 5098).
			//   Сервер поле НЕ читает: `sub_450FB0@0x450FB0` (GameServer.c:32576) копирует его в локальную и не использует,
			//   отряд отправителя ищет сам по сессии (`sub_44F470(this, a2)`), рассылка 5106 — по составу отряда (`sub_451210`).
			//   Прежняя ветка (В2-A, U5b) слала свой userNum и молчала без него — построена на неверном прочтении поля; в отряде
			//   номер всегда известен, поэтому гейт «номер неизвестен» здесь не нужен. Гейт «в отряде» — в `ChannelAllowed`.
			const UAsdaPartySubsystem* Party = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPartySubsystem>() : nullptr;
			const int32 PartyIdx = Party ? Party->GetPartyIndex() : -1;
			Opcode = 5105; bOk = Net->SendChatParty5105((uint16)(int16)PartyIdx, Text);   // __int16, как у эталона (−1 → 0xFFFF)
		}
		break;
	case 6:
		// ★ 27.09.2026 [ОРИГИНАЛ]: @2 i32 — charId (`*(_DWORD*)&Src[1] = …->userIdx`, chat.c:5667; `userIdx` = `char+1052`, миснейм).
		//   Прежде уходил userNum. Сервер поле не читает (`sub_403F50`) — поведение сервера то же.
		Opcode = 4120; bOk = Net->SendChatClan4120(Net->SelectedCharId, SelfName, Text); break;
	case 7:
		Opcode = 5088; bOk = Net->SendChatWhisper5088(0, WhisperTarget, Text); break;
	case 8:
		// ★ ИСПРАВЛЕНИЕ 12.10.2026 [ОРИГИНАЛ]: адресат 5088 вида 1 — ИМЯ ПАРТНЁРА из записи соулмейта (`sub_93859A(g_pSoulmateWnd)+63`,
		//   `ChatInput_SendWhisperReply@0xBA4020`, chat.c:2303-2311). Было: `WhisperTarget` (адресат ШЁПОТА, не Друга) — ветка была
		//   недостижима за заглушкой гейта. Стало: `UAsdaSoulmateSubsystem::GetPartnerName()`. Откат `asda.Chat.ChannelGateLegacy 1`.
		if (CVarChannelGateLegacy.GetValueOnGameThread() == 0)
		{
			const UAsdaSoulmateSubsystem* Soul = GI ? GI->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr;
			Opcode = 5088; bOk = Net->SendChatWhisper5088(1, Soul ? Soul->GetPartnerName() : FString(), Text);
		}
		else
		{
			Opcode = 5088; bOk = Net->SendChatWhisper5088(1, WhisperTarget, Text);
		}
		break;
	case 9:
	{
		// ★★★ 31.10.2026 [ОРИГИНАЛ + ПОРТ] (отзыв игроков «Can't use faction chat»): ЧАТ АРМИИ — `Clan_Send_SetNotice@0xCA3010` (6718).
		//   Прежде здесь был отказ «требует клан-подсистемы» — пакета 6718 в порту не было вовсе. Тело собирает бой
		//   (`UAsdaCampWarBattleSubsystem::SendArmyChat6718`, серверная раскладка 108 б — разбор там). Эхо — 6719 от сервера.
		UAsdaCampWarBattleSubsystem* Bt = GI ? GI->GetSubsystem<UAsdaCampWarBattleSubsystem>() : nullptr;
		Opcode = 6718; bOk = Bt && Bt->SendArmyChat6718(Text);
		break;
	}
	default:
		UE_LOG(LogAsdaChatWin, Warning, TEXT("[чат] режим %d: отправителя нет — не отправлено"), InputState);
		return false;
	}
	// ★ Рантайм-лог маршрутизации — видно, что канал реально доезжает до выбора опкода.
	UGameInstance* GI2 = GetGameInstance();
	UAsdaTextSubsystem* Txt2 = GI2 ? GI2->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	const int32 Slot = CategoryToSlot(InputState);
	static const int32 kNameLoc[10] = { 5316, 5317, 5318, 5319, 5320, 5321, 5322, 5323, 5325, 5337 };
	const FString ChanName = (Txt2 && InputState >= 0 && InputState <= 9) ? Txt2->GetTextById(kNameLoc[InputState]) : FString();
	UE_LOG(LogAsdaChatWin, Log,
		TEXT("[чат] отправка: режим=%d канал='%s' опкод=%d адресат='%s' длина=%d → %s"),
		InputState, *ChanName, Opcode, *WhisperTarget, Text.Len(), bOk ? TEXT("OK") : TEXT("ОШИБКА"));
	(void)Slot;
	return bOk;
}

// ★★★ ПРОВЕРКИ ДОСТУПНОСТИ КАНАЛОВ — 1:1 `chatInput_OnCommandKey` (chat.c:3860-3970).
//   Отказы в эталоне идут через `CChat_AddTokenizedLines(chat, текст, -65536, 10000, 0)`,
//   то есть КРАСНОЙ строкой на 10 секунд в СИСТЕМНЫЙ ЖУРНАЛ (дорожка 0), а не в ленту чата.
//   Исключение — каналы 0/1/2 во время боя: loc 10222 через CItem_BuildTooltipText, то есть в
//   ВЕРХНЕЕ окно WARMESSAGE_WIN.
//
//     режим  условие эталона                                     отказ
//       0/1/2  флаг CChat+1696 == 1 (бой/кампвар)                    loc 10222 в верхнее окно
//       3      `pLocalPlayer->gap_43B[10]` — ★ ФЛАГ «ЕСТЬ ПРОФЕССИЯ»  loc 7004
//       4      проверок НЕТ                                        —
//       5/41   `Party_IsInParty(g_CharManager)` (12.10.2026: живой)       loc 6928
//       6      `LocalPlayer->gap_93A[32]` (int16 clanId) >= 0            loc 7071
//       7      внутри `ChatInput_ParseWhisper(…, 2)`: адресата нет          loc 6943 строкой ленты кат. 5 (12.10.2026)
//       8      `*(DWORD*)sub_93859A(g_pSoulmateWnd) >= 0`                loc 6929; затем пара/имя → 6929 лентой кат. 101
//       9      `Clan_Send_SetNotice`                                     —
//   ★ 12.10.2026 [ОРИГИНАЛ]: ВСЕ эти гейты у эталона стоят ДО антифлуда — `chatAntiFloodCheck` зовут только сами отправители
//   (`ChatInput_SendChannel/SendNormal/SendShout/SendParty/SendWhisperReply/ParseWhisper`), а клан (`chatSendMessage`) и армия
//   (`Clan_Send_SetNotice`) его не зовут вовсе. Порядок в порту — `SubmitInput` (ключ отката `asda.Chat.ChannelGateLegacy`).
//   ★ ПОРОГ УРОВНЯ 10 ДЛЯ ГЛОБАЛЬНОГО — МИФ: в коде проверяется байт-флаг «есть
//   профессия» (тот же флаг читают item_inventory.c:3365 и network.c:21838), а сам текст
//   loc 7004 говорит то же: «Глобальный чат доступен только персонажам, у которых есть
//   профессия.» Профессию в игре берут на 10 уровне — отсюда и впечатление про уровень.
// ★ Дедуп №2, В1-B (U10c, 11.10.2026): строки порта 9904xx/9905xx в членах окна (отказ канала, причины кулдауна) берутся через
//   текстовую подсистему (`AsdaNotice::Loc` = `UAsdaTextSubsystem::GetTextById` = та же `FAsdaLoginData::Loc` + удержание
//   DT_GameText на сессию). Статические `SlotShortName` / `RenameChannelText` / `WideChannelName` экземпляра игры не имеют —
//   оставлены на `FAsdaLoginData::Loc` (перевод требует смены сигнатуры — отчёт `wave1_B.md`).
bool UAsdaChatWindowSubsystem::ChannelAllowed(int32 State, FString& OutDenyText, bool& bOutTopWindow, int32* OutFeedCategory) const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка — нет строки → «[#id]».
	auto Loc = [&](int32 Id) { const FString S = Txt ? Txt->GetTextById(Id) : FString(); return S.IsEmpty() ? FAsdaLang::MissingTag(Id) : S; };
	bOutTopWindow = false;
	if (OutFeedCategory) { *OutFeedCategory = -1; }   // по умолчанию отказ — в системный журнал (`CChat_AddTokenizedLines`)
	const bool bGateLegacy = CVarChannelGateLegacy.GetValueOnGameThread() != 0;   // ключ отката порядка проверок каналов

	switch (State)
	{
	case 0: case 1: case 2:
		// ★ 31.10.2026 [ОРИГИНАЛ] (chat.c / G:226348-226352): флаг «бой» CChat+0x6A0 (`bChannelPrefixMode`, ставит
		//   `toggleField105Swap` из `campWarWnd_ToggleVisible`) — однокнопочное окно loc 10222 «Во время боя чат будет отключен.»,
		//   поле очищается, пакета нет. Прежде флаг не проверялся (комментарий «в порт не заведён» устарел — флаг есть с 30.10).
		if (bChannelPrefixMode)
		{
			OutDenyText = Loc(10222);
			if (OutFeedCategory) { *OutFeedCategory = kDenyMsgBox; }
			return false;
		}
		return true;
	case 3:
	{
		// ★ НАСТОЯЩАЯ проверка эталона: байт «есть профессия» (`player->gap_43B[10]`, пишется из
		//   `charApplyFullStatusPacket` body+17 — см. UAsdaNetSubsystem::SelfJob). Порога уровня
		//   в коде НЕТ, приближение по уровню убрано.
		const UAsdaNetSubsystem* NetJ = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (NetJ && NetJ->SelfJob == 0)
		{
			OutDenyText = Loc(7004);
			return false;
		}
		if (!NetJ || NetJ->SelfJob < 0)
		{
			// Пакет полного статуса ещё не заведён в порт ⇒ значение неизвестно. Не блокируем
			//   (сервер откажет сам), но честно пишем в лог, что проверка неактивна.
			UE_LOG(LogAsdaChatWin, Verbose,
				TEXT("[чат] Глобальный: профессия неизвестна (пакет полного статуса не заведён) — проверка пропущена"));
		}
		return true;
	}
	case 4:
		return true;                                   // эталон ничего не проверяет
	case 5: case 41:
	{
		// ★ 12.10.2026 [ОРИГИНАЛ]: `if (Party_IsInParty(g_CharManager)) ChatInput_SendParty(); else loc 6928 красной строкой
		//   на 10 с в системный журнал` (chat.c:3914-3925). `Party_IsInParty@0x9CCEF0` = число участников отряда (social.c:513) —
		//   у порта `UAsdaPartySubsystem::IsInParty()` (ставят 5093/5097/5107, снимают 5099/5101/5103 и сброс сессии).
		//   Прежде здесь стояла заглушка «отряд не портирован» (всегда 6928) — отряд портирован, заглушка снята.
		//   Ключ отката `asda.Chat.PartyChatLegacy 1` — прежняя заглушка.
		const UAsdaPartySubsystem* Party = GI ? GI->GetSubsystem<UAsdaPartySubsystem>() : nullptr;
		if (CVarPartyChatLegacy.GetValueOnGameThread() == 0 && Party && Party->IsInParty())
		{
			return true;
		}
		OutDenyText = Loc(6928);
		return false;
	}
	case 6:
		// Эталон: `LocalPlayer->gap_93A[32]` (int16 clanId) >= 0. У нас это есть — SelfClanId.
		if (!WS || WS->GetSelfClanId() < 0)
		{
			OutDenyText = Loc(7071);
			return false;
		}
		return true;
	case 7:
		// Адресат шёпота задаётся командой `/W имя`, меню 80000 (ЭТАП 3) или CHAT ANSWER.
		//   Проверка «в сети ли адресат» в оригинале ДЕЛАЕТСЯ СЕРВЕРОМ (клиент шлёт 5088
		//   и получает ответ-отказ); локально проверяется только длина имени (> 20 б → loc 6942).
		if (WhisperTarget.IsEmpty())
		{
			// ★ ИСПРАВЛЕНИЕ 12.10.2026 [ОРИГИНАЛ] (loc по обработчику): Enter в режиме 7 = `ChatInput_ParseWhisper(chat, 2)`; ветка
			//   `case 2` без адресата (оба поля +2532/+2552 пусты) печатает loc **6943** «Никто не услышит твой шепот.» строкой ЛЕНТЫ
			//   `Chat_AddChatLine(chat, 5, …, −10240)` (chat.c:2903-2909) — ДО антифлуда, пакета нет. Было: loc 6942 («Неправильно
			//   указано имя» — это ветки `case 0` «имя > 20 б» и `case 1`) красной строкой в системный журнал. Откат — ключ.
			if (bGateLegacy)
			{
				OutDenyText = Loc(6942);
				return false;
			}
			OutDenyText = Loc(6943);
			if (OutFeedCategory) { *OutFeedCategory = 5; }
			return false;
		}
		return true;
	case 8:
	{
		if (bGateLegacy)
		{
			// Прежняя заглушка порта: «соулмейт не портирован» — всегда отказ 6929 в журнал.
			OutDenyText = Loc(6929);
			return false;
		}
		// ★ ИСПРАВЛЕНИЕ 12.10.2026 [ОРИГИНАЛ]: заглушка «соулмейт не портирован» снята — подсистема Друга есть
		//   (`UAsdaSoulmateSubsystem`). Гейты эталона по порядку, все ДО антифлуда:
		//   1) `chatInput_OnCommandKey` case 8 (chat.c:3956-3964): `*(DWORD*)sub_93859A(g_pSoulmateWnd) >= 0` — слот Друга
		//      (у порта `GetFriendSlot()`, −1 = Друга нет), иначе loc 6929 КРАСНОЙ строкой на 10 с в системный журнал;
		//   2) `ChatInput_SendWhisperReply@0xBA4020` (chat.c:2294-2302): признак пары `gap_8B4[6]` (`HasSoulmatePair()`), иначе
		//      loc 6929 строкой ЛЕНТЫ `Chat_AddChatLine(chat, 101, …, −128)`;
		//   3) там же (chat.c:2303-2305/2365-2369): имя партнёра `rec+63` пусто → та же строка ленты кат. 101.
		//   Только после них — мат-фильтр и `chatAntiFloodCheck` (chat.c:2324/2349). Было: всегда отказ 6929 в журнал.
		const UAsdaSoulmateSubsystem* Soul = GI ? GI->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr;
		if (!Soul || Soul->GetFriendSlot() < 0)
		{
			OutDenyText = Loc(6929);
			return false;
		}
		if (!Soul->HasSoulmatePair() || Soul->GetPartnerName().IsEmpty())
		{
			OutDenyText = Loc(6929);
			if (OutFeedCategory) { *OutFeedCategory = 101; }
			return false;
		}
		return true;
	}
	case 9:
		// ★ ВНЕ БИТВЫ КАНАЛ АРМИИ НЕДОСТУПЕН (требование юзера).
		if (!bChannelPrefixMode)
		{
			OutDenyText = AsdaNotice::Loc(GetGameInstance(), 990455);   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): «Канал армии доступен только во время битвы армий.»
			return false;
		}
		// ★★★ 31.10.2026 [ОРИГИНАЛ]: в бою канал армии ОТКРЫТ — эталон для состояния 9 проверок не делает (chat.c:3966-3971), сразу
		//   `Clan_Send_SetNotice(g_pCampWarClan, текст)`; отправитель — `UAsdaCampWarBattleSubsystem::SendArmyChat6718`. Прежде здесь
		//   стояла заглушка «кампвар не портирован» (loc 990456) — канал не работал вовсе (жалоба игроков 30.10).
		return true;
	default:
		return true;
	}
}

// Enter — разбор и отправка. Порядок 1:1 эталона `chatInput_OnCommandKey` (chat.c:3851):
//   1) слэш-команда → смена режима; 2) фильтр мата; 3) антифлуд для каналов 0/1/2;
//   4) проверка доступности канала + отправка; 5) учёт повтора; поле очищается.
void UAsdaChatWindowSubsystem::SubmitInput(const FString& Raw)
{
	FString Text = Raw;
	ParseSlashCommand(Text);                    // 1. команда меняет режим и вырезается
	Text.TrimStartAndEndInline();

	if (InputEdit) { InputEdit->SetText(FText::GetEmpty()); }
	if (Text.IsEmpty())
	{
		// Пустая строка после команды — только сменили канал (эталон: strcmp с приглашением).
		if (InputEdit) { InputEdit->SetKeyboardFocus(); }
		return;
	}
	// ★ Лимит — `ctrlEditSetTextLength(chatNewEdit, 68)` (chat.c:829), считается в БАЙТАХ.
	//   У нас это ПАРАМЕТР `asda.Chat.InputMaxChars` (умолчание 136, эталон 68) — см. GetInputMaxChars.
	//   Основной стоп стоит в OnInputChanged (аналог EM_LIMITTEXT); здесь — страховка для путей
	//   без поля ввода (asda.ChatWin.Say, слэш-команды).
	//   ★ Для режима 9 «Армия» лимит СВОЙ — 80 (опкод 6718: фикс. длина + MD5, патч P14 его не трогает).
	{
		const int32 MaxLen = GetActiveInputMaxChars();
		if (!IsWithinActiveInputLimit(Text))   // [ПОРТ] 08.10.2026: байты провода, не символы (С3: с префиксом «Друг»)
		{
			UE_LOG(LogAsdaChatWin, Warning,
				TEXT("[чат-ввод] текст %d символов обрезан до лимита %d (режим %d; asda.Chat.InputMaxChars=%d)"),
				Text.Len(), MaxLen, InputState, GetInputMaxChars());
			Text = ClampToActiveInputLimit(Text);   // по границе символа
		}
	}

	// 2. Мат-фильтр. ★ Эталон при срабатывании НЕ ОТПРАВЛЯЕТ пакет вовсе,
	//   а печатает ЛОКАЛЬНО loc 9005 вместо текста (chat.c:2318-2322 и ещё 12 таких мест).
	if (IsCursed(Text))
	{
		UGameInstance* GI = GetGameInstance();
		UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
		const FString M = Txt ? Txt->GetTextById(9005) : FString();
		AddChatLineRaw(0, M.IsEmpty() ? FString(TEXT("[#9005]")) : M);   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка
		// ★ ОТСТУПЛЕНИЕ (требование юзера): в эталоне таймера при мате НЕТ —
		//   только подмена текста. Длительность вынесена в константу kCurseCooldownSec.
		StartCooldown(kCurseCooldownSec, AsdaNotice::Loc(GetGameInstance(), 990503));   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): причина видна в подписи 2619 — «запрещённое слово»
		if (InputEdit) { InputEdit->SetKeyboardFocus(); }
		return;
	}

	// 2б. ★ 12.10.2026 [ОРИГИНАЛ]: у ОТРЯДА гейт «в отряде» стоит ДО антифлуда — эталон проверяет `Party_IsInParty`
	//   (chat.c:3917), и только внутри `ChatInput_SendParty` зовёт `chatAntiFloodCheck` (chat.c:3223/3237). Вне отряда игрок
	//   видит ровно loc 6928, а счётчики антифлуда не растут. Отказ печатает `SendCurrentChannel` (одна точка отказа канала).
	// ★ ИСПРАВЛЕНИЕ 12.10.2026 [ОРИГИНАЛ] (отчёт `Reports/owner_1012/party_chat.md`, «Порядок проверок каналов»): так же —
	//   у ВСЕХ каналов. Эталон `chatInput_OnCommandKey` case 2609 (chat.c:3858-3975) сперва проверяет допустимость канала
	//   (3 — профессия → 7004; 6 — клан → 7071; 7 — адресат шёпота → 6943; 8 — Друг → 6929; 0/1/2 — кулдаун `v37[112]`), и
	//   только отправитель канала внутри себя зовёт `chatAntiFloodCheck`. Было: антифлуд первым для любого канала — отказ канала
	//   тратил лимит (быстрые/повторы, время последней отправки, последний текст), а при активном блоке игрок видел 7032 вместо
	//   причины отказа. Стало: отказ канала — ровно его нотис, счётчики антифлуда не трогаются. Откат `asda.Chat.ChannelGateLegacy 1`.
	const bool bGateLegacy = CVarChannelGateLegacy.GetValueOnGameThread() != 0;
	if (!bGateLegacy || InputState == 5 || InputState == 41)
	{
		FString PreDeny; bool bPreTop = false;
		if (!ChannelAllowed(InputState, PreDeny, bPreTop))
		{
			SendCurrentChannel(Text);               // → нотис отказа обработчика (журнал или строка ленты), пакета нет
			if (InputEdit) { InputEdit->SetKeyboardFocus(); }
			return;
		}
	}

	// Кулдаун «широких» каналов 0/1/2 — эталон проверяет `!v37[112]` (chat.c:3878) ДО вызова `ChatInput_SendChannel`, то есть
	//   ДО антифлуда. Общая проверка для обоих порядков: в прежнем (ключ отката) она стоит после антифлуда, как было.
	const bool bWide = (InputState >= 0 && InputState < kWideChannelCount);
	auto WideCooldownBlocks = [&]() -> bool
	{
		if (!(bWide && ChannelCooldownLeft[InputState] > 0.f)) { return false; }
		UE_LOG(LogAsdaChatWin, Log,
			TEXT("[чат] отправка пропущена: идёт кулдаун канала %d «%s», осталось %.0f с ")
			TEXT("| прочие каналы: 0=%.0f 1=%.0f 2=%.0f (★ отступление: в игре счётчик один общий)"),
			InputState, *WideChannelName(InputState), ChannelCooldownLeft[InputState],
			ChannelCooldownLeft[0], ChannelCooldownLeft[1], ChannelCooldownLeft[2]);
		if (InputEdit) { InputEdit->SetKeyboardFocus(); }
		return true;
	};
	if (!bGateLegacy && WideCooldownBlocks()) { return; }

	// 3. антифлуд. ★ 12.10.2026 [ОРИГИНАЛ]: клан (6) и армия (9) его не проходят — у эталона их отправители `chatSendMessage`
	//   (chat.c:5639) и `Clan_Send_SetNotice` (social.c) `chatAntiFloodCheck` не зовут: ни проверки, ни учёта текста.
	const bool bFloodApplies = !ChannelSkipsAntiFlood(InputState);
	if (bFloodApplies && !AntiFloodAllows()) { return; }

	// ★★★ ЖИВОЙ ТЕСТ 2026-08-05: «в „По всем каналам“ сообщение не уходит, сразу кулдаун».
	//   Причина — ПОРЯДОК. Кулдаун-полоса взводилась ВНУТРИ AntiFloodAllows, то есть ДО отправки,
	//   и попутно `ApplyCooldownBar` гасил поле ввода (`SetIsEnabled(false)`) — так что даже когда
	//   пакет уходил, поле «умирало» в тот же кадр и выглядело это как «не ушло».
	//   ЭТАЛОН (chat.c:3878-3882) делает строго наоборот:
	//       if ( strcmp(editText, приглашение) && !v37[112] )   // v37[112] — счётчик кулдауна
	//       { ChatInput_SendChannel(v37, v37[105]);  uiProgressBarSetVisible(v37, 1); }
	//   то есть: (1) пока счётчик НЕ нулевой — отправка вообще не вызывается; (2) полоса
	//   поднимается ТОЛЬКО ПОСЛЕ успешного `ChatInput_SendChannel`. Воспроизводим 1:1.
	// ★★★ КУЛДАУН «ШИРОКИХ» КАНАЛОВ — ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА (решение юзера 2026-08-05).
	//   ЭТАЛОН: счётчик — `CChat::stateFlags4`, ОДИН int в объекте чата, а не массив по каналам:
	//     • `uiProgressBarSetVisible` (ui_core.c:11804): `if (!*(this+112)) *(this+112) = 30;`
	//     • `uiProgressBarTickCountdown` (ui_core.c:11791): `--this->stateFlags4;` раз в секунду
	//       (`game_main.c:1438`), `CProgressBar_SetValue(bar, 30 − stateFlags4)`;
	//     • проверка перед отправкой (chat.c:3878): `if (strcmp(...) && !v37[112])` — БЕЗ индексации
	//       по каналу, общая для всей ветки `case 0u: case 1u: case 2u`.
	//   ⇒ в ИГРЕ отправил в «Торговлю» — 30 секунд молчат и «По всем каналам», и «Поиск отряда».
	//   ★ МЫ ДЕЛАЕМ ИНАЧЕ ПО РЕШЕНИЮ ЮЗЕРА: свой счётчик на каждый слот 0/1/2, блокируется ТОЛЬКО
	//   тот канал, чей таймер идёт. НЕ «чинить» обратно в общий — это осознанное отступление.
	if (bGateLegacy && WideCooldownBlocks()) { return; }   // прежний порядок порта (ключ отката): кулдаун после антифлуда
	const bool bSent = SendCurrentChannel(Text);   // 4. проверка канала + отправка
	// 4б. Полоса — РОВНО тот же сайт, что в эталоне: после успешной отправки в каналы 0/1/2.
	//   Отличие от эталона только в адресате счётчика: взводим таймер ИМЕННО ЭТОГО канала.
	if (bSent && bWide) { StartCooldown(kFloodSeconds, AsdaNotice::Loc(GetGameInstance(), 990501), InputState); }   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): «кулдаун канала»
	if (bFloodApplies) { NoteSentText(Text); }  // 5. учёт повтора для антифлуда (клан/армия у эталона текст не учитывают)
	// ★ ИСТОРИИ ВВОДА СТРЕЛКАМИ В ОРИГИНАЛЕ НЕТ (VK_UP/VK_DOWN в срезе отсутствуют,
	//   а `chatHistoryAddLine` chat.c:5627 — это список ИГНОРА из меню 80002) — не выдумываем.
	if (InputEdit) { InputEdit->SetKeyboardFocus(); }
}

// ★ МАТ-ФИЛЬТР — КАРКАС. Эталон `Chat_MatchBannedWord @0xBB2830` (chat.c:5166) зовётся ДО
//   отправки; таблица — `<CurrentDir>\system\CurseTable.bin` (`Chat_LoadCurseTable @0xBB2710`,
//   chat.c:5137): `DWORD size` + size байт, КАЖДЫЙ БАЙТ ДЕКРЕМЕНТИРУЕТСЯ НА 1 при загрузке.
//   ★★★ ТАБЛИЦА НАМЕРЕННО НЕ ЗАПОЛНЕНА (требование юзера): фильтрация фактически
//   ВЫКЛЮЧЕНА. ВКЛЮЧАЕТСЯ ЗАПОЛНЕНИЕМ массива `CurseWords`.
bool UAsdaChatWindowSubsystem::IsCursed(const FString& In) const
{
	if (CurseWords.Num() == 0) { return false; }
	const FString L = In.ToLower();
	for (const FString& W : CurseWords)
	{
		if (!W.IsEmpty() && L.Contains(W, ESearchCase::IgnoreCase)) { return true; }
	}
	return false;
}

// Антифлуд `chatAntiFloodCheck @0xBB4950` (chat.c:5536) + КУЛДАУН каналов 0/1/2 (полоса 2618).
bool UAsdaChatWindowSubsystem::AntiFloodAllows()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка — нет строки → «[#id]».
	auto Say = [&](int32 LocId)
	{
		const FString M = Txt ? Txt->GetTextById(LocId) : FString();
		AddChatLineRaw(6, M.IsEmpty() ? FAsdaLang::MissingTag(LocId) : M, FColor(255, 0, 0, 255));
	};
	if (bFloodBlocked)                                   // блок активен → loc 7032
	{
		Say(7032);
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - FloodLastSend <= 1.0) { ++FloodFastCount; } else { FloodFastCount = 0; }
	if (Now - FloodRepeatWindow > 60.0) { FloodRepeatWindow = Now; FloodRepeatCount = 0; FloodLastText.Empty(); }
	FloodLastSend = Now;
	if (FloodFastCount > kFloodFastLimit)                // 5 быстрых → блок 60 с, loc 7030
	{
		bFloodBlocked = true; FloodBlockLeft = 60.f;
		StartCooldown(60.f, AsdaNotice::Loc(GetGameInstance(), 990502));      // ★ визуализация — отступление; [ПОРТ] 09.10.2026 (мультиязычность, этап 3): «антифлуд»
		Say(7030);
		return false;
	}
	if (FloodRepeatCount > kFloodRepeatLimit)            // 10 повторов → блок 600 с, loc 7031
	{
		bFloodBlocked = true; FloodBlockLeft = 600.f;
		StartCooldown(600.f, AsdaNotice::Loc(GetGameInstance(), 990504));       // ★ визуализация — отступление; [ПОРТ] 09.10.2026 (мультиязычность, этап 3): «повторы»
		Say(7031);
		return false;
	}
	// ★ КУЛДАУН ЗДЕСЬ БОЛЬШЕ НЕ ВЗВОДИТСЯ И НЕ ПРОВЕРЯЕТСЯ. Это ДРУГАЯ механика (эталон
	//   chat.c:3878/3881), и место ей — в SubmitInput ПОСЛЕ отправки. Раньше она стояла тут:
	//   полоса поднималась до `SendCurrentChannel`, поле ввода гасилось в тот же кадр, и любой
	//   канал 0/1/2 выглядел как «сообщение не уходит». Антифлуд (60/600 с) — механика чата
	//   целиком и живёт здесь, кулдаун каналов — нет.
	return true;
}

// Учёт повтора текста (эталон хранит буфер прошлого сообщения).
void UAsdaChatWindowSubsystem::NoteSentText(const FString& Text)
{
	if (Text == FloodLastText) { ++FloodRepeatCount; } else { FloodRepeatCount = 0; FloodLastText = Text; }
}

// ★ 12.10.2026 [ОРИГИНАЛ]: каналы без антифлуда у эталона — клан 6 (`chatSendMessage@0xBCAA00`, chat.c:5639-5690) и армия 9
//   (`Clan_Send_SetNotice@0xCA3010`, social.c): вызова `chatAntiFloodCheck@0xBB4950` в них нет (все 15 вызовов — в отправителях
//   chat.c:2324…3237). Ключ отката `asda.Chat.ChannelGateLegacy 1` — прежнее «антифлуд для всех».
bool UAsdaChatWindowSubsystem::ChannelSkipsAntiFlood(int32 State) const
{
	if (CVarChannelGateLegacy.GetValueOnGameThread() != 0) { return false; }
	return State == 6 || State == 9;
}

// ★ 12.10.2026: только самотест `asda.Chat.ChannelGateSelfTest` — чистое состояние антифлуда перед шагом (прогон шлёт пакеты
//   подряд быстрее секунды: без сброса шестая отправка сама включила бы блок 7030) или блок 60 с для ветки «блок активен».
void UAsdaChatWindowSubsystem::DebugResetAntiFlood(bool bBlocked)
{
	FloodFastCount = 0; FloodRepeatCount = 0; FloodLastSend = 0.0; FloodRepeatWindow = 0.0; FloodLastText.Empty();
	bFloodBlocked = bBlocked; FloodBlockLeft = bBlocked ? 60.f : 0.f;
	for (int32 i = 0; i < kWideChannelCount; ++i) { ChannelCooldownLeft[i] = 0.f; }
	CooldownLeft = 0.f;
	ApplyCooldownBar();
}

void UAsdaChatWindowSubsystem::TickAntiFlood(float Dt)
{
	// Снятие блокировки антифлуда (эталон: WM_TIMER 0x1C → `chatAntiFloodReset` + loc 7033).
	if (bFloodBlocked)
	{
		FloodBlockLeft -= Dt;
		if (FloodBlockLeft <= 0.f)
		{
			bFloodBlocked = false; FloodFastCount = 0; FloodRepeatCount = 0;
			UGameInstance* GI = GetGameInstance();
			UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
			const FString M = Txt ? Txt->GetTextById(7033) : FString();
			AddChatLineRaw(6, M.IsEmpty() ? FString(TEXT("[#7033]")) : M, FColor(255, 255, 255, 255));   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка
		}
	}
	// ★★★ ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА: тикают ТРИ независимых счётчика «широких» каналов
	//   (в игре тикает один общий `stateFlags4`) плюс общий счётчик наших отступлений.
	bool bWas = CooldownLeft > 0.f;
	for (int32 i = 0; i < kWideChannelCount; ++i) { bWas |= (ChannelCooldownLeft[i] > 0.f); }

	for (int32 i = 0; i < kWideChannelCount; ++i)
	{
		if (ChannelCooldownLeft[i] <= 0.f) { continue; }
		ChannelCooldownLeft[i] = FMath::Max(0.f, ChannelCooldownLeft[i] - Dt);
		if (ChannelCooldownLeft[i] <= 0.f)
		{
			UE_LOG(LogAsdaChatWin, Log,
				TEXT("[чат-кулдаун] канал %d «%s» свободен | осталось у прочих: 0=%.0f 1=%.0f 2=%.0f"),
				i, *WideChannelName(i), ChannelCooldownLeft[0], ChannelCooldownLeft[1], ChannelCooldownLeft[2]);
			ChannelCooldownReason[i].Empty();
		}
	}
	if (CooldownLeft > 0.f)
	{
		CooldownLeft = FMath::Max(0.f, CooldownLeft - Dt);
		if (CooldownLeft <= 0.f)
		{
			UE_LOG(LogAsdaChatWin, Log, TEXT("[чат-кулдаун] снят (была причина: %s)"), *CooldownReason);
			CooldownReason.Empty();
		}
	}

	bool bNow = CooldownLeft > 0.f;
	for (int32 i = 0; i < kWideChannelCount; ++i) { bNow |= (ChannelCooldownLeft[i] > 0.f); }
	// Перерисовываем, пока хоть один счётчик идёт, и один раз на переходе «идёт → свободно»:
	//   полоса сама решит, показывать ли ЭТОТ канал (см. ApplyCooldownBar).
	if (bNow || bWas != bNow) { ApplyCooldownBar(); }
}

// ═══════ КУЛДАУН-ПОЛОСА В ПОЛЕ ВВОДА (2618 / 2619) ════════════════════
//
// ★★★ ЭТО И ЕСТЬ «красная полупрозрачная полоса в строке ввода». Доказательство:
//   • контрол 2618 `CHAT_NEW_Bar2` в JUI стоит на rect (128, 726, 336×20) — БАЙТ В БАЙТ тот
//     же прямоугольник, что и поле ввода 2609, то есть полоса рисуется ПОВЕРХ строки ввода;
//   • скин называется `Red_Bar_R` (`Type = ProgressBar`), и его план ES=0 имеет
//     `Diffuse = "32FF8B59"` — то есть красно-оранжевый FF8B59 с АЛЬФОЙ 0x32 (50/255 ≈ 20 %)
//     — именно «красная полупрозрачная»; второй план ES=1 чёрный FF000000;
//   • диапазон `CProgressBar_SetRange(bar, 1.0f, 32.0f)` (chat.c:845);
//   • тик — `uiProgressBarTickCountdown @0xBA29F0` (ui_core.c:11791), вызывается РАЗ В СЕКУНДУ
//     из `game_main.c:1438`: `--stateFlags4;  CProgressBar_SetValue(bar, 30 - stateFlags4)`
//     ⇒ значение РАСТЁТ 0 → 30, то есть заливка идёт СЛЕВА НАПРАВО — ровно как описал юзер;
//   • показ — `uiProgressBarSetVisible @0xBA2CA0` (ui_core.c:11804): при a2 == 1 ставит
//     `stateFlags4 = 30` (если был 0) и показывает ОБА контрола — бар 2618 и подпись 2619.
//
// ★ ГДЕ ОНА ВКЛЮЧАЕТСЯ В ЭТАЛОНЕ — РОВНО ОДИН САЙТ: chat.c:3881, сразу после
//   `ChatInput_SendChannel` для каналов 0/1/2. Гасится в тике и при смене канала (chat.c:1795).
//   ⇒ ★ ОТДЕЛЬНОГО ТАЙМЕРА-КУЛДАУНА НА МАТ В ЭТАЛОНЕ НЕТ: мат-фильтр только подменяет
//   текст на loc 9005 и не шлёт пакет. По просьбе юзера мы РАСШИРЯЕМ показ этой же
//   полосы на ЛЮБОЙ запрет отправки (антифлуд 60/600 с и срабатывание мат-фильтра) —
//   это ОТСТУПЛЕНИЕ, помечено здесь и в отчёте.
// Имена «широких» слотов — как в списке каналов 2608 (0/1/2).
// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): имена — строки DT_GameText на языке сессии (видны в подписи 2619).
FString UAsdaChatWindowSubsystem::WideChannelName(int32 Slot)
{
	switch (Slot)
	{
	case 0: return FAsdaLoginData::Loc(990458);   // «Все каналы» ★ переименовано по требованию юзера (было «По всем каналам»)
	case 1: return FAsdaLoginData::Loc(5415);     // «Поиск отряда»
	case 2: return FAsdaLoginData::Loc(5416).TrimStartAndEnd();   // «Торговля»
	default: return TEXT("-");                    // не широкий канал (только журнал)
	}
}

// ★★★ ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА (решение юзера 2026-08-05): Channel 0..2 взводит ОТДЕЛЬНЫЙ
//   таймер этого канала. В ИГРЕ счётчик один общий (`CChat::stateFlags4`) — не «чинить» обратно.
void UAsdaChatWindowSubsystem::StartCooldown(float Seconds, const FString& Reason, int32 Channel)
{
	if (Seconds <= 0.f) { return; }
	if (Channel >= 0 && Channel < kWideChannelCount)
	{
		ChannelCooldownTotal[Channel] = Seconds;
		ChannelCooldownLeft[Channel] = Seconds;
		ChannelCooldownReason[Channel] = Reason;
		UE_LOG(LogAsdaChatWin, Log,
			TEXT("[чат-кулдаун] старт %.0f с на канале %d «%s», причина: %s ")
			TEXT("| таймеры каналов: 0=%.0f 1=%.0f 2=%.0f (★ отступление: в игре счётчик один общий)"),
			Seconds, Channel, *WideChannelName(Channel), *Reason,
			ChannelCooldownLeft[0], ChannelCooldownLeft[1], ChannelCooldownLeft[2]);
	}
	else
	{
		CooldownTotal = Seconds;
		CooldownLeft = Seconds;
		CooldownReason = Reason;
		UE_LOG(LogAsdaChatWin, Log,
			TEXT("[чат-кулдаун] старт %.0f с (общий, вне каналов), причина: %s"), Seconds, *Reason);
	}
	ApplyCooldownBar();
}

// ★★★ ЖИВОЙ ТЕСТ 2026-08-05: «полосы НЕТ». Причина — ДВЕ, обе теперь устранены.
//   (1) РИСОВАЛСЯ НЕ ТОТ ПЛАН. Закон скинов ProgressBar (MEMANTO ui-progressbar-animate-existing):
//       **ES0 = ПОДЛОЖКА, ES1 = ЗАЛИВКА**. У скина `Red_Bar_R` (JUISKIN.ini:1501-1546):
//         ES0  Rect "0 0 30 5"  Diffuse 32FF8B59  UV 0.469 0.039 … 0.473 0.043  (N_01.tga)
//         ES1  Rect "0 0 20 5"  Diffuse FF000000  UV 0.420 0.088 … 0.430 0.098  (N_01.tga)
//       То есть КРАСНО-ОРАНЖЕВАЯ ПОЛУПРОЗРАЧНАЯ ПЛИТА 32FF8B59 — это подложка НА ВСЮ ШИРИНУ,
//       она и есть «красная полоса», которую видит игрок. Порт же рисовал ES0 шириной
//       `kEditW × Frac`, а Frac стартует с НУЛЯ ⇒ в первый момент ширина 0 и на экране НИЧЕГО
//       (а дальше — 20 % альфы поверх тёмного поля, что тоже почти не видно). ES1 (чёрная
//       заливка) была принудительно скрыта, то есть индикатора прогресса не было вовсе.
//   (2) ДВОЙНОЕ УМНОЖЕНИЕ АЛЬФЫ. Импортёр уже кладёт Diffuse скина в тинт кисти плана; поверх
//       ставился ещё и `SetColorAndOpacity(32FF8B59)` ⇒ 0.196 × 0.196 ≈ 0.038 — прозрачнее
//       стекла. Теперь цвет задаётся РОВНО ОДИН РАЗ (в кисти), а ColorAndOpacity = белый.
//   Геометрия обоих планов — rect контрола 2618 (128, 726, 336×20), он же rect поля ввода 2609
//   (`CHAT_NEW_Edit2`), см. дамп WBP: оба узла (L127 T138 R336 B20) в группе SizeWin.
void UAsdaChatWindowSubsystem::ApplyCooldownBar()
{
	if (!ChatWidget) { return; }
	// ★ ЭТАЛОН ПРЯЧЕТ ПОЛОСУ ВНЕ КАНАЛОВ 0/1/2 (chat.c:1785-1796: при смене режима бар сначала
	//   гасится, и показывается снова только в ветке «широких» каналов, если счётчик > 0).
	//   Таймер при этом продолжает идти — вернулись в канал, полоса снова видна.
	const bool bWideMode = (InputState >= 0 && InputState < kWideChannelCount);

	// ★★★ ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА (решение юзера 2026-08-05): полоса и подпись показывают
	//   таймер ТОЛЬКО ТЕКУЩЕГО выбранного «широкого» канала. Идёт кулдаун «Торговли», игрок
	//   переключился на «Поиск отряда» — полосы нет; вернулся в «Торговлю» — полоса с корректным
	//   остатком (таймер всё это время тикал). В ИГРЕ счётчик один общий — не «чинить» обратно.
	//   Приоритет у общего кулдауна НАШИХ отступлений (антифлуд/мат): он запрещает чат целиком
	//   и виден в любом режиме, иначе игрок не поймёт, почему чат молчит.
	float ShowLeft = 0.f;
	float ShowTotal = 0.f;
	FString ShowReason;
	int32 ShowChannel = INDEX_NONE;
	if (CooldownLeft > 0.f)
	{
		ShowLeft = CooldownLeft; ShowTotal = CooldownTotal; ShowReason = CooldownReason;
	}
	else if (bWideMode && ChannelCooldownLeft[InputState] > 0.f)
	{
		ShowChannel = InputState;
		ShowLeft  = ChannelCooldownLeft[InputState];
		ShowTotal = ChannelCooldownTotal[InputState];
		ShowReason = ChannelCooldownReason[InputState].IsEmpty()
			? AsdaNotice::Loc(GetGameInstance(), 990501) : ChannelCooldownReason[InputState];   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): «кулдаун канала»
	}
	const bool bShow = ShowLeft > 0.f;

	// Доля ПРОШЕДШЕГО времени: эталонный тик пишет value = 30 − stateFlags4, то есть значение
	//   растёт 0 → 30 ⇒ заливка идёт СЛЕВА НАПРАВО.
	const float Frac = (bShow && ShowTotal > 0.f)
		? FMath::Clamp(1.f - ShowLeft / ShowTotal, 0.f, 1.f) : 0.f;

	// ★★★ ГОРИЗОНТАЛЬНОЕ РАСТЯГИВАНИЕ (2026-08-05): полоса 2618 лежит РОВНО НА ПОЛЕ ВВОДА 2609
	//   (оба узла имеют rect 128,726,336×20 = kEditX/kEditW), а поле в BuildLayoutTable тянется на
	//   всю надбавку ширины `kEditW + DW`. Значит и полная длина полосы — `kEditW + DW`, иначе при
	//   растянутом окне заливка кулдауна обрывалась бы на авторских 336 px посреди поля.
	const float BarFullW = kEditW + FMath::Max(0.f, BaseWidth() - kWinW);

	// Единый способ настроить план: кисть из атласа N_01 с UV скина + тинт (sRGB→linear) + слот.
	auto SetupPlane = [&](const TCHAR* NodeName, const TCHAR* Hex, const FBox2f& UV,
	                      float WidthPx, int32 Z) -> UImage*
	{
		UImage* Img = Cast<UImage>(ChatWidget->GetWidgetFromName(FName(NodeName)));
		if (!Img) { return nullptr; }
		Img->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (!bShow) { return Img; }
		FSlateBrush Br = Img->GetBrush();
		if (!Br.GetResourceObject())
		{
			// План импортёра без ресурса — берём атлас скина у менеджера (ImageFileName = "N_01.tga").
			// ★ Аудит №3 (A8/B8): `n_01` грузился прямым `LoadObject` в четырёх местах порта — мимо
			//   кэша и refCount `UAsdaTextureSubsystem` (закон `resource-subsystems`).
			UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();
			if (UTexture2D* Tex = TexMgr ? TexMgr->LoadUITexture(FName(TEXT("n_01"))) : nullptr)
			{
				Br.SetResourceObject(Tex);
				Br.DrawAs = ESlateBrushDrawType::Image;
			}
		}
		Br.SetUVRegion(UV);
		Br.ImageSize = FVector2D(FMath::Max(1.f, WidthPx), kEditH);
		Br.TintColor = FSlateColor(AsdaSrgbHexToLinear(Hex));   // цвет задаём ТОЛЬКО здесь
		Img->SetBrush(Br);
		Img->SetColorAndOpacity(FLinearColor::White);           // …и не умножаем его второй раз
		if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(Img))
		{
			CS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
			CS->SetAlignment(FVector2D(0.f, 0.f));
			CS->SetOffsets(FMargin(kEditX - 1.f, 138.f, WidthPx, kEditH));   // локально в группе SizeWin
			CS->SetZOrder(Z);
		}
		return Img;
	};

	// ★★★ ЖИВОЙ ТЕСТ 2026-08-05 (второй заход): «полоса ЧЁРНАЯ, а должна быть полупрозрачная красная».
	//   РАЗБОР СКИНОВ ЦЕЛИКОМ (JUISKIN.ini, 38 контролов `Type = "ProgressBar"`):
	//     • у 36 из 38 `ES0 Diffuse = FFFFFFFF` (нейтральный, цвет берётся из текселя), а ЦВЕТ несёт
	//       ES1: HP_GreenBar FF58D206, MP_BlueBar FF0B63F4, EXP_Green_Bar FF87D92C, Orange_Bar FFFF6C00…
	//       ⇒ общий закон подтверждён: ES0 = подложка, ES1 = заливка;
	//     • ИСКЛЮЧЕНИЙ ровно два, и `Red_Bar_R` — одно из них: у него ЦВЕТ лежит в **ES0**
	//       (`32FF8B59`, альфа 0x32 ≈ 20 %), а ES1 — `FF000000`, чистый чёрный.
	//   ★ ЧТО ЭТО ЗНАЧИТ НА ЭКРАНЕ. `FF000000` — это не «цвет», это отсутствие цвета: умноженный
	//   на любой тексель он даёт чистый чёрный, а полоса лежит ПОВЕРХ поля ввода 2609 со скином
	//   **Black_EditBox**, то есть поверх и без того ЧЁРНОГО прямоугольника. Чёрный по чёрному
	//   игрок не видит вообще — видимой в этом скине является ТОЛЬКО красно-оранжевая
	//   полупрозрачная часть `32FF8B59`. Именно её и описывал юзер.
	//   ⇒ РИСУЕМ РОВНО ОДНУ ПЛИТУ — `32FF8B59`, ширина = `kEditW × Frac`, рост слева направо
	//   (эталонный тик `uiProgressBarTickCountdown`, ui_core.c:11791, пишет `value = 30 − stateFlags4`,
	//   то есть значение РАСТЁТ 0 → 30 при диапазоне 1…32 из `CProgressBar_SetRange`, chat.c:845).
	//   Чёрный план ES1 НЕ рисуем: на нашем фоне он выглядит как чёрная плашка (это и было на
	//   скриншоте), а в оригинале он сливается с `Black_EditBox` и никакой полосы не образует.
	UImage* Back = SetupPlane(TEXT("CHAT_NEW_Bar2_32_0"), TEXT("32FF8B59"),
		FBox2f(FVector2f(0.469f, 0.039f), FVector2f(0.473f, 0.043f)), BarFullW * Frac, 89);
	UImage* Fill = Back;
	// ★ Перенесено в ассет (15.09, разрешение владельца): чёрный план ES1 FF000000 `CHAT_NEW_Bar2_32_1`
	//   свёрнут в самом WBP и никем не показывается — код его больше не гасит (см. разбор выше).

	// Подпись 2619 — loc 5326, видна вместе с полосой (эталон показывает оба контрола).
	if (FloodText)
	{
		FloodText->SetVisibility(bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		if (bShow && !ShowReason.IsEmpty())
		{
			// ★ Отступление: в подписи указываем ИМЕННО ТОТ канал, чей таймер показан.
			const FString Label = (ShowChannel >= 0)
				? FString::Printf(TEXT("%s: %s  %.0f"), *ShowReason, *WideChannelName(ShowChannel), ShowLeft)
				: FString::Printf(TEXT("%s  %.0f"), *ShowReason, ShowLeft);
			FloodText->SetText(FText::FromString(Label));
		}
	}
	// ★ ПОЛЕ ВВОДА БОЛЬШЕ НЕ ГАСИМ. В эталоне оно остаётся живым: кулдаун лишь не даёт
	//   вызвать ChatInput_SendChannel (chat.c:3878), а печатать и менять канал можно.
	//   `SetIsEnabled(false)` здесь и создавал ощущение «чат умер после первого сообщения».
	if (InputEdit) { InputEdit->SetIsEnabled(true); }

	// Рантайм-доказательство: печатаем ФАКТИЧЕСКУЮ геометрию обоих планов раз в секунду.
	static double LastLog = 0.0;
	const double Now = FPlatformTime::Seconds();
	if (bShow && Now - LastLog > 1.0)
	{
		LastLog = Now;
		auto Geo = [](UWidget* W) -> FString
		{
			if (!W) { return TEXT("НЕТ УЗЛА"); }
			const FGeometry& G = W->GetCachedGeometry();
			const FVector2D Sz = G.GetLocalSize() * G.Scale;
			const FVector2D P = G.GetAbsolutePosition();
			return FString::Printf(TEXT("(%.0f,%.0f) %.0fx%.0f vis=%d"),
				P.X, P.Y, Sz.X, Sz.Y, (int32)W->GetVisibility());
		};
		const FLinearColor Lin = AsdaSrgbHexToLinear(TEXT("32FF8B59"));
		FLinearColor Act = FLinearColor::White;
		if (Back) { Act = Back->GetBrush().TintColor.GetSpecifiedColor(); }
		UE_LOG(LogAsdaChatWin, Log,
			TEXT("[чат-полоса] показан %s: осталось %.0f/%.0f с (доля %.2f) режим=%d ")
			TEXT("| таймеры каналов: 0=%.0f 1=%.0f 2=%.0f, общий=%.0f | видимая плита 32FF8B59 ")
			TEXT("linear(%.3f,%.3f,%.3f,a=%.3f) факт.тинт(%.3f,%.3f,%.3f,a=%.3f) | геометрия %s | ES1 FF000000 скрыт"),
			(ShowChannel >= 0) ? *WideChannelName(ShowChannel) : TEXT("общий кулдаун"),
			ShowLeft, ShowTotal, Frac, InputState,
			ChannelCooldownLeft[0], ChannelCooldownLeft[1], ChannelCooldownLeft[2], CooldownLeft,
			Lin.R, Lin.G, Lin.B, Lin.A, Act.R, Act.G, Act.B, Act.A, *Geo(Back));
		(void)Fill;
	}
}
