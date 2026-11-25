// Copyright (c) 2026 @sitxovski (Discord)
// AAsdaLoginGameMode — режим уровня логина (3D-сцена: фон+шкатулка+логотип + оригинальный JUI-UI).
// Читает адрес login-сервера из OPSet.bin (НЕ ввод руками), проводит логин к UAsdaNetSubsystem,
// управляет камерой (обзор шкатулки → перелёт → открытие) по стейт-машине g_GameState.
// UI (Login.JUI-раскладка + текстуры) и камера/анимация — строятся 1:1 (login_scene_flow.md, login_ui_layout.md).
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AsdaOpSet.h"
#include "UObject/ObjectKey.h"              // ★ charsel_preload: TObjectKey — доля превью во флаге полных мипов
#include "Styling/SlateTypes.h"          // FTextBlockStyle — снимок стиля контролов JUI (см. FAsdaJuiTextStyle)
#include "Framework/Text/TextLayout.h"   // ETextJustify — там же (выравнивание из блока `EXS TextAlign`)
#include "AsdaLoginGameMode.generated.h"

// Экран логин-флоу (аналог g_GameState: login/server/char-select/char-create). Единое состояние вместо
//   разрозненных булевых флагов (DIV-1). 2=login,3=server,5=char-select,6=char-create по нумерации эталона.
UENUM()
enum class EAsdaScreen : uint8
{
	None         = 0,
	Login        = 2,
	ServerSelect = 3,
	CharSelect   = 5,
	CharCreate   = 6,
};

// ★★★ СНИМОК СТИЛЯ ОДНОГО ТЕКСТОВОГО КОНТРОЛА JUI, снятый ДО первой подмены на `URichTextBlock`.
//   ЗАЧЕМ. `UAsdaRichText::ApplyFormatted` по ПРЯМОМУ контракту `AsdaMarkup.h:64-73` обязан
//   получить собственный стиль готового контрола: иначе новый rich-контрол берёт строку `Default`
//   таблицы `DT_AsdaRichStyle` (белый, 10 px, autowrap) и теряет всё, что импортёр взял из JUI —
//   кегль `FONT_PX`, цвет из блока `EXS TextColor`, выравнивание из `EXS TextAlign` и чёрную
//   обводку скина. Ровно на этом классе дефектов проект уже горел («нечитаемо, не те цвета и
//   размер»). Снимать можно ТОЛЬКО до подмены — после неё `Cast<UTextBlock>` не найдёт ничего.
//   Форма структуры — как у `FAsdaTitleRichSlot` (общая практика порта), поэтому она вне класса.
//   ★ 02.09.2026 переименована из `FAsdaSoulInfoTextStyle`: снимок нужен НЕ только окну 114, но и
//     всем остальным контролам логина, которым показывают форматированный текст (пузырь помощника
//     `ANI_TXT`, строка окна сообщения `MESSAGE_TXT01`). Имя со словом «SoulInfo» после этого врало.
struct FAsdaJuiTextStyle
{
	FTextBlockStyle    Style;                          // шрифт+кегль+обводка, цвет, тень контрола
	ETextJustify::Type Justify = ETextJustify::Left;   // из `EXS TextAlign`
	bool  bWrap  = true;                               // многострочный ли контрол (высота слота >= 28)
	float WrapAt = 0.f;                                // ширина переноса = ширина canvas-слота
	bool  bCaptured = false;                           // снимок сделан (иначе стиль не передаём)
};

