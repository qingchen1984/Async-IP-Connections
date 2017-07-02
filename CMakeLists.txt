cmake_minimum_required( VERSION 2.8 )
project( Async-IP-Connections )

set( CMAKE_C_STANDARD 99 )
set( CMAKE_C_STANDARD_REQUIRED ON )

include_directories( ${CMAKE_CURRENT_LIST_DIR} )

set( LIBRARY_DIR CACHE PATH "Relative or absolute path to directory where built shared libraries will be placed" )

set( USE_IP_LEGACY FALSE CACHE BOOL "Enable to compile for older systems, with no modern socket options (e.g. IPv6)" )

include( ${CMAKE_CURRENT_LIST_DIR}/threads/CMakeLists.txt )

add_library( AsyncIPConnections SHARED ${CMAKE_CURRENT_LIST_DIR}/ip_network.c ${CMAKE_CURRENT_LIST_DIR}/async_ip_network.c)
set_target_properties( AsyncIPConnections PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${LIBRARY_DIR} )
target_link_libraries( AsyncIPConnections MultiThreading )
if( UNIX AND NOT APPLE )
  target_link_libraries( AsyncIPConnections -lrt )
endif()
target_compile_definitions( AsyncIPConnections PUBLIC -D_DEFAULT_SOURCE=__STRICT_ANSI__ -DDEBUG )
if( USE_IP_LEGACY )
  target_compile_definitions( AsyncIPConnections -DIP_NETWORK_LEGACY )
endif()

