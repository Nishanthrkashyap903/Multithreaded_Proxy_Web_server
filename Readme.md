## Working of the Project

### Diagram

![](https://github.com/Nishanthrkashyap903/Multithreaded_Proxy_Web_server/blob/main/pics/Working_Diagram.png)

### How did we implement Multi-threading?

- Used Semaphore instead of Condition Variables.
- Semaphore’s sem_wait() and sem_post() doesn’t need any parameter. So it is a better option. 
- Used pthread_mutex to avoid inconsitency of data in LRU Cache.

##### OS Component Used ​

- Threading
- Locks 
- Semaphore
- Cache (LRU algorithm is used in it)

## How to Run

```bash
$ git clone https://github.com/Nishanthrkashyap903/Multithreaded_Proxy_Web_server.git
$ cd Multithreaded_Proxy_Web_server
$ make all
$ ./proxy <port no.>
```
`Open http://localhost:port/https://www.cs.princeton.edu/`

## Demo

- When the request is sent first time

![](https://github.com/Nishanthrkashyap903/Multithreaded_Proxy_Web_server/blob/main/pics/1a.png)

- if we send the same request then it will be retrieved from cache

![](https://github.com/Nishanthrkashyap903/Multithreaded_Proxy_Web_server/blob/main/pics/1b.png)

- Browser

![](https://github.com/Nishanthrkashyap903/Multithreaded_Proxy_Web_server/blob/main/pics/2.png)