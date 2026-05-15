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

#define BUF_SIZE 256
#define TIMEOUT_MS 5000

static int keepRunning = 1;   // loop control flag


void handle_sigint(int sig)
{
   // ctrl+c should stop polling loop
   keepRunning = 0;
}


void print_time()
{
    time_t now = time(NULL);

   struct tm *info = localtime(&now);

    // printing current time before logs
    printf("[%02d:%02d:%02d] ",
       info->tm_hour,
         info->tm_min,
      info->tm_sec);
}


// helper function for baudrate conversion
speed_t pick_baud(int baud)
{
    switch(baud)
    {
      case 9600:
            return B9600;

        case 19200:
          return B19200;

      case 38400:
            return B38400;

        case 57600:
             return B57600;

      case 115200:
        return B115200;

      default:
            // fallback baud
          return B115200;
    }
}


int setup_uart(int fd,int baud)
{
    struct termios tty;

    // getting current serial config first
    if(tcgetattr(fd,&tty) != 0)
    {
      return -1;
    }

    speed_t speed = pick_baud(baud);

   cfsetispeed(&tty,speed);
    cfsetospeed(&tty,speed);

    tty.c_cflag &= ~PARENB;     // no parity
   tty.c_cflag &= ~CSTOPB;      // 1 stop bit

     tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;         // 8 bit data

    tty.c_cflag &= ~CRTSCTS;    // disable hw flow ctrl
      tty.c_cflag |= (CREAD | CLOCAL);

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);

   // raw input mode
   tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

    tty.c_oflag &= ~OPOST;

    tty.c_cc[VMIN] = 0;
      tty.c_cc[VTIME] = 10;

    if(tcsetattr(fd, TCSANOW, &tty) != 0)
    {
        return -1;
    }

      return 0;
}


void print_hex(unsigned char *buf,int len)
{
   int i;

    // dumping bytes in hex format
    for(i = 0; i < len; i++)
     {
        printf("%02X ",buf[i]);
     }

   printf("\n");
}


/* uart test app
   mainly for debugging serial devices
   maybe useful for loopback testing too
*/
int main(int argc,char *argv[])
{
    int fd;
   int baud;

    struct pollfd pfd;

    unsigned char rxBuf[BUF_SIZE];

   char *msg = "Hello from UART\n";

    signal(SIGINT, handle_sigint);

    // expecting device + baudrate
    if(argc != 3)
    {
        printf("Usage: %s <device> <baudrate>\n",argv[0]);
      printf("Example: %s /dev/ttyUSB0 115200\n", argv[0]);

        return 1;
    }

   baud = atoi(argv[2]);

    // open serial device
    fd = open(argv[1], O_RDWR | O_NOCTTY | O_SYNC);

     if(fd < 0)
    {
        print_time();
       printf("open failed : %s\n", strerror(errno));

        return 1;
    }

    // configure uart settings
    if(setup_uart(fd,baud) < 0)
    {
      print_time();
        printf("uart config failed\n");

       close(fd);
      return 1;
    }

   print_time();
    printf("uart initialized at %d baud\n", baud);

    // sending one startup packet
    int tx = write(fd,msg,strlen(msg));

    if(tx < 0)
     {
        print_time();
        printf("write failed : %s\n", strerror(errno));

          close(fd);
        return 1;
    }

    print_time();
     printf("sent -> %s", msg);

    pfd.fd = fd;
    pfd.events = POLLIN;

    // main receive loop
    while(keepRunning)
    {
       int ret = poll(&pfd,1,TIMEOUT_MS);

        if(ret < 0)
        {
            print_time();
          printf("poll failed\n");
            break;
        }

       if(ret == 0)
        {
           print_time();
            printf("timeout\n");

            continue;
        }

        // data available from uart
        if(pfd.revents & POLLIN)
        {
            memset(rxBuf,0,sizeof(rxBuf));

            int rx = read(fd, rxBuf,sizeof(rxBuf)-1);

            if(rx < 0)
            {
                print_time();
               printf("read failed\n");
                break;
            }

             if(rx > 0)
            {
                rxBuf[rx] = '\0';

               print_time();
                printf("received : %s\n", rxBuf);

                print_time();
                 printf("hex : ");

                print_hex(rxBuf,rx);

                // maybe add packet parser later
            }
        }
    }

   print_time();
    printf("closing uart\n");

    close(fd);

    return 0;
}
