// Ensure that if CUDA and KokkosCompat are enabled, then
// only the .cu version of this file is actually compiled.
#include <Tpetra_ConfigDefs.hpp>
#ifdef HAVE_TPETRACORE_TEUCHOSKOKKOSCOMPAT
#  include <KokkosCore_config.h>
#  ifdef KOKKOS_HAVE_CUDA
#    define KOKKOS_USE_CUDA_BUILD
#    include "CrsMatrix/CrsMatrix_ReindexColumns.cpp"
#    undef KOKKOS_USE_CUDA_BUILD
#  endif // KOKKOS_HAVE_CUDA
#endif // HAVE_TPETRACORE_TEUCHOSKOKKOSCOMPAT