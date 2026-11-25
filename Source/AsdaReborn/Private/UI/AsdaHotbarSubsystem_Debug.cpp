// Copyright (c) 2026 @sitxovski (Discord)
// AsdaHotbarSubsystem_Debug.cpp — ★ R2 10.11.2026 (разрез AsdaHotbarSubsystem.cpp, дословный перенос): дампы, самотест эмоций, дев-команды GQuick*.
//   Общее для частей — в AsdaHotbarSubsystemInternal.h; шапка и реверс — в AsdaHotbarSubsystem.cpp.
#include "UI/AsdaHotbarSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaSoulmateSubsystem.h"    // ★ 06.11.2026: пара и «Друг в игре» — из карточки (`asda.Soul.SlotTruth`)
#include "UI/AsdaCharaInfoSubsystem.h"   // ★ 06.11.2026: перерисовка лотков действий при смене пары
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "UI/AsdaItemCountStyle.h"   // ★ 26.10.2026: обводка числа стопки — общая точка (ключ asda.UI.ItemCountOutline)
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "AsdaBytes.h"   // D4: единые читалки сырой записи (little-endian, DataHex, ASCII-поле)
#include "UI/AsdaUiHelpers.h"   // ★ D2/D4: единые UI-хелперы (дедуп 2026-08-08)

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMiniStateSubsystem.h"        // один общий тултип на HUD (эталон держит один g_ItemTooltip)
// владелец общей плашки (порт g_ItemTooltip)   ★ R3: комментарий снятого включения "UI/AsdaItemTooltipSubsystem.h" (файл им не пользовался) — сохранён
// ★ ТОЛЬКО ЗАГОЛОВОК, поведение не меняется: ниже (мост к окну навыков) уже используется
//   `UAsdaSkillWindowSubsystem`, но #include отсутствовал — тип «доезжал» через unity-сборку.
//   Как только этот файл попадает в отдельную единицу трансляции, компиляция падала.
#include "UI/AsdaSkillWindowSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"     // сводная сверка шторок отката (`asda.Cooldown.Dump`)
#include "UI/AsdaBoosterPanelSubsystem.h"  // ★ гейт `CObject_GetField254(g_pInventoryWnd)` = ОКНО ГАЧИ 10050
#include "UI/AsdaChatWindowSubsystem.h"    // ★ гейт `Input_IsTextInputFocused(g_pChatInputWnd)`
#include "UI/AsdaSkillDatabase.h"          // ★ Аудит №3 (A13): имя иконки навыка — только отсюда
#include "UI/AsdaAnimatedPlane.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Login/AsdaText.h"   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): FAsdaText::Loc — заголовки подсказок по id
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaKeybindConfig.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Engine.h"   // ★ 17.10.2026 GEngine->Exec(quit) в самотесте иконок эмоций
#include "Components/CanvasPanel.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Engine/DataTable.h"
#include "Items/AsdaItemDbRows.h"
#include "HAL/IConsoleManager.h"
#include "UObject/ObjectKey.h"       // ★ 26.10.2026: TObjectKey — ревизия списка навыков по экземпляру панели
#include "UI/AsdaCraftSubsystem.h"   // ★ волна В3 ремесла: единый предикат «окно ремесла открыто» (`IsAnyCraftWindowOpen`)
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 29.09.2026: окно обмена 2008 открыто — гейт слотов и значков заявки 4/5
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "World/AsdaFishingSubsystem.h"     // ★ 08.10.2026 (рыбалка В4): гейт «идёт рыбалка» в `IsActivationBlocked`
#include "AsdaSelf.h"                      // ★ Дедуп №2, В1-B (U5a): «это я?» — `AsdaSelf::IsSelfUserIdx`
#include "UI/AsdaHelpTopics.h"             // ★ 30.10.2026: справочные темы кнопок листания и пустого слота оружия
#include "UI/AsdaDeathInputLock.h"          // ★ 05.11.2026: меню смерти запирает слоты (эталон CUIManager+0x44, ui_core.c:67351)
#include "AsdaHotbarSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaHotbarSubsystem*.cpp

