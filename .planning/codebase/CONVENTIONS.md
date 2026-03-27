# Coding Conventions

**Analysis Date:** 2026-03-27

## Naming Patterns

**Variables:**
- Camel case starting with lowercase: `someVariableName`
- Function arguments prefixed with `a`: `aFunctionArgument`
- Const variables prefixed with `c`: `const int cSomeInt`
- Pointers prefixed with `p`: `int* pSomePointer`
- Combined rules for const pointers: `const int* acpSomeArgument`
- Static variables prefixed with `s_`: `static int s_someInt`
- Global variables prefixed with `g_`: `extern int g_someGlobalInt`

**Classes and Structs:**
- PascalCase starting with uppercase: `class SomeClass`, `struct TransportService`
- Class attributes prefixed with `m_`: `int* m_pSomeMemberPointer`
- Example: `TransportService` has `m_world`, `m_dispatcher`, `m_connected`

**Functions:**
- PascalCase starting with uppercase: `void SomeFunc()`, `bool IsOnline()`
- Handler methods prefixed with `Handle`: `HandleUpdate()`, `HandleConnected()`
- Getter methods use `Get` or direct property access: `GetWorld()`, `IsOnline()`

**TypeScript/Angular:**
- camelCase for variables and functions: `autoScroll`, `messageTypeToClassName()`
- PascalCase for classes and interfaces: `ChatComponent`, `ChatMessage`
- Component selectors use kebab-case with `app` prefix: `app-chat`, `app-settings`
- Directive selectors use camelCase with `app` prefix: `appDirective`

## Code Style

**Formatting (C++):**
- Tool: `clang-format`
- Config file: `.clang-format`
- Key settings:
  - BasedOnStyle: LLVM
  - IndentWidth: 4
  - ColumnLimit: 300
  - BreakBeforeBraces: Allman (opening brace on new line)
  - AlignAfterOpenBracket: AlwaysBreak
  - AllowShortFunctionsOnASingleLine: InlineOnly
  - AllowShortLambdasOnASingleLine: Inline
  - PointerAlignment: Left

**Formatting (TypeScript):**
- Tool: Prettier
- Config: `Code/skyrim_ui/.prettierrc`
- Key settings:
  - tabWidth: 2
  - useTabs: false
  - singleQuote: true
  - semi: true
  - bracketSpacing: true
  - arrowParens: avoid
  - trailingComma: all
  - printWidth: 80

**Linting (C++):**
- Tool: clang-format only (no separate linter in use)

**Linting (TypeScript):**
- Tool: ESLint
- Config: `Code/skyrim_ui/.eslintrc.js`
- Extends: `@typescript-eslint/recommended` and `@typescript-eslint/recommended-requiring-type-checking`
- Key enforced rules:
  - `@typescript-eslint/explicit-function-return-type`: error (all functions must declare return type)
  - `@typescript-eslint/explicit-module-boundary-types`: error (exported functions must declare types)
  - `@typescript-eslint/naming-convention`: error (enforces camelCase/PascalCase)
  - `@typescript-eslint/no-explicit-any`: off (allows use of `any` type)
  - `@typescript-eslint/no-non-null-assertion`: error (disallows `!` operator)
  - `prefer-arrow/prefer-arrow-functions`: error (prefer arrow functions over function expressions)
  - `@angular-eslint/component-class-suffix`: error (component classes must end with `Component`)
  - `@angular-eslint/component-selector`: error (component selectors must be elements with `app-` prefix, kebab-case)
  - `max-len`: [error, code: 140]
  - `no-console`: error (except for allowed methods like log, warn, error)
  - `no-var`: error (enforce `const`/`let` over `var`)
  - `prefer-const`: error (enforce `const` when possible)

## Import Organization

**C++ Order:**
1. System includes: `#include <Windows.h>`, `#include <memory>`
2. TiltedPhoques framework: `#include <TiltedCore/Stl.hpp>`, `#include <Server.hpp>`
3. Project headers: `#include "Services/CharacterService.h"`, `#include "Events/UpdateEvent.h"`
4. Local headers: `#include "StringCache.h"`
5. Empty line between groups

**TypeScript Order:**
1. Angular imports: `import { Component, ... } from '@angular/core'`
2. RxJS imports: `import { takeUntil, map } from 'rxjs'`
3. Third-party libraries: `import { TranslocoService } from '@ngneat/transloco'`
4. Project services: `import { ChatService } from 'src/app/services/chat.service'`
5. Project models/interfaces: `import { ChatMessage } from './message-history'`

**Path Aliases:**
- TypeScript uses `src/` for project root references
- Example: `import { ClientService } from 'src/app/services/client.service'`

## Error Handling

**C++:**
- No exceptions allowed (code style guideline)
- Use nothrow versions of functions
- Return success/failure via boolean or result codes
- Example from `TransportService`: functions return `bool`, use noexcept specifiers
- Validate preconditions before operations (e.g., checking if pointer is null before dereferencing)
- Log errors via spdlog: `spdlog::error("message")`, `spdlog::warn("message")`

