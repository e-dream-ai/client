cmake_minimum_required(VERSION 3.20)
project(infinidream LANGUAGES C CXX OBJC OBJCXX)
enable_language(Swift OPTIONAL)

# Set to build a macOS app bundle
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES)
set(CMAKE_XCODE_ATTRIBUTE_CLANG_ENABLE_MODULES YES)

# Compiler definitions
add_compile_definitions(
    MAC
    USE_METAL
    BOOST_FILESYSTEM_VERSION=3
    BOOST_NETWORK_ENABLE_HTTPS
    OPENSSL_NO_ERR
    OBJC_OLD_DISPATCH_PROTOTYPES=0
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    $<$<CONFIG:Release>:NS_BLOCK_ASSERTIONS>
)


# App specific
set(APP_SOURCES
    main.m
)

# ScreenSaver specific - no unique sources
set(SCREENSAVER_SOURCES
)

# All shared sources
set(SHARED_SOURCES
    ${COMMON_SOURCES}
    ${CLIENT_SOURCES}
    ${CONTENT_SOURCES}
    ${DISPLAY_SOURCES}
    ${METAL_SOURCES}
    ${NETWORKING_SOURCES}
    ${STORAGE_SOURCES}
    ${METAL_HELPERS}
    ${SWIFT_SOURCES}
    proximus.c
)

# Create App Prod target
add_executable("infinidream_app" MACOSX_BUNDLE
    main.m
)

# Create infinidream_saver target
add_library("infinidream_saver" MODULE
    ${SHARED_SOURCES}
    ${MAC_SOURCES}
    ${SCREENSAVER_SOURCES}
)

# Include directories
target_include_directories(infinidream_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/First\ startup
    ../../cache/univ-osx/include
    ../Client
    ../Common/Math
    ../Common
    ../DisplayOutput
    ../ContentDecoder
    ../ContentDownloader
    ../TupleStorage
    ../DisplayOutput/OpenGL
    ../DisplayOutput/Renderer
    ../DisplayOutput/Metal
    ../Networking
    ../libpng/include
    ..
    ../socket.io-client-cpp/include
)

target_include_directories(infinidream_saver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/First\ startup
    ../../cache/univ-osx/include
    ../Client
    ../Common/Math
    ../Common
    ../DisplayOutput
    ../ContentDecoder
    ../ContentDownloader
    ../TupleStorage
    ../DisplayOutput/OpenGL
    ../DisplayOutput/Renderer
    ../DisplayOutput/Metal
    ../Networking
    ../libpng/include
    ..
    ../socket.io-client-cpp/include
)

# Library search paths
target_link_directories(infinidream_app PRIVATE
    ../../cache/univ-osx/lib
    ../libpng/osx/lib
    ../ffmpeg/osx/lib
    ../openssl-1.0.2k/MacOS/lib
    ../socket.io-client-cpp/lib
)

target_link_directories(infinidream_saver PRIVATE
    ../../cache/univ-osx/lib
    ../libpng/osx/lib
    ../ffmpeg/osx/lib
    ../openssl-1.0.2k/MacOS/lib
    ../socket.io-client-cpp/lib
)

# Import Sparkle framework
add_library(Sparkle SHARED IMPORTED)
set_target_properties(Sparkle PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/Frameworks/Sparkle.framework/Versions/B/Sparkle"
    FRAMEWORK TRUE
)

# Link frameworks - App
target_link_libraries(infinidream_app
    "-framework Cocoa"
    "-framework Metal"
    "-framework MetalKit"
    "-framework AVFoundation"
    Sparkle
    "-lobjc"
    ssl
    crypto
    sioclient_tls
)

# Link frameworks - ScreenSaver
target_link_libraries(infinidream_saver
    "-framework Cocoa"
    "-framework ScreenSaver"
    "-framework Metal"
    "-framework MetalKit"
    "-framework AVFoundation"
    "-lobjc"
    ssl
    crypto
    sioclient_tls
)

# Copy Sparkle framework to app bundle
add_custom_command(TARGET infinidream_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:infinidream_app>/Contents/Frameworks"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/Frameworks/Sparkle.framework"
        "$<TARGET_BUNDLE_DIR:infinidream_app>/Contents/Frameworks/Sparkle.framework"
    COMMENT "Copying Sparkle.framework to app bundle"
)

# Set bundle properties - App
set_target_properties(infinidream_app PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/App-Info.plist
    MACOSX_BUNDLE_BUNDLE_NAME "infinidream"
    MACOSX_BUNDLE_GUI_IDENTIFIER "ai.infinidream.alpha.app"
    OUTPUT_NAME "infinidream"
    XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER "infinidream App Prod-Bridging-Header.h"
    XCODE_ATTRIBUTE_SWIFT_VERSION "5.0"
    XCODE_ATTRIBUTE_FRAMEWORK_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/Frameworks $(inherited)"
    XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@executable_path/../Frameworks $(inherited)"
)

# Set bundle properties - ScreenSaver
set_target_properties(infinidream_saver PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "saver"
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Screensaver-Info.plist
    OUTPUT_NAME "infinidream"
)
