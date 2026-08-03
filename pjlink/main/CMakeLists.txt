idf_component_register(
    SRCS "pjlink.c"
    INCLUDE_DIRS
        "../../sdk/include"
)
target_compile_options(${COMPONENT_LIB} PRIVATE -fno-builtin -fno-tree-loop-distribute-patterns)
