// Copyright (c) 2026 @sitxovski (Discord)
// AsdaItemBrowserDebug — headless-самопроверка браузера (вызывается из Python/Blueprint в редакторе).
// Ничего не рисует: грузит те же DataTable-ассеты, строит дерево категорий и печатает разрешение
// «предмет → модель / текстура / иконка / эффект» для заданных ID.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AsdaItemBrowserDebug.generated.h"

UCLASS()
class UAsdaItemBrowserDebug : public UObject
{
	GENERATED_BODY()

public:
	// Сводка по дереву категорий (тип/подтипы/количества). Возвращает текст, он же уходит в лог.
	UFUNCTION(BlueprintCallable, Category = "Asda|ItemBrowser")
	static FString DumpTree();

	// Что покажет дерево при заданном поиске — ровно то же, что видит панель.
	//   Space: −1 = все три таблицы, 0 = item_list, 1 = sowell_list, 2 = special_itemlist.
	// Нужна для проверки главного правила: число трактуется как КОД и точные совпадения выносятся
	// отдельной веткой с указанием таблицы (иначе 461 из special_itemlist тонет среди 24610 и т.п.).
	UFUNCTION(BlueprintCallable, Category = "Asda|ItemBrowser")
	static FString DumpSearch(const FString& Filter, int32 Space = -1, int32 MaxItemsPerBranch = 12);

	// Разбор конкретных предметов. Enhance: 0 = без заточки, 5/6/7 — ступени свечения.
	UFUNCTION(BlueprintCallable, Category = "Asda|ItemBrowser")
	static FString DumpItems(const TArray<int32>& ItemCodes, int32 Enhance = 7, bool bMale = true);

	// Пробег по ВСЕЙ базе: сколько предметов с моделью/текстурой/иконкой/эффектом (медленно, ~минута).
	UFUNCTION(BlueprintCallable, Category = "Asda|ItemBrowser")
	static FString AuditAll(int32 MaxItems = 0);

	// Автоориентация превью: габариты меша, выбранная продольная ось и итоговый доворот.
	UFUNCTION(BlueprintCallable, Category = "Asda|ItemBrowser")
	static FString DumpOrientation(const TArray<int32>& ItemCodes, bool bMale = true);

	// Спавн эффекта предмета в сцене превью (без Slate): проверяет, что BP грузится, строит
	// геометрию при прогреве и продолжает симулироваться при тике мира превью.
	UFUNCTION(BlueprintCallable, Category = "Asda|ItemBrowser")
	static FString TestFxSpawn(int32 ItemCode, int32 Enhance = 7);

	// ★ 13.09.2026: самопроверка «Конфигуратора свечения» БЕЗ Slate — тем же слоем данных, что вкладка:
	//   дерево (сколько семейств/предметов), правка «предмет + эффект» (сила света и сила свечения) или снятие записи,
	//   «Сохранить» настоящим путём (Python → JSON → ini → --check → перечитать CDO) и чтение того, что действует в CDO.
	UFUNCTION(BlueprintCallable, Category = "Asda|ItemBrowser")
	static FString TestGlowConfig(const FString& Jeb, int32 ItemCode, float IntensityCd, float EmissiveBoost, bool bRemove);

