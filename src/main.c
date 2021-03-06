#include <stdio.h>
#include "xmalloc.h"
#include <sys/time.h>
#include <stdint.h>
#include <float.h>
#include <getopt.h>
#include <assert.h>
#include <errno.h>
#include "util.h"
#include <stdlib.h>
#include <omp.h>
#include <time.h>
#include <string.h>

#include <malloc.h>

#include <x86intrin.h>

//#include <ipps.h>

//#include <ippcore.h>
//#include <ippvm.h>


// Random Tuning Parameters
//////////////////////////////
//typedef Ipp32s vec_t;
typedef uint32_t vec_t;
#define INFINITY_VALUE 1073741824
#define NEGATIVE_INFINITY_VALUE 0

// Function Prototypes
//////////////////////////////
void hostParseArgs(
    int argc, char** argv);
void tester(vec_t**, uint32_t,vec_t**, uint32_t,
            vec_t**, uint32_t,vec_t**, uint32_t, vec_t**);

// Global Host Variables
////////////////////////////
vec_t*    globalA;
vec_t*    globalB;
vec_t*    globalC;
vec_t*    CSorted;
vec_t*    CUnsorted;
uint32_t  h_ui_A_length                = 1000000;
uint32_t  h_ui_B_length                = 1000000;
uint32_t  h_ui_C_length                = 2000000;
uint32_t  h_ui_Ct_length               = 2000000;
uint32_t  RUNS                         = 10;
uint32_t  entropy                      = 28;

// _MM_SHUFFLE (z, y, x, w)
// (z<<6) | (y<<4) | (x<<2) | w
// Note that these indices are all reverse because
// an implcit reverse happens during the store, and
// thus we reverse here to avoid the need to reverse
// after we get the results.
const uint8_t m0110 =          (1<<4) | (1<<2);
const uint8_t m1010 = (1<<6) |          (1<<2);
const uint8_t m1100 = (1<<6) | (1<<4);
const uint8_t m1221 = (1<<6) | (2<<4) | (2<<2) | 1;
const uint8_t m2121 = (2<<6) | (1<<4) | (2<<2) | 1;
const uint8_t m2332 = (2<<6) | (3<<4) | (3<<2) | 2;
const uint8_t m3120 = (3<<6) | (1<<4) | (2<<2) | 0;
const uint8_t m3232 = (3<<6) | (2<<4) | (3<<2) | 2;

const uint8_t m0123 = (0<<6) | (1<<4) | (2<<2) | 3;
const uint8_t m0321 = (0<<6) | (3<<4) | (2<<2) | 1;
const uint8_t m2103 = (2<<6) | (1<<4) | (0<<2) | 3;
const uint8_t m0213 = (0<<6) | (2<<4) | (1<<2) | 3;
const uint8_t m1001 = (1<<6)                   | 1;

#define min(a,b) (a <= b)? a : b
#define max(a,b) (a <  b)? b : a

// Host Functions
////////////////////////////
int main(int argc, char** argv)
{
  // parse langths of A and B if user entered
  hostParseArgs(argc, argv);

  tester(&globalA, h_ui_A_length,
      &globalB, h_ui_B_length,
      &globalC, h_ui_C_length,
      &CSorted, h_ui_Ct_length,
      &CUnsorted);

  free(globalA);
  free(globalB);
  free(globalC);
  free(CSorted);
  free(CUnsorted);
}


int hostBasicCompare(const void * a, const void * b) {
  return (int) (*(vec_t *)a - *(vec_t *)b);
}

