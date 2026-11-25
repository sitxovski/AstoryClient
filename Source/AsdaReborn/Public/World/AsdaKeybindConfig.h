// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaKeybindConfig — полная система клавиш игрока (порт CKeybindMgr / CKeyConfig).
//
// ★ ГРАУНД-ТРУС (всё сверено ДВАЖДЫ: декомпил + прямой дизасм бинаря 13 344 768 б):
//   · `keybindLoadDefaults@0xE3A9C0`      — 85 хардкод-дефолтов (cmd, mod, VK);
//   · `UI_BuildKeybindLabels@0xE38B30`    — 86 лейблов actionId 0..85 («PAGE 1:», …, с ДВОЕТОЧИЕМ);
//   · `CKeyConfig_BuildDefaultBindings@0xE34730` — 74 пары VK↔имя клавиши («F1», «SHIFT», «[», «PGUP»…);
//        ⚠ эта функция декомпилируется с «bad sp value» — целочисленные ключи в .c МУСОР,
//          таблица снята дизассемблером напрямую из .text, см. `Reports/keybinds_shortcutkey.md`;
//   · `keybind_LoadShortcutkeyIni@0xE3BBE0` + `cfgParsePrefixedOption@0xE3C0B0` — чтение ini;
//   · `keybindSaveShortcutIni@0xE3C300`   — запись ini;
//   · `Keybind_SetBind@0xE3B660` (+`Keybind_RemoveConflicts@0xE3B8A0`), `Keybind_Lookup@0xE3B740`,
//     `Keybind_ResolveModifier@0xE3CA80`, `Keybind_NormalizeVK@0xE3CB60`.
//
// ★ РАСКЛАДКУ РЕШАЕТ INI, А НЕ ХАРДКОД. `keybindInputCtrlInit@0xE3BAF0` читает
//   `<GameDir>\system\Shortcutkey.ini` сразу после установки дефолтов, поэтому у реального игрока
//   PAGE 1..5 = F1..F5 (а не Shift+F1..F5) и NEW BAR SLOT = SHIFT+1..9 / LEFT / [ / ] (а не F1..F12).
//   Порт читает тот же файл в том же формате; при его отсутствии — хардкод-дефолты (и ini пишется).
//
// Семантика команд (cmd → действие) — `Reports/keybind_actions.md`; исполнение в AsdaPlayerController.
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "AsdaKeybindConfig.generated.h"

class UAsdaKeybindProfile;

// Модификатор (`Keybind_ResolveModifier@0xE3CA80`): 0=нет, 1=Shift, 2=Ctrl, 3=Alt.
//   Приоритет при живом опросе — Shift → Ctrl → Alt (порядок GetKeyState в эталоне).
UENUM(BlueprintType)
enum class EAsdaKeyMod : uint8
{
	None = 0, Shift = 1, Ctrl = 2, Alt = 3
};

// Один бинд: Windows virtual-key + модификатор (как в оригинале). VK 0 = не назначено («NONE»).
//   ★ В памяти оригинала это std::pair, где ПЕРВЫЙ элемент — модификатор, ВТОРОЙ — VK
//     (`CPair_Set@0xE39C50`: `this[1]=vk; this[0]=mod`). Здесь поля названы явно.
USTRUCT(BlueprintType)
struct FAsdaKeyBind
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Keybind") int32 Vk = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Keybind") EAsdaKeyMod Mod = EAsdaKeyMod::None;

	bool operator==(const FAsdaKeyBind& O) const { return Vk == O.Vk && Mod == O.Mod; }
	bool operator!=(const FAsdaKeyBind& O) const { return !(*this == O); }
	bool IsBound() const { return Vk != 0; }
};

// Откуда взялась ДЕЙСТВУЮЩАЯ раскладка (для консольной команды `asda.Keybinds`).
UENUM(BlueprintType)
enum class EAsdaKeybindSource : uint8
{
	Defaults        UMETA(DisplayName = "хардкод-дефолты (keybindLoadDefaults)"),
	Profile         UMETA(DisplayName = "DataAsset-профиль"),
	ShortcutkeyIni  UMETA(DisplayName = "system/Shortcutkey.ini (legacy-импорт из оригинала)"),
	// ★★★ ШТАТНЫЙ КОНФИГ ДВИЖКА — основной и ПОБЕЖДАЮЩИЙ слой (решение владельца 2026-08-29).
	InputSettings   UMETA(DisplayName = "Config/DefaultInput.ini → ActionMappings (штатный UE)")
};

