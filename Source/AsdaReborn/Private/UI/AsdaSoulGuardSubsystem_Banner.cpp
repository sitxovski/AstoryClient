// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSoulGuardSubsystem_Banner.cpp — ★ R3-c 12.11.2026 (разрез AsdaSoulGuardSubsystem.cpp, дословный перенос): положение окна и указатель, анимации меток, баннеры (свой и соседей), летящие монеты.
//   Общее для частей — в AsdaSoulGuardSubsystem_Private.h; шапка и реверс — в AsdaSoulGuardSubsystem.cpp.
#include "UI/AsdaSoulGuardSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"   // ★ U13 (В1 дедупа №2): AsdaNotice::ConsoleEcho — эхо консольных команд
#include "AsdaSelf.h"   // ★ волна 1: «это я?» / мой номер / мой уровень — общая точка ядра (E2, E4)
#include "UI/AsdaUiHelpers.h"   // ★ Аудит №4 (B1): AsdaUi::SlotOf/SlotRect — общая точка слота канваса

#include "UI/AsdaMiniStateSubsystem.h"
#include "UI/AsdaMinimapSubsystem.h"    // ★ ширина окна мини-карты для авто-якоря (эталон GetSize(g_pMiniMapWnd))
#include "UI/AsdaSkillUseSubsystem.h"   // ★ ОБЩИЙ диспетчер применения: ряды Хранителя ходят через него
#include "UI/AsdaSkillDatabase.h"       // ★ Аудит №3 (A13): имя иконки навыка — только отсюда
#include "UI/AsdaWindowSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Items/AsdaItemTableSubsystem.h" // ★ Аудит №3 (A3): SkillCoin и skilltooltip — только через менеджер
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaWorldSubsystem.h"   // ★ 3D-компаньон хранителя: наличие → высота плашки, каст → мотион
// ★ цель-МОБ как точка вылета монеты (FindNetMobByRid отдаёт AAsdaMobActor*)   ★ R3: комментарий снятого включения "Npc/AsdaMobActor.h" (файл им не пользовался) — сохранён
#include "AsdaNetSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Engine/DataTable.h"
#include "Kismet/GameplayStatics.h"
#include "Framework/Application/SlateApplication.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "AsdaSoulGuardSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaSoulGuardSubsystem*.cpp

