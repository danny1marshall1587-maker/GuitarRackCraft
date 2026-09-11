# =============================================================================
# cmake/targets/cyber_audio.cmake — Build Cyber Audio Plugins (Amps, Pedals, Tuner)
# =============================================================================

set(CYBER_SRC "${PROJECT_ROOT}/cyber_pack")

if(EXISTS "${CYBER_SRC}")
    file(GLOB _cyber_plugins "${CYBER_SRC}/*")
    set(_cyber_done_targets "")

    foreach(_plugin_dir IN LISTS _cyber_plugins)
        if(IS_DIRECTORY "${_plugin_dir}" AND NOT "${_plugin_dir}" MATCHES "include$")
            get_filename_component(_bundle_name "${_plugin_dir}" NAME)
            
            if(EXISTS "${_plugin_dir}/manifest.ttl")
                file(READ "${_plugin_dir}/manifest.ttl" _manifest_content)
                string(REGEX MATCH "<([^>]+\\.so)>" _match "${_manifest_content}")
                set(_so_name "${CMAKE_MATCH_1}")
                if(NOT _so_name)
                    string(REGEX MATCH "lv2:binary[ 	
]+<([^>]+)>" _match "${_manifest_content}")
                    set(_so_name "${CMAKE_MATCH_1}")
                endif()
                
                if(_so_name)
                    string(REGEX REPLACE "\\.(so|dll)$" "" _lib_stem "${_so_name}")
                    string(REPLACE "-" "_" _target_safe "${_lib_stem}")
                    set(_target_name "target_${_target_safe}")
                    
                    # 1. Copy bundle to ASSETS_DIR
                    set(_dest_dir "${ASSETS_DIR}/${_bundle_name}")
                    file(MAKE_DIRECTORY "${_dest_dir}")
                    
                    file(GLOB _ttls "${_plugin_dir}/*.ttl")
                    foreach(_ttl IN LISTS _ttls)
                        get_filename_component(_ttl_name "${_ttl}" NAME)
                        configure_file("${_ttl}" "${_dest_dir}/${_ttl_name}" COPYONLY)
                    endforeach()
                    
                    if(IS_DIRECTORY "${_plugin_dir}/modgui")
                        file(COPY "${_plugin_dir}/modgui" DESTINATION "${_dest_dir}")
                    endif()
                    
                    # 2. Collect C/C++ sources
                    file(GLOB _srcs "${_plugin_dir}/src/*.cpp" "${_plugin_dir}/src/*.c")
                    if(_srcs)
                        add_library(${_target_name} SHARED ${_srcs})
                        
                        target_include_directories(${_target_name} PRIVATE
                            "${_plugin_dir}/src"
                            "${CYBER_SRC}/include"
                            "${CYBER_SRC}/include/lv2"
                            "${THIRD_PARTY}/lv2/include"
                        )
                        
                        target_compile_options(${_target_name} PRIVATE
                            -fPIC -O3 -fvisibility=hidden
                            -Wno-unused-parameter -Wno-unused-result
                            -ffast-math
                            $<$<COMPILE_LANGUAGE:CXX>:-std=c++17>
                            $<$<COMPILE_LANGUAGE:C>:-std=c11>
                        )
                        
                        set_target_properties(${_target_name} PROPERTIES
                            PREFIX "lib"
                            OUTPUT_NAME "${_lib_stem}"
                            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/cyber_libs"
                        )
                        
                        add_custom_command(
                            TARGET ${_target_name} POST_BUILD
                            COMMAND ${CMAKE_COMMAND} -E make_directory "${JNILIBS_DIR}"
                            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                                "$<TARGET_FILE:${_target_name}>"
                                "${JNILIBS_DIR}/$<TARGET_FILE_NAME:${_target_name}>"
                            COMMAND ${NDK_STRIP} --strip-unneeded "${JNILIBS_DIR}/$<TARGET_FILE_NAME:${_target_name}>"
                            COMMENT "Staging ${_target_name} to jniLibs"
                        )
                        
                        list(APPEND _cyber_done_targets ${_target_name})
                    endif()
                endif()
            endif()
        endif()
    endforeach()
    
    add_custom_target(cyber_audio_done ALL DEPENDS ${_cyber_done_targets})
    message(STATUS "Cyber Audio Suite configured with ${_cyber_done_targets}")
endif()
