// Copyright (c) 2026 @sitxovski (Discord)
#include "World/AsdaAdminSubsystem.h"
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Resources/AsdaGameUserSettings.h"   // ★ 19.10.2026: GetCameraTurnScale — чувствительность поворота камеры
#include "UI/AsdaUiHelpers.h"   // ★ Аудит №4 (B1): AsdaUi::SlotOf/SlotRect — общая точка слота канваса

#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaPlayerController.h"   // ★ раскладка клавиш: IsCmdKeyDown (данные из Shortcutkey.ini)
#include "World/AsdaKeybindConfig.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaVehicleRideSubsystem.h"   // приоритет F12 у пасхалки «Транспорт»
#include "Npc/AsdaNpcGmOnly.h"   // ★ 09.11.2026: простой гейт «имя GM_» (решение владельца) — сверх прав сервера
#include "Login/AsdaButtonRelay.h"
#include "Login/AsdaText.h"   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): подписи GM-меню — строки DT_GameText 990600… (решение владельца 09.10)
#include "Text/AsdaLang.h"    // ★ 10.10.2026: FAsdaLang::OnLanguageChanged — подписи меню при смене языка на лету
#include "AsdaNetSubsystem.h"   // ★ пункт «Пересчитать рейтинги»: опкод 6020 (серверный патч P25)
#include "UI/AsdaNotice.h"           // ★ 12.10.2026 (античит П6): нотисы пункта «Заблокировать учётку» в системный чат
#include "Text/AsdaTextSubsystem.h"  // ★ 12.10.2026 (античит П6): FormatById — подстановки [ss1]/[sd1] в текст подтверждения
#include "Components/EditableText.h" // ★ 12.10.2026 (античит П6): самотест вводит часы в поле нумпада
#include "Containers/Ticker.h"         // ★ 19.10.2026: DeferMenuAction — действие меню на следующем тике
#include "AsdaBytes.h"               // ★ 12.10.2026 (античит П6): самотест сверяет тело 6094 побайтно (RdU16/RdI32, WrI16 для 4011)
#include "Test/AsdaSelfTestKit.h"   // ★ античит А2: каркас самотеста гейта GM-меню (только вне Shipping)

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Engine/Font.h"
#include "Framework/Application/SlateApplication.h"
#include "World/AsdaOrbitCursor.h"   // ★ 07.11.2026: курсор при зажатой ПКМ скрыт и стоит на месте
#include "HAL/IConsoleManager.h"   // ★ ключи отката пункта «Скрыться»: asda.Gm.Hide / asda.Gm.HideAckTimeoutSec

DEFINE_LOG_CATEGORY_STATIC(LogAsdaAdmin, Log, All);

// ★★★ КЛЮЧ ОТКАТА пункта «Скрыться» (опкод 6005). 1 = пункт работает, 0 = пункт остаётся в списке,
//   но НИЧЕГО не отправляет и честно говорит об этом в подписи и в логе. Выключать нужно, если
//   команда начнёт мешать (например, сервер откажет и посыплются отказы в лог).
static ASDA_DEV_CVAR(int32, CVarGmHideEnable,
	TEXT("asda.Gm.Hide"), 1,
	TEXT("GM-меню, пункт «Скрыться» (опкод 6005): 1 = включён, 0 = отключён (пакет не отправляется)"),
	ECVF_Default);

// ★ Сколько секунд ждём ответ 6006, прежде чем снять «ждём» и разрешить повторное нажатие.
//   Сервер отвечает ВСЕГДА (и на успех, и на отказ), так что срабатывание таймаута = потеря связи.
static ASDA_DEV_CVAR(float, CVarGmHideAckTimeout,
	TEXT("asda.Gm.HideAckTimeoutSec"), 3.0f,
	TEXT("Таймаут ожидания ответа 6006 на GM-команду «Скрыться», секунд"),
	ECVF_Default);

// Пути к ассетам пользователя (окна собраны в редакторе из JUI-скинов — трогать их код не должен).
static const TCHAR* kAdminMenuWbp  = TEXT("/Game/AsdaAssets/UI/Admin/Admin_menu.Admin_menu");
static const int32  kAdminZ        = 9000;   // выше всего игрового UI (мини-карта ~100, диалоги ~500)

// ★ Античит А2 (мера 7, 12.10.2026): прежний клиентский пароль (SHA-256 + зашитый хэш) удалён — права GM-меню
//   выдаёт только эхо сервера 6006 «принято» (`RequestAdminMode` / `TickAuthProbe` ниже).

// ───────────────────────────── Жизненный цикл ─────────────────────────────
void UAsdaAdminSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Реестр функций GM-меню. Новый пункт = ОДНА регистрация здесь (или RegisterFunction извне).
	// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): подписи меню оператора — строки DT_GameText на языке сессии (990600…990623).
	RegisterFunction(TEXT("FREECAM"),
		FText::FromString(FAsdaText::Loc(990600)),   // «Свободная камера»
		FText::FromString(FAsdaText::Loc(990601)),   // «WASD — полёт, ПКМ — обзор, …»
		[this](APlayerController* PC) { EnterFreeCam(PC); });
	SetFunctionLoc(TEXT("FREECAM"), 990600, 990601);

	// ★★★ [ОРИГИНАЛ] «Скрыться» — GM-невидимость, опкод 6005 (`Send_HideCommand@0x54E970`).
	//   Полный поток: `Reports/gm_commands_hide_2026-09-09.md`.
	//   ЭТО СТОКОВАЯ КОМАНДА СЕРВЕРА, не наш патч: серверную часть трогать не потребовалось.
	//   Оригинал шлёт её НЕ чат-строкой, а отдельным пакетом в конверте СЕКРЕТ+MD5 с телом 2 байта,
	//   где второй байт = желаемое состояние. Мы шлём то же самое, байт в байт.
	//   ★ ПЕРЕКЛЮЧАТЕЛЬ: подпись показывает состояние, ПОДТВЕРЖДЁННОЕ сервером (эхо 6006),
	//   а не то, что мы попросили. Пока ответа нет — «ждём…», по таймауту заявка снимается.
	//   ⚠ Права: сервер требует UserIndex учётки в 0…5000 (`sub_52ECF0`). GMFlag из
	//   dbo.CharGmInfo для ЭТОЙ команды НЕ проверяется — в отличие от прочих операторских.
	RegisterToggleFunction(TEXT("GM_HIDE"),
		[this]() -> FText
		{
			UGameInstance* GI = GetGameInstance();
			const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			if (CVarGmHideEnable.GetValueOnGameThread() == 0)
			{
				return FText::FromString(FAsdaText::Loc(990602));   // «Скрыться: ОТКЛЮЧЕНО (asda.Gm.Hide 0)»
			}
			if (!Net) { return FText::FromString(FAsdaText::Loc(990603)); }   // «Скрыться: нет связи»
			if (Net->IsGmHidePending()) { return FText::FromString(FAsdaText::Loc(990604)); }   // «Скрыться: ждём ответ сервера…»
			// ★★★ [ОРИГИНАЛ] Оператор при ВХОДЕ в мир и после КАЖДОГО ПОРТАЛА уже скрыт: сервер
			//   пишет 300 в `CUser+0x3BC3B` безусловно (`CharMake`/`CharPortalMake`), клиенту об
			//   этом не сообщая. Поэтому «ВКЛ» здесь — штатное состояние сразу после входа, а не
			//   след нажатия. Пометка «(автоматически)» отличает вычисленное состояние от
			//   подтверждённого эхом 6006 — иначе оператор решил бы, что меню врёт.
			if (Net->IsGmHidden())
			{
				return FText::FromString(FAsdaText::Loc(Net->IsGmHiddenFromEntry()
					? 990605 : 990606));   // «Скрыться: ВКЛ (автоматически при входе)» / «Скрыться: ВКЛ»
			}
			return FText::FromString(FAsdaText::Loc(990607));   // «Скрыться: ВЫКЛ»
		},
		// ★ Подпись описывает НАШЕ поведение, а не эталонное: у оригинала свой персонаж пропадал и
		//   с собственного экрана, у нас он полупрозрачен (ключ `asda.Gm.HideTranslucent`).
		FText::FromString(FAsdaText::Loc(990608)),   // «невидимость оператора: …» (одна строка таблицы)
		[this](APlayerController* /*PC*/)
		{
			if (CVarGmHideEnable.GetValueOnGameThread() == 0)
			{
				UE_LOG(LogAsdaAdmin, Warning, TEXT("[admin] «Скрыться» отключено ключом asda.Gm.Hide 0 — пакет не отправлен"));
				return;
			}
			UGameInstance* GI = GetGameInstance();
			UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			if (!Net)
			{
				UE_LOG(LogAsdaAdmin, Error, TEXT("[admin] «Скрыться»: нет сетевой подсистемы"));
				return;
			}
			// Повторное нажатие, пока прошлый запрос без ответа, отбрасываем: сервер сверяет
			//   анти-реплей-счётчик и второй пакет ничего не ускорит, а состояние запутает.
			if (Net->IsGmHidePending())
			{
				UE_LOG(LogAsdaAdmin, Log, TEXT("[admin] «Скрыться»: предыдущий запрос ещё без ответа (%.1f с) — пропускаем"),
					Net->GmHidePendingAge());
				return;
			}
			const bool bWant = !Net->IsGmHidden();   // переключаем ОТ подтверждённого состояния
			const bool bSent = Net->SendGmHide6005(bWant);
			UE_LOG(LogAsdaAdmin, Log, TEXT("[admin] «Скрыться»: просим %s — запрос 6005 %s "
				"(состояние сменится только по ответу 6006; отказ = нет прав оператора, "
				"смотрите GameServer\\Log\\Log_Operator)"),
				bWant ? TEXT("СКРЫТЬ") : TEXT("ПОКАЗАТЬ"), bSent ? TEXT("отправлен") : TEXT("НЕ отправлен"));
		});
	SetFunctionLoc(TEXT("GM_HIDE"), 0, 990608);   // подпись — `DynamicTitle` (всегда язык сессии), подсказка — по loc

	// ★ Пересчёт званий и рейтингов по требованию (серверные патчи P25 в GameServer
	//   и P23 в DBManager). Режимов ЧЕТЫРЕ — они же значения байта payload опкода 6020.
	//
	//   ★★★ ЧЕСТНО О ГРАНИЦАХ (закон never-invent-only-original). В оригинале
	//   ПЕРИОДИЧЕСКОЙ проверки условий славы нет вовсе: `TimerProcess@0x5431C0`
	//   трогает ровно одно звание (id 22, «время в игре»), всё остальное начисляется
	//   ТОЛЬКО по событию (`sub_54A050@0x54A050`, 204 вызова по серверу). Поэтому
	//   «пересчитать» можно лишь то, чей прогресс где-то ЛЕЖИТ. Разбор всех 280
	//   записей `ConfigSet\FameInfo.txt`: 150 званий пересчитываемы, 25 — нет
	//   (значение приходит в самом событии и нигде не сохраняется), 92 выключены
	//   колонкой 1, 12 не описаны ни одной веткой условия. Персонажи НЕ В ИГРЕ не
	//   охвачены: накопители живут в `CUser`. Всё это сказано и в подписях пунктов —
	//   владелец не должен узнавать про ограничение из исходника.
	//
	//   ⚠ Права решает СЕРВЕР, а не это меню: нужен UserIndex 0…5000 (учётка ADM_*)
	//   и GMFlag=1 в dbo.CharGmInfo. Под обычной учёткой сервер откажет молча.
	//   Результат сервер присылает строками СТОКОВОГО 6561 — они появятся в чате.
	{
		// Общая точка: один и тот же обработчик на все четыре пункта, отличается
		// только режимом. Новый режим = одна строка в таблице ниже.
		// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): подпись и подсказка — id строк DT_GameText (язык сессии), не литералы.
		struct FRecalcMode
		{
			const TCHAR* Id;
			int32        TitleLoc;   // строка-подпись пункта
			int32        HintLoc;    // строка-подсказка пункта
			uint8        What;
		};
		static const FRecalcMode Modes[] =
		{
			{ TEXT("RECALC_ALL"),    990612, 990613, 0 },   // «Пересчитать все звания и рейтинги»
			{ TEXT("RECALC_TITLES"), 990615, 990616, 3 },   // «Только звания: проверить условия»
			{ TEXT("RECALC_RANK"),   990617, 990618, 1 },   // «Только соревновательные звания 65…69»
			{ TEXT("RECALC_FAME"),   990619, 990620, 2 },   // «Только место в топе по очкам славы»
		};

		for (const FRecalcMode& M : Modes)
		{
			const uint8 What = M.What;
			RegisterFunction(FName(M.Id),
				FText::FromString(FAsdaText::Loc(M.TitleLoc)),
				FText::FromString(FAsdaText::Loc(M.HintLoc)),
				[this, What](APlayerController* /*PC*/)
				{
					UGameInstance* GI = GetGameInstance();
					UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
					if (!Net)
					{
						UE_LOG(LogAsdaAdmin, Error, TEXT("[admin] пересчёт: нет сетевой подсистемы"));
						return;
					}
					const bool bOk = Net->SendGmRatingRecalc6020(What);
					UE_LOG(LogAsdaAdmin, Log, TEXT("[admin] пересчёт (режим %d): запрос 6020 -> %s "
						"(ответ придёт строками 6561 в чат; при отказе сервер молчит — смотрите "
						"GameServer\\Log\\Log_Operator)"), (int32)What, bOk ? TEXT("отправлен") : TEXT("НЕ отправлен"));
				});
			SetFunctionLoc(FName(M.Id), M.TitleLoc, M.HintLoc);
		}
	}

	// ═══ ★★★ [ОРИГИНАЛ] «Заблокировать учётку цели на N часов» — опкод 6094 → сервер `sub_55A510` → DB 2271 → `UPHackBlock` ═══
	//   Поток и раскладка — `RE/docs/client/logic/gm_block_6094.md`; разбор — `Reports/anticheat/wave_P6_gm_block.md`.
	//   У розничного клиента пункта нет (отправитель `net_sendCharFieldSyncOpcode6094@0xA13680` в рознице зовётся только
	//   самодоносом 6095) — UI пункта наш, ПАКЕТ эталонный (`UAsdaNetSubsystem::SendGmBlock6094`, тело байт в байт).
	//   Цель — ВЫБРАННЫЙ персонаж игрока (эталонный `g_nTargetUserIdx` = `AAsdaPlayerController::TargetActor`): сервер
	//   ищет цель по сессионному userNum, поэтому она обязана быть онлайн в этом GameServer.
	//   Срок — штатное окно количества NUMPAD_WIN 1410 (`ShowNumPad`, подпись 12124 «количество» — другой подписи у окна
	//   нет; единицы названы в пункте меню и в окне подтверждения), затем TWOBUT_MSGBOX 1520 «ОК/Отмена» (блок учётки —
	//   ★ Нотисы (правило audit-notices-on-new-system): у ОРИГИНАЛА их нет вовсе (ни у клиента — отправителя нет, ни от
	//   сервера — ответа нет). Строки ниже — ПОРТОВЫЕ подсказки оператору в СИСТЕМНЫЙ ЧАТ (категория System, как прочие
	//   служебные строки порта); факт отправки ≠ факт блока: сервер молчит и при отказе гейта (P47/`sub_52ECF0`).
	//   ⚠ Права решает СЕРВЕР: UserIndex 0…5000 + под P47 GM-флаг `CharGmInfo`; без них пакет тихо сброшен (ac0r).
	RegisterFunction(TEXT("GM_BLOCK"),
		FText::FromString(FAsdaText::Loc(990633)),   // «Заблокировать учётку цели (на N часов)»
		FText::FromString(FAsdaText::Loc(990634)),   // «выбранный игрок → часы → подтверждение; 6094 → UPHackBlock …»
		[this](APlayerController* PC) { ActivateGmBlock(PC); });
	SetFunctionLoc(TEXT("GM_BLOCK"), 990633, 990634);

	// Итог регистрации — в лог: единственный способ убедиться снаружи (список приватный), что
	//   пункт заведён и не потерялся из-за идемпотентности `RegisterFunction`.
	{
		FString Ids;
		for (const FAsdaAdminFunction& F : Functions)
		{
			if (!Ids.IsEmpty()) { Ids += TEXT(", "); }
			Ids += F.Id.ToString();
			if (F.DynamicTitle) { Ids += TEXT("(переключатель)"); }
		}
		UE_LOG(LogAsdaAdmin, Log, TEXT("[admin] реестр GM-меню: %d пункт(ов) — %s"), Functions.Num(), *Ids);
	}
	// ★ 10.10.2026 (язык на лету): подписи пунктов с номерами loc — заново на новом языке (и у пунктов транспорта).
	FAsdaLang::OnLanguageChanged().AddWeakLambda(this, [this]()
	{
		for (FAsdaAdminFunction& F : Functions) { SetFunctionLoc(F.Id, F.TitleLoc, F.HintLoc); }
		// ★ 19.10.2026 (ревизия М-14): открытое меню — пересобрать строки, вкладки и листание на новом языке.
		if (MenuWidget) { RebuildRows(); }
	});
}

