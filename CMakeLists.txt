cmake_minimum_required(VERSION 3.12)
project(FixMarketDataSystem)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Find Packages
find_package(quickfix REQUIRED)
find_package(ixwebsocket REQUIRED)
find_package(nlohmann_json REQUIRED)

# MarketDataSimulator
add_executable(MarketDataSimulator MarketDataSimulator/market_data_simulator.cpp)
target_link_libraries(MarketDataSimulator PRIVATE quickfix)

# MarketDataClient
add_executable(MarketDataClient 
    MarketDataClient/main.cpp 
    MarketDataClient/FIXMarketDataApp.h 
    MarketDataClient/OHLCBarAggregator.h
)
target_link_libraries(MarketDataClient PRIVATE 
    quickfix
    ixwebsocket::ixwebsocket
    nlohmann_json::nlohmann_json
)

if(WIN32)
    target_link_libraries(MarketDataClient PRIVATE bcrypt)
endif()

# Copy config files and data dictionaries to output directory
if(WIN32)
    # Get quickfix share directory where XMLs are located
    # Typically in vcpkg_installed/x64-windows/share/quickfix/
    get_target_property(QUICKFIX_INCLUDE_DIR quickfix INTERFACE_INCLUDE_DIRECTORIES)
    set(FIX_SPEC_DIR "${QUICKFIX_INCLUDE_DIR}/../share/quickfix")
    
    add_custom_command(TARGET MarketDataSimulator POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MarketDataSimulator>/log"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MarketDataSimulator>/store"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/MarketDataSimulator/server.cfg"
        "$<TARGET_FILE_DIR:MarketDataSimulator>/server.cfg"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FIX_SPEC_DIR}/FIX44.xml"
        "$<TARGET_FILE_DIR:MarketDataSimulator>/FIX44.xml")
    
    add_custom_command(TARGET MarketDataClient POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MarketDataClient>/log"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MarketDataClient>/store"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MarketDataClient>/ohlc_data"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/MarketDataClient/client.cfg"
        "$<TARGET_FILE_DIR:MarketDataClient>/client.cfg"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FIX_SPEC_DIR}/FIX44.xml"
        "$<TARGET_FILE_DIR:MarketDataClient>/FIX44.xml")
endif()
