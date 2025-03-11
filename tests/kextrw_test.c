#include <lib/KextRW.h>

#include <mach-o/loader.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

// The following offsets are specific to my MacBook Pro M4 running 24C5089c

/*
Note: I have experienced some issues with calling some pmap functions (e.g. `pmap_enter_options_addr`), where my Mac seemingly panics, but there is no panic log once it powers back on. I have no idea what causes this, it could be something to do with SPTM.
*/

// Kernel functions
#define PMAP_ENTER_OPTIONS_ADDR kslide(0xFFFFFE0008869DAC)
#define PMAP_MAP_BLOCK_ADDR     kslide(0xFFFFFE000887458C)
#define PMAP_NEST               kslide(0xFFFFFE0008876388)
#define PHYSTOKV                kslide(0xFFFFFE000888F29C)
#define PANIC                   kslide(0xFFFFFE0008F99D88)
#define KAUTH_CRED_PROC_REF     kslide(0xFFFFFE0008C30EF4)
#define KAUTH_CRED_UNREF        kslide(0xFFFFFE0008C32188)
#define ML_SIGN_THREAD_STATE    kslide(0xFFFFFE00086E3874)

#define TASK_MAP                kslide(0xFFFFFE00087B1AF8)
#define TASK_PMAP               kslide(0xFFFFFE00087B1F2C)

// Kernel constants and variables
#define KERNPROC                kslide(0xFFFFFE0007CA6F38)
#define KERNEL_TASK             kslide(0xFFFFFE0007CA5DF0)
#define TASK_SIZE               kslide(0xFFFFFE000C049520) // proc->task = proc + sizeof(proc)

// Kernel structure offsets
#define off_proc_pid            (0x60)
#define off_proc_next           (0x0)
#define off_proc_prev           (0x8)
#define off_task_map            (0x28)
#define off_vm_map_pmap         (0x40)
#define off_thread_contextData  (0x100)
#define off_thread_cpudatap     (0x1B0)

#define koffsetof(type, field) off_##type##_##field

void panic(const char *msg, ...)
{
    char *panicMessage = NULL;
    va_list args;
    va_start(args, msg);
    vasprintf(&panicMessage, msg, args);
    va_end(args);
    uint64_t panicMsgBuf = kalloc(strlen(panicMessage) + 1);
    kwritebuf(panicMsgBuf, (void *)panicMessage, strlen(panicMessage) + 1);
    kcall(PANIC, (uint64_t []){ panicMsgBuf }, 1);
    kfree(panicMsgBuf, strlen(panicMessage) + 1);
}

static uint64_t kernproc = 0;

static uint64_t off_proc_task = 0;
static uint64_t proc_find(pid_t pid)
{
    if (!kernproc) return 0;
    uint64_t curProc = kernproc;
    while (curProc > STATIC_KERNEL_BASE) {
        pid_t curPid = kread32(curProc + koffsetof(proc, pid));
        if (curPid == pid) break;
        curProc = kreadptr(curProc + koffsetof(proc, prev));
    }
    return curProc;
}

static uint64_t proc_task(uint64_t proc)
{
    if (!off_proc_task) off_proc_task = kread64(TASK_SIZE);
    return proc + koffsetof(proc, task);
}

static uint64_t task_map(uint64_t task)
{
    return kreadptr(task + koffsetof(task, map));
}

static uint64_t task_pmap(uint64_t task)
{
    return kreadptr(task_map(task) + koffsetof(vm_map, pmap));
}

int main(void) {
    if (kextrw_init() == -1) {
        printf("Failed to initialize KextRW\n");
        return 1;
    }

    uint64_t kernelBase = get_kernel_base();

    printf("Kernel base (VA): 0x%llX\n", kernelBase);
    printf("Kernel base (PA): 0x%llX\n", kvtophys(kernelBase));
    printf("Kernel slide: 0x%llX\n", kslide(0));

    printf("kread32(0x%llX) -> 0x%X\n", kernelBase, kread32(kernelBase));

    uint64_t alloc = kalloc(0x100);
    printf("kalloc(0x100) -> 0x%llX\n", alloc);
    kfree(alloc, 0x100);

    uint64_t kbasePA = kvtophys(kernelBase);
    printf("kvtophys(0x%llX) -> 0x%llX\n", kernelBase, kbasePA);
    printf("physread32(0x%llX) -> 0x%X\n", kbasePA, physread32(kbasePA));

    printf("phystokv(0x%llX) -> 0x%llX\n", kbasePA, kcall(PHYSTOKV, (uint64_t []){ kbasePA}, 1));

    kernproc = kreadptr(KERNPROC);
    uint64_t kernel_task = proc_task(kernproc);

    uint64_t self_proc = proc_find(getpid());
    uint64_t self_task = proc_task(self_proc);
    uint64_t self_vm_map = task_map(self_task);
    uint64_t self_pmap = task_pmap(self_task);

    printf("kernproc: 0x%llX\n", kernproc);
    printf("kernel_task: 0x%llX\n", kernel_task);
    printf("Our proc: 0x%llX\n", self_proc);
    printf("Our task: 0x%llX\n", self_task);
    printf("Our vm_map: 0x%llX\n", self_vm_map);
    printf("Our pmap: 0x%llX\n", self_pmap);

    kextrw_deinit();
    return 0;
}