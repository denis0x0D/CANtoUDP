A simple program which binds:
UDP -> CAN
CAN -> UDP

By default it binds a vritual scan interaces to udp ports:
"localhost:11111" -> "can0v"
"localhost:11112" -> "can1v"
"can0v" -> "localhost:11113"
"can1v" -> "localhost:11114"

How to build:
$./make.sh