// `cameraTarget_UpdateAbovePlayer@0xEF6790` (render_core.c:5811), часть «позиция окна + цель монет».
//   ★ Смысл флага `this[524]` (у нас bAutoAnchor): пока он взведён (сразу после инициализации),
//     окно КАЖДЫЙ КАДР ставится в (ширина экрана − ширина мини-карты − 145, 20), а монеты летят
//     в точку «игрок + (−right)·310». После ЛЮБОГО клика по чекбоксу 56264 эталон запоминает
//     текущую позицию окна в [129]/[130], гасит флаг — и с этого момента окно стоит там, где его
//     оставили, а монеты летят прямо в игрока.
void UAsdaSoulGuardSubsystem::UpdatePinnedPosition()
{
	if (!bControlsReady) { return; }

	const AAsdaPlayerCharacter* Ch = GetLocalChar();
	// ★★★ ТОЧКА ПРИЛЁТА МОНЕТ СЧИТАЕТСЯ ВСЕГДА, а не только при видимом окне. В эталоне
	//   (`cameraTarget_UpdateAbovePlayer@0xEF6790`, `render_core.c:5814-5863`) и вычисление точки
	//   `cameraCtrl_2[81..83]`, и сам полёт монет (`coinDropMgr_UpdateFlyToPlayer`) стоят под ОДНИМ
	//   гейтом `byte+308` (готовность HUD), а видимость окна 56000 решает отдельный
	//   `hudSkillPopup_RefreshOrHide`, вызванный СТРОКОЙ ВЫШЕ и на полёт не влияющий.
	//   Дальше по функции (после раннего выхода) — только РАСКЛАДКА самого окна, ей `bWindowShown` нужен.
	if (bAutoAnchor)
	{
		// Точка прилёта монет: игрок + (−right)·310, где right = cross(up, камера→игрок).
		if (Ch)
		{
			const FVector P = Ch->GetActorLocation();
			FVector CamLoc = P + FVector(0, -1, 0);
			if (const APlayerController* PC = Cast<APlayerController>(Ch->GetController()))
			{
				FRotator R; PC->GetPlayerViewPoint(CamLoc, R);
			}
			const FVector ToCam = (P - CamLoc).GetSafeNormal();
			const FVector Right = FVector::CrossProduct(FVector::UpVector, ToCam).GetSafeNormal();
			CoinTargetWorld = P + (-Right) * CoinTargetSideOffset;
		}
		if (!bWindowShown) { return; }   // дальше — только раскладка ОКНА (см. комментарий выше)
		// X = ширина экрана − ширина мини-карты − 145, Y = 20 (в «дизайн-пикселях» канваса импортёра).
		float ScreenW = MiniWidget ? MiniWidget->GetCachedGeometry().GetLocalSize().X : 0.f;
		if (ScreenW <= 1.f) { return; }
		// ★★★ ШИРИНА МИНИ-КАРТЫ = ШИРИНА ЕЁ ОКНА (порт эталонного GetSize(g_pMiniMapWnd), слот
		//   vtable+24): 133 компакт / 260 развёрнуто, БЕЗ оглядки на видимость — эталон размер окна
		//   тоже читает всегда. ⚠ Прежний замер по UUserWidget'у MINIMAP_WIN был ошибкой: этот
		//   виджет добавлен в вьюпорт ПОЛНОЭКРАННЫМ, его ширина = ширина экрана (лог: «мини-карта
		//   2554»), из-за чего X = screenW − screenW − 145 < 0 и окно прижимало к ЛЕВОМУ краю.
		float MiniMapW = MiniMapFallbackWidth;   // фолбэк: .JUI MINIMAP_WIN Size "133 134"
		if (const UAsdaMinimapSubsystem* MapSys =
				GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr)
		{
			MiniMapW = MapSys->GetWindowWidthPx();
		}
		// ★ Габариты окна = размер группы (union поддерева); нужны страховочному клампу ниже.
		FVector2D GP(0, 0), GS(0, 0);
		GetSlotRect(RootGroup, GP, GS);
		FVector2D HP(0, 0), HS(0, 0);
		GetSlotRect(RootHit, HP, HS);
		const float ScreenH = MiniWidget ? MiniWidget->GetCachedGeometry().GetLocalSize().Y : 0.f;
		// X — эталон 1:1 (+ начало родителя `MINISTATE_WIN` (0,6), т.к. координаты контролов локальны).
		// Y — эталон 1:1: локальный литерал 20 из машинного кода (⇒ экранные 26). Прежний режим
		//   «низ экрана» оставлен за флагом bEtalonTopAnchor = false (см. шапку .h).
		const float WantY = (bEtalonTopAnchor || ScreenH <= 1.f)
			? (ParentOrigin.Y + PinTop)
			: (ScreenH - PinBottomMargin - GS.Y + HP.Y);   // низ ГРУППЫ на PinBottomMargin выше низа экрана
		FVector2D Want(ParentOrigin.X + ScreenW - MiniMapW - PinRightMargin, WantY);
		// ★ СТРАХОВКА (не эталон): не дать окну выйти за экран. Минимум — 0 (не отступ! иначе
		//   эталонный верхний край группы, стоящий выше 20 px, отодвигало бы вниз), максимум —
		//   правый/нижний край экрана минус габарит группы минус зазор PinEdgeGuard.
		//   При штатном HUD (мини-карта на месте) кламп не срабатывает вовсе.
		const FVector2D WantGroup = Want - HP;
		const float Guard = FMath::Max(0.f, PinEdgeGuard);
		const FVector2D MaxGroup(FMath::Max(0.f, ScreenW - GS.X - Guard),
		                         FMath::Max(0.f, ScreenH - GS.Y - Guard));
		const FVector2D ClampedGroup(FMath::Clamp(WantGroup.X, 0.f, MaxGroup.X),
		                             ScreenH > 1.f ? FMath::Clamp(WantGroup.Y, 0.f, MaxGroup.Y) : WantGroup.Y);
		Want = ClampedGroup + HP;
		if (!bLoggedPin || !PinnedPos.Equals(Want, 0.5f))
		{
			bLoggedPin = true;
			UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] авто-привязка окна: экран %.0fx%.0f, мини-карта %.0f → 56000 в (%.0f,%.0f), группа %.0fx%.0f"),
				ScreenW, ScreenH, MiniMapW, Want.X, Want.Y, GS.X, GS.Y);
		}
		PinnedPos = Want;
	}
	else if (Ch)
	{
		// ★ Эталон (`render_core.c:5845-5852`, ветка «авто-якорь СНЯТ»):
		//     cameraCtrl_2[81] = localPlayer[111]; [82] = …; [83] = …;
		//     flyT = cameraCtrl_2[82] + 30.0;  cameraCtrl_2[82] = flyT;   // ← +30 ПО ИГРОВОЙ Y (вверх)
		//   Порт брал только позицию игрока и терял «+30»: старт монеты (позиция цели) и её цель
		//   совпадали при бое вплотную, дистанция выходила ~0 ⇒ монета «долетала» в тот же тик,
		//   полёта не было вовсе (лог: «летящая монета» и «монета долетела» с разницей 2 мс).
		//   Игровая ось Y = мировая UE +Z ([[effects-conversion-model]], конверсия (x,−z,y)).
		CoinTargetWorld = Ch->GetActorLocation() + FVector(0.f, 0.f, CoinTargetUpOffset);
	}
	if (!bWindowShown) { return; }   // дальше — только раскладка ОКНА (см. комментарий выше)

	// ★ Позиция была восстановлена из настроек — захватить её ФАКТ по живому дереву (общий механизм
	//   раскладки уже применил сдвиг из ini к этому моменту: окно показано ⇒ давно зарегистрировано).
	if (bPinnedPosStale && !bAutoAnchor && RootHit && RootGroup)
	{
		bPinnedPosStale = false;
		PinnedPos = GroupSpacePos(RootGroup, RootHit);
		UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] восстановленная позиция зафиксирована по факту: 56000 в (%.0f,%.0f)"),
			PinnedPos.X, PinnedPos.Y);
	}

	// Эталон: перемещать окно только когда чекбокс ВЗВЕДЁН (`if (getFlagByte_p608(cbox))`);
	//   если чекбокса нет вовсе — перемещать всегда.
	if ((PinCheck && !bPinned) || !RootHit) { return; }
	// ★ Оффсеты вложенных узлов ЛОКАЛЬНЫ относительно своей группы (генератор строит вложенные
	//   CanvasPanel), поэтому позиция самого контрола 56000 = оффсет группы в корневом канвасе
	//   ПЛЮС локальный оффсет его хит-зоны внутри группы.
	FVector2D Target = PinnedPos;
	// ★ СТРАХОВКА (не эталон, помечено): после смены разрешения зафиксированная позиция может
	//   оказаться за экраном — целимся в ближайшую точку НА экране, сам PinnedPos не портим
	//   (на прежнем разрешении окно вернётся ровно туда, где его оставили).
	{
		const float ScrW = MiniWidget ? MiniWidget->GetCachedGeometry().GetLocalSize().X : 0.f;
		const float ScrH = MiniWidget ? MiniWidget->GetCachedGeometry().GetLocalSize().Y : 0.f;
		if (ScrW > 1.f && ScrH > 1.f && RootGroup)
		{
			FVector2D GP2(0, 0), GS2(0, 0), HP2(0, 0), HS2(0, 0);
			GetSlotRect(RootGroup, GP2, GS2);
			GetSlotRect(RootHit, HP2, HS2);
			const FVector2D Grp = Target - HP2;
			const FVector2D Cl(FMath::Clamp(Grp.X, 0.f, FMath::Max(0.f, ScrW - GS2.X)),
			                   FMath::Clamp(Grp.Y, 0.f, FMath::Max(0.f, ScrH - GS2.Y)));
			Target = Cl + HP2;
		}
	}
	const FVector2D Cur = GroupSpacePos(RootGroup, RootHit);
	const FVector2D Delta = Target - Cur;
	if (Delta.SizeSquared() > 0.25f) { ShiftGroup(RootGroup, Delta); }
}

// Позиция вложенного узла в системе координат РОДИТЕЛЯ ГРУППЫ (= оффсет группы + локаль узла).
FVector2D UAsdaSoulGuardSubsystem::GroupSpacePos(const UWidget* Group, const UWidget* Inner)
{
	FVector2D GP(0, 0), IP(0, 0), Sz;
	GetSlotRect(Group, GP, Sz);
	GetSlotRect(Inner, IP, Sz);
	return GP + IP;
}

