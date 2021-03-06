#include "ps.h"
#include <driver/ps2.h>
#include <driver/sd.h>
#include <driver/vga.h>
#include <zjunix/bootmm.h>
#include <zjunix/buddy.h>
#include <zjunix/slab.h>
#include <zjunix/time.h>
#include <zjunix/utils.h>

char ps_buffer[64];
int ps_buffer_index;

void test_syscall4() {
    asm volatile(
        "li $a0, 0x00ff\n\t"
        "li $v0, 4\n\t"
        "syscall\n\t"
        "nop\n\t");
}

void ps() {
    kernel_printf("Press any key to enter shell.\n");
    kernel_getchar();
    char c;
    ps_buffer_index = 0;
    ps_buffer[0] = 0;
    kernel_clear_screen(31);
    kernel_puts("PowerShell\n", 0xfff, 0);
    kernel_puts("PS>", 0xfff, 0);
    while (1) {
        c = kernel_getchar();
        if (c == '\n') {
            ps_buffer[ps_buffer_index] = 0;
            if (kernel_strcmp(ps_buffer, "exit") == 0) {
                ps_buffer_index = 0;
                ps_buffer[0] = 0;
                kernel_printf("\nPowerShell exit.\n");
            } else
                parse_cmd();
            ps_buffer_index = 0;
            kernel_puts("PS>", 0xfff, 0);
        } else if (c == 0x08) {
            if (ps_buffer_index) {
                ps_buffer_index--;
                kernel_putchar_at(' ', 0xfff, 0, cursor_row, cursor_col - 1);
                cursor_col--;
                kernel_set_cursor();
            }
        } else {
            if (ps_buffer_index < 63) {
                ps_buffer[ps_buffer_index++] = c;
                kernel_putchar(c, 0xfff, 0);
            }
        }
    }
}