void UAsdaAdminSubsystem::Deinitialize()
{
	FAsdaLang::OnLanguageChanged().RemoveAll(this);   // ★ 10.10.2026: подписка «язык на лету»
	if (DeferTickHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(DeferTickHandle); DeferTickHandle.Reset(); }
	PendingMenuActions.Reset();
	ResetSession();
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaAdminSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// ★ Своего сброса здесь ЗАВОДИТЬ НЕ НАДО — он уже есть (`ResetSession`, её зовёт
	//   `AsdaLoginGameMode` при загрузке карты логина). Не хватало ровно одного: вызова при
	//   РАЗРЫВЕ связи — тогда права оператора переживали обрыв, хотя выдаются на сессию.
	//   При смене карты права сохраняются: сессия та же (закон `check-existing-before-creating`).
	if (!AsdaIsFullSessionReset(Reason)) { return; }
	ResetSession();
}

void UAsdaAdminSubsystem::RegisterFunction(FName Id, const FText& Title, const FText& Hint,
	TFunction<void(APlayerController*)> Activate)
{
	for (const FAsdaAdminFunction& F : Functions)
	{
		if (F.Id == Id) { return; }   // идемпотентно: повторная регистрация того же id — не дублируем
	}
	FAsdaAdminFunction F;
	F.Id = Id; F.Title = Title; F.Hint = Hint; F.Activate = MoveTemp(Activate);
	Functions.Add(MoveTemp(F));
}

void UAsdaAdminSubsystem::SetFunctionLoc(FName Id, int32 TitleLoc, int32 HintLoc)
{
	for (FAsdaAdminFunction& F : Functions)
	{
		if (F.Id != Id) { continue; }
		F.TitleLoc = TitleLoc;
		F.HintLoc = HintLoc;
		// ★ 12.10.2026 (античит П6): строки, которых в DT_GameText ещё нет (ведомость не применена), подпись НЕ стирают —
		//   остаётся текст регистрации; иначе пункт выглядел бы пустой строкой и был бы невидим в списке.
		if (TitleLoc > 0) { const FString S = FAsdaText::Loc(TitleLoc); if (!S.IsEmpty()) { F.Title = FText::FromString(S); } }
		if (HintLoc > 0)  { const FString S = FAsdaText::Loc(HintLoc);  if (!S.IsEmpty()) { F.Hint  = FText::FromString(S); } }
		return;
	}
}

bool UAsdaAdminSubsystem::HasFunction(FName Id) const
{
	for (const FAsdaAdminFunction& F : Functions) { if (F.Id == Id) { return true; } }
	return false;
}

bool UAsdaAdminSubsystem::ActivateFunctionById(FName Id, APlayerController* PC)
{
	if (!bAuthorized) { return false; }   // тот же гейт, что у `ActivateSelected` (античит А2, мера 7)
	for (const FAsdaAdminFunction& F : Functions)
	{
		if (F.Id != Id) { continue; }
		UE_LOG(LogAsdaAdmin, Log, TEXT("[admin] применяем функцию '%s' (по id)"), *Id.ToString());
		if (F.Activate) { F.Activate(PC); }
		return true;
	}
	return false;
}

// ═══ ★★★ Пункт «Заблокировать учётку цели на N часов» (античит П6, 12.10.2026) ═══════════════════════════════════
// Верхняя граница срока в окне количества. Эталонных ограничений у 6094 нет (i32 «время» уходит в SQL как есть);
//   −1 («30 лет», как у `UPHackBlock`) из нумпада не ввести — бессрочный блок остаётся за SQL владельца (лестница §5 п.5).
static ASDA_DEV_CVAR(int32, CVarGmBlockMaxHours,
	TEXT("asda.Gm.BlockMaxHours"), 24 * 365,
	TEXT("GM-блок учётки (6094): максимум часов в окне ввода (1..N); 0 = без верхней границы"),
	ECVF_Default);

