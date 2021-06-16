USAGE: ./httpserver [port] 
<pre>
Optional flags:
l: specify log file, to record formatted responses
N: Number of worker threads, default = 4
</pre>
Examples:\
This enables 8 worker threads.
<pre>
./httpserver [port] -N 8
</pre>
\
This enables a logfile named 'logfile' and will be stored in server's directory.
<pre>
./httpserver [port] -l logfile
</pre>
