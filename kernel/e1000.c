#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

// guess 32 buffers are enough for packets recpetion
#define RX_RING_SIZE 32
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_tx_lock;
struct spinlock e1000_rx_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_tx_lock, "e1000_tx");
  initlock(&e1000_rx_lock, "e1000_rx");


  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  /* 
    [E1000 3.2.7.1.1 Receive Interrupt Delay Timer / Packet Timer (RDTR)]
    Setting the Packet Timer to 0b disables both the Packet Timer
    and the Absolute Timer(RADV) and causes the Receive Timer Interrupt
    to be generated whenever a new packet has been stored in memory.
  */
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  // [E1000 13.4.20 Interrupt Mask Set/Read Register]
  // 7b of IMS: RXT0: Sets mask for Receiver Timer Interrupt
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}


int
e1000_transmit(struct mbuf *m)
{
  //
  // Your code here.
  //
  // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash(or Store)
  // a pointer so that it can be freed after sending.
  // we also assume every packet needs only one tx_desc
  
  // [E1000 3.4]
  // The process of checking for completed packets consists of one of the following:
  // 1.Scan memory for descriptor status write-backs.
  // 2 Take an interrupt. An interrupt condition [E1000 3.4.3]
  //   can be generated whenever a transmit queue goes empty (ICR.TXQE). 
  //   Interrupts can also be triggered in other ways.
  // we will use the first way to check completed packet
  int tail = regs[E1000_TDT];
  struct tx_desc *desc = &tx_ring[tail];

  if((desc->cmd & E1000_TXD_CMD_RS) && !(desc->status & E1000_TXD_STAT_DD)){
    // transmission is not completed
    return -1;
  }

  acquire(&e1000_tx_lock);

  //printf("[E1000]: transmit begins\n");

  // at this point,we have a done tx_desc,or a first-use tx_desc
  // the RS bit of latter is not set
  if(desc->cmd & E1000_TXD_CMD_RS)
    mbuffree(tx_mbufs[tail]);

  desc->addr = (uint64)m->head;
  desc->length = m->len;
  desc->cmd = E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;
  desc->status &= ~E1000_TXD_STAT_DD;
  tx_mbufs[tail] = m;

  regs[E1000_TDT] = (tail + 1) % TX_RING_SIZE;

  //printf("[E1000]: transmit ends\n");

  release(&e1000_tx_lock);

  return 0;
}

static int
has_valid_packet() {
  // initial state after configuration
  // regs[E1000_RDH] = 0,regs[E1000_RDT] = RX_RING_SIZE - 1
  // by that time,there is no valid packet for software
  // and (regs[E1000_RDT] + 1) % RX_RING_SIZE = regs[E1000_RDH] = 0
  return (regs[E1000_RDT] + 1) % RX_RING_SIZE != regs[E1000_RDH];
}

static void
e1000_recv(void)
{
  //
  // Your code here.
  // You'll need locks to cope with the possibility that
  // xv6 might use the E1000 from more than one process,
  // or might be using the E1000 in a kernel thread when 
  // an interrupt arrives.
  //
  // Check for PACKETS that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  
  // how do I know there are some desc waiting to be processed
  // when has_valid_packet = 0
  // that means (tail + 1) % RX_RING_SZ == head
  // tail and head are just "side by side"
  // let's assume that a packet is not larger than a mbuf
  while(has_valid_packet()){
    //printf("[E1000]: reception\n");
    acquire(&e1000_rx_lock);
    uint32 tail = (regs[E1000_RDT] + 1) % RX_RING_SIZE;
    struct rx_desc *desc = &rx_ring[tail];
    // headroom is 0
    // addr is the pos of buf,-20 make it point to the start of this mbuf
    // 8 + 8 + 4 = 20
    struct mbuf *m = (struct mbuf *)((char *)desc->addr - MBUF_BEFORE_BUF);

    if(!(desc->status & E1000_RXD_STAT_DD) || !(desc-> status & E1000_RXD_STAT_EOP)){
      // this packet is not ready
      // for now,multi-desc packet is not supported
      if((desc->status & E1000_RXD_STAT_DD)  && !(desc-> status & E1000_RXD_STAT_EOP)){
        //printf("[e1000 driver]: cannot handle multi-desc packet.\n");
      }
      break;
    }

    // now the driver get a valid packet
    m->len = desc->length;

    net_rx(m);

    // after sending packet,allocate new space for this rx_mbuf
    rx_mbufs[tail] = mbufalloc(0);
    if (!rx_mbufs[tail])
      panic("e1000");
    desc->addr = (uint64) rx_mbufs[tail]->head;
    desc->status = 0;
    // Finally, update the E1000_RDT register 
    // to be the index of the last ring descriptor processed.
    regs[E1000_RDT] = tail;
    release(&e1000_rx_lock);
  }
  //printf("[E1000]: reception out\n");
}


// See [E1000 3.2.7 Receive Interrupts]
void
e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  // [E1000  13.4.17 Interrupt Cause Read Register]
  // Writing a 1b to any bit in the register also clears that bit.
  regs[E1000_ICR] = 0xffffffff;

  // process thoese rx desc
  //printf("[E1000]: intr\n");
  e1000_recv();
  //printf("[E1000]: intr out\n");
}
