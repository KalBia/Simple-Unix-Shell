#include "shell.h"

typedef struct proc {
  pid_t pid;    /* process identifier */
  int state;    /* RUNNING or STOPPED or FINISHED */
  int exitcode; /* -1 if exit status not yet received */
} proc_t;

typedef struct job {
  pid_t pgid;            /* 0 if slot is free */
  proc_t *proc;          /* array of processes running in as a job */
  struct termios tmodes; /* saved terminal modes */
  int nproc;             /* number of processes */
  int state;             /* changes when live processes have same state */
  char *command;         /* textual representation of command line */
} job_t;

static job_t *jobs = NULL;          /* array of all jobs */
static int njobmax = 1;             /* number of slots in jobs array */
static int tty_fd = -1;             /* controlling terminal file descriptor */
static struct termios shell_tmodes; /* saved shell terminal modes */

static void sigchld_handler(int sig) {
  int old_errno = errno;
  pid_t pid;
  int status;
  /* TODO: Change state (FINISHED, RUNNING, STOPPED) of processes and jobs.
   * Bury all children that finished saving their status in jobs. */
#ifdef STUDENT

  /* receive all the signals from processess
   * WNOHANG - return if no more signals
   * WUNTRACED - receive stop signals
   * WCONTINUED - receive continue signals */
  while ((pid = waitpid(WAIT_ANY, &status, WNOHANG | WUNTRACED | WCONTINUED)) >
         0) {
    /* job by job */
    for (int j = FG; j < njobmax; j++) {
      job_t *job = &jobs[j];
      if (job->pgid == 0) /* free slot - ignore it */
        continue;

      /* variables to check if we need to change the job's status */
      bool changeForRunning =
        false; /* Any running process? == status->RUNNING */
      bool changeForStopped = false; /* Any stopped process? AND none running
                                        processes == status->STOPPED*/

      for (int i = 0; i < job->nproc; i++) /* process by process */
      {
        if (job->proc[i].state ==
            FINISHED) /* we don't need to do anything with it anymore*/
          continue;

        if (job->proc[i].pid ==
            pid) /* our process from waitpid that we need to change */
        {
          job->proc[i].exitcode = -1; /* forewarned is forearmed */

          if (WIFEXITED(status)) /* if terminated normally */
          {
            job->proc[i].state = FINISHED;
            job->proc[i].exitcode = WEXITSTATUS(status);
          } else if (WIFSIGNALED(status)) /* if killed by signal */
          {
            job->proc[i].state = FINISHED;
            job->proc[i].exitcode = WTERMSIG(status);
          } else if (WIFSTOPPED(status)) /* if stopped */
          {
            job->proc[i].state = STOPPED;
          } else if (WIFCONTINUED(status)) /* if continued */
          {
            job->proc[i].state = RUNNING;
          }
        }

        if (job->proc[i].state == RUNNING)
          changeForRunning = true;
        else if (job->proc[i].state == STOPPED)
          changeForStopped = true;
      }

      if (changeForRunning) /* we have at least one running process in job */
        job->state = RUNNING;
      else if (changeForStopped) /* we don't have any running process in job,
                                    but at least one is stopped */
        job->state = STOPPED;
      else /* we don't have any running or stopped process == all are finished
            */
        job->state = FINISHED;
    }
  }

  (void)status;
  (void)pid;
#endif /* !STUDENT */
  errno = old_errno;
}

/* When pipeline is done, its exitcode is fetched from the last process. */
static int exitcode(job_t *job) {
  return job->proc[job->nproc - 1].exitcode;
}

static int allocjob(void) {
  /* Find empty slot for background job. */
  for (int j = BG; j < njobmax; j++)
    if (jobs[j].pgid == 0)
      return j;

  /* If none found, allocate new one. */
  jobs = realloc(jobs, sizeof(job_t) * (njobmax + 1));
  memset(&jobs[njobmax], 0, sizeof(job_t));
  return njobmax++;
}

static int allocproc(int j) {
  job_t *job = &jobs[j];
  job->proc = realloc(job->proc, sizeof(proc_t) * (job->nproc + 1));
  return job->nproc++;
}