void UAsdaAdminSubsystem::ActivateGmBlock(APlayerController* PC)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem*    Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaWorldSubsystem*  WS  = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	UAsdaWindowSubsystem* WM  = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!Net || !WS || !WM)
	{
		UE_LOG(LogAsdaAdmin, Error, TEXT("[admin] блок учётки: нет подсистем (net=%d world=%d win=%d)"), Net != nullptr, WS != nullptr, WM != nullptr);
		return;
	}
	// 1. Цель — выбранный персонаж (эталонный `g_nTargetUserIdx`): актор цели → сессионный userNum → запись соседа (имя).
	const AAsdaPlayerController* APC = Cast<AAsdaPlayerController>(PC);
	const AActor* Target = APC ? APC->TargetActor.Get() : nullptr;
	const int32 TargetUserNum = Target ? WS->FindUserIdxByActor(Target) : -1;
	FString TargetName; int32 Cls = 0, Lvl = 0, Clan = -1;
	if (TargetUserNum >= 0 && Net->IsSelfUserIdx(TargetUserNum))
	{
		// Сервер цель по номеру найдёт — это была бы блокировка СОБСТВЕННОЙ учётки оператора.
		AsdaNotice::Chat(GI, EAsdaMsgCategory::System, 990636);   // «Блок учётки: нельзя заблокировать собственную учётку»
		UE_LOG(LogAsdaAdmin, Warning, TEXT("[admin] блок учётки: цель — собственный персонаж (userNum %d) — отказ"), TargetUserNum);
		return;
	}
	if (TargetUserNum < 0 || !WS->GetNetPlayerInfo(TargetUserNum, TargetName, Cls, Lvl, Clan))
	{
		// Нет цели, цель — моб/NPC или не игрок из реестра соседей: серверу нужен userNum ЖИВОЙ сессии игрока.
		AsdaNotice::Chat(GI, EAsdaMsgCategory::System, 990635);   // «Блок учётки: сначала выберите персонажа игрока»
		UE_LOG(LogAsdaAdmin, Warning, TEXT("[admin] блок учётки: цель не выбрана или не игрок (актор=%s, userNum=%d) — отказ"),
			Target ? *Target->GetName() : TEXT("нет"), TargetUserNum);
		return;
	}
	// 2. Срок в часах — штатное окно количества (центр экрана: открываем не от инвентаря, у курсора нечего «продолжать»).
	//   Диапазон 1..Max проверяет само окно (эталонные OK-хендлеры); вне диапазона — отмена без операции.
	const int32 MaxHours = CVarGmBlockMaxHours.GetValueOnGameThread();
	TWeakObjectPtr<UAsdaAdminSubsystem> Self(this);
	WM->ShowNumPad(MaxHours, [Self, TargetUserNum, TargetName](bool bOk, int32 Hours)
	{
		UAsdaAdminSubsystem* A = Self.Get();
		UGameInstance* GI2 = A ? A->GetGameInstance() : nullptr;
		UAsdaWindowSubsystem* WM2 = GI2 ? GI2->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UAsdaTextSubsystem*   Txt = GI2 ? GI2->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
		if (!bOk || Hours <= 0 || !A || !WM2 || !Txt)
		{
			UE_LOG(LogAsdaAdmin, Log, TEXT("[admin] блок учётки «%s» (userNum %d): ввод срока отменён (ok=%d, часов=%d)"),
				*TargetName, TargetUserNum, bOk ? 1 : 0, Hours);
			return;
		}
		// 3. Подтверждение — двухкнопочное окно 1520: текст 990637 «Заблокировать учётку игрока «[ss1]» на [sd1] ч? …».
		const FString Msg = Txt->FormatById(990637, { TargetName }, { Hours });
		AsdaEnterGuard::NoEnter(WM2)->ShowGameConfirm(Msg, [Self, TargetUserNum, TargetName, Hours](bool bYes)
		{
			UAsdaAdminSubsystem* A2 = Self.Get();
			UGameInstance* GI3 = A2 ? A2->GetGameInstance() : nullptr;
			UAsdaNetSubsystem* Net3 = GI3 ? GI3->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			if (!bYes || !Net3)
			{
				UE_LOG(LogAsdaAdmin, Log, TEXT("[admin] блок учётки «%s» на %d ч: отменено в окне подтверждения"), *TargetName, Hours);
				return;
			}
			// 4. Сам пакет. ★ Права не перепроверяем вторично: гейт — сервер; свой гейт стоял на входе в меню.
			const bool bSent = Net3->SendGmBlock6094(TargetUserNum, Hours);
			if (bSent)
			{
				// «Блок учётки: запрос на [sd1] ч для «[ss1]» отправлен. Подтверждения сервер не присылает — след в Log_SpeedHack»
				AsdaNotice::ChatFmt(GI3, EAsdaMsgCategory::System, 990638, { TargetName }, { Hours });
			}
			else
			{
				AsdaNotice::ChatFmt(GI3, EAsdaMsgCategory::System, 990639, { TargetName }, { Hours });   // «… пакет 6094 не отправлен»
			}
			UE_LOG(LogAsdaAdmin, Log, TEXT("[admin] блок учётки «%s» (userNum %d) на %d ч: запрос 6094 %s "
				"(ответа сервер не шлёт; отказ гейта оператора — тихий; след — GameServer\\Log\\Log_SpeedHack, DBManager UPHackBlock)"),
				*TargetName, TargetUserNum, Hours, bSent ? TEXT("отправлен") : TEXT("НЕ отправлен"));
		}, /*GaugeGateSec*/0.f, AsdaLoc::kBtnOk, AsdaLoc::kBtnCancel, AsdaMsgBox::kNoKey, /*EtalonKind*/1);
	}, /*bCenterOnScreen*/true);
	UE_LOG(LogAsdaAdmin, Log, TEXT("[admin] блок учётки: цель «%s» (userNum %d) — открыто окно ввода срока (часы, 1..%d)"),
		*TargetName, TargetUserNum, MaxHours);
}

// ★ Пункт-переключатель: подпись строится на лету, поэтому строка меню всегда показывает
//   ФАКТИЧЕСКОЕ состояние, а не то, что было при регистрации.
void UAsdaAdminSubsystem::RegisterToggleFunction(FName Id, TFunction<FText()> TitleFn, const FText& Hint,
	TFunction<void(APlayerController*)> Activate)
{
	for (const FAsdaAdminFunction& F : Functions)
	{
		if (F.Id == Id) { return; }   // идемпотентно — как и у обычной регистрации
	}
	FAsdaAdminFunction F;
	F.Id = Id; F.Hint = Hint;
	F.DynamicTitle = MoveTemp(TitleFn);
	F.Activate = MoveTemp(Activate);
	Functions.Add(MoveTemp(F));
}

void UAsdaAdminSubsystem::ResetSession()
{
	ExitFreeCam();
	CloseMenuWindow();
	if (bAuthorized) { UE_LOG(LogAsdaAdmin, Log, TEXT("[admin] права сброшены (сессия завершена)")); }
	bAuthorized = false;
	bAuthPending = false;   // ★ античит А2: незавершённая проба 6036 к новой сессии не относится
	PrevDown.Reset();
	// ★ Кэш подписи пункта-переключателя: само состояние GM-невидимости живёт в сетевой подсистеме
	//   и чистится её собственным `ResetSessionState` — здесь сбрасываем только «что показали».
	bLastShownGmHidden = false;
	bLastShownGmPending = false;
}

// ───────────────────────────── Хоткеи ─────────────────────────────
// ★★★ Снять указатели на окна, которые убил `UAsdaWindowSubsystem::CloseAll()` (снос дерева;
//   его делает вход в мир `UAsdaWorldSubsystem::RevealWorld` и разборка подсистемы).
//   `CloseAll` выкидывает окно из реестра и из вьюпорта, НАШ указатель при этом остаётся
//   ненулевым. Без этой чистки: `OpenMenuWindow`/`OpenLoginWindow` выходили по `if (MenuWidget)
//   return;`, а F12 и ESC считали мёртвое окно открытым и «закрывали» его вместо открытия —
//   GM-панель после гачи не поднималась. Разбор — `Reports/minimap_channels_fix.md`.
void UAsdaAdminSubsystem::DropClosedWindows()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// Штатные Close*: они же снимают производное (строки функций, реле) и возвращают фокус миру.
	if (MenuWidget && !WM->IsWindowLive(MenuWidget)) { CloseMenuWindow(); }
}

bool UAsdaAdminSubsystem::PollHotkeys(APlayerController* PC)
{
	if (!PC) { return false; }
	DropClosedWindows();   // ← до разбора клавиш: F12/ESC ниже судят об открытости по указателям
	TickGmHideWatchdog();  // ← «Скрыться»: снять зависшее «ждём» и обновить подпись открытого меню
	TickAuthProbe(PC);     // ← ★ античит А2/ac0r: ответ сервера на пробу 6036 → права и меню (или таймаут)

	// Фронт НАЖАТИЯ клавиши (true только в кадре, когда клавиша перешла из отпущенной в нажатую).
	auto Pressed = [this, PC](const FKey& K) -> bool
	{
		const bool bDown = PC->IsInputKeyDown(K);
		const bool bPrev = PrevDown.FindRef(K);
		PrevDown.Add(K, bDown);
		return bDown && !bPrev;
	};

	// Фронт нажатия по КОДУ ДЕЙСТВИЯ: клавишу берём из раскладки, латч — по коду действия.
	//   ⚠ Раскладку знает только наш контроллер; на чужом (логин-карта) — фронта нет.
	const AAsdaPlayerController* AsdaPC = Cast<AAsdaPlayerController>(PC);
	auto PressedCmd = [&](int32 Cmd) -> bool
	{
		const bool bDown = AsdaPC && AsdaPC->IsCmdKeyDown(Cmd);
		const FKey Latch = FKey(*FString::Printf(TEXT("AsdaCmd%d"), Cmd));   // ключ латча, не ввод
		const bool bPrev = PrevDown.FindRef(Latch);
		PrevDown.Add(Latch, bDown);
		return bDown && !bPrev;
	};

	const bool bShift = PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift);
	const bool bCtrl  = PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl);
	const bool bAlt   = PC->IsInputKeyDown(EKeys::LeftAlt) || PC->IsInputKeyDown(EKeys::RightAlt);

	// ── CTRL+SHIFT+ALT+Q: запрос админ-режима ──
	// ★ 09.11.2026 (владелец): персонаж без префикса «GM_» — ни запроса прав, ни GM-меню по клавишам (сверх проверок сервера).
	const bool bGmName = AsdaNpcGmOnly::SelfIsGm(GetGameInstance());
	const bool bQ = Pressed(EKeys::Q);
	if (bQ && bCtrl && bShift && bAlt && bGmName)
	{
		RequestAdminMode(PC);   // ★ античит А2 (мера 7): права — по ответу сервера (6037/6006), не по паролю
	}

	// ── F12: GM-меню (только авторизованным) ──
	//   ★ ПРИОРИТЕТ У ТРАНСПОРТА: пока игрок за рулём (пасхалка «Транспорт»), F12 означает
	//   «вернуть управление персонажу» и обрабатывается в `UAsdaVehicleRideSubsystem::TickRide`.
	//   Без этой проверки одно нажатие делало бы два дела разом: и высаживало из машины, и открывало
	//   меню. Сам факт езды спрашиваем у подсистемы транспорта — обе живут на `GameInstance`.
	bool bRiding = false;
	if (UGameInstance* GIr = GetGameInstance())
	{
		if (const UAsdaVehicleRideSubsystem* Ride = GIr->GetSubsystem<UAsdaVehicleRideSubsystem>())
		{
			bRiding = Ride->IsRiding();
		}
	}
	// ★ Клавиша берётся из конфига (`EXT FREECAM TOGGLE:`, cmd 86), а не зашита: владелец
	//   переназначает её в `system/Shortcutkey.ini` без пересборки. Дефолт тот же — F12.
	//   Фронт нажатия по-прежнему считает `Pressed`, поэтому опрашиваем ФАКТИЧЕСКУЮ клавишу бинда.
	if (PressedCmd(86) && bAuthorized && bGmName && !bRiding)
	{
		if (MenuWidget) { CloseMenuWindow(); } else { OpenMenuWindow(PC); }
	}

	// ── ESC: выход из свободной камеры, иначе закрытие меню ──
	// ★ РЕВИЗИЯ 2026-09-17 (С6): о поглощении клавиши обязаны сообщить наверх, иначе тем же
	//   нажатием `UAsdaSystemOptionSubsystem::PollHotkeys` откроет ESC-меню 69400 (двойное
	//   срабатывание). Фронт `Pressed` опрашиваем в ЛЮБОМ случае — латч обязан обновиться.
	if (Pressed(EKeys::Escape))
	{
		if (IsFreeCamActive()) { ExitFreeCam();     return true; }
		if (MenuWidget)        { CloseMenuWindow(); return true; }
	}
	return false;
}

