#!/bin/bash

# Fix ptrace_scope to allow debugging on Linux
# This is required for attaching debuggers to running processes

echo "=== Fixing ptrace_scope for debugging ==="
echo ""

# Check current value
CURRENT=$(cat /proc/sys/kernel/yama/ptrace_scope)
echo "Current ptrace_scope: $CURRENT"
echo ""

if [ "$CURRENT" = "0" ]; then
    echo "✓ ptrace_scope is already set to 0 (debugging allowed)"
    exit 0
fi

echo "ptrace_scope values:"
echo "  0 = No restrictions (allows debugging)"
echo "  1 = Restricted (only parent processes can debug) [CURRENT]"
echo "  2 = Admin-only debugging"
echo "  3 = No debugging allowed"
echo ""

echo "To enable debugging, we need to set ptrace_scope to 0"
echo ""

# Offer choices
echo "Choose an option:"
echo "  1) Temporary fix (until reboot)"
echo "  2) Permanent fix (survives reboot)"
echo "  3) Cancel"
echo ""
read -p "Enter choice [1-3]: " choice

case $choice in
    1)
        echo ""
        echo "Applying temporary fix..."
        sudo sysctl -w kernel.yama.ptrace_scope=0
        if [ $? -eq 0 ]; then
            echo "✓ Success! Debugging is now enabled (until next reboot)"
            echo ""
            echo "You can now attach debuggers to running processes."
        else
            echo "✗ Failed to change ptrace_scope"
        fi
        ;;
    2)
        echo ""
        echo "Applying permanent fix..."

        # Create sysctl config file
        echo "kernel.yama.ptrace_scope=0" | sudo tee /etc/sysctl.d/10-ptrace.conf > /dev/null

        # Apply immediately
        sudo sysctl -p /etc/sysctl.d/10-ptrace.conf

        if [ $? -eq 0 ]; then
            echo "✓ Success! Debugging is now enabled permanently"
            echo ""
            echo "Configuration saved to: /etc/sysctl.d/10-ptrace.conf"
            echo "This setting will persist across reboots."
        else
            echo "✗ Failed to apply permanent fix"
        fi
        ;;
    3)
        echo "Cancelled."
        exit 0
        ;;
    *)
        echo "Invalid choice."
        exit 1
        ;;
esac

echo ""
echo "New ptrace_scope: $(cat /proc/sys/kernel/yama/ptrace_scope)"
