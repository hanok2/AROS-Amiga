/*
    Copyright � 1995-2017, The AROS Development Team. All rights reserved.
    $Id:$
*/

#include <asm/cpu.h>
#include <strings.h>

#define __KERNEL_NOLIBBASE__
#include <proto/kernel.h>


#include "kernel_base.h"
#include "kernel_intern.h"
#include "kernel_debug.h"

#include "kernel_ipi.h"

#include <kernel_scheduler.h>
#include <kernel_intr.h>

#include "apic_ia32.h"

#define D(x)

void core_DoIPI(uint8_t ipi_number, void *cpu_mask, struct KernelBase *KernelBase)
{
    int cpunum = KrnGetCPUNumber();
    ULONG cmd = (APIC_IRQ_IPI_START + ipi_number) | ICR_INT_ASSERT;
    struct PlatformData *kernPlatD = (struct PlatformData *)KernelBase->kb_PlatformData;
    struct APICData *apicPrivate = kernPlatD->kb_APIC;
    IPTR __APICBase = apicPrivate->lapicBase;

    D(bug("[Kernel:IPI] Sending IPI %02d form CPU.%03u to target mask @ 0x%p\n", ipi_number, cpunum, cpu_mask));
    
    if ((cmd & 0xff) <= APIC_IRQ_IPI_END)
    {
        // special case - send IPI to all
        if ((IPTR)cpu_mask == TASKAFFINITY_ANY)
        {
            // Shorthand - all including self
            cmd |= 0x80000;

            D(bug("[Kernel:IPI] waiting for DS bit to be clear\n"));
            while (APIC_REG(__APICBase, APIC_ICRL) & ICR_DS) asm volatile("pause");
            D(bug("[Kernel:IPI] sending IPI cmd %08x\n", cmd));
            APIC_REG(__APICBase, APIC_ICRL) = cmd;
        }
        else if ((IPTR)cpu_mask == TASKAFFINITY_ALL_BUT_SELF)
        {
            // Shorthand - all excluding self
            cmd |= 0xc0000;

            D(bug("[Kernel:IPI] waiting for DS bit to be clear\n"));
            while (APIC_REG(__APICBase, APIC_ICRL) & ICR_DS) asm volatile("pause");
            D(bug("[Kernel:IPI] sending IPI cmd %08x\n", cmd));
            APIC_REG(__APICBase, APIC_ICRL) = cmd;
        }
        else
        {
            int i;

            // No shortcut, send IPI to each CPU one after another
            for (i=0; i < apicPrivate->apic_count; i++)
            {
                if (KrnCPUInMask(i, cpu_mask))
                {
                    D(bug("[Kernel:IPI] waiting for DS bit to be clear\n"));
                    while (APIC_REG(__APICBase, APIC_ICRL) & ICR_DS) asm volatile("pause");
                    if (cpunum == i)
                    {
                        D(bug("[Kernel:IPI] sending IPI cmd %08x to destination %08x (self-ipi)\n", cmd, (apicPrivate->cores[i].cpu_LocalID << 24)));
                        APIC_REG(__APICBase, APIC_ICRL) = cmd | 0x40000;
                    }
                    else
                    {
                        D(bug("[Kernel:IPI] sending IPI cmd %08x to destination %08x\n", cmd, (apicPrivate->cores[i].cpu_LocalID << 24)));
                        APIC_REG(__APICBase, APIC_ICRH) = (apicPrivate->cores[i].cpu_LocalID << 24);
                        APIC_REG(__APICBase, APIC_ICRL) = cmd;
                    }
                }
            }
        }
    }
}

