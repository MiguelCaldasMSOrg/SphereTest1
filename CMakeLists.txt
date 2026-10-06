cmake_minimum_required(VERSION 3.20)

project(SphereTest1 LANGUAGES C)

add_executable(${PROJECT_NAME}
    main.c
    "examples\\examples.c"
    "examples\\example_timer.c"
    "examples\\gpio_example.c"
    "examples\\uart_example.c"
    "examples\\network_example.c"
    "examples\\async_job.c"
    "examples\\i2c_example.c"
    "examples\\storage_example.c"
)
target_compile_options(${PROJECT_NAME} PRIVATE -Wextra -Werror)
target_link_libraries(${PROJECT_NAME} PRIVATE applibs pthread gcc_s c)

azsphere_target_add_image_package(${PROJECT_NAME}
    RESOURCE_FILES "examples/resources/message.txt"
)