// `uiDialogMsgProc@0xEF40C0` (ui_core.c:92512): наведение/клик по иконкам скиллов рядов
//   (56013/56053/56103) и переключение чекбокса 56264. Мышь ловим ОПРОСОМ — узлы импортированных
//   окон это UImage, а SImage не поглощает OnMouseButtonDown (та же грабля, что у иконок эффектов).
void UAsdaSoulGuardSubsystem::TickPointer()
{
	if (!bControlsReady) { return; }
	UAsdaMiniStateSubsystem* Mini = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr;
	if (!Mini) { return; }

	// ── чекбокс 56264: события 67073 (взведён) / 67074 (снят) ──
	if (PinCheck)
	{
		const bool bNow = PinCheck->IsChecked();
		if (bNow != bPrevPinChecked)
		{
			bPrevPinChecked = bNow;
			// Эталон в ОБЕИХ ветках делает одно и то же: запомнить ТЕКУЩУЮ позицию окна
			//   (`Class_GetVec2_74(this[0])` = posX/posY контрола 56000 в координатах родителя)
			//   и снять авто-привязку (`this[524] = 0`), затем RefreshIcons.
			//   ★★★ БАГ БЫЛ ЗДЕСЬ: бралась ЛОКАЛЬНАЯ позиция RootHit внутри группы (GetSlotRect →
			//   (19,24)), а мувер сравнивает с GroupSpacePos = сдвиг группы + локаль. При взведении
			//   чекбокса окно уезжало в (19,24) = ЛЕВЫЙ-ВЕРХНИЙ угол (лог 2026-08-07 19:40).
			if (RootHit && RootGroup) { PinnedPos = GroupSpacePos(RootGroup, RootHit); }
			bAutoAnchor = false;
			bPinnedPosStale = false;
			bPinned = bNow;
			// ★ СИНХРОНИЗАЦИЯ УЧЁТА РАСКЛАДКИ (не эталон — у эталона сохранения позиций нет вовсе).
			//   С этого момента окно можно таскать (чекбокс снят) или оно зафиксировано пользователем
			//   (взведён) — общий механизм обязан считать драг-дельты от ФАКТИЧЕСКОЙ базы
			//   «дизайн → текущая», иначе при следующем запуске восстановит «дизайн + дельты» мимо
			//   (ровно так в ini попал мусор 2592,1783).
			if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
			{
				WM->SyncSavedWindowOffset(FName(TEXT("MINISTATE_WIN")), FName(TEXT("Soul_Boost_Win")),
					PinnedPos - DesignPos56000);
			}
			RefreshIcons();
			UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] чекбокс 56264 %s: позиция окна зафиксирована (%.0f,%.0f), авто-привязка снята"),
				bNow ? TEXT("взведён") : TEXT("снят"), PinnedPos.X, PinnedPos.Y);
		}
	}

	// ── наведение/клик по MARK_1/2/3 ──
	// ★ Гасим ТОЛЬКО СВОЙ тултип (см. шапку `EAsdaTipOwner`). До правки здесь стоял безусловный
	//   `HideSharedTip()`, и эта строка КАЖДЫЙ КАДР (пока окно хранителя закрыто — то есть почти
	//   всегда) сносила ОБЩУЮ плашку: тултип предмета/хотбара не доживал до отрисовки.
	// ★ Аудит №3 (B3): `Mouse.Reset()` — тот же самый ручной сброс латча, что стоял здесь строкой
	//   `bPrevMouseDown = false` (эквивалент сохранён дословно: латч в «кнопка была отпущена»).
	if (!bWindowShown) { HoverRow = -1; Mini->HideSharedTip(EAsdaTipOwner::SoulGuard); Mouse.Reset(); return; }
	int32 Over = -1;
	for (int32 i = 0; i < 3; ++i)
	{
		if (RowMark.IsValidIndex(i) && Mini->IsCursorOverWidget(RowMark[i])) { Over = i; break; }
	}
	if (Over != HoverRow)
	{
		HoverRow = Over;
		if (Over < 0) { Mini->HideSharedTip(EAsdaTipOwner::SoulGuard); }   // событие 0xA: KillFocus
		else
		{
			// Событие 9: тултип скилла у курсора (эталон skillShowTooltipForId(g_pSkillWnd, code)).
			//   Источник текста — та же запись skilltooltip.bin: Name (+4) и Text (+0x64).
			const int32 Code = GetRowSkillCode(Over);
			FString Title, Body;
			if (Code >= 0)
			{
				// ★ Аудит №3 (A3): свой `LoadObject` таблицы `skilltooltip` снят — её держит менеджер
				//   (таблица есть в манифесте item-БД, 1255 строк). Резолв по `Id`, который в данных
				//   совпадает с прежним именем строки-номера, поэтому строка возвращается ТА ЖЕ.
				UGameInstance* GI = GetGameInstance();
				UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
				FAsdaSkilltooltipRow TipRow;
				if (IT && IT->GetSkillTooltip(Code, TipRow)) { Title = TipRow.Name; Body = TipRow.Text; }
			}
			if (!Title.IsEmpty() || !Body.IsEmpty()) { Mini->ShowSharedTip(Title, Body, EAsdaTipOwner::SoulGuard); }
		}
	}

	// Событие 0xD: ЛКМ по иконке ряда → применить скилл (свой антиспам 250 мс, отдельный от
	//   антиспама самого TryUseSkill — в эталоне это разные глобали).
	// ★ Аудит №3 (B3): опрос ЛКМ и фронт — общая точка, ровно на месте прежнего опроса (то есть
	//   ПОСЛЕ раннего выхода по закрытому окну — иначе сброс латча выше терял бы смысл).
	Mouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ⚠ Защёлки сбрасываем: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия сошлись бы
	//   в клик, которого игрок не делал, — здесь это был бы ПОТРАЧЕННЫЙ навык Хранителя.
	//   ★ Наведение эта функция разбирает ВЫШЕ опроса (так стоял прежний опрос, порядок не трогаем),
	//     поэтому подсказку ряда гасим здесь: иначе зажжённая этим же кадром плашка повисла бы.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		if (HoverRow >= 0) { HoverRow = -1; Mini->HideSharedTip(EAsdaTipOwner::SoulGuard); }
		Mouse.Reset();
		return;
	}
	if (Mouse.bLmbDown && HoverRow >= 0)
	{
		const double Now = FPlatformTime::Seconds();
		if ((Now - LastClickTickS) * 1000.0 > 250.0)
		{
			LastClickTickS = Now;
			TryUseSkill(HoverRow);
		}
	}
	// ★ Аудит №3 (B3): запись латча делает `Mouse.Update()` выше.
}

// Вернуть окно на «авто-якорь» и пересчитать его (после правки отступов из консоли).
void UAsdaSoulGuardSubsystem::ReanchorNow()
{
	if (!EnsureControls()) { return; }
	bAutoAnchor = true;      // снова считать позицию по эталонной формуле
	bPinnedPosStale = false; // ленивый захват восстановленной позиции больше не нужен
	bLoggedPin = false;      // и один раз напечатать, что получилось
	UpdatePinnedPosition();  // пересчитать PinnedPos (и подвинуть, если чекбокс взведён)
	// Состояние чекбокса НЕ трогаем (это не наше дело — команда называется «якорь»). Но если он
	//   снят, эталонный путь окно не двигает — применяем пересчитанную позицию разово сами,
	//   иначе команда выглядела бы «ничего не сделала».
	if (PinCheck && !bPinned && RootHit && RootGroup)
	{
		ShiftGroup(RootGroup, PinnedPos - GroupSpacePos(RootGroup, RootHit));
	}
}

// ★★★ ТОЛЬКО ПОКАЗАТЬ ОКНО. Ничего больше: ни авто-привязку не снимает, ни позицию не подменяет.
//   ЗАЧЕМ ТАК СТРОГО: прежняя версия ставила окно в (400,200) и гасила авто-якорь — а юзер
//   открывает окно именно этой командой (монет в бою пока не начисляют), поэтому эталонная
//   позиция НИКОГДА не применялась, а чекбокс «не работал», потому что фиксировать было уже нечего.
//   Правило: диагностическая команда меняет ровно то, что заявлено в её имени.
void UAsdaSoulGuardSubsystem::ShowWindowForTest()
{
	if (!EnsureControls())
	{
		UE_LOG(LogAsdaSoul, Error, TEXT("[хранитель] окно не разобрано (мини-статус ещё не поднят) — показывать нечего"));
		return;
	}
	// Единственное действие: зарядить по монете, если рядов не заряжено — окно открывается
	//   штатным `hudSkillPopup_RefreshOrHide` (видно, пока есть хоть одна монета).
	if (Coin[0] < 1 && Coin[1] < 1 && Coin[2] < 1) { UpdateArrowIndicators(1, 1, 1); }
	RefreshIcons();
	RefreshOrHide();
	UpdatePinnedPosition();              // поставить туда, куда велит ЭТАЛОННЫЙ авто-якорь
	UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] показ окна: 56000 в (%.0f,%.0f), авто-якорь %s, окно %s"),
		PinnedPos.X, PinnedPos.Y, bAutoAnchor ? TEXT("ВКЛ") : TEXT("выкл"),
		IsEffectivelyVisible(RootGroup) ? TEXT("РИСУЕТСЯ") : TEXT("НЕ РИСУЕТСЯ"));
}

