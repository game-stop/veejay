# Debugging veejay

Please report any issues here: https://github.com/c0ntrol/veejay/issues

Open a new bug report for each issue, so an effective bugfix workflow will be completed for each issue.

Other details you should heed
* Make sure your software is up to date. Ideally, test an in-development version (https://github.com/c0ntrol/veejay)
* Figure out the steps to reproduce a bug:
** If you have precise steps to reproduce — great! — you're on your way to reporting a useful bug report.
** If you can reproduce occasionally, but not after following specific steps, you must provide additional information for the bug to be useful.

## Redirecting veejay's console output

You can run veejay with the -v commandline flag, telling it to be more
verbosive:

```
$ veejay -v -n > /tmp/logfile
```

You can watch the console logging using tail:

```
$ tail -f /tmp/logfile
```

## Network event logging

You can log all network related events to /tmp/veejay.net.log (file
destination cannot be changed)

```
$ export VEEJAY_LOG_NET_IO=on

$ veejay -v
```

## Crash Recovery

If veejay crahes, it will write your samplelist and edit descision files to
`$HOME/.veejay/recovery`.

The recovery files can be loaded with:

```
$ veejay /path/to/recovery_editlist_p???.edl -l /path/to/recovery_samplelist_p???.sl
```

## Useful backtraces

A useful backtrace not only contains symbols but also lists the linenumber and
name of the source file

To enable debugging symbols to be build in you must do a clean build and pass
the --enable-debug flag to configure.

```
$ ./configure --enable-debug
$ (make clean)
$ make -j12 && make install
```

You can attach a debugger to veejay, or you can load veejay in the debugger:

```
$ sudo gdb -p `pidof veejay`
```

```
$ gdb /path/to/veejay
...
$ bt
```

To enable profiling you must do a clean build and pass
the --enable-profiling flag to configure.

Alternatively, you can use valgrind to look for memory leaks, threading
problems, etc:

```
$ valgrind --leak-check=yes --leak-resolution=high --log-file=/tmp/valgrind.veejay.log /path/to/veejay -n -v ...

$ valgrind --tool=helgrind /path/to/veejay -n -v
```