void Init(vec_t** A, uint32_t A_length,
    vec_t** B, uint32_t B_length,
    vec_t** C, uint32_t C_length,
    vec_t** CSorted, uint32_t Ct_length,
    vec_t** CUnsorted)
{
  *A  = (vec_t*) xmalloc((A_length  + 8) * (sizeof(vec_t)));
  *B  = (vec_t*) xmalloc((B_length  + 8) * (sizeof(vec_t)));
  *C  = (vec_t*) xmalloc((C_length  + 32) * (sizeof(vec_t)));
  *CSorted = (vec_t*) xmalloc((Ct_length + 32) * (sizeof(vec_t)));
  *CUnsorted = (vec_t*) xmalloc((Ct_length + 32) * (sizeof(vec_t)));

   uint32_t seed = time(0);// % 100000000;
//  uint32_t seed = 13503;
  srand(seed);

  for(uint32_t i = 0; i < A_length; ++i) {
    (*A)[i] = rand() % (1 << (entropy - 1));
    (*CUnsorted)[i] = (*A)[i];
  }

  for(uint32_t i = 0; i < B_length; ++i) {
    (*B)[i] = rand() % (1 << (entropy - 1));
   (*CUnsorted)[i+A_length] = (*A)[i];
  }

  qsort(*A, A_length, sizeof(vec_t), hostBasicCompare);
  qsort(*B, B_length, sizeof(vec_t), hostBasicCompare);

  for(int i = 0; i < 8; ++i) {
    (*A)[A_length + i] = INFINITY_VALUE; (*B)[B_length + i] = INFINITY_VALUE;
  }

  // reference 'CORRECT' results
  uint32_t ai = 0,bi = 0,ci = 0;
  while(ai < A_length && bi < B_length) {
    (*CSorted)[ci++] = (*A)[ai] < (*B)[bi] ? (*A)[ai++] : (*B)[bi++];
  }
  while(ai < A_length) (*CSorted)[ci++] = (*A)[ai++];
  while(bi < B_length) (*CSorted)[ci++] = (*B)[bi++];
}

void serialMerge(
    vec_t* A, uint32_t A_length,
    vec_t* B, uint32_t B_length,
    vec_t* C, uint32_t C_length)
{
  uint32_t Aindex = 0;
  uint32_t Bindex = 0;
  uint32_t Cindex = 0;
  int32_t flag;

  while(Aindex < A_length && Bindex < B_length) {
    C[Cindex++] = A[Aindex] < B[Bindex] ? A[Aindex++] : B[Bindex++];
  }
  while(Aindex < A_length) C[Cindex++] = A[Aindex++];
  while(Bindex < B_length) C[Cindex++] = B[Bindex++];
}


  
void serialMergeNoBranch(
      vec_t* A, uint32_t A_length,
      vec_t* B, uint32_t B_length,
      vec_t* C, uint32_t C_length)
  {
    uint32_t Aindex = 0;
    uint32_t Bindex = 0;
    uint32_t Cindex = 0;
    int32_t flag;
    //	uint32_t mask,notMask;
    while(Aindex < A_length && Bindex < B_length) {
      flag = ((unsigned int)(A[Aindex] - B[Bindex]) >> 31 ) ;
      C[Cindex++] = (flag)*A[Aindex] + (1-flag)*B[Bindex];
      Aindex +=flag;
      Bindex +=1-flag;
    }
    while(Aindex < A_length) C[Cindex++] = A[Aindex++];
    while(Bindex < B_length) C[Cindex++] = B[Bindex++];
}


