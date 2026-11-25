// Copyright (c) 2026 @sitxovski (Discord)
// AsdaLoginPackageList — ОБЩИЙ список пакетов сцены логина (Tools/data/asda_login_packages.json) в C++ (16.09.2026).
//
// ★ Зачем. Список пакетов логина был только у Python (Tools/asda_login_guard.py). C++-сторож защищал один уровень
//   /Game/Map/Login, а ассеты сцены (свет владельца в BP HolyLight_SpotLight, Login_Shine, Shine_Dust, задник,
//   материалы, текстуры, окна UI/Login …) в автоматической сессии никто не держал: rename_asset и пути удаления мимо
//   делегатов их стирали. Теперь один и тот же JSON читают оба: Python — _load_login_packages, C++ — этот модуль.
//
// ★ Разбор — тот же, что в Python (схема и правила: сам JSON, поля «rules»/«groups»;
//   Reports/dedup_audit/tex_rebind_2026-09-15.md, §17.4):
//   - UTF-8 БЕЗ BOM (BOM — ошибка, как у json.load), корень — объект;
//   - format = «asda_login_packages», version = 1;
//   - три массива model_dirs / packages / shared_exceptions; элемент — объект со строкой path («/Game/…», без «*»,
//     «\» и хвостового «/») и строкой match: tree|prefix для первых двух, exact для исключений; повторов нет;
//   - в model_dirs+packages обязана быть запись {/Game/Map/Login, tree}.
//   Любая ошибка чтения/разбора/проверки → FAIL-CLOSED: защищаемым считается ВСЁ под /Game (как в Python).
//
// ★ Сверка пути (после нормализации, без учёта регистра): исключение exact — НЕ логин; tree — путь равен path или
//   начинается с «path/»; prefix — путь начинается с path (прежняя маска «path*»). Иначе — не логин.
//   SubTree — ТОЛЬКО для C++: «path/**» без самого path. Им заменяются записи уровня (/Game/Map/Login и
//   /Game/Map/Login_BuiltData) в сторожевом наборе: сам уровень остаётся за ASDA_ALLOW_LOGIN_MAP_WRITE, а папка с
//   таким же именем — за ASDA_ALLOW_LOGIN_WRITE.
//   ★ 16.09.2026 (ревизия §9, МЕЛК-1): записи папок OFPA уровня (/Game/__ExternalActors__/Map/Login и …Objects…) в
//   сторожевом наборе по-прежнему ВЫБРАСЫВАЮТСЯ (DropPaths) — и сама папка, и всё в ней, и файл-пакет с именем папки
//   держатся набором карты (AsdaLoginSaveGuard::FindProtectedIn, AsdaLoginFileGuard::MakeProtectedFiles). SubTree здесь
//   не годится: он не покрывает сам path и увёл бы содержимое папок OFPA под ASDA_ALLOW_LOGIN_WRITE.
//
// ★ Скорость. Сопоставление — не перебор 1529 пакетов и не перебор записей: деревья, поддеревья, исключения и
//   родительские папки лежат в отсортированных массивах (двоичный поиск), префиксные записи — в массиве, отсортированном
//   по папке-родителю. На путь делается O(глубина пути) двоичных поисков, от размера списка это почти не зависит.
//   Проверка: asda.LoginGuard.SelfTestPackageList, asda.LoginGuard.DumpLoginPackages (сверка с Python по всему Content).
#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"

namespace AsdaLoginPackageList
{
	// Вид записи списка.
	enum class EEntryMatch : uint8
	{
		Tree,     // JSON match=tree: сам пакет и всё под ним
		Prefix,   // JSON match=prefix: префикс имени (прежняя маска «path*»)
		Exact,    // JSON match=exact (shared_exceptions): ровно этот пакет — НЕ логин
		SubTree,  // только C++: всё под path, но не сам path (записи уровня в сторожевом наборе)
	};

	// Одна запись списка (path — как в JSON, регистр сохранён).
	struct FEntry
	{
		FString Path;
		EEntryMatch Match = EEntryMatch::Tree;
	};

	// Разобранный JSON: три группы в прежней нотации Python (LOGIN_OWNED_MODEL_DIRS / LOGIN_OWNED_PACKAGES / LOGIN_SHARED_EXCEPTIONS).
	struct FParsedList
	{
		TArray<FEntry> ModelDirs;
		TArray<FEntry> Packages;
		TArray<FEntry> SharedExceptions;

		// Записей всего (без справочных полей).
		int32 Num() const { return ModelDirs.Num() + Packages.Num() + SharedExceptions.Num(); }
	};

	// Разобрать байты файла. true — разобрано и все проверки пройдены; false — OutError с причиной (текст для лога).
	// Байты, а не строка: BOM и неверный UTF-8 — тоже ошибки (как у Python open(encoding="utf-8") + json.load).
	bool ParseBytes(const TArray<uint8>& Bytes, FParsedList& Out, FString& OutError);

