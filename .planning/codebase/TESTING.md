# Testing Patterns

**Analysis Date:** 2026-03-27

## Test Framework

**C++ Runner:**
- Framework: Catch2 for encoding/network tests
- Framework: Google Test (GTest) v1.14.0 for unit tests
- Both frameworks present in codebase for different test suites

**TypeScript Runner:**
- Framework: Angular Testing utilities with TestBed
- E2E: Playwright (`@ngx-playwright/test`)

**Run Commands:**

```bash
# C++ Tests
xmake build TPTests
./build/release/tests/TPTests              # Run all tests
./build/release/tests/TPTests "TestName"   # Run specific test

# TypeScript Tests
cd Code/skyrim_ui
pnpm lint                                  # Run linter
ng test                                    # Run tests
```

## Test File Organization

**Location:**
- C++ tests co-located with source or in `Code/tests/` and `Code/components/*/` subdirectories
- TypeScript tests co-located with components using `.spec.ts` suffix

**Naming:**
- C++: `{ComponentName}Test.cpp` or `encoding.cpp`
- TypeScript: `{ComponentName}.spec.ts`

**Structure:**

```
C++ Tests:
Code/tests/encoding.cpp              # Network message serialization tests
Code/tests/main.cpp                  # Catch2 entry point
Code/base/tests/IniSettingTests.cpp  # Settings tests
Code/components/console/ConsoleRegistryTest.cpp
Code/components/es_loader/ESLoaderTest.cpp

TypeScript Tests:
Code/skyrim_ui/src/app/components/chat/chat.component.spec.ts
Code/skyrim_ui/src/app/app.component.spec.ts
Code/one_ui/one-ui/src/app/app.component.spec.ts
```

## Test Structure

**Catch2 Organization (C++):**

```cpp
#include <catch2/catch.hpp>

TEST_CASE("Encoding factory", "[encoding.factory]")
{
    // Test implementation
    {
        AuthenticationRequest request;
        request.Token = "TesSt";

        Buffer buff(1000);
        Buffer::Writer writer(&buff);
        request.Serialize(writer);

        Buffer::Reader reader(&buff);
        const ClientMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == request.GetOpcode());
    }
}

TEST_CASE("Static structures", "[encoding.static]")
{
    GIVEN("GameId")
    {
        // GIVEN/WHEN/THEN pattern
        GameId sendObjects, recvObjects;
        sendObjects.ModId = 1456987;

        {
            // Action
            Buffer buff(1000);
            Buffer::Writer writer(&buff);
            sendObjects.Serialize(writer);

            // Assertion
            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);
            REQUIRE(sendObjects == recvObjects);
        }
    }
}
```

**GTest Organization (C++):**

```cpp
#include <gtest/gtest.h>

// Test fixture (reusable setup/teardown)
class ESLoaderTest : public ::testing::Test
{
public:
    static void SetUpTestSuite()
    {
        ESLoader::ESLoader loader;
        s_collection = loader.BuildRecordCollection();
    }

    UniquePtr<ESLoader::RecordCollection>& GetCollection() { return s_collection; }

    static UniquePtr<ESLoader::RecordCollection> s_collection;
};

UniquePtr<ESLoader::RecordCollection> ESLoaderTest::s_collection = nullptr;

// Individual test using fixture
TEST_F(ESLoaderTest, BuildRecordCollection)
{
    auto& pCollection = ESLoaderTest::GetCollection();
    ASSERT_TRUE(pCollection);
}

TEST_F(ESLoaderTest, GetMapMarkerLandmark)
{
    auto& pCollection = ESLoaderTest::GetCollection();
    REFR& mapMarker = pCollection->GetObjectRefById(0x105F3A);

    EXPECT_EQ(mapMarker.m_basicObject.m_baseId, 0x10);
    EXPECT_TRUE(mapMarker.m_markerData.m_isMarker);
}
```

**Angular TestBed Organization (TypeScript):**

```typescript
describe('AppComponent', () => {
  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [
        RouterTestingModule
      ],
      declarations: [
        AppComponent
      ],
    }).compileComponents();
  });

  it('should create the app', () => {
    const fixture = TestBed.createComponent(AppComponent);
    const app = fixture.componentInstance;
    expect(app).toBeTruthy();
  });

  it(`should have as title 'one-ui'`, () => {
    const fixture = TestBed.createComponent(AppComponent);
    const app = fixture.componentInstance;
    expect(app.title).toEqual('one-ui');
  });

  it('should render title', () => {
    const fixture = TestBed.createComponent(AppComponent);
    fixture.detectChanges();
    const compiled = fixture.nativeElement as HTMLElement;
    expect(compiled.querySelector('.content span')?.textContent).toContain('one-ui app is running!');
  });
});
```

