// Copyright (c) 2026 @sitxovski (Discord)
// ═══════════════════════════════════════════════════════════════════════════════════════════════
// Порт рулетки соулмейта. Полный структурный разбор окна 67000, поток и раскладка клиентской
//   таблицы — в шапке `AsdaSoulRouletteSubsystem.h`.
//
// ═════════════════════ ТАБЛИЦА НОТИСОВ «СОБЫТИЕ → loc → КАНАЛ» (рулетка) ═══════════════════════
//   6576 код 10 (близость < 200 и сегодня уже крутил) → loc 8081 · ЧАТ · цвет −65536 (красный)
//                                                       `CChat_AddTokenizedLines(..., −65536, 10000, 0)`
//   6576 код 11 (партнёр офлайн / другой канал / далеко)→ loc 8167 · ЧАТ · цвет −65536
//   6576 код 12                                        → ★ ДЕФЕКТ D3: сервер шлёт, клиент МОЛЧИТ
//   6580 приз 0 «пусто»                                → loc 8071 · ЧАТ · цвет −11514 (жёлтый)
//   6580 приз 1 «опыт»    (printf: значение/10, пакет@6)→ loc 8072 · ЧАТ · −11514
//   6580 приз 2 «предмет» (printf: имя, количество)    → loc 8073 · ЧАТ · −11514
//   6580 приз 3 «ОЗ/ОМ»   (printf: процент)            → loc 8074 · ЧАТ · −11514
//   старт вращения                                     → без текста, звук `S_e_sm_ro_start_00.wav` (2D)
//   готовность панели (ветка a3 == 1)                  → без текста, звук `S_e_sm_ro_ready_00.wav` (2D)
//   выдача приза                                       → звук из таблицы (+84+64·s): свой — 2D, чужой — 3D
//   ★ Строка приза (6580) уходит в чат ТОЛЬКО победителю: `if (pkt+1 == g_LocalPlayer+1052)`.
//
// ═══════════════════════════════ НУМПАД (окно 1410) — НЕ УЧАСТВУЕТ ═════════════════════════════
//   Ставок и количеств у рулетки нет: валюта входа — очки близости соулмейта (порог 200 в сутки),
//   и всё это считает СЕРВЕР. Проверено теми же двумя методами, что и у навыков Дружбы
//   (перечень мест `uiOpenContextPopupWithHandlers` в `Docs/Port/systems/NUMPAD_FLOW.md` + grep по `social.c`).
//
// ══════════════════════════ ДЕФЕКТЫ ОРИГИНАЛА: ЧТО 1:1, ЧТО ЧИНИМ ══════════════════════════════
//   D3 (6576 код 12 без текста) — 1:1 по умолчанию; `asda.Soul.Roulette.FixSilentCode12 1` даёт строку 990346.
//   D6 (текст приза из клиентской таблицы по локальному углу) — 1:1 по умолчанию;
//       `asda.Soul.Roulette.TextFromServer 1` строит текст по виду приза из пакета 6580.
//   D9 (наше обозначение, найдено при портировании): в `RoulettePanel_UpdateRewardSlots`
//       (item_inventory.c:56866-56875) ветка `if (rec[+68+4s] > -1 && тип == 1) { тип = 0; U = 64; }`
//       ПЕРЕКРЫВАЕТ ветку `else if (тип == 1) U = ((значение−1)/10) << 6`. На живых данных
//       включённость у опытных секторов положительная, поэтому вторая ветка не исполняется НИКОГДА,
//       и все опытные сектора рисуются одной клеткой (64, 0). Воспроизводится 1:1;
//       `asda.Soul.Roulette.FixExpIcon 1` включает «правильную» ветку.
#include "UI/AsdaSoulRouletteSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaSelf.h"   // ★ В2-A дедупа №2: общая точка «это я?» (класс/уровень/charId/userNum)
#include "UI/AsdaUiHelpers.h"   // ★ U20: AsdaUi::BindButton
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaDataTableCache.h"
#include "AsdaEffectActor.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaAnimatedPlane.h"   // кадровая анимация плана JUI: TotalFrame/FrameInterval из скина
#include "UI/AsdaSoulAppleSubsystem.h"   // ★ волна 1-Б: флаг «яблоко созрело» (+495) для кнопки 67002
#include "Login/AsdaButtonRelay.h"        // реле клика кнопки 67002
#include "Components/Button.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanel.h"       // ★ ревизия 1-Б М-3: сдвиг окна к якорю 6699
#include "Components/CanvasPanelSlot.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"
#include "UI/AsdaNoticeColors.h"   // ★ R5-c (12.11.2026): AsdaNoticeColor::Gold — золотой цвет нотисов (общая точка)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaRoulette, Log, All);

const TCHAR* UAsdaSoulRouletteSubsystem::kPrizeTablePath =
	TEXT("/Game/AsdaAssets/Data/DT_Soulmate_Roulette.DT_Soulmate_Roulette");
const TCHAR* UAsdaSoulRouletteSubsystem::kWindowWbp =
	TEXT("/Game/AsdaAssets/UI/Player_UI/Soulmate/WBP_GOLD_roullete_WIN.WBP_GOLD_roullete_WIN_C");
const FName UAsdaSoulRouletteSubsystem::kWindowId(TEXT("GOLD_roullete_WIN"));

namespace
{
	const FColor kTipYellow = AsdaNoticeColor::Gold;   // −11514   ★ R5-c (12.11.2026): значение — общая точка `AsdaNoticeColor::Gold` (было своё `FColor(0xFF, 0xD3, 0x06, 0xFF)`, те же байты).
	const FColor kChatRed  (0xFF, 0x00, 0x00, 0xFF);   // −65536

	// Свои строки — только в диапазоне > 990000 (закон о собственных loc-id).
	constexpr int32 kLocOwnRefuse12 = 990346;   // «Рулетка: сервер отказал (код 12)»; ★ 19.09.2026: был 990201 — занят окном настроек («Страница панели 1»), перенесён на свободный

	// Звуки эталона (имена — литералы клиента, `CSoundFlag_PlaySound2D`).
	const TCHAR* kWavSpinStart = TEXT("S_e_sm_ro_start_00.wav");
	const TCHAR* kWavReady     = TEXT("S_e_sm_ro_ready_00.wav");

	// Эффекты приза (`Effect_SpawnByName`, привязка 0 = статично в точке победителя).
	const TCHAR* kJebByKind[4] = { TEXT("Roul_none"), TEXT("Roul_Exp"), TEXT("Roul_Item"), TEXT("Roul_HPMP") };

	static ASDA_DEV_CVAR(int32, CVarFixSilentCodes,
		TEXT("asda.Soul.Roulette.FixSilentCode12"), 0,
		TEXT("Дефект D3: 0 — код 12 пакета 6576 остаётся без текста (как в оригинале); ")
		TEXT("1 — показывать служебную строку 990346."));

	static ASDA_DEV_CVAR(int32, CVarTextFromServer,
		TEXT("asda.Soul.Roulette.TextFromServer"), 0,
		TEXT("Дефект D6: 0 — текст приза строится по КЛИЕНТСКОЙ таблице и локальному углу колеса ")
		TEXT("(как в оригинале, при расхождении таблиц «показал одно, дал другое»); ")
		TEXT("1 — строить по виду приза из пакета 6580 (авторитет — сервер)."));

	// ★ Ревизия 1-Б М-3: эталонная ветка `a3 == 1` `RoulettePanel_UpdateRewardSlots` ставит окно 67000 у якоря 6699
	//   (`Soulmate_Glass` окна 6500) при КАЖДОМ показе: X = якорь.X + 57 − ширина(67050) + 5, Y = якорь.Y − 75
	//   (`uiCalcAnchoredScreenPos` + `CControl_SetField74_75` + `CClass_MapInsert` — поверх).
	static ASDA_DEV_CVAR(int32, CVarAnchorPos,
		TEXT("asda.Soul.Roulette.AnchorPos"), 1,
		TEXT("1 (по умолчанию, как оригинал) — при показе окно 67000 встаёт у панели «Яблока Дружбы» (6699, окно 6500); ")
		TEXT("0 — позицию держит общая система окон."));

	static ASDA_DEV_CVAR(int32, CVarFixExpIcon,
		TEXT("asda.Soul.Roulette.FixExpIcon"), 0,
		TEXT("Дефект D9: 0 — опытные сектора рисуются клеткой (64,0), как получается у оригинала; ")
		TEXT("1 — по формуле ((значение−1)/10)<<6, которую оригинал перекрыл своей же веткой."));