int addjob(pid_t pgid, int bg) {
  int j = bg ? allocjob() : FG;
  job_t *job = &jobs[j];
  /* Initial state of a job. */
  job->pgid = pgid;
  job->state = RUNNING;
  job->command = NULL;
  job->proc = NULL;
  job->nproc = 0;
  job->tmodes = shell_tmodes;
  return j;
}

static void deljob(job_t *job) {
  assert(job->state == FINISHED);
  free(job->command);
  free(job->proc);
  job->pgid = 0;
  job->command = NULL;
  job->proc = NULL;
  job->nproc = 0;
}

static void movejob(int from, int to) {
  assert(jobs[to].pgid == 0);
  memcpy(&jobs[to], &jobs[from], sizeof(job_t));
  memset(&jobs[from], 0, sizeof(job_t));
}

static void mkcommand(char **cmdp, char **argv) {
  if (*cmdp)
    strapp(cmdp, " | ");

  for (strapp(cmdp, *argv++); *argv; argv++) {
    strapp(cmdp, " ");
    strapp(cmdp, *argv);
  }
}

void addproc(int j, pid_t pid, char **argv) {
  assert(j < njobmax);
  job_t *job = &jobs[j];

  int p = allocproc(j);
  proc_t *proc = &job->proc[p];
  /* Initial state of a process. */
  proc->pid = pid;
  proc->state = RUNNING;
  proc->exitcode = -1;
  mkcommand(&job->command, argv);
}

/* Returns job's state.
 * If it's finished, delete it and return exitcode through statusp. */
static int jobstate(int j, int *statusp) {
  assert(j < njobmax);
  job_t *job = &jobs[j];
  int state = job->state;

  /* TODO: Handle case where job has finished. */
#ifdef STUDENT

  if (state == FINISHED) {
    *statusp = exitcode(job); /* get the job's status */
    deljob(job);              /* clean up the job */
  }

  (void)exitcode;
#endif /* !STUDENT */

  return state;
}

char *jobcmd(int j) {
  assert(j < njobmax);
  job_t *job = &jobs[j];
  return job->command;
}

/* Continues a job that has been stopped. If move to foreground was requested,
 * then move the job to foreground and start monitoring it. */
bool resumejob(int j, int bg, sigset_t *mask) {
  if (j < 0) {
    for (j = njobmax - 1; j > 0 && jobs[j].state == FINISHED; j--)
      continue;
  }

  if (j >= njobmax || jobs[j].state == FINISHED)
    return false;

    /* TODO: Continue stopped job. Possibly move job to foreground slot. */
#ifdef STUDENT

  job_t *job = &jobs[j];
  job->state = RUNNING;

  /* Explanation:
   * negative pgid sends signal to all processes in group
   * SIGCONT - signal to continue process */

  /* foreground job */
  if (!bg) {
    movejob(j, 0);

    /* set as foreground process group (for example for cat - it would stop
     * again immediately otherwise)*/
    setfgpgrp(jobs[0].pgid);
    Tcsetattr(tty_fd, 0, &shell_tmodes);

    Kill(-jobs[0].pgid, SIGCONT);

    msg("[%d] continue '%s'\n", j, jobcmd(0));
    (void)monitorjob(mask);
  } else {
    Kill(-(job->pgid), SIGCONT);
    msg("[%d] continue '%s'\n", j, jobcmd(j));
  }

  (void)movejob;
#endif /* !STUDENT */

  return true;
}

/* Kill the job by sending it a SIGTERM. */
bool killjob(int j) {
  if (j >= njobmax || jobs[j].state == FINISHED)
    return false;
  debug("[%d] killing '%s'\n", j, jobs[j].command);

  /* TODO: I love the smell of napalm in the morning. */
#ifdef STUDENT

  job_t *job = &jobs[j];

  /* negative pgid - kill all processes in group */
  Kill(-(job->pgid), SIGTERM);
  if (job->state == STOPPED)
    Kill(-(job->pgid), SIGCONT); /* process has to be concious, to be killed */

#endif /* !STUDENT */

  return true;
}