// ★★★ Сторож пункта «Скрыться». Делает ровно две вещи и НИЧЕГО не отправляет:
//   1) снимает заявку «ждём ответа», если 6006 не пришёл за отведённое время (закон
//      no-state-latch-before-shown: молчание сервера не должно оставить кнопку мёртвой);
//   2) обновляет подпись строки, если состояние сменилось при ОТКРЫТОМ меню — ответ сервера
//      асинхронный и может прийти уже после того, как меню нарисовано.
void UAsdaAdminSubsystem::TickGmHideWatchdog()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }

	if (Net->IsGmHidePending())
	{
		const double Age = Net->GmHidePendingAge();
		if (Age > (double)FMath::Max(0.5f, CVarGmHideAckTimeout.GetValueOnGameThread()))
		{
			Net->ClearGmHidePending();
			UE_LOG(LogAsdaAdmin, Warning,
				TEXT("[admin] «Скрыться»: сервер не ответил за %.1f с — заявка снята, состояние осталось «%s». "
				     "Кнопка снова активна."), Age, Net->IsGmHidden() ? TEXT("ВКЛ") : TEXT("ВЫКЛ"));
		}
	}

	const bool bHidden  = Net->IsGmHidden();
	const bool bPending = Net->IsGmHidePending();
	if (bHidden != bLastShownGmHidden || bPending != bLastShownGmPending)
	{
		bLastShownGmHidden = bHidden;
		bLastShownGmPending = bPending;
		if (MenuWidget) { RefreshRowTexts(); }
	}
}

// ───────────────────────────── ★ Вход в админ-режим по признаку сервера (античит А2, мера 7) ─────────────────────────────
// Поток и обоснование — шапка `AsdaAdminSubsystem.h`. Пакеты (★ ac0r): проба = `SendGmRightsProbe6036` (операторский
//   GetValue, на сервере только чтение), ответ = обработчик 6037 в `UAsdaNetSubsystem` (`IsOperatorConfirmedByServer`,
//   `GetGmRightsAnswerSeq`). Неотвеченная проба — одна на сессию: таймаут повторов не открывает (решение координатора
//   ac0r: не-оператор с префиксом GM_/ADM_ не должен слать операторские пакеты пачками).
void UAsdaAdminSubsystem::RequestAdminMode(APlayerController* PC)
{
	if (!AsdaNpcGmOnly::SelfIsGm(GetGameInstance())) { return; }   // ★ 09.11.2026: без «GM_» — ничего не шлём и не открываем
	if (bAuthorized) { OpenMenuWindow(PC); return; }   // права уже выданы сервером в этой сессии — сразу меню
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net)
	{
		UE_LOG(LogAsdaAdmin, Warning, TEXT("[admin] запрос админ-режима: нет сетевой подсистемы"));
		return;
	}
	// Сервер уже принимал операторскую команду в этой сессии (например, «Скрыться») — признак есть, проба не нужна.
	if (Net->IsOperatorConfirmedByServer())
	{
		bAuthorized = true;
		UE_LOG(LogAsdaAdmin, Log, TEXT("[admin] права оператора подтверждены сервером ранее (6037 / 6006 «принято») — GM-меню"));
		OpenMenuWindow(PC);
		return;
	}
	if (bAuthPending)
	{
		UE_LOG(LogAsdaAdmin, Log, TEXT("[admin] запрос админ-режима: ждём ответ сервера 6037 (%.1f с)"),
			FPlatformTime::Seconds() - AuthProbeSentAt);
		return;
	}
	// Фильтр: обычный игрок операторский пакет не шлёт. Это ПРЕДСКАЗАНИЕ клиента, прав оно не даёт.
	if (!Net->IsSelfOperator())
	{
		// Log-уровень намеренно: при отладке видно, ПОЧЕМУ меню не открылось.
		UE_LOG(LogAsdaAdmin, Log, TEXT("[admin] запрос админ-режима отклонён: учётка/персонаж не операторские (UserIndex=%u, имя «%s»)"),
			Net->GetUserIndexForDiagnostics(), *Net->GetSelfCharNameForOperatorCheck());
		return;
	}
	// ★ ac0r: неотвеченная проба — одна на сессию. Без прав сервер молчит; повтор — только после явного ответа.
	if (!Net->CanSendGmRightsProbe())
	{
		UE_LOG(LogAsdaAdmin, Log, TEXT("[admin] запрос админ-режима: проба прав этой сессии осталась без ответа — "
			"повторов до конца сессии нет (прав оператора у учётки, по-видимому, нет)"));
		return;
	}
	// Проба: 6036 GetValue — сервер только читает и отвечает 6037 (ничего не меняет, никому не рассылает).
	AuthProbeSeq = Net->GetGmRightsAnswerSeq();
	if (!Net->SendGmRightsProbe6036())
	{
		UE_LOG(LogAsdaAdmin, Warning, TEXT("[admin] запрос админ-режима: проба 6036 не отправлена (нет связи)"));
		return;
	}
	bAuthPending = true;
	AuthProbeSentAt = FPlatformTime::Seconds();
	UE_LOG(LogAsdaAdmin, Log, TEXT("[admin] запрос админ-режима: проба прав 6036 (только чтение на сервере) — ждём 6037"));
}

void UAsdaAdminSubsystem::TickAuthProbe(APlayerController* PC)
{
	if (!bAuthPending) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { bAuthPending = false; return; }
	// Ответ 6037 на пробу (или 6006 «принято» от пункта «Скрыться») — признак оператора у сети.
	if (Net->GetGmRightsAnswerSeq() != AuthProbeSeq || Net->IsOperatorConfirmedByServer())
	{
		bAuthPending = false;
		if (Net->IsOperatorConfirmedByServer())
		{
			bAuthorized = true;   // ★ только на сессию: ни в ini, ни в SaveGame не пишем
			UE_LOG(LogAsdaAdmin, Log, TEXT("[admin] сервер подтвердил права оператора (6037) — админ-режим включён (F12 = GM-меню)"));
			OpenMenuWindow(PC);
		}
		return;
	}
	const double Age = FPlatformTime::Seconds() - AuthProbeSentAt;
	if (Age > (double)FMath::Max(0.5f, CVarGmHideAckTimeout.GetValueOnGameThread()))
	{
		bAuthPending = false;   // закон no-state-latch-before-shown: молчание сервера не оставляет вечное «ждём»
		UE_LOG(LogAsdaAdmin, Warning, TEXT("[admin] запрос админ-режима: сервер не ответил на пробу прав за %.1f с — заявка снята, "
			"повторных проб в этой сессии не будет (нет прав оператора или нет связи с сервером)"), Age);
	}
}

// ───────────────────────────── GM-меню ─────────────────────────────
void UAsdaAdminSubsystem::OpenMenuWindow(APlayerController* PC)
{
	if (!bAuthorized) { return; }
	if (!AsdaNpcGmOnly::SelfIsGm(GetGameInstance())) { return; }   // ★ 09.11.2026: без «GM_» меню не открывается ни одним путём
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// ★ То же, что у окна пароля выше: судим по живости окна, а не по ненулевому указателю.
	if (WM->IsWindowLive(MenuWidget)) { return; }
	if (MenuWidget) { CloseMenuWindow(); }

	// ★ НЕ модальное: модалка перевела бы ввод в UIOnly, и контроллер перестал бы получать F12/ESC и
	//   клавиши свободной камеры (та самая регрессия «открыл окно — умерли WASD и клик по земле»).
	//   Клики мимо окна в мир не проваливаются, потому что тело окна ниже сделано хит-тестируемым.
	MenuWidget = WM->OpenWindowByPath(FName(TEXT("ADMIN_MENU_WIN")), kAdminMenuWbp, kAdminZ,
		ESlateVisibility::SelfHitTestInvisible, /*bModal*/false);
	if (!MenuWidget)
	{
		UE_LOG(LogAsdaAdmin, Error, TEXT("[admin] не найден виджет %s"), kAdminMenuWbp);
		return;
	}

	// Тело и рамка окна ловят мышь (иначе клик по окну уходил бы в мир = «идти сюда» под окном).
	if (UWidgetTree* Tree = MenuWidget->WidgetTree)
	{
		TArray<UWidget*> All;
		Tree->GetAllWidgets(All);
		for (UWidget* W : All)
		{
			if (!W) { continue; }
			const FString N = W->GetName();
			if (W->IsA<UImage>() && (N.StartsWith(TEXT("AD_Win_0_")) || N == TEXT("Admin_menu_content")))
			{
				W->SetVisibility(ESlateVisibility::Visible);
			}
		}
	}

	// «Выбрать» (внутри группы Apply_button) — применить выделенную функцию.
	if (UButton* Ap = Cast<UButton>(MenuWidget->GetWidgetFromName(TEXT("AD_TodayBtn"))))
	{
		Ap->OnClicked.Clear();
		Ap->OnClicked.AddDynamic(this, &UAsdaAdminSubsystem::OnApplyClicked);
	}
	// «Закрыть» (группа Close_button) и крестик Admin_Close.
	for (const TCHAR* BtnName : { TEXT("AD_ClsBtn2"), TEXT("Admin_Close") })
	{
		if (UButton* B = Cast<UButton>(MenuWidget->GetWidgetFromName(FName(BtnName))))
		{
			B->OnClicked.Clear();
			B->OnClicked.AddDynamic(this, &UAsdaAdminSubsystem::OnCloseClicked);
		}
	}
	// Подписи кнопок не должны перехватывать мышь (иначе нет hover/pressed у кнопки под ними).
	for (const TCHAR* TxtName : { TEXT("AD_TodayBtn_Text"), TEXT("AD_ClsBtn2_Text"), TEXT("AD_Title_Move") })
	{
		if (UWidget* W = MenuWidget->GetWidgetFromName(FName(TxtName)))
		{
			W->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
	}
	if (UTextBlock* Title = Cast<UTextBlock>(MenuWidget->GetWidgetFromName(TEXT("AD_Title_Move"))))
	{
		Title->SetText(FText::FromString(FAsdaText::Loc(990623)));   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): «  Меню администратора»
	}

	BuildFunctionRows();   // ★ 19.10.2026: выбор строки зажимает сама (строки — пункты страницы активной вкладки)
	UE_LOG(LogAsdaAdmin, Log, TEXT("[admin] GM-меню открыто (функций: %d)"), Functions.Num());
}

void UAsdaAdminSubsystem::CloseMenuWindow()
{
	if (!MenuWidget) { return; }
	RowButtons.Reset();
	RowTexts.Reset();
	Relays.Reset();
	RowFuncIndex.Reset();   // ★ 19.10.2026: вкладки и листание
	TabButtons.Reset();
	TabTexts.Reset();
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->CloseWindow(FName(TEXT("ADMIN_MENU_WIN")));
		}
	}
	MenuWidget = nullptr;
	// Ввод возвращается миру целиком (базовый режим GameAndUI менеджер окон держит сам).
	if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
}

void UAsdaAdminSubsystem::OnCloseClicked() { CloseMenuWindow(); }
void UAsdaAdminSubsystem::OnApplyClicked() { ActivateSelected(); }

