configure_file(integration_test.sh.in integration_test.sh @ONLY)

# Coreneuron simulator Client
add_executable(reports_integration_test integration_test.cpp)
target_include_directories(reports_integration_test
    PRIVATE
        $<BUILD_INTERFACE:${SONATA_REPORT_INCLUDE_DIR}>
        $<BUILD_INTERFACE:${SONATA_REPORT_SRC_DIR}>
    SYSTEM
        ${PROJECT_SOURCE_DIR}/extlib/spdlog/include
)
target_compile_options(reports_integration_test
    PRIVATE ${SONATA_REPORT_COMPILE_OPTIONS}
)
target_link_libraries(reports_integration_test
    PUBLIC sonata_report
    PRIVATE spdlog::spdlog_header_only
    PRIVATE ${MPI_C_LIBRARIES}
)

add_test(NAME reports_integration_test
    COMMAND "/bin/sh" ${CMAKE_CURRENT_BINARY_DIR}/integration_test.sh
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

set_tests_properties(
    reports_integration_test
    PROPERTIES
        ENVIRONMENT "LIBSONATAREPORT_ONE_BASED_GIDS=1"
)

add_executable(summed_report_test summed_report_test.cpp)
target_include_directories(summed_report_test
    PRIVATE
        $<BUILD_INTERFACE:${SONATA_REPORT_INCLUDE_DIR}>
)
target_compile_options(summed_report_test
    PRIVATE ${SONATA_REPORT_COMPILE_OPTIONS}
)
target_link_libraries(summed_report_test
    PUBLIC sonata_report
    PRIVATE ${MPI_C_LIBRARIES}
    PRIVATE ${HDF5_C_LIBRARIES}
)

if(SONATA_REPORT_HAVE_MPI)
    set(SUMMED_REPORT_TEST_COMMAND mpirun -n 4 $<TARGET_FILE:summed_report_test>)
else()
    set(SUMMED_REPORT_TEST_COMMAND $<TARGET_FILE:summed_report_test>)
endif()
add_test(NAME summed_report_test COMMAND ${SUMMED_REPORT_TEST_COMMAND})
add_test(NAME summed_report_test_one_based COMMAND ${SUMMED_REPORT_TEST_COMMAND})
set_tests_properties(
    summed_report_test_one_based
    PROPERTIES
        ENVIRONMENT "LIBSONATAREPORT_ONE_BASED_GIDS=1"
)