	// Прочитать и разобрать файл (нет файла — тоже ошибка).
	bool LoadFile(const FString& Path, FParsedList& Out, FString& OutError);

	// Принадлежность пути сцене логина.
	enum class EOwnership : uint8
	{
		None,      // не логин
		Owned,     // пакет сцены логина
		Exception, // общий с игрой пакет внутри записей логина (shared_exceptions) — НЕ логин, но причина отдельная
	};

	// Готовый к сверке список. Строится один раз (игровой поток), дальше только читается — потокобезопасен.
	class FMatcher
	{
	public:
		// Построить из разобранного JSON. SubTreeOnly — пути, которые превращаются в SubTree (уровни: сам пакет остаётся
		// за картой), DropPaths — пути, которые выбрасываются совсем (папки OFPA уровня: их целиком держит карта).
		void Build(const FParsedList& Parsed, const TArray<FString>& SubTreeOnly, const TArray<FString>& DropPaths);

		// Fail-closed: список не прочитан — защищаемым считается ВСЁ под /Game (Reason — для лога).
		void SetFailClosed(const FString& Reason);

		// Добавить запись tree (стендовые пакеты ASDA_LOGIN_GUARD_TEST_PACKAGE). Только до Finalize.
		void AddTree(const FString& PackagePath);

		// Отсортировать таблицы (после Build/AddTree).
		void Finalize();

		// Принадлежность пути пакета/объекта/папки (любой регистр, «\» и хвостовой «/» допустимы, «…/Имя.Имя» → пакет).
		EOwnership Classify(FStringView PackagePath) const;

		// То же для УЖЕ приведённого ключа: нижний регистр, «/», без хвостового «/» и без расширения.
		EOwnership ClassifyKey(FStringView LowerKey) const;

		// Есть ли под этой папкой (ключ в том же виде) хотя бы один пакет логина, самой папкой не являющийся.
		bool IsAncestorKey(FStringView LowerKey) const;

		// Список не прочитан (всё под /Game — логин).
		bool IsFailClosed() const { return bFailClosed; }

		// Причина fail-closed.
		const FString& GetFailReason() const { return FailReason; }

		// Папки-родители пакетов логина (нижний регистр, «/game/…») — базы для путей вида «D:хвост» в файловом стороже.
		const TArray<FString>& GetAncestorDirs() const { return AncestorDirs; }

		// Короткое описание для строки установки.
		FString Describe() const;

	private:
		void AddEntry(const FEntry& Entry);
		void AddAncestors(FStringView LowerPath, bool bIncludeSelf);

		TArray<FString> Trees;        // «/game/…» (tree)
		TArray<FString> SubTrees;     // «/game/…» (только содержимое)
		TArray<FString> Exceptions;   // «/game/…» (exact)
		TArray<FString> PrefixParents;// папка-родитель префиксной записи (параллелен PrefixNames, отсортированы вместе)
		TArray<FString> PrefixNames;  // префикс имени в этой папке
		TArray<FString> AncestorDirs; // все папки-родители записей
		bool bFailClosed = false;
		FString FailReason;
		int32 EntryCount = 0;
	};

	// Путь пакета → ключ сверки (нижний регистр, «\»→«/», хвостовой «/» снят, «…/Имя.Имя[:Подобъект]» → пакет).
	void MakeKey(FStringView PackagePath, FStringBuilderBase& OutKey);

	// Путь общего JSON в проекте: <ProjectDir>/Tools/data/asda_login_packages.json.
	FString GetProjectListPath();

	// ★ 16.09.2026 (Reports/dedup_audit/login_writers_scan.md, раздел 9). Общий список для ИНСТРУМЕНТОВ редактора —
	//   в отличие от набора AsdaLoginSaveGuard, который ставится ТОЛЬКО в автоматической сессии и делит пакеты на «уровень»
	//   и «ассеты». Здесь список СЫРОЙ, как его читает Python (записи уровня тоже «логин»): инструменту массовой правки
	//   ассетов уровень нельзя ни при каком разрешении карты. Читается один раз за процесс (лениво, при первом обращении),
	//   живёт до конца процесса; после инициализации только читается — потокобезопасен. Ошибка чтения/разбора →
	//   FAIL-CLOSED: защищаемым считается ВСЁ под /Game (как в Python и у сторожа).
	const FMatcher& GetSharedMatcher();

	// ASDA_ALLOW_LOGIN_WRITE=1 в окружении процесса — единственное разрешение писать ПАКЕТЫ сцены логина.
	bool IsAssetWriteAllowedByEnv();

	// Путь принадлежит сцене логина И разрешения нет ⇒ вызывающий обязан пакет ПРОПУСТИТЬ.
	// Точный аналог Python login_package_blocked (Tools/asda_login_guard.py) для C++-инструментов.
	bool IsLoginPackageBlocked(FStringView PackagePath);
}

#endif // WITH_EDITOR
