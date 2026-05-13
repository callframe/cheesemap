set(CM_OLD "cheesemap_old")
project(${CM_OLD}
    LANGUAGES C
)

set(CM_OLD_SOURCE "${CM_OLD_DIR}/cheesemap.c")

add_library(${CM_OLD} STATIC ${CM_OLD_SOURCE})
target_include_directories(${CM_OLD} PUBLIC ${CM_OLD_DIR})
target_compile_features(${CM_OLD} PRIVATE c_std_11)
