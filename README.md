# UART Communication Framework

![C](https://img.shields.io/badge/C-POSIX-blue) ![Linux](https://img.shields.io/badge/Linux-termios-informational) 

> Linux UART interface using `termios` — configurable baud, non-blocking RX via `poll()`, hex dump logging, graceful `SIGINT` shutdown. Tested with virtual UARTs via `socat`.

---

## UART Config — 8N1

| Data Bits | Parity | Stop Bits | Flow Control | Baud Rate |
|:---------:|:------:|:---------:|:------------:|:---------:|
| 8 | None | 1 | Disabled | CLI arg |

---

## Build & Run

```bash
gcc uart_comm.c -o uart_comm
```

```bash
./uart_comm /dev/ttyUSB0 115200
```

**Syntax:** `./uart_comm <device> <baudrate>`

---

## Virtual UART Testing with `socat`

**Step 1** — Create a virtual UART pair (keep this terminal open):

```bash
socat -d -d pty,raw,echo=0 pty,raw,echo=0
# outputs /dev/pts/1 and /dev/pts/2
```

**Step 2** — Run the program on one endpoint:

```bash
./uart_comm /dev/pts/1 115200
```

**Step 3** — Send data from the other endpoint:

```bash
echo "UART TEST" > /dev/pts/2
```

**Step 4** — Observe RX output:

```
[23:45:25] received: UART TEST
[23:45:25] hex: 55 41 52 54 20 54 45 53 54
```

**Step 5** — Graceful shutdown:

```
Ctrl+C  →  [23:46:10] closing uart
```

---

## Key APIs

| API | Purpose |
|-----|---------|
| `tcgetattr()` / `tcsetattr()` | Read / apply UART settings |
| `cfsetispeed()` / `cfsetospeed()` | Set baud rate |
| `poll()` | Non-blocking RX with timeout |
| `open()` with `O_RDWR \| O_NOCTTY` | Open serial device |
| `signal(SIGINT, …)` | Graceful shutdown |

---

## Error Reference

**Permission denied**
```bash
sudo usermod -aG dialout $USER   # then re-login
```

**`open failed`** — verify device path:
```bash
ls /dev/tty*
```

**Garbled / no data** — baud rate mismatch. Ensure both ends use the same rate and 8N1 framing.

---

## Source Code

<details>
<summary><code>uart_comm.c</code> — click to expand</summary>

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <poll.h>
#include <signal.h>
#include <time.h>

#define BUF_SIZE   256
#define TIMEOUT_MS 5000          /* poll() timeout — 5 s */

static int running = 1;

/* ── Signal handler ─────────────────────────────────────── */
void sigint_handler(int sig) { running = 0; }

/* ── Timestamped log prefix ─────────────────────────────── */
void timestamp(void) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    printf("[%02d:%02d:%02d] ", t->tm_hour, t->tm_min, t->tm_sec);
}

/* ── Map integer baud to termios constant ───────────────── */
speed_t get_baud(int baud) {
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        default:     return B115200;
    }
}

/* ── Configure UART — 8N1, no flow control ──────────────── */
int uart_init(int fd, int baud) {
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) return -1;

    cfsetispeed(&tty, get_baud(baud));
    cfsetospeed(&tty, get_baud(baud));

    tty.c_cflag &= ~PARENB;          /* no parity  */
    tty.c_cflag &= ~CSTOPB;          /* 1 stop bit */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |=  CS8;             /* 8 data bits */
    tty.c_cflag &= ~CRTSCTS;         /* no HW flow ctrl */
    tty.c_cflag |=  CREAD | CLOCAL;

    tty.c_iflag &= ~(IXON | IXOFF | IXANY); /* no SW flow ctrl */
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); /* raw mode */
    tty.c_oflag &= ~OPOST;           /* raw output */

    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 10;            /* 1 s inter-byte timeout */

    return tcsetattr(fd, TCSANOW, &tty) == 0 ? 0 : -1;
}

/* ── Hex dump received bytes ────────────────────────────── */
void print_hex(unsigned char *buf, int len) {
    for (int i = 0; i < len; i++) printf("%02X ", buf[i]);
    printf("\n");
}

/* ── Entry point ────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <device> <baudrate>\n", argv[0]);
        return 1;
    }

    signal(SIGINT, sigint_handler);

    int baud = atoi(argv[2]);
    int fd   = open(argv[1], O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) { timestamp(); printf("open failed: %s\n", strerror(errno)); return 1; }

    if (uart_init(fd, baud) < 0) {
        timestamp(); printf("uart config failed\n"); close(fd); return 1;
    }
    timestamp(); printf("uart initialized at %d baud\n", baud);

    /* ── TX ─────────────────────────────────────────────── */
    const char *msg = "Hello from UART\n";
    if (write(fd, msg, strlen(msg)) < 0) {
        timestamp(); printf("write failed: %s\n", strerror(errno));
        close(fd); return 1;
    }
    timestamp(); printf("sent: %s", msg);

    /* ── Non-blocking RX loop via poll() ────────────────── */
    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    unsigned char rx_buf[BUF_SIZE];

    while (running) {
        int ret = poll(&pfd, 1, TIMEOUT_MS);
        if (ret < 0) { timestamp(); printf("poll failed\n"); break; }
        if (ret == 0) { timestamp(); printf("timeout\n"); continue; }

        if (pfd.revents & POLLIN) {
            memset(rx_buf, 0, sizeof(rx_buf));
            int rx = read(fd, rx_buf, sizeof(rx_buf) - 1);
            if (rx < 0) { timestamp(); printf("read failed\n"); break; }
            if (rx > 0) {
                rx_buf[rx] = '\0';
                timestamp(); printf("received: %s\n", rx_buf);
                timestamp(); printf("hex: "); print_hex(rx_buf, rx);
            }
        }
    }

    timestamp(); printf("closing uart\n");
    close(fd);
    return 0;
}
```

</details>

---

## Relevance — RISC-V ACT / M-Mode Firmware Validation

| Use Case | How This Project Applies |
|----------|--------------------------|
| OpenSBI debug logs | UART console is the primary M-mode output channel for `PASS/FAIL` reporting |
| Hardware bring-up | Hex dump + timestamped logs accelerate binary protocol inspection |
| ACT framework | Non-blocking `poll()` + timeout mirrors the host harness reading compliance results over serial |
| Bootloader comms | 8N1 raw mode matches the framing used by most RISC-V bootloaders |

---

## File Structure

```
uart_project/
├── uart_comm.c
├── uart_comm        ← compiled binary
└── README.md
```

---


