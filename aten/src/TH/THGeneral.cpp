#include <TH/THGeneral.h>

#ifdef __cplusplus
#include <c10/core/CPUAllocator.h>
#endif

#ifdef _OPENMP
#include <omp.h>
#endif

#ifndef TH_HAVE_THREAD
#define __thread
#elif _MSC_VER
#define __thread __declspec( thread )
#endif

#if (defined(__unix) || defined(_WIN32))
  #if defined(__FreeBSD__)
    #include <malloc_np.h>
  #else
    #include <malloc.h>
  #endif
#elif defined(__APPLE__)
#include <malloc/malloc.h>
#endif

#ifdef TH_BLAS_MKL
#include <mkl.h>
#endif

/* Torch Error Handling */
static void defaultErrorHandlerFunction(const char *msg, void *data)
{
  printf("$ Error: %s\n", msg);
  exit(-1);
}

static THErrorHandlerFunction defaultErrorHandler = defaultErrorHandlerFunction;
static void *defaultErrorHandlerData;
static __thread THErrorHandlerFunction threadErrorHandler = NULL;
static __thread void *threadErrorHandlerData;

void _THError(const char *file, const int line, const char *fmt, ...)
{
  char msg[2048];
  va_list args;

  /* vasprintf not standard */
  /* vsnprintf: how to handle if does not exists? */
  va_start(args, fmt);
  int n = vsnprintf(msg, 2048, fmt, args);
  va_end(args);

  if(n < 2048) {
    snprintf(msg + n, 2048 - n, " at %s:%d", file, line);
  }

  if (threadErrorHandler)
    (*threadErrorHandler)(msg, threadErrorHandlerData);
  else
    (*defaultErrorHandler)(msg, defaultErrorHandlerData);
  TH_UNREACHABLE;
}

void _THAssertionFailed(const char *file, const int line, const char *exp, const char *fmt, ...) {
  char msg[1024];
  va_list args;
  va_start(args, fmt);
  vsnprintf(msg, 1024, fmt, args);
  va_end(args);
  _THError(file, line, "Assertion `%s' failed. %s", exp, msg);
}

void THSetErrorHandler(THErrorHandlerFunction new_handler, void *data)
{
  threadErrorHandler = new_handler;
  threadErrorHandlerData = data;
}

void THSetDefaultErrorHandler(THErrorHandlerFunction new_handler, void *data)
{
  if (new_handler)
    defaultErrorHandler = new_handler;
  else
    defaultErrorHandler = defaultErrorHandlerFunction;
  defaultErrorHandlerData = data;
}

/* Torch Arg Checking Handling */
static void defaultArgErrorHandlerFunction(int argNumber, const char *msg, void *data)
{
  if(msg)
    printf("$ Invalid argument %d: %s\n", argNumber, msg);
  else
    printf("$ Invalid argument %d\n", argNumber);
  exit(-1);
}

static THArgErrorHandlerFunction defaultArgErrorHandler = defaultArgErrorHandlerFunction;
static void *defaultArgErrorHandlerData;
static __thread THArgErrorHandlerFunction threadArgErrorHandler = NULL;
static __thread void *threadArgErrorHandlerData;

void _THArgCheck(const char *file, int line, int condition, int argNumber, const char *fmt, ...)
{
  if(!condition) {
    char msg[2048];
    va_list args;

    /* vasprintf not standard */
    /* vsnprintf: how to handle if does not exists? */
    va_start(args, fmt);
    int n = vsnprintf(msg, 2048, fmt, args);
    va_end(args);

    if(n < 2048) {
      snprintf(msg + n, 2048 - n, " at %s:%d", file, line);
    }

    if (threadArgErrorHandler)
      (*threadArgErrorHandler)(argNumber, msg, threadArgErrorHandlerData);
    else
      (*defaultArgErrorHandler)(argNumber, msg, defaultArgErrorHandlerData);
    TH_UNREACHABLE;
  }
}

void THSetArgErrorHandler(THArgErrorHandlerFunction new_handler, void *data)
{
  threadArgErrorHandler = new_handler;
  threadArgErrorHandlerData = data;
}

