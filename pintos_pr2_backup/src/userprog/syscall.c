#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "devices/timer.h"
#include "filesys/file.h"

struct lock lock;

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&lock);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  void *sp = (void*)f->esp;
  check_address(sp);
  int syscall_n = *(int*)f->esp;
  char **arg = (char**)malloc(10*sizeof(char*));
/*

  0 : halt
  1 : exit
  2 : exec
  3 : wait
  4 : create
  5 : remove
  6 : open
  7 : file size
  8 : read
  9 : write
  10: seek
  11: tell
  12: close

*/
//  printf("syscall_n is %d\n", syscall_n);
//  hex_dump(sp, sp, PHYS_BASE - sp, true);
  switch(syscall_n){
	case SYS_HALT:
	  halt();
	break;
	case SYS_EXIT:
	  get_argument(sp, arg, 1);
      exit(arg[0]);
	break;
	case SYS_EXEC:
	  get_argument(sp, arg, 1);
//	  check_address((void*)arg[0]);
	  f->eax = exec((const char*)arg[0]);
	break;
	case SYS_WAIT:
      get_argument(sp, arg, 1);
	  f->eax = wait((tid_t) arg[0]);
	break;
	case SYS_CREATE:                 /* Create a file. */
	  get_argument(sp, arg, 2);
	  check_address(arg[0]);
	  f->eax = create((const char*)arg[0], (unsigned)arg[1]);
	break;
    case SYS_REMOVE:                 /* Delete a file. */
      get_argument(sp, arg, 1);
	  check_address(arg[0]);
	  f->eax = remove((const char *)arg[0]);
	break;
	case SYS_OPEN:                   /* Open a file. */
	  get_argument(sp, arg, 1);
	  check_address(arg[0]);
	  f->eax = open((const char *)arg[0]);
    break;
    case SYS_FILESIZE:               /* Obtain a file's size. */
	  get_argument(sp, arg, 1);
	  f->eax = filesize((int)arg[0]);
    break;
	case SYS_READ:                   /* Read from a file. */
	  get_argument(sp, arg, 3);
	  check_address(arg[1]);
	  f->eax = read((int)arg[0], (void*)arg[1], (unsigned)arg[2]);
    break;
	case SYS_WRITE:                  /* Write to a file. */
	  get_argument(sp, arg, 3);
	  check_address(arg[1]);
	  f->eax = write(arg[0], (void*)arg[1], (unsigned)arg[2]);
    break;
	case SYS_SEEK:                   /* Change position in a file. */
	  get_argument(sp, arg, 2);
	  seek((int)arg[0], (unsigned)arg[1]);
    break;
	case SYS_TELL:                   /* Report current position in a file. */

      get_argument(sp, arg, 1);
	  f->eax = tell((int)arg[0]);
    break;
	case SYS_CLOSE:
	  get_argument(sp, arg, 1);
	  close((int)arg[0]);
	break;
  }
  free(arg);
}

void check_address (void *addr)
{
  if(addr>=PHYS_BASE || addr <= 0x08048000){
	exit(-1);
  }
}

void get_argument (void *esp, char **arg, int count)
{
  int *ptr;
  int i=0;
  for(i=0; i<count; i++){
	ptr = (int*)esp+1+i;
	check_address((const void*)ptr);
	arg[i]=*ptr;
  }
//  hex_dump(esp, esp, PHYS_BASE - esp, true);

}

void halt(void)
{
  shutdown_power_off();
}

void exit(int status)
{
  thread_current()->exit_status = status;
  printf("%s: exit(%d)\n", thread_current()->name, status);
  thread_exit();
}

///
int wait(tid_t tid)
{
  int status =  process_wait(tid);

  return status;
}

tid_t exec(const char *cmd_line)
{
  tid_t tid;

  tid = process_execute(cmd_line);
  struct thread* child= get_child_process(tid);
  sema_down(&child->load);
  if(!child->load_status) return -1;
  return tid;
}

bool create(const char *file, unsigned initial_size)
{
  bool success = filesys_create(file, (off_t)initial_size);
  return success;
}

bool remove(const char *file)
{
  bool success = filesys_remove(file);
  return success;
}

int open (const char *file_name)
{
  struct file* file = filesys_open(file_name);
  if(file == NULL) return -1;
  int fd = process_add_file(file);
  //file_deny_write(file);
  return fd; 
}

int filesize (int fd)
{
  struct file* file = thread_current()->fd[fd];
  if(file==NULL) return -1;
  return file_length(file);
}

int read (int fd, void *buffer, unsigned size)
{
  int input;
  if(fd == 0){
	input=input_getc();
	return input;
  }

  lock_acquire(&lock);

  struct file* file = thread_current()->fd[fd];
  if(file==NULL){
	 lock_release(&lock);
	 return -1;
  }
  input=file_read (file, buffer, size);

  lock_release(&lock);

  return input;
}

int write(int fd, void *buffer, unsigned size)
{
//  printf("fd is %d\n", fd);
//  printf("thread file index is %d\n", thread_current()->file_index);
  if(fd==1){
	putbuf(buffer, size);
	return size;
  }
  int output;

  lock_acquire(&lock);
  struct file* file = thread_current()->fd[fd];
  if(file==NULL){
	 lock_release(&lock);
     return -1;
  }
  
  output = file_write(file, buffer, size);
  lock_release(&lock);
  return output;
}

////
void seek (int fd, unsigned position)
{
  struct file* file = thread_current()->fd[fd];
  file_seek(file, position);
}
///
unsigned tell (int fd)
{
  struct file* file = thread_current()->fd[fd];
  if(file==NULL) return -1;
  return file_tell(file);
}
///
void close (int fd)
{
//  file_allow_write(thread_current()->fd[fd]);
  process_close_file(fd);
}
