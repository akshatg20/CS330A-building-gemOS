#include <types.h>
#include <mmap.h>
#include <fork.h>
#include <v2p.h>
#include <page.h>

/*
 * You may define macros and other helper functions here
 * You must not declare and use any static/global variables
 * */

#define FOUR_KB 0x1000

#define PGD_MASK 0xFF8000000000
#define PUD_MASK 0x7FC0000000
#define PMD_MASK 0x3FE00000
#define PTE_MASK 0x1FF000

#define PGD_SHIFT 0x27
#define PUD_SHIFT 0x1E
#define PMD_SHIFT 0x15
#define PTE_SHIFT 0xC

#define PTE_SIZE 0x8
#define PT_SIZE 0x200
#define ADDR_SHIFT 0xC



// helper function to create a new virtual memory area
void createNewVMArea(u64 start, u64 end, u32 access_flags, struct vm_area *prev, struct vm_area *next)
{
    struct vm_area *newVMA = os_alloc(sizeof(struct vm_area));
    newVMA->access_flags = access_flags;
    newVMA->vm_start = start;
    newVMA->vm_end = end;
    newVMA->vm_next = next;
    prev->vm_next = newVMA;
    stats->num_vm_area++;
}

// helper function which assigns the first legal vm_area
long allocVMArea(struct exec_context *current, int alignedLen, int prot)
{
    struct vm_area *memptr = current->vm_area;
    u64 addr_start = memptr->vm_start;

    while (memptr != NULL)
    {
        // case 1: this is the last allocated vm_area
        if (memptr->vm_next == NULL)
        {
            // create new vm_area if new memory to be allocated does'nt exceed MMAP_AREA_END
            if (memptr->vm_end + alignedLen <= MMAP_AREA_END)
            {
                addr_start = memptr->vm_end;
                // check if the access_flags are similar
                if (memptr->access_flags == prot)
                {
                    // simply extend the size of the previous vm_area
                    memptr->vm_end += alignedLen;
                }
                else
                {
                    createNewVMArea(memptr->vm_end, memptr->vm_end + alignedLen, prot, memptr, NULL);
                }

                // return the start_addr of the newly allocated memory
                return addr_start;
            }

            // not enough space to allocate new memory
            else
            {
                return -EINVAL;
            }
        }

        // case 2: space between current and next vm_area is sufficient to allocate new memory
        else if (memptr->vm_end + alignedLen <= memptr->vm_next->vm_start)
        {
            // update the return value of the start addr of the newly created vm_area
            addr_start = memptr->vm_end;

            // if current memory can only be combined with current vma
            if ((prot == memptr->access_flags) &&
                (!(memptr->vm_end + alignedLen == memptr->vm_next->vm_start && prot == memptr->vm_next->access_flags)))
            {
                memptr->vm_end += alignedLen;
            }
            // if current memory can only be combined with the next vma
            else if (!(prot == memptr->access_flags) &&
                     ((memptr->vm_end + alignedLen == memptr->vm_next->vm_start && prot == memptr->vm_next->access_flags)))
            {
                memptr->vm_next->vm_start = memptr->vm_end;
            }
            // if current memory can  be combined with the next vma and current vma
            else if ((prot == memptr->access_flags) &&
                     ((memptr->vm_end + alignedLen == memptr->vm_next->vm_start && prot == memptr->vm_next->access_flags)))
            {
                struct vm_area *vmFreed = memptr->vm_next;
                memptr->vm_end = vmFreed->vm_end;
                memptr->vm_next = vmFreed->vm_next;
                os_free(vmFreed, sizeof(vmFreed));
                stats->num_vm_area--;
            }
            // if memory cannot be combined
            else
            {
                createNewVMArea(memptr->vm_end, memptr->vm_end + alignedLen, prot, memptr, memptr->vm_next);
            }

            // return the start address of the newly allocated memory (not accounting for merging)
            return addr_start;
        }

        memptr = memptr->vm_next;
    }
}

// helper function to deallocate a particular pfn mapped to a virtual page
void freePFN(long addr) {

    struct exec_context *current = get_current_ctx();

    // first we update the page table entry

    // Compute offsets required in each level of the page table
    u64 pgdIdx = (addr & PGD_MASK) >> PGD_SHIFT;
    u64 pudIdx = (addr & PUD_MASK) >> PUD_SHIFT;
    u64 pmdIdx = (addr & PMD_MASK) >> PMD_SHIFT;
    u64 pteIdx = (addr & PTE_MASK) >> PTE_SHIFT;

    // calculate the entry of in the first level of the page table
    u64 pgd_entry_VA = ((u64)osmap(current->pgd)) + (pgdIdx)*(PTE_SIZE);

    if( ( *((u64*)pgd_entry_VA) & 1 ) == 0) {
        return;
    }

    // calculate the entry of in the second level of the page table
    u64 pud_entry_VA = ((u64)osmap( ( ( *((u64*)pgd_entry_VA)  ) >> ADDR_SHIFT) ) ) + (pudIdx)*(PTE_SIZE);

    if( ( *((u64*)pud_entry_VA) & 1 ) == 0) {
        return;
    }

    // calculate the entry of in the third level of the page table
    u64 pmd_entry_VA = ((u64)osmap( ( ( *((u64*)pud_entry_VA)  ) >> ADDR_SHIFT) ) ) + (pmdIdx)*(PTE_SIZE);

    if( ( *((u64*)pmd_entry_VA) & 1 ) == 0) {
        return;
    }

    // calculate the entry of in the final level of the page table
    u64 pte_entry_VA = ((u64)osmap( ( ( *((u64*)pmd_entry_VA)  ) >> ADDR_SHIFT) ) ) + (pteIdx)*(PTE_SIZE);

    if( ( *((u64*)pte_entry_VA) & 1 ) == 0) {
        return;
    }

    // extract the pfn number from the entry
    u64 pfn = ( *((u64*)pte_entry_VA) >> ADDR_SHIFT );

    // set the entry to zero
    *((u64*)pte_entry_VA) = 0x0;

    // decrement the reference count of the pfn
    if(get_pfn_refcount(pfn) == 0) return;
    put_pfn(pfn);
    
    // check the ref_count of the pfn before unmapping
    if(get_pfn_refcount(pfn) == 0) {
        // now we actually deallocate the pfn
        os_pfn_free(USER_REG,pfn);
    }
    asm volatile("invlpg (%0);" ::"r"(addr) : "memory");
}

