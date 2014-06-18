// 
// memslice is a simple utility to list tasks & read + write their memory.
// run as root or add the task for pid entitlement. 
//
// by adc
//
#include <mach/mach.h>
#include <unistd.h> 
#include <sys/types.h> 
#include <sys/ptrace.h> 
#include <stdlib.h>
#include <stdio.h>
#include <libproc.h>
#include <assert.h>
#include <mach/mach_vm.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-prototypes"

char *prot_str[] = {
                    "---",
                    "r--",
                    "-w-",
                    "rw-",
                    "--x",
                    "r-x",
                    "rw-",
                    "rwx"
                   };

task_t pid2task(pid_t pid)
{
    mach_port_t task;
    kern_return_t ret;
    ret = task_for_pid(mach_task_self(), pid, &task);
    assert(ret == 0);
    return task;
}


/*
    Enumerate the memory regions of a process using proc_pidinfo
*/
void examine_process_proc_pidinfo(pid_t pid, task_t task)
{
    kern_return_t ret;
    struct proc_regioninfo regioninfo;
    
    uint64_t addr;
    
    addr = 0;
    
    do
    {
        if (addr)
        {
            printf("-> %s 0x%.16llx - 0x%.16llx (%llx)\n",
                    prot_str[regioninfo.pri_protection & 7],
                    regioninfo.pri_address,
                    addr,
                    regioninfo.pri_size);
        }
        
        //
        // proc_pidinfo is nice because theres no need to worry about
        // 32 bit vs 64bit, however page permission information will be lacking
        // the same permission granuality as vm_region_recurse. in detail,
        // the entire shared cache will show up as r-- when theres more going on.
        //
        ret = proc_pidinfo(pid, PROC_PIDREGIONINFO, addr, &regioninfo, sizeof(regioninfo));
        
        addr = regioninfo.pri_address + regioninfo.pri_size;
    }
    while (ret == sizeof(regioninfo));
}

/*
    Enumerate the memory regions of a process using mach_vm_region_recurse
*/
void examine_process(pid_t pid, task_t task)
{
	mach_vm_address_t address = 0x0;
	mach_vm_size_t size = 0;
    kern_return_t result;

    natural_t nesting_level = 0;
    struct vm_region_submap_info_64 submap_info;
    mach_msg_type_number_t info_count = VM_REGION_SUBMAP_INFO_COUNT_64;
    
    //
    // TBD handle vm_region_submap_info for 32-bit processes.
    //
    
    while (1)
    {
        // get the next region
        nesting_level= 0;
        result = mach_vm_region_recurse(task,
                                       &address,
                                       &size,
                                       &nesting_level,
                                       (vm_region_recurse_info_t)&submap_info,
                                       &info_count);

        if (result) break;
        
        if (submap_info.is_submap)
        {
recurse:

            nesting_level++;
            while (1)
            {
                result = mach_vm_region_recurse(task,
                                               &address,
                                               &size,
                                               &nesting_level,
                                               (vm_region_recurse_info_t)&submap_info,
                                               &info_count);
                if (result) break;
                
                printf("%d [%d] @@ %s addr 0x%llx size %llu\n",
                    submap_info.is_submap,
                    nesting_level,
                    prot_str[submap_info.protection & 7],
                    address,
                    size);
                
                if (submap_info.is_submap)
                {
                    goto recurse;
                }

                address += size;
            }
        }
        
        printf("%d [%d] @@ %s addr 0x%llx size %llu\n",
            submap_info.is_submap,
            nesting_level,
            prot_str[submap_info.protection & 7],
            address,
            size);
        address += size;
    }
}



/*
    Write 'length' data into the target process at 'address', sourcing
    the data from stdin.
*/
void write_process(task_t task, uint64_t address, uint64_t length)
{
    char *buf;
    off_t i;
    ssize_t ret;
    
    buf = malloc(length);
    
    assert(buf != 0);
    
    for (i = 0; i < length; )
    {
        ret = read(STDIN_FILENO, buf, length - i);
        if (ret <= 0)
            break;
        i += ret;
    }
    assert(i == length);
    
    vm_write(task, address, (vm_offset_t)buf, (mach_msg_type_number_t)length);
    
    free(buf);

}

/*
    Read data the target process and write it to stdout
*/
void read_process(task_t task, uint64_t address, uint64_t length)
{
    kern_return_t ret;
    pointer_t data;
    u_int readlen;
    
    ret = vm_read(task, address, length, &data, &readlen);

    if (ret == 0)
    {
        write(STDOUT_FILENO, (char *)data, readlen);
        vm_deallocate(mach_task_self(), data, readlen);
    }
    else
    {
        printf("[-] vm read of %llx:%lld failed with ret=%d\n",
                address, length, ret);
    }
}


int main(int argc, char *argv[])
{
    pid_t pid;
    uint64_t address;
    uint64_t length;
    char ch;
    int write;
    
    address = 0;
    length = 0;
    write = 0;
    
    while ((ch = getopt(argc, argv, "p:r:w:l:")) != -1)
    {
        switch (ch) {
          case 'p':
            pid = atoi(optarg);
            break;
          case 'w':
            write = 1;
          case 'r':
            address = strtoull(optarg, 0, 0);
            break;
          case 'l':
            length = strtoull(optarg, 0, 0);
            break;
          default:
            break;
        }
    }

    mach_port_t task;

    task = pid2task(pid);
    
    task_suspend(task);
    
    if (length == 0)
    {
        examine_process(pid, task);
    }
    else if (write)
    {
        write_process(task, address, length);
    }
    else
    {
        read_process(task, address, length);
    }
    
    task_resume(task);
    
    return 0;
}

#pragma clang diagnostic pop
