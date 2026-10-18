#include <cstdint>

namespace
{
    constexpr std::uintptr_t GetFerryCommandsAddress = 0x0060EB50;

    using GetFerryCommandsFunction = void (__attribute__((stdcall)) *)(
        void *ferryTask,
        void *routeCommands
    );
}

// Preserve the original route lookup before inspecting whether the transport
// assisting the ferry beacon can share the commands that were found.
extern "C" void __attribute__((stdcall, used)) GetFerryCommandsAndPromoteAssistant(
    void *ferryTask,
    void *routeCommands
)
{
    reinterpret_cast<GetFerryCommandsFunction>(GetFerryCommandsAddress)(
        ferryTask,
        routeCommands
    );

    // Ferry assistant promotion is added here after the original lookup.
}