// Ручная установка позиции — ОТДЕЛЬНАЯ команда `asda.Soul.MoveTo` (её имя честно говорит, что она
//   двигает окно и потому снимает авто-якорь, как это делает и сам эталон при перетаскивании).
void UAsdaSoulGuardSubsystem::MoveWindowTo(const FVector2D& DesignPos)
{
	if (!EnsureControls()) { return; }
	bAutoAnchor = false;
	PinnedPos = DesignPos;
	if (RootHit && RootGroup)
	{
		const FVector2D Cur = GroupSpacePos(RootGroup, RootHit);
		ShiftGroup(RootGroup, DesignPos - Cur);
	}
	// ⚠ В ini НЕ пишем: сохранением раскладки заведует общий механизм `UAsdaWindowSubsystem`
	//   (он сам записывает сдвиг, когда окно тащат за ручки 56345/56346). Писать отсюда значило бы
	//   считать «сдвиг относительно .JUI» второй, независимой формулой — и рано или поздно разойтись
	//   с ней. Хочешь сохранить место — перетащи окно мышью.
	UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] окно перемещено вручную: 56000 в (%.0f,%.0f), авто-якорь снят ")
		TEXT("(в настройки НЕ записано — сохраняется только перетаскивание за боковые полосы)"),
		DesignPos.X, DesignPos.Y);
}

// Событие 24 у 56208 (стрелка ВВЕРХ): спрятать стрелку и все три «вспышки» шара.
void UAsdaSoulGuardSubsystem::OnArrowUpFinished()
{
	CloseHide(ArrowUp);
	SetVis(FlashRed, false); SetVis(FlashBlue, false); SetVis(FlashGreen, false);
	FlashHideAtS = 0.0;
}

// Событие 24 у 56207 (стрелка ВНИЗ).
void UAsdaSoulGuardSubsystem::OnArrowDownFinished() { CloseHide(ArrowDown); }

void UAsdaSoulGuardSubsystem::OnMarkAniFinished(int32 Row)
{
	if (RowMarkAni.IsValidIndex(Row)) { CloseHide(RowMarkAni[Row]); }
}
void UAsdaSoulGuardSubsystem::OnMarkAni1Finished() { OnMarkAniFinished(0); }
void UAsdaSoulGuardSubsystem::OnMarkAni2Finished() { OnMarkAniFinished(1); }
void UAsdaSoulGuardSubsystem::OnMarkAni3Finished() { OnMarkAniFinished(2); }

