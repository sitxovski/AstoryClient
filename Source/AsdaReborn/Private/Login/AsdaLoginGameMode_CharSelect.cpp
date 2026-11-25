// Copyright (c) 2026 @sitxovski (Discord)
// AsdaLoginGameMode_CharSelect.cpp — ★ R3-c 12.11.2026 (разрез AsdaLoginGameMode.cpp, дословный перенос): выбор персонажа: режим входа, SendEnterWorld, окно выбора, сведения о персонаже.
//   Общее для частей — в AsdaLoginGameMode_Private.h; шапка и реверс — в AsdaLoginGameMode.cpp.
#include "Login/AsdaLoginGameMode.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaUiHelpers.h"   // ★ Аудит №4 (B1): AsdaUi::SlotOf/SlotRect — общая точка слота канваса
#include "Resources/AsdaMaterialNames.h"   // ★ дедуп №2 В1-C (F10/F16): пути мастеров, имена параметров, экранные ассеты
#include "Resources/AsdaSoundSubsystem.h"
#include "Resources/AsdaModelAnimSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Login/AsdaButtonRelay.h"
#include "Login/AsdaLoginData.h"
#include "Login/AsdaText.h"          // FAsdaText::Strip (срез JUI-разметки)
#include "UI/AsdaCommunityFriendSubsystem.h"   // AsdaCommunityUi::ShownVisibilityFor — общая точка видимости узла
// ★ Аудит №4 (A8): `Login/AsdaGameText.h` больше не нужен — строка помощника теперь
//   `FAsdaLoginhelpertextRow` из `Items/AsdaItemDbRows.h` (тянется через AsdaItemTableSubsystem.h).
#include "Engine/DataTable.h"
#include "Blueprint/WidgetTree.h"    // GetAllWidgets — поиск план-виджетов по префиксу (имена с idx-суффиксом)
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/WidgetTree.h"
#include "Components/EditableText.h"
#include "Components/RichTextBlock.h"   // [САМОТЕСТ] 08.10.2026: чтение пузыря ANI_TXT в asda.Login.CreateNameTest
#include "AsdaOutgoingGate.h"           // [САМОТЕСТ] 08.10.2026: офлайн-гейт asda.Login.CreateNameTest
// ── Ctrl+N-пикер разрешения/качества (dev) ──
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "AsdaNetSubsystem.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"                 // FScreenshotRequest (движковый захват вместо RDP CopyFromScreen)
#include "GameFramework/PlayerController.h"
#include "Engine/StaticMesh.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaHudSubsystem.h"            // CharexpCumExp — int64-порог опыта (общая точка с HUD)
#include "UI/AsdaIconCellGrid.h"
#include "AsdaEffectActor.h"
#include "World/AsdaPlayerCharacter.h" // ★ общая точка kAsdaCharPreviewBaseM/F (база пути превью-скелета)
#include "PipelineStateCache.h"             // ★ charsel_preload: IsPSOPrecachingEnabled — прогрев PSO только где он есть
#include "AsdaLoginGameModeInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaLoginGameMode*.cpp (LogAsdaLogin, чистые хелперы)
#include "AsdaLoginGameMode_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaLoginGameMode*.cpp

// ★★★ [ПОРТ] МОДАЛКА ВЫБОРА РЕЖИМА ВХОДА (решение владельца 03.09.2026, вечер).
//   ИСТОРИЯ. Утром 03.09 режим «душа» оживили по эталонной схеме: клик по призраку `+946128` ставил
//   режим слота 1, «Играть» слал 1016 режим 1. Владелец по скриншоту: «пустой персонаж друга (силуэт)
//   выбираешь — игра начинает загрузку. Бред!» — фигура рядом с персонажем читается как ЗАГЛУШКА МЕСТА
//   ДРУГА, а не как переключатель режима. Итог: «Давай проще — когда выбран наш персонаж и нажимаем
//   войти — модалка с двумя выборами: Обычный / Душа (без взаимодействия с игроками)».
//   ЧТО ДЕЛАЕТ ОРИГИНАЛ: режим 1 выбирается кликом по объекту `+946128` (`login_charselect.c:734-750`),
//   «Играть» при нём показывает подпись 33 и НЕ шлёт 1016 (`entity_char.c:51785-51790`). Окна выбора
//   режима у него нет. ЧТО ДЕЛАЕМ МЫ: клик по фигурам режим 1 больше НЕ ставит (клик по заглушке друга —
//   однокнопочная модалка loc 990136, см. `OnScenePickClicked`); «Играть» при режиме слота 0 открывает
//   двухкнопочное MESSAGE2_WIN — СУЩЕСТВУЮЩАЯ система подтверждений (`ShowConfirm`, та же, что удаление
//   персонажа; закон `check-existing-before-creating`). Тексты — ТОЛЬКО из DT_GameText (наш диапазон
//   990132..990135). НА СЕРВЕР не влияет: тело 1016 в обоих режимах прежнее, меняется только байт режима.
//   КЛЮЧ ОТКАТА: `asda.Login.AskEnterMode 0` — «Играть» сразу шлёт обычный вход, как до 03.09.
static ASDA_DEV_CVAR(int32, CVarAsdaAskEnterMode,
	TEXT("asda.Login.AskEnterMode"), 1,
	TEXT("«Играть» своим персонажем: 1 = спросить режим входа модалкой (Обычный / Душа; по умолчанию, ")
	TEXT("решение владельца 03.09.2026), 0 = сразу обычный вход (1016 режим 0), как раньше."),
	ECVF_Default);

