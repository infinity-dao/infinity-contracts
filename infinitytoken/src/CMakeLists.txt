project(ifttoken)

set(EOSIO_WASM_OLD_BEHAVIOR "Off")
find_package(eosio.cdt)

add_contract( ifttoken ifttoken ifttoken.cpp )
target_include_directories( ifttoken PUBLIC ${CMAKE_SOURCE_DIR}/../include )
target_ricardian_directory( ifttoken ${CMAKE_SOURCE_DIR}/../ricardian )