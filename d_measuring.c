#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <inttypes.h>
#include "pcg_basic.h"

typedef uint64_t ull;
typedef uint32_t ulong;
typedef uint16_t uint;
typedef uint8_t uchar;

#define NUM_THREADS 16
#define QUEUE_SIZE 64

typedef struct {
    uint diff;
    double result;
    int done;           
}Job;

typedef struct {
    Job* queue[QUEUE_SIZE]; 
    int head;               
    int tail;              
    int count;             
    int shutdown;           
    pthread_mutex_t lock;
    pthread_cond_t not_empty;      
    pthread_cond_t not_full;       
}threadPool;

static threadPool party;

void pool_init(void){
    party.head = 0;
    party.tail = 0;
    party.count = 0;
    party.shutdown = 0;
    pthread_mutex_init(&party.lock, NULL);
    pthread_cond_init(&party.not_empty, NULL);
    pthread_cond_init(&party.not_full, NULL);
}

void pool_submit(Job* job){
    pthread_mutex_lock(&party.lock);
    
    while(party.count == QUEUE_SIZE){
        pthread_cond_wait(&party.not_full, &party.lock);
    }

    party.queue[party.tail] = job;
    party.tail = (party.tail + 1) & (QUEUE_SIZE - 1);
    party.count++;
    pthread_cond_signal(&party.not_empty);
    pthread_mutex_unlock(&party.lock);
}

void pool_shutdown(void){
    pthread_mutex_lock(&party.lock);
    party.shutdown = 1;
    pthread_cond_broadcast(&party.not_empty); 
    pthread_mutex_unlock(&party.lock);
}

//Binary GCD nicked, then modified from https://math.stackexchange.com/questions/445351/
static inline ull gcd(ull u, ull v){
    ull shift = __builtin_ctzll(u | v);
    u >>= __builtin_ctzll(u);
    
    do{
        v >>= __builtin_ctzll(v);
        if(u > v){
            ull t = v;
            v = u;
            u = t;
        }
     }while((v -= u));
    
    return u << shift;
}

static inline char coprimality(ull u, ull v){
    if(!((u | v) & 1)) return 0;        //checks if both numbers are even
    return 1 == gcd(u, v);
}

long double improbabilityDrive(uint diff, pcg32_random_t* prng){
    uint ind = 0;
    uint pos = 0;
    ulong bound = UINT32_MAX-diff;
    uint max = UINT16_MAX-4;

    do{
        ulong a = pcg32_boundedrand_r(prng, bound);
        if(coprimality(a,a+diff)) {
            pos++;
        }
        ind++;
    }while(ind<max);

    double rat= (double)pos/(double)ind;
    return rat;
}

void* jobber(void* arg){
    pcg32_random_t prng;
    pcg32_srandom_r(&prng, time(NULL)^(ull)(intptr_t)arg, (ull)pthread_self());

    while(1){
        pthread_mutex_lock(&party.lock);

        while(party.count == 0 && !party.shutdown) pthread_cond_wait(&party.not_empty, &party.lock);

        if(party.shutdown && party.count == 0){
            pthread_mutex_unlock(&party.lock);
            return NULL;
        }

        Job* job = party.queue[party.head];
        party.head = (party.head + 1) & (QUEUE_SIZE - 1);
        party.count--;
        
        pthread_cond_signal(&party.not_full);  
        pthread_mutex_unlock(&party.lock);

        job->result = improbabilityDrive(job->diff, &prng);

        __atomic_store_n(&job->done, 1, __ATOMIC_RELEASE);
    }
}

int main(void){
    FILE *fp;
    fp = fopen("probs.csv", "w+");
    if(fp == NULL){
        printf("Error opening csv.\n");
        return 0;
    }

    pool_init();

    pthread_t threads[NUM_THREADS];
    for(int t = 0; t < NUM_THREADS; t++)
        pthread_create(&threads[t], NULL, jobber, (void*)(intptr_t)t);

    uint max = UINT16_MAX / 2;

    Job* jobs = calloc(max, sizeof(Job));
    if(!jobs){ printf("OOM\n"); return 1; }

    for(uint i = 1; i < max; i++){
        jobs[i].diff = i;
        jobs[i].done = 0;
        pool_submit(&jobs[i]);
    }

    for(uint i = 1; i < max; i++){
        while(!__atomic_load_n(&jobs[i].done, __ATOMIC_ACQUIRE));
        fprintf(fp,  "%u,%lf\n", jobs[i].diff, jobs[i].result);
        //printf("i: %u, prob: %Lf\n", jobs[i].diff, jobs[i].result);
    }

    pool_shutdown();
    for(int t = 0; t < NUM_THREADS; t++)
        pthread_join(threads[t], NULL);

    free(jobs);
    fclose(fp);
    return 0;
}