#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int p1[2], p2[2];// 每个管道都是单向传输
  char buf[1];

  if(pipe(p1) < 0 || pipe(p2) < 0){
    fprintf(2, "pipe failed\n");
    exit(1);
  }

  int pid = fork();
  if(pid < 0){
    fprintf(2, "fork failed\n");
    exit(1);
  }

  if(pid == 0){
    // Child process 子进程要求从管道1读，从管道2写
    close(p1[1]); // Close write end of pipe 1
    close(p2[0]); // Close read end of pipe 2

    if(read(p1[0], buf, 1) != 1){
      fprintf(2, "child read failed\n");
      exit(1);
    }
    printf("%d: received ping\n", getpid());
    
    if(write(p2[1], buf, 1) != 1){
      fprintf(2, "child write failed\n");
      exit(1);
    }

    close(p1[0]);
    close(p2[1]);
    exit(0);
  } else {
    // Parent process 父进程要求从管道2读，从管道1写
    close(p1[0]); // Close read end of pipe 1
    close(p2[1]); // Close write end of pipe 2

    buf[0] = 'x';
    if(write(p1[1], buf, 1) != 1){
      fprintf(2, "parent write failed\n");
      exit(1);
    }

    if(read(p2[0], buf, 1) != 1){
      fprintf(2, "parent read failed\n");
      exit(1);
    }
    printf("%d: received pong\n", getpid());

    close(p1[1]);
    close(p2[0]);
    wait(0);
    exit(0);
  }
}
