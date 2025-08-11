#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/socket.h>

#define RADIO_BASE_PHYS  0x43C10000u  
#define MAP_BYTES        0x1000

#define REG_FAKE  0x00   // w: fake DDS phase inc
#define REG_TUNE  0x04   // w: tuner DDS phase inc
#define REG_CTRL  0x08   // w: bit0=reset, bit1=timer clr
#define REG_COUNT 0x00   // r: FIFO rd_data_count (words)
#define REG_DATA  0x04   // r: pops one q and i

#define UDP_PORT         25344
#define SAMPLES_PER_PKT  256
#define FCLK_HZ          125000000.0  

static volatile uint32_t *axil = NULL;
static inline uint32_t rd32(uint32_t off){ return axil[off>>2]; }
static inline void     wr32(uint32_t off, uint32_t v){ axil[off>>2] = v; }

static uint32_t pinc_from_hz(double hz){
  if (hz < 0) hz = 0;
  if (hz > FCLK_HZ/2) hz = FCLK_HZ/2;
  double scale = (double)(1ULL<<32) / FCLK_HZ;
  return (uint32_t)(hz * scale + 0.5);
}

static void usage(const char* p){
  fprintf(stderr,
    "Usage: %s <dest_ip> [--fake Hz] [--tune Hz] [--start]\n"
    "Runtime commands on stdin:\n"
    "  f <Hz>    set fake DDS\n"
    "  t <Hz>    set tune DDS\n"
    "  d <ip>    change destination IP\n"
    "  s on|off  start/stop streaming\n"
    "  q         quit\n", p);
}

int main(int argc, char**argv){
  if (argc < 2){ usage(argv[0]); return 1; }

  int memfd = open("/dev/mem", O_RDWR|O_SYNC);
  if (memfd < 0){ perror("open /dev/mem"); return 1; }
  void* map = mmap(NULL, MAP_BYTES, PROT_READ|PROT_WRITE, MAP_SHARED, memfd, RADIO_BASE_PHYS);
  if (map == MAP_FAILED){ perror("mmap"); return 1; }
  axil = (volatile uint32_t*)map;

  // UDP socket
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0){ perror("socket"); return 1; }
  struct sockaddr_in dst = {0};
  dst.sin_family = AF_INET;
  dst.sin_port = htons(UDP_PORT);
  if (inet_pton(AF_INET, argv[1], &dst.sin_addr) != 1){ fprintf(stderr,"bad IP\n"); return 1; }

  int streaming = 0;
  for (int i=2;i<argc;i++){
    if (!strcmp(argv[i],"--fake") && i+1<argc) wr32(REG_FAKE, pinc_from_hz(atof(argv[++i])));
    else if (!strcmp(argv[i],"--tune") && i+1<argc) wr32(REG_TUNE, pinc_from_hz(atof(argv[++i])));
    else if (!strcmp(argv[i],"--start")) streaming = 1;
    else { usage(argv[0]); return 1; }
  }

  // bring radio out of reset
  wr32(REG_CTRL, 0u);  // bit0=0 => run

  int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

  uint8_t pkt[2 + 4*SAMPLES_PER_PKT];
  uint16_t seq = 0;

  fprintf(stderr,"Dest=%s:%d  streaming=%d\n", argv[1], UDP_PORT, streaming);

  for(;;){
    char buf[128]; ssize_t n = read(STDIN_FILENO, buf, sizeof(buf)-1);
    if (n > 0){
      buf[n]=0;
      if (buf[0]=='q') break;
      else if (buf[0]=='f'){ double hz=atof(&buf[1]); wr32(REG_FAKE, pinc_from_hz(hz)); fprintf(stderr,"fake=%.3f Hz\n",hz); }
      else if (buf[0]=='t'){ double hz=atof(&buf[1]); wr32(REG_TUNE, pinc_from_hz(hz)); fprintf(stderr,"tune=%.3f Hz\n",hz); }
      else if (buf[0]=='d'){ char ip[64]; if (sscanf(buf+1,"%63s",ip)==1){ inet_pton(AF_INET, ip, &dst.sin_addr); fprintf(stderr,"dest=%s\n",ip);} }
      else if (buf[0]=='s'){ if (strstr(buf,"on")) streaming=1; else if (strstr(buf,"off")) streaming=0; fprintf(stderr,"streaming=%d\n",streaming); }
    }

    if (!streaming){ usleep(10000); continue; }

    // ensure fifo fine
    uint32_t count = rd32(REG_COUNT);
    if (count < SAMPLES_PER_PKT){ usleep(2000); continue; }

    memcpy(pkt, &seq, 2); seq++;

    // pop 256 IQ words
    // memcpy the 32-bit I Q in right order
    for (int i=0;i<SAMPLES_PER_PKT;i++){
      uint32_t w = rd32(REG_DATA);
      memcpy(&pkt[2 + 4*i], &w, 4);
    }

    ssize_t s = sendto(sock, pkt, sizeof(pkt), 0, (struct sockaddr*)&dst, sizeof(dst));
    if (s != (ssize_t)sizeof(pkt)){ perror("sendto"); usleep(10000); }
  }

  close(sock);
  munmap(map, MAP_BYTES);
  close(memfd);
  return 0;
}