// helper function which deallocates all pfns from vma to be unmapped
void freeAllPFNs(long addr_start, long addr_end) {

    // find the number of pages to be deallocated
    int numPages = (addr_end - addr_start) / (FOUR_KB);

    // iterate through each page and deallocate it
    for(int i = 0; i < numPages; i++) {
        freePFN(addr_start + i*(FOUR_KB));
    }
}

// helper function to update permissions in the page table
void updatePTPermissions(u64 pfn, u64 pgd_entry_VA, u64 pud_entry_VA, u64 pmd_entry_VA) {
    
    // first we will check all the entries of pte_t

    u64 addr_ptr = (u64)osmap( ( *((u64*)pmd_entry_VA) ) >> 12) ;
    while(addr_ptr < (u64)osmap( ( *((u64*)pmd_entry_VA) ) >> 12) + PT_SIZE) {
        // if an entry of pte_t has read/write permission, then return
        if( ( ( ( *((u64*)addr_ptr) ) >> 0x3) & 0x1 ) ==  0x1 ) return;
        addr_ptr += PTE_SIZE;
    }

    // since all the entries in pte_t were read_only, update the permission of pmd_entry
    *((u64*)pmd_entry_VA) &= ~(0x3);

    // check all the entries of the pmd_t
    addr_ptr = (u64)osmap( ( *((u64*)pud_entry_VA) ) >> 12) ;
    while(addr_ptr < (u64)osmap( ( *((u64*)pud_entry_VA) ) >> 12) + PT_SIZE) {
        // if an entry of pte_t has read/write permission, then return
        if( ( ( ( *((u64*)addr_ptr) ) >> 0x3) & 0x1 ) ==  0x1 ) return;
        addr_ptr += PTE_SIZE;
    }

    // since all the entries in pmd_t were read_only, update the permission of pud_entry
    *((u64*)pud_entry_VA) &= ~(0x3);

    // check all the entries of the pud_t
    addr_ptr = (u64)osmap( ( *((u64*)pgd_entry_VA) ) >> 12) ;
    while(addr_ptr < (u64)osmap( ( *((u64*)pgd_entry_VA) ) >> 12) + PT_SIZE) {
        // if an entry of pte_t has read/write permission, then return
        if( ( ( ( *((u64*)addr_ptr) ) >> 0x3) & 0x1 ) ==  0x1 ) return;
        addr_ptr += PTE_SIZE;
    }

    // since all the entries in pmd_t were read_only, update the permission of pgd_entry
    *((u64*)pgd_entry_VA) &= ~(0x3);

    return;
}

// helper functio to update a particular pfn mapped to a virtual page
void updatePFN(long addr, int prot) {

    struct exec_context *current = get_current_ctx();

     // first we update the page table entry

    // Compute offsets required in each level of the page table
    u64 pgdIdx = (addr & PGD_MASK) >> PGD_SHIFT;
    u64 pudIdx = (addr & PUD_MASK) >> PUD_SHIFT;
    u64 pmdIdx = (addr & PMD_MASK) >> PMD_SHIFT;
    u64 pteIdx = (addr & PTE_MASK) >> PTE_SHIFT;

    // calculate the entry of in the first level of the page table
    u64 pgd_entry_VA = ((u64)osmap(current->pgd)) + (pgdIdx)*(PTE_SIZE);

    if( ( *((u64*)pgd_entry_VA) & 1 ) == 0) {
        return;
    }

    // calculate the entry of in the second level of the page table
    u64 pud_entry_VA = ((u64)osmap( ( ( *((u64*)pgd_entry_VA)  ) >> ADDR_SHIFT) ) ) + (pudIdx)*(PTE_SIZE);

    if( ( *((u64*)pud_entry_VA) & 1 ) == 0) {
        return;
    }

    // calculate the entry of in the third level of the page table
    u64 pmd_entry_VA = ((u64)osmap( ( ( *((u64*)pud_entry_VA)  ) >> ADDR_SHIFT) ) ) + (pmdIdx)*(PTE_SIZE);

    if( ( *((u64*)pmd_entry_VA) & 1 ) == 0) {
        return;
    }

    // calculate the entry of in the final level of the page table
    u64 pte_entry_VA = ((u64)osmap( ( ( *((u64*)pmd_entry_VA)  ) >> ADDR_SHIFT) ) ) + (pteIdx)*(PTE_SIZE);

    if( ( *((u64*)pte_entry_VA) & 1 ) == 0) {
        return;
    }

    if(prot == 1) {
        *((u64*)pte_entry_VA) &= ~(0x8);

        // now we have to restructure the page table
        u64 pfn = ( ( *((u64*)pte_entry_VA)  ) >> ADDR_SHIFT );
        updatePTPermissions(pfn,pgd_entry_VA,pud_entry_VA,pmd_entry_VA);
    }
    else {

        u64 pfn = ( ( *((u64*)pte_entry_VA)  ) >> ADDR_SHIFT );
        if(get_pfn_refcount(pfn) > 1) {
            // create a new pfn with read/write permissions
            u64 new_pfn = os_pfn_alloc(USER_REG);
            *((u64*)pte_entry_VA) = (new_pfn << ADDR_SHIFT) | 0x11;
            *((u64*)pte_entry_VA) |= 0x8;
            
            // decrement the ref count of the previous pfn
            put_pfn(pfn);
            if(get_pfn_refcount(pfn) == 0) {
                os_pfn_free(USER_REG,pfn);
            }
        }

        // now we have to set all the preceding level entries to read/write permissions
        *((u64*)pte_entry_VA) |= 0x8;
        *((u64*)pmd_entry_VA) |= 0x8;
        *((u64*)pud_entry_VA) |= 0x8;
        *((u64*)pgd_entry_VA) |= 0x8;

    }
    asm volatile("invlpg (%0);" ::"r"(addr) : "memory");
}

