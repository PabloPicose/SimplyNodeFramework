// Configures ASAN for the profiler test binary.
// The profiler overrides global operator new/delete and routes everything
// through malloc/free internally. ASAN's alloc_dealloc_mismatch check treats
// operator new(nothrow) → free() as a mismatch even though the memory was
// allocated via malloc. Suppress that false-positive for this binary.
#ifdef __SANITIZE_ADDRESS__
extern "C" const char* __asan_default_options() {
    return "alloc_dealloc_mismatch=0";
}
#endif