**TypeScript:**
- Use proper error handling in observables via `catchError` or error callbacks
- Validate component inputs in lifecycle hooks (`ngOnInit`)
- Handle missing data gracefully (e.g., `if (this.entryRefQuery && this.entryRefQuery.first)`)

## Logging

**Framework:** spdlog (C++), console (TypeScript)

**C++ Patterns:**
- Include header: `#include <spdlog/spdlog.h>`
- Log levels: `spdlog::info()`, `spdlog::warn()`, `spdlog::error()`, `spdlog::debug()`
- Create logger by name: `spdlog::stdout_color_mt("LoggerName")`
- Example from `HostService.cpp`:
  ```cpp
  spdlog::info("[HostService] Starting P2P host session on port {} for up to {} players", aPort, aMaxPlayers);
  spdlog::warn("[HostService] Already hosting a session");
  ```
- Custom log levels in service classes:
  ```cpp
  static spdlog::level::level_enum m_logLevel;
  template <typename... Args> static inline void Spdlog(spdlog::format_string_t<Args...> aFmt, Args&&... args) {
      spdlog::log(m_logLevel, aFmt, std::forward<Args>(args)...);
  }
  ```

**TypeScript Patterns:**
- Use `console.log()`, `console.warn()`, `console.error()` (allowed by ESLint config)
- Angular services can use dependency injection for custom loggers
- Example from chat component: component logs don't appear in examined code, but Angular follows standard patterns

## Comments

**When to Comment:**
- Document public API contracts (what function does, preconditions, postconditions)
- Explain non-obvious algorithm choices or workarounds
- Mark TODO items with author attribution when significant: `// TODO(username): description`

**JSDoc/TSDoc (TypeScript):**
- TSDoc format not strictly required (ESLint has `jsdoc/no-types` rule set to error)
- Keep comments as text without type annotations
- Example from services: minimal comments, mostly self-explanatory code

**C++ Documentation:**
- Use forward slashes for inline comments: `// comment`
- Block comments for multi-line: `/* ... */`
- Doxygen comments for public APIs: `/** ... */`
- Example from `TransportService.h`:
  ```cpp
  /**
   * @brief Handles communication with the server.
   */
  struct TransportService : Client
  ```

## Function Design

**Size:** Functions should be focused and reasonably sized (code review standard, no hard limit enforced)

**Parameters:**
- Use argument prefix `a` for all parameters
- Pass complex objects by const reference: `const AuthenticationResponse& acMessage`
- Use noexcept where possible: `void OnConnected() override noexcept;`

**Return Values:**
- Prefer returning `bool` for success/failure
- Use `[[nodiscard]]` attribute for important return values:
  ```cpp
  [[nodiscard]] bool IsOnline() const noexcept { return m_connected; }
  ```
- Const references for returning internal data to prevent modification:
  ```cpp
  const Info& GetInfo() const noexcept { return m_info; }
  ```

## Module Design

**Exports:**
- C++ uses header files (.h) with struct/class definitions
- Services inherit from base classes or extend TiltedPhoques framework classes
- Example: `struct TransportService : Client` in `TransportService.h`

**Barrel Files:**
- Not used in C++
- TypeScript services are imported individually from their paths

**Struct vs Class:**
- C++ uses `struct` for most definitions (POD-like structures with public members or services)
- Example: `struct TransportService`, `struct World`, `struct PlayerService`

## Special Naming Conventions

**Event Handlers:**
- Prefix with `Handle`: `HandleUpdate()`, `HandleConnected()`, `HandleAuthenticationResponse()`
- Placed in `protected` or `private` sections of services
- Connected via entt dispatcher subscriptions

**Component Types:**
- Suffix with `Component`: `LocalComponent`, `RemoteComponent`, `CharacterComponent`
- Located in `Code/client/Components/` or `Code/server/Components/`

**Services:**
- Suffix with `Service`: `TransportService`, `CharacterService`, `InventoryService`
- Located in `Code/client/Services/` or `Code/server/Services/`

**Messages:**
- Suffix with `Request` or `Response`: `AuthenticationRequest`, `AssignCharacterRequest`
- Located in `Code/encoding/Messages/`
- Inherit from `ClientMessage` or `ServerMessage`

**Events:**
- Suffix with `Event`: `UpdateEvent`, `ConnectedEvent`, `DisconnectedEvent`
- Located in `Code/client/Events/` or `Code/server/Events/`
- Dispatched via entt::dispatcher

## Build and Compilation

**C++20 Features:**
- C++20 is fully supported and encouraged
- Use modern features: concepts, ranges, structured bindings
- Avoid templates when they cause compilation bloat (code guideline)

**Copy/Move Prevention:**
- Use `TP_NOCOPYMOVE` macro in service classes to prevent accidental copying:
  ```cpp
  struct TransportService : Client {
      TP_NOCOPYMOVE(TransportService);
  };
  ```

**Preprocessor:**
- Avoid excessive macros
- Use `#pragma once` for header guards (no `#ifndef` guards)
- Example: all headers start with `#pragma once`

---

*Convention analysis: 2026-03-27*