void core_DoCallIPI(struct Hook *hook, void *cpu_mask, int async, struct KernelBase *KernelBase)
{
    struct PlatformData *pdata = KernelBase->kb_PlatformData;
    struct IPIHook *ipi = NULL;
    struct APICData *apicPrivate = pdata->kb_APIC;
    int cpunum = KrnGetCPUNumber();

    D(bug("[Kernel:IPI] %s: Calling hook %p, async=%d\n", __func__, hook, async));

    if (hook)
    {
        /*
            Allocate IPIHook just by removing it form the Free list:
            First Disable() so that we are not interrupted on this CPU core, then use SpinLock to protect us from
            other cores accessing the list.

            If the FreeIPIHooks list is empty, just do busyloop wait - other cores shall free the hook sooner or later
        */
        do 
        {
            Disable();
            KrnSpinLock(&pdata->kb_FreeIPIHooksLock, NULL, SPINLOCK_MODE_WRITE);
            ipi = (struct IPIHook *)REMHEAD(&pdata->kb_FreeIPIHooks);
            KrnSpinUnLock(&pdata->kb_FreeIPIHooksLock);
            Enable();
            if (ipi == NULL)
            {
                D(bug("[Kernel:IPI] %s: Failed to allocate IPIHook entry\n", __func__));
            }
        } while(ipi == NULL);

        D(bug("[Kernel:IPI] %s: Allocated IPIHook %p\n", __func__, ipi));
    
        /*
            Copy IPI data from struct Hook provided by caller into allocated ipi
        */
        if (async)
        {
            ipi->ih_Hook.h_Entry = hook->h_Entry;
            ipi->ih_Hook.h_SubEntry = hook->h_SubEntry;
            ipi->ih_Hook.h_Data = hook->h_Data;
            ipi->ih_Async = 1;
        }
        else
        {
            ipi->ih_Hook.h_Entry = hook->h_Entry;
            ipi->ih_Hook.h_Data = hook;
            ipi->ih_Async = 0;

            KrnSpinInit(&ipi->ih_SyncLock);
            KrnSpinLock(&ipi->ih_SyncLock, NULL, SPINLOCK_MODE_WRITE);
        }

        /* Clear CPU done mask */
        bzero(ipi->ih_CPUDone, sizeof(ULONG)*((31 + apicPrivate->apic_count) / 32));

        /* Copy CPU mask */
        if (cpu_mask && cpu_mask != (void*)TASKAFFINITY_ANY && cpu_mask != (void*)TASKAFFINITY_ALL_BUT_SELF)
            bcopy(cpu_mask, ipi->ih_CPURequested, sizeof(ULONG)*((31 + apicPrivate->apic_count) / 32));
        else
        {
            int i;

            bzero(ipi->ih_CPURequested, sizeof(ULONG)*((31 + apicPrivate->apic_count) / 32));

            for (i=0; i < apicPrivate->apic_count; i++)
            {
                if ((cpu_mask == (APTR)TASKAFFINITY_ALL_BUT_SELF) && (i == cpunum))
                    continue;

                bit_test_and_set_long(ipi->ih_CPURequested, i);
            }
        }


        /*
            Put the IPIHook on the BusyIPIHooks list, so that it gets processed once IPIs are called
        */
        Disable();
        KrnSpinLock(&pdata->kb_BusyIPIHooksLock, NULL, SPINLOCK_MODE_WRITE);
        ADDHEAD(&pdata->kb_BusyIPIHooks, ipi);
        KrnSpinUnLock(&pdata->kb_BusyIPIHooksLock);
        Enable();

        D(bug("[Kernel:IPI] %s: Sending IPI message\n", __func__, ipi));

        /* Issue IPI_CALL_HOOK to requested CPUs */
        core_DoIPI(IPI_CALL_HOOK, cpu_mask, KernelBase);

        /* If synchronous IPI, wait for completion */
        if (!async)
        {
            D(bug("[Kernel:IPI] %s: Synchronous IPI, waiting for completion\n", __func__));
            KrnSpinLock(&ipi->ih_SyncLock, NULL, SPINLOCK_MODE_WRITE);
            KrnSpinUnLock(&ipi->ih_SyncLock);
            D(bug("[Kernel:IPI] %s: Synchronous IPI completed\n", __func__));
        }
    }
}

