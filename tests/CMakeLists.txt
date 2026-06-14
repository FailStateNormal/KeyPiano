include(GoogleTest)

function(keypiano_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE keypiano_core GTest::gtest_main)
    gtest_discover_tests(${name})
endfunction()

# Uncomment each entry as the corresponding source file is created:
#
# Phase 1:
keypiano_add_test(test_lockfree_queue)     # P1-B
keypiano_add_test(test_keymap_parser)      # P1-G
keypiano_add_test(test_cfg_parser)         # P1-H
#
# Phase 2:
keypiano_add_test(test_kps_format)         # P2-A
keypiano_add_test(test_recorder)           # P2-B