	// ★★★ КЛЮЧ `asda.Soul.Roulette.PlateAnimSec` УБРАН 31.08.2026: длительность анимации плашки
	//   ДОКАЗАНА и берётся ИЗ ДАННЫХ СКИНА, а не из параметра. Полный вывод — у `StartPlateAnim`.
	//   (1) `FrameInterval` скина — длительность ВСЕГО прогона в МИЛЛИСЕКУНДАХ, а не период кадра:
	//       `CControl_BuildPlaneVertices@0x12DE440` считает `frame = elapsed / FrameInterval *
	//       (TotalFrame − 1) + StartFrame` (порт формулы — `UAsdaAnimatedPlane::TickAnim`).
	//   (2) Читать скин отдельно НЕ НАДО (закон №0): импортёр JUI уже создаёт планам с
	//       `Animate = "1"` виджет `UAsdaAnimatedPlane` и кладёт в него `TotalFrame`/`FrameInterval`
	//       из скина (`Tools/asda_jui_generic_umg.py`, ветка `add_animated_plane`). Срок берём
	//       у ЖИВОГО виджета; константы ниже — только страховка, если плана в сборке нет.
	//   (3) Прогонов РОВНО ДВА: `ctrlShowWithItemFlags(ctrl, 2)` ставит лимит `gap_220[32] = 2`.
	constexpr int32 kPlateAnimLoops = 2;      // a2 в `ctrlShowWithItemFlags(ctrl, 2)` (social.c:6864/6869)
	constexpr float kAniPlateMs     = 700.f;  // скины `GOLD_roullete_ar_B_*` (67007…67010), FrameInterval
	constexpr float kClaimPlateMs   = 1000.f; // скин `Extract_Icon_Effect` (67015…67018), JuiSkin.ini:25956

	// ★★★ ИМЯ СКИНА контролов 67015…67018 (`gamemain_jui.ini:310484` и далее, ключ `Name`).
	//   Импортёр порта строит имя виджета безымянного контрола как `san(скин)_{индекс}`
	//   (`Tools/asda_jui_generic_umg.py`, `base = "%s_%d" % (san(node.idc or node.name), idx)`),
	//   поэтому ПРЕФИКС имени = имя скина. В окне 67000 этот скин встречается ровно 4 раза.
	const TCHAR* const kClaimSkin = TEXT("Extract_Icon_Effect");
}

// ═══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ════════════════════════════════════════════════

void UAsdaSoulRouletteSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// ★★★ РОСПИСЬ «КАК ЗАКРЫТЬ СЕБЯ ПО ESC» — порт события 21 (`CControl_OnEvent21@0x12DCC30`).
	//   Окно `GOLD_roullete_WIN` (IDNum 67000) стоит в эталонной таблице `kEscCloseWindows`: признак
	//   `CControl+0x228` ему ставит `CGoldRoulette_InitUI@0xE615E0`. Проход
	//   `UAsdaWindowSubsystem::CloseTopmostOnEsc` закрывает верхнее окно ИМЕННО ЭТИМ вызовом,
	//   а не снятием виджета: снятие оставило бы подсистеме состояние «открыто», и окно
	//   больше не открылось бы. Закрыватель идемпотентен — повторный вызов ничего не делает.
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	if (UAsdaWindowSubsystem* EscWM = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		EscWM->RegisterEscCloser(kWindowId, FSimpleDelegate::CreateUObject(this, &UAsdaSoulRouletteSubsystem::CloseWindow));
	}
	LoadTable();
	if (UAsdaNetSubsystem* N = Net())
	{
		HResult = N->OnRouletteResult.AddUObject(this, &UAsdaSoulRouletteSubsystem::OnSpinResult6576);
		HPrize  = N->OnRoulettePrize.AddUObject(this, &UAsdaSoulRouletteSubsystem::OnPrize6580);
		bNetBound = true;
	}
	UE_LOG(LogAsdaRoulette, Log, TEXT("[рулетка] подсистема поднята: строк таблицы %d, сеть %s"),
		Rows.Num(), bNetBound ? TEXT("привязана") : TEXT("НЕ привязана"));
}

void UAsdaSoulRouletteSubsystem::Deinitialize()
{
	if (UAsdaNetSubsystem* N = Net())
	{
		N->OnRouletteResult.Remove(HResult);
		N->OnRoulettePrize.Remove(HPrize);
	}
	ReleaseWindow();
	Super::Deinitialize();
}

// ★★★ К-3 / находка Р5 отчёта `14_soulmate_ревизия.md`: сессия прокрутки рулетки (`bSession`,
//   `CurRowId`, `NextRowId`) — состояние ОДНОГО захода, которое ведёт сервер пакетами 6576/6580.
//   Ревизия отдельно отметила (Р7): машина состояний стартует ТОЛЬКО из 6576, поэтому оставшийся
//   `bSession` не «доиграет» сам — он просто заблокирует следующий заход.
void UAsdaSoulRouletteSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Прокрутка привязана к виджету умершего мира и к закрытому сокету — гасим при любой причине.
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-B; тот же дефект, что у чата — `wave2_A.md` п.0): окно 67000 (GOLD_roullete_WIN) ЗАКРЫВАЕТСЯ В РЕЕСТРЕ.
	//   Было: сброс только забывал указатель, окно оставалось живым в реестре; при сбросе без смены мира (разрыв связи)
	//   следующая сборка получала от `OpenWindowByPath` ТОТ ЖЕ виджет и строила узлы/привязки второй раз поверх старого
	//   дерева (второе реле кнопки 67002 «крутить» — клик срабатывал бы дважды до сборки мусора). Стало: окно снимается с реестра и вьюпорта (тихо), следующая сборка создаёт НОВЫЙ экземпляр — как
	//   после травела. Ключа отката нет: прежнее поведение — порча дерева.
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->CloseWindowSilently(kWindowId);
	}
	ReleaseWindow();
	CurAngle = 0.f; TargetAngle = 0.f; TotalPath = 0.f;
	PrevAngle = 0.f; AngleAccum = 0.f;
	Speed = kSpinSpeed0;
	CurRowId = 0; NextRowId = -1;
	PlateTimerEndS = 0.0; PlateSector = -1; bPlateIsClaim = false;
	if (AsdaIsFullSessionReset(Reason))
	{
		UE_LOG(LogAsdaRoulette, Log, TEXT("[рулетка] сброс сессии (%s): заход прерван"),
			AsdaSessionResetReasonName(Reason));
	}
}

UAsdaNetSubsystem* UAsdaSoulRouletteSubsystem::Net() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
}

// ═══════════════════════════════ ТАБЛИЦА ПРИЗОВ ════════════════════════════════════════════════

void UAsdaSoulRouletteSubsystem::LoadTable()
{
	if (bTableLoaded) { return; }
	Rows.Reset();
	if (const UDataTable* Dt = AsdaDt::Cached(kPrizeTablePath))
	{
		for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
		{
			if (const FAsdaRouletteRow* R = reinterpret_cast<const FAsdaRouletteRow*>(It.Value))
			{
				if (R->RowId >= 0) { Rows.Add(R->RowId, *R); }
			}
		}
	}
	else
	{
		UE_LOG(LogAsdaRoulette, Error,
			TEXT("[рулетка] нет таблицы %s — плашки призов рисовать нечем ")
			TEXT("(источник: Data\\Soulmate_Roulette.bin, заголовок 20 б «SRL», 10 × 340 б)"), kPrizeTablePath);
	}
	bTableLoaded = (Rows.Num() > 0);
}

// Эталон `findEntryById340@0xE62690` — поиск записи по id, а НЕ по индексу.
const FAsdaRouletteRow* UAsdaSoulRouletteSubsystem::GetRow(int32 RowId) const
{
	return Rows.Find(RowId);
}

// ═══════════════════════════════ ОКНО 67000 ════════════════════════════════════════════════════

bool UAsdaSoulRouletteSubsystem::IsOpen() const
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaWindowSubsystem* W = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	return W && W->IsOpen(kWindowId);
}

void UAsdaSoulRouletteSubsystem::OpenWindow()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* W = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!W) { return; }
	Wnd = W->OpenWindowByPath(kWindowId, kWindowWbp, kWinZ);
	if (!Wnd)
	{
		UE_LOG(LogAsdaRoulette, Error, TEXT("[рулетка] окно %s не открылось (WBP %s)"), *kWindowId.ToString(), kWindowWbp);
		return;
	}
	BindWindowControls();
	// Эталон при показе панели перерисовывает плашки текущей строки и играет «готово»
	//   (`RoulettePanel_UpdateRewardSlots(..., a3 = 1, a4 = 1)`); строка — текущая (+498).
	UpdateRewardSlots(CurRowId, /*bPlayReady*/true, /*bVisible*/true);
}

void UAsdaSoulRouletteSubsystem::CloseWindow()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* W = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		W->CloseWindow(kWindowId);
	}
	ReleaseWindow();
}

void UAsdaSoulRouletteSubsystem::ReleaseWindow()
{
	Wnd = nullptr;
	Arrow = nullptr;
	Gfont.Reset();
	GfontIcon.Reset();
	Plate.Reset();
	PlateAni.Reset();
	PlateClaim.Reset();
	ClaimByWindowOrder.Reset();
	bWndBound = false;
	SpinState = EAsdaRouletteSpin::Idle;
	bSession = false;
	PlateSector = -1;
	PlateTimerEndS = 0.0;
	SpinBtn = nullptr;
	Relays.Reset();
	bAnchorPending = false;   // ★ ревизия М-3
	AnchorTries = 0;
}

