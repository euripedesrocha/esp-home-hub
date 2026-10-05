target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wshadow
    -Wformat=2
    -Wnull-dereference
    -Wduplicated-cond
    -Wduplicated-branches
    -Wlogical-op
    -fanalyzer)
