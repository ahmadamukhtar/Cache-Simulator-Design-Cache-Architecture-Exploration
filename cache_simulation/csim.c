/*
 * csim.c - A cache simulator that can replay traces from Valgrind
 *     and output statistics such as number of hits, misses, and
 *     evictions.  The replacement policy is LRU.
 *
 * Implementation and assumptions:
 *  1. Each load/store can cause at most one cache miss.
 *  2. Instruction loads (I) are ignored, since we are interested in evaluating
 *     trans.c in terms of its data cache performance.
 *  3. data modify (M) is treated as a load followed by a store to the same
 *     address. Hence, an M operation can result in two cache hits, or a miss and a
 *     hit plus an possible eviction.
 *
 * The function printSummary() is given to print output.
 * Please use this function to print the number of hits, misses, and evictions.
 * This is crucial for the driver to evaluate your work. 
 */
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include "cachelab.h"

// #define DEBUG_ON 
#define ADDRESS_LENGTH 64

/****************************************************************************/
/***** DO NOT MODIFY THESE VARIABLE NAMES ***********************************/

/* Globals set by command line args */
int verbosity = 0; /* print trace if set */
int s = 0; /* set index bits */
int b = 0; /* block offset bits */
int E = 0; /* associativity */
char* trace_file = NULL;

/* Derived from command line args */
int S; /* number of sets S = 2^s In C, you can use "pow" function*/
int B; /* block size (bytes) B = 2^b In C, you can use "pow" function*/

/* Counters used to record cache statistics */
int miss_count = 0;
int hit_count = 0;
int eviction_count = 0;
/*****************************************************************************/


/* Type: Memory address 
 * Use this type whenever dealing with addresses or address masks
 */
typedef unsigned long long int mem_addr_t;

/* Type: Cache line
 * Added LRU counter field to implement LRU policy
 */
typedef struct cache_line {
    char valid;
    mem_addr_t tag;
    int lru_counter; // To keep track of Least Recently Used line
} cache_line_t;

typedef cache_line_t* cache_set_t;
typedef cache_set_t* cache_t;

/* The cache we are simulating */
cache_t cache;  

/* initCache - 
 * Allocate data structures to hold info regarding the sets and cache lines
 * Initialize valid and tag field with 0s.
 * calculate S = 2^s
 * use S and E while allocating the data structures here
 */
void initCache() {
    S = pow(2, s);  // Number of sets
    cache = (cache_set_t *)malloc(S * sizeof(cache_set_t));
    for (int i = 0; i < S; i++) {
        cache[i] = (cache_line_t *)malloc(E * sizeof(cache_line_t));
        for (int j = 0; j < E; j++) {
            cache[i][j].valid = 0;  // Initialize valid bit to 0
            cache[i][j].tag = 0;    // Initialize tag to 0
            cache[i][j].lru_counter = 0;  // Initialize LRU counter
        }
    }
}

/* freeCache - free each piece of memory you allocated using malloc 
 * inside initCache() function
 */
void freeCache() {
    for (int i = 0; i < S; i++) {
        free(cache[i]);
    }
    free(cache);
}

/* updateLRU - Update the LRU counters for a given set.
 * For each access, increase the counter of other cache lines and reset the one being accessed.
 */
void updateLRU(int setIndex, int lineIndex) {
    for (int i = 0; i < E; i++) {
        if (i != lineIndex && cache[setIndex][i].valid) {
            cache[setIndex][i].lru_counter++; // Increase counter for all other lines
        }
    }
    cache[setIndex][lineIndex].lru_counter = 0;  // Reset LRU counter for accessed line
}

/* accessData - Access data at memory address addr.
 *   If it is already in cache, increase hit_count
 *   If it is not in cache, bring it in cache, increase miss count.
 *   Also increase eviction_count if a line is evicted.
 *   Implement Least-Recently-Used (LRU) cache replacement policy
 */
