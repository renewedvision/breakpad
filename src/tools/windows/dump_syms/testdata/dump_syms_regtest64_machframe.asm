COMMENT ^
ml64 /Cp /Cx /Fm /FR /W2 /Zd /Zf /Zi /Ta dump_syms_regtest64_machframe.asm /link /debug /entry:DllMain /dll /subsystem:windows,6.0 /out:dump_syms_regtest64_machframe.dll
dump_syms dump_syms_regtest64_machframe.pdb > dump_syms_regtest64_machframe.sym
^

.CODE

test_machframe PROC FRAME
	.PUSHFRAME
	.ENDPROLOG
	ret
test_machframe ENDP

DllMain   PROC
	ret
DllMain   ENDP
END