// helper function which updates all pfns from vma being modified
void updateAllPFNs(long addr_start, long addr_end, int prot) {

    // find the number of pages being updated
    int numPages = (addr_end - addr_start) / (FOUR_KB);

    // iterate through each page and deallocate it
    for(int i = 0; i < numPages; i++) {
        updatePFN(addr_start + i*(FOUR_KB), prot);
    }
}

// helper function to build child process page table
int copyPageToChildProcess(u64 addr, struct exec_context* parCtx, struct exec_context* childCtx) {
    
    // Compute offsets required in each level of the page table
    u64 pgdIdx = (addr & PGD_MASK) >> PGD_SHIFT;
    u64 pudIdx = (addr & PUD_MASK) >> PUD_SHIFT;
    u64 pmdIdx = (addr & PMD_MASK) >> PMD_SHIFT;
    u64 pteIdx = (addr & PTE_MASK) >> PTE_SHIFT;

    u64 pgd_par_VA = ((u64)osmap(parCtx->pgd)) + (pgdIdx)*(PTE_SIZE);
    u64 pgd_child_VA = ((u64)osmap(childCtx->pgd)) + (pgdIdx)*(PTE_SIZE);

    // check if page frame has been allocated for the next level of the page table in parent process
    if( ( *((u64*)pgd_par_VA) & 1 ) == 0) {
        return 0;
    }

    if( ( *((u64*)pgd_child_VA) & 1 ) == 0) {

        // allocate pfn for pud_t
        u64 pud_pfn = os_pfn_alloc(OS_PT_REG);

        // update the pgd_entry
        *((u64*)pgd_child_VA) = (pud_pfn << ADDR_SHIFT) | 0x11;  // set the present bit along with the pfn value

        // restrict read/write permissions for both processes
        *((u64*)pgd_par_VA) &= ~(0x8);
        *((u64*)pgd_child_VA) &= ~(0x8);
    }

    u64 pud_par_VA = ((u64)osmap( ( ( *((u64*)pgd_par_VA)  ) >> ADDR_SHIFT) ) ) + (pudIdx)*(PTE_SIZE);
    u64 pud_child_VA = ((u64)osmap( ( ( *((u64*)pgd_child_VA)  ) >> ADDR_SHIFT) ) ) + (pudIdx)*(PTE_SIZE);

    // check if page frame has been allocated for the next level of the page table in parent process
    if( ( *((u64*)pud_par_VA) & 1 ) == 0) {
        return 0;
    }

    if( ( *((u64*)pud_child_VA) & 1 ) == 0) {

        // allocate pfn for pud_t
        u64 pmd_pfn = os_pfn_alloc(OS_PT_REG);

        // update the pgd_entry
        *((u64*)pud_child_VA) = (pmd_pfn << ADDR_SHIFT) | 0x11;  // set the present bit along with the pfn value

        // restrict read/write permissions for both processes
        *((u64*)pud_par_VA) &= ~(0x8);
        *((u64*)pud_child_VA) &= ~(0x8);
    }

    u64 pmd_par_VA = ((u64)osmap( ( ( *((u64*)pud_par_VA)  ) >> ADDR_SHIFT) ) ) + (pmdIdx)*(PTE_SIZE);
    u64 pmd_child_VA = ((u64)osmap( ( ( *((u64*)pud_child_VA)  ) >> ADDR_SHIFT) ) ) + (pmdIdx)*(PTE_SIZE);

    // check if page frame has been allocated for the next level of the page table in parent process
    if( ( *((u64*)pmd_par_VA) & 1 ) == 0) {
        return 0;
    }

    if( ( *((u64*)pmd_child_VA) & 1 ) == 0) {

        // allocate pfn for pud_t
        u64 pte_pfn = os_pfn_alloc(OS_PT_REG);

        // update the pgd_entry
        *((u64*)pmd_child_VA) = (pte_pfn << ADDR_SHIFT) | 0x11;  // set the present bit along with the pfn value

        // restrict read/write permissions for both processes
        *((u64*)pmd_par_VA) &= ~(0x8);
        *((u64*)pmd_child_VA) &= ~(0x8);
    }

    u64 pte_par_VA = ((u64)osmap( ( ( *((u64*)pmd_par_VA)  ) >> ADDR_SHIFT) ) ) + (pteIdx)*(PTE_SIZE);
    u64 pte_child_VA = ((u64)osmap( ( ( *((u64*)pmd_child_VA)  ) >> ADDR_SHIFT) ) ) + (pteIdx)*(PTE_SIZE);

    // check if page frame has been allocated for the next level of the page table in parent process
    if( ( *((u64*)pte_par_VA) & 1 ) == 0) {
        return 0;
    }

    if( ( *((u64*)pte_child_VA) & 1 ) == 0) {

        // extract the pfn used in the parent process
        u64 physical_pfn = ( *((u64*)pte_par_VA) >> ADDR_SHIFT );
        // increment its reference count
        get_pfn(physical_pfn);

        // point the child_pte to this pfn
        *((u64*)pte_child_VA) = (physical_pfn << ADDR_SHIFT) | 0x11; 

        // restrict read/write permissions for both processes
        *((u64*)pte_par_VA) &= ~(0x8);
        *((u64*)pte_child_VA) &= ~(0x8);
    }

    asm volatile("invlpg (%0);" ::"r"(addr) : "memory");
    return 1;
}

