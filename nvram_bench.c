////////////////////////////////////////////////////////////////////////
//
// Copyright 2014 PMC-Sierra, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you
// may not use this file except in compliance with the License. You may
// obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0 Unless required by
// applicable law or agreed to in writing, software distributed under the
// License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for
// the specific language governing permissions and limitations under the
// License.
//
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
//
//   Date: Oct 23, 2014
//
//   Description:
//     Simple tool to perform various benchmarks on mmapp-able
//     devices/files.
//
////////////////////////////////////////////////////////////////////////

/*
 *
 * Direct Memory Interface test utility
 *
 * Included test cases:
 *         Random-Read
 *         Random-Write
 *         Sequential-Read
 *         Sequential-Write
 */

#include <argconfig/argconfig.h>

#include <errno.h>
#include <fcntl.h>
#include <emmintrin.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

const char program_desc[] =
    "data dump and write using mmap calls ";

struct config {
    int seq_test, random_test, latency_test, mfence;
    long dmi_size, offset;
    long packet_size;
    int iterations;
    int data_integ, force;
    int num_threads;

    long block_size;
    unsigned long *rand_map;
};

static const struct config defaults = {
    .iterations = 100,
    .packet_size = 32,
    .block_size = 0x10000,
    .num_threads = 10,
    .force = 0,
};

static const struct argconfig_commandline_options command_line_options[] = {
    {"b",             "NUM", CFG_LONG_SUFFIX, &defaults.block_size, required_argument, NULL},
    {"block",         "NUM", CFG_LONG_SUFFIX, &defaults.block_size, required_argument,
            "block size per iteration"},
    {"d",             "NUM", CFG_LONG_SUFFIX, &defaults.dmi_size, required_argument, NULL},
    {"dmi",           "NUM", CFG_LONG_SUFFIX, &defaults.dmi_size, required_argument,
            "optional DMI size"},
    {"i",             "", CFG_NONE, &defaults.data_integ, no_argument, NULL},
    {"di",            "", CFG_NONE, &defaults.data_integ, no_argument,
            "check data integrity"},
    {"f",             "", CFG_NONE, &defaults.force, no_argument,
            "run even if test is less than a second"},
    {"n",             "NUM",  CFG_POSITIVE, &defaults.iterations, required_argument, NULL},
    {"lo",            "NUM",  CFG_POSITIVE, &defaults.iterations, required_argument,
            "number of iterations to run"},
    {"o",             "NUM", CFG_LONG_SUFFIX, &defaults.offset, required_argument, NULL},
    {"offset",        "NUM", CFG_LONG_SUFFIX, &defaults.offset, required_argument,
            "offset within the device to map"},
    {"p",             "NUM", CFG_LONG_SUFFIX, &defaults.packet_size, required_argument, NULL},
    {"packet",        "NUM", CFG_LONG_SUFFIX, &defaults.packet_size, required_argument,
            "packet size"},
    {"t",             "NUM",  CFG_POSITIVE, &defaults.num_threads, required_argument, NULL},
    {"threads",       "NUM",  CFG_POSITIVE, &defaults.num_threads, required_argument,
            "number of threads to run"},
    {"m",               "", CFG_NONE, &defaults.mfence, no_argument, NULL},
    {"mfence",      "", CFG_NONE, &defaults.mfence, no_argument,
            "use mfence asm directives (impact perfrormance in odd ways"},

    {"==Tests to Run:", "", CFG_NONE, NULL, required_argument, NULL},
    {"s",               "", CFG_NONE, &defaults.seq_test, no_argument, NULL},
    {"sequential",      "", CFG_NONE, &defaults.seq_test, no_argument,
            "run sequential test"},
    {"r",               "", CFG_NONE, &defaults.random_test, no_argument, NULL},
    {"random",          "", CFG_NONE, &defaults.random_test, no_argument,
            "run random test"},
    {"l",               "", CFG_NONE, &defaults.latency_test, no_argument, NULL},
    {"latency",         "", CFG_NONE, &defaults.latency_test, no_argument,
            "run latency test"},
    {0}
};

struct thrd_info
{
	void *mem;
	int thr_i;
	unsigned int time_stamp_1;
	unsigned int time_stamp_2;
    struct config *cfg;
    int mfence;
};

