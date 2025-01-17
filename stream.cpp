/*-----------------------------------------------------------------------*/
/* Program: STREAM                                                       */
/* Revision: $Id: stream.c,v 5.10 2013/01/17 16:01:06 mccalpin Exp mccalpin $ */
/* Original code developed by John D. McCalpin                           */
/* Programmers: John D. McCalpin                                         */
/*              Joe R. Zagar                                             */
/*                                                                       */
/* This program measures memory transfer rates in MB/s for simple        */
/* computational kernels coded in C.                                     */
/*-----------------------------------------------------------------------*/
/* Copyright 1991-2013: John D. McCalpin                                 */
/*-----------------------------------------------------------------------*/
/* License:                                                              */
/*  1. You are free to use this program and/or to redistribute           */
/*     this program.                                                     */
/*  2. You are free to modify this program for your own use,             */
/*     including commercial use, subject to the publication              */
/*     restrictions in item 3.                                           */
/*  3. You are free to publish results obtained from running this        */
/*     program, or from works that you derive from this program,         */
/*     with the following limitations:                                   */
/*     3a. In order to be referred to as "STREAM benchmark results",     */
/*         published results must be in conformance to the STREAM        */
/*         Run Rules, (briefly reviewed below) published at              */
/*         http://www.cs.virginia.edu/stream/ref.html                    */
/*         and incorporated herein by reference.                         */
/*         As the copyright holder, John McCalpin retains the            */
/*         right to determine conformity with the Run Rules.             */
/*     3b. Results based on modified source code or on runs not in       */
/*         accordance with the STREAM Run Rules must be clearly          */
/*         labelled whenever they are published.  Examples of            */
/*         proper labelling include:                                     */
/*           "tuned STREAM benchmark results"                            */
/*           "based on a variant of the STREAM benchmark code"           */
/*         Other comparable, clear, and reasonable labelling is          */
/*         acceptable.                                                   */
/*     3c. Submission of results to the STREAM benchmark web site        */
/*         is encouraged, but not required.                              */
/*  4. Use of this program or creation of derived works based on this    */
/*     program constitutes acceptance of these licensing restrictions.   */
/*  5. Absolutely no warranty is expressed or implied.                   */
/*-----------------------------------------------------------------------*/
# include <stdio.h>
# include <unistd.h>
# include <math.h>
# include <float.h>
# include <limits.h>
# include <stdint.h>
# include <stdlib.h>
# include <sys/time.h>

#ifdef GEM5_RV64
#include "gem5/m5ops.h"
#elif (__amd64__) && (USE_PCM)
#include "roi_hooks.h"
#include "cpu_uarch.h"
#include "errordefs.h"
#endif

/*-----------------------------------------------------------------------
 * INSTRUCTIONS:
 *
 *	1) STREAM requires different amounts of memory to run on different
 *           systems, depending on both the system cache size(s) and the
 *           granularity of the system timer.
 *     You should adjust the value of 'STREAM_ARRAY_SIZE' (below)
 *           to meet *both* of the following criteria:
 *       (a) Each array must be at least 4 times the size of the
 *           available cache memory. I don't worry about the difference
 *           between 10^6 and 2^20, so in practice the minimum array size
 *           is about 3.8 times the cache size.
 *           Example 1: One Xeon E3 with 8 MB L3 cache
 *               STREAM_ARRAY_SIZE should be >= 4 million, giving
 *               an array size of 30.5 MB and a total memory requirement
 *               of 91.5 MB.  
 *           Example 2: Two Xeon E5's with 20 MB L3 cache each (using OpenMP)
 *               STREAM_ARRAY_SIZE should be >= 20 million, giving
 *               an array size of 153 MB and a total memory requirement
 *               of 458 MB.  
 *       (b) The size should be large enough so that the 'timing calibration'
 *           output by the program is at least 20 clock-ticks.  
 *           Example: most versions of Windows have a 10 millisecond timer
 *               granularity.  20 "ticks" at 10 ms/tic is 200 milliseconds.
 *               If the chip is capable of 10 GB/s, it moves 2 GB in 200 msec.
 *               This means the each array must be at least 1 GB, or 128M elements.
 *
 *      Version 5.10 increases the default array size from 2 million
 *          elements to 10 million elements in response to the increasing
 *          size of L3 caches.  The new default size is large enough for caches
 *          up to 20 MB. 
 *      Version 5.10 changes the loop index variables from "register int"
 *          to "ssize_t", which allows array indices >2^32 (4 billion)
 *          on properly configured 64-bit systems.  Additional compiler options
 *          (such as "-mcmodel=medium") may be required for large memory runs.
 *
 *      Array size can be set at compile time without modifying the source
 *          code for the (many) compilers that support preprocessor definitions
 *          on the compile line.  E.g.,
 *                gcc -O -DSTREAM_ARRAY_SIZE=100000000 stream.c -o stream.100M
 *          will override the default size of 10M with a new size of 100M elements
 *          per array.
 */
