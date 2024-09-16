![Build Status](https://github.com/Smithsonian/smax-clib/actions/workflows/build.yml/badge.svg)
![Static Analysis](https://github.com/Smithsonian/smax-clib/actions/workflows/check.yml/badge.svg)
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

# SMA-X: SMA information exchange

Author: Attila Kovacs

Last Updated: 14 September 2024

## Table of Contents

 - [Introduction](#introduction)
 - [Prerequisites](#prerequisites)
 - [Building the SMA-X C library](#building)
 - [Initial configuration](#configuration)
 - [Connecting to / disconnecting from SMA-X](#connecting)
 - [Sharing and pulling data](#sharing-and-pulling)
 - [Lazy pulling (high-frequency queries)](#lazy-pulling)
 - [Pipelined pulls (high volume queries)](#pipelined-pulls)
 - [Custom notification and update handling](#notifications)
 - [Optional metadata](#optional-metadata)
 - [Error handling](#error-handling)
 - [Debug support](#debug-support)
 - [Future plans](#future-plans)

------------------------------------------------------------------------------

<a name="introduction"></a>
## Introduction

The [SMA information eXchange (SMA-X)](https://docs.google.com/document/d/1eYbWDClKkV7JnJxv4MxuNBNV47dFXuUWu7C4Ve_YTf0/edit?usp=sharing) 
is a high performance and versatile data sharing platform for distributed software systems. It is built around a 
central Redis database, and provides atomic access to structured data, including specific branches and/or leaf nodes, 
with associated metadadata. SMA-X was developed at the Submillimeter Array (SMA) observatory, where we use it to share 
real-time data among hundreds of computers and nearly a thousand individual programs.

SMA-X consists of a set of server-side [LUA](https://lua.org/) scripts that run on [Redis](https://redis.io) (or one 
of its forks / clones such as [Valkey](https://valkey.io) or [Dragonfly](https://dragonfly.io)); a set of libraries to 
interface client applications; and a set of command-line tools built with them. Currently we provide client libraries 
for C/C++ and Python 3. We may provide Java and/or Rust client libraries too in the future. 

There are no official releases of __smax-clib__ yet. An initial 1.0.0 release is expected early/mid 2025. 
Before then the API may undergo slight changes and tweaks. Use the repository as is at your own risk for now.

Some related links:

 - [API documentation](https://smithsonian.github.io/smax-clib/apidoc/html/files.html)
 - [SMA-X specification](https://docs.google.com/document/d/1eYbWDClKkV7JnJxv4MxuNBNV47dFXuUWu7C4Ve_YTf0/edit?usp=sharing)
 - [Project page](https://smithsonian.github.io/smax-clib) on github.io
 - [Smithsonian/smax-python](https://smithsonian.github.io/smax-python) an alternative library for Python 3.


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

 - `XCHANGE`: the root of the location where the [Smithsonian/xchange](https://github.com/Smithsonian/xchange) library 
   is installed. It expects to find `xchange.h` under `$(XCHANGE)/include` and `libxchange.so` under `$(XCHANGE)/lib`
   or else in the default `LD_LIBRARY_PATH`.
   
 - `REDISX`: the root of the location where the [Smithsonian/redisx](https://github.com/Smithsonian/redisx) library 
   is installed. It expects to find `redisx.h` under `$(REDISX)/include` and `libredisx.so` under `$(REDISX)/lib`
   or else in the default `LD_LIBRARY_PATH`.
   
 - `CC`: The C compiler to use (default: `gcc`).

 - `CPPFLAGS`: C pre-processor flags, such as externally defined compiler constants.
 
 - `CFLAGS`: Flags to pass onto the C compiler (default: `-Os -Wall`). Note, `-Iinclude` will be added automatically.
   
 - `LDFLAGS`: Linker flags (default is `-lm`). Note, `-lredisx -lxchange` will be added automatically.

 - `BUILD_MODE`: You can set it to `debug` to enable debugging features: it will initialize the global `xDebug` 
   variable to `TRUE` and add `-g` to `CFLAGS`.

 - `CHECKEXTRA`: Extra options to pass to `cppcheck` for the `make check` target
 
After configuring, you can simply run `make`, which will build the `shared` (`lib/libsmax.so[.1]`) and `static` 
(`lib/libsmax.a`) libraries, local HTML documentation (provided `doxygen` is available), and performs static
analysis via the `check` target. Or, you may build just the components you are interested in, by specifying the
desired `make` target(s). (You can use `make help` to get a summary of the available `make` targets). 

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

And finally, you can select the option to automatically try reconnect to the SMA-X server in case of lost connection or
network errors (and keep track of changes locally until then):

```c
  smaxSetResilient(TRUE);
```

------------------------------------------------------------------------------

<a name="connecting"></a>
## Connecting to / disconnecting from SMA-X 

Once you have configured the connection parameters, you can connec to the server by:

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
library provides support for connection 'hooks' -- that is custom functions that are called in the even of connecting 
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
will simply apply the necessaty type conversion automatically, and then truncate, or else pad (with zeroes), the data 
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
no not want to do that, especially if the data you need is not necessaily changing fast. There is no point on wasting
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

When the vatiable is updated in SMA-X, our client library will be notified, and one of two things can happen:

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
such, instead of thousand of queries per second, you can pull 2-3 orders of magnitude more in a given time, with hudreds 
of thousands to even millions of pull per second this way.

<a name="lazy-synchronization"></a>
### Synchronization points and waiting

After you have submitted a batch of pull request to the queue, you can create a synchronization point as:

```c
  XSyncPoint *syncPoint = smaxCreateSyncPoint();
```

A synchronization point is a marker in the queue that we can wait on. After the synchronization point is created, you
can sumbit more pull request to the same queue (e.g. for another processing block), or do some other things for a bit
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
all previously sumbitted requests have been collected. You can do that with:

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

<a name="notifications"></a>
## Custom notifications and update handling

### Monitoring updates

### Waiting for updates

### Status / error messages


------------------------------------------------------------------------------

<a name="optional-metadata"></a>
## Optional metadata


### Descriptions

### Coordinate Systems

### Physical units

#### Coordinate systems



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

If you have an idea for a must have feature, please let me (Attila) know. Pull requests, for new features or fixes to
existing ones, are especially welcome! 
 
-----------------------------------------------------------------------------
Copyright (C) 2024 Attila Kovács

