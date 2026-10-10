#ifndef PHOENOS_GDT_H
#define PHOENOS_GDT_H

#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE   0x1B   /* 0x18 | RPL 3 */
#define GDT_USER_DATA   0x23   /* 0x20 | RPL 3 */
#define GDT_TSS         0x28

void gdt_init(void);

#endif
