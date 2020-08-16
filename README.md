# HTTPSERVER

This assignment is to implement a multi-threaded HTTP server with logging. In order to fulfill multi-threading, I created a ThreadArg struct. Queue struct I used in this program is from GeeksforGeeks. Source: https://www.geeksforgeeks.org/queue-set-1introduction-and-array-implementation/

__How to Run:__

```bash
make
```

_Makefile_ will generate an executable file named _httpserver_:

```bash
./httpserver port
```

```bash
./httpserver port -N 4 
```

```bash
./httpserver port -N 4 -l log_file
```

```bash
./httpserver port -l log_file
```

-N is followed by the number of threads, -l will enable the logging functionality and is followed by the name of the log file. 

As the server is on, open another terminal, which will be acting as the client. Utilize the curl command to make requests: 

```bash
-T, --upload-file <file>
-I, --head
-w, --write-out <format>

curl -s http://localhost:8080/FILENAME
curl -s -T FILENAME http://localhost:8080/FILENAME
curl -s -I http://localhost:8080/FILENAME
```