static inline void get_cpu_clock(struct timeval *tp)
{
    struct timezone tzp;

    gettimeofday(tp,&tzp);
}

static void thread_stats_collect(struct timeval tp, struct timeval tp1,
                                 struct thrd_info *thread_info)
{
	int td1, td2;

	if (tp1.tv_usec > tp.tv_usec)
    {
        td1 = (int)(tp1.tv_sec - tp.tv_sec);
        td2 = (int)(tp1.tv_usec - tp.tv_usec);
    }
    else
    {
        td1 = (int)(tp1.tv_sec - tp.tv_sec - 1);
        td2 = (int)((1000000 - tp.tv_usec)+ tp1.tv_usec);
    }

	thread_info->time_stamp_1 = td1;
	thread_info->time_stamp_2 = td2;
}

static void thread_stats_print(const char * msg, struct thrd_info *threads_info)
{
	unsigned int secs = 0;
	unsigned int usecs = 0;
	uint64_t data_mb;
	float mbs;
	float iops;
    float ttime;
    struct config *cfg = threads_info[0].cfg;

	for (int i = 0; i < cfg->num_threads; i++) {
		secs += threads_info[i].time_stamp_1;
		usecs += threads_info[i].time_stamp_2;
	}

	secs += (usecs / 1000000);
	usecs -= ((usecs / 1000000) * 1000000);

	secs /= cfg->num_threads; // average sec

    ttime = (float)secs + (float) ((float)usecs / (float)1000000);

	if (!cfg->force && secs == 0) {
		printf ("\n[ERROR] Test too short\n");
		exit (-1);
	}

	data_mb = (cfg->packet_size * cfg->iterations * cfg->block_size *
               cfg->num_threads);

	data_mb /= 1024 * 1024;
	mbs = data_mb / ttime;

	iops = (cfg->iterations * cfg->block_size *
            cfg->num_threads / 1000); // K-IOP

    iops = iops / ttime;

	printf ("Test: %s\n", msg);
	printf ("\tPacket Size: %lu\n", cfg->packet_size);
	printf ("\tTransmission time: %d.%06ds\n", secs, usecs);
	printf ("\tTotal Transmitted: %lluMiB\n", (long long) data_mb);
	printf ("\tPerformance: %.0fMiB/s - %.0fKIOPS \n", mbs, iops);
}

static void print_time(const char *name, const char *message)
{
    time_t timer;
    char buffer[25];
    struct tm* tm_info;

    time(&timer);
    tm_info = localtime(&timer);

    strftime(buffer, 25, "%H:%M:%S", tm_info);

    //printf("%s %s time is %s\n", name, message, buffer);
}

/*
 * Test functions below
 */

/*
 * LATENCY MEASUREMENTS
 */
static double measure_latency_rw(void *mem, unsigned long tr_size, int f_write,
                                 double * lat_min, double *lat_max, struct config *cfg)
{
    struct timeval tp,tp1,tp2;
    typedef struct { char a[tr_size]; } AR;
    AR src;
    AR * dst;
    int i;
    double times = 300.0;
    double delta;

    *lat_min = 0;
    *lat_max = 0;
    tp2.tv_usec = 0;
    tp2.tv_sec = 0;
    dst = (AR*)mem ;

    for (i = 0; i < (times + 2); i++) // 2 iterations are to exclude min and max
    {
        tp.tv_usec = tp1.tv_usec = 0;
        get_cpu_clock(&tp);

        if (f_write)
            dst[i] = src;
        else
            src = dst[i];

        if ( cfg->mfence )
            asm volatile("mfence" ::: "memory");

        get_cpu_clock(&tp1);

        if (tp1.tv_sec >= tp.tv_sec)
            delta = tp1.tv_usec - tp.tv_usec;
        else
            delta = ( 1000000 - tp.tv_usec)+ tp1.tv_usec;

        tp2.tv_usec += delta;

        if (!*lat_min || (delta < *lat_min))
            *lat_min = delta;
        if (!*lat_max || (delta > *lat_max))
            *lat_max = delta;
    }

    tp2.tv_usec -= *lat_min;
    tp2.tv_usec -= *lat_max;

    return (tp2.tv_usec / times);
}

