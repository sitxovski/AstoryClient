// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ОБЩИЙ КАРКАС САМОТЕСТОВ `AsdaSelfTestKit` (дедуп №2, волна В3, S1; 11.10.2026). [ПОРТ]
//   Разбор и план: `Reports/dedup_audit2/02_cpp_world_net_data.md` §2.1 (N1), `01_cpp_ui.md` U14, отчёт волны —
//   `Reports/dedup_audit2/wave3_S1.md`.
//
//   ЗАЧЕМ. У каждого самотеста был свой каркас: свой разбор `delay=`/`quit` (у трёх `quit` попадал в число), свой отказ
//   «онлайн» (у 41 из 42 без строки ИТОГ и без `quit` — пакет висел до таймаута), своя копия флага отложенного старта,
//   второй барьер шлюза только у 6 из 42, перехват отправок молча перетирал чужой, подменённый «свой» персонаж (номер,
//   charId, уровень, смерть) не возвращался. Здесь — ОДНА точка на все эти вещи. Опирается на существующие общие точки и
//   новых гейтов не вводит: офлайн-гейт и окно защиты — `FAsdaOutgoingGate` (`AsdaOutgoingGate.h`), флаг отложенного старта —
//   `FAsdaSelfTestRunGate`, ключи консоли — `AsdaCVarSave`, выход — `AsdaSelfTestBatch::Quit` (виден пакетному прогону).
//
//   ЧТО ДАЁТ:
//     • `FAsdaSelfTestArgs::Parse` — `delay=N`, `quit`, `online` в ЛЮБОЙ позиции; остальное — позиционные аргументы
//       (`Pos`), поэтому `quit` никогда не принимается за число/имя.
//     • `AsdaSelfTestKit::RefuseStart` — офлайн-гейт + ВТОРОЙ БАРЬЕР у всех (окно защиты на ВСЕ опкоды, кроме пути входа,
//       плюс опкоды самотеста). При отказе — строка `<Cmd> ИТОГ: ok=0 fail=1 (ОТКАЗ: …)` и `quit` (исправление N1(а)).
//     • `AsdaSelfTestKit::Refused` — тот же итог отказа по иной причине (нет мира, подсистемы, персонажа).
//     • `AsdaSelfTestKit::Summary` / `Finish` — единая строка `<префикс> ИТОГ: ok=N fail=M[хвост]` (её ловит
//       `asda.SelfTest.Batch`) в категорию лога самого самотеста; `Finish` ещё возвращает ключи и выходит по `quit`.
//     • `AsdaSelfTestKit::ArmCapture` — перехват отправок с ОТКАЗОМ, если перехват уже занят (исправление N1(в)).
//     • `FAsdaNetSelfScope` — RAII «свой персонаж»: номер, известность номера, charId, уровень, вес, смерть (с
//       воскрешением тем же делегатом, что 4055/4056) возвращаются на любом выходе (исправление N1(г)).
//     • `FAsdaCVarScope` — RAII ключей консоли поверх `AsdaCVarSave` (сторож мира + возврат значения и SetBy).
//
//   ОРАКУЛЫ НЕ ВЫНОСЯТСЯ: ожидаемые байты/значения проверок остаются в самих самотестах (независимыми от боевого кода).
//   Доступен во всех сборках (часть самотестов живёт в боевых подсистемах, которые собираются и в Shipping).
#pragma once
#include "CoreMinimal.h"

#include "AsdaCVarSave.h"

class UObject;
class UWorld;
class UAsdaNetSubsystem;

// Разобранные аргументы консольной команды самотеста.
struct ASDAREBORN_API FAsdaSelfTestArgs
{
	float Delay = 0.f;          // `delay=N` — задержка запуска, с (0 — без задержки)
	bool bHasDelay = false;     // аргумент `delay=` был
	bool bQuit = false;         // `quit` — выйти после прогона (и при отказе)
	bool bOnline = false;       // `online` — явное согласие оператора (только для команд, которые его признают)
	TArray<FString> Pos;        // позиционные аргументы по порядку — без `delay=`, `quit`, `online`

