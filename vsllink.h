#ifndef JML_VSLLINK_H
#define JML_VSLLINK_H

// Playing into VSL Mixer (~/code/vsl1818/VSLMixer) instead of out of jacks.
//
// With an AudioBox 1818VSL the box is a mixer, and VSL Mixer is what does
// the mixing.  Then jammer plays its main output into that mixer's inputs 1
// and 2 -- left on 1, right on 2 -- as if it had come in on those jacks, so
// it gets the mixer's EQ and compression and goes to every aux.  The other
// way needs nothing from here: jammer listens to the 1818's inputs 1 and 2
// itself, the way it listens to the Scarlett's, since any number of apps
// can read one device's inputs.
//
// Between the two apps is a ring in shared memory.  Jammer still opens the
// 1818 for output -- that's what keeps it on the box's clock, so the ring
// never drifts -- but writes silence to it, and its block to the ring.  VSL
// Mixer reads the ring a block or two behind, and only while the heartbeat
// is fresh, so jammer quitting is silence there rather than a stuck loop.
//
// Whenever the output is the 1818, then, and only then: the talkbox and
// everything else keep their own devices.
//
// The same layout is in VSLMixer/Engine/jammer_link.h; change both
// together, and bump VSL_LINK_MAGIC.

#include <fcntl.h>
#include <mach/mach_time.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define VSL_LINK_DEVICE "1818"
// VSL Mixer is sandboxed, and the sandbox only lets it share memory under
// an application group's name, which has to start with the team ID.
// shm_open names are at most 31 characters.
#define VSL_LINK_NAME "C58DMTY76R.jefftk.audio/link"
#define VSL_LINK_MAGIC 0x4a4d4c31u   // "JML1"
#define VSL_LINK_FRAMES 4096u        // power of two, for the mask

typedef struct {
  _Atomic uint32_t magic;
  _Atomic uint32_t block;        // frames in jammer's latest write
  _Atomic uint64_t write_pos;    // frames written, ever
  _Atomic uint64_t heartbeat;    // mach_absolute_time() of the latest write
  float audio[VSL_LINK_FRAMES][2];
} VslLink;

// Mapped once and kept: the audio thread may be reading it at any moment.
static VslLink* vsl_link;
static _Atomic bool vsl_link_active;

static VslLink* vsl_link_map(void) {
  int fd = shm_open(VSL_LINK_NAME, O_RDWR | O_CREAT, 0600);
  if (fd < 0) {
    printf("vsl link: couldn't open shared memory\n");
    return NULL;
  }
  // Whichever app gets here first sizes it; it can only be sized once.
  struct stat st;
  if (fstat(fd, &st) == 0 && st.st_size == 0) {
    ftruncate(fd, sizeof(VslLink));
  }
  if (fstat(fd, &st) != 0 || st.st_size < (off_t)sizeof(VslLink)) {
    printf("vsl link: shared memory is the wrong size\n");
    close(fd);
    return NULL;
  }
  void* p = mmap(NULL, sizeof(VslLink), PROT_READ | PROT_WRITE, MAP_SHARED,
                 fd, 0);
  close(fd);
  if (p == MAP_FAILED) return NULL;
  VslLink* link = (VslLink*)p;
  uint32_t expected = 0;
  atomic_compare_exchange_strong(&link->magic, &expected, VSL_LINK_MAGIC);
  if (atomic_load(&link->magic) != VSL_LINK_MAGIC) {
    printf("vsl link: VSL Mixer is a different version; not linking\n");
    munmap(p, sizeof(VslLink));
    return NULL;
  }
  return link;
}

// Before the synth opens `device`, by the name it opens it by, so not even
// its first block goes to the 1818's jacks.  Main thread.
static void vsl_link_choose(const char* device) {
  bool on = device && strstr(device, VSL_LINK_DEVICE) != NULL;
  if (on && !vsl_link) vsl_link = vsl_link_map();
  atomic_store_explicit(&vsl_link_active, on && vsl_link,
                        memory_order_release);
}

// Last thing on the audio thread, once the block is final: into the ring,
// and silence to the jacks.
static void vsl_link_push(float** out, int nout, int len) {
  if (!atomic_load_explicit(&vsl_link_active, memory_order_acquire)) return;
  if (nout < 2 || len <= 0) return;
  VslLink* link = vsl_link;
  uint64_t w = atomic_load_explicit(&link->write_pos, memory_order_relaxed);
  for (int i = 0; i < len; i++) {
    float* frame = link->audio[(w + (uint64_t)i) & (VSL_LINK_FRAMES - 1)];
    frame[0] = out[0][i];
    frame[1] = out[1][i];
  }
  atomic_store_explicit(&link->block, (uint32_t)len, memory_order_relaxed);
  atomic_store_explicit(&link->write_pos, w + (uint64_t)len,
                        memory_order_release);
  atomic_store_explicit(&link->heartbeat, mach_absolute_time(),
                        memory_order_release);
  for (int i = 0; i < nout; i++) {
    memset(out[i], 0, (size_t)len * sizeof(float));
  }
}

#endif
