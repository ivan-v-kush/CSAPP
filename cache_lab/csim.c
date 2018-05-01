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
    unsigned long tag;
}CacheLine;

typedef struct{
    CacheLine *cache_line;
    int *used_order;
}CacheSet;

//global variables for this cache
int s=0, E=0, b=0, c, verbose=0;
int hit=0, miss=0, evict=0;
char *t;

void readCacheParameter(int argc, char *argv[]);
unsigned int getSet(long address);
unsigned long getTag(long address);
void loadAndStore(CacheSet *cache, long address, char* line);
void modify(CacheSet *cache, long address, char* line);
void parseLine(char *line, char parsed_line[]);

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
    CacheSet *cache_set = malloc(num_of_set * sizeof(CacheSet));
    for(int i = 0; i < num_of_set; ++i){
        cache_set[i].cache_line = malloc(E * sizeof(CacheLine));
        cache_set[i].used_order = malloc(E * sizeof(int));
        for(int j = 0; j < E; ++j){
            cache_set[i].cache_line[j].valid = 0;
            cache_set[i].cache_line[j].tag = -1;
            cache_set[i].used_order[j] = -1;
        }
    }

    while(getline(&line, &size, trace) != -1){
        char instruction=0;
        long address=0;
        int len = strlen(line);
        if(line[0] == 'I'){
            continue;
        }
        instruction = line[1];

        //use a temp array to store the address string
        char temp[len-5];
        for(int i = 0; i < len-5; ++i){
            temp[i] = line[i+3];
        }
        temp[len-5] = '\0';
        char parsed_line[len-1];
        for(int i = 0 ; line[i+1] != '\0'; ++i){
            parsed_line[i] = line[i+1];
        }
        parsed_line[len-2]='\0';
        char *temp_ptr; //for the usage of strtol
        address = strtol(temp, &temp_ptr, 16);

        if (instruction=='L' || instruction == 'S'){
            loadAndStore(&cache_set[getSet(address)], address, parsed_line);
        }else if(instruction=='M'){
            modify(&cache_set[getSet(address)], address, parsed_line);
        }
    }
    printSummary(hit, miss, evict);
    for(int i = 0; i < num_of_set; ++i){
        free(cache_set[i].cache_line);
        free(cache_set[i].used_order);
    }
    free(cache_set);
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

unsigned int getSet(long address){
    unsigned long temp = address;
    temp = temp >> b;
    int set_num = pow(2, s);
    return temp % set_num;
}

unsigned long getTag(long address){
    unsigned long temp= address;
    temp = temp >> (b+s);
    return temp;
}

void changeUsedOrder(int *order, int index){
    if(E==1) return;
    int position = -1;
    for(int i = 0 ; i < E; ++i){
        if(order[i] == index){
            position = i;
            break;
        }
    }
    if(position == -1){
        for(int i = E-1; i > 0; --i){
            order[i] = order[i-1];
        }
        order[0] = index;
    }else{
        for(int i = position; i > 0; --i){
            order[i] = order[i-1];
        }
        order[0] = index;
    }
    return;
}

void loadAndStore(CacheSet *cache, long address, char* parsed_line){
    for(int i = 0; i < E; ++i){
        CacheLine cache_line = (*cache).cache_line[i];
        if(cache_line.valid && cache_line.tag == getTag(address)){
            hit ++;
            changeUsedOrder((*cache).used_order, i);
            if(verbose) printf("%s hit\n", parsed_line);
            break;
        }else if(cache_line.valid && cache_line.tag != getTag(address)){
            if(i==E-1){
                int evicted = 0;
                if(E!=1)
                    evicted = (*cache).used_order[E-1];
                (*cache).cache_line[evicted].tag = getTag(address);
                changeUsedOrder((*cache).used_order, evicted);
                miss ++;
                evict ++;
                if(verbose) printf("%s miss eviction\n", parsed_line);
            }
        }else{
            (*cache).cache_line[i].valid = 1;
            (*cache).cache_line[i].tag = getTag(address);
            miss ++;
            changeUsedOrder((*cache).used_order, i);
            if(verbose) printf("%s miss\n", parsed_line);
            break;
        }
    }
}

void modify(CacheSet *cache, long address, char* parsed_line){
    for(int i = 0; i < E; ++i){
        CacheLine cache_line = (*cache).cache_line[i];
        if(cache_line.valid && cache_line.tag == getTag(address)){
            hit +=2;
            changeUsedOrder((*cache).used_order, i);
            if(verbose) printf("%s hit hit\n", parsed_line);
            break;
        }else if(cache_line.valid && cache_line.tag != getTag(address)){
            if(i==E-1){
                int evicted = 0;
                if(E!=1)
                    evicted = (*cache).used_order[E-1];
                (*cache).cache_line[evicted].tag = getTag(address);
                changeUsedOrder((*cache).used_order, evicted);
                miss ++;
                evict ++;
                hit ++;
                if(verbose) printf("%s miss eviction hit\n", parsed_line);
            }
        }else{
            (*cache).cache_line[i].valid = 1;
            (*cache).cache_line[i].tag = getTag(address);
            miss ++;
            hit ++;
            changeUsedOrder((*cache).used_order, i);
            if(verbose) printf("%s miss hit\n", parsed_line);
            break;
        }
    }
}
