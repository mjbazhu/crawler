cmake_minimum_required(VERSION 3.20)
project(demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
enable_testing()

if(MSVC)
    add_compile_options(/MP /utf-8 /EHsc)
endif()

# 固定调试输出目录，便于 VS/LLDB/终端直接定位 exe、pdb、lib 等文件。
set(CRAWLER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/Debug")
foreach(OUTPUT_KIND RUNTIME LIBRARY ARCHIVE PDB COMPILE_PDB)
    set(CMAKE_${OUTPUT_KIND}_OUTPUT_DIRECTORY "${CRAWLER_OUTPUT_DIR}")
    set(CMAKE_${OUTPUT_KIND}_OUTPUT_DIRECTORY_DEBUG "${CRAWLER_OUTPUT_DIR}")
endforeach()

find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Drogon REQUIRED)
find_package(boost_asio CONFIG REQUIRED)
find_package(boost_mysql CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

file(GLOB_RECURSE SRC_FILES
    src/*.cpp
)

list(FILTER SRC_FILES EXCLUDE REGEX "src/main.cpp$")

# 生产源文件只在对象库中编译一次；demo 和测试目标复用这些对象，避免每个 exe 重复编译全量 src。
add_library(crawler_core OBJECT
    ${SRC_FILES}
)

target_include_directories(crawler_core
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(crawler_core
    PUBLIC
    _WIN32_WINNT=0x0A00
)

target_link_libraries(crawler_core
    PUBLIC
    Drogon::Drogon
    Boost::asio
    Boost::mysql
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    unofficial::sqlite3::sqlite3
)

function(configure_crawler_output TARGET_NAME)
    set_target_properties(${TARGET_NAME}
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CRAWLER_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CRAWLER_OUTPUT_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${CRAWLER_OUTPUT_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CRAWLER_OUTPUT_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY "${CRAWLER_OUTPUT_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CRAWLER_OUTPUT_DIR}"
        PDB_OUTPUT_DIRECTORY "${CRAWLER_OUTPUT_DIR}"
        PDB_OUTPUT_DIRECTORY_DEBUG "${CRAWLER_OUTPUT_DIR}"
        COMPILE_PDB_OUTPUT_DIRECTORY "${CRAWLER_OUTPUT_DIR}"
        COMPILE_PDB_OUTPUT_DIRECTORY_DEBUG "${CRAWLER_OUTPUT_DIR}"
    )
endfunction()

function(configure_crawler_executable TARGET_NAME)
    target_link_libraries(${TARGET_NAME}
        PRIVATE
        crawler_core
    )
    configure_crawler_output(${TARGET_NAME})
endfunction()

function(add_crawler_test TARGET_NAME SOURCE_FILE)
    add_executable(${TARGET_NAME}
        ${SOURCE_FILE}
    )
    configure_crawler_executable(${TARGET_NAME})
    target_include_directories(${TARGET_NAME}
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
    set_tests_properties(${TARGET_NAME}
        PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endfunction()

function(disable_msvc_debug_artifacts TARGET_NAME)
    target_link_options(${TARGET_NAME}
        PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/DEBUG:NONE>
        $<$<CXX_COMPILER_ID:MSVC>:/PDB:NONE>
        $<$<CXX_COMPILER_ID:MSVC>:/INCREMENTAL:NO>
    )
endfunction()

add_executable(demo
    src/main.cpp
)
configure_crawler_executable(demo)

add_crawler_test(MiguSingerSongIngestionTest tests/migu/ingestion/MiguSingerSongIngestionTest.cpp)
set_tests_properties(MiguSingerSongIngestionTest
    PROPERTIES
    ENVIRONMENT "CRAWLER_OUTBOUND_VERIFY_TLS=false"
)

add_crawler_test(MiguSingerIndexIngestionTest tests/migu/ingestion/MiguSingerIndexIngestionTest.cpp)
set_tests_properties(MiguSingerIndexIngestionTest
    PROPERTIES
    ENVIRONMENT "CRAWLER_OUTBOUND_VERIFY_TLS=false"
)

add_crawler_test(MiguSingerAlbumIngestionTest tests/migu/ingestion/MiguSingerAlbumIngestionTest.cpp)
set_tests_properties(MiguSingerAlbumIngestionTest
    PROPERTIES
    ENVIRONMENT "CRAWLER_OUTBOUND_VERIFY_TLS=false"
)

add_crawler_test(MiguProviderTest tests/migu/provider/MiguProviderTest.cpp)
add_crawler_test(MiguApiDocumentTest tests/migu/api/MiguApiDocumentTest.cpp)
add_crawler_test(MiguCatalogApiSmokeTest tests/migu/api/MiguCatalogApiSmokeTest.cpp)
add_crawler_test(MiguHttpRouteSmokeTest tests/migu/api/MiguHttpRouteSmokeTest.cpp)
add_crawler_test(MiguCatalogQueryIntegrationTest tests/migu/integration/MiguCatalogQueryIntegrationTest.cpp)
add_crawler_test(MiguBackfillClaimIntegrationTest tests/migu/integration/MiguBackfillClaimIntegrationTest.cpp)
add_crawler_test(MiguMetadataRecoveryIntegrationTest tests/migu/integration/MiguMetadataRecoveryIntegrationTest.cpp)
add_crawler_test(MiguSongRelationIntegrationTest tests/migu/integration/MiguSongRelationIntegrationTest.cpp)
add_executable(MiguSingerSongTableIntegrityProbe tests/migu/integration/MiguSingerSongTableIntegrityProbe.cpp)
configure_crawler_executable(MiguSingerSongTableIntegrityProbe)
target_include_directories(MiguSingerSongTableIntegrityProbe
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
add_crawler_test(MiguSingerIndexParserTest tests/migu/parser/MiguSingerIndexParserTest.cpp)
add_crawler_test(MiguAlbumSourceParserTest tests/migu/parser/MiguAlbumSourceParserTest.cpp)
add_crawler_test(MiguSongResourceParserTest tests/migu/parser/MiguSongResourceParserTest.cpp)
add_crawler_test(SongResourceDownloadStorageTest tests/migu/resource/SongResourceDownloadStorageTest.cpp)
add_crawler_test(SongResourceDownloadWorkflowTest tests/migu/flow/SongResourceDownloadWorkflowTest.cpp)
add_crawler_test(CrawlerServiceResourceBatchEventTest tests/migu/service/CrawlerServiceResourceBatchEventTest.cpp)
add_crawler_test(AsyncTaskTest tests/async/AsyncTaskTest.cpp)
add_crawler_test(LoggerTest tests/infrastructure/logging/LoggerTest.cpp)
add_crawler_test(AsyncSongResourceDownloadWorkflowTest tests/migu/flow/AsyncSongResourceDownloadWorkflowTest.cpp)
add_crawler_test(AsyncHttpStreamTest tests/infrastructure/http/AsyncHttpStreamTest.cpp)
add_crawler_test(AsyncFileBlobStoreTest tests/migu/resource/AsyncFileBlobStoreTest.cpp)

add_crawler_test(MiguSongResourceIngestionTest tests/migu/ingestion/MiguSongResourceIngestionTest.cpp)
disable_msvc_debug_artifacts(MiguSongResourceIngestionTest)
set_tests_properties(MiguSongResourceIngestionTest
    PROPERTIES
    ENVIRONMENT "CRAWLER_OUTBOUND_VERIFY_TLS=false"
)

add_crawler_test(MiguSongResourceBatchIngestionTest tests/migu/ingestion/MiguSongResourceBatchIngestionTest.cpp)
disable_msvc_debug_artifacts(MiguSongResourceBatchIngestionTest)
set_tests_properties(MiguSongResourceBatchIngestionTest
    PROPERTIES
    ENVIRONMENT "CRAWLER_OUTBOUND_VERIFY_TLS=false;CRAWLER_TEST_MIGU_RESOURCE_SONG_LIMIT=10;CRAWLER_TEST_MIGU_RESOURCE_FORMAT=ZQ24;CRAWLER_TEST_MIGU_RESOURCE_PER_SONG_LIMIT=1"
)

add_crawler_test(MiguAsyncSongResourceBatchIngestionTest tests/migu/ingestion/MiguAsyncSongResourceBatchIngestionTest.cpp)
disable_msvc_debug_artifacts(MiguAsyncSongResourceBatchIngestionTest)
set_tests_properties(MiguAsyncSongResourceBatchIngestionTest
    PROPERTIES
    ENVIRONMENT "CRAWLER_OUTBOUND_VERIFY_TLS=false;CRAWLER_TEST_MIGU_ASYNC_RESOURCE_LIMIT=3;CRAWLER_TEST_MIGU_ASYNC_RESOURCE_FORMAT=ZQ24"
)

add_crawler_test(MiguPacmtokenConcurrencyProbeTest tests/migu/ingestion/MiguPacmtokenConcurrencyProbeTest.cpp)
disable_msvc_debug_artifacts(MiguPacmtokenConcurrencyProbeTest)
set_tests_properties(MiguPacmtokenConcurrencyProbeTest
    PROPERTIES
    ENVIRONMENT "CRAWLER_OUTBOUND_VERIFY_TLS=false;CRAWLER_TEST_MIGU_TOKEN_PROBE_TOKENS=3;CRAWLER_TEST_MIGU_TOKEN_PROBE_FORMAT=PQ;CRAWLER_TEST_MIGU_TOKEN_PROBE_PER_TOKEN_LIMIT=5;CRAWLER_TEST_MIGU_TOKEN_PROBE_PER_TOKEN_CONCURRENCY=3;CRAWLER_TEST_MIGU_TOKEN_PROBE_MIXED_LIMIT=15;CRAWLER_TEST_MIGU_TOKEN_PROBE_MIXED_CONCURRENCY=5"
)

add_crawler_test(MiguSingerIndexIngestionFlowTest tests/migu/flow/MiguSingerIndexIngestionFlowTest.cpp)
add_crawler_test(MiguSingerAlbumIngestionFlowTest tests/migu/flow/MiguSingerAlbumIngestionFlowTest.cpp)
