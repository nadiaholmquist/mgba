include("${DEVKITPRO}/wut/share/wut.cmake" REQUIRED)
include("${DEVKITPRO}/portlibs/wiiu/share/romfs-wiiu.cmake" REQUIRED)

set(OS_DEFINES USE_VFS_FILE IOAPI_NO_64)
list(APPEND CORE_VFS_SRC ${CMAKE_SOURCE_DIR}/src/util/vfs/vfs-file.c ${CMAKE_SOURCE_DIR}/src/util/vfs/vfs-dirent.c)
list(APPEND GUI_SRC ${CMAKE_CURRENT_SOURCE_DIR}/gui-font.c)

file(GLOB OS_SRC ${CMAKE_SOURCE_DIR}/src/platform/wiiu/wiiu-*.c)
set(CORE_VFS_SRC ${CORE_VFS_SRC} PARENT_SCOPE)
set(OS_DEFINES ${OS_DEFINES} PARENT_SCOPE)
set(OS_SRC ${OS_SRC} PARENT_SCOPE)
set(OS_LIB ${OS_LIB} PARENT_SCOPE)

if(BUILD_PERF)
endif()

add_executable(${BINARY_NAME}-wiiu ${GUI_SRC} ${PLATFORM_SRC} main.c)
set_target_properties(${BINARY_NAME}-wiiu PROPERTIES COMPILE_DEFINITIONS "${OS_DEFINES};${FEATURE_DEFINES};${FUNCTION_DEFINES}")
target_link_libraries(${BINARY_NAME}-wiiu
    ${BINARY_NAME} ${M_LIBRARY} coreinit gx2 vpad vpadbase gfd whb proc_ui sysapp ${OS_LIB}
)
configure_file("${CMAKE_SOURCE_DIR}/res/font-new.png" romfs/font-new.png COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/shaders/main.gsh" romfs/main.gsh COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/shaders/font.gsh" romfs/font.gsh COPYONLY)

#add_custom_command(OUTPUT romfs.bin
#                   COMMAND ${CMAKE_COMMAND} -E make_directory romfs
#                   COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/res/font-new.png" romfs/
#                   COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/fileassoc.cfg.in" romfs/
#                   COMMAND ${BUILD_ROMFS} romfs romfs.bin
#                   COMMAND ${CMAKE_COMMAND} -E remove_directory romfs
#               DEPENDS "${CMAKE_SOURCE_DIR}/res/font-new.png" "${CMAKE_CURRENT_SOURCE_DIR}/fileassoc.cfg.in")

romfs_add(${BINARY_NAME}-wiiu "${CMAKE_CURRENT_BINARY_DIR}/romfs")
wut_create_rpx(${BINARY_NAME}-wiiu)

install(TARGETS ${BINARY_NAME}-wiiu DESTINATION . COMPONENT ${BINARY_NAME}-dbg)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${BINARY_NAME}-wiiu.rpx DESTINATION . COMPONENT ${BINARY_NAME}-wiiu)

