<img src="/smax-clib/resources/CfA-logo.png" alt="CfA logo" width="400" height="67" align="right">
<br clear="all">
A free C/C++ client library and toolkit for the 
[SMA Exchange (SMA-X)](https://docs.google.com/document/d/1eYbWDClKkV7JnJxv4MxuNBNV47dFXuUWu7C4Ve_YTf0/edit?usp=sharing) 
structured real-time database

 - [API documentation](https://smithsonian.github.io/smax-clib/apidoc/html/files.html)
 - [Project pages](https://smithsonian.github.io/smax-clib) on github.io

Author: Attila Kovacs

Updated for version 1.0 and later releases.


## Table of Contents

 - [Introduction](#smax-introduction)
 - [Prerequisites](#smax-prerequisites)
 - [Building the SMA-X C library](#building-smax)
 - [Linking your application against `smax-clib`](#smax-linking)
 - [Command-line tools](#command-line-tools)
 - [Initial configuration](#smax-configuration)
 - [Connecting to / disconnecting from SMA-X](#smax-connecting)
 - [Sharing and pulling data](#sharing-and-pulling)
 - [Lazy pulling (high-frequency queries)](#lazy-pulling)
 - [Pipelined pulls (high volume queries)](#pipelined-pulls)
 - [Custom update handling](#update-handling)
 - [Remote program control via SMA-X](#remote-control)
 - [Program status / error messages via SMA-X](#status-messages)
 - [Optional metadata](#optional-metadata)
 - [Error handling](#smax-error-handling)
 - [Debug support](#smax-debug-support)
 - [Future plans](#smax-future-plans)

------------------------------------------------------------------------------

<a name="smax-introduction"></a>
## Introduction

The [SMA Exchange (SMA-X)](https://docs.google.com/document/d/1eYbWDClKkV7JnJxv4MxuNBNV47dFXuUWu7C4Ve_YTf0/edit?usp=sharing) 
is a free, high performance, and versatile real-time data sharing platform for distributed software systems. It is 
built around a central Redis (or equivalent) database, and provides efficient atomic access to structured data, 
including specific branches and/or leaf nodes, with associated metadata. SMA-X was developed at the Submillimeter 
Array (SMA) observatory, where we use it to share real-time data among hundreds of computers and nearly a thousand 
individual programs. The __smax-clib__ library is free to use, in any way you like, without licensing restrictions.

SMA-X consists of a set of server-side [LUA](https://lua.org/) scripts that run on [Redis](https://redis.io) (or one 
of its forks / clones such as [Valkey](https://valkey.io) or [Dragonfly](https://dragonfly.io)); a set of libraries to 
interface client applications; and a set of command-line tools built with them. Currently we provide client libraries 
for C/C++ (C99) and Python 3. We may provide Java and/or Rust client libraries too in the future. 

There are no official releases of __smax-clib__ yet. An initial 1.0.0 release is expected early/mid 2025. 
Before then the API may undergo slight changes and tweaks. Use the repository as is at your own risk for now.

### Related links

 - [SMA-X specification](https://docs.google.com/document/d/1eYbWDClKkV7JnJxv4MxuNBNV47dFXuUWu7C4Ve_YTf0/edit?usp=sharing)
 - [Smithsonian/smax-python](https://github.com/Smithsonian/smax-python) an alternative library for Python 3.
 - [Smithsonian/smax-postgres](https://github.com/Smithsonian/smax-postgres) for creating a time-series history of 
   SMA-X in a __PostgreSQL__ database.


------------------------------------------------------------------------------

<a name="smax-prerequisites"></a>
## Prerequisites

The SMA-X C/C++ library has a build and runtime dependency on the __xchange__ and __RedisX__ libraries also available
at the Smithsonian Github repositories:

 - [Smithsonian/xchange](https://github.com/Smithsonian/xchange)
 - [Smithsonian/redisx](https://github.com/Smithsonian/redisx)

Additionally, to configure your Redis (or Valkey / Dragonfly) servers for SMA-X, you will need the 
[Smithsonian/smax-server](https://github.com/Smithsonian/smax-server) repo also.


------------------------------------------------------------------------------

<a name="building-smax"></a>
## Building the SMA-X C library

The __smax-clib__ library can be built either as a shared (`libsmax.so[.1]`) and as a static (`libsmax.a`) library, 
depending on what suits your needs best.

You can configure the build, either by editing `config.mk` or else by defining the relevant environment variables 
prior to invoking `make`. The following build variables can be configured:
   
 - `CC`: The C compiler to use (default: `gcc`).

 - `CPPFLAGS`: C preprocessor flags, such as externally defined compiler constants.
 
 - `CFLAGS`: Flags to pass onto the C compiler (default: `-g -Os -Wall`). Note, `-Iinclude` will be added 
   automatically.
 
 - `CSTANDARD`: Optionally, specify the C standard to compile for, e.g. `c99` to compile for the C99 standard. If
   defined then `-std=$(CSTANDARD)` is added to `CFLAGS` automatically.
   
 - `WEXTRA`: If set to 1, `-Wextra` is added to `CFLAGS` automatically.

 - `FORTIFY`: If set it will set the `_FORTIFY_SOURCE` macro to the specified value (`gcc` supports values 1 
   through 3). It affords varying levels of extra compile time / runtime checks.

 - `LDFLAGS`: Extra linker flags (default: _not set_). Note, `-lm  -lpthread -lredisx -lxchange` will be added 
   automatically.

 - `CHECKEXTRA`: Extra options to pass to `cppcheck` for the `make check` target

 - `DOXYGEN`: Specify the `doxygen` executable to use for generating documentation. If not set (default), `make` will
   use `doxygen` in your `PATH` (if any). You can also set it to `none` to disable document generation and the
   checking for a usable `doxygen` version entirely.

 - `XCHANGE`: If the [Smithsonian/xchange](https://github.com/Smithsonian/xchange) library is not installed on your
   system (e.g. under `/usr`) set `XCHANGE` to where the distribution can be found. The build will expect to find 
   `xchange.h` under `$(XCHANGE)/include` and `libxchange.so` / `libxchange.a` under `$(XCHANGE)/lib` or else in the 
   default `LD_LIBRARY_PATH`.
   
 - `REDISX`: If the [Smithsonian/redisx](https://github.com/Smithsonian/redisx) library is not installed on your
   system (e.g. under `/usr`) set `REDISX` to where the distribution can be found. The build will expect to find 
   `redisx.h` under `$(REDISX)/include` and `libredisx.so` / `libredisx.a` under `$(REDISX)/lib` or else in the 
   default `LD_LIBRARY_PATH`.
   
 - `STATICLINK`: Set to 1 to prefer linking tools statically against `libsmax.a`. (It may still link dynamically if 
   `libsmax.so` is also available.
 
After configuring, you can simply run `make`, which will build the `shared` (`lib/libsmax.so[.1]`) and `static` 
(`lib/libsmax.a`) libraries, local HTML documentation (provided `doxygen` is available), and performs static
analysis via the `check` target. Or, you may build just the components you are interested in, by specifying the
desired `make` target(s). (You can use `make help` to get a summary of the available `make` targets). 

After building the library you can install the above components to the desired locations on your system. For a 
system-wide install you may simply run:

```bash
  $ sudo make install
```

Or, to install in some other locations, you may set a prefix and/or `DESTDIR`. For example, to install under `/opt` 
instead, you can:

```bash
  $ sudo make prefix="/opt" install
```

Or, to stage the installation (to `/usr`) under a 'build root':

```bash
  $ make DESTDIR="/tmp/stage" install
```

-----------------------------------------------------------------------------

<a name="smax-linking"></a>
## Linking your application against `smax-clib`

Provided you have installed the shared (`libsmax.so`, `libredisx.so`, and `libxchange.so`) or static (`libsmax.a`, 
`libredisx.a`, and `libxchange.so`) libraries in a location that is in your `LD_LIBRARY_PATH` (e.g. in `/usr/lib` or 
`/usr/local/lib`) you can simply link your program using the  `-lsmax -lredisx -lxchange` flags. Your `Makefile` may 
look like: 

```make
myprog: ...
	$(CC) -o $@ $^ $(LDFLAGS) -lsmax -lredisx -lxchange 
```

(Or, you might simply add `-lsmax -lredisx -lxchange` to `LDFLAGS` and use a more standard recipe.) And, in if you 
installed the __smax-clib__, __RedisX__, and/or __xchange__ libraries elsewhere, you can simply add their location(s) 
to `LD_LIBRARY_PATH` prior to linking.

------------------------------------------------------------------------------

<a name="command-line-tools"></a>
## Command-line tools

The __smax-clib__ library provides two basic command-line tools:

 - `smaxValue` -- for exploring the contents of SMA-X, including associated metadata, and
 - `smaxWrite` -- for putting data into SMA-X from the shell.
 
The tools can be compiled with `make tools`. Both tools can be run with the `--help` option for a simple help screen on
usage. E.g.:

```bash
  $ smaxValue --help
```

These command-line tools provide a simple means to interact with SMA-X from the shell or a scripting language, such
as `bash`, or `perl` (also `python` though we recommend to use the native 
[Smithsonian/smax-python](https://github.com/Smithsonian/smax-python) library instead).

------------------------------------------------------------------------------

<a name="smax-configuration"></a>
## Initial configuration

Bu default, the library assumes that the Redis server name used for SMA-X is either stored in the environment variable
`SMAX_HOST` or is `smax` (e.g. you may assign `smax` to an IP address in `/etc/hosts`), and that the Redis is on 
the default port 6379/tcp. However, you can configure to use a specific host and/or an alternative Redis port number 
also, e.g.:

```c
  smaxSetServer("my-smax.example.com", 7033);
```

Also, while SMA-X will normally run on database index 0, you can also specify a different database number to use. E.g.:

```c
  smaxSetDB(3);
```

(Note, you can switch the database later also, but beware that if you have an active subscription client open, you cannot
switch that client until the subscriptions are terminated.)

You can also set up the authentication credentials for using the SMA-X database on the Redis server:

```c
  smaxSetUser("johndoe");
  smaxSetPassword(mySecretPassword);
```

By default __smax_clib__ will connect both an interactive and a pipeline (high-throughput) Redis client. However, if you
are planning to only use interactive mode (for setting an queries), you might not want to connect the pipeline client
at all:

```c
  smaxSetPipelined(FALSE);
```

And finally, you can select the option to automatically try reconnect to the SMA-X server in case of lost connection or
network errors (and keep track of changes locally until then):

```c
  smaxSetResilient(TRUE);
```

### TLS configuration

You can also use SMA-X with a TLS encrypted connection. (We don't recommend using TLS with SMA-X in general though, 
since it may adversely affect the performance / throughput of the database.) When enabled, Redis normally uses mutual 
TLS (mTLS), but it may be configured otherwise also. Depending on the server configuration, and the level of security 
required, you may configure some or all of the following options:

```c
  int status;

  // Use TLS with the specified CA certificate file and path
  status = smaxSetTLS("path/to/certificates", "ca.crt");
  if(status) {
    // Oops, the CA certificate is not accessible...
    ...
  }
  
  // (optional) If servers requires mutual TLS, you will need to provide 
  // a certificate and private key also
  status = smaxSetMutualTLS("path/to/redis.crt", "path/to/redis.key");
  if(status) {
    // Oops, the certificate or key file is not accessible...
    ...
  }

  // (optional) Skip verification of the certificate (insecure!)
  smaxSetTLSVerify(FALSE);

  // (optional) Set server name for SNI
  smaxSetTLSServerName("my.smax-server.com");

  // (optional) Set ciphers to use (TLSv1.2 and earlier)
  smaxSetTLSCiphers("HIGH:!aNULL:!kRSA:!PSK:!SRP:!MD5:!RC4");
  
  // (optional) Set cipher suites to use (TLSv1.3 and later)
  smaxSetTLSCipherSuites("ECDHE-RSA-AES256-GCM-SHA384:TLS_AES_256_GCM_SHA384");
  
  // (optional) Set parameters for DH-based ciphers
  status = smaxSetDHCypherParams("path/to/redis.dh");
  if(status) {
    // Oops, the parameter file is not accessible...
    ...
  }
```

### Reconfiguration

The SMA-X configuration is activated at the time of connection (see below), after which it persists, through 
successive connections also. That means, that once you have connected to the server, you cannot alter the 
configuration prior to another connection attempt, unless you call `smaxReset()` first while disconnected. 
`smaxReset()` will discard the currently configured Redis instance, so the next connection will create a new one, with 
the current configuration.


------------------------------------------------------------------------------

<a name="smax-connecting"></a>
## Connecting to / disconnecting from SMA-X 

Once you have configured the connection parameters, you can connect to the configured (or default) server by:

```c
  int status = smaxConnect();
  if(status < 0) {
     // Oops, we could not connect to the server
     ...
  }
```

And, when you are done, you should disconnect with:

```c
  smaxDisconnect();
```

<a name="smax-connection-hooks"></a>
### Connection / disconnection hooks

The user of the __smax-clib__ library might want to know when connections to the SMA-X server are established, or when 
disconnections happen, and may want to perform some configuration or clean-up accordingly. For this reason, the 
library provides support for connection hooks -- that is custom functions that are called in the even of connecting 
to or disconnecting from a Redis server.

Here is an example of a connection hook, which simply prints a message about the connection to the console.

```c
  void my_connect_hook() {
     printf("Connected to SMA-X server\n");
  }
```

And, it can be activated prior to the `smaxConnect()` call.

```c
  smaxAddConnectHook(my_connect_hook);
```

The same goes for disconnect hooks, using `smaxAddDisconnectHook()` instead.



------------------------------------------------------------------------------

<a name="sharing-and-pulling"></a>
## Sharing and pulling data

 - [The basics](#smax-basics)
 - [Standard metadata](#metadata)
 - [Flexible types and sizes](#flexible-types-and-sizes)
 - [Scalar quantities](#smax-scalars)
 - [Arrays](#smax-arrays)
 - [Structures / substructures](#smax-structures)

<a name="smax-basics"></a>
### The basics

For SMA-X we use the terms sharing and pulling, instead of the more generic get/set terminology. The intention is to
make programmers conscious of the fact that the transactions are not some local access to memory, but that they 
involve networking, and as such may be subject to unpredictable latencies, network outages, or other errors.

At the lowest level, the library provides two functions accordingly: `smaxShare()` and `smaxPull()`, either to send
local data to store in SMA-X, or to retrieve data from SMA-X for local use, respectively. There are higher-level 
functions too, which build on these, providing a simpler API for specific data types, or extra features, such as 
lazy or pipelined pulls. These will be discussed in the sections below. But, before that, let's look into the basics
of how data is handled between you machine local data that you use in your C/C++ application and its 
machine-independent representation in SMA-X.

Here is an example for a generic sharing of a `double[]` array from C/C++:

```c
  double data[8] = ...   // your local data

  // Share (send) this data to SMA-X as "system:subsystem:some_data"
  int status = smaxShare("system:subsystem", "some_data", data, X_DOUBLE, 8);
  if(status < 0) {
    // Ooops, that did not work...
    ...
  }
  
```

Pulling data back from SMA-X works similarly, e.g.:

```c
  double data[4][2] = ...   // buffer in which we will receive data
  XMeta meta;               // (optional) metadata we can obtain 
  
  // Retrieve 'data' from the SMA-X "system:subsystem:some_data", as 8 doubles
  int status = smaxPull("system:subsystem", "some_data", X_DOUBLE, 8, data, &meta);
  if(status < 0) {
    // Oops, something went wrong...
    return;
  }

```

The metadata argument is optional, and can be `NULL` if not required.


<a name="metadata"></a>
### Standard metadata

For every variable (structure or leaf node) in SMA-X there is also a set of essential metadata that is stored in the
Redis database, which describe the data themselves, such as the native type (at the origin); the size as stored in 
Redis; the array dimension and shape; the host (and program) which provided the data; the time it was last updated;
and a serial number.

The user of the library has the option to retrieve metadata together with the actual data, and thus gain access to
this information. The header file `smax.h` defines the `Xmeta` type as:


```c
  typedef struct XMeta {
    int status;                       // Error code or X_SUCCESS.
    XType storeType;                  // Type of variable as stored.
    int storeDim;                     // Dimensionality of the data as stored.
    int storeSizes[X_MAX_DIMS];       // Sizes along each dimension of the data as stored.
    int storeBytes;                   // Total number of bytes stored.
    char origin[SMAX_ORIGIN_LENGTH];  // Host name that last modified.
    struct timespec timestamp;        // Timestamp of the last modification.
    int serial;                       // Number of times the variable was updated.
  } XMeta;
```

<a name="flexible-types-and-sizes"></a>
### Flexible types and sizes

One nifty feature of the library is that as a consumer you need not be too concerned about what type or size of data
the producer provides. The program that produces the data may sometimes change, for example from writing 32-bit 
floating point types to a 64-bit floating point types. Also while it produced data for say 10 units before (as an 
array of 10), now it might report for just 9, or perhaps it now reports for 12. 

The point is that if your consumer application was written to expect ten 32-bit floating floating point values, it can 
get that even if the producer changed the exact type or element count since you have written your client. The library 
will simply apply the necessary type conversion automatically, and then truncate, or else pad (with zeroes), the data 
as necessary to get what you want.

The type conversion can be both widening or narrowing. Strings and numerical values can be converted to one another 
through the expected string representation of numbers and vice versa. Boolean `true` values are treated equivalently 
to a numerical value of 1 and all non-zero numerical values will convert to boolean `true`.

And, if you are concerned about the actual type or size (or shape) of the data stored, you have the option to inspect
the metadata, and make decisions based on it. Otherwise, the library will just take care of giving you the available 
data in the format you expect.


<a name="smax-scalars"></a>
### Scalar quantities

Often enough we deal with scalar quantities (not arrays), such as a single number, boolean value, or a
string (yes, we treat strings i.e., `char *`, as a scalar too!).

Here are some examples of sharing scalars to SMA-X. Easy-peasy:

```c
  int status;

  // Put a boolean value into SMA-X
  status = smaxShareBoolean("system:subsystem", "is_online", TRUE);

  // Put an integer value into SMA-X
  status = smaxShareInt("system:subsystem", "some_int_value", 1012);
  
  // Put a floating-point value into SMA-X
  status = smaxShareDouble("system:subsystem", "some_value", -3.4032e-11);

  // Put a string value into SMA-X
  status = smaxShareString("system:subsystem", "name", "blah-blah");
```

Or pulling them from SMA-X:

```c
  // Retrieve "system:subsystem:is_online" as a boolean, defaulting to FALSE
  boolean isTrue = smaxPullBoolean("system:subsystem", "is_online", FALSE);

  // Retrieve "system:subsystem:some_int_value" as an int, with a default value of -1
  int c = smaxPullInt("system:subsystem", "some_int_value", -1);
  
  // Retrieve "system:subsystem:some_value" as a double (or else NAN if cannot).
  double value = smaxPullDouble("system:subsystem", "some_value");
  if(!isnan(value)) {    // check for NAN if need be
    ...
  }

  // Retrieve "system:subsystem:name" as a 0-terminated C string (or NULL if cannot).
  char *str = smaxPullDouble("system:subsystem", "name");
  if(str != NULL) {      // check for NULL
    ...
  }
 
  ...
  
  // Once the pulled string is no longer needed, destroy it.
  free(str)

```

<a name="smax-arrays"></a>
### Arrays

The generic `smaxShare()` function readily handles 1D arrays, and the `smaxPull()` handles native (monolithic) arrays
of all types (e.g. `double[][]`, or `boolean[][][]`). However, you may want to share multidimensional arrays, noting 
their specific shapes, or else pull array data for which you may not know in advance what size (or shape) of the 
storage needed locally for the values stored in SMA-X for some variable. For these reasons, the library provides a set 
of functions to make the handling of arrays a simpler too.

Let's begin with sharing multi-dimensional arrays. Instead of `smaxShare()`, you can use `smaxShareArray()` which 
allows you to define the multi-dimensional shape of the data beyond just the number of elements stored. E.g.:

```c
  float data[4][2] = ...    // Your local 2D data array
  int shape[] = { 4, 2 };   // The array shape as stored in SMA-X
  
  int status = smaxShareArray("system:subsystem", "my_2d_array", data, X_FLOAT, 2, shape);
  if (status < 0) {
    // Oops, did not work...
    ...
  }
```

Note, that the dimensions of how the data is stored in SMA-X is determined solely by the '2' dimensions specified
as the 4th argument and the corresponding 2 elements in the `shape` array. The `data` could have been any pointer
to an array of floats containing at least the required number of element (8 in the example above).

For 1D arrays, you have some convenience methods for sharing specific types. These can be convenient because they 
eliminate one potential source of bugs, where the type argument to `smaxShare()` does not match the pointer type of 
the data. Using, say, `smaxShareFloats()` instead to share a 1D floating-point array instead of the generic 
`smaxShare()` will allow the compiler to check and warn you if the data array is not the `float *` type. E.g.:

```c
  float *data = ...  // pointer to a one or higher-dimensional C array of floats.

  // Send N elements from data to SMA-X as "system:subsystem:my_array"
  int status = smaxShareFloats("system:subsystem", "my_array", data, N);
  if (status < 0) {
    // Oops, did not work...
    ...
  }
```

Similar functions are available for every built-in type (primitives, plus strings and booleans). For pulling arrays 
without knowing a-priori the element count or shape, there are also convenience functions, such as:

```c
  XMeta meta;    // (optional) we'll return the metadata in this
  int status;	 // we'll return the status in this

  // Get whatever data is in "system:subsystem:my_array" as doubles.
  double *data = smaxPullDoubles("system:subsystem", "my_array", &meta, &status);
  if(status < 0) {
    // Oops, we got an error
  } 
  if(data == NULL) {
    // Oops the data is NULL
  }
  
  ...
  
  // When done using the data we obtained, destroy it.
  free(data);
```

As illustrated, the above will return a dynamically allocated array with the required size to hold the data, and the
size and shape of the data is returned in the metadata that was also supplied with the call. After using the returned
data (and ensuring that it is not `NULL`), you should always call `free()` on it to avoid memory leaks in your
application.

<a name="smax-structures"></a>
### Structures / substructures...

You can share entire data structures, represented by an appropriate `XStructure` type (see the __xchange__ library for 
a description and usage):

```c
  XStructure s = ...   // The structured data you have prepared locally.
  
  int status = smaxShareStruct("system:subsystem", &s);
  if(status < 0) {
    // Oops, something did not work
    ...
  }
```

Or, you can read a structure, including all embedded substructures in it, with `smaxPullStruct()`:


```c
  XMeta meta;
  int nElements;
  
  XStructure *s = smaxPullStruct("system", "subsystem", &meta, &n);
  if(n < 0) {
    // Oops there was an error...
    return;
  }
  
  double value = smaxGetDoubleField(s, "some_value", 0.0);
  
  ...
  
  // Once the pulled structure is no longer needed, destroy it...
  xDestroyStruct(s);
```

Note, that the structure returned by `smaxPullStruct()` is in the serialized format of SMA-X. That is, all leaf nodes
are stored as strings, just as they appear in the Redis database. Hence, we used the `smaxGet...Field()` methods above 
to deserialize the leaf nodes as needed on demand. If you want to use the methods of __xchange__ to access the 
structure, you will need to convert to binary format first, using `smax2xStruct(XStructure *)`.

Note also, that pulling large structures can be an expensive operation on the Redis server, and may block the server 
for longer than usual periods, causing latencies for other programs that use SMA-X. It's best to use this method for 
smallish structures only (with, say, a hundred or so or fewer leaf nodes).


------------------------------------------------------------------------------

<a name="lazy-pulling"></a>
## Lazy pulling (high-frequency queries)
  
What happens if you need the data frequently? Do you pound on the database at some high-frequency? No, you probably 
no not want to do that, especially if the data you need is not necessarily changing fast. There is no point on wasting
network bandwidth only to return the same values again and again. This is where 'lazy' pulling excels.

From the caller's perspective lazy pulling works just like regular SMA-X pulls, e.g.:

```c
  int data[10][4][2];
  int sizes[] = { 10, 4, 2 };
  XMeta meta;

  int status = smaxLazyPull("some_table", "some_data", X_INT, 3, sizes, data, &meta);
```

or

```c
  int status = smaxLazyPullDouble("some_table", "some_var");
```

But, under the hood, it does something different. The first time a new variable is lazy pulled it is fetched from the
Redis database just like a regular pull. But, it also will cache the value, and watch for update notifications from
the SMA-X server. Thus, as long as no update notification is received, successive calls will simply return the locally
cached value. This can save big on network usage, and also provides orders of magnitude faster access so long as the 
variable remains unchanged.

When the variable is updated in SMA-X, our client library will be notified, and one of two things can happen:

 1. it invalidates the cache, so that the next lazy pull will again work just like a regular pull, fetching the 
    updated value from SMA-X on demand. And again the library will cache that value and watch for notifications for 
    the next update. Or,
    
 2. it will trigger a background process to update the cached value in the background with a pipelined 
    (high-throughput) pull. However, until the new value is actually fetched, it will return the previously cached
    value promptly.
    
The choice between the two is yours, and you can control which suits your need best. The default behavior for lazy pulls
is (1), but you may call `smaxCache()` after the first pull of a variable, to indicate that you want to enable 
background cache updates (2) for it. The advantage of (1) is that it will never serve you outdated data even if there
are significant network latencies -- but you may have to wait a little to fetch updates. On the other hand (2) will
always provide a recent value with effectively no latency, but this value may be outdated if there are delays on the
network updating the cache. The difference is typically at the micro-seconds level on a local LAN. However, (2) may 
be preferable when you need to access SMA-X data from timing critical code blocks, where it is more important to ensure
that the value is returned quickly, rather than whether it is a millisecond too old or not.

You can also explicitly select the second behavior by using `smaxGetCached()` instead of `smaxLazyPull()`:

```c
  int status = smaxGetCached("some_table", "some_data", X_INT, 3, sizes, data, &meta);
```

In either case, when you are done using lazy variables, you should let the library know that it no longer needs to watch
updates for these, by calling either `smaxLazyEnd()` on specific variables, or else `smaxLazyFlush()` to stop watching
updates for all lazy variables. (A successive lazy pull will automatically start watching for updates again, in case you
wish to re-enable).

```c
  // Lazy pull a bunch of data (typically in a loop).
  for(...) {
    smaxLazyPull("some_table", "some_var", ...);
    smaxLaxyPull(...);
    ...
  }
  
  // Once we do not need "some_table:some_var" any more:
  smaxLazyEnd("some_table", "some_var");
  
  ...
  
  // And to stop lazy accessing all
  smaxLazyFlush();
```


------------------------------------------------------------------------------

<a name="pipelined-pulls"></a>
## Pipelined pulling (high volume queries)

 - [Synchronization points and waiting](#lazy-synchronization)
 - [Callbacks](#lazy-callbacks)
 - [Finishing up](#lazy-finish)

The regular pulling of data from SMA-X requires a separate round-trip for each and every request. That is, successive 
pulls are sent only after the responses from the prior pull has been received. A lot of the time is spent on waiting 
for responses to come back. With round trip times in the 100 &mu;s range, this means that this method of fetching data
from SMA-X is suitable for obtaining at most a a few thousand values per second.

However, sometimes you want to get access to a large number of values faster. This is what pipelined pulling is for.
In pipelined mode, a batch of pull requests are sent to the SMA-X Redis server in quick succession, without waiting
for responses. The values, when received are processed by a dedicated background thread. And, the user has an option
of either waiting until all data is collected, or asking for as callback when the data is ready. 

Again it works similarly to the basic pulling, except that you submit your pull request to a queue with 
`smaxQueue()`. For example:

```c
  double d;	// A value we will fill
  XMeta meta;   // (optional) metadata to fill (for the above value).

  int status = smaxQueue("some_table", "some_var", X_DOUBLE, 1, &d, &meta);
```

Pipelined (batched) pulls have dramatic effects on performance. Rather than being limited by round-trip times, you will
be limited by the performance of the Redis server itself (or the network bandwidth on some older infrastructure). As 
such, instead of thousand of queries per second, you can pull 2-3 orders of magnitude more in a given time, with 
hundreds of thousands of pull per second this way.

<a name="lazy-synchronization"></a>
### Synchronization points and waiting

After you have submitted a batch of pull request to the queue, you can create a synchronization point as:

```c
  XSyncPoint *syncPoint = smaxCreateSyncPoint();
```

A synchronization point is a marker in the queue that we can wait on. After the synchronization point is created, you
can submit more pull request to the same queue (e.g. for another processing block), or do some other things for a bit
(since it will take at least some microseconds before the data is ready). Then, when ready you can wait on the 
specific synchronization point to ensure that data submitted prior to its creation is delivered from SMA-X:

```c
  // Wait for data submitted prior to syncPoint to be ready, or time out after 1000 ms.
  int status = smaxSync(syncPoint, 1000);
  
  // Destroy the synchronization point if we no longer need it.
  xDestroySyncPoint(syncPoint);
  
  // Check return status...
  if(status == X_TIMEOUT) {
    // We timed out
    ...
  }
  else if(status < 0) {
    // Some other error
    ...
  }
```

<a name="lazy-callbacks"></a>
### Callbacks

The alternative to synchronization points and waiting, is to provide a callback function, which will process your data
as soon as it is available, e.g.:

```c
  void my_pull_processor(void *arg) {
     // Say, we expect a string tag passed along to identify what we need to process...
     char *tag = (char *) arg;
     
     // Do what we need to do...
     ...
  }
```

Then submit this callback routine to the queue after the set of variables it requires with:

```c
  // We'll call my_pull_processor, with the argument "some_tag", when prior data has arrived.
  smaxQueueCallback(my_pull_processor, "some_tag");
```

<a name="lazy-finish"></a>
### Finishing up

If you might still have some pending pipelined pulls that have not received responses yet, you may want to wait until
all previously submitted requests have been collected. You can do that with:

```c
  // Wait for up to 3000 ms for all pipelined pulls to collect responses from SMA-X.
  int status = smaxWaitQueueComplete(3000);
  
  // Check return status...
  if(status == X_TIMEOUT) {
    // We timed out
    ...
  }
  else if(status < 0) {
    // Some other error
    ...
  }
```

------------------------------------------------------------------------------

<a name="update-handling"></a>
## Custom update handling

 - [Monitoring updates](#monitoring-updates)
 - [Waiting for updates](#waiting-for-updates)
 - [Update callbacks](#update-callbacks)


<a name="monitoring-updates"></a>
### Monitoring updates

The LUA scripts that define SMA-X interface on the Redis server send out PUB/SUB notifications for every variable on 
their own dedicate PUB/SUB channel whenever the variable is updated. By default, lazy access methods subscribe to 
these messages and use them to determine when to invalidate the cache and fetch new values from the database again. 
However, you may subscribe and use these messages outside of the lazy update routines also. The only thing you need to 
pay attention to is not to unsubscribe from update notifications for those variables that have multiple active monitors
(including lazy updates).

One common use case is that you want to execute some code in your application when some value changes in SMA-X. You
can do that easily, and have two choices on how you want to trigger the code execution: (1) you can block execution of
your current thread until an update notification is received for one of the variables (or patterns) of interest to 
which you have subscribed, or (2) you can let __smax_clib__ call a designated function of your application when such 
an update notification is captured. We'll cover these two cases separately below.

However, in either case you will have to subscribe to the variable(s) or pattern(s) of interest with `smaxSubscribe()`
before updates can be processed, e.g.

```c
  int status = smaxSubscribe("some_table", "watch_point_variable");
  if (status < 0) {
    // Ooops something did not go to plan
    ...
  }
```

and/or pattern(s):

```c
  int status = smaxSubscribe("*", "b[a-c]?");
  if (status < 0) {
    // Ooops something did not go to plan
    ...
  }
```

You can subscribe to any number of variables or patterns in this way. __smax_clib__ will receive and process 
notifications for all of them. (So beware of creating unnecessary network traffic.)

<a name="waiting-for-updates"></a>
### Waiting for updates

The first option for executing code conditional on some variable update is to block execution in the current thread
and wait until the variable(s) of interest change(s) (or until some timeout limit is reached). There is a group of
functions `smaxWaitOn...()` that do exactly that, provided you have already subscribed to receiving updates for
the desired variables / patterns. For example:

```c
  // Wait for foo:bar to update, or wait at most 500 ms
  int status = smaxWaitOnSubscribedVar("foo", "bar", 500, NULL);
  if (status == X_TIMEDOUT) {
    // Wait timed out, maybe we want to try again, or do something else...
    ...
  }
  else if (status < 0) {
    // Oh no, some other error happened.
    ...
  }
  ...

```

Similar methods allow you to wait for updates on any subscribed variable in selected tables, or the update of select 
variables in all subscribed tables, or any of the subscribed variables to change for the wait to end normally (with 
return value 0). 

The last parameter (`NULL` in the above example) allows to pass a pointer to a POSIX sempahore, which can be used for 
gating another thread, which might also want exclusive access to the SMA-X notifications, but we do not want it to
accidentally block entering the wait in a timely manner.


<a name="update-callbacks"></a>
### Update callbacks

Sometimes, you don't want to block execution, but you want to make sure some code executes when the a variable
or variables of interest get updated. For such cases you can designate your own `RedisSubscriberCall` callback 
function, e.g.:

```c
  void my_update_processor(const char *pattern, const char *channel, const char *msg, long len) {
     const char *varID;  // The variable aggregate ID. 
  
     // check that it's a SMA-X update -- channel should begin with SMAX_UPDATE
     if (strncmp(channel, SMAX_UPDATE, SMAX_UPDATE_LENGTH) != 0) {
       // Not an SMA-X variable update. It's not what we expected.
       return;
     }
     
     id = &channel[SMAX_UPDATE_LENGTH];
     // Now we can check the ID to see which of the variables updated, and act accordingly.
     ...
  }
```

Once you have defined your callback function, you can activate it with `smaxAddSubscriber()`, e.g.:

```c
  // Call my_update_processor on updates for any subscribed variable whose table name starts with "watch_table_"
  int status = smaxAddSubscriber("watch_table_", my_update_processor);
  if (status < 0) {
    // Did not go to plan.
    ...
  }
```
  
When you no longer need to process such updates, you can simply remove the function from being called via 
`smaxRemoveSubscriber()`, and if you are absolutely sure that no other part of your code needs the subscription(s)
that could trigger it, you can also unsubscribe from the trigger variables/pattern to eliminate unnecessary network 
traffic.

One word of caution on callbacks is that they are expected to:

 - execute quickly
 - never block for prolonged periods. (It may be OK to wait briefly on a mutex, provided nothing else can hog that 
   mutex for prolonged periods).
  
If the above two conditions cannot be guaranteed, it's best practice for your callback to place a copy of the 
callback information on a queue, and then spawn or notify a separate thread to process the information in the 
background, including discarding the copied data if it's no longer needed. Alternatively, you can launch a dedicated
processor thread early on, and inside it wait for the updates before executing some complex action. The choice is
yours.


------------------------------------------------------------------------------  

<a name="remote-control"></a>  
## Remote program control via SMA-X

 - [Server side](#server-side)
 - [Client side](#client-side)
 - [Complex remote control calls and return values](#complex-control)

It is possible to use SMA-X for remote control of programs on distributed systems. In effect, any client can set 
designated control variables / values. These variables are monitored by an appropriate server program, which acts to 
changes to the 'commanded' values accordingly, and report the result back in a related other SMA-X variable. The 
client thus can obtain confirmation from the response variable after it submits it requested 'command' variable.

<a name="server-side"></a>
### Server side

On the server side, you will need a function (of `SMAXControlFunction` type), which acts when some control variable 
changes. E.g.:

```c
  // the function will be called with the SMA-X hash table and control variable names 
  // which triggered the call, as well as an optional user-supplied pointer argument.
  int my_control_function(const char *table, const char *key, void *parg) {
    // Let's assume the pointer argument defines what variable we use for the response...
    const char *replyKey = (const char *) parg;
    
    // Let's say we expect an integer control value...
    // We'll pull it from SMA-X as such, or default to -1 if the value is not an integer.
    int value = smaxPullInt(table, key, -1);
  
    // We could do something with the requested value...
    ...
  
    // Finally we'll write the requested (or actual) value to the reply keyword to 
    // indicate completion.
    return smaxShareInt(table, replyKey, value);
  }
```

It is important to remember that each action taken on a control variable should the set the designated response
variable exactly once, and only when the action is completed, so that the calling client can use it to confirm 
the completed action. The same action action is free to set any number of other values in SMA-X before signaling 
completion, thus 'returning' further data to the caller, if needed. 

Next, all you have to do is specify what control variable to use this function with and what optional pointer
argument to pass on to it:

```c
  // We'll trigger the function whenever 'system:subsystem:control_value' changes.
  // And we'll send a response to 'actual_value' in the same hash table.
  int status = smaxSetControlFunction("system:subsystem", "control_value", my_control_function, "actual_value");
  if(status < 0) {
    // Oops, something went wrong...
    return -1;
  }
```

The above call will subscribe for updates to `system:subsystem:control_value` and will call `my_control_function` with
`actual_value` as the optional argument. You may change the function called later, or undefine it by calling 
`smaxSetControlFunction()` with `NULL` as the function pointer.

Clearly, the same processing function can be used with multiple control values, if convenient, or you may specify 
different control functions to every control value if it makes more sense for the implementation.

One thing to watch out for on the server-side implementation is that control functions are called asynchronously and 
immediately, each time the control variable updates. As such, a control function may be called while another, or
even the same one, is in the middle of performing its task for a prior 'command'. You should therefore use mutexes as 
necessary to prevent the concurrent execution of program controls as appropriate. E.g.

```c
  // A mutex to prevent concurrent execution of control calls...
  static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
 
  int my_control_function(const char *table, const char *key, void *parg) {
    // Ensure we are executing the control code exclusively, with regard to other calls
    // to the same function, and any other control function that locks the same mutex...    
    pthread_mutex_lock(&mutex);
    
    // Perform the program control
    ...
    
    // We are done. Let other control calls execute now...
    phtread_mutex_unlock(&mutex);
  
    return 0;
  }
```

<a name="client-side"></a>
### Client side

From the client side, you control the above server by setting `system:subsystem:control_value` to an appropriate new 
value, and then wait for the response in `system:subsystem:actual_value`. You can do that via `smaxControl()` or one 
of its type-specific variants. Since in the above server example, we use integer control value and reply, we'll use
`smaxControlInt()`:

```c
  int timeout = 5; // [s] max. time to wait for a response.

  // We'll set the control value to 42, and wait for a response for up to 5 seconds, 
  // or else return -1.
  int reply = smaxControlInt("system:subsystem", "control_value", 42, NULL, "actual_value", -1, timeout);
  if(reply != 42) {
    // Oops, no luck
    ...
  }
```

The `NULL` as the 3rd argument is a shorthand to indicate that we expect the reply in the same hash table in which we
set `control_value` (that is in `system:subsystem`). If we expect the response in some other location, we can specify
the appropriate table name instead of the `NULL` pointer in the example above.


<a name="complex-control"></a>
### Complex remote control calls and return values

As hinted earlier, the remote control via SMA-X relies on a single control variable, and a single response variable
provides confirmation of completion back to the client. This scheme does not preclude passing multiple values to the
server, or receiving multiple values as a response. A client may set a number of call parameters in SMA-X before 
triggering the action on them by the designated control variable. The server will not act on the parameters alone. 
Instead, only when the designated control (trigger) variable is set it proceeds to read all associated parameter 
values from SMA-X.

Similarly, the server may set a number of return parameters during its action, before finally setting the designated
response variable to indicate completion. For example, consider locking a local oscillator to a designated frequency
and sideband (two parameters). The client might do that by:

```c
  // Set the frequency and sideband parameters first...
  smaxShareDouble("system:lo", "lock_frequency", 230.5e9);
  smaxShareString("system:lo", "lock_sideband", "usb");
  
  // Then trigger the action on the remote server by setting "lock" to 1, waiting 
  // for confirmation in "is_locked", defaulting to -1 in case of failure.
  int reply = smaxControlInt("system:lo", "lock", 1, NULL, "is_locked", -1, timeout);
  
  if(reply == -1) {
    // Oops, something went wrong
    ...
  }
  else if(reply != 0) {
    // Read the actual values sent in response in "current_frequency" and "current_sideband"
    double freq = smaxPullDouble("system:lo", "current_frequency");
    char *sideband = smaxPullString("system:lo", "current_sideband");
 
    // Do whatever with the returned current values... 
    ...
    
    // Clean up
    if(sideband) free(sideband);
  }
  else {
    fprintf(stderr, "WARNING! LO failed to lock\n");
  }
```

And the server might process the request as:

```c
  int lock_function(const char *table, const char *key, void *parg) {
    double freq = smaxPullDouble("system:lo", "lock_frequency");
    char *sideband = smaxPullString("system:lo", "lock_sideband");
    
    // Perform the locking with the above parameters...
    ...
    
    // Write back the actual frequency and sideband
    smaxShareDouble("system:lo", "current_frequency", current_freq);
    smaxShareString("system:lo", "current_sideband", current_sb);
    
    // Clean up...
    if(sideband) free(sideband);
    
    // Finally indicate lock status and completion
    return smaxShareInt("system:lo", "is_locked", is_locked);
  }
```


------------------------------------------------------------------------------  

<a name="status-messages"></a>  
## Program status / error messages via SMA-X

 - [Broadcasting status messages from an application](#smax-broadcasting)
 - [Processing program messages](#smax-processing-messages)

SMA-X also provides a standard for reporting program status, warning, and error messages via the Redis PUB/SUB 
infrastructure. 
 
<a name="smax-broadcasting"></a>
### Broadcasting status messages from an application

Broadcasting program messages to SMA-X is very simple using a set of dedicated messaging functions by message
type. These are:

 | __smax_clib__ function                                     | Description                                |
 |------------------------------------------------------------|--------------------------------------------|
 | `smaxSendStatus(const char *msg, ...)`                     | sends a status message                     |
 | `smaxSendInfo(const char *msg, ...)`                       | sends an informational message             |
 | `smaxSendDetail(const char *msg, ...)`                     | sends optional status/information detail   |
 | `smaxSendDebug(const char *msg, ...)`                      | sends a debugging messages                 |
 | `smaxSendWarning(const char *msg, ...)`                    | sends a warning message                    |
 | `smaxSendError(const char *msg, ...)`                      | sends an error message                     |
 | `smaxSendProgress(double fraction, const char *msg, ...)`  | sends a progress update and message        |

All the above methods work like `printf()`, and can take additional parameters corresponding to the format specifiers
contained in the `msg` argument.

By default, the messages are sent under the canonical program name (i.e. set by `_progname` on GNU/Linux systems) 
that produced the message. You can override that, and define a custom sender ID for your status messages, by calling
`smaxSetMessageSenderID()` prior to broadcasting, e.g.:

```c
  // Set out sender ID to "my_program_id"
  smaxSetMessageSenderID("my_program_id");
  
  ...
  
  // Broadcast a warning message for "my_program_id"
  int status = smaxSendWarning("Something did not work" %s", explanation);
```

<a name="smax-processing-messages"></a>
### Processing program messages

On the receiving end, other applications can process such program messages, for a selection of hosts, programs, and 
message types. You need to prepare you message processor function(s) first, e.g.:

```c
  void my_message_processor(XMessage *m) {
    printf("Received %s message from %s: %s\n", m->type, m->prog, m->text);
  }
  
```

The processor function does not return any value, since it is called by a background thread, which does not check for
return status. The `XMessage` type, a pointer to which is the sole argument of the processor, is defined in `smax.h` 
as:


```c
  typedef struct {
    char *host;                   // Host where message originated from
    char *prog;                   // Originator program name
    char *type;                   // Message type, e.g. "info", "detail", "warning", "error"
    char *text;                   // Message body (with timestamp stripped).
    double timestamp;             // Message timestamp, if available (otherwise 0.0)
  } XMessage;
```

Once you have your message consumer function, you can set it to be called for messages from select hosts, programs, and/or
select message types, using `smaxAddMessageProcessor()`, e.g.:

```c
  // Will call my_message_procesor for all messages coming from "my_program_id" from all hosts.
  // The return ID number (if > 0) can be used later to uniquely identify the processor with the set of selection 
  // parameters it is used with. So make sure to keep it handy for later.
  int id = smaxAddMessageProcessor("*", "my_program_id", "*", my_message_processor);
  if (id < 0) {
    // Oops that did not work as planned.
    ...
  }
```

Each string argument (`host`, `prog`, and `type`) may take an asterisk (`"*"`) or `NULL` as the argument to indicate that
the processor function should be called for incoming messages for all values for the given parameter.

The processor function can also inspect what type of message it received by comparing the `XMessage` `type` value against
one of the predefined constant expressions in `smax.h`:

 | `XMessage` `type`          | Description                                     |
 |----------------------------|-------------------------------------------------|
 | `SMAX_MSG_STATUS`          | status update                                   |
 | `SMAX_MSG_INFO`            | informational program message                   |
 | `SMAX_MSG_DETAIL`          | additional detail (e.g. for verbose messages).  |
 | `SMAX_MSG_PROGRESS`        | progress update.                                |
 | `SMAX_MSG_DEBUG`           | debug messages (also e.g. traces)               |
 | `SMAX_MSG_WARNING`         | warning message                                 |
 | `SMAX_MSG_ERROR`           | error message                                   |

Once you no longer need to process messages by the given processor function, you can remove it from the call list by
passing its ID number (&lt;0) to `smaxRemoveMessageProcessor()`.

------------------------------------------------------------------------------

<a name="optional-metadata"></a>
## Optional metadata


### Descriptions

### Coordinate Systems

### Physical units


-----------------------------------------------------------------------------

<a name="smax-error-handling"></a>
## Error handling

The principal error handling of the library is an extension of that of __xchange__, with further error codes defined 
in `smax.h` and `redisx.h`. The functions that return an error status (either directly, or into the integer designated 
by a pointer argument), can be inspected by `smaxErrorDescription()`, e.g.:

```c
  int status = smaxShare(...);
  if (status != X_SUCCESS) {
    // Ooops, something went wrong...
    fprintf(stderr, "WARNING! set value: %s", smaxErrorDescription(status));
    ...
  }
```

-----------------------------------------------------------------------------

<a name="smax-debug-support"></a>
## Debug support

You can enable verbose output of the library with `smaxSetVerbose(boolean)`. When enabled, it will produce status 
messages to `stderr`so you can follow what's going on. In addition (or alternatively), you can enable debug messages 
with `xSetDebug(boolean)`. When enabled, all errors encountered by the library (such as invalid arguments passed) will 
be printed to `stderr`, including call traces, so you can walk back to see where the error may have originated from. 
(You can also enable debug messages by default by defining the `DEBUG` constant for the compiler, e.g. by adding 
`-DDEBUG` to `CFLAGS` prior to calling `make`). 

For helping to debug your application, the __xchange__ library provides two macros: `xvprintf()` and `xdprintf()`, 
for printing verbose and debug messages to `stderr`. Both work just like `printf()`, but they are conditional on 
verbosity being enabled via `xSetVerbose(boolean)` and `xSetDebug(boolean)`, respectively. Applications using this 
library may use these macros to produce their own verbose and/or debugging outputs conditional on the same global 
settings. 



-----------------------------------------------------------------------------

<a name="smax-future-plans"></a>
## Future plans

Some obvious ways the library could evolve and grow in the not too distant future:

 - Automated regression testing and coverage tracking.
 - Standardized remote program settings implementation.

If you have an idea for a must have feature, please let me (Attila) know. Pull requests, for new features or fixes to
existing ones, are especially welcome! 
 
-----------------------------------------------------------------------------
Copyright (C) 2025 Attila Kovács

