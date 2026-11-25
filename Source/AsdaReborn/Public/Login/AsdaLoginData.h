// Copyright (c) 2026 @sitxovski (Discord)
// Логин-данные порта. ВЕСЬ текст берётся из ИМПОРТИРОВАННЫХ ассетов (DT_GameText и др.), не из .bin в рантайме
//   (правило never-invent: текст только из оригинала, но через offline-импорт → UE-ассеты, код ссылается на ассет).
//   Форматы исходных .bin (error list.bin StringTable / loginhelpertext.bin) задокументированы в Reports.
//   Здесь остаются лишь: cp1251-декод (общий) + check.bin (сохранённый логин, не текст).
#pragma once
#include "CoreMinimal.h"
#include "AsdaOpSet.h"                 // FAsdaServerEntry (описание активного login-сервера)
#include "Engine/DataTable.h"
#include "AsdaLoginData.generated.h"

// Строка DT_ServerList — запись списка login-серверов (эталон CServerList::Load@0x981f10).
//   Источник импорта: оригинальный system/OPset.bin (см. AsdaOpSet.h), импортёр
//   Tools/asda_build_server_list_dt.py. Ключ строки = порядковый номер записи (1-based).
// ★ Рантайм БОЛЬШЕ НЕ читает OPset.bin с диска: путь был абсолютным хардкодом в каталог оригинальной
//   игры («@@ASTORY_DEV_ROOT_FWD@@/Donors/AsdaChroniclesInterlude_MINE/system/OPset.bin»), которого у игрока нет, а .bin
//   в Content/ кукер в пак не пакует. IP поверх таблицы перекрывается адресом из поля ввода (GConfig).
USTRUCT(BlueprintType)
struct FAsdaServerListRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Server") FString Group;      // группа серверов
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Server") FString IP;         // дефолт-адрес (фолбэк)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Server") int32   Port = 0;   // порт login-сервера
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Server") FString Name;       // отображаемое имя мира
};

struct ASDAREBORN_API FAsdaLoginData
{
	// ★★★ [ПОРТ] 08.10.2026: `DecodeCp1251` / `EncodeCp1251` — ГИБРИД провода (RU+EN+AR), см. `DecodeWireText` / `EncodeWireText`
	//   ниже. Имена оставлены ради ~100 точек вызова; представимое в cp1251 — байт-в-байт как раньше. Настоящий cp1251 —
	//   `…Strict`. Откат: `asda.Text.WireCodec 0`.
	// Байты провода → FString (гибрид: 0x98+cp1256 / строгий UTF-8 / cp1251).
	static FString DecodeCp1251(const uint8* Data, int32 MaxLen);

	// НАСТОЯЩИЙ CP1251 → FString (кириллица 0xC0-0xFF = U+0410.., Ё/ё 0xA8/0xB8; прочее — ASCII).
	static FString DecodeCp1251Strict(const uint8* Data, int32 MaxLen);

	// ★ ОБРАТНОЕ ПРЕОБРАЗОВАНИЕ: FString → CP1251. Пишет НЕ БОЛЕЕ `MaxBytes` байт и возвращает
	//   сколько записал. Буфер НЕ обнуляет и завершающий ноль НЕ ставит — это делает вызывающий
	//   (у разных пакетов разные правила: чат шлёт длину, а имя в char[20] требует терминатора).
	//   Непредставимый символ ⇒ '?', ровно как в трёх прежних копиях.
	//   ★ D9 (аудит `code_dedup_audit_2.md`): энкодер жил ТРЕМЯ копиями внутри одного
	//   `AsdaNetSubsystem.cpp` (`AsdaChatToCp1251`, инлайн в отправке 6616, `EncodeCp1251`) —
	//   при том, что декодер (`DecodeCp1251` выше) уже был единым. Теперь пара симметрична.
	static int32 EncodeCp1251(const FString& In, uint8* Out, int32 MaxBytes);

	// НАСТОЯЩИЙ cp1251 (таблица выше: ASCII, А..я, Ё/ё, « » – — … №; прочее → '?'). Для локальных файлов/ключей сортировки
	//   и как ветка 0 гибрида.
	static int32 EncodeCp1251Strict(const FString& In, uint8* Out, int32 MaxBytes);

	// Лимит поля ввода в байтах провода с «бесплатным» маркером cp1256: min(FieldBytes, LimitBytes + 1 для арабской ветки).
	//   LimitBytes — лимит оригинала (EM_LIMITTEXT, байты cp1251/cp1256), FieldBytes — сколько байт реально вмещает поле пакета.
	static int32 WireLimitBytes(const FString& In, int32 LimitBytes, int32 FieldBytes);