// Показать/скрыть тройку плашки вместе с её группами-контейнерами.
void UAsdaSoulGuardSubsystem::ShowBannerTriple(int32 Row, bool bGuard, bool bShow)
{
	if (!BurstGroup.IsValidIndex(Row)) { return; }
	UWidget* Grp   = bGuard ? GuardGroup[Row].Get()      : BurstGroup[Row].Get();
	UWidget* LGrp  = bGuard ? GuardLabelGroup[Row].Get() : BurstLabelGroup[Row].Get();
	UImage*  Panel = bGuard ? GuardPanel[Row].Get()      : BurstPanel[Row].Get();
	UImage*  Label = bGuard ? GuardLabel[Row].Get()      : BurstLabel[Row].Get();
	UImage*  Digit = bGuard ? GuardDigit[Row].Get()      : BurstDigit[Row].Get();
	// Группа-контейнер остаётся SelfHitTestInvisible (как её ставит генератор).
	if (Grp)  { Grp->SetVisibility(bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (LGrp) { LGrp->SetVisibility(bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	SetVis(Panel, bShow); SetVis(Label, bShow); SetVis(Digit, bShow);
}

void UAsdaSoulGuardSubsystem::SetBannerTripleAlpha(int32 Row, bool bGuard, float A)
{
	if (!BurstGroup.IsValidIndex(Row)) { return; }
	SetAlpha(bGuard ? GuardPanel[Row].Get() : BurstPanel[Row].Get(), A);
	SetAlpha(bGuard ? GuardLabel[Row].Get() : BurstLabel[Row].Get(), A);
	SetAlpha(bGuard ? GuardDigit[Row].Get() : BurstDigit[Row].Get(), A);
}

// Эталонный масштаб кадра. `g_FrameTimeScale` считается РАЗ В СЕКУНДУ как `30.0 / кадров_за_секунду`
//   (game_main.c:1488-1494), то есть это «сколько 1/30 секунды длится кадр». Значит любое эталонное
//   «X за кадр» = `X * 30` в секунду. Клампим тем же потолком 4, что и раньше (защита от лагового кадра).
float UAsdaSoulGuardSubsystem::EtalonFrameScale(float DeltaTime) const
{
	return FMath::Clamp(DeltaTime * FMath::Max(1.f, EtalonFrameFps), 0.f, 4.f);
}

// ─────────────── ★ НАДПИСИ НАД ЧУЖИМИ ИГРОКАМИ (ОТСТУПЛЕНИЕ, флаг bPeerBanners) ────────────────
//  Почему это отступление, а не порт: в оригинале комплект контролов ОДИН, и показывает их
//  единственная функция `uiZoneFadeAndNameplate_update`, которая берёт `CharManager_GetLocalPlayer`.
//  Данные же про соседей у клиента есть (6156 приходит и про них, 6159 рассылается по сектору),
//  поэтому мы рисуем ТЕ ЖЕ планы копиями. Ни одной новой картинки/размера не придумано: кисти,
//  размеры и взаимные смещения берутся с самих импортированных JUI-узлов (CacheBannerLayout).
// Позиция узла в координатах КОРНЕВОГО канваса окна (оффсеты в UMG-импорте локальны для группы).
static FVector2D SoulAbsPos(const UWidget* W)
{
	FVector2D P(0.f, 0.f);
	for (const UWidget* It = W; It; It = It->GetParent())
	{
		if (const UCanvasPanelSlot* S = AsdaUi::SlotOf(It)) { P += S->GetPosition(); }
	}
	return P;
}

void UAsdaSoulGuardSubsystem::CacheBannerLayout()
{
	if (bRelOffsetsCached) { return; }
	for (int32 i = 0; i < 3; ++i)
	{
		const UImage* BP = BurstPanel.IsValidIndex(i) ? BurstPanel[i].Get() : nullptr;
		const UImage* GP = GuardPanel.IsValidIndex(i) ? GuardPanel[i].Get() : nullptr;
		if (!BP || !GP) { return; }   // виджет ещё не готов — попробуем на следующем кадре
		const FVector2D BPa = SoulAbsPos(BP), GPa = SoulAbsPos(GP);
		BurstRelOff[i][0] = SoulAbsPos(BurstLabel[i].Get()) - BPa;
		BurstRelOff[i][1] = SoulAbsPos(BurstDigit[i].Get()) - BPa;
		GuardRelOff[i][0] = SoulAbsPos(GuardLabel[i].Get()) - GPa;
		GuardRelOff[i][1] = SoulAbsPos(GuardDigit[i].Get()) - GPa;
		FVector2D Tmp;
		GetSlotRect(BP, Tmp, BurstRelSize[i][0]);
		GetSlotRect(BurstLabel[i].Get(), Tmp, BurstRelSize[i][1]);
		GetSlotRect(BurstDigit[i].Get(), Tmp, BurstRelSize[i][2]);
		GetSlotRect(GP, Tmp, GuardRelSize[i][0]);
		GetSlotRect(GuardLabel[i].Get(), Tmp, GuardRelSize[i][1]);
		GetSlotRect(GuardDigit[i].Get(), Tmp, GuardRelSize[i][2]);
	}
	bRelOffsetsCached = true;
	UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] раскладка надписей снята с JUI: «Взрыв» %s/%s, «Хранит» %s/%s"),
		*BurstRelOff[0][0].ToString(), *BurstRelOff[0][1].ToString(),
		*GuardRelOff[0][0].ToString(), *GuardRelOff[0][1].ToString());
}

void UAsdaSoulGuardSubsystem::ClearPeerBanners()
{
	for (FPeerBanner& B : PeerBanners)
	{
		if (UImage* I = B.Panel.Get()) { I->RemoveFromParent(); }
		if (UImage* I = B.Label.Get()) { I->RemoveFromParent(); }
		if (UImage* I = B.Digit.Get()) { I->RemoveFromParent(); }
	}
	PeerBanners.Reset();
}

// Ряд (0..2) по коду АКТИВНОГО скилла хранителя. Связка только ИЗ ДАННЫХ: SoulGuarderTable
//   +152 GuardianSkillId → +8 RowIndex (формула «1228 + i» врёт на 6 записях, см. отчёт §13).
// ★★★ П15: «это скилл ряда Хранителя?» — ПО ДАННЫМ (закон «код из данных»).
//   1) `SoulGuarderTable.BIN +152 GuardianSkillId` — прямая принадлежность ряду;
//   2) `SkillCoinTable.bin +16 Field16` ∈ {1,2,3} — эталонный признак «у скилла есть боевой мотион
//      Хранителя» (`sub_EF3FA0` → `rec[+16]`, читается в `entity_char.c:61316-61325`); −1 у обычных.
//   3) фолбэк по диапазону — только пока ни одна таблица не загружена.
bool UAsdaSoulGuardSubsystem::IsGuardianSkillCode(int32 SkillCode) const
{
	for (const FAsdaSoulGuarderRow& R : GuarderTable)
	{
		if (R.GuardianSkillId == SkillCode) { return true; }
	}
	FAsdaSkillCoinRow CoinRow;   // ★ имя НЕ `Coin`: одноимённое поле класса (счётчик монет) — C4458
	if (FindSkillCoin(SkillCode, CoinRow))
	{
		return (CoinRow.Field16 >= 1 && CoinRow.Field16 <= 3);
	}
	if (GuarderTable.Num() == 0 && SkillCoinCount() == 0)
	{
		// Таблиц ещё нет — иначе ряд Хранителя молча перестал бы работать до их загрузки.
		return SkillCode >= 1228 && SkillCode <= 1254;
	}
	return false;
}

bool UAsdaSoulGuardSubsystem::RowByGuardianSkill(int32 SkillCode, int32& OutRow) const
{
	for (const FAsdaSoulGuarderRow& R : GuarderTable)
	{
		if (R.GuardianSkillId == SkillCode) { OutRow = FMath::Clamp(R.RowIndex, 0, 2); return true; }
	}
	return false;
}

void UAsdaSoulGuardSubsystem::ShowPeerBanner(AActor* Owner, int32 CharId, int32 UserIdx, int32 Row, bool bGuard)
{
	if (!bPeerBanners || !Owner || !MiniWidget || !MiniWidget->WidgetTree) { return; }
	CacheBannerLayout();
	if (!bRelOffsetsCached) { return; }
	Row = FMath::Clamp(Row, 0, 2);
	UCanvasPanel* Canvas = TextsGroup ? Cast<UCanvasPanel>(TextsGroup->GetParent()) : nullptr;
	if (!Canvas) { return; }

	// Один игрок = одна надпись: новая заменяет старую (как эталон гасит предыдущую тройку).
	for (int32 n = PeerBanners.Num() - 1; n >= 0; --n)
	{
		if (PeerBanners[n].Owner.Get() != Owner) { continue; }
		if (UImage* I = PeerBanners[n].Panel.Get()) { I->RemoveFromParent(); }
		if (UImage* I = PeerBanners[n].Label.Get()) { I->RemoveFromParent(); }
		if (UImage* I = PeerBanners[n].Digit.Get()) { I->RemoveFromParent(); }
		PeerBanners.RemoveAt(n);
	}

	UImage* Src[3] = {
		bGuard ? GuardPanel[Row].Get() : BurstPanel[Row].Get(),
		bGuard ? GuardLabel[Row].Get() : BurstLabel[Row].Get(),
		bGuard ? GuardDigit[Row].Get() : BurstDigit[Row].Get() };
	if (!Src[0] || !Src[1] || !Src[2]) { return; }

	FPeerBanner B;
	B.Owner = Owner; B.CharId = CharId; B.UserIdx = UserIdx; B.Row = Row; B.bGuard = bGuard;
	UImage* Made[3] = { nullptr, nullptr, nullptr };
	static int32 PeerSerial = 0;
	++PeerSerial;
	for (int32 k = 0; k < 3; ++k)
	{
		UImage* Img = MiniWidget->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),
			FName(*FString::Printf(TEXT("SoulPeerBanner_%d_%d"), PeerSerial, k)));
		if (!Img) { return; }
		Img->SetBrush(Src[k]->GetBrush());          // кисть = ТОТ ЖЕ JUI-план (текстура + UV + размер)
		Img->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		Img->SetRenderOpacity(0.f);
		if (UCanvasPanelSlot* S = Canvas->AddChildToCanvas(Img))
		{
			S->SetAutoSize(false);
			S->SetAlignment(FVector2D(0.f, 0.f));
			S->SetSize(bGuard ? GuardRelSize[Row][k] : BurstRelSize[Row][k]);
			S->SetZOrder(0);
		}
		Made[k] = Img;
	}
	B.Panel = Made[0]; B.Label = Made[1]; B.Digit = Made[2];
	PeerBanners.Add(B);
	UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] надпись над ЧУЖИМ «%s»: %s %d (charId=%d, userIdx=%d)"),
		*Owner->GetName(), bGuard ? TEXT("Душа Хранит") : TEXT("Душа Взрыв"), Row + 1, CharId, UserIdx);
}

