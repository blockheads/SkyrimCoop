#!/bin/bash
set -e
echo "=== Smoke Test: ptrace Environment ==="

# Check ptrace_scope
SCOPE=$(cat /proc/sys/kernel/yama/ptrace_scope 2>/dev/null || echo "N/A")
echo "ptrace_scope: $SCOPE"

# Test: ProcMemReader self-test (reads own process memory)
echo "Running ProcMem unit tests..."
xmake run TPTests "[ProcMem]"

# Test: Can we ptrace a child process?
echo "Testing ptrace on child process..."
sleep 60 &
CHILD_PID=$!
if [ -r "/proc/$CHILD_PID/mem" ] 2>/dev/null; then
    echo "PASS: /proc/$CHILD_PID/mem is readable"
else
    echo "INFO: /proc/$CHILD_PID/mem not directly readable (scope=$SCOPE), PTRACE_SEIZE needed"
fi
kill $CHILD_PID 2>/dev/null || true
wait $CHILD_PID 2>/dev/null || true

echo "=== ptrace smoke test PASSED ==="