UCLASS()
class ASDAREBORN_API AAsdaLoginGameMode : public AGameModeBase
{
	GENERATED_BODY()
	// ★ 11.10.2026 (дедуп №2, В2-C): самотест `asda.Dedup.W2C.Glyph` — штатный путь появления превью и глифов (F9/F11).
	friend struct FAsdaDedupW2CTestAccess;
	// ★ 02.11.2026 (волна jl_presm): самотест `asda.Login.SoulmateStatusSelfTest` — 1025 коды 3/4/0, отказ 1027 код 2, подсказка «Играть».
	friend struct FAsdaSoulmateStatusSelfTest;
	friend struct FAsdaRelogArmorTestAccess;   // самотест разрыва на выборе (AsdaRelogArmorSelfTest.cpp): фазы сцены
	// ★ 06.11.2026: самотест `asda.Login.SelfTestCreateClicks` — настоящие клики по окну создания персонажа.
	friend struct FAsdaCreateClicksAccess;
	// ★ 08.11.2026 (пачка 16, баг 10): самотест `asda.Login.SelfTestCreateDim` — затемнение за создаваемым персонажем (квад blackscreen).
	friend struct FAsdaCreateDimAccess;
public:
	AAsdaLoginGameMode();

	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ★ Адрес login-сервера — из АССЕТА /Game/AsdaAssets/Data/DT_ServerList (импорт из OPset.bin офлайн,
	//   Tools/asda_build_server_list_dt.py). Абсолютный путь к каталогу оригинальной игры УБРАН:
	//   его нет у игрока, а .bin в Content/ кукер в пак не пакует. ★ 16.10.2026: хост — всегда домен (AsdaServerAddress.h), из таблицы берётся только порт.

	// Вызывается оригинальной кнопкой Login (Log_OkBtn ID4) после ввода логина/пароля.
	void DoLogin(const FString& Login, const FString& Password);

	// Активный сервер (из OPSet.bin).
	const FAsdaServerEntry& GetServer() const { return Server; }

	// Самопроверка хит-зон сцены «одержимости» ПУТЁМ ИГРОКА (`-game`, закон ui-check-game-path-geometry):
	//   для объектов текущего слота — коробка пика (та, что бьёт луч) и хулл СКИННУТЫХ вершин (то, что
	//   видно) в мире и на экране + карта «что выберет луч» по сетке экрана против карты «что видно».
	//   Консоль: `asda.Login.PossessPickTest [задержкаСек] [quit]` (зовёт команда из .cpp — потому public).
	void RunPossessPickSelfTest(bool bQuit);

	// [САМОТЕСТ] 08.10.2026 (batch_fixes Блок 3): экран создания персонажа путём игрока — открыть создание, выбрать созвездие,
	//   вписать имя Name в поле CARAMAKE_NAMEINPUT и нажать «Создать» штатным `SubmitCharCreate`. Недопустимое имя пакета не
	//   шлёт (клиентская валидация), допустимое офлайн тоже не уйдёт (связи нет). Консоль: `asda.Login.CreateNameTest`.
	void RunCreateNameSelfTest(const FString& Name);

	// [САМОТЕСТ] 10.10.2026 (en_completeness): экран выбора персонажа БЕЗ сервера — для снимков и проверки подписей
	//   языка путём игрока. Пустой список персонажей заполняется одним условным персонажем (только офлайн; в сеть ничего
	//   не уходит), сцена логина прячется, панели выбора открываются штатным `ShowCharSelectUI`. Консоль:
	//   `asda.Login.CharSelectOffline [задержкаСек]`.
	void RunCharSelectOfflineDemo();

private:
	FAsdaServerEntry Server;

	// UI-атласы держит единый текстур-менеджер (UAsdaTextureSubsystem, engine-lifetime кэш+GC-рут).
	// Музыку логина держит единый звук-менеджер (UAsdaSoundSubsystem, единичный стрим; стоп — SoundMgr->StopMusic()).

	// ★ UMG-логин (правило ui-editable-in-ue): WBP_LOGIN_WIN, правится в UMG-дизайнере.
	UPROPERTY()
	TObjectPtr<class UUserWidget> LoginWidget;

	// ══════════ ★★★ СТАРТОВЫЙ ЛОГОТИП (задача владельца 2026-08-25) ═══════════════════════════
	//   Полноэкранная заставка `Start_logo` поверх ВСЕГО, показывается первым же кадром сцены
	//   логина. Логин строится ПОД ней, поэтому к моменту растворения он уже готов и переход
	//   получается без паузы и без чёрного кадра. Держим 3 с, затем плавно растворяем.
	//   Ключи: `asda.Login.StartLogoHoldSec` (0 = не показывать), `asda.Login.StartLogoFadeSec`.
	UPROPERTY()
	TObjectPtr<class UUserWidget> StartLogoWidget;
	UPROPERTY()
	TObjectPtr<class UImage> StartLogoImage;
	double StartLogoShownAtS = 0.0;   // момент показа (секунды мира)
	void ShowStartLogo();             // создать и показать заставку
	void TickStartLogo(float DeltaSeconds);   // выдержка → растворение → снятие
	void LayoutStartLogo();                   // заполнить экран по центру, сохранив пропорции
	// ★ 12.10.2026 (владелец): окно ПЕРВОГО ВЫБОРА ЯЗЫКА не должно появляться, пока заставка на экране, — оно
	//   показывается в момент снятия заставки (`TickStartLogo`, конец растворения). Флаг ставит `StartPlay`, когда язык
	//   не выбран и заставка показана; без заставки (ключ 0, перезагрузка уровня) окно открывается сразу, как раньше.
	bool bLangSelectAfterStartLogo = false;
	// ★ 12.10.2026 (автоподбор графики): язык уже выбран, а замер первого запуска ещё не проходил — запустить его после
	//   снятия заставки (`UAsdaBenchSubsystem::StartFirstRun`: уход на карту замера). Экран логина отложен до возврата.
	bool bBenchAfterStartLogo = false;
	// Экран логина, отложенный до выбора языка: прогрев атласов + WBP_LOGIN_WIN + музыка. Общий код для обычного
	//   старта (`StartPlay`) и запасного пути (окно выбора языка не загрузилось после заставки — без него экрана логина
	//   не было бы вовсе). Перезагрузку уровня при выборе языка делает `UAsdaLangSelectSubsystem`.
	void PresentLoginScreen();
	void ShowLoginScreenUMG();     // показать логин-экран UMG (поля ввода + кнопки)

	// ★ ПОЛНАЯ композиция login-сцены (правило ui-scene-full-composition): дефолт-видимые окна
	//   MM_LOGO(лого)/ANI_WIN/VER_TEXT/QuitBtn_Base/CharacterSlot_Num/CHATER_X (login_flow_re.md).
	//   Окна держит единый оконный менеджер (UAsdaWindowSubsystem, реестр по id + z-order).
	void AddLoginSceneOverlays();  // загрузить+показать дефолт-видимые окна сцены (кроме GRAD_BG — 3D-задник)
	void HideLoginScene();         // спрятать логин+оверлеи при уходе (char-select)
	void DoLoginFromWidget();      // клик OK/Enter → прочитать поля → DoLogin
	void SetLoginStatus(const FString& Msg);   // обратная связь входа (ретрай/ошибка) через текст помощника

	// Применить запись помощника (LoginHelperText): ANI_TXT = текст, god-robot состояние по mode (0/1/2).
	// ★ InsertName — ПОДСТАНОВКА ИМЕНИ ПЕРСОНАЖА в строку-шаблон (эталон `scene_handleLeftClickPick`
	//   и `charSelect_OnClickPick`: берут запись 21 «Ты хочешь подключиться к серверу персонажем []?»
	//   и вставляют имя СРАЗУ ПОСЛЕ первой '[' — `std::wstring::find('[')` + `insert(pos+1, name)`).
	//   Пусто (по умолчанию) — строка показывается как есть, поведение прежних вызовов не меняется.
	void ApplyHelper(int32 RecordIdx, const FString& InsertName = FString());
	// ★ 02.11.2026 [ОРИГИНАЛ] Запись помощника для наведения на «Играть» (CARASTART_BUT00) по режиму выбранного слота и статусу
	//   соулмейта: 1 → 33, 2 → 34 (соулмейт в сети) / 26, иначе 26 (эталон CCharSelectScene_onUiEvent@0xF06910; ключ asda.Login.PlayHelperByMode).
	int32 PlayHelperRec() const;
	// Показ ГОТОВОЙ строки в пузыре помощника + режим god-robot (общий хвост `ApplyHelper`).
	void ShowHelperText(const FString& Raw, int32 Mode);

	// ── Помощник «Минопио»: подсказки по фокусу/наведению (HELPER_HINT_CASES_RE.md) ──
	//   Кнопки → hover (relay OnHovered/OnUnhovered), поля ввода → клавиатурный фокус (SlateApplication).
	//   Навесить hover на кнопку виджета W с записью Rec (уход → RecLost, обычно 0).
	void BindHelperHover(class UUserWidget* W, const FString& BtnName, int32 Rec, int32 RecLost = 0);
	//   Зарегистрировать поле ввода W (по имени) как фокус-триггер записи Rec.
	void AddHelperFocusField(class UUserWidget* W, const FString& FieldName, int32 Rec);
	void RegisterHelperFocus();      // подписка на смену клавиатурного фокуса Slate
	void UnregisterHelperFocus();    // отписка (при скрытии экрана/уничтожении)
	void OnUIFocusChanging(const struct FFocusEvent& FocusEvent, const class FWeakWidgetPath& OldPath,
		const TSharedPtr<class SWidget>& OldWidget, const class FWidgetPath& NewPath, const TSharedPtr<class SWidget>& NewWidget);
	// Триггер-поля: слабый указатель на UWidget + запись помощника; RecLost при уходе фокуса со ВСЕХ (обычно 0).
	struct FHelperFocusField { TWeakObjectPtr<class UWidget> Widget; int32 Rec = 0; };
	TArray<FHelperFocusField> HelperFocusFields;
	FDelegateHandle HelperFocusHandle;
	int32 CurrentHelperDefault = 0;   // rec «дефолт экрана» (login=0, выбор=18/17, создание=35): показывается при уходе фокуса
	TWeakObjectPtr<class UWidget> CurrentFocusField;   // поле, что СЕЙЧАС в фокусе (или null): дефолт-сброс только при ЕГО потере
	// Показать/скрыть god-robot по mode: 0→ANI_TALK(sation)/1→ANI_VICTORY(praise)/2→ANI_WARNING(warning).
	void SetGodRobotMode(int32 Mode);
	// Модальное окно сообщения/ошибки MESSAGE_WIN (текст + OK). Показ через оконный менеджер (bModal).
	void ShowMessageWin(const FString& Text);
	// Текст ошибки логина по статусу ответа 1004: нотисы error list.bin (Loc 100/101/102/117/118/119).
	FString BuildLoginErrorText(uint8 Status) const;
	// Текст кика 4101 / обрыва по эталонной карте loc-нотисов (см. .cpp).
	FString BuildKickText(int32 Code, bool bClosed) const;
	// Барьер connect→1003 (login_ipnull_fix.md §4): отложенная отправка логина после СВЕЖЕГО коннекта.
	FTimerHandle LoginSendTimer;

	// ★ Char-select = ЕДИНЫЙ монолит WBP_CharSelect (id CHARSELECT_WIN в оконном менеджере): статы CARASTATE_* +
	//   кнопки CARASTART_* + слот-номер в одном дереве (выверен 1:1 под эталон). Оба члена указывают на ОДИН инстанс —
	//   разделение State/Start историческое (было per-window WBP_CARASTATE_WIN/WBP_CARASTART_WIN); поиск контролов по имени.
	UPROPERTY()
	TObjectPtr<class UUserWidget> CharStateWidget;   // = монолит (доступ к статам CARASTATE_*)
	UPROPERTY()
	TObjectPtr<class UUserWidget> CharStartWidget;   // = тот же монолит (доступ к кнопкам CARASTART_*)
	EAsdaScreen CurrentScreen = EAsdaScreen::None;   // текущий экран флоу (единое состояние, DIV-1)
	int32 SelectedSlot = 0;        // текущий выбранный слот (0..2) для Играть/поворота
	void ShowCharSelectUI();       // показать панель выбора персонажа + биндинг кнопок/инфо
	void UpdateCharSelectInfo();   // заполнить инфо-панель данными CharList[SelectedSlot]

	// ══════════ ★★★ «ОДЕРЖИМОСТЬ» (possessed): персонаж соулмейта в сцене выбора ═══════════════
	//
	// ЧТО ЭТО. Персонаж, которым соулмейт «поделился» (галка `CharInfo.Share=1`), встаёт РЯДОМ
	//   с моим на подставке шкатулки полной 3D-моделью — своя причёска/лицо/цвет и надетая
	//   экипировка. Его можно навести мышью, выбрать кликом и ВОЙТИ ИМ В ИГРУ (1016, режим 2).
	//   Данные приносит логин-сервер пакетами 1024 (экип) → 1023 (статы+внешность) → 1025 (статус);
	//   их разбирает `UAsdaNetSubsystem` и отдаёт готовым набором `GetSoulmateSharedChars()`.
	//   ★ ФАКТ ПО СЕРВЕРУ (02.09.2026, `LoginManager.c:3011` + текст процедур живой БД): 1023/1024
	//   приходят ТОЛЬКО для персонажей соулмейта с `Share = 1`; при паре без единой галки сервер
	//   шлёт один 1025 {1,3} и никаких данных о персонаже друга — строить модель не из чего, поэтому
	//   в этом случае стоит силуэт «души». Разделение «показ всегда / вход только по галке» требует
	//   серверной (SQL) правки — описано в `audit_2026-09-02/fix_soul_scene_bugs.md` как [ПРОТОКОЛ].
	//
	// ЭТАЛОН держит на КАЖДЫЙ слот ТРИ объекта `CCharacter`: мой (`+236592`), «душа»-заглушка
	//   (`+946128`) и персонаж соулмейта (`+1655664`). Здесь воспроизведены все три:
	//   мой (`CharacterActors`), соулмейт (`PartnerActors`) и «душа» (`SoulActors`).
	//   Ассеты «души» (силуэт + клипы) перенесены 02.09.2026 —
	//   `Tools/export_char_possess.py` / `Tools/import_char_possess.py`.
	//
	// ★ Три параллельных массива состояния — 1:1 с эталонными (`charSelect_OnClickPick@0xF0AF00`):
	//   `SlotPartnerCharidx` = `+768769`, `SlotMode` = `+768772`, `MyClipState` = `+768742`,
	//   `PartnerClipState` = `+768748`. Стартовые значения тоже эталонные (`AsdaStory_game.c:528327`):
	//   мой клип 0, клип партнёра 2 — то есть «выбран мой персонаж».
	UPROPERTY() TObjectPtr<class UUserWidget> SoulInfoWidget;             // окно 114 SOULINFO_WIN
	UPROPERTY() TObjectPtr<class UAsdaScenePickCatcher> ScenePick;        // ловец кликов по 3D под UI
	UPROPERTY() TArray<TObjectPtr<AActor>> PartnerActors;                 // [0..2] персонаж соулмейта в слоте
	// ★★★ ТРЕТИЙ ОБЪЕКТ СЛОТА — «ДУША» (эталонный `+946128`). Голубой силуэт, который стоит рядом с
	//   моим персонажем в слоте, КОГДА ПАРТНЁРА НЕТ. Эталон `charDealWndSetSlotEnable@0xF08560`
	//   (`entity_char.c:51653-51663`): `playerFindSubRecordByType` вернул запись → включается объект
	//   партнёра, вернул null → включается `+946128`. Именно наведение на него даёт режим 1 панели
	//   114 («здесь будет стоять персонаж твоего Друга») и подсказки помощника 22/23.
	//   ★ 03.09.2026 — решение владельца «режим „душа“ ОЖИВЛЯЕМ»: призрак показывается по замыслу
	//   мёртвого загрузчика `@0xF05420` (меш `cre_cresoul*` со своей сине-облачной текстурой, вид по
	//   эталону — `soul_silhouette_visual_re.md` §5.2/§8). Ключ `asda.Login.SoulSilhouette` по
	//   умолчанию 1 (0 = как живой оригинал, пусто). В эталоне это единственная точка входа в режим 1
	//   (клик → `SlotMode` 1 + подпись 23) — у нас с вечера 03.09 УЖЕ НЕТ, см. ниже. Вход режимом 1 —
	//   `SendEnterWorld(1)` (`SendSelectChar(Mode=1)`, гейт `asda.Soul.AllowEnter`; при отказе — подпись
	//   33). Друг показывается моделью ТОЛЬКО по share.
	//   ★ 03.09.2026 (вечер, решение владельца по скриншоту): смысл ЭТОГО объекта в порту — ЗАГЛУШКА
	//   МЕСТА ДРУГА («здесь встанет друг»). Клик по ней РЕЖИМ НЕ СТАВИТ и к 1016 не ведёт — показывает
	//   модальное окно MESSAGE_WIN с loc 990136 «Тут будет персонаж твоего друга…» (`OnScenePickClicked`).
	//   Режим 1 («душа») выбирается ТОЛЬКО модалкой на «Играть» (`ShowEnterModeChoice`, ключ
	//   `asda.Login.AskEnterMode`) — ни клик по фигурам, ни наведение режим не ставят.
	UPROPERTY() TArray<TObjectPtr<AActor>> SoulActors;                    // [0..2] заглушка места друга (справа) слота
	uint8 SlotPartnerCharidx[3] = { 0, 0, 0 };   // сырой Charidx партнёра слота (0 = партнёра нет)
	int32 SlotMode[3]           = { 0, 0, 0 };   // режим слота: 0 = мой перс, 1 = «душа», 2 = соулмейт
	int32 MyClipState[3]        = { 0, 0, 0 };   // клип моего перса: 0 idle-выбран, 1→2 уход, 2 idle-не выбран, 3→0 приход
	int32 PartnerClipState[3]   = { 2, 2, 2 };   // клип партнёра, та же семантика
	int32 SoulClipState[3]      = { 2, 2, 2 };   // клип «души» (эталонный `+768745`, старт 2), та же семантика
	float ClipOneShotLeft[3]    = { 0.f, 0.f, 0.f };   // остаток one-shot клипа слота (сек); ≤0 = не играет
	bool  bPossessHoverMyChar   = false;         // курсор сейчас над МОИМ персонажем текущего слота
	bool  bPossessHoverPartner  = false;         // курсор сейчас над персонажем соулмейта
	bool  bPossessHoverSoul     = false;         // курсор сейчас над «душой»-заглушкой

	// ★★★ АУРА силуэта «души» (glow-shell) — overlay-материал на мешах силуэтов; параметры
	//   Thickness/AuraAlpha покадрово гонит TickSoulAura (замысел эталона pbb-прохода `+0xE6FE4`,
	//   разбор `Reports/soulmate_soul_aura_render.md`, реализация — fix_soul_aura_impl.md).
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> SoulAuraMIDs[3];   // overlay-MID ауры по слоту (null = нет)
	float SoulAuraHeightScale[3] = { 1.f, 1.f, 1.f };  // высота_UE/150: перевод «толщины» из ед. модели в см (ДОПУЩЕНИЕ)
	float SoulAuraElapsedMs      = 0.f;                 // общий счётчик фазы дыхания (все силуэты в фазе — упрощение)

	void SetupPossessScene();        // раздать партнёров по слотам и заспавнить (после открытия шкатулки)
	void DestroyPossessScene();      // снести акторов партнёров и «душ», сбросить состояние слотов
	void AssignPartnersToSlots();    // сортировка по уровню ↓ + раздача по ЗАНЯТЫМ слотам (эталон assignSortedTopValuesToSlots@0xF0D000)
	// Спавн одного персонажа соулмейта на кость пьедестала слота. nullptr, если парного клипа нет
	//   (без него партнёра некуда ставить — смещение позы живёт в корне клипа, см. AsdaPossessScene.h).
	AActor* SpawnPartnerInSlot(int32 SlotIdx, const struct FAsdaSoulmateSharedChar& Rec, uint8 MySex);
	// Спавн «души»-заглушки слота (силуэт). Набор клипов и меш выбирает МОЙ пол — эталон
	//   `CEntity_LoadCreatureMorphModels_Cremmman@0xF05930`. nullptr, если ассетов нет.
	AActor* SpawnSoulInSlot(int32 SlotIdx, uint8 MySex);
	// Запись партнёра по сырому Charidx (эталон playerFindSubRecordByType@0xF0A210: 10..12, иначе nullptr).
	const struct FAsdaSoulmateSharedChar* FindPartnerRecord(uint8 Charidx) const;

	// Панель 114 (эталон uiStatusPanel_ShowByMode@0xF083F0): bVisible=false ⇒ окно скрыть (Mode не важен);
	//   Mode 1 ⇒ показать 115+116 (тип 0/1), Mode 2 ⇒ показать 117 (тип 2).
	void ShowSoulInfoPanel(bool bVisible, int32 Mode);
	// Высота рамки окна 114 ПО ФАКТИЧЕСКОМУ ТЕКСТУ показанного режима (ключ `asda.SoulInfo.AutoHeight`).
	//   Зовётся из `ShowSoulInfoPanel` ПОСЛЕ наполнения текстом — обоснование, почему это рантайм, а
	//   не правка разметки, в блок-комментарии над телом функции.
	void ApplySoulInfoFrameHeight(int32 Mode);
	// ★ Базовая (АССЕТНАЯ) раскладка окна 114 — снимок при первом показе, ДО первой растяжки.
	//   Только он даёт ТОЧНЫЙ откат по ключу 0 и фолбэк, когда текст померить нечем.
	float SoulInfoBaseFrameH = 0.f;              // высота рамки из ассета (485 px); 0 = снимок ещё не сделан
	TMap<FName, float> SoulInfoBaseBoxH;         // высоты текстовых боксов из ассета: TXT02 445, TXT00 200, TXT01 150
	// Видимость панели статов 17 CARASTATE (эталон sub_929C7A → 0xF083B0). В порту статы живут внутри
	//   монолита WBP_CharSelect, поэтому гасим группу узлов по префиксу `CARASTATE`.
	void ShowCharStatePanel(bool bVisible);

	void TickPossessScene(float DeltaSeconds);   // наведение мышью + докрутка one-shot клипов
	void TickSoulAura(float DeltaSeconds);       // дыхание ауры силуэтов (ping-pong 2000 мс, формулы §7 разбора)
	void OnScenePickClicked();                   // клик мимо UI (эталон charSelect_OnClickPick@0xF0AF00)
	// Луч мыши против AABB актора — порт эталонного `entityRayVsObbHit` (там тоже коробка объекта).
	bool RayHitsActor(const FVector& Origin, const FVector& Dir, const AActor* A) const;
	// Коробка пика актора по ТЕКУЩЕЙ позе: мировые позиции костей, на которые ссылаются веса вершин
	//   (без костей-пустышек `*_End`), + паддинг по горизонтали. false = коробки нет (нет мешей).
	//   Одна точка для луча (`RayHitsActor`) и самопроверки (`asda.Login.PossessPickTest`).
	bool ComputePickBox(const AActor* A, FBox& OutBox) const;
	void ApplyClipStates(int32 SlotIdx);         // проиграть текущие клипы моего перса, партнёра и «души» слота
	// Подсказка помощника при наведении/клике по МОЕМУ персонажу — порт эталонного
	//   `chatInput_InsertTargetName@0xF0C1B0` (имя ВРЁТ: это подсказка «Минопио», а не чат).
	//   Запись 21 с подставленным именем при `уровень >= 9 && класс != 0`, иначе запись 19.
	void ApplyHelperForMyChar(int32 SlotIdx);
	int32 SoulPanelMode = -1;                    // текущий показанный режим панели 114 (-1 = скрыта): гейт «не дёргать каждый кадр»
	// ★ Снимки стиля текстовых контролов JUI (см. `FAsdaJuiTextStyle` выше класса).
	//   Ключ — имя узла: SOULINFO_TXT00/01/02 (окно 114), ANI_TXT (пузырь помощника),
	//   MESSAGE_TXT01 (модальное окно сообщения). Имена узлов JUI на логине уникальны.
	TMap<FName, FAsdaJuiTextStyle> JuiTextStyles;
	// ★★★ ЕДИНАЯ ТОЧКА показа форматированного текста в ГОТОВОМ контроле JUI (закон
	//   `port-shared-points`). Снимает стиль контрола при ПЕРВОМ показе (пока это ещё `UTextBlock`),
	//   кладёт в `JuiTextStyles` и зовёт `UAsdaRichText::ApplyFormatted` с ним третьим аргументом —
	//   ровно как требует контракт `AsdaMarkup.h`. Возвращает актуальный узел (после первой подмены
	//   это `URichTextBlock` с тем же именем) или nullptr.
	class UWidget* ApplyRichWithJuiStyle(class UWidget* Node, const FString& Raw);
	// Отказ на вход (1027 ≠ 1): код 2 — соулмейт онлайн (нотис loc 131), код 3 — у аккаунта нет пары.
	void OnCharSelectRefused(int32 Code);
	void OnSoulmateSharedCharsUpdated();         // 1025 получен → пересобрать сцену «одержимости»

	// ★★★ [ПОРТ] ВЫБОР РЕЖИМА ВХОДА МОДАЛКОЙ (решение владельца 03.09.2026: «когда выбран наш персонаж
	//   и нажимаем войти — модалка с двумя выборами: Обычный / Душа (без взаимодействия с игроками)»).
	//   ЭТАЛОН режим 1 выбирал кликом по объекту `+946128` (`login_charselect.c:734-750`) и на «Играть»
	//   его глушил (`entity_char.c:51785-51790`). У НАС: «Играть» при режиме слота 0 → двухкнопочное окно
	//   MESSAGE2_WIN (та же система, что подтверждение удаления, `UAsdaWindowSubsystem::ShowConfirm`):
	//   текст loc 990132 + 990135, кнопки loc 990133 «Обычный» (BUT00) / 990134 «Душа» (BUT01).
	//   «Обычный» → `SendEnterWorld(0)`, «Душа» → `SendEnterWorld(1)` (1016 режим 1; при false —
	//   подпись 33 как у эталонной заглушки). Ключ `asda.Login.AskEnterMode` (0 = сразу обычный вход).
	void ShowEnterModeChoice();
	// Общая точка отправки 1016 СВОИМ слотом: Mode 0 — обычный, 1 — «душа». Режим 2 (соулмейт) сюда
	//   не ходит — у него свой Charidx и гейт share в обработчике «Играть».
	void SendEnterWorld(int32 Mode);
	// Реле кнопки «Душа» (BUT01) поверх реле оконной подсистемы: `ShowConfirm` отдаёт `false` и на
	//   клик BUT01, и на снос диалога другим `ShowConfirm` — различаем по кадру (см. `ShowEnterModeChoice`).
	UPROPERTY() TObjectPtr<class UAsdaButtonRelay> EnterChoiceSoulRelay;
	uint64 EnterChoiceSoulPendingFrame = 0;      // кадр, в котором колбэк диалога получил `false`; 0 = ничего не ждём

	// ── УДАЛЕНИЕ персонажа (BUT01 → система WinMgr->ShowConfirm + пакет 1018) ──
	void ShowDeleteConfirm();      // диалог подтверждения удаления выбранного слота (через единую систему)

	// ── СОЗДАНИЕ персонажа (state 6, CARAMAKE_WIN) — CHAR_CREATE_RE.md ──
	UPROPERTY()
	TObjectPtr<class UUserWidget> CharCreateWidget;   // WBP_CARAMAKE_WIN
	uint8 CreateGender = 1;         // 1=муж/2=жен
	uint8 CreateHair = 0;           // причёска 0..4
	uint8 CreateColor = 0;          // цвет волос 0..2
	uint8 CreateFace = 0;           // лицо (муж 0..10 / жен 0..9)
	uint8 CreateConstellation = 0;  // созвездие/знак (обязательно)
	bool bCreateInFlight = false;   // запрос создания отправлен, ждём ответ 1026 → блок повторных кликов (иначе десинк секрета→кик 4101)
	int32 CreateSlot = 0;           // слот для создания
	bool bCharCreateResultBound = false;  // подписка на 1019 сделана
	// ★★★ Реле OK-кнопки ТЕКУЩЕГО виджета MESSAGE_WIN. БЫЛО (дефект ПОРТА, найден ревизией 03.09.2026):
	//   `bool bMessageWinBound` — флаг «забиндено один раз» на весь GameMode, тогда как OK зовёт
	//   `CloseWindow(MESSAGE_WIN)`, а тот УДАЛЯЕТ запись реестра и виджет ⇒ второй `ShowMessageWin`
	//   за сессию получал от `OpenWindow` НОВЫЙ виджет с НЕПРИВЯЗАННОЙ кнопкой: Esc на логине нет,
	//   `ModalBlocker` ест клики — клиент запирался до перезапуска. Теперь бинд ставится при КАЖДОМ
	//   показе (как `ShowConfirm`), а реле живёт здесь, а не в `ButtonRelays`: при следующем показе
	//   указатель перезаписывается и старое реле уходит в GC вместе со своим виджетом.
	UPROPERTY() TObjectPtr<class UAsdaButtonRelay> MessageWinRelay;
	void ShowCharCreateUI(int32 Slot);  // открыть экран создания для пустого слота
	void HideCharCreate(bool bBackToSelect);  // закрыть экран создания (→ char-select или нет)
	void SubmitCharCreate();        // OK: собрать поля → SendCreateChar (1014)
	void OnCharCreateResult(int32 Code);  // ответ 1019: успех/ошибка
	TArray<int32> GetDefaultParts(uint8 Gender) const;  // дефолт 5 частей из CharEquipTable (slotOrder)
	void SpawnCreatePreview();      // 3D-превью создаваемого перса = ОТДЕЛЬНЫЙ объект (не слот!) перед шкатулкой
	void CycleCreateAppearance(int32 Which, int32 Delta);  // 0=причёска/1=цвет/2=лицо ±1
	// ── СЦЕНА char-create (эталон SPEC_CHARCREATE_SCENE.md): отдельный объект + затемнение + вращение + робот ──
	UPROPERTY() TObjectPtr<AActor> CreatePreviewActor;   // создаваемый персонаж — ОТДЕЛЬНЫЙ актор (не слот-карусель)
	UPROPERTY() TObjectPtr<AActor> CreateDimActor;       // квад затемнения blackscreen за создаваемым персонажем (SpawnCreatePreview; расстановка — asda.Login.CreateDimPlace)
	UPROPERTY() TObjectPtr<class UBorder> CreateDimBorder;   // UMG-затемнение char-create (полноэкранный тёмный Border поверх 3D)
	// Персонаж НЕ должен темнеть от дима: рендерим его в RT (SceneCapture только персонаж, прозрачный фон) и
	//   показываем ярким UMG-Image ПОВЕРХ дима. Захват-камера = главная камера сцены (перс совпадает 1:1).
	UPROPERTY() TObjectPtr<class UTextureRenderTarget2D> CreateCharRT;
	UPROPERTY() TObjectPtr<class USceneCaptureComponent2D> CreateCharCapture;
	UPROPERTY() TObjectPtr<class UImage> CreateCharImage;
	int32 CreateRotDir = 0;         // вращение превью: -1 влево / +1 вправо / 0 стоп (стрелки LEFTBUT/RIGHTBUT)
	float CreateYaw = 0.f;          // накопленный поворот превью вокруг Y (эталон objPitchStepBy6Deg: ±6°/кадр)
	FRotator CreateBaseRot = FRotator::ZeroRotator;   // базовая ориентация превью (yaw вращения добавляется поверх)
	void TeardownCreateScene();     // снести превью/затемнение, вернуть слоты char-select (выход из создания)

	// ── Сетка выбора частей внешности (5 категорий × варианты из CharEquipTable) ──
	// Рендер/клики ячеек — через переиспользуемую систему UAsdaIconCellGrid; здесь только МОДЕЛЬ данных
	// (карусель категорий): что за itemCode в ячейке и что делать по клику (провайдеры для контроллера).
	TArray<int32> CreatePartCodes;         // 5 ItemCode по fileSlot 0..4 (текущий выбор частей)
	TArray<TArray<int32>> CategoryVars;    // 5 векторов вариантов ItemCode по fileSlot (для пола)
	TArray<int32> CategorySelIdx;          // выбранный индекс варианта per fileSlot
	TArray<int32> CategoryScroll;          // ПЕР-СТРОКОВЫЙ scroll-индекс (эталон this[i+118345]): ±1 с враппом по длине СВОЕЙ строки
	UPROPERTY() TObjectPtr<class UAsdaIconCellGrid> IconGrid;   // контроллер сетки иконок (система #2)
	int32 CreateCellCode(int32 Cell) const;     // провайдер: ячейка (row*5+col) → itemCode по карусели
	void BuildCreateCategories(uint8 Gender);   // заполнить CategoryVars + дефолт-выбор из CharEquipTable
	void PopulateIconGrid();                    // обновить иконки сетки (делегирует IconGrid->Refresh)
	void OnIconCellClicked(int32 Cell);         // выбор варианта части по клику ячейки
	void ScrollIconGrid(int32 Delta);           // BACK/NEXT — сдвиг вариантов всех строк
	// ── SEL-маркеры «сияющая рамка» выбранного варианта (эталон uiArrangeSkillSlots5 @0xF132B0) ──
	//   5 рамок CARAMAKE_ICONBOX_SEL00..04 (по одной на строку) = UAsdaAnimatedPlane (сияние 4 кадра ×
	//   300мс крутит сам виджет, конвертер JUI); здесь только позиция над видимой ячейкой с выбранным
	//   вариантом (или скрытие, если он вне окна скролла). Позиция = слот обёртки CARAMAKE_BLANK{N}_grp
	//   (та же канва BOTWIN_grp, что и рамки; сами ICONBOX внутри обёрток с pos 0,0).
	void ArrangeCreateSelMarkers();             // позиционирование/видимость 5 рамок (1:1 эталон)
	// ── Showcase-анимация по клику одежды (эталон uiCharPartSelectCycle: one-shot трек → возврат к idle 0) ──
	//   Набор клипов CHAR-CREATE = cre_creman/crewoman-a_00_{0..6} (CCreature_LoadCreManAnims @0xF10050),
	//   импортированы как previewanim_05..11 (05=idle создания, трек N → ассет 05+N).
	//   Треки по категории: Chest→4, Gloves→3, Pants→5, Shoes→6, Helmet→2. Пока играет — клики НЕ перезапускают.
	int32 CreateAnimState = 0;                  // текущий трек 0..6 (0=idle; эталон char+118382)
	bool  bCreateShowcase = false;              // one-shot играет (эталон флаг char[473507])
	float CreateShowcaseStart = 0.f;            // WorldTime старта клипа (для продолжения позиции при пересборке превью)
	FTimerHandle CreateShowcaseTimer;           // конец клипа → возврат к idle
	void OnShowcaseAnimEnd();                   // one-shot доиграл: флаг/трек сброс + idle на превью
	// Наложить выбранные части (+причёска/лицо) на превью-скелет (create-вариант ApplyEquipToSlot).
	void ApplyCreateParts(AActor* SlotChar, class USceneComponent* Root, class USkeletalMeshComponent* BaseSkel, uint8 Sex);
	UFUNCTION()
	void OnConstellationSelected(FString SelectedItem, ESelectInfo::Type SelectInfo);  // (устар.) UE-combobox
	// ★ ИГРОВОЙ комбобокс созвездия (never-invent: скин Bk_ComboBox + список DarkGrey_List): строит тёмный бокс+стрелку
	//   (атлас n_02) и выпадающий список 12 знаков вместо дефолтного UComboBoxString. Клик по боксу → показать список.
	void BuildGameConstellationCombo();
	void OnConstellationPick(int32 Index);   // выбор знака из списка → CreateConstellation=Index+1, закрыть список
	// Подсветка выбранного пола (эталон Log_CaracterSex: выбран = ЖЁЛТЫЙ FFFFFF00, невыбран = серый FFE6E6E6):
	//   тинт иконок-планов ♂(левая)/♀(правая) по CreateGender.
	void UpdateGenderHighlight();
	UPROPERTY() TObjectPtr<class UPanelWidget> ConstellationDropdown;   // выпадающий список (toggle видимости)
	UPROPERTY() TObjectPtr<class UTextBlock> ConstellationDisplay;      // метка выбранного знака (в закрытом боксе)

	// Enter в поле пароля → логин.
	UFUNCTION()
	void OnPasswordCommitted(const FText& Text, ETextCommit::Type CommitType);

	// Тоггл чекбокса «сохранить ID».
	UFUNCTION()
	void OnSaveIdChanged(bool bChecked);
	void ToggleSaveId();   // клик по строке чекбокса: свап визуала + персист флага/ID в check.bin
	bool bSaveIdChecked = false;

	// ★ Режим XyT (подключение к чужой сборке через её локальный прокси `Launcher.exe`).
	//   Разбор — `Reports/xyt_traffic_capture.md`. Переключатель строится В РАНТАЙМЕ
	//   (ассет WBP_LOGIN_WIN не трогаем — правило «UI не перегенерировать»).
	void BuildXytToggle();     // создать чекбокс+подпись рядом с полем адреса
	void ToggleXytMode();      // клик: сменить состояние, применить профиль шифра, подставить адрес
	void ApplyXytMode();       // применить текущее состояние к шифру и полю адреса (★ 16.10.2026: XyT — только дев-сборки)
public:
	// ★ 16.10.2026: показать в поле SERVER_IP_INPUT домен сервера (только текст; вид/доступность — из ассета).
	//   Публична для самотеста asda.SelfTest.ServerAddr.
	void FillServerAddrField();
private:
	bool bXytMode = false;
	UPROPERTY(Transient) class UCheckBox* XytCheck = nullptr;

	// ★ UMG-версия выбора сервера/канала (правило ui-editable-in-ue): грузится из WBP_ServerSelect,
	//   правится в UMG-дизайнере. Заменяет Slate. Кнопки биндятся через релеи (несут индекс).
	UPROPERTY()
	TObjectPtr<class UUserWidget> ServerSelectWidget;
	UPROPERTY()
	TArray<TObjectPtr<class UAsdaButtonRelay>> ButtonRelays;

	// (Ctrl+N-пикер разрешения/качества УЕХАЛ в общий UAsdaDevMenuSubsystem — работает на всех картах.)

	int32 NumRealChannels = 1;
	// ★ Число СЛОТОВ каналов на экране (эталон m_nChannelCount @+0x3BC — константа клиента: 3 в RU-сборке,
	//   5 в мастер-IDB). Недоступные каналы показываются СЕРЫМИ, а не скрываются. Правится в редакторе.
	UPROPERTY(EditAnywhere, Category="Asda|Login")
	int32 ChannelSlotCount = 3;         // слотов каналов на экране (эталон m_nChannelCount: 3 в RU-сборке)
	int32 SelectedServer = 0;          // выбранный слот сервера (для _O/_C состояний; наш сервер = слот 0)

	void FocusUI(class UUserWidget* W);   // UIOnly + фокус на этот виджет (иначе клики уходят не туда при смене экранов)
	void ShowServerSelect();          // после логина: показать выбор сервера/канала (окно 199)
	void ShowServerOrChannelView(bool bChannels);  // переключить видимость групп SERVER_*/CHANNEL_*
	void OnServerChosen(int32 ServerIndex);   // клик по серверу → 1012 (server-select) + показать каналы
	void OnChannelClicked(int32 Channel);     // клик по каналу → ТОЛЬКО выделить (Sel-состояние), без сети
	void OnChannelSelected(int32 Channel);    // подтверждение («Войти») → 1012 + char-list (1005)
	int32 ComputeRealChannels() const;
	// Канал недоступен (счётчик 1013 < 0) — рисуется серой плашкой и не кликается.
	bool IsChannelDisabled(int32 Channel) const;        // число каналов из данных сервера (1013), не из константы
	void OnChannelOnlineUpdated();    // 1013 получен → обновить бары онлайна каналов в UI
	void OnCharListReady();           // char-list получен → вход в char-select (перелёт+открытие)
	void OnEnterWorld();              // ★ ФАЗА A4: 1021 получен → скрыть логин-сцену + загрузочный экран мира
	uint8 SelectedChannel = 0;        // выбранный канал (для enter-world 1016)

	// --- Флоу после авторизации (реверс: Game_UpdateAndRenderFrame стр.539867-539881 + клип b_00_0) ---
	// Сцена (задник+шкатулка) стоит в ДАЛЬНЕЙ позе якоря (key0 b_00_0); по логину-OK трек якоря
	// отыгрывается до identity (3.33с) = «камера подлетает к шкатулке», затем шкатулка играет клип _0 (открытие).
	UPROPERTY()
	TObjectPtr<class ASkeletalMeshActor> BackdropActor;
	UPROPERTY()
	TObjectPtr<class ASkeletalMeshActor> CasketActor;
	// Эффект шкатулки char-select (eft_creation01, стоит в уровне, скрыт до открытия шкатулки).
	UPROPERTY()
	TObjectPtr<class AActor> CasketEffectActor;

	// 1=OK→флоу; любой другой статус = нотис + возврат на логин (авто-ретрая НЕТ, он плодил дубль-сессию).
	void OnLoginResult(uint8 Status);
	// 4101/обрыв: bClosed=true → сокет закрыт, откат на экран логина (повторный вход поднимет новый коннект).
	void OnLoginKick(int32 Code, bool bClosed);
	// ★ УДАЛЕНЫ `LastLogin`/`LastPassword`/`OverlapRetries` — остатки авто-ретрая при overlap, которого
	//   больше нет (см. OnLoginResult). Писались, но не читались нигде; открытый пароль лежал в акторе
	//   весь сеанс без единого потребителя. Возвращать их нельзя: авто-ретрай создаёт вторую сессию,
	//   сервер кикает 4101 code 02 («канал не выбирается»).
	bool bLoggedIn = false;                  // вошли (server/char-select) — гасит всплытие логина от стрея-пакетов
	// Заявка на РАННИЙ коннект к логин-серверу: взводится в StartPlay, исполняется ПЕРВЫМ Tick.
	//   Раньше коннект стоял прямо в StartPlay — до первого кадра, и пауза на нём выглядела как зависший
	//   клиент, который «даже не пытается соединиться» (аудит 2026-09-02, К-4).
	bool bEarlyConnectPending = false;
	void FreezeCasketClosed();               // логин: клип _0 кадр 0, пауза (ЗАКРЫТА)
	void ClearCharSelectSceneClosed();       // ★ R5: снести сцену выбора (персонажи, партнёры, глифы) и закрыть шкатулку — общая для двух путей «назад»
	void ApplyFlightPose(float TimeSec);     // сэмпл трека b_00_0 (лерп/слерп): камера (★ 13.09) или, в откате, фон+шкатулка

	// Фазы char-select (строго как оригинал): перелёт → шкатулка ОТКРЫВАЕТСЯ → и ТОЛЬКО потом персонаж.
	bool bFlightActive = false;      // фаза 1: камера летит к шкатулке
	float FlightTime = 0.f;
	bool bCasketOpening = false;     // фаза 2: шкатулка проигрывает клип открытия
	float CasketOpenTime = 0.f;
	float CasketOpenDur = 2.0f;      // длительность клипа _0 открытия (сек; уточняется из анима)
	bool bCasketOpened = false;      // фаза 3: шкатулка открыта → персонаж появляется

	// --- Спавн персонажа в слоте шкатулки (char-select, charselect_render.md §1) ---
	// Персонаж = сборка статических частей (chst/pant/shoe/face/hair) в bind-позе на общем локале модели.
	// Эталон: playerCharObjectLoadColored@0xF0A2B0 собирает 6 частей SetPartsModelByType; тут — первый
	// рабочий срез (тело+штаны+обувь+лицо+волосы) как StaticMesh, привязанный к якорю сцены (слот CH_001).
	UPROPERTY()
	TObjectPtr<AActor> CharacterActor;
	// Персонажи по слотам (до 3, по char-list) + глиф-акторы пустых слотов (eft_creation02/03/04).
	UPROPERTY()
	TArray<TObjectPtr<AActor>> CharacterActors;
	UPROPERTY()
	TArray<TObjectPtr<class AActor>> GlyphActors;   // [0..2] слоты; показываются на пустых

	// ★ Фейд-ин появления превью (эталон: ~1с после открытия шкатулки; персонажи всех занятых слотов + глифы
	//   пустых — ОДНОВРЕМЕННО, один раз). ПЕРСОНАЖИ: временный СВАП материалов на translucent
	//   M_AsdaPreviewFade (honest-фейд; дизер в общей masked-маске ломал весь мир) с возвратом исходных.
	//   ГЛИФЫ: скаляр "FadeIn" их эффект-материалов (translucent, авто-MID) — asda_add_fadein_param.py.
	struct FPreviewFadeEntry
	{
		TWeakObjectPtr<class UMeshComponent> Comp;
		TArray<TObjectPtr<class UMaterialInterface>> Orig;         // исходные материалы слотов (вернуть по концу)
		TArray<TObjectPtr<class UMaterialInstanceDynamic>> Mids;   // временные фейд-MID по слотам (null = слот не свапнут)
	};
	TArray<FPreviewFadeEntry> PreviewFadeEntries;   // персонажи (свап)
	// ★ GC-защита фейда: Orig/Mids в FPreviewFadeEntry — НЕ-UPROPERTY TObjectPtr (структура без рефлексии) →
	//   сборщик мусора мог собрать экип-MIDы за время фейда/хитча → dangling при возврате материалов
	//   (краш FinishPreviewFade 0xffffffffffffffff). Strong-ref зеркало держит их живыми до конца фейда.
	UPROPERTY() TArray<TObjectPtr<UObject>> PreviewFadeKeepAlive;
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-C, F11): глифы пустых слотов — эффект-акторы; их альфа — множитель
	//   `AAsdaEffectActor::FxAlphaScale` (MID на эффекте не живёт), и только при ключе `asda.Login.GlyphFade 1`.
	//   Было: `PreviewFadeGlyphComps` (меш-компоненты, скаляр FadeIn на авто-MID — эффект снимал его пересборкой секций).
	TArray<TWeakObjectPtr<class AAsdaEffectActor>> PreviewFadeGlyphFx;
	float PreviewFadeTime = -1.f;                   // <0 = фейд неактивен
	static constexpr float PreviewFadeDur = 1.0f;   // длительность (эталон ~1с)
	void StartPreviewFadeIn();                      // собрать превью+глифы и запустить фейд
	void CollectPreviewFadeComps(float ApplyAlpha); // (пере)собрать (пересборка превью при догрузке 0x3FF)
	void FinishPreviewFade();                       // вернуть исходные материалы, сброс
	// ★★★ ПРЕДЗАГРУЗКА ЭКРАНА ВЫБОРА ПЕРСОНАЖА (задача владельца 13.09.2026, `Reports/charsel_preload/`):
	//   фоном и заранее — всё, из чего собираются превью (меши, клипы, материалы, текстуры с полными мипами),
	//   прогрев PSO и правило качества персонажа на превью. Ключ отката `asda.Login.CharSelPreload 0`.
	void RequestCharSelectPreload();                 // собрать пути по текущим данным и отдать менеджеру загрузки
	void ReleaseCharSelectPreload();                 // выход с экрана выбора (назад к каналам, вход в мир)
	void OnCharSelectPreloadLoaded();                // ассеты в памяти → скрытые компоненты для прекэша PSO
	void ApplyPreviewStableQuality(AActor* Preview); // LOD0 + постоянные полные мипы на актор превью (char-stable-quality)
	UPROPERTY() TObjectPtr<AActor> CharSelPsoWarmActor;   // скрытый актор прогрева PSO (живёт до ReleaseCharSelectPreload)
	TSet<TObjectKey<class UStreamableRenderAsset>> CharSelPreviewMips;   // доля превью во флаге полных мипов (реестр UAsdaTextureSubsystem), до ReleaseCharSelectPreload
	// ★ Контент слота (персонаж ИЛИ глиф) по слоту [0..2] — для карусель-перепривязки к костям пьедестала
	//   при смене выбранного слота (эталон: контент назначается кости по роли front/next/prev, не фикс-индексу).
	UPROPERTY()
	TArray<TObjectPtr<class AActor>> SlotContent;
	void ReattachCarousel();   // перепривязать контент всех слотов к костям пьедестала T0[curSlot=SelectedSlot]
	// Спутники слота (партнёр + «душа») — на ту же кость, что и мой персонаж; общая точка для idle-раскладки
	//   и раскладки поворота (02.09.2026: поворот перепривязывал только моего → «чужой силуэт» при прокрутке).
	void AttachSlotCompanionsToBone(int32 SlotIdx, class USkeletalMeshComponent* CaskComp, FName Bone);