#ifndef STREAM_ARRAY_SIZE
#   define STREAM_ARRAY_SIZE	10000000
#endif

/*  2) STREAM runs each kernel "NTIMES" times and reports the *best* result
 *         for any iteration after the first, therefore the minimum value
 *         for NTIMES is 2.
 *      There are no rules on maximum allowable values for NTIMES, but
 *         values larger than the default are unlikely to noticeably
 *         increase the reported performance.
 *      NTIMES can also be set on the compile line without changing the source
 *         code using, for example, "-DNTIMES=7".
 */
#ifdef NTIMES
#if NTIMES<=1
#   define NTIMES	10
#endif
#endif
#ifndef NTIMES
#   define NTIMES	10
#endif

/*  Users are allowed to modify the "OFFSET" variable, which *may* change the
 *         relative alignment of the arrays (though compilers may change the 
 *         effective offset by making the arrays non-contiguous on some systems). 
 *      Use of non-zero values for OFFSET can be especially helpful if the
 *         STREAM_ARRAY_SIZE is set to a value close to a large power of 2.
 *      OFFSET can also be set on the compile line without changing the source
 *         code using, for example, "-DOFFSET=56".
 */
#ifndef OFFSET
#   define OFFSET	0
#endif

/*
 *	3) Compile the code with optimization.  Many compilers generate
 *       unreasonably bad code before the optimizer tightens things up.  
 *     If the results are unreasonably good, on the other hand, the
 *       optimizer might be too smart for me!
 *
 *     For a simple single-core version, try compiling with:
 *            cc -O stream.c -o stream
 *     This is known to work on many, many systems....
 *
 *     To use multiple cores, you need to tell the compiler to obey the OpenMP
 *       directives in the code.  This varies by compiler, but a common example is
 *            gcc -O -fopenmp stream.c -o stream_omp
 *       The environment variable OMP_NUM_THREADS allows runtime control of the 
 *         number of threads/cores used when the resulting "stream_omp" program
 *         is executed.
 *
 *     To run with single-precision variables and arithmetic, simply add
 *         -DSTREAM_TYPE=float
 *     to the compile line.
 *     Note that this changes the minimum array sizes required --- see (1) above.
 *
 *     The preprocessor directive "TUNED" does not do much -- it simply causes the 
 *       code to call separate functions to execute each kernel.  Trivial versions
 *       of these functions are provided, but they are *not* tuned -- they just 
 *       provide predefined interfaces to be replaced with tuned code.
 *
 *
 *	4) Optional: Mail the results to mccalpin@cs.virginia.edu
 *	   Be sure to include info that will help me understand:
 *		a) the computer hardware configuration (e.g., processor model, memory type)
 *		b) the compiler name/version and compilation flags
 *      c) any run-time information (such as OMP_NUM_THREADS)
 *		d) all of the output from the test case.
 *
 * Thanks!
 *
 *-----------------------------------------------------------------------*/

# define HLINE "-------------------------------------------------------------\n"

# ifndef MIN
# define MIN(x,y) ((x)<(y)?(x):(y))
# endif
# ifndef MAX
# define MAX(x,y) ((x)>(y)?(x):(y))
# endif

#ifndef STREAM_TYPE
#define STREAM_TYPE double
#endif

void checkSTREAMresults(STREAM_TYPE *a, \
                        STREAM_TYPE *b, \
						STREAM_TYPE *c, \
						unsigned num_element);