void bitonicMergeReal(vec_t* A, uint32_t A_length,
                      vec_t* B, uint32_t B_length,
                      vec_t* C, uint32_t C_length){
  uint32_t Aindex = 0,Bindex = 0, Cindex = 0;

  __m128i sA = _mm_loadu_si128((const __m128i*)&(A[Aindex]));
  __m128i sB = _mm_loadu_si128((const __m128i*)&(B[Bindex]));
  while ((Aindex < (A_length-4)) || (Bindex < (B_length-4)))
  {
    // load SIMD registers from A and B
   // reverse B
    sB = _mm_shuffle_epi32(sB, m0123);
    // level 1
    __m128i sL1 = _mm_min_epu32(sA, sB);
    __m128i sH1 = _mm_max_epu32(sA, sB);
    __m128i sL1p = _mm_unpackhi_epi64(sH1, sL1);
    __m128i sH1p = _mm_unpacklo_epi64(sH1, sL1);
    // level 2
    __m128i sL2 = _mm_min_epu32(sH1p, sL1p);
    __m128i sH2 = _mm_max_epu32(sH1p, sL1p);
    __m128i c1010 = _mm_set_epi32(-1, 0, -1, 0); 
    __m128i c0101 = _mm_set_epi32(0, -1, 0, -1);
    // use blend
    __m128i sL2p = _mm_or_si128(_mm_and_si128(sL2, c1010), _mm_and_si128(_mm_shuffle_epi32(sH2, m0321), c0101));
    __m128i sH2p = _mm_or_si128(_mm_and_si128(_mm_shuffle_epi32(sL2, m2103), c1010), _mm_and_si128(sH2, c0101));
    // level 3
    __m128i sL3 = _mm_min_epu32(sL2p, sH2p);
    __m128i sH3 = _mm_max_epu32(sL2p, sH2p);
    __m128i sL3p = _mm_shuffle_epi32(_mm_unpackhi_epi64(sH3, sL3), m0213);
    __m128i sH3p = _mm_shuffle_epi32(_mm_unpacklo_epi64(sH3, sL3), m0213);
    // store back data into C from SIMD registers
    _mm_storeu_si128((__m128i*)&(C[Cindex]), sL3p);
    // calculate index for the next run
    sB=sH3p;          
    Cindex+=4;
    if (A[Aindex+4]<B[Bindex+4]){
    	Aindex+=4;
    	sA = _mm_loadu_si128((const __m128i*)&(A[Aindex]));
    }
    else {
    	Bindex+=4;
    	sA = _mm_loadu_si128((const __m128i*)&(B[Bindex]));
    }
 } 
}

inline void serialMergeIntrinsicAVX( vec_t* A, int32_t A_length,
                                     vec_t* B, int32_t B_length,
                                     vec_t* C, uint32_t C_length) {
  uint32_t ai = 0;
  uint32_t bi = 0;
  uint32_t ci = 0;

  __m256i mione = _mm256_set_epi32(0, 0, 0, 0, 0, 0, 0, 1); //256bit 1
  __m256i miand, miandnot;
  __m256i miAi = _mm256_set_epi32(0, 0, 0, 0, 0, 0, 0, 0); //ai
  __m256i miBi = _mm256_set_epi32(0, 0, 0, 0, 0, 0, 0, 0); //bi

  while (ai < A_length && bi < B_length) {
    __m256i miAelem = _mm256_set_epi32(0, 0, 0, 0, 0, 0, 0, A[ai]); //256bit A[ai]
    __m256i miBelem = _mm256_set_epi32(0, 0, 0, 0, 0, 0, 0, B[bi]); //256bit B[bi]
    __m256i micmp   = _mm256_cmpgt_epi32(miBelem, miAelem); //checks if B[bi] > A[ai]? 0xFFFFFFFF:0
    miand           = _mm256_and_si256(micmp, mione); //micmp & mione - result is either 1 or 0
    miandnot        = _mm256_andnot_si256(micmp, mione); //NOT(micmp) & mione - result is either 0 or 1
    miAelem         = _mm256_and_si256(micmp, miAelem); //micmp & miAelem - A[ai] or 0
    miBelem         = _mm256_andnot_si256(micmp, miBelem); //NOT(micmp) & miBelem - 0 or B[bi]
    miAi            = _mm256_add_epi32(miAi, miand); //miAi + miand
    miBi            = _mm256_add_epi32(miBi, miandnot); //miBi + miandnot
    C[ci++]         = _mm256_extract_epi32(_mm256_add_epi32(miAelem, miBelem), 0); //copy miAelem + miBelem into C[ci] and increment ci
    ai              = _mm256_extract_epi32(miAi, 0); //copy miAi into ai
    bi              = _mm256_extract_epi32(miBi, 0); //copy miBi into bi
  }

  while(ai < A_length) {
    C[ci++] = A[ai++];
  }
  while(bi < B_length) {
    C[ci++] = B[bi++];
  }

  for (int i = 0; i < C_length; i++) {
      if(C[i] != CSorted[i])
      {
        printf("\n %d,%d,%d \n", i, C[i], CSorted[i]);
        return;
      }
    }
    printf("\n %s", "Success!");

}