// ══════════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════════
FString UAsdaHotbarSubsystem::DumpControls() const
{
	FString R;
	R += FString::Printf(TEXT("=== QUICK_NEW_WIN (73000): окно %s, контролы %s, поколение %d ===\n"),
		QuickWidget ? TEXT("открыто") : TEXT("НЕ открыто"), bControlsReady ? TEXT("разобраны") : TEXT("НЕ разобраны"),
		WidgetGeneration);
	R += FString::Printf(TEXT("страница %d/5, второй ряд %s, панель %s\n"),
		CurrentPage + 1, bSecondRowVisible ? TEXT("виден") : TEXT("скрыт"),
		bPanelActive ? TEXT("активна") : TEXT("заблокирована"));
	// ★★★ ВОЛНА 9 (П8-9): слот под курсором и состояние модального гейта. Без этой строки
	//   раскатку гейта на чужие подсистемы НЕЧЕМ проверить живьём: наведение видно только здесь.
	//   Под открытым нотисом эталон не отдаёт мышь никому ниже (`ui_core.c:104149-104153`),
	//   поэтому ожидание такое: гейт ЗАКРЫТ ⇒ «слот под курсором» обязан быть −1.
	R += FString::Printf(TEXT("слот под курсором=%d, МОДАЛЬНЫЙ ГЕЙТ=%s\n"),
		HoverSlot,
		AsdaUi::IsModalDispatchBlocked(GetGameInstance()) ? TEXT("ЗАКРЫТ (мышь отдана модалке)")
		                                                  : TEXT("открыт"));
	auto DumpRow = [&](const TArray<FAsdaHotbarSlotUi>& Row, const TCHAR* Tag, bool bTRow)
	{
		for (int32 C = 0; C < Row.Num(); ++C)
		{
			const FAsdaHotbarSlotUi& U = Row[C];
			const int32 Abs = AbsSlot(C, bTRow);
			// ★ ЖИВОЙ прямоугольник иконки слота. Нужен, чтобы бросок можно было проверить ПУТЁМ
			//   ИГРОКА (`asda.UI.SimDrag <откуда> <куда>`): координаты цели иначе взять неоткуда,
			//   а слот канваса про абсолютное положение врёт (закон `jui-cell-chain-nesting`).
			FVector2D Mn(0, 0), Mx(0, 0);
			const bool bGeom = U.Icon && AsdaUi::WidgetAbsRect(U.Icon, Mn, Mx);
			R += FString::Printf(TEXT("%s кол.%-2d абс.%-2d живая %s(%.0f,%.0f)-(%.0f,%.0f) центр (%.0f,%.0f) "),
				Tag, C, Abs, bGeom ? TEXT("") : TEXT("НЕТ "), Mn.X, Mn.Y, Mx.X, Mx.Y,
				(Mn.X + Mx.X) * 0.5f, (Mn.Y + Mx.Y) * 0.5f);
			R += FString::Printf(TEXT("%s кол.%-2d абс.%-2d иконка:%s текст:%s шторка:%s(%d кадр.) вспышка:%s(%d кадр.) затемн.:%s клавиша:%s\n"),
				Tag, C, Abs,
				U.Icon ? TEXT("+") : TEXT("-"),
				U.StackText ? TEXT("+") : TEXT("-"),
				U.Cool.Plane ? TEXT("+") : TEXT("-"), U.Cool.Plane ? U.Cool.Plane->TotalFrame : 0,
				U.Flash ? TEXT("+") : TEXT("-"),   U.Flash ? U.Flash->TotalFrame : 0,
				U.Dim ? TEXT("+") : TEXT("-"),
				U.KeyGlyph ? TEXT("+") : TEXT("-"));
		}
	};
	DumpRow(MainUi, TEXT("ряд1"), false);
	DumpRow(TUi,    TEXT("Tряд"), true);
	R += FString::Printf(TEXT("цифры страниц: %d (NUM6 скрыт: %s), кнопки: вверх %s / вниз %s\n"),
		PageDigits.Num(), PageDigit6 ? TEXT("да") : TEXT("нет"),
		BtnPageUp ? TEXT("+") : TEXT("-"), BtnPageDown ? TEXT("+") : TEXT("-"));
	R += FString::Printf(TEXT("глифов клавиш в таблице: %d (эталон 65)\n"), KeyGlyphs.Num());
	int32 Notices = 0;
	for (const FAsdaHotbarNoticeUi& N : NoticeUi) { if (N.Icon) { ++Notices; } }
	R += FString::Printf(TEXT("иконок уведомлений: %d/7\n"), Notices);
	// ★ Мост навыков показываем ТРЕМЯ ответами: «иконки», «применение навыка», «навык соулмейта».
	//   Один флаг здесь врал: `IsBound()` = только иконки, а слот при этом мог не применяться.
	R += FString::Printf(TEXT("мосты: инвентарь %s, навыки: иконки %s / применение %s / соулмейт %s, эмоции-допменю %s\n"),
		Inventory.IsBound() ? TEXT("подключён") : TEXT("НЕТ"),
		Skills.IsBound() ? TEXT("да") : TEXT("НЕТ"),
		Skills.CanUse() ? TEXT("да") : TEXT("НЕТ"),
		Skills.CanCastAuto() ? TEXT("да") : TEXT("НЕТ"),
		Misc.GetEmotionIconName ? TEXT("подключён") : TEXT("НЕТ"));
	return R;
}

