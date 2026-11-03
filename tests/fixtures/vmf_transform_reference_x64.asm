; Source-owned raw-bit scalar SSE operations: destination always holds lhs.
; These generic primitives do not call or include commercial game code.
.code
PUBLIC review_ordered_mul
review_ordered_mul PROC
    movd xmm0, ecx
    movd xmm1, edx
    mulss xmm0, xmm1
    movd eax, xmm0
    ret
review_ordered_mul ENDP
PUBLIC review_ordered_add
review_ordered_add PROC
    movd xmm0, ecx
    movd xmm1, edx
    addss xmm0, xmm1
    movd eax, xmm0
    ret
review_ordered_add ENDP
END