inline void serialMergeIntrinsic( vec_t* A, int32_t A_length,
                                  vec_t* B, int32_t B_length,
                                  vec_t* C, uint32_t C_length){
	uint32_t ai = 0;
	uint32_t bi = 0;
	uint32_t ci = 0;

	__m128i mione = _mm_cvtsi32_si128(1); //copies 32bit integer 1 to mione -> basically makes mione a 128bit 1
	__m128i miand, miandnot;
	__m128i miAi = _mm_cvtsi32_si128(0); //ai);
	__m128i miBi = _mm_cvtsi32_si128(0); //bi);

	// consider the two cases when micmp evaluates to 0xFFFFFFFF (A[ai] < B[bi]) and when it evaluates to 0 (A[ai] > B[bi]). Correspondingly, either A[ai] gets copied or B[bi] gets copied.
  while(ai < A_length && bi < B_length) {
		__m128i miAelem = _mm_cvtsi32_si128(A[ai]); //128bit A[ai]
		__m128i miBelem = _mm_cvtsi32_si128(B[bi]); //128bit B[bi]
		__m128i micmp   = _mm_cmplt_epi32(miAelem,miBelem); //checks if A[ai] < B[bi]? 0xFFFFFFFF:0
		miand           = _mm_and_si128(micmp,mione); //micmp & mione - result is either 1 or 0
		miandnot        = _mm_andnot_si128(micmp,mione); //NOT(micmp) & mione - result is either 0 or 1
		miAelem         = _mm_and_si128(micmp,miAelem); //micmp & miAelem - A[ai] or 0
		miBelem         = _mm_andnot_si128(micmp,miBelem); //NOT(micmp) & miBelem - 0 or B[bi]
		miAi            = _mm_add_epi32(miAi,miand); //miAi + miand
		miBi            = _mm_add_epi32(miBi,miandnot); //miBi + miandnot
		C[ci++]         = _mm_cvtsi128_si32(_mm_add_epi32(miAelem,miBelem)); //copy miAelem + miBelem into C[ci] and increment ci
		ai              = _mm_cvtsi128_si32(miAi); //copy miAi into ai
		bi              = _mm_cvtsi128_si32(miBi); //copy miBi into bi
	}

	while(ai < A_length) {
		C[ci++] = A[ai++];
	}
	while(bi < B_length) {
		C[ci++] = B[bi++];
	}
}

#define PRINTEXTRA 0
#define STORE_AND_PRINT(str,reg) {if (PRINTEXTRA){vec_t arr[4]; _mm_storeu_si128((__m128i*)&arr,reg);printf("%s : %d %d %d %d\n", str, arr[3],arr[2],arr[1],arr[0]); }}

const uint8_t min1=(3<<6)| (3<<4)|(2<<2) |1;
const uint8_t max1=(2<<6)| (1<<4);