// ★ 17.10.2026 Самотест иконок эмоций (см. объявление). Два прохода: «таблица» — моста нет, имя и признак 64×64 из
//   `ResolveEmotionIcon`; «мост» — имя отдаёт мост окна эмоций (настоящий, если поднят, иначе подставной: только имя, как
//   `UAsdaCharaInfoSubsystem::GetEmotionIconName`) — именно в нём признак 64×64 терялся до правки. Кисть проверяется на
//   временном UImage тем же `ApplyIconToImage`, что рисует ячейку.
FString UAsdaHotbarSubsystem::SelfTestEmoteIcons()
{
	BuildEmotionLists();
	FString R = TEXT("=== самотест иконок эмоций хотбара ===\n");
	int32 Ok = 0, Fail = 0;
	const int32 Probe = 0;                                         // слот-полигон (восстанавливается)
	if (!Slots.IsValidIndex(Probe)) { return R + TEXT("ИТОГ: ok=0 fail=1 (нет слотов)\n"); }
	const FAsdaHotbarSlot Saved = Slots[Probe];                    // содержимое слота до теста
	const FAsdaHotbarMiscBridge SavedMisc = Misc;                  // мосты до теста
	const bool bRealBridge = (bool)Misc.GetEmotionIconName;        // поднят ли настоящий мост окна эмоций
	UImage* Img = NewObject<UImage>(this);                         // временная картинка 32×32 вместо ячейки
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const bool bBridge = (Pass == 1);
		if (!bBridge) { Misc.GetEmotionIconName = nullptr; }
		else if (!bRealBridge)
		{
			Misc.GetEmotionIconName = [this](int32 Code)           // подставной мост: только имя, без признака размера
			{
				FString T; bool bF = false;
				ResolveEmotionIcon(Code, T, bF);
				return T;
			};
		}
		else { Misc = SavedMisc; }
		for (int32 G = 0; G < 3; ++G)
		{
			for (int32 I = 0; I < EmotionList[G].Num(); ++I)
			{
				const int32 Code = G * 100 + I;
				FAsdaHotbarSlot& S = Slots[Probe];
				S.Clear();
				S.Type = (uint8)EAsdaHotbarSlotType::Emotion;
				S.Index = Code;
				RebuildOneSlot(Probe);
				const FAsdaHotbarSlot Got = Slots[Probe];
				const int32 Want = IsFullSizeEmotionIcon(Got.IconAtlas) ? 64 : 32;
				bool bPass = !Got.IconAtlas.IsEmpty() && Got.IconSize == Want;
				FString Uv = TEXT("—");
				if (bPass)
				{
					bPass = ApplyIconToImage(Img, Got);
					const FBox2f Box = Img->GetBrush().GetUVRegion();
					const FVector2f Mn = Box.bIsValid ? Box.Min : FVector2f(0.f, 0.f);
					const FVector2f Mx = Box.bIsValid ? Box.Max : FVector2f(1.f, 1.f);
					Uv = FString::Printf(TEXT("(%.3f,%.3f)-(%.3f,%.3f)"), Mn.X, Mn.Y, Mx.X, Mx.Y);
					bPass = bPass && Mn.Equals(FVector2f(0.f, 0.f), 0.01f) && Mx.Equals(FVector2f(1.f, 1.f), 0.01f);
				}
				(bPass ? Ok : Fail)++;
				if (!bPass || Want == 64)
				{
					R += FString::Printf(TEXT("%s %s код %3d '%s' крой %d (ждём %d) UV %s\n"), bPass ? TEXT("[OK]  ") : TEXT("[FAIL]"),
						bBridge ? (bRealBridge ? TEXT("мост") : TEXT("мост*")) : TEXT("табл."), Code, *Got.IconAtlas, Got.IconSize, Want, *Uv);
				}
			}
		}
	}
	Misc = SavedMisc;
	Slots[Probe] = Saved;
	RebuildOneSlot(Probe);
	RefreshPage();
	R += FString::Printf(TEXT("эмоций: группа0 %d, группа1 %d, группа2 %d; мост окна эмоций %s (мост* — подставной)\n"),
		EmotionList[0].Num(), EmotionList[1].Num(), EmotionList[2].Num(), bRealBridge ? TEXT("поднят") : TEXT("не поднят"));
	R += FString::Printf(TEXT("ИТОГ: ok=%d fail=%d\n"), Ok, Fail);
	return R;
}

FString UAsdaHotbarSubsystem::DumpSlots() const
{
	FString R = TEXT("=== модель хотбара: 78 слотов (0..64 = 5 страниц × 13, 65..76 = T-ряд, 77 без контрола) ===\n");
	for (int32 I = 0; I < Slots.Num(); ++I)
	{
		const FAsdaHotbarSlot& S = Slots[I];
		if (S.IsEmpty()) { continue; }
		R += FString::Printf(TEXT("слот %-2d (стр.%d кол.%-2d): тип=%d подтип=%d ячейка=%d кол-во=%d id=%d атлас='%s' (%d,%d,%d)\n"),
			I, I / kColsPerPage, I % kColsPerPage, S.Type, S.SubType, S.Index, S.Count, S.Id,
			*S.IconAtlas, S.IconX, S.IconY, S.IconSize);
	}
	R += FString::Printf(TEXT("перезарядок: предметов %d, навыков %d\n"), ItemCooldowns.Num(), SkillCooldowns.Num());
	// ★ Каждая запись отката предмета — построчно (закон `full-structural-analysis-always`): по этому
	//   дампу видно, ПО ЧЕМУ гаснут слоты — по группе сервера или по коду+контейнеру клиента.
	{
		const double Now = AsdaCooldownNowS(GetGameInstance());   // ★ 25.10.2026: единые часы реестра (ключ asda.Cooldown.RealClock)
		for (const FItemCooldownRec& C : ItemCooldowns)
		{
			R += FString::Printf(
				TEXT("  откат: предмет %d, контейнер %d, ячейка %d, группа %d, осталось %.1f из %.1f с\n"),
				C.ItemCode, C.Container, C.SlotIndex, C.Group, FMath::Max(0.0, C.EndS - Now), C.TotalS);
		}
	}
	return R;
}

