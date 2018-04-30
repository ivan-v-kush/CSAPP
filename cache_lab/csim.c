#define _GNU_SOURCE
#include "cachelab.h"
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

typedef struct {
// the address space have 36 bits
    short valid;
    long tag;
    int set;
    //int block;
}CacheLine;

//global variables for this cache
int s=0, E=0, b=0, c, verbose=0;
int hit=0, miss=0, evict=0;
char *t;

void readCacheParameter(int argc, char *argv[]);
int getSet(long address);
long getTag(long address);
void loadAndStore(CacheLine *cache, long address, char* line);
void modify(CacheLine *cache, long address, char* line);

int main(int argc, char *argv[])
{

    readCacheParameter(argc, argv);
    char *line = NULL;
    size_t size=0;
    FILE *trace;
    trace = fopen(t, "r");
    if(trace == NULL){
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    //create cache struct
    int num_of_set = pow(2, s);
    CacheLine *cache = malloc(num_of_set * sizeof(CacheLine));
    for(int i = 0; i < num_of_set; ++i){
        cache[i].set = i;
        cache[i].valid = 0;
        cache[i].tag = -1;
    }

    while(getline(&line, &size, trace) != -1){
        char instruction=0;
        long address=0;
        int len = strlen(line);
        instruction = line[1];
        //use a temp array to store the address string
        char temp[len-5];
        for(int i = 0; i < len-5; ++i){
            temp[i] = line[i+3];
        }
        char parsed_line[len-2];
        for(int i = 0 ; i < len-2; ++i){
            parsed_line[i] = line[i+1];
        }
        parsed_line[len-1]='\0';
        char *temp_ptr; //for the usage of strtol
        address = strtol(temp, &temp_ptr, 16);
        // if(cache_set.valid && cache_set.tag == getTag(address)){
        //     hit ++;
        //     if(verbose) printf("%s hit\n", parsed_line);
        // }else if(cache_set.valid && cache_set.tag != getTag(address)){
        //     cache[getSet(address)].tag = getTag(address);
        //     miss ++;
        //     evict ++;
        //     if(verbose) printf("%s miss eviction\n", parsed_line);
        // }else{
        //     cache[getSet(address)].valid = 1;
        //     cache[getSet(address)].tag = getTag(address);
        //     miss ++;
        //     if(verbose) printf("%s miss\n", parsed_line);
        // }
        if (instruction=='L' || instruction == 'S'){
            loadAndStore(&cache[getSet(address)], address, parsed_line);
        }else{
            modify(&cache[getSet(address)], address, parsed_line);
        }
    }
    printSummary(hit, miss, evict);
    return 0;
}

void readCacheParameter(int argc, char *argv[]){
    while((c = getopt(argc, argv, "vs:E:b:t:")) != -1){
        switch(c){
            case 'v':
                verbose = 1;
                //printf("verbose is %d\n", verbose);
                break;
            case 's':
                s = atoi(optarg);
                //printf("s is %d\n", s);
                break;
            case 'E':
                E = atoi(optarg);
                //printf("E is %d\n", E);
                break;
            case 'b':
                b = atoi(optarg);
                //printf("b is %d\n", b);
                break;
            case 't':
                t = optarg;
                //printf("t is %s\n", t);
                break;
            case '?':
                if(optopt == 's' && optopt == 'E' && optopt == 'b' && optopt == 't')
                    fprintf(stderr, "Options -%c requires an argument\n", optopt);
                else if(isprint(optopt))
                    fprintf(stderr, "Unkown option `-%c`\n",optopt);
                return;
            default:
                abort();
        }
    }
}

int getSet(long address){
    long temp = address >> b;
    int set_num = pow(2, s);
    return temp % set_num;
}

long getTag(long address){
    unsigned long temp= address;
    temp = temp >> (b+s);
    return temp;
}

void loadAndStore(CacheLine *cache, long address, char* parsed_line){
    CacheLine cache_set = *cache;
    if(cache_set.valid && cache_set.tag == getTag(address)){
        hit ++;
        if(verbose) printf("%s hit\n", parsed_line);
    }else if(cache_set.valid && cache_set.tag != getTag(address)){
        cache[getSet(address)].tag = getTag(address);
        miss ++;
        evict ++;
        if(verbose) printf("%s miss eviction\n", parsed_line);
    }else{
        (*cache).valid = 1;
        (*cache).tag = getTag(address);
        miss ++;
        if(verbose) printf("%s miss\n", parsed_line);
    }
}

void modify(CacheLine *cache, long address, char* parsed_line){
    CacheLine cache_set = *cache;
    if(cache_set.valid && cache_set.tag == getTag(address)){
        hit += 2;
        if(verbose) printf("%s hit hit\n", parsed_line);
    }else if(cache_set.valid && cache_set.tag != getTag(address)){
        cache[getSet(address)].tag = getTag(address);
        miss ++;
        evict ++;
        hit ++;
        if(verbose) printf("%s miss eviction hit\n", parsed_line);
    }else{
        (*cache).valid = 1;
        (*cache).tag = getTag(address);
        miss ++;
        hit ++;
        if(verbose) printf("%s miss hit\n", parsed_line);
    }
}