**Patterns:**

- **Setup**: Test fixtures use `SetUpTestSuite()` (GTest) or `beforeEach()` (Angular)
- **Teardown**: GTest uses `TearDown()` for cleanup
- **Assertions**:
  - Catch2: `REQUIRE()` for mandatory assertions, `CHECK()` for non-fatal
  - GTest: `ASSERT_*` (fatal) vs `EXPECT_*` (non-fatal)
  - Angular: `expect()` with matchers like `.toBeTruthy()`, `.toEqual()`

## Mocking

**C++ Framework:**
- No dedicated mocking framework detected
- Tests use real objects or stubs
- Example: `ConsoleRegistryTest` creates actual `ConsoleRegistry` instances
- Buffer-based serialization tests use real `Buffer::Reader` and `Buffer::Writer`

**C++ Patterns - Stubs and Fakes:**

```cpp
// From ConsoleRegistryTest.cpp - using real objects
class ConsoleRegistryTest : public ::testing::Test {
    void SetUp() override {
        spdlog::stdout_color_mt("Test");  // Real logger setup
    }
};

TEST_F(ConsoleRegistryTest, RegisterCommand) {
    auto r{std::make_unique<ConsoleRegistry>("Test")};  // Real object
    r->RegisterCommand<bool, bool>(
        "name", "description",
        [&](ArgStack& stack) {
            EXPECT_TRUE(stack.Pop<bool>());
        });
}
```

**TypeScript Mocking:**
- Angular TestBed provides dependency injection for mocks
- Can use `jasmine.createSpyObj()` for method spies
- Example from settings component tests:
  - Minimal spying shown in examined spec files
  - Would use `TestBed.inject()` to get services
  - Can spy on service methods with `spyOn()`

**What to Mock:**
- External services (HTTP calls, network I/O)
- Heavy initialization (file I/O, database connections)
- Time-dependent code (use `jasmine.clock()` or `fakeAsync()`)

**What NOT to Mock:**
- Core business logic (test the real implementation)
- Data structures and messages (use real serialization)
- Model objects and enums
- Angular built-in directives and pipes

## Fixtures and Factories

**C++ Test Data:**

```cpp
// From encoding.cpp - setting up test data
ActionEvent sendAction, recvAction;
sendAction.ActionId = 42;
sendAction.State1 = 6547;
sendAction.Tick = 48;
sendAction.ActorId = 12345678;
sendAction.EventName = "test";
sendAction.IdleId = 87964;
sendAction.State2 = 8963;
sendAction.TargetEventName = "toast";
sendAction.TargetId = 963741;
sendAction.Type = 4;

// Serialization round-trip test
Buffer buff(1000);
Buffer::Writer writer(&buff);
sendAction.GenerateDifferential(recvAction, writer);

Buffer::Reader reader(&buff);
recvAction.ApplyDifferential(reader);
REQUIRE(sendAction == recvAction);
```

```cpp
// From encoding.cpp - Mods fixture
Mods sendMods, recvMods;
Buffer buff(1000);
Buffer::Writer writer(&buff);

sendMods.ModList.push_back({"Hello", 42});
sendMods.ModList.push_back({"Hi", 14});
sendMods.ModList.push_back({"Test", 8});
sendMods.ModList.push_back({"Toast", 49});

sendMods.Serialize(writer);
Buffer::Reader reader(&buff);
recvMods.Deserialize(reader);
REQUIRE(sendMods == recvMods);
```

```cpp
// From encoding.cpp - AnimationVariables fixture
AnimationVariables vars, recvVars;
vars.Booleans.resize(76);
String testString("\xDE\xAD\xBE\xEF\xDE\xAD\xBE\xEF\x76\xB");
vars.String_to_VectorBool(testString, vars.Booleans);

vars.Floats.push_back(1.f);
vars.Floats.push_back(7.f);
vars.Floats.push_back(12.f);

vars.Integers.push_back(0);
vars.Integers.push_back(12000);
```

