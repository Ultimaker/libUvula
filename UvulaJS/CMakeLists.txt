add_executable(uvula_js UvulaJS.cpp)
target_link_options(uvula_js
        PUBLIC
        "SHELL:-s USE_ES6_IMPORT_META=1"
        "SHELL:-s FORCE_FILESYSTEM=1"
        "SHELL:-s EXPORT_NAME=uvula"
        "SHELL:-s MODULARIZE=1"
        "SHELL:-s EXPORT_ES6=1"
        "SHELL:-s SINGLE_FILE=1"
        "SHELL:-s ALLOW_MEMORY_GROWTH=1"
        "SHELL:-s ERROR_ON_UNDEFINED_SYMBOLS=0"
        "SHELL:--bind"
        "SHELL:-l embind"
        "SHELL: --emit-tsd uvula_js.d.ts"
)
target_link_libraries(uvula_js PUBLIC libuvula)