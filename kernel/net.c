#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"

// xv6's ethernet and IP addresses
static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15);

// qemu host's ethernet address.
static uint8 host_mac[ETHADDR_LEN] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };

static struct spinlock netlock;

struct udp_state udp_lab;

static int debug_count;
int kalloc_total = 0;
int kalloc_e1000 = 0;
int kalloc_iprx = 0;

int kfree_total = 0;
int kfree_e1000 = 0;
int kfree_iprx = 0;
int kfree_MAX_PACKETS = 0;
int kfree_OWNER = 0;
int kfree_PROTO = 0;
int kfree_PAYLOAD = 0;
int kfree_QUEUE = 0;
int kfree_IPSUCCESS = 0;
int kfree_SYSRECV =0;

int binding_total = 0;

void
netinit(void)
{
  initlock(&netlock, "netlock");
}


//
// bind(int port)
// prepare to receive UDP packets address to the port,
// i.e. allocate any queues &c needed.
//
uint64
sys_bind(void)
{
  int port;
  argint(0, &port);
  if (port < 0 || port >= MAX_UDP_PORTS)
    return -1;

  struct udp_binding *b = &udp_lab.bindings[port];

  acquire(&b->lock);
  if((b->owner != 0)) {
    release(&b->lock);
    return -1;
  }
  b->owner = myproc();     
  b->queue_head = 0;
  b->queue_tail = 0;
  b->count = 0;
  release(&b->lock);
  binding_total++;
  printf("(%d) BINDING %d\n", binding_total, port);

  return 0;
}

//
// unbind(int port)
// release any resources previously created by bind(port);
// from now on UDP packets addressed to port should be dropped.
//
uint64
sys_unbind(void)
{
  //
  // Optional: Your code here.
  //

  return 0;
}

//
// recv(int dport, int *src, short *sport, char *buf, int maxlen)
// if there's a received UDP packet already queued that was
// addressed to dport, then return it.
// otherwise wait for such a packet.
//
// sets *src to the IP source address.
// sets *sport to the UDP source port.
// copies up to maxlen bytes of UDP payload to buf.
// returns the number of bytes copied,,
// and -1 if there was an error.
//
// dport, *src, and *sport are host byte order.
// bind(dport) must previously have been called.
//
uint64
sys_recv(void)
{
  struct proc *p = myproc();
  int dport, ip_src_addr, sport_ptr, maxlen, payload_len;
  uint64 bufaddr; 
  
  argint(0, &dport);
  argint(1, &ip_src_addr);
  argint(2, &sport_ptr);
  argaddr(3, &bufaddr);
  argint(4, &maxlen); 

  if (dport < 0 || dport >= MAX_UDP_PORTS)
    return -1;
  
  struct udp_binding *b = &udp_lab.bindings[dport];

  acquire(&b->lock);
  if (b->owner != myproc()) {
    release(&b->lock);
    return -1;
  }

  while (b->queue_head == 0) {
    sleep(b, &b->lock);
  } 
    
  struct udp_packet_queue *curr_head = b->queue_head;
  b->queue_head = curr_head->next;
  if (b->queue_head == 0)
    b->queue_tail = 0;
  b->count--;
  payload_len = curr_head->len;
  if (payload_len > maxlen)
    payload_len = maxlen;

  if (dport == 2009)
  
  copyout(p->pagetable, bufaddr, curr_head->data, payload_len);
  copyout(p->pagetable, ip_src_addr, (char *)&curr_head->src_ip, sizeof(uint32));
  copyout(p->pagetable, sport_ptr, (char *)&curr_head->src_port, sizeof(uint16));

  debug_count++;
  printf("SYS RECV debug_count: %d\n", debug_count);

  if (dport == 2009){
    printf("2009 DPORT SYS_REC\n");
    printf("2009: IPRX: len %d; data= ", payload_len);
    for (int i = 0; i < payload_len; i++)
      printf("%c", curr_head->data[i]); 
    printf("\n");
  }
  kfree_total++;
  kfree_SYSRECV++;
  printf("(%d) FREE SYS_RECV: %p\n", kfree_SYSRECV, curr_head);
  kfree(curr_head);
  release(&b->lock); 
  return payload_len;
}

