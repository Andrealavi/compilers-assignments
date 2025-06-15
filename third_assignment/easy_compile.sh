# --- Configuration based on your commands ---
# Output of clang++ (intermediate, unoptimized LLVM IR)
CLANG_OUTPUT_LL_FILE="out.ll"

OPT_TARGET_DIR="./examples"
OPT_TARGET_BASENAME="out.ll" # The basename is the same
OPT_TARGET_LL_FILE="${OPT_TARGET_DIR}/${OPT_TARGET_BASENAME}"

# --- Script Logic ---

# 1. Check for input C++ file argument
if [ -z "${1-}" ]; then # Check if $1 is unset or empty
    echo "Usage: $0 <cpp_file_path>"
    echo "Example: $0 my_program.cpp"
    exit 1
fi
CPP_FILE="$1"

# 2. Validate input C++ file exists
if [ ! -f "$CPP_FILE" ]; then
    echo "Error: Input C++ file '$CPP_FILE' not found."
    exit 1
fi

echo "Input C++ file: '$CPP_FILE'"

# 3. Compile C++ to LLVM IR (unoptimized) using clang++
# Command: clang++ -O0 -Xclang -disable-O0-optnone -emit-llvm file_name -S -o out.ll
echo "Step 1: Compiling '$CPP_FILE' to unoptimized LLVM IR ('$CLANG_OUTPUT_LL_FILE')..."
clang++ -O0 -Xclang -disable-O0-optnone -emit-llvm "$CPP_FILE" -S -o "$CLANG_OUTPUT_LL_FILE"
# set -e will handle exit on error, no need for explicit if [ $? -ne 0 ]

echo "Successfully created intermediate LLVM IR: '$CLANG_OUTPUT_LL_FILE'."

# 4. Prepare for opt: ensure target directory exists and move the LLVM IR file
echo "Preparing for opt: ensuring directory '$OPT_TARGET_DIR' exists..."
mkdir -p "$OPT_TARGET_DIR"
# set -e will handle exit on error

echo "Moving '$CLANG_OUTPUT_LL_FILE' to '$OPT_TARGET_LL_FILE'..."
mv "$CLANG_OUTPUT_LL_FILE" "$OPT_TARGET_LL_FILE"
# set -e will handle exit on error

# 5. Apply mem2reg optimization using opt
# Command: opt -passes=mem2reg ./examples/out.ll -S -o ./examples/out.ll
echo "Step 2: Applying mem2reg optimization to '$OPT_TARGET_LL_FILE'..."
opt -passes=mem2reg "$OPT_TARGET_LL_FILE" -S -o "$OPT_TARGET_LL_FILE"
# set -e will handle exit on error

echo "Successfully applied mem2reg optimization."

# --- Final Message ---
echo ""
echo "Process complete."
echo "Optimized LLVM IR is at: $OPT_TARGET_LL_FILE"

exit 0