// Порт `CGoldRoulette_InitUI@0xE615E0`: 67000 (окно), 67001 (стрелка), затем ЦИКЛОМ i = 0..3
//   четвёрки 67011+i, 67021+i, 67003+i, 67007+i, 67015+i.
//   ★★★ ИСПРАВЛЕНО 06.10.2026 (волна 1-Б, дефект ПОРТА — окно в игре не открывалось, и ошибки не было видно):
//   прежний поиск «точно, затем по ПРЕФИКСУ» брал НЕ ТЕ узлы (сверено с дампом ассета
//   `Reports/owner_1006/_wave1B_wbp_dump.txt`, строки 132-184):
//     • `GOLD_roullete_gfont1` → префиксом ловилась ГРУППА `GOLD_roullete_gfont1_grp` (CanvasPanel), `Cast<UImage>`
//       давал nullptr ⇒ иконки призов 67011…67014 НЕ РИСОВАЛИСЬ НИКОГДА;
//     • `GOLD_roullete_gfont1_m1` → точным именем ловился TextBlock-подпись, `Cast<UImage>` ⇒ nullptr ⇒ иконки
//       предметов 67021…67024 не рисовались;
//     • `GOLD_roullete_ar2_UP` (стрелка-указатель 67003) → префиксом ловилась ГРУППА анимации
//       `GOLD_roullete_ar2_UP_ani_grp` (67007) ⇒ при открытии окна показывались анимированные плашки всех
//       включённых секторов, а статичные стрелки 67003…67006 не трогались вовсе.
//   СТАЛО: планы ищутся РАЗБОРОМ имени (`{idc}_{индекс}_{план}`, `AsdaUi::ParsePlaneName`), группы — точным `{idc}_grp`.
namespace
{
	// План контрола по разобранному имени импортёра (закон `jui-importer-widget-naming`).
	UImage* RoulettePlaneOf(const UUserWidget* Host, const TCHAR* Idc, int32 PlaneNo)
	{
		UWidgetTree* Tree = Host ? Host->WidgetTree : nullptr;
		if (!Tree || !Idc) { return nullptr; }
		const FString Want(Idc);
		UImage* Found = nullptr;
		Tree->ForEachWidget([&Found, &Want, PlaneNo](UWidget* W)
		{
			if (Found) { return; }
			UImage* Img = Cast<UImage>(W);
			if (!Img) { return; }
			FString ParsedIdc; int32 JuiIdx = 0, Plane = 0;
			if (!AsdaUi::ParsePlaneName(Img->GetName(), ParsedIdc, JuiIdx, Plane)) { return; }
			if (Plane == PlaneNo && ParsedIdc.Equals(Want, ESearchCase::IgnoreCase)) { Found = Img; }
		});
		return Found;
	}
}

bool UAsdaSoulRouletteSubsystem::BindWindowControls()
{
	if (!Wnd) { return false; }
	Gfont.SetNum(kSectors);
	GfontIcon.SetNum(kSectors);
	Plate.SetNum(kSectors);
	PlateAni.SetNum(kSectors);
	PlateClaim.SetNum(kSectors);

	// Все узлы окна, чьё имя начинается со скина `Extract_Icon_Effect` — в порядке обхода дерева
	//   (он стабилен и совпадает с порядком документа .ini, то есть с порядком секторов 0..3).
	ClaimByWindowOrder.Reset();
	if (UWidgetTree* Tree = Wnd->WidgetTree)
	{
		Tree->ForEachWidget([this](UWidget* W)
		{
			if (W && W->GetName().StartsWith(kClaimSkin)) { ClaimByWindowOrder.Add(W); }
		});
	}
	if (ClaimByWindowOrder.Num() != kSectors)
	{
		// В данных их ровно 4. Иное число = сборка окна разошлась с разметкой — это надо видеть.
		UE_LOG(LogAsdaRoulette, Warning,
			TEXT("[рулетка] узлов со скином «%s» в окне %d, а по разметке их ровно %d (67015…67018)"),
			kClaimSkin, ClaimByWindowOrder.Num(), kSectors);
	}

	Arrow = RoulettePlaneOf(Wnd, TEXT("GOLD_roullete_arrow"), 0);   // 67001

	// Порядок четвёрок эталона: 0 = ВВЕРХ, 1 = ВПРАВО, 2 = ВНИЗ, 3 = ВЛЕВО (см. шапку .h).
	static const TCHAR* kGfont[kSectors]    = { TEXT("GOLD_roullete_gfont1"),    TEXT("GOLD_roullete_gfont2"),
	                                            TEXT("GOLD_roullete_gfont3"),    TEXT("GOLD_roullete_gfont4") };
	static const TCHAR* kGfontIcon[kSectors]= { TEXT("GOLD_roullete_gfont1_m1"), TEXT("GOLD_roullete_gfont2_m2"),
	                                            TEXT("GOLD_roullete_gfont3_m3"), TEXT("GOLD_roullete_gfont4_m4") };
	static const TCHAR* kPlate[kSectors]    = { TEXT("GOLD_roullete_ar2_UP"),    TEXT("GOLD_roullete_ar2_right"),
	                                            TEXT("GOLD_roullete_ar2_DOWN"),  TEXT("GOLD_roullete_ar2_left") };
	// У 67007…67010 есть дети (67031…67034) ⇒ импортёр сделал ГРУППУ `{idc}_grp`: видимость — у неё,
	//   кадровая анимация — у плана `{idc}_{n}_0` внутри (его находит `FindOwnAnimPlane`).
	static const TCHAR* kPlateAniGrp[kSectors] = { TEXT("GOLD_roullete_ar2_UP_ani_grp"),   TEXT("GOLD_roullete_ar2_right_ani_grp"),
	                                               TEXT("GOLD_roullete_ar2_DOWN_ani_grp"), TEXT("GOLD_roullete_ar2_left_ani_grp") };

	int32 Found = Arrow ? 1 : 0;
	for (int32 i = 0; i < kSectors; ++i)
	{
		Gfont[i]     = RoulettePlaneOf(Wnd, kGfont[i], 0);
		GfontIcon[i] = RoulettePlaneOf(Wnd, kGfontIcon[i], 0);
		Plate[i]     = RoulettePlaneOf(Wnd, kPlate[i], 0);
		PlateAni[i]  = AsdaUi::FindNodeExact(Wnd, kPlateAniGrp[i]);
		Found += (Gfont[i] ? 1 : 0) + (GfontIcon[i] ? 1 : 0) + (Plate[i] ? 1 : 0) + (PlateAni[i] ? 1 : 0);
		if (!Gfont[i] || !GfontIcon[i] || !Plate[i] || !PlateAni[i])
		{
			UE_LOG(LogAsdaRoulette, Warning, TEXT("[рулетка] сектор %d: 67011=%s 67021=%s 67003=%s 67007=%s"), i,
				Gfont[i] ? TEXT("да") : TEXT("НЕТ"), GfontIcon[i] ? TEXT("да") : TEXT("НЕТ"),
				Plate[i] ? TEXT("да") : TEXT("НЕТ"), PlateAni[i] ? TEXT("да") : TEXT("НЕТ"));
		}

		// ★★★ 67015…67018 — СЛОЙ «ЗАБРАТЬ» (`clan_soulmate_gaps.md` §5). Прежний вывод «искать
		//   обходом дерева, имени быть не может» ОКАЗАЛСЯ НЕВЕРЕН — и вот почему.
		//   (1) В `gamemain_jui.ini` (строки 310484/310551/310618/310685) у них пустая строка
		//       `ID = ""`, но **`IDNum` ЕСТЬ**: 67015…67018. Сам эталон адресует их именно по числу
		//       (`CGoldRoulette_InitUI@0xE615E0`: `CWindowMgr_FindControlByID(mgr, i + 67015)`;
		//       строка ошибки в бинаре — буквально «67015 UI Error», то есть имени не было и у авторов).
		//   (2) `IDNum` импортёр порта в виджет НЕ КЛАДЁТ (`Tools/asda_jui_generic_umg.py`:
		//       у контрола с пустым `ID` имя строится как `san(node.Name)_{индекс}`, где `node.Name`
		//       — это ИМЯ СКИНА). Значит опознавать надо ПО СКИНУ — и это положительный признак,
		//       а не обход вслепую: скин **`Extract_Icon_Effect`** встречается в окне 67000 РОВНО
		//       ЧЕТЫРЕ РАЗА, по одному на сектор, и только на этих контролах (сверено по .ini:
		//       у 67021…67024 скин `NoImage_icon`, у самих плашек 67011…67014 — `NoImage2_icon`).
		//   Порядок поиска: сперва среди ДЕТЕЙ своей плашки (родитель доказан: 67015+i лежит под
		//   67011+i), затем — по всему окну, если импортёр расплющил иерархию (закон
		//   `jui-flattened-hierarchy-geometry`). Не нашли — пишем ИСКОМОЕ в журнал, а не молчим.
		//   ★ 06.10.2026: родитель слоя — ГРУППА плашки `GOLD_roullete_gfont{N}_grp`, а не её план.
		UWidget* GfontGrp = AsdaUi::FindNodeExact(Wnd, *FString::Printf(TEXT("%s_grp"), kGfont[i]));
		PlateClaim[i] = FindClaimLayer(GfontGrp, i);
		if (!PlateClaim[i])
		{
			UE_LOG(LogAsdaRoulette, Warning,
				TEXT("[рулетка] слой «забрать» (IDNum %d, idc в JUI пуст, скин «%s») под плашкой «%s» ")
				TEXT("не найден — забор приза 6579 уйдёт по таймеру, но без подсветки сектора"),
				67015 + i, kClaimSkin, kGfont[i]);
		}
		else { ++Found; }
	}

	// ★ Волна 1-Б: кнопка 67002 «крутить» (`GOLD_roullete_btn`, UButton) — эталонная ветка
	//   `SoulmateUI_HandleButtonCommand` (msg 0x10401). Прежде не была привязана вовсе.
	SpinBtn = Cast<UButton>(AsdaUi::FindNodeExact(Wnd, TEXT("GOLD_roullete_btn")));
	Relays.Reset();
	if (SpinBtn)
	{
		AsdaUi::BindButton(this, SpinBtn, 67002, [this](int32) { OnSpinButton67002(); }, Relays);   // ★ U20: реле клика — общая точка
		++Found;
	}
	else
	{
		UE_LOG(LogAsdaRoulette, Warning, TEXT("[рулетка] кнопка 67002 `GOLD_roullete_btn` в окне не найдена"));
	}
	bWndBound = true;
	UE_LOG(LogAsdaRoulette, Log, TEXT("[рулетка] окно разобрано: узлов найдено %d/22"), Found);
	return true;
}

