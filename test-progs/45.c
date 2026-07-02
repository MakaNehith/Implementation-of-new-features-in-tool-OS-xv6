// raidtest.c
// Verifies RAID 0, RAID 1, and RAID 5 implementations.
//
// Tests performed:
//   1. RAID 0 (striping)        - data striped across 4 disks; correct read-back
//   2. RAID 1 (mirroring)       - data mirrored on pairs; failover to mirror on read
//   3. RAID 5 (parity striping) - data + parity across 4 disks; reconstruction on failure
//
// For RAID 5 reconstruction: we mark a disk as failed via setdiskfault(),
// write data, then read it back expecting XOR reconstruction to restore it.

#include "kernel/types.h"
#include "user/user.h"

#define SBRK_LAZY   0
#define SBRK_EAGER  1

// We use a moderate size to make multiple RAID stripes
#define ALLOC_PAGES  (900)
#define PAGE_SIZE    4096

static char *
lazy_alloc(int nbytes)
{
  return (char *)sbrklazy(nbytes);
}

static void
print_vmstats_short(void)
{
  struct vmstats s;
  getvmstats(getpid(), &s);
  printf("  [stats] disk_r=%d disk_w=%d faults=%d evicted=%d swapin=%d\n",
         s.disk_reads, s.disk_writes,
         s.page_faults, s.pages_evicted, s.pages_swapped_in);
}

// Write a deterministic pattern and verify it survives swap through disk.
// Returns number of mismatches (0 = pass).
static int
write_verify(char *mem, int npages, int salt)
{
  // Write
  for (int i = 0; i < npages; i++) {
    char *page = mem + (i * PAGE_SIZE);
    // Fill every byte of the page with a pattern
    for (int b = 0; b < PAGE_SIZE; b++)
      page[b] = (char)((i * 7 + b + salt) & 0xFF);
  }

  // Force earlier pages out of RAM by walking forward again with pressure:
  // a second forward pass re-accesses them, triggering swap-in.
  int mismatches = 0;
  for (int i = 0; i < npages; i++) {
    char *page = mem + (i * PAGE_SIZE);
    for (int b = 0; b < PAGE_SIZE; b++) {
      char expected = (char)((i * 7 + b + salt) & 0xFF);
      if (page[b] != expected) {
        mismatches++;
        if (mismatches <= 3)
          printf("    MISMATCH page=%d byte=%d got=%d exp=%d\n",
                 i, b, (int)(unsigned char)page[b], (int)(unsigned char)expected);
      }
    }
  }
  return mismatches;
}

// ----------------------------------------------------------------
// TEST: RAID 0 - Striping
// Each logical block b goes to disk (b % 4), offset (b / 4).
// No redundancy; all 4 disks must be healthy.
// ----------------------------------------------------------------
static void
test_raid0(void)
{
  printf("\n=== RAID 0: Striping ===\n");
  printf("Logical block b -> disk (b%%4), offset (b/4)\n");
  printf("No redundancy; verifying data integrity only.\n");

  if (setraidlevel(0) < 0) { printf("setraid(0) failed\n"); exit(1); }
  if (setdisksched(0)  < 0) { printf("setdisksched failed\n");    exit(1); }

  int pid = fork();
  if (pid < 0) { printf("fork failed\n"); exit(1); }
  if (pid == 0) {
    char *mem = lazy_alloc(ALLOC_PAGES * PAGE_SIZE);
    if (mem == (char *)-1) { printf("sbrk failed\n"); exit(2); }

    int mismatches = write_verify(mem, ALLOC_PAGES, 11);
    print_vmstats_short();
    if (mismatches == 0)
      printf("  PASS: RAID 0 - all %d pages verified\n", ALLOC_PAGES);
    else
      printf("  FAIL: RAID 0 - %d mismatches\n", mismatches);
    exit(mismatches == 0 ? 0 : 1);
  }
  int status; wait(&status);
}

