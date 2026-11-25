// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ [ПОРТ] 09.10.2026 ОКНО ПЕРВОГО ВЫБОРА ЯЗЫКА (мультиязычность, решения владельца 09.10.2026).
//   В оригинале переключателя языка нет — окно наше. Показывается на экране логина ОДИН раз — пока в профиле игрока
//   (`UAsdaGameUserSettings::Language`) нет выбора и язык сессии не задан командной строкой (`-AsdaLang` / `-culture`).
//   Три кнопки столбцом (разметка V3, 11.10.2026): сверху «English», ниже «Русский», внизу «العربية» — неактивна
//   (арабский пока не делаем).
//   Выбор пишется в профиль. Совпал с языком сессии (до выбора это язык системы) — окно просто закрывается; иначе язык
//   ★ 10.10.2026 (требование владельца) применяется СРАЗУ, без перезапуска: `FAsdaLang::ApplyLive` (культура, подписи,
//   кэши текста) + перезагрузка уровня логина — атласы с надписями движок подменяет при ЗАГРУЗКЕ пакета, поэтому экран
//   логина пересоздаётся, а пакеты с L10N-вариантом (они отпущены из кэша текстур) грузятся заново уже нужной культуры.
//   Прежде здесь был перезапуск процесса (`FAsdaLang::RestartGame`). Окно показывается и в PIE (язык не выбран в
//   профиле редактора); в PIE текстуры L10N движок не подменяет вовсе — там на лету меняется только текст.
//   Разметка окна — ассет `/Game/AsdaAssets/UI/CommonWin/WBP_LANG_SELECT` (не в UI/Login — там сторож сцены логина) (клон нейтрального окна выбора
//   `WBP_MESSAGE2_WIN_NEUTRAL` + третья кнопка; шаг `Tools/asda_ui_lang_select_window.py`). Код ставит только подписи
//   из loc 990350…990353 и обработчики — разметку не создаёт (закон no-runtime-widget-fixes).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Text/AsdaLang.h"
#include "AsdaLangSelectSubsystem.generated.h"

class UUserWidget;
class UAsdaButtonRelay;

UCLASS()
class ASDAREBORN_API UAsdaLangSelectSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Путь класса окна и его id в оконной подсистеме.
	static const TCHAR* const kWidgetPath;
	static const FName kWindowId;

	// Старт подсистемы (вместе с экземпляром игры, до первых окон): таблица подписей окон — на язык сессии
	//   (`FAsdaLang::ApplyCaptionTable`).
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// ★ 12.10.2026: язык ещё не выбран (ни в профиле, ни командной строкой) — окно первого выбора понадобится. Режим логина
	//   по этому признаку откладывает экран логина, а само окно открывает после снятия стартовой заставки
	//   (`AAsdaLoginGameMode::TickStartLogo`), чтобы оно не просвечивало сквозь её растворение (замечание владельца).
	bool NeedsFirstRun() const { return !FAsdaLang::IsChosen(); }
	// Показать окно, если язык ещё не выбран (★ 10.10.2026: и в PIE). true — окно показано, экран логина вызывающий
	//   откладывает до выбора (после выбора уровень логина перезагружается).
	bool ShowFirstRunIfNeeded();
	// Показать окно безусловно (самотест). bNoRestart — выбор не перезапускает игру (для проверки без выхода).
	UUserWidget* ShowWindow(bool bNoRestart);
	// Обработать выбор языка (то же, что нажатие кнопки): записать профиль, закрыть окно, при смене — применить на
	//   лету и пересоздать экран логина (`ApplyLiveAndReloadLogin`).
	void Pick(EAsdaLang L);
	// Применить язык на лету (если отличается от языка сессии) и перезагрузить текущий уровень (экран логина):
	//   культура/подписи/кэши текста — сразу, пакеты с L10N-вариантом отпускаются из кэша текстур и грузятся заново
	//   после перезагрузки. false — нет мира.
	bool ApplyLiveAndReloadLogin(EAsdaLang L);
	// Забрать признак «уровень перезагружен ради языка» (тогда стартовая заставка второй раз не показывается).
	bool ConsumeSkipStartLogo();
	// Окно сейчас открыто.
	bool IsOpen() const;

	// Подписи и кнопки окна (имена узлов клона `WBP_MESSAGE2_WIN_NEUTRAL`): текст, «English», «Русский», «العربية».
	static const TCHAR* const kTextNode;
	static const TCHAR* const kBtnEn;
	static const TCHAR* const kBtnRu;
	static const TCHAR* const kBtnAr;

	// Последний выбор в этом процессе (для самотеста); пока не выбирали — false.
	bool bPicked = false;
	EAsdaLang LastPick = EAsdaLang::Ru;
	// Был бы перезапуск (выбор ≠ языку сессии) — для самотеста при bNoRestart.
	bool bLastWouldRestart = false;
	// Сколько раз язык применён на лету в этом экземпляре игры и имя мира, который при этом перезагружался (самотест).
	int32 LiveApplyCount = 0;
	FString LiveReloadedWorld;
	// Мир, в котором язык применён на лету последний раз (самотест проверяет, что текущий мир — новый).
	TWeakObjectPtr<UWorld> LiveOldWorld;

private:
	// Реле кнопок (живут, пока открыто окно).
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAsdaButtonRelay>> Relays;
	// Перезапуск подавлен (режим самотеста).
	bool bSuppressRestart = false;
	// Уровень перезагружается ради языка — стартовую заставку при новом `StartPlay` логина не показывать.
	bool bSkipStartLogoOnce = false;
	// Окно первого выбора показано ДО экрана логина (он отложен) — после выбора уровень логина перезагружается всегда,
	//   даже если язык совпал с языком сессии (иначе экрана логина не будет).
	bool bLoginDeferred = false;
};