void mergeNetwork(vec_t* A, int32_t A_length,
                  vec_t* B, int32_t B_length,
                  vec_t* C, uint32_t C_length){
  uint32_t Aindex = 0,Bindex = 0, Cindex = 0;

  __m128i sA = _mm_loadu_si128((const __m128i*)&(A[Aindex]));
  __m128i sB = _mm_loadu_si128((const __m128i*)&(B[Bindex]));
  __m128i mifour = _mm_cvtsi32_si128(4);


   while ((Aindex < (A_length-4)) || (Bindex < (B_length-4)))
  {
      //STORE_AND_PRINT("original ", sA);
      //STORE_AND_PRINT("original ", sB);

      __m128i smin1 = _mm_min_epu32(sA, sB);
      __m128i smax1 = _mm_max_epu32(sA, sB);
      //STORE_AND_PRINT("first min", smin1);
      //STORE_AND_PRINT("first max", smax1);
      __m128i smin1shift = _mm_shuffle_epi32(smin1, (3<<6)| (3<<4)|(2<<2) |1);
      __m128i smax1shift = _mm_shuffle_epi32(smax1, (2<<6)| (1<<4));
      //STORE_AND_PRINT("shift min", smin1shift);
      //STORE_AND_PRINT("shift max", smax1shift);

      __m128i smin2 = _mm_min_epu32(smin1, smax1shift);
      __m128i smax2 = _mm_max_epu32(smax1, smin1shift);
      //STORE_AND_PRINT("2nd   min",smin2);
      //STORE_AND_PRINT("2nd   max",smax2);
      __m128i smin2shift = _mm_shuffle_epi32(smin2, (3<<6)| (3<<4)|(3<<2) |2);
      __m128i smax2shift = _mm_shuffle_epi32(smax2, (1<<6));
      //STORE_AND_PRINT("shift min", smin2shift);
      //STORE_AND_PRINT("shift max", smax2shift);

      __m128i smin3 = _mm_min_epu32(smin2, smax2shift);
      __m128i smax3 = _mm_max_epu32(smax2, smin2shift);
      //STORE_AND_PRINT("3rd   min",smin3);
      //STORE_AND_PRINT("3rd   max",smax3);
      __m128i smin3shift = _mm_shuffle_epi32(smin3, (3<<6)| (3<<4)|(3<<2) | 3);
      __m128i smax3shift = _mm_shuffle_epi32(smax3, 0);
      //STORE_AND_PRINT("shift min", smin3shift);
      //STORE_AND_PRINT("shift max", smax3shift);

      __m128i smin4 = _mm_min_epu32(smin3, smax3shift);
      __m128i smax4 = _mm_max_epu32(smax3, smin3shift);      //STORE_AND_PRINT("4th   min",smin4);
      //STORE_AND_PRINT("4th   max",smax4);

      #if (PRINTEXTRA==1)
        printf("\n");
      #endif
      _mm_storeu_si128((__m128i*)&(C[Cindex]), smin4);
      // calculate index for the next run
      sB=smax4; 
      Cindex+=4;

      __m128i tempA = _mm_loadu_si128((const __m128i*)&(A[Aindex+4]));
      __m128i tempB = _mm_loadu_si128((const __m128i*)&(B[Bindex+4]));
      // __m128i tempAS = _mm_shuffle_epi32(tempA,0);
      // __m128i tempBS = _mm_shuffle_epi32(tempB,0);
      // __m128i cmpAB = _mm_cmplt_epi32(tempAS,tempBS);
      
      __m128i cmpAB = _mm_cmplt_epi32(_mm_shuffle_epi32(tempA,0),_mm_shuffle_epi32(tempB,0));

      __m128i addA  = _mm_and_si128(cmpAB,mifour);
      __m128i addB  = _mm_andnot_si128(cmpAB,mifour);
      Aindex       += _mm_cvtsi128_si32(addA);
      Bindex       += _mm_cvtsi128_si32(addB);

      tempA         = _mm_and_si128(cmpAB,tempA);
      tempB         = _mm_andnot_si128(cmpAB,tempB);
      sA            = _mm_add_epi32(tempA,tempB);



  //    printf("%d, %d, %d \n", Aindex, Bindex, Cindex);
  //    printf("%d, %d, %d \n", Aindex, Bindex, Cindex);
      // if (A[Aindex+4]<B[Bindex+4]) {
      //   Aindex+=4;
      //   sA = _mm_loadu_si128((const __m128i*)&(A[Aindex]));
      // }
      // else{
      //   Bindex+=4;
      //   sA = _mm_loadu_si128((const __m128i*)&(B[Bindex]));
      // }
    //    printf("INDICES are %d %d %d\n", Aindex,Bindex,Cindex);
    }

 // printf("indices are %d %d %d\n", Aindex,Bindex,Cindex);
    vec_t last4[4]; int countlast4=0;
    _mm_storeu_si128((__m128i*)&(last4), sB);

    while (countlast4<4 && Aindex < A_length && Bindex < B_length){
      if (last4[countlast4] < A[Aindex] && A[Aindex] < B[Bindex]){
        C[Cindex++] = A[Aindex++];
        countlast4++;
      }
      else if (last4[countlast4] < B[Bindex] && B[Bindex] < A[Aindex]){
        C[Cindex++] = B[Bindex];
        countlast4++;
      }
      else{
        C[Cindex++] = A[Aindex] < B[Bindex] ? A[Aindex++] : B[Bindex++];
      }
    }

    while(Aindex < A_length && Bindex < B_length) {
      C[Cindex++] = A[Aindex] < B[Bindex] ? A[Aindex++] : B[Bindex++];
    }
    while(Aindex < A_length) C[Cindex++] = A[Aindex++];
    while(Bindex < B_length) C[Cindex++] = B[Bindex++];
 // printf("indices are %d %d %d\n", Aindex,Bindex,Cindex);

  //  float time_elapsed = tic_sincelast();
  // check sanity of results
    // for(int i = 0; i < C_length; ++i) {
    //   //assert(C[i] == globalC[i]);
    //   if(C[i]!=CSorted[i])
    //   {
    //     printf("\n %d,%d,%d \n", i,C[i],CSorted[i]);
    //     return;
    //   }
    // }
    

}