// ★ Сверка «один источник — три окна». Печатает СНАЧАЛА сам реестр (единственный источник правды),
//   потом состояние шторок панели. Инвентарь и книга навыков печатают своё тем же форматом.
FString UAsdaHotbarSubsystem::DumpCooldownCurtains() const
{
	const double Now = AsdaCooldownNowS(GetGameInstance());   // ★ 25.10.2026: единые часы реестра (ключ asda.Cooldown.RealClock)
	FString R = FString::Printf(
		TEXT("=== ОТКАТЫ: ЕДИНЫЙ РЕЕСТР (панель быстрого доступа) ===\n")
		TEXT("ключ asda.Cooldown.ShowEverywhere = %d (1 = шторка во всех окнах)\n"),
		IsCooldownShownEverywhere() ? 1 : 0);

	R += FString::Printf(TEXT("предметов в реестре: %d\n"), ItemCooldowns.Num());
	for (const FItemCooldownRec& C : ItemCooldowns)
	{
		// ★ Рядом с записью печатаем ОТВЕТ ОБЩЕЙ ТОЧКИ — того самого метода, которым пользуются и
		//   панель, и ячейка сумки, и слот надетого. Совпадение чисел и есть «источник один».
		const FAsdaHotbarCooldown Q = QueryItemCooldown(C.ItemCode, C.Container);
		R += FString::Printf(
			TEXT("  предмет %d · контейнер %d · ячейка %d · группа %d · осталось %.2f из %.2f с")
			TEXT("  → QueryItemCooldown: состояние %d, остаток %.0f мс, стартовый кадр %d\n"),
			C.ItemCode, C.Container, C.SlotIndex, C.Group, FMath::Max(0.0, C.EndS - Now), C.TotalS,
			Q.State, Q.RemainMs, Q.IconFrame);
	}
	R += FString::Printf(TEXT("навыков в локальном реестре: %d%s\n"), SkillCooldowns.Num(),
		Skills.GetCooldown ? TEXT(" (авторитет — мост книги навыков)") : TEXT(""));
	for (const TPair<int32, FCooldownRec>& It : SkillCooldowns)
	{
		const FAsdaHotbarCooldown Q = QuerySkillCooldown(It.Key);
		R += FString::Printf(
			TEXT("  навык %d · осталось %.2f из %.2f с")
			TEXT("  → QuerySkillCooldown: состояние %d, остаток %.0f мс, стартовый кадр %d\n"),
			It.Key, FMath::Max(0.0, It.Value.EndS - Now), It.Value.TotalS,
			Q.State, Q.RemainMs, Q.IconFrame);
	}

	R += TEXT("=== ШТОРКИ: ПАНЕЛЬ БЫСТРОГО ДОСТУПА ===\n");
	int32 Lit = 0;
	auto Row = [&](const TArray<FAsdaHotbarSlotUi>& Arr, bool bTRow, const TCHAR* Tag)
	{
		for (int32 C = 0; C < Arr.Num(); ++C)
		{
			const FAsdaHotbarSlotUi& U = Arr[C];
			if (U.Cool.ShownState == 0) { continue; }
			++Lit;
			const FAsdaHotbarSlot& S = GetSlot(AbsSlot(C, bTRow));
			R += FString::Printf(
				TEXT("  %s кол.%-2d абс.слот %-2d тип %d id %-6d состояние %d осталось %.2f с\n"),
				Tag, C, U.Cool.ShownKey, S.Type, S.Id, U.Cool.ShownState,
				FMath::Max(0.0, U.Cool.ShownEndS - Now));
		}
	};
	Row(MainUi, false, TEXT("ряд1"));
	Row(TUi,    true,  TEXT("ряд2"));
	if (Lit == 0) { R += TEXT("  горящих шторок нет\n"); }
	return R;
}

void UAsdaHotbarSubsystem::SimulateLoadPage(int32 Page, const TArray<uint8>& Body130)
{
	if (Page < 0 || Page >= kPageCount) { return; }
	TArray<uint8> Raw = Body130;
	Raw.SetNumZeroed(kColsPerPage * kWireSlotBytes);
	OnHotbarPageReceived((uint8)Page, Raw);
	UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] эмуляция 5059: страница %d поставлена в очередь (применится в Tick)"), Page);
}

// ══════════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ ═════════════════════════════════════════════
//   Юзер запускает их В ИГРЕ и лог не читает — каждая строка уходит и в лог, и в системный журнал.
static UAsdaHotbarSubsystem* QuickOf(UWorld* W)
{
	return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
}

