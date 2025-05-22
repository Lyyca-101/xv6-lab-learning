struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?modified by virtio disk driver
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  uint timestamp;   //struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];
};

