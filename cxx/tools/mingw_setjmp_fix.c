// Workaround for MinGW _setjmp issue with SFML's FreeType
// SFML's bundled FreeType references _setjmp which may not be available
// in newer MinGW-w64 (UCRT) installations
//
// FreeType expects the old-style _setjmp(jmp_buf) signature, but MinGW now
// provides _setjmp(jmp_buf, void*) for SEH. We provide a wrapper with the
// old signature that FreeType expects.

// Include setjmp.h to get jmp_buf type, but prevent _setjmp declaration
#define _setjmp _setjmp_system_seh
#include <setjmp.h>
#undef _setjmp

// Undefine setjmp macro if it exists, then declare it as a function
#ifdef setjmp
#undef setjmp
#endif

// Declare setjmp as an actual function (not a macro)
// This allows us to take its address and call it
extern int setjmp(jmp_buf env);

// Provide _setjmp with the old single-parameter signature FreeType expects
__attribute__((used))
int _setjmp(jmp_buf env) {
    // Call setjmp directly - since we undef'd any macro, this should work
    return setjmp(env);
}

// Also provide _longjmp for completeness
__attribute__((used))
void _longjmp(jmp_buf env, int val) {
    extern void longjmp(jmp_buf env, int val);
    longjmp(env, val);
}