void UAsdaSoulGuardSubsystem::TickPeerBanners(float DeltaTime)
{
	if (PeerBanners.Num() == 0) { return; }
	const float FrameScale = EtalonFrameScale(DeltaTime);
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const float Scale = FMath::Max(0.01f, MiniWidget ? MiniWidget->GetCachedGeometry().Scale : 1.f);
	const UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;

	for (int32 n = PeerBanners.Num() - 1; n >= 0; --n)
	{
		FPeerBanner& B = PeerBanners[n];
		AActor* Own = B.Owner.Get();
		UImage* Pn = B.Panel.Get();
		bool bKill = (!Own || !Pn || !PC);
		if (!bKill)
		{
			// Фейд 1:1 с эталоном: 0 → 500 (показ клампится к 255), затем 255 → 0.
			if (B.bFadeOut)
			{
				B.Alpha -= BannerFadeStep * FrameScale;
				if (B.Alpha < 0.f) { bKill = true; }
			}
			else
			{
				B.Alpha += BannerFadeStep * FrameScale;
				if (B.Alpha >= BannerFadePeak) { B.bFadeOut = true; B.Alpha = 255.f; }
			}
		}
		if (bKill)
		{
			if (UImage* I = B.Panel.Get()) { I->RemoveFromParent(); }
			if (UImage* I = B.Label.Get()) { I->RemoveFromParent(); }
			if (UImage* I = B.Digit.Get()) { I->RemoveFromParent(); }
			PeerBanners.RemoveAt(n);
			continue;
		}
		const float A = FMath::Clamp(B.Alpha, 0.f, 255.f) / 255.f;

		// Высота — та же формула, что и у своей плашки (эталон ui_core.c:92455-92464).
		const AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(Own);
		const bool bHasGuardian = WS && (WS->FindSoulGuardianByOwner(Own) != nullptr);
		const float Z = BannerBaseZ + (bHasGuardian ? BannerGuardianZ : BannerFootZ)
		              + (Ch ? Ch->MountHeightOffset : 0.f)
		              + (Ch ? Ch->GetOverheadLiftZ() : 0.f);   // ★ 24.10.2026 [ПОРТ]: в прыжке — вместе с телом (asda.Jump.OverheadFollow)
		FVector2D Screen;
		if (!UGameplayStatics::ProjectWorldToScreen(PC, Own->GetActorLocation() + FVector(0, 0, Z), Screen, false))
		{
			// За камерой — просто не рисуем этот кадр (надпись продолжает гаснуть).
			for (UImage* I : { B.Panel.Get(), B.Label.Get(), B.Digit.Get() })
			{
				if (I) { I->SetRenderOpacity(0.f); }
			}
			continue;
		}
		const FVector2D PanelSz = B.bGuard ? GuardRelSize[B.Row][0] : BurstRelSize[B.Row][0];
		const FVector2D Base(Screen.X / Scale - PanelSz.X * 0.5f, Screen.Y / Scale - PanelSz.Y * 0.5f);
		const FVector2D* Off = B.bGuard ? GuardRelOff[B.Row] : BurstRelOff[B.Row];
		UImage* Parts[3] = { B.Panel.Get(), B.Label.Get(), B.Digit.Get() };
		const FVector2D Pos[3] = { Base, Base + Off[0], Base + Off[1] };
		for (int32 k = 0; k < 3; ++k)
		{
			if (!Parts[k]) { continue; }
			if (UCanvasPanelSlot* S = AsdaUi::SlotOf(Parts[k])) { S->SetPosition(Pos[k]); }
			Parts[k]->SetRenderOpacity(A);
		}
	}
}

// ── ПРОВЕРКА БЕЗ СЕРВЕРА ─────────────────────────────────────────────────────────────────────────
FString UAsdaSoulGuardSubsystem::DebugShowBanner(bool bGuard, int32 Level, int32 PeerKey)
{
	if (!EnsureControls()) { return TEXT("Хранитель: окно MINISTATE_WIN ещё не создано."); }
	const int32 Row = FMath::Clamp(Level, 1, 3) - 1;
	const TCHAR* Word = bGuard ? TEXT("Душа Хранит") : TEXT("Душа Взрыв");
	if (PeerKey == 0)
	{
		// Свой игрок — та же тройка контролов, что показывает эталон.
		if (BannerRow >= 0) { ShowBannerTriple(BannerRow, bBannerIsGuard, false); }
		BannerRow = Row; bBannerIsGuard = bGuard;
		ShowBannerTriple(Row, bGuard, true);
		SetBannerTripleAlpha(Row, bGuard, 0.f);
		BannerAlpha = 0.f; bBannerFadeOut = false;
		// Чтобы эталонный триггер BURST не «съел» показ на следующем кадре.
		if (!bGuard) { ActiveBurstSlot = Row; for (int32 j = 0; j < 3; ++j) { BurstCache[j] = Coin[j]; } }
		else { LastBannerGuardIndex = SoulGuardIndex; }
		return FString::Printf(TEXT("Хранитель: над СВОИМ игроком — «%s %d» (тройка %d/%d/%d)"),
			Word, Row + 1, (bGuard ? 56204 : 56201) + Row, (bGuard ? 56214 : 56211) + Row,
			(bGuard ? 56224 : 56221) + Row);
	}
	UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	AActor* Peer = WS ? WS->FindPlayerActorByCharId(PeerKey) : nullptr;
	if (!Peer && WS) { Peer = WS->ActorByUserIdx(PeerKey); }
	if (!Peer)
	{
		return FString::Printf(TEXT("Хранитель: игрок с ключом %d в мире не найден (ни charId, ни userIdx)."), PeerKey);
	}
	const bool bSaved = bPeerBanners;
	bPeerBanners = true;                      // ручная проверка работает и при выключенном отступлении
	ShowPeerBanner(Peer, PeerKey, PeerKey, Row, bGuard);
	bPeerBanners = bSaved;
	return FString::Printf(TEXT("Хранитель: над «%s» — «%s %d»"), *Peer->GetName(), Word, Row + 1);
}

FString UAsdaSoulGuardSubsystem::DumpBanners() const
{
	FString S = TEXT("══ НАДПИСИ ХРАНИТЕЛЯ ══\n");
	S += FString::Printf(TEXT("  СВОЯ: %s, альфа %.0f/255 (%s), эталонный слот BURST %d, кэш рядов %d/%d/%d\n"),
		BannerRow < 0 ? TEXT("нет")
		              : *FString::Printf(TEXT("%s %d"), bBannerIsGuard ? TEXT("Душа Хранит") : TEXT("Душа Взрыв"), BannerRow + 1),
		BannerAlpha, bBannerFadeOut ? TEXT("гаснет") : TEXT("проявляется"), ActiveBurstSlot,
		BurstCache.IsValidIndex(0) ? BurstCache[0] : -1, BurstCache.IsValidIndex(1) ? BurstCache[1] : -1,
		BurstCache.IsValidIndex(2) ? BurstCache[2] : -1);
	S += FString::Printf(TEXT("  Монеты %d/%d/%d, порог показа «Взрыв» = %d, SoulGuardIndex = %d\n"),
		Coin.IsValidIndex(0) ? Coin[0] : -1, Coin.IsValidIndex(1) ? Coin[1] : -1,
		Coin.IsValidIndex(2) ? Coin[2] : -1, AvailableThreshold, SoulGuardIndex);
	S += FString::Printf(TEXT("  ЧУЖИЕ (отступление, bPeerBanners=%d / каст=%d / свой-каст=%d): %d шт\n"),
		bPeerBanners ? 1 : 0, bPeerBurstOnCast ? 1 : 0, bBurstOnCast ? 1 : 0, PeerBanners.Num());
	for (const FPeerBanner& B : PeerBanners)
	{
		S += FString::Printf(TEXT("    %s — %s %d, альфа %.0f (%s), charId=%d userIdx=%d\n"),
			B.Owner.IsValid() ? *B.Owner->GetName() : TEXT("<нет актора>"),
			B.bGuard ? TEXT("Душа Хранит") : TEXT("Душа Взрыв"), B.Row + 1,
			B.Alpha, B.bFadeOut ? TEXT("гаснет") : TEXT("проявляется"), B.CharId, B.UserIdx);
	}
	S += FString::Printf(TEXT("  Масштаб кадра эталона: %.0f (g_FrameTimeScale = 30/FPS), шаг %.0f, пик %.0f\n"),
		EtalonFrameFps, BannerFadeStep, BannerFadePeak);
	return S;
}

