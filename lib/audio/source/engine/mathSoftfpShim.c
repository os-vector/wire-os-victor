// bridge for softfp wwise libs calling hardfp libm.
// we use objcopy to rename wwise's references

__asm__(

#define WRAP1F(name) \
    ".global __softfp_" #name "\n" \
    ".type __softfp_" #name ", %function\n" \
    "__softfp_" #name ":\n" \
    "    push {lr}\n" \
    "    vmov s0, r0\n" \
    "    bl " #name "\n" \
    "    vmov r0, s0\n" \
    "    pop {pc}\n" \
    ".size __softfp_" #name ", .-__softfp_" #name "\n"

WRAP1F(sinf)
WRAP1F(cosf)
WRAP1F(tanf)
WRAP1F(asinf)
WRAP1F(acosf)
WRAP1F(sqrtf)
WRAP1F(expf)
WRAP1F(exp2f)
WRAP1F(logf)
WRAP1F(log10f)
WRAP1F(floorf)
WRAP1F(ceilf)

#define WRAP2F(name) \
    ".global __softfp_" #name "\n" \
    ".type __softfp_" #name ", %function\n" \
    "__softfp_" #name ":\n" \
    "    push {lr}\n" \
    "    vmov s0, r0\n" \
    "    vmov s1, r1\n" \
    "    bl " #name "\n" \
    "    vmov r0, s0\n" \
    "    pop {pc}\n" \
    ".size __softfp_" #name ", .-__softfp_" #name "\n"

WRAP2F(powf)
WRAP2F(fmodf)
WRAP2F(atan2f)

#define WRAP1D(name) \
    ".global __softfp_" #name "\n" \
    ".type __softfp_" #name ", %function\n" \
    "__softfp_" #name ":\n" \
    "    push {lr}\n" \
    "    vmov d0, r0, r1\n" \
    "    bl " #name "\n" \
    "    vmov r0, r1, d0\n" \
    "    pop {pc}\n" \
    ".size __softfp_" #name ", .-__softfp_" #name "\n"

WRAP1D(sin)
WRAP1D(cos)
WRAP1D(tan)
WRAP1D(sqrt)
WRAP1D(exp)
WRAP1D(exp2)
WRAP1D(log)
WRAP1D(log10)
WRAP1D(floor)

#define WRAP2D(name) \
    ".global __softfp_" #name "\n" \
    ".type __softfp_" #name ", %function\n" \
    "__softfp_" #name ":\n" \
    "    push {lr}\n" \
    "    vmov d0, r0, r1\n" \
    "    vmov d1, r2, r3\n" \
    "    bl " #name "\n" \
    "    vmov r0, r1, d0\n" \
    "    pop {pc}\n" \
    ".size __softfp_" #name ", .-__softfp_" #name "\n"

WRAP2D(pow)
WRAP2D(atan2)
WRAP2D(hypot)

);
