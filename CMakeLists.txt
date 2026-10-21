#指定cmake最小版本
cmake_minimum_required(VERSION 3.22.1)

#设置项目版本以及名称
project(EEGBridge VERSION 1.0.0 LANGUAGES CXX)

#设置动态库/静态库可选
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(EEGBRIDGE_BUILD_EXAMPLES "Build EEGBridge examples" ${PROJECT_IS_TOP_LEVEL})
option(EEGBRIDGE_BUILD_TESTS "Build EEGBridge tests" OFF)

#仅在项目顶层设置默认安装路径，避免作为子模块时污染外部主项目
if (PROJECT_IS_TOP_LEVEL AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/install" CACHE PATH "默认安装路径" FORCE)
endif ()

#==================== 1. 定义库与别名 ====================
#递归收集 src 下的 .cpp，新增或删除文件后在构建时重新检查
file(GLOB_RECURSE EEGBRIDGE_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
#编译成动态库/静态库
add_library(EEGBridge ${EEGBRIDGE_SOURCES})

#【关键机关 1】取一个带命名空间的别名！
#保证 add_subdirectory 和 find_package 两种模式下，主项目链接的名字完全一致！
add_library(EEGBridge::EEGBridge ALIAS EEGBridge)

target_compile_features(EEGBridge PUBLIC cxx_std_20)

set_target_properties(EEGBridge PROPERTIES
        DEBUG_POSTFIX d
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
)

#====================2.生成导入/导出文件===================
include(GenerateExportHeader)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include")
generate_export_header(EEGBridge EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/include/EEGBridge_export.h"
        EXPORT_MACRO_NAME EEGBRIDGE_EXPORT
        STATIC_DEFINE EEGBRIDGE_STATIC_DEFINE
)
#静态库：关闭导入/导出标记
#PUBLIC: 编译库自身和使用这个库的目标都需要这个宏
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(EEGBridge PUBLIC EEGBRIDGE_STATIC_DEFINE)
endif()

#【关键机关 2】双模头文件路径自动切换
target_include_directories(EEGBridge PUBLIC

        #源码中的公开头文件
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>

        #自动生成的宏头文件
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>

        #安装后的头文件
        $<INSTALL_INTERFACE:include>
)


#连接依赖的第三方库
include(${CMAKE_CURRENT_LIST_DIR}/third_party/ixwebsocket/findIXWS.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/third_party/nlohmannJson/findNlohmann.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/third_party/liblsl/findLSL.cmake)

if (EEGBRIDGE_BUILD_EXAMPLES)
    add_subdirectory(example)
endif ()

if (EEGBRIDGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

#==================== 3. 独立发布模式专属配置 ====================
#【关键机关 3】只有当你单独编译这个库时，才需要安装头文件和生成 find_package 配置文件
#如果你是把它放在主项目的子目录里一起编译，下面这段会自动跳过，保持主项目干净！
if (PROJECT_IS_TOP_LEVEL)

    # 把动态库/静态库安装到 lib 目录
    install(TARGETS EEGBridge EXPORT EEGBridgeTargets
            RUNTIME DESTINATION "$<IF:$<CONFIG:Debug>,debug/bin,bin>"
            LIBRARY DESTINATION "$<IF:$<CONFIG:Debug>,debug/lib,lib>"
            ARCHIVE DESTINATION "$<IF:$<CONFIG:Debug>,debug/lib,lib>"
    )


    #静态版的使用方仍需链接这些第三方库，目录与安装后的搜索路径保持一致
    install(DIRECTORY "${IXWS_LIBRARY_DIR}/"
            DESTINATION "$<IF:$<CONFIG:Debug>,debug/lib,lib>"
            FILES_MATCHING PATTERN "*.lib"
    )
    #zlib 是动态依赖，安装时也需要提供对应配置的 DLL
    install(FILES "${IXWS_LIBRARY_DIR}/${IXWS_ZLIB_DLL}"
            DESTINATION "$<IF:$<CONFIG:Debug>,debug/bin,bin>"
    )

    #liblsl 使用导入库链接，运行时还需要对应配置的 DLL
    install(FILES "${LSL_LIBRARY_DIR}/${LSL_LIBRARY}.lib"
            DESTINATION "$<IF:$<CONFIG:Debug>,debug/lib,lib>"
    )
    install(FILES "${LSL_LIBRARY_DIR}/${LSL_DLL}"
            "${LSL_LIBRARY_DIR}/${LSL_PUGIXML_DLL}"
            DESTINATION "$<IF:$<CONFIG:Debug>,debug/bin,bin>"
    )

    #安装头文件
    install(DIRECTORY include/ DESTINATION include)
    install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/ DESTINATION include)

    #生成 find_package 配置文件
    include(CMakePackageConfigHelpers)
    write_basic_package_version_file(
            "${CMAKE_CURRENT_BINARY_DIR}/EEGBridgeConfigVersion.cmake"
            VERSION ${PROJECT_VERSION}
            COMPATIBILITY AnyNewerVersion
    )

    #导出目标账本
    install(EXPORT EEGBridgeTargets
            FILE EEGBridgeTargets.cmake
            NAMESPACE EEGBridge::
            DESTINATION lib/cmake/EEGBridge
    )

    #生成并安装 Config.cmake 文件
    configure_package_config_file(
            "${CMAKE_CURRENT_LIST_DIR}/cmake/EEGBridgeConfig.cmake.in"
            "${CMAKE_CURRENT_BINARY_DIR}/EEGBridgeConfig.cmake"
            INSTALL_DESTINATION lib/cmake/EEGBridge
    )

    #安装 Config和Version 文件
    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/EEGBridgeConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/EEGBridgeConfigVersion.cmake"
            DESTINATION lib/cmake/EEGBridge
    )
endif ()