UCLASS(BlueprintType)
class ASDAREBORN_API UAsdaKeybindConfig : public UObject
{
	GENERATED_BODY()

public:
	// cmd_id → бинд: 1..85 — эталонные действия, 86..94 — НАШИ расширения (см. `IsExtensionCmd`).
	//   Пустые cmd (VK 0) присутствуют для полноты/ремапа.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Keybind")
	TMap<int32, FAsdaKeyBind> Binds;

	// Профиль-DataAsset (редакторный слой + путь к ini). Может быть null — тогда только дефолты+ini.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Keybind")
	TObjectPtr<UAsdaKeybindProfile> Profile;

	// Какой слой сформировал итоговую раскладку и из какого файла.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Keybind")
	EAsdaKeybindSource Source = EAsdaKeybindSource::Defaults;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Keybind")
	FString SourcePath;

	// ── Загрузка раскладки ──────────────────────────────────────────────────────────────────────
	// Хардкод-дефолты 1:1 `keybindLoadDefaults@0xE3A9C0` (85 записей, проверены дизасмом).
	UFUNCTION(BlueprintCallable, Category="Asda|Keybind")
	void LoadDefaults();

	// ПОЛНЫЙ поток: дефолты → профиль → ini. Вызывается контроллером в BeginPlay.
	UFUNCTION(BlueprintCallable, Category="Asda|Keybind")
	void LoadLayout(UAsdaKeybindProfile* InProfile);

	// Перечитать раскладку с диска на лету (консоль `asda.Keybinds.Reload`). true = ini прочитан.
	UFUNCTION(BlueprintCallable, Category="Asda|Keybind")
	bool ReloadLayout();

	// Прочитать/записать ЭТАЛОННЫЙ формат `Shortcutkey.ini` (порт keybind_LoadShortcutkeyIni /
	//   keybindSaveShortcutIni). Формат строки: `<ЛЕЙБЛ С ДВОЕТОЧИЕМ>\t<КЛАВИША>`, где клавиша —
	//   `NONE`, имя из таблицы `VkToKeyName`, либо `SHIFT+X` / `CTRL+X` / `ALT+X`.
	UFUNCTION(BlueprintCallable, Category="Asda|Keybind")
	bool LoadShortcutkeyIni(const FString& Path);
	UFUNCTION(BlueprintCallable, Category="Asda|Keybind")
	bool SaveShortcutkeyIni(const FString& Path) const;

	// ══════ ★★★ ШТАТНЫЙ МЕХАНИЗМ ДВИЖКА (основной источник раскладки) ══════════════════════════
	//   Проект на LEGACY-вводе (доказательство — в шапке файла), значит штатное место клавиш —
	//   `UInputSettings::ActionMappings`, то есть секция `[/Script/Engine.InputSettings]` файла
	//   `Config/DefaultInput.ini`, а пользовательские переназначения движок сам кладёт в
	//   `Saved/Config/<Платформа>/Input.ini` (`UInputSettings::SaveKeyMappings`).
	//   Имя действия — `ASDA_<ИМЯ ИЗ ОРИГИНАЛА через подчёркивания>`; неназначенные присутствуют
	//   с `Key=None`, чтобы действие было видно и не потерялось.

	// Прочитать раскладку из ActionMappings движка. Возвращает число применённых действий.
	UFUNCTION(BlueprintCallable, Category="Asda|Keybind")
	int32 LoadFromInputSettings();

	// Записать ВСЮ раскладку в ActionMappings движка. bSaveToDisk — ещё и сохранить штатно
	//   (`SaveKeyMappings` + `SaveConfig`), то есть в пользовательский Input.ini.
	UFUNCTION(BlueprintCallable, Category="Asda|Keybind")
	int32 SaveToInputSettings(bool bSaveToDisk);

	// Имя действия движка для кода команды: `ASDA_PAGE_1`, `ASDA_ACTION_TARGET_SHARE_NEXT`, …
	//   Пустое имя = такого действия нет.
	static FName GetInputActionName(int32 Cmd);
	// Обратно: имя действия движка → код команды (0 = не наше действие).
	static int32 CmdFromInputActionName(FName ActionName);