static void measure_latency(void *mem, unsigned long tr_size, struct config *cfg)
{
    double lat = 0;
    double lat_min;
    double lat_max;

    lat = measure_latency_rw (mem, tr_size, 1, &lat_min, &lat_max, cfg); //write
    printf ("average write latency is: %06f uSec for sizeof %lu \n", lat, tr_size);

    lat = measure_latency_rw (mem, tr_size, 0, &lat_min, &lat_max, cfg); //read
    printf ("average read latency is: %06f uSec for sizeof %lu \n",  lat, tr_size);
}

/*
 * RANDOM WRITE
 */
static void rand_write(struct thrd_info * thr_data)
{
    struct timeval tp,tp1;
    struct config *cfg = thr_data->cfg;
    typedef struct { char a[cfg->packet_size]; } AR;
    AR src;
    AR dint;
    AR *dst;
    AR *mem = thr_data->mem;

    get_cpu_clock(&tp);

    for (int j = 0; j < cfg->iterations; j++)
    {
        for (int i = 0; i < cfg->block_size; i++)
        {
            dst = mem + cfg->rand_map[i];
            if ( thr_data->cfg->mfence)
                asm volatile("mfence" ::: "memory");

            *dst = src;
            if (cfg->data_integ)
            {
                dint = *dst;
                if (memcmp(&src, &dint, sizeof (AR)))
                    printf ("[ERROR] Data integrity at %p \n",  dst);
            }
        }
    }

    get_cpu_clock(&tp1);

    thread_stats_collect(tp, tp1, thr_data);
}

/*
 * RANDOM READ
 */
static void rand_read(struct thrd_info * thr_data)
{
    struct timeval tp,tp1;
    struct config *cfg = thr_data->cfg;
    typedef struct { char a[cfg->packet_size]; } AR;
    AR dst;
    AR *src;
    AR *mem = thr_data->mem;

    get_cpu_clock(&tp);

    for (int j = 0; j < cfg->iterations; j++)
    {
        for (int i = 0; i < cfg->block_size; i++)
        {
            src = mem + cfg->rand_map[i];
            if (thr_data->cfg->mfence)
                asm volatile("mfence" ::: "memory");

            dst = *src;
        }
    }

    get_cpu_clock(&tp1);

    thread_stats_collect(tp, tp1, thr_data);
    (void) dst; //suppress set but not used warning
}

/*
 * SEQUENTIAL WRITE
 */
static void seq_write(struct thrd_info * thr_data)
{
    struct timeval tp,tp1;
    struct config *cfg = thr_data->cfg;
    typedef struct { char a[cfg->packet_size]; } AR;
    AR src;
    AR dint;

    get_cpu_clock(&tp);

    for (int j = 0; j < cfg->iterations; j++)
    {
        AR *dst = thr_data->mem;

        for (int i = 0; i < cfg->block_size; i++)
        {
            *dst = src;
            if (cfg->data_integ)
            {
                dint = *dst;
                if (memcmp(&src, &dint, sizeof (AR)))
                    printf ("[ERROR] Data integrity at %p \n",  dst);
            }

            dst++;
            if ( thr_data->cfg->mfence )
                asm volatile("mfence" ::: "memory");
        }
    }

    get_cpu_clock(&tp1);

    thread_stats_collect(tp, tp1, thr_data);
}

/*
 * SEQUENTIAL READ
 */
static void seq_read(struct thrd_info * thr_data)
{
    struct timeval tp,tp1;
    struct config *cfg = thr_data->cfg;
    typedef struct { char a[cfg->packet_size]; } AR;
    AR dst;

    get_cpu_clock(&tp);

    for (int j = 0; j < cfg->iterations; j++)
    {
        AR *src = thr_data->mem;

        for (int i = 0; i < cfg->block_size; i++)
        {
            dst = *src;
            src++;
            if ( thr_data->cfg->mfence)
                asm volatile("mfence" ::: "memory");
        }
    }

    get_cpu_clock(&tp1);

    thread_stats_collect(tp, tp1, thr_data);
    (void) dst; //suppress set but not used warning
}

static uint64_t get_fd_size(int fd)
{
    struct stat stat;
    fstat(fd, &stat);

    if (stat.st_size)
        return stat.st_size;

    uint64_t ret = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);

    return ret;
}

