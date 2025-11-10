# Debugging SkyrimCoop on Linux

This guide explains how to debug SkyrimTogether.exe running through Proton on Linux using VSCode.

## Overview

Since SkyrimTogether.exe is a Windows executable, we need to run it through Proton (Wine-based compatibility layer). We use the `PROTON_WAIT_ATTACH=1` environment variable to pause the process at startup, allowing us to attach a debugger before any code executes.

## Prerequisites

1. **Build the project with debug symbols**
   ```bash
   xmake config -m debug
   xmake -y
   ```
   This generates PDB files alongside your executables.

2. **Install required debugger** (choose one):
   - **GDB**: Usually pre-installed on most Linux distributions
     ```bash
     sudo apt install gdb  # Debian/Ubuntu
     sudo dnf install gdb  # Fedora
     ```
   - **LLDB**: Better PDB support, recommended
     ```bash
     sudo apt install lldb  # Debian/Ubuntu
     sudo dnf install lldb  # Fedora
     ```

3. **Install VSCode C++ extension**
   - For GDB: Install "C/C++" extension by Microsoft
   - For LLDB: Install "CodeLLDB" extension by Vadim Chugunov

4. **Have Proton installed** (comes with Steam)
   - Default location: `~/.steam/debian-installation/steamapps/common/Proton X.X/`

## Quick Start

### Method 1: Using the Launch Script (Recommended)

1. **Launch the app with PROTON_WAIT_ATTACH**:
   ```bash
   ./debug_launch_proton.sh
   ```

2. **The script will**:
   - Set up Proton environment
   - Launch SkyrimTogether.exe
   - Pause and wait for debugger
   - Display the process ID

3. **Attach the debugger in VSCode**:
   - Press `Ctrl+Shift+D` (open Run and Debug panel)
   - Select either:
     - `Proton: Wait and Attach (GDB)` - for GDB debugger
     - `Proton: Wait and Attach (LLDB)` - for LLDB debugger
   - Press `F5` or click "Start Debugging"
   - Select `SkyrimTogether.exe` from the process list

4. **Set breakpoints and debug**:
   - Set breakpoints in your source files
   - Use the debug controls (Continue, Step Over, Step Into, etc.)
   - Inspect variables in the Debug panel

### Method 2: Manual Launch

1. **Set environment variables**:
   ```bash
   export PROTON_WAIT_ATTACH=1
   export PROTON_LOG=1
   export STEAM_COMPAT_CLIENT_INSTALL_PATH="${HOME}/.steam/steam"
   export STEAM_COMPAT_DATA_PATH="./proton_prefix"
   ```

2. **Launch with Proton**:
   ```bash
   cd build/windows/x64/debug
   ~/.steam/debian-installation/steamapps/common/Proton\ 10.0/proton run SkyrimTogether.exe
   ```

3. **Find the process ID**:
   ```bash
   pgrep -a SkyrimTogether
   ```

4. **Attach debugger from VSCode** (same as Method 1, step 3)

## Available VSCode Debug Configurations

### Attach Configurations (for PROTON_WAIT_ATTACH)

1. **Proton: Wait and Attach (GDB)**
   - Uses GDB debugger
   - Works with PDB symbols (limited support)
   - Good for basic debugging

2. **Proton: Wait and Attach (LLDB)**
   - Uses LLDB debugger
   - Better PDB symbol support
   - Recommended for complex debugging

### Direct Launch Configurations (Alternative)

These launch the app directly without PROTON_WAIT_ATTACH:

1. **Wine: LLDB Launch SkyrimTogether**
   - Launches directly with LLDB attached
   - No need for separate launch script

2. **Linux: LLDB Debug SkyrimTogether (Full Symbols)**
   - Full symbol support
   - Source map configured

3. **Linux: GDB Debug SkyrimTogether (PDB Symbols)**
   - GDB-based direct launch

## Debugging Workflow

### Setting Breakpoints

1. Open the source file in VSCode
2. Click in the gutter (left of line numbers) to set a breakpoint
3. Red dot appears indicating breakpoint is set
4. When debugging starts, the dot becomes verified

### Common Debug Actions

- **Continue (F5)**: Resume execution until next breakpoint
- **Step Over (F10)**: Execute current line, skip function internals
- **Step Into (F11)**: Enter function calls
- **Step Out (Shift+F11)**: Exit current function
- **Restart (Ctrl+Shift+F5)**: Restart debugging session
- **Stop (Shift+F5)**: Terminate debugging session

### Inspecting Variables

- Hover over variables in code to see values
- Use the **Variables** panel to browse local/global variables
- Use the **Watch** panel to monitor specific expressions
- Use the **Call Stack** panel to navigate execution frames

## Troubleshooting

### Problem: "Cannot find process SkyrimTogether.exe"

**Solution**: The process may not have started yet. Wait a few seconds and try again, or use:
```bash
watch -n 1 pgrep -a SkyrimTogether
```

### Problem: "Debug symbols not loading"

**Solution**: Verify PDB files exist:
```bash
ls -la build/windows/x64/debug/*.pdb
```

Ensure source path mappings are correct in launch.json:
- `Z:\workspace` → `${workspaceFolder}`
- `c:/projects/SkyrimCoop` → `${workspaceFolder}`

### Problem: "Breakpoints not hitting"

**Possible causes**:
1. Code may be optimized out (use debug build)
2. Source paths don't match (check sourceMap in launch.json)
3. PDB symbols don't match binary (rebuild clean)

**Solution**:
```bash
xmake clean
xmake config -m debug
xmake -y
```

### Problem: "Cannot step through code / jumps around"

**Cause**: Wine's debugging support has limitations with single-stepping.

