// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEacDiagLog.h — диагностический журнал клиентского EAC, работающий и в Shipping (живая проверка 16.10.2026).
//
// ЗАЧЕМ. Shipping собран с bUseLoggingInShipping=false — UE_LOG там вырезан, и по живому прогону через
//   start_protected_game.exe не видно, на каком шаге модуль остановился (политика, DLL, EOS, AsdaId, 15901).
//   Этот журнал пишет те же строки [EAC] в отдельный файл в обход UE_LOG.
//
// ГДЕ ФАЙЛ. FPaths::ProjectSavedDir()/Logs/AsdaEac.log:
//   - упакованная Shipping-сборка (FApp::IsInstalled → ShouldSaveToUserDir): %LOCALAPPDATA%\AsdaReborn\Saved\Logs\AsdaEac.log;
//   - редактор / Development из проекта: AstoryClient/Saved/Logs/AsdaEac.log.
//   Ротация по размеру: > 1 МБ → AsdaEac.log → AsdaEac.1.log → AsdaEac.2.log (всего 3 файла). Запуски дописываются в конец.
//
// ★ РИТЕЙЛ (решение владельца 16.10.2026): в Shipping (и в Development с -AsdaLogSeal) журнал пишется ТОЛЬКО запечатанным —
//   AsdaEac.alog (+ AsdaEac.1.alog, AsdaEac.2.alog) и AsdaEac_raw.alog: сегмент AsdaLogSeal на каждое открытие файла (ECDH P-256
//   к нашему открытому ключу + AES-256-GCM, Source/AsdaNet/Public/AsdaLogSeal.h). Не вышло запечатать — строка отбрасывается,
//   открытым текстом не пишется ничего. Расшифровка — python Tools/asda_logseal.py decrypt <файл>. Development без ключа — как раньше.
//
// КАК ПИШЕТ. Строки ставятся в очередь (MPSC) из любого потока; на диск их пишет отдельная нить (раз в 200 мс или по
//   сигналу), игровой поток диск не ждёт. Без многопоточности (-nothreading) — запись на месте под критической секцией.
//
// СЕКРЕТЫ. Сюда НЕ пишутся ClientSecret, пароль, MD5, токены (JWT/refresh/IdToken), PUID целиком (только MaskId — 6 символов),
//   логин (только MaskLogin). Строки лога EOS SDK, которые мы не контролируем, проходят SanitizeForeign (маскирует длинные
//   «токеноподобные» последовательности). Вызывающий обязан передавать только длины/коды/маски.
#pragma once

#include "CoreMinimal.h"

class FAsdaEacDiag
{
public:
	// Записать строку журнала. Level — короткая метка уровня («Log», «Warning», «Error», «Verbose»). Потокобезопасно.
	static void Write(const TCHAR* Level, const FString& Line);
	// Записать строку из стороннего источника (лог EOS SDK): перед записью маскируются длинные токеноподобные последовательности.
	static void WriteForeign(const TCHAR* Level, const FString& Line);
	// Сбросить очередь на диск и остановить нить записи (ShutdownModule). Повторный Write после этого пишет синхронно.
	static void Shutdown();
	// ★ Живая проверка 16.10: строка прямым Win32 CreateFileW в %LOCALAPPDATA%\AsdaReborn\AsdaEac_raw.log (мимо файлового
	//   слоя UE) — метка «код модуля выполняется» и запасной канал, если AsdaEac.log не открылся. Без секретов.
	static void RawMark(const FString& Line);
	// Полный путь текущего файла журнала (для строки в UE-логе и отчёта).
	static FString GetFilePath();

	// Маска идентификатора (PUID и т. п.): первые 6 символов + «…» + длина. Пусто → «—».
	static FString MaskId(const FString& Id);
	// Маска логина: первые 2 символа + «***» + длина. Пусто → «—».
	static FString MaskLogin(const FString& Login);
	// Замаскировать в строке длинные (≥ 20) последовательности символов [A-Za-z0-9_-+/=] — вероятные токены/секреты/PUID.
	static FString SanitizeForeign(const FString& Line);
};
