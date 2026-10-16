
message(STATUS "连接liblsl动态库")

set(LSL_ROOT_PATH "${CMAKE_CURRENT_LIST_DIR}")

set(LSL_INCLUDE_PATH "${LSL_ROOT_PATH}/include")

if(WIN32)
    #Debug 和 Release 的文件同名，通过目录选择对应的配置
    set(LSL_LIBRARY_DIR "${LSL_ROOT_PATH}/$<IF:$<CONFIG:Debug>,debug,release>")
    set(LSL_LIBRARY lsl)
    set(LSL_DLL lsl.dll)
    #lsl.dll 运行时还依赖 pugixml.dll，不需要额外链接 pugixml.lib
    set(LSL_PUGIXML_DLL pugixml.dll)
else ()
    message(FATAL_ERROR "CURRENT ONLY SUPPORT WIN PLATFORM")
endif ()

#第三方类型暂不出现在公开接口中，头文件只供库内部编译使用
target_include_directories(${PROJECT_NAME} PRIVATE "${LSL_INCLUDE_PATH}")

#静态版 PythonBridge 的使用方仍需链接 lsl.lib，安装后使用安装目录中的库
target_link_directories(${PROJECT_NAME} PUBLIC
        "$<BUILD_INTERFACE:${LSL_LIBRARY_DIR}>"
        "$<INSTALL_INTERFACE:$<INSTALL_PREFIX>/$<IF:$<CONFIG:Debug>,debug/lib,lib>>"
)

#lsl.lib 是 lsl.dll 的导入库，链接后运行时仍需要 DLL
target_link_libraries(${PROJECT_NAME} PRIVATE ${LSL_LIBRARY})

#复制当前配置的 DLL 到 PythonBridge 输出目录
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${LSL_LIBRARY_DIR}/${LSL_DLL}"
        "${LSL_LIBRARY_DIR}/${LSL_PUGIXML_DLL}"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"

        COMMENT "Copying liblsl DLL"
        VERBATIM
)

message(STATUS "liblsl Library: ${LSL_LIBRARY_DIR}")
message(STATUS "liblsl Libraries: ${LSL_LIBRARY}")
