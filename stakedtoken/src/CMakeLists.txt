project(stakedtoken)

set(EOSIO_WASM_OLD_BEHAVIOR "Off")
find_package(eosio.cdt)

add_contract( stakedtoken stakedtoken stakedtoken.cpp )
target_include_directories( stakedtoken PUBLIC ${CMAKE_SOURCE_DIR}/../include )
target_ricardian_directory( stakedtoken ${CMAKE_SOURCE_DIR}/../ricardian )