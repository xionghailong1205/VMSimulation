#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct
{
    int pageNo;
    int modified;
    int timestamp;
    int reference;
} page;

enum repl
{
    rand_repl,
    fifo,
    lru,
    clock
};
int createMMU(int);
int checkInMemory(int);
int allocateFrame(int);
page selectVictim(int, enum repl);
const int pageoffset = 12; // Page size is fixed to 4 KB
int numFrames;

page *pageTable;
int timestamp = 0;
int clockPointer = 0;

/* Creates the page table structure to record memory allocation */
int createMMU(int frames)
{
    pageTable = (page *)malloc(frames * sizeof(page));
    if (!pageTable)
        return -1;

    for (int i = 0; i < frames; i++)
    {
        pageTable[i].pageNo = -1;
        pageTable[i].modified = 0;
        pageTable[i].timestamp = -1;
        pageTable[i].reference = 0;
    }

    return 0;
}

/* Checks for residency: returns frame no or -1 if not found */
int checkInMemory(int page_number)
{
    for (int i = 0; i < numFrames; i++)
    {
        if (pageTable[i].pageNo == page_number)
        {
            pageTable[i].timestamp = timestamp++;
            pageTable[i].reference = 1;
            return i;
        }
    }
    return -1;
}

/* allocate page to the next free frame and record where it put it */
int allocateFrame(int page_number)
{
    for (int i = 0; i < numFrames; i++)
    {
        if (pageTable[i].pageNo == -1)
        {
            pageTable[i].pageNo = page_number;
            pageTable[i].modified = 0;
            pageTable[i].timestamp = timestamp++;
            pageTable[i].reference = 1;
            return i;
        }
    }
    return -1; // No free frames (should not occur)
}

/* Selects a victim for eviction/discard according to the replacement algorithm, returns chosen frame_no */
page selectVictim(int page_number, enum repl mode)
{
    page victim;
    int victim_index = -1;

    if (mode == lru)
    {
        int oldest_timestamp = timestamp;
        for (int i = 0; i < numFrames; i++)
        {
            if (pageTable[i].timestamp < oldest_timestamp)
            {
                oldest_timestamp = pageTable[i].timestamp;
                victim = pageTable[i];
                victim_index = i;
            }
        }
    }
    else if (mode == clock)
    {
        while (1)
        {
            if (pageTable[clockPointer].reference == 0)
            {
                victim = pageTable[clockPointer];
                victim_index = clockPointer;
                clockPointer = (clockPointer + 1) % numFrames;
                break;
            }
            else
            {
                pageTable[clockPointer].reference = 0;
                clockPointer = (clockPointer + 1) % numFrames;
            }
        }
    }

    printf("代码执行");

    // Mark the chosen victim's frame as free
    if (victim_index != -1)
    {
        pageTable[victim_index].pageNo = -1;
        pageTable[victim_index].modified = 0;
        pageTable[victim_index].reference = 0;
    }

    return victim;
}

int main(int argc, char *argv[])
{
    char *tracename;
    int page_number, frame_no, done;
    int do_line, i;
    int no_events, disk_writes, disk_reads;
    int debugmode;
    enum repl replace;
    int allocated = 0;
    unsigned address;
    char rw;
    page Pvictim;
    FILE *trace;

    if (argc < 5)
    {
        printf("Usage: ./memsim inputfile numberframes replacementmode debugmode \n");
        exit(-1);
    }
    else
    {
        tracename = argv[1];
        trace = fopen(tracename, "r");
        if (trace == NULL)
        {
            printf("Cannot open trace file %s \n", tracename);
            exit(-1);
        }
        numFrames = atoi(argv[2]);
        if (numFrames < 1)
        {
            printf("Frame number must be at least 1\n");
            exit(-1);
        }
        if (strcmp(argv[3], "lru") == 0)
            replace = lru;
        else if (strcmp(argv[3], "rand") == 0)
            replace = rand_repl;
        else if (strcmp(argv[3], "clock") == 0)
            replace = clock;
        else if (strcmp(argv[3], "fifo") == 0)
            replace = fifo;
        else
        {
            printf("Replacement algorithm must be rand/fifo/lru/clock  \n");
            exit(-1);
        }

        if (strcmp(argv[4], "quiet") == 0)
            debugmode = 0;
        else if (strcmp(argv[4], "debug") == 0)
            debugmode = 1;
        else
        {
            printf("Replacement algorithm must be quiet/debug  \n");
            exit(-1);
        }
    }

    done = createMMU(numFrames);
    if (done == -1)
    {
        printf("Cannot create MMU\n");
        exit(-1);
    }
    no_events = 0;
    disk_writes = 0;
    disk_reads = 0;

    do_line = fscanf(trace, "%x %c", &address, &rw);
    while (do_line == 2)
    {
        page_number = address >> pageoffset;
        frame_no = checkInMemory(page_number); /* ask for physical address */

        // 说明不在内存中
        if (frame_no == -1)
        {
            disk_reads++; /* Page fault, need to load it into memory */
            if (debugmode)
                printf("Page fault %8d \n", page_number);
            if (allocated < numFrames)
            { /* allocate it to an empty frame */
                frame_no = allocateFrame(page_number);
                allocated++;
            }
            else
            {
                Pvictim = selectVictim(page_number, replace); /* returns page number of the victim  */
                if (Pvictim.modified)
                {
                    disk_writes++;
                    if (debugmode)
                        printf("Disk write %8d \n", Pvictim.pageNo);
                }
                else
                {
                    if (debugmode)
                        printf("Discard    %8d \n", Pvictim.pageNo);
                }
                frame_no = allocateFrame(page_number); /* reallocate the new page */
            }
        }

        if (rw == 'W')
        {
            pageTable[frame_no].modified = 1;
        }

        if (debugmode)
        {
            if (rw == 'R')
                printf("reading    %8d \n", page_number);
            else if (rw == 'W')
                printf("writing    %8d \n", page_number);
        }

        no_events++;
        do_line = fscanf(trace, "%x %c", &address, &rw);
    }

    printf("total memory frames:  %d\n", numFrames);
    printf("events in trace:      %d\n", no_events);
    printf("total disk reads:     %d\n", disk_reads);
    printf("total disk writes:    %d\n", disk_writes);
    printf("page fault rate:      %.4f\n", (float)disk_reads / no_events);

    return 0;
}
