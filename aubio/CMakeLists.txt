cmake_minimum_required (VERSION 3.13)

project (aubio LANGUAGES C VERSION 0.5.0)

enable_testing ()

# enter subdirectories
add_subdirectory (src)
add_subdirectory (examples)
add_subdirectory (tests)
