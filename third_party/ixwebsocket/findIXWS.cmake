
message(STATUS "连接ixwebsocket静态库")

set(IXWS_ROOT_PATH "${CMAKE_CURRENT_LIST_DIR}")

set(IXWS_INCLUDE_PATH "${IXWS_ROOT_PATH}/include")

if(WIN32)
    #在构建时选择配置，同时支持 Ninja 和 Visual Studio 等多配置生成器
    set(IXWS_LIBRARY_DIR "${IXWS_ROOT_PATH}/$<IF:$<CONFIG:Debug>,debug,release>")
    set(IXWS_ZLIB_LIBRARY "$<IF:$<CONFIG:Debug>,zlibd,zlib>")
    set(IXWS_ZLIB_DLL "$<IF:$<CONFIG:Debug>,zlibd1.dll,zlib1.dll>")

    # 静态库及其依赖
    set(IXWS_LIBRARY
            ixwebsocket
            mbedtls
            mbedx509
            mbedcrypto
            everest
            p256m
            ${IXWS_ZLIB_LIBRARY}
            ws2_32
            crypt32
            bcrypt
    )
else ()
    message(FATAL_ERROR "CURRENT ONLY SUPPORT WIN PLATFORM")
endif ()

# 头文件路径传给使用方
# 第三方类型暂不出现在公开接口中，因此只供库内部编译使用。
target_include_directories(${PROJECT_NAME} PRIVATE "${IXWS_INCLUDE_PATH}")

# 静态库的使用方还需要从此目录链接底层依赖。
target_link_directories(${PROJECT_NAME} PUBLIC
        "$<BUILD_INTERFACE:${IXWS_LIBRARY_DIR}>"
        "$<INSTALL_INTERFACE:$<INSTALL_PREFIX>/$<IF:$<CONFIG:Debug>,debug/lib,lib>>"
)

#链接静态库
target_link_libraries(${PROJECT_NAME} PRIVATE ${IXWS_LIBRARY})

#复制 DLL
#源路径包含构建时才求值的生成器表达式，不能用配置阶段的 EXISTS 判断。
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${IXWS_LIBRARY_DIR}/${IXWS_ZLIB_DLL}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"

            COMMENT "Copying zlib DLL"
            VERBATIM
    )

message(STATUS "IXWebSocket Library: ${IXWS_LIBRARY_DIR}")
message(STATUS "IXWebSocket Libraries: ${IXWS_LIBRARY}")
