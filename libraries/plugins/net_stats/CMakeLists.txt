file(GLOB HEADERS "include/graphene/net_stats/*.hpp")

add_library( graphene_net_stats
        net_stats.cpp
        ${HEADERS}
           )

target_link_libraries( graphene_net_stats graphene_chain graphene_app )
target_include_directories( graphene_net_stats
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

if(MSVC)
  set_source_files_properties(net_stats.cpp PROPERTIES COMPILE_FLAGS "/bigobj" )
endif(MSVC)

install( TARGETS
   graphene_net_stats

   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
   ARCHIVE DESTINATION lib
)
INSTALL( FILES ${HEADERS} DESTINATION "include/graphene/net_stats" )
