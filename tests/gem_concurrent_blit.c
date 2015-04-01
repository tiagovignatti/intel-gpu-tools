/* This test is just a duplicate of gem_concurrent_all. */
/* However the executeable will be gem_concurrent_blit. */
/* The main function examines argv[0] and, in the case  */
/* of gem_concurent_blit runs only a subset of the      */
/* available subtests. This avoids the use of           */
/* non-standard command line parameters which can cause */
/* problems for automated testing */
#include "gem_concurrent_all.c"