// Строки списка функций — поверх области Admin_menu_content (это Image-подложка, не панель, поэтому
//   строки кладём в ЕЁ РОДИТЕЛЬСКУЮ канву по её же слоту, как в UAsdaIconCellGrid::BuildClickOverlay).
// ★ 19.10.2026 (просьба владельца): сверху — строка ВКЛАДОК («Функции порта» / «Команды оригинала»), ниже — пункты
//   активной вкладки; не помещаются — нижняя строка листания «< Назад | N / M | Вперёд >». Всё строится кодом,
//   как и прежде строки списка: ассет окна `Admin_menu` не меняется.
void UAsdaAdminSubsystem::BuildFunctionRows()
{
	RowButtons.Reset(); RowTexts.Reset(); Relays.Reset(); RowFuncIndex.Reset();
	TabButtons.Reset(); TabTexts.Reset();
	if (!MenuWidget || !MenuWidget->WidgetTree) { return; }

	UWidget* Content = MenuWidget->GetWidgetFromName(TEXT("Admin_menu_content"));
	UCanvasPanelSlot* CS = Content ? AsdaUi::SlotOf(Content) : nullptr;
	UPanelWidget* Parent = Content ? Content->GetParent() : nullptr;
	if (!CS || !Parent)
	{
		UE_LOG(LogAsdaAdmin, Error, TEXT("[admin] в Admin_menu нет области 'Admin_menu_content' (или она не в канве)"));
		return;
	}
	const FAnchors  A     = CS->GetAnchors();
	const FVector2D Align = CS->GetAlignment();
	const FMargin   Off   = CS->GetOffsets();   // point-анкор: Right/Bottom = размер в px

	// Игровой шрифт — тот же, что у остальных подписей окна (берём с готового TextBlock, иначе грузим).
	FSlateFontInfo Font;
	if (UTextBlock* Sample = Cast<UTextBlock>(MenuWidget->GetWidgetFromName(TEXT("AD_TodayBtn_Text"))))
	{
		Font = Sample->GetFont();
	}
	else if (UFont* F = LoadObject<UFont>(nullptr, TEXT("/Game/AsdaAssets/Fonts/AsdaUIFontRT")))
	{
		Font = FSlateFontInfo(F, 12, FName(TEXT("Regular")));
	}

	const float RowH = 26.f, Pad = 10.f, Gap = 2.f;
	const float RowW = Off.Right - Pad * 2.f;
	// Кнопка-строка: normal прозрачная (или «выбрано»), hover/pressed — заливка (тот же приём, что у списка каналов
	//   логина: JuiSkin Bk_ComboBox ES0 hover FF225777 / pressed FF153549; цвета sRGB → linear).
	auto Fill = [](FLinearColor C) { FSlateBrush B; B.DrawAs = ESlateBrushDrawType::Image; B.TintColor = FSlateColor(C); return B; };
	auto MakeButton = [&](const FString& Name, float X, float Y, float W, bool bLit) -> UButton*
	{
		// ★ 19.10.2026 (ревизия С-3): имя делаем уникальным — строки пересобираются, а снятые виджеты живут до сборки мусора;
		//   `NewObject` с тем же именем и Outer подменил бы прежний объект на месте (в том числе изнутри его же клика).
		UButton* Row = MenuWidget->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),
			MakeUniqueObjectName(MenuWidget->WidgetTree, UButton::StaticClass(), FName(*Name)));
		FButtonStyle St = Row->GetStyle();
		St.Normal  = Fill(bLit ? FLinearColor(FColor(0x1B, 0x44, 0x5E)) : FLinearColor(1.f, 1.f, 1.f, 0.f));
		St.Hovered = Fill(FLinearColor(FColor(0x22, 0x57, 0x77)));
		St.Pressed = Fill(FLinearColor(FColor(0x15, 0x35, 0x49)));
		St.NormalPadding = FMargin(0); St.PressedPadding = FMargin(0);
		Row->SetStyle(St);
		if (UCanvasPanelSlot* RS = Cast<UCanvasPanelSlot>(Parent->AddChild(Row)))
		{
			RS->SetAnchors(A); RS->SetAlignment(Align);
			RS->SetOffsets(FMargin(X, Y, W, RowH));
			RS->SetZOrder(10);
		}
		return Row;
	};
	// Подпись: мышь не ловит (иначе строка не подсветится).
	auto MakeText = [&](const FString& Name, const FString& Txt, float X, float Y, float W) -> UTextBlock*
	{
		UTextBlock* T = MenuWidget->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			MakeUniqueObjectName(MenuWidget->WidgetTree, UTextBlock::StaticClass(), FName(*Name)));   // см. С-3 выше
		T->SetText(FText::FromString(Txt));
		if (Font.HasValidFont()) { T->SetFont(Font); }
		T->SetAutoWrapText(false);   // ★ autowrap в CanvasPanel = баг раскладки (закон однострочного UI)
		T->SetColorAndOpacity(FSlateColor(FLinearColor(FColor(0xE9, 0xD6, 0xBC))));   // цвет подписей JUI (sRGB→linear)
		T->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		if (UCanvasPanelSlot* TS = Cast<UCanvasPanelSlot>(Parent->AddChild(T)))
		{
			TS->SetAnchors(A); TS->SetAlignment(Align);
			TS->SetOffsets(FMargin(X + 8.f, Y + 6.f, W - 16.f, RowH));
			TS->SetZOrder(11);
		}
		return T;
	};

	// ── ★ 19.10.2026: строка вкладок — кнопки на всю ширину списка; активная подсвечена ──
	const float TabTop = Off.Top + Pad;
	const int32 NumTabs = (int32)EAsdaAdminTab::Count;
	const float TabW = (RowW - Gap * (NumTabs - 1)) / NumTabs;
	for (int32 t = 0; t < NumTabs; ++t)
	{
		const float X = Off.Left + Pad + t * (TabW + Gap);
		UButton* B = MakeButton(FString::Printf(TEXT("ADMIN_TAB%d"), t), X, TabTop, TabW, t == (int32)ActiveTab);
		AsdaUi::BindButton(MenuWidget, B, t, [this](int32 Idx) { DeferMenuAction([this, Idx]() { SetActiveTab((EAsdaAdminTab)Idx); }); }, Relays);
		TabButtons.Add(B);
		const FString Mark = (t == (int32)ActiveTab) ? FString(TEXT("> ")) : FString();
		TabTexts.Add(MakeText(FString::Printf(TEXT("ADMIN_TAB_TEXT%d"), t), Mark + TabTitle((EAsdaAdminTab)t), X, TabTop, TabW));
	}

	// ── Пункты активной вкладки: сколько помещается; не помещаются — листание ──
	const float ListTop = TabTop + RowH + Gap * 4.f;
	const float ListH = Off.Bottom - (ListTop - Off.Top) - Pad;
	const TArray<int32> All = TabFunctionIndices(ActiveTab);
	int32 Cap = FMath::Max(1, FMath::FloorToInt((ListH + Gap) / (RowH + Gap)));
	const bool bPaged = All.Num() > Cap;
	if (bPaged) { Cap = FMath::Max(1, Cap - 1); }   // нижняя строка — листание
	RowsPerPage = Cap;
	Page = FMath::Clamp(Page, 0, FMath::Max(0, GetPageCount() - 1));
	const int32 First = Page * Cap;
	const int32 Last = FMath::Min(All.Num(), First + Cap);
	for (int32 k = First; k < Last; ++k)
	{
		const int32 i = k - First;
		RowFuncIndex.Add(All[k]);
		const float RowTop = ListTop + i * (RowH + Gap);
		// ★ Клавиатурный фокус после клика по строке возвращаем вьюпорту (см. SelectRow): сфокусированная
		//   UButton съедала бы Space/Enter/стрелки, и клавиши мира «пропадали» бы. Снять фокусируемость
		//   у UButton нельзя (InitIsFocusable protected), поэтому лечим возвратом фокуса.
		UButton* Row = MakeButton(FString::Printf(TEXT("ADMIN_FUNC_ROW%d"), i), Off.Left + Pad, RowTop, RowW, false);
		AsdaUi::BindButton(MenuWidget, Row, i, [this](int32 Idx) { SelectRow(Idx); }, Relays);   // ★ U20: реле клика — общая точка
		RowButtons.Add(Row);
		RowTexts.Add(MakeText(FString::Printf(TEXT("ADMIN_FUNC_TEXT%d"), i), RowLineText(i), Off.Left + Pad, RowTop, RowW));
	}
	if (bPaged)
	{
		// Нижняя строка: «< Назад» | «N / M» | «Вперёд >».
		const float RowTop = ListTop + Cap * (RowH + Gap);
		const float BtnW = (RowW - Gap * 2.f) / 3.f;
		UButton* Prev = MakeButton(TEXT("ADMIN_PAGE_PREV"), Off.Left + Pad, RowTop, BtnW, false);
		AsdaUi::BindButton(MenuWidget, Prev, -1, [this](int32) { DeferMenuAction([this]() { SetPage(Page - 1); }); }, Relays);
		TabButtons.Add(Prev);
		TabTexts.Add(MakeText(TEXT("ADMIN_PAGE_PREV_TEXT"), FString(TEXT("< ")) + FAsdaText::Loc(990702), Off.Left + Pad, RowTop, BtnW));
		TabTexts.Add(MakeText(TEXT("ADMIN_PAGE_NUM_TEXT"), FString::Printf(TEXT("%d / %d"), Page + 1, GetPageCount()),
			Off.Left + Pad + BtnW + Gap, RowTop, BtnW));
		UButton* Next = MakeButton(TEXT("ADMIN_PAGE_NEXT"), Off.Left + Pad + (BtnW + Gap) * 2.f, RowTop, BtnW, false);
		AsdaUi::BindButton(MenuWidget, Next, 1, [this](int32) { DeferMenuAction([this]() { SetPage(Page + 1); }); }, Relays);
		TabButtons.Add(Next);
		TabTexts.Add(MakeText(TEXT("ADMIN_PAGE_NEXT_TEXT"), FAsdaText::Loc(990703) + FString(TEXT(" >")),
			Off.Left + Pad + (BtnW + Gap) * 2.f, RowTop, BtnW));
	}
	SelectedRow = FMath::Clamp(SelectedRow, 0, FMath::Max(RowFuncIndex.Num() - 1, 0));
	RefreshRowSelection();
}

TArray<int32> UAsdaAdminSubsystem::TabFunctionIndices(EAsdaAdminTab Tab) const
{
	TArray<int32> Out;
	for (int32 i = 0; i < Functions.Num(); ++i) { if (Functions[i].Tab == (uint8)Tab) { Out.Add(i); } }
	return Out;
}

TArray<FName> UAsdaAdminSubsystem::GetTabFunctionIds(EAsdaAdminTab Tab) const
{
	TArray<FName> Out;
	for (const int32 i : TabFunctionIndices(Tab)) { Out.Add(Functions[i].Id); }
	return Out;
}

int32 UAsdaAdminSubsystem::GetPageCount() const
{
	const int32 N = TabFunctionIndices(ActiveTab).Num();
	return (RowsPerPage > 0) ? FMath::Max(1, FMath::DivideAndRoundUp(N, RowsPerPage)) : 1;
}

void UAsdaAdminSubsystem::SetPage(int32 NewPage)
{
	const int32 Clamped = FMath::Clamp(NewPage, 0, FMath::Max(0, GetPageCount() - 1));
	if (Clamped == Page && MenuWidget) { return; }
	Page = Clamped;
	SelectedRow = 0;
	if (MenuWidget) { RebuildRows(); }
}

void UAsdaAdminSubsystem::SetActiveTab(EAsdaAdminTab Tab)
{
	if ((uint8)Tab >= (uint8)EAsdaAdminTab::Count) { return; }
	if (Tab == ActiveTab && MenuWidget) { return; }
	ActiveTab = Tab;
	Page = 0;
	SelectedRow = 0;
	UE_LOG(LogAsdaAdmin, Log, TEXT("[admin] вкладка GM-меню: %s"), *TabTitle(Tab));
	if (MenuWidget) { RebuildRows(); }
	if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
}

