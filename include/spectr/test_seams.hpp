#pragma once

#include <cstdlib>

/// Test seams: environment variables that drive, dump or deliberately break
/// Spectr so a test can prove it sees a defect (negative controls), load a
/// script into the editor, synthesise input, or tune Auto Gain.
///
/// None of them belong in a binary a user runs. A release build compiles every
/// one out: SPECTR_TEST_ENV("NAME") becomes a null pointer, so the variable is
/// never read and its name never reaches the binary. They are compiled in only
/// when SPECTR_ENABLE_TEST_SEAMS is defined, which CMake does for test
/// executables always and for the plug-in, standalone and probe targets only
/// with -DSPECTR_ENABLE_TEST_SEAMS=ON (default OFF; package.sh refuses a build
/// configured with it). tools/check_no_test_seams.py proves the shipped
/// binaries carry none of the names.
///
/// Always pass a string literal: in a release build the argument is discarded
/// unevaluated, before it can become data in the object file.
#if defined(SPECTR_ENABLE_TEST_SEAMS)
#define SPECTR_TEST_ENV(name) (::std::getenv(name))
#else
#define SPECTR_TEST_ENV(name) (static_cast<const char*>(nullptr))
#endif
