// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaStartupAdSubsystem — СТАРТОВОЕ ОКНО НОВОСТЕЙ/РЕКЛАМЫ `AD_Win` (5000), порт 02.09.2026.
//
// ═══════════════════════ ЭТО НЕ ЗАГРУЗОЧНЫЙ ЭКРАН — ЭТО ДРУГАЯ ПОДСИСТЕМА ═══════════════════════
//   Реклама на загрузочном экране (`map\load\ad_img000..008`, вероятность `g_nLoadingScreenChannel`)
//   живёт в `UAsdaLoadManagerSubsystem::PickLoadingTexture` и в оригинале НЕ ПОКАЗЫВАЕТСЯ НИКОГДА.
//   Здесь — совершенно отдельная вещь: всплывающее окно «Asda Story news» с листалкой картинок
//   `start_ad_img0N`, файлом настроек `System\AD.bin` и кнопкой «не показывать сегодня».
//   Путать их нельзя: разные картинки, разный момент показа, разные данные.
//
// ═══════════════════════════════ ЧТО ЕСТЬ В ОРИГИНАЛЕ ══════════════════════════════════════════
// КОД (образ `Donors\AsdaChroniclesInterlude\system\AsdaStory.exe`, md5 a0a00b40…; те же адреса и байты
//   проверены на `AsdaStory1.exe`, md5 9dbc69eb… — это образ, на котором построен IDB):
//   0xF01A30  InitControls   — поиск контролов 5000…5006 в LOGIN-менеджере окон (`0x15CB800`),
//                              подписи из loc 99501 / 99500 / 5035, `+0x24 = 0`, `+0x44 = 1`.
//   0xF01BB0  LoadConfig     — `fopen("<CurDir>\System\AD.bin","rt")`, 5 × (`fgets` + `sscanf("%d")`)
//                              в поля +0x30, +0x34, +0x38, +0x3C, +0x40.
//   0xF01DC0  SaveConfig     — `fopen(...,"wt")`, 5 × `sprintf("%d\n")` + `fwrite` тех же пяти полей.
//   0xF02020  Tick           — `if (+0x44 && +0x2C) RotateImage();`
//   0xF02060  RotateImage    — раз в `0xBB8` = 3000 мс: `++index`, при `index >= +0x48` → 0;
//                              `sprintf("start_ad_img0%d", index)`; `CControl_DrawTextureRect(
//                              contents, "<имя>.tex", 0, 0, 1024, 768)`; `SetSelectedIndex(page, index)`.
//   0xF02150  SetImageIndex  — то же, но индекс задан снаружи (клик по номеру страницы).
//   0xF02220  Open           — `LoadConfig`; `+0x48 = +0x34` (счётчик картинок); полосе номеров
//                              `SetSize(+0x48 * 15, 19)`; `GetLocalTime`;
//                              ЕСЛИ байт +0x30 == 0 → ВЫХОД (ничего не показывать);
//                              ЕСЛИ сохранённые Y/M/D == сегодняшним → ВЫХОД;
//                              иначе `+0x2C = 1`, `+0x28 = timeGetTime()`, `SetImageIndex(0)`,
//                              `CControl_SetVisible(window, 1)` и регистрация окна в менеджере
//                              (`sub_12AFBB0(mgr, window, 0)`: `mgr+436 = window`, `mgr+434 = 1`,
//                              `mgr+512 = 1`).
//   0xF02320  CloseForToday  — кнопка 5005: `GetLocalTime`, `+0x30 = 1`, `+0x38/+0x3C/+0x40 = Y/M/D`,
//                              `SaveConfig` (окно при этом НЕ прячется — так в оригинале).
//   0xF02390  Close          — кнопки 5001/5006: `+0x2C = 0`, `CControl_SetVisible(window, 0)`,
//                              `sub_12B2900(mgr)`: `mgr+416 = 0`, `mgr+512 = 0`.
//   ⚠ АСИММЕТРИЯ ОРИГИНАЛА, зафиксирована как факт: открытие пишет указатель окна в `mgr+436`,
//     а закрытие обнуляет `mgr+416`. Общий у них только байт `mgr+512`. В порту модальность —
//     штатный флаг реестра окон, поэтому расхождение воспроизводить нечем и незачем.
//
// ДАННЫЕ: `Donors\AsdaChroniclesInterlude\system\AD.bin` — 20 байт текста `0\r\n5\r\n2008\r\n10\r\n31\r\n`
//   (это ровно то, что пишет `SaveConfig` в текстовом режиме: `\n` → `\r\n`):
//     [0] = 0     признак показа   (0 — окно не показывать вовсе)
//     [1] = 5     число картинок   (`start_ad_img00`…`start_ad_img04`)
//     [2] = 2008  год  ) дата последнего «не показывать сегодня»
//     [3] = 10    месяц)
//     [4] = 31    день )
//   РАЗМЕТКА ОКНА: `Login.JUI` → 5000 `AD_Win` (824×677, `AlwaysTop=1`, `Visible=0`),
//     5001 `AD_ClsBtn` (крестик), 5002 `AD_Title_Move` (заголовок + ручка перетаскивания EVS 6/20003),
//     5003 `AD_Contents` (798×598 — сюда кладётся картинка), 5004 `AD_PageBtn` (полоса номеров:
//     5 цифр `MdNum_1…MdNum_5` с шагом 15 px — отсюда и `count * 15` в `Open`),
//     5005 `AD_TodayBtn` («не показывать сегодня»), 5006 `AD_ClsBtn2` («Закрыть»).
//   ПОДПИСИ: loc 99501 «Asda Story news» (заголовок), 99500 «Close for today», 5035 «Close».
//
// ═════════ ★★★ ПОЧЕМУ В ЭТАЛОННОМ КЛИЕНТЕ ОКНО НЕ ПОЯВЛЯЕТСЯ (три независимых довода) ══════════
//   1. НА КЛАСС НЕТ НИ ОДНОЙ ССЫЛКИ. Сплошной скан ВСЕГО `.text` (13 344 768 б) на `E8`/`E9` с
//      целью в {0xF01A30, 0xF02020, 0xF02220, 0xF02320, 0xF02390} и на их линкерные переходники
//      {0x93AFE8, 0x93D595, 0x92A698, 0x93765E, 0x92BE3F} даёт РОВНО пять попаданий — это сами
//      переходники (`jmp`). Вызовов нет ни одного. Поиск 4-байтового литерала любого из этих
//      десяти адресов по всему файлу — ноль попаданий, значит адрес нигде не берётся и в
//      таблицу виртуальных методов не кладётся. Ни `InitControls`, ни `Open` не вызываются.
//      (Контроль метода: тот же скан находит вызовы `LoadConfig`/`SaveConfig`/`RotateImage`/
//      `SetImageIndex` — их зовут как раз эти четыре функции. Метод рабочий, «ноль» настоящий.)
//   2. ДАННЫЕ ЗАПРЕЩАЮТ ПОКАЗ. В `AD.bin` первое число = 0, а `Open` при нуле выходит сразу.
//   3. КАРТИНОК НЕТ. Строка `start_ad_img` встречается во ВСЁМ каталоге эталонного клиента
//      только внутри двух образов `.exe`; ни файла, ни записи в `juidata\Image\Texture.btz`
//      (115 записей), ни в `gamemain` — нет.
//
//   ⇒ ПОЭТОМУ ЗДЕСЬ НЕТ АВТОМАТИЧЕСКОГО ВЫЗОВА. Подсистема воспроизведена целиком и включается
//     ровно тогда, когда появятся данные (`Enabled = 1` в `DT_StartupAd` или в `Saved/AD.bin`)
//     и картинки `start_ad_img0N`. Точку показа в сцене логина в оригинале НЕ ИЗ ЧЕГО взять —
//     её там нет, и выдумывать её нельзя (закон `never-invent-only-original`).
//     Для живой проверки есть консольные команды `asda.UI.StartupAd*` — ЭТО НАША ДОБАВКА.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/DataTable.h"
#include "Styling/SlateBrush.h"    // FSlateBrush — кэш исходного вида планов полосы номеров
#include "Styling/SlateTypes.h"    // FButtonStyle — кэш исходного стиля кнопок-номеров
#include "Subsystems/GameInstanceSubsystem.h"
#include "AsdaStartupAdSubsystem.generated.h"