// ★★★ [ОРИГИНАЛ] 02.11.2026 (волна jl_presm): подсказка Минопио при наведении на «Играть» — по режиму слота, как в эталоне
//   `CCharSelectScene_onUiEvent@0xF06910` (ui_core.c:93590-93620, контрол 11 = CARASTART_BUT00, события 0xB/9):
//   режим слота (+768772) 1 «душа» → запись 33; 2 «персонаж соулмейта» → 34 «твой Друг уже в игре…», если взведён флаг
//   «соулмейт в сети» (+3075039: 1025 код 3 либо отказ 1027 код 2), иначе 26; режим 0 (свой) → 26. Пустой слот (+3075033 → 31)
//   у нас — отдельная кнопка CARASTART_BUT04 со своей подсказкой 31. До этой правки порт показывал 26 всегда, и после отказа
//   «соулмейт в игре» подсказка противоречила нотису. КЛЮЧ ОТКАТА: `asda.Login.PlayHelperByMode 0` — всегда 26, как раньше.
static ASDA_DEV_CVAR(int32, CVarAsdaPlayHelperByMode,
	TEXT("asda.Login.PlayHelperByMode"), 1,
	TEXT("Подсказка при наведении на «Играть»: 1 = по режиму слота и статусу соулмейта (эталон: 33/34/26), 0 = всегда 26 (как до 02.11.2026)."),
	ECVF_Default);

int32 AAsdaLoginGameMode::PlayHelperRec() const
{
	if (CVarAsdaPlayHelperByMode.GetValueOnGameThread() == 0) { return 26; }
	const int32 Mode = (SelectedSlot >= 0 && SelectedSlot < 3) ? SlotMode[SelectedSlot] : 0;
	if (Mode == 1) { return 33; }
	if (Mode == 2)
	{
		const UGameInstance* GI = GetGameInstance();
		const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		return (Net && Net->bSoulmateOnline) ? 34 : 26;
	}
	return 26;
}

// ★ 18.10.2026 (решение владельца): режим входа «Душа» отключён — в модалке выбора режима кнопка неактивна и притушена.
//   1 — вернуть кнопку в рабочий вид (вход душой 1016 режим 1, как с 03.09.2026).
static ASDA_DEV_CVAR(int32, CVarAsdaSoulModeEnabled,
	TEXT("asda.Login.SoulModeEnabled"), 0,
	TEXT("Кнопка «Душа» в модалке режима входа: 0 = отключена и притушена (по умолчанию, решение владельца 18.10.2026), ")
	TEXT("1 = рабочая (вход душой, 1016 режим 1)."),
	ECVF_Default);

void AAsdaLoginGameMode::SendEnterWorld(int32 Mode)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	// СЫРОЙ slotId (10/11/12) из записи, НЕ индекс 0..2 (эталон charDealMsg b5=rec[1076]). В режиме 1
	//   тело 1016 заполняется СВОИМ слотом ровно как в режиме 0 (эталонная ветка `else`
	//   `charDealSendReq`, до которой режим 1 у оригинала не доходил) — только байт режима = 1.
	const uint8 RawSlot = Net->CharList.IsValidIndex(SelectedSlot) ? Net->CharList[SelectedSlot].RawSlotId : (uint8)(SelectedSlot + 10);
	if (Mode == 1)
	{
		// ★★★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА — ПОМЕЧЕНО (решение владельца 03.09.2026 «режим „душа“ оживляем»,
		//   закон `fix-original-defects`). ЧТО ДЕЛАЕТ ОРИГИНАЛ: `charDealSendReq@0xF088E0`
		//   (`entity_char.c:51785-51790`) при `dealMode == 1` показывает подпись помощника 33 («Хм. Не
		//   думаю, что ты сможешь войти в игру призраком. Подожди минуту.») и `return` ДО `CNetwork_Send`.
		//   ЧТО ДЕЛАЕМ МЫ: зовём `SendSelectChar(Mode=1)`; гейт живёт в сетевой подсистеме за ключом
		//   `asda.Soul.AllowEnter` (0 = как оригинал: пакет не шлётся, возврат false) — здесь проверку не
		//   дублируем, а на false показываем ту же подпись 33 (1:1 с заглушкой эталона). Сервер режим 1
		//   принимает без партнёрской логики (`UPCharSoulState @State=1`, `LoginManager.c:4200`) —
		//   доказано в `soul_state_1_system.md` §2.1.
		if (!Net->SendSelectChar(RawSlot, (uint8)Net->SelectedServerIndex, SelectedChannel, /*Mode=*/1))
		{
			ApplyHelper(33);
			UE_LOG(LogAsdaLogin, Log, TEXT("Играть слот %d: «душа» — 1016 режим 1 НЕ отправлен (asda.Soul.AllowEnter 0 / нет связи), подпись 33"), SelectedSlot);
			return;
		}
		UE_LOG(LogAsdaLogin, Log, TEXT("Играть слот %d → ДУША (призрак) своим персонажем rawSlot=%u, 1016 режим 1"), SelectedSlot, (uint32)RawSlot);
		return;
	}
	UE_LOG(LogAsdaLogin, Log, TEXT("Играть слот %d → enter-world (1016)"), SelectedSlot);
	Net->SendSelectChar(RawSlot, (uint8)Net->SelectedServerIndex, SelectedChannel, /*Mode=*/0);
}