/* Initial arrays */
void initializeArrays(STREAM_TYPE *arr_ptr, uint32_t num_elements) {
	for (uint32_t i = 0; i < num_elements; i++) {
		arr_ptr[i] = ((STREAM_TYPE)rand()/RAND_MAX)*2.0-1.0;
	}
}

class ROICounter {
	private	:
		int32_t lproc_id;
		uint64_t tsc;
		uint64_t instret;
		uint64_t cpu_cycles;
		uint64_t l1d_miss;
		uint64_t l1d_hits;
		uint64_t l2_miss;
		uint64_t l2_hits;
		uint64_t l3_miss;
		uint64_t l3_hits;
		#if (__amd64__) && (USE_PCM)
		core_counter_state_ptr_t counter_state;
		#endif
	public :
		ROICounter(int32_t lproc_id) :
			#if (__amd64__) && (USE_PCM)
			counter_state(NULL),
			#endif
			lproc_id(lproc_id),
			tsc(0),
			instret(0),
			cpu_cycles(0),
			l1d_hits(0),
			l1d_miss(0),
			l2_hits(0),
			l2_miss(0),
			l3_hits(0),
			l3_miss(0) {}
			
		void mark_roi();
		void start_roi();
		void stop_roi();
		ROICounter & operator - (const ROICounter & o);
};

ROICounter & ROICounter::operator - (const ROICounter & o) {
	#if (__amd64__) && (USE_PCM)
	struct __eco_roi_stats_struct  tmp = __eco_counter_diff(counter_state, o.counter_state);
	tsc = tmp.tsc;
	instret = tmp.instret;
	cpu_cycles = tmp.cpu_cycles;
	l1d_miss = tmp.l1d_miss;
	l1d_hits = tmp.l1d_hits;
	l2_miss = tmp.l2_miss;
	l2_hits = tmp.l2_hits;
	l3_miss = tmp.l3_miss;
	l3_hits = tmp.l3_hits;
	#else
	tsc = this->tsc - o.tsc;
	instret = 0;
	cpu_cycles = 0;
	l1d_miss = 0;
	l1d_hits = 0;
	l2_miss = 0;
	l2_hits = 0;
	l3_miss = 0;
	l3_hits = 0;
	#endif
}

void ROICounter::mark_roi() {
	#if (__amd64__) && (USE_PCM)
   	counter_state = __eco_roi_begin(lproc_id);
   	#endif
	#ifdef GEM5_RV64
	tsc = -1;
	#else
	tsc = __eco_rdtsc();
	#endif
	instret = -1;
	cpu_cycles = -1;
	l1d_miss = -1;
	l1d_hits = -1;
	l2_miss = -1;
	l2_hits = -1;
	l3_miss = -1;
	l3_hits = -1;
}

void ROICounter::start_roi() {
	#ifdef GEM5_RV64
	m5_reset_stats(0,0);
	#endif
	mark_roi();
}


void ROICounter::stop_roi() {
	#ifdef GEM5_RV64
	 m5_dump_stats(0,0);
	#endif
	mark_roi();
}