class UButton;
class UImage;
class UTextBlock;
class UUserWidget;
class UAsdaButtonRelay;

// ═════════ Строка `DT_StartupAd` — ровно пять чисел файла `System\AD.bin` эталона ═════════════
//   Импортёр: `Tools/asda_build_startup_ad_dt.py` (рантайм в каталог оригинальной игры не лезет —
//   правило то же, что у `DT_ServerList`). Ключ строки — «1» (файл описывает одно состояние).
USTRUCT(BlueprintType)
struct FAsdaStartupAdRow : public FTableRowBase
{
	GENERATED_BODY()

	// [0] Признак показа. 0 = окно не показывать вовсе (значение эталонного клиента).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|StartupAd") int32 Enabled = 0;

	// [1] Сколько картинок `start_ad_img0N` листает окно (в эталонном файле 5).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|StartupAd") int32 ImageCount = 0;

	// [2][3][4] Дата последнего нажатия «не показывать сегодня» (в эталонном файле 2008-10-31).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|StartupAd") int32 SuppressYear = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|StartupAd") int32 SuppressMonth = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|StartupAd") int32 SuppressDay = 0;
};

UCLASS()
class ASDAREBORN_API UAsdaStartupAdSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ★ Порт `Open@0xF02220` целиком, вместе с обоими гейтами (признак показа и «уже сегодня
	//   отказались»). Возвращает true, если окно действительно показано.
	//   ⚠ В оригинале эту функцию НИКТО не зовёт (доказательство — в шапке файла). У нас её зовёт
	//     только консольная команда `asda.UI.StartupAd` (наша добавка для живой проверки).
	UFUNCTION(BlueprintCallable, Category="Asda|StartupAd")
	bool ShowStartupAd();

	// Порт `Close@0xF02390` — кнопки 5001 (крестик) и 5006 («Закрыть»).
	UFUNCTION(BlueprintCallable, Category="Asda|StartupAd")
	void CloseStartupAd();

	// Порт `CloseForToday@0xF02320` — кнопка 5005. ★ ОКНО ПРИ ЭТОМ НЕ ЗАКРЫВАЕТСЯ: оригинал только
	//   записывает сегодняшнюю дату в `AD.bin`. Воспроизведено 1:1.
	UFUNCTION(BlueprintCallable, Category="Asda|StartupAd")
	void SuppressForToday();

	// Порт `SetImageIndex@0xF02150` — показать картинку с этим номером (клик по номеру страницы).
	UFUNCTION(BlueprintCallable, Category="Asda|StartupAd")
	void SetImageIndex(int32 Index);

	// Окно сейчас на экране (эталонный флаг +0x2C).
	UFUNCTION(BlueprintPure, Category="Asda|StartupAd")
	bool IsShown() const { return bShown; }

	// Текстовая сводка состояния (консоль `asda.UI.StartupAdDump`): откуда взяты настройки,
	//   какие они, что нашлось в дереве виджета. Нужна, чтобы «ничего не показалось» можно было
	//   объяснить причиной, а не догадкой.
	FString DebugState() const;

private:
	// ── Настройки (эталонные поля +0x30, +0x34, +0x38, +0x3C, +0x40) ──
	int32 CfgEnabled = 0;
	int32 CfgImageCount = 0;
	int32 CfgYear = 0;
	int32 CfgMonth = 0;
	int32 CfgDay = 0;
	// Откуда они приехали (для `DebugState`): «Saved/AD.bin», «DT_StartupAd» или «нет данных».
	FString CfgSource;

	// ── Рантайм (эталонные поля +0x24, +0x28, +0x2C, +0x44, +0x48) ──
	int32  CurrentIndex = 0;      // +0x24 номер показанной картинки
	double LastRotateSec = 0.0;   // +0x28 отметка времени последней смены (у эталона timeGetTime)
	bool   bShown = false;        // +0x2C окно на экране
	bool   bReady = false;        // +0x44 контролы найдены
	int32  ImageCount = 0;        // +0x48 копия CfgImageCount, сделанная в момент показа

	// ── Виджет и его узлы ──
	UPROPERTY() TObjectPtr<UUserWidget> Widget;
	UPROPERTY() TObjectPtr<UImage>      ContentsImage;   // 5003 `AD_Contents_<n>_0`
	UPROPERTY() TObjectPtr<UTextBlock>  TitleText;       // 5002 `AD_Title_Move`
	UPROPERTY() TObjectPtr<UButton>     CloseBtn;        // 5001 `AD_ClsBtn`
	UPROPERTY() TObjectPtr<UButton>     TodayBtn;        // 5005 `AD_TodayBtn`
	UPROPERTY() TObjectPtr<UButton>     CloseBtn2;       // 5006 `AD_ClsBtn2`
	UPROPERTY() TArray<TObjectPtr<UButton>>    PageButtons;   // 5004 `AD_PageBtn_OPT0…OPT4`
	UPROPERTY() TArray<TObjectPtr<UImage>>     PagePlanes;    // планы полосы номеров
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> PageCaptions;  // подписи номеров (их у полосы нет — пусто)
	UPROPERTY() TArray<TObjectPtr<UObject>>    Relays;        // мостики `UButton::OnClicked`

	// Исходный вид полосы номеров — кэш для `AsdaUi::ApplyTabState` (аналог `SetSelectedIndex`).
	TArray<FSlateBrush>   PageIdleBrush;
	TArray<FButtonStyle>  PageIdleStyle;
	TArray<FLinearColor>  PageIdleCaption;

	FTSTicker::FDelegateHandle RotateTickHandle;   // покадровый опрос (порт `Tick@0xF02020`)

	// Создать окно и разобрать его дерево. true — контролы на месте (эталонный флаг +0x44).
	bool EnsureWidget();
	// Порт `InitControls@0xF01A30`: узлы, подписи из loc, привязка кнопок.
	void SetupControls();
	// Порт `LoadConfig@0xF01BB0`: сначала `Saved/AD.bin` (его пишет сама игра), иначе `DT_StartupAd`.
	void LoadConfig();
	// Порт `SaveConfig@0xF01DC0`: те же пять чисел тем же текстовым форматом.
	void SaveConfig() const;
	// Порт `RotateImage@0xF02060` — вызывается тикером раз в кадр, сам считает свои 3000 мс.
	bool TickRotate(float DeltaSec);
	// Полоса номеров: ширина `ImageCount * 15`, высота 19; лишние номера гасятся (порт `Open`).
	void ApplyPageCount(int32 Count);
	// Картинка `start_ad_img0<Index>` в контрол 5003 с эталонной обрезкой UV 0,0…1024,768.
	void ApplyImage(int32 Index);
	// Loc-строка по id (через UAsdaTextSubsystem).
	FString Loc(int32 Id) const;
};