// `uiZoneFadeAndNameplate_update@0xEF38E0` (ui_core.c:92336) — плашка «SOUL BURST» / «SOUL GUARDER»
//   над головой игрока + её фейд.
void UAsdaSoulGuardSubsystem::UpdateBanner(float DeltaTime)
{
	if (!bControlsReady) { return; }
	// ★★★ Масштаб времени = ЭТАЛОННЫЙ `g_FrameTimeScale = 30.0 / FPS` (см. EtalonFrameFps).
	const float FrameScale = EtalonFrameScale(DeltaTime);

	// 1. Фейд текущей тройки (эталонные g_flFadeAlpha / g_bScreenFadeOut).
	if (BannerRow >= 0)
	{
		SetBannerTripleAlpha(BannerRow, bBannerIsGuard, FMath::Clamp(BannerAlpha, 0.f, 255.f) / 255.f);
		if (bBannerFadeOut)
		{
			BannerAlpha -= BannerFadeStep * FrameScale;
			if (BannerAlpha < 0.f)
			{
				BannerAlpha = 0.f;
				ShowBannerTriple(BannerRow, bBannerIsGuard, false);
				BannerRow = -1;
			}
		}
		else
		{
			BannerAlpha += BannerFadeStep * FrameScale;
			if (BannerAlpha >= BannerFadePeak) { bBannerFadeOut = true; BannerAlpha = 255.f; }
		}
	}

	auto SwitchTo = [this](int32 Row, bool bGuard)
	{
		if (BannerRow >= 0) { ShowBannerTriple(BannerRow, bBannerIsGuard, false); }
		BannerRow = Row; bBannerIsGuard = bGuard;
		ShowBannerTriple(Row, bGuard, true);
		SetBannerTripleAlpha(Row, bGuard, 0.f);
		BannerAlpha = 0.f; bBannerFadeOut = false;
	};

	// 2. Триггер «SOUL BURST»: ряд достиг порога 5 (эталон: обход i = 2 → 0, первый сработавший).
	if (Coin[0] < AvailableThreshold && Coin[1] < AvailableThreshold && Coin[2] < AvailableThreshold)
	{
		ActiveBurstSlot = -1;
	}
	// Эталон: BURST не перебивает уже висящую плашку GUARDER.
	const bool bGuardBannerUp = (BannerRow >= 0 && bBannerIsGuard);
	if (!bGuardBannerUp)
	{
		for (int32 i = 2; i >= 0; --i)
		{
			if (BurstCache[i] != Coin[i] && Coin[i] >= AvailableThreshold)
			{
				for (int32 j = 0; j < 3; ++j) { BurstCache[j] = Coin[j]; }
				if (ActiveBurstSlot != i)
				{
					ActiveBurstSlot = i;
					SwitchTo(i, /*bGuard=*/false);
					UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] плашка SOUL BURST ряд %d (монет %d)"), i + 1, Coin[i]);
				}
				break;
			}
		}
	}

	// 3. Триггер «SOUL GUARDER»: сменился призванный хранитель (поле из 6156).
	if (LastBannerGuardIndex != SoulGuardIndex)
	{
		LastBannerGuardIndex = SoulGuardIndex;
		if (const FAsdaSoulGuarderRow* R = FindGuarderByIndex(SoulGuardIndex))
		{
			const int32 Ri = FMath::Clamp(R->RowIndex, 0, 2);
			SwitchTo(Ri, /*bGuard=*/true);
			UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] плашка SOUL GUARDER: индекс %d («%s»), ряд %d"),
				SoulGuardIndex, *R->Name, Ri + 1);
		}
	}

	// 3б. ★ Надписи над ЧУЖИМИ игроками (отступление; в эталоне их нет — см. bPeerBanners).
	//     Ведём ДО раннего выхода ниже, иначе они замирали бы вне мира.
	TickPeerBanners(DeltaTime);

	// 4. Позиция плашки 56200 — экранная проекция точки над игроком.
	const AAsdaPlayerCharacter* Ch = GetLocalChar();
	APlayerController* PC = Ch ? Cast<APlayerController>(Ch->GetController()) : nullptr;
	if (!Ch || !PC || !TextsHit || !TextsGroup) { return; }
	// Эталон ui_core.c:92455-92464: +30, затем +400 при НАЛИЧИИ 3D-КОМПАНЬОНА (иначе +250),
	//   затем ОТДЕЛЬНО высота транспорта. См. §10 soul_guardian_companion_re.md.
	const UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	// ★ Реестр компаньонов ключуется charId (ключ 6156); офлайн-эмуляции подхватит фолбэк по userIdx.
	const UAsdaNetSubsystem* NetB = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const int32 SelfGuardKey = (NetB && NetB->SelectedCharId != 0) ? NetB->SelectedCharId : SelfUserIdx();
	const bool bHasGuardian = WS && WS->HasSoulGuardian(SelfGuardKey);
	float Z = BannerBaseZ + (bHasGuardian ? BannerGuardianZ : BannerFootZ) + Ch->MountHeightOffset
	        + Ch->GetOverheadLiftZ();   // ★ 24.10.2026 [ПОРТ]: в прыжке — вместе с телом (asda.Jump.OverheadFollow)
	FVector2D Screen;
	if (!UGameplayStatics::ProjectWorldToScreen(PC, Ch->GetActorLocation() + FVector(0, 0, Z), Screen, false)) { return; }
	// Экранные пиксели → «дизайн-пиксели» канваса импортёра (тот же масштаб, что у самого виджета).
	const float Scale = FMath::Max(0.01f, MiniWidget ? MiniWidget->GetCachedGeometry().Scale : 1.f);
	FVector2D PanelSz(111, 46), Tmp;
	GetSlotRect(BurstPanel[0], Tmp, PanelSz);
	const FVector2D Target(Screen.X / Scale - PanelSz.X * 0.5f, Screen.Y / Scale - PanelSz.Y * 0.5f);
	const FVector2D Cur = GroupSpacePos(TextsGroup, TextsHit);   // группа + локаль хит-зоны 56200
	const FVector2D Delta = Target - Cur;
	if (Delta.SizeSquared() > 0.25f) { ShiftGroup(TextsGroup, Delta); }
}