void UAsdaAdminSubsystem::RebuildRows()
{
	// Снять прежние строки, вкладки и кнопки листания с канвы, затем собрать заново.
	for (UButton* B : RowButtons) { if (B) { B->RemoveFromParent(); } }
	for (UTextBlock* T : RowTexts) { if (T) { T->RemoveFromParent(); } }
	for (UButton* B : TabButtons) { if (B) { B->RemoveFromParent(); } }
	for (UTextBlock* T : TabTexts) { if (T) { T->RemoveFromParent(); } }
	LastClickedRow = -1;   // ★ ревизия М-12: номер строки страницы после пересборки значит другое — двойной клик не склеивать
	BuildFunctionRows();
}

void UAsdaAdminSubsystem::DeferMenuAction(TFunction<void()> Action)
{
	// ★ 21.10.2026: очередь + один тикер (раньше — тикер на каждое действие). Самотест разбирает очередь `DebugRunDeferredMenuActions`,
	//   а не прокачкой тикера ядра: самотесты пакета сами запускаются из тикера, и повторный вход в него — неопределённое поведение.
	PendingMenuActions.Add(MoveTemp(Action));
	if (!DeferTickHandle.IsValid())
	{
		DeferTickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float)
		{
			DeferTickHandle.Reset();
			RunDeferredMenuActions();
			return false;   // один раз
		}), 0.f);
	}
}

void UAsdaAdminSubsystem::RunDeferredMenuActions()
{
	TArray<TFunction<void()>> Batch = MoveTemp(PendingMenuActions);
	PendingMenuActions.Reset();
	for (TFunction<void()>& A : Batch) { if (A) { A(); } }
}

FString UAsdaAdminSubsystem::TabTitle(EAsdaAdminTab Tab)
{
	return FAsdaText::Loc(Tab == EAsdaAdminTab::Original ? 990701 : 990700);   // «Команды оригинала» / «Функции порта»
}

void UAsdaAdminSubsystem::SetFunctionTab(FName Id, EAsdaAdminTab Tab)
{
	for (FAsdaAdminFunction& F : Functions) { if (F.Id == Id) { F.Tab = (uint8)Tab; return; } }
}

// Подпись строки: «Название — подсказка». У пунктов-переключателей название пересчитывается
//   каждый раз — иначе строка врала бы о состоянии до следующего открытия меню.
FString UAsdaAdminSubsystem::RowLineText(int32 Row) const
{
	if (!RowFuncIndex.IsValidIndex(Row) || !Functions.IsValidIndex(RowFuncIndex[Row])) { return FString(); }
	const FAsdaAdminFunction& F = Functions[RowFuncIndex[Row]];
	const FString Name = F.DynamicTitle ? F.DynamicTitle().ToString() : F.Title.ToString();
	return F.Hint.IsEmpty() ? Name : FString::Printf(TEXT("%s   —   %s"), *Name, *F.Hint.ToString());
}

// Пересобрать только ТЕКСТЫ (виджеты не трогаем). Зовётся из `PollHotkeys`, когда состояние
//   пункта-переключателя изменилось при ОТКРЫТОМ меню.
void UAsdaAdminSubsystem::RefreshRowTexts()
{
	for (int32 i = 0; i < RowTexts.Num(); ++i)
	{
		if (RowTexts[i]) { RowTexts[i]->SetText(FText::FromString(RowLineText(i))); }
	}
}

void UAsdaAdminSubsystem::RefreshRowSelection()
{
	for (int32 i = 0; i < RowButtons.Num(); ++i)
	{
		if (!RowButtons[i]) { continue; }
		FButtonStyle St = RowButtons[i]->GetStyle();
		FSlateBrush B; B.DrawAs = ESlateBrushDrawType::Image;
		B.TintColor = FSlateColor(i == SelectedRow ? FLinearColor(FColor(0x1B, 0x44, 0x5E))
		                                           : FLinearColor(1.f, 1.f, 1.f, 0.f));
		St.Normal = B;
		RowButtons[i]->SetStyle(St);
	}
}

void UAsdaAdminSubsystem::SelectRow(int32 Index)
{
	if (!RowFuncIndex.IsValidIndex(Index)) { return; }   // ★ 19.10.2026: строка страницы, не индекс реестра
	const double Now = FPlatformTime::Seconds();
	const bool bDouble = (Index == LastClickedRow) && (Now - LastRowClickTime < 0.35);
	LastClickedRow = Index; LastRowClickTime = Now;

	SelectedRow = Index;
	RefreshRowSelection();
	// ★ Фокус клавиатуры возвращаем вьюпорту: иначе после клика по UI мир перестаёт получать клавиши
	//   (та же природа, что и у регрессии «открыл окно — умерли WASD»).
	if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
	if (bDouble) { ActivateSelected(); }   // ★ двойной клик по строке = «Выбрать» (UButton двойных не шлёт)
}

void UAsdaAdminSubsystem::ActivateSelected()
{
	if (!RowFuncIndex.IsValidIndex(SelectedRow) || !Functions.IsValidIndex(RowFuncIndex[SelectedRow])) { return; }
	if (!bAuthorized) { return; }   // ★ античит А2 (мера 7): функции меню — только с правами от сервера
	APlayerController* PC = nullptr;
	if (UGameInstance* GI = GetGameInstance()) { PC = GI->GetFirstLocalPlayerController(GI->GetWorld()); }
	const FAsdaAdminFunction F = Functions[RowFuncIndex[SelectedRow]];   // копия: закрытие меню чистит строки
	UE_LOG(LogAsdaAdmin, Log, TEXT("[admin] применяем функцию '%s'"), *F.Id.ToString());
	CloseMenuWindow();               // меню уходит — функция работает в мире
	if (F.Activate) { F.Activate(PC); }
}

// ───────────────────────────── Свободная камера ─────────────────────────────
void UAsdaAdminSubsystem::EnterFreeCam(APlayerController* PC)
{
	// ★ Античит А2 (мера 7): свободная камера (разведка в пределах AoI) — только с правами, выданными сервером.
	if (!bAuthorized)
	{
		UE_LOG(LogAsdaAdmin, Warning, TEXT("[admin] свободная камера: нет прав оператора от сервера — отказ"));
		return;
	}
	if (!PC || FreeCam) { return; }
	UWorld* W = PC->GetWorld();
	if (!W) { return; }

	// Стартуем ровно из текущей точки обзора — переход без рывка.
	FVector  Loc = FVector::ZeroVector;
	FRotator Rot = FRotator::ZeroRotator;
	PC->GetPlayerViewPoint(Loc, Rot);

	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FreeCam = W->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Loc, Rot, SP);
	if (!FreeCam) { return; }
#if WITH_EDITOR
	FreeCam->SetActorLabel(TEXT("AsdaAdminFreeCam"));
#endif
	if (UCameraComponent* Cam = FreeCam->GetCameraComponent())
	{
		Cam->SetConstraintAspectRatio(false);   // как игровая камера — без чёрных полей
	}
	FreeCamYaw = Rot.Yaw; FreeCamPitch = Rot.Pitch;
	PrevViewTarget = PC->GetViewTarget();
	PC->SetViewTargetWithBlend(FreeCam, 0.25f);
	// ★ Сбрасываем зажатые клавиши: иначе персонаж уедет по «залипшему» W, пока мы летаем.
	PC->FlushPressedKeys();
	UE_LOG(LogAsdaAdmin, Log, TEXT("[admin] свободная камера ВКЛ (ESC — вернуть камеру на игрока)"));
}

void UAsdaAdminSubsystem::ExitFreeCam()
{
	if (!FreeCam) { return; }
	if (UGameInstance* GI = GetGameInstance())
	{
		if (APlayerController* PC = GI->GetFirstLocalPlayerController(GI->GetWorld()))
		{
			AActor* Back = PrevViewTarget.IsValid() ? PrevViewTarget.Get() : Cast<AActor>(PC->GetPawn());
			if (Back) { PC->SetViewTargetWithBlend(Back, 0.25f); }
			PC->FlushPressedKeys();   // клавиши полёта не должны «дожать» персонажа при возврате
		}
	}
	FreeCam->Destroy();
	FreeCam = nullptr;
	PrevViewTarget = nullptr;
	UE_LOG(LogAsdaAdmin, Log, TEXT("[admin] свободная камера ВЫКЛ — вид вернулся на игрока"));
}

void UAsdaAdminSubsystem::TickFreeCam(APlayerController* PC, float DeltaSeconds)
{
	if (!FreeCam || !PC) { return; }

	// ── Обзор: ПКМ-драг, как орбит игровой камеры (0.01 рад/px = 0.573°/px) ──
	FAsdaOrbitCursor::ReportOrbit(PC->IsInputKeyDown(EKeys::RightMouseButton));   // ★ 07.11.2026 [ИСПРАВЛЕНИЕ]: свободная камера тоже не упирается в край
	if (PC->IsInputKeyDown(EKeys::RightMouseButton))
	{
		float DX = 0.f, DY = 0.f;
		PC->GetInputMouseDelta(DX, DY);
		// ★ 19.10.2026: «Чувствительность поворота камеры» окна настроек (строка I3) — как у игровой камеры.
		const float TurnRad = 0.01f * UAsdaGameUserSettings::GetCameraTurnScale() * FAsdaOrbitCursor::DeltaScale();
		FreeCamYaw   += DX * FMath::RadiansToDegrees(TurnRad);
		FreeCamPitch  = FMath::Clamp(FreeCamPitch + DY * FMath::RadiansToDegrees(TurnRad), -89.f, 89.f);
	}
	const FRotator Rot(FreeCamPitch, FreeCamYaw, 0.f);

	// ── Скорость: колесо меняет базовую, Shift ×4 (быстро), Ctrl ×0.25 (точная подводка) ──
	const float Wheel = PC->GetInputAnalogKeyState(EKeys::MouseWheelAxis);
	if (!FMath::IsNearlyZero(Wheel))
	{
		FreeCamSpeed = FMath::Clamp(FreeCamSpeed * (Wheel > 0.f ? 1.25f : 0.8f), 100.f, 20000.f);
	}
	float Speed = FreeCamSpeed;
	if (PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift))   { Speed *= 4.f; }
	if (PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl)) { Speed *= 0.25f; }

	// ── Полёт: WASD в плоскости взгляда, Space/E — вверх, C/Q — вниз ──
	FVector Move = FVector::ZeroVector;
	const FVector Fwd   = Rot.Vector();
	const FVector Right = FRotationMatrix(Rot).GetScaledAxis(EAxis::Y);
	if (PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::Up))    { Move += Fwd; }
	if (PC->IsInputKeyDown(EKeys::S) || PC->IsInputKeyDown(EKeys::Down))  { Move -= Fwd; }
	if (PC->IsInputKeyDown(EKeys::D) || PC->IsInputKeyDown(EKeys::Right)) { Move += Right; }
	if (PC->IsInputKeyDown(EKeys::A) || PC->IsInputKeyDown(EKeys::Left))  { Move -= Right; }
	// ★ Подъём/спуск — из конфига (cmd 87/88 «EXT FREECAM UP/UP ALT», 89/90 «DOWN/DOWN ALT»).
	//   Дефолты прежние: SPACE или E вверх, C или Q вниз. WASD/стрелки оставлены зашитыми: это
	//   служебная камера, и её горизонталь совпадает с движением персонажа (cmd 51-54).
	const AAsdaPlayerController* AsdaPC = Cast<AAsdaPlayerController>(PC);
	if (AsdaPC && (AsdaPC->IsCmdKeyDown(87) || AsdaPC->IsCmdKeyDown(88))) { Move += FVector::UpVector; }
	if (AsdaPC && (AsdaPC->IsCmdKeyDown(89) || AsdaPC->IsCmdKeyDown(90))) { Move -= FVector::UpVector; }

	if (!Move.IsNearlyZero()) { Move = Move.GetSafeNormal() * Speed * DeltaSeconds; }
	FreeCam->SetActorLocationAndRotation(FreeCam->GetActorLocation() + Move, Rot);
}