// helper function to copy page table for different memories
void copy_MM_Segment(u64 addr_start, u64 addr_end, struct exec_context* parCtx, struct exec_context* childCtx) {
    int length = (addr_end - addr_start);
    length = (length + 0xFFF) & ~(0xFFF);
    int numPagesCopied = (length) / (FOUR_KB);

    for(int i = 0; i < numPagesCopied; i++) {
        copyPageToChildProcess(addr_start + i*(FOUR_KB), parCtx, childCtx);
    }
}

/**
 * mprotect System call Implementation.
 */
long vm_area_mprotect(struct exec_context *current, u64 addr, int length, int prot)
{
    if (length < 0)
    {
        return -EINVAL;
    }
    if (prot != 1 && prot != 3)
    {
        return -EINVAL;
    }

    // locate the vm_area being modified
    struct vm_area *memptr = current->vm_area;
    struct vm_area *prev = NULL;
    prev->vm_next = memptr;

    // create a length which is alligned with 4KB
    int alignedLen = (length + 0xFFF) & ~(0xFFF);

    while (memptr != NULL && memptr->vm_start <= addr)
    {
        // if addr present in current vm_area
        if (addr < memptr->vm_end && addr >= memptr->vm_start)
        {
            u64 addr_start = addr;
            u64 addr_end = addr + alignedLen;

            updateAllPFNs(addr_start,addr_end,prot);

            if (addr_start != memptr->vm_start && addr_end != memptr->vm_end)
            {
                // split the vm_area into three separate vm_areas
                struct vm_area *vm1 = os_alloc(sizeof(struct vm_area));
                struct vm_area *vm2 = os_alloc(sizeof(struct vm_area));
                struct vm_area *vm3 = os_alloc(sizeof(struct vm_area));

                vm1->access_flags = vm3->access_flags = memptr->access_flags;
                vm2->access_flags = prot;

                vm1->vm_start = memptr->vm_start;
                vm1->vm_end = addr_start;
                prev->vm_next = vm1;
                vm1->vm_next = vm2;

                vm2->vm_start = addr_start;
                vm2->vm_end = addr_end;
                vm2->vm_next = vm3;

                vm3->vm_start = addr_end;
                vm3->vm_end = memptr->vm_end;
                vm3->vm_next = memptr->vm_next;

                os_free(memptr, sizeof(memptr));
                stats->num_vm_area += 2;
            }

            else if (addr_start != memptr->vm_start && addr_end == memptr->vm_end)
            {
                // modify the initial portion of the vm_area
                memptr->vm_end = addr_start;

                // check if final portion of this vm_area can be merged with the next
                if (memptr->vm_next != NULL && addr_end == memptr->vm_next->vm_start && memptr->vm_next->access_flags == prot)
                {
                    memptr->vm_next->vm_start = addr_start;
                }
                else
                {
                    createNewVMArea(addr_start, addr_end, prot, memptr, memptr->vm_next);
                }
            }

            else if (addr_start == memptr->vm_start && addr_end != memptr->vm_end)
            {
                // modify the ending section of the vm_area
                memptr->vm_start = addr_end;

                // check if the initial portion of this vm_area can be merged with the previous
                if (addr_start == prev->vm_end && prev->access_flags == prot)
                {
                    prev->vm_end = addr_end;
                }
                else
                {
                    createNewVMArea(addr_start, addr_end, prot, prev, memptr);
                }
            }

            else {
                memptr->access_flags = prot;

                // now we have to check if this vma can be combined with the previous and the next
                
                if( (prev->access_flags == prot && prev->vm_end == addr_start) && (memptr->vm_next != NULL && memptr->vm_next->access_flags == prot && memptr->vm_next->vm_start == addr_end) ) {
                    struct vm_area *vm1 = memptr->vm_next, *vm2 = memptr;
                    prev->vm_next = memptr->vm_next->vm_next;
                    prev->vm_end = memptr->vm_next->vm_end;
                    os_free(vm1,sizeof(vm1));
                    os_free(vm2,sizeof(vm2));
                    stats->num_vm_area--;
                    stats->num_vm_area--;
                }

                else if( !(prev->access_flags == prot && prev->vm_end == addr_start) && (memptr->vm_next != NULL && memptr->vm_next->access_flags == prot && memptr->vm_next->vm_start == addr_end)) {
                    struct vm_area *vmFreed = memptr->vm_next;
                    memptr->vm_end = vmFreed->vm_end;
                    memptr->vm_next = vmFreed->vm_next;
                    os_free(vmFreed, sizeof(vmFreed));
                    stats->num_vm_area--;
                }

                else if( (prev->access_flags == prot && prev->vm_end == addr_start) && !(memptr->vm_next != NULL && memptr->vm_next->access_flags == prot && memptr->vm_next->vm_start == addr_end)) {
                    struct vm_area *vmFreed = memptr;
                    prev->vm_end = vmFreed->vm_end;
                    prev->vm_next = vmFreed->vm_next;
                    os_free(vmFreed, sizeof(vmFreed));
                    stats->num_vm_area--;
                }
            }

            return 0;
        }

        prev = memptr;
        memptr = memptr->vm_next;
    }
    return -EINVAL;
}

