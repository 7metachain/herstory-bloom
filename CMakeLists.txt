##
# @file CMakeLists.txt
# @brief
#/

set(APP_PATH ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(APP_NAME ${APP_PATH} NAME)
aux_source_directory(${APP_PATH}/src APP_SRCS)
set(APP_INC ${APP_PATH}/include)
add_library(${EXAMPLE_LIB})
target_sources(${EXAMPLE_LIB}
    PRIVATE
        ${APP_SRCS}
    )
target_include_directories(${EXAMPLE_LIB}
    PRIVATE
        ${APP_INC}
    )
