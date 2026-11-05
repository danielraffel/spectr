// Keep the product version in the shipped CLAP image.  CLAP has no bundle
// Info.plist, so the release checker verifies the descriptor's binary bytes.
// The exported anchor prevents a linker dead-strip from removing the string.
#if defined(_WIN32)
#define SPECTR_CLAP_EXPORT __declspec(dllexport)
#else
#define SPECTR_CLAP_EXPORT __attribute__((visibility("default")))
#endif

extern "C" SPECTR_CLAP_EXPORT const char spectr_product_version_anchor[] =
    SPECTR_PRODUCT_VERSION;
