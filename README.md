# networks_reliable_transport

This project implements a lightweight reliable sliding window transport layer on top of the User Datagram Protocol (UDP). 

This implementation has the following functionality:

- Handles packed drops
- Handles packet corruption
- Provides trivial flow control
- Provides a stream abstraction
- Allows multiple packets to be outstanding at any time 
- Handles packet reordering
- Detects any single-bit errors in packets

This implementation covers both the client and server component of a transport layer. The client reads a stream of data (from STDIN), breaks it into fixed-sized packets suitable for UDP transport, prepends a control header to the data, and sends each packet to the server. The server reads these packets and writes the corresponding data, in order, to a reliable stream (STDOUT).

Run instructions:

On one shell, run:
./realiable 6666 -w 5 localhost:5555

On another shell, run:
./reliable 5555 -w 5 localhost:6666

Anything typed on one shell will show up on the other shell

To run the tester, run:
./tester -w 2 ./reliable

-w N: sets the window size to N
-v: shows the stderr output 
-T N: runs test number N instead of running all of them
--gdb: Attaches the process in the gdb debugger

Acknowledgements:
This project has been adapted from Stanford's CS144 Introduction to Computer Networking Labs