	// Локальный оффсет слота относительно якоря шкатулки (узел CH_001 сцены; точные XYZ рантайм —
	// в исходном jbm нет литералов, берём тюнингуемый оффсет). ★ ASDA_CHAR_OFFSET="x,y,z" переопределяет.
	UPROPERTY(EditAnywhere, Category="AsdaLogin")
	FVector SlotLocalOffset = FVector(0.f, 0.f, 0.f);

	// Масштаб персонажа в слоте (jbm-верты в игровых ед.; тюнингуемо ASDA_CHAR_SCALE).
	// ★★★ 12.09.2026: было 1.f. Меши превью запечены glTF-импортом в ×100 (рост 15 592 юн = 156 м),
	//   а сцена логина переведена в ×1 (GLoginScale в AsdaLoginGameMode.cpp) → нужен 0.01.
	//   Значение перекрывает DA_LoginConfig; этот дефолт — фолбэк, когда конфига нет.
	UPROPERTY(EditAnywhere, Category="AsdaLogin")
	float CharacterScale = 0.01f;

	// ★ Карусель = РОДНАЯ анимация ШКАТУЛКИ (крутится подставка+ручка+шестерёнки), НЕ поворот актора.
	//   Эталон charSelect_PlaySlotAnim@0xF9F090: mode 2 (lturn) грузит трек 8-curSlot, mode 3 (rturn) — curSlot+9.
	//   В нашем GLB anim_06..08 = lturn слот 0/1/2, anim_09..11 = rturn слот 0/1/2 (тот же скелет 45 костей,
	//   персонажи на костях CH_00N едут вместе с подставкой). По завершении клипа — сменить активный слот + idle.
	bool  bTurnPlaying     = false;  // идёт клип поворота шкатулки
	float TurnTime         = 0.f;    // прошло с начала клипа поворота (сек)
	float TurnDur          = 2.0f;   // длительность клипа поворота (сек; уточняется из анима)
	int32 PendingSlot      = 0;      // слот, который станет активным ПОСЛЕ завершения поворота
	void StartCasketTurn(bool bRight);   // запуск клипа lturn/rturn на CasketActor (curSlot=SelectedSlot)

