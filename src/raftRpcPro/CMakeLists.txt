# 获取当前源文件所在目录的绝对路径
get_filename_component(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}" ABSOLUTE)

# 将该目录下所有源文件添加到 SRC_LIST 变量中
aux_source_directory(${SRC_DIR} SRC_LIST)


# 将收集到的源文件列表 SRC_LIST 赋值给 src_common 变量，并将其设置为内部缓存变量 ，使这个变量可以在整个项目的其他CMakeLists.txt文件中使用
set(src_raftRpcPro ${SRC_LIST} CACHE INTERNAL "Description of the variable")