	// Разбор: `delay=`, `quit`, `online` (без учёта регистра) в любой позиции, прочее — в `Pos`.
	static FAsdaSelfTestArgs Parse(const TArray<FString>& Args);

	// Позиционный аргумент `I` целым (нет — `Def`).
	int32 Int(int32 I, int32 Def) const;
	// Позиционный аргумент `I` вещественным (нет — `Def`).
	float Float(int32 I, float Def) const;
	// Позиционный аргумент `I` строкой (нет — `Def`).
	FString Str(int32 I, const TCHAR* Def = TEXT("")) const;
	// Есть ли позиционный аргумент-слово `Word` (без учёта регистра).
	bool Has(const TCHAR* Word) const;
};

namespace AsdaSelfTestKit
{
	// Метка «все опкоды» в окне защиты `UAsdaNetSubsystem::DebugSendGuardOpcodes` (опкода 0 у протокола нет):
	//   окно закрывает каждый опкод, кроме пути входа игрока (`FAsdaOutgoingGate::IsSessionEntryOpcode`).
	constexpr uint16 kGuardAllOpcodes = 0;
	// Окно второго барьера по умолчанию сверх задержки старта, с (самый долгий самотест мира идёт ≈ 4 мин).
	constexpr float kDefaultGuardSec = 600.f;

	// ★ СТАРТ САМОТЕСТА. true = ОТКАЗ (есть живое соединение / нет GameInstance): строка
	//   `<Cmd> ИТОГ: ok=0 fail=1 (ОТКАЗ: онлайн — …)` и `quit` при `Args.bQuit`; команда должна сразу выйти.
	//   false = офлайн: взведено окно второго барьера на `Args.Delay + GuardSeconds` (0 — `kDefaultGuardSec`) для ВСЕХ
	//   опкодов (кроме пути входа) и для `ExtraOps`; самотест учтён офлайн-гейтом (`RefuseDebugWhenOnline`).
	ASDAREBORN_API bool RefuseStart(const UObject* Ctx, const TCHAR* Cmd, const FAsdaSelfTestArgs& Args, float GuardSeconds = 0.f,
		TArrayView<const uint16> ExtraOps = TArrayView<const uint16>());

	// Отказ запуска по иной причине (`Why`: «нет мира», «нет подсистемы сети», «перехват занят»…): строка
	//   `<Cmd> ИТОГ: ok=0 fail=1 (ОТКАЗ: Why)` (Warning) и `quit` при `bQuit`.
	ASDAREBORN_API void Refused(const UObject* Ctx, const TCHAR* Cmd, bool bQuit, const TCHAR* Why);

	// Повторная проверка офлайна на ОТЛОЖЕННОМ старте (`delay=N`) — `FAsdaOutgoingGate::RefuseDeferredStartWhenOnline`
	//   (итог «ИТОГ: ОТКАЗ на отложенном старте» и `quit` при `bQuit` печатает он). true = отказ.
	ASDAREBORN_API bool RefuseDeferred(const UObject* Ctx, const TCHAR* Cmd, bool bQuit);

	// Единая строка итога `<Prefix> ИТОГ: ok=N fail=M<Tail>` в категорию лога самотеста (`bWarning` — уровень Warning,
	//   как у самотестов, чьи строки идут Warning). `Tail` — хвост как был у самотеста (например « (перехвачено 4008: 3)»).
	ASDAREBORN_API void Summary(const FLogCategoryBase& Cat, const TCHAR* Prefix, int32 Ok, int32 Fail, const TCHAR* Tail = TEXT(""),
		bool bWarning = false);

	// Выход самотеста: при `bQuit` — `quit` движка через `AsdaSelfTestBatch::Quit` (пакетный прогон его перехватывает).
	ASDAREBORN_API void Quit(UWorld* W, bool bQuit);