	// ★ Звук анимации шкатулки (эталон charSelect_PlaySlotAnim@0xF9F090 → csoundPlayName2DStore, 2D-звук).
	//   Пороги в КАДРАХ клипа (30fps), одноразовое срабатывание на клип (латч). Файлы = SFT.BIN id 45..54:
	//     open/idle(case0): f0→S_e_creation01+03, f19→04, f33→05, f55→02
	//     turn (case2/3):   f0→S_e_creation09,    f10→08, f51→10
	void PlayCreationSound(int32 CreationIdx);   // проиграть S_e_creation{NN} 2D (NN=1..10)
	// Проиграть кью, чей кадр (TimeSec×AnimFrameRate) достигнут и ещё не сыгран (латч Mask по индексу кью).
	void FireCasketCues(const TArray<struct FAsdaCasketSoundCue>& Cues, float TimeSec, uint32& Mask);
	uint32 OpenSndMask = 0;          // латч сыгранных кью open-клипа (бит = индекс кью в конфиге)
	uint32 TurnSndMask = 0;          // латч сыгранных кью turn-клипа

	// ★ Конфиг сцены (Editor-Friendly): звук-кью/масштабы/оффсеты. Назначается в редакторе; если пусто —
	//   грузится дефолтный /Game/AsdaAssets/Config/DA_LoginConfig; если и его нет — эталонные C++-дефолты.
	UPROPERTY(EditAnywhere, Category = "AsdaLogin")
	TObjectPtr<class UAsdaLoginConfig> LoginConfig = nullptr;
	const class UAsdaLoginConfig* ResolveConfig();   // вернуть конфиг (лениво грузит дефолт), либо nullptr