/**
 * mmap system call implementation.
 */
long vm_area_map(struct exec_context *current, u64 addr, int length, int prot, int flag)
{
    // return error in case of illegal inputs
    if (length < 0)
    {
        return -EINVAL;
    }
    if (prot != 1 && prot != 3)
    {
        return -EINVAL;
    }
    if (flag > 1 || flag < 0)
    {
        return -EINVAL;
    }

    // create dummy node if vm_area is empty
    if (current->vm_area == NULL)
    {
        struct vm_area *dummy_node = os_alloc(sizeof(struct vm_area));
        dummy_node->access_flags = 0x0;
        dummy_node->vm_start = MMAP_AREA_START;
        dummy_node->vm_end = MMAP_AREA_START + FOUR_KB;
        dummy_node->vm_next = NULL;
        current->vm_area = dummy_node;
        stats->num_vm_area = 1;
    }

    struct vm_area *memptr = current->vm_area;

    // create a length which is alligned with 4KB
    int alignedLen = (length + 0xFFF) & ~(0xFFF);

    // find the appropriate start_address for the new memory to be allocated
    if (addr == 0)
    {
        if (flag == MAP_FIXED)
        {
            return -EINVAL;
        }
        return allocVMArea(current, alignedLen, prot);
    }

    // if addr is passed as a hint
    else
    {
        while (memptr != NULL)
        {
            // case 1: addr passed is not free
            if (memptr->vm_start <= addr && memptr->vm_end > addr)
            {
                // allocate memory based upon first free eligible vm_area
                if (flag == MAP_FIXED)
                {
                    return -EINVAL;
                }
                return allocVMArea(current, alignedLen, prot);
            }

            // case 2: addr passed is present in between current and next vm_area
            else if ((memptr->vm_end <= addr) && (memptr->vm_next != NULL && memptr->vm_next->vm_start > addr))
            {
                // if there is enough space to allocate memory
                if ((addr + alignedLen <= memptr->vm_next->vm_start))
                {
                    // if current memory can only be combined with previous vma
                    if ((addr == memptr->vm_end && prot == memptr->access_flags) &&
                        (!(addr + alignedLen == memptr->vm_next->vm_start && prot == memptr->vm_next->access_flags)))
                    {
                        memptr->vm_end += alignedLen;
                    }
                    // if current memory can only be combined with the next vma
                    else if (!(addr == memptr->vm_end && prot == memptr->access_flags) &&
                             ((addr + alignedLen == memptr->vm_next->vm_start && prot == memptr->vm_next->access_flags)))
                    {
                        memptr->vm_next->vm_start = addr;
                    }
                    // if current memory will be combined with both the current and the next vma
                    else if ((addr == memptr->vm_end && prot == memptr->access_flags) &&
                             ((addr + alignedLen == memptr->vm_next->vm_start && prot == memptr->vm_next->access_flags)))
                    {
                        struct vm_area *vmFreed = memptr->vm_next;
                        memptr->vm_end = vmFreed->vm_end;
                        memptr->vm_next = vmFreed->vm_next;
                        os_free(vmFreed, sizeof(vmFreed));
                        stats->num_vm_area--;
                    }
                    // if current memory cannot be combined
                    else
                    {
                        createNewVMArea(addr, addr + alignedLen, prot, memptr, memptr->vm_next);
                    }

                    return addr;
                }
                // there isn't enough space to allocate memory at addr
                else
                {
                    if (flag == MAP_FIXED)
                    {
                        return -EINVAL;
                    }
                    else
                        return allocVMArea(current, alignedLen, prot);
                }
            }
            // case 3: address passed is after any allocated vm_area
            else if (memptr->vm_end <= addr && memptr->vm_next == NULL)
            {
                // if there is enough space to allocate memory
                if (addr + alignedLen <= MMAP_AREA_END)
                {
                    // if memory can be combined with the previous vm_area
                    if (addr == memptr->vm_end && prot == memptr->access_flags)
                    {
                        memptr->vm_end += alignedLen;
                    }

                    // if memory cannot be combined
                    else
                    {
                        createNewVMArea(addr, addr + alignedLen, prot, memptr, NULL);
                    }
                    return addr;
                }
                else
                {
                    if (flag == MAP_FIXED)
                    {
                        return -EINVAL;
                    }
                    else
                        return allocVMArea(current, alignedLen, prot);
                }
            }
            memptr = memptr->vm_next;
        }
    }

    return -EINVAL;
}