void AAsdaLoginGameMode::ShowEnterModeChoice()
{
	UAsdaWindowSubsystem* WinMgr = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WinMgr) { SendEnterWorld(0); return; }   // без оконной подсистемы спросить нечем — обычный вход
	// Тело окна: вопрос (990132) + пояснение второй строкой (990135). Пояснение НЕ на кнопке — в
	//   MESSAGE2 подпись кнопки 72×23 px (`ui_snapshot_before.json`: MESSAGE2_BUT00_Text), длинный
	//   русский текст туда не влезет (закон `ru-text-wider-than-field`); поле текста 230×60 с
	//   автопереносом вмещает три строки. Таблица не загрузилась → тот же текст литералом (как RU-фолбэк
	//   у `BuildLoginErrorText`), чтобы игрок не получил пустое окно.
	FString Body = FAsdaText::Loc(990132);
	if (Body.IsEmpty()) { Body = TEXT("[#990132]"); UE_LOG(LogAsdaLogin, Warning, TEXT("DT_GameText: нет строки 990132 — нужен переимпорт (Tools/import_gametext.py)")); }
	FString Hint = FAsdaText::Loc(990135);
	if (Hint.IsEmpty()) { Hint = TEXT("[#990135]"); }   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): фолбэки без RU-текста
	Body += TEXT("\n") + Hint;

	EnterChoiceSoulPendingFrame = 0;
	// Yes (BUT00, loc 990133 «Обычный») → обычный вход сразу из колбэка.
	// No  (BUT01, loc 990134 «Душа») → колбэк получает `false`, но `false` приходит и при СНОСЕ диалога
	//   другим `ShowConfirm`/`ShowGameConfirm` (`AsdaWindowSubsystem.cpp:2127/2279`; при сбросе сессии
	//   колбэк обнуляется ДО закрытия и не зовётся). Поэтому в колбэке только запоминаем кадр, а
	//   отправку 1016 режим 1 делает наше реле на самой кнопке BUT01 — оно стоит в списке делегатов
	//   ПОСЛЕ реле подсистемы (то первым зовёт CloseConfirm → колбэк) и срабатывает в том же кадре
	//   ТОЛЬКО при настоящем клике. Снос диалога без клика → реле не зовётся → ничего не отправляем.
	UUserWidget* Dlg = WinMgr->ShowConfirm(Body, [this](bool bNormal)
	{
		if (bNormal) { EnterChoiceSoulPendingFrame = 0; SendEnterWorld(0); return; }
		EnterChoiceSoulPendingFrame = GFrameCounter;
	// Вид — нейтральный (WBP_MESSAGE2_WIN_NEUTRAL): «Душа» не должна читаться как «Отмена» (решение владельца 03.09.2026).
	}, /*TimeoutSec*/0.f, /*bTimeoutConfirms*/false, /*YesLoc*/990133, /*NoLoc*/990134, /*bGateYesOnGauge*/false, /*bNeutral*/true);
	if (!Dlg) { return; }   // ShowConfirm сам позвал колбэк(false) без кадра-ожидания → входа нет, это правильно
	UButton* SoulBtn = Cast<UButton>(Dlg->GetWidgetFromName(TEXT("MESSAGE2_BUT01")));
	if (SoulBtn && CVarAsdaSoulModeEnabled.GetValueOnGameThread() == 0)
	{
		// ★ 18.10.2026 (решение владельца): режим «Душа» отключён — кнопка неактивна и притушена (0.55, как гейт «Да»
		//   того же окна: disabled-кадра в JUI-атласе нет). Реле входа душой не вешаем; «Обычный» работает как прежде.
		SoulBtn->SetIsEnabled(false);
		SoulBtn->SetRenderOpacity(0.55f);
		UE_LOG(LogAsdaLogin, Log, TEXT("Играть слот %d: модалка режима — «Душа» отключена (asda.Login.SoulModeEnabled 0)"), SelectedSlot);
		return;
	}
	if (SoulBtn)
	{
		// `ShowConfirm` делает `OnClicked.Clear()` при каждом показе, поэтому реле привязываем заново
		//   каждый раз; объект реле один (член класса), чтобы не плодить по объекту на каждый «Играть».
		if (!EnterChoiceSoulRelay)
		{
			EnterChoiceSoulRelay = NewObject<UAsdaButtonRelay>(this);
			EnterChoiceSoulRelay->Index = 1;
			EnterChoiceSoulRelay->Callback = [this](int32)
			{
				if (EnterChoiceSoulPendingFrame == 0 || EnterChoiceSoulPendingFrame != GFrameCounter) { return; }
				EnterChoiceSoulPendingFrame = 0;
				SendEnterWorld(1);
			};
		}
		SoulBtn->OnClicked.AddUniqueDynamic(EnterChoiceSoulRelay.Get(), &UAsdaButtonRelay::OnClicked);
	}
	UE_LOG(LogAsdaLogin, Log, TEXT("Играть слот %d: модалка выбора режима (Обычный / Душа), ключ asda.Login.AskEnterMode"), SelectedSlot);
}

