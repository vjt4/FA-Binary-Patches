#include "../define.h"
asm(
  ".section h0; .set h0,0x871182;"
  "JMP " QU(ReclaimHoldFrameHook) ";"
  "NOP;"
  "NOP;"

  ".section h1; .set h1,0x8B05E0;"
  "JMP " QU(ReclaimHoldIssueHook) ";"
  "NOP;"
  "NOP;"
);
