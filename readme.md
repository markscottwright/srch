A Standard C++ implementation of ACK
====================================

Ack is an amazing program, but the dependency on Perl is annoying, especially on
Windows.  C++ 14 should have everything we need to implement Ack in standard and
portable C++.

Although, it looks like <filesystem> is C++ 17, not C++ 14.  It appears that
recent gcc release have it, and Visual Studio 13 does.

TODO
----

*   catch.hpp unit tests
*   Documentation
*   Complete language types
*   Config from rc, environment variables
*   Change arguments from multiple patterns to <pattern> <directories>
*   CMake build instead of .bat file
*   Add --type= options in addition to --type option