void AAsdaLoginGameMode::ShowCharSelectUI()
{
	if (!GetWorld()) return;
	UAsdaWindowSubsystem* WinMgr = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WinMgr) return;
	CurrentScreen = EAsdaScreen::CharSelect;

	// ★ Робот-помощник обратно (скрыт на перелёте камеры, OnCharListReady) + Z=20 ПОВЕРХ градиента char-select
	//   (робот открыт в login-сцене с Z=3 → градиент его перекрывал).
	WinMgr->SetWindowVisible(FName(TEXT("WBP_ANI_WIN")), true);
	WinMgr->SetWindowZOrder(FName(TEXT("WBP_ANI_WIN")), 20);

	// Уже открыто — показать окно.
	if (CharStartWidget)
	{
		WinMgr->SetWindowVisible(FName(TEXT("CHARSELECT_WIN")), true);
		// ⚠ Ловец кликов по сцене — ИМЕННО `Visible`: `SelfHitTestInvisible` мышь не ловит
		//   (закон `selfhittest-vs-hittest-invisible`), и выбор персонажа соулмейта перестал бы работать.
		WinMgr->SetWindowVisible(FName(TEXT("CHARSELECT_PICK")), true, ESlateVisibility::Visible);
		FocusUI(CharStartWidget);
		UpdateCharSelectInfo();
		return;
	}

	// ★ ЕДИНЫЙ монолит WBP_CharSelect (нижняя полоса CARASTART_* + правая панель статов CARASTATE_* + слот-номер) —
	//   заменяет per-window WBP_CARASTATE_WIN/WBP_CARASTART_WIN. Все контролы в ОДНОМ дереве под теми же именами
	//   (CARASTART_BUT*, CARASTATE_TXT*), поэтому хендлеры ниже находят их по имени без изменений.
	//   Причина: генерик per-window строил CARASTATE из JUI (autowrap-баг + сырые JUI-цвета), а монолит выверен 1:1
	//   под эталон (золото Уровень/Профессия/Знак-зодиака, right-justify значений, размер). Оба указателя → на него.
	UUserWidget* CharSel = WinMgr->OpenWindowByPath(FName(TEXT("CHARSELECT_WIN")),
		kCharSelectWidgetPath, 15, ESlateVisibility::SelfHitTestInvisible);
	CharStateWidget = CharSel;
	CharStartWidget = CharSel;
	if (!CharStartWidget) { UE_LOG(LogAsdaLogin, Warning, TEXT("WBP_CharSelect не загружен")); return; }

	// Биндинг кнопок на панели CARASTART.
	auto BindBtn = [&](const FString& N, TFunction<void(int32)> Cb)
	{
		if (UButton* B = Cast<UButton>(CharStartWidget->GetWidgetFromName(FName(*N))))
		{
			AsdaUi::BindButton(CharStartWidget, B, 0, MoveTemp(Cb), ButtonRelays);   // ★ U20: реле клика — общая точка
		}
	};

	// Играть → enter-world (1016 Send_SelectChar). ★★★ РЕЖИМ входа берётся из режима слота — ровно
	//   как в эталонном `charDealSendReq@0xF088E0`: `LOBYTE(charDealMsg[0]) = this[curSlot+768772]`.
	//     0 — свой персонаж. ★ 03.09.2026 (решение владельца): при ключе `asda.Login.AskEnterMode 1`
	//         сначала МОДАЛКА выбора «Обычный / Душа» (`ShowEnterModeChoice`), и только её кнопка шлёт
	//         1016 (режим 0 или 1 через `SendEnterWorld`). Клик по фигурам сцены режим 1 больше не ставит.
	//     1 — «ДУША» (призрак): вход СВОИМ персонажем призраком-наблюдателем. Тело 1016 — то же,
	//         что в режиме 0 (свой аккаунт, свой сырой Charidx), только байт режима = 1. Отступление от
	//         эталона (у него «Играть» в режиме 1 заглушено подписью 33) помечено в `SendEnterWorld`.
	//         Режим слота 1 сегодня никем не ставится — ветка оставлена ради общей точки отправки.
	//     2 — ОДЕРЖИМОСТЬ: вход персонажем соулмейта. В пакет уходит ЕГО Charidx, а аккаунт владельца
	//         подставляет сама сетевая подсистема из записи 1024 (эталон берёт `subRecord[1052]`).
	BindBtn(TEXT("CARASTART_BUT00"), [this](int32)
	{
		const int32 Mode = (SelectedSlot >= 0 && SelectedSlot < 3) ? SlotMode[SelectedSlot] : 0;
		UGameInstance* GI = GetGameInstance();
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!Net) { return; }
		if (Mode == 1)
		{
			SendEnterWorld(1);
			return;
		}
		if (Mode == 2)
		{
			// Вход ОДЕРЖИМЫМ: сырой Charidx чужого персонажа (10/11/12) из набора соулмейта.
			const uint8 RawSlot = SlotPartnerCharidx[SelectedSlot];
			const FAsdaSoulmateSharedChar* Rec = FindPartnerRecord(RawSlot);
			// ★★★ ГЕЙТ ВХОДА ПО ГАЛКЕ SHARE (требование владельца 02.09.2026: «показывать модель друга
			//   в любом случае, а пускать играть за него — только если разрешил галкой»).
			//   ФАКТ ПО СЕРВЕРУ: признак «разрешил» на клиенте — это САМО НАЛИЧИЕ записи 1024
			//   (`OwnerUserIndex` ≠ 0): LoginManager шлёт 1024/1023 только для персонажей соулмейта с
			//   `CharInfo.Share = 1` (`UPPreSoulEqitem`/`UPPreSoulMateChar`: `WHERE ... Share = 1`,
			//   гейт ветки `else if (ShareCount)` в `Send_GetCharInfo`, `LoginManager.c:3011`). Отдельного
			//   байта Share в записях 1023/1024 НЕТ, а сам сервер при входе режимом 2 галку НЕ проверяет
			//   (`UPCharSoulState` смотрит только на наличие пары) — значит, гейт ОБЯЗАН стоять здесь.
			//   Сегодня модель без 1024 не строится вовсе (данных нет), поэтому ветка отказа — страховка
			//   на будущую серверную правку «данные независимо от Share» (описана в отчёте как [ПРОТОКОЛ]).
			//   Подсказка отказа — запись 33 помощника (эталон даёт её на вход «призраком», то есть на
			//   объект, за который играть нельзя; своей строки не выдумываем — ОТСТУПЛЕНИЕ помечено).
			if (!Rec || Rec->OwnerUserIndex == 0)
			{
				ApplyHelper(33);
				UE_LOG(LogAsdaLogin, Warning,
					TEXT("Играть слот %d: персонаж соулмейта Charidx=%u %s — вход режимом 2 НЕ отправлен"),
					SelectedSlot, (uint32)RawSlot,
					Rec ? TEXT("без записи 1024 (галка share не стоит)") : TEXT("без записи 1023"));
				return;
			}
			UE_LOG(LogAsdaLogin, Log, TEXT("Играть слот %d → ОДЕРЖИМОСТЬ персонажем '%s' (Charidx=%u, аккаунт %d, share=да), 1016 режим 2"),
				SelectedSlot, *Rec->CharName, (uint32)RawSlot, Rec->OwnerUserIndex);
			Net->SendSelectChar(RawSlot, (uint8)Net->SelectedServerIndex, SelectedChannel, /*Mode=*/2);
			return;
		}
		// Режим слота 0 — свой персонаж: спросить режим входа модалкой (решение владельца 03.09.2026)
		//   либо, при `asda.Login.AskEnterMode 0`, сразу обычный вход как раньше.
		if (CVarAsdaAskEnterMode.GetValueOnGameThread() != 0) { ShowEnterModeChoice(); return; }
		SendEnterWorld(0);
	});
	// Add (пустой слот) → экран СОЗДАНИЯ персонажа (эталон charDealSendReq create-флаг → Game_SetState(6)).
	BindBtn(TEXT("CARASTART_BUT04"), [this](int32) { ShowCharCreateUI(SelectedSlot); });
	// Delete (занятый слот, эталон BUT01 id14) → диалог подтверждения удаления.
	BindBtn(TEXT("CARASTART_BUT01"), [this](int32) { ShowDeleteConfirm(); });
	// Назад → АНИМАЦИЯ выхода (эталон 1028-путь, флаг 3075044): UI прячется сразу → шкатулка ЗАКРЫВАЕТСЯ →
	//   обратный перелёт камеры (реверс LS_LoginFlight) → и только потом server/channel-view (этап КАНАЛА, A2).
	//   Чистка 3D-сцены (DIV-5) выполняется по концу close-фазы в TickCasketSequence.
	BindBtn(TEXT("CARASTART_BUT02"), [this](int32) { StartBackSequence(); });
	// Выход.
	BindBtn(TEXT("CARASTART_BUT03"), [this](int32)
	{
		if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
			PC->ConsoleCommand(TEXT("quit"));
	});
	// ★ Стрелки поворота слота = РОДНАЯ анимация карусели-шкатулки (крутится подставка+ручка+шестерёнки),
	//   НЕ поворот всего актора. Эталон charSelect_PlaySlotAnim mode 2/3: lturn=трек(8-curSlot)/rturn=трек(curSlot+9).
	//   curSlot = текущий SelectedSlot; активный слот сменится ПОСЛЕ клипа (uiCyclePageThreeState → FinishCasketTurn).
	BindBtn(TEXT("CARASTART_LBUT"), [this](int32){ StartCasketTurn(false); });   // влево
	BindBtn(TEXT("CARASTART_RBUT"), [this](int32){ StartCasketTurn(true);  });   // вправо

	// ★ Подсказки помощника char-select (HELPER_HINT_CASES_RE.md §2.3). Наведение на кнопку → запись; уход → дефолт
	//   экрана (§2.1: есть персонажи → rec18 «каким играть?», нет → rec17 «нет персонажа, создай»). Delete: наведение
	//   → rec27 «будет удалён, осторожно», сам диалог подтверждения → rec28 (в ShowDeleteConfirm).
	int32 DefRec = 17;
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		for (const FAsdaCharRecord& C : Net->CharList) { if (!C.IsEmpty()) { DefRec = 18; break; } }
	}
	HelperFocusFields.Reset();          // на char-select полей ввода нет — только hover кнопок
	CurrentHelperDefault = DefRec;      // уход наведения → дефолт экрана (18 «каким играть?» / 17 «нет персонажа»)
	// Войти в мир: подсказка зависит от режима слота и статуса соулмейта — выбирается В МОМЕНТ наведения (см. PlayHelperRec).
	if (UButton* PlayBtn = Cast<UButton>(CharStartWidget->GetWidgetFromName(TEXT("CARASTART_BUT00"))))
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(CharStartWidget);   // реле наведения «Играть» (как в BindHelperHover)
		R->HoverCallback   = [this]()       { ApplyHelper(PlayHelperRec()); };
		R->UnhoverCallback = [this, DefRec]() { ApplyHelper(DefRec); };
		PlayBtn->OnHovered.AddDynamic(R, &UAsdaButtonRelay::OnHovered);
		PlayBtn->OnUnhovered.AddDynamic(R, &UAsdaButtonRelay::OnUnhovered);
		AsdaUi::AnchorRelay(ButtonRelays, R, PlayBtn);
	}
	BindHelperHover(CharStartWidget, TEXT("CARASTART_BUT04"), 31, DefRec);   // создать ещё персонажа
	BindHelperHover(CharStartWidget, TEXT("CARASTART_BUT01"), 27, DefRec);   // удалить (наведение)
	BindHelperHover(CharStartWidget, TEXT("CARASTART_BUT02"), 29, DefRec);   // к меню входа
	BindHelperHover(CharStartWidget, TEXT("CARASTART_BUT03"), 30, DefRec);   // выход из игры

	FocusUI(CharStartWidget);
	UpdateCharSelectInfo();
	ApplyHelper(DefRec);   // §2.1 вход в экран выбора персонажа
}

