![Build Status](https://github.com/Smithsonian/smax-clib/actions/workflows/build.yml/badge.svg)
![Static Analysis](https://github.com/Smithsonian/smax-clib/actions/workflows/analyze.yml/badge.svg)
<a href="https://smithsonian.github.io/smax-clib/apidoc/html/files.html">
 ![API documentation](https://github.com/Smithsonian/smax-clib/actions/workflows/dox.yml/badge.svg)
</a>
<a href="https://smithsonian.github.io/smax-clib/index.html">
 ![Project page](https://github.com/Smithsonian/smax-clib/actions/workflows/pages/pages-build-deployment/badge.svg)
</a>

<picture>
  <source srcset="resources/CfA-logo-dark.png" alt="CfA logo" media="(prefers-color-scheme: dark)"/>
  <source srcset="resources/CfA-logo.png" alt="CfA logo" media="(prefers-color-scheme: light)"/>
  <img src="resources/CfA-logo.png" alt="CfA logo" width="400" height="67" align="right"/>
</picture>
<br clear="all">

# smax-clib

A free C/C++ client library and toolkit for the 
[SMA Exchange (SMA-X)](https://docs.google.com/document/d/1eYbWDClKkV7JnJxv4MxuNBNV47dFXuUWu7C4Ve_YTf0/edit?usp=sharing) 
structured real-time database

 - [API documentation](https://smithsonian.github.io/smax-clib/apidoc/html/files.html)
 - [Project pages](https://smithsonian.github.io/smax-clib) on github.io

Author: Attila Kovacs

Last Updated: 18 September 2024


## Table of Contents

 - [Introduction](#introduction)
 - [Prerequisites](#prerequisites)
 - [Building the SMA-X C library](#building)
 - [Linking your application against `smax-clib`](#linking)
 - [Command-line tools](#command-line-tools)
 - [Initial configuration](#configuration)
 - [Connecting to / disconnecting from SMA-X](#connecting)
 - [Sharing and pulling data](#sharing-and-pulling)
 - [Lazy pulling (high-frequency queries)](#lazy-pulling)
 - [Pipelined pulls (high volume queries)](#pipelined-pulls)
 - [Custom update handling](#update-handling)
 - [Program status / error messages via SMA-X](#status-messages)
 - [Optional metadata](#optional-metadata)
 - [Error handling](#error-handling)
 - [Debug support](#debug-support)
 - [Future plans](#future-plans)

------------------------------------------------------------------------------

<a name="introduction"></a>
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

<a name="prerequisites"></a>
## Prerequisites

The SMA-X C/C++ library has a build and runtime dependency on the __xchange__ and __RedisX__ libraries also available
at the Smithsonian Github repositories:

 - [Smithsonian/xchange](https://github.com/Smithsonian/xchange)
 - [Smithsonian/redisx](https://github.com/Smithsonian/redisx)

Additionally, to configure your Redis (or Valkey / Dragonfly) servers for SMA-X, you will need the 
[Smithsonian/smax-server](https://github.com/Smithsonian/smax-server) repo also.


------------------------------------------------------------------------------

<a name="building"></a>
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
   
 - `LDFLAGS`: Extra linker flags (default: _not set_). Note, `-lm -lredisx -lxchange -pthread` will be added 
   automatically.

 - `CHECKEXTRA`: Extra options to pass to `cppcheck` for the `make check` target

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

<a name="linking"></a>
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

<a name="configuration"></a>
## Initial configuration

Bu default, the library assumes that the Redis server used for SMA-X runs on a machine called `smax` (e.g. you may assign
`smax` to the IP address in `/etc/hosts`), and that the Redis is on the default port 6379/tcp. However, you can configure
to use a specific host and/or an alternative Redis port number to use instead. 


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

------------------------------------------------------------------------------

<a name="connecting"></a>
## Connecting to / disconnecting from SMA-X 

Once you have configured the connection parameters, you can connect to the server by:

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

<a name="connection-hooks"></a>
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

 - [The basics](#basics)
 - [Standard metadata](#metadata)
 - [Flexible types and sizes](#flexible-types-and-sizes)
 - [Scalar quantities](#scalars)
 - [Arrays](#arrays)
 - [Structures / substructures](#structures)

<a name="basics"></a>
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
  int status = smaxShare("system:subsystem", "some_data", X_DOUBLE, 8);
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


<a name="scalars"></a>
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

<a name="arrays"></a>
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
  
  int status = smaxShareArray("system:subsystem", "my_2d_array", X_FLOAT, 2, shape);
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

<a name="structures"></a>
### Structures / substructures...

You can share entire data structures, represented by an appropriate `XStructure` type (see the __xchange__ library for 
a description and usage):

```c
  XStructure s = ...   // The structured data you have prepared locally.
  
  int status = smaxShareStruct("syste:subsystem", &s);
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
is (1), but you may call `smaxLazyCache()` after the first pull of a variable, to indicate that you want to enable 
background cache updates (2) for it. The advantage of (1) is that it will never serve you outdated data even if there
are significant network latencies -- but you may have to wait a little to fetch updates. On the other hand (2) will
always provide a recent value with effectively no latency, but this value may be outdated if there are delays on the
network updating the cache. The difference is typically at the micro-seconds level on a local LAN. However, (2) may 
be preferable when you need to access SMA-X data from timing critical code blocks, where it is more important to ensure
that the value is returned quickly, rather than whether it is a millisecond too old or not.

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
of either waiting until all data is collected, or ask for as callback when the data is ready. 

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

and/or pattern(s)

```c
  int status = smaxSubscribe("watch_*", "watch_[a-z]_*");
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
functions `smaxWaitOn...()` that do exactly that. For example:

```c
  // Wait for watch_foo:watch_h_bar to update, or wait at most 500 ms
  int status = smaxWaitOnSubscribedVar("watch_foo", "watch_h_bar", 500);
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

Similar methods allow you to wait for updates on any subscribed variable in selected tables, or the update of
select variables in all subscribed tables, or any of the subscribed variables to change for the wait to end
normally (with return value 0).


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

<a name="status-messages"></a>  
## Program status / error messages via SMA-X

 - [Broadcasting status messages from an application](#broadcasting-messages)
 - [Processing program messages](#processing-messages)

SMA-X also provides a standard for reporting program status, warning, and error messages via the Redis PUB/SUB 
infrastructure. 
 
<a name="broadcasting-messages"></a>
### Broadcasting status messages from an application

Broadcasting program messages to SMA-X is very simple using a set of dedicated messaging functions by message
type. These are:

 | __smax_clib__ function                                     | Description                                |
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

<a name="processing-messages"></a>
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
 | -------------------------- | ----------------------------------------------- |
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

<a name="error-handling"></a>
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

<a name="debug-support"></a>
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

<a name="future-plans"></a>
## Future plans

Some obvious ways the library could evolve and grow in the not too distant future:

 - Automated regression testing and coverage tracking.
 - Standardized remote program settings implementation.

If you have an idea for a must have feature, please let me (Attila) know. Pull requests, for new features or fixes to
existing ones, are especially welcome! 
 
-----------------------------------------------------------------------------
Copyright (C) 2024 Attila Kovács