**Workaround**: Use breakpoints instead of excessive stepping.

### Problem: "Variables show <optimized out>"

**Cause**: Even in debug mode, some compiler optimizations may occur.

**Solution**: Add to your xmake.lua:
```lua
add_cxxflags("-O0", "-g3", "-ggdb3")
```

### Problem: "Call stack is incomplete"

**Cause**: Wine/Windows boundary causes stack unwinding issues.

**Solution**: Consider using the WineHQ custom GDB fork:
```bash
git clone https://gitlab.winehq.org/rbernon/binutils-gdb
cd binutils-gdb
./configure
make all-gdb
sudo make install-gdb
```

Then source Wine's unwinder in `~/.gdbinit`:
```
source /path/to/wine/tools/gdbinit.py
```

## Advanced: Using winedbg Server Mode

For more advanced debugging, you can use winedbg in GDB server mode:

1. **Launch with winedbg**:
   ```bash
   winedbg --gdb --no-start --port 2159 SkyrimTogether.exe
   ```

2. **Add a VSCode task** to `.vscode/tasks.json`:
   ```json
   {
       "label": "launch winedbg server",
       "type": "shell",
       "command": "cd build/windows/x64/debug && winedbg --gdb --no-start --port 2159 SkyrimTogether.exe",
       "isBackground": true
   }
   ```

3. **Add launch configuration** to `.vscode/launch.json`:
   ```json
   {
       "name": "winedbg GDB Server",
       "type": "cppdbg",
       "request": "launch",
       "program": "${workspaceFolder}/build/windows/x64/debug/SkyrimTogether.exe",
       "MIMode": "gdb",
       "miDebuggerServerAddress": "localhost:2159",
       "preLaunchTask": "launch winedbg server"
   }
   ```

## Environment Variables Reference

| Variable | Purpose | Example |
|----------|---------|---------|
| `PROTON_WAIT_ATTACH=1` | Pause process until debugger attaches | Required for attach method |
| `PROTON_LOG=1` | Enable Proton logging | Useful for troubleshooting |
| `WINEDEBUG=-all` | Disable Wine debug messages | Reduces noise in output |
| `PROTON_DUMP_DEBUG_COMMANDS=1` | Dump debug configuration to /tmp | For advanced setup |
| `STEAM_COMPAT_CLIENT_INSTALL_PATH` | Path to Steam installation | `~/.steam/steam` |
| `STEAM_COMPAT_DATA_PATH` | Proton prefix location | `./proton_prefix` |

## Tips and Best Practices

1. **Use LLDB over GDB** for better PDB support
2. **Build with full debug symbols**: `xmake config -m debug`
3. **Set breakpoints before attaching** for faster debugging
4. **Use conditional breakpoints** to break only on specific conditions
5. **Check Proton logs** if app doesn't start: `tail -f /tmp/proton_*.log`
6. **Keep symbol files (.pdb) with executables** - never delete them

## See Also

- [Proton Debugging Documentation](https://github.com/ValveSoftware/Proton/blob/proton_9.0/docs/DEBUGGING.md)
- [VSCode C++ Debugging Guide](https://code.visualstudio.com/docs/cpp/cpp-debug)
- [Wine Debugging Guide](https://wiki.winehq.org/Winedbg)
- Project build guide: [CLAUDE.md](./CLAUDE.md)


Key Resources for Debugging Wine with PDB Symbols
1. Primary Resource: "Debugging Wine with LLDB and VSCode"
URL: https://werat.dev/blog/debugging-wine-with-lldb-and-vscode/ This is the definitive guide written by the engineer who created the Wine DYLD patches you just applied. It covers:
Complete setup instructions
What works and what doesn't
PDB symbol support limitations
Source mapping for debugging
VS Code integration
2. Google Stadia's Official LLDB Fork
URL: https://github.com/googlestadia/vsi-lldb This is where the Wine patches originally came from. Google Stadia used this for production debugging of Windows games running on Linux.
3. Wine PDB Symbol Store Tutorial
URL: https://gist.github.com/rmi1974/90f14258c622fe57a9b70f6cabe69d66 Shows how to create Wine PDB symbol stores for symbol server usage.
4. Building Wine with PDB Files
URL: https://gist.github.com/CodingKoopa/26bd17b5ee6af866cf5f330933e706fe Tutorial on building Wine itself with PDB debug symbols using llvm-mingw.
Important Findings
Based on the research, here's what I learned:
✅ It CAN Work (with caveats):
Google Stadia did use this in production for debugging Windows games on Linux
The engineer who wrote the guide successfully debugs Wine apps with LLDB + PDB
BUT: They mention it requires:
Wine built with CFI patches (Call Frame Information)
Proper Wine version compatibility
Sometimes multiple attach attempts
⚠️ Critical Missing Piece:
The guide mentions: "Without Wine dynamic loader support, LLDB cannot resolve call stacks or identify loaded modules" But you HAVE the Wine dynamic loader plugin compiled! The crash you're experiencing might be due to:
Wine version mismatch - The patches were designed for specific Wine versions
Missing CFI support in Wine - Your Wine/Proton might not have the CFI patches
Proton-specific incompatibility - Proton is a fork of Wine with modifications
🔧 What to Try Next:
Check your Wine/Proton version's CFI support:
# Check which Wine you're using
wine --version

# Or for Proton:
ls -la ~/.steam/debian-installation/steamapps/common/Proton*/
The article specifically mentions needing Wine merge request #1065 for CFI directives. Your Proton installation might not have this. Would you like me to help you:
Check if your Proton has the required CFI patches?
Try using regular Wine instead of Proton?
Look into alternative debugging approaches mentioned in these resources (like winedbg or GDB)?