#if ASDA_DEV_CONSOLE
// ═══ ★ САМОТЕСТ ГЕЙТА GM-МЕНЮ (античит А2, мера 7; 12.10.2026; ★ ac0r — проба 6036): `asda.Admin.SelfTestOperatorGate [quit]` ══════
//   Офлайн, без сервера и без чужих учёток: исходящие пакеты ПЕРЕХВАТЫВАЮТСЯ (в сокет не уходят), ответ сервера 6037 (и 6006)
//   подаётся ШТАТНЫМ обработчиком (`DebugFeedPacket` → `HandlePacket`), права проверяются боевыми `RequestAdminMode`/`TickAuthProbe`.
//   Критерий «оператор» — только индекс учётки (ключ имени на время теста 0), индекс подменяется и возвращается.
//   ac0r: проба — 6036 (тело 8 б: только токен), 6005 при запросе меню не уходит вовсе; неотвеченная проба одна на сессию.
static FAutoConsoleCommandWithWorldAndArgs GAsdaAdminSelfTestOperatorGate(
	TEXT("asda.Admin.SelfTestOperatorGate"),
	TEXT("Самотест: GM-меню и свободная камера открываются только по ответу сервера (6037 на пробу 6036 / 6006 «принято»), офлайн"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		static const TCHAR* kCmd = TEXT("asda.Admin.SelfTestOperatorGate");
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		static const uint16 kOwnOps[] = { 6036, 6005 };   // свои опкоды теста (проба прав и контроль «6005 не уходит»), перехватываются
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA, 0.f, kOwnOps)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaAdminSubsystem* Admin = GI ? GI->GetSubsystem<UAsdaAdminSubsystem>() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		if (!Admin || !Net || !PC) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем или контроллера")); return; }

		int32 Ok = 0, Fail = 0;   // счёт проверок
		auto Check = [&Ok, &Fail](bool bPass, const TCHAR* What)
		{
			if (bPass) { ++Ok;   UE_LOG(LogAsdaAdmin, Log,   TEXT("[самотест А2] ok   %s"), What); }
			else       { ++Fail; UE_LOG(LogAsdaAdmin, Error, TEXT("[самотест А2] FAIL %s"), What); }
		};
		// Исходное состояние — вернуть в конце.
		const uint32 IdxWas = Net->GetUserIndexForDiagnostics();
		const bool bHiddenWas = Net->IsGmHidden();
		const bool bConfirmedWas = Net->IsOperatorConfirmedByServer();
		FAsdaCVarScope Keys(W, kCmd);
		Keys.Set(TEXT("asda.Gm.OperatorByCharName"), 0);   // критерий — только индекс: имя своего персонажа не влияет
		Net->ClearOperatorConfirmation();
		Admin->ResetSession();
		UAsdaWorldSubsystem* WSn = W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		const FString NameWas = WSn ? WSn->GetSelfCharName() : FString();
		if (WSn) { WSn->SetSelfCharNameForSelfTest(TEXT("GM_SelfTest")); }   // ★ 09.11.2026: шаги A–F — персонаж «GM_» (гейт имени)

		TArray<TArray<uint8>> Probes;   // перехваченные пробы 6036 (полезная нагрузка без токена — пустая)
		int32 Hide6005 = 0;             // перехваченные 6005 (при запросе меню их быть не должно)
		const uint32 Gen = AsdaSelfTestKit::ArmCapture(Net, [&Probes, &Hide6005](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			if (Op == 6036) { Probes.Emplace(P, Len); }
			if (Op == 6005) { ++Hide6005; }
		}, 0, kCmd);
		if (Gen != 0)
		{
			const double Timeout = 60.0;   // заведомо больше `asda.Gm.HideAckTimeoutSec` — «состарить» ждущую пробу

			// A. Обычная учётка: пробы нет, прав нет, камера закрыта.
			Net->SetUserIndexForSelfTest(75181);
			Admin->RequestAdminMode(PC);
			Check(Probes.Num() == 0 && Hide6005 == 0 && !Admin->IsAuthPending() && !Admin->IsAuthorized() && !Admin->IsMenuOpen(),
				TEXT("A1 обычная учётка: проба не ушла, прав нет, меню закрыто"));
			Admin->EnterFreeCam(PC);
			Check(!Admin->IsFreeCamActive(), TEXT("A2 без прав от сервера свободная камера не включается"));

			// B. Расчёт «оператор» (индекс 2), сервер молчит (как под P47 у не-оператора с префиксом GM_/ADM_).
			Net->SetUserIndexForSelfTest(2);
			const bool bHiddenBefore = Net->IsGmHidden();
			Admin->RequestAdminMode(PC);
			Check(Probes.Num() == 1 && Probes[0].Num() == 0 && Hide6005 == 0
				&& Admin->IsAuthPending() && !Admin->IsAuthorized() && !Admin->IsMenuOpen() && !Net->CanSendGmRightsProbe(),
				TEXT("B1 расчёт «оператор»: ушла ОДНА проба 6036 (только токен), 6005 не ушёл, прав до ответа нет"));
			Admin->RequestAdminMode(PC);
			Check(Probes.Num() == 1, TEXT("B2 повторное нажатие, пока проба ждёт ответа, — новой пробы нет"));
			Admin->AgeAuthProbeForSelfTest(Timeout);
			Admin->TickAuthProbe(PC);
			Check(!Admin->IsAuthPending() && !Admin->IsAuthorized() && !Admin->IsMenuOpen() && !Net->IsOperatorConfirmedByServer(),
				TEXT("B3 без ответа — по таймауту заявка снята, прав нет, меню закрыто"));
			for (int32 i = 0; i < 10; ++i) { Admin->RequestAdminMode(PC); Admin->TickAuthProbe(PC); }
			Check(Probes.Num() == 1 && Hide6005 == 0 && !Admin->IsAuthPending(),
				TEXT("B4 ещё 10 нажатий после таймаута — проб больше нет (одна неотвеченная проба на сессию)"));

			// C. Новая сессия (полный сброс): проба снова допустима; ответ 6037 — права, меню, камера; невидимость не тронута.
			Net->ClearOperatorConfirmation();
			Admin->ResetSession();
			Admin->RequestAdminMode(PC);
			Check(Probes.Num() == 2 && Admin->IsAuthPending(), TEXT("C1 новая сессия — одна новая проба 6036"));
			Net->DebugFeedPacket(6037, TArray<uint8>());   // тело ответа обработчик не читает — важен сам факт ответа
			Admin->TickAuthProbe(PC);
			Check(Admin->IsAuthorized() && Net->IsOperatorConfirmedByServer() && Admin->IsMenuOpen() && Net->CanSendGmRightsProbe()
				&& Net->IsGmHidden() == bHiddenBefore && Hide6005 == 0,
				TEXT("C2 ответ 6037: права, меню открыто, невидимость не изменилась, 6005 не уходил"));
			Admin->EnterFreeCam(PC);
			const bool bCamOn = Admin->IsFreeCamActive();
			Admin->ExitFreeCam();
			Check(bCamOn && !Admin->IsFreeCamActive(), TEXT("C3 с правами свободная камера включается и выключается"));

			// D. Признак сервера уже есть (сброшено только меню): новая проба не нужна.
			Admin->ResetSession();
			Admin->RequestAdminMode(PC);
			Check(Probes.Num() == 2 && Admin->IsAuthorized() && Admin->IsMenuOpen(),
				TEXT("D1 признак сервера уже есть: меню без новой пробы"));

			// E. Признак снят (как при полном сбросе сессии): права снова только через сервер.
			Admin->ResetSession();
			Net->ClearOperatorConfirmation();
			Net->SetUserIndexForSelfTest(75181);
			Admin->RequestAdminMode(PC);
			Admin->EnterFreeCam(PC);
			Check(!Admin->IsAuthorized() && !Admin->IsMenuOpen() && !Admin->IsFreeCamActive() && Probes.Num() == 2,
				TEXT("E1 признак снят: прав нет, камера закрыта"));

			// F. 6006 «принято» (ответ на пункт «Скрыться») тоже даёт признак: меню без пробы.
			Net->SetUserIndexForSelfTest(2);
			const uint8 HiddenByte = Net->IsGmHidden() ? 1 : 0;
			Net->DebugFeedPacket(6006, TArray<uint8>({ 1, HiddenByte }));
			Admin->RequestAdminMode(PC);
			Check(Admin->IsAuthorized() && Admin->IsMenuOpen() && Probes.Num() == 2,
				TEXT("F1 6006 «принято» в этой сессии — меню без пробы 6036"));
			// G. ★ 09.11.2026 (владелец): имя без «GM_» — ни пробы, ни меню, даже с признаком сервера и индексом оператора.
			Admin->ResetSession();
			if (WSn) { WSn->SetSelfCharNameForSelfTest(TEXT("SelfTester")); }
			const int32 ProbesG = Probes.Num();
			Admin->RequestAdminMode(PC);
			Admin->OpenMenuWindow(PC);
			Check(WSn && !AsdaNpcGmOnly::SelfIsGm(W->GetGameInstance()) && !Admin->IsMenuOpen() && Probes.Num() == ProbesG && Hide6005 == 0,
				TEXT("G1 имя без «GM_» (индекс 2, признак сервера есть): GM-меню не открывается, проба не уходит"));
			Net->ClearOperatorConfirmation();
			Admin->ResetSession();
			Admin->RequestAdminMode(PC);
			Check(Probes.Num() == ProbesG && !Admin->IsAuthPending() && !Admin->IsMenuOpen(),
				TEXT("G2 имя без «GM_», признака нет: запрос прав не шлёт пробу 6036"));
			AsdaSelfTestKit::DisarmCapture(Net, Gen);
		}
		else
		{
			++Fail;   // перехват занят — отказ каркаса уже в логе
		}
		if (WSn) { WSn->SetSelfCharNameForSelfTest(NameWas); }
		// Возврат состояния.
		Admin->ResetSession();
		Net->SetUserIndexForSelfTest(IdxWas);
		Net->ClearOperatorConfirmation();
		if (bConfirmedWas) { Net->DebugFeedPacket(6037, TArray<uint8>()); }   // признак был — вернуть тем же путём
		if (Net->IsGmHidden() != bHiddenWas) { Net->ForceGmHiddenForDiagnostics(bHiddenWas); }
		Keys.Restore();
		AsdaSelfTestKit::Finish(W, kCmd, KA.bQuit, LogAsdaAdmin, TEXT("[самотест А2] гейт GM-меню"), Ok, Fail);
	}));

