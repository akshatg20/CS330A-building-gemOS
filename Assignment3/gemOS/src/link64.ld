OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)
SECTIONS
{
  . = 0x200000;
  .user_text : ALIGN(4k)
  {
     user/lib.o(.text)
     user/init.o(.text)
     user/*.o(.rodata)
  }
 .user_data : ALIGN(4k)
  {
     user/*.o(.data)
  }
   . = 0x100000;
  .text : ALIGN(4k)
  {
    *(.multiboot)
    *(.text)
  }

  .data :
  {
     data = .; _data = .; __data = .;
     *(.data)
     *(.rodata)
     . = ALIGN(4096);
  }

  .bss : ALIGN(4K)
  {
    *(.bss)
    *(.bstack)
  }
 
  end = .; _end = .; __end = .;
} 