	// ★★★ ВОЛНЫ Р0 и Р1 (20.09.2026): самопроверка РЕДАКТОРСКОЙ части конфигуратора — БЕЗ Slate и
	//   БЕЗ ЕДИНОЙ ЗАПИСИ НА ДИСК (ни JSON, ни ini, ни .uasset не пишутся).
	//   ★★★ ПОПРАВКА РЕВИЗИИ Ф12 (К3): ЗДЕСЬ СТОЯЛО «карты проекта не грузятся» — И ЭТО ВВОДИЛО В
	//     ЗАБЛУЖДЕНИЕ. Сама функция карт не грузит, но прогон 20.09 06:05 шёл через
	//     -ExecutePythonScript, то есть через полный редактор, и тот загрузил стартовую карту сам:
	//     Reports/headless_glow_editor_ui.log:1892 «MAP LOAD FILE=…/Content/Map/Login.umap».
	//     Login.umap защищён (owner-login-light-protected), грузить его нельзя даже на чтение.
	//     ПРАВИЛЬНО: запускать ТОЛЬКО коммандлетом `-run=pythonscript` (карты не грузятся вовсе),
	//     со своим `-saveddirsuffix` и копиями профилей владельца. Функция, вызванная из живого
	//     редактора владельца, картами распоряжаться не может — поэтому РАЗБОР внутри ограничен
	//     акторами своего временного мира, а число «чужих» экземпляров печатается отдельно.
	//   Печатает: разделы полей и сколько полей в каждом; поля, ушедшие в «Служебное» и в «только
	//   чтение»; какие поля работают, а какие спрятаны при заданной ФОРМЕ (проверка разбора meta
	//   EditCondition штатным вычислителем движка); подписи без номеров и поправленные имена; корень
	//   «Эффекты карт и навыков» и список эмиттеров эффекта.
	//   ★ РЕВИЗИЯ Ф11: добавлен разбор поэмиттерной правки (К1/К2/К3/С8) — он В ПАМЯТИ правит CDO
	//     одного эффекта и потому НА ВРЕМЯ помечает его пакет изменённым. Экземпляры создаются в СВОЁМ
	//     временном мире (UWorld::CreateWorld), проверка идёт парой «подопытный + контрольный», а в
	//     конце всё возвращается к исходному и признак «изменён» снимается, если его не было до теста.
	//   Jeb — имя .jeb эффекта, у которого разбираются эмиттеры (пусто = первый эффект реестра).
	UFUNCTION(BlueprintCallable, Category = "Asda|ItemBrowser")
	static FString TestGlowEditorUi(const FString& Jeb);

	// ★★★ 29.10.2026 — ПЕРЕДЕЛКА ПРОСМОТРЩИКА (Reports/item_viewer/REPORT.md).
	// Счётчики разбивки: категории → подкатегории, ветки классов, разряды уровня, грейды, премиум (текст и лог).
	UFUNCTION(BlueprintCallable, Category = "Asda|ItemBrowser")
	static FString DumpBreakdown();
	// Текст игровой подсказки предмета RU и EN (теги сняты) тем же построителем, что карточка.
	//   CardKind 0 авто / 1 полный / 2 простой; ViewerJob — профессия 0…44, ViewerLevel — уровень зрителя (0 — не задан).
	UFUNCTION(BlueprintCallable, Category = "Asda|ItemBrowser")
	static FString DumpCard(int32 ItemCode, int32 CardKind = 0, int32 ViewerJob = 0, int32 ViewerLevel = 0);
	// Кадр панели просмотрщика в PNG (виджет рисуется в render target, ОКНА НЕТ): фильтры как у человека, выбран ItemCode
	//   (0 — без выбора). Возвращает сводку: сколько прошло фильтр, путь кадра. Space 0 все/1/2/3; Cat/Job/Grade −1 — любой;
	//   JobMode 0/1/2; Premium −1/0/1; Grouping 0…3; ExpandDepth — до какой глубины раскрыть дерево (−1 — как панель сама).
	UFUNCTION(BlueprintCallable, Category = "Asda|ItemBrowser")
	static FString RenderViewer(const FString& PngPath, int32 ItemCode, const FString& Search, int32 Space, int32 Cat,
		int32 Job, int32 JobMode, int32 LvFrom, int32 LvTo, int32 CharLevel, int32 Grade, int32 Premium, int32 Grouping,
		int32 CardKind, int32 ViewerJob, int32 ViewerLevel, int32 ExpandDepth = -1, int32 Width = 1900, int32 Height = 1200);
};
