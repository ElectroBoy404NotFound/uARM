.text
.arm
.globl _sys_pvd_call
_sys_pvd_call:
	MOV R12, R3
	.word 0xF7BBBBBB;
	BX LR
