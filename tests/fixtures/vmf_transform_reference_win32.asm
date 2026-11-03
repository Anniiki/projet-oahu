; Source-owned raw-bit scalar SSE operations, independent cdecl primitives.
.686
.XMM
.model flat, C
.code
PUBLIC review_ordered_mul
review_ordered_mul PROC
    movd xmm0, dword ptr [esp+4]
    movd xmm1, dword ptr [esp+8]
    mulss xmm0, xmm1
    movd eax, xmm0
    ret
review_ordered_mul ENDP
PUBLIC review_ordered_add
review_ordered_add PROC
    movd xmm0, dword ptr [esp+4]
    movd xmm1, dword ptr [esp+8]
    addss xmm0, xmm1
    movd eax, xmm0
    ret
review_ordered_add ENDP
END
