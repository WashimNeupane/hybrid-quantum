# Build recipes for this project.
#

# Load environment vars from .env file
# Write LLVM_BUILD_DIR="path" into that file or set this env var in your shell.
set dotenv-load := true

# Make sure your LLVM is tags/llvmorg-18.1.6

llvm_prefix := env_var("LLVM_BUILD_DIR")
build_type := env_var_or_default("LLVM_BUILD_TYPE", "RelWithDebInfo")
linker := env_var_or_default("CMAKE_LINKER_TYPE", "DEFAULT")
upmem_dir := env_var_or_default("UPMEM_HOME", "")
build_dir := "quantum/build"

# execute cmake -- this is only needed on the first build
cmake *ARGS:
    cmake -S . -B {{build_dir}} \
        -G Ninja \
        -DCMAKE_BUILD_TYPE={{build_type}} \
        -DLLVM_DIR="{{llvm_prefix}}/lib/cmake/llvm" \
        -DMLIR_DIR="{{llvm_prefix}}/lib/cmake/mlir" \
        -DUPMEM_DIR="{{upmem_dir}}" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=1 \
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_LINKER_TYPE={{linker}} \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DCMAKE_C_USING_LINKER_mold=-fuse-ld=mold \
        -DCMAKE_CXX_USING_LINKER_mold=-fuse-ld=mold \
        {{ARGS}}

# execute a specific ninja target
doNinja *ARGS:
    ninja -C {{build_dir}} {{ARGS}}


# run build --first build needs cmake though
build: doNinja

cleanBuild:
    rm -rf {{build_dir}}
    just cmake
    just build

alias b := build

# run tests
test: (doNinja "check-quantum-mlir")

[no-cd]
quantum-opt *ARGS: (doNinja "quantum-opt")
    {{source_directory()}}/{{build_dir}}/bin/quantum-opt {{ARGS}}

quantum-opt-help: (quantum-opt "--help")

# Start a gdb session on quantum-opt.
debug-quantum-opt *ARGS:
    gdb --args {{build_dir}}/bin/quantum-opt {{ARGS}}

cnm-to-gpu FILE *ARGS: (quantum-opt FILE "--convert-cnm-to-gpu" ARGS)

cinm-vulkan-runner FILE *ARGS:
    {{build_dir}}/bin/cinm-vulkan-runner {{FILE}} \
        --shared-libs=../llvm-project/build/lib/libvulkan-runtime-wrappers.so,../llvm-project/build/lib/libmlir_runner_utils.so.17 \
        {{ARGS}}

genBench NAME: (doNinja "quantum-opt")
    #!/bin/bash
    source "{{upmem_dir}}/upmem_env.sh"
    cd testbench
    export BENCH_NAME="{{NAME}}"
    make clean && make {{NAME}}-exe

runBench NAME:
    #!/bin/bash
    source "{{upmem_dir}}/upmem_env.sh"
    cd testbench/generated2/{{NAME}}/bin
    ./host

bench NAME: (doNinja "quantum-opt")
    #!/bin/bash
    set -e
    source "{{upmem_dir}}/upmem_env.sh"
    cd testbench
    export BENCH_NAME="{{NAME}}"
    make clean && make {{NAME}}-exe
    cd generated2/{{NAME}}/bin
    ./host



# Invoke he LLVM IR compiler.
llc *ARGS:
    {{llvm_prefix}}/bin/llc {{ARGS}}

# Lowers Sigi all the way to LLVM IR. Temporary files are left there.
llvmDialectIntoExecutable FILE:
    #!/bin/bash
    FILEBASE={{FILE}}
    FILEBASE=${FILEBASE%.*}
    FILEBASE=${FILEBASE%.llvm}
    {{llvm_prefix}}/bin/mlir-translate -mlir-to-llvmir {{FILE}} > ${FILEBASE}.ll
    # creates {{FILE}}.s
    {{llvm_prefix}}/bin/llc -O0 ${FILEBASE}.ll
    clang-14 -fuse-ld=lld -L{{build_dir}}/lib -lSigiRuntime ${FILEBASE}.s -g -o ${FILEBASE}.exe -no-pie

addNewDialect DIALECT_NAME DIALECT_NS:
    just --justfile ./dialectTemplate/justfile applyTemplate {{DIALECT_NAME}} {{DIALECT_NS}} "cinm-mlir" {{justfile_directory()}}
