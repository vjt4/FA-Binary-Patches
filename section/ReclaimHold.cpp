#include "global.h"
#include "magic_classes.h"
#include "moho.h"

namespace {

using EntId = uint32_t;

constexpr int kVirtualKeyLeftButton = 0x01;
constexpr int kVirtualKeyShift = 0x10;
constexpr int32_t kCommandModeOrder = 1;
constexpr int32_t kCommandCapsReclaim = 0x00100000;
constexpr int32_t kReclaimCommand = 0x13;
constexpr int32_t kTargetEntity = 1;
constexpr size_t kSweepTargetCapacity = 1024;

struct SweepTarget {
    int32_t type;
    EntId entity;
    Vector3f position;
};

struct SweepCommandPrefix {
    int32_t nextCommandId;
    int32_t unknown04;
    int32_t index;
    int32_t commandType;
    SweepTarget target;
};

using GetAsyncKeyStateFn = short(__stdcall *)(int);

bool reclaimHoldEnabled = true;
EntId sweepTargets[kSweepTargetCapacity]{};
size_t sweepTargetCount = 0;
bool sweepActive = false;
bool sweepTargetPreapproved = false;
EntId preapprovedTarget = 0;
EntId lastHoveredTarget = 0;
GetAsyncKeyStateFn getAsyncKeyState = nullptr;

bool IsNativeReclaimMode(const void *view) {
    if (!view)
        return false;

    const auto *bytes = reinterpret_cast<const uint8_t *>(view);
    const int32_t mode = *reinterpret_cast<const int32_t *>(bytes + 0x148);
    const int32_t commandCaps =
        *reinterpret_cast<const int32_t *>(bytes + 0x14C);
    return mode == kCommandModeOrder && commandCaps == kCommandCapsReclaim;
}

bool SweepButtonsHeld() {
    if (!getAsyncKeyState) {
        void *user32 = GetModuleHandleA("user32.dll");
        if (user32) {
            getAsyncKeyState = reinterpret_cast<GetAsyncKeyStateFn>(
                GetProcAddress(user32, "GetAsyncKeyState"));
        }
    }

    return getAsyncKeyState &&
           (getAsyncKeyState(kVirtualKeyShift) & 0x8000) != 0 &&
           (getAsyncKeyState(kVirtualKeyLeftButton) & 0x8000) != 0;
}

void ResetSweep() {
    sweepTargetCount = 0;
    sweepActive = false;
    sweepTargetPreapproved = false;
    preapprovedTarget = 0;
    lastHoveredTarget = 0;
}

bool RememberSweepTarget(EntId target) {
    for (size_t index = 0; index < sweepTargetCount; ++index) {
        if (sweepTargets[index] == target)
            return false;
    }

    if (sweepTargetCount == kSweepTargetCapacity)
        return false;

    sweepTargets[sweepTargetCount++] = target;
    return true;
}

bool UpdateSweepGesture(const void *view) {
    const bool active = reclaimHoldEnabled && IsNativeReclaimMode(view) &&
                        SweepButtonsHeld();
    if (!active && sweepActive)
        ResetSweep();
    return active;
}

bool PrepareSweepTarget(EntId target) {
    if (target == lastHoveredTarget)
        return false;

    lastHoveredTarget = target;
    if (!RememberSweepTarget(target))
        return false;

    sweepActive = true;
    sweepTargetPreapproved = true;
    preapprovedTarget = target;
    return true;
}

} // namespace

extern "C" bool __cdecl ReclaimHoldShouldSuppress(
    const SweepCommandPrefix *command)
{
    if (!command || command->commandType != kReclaimCommand ||
        command->target.type != kTargetEntity) {
        return false;
    }

    const EntId target = command->target.entity;
    if (sweepTargetPreapproved && target == preapprovedTarget) {
        sweepTargetPreapproved = false;
        preapprovedTarget = 0;
        return false;
    }

    if (!reclaimHoldEnabled || !SweepButtonsHeld()) {
        if (sweepActive)
            ResetSweep();
        return false;
    }

    if (!sweepActive)
        return false;

    return !RememberSweepTarget(target);
}