void tester(
    vec_t** A, uint32_t A_length,
    vec_t** B, uint32_t B_length,
    vec_t** C, uint32_t C_length,
    vec_t** CSorted, uint32_t Ct_length,
    vec_t** CUnsorted)
{
  Init(A, A_length,
      B, B_length,
      C, C_length,
      CSorted, Ct_length,
      CUnsorted);

  float serial   = 0.0;
  float serialNoBranch   = 0.0;
  float bitonicReal = 0.0;
  float intrinsic = 0.0;
  float intrinsicAVX = 0.0;
  float mergenet = 0.0;
  float avx2 = 0.0;

  vec_t* Cptr=*C; 

  for (int i = 0; i < RUNS; i++) {

    tic_reset();
    serialMerge(*A, A_length, *B, B_length, *C, C_length);
    serial += tic_sincelast();
    for(int ci=0; ci<C_length; ci++) {Cptr[ci]=0;}

    tic_reset();
    serialMergeNoBranch(*A, A_length, *B, B_length, *C, Ct_length);
    serialNoBranch += tic_sincelast();
    for(int ci=0; ci<C_length; ci++) {Cptr[ci]=0;}

    tic_reset();
    bitonicMergeReal(*A, A_length, *B, B_length, *C, Ct_length);
    bitonicReal+=tic_sincelast();	
    for(int ci=0; ci<C_length; ci++) {Cptr[ci]=0;}

    tic_reset();
    serialMergeIntrinsic(*A, A_length, *B, B_length, *C, Ct_length);
    intrinsic+=tic_sincelast();	
    for(int ci=0; ci<C_length; ci++) {Cptr[ci]=0;}

    tic_reset();
    serialMergeIntrinsicAVX(*A, A_length, *B, B_length, *C, Ct_length);
    intrinsicAVX+=tic_sincelast(); 
    for(int ci=0; ci<C_length; ci++) {Cptr[ci]=0;}

    tic_reset();
    mergeNetwork(*A, A_length, *B, B_length, *C, Ct_length);
    mergenet+=tic_sincelast(); 
    for(int ci=0; ci<C_length; ci++) {Cptr[ci]=0;}

    const int threads=8;
  	uint32_t* Aindices = (uint32_t*)memalign(32, (threads+1) * sizeof(uint32_t));
  	uint32_t* Bindices = (uint32_t*)memalign(32, (threads+1) * sizeof(uint32_t));
  	uint32_t* Cindices = (uint32_t*)memalign(32, (threads+1) * sizeof(uint32_t));
  	uint32_t* AEndindices = (uint32_t*)memalign(32, (threads) * sizeof(uint32_t));
  	uint32_t* BEndindices = (uint32_t*)memalign(32, (threads) * sizeof(uint32_t));
  	uint32_t* CEndindices = (uint32_t*)memalign(32, (threads) * sizeof(uint32_t));

   	
  	free(Aindices);
  	free(Bindices);
  	free(Cindices);
  	free(AEndindices);
  	free(BEndindices);
  	free(CEndindices);

  }

  printf("%d", entropy);
  printf(",%.10f", 1e8*(serial / (float)(RUNS*Ct_length)));
  printf(",%.10f", 1e8*(serialNoBranch / (float)(RUNS*Ct_length)));
  printf(",%.10f", 1e8*(bitonicReal / (float)(RUNS*Ct_length)));
  printf(",%.10f", 1e8*(intrinsic / (float)(RUNS*Ct_length)));
  printf(",%.10f", 1e8*(intrinsicAVX / (float)(RUNS*Ct_length)));
  printf(",%.10f", 1e8*(mergenet / (float)(RUNS*Ct_length)));


  printf("\n");
  int32_t swapped,missed;


  vec_t Astam[20]={1,1,1,3,4,4,7,7,8,9,100000,100000,100000,100000,100000,100000,100000,100000,100000,100000};
  vec_t Bstam[20]={2,2,3,3,4,5,6,7,8,9,100000,100000,100000,100000,100000,100000,100000,100000,100000,100000};
  vec_t Cstam[30];

// mergeNetwork(Astam,10,Bstam,10, Cstam,20);
//  mergeNetwork(*A, A_length, *B, B_length, *Ct, Ct_length);

  // for (int ci=0; ci<20; ci++)
  //   printf("%d, ", Cstam[ci]);
  //   // printf("%d, ", (*Ct)[ci]);
  // printf("\n");

//  return;

  return;


}


