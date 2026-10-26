add_executable(uvula_js UvulaJS.cpp)
target_link_options(uvula_js
        PUBLIC
        "SHELL:-s ENVIRONMENT=web,worker,node"
        "SHELL:-s USE_ES6_IMPORT_META=1"
        "SHELL:-s FORCE_FILESYSTEM=1"
        "SHELL:-s EXPORT_NAME=uvula"
        "SHELL:-s MODULARIZE=1"
        "SHELL:-s EXPORT_ES6=1"
        "SHELL:-s SINGLE_FILE=1"
        "SHELL:-s ALLOW_MEMORY_GROWTH=1"
        "SHELL:-s WASM_BIGINT=1"
        "SHELL:-s STACK_SIZE=196608"
        "SHELL:-s ERROR_ON_UNDEFINED_SYMBOLS=0"
        "SHELL:-lembind"
        "SHELL:--emit-tsd uvula_js.d.ts"
        "$<$<CONFIG:Debug>:SHELL:-s ASSERTIONS=2>"
        "$<$<CONFIG:Debug>:SHELL:-s SAFE_HEAP=1>"
        "$<$<CONFIG:Debug>:SHELL:-s STACK_OVERFLOW_CHECK=2>"
        "$<$<CONFIG:Debug>:SHELL:-s EXCEPTION_DEBUG=1>"
        "$<$<CONFIG:Debug>:SHELL:-g3>"
        "$<$<CONFIG:Debug>:SHELL:-gsource-map>"
)
target_link_libraries(uvula_js PUBLIC libuvula)
