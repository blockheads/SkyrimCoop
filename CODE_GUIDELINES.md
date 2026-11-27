# Code Guidelines

## Language

We are using C++23: clang-cl with the MSVC STL 14.44 (`/std:c++latest`) for the plugin, clang 18 with libstdc++ 13 for the Linux tests. Use what both have (`std::expected` and `std::format` yes; `std::ranges::to` only in Windows-only code; check newer library features against libstdc++ 13).

Please try to use templates responsibly, we don't want compilation times to explode and to deal with bloated binaries.

Try to follow SRP as much as possible, a huge class containing tons of functionnalities is not better than many small components, it's easier to re-use them and to extend.

## Naming

### Variables

The first letter is lower case, other words must start with an upper case : ``someVariableName``.

Function arguments must be prefixed with an ``a`` for 'argument' : ``aFunctionArgument``.

Const variables must be prefixed with a ``c`` for 'const' : ``const int cSomeInt;``

Pointers must be prefixed with a ``p`` for 'pointer' : ``int* pSomePointer``.

The rules above must be used together for example ``void SomeFunc(const int* acpSomeArgument)``.

Static variables must be prefixed with ``s_`` : ``static int s_someInt;``.

Global variables must be prefixed with ``g_`` : ``extern int g_someGlobalInt;``.

### Classes

Class names must start with an upper case : ``class SomeClass``.

Class attributes must be prefixed with ``m_`` and must use the same rules as variables : ``int* m_pSomeMemberPointer;``.

### Functions

All functions must start with an upper case : ``void SomeFunc();``.

## Generalities

Names must be self explanatory, ``size_t a;`` is not acceptable, ``size_t incomingPacketCount;`` is good.

``auto`` is allowed when dealing with long names, it is not accepted for primitive types as we don't want the compiler to give us a signed int when we are using it as unsigned.

Don't use java style blocks, a ``{`` needs to be on a new line.

Don't use exceptions, don't use STL code that can throw, use the nothrow version if available or the unsafe version.

## Engine object lifetime

The engine owns every object we touch and frees it on its own schedule.

1. Across frames keep a form id or a handle and look the object up when used. Keep a `NiPointer` or
   `BSTSmartPointer` only to hold something alive on purpose.
2. Engine pointers are locals and parameters: not a field, a static, a container element or a capture of queued
   work (`Runner().Queue`, `MainLoopTasks::Queue`, an event through `Runner().Trigger`).
3. A temporary form (id `0xFF......`) is never recorded anywhere that outlives the frame.
4. An address is a key only in one of the six situations below.
5. A callback handed to the engine is cancelled before the object behind it goes.
6. Queued work captures by value, never by reference.

| An address is a safe key when | Example |
| --- | --- |
| The object lives all session (code, singletons, registered natives) | `NativeReach`, `CodeReach` |
| The entry lives only inside one engine call | `FreedWornList` |
| The whole table is rebuilt from form ids every frame | `s_copyControllers` |
| Every new keyed object passes our hook, which sets or clears its entry | `ScriptTags::m_calls`, `m_functors` |
| A hook on the object's destruction removes the entry | none yet |
| The entry holds a reference that keeps the object alive | `AnimationTrace::m_graphs` |

## Commit Naming
Preface commit messages with one of the following tags:
* `feat:` for new features
* `tweak:` for tweaks to an existing system
* `fix:` for bug fixes and crash fixes
* `refactor:` for refactoring