	// Путь к ini: профиль → `<ProjectDir>/system` → `<LaunchDir>/system`.
	//   bForWrite=true возвращает путь, даже если файла ещё нет.
	static FString ResolveShortcutkeyIniPath(const UAsdaKeybindProfile* Prof, bool bForWrite);

	// ── Поиск / изменение ───────────────────────────────────────────────────────────────────────
	// Найти cmd по нажатой клавише (VK+mod) — эталон `Keybind_Lookup`. 0 = нет команды.
	UFUNCTION(BlueprintCallable, Category="Asda|Keybind")
	int32 LookupCmd(int32 Vk, EAsdaKeyMod Mod) const;

	UFUNCTION(BlueprintCallable, Category="Asda|Keybind")
	bool GetBind(int32 Cmd, FAsdaKeyBind& OutBind) const;

	// Назначить бинд команде (rebind). Возвращает cmd, у которого клавиша БЫЛА СНЯТА (0 = ни у кого).
	//   ★ Эталон `Keybind_SetBind@0xE3B660` в конце зовёт `Keybind_RemoveConflicts@0xE3B8A0` —
	//     прежний владелец комбинации ОЧИЩАЕТСЯ (и только если новый VK != 0). Здесь так же,
	//     НО только внутри эталонного диапазона 1..85: наши расширения 86..94 работают в
	//     исключительных режимах и штатно делят клавиши с эталонными действиями (подробности и
	//     разбор дефекта — в комментарии к телу `SetBind`, аудит 2026-09-02, находка №12).
	//   ★ 25.10.2026 `bPlayerRebind` — назначение ИГРОКОМ (окно настроек, консоль `asda.Keybinds.Set`), а не чтение файла:
	//     тогда прыжок (наше расширение 95, тот же игровой контекст, что 1..85) участвует в снятии конфликта наравне с
	//     эталонными действиями — «одна клавиша — одно действие» (ключ `asda.Keys.JumpPickupSplit`). Чтение ini/профиля
	//     конфликтов с прыжком не снимает: старые файлы чинит `ApplyJumpPickupSplit` после загрузки целиком.
	UFUNCTION(BlueprintCallable, Category="Asda|Keybind")
	int32 SetBind(int32 Cmd, int32 Vk, EAsdaKeyMod Mod, bool bPlayerRebind = false);

	// Переназначение «как в игре»: нормализует VK (эталон `Keybind_NormalizeVK`), отбрасывает
	//   клавиши вне таблицы 74 (эталон: нотис 11603 «клавиша недопустима») и, если профиль просит,
	//   сохраняет ini. Принимает любой код действия, у которого есть лейбл (1..85 эталонных и
	//   86..94 наших). Возвращает cmd, у которого клавиша была снята (0 = ни у кого).
	//   ★ 25.10.2026 `bSaveIni` — записать ini сразу (прежнее поведение). Окно настроек передаёт false: у эталона захват
	//     правит только «редактируемую» раскладку (`Input_OnKeyDown_Dispatch`, ветка captureMode), а файл пишет «ОК»
	//     (`optionsWnd_ApplyAndSave@0xED5800` → `keybindSaveShortcutIni`); «Отмена» иначе не отменяла бы назначение после
	//     перезапуска (ключ `asda.Keys.SaveOnOkOnly`).
	UFUNCTION(BlueprintCallable, Category="Asda|Keybind")
	int32 RebindFromKey(int32 Cmd, const FKey& Key, EAsdaKeyMod Mod, bool bSaveIni = true);

	// ★ 25.10.2026 [ИСПРАВЛЕНИЕ] (ключ `asda.Keys.JumpPickupSplit`): «подобрать» (56) и прыжок (95) не делят одну клавишу.
	//   Пробел у порта — прыжок (решение владельца); эталонный дефолт подбора тоже Пробел (`keybindLoadDefaults`), и до
	//   правки Пробел жал обе команды. Здесь: если после загрузки у 56 и 95 одна и та же клавиша (так бывает ТОЛЬКО в файлах,
	//   записанных до правки, — назначение игроком их теперь разводит), подбор переезжает на дефолт порта F (донор
	//   AsdaFableClassic держит подбор на F), если F свободна; иначе — «не назначено». Возврат — переназначено ли.
	bool ApplyJumpPickupSplit(const TCHAR* Why);

