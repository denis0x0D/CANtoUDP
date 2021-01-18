#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#define BUFSIZE 4096
#define NUM_INTERFACES 2
static const char *localhost = "127.0.0.1";
static unsigned char buf[BUFSIZE];

// Map port to CAN interfaces.
const int udp_ports0[NUM_INTERFACES] = {11111, 11112};
const int udp_ports1[NUM_INTERFACES] = {11113, 11114};
const char *can_interfaces[NUM_INTERFACES] = {"vcan0", "vcan1"};

// Create virtual CAN's:
// sudo ip link add dev vcan0 type vcan
// sudo ifconfig vcan0 up
// sudo ip link add dev vcan1 type vcan
// sudo ifconfig vcan1 up

static void run_udp_to_can() {
  for (int num = 0; num < NUM_INTERFACES; ++num) {
    if (fork()) {
      // UDP machinery
      struct sockaddr_in serv_addr;
      int recvlen;
      int fd;

      if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket UDP failed");
        exit(0);
      }

      memset((char *)&serv_addr, 0, sizeof(serv_addr));
      serv_addr.sin_family = AF_INET;
      serv_addr.sin_addr.s_addr = inet_addr(localhost);
      serv_addr.sin_port = htons(udp_ports0[num]);

      if (bind(fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind UDP failed");
        exit(0);
      }

      // CAN machinery
      struct sockaddr_can addr;
      struct ifreq ifr;
      int s;

      if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
        perror("socket CAN failed");
        exit(0);
      }

      strcpy(ifr.ifr_name, can_interfaces[num]);
      if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl failed");
      }

      memset(&addr, 0, sizeof(addr));
      addr.can_family = AF_CAN;
      addr.can_ifindex = ifr.ifr_ifindex;

      if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind CAN failed");
        exit(0);
      }

      // Recieve data from UDP socket and send it to CAN socket.
      while (1) {
#ifdef DEBUG
        printf("Wait connection \n");
#endif
        recvlen = recv(fd, buf, BUFSIZE, 0);
#ifdef DEBUG
        printf("recieved from UDP bytes %d\n", recvlen);
        for (int i = 0; i < recvlen; ++i) {
          printf("0x%02X ", buf[i]);
        }
        printf("\n");
#endif
        unsigned int n = recvlen / 8;
        if (recvlen % 8)
          n += 1;

        struct can_frame frames[n];
        memset(frames, 0, sizeof(struct can_frame) * n);

        for (int i = 0, e = recvlen / 8; i < e; i = i + 8) {
          int frame_num = i / 8;
          frames[frame_num].can_id = frame_num + 1;
          frames[frame_num].can_dlc = 8;
          for (int j = 0; j < 8; ++j) {
            frames[frame_num].data[j] = buf[i + j];
          }
        }

        if (recvlen % 8) {
          for (int i = 0, e = recvlen % 8; i < e; ++i) {
            frames[n - 1].can_id = n;
            frames[n - 1].can_dlc = e;
            for (int j = 0; j < 8; ++j) {
              frames[n - 1].data[i] = buf[recvlen - e + i];
            }
          }
        }

        for (int i = 0; i < n; ++i) {
#ifdef DEBUG
          printf("write frame \n");
#endif
          if (write(s, &frames[i], sizeof(struct can_frame)) !=
              sizeof(struct can_frame)) {
            perror("Write");
            exit(0);
          }
        }
      }
    }
  }
}

static void run_can_to_udp() {
  for (int num = 0; num < NUM_INTERFACES; ++num) {
    if (fork()) {
      // CAN machinery
      int s;
      int nbytes;
      struct sockaddr_can addr;
      struct ifreq ifr;

      if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
        perror("socket CAN failed");
        exit(0);
      }

      strcpy(ifr.ifr_name, can_interfaces[num]);
      int result_ioctl = ioctl(s, SIOCGIFINDEX, &ifr);
      if (result_ioctl < 0) {
        perror("ioctl failed");
        exit(0);
      }

      memset(&addr, 0, sizeof(addr));
      addr.can_family = AF_CAN;
      addr.can_ifindex = ifr.ifr_ifindex;

      if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind CAN failed");
        exit(0);
      }

      struct sockaddr_in serv_addr;
      int fd;
      if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket UDP failed");
        exit(0);
      }

      memset(&serv_addr, 0, sizeof(struct sockaddr_in));
      serv_addr.sin_family = AF_INET;
      serv_addr.sin_addr.s_addr = inet_addr(localhost);
      serv_addr.sin_port = htons(udp_ports1[num]);

      if (connect(fd, (struct socaddr *)&serv_addr, sizeof(serv_addr)) > 0) {
        perror("connect UDP failed");
        exit(0);
      }

      while (1) {
        struct can_frame frame;
        memset(&frame, 0, sizeof(struct can_frame));
        memset(&addr, 0, sizeof(addr));
#ifdef DEBUG
        printf("wait from CAN interface\n");
        nbytes = read(s, &frame, sizeof(struct can_frame));
#endif
#ifdef DEBUG
        printf("recieved from CAN bytes %d \n", nbytes);
        for (int i = 0; i < frame.can_dlc; i++) {
          printf("0x%02X ", frame.data[i]);
        }
        printf("\n");
#endif
        if (nbytes == 0) {
          continue;
        }
        if (nbytes < 0) {
          perror("read failed");
          exit(0);
        }

        char frameBuff[8];
        memset(frameBuff, 0, 8);
        for (int i = 0; i < frame.can_dlc; i++) {
          frameBuff[i] = frame.data[i];
        }

        if (send(fd, frameBuff, frame.can_dlc, 0) < 0) {
          perror("send failed");
          exit(0);
        }
      }
    }
  }
}

int main(int argc, char **argv) {
  run_udp_to_can();
  run_can_to_udp();
}