static void core_IPICallHookHandle(struct ExceptionContext *regs, struct KernelBase *KernelBase)
{
    struct PlatformData *pdata = KernelBase->kb_PlatformData;
    struct IPIHook *ipi = NULL;
    struct IPIHook *n = NULL;
    struct APICData *apicPrivate = pdata->kb_APIC;
    int cpunum = KrnGetCPUNumber();

    D(bug("[Kernel:IPI.CPU.%03u] %s\n", cpunum, __func__));

    KrnSpinLock(&pdata->kb_BusyIPIHooksLock, NULL, SPINLOCK_MODE_WRITE);

    ForeachNodeSafe(&pdata->kb_BusyIPIHooks, ipi, n)
    {
        D(bug("[Kernel:IPI.CPU.%03u] %s: Checking node %p\n", cpunum, __func__, ipi));
        if (KrnCPUInMask(cpunum, ipi->ih_CPURequested))
        {
            
            /*
            In case of asynchronous hook, the object we pass is IPIHook itself. Dangerous, but we needed to copy the
            original Hook since it could have existed on stack of caller...
            */
            if (ipi->ih_Async)
            {
                D(bug("[Kernel:IPI.CPU.%03u] %s: Calling HOOK Entry %p with Data %p\n", cpunum, __func__, 
                    ipi->ih_Hook.h_Entry, &ipi->ih_Hook));

                CALLHOOKPKT(&ipi->ih_Hook, &ipi->ih_Hook, 0);
            }
            else
            {
                D(bug("[Kernel:IPI.CPU.%03u] %s: Calling HOOK Entry %p with Data %p\n", cpunum, __func__, 
                    ipi->ih_Hook.h_Entry, ipi->ih_Hook.h_Data));

                CALLHOOKPKT(&ipi->ih_Hook, ipi->ih_Hook.h_Data, 0);
            }

            /* This part operates on locked IPIHook */
            KrnSpinLock(&ipi->ih_Lock, NULL, SPINLOCK_MODE_WRITE);

            /* Mark this CPU as done */
            bit_test_and_set_long(ipi->ih_CPUDone, cpunum);

            /* Check if all requested CPUs have handled the IPI */
            if (!memcmp(ipi->ih_CPUDone, ipi->ih_CPURequested, sizeof(ULONG) * ((31 + apicPrivate->apic_count)/32)))
            {
                D(bug("[Kernel:IPI.CPU.%03u] %s: IPIHook completed. Releasing it\n", cpunum, __func__));
                int async = ipi->ih_Async;

                /* Yes, remove this ipi from BusyList */
                REMOVE(ipi);

                /* Put it on FreeList */
                KrnSpinLock(&pdata->kb_FreeIPIHooksLock, NULL, SPINLOCK_MODE_WRITE);
                ADDHEAD(&pdata->kb_FreeIPIHooks, ipi);
                KrnSpinUnLock(&pdata->kb_FreeIPIHooksLock);

                if (!async)
                {
                    D(bug("[Kernel:IPI.CPU.%03u] %s: Releasing sync lock\n", cpunum, __func__));
                    KrnSpinUnLock(&ipi->ih_SyncLock);
                }
            }
            /* End of IPIHook critical section */
            KrnSpinUnLock(&ipi->ih_Lock);
        }
    }

    KrnSpinUnLock(&pdata->kb_BusyIPIHooksLock);
}

void core_IPIHandle(struct ExceptionContext *regs, unsigned long ipi_number, struct KernelBase *KernelBase)
{
    //int cpunum = KrnGetCPUNumber();
    IPTR __APICBase = core_APIC_GetBase();
    
    D(bug("[Kernel:IPI] CPU.%03u IPI%02d\n", cpunum, ipi_number));

    switch (ipi_number)
    {
        case IPI_RESCHEDULE:
            APIC_REG(__APICBase, APIC_EOI) = 0;
            // If IPI was called when CPU was in user mode, perform task switch, otherwise
            // set delayed schedule flag
            if (regs->ss != 0)
            {
                if (core_Schedule())
                {
                    cpu_Switch(regs);
                    cpu_Dispatch(regs);
                }
            }
            else
            {
                FLAG_SCHEDSWITCH_SET;
            }
            break;

        case IPI_CALL_HOOK:
            core_IPICallHookHandle(regs, KernelBase);
            APIC_REG(__APICBase, APIC_EOI) = 0;
            break;
    }
}