#if !UE_BUILD_SHIPPING   // ★ античит А2 (12.10.2026): отладка самотестов/дев-команд — только вне Shipping
// ★ U13 (В1 дедупа №2): тело — общая точка `AsdaNotice::ConsoleEcho` (прежняя копия была дословной, менялась только категория лога).
static void QuickEcho(UWorld* W, const FString& Text)
{
	AsdaNotice::ConsoleEcho(W, Text, LogAsdaQuick);
}
#endif // !UE_BUILD_SHIPPING

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GQuickDump(
	TEXT("asda.Quick.Dump"),
	TEXT("Хотбар: дамп КАЖДОГО разобранного контрола окна QUICK_NEW_WIN"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaHotbarSubsystem* Q = QuickOf(W)) { QuickEcho(W, Q->DumpControls()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GQuickSlots(
	TEXT("asda.Quick.Slots"),
	TEXT("Хотбар: дамп модели (78 слотов + перезарядки)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaHotbarSubsystem* Q = QuickOf(W)) { QuickEcho(W, Q->DumpSlots()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ 17.10.2026 дев-команда, в Shipping не регистрируется
// Самотест иконок эмоций хотбара (`SelfTestEmoteIcons`); отчёт — в журнал построчно (пакетный прогон ищет строку «ИТОГ»).
static FAutoConsoleCommandWithWorldAndArgs GQuickSelfTestEmoteIcons(
	TEXT("asda.Quick.SelfTestEmoteIcons"),
	TEXT("Хотбар: самотест иконок эмоций — крой 64/32 и UV кисти (0,0)-(1,1) для каждой эмоции, по таблице и через мост окна эмоций"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// аргумент `quit` — выйти после отчёта (так пакетный прогон узнаёт конец самотеста)
		const bool bQuit = A.ContainsByPredicate([](const FString& S) { return S.Equals(TEXT("quit"), ESearchCase::IgnoreCase); });
		UAsdaHotbarSubsystem* Q = QuickOf(W);
		if (!Q)
		{
			UE_LOG(LogAsdaQuick, Error, TEXT("[самотест иконок эмоций] ИТОГ: ОТКАЗ — нет подсистемы хотбара"));
		}
		else
		{
			TArray<FString> Lines;
			Q->SelfTestEmoteIcons().ParseIntoArrayLines(Lines);
			for (const FString& L : Lines) { UE_LOG(LogAsdaQuick, Display, TEXT("[самотест иконок эмоций] %s"), *L); }
		}
		if (bQuit && GEngine && W) { GEngine->Exec(W, TEXT("quit")); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GQuickSet(
	TEXT("asda.Quick.Set"),
	TEXT("asda.Quick.Set <абс.слот 0..77> <тип 0..5> <подтип> <ячейка> <кол-во> <id> — положить содержимое "
	     "в слот тем же путём, что перетаскивание из инвентаря (Hotbar_DragDrop_SetSlot) + отправка 5067"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: сырые поля слота без сверки с сервером — при живом соединении 5067 сохранил бы синтетическую раскладку.
		static const uint16 kGuardOps[] = { 5067 };
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Quick.Set"), 5.f, MakeArrayView(kGuardOps))) { return; }
		UAsdaHotbarSubsystem* Q = QuickOf(W);
		if (!Q || A.Num() < 6)
		{
			QuickEcho(W, TEXT("использование: asda.Quick.Set <слот> <тип> <подтип> <ячейка> <кол-во> <id>"));
			return;
		}
		const bool bOk = Q->SetSlotContent(FCString::Atoi(*A[0]), (uint8)FCString::Atoi(*A[1]),
			(uint8)FCString::Atoi(*A[2]), FCString::Atoi(*A[3]), FCString::Atoi(*A[4]), FCString::Atoi(*A[5]));
		QuickEcho(W, bOk ? TEXT("слот заполнен") : TEXT("отказ (проверь правило слота оружия itemType==9)"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GQuickClear(
	TEXT("asda.Quick.Clear"),
	TEXT("asda.Quick.Clear <абс.слот> — снять содержимое слота (эквивалент перетаскивания наружу) + 5067"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaHotbarSubsystem* Q = QuickOf(W);
		if (!Q || A.Num() < 1) { QuickEcho(W, TEXT("использование: asda.Quick.Clear <слот>")); return; }
		Q->ClearSlot(FCString::Atoi(*A[0]));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GQuickPage(
	TEXT("asda.Quick.Page"),
	TEXT("asda.Quick.Page <1..5> — сменить страницу тем же путём, что клавиши PAGE 1..5 "
	     "(вне боя — через отложенное действие персонажа 104 + анимация смены оружия)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaHotbarSubsystem* Q = QuickOf(W);
		if (!Q || A.Num() < 1) { QuickEcho(W, TEXT("использование: asda.Quick.Page <1..5>")); return; }
		Q->SetPage(FMath::Clamp(FCString::Atoi(*A[0]), 1, 5) - 1);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GQuickPageStep(
	TEXT("asda.Quick.PageStep"),
	TEXT("asda.Quick.PageStep <1|-1> — листание кнопками 73327/73328 (событие 66561, wrap 5→0 / <0→4)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaHotbarSubsystem* Q = QuickOf(W);
		if (!Q) { return; }
		Q->PageUpDown(A.Num() < 1 || FCString::Atoi(*A[0]) >= 0);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GQuickCool(
	TEXT("asda.Quick.Cool"),
	TEXT("asda.Quick.Cool <id предмета|навыка> <мс> [skill|контейнер] [группа] — запустить перезарядку: "
	     "спрайтовая шторка NE_CoolTime32 (118 кадров) на всех совпавших слотах, по окончании — вспышка "
	     "NE_IconEffect1. Для предмета 3-й аргумент = контейнер (1 сумка / 2 расходники), 4-й = группа отката"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaHotbarSubsystem* Q = QuickOf(W);
		if (!Q || A.Num() < 2) { QuickEcho(W, TEXT("использование: asda.Quick.Cool <id> <мс> [skill|контейнер] [группа]")); return; }
		const int32 Id = FCString::Atoi(*A[0]);
		const int32 Ms = FCString::Atoi(*A[1]);
		if (A.Num() > 2 && A[2].Equals(TEXT("skill"), ESearchCase::IgnoreCase))
		{
			Q->PushSkillCooldown(Id, Ms);
		}
		else
		{
			const int32 Cont  = (A.Num() > 2) ? FCString::Atoi(*A[2]) : 2;   // расходники — самый частый случай
			const int32 Group = (A.Num() > 3) ? FCString::Atoi(*A[3]) : 0;
			Q->PushItemCooldownRaw(Id, Cont, /*SlotIndex*/ -1, Group, Ms);
		}
		QuickEcho(W, FString::Printf(TEXT("перезарядка id=%d на %d мс запущена"), Id, Ms));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★★★ СВЕРКА «ОДИН ИСТОЧНИК — ТРИ ОКНА» (отступление 2026-08-14). Печатает подряд единый реестр
//   откатов и состояние шторок ВСЕХ трёх окон, чтобы одной командой было видно: шторка зажглась и
//   гаснет в панели, сумке и книге навыков ОДНОВРЕМЕННО — потому что запись отката у них общая.
static FAutoConsoleCommandWithWorldAndArgs GCooldownDump(
	TEXT("asda.Cooldown.Dump"),
	TEXT("Откаты: единый реестр + состояние шторок в панели быстрого доступа, инвентаре и книге навыков"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI) { return; }
		if (const UAsdaHotbarSubsystem* Q = GI->GetSubsystem<UAsdaHotbarSubsystem>())
		{
			QuickEcho(W, Q->DumpCooldownCurtains());
		}
		if (const UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>())
		{
			QuickEcho(W, Inv->DumpCooldownCurtains());
		}
		if (const UAsdaSkillWindowSubsystem* Sk = GI->GetSubsystem<UAsdaSkillWindowSubsystem>())
		{
			QuickEcho(W, Sk->DumpCooldownCurtains());
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★★★ ОФЛАЙН-ЭМУЛЯЦИЯ ВТОРОГО БЛОКА 5465 (откаты премиум-предметов, `Recv_SkillCoolTime@0xE4B180`,
//   `network.c:28806-28832`). Собираем ровно тот блок 30×6, который лежит в теле пакета со смещения
//   +180, и отдаём в `ApplyServerItemCooldowns` — сервер для проверки не нужен.
static FAutoConsoleCommandWithWorldAndArgs GQuickRecv5465Items(
	TEXT("asda.Quick.Recv5465Items"),
	TEXT("asda.Quick.Recv5465Items <код> <остатокСек> [<код> <остатокСек> …] — эмулировать БЛОК ОТКАТОВ "
	     "ПРЕДМЕТОВ пакета 5465 (байты 180…359: i32 код + i16 остаток в секундах, терминатор −1). "
	     "Полную длительность подсистема берёт из special_itemlist.CooldownBase, как эталон"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaHotbarSubsystem* Q = QuickOf(W);
		if (!Q || A.Num() < 2)
		{
			QuickEcho(W, TEXT("использование: asda.Quick.Recv5465Items <код> <остатокСек> [<код> <остатокСек> …]"));
			return;
		}
		TArray<uint8> Block;
		Block.SetNumZeroed(30 * 6);
		int32 J = 0;
		for (int32 I = 0; I + 1 < A.Num() && J < 30; I += 2, ++J)
		{
			const int32 Code = FCString::Atoi(*A[I]);
			const int32 Sec  = FCString::Atoi(*A[I + 1]);
			uint8* P = Block.GetData() + J * 6;
			P[0] = (uint8)(Code & 0xFF);       P[1] = (uint8)((Code >> 8) & 0xFF);
			P[2] = (uint8)((Code >> 16) & 0xFF); P[3] = (uint8)((Code >> 24) & 0xFF);
			P[4] = (uint8)(Sec & 0xFF);        P[5] = (uint8)((Sec >> 8) & 0xFF);
		}
		if (J < 30)   // терминатор −1 в поле кода следующей записи (эталонное условие выхода)
		{
			uint8* P = Block.GetData() + J * 6;
			P[0] = P[1] = P[2] = P[3] = 0xFF;
		}
		Q->ApplyServerItemCooldowns(Block);
		QuickEcho(W, FString::Printf(TEXT("5465: блок откатов предметов принят, записей %d"), J));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GQuickRecv(
	TEXT("asda.Quick.Recv5059"),
	TEXT("asda.Quick.Recv5059 <страница 0..5> [тип подтип ячейка кол-во id] ... — эмулировать приход пакета "
	     "5059 (сервер не нужен): аргументы после номера страницы идут пятёрками на колонки 0..12"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельная страница 5059 становится «сохранённой» (SentSnapshot) и уехала бы на сервер со следующим 5067.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Quick.Recv5059"))) { return; }
		UAsdaHotbarSubsystem* Q = QuickOf(W);
		if (!Q || A.Num() < 1)
		{
			QuickEcho(W, TEXT("использование: asda.Quick.Recv5059 <страница> [тип подтип ячейка кол-во id] ..."));
			return;
		}
		const int32 Page = FCString::Atoi(*A[0]);
		TArray<uint8> Body;
		Body.SetNumZeroed(130);
		for (int32 C = 0; C < 13; ++C)
		{
			const int32 Base = 1 + C * 5;
			if (A.Num() < Base + 5) { break; }
			uint8* R = Body.GetData() + C * 10;
			R[0] = (uint8)FCString::Atoi(*A[Base + 0]);
			R[1] = (uint8)FCString::Atoi(*A[Base + 1]);
			const uint16 Idx = (uint16)FCString::Atoi(*A[Base + 2]); R[2] = (uint8)(Idx & 0xFF); R[3] = (uint8)(Idx >> 8);
			const uint32 Cnt = (uint32)FCString::Atoi(*A[Base + 3]);
			R[4] = (uint8)(Cnt & 0xFF); R[5] = (uint8)((Cnt >> 8) & 0xFF);
			R[6] = (uint8)((Cnt >> 16) & 0xFF); R[7] = (uint8)((Cnt >> 24) & 0xFF);
			const uint16 Id = (uint16)FCString::Atoi(*A[Base + 4]); R[8] = (uint8)(Id & 0xFF); R[9] = (uint8)(Id >> 8);
		}
		Q->SimulateLoadPage(Page, Body);
		QuickEcho(W, FString::Printf(TEXT("5059 страница %d поставлена в очередь"), Page));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GQuickUse(
	TEXT("asda.Quick.Use"),
	TEXT("asda.Quick.Use <клавишная колонка 0..23> — применить слот той же точкой входа, которой пользуются "
	     "хоткеи (0..11 = BAR SLOT 1..12 текущей страницы, 12..23 = NEW BAR SLOT 1..12 → слоты 65..76)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaHotbarSubsystem* Q = QuickOf(W);
		if (!Q || A.Num() < 1) { QuickEcho(W, TEXT("использование: asda.Quick.Use <0..23>")); return; }
		const int32 C = FCString::Atoi(*A[0]);
		Q->ActivateSlotByColumn(C);
		Q->ReleaseSlotByColumn(C);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GQuickRow2(
	TEXT("asda.Quick.SecondRow"),
	TEXT("asda.Quick.SecondRow <0|1> — опция g_bOptChildPairVisible: показать/скрыть T-ряд (12 слотов 65..76)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaHotbarSubsystem* Q = QuickOf(W);
		if (!Q) { return; }
		Q->SetSecondRowVisible(A.Num() < 1 || FCString::Atoi(*A[0]) != 0);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GQuickBlock(
	TEXT("asda.Quick.Active"),
	TEXT("asda.Quick.Active <0|1> — блокировка панели затемняющими оверлеями "
	     "(эталон uiHide/ShowPanelControls; 0 = затемнить и игнорировать события)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaHotbarSubsystem* Q = QuickOf(W);
		if (!Q) { return; }
		Q->SetPanelActive(A.Num() < 1 || FCString::Atoi(*A[0]) != 0);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GQuickNotice(
	TEXT("asda.Quick.Notice"),
	TEXT("asda.Quick.Notice <тип 0..15> [0=убрать] — иконка уведомления P_ICON (0/8 пати, 1 соулмейт, "
	     "2 друг, 3 подарок, 4/5 обмен предметами) + мигающая рамка Slot_AniSelect"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaHotbarSubsystem* Q = QuickOf(W);
		if (!Q || A.Num() < 1) { QuickEcho(W, TEXT("использование: asda.Quick.Notice <тип> [0=убрать]")); return; }
		const int32 T = FCString::Atoi(*A[0]);
		if (A.Num() > 1 && FCString::Atoi(*A[1]) == 0) { Q->RemoveNoticeIcon(T); }
		else { Q->AddNoticeIcon(T); }
		QuickEcho(W, Q->DumpNoticeIcons());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★★★ ЖИВАЯ ДИАГНОСТИКА «иконки не видно». Печатает по каждому из семи слотов уведомления не
//   «висит/не висит», а ВСЁ, что решает видимость: состояние ГРУППЫ-родителя `P_ICON0X_N_grp`
//   (её сворачивает импортёр из-за `Visible="0"` в `.JUI`), самой иконки, рамки, прозрачность
//   пульсации и РЕАЛЬНЫЙ ЭКРАННЫЙ ПРЯМОУГОЛЬНИК. Без этой команды баг «иконка создана, но не
//   видна» проверить в живом запуске нечем.
static FAutoConsoleCommandWithWorldAndArgs GQuickNoticeDump(
	TEXT("asda.Quick.NoticeDump"),
	TEXT("asda.Quick.NoticeDump — семь слотов уведомлений: тип, видимость ГРУППЫ/иконки/рамки, "
	     "альфа пульсации и экранный прямоугольник"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaHotbarSubsystem* Q = QuickOf(W);
		if (!Q) { QuickEcho(W, TEXT("панель квик-бара не поднята")); return; }
		QuickEcho(W, Q->DumpNoticeIcons());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GQuickStackStyle(
	TEXT("asda.Quick.StackStyle"),
	TEXT("asda.Quick.StackStyle [кегль] [обводка px] — оформление счётчика стака: кегль, чёрная обводка "
	     "(эталонная обводка = 0: поле 926 счётчику не ставится), выравнивание центр/низ (EXS TextAlign=9). Без аргументов — "
	     "просто применить текущие значения заново"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaHotbarSubsystem* Q = QuickOf(W);
		if (!Q) { return; }
		if (A.Num() > 0) { Q->StackCountFontPx = FMath::Clamp(FCString::Atoi(*A[0]), 4, 24); }
		if (A.Num() > 1) { Q->StackOutlinePx  = FMath::Clamp(FCString::Atoi(*A[1]), 0, 4); }
		Q->RefreshStackTextStyle();
		Q->RefreshPage();
		QuickEcho(W, FString::Printf(TEXT("счётчик стака: кегль %d, обводка %d px, центр/низ"),
			Q->StackCountFontPx, Q->StackOutlinePx));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GQuickIconProbe(
	TEXT("asda.Quick.IconProbe"),
	TEXT("asda.Quick.IconProbe <тип 2|3|4> <id/код> — откуда берётся иконка: 2 = эмоция (код, таблица "
	     "emotiontip @+32), 3 = навык (ИНДЕКС записи skilltooltip @+36), 4 = навык соулмейта "
	     "(код, soulmateskilltable @+36). Печатает имя и найдена ли текстура"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaHotbarSubsystem* Q = QuickOf(W);
		if (!Q || A.Num() < 2) { QuickEcho(W, TEXT("использование: asda.Quick.IconProbe <2|3|4> <id/код>")); return; }
		QuickEcho(W, Q->ProbeIcon(FCString::Atoi(*A[0]), FCString::Atoi(*A[1])));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★ Диагностика ЦЕПОЧКИ перетаскивания: что видит панель под курсором прямо сейчас. Отвечает на
//   два вопроса разом — «нашёлся ли слот» (это же зовёт инвентарь при броске) и «считает ли общий
//   хит-тест интерфейса, что курсор над окном» (от этого зависит очистка слота при броске наружу).
static FAutoConsoleCommandWithWorldAndArgs GQuickHit(
	TEXT("asda.Quick.HitTest"),
	TEXT("Хотбар: какой слот сейчас под курсором (то же, что зовёт инвентарь при броске) + над каким "
	     "окном интерфейса курсор"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaHotbarSubsystem* Q = QuickOf(W);
		if (!Q) { return; }
		const int32 Abs = Q->HitTestSlotUnderCursor();
		FName Zone = NAME_None;
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		const bool bOverUi = WM && WM->IsCursorOverAnyUiWindow(Zone);
		const FVector2D Cur = FSlateApplication::IsInitialized()
			? FSlateApplication::Get().GetCursorPos() : FVector2D::ZeroVector;
		QuickEcho(W, FString::Printf(
			TEXT("курсор (%.0f, %.0f): слот панели = %s; интерфейс под курсором = %s"),
			Cur.X, Cur.Y,
			Abs >= 0 ? *FString::Printf(TEXT("%d (страница %d)"), Abs, Abs / UAsdaHotbarSubsystem::kColsPerPage)
			         : TEXT("нет"),
			bOverUi ? *Zone.ToString() : TEXT("нет (бросок сюда СНИМЕТ содержимое слота)")));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GQuickKeyMap(
	TEXT("asda.Quick.KeyMap"),
	TEXT("Хотбар: таблица «ряд → колонка → слот → команда Shortcutkey.ini → клавиша» для обоих рядов."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaHotbarSubsystem* Q = QuickOf(W)) { UE_LOG(LogAsdaQuick, Log, TEXT("\n%s"), *Q->DumpKeyMap()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GQuickKeys(
	TEXT("asda.Quick.Keys"),
	TEXT("Хотбар: пересобрать подписи клавиш на слотах из текущего конфига клавиш "
	     "(эталон hotkeyBar_RebuildAndRefresh)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaHotbarSubsystem* Q = QuickOf(W)) { Q->RebuildKeyHints(); QuickEcho(W, TEXT("подписи клавиш пересобраны")); }
	}));
#endif // ASDA_DEV_CONSOLE