/**
 * munmap system call implemenations
 */

long vm_area_unmap(struct exec_context *current, u64 addr, int length)
{
    if (length < 0)
    {
        return -EINVAL;
    }

    struct vm_area *memptr = current->vm_area;
    struct vm_area *prev = NULL;
    prev->vm_next = memptr;

    u64 alignedLen = 0;

    while (memptr != NULL && length > 0)
    {

        // case 1: unmapped area lies completely inside a vm_area
        if (memptr->vm_start <= addr && memptr->vm_end >= addr + length)
        {
            // page align the number of bytes being unmapped
            alignedLen = (length + 0xFFF) & ~(0xFFF);

            // deallocate the PFNs mapped with the pages in this vma
            freeAllPFNs(addr,addr+alignedLen);

            // check if complete vm_area is to be unmapped
            if (memptr->vm_end == addr + alignedLen && memptr->vm_start == addr)
            {
                // deallocate this vma altogether
                struct vm_area *vmFreed = memptr;
                prev->vm_next = memptr->vm_next;
                os_free(vmFreed, sizeof(vmFreed));
                stats->num_vm_area--;
            }

            // if starting portion of vm_area is unmapped
            else if (memptr->vm_start == addr)
            {
                memptr->vm_start = addr + alignedLen;
            }

            // if ending portion of vm_area is unmapped
            else if (memptr->vm_end == addr + alignedLen)
            {
                memptr->vm_end = addr;
            }

            // if middle portion of vm_area is unmapped
            else
            {
                // split the vm_area into two
                struct vm_area *vm1 = os_alloc(sizeof(struct vm_area));
                struct vm_area *vm2 = os_alloc(sizeof(struct vm_area));
                struct vm_area *vmFreed = memptr;
                vm1->access_flags = vm2->access_flags = memptr->access_flags;

                vm1->vm_start = memptr->vm_start;
                vm1->vm_end = addr;
                vm1->vm_next = vm2;
                prev->vm_next = vm1;

                vm2->vm_start = addr + alignedLen;
                vm2->vm_end = memptr->vm_end;
                vm2->vm_next = memptr->vm_next;

                os_free(vmFreed, sizeof(vmFreed));
                stats->num_vm_area++;
            }

            return 0;
        }

        // case 2: unmapped area starts at current vm_area but ends somewhere after
        else if ((memptr->vm_start <= addr && memptr->vm_end > addr) && (addr + length > memptr->vm_end))
        {
            freeAllPFNs(addr,memptr->vm_end);
            // modify the starting portion of the current vm_area
            memptr->vm_end = addr;

            // if the unmapped region doesn't spread till the next vm_area, return
            if ( memptr->vm_next == NULL || (addr + length < memptr->vm_next->vm_start) )
            {
                return 0;
            }

            // else simply move forward in the vm_area list with a new a starting address of region to be unmapped
            else
            {
                length = addr + length - memptr->vm_next->vm_start;
                addr = memptr->vm_next->vm_start;
            }
        }

        // case 3: unmapped region began just before current vm_area
        else if (addr < memptr->vm_start && addr + length >= memptr->vm_start)
        {
            // modify starting addr and length of region to be unmapped and treat this as case 1 / case 2
            length = addr + length - memptr->vm_start;
            addr = memptr->vm_start;
            continue;
        }

        // case 4: if we have already moved past region to be unmapped
        else if (memptr->vm_start > addr + length || memptr->vm_next == NULL)
        {
            return 0;
        }

        prev = memptr;
        memptr = memptr->vm_next;
    }

    return -EINVAL;
}

/**
 * Function will invoked whenever there is page fault for an address in the vm area region
 * created using mmap
 */

