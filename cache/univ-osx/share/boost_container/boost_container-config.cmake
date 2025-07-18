# Generated by BoostInstall.cmake for boost_container-1.87.0

if(Boost_VERBOSE OR Boost_DEBUG)
  message(STATUS "Found boost_container ${boost_container_VERSION} at ${boost_container_DIR}")
endif()

include(CMakeFindDependencyMacro)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_dependency(Threads)
if(NOT boost_assert_FOUND)
  find_dependency(boost_assert 1.87.0 EXACT HINTS "${CMAKE_CURRENT_LIST_DIR}/..")
endif()
if(NOT boost_config_FOUND)
  find_dependency(boost_config 1.87.0 EXACT HINTS "${CMAKE_CURRENT_LIST_DIR}/..")
endif()
if(NOT boost_intrusive_FOUND)
  find_dependency(boost_intrusive 1.87.0 EXACT HINTS "${CMAKE_CURRENT_LIST_DIR}/..")
endif()
if(NOT boost_move_FOUND)
  find_dependency(boost_move 1.87.0 EXACT HINTS "${CMAKE_CURRENT_LIST_DIR}/..")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/boost_container-targets.cmake")