void MergePathSplitter( vec_t * A, uint32_t A_length, vec_t * B, uint32_t B_length, vec_t * C, uint32_t C_length, 
    uint32_t threads, uint32_t* splitters) {
  splitters[threads*2] = A_length;
  splitters[threads*2+1] = B_length;

  for (int thread=0; thread<threads;thread++)
  {
    // uint32_t thread = omp_get_thread_num();
    int32_t combinedIndex = thread * (A_length + B_length) / threads;
    int32_t x_top, y_top, x_bottom, current_x, current_y, offset;
    x_top = combinedIndex > A_length ? A_length : combinedIndex;
    y_top = combinedIndex > (A_length) ? combinedIndex - (A_length) : 0;
    x_bottom = y_top;

    vec_t Ai, Bi;
    while(1) {
      offset = (x_top - x_bottom) / 2;
      current_y = y_top + offset;
      current_x = x_top - offset;
      if(current_x > A_length - 1 || current_y < 1) {
        Ai = 1;Bi = 0;
      } else {
        Ai = A[current_x];Bi = B[current_y - 1];
      }
      if(Ai > Bi) {
        if(current_y > B_length - 1 || current_x < 1) {
          Ai = 0;Bi = 1;
        } else {
          Ai = A[current_x - 1];Bi = B[current_y];
        }

        if(Ai <= Bi) {//Found it
          splitters[thread*2]   = current_x;splitters[thread*2+1] = current_y;
          break;
        } else {//Both zeros
          x_top = current_x - 1;y_top = current_y + 1;
        }
      } else {// Both ones
        x_bottom = current_x + 1;
      }
    }
//    #pragma omp barrier

    // uint32_t astop = uip_diagonal_intersections[thread*2+2];
    // uint32_t bstop = uip_diagonal_intersections[thread*2+3];
    // uint32_t ci = current_x + current_y;

    // while(current_x < astop && current_y < bstop) {
    //   C[ci++] = A[current_x] < B[current_y] ? A[current_x++] : B[current_y++];
    // }
    // while(current_x < astop) {
    //   C[ci++] = A[current_x++];
    // }
    // while(current_y < bstop) {
    //   C[ci++] = B[current_y++];
    // }
  }
}





