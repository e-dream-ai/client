add_library (progutils OBJECT "utils.c")
target_include_directories (progutils PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_include_directories (progutils PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/../src)

function (add_aubio_executable name)

  add_executable (${name} "${name}.c")

  target_link_libraries (${name} PUBLIC aubio)
  target_link_libraries (${name} PRIVATE progutils)
  target_include_directories (${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
  target_include_directories (${name} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/../src)

endfunction ()

add_aubio_executable (aubioonset)
add_aubio_executable (aubiomfcc)
add_aubio_executable (aubionotes)
add_aubio_executable (aubiopitch)
add_aubio_executable (aubioquiet)
add_aubio_executable (aubiotrack)