// This code is lifted from FreeBSD's ping.c, and is copyright by the Regents
// of the University of California.
static unsigned short
in_cksum(const unsigned char *addr, int len)
{
  int nleft = len;
  const unsigned short *w = (const unsigned short *)addr;
  unsigned int sum = 0;
  unsigned short answer = 0;

  /*
   * Our algorithm is simple, using a 32 bit accumulator (sum), we add
   * sequential 16 bit words to it, and at the end, fold back all the
   * carry bits from the top 16 bits into the lower 16 bits.
   */
  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  /* mop up an odd byte, if necessary */
  if (nleft == 1) {
    *(unsigned char *)(&answer) = *(const unsigned char *)w;
    sum += answer;
  }

  /* add back carry outs from top 16 bits to low 16 bits */
  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);
  /* guaranteed now that the lower 16 bits of sum are correct */

  answer = ~sum; /* truncate to 16 bits */
  return answer;
}

//
// send(int sport, int dst, int dport, char *buf, int len)
//
uint64
sys_send(void)
{
  struct proc *p = myproc();
  int sport;
  int dst;
  int dport;
  uint64 bufaddr;
  int len;

  argint(0, &sport);
  argint(1, &dst);
  argint(2, &dport);
  argaddr(3, &bufaddr);
  argint(4, &len);

  int total = len + sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp);
  if(total > PGSIZE)
    return -1;

  char *buf = kalloc();
  if(buf == 0){
    printf("sys_send: kalloc failed\n");
    return -1;
  }
  memset(buf, 0, PGSIZE);

  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, host_mac, ETHADDR_LEN);
  memmove(eth->shost, local_mac, ETHADDR_LEN);
  eth->type = htons(ETHTYPE_IP);

  struct ip *ip = (struct ip *)(eth + 1);
  ip->ip_vhl = 0x45; // version 4, header length 4*5
  ip->ip_tos = 0;
  ip->ip_len = htons(sizeof(struct ip) + sizeof(struct udp) + len);
  ip->ip_id = 0;
  ip->ip_off = 0;
  ip->ip_ttl = 100;
  ip->ip_p = IPPROTO_UDP;
  ip->ip_src = htonl(local_ip);
  ip->ip_dst = htonl(dst);
  ip->ip_sum = in_cksum((unsigned char *)ip, sizeof(*ip));

  struct udp *udp = (struct udp *)(ip + 1);
  udp->sport = htons(sport);
  udp->dport = htons(dport);
  udp->ulen = htons(len + sizeof(struct udp));

  char *payload = (char *)(udp + 1);
  if(copyin(p->pagetable, payload, bufaddr, len) < 0){
    kfree(buf);
    printf("send: copyin failed\n");
    return -1;
  }

  e1000_transmit(buf, total);

  return 0;
}

void dump_packet(char *buf, int len) {
  printf("== PACKET DUMP ==\n");
  for (int i = 0; i < len; i++) {
    if (i % 16 == 0)
      printf("%04x: ", i);
    printf("%02x ", (unsigned char)buf[i]);
    if (i % 16 == 15 || i == len - 1)
      printf("\n");
  }
}