**Location:**
- Fixtures defined inline in test functions
- No separate fixture files
- Test data embedded in `TEST_CASE()` or `TEST_F()` bodies

## Coverage

**Requirements:** Not enforced

**View Coverage:**
- No coverage tool integration detected in examined build files
- Can be added via `xmake config --coverage=y` (not currently configured)

## Test Types

**Unit Tests:**
- Scope: Individual message types, serialization, components
- Approach: Test expected behavior and edge cases
- Examples:
  - `Vector3_NetQuantize` serialization/deserialization
  - `GameId` encoding
  - `AnimationVariables` differential serialization
  - Console registry command registration and execution

**Integration Tests:**
- Scope: Message factories, full packet serialization
- Approach: Test message creation and multiple serialization/deserialization roundtrips
- Examples from `encoding.cpp`:
  - `AuthenticationRequest` with mod list serialization
  - `AssignCharacterRequest` with animations and position
  - `ClientReferencesMoveRequest` with movement variables

**E2E Tests:**
- Framework: Playwright (present in `Code/skyrim_ui/playwright/`)
- Not heavily used in examined codebase
- Can be run with `ng e2e`

## Common Patterns

**Async Testing (C++):**
- Not detected in examined tests (synchronous buffer I/O)
- Would use Catch2 sections or GTest async helpers if needed

**Async Testing (TypeScript):**

```typescript
// Angular TestBed with async/fakeAsync
it('should load data', fakeAsync(() => {
    // Set up
    const fixture = TestBed.createComponent(MyComponent);

    // Action
    fixture.detectChanges();
    tick();  // Simulate passage of time

    // Assert
    fixture.detectChanges();
    expect(fixture.nativeElement.innerHTML).toContain('data');
}));
```

**Error Testing:**

```cpp
// From ConsoleRegistryTest.cpp - testing error conditions
TEST_F(ConsoleRegistryTest, RegisterCommand) {
    auto r{std::make_unique<ConsoleRegistry>("Test")};
    r->RegisterCommand<bool, bool>("name", "description", [...]);

    // Test valid execution
    r->TryExecuteCommand("/name true false");

    // Test invalid/error conditions
    r->TryExecuteCommand("test0 7");              // Missing slash
    r->TryExecuteCommand("/test \xe2\x28\xa1");   // Invalid UTF-8
    r->TryExecuteCommand("/test0 true");          // Wrong argument type
}
```

**Serialization Round-Trip Testing (C++):**
- Create object with test data
- Serialize to buffer
- Deserialize from buffer
- Assert equality
- Pattern used extensively in `Code/tests/encoding.cpp`

## Test Organization

**Naming Conventions:**
- C++ test suites: Use `TEST_CASE()` with descriptive names and tags like `[encoding.factory]`, `[encoding.static]`, `[encoding.differential]`
- TypeScript: Use `describe()` blocks and `it()` with clear intent descriptions

**Test Tags (Catch2):**
- `[encoding.factory]` - Message factory tests
- `[encoding.static]` - Static structure serialization
- `[encoding.differential]` - Differential serialization/deserialization
- `[encoding.packets]` - Full packet tests
- `[encoding.string_cache]` - String cache updates

## Coverage Gaps

**Untested Areas:**

1. **C++ Network Transport:**
   - Files: `Code/client/Services/TransportService.cpp`, `Code/libraries/networking/Client.cpp`
   - What's not tested: Actual network I/O, connection handling, message dispatch
   - Risk: Protocol bugs in UDP communication could go undetected
   - Priority: High

2. **C++ Game Integration:**
   - Files: `Code/client/Games/Skyrim/`, `Code/client/Services/CharacterService.cpp`
   - What's not tested: SKSE hooks, game memory access, actor synchronization
   - Risk: Game integration bugs surface only in live gameplay
   - Priority: High (difficult to test without game runtime)

3. **TypeScript Components:**
   - Files: `Code/skyrim_ui/src/app/components/`
   - What's not tested: Most component logic, event handling, animations
   - Risk: UI bugs go undetected until runtime
   - Priority: Medium (E2E tests could cover this)

4. **Server Authority Logic:**
   - Files: `Code/server/Services/`, `Code/server/World.cpp`
   - What's not tested: P2P authority rules, packet handling, player synchronization
   - Risk: Multiplayer consistency bugs
   - Priority: High

---

*Testing analysis: 2026-03-27*