// ----------------------------------------------------------------
// TEST: RAID 1 - Mirroring
// Logical block b -> primary disk (b%2)*2, mirror disk primary+1.
// On read, if primary is failed, mirror is used transparently.
// ----------------------------------------------------------------
static void
test_raid1_normal(void)
{
  printf("\n=== RAID 1: Mirroring (no failure) ===\n");
  printf("Logical block b -> disks { (b%%2)*2, (b%%2)*2+1 }\n");

  if (setraidlevel(1) < 0) { printf("setraid(1) failed\n"); exit(1); }

  int pid = fork();
  if (pid < 0) { printf("fork failed\n"); exit(1); }
  if (pid == 0) {
    char *mem = lazy_alloc(ALLOC_PAGES * PAGE_SIZE);
    if (mem == (char *)-1) { printf("sbrk failed\n"); exit(2); }

    int mismatches = write_verify(mem, ALLOC_PAGES, 22);
    print_vmstats_short();
    if (mismatches == 0)
      printf("  PASS: RAID 1 (no failure) - all pages correct\n");
    else
      printf("  FAIL: RAID 1 - %d mismatches\n", mismatches);
    exit(mismatches == 0 ? 0 : 1);
  }
  int status; wait(&status);
}

static void
test_raid1_with_failure(int failed_disk)
{
  printf("\n=== RAID 1: Mirroring (disk %d failed) ===\n", failed_disk);
  printf("Reads should transparently fall back to the mirror.\n");

  if (setraidlevel(1)          < 0) { printf("setraid(1) failed\n"); exit(1); }
  if (setfaileddisk(failed_disk)  < 0) { printf("setdiskfault failed\n");       exit(1); }

  int pid = fork();
  if (pid < 0) { printf("fork failed\n"); exit(1); }
  if (pid == 0) {
    char *mem = lazy_alloc(ALLOC_PAGES * PAGE_SIZE);
    if (mem == (char *)-1) { printf("sbrk failed\n"); exit(2); }

    // Write (only one disk of each pair is written since the other is "failed")
    for (int i = 0; i < ALLOC_PAGES; i++) {
      char *page = mem + (i * PAGE_SIZE);
      for (int b = 0; b < PAGE_SIZE; b++)
        page[b] = (char)((i * 13 + b + 33) & 0xFF);
    }

    // Verify (reads from mirror since primary may be failed)
    int mismatches = 0;
    for (int i = 0; i < ALLOC_PAGES; i++) {
      char *page = mem + (i * PAGE_SIZE);
      for (int b = 0; b < PAGE_SIZE; b++) {
        char expected = (char)((i * 13 + b + 33) & 0xFF);
        if (page[b] != expected) mismatches++;
      }
    }

    print_vmstats_short();
    if (mismatches == 0)
      printf("  PASS: RAID 1 failover - all pages reconstructed from mirror\n");
    else
      printf("  FAIL: RAID 1 failover - %d mismatches\n", mismatches);
    exit(mismatches == 0 ? 0 : 1);
  }
  int status; wait(&status);

  // Reset disk fail
  setfaileddisk(-1);  // -1 = no failure  (kernel clamps invalid to -1)
}

// ----------------------------------------------------------------
// TEST: RAID 5 - Striping with distributed parity
//
// Each stripe of 3 data blocks + 1 parity block spans all 4 disks.
// stripe  = logical_block / 3
// parity_disk = stripe % 4
// data_disk   = logical_block % 3  (adjusted past parity disk)
//
// Write path: new_parity = old_parity XOR old_data XOR new_data
// Read path (healthy): read directly from data disk
// Read path (failed):  XOR all other disks in stripe to reconstruct
// ----------------------------------------------------------------
static void
test_raid5_normal(void)
{
  printf("\n=== RAID 5: Parity striping (no failure) ===\n");
  printf("stripe = lb/3, parity_disk = stripe%%4\n");

  if (setraidlevel(5) < 0) { printf("setraid(5) failed\n"); exit(1); }

  int pid = fork();
  if (pid < 0) { printf("fork failed\n"); exit(1); }
  if (pid == 0) {
    char *mem = lazy_alloc(ALLOC_PAGES * PAGE_SIZE);
    if (mem == (char *)-1) { printf("sbrk failed\n"); exit(2); }

    int mismatches = write_verify(mem, ALLOC_PAGES, 44);
    print_vmstats_short();
    if (mismatches == 0)
      printf("  PASS: RAID 5 (no failure) - all pages correct\n");
    else
      printf("  FAIL: RAID 5 - %d mismatches\n", mismatches);
    exit(mismatches == 0 ? 0 : 1);
  }
  int status; wait(&status);
}

