#include "global.h"
#include "LuaAPI.h"
#include "magic_classes.h"
#include "moho.h"

extern "C" void __cdecl SetExactCommandId(LuaObject *command,
                                            int32_t commandId)
{
    char value[16]{};
    sprintf_s(value, sizeof(value), "%d", commandId);
    command->SetString("ExactID", value);
}

// HOrderIDOutput jumps here from UserUnit:GetCommandQueue at 0x008C55AA.
// Keep the existing float-backed numeric ID for compatibility, then add the
// exact signed 32-bit value as a decimal string before resuming the function.
void OrderIDOutput()
{
    const char *s_ID = "ID";
    asm(
        "LEA ECX,[ESP+0x28];"
        "PUSH [EBX+4];"
        "PUSH %[s_ID];"
        "CALL 0x9081F0;"

        "LEA EAX,[ESP+0x28];"
        "PUSH [EBX+4];"
        "PUSH EAX;"
        "CALL %[setExactCommandId];"
        "ADD ESP,8;"

        "CALL 0x8B4140;"
        "JMP 0x8C55AF;"
        :
        : [s_ID] "i" (s_ID),
          [setExactCommandId] "i" (SetExactCommandId)
        :
    );
}

// Looks up and deletes the raw signed command ID without passing through
// FA's float lua_Number representation. This follows the stock DeleteCommand
// binding's command-source notification and synchronized deletion path.
extern "C" __attribute__((naked)) void __cdecl DeleteRawCommand(int32_t)
{
    asm volatile(
        "PUSH EBP;"
        "MOV EBP,ESP;"
        "SUB ESP,8;"
        "PUSH EBX;"
        "PUSH ESI;"

        "MOV EDX,[0x010A6470];"
        "TEST EDX,EDX;"
        "JZ 2f;"
        "MOV ESI,[EDX+0x3FC];"
        "TEST ESI,ESI;"
        "JZ 2f;"

        "LEA ECX,[ESI+0xCB4];"
        "LEA EBX,[EBP+8];"
        "LEA EAX,[EBP-4];"
        "CALL 0x008B6160;"

        "MOV EAX,[EBP-4];"
        "CMP EAX,[ESI+0xCB8];"
        "JE 2f;"
        "MOV ESI,[EAX+0x10];"
        "TEST ESI,ESI;"
        "JZ 2f;"

        "MOV EAX,[ESI+4];"
        "MOV ECX,[0x010C4F50];"
        "MOV EDX,[ECX];"
        "MOV EDX,[EDX+0x70];"
        "PUSH 1;"
        "PUSH EAX;"
        "LEA EAX,[EBP-8];"
        "PUSH EAX;"
        "CALL EDX;"

        "MOV EAX,[ESI+4];"
        "PUSH 1;"
        "PUSH EAX;"
        "PUSH ESI;"
        "CALL 0x008B49D0;"

        "2:;"
        "POP ESI;"
        "POP EBX;"
        "MOV ESP,EBP;"
        "POP EBP;"
        "RET;"
    );
}

static bool ParseExactCommandId(const char *text, unsigned int length,
                                int32_t &commandId)
{
    if (!text || length == 0)
        return false;

    unsigned int index = 0;
    bool negative = false;
    if (text[index] == '-') {
        negative = true;
        ++index;
    }
    if (index == length)
        return false;

    const uint32_t limit = negative ? 0x80000000u : 0x7FFFFFFFu;
    uint32_t value = 0;
    for (; index < length; ++index) {
        const unsigned int digit = static_cast<unsigned int>(text[index] - '0');
        if (digit > 9)
            return false;
        if (value > (limit - digit) / 10u)
            return false;
        value = value * 10u + digit;
    }

    if (negative) {
        commandId = value == 0x80000000u
            ? static_cast<int32_t>(0x80000000u)
            : -static_cast<int32_t>(value);
    } else {
        commandId = static_cast<int32_t>(value);
    }
    return true;
}

int LuaDeleteCommandExact(lua_State *L)
{
    unsigned int length = 0;
    const char *text = luaL_checklstring(L, 1, &length);
    int32_t commandId = 0;
    if (!ParseExactCommandId(text, length, commandId))
        return luaL_argerror(L, 1,
                             "expected an exact signed 32-bit command ID");

    DeleteRawCommand(commandId);
    return 0;
}

UIRegFunc DeleteCommandExactReg{
    "DeleteCommandExact",
    "DeleteCommandExact(exactId) - delete the ExactID string returned by "
    "UserUnit:GetCommandQueue()",
    LuaDeleteCommandExact};