	// Самая длинная голова строки, укладывающаяся в `WireLimitBytes` (обрезка ввода по границе символа).
	static FString TruncateToWireLimit(const FString& In, int32 LimitBytes, int32 FieldBytes);

	// ★ [ПОРТ] 08.10.2026: допустимы ли СИМВОЛЫ имени персонажа — по локальным правилам оригинала (`validateStringByLocale`):
	//   имя целиком кириллической локали (case 7) ИЛИ целиком арабской (case 8, в декомпиле мисней `isDelimiterChar`).
	//   `asda.Text.NameLocale 0` — эталон порта (только A-Z a-z 0-9). Длину проверяет вызывающий.
	static bool IsValidCharNameByLocale(const FString& Name);

	// ═══ [ПОРТ] МНОГОЯЗЫЧНЫЙ СВОБОДНЫЙ ТЕКСТ (08.10.2026, RU+EN+AR; отчёт Reports/owner_1008/arabic_text.md) ═══
	//   cp1251 не умеет арабский (→ '?'), а сервер чат-текст пересылает БАЙТ-ПРОЗРАЧНО (`Recv_NormalChatClient@0x5471A0`:
	//   strlen+memcpy, конверсии кодовой страницы нет). Поэтому для СВОБОДНОГО ТЕКСТА без хранения в БД (чат 5084/5086/6560,
	//   шёпот 5088, отряд 5105, клан 4120) — ГИБРИД: строка, целиком представимая в cp1251, уходит в cp1251 БАЙТ-В-БАЙТ как
	//   раньше (русский/английский без изменений и совместимы со старыми клиентами); иначе — UTF-8.
	//   ★ Имена и поля, которые сервер пишет в БД (имя char[20], «о себе» 6080, клан, лавка, почта, питомец), — ТОЛЬКО
	//   `EncodeCp1251`/`DecodeCp1251` до миграции БД (см. `AstoryServer/Docs/_Common/arabic-multilang-plan.md`).
	//   Откат: `asda.Text.WireCodec 0` — обе функции ниже ведут себя ровно как cp1251-пара.

	// Кодек провода включён? (`asda.Text.WireCodec` ≠ 0). 1 = полный гибрид cp1251 / 0x98+cp1256 / UTF-8 (умолчание),
	//   2 = cp1251 / UTF-8 без маркера, 0 = только cp1251.
	static bool IsWireUtf8Enabled();

	// Вся строка представима в cp1251 (таблица `EncodeCp1251` без единого '?')?
	static bool IsCp1251Representable(const FString& In);

	// Вся строка представима в cp1256 (арабский + латиница + знаки Windows-1256)?
	static bool IsCp1256Representable(const FString& In);

	// ★ Маркер ветки «cp1256» в начале строки провода. 0x98 — ЕДИНСТВЕННЫЙ неопределённый байт cp1251 (его не выдаёт ни один
	//   cp1251-энкодер — ни наш, ни оригинальный WideCharToMultiByte), и с него не может начинаться валидный UTF-8 (это байт
	//   продолжения). Поэтому три ветки различаются однозначно. Зачем ветка: арабский в cp1256 — 1 байт на букву, а в UTF-8 —
	//   2; имя `char[20]` вмещает 18 арабских букв вместо 9. В базе `Joy1` (Arabic_CI_AS = cp1256) такие байты — НАСТОЯЩИЙ
	//   арабский (виден в SSMS, сортируется арабской коллацией), маркер — буква «ک».
	static constexpr uint8 kCp1256Marker = 0x98;

	// Свободный текст → байты провода (гибрид, см. выше). Пишет ≤ MaxBytes байт, БЕЗ терминатора, и НИКОГДА не режет
	//   посреди символа: UTF-8-последовательность, не влезающая целиком, отбрасывается. Возвращает число байт.
	static int32 EncodeWireText(const FString& In, uint8* Out, int32 MaxBytes);

	// Сколько байт займёт строка на проводе (та же логика, что `EncodeWireText`, без лимита) — для лимитов полей ввода,
	//   которые у оригинала считаются в БАЙТАХ (EM_LIMITTEXT по ANSI-буферу).
	static int32 WireTextByteLen(const FString& In);

	// Самая длинная голова строки, чья длина на проводе ≤ MaxBytes (обрезка ввода по символам, не по байтам).
	static FString TruncateToWireBytes(const FString& In, int32 MaxBytes);