void THSetDefaultArgErrorHandler(THArgErrorHandlerFunction new_handler, void *data)
{
  if (new_handler)
    defaultArgErrorHandler = new_handler;
  else
    defaultArgErrorHandler = defaultArgErrorHandlerFunction;
  defaultArgErrorHandlerData = data;
}

static __thread void (*torchGCFunction)(void *data) = NULL;
static __thread void *torchGCData;

/* Optional hook for integrating with a garbage-collected frontend.
 *
 * If torch is running with a garbage-collected frontend (e.g. Lua),
 * the GC isn't aware of TH-allocated memory so may not know when it
 * needs to run. These hooks trigger the GC to run in two cases:
 *
 * (1) When a memory allocation (malloc, realloc, ...) fails
 * (2) When the total TH-allocated memory hits a dynamically-adjusted
 *     soft maximum.
 */
void THSetGCHandler( void (*torchGCFunction_)(void *data), void *data )
{
  torchGCFunction = torchGCFunction_;
  torchGCData = data;
}

void* THAlloc(ptrdiff_t size)
{
  if(size < 0)
    THError("$ Torch: invalid memory size -- maybe an overflow?");

  return c10::alloc_cpu(size);
}

void* THRealloc(void *ptr, ptrdiff_t size)
{
  if(!ptr)
    return(THAlloc(size));

  if(size == 0)
  {
    THFree(ptr);
    return NULL;
  }

  if(size < 0)
    THError("$ Torch: invalid memory size -- maybe an overflow?");

  void *newptr = realloc(ptr, size);

  if(!newptr && torchGCFunction) {
    torchGCFunction(torchGCData);
    newptr = realloc(ptr, size);
  }

  if(!newptr)
    THError("$ Torch: not enough memory: you tried to reallocate %dGB. Buy new RAM!", size/1073741824);

  return newptr;
}

void THFree(void *ptr)
{
  free(ptr);
}

double THLog10(const double x)
{
  return log10(x);
}

double THLog1p(const double x)
{
#if (defined(_MSC_VER) || defined(__MINGW32__))
  volatile double y = 1 + x;
  return log(y) - ((y-1)-x)/y ;  /* cancels errors with IEEE arithmetic */
#else
  return log1p(x);
#endif
}

double THLog2(const double x)
{
  return log2(x);
}

double THExpm1(const double x)
{
  return expm1(x);
}

void THSetNumThreads(int num_threads)
{
#ifdef _OPENMP
  omp_set_num_threads(num_threads);
#endif
#ifdef TH_BLAS_MKL
  mkl_set_num_threads(num_threads);

  // because PyTorch uses OpenMP outside of MKL invocations
  // as well, we want this flag to be false, so that
  // threads aren't destroyed and recreated across every
  // MKL / non-MKL boundary of OpenMP usage
  // See https://github.com/pytorch/pytorch/issues/13757
  mkl_set_dynamic(false);
#endif

}

int THGetNumThreads(void)
{
#ifdef _OPENMP
  return omp_get_max_threads();
#else
  return 1;
#endif
}

int THGetNumCores(void)
{
#ifdef _OPENMP
  return omp_get_num_procs();
#else
  return 1;
#endif
}

TH_API void THInferNumThreads(void)
{
#if defined(_OPENMP) && defined(TH_BLAS_MKL)
  // If we are using MKL an OpenMP make sure the number of threads match.
  // Otherwise, MKL and our OpenMP-enabled functions will keep changing the
  // size of the OpenMP thread pool, resulting in worse performance (and memory
  // leaks in GCC 5.4)
  omp_set_num_threads(mkl_get_max_threads());
#endif
}

THDescBuff _THSizeDesc(const int64_t *size, const int64_t ndim) {
  const int L = TH_DESC_BUFF_LEN;
  THDescBuff buf;
  char *str = buf.str;
  int i, n = 0;
  n += snprintf(str, L-n, "[");

  for (i = 0; i < ndim; i++) {
    if (n >= L) break;
    n += snprintf(str+n, L-n, "%" PRId64, size[i]);
    if (i < ndim-1) {
      n += snprintf(str+n, L-n, " x ");
    }
  }

  if (n < L - 2) {
    snprintf(str+n, L-n, "]");
  } else {
    snprintf(str+L-5, 5, "...]");
  }

  return buf;
}
