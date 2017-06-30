cmake_minimum_required( VERSION 2.8 )
project( Platform-Utils )

set( CMAKE_C_STANDARD 99 )
set( CMAKE_C_STANDARD_REQUIRED ON )

include_directories( ${CMAKE_CURRENT_LIST_DIR} )

set( LIBRARY_DIR CACHE PATH "Relative or absolute path to directory where built shared libraries will be placed" )
set( MODULES_DIR CACHE PATH "Relative path to directory where built modules/plugins will be placed and loaded" )

find_package( Threads REQUIRED )
  
set( PLATFORM_SOURCES )
if( UNIX )
  list( APPEND PLATFORM_SOURCES ${CMAKE_CURRENT_LIST_DIR}/shared_memory/shm_unix.c ${CMAKE_CURRENT_LIST_DIR}/threading/threads_unix.c ${CMAKE_CURRENT_LIST_DIR}/timing/timing_unix.c )
elseif( WIN32 )
  list( APPEND PLATFORM_SOURCES ${CMAKE_CURRENT_LIST_DIR}/shared_memory/shm_windows.c ${CMAKE_CURRENT_LIST_DIR}/threading/threads_windows.c ${CMAKE_CURRENT_LIST_DIR}/timing/timing_windows.c )
endif()

set( DATA_IO_MODULES_PATH ${MODULES_DIR}/${DATA_IO_SUBDIR} )

add_library( PlatformUtils SHARED ${CMAKE_CURRENT_LIST_DIR}/data_io.c ${CMAKE_CURRENT_LIST_DIR}/debug/data_logging.c ${CMAKE_CURRENT_LIST_DIR}/ip_network.c ${CMAKE_CURRENT_LIST_DIR}/async_ip_network.c ${CMAKE_CURRENT_LIST_DIR}/thread_safe_data.c ${PLATFORM_SOURCES} )
set_target_properties( PlatformUtils PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${LIBRARY_DIR} )
target_link_libraries( PlatformUtils ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT} )
if( UNIX AND NOT APPLE )
  target_link_libraries( PlatformUtils -lrt )
endif()
target_compile_definitions( PlatformUtils PUBLIC -D_DEFAULT_SOURCE=__STRICT_ANSI__ -DDEBUG -DIP_NETWORK_LEGACY -DDATA_IO_MODULES_PATH=${DATA_IO_MODULES_PATH} )

add_library( JSON MODULE ${CMAKE_CURRENT_LIST_DIR}/data_io/json_io.c ${CMAKE_CURRENT_LIST_DIR}/data_io/json.c )
set_target_properties( JSON PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${DATA_IO_MODULES_PATH} )
set_target_properties( JSON PROPERTIES PREFIX "" )
target_compile_definitions( JSON PUBLIC -DDEBUG )