static void *load_mmap(int fd, struct config *cfg)
{
    void *mem = mmap(NULL, cfg->dmi_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                     cfg->offset);

    if (mem == MAP_FAILED) {
        // With the nvidia device, if something is already pinned we cannot use the
        // whole device size. So, try again with a smaller space.
        cfg->dmi_size -= 8*1024*1024;
        mem = mmap(NULL, cfg->dmi_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                   cfg->offset);
    }

    if (mem == MAP_FAILED) {
        perror("[ERROR] mmap failed");
        exit(errno);
    }

    return mem;
}

static void generate_rand_map(struct config *cfg)
{
    cfg->rand_map = malloc(cfg->block_size * sizeof(*cfg->rand_map));

    for(int i = 0; i < cfg->block_size; i++) {
        unsigned r;
        while (1) {
            r = rand() % cfg->block_size;

            if (r < (cfg->dmi_size - cfg->packet_size))
                break;
        }

        cfg->rand_map[i] = r;
    }
}

static void start_threaded_test(const char *name,
                                void (*start_routine)(struct thrd_info *),
                                void *mem, struct config *cfg)
{
    struct thrd_info threads_info[cfg->num_threads];
    pthread_t tid[cfg->num_threads];
    print_time(name, "started");
    memset(threads_info, 0, sizeof(threads_info));

    for (int i = 0; i < cfg->num_threads; i++) {
        threads_info[i].thr_i = i;
        threads_info[i].cfg = cfg;
        threads_info[i].mem = mem;

        pthread_create(&tid[i], NULL, (void * (*)(void *)) start_routine,
                       &threads_info[i]);
    }

    for (int i = 0; i < cfg->num_threads; i++)
        pthread_join(tid[i], NULL);

    print_time(name, "finished");
    thread_stats_print(name, threads_info);
}

/****************************************************************************
 *
 *
 * MAIN function
 * Parses command line parameters and executes the specified tests
 *
 *
 ****************************************************************************/
int main(int argc, char *argv[])
{
    struct config cfg;

    srand(time(NULL));

    argconfig_append_usage("device");

    int args = argconfig_parse(argc, argv, program_desc, command_line_options,
                               &defaults, &cfg, sizeof(cfg));
    argv[args+1] = NULL;

    if (args != 1)  {
        argconfig_print_help(argv[0], program_desc, command_line_options);
        return -1;
    }

    int fd = open(argv[1], O_RDWR);
    if( fd < 0 ) {
        fprintf(stderr, "[ERROR] Open device file failed '%s': %s\n", argv[1],
                strerror(errno));
        exit(errno);
    }

    if (cfg.dmi_size <= 0) {
        cfg.dmi_size = get_fd_size(fd);
        if (cfg.dmi_size == 0 || cfg.dmi_size > (16ULL << 30)) {
            printf("[ERROR] Unable to detect size of device, please specify "
                   "with -dmi option.\n");
            exit(-1);
        }
        cfg.dmi_size -= cfg.offset;
    }

    void *mem = load_mmap(fd, &cfg);

    if (cfg.block_size * cfg.packet_size > cfg.dmi_size) {
        cfg.block_size = cfg.dmi_size / cfg.packet_size;
        printf("Warning: block and packet size settings exceed the file size. "
               "Setting the block size to %ld\n", cfg.block_size);
    }

    generate_rand_map(&cfg);

    if (cfg.seq_test) {
        start_threaded_test("Seq Write", seq_write, mem, &cfg);
        sleep(3);
        start_threaded_test("Seq Read", seq_read, mem, &cfg);

        if (cfg.random_test)
            sleep(3);
    }

    if (cfg.random_test) {
        start_threaded_test("Rand Write", rand_write, mem, &cfg);
        sleep(3);
        start_threaded_test("Rand Read", rand_read, mem, &cfg);
    }

    if (cfg.latency_test) {
        measure_latency(mem,  24, &cfg);
        measure_latency(mem,  64, &cfg);
        measure_latency(mem, 128, &cfg);
        measure_latency(mem, 256, &cfg);
        measure_latency(mem, 512, &cfg);
        measure_latency(mem, 1024, &cfg);
        measure_latency(mem, 2048, &cfg);
        measure_latency(mem, 4096, &cfg);
        measure_latency(mem, 8192, &cfg);
    }

    printf("Done!\n");

    free(cfg.rand_map);

    return 0;
}