// ═══ ★ САМОТЕСТ ПУНКТА «ЗАБЛОКИРОВАТЬ УЧЁТКУ ЦЕЛИ» (античит П6, 12.10.2026): `asda.Admin.SelfTestGmBlock6094 [quit]` ═══════
//   Офлайн (`-AsdaNoNet`), без сервера: исходящие 6094 ПЕРЕХВАТЫВАЮТСЯ (в сокет не уходят, счётчик секрета не тратится),
//   права даются штатным путём (ответ 6037 через `DebugFeedPacket` → `RequestAdminMode`), цель — фейковый сосед штатным
//   путём снапшота (`DebugSpawnFakePeer`), выбирается `SetTarget` контроллера, ввод часов — в поле NUMPAD_NUMBOX настоящего
//   окна 1410, подтверждение — `AnswerMsgBox` настоящего окна 1520. Проверяется боевой путь целиком
//   (закон diagnostic-must-use-real-path), тело пакета — побайтно против раскладки эталона.
static FAutoConsoleCommandWithWorldAndArgs GAsdaAdminSelfTestGmBlock6094(
	TEXT("asda.Admin.SelfTestGmBlock6094"),
	TEXT("Самотест: пункт GM-меню «Заблокировать учётку цели» — отказы без цели/на себе, нумпад → подтверждение → тело 6094, офлайн"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		static const TCHAR* kCmd = TEXT("asda.Admin.SelfTestGmBlock6094");
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		static const uint16 kOwnOps[] = { 6094 };   // свой опкод теста — перехватывается
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA, 0.f, kOwnOps)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaAdminSubsystem*  Admin = GI ? GI->GetSubsystem<UAsdaAdminSubsystem>() : nullptr;
		UAsdaNetSubsystem*    Net   = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaWorldSubsystem*  WS    = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM    = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		AAsdaPlayerController* PC   = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
		if (!Admin || !Net || !WS || !WM || !PC) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем или контроллера")); return; }

		int32 Ok = 0, Fail = 0;
		auto Check = [&Ok, &Fail](bool bPass, const FString& What)
		{
			if (bPass) { ++Ok;   UE_LOG(LogAsdaAdmin, Log,   TEXT("[самотест П6] ok   %s"), *What); }
			else       { ++Fail; UE_LOG(LogAsdaAdmin, Error, TEXT("[самотест П6] FAIL %s"), *What); }
		};
		// Исходное состояние — вернуть в конце (свой userNum — RAII-снимок каркаса; права и индекс учётки — вручную, как в А2).
		FAsdaNetSelfScope SelfScope(Net, kCmd);
		const uint32 IdxWas = Net->GetUserIndexForDiagnostics();
		UAsdaWorldSubsystem* WSb = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;   // ★ 09.11.2026: имя «GM_» на время самотеста
		const FString NameWasB = WSb ? WSb->GetSelfCharName() : FString();
		const bool bConfirmedWas = Net->IsOperatorConfirmedByServer();
		FAsdaCVarScope Keys(W, kCmd);
		Keys.Set(TEXT("asda.Gm.OperatorByCharName"), 0);
		Keys.Set(TEXT("asda.Gm.BlockMaxHours"), 8760);
		constexpr uint16 kMe = 17;        // свой userNum на время теста
		constexpr int32  kPeer = 23;      // userNum фейкового соседа
		constexpr int32  kHours = 24;     // вводимый срок
		Net->SetSelfUserNumForDebug(kMe);

		TArray<TArray<uint8>> Sent;   // перехваченные тела 6094 (payload без конверта)
		const uint32 Gen = AsdaSelfTestKit::ArmCapture(Net, [&Sent](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			if (Op == 6094) { Sent.Emplace(P, Len); }
		}, 0, kCmd);
		if (Gen != 0)
		{
			// Тело эталона: [0..1] свой userNum, [2..3] userNum цели, [4..7] i32 часы (LE).
			auto BodyIs = [](const TArray<uint8>& B, uint16 Me, uint16 Tgt, int32 Hours)
			{
				return B.Num() == 8 && AsdaBytes::RdU16(B.GetData()) == Me && AsdaBytes::RdU16(B.GetData() + 2) == Tgt
					&& AsdaBytes::RdI32(B.GetData() + 4) == Hours;
			};

			// A. Отправитель: раскладка и отказы без отправки.
			Check(Net->SendGmBlock6094(kPeer, kHours) && Sent.Num() == 1 && BodyIs(Sent[0], kMe, (uint16)kPeer, kHours),
				FString::Printf(TEXT("A1 SendGmBlock6094(%d, %d): тело 8 б {свой %d, цель %d, часы %d}"), kPeer, kHours, (int32)kMe, kPeer, kHours));
			Check(!Net->SendGmBlock6094(kPeer, 0) && !Net->SendGmBlock6094(kPeer, -1) && Sent.Num() == 1,
				TEXT("A2 срок ≤ 0 — не отправлен"));
			Check(!Net->SendGmBlock6094(kMe, kHours) && Sent.Num() == 1, TEXT("A3 цель = я сам — не отправлен"));
			Check(!Net->SendGmBlock6094(-1, kHours) && !Net->SendGmBlock6094(0x226, kHours) && Sent.Num() == 1,
				TEXT("A4 userNum цели вне 0…549 (sub_5328F0) — не отправлен"));

			// B. Пункт в реестре и гейт прав: без прав от сервера обработчик не зовётся.
			Check(Admin->HasFunction(TEXT("GM_BLOCK")), TEXT("B1 пункт GM_BLOCK зарегистрирован"));
			Net->ClearOperatorConfirmation();
			Admin->ResetSession();
			Check(!Admin->ActivateFunctionById(TEXT("GM_BLOCK"), PC) && !WM->IsNumPadOpen() && Sent.Num() == 1,
				TEXT("B2 без прав: пункт не применяется, окно ввода не открыто"));

			// Права — штатно: 6037 (ответ на пробу) → признак сервера → меню без пробы.
			if (WSb) { WSb->SetSelfCharNameForSelfTest(TEXT("GM_SelfTest")); }   // ★ 09.11.2026: права и меню — только персонажу «GM_»
			Net->SetUserIndexForSelfTest(2);
			Net->DebugFeedPacket(6037, TArray<uint8>());
			Admin->RequestAdminMode(PC);
			Admin->CloseMenuWindow();
			Check(Admin->IsAuthorized(), TEXT("B3 права по ответу 6037 получены (подготовка)"));

			// C. Без цели / цель — я сам: отказ до окна ввода, пакета нет.
			PC->ClearTarget();
			Admin->ActivateFunctionById(TEXT("GM_BLOCK"), PC);
			Check(!WM->IsNumPadOpen() && Sent.Num() == 1, TEXT("C1 цель не выбрана — окно ввода не открыто, пакета нет"));
			if (APawn* Me = PC->GetPawn())
			{
				PC->SetTarget(Me);
				Admin->ActivateFunctionById(TEXT("GM_BLOCK"), PC);
				Check(!WM->IsNumPadOpen() && Sent.Num() == 1, TEXT("C2 цель — собственный персонаж — отказ, пакета нет"));
				PC->ClearTarget();
			}
			else { UE_LOG(LogAsdaAdmin, Warning, TEXT("[самотест П6] C2 пропущен: своего павна нет (карта без персонажа)")); }

			// D. Фейковый сосед → цель → нумпад → часы → подтверждение «ОК» → тело 6094.
			WS->DebugSpawnFakePeer(kPeer, TEXT("ЦельБлока"), FVector2D(25700.f, 25700.f));
			AActor* PeerActor = WS->ActorByUserIdx(kPeer);
			Check(PeerActor != nullptr && WS->FindUserIdxByActor(PeerActor) == kPeer, TEXT("D1 подготовка: сосед в реестре, актор → userNum"));
			if (PeerActor)
			{
				auto TypeHours = [WM](int32 Hours) -> bool
				{
					UUserWidget* Dlg = WM->FindWindow(TEXT("NUMPAD_WIN"));
					UEditableText* E = Dlg ? Cast<UEditableText>(Dlg->GetWidgetFromName(TEXT("NUMPAD_NUMBOX"))) : nullptr;
					if (!E) { return false; }
					E->SetText(FText::FromString(FString::FromInt(Hours)));
					return true;
				};
				PC->SetTarget(PeerActor);
				Admin->ActivateFunctionById(TEXT("GM_BLOCK"), PC);
				Check(WM->IsNumPadOpen() && Sent.Num() == 1, TEXT("D2 цель-игрок: открыто окно ввода срока, пакета ещё нет"));
				bool bTyped = TypeHours(kHours);
				WM->CloseNumPad(true);
				const bool bConfirmOpen = WM->IsOpen(TEXT("TWOBUT_MSGBOX"));
				Check(bTyped && !WM->IsNumPadOpen() && bConfirmOpen && Sent.Num() == 1,
					TEXT("D3 ОК нумпада с 24 ч: окно ввода закрыто, открыто подтверждение 1520, пакета ещё нет"));
				WM->AnswerMsgBox(/*bTwoBut*/true, /*bYes*/true);
				Check(!WM->IsOpen(TEXT("TWOBUT_MSGBOX")) && Sent.Num() == 2 && BodyIs(Sent.Last(), kMe, (uint16)kPeer, kHours),
					FString::Printf(TEXT("D4 «ОК» подтверждения: ушёл ОДИН 6094 {свой %d, цель %d, часы %d}"), (int32)kMe, kPeer, kHours));

				// E. Отмена в подтверждении и отмена в нумпаде — пакета нет.
				Admin->ActivateFunctionById(TEXT("GM_BLOCK"), PC);
				bTyped = TypeHours(kHours);
				WM->CloseNumPad(true);
				WM->AnswerMsgBox(true, false);
				Check(bTyped && !WM->IsOpen(TEXT("TWOBUT_MSGBOX")) && Sent.Num() == 2, TEXT("E1 «Отмена» в подтверждении — пакета нет"));
				Admin->ActivateFunctionById(TEXT("GM_BLOCK"), PC);
				WM->CloseNumPad(false);
				Check(!WM->IsNumPadOpen() && !WM->IsOpen(TEXT("TWOBUT_MSGBOX")) && Sent.Num() == 2,
					TEXT("E2 отмена в окне ввода — подтверждения и пакета нет"));
				// E3. Срок вне диапазона окна (0) — эталонная проверка 1..max: операция не запускается.
				Admin->ActivateFunctionById(TEXT("GM_BLOCK"), PC);
				bTyped = TypeHours(0);
				WM->CloseNumPad(true);
				Check(bTyped && !WM->IsOpen(TEXT("TWOBUT_MSGBOX")) && Sent.Num() == 2, TEXT("E3 введён 0 ч — окно отменяет операцию, пакета нет"));

				PC->ClearTarget();
			}
			// Уход соседа — штатным 4011 (как в самотесте короны), чтобы реестр мира остался чистым.
			{
				TArray<uint8> B; B.SetNumZeroed(2);
				AsdaBytes::WrI16(B.GetData(), (int16)kPeer);
				Net->DebugFeedPacket(4011, B);
			}
			if (WM->IsNumPadOpen()) { WM->DismissNumPad(kCmd); }
			if (WM->IsOpen(TEXT("TWOBUT_MSGBOX"))) { WM->AnswerMsgBox(true, false); }
			AsdaSelfTestKit::DisarmCapture(Net, Gen);
		}
		else
		{
			++Fail;   // перехват занят — отказ каркаса уже в логе
		}
		if (WSb) { WSb->SetSelfCharNameForSelfTest(NameWasB); }
		// Возврат состояния (свой userNum вернёт `SelfScope`).
		Admin->ResetSession();
		Net->SetUserIndexForSelfTest(IdxWas);
		Net->ClearOperatorConfirmation();
		if (bConfirmedWas) { Net->DebugFeedPacket(6037, TArray<uint8>()); }
		Keys.Restore();
		AsdaSelfTestKit::Finish(W, kCmd, KA.bQuit, LogAsdaAdmin, TEXT("[самотест П6] GM-блок учётки 6094"), Ok, Fail);
	}));
#endif // ASDA_DEV_CONSOLE