/* Report state of requested background jobs. Clean up finished jobs. */
void watchjobs(int which) {
  for (int j = BG; j < njobmax; j++) {
    if (jobs[j].pgid == 0)
      continue;

      /* TODO: Report job number, state, command and exit code or signal. */
#ifdef STUDENT

    int exitcode;
    char *cmd = NULL;
    strapp(
      &cmd,
      jobcmd(
        j)); /* jobstate deletes job, so we need to remember it somewhere */
    int status =
      jobstate(j, &exitcode); /* we clean up finished jobs on the fly */

    if ((which == ALL) || (which == status)) {
      if (status == RUNNING)
        msg("[%d] running '%s'\n", j, cmd);
      else if (status == STOPPED)
        msg("[%d] suspended '%s'\n", j, cmd);
      else if (WIFEXITED(exitcode))
        msg("[%d] exited '%s', status=%d\n", j, cmd, exitcode);
      else {
        if (strcmp(cmd, "false") ==
            0) /* only for 'false' we want to write something else - otherwise
                  signal 1 is SIHUP */
          msg("[%d] exited '%s', status=%d\n", j, cmd, WTERMSIG(exitcode));
        else
          msg("[%d] killed '%s' by signal %d\n", j, cmd, WTERMSIG(exitcode));
      }
    }

    free(cmd);

    (void)deljob;
#endif /* !STUDENT */
  }
}

/* Monitor job execution. If it gets stopped move it to background.
 * When a job has finished or has been stopped move shell to foreground. */
int monitorjob(sigset_t *mask) {
  int exitcode = 0, state;

  /* TODO: Following code requires use of Tcsetpgrp of tty_fd. */
#ifdef STUDENT

  job_t *job = &jobs[0];

  /* foreground job */
  pid_t shell_pid = getpgrp();
  setfgpgrp(job->pgid);

  /* wait untill it is finished or stopped - we clean up on the fly if finished
   */
  state = jobstate(0, &exitcode);
  while (state != FINISHED && state != STOPPED) {
    Sigsuspend(mask);
    state = jobstate(0, &exitcode);
  }

  /* move to background */
  if (state == STOPPED) {
    int new_j = allocjob();
    movejob(0, new_j);
  }

  setfgpgrp(shell_pid);

  (void)jobstate;
  (void)exitcode;
  (void)state;
#endif /* !STUDENT */

  return exitcode;
}

/* Called just at the beginning of shell's life. */
void initjobs(void) {
  struct sigaction act = {
    .sa_flags = SA_RESTART,
    .sa_handler = sigchld_handler,
  };

  /* Block SIGINT for the duration of `sigchld_handler`
   * in case `sigint_handler` does something crazy like `longjmp`. */
  sigemptyset(&act.sa_mask);
  sigaddset(&act.sa_mask, SIGINT);
  Sigaction(SIGCHLD, &act, NULL);

  jobs = calloc(sizeof(job_t), 1);

  /* Assume we're running in interactive mode, so move us to foreground.
   * Duplicate terminal fd, but do not leak it to subprocesses that execve. */
  assert(isatty(STDIN_FILENO));
  tty_fd = Dup(STDIN_FILENO);
  fcntl(tty_fd, F_SETFD, FD_CLOEXEC);

  /* Take control of the terminal. */
  Tcsetpgrp(tty_fd, getpgrp());

  /* Save default terminal attributes for the shell. */
  Tcgetattr(tty_fd, &shell_tmodes);
}

/* Called just before the shell finishes. */
void shutdownjobs(void) {
  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  /* TODO: Kill remaining jobs and wait for them to finish. */
#ifdef STUDENT

  for (int j = FG; j < njobmax; j++) {
    job_t *job = &jobs[j];

    if (job->pgid == 0 || job->state == FINISHED)
      continue;

    /* we kind of do monitorjob but simplier */
    if (j > FG)
      setfgpgrp(job->pgid);

    (void)killjob(j);

    /* we don't need to use jobstate-loop here, 'casuse we do watchjobs later*/
    while (job->state != FINISHED)
      Sigsuspend(&mask);

    if (j > FG)
      setfgpgrp(getpgrp()); /* give the power to the terminal again */
  }

#endif /* !STUDENT */

  watchjobs(FINISHED);

  Sigprocmask(SIG_SETMASK, &mask, NULL);

  Close(tty_fd);
}

/* Sets foreground process group to `pgid`. */
void setfgpgrp(pid_t pgid) {
  Tcsetpgrp(tty_fd, pgid);
}
