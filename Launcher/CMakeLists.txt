# Copyright (c) 2026 @sitxovski (Discord)
# AsdaLauncher — лаунчер AStory Remake: проверка и загрузка обновлений (формат v1), запуск игры через EAC.
# Спецификация: @@ASTORY_DEV_ROOT@@\Reports\updater\FORMAT_v1.md; эталон поведения — AstoryClient/Tools/asda_update_client.py.
#
# Сборка (x64 Native Tools / vcvars64):
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
# Цели: AsdaLauncher.exe (оконный, для игроков) и AsdaLauncherCli.exe (консольный, ТОЛЬКО для тестов: позволяет
# задать источник и корневой ключ; в раздачу не входит).
cmake_minimum_required(VERSION 3.20)
project(AsdaLauncher LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Динамический CRT (/MD): libcrypto.lib из движка собран под /MD. VC++ 2015–2022 Redistributable нужен и самой игре
# (UE Shipping) — установщик (волна 7) ставит его до первого запуска лаунчера.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL$<$<CONFIG:Debug>:Debug>")

# Движок UE: из него берутся OpenSSL 1.1.1t (Ed25519) и RapidJSON (только заголовки)
# Умолчание: переменная окружения ASDA_UE_DIR, иначе D:/Games/UE_5.8 (явный -DUE_ROOT=... главнее; build_cli.bat передаёт его).
if(DEFINED ENV{ASDA_UE_DIR})
  file(TO_CMAKE_PATH "$ENV{ASDA_UE_DIR}" UE_ROOT_DEFAULT)
else()
  set(UE_ROOT_DEFAULT "D:/Games/UE_5.8")
endif()
set(UE_ROOT "${UE_ROOT_DEFAULT}" CACHE PATH "корень Unreal Engine 5.8")
set(UE_TP "${UE_ROOT}/Engine/Source/ThirdParty")
set(OPENSSL_INC "${UE_TP}/OpenSSL/1.1.1t/include/Win64/VS2015")
set(OPENSSL_LIB "${UE_TP}/OpenSSL/1.1.1t/lib/Win64/VS2015/Release/libcrypto.lib")
set(RAPIDJSON_INC "${UE_TP}/RapidJSON/1.1.0")
foreach(p ${OPENSSL_INC} ${OPENSSL_LIB} ${RAPIDJSON_INC})
  if(NOT EXISTS "${p}")
    message(FATAL_ERROR "нет ${p} — укажите -DUE_ROOT=<корень UE 5.8> или переменную окружения ASDA_UE_DIR")
  endif()
endforeach()

# Корневой ключ обновлений (src/Config.h kRootPubHex): 64 hex-символа (32 байта Ed25519). Пока это заглушка configure —
# предупреждение: такой лаунчер соберётся, но откажет в обновлении с понятной ошибкой (python Tools/asda_release.py keys init).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/src/Config.h" ROOT_PUB_LINE REGEX "kRootPubHex[ ]*=")
string(REGEX MATCH "\"([^\"]*)\"" _rp "${ROOT_PUB_LINE}")
set(ROOT_PUB_HEX "${CMAKE_MATCH_1}")
string(LENGTH "${ROOT_PUB_HEX}" ROOT_PUB_LEN)
if(NOT ROOT_PUB_HEX MATCHES "^[0-9a-fA-F]+$" OR NOT ROOT_PUB_LEN EQUAL 64)
  message(WARNING "src/Config.h: kRootPubHex = \"${ROOT_PUB_HEX}\" — не 64 hex-символа. Создайте ключи выпуска "
                  "(python Tools/asda_release.py keys init), впишите hex keys/root.pub в _setup/configure.bat (ASTORY_LAUNCHER_ROOT_PUBKEY) и пересоберите лаунчер.")
endif()

# zstd 1.4.7 — только распаковка (BSD, third_party/zstd)
file(GLOB ZSTD_SRC third_party/zstd/common/*.c third_party/zstd/decompress/*.c)
add_library(zstd_dec STATIC ${ZSTD_SRC})
target_include_directories(zstd_dec PUBLIC third_party/zstd)
target_compile_definitions(zstd_dec PRIVATE ZSTD_NO_INLINE=0 XXH_NAMESPACE=ASDA_)

set(CORE_SRC
  src/Util.cpp src/Crypto.cpp src/Format.cpp src/Http.cpp src/FileSys.cpp src/State.cpp src/Updater.cpp
  src/Launch.cpp src/LogUpload.cpp)
# Ядро в двух вариантах: боевое (только https, свой ключ реестра) и тестовое (папка-источник, http://127.0.0.1)
foreach(v updcore updcore_test)
  add_library(${v} STATIC ${CORE_SRC})
  target_include_directories(${v} PUBLIC src ${OPENSSL_INC} ${RAPIDJSON_INC})
  target_compile_definitions(${v} PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
    RAPIDJSON_HAS_STDSTRING=1)
  target_compile_options(${v} PUBLIC /utf-8 /W4 /permissive- /EHsc)
  target_link_libraries(${v} PUBLIC zstd_dec ${OPENSSL_LIB} bcrypt winhttp crypt32 ws2_32 advapi32 user32 shell32 ole32)
endforeach()
target_compile_definitions(updcore_test PUBLIC ASDA_LAUNCHER_TEST_BUILD=1)

# Оконный лаунчер (для игроков)
add_executable(AsdaLauncher WIN32 src/main_gui.cpp src/Ui.cpp src/Ping.cpp src/SelfUpdate.cpp res/launcher.rc)
target_link_libraries(AsdaLauncher PRIVATE updcore gdiplus dwmapi uxtheme)
target_link_options(AsdaLauncher PRIVATE /MANIFEST:NO)

# Консольный клиент для тестов (сквозной самотест asda_update_selftest.py --client)
add_executable(AsdaLauncherCli src/main_cli.cpp src/Ping.cpp src/SelfUpdate.cpp)
target_link_libraries(AsdaLauncherCli PRIVATE updcore_test)
