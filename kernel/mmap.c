#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"


static struct vma* find_vma(struct proc *p){
    struct vma *vma = 0;
    int i;

    for(i = 0;i < MAX_VMA;i++){
        if(!p->vmas[i].used){
            p->vmas[i].used = 1;
            vma = &p->vmas[i];
            break;
        }
    }

    return vma;
}

/* stupid way to find a continuous virtual addr range
   between MMAP_AREA and TRAPFRAME - PGSIZE
*/
uint64 build_vma(int length,int prot,int flags,struct file *fp,int offset){
    struct vma *vma = 0;
    uint64 addr = -1;
    struct proc *p = myproc();

    if((prot & PROT_WRITE) && (flags & MAP_SHARED) && !(fp->writable)){
        return addr;
    }

    if((vma = find_vma(p)) == 0){
        return addr;
    }

    addr = p->unused_addr;
    vma->addr = addr;
    vma->length = length;
    vma->prot = prot;
    vma->flags = flags;
    vma->offset = offset;
    // increment ref of file in case fp disappear when the file is closed.
    vma->f = filedup(fp);

    // stupid code,not scalable
    p->unused_addr += length;
    if(p->unused_addr >= MMAP_AREA + MAX_MMAP_PAGE * PGSIZE){
        p->unused_addr = MMAP_AREA;
    }

    return addr;
}

static int writeback(struct proc *p,struct vma *vma,int length){
    // assume the length is page-align
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0,n,r;
    uint64 virt_addr = vma->addr;

    if(vma->f->type != FD_INODE){
        // cannot handle not regular file
        return -1;
    }

    ilock(vma->f->ip);
    n = length > (vma->f->ip->size - vma->offset) ?
    (vma->f->ip->size - vma->offset) : length;
    iunlock(vma->f->ip);

    while(i < n){
        uint64 pa = walkaddr(p->pagetable,virt_addr);
        // if left is below PGSIZE,n1 = left
        // or just write PGSIZ
        int n1 = PGSIZE > (n - i) ? (n - i) : PGSIZE;
        n1 = n1 > (PGSIZE - (virt_addr % PGSIZE)) ? 
        (PGSIZE - (virt_addr % PGSIZE)) : n1;
        if(n1 > max){
            n1 = max;
        }

        if(!pa){
            printf("[writeback]: not mapped!\n");
        }

        begin_op();
        ilock(vma->f->ip);
        if ((r = writei(vma->f->ip, 0, pa + (i % PGSIZE), vma->offset, n1)) > 0)
            vma->offset += r;
        iunlock(vma->f->ip);
        end_op();

        if(r != n1){
            // error from writei
            break;
        }
      i += r;
      virt_addr += r;
    }
    
    if(i != n){
        printf("[writeback]: not enough write: i: %d n: %d\n",i,n);
    } else {
        //printf("[writeback]: OK i: %d n: %d\n",i,n);
    }

    return i == n ? 0: -1;
}

/* 
    unmap addr~addr+length area
*/
int unmap_vma(uint64 addr,int length){
    struct proc *p = myproc();
    struct vma *vma = 0;
    int i;

    for(i = 0;i < MAX_VMA;i++){
        if(p->vmas[i].used){
            // we assmue munmap always unmap the start of the still
            // valid vma area
            if(p->vmas[i].addr == addr){
                vma = &p->vmas[i];
                break;
            }
        }
    }

    if(!vma){
        return -1;
    }

    // write back if vma is MAP_SHARED,no checking dirty bit of PTE
    // and the vma is writable,or there is no modification on the file
    if((vma->flags & MAP_SHARED) && (vma->prot & PROT_WRITE)){
        if(writeback(p,vma,length) < 0){
            printf("[unmap_vma]: failed to writeback\n");
            return -1;
        }
    }

    // assume that length is page-aligned
    for(i = 0;i < length / PGSIZE;i++){
        uint64 virt_addr = vma->addr + i*PGSIZE;
        pte_t *pte = walk(p->pagetable,virt_addr,0);
        if(!(*pte & PTE_V)){
            // process did not access this virt_addr of the vma
            // thus pagefault_handler is not triggered
            // thus the virt_addr is not mapped.
            continue;
        }
        uvmunmap(p->pagetable,virt_addr,1,1);
    }

    vma->addr += length;
    vma->length -= length;

    if(vma->length == 0){
    // all the page in this vma is unmapped
        fileclose(vma->f);
        vma->used = 0;
    }

    return 0;
}


int pagefault_handler_vma(uint64 addr,struct proc *p){
    struct vma *vma = 0;
    int i, perm = 0;
    void *pa;


    for(i = 0;i < MAX_VMA;i++){
        if(p->vmas[i].used){
            if(addr >= p->vmas[i].addr 
                && addr < p->vmas[i].addr + p->vmas[i].length){
                    vma = &p->vmas[i];
                    break;
                }
        }
    }

    if(!vma){
        // virt addr not in any vma of this process
        return -1;
    }

    // make it page-align
    addr = PGROUNDDOWN(addr);
    pa = kalloc();
    if(!pa){
        panic("pagefault_handler_vma: kalloc failed!");
    }
    memset(pa,0,PGSIZE);
    //we assume the offset of mapped file is always 0 in this lab
    ilock(vma->f->ip);
    if(readi(vma->f->ip,0,(uint64)pa,addr - vma->addr,PGSIZE) < 0){
        panic("pagefault_handler_vma");
    }

    if(vma->prot & PROT_READ){
        perm |= PTE_R;
    }

    if(vma->prot & PROT_WRITE){
        perm |= PTE_W;
    }

    if(vma->prot & PROT_EXEC){
        perm |= PTE_X;
    }

    if(mappages(p->pagetable,addr,PGSIZE,(uint64)pa,perm|PTE_U) != 0){
        panic("pagefault_handler_vma: failed to map!");
    }

    iunlock(vma->f->ip);

    return 0;
}