long vm_area_pagefault(struct exec_context *current, u64 addr, int error_code)
{
    if (addr < 0)
    {
        return -EINVAL;
    }

    // find the vm_area corresponding to the faulting address
    struct vm_area *vma = current->vm_area;
    int flag = 0;
    while (vma != NULL)
    {
        if (vma->vm_start <= addr && vma->vm_end > addr)
        {
            flag = 1;
            break;
        }
        else if (addr < vma->vm_start)
            break;
        vma = vma->vm_next;
    }

    if (flag == 0)
    {
        return -EINVAL;
    }

    if (error_code == 0x6 && vma->access_flags == PROT_READ) {
        return -EINVAL;
    }

    // Another invalid fault can occur if there is a write access to a page with read only permission
    if (error_code == 0x7)
    {
        if (vma->access_flags == PROT_READ)
        {
            return -EINVAL;
        }
        else
        {
            return handle_cow_fault(current, addr, vma->access_flags);
        }
    }

    // Manipulate Page Table

    // Compute offsets required in each level of the page table
    u64 pgdIdx = (addr & PGD_MASK) >> PGD_SHIFT;
    u64 pudIdx = (addr & PUD_MASK) >> PUD_SHIFT;
    u64 pmdIdx = (addr & PMD_MASK) >> PMD_SHIFT;
    u64 pteIdx = (addr & PTE_MASK) >> PTE_SHIFT;

    // calculate the entry of in the first level of the page table
    u64 pgd_entry_VA = ((u64)osmap(current->pgd)) + (pgdIdx)*(PTE_SIZE);

    // check if page frame has been allocated for the next level of the page table
    if( ( *((u64*)pgd_entry_VA) & 1 ) == 0) {
        // allocate pfn for pud_t
        u64 pud_pfn = os_pfn_alloc(OS_PT_REG);
        if(pud_pfn == 0) {
            return -EINVAL;
        }

        // update the pgd_entry
        *((u64*)pgd_entry_VA) = (pud_pfn << ADDR_SHIFT) | 0x1;  // set the present bit along with the pfn value
        *((u64*)pgd_entry_VA) |= 0x10;                          // set the user bit

        if(vma->access_flags == 0x3) {
            *((u64*)pgd_entry_VA) |= 0x8;                       // set the read/write bit
        }
    }

    // calculate the entry of in the second level of the page table
    u64 pud_entry_VA = ((u64)osmap( ( ( *((u64*)pgd_entry_VA)  ) >> ADDR_SHIFT) ) ) + (pudIdx)*(PTE_SIZE);

    // check if page frame has been allocated for the next level of the page table
    if( ( *((u64*)pud_entry_VA) & 1 ) == 0) {
        // allocate pfn for pmd_t
        u64 pmd_pfn = os_pfn_alloc(OS_PT_REG);
        if(pmd_pfn == 0) {
            return -EINVAL;
        }

        // update the pud_entry
        *((u64*)pud_entry_VA) = (pmd_pfn << ADDR_SHIFT) | 0x1;  // set the present bit along with the pfn value
        *((u64*)pud_entry_VA) |= 0x10;                          // set the user bit

        if(vma->access_flags == 0x3) {
            *((u64*)pud_entry_VA) |= 0x8;                       // set the read/write bit
        }
    }

    // calculate the entry of in the third level of the page table
    u64 pmd_entry_VA = ((u64)osmap( ( ( *((u64*)pud_entry_VA)  ) >> ADDR_SHIFT) ) ) + (pmdIdx)*(PTE_SIZE);

    // check if page frame has been allocated for the next level of the page table
    if( ( *((u64*)pmd_entry_VA) & 1 ) == 0) {
        // allocate pfn for pte_t
        u64 pte_pfn = os_pfn_alloc(OS_PT_REG);
        if(pte_pfn == 0) {
            return -EINVAL;
        }

        // update the pmd_entry
        *((u64*)pmd_entry_VA) = (pte_pfn << ADDR_SHIFT) | 0x1;  // set the present bit along with the pfn value
        *((u64*)pmd_entry_VA) |= 0x10;                          // set the user bit

        if(vma->access_flags == 0x3) {
            *((u64*)pmd_entry_VA) |= 0x8;                       // set the read/write bit
        }
    }

    // calculate the entry of in the final level of the page table
    u64 pte_entry_VA = ((u64)osmap( ( ( *((u64*)pmd_entry_VA)  ) >> ADDR_SHIFT) ) ) + (pteIdx)*(PTE_SIZE);

    // check if page frame has been allocated for the final level of the page table
    if( ( *((u64*)pte_entry_VA) & 1 ) == 0) {
        // allocate pfn for pte_t
        u64 user_called_pfn = os_pfn_alloc(USER_REG);
        if(user_called_pfn == 0) {
            return -EINVAL;
        }

        // update the pte_entry
        *((u64*)pte_entry_VA) = (user_called_pfn << ADDR_SHIFT) | 0x1;  // set the present bit along with the pfn value
        *((u64*)pte_entry_VA) |= 0x10;                                  // set the user bit
        if(vma->access_flags == 0x3) {
            *((u64*)pte_entry_VA) |= 0x8;                               // set the read/write bit
        }
        else {
            *((u64*)pte_entry_VA) &= ~(0x8);  
        }

        asm volatile("invlpg (%0);" ::"r"(addr) : "memory");
    }

    return 1;
}

/**
 * cfork system call implemenations
 * The parent returns the pid of child process. The return path of
 * the child process is handled separately through the calls at the
 * end of this function (e.g., setup_child_context etc.)
 */