int main(int argc, char* argv[]) {
    int			bytesPerWord;
    int			k;
    ssize_t		j;
    STREAM_TYPE		scalar;
    double		t, times[4][NTIMES];

	/* --- SETUP --- */
    fprintf(stderr,HLINE);
    fprintf(stderr,"STREAM version $Revision: 5.10 $\n");
    fprintf(stderr,HLINE);
    bytesPerWord = sizeof(STREAM_TYPE);
    fprintf(stderr,"This system uses %d bytes per array element.\n",
	bytesPerWord);
    fprintf(stderr,HLINE);
	if (argc != 4) {
      fprintf(stderr, "argc=%d\n", argc);
      fprintf(stderr, "");
      return 1;
   	}
	uint32_t num_elements = atoi(argv[1]);

	/* --- Affine CPUs --- */
	int32_t lproc_id = 0; // Logical processor ID for this thread
	#if (__amd64__) && (USE_PCM)
	affinity_set_cpu2(lproc_id);
	__eco_init(lproc_id);
	#endif

#ifdef N
    printf("*****  WARNING: ******\n");
    printf("      It appears that you set the preprocessor variable N when compiling this code.\n");
    printf("      This version of the code uses the preprocesor variable STREAM_ARRAY_SIZE to control the array size\n");
    printf("      Reverting to default value of STREAM_ARRAY_SIZE=%llu\n",(unsigned long long) STREAM_ARRAY_SIZE);
    printf("*****  WARNING: ******\n");
#endif

    fprintf(stderr,"Array size = %llu (elements), Offset = %d (elements)\n" , (unsigned long long) STREAM_ARRAY_SIZE, OFFSET);
    fprintf(stderr,"Memory per array = %.1f MiB (= %.1f GiB).\n", 
	bytesPerWord * ( (double) STREAM_ARRAY_SIZE / 1024.0/1024.0),
	bytesPerWord * ( (double) STREAM_ARRAY_SIZE / 1024.0/1024.0/1024.0));
    fprintf(stderr,"Total memory required = %.1f MiB (= %.1f GiB).\n",
	(3.0 * bytesPerWord) * ( (double) STREAM_ARRAY_SIZE / 1024.0/1024.),
	(3.0 * bytesPerWord) * ( (double) STREAM_ARRAY_SIZE / 1024.0/1024./1024.));
    fprintf(stderr,"Each kernel will be executed %d times.\n", NTIMES);
    fprintf(stderr,"The *best* time for each kernel (excluding the first iteration)\n"); 
    fprintf(stderr,"will be used to compute the reported bandwidth.\n");

    /* Get initial value for system clock. */
	STREAM_TYPE *a   = (STREAM_TYPE *)malloc(num_elements * sizeof(STREAM_TYPE));
	STREAM_TYPE *b   = (STREAM_TYPE *)malloc(num_elements * sizeof(STREAM_TYPE));
	STREAM_TYPE *c   = (STREAM_TYPE *)malloc(num_elements * sizeof(STREAM_TYPE));
	initializeArrays(a, num_elements);
	initializeArrays(b, num_elements);
	initializeArrays(c, num_elements);
    fprintf(stderr, HLINE);
    
    /*	--- MAIN LOOP --- repeat test cases NTIMES times --- */
    ROICounter start(lproc_id); // CRITICAL SECTION : START
	scalar = 3.0;
    for (k=0; k<NTIMES; k++) {
		#pragma omp parallel for
		for (j=0; j<STREAM_ARRAY_SIZE; j++)
		    c[j] = a[j];

		#pragma omp parallel for
		for (j=0; j<STREAM_ARRAY_SIZE; j++)
		    b[j] = scalar*c[j];

		#pragma omp parallel for
		for (j=0; j<STREAM_ARRAY_SIZE; j++)
		    c[j] = a[j]+b[j];

		#pragma omp parallel for
		for (j=0; j<STREAM_ARRAY_SIZE; j++)
		    a[j] = b[j]+scalar*c[j];
	}
	ROICounter stop(lproc_id); // CRITICAL SECTION : STOP
   
	/* --- SUMMARY --- */
	ROICounter diff_count = stop-start;

    /* --- Check Results --- */
    checkSTREAMresults(a,b,c,num_elements);
    printf(HLINE);

    return 0;
}

# define	M	20