	// Ключ `asda.Keys.SaveOnOkOnly` (окно настроек читает его через аксессор — имя ключа в рабочем коде окна не нужно).
	static bool IsSaveOnOkOnly();

	// ★ Самотест: увести файл раскладки в песочницу (пусто — снять). Высший приоритет в `ResolveShortcutkeyIniPath`,
	//   как у `-AsdaShortcutkeyIni=`; нужен, чтобы самотест переназначения не записал `system/Shortcutkey.ini` владельца.
	static void SetTestIniOverride(const FString& Path);

	// ★ 26.10.2026 (ревизия wave_1025d п. 7) [ПОРТ]: самотест допущен офлайн-гейтом (`FAsdaOutgoingGate::RefuseDebugWhenOnline`) —
	//   отметка процесса для предохранителя `ResolveShortcutkeyIniPath`.
	static void NoteSelfTestAdmitted();
	// Самотест-запуск ли это: пакет (`-saveddirsuffix=`), самотест в командной строке (`-ExecCmds=…SelfTest…`) или `-AsdaNoNet`
	//   и допущенный офлайн самотест. В таком запуске без песочницы раскладки (`-AsdaShortcutkeyIni=` / `SetTestIniOverride`)
	//   `ResolveShortcutkeyIniPath` уводит файл в `Saved/AsdaSelfTest/Shortcutkey.ini` (копия файла владельца при первом обращении).
	//   В Shipping — всегда false.
	static bool IsSelfTestRun();
	// ★ 26.10.2026 (ревизия wave_1025d 2.2) [ИСПРАВЛЕНИЕ]: миграция `ApplyJumpPickupSplit` оставила подбор без клавиши (F занята) —
	//   код команды, держащей F (0 — такого не было). Отметка снимается чтением; «один раз за процесс» держит показывающий
	//   (`UAsdaSystemOptionSubsystem::Tick`).
	static int32 ConsumePickupUnboundNotice();

	// ── Справочники (все — прямые дампы бинаря) ─────────────────────────────────────────────────
	// Лейбл действия С ДВОЕТОЧИЕМ, как в ini и в `UI_BuildKeybindLabels`: эталонные 0..85 И наши
	//   расширения 86..94 (они лежат отдельным массивом, чтобы эталонная нумерация не сдвигалась).
	//   Вне обоих диапазонов → nullptr. ★ Это и есть канонический признак «код действия существует»:
	//   проверять надо им, а не сравнением с 85 — три места уже разъезжались (аудит 2026-09-02, №11).
	static const TCHAR* GetActionLabel(int32 Cmd);
	// Имя действия без двоеточия — для лога и окна переназначения.
	static FString GetCmdName(int32 Cmd);
	// ★ Последний существующий код действия (эталонные 1..85 + НАШИ расширения 86+).
	//   Обходить действия следует до него, а не до 85: иначе расширения «потеряются».
	static int32 GetLastActionCmd();
	// ★ Это НАШЕ расширение (86+), которого в оригинале не было?
	static bool IsExtensionCmd(int32 Cmd);
	// Имя клавиши по VK («F1», «SHIFT», «[», «PGUP»…). Не в таблице → nullptr. VK 0 → «NONE».
	static const TCHAR* VkToKeyName(int32 Vk);
	// VK по имени клавиши (точное сравнение, как `StringMap_FindValueByName` = std::string::compare).
	static int32 KeyNameToVk(const FString& Name);
	// Бинд → строка ini («SHIFT+1», «F1», «NONE») — эталон `keybindSaveShortcutIni`.
	static FString FormatBind(const FAsdaKeyBind& B);
	// Строка ini → бинд — эталон `cfgParsePrefixedOption@0xE3C0B0`. false = токен не распознан.
	static bool ParseBindToken(const FString& Token, FAsdaKeyBind& Out);

	// Нормализация VK при вводе — эталон `Keybind_NormalizeVK@0xE3CB60`:
	//   нумпад 96..105 → 48..57; VK 13/16/17/18 (Enter, Shift, Ctrl, Alt) → 0.
	static int32 NormalizeVk(int32 Vk);

	// UE FKey ↔ Windows VK (для чтения ввода UE и показа в окне опций).
	static int32 FKeyToVk(const FKey& Key);
	static FKey  VkToFKey(int32 Vk);
	// Нумпад-двойник цифрового VK 48..57 (эталон нормализует нумпад в цифровой ряд). Иначе Invalid.
	static FKey  VkToNumpadFKey(int32 Vk);
	// Текущий модификатор по состоянию Shift/Ctrl/Alt (эталон `Keybind_ResolveModifier` без явного mod).
	static EAsdaKeyMod CurrentModifier(bool bShift, bool bCtrl, bool bAlt);

