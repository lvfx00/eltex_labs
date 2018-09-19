#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>

#define LOADER_INCREASE_MIN 300
#define LOADER_INCREASE_MAX 500
#define LOADER_SLEEP_PERIOD 1 // in seconds

#define CUSTOMER_NUM 3
#define CUSTOMER_NEED_MIN 3000
#define CUSTOMER_NEED_MAX 3500
#define CUSTOMER_SLEEP_PERIOD 3 // in seconds

#define MARKETS_NUM 5
#define MARKETS_INIT_MIN 1000
#define MARKETS_INIT_MAX 1200

typedef struct loader_func_args {
    int *markets;
    pthread_mutex_t *mutexes;
} loader_func_args;

typedef struct customer_func_args {
    int customer_num;
    int *markets;
    pthread_mutex_t *mutexes;
} customer_func_args;

void *loader_func(void *arg) {
    loader_func_args *args = (loader_func_args *) arg;

    while (1) {
        // randomly choose one of markets
        int market_num = rand() % MARKETS_NUM;
        // take lock
        pthread_mutex_lock(args->mutexes + market_num);
        // load products
        int inc_value = LOADER_INCREASE_MIN + (rand() % (LOADER_INCREASE_MAX - LOADER_INCREASE_MIN + 1));
        *(args->markets + market_num) += inc_value;
        printf("Loader added %d to market[%d]\n", inc_value, market_num);
        // release lock
        pthread_mutex_unlock(args->mutexes + market_num);
        // sleep 1 second
        // also used as cancellation point
        nanosleep((const struct timespec[]) {{LOADER_SLEEP_PERIOD, 0}}, NULL);
    }
}

void *customer_func(void *arg) {
    customer_func_args *args = (customer_func_args *) arg;

    // init need value
    int customer_need = CUSTOMER_NEED_MIN + (rand() % (CUSTOMER_NEED_MAX - CUSTOMER_NEED_MIN + 1));

    while (customer_need > 0) {
        // randomly choose one of markets
        int market_num = rand() % MARKETS_NUM;
        // take lock
        pthread_mutex_lock(args->mutexes + market_num);
        // take products
        if (customer_need < args->markets[market_num]) {
            args->markets[market_num] -= customer_need;
            printf("Customer[%d] took %d from market[%d]\n", args->customer_num, customer_need, market_num);
            customer_need = 0;
        } else {
            customer_need -= args->markets[market_num];
            printf("Customer[%d] took %d from market[%d]\n", args->customer_num, args->markets[market_num], market_num);
            args->markets[market_num] = 0;
        }
        // release lock
        pthread_mutex_unlock(args->mutexes + market_num);
        // sleep
        nanosleep((const struct timespec[]) {{CUSTOMER_SLEEP_PERIOD, 0}}, NULL);
    }

    printf("Customer[%d] satisfied his needs\n", args->customer_num);
    pthread_exit(NULL);
}

int main() {
    // init markets
    int markets[MARKETS_NUM];
    srand((unsigned int) time(0)); // Use current time as seed for random generator
    for (int i = 0; i < MARKETS_NUM; ++i) {
        markets[i] = MARKETS_INIT_MIN + (rand() % (MARKETS_INIT_MAX - MARKETS_INIT_MIN + 1));
    }

    // init mutexes
    pthread_mutex_t mutexes[5];
    for (int i = 0; i < MARKETS_NUM; ++i) {
        pthread_mutex_init(&mutexes[i], NULL);
    }

    // create loader thread
    loader_func_args loader_args = {markets, mutexes};
    pthread_t loader_thread;
    if (pthread_create(&loader_thread, NULL, loader_func, (void *) &loader_args)) {
        perror("pthread_create");
        exit(EXIT_FAILURE);
    }

    // create customer threads
    pthread_t customer_threads[CUSTOMER_NUM];
    customer_func_args customer_args[CUSTOMER_NUM];
    for (int i = 0; i < CUSTOMER_NUM; ++i) {
        customer_args[i] = (customer_func_args){i, markets, mutexes};
        if (pthread_create(&customer_threads[i], NULL, customer_func, (void *) &customer_args[i])) {
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }
    }

    // wait until customers satisfy their needs
    for (int i = 0; i < CUSTOMER_NUM; ++i) {
        if (pthread_join(customer_threads[i], NULL)) {
            perror("pthread_join");
            exit(EXIT_FAILURE);
        }
    }

    // stop loader thread
    if (pthread_cancel(loader_thread)) {
        perror("pthread_cancel");
        exit(EXIT_FAILURE);
    }

    return 0;
}