void parse_cmd() {
    unsigned int result = 0;
    char dir[32];
    char c;
    kernel_putchar('\n', 0, 0);
    char sd_buffer[8192];
    int i = 0;
    char *param;
    for (i = 0; i < 63; i++) {
        if (ps_buffer[i] == ' ') {
            ps_buffer[i] = 0;
            break;
        }
    }
    if (i == 63)
        param = ps_buffer;
    else
        param = ps_buffer + i + 1;
    if (ps_buffer[0] == 0) {
        return;
    } else if (kernel_strcmp(ps_buffer, "clear") == 0) {
        kernel_clear_screen(31);
    } else if (kernel_strcmp(ps_buffer, "echo") == 0) {
        kernel_printf("%s\n", param);
    } else if (kernel_strcmp(ps_buffer, "gettime") == 0) {
        char buf[10];
        get_time(buf, sizeof(buf));
        kernel_printf("%s\n", buf);
    } else if (kernel_strcmp(ps_buffer, "syscall4") == 0) {
        test_syscall4();
    } else if (kernel_strcmp(ps_buffer, "sdwi") == 0) {
        for (i = 0; i < 512; i++)
            sd_buffer[i] = i;
        sd_write_block(sd_buffer, 7, 1);
        kernel_puts("sdwi\n", 0xfff, 0);
    } else if (kernel_strcmp(ps_buffer, "sdr") == 0) {
        sd_read_block(sd_buffer, 7, 1);
        for (i = 0; i < 512; i++) {
            kernel_printf("%d ", sd_buffer[i]);
        }
        kernel_putchar('\n', 0xfff, 0);
    } else if (kernel_strcmp(ps_buffer, "sdwz") == 0) {
        for (i = 0; i < 512; i++) {
            sd_buffer[i] = 0;
        }
        sd_write_block(sd_buffer, 7, 1);
        kernel_puts("sdwz\n", 0xfff, 0);
    } else if (kernel_strcmp(ps_buffer, "mminfo") == 0) {
        bootmap_info();
        buddy_info();
    } else if (kernel_strcmp(ps_buffer, "mmtest") == 0) {
        void* address = kmalloc(1024);
        kernel_printf("kmalloc : %x, size = 1KB\n", address);
        kfree(address);
        kernel_printf("kfree succeed");
    } else if(kernel_strcmp(ps_buffer, "slubtest") == 0){
        unsigned int  size_kmem_cache[PAGE_SHIFT] = {96, 192, 8, 16, 32, 64, 128, 256, 512, 1024};
        unsigned int i;
        for(i = 0; i < 10; i++)
        {
            void* address = kmalloc(size_kmem_cache[i]);
            kernel_printf("kmalloc : %x, size = 1KB\n", address);
        }
    } else if(kernel_strcmp(ps_buffer, "buddytest") == 0){
        void* address = kmalloc(4096*2*2*2*2);
        kernel_printf("kmalloc : %x, size = 1KB\n", address);
        address = kmalloc(4096*2*2*2);
        kernel_printf("kmalloc : %x, size = 1KB\n", address);
        address = kmalloc(4096*2*2);
        kernel_printf("kmalloc : %x, size = 1KB\n", address);
        address = kmalloc(4096*2);
        kernel_printf("kmalloc : %x, size = 1KB\n", address);
    }else if(kernel_strcmp(ps_buffer, "buddy") == 0)
    {
        void *address = kmalloc(4096);
        kernel_printf("kmalloc : %x, size = 4KB\n", address);
        address = kmalloc(4096);
        kernel_printf("kmalloc : %x, size = 4KB\n", address);
        address = kmalloc(4096);
        kernel_printf("kmalloc : %x, size = 4KB\n", address);
        address = kmalloc(2*4096);
        kernel_printf("kmalloc : %x, size = 8KB\n", address);
        address = kmalloc(4096);
        kernel_printf("kmalloc : %x, size = 4KB\n", address);
        kfree(address);
        kfree(address);
        address = kmalloc(4096);
        kernel_printf("kmalloc : %x, size = 4KB\n", address);
        address = kmalloc(4096);
        kernel_printf("kmalloc : %x, size = 4KB\n", address);
        address = kmalloc(4096);
        kernel_printf("kmalloc : %x, size = 4KB\n", address);
        address = kmalloc(4096);
        kernel_printf("kmalloc : %x, size = 4KB\n", address);
        
    }else if(kernel_strcmp(ps_buffer, "slub") == 0)
    {
        void *address1 = kmalloc(1024);
        kernel_printf("kmalloc : %x, size = 4KB\n", address1);
        void *address2 = kmalloc(1024);
        kernel_printf("kmalloc : %x, size = 4KB\n", address2);
        void *address3 = kmalloc(1024);
        kernel_printf("kmalloc : %x, size = 4KB\n", address3);
        kfree(address2);
        kfree(address1);
        kfree(address2);
        kfree(address1);
        address2 = kmalloc(1024);
        kernel_printf("kmalloc : %x, size = 4KB\n", address2);
        address2 = kmalloc(1024);
        kernel_printf("kmalloc : %x, size = 4KB\n", address2);
        address2 = kmalloc(1024);
        kernel_printf("kmalloc : %x, size = 4KB\n", address2);
        address2 = kmalloc(1024);
        kernel_printf("kmalloc : %x, size = 4KB\n", address2);
    }else if(kernel_strcmp(ps_buffer, "slub2") == 0){
        void *address1 = kmalloc(1024);
        kernel_printf("kmalloc : %x, size = 4KB\n", address1);
        void *address2 = kmalloc(1024);
        kernel_printf("kmalloc : %x, size = 4KB\n", address2);
        void *address3 = kmalloc(1024);
        kernel_printf("kmalloc : %x, size = 4KB\n", address3);
        kfree(address1);
        kfree(address2);
        kfree(address3);
        kernel_printf("free a page\n");
        address2 = kmalloc(1024);
        kernel_printf("kmalloc : %x, size = 4KB\n", address2);
        address2 = kmalloc(1024);
        kernel_printf("kmalloc : %x, size = 4KB\n", address2);
        address2 = kmalloc(1024);
        kernel_printf("kmalloc : %x, size = 4KB\n", address2);
        address2 = kmalloc(1024);
        kernel_printf("kmalloc : %x, size = 4KB\n", address2);
        
    }
    else {
        kernel_puts(ps_buffer, 0xfff, 0);
        kernel_puts(": command not found\n", 0xfff, 0);
    }
}
