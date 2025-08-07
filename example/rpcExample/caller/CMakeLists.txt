# aux_source_directory(. SRC_LIST)
set(SRC_LIST callFriendService.cpp ../friend.pb.cc)

add_executable(consumer ${SRC_LIST})
target_link_libraries(consumer rpc_lib protobuf)