#!/bin/bash
# Run script for MPI Usage Sanitizer — executes all tests
# Usage: ./run.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MPIASAN="$SCRIPT_DIR/mpiasan-cc"
PASS=0
FAIL=0
TOTAL=0

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

chmod +x "$MPIASAN"

run_test() {
    local src="$1"
    local np="$2"
    local expect_error="$3"   # "yes" or "no"
    local timeout_sec="${4:-10}"
    local name=$(basename "$src" .c)
    TOTAL=$((TOTAL + 1))

    # Compile
    if ! "$MPIASAN" "$src" -o /tmp/mpiasan_${name} 2>/dev/null; then
        echo -e "  ${RED}[COMPILE FAIL]${NC} $name"
        FAIL=$((FAIL + 1))
        return
    fi

    # Run and capture output
    output=$(timeout "$timeout_sec" mpirun --oversubscribe -np "$np" /tmp/mpiasan_${name} 2>&1 || true)

    has_error=false
    if echo "$output" | grep -q "==MPISanitizer=="; then
        has_error=true
    fi

    if [ "$expect_error" = "yes" ] && [ "$has_error" = true ]; then
        error_type=$(echo "$output" | grep "==MPISanitizer==" | head -1 | grep -o '\[.*\]')
        echo -e "  ${GREEN}[PASS]${NC} $name — detected $error_type"
        PASS=$((PASS + 1))
    elif [ "$expect_error" = "no" ] && [ "$has_error" = false ]; then
        echo -e "  ${GREEN}[PASS]${NC} $name — clean (no false positive)"
        PASS=$((PASS + 1))
    elif [ "$expect_error" = "yes" ] && [ "$has_error" = false ]; then
        echo -e "  ${RED}[FAIL]${NC} $name — expected error but none detected"
        FAIL=$((FAIL + 1))
    else
        echo -e "  ${RED}[FAIL]${NC} $name — false positive detected"
        FAIL=$((FAIL + 1))
    fi
}

echo "╔══════════════════════════════════════════════════════════╗"
echo "║        MPI Usage Sanitizer — Test Suite Runner          ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""

# ── Type Mismatch ──
echo -e "${YELLOW}▸ Type Mismatch Tests${NC}"
run_test tests/test_01_type_mismatch.c          2 yes
run_test tests/test_11_type_double_as_float.c   2 yes
run_test tests/test_12_type_correct.c           2 no
echo ""

# ── Buffer Overlap ──
echo -e "${YELLOW}▸ Buffer Overlap Tests${NC}"
run_test tests/test_05_buffer_overlap.c         2 yes
run_test tests/test_13_overlap_partial.c        2 yes
run_test tests/test_14_overlap_safe.c           2 no
echo ""

# ── Collective Ordering ──
echo -e "${YELLOW}▸ Collective Ordering Tests${NC}"
run_test tests/test_06_collective_order.c       2 yes
run_test tests/test_15_collective_root.c        2 yes
run_test tests/test_18_coll_extra.c             2 yes
run_test tests/test_19_coll_reduce_allreduce.c  2 yes
run_test tests/test_20_coll_correct_seq.c       2 no
run_test tests/test_21_coll_conditional.c       2 yes
echo ""

# ── Deadlock ──
echo -e "${YELLOW}▸ Deadlock Detection Tests${NC}"
run_test tests/test_07_deadlock.c               2 yes
run_test tests/test_08_deadlock_correct.c       2 no
run_test tests/test_09_deadlock_circular.c      3 yes
run_test tests/test_10_deadlock_small.c         2 yes
run_test tests/test_16_deadlock_4rank.c         4 yes
run_test tests/test_17_ring_correct.c           4 no
run_test tests/test_22_deadlock_halo.c          4 yes 15
echo ""

# ── Multi-Error ──
echo -e "${YELLOW}▸ Multi-Error Combination Tests${NC}"
run_test tests/test_23_combo_scatter.c          2 yes
run_test tests/test_24_combo_type_overlap.c     2 yes
echo ""

# ── Summary ──
echo "════════════════════════════════════════════════════════════"
echo -e "  Total: $TOTAL   ${GREEN}Passed: $PASS${NC}   ${RED}Failed: $FAIL${NC}"
echo "════════════════════════════════════════════════════════════"

if [ $FAIL -eq 0 ]; then
    echo -e "  ${GREEN}✓ All tests passed!${NC}"
else
    echo -e "  ${RED}✗ Some tests failed.${NC}"
    exit 1
fi