#ifndef abs
#define abs(a) ((a) >= 0 ? (a) : -(a))
#endif
void checkSTREAMresults(STREAM_TYPE *a, \
                        STREAM_TYPE *b, \
						STREAM_TYPE *c, \
						unsigned num_elements) {
	STREAM_TYPE aj,bj,cj,scalar;
	STREAM_TYPE aSumErr,bSumErr,cSumErr;
	STREAM_TYPE aAvgErr,bAvgErr,cAvgErr;
	double epsilon;
	ssize_t	j;
	int	k,ierr,err;

    /* reproduce initialization */
	aj = 1.0;
	bj = 2.0;
	cj = 0.0;
    
	/* a[] is modified during timing check */
	aj = 2.0E0 * aj;
    
	/* now execute timing loop */
	scalar = 3.0;
	for (k=0; k<NTIMES; k++) {
        cj = aj;
        bj = scalar*cj;
        cj = aj+bj;
        aj = bj+scalar*cj;
    }

    /* accumulate deltas between observed and expected results */
	aSumErr = 0.0;
	bSumErr = 0.0;
	cSumErr = 0.0;
	for (j=0; j<STREAM_ARRAY_SIZE; j++) {
		aSumErr += abs(a[j] - aj);
		bSumErr += abs(b[j] - bj);
		cSumErr += abs(c[j] - cj);
		// if (j == 417) printf("Index 417: c[j]: %f, cj: %f\n",c[j],cj);	// MCCALPIN
	}
	aAvgErr = aSumErr / (STREAM_TYPE) STREAM_ARRAY_SIZE;
	bAvgErr = bSumErr / (STREAM_TYPE) STREAM_ARRAY_SIZE;
	cAvgErr = cSumErr / (STREAM_TYPE) STREAM_ARRAY_SIZE;

	if (sizeof(STREAM_TYPE) == 4) {
		epsilon = 1.e-6;
	}
	else if (sizeof(STREAM_TYPE) == 8) {
		epsilon = 1.e-13;
	}
	else {
		printf("WEIRD: sizeof(STREAM_TYPE) = %lu\n",sizeof(STREAM_TYPE));
		epsilon = 1.e-6;
	}

	err = 0;
	if (abs(aAvgErr/aj) > epsilon) {
		err++;
		printf ("Failed Validation on array a[], AvgRelAbsErr > epsilon (%e)\n",epsilon);
		printf ("     Expected Value: %e, AvgAbsErr: %e, AvgRelAbsErr: %e\n",aj,aAvgErr,abs(aAvgErr)/aj);
		ierr = 0;
		for (j=0; j<STREAM_ARRAY_SIZE; j++) {
			if (abs(a[j]/aj-1.0) > epsilon) {
				ierr++;
#ifdef VERBOSE
				if (ierr < 10) {
					printf("         array a: index: %ld, expected: %e, observed: %e, relative error: %e\n",
						j,aj,a[j],abs((aj-a[j])/aAvgErr));
				}
#endif
			}
		}
		printf("     For array a[], %d errors were found.\n",ierr);
	}
	if (abs(bAvgErr/bj) > epsilon) {
		err++;
		printf ("Failed Validation on array b[], AvgRelAbsErr > epsilon (%e)\n",epsilon);
		printf ("     Expected Value: %e, AvgAbsErr: %e, AvgRelAbsErr: %e\n",bj,bAvgErr,abs(bAvgErr)/bj);
		printf ("     AvgRelAbsErr > Epsilon (%e)\n",epsilon);
		ierr = 0;
		for (j=0; j<STREAM_ARRAY_SIZE; j++) {
			if (abs(b[j]/bj-1.0) > epsilon) {
				ierr++;
#ifdef VERBOSE
				if (ierr < 10) {
					printf("         array b: index: %ld, expected: %e, observed: %e, relative error: %e\n",
						j,bj,b[j],abs((bj-b[j])/bAvgErr));
				}
#endif
			}
		}
		printf("     For array b[], %d errors were found.\n",ierr);
	}
	if (abs(cAvgErr/cj) > epsilon) {
		err++;
		printf ("Failed Validation on array c[], AvgRelAbsErr > epsilon (%e)\n",epsilon);
		printf ("     Expected Value: %e, AvgAbsErr: %e, AvgRelAbsErr: %e\n",cj,cAvgErr,abs(cAvgErr)/cj);
		printf ("     AvgRelAbsErr > Epsilon (%e)\n",epsilon);
		ierr = 0;
		for (j=0; j<STREAM_ARRAY_SIZE; j++) {
			if (abs(c[j]/cj-1.0) > epsilon) {
				ierr++;
#ifdef VERBOSE
				if (ierr < 10) {
					printf("         array c: index: %ld, expected: %e, observed: %e, relative error: %e\n",
						j,cj,c[j],abs((cj-c[j])/cAvgErr));
				}
#endif
			}
		}
		printf("     For array c[], %d errors were found.\n",ierr);
	}
	if (err == 0) {
		printf ("Solution Validates: avg error less than %e on all three arrays\n",epsilon);
	}
#ifdef VERBOSE
	printf ("Results Validation Verbose Results: \n");
	printf ("    Expected a(1), b(1), c(1): %f %f %f \n",aj,bj,cj);
	printf ("    Observed a(1), b(1), c(1): %f %f %f \n",a[1],b[1],c[1]);
	printf ("    Rel Errors on a, b, c:     %e %e %e \n",abs(aAvgErr/aj),abs(bAvgErr/bj),abs(cAvgErr/cj));
#endif
}