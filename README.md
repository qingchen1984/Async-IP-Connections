# Async IP Connections

Multiplatform C library for synchronous and asynchronous [IP](https://en.wikipedia.org/wiki/Internet_Protocol) (v4 or v6) communications, using the same interface for [TCP](https://en.wikipedia.org/wiki/Transmission_Control_Protocol) and [UDP](https://en.wikipedia.org/wiki/User_Datagram_Protocol) transports.

This library is more intended for usage in systems not supported by more feature-complete and well tested libraries, such as [ZeroMQ](http://zeromq.org/) and [nanomsg](http://nanomsg.org/). We recommend you take a look at those or similar projects before.

### Building

This library depends on [Simple Multithreading](https://github.com/LabDin/Simple-Multithreading) project, which is added as a [git submodule](https://git-scm.com/docs/git-submodule).

It also uses the available [IP sockets](https://en.wikipedia.org/wiki/Network_socket) implementation, which provided by all supported platforms (**Windows** and **Linux**, for now).

For building this library e.g. with [GCC](https://gcc.gnu.org/) as a shared object, compile from terminal with (from root directory):

>$ gcc async_ip_network.c ip_network.c threading/threads.c threading/thread_safe_maps.c threading/thread_safe_queues.c -Ithreading -shared -fPIC -o ip.so

For detecting socket input more efficiently, this library uses [poll](http://man7.org/linux/man-pages/man2/poll.2.html) system call. In older host systems, where **poll** is not available, you can also compile with:

>$ gcc async_ip_network.c ip_network.c threading/threads.c threading/thread_safe_maps.c threading/thread_safe_queues.c -DIP_NETWORK_LEGACY -Ithreading -shared -fPIC -o ip.so

Which will use [select](http://man7.org/linux/man-pages/man2/select.2.html), slower but more supported.

### Documentation

Descriptions of how the functions and data structures work are available at the [Doxygen](http://www.stack.nl/~dimitri/doxygen/index.html)-generated [documentation pages](https://labdin.github.io/Async-IP-Connections/files.html)
