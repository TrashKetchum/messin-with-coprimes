#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <inttypes.h>
#include "pcg_basic.h"

pthread_mutex_t tuple_mutex = PTHREAD_MUTEX_INITIALIZER; //mutex lock in the incredibly unlikely event of a simultaneous read/write to the same index
volatile char stop = 0, go = 0;     //stop kills loop in prngmachine, go stalls loop in improbabilityDrive


typedef uint64_t ull;
typedef uint32_t ulong;
typedef uint16_t uint;
typedef uint8_t uchar;  

typedef struct{     //two ulongs for the price of both
    ulong a;
    ulong b;
}tuple; 

typedef struct{
    tuple *tupleArr;
    ulong size;
    ulong interval;
    uchar nearby;
}prng_args;

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
    if(!((u | v)) & 1) return 0;        //checks if both numbers are even
    return 1 == gcd(u, v);
}

static inline ulong sat_sub(ulong a, ulong b){   //saturated subtraction
    ulong res = a-b;
    res &= -(res<=a);
    return res;
}

void* prngmachine(void* arg){
    printf("prng thread booted\n");
    prng_args *args = (prng_args*) arg;
    tuple *tupleArr = args->tupleArr;
    ulong size = args->size;
    ulong interval = args->interval;
    uchar nearby = args->nearby;

    pcg32_random_t rnga, rngb;
    pcg32_srandom_r(&rnga, time(NULL), (intptr_t)&rnga);
    pcg32_srandom_r(&rngb, time(NULL), (intptr_t)&rngb);

    ulong index=0;
    ulong a,b;

    do{
        a=pcg32_boundedrand_r(&rnga, interval);
        b=pcg32_boundedrand_r(&rngb, interval);
        //I decided i wanted to do the calculation of |a-b| branchless, 
        //so im doing saturated subtrataction twice an ORing outputs
        //fuck knows if its better in anyway, but i like it
        ulong diff = sat_sub(a,b)|sat_sub(b,a);
        if(diff==nearby){
            pthread_mutex_lock(&tuple_mutex);
            tupleArr[index] = (tuple){a,b};
            index = (index+1)%size;
            pthread_mutex_unlock(&tuple_mutex);
            go=1;
        }
    }while(!stop);
    printf("loop finished\n");
    return NULL;
}

void improbabilityDrive(tuple* tupleArr, ulong interval, uint sampleRate, uchar nearby, ulong size){
    //checking the probability that two integers u and v are coprime, where u-v is some difference d.
    //doing this in chunks, and increasing order to see if it changes over time (it does not)
    FILE *fp;
    fp = fopen("probs.csv", "w+");
    if(fp == NULL){
        printf("Error opening csv.\n");
        return;
    }
    fprintf(fp, "Interval,Ratio,Positives,Sample Rate\n");

    ull u =0,v = 0;
    ull i = interval;
    ull max = 1000000000;
    uint arri = 0;
    double ratio;

    printf("i: %lu\n", i);
    printf("SampleRate:%u\n", sampleRate);

    do{  
        uint total=0, positives = 0;
        do{
            if(go){
                ull min = i-interval;

                pthread_mutex_lock(&tuple_mutex);
                u = min+tupleArr[arri].a;
                v = min+tupleArr[arri].b;

                if(u==min&&v==min){
                    go=0;
                }
                else{
                    arri = (arri+1)%size;
                    positives += coprimality(u,v);
                    total++;
                }
                pthread_mutex_unlock(&tuple_mutex);
            }
        }while(total<sampleRate);

        ratio=(double)positives / (double)total;
        fprintf(fp, "%llu,%lf,%u,%u\n", i, ratio, positives, total);
        i+=interval;
    }while(i<max);

    stop=1;
    fclose(fp);
}

int main(void){
    ulong interval;
    uint sampleRate;
    uchar nearby;
    scanf("%" SCNu32, &interval);
    scanf("%" SCNu16, &sampleRate);
    scanf("%" SCNu8, &nearby);

    printf("input scanned\n");

    ulong size = 17179869184/sizeof(tuple);
    tuple* tupleArr = malloc(size*sizeof(tuple));           //lets the array be roughly 2gb
    printf("array created\n");
    prng_args args;
    args.tupleArr = tupleArr;
    args.size = size;
    args.interval = interval;
    args.nearby = nearby;

    pthread_t rngthread;

    pthread_create(&rngthread, NULL, prngmachine, &args);

    printf("interval: %lu\n", interval);

    improbabilityDrive(tupleArr, interval, sampleRate, nearby, size);
 
    pthread_join(rngthread, NULL);
    
    free(tupleArr);

    return 0;
}