// `CharFx_CreateSkillCastEffect@0xEF50E0` (effects_particles.c:55463): создать летящую монету.
//   Эталон создаёт объект эффекта (jeb `eff_1_char_99_soulritec-a_03.jeb`) и играет 3D-звук
//   `S_e_sb_coin_00.wav` в точке игрока/цели. Здесь — модель полёта; сам .jeb цепляет
//   подсистема эффектов мира (её вызов ниже помечен).
void UAsdaSoulGuardSubsystem::SpawnFlyingCoin(int32 C0, int32 C1, int32 C2, int32 SkillId)
{
	// Эталон: эффект создаётся ТОЛЬКО если хоть один счётчик ненулевой (`if (posX || posY || posZ)`).
	if (C0 == 0 && C1 == 0 && C2 == 0) { return; }
	const AAsdaPlayerCharacter* Ch = GetLocalChar();
	if (!Ch)
	{
		// ★ Отступление: без локального персонажа монету создавать не из чего. Эталон в этом случае
		//   теряет пакет целиком; мы применяем счётчики сразу (флаг bApplyCoinsWithoutFlight).
		if (bApplyCoinsWithoutFlight) { UpdateArrowIndicators(C0, C1, C2); RefreshIcons(); }
		return;
	}
	FAsdaSoulFlyingCoin NewCoin;
	// ★★★ МОНЕТА ВЫЛЕТАЕТ ИЗ ЦЕЛИ, А НЕ ИЗ ИГРОКА. Эталон `CharFx_CreateSkillCastEffect@0xEF50E0`
	//   (`effects_particles.c:55516-55552`) кладёт в `effectObj[544..546]` (= `coinEntry + 2176`,
	//   стартовая позиция монеты в `coinDropMgr_UpdateFlyToPlayer`) ТРИ варианта по порядку:
	//     1) `Entity_FindByRenderId_thunk(g_pCharManager, localPlayer->targetRenderId)` → `ent + 436`
	//        (позиция СУЩНОСТИ-цели, т.е. моба);
	//     2) иначе `CharManager_FindByUserIdx(targetRenderId)` → `soundPosX..Z` (цель-ИГРОК);
	//     3) и только если ни того, ни другого нет — `localPlayer->soundPosX..Z`.
	//   Порт всегда брал позицию игрока ⇒ старт совпадал с целью полёта, монета «долетала» мгновенно
	//   и анимации не было (лог живого теста: «летящая монета» и «монета долетела» с разницей 2 мс).
	//   ★ Там же эталон играет `S_e_sb_coin_00.wav` — звук ВЫЛЕТА (звук ПРИЛЁТА `…coin_01` уже есть).
	NewCoin.Pos = Ch->GetActorLocation();
	{
		bool bTgtIsMob = false;
		if (const AActor* Tgt = ResolveCoinSourceTarget(bTgtIsMob))
		{
			NewCoin.Pos = Tgt->GetActorLocation();
		}
	}
	NewCoin.Accel = 0.f;      // эталон effectObj[547] = 0
	NewCoin.UpSpeed = 15.f;   // эталон effectObj[548] = 0x41700000 = 15.0f
	NewCoin.Speed = 0.f;      // эталон effectObj[549] = 0
	NewCoin.Coin0 = C0; NewCoin.Coin1 = C1; NewCoin.Coin2 = C2; NewCoin.SkillId = SkillId;
	FlyingCoins.Add(NewCoin);
	UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] летящая монета: скилл %d → монеты %d/%d/%d (в полёте %d)"),
		SkillId, C0, C1, C2, FlyingCoins.Num());
}

// ★ Откуда ВЫЛЕТАЕТ монета — порт эталонного порядка `CharFx_CreateSkillCastEffect@0xEF50E0`
//   (`effects_particles.c:55520-55545`): сущность по `targetRenderId` → игрок по тому же индексу →
//   nullptr («ни того, ни другого» = эталон берёт позицию самого игрока, это делает вызывающий).
//   Источник индекса — `localPlayer->targetRenderId` (+0x32DFC, effects_particles.c:55521), тот же, что у боя/навыков
//   (★ 27.09.2026: `Target_GetSelectedUserIdx` — миснейм IDA, = `Char_GetHoveredUserIdx@0x966490`, возвращает userNum ПЕРЕДАННОГО персонажа) (ЧТЕНИЕ чужой
//   подсистемы, своего реестра целей не заводим — закон дедупа).
const AActor* UAsdaSoulGuardSubsystem::ResolveCoinSourceTarget(bool& bOutIsMob) const
{
	bOutIsMob = false;
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return nullptr; }
	const UAsdaSkillUseSubsystem* Use = GI->GetSubsystem<UAsdaSkillUseSubsystem>();
	UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
	if (!Use || !WS) { return nullptr; }
	int32 TType = 0, TIdx = -1;
	FVector2D TPos = FVector2D::ZeroVector;
	float TDist = 0.f;
	if (!Use->GetSelectedTarget(TType, TIdx, TPos, TDist) || TIdx < 0) { return nullptr; }
	// ★ Аудит №3 (B6): развилка «персонаж/сущность» — единой точкой. `TType` диспетчера целей идёт
	//   по конвенции ЗАПИСИ КАСТА (1 = персонаж), а НЕ по конвенции глобали ввода эталона (там 1 =
	//   сущность) — именно поэтому сырой байт в `ActorByTarget` не передаётся, а переводится здесь.
	const EAsdaTargetKind Kind = AsdaTargetKindFromHitRec(TType);
	bOutIsMob = (Kind == EAsdaTargetKind::Entity);
	return WS->ActorByTarget(Kind, TIdx);
}

// `coinDropMgr_UpdateFlyToPlayer@0xEF5BC0` (math_geom.c:1954) 1:1.
void UAsdaSoulGuardSubsystem::TickFlyingCoins(float DeltaTime)
{
	if (FlyingCoins.Num() == 0) { return; }
	// ★ Тот же `g_FrameTimeScale = 30 / FPS`, что и у плашки (math_geom.c:2006-2036 — все
	//   коэффициенты разгона монеты умножаются ровно на него). Было `dt * 60` = вдвое быстрее.
	const float FrameScale = EtalonFrameScale(DeltaTime);
	const float ArriveDist = 30.f * FrameScale;

	for (int32 n = FlyingCoins.Num() - 1; n >= 0; --n)
	{
		FAsdaSoulFlyingCoin& C = FlyingCoins[n];
		const FVector ToTarget = CoinTargetWorld - C.Pos;
		const float Dist = ToTarget.Size();
		if (Dist >= ArriveDist)
		{
			// Разгон: вертикальная составляющая гаснет, горизонтальная растёт, общий множитель → 1.
			if (C.UpSpeed > 0.f) { C.UpSpeed -= 0.1f * FrameScale; }
			if (C.Speed < 30.f)  { C.Speed   += 1.0f * FrameScale; }
			if (C.Accel < 1.f)   { C.Accel   += 0.1f * FrameScale; }
			const FVector Dir = ToTarget.GetSafeNormal();
			const FVector Up  = FVector::UpVector;
			C.Pos += (Dir * (C.Speed * C.Accel) + Up * (C.UpSpeed * C.Accel)) * (2.0f * FrameScale);
			continue;
		}
		// ★★★ ПРИЛЁТ: именно здесь эталон применяет новые счётчики, играет `S_e_sb_coin_01.wav`
		//   и перерисовывает окно. До прилёта шары НЕ меняются — это анимация, а не дефект.
		UE_LOG(LogAsdaSoul, Log, TEXT("[хранитель] монета долетела: применяем %d/%d/%d"),
			C.Coin0, C.Coin1, C.Coin2);
		UpdateArrowIndicators(C.Coin0, C.Coin1, C.Coin2);
		RefreshIcons();
		FlyingCoins.RemoveAt(n);
		// ★ Эмулятор: у сервера пересчёт уровня идёт сразу за начислением; у клиента счётчики
		//   применяются по прилёту монеты — здесь и догоняем (при живом сервере 6156 придёт сам).
		if (bSimulateServer) { SimRecomputeGuard(); }
	}
}