	// Конец самотеста: `Summary` → возврат ключей консоли этого самотеста (`AsdaCVarSave::RestoreAll(Cmd)`, если реестр
	//   не пуст) → `Quit`. `W` может быть null.
	ASDAREBORN_API void Finish(UWorld* W, const TCHAR* Cmd, bool bQuit, const FLogCategoryBase& Cat, const TCHAR* Prefix,
		int32 Ok, int32 Fail, const TCHAR* Tail = TEXT(""), bool bWarning = false);

	// Тип перехватчика отправок (`UAsdaNetSubsystem::ArmSendCapture`): опкод, тело, длина, паддинг, конверт.
	using FCaptureFn = TFunction<void(uint16, const uint8*, int32, bool, TCHAR)>;

	// ★ N1(в): взвести перехват отправок, ТОЛЬКО если он свободен. Возвращает поколение (как `ArmSendCapture`);
	//   0 = ОТКАЗ — перехват уже держит другой самотест/стенд (Warning `[самотест] <Who>: перехват отправок занят…`),
	//   чужой перехватчик не тронут. `OnlyOpcode` 0 — весь трафик.
	ASDAREBORN_API uint32 ArmCapture(UAsdaNetSubsystem* Net, FCaptureFn Fn, uint16 OnlyOpcode, const TCHAR* Who);
	// Снять перехват своего поколения (чужой не снимается). true — снят.
	ASDAREBORN_API bool DisarmCapture(UAsdaNetSubsystem* Net, uint32 Generation);
	// Занят ли перехват отправок.
	ASDAREBORN_API bool IsCaptureBusy(const UAsdaNetSubsystem* Net);
	// Снять перехват, ОСТАВЛЕННЫЙ самотестом после своего конца (поколение — текущее). Только для пакетного прогона между
	//   самотестами (`asda.SelfTest.Batch`); true — был взведён и снят.
	ASDAREBORN_API bool DisarmLeakedCapture(UAsdaNetSubsystem* Net);

	// Сетевая подсистема из мира/GI/подсистемы GI (nullptr — нет).
	ASDAREBORN_API UAsdaNetSubsystem* NetOf(const UObject* Ctx);

	// Нужен ли офлайн-спавн своего персонажа перед прогоном: павна нет ИЛИ павн не `LocalPlayerActor` ИЛИ (задано `WantName`) своё имя другое. Загрязнение
	//   пакетного прогона: павн уже стоит от прошлого самотеста, но с чужим/пустым именем — проверки имени вырождались.
	ASDAREBORN_API bool NeedSelfSpawn(const UWorld* W, const TCHAR* WantName = nullptr);

	// ★ R5-fix2-c (13.11.2026): аргумент `delay=N` — последнее вхождение; без `delay=` возвращается `Def` (то, что стояло в переменной
	//   вызывающего до разбора). Общая точка для самотестов, где `delay=` разбирался веткой своего цикла аргументов вместе с
	//   `quit`/`shot`/`limit=`…: ветка из цикла убрана, значение берётся здесь. Тело — те же `StartsWith` и `Atof`, что были
	//   в циклах; отрицательное значение не обрезается (как и было). Чистая функция.
	inline float DelayArgOr(const TArray<FString>& Args, float Def)
	{
		float D = Def;
		for (const FString& A : Args) { if (A.StartsWith(TEXT("delay="))) { D = FCString::Atof(*A.Mid(6)); } }
		return D;
	}

	// ★ R5-fix-c (13.11.2026): аргумент `delay=N` (секунды до запуска) — последнее вхождение, без `delay=` — 0. Чистая функция.
	//   Общая точка двенадцати дословных копий (`ParseDelay` ×6, `ParseStoreDelay`, `ParseStoreL2..L5Delay`, `ParseShopDblDelay`:
	//   движение 4007, обмен между игроками ×4, личная лавка ×5, двойной щелчок магазина, гейты торговли). Тело — как у копий:
	//   приставка сравнивается без учёта регистра (`FString::StartsWith` по умолчанию), отрицательное значение НЕ обрезается
	//   и признака «аргумент был» нет. Этим она отличается от `FAsdaSelfTestArgs::Parse` (не меньше 0, `bHasDelay`) —
	//   поведение прежних команд сохранено намеренно.
	inline float DelayArg(const TArray<FString>& Args) { return DelayArgOr(Args, 0.f); }
}