	// ★ Перелёт-якорь = LevelSequence (LS_LoginFlight, правится в Sequencer). Плеер создаётся в init;
	//   если секвенция не загрузилась — фолбэк на запечённый трек GLoginFlight* + ApplyFlightPose (регресс-защита).
	UPROPERTY() TObjectPtr<class ULevelSequencePlayer> FlightPlayer = nullptr;
	bool bFlightSeqMode = false;     // true = перелёт играет секвенция; false = массив-фолбэк
	void SetupFlightSequence();      // создать плеер + поза frame0 (или фолбэк)
	// ★★★ 13.09.2026: режим перелёта. true = летит КАМЕРА, фон и шкатулка стоят в конечной позе (схема владельца);
	//   false = откат «сцена летит к неподвижной камере». Берётся из DA_LoginConfig.bFlightMovesCamera,
	//   env ASDA_LOGIN_FLIGHT_SCENE=1 принудительно включает откат.
	bool bFlightMovesCamera = true;
	TWeakObjectPtr<AActor> FlightCamActor;   // LoginCamera (CameraActor с автоактивацией Player 0)
	FTransform FlightCamFixed;               // её поза в уровне = поза камеры в ПОСЛЕДНЕМ кадре перелёта (Cam_fixed)

	// ── Секвенции шкатулки (эталон): ФОРДЖИНГ после create-успеха + ЗАКРЫТИЕ на back ──
	//   Форджинг (1026 code 1, netHandleNameEntryDialogPacket→charApplyAppearanceChange + тик 3075037):
	//     закрытие (клип 3+slot, камера отлетает = обратный перелёт) → свечение eft_creation01 →
	//     открытие (клип slot, камера подлетает) → спавн+фейд нового перса (существующий StartPreviewFadeIn).
	//   Back (net_sendLoginCharEnterOpcode1028 + флаг 3075044): UI скрыть сразу → закрытие шкатулки →
	//     обратный перелёт (реверс LS_LoginFlight) → server/channel-view (порт-решение A2: этап канала).
	enum class ECasketSeqPhase : uint8 { None, ForgeClose, ForgeGlow, ForgeOpen, BackClose, BackFlight };
	ECasketSeqPhase CasketSeq = ECasketSeqPhase::None;
	float CasketSeqTime = 0.f;       // прошло в текущей фазе (сек)
	float CasketSeqDur  = 2.f;       // длительность текущей фазы (сек)
	void StartForgeSequence();       // запуск форджинга (из OnCharCreateResult code 1)
	void StartBackSequence();        // запуск back-выхода (из CARASTART_BUT02)
	void TickCasketSequence(float DeltaSeconds);      // фазовая машина (вызов из Tick)
	bool PlayCasketClip(int32 ClipIdx, float& OutDur); // one-shot клип шкатулки anim_XX (без лупа, холд конца)
	void StartFlightScaled(bool bReverse, float DurSec); // перелёт с rate = FlightDur/DurSec (DurSec<=0 → realtime)
	float FlightDurationSec() const; // длительность перелёта (секвенция или массив-трек)
	float GlowDurationSec() const;   // длительность свечения = max(StartDelay+Period) эмиттеров eft_creation01
	// ★ Камера форджинга = ОРБИТА вокруг шкатулки (эталон cameraUpdateCutsceneTransition@0xF0B540: target
	//   game (0,187,0), dist base→1700, подъём 0.5 рад — камера выше, смотрит вниз; шкатулка ОСТАЁТСЯ в кадре).
	//   НЕ реверс перелёта (он уводит в login-композицию — выглядит как «назад»). Двигаем сам CameraActor уровня.
	void SetForgeCamera(float Alpha);   // 0 = базовая поза камеры, 1 = дальняя-верхняя (dist 1700, +0.5 рад)
	void RestoreForgeCamera();          // вернуть камеру в точную базовую позу (конец форджинга)
	TWeakObjectPtr<AActor> OrbitCamActor;
	FVector  OrbitBaseLoc = FVector::ZeroVector;
	FRotator OrbitBaseRot = FRotator::ZeroRotator;
	bool bOrbitSaved = false;

	// ── УДАЛЕНИЕ: ответ сервера 1019 (эталон: code 1 → слот чистится ЛОКАЛЬНО, без повторного запроса списка) ──
	void OnCharDeleteResult(int32 Code);
	int32 DeleteSlot = -1;              // слот, на который отправлен 1018 (ждём 1019)
	bool bForgeTestDone = false;        // self-test ASDA_TEST_FORGE: форджинг запущен один раз
	void FinishCasketTurn();             // по завершении: активировать PendingSlot + вернуть idle-позу
	void SpawnCharacterInSlot();              // оркестратор: 3 слота — персонаж по char-list ИЛИ глиф на пустом
	void SpawnOneCharSlot(int32 SlotIdx, uint8 Sex);   // собрать персонажа (меш по полу) на кости CH_00{i+1}
	void ApplyEquipToSlot(AActor* SlotChar, USceneComponent* Root, USkeletalMeshComponent* BaseSkel, uint8 Sex, int32 SlotIdx);  // рендер надетой снаряги (броня+оружие)
	bool bCharacterSpawned = false;
};