	// Человекочитаемый дамп всей раскладки + источник (консоль `asda.Keybinds`).
	FString DumpLayout(bool bOnlyBound) const;

private:
	// ★ 25.10.2026: команда «игрового» контекста для правила «одна клавиша — одно действие» при назначении игроком:
	//   эталонные 1..85 и прыжок 95 (расширения 86..94 живут в исключительных режимах и делят клавиши штатно).
	static bool IsWorldConflictCmd(int32 Cmd);
	// Владелец комбинации среди команд игрового контекста, кроме `ExcludeCmd` (наименьший код, как `Keybind_Lookup`). 0 — нет.
	int32 LookupWorldOwner(int32 Vk, EAsdaKeyMod Mod, int32 ExcludeCmd) const;
};

// ★ 26.10.2026 (ревизия wave_1025d п. 7) [ПОРТ]: ПЕСОЧНИЦА РАСКЛАДКИ ДЛЯ САМОТЕСТОВ ОКНА НАСТРОЕК — схема `asda.SelfTest.KeybindActions`
//   в одном месте. «ОК»/«Применить» окна 69500 пишут `Shortcutkey.ini` (`SaveShortcutkeyIni`) и ActionMappings (`Saved/Config`);
//   без песочницы самотесты `asda.SysOpt.*` писали `system/Shortcutkey.ini` владельца.
//   `Begin`: отказ без `-saveddirsuffix` (иначе ActionMappings «ОК» запишет `Input.ini` владельца); снимок раскладки живого объекта
//   (`Binds`/`Source`/`SourcePath`) и ActionMappings ASDA_*; копия действующего файла раскладки в
//   `Saved/AsdaSelfTest/Shortcutkey_<Tag>.ini`; `SetTestIniOverride` на неё. `End` (и деструктор): снять песочницу, вернуть
//   раскладку и ActionMappings, удалить копию; true — файл раскладки владельца (`<ProjectDir>/system/Shortcutkey.ini`) байт в байт
//   тот же, что на `Begin`. Некопируемый.
struct ASDAREBORN_API FAsdaKeybindTestSandbox
{
	FAsdaKeybindTestSandbox() = default;
	~FAsdaKeybindTestSandbox() { End(); }
	FAsdaKeybindTestSandbox(const FAsdaKeybindTestSandbox&) = delete;
	FAsdaKeybindTestSandbox& operator=(const FAsdaKeybindTestSandbox&) = delete;

	// Взвести песочницу для живого объекта раскладки `Keys` (может быть null — тогда только файл). `Tag` — часть имени копии.
	//   false — отказ (причина в `OutWhy`), ничего не взведено.
	bool Begin(UAsdaKeybindConfig* Keys, const TCHAR* Tag, FString& OutWhy);
	// Снять (повторный вызов ничего не делает и возвращает итог первого). true — файл владельца не изменился.
	bool End();
	// Песочница взведена.
	bool IsArmed() const { return bArmed; }
	// Путь копии раскладки (пусто — не взведена).
	const FString& GetPath() const { return Path; }

private:
	bool bArmed = false;                              // взведена
	bool bOwnerUntouched = true;                      // итог сверки файла владельца (после `End`)
	FString Path;                                     // копия раскладки в Saved/AsdaSelfTest
	FString OwnerPath;                                // `<ProjectDir>/system/Shortcutkey.ini`
	bool bOwnerExisted = false;                       // файл владельца был на `Begin`
	TArray<uint8> OwnerBytes;                         // его байты на `Begin`
	TWeakObjectPtr<UAsdaKeybindConfig> WKeys;         // живой объект раскладки (слабо: контроллер мог уйти)
	TMap<int32, FAsdaKeyBind> SavedBinds;             // его раскладка на `Begin`
	EAsdaKeybindSource SavedSource = EAsdaKeybindSource::Defaults;   // и источник
	FString SavedSourcePath;                          // и путь источника
	TSharedPtr<TArray<struct FInputActionKeyMapping>> SavedMappings;  // ActionMappings ASDA_* на `Begin`
};
