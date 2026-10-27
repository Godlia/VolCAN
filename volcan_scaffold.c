#include <errno.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/if.h>
#include <net/if.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_CAN_IFACE "can0"
#define PPPD_UP_COMMAND "pppd call provider"
#define PPPD_DOWN_COMMAND "poff"

static void print_usage(const char *program) {
  printf("Usage: %s [options]\n", program);
  printf("  --pppd-up                     Bring PPP link up with pppd\n");
  printf("  --pppd-down                   Bring PPP link down\n");
  printf("  --mqtt-pub <topic> <payload>  Publish MQTT message (scaffold)\n");
  printf("  --mqtt-sub <topic>            Subscribe MQTT topic (scaffold)\n");
  printf("  --can-send <id> <bytes>       Send CAN frame, bytes as hex (e.g. 0102A0)\n");
  printf("  --can-sniff <seconds>         Sniff CAN frames from can0\n");
  printf("  --help                        Show this message\n");
}

static int run_command(const char *cmd) {
  int rc = system(cmd);
  if (rc != 0) {
    fprintf(stderr, "Command failed (%d): %s\n", rc, cmd);
    return -1;
  }
  return 0;
}

static int scaffold_mqtt_publish(const char *topic, const char *payload) {
  printf("[scaffold] MQTT publish topic='%s' payload='%s'\n", topic, payload);
  printf("[scaffold] Replace this with your MQTT client implementation.\n");
  return 0;
}

static int scaffold_mqtt_subscribe(const char *topic) {
  printf("[scaffold] MQTT subscribe topic='%s'\n", topic);
  printf("[scaffold] Replace this with your MQTT client implementation.\n");
  return 0;
}

static int open_can_socket(const char *ifname) {
  int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
  struct ifreq ifr;
  struct sockaddr_can addr;

  if (sock < 0) {
    perror("socket(PF_CAN)");
    return -1;
  }

  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
  if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
    perror("ioctl(SIOCGIFINDEX)");
    close(sock);
    return -1;
  }

  memset(&addr, 0, sizeof(addr));
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;
  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind(AF_CAN)");
    close(sock);
    return -1;
  }

  return sock;
}

static int parse_hex_payload(const char *input, struct can_frame *frame) {
  size_t len = strlen(input);
  unsigned int byte = 0;

  if (len == 0 || (len % 2) != 0) {
    fprintf(stderr, "CAN payload must be an even-length hex string.\n");
    return -1;
  }
  if ((len / 2) > CAN_MAX_DLEN) {
    fprintf(stderr, "CAN payload too long. Max %d bytes.\n", CAN_MAX_DLEN);
    return -1;
  }

  frame->can_dlc = 0;
  for (size_t i = 0; i < len; i += 2) {
    if (sscanf(input + i, "%2x", &byte) != 1) {
      fprintf(stderr, "Invalid hex payload.\n");
      return -1;
    }
    frame->data[frame->can_dlc++] = (uint8_t)byte;
  }

  return 0;
}

static int can_send_frame(const char *ifname, const char *id_str, const char *payload_hex) {
  struct can_frame frame;
  char *end = NULL;
  unsigned long can_id_ul = strtoul(id_str, &end, 16);

  if (end == id_str || *end != '\0' || can_id_ul > 0x7FFUL) {
    fprintf(stderr, "CAN ID must be a valid 11-bit hex value.\n");
    return -1;
  }

  memset(&frame, 0, sizeof(frame));
  frame.can_id = (canid_t)can_id_ul;
  if (parse_hex_payload(payload_hex, &frame) != 0) {
    return -1;
  }

  int sock = open_can_socket(ifname);
  if (sock < 0) {
    return -1;
  }

  ssize_t written = write(sock, &frame, sizeof(frame));
  close(sock);
  if (written != (ssize_t)sizeof(frame)) {
    perror("write(can_frame)");
    return -1;
  }

  printf("Sent CAN frame: id=0x%03lX dlc=%u\n", can_id_ul, frame.can_dlc);
  return 0;
}

static int can_sniff_frames(const char *ifname, int seconds) {
  int sock = open_can_socket(ifname);
  if (sock < 0) {
    return -1;
  }

  time_t end_time = time(NULL) + seconds;
  while (time(NULL) < end_time) {
    fd_set readfds;
    struct timeval tv;
    struct can_frame frame;

    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    int ready = select(sock + 1, &readfds, NULL, NULL, &tv);
    if (ready < 0) {
      perror("select");
      close(sock);
      return -1;
    }
    if (ready == 0) {
      continue;
    }

    ssize_t bytes = read(sock, &frame, sizeof(frame));
    if (bytes < 0) {
      perror("read(can_frame)");
      close(sock);
      return -1;
    }
    if (bytes != (ssize_t)sizeof(frame)) {
      continue;
    }

    printf("RX id=0x%03X dlc=%u data=", frame.can_id & CAN_SFF_MASK, frame.can_dlc);
    for (uint8_t i = 0; i < frame.can_dlc; ++i) {
      printf("%02X", frame.data[i]);
    }
    printf("\n");
  }

  close(sock);
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 1) {
    print_usage(argv[0]);
    return 0;
  }

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--help") == 0) {
      print_usage(argv[0]);
      return 0;
    }
    if (strcmp(argv[i], "--pppd-up") == 0) {
      if (run_command(PPPD_UP_COMMAND) != 0) {
        return 1;
      }
      continue;
    }
    if (strcmp(argv[i], "--pppd-down") == 0) {
      if (run_command(PPPD_DOWN_COMMAND) != 0) {
        return 1;
      }
      continue;
    }
    if (strcmp(argv[i], "--mqtt-pub") == 0) {
      if ((i + 2) >= argc) {
        fprintf(stderr, "--mqtt-pub requires <topic> <payload>\n");
        return 1;
      }
      if (scaffold_mqtt_publish(argv[i + 1], argv[i + 2]) != 0) {
        return 1;
      }
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--mqtt-sub") == 0) {
      if ((i + 1) >= argc) {
        fprintf(stderr, "--mqtt-sub requires <topic>\n");
        return 1;
      }
      if (scaffold_mqtt_subscribe(argv[i + 1]) != 0) {
        return 1;
      }
      i += 1;
      continue;
    }
    if (strcmp(argv[i], "--can-send") == 0) {
      if ((i + 2) >= argc) {
        fprintf(stderr, "--can-send requires <id> <hex-bytes>\n");
        return 1;
      }
      if (can_send_frame(DEFAULT_CAN_IFACE, argv[i + 1], argv[i + 2]) != 0) {
        return 1;
      }
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--can-sniff") == 0) {
      if ((i + 1) >= argc) {
        fprintf(stderr, "--can-sniff requires <seconds>\n");
        return 1;
      }
      int seconds = atoi(argv[i + 1]);
      if (seconds <= 0) {
        fprintf(stderr, "--can-sniff seconds must be > 0\n");
        return 1;
      }
      if (can_sniff_frames(DEFAULT_CAN_IFACE, seconds) != 0) {
        return 1;
      }
      i += 1;
      continue;
    }

    fprintf(stderr, "Unknown option: %s\n", argv[i]);
    print_usage(argv[0]);
    return 1;
  }

  return 0;
}
