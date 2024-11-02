compile
-------
```bash
g++ main.cpp -o pingle
```

run
---
```bash
./pingle <eth device> 2001:610:1908:a000::0 <x> <y>
```

For example, to compile and draw at the bottom right from some UltraVPS node in an infinite loop:
```bash
g++ main.cpp -o pingle; while (true); do sudo ./pingle net0 2001:610:1908:a000::0 1416 948; done
```

TODO
----
* make loop an optional marameter
* try to find a way to have a `dest_mac` address since not all routers seem to route these packets.

notes
-----
* it uses [Sean T. Barrett's image library](https://github.com/nothings/stb)