#define PRINT_ARRAY(ARR) for (int t=0; t<threads;t++){printf("%10d, ",ARR[t]);}printf("\n");
#define PRINT_ARRAY_INDEX(ARR,IND) for (int t=0; t<threads;t++){printf("%10d, ",ARR[IND[t]]);}printf("\n");



void hostParseArgs(int argc, char** argv)
{
  static struct option long_options[] = {
    {"Alength", required_argument, 0, 'A'},
    {"Blength", required_argument, 0, 'B'},
    {"Runs"   , optional_argument, 0, 'R'},
    {"Entropy", optional_argument, 0, 'E'},
    {"help"   , no_argument      , 0, 'h'},
    {0        , 0                , 0,  0 }
  };

  while(1) {
    int option_index = 0;
    int c = getopt_long(argc, argv, "A:B:R:E:h",
  long_options, &option_index);
    extern char * optarg;
    extern int    optind, opterr, optopt;
    int intout = 0;

    if(-1 == c)
      break;

    switch(c) {
      default:
        printf("Unrecognized option: %c\n\n", c);
      case 'h':
        printf("\nCreates two arrays A and B and "
            "merges them into array C in parallel on OpenMP.");
        printf("\n\nUsage"
            "\n====="
            "\n\n\t-A --Alength <number>"
            "\n\t\tSpecify the length of randomly "
            "generated array A.\n"
            "\n\t-B --Blength <number>"
            "\n\t\tSpecify the length of randomly "
            "generated array B.\n"
            "\n\t-R --Runs <number>"
            "\n\t\tSpecify the number of runs to be used for "
            "serial and parallel algorithms.\n"
            "\n\t-E --Entropy <number>"
            "\n\t\tSpecify the number of bits of entropy "
            "to be used for random number generation\n"
            );
         exit(0);
         break;
      case 'A':
        errno = 0;
        intout = strtol(optarg, NULL, 10);
        if(errno || intout < 0) {
          printf("Error - Alength %s\n", optarg);
          exit(-1);
        }
        h_ui_A_length = intout;
        break;
      case 'B':
        errno = 0;
        intout = strtol(optarg, NULL, 10);
        if(errno || intout < 0) {
          printf("Error - Blength %s\n", optarg);
          exit(-1);
        }
        h_ui_B_length = intout;
        break;
      case 'R':
        intout = strtol(optarg, NULL, 10);
        if(errno || intout < 0) {
          printf("Error - Runs %s\n", optarg);
          exit(-1);
        }
        RUNS = intout;
        break;
      case 'E':
        intout = strtol(optarg, NULL, 10);
        if(errno || intout < 0) {
          printf("Error - Entropy %s\n", optarg);
          exit(-1);
        }
        entropy = (intout < 1) ? 1: intout;
        //        printf("entropy %d\n", entropy);
        //               fprintf(stderr, "Entropy = %d.\n", entropy);
        break;
    }
  }
  h_ui_C_length = h_ui_A_length + h_ui_B_length;
  h_ui_Ct_length = h_ui_C_length;
}