static void
test_raid5_reconstruction(int failed_disk)
{
  printf("\n=== RAID 5: Reconstruction with disk %d failed ===\n", failed_disk);
  printf("Reads to blocks on disk %d must be rebuilt via XOR of the other 3 disks.\n",
         failed_disk);

  if (setraidlevel(5)         < 0) { printf("setraid(5) failed\n"); exit(1); }
  if (setfaileddisk(failed_disk) < 0) { printf("setdiskfault failed\n");      exit(1); }

  int pid = fork();
  if (pid < 0) { printf("fork failed\n"); exit(1); }
  if (pid == 0) {
    char *mem = lazy_alloc(ALLOC_PAGES * PAGE_SIZE);
    if (mem == (char *)-1) { printf("sbrk failed\n"); exit(2); }

    // Write: the kernel skips writes to the failed disk and handles parity accordingly
    for (int i = 0; i < ALLOC_PAGES; i++) {
      char *page = mem + (i * PAGE_SIZE);
      for (int b = 0; b < PAGE_SIZE; b++)
        page[b] = (char)((i * 17 + b + 55) & 0xFF);
    }

    // Read back: kernel reconstructs any block that landed on failed_disk
    int mismatches = 0;
    for (int i = 0; i < ALLOC_PAGES; i++) {
      char *page = mem + (i * PAGE_SIZE);
      for (int b = 0; b < PAGE_SIZE; b++) {
        char expected = (char)((i * 17 + b + 55) & 0xFF);
        if (page[b] != expected) {
          mismatches++;
          if (mismatches <= 3)
            printf("    MISMATCH page=%d byte=%d got=%d exp=%d\n",
                   i, b, (int)(unsigned char)page[b], (int)(unsigned char)expected);
        }
      }
    }

    print_vmstats_short();
    if (mismatches == 0)
      printf("  PASS: RAID 5 reconstruction (disk %d failed) - all data restored\n",
             failed_disk);
    else
      printf("  FAIL: RAID 5 reconstruction - %d mismatches\n", mismatches);
    exit(mismatches == 0 ? 0 : 1);
  }
  int status; wait(&status);

  // Reset
  setfaileddisk(-1);
}

// ----------------------------------------------------------------
// Block-to-disk mapping walkthrough (printed explanation)
// ----------------------------------------------------------------
static void
explain_raid_mappings(void)
{
  printf("\n=== RAID Mapping Walkthrough ===\n");

  printf("\nRAID 0 (4 disks, striping):\n");
  printf("  logical_block -> disk (lb%%4), offset (lb/4)\n");
  for (int lb = 0; lb < 8; lb++)
    printf("  lb=%d  disk=%d  offset=%d\n", lb, lb % 4, lb / 4);

  printf("\nRAID 1 (2 mirror pairs: {disk0,disk1}, {disk2,disk3}):\n");
  printf("  logical_block -> primary disk (lb%%2)*2, mirror primary+1\n");
  for (int lb = 0; lb < 6; lb++) {
    int pri = (lb % 2) * 2;
    printf("  lb=%d  primary=disk%d  mirror=disk%d  offset=%d\n",
           lb, pri, pri + 1, lb / 2);
  }

  printf("\nRAID 5 (4 disks, rotating parity):\n");
  printf("  stripe = lb/3,  parity_disk = stripe%%4\n");
  printf("  data_disk  = lb%%3 (adjusted: if >= parity_disk, +1)\n");
  for (int lb = 0; lb < 12; lb++) {
    int stripe = lb / 3;
    int parity = stripe % 4;
    int dsk    = lb % 3;
    if (dsk >= parity) dsk++;
    printf("  lb=%d  stripe=%d  parity_disk=%d  data_disk=%d  offset=%d\n",
           lb, stripe, parity, dsk, stripe);
  }
}

int
main(void)
{
  printf("==========================================\n");
  printf("  PA4 RAID Verification Test\n");
  printf("==========================================\n");

  explain_raid_mappings();

  // RAID 0
  test_raid0();

  // RAID 1
  test_raid1_normal();
  test_raid1_with_failure(0);   // disk 0 is the primary of pair {0,1}
  test_raid1_with_failure(2);   // disk 2 is the primary of pair {2,3}

  // RAID 5
  test_raid5_normal();
  test_raid5_reconstruction(0);  // failed disk = 0
  test_raid5_reconstruction(1);  // failed disk = 1
  test_raid5_reconstruction(2);  // failed disk = 2
  test_raid5_reconstruction(3);  // failed disk = 3

  printf("\n==========================================\n");
  printf("  raidtest done.\n");
  printf("==========================================\n");
  exit(0);
}