void
ip_rx(char *buf, int len)
{
  // don't delete this printf; make grade depends on it.
  static int seen_ip = 0;
  if(seen_ip == 0)
    printf("ip_rx: received an IP packet\n");
  seen_ip = 1;
  
  //printf("== PACKET DUMP ==\n");
  //dump_packet(buf, len);

  struct eth *eth = (struct eth *) buf;

  struct ip *ipin = (struct ip *)(eth +1); //struct ip *ipin = (struct ip *)(buf + 14);    
  uint8 ip_protocol = ipin->ip_p;
  if (ip_protocol != IPPROTO_UDP){
    kfree_total++;
    kfree_PROTO++;
    printf("(%d) FREE IP - !IP_PROTO:  %p\n",  kfree_PROTO, buf);
    kfree(buf);
    return;
  }
  uint32 src_ip = ntohl(ipin->ip_src);
  struct udp *udpin = (struct udp *)(ipin + 1);//struct udp *udpin = (struct udp *)((char *)ipin + 20);
  uint16 dport = ntohs(udpin->dport);  
  uint16 sport = ntohs(udpin->sport);
  int payload_len = ntohs(udpin->ulen) - sizeof(struct udp);

  char *payload = (char *)(udpin) + 8;//sizeof(struct udp);

  if (dport == 2009) {
    printf("2009: IPRX: len %d -- data = ", payload_len);
    int rem = len - (sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp)); 
    for (int i = 0; i < rem; i++){
      char ch = payload[i];
      if (ch >= 32 && ch <= 126)
        printf("'%c'(0x%02x) ", ch, ch);
      else
        printf(".(0x%02x) ", ch);
    } 
    printf("ip_rx: buf=%p eth=%p ipin=%p udpin=%p payload=%p\n", buf, eth, ipin, udpin, payload);    
    printf("\n");
  }

  if(payload_len < 0 || payload_len > UDP_DATA_MAXLEN) {
    kfree_total++;
    kfree_PAYLOAD++;
    printf("(%d) FREE IP_PAYOLOAD:  %p\n", kfree_PAYLOAD, buf);
    kfree(buf);
    return;  
  } 

  struct udp_binding *b = &udp_lab.bindings[dport];
  acquire(&b->lock);            

    // checks if port is bound 
  if (!b->owner){
    kfree_total++;
    kfree_OWNER++;
    printf("(%d) FREE IP_OWNER:  %p\n", kfree_OWNER, buf);
    kfree(buf);
    release(&b->lock);
    return; 
  }

  if(b->count >= MAX_UDP_PACKETS) {
    kfree_total++;
    kfree_MAX_PACKETS++;
    printf("(%d) FREE IP_MAX_PACKETS:  %p\n", kfree_MAX_PACKETS, buf);
    kfree(buf);
    release(&b->lock);
    return;
  }
  
  struct udp_packet_queue *new_pack = (struct udp_packet_queue *) kalloc();
  kalloc_total++;

  printf("(%d) ALLOC IPRX PACKET QUEUE:  %p\n", kalloc_total, new_pack);
  if (!new_pack){
    kfree_total++;
    kfree_QUEUE++;
    printf("(%d) FREE IP: %p\n",kfree_QUEUE, buf);
    kfree(buf);
    release(&b->lock);
    return; 
  } 
  new_pack->next = 0;
  new_pack->src_ip = src_ip;
  new_pack->src_port = sport;
  new_pack->len = payload_len;
  memmove(new_pack->data, payload, payload_len);

  if (b->queue_tail)
    b->queue_tail->next = new_pack;  
  else
    b->queue_head = new_pack;        
  b->queue_tail = new_pack;
  b->count++;
  kfree_total++;
  kfree_IPSUCCESS++;
  printf("(%d)FREE IP IP_RX SUCCESS: %p\n", kfree_IPSUCCESS++, buf);
  kfree(buf);
  wakeup(b);
  release(&b->lock);
}


//
// send an ARP reply packet to tell qemu to map
// xv6's ip address to its ethernet address.
// this is the bare minimum needed to persuade
// qemu to send IP packets to xv6; the real ARP
// protocol is more complex.
//
void
arp_rx(char *inbuf)
{
  static int seen_arp = 0;

  if(seen_arp){
    kfree(inbuf);
    return;
  }
  printf("arp_rx: received an ARP packet\n");
  seen_arp = 1;

  struct eth *ineth = (struct eth *) inbuf;
  struct arp *inarp = (struct arp *) (ineth + 1);

  char *buf = kalloc();
  if(buf == 0)
    panic("send_arp_reply");
  
  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, ineth->shost, ETHADDR_LEN); // ethernet destination = query source
  memmove(eth->shost, local_mac, ETHADDR_LEN); // ethernet source = xv6's ethernet address = 
  eth->type = htons(ETHTYPE_ARP);

  struct arp *arp = (struct arp *)(eth + 1);
  arp->hrd = htons(ARP_HRD_ETHER);
  arp->pro = htons(ETHTYPE_IP);
  arp->hln = ETHADDR_LEN;
  arp->pln = sizeof(uint32);
  arp->op = htons(ARP_OP_REPLY);

  memmove(arp->sha, local_mac, ETHADDR_LEN);
  arp->sip = htonl(local_ip);
  memmove(arp->tha, ineth->shost, ETHADDR_LEN);
  arp->tip = inarp->sip;

  e1000_transmit(buf, sizeof(*eth) + sizeof(*arp));

  kfree(inbuf);
}

void
net_rx(char *buf, int len)
{
  struct eth *eth = (struct eth *) buf;

  if(len >= sizeof(struct eth) + sizeof(struct arp) &&
     ntohs(eth->type) == ETHTYPE_ARP){
    arp_rx(buf);
  } else if(len >= sizeof(struct eth) + sizeof(struct ip) &&
     ntohs(eth->type) == ETHTYPE_IP){
    ip_rx(buf, len);
  } else {
    kfree(buf);
  }
}