void AAsdaLoginGameMode::UpdateCharSelectInfo()
{
	if (!CharStartWidget) return;
	// Статы — на панели CARASTATE (CharStateWidget); кнопки — на панели CARASTART (CharStartWidget).
	auto SetTxt = [&](const TCHAR* N, const FString& S)
	{
		if (CharStateWidget)
			if (UTextBlock* T = Cast<UTextBlock>(CharStateWidget->GetWidgetFromName(FName(N))))
				T->SetText(FText::FromString(S));
	};
	TArray<FAsdaCharRecord> CharList;
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>()) { CharList = Net->CharList; }
	}
	// CharacterSlot_Num — оригинал НЕ ставит сюда «N/3» (была заглушка); оставляем виджет пустым.
	SetTxt(TEXT("CharacterSlot_Num"), TEXT(""));

	// ★ МЕТКИ статов (в JUI CaptionText пусты → рантайм из loc-строк error list.bin, эталон UI init @ui_core.c:95784).
	//   never-invent: тексты из DT_GameText по string-id (CARASTATE_LABELS_GAUGE_RE.md).
	struct FLbl { const TCHAR* W; int32 Loc; };
	static const FLbl Labels[] = {
		{ TEXT("CARASTATE_TXT02"), 11029 },  // Уровень
		{ TEXT("CARASTATE_TXT04"), 11030 },  // Профессия
		{ TEXT("CARASTATE_TXT06"), 11031 },  // ОЗ (HP)
		{ TEXT("CARASTATE_TXT08"), 11032 },  // ОМ (MP)
		{ TEXT("CARASTATE_TXT10"), 11033 },  // Сила
		{ TEXT("CARASTATE_TXT12"), 11034 },  // Ловкость
		{ TEXT("CARASTATE_TXT14"), 11035 },  // Выносливость
		{ TEXT("CARASTATE_TXT16"), 11036 },  // Энергия
		{ TEXT("CARASTATE_TXT18"), 11037 },  // Интеллект
		{ TEXT("CARASTATE_TXT20"), 11038 },  // Удача
		{ TEXT("CARASTATE_TXT22"), 11039 },  // Опыт
		{ TEXT("CARASTATE_TXT23"), 110631 }, // Знак зодиака
	};
	for (const FLbl& L : Labels) { SetTxt(L.W, FAsdaLoginData::Loc(L.Loc)); }

	// ★★★ ЧЬИ статы рисует панель — решает РЕЖИМ СЛОТА, а не «мой список персонажей».
	//   Эталон `charDealWndRefreshInfo@0xF086F0` зовёт ОДНУ И ТУ ЖЕ `CCharInfoWnd_UpdateInfo`
	//   и для своего персонажа, и для персонажа соулмейта — меняется ТОЛЬКО указатель на запись
	//   (`findSlotByType` против `playerFindSubRecordByType`). Воспроизводим ровно это: собираем
	//   нейтральный набор полей панели из одного из двух источников и дальше идём общим кодом.
	//   Второго «почти такого же» блока показа при этом не появляется (закон `port-shared-points`).
	struct FStatPanel
	{
		bool    bValid = false;
		FString Name;
		uint8   Job = 0, SubClass = 0, Level = 0, Constellation = 0;
		bool    bHasConstellation = false;   // у персонажа соулмейта созвездия НЕТ (сервер его не шлёт)
		int64   Exp = 0;
		int32   HpCur = 0, HpMax = 0, MpCur = 0, MpMax = 0;
		TArray<int32> Stats;
	};
	FStatPanel P;
	const int32 SlotModeNow = (SelectedSlot >= 0 && SelectedSlot < 3) ? SlotMode[SelectedSlot] : 0;
	const bool  bMyOccupied = CharList.IsValidIndex(SelectedSlot) && !CharList[SelectedSlot].IsEmpty();
	if (SlotModeNow == 2)
	{
		// Выбран персонаж СОУЛМЕЙТА: запись ищем по сырому Charidx, как эталонная
		//   `playerFindSubRecordByType@0xF0A210` (она тоже отсекает всё вне 10..12).
		if (const FAsdaSoulmateSharedChar* S = FindPartnerRecord(SlotPartnerCharidx[SelectedSlot]))
		{
			P.bValid = true;
			P.Name = S->CharName;  P.Job = S->Job;  P.SubClass = S->SubJob;  P.Level = S->Level;
			P.Exp = S->Exp;
			P.HpCur = S->Hp;  P.HpMax = S->MaxHp;  P.MpCur = S->Mp;  P.MpMax = S->MaxMp;
			P.Stats = S->Stats;
			// ★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА (дефект оригинала, закон `fix-original-defects`).
			//   Созвездие чужого персонажа сервер НЕ присылает: `UPPreSoulMateChar` его не выбирает,
			//   а `usp_CharConstellationSelect` в соулмейт-ветке не зовётся вовсе. Оригинал в этом
			//   случае оставляет в контроле 47 значение ОТ ПРЕДЫДУЩЕГО показа — то есть выдаёт МОЙ
			//   знак зодиака за знак чужого персонажа. Мы поле ОЧИЩАЕМ: пусто честнее вранья.
			P.bHasConstellation = false;
		}
	}
	else if (bMyOccupied)
	{
		const FAsdaCharRecord& R = CharList[SelectedSlot];
		P.bValid = true;
		P.Name = R.Name;  P.Job = R.Job;  P.SubClass = R.SubClass;  P.Level = R.Level;
		P.Exp = R.Exp;
		P.HpCur = R.HpCur;  P.HpMax = R.HpMax;  P.MpCur = R.MpCur;  P.MpMax = R.MpMax;
		P.Stats = R.Stats;
		P.Constellation = R.Constellation;  P.bHasConstellation = true;
	}

	if (P.bValid)
	{
		const FStatPanel& R = P;
		SetTxt(TEXT("CARASTATE_TXT01"), R.Name);                                        // имя
		// Уровень: префикс из loc12215 ("Ур.") + число (было хардкод "Ур.").
		{
			const FString LvPrefix = FAsdaLoginData::Loc(12215);
			SetTxt(TEXT("CARASTATE_TXT03"), FString::Printf(TEXT("%s %d"),
				LvPrefix.IsEmpty() ? TEXT("[#12215]") : *LvPrefix, R.Level));   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка
		}
		// Класс: при subClass≠0 показываем подкласс loc(subClass+12556) (эталон buildLocalizedNoticeString),
		//   иначе базовый job. Раньше subClass игнорился.
		SetTxt(TEXT("CARASTATE_TXT05"), (R.SubClass != 0)
			? FAsdaLoginData::Loc((int32)R.SubClass + 12556)
			: FAsdaCharRecord::ClassName(R.Job));
		SetTxt(TEXT("CARASTATE_TXT07"), FString::Printf(TEXT("%d / %d"), R.HpCur, R.HpMax)); // HP cur/max
		SetTxt(TEXT("CARASTATE_TXT09"), FString::Printf(TEXT("%d / %d"), R.MpCur, R.MpMax)); // MP cur/max
		// 6 статов: TXT11/13/15/17/19/21 = Сила/Ловкость/Выносливость/Энергия/Интеллект/Удача
		static const TCHAR* const StatWidgets[6] = {
			TEXT("CARASTATE_TXT11"), TEXT("CARASTATE_TXT13"), TEXT("CARASTATE_TXT15"),
			TEXT("CARASTATE_TXT17"), TEXT("CARASTATE_TXT19"), TEXT("CARASTATE_TXT21")
		};
		for (int32 s = 0; s < 6; ++s)
		{
			SetTxt(StatWidgets[s], R.Stats.IsValidIndex(s) ? FString::FromInt(R.Stats[s]) : TEXT(""));
		}
		SetTxt(TEXT("CARASTATE_TXT24"), R.bHasConstellation
			? FAsdaCharRecord::ConstellationName(R.Constellation) : FString());   // созвездие (у чужого — нет)

		// ★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА (дефект оригинала, закон `fix-original-defects`): эталонная
		//   `statBarUpdatePercent@0xF09640` ВСЕГДА берёт запись СВОЕГО персонажа
		//   (`findSlotByType(this, this[curSlot+768766])`) — в функции нет ни одной ветки на
		//   `+768769`. Из-за этого при выборе персонажа соулмейта полоса опыта показывала прогресс
		//   МОЕГО персонажа рядом с ЕГО уровнем и статами. Мы считаем полосу по той же записи,
		//   что и остальная панель (`P`), поэтому она согласована с показанными цифрами.
		// ★ EXP-бар CARASTATE_GAUGE (эталон statBarUpdatePercent@0xF09640, ui_core.c:94193-94198):
		//   pct = (Span − (LevelUpTotal − Exp)) / Span, где LevelUpTotal = порог СЛЕДУЮЩЕГО уровня = CumExp[level+1],
		//   Span = CumExp[level+1] − CumExp[level] (= ExpToNext[level+1]). Уровень держится пока CumExp[level] ≤ Exp
		//   < CumExp[level+1] (проверено: lvl3 Exp=135, CumExp 116..184 → 28%). Прежний вариант брал base=CumExp[level−1]
		//   → на живых данных pct>1 → бар «пустой» из-за клип-бага/клампа. Таблица smcharexp (DT_smcharexp, Bin.bbz).
		float ExpPct = 0.f;
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAsdaItemTableSubsystem* Items = GI->GetSubsystem<UAsdaItemTableSubsystem>())
			{
				// ★ ТАБЛИЦА ОПЫТА ПЕРСОНАЖА = charexp (150 уровней). smcharexp (30 уровней) — это таблица
				//   СОУЛМЕЙТА: с ней бар всегда упирался в 100%, т.к. пороги в разы меньше настоящих.
				if (UDataTable* DT = Items->GetDataTable(FName(TEXT("charexp"))))
				{
					// ★ Диапазон уровня L = [CumExp[L−1], CumExp[L]) — проверено на живых данных:
					//   Level=6, Exp=1025 → 994…1720 → 4.3% (совпало с оригиналом).
					//   Эталон statBarUpdatePercent@0xF09640: pct = (Span − (LevelUpTotal − Exp)) / Span,
					//   где LevelUpTotal = порог СЛЕДУЮЩЕГО уровня = CumExp[L], Span = CumExp[L] − CumExp[L−1].
					const FAsdaCharexpRow* Prev = DT->FindRow<FAsdaCharexpRow>(FName(*FString::FromInt(FMath::Max(R.Level - 1, 0))), TEXT("exp"), false);
					const FAsdaCharexpRow* Cur  = DT->FindRow<FAsdaCharexpRow>(FName(*FString::FromInt(R.Level)), TEXT("exp"), false);
					if (Prev && Cur)
					{
						// ★ Пороги — int64 через общую точку HUD: с 64-го уровня младшее слово отрицательное.
						const double Base = (double)UAsdaHudSubsystem::CharexpCumExp(*Prev);
						const double Span = (double)UAsdaHudSubsystem::CharexpCumExp(*Cur) - Base;
						if (Span > 0.0) { ExpPct = (float)FMath::Clamp(((double)R.Exp - Base) / Span, 0.0, 1.0); }
					}
				}
			}
		}
		UE_LOG(LogAsdaLogin, Log, TEXT("[exp-бар] Level=%d Exp=%lld -> ExpPct=%.3f"), R.Level, (long long)R.Exp, ExpPct);
		// ★ Заполнение полосы = КЛИП ШИРИНЫ СЛОТА заливки (fullW·pct), пивот слева — эталонный метод
		//   ([[ui-progressbar-animate-existing]]): не RenderScale (сквишит 9-slice капы бруша). Left/anchor не трогаем,
		//   уменьшаем ТОЛЬКО ширину (Offsets.Right при point-анкоре) → орандж открывается слева-направо. FullW=196 (дизайн).
		if (CharStateWidget && CharStateWidget->WidgetTree)
		{
			TArray<UWidget*> GaugeW;
			CharStateWidget->WidgetTree->GetAllWidgets(GaugeW);
			for (UWidget* W : GaugeW)
			{
				if (!W) continue;
				const FString WN = W->GetName();
				if (WN.StartsWith(TEXT("CARASTATE_GAUGE")) && !WN.StartsWith(TEXT("CARASTATE_GAUGE_BASE")))
				{
					if (UCanvasPanelSlot* GS = AsdaUi::SlotOf(W))
					{
						const FMargin Off = GS->GetOffsets();   // (Left=posX, Top=posY, Right=sizeX, Bottom=sizeY)
						GS->SetOffsets(FMargin(Off.Left, Off.Top, 198.0f * FMath::Clamp(ExpPct, 0.f, 1.f), Off.Bottom));   // fullW=198 (дизайн заливки)
					}
				}
			}
		}
	}
	else
	{
		static const TCHAR* const AllValueWidgets[] = {
			TEXT("CARASTATE_TXT01"), TEXT("CARASTATE_TXT03"), TEXT("CARASTATE_TXT05"),
			TEXT("CARASTATE_TXT07"), TEXT("CARASTATE_TXT09"), TEXT("CARASTATE_TXT11"),
			TEXT("CARASTATE_TXT13"), TEXT("CARASTATE_TXT15"), TEXT("CARASTATE_TXT17"),
			TEXT("CARASTATE_TXT19"), TEXT("CARASTATE_TXT21"), TEXT("CARASTATE_TXT24")
		};
		for (const TCHAR* W : AllValueWidgets) { SetTxt(W, TEXT("")); }
	}

	// ★ Go/Add — ОДНА кнопка-платформа (эталон charDealWndRefreshInfo): занятый слот → Go (играть),
	//   пустой → Add (создать). Платформа-подложка (BASE) видна всегда, иконки/кликер переключаются.
	//   ⚠ Считается по МОЕМУ персонажу слота даже в режиме «одержимости»: эталон гейтит всю панель
	//   проверкой `+768766` (мой Charidx), а не выбранной записью — вход чужим возможен только
	//   с моего ЗАНЯТОГО слота (`charDealSendReq` иначе показывает нотис 6516).
	const bool bOccupied = bMyOccupied;
	auto SetBtnVis = [&](const TCHAR* N, bool bShow)   // кнопки на панели CARASTART
	{
		if (UWidget* W = CharStartWidget->GetWidgetFromName(FName(N)))
			W->SetVisibility(bShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	};
	auto SetImgVis = [&](const TCHAR* N, bool bShow)
	{
		if (UWidget* W = CharStartWidget->GetWidgetFromName(FName(N)))
			W->SetVisibility(bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	};
	SetBtnVis(TEXT("CARASTART_BUT00"), bOccupied);   SetImgVis(TEXT("CARASTART_BUT00_Icon"), bOccupied);  // Go
	SetBtnVis(TEXT("CARASTART_BUT04"), !bOccupied);  SetImgVis(TEXT("CARASTART_BUT04_Icon"), !bOccupied); // Add
	SetBtnVis(TEXT("CARASTART_BUT01"), bOccupied);   // Delete — только на занятом слоте (эталон charDealWndRefreshInfo)
	SetImgVis(TEXT("CARASTART_BUT01_Text"), bOccupied);   // ★ A1: подпись «Удалить» — импортёр разбил кнопку на BUT01+BUT01_Text; в оригинале caption внутри кнопки → скрывать синхронно (иначе текст висит на пустом слоте)

	// ★ Какая из двух панелей одного прямоугольника видна — ровно как в `charDealWndRefreshInfo@0xF086F0`:
	//     мой Charidx вне 10..12 (пустой слот) → панель статов 17 СКРЫТЬ;
	//     режим 1 («душа») → 17 скрыть, показать 114 в режиме 1 (текст «здесь будет стоять…»);
	//     режим 0 и 2      → 17 ПОКАЗАТЬ (в режиме 2 в ней уже лежат статы ЧУЖОГО персонажа).
	//   Панель 114 в режимах 0/2 здесь не трогаем: её гасит сам обработчик клика (эталон делает
	//   `uiStatusPanel_ShowByMode(0,-1)` в момент смены режима).
	if (!bOccupied)
	{
		ShowCharStatePanel(false);
	}
	else if (SlotModeNow == 1)
	{
		ShowCharStatePanel(false);
		ShowSoulInfoPanel(true, 1);
	}
	else
	{
		ShowCharStatePanel(true);
	}
	// 3D КАРУСЕЛЬ: все 3 слота на шкатулке видны ОДНОВРЕМЕННО (шкатулка крутится, не прячет слоты).
	//   Занятый слот → персонаж, пустой → облако создания (глиф). Видимость ПЕР-СЛОТОВАЯ (НЕ по выбранному —
	//   иначе при выборе занятого слота глифы пустых прятались, при выборе пустого — прятались все персонажи).
	for (AActor* A : CharacterActors) { if (A) { A->SetActorHiddenInGame(false); } }   // персонажи (занятые слоты) видны всегда
	// ★ Персонажи соулмейта — тоже на своих слотах и видны всегда (эталон включает их тем же
	//   `charDealWndSetSlotEnable`, что и моих; отдельного «показать только выбранного» там нет).
	for (AActor* A : PartnerActors) { if (A) { A->SetActorHiddenInGame(false); } }
	// «Душа» — тот же объект слота (`+946128`), включается тем же `charDealWndSetSlotEnable`.
	for (AActor* A : SoulActors) { if (A) { A->SetActorHiddenInGame(false); } }
	for (int32 i = 0; i < GlyphActors.Num(); ++i)
	{
		const bool bSlotEmpty = !(CharList.IsValidIndex(i) && !CharList[i].IsEmpty());
		if (GlyphActors[i]) { GlyphActors[i]->SetActorHiddenInGame(!bSlotEmpty); }   // пустой слот → глиф виден
	}
}
