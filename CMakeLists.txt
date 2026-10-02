cmake_minimum_required(VERSION 3.18)
project(facet-telegram-dm-notif VERSION 0.2.0 LANGUAGES CXX)
set(TG_VERSION_SUFFIX "alpha")  # pre-release tag; empty for final releases; keep manifest.json in sync

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(TG_STATIC "Link fully static (portable release binaries)" OFF)

if(TG_VERSION_SUFFIX)
    set(TG_VERSION "${PROJECT_VERSION}-${TG_VERSION_SUFFIX}")
else()
    set(TG_VERSION "${PROJECT_VERSION}")
endif()

include(FetchContent)

# The plugin SDK lives in facet-core: a local checkout next to this project
# (or FACET_CORE_DIR) is used when present, otherwise it is downloaded.
set(FACET_CORE_DIR "" CACHE PATH "facet-core checkout providing the SDK (default: ../facet-core, else download)")
set(FACET_CORE_REF "v0.4.0-alpha" CACHE STRING "facet-core git ref to download the SDK from")
if(NOT FACET_CORE_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../facet-core/sdk/CMakeLists.txt")
    set(FACET_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../facet-core")
endif()
if(FACET_CORE_DIR)
    add_subdirectory(${FACET_CORE_DIR}/sdk ${CMAKE_BINARY_DIR}/facet_sdk)
else()
    FetchContent_Declare(facet_core
        GIT_REPOSITORY https://github.com/siakinnik/facet-core.git
        GIT_TAG ${FACET_CORE_REF}
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR sdk)
    FetchContent_MakeAvailable(facet_core)
endif()

# TDLib, Telegram's official client library, lives inside this project: the
# first configure builds the commit pinned in TDLIB_COMMIT into
# third_party/tdlib (20-60 minutes, once) and it is linked statically, so the
# plugin needs nothing outside itself. System-wide TDLib installs are ignored.
set(TG_TDLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tdlib" CACHE PATH "TDLib install prefix owned by this plugin")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/TDLIB_COMMIT" TDLIB_COMMIT)
string(STRIP "${TDLIB_COMMIT}" TDLIB_COMMIT)
set(TDLIB_BUILT "")
if(EXISTS "${TG_TDLIB_DIR}/.tdlib-commit")
    file(READ "${TG_TDLIB_DIR}/.tdlib-commit" TDLIB_BUILT)
    string(STRIP "${TDLIB_BUILT}" TDLIB_BUILT)
endif()
if(NOT TDLIB_BUILT STREQUAL TDLIB_COMMIT)
    message(STATUS "Building TDLib ${TDLIB_COMMIT} into ${TG_TDLIB_DIR} (once, takes a while)")
    execute_process(COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build-tdlib.sh" "${TG_TDLIB_DIR}"
                    RESULT_VARIABLE tdlib_result)
    if(NOT tdlib_result EQUAL 0)
        message(FATAL_ERROR "TDLib build failed (see above)")
    endif()
endif()
find_package(Td CONFIG REQUIRED PATHS "${TG_TDLIB_DIR}" NO_DEFAULT_PATH)
message(STATUS "TDLib ${Td_VERSION} from ${Td_DIR}")

# QR codes for signing in with the phone (MIT, header + one source file).
FetchContent_Declare(qrcodegen
    GIT_REPOSITORY https://github.com/nayuki/QR-Code-generator.git
    GIT_TAG v1.8.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(qrcodegen)
add_library(qrcodegen STATIC ${qrcodegen_SOURCE_DIR}/cpp/qrcodegen.cpp)
target_include_directories(qrcodegen PUBLIC ${qrcodegen_SOURCE_DIR}/cpp)

add_library(tg_inbox STATIC src/inbox.cpp)
target_include_directories(tg_inbox PUBLIC src)
target_link_libraries(tg_inbox PUBLIC facet_sdk)
target_compile_options(tg_inbox PRIVATE -Wall -Wextra)

add_executable(telegram-dm-notif src/main.cpp src/td_client.cpp src/i18n/ru.cpp)
target_link_libraries(telegram-dm-notif PRIVATE tg_inbox qrcodegen Td::TdJsonStatic pthread)
target_compile_options(telegram-dm-notif PRIVATE -Wall -Wextra)
target_compile_definitions(telegram-dm-notif PRIVATE TG_VERSION="${TG_VERSION}")
if(TG_STATIC)
    target_link_options(telegram-dm-notif PRIVATE -static -s)
endif()

# The build dir doubles as a plugin dir: FACET_PLUGIN_PATH=<build>
set(PLUGIN_OUT ${CMAKE_BINARY_DIR}/telegram-dm-notif.plugin)
set_target_properties(telegram-dm-notif PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PLUGIN_OUT})
facet_plugin_manifest(${CMAKE_CURRENT_SOURCE_DIR}/manifest.json ${PLUGIN_OUT}/manifest.json)

enable_testing()
add_executable(inbox_test tests/inbox_test.cpp)
target_link_libraries(inbox_test PRIVATE tg_inbox)
add_test(NAME inbox COMMAND inbox_test)

install(TARGETS telegram-dm-notif RUNTIME DESTINATION lib/facet/plugins/telegram-dm-notif)
install(FILES ${PLUGIN_OUT}/manifest.json DESTINATION lib/facet/plugins/telegram-dm-notif)