	// Байты провода → свободный текст. До первого NUL в пределах MaxLen: строгий валидный UTF-8 c хотя бы одной
	//   многобайтовой последовательностью ⇒ UTF-8; иначе ⇒ `DecodeCp1251` (старые строки, нотисы, чужие клиенты).
	//   Незавершённая последовательность В САМОМ КОНЦЕ (сервер зажал длину посреди символа) отбрасывается, не ломая разбор.
	static FString DecodeWireText(const uint8* Data, int32 MaxLen);

	// Строгая проверка UTF-8 (без overlong/суррогатов/>U+10FFFF). OutMulti — встретилась ли многобайтовая
	//   последовательность; OutValidLen — длина валидной части (хвост-обрубок в конце допускается, если bAllowTruncTail).
	static bool IsStrictUtf8(const uint8* Data, int32 Len, bool bAllowTruncTail, bool& OutMulti, int32& OutValidLen);

	// ★ loc-строка по string-ID из ИМПОРТИРОВАННОГО ассета проекта DT_GameText (StripMarkup применён). "" если
	//   нет. Единый источник ВСЕХ подписей UI (правило never-invent: текст только из оригинала, из ассета игры).
	static FString Loc(int32 Id);

	// ★ То же, но СЫРАЯ строка С тегами <F..> (для rich-рендера через UAsdaRichText::ApplyFormatted).
	static FString LocRaw(int32 Id);

	// ★ Объектный путь `DT_GameText` — одно объявление (15.09.2026, dt_gc_hold): читают `Loc`/`LocRaw`,
	//   удерживает на сессию `UAsdaTextSubsystem::HoldGameTextTable`.
	static constexpr const TCHAR* kGameTextPath = TEXT("/Game/AsdaAssets/Text/DT_GameText.DT_GameText");

	// Срезать inline-разметку — ВСЕ 6 тегов (<FC=>/<FS##>/<FB>/<FO>/<FD>/</F>) + escape "\n"/"\t"/"\r".
	//   Делегирует в единый парсер FAsdaMarkup::StripTags (UI/AsdaMarkup.h).
	static FString StripMarkup(const FString& In);

	// check.bin (loginSaveCredentials @0xF14FD0): 33Б = byte флаг «сохранить ID» + char[32] ID (ASCII).
	// true при успехе; OutSave=флаг, OutId=сохранённый логин.
	// ★ Путь ВСЕГДА LocalCheckPath() (Saved/AsdaLogin/check.bin) — раньше писалось в каталог
	//   ОРИГИНАЛЬНОЙ игры (<папка OPset>/log/check.bin), чего у игрока нет и быть не должно.
	static bool LoadSavedId(bool& OutSave, FString& OutId);

	// Запись check.bin (тот же формат 33Б): flag + ID (обрезка до 32). Save=false → пустой ID.
	static bool SaveSavedId(bool Save, const FString& Id);

	// Локальный путь порта для сохранённого логина (пользовательский сейв): Saved/AsdaLogin/check.bin.
	static FString LocalCheckPath();

	// ★ Строка login-сервера из ассета /Game/AsdaAssets/Data/DT_ServerList (первая валидная строка), КАК ЕСТЬ.
	//   true при успехе. ★ 16.10.2026: IP из таблицы для подключения НЕ используется — только порт/группа/имя;
	//   адрес подключения даёт LoadLoginServer.
	static bool LoadActiveServer(FAsdaServerEntry& Out);

	// ★★ 16.10.2026 (решение владельца): ЛОГИН-СЕРВЕР ДЛЯ ПОДКЛЮЧЕНИЯ. Хост — всегда домен AsdaServerAddr::LoginHost();
	//   порт, группа и имя — из DT_ServerList. IP таблицы, сохранённый `[AsdaLogin] ServerIP` и поле SERVER_IP_INPUT
	//   не читаются. true — порт из таблицы получен (Out.IsValid()); false — таблица не загрузилась (Out.Port = 0,
	//   Out.IP = домен).
	static bool LoadLoginServer(FAsdaServerEntry& Out);

	// ★ 16.10.2026: удалить из настроек UE прежний сохранённый адрес мира (GameUserSettings.ini, [AsdaLogin] ServerIP —
	//   его писала старая версия клиента при каждом входе). Зовётся при старте экрана логина. true — ключ был и удалён.
	static bool PurgeLegacyServerIp();
	// Секция и ключ прежнего сохранённого адреса (для удаления и самотеста).
	static constexpr const TCHAR* LegacyServerIpSection = TEXT("AsdaLogin");
	static constexpr const TCHAR* LegacyServerIpKey     = TEXT("ServerIP");
};