void accessData(mem_addr_t addr) {
    mem_addr_t tag = addr >> (s + b);  // Extract the tag from the address
    int setIndex = (addr >> b) & ((1 << s) - 1);  // Extract the set index

    int hit = 0;
    int empty_index = -1;
    int lru_index = 0;
    int max_lru = -1;

    // Check for hits, find empty slots, or determine LRU line
    for (int i = 0; i < E; i++) {
        if (cache[setIndex][i].valid) {
            if (cache[setIndex][i].tag == tag) {
                hit = 1;
                hit_count++;  // Cache hit
                updateLRU(setIndex, i); // Update LRU counter for the accessed line
                break;
            } else if (cache[setIndex][i].lru_counter > max_lru) {
                max_lru = cache[setIndex][i].lru_counter;
                lru_index = i;  // Track the least recently used line
            }
        } else if (empty_index == -1) {
            empty_index = i;  // Find the first empty slot
        }
    }

    if (!hit) {
        miss_count++;
        if (empty_index != -1) {
            // Place the new line in the empty cache slot
            cache[setIndex][empty_index].valid = 1;
            cache[setIndex][empty_index].tag = tag;
            updateLRU(setIndex, empty_index);
        } else {
            // Evict the least recently used line
            eviction_count++;
            cache[setIndex][lru_index].tag = tag;
            updateLRU(setIndex, lru_index);
        }
    }

    // Verbose mode output
    if (verbosity) {
        printf("Address: %llx - ", addr);
        if (hit) {
            printf("hit\n");
        } else {
            printf("miss");
            if (empty_index == -1) {
                printf(" eviction");
            }
            printf("\n");
        }
    }
}

/* replayTrace - replays the given trace file against the cache 
 * reads the input trace file line by line
 * extracts the type of each memory access : L/S/M
 * "L" -> load, "S" -> store, "M" -> modify (load + store)
 * Ignore instruction fetch "I"
 */
void replayTrace(char* trace_fn) {
    char buf[1000];
    mem_addr_t addr = 0;
    unsigned int len = 0;
    FILE* trace_fp = fopen(trace_fn, "r");

    while (fgets(buf, 1000, trace_fp) != NULL) {
        if (buf[1] == 'S' || buf[1] == 'L' || buf[1] == 'M') {
            sscanf(buf + 3, "%llx,%u", &addr, &len);
            accessData(addr);  // Call accessData for each memory access
            if (buf[1] == 'M') {
                accessData(addr);  // For 'M' operation, access twice
            }
        }
    }
    fclose(trace_fp);
}

/* printUsage - Print usage info */
void printUsage(char* argv[])
{
    printf("Usage: %s [-hv] -s <num> -E <num> -b <num> -t <file>\n", argv[0]);
    printf("Options:\n");
    printf("  -h         Print this help message.\n");
    printf("  -v         Optional verbose flag.\n");
    printf("  -s <num>   Number of set index bits.\n");
    printf("  -E <num>   Number of lines per set.\n");
    printf("  -b <num>   Number of block offset bits.\n");
    printf("  -t <file>  Trace file.\n");
    printf("\nExamples:\n");
    printf("  linux>  %s -s 4 -E 1 -b 4 -t traces/yi.trace\n", argv[0]);
    printf("  linux>  %s -v -s 8 -E 2 -b 4 -t traces/yi.trace\n", argv[0]);
    exit(0);
}

/* main - Main routine */
int main(int argc, char* argv[])
{
    char c;
    
    // Parse the command line arguments: -h, -v, -s, -E, -b, -t 
    while( (c=getopt(argc,argv,"s:E:b:t:vh")) != -1){
        switch(c){
        case 's':
            s = atoi(optarg);
            break;
        case 'E':
            E = atoi(optarg);
            break;
        case 'b':
            b = atoi(optarg);
            break;
        case 't':
            trace_file = optarg;
            break;
        case 'v':
            verbosity = 1;
            break;
        case 'h':
            printUsage(argv);
            exit(0);
        default:
            printUsage(argv);
            exit(1);
        }
    }

    /* Make sure that all required command line args were specified */
    if (s == 0 || E == 0 || b == 0 || trace_file == NULL) {
        printf("%s: Missing required command line argument\n", argv[0]);
        printUsage(argv);
        exit(1);
    }

    /* Initialize cache */
    initCache();

#ifdef DEBUG_ON
    printf("DEBUG: S:%u E:%u B:%u trace:%s\n", S, E, B, trace_file);
#endif
 
    /* Replay the memory access trace */
    replayTrace(trace_file);

    /* Free allocated memory */
    freeCache();

    /* Output the hit and miss statistics for the autograder */
    printSummary(hit_count, miss_count, eviction_count);
    return 0;
}