// ★ Волна 1-Б: «Золотое яблоко» 6693 — в окне соулмейта 6500, его ловит `UAsdaSoulAppleSubsystem`.
//   Здесь только след в журнале: метод зовёт `UAsdaSoulmateSubsystem` при привязке вкладки 6400.
void UAsdaSoulRouletteSubsystem::BindTabControls(UUserWidget* /*CommunityWindow*/)
{
	UE_LOG(LogAsdaRoulette, Verbose,
		TEXT("[рулетка] вкладка соулмейта 6400 привязана; «Золотое яблоко» 6693 живёт в окне 6500 — его ведёт UAsdaSoulAppleSubsystem"));
}

// Кнопка 67002 (game.c:474286-474294): `if (a2 == 67002 && win[495] == 1) { win[495] = 0; win[505] = 1;
//   win[503] = 0; win[496] = 2; win[497] = 80.0f; PlaySound2D("S_e_sm_ro_start_00.wav"); }` — без пакета.
void UAsdaSoulRouletteSubsystem::OnSpinButton67002()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaSoulAppleSubsystem* Apple = GI ? GI->GetSubsystem<UAsdaSoulAppleSubsystem>() : nullptr;
	if (!Apple || !Apple->ConsumeGoldenFlag())
	{
		UE_LOG(LogAsdaRoulette, Log, TEXT("[рулетка] 67002: яблоко не созрело (+495 != 1) — ничего, как эталон"));
		return;
	}
	bSession  = true;
	CurAngle  = 0.f;
	SpinState = EAsdaRouletteSpin::Start;
	Speed     = kSpinSpeed0;
	PlaySound2D(kWavSpinStart);
	UE_LOG(LogAsdaRoulette, Log, TEXT("[рулетка] 67002: флаг «созрело» снят, колесо крутится ЛОКАЛЬНО к %.0f° (пакета нет)"), TargetAngle);
}

// Тело здесь: в заголовке UButton — неполный тип.
UWidget* UAsdaSoulRouletteSubsystem::GetSpinButton() const { return SpinBtn.Get(); }

bool UAsdaSoulRouletteSubsystem::IsWindowShown() const
{
	return bWndBound && Wnd && Wnd->IsVisible() && IsOpen();
}

// ═══════════════════════════════ ОТРИСОВКА ПЛАШЕК ══════════════════════════════════════════════

