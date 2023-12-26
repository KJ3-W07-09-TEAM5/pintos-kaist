/* Reads a 128 kB file into static data and "sorts" the bytes in
   it, using counting sort, a single-pass algorithm.  The sorted
   data is written back to the same file in-place. */

#include <debug.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

const char *test_name = "child-sort";

unsigned char buf[128 * 1024];
size_t histogram[256];

int
main (int argc UNUSED, char *argv[]) 
{
#ifdef DEBUG
  printf("(child-sort) main(), argv[1]: %s\n", argv[1]);
#endif
  int handle;
  unsigned char *p;
  size_t size;
  size_t i;

  quiet = true;

  CHECK ((handle = open (argv[1])) > 1, "open \"%s\"", argv[1]);

  size = read (handle, buf, sizeof buf);
  for (i = 0; i < size; i++)
    histogram[buf[i]]++;
  p = buf;
  for (i = 0; i < sizeof histogram / sizeof *histogram; i++) 
    {
      size_t j = histogram[i];
      while (j-- > 0)
        *p++ = i;
    }
#ifdef DEBUG
  printf("(child-sort) read(), size: %d\n", size);
#endif
  seek (handle, 0);
#ifdef DEBUG
  printf("(child-sort) seek()\n");
#endif
  write (handle, buf, size);
#ifdef DEBUG
  printf("(child-sort) write(), buf(%p): %s\n", buf, buf);
#endif
  close (handle);
#ifdef DEBUG
  printf("(child-sort) close(). exiting...\n");
#endif
  
  return 123;
}