long do_cfork()
{
    u32 pid;
    struct exec_context *new_ctx = get_new_ctx();
    struct exec_context *ctx = get_current_ctx();
    /* Do not modify above lines
     *
     * */
    /*--------------------- Your code [start]---------------*/

    /*--------------------- Copying Parent Address Space starts ---------------*/

    // copy the address space of the parent into the child address space

    new_ctx->ppid = ctx->pid;
    new_ctx->type = ctx->type;
    new_ctx->state = READY;
    new_ctx->used_mem = ctx->used_mem;

    for(int i = 0; i < MAX_MM_SEGS; i++) {
        new_ctx->mms[i] = ctx->mms[i];
    }

    for(int i = 0; i < MAX_OPEN_FILES; i++) {
        new_ctx->files[i] = ctx->files[i];
    }

    // copy the user regs
    new_ctx->regs = ctx->regs;

    // copy the vm_areas
    if(ctx->vm_area != NULL) {
        struct vm_area *dummy_node = os_alloc(sizeof(struct vm_area));
        dummy_node->access_flags = 0x0;
        dummy_node->vm_start = MMAP_AREA_START;
        dummy_node->vm_end = MMAP_AREA_START + FOUR_KB;
        dummy_node->vm_next = NULL;
        new_ctx->vm_area = dummy_node;
        stats->num_vm_area++;

        struct vm_area* curr = dummy_node;
        struct vm_area* par = ctx->vm_area->vm_next;

        while (par != NULL) {
            struct vm_area* childVMA = os_alloc(sizeof(struct vm_area));
            childVMA->access_flags = par->access_flags;
            childVMA->vm_start = par->vm_start;
            childVMA->vm_end = par->vm_end;
            childVMA->vm_next = NULL;  
            curr->vm_next = childVMA;  
            curr = childVMA;           
            par = par->vm_next;  
            stats->num_vm_area++;
        }
    }

    /*--------------------- Copying Parent Address Space ends ---------------*/



    /*--------------------- Building Child Process Page Table starts ---------------*/

    // first allocate the first level of the page table
    u64 pfn = os_pfn_alloc(OS_PT_REG);
    new_ctx->pgd = pfn;

    // building page table for vm_areas

    u64 addr_start, addr_end;
    struct vm_area* parVMA = ctx->vm_area;

    while(parVMA != NULL) {
        int numPages = (parVMA->vm_end - parVMA->vm_start) / (FOUR_KB) ;
        for(int i = 0; i < numPages; i++) {
            copyPageToChildProcess(addr_start + i*(FOUR_KB), ctx, new_ctx);
        }
        parVMA = parVMA->vm_next;
    }

    // building page table for mm_segment

    // checking valid region for code segment
    addr_start = ctx->mms[MM_SEG_CODE].start;
    addr_end = ctx->mms[MM_SEG_CODE].next_free;
    copy_MM_Segment(addr_start,addr_end,ctx,new_ctx);

    // checking valid region for RO_data segment
    addr_start = ctx->mms[MM_SEG_RODATA].start;
    addr_end = ctx->mms[MM_SEG_RODATA].next_free;
    copy_MM_Segment(addr_start,addr_end,ctx,new_ctx);

    // checking valid region for data segment
    addr_start = ctx->mms[MM_SEG_DATA].start;
    addr_end = ctx->mms[MM_SEG_DATA].next_free;
    copy_MM_Segment(addr_start,addr_end,ctx,new_ctx);

    // checking valid region for stack segment
    addr_start = ctx->mms[MM_SEG_STACK].next_free - FOUR_KB;
    addr_end = ctx->mms[MM_SEG_STACK].end;
    copy_MM_Segment(addr_start,addr_end,ctx,new_ctx);

    pid = new_ctx->pid;

    /*--------------------- Building Child Process Page Table ends ---------------*/

    /*--------------------- Your code [end] ----------------*/

    /*
     * The remaining part must not be changed
     */
    copy_os_pts(ctx->pgd, new_ctx->pgd);
    do_file_fork(new_ctx);
    setup_child_context(new_ctx);
    return pid;
}

/* Cow fault handling, for the entire user address space
 * For address belonging to memory segments (i.e., stack, data)
 * it is called when there is a CoW violation in these areas.
 *
 * For vm areas, your fault handler 'vm_area_pagefault'
 * should invoke this function
 * */

long handle_cow_fault(struct exec_context *current, u64 vaddr, int access_flags)
{
    if(vaddr < 0) {
        return -EINVAL;
    }
    if(access_flags != 0x1 && access_flags != 0x3) {
        return -EINVAL;
    }

    u64 pgdIdx = (vaddr & PGD_MASK) >> PGD_SHIFT;
    u64 pudIdx = (vaddr & PUD_MASK) >> PUD_SHIFT;
    u64 pmdIdx = (vaddr & PMD_MASK) >> PMD_SHIFT;
    u64 pteIdx = (vaddr & PTE_MASK) >> PTE_SHIFT;

    u64 pgd_entry_VA = ((u64)osmap(current->pgd)) + (pgdIdx)*(PTE_SIZE);
    if(access_flags == 0x3) {
        *((u64*)pgd_entry_VA) |= 0x8;
    }
    u64 pud_entry_VA = ((u64)osmap( ( ( *((u64*)pgd_entry_VA)  ) >> ADDR_SHIFT) ) ) + (pudIdx)*(PTE_SIZE);
    if(access_flags == 0x3) {
        *((u64*)pud_entry_VA) |= 0x8;
    }
    u64 pmd_entry_VA = ((u64)osmap( ( ( *((u64*)pud_entry_VA)  ) >> ADDR_SHIFT) ) ) + (pmdIdx)*(PTE_SIZE);
    if(access_flags == 0x3) {
        *((u64*)pmd_entry_VA) |= 0x8;
    }
    u64 pte_entry_VA = ((u64)osmap( ( ( *((u64*)pmd_entry_VA)  ) >> ADDR_SHIFT) ) ) + (pteIdx)*(PTE_SIZE);

    // now we have to allocate a new page and copy the contents of the previous page
    u64 new_pfn = os_pfn_alloc(USER_REG);
    u64 old_pfn = (( *((u64*)pte_entry_VA)  ) >> ADDR_SHIFT) ;

    // copy the contents of the old page to the new page
    memcpy((char*)osmap(new_pfn),(char*)osmap(old_pfn),FOUR_KB);

    // point this process to the new pfn
    *((u64*)pte_entry_VA) = (new_pfn << ADDR_SHIFT) | 0x11;

    // update the access_permissions for this entry
    if(access_flags == 0x3) {
        *((u64*)pte_entry_VA) |= 0x8;
    }
    else {
        *((u64*)pte_entry_VA) &= ~(0x8);
    }

    // decrease the ref_count of the old pfn
    put_pfn(old_pfn);

    if(get_pfn_refcount(old_pfn) == 0) {
        os_pfn_free(USER_REG,old_pfn);
    }
    asm volatile("invlpg (%0);" ::"r"(vaddr) : "memory");

    return 1;
}