// Порт `RoulettePanel_UpdateRewardSlots@0xE61930` (item_inventory.c:56779-56945).
void UAsdaSoulRouletteSubsystem::UpdateRewardSlots(int32 RowId, bool bPlayReady, bool bVisible)
{
	if (!bWndBound) { return; }

	if (bPlayReady)
	{
		// Ветка `a3 == 1`: сбросить строку в 0, сыграть «готово» и переставить окно к якорю 6699
		//   относительно подложки 67050 (эталон: X += 57 − ширина подложки + 5, Y −= 75).
		//   ★ Позиционирование по якорю 6699 (`Soulmate_Glass` окна 6500, а не вкладки 6400 — поправка
		//     волны 1-Б) здесь не воспроизводим: окно у порта ставит `UAsdaWindowSubsystem` (сохранённая
		//     раскладка окна — закон о переносе окон). Отступление записано в отчёт волны 1-Б.
		CurRowId = 0;
		RowId = 0;
		PlaySound2D(kWavReady);
		// ★ Ревизия 1-Б М-3: поставить окно у якоря 6699 — как только обе геометрии отрисованы (`TickAnchor`).
		if (CVarAnchorPos.GetValueOnGameThread() != 0)
		{
			bAnchorPending = true;
			AnchorTries = 0;
			if (Wnd) { Wnd->SetRenderOpacity(0.f); }   // без кадра «окно в чужом месте»
		}
	}

	const FAsdaRouletteRow* Rec = GetRow(RowId);
	if (!Rec)
	{
		UE_LOG(LogAsdaRoulette, Warning, TEXT("[рулетка] строки %d в таблице нет — плашки не перерисованы"), RowId);
		return;
	}
	CurRowId = RowId;

	if (Wnd) { Wnd->SetVisibility(bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (!bVisible) { return; }

	// Эталон: вне сессии стрелка возвращается в 0, скорость — к 80.0f.
	if (!bSession)
	{
		SetArrowAngle(0.f);
		Speed = kSpinSpeed0;
		CurAngle = 0.f;
	}

	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const bool bFixExpIcon = (CVarFixExpIcon.GetValueOnGameThread() != 0);

	for (int32 s = 0; s < kSectors; ++s)
	{
		const int32 Type    = Rec->Types.IsValidIndex(s)   ? Rec->Types[s]   : 0;
		const int32 Value   = Rec->Values.IsValidIndex(s)  ? Rec->Values[s]  : 0;
		const int32 Count   = Rec->Counts.IsValidIndex(s)  ? Rec->Counts[s]  : 0;
		const int32 Enabled = Rec->Enabled.IsValidIndex(s) ? Rec->Enabled[s] : -1;

		// Плашка сектора видна, только если включённость > 0.
		if (Plate.IsValidIndex(s) && Plate[s])
		{
			Plate[s]->SetVisibility(Enabled > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
		// Оверлей иконки предмета по умолчанию скрыт.
		if (GfontIcon.IsValidIndex(s) && GfontIcon[s])
		{
			GfontIcon[s]->SetVisibility(ESlateVisibility::Collapsed);
		}

		int32 IconU = 0;
		int32 DrawType = Type;

		// ★ ДЕФЕКТ D9: эта ветка перекрывает «правильную» формулу опыта (см. шапку файла).
		if (!bFixExpIcon && Enabled > -1 && Type == 1)
		{
			DrawType = 0;
			IconU = 64;
		}
		else if (Type == 1)
		{
			IconU = ((Value - 1) / 10) << 6;
		}
		else if (Type == 2)
		{
			// Предмет: оверлей 67021+s показывается и получает иконку предмета; количество > 1
			//   печатается строкой на оверлее (у порта текст ячейки рисует сам оверлей-Image,
			//   поэтому число уходит в журнал — своего текстового узла у 67021..67024 в JUI нет).
			if (GfontIcon.IsValidIndex(s) && GfontIcon[s])
			{
				GfontIcon[s]->SetVisibility(ESlateVisibility::HitTestInvisible);
				if (Inv) { Inv->ApplyIconToImage(GfontIcon[s].Get(), Value, 32.f); }
			}
			if (Count > 1)
			{
				UE_LOG(LogAsdaRoulette, Verbose, TEXT("[рулетка] сектор %d: предмет %d × %d"), s, Value, Count);
			}
		}

		// Общая для всех типов отрисовка: `roulette_ListImg.tex`, UV (IconU, тип << 6, 64, 64).
		if (Gfont.IsValidIndex(s) && Gfont[s])
		{
			AsdaUi::ApplyJuiIconCell(Gfont[s].Get(), TEXT("roulette_ListImg"), IconU, DrawType << 6, 64, 64.f);
			Gfont[s]->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}
	UE_LOG(LogAsdaRoulette, Log, TEXT("[рулетка] плашки перерисованы по строке %d"), RowId);
}

// Эталон `sub_926C2D(arrow, angle)` — поворот контрола стрелки. У порта это рендер-трансформ узла.
void UAsdaSoulRouletteSubsystem::SetArrowAngle(float Degrees)
{
	if (Arrow) { Arrow->SetRenderTransformAngle(Degrees); }
}

// ★★★ СЛОЙ «ЗАБРАТЬ» СЕКТОРА (IDNum 67015 + Sector; в JUI у контрола пустой `ID`, есть только IDNum).
//   Опознаётся ПО СКИНУ `Extract_Icon_Effect` — см. развёрнутое обоснование в `BindWindowControls`.
//   Шаг 1: среди детей своей плашки 67011+Sector (родитель доказан разметкой).
//   Шаг 2: по всему окну, в порядке обхода дерева (импортёр расплющивает иерархию, и тогда узел
//          лежит СОСЕДОМ плашки, а не её ребёнком — закон `jui-flattened-hierarchy-geometry`).
UWidget* UAsdaSoulRouletteSubsystem::FindClaimLayer(UWidget* PlateGfont, int32 Sector)
{
	if (UPanelWidget* Panel = Cast<UPanelWidget>(PlateGfont))
	{
		for (int32 c = 0; c < Panel->GetChildrenCount(); ++c)
		{
			UWidget* Child = Panel->GetChildAt(c);
			if (Child && Child->GetName().StartsWith(kClaimSkin)) { return Child; }
		}
	}
	return ClaimByWindowOrder.IsValidIndex(Sector) ? ClaimByWindowOrder[Sector].Get() : nullptr;
}

// ★★★ ПЛАН КАДРОВОЙ АНИМАЦИИ САМОГО КОНТРОЛА (не его детей!).
//   Импортёр строит `UAsdaAnimatedPlane` каждому плану скина с `Animate = "1"` и называет его
//   `{idc}_{порядок}_{номер плана}`. Эталон берёт РОВНО план 0 СВОЕГО контрола
//   (`ctrlShowWithItemFlags@0x12E1130`: `CContainer_GetItemAt(this, 0)`), поэтому ищем по
//   ПРЕФИКСУ `idc` этого контрола, а не обходом поддерева: у плашки 67007 есть ребёнок 67031
//   со скином `Ani_Soul2` и ДРУГИМИ параметрами (22 кадра / 1000 мс) — он декоративный и
//   тайминг НЕ задаёт. Вернуть его вместо плана контрола было бы ошибкой на 600 мс.
UAsdaAnimatedPlane* UAsdaSoulRouletteSubsystem::FindOwnAnimPlane(UWidget* PlateNode, const TCHAR* IdcPrefix) const
{
	// Обычный случай: `FindNodeByPrefix` в `BindWindowControls` уже вернул сам план контрола.
	if (UAsdaAnimatedPlane* Direct = Cast<UAsdaAnimatedPlane>(PlateNode)) { return Direct; }
	if (!Wnd || !Wnd->WidgetTree || !IdcPrefix) { return nullptr; }
	UAsdaAnimatedPlane* Found = nullptr;
	Wnd->WidgetTree->ForEachWidget([&Found, IdcPrefix](UWidget* W)
	{
		if (Found || !W || !W->GetName().StartsWith(IdcPrefix)) { return; }
		if (UAsdaAnimatedPlane* P = Cast<UAsdaAnimatedPlane>(W)) { Found = P; }
	});
	return Found;
}

// ★★★ ЗАПУСК АНИМАЦИИ ПЛАШКИ И ЕЁ ДЛИТЕЛЬНОСТЬ — ПОЛНОСТЬЮ ИЗ ДАННЫХ (ключа больше нет).
//   Что именно делает эталон, когда колесо встало (`gachaWheel_UpdateSpinAnimation@0xE62020`,
//   `case 4`, social.c:6862-6870): показывает плашку и зовёт `ctrlShowWithItemFlags(ctrl, 2)`.
//   Эта функция (`ui_core.c:109888`) на плане 0 КОНТРОЛА выставляет:
//     `sub_932A0F(item) = 1`  — Animate;
//     `sub_925D73(item) = 1`  — **EFrameStop**, то есть «доиграть и остановиться»;
//     `sub_9259EA(this, a2)`  — **ЛИМИТ ПРОГОНОВ = a2 = 2**;
//     время старта = `timeGetTime()`.
//   Дальше `CControl_BuildPlaneVertices@0x12DE440` (ui_core.c:108836-108901) крутит план и по
//   исчерпании лимита (`gap_220[28] >= gap_220[32]`) прячет контрол и шлёт **событие 24** —
//   то самое, по которому уходит 6575 (плашки 67007…67010) либо 6579 (слои 67015…67018).
//   ⇒ СРОК = `FrameInterval` скина × ЧИСЛО ПРОГОНОВ (2). `FrameInterval` — длительность ВСЕГО
//   прогона в мс, а не период кадра (`frame = elapsed / FrameInterval * (TotalFrame − 1)`).
//
//   ⚠ ПОПРАВКА К ОТЧЁТУ `clan_soulmate_gaps.md` §4 (проверено дословно по данным и по коду):
//     (1) тайминг задаёт НЕ скин `Ani_Soul2` (это ребёнок 67031…67034, 22 кадра / 1000 мс), а
//         план САМОГО контрола: у 67007…67010 это `GOLD_roullete_ar_B_UP/RIGHT/DOWN/LEFT`
//         (`JuiSkin.ini`: TotalFrame 3, **FrameInterval 700**), у 67015…67018 —
//         `Extract_Icon_Effect` (`JuiSkin.ini:25956`: TotalFrame 16, **FrameInterval 1000**);
//     (2) `EFrameStop` в `gamemain_jui.ini` у ВСЕХ этих контролов = "0" — признак ставится
//         В РАНТАЙМЕ вызовом `ctrlShowWithItemFlags`, вместе с лимитом прогонов 2.
//   Итог: ветка «крутить дальше» = 700 × 2 = 1400 мс, ветка «забрать» = 1000 × 2 = 2000 мс.
double UAsdaSoulRouletteSubsystem::StartPlateAnim(UWidget* PlateNode, const TCHAR* IdcPrefix,
                                                  float FallbackMs, const TCHAR* Tag)
{
	UAsdaAnimatedPlane* Plane = FindOwnAnimPlane(PlateNode, IdcPrefix);
	if (!Plane || Plane->FrameIntervalMs <= 0.f)
	{
		UE_LOG(LogAsdaRoulette, Warning,
			TEXT("[рулетка] у плашки «%s» плана кадровой анимации нет — беру %.0f мс × %d прогона ")
			TEXT("из скина (JuiSkin.ini)"), Tag, FallbackMs, kPlateAnimLoops);
		return (double)(FallbackMs * kPlateAnimLoops) / 1000.0;
	}
	// 1:1 `ctrlShowWithItemFlags(ctrl, 2)`: EFrameStop + лимит прогонов 2 + рестарт с нуля.
	Plane->bLoop    = false;              // EFrameStop = 1 ⇒ по исчерпании лимита стоп + событие 24
	Plane->MaxLoops = kPlateAnimLoops;    // a2 = 2
	Plane->RestartAnim();
	const double Sec = (double)(Plane->FrameIntervalMs * kPlateAnimLoops) / 1000.0;
	UE_LOG(LogAsdaRoulette, Log,
		TEXT("[рулетка] анимация плашки «%s»: %d кадров, %.0f мс на прогон × %d = %.0f мс (данные скина)"),
		Tag, Plane->TotalFrame, Plane->FrameIntervalMs, kPlateAnimLoops, Sec * 1000.0);
	return Sec;
}

void UAsdaSoulRouletteSubsystem::ShowPlate(int32 Sector, bool bAniLayer, bool bClaimLayer)
{
	if (Sector < 0 || Sector >= kSectors) { return; }
	// idc плашек 67007…67010 — по нему импортёр назвал их планы (см. `FindOwnAnimPlane`).
	static const TCHAR* kPlateAniIdc[kSectors] = {
		TEXT("GOLD_roullete_ar2_UP_ani"),   TEXT("GOLD_roullete_ar2_right_ani"),
		TEXT("GOLD_roullete_ar2_DOWN_ani"), TEXT("GOLD_roullete_ar2_left_ani") };

	PlateAnimSec = (double)(kClaimPlateMs * kPlateAnimLoops) / 1000.0;
	if (bAniLayer && PlateAni.IsValidIndex(Sector) && PlateAni[Sector])
	{
		PlateAni[Sector]->SetVisibility(ESlateVisibility::HitTestInvisible);
		PlateAnimSec = StartPlateAnim(PlateAni[Sector].Get(), kPlateAniIdc[Sector],
			kAniPlateMs, TEXT("GOLD_roullete_ar_B_* (67007+i)"));
	}
	if (bClaimLayer && PlateClaim.IsValidIndex(Sector) && PlateClaim[Sector])
	{
		PlateClaim[Sector]->SetVisibility(ESlateVisibility::HitTestInvisible);
		// У 67015…67018 `ID` пуст ⇒ имя плана строится от имени СКИНА (см. `kClaimSkin`).
		PlateAnimSec = StartPlateAnim(PlateClaim[Sector].Get(), kClaimSkin,
			kClaimPlateMs, TEXT("Extract_Icon_Effect (67015+i)"));
	}
}

// ═══════════════════════════════ ПРОКРУТКА: ОТПРАВКА 6575 ══════════════════════════════════════

bool UAsdaSoulRouletteSubsystem::Spin()
{
	UAsdaNetSubsystem* N = Net();
	if (!N) { return false; }
	// ★ Все семь гейтов (партнёр онлайн, тот же канал/мир/зона, дистанция 50, близость ≥ 200 либо
	//   «уже сегодня») живут НА СЕРВЕРЕ (`Recv_UserSoulRoulettet@0x49A1C0`). Клиент не проверяет
	//   НИЧЕГО — своих гейтов не выдумываем, отказ придёт кодом 10/11/12 в 6576.
	const bool bOk = N->SendRouletteSpin6575();
	if (bOk) { ++Tx6575; }
	UE_LOG(LogAsdaRoulette, Log, TEXT("[рулетка] 6575 «крутить» %s"), bOk ? TEXT("отправлен") : TEXT("НЕ отправлен"));
	return bOk;
}

// ═══════════════════════════════ 6576 «результат прокрутки» ════════════════════════════════════

void UAsdaSoulRouletteSubsystem::OnSpinResult6576(const TArray<uint8>& Body)
{
	if (Body.Num() < 3) { return; }
	++Rx6576;
	const int32 Code   = (int32)Body[0];
	const int32 Sector = (int32)(int8)Body[1];
	const int32 Next   = (int32)(int8)Body[2];

	if (Code == 1)
	{
		// ★★★ ЕДИНСТВЕННЫЙ ПУТЬ ОТКРЫТИЯ ОКНА 67000 В ИГРЕ (волна 1-Б, 06.10.2026).
		//   Эталон `roulettePanelHandleNetEvent@0xE6E480` (game.c:475803):
		//     v2 = *(this->gap_3B0)  /* = self[236] = окно 67000 */;
		//     if (*(BYTE*)(v2 + 292) /* CControl+0x124 isVisible */) PlaySound2D("S_e_sm_ro_start_00.wav");
		//     else RoulettePanel_UpdateRewardSlots(self, 0, /*a3*/1, /*a4*/1);   // ← ПОКАЗ ОКНА + «готово»
		//   У `RoulettePanel_UpdateRewardSlots` во всём образе ровно ТРИ вызова (скан E8/E9 по 0xE61930 и
		//   thunk 0x922ACE): этот (a4 = 1) и два в обработчике конца анимации плашки (a4 = 1 цепочка /
		//   a4 = 0 «забрать»). ⇒ окно 67000 открывает ТОЛЬКО ответ 6576 код 1, а 6576 — ответ на 6575
		//   «Золотого яблока» 6693 (вкладка соулмейта окна 6400). Ни предмета, ни NPC, ни кнопки окна нет.
		//   БЫЛО (дефект порта): ветка «не показано» звала `UpdateRewardSlots`, а та при `!bWndBound`
		//   сразу выходит — окно в игре не появлялось никогда, только консолью asda.Soul.Roulette.Open.
		const bool bShown = bWndBound && Wnd && Wnd->IsVisible();
		if (bShown)          { PlaySound2D(kWavSpinStart); }
		else if (bWndBound)  { UpdateRewardSlots(CurRowId, /*bPlayReady*/true, /*bVisible*/true); }   // окно есть, но спрятано (после «забрать»)
		else                 { OpenWindow(); }   // окна ещё нет: собрать + та же перерисовка (a3 = 1, a4 = 1)
		UE_LOG(LogAsdaRoulette, Log, TEXT("[рулетка] 6576 код 1: окно 67000 %s"),
			bShown ? TEXT("уже на экране — звук старта") : (bWndBound && Wnd && Wnd->IsVisible() ? TEXT("показано") : TEXT("НЕ показано (см. лог выше)")));

		NextRowId = Next;
		BeginSpinTo(Sector);
		UE_LOG(LogAsdaRoulette, Log, TEXT("[рулетка] 6576 код 1: сектор %d (угол %.0f°), следующая строка %d"),
			Sector, TargetAngle, Next);
		return;
	}
	if (Code == 10) { ChatRefuse(8081); return; }
	if (Code == 11) { ChatRefuse(8167); return; }
	if (Code == 12)
	{
		// ★ ДЕФЕКТ D3: эталон эту ветку не завёл вовсе — игрок получает молчание.
		if (CVarFixSilentCodes.GetValueOnGameThread() != 0)
		{
			UGameInstance* GI = GetGameInstance();
			if (UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
			{
				FAsdaMsgParams P;
				P.Category  = EAsdaMsgCategory::Social;
				P.LocId     = kLocOwnRefuse12;
				// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): RU-фолбэк убран — нет строки 990346 → «[#990346]».
				P.bUseColor = true;
				P.Color     = kChatRed;
				P.TtlMs     = 10000.f;
				P.SrcOpcode = 6576;
				Msg->Post(P);
			}
		}
		UE_LOG(LogAsdaRoulette, Warning, TEXT("[рулетка] 6576 код 12 — в оригинале БЕЗ текста (дефект D3)"));
		return;
	}
	UE_LOG(LogAsdaRoulette, Warning, TEXT("[рулетка] 6576 код %d отчётом не описан"), Code);
}

// ═══════════════════════════════ АНИМАЦИЯ КОЛЕСА ═══════════════════════════════════════════════

// Порт `roulettePanelHandleNetEvent@0xE6E480` в части постановки цели + состояние 2.
void UAsdaSoulRouletteSubsystem::BeginSpinTo(int32 Sector)
{
	TargetAngle = (float)Sector * 90.f;
	if (TargetAngle == 0.f) { TargetAngle = 360.f; }   // эталон: `if (== 0.0) = 1135869952` = 360.0f
	Speed      = kSpinSpeed0;
	bSession   = true;
	SpinState  = EAsdaRouletteSpin::Start;
	PlateSector = -1;
	PlateTimerEndS = 0.0;
	// Все плашки-анимации и слои «забрать» гасим — эталон делает это в состоянии 4 перед показом.
	for (int32 i = 0; i < kSectors; ++i)
	{
		if (PlateAni.IsValidIndex(i)   && PlateAni[i])   { PlateAni[i]->SetVisibility(ESlateVisibility::Collapsed); }
		if (PlateClaim.IsValidIndex(i) && PlateClaim[i]) { PlateClaim[i]->SetVisibility(ESlateVisibility::Collapsed); }
	}
}

// Порт `gachaWheel_UpdateSpinAnimation@0xE62020` — дословно, включая формулу торможения.
//   ★ `g_FrameTimeScale` эталона — множитель кадра: при >= 1.0 шаг берётся БЕЗ него (иначе колесо
//     на быстрой машине улетало бы). У порта роль этого множителя играет DeltaTime × 60
//     (эталонный шаг рассчитан на кадр 1/60 с) — это ЕДИНСТВЕННОЕ отступление, и оно вынужденное:
//     фиксированного кадра у UE нет. Логика ветвления сохранена 1:1.
void UAsdaSoulRouletteSubsystem::TickSpin(float DeltaTime)
{
	switch (SpinState)
	{
	case EAsdaRouletteSpin::Start:
	{
		SetArrowAngle(CurAngle);
		TotalPath = 360.f - CurAngle + kSpinExtra + TargetAngle;
		PrevAngle = CurAngle;
		AngleAccum = 0.f;
		SpinState = EAsdaRouletteSpin::Spin;
		break;
	}
	case EAsdaRouletteSpin::Spin:
	{
		float Delta = CurAngle - PrevAngle;
		if (Delta < 0.f) { Delta += 360.f; }
		AngleAccum += Delta;
		PrevAngle = CurAngle;
		Speed = kSpinSpeed0 - kSpinSpeed0 * AngleAccum / FMath::Max(TotalPath, 1.f) + kSpinFloor;

		if (AngleAccum < TotalPath)
		{
			const float FrameScale = DeltaTime * 60.f;
			CurAngle += (FrameScale >= 1.f) ? Speed : (FrameScale * Speed);
			SetArrowAngle(CurAngle);
			if (CurAngle > 360.f) { CurAngle -= 360.f; }
		}
		else
		{
			CurAngle = TargetAngle;
			SetArrowAngle(CurAngle);
			SpinState = EAsdaRouletteSpin::Stopped;
		}
		break;
	}
	case EAsdaRouletteSpin::Stopped:
	{
		bSession = false;
		for (int32 i = 0; i < kSectors; ++i)
		{
			if (Plate.IsValidIndex(i) && Plate[i]) { Plate[i]->SetVisibility(ESlateVisibility::Collapsed); }
		}
		// Номер сектора считается ИЗ УГЛА — ровно как это делает эталон (и в 6580 тоже, дефект D6).
		int32 Sector = (int32)(TargetAngle / 90.f);
		if (Sector == 4) { Sector = 0; }

		bPlateIsClaim = (NextRowId == -1);
		ShowPlate(Sector, /*bAniLayer*/!bPlateIsClaim, /*bClaimLayer*/bPlateIsClaim);
		PlateSector = Sector;
		// ★ Срок «конца анимации» — из ДАННЫХ СКИНА (его вернул `ShowPlate` через `StartPlateAnim`),
		//   а не из ключа: `FrameInterval` скина = длительность ВСЕГО прогона в мс (см. шапку файла).
		PlateTimerEndS = FPlatformTime::Seconds() + FMath::Max(0.0, PlateAnimSec);
		SpinState = EAsdaRouletteSpin::Idle;

		UE_LOG(LogAsdaRoulette, Log, TEXT("[рулетка] колесо встало на секторе %d; показан слой %s"),
			Sector, bPlateIsClaim ? TEXT("«забрать» — по концу анимации уйдёт 6579")
			                      : TEXT("«анимация» — по концу анимации уйдёт 6575 (цепочка строк)"));
		break;
	}
	default: break;
	}
}

// ★ Ревизия 1-Б М-3: порт позиционирования ветки `a3 == 1` (`RoulettePanel_UpdateRewardSlots`, game.c:470830-470845).
//   Якорь — живая геометрия `Soulmate_Glass_grp` (6699) окна 6500, её отдаёт `UAsdaSoulAppleSubsystem`. Цель — левый
//   верхний угол подложки 67050 (`GOLD_roullete_main`, начало окна): (якорь.X + 57 − 256 + 5, якорь.Y − 75) в единицах
//   макета. Окно сдвигается ОДНИМ общим смещением всех point-анкор детей корневой канвы (как `CenterWindowOnScreen`).
//   Окна 6500 нет — позиция не трогается (рецепт ревизии). Геометрия ещё не отрисована — ждём до 30 кадров.
void UAsdaSoulRouletteSubsystem::TickAnchor()
{
	auto Done = [this](const TCHAR* Why)
	{
		bAnchorPending = false;
		if (Wnd) { Wnd->SetRenderOpacity(1.f); }
		UE_LOG(LogAsdaRoulette, Log, TEXT("[рулетка] позиция окна 67000: %s"), Why);
	};
	UGameInstance* GI = GetGameInstance();
	const UAsdaSoulAppleSubsystem* Apple = GI ? GI->GetSubsystem<UAsdaSoulAppleSubsystem>() : nullptr;
	UWidget* Glass = Apple ? Apple->GetGlassAnchor() : nullptr;
	UWidget* Main = Wnd ? RoulettePlaneOf(Wnd, TEXT("GOLD_roullete_main"), 0) : nullptr;
	if (!Glass || !Main) { Done(TEXT("якоря 6699 (окно 6500) нет — позиция не тронута")); return; }
	FVector2D GMin, GMax, MMin, MMax;
	const bool bG = AsdaUi::WidgetAbsRect(Glass, GMin, GMax);
	const bool bM = AsdaUi::WidgetAbsRect(Main, MMin, MMax);
	if (!bG || !bM)
	{
		if (++AnchorTries > 30) { Done(TEXT("геометрия не отрисована за 30 кадров — позиция не тронута")); }
		return;
	}
	const float Sc = FMath::Max(Main->GetCachedGeometry().Scale, 0.01f);   // абс. пикселей на единицу макета
	const UCanvasPanelSlot* MS = AsdaUi::SlotOf(Main);
	const float MainW = MS ? MS->GetOffsets().Right : 256.f;              // ширина 67050 (эталон: GetSize, vtbl+24)
	const FVector2D Target(GMin.X + (57.f - MainW + 5.f) * Sc, GMin.Y - 75.f * Sc);
	const FVector2D Delta = (Target - MMin) / Sc;
	UCanvasPanel* Root = Wnd && Wnd->WidgetTree ? Cast<UCanvasPanel>(Wnd->WidgetTree->RootWidget) : nullptr;
	if (Root && !Delta.IsNearlyZero(0.5f))
	{
		// ★ ИСПРАВЛЕНИЕ (дедуп №2, В2-B, U15): было «растянутые — не трогаем» (растянутый ребёнок остался бы на месте, окно
		//   уехало бы без него); стало — едут ВСЕ дети, растянутые SPAN-безопасно. У WBP_GOLD_roullete_WIN SPAN-детей нет
		//   (перепись якорей) — результат прежний.
		AsdaUi::ShiftRootChildren(Root, Delta);
	}
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr) { WM->BringWindowToFront(kWindowId); }
	AnchorTarget = Target;
	Done(*FString::Printf(TEXT("у якоря 6699: подложка 67050 → (%.0f,%.0f) абс., сдвиг (%.1f,%.1f) ед."), Target.X, Target.Y, Delta.X, Delta.Y));
}

void UAsdaSoulRouletteSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.SoulRoulette");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	// ★ Волна 1-Б: опрос «Золотого яблока» 6693 переехал в `UAsdaSoulAppleSubsystem` (яблоко — в окне 6500).

	if (!bWndBound) { return; }
	if (bAnchorPending) { TickAnchor(); }

	if (SpinState != EAsdaRouletteSpin::Idle) { TickSpin(DeltaTime); }

	// «Конец анимации плашки» — эталонное сообщение окна a4 == 24 (social.c:7957-7979):
	//   67007..67010 → снова 6575 (цепочка строк), 67015..67018 → 6579 (забрать приз).
	if (PlateSector >= 0 && PlateTimerEndS > 0.0 && FPlatformTime::Seconds() >= PlateTimerEndS)
	{
		const int32 Sector = PlateSector;
		PlateSector = -1;
		PlateTimerEndS = 0.0;
		for (int32 i = 0; i < kSectors; ++i)
		{
			if (PlateAni.IsValidIndex(i) && PlateAni[i]) { PlateAni[i]->SetVisibility(ESlateVisibility::Collapsed); }
		}
		if (bPlateIsClaim)
		{
			// ★ ИСПРАВЛЕНО: эталон в ветке 67015..67018 сперва ГАСИТ панель призов
			//   (`RoulettePanel_UpdateRewardSlots(win, a2, 0, 0)`, game.c:473621-473623) и только
			//   потом шлёт 6579. Порт панель оставлял на экране.
			UpdateRewardSlots(CurRowId, /*bPlayReady*/false, /*bVisible*/false);
			if (UAsdaNetSubsystem* N = Net())
			{
				if (N->SendRouletteClaim6579(Sector)) { ++Tx6579; }
			}
			UE_LOG(LogAsdaRoulette, Log, TEXT("[рулетка] слой «забрать» отыграл → 6579 (сектор %d)"), Sector);
		}
		else if (NextRowId != -1)
		{
			// Цепочка: переходим на следующую строку таблицы и крутим снова (эталон social.c:7961-7972).
			CurRowId = NextRowId;
			NextRowId = -1;
			UpdateRewardSlots(CurRowId, /*bPlayReady*/false, /*bVisible*/true);
			bSession = true;
			SpinState = EAsdaRouletteSpin::Start;
			Speed = kSpinSpeed0;
			CurAngle = 0.f;
			Spin();
			PlaySound2D(kWavSpinStart);
		}
	}
}

// ═══════════════════════════════ 6580 «выдача приза» ═══════════════════════════════════════════

void UAsdaSoulRouletteSubsystem::OnPrize6580(const TArray<uint8>& Body)
{
	if (Body.Num() < 73 || Body[0] != 1) { return; }
	++Rx6580;
	const uint8* P = Body.GetData();
	const int32 WinnerCharId = AsdaBytes::RdI32(&P[1]);    // @1  i32 charId победителя
	const int32 Kind         = (int32)P[5];                // @5  u8  вид приза 0..3
	const int32 ExpValue     = AsdaBytes::RdI32(&P[6]);    // @6  i32 значение (опыт)
	const int32 CurHp        = AsdaBytes::RdI32(&P[67]);   // @67 i32 текущие ОЗ
	const int32 CurMp        = AsdaBytes::RdI16(&P[71]);   // @71 i16 текущие ОМ

	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* N = Net();
	const bool bMine = AsdaSelf::IsSelfCharId(N, WinnerCharId);   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, N2): «charId мой?» — `AsdaSelf::IsSelfCharId` (свой ещё неизвестен ⇒ не мой; было 0 == 0 ⇒ мой)

	// ★ Сектор для ТЕКСТА берётся из ЛОКАЛЬНОГО угла колеса — это и есть дефект D6.
	int32 Sector = (int32)(TargetAngle / 90.f);
	if (Sector == 4) { Sector = 0; }

	const FAsdaRouletteRow* Rec = GetRow(CurRowId);
	FString MsgText;
	FString WavName;
	if (Rec)
	{
		const bool bFromServer = (CVarTextFromServer.GetValueOnGameThread() != 0);
		const int32 TblType  = Rec->Types.IsValidIndex(Sector)  ? Rec->Types[Sector]  : 0;
		const int32 TblValue = Rec->Values.IsValidIndex(Sector) ? Rec->Values[Sector] : 0;
		const int32 TblCount = Rec->Counts.IsValidIndex(Sector) ? Rec->Counts[Sector] : 0;
		// Дефект D6: 1:1 — тип берётся из ТАБЛИЦЫ; исправление — из пакета (авторитет сервера).
		const int32 UseType = bFromServer ? Kind : TblType;

		switch (UseType)
		{
		case 0:
			MsgText = Loc(8071);
			break;
		case 1:
			// Эталон: `sprintf(msg, loc 8072, значение_таблицы/10, пакет@6)`.
			MsgText = AsdaUi::FormatPrintfArgs(Loc(8072),
				{ FString::FromInt(TblValue / 10), FString::FromInt(ExpValue) });
			break;
		case 2:
		{
			// Эталон: `sprintf(msg, loc 8073, имя предмета, количество_таблицы)`.
			FString ItemName;
			if (const UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr)
			{
				ItemName = Txt->GetItemName(TblValue);
			}
			MsgText = AsdaUi::FormatPrintfArgs(Loc(8073), { ItemName, FString::FromInt(TblCount) });
			break;
		}
		case 3:
			MsgText = AsdaUi::FormatPrintfArgs(Loc(8074), { FString::FromInt(TblValue) });
			break;
		default:
			break;
		}
		if (Rec->Sounds.IsValidIndex(Sector) && !Rec->Sounds[Sector].IsEmpty() && Rec->Sounds[Sector] != TEXT("-1"))
		{
			WavName = Rec->Sounds[Sector] + TEXT(".wav");
		}
	}

	// Эффект в точке победителя: `Effect_SpawnByName(..., jeb, 1, x, y, z, 0, posVec, 0, /*attach*/0, 0, 1.0f)`
	//   — привязка 0 = статично в мировой точке, поэтому берём общий статический спавн эффект-BP.
	AActor* Winner = nullptr;
	if (UAsdaWorldSubsystem* World = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
	{
		Winner = World->FindPlayerActorByCharId(WinnerCharId);
	}
	if (Winner && Kind >= 0 && Kind < 4)
	{
		AAsdaEffectActor::SpawnByJebName(Winner->GetWorld(), kJebByKind[Kind],
			Winner->GetActorLocation(), FRotator::ZeroRotator, nullptr);
		// Звук: свой — 2D, чужой — 3D в точке победителя (`effectSlots[112].field_40 == 1`).
		if (!WavName.IsEmpty())
		{
			if (bMine) { PlaySound2D(WavName); }
			else if (UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
			{
				Snd->PlaySound3D(FName(*WavName), Winner->GetActorLocation());
			}
		}
	}

	// Строка в чат — ТОЛЬКО победителю (эталонный гейт `pkt+1 == g_LocalPlayer+1052`).
	if (bMine)
	{
		bSession = false;      // эталон: `self[505] = 0.0`
		ChatPrize(MsgText);
	}
	CurRowId = 0;              // эталон: `self[498] = 0.0`

	UE_LOG(LogAsdaRoulette, Log,
		TEXT("[рулетка] 6580: победитель charId=%d%s, вид приза %d, сектор (по углу) %d, ОЗ=%d ОМ=%d"),
		WinnerCharId, bMine ? TEXT(" (я)") : TEXT(""), Kind, Sector, CurHp, CurMp);
}

// ═══════════════════════════════ НОТИСЫ, ЗВУК, ДАМП ════════════════════════════════════════════

void UAsdaSoulRouletteSubsystem::ChatPrize(const FString& Text) const
{
	if (Text.IsEmpty()) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr;
	if (!Msg) { return; }
	FAsdaMsgParams P;
	P.Category  = EAsdaMsgCategory::Progress;   // награда игроку — «прогресс», маршрут решает таблица
	P.Text      = Text;
	P.bUseColor = true;
	P.Color     = kTipYellow;   // эталонный −11514
	P.TtlMs     = 10000.f;
	P.SrcOpcode = 6580;
	Msg->Post(P);
}

void UAsdaSoulRouletteSubsystem::ChatRefuse(int32 LocId) const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr;
	if (!Msg) { return; }
	FAsdaMsgParams P;
	P.Category  = EAsdaMsgCategory::Social;
	P.LocId     = LocId;
	P.bUseColor = true;
	P.Color     = kChatRed;     // эталонный −65536
	P.TtlMs     = 10000.f;
	P.SrcOpcode = 6576;
	Msg->Post(P);
}

void UAsdaSoulRouletteSubsystem::PlaySound2D(const FString& WavName) const
{
	if (WavName.IsEmpty()) { return; }
	UGameInstance* GI = GetGameInstance();
	if (UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		Snd->PlaySound2D(FName(*WavName));
	}
}

FString UAsdaSoulRouletteSubsystem::Loc(int32 Id) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` — теги срезаны, маркеры как в таблице (как было).
	return AsdaNotice::Loc(GetGameInstance(), Id);
}

FString UAsdaSoulRouletteSubsystem::DumpState() const
{
	FString S = TEXT("══ РУЛЕТКА СОУЛМЕЙТА ══\n");
	S += FString::Printf(TEXT("  таблица: %d строк (%s)\n"), Rows.Num(), kPrizeTablePath);
	S += FString::Printf(TEXT("  окно 67000: %s · сеть: %s\n"),
		bWndBound ? TEXT("привязано") : TEXT("не привязано"), bNetBound ? TEXT("привязана") : TEXT("НЕТ"));
	S += FString::Printf(TEXT("  пакеты: 6575=%d 6579=%d · принято 6576=%d 6580=%d\n"),
		Tx6575, Tx6579, Rx6576, Rx6580);
	S += FString::Printf(TEXT("  колесо: состояние %d, угол %.1f° → %.1f°, скорость %.2f, путь %.1f, накоплено %.1f\n"),
		(int32)SpinState, CurAngle, TargetAngle, Speed, TotalPath, AngleAccum);
	S += FString::Printf(TEXT("  строка текущая %d, следующая %d, сессия %s\n"),
		CurRowId, NextRowId, bSession ? TEXT("идёт") : TEXT("нет"));
	if (const FAsdaRouletteRow* R = GetRow(CurRowId))
	{
		for (int32 s = 0; s < kSectors; ++s)
		{
			S += FString::Printf(TEXT("    сектор %d: тип %d, значение %d, кол-во %d, вес %d, вкл %d, звук %s\n"), s,
				R->Types.IsValidIndex(s) ? R->Types[s] : -1,
				R->Values.IsValidIndex(s) ? R->Values[s] : -1,
				R->Counts.IsValidIndex(s) ? R->Counts[s] : -1,
				R->Weights.IsValidIndex(s) ? R->Weights[s] : -1,
				R->Enabled.IsValidIndex(s) ? R->Enabled[s] : -1,
				R->Sounds.IsValidIndex(s) ? *R->Sounds[s] : TEXT("?"));
		}
	}
	return S;
}

// ── Консольные точки (диагностика ТОЛЬКО реальным путём: те же функции, что и у игрока) ────────
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaRouletteDump(
	TEXT("asda.Soul.Roulette.Dump"),
	TEXT("Состояние рулетки: таблица, окно, колесо, счётчики пакетов."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& /*Args*/, UWorld* W)
		{
			UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
			if (UAsdaSoulRouletteSubsystem* S = GI ? GI->GetSubsystem<UAsdaSoulRouletteSubsystem>() : nullptr)
			{
				UE_LOG(LogAsdaRoulette, Log, TEXT("%s"), *S->DumpState());
			}
		}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaRouletteOpen(
	TEXT("asda.Soul.Roulette.Open"),
	TEXT("asda.Soul.Roulette.Open [0|1] — ДИАГНОСТИКА: закрыть/открыть окно рулетки 67000 (в игре окно открывает ответ 6576 код 1 на «Золотое яблоко»)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* W)
		{
			UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
			UAsdaSoulRouletteSubsystem* S = GI ? GI->GetSubsystem<UAsdaSoulRouletteSubsystem>() : nullptr;
			if (!S) { return; }
			const bool bOpen = (Args.Num() == 0) || (FCString::Atoi(*Args[0]) != 0);
			if (bOpen) { S->OpenWindow(); } else { S->CloseWindow(); }
		}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaRouletteSpin(
	TEXT("asda.Soul.Roulette.Spin"),
	TEXT("Отправить 6575 «крутить» тем же путём, что и «Золотое яблоко» (6693)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& /*Args*/, UWorld* W)
		{
			UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
			if (UAsdaSoulRouletteSubsystem* S = GI ? GI->GetSubsystem<UAsdaSoulRouletteSubsystem>() : nullptr)
			{
				S->Spin();
			}
		}));
#endif // ASDA_DEV_CONSOLE