extern "C" __attribute__((naked)) void __cdecl ReclaimHoldCommandConstructor(
    void *, int32_t)
{
    asm volatile(
        "MOV EAX,[ESP+4];"
        "MOV ECX,[ESP+8];"
        "PUSH EAX;"
        "CALL 0x00552550;"
        "RET;"
    );
}

extern "C" __attribute__((naked)) void __cdecl ReclaimHoldIssueCommand(
    void *, void *, bool)
{
    asm volatile(
        "PUSH EBX;"
        "MOV EBX,[ESP+8];"
        "MOVZX EAX,BYTE PTR [ESP+16];"
        "PUSH EAX;"
        "PUSH [ESP+16];"
        "CALL 0x008B05E0;"
        "ADD ESP,8;"
        "POP EBX;"
        "RET;"
    );
}

extern "C" void __stdcall ReclaimHoldFrameUpdate(void *view)
{
    if (!view)
        return;

    auto *viewBytes = reinterpret_cast<uint8_t *>(view);
    const bool isMinimap = viewBytes[0x135] != 0;
    if (isMinimap)
        return;

    if (!UpdateSweepGesture(view))
        return;

    const bool cursorInside = viewBytes[0x275] != 0;
    if (!cursorInside)
        return;

    auto *session = *reinterpret_cast<uint8_t **>(viewBytes + 0x208);
    if (!session)
        return;

    uint8_t *cursor = session + 0x4B0;
    auto *weakOwner = *reinterpret_cast<uint8_t **>(cursor + 0x10);
    if (!weakOwner)
        return;

    uint8_t *target = weakOwner - 0x08;
    if (target[0x145] != 0)
        return;

    const EntId targetId = *reinterpret_cast<EntId *>(target + 0x44);
    if (!PrepareSweepTarget(targetId))
        return;

    alignas(4) uint8_t commandStorage[0x98]{};
    auto *command = reinterpret_cast<SweepCommandPrefix *>(commandStorage);
    ReclaimHoldCommandConstructor(command, kReclaimCommand);
    command->target.type = kTargetEntity;
    command->target.entity = targetId;
    command->target.position = *reinterpret_cast<Vector3f *>(cursor + 0x04);

    void *selection = session + 0x4A0;
    ReclaimHoldIssueCommand(selection, command, false);

    using CommandDestructorFn = void(__stdcall *)(void *);
    reinterpret_cast<CommandDestructorFn>(0x0057ABB0)(command);
}

extern "C" __attribute__((naked)) void ReclaimHoldFrameHook()
{
    asm volatile(
        "PUSHFD;"
        "PUSHAD;"
        "PUSH ESI;"
        "CALL %[frameUpdate];"
        "POPAD;"
        "POPFD;"
        "CMP DWORD PTR [ESI+0x138],0;"
        "JMP 0x00871189;"
        :
        : [frameUpdate] "i" (ReclaimHoldFrameUpdate)
        :
    );
}

extern "C" __attribute__((naked)) void ReclaimHoldIssueHook()
{
    asm volatile(
        "PUSH DWORD PTR [ESP+4];"
        "CALL %[shouldSuppress];"
        "ADD ESP,4;"
        "TEST AL,AL;"
        "JNZ ReclaimHoldSuppressReturnLabel;"
        "PUSH 0xFFFFFFFF;"
        "PUSH 0x00BB3958;"
        "JMP 0x008B05E7;"
        "ReclaimHoldSuppressReturnLabel:;"
        "RET;"
        :
        : [shouldSuppress] "i" (ReclaimHoldShouldSuppress)
        :
    );
}

ConDescReg<bool> ReclaimHoldEnabledConsole{
    "ui_ReclaimHold",
    "Hold Shift and left mouse in native reclaim mode to sweep props",
    &reclaimHoldEnabled};