// ★ N1(г): RAII «свой персонаж» сетевой подсистемы. При `Capture` запоминает номер (`SelfUserNum`, `bSelfUserNumKnown`),
//   charId (`SelectedCharId`), уровень (`SelfLevel`), вес (`SelfWeightCur`) и смерть (`bSelfDead`); `Restore` (и деструктор)
//   возвращает их. Если персонаж был жив, а самотест оставил его мёртвым, — воскрешение тем же делегатом, что приёмник
//   4055/4056 (`OnCharRevived`: мир снимает позу смерти, меню 25000 закрывается). Строка в лог — только если что-то вернулось.
//   Некопируемый; подсистема держится слабой ссылкой (мир мог уйти).
struct ASDAREBORN_API FAsdaNetSelfScope
{
	FAsdaNetSelfScope() = default;
	// Сразу запомнить состояние `Net` (имя `Who` — для строки лога).
	FAsdaNetSelfScope(UAsdaNetSubsystem* Net, const TCHAR* Who) { Capture(Net, Who); }
	~FAsdaNetSelfScope() { Restore(); }
	FAsdaNetSelfScope(const FAsdaNetSelfScope&) = delete;
	FAsdaNetSelfScope& operator=(const FAsdaNetSelfScope&) = delete;

	// Запомнить состояние (повторный вызов при уже взятом снимке ничего не делает — первое состояние главное).
	void Capture(UAsdaNetSubsystem* Net, const TCHAR* Who);
	// Вернуть запомненное и снять снимок (повторный вызов ничего не делает).
	void Restore();
	// Снимок взят и ещё не возвращён.
	bool IsArmed() const { return bArmed; }

private:
	TWeakObjectPtr<UAsdaNetSubsystem> WNet;   // подсистема сети (слабо: мир мог уйти)
	FString Who;                              // имя самотеста
	bool bArmed = false;                      // снимок взят
	uint16 SelfUserNum = 0;                   // свой userNum
	bool bSelfUserNumKnown = false;           // номер известен
	int32 SelectedCharId = 0;                 // свой charId
	uint8 SelfLevel = 0;                      // свой уровень
	int32 SelfWeightCur = -1;                 // текущий вес
	bool bSelfDead = false;                   // мёртв
};

// RAII ключей консоли: сторож мира (`AsdaCVarSave::ArmWorldGuard`) при создании, `AsdaCVarSave::RestoreAll(Who)` при
//   уничтожении (если реестр не пуст). `Set` — `AsdaCVarSave::SetForTest`.
struct ASDAREBORN_API FAsdaCVarScope
{
	// `W` — мир прогона (для сторожа), `InWho` — имя самотеста (строка `CVAR_RESTORE [Who]`).
	FAsdaCVarScope(const UWorld* W, const TCHAR* InWho);
	~FAsdaCVarScope() { Restore(); }
	FAsdaCVarScope(const FAsdaCVarScope&) = delete;
	FAsdaCVarScope& operator=(const FAsdaCVarScope&) = delete;

	// Поставить ключ на время самотеста (запомнив исходное значение и SetBy).
	void Set(const TCHAR* Name, int32 Value) const { AsdaCVarSave::SetForTest(Name, Value); }
	void Set(const TCHAR* Name, float Value) const { AsdaCVarSave::SetForTest(Name, Value); }
	void Set(const TCHAR* Name, const TCHAR* Value) const { AsdaCVarSave::SetForTest(Name, Value); }
	// Вернуть все ключи сейчас (повторно — ничего).
	void Restore();

private:
	FString Who;          // имя самотеста
	bool bDone = false;   // ключи